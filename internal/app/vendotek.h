#ifndef VENDOTEK_H__
#define VENDOTEK_H__

#include <stddef.h>
#include <stdint.h>

/*
 * logging + main state structure
 */
#define VTK_LOG_PRIMASK 0x07
#define VTK_LOG_NOEOL   0x10

typedef void __attribute__((format(printf, 2, 3)))
        (vtk_log_fn)(int flags, const char *format, ...);

typedef struct vtk_s vtk_t;

int  vtk_init   (vtk_t **vtk, vtk_log_fn *log_fn);
void vtk_free   (vtk_t  *vtk);
void vtk_log_set(vtk_t  *vtk, vtk_log_fn *log_fn);

/*
 * vendotek messaging
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
 * vendotek network state
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

int       vtk_net_set(vtk_t *vtk, vtk_net_t net_to, char *addr, char *port);
vtk_net_t vtk_net_get(vtk_t *vtk);
int       vtk_net_get_socket(vtk_t *vtk);
int       vtk_net_send(vtk_t *vtk, vtk_msg_t *msg, int verbose);
int       vtk_net_recv(vtk_t *vtk, vtk_msg_t *msg, int *eof, int verbose);

#endif
