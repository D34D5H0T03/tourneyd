# Performance Benchmarks

**Last updated:** 2026-10-26
**Server versions measured:** fork baseline measured at `v0.5` (static files only). DB-backed endpoints at `v1.0` are listed below as pending.

## Purpose

These numbers exist so that the thread and epoll versions can be compared against the fork model on **the same machine, the same application and the same commands**. They are a learning tool, not a claim about production performance: everything runs over the loopback interface on one laptop, with the load generator competing with the server for the same CPU cores.

## Environment

| Item | Value |
|---|---|
| OS | Ubuntu Linux (64-bit) |
| Hardware | 11th Gen Intel(R) Core(TM) i5-1135G7 @ 2.40GHz, 8 GB RAM |
| Network | Loopback interface (`127.0.0.1`) |
| Payload | 2,556 byte static HTML file (`/index.html`) |
| Build | Release build (no sanitizers), fork concurrency model |
| Load tools | `ab` (ApacheBench), `wrk` |

**Server configuration (`config/bench.conf`):**

| Key | Value |
|---|---|
| `max_children` | 64 |
| `idle_timeout_sec` | 5 |
| `max_requests_per_conn` | 100 |
| `trust_proxy` | off |
| Logging | access and error output routed to `/dev/null` |

## Method

1. Release build, no other heavy programs running, laptop on mains power with the same power profile every time.
2. Start the server with `config/bench.conf` and the access log going to `/dev/null`, so terminal or disk speed is not measured.
3. Warm up with a short run, then run each command below and record the output verbatim.
4. Write a **prediction before every new run**. The gap between the prediction and the result is the interesting part (see the log at the end).
5. Run each test at least three times when comparing models, and report the median.

For other models, keep everything identical except `make CONCURRENCY=...`.

## Baseline: fork concurrency model (`v0.5`, static file)

### Test A: standard load, under the worker cap

| | |
|---|---|
| Command | `ab -k -n 20000 -c 50 http://127.0.0.1:8080/index.html` |
| Requests per second (mean) | 115,703.91 |
| Transfer rate | 343.37 MB/s |
| Mean time per request (across concurrent requests) | 0.009 ms |
| 99th percentile latency | 1 ms |

### Test B: saturation, over the worker cap

| | |
|---|---|
| Command | `ab -k -n 20000 -c 200 http://127.0.0.1:8080/index.html` |
| Requests per second (mean) | 3,505.78 |
| Transfer rate | 10.37 MB/s |
| Mean time per request (across concurrent requests) | 0.285 ms |
| 99th percentile latency | 121 ms |
| Maximum latency | 5,631 ms (5.6 s) |

**Observation.** With 200 concurrent clients against a cap of 64 workers, throughput fell by about 97% compared with Test A, and the slowest request took 5.6 s.

**What is known.** At the cap the parent stops calling `accept()`. The 136 connections that do not have a worker wait in the kernel's listen backlog until a worker is free. Under the fork model one connection occupies one process for as long as the connection lives, so the number of connections that can make progress is capped by the number of processes.

**What is not yet known (hypotheses to test).**

- **H1, worker pinning:** a worker is held by a keep-alive connection until it reaches `max_requests_per_conn` (100) or closes, so waiting clients are blocked for that long.
- **H2, idle timeouts in the tail:** the maximum latency (5.6 s) is close to `idle_timeout_sec` (5 s), which suggests some workers wait for connections that went quiet, and a waiting client is served only after that timeout fires.
- **H3, accept and fork churn:** with connections closing every 100 requests, the parent spends noticeable time in `fork()` and signal handling.

**Experiments to separate them** (same machine, same command otherwise):

| Change | If it matters, expect |
|---|---|
| `idle_timeout_sec = 1`, then `10` | The maximum latency follows the timeout (H2) |
| `max_requests_per_conn = 10`, then `1000` | Throughput and latency shift with it (H1) |
| Drop `-k` from `ab` (one request per connection) | Fork churn dominates (H3) |
| `max_children = 256` | Fewer clients wait, and the memory cost per worker becomes visible |
| `strace -c -f -p <parent pid>` during the run | Time spent in `clone`, `accept`, `rt_sigsuspend` (H3) |

Record the results here when you run them. Until then, this section is a set of questions.

### Test C: sustained throughput (`wrk`)

| | |
|---|---|
| Command | `wrk -t2 -c100 -d15s http://127.0.0.1:8080/index.html` |
| Total requests processed | 2,207,347 |
| Requests per second | 146,172.35 |
| Transfer rate | 423.63 MB/s |
| Average latency | 3.77 ms |

### Working conclusion

For a small static file and moderate concurrency, the fork model is fast, because after the first request on a connection the work is a handful of system calls and the worker count is not the limit. The saturation test shows its structural limit: concurrency is bounded by the number of processes, each of which costs memory and a scheduler slot, and a connection that does nothing still occupies one. That is the motivation for the event-driven (`epoll`) version. This document treats it as a working conclusion until the experiments above and the epoll measurements either support or contradict it.

## Pending: database-backed endpoints (`v1.0`)

Static files do no database work. The API opens a SQLite connection per forked child on first use, so keep-alive matters much more here. Run these with a seeded database and record the results, writing a prediction first.

| Test | Command | Prediction | Result |
|---|---|---|---|
| API list, 50 concurrent | `ab -k -n 20000 -c 50 http://127.0.0.1:8080/api/challenges` | | |
| API writeups, 50 concurrent | `ab -k -n 20000 -c 50 http://127.0.0.1:8080/api/writeups` | | |
| API list, saturated | `ab -k -n 20000 -c 200 http://127.0.0.1:8080/api/challenges` | | |
| API list without keep-alive | `ab -n 5000 -c 50 http://127.0.0.1:8080/api/challenges` | | |
| Static file, same run, for comparison | `ab -k -n 20000 -c 50 http://127.0.0.1:8080/index.html` | | |

Flag submission is a write and is rate limited (30 attempts per challenge per minute), so it is **not** benchmarked directly. The writer-contention behavior is covered by the API integration test instead.

## Comparison plan for later versions


| Scenario | fork | thread | epoll |
|---|---|---|---|
| Static file, 50 connections (Test A) | 115,704 req/s | | |
| Static file, 200 connections (Test B) | 3,506 req/s | | |
| Sustained, 100 connections (Test C) | 146,172 req/s | | |
| API list, 50 connections | pending | | |
| 10,000 idle keep-alive connections: memory (RSS) and still serving a new request? | | | |
| One slow endpoint mixed with fast static requests (lab endpoint): latency of the fast ones | | | |
| Large download to a slow client, 100 clients: memory and throughput | | | |

Also record **memory** (the sum of the resident size of the parent and all children for fork, the single process otherwise) next to throughput. Memory per connection is where the models differ most.

## Prediction log

| Date | Experiment | Predicted | Measured | What I learned |
|---|---|---|---|---|
| | | | | |

## Caveats

- Loopback has no real network latency, loss or bandwidth limits, and the load generator shares the CPU with the server.
- `ab` is single-threaded and can become the bottleneck at high rates. `wrk` is better for sustained throughput.
- One machine, one run per row so far. Treat differences under about 5 to 10 percent as noise until repeated.
- The first run after a build can be slower (cold caches).
- Access logging was disabled. With logging on, each request adds a formatted line and a `write()`, so real throughput is lower.
