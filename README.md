# mini-redis

A Redis-inspired in-memory key-value server in C++20 for Linux.

## Index

- [Features](#features)
- [Architecture](#architecture)
- [Protocol](#protocol)
- [Build and run](#build-and-run)
- [Testing](#testing)
- [What I learned](#what-i-learned)
- [Known limitations](#known-limitations)
- [Roadmap](#roadmap)
- [Project structure](#project-structure)
- [License](#license)
- [Author](#author)

## Features

- TCP server listening on port 8080.
- Non-blocking sockets with edge-triggered `epoll`.
- Fixed-size pool of 10 worker threads.
- Sharded in-memory store with 16 buckets.
- Custom writer-preferring reader-writer lock with RAII guards.
- Per-client buffering for partial and pipelined commands.
- Input validation, syscall error reporting, and handling for partial writes.
- A client load tester that opens concurrent clients and validates replies.

## Architecture

The epoll thread accepts connections and reads commands. Parsed commands are
dispatched to worker threads, which access the sharded store.

```text
                         +----------------------+
                         |  Non-blocking clients |
                         +----------+-----------+
                                    |
                                    | TCP :8080
                                    v
                         +----------------------+
                         |  epoll thread        |
                         |  EPOLLET             |
                         |  accept/read/parse    |
                         +----------+-----------+
                                    |
                         parsed commands
                                    v
                         +----------------------+
                         |  10 worker threads   |
                         |  mutex + condition   |
                         |  variable task queue |
                         +----------+-----------+
                                    |
                                    v
                 +-----------------------------------------+
                 |  16-bucket sharded in-memory store       |
                 |  unordered_map + writer-preferring lock |
                 +-----------------------------------------+
```

The epoll thread drains accept and read loops until `EAGAIN`. Input is buffered
per client because TCP is a byte stream without message boundaries. Each
bucket has its own reader-writer lock. `ReadGuard` and `WriteGuard` release
their locks through RAII.

## Protocol

The protocol is text-based and case-sensitive. Every line ends with `\r\n`.

| Command | Reply |
|---|---|
| `SET <key> <value>` | `SUCCESS` |
| `GET <key>` | The value, or `nil` if the key does not exist |
| Anything else, or the wrong argument count | `ERROR unknown or malformed command` |

`SET` requires exactly 3 tokens and `GET` requires exactly 2 tokens. Values
cannot contain spaces. Empty lines are ignored. The string `NULL` is reserved
internally as the miss marker.

## Build and run

The project requires CMake 3.22 or newer and a C++20-compatible compiler. From
the repository root, configure and build both executables with:

```sh
cmake -S . -B build
cmake --build build --parallel
```

The executables are created inside the `build/` directory. Start the server in
one terminal, then run the load-testing client in another:

```sh
./build/server
./build/client
```

For an optimized build, select the Release configuration during configuration:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

For many clients, raise the file descriptor limit first:

```sh
ulimit -n 4096
```

## Testing

Start `./build/server`, then use `printf` with `\r\n` when testing with `nc`:

```sh
printf 'SET name kartik\r\n' | nc -q1 127.0.0.1 8080
printf 'GET name\r\n' | nc -q1 127.0.0.1 8080
```

Do not use `nc -N` for tests. A half-close can cause the server to close the
connection before replies are sent.

`./build/client` opens `N` concurrent non-blocking clients, using half for `SET` and
half for `GET`. It validates every reply and prints how many clients
completed, how many replies were wrong, and how many `GET` misses occurred.
The number of misses varies by run because it depends on whether a `SET` lands
first.

## What I learned

- A `GET` path returned before releasing its read lock, deadlocking later
  `SET` operations. RAII lock guards fixed this.
- With edge-triggered `epoll`, accepting or reading only once stranded clients.
  Both loops must continue until `EAGAIN`.
- An early client close caused a silent exit with code 141 because of
  `SIGPIPE`. Using `send(..., MSG_NOSIGNAL)` fixed this.
- A bare `SET` indexed past the end of a token vector and crashed a worker.
  Token counts are now validated before indexing.
- Per-client buffers had to be erased on close so a recycled file descriptor
  could not inherit another client's data.
- Checking syscall return values and printing `perror` messages made failures
  visible instead of silent.
- The test client initially appended the whole read buffer instead of only the
  bytes read, quit after the first reply, and looped on epoll iterations
  instead of completed clients.

## Known limitations

- Commands from one connection can run on different workers, so a pipelined
  `SET` followed by `GET` may be reordered.
- Replies are not buffered. If a client's receive buffer is full, a reply can
  be dropped.
- Workers receive the file descriptor as a raw integer, so a very fast
  disconnect and reconnect could misdeliver a reply.
- A client that half-closes immediately after sending, such as `nc -N`, may
  receive no replies because the connection closes when EOF is read.
- Exceptions inside worker tasks are not caught.
- The default limit is about 1000 concurrent connections, based on the file
  descriptor limit. Raise it with `ulimit -n`; after reaching it, the listener
  does not recover on its own.
- This is a custom line protocol, not RESP. There is no persistence, expiry,
  or `DEL` command.
- The writer-preferring reader-writer lock can starve readers under heavy
  writes.

## Roadmap

- Use `shared_ptr<Connection>` to own the file descriptor and prevent replies
  from reaching a recycled descriptor.
- Add a per-connection command queue to guarantee reply order.
- Add output buffering with `EPOLLOUT` instead of dropping replies.
- Add `SIGINT`/`SIGTERM` handling with clean shutdown.
- Add a Dockerfile.
- Add RESP support so `redis-cli` and `redis-benchmark` can be used.
- Add `DEL` and `EXPIRE` commands.
- Replace the `"NULL"` sentinel with `optional<string>`.

## Project structure

```text
├── CMakeLists.txt
├── include/
│   ├── read_write_lock.hpp   Custom reader-writer lock with RAII guards
│   ├── threadPool.hpp        Fixed-size thread pool
│   └── threadSafeCache.hpp   Sharded in-memory store
└── src/
    ├── client.cpp             Epoll-based load tester that validates replies
    └── server.cpp             Server implementation
```

## License

This project is licensed under the [MIT License](LICENSE).

## Author

Created by [Krtk-k](https://github.com/Krtk-k)
