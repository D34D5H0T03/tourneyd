#include "db/models/lab_model.h"
#include "db/db.h"
#include "sqlite3.h"

#include <stdio.h>

int lab_db_counter_next(int64_t *out_val)
{
    sqlite3 *db = db_get(); // get db
    if (!db)
        return DB_ERROR;

    sqlite3_stmt *st = NULL; // prepare sql statement
    // Uses RETURNING to safely fetch the new value in the exact same atomic transaction
    if (sqlite3_prepare_v2(db, "UPDATE lab_counter SET n = n + 1 WHERE id = 1 RETURNING n", -1, &st, NULL) != SQLITE_OK)
        return DB_ERROR;

    int rc = sqlite3_step(st); // execute
    if (rc == SQLITE_ROW)
    {
        *out_val = sqlite3_column_int64(st, 0); // get val and finalize
        sqlite3_finalize(st);
        return DB_OK;
    }
    sqlite3_finalize(st);
    return DB_ERROR; //error
}

int lab_db_sleep(int ms)
{
    sqlite3 *db = db_get();
    if (!db)
        return DB_ERROR;

    // Limits the sleep to 2 seconds max
    if (ms > 2000)
        ms = 2000;
    if (ms < 0)
        ms = 0;

    sqlite3_sleep(ms);
    return DB_OK;
}

int lab_db_cpu(long n, int64_t *out_count)
{
    sqlite3 *db = db_get();
    if (!db)
        return DB_ERROR;

    if (n > 5000000)
        n = 5000000;

    // A recursive CTE designed to intentionally burn CPU cycles entirely inside the SQLite engine
    static const char SQL[] =
        "WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM c WHERE x < ?1) "
        "SELECT count(*) FROM c";

    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, SQL, -1, &st, NULL) != SQLITE_OK) // prepare statement
        return DB_ERROR;
    sqlite3_bind_int64(st, 1, n); // bind param

    int rc = sqlite3_step(st);
    if (rc == SQLITE_ROW)
    {
        *out_count = sqlite3_column_int64(st, 0);
        sqlite3_finalize(st);
        return DB_OK;
    }
    sqlite3_finalize(st);
    return DB_ERROR;
}