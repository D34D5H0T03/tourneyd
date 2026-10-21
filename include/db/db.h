#ifndef DB_H
#define DB_H

struct sqlite3; // handlers never see the full type

#define DB_OK            0
#define DB_NOT_FOUND     1
#define DB_CONFLICT      2
#define DB_RATE_LIMITED  3
#define DB_INVALID       4 // a value broke a check / not null / foreign key constraint
#define DB_ERROR        (-1)

/* Parent process, before any fork or thread: stores settings, opens connection,
 *  applies pending migrations, then closes it. 0 or DB_ERROR. */
int db_init(const char *path, const char *migrations_dir, int busy_timeout_ms);

/* This thread's connection, opened on first use. NULL on failure. */
struct sqlite3 *db_get(void);

/* Closes thread's connection. Required for threads that exit, optional for fork children. */
void db_close_thread(void);

#endif