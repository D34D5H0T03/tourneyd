#ifndef MODEL_UTIL_H
#define MODEL_UTIL_H

#include "db/db.h"
#include "util/log.h"
#include "sqlite3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline sqlite3_stmt *model_prepare(sqlite3 *db, const char *sql)
{
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, sql, -1, &st, NULL) != SQLITE_OK)
    {
        log_error("db: prepare: %s", sqlite3_errmsg(db));
        return NULL;
    }
    return st;
}

/* Copies a TEXT column into a fixed buffer. NULL becomes "". Truncates, always NUL-terminates. */
static inline void model_copy_text(char *dst, size_t cap, const unsigned char *src)
{
    snprintf(dst, cap, "%s", src ? (const char *)src : "");
}

/* malloc'd, NUL-terminated copy of a TEXT column (NULL if allocation fails). *len = byte length. */
static inline char *model_dup_text(sqlite3_stmt *st, int col, size_t *len)
{
    const unsigned char *t = sqlite3_column_text(st, col); /* text first, THEN bytes */
    int n = sqlite3_column_bytes(st, col);
    char *out = malloc((size_t)n + 1);
    if (!out)
        return NULL;
    if (n > 0)
        memcpy(out, t, (size_t)n);
    out[n] = '\0';
    if (len)
        *len = (size_t)n;
    return out;
}

/* Runs a write statement once and classifies the outcome. Needs extended result codes (db.c enables them). */
static inline int model_step_write(sqlite3 *db, sqlite3_stmt *st, const char *what)
{
    int rc = sqlite3_step(st);
    int ec = sqlite3_extended_errcode(db);
    if (rc == SQLITE_DONE)
        return DB_OK;
    if (ec == SQLITE_CONSTRAINT_UNIQUE || ec == SQLITE_CONSTRAINT_PRIMARYKEY)
        return DB_CONFLICT;
    if (ec == SQLITE_CONSTRAINT_CHECK || ec == SQLITE_CONSTRAINT_NOTNULL ||
        ec == SQLITE_CONSTRAINT_FOREIGNKEY)
        return DB_INVALID;
    log_error("db: %s: %s", what, sqlite3_errmsg(db));
    return DB_ERROR; /* includes SQLITE_BUSY */
}

#endif