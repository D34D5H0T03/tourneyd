#include "db/migrate.h"
#include "util/log.h"
#include "sqlite3.h"
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_MIGRATION_BYTES (1024 * 1024)

// extracts version number and ensures files follow a specific format, such as 001_init.sql
static int version_of(const char *name)
{
    size_t n = strlen(name);

    // checks if name is minimum 9 characters long, and if it ends in .sql
    if (n < 9 || strcmp(name + n - 4, ".sql") != 0)
        return -1;

    // this ensures that the filename starts with 3 digits and an underscore (e.g 001_)
    if (!isdigit((unsigned char)name[0]) || !isdigit((unsigned char)name[1]) ||
        !isdigit((unsigned char)name[2]) || name[3] != '_')
    {
        return -1;
    }

    // returns the version number
    return (name[0] - '0') * 100 + (name[1] - '0') * 10 + (name[2] - '0');
}

// checks if valid migration file
static int is_migration(const struct dirent *e){
     return version_of(e->d_name) >= 0; 
}

// reads the .sql file
static char *read_file(const char *path)
{
    FILE *f = fopen(path, "rb"); // read binary mode
    if (!f)
        return NULL;
    char *buf = malloc(MAX_MIGRATION_BYTES + 1); // the + 1 is for checking if the sql query exceeds max migration bytes and for appending the null byte                                               
    if (!buf)                                     
    { // malloc fail
        fclose(f);
        return NULL;
    }
    size_t n = fread(buf, 1, MAX_MIGRATION_BYTES + 1, f);
    fclose(f);
    if (n > MAX_MIGRATION_BYTES)
    {
        free(buf);
        return NULL;
    }
    buf[n] = '\0';
    return buf;
}

// executes the migration commands
static int apply(sqlite3 *db, int version, const char *name, const char *sql)
{
    char *err = NULL;
    char pragma[64];

    // starts the db transaction
    if (sqlite3_exec(db, "BEGIN IMMEDIATE", NULL, NULL, &err) != SQLITE_OK) goto fail;

    // executes the sql
    if (sqlite3_exec(db, sql, NULL, NULL, &err) != SQLITE_OK) goto rollback;
        

    // writes the version number
    snprintf(pragma, sizeof(pragma), "PRAGMA user_version = %d", version);

    // executes the pragma command
    if (sqlite3_exec(db, pragma, NULL, NULL, &err) != SQLITE_OK) goto rollback;
    // saves the changes
    if (sqlite3_exec(db, "COMMIT", NULL, NULL, &err) != SQLITE_OK) goto rollback;

    log_info("db: applied migration %s", name);
    return 0;

// rollback discards evry change made since BEGIN immediate
rollback:
    sqlite3_exec(db, "ROLLBACK", NULL, NULL, NULL);
fail:
    log_error("db: migration %s failed: %s", name, err ? err : sqlite3_errmsg(db));
    sqlite3_free(err);
    return -1;
}

int db_migrate(sqlite3 *db, const char *dir)
{
    // queries the database for the version
    int current = 0;
    sqlite3_stmt *st = NULL;
    if (sqlite3_prepare_v2(db, "PRAGMA user_version", -1, &st, NULL) != SQLITE_OK)
        return -1;
    if (sqlite3_step(st) == SQLITE_ROW)
        current = sqlite3_column_int(st, 0); // saves the version in current
    sqlite3_finalize(st);

    // scans the directiory and fileters using is_migration() and sorts them
    struct dirent **list = NULL;
    int n = scandir(dir, &list, is_migration, alphasort);
    if (n < 0)
    {
        log_error("db: cannot read migrations directory %s", dir);
        return -1;
    }

    // starts the loop to execute the migrations. if any migration fails the loop breaks
    int rc = 0, last = -1;
    for (int i = 0; i < n && rc == 0; i++)
    {
        int v = version_of(list[i]->d_name);
        if (v == last)
        { // duplicate versions
            log_error("db: two migrations share the number %03d", v);
            rc = -1;
            break;
        }
        // this moves the migration version until it catches up with the current db version
        last = v;
        if (v <= current)
            continue;

        // builds the path and reads the file
        char path[512];
        snprintf(path, sizeof(path), "%s/%s", dir, list[i]->d_name);
        char *sql = read_file(path);
        if (!sql)
        {
            log_error("db: cannot read %s", path);
            rc = -1;
            break;
        }

        // applies the db query
        rc = apply(db, v, list[i]->d_name, sql);
        free(sql);
    }
    // protection against db version being ahead of current version
    if (rc == 0 && current > last)
    {
        log_error("db: database is at version %d but this server only knows up to %d", current, last);
        rc = -1;
    }

    //cleanup and return
    for (int i = 0; i < n; i++)
        free(list[i]);
    free(list);
    
    return rc;
}