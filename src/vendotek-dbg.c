#include <ctype.h>
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "vendotek.h"

void
show_help(void)
{
    char *text[] = {
        "",
        "Network commands, server mode: ",
        "    net list 0.0.0.0 1234",
        "       start to listen on port 1234, from all IPs",
        "    net drop",
        "       drop remote connection, continue to listen for new one, if applicable",
        "    net down",
        "       drop remote connection, stop to listen, if applicable",
        "    net conn 127.0.0.1 1234",
        "       conn to 127.0.0.1, on port 1234",
        "    net stat",
        "       show current net state",
        "",
        "Message commands:",
        "    msg reset [POS | VMC]",
        "       reset / initialize Upload message structure",
        "    msg addstr 1 IDL",
        "       add new field to Upload message, with code 0x01 and value = \"IDL\"",
        "    msg print",
        "       show Upload message in human readable form",
        "    msg printhex",
        "       show Upload message in hex form",
        "    msg send",
        "       send message over TCP, if connected; message structure will be reset then",
        "",
        "Other commands:",
        "    macro sample0.macro",
        "       load commands from \"sample0.macro\" file and execute them ",
        "       as if they were read from terminal ",
        "    help",
        "       show this help",
        "    quit",
        "       quit gracefully",
        "",  NULL
    };
    for (int i = 0; text[i]; i++) {
        vtk_logi("%s", text[i]);
    }
}

typedef struct state_s {
    vtk_t        *vtk;
    vtk_msg_t    *msg_up;
    vtk_stream_t  msg_stream_up;
    vtk_msg_t    *msg_down;
    vtk_stream_t  msg_stream_down;
} state_t;

int user_action_ctrl(state_t *state, char *action)
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
                vtk_loge(errmsg);
                return -1;
            }
            return vtk_net_set(state->vtk, VTK_NET_CONNECTED, args[2], args[3]);
        }

        if (args[1] && (strcasecmp(args[1], "list") == 0)) {
            if (! args[2] || ! args[3]) {
                vtk_loge(errmsg);
                return -1;
            }
            return vtk_net_set(state->vtk, VTK_NET_LISTENED, args[2], args[3]);
        }

        if (args[1] && (strcasecmp(args[1], "drop") == 0)) {
            if (VTK_NET_IS_ACCEPTED(vtk_net_get_state(state->vtk))) {
                return vtk_net_set(state->vtk, VTK_NET_LISTENED, NULL, NULL);
            } else {
                return vtk_net_set(state->vtk, VTK_NET_DOWN, NULL, NULL);
            }
        }
        if (args[1] && (strcasecmp(args[1], "down") == 0)) {
            return vtk_net_set(state->vtk, VTK_NET_DOWN, NULL, NULL);
        }
        if (args[1] && (strcasecmp(args[1], "stat") == 0)) {
            vtk_logi("current state: %s", vtk_net_stringify(vtk_net_get_state(state->vtk)));
            return 0;
        }

        vtk_loge(errmsg);
        return -1;

    } else if (args[0] && (strcasecmp(args[0], "msg") == 0)) {
        /*
         * message commands
         */
        if (args[1] && (strcasecmp(args[1], "reset") == 0)) {
            uint16_t protocol = VTK_BASE_FROM_STATE(vtk_net_get_state(state->vtk));
            if (args[2]) {
                if (strcasecmp(args[2], "VMC") == 0) {
                    protocol = VTK_BASE_VMC;
                } else if (strcasecmp(args[2], "POS") == 0) {
                    protocol = VTK_BASE_POS;
                } else {
                    vtk_loge(errmsg);
                    return -1;
                }
            }
            return vtk_msg_mod(state->msg_up, VTK_MSG_RESET, protocol, 0, NULL);
        }

        if (args[1] && (strcasecmp(args[1], "addstr") == 0)) {
            if (! args[2] || ! args[3]) {
                vtk_loge(errmsg);
                return -1;
            }
            uint16_t id;
            if (sscanf(args[2], "%0x%x", &id) < 1) {
                vtk_loge(errmsg);
                return -1;
            }
            return vtk_msg_mod(state->msg_up, VTK_MSG_ADDSTR, id, 0, args[3]);
        }

        if (args[1] && (strcasecmp(args[1], "print") == 0)) {
            return vtk_msg_print(state->msg_up);
        }

        if (args[1] && (strcasecmp(args[1], "printhex") == 0)) {
            return vtk_msg_serialize(state->msg_up, &state->msg_stream_up);

        }

        if (args[1] && (strcasecmp(args[1], "send") == 0)) {

            if (vtk_net_send(state->vtk, state->msg_up) < 0) {
                return -1;
            }
            vtk_msg_mod(state->msg_up, VTK_MSG_RESET, VTK_BASE_FROM_STATE(vtk_net_get_state(state->vtk)), 0, NULL);
            return 0;
        }

        vtk_loge(errmsg);
        return -1;

    } else if (args[0] && (strcasecmp(args[0], "macro") == 0)) {
        if (! args[1]) {
            vtk_loge(errmsg);
            return -1;
        }
        FILE *fin = fopen(args[1], "r");
        if (! fin) {
            vtk_loge("can't open %s macro file for read", args[1]);
            return -1;
        }
        char line[0xff];
        line[sizeof(line) - 1] = 0;

        while (fgets(line, sizeof(line) - 1, fin) > 0) {
            /* drop trailing control characters */
            if (! line[0] || (line[0] == '\n') || (line[0] == '#')) {
                continue;
            }
            user_action_ctrl(state, line);
        }
        fclose(fin);

    } else {
        vtk_loge(errmsg);
        return -1;
    }

    return 0;
}

int main_loop_run(state_t *state)
{
    const int in_term = 0;
    const int in_sock = 1;

    struct pollfd spool[2] = {0};

    spool[in_term].fd     = fileno(stdin);
    spool[in_term].events = POLLIN;
    spool[in_sock].fd     = -1;
    spool[in_sock].events = POLLIN;

    for (;;) {
        vtk_logio("command > ");

        spool[in_sock].fd = vtk_net_get_socket(state->vtk);
        nfds_t pollsize   = spool[in_sock].fd >= 0 ? 2 : 1;
        int    rcode      = poll(spool, pollsize, -1);
        if (rcode < 0) {
            vtk_loge("IO error on poll syscall: %s", strerror(errno));
            break;
        }
        if (spool[in_term].revents == POLLIN) {
            /* terminal input has some bytes */
            char buff[0x100];
            int  bufflen = read(spool[in_term].fd, buff, sizeof(buff) - 1);

            if (bufflen <= 0) {
                vtk_loge("IO error on read syscall: %s", strerror(errno));
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
            user_action_ctrl(state, buff);
        }

        if (spool[in_sock].revents == POLLIN) {
            /* network socket was triggered */

            if (VTK_NET_IS_LISTEN(vtk_net_get_state(state->vtk))) {
                vtk_net_set(state->vtk, VTK_NET_ACCEPTED, NULL, NULL);
                continue;
            }

            if (VTK_NET_IS_ESTABLISHED(vtk_net_get_state(state->vtk))) {
                /*
                 * check for incoming data
                 */
                int fleof = 0;
                int rcode = vtk_net_recv(state->vtk, state->msg_down, &fleof);

                if (rcode >= 0) {
                    vtk_msg_print(state->msg_down);
                }
                if (fleof) {
                    vtk_net_t nstate = vtk_net_get_state(state->vtk);
                    vtk_logi("EOF was found on %s socket. Close it", vtk_net_stringify(nstate));
                    vtk_net_set(state->vtk, VTK_NET_IS_CONNECTED(nstate) ? VTK_NET_DOWN : VTK_NET_LISTENED, NULL, NULL);
                }
            }
        }
    }
    return 0;
}

int main(int argc, char *argv[])
{
    state_t state = {0};
    vtk_init(&state.vtk);
    vtk_msg_init(&state.msg_up, state.vtk);
    vtk_msg_init(&state.msg_down, state.vtk);

    main_loop_run(&state);

    free(state.msg_stream_up.data);
    free(state.msg_stream_down.data);
    vtk_msg_free(state.msg_up);
    vtk_msg_free(state.msg_down);
    vtk_free(state.vtk);

    return 0;
}
