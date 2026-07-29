// ctf_sqlite_unidb.c -- unified SQLite backend, one players.db for the server.
//
// Reconstructed 2026-07. The original was compiled on 2020-07-28 but never
// committed; only Debug/ctf_sqlite_unidb.obj survived. The schema, the
// "%s/players.db" path, the "sqlite error %d: %s" and "SQLite (single mode):
// creating initial data for player id %d.." messages and the function names
// are all recovered from that object's string table. The bodies are new.
//
// One database, one connection held open for the life of the game module, and
// every table keyed on char_idx.
//
// Differences from the 2020 original, all deliberate:
//   - the player name is bound, not pasted into the SELECT with sprintf.
//   - DB_NewID uses MAX(char_idx)+1 rather than COUNT(*). COUNT(*) hands out an
//     id that already belongs to somebody as soon as one row is ever deleted.
//   - a saved player who is not yet in the database gets a row instead of
//     silently losing the session.

#include <string.h>

#include "g_local.h"
#include "sqlite3.h"
#include "ctf_file_io.h"
#include "ctf_sqlite_unidb.h"

#define DB_CREATEUDATA \
	"CREATE TABLE [userdata] ([char_idx] INTEGER, [playername] CHAR(64), " \
	"[member_since] CHAR(30), [last_played] CHAR(30), [playtime_total] INTEGER,[playingtime] INTEGER)"
#define DB_CREATESTATS \
	"CREATE TABLE [game_stats] ([char_idx] INTEGER,  [shots] INTEGER,   " \
	"[shots_hit] INTEGER,   [frags] INTEGER,   [fragged] INTEGER,   " \
	"[num_sprees] INTEGER,   [max_streak] INTEGER,   [suicides] INTEGER)"
#define DB_CREATECTFSTATS \
	"CREATE TABLE [ctf_stats] ([char_idx] INTEGER,  [flag_pickups] INTEGER,   " \
	"[flag_captures] INTEGER,   [flag_returns] INTEGER,   [flag_kills] INTEGER,   " \
	"[offense_kills] INTEGER,   [defense_kills] INTEGER,   [assists] INTEGER,   " \
	"[max_cap_streak] INTEGER,   [sweeps] INTEGER)"
#define DB_CREATECDATA \
	"CREATE TABLE [character_data] ([char_idx] INTEGER,  [adminlevel] INTEGER)"

#define DB_UPDATEUDATA \
	"UPDATE userdata SET playername=?, member_since=?, last_played=?, " \
	"playtime_total=?, playingtime=? WHERE char_idx=?;"
#define DB_UPDATESTATS \
	"UPDATE game_stats SET shots=?, shots_hit=?, frags=?, fragged=?, " \
	"num_sprees=?, max_streak=?, suicides=? WHERE char_idx=?;"
#define DB_UPDATECTFSTATS \
	"UPDATE ctf_stats SET flag_pickups=?, flag_captures=?, flag_returns=?, " \
	"flag_kills=?, offense_kills=?, defense_kills=?, assists=?, " \
	"max_cap_streak=?, sweeps=? WHERE char_idx=?;"
#define DB_UPDATECDATA \
	"UPDATE character_data SET adminlevel=? WHERE char_idx=?;"

static sqlite3 *dbconn = NULL;
static char     dbname[CTF_MAX_DBPATH];

static void db_error(const char *what)
{
	gi.dprintf("sqlite error %d: %s (%s)\n",
		dbconn ? sqlite3_errcode(dbconn) : -1,
		dbconn ? sqlite3_errmsg(dbconn) : "no handle",
		what);
}

static qboolean db_exec(const char *sql)
{
	char *err = NULL;

	if (!dbconn)
		return false;

	if (sqlite3_exec(dbconn, sql, NULL, NULL, &err) != SQLITE_OK)
	{
		gi.dprintf("sqlite error: %s (%s)\n", err ? err : "unknown", sql);
		sqlite3_free(err);
		return false;
	}

	return true;
}

static void db_copy_text(char *dest, size_t destsize, const unsigned char *src)
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

// Adds a column if the table does not already have it, so a database written
// by an older build keeps working instead of failing every read. Table and
// column names here are compile-time constants, never player input.
static void db_ensure_column(sqlite3* db, const char* table, const char* column)
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

	if (sqlite3_exec(db, sql, NULL, NULL, NULL) == SQLITE_OK)
		gi.dprintf("stats db: added %s.%s\n", table, column);
}

static qboolean db_has_schema(void)
{
	sqlite3_stmt *res = NULL;
	qboolean found;

	if (sqlite3_prepare_v2(dbconn,
			"SELECT name FROM sqlite_master WHERE type='table' AND name='userdata';",
			-1, &res, NULL) != SQLITE_OK)
	{
		db_error("schema probe");
		return false;
	}

	found = (sqlite3_step(res) == SQLITE_ROW);
	sqlite3_finalize(res);

	return found;
}

static qboolean build_db(void)
{
	static const char *schema[] = {
		DB_CREATEUDATA,
		DB_CREATESTATS,
		DB_CREATECTFSTATS,
		DB_CREATECDATA,
		NULL
	};
	int i;

	for (i = 0; schema[i]; i++)
	{
		if (!db_exec(schema[i]))
			return false;
	}

	return true;
}

qboolean DB_Conn_Start(void)
{
	if (dbconn)
		return true;

	if (!ctf_get_unified_db_path(dbname, sizeof(dbname)))
		return false;

	if (sqlite3_open_v2(dbname, &dbconn,
			SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) != SQLITE_OK)
	{
		db_error(dbname);
		sqlite3_close(dbconn);
		dbconn = NULL;
		return false;
	}

	// keep a crash from truncating the file mid-write
	db_exec("PRAGMA journal_mode=WAL;");
	db_exec("PRAGMA synchronous=NORMAL;");

	if (!db_has_schema() && !build_db())
	{
		sqlite3_close(dbconn);
		dbconn = NULL;
		return false;
	}

	// databases written before capture streaks and sweeps existed
	db_ensure_column(dbconn, "ctf_stats", "max_cap_streak");
	db_ensure_column(dbconn, "ctf_stats", "sweeps");

	return true;
}

void DB_Conn_Cleanup(void)
{
	if (!dbconn)
		return;

	sqlite3_close(dbconn);
	dbconn = NULL;
	dbname[0] = '\0';
}

int DB_GetID(const char *playername)
{
	sqlite3_stmt *res = NULL;
	int id = -1;

	if (!dbconn || !playername || playername[0] == '\0')
		return -1;

	if (sqlite3_prepare_v2(dbconn,
			"SELECT char_idx FROM userdata WHERE playername=?",
			-1, &res, NULL) != SQLITE_OK)
	{
		db_error("DB_GetID");
		return -1;
	}

	sqlite3_bind_text(res, 1, playername, -1, SQLITE_TRANSIENT);

	if (sqlite3_step(res) == SQLITE_ROW)
		id = sqlite3_column_int(res, 0);

	sqlite3_finalize(res);
	return id;
}

int DB_NewID(const char *playername)
{
	sqlite3_stmt *res = NULL;
	int id = -1;

	if (!dbconn)
		return -1;

	// MAX+1, not COUNT(*): COUNT reissues a live id the moment a row is deleted
	if (sqlite3_prepare_v2(dbconn,
			"SELECT IFNULL(MAX(char_idx), -1) + 1 FROM userdata",
			-1, &res, NULL) != SQLITE_OK)
	{
		db_error("DB_NewID");
		return -1;
	}

	if (sqlite3_step(res) == SQLITE_ROW)
		id = sqlite3_column_int(res, 0);

	sqlite3_finalize(res);
	res = NULL;

	if (id < 0)
		return -1;

	gi.dprintf("SQLite (single mode): creating initial data for player id %d..", id);

	if (!db_exec("BEGIN TRANSACTION;"))
		return -1;

	if (sqlite3_prepare_v2(dbconn,
			"INSERT INTO userdata VALUES (?,?,\"\",\"\",0,0)",
			-1, &res, NULL) != SQLITE_OK)
	{
		db_error("DB_NewID insert");
		db_exec("ROLLBACK;");
		return -1;
	}
	sqlite3_bind_int(res, 1, id);
	sqlite3_bind_text(res, 2, playername ? playername : "", -1, SQLITE_TRANSIENT);
	if (sqlite3_step(res) != SQLITE_DONE)
	{
		db_error("DB_NewID insert");
		sqlite3_finalize(res);
		db_exec("ROLLBACK;");
		return -1;
	}
	sqlite3_finalize(res);

	if (!db_exec(va("INSERT INTO game_stats VALUES (%d,0,0,0,0,0,0,0)", id)) ||
		!db_exec(va("INSERT INTO ctf_stats VALUES (%d,0,0,0,0,0,0,0,0,0)", id)) ||
		!db_exec(va("INSERT INTO character_data VALUES (%d,0)", id)))
	{
		db_exec("ROLLBACK;");
		return -1;
	}

	if (!db_exec("COMMIT;"))
		return -1;

	gi.dprintf(" inserted bases.\n");
	return id;
}

qboolean DB_LoadPlayer(edict_t *player)
{
	sqlite3_stmt *res = NULL;
	playerstats_t *ps;
	int id;

	if (!player || !player->client)
		return false;

	if (!dbconn && !DB_Conn_Start())
		return false;

	ps = &player->client->ctfstats;
	id = DB_GetID(player->client->pers.netname);

	if (id < 0)
	{
		gi.dprintf("INFO: Player %s does not exist in the database.\n",
			player->client->pers.netname);
		return false;
	}

	if (sqlite3_prepare_v2(dbconn, "SELECT * FROM userdata WHERE char_idx=?",
			-1, &res, NULL) == SQLITE_OK)
	{
		sqlite3_bind_int(res, 1, id);
		if (sqlite3_step(res) == SQLITE_ROW)
		{
			db_copy_text(ps->player_name,  sizeof(ps->player_name),  sqlite3_column_text(res, 1));
			db_copy_text(ps->member_since, sizeof(ps->member_since), sqlite3_column_text(res, 2));
			db_copy_text(ps->last_played,  sizeof(ps->last_played),  sqlite3_column_text(res, 3));
			ps->total_playtime = sqlite3_column_int(res, 4);
			ps->playingtime    = sqlite3_column_int(res, 5);
		}
	}
	sqlite3_finalize(res); res = NULL;

	if (sqlite3_prepare_v2(dbconn, "SELECT * FROM game_stats WHERE char_idx=?",
			-1, &res, NULL) == SQLITE_OK)
	{
		sqlite3_bind_int(res, 1, id);
		if (sqlite3_step(res) == SQLITE_ROW)
		{
			ps->shots      = (unsigned long)sqlite3_column_int64(res, 1);
			ps->shots_hit  = (unsigned long)sqlite3_column_int64(res, 2);
			ps->frags      = (unsigned int)sqlite3_column_int(res, 3);
			ps->fragged    = (unsigned int)sqlite3_column_int(res, 4);
			ps->num_sprees = (unsigned int)sqlite3_column_int(res, 5);
			ps->max_streak = sqlite3_column_int(res, 6);
			ps->suicides   = sqlite3_column_int(res, 7);
		}
	}
	sqlite3_finalize(res); res = NULL;

	if (sqlite3_prepare_v2(dbconn, "SELECT * FROM ctf_stats WHERE char_idx=?",
			-1, &res, NULL) == SQLITE_OK)
	{
		sqlite3_bind_int(res, 1, id);
		if (sqlite3_step(res) == SQLITE_ROW)
		{
			ps->flag_pickups  = sqlite3_column_int(res, 1);
			ps->flag_captures = sqlite3_column_int(res, 2);
			ps->flag_returns  = sqlite3_column_int(res, 3);
			ps->flag_kills    = sqlite3_column_int(res, 4);
			ps->offense_kills = sqlite3_column_int(res, 5);
			ps->defense_kills = sqlite3_column_int(res, 6);
			ps->assists        = sqlite3_column_int(res, 7);
			ps->max_cap_streak = sqlite3_column_int(res, 8);
			ps->sweeps         = sqlite3_column_int(res, 9);
		}
	}
	sqlite3_finalize(res); res = NULL;

	if (sqlite3_prepare_v2(dbconn, "SELECT * FROM character_data WHERE char_idx=?",
			-1, &res, NULL) == SQLITE_OK)
	{
		sqlite3_bind_int(res, 1, id);
		if (sqlite3_step(res) == SQLITE_ROW)
			ps->administrator = sqlite3_column_int(res, 1);
	}
	sqlite3_finalize(res);

	return true;
}

qboolean DB_SavePlayer(edict_t *player)
{
	sqlite3_stmt *res = NULL;
	playerstats_t *ps;
	const char *name;
	int id;
	qboolean ok = false;

	if (!player || !player->client)
		return false;

	if (!dbconn && !DB_Conn_Start())
		return false;

	ps = &player->client->ctfstats;
	name = player->client->pers.netname;

	id = DB_GetID(name);
	if (id < 0)
	{
		// first time we have seen this name -- give them a row rather than
		// dropping the session on the floor, which is what the 2020 code did.
		id = DB_NewID(name);
		if (id < 0)
			return false;
	}

	if (!db_exec("BEGIN TRANSACTION;"))
		return false;

	if (sqlite3_prepare_v2(dbconn, DB_UPDATEUDATA, -1, &res, NULL) != SQLITE_OK)
		goto done;
	sqlite3_bind_text(res, 1, name ? name : "", -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(res, 2, ps->member_since, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(res, 3, ps->last_played,  -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(res, 4, ps->total_playtime);
	sqlite3_bind_int(res, 5, ps->playingtime);
	sqlite3_bind_int(res, 6, id);
	if (sqlite3_step(res) != SQLITE_DONE) goto done;
	sqlite3_finalize(res); res = NULL;

	if (sqlite3_prepare_v2(dbconn, DB_UPDATESTATS, -1, &res, NULL) != SQLITE_OK)
		goto done;
	sqlite3_bind_int64(res, 1, (sqlite3_int64)ps->shots);
	sqlite3_bind_int64(res, 2, (sqlite3_int64)ps->shots_hit);
	sqlite3_bind_int(res, 3, (int)ps->frags);
	sqlite3_bind_int(res, 4, (int)ps->fragged);
	sqlite3_bind_int(res, 5, (int)ps->num_sprees);
	sqlite3_bind_int(res, 6, ps->max_streak);
	sqlite3_bind_int(res, 7, ps->suicides);
	sqlite3_bind_int(res, 8, id);
	if (sqlite3_step(res) != SQLITE_DONE) goto done;
	sqlite3_finalize(res); res = NULL;

	if (sqlite3_prepare_v2(dbconn, DB_UPDATECTFSTATS, -1, &res, NULL) != SQLITE_OK)
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
	sqlite3_bind_int(res, 10, id);
	if (sqlite3_step(res) != SQLITE_DONE) goto done;
	sqlite3_finalize(res); res = NULL;

	if (sqlite3_prepare_v2(dbconn, DB_UPDATECDATA, -1, &res, NULL) != SQLITE_OK)
		goto done;
	sqlite3_bind_int(res, 1, ps->administrator);
	sqlite3_bind_int(res, 2, id);
	if (sqlite3_step(res) != SQLITE_DONE) goto done;
	sqlite3_finalize(res); res = NULL;

	ok = db_exec("COMMIT;");

done:
	if (!ok)
	{
		db_error("DB_SavePlayer");
		db_exec("ROLLBACK;");
	}
	if (res)
		sqlite3_finalize(res);

	return ok;
}
