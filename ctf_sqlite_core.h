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

// db_stmt() / db_stmt_close(): a per-statement prepare cache.
//
// DB_SessionRecord (ctf_sqlite_unidb.c) already proved the idiom this
// generalizes: prepare a statement once, then for every row just
// sqlite3_reset() + sqlite3_clear_bindings() it and bind the next row's
// values, instead of paying sqlite3_prepare_v2's parse-and-plan cost again
// for every row. db_stmt() takes that from "one caller's private loop
// variable" to a helper any call site can use for a statement that gets
// re-run many times against the SAME connection over the connection's
// life: the caller keeps a file-scope `static sqlite3_stmt *` next to the
// statement's SQL text -- one cache variable per distinct statement -- and
// passes its address in every time it would otherwise have called
// sqlite3_prepare_v2().
//
// First call for a given cache pointer prepares the statement and stores
// the handle. Every later call resets and clears bindings on the cached
// handle and hands it back ready for fresh sqlite3_bind_* calls -- callers
// still do their own binding and sqlite3_step(), db_stmt() only owns
// "prepare vs. reuse." Returns NULL (and leaves *cache untouched) if
// prepare fails, so callers keep whatever "did this fail" check they had
// with sqlite3_prepare_v2() directly. Callers must NOT sqlite3_finalize() a
// handle db_stmt() gave them -- that would free the cached pointer while
// *cache still points at it, so the next call would return a dangling
// handle. Finalizing happens once, centrally, in db_stmt_close().
//
// A cached handle belongs to exactly one sqlite3 connection: the one it
// was prepared against. This is safe for ctf_sqlite_unidb.c, where dbconn
// is one connection held open for the life of the game module. It is NOT
// safe for ctf_sqlite_player.c: that backend opens a fresh sqlite3 handle
// per call (a different player's file, or the same player's file reopened
// later) and closes it before returning, so a statement cached across
// calls would outlive the connection it was prepared against -- the next
// call would hand back a handle bound to an already-closed database.
// ctf_sqlite_player.c deliberately keeps plain prepare-then-finalize per
// call for that reason; see the comment near the top of that file.
//
// db_stmt_close() finalizes a cached handle and clears the pointer, so
// nothing is left referencing a connection that is about to close. Every
// db_stmt() cache tied to a connection must be closed before that
// connection is -- see DB_Conn_Cleanup() in ctf_sqlite_unidb.c, which
// closes every cache the unified backend fills. This matters because
// DB_Conn_Start() can open a brand new connection after a prior
// DB_Conn_Cleanup(): a cache still holding a handle from the OLD
// connection would otherwise get reused against the new one.
sqlite3_stmt *db_stmt(sqlite3 *db, sqlite3_stmt **cache, const char *sql);
void db_stmt_close(sqlite3_stmt **cache);

#endif
