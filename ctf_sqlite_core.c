// ctf_sqlite_core.c -- primitives shared by the two stats backends.
//
// See ctf_sqlite_core.h for why this file exists and what it deliberately
// changes versus the two private copies it replaces.

#include <string.h>
#include <stdio.h>

#include "g_local.h"
#include "sqlite3.h"
#include "ctf_sqlite_core.h"

void db_error(sqlite3 *db, const char *what)
{
	gi.dprintf("sqlite error %d: %s (%s)\n",
		db ? sqlite3_errcode(db) : -1,
		db ? sqlite3_errmsg(db) : "no handle",
		what);
}

qboolean db_exec(sqlite3 *db, const char *sql)
{
	char *err = NULL;

	if (!db)
		return false;

	if (sqlite3_exec(db, sql, NULL, NULL, &err) != SQLITE_OK)
	{
		gi.dprintf("sqlite error: %s (%s)\n", err ? err : "unknown", sql);
		sqlite3_free(err);
		return false;
	}

	return true;
}

qboolean db_begin(sqlite3 *db)
{
	return db_exec(db, "BEGIN TRANSACTION;");
}

qboolean db_commit(sqlite3 *db)
{
	return db_exec(db, "COMMIT;");
}

qboolean db_rollback(sqlite3 *db)
{
	return db_exec(db, "ROLLBACK;");
}

qboolean db_open_tuned(const char *path, int flags, sqlite3 **out)
{
	sqlite3 *db = NULL;
	qboolean ok;

	ok = (sqlite3_open_v2(path, &db, flags, NULL) == SQLITE_OK);

	if (ok)
	{
		// keep a crash from truncating the file mid-write. Applied on every
		// tuned open, load or save, per-player or unified -- see the banner
		// in ctf_sqlite_core.h for why the per-player backend didn't have
		// this before.
		db_exec(db, "PRAGMA journal_mode=WAL;");
		db_exec(db, "PRAGMA synchronous=NORMAL;");
	}

	*out = db;
	return ok;
}

void db_ensure_column(sqlite3 *db, const char *table, const char *column, const char *coltype)
{
	sqlite3_stmt *res = NULL;
	char sql[256];
	qboolean found = false;

	if (!db)
		return;

	snprintf(sql, sizeof(sql), "PRAGMA table_info(%s);", table);
	if (sqlite3_prepare_v2(db, sql, -1, &res, NULL) != SQLITE_OK)
		return;

	while (sqlite3_step(res) == SQLITE_ROW)
	{
		const unsigned char *name = sqlite3_column_text(res, 1);

		if (name && strcmp((const char *)name, column) == 0)
		{
			found = true;
			break;
		}
	}
	sqlite3_finalize(res);

	if (found)
		return;

	snprintf(sql, sizeof(sql),
		"ALTER TABLE %s ADD COLUMN %s %s DEFAULT 0;", table, column, coltype);

	if (sqlite3_exec(db, sql, NULL, NULL, NULL) == SQLITE_OK)
		gi.dprintf("stats db: added %s.%s\n", table, column);
}
