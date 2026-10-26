# Testing

**Applies to:** tourneyd `v1.0`
**Last updated:** 2026-10-26
**Last full run:** commit `<fill in>` on `<date>` (all suites passing)

Fill in the line above after every full run, and before every tag.

## Philosophy

- **Test the layer where the bug would live.** Parsing bugs are caught by unit tests and the fuzzer, protocol and abuse behavior by raw-socket integration tests, and application behavior by tests against a real running server.
- **Attack tests use raw sockets** (`/dev/tcp`), because `curl` normalizes `..`, rejects malformed requests and fixes headers before the server ever sees them.
- **Every suite starts its own server** on a spare port with short timeouts and an isolated document root and database, so a test never depends on a server you left running, and never touches your real data.
- **Every bug that was found becomes a test** (fuzzer findings go into the corpus and a unit test).
- **Sanitizers are part of the normal run**, not an extra.

## Running everything

```bash
make test               # unit tests, then every integration suite
make test SAN=1         # the same, built with AddressSanitizer and UndefinedBehaviorSanitizer
make fuzz               # parser fuzzer, 2,000,000 iterations (ITER=... to change)
make fuzz-clang SECS=600    # optional coverage-guided run (needs clang)
```

For `SAN=1`, rebuild the server too (`make clean && make SAN=1`), because the integration suites run `./build/tourneyd`. The per-child address-space limit is skipped automatically in sanitizer builds.

Run a single suite:

```bash
./build/tests/test_parser                     # one unit test binary
bash tests/integration/test_api.sh            # one integration suite (from the repo root)
```

Integration suites need `curl`, `nc`, `timeout`, `pgrep`, `ps` and bash 4.1 or newer. `test_api.sh` also needs `build/tourneyctl`. The suites use port `18080` by default (`PORT=...` to change).

## Inventory

### Unit tests (`tests/unit/`)

| File | Covers |
|---|---|
| `test_parser.c` | Request line, headers, body, limits, error statuses, byte-at-a-time feeding, query splitting, duplicate and conflicting headers, token and control-character rules |
| `test_response.c` | Serialization (exact bytes), header injection rejection, reserved headers, replacing `Content-Type`, header table limit, error pages |
| `test_router.c` | Exact and prefix precedence, `HEAD` as `GET`, `405` with `Allow`, fallback, `404`, route table limits, invalid prefixes |
| `test_config.c` | Defaults, file parsing, comments and whitespace, unknown keys, bad values, validation against compile-time caps, flag-over-file precedence |
| `test_log.c` | Message lengths on both sides of the buffer boundary, control-character removal, single-line output |
| `test_json.c` | Escaping (quotes, backslashes, control characters, invalid UTF-8), comma placement across nested objects and arrays, buffer growth |
| `test_url.c` | Percent decoding (valid, malformed, truncated, `%00`), query lookup (absent key, prefix keys, `+`, bad escapes, buffer limits) |
| `test_utf8.c`, `test_slug.c` | Valid and invalid sequences (overlong, surrogates, truncated), slug accept and reject cases |
| `test_hash.c` | A known SHA-256 vector, salts make hashes differ, constant-time equality |
| `test_db.c` | Migrations (apply, rerun, failing migration leaves no partial schema, duplicate numbers, newer database refused), constraints, injection attempt, per-thread connections, **fork safety** |
| `test_challenge_model.c`, `test_writeup_model.c` | Create, get, list, publish and hide, flag check, rate limit, conflicts, invalid input, pagination, links to events and challenges |

### Integration suites (`tests/integration/`)

| Suite | What it proves | Time |
|---|---|---|
| `test_attacks.sh` | Malformed requests, header abuse, framing and smuggling shapes, path traversal, response and log hygiene (details in [Security.md](Security.md#52-attack-suite-testsintegrationtest_attackssh)) | seconds |
| `test_limits.sh` | Connection cap, idle and request timeouts, slowloris, keep-alive rules, leaks and zombies, crash reporting, graceful shutdown (details in [Security.md](Security.md#53-limits-suite-testsintegrationtest_limitssh)) | about a minute |
| `test_keepalive.sh` | Connection reuse, pipelining, `Connection: close`, `100-continue`, request limit per connection | seconds |
| `test_api.sh` | Public API behavior, no secret leakage, input handling, rate limit, concurrent writers, writeup pagination and escaping | seconds |
| `test_frontend.sh` | CSP lint (no inline code, no HTML-string sinks, no missing files) and the real pages served with the right content types and headers | seconds |
| `run_all.sh` | Runs every `test_*.sh` and fails if any suite fails | |

`tests/integration/lib.sh` is the shared harness: it builds an isolated document root, writes a short-timeout config (`build/test.conf`), starts and stops the server, and provides `raw` (send exact bytes), `raw_status`, `pass`, `fail` and `assert_eq`.

### Fuzzing (`tests/fuzz/`)

| File | Purpose |
|---|---|
| `fuzz_parser.c` | The harness. Checks for memory errors, **split-invariance** (one call equals many small calls), and structural invariants, on every input |
| `fuzz_driver.c` | A small mutation fuzzer for machines without clang. `make fuzz` builds and runs it |
| `corpus/`, `parser.dict` | Seed requests and a token dictionary |
| `make_corpus.sh` | Regenerates the seeds |

When a run fails, the input is saved to `fuzz-crash.bin` (driver) or `build/crash-*` (libFuzzer). Replay it with `./build/fuzz_driver fuzz-crash.bin`, fix the parser, copy the input into `tests/fuzz/corpus/`, and add a unit test for it.

Fuzz targets worth adding next: the URL decoder, the query parser, the JSON writer, the header-token helper and the configuration loader. Each needs a small harness and a clear invariant (for example "output is always NUL-terminated within the buffer").

## Manual browser check

Run this after changing anything under `public/`, about ten minutes:

1. Seed a database (`scripts/seed-dev.sh`), start the server and open the site.
2. Open DevTools. **The console must show no `Refused to ...` lines** (those are CSP violations), and the Network tab must show only same-origin requests.
3. Solve `test-flag` through the form. A correct flag shows a green message and a check mark on the list, and survives a reload. A wrong flag shows the red message.
4. Download each attachment from a challenge page. Solve at least `layers` and `one-byte-key` as a player would.
5. Submit 31 wrong flags quickly and confirm the rate-limit message.
6. Open `/challenge.html?c=nope`, `?c=<script>` and no parameter. Each shows a polite "not found", and nothing executes.
7. Narrow the window to phone width, tab through the page with only the keyboard, and switch the operating system to dark mode.
8. Add a challenge titled `<img src=x onerror=alert(1)>` with `tourneyctl`. It must appear as literal text and no alert may fire. Delete it afterwards.

## Writing a new test

- **Unit test.** Add `tests/unit/test_<module>.c` with a `main` that returns non-zero on failure. `make test` picks up `test_*.c` files automatically and links them against the static library of all modules. Cover the normal case, the boundary on each side, and one hostile input.
- **Integration test.** Add `tests/integration/test_<topic>.sh` that sources `lib.sh`, builds its fixtures, starts a server, uses `pass`/`fail`/`assert_eq`, and ends with `finish`. Do not use `set -e`: a suite should run every check and report all failures.
- **Regression test.** For every bug, add the smallest test that fails without the fix, and mention the symptom in a comment.
- **Hostile-input rule.** A new endpoint is not done until its tests include a malformed slug, an oversized body and an input shaped like an injection.

## Benchmarks

Performance runs are not part of `make test`. Method and results are in [Benchmarks.md](Benchmarks.md).

## Known gaps

- No test yet for behavior under a full disk or a read-only database file.
- No test for the optional `?event=` parameter, because it does not exist yet.
- The frontend has a lint and a manual checklist but no automated browser test.
- Only the parser is fuzzed.
