#include <byteswap.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <poll.h>
#include <unistd.h>

#include <arpa/inet.h>


/*
 * logging
 */
#define PRINT_PRIMASK 0x07
#define PRINT_PROMPT  0x10

void __attribute__((format(printf, 2, 3)))
print(int flags, const char *format, ...)
{
    char buffer[4096];
    int  bufflen;

    va_list vlist;
    va_start(vlist, format);
    bufflen = vsnprintf(buffer, sizeof(buffer), format, vlist);
    va_end(vlist);

    FILE *outfile = ((flags & PRINT_PRIMASK) <= LOG_ERR) ? stderr : stdout;
    char *outform = (flags & PRINT_PROMPT) ? "%s" : "%s\n";

    fprintf(outfile, outform, buffer);
    fflush(outfile);
}
#define printe(format, ...) print(LOG_ERR, format, ##__VA_ARGS__)
#define printw(format, ...) print(LOG_WARNING, format, ##__VA_ARGS__)
#define printi(format, ...) print(LOG_INFO, format, ##__VA_ARGS__)
#define printd(format, ...) print(LOG_DEBUG, format, ##__VA_ARGS__)
#define printp(format, ...) print(LOG_INFO | PRINT_PROMPT, format, ##__VA_ARGS__)

/*
 * network
 */
typedef enum nstate_e {
    NSTATE_DOWN,
    NSTATE_CONNECTED,
    NSTATE_LISTENED,
    NSTATE_ACCEPTED
} nstate_t;
#define NSTATE_IS_DOWN(nstate)         (nstate == NSTATE_DOWN)
#define NSTATE_IS_CONNECTED(nstate)    (nstate == NSTATE_CONNECTED)
#define NSTATE_IS_LISTEN(nstate)       (nstate == NSTATE_LISTENED)
#define NSTATE_IS_ACCEPTED(nstate)     (nstate == NSTATE_ACCEPTED)
#define NSTATE_IS_ESTABLISHED(nstate)  (nstate == NSTATE_CONNECTED || nstate == NSTATE_ACCEPTED)

typedef struct nsock_s {
    struct sockaddr_in addr;
    int                fd;
} nsock_t;
typedef struct network_s {
    nsock_t  sock_conn;
    nsock_t  sock_list;
    nsock_t  sock_accept;
    nstate_t state;
} network_t;

char *
nstate_stringify(nstate_t state)
{
    switch(state) {
        case NSTATE_DOWN:      return "DOWN";
        case NSTATE_CONNECTED: return "CONNECTED";
        case NSTATE_LISTENED:  return "LISTENED";
        case NSTATE_ACCEPTED:  return "ACCEPTED";
        default:               return "UNKNOWN";
    }
}

int
nstate_change(nstate_t tostate, network_t *network, struct pollfd *pollfd, char *addr, char *port)
{
    if (NSTATE_IS_DOWN(network->state) && NSTATE_IS_LISTEN(tostate)) {
        /*
         * setup LISTEN socket
         */
        nsock_t *lsock = &network->sock_list;

        lsock->fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (lsock->fd < 0) {
            printe("%s -> %s: %s", nstate_stringify(network->state), nstate_stringify(tostate),
                   "Can't create listen socket");
            return -1;
        }
        int sockoption = 1;
        setsockopt(lsock->fd, SOL_SOCKET, SO_REUSEADDR, &sockoption, sizeof(sockoption));

        lsock->addr.sin_family = AF_INET;
        lsock->addr.sin_port   = htons(atoi(port));
        if (inet_aton(addr, &lsock->addr.sin_addr) == 0) {
            printe("%s -> %s: %s %s", nstate_stringify(network->state), nstate_stringify(tostate),
                   "Bad listen addr: ", addr);
            close(lsock->fd);
            lsock->fd = -1;
            return -1;
        }
        if (bind(lsock->fd, (struct sockaddr *)&lsock->addr, sizeof(lsock->addr)) < 0) {
            printe("%s -> %s: %s %s", nstate_stringify(network->state), nstate_stringify(tostate),
                   "Listen socket binding error: ", strerror(errno));
            close(lsock->fd);
            lsock->fd = -1;
            return -1;
        }
        if (listen(lsock->fd, INT32_MAX) < 0) {
            printe("%s -> %s: %s %s", nstate_stringify(network->state), nstate_stringify(tostate),
                   "Listen socket error: ", strerror(errno));
            close(lsock->fd);
            lsock->fd = -1;
            return -1;
        }
        pollfd->fd     = lsock->fd;
        pollfd->events = POLLIN;
        network->state = tostate;

        printi("Start to listen on %s:%s", addr, port);
        return 0;
    }

    if (NSTATE_IS_LISTEN(network->state) && NSTATE_IS_ACCEPTED(tostate)) {
        /*
         * accept incoming connection
         */
        nsock_t  *lsock = &network->sock_list;
        nsock_t  *asock = &network->sock_accept;
        socklen_t asize = sizeof(asock->addr);

        asock->fd = accept(lsock->fd, (struct sockaddr *)&asock->addr, &asize);
        if (asock->fd < 0) {
            printe("%s -> %s: %s %s", nstate_stringify(network->state), nstate_stringify(tostate),
                   "Can't accept incoming connection: ", strerror(errno));
            return -1;
        }
        fcntl(asock->fd, F_SETFL, O_NONBLOCK);

        pollfd->fd     = asock->fd;
        pollfd->events = POLLIN;
        network->state = tostate;
        printi("Client connected from %s:%u",
                inet_ntoa(asock->addr.sin_addr), ntohs(asock->addr.sin_port));
        return 0;
    }

    if (NSTATE_IS_ACCEPTED(network->state) && NSTATE_IS_LISTEN(tostate)) {
        /*
         * close incoming connection and start listen for new one
         */
        nsock_t *lsock = &network->sock_list;
        nsock_t *asock = &network->sock_accept;
        close(asock->fd);
        asock->fd = -1;
        memset(&asock->addr, 0, sizeof(asock->addr));

        pollfd->fd     = lsock->fd;
        pollfd->events = POLLIN;
        network->state = tostate;
        printi("Client connected was closed. Continue listen on %s:%u",
                inet_ntoa(lsock->addr.sin_addr), ntohs(lsock->addr.sin_port));
        return 0;
    }

    if (NSTATE_IS_ACCEPTED(network->state) && NSTATE_IS_DOWN(tostate)) {
        /*
         * close incoming connection as well as listen one
         */
        nsock_t *lsock = &network->sock_list;
        nsock_t *asock = &network->sock_accept;

        close(asock->fd);
        asock->fd = -1;
        memset(&asock->addr, 0, sizeof(asock->addr));

        close(lsock->fd);
        lsock->fd = -1;
        memset(&lsock->addr, 0, sizeof(lsock->addr));

        pollfd->fd     = -1;
        pollfd->events = 0;
        network->state = tostate;
        printi("Network state is DOWN");

        return 0;
    }

    if (NSTATE_IS_LISTEN(network->state) && NSTATE_IS_DOWN(tostate)) {
        /*
         * close LISTEN socket
         */
        nsock_t *lsock = &network->sock_list;

        close(lsock->fd);
        lsock->fd = -1;
        memset(&lsock->addr, 0, sizeof(lsock->addr));

        pollfd->fd     = -1;
        pollfd->events = 0;
        network->state = tostate;
        printi("Network state is DOWN");
        return 0;
    }

    if (NSTATE_IS_DOWN(network->state) && NSTATE_IS_CONNECTED(tostate)) {
        /*
         * setup CONNECT socket
         */
        nsock_t *csock = &network->sock_conn;

        csock->fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (csock->fd < 0) {
            printe("%s -> %s: %s", nstate_stringify(network->state), nstate_stringify(tostate),
                   "Can't create connect socket");
            return -1;
        }
        int sockoption = 1;
        setsockopt(csock->fd, SOL_SOCKET, SO_REUSEADDR, &sockoption, sizeof(sockoption));

        csock->addr.sin_family = AF_INET;
        csock->addr.sin_port   = htons(atoi(port));
        if (inet_aton(addr, &csock->addr.sin_addr) == 0) {
            printe("%s -> %s: %s %s", nstate_stringify(network->state), nstate_stringify(tostate),
                   "Bad connect addr: ", addr);
            close(csock->fd);
            csock->fd = -1;
            return -1;
        }
        if (connect(csock->fd, (struct sockaddr *)&csock->addr, sizeof(csock->addr)) < 0) {
            printe("%s -> %s: Can't connect to %s:%u: %s",
                    nstate_stringify(network->state), nstate_stringify(tostate),
                    inet_ntoa(csock->addr.sin_addr), ntohs(csock->addr.sin_port), strerror(errno));
            close(csock->fd);
            csock->fd = -1;
            return -1;
        }
        fcntl(csock->fd, F_SETFL, O_NONBLOCK);

        pollfd->fd     = csock->fd;
        pollfd->events = POLLIN;
        network->state = tostate;

        printi("Connected to %s:%u", inet_ntoa(csock->addr.sin_addr), ntohs(csock->addr.sin_port));
        return 0;

    }

    if (NSTATE_IS_CONNECTED(network->state) && NSTATE_IS_DOWN(tostate)) {
        /*
         * do disconnect
         */
        nsock_t *csock = &network->sock_conn;

        close(csock->fd);
        csock->fd = -1;
        memset(&csock->addr, 0, sizeof(csock->addr));

        pollfd->fd     = -1;
        pollfd->events = 0;
        network->state = tostate;
        printi("Network state is DOWN");
        return 0;

    }

    printe("%s -> %s: Unsupported network transition", nstate_stringify(network->state), nstate_stringify(tostate));
    return -1;
}

/*
 * messaging
 */
#define VTK_BASE_VMC                0x96FB
#define VTK_BASE_POS                0x97FB
#define VTK_BASE_FROM_STATE(state) ((NSTATE_IS_ACCEPTED(state) || NSTATE_IS_LISTEN(state)) ? VTK_BASE_POS : VTK_BASE_VMC)

#define MSG_MAXLEN         0xFFFF
#define MSG_VARINT_LEN(x) (x <= 127 ? 1 : (x <= 255 ? 2 : 3))

typedef struct msg_arg_s {
    uint16_t   id;
    uint16_t   len;
    char      *val;
    size_t     val_sz;
} msg_arg_t;

typedef struct msg_s {
    struct msg_header_s {
        uint16_t len;
        uint16_t proto;
    } __attribute__((packed))  header;
    msg_arg_t *args;
    size_t     args_cnt;
    size_t     args_sz;
} msg_t;

typedef struct msg_stream_s {
    char      *data;
    size_t     len;
    size_t     size;
    size_t     offset;
} msg_stream_t;

typedef enum msg_modtype_s {
    MSG_MT_ADDSTR,
    MSG_MT_ADDBIN,
    MSG_MT_ADDHEX,
    MSG_MT_ADDFILE,
    MSG_MT_RESET,
    MSG_MT_FREE,
} msg_modtype_t;
#define MSGMOD_IS_ADD(type) ((mod == MSG_MT_ADDSTR) || (mod == MSG_MT_ADDBIN) || (mod == MSG_MT_ADDHEX) || (mod == MSG_MT_ADDFILE))

char *
msg_argid_stringify(uint16_t id)
{
    struct msg_argid_s {
        uint16_t  id;
        char     *desc;
    } argids[] = {
       { 0x01, "Message name            " },
       { 0x03, "Operation number        " },
       { 0x04, "Minor currency units    " },
       { 0x05, "Keepalive interval, sec " },
       { 0x06, "Operation timeout, sec  " },
       { 0x07, "Event name              " },
       { 0x08, "Event number            " },
       { 0x09, "Product id              " },
       { 0x0A, "QR-code data            " },
       { 0x0B, "TCP/IP destinactio      " },
       { 0x0C, "Outgoing byte counter   " },
       { 0x0D, "Simple data block       " },
       { 0x0E, "Confirmable data block  " },
       { 0x0F, "Product name            " },
       { 0x10, "POS management data     " },
       { 0x11, "Local time              " },
       { 0x12, "System information      " },
       { 0x13, "Banking receipt         " },
       { 0x14, "Display time, ms        " },
       { 0, NULL }
    };
    for (int iarg = 0; argids[iarg].id; iarg++) {
        if (argids[iarg].id == id) {
            return argids[iarg].desc;
        }
    }
    return "Unknown argument ID";
}

int
msg_mod(msg_t *msg, msg_modtype_t mod, uint16_t id, uint16_t len, char *value)
{
    if (MSGMOD_IS_ADD(mod)) {
        size_t newlen = msg->header.len + MSG_VARINT_LEN(id) + MSG_VARINT_LEN(len);
        if (newlen > MSG_MAXLEN) {
            return -1;
        }
        msg->header.len = newlen;

        if (msg->args_cnt == msg->args_sz) {
            msg->args_sz = msg->args_sz ? (msg->args_sz * 2) : 1;
            msg->args = realloc(msg->args, sizeof(msg_arg_t) * msg->args_sz);
            memset(&msg->args[msg->args_cnt], 0, sizeof(msg_arg_t));
        }
    }
    if (mod == MSG_MT_ADDSTR) {
        msg_arg_t *arg = &msg->args[msg->args_cnt];
        arg->id = id;
        arg->len = strlen(value);
        if (msg->header.len + arg->len > MSG_MAXLEN) {
            return -1;
        }
        msg->header.len += arg->len;

        if (arg->val_sz <= arg->len) {
            arg->val = realloc(arg->val, arg->len + 1);
            arg->val_sz = arg->len + 1;
        }
        snprintf(arg->val, arg->val_sz, "%s", value);
        msg->args_cnt++;
    }
    if (mod == MSG_MT_ADDBIN) {
        msg_arg_t *arg = &msg->args[msg->args_cnt];
        arg->id = id;
        arg->len = len;
        if (msg->header.len + arg->len > MSG_MAXLEN) {
            return -1;
        }
        msg->header.len += arg->len;

        if (arg->val_sz <= arg->len) {
            arg->val = realloc(arg->val, arg->len + 1);
            arg->val_sz = arg->len + 1;
        }
        memcpy(arg->val, value, len);
        arg->val[len] = 0;
        msg->args_cnt++;
    }


    if (mod == MSG_MT_RESET) {
        msg->header.proto = id;
        msg->header.len   = sizeof(msg->header.proto);
        msg->args_cnt     = 0;
    }
    if (mod == MSG_MT_FREE) {
        for (int iarg = 0; iarg < msg->args_cnt; iarg++) {
            free(msg->args[iarg].val);
        }
        free(msg->args);
        msg->args = NULL;
        msg->args_sz = msg->args_cnt = 0;
    }
    return 0;
}

int
msg_print(msg_t *msg)
{
    for (int iarg = 0; iarg < msg->args_cnt; iarg++) {
        msg_arg_t *arg = &msg->args[iarg];
        printp("  % 2d: 0x%x  %s  => ", iarg, arg->id, msg_argid_stringify(arg->id));

        int hexout = 0;
        for (int i = 0; i < arg->len; i++) {
            if (! isprint(arg->val[i]) && (arg->val[i] != '\t')) {
                hexout = 1;
                break;
            }
        }
        if (hexout) {
            for (int i = 0; i < arg->len; i++) {
                printp("%0x ", (uint8_t)arg->val[i]);
            }
            printi("");
        } else {
            printi("%s", arg->val);
        }
    }
    return 0;
}

int
msg_stream_write(msg_stream_t *stream, uint16_t len, void *data, int printout)
{
    if (stream->len + len >= stream->size) {
        if (stream->len + len > stream->size * 2) {
            stream->size = stream->len + len;
        } else {
            stream->size = stream->size * 2;
        }
        stream->data = realloc(stream->data, stream->size);
    }
    char *cdata = (char *)data;
    for (int i = 0; i < len; i++) {
        stream->data[stream->len + i] = cdata[i];
        if (printout) {
            printp("%02X", (uint8_t)cdata[i]);
        }
    }
    stream->len += len;
    return len;
}

int
msg_stream_read(msg_stream_t *stream, uint16_t len, void *data, int printout)
{
    if (stream->offset + len > stream->len) {
        return -1;
    }
    char *cdata = (char *)data;
    for (int i = 0; i < len; i++) {
        cdata[i] = stream->data[stream->offset + i];
        if (printout) {
            printp("%02X", (uint8_t)cdata[i]);
        }
    }
    stream->offset += len;
    return len;
}

int
msg_serialize_varint(msg_stream_t *stream, uint16_t value, int printout)
{
    uint8_t varint[3];
    if (value <= 127) {
        varint[0] = value;
    } else if (value <= 255) {
        varint[0] = 128 + 1;
        varint[1] = value;
    } else {
        varint[0] = 128 + 2;
        varint[1] = (uint8_t)(value >> 8);
        varint[2] = (uint8_t)(value & 255);
    }
    return msg_stream_write(stream, MSG_VARINT_LEN(value), varint, printout);
}

int
msg_deserialize_varint(msg_stream_t *stream, uint16_t *value, int printout)
{
    uint8_t varint[3];
    msg_stream_read(stream, 1, &varint[0], printout);

    if (varint[0] <= 127) {
        *value = varint[0];
    } else if ((varint[0] & 127) == 1) {
        msg_stream_read(stream, 1, &varint[1], printout);
        *value = varint[0];
    } else if ((varint[0] & 127) == 2) {
        msg_stream_read(stream, 1, &varint[1], printout);
        msg_stream_read(stream, 1, &varint[2], printout);
        *value = (varint[1] << 8) + varint[2];
    } else {
        return -1;
    }
    return 0;
}

int
msg_serialize(msg_t *msg, msg_stream_t *stream, int printout)
{
    struct msg_header_s swap = {
        .len   = bswap_16(msg->header.len),
        .proto = bswap_16(msg->header.proto),
    };
    stream->len = 0;
    msg_stream_write(stream, sizeof(swap.len), &swap.len, printout);
    msg_stream_write(stream, sizeof(swap.proto), &swap.proto, printout);
    if (printout) {
        printp(" ");
    }

    for (int iarg = 0; iarg < msg->args_cnt; iarg++) {
        msg_arg_t *arg = &msg->args[iarg];
        msg_serialize_varint(stream, arg->id, printout);
        msg_serialize_varint(stream, arg->len, printout);
        msg_stream_write(stream, arg->len, arg->val, printout);
        if (printout) {
            printp(" ");
        }
    }
    if (printout) {
        printi("");
    }
    return 0;
}

int
msg_deserialize(msg_t *msg, msg_stream_t *stream, int printout)
{
    stream->offset = 0;

    if (stream->len < sizeof(msg->header.len)) {
        return -1;
    }
    struct msg_header_s swap;
    msg_stream_read(stream, sizeof(swap), &swap, printout);
    msg->header.len   = bswap_16(msg->header.len);
    msg->header.proto = bswap_16(msg->header.proto);
    if (printout) {
        printp(" ");
    }
    for (int iarg = 0; stream->offset < stream->len; iarg++) {
        /* todo: need to be improved */
        msg_arg_t arg = {0};
        msg_deserialize_varint(stream, &arg.id, printout);
        msg_deserialize_varint(stream, &arg.len, printout);
        msg_mod(msg, MSG_MT_ADDBIN, arg.id, arg.len, &stream->data[stream->offset]);

        for (int i = 0; i < arg.len; i++) {
            if (printout) {
                printp("%02X", (uint8_t)stream->data[stream->offset]);
            }
            stream->offset++;
        }
        if (printout) {
            printp(" ");
        }
    }
    if (printout) {
        printi("");
    }

    return 0;
}

/*
 *
 */
void
show_help(void)
{

}

typedef struct state_s {
    network_t     network;
    msg_t         msg_up;
    msg_stream_t  msg_stream_up;
    msg_t         msg_down;
    msg_stream_t  msg_stream_down;
} state_t;


int
user_action_ctrl(state_t *state, struct pollfd *pollfd, char *action)
{
    const char *errmsg = "unexpected command; type \"help\" to check syntax";
    char   *args[5] = {0};
    size_t  args_sz = sizeof(args) / sizeof(args[0]);

    for (int i = 0; (i < args_sz) && (args[i] = strtok((i ? NULL : action), " \t\n")); i++);

    if (args[0] && (strcasecmp(args[0], "net") == 0)) {
        /*
         * network commands
         */
        if (args[1] && (strcasecmp(args[1], "conn") == 0)) {
            if (! args[2] || ! args[3]) {
                printe(errmsg);
                return -1;
            }
            return nstate_change(NSTATE_CONNECTED, &state->network, pollfd, args[2], args[3]);
        }

        if (args[1] && (strcasecmp(args[1], "listen") == 0)) {
            if (! args[2] || ! args[3]) {
                printe(errmsg);
                return -1;
            }
            return nstate_change(NSTATE_LISTENED, &state->network, pollfd, args[2], args[3]);
        }

        if (args[1] && (strcasecmp(args[1], "drop") == 0)) {
            return nstate_change(NSTATE_LISTENED, &state->network, pollfd, NULL, NULL);
        }
        if (args[1] && (strcasecmp(args[1], "down") == 0)) {
            return nstate_change(NSTATE_DOWN, &state->network, pollfd, NULL, NULL);
        }
        if (args[1] && (strcasecmp(args[1], "stat") == 0)) {
            printi("%s", nstate_stringify(state->network.state));
            return 0;
        }


        printe(errmsg);
        return -1;

    } else if (args[0] && (strcasecmp(args[0], "msg") == 0)) {
        /*
         * message commands
         */
        if (args[1] && (strcasecmp(args[1], "reset") == 0)) {
            uint16_t protocol = VTK_BASE_FROM_STATE(state->network.state);
            if (args[2]) {
                if (strcasecmp(args[2], "VMC")) {
                    protocol = VTK_BASE_VMC;
                } else if (strcasecmp(args[2], "POS")) {
                    protocol = VTK_BASE_POS;
                } else {
                    printe(errmsg);
                    return -1;
                }
            }
            return msg_mod(&state->msg_up, MSG_MT_RESET, protocol, 0, NULL);
        }

        if (args[1] && (strcasecmp(args[1], "addstr") == 0)) {
            if (! args[2] || ! args[3]) {
                printe(errmsg);
                return -1;
            }
            if (! state->msg_up.header.proto) {
                msg_mod(&state->msg_up, MSG_MT_RESET, VTK_BASE_FROM_STATE(state->network.state), 0, NULL);
            }
            uint16_t id;
            if (sscanf(args[2], "%0x%x", &id) < 1) {
                printe(errmsg);
                return -1;
            }
            return msg_mod(&state->msg_up, MSG_MT_ADDSTR, id, 0, args[3]);
        }

        if (args[1] && (strcasecmp(args[1], "print") == 0)) {
            return msg_print(&state->msg_up);
        }

        if (args[1] && (strcasecmp(args[1], "printhex") == 0)) {
            return msg_serialize(&state->msg_up, &state->msg_stream_up, 1);

        }

        if (args[1] && (strcasecmp(args[1], "sent") == 0)) {
            if (! NSTATE_IS_ESTABLISHED(state->network.state)) {
                printe("Message can be sent in case of %s or %s network state",
                        nstate_stringify(NSTATE_ACCEPTED), nstate_stringify(NSTATE_CONNECTED));
                return -1;
            }
            int fd = NSTATE_IS_CONNECTED(state->network.state) ?
                     state->network.sock_conn.fd : state->network.sock_accept.fd;

            msg_serialize(&state->msg_up, &state->msg_stream_up, 1);
            ssize_t bwritten = write(fd, state->msg_stream_up.data, state->msg_stream_up.len);
            msg_mod(&state->msg_up, MSG_MT_RESET, VTK_BASE_FROM_STATE(state->network.state), 0, NULL);

            return (bwritten == state->msg_stream_up.len) ? 0 : -1;
        }

        printe(errmsg);
        return -1;

    } else if (args[0] && (strcasecmp(args[0], "macro") == 0)) {
        if (! args[1]) {
            printe(errmsg);
            return -1;
        }
        FILE *fin = fopen(args[1], "r");
        if (! fin) {
            printe("can't open %s macro file for read", args[1]);
            return -1;
        }
        char line[0xff];
        line[sizeof(line) - 1] = 0;

        while (fgets(line, sizeof(line) - 1, fin) > 0) {
            /* drop trailing control characters */
            if (! line[0] || (line[0] == '\n') || (line[0] == '#')) {
                continue;
            }
            user_action_ctrl(state, pollfd, line);
        }
        fclose(fin);

    } else {
        printe(errmsg);
        return -1;
    }

    return 0;
}

int
main_loop_run(state_t *state)
{
    const int in_term = 0;
    const int in_sock = 1;

    struct pollfd spool[2] = {0};

    spool[in_term].fd     = fileno(stdin);
    spool[in_term].events = POLLIN;
    spool[in_sock].fd     = -1;
    spool[in_sock].events = POLLIN;

    for (;;) {
        printp("command > ");
        nfds_t pollsize = spool[in_sock].fd >= 0 ? 2 : 1;
        int    rcode    = poll(spool, pollsize, -1);

        if (rcode < 0) {
            printe("IO error on poll syscall: %s", strerror(errno));
            break;
        }
        if (spool[in_term].revents == POLLIN) {
            /* terminal input has some bytes */
            char buff[0x100];
            int  bufflen = read(spool[in_term].fd, buff, sizeof(buff) - 1);

            if (bufflen <= 0) {
                printe("IO error on read syscall: %s", strerror(errno));
                break;
            }
            buff[bufflen] = 0;

            /* drop trailing control characters */
            for (int ic = bufflen; (ic > 0) && iscntrl(buff[ic - 1]); ic--) {
                bufflen = ic - 1;
                buff[bufflen] = 0;
            }
            if (! bufflen) {
                continue;
            }

            /* process basic commands here */
            if (strcasecmp(buff, "help") == 0) {
                show_help();
                continue;
            }
            if (strcasecmp(buff, "quit") == 0) {
                break;
            }
            /* care about most actions here */
            user_action_ctrl(state, &(spool[in_sock]), buff);
        }

        if (spool[in_sock].revents == POLLIN) {
            /* network socket was triggered */

            if (NSTATE_IS_LISTEN(state->network.state)) {
                nstate_change(NSTATE_ACCEPTED, &state->network, &spool[in_sock], NULL, NULL);
                continue;
            }

            if (NSTATE_IS_ESTABLISHED(state->network.state)) {
                /*
                 * check for incoming data
                 */
                int           is_connected = NSTATE_IS_CONNECTED(state->network.state);
                nsock_t      *sock   = is_connected ? &state->network.sock_conn : &state->network.sock_accept;
                msg_stream_t *stream = &state->msg_stream_down;
                ssize_t       rcount = 0;
                char          buffer[0xff];

                stream->len = 0;
                for (;;) {
                    rcount = read(sock->fd, buffer, sizeof(buffer));
                    if (rcount > 0) {
                        msg_stream_write(stream, rcount, buffer, 0);
                    } else {
                        break;
                    }
                }
                if (stream->len) {
                    printi("%lu bytes was read", stream->len);

                    msg_mod(&state->msg_down, MSG_MT_RESET, VTK_BASE_FROM_STATE(state->network.state), 0, NULL);
                    msg_deserialize(&state->msg_down, stream, 1);
                    msg_print(&state->msg_down);

                }
                if (rcount == 0)  {
                    printi("EOF was found on %s socket. Close it", nstate_stringify(state->network.state));
                    nstate_change(is_connected ? NSTATE_DOWN : NSTATE_LISTENED,
                                 &state->network, &spool[in_sock], NULL, NULL);
                }
            }
        }
    }
    return 0;
}

int
main(int argc, char *argv[])
{
    state_t state = {
        .network = {
            .sock_conn   = { .fd = -1 },
            .sock_list   = { .fd = -1 },
            .sock_accept = { .fd = -1 },
        }
    };
    main_loop_run(&state);

    return 0;
}
