#include <arpa/inet.h>
#include <byteswap.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include "vendotek.h"

/*
 * Logging
 */
static void
vtk_log_dflt(int flags, const char *format, ...)
{
    char buffer[4096];

    va_list vlist;
    va_start(vlist, format);
    vsnprintf(buffer, sizeof(buffer), format, vlist);
    va_end(vlist);

    FILE *outfile = ((flags & VTK_LOG_PRIMASK) <= LOG_ERR) ? stderr : stdout;
    char *outform = (flags & VTK_LOG_NOEOL) ? "%s" : "%s\n";

    fprintf(outfile, outform, buffer);
    fflush(outfile);
}
#define loge(format, ...) vtk_log_dflt(LOG_ERR, format, ##__VA_ARGS__)
#define logw(format, ...) vtk_log_dflt(LOG_WARNING, format, ##__VA_ARGS__)
#define logn(format, ...) vtk_log_dflt(LOG_NOTICE, format, ##__VA_ARGS__)
#define logi(format, ...) vtk_log_dflt(LOG_INFO, format, ##__VA_ARGS__)
#define logd(format, ...) vtk_log_dflt(LOG_DEBUG, format, ##__VA_ARGS__)
#define logp(format, ...) vtk_log_dflt(LOG_INFO | VTK_LOG_NOEOL, format, ##__VA_ARGS__)

/*
 * Main State
 */
typedef struct vkt_sock_s {
    struct sockaddr_in addr;
    int                fd;
} vtk_sock_t;

struct vtk_s {
    vtk_net_t    net_state;
    vtk_sock_t   sock_conn;
    vtk_sock_t   sock_list;
    vtk_sock_t   sock_accept;
    vtk_stream_t stream_up;
    vtk_stream_t stream_down;
};

int vtk_init(vtk_t **vtk, vtk_log_fn *log_fn)
{
    *vtk  = malloc(sizeof(vtk_t));
    **vtk = (vtk_t) {
        .net_state = VTK_NET_DOWN,
        .sock_conn.fd   = -1,
        .sock_list.fd   = -1,
        .sock_accept.fd = -1
    };
    return 0;
}

void vtk_free(vtk_t *vtk){
    if (! VTK_NET_IS_DOWN(vtk->net_state)) {
        vtk_net_set(vtk, VTK_NET_DOWN, NULL, NULL);
    }
    if (vtk->sock_conn.fd >= 0) {
        close(vtk->sock_conn.fd);
    }
    if (vtk->sock_accept.fd >= 0) {
        close(vtk->sock_accept.fd);
    }
    if (vtk->sock_list.fd >= 0) {
        close(vtk->sock_list.fd);
    }
    free(vtk->stream_up.data);
    free(vtk->stream_down.data);
    free(vtk);
}

void vtk_log_set(vtk_t  *vtk, vtk_log_fn *log_fn)
{

}

/*
 * Messaging
 */
#define VTK_BASE_VMC                0x96FB
#define VTK_BASE_POS                0x97FB
#define VTK_BASE_FROM_STATE(state) ((VTK_NET_IS_ACCEPTED(state) || VTK_NET_IS_LISTEN(state)) ? VTK_BASE_POS : VTK_BASE_VMC)

#define VTK_MSG_MAXLEN              0xFFFF
#define VTK_MSG_VARLEN(x)          (x <= 127 ? 1 : (x <= 255 ? 2 : 3))
#define VTK_MSG_MODADD(mod)        ((mod == VTK_MSG_ADDSTR) || (mod == VTK_MSG_ADDBIN) || (mod == VTK_MSG_ADDHEX) || (mod == VTK_MSG_ADDFILE))

typedef struct msg_arg_s {
    uint16_t   id;
    uint16_t   len;
    char      *val;
    size_t     val_sz;
} msg_arg_t;

typedef struct msg_hdr_s {
    uint16_t   len;
    uint16_t   proto;
} __attribute__((packed)) msg_hdr_t;

struct vtk_msg_s {
    vtk_t     *vtk;
    msg_hdr_t  header;
    msg_arg_t *args;
    size_t     args_cnt;
    size_t     args_sz;
};

static char *
vtk_msgid_stringify(uint16_t id)
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

int vtk_msg_init(vtk_msg_t **msg, vtk_t *vtk)
{
    *msg  = malloc(sizeof(vtk_msg_t));
    **msg = (vtk_msg_t) { .vtk = vtk };
    return 0;
}

void vtk_msg_free(vtk_msg_t  *msg)
{
    for (int iarg = 0; (iarg < msg->args_sz); iarg++) {
        free(msg->args[iarg].val);
    }
    free(msg->args);
    free(msg);
}

int vtk_msg_mod(vtk_msg_t *msg, vtk_msgmod_t mod, uint16_t id, uint16_t len, char *value)
{
    if (VTK_MSG_MODADD(mod)) {
        size_t newlen = msg->header.len + VTK_MSG_VARLEN(id) + VTK_MSG_VARLEN(len);
        if (newlen > VTK_MSG_MAXLEN) {
            return -1;
        }
        msg->header.len = newlen;

        if (msg->args_cnt == msg->args_sz) {
            msg->args_sz = msg->args_sz ? (msg->args_sz * 2) : 1;
            msg->args = realloc(msg->args, sizeof(msg_arg_t) * msg->args_sz);
            memset(&msg->args[msg->args_cnt], 0, sizeof(msg_arg_t) * (msg->args_sz - msg->args_cnt));
        }
    }
    if (mod == VTK_MSG_ADDSTR) {
        msg_arg_t *arg = &msg->args[msg->args_cnt];
        arg->id = id;
        arg->len = strlen(value);
        if (msg->header.len + arg->len > VTK_MSG_MAXLEN) {
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
    if (mod == VTK_MSG_ADDBIN) {
        msg_arg_t *arg = &msg->args[msg->args_cnt];
        arg->id = id;
        arg->len = len;
        if (msg->header.len + arg->len > VTK_MSG_MAXLEN) {
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
    if (mod == VTK_MSG_RESET) {
        msg->header.proto = id;
        msg->header.len   = sizeof(msg->header.proto);
        msg->args_cnt     = 0;
    }

    return 0;
}

int vtk_msg_print(vtk_msg_t *msg)
{
    for (int iarg = 0; iarg < msg->args_cnt; iarg++) {
        msg_arg_t *arg = &msg->args[iarg];
        logp("  % 2d: 0x%x  %s  => ", iarg, arg->id, vtk_msgid_stringify(arg->id));

        int hexout = 0;
        for (int i = 0; i < arg->len; i++) {
            if (! isprint(arg->val[i]) && (arg->val[i] != '\t')) {
                hexout = 1;
                break;
            }
        }
        if (hexout) {
            for (int i = 0; i < arg->len; i++) {
                logp("%0x ", (uint8_t)arg->val[i]);
            }
            logi("");
        } else {
            logi("%s", arg->val);
        }
    }
    return 0;
}

static int
vtk_stream_write(vtk_stream_t *stream, uint16_t len, void *data, int verbose)
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
        if (verbose) {
            logp("%02X", (uint8_t)cdata[i]);
        }
    }
    stream->len += len;
    return len;
}

static int
vtk_stream_read(vtk_stream_t *stream, uint16_t len, void *data, int verbose)
{
    if (stream->offset + len > stream->len) {
        return -1;
    }
    char *cdata = (char *)data;
    for (int i = 0; i < len; i++) {
        cdata[i] = stream->data[stream->offset + i];
        if (verbose) {
            logp("%02X", (uint8_t)cdata[i]);
        }
    }
    stream->offset += len;
    return len;
}

static int
vtk_varint_serialize(vtk_stream_t *stream, uint16_t value, int verbose)
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
    return vtk_stream_write(stream, VTK_MSG_VARLEN(value), varint, verbose);
}

static int
vtk_varint_deserialize(vtk_stream_t *stream, uint16_t *value, int verbose)
{
    uint8_t varint[3];
    vtk_stream_read(stream, 1, &varint[0], verbose);

    if (varint[0] <= 127) {
        *value = varint[0];
    } else if ((varint[0] & 127) == 1) {
        vtk_stream_read(stream, 1, &varint[1], verbose);
        *value = varint[0];
    } else if ((varint[0] & 127) == 2) {
        vtk_stream_read(stream, 1, &varint[1], verbose);
        vtk_stream_read(stream, 1, &varint[2], verbose);
        *value = (varint[1] << 8) + varint[2];
    } else {
        return -1;
    }
    return 0;
}

int vtk_msg_serialize(vtk_msg_t *msg, vtk_stream_t *stream, int verbose)
{
    msg_hdr_t swap = {
        .len   = bswap_16(msg->header.len),
        .proto = bswap_16(msg->header.proto),
    };
    stream->offset = stream->len = 0;
    vtk_stream_write(stream, sizeof(swap.len), &swap.len, verbose);
    vtk_stream_write(stream, sizeof(swap.proto), &swap.proto, verbose);
    if (verbose) {
        logp(" ");
    }

    for (int iarg = 0; iarg < msg->args_cnt; iarg++) {
        msg_arg_t *arg = &msg->args[iarg];
        vtk_varint_serialize(stream, arg->id, verbose);
        vtk_varint_serialize(stream, arg->len, verbose);
        vtk_stream_write(stream, arg->len, arg->val, verbose);
        if (verbose) {
            logp(" ");
        }
    }
    if (verbose) {
        logi("");
    }
    return 0;
}

int vtk_msg_deserialize(vtk_msg_t *msg, vtk_stream_t *stream, int verbose)
{
    stream->offset = 0;

    if (stream->len < sizeof(msg->header.len)) {
        return -1;
    }
    msg_hdr_t swap;
    vtk_stream_read(stream, sizeof(swap), &swap, verbose);
    msg->header.len   = bswap_16(msg->header.len);
    msg->header.proto = bswap_16(msg->header.proto);
    if (verbose) {
        logp(" ");
    }
    for (int iarg = 0; stream->offset < stream->len; iarg++) {
        /* todo: need to be improved */
        msg_arg_t arg = {0};
        vtk_varint_deserialize(stream, &arg.id, verbose);
        vtk_varint_deserialize(stream, &arg.len, verbose);
        vtk_msg_mod(msg, VTK_MSG_ADDBIN, arg.id, arg.len, &stream->data[stream->offset]);

        for (int i = 0; i < arg.len; i++) {
            if (verbose) {
                logp("%02X", (uint8_t)stream->data[stream->offset]);
            }
            stream->offset++;
        }
        if (verbose) {
            logp(" ");
        }
    }
    if (verbose) {
        logi("");
    }
    return 0;
}

/*
 * Network State
 */
static char *
vtk_net_stringify(vtk_net_t vtk_net)
{
    switch(vtk_net) {
        case VTK_NET_DOWN:      return "DOWN";
        case VTK_NET_CONNECTED: return "CONNECTED";
        case VTK_NET_LISTENED:  return "LISTENED";
        case VTK_NET_ACCEPTED:  return "ACCEPTED";
        default:                return "UNKNOWN";
    }
}

int vtk_net_set(vtk_t *vtk, vtk_net_t net_to, char *addr, char *port)
{
    if (VTK_NET_IS_DOWN(vtk->net_state) && VTK_NET_IS_LISTEN(net_to)) {
        /*
         * setup listen socket
         */
        vtk_sock_t *lsock = &vtk->sock_list;

        lsock->fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (lsock->fd < 0) {
            loge("%s -> %s: %s", vtk_net_stringify(vtk->net_state), vtk_net_stringify(net_to),
                 "Can't create listen socket");
            return -1;
        }
        int sockoption = 1;
        setsockopt(lsock->fd, SOL_SOCKET, SO_REUSEADDR, &sockoption, sizeof(sockoption));

        lsock->addr.sin_family = AF_INET;
        lsock->addr.sin_port   = htons(atoi(port));
        if (inet_aton(addr, &lsock->addr.sin_addr) == 0) {
            loge("%s -> %s: %s %s", vtk_net_stringify(vtk->net_state), vtk_net_stringify(net_to),
                 "Bad listen addr: ", addr);
            close(lsock->fd);
            lsock->fd = -1;
            return -1;
        }
        if (bind(lsock->fd, (struct sockaddr *)&lsock->addr, sizeof(lsock->addr)) < 0) {
            loge("%s -> %s: %s %s", vtk_net_stringify(vtk->net_state), vtk_net_stringify(net_to),
                 "Listen socket binding error: ", strerror(errno));
            close(lsock->fd);
            lsock->fd = -1;
            return -1;
        }
        if (listen(lsock->fd, INT32_MAX) < 0) {
            loge("%s -> %s: %s %s", vtk_net_stringify(vtk->net_state), vtk_net_stringify(net_to),
                 "Listen socket error: ", strerror(errno));
            close(lsock->fd);
            lsock->fd = -1;
            return -1;
        }
        vtk->net_state = net_to;
        logi("Start to listen on %s:%s", addr, port);
        return 0;
    }

    if (VTK_NET_IS_LISTEN(vtk->net_state) && VTK_NET_IS_ACCEPTED(net_to)) {
        /*
         * accept incoming connection
         */
        vtk_sock_t *lsock = &vtk->sock_list;
        vtk_sock_t *asock = &vtk->sock_accept;
        socklen_t   asize = sizeof(asock->addr);

        asock->fd = accept(lsock->fd, (struct sockaddr *)&asock->addr, &asize);
        if (asock->fd < 0) {
            loge("%s -> %s: %s %s", vtk_net_stringify(vtk->net_state), vtk_net_stringify(net_to),
                 "Can't accept incoming connection: ", strerror(errno));
            return -1;
        }
        fcntl(asock->fd, F_SETFL, O_NONBLOCK);

        vtk->net_state = net_to;
        logi("Client connected from %s:%u",
              inet_ntoa(asock->addr.sin_addr), ntohs(asock->addr.sin_port));
        return 0;
    }

    if (VTK_NET_IS_ACCEPTED(vtk->net_state) && VTK_NET_IS_LISTEN(net_to)) {
        /*
         * close incoming connection; listen socket remains open and should be reused
         */
        vtk_sock_t *lsock = &vtk->sock_list;
        vtk_sock_t *asock = &vtk->sock_accept;
        close(asock->fd);
        asock->fd = -1;
        memset(&asock->addr, 0, sizeof(asock->addr));

        vtk->net_state = net_to;
        logi("Client connected was closed. Continue listen on %s:%u",
              inet_ntoa(lsock->addr.sin_addr), ntohs(lsock->addr.sin_port));
        return 0;
    }

    if (VTK_NET_IS_ACCEPTED(vtk->net_state) && VTK_NET_IS_DOWN(net_to)) {
        /*
         * close incoming connection; listen socket should be closed too
         */
        vtk_sock_t *lsock = &vtk->sock_list;
        vtk_sock_t *asock = &vtk->sock_accept;
        close(asock->fd);
        asock->fd = -1;
        memset(&asock->addr, 0, sizeof(asock->addr));

        close(lsock->fd);
        lsock->fd = -1;
        memset(&lsock->addr, 0, sizeof(lsock->addr));

        vtk->net_state = net_to;
        logi("Network state is DOWN");

        return 0;
    }

    if (VTK_NET_IS_LISTEN(vtk->net_state) && VTK_NET_IS_DOWN(net_to)) {
        /*
         * close listen socket
         */
        vtk_sock_t *lsock = &vtk->sock_list;

        close(lsock->fd);
        lsock->fd = -1;
        memset(&lsock->addr, 0, sizeof(lsock->addr));

        vtk->net_state = net_to;
        logi("Network state is DOWN");
        return 0;
    }

    if (VTK_NET_IS_DOWN(vtk->net_state) && VTK_NET_IS_CONNECTED(net_to)) {
        /*
         * setup outgoing connection
         */
        vtk_sock_t *csock = &vtk->sock_conn;

        csock->fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (csock->fd < 0) {
            loge("%s -> %s: %s", vtk_net_stringify(vtk->net_state), vtk_net_stringify(net_to),
                 "Can't create connect socket");
            return -1;
        }
        int sockoption = 1;
        setsockopt(csock->fd, SOL_SOCKET, SO_REUSEADDR, &sockoption, sizeof(sockoption));

        csock->addr.sin_family = AF_INET;
        csock->addr.sin_port   = htons(atoi(port));
        if (inet_aton(addr, &csock->addr.sin_addr) == 0) {
            loge("%s -> %s: %s %s", vtk_net_stringify(vtk->net_state), vtk_net_stringify(net_to),
                 "Bad connect addr: ", addr);
            close(csock->fd);
            csock->fd = -1;
            return -1;
        }
        if (connect(csock->fd, (struct sockaddr *)&csock->addr, sizeof(csock->addr)) < 0) {
            loge("%s -> %s: Can't connect to %s:%u: %s",
                  vtk_net_stringify(vtk->net_state), vtk_net_stringify(net_to),
                  inet_ntoa(csock->addr.sin_addr), ntohs(csock->addr.sin_port), strerror(errno));
            close(csock->fd);
            csock->fd = -1;
            return -1;
        }
        fcntl(csock->fd, F_SETFL, O_NONBLOCK);

        vtk->net_state = net_to;
        logi("Connected to %s:%u", inet_ntoa(csock->addr.sin_addr), ntohs(csock->addr.sin_port));
        return 0;

    }

    if (VTK_NET_IS_CONNECTED(vtk->net_state) && VTK_NET_IS_DOWN(net_to)) {
        /*
         * do disconnect
         */
        vtk_sock_t *csock = &vtk->sock_conn;

        close(csock->fd);
        csock->fd = -1;
        memset(&csock->addr, 0, sizeof(csock->addr));

        vtk->net_state = net_to;
        logi("Network state is DOWN");
        return 0;

    }

    loge("%s -> %s: Unsupported network state transition", vtk_net_stringify(vtk->net_state), vtk_net_stringify(net_to));
    return -1;
}

vtk_net_t vtk_net_get(vtk_t *vtk)
{
    return vtk->net_state;
}

int vtk_net_get_socket(vtk_t *vtk)
{
    switch(vtk->net_state) {
        case VTK_NET_CONNECTED: return vtk->sock_conn.fd;
        case VTK_NET_LISTENED:  return vtk->sock_list.fd;
        case VTK_NET_ACCEPTED:  return vtk->sock_accept.fd;
        default:                return -1;
    }
}

int vtk_net_send(vtk_t *vtk, vtk_msg_t *msg, int verbose)
{
    if (! VTK_NET_IS_ESTABLISHED(vtk->net_state)) {
        return -1;
    }
    int sock = vtk_net_get_socket(vtk);

    vtk_msg_serialize(msg, &vtk->stream_up, verbose);
    ssize_t bwritten = write(sock, vtk->stream_up.data, vtk->stream_up.len);

    return (bwritten == vtk->stream_up.len) ? vtk->stream_up.len : -1;
}

int vtk_net_recv(vtk_t *vtk, vtk_msg_t *msg, int *eof, int verbose)
{
    if (! VTK_NET_IS_ESTABLISHED(vtk->net_state)) {
        return -1;
    }
    int     sock = vtk_net_get_socket(vtk);
    ssize_t rcount = 0;
    char    buffer[0xff];

    vtk->stream_down.len = 0;
    for (;;) {
        rcount = read(sock, buffer, sizeof(buffer));
        if (rcount > 0) {
            vtk_stream_write(&vtk->stream_down, rcount, buffer, verbose);
        } else {
            *eof = (rcount == 0);
            break;
        }
    }
    if (vtk->stream_down.len) {
        vtk_msg_mod(msg, VTK_MSG_RESET, VTK_BASE_FROM_STATE(vtk->net_state), 0, NULL);

        if (vtk_msg_deserialize(msg, &vtk->stream_down, verbose) >= 0) {
            return vtk->stream_down.len;
        } else {
            return -1;
        }
    }
    return 0;
}
