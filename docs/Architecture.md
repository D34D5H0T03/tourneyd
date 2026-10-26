# Architecture

**Applies to:** tourneyd `v1.0` (fork concurrency model)
**Last updated:** 2026-10-26

This document explains how tourneyd is put together: the layers and the one rule that connects them, the life of a request, how concurrency and the database are handled, the decisions behind the design, and what will change when threads, `epoll` and TLS arrive.

## Contents

- [Architecture](#architecture)
  - [Contents](#contents)
  - [1. Goals and constraints](#1-goals-and-constraints)
  - [2. Layers and the dependency rule](#2-layers-and-the-dependency-rule)
  - [3. Module map](#3-module-map)
  - [4. Startup, configuration and shutdown](#4-startup-configuration-and-shutdown)
  - [5. The life of a request](#5-the-life-of-a-request)
  - [6. Concurrency layer](#6-concurrency-layer)
    - [serial model](#serial-model)
    - [fork model](#fork-model)
  - [7. Connection layer](#7-connection-layer)
  - [8. HTTP layer](#8-http-layer)
    - [Parser (`http/parser.c`)](#parser-httpparserc)
    - [Router (`http/router.c`)](#router-httprouterc)
    - [Response (`http/response.c`)](#response-httpresponsec)
    - [URL helpers, MIME, status](#url-helpers-mime-status)
  - [9. Application layer](#9-application-layer)
    - [Static files (`app/static_files.c`)](#static-files-appstatic_filesc)
    - [API handlers (`app/*_api.c`)](#api-handlers-app_apic)
    - [Routes (`app/routes.c`)](#routes-approutesc)
  - [10. Data layer](#10-data-layer)
    - [Connections](#connections)
    - [Migrations (`db/migrate.c`)](#migrations-dbmigratec)
    - [Models (`db/models/*.c`)](#models-dbmodelsc)
    - [`tourneyctl`](#tourneyctl)
  - [11. Frontend](#11-frontend)
  - [12. Conventions](#12-conventions)
  - [13. Design decisions](#13-design-decisions)
  - [14. What changes for threads, epoll and TLS](#14-what-changes-for-threads-epoll-and-tls)
  - [15. Cookbook](#15-cookbook)

---

## 1. Goals and constraints

- **Learning first.** Every layer is written directly against the operating system so its behavior can be observed (`strace`, `ss`, `/proc`) and compared across concurrency models.
- **One application, several concurrency models.** The same parser, router, handlers and database code must run unchanged under `serial`, `fork`, and later `thread` and `epoll`. Only the code that decides *who runs a connection* may differ.
- **Separation of concerns.** Sockets know nothing about HTTP. The HTTP layer knows nothing about sockets or files. Handlers know nothing about sockets. Only the model layer knows SQL.
- **Hostile input by default.** Every byte from a socket is untrusted, every size is bounded, and every limit is tested.
- **Small dependency surface.** No third-party networking or web code. SQLite is vendored, OpenSSL's libcrypto is used for hashing (and later TLS).

## 2. Layers and the dependency rule

```text
                    ┌───────────────────────────────────────────────┐
   main.c ────────► │ core: config, signals, server lifecycle       │
                    └───────────────────┬───────────────────────────┘
                                        ▼
                    ┌───────────────────────────────────────────────┐
                    │ concurrency: owns the accept loop             │   fork / serial
                    │ (later: thread, epoll)                        │   (later: more)
                    └───────────────────┬───────────────────────────┘
                                        ▼  runs one connection handler per client
                    ┌───────────────────────────────────────────────┐
                    │ connections: http_connection_handle           │   keep-alive loop,
                    │                                               │   deadlines, access log
                    └───────────┬───────────────────────┬───────────┘
                                ▼                       ▼
                    ┌───────────────────┐   ┌─────────────────────────────┐
                    │ net: conn_t, I/O  │   │ http: parser, router,       │
                    │ listener          │   │ response, url, mime, status │
                    └───────────────────┘   └──────────────┬──────────────┘
                                                           ▼
                                            ┌─────────────────────────────┐
                                            │ app: route handlers         │
                                            │ static files, JSON API      │
                                            └──────────────┬──────────────┘
                                                           ▼
                                            ┌─────────────────────────────┐
                                            │ db: models, SQLite          │
                                            └─────────────────────────────┘

   util/ (log, clock, shutdown flag, hash, json, slug, utf8) is usable from any layer.
```

**The dependency rule:** arrows only point downward, and three boundaries are never crossed.

| Boundary | Rule | Why |
|---|---|---|
| Sockets | Only `net/` and `connections/` touch a `conn_t`. The parser, router, handlers and models never see a file descriptor. | Handlers run unchanged when I/O becomes non-blocking, and TLS can live entirely inside `net/conn.c`. |
| HTTP | `http/` takes bytes in and structs out. The parser never calls `read()`. | The same parser works under blocking reads and under an event loop. |
| SQL | `sqlite3.h` is included only under `db/`. Handlers call model functions and receive plain structs. | Secrets (flag hashes) cannot leak through a struct that has no field for them, and the storage engine is replaceable. |

## 3. Module map

| Directory | Files | Responsibility |
|---|---|---|
| `src/` | `main.c` | Parses config, then calls `server_init`, `server_run`, `server_shutdown`. Nothing else. |
| `src/core/` | `config.c`, `signals.c`, `server.c` | Defaults, file and flag parsing, validation. Signal handlers that only set a flag. Startup order, and building the per-module option structs from the config. |
| `src/net/` | `listener.c`, `conn.c` | Dual-stack listening socket. `accept`, `read`, `write` wrappers that retry on `EINTR`, enforce absolute deadlines, and never raise `SIGPIPE`. |
| `src/concurrency/` | `serial_model.c`, `fork_model.c` | The accept loop and the decision of who runs each connection. One of them is compiled in. |
| `src/connections/` | `http_connection.c` | The per-connection loop: read, parse, route, respond, log, decide on keep-alive. |
| `src/http/` | `parser.c`, `router.c`, `response.c`, `url.c`, `mime.c`, `status.c` | The HTTP protocol, independent of sockets and files. |
| `src/app/` | `routes.c`, `static_files.c`, `api_util.c`, `challenges_api.c`, `writeups_api.c` | Application behavior. Route registration, static files, and the JSON API. |
| `src/db/` | `db.c`, `migrate.c`, `models/*.c` | Connection management, schema migrations, and the model layer. |
| `src/util/` | `log.c`, `clock.c`, `shutdown.c`, `hash.c`, `json.c`, `slug.c`, `utf8.c` | Small shared helpers. |
| `tools/` | `tourneyctl.c` | The admin CLI. It links the same model layer as the server. |

## 4. Startup, configuration and shutdown

**Configuration.** `config_t` is filled once: built-in defaults, then the config file, then command-line flags. It is validated (including against compile-time caps) and never changes afterwards. Lower layers do not read `config_t`. `server_init` copies the few values each module needs into small option structs (`http_limits_t`, `http_conn_opts_t`, `concurrency_opts_t`) that the modules define themselves. This keeps the parser testable without a config file and keeps the dependency arrow pointing at `core`.

**Startup order** (`server_init`), chosen so nothing is shared before it is ready and nothing is open when `fork()` happens:

1. Install signal handlers (`SIGINT`/`SIGTERM` set a flag, `SIGPIPE` is ignored).
2. Open the database once, apply pending migrations, **close it again** (`db_init`).
3. Build the route table (`router_init`, `app_register_routes`) and canonicalize the document root (`static_files_init`).
4. Create the listening socket (`listener_create`).
5. Hand control to `concurrency_run`.

After step 3 the route table and the configuration are read-only. That is what makes them safe to share across forked children, threads and an event loop without locks.

**Shutdown.** `SIGINT` or `SIGTERM` only sets a flag (`shutdown_requested()`). The accept loop checks it between short `poll()` waits, so it never sits blocked in `accept()` after the signal. The parent then stops accepting, signals exactly its own children (it tracks their PIDs), and waits up to `shutdown_grace_sec`. Idle keep-alive children leave immediately, children in the middle of a response finish it and send `Connection: close`, and anything still running after the grace period gets `SIGKILL`.

## 5. The life of a request

```text
client ──TCP──► listener (net/listener.c)
                   │ accept()
                   ▼
        concurrency_run (fork_model.c)
                   │ fork()  ── parent: close client fd, back to accept
                   ▼ child:  close listener, apply rlimits
        http_connection_handle (connections/http_connection.c)
          loop per request:
            1. init request/response objects
            2. read_request: wait for the first byte (idle deadline),
               then receive the rest (request deadline); feed http_parse;
               keep leftover bytes for pipelined requests
            3. parse error ──► error page (status from the parser)
               parse done  ──► router_dispatch (http/router.c)
            4. handler fills the response (app/*)  ── may call models (db/)
            5. add security headers, serialize the head, send head + body
            6. write one access-log line
            7. keep-alive? (HTTP version, Connection header, status, limits,
               shutdown) ── yes: loop, no: close and exit the child
```

**Worked example: submitting a flag.**

```text
POST /api/challenges/layers/submit        body: flag{...}
  parser      → request with method, path, headers, body (≤ 1 MiB)
  router      → prefix route "/api/challenges/" (POST) → challenge_submit_handler
  handler     → validate slug, body 1..256 bytes, no NUL, trim whitespace
  model       → challenge_check_flag():
                  SELECT id, salt, hash, recent-attempt count   (prepared statement)
                  count ≥ 30 in the last 60 s?  → DB_RATE_LIMITED  (handler: 429)
                  SHA-256(salt || flag), constant-time compare
                  INSERT INTO submissions (challenge_id, correct)
  handler     → {"correct":true|false} with status 200, Cache-Control: no-store
  connection  → headers + body, access log (the body is never logged)
```

## 6. Concurrency layer

**The interface** (`include/concurrency/concurrency.h`):

```c
typedef void (*conn_handler_fn)(conn_t *client, void *ctx);
int  concurrency_run(int listen_fd, conn_handler_fn handler, void *ctx,
                     const concurrency_opts_t *opts);
void concurrency_shutdown(void);
```

Every model implements exactly this. The Makefile compiles one of `src/concurrency/*_model.c` (`make CONCURRENCY=fork|serial`), so there are no `#ifdef`s. The accept loop lives in the concurrency layer rather than in `main` because an event-loop model has to own the listening socket itself.

`ctx` is a read-only pointer to the connection handler's options. The concurrency layer passes it through without looking inside.

### serial model

Accepts and handles one connection at a time in the main process. Useful for debugging and memory checking, and as the baseline in benchmarks.

### fork model

- **Accept loop.** Waits in `poll()` with a short timeout, then `accept()`, then `fork()`. The parent closes its copy of the client descriptor right away, and the child closes the listening socket.
- **Worker cap.** The parent counts live children. At `max_children` it stops accepting and waits in `sigsuspend()`, with `SIGCHLD`, `SIGINT` and `SIGTERM` blocked around the check so a wakeup cannot be lost. Clients that arrive meanwhile queue in the kernel's listen backlog.
- **Reaping and bookkeeping.** The `SIGCHLD` handler (`SA_RESTART | SA_NOCLDSTOP`) loops over `waitpid(-1, ..., WNOHANG)`, decrements the live count, clears the child's slot in a PID table, and records children that died from a signal. The parent's loop reports those in the error log, because a handler cannot log safely.
- **Resource limits in the child.** Address space, CPU time and open files are capped before the handler runs. The address-space cap is skipped in sanitizer builds, which reserve huge virtual ranges.
- **Shutdown drain.** See section 4. Children are signalled individually from the PID table, never as a process group.
- **State is per process.** Counters or caches in memory are not shared between children. Anything shared (rate limits, solves, sessions later) lives in the database.

## 7. Connection layer

`http_connection_handle` is the only function that combines the socket, the parser, the router and the response writer.

- **Per-connection state** (`http_conn_state_t`): an 8 KiB input buffer that outlives each request, so bytes of a pipelined next request are never lost. A compile-time assertion keeps the buffer larger than the parser's line limit, so the parser always reaches a verdict before the buffer fills.
- **Deadlines.** `conn_set_deadline(c, seconds)` stores an absolute monotonic deadline in the connection. Every `conn_read` and `conn_write_all` sets its socket timeout to the time that remains. The handler uses a short **idle** deadline while waiting for the first byte of a request and the **request** deadline once bytes arrive. A client that sends one byte at a time is cut off at the request deadline whatever its pace.
- **Idle versus stalled.** A connection that times out before sending anything is closed silently (no response, no access-log line). A partial request that stalls gets `408`.
- **Keep-alive decision.** HTTP/1.1 stays open unless `Connection: close`, HTTP/1.0 closes unless `Connection: keep-alive` (the header is parsed as a token list). The connection also closes after a parse error, after a 5xx, after `max_requests_per_conn`, and during shutdown. A routed `404` or `405` keeps it open.
- **`Expect: 100-continue`** is answered once, when the headers are complete and a body is expected.
- **Writes.** The head is sent with `MSG_MORE` when a body follows, so the head and body leave as one train of packets and Nagle's algorithm cannot stall the second write.
- **Access log.** One line per request: time, pid, client address, request line, status, body bytes, duration in microseconds and `User-Agent`. All fields are cleaned (quotes, backslashes, control characters) and truncated. The client address is the peer address, or `CF-Connecting-IP` only when `trust_proxy` is on, the peer is loopback, and the value is a valid IP address.

## 8. HTTP layer

### Parser (`http/parser.c`)

An incremental state machine:

```text
 PARSE_REQ_LINE ──valid line──► PARSE_HEADERS ──blank line──► PARSE_BODY ──all bytes──► PARSE_DONE
        │                            │                │
        └─────── any violation ──────┴────────────────┴──────────────────────────────► PARSE_ERROR
                                   (status code stored in req->error_status)
```

`http_parse(req, buf, len)` consumes only **complete** lines (or body bytes) and returns how many bytes it used. The caller keeps the rest. The parser therefore holds no partial-line state of its own, and feeding it one byte at a time gives exactly the same result as feeding it everything at once. That property is what lets it run unchanged under an event loop, and it is checked by the fuzzer.

Every field has a fixed size, and every length is checked before any copy. The full list of rules (token header names, `Content-Length` handling, duplicate and conflicting headers, `Transfer-Encoding`, `Host` requirement) is in [security.md](security.md#41-parsing-and-request-smuggling).

The request object owns its data: fixed arrays for path, query and headers, and a heap buffer for the body (freed by `http_request_free`). It is about 31 KB, so the handler allocates it on the heap.

### Router (`http/router.c`)

A fixed table (64 routes) filled at startup and read-only afterwards. Matching rules, in order:

1. An exact path and method match wins over any prefix match. Among prefixes, the longest wins.
2. If the path matches a route but the method does not, the answer is `405` with an `Allow` header built from a bitmask of the methods that do match.
3. Unmatched `GET` and `HEAD` requests go to the fallback handler (the static file handler).
4. Anything else is `404`.

`HEAD` is routed as `GET`, and the connection handler drops the body when sending. Routes carry a flags field (`ROUTE_BLOCKING`) that does nothing under `fork` and `thread`. The epoll model will use it to decide which handlers must run on a worker pool.

### Response (`http/response.c`)

Handlers fill an `http_response_t` (status, up to 16 headers, an owned body). They never write to a socket. The serializer builds the status line and head, computes `Content-Length`, and generates `Date`, `Server` and `Connection` itself. Those four headers are reserved, so a handler cannot send a wrong one. Header names and values containing CR or LF are rejected, which prevents response splitting.

### URL helpers, MIME, status

`http_url_decode` percent-decodes and rejects malformed escapes and encoded NUL. `http_query_get` extracts one decoded value from a query string. `http_mime_from_path` maps an extension to a type (unknown extensions are `application/octet-stream`). `status_reason` maps codes to reason phrases.

## 9. Application layer

### Static files (`app/static_files.c`)

The fallback handler. The pipeline (decode, reject `/.`, resolve with `realpath`, require the result inside the resolved document root, `open` then `fstat`, regular files only, size cap) is described step by step in [security.md](security.md#42-static-files-and-path-traversal). Every rejection is a plain `404`.

### API handlers (`app/*_api.c`)

Thin: validate the input, call a model function, translate its result code into a status, build JSON with the JSON writer. `api_util.c` holds the shared pieces (sending JSON, error bodies, query integers, slug extraction from the path). The endpoints are documented in [api.md](api.md).

### Routes (`app/routes.c`)

The one place that lists the application's routes and the fallback. Database-backed routes are registered with `ROUTE_BLOCKING`.

## 10. Data layer

### Connections

SQLite is vendored and compiled with its own flags (`SQLITE_THREADSAFE=1`, `SQLITE_DQS=0`, foreign keys on by default, extension loading omitted). The model of use:

- **One connection per thread, opened lazily.** `db_get()` returns the calling thread's connection and opens it on first use. A request that never touches the database never opens one. Under `fork`, each child opens its own the first time a handler needs it.
- **Never across a `fork()`.** `db_init` opens a connection only to run migrations and closes it before returning. A `pthread_atfork` child handler also clears the thread-local pointer, so a child can never use a connection inherited from its parent.
- **Settings per connection:** WAL journal mode (readers do not block the writer), `synchronous=NORMAL`, foreign keys on, `trusted_schema` off, and a busy timeout from the config.
- **The database file is created owner-only** (`umask 077`, directory `0700`).

### Migrations (`db/migrate.c`)

Numbered, forward-only SQL files in `migrations/` (`NNN_name.sql`). The schema version lives in `PRAGMA user_version`. Each file runs in its own transaction together with the version bump, so a failure leaves neither half a schema change nor a new version number. The runner refuses duplicate numbers and refuses to start an older server against a newer database. Details and the workflow are in [schema.md](schema.md).

### Models (`db/models/*.c`)

The only code that contains SQL. Rules every function follows:

- **Prepared statements with bound parameters only.** No value is ever pasted into SQL text.
- **Plain structs out.** Strings are copied out of SQLite before the statement is finalized (column pointers die at the next step). Large text (descriptions, writeup bodies) is returned as a `malloc`'d buffer with a matching free function.
- **No secrets in return types.** Flag salts and hashes are read and used only inside `challenge_check_flag`.
- **Result codes, not errno:** `DB_OK`, `DB_NOT_FOUND`, `DB_CONFLICT`, `DB_RATE_LIMITED`, `DB_INVALID`, `DB_ERROR`. A busy database surfaces as `DB_ERROR`, which the API turns into `503`.

### `tourneyctl`

The authoring tool is a second `main()` over the same library. It calls the same `challenge_create` and `writeup_upsert` the tests exercise, so there is one implementation of every write. It validates sizes and UTF-8 before calling the models, and the schema's `CHECK` constraints validate again.

## 11. Frontend

Plain HTML, CSS and JavaScript in `public/`, served by the static handler:

- **Pages:** `index.html`, `challenges.html`, `challenge.html?c=<slug>`, `writeups.html`, `writeup.html?w=<slug>`.
- **Data flow:** each page loads `common.js` plus its own script, which calls the JSON API with `fetch` and builds the page with DOM APIs.
- **Rules that follow from the server's CSP** (`default-src 'self'` and friends): no inline scripts, no inline event handlers, no `<style>` blocks, no `style=` attributes, and no `innerHTML`. Server-supplied text goes in through `textContent`. The only links built from data are same-origin `/files/...` paths matching a strict pattern and a validated `http(s)` source URL.
- **No accounts yet:** "solved" marks live in the browser's `localStorage` only.

## 12. Conventions

- **Ownership.** The function that allocates a request, response, buffer or JSON object frees it, on every exit path (a single cleanup label). Models copy out of SQLite. Handlers never own sockets.
- **Errors.** Library functions return `0`/`-1` or a domain result code, and log the cause once, where it happens.
- **No hidden global mutable state.** Globals are written only during startup (config, route table, options). Anything else lives in a struct passed through or in thread-local storage. This is written for the thread and event-loop models, even though `fork` would forgive it.
- **Reentrant libc.** `gmtime_r`, `strtok_r`-style functions, `getaddrinfo`, and logging that uses a local buffer.
- **Logging.** One `write()` per line to stderr (diagnostics) or stdout (access log). The logger saves and restores `errno` and removes control characters.
- **Signals.** Handlers only set flags or do async-signal-safe bookkeeping. Nothing logs from a handler.
- **Naming.** Module prefixes (`http_`, `conn_`, `challenge_`), `_t` for types, slugs `[a-z0-9-]{1,64}` for public identifiers.

## 13. Design decisions

| Decision | Reason | Cost or alternative |
|---|---|---|
| One concurrency interface, selected at compile time | The same application runs under every model, so benchmarks compare the models and nothing else. | A `#ifdef`-free build, but each model is a full replacement of one file. |
| The accept loop belongs to the concurrency layer | An event loop must own the listening socket. | `main` stays tiny. |
| Incremental parser, caller owns the buffer | Works under blocking reads and under an event loop, and is trivial to fuzz. | Rescans a partial line on each call (negligible at these sizes). |
| Handlers fill a response object and never touch sockets | Handlers can move to a worker thread, and responses can be sent in pieces. | The whole body is buffered in memory first (8 MiB static cap). |
| Route table built at startup, read-only afterwards | Lock-free under every model. | No dynamic routes. |
| Absolute per-connection deadlines in `net/conn.c` | Slowloris protection that works in every model. `alarm()` is process-wide and would kill the serial server. | One extra `setsockopt` per I/O call. |
| Option structs per module instead of passing `config_t` | Modules stay testable and independent of `core`. | A little translation code in `server_init`. |
| SQLite vendored and compiled in | The runtime has no database dependency and the version is pinned. | A 9 MB source file in the tree. |
| Thread-local, lazily opened connection, closed before `fork()` | The same code is correct under fork, threads and worker pools, and static requests never pay for a connection. | One open per child under fork. |
| Models are the only SQL, results are plain structs | Secrets cannot leak by accident, and handlers stay storage-agnostic. | Some copying. |
| Flags stored as salted SHA-256, compared in constant time | A leaked database does not reveal flags, and the salt defeats precomputed tables. | A fast hash is fine for long random flags, but not for passwords (those will use PBKDF2). |
| Content authored with `tourneyctl`, not over HTTP | With no authentication, a write endpoint would let anyone change the data. | Content changes need shell access. |
| Rate limiting kept in the database | State shared across forked children cannot live in memory. | The limit is per challenge and global, so a spammer can briefly lock one out. |
| Forward-only numbered migrations in one transaction each | Simple, predictable, and a failed upgrade leaves the database as it was. | No automatic downgrades. |
| Events present in the schema, one fixed event in the API | The data model will not need rework, but no feature is built before it is needed. | The API assumes the `practice` event for now. |
| Strict CSP and DOM-only rendering in the frontend | A stored-XSS bug needs two separate mistakes. | No inline code at all, even for convenience. |
| One trunk with milestone tags | Shared code is fixed once, and models are additive files. | Branch discipline instead of long-lived forks. |

## 14. What changes for threads, epoll and TLS

| Part | Threads (`v1.1`) | Epoll (`v1.2`) | TLS (`v2.0`) |
|---|---|---|---|
| Parser | no change | no change | no change |
| Router and route table | no change (read-only) | no change | no change |
| Handlers and models | no change | no change, but `ROUTE_BLOCKING` handlers run on a worker pool | no change |
| `net/conn.c` | no change | non-blocking I/O, partial writes | `SSL_read`/`SSL_write` inside the same functions |
| Concurrency layer | thread pool plus a connection queue | an event loop that owns the listener | handshake becomes a connection state |
| Connection handler | no change | a non-blocking variant that reuses the keep-alive, parsing and logging helpers | no change |
| Database | one connection per thread (already the model) | a few worker threads with their own connections, results handed back through an `eventfd` | no change |
| Deadlines | same mechanism | timers instead of socket timeouts | same |
| Signals and shutdown | the flag and `pthread_sigmask` | `signalfd` or `eventfd` | no change |
| Body and file buffering | no change | incremental body buffers, partial file sends | no `sendfile` (it bypasses TLS) |
| Process-wide settings | per-process rlimits become process-wide | one process holds every connection, so descriptor limits matter | no change |

The habits that make this cheap are already in the code: no handler touches a socket, no module has mutable globals after startup, the parser never reads, and everything shared lives in the database.

## 15. Cookbook

**Add an API route.** Write a handler `void h(const http_request_t *, http_response_t *)` in `app/`, declare it in a header, register it in `routes.c` (with `ROUTE_BLOCKING` if it touches the database), and document it in `api.md`. Validate input before calling a model, and map result codes to statuses.

**Add a model function.** Put it in `db/models/`. Use `model_prepare`, bind every value, copy results out before finalizing, return a `DB_*` code, and never put secrets in the returned struct. Add a case to the model's unit test, including a hostile-input case.

**Change the schema.** Add `migrations/NNN_name.sql` with the next number. Never edit an applied migration. Rebuild a table (create, copy, drop, rename) when SQLite's `ALTER TABLE` cannot do the change. Update [schema.md](schema.md) and the migration test.

**Add a config key.** Add the field to `config_t`, the default, `config_set` (with its range), validation if it depends on a compile-time cap, and `config/server.conf.example`. Pass it to the module that needs it through that module's option struct, not by including `config.h` there.

**Add a concurrency model.** Add `src/concurrency/<name>_model.c` implementing `concurrency_run` and `concurrency_shutdown`, and run `make CONCURRENCY=<name>`. Run the full test suite against it before anything else.
