# Operations Guide

**Applies to:** tourneyd `v1.0`
**Last updated:** 2026-10-26

How to build, configure, run, feed with content, deploy, back up and troubleshoot tourneyd.

## Contents

- [Operations Guide](#operations-guide)
  - [Contents](#contents)
  - [Requirements and building](#requirements-and-building)
  - [Configuration reference](#configuration-reference)
  - [Running](#running)
  - [Content with `tourneyctl`](#content-with-tourneyctl)
  - [The seed script](#the-seed-script)
  - [Deploying behind a Cloudflare quick tunnel](#deploying-behind-a-cloudflare-quick-tunnel)
  - [Data, backups and migrations](#data-backups-and-migrations)
  - [Logs](#logs)
  - [Troubleshooting](#troubleshooting)

## Requirements and building

| Need | For |
|---|---|
| `gcc` (GNU C17), `make` | Building |
| OpenSSL headers (`libssl-dev`) | Flag hashing now, TLS later |
| `curl`, `nc` (netcat-openbsd), `timeout`, `pgrep`, `ps` | Integration tests |
| `xxd`, `factor`, `base64`, `od`, `rev`, `date` | The seed script (all but `xxd` come with coreutils) |
| `sqlite3` command-line shell (optional) | Inspecting the database |
| `ab` (apache2-utils) and `wrk` (optional) | Benchmarks |
| `clang` (optional) | The coverage-guided fuzzer |

```bash
sudo apt install build-essential libssl-dev xxd curl netcat-openbsd
make                              # build/tourneyd (fork model)
make build/tourneyctl             # the admin tool
make clean && make CONCURRENCY=serial     # single-process model
make SAN=1                        # AddressSanitizer + UBSan build (for testing, not deployment)
```

The build compiles `third_party/sqlite/sqlite3.c` once into `build/obj/sqlite3.o` with its own flags, which takes a while the first time.

## Configuration reference

Sources, in increasing priority: **built-in defaults, then a config file, then command-line flags.**

- The file is `config/server.conf` if it exists, or the one named with `-c`. A missing default file is fine. A missing file named with `-c` is an error.
- Format: one `key = value` per line, `#` starts a comment (so values cannot contain `#`), blank lines are ignored.
- **Unknown keys and out-of-range values stop startup** with a message that names the line. A typo is never silently ignored.
- Flags: `-c file`, `-p port`, `-b address`, `-r docroot`, `-h`.

| Key | Default | Rule | Meaning |
|---|---|---|---|
| `port` | `8080` | 1 to 65535 | TCP port |
| `bind_addr` | `127.0.0.1` | address string | Interface to listen on. Use `0.0.0.0` only when you really want direct exposure. |
| `doc_root` | `public` | existing directory | Where static files and the frontend live |
| `backlog` | `128` | 1 to 4096 | Kernel queue of completed connections waiting for `accept()` |
| `max_children` | `64` | 1 to 10000 | Maximum concurrent connections under the fork model |
| `idle_timeout_sec` | `10` | 1 to 3600 | How long a keep-alive connection may wait for the next request |
| `request_timeout_sec` | `15` | 1 to 3600 | Time allowed to receive one request, and the budget for sending its response |
| `max_requests_per_conn` | `100` | 1 to 100000 | Requests served on one connection before it is closed |
| `max_body_size` | `1048576` | at most the compile-time cap (1 MiB) | Largest accepted request body |
| `max_header_bytes` | `16384` | within what the header table can hold | Largest total size of the request line and headers |
| `shutdown_grace_sec` | `10` | 0 to 600 | How long running connections get to finish after `SIGTERM` or Ctrl-C |
| `trust_proxy` | `off` | `on`/`off`/`true`/`false`/`1`/`0` | Believe `CF-Connecting-IP` in the access log, but only from a loopback peer |
| `db_path` | `data/tourneyd.db` | path | The SQLite file. The directory is created if missing. |
| `db_busy_timeout_ms` | `5000` | 0 to 60000 | How long a database writer waits for the lock |
| `migrations_dir` | `migrations` | directory | Where the SQL migration files are read from |
| `lab_enabled` | `off` | boolean | Reserved for the lab endpoints of a later version. It has no effect in `v1.0`. |

Example `config/server.conf` (it is gitignored; commit only `config/server.conf.example`):

```ini
port = 8080
bind_addr = 127.0.0.1
doc_root = public
max_children = 64
trust_proxy = off        # on only when running behind cloudflared
db_path = data/tourneyd.db
```

Settings are read once at startup. Changing the file requires a restart.

## Running

```bash
./build/tourneyd                          # defaults
./build/tourneyd -c config/server.conf    # with a config file
./build/tourneyd -p 9090 -r public        # flags override the file
```

The database is created and migrated on the first start. Stop the server with Ctrl-C or `kill -TERM <pid>`: it stops accepting, lets running requests finish (up to `shutdown_grace_sec`), and exits with status 0. A restart right afterwards works, because the listening socket is created with `SO_REUSEADDR`.

Typical development setup: access log and errors in separate files.

```bash
./build/tourneyd > build/access.log 2> build/error.log
```

## Content with `tourneyctl`

`tourneyctl` opens the same database as the server (use the same `-c` file), applies pending migrations, and then runs one command. It can run while the server is running.

```text
tourneyctl [-c config] migrate
tourneyctl [-c config] challenge add   --slug S --title T --desc-file F (--flag-file F | --flag-stdin)
                                       [--event E] [--category C] [--points N]
tourneyctl [-c config] challenge list  [--event E]
tourneyctl [-c config] challenge hide|show|delete --slug S [--event E] [--yes]
tourneyctl [-c config] writeup add|put --slug S --title T --file F.md [--author A]
                                       [--event E] [--challenge C] [--source-url U]
tourneyctl [-c config] writeup list
tourneyctl [-c config] writeup hide|show|delete --slug S [--yes]
```

| Rule | Detail |
|---|---|
| Defaults | `--event practice`, `--category misc`, `--points 100` |
| Flags | Read from a file or from standard input, one line, 1 to 256 characters. **Never accepted as an argument**, because arguments land in shell history and `ps` output. |
| `add` versus `put` | `writeup add` fails if the slug exists. `writeup put` replaces it (keeping its creation date). Challenges have no `put`: to change one, delete it and add it again. |
| Hiding | `hide` unpublishes (it vanishes from the API and answers `404`), and `show` publishes again. Nothing is lost. |
| Deleting | Needs `--yes`. Deleting a challenge also deletes its submissions, and linked writeups stay but lose the link. |
| Validation | Slug format, sizes in bytes, valid UTF-8 and no NUL bytes in every text file, and `--source-url` must be `http://` or `https://`. Titles are limited to 200 bytes, authors to 100, descriptions to 64 KiB, writeup bodies to 256 KiB. |
| Exit status | 0 success, 1 runtime or database error, 2 usage error (nothing was touched) |

Examples:

```bash
# A challenge, with the flag from a file you delete afterwards
printf '%s' 'flag{found-it}' > /tmp/flag && chmod 600 /tmp/flag
./build/tourneyctl challenge add --slug first-steps --title "First Steps" \
    --category web --points 50 --desc-file desc.txt --flag-file /tmp/flag
shred -u /tmp/flag

# The flag typed without echo
read -rs FLAG; printf '%s' "$FLAG" | ./build/tourneyctl challenge add ... --flag-stdin; unset FLAG

# Attachments: put files under public/files/<slug>/ and mention the path in the description
mkdir -p public/files/first-steps && cp dump.pcap.txt public/files/first-steps/
#   description text:  Attachment: /files/first-steps/dump.pcap.txt

# A hosted writeup linked to a challenge, and a link-out entry
./build/tourneyctl writeup add --slug first-steps-solution --title "First Steps" \
    --author me --challenge first-steps --file solution.md
./build/tourneyctl writeup put --slug elsewhere --title "Written elsewhere" \
    --source-url https://example.org/post --file summary.md
```

Descriptions and writeups are plain text (Markdown is stored but shown as preformatted text). A path of the form `/files/<slug>/<file>` becomes a download link on the challenge page.

## The seed script

`scripts/seed-dev.sh` fills a development database with five generated challenges (`layers`, `one-byte-key`, `tiny-rsa`, `suspicious-logs`, `check-me`), a test challenge, and three sample writeups.

```bash
scripts/seed-dev.sh                       # default config
scripts/seed-dev.sh -c build/test.conf    # a specific config (and so a specific database)
scripts/seed-dev.sh --no-test             # without the test challenge, and removes it if present
```

- Attachments are generated from the flags into `public/files/<slug>/` (gitignored), and each challenge has a built-in solver that must recover the flag from the generated files, or the script stops (`self-check ok` lines).
- It is idempotent: rerunning replaces the same challenges and writeups (and deletes their submissions).
- **It contains every solution.** It is for development. Use `tourneyctl` with your real flags for a live site, and keep the script out of any public repository if you publish the challenges.
- `test-flag` has its flag in its description (`flag{this_is_only_a_test}`) so the submission pipeline can be tested end to end. Remove it for a real deployment.

## Deploying behind a Cloudflare quick tunnel

A quick tunnel exposes a local server on a temporary public HTTPS address. It needs no account and no open port. The address changes whenever `cloudflared` restarts, so it suits demos, not a permanent site.

1. Install `cloudflared` from Cloudflare's apt repository (see Cloudflare's documentation for the current commands).
2. Set `trust_proxy = on` in `config/server.conf` so the access log shows real client addresses, and keep `bind_addr = 127.0.0.1`.
3. Run `scripts/share.sh` (it starts the server, waits until the port answers, starts the tunnel, prints the public URL, and stops both on Ctrl-C), or by hand:

```bash
./build/tourneyd > build/access.log 2> build/error.log &
cloudflared tunnel --url http://127.0.0.1:8080
```

Use `127.0.0.1`, not `localhost`: `localhost` can resolve to `::1` first, and the server binds IPv4. If the tunnel keeps reconnecting, try `--protocol http2` (the default transport uses UDP, which some networks block).

**Before sharing the URL,** go through the checklist in [Security.md](Security.md#7-operational-checklist). In particular: a release build, a real database (not the development seed), no demo routes, and an access log you keep private.

**Do not run load tests or attack scripts through the tunnel.** Run them against `127.0.0.1`.

For a stable address you need a named tunnel and a domain on Cloudflare, which is not set up yet.

## Data, backups and migrations

- **Location.** `data/tourneyd.db` by default, plus `-wal` and `-shm` companion files while the server runs. The directory is created `0700` and the file `0600`. Keep it outside `public/`.
- **Migrations** run automatically at startup and with `tourneyctl migrate`. See [Schema.md](Schema.md#migrations) for the rules.
- **Backup (safe while running):**

```bash
sqlite3 data/tourneyd.db ".backup 'backups/tourneyd-$(date +%F).db'"
```

  Do not copy the `.db` file alone while the server runs. Protect backups like the live file.
- **Restore.** Stop the server, replace `data/tourneyd.db`, delete any `-wal` and `-shm` files, start the server.
- **Reset a development database.** Stop the server, `rm -f data/tourneyd.db*`, and start again (or rerun the seed script).
- **Downgrades.** An older server refuses to open a database created by a newer one. Restore a backup instead.

## Logs

Two streams, both one line per event:

| Stream | Content | Example |
|---|---|---|
| stdout | Access log, one line per request | `2026-10-26T12:00:00Z ACCESS pid=4312 203.0.113.9 "GET /api/challenges HTTP/1.1" 200 412 318us "curl/8.5.0"` |
| stderr | Diagnostics, `INFO` and `ERROR` lines | `2026-10-26T12:00:01Z ERROR pid=4312 db: step: database is locked` |

Access fields: UTC time, process id, client address, request line (`-` when the request could not be parsed), status, body bytes sent, handling time in microseconds, `User-Agent`, and ` aborted` when the client disconnected during the response. Request bodies, cookies and `Authorization` headers are never logged, so submitted flags never appear.

A connection that closes without sending anything produces no line. Children killed by a signal are reported as `child <pid> killed by signal <n>`. Logs are not rotated by the server. Redirect them and rotate externally, and treat them as private because they contain client addresses.

## Troubleshooting

| Symptom | Likely cause and fix |
|---|---|
| `bind: Address already in use` at startup | Another server is on that port (`ss -tlnp \| grep 8080`) or another process holds it. A restart right after a stop works, so this is not `TIME_WAIT`. |
| Startup stops with `config:N: unknown key` | A typo in `server.conf`. Fix the named line. |
| `db: migration ... failed` or `database is at version N but this server only knows up to M` | A bad migration file, or an older binary against a newer database. Restore a backup or rebuild the matching version. |
| `db: open data/tourneyd.db` fails | The directory is not writable by the server's user, or the path in `db_path` is wrong. |
| API answers `503` | The database is busy longer than `db_busy_timeout_ms`, or it cannot be opened. Check `build/error.log`. |
| Everything logs as `127.0.0.1` behind the tunnel | `trust_proxy` is off, or the server was not restarted after editing the config. |
| Pages load without styles or scripts, console shows `Refused to ...` | Inline code violates the CSP. Move it into a file under `public/`. |
| A challenge attachment link returns 404 | The file is not under `public/files/<slug>/`, or the name has characters outside `A-Z a-z 0-9 . _ -`. |
| Page says "Challenge not found" for a challenge that exists | It is hidden (`tourneyctl challenge list` shows `pub no`), or the slug in the URL is wrong. |
| New clients hang while others work | The child cap is reached (`max_children`). They wait in the backlog until a worker frees up. |
| `429` while testing a challenge | The per-challenge limit of 30 attempts per minute. Wait a minute. |
| Seed script stops with `SELF-CHECK FAILED` | A generator changed without its solver (or the other way round). The message names the challenge. |
| `seed-dev.sh: missing tool` | Install the named tool (`sudo apt install xxd`). |
| `make test` fails only under `SAN=1` | A memory or undefined-behavior bug. Run the failing test directly and read the sanitizer report. |
