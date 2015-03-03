# C/C++ REST API for Driver Benchmarks
The Software Under Test (SUT) uses the latest [cpp-driver] to execute commands
via the "Runner" application. This project is a REST API exposed over HTTP for
the [cpp-driver] using an embedded [Mongoose Web Server].

## Configuring
The SUT currently works with *nix has been tested against Ubuntu 14.04.

### Requirements
- CMake v2.8.3+
- Git
- Python 2.7.x
- Linux
 - Autotools (Autoconf)
 - GCC
 - Libtool

### Configure Command
```sh
mkdir build
cd build
cmake ..
```

NOTE: To specify branch or tag for the [cpp-driver] use the option
-DCPP_DRIVER_VERSION=<branch_or_tag> (default HEAD)

## Building
The SUT will automatically download and build the dependencies based on CMake
external projects.

### External Dependencies
- [cpp-driver]
- [EasyLogging++ v8.91]
- [libuv]
- [Mongoose Web Server]
- Linux
 - [libunwind]
- Windows
 - [StackWalker]


### Build Command
```sh
make
```

## Usage

```sh
Usage: cpp-driver-http-server [OPTION...]

  --append_logs       disable overwriting of log files (default enabled)
  --driver_level      assign driver logging level (default Debug)
                      [CRITICAL | ERROR | WARN | INFO | DEBUG | TRACE]
  --driver_logging    enable driver logging (default disabled)
  -h, --host          node to use as initial contact point
                      (default 127.0.0.1)
  -l, --listen_port   port to use for listening to HTTP requests
                      (default 8080)
  --ssl               enable SSL (default disabled)
  -v, --verbose       enable verbose log output (level = 9)
  --v=[0-9]           enable verbose log output at a level

  --help              give this help list
```

## Implemented Endpoints

Method   | Path                                         | Return Codes
---------| ---------------------------------------------| -------------
`GET`    | `/`                                          | 200
`GET`    | `/cassandra`                                 | 200, 500
`DELETE` | `/keyspace`                                  | 204, 304, 500
`POST`   | `/keyspace`                                  | 201, 304, 500
`PUT`    | `/keyspace`                                  | 204, 304, 500
`GET`    | `/prepared-statements/users/<id>/<nb_users>` | 200, ~~500~~
`POST`   | `/prepared-statements/users/<id>/<nb_users>` | 200, ~~500~~
`GET`    | `/simple-statements/users/<id>/<nb_users>`   | 200, ~~500~~
`POST`   | `/simple-statements/users/<id>/<nb_users>`   | 200, ~~500~~

## TODO

- Implement 500 (Internal server error for simple and prepared statements)
- Add [cpp-driver] cluster configurations as command line options
- Implement multi-threaded HTTP listening servers
- Refactor CQLConnection statement execution functions to take result pointer
- Add an interface to cleanup HTTP callback functionality (UGLY!)
- Update the help message to be clearer (driver vs HTTP server)
- Add Graphite/Cyanite connection for driver metrics

[cpp-driver]: (https://github.com/datastax/cpp-driver)
[EasyLogging++ v8.91]: (https://github.com/easylogging/easyloggingpp/tree/v8.91)
[libunwind]: (http://www.nongnu.org/libunwind/)
[libuv]: (https://github.com/libuv/libuv)
[Mongoose Web Server]: (https://github.com/cesanta/mongoose)
[StackWalker]: (http://stackwalker.codeplex.com/)
