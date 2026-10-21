#include "db/models/writeup_model.h"
#include "db/models/model_util.h"
#include "db/db.h"
#include "util/log.h"

// Macros to prevent giant SQL strings cluttering the C functions
#define WRITEUP_COLUMNS \
    "w.slug, w.title, w.author, w.created_at, w.source_url, e.slug, c.slug, w.published"
#define WRITEUP_JOINS                                           \
    " FROM writeups w LEFT JOIN events e ON e.id = w.event_id " \
    "                 LEFT JOIN challenges c ON c.id = w.challenge_id "

static const char INSERT_SQL[] =
    "INSERT INTO writeups (slug, title, author, event_id, challenge_id, body_md, source_url, published) "
    "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8)";

// The ON CONFLICT clause enables "Upserts". If the slug already exists, it updates all the text
// but explicitly leaves 'created_at' alone so the post doesn't lose its chronological place.
static const char UPSERT_SQL[] =
    "INSERT INTO writeups (slug, title, author, event_id, challenge_id, body_md, source_url, published) "
    "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8) "
    "ON CONFLICT(slug) DO UPDATE SET "
    "  title = excluded.title, author = excluded.author, event_id = excluded.event_id, "
    "  challenge_id = excluded.challenge_id, body_md = excluded.body_md, "
    "  source_url = excluded.source_url, published = excluded.published, "
    "  updated_at = unixepoch()";

// Helper to populate a summary struct from an active SQLite statement row
static void read_summary(sqlite3_stmt *st, writeup_summary_t *s)
{
    model_copy_text(s->slug, sizeof s->slug, sqlite3_column_text(st, 0));
    model_copy_text(s->title, sizeof s->title, sqlite3_column_text(st, 1));
    model_copy_text(s->author, sizeof s->author, sqlite3_column_text(st, 2));
    s->created_at = sqlite3_column_int64(st, 3);
    model_copy_text(s->source_url, sizeof s->source_url, sqlite3_column_text(st, 4));

    // LEFT JOIN returns NULL for unlinked rows, which model_copy_text safely turns into ""
    model_copy_text(s->event_slug, sizeof s->event_slug, sqlite3_column_text(st, 5));
    model_copy_text(s->challenge_slug, sizeof s->challenge_slug, sqlite3_column_text(st, 6));
    s->published = sqlite3_column_int(st, 7);
}

// Counts total rows for pagination
static int count_rows(sqlite3 *db, int include_unpublished, int *total)
{
    sqlite3_stmt *st = model_prepare(db, "SELECT count(*) FROM writeups WHERE (published = 1 OR ?1)");
    if (!st)
        return DB_ERROR;
    sqlite3_bind_int(st, 1, include_unpublished ? 1 : 0);
    int rc = sqlite3_step(st);
    if (rc == SQLITE_ROW)
        *total = sqlite3_column_int(st, 0);
    else
        log_error("db: count writeups: %s", sqlite3_errmsg(db));
    sqlite3_finalize(st);
    return rc == SQLITE_ROW ? DB_OK : DB_ERROR;
}

// Retrieves a paginated list of writeups
int writeup_list(int offset, int include_unpublished, writeup_summary_t *out, int max,
                 int *count, int *total)
{
    *count = 0;
    if (total)
        *total = 0;
    if (offset < 0)
        offset = 0;
    if (max > WRITEUP_LIST_MAX)
        max = WRITEUP_LIST_MAX;
    if (max < 1)
        return DB_OK;

    sqlite3 *db = db_get();
    if (!db)
        return DB_ERROR;

    sqlite3_stmt *st = model_prepare(db,
                                     "SELECT " WRITEUP_COLUMNS WRITEUP_JOINS
                                     "WHERE (w.published = 1 OR ?3) "
                                     "ORDER BY w.created_at DESC, w.id DESC LIMIT ?1 OFFSET ?2");
    if (!st)
        return DB_ERROR;

    sqlite3_bind_int(st, 1, max);
    sqlite3_bind_int(st, 2, offset);
    sqlite3_bind_int(st, 3, include_unpublished ? 1 : 0);

    int n = 0, rc = SQLITE_ROW;
    while (n < max && (rc = sqlite3_step(st)) == SQLITE_ROW)
    {
        read_summary(st, &out[n]);
        n++;
    }

    int failed = rc != SQLITE_ROW && rc != SQLITE_DONE;
    if (failed)
        log_error("db: list writeups: %s", sqlite3_errmsg(db));
    sqlite3_finalize(st);
    if (failed)
        return DB_ERROR;

    // Get the total row count to help the frontend build pagination UI
    if (total && count_rows(db, include_unpublished, total) != DB_OK)
        return DB_ERROR;

    *count = n;
    return DB_OK;
}

// Retrieves a single writeup including its massive markdown body
int writeup_get(const char *slug, writeup_t *out)
{
    memset(out, 0, sizeof *out);
    sqlite3 *db = db_get();
    if (!db)
        return DB_ERROR;

    sqlite3_stmt *st = model_prepare(db,
                                     "SELECT " WRITEUP_COLUMNS ", w.body_md" WRITEUP_JOINS
                                     "WHERE w.slug = ?1 AND w.published = 1");
    if (!st)
        return DB_ERROR;
    sqlite3_bind_text(st, 1, slug, -1, SQLITE_STATIC);

    int rc = sqlite3_step(st);
    if (rc == SQLITE_DONE)
    {
        sqlite3_finalize(st);
        return DB_NOT_FOUND;
    }
    if (rc != SQLITE_ROW)
    {
        log_error("db: get writeup: %s", sqlite3_errmsg(db));
        sqlite3_finalize(st);
        return DB_ERROR;
    }

    read_summary(st, &out->s);
    // Dynamically duplicate the markdown text off the SQLite pointer before finalize
    out->body_md = model_dup_text(st, 8, &out->body_len);

    sqlite3_finalize(st);
    return out->body_md ? DB_OK : DB_ERROR;
}

void writeup_free(writeup_t *w)
{
    free(w->body_md);
    w->body_md = NULL;
    w->body_len = 0;
}

// Looks up a relational ID by slug to prevent silent NULL insertions for mistyped foreign keys
static int lookup_id(sqlite3 *db, const char *sql, const char *a, const char *b, int64_t *id)
{
    sqlite3_stmt *st = model_prepare(db, sql);
    if (!st)
        return DB_ERROR;
    sqlite3_bind_text(st, 1, a, -1, SQLITE_STATIC);
    if (b)
        sqlite3_bind_text(st, 2, b, -1, SQLITE_STATIC);

    int rc = sqlite3_step(st);
    if (rc == SQLITE_ROW)
        *id = sqlite3_column_int64(st, 0);
    else if (rc != SQLITE_DONE)
        log_error("db: lookup: %s", sqlite3_errmsg(db));

    sqlite3_finalize(st);
    return rc == SQLITE_ROW ? DB_OK : (rc == SQLITE_DONE ? DB_NOT_FOUND : DB_ERROR);
}

// Core save engine used by both create and upsert
static int save(const writeup_new_t *in, int overwrite)
{
    // Extensive validation layer to prevent garbage data from ever reaching the engine
    if (!in || !in->slug || !in->title || !in->body_md ||
        in->body_len == 0 || in->body_len > WRITEUP_BODY_MAX ||
        !slug_valid(in->slug, strlen(in->slug)) ||
        (in->challenge_slug && !in->event_slug) ||
        memchr(in->body_md, '\0', in->body_len))
    {
        return DB_INVALID;
    }
    sqlite3 *db = db_get();
    if (!db)
        return DB_ERROR;

    // Resolve the relational links up front. A typo in an event or challenge slug must be an error, not a NULL.
    int64_t event_id = 0, challenge_id = 0;
    int rc;
    if (in->event_slug)
    {
        rc = lookup_id(db, "SELECT id FROM events WHERE slug = ?1", in->event_slug, NULL, &event_id);
        if (rc == DB_NOT_FOUND)
            log_error("db: unknown event '%s'", in->event_slug);
        if (rc != DB_OK)
            return rc;
    }
    if (in->challenge_slug)
    {
        rc = lookup_id(db,
                       "SELECT c.id FROM challenges c JOIN events e ON e.id = c.event_id "
                       "WHERE e.slug = ?1 AND c.slug = ?2",
                       in->event_slug, in->challenge_slug, &challenge_id);
        if (rc == DB_NOT_FOUND)
            log_error("db: unknown challenge '%s' in event '%s'", in->challenge_slug, in->event_slug);
        if (rc != DB_OK)
            return rc;
    }

    sqlite3_stmt *st = model_prepare(db, overwrite ? UPSERT_SQL : INSERT_SQL);
    if (!st)
        return DB_ERROR;

    sqlite3_bind_text(st, 1, in->slug, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, in->title, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 3, in->author ? in->author : "", -1, SQLITE_STATIC);

    if (in->event_slug)
        sqlite3_bind_int64(st, 4, event_id);
    else
        sqlite3_bind_null(st, 4);
    if (in->challenge_slug)
        sqlite3_bind_int64(st, 5, challenge_id);
    else
        sqlite3_bind_null(st, 5);

    // Explicit length is used here so the 256KB body avoids assumptions about null termination
    sqlite3_bind_text(st, 6, in->body_md, (int)in->body_len, SQLITE_STATIC);

    if (in->source_url && in->source_url[0])
        sqlite3_bind_text(st, 7, in->source_url, -1, SQLITE_STATIC);
    else
        sqlite3_bind_null(st, 7);

    sqlite3_bind_int(st, 8, in->published ? 1 : 0);

    rc = model_step_write(db, st, "save writeup");
    sqlite3_finalize(st);
    return rc;
}

int writeup_create(const writeup_new_t *in){ 
    return save(in, 0); 
}

int writeup_upsert(const writeup_new_t *in){ 
    return save(in, 1); 
}

// Helper for UPDATE/DELETE by slug
static int slug_write(const char *sql, const char *slug, int flag)
{
    sqlite3 *db = db_get();
    if (!db)
        return DB_ERROR;

    sqlite3_stmt *st = model_prepare(db, sql);
    if (!st)
        return DB_ERROR;

    sqlite3_bind_text(st, 1, slug, -1, SQLITE_STATIC);
    if (flag >= 0)
        sqlite3_bind_int(st, 2, flag);

    int rc = model_step_write(db, st, "writeup update");
    // "0 rows changed" means the query ran perfectly, but the slug didn't exist
    int changed = sqlite3_changes(db);

    sqlite3_finalize(st);
    return (rc == DB_OK && changed == 0) ? DB_NOT_FOUND : rc;
}

int writeup_set_published(const char *slug, int published)
{
    return slug_write("UPDATE writeups SET published = ?2, updated_at = unixepoch() WHERE slug = ?1",
                      slug, published ? 1 : 0);
}

int writeup_delete(const char *slug)
{
    return slug_write("DELETE FROM writeups WHERE slug = ?1", slug, -1);
}