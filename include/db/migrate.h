#ifndef MIGRATE_H
#define MIGRATE_H

struct sqlite3;
int db_migrate(struct sqlite3 *db, const char *dir);

#endif 