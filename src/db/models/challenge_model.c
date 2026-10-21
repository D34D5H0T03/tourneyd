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
    // treat NULL as ""
    snprintf(dst, cap, "%s", src ? (const char *)src : "");
}

// returns a list of challange summary for browsing page
int challenge_list(const char *event_slug, challenge_summary_t *out, int max, int *count)
{
    // guard clauses
    if (!count) {
        return DB_ERROR;
    }
    // initialize out as 0
    *count = 0;

    if (!event_slug || max < 0 || (max > 0 && !out)) {
        return DB_ERROR;
    }

    // get db handle
    sqlite3 *db = db_get();
    if (!db)
        return DB_ERROR;

    // query using prepared statements for event slug and limit
    static const char SQL[] =
        "SELECT c.slug, c.title, c.category, c.points "
        "FROM challenges c JOIN events e ON e.id = c.event_id "
        "WHERE e.slug = ?1 AND c.published = 1 "
        "ORDER BY c.points ASC, c.id ASC LIMIT ?2";

    // start of prepared statement
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, SQL, -1, &st, NULL) != SQLITE_OK)
        return DB_ERROR;

    // bind the text and int inputs to the query as 1 and 2
    // may need error handling here
    sqlite3_bind_text(st, 1, event_slug, -1, SQLITE_STATIC);
    sqlite3_bind_int(st, 2, max);

    // execute statements and return the rows
    int n = 0;
    while (sqlite3_step(st) == SQLITE_ROW && n < max)
    {
        // copy the 4 columns
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

// returns a specific challenge for the description page
int challenge_get(const char *event_slug, const char *slug, challenge_t *out)
{
    // init everythin to 0
    memset(out, 0, sizeof *out);

    // get db handle
    sqlite3 *db = db_get();
    if (!db)
        return DB_ERROR;

    // query prepared statement
    static const char SQL[] =
        "SELECT c.slug, c.title, c.category, c.points, c.description "
        "FROM challenges c JOIN events e ON e.id = c.event_id "
        "WHERE e.slug = ?1 AND c.slug = ?2 AND c.published = 1";

    // prepare and check error
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, SQL, -1, &st, NULL) != SQLITE_OK)
    {
        log_error("db: prepare: %s", sqlite3_errmsg(db));
        return DB_ERROR;
    }

    // pass the parameters
    sqlite3_bind_text(st, 1, event_slug, -1, SQLITE_STATIC); 
    sqlite3_bind_text(st, 2, slug, -1, SQLITE_STATIC);

    // execute statement 
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

    // copy the summary columns to outpur
    copy_text(out->s.slug, sizeof out->s.slug, sqlite3_column_text(st, 0));
    copy_text(out->s.title, sizeof out->s.title, sqlite3_column_text(st, 1));
    copy_text(out->s.category, sizeof out->s.category, sqlite3_column_text(st, 2));
    out->s.points = sqlite3_column_int(st, 3);

    // dynamically allocate memory for the description
    const unsigned char *d = sqlite3_column_text(st, 4); /* valid only until the next step/finalize */
    int dlen = sqlite3_column_bytes(st, 4);
    out->description = malloc((size_t)dlen + 1);
    if (!out->description)
    { // malloc fail
        sqlite3_finalize(st);
        return DB_ERROR;
    }
    // copy the description to the buffer
    if (dlen > 0)
        memcpy(out->description, d, (size_t)dlen);
    out->description[dlen] = '\0'; // append null byte

    // finalize and return
    sqlite3_finalize(st);
    return DB_OK;
}

// challenge description cleanup
void challenge_free(challenge_t *c)
{
    free(c->description);
    c->description = NULL;
}

// check the flag from submission
int challenge_check_flag(const char *event_slug, const char *slug, const char *flag, int *correct)
{
    // get db handle
    sqlite3 *db = db_get();
    if (!db)
        return DB_ERROR;

    // the lookup query to get id, salt, flag hash and no of submisisons in the last 60 seconds
    static const char LOOKUP[] =
        "SELECT c.id, c.flag_salt, c.flag_hash, "
        "       (SELECT count(*) FROM submissions s WHERE s.challenge_id = c.id "
        "                                              AND s.submitted_at > unixepoch() - 60) "
        "FROM challenges c JOIN events e ON e.id = c.event_id "
        "WHERE e.slug = ?1 AND c.slug = ?2 AND c.published = 1";

    // prepare the statement and bind the parameters
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, LOOKUP, -1, &st, NULL) != SQLITE_OK)
        return DB_ERROR;
    sqlite3_bind_text(st, 1, event_slug, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, slug, -1, SQLITE_STATIC);

    // execute the query
    int rc = sqlite3_step(st);
    if (rc == SQLITE_DONE) // not found
    {
        sqlite3_finalize(st);
        return DB_NOT_FOUND;
    }
    if (rc != SQLITE_ROW) // lookup error
    {
        sqlite3_finalize(st);
        return DB_ERROR;
    }
    // signed 8 byte int
    int64_t id = sqlite3_column_int64(st, 0); // id as signed 64 bit int for sqlite INTEGER
    int salt_len = sqlite3_column_bytes(st, 1); 
    int hash_len = sqlite3_column_bytes(st, 2);
    int recent = sqlite3_column_int(st, 3); // count of recent submissions

    // unsigned 8 bit(exactly 1 byte) int for salt and hash
    uint8_t salt[FLAG_SALT_LEN]; // 16 bytes
    uint8_t want[FLAG_HASH_LEN]; // 32 bytes
    if (salt_len != FLAG_SALT_LEN || hash_len != FLAG_HASH_LEN) // validate len
    {
        sqlite3_finalize(st); 
        return DB_ERROR;
    }
    // copy the salt and hash bytes
    memcpy(salt, sqlite3_column_blob(st, 1), FLAG_SALT_LEN);
    memcpy(want, sqlite3_column_blob(st, 2), FLAG_HASH_LEN);
    sqlite3_finalize(st);

    // rate limiting with no user this is currently global
    if (recent >= 30)
        return DB_RATE_LIMITED; /* Rate limit stored purely in the DB! */
    
    // takes the submitted flag and adds the salt and hashes it
    uint8_t got[FLAG_HASH_LEN];
    if (flag_hash_with_salt(flag, salt, got) < 0)
        return DB_ERROR;
    *correct = flag_hash_equal(want, got); // compares the hashes

    // prepare logging statement
    static const char LOG[] = "INSERT INTO submissions (challenge_id, correct) VALUES (?1, ?2)";
    if (sqlite3_prepare_v2(db, LOG, -1, &st, NULL) != SQLITE_OK)
        return DB_ERROR;
    // bind params
    sqlite3_bind_int64(st, 1, id);
    sqlite3_bind_int(st, 2, *correct);
    rc = sqlite3_step(st); // execute
    sqlite3_finalize(st); //finalize and return

    return rc == SQLITE_DONE ? DB_OK : DB_ERROR;
}

// creates new challenfe and inserts it into the db
int challenge_create(const challenge_new_t *in)
{
    // array of unsigned bytes for salt and hashes
    uint8_t salt[FLAG_SALT_LEN];
    uint8_t hash[FLAG_HASH_LEN];
    if (flag_hash_new(in->flag, salt, hash) < 0) // hash error
        return DB_ERROR;

    sqlite3 *db = db_get();
    if (!db)
        return DB_ERROR;

    // sql data insertion using INSERT ... SELECT .. FROM ..WHERE instead of INSERT .. VALUES..
    static const char SQL[] =
        "INSERT INTO challenges (event_id, slug, title, category, points, description, flag_salt, flag_hash) "
        "SELECT id, ?2, ?3, ?4, ?5, ?6, ?7, ?8 FROM events WHERE slug = ?1";

    // prepare statement
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, SQL, -1, &st, NULL) != SQLITE_OK)
        return DB_ERROR;

    // bind text
    sqlite3_bind_text(st, 1, in->event_slug, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, in->slug, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 3, in->title, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 4, in->category, -1, SQLITE_STATIC);
    sqlite3_bind_int(st, 5, in->points);
    sqlite3_bind_text(st, 6, in->description, -1, SQLITE_STATIC);
    sqlite3_bind_blob(st, 7, salt, FLAG_SALT_LEN, SQLITE_STATIC);
    sqlite3_bind_blob(st, 8, hash, FLAG_HASH_LEN, SQLITE_STATIC);

    // execute and get error data
    int rc = sqlite3_step(st);
    int ec = sqlite3_extended_errcode(db);
    int changes = sqlite3_changes(db);

    // error
    if (rc != SQLITE_DONE)
        log_error("db: create challenge: %s", sqlite3_errmsg(db));
    sqlite3_finalize(st);

    // return
    if (rc == SQLITE_DONE)
        return changes == 1 ? DB_OK : DB_NOT_FOUND; /* 0 rows = unknown event */
    return ec == SQLITE_CONSTRAINT_UNIQUE ? DB_CONFLICT : DB_ERROR;
}