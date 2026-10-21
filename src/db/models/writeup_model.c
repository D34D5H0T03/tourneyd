#include "db/models/writeup_model.h"
#include "db/db.h"
#include "util/log.h"
#include "sqlite3.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Check comments in challenge_model.c for complete documentation*/

static void copy_text(char *dst, size_t cap, const unsigned char *src)
{
    snprintf(dst, cap, "%s", src ? (const char *)src : "");
}

int writeup_list(writeup_summary_t *out, int limit, int offset, int *count)
{
    *count = 0;
    sqlite3 *db = db_get();
    if (!db)
        return DB_ERROR;

    static const char SQL[] =
        "SELECT slug, title, author FROM writeups "
        "WHERE published = 1 "
        "ORDER BY created_at DESC LIMIT ?1 OFFSET ?2";

    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, SQL, -1, &st, NULL) != SQLITE_OK)
        return DB_ERROR;

    sqlite3_bind_int(st, 1, limit);
    sqlite3_bind_int(st, 2, offset);

    int n = 0;
    while (sqlite3_step(st) == SQLITE_ROW && n < limit)
    {
        copy_text(out[n].slug, sizeof out[n].slug, sqlite3_column_text(st, 0));
        copy_text(out[n].title, sizeof out[n].title, sqlite3_column_text(st, 1));
        copy_text(out[n].author, sizeof out[n].author, sqlite3_column_text(st, 2));
        n++;
    }
    sqlite3_finalize(st);
    *count = n;
    return DB_OK;
}

int writeup_get(const char *slug, writeup_t *out)
{
    memset(out, 0, sizeof *out);
    sqlite3 *db = db_get();
    if (!db)
        return DB_ERROR;

    static const char SQL[] =
        "SELECT slug, title, author, body_md FROM writeups "
        "WHERE slug = ?1 AND published = 1";

    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, SQL, -1, &st, NULL) != SQLITE_OK)
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
        sqlite3_finalize(st);
        return DB_ERROR;
    }

    copy_text(out->s.slug, sizeof out->s.slug, sqlite3_column_text(st, 0));
    copy_text(out->s.title, sizeof out->s.title, sqlite3_column_text(st, 1));
    copy_text(out->s.author, sizeof out->s.author, sqlite3_column_text(st, 2));

    const unsigned char *d = sqlite3_column_text(st, 3);
    int dlen = sqlite3_column_bytes(st, 3);
    out->body_md = malloc((size_t)dlen + 1);
    if (!out->body_md)
    {
        sqlite3_finalize(st);
        return DB_ERROR;
    }

    if (dlen > 0)
        memcpy(out->body_md, d, (size_t)dlen);
    out->body_md[dlen] = '\0';

    sqlite3_finalize(st);
    return DB_OK;
}

void writeup_free(writeup_t *w)
{
    free(w->body_md);
    w->body_md = NULL;
}

int writeup_create(const writeup_new_t *in)
{
    sqlite3 *db = db_get();
    if (!db)
        return DB_ERROR;

    // Subqueries resolve the optional event_id and challenge_id safely
    static const char SQL[] =
        "INSERT INTO writeups (slug, title, author, body_md, source_url, event_id, challenge_id) "
        "VALUES (?1, ?2, ?3, ?4, ?5, "
        "  (SELECT id FROM events WHERE slug = ?6), "
        "  (SELECT id FROM challenges WHERE slug = ?7))";

    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, SQL, -1, &st, NULL) != SQLITE_OK)
        return DB_ERROR;

    sqlite3_bind_text(st, 1, in->slug, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, in->title, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 3, in->author ? in->author : "", -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 4, in->body_md, -1, SQLITE_STATIC);
    if (in->source_url)
        sqlite3_bind_text(st, 5, in->source_url, -1, SQLITE_STATIC);
    else
        sqlite3_bind_null(st, 5);

    if (in->event_slug)
        sqlite3_bind_text(st, 6, in->event_slug, -1, SQLITE_STATIC);
    else
        sqlite3_bind_null(st, 6);

    if (in->challenge_slug)
        sqlite3_bind_text(st, 7, in->challenge_slug, -1, SQLITE_STATIC);
    else
        sqlite3_bind_null(st, 7);

    int rc = sqlite3_step(st);
    int ec = sqlite3_extended_errcode(db);
    sqlite3_finalize(st);

    if (rc == SQLITE_DONE)
        return DB_OK;
    return ec == SQLITE_CONSTRAINT_UNIQUE ? DB_CONFLICT : DB_ERROR;
}