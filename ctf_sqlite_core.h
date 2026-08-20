#ifndef CTF_SQLITE_CORE_H
#define CTF_SQLITE_CORE_H

// Shared SQLite primitives for per-player and unified stats backends.

#include "sqlite3.h"

// Logs an SQLite error. Accepts a NULL handle.
void db_error(sqlite3 *db, const char *what);

// Runs a statement that returns no rows.
qboolean db_exec(sqlite3 *db, const char *sql);

qboolean db_begin(sqlite3 *db);
qboolean db_commit(sqlite3 *db);
qboolean db_rollback(sqlite3 *db);

// Opens a database and applies WAL/NORMAL tuning. Always assigns *out.
qboolean db_open_tuned(const char *path, int flags, sqlite3 **out);

// Adds a missing column. All identifier arguments must be trusted constants.
void db_ensure_column(sqlite3 *db, const char *table, const char *column, const char *coltype);

// Prepare or reset one statement cached for the lifetime of one connection.
// Finalize every cached handle before closing its connection.
sqlite3_stmt *db_stmt(sqlite3 *db, sqlite3_stmt **cache, const char *sql);
void db_stmt_close(sqlite3_stmt **cache);

#endif
