# Web Construct

### WebAssembly virtual machine for servers, clusters, and distributed executions

Construct is a **[single system image](https://en.wikipedia.org/wiki/Single_system_image)
runtime environment** for [WebAssembly](https://webassembly.org/) applications. It
facilitates the execution of [WebAssembly](https://webassembly.org/) binaries among
multiple servers.

- [Distributed shared memory](https://en.wikipedia.org/wiki/Distributed_shared_memory) model.
Message-passing is accomplished through shared-memory and trapdoors. The runtime provides
the actual communication; the application may also shape communication with fences and
configuration parameters.

- Automatic [checkpointing](https://en.wikipedia.org/wiki/Application_checkpointing) of application state in case of failure.
The failure of any node (or minority of nodes) will have minimal impact as other nodes
can continue the execution. The runtime is concerned with the *observed* effects of
an application for coherent automatic recovery.

- Transparent [migration](https://en.wikipedia.org/wiki/Process_migration) for load, I/O and data-locality.
The system will optimize the time and place(s) of execution for applications based
on several factors and configuration parameters. This is founded on a data-oriented
strategy where execution may occur on a node which already has a memory segment rather
than paging that memory over to the execution.

- [Single-level storage](https://en.wikipedia.org/wiki/Single-level_store) exclusively via memory mappings.
The runtime provides shared memory between applications and may persist memory segments after
they terminate. Userspace daemons can then organize a named filesystem hierarchy if desired.

- Optimistic consistency through [software transactional memory](https://en.wikipedia.org/wiki/Software_transactional_memory).
By default an application's memory is intuitively strongly-ordered and coherent: a suite of
concurrency primitives are then offered to relax and parallelize execution. Traditional
interfaces such as fences, atomics, and mutexes are available to application programmers. This is
extended by an optimistic execution feature with commit/rollback provided by the runtime:
branches may be taken by nodes on a speculative basis where conflicting executions are rolled-back
to be retried with updated memory.

#### Construct can execute untrusted bytecodes received from remote clients.

Construct provides the latest generation of web applications a flexible way
to interface with their server component by constructing a server-side environment
with access to a potentially large and complex dataset. Clients then send code
for execution in the environment -- nearer to the data than the client. Only
the specific result desired by the client developer is returned over the wire.

### Dependencies

- **Boost** (1.66 or later)
- **RocksDB** (based on LevelDB):
- **Sodium** (NaCl crypto):
- **OpenSSL** (libssl/libcrypto):
- **GNU C++ compiler**, **automake**, **autoconf**, **autoconf2.13**,
**autoconf-archive**, **libtool**, **shtool**

##### Additional dependencies

- **libmagic** (~Optional~):
- **zlib** or **lz4** or **snappy** (Optional):


#### Platforms

[![Construct](https://img.shields.io/SemVer/v0.0.0-dev.png)](https://github.com/jevolk/charybdis/tree/master)

| <sub> Continuously Integrated Host </sub>   | <sub> Compiler </sub>    | <sub> Third party </sub> | <sub> Status </sub> |
|:------------------------------------------- |:------------------------ |:------------------------ |:------------------- |
| <sub> Linux Ubuntu 16.04 Xenial </sub>      | <sub> GCC 6       </sub> | <sub> Boost 1.66 </sub>  | [![POSIX Build Status](https://travis-ci.org/jevolk/charybdis.svg?branch=master)](https://travis-ci.org/jevolk/charybdis) |


## Installation

```
./autogen.sh
./configure
make
sudo make install
```

#### (STANDALONE)

*Intended to allow building with dependencies that have not made their way
to mainstream systems.*

```
./autogen.sh
mkdir build
```

- The install directory may be this or another place of your choosing.
- If you decide elsewhere, make sure to change the `--prefix` in the `./configure`
statement below.

```
CXX=g++-6 ./configure --prefix=$PWD/build --with-included-boost=shared --with-included-rocksdb=shared
```

- Many systems alias `g++` to an older version. To be safe, specify a version manually
in `CXX`. This will also build the submodule dependencies with that version.
- The `--with-included-*` will fetch, configure **and build** the dependencies included
as submodules. Include `=shared` for now until static libraries are better handled.

```
make install
```

#### Building from git (DEVELOPMENT)

Development builds should follow the same instructions as the standalone
section above while taking note of the following `./configure` options:

##### Debug mode

```
--enable-debug
```
Full debug mode. Includes additional code within `#ifdef RB_DEBUG` sections.
Optimization level is `-Og`, which is still valgrind-worthy. Debugger support
is `-ggdb`. Log level is `DEBUG` (maximum). Assertions are enabled.


##### Manually enable assertions

```
--enable-assert
```
Implied by `--enable-debug`. This is useful to specifically enable `assert()`
statements when `--enable-debug` is not used.


##### Manually enable optimization

```
--enable-optimize
```
This manually applies full release-mode optimizations even when using
`--enable-debug`. Implied when not in debug mode.


##### Logging level

```
--with-log-level=
```
This manually sets the level of logging. All log levels at or below this level
will be available. When a log level is not available, all code used to generate
its messages will be entirely eliminated via *dead-code-elimination* at compile
time.

The log levels are (from logger.h):
```
7  DEBUG      Maximum verbosity for developers.
6  DWARNING   A warning but only for developers (more frequent than WARNING).
5  DERROR     An error but only worthy of developers (more frequent than ERROR).
4  INFO       A more frequent message with good news.
3  NOTICE     An infrequent important message with neutral or positive news.
2  WARNING    Non-impacting undesirable behavior user should know about.
1  ERROR      Things that shouldn't happen; user impacted and should know.
0  CRITICAL   Catastrophic/unrecoverable; program is in a compromised state.
```

When `--enable-debug` is used `--with-log-level=DEBUG` is implied. Otherwise
for release mode `--with-log-level=INFO` is implied. Large deployments with
many users may consider lower than `INFO` to maximize optimization and reduce
noise.
