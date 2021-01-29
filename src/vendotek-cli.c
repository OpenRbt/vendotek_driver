#include <errno.h>
#include <getopt.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "vendotek.h"

typedef struct stage_req_s {
    uint16_t  id;
    char     *valstr;
    ssize_t  *valint;
} stage_req_t;

typedef struct stage_resp_s {
    uint16_t  id;
    char     *valstr;
    ssize_t  *valint;
    char     *expstr;
    ssize_t  *expint;
    int       optional;
} stage_resp_t;

typedef struct stage_opts_s {
    vtk_t     *vtk;
    vtk_msg_t *mreq;
    vtk_msg_t *mresp;
    int        timeout;  /* poll timeout, ms */
    int        verbose;
    int        allow_eof;
} stage_opts_t;

int do_stage(stage_opts_t *opts, stage_req_t *req, stage_resp_t *resp)
{
    /*
     * fill & send request message
     */
    char  valbuf[0xff];
    char *value;
    vtk_msg_mod(opts->mreq, VTK_MSG_RESET, VTK_BASE_VMC, 0, NULL);

    for (int i = 0; req[i].id; i++) {
        if (req[i].valint) {
            snprintf(valbuf, sizeof(valbuf), "%lld", *req[i].valint);
            value = valbuf;
        } else if (req[i].valstr) {
            value = req[i].valstr;
        } else {
            continue;
        }
        vtk_msg_mod(opts->mreq, VTK_MSG_ADDSTR, req[i].id, 0, value);
    }
    if (opts->verbose) {
        vtk_msg_print(opts->mreq);
    }
    if (vtk_net_send(opts->vtk, opts->mreq) < 0) {
        return -1;
    }

    /*
     * wait & validate response
     */
    struct pollfd pollfd = {
        .fd     = vtk_net_get_socket(opts->vtk),
        .events = POLLIN
    };
    int rpoll = poll(&pollfd, 1, opts->timeout);

    if (rpoll < 0) {
        vtk_loge("POS connection error: %s", strerror(errno));
        return -1;
    } else if (rpoll == 0) {
        vtk_loge("POS connection timeout");
        return -1;
    }
    int fleof = 0;
    if (vtk_net_recv(opts->vtk, opts->mresp, &fleof) < 0) {
        vtk_loge("Expected event can't be received/validated");
        return -1;
    }
    if (opts->verbose) {
        vtk_msg_print(opts->mresp);
    }
    for (int i = 0; resp[i].id; i++) {
        char    *valstr = NULL;
        ssize_t  valint = 0;
        int      idfound = vtk_msg_find_param(opts->mresp, resp[i].id, NULL, &valstr) >= 0;
        int      vsfound = valstr != NULL;
        int      vifound = vsfound && (sscanf(valstr, "%lld", &valint) == 1);

        if (!idfound && !resp[i].optional) {
            vtk_loge("Expected message parameter wasn't found: 0x%x (%s)", resp[i].id, vtk_msg_stringify(resp[i].id));
            return -1;
        } else  if (!idfound) {
            continue;
        }
        if (resp[i].valstr && vsfound) {
            resp[i].valstr = valstr;
        }
        if (resp[i].valint && vifound) {
            *resp[i].valint = valint;
        }
        if (resp[i].expstr && ! (vsfound && strcasecmp(resp[i].expstr, valstr) == 0)) {
            vtk_loge("Wrong string parameter. id: 0x%x, returned: %s, expected: %s",
                     resp[i].id, valstr, resp[i].expstr);
            return -1;
        }
        if (resp[i].expint && ! (vifound && (*resp[i].expint == valint))) {
            vtk_loge("Wrong numeric parameter. id: 0x%x, returned: %lld, expected: %lld",
                     resp[i].id, valint, *resp[i].expint);
            return -1;
        }
    }

    if (! opts->allow_eof && fleof) {
        vtk_loge("Connection with POS was closed unexpectedly");
        return -1;
    }

    return 0;
}

typedef struct payment_opts_s {
    vtk_t     *vtk;
    vtk_msg_t *mreq;
    vtk_msg_t *mresp;
    int        ping;
    int        timeout;
    int        verbose;

    ssize_t    evnum;
    char      *evname;
    ssize_t    prodid;
    char      *prodname;
    ssize_t    price;
} payment_opts_t;

int do_payment(payment_opts_t *opts)
{
    /*
     * common state
     */
    struct payment_s {
        ssize_t  opnum;
        ssize_t  evnum;
        char    *evname;
        ssize_t  prodid;
        char    *prodname;
        ssize_t  price;
        ssize_t  price_confirmed;
        ssize_t  timeout;
    } payment = {
        .evnum     = opts->evnum,
        .evname    = opts->evname,
        .prodid    = opts->prodid,
        .prodname  = opts->prodname,
        .price     = opts->price,
        .timeout   = opts->timeout
    };
    int rc_idl = 0, rc_vrp = 0, rc_fin = 0;

    /*
     * 1 stage, IDL 1
     */
    vtk_logi("IDL stage");

    stage_opts_t stopts = {
        .vtk     = opts->vtk,
        .timeout = opts->timeout * 1000,
        .verbose = opts->verbose,
        .mreq    = opts->mreq,
        .mresp   = opts->mresp
    };
    if (1) {
        stage_req_t idl1_req[] = {
            {.id = 0x1, .valstr = "IDL"             },
            {.id = 0x8, .valint =  payment.evname   ? &payment.evnum : NULL  },
            {.id = 0x7, .valstr =  payment.evname   },
            {.id = 0x9, .valint =  payment.prodname ? &payment.prodid : NULL },
            {.id = 0xf, .valstr =  payment.prodname },
            {.id = 0x4, .valint = &payment.price    },
            { 0 }
        };
        stage_resp_t idl1_resp[] = {
            {.id = 0x1, .expstr = "IDL"             },
            {.id = 0x3, .valint = &payment.opnum    },
            {.id = 0x6, .valint = &payment.timeout  },
            {.id = 0x8, .valint = &payment.evnum    },
            { 0 }
        };
        rc_idl = do_stage(&stopts, idl1_req, idl1_resp) >= 0;
    }

    /*
     * 2 stage, VRP
     */
    vtk_logi("VRP stage");

    if (rc_idl) {
        stopts.timeout = payment.timeout * 1000;
        payment.opnum++;

        stage_req_t vrp_req[] = {
            {.id = 0x1, .valstr = "VRP"             },
            {.id = 0x3, .valint = &payment.opnum    },
            {.id = 0x9, .valint =  payment.prodname ? &payment.prodid : NULL },
            {.id = 0xf, .valstr =  payment.prodname },
            {.id = 0x4, .valint = &payment.price    },
            { 0 }
        };
        stage_resp_t vrp_resp[] = {
            {.id = 0x1, .expstr = "VRP" },
            {.id = 0x3, .expint = &payment.opnum    },
            {.id = 0x4, .expint = &payment.price    },
            { 0 }
        };
        rc_vrp = do_stage(&stopts, vrp_req, vrp_resp) >= 0;
    }

    /*
     * 3 stage, FIN
     */
    vtk_logi("FIN stage");

    if (rc_vrp) {
        stopts.allow_eof = 1;

        stage_req_t fin_req[] = {
            {.id = 0x1, .valstr = "FIN"             },
            {.id = 0x3, .valint = &payment.opnum    },
            {.id = 0x9, .valint =  payment.prodname ? &payment.prodid : NULL },
            {.id = 0x4, .valint = &payment.price    },
            { 0 }
        };
        stage_resp_t fin_resp[] = {
            {.id = 0x1, .expstr = "FIN" },
            {.id = 0x3, .expint = &payment.opnum },
            {.id = 0x4, .expint = &payment.price },
            { 0 }
        };
        rc_fin = do_stage(&stopts, fin_req, fin_resp) >= 0;
    }

    /*
     * 4 stage, IDL 2, always
     */
    stage_req_t idl2_req[] = {
        {.id = 0x1, .valstr = "IDL"             },
        { 0 }
    };
    stage_resp_t idl2_resp[] = {
        {.id = 0x1, .expstr = "IDL"             },
        { 0 }
    };
    do_stage(&stopts, idl2_req, idl2_resp);

    return ! (rc_idl && rc_vrp && rc_fin) ? -1 : 0;
}

int do_ping(payment_opts_t *opts)
{
    stage_opts_t stopts = {
        .vtk     = opts->vtk,
        .timeout = opts->timeout * 1000,
        .verbose = opts->verbose,
        .mreq    = opts->mreq,
        .mresp   = opts->mresp
    };
    stage_req_t idl_req[] = {
        {.id = 0x1, .valstr = "IDL"             },
        { 0 }
    };
    stage_resp_t idl_resp[] = {
        {.id = 0x1, .expstr = "IDL"             },
        { 0 }
    };
    if (do_stage(&stopts, idl_req, idl_resp) < 0) {
        return -1;
    }
    return 0;
}

void show_help(void) {
    const char *help[] = {
        "Available options are:",
        "  --host       mandatory       POS hostname IP",
        "  --port       mandatory       POS port number",
        "  --price      optional        Price in minor currency units (MCU)",
        "  --ping       optional        Connect, send IDL message, disconnect",
        "  --prodname   optional        Product Name",
        "  --prodid     optional        Product ID",
        "  --evname     optional        Event Name",
        "  --evnum      optional        Event Number",
        "  --timeout    optional        Timeout in seconds, 60 by default",
        "  --verbose    optional        Switch to Verbose mode",
        NULL
    };
    int iline;
    for (iline = 0; help[iline]; iline++) {
        vtk_logi("  %s", help[iline]);
    }
}


int main(int argc, char *argv[])
{
    /*
     * Configure
     */
    payment_opts_t popts = {
        .timeout   = 60,
        .verbose   = LOG_WARNING
    };
    char *conn_host = NULL, *conn_port = NULL;

    /* command line optios */
    const struct option longopts[] = {
        {"host",      required_argument, NULL, 'h'},
        {"port",      required_argument, NULL, 'p'},
        {"price",     required_argument, NULL, 'P'},
        {"prodname",  required_argument, NULL, 'N'},
        {"prodid",    required_argument, NULL, 'I'},
        {"evname",    required_argument, NULL, 'e'},
        {"evnum",     required_argument, NULL, 'E'},
        {"ping",      optional_argument, NULL, 'i'},
        {"timeout",   required_argument, NULL, 't'},
        {"verbose",   required_argument, NULL, 'v'},
        {NULL,        0,                 NULL,  0 }
    };
    int longind = -1;
    int opt;

    while ((opt = getopt_long(argc, argv, "", longopts, &longind)) != -1) {
        switch(opt) {
        case 'h':
            conn_host = strdup(optarg);
            break;
        case 'p':
            conn_port = strdup(optarg);
            break;
        case 'P':
            popts.price = atol(optarg);
            break;
        case 'N':
            popts.prodname = strdup(optarg);
            break;
        case 'I':
            popts.prodid = atol(optarg);
            break;
        case 'e':
            popts.evname = strdup(optarg);
            break;
        case 'E':
            popts.evnum = atol(optarg);
            break;
        case 'i':
            popts.ping = 1;
            break;
        case 't':
            popts.timeout = atol(optarg);
            break;
        case 'v':
            popts.verbose = atol(optarg);
            break;
        }
    }
    if (!conn_host || !conn_port) {
        vtk_loge("--host and --port options are mandatory. Please check documentation");
        return -1;
    }
    if ((!popts.price && !popts.ping) || (popts.price && popts.ping)) {
        vtk_loge("one of --price or --ping option should be set. Please check documentation");
        return -1;
    }
    /*
     * Initialize VTK & do payment
     */
    int rcode = 0;

    vtk_logline_set(NULL, popts.verbose);
    vtk_init(&popts.vtk);
    rcode = vtk_net_set(popts.vtk, VTK_NET_CONNECTED, popts.timeout * 1000, conn_host, conn_port);

    if (rcode >= 0) {
        vtk_msg_init(&popts.mreq,  popts.vtk);
        vtk_msg_init(&popts.mresp, popts.vtk);

        rcode = popts.ping ? do_ping(&popts) : do_payment(&popts);

        vtk_msg_free(popts.mreq);
        vtk_msg_free(popts.mresp);
    }
    vtk_free(popts.vtk);

    return rcode < 0 ? 1 : 0;
}
