#include "db/models/challenge_model.h"
#include "db/models/model_util.h"
#include "db/db.h"
#include "util/log.h"
#include "sqlite3.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

// returns a list of challange summary for browsing page
int challenge_list(const char *event_slug, int include_unpublished, challenge_summary_t *out, int max, int *count)
{
    // initialize out as 0
    *count = 0;

    // guard clauses and limit capping
    if (max > CHALLENGE_LIST_MAX) max = CHALLENGE_LIST_MAX;
    if (max < 1) return DB_OK;

    // get db handle
    sqlite3 *db = db_get();
    if (!db)
        return DB_ERROR;

    // query using prepared statements for event slug and limit
    // updated to include published column and conditional filtering
    sqlite3_stmt *st = model_prepare(db,
        "SELECT c.slug, c.title, c.category, c.points, c.published "
        "FROM challenges c JOIN events e ON e.id = c.event_id "
        "WHERE e.slug = ?1 AND (c.published = 1 OR ?2) "
        "ORDER BY c.points, c.slug LIMIT ?3");
    if (!st) return DB_ERROR;

    // bind the text and int inputs to the query as 1, 2 and 3
    sqlite3_bind_text(st, 1, event_slug, -1, SQLITE_STATIC);
    sqlite3_bind_int(st, 2, include_unpublished ? 1 : 0);
    sqlite3_bind_int(st, 3, max);

    // execute statements and return the rows
    int n = 0, rc = SQLITE_ROW;
    while (n < max && (rc = sqlite3_step(st)) == SQLITE_ROW)
    {
        // copy the 5 columns using model_util helper
        model_copy_text(out[n].slug, sizeof out[n].slug, sqlite3_column_text(st, 0));
        model_copy_text(out[n].title, sizeof out[n].title, sqlite3_column_text(st, 1));
        model_copy_text(out[n].category, sizeof out[n].category, sqlite3_column_text(st, 2));
        out[n].points = sqlite3_column_int(st, 3);
        out[n].published = sqlite3_column_int(st, 4);
        n++;
    }
    
    int failed = rc != SQLITE_ROW && rc != SQLITE_DONE;
    if (failed) log_error("db: list challenges: %s", sqlite3_errmsg(db));
    
    sqlite3_finalize(st);
    if (failed) return DB_ERROR;
    
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

    // prepare using model_util helper
    sqlite3_stmt *st = model_prepare(db, SQL);
    if (!st) return DB_ERROR;

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

    // copy the summary columns to output using model_util helper
    model_copy_text(out->s.slug, sizeof out->s.slug, sqlite3_column_text(st, 0));
    model_copy_text(out->s.title, sizeof out->s.title, sqlite3_column_text(st, 1));
    model_copy_text(out->s.category, sizeof out->s.category, sqlite3_column_text(st, 2));
    out->s.points = sqlite3_column_int(st, 3);

    // dynamically allocate memory for the description using model_util helper
    out->description = model_dup_text(st, 4, NULL);
    if (!out->description)
    { // malloc fail
        sqlite3_finalize(st);
        return DB_ERROR;
    }

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
    sqlite3_stmt *st = model_prepare(db, LOOKUP);
    if (!st) return DB_ERROR;
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
    st = model_prepare(db, "INSERT INTO submissions (challenge_id, correct) VALUES (?1, ?2)");
    if (!st) return DB_ERROR;
    
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

    // prepare statement using model_util helper
    sqlite3_stmt *st = model_prepare(db, SQL);
    if (!st) return DB_ERROR;

    // bind text
    sqlite3_bind_text(st, 1, in->event_slug, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, in->slug, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 3, in->title, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 4, in->category, -1, SQLITE_STATIC);
    sqlite3_bind_int(st, 5, in->points);
    sqlite3_bind_text(st, 6, in->description, -1, SQLITE_STATIC);
    sqlite3_bind_blob(st, 7, salt, FLAG_SALT_LEN, SQLITE_STATIC);
    sqlite3_bind_blob(st, 8, hash, FLAG_HASH_LEN, SQLITE_STATIC);

    // execute and get error data using model_step_write helper
    int rc = model_step_write(db, st, "create challenge");
    int changes = sqlite3_changes(db);
    sqlite3_finalize(st);
    
    // return
    if (rc == DB_OK && changes == 0) return DB_NOT_FOUND; /* the SELECT matched no event */
    return rc;
}

// tourneyctl helpers

// helper to execute an update or delete by slug
static int challenge_write(const char *sql, const char *event_slug, const char *slug, int flag) {
    sqlite3 *db = db_get();
    if (!db) return DB_ERROR;
    
    sqlite3_stmt *st = model_prepare(db, sql);
    if (!st) return DB_ERROR;
    
    sqlite3_bind_text(st, 1, event_slug, -1, SQLITE_STATIC);
    sqlite3_bind_text(st, 2, slug, -1, SQLITE_STATIC);
    if (flag >= 0) sqlite3_bind_int(st, 3, flag);
    
    int rc = model_step_write(db, st, "challenge update");
    int changed = sqlite3_changes(db);
    sqlite3_finalize(st);
    
    return (rc == DB_OK && changed == 0) ? DB_NOT_FOUND : rc;
}

// publish or hide a challenge
int challenge_set_published(const char *event_slug, const char *slug, int published) {
    return challenge_write(
        "UPDATE challenges SET published = ?3, updated_at = unixepoch() "
        "WHERE slug = ?2 AND event_id = (SELECT id FROM events WHERE slug = ?1)",
        event_slug, slug, published ? 1 : 0);
}

// delete a challenge (cascades to its submissions)
int challenge_delete(const char *event_slug, const char *slug) {
    return challenge_write(
        "DELETE FROM challenges WHERE slug = ?2 AND event_id = (SELECT id FROM events WHERE slug = ?1)",
        event_slug, slug, -1);
}