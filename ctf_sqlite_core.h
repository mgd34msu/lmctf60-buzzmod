#ifndef CTF_SQLITE_CORE_H
#define CTF_SQLITE_CORE_H

// Shared SQLite primitives for the two stats backends: ctf_sqlite_player.c
// (one database file per player) and ctf_sqlite_unidb.c (one shared
// players.db keyed on char_idx).
//
// Before this file existed, each backend carried its own copy of the same
// primitives -- an error printer, an exec wrapper, BEGIN/COMMIT/ROLLBACK
// helpers, and a PRAGMA table_info + ALTER TABLE migration probe -- and the
// copies had already drifted apart: ctf_sqlite_unidb.c turned on
// "PRAGMA journal_mode=WAL" and "PRAGMA synchronous=NORMAL" when it opened
// its connection, so a crash mid-write there loses at most the write in
// flight; ctf_sqlite_player.c never turned either pragma on, so the same
// crash could leave a per-player file's journal in a state SQLite has to
// recover on the next open.
//
// The one deliberate behavior change in this pass: both backends now open
// through db_open_tuned() below, which applies that same tuning on a
// successful open. That intentionally gives the per-player backend the
// WAL/NORMAL tuning it was missing. db_open_tuned() takes the open flags as
// a parameter rather than hardcoding SQLITE_OPEN_CREATE, because
// CTF_LoadPlayer relies on opening WITHOUT create-on-missing: a player who
// has never been seen before must not get an empty file created just from
// being looked up, and folding that flag choice into the helper would have
// broken that. On a failed open, db_open_tuned() does not close the handle
// or report anything itself -- it hands back whatever sqlite3_open_v2 gave
// it so the caller can log with db_error() (or not: a missing per-player
// file is normal, not an error) before closing.
//
// Known debt, left alone on purpose: the CREATE TABLE schema macros in the
// two backends still say almost the same thing twice. ctf_sqlite_unidb.c's
// tables carry a char_idx column the per-player tables don't need -- each
// per-player file holds exactly one row per table, so there is nothing to
// key rows on -- and merging the two schemas risked changing what either
// backend persists for a behavior-neutral cleanup pass. That merge did not
// happen here.

#include "sqlite3.h"

// Prints "sqlite error <code>: <message> (<what>)" via gi.dprintf. Safe to
// call with db == NULL (prints code -1, "no handle").
void db_error(sqlite3 *db, const char *what);

// Runs a statement that returns no rows. Returns true on success; on
// failure, prints the SQLite error text and the statement via gi.dprintf.
qboolean db_exec(sqlite3 *db, const char *sql);

qboolean db_begin(sqlite3 *db);
qboolean db_commit(sqlite3 *db);
qboolean db_rollback(sqlite3 *db);

// Opens `path` with `flags` (e.g. SQLITE_OPEN_READWRITE, optionally
// OR'd with SQLITE_OPEN_CREATE) and, only on success, applies the
// WAL/NORMAL tuning both backends now share. *out is always set to
// whatever sqlite3_open_v2 produced, including on failure -- the caller
// closes it and decides how (or whether) to report the failure, since a
// missing per-player file and a genuinely broken open are not the same
// thing to the two backends.
qboolean db_open_tuned(const char *path, int flags, sqlite3 **out);

// Adds `column` (declared `coltype`, default 0) to `table` if it is not
// already there, so a database written by an older build keeps working
// instead of failing every read. Table, column and coltype must all be
// compile-time constants, never player input -- they are pasted into DDL
// text because SQLite has no way to bind an identifier as a parameter.
void db_ensure_column(sqlite3 *db, const char *table, const char *column, const char *coltype);

#endif
