# API Reference

**Applies to:** tourneyd `v1.0`
**Last updated:** 2026-10-26

A small JSON API served by the same process as the static site. All endpoints are same-origin: no CORS headers are sent, so other origins cannot read the responses. There are no accounts, so no endpoint needs authentication, and nothing in the API creates or changes content (that is done offline with `tourneyctl`, see [Operations.md](Operations.md)).

## Conventions

| Topic | Rule |
|---|---|
| Base path | `/api/` |
| Format | Responses are `application/json; charset=utf-8`, UTF-8, compact (no extra whitespace) |
| Caching | Every API response carries `Cache-Control: no-store` |
| Identifiers | Public identifiers are **slugs**: `[a-z0-9-]`, 1 to 64 characters. A path that contains anything else is a `404`. |
| Time | Unix seconds, UTC (`created_at`) |
| Unset values | `null` (never an empty string) for optional links such as `event`, `challenge`, `source_url` |
| Visibility | Hidden (unpublished) challenges and writeups behave exactly like missing ones: `404`, and they never appear in lists |
| Methods | `HEAD` works wherever `GET` does. A known path with a wrong method answers `405` with an `Allow` header |
| Secrets | Flag hashes and salts are never returned |

### Errors

Errors from API handlers are JSON with the same shape everywhere:

```json
{"error":"no such challenge"}
```

| Status | When |
|---|---|
| 400 | The request is malformed (for example an empty or oversized flag) |
| 404 | Unknown or hidden challenge or writeup, or a slug that is not valid |
| 405 | The path exists but not for that method (the body is an HTML page produced by the router, and `Allow` lists the allowed methods) |
| 429 | Too many flag attempts on one challenge. `Retry-After: 60` is set. |
| 503 | The database is busy or unavailable. Retry shortly. |
| 500 | Unexpected internal failure |

Errors raised before a handler runs (a malformed HTTP request, a request that is too large, a timeout) use the server's HTML error pages, and the status codes are listed in [Security.md](Security.md#status-codes-returned-for-rejected-input).

---

## Challenges

The API currently serves the single event `practice`. The schema supports many events, and selecting one will be a query parameter (see [Schema.md](Schema.md#adding-events)).

### `GET /api/challenges`

Lists published challenges, easiest first (by points, then slug), up to 200.

```json
{"challenges":[
  {"slug":"layers","title":"Layers","category":"misc","points":50},
  {"slug":"one-byte-key","title":"One Byte Key","category":"crypto","points":100}
]}
```

| Status | Meaning |
|---|---|
| 200 | The list (empty if there are no published challenges) |
| 503 | Database unavailable |

### `GET /api/challenges/<slug>`

One challenge, including its description. Descriptions are plain text. A path like `/files/layers/layers.txt` inside a description refers to a downloadable attachment, and the frontend turns it into a link.

```json
{"slug":"layers","title":"Layers","category":"misc","points":50,
 "description":"A teammate swore that nobody would ever read this file...\n\nAttachment: /files/layers/layers.txt\n"}
```

| Status | Meaning |
|---|---|
| 200 | The challenge |
| 404 | Unknown, hidden, or invalid slug |
| 503 | Database unavailable |

### `POST /api/challenges/<slug>/submit`

Checks a flag. The **request body is the flag itself, as plain text**, 1 to 256 bytes, with no NUL byte. Leading and trailing whitespace is trimmed. Any `Content-Type` is accepted, and `text/plain` is the convention.

```bash
curl -s -X POST --data 'flag{this_is_only_a_test}' \
     http://127.0.0.1:8080/api/challenges/test-flag/submit
# {"correct":true}

echo 'flag{wrong}' | curl -s -X POST --data-binary @- \
     http://127.0.0.1:8080/api/challenges/test-flag/submit
# {"correct":false}
```

A wrong flag is a normal answer, not an error: the status is `200` and the body is `{"correct":false}`.

| Status | Meaning |
|---|---|
| 200 | `{"correct":true}` or `{"correct":false}` |
| 400 | Empty body, more than 256 bytes, a NUL byte, or only whitespace |
| 404 | Unknown, hidden, or invalid slug |
| 429 | 30 attempts in the last 60 seconds on this challenge. This applies to every flag, including the correct one. `Retry-After: 60` |
| 503 | Database busy or unavailable. Try again. |

Every attempt is recorded as `(challenge, correct, time)`. The submitted text is never stored, echoed or logged. The limit is **per challenge and global**, not per person, because there are no accounts yet (see the known limitations in [Security.md](Security.md#6-known-limitations)).

---

## Writeups

### `GET /api/writeups`

Lists published writeups, newest first.

| Query parameter | Default | Range | Notes |
|---|---|---|---|
| `limit` | 20 | 1 to 50 | Values outside the range are clamped, and a non-numeric value uses the default |
| `offset` | 0 | 0 to 100000 | Same rule |

```json
{"total":3,"offset":0,"limit":20,"writeups":[
  {"slug":"layers-walkthrough","title":"Walkthrough: Layers","author":"tourneyd",
   "created_at":1792000000,"event":"practice","challenge":"layers","source_url":null},
  {"slug":"ctf-writeups-around-the-web","title":"Writeups around the web","author":"various",
   "created_at":1792000000,"event":null,"challenge":null,"source_url":"https://ctftime.org/writeups"}
]}
```

`total` counts all published writeups, so a client can build pagination. `event` and `challenge` are the slugs of the linked event and challenge. `source_url` is set for writeups that live elsewhere, and the writeup's body is then a short summary.

### `GET /api/writeups/<slug>`

One writeup, with the same fields as the list plus the body.

```json
{"slug":"how-flags-are-stored","title":"How this platform stores flags","author":"tourneyd",
 "created_at":1792000000,"event":null,"challenge":null,"source_url":null,
 "body_md":"How this platform stores flags\n\nA challenge's flag is never stored..."}
```

`body_md` is text (up to 256 KiB), conventionally Markdown, and is currently displayed as preformatted text. Invalid UTF-8 in stored text is replaced with U+FFFD, and control characters are escaped.

| Status | Meaning |
|---|---|
| 200 | The writeup |
| 404 | Unknown, hidden, or invalid slug |
| 503 | Database unavailable |

---

## Static content used by the API

| Path | Content |
|---|---|
| `/files/<challenge-slug>/<file>` | Attachments for a challenge. They are generated by the seed script or placed by hand in `public/files/`, and are served as downloads. |

## Behavior to know about

- `GET /api/challenges/<slug>/submit` returns `404` (the shape does not match a challenge lookup), while `POST /api/writeups` returns `405` (the path exists, the method does not).
- A `GET` to an unknown path under `/api/` falls through to the static handler and returns its HTML `404` page. A JSON `404` for the whole prefix is a planned improvement.
- The API is versionless for now. If an incompatible change is ever needed, it will be added under a new prefix.
