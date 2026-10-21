#include "db/models/lab_model.h"
#include "db/models/model_util.h" // Added for model_prepare
#include "db/db.h"
#include "sqlite3.h"

// Atomically increments the counter row
int lab_db_counter_next(int64_t *out_val) {
    sqlite3 *db = db_get();
    if (!db) return DB_ERROR;

    // Uses RETURNING to safely fetch the new value in the exact same atomic transaction,
    // avoiding race conditions between an UPDATE and a subsequent SELECT.
    sqlite3_stmt *st = model_prepare(db, "UPDATE lab_counter SET n = n + 1 WHERE id = 1 RETURNING n");
    if (!st) return DB_ERROR;

    int rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
        *out_val = sqlite3_column_int64(st, 0);
        sqlite3_finalize(st);
        return DB_OK;
    }
    sqlite3_finalize(st);
    return DB_ERROR;
}

// Simulates a slow disk I/O operation
int lab_db_sleep(int ms) {
    sqlite3 *db = db_get();
    if (!db) return DB_ERROR;
    
    // Hard limit the sleep to 2 seconds max to prevent accidental permanent locking
    if (ms > 2000) ms = 2000;
    if (ms < 0) ms = 0;
    
    sqlite3_sleep(ms);
    return DB_OK;
}

// Simulates a computationally expensive database query
int lab_db_cpu(long n, int64_t *out_count) {
    sqlite3 *db = db_get();
    if (!db) return DB_ERROR;

    if (n > 5000000) n = 5000000;
    
    // A recursive CTE designed to intentionally burn CPU cycles entirely inside the SQLite engine.
    // It builds an in-memory table of 'n' rows by adding 1 recursively.
    static const char SQL[] = 
        "WITH RECURSIVE c(x) AS (SELECT 1 UNION ALL SELECT x+1 FROM c WHERE x < ?1) "
        "SELECT count(*) FROM c";
        
    sqlite3_stmt *st = model_prepare(db, SQL);
    if (!st) return DB_ERROR;
    
    sqlite3_bind_int64(st, 1, n);

    int rc = sqlite3_step(st);
    if (rc == SQLITE_ROW) {
        *out_count = sqlite3_column_int64(st, 0);
        sqlite3_finalize(st);
        return DB_OK;
    }
    sqlite3_finalize(st);
    return DB_ERROR;
}