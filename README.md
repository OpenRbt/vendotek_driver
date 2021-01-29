## Vendotek driver

This project provide Vendotek protocol (VTK) implementation, that allow do basic interaction between
Vendotek POS terminal and Vending Machine Controller (VMC)

#### Repository structure

- __doc__ - vendor-provided documentation aboud VTK protocol
- __src__ - source code, which contain
    - `vendotek.c, vendotek.h` - VTK protocol implementation, realized as mini-library with own API
    - `vendotek-cli.c` - client app (driver), that allow VMC to do payment operation
    - `vendotek-dbg.c` - VTK protocol debugger interactive application
- __messages__ - VTK messages for debugger

#### Build instruction

This project doesn't have any non-standard dependencies. So, if you have Linux environment with gcc
installed, you should be able to build it for easy with `make` command. Then two binaries will be
produced:
- `vendotek-cli` - client app (driver)
- `vendotek-dbg` - protocol debugger

#### Work with client app

Only purpose for the client app is to do payment operation. This goal is achieved via hard-coded messages
queue to Vendotek POS: `IDL`, `VRP`, `FIN`, and `IDL` again, with request and response messages for each.
Please take a look documentation provided to understand these operations.

There are several command line options for the client app:
```
  Available options are:
    --host       mandatory       POS hostname IP
    --port       mandatory       POS port number
    --price      mandatory       Price in minor currency units (MCU)
    --ping       optional        Connect, send IDL message, disconnect
    --prodname   optional        Product Name
    --prodid     optional        Product ID
    --evname     optional        Event Name
    --evnum      optional        Event Number
    --timeout    optional        Timeout in seconds, 60 by default
    --verbose    optional        Set verbosity level.
                                 0 - silent, 4 - errs + warnings, 7 - most verbose
                                 4 by default
```
Example 0. Ping with 3 seconds timeout
```
$ ./vendotek-cli --host 127.0.0.1 --port 1234 --ping --timeout 3
```

Example 1. Do payment of 25000 MCU (250 Rubles)
```
$ ./vendotek-cli --host 127.0.0.1 --port 1234 --price 25000
```

Example 2. Do payment of 25000 MCU (250 Rubles), silent mode
```
$ ./vendotek-cli --host 127.0.0.1 --port 1234 --price 25000 --verbose 0
```

Example 3. Payment with product name & product id. Maximum verbosity
```
$ ./vendotek-cli --host 127.0.0.1 --port 1234 --price 25000 --prodname "CARWASH" --prodid 7 --verbose 7
```

Example 4. Payment with event name and event number. Default verbosity
```
$ ./vendotek-cli --host 127.0.0.1 --port 1234 --price 25000 --prodname "CARWASH" --prodid 7 --evname "CSAPP" --evnumber 10
```

__Note!__ VMC must check `vendotek-cli` return code. E.g:
```
$ ./vendotek-cli
$ echo $?
```
Return code is equals zero for success operation - payment or ping, and non-zero if any error has occured

#### Work with protocol debugger

Protocol debugger is an interactive application that allow to simulate both VMC (client) or POS (server)
sides. The features are:
- accept incoming connections in Server (POS) mode
- to do outgoing connections in Client (VMC) mode
- create wide spectre of VTK messages via interactive commands or read them from file as a macro command

For example, you can simulate POS terminal in case of client app payment transaction in following steps
- create listen connection
- connect to debugger via `vendotek-cli`
- when you see first `IDL` request from the client, execute macro command from `messages\pos.idl.macro`
- do the same for `VRP`, then for `FIN`, and then for the second `IDL` messages
- please make sure that amount of MCUs (0x4 field) in macro files should be the same as in your client request


Please look at embedded help page when work with debugger
```
$ ./vendotek-dbg
command >
command > help
```
