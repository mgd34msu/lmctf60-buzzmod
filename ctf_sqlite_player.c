// ctf_sqlite_player.c -- per-player SQLite backend.
//
// Reconstructed 2026-07. The original was compiled on 2020-07-28 but never
// committed; only Debug/ctf_sqlite_player.obj survived. Every CREATE TABLE,
// INSERT, SELECT and UPDATE below is the exact statement text recovered from
// that object's string table, as are the function names and the two operator
// messages. The bodies are new.
//
// Each player gets their own database file, so every table holds exactly one
// row and the UPDATE statements carry no WHERE clause -- that is how the
// original was written and the schema still reflects it.
//
// Differences from the 2020 original, all deliberate:
//   - values are bound with sqlite3_bind_*, not pasted into the SQL with
//     sprintf. A player named  Bob" or "1"="1  can no longer corrupt a write.
//   - every sqlite3_stmt is finalised and every handle closed on all paths.
//   - writes run inside one transaction, so a crash mid-save cannot leave a
//     player half-written.

#include <string.h>
#include <stdio.h>

#include "g_local.h"
#include "sqlite3.h"
#include "ctf_file_io.h"
#include "ctf_sqlite_player.h"

#define SQL_CREATE_USERDATA \
	"CREATE TABLE [userdata] ([playername] CHAR(64), [member_since] CHAR(30), " \
	"[last_played] CHAR(30), [playtime_total] INTEGER,[playingtime] INTEGER)"
#define SQL_CREATE_GAMESTATS \
	"CREATE TABLE [game_stats] ([shots] INTEGER,   [shots_hit] INTEGER,   " \
	"[frags] INTEGER,   [fragged] INTEGER,   [num_sprees] INTEGER,   " \
	"[max_streak] INTEGER,   [suicides] INTEGER)"
#define SQL_CREATE_CTFSTATS \
	"CREATE TABLE [ctf_stats] ([flag_pickups] INTEGER,   [flag_captures] INTEGER,   " \
	"[flag_returns] INTEGER,   [flag_kills] INTEGER,   [offense_kills] INTEGER,   " \
	"[defense_kills] INTEGER,   [assists] INTEGER,   " \
	"[max_cap_streak] INTEGER,   [sweeps] INTEGER)"
#define SQL_CREATE_CHARDATA \
	"CREATE TABLE [character_data] ([adminlevel] INTEGER)"

#define SQL_UPDATE_UDATA \
	"UPDATE userdata SET playername=?, member_since=?, last_played=?, " \
	"playtime_total=?, playingtime=?;"
#define SQL_UPDATE_STATS \
	"UPDATE game_stats SET shots=?, shots_hit=?, frags=?, fragged=?, " \
	"num_sprees=?, max_streak=?, suicides=?;"
#define SQL_UPDATE_CTFSTATS \
	"UPDATE ctf_stats SET flag_pickups=?, flag_captures=?, flag_returns=?, " \
	"flag_kills=?, offense_kills=?, defense_kills=?, assists=?, " \
	"max_cap_streak=?, sweeps=?;"
#define SQL_UPDATE_CDATA \
	"UPDATE character_data SET adminlevel=?;"

static void ctf_sql_error(sqlite3 *db, const char *what)
{
	gi.dprintf("sqlite error %d: %s (%s)\n",
		db ? sqlite3_errcode(db) : -1,
		db ? sqlite3_errmsg(db) : "no handle",
		what);
}

// Runs a statement that returns no rows. Returns true on success.
static qboolean ctf_sql_exec(sqlite3 *db, const char *sql)
{
	char *err = NULL;

	if (sqlite3_exec(db, sql, NULL, NULL, &err) != SQLITE_OK)
	{
		gi.dprintf("sqlite error: %s (%s)\n", err ? err : "unknown", sql);
		sqlite3_free(err);
		return false;
	}

	return true;
}

static qboolean BeginTransaction(sqlite3 *db)
{
	return ctf_sql_exec(db, "BEGIN TRANSACTION;");
}

static qboolean CommitTransaction(sqlite3 *db)
{
	return ctf_sql_exec(db, "COMMIT;");
}

// Copies a text column into a fixed field without ever running past its end.
static void ctf_copy_text(char *dest, size_t destsize, const unsigned char *src)
{
	if (!dest || destsize == 0)
		return;

	if (!src)
	{
		dest[0] = '\0';
		return;
	}

	strncpy(dest, (const char *)src, destsize - 1);
	dest[destsize - 1] = '\0';
}

// Creates the four tables and their single base row. Called only when the
// database file did not already contain a userdata table.
static qboolean ctf_build_player_db(sqlite3 *db)
{
	static const char *schema[] = {
		SQL_CREATE_USERDATA,
		SQL_CREATE_GAMESTATS,
		SQL_CREATE_CTFSTATS,
		SQL_CREATE_CHARDATA,
		"INSERT INTO userdata VALUES (\"\",\"\",\"\",0,0)",
		"INSERT INTO game_stats VALUES (0,0,0,0,0,0,0)",
		"INSERT INTO ctf_stats VALUES (0,0,0,0,0,0,0,0,0)",
		"INSERT INTO character_data VALUES (0)",
		NULL
	};
	int i;

	gi.dprintf("SQLite: creating initial database [%d]... ", CTF_STATSDB_PERPLAYER);

	for (i = 0; schema[i]; i++)
	{
		if (!ctf_sql_exec(db, schema[i]))
			return false;
	}

	gi.dprintf("inserted bases.\n");
	return true;
}

// True when the database already carries our schema.
// Adds a column if the table does not already have it, so a per-player file
// written by an older build keeps working. Names here are compile-time
// constants, never player input.
static void ctf_ensure_column(sqlite3* db, const char* table, const char* column)
{
	sqlite3_stmt* res = NULL;
	char sql[256];
	qboolean found = false;

	if (!db)
		return;

	snprintf(sql, sizeof(sql), "PRAGMA table_info(%s);", table);
	if (sqlite3_prepare_v2(db, sql, -1, &res, NULL) != SQLITE_OK)
		return;

	while (sqlite3_step(res) == SQLITE_ROW)
	{
		const unsigned char* name = sqlite3_column_text(res, 1);

		if (name && strcmp((const char*)name, column) == 0)
		{
			found = true;
			break;
		}
	}
	sqlite3_finalize(res);

	if (found)
		return;

	snprintf(sql, sizeof(sql),
		"ALTER TABLE %s ADD COLUMN %s INTEGER DEFAULT 0;", table, column);
	sqlite3_exec(db, sql, NULL, NULL, NULL);
}

static qboolean ctf_db_has_schema(sqlite3 *db)
{
	sqlite3_stmt *res = NULL;
	qboolean found = false;

	if (sqlite3_prepare_v2(db,
			"SELECT name FROM sqlite_master WHERE type='table' AND name='userdata';",
			-1, &res, NULL) != SQLITE_OK)
	{
		ctf_sql_error(db, "schema probe");
		return false;
	}

	found = (sqlite3_step(res) == SQLITE_ROW);
	sqlite3_finalize(res);

	return found;
}

qboolean CTF_LoadPlayer(edict_t *player, const char *path)
{
	sqlite3 *db = NULL;
	sqlite3_stmt *res = NULL;
	playerstats_t *ps;

	if (!player || !player->client || !path)
		return false;

	ps = &player->client->ctfstats;

	// SQLITE_OPEN_READWRITE without _CREATE: a missing file is not an error
	// here, it just means we have never seen this player before.
	if (sqlite3_open_v2(path, &db, SQLITE_OPEN_READWRITE, NULL) != SQLITE_OK)
	{
		gi.dprintf("INFO: Player %s does not exist in the database.\n",
			player->client->pers.netname);
		sqlite3_close(db);
		return false;
	}

	if (!ctf_db_has_schema(db))
	{
		gi.dprintf("INFO: Player %s does not exist in the database.\n",
			player->client->pers.netname);
		sqlite3_close(db);
		return false;
	}

	// files written before capture streaks and sweeps existed
	ctf_ensure_column(db, "ctf_stats", "max_cap_streak");
	ctf_ensure_column(db, "ctf_stats", "sweeps");

	if (sqlite3_prepare_v2(db, "SELECT * FROM userdata", -1, &res, NULL) == SQLITE_OK &&
		sqlite3_step(res) == SQLITE_ROW)
	{
		ctf_copy_text(ps->player_name,  sizeof(ps->player_name),  sqlite3_column_text(res, 0));
		ctf_copy_text(ps->member_since, sizeof(ps->member_since), sqlite3_column_text(res, 1));
		ctf_copy_text(ps->last_played,  sizeof(ps->last_played),  sqlite3_column_text(res, 2));
		ps->total_playtime = sqlite3_column_int(res, 3);
		ps->playingtime    = sqlite3_column_int(res, 4);
	}
	sqlite3_finalize(res);
	res = NULL;

	if (sqlite3_prepare_v2(db, "SELECT * FROM game_stats", -1, &res, NULL) == SQLITE_OK &&
		sqlite3_step(res) == SQLITE_ROW)
	{
		ps->shots      = (unsigned long)sqlite3_column_int64(res, 0);
		ps->shots_hit  = (unsigned long)sqlite3_column_int64(res, 1);
		ps->frags      = (unsigned int)sqlite3_column_int(res, 2);
		ps->fragged    = (unsigned int)sqlite3_column_int(res, 3);
		ps->num_sprees = (unsigned int)sqlite3_column_int(res, 4);
		ps->max_streak = sqlite3_column_int(res, 5);
		ps->suicides   = sqlite3_column_int(res, 6);
	}
	sqlite3_finalize(res);
	res = NULL;

	if (sqlite3_prepare_v2(db, "SELECT * FROM ctf_stats", -1, &res, NULL) == SQLITE_OK &&
		sqlite3_step(res) == SQLITE_ROW)
	{
		ps->flag_pickups  = sqlite3_column_int(res, 0);
		ps->flag_captures = sqlite3_column_int(res, 1);
		ps->flag_returns  = sqlite3_column_int(res, 2);
		ps->flag_kills    = sqlite3_column_int(res, 3);
		ps->offense_kills = sqlite3_column_int(res, 4);
		ps->defense_kills = sqlite3_column_int(res, 5);
		ps->assists        = sqlite3_column_int(res, 6);
		ps->max_cap_streak = sqlite3_column_int(res, 7);
		ps->sweeps         = sqlite3_column_int(res, 8);
	}
	sqlite3_finalize(res);
	res = NULL;

	if (sqlite3_prepare_v2(db, "SELECT * FROM character_data", -1, &res, NULL) == SQLITE_OK &&
		sqlite3_step(res) == SQLITE_ROW)
	{
		ps->administrator = sqlite3_column_int(res, 0);
	}
	sqlite3_finalize(res);

	sqlite3_close(db);
	return true;
}

qboolean CTF_SavePlayer(edict_t *player, const char *path, const char *playername)
{
	sqlite3 *db = NULL;
	sqlite3_stmt *res = NULL;
	playerstats_t *ps;
	qboolean ok = false;

	if (!player || !player->client || !path)
		return false;

	ps = &player->client->ctfstats;

	if (sqlite3_open_v2(path, &db,
			SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) != SQLITE_OK)
	{
		ctf_sql_error(db, path);
		sqlite3_close(db);
		return false;
	}

	if (!ctf_db_has_schema(db) && !ctf_build_player_db(db))
	{
		sqlite3_close(db);
		return false;
	}

	ctf_ensure_column(db, "ctf_stats", "max_cap_streak");
	ctf_ensure_column(db, "ctf_stats", "sweeps");

	if (!BeginTransaction(db))
	{
		sqlite3_close(db);
		return false;
	}

	// userdata
	if (sqlite3_prepare_v2(db, SQL_UPDATE_UDATA, -1, &res, NULL) != SQLITE_OK)
		goto done;
	sqlite3_bind_text(res, 1, playername ? playername : "", -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(res, 2, ps->member_since, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(res, 3, ps->last_played,  -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(res, 4, ps->total_playtime);
	sqlite3_bind_int(res, 5, ps->playingtime);
	if (sqlite3_step(res) != SQLITE_DONE) goto done;
	sqlite3_finalize(res); res = NULL;

	// game_stats
	if (sqlite3_prepare_v2(db, SQL_UPDATE_STATS, -1, &res, NULL) != SQLITE_OK)
		goto done;
	sqlite3_bind_int64(res, 1, (sqlite3_int64)ps->shots);
	sqlite3_bind_int64(res, 2, (sqlite3_int64)ps->shots_hit);
	sqlite3_bind_int(res, 3, (int)ps->frags);
	sqlite3_bind_int(res, 4, (int)ps->fragged);
	sqlite3_bind_int(res, 5, (int)ps->num_sprees);
	sqlite3_bind_int(res, 6, ps->max_streak);
	sqlite3_bind_int(res, 7, ps->suicides);
	if (sqlite3_step(res) != SQLITE_DONE) goto done;
	sqlite3_finalize(res); res = NULL;

	// ctf_stats
	if (sqlite3_prepare_v2(db, SQL_UPDATE_CTFSTATS, -1, &res, NULL) != SQLITE_OK)
		goto done;
	sqlite3_bind_int(res, 1, ps->flag_pickups);
	sqlite3_bind_int(res, 2, ps->flag_captures);
	sqlite3_bind_int(res, 3, ps->flag_returns);
	sqlite3_bind_int(res, 4, ps->flag_kills);
	sqlite3_bind_int(res, 5, ps->offense_kills);
	sqlite3_bind_int(res, 6, ps->defense_kills);
	sqlite3_bind_int(res, 7, ps->assists);
	sqlite3_bind_int(res, 8, ps->max_cap_streak);
	sqlite3_bind_int(res, 9, ps->sweeps);
	if (sqlite3_step(res) != SQLITE_DONE) goto done;
	sqlite3_finalize(res); res = NULL;

	// character_data
	if (sqlite3_prepare_v2(db, SQL_UPDATE_CDATA, -1, &res, NULL) != SQLITE_OK)
		goto done;
	sqlite3_bind_int(res, 1, ps->administrator);
	if (sqlite3_step(res) != SQLITE_DONE) goto done;
	sqlite3_finalize(res); res = NULL;

	ok = CommitTransaction(db);

done:
	if (!ok)
	{
		ctf_sql_error(db, path);
		ctf_sql_exec(db, "ROLLBACK;");
	}
	if (res)
		sqlite3_finalize(res);
	sqlite3_close(db);

	return ok;
}
