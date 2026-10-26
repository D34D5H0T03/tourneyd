# Project Status

**Version:** `v1.0`
**Last updated:** 2026-10-26

A note to future me. Read this first when coming back to the project after a break, then follow the restart checklist at the bottom.

## Where things stand

**Finished and tagged:**

| Milestone | Contents |
|---|---|
| `v0.1` to `v0.5` | Networking, concurrency interface, fork model, incremental parser, router, static files, keep-alive, graceful shutdown, access log, security headers, resource limits, attack suites, parser fuzzer, baseline benchmarks |
| `v1.0` | Vendored SQLite and migrations, challenge and writeup models, flag verification with rate limiting, public JSON API, vanilla frontend, `tourneyctl`, development seed script, full documentation set |

**What works:** a hardened fork-per-connection HTTP/1.1 server serving a small CTF platform. Challenges have salted flag hashes and attachments, flags can be submitted through the API and the website, writeups can be hosted or linked, and content is managed offline with `tourneyctl`.

**What does not exist yet:** threads, epoll, TLS, accounts, multiple-event support in the API, lab endpoints (experiments for comparing concurrency models), Markdown rendering, a scoreboard.

## How to get going again

```bash
git switch main && git pull
make clean && make && make build/tourneyctl
make test SAN=1                    # should be green before anything else
scripts/seed-dev.sh && ./build/tourneyd      # then open http://127.0.0.1:8080
```

Documentation map: [Architecture.md](Architecture.md) (how it fits together), [Operations.md](Operations.md) (config, `tourneyctl`, deployment), [Testing.md](Testing.md), [Security.md](Security.md), [Schema.md](Schema.md), [Api.md](Api.md), [Benchmarks.md](Benchmarks.md).

## Next steps, in order

1. **Break and reading.** Computer Networking chapters 4 to 8 (chapter 8 matters most for TLS: re-read its TLS section right before the TLS phase). Hands-on pairings: `ip route`, `traceroute` and `tcpdump` for the network layer, `ip neigh` and Wireshark for the link layer, `openssl s_client` and Wireshark for the TLS handshake. Turn each pwn.college web lesson into a regression test against this server where it applies.
2. **Run the benchmark experiments** in [Benchmarks.md](Benchmarks.md): the hypotheses about the saturation test, and the pending database-backed endpoints. Write predictions first.
3. **Lab endpoints** (`/lab/whoami`, `/lab/counter`, `/lab/db-counter`, `/lab/slow`, `/lab/big`, `/lab/upload`), registered only when `lab_enabled` is on. They are experiments whose results differ by concurrency model.
4. **Thread model** (`v1.1`): thread per connection, then a pool with a work queue. Per-thread database connections (already how the data layer works). ThreadSanitizer on the shared-counter experiment. Replace `sigprocmask` with `pthread_sigmask` and the child-cap logic with a thread cap.
5. **Epoll** (`v1.2`): non-blocking sockets, per-connection state, partial writes, timers instead of socket timeouts, then a worker pool plus `eventfd` for `ROUTE_BLOCKING` handlers. Compare against the benchmark table.
6. **TLS** (`v2.0`): blocking OpenSSL on the fork model first, then non-blocking inside the epoll loop. Everything stays inside `net/conn.c` and the connection state.
7. **Later:** accounts and sessions, event selection and time windows, scoreboard, Markdown renderer built on DOM nodes.

## Decisions worth remembering

| Decision | Why |
|---|---|
| One trunk, concurrency models as additive files | Shared code is fixed once, and benchmarks compare only the model |
| Handlers never touch sockets; the parser never reads | Makes threads and epoll cheap to add |
| Absolute deadlines in `net/conn.c`, no `alarm()` | Works in every model |
| SQLite vendored, thread-local lazy connection, closed before `fork()` | Pinned version, correct under every model |
| Models are the only SQL, results are plain structs without secrets | Flag hashes cannot leak by accident |
| Content authored offline with `tourneyctl` | No authentication yet, so no write endpoints |
| Rate limit stored in the database | Memory is not shared across forked children |
| Strict CSP, DOM-only rendering | A single mistake should not be enough for XSS |

The full list with trade-offs is in [Architecture.md](Architecture.md#13-design-decisions).

## Known issues and rough edges

- The submission rate limit is global per challenge, so a spammer can briefly lock a challenge. Needs identity to fix properly.
- The response write budget is the whole `request_timeout_sec`. A slow but legitimate download of a large file can be cut off. A per-progress write timeout is the better design.
- Static files are read fully into memory (8 MiB cap), and request bodies are allocated when the headers finish. Both must become incremental for epoll.
- An unknown `GET /api/...` path returns the HTML `404` from the static handler, not JSON.
- The configuration cannot contain `#` inside a value.
- Logs are not rotated, and only the parser is fuzzed.
- The seed script contains every solution. Do not reuse its flags on a real site.

More in [Security.md](Security.md#6-known-limitations).

## Open questions

- Should the API select the event with `?event=<slug>` or with path segments when events are added?
- When accounts arrive, per-address limits through a proxy need a trustworthy client address. Is `CF-Connecting-IP` enough behind a tunnel, or does it need the proxy's address list?
- Is the benchmark collapse at 200 connections caused by worker pinning, idle timeouts or fork churn? (See the experiments in [Benchmarks.md](Benchmarks.md).)

## Restart checklist

- [ ] Read this file, then skim `Architecture.md` sections 2 and 5
- [ ] `git status` is clean, `main` is up to date, and `git tag` shows `v1.0`
- [ ] `make clean && make && make build/tourneyctl`
- [ ] `make test SAN=1` passes (record the commit in `Testing.md`)
- [ ] `make fuzz` runs clean
- [ ] Rerun the fork benchmarks and confirm they match `Benchmarks.md` on this machine (a different power profile or background load changes the numbers)
- [ ] Pick the next step above, create a branch for it, and write the first three lines of what you will do in this file before starting
- [ ] End every session by updating "Next steps" so the next restart is cheap

## Working agreement with myself

- About four hours of project coding a day at most, with a fixed stop time, and at least one full day a week with nothing project-related.
- If something does not work after an hour, write down what was tried and stop.
- Predict before measuring, break things on purpose, and write down what surprised me in `Lab-notes.md`.
