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
queue to Vendotek POS: `IDL (request/response)`, `VRP (request/pesponse)`, `FIN (request)/response`.
Please take a look documentation provided to understand these operations.

There are several command line options for the client app:
```
  Available options are:
    --host       mandatory       POS hostname IP
    --port       mandatory       POS port number
    --price      mandatory       Price in minor currency units (MCU)
    --prodname   optional        Product Name
    --prodid     optional        Product ID
    --evname     optional        Event Name
    --evnum      optional        Event Number
    --timeout    optional        Timeout in seconds, 60 by default
    --verbose    optional        Switch to Verbose mode
```

Example 1. Do payment of 35000 MCU (350 Rubles)
```
$ ./vendotek-cli --host 192.168.1.51 --port 62801 --price 35000
```

Example 2. Do payment of 35000 MCU (350 Rubles), verbose mode
```
$ ./vendotek-cli --host 192.168.1.51 --port 62801 --price 35000 --verbose
```

Example 3. The same as in above. Add product name & product id
```
$ ./vendotek-cli --host 192.168.1.51 --port 62801 --price 35000 --prodname "CARWASH" --prodid 7 --verbose
```

Example 4. The same as in above. Add POS event name and event number
```
$ ./vendotek-cli --host 192.168.1.51 --port 62801 --price 35000 --prodname "CARWASH" --prodid 7 --evname "CSAPP" --evnumber 10 --verbose
```

__Note!__ VMC must check `vendotek-cli` return code. E.g:
```
$ ./vendotek-cli
$ echo $?
```
Return code is equals zero for succeed payment, and non-zero if any error was occured (you will see text
message on stderr stream in this case)

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
- do the same for `VRP` and `FIN` commands
- please make sure that amount of MCUs (0x4 field) in macro files should be the same as in your client request


Please look at embedded help page when work with debugger
```
$ ./vendotek-dbg
command >
command > help
```
