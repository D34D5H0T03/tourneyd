#include "db/db.h"
#include "db/migrate.h"
#include "util/log.h"
#include "sqlite3.h"
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#define MAX_PATH_LENGTH 256

static char g_path[MAX_PATH_LENGTH]; // containter for path to db
static char g_migrations[MAX_PATH_LENGTH]; // contaitner for path to migrations
static int g_busy_ms = 5000; //fallback value
static __thread sqlite3 *tl_db; // one connection per thread

/* Runs in child right after fork(): a SQLite connection must never be used in the process
   that did not open it, so the child forgets it and opens its own later. */
static void forget_inherited_connection(void){
    tl_db = NULL;
}

static void ensure_parent_dir(const char *path){
    char dir[MAX_PATH_LENGTH];
    snprintf(dir, sizeof(dir), "%s", path);
    char *slash = strrchr(dir, '/');

    if(slash && slash != dir){
        *slash = '\0';
        mkdir(dir, 0700); // octal permission bitmask. rwx------
    }
}

static sqlite3 *open_connection(void) {
    sqlite3 *db = NULL;
    int rc = sqlite3_open_v2(g_path, &db,
                SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX, NULL);
    if (rc != SQLITE_OK) {
        log_error("db: open %s: %s", g_path, db ? sqlite3_errmsg(db) : sqlite3_errstr(rc));
        sqlite3_close_v2(db);
        return NULL;
    }

    // setting options for db
    sqlite3_extended_result_codes(db, 1);
    sqlite3_busy_timeout(db, g_busy_ms);


    char *err = NULL;
    rc = sqlite3_exec(db,
        "PRAGMA journal_mode=WAL;"          /* readers do not block the writer */
        "PRAGMA synchronous=NORMAL;"        /* safe with WAL, much faster than FULL */
        "PRAGMA foreign_keys=ON;"           /* allows foreign key constraints*/
        "PRAGMA trusted_schema=OFF;", NULL, NULL, &err);

    // error with PRAGMA execution
    if (rc != SQLITE_OK) {
        log_error("db: pragmas: %s", err ? err : "unknown error");
        sqlite3_free(err);
        sqlite3_close_v2(db); ///close connection
        return NULL;
    }
    return db;
}

int db_init(const char *path, const char *migrations_dir, int busy_timeout_ms){
    if(snprintf(g_path, sizeof(g_path), "%s", path) >= (int)sizeof(g_path)){
        return DB_ERROR; // longer path than allowed
    }
    if(snprintf(g_migrations, sizeof(g_migrations), "%s", migrations_dir) >= (int)sizeof(g_migrations)){
        return DB_ERROR;
    }

    g_busy_ms = busy_timeout_ms;
    pthread_atfork(NULL, NULL, forget_inherited_connection);

    ensure_parent_dir(g_path);
    mode_t old = umask(077); //database file is created owner only

    sqlite3 *db = open_connection(); // the db handle
    umask(old);
    if (!db) return DB_ERROR;

    int rc = db_migrate(db, g_migrations); // apply migrations
    sqlite3_close_v2(db);    /* nothing may stay open across the fork() that follows */
    return rc == 0 ? DB_OK : DB_ERROR;
}

sqlite3 *db_get(void) {
    if (!tl_db) tl_db = open_connection();
    return tl_db;
}

void db_close_thread(void) {
    if (tl_db) { 
        sqlite3_close_v2(tl_db);
        tl_db = NULL; 
    }
}