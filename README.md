# tourneyd (Work in Progress)

`tourneyd` is a zero-dependency, bare-metal HTTP/1.1 web server and backend platform written from scratch in C (GNU C17) using raw POSIX sockets. It is designed to serve as the core infrastructure for hosting Capture The Flag (CTF) competitions and archiving offensive security and vulnerability research writeups.

> **Disclaimer:** This is strictly an **educational systems programming project**, not a commercial product. It is built to explore low-level operating system primitives, network protocol framing, memory safety, and server concurrency models without relying on external web frameworks or third-party networking libraries.

---

## Inspiration

This project began as a practical implementation of the theoretical networking foundations in ***Computer Networking: A Top-Down Approach* by James F. Kurose and Keith W. Ross**. Moving from textbook diagrams of the TCP/IP stack to working C code meant translating core transport- and application-layer concepts directly into Linux system calls:

* **Transport Layer (TCP Byte Streams):** Handling the reality that TCP is a stream protocol rather than a message-boundary protocol—accounting for partial `read()`/`write()` calls, arbitrary packet fragmentation across network buffers, and kernel signal interruptions (`EINTR`).
* **Application Layer (HTTP/1.0 & HTTP/1.1):** Implementing RFC-compliant text framing (`\r\n` delimiters), state-machine request parsing, header validation, status code semantics, MIME type negotiation, and connection lifecycle management directly over reliable socket pipes.

---

## Architecture Overview

The codebase is organized into decoupled layers with strict header boundaries so that the underlying concurrency engine, transport layer, and application logic can evolve independently:

```text
tourneyd/
├── include/               # Public header interfaces per module
│   ├── app/               # Application route & handler declarations
│   ├── concurrency/       # Unified concurrency engine interface
│   ├── connections/       # HTTP connection lifecycle orchestrator
│   ├── core/              # Server bootstrap & configuration structs
│   ├── db/                # Upcoming database layer interfaces
│   ├── http/              # Parser, router, serializer, MIME & URL headers
│   ├── net/               # Listener & socket connection primitives
│   └── util/              # Thread/fork-safe logging utilities
├── src/                   # Implementation files mirroring include/
│   ├── app/               # routes.c, static_files.c, hello.c
│   ├── concurrency/       # fork_model.c, serial_model.c
│   ├── connections/       # http_connection.c
│   ├── core/              # server.c, config.c
│   ├── db/                # Models & database bindings (Planned)
│   ├── http/              # parser.c, router.c, response.c, mime.c, status.c, url.c
│   ├── net/               # listener.c, conn.c
│   ├── util/              # log.c
│   └── main.c             # Entry point
├── migrations/            # Upcoming SQLite schema migrations
├── scripts/               # Build & test automation scripts
├── tests/                 # Unit & load test suites
└── Makefile               # Dynamic build system with concurrency selection
```

| Module | Directory | Responsibility |
| :--- | :--- | :--- |
| **Core & Config** | `src/core/` | Server lifecycle orchestration (`server_init`, `server_run`, `server_shutdown`), signal setup, and runtime configuration (`config_t`). |
| **Networking** | `src/net/` | Dual-stack (IPv4/IPv6) TCP welcome socket creation (`listener.c`) and `EINTR`-safe client socket I/O wrappers (`conn.c`). |
| **Concurrency** | `src/concurrency/` | Pluggable accept-loop engines selected at compile time (`fork_model.c` and `serial_model.c`). |
| **Connections** | `src/connections/` | HTTP connection orchestrator (`http_connection.c`) bridging raw socket buffers, the HTTP parser, router, and response serializer. |
| **HTTP Engine** | `src/http/` | Incremental state-machine request parser, read-only route dispatcher, response serializer, percent-decoder, and MIME resolver. |
| **Application** | `src/app/` | Route registration (`routes.c`), hardened static file delivery (`static_files.c`), and endpoint handlers (`hello.c`). |
| **Database (Planned)** | `src/db/`, `migrations/` | Placeholder structure for the upcoming embedded SQLite persistence layer. |
| **Utilities** | `src/util/` | Thread- and fork-safe atomic timestamped logging to `STDERR_FILENO` (`log.c`). |
| **Test Suite** | `tests/unit/` | Standalone unit tests for incremental parsing, response serialization, and router precedence. |

### Compile-Time Concurrency Selection
The `Makefile` dynamically filters `src/concurrency/*_model.c` and links only the concurrency engine specified via the `CONCURRENCY` variable (defaulting to `fork`). Because every model implements the common `concurrency.h` interface (`concurrency_run` and `concurrency_shutdown`), switching between single-process iterative execution and multi-process forking requires zero changes to the core server or HTTP layers.

---

## How the Server Works So Far

### 1. Bootstrap & Dual-Stack TCP Listener
1. **Configuration:** `main()` initializes default settings (`port = "8080"`, `backlog = 128`, `doc_root = "./public"`) and invokes `server_init()`.
2. **Signal & Path Initialization:** The server ignores `SIGPIPE` (`signal(SIGPIPE, SIG_IGN)`) so abrupt client disconnects on write operations do not terminate the server process, canonicalizes the document root path via POSIX `realpath()`, and populates the in-memory routing table.
3. **Socket Binding (`listener_create`):** Using `getaddrinfo()` configured with `AF_UNSPEC`, `SOCK_STREAM`, and `AI_PASSIVE`, the server iterates through available IPv4/IPv6 interfaces, applies `SO_REUSEADDR` via `setsockopt()` for rapid port rebinding during debugging, binds the descriptor, and marks it as a passive welcome socket with `listen()`.

### 2. Concurrency & Process Management
Once the welcome socket is listening, `server_run()` passes the listening file descriptor and the `http_connection_handle` callback to `concurrency_run()`:
* **Fork Model (`fork_model.c`):**
  * Registers a `SIGCHLD` handler using `sigaction` with `SA_RESTART | SA_NOCLDSTOP`. When child worker processes exit, the handler reaps zombie processes non-blockingly in a `while (waitpid(-1, NULL, WNOHANG) > 0)` loop while preserving `errno`.
  * Blocks on `conn_accept()`, which wraps `accept()` in an `EINTR`-retry loop and stores client address metadata inside a 128-byte `struct sockaddr_storage` container for IPv4/IPv6 compatibility.
  * Calls `fork()` for each accepted connection: the parent closes its copy of the client descriptor and immediately loops back to `conn_accept()`, while the child closes the listening socket, handles the HTTP exchange, closes the connection, and terminates cleanly via `_exit(0)`.
* **Serial Model (`serial_model.c`):** Accepts and handles one client connection at a time in the main process, ideal for deterministic debugging and Valgrind memory analysis.

### 3. Connection Orchestration & Sliding Buffer I/O
Inside `http_connection_handle()` (`src/connections/http_connection.c`):
* The server heap-allocates `http_request_t` (~30 KB) and `http_response_t` via `malloc()` to keep large header/path buffers off the call stack.
* `read_request()` reads bytes from the client socket into an 8,192-byte buffer (`READ_BUFFER_SIZE`) using `conn_read()`.
* After each read, the accumulated buffer is fed to `http_parse()`, and any unconsumed trailing bytes are shifted to the front of the buffer via `memmove()` until the parser reaches `PARSE_DONE` or `PARSE_ERROR`.

### 4. Incremental State-Machine HTTP Parser
`http_parse()` (`src/http/parser.c`) implements a zero-trust, byte-stream state machine capable of parsing requests across arbitrary TCP segment boundaries (tested down to 1-byte incremental chunks):
* **`PARSE_REQ_LINE`:** Scans for `\r\n`, extracts the HTTP method (`GET`, `POST`, `HEAD`, `PUT`, `DELETE`, `PATCH`, `OPTIONS`), validates that the URI begins with `/`, splits the path and query string at `?`, rejects ASCII control characters (`< 0x20` or `0x7F`), and verifies the protocol version (`HTTP/1.0` or `HTTP/1.1`).
* **`PARSE_HEADERS`:** Parses up to `MAX_HEADERS` (50) headers. Rejects whitespace inside header names, trims leading and trailing horizontal whitespace from values, and transitions on an empty `\r\n` line.
* **`finish_headers` & `PARSE_BODY`:** Explicitly rejects `Transfer-Encoding` with `501 Not Implemented`. If `Content-Length` is present, validates that it is strictly numeric and within `MAX_BODY_SIZE` (1 MB), allocates a heap buffer for the payload, and streams raw body bytes until `body_received == content_length`.
* **DoS & Overflow Guardrails:** Enforces strict compile-time caps on line length (`4096`), path/query lengths (`1024`), header names (`64`), and header values (`512`), mapping violations to appropriate HTTP error codes (`400`, `413`, `414`, `431`, `501`, `505`).

### 5. Read-Only Route Dispatch
`router_dispatch()` (`src/http/router.c`) matches the parsed request in $O(N)$ time against a static table of up to 64 routes populated at startup (designed to be read-only at runtime so it remains lock-free across processes, threads, and `epoll` workers):
* **Scoring & Precedence:** Supports both exact matches (`router_add`) and prefix wildcards (`router_add_prefix`). Exact matches receive the maximum unsigned score (`(size_t)-1`), beating any prefix route, while the longest matching prefix wins among prefix routes.
* **Method Resolution & `HEAD` Aliasing:** Automatically routes `HEAD` requests to registered `GET` handlers. If a path matches one or more routes but none support the requested HTTP method, the router tracks allowed methods using bitwise flags and returns `405 Method Not Allowed` with a dynamically constructed `Allow` header.
* **Fallback Routing:** Unmatched `GET` and `HEAD` requests fall through to the registered fallback handler (`static_files_handler`), while unmatched non-`GET` requests return `404 Not Found`.

### 6. Hardened Static File Delivery & Response Serialization
* **Directory Traversal & Null-Byte Defense (`static_files.c` & `url.c`):**
  1. `http_url_decode()` decodes `%XX` hex sequences using bitwise shifts while explicitly rejecting `%00` null-byte injections with `400 Bad Request`.
  2. Any decoded path containing `/.` is immediately rejected with `404 Not Found` to block `..` directory traversal attempts and hide dotfiles (such as `.git` or `.env`).
  3. Requests ending with `/` automatically resolve to `index.html`.
  4. The target path is canonicalized via `realpath()` and verified with `is_inside_root()` to guarantee it resides strictly inside the configured document root before opening the descriptor with `O_RDONLY | O_CLOEXEC`.
  5. `fstat()` verifies that the target is a regular file (`S_ISREG`) and does not exceed `STATIC_MAX_FILE` (8 MB), serving the payload with `X-Content-Type-Options: nosniff`.
* **Response Framing (`response.c`):**
  * `http_response_add_header()` validates header names and blocks `\r` and `\n` in values to prevent CRLF injection / HTTP response splitting.
  * `http_response_serialize_head()` formats the `HTTP/1.1` status line, thread-safe RFC 1123 GMT `Date` (`gmtime_r`), `Server: tourneyd`, exact `Content-Length`, and `Connection` headers while ignoring any handler attempts to override reserved framing headers.
  * `send_response()` writes the serialized header block and body payload (omitting the body on `HEAD` requests) over the socket via `conn_write_all()`, which loops over `write()` until every byte is transmitted.

---

## Building, Running & Testing

### Build the Server
Compile with the default multi-process `fork` concurrency model:
```bash
make
```

Compile with the single-process iterative `serial` model:
```bash
make clean
make CONCURRENCY=serial
```

### Run the Server
Start the binary (listens on port `8080` and serves `./public` by default):
```bash
./build/tourneyd
```

### Run Unit Tests
Execute the unit test suite covering the incremental HTTP parser, response serializer, and router:
```bash
./scripts/run_unit_tests.sh
```

---

## Project Roadmap

`tourneyd` is being developed incrementally across four major architectural milestones:

- [x] **Phase 1: Bare-Metal Networking, Fork Concurrency & HTTP/1.1 Core (Current)**
  - Dual-stack TCP listener, `EINTR`-safe socket primitives, `serial` and `fork` concurrency models, incremental HTTP/1.1 parser, priority router, traversal-hardened static file server, and unit test suite.
- [ ] **Phase 2: Database Layer (`SQLite`)**
  - Integrate SQLite in `src/db/` and `migrations/` to support persistent storage for CTF challenges, flag verification, user submissions, and vulnerability writeups.
- [ ] **Phase 3: Multi-Threaded Concurrency (`pthreads`)**
  - Implement a POSIX threads (`pthread`) concurrency model (`pthread_model.c`) with a thread-pool worker queue to benchmark shared-memory multi-threading against process forking.
- [ ] **Phase 4: Event-Driven Non-Blocking I/O (`epoll`) & TLS (Final Goal)**
  - Transition connection handling to an asynchronous, non-blocking state machine driven by Linux `epoll` (`epoll_model.c`), and integrate TLS encryption to serve high-concurrency HTTPS traffic.