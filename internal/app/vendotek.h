#ifndef VENDOTEK_H__
#define VENDOTEK_H__

#include <stddef.h>
#include <stdint.h>
#include <syslog.h>

/*
 * Logging
 */
void  vtk_log(int flags, const char *format, ...);

#define VTK_LOG_PRIMASK 0x07
#define VTK_LOG_NOEOL   0x10

#define vtk_loge(format, ...) vtk_log(LOG_ERR, format, ##__VA_ARGS__)
#define vtk_logw(format, ...) vtk_log(LOG_WARNING, format, ##__VA_ARGS__)
#define vtk_logn(format, ...) vtk_log(LOG_NOTICE, format, ##__VA_ARGS__)
#define vtk_logi(format, ...) vtk_log(LOG_INFO, format, ##__VA_ARGS__)
#define vtk_logd(format, ...) vtk_log(LOG_DEBUG, format, ##__VA_ARGS__)
#define vtk_logp(format, ...) vtk_log(LOG_INFO | VTK_LOG_NOEOL, format, ##__VA_ARGS__)

typedef void  (*vtk_logline_fn)(int flags, const char *logline);

void vtk_logline_set(vtk_logline_fn logline);

/*
 * Main state structure
 */
typedef struct vtk_s vtk_t;

int  vtk_init   (vtk_t **vtk);
void vtk_free   (vtk_t  *vtk);

/*
 * Messaging
 */
typedef struct vtk_msg_s vtk_msg_t;

int  vtk_msg_init(vtk_msg_t **msg, vtk_t *vtk);
void vtk_msg_free(vtk_msg_t  *msg);

typedef enum vtk_msgmod_s {
    VTK_MSG_ADDSTR,
    VTK_MSG_ADDBIN,
    VTK_MSG_ADDHEX,
    VTK_MSG_ADDFILE,
    VTK_MSG_RESET
} vtk_msgmod_t;

#define VTK_BASE_VMC                0x96FB
#define VTK_BASE_POS                0x97FB
#define VTK_BASE_FROM_STATE(state) ((VTK_NET_IS_ACCEPTED(state) || VTK_NET_IS_LISTEN(state)) ? VTK_BASE_POS : VTK_BASE_VMC)

#define VTK_MSG_MAXLEN              0xFFFF
#define VTK_MSG_VARLEN(x)          (x <= 127 ? 1 : (x <= 255 ? 2 : 3))
#define VTK_MSG_MODADD(mod)        ((mod == VTK_MSG_ADDSTR) || (mod == VTK_MSG_ADDBIN) || (mod == VTK_MSG_ADDHEX) || (mod == VTK_MSG_ADDFILE))

int vtk_msg_mod(vtk_msg_t *msg, vtk_msgmod_t mod, uint16_t id, uint16_t len, char *value);
int vtk_msg_print(vtk_msg_t *msg);

typedef struct vtk_stream_s {
    char      *data;
    size_t     len;
    size_t     size;
    size_t     offset;
} vtk_stream_t;

int vtk_msg_serialize(vtk_msg_t *msg, vtk_stream_t *stream, int verbose);
int vtk_msg_deserialize(vtk_msg_t *msg, vtk_stream_t *stream, int verbose);

/*
 * Network State
 */
typedef enum vtk_net_e {
    VTK_NET_DOWN,
    VTK_NET_CONNECTED,
    VTK_NET_LISTENED,
    VTK_NET_ACCEPTED
} vtk_net_t;
#define VTK_NET_IS_DOWN(state)         (state == VTK_NET_DOWN)
#define VTK_NET_IS_CONNECTED(state)    (state == VTK_NET_CONNECTED)
#define VTK_NET_IS_LISTEN(state)       (state == VTK_NET_LISTENED)
#define VTK_NET_IS_ACCEPTED(state)     (state == VTK_NET_ACCEPTED)
#define VTK_NET_IS_ESTABLISHED(state)  (state == VTK_NET_CONNECTED || state == VTK_NET_ACCEPTED)

char     *vtk_net_stringify(vtk_net_t vtk_net);
int       vtk_net_set(vtk_t *vtk, vtk_net_t net_to, char *addr, char *port);
vtk_net_t vtk_net_get_state(vtk_t *vtk);
int       vtk_net_get_socket(vtk_t *vtk);
int       vtk_net_send(vtk_t *vtk, vtk_msg_t *msg, int verbose);
int       vtk_net_recv(vtk_t *vtk, vtk_msg_t *msg, int *eof, int verbose);

#endif
