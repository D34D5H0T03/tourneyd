# tourneyd

`tourneyd` is an HTTP/1.1 web server and a small Capture The Flag (CTF) platform, written from scratch in C (GNU C17) on raw POSIX sockets. It serves static pages, a JSON API for CTF challenges and writeups, and stores everything in an embedded SQLite database.

> **Disclaimer:** this is an **educational systems-programming project**, not a commercial product. It exists to explore operating-system primitives, network protocol framing, memory safety and server concurrency models without web frameworks or third-party networking libraries.

**Current version: `v1.0`.** A multi-process (`fork`) server, a SQLite-backed platform with challenges, flag verification and writeups, a public API, and a vanilla-JavaScript frontend. Threads, `epoll` and TLS are the next milestones (see the [roadmap](#roadmap)).

## Dependencies

| Part | Dependency |
|---|---|
| HTTP stack, networking, concurrency, routing, logging | None. POSIX and the C library. |
| Database | **SQLite**, vendored as a single source file in `third_party/sqlite/` and compiled into the binary. |
| Flag hashing (and TLS later) | **OpenSSL** libcrypto at build time (`libssl-dev` on Ubuntu). |
| Frontend | None. Plain HTML, CSS and JavaScript, no framework and no build step. |

## What it does

- **Web server:** HTTP/1.0 and HTTP/1.1, persistent connections with pipelining, `100-continue`, `HEAD`, `405` responses with `Allow`, dual-stack IPv4/IPv6.
- **Concurrency:** a process per connection (`fork`) with a worker cap, plus a single-process `serial` model. The model is chosen at build time behind one interface.
- **Hardened by design:** an incremental, strict request parser, hard limits on every field, per-connection deadlines against slow clients, per-child resource limits, graceful shutdown, a path-traversal-proof static file handler, and default security headers (including a strict CSP).
- **CTF platform:** challenges with salted flag hashes, a rate-limited flag-submission endpoint, hosted or linked writeups, and challenge attachments.
- **Content tooling:** `tourneyctl`, a command-line tool that shares the server's model layer, and `scripts/seed-dev.sh`, which generates real self-checking challenges.
- **Quality gates:** unit tests, raw-socket attack suites, limits and shutdown tests, API and frontend tests, a parser fuzzer, and recorded benchmarks.

## Inspiration

The project began as a practical implementation of the networking foundations in ***Computer Networking: A Top-Down Approach*** by James F. Kurose and Keith W. Ross. Moving from the textbook's diagrams to working C meant translating transport- and application-layer ideas into Linux system calls:

* **Transport layer (TCP byte streams):** TCP has no message boundaries. The code deals with partial `read()` and `write()` calls, arbitrary fragmentation, and signal interruptions (`EINTR`).
* **Application layer (HTTP):** `\r\n` framing, a state-machine parser, header validation, status code semantics, MIME types and connection lifecycles, all over raw sockets.

## Quick start

```bash
# 1. Build requirements (Ubuntu)
sudo apt install build-essential libssl-dev xxd curl netcat-openbsd

# 2. Build the server and the admin tool in one command
make

# 3. Fill a development database with sample challenges and writeups
scripts/seed-dev.sh

# 4. Run
./build/tourneyd            # listens on 127.0.0.1:8080, serves ./public
```

Open <http://127.0.0.1:8080>. The seed script includes a `test-flag` challenge whose flag is printed in its description (`flag{this_is_only_a_test}`), so you can try the whole submission flow immediately.

```bash
curl -s http://127.0.0.1:8080/api/challenges
curl -s -X POST --data 'flag{this_is_only_a_test}' http://127.0.0.1:8080/api/challenges/test-flag/submit
```

Add your own content with the admin tool (flags are read from a file or stdin, never from arguments):

```bash
printf '%s' 'flag{my-secret}' | ./build/tourneyctl challenge add \
    --slug my-first --title "My First" --category misc --points 100 \
    --desc-file desc.txt --flag-stdin
./build/tourneyctl writeup add --slug my-notes --title "My notes" --file notes.md
```

More in [docs/operations.md](docs/operations.md).

## Configuration

Settings come from built-in defaults, then `config/server.conf` (if it exists, or the file given with `-c`), then command-line flags, in that order.

```bash
./build/tourneyd -c config/server.conf -p 9090 -b 127.0.0.1 -r public
```

Unknown keys and out-of-range values stop startup with a message that names the line. The full key reference is in [docs/operations.md](docs/operations.md#configuration-reference).

## Repository layout

```text
tourneyd/
├── include/                # public headers, mirroring src/
├── src/
│   ├── main.c              # entry point: config, init, run, shutdown
│   ├── core/               # config, signals, server lifecycle
│   ├── net/                # listener and connection primitives (deadlines, EINTR-safe I/O)
│   ├── concurrency/        # accept loops: fork_model.c, serial_model.c
│   ├── connections/        # http_connection.c: keep-alive loop, access log
│   ├── http/               # parser, router, response, url, mime, status
│   ├── app/                # route registration, static files, API handlers
│   ├── db/                 # connection layer, migrations, models
│   └── util/               # log, clock, shutdown flag, hash, json, slug, utf8
├── tools/                  # tourneyctl (admin CLI)
├── scripts/                # seed-dev.sh, share.sh (quick tunnel), unit-test runner
├── migrations/             # numbered, forward-only SQL migrations
├── public/                 # the frontend (static files) and generated attachments
├── third_party/sqlite/     # vendored SQLite amalgamation
├── tests/                  # unit, integration, fuzz
├── docs/                   # documentation (see below)
├── config/                 # server.conf example
└── Makefile
```

## How a request flows

1. The **concurrency layer** owns the accept loop. For each client, the `fork` model starts a child process (up to `max_children` at once); the `serial` model handles clients one at a time.
2. The **connection handler** (`http_connection_handle`) runs a keep-alive loop. Each request gets a deadline: a short idle wait for the first byte, then a request deadline for the rest.
3. The **incremental parser** consumes bytes as they arrive and never blocks. Complete lines go into a request object, and leftovers stay in the connection buffer for pipelined requests.
4. The **router** maps `(method, path)` to a handler (exact routes beat prefixes, `HEAD` is routed as `GET`), or produces `404`/`405`.
5. A **handler** fills a response object. It never touches the socket. API handlers call the **model layer**, the only code that contains SQL.
6. The connection handler adds the security headers, sends the head and body, writes one access-log line, and decides whether to keep the connection.

The full picture, with diagrams and design decisions, is in [docs/architecture.md](docs/architecture.md).

## Concurrency models

The model is selected at compile time and implements one interface (`concurrency_run`, `concurrency_shutdown`), so nothing above it changes:

```bash
make                         # fork (default)
make clean && make CONCURRENCY=serial
```

| Model | Status | Notes |
|---|---|---|
| `serial` | done | One client at a time. Useful for debugging and Valgrind. |
| `fork` | done | A process per connection, capped, with signal-based reaping and a drain on shutdown. |
| `thread` | planned (`v1.1`) | pthreads with a thread pool. |
| `epoll` | planned (`v1.2`) | Non-blocking event loop, with a worker pool for blocking database handlers. |

Measured results for the fork model are in [docs/benchmarks.md](docs/benchmarks.md).

## Testing

```bash
make test               # unit tests, then the integration suites
make test SAN=1         # the same under AddressSanitizer and UBSan
make fuzz               # parser fuzzer (gcc driver); `make fuzz-clang` for libFuzzer
```

What each suite checks, and how to run one suite alone, is in [docs/testing.md](docs/testing.md).

## Documentation

| Document | Contents |
|---|---|
| [docs/architecture.md](docs/architecture.md) | Layers, request lifecycle, concurrency, data layer, design decisions, what changes for threads and epoll |
| [docs/security.md](docs/security.md) | Threat model, defenses, limits, verification results, known limitations |
| [docs/api.md](docs/api.md) | The public JSON API |
| [docs/schema.md](docs/schema.md) | Database schema, conventions, migrations, how users and events will be added |
| [docs/operations.md](docs/operations.md) | Configuration reference, `tourneyctl`, seeding, running, deploying behind a tunnel, backups |
| [docs/testing.md](docs/testing.md) | Test inventory, how to run and extend it, fuzzing, manual checks |
| [docs/benchmarks.md](docs/benchmarks.md) | Methodology and measured results |
| [docs/STATUS.md](docs/STATUS.md) | Where the project stands, next steps, and a restart checklist |

## Roadmap

- [x] **Phase 1: networking, HTTP/1.1 core and fork concurrency** (`v0.5`): dual-stack listener, deadlines, keep-alive, incremental parser, router, static files, graceful shutdown, resource limits, access log, attack suites, fuzzing.
- [x] **Phase 2: database, API and frontend** (`v1.0`): vendored SQLite, migrations, challenge and writeup models, flag verification with rate limiting, public API, vanilla frontend, `tourneyctl`, seed script.
- [ ] **Phase 3: lab endpoints and threads** (`v1.1`): experiments that behave differently per model (shared counter, slow endpoint, large bodies), then a pthread model with a thread pool.
- [ ] **Phase 4: epoll** (`v1.2`): a non-blocking connection state machine, timers, partial writes, and a worker pool for blocking database handlers.
- [ ] **Phase 5: TLS** (`v2.0`): OpenSSL, blocking first on the fork model, then non-blocking inside the epoll loop.
- [ ] **Later:** accounts and sessions, multiple events with time windows, CP and hackathon event kinds.

## Development workflow

One trunk (`main`) with short-lived feature branches merged with `--no-ff`, and annotated tags for milestones (`v0.1` ... `v0.5`, `v1.0`). The concurrency models are additive files behind one interface, selected by `make CONCURRENCY=...`, so every model runs the same application code.
