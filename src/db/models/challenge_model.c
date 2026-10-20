#include "db/models/challenge_model.h"
#include "db/db.h"
#include "util/log.h"
#include "sqlite3.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// copies text from source to dst 
static void copy_text(char *dst, size_t cap, const unsigned char *src)
{
    snprintf(dst, cap, "%s", src ? (const char *)src : "");
}

int challenge_list(const char *event_slug, challenge_summary_t *out, int max, int *count)
{
    *count = 0;
    sqlite3 *db = db_get();
    if (!db)
        return DB_ERROR;

    static const char SQL[] =
        "SELECT c.slug, c.title, c.category, c.points "
        "FROM challenges c JOIN events e ON e.id = c.event_id "
        "WHERE e.slug = ?1 AND c.published = 1 "
        "ORDER BY c.points ASC, c.id ASC LIMIT ?2";

    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, SQL, -1, &st, NULL) != SQLITE_OK)
        return DB_ERROR;

    sqlite3_bind_text(st, 1, event_slug, -1, SQLITE_STATIC);
    sqlite3_bind_int(st, 2, max);

    int n = 0;
    while (sqlite3_step(st) == SQLITE_ROW && n < max)
    {
        copy_text(out[n].slug, sizeof out[n].slug, sqlite3_column_text(st, 0));
        copy_text(out[n].title, sizeof out[n].title, sqlite3_column_text(st, 1));
        copy_text(out[n].category, sizeof out[n].category, sqlite3_column_text(st, 2));
        out[n].points = sqlite3_column_int(st, 3);
        n++;
    }
    sqlite3_finalize(st);
    *count = n;
    return DB_OK;
}

int challenge_get(const char *event_slug, const char *slug, challenge_t *out)
{
    memset(out, 0, sizeof *out);
    sqlite3 *db = db_get();
    if (!db)
        return DB_ERROR;

    static const char SQL[] =
        "SELECT c.slug, c.title, c.category, c.points, c.description "
        "FROM challenges c JOIN events e ON e.id = c.event_id "
        "WHERE e.slug = ?1 AND c.slug = ?2 AND c.published = 1";

    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, SQL, -1, &st, NULL) != SQLITE_OK)
    {
        log_error("db: prepare: %s", sqlite3_errmsg(db));
        return DB_ERROR;
    }
    sqlite3_bind_text(st, 1, event_slug, -1, SQLITE_STATIC); /* values are BOUND, never pasted in */
    sqlite3_bind_text(st, 2, slug, -1, SQLITE_STATIC);

    int rc = sqlite3_step(st);
    if (rc == SQLITE_DONE)
    {
        sqlite3_finalize(st);
        return DB_NOT_FOUND;
    }
    if (rc != SQLITE_ROW)
    {
        log_error("db: step: %s", sqlite3_errmsg(db));
        sqlite3_finalize(st);
        return DB_ERROR; /* includes SQLITE_BUSY */
    }
    copy_text(out->s.slug, sizeof out->s.slug, sqlite3_column_text(st, 0));
    copy_text(out->s.title, sizeof out->s.title, sqlite3_column_text(st, 1));
    copy_text(out->s.category, sizeof out->s.category, sqlite3_column_text(st, 2));
    out->s.points = sqlite3_column_int(st, 3);

    const unsigned char *d = sqlite3_column_text(st, 4); /* valid only until the next step/finalize */
    int dlen = sqlite3_column_bytes(st, 4);
    out->description = malloc((size_t)dlen + 1);
    if (!out->description)
    {
        sqlite3_finalize(st);
        return DB_ERROR;
    }
    if (dlen > 0)
        memcpy(out->description, d, (size_t)dlen);
    out->description[dlen] = '\0';

    sqlite3_finalize(st);
    return DB_OK;
}

void challenge_free(challenge_t *c)
{
    free(c->description);
    c->description = NULL;
}

int challenge_check_flag(const char *event_slug, const char *slug, const char *flag, int *correct)
{
    sqlite3 *db = db_get();
    if (!db)
        return DB_ERROR;

    static const char LOOKUP[] =
        "SELECT c.id, c.flag_salt, c.flag_hash, "
        "       (SELECT count(*) FROM submissions s WHERE s.challenge_id = c.id "
        "                                              AND s.submitted_at > unixepoch() - 60) "
        "FROM challenges c JOIN events e ON e.id = c.event_id "
        "WHERE e.slug = ?1 AND c.slug = ?2 AND c.published = 1";

    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, LOOKUP, -1, &st, NULL) != SQLITE_OK)
        return DB_ERROR;
    sqlite3_bind_text(st, 1, event_slug, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, slug, -1, SQLITE_STATIC);

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

    int64_t id = sqlite3_column_int64(st, 0);
    int salt_len = sqlite3_column_bytes(st, 1), hash_len = sqlite3_column_bytes(st, 2);
    int recent = sqlite3_column_int(st, 3);

    uint8_t salt[FLAG_SALT_LEN], want[FLAG_HASH_LEN];
    if (salt_len != FLAG_SALT_LEN || hash_len != FLAG_HASH_LEN)
    {
        sqlite3_finalize(st);
        return DB_ERROR;
    }
    memcpy(salt, sqlite3_column_blob(st, 1), FLAG_SALT_LEN);
    memcpy(want, sqlite3_column_blob(st, 2), FLAG_HASH_LEN);
    sqlite3_finalize(st);

    if (recent >= 30)
        return DB_RATE_LIMITED; /* Rate limit stored purely in the DB! */

    uint8_t got[FLAG_HASH_LEN];
    if (flag_hash_with_salt(flag, salt, got) < 0)
        return DB_ERROR;
    *correct = flag_hash_equal(want, got);

    static const char LOG[] = "INSERT INTO submissions (challenge_id, correct) VALUES (?1, ?2)";
    if (sqlite3_prepare_v2(db, LOG, -1, &st, NULL) != SQLITE_OK)
        return DB_ERROR;
    sqlite3_bind_int64(st, 1, id);
    sqlite3_bind_int(st, 2, *correct);
    rc = sqlite3_step(st);
    sqlite3_finalize(st);

    return rc == SQLITE_DONE ? DB_OK : DB_ERROR;
}

int challenge_create(const challenge_new_t *in)
{
    uint8_t salt[FLAG_SALT_LEN], hash[FLAG_HASH_LEN];
    if (flag_hash_new(in->flag, salt, hash) < 0)
        return DB_ERROR;

    sqlite3 *db = db_get();
    if (!db)
        return DB_ERROR;

    static const char SQL[] =
        "INSERT INTO challenges (event_id, slug, title, category, points, description, flag_salt, flag_hash) "
        "SELECT id, ?2, ?3, ?4, ?5, ?6, ?7, ?8 FROM events WHERE slug = ?1";

    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, SQL, -1, &st, NULL) != SQLITE_OK)
        return DB_ERROR;

    sqlite3_bind_text(st, 1, in->event_slug, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, in->slug, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 3, in->title, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 4, in->category, -1, SQLITE_STATIC);
    sqlite3_bind_int(st, 5, in->points);
    sqlite3_bind_text(st, 6, in->description, -1, SQLITE_STATIC);
    sqlite3_bind_blob(st, 7, salt, FLAG_SALT_LEN, SQLITE_STATIC);
    sqlite3_bind_blob(st, 8, hash, FLAG_HASH_LEN, SQLITE_STATIC);

    int rc = sqlite3_step(st);
    int ec = sqlite3_extended_errcode(db);
    int changes = sqlite3_changes(db);

    if (rc != SQLITE_DONE)
        log_error("db: create challenge: %s", sqlite3_errmsg(db));
    sqlite3_finalize(st);

    if (rc == SQLITE_DONE)
        return changes == 1 ? DB_OK : DB_NOT_FOUND; /* 0 rows = unknown event */
    return ec == SQLITE_CONSTRAINT_UNIQUE ? DB_CONFLICT : DB_ERROR;
}