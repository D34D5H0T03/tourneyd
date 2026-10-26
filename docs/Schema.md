# Database Schema

**Applies to:** tourneyd `v1.0`, schema version `1`
**Last updated:** 2026-10-26

The database is a single SQLite file (default `data/tourneyd.db`). SQLite is vendored in `third_party/sqlite/`, so the version is pinned. Anything written here that needs a minimum version (`STRICT` tables, `RETURNING`, `unixepoch()`) is satisfied by that copy, not by the operating system's package.

## Conventions

| Convention | Rule |
|---|---|
| Tables | `STRICT`, so SQLite enforces column types |
| Primary keys | `INTEGER PRIMARY KEY` (an alias for the row id) |
| Timestamps | Integer unix seconds, UTC, default `unixepoch()` |
| Public identifiers | `slug` columns: `[a-z0-9-]`, 1 to 64 characters, enforced by a `CHECK` using `GLOB`, and again in code before any query |
| Sizes | Limited in **bytes** with `length(CAST(col AS BLOB))`, because `length()` on text counts characters |
| Booleans | `INTEGER` with `CHECK (col IN (0, 1))` |
| Foreign keys | Always enabled (`PRAGMA foreign_keys=ON` and the compile-time default), with explicit `ON DELETE` behavior |
| Secrets | Stored only as salted hashes, never in a column the API can return |
| Derived state | Computed from append-only logs, never stored (solves and scoreboards will be queries over `submissions`) |
| Strings | UTF-8 only. The admin tool rejects anything else. |

## Entity overview

```text
 events 1 ───< challenges 1 ───< submissions
   │                │
   │ 0..1           │ 0..1
   └──────< writeups >──────┘
```

- An **event** is a competition or an archive. `practice` is the always-open archive and the only one the API serves today.
- A **challenge** belongs to exactly one event. Its slug is unique within the event, not globally.
- A **submission** is one flag attempt on one challenge. It is an append-only log.
- A **writeup** may be linked to an event and to a challenge, or to neither.

## Tables (schema version 1, `migrations/001_init.sql`)

### `events`

| Column | Type | Notes |
|---|---|---|
| `id` | integer | primary key |
| `slug` | text | unique, slug rule |
| `title` | text | 1 to 200 characters |
| `kind` | text | `ctf`, `cp` or `hackathon` (default `ctf`) |
| `description` | text | up to 64 KiB |
| `starts_at`, `ends_at` | integer or null | null means open-ended. When both are set, `starts_at < ends_at`. **Not enforced by the API yet.** |
| `created_at` | integer | |

Seeded with one row: `practice` ("Practice Archive").

### `challenges`

| Column | Type | Notes |
|---|---|---|
| `id` | integer | primary key |
| `event_id` | integer | required, `REFERENCES events ON DELETE CASCADE` |
| `slug` | text | slug rule, `UNIQUE (event_id, slug)` |
| `title` | text | 1 to 200 characters |
| `category` | text | 1 to 32 characters (default `misc`) |
| `points` | integer | `>= 0` |
| `description` | text | up to 64 KiB, plain text |
| `flag_salt` | blob | exactly 16 bytes |
| `flag_hash` | blob | exactly 32 bytes: `SHA-256(flag_salt \|\| flag)` |
| `published` | integer | 0 hides it everywhere |
| `created_at`, `updated_at` | integer | |

Index: `(event_id, published)` for listing.

### `submissions`

| Column | Type | Notes |
|---|---|---|
| `id` | integer | primary key |
| `challenge_id` | integer | `REFERENCES challenges ON DELETE CASCADE` |
| `correct` | integer | 0 or 1 |
| `submitted_at` | integer | |

Index: `(challenge_id, submitted_at)`, which serves the rate-limit query (attempts in the last 60 seconds). The submitted text is never stored.

### `writeups`

| Column | Type | Notes |
|---|---|---|
| `id` | integer | primary key |
| `slug` | text | globally unique, slug rule |
| `title` | text | 1 to 200 characters |
| `author` | text | up to 100 characters (empty means anonymous) |
| `event_id` | integer or null | `REFERENCES events ON DELETE SET NULL` |
| `challenge_id` | integer or null | `REFERENCES challenges ON DELETE SET NULL` |
| `body_md` | text | 1 byte to 256 KiB |
| `source_url` | text or null | set when the writeup lives elsewhere (`http(s)` only, enforced by the admin tool) |
| `published` | integer | 0 hides it |
| `created_at`, `updated_at` | integer | listing order is `created_at DESC, id DESC` |

Index: `(published, created_at DESC)`. Deleting a challenge keeps its writeups and clears the link.

### Other tables

`002_lab.sql` is reserved for the lab endpoints used to compare concurrency models (a single-row counter table). It is not part of the public data model.

## Connection settings

Applied to every connection when it is opened:

| Pragma | Value | Why |
|---|---|---|
| `journal_mode` | `WAL` | Readers do not block the single writer, which suits one writer and many readers |
| `synchronous` | `NORMAL` | Safe with WAL, much faster than `FULL` (a power loss can lose the last few transactions, never corrupt the file) |
| `foreign_keys` | `ON` | Referential integrity is part of the model |
| `trusted_schema` | `OFF` | Defense in depth against malicious schema content |
| busy timeout | `db_busy_timeout_ms` (5000) | A writer waits for the lock instead of failing at once. A timeout becomes a `503`. |

SQLite allows one writer at a time. That is fine here: the only hot write is a flag attempt, and the busy timeout absorbs short collisions. It is also the reason a bigger worker pool will not speed up writes later.

## Migrations

- Files live in `migrations/` and are named `NNN_description.sql` with a three-digit number (`001_init.sql`).
- The schema version is stored in `PRAGMA user_version` and equals the number of the last applied file.
- On startup, `db_init` applies every file whose number is higher than the stored version, **in order, each in its own transaction together with the version bump**. If one fails, the transaction rolls back, startup stops, and the database stays at the previous version.
- The runner rejects two files with the same number and refuses to run an older server against a database whose version is higher than any file it knows.
- `tourneyctl migrate` applies pending migrations without starting the server.

**Rules for writing a migration:**

1. Never edit a migration that has been applied anywhere. Add a new file.
2. Do not put `BEGIN`, `COMMIT`, `VACUUM` or `PRAGMA foreign_keys` in a file: the runner provides the transaction, and none of those work inside one.
3. Keep each migration small and testable (the migration test applies all files to an empty database).
4. Test the upgrade on a copy of real data before deploying.

SQLite can add a nullable column with `ALTER TABLE ... ADD COLUMN`, but it cannot change a column's type or add a `NOT NULL` column without a default. For those, rebuild the table inside the migration: create the new table, copy the rows, drop the old one, rename the new one, and recreate its indexes.

## How the model will grow

### Adding users

A future `003_users.sql` would add, without touching existing rows:

- `users` (and `teams` if needed) and a `sessions` table;
- `submissions.user_id INTEGER REFERENCES users(id)` and `writeups.author_user_id INTEGER REFERENCES users(id)`, both nullable at first;
- `CREATE UNIQUE INDEX one_solve ON submissions (challenge_id, user_id) WHERE correct = 1` so a user solves a challenge once;
- `CREATE VIEW scoreboard AS ...` over `submissions` and `challenges`, because scores are derived, never stored.

The rate limit then becomes per user (and per address) instead of per challenge.

### Adding events

The data model already supports many events. To serve them:

1. Add `event_list` and `event_get` models.
2. Select the event in the API with a query parameter (`?event=<slug>`, defaulting to `practice`), so the router does not change.
3. Key the frontend's solved marks by `event/slug`, because a slug is unique only within its event.
4. Enforce visibility and submission windows **inside the SQL** (`starts_at IS NULL OR starts_at <= unixepoch()`), so no handler can forget the rule.

Hackathon and CP events differ from flag-based CTFs (human judging, external judges), so their `kind`-specific behavior belongs in a later design, not in the schema.

## Inspecting and backing up

```bash
sqlite3 data/tourneyd.db '.schema'
sqlite3 data/tourneyd.db 'PRAGMA user_version;'
sqlite3 data/tourneyd.db 'SELECT slug, published FROM challenges;'
sqlite3 data/tourneyd.db ".backup 'backup.db'"       # safe while the server is running
```

Do not copy the `.db` file by itself while the server runs: recent changes may still be in the `-wal` file. Use `.backup`. Backups hold the same salted hashes as the live database and need the same protection. To restore, stop the server, replace the file, and delete any leftover `-wal` and `-shm` files.
