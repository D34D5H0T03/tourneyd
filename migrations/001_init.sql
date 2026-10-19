CREATE TABLE events (
    id          INTEGER PRIMARY KEY,
    slug        TEXT NOT NULL UNIQUE
                CHECK (length(slug) BETWEEN 1 AND 64 AND slug NOT GLOB '*[^a-z0-9-]*'),
    title       TEXT NOT NULL CHECK (length(title) BETWEEN 1 AND 200),
    kind        TEXT NOT NULL DEFAULT 'ctf' CHECK (kind IN ('ctf', 'cp', 'hackathon')),
    description TEXT NOT NULL DEFAULT '' CHECK (length(CAST(description AS BLOB)) <= 65536),
    starts_at   INTEGER,                         
    ends_at     INTEGER,
    created_at  INTEGER NOT NULL DEFAULT (unixepoch()),
    CHECK (starts_at IS NULL OR ends_at IS NULL OR starts_at < ends_at)
) STRICT;

INSERT INTO events (slug, title, description)
VALUES ('practice', 'Practice Archive', 'Always-open challenge archive');

CREATE TABLE challenges (
    id          INTEGER PRIMARY KEY,
    event_id    INTEGER NOT NULL REFERENCES events(id) ON DELETE CASCADE,
    slug        TEXT NOT NULL
                CHECK (length(slug) BETWEEN 1 AND 64 AND slug NOT GLOB '*[^a-z0-9-]*'),
    title       TEXT NOT NULL CHECK (length(title) BETWEEN 1 AND 200),
    category    TEXT NOT NULL DEFAULT 'misc' CHECK (length(category) BETWEEN 1 AND 32),
    points      INTEGER NOT NULL DEFAULT 100 CHECK (points >= 0),
    description TEXT NOT NULL DEFAULT '' CHECK (length(CAST(description AS BLOB)) <= 65536),
    flag_salt   BLOB NOT NULL CHECK (length(flag_salt) = 16),
    flag_hash   BLOB NOT NULL CHECK (length(flag_hash) = 32),
    published   INTEGER NOT NULL DEFAULT 1 CHECK (published IN (0, 1)),
    created_at  INTEGER NOT NULL DEFAULT (unixepoch()),
    updated_at  INTEGER NOT NULL DEFAULT (unixepoch()),
    UNIQUE (event_id, slug)
) STRICT;
CREATE INDEX idx_challenges_event ON challenges (event_id, published);

CREATE TABLE submissions (
    id           INTEGER PRIMARY KEY,
    challenge_id INTEGER NOT NULL REFERENCES challenges(id) ON DELETE CASCADE,
    correct      INTEGER NOT NULL CHECK (correct IN (0, 1)),
    submitted_at INTEGER NOT NULL DEFAULT (unixepoch())
) STRICT;
CREATE INDEX idx_submissions_recent ON submissions (challenge_id, submitted_at);

CREATE TABLE writeups (
    id           INTEGER PRIMARY KEY,
    slug         TEXT NOT NULL UNIQUE
                 CHECK (length(slug) BETWEEN 1 AND 64 AND slug NOT GLOB '*[^a-z0-9-]*'),
    title        TEXT NOT NULL CHECK (length(title) BETWEEN 1 AND 200),
    author       TEXT NOT NULL DEFAULT '' CHECK (length(author) <= 100),
    event_id     INTEGER REFERENCES events(id) ON DELETE SET NULL,
    challenge_id INTEGER REFERENCES challenges(id) ON DELETE SET NULL,
    body_md      TEXT NOT NULL CHECK (length(CAST(body_md AS BLOB)) <= 262144),
    source_url   TEXT,                           
    published    INTEGER NOT NULL DEFAULT 1 CHECK (published IN (0, 1)),
    created_at   INTEGER NOT NULL DEFAULT (unixepoch()),
    updated_at   INTEGER NOT NULL DEFAULT (unixepoch())
) STRICT;
CREATE INDEX idx_writeups_listing ON writeups (published, created_at DESC);