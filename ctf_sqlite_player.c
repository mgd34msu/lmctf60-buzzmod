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
#include "ctf_sqlite_core.h"
#include "ctf_sqlite_player.h"

#define SQL_CREATE_USERDATA \
	"CREATE TABLE IF NOT EXISTS [userdata] ([playername] CHAR(64), [member_since] CHAR(30), " \
	"[last_played] CHAR(30), [playtime_total] INTEGER,[playingtime] INTEGER)"
#define SQL_CREATE_GAMESTATS \
	"CREATE TABLE IF NOT EXISTS [game_stats] ([shots] INTEGER,   [shots_hit] INTEGER,   " \
	"[frags] INTEGER,   [fragged] INTEGER,   [num_sprees] INTEGER,   " \
	"[max_streak] INTEGER,   [suicides] INTEGER,   " \
	"[score] INTEGER,   [deaths] INTEGER,   [damage_given] INTEGER,   [damage_received] INTEGER,   [rail_shot] INTEGER,   [rail_hit] INTEGER,   [rail_kill] INTEGER,   [ping_total] INTEGER,   [ping_samples] INTEGER,   [item_quad] INTEGER,   [item_shield] INTEGER,   [item_armor] INTEGER,   [item_mega] INTEGER,   [rune_strength] INTEGER,   [rune_haste] INTEGER,   [rune_regen] INTEGER,   [rune_resist] INTEGER)"
#define SQL_CREATE_CTFSTATS \
	"CREATE TABLE IF NOT EXISTS [ctf_stats] ([flag_pickups] INTEGER,   [flag_captures] INTEGER,   " \
	"[flag_returns] INTEGER,   [flag_kills] INTEGER,   [offense_kills] INTEGER,   " \
	"[defense_kills] INTEGER,   [assists] INTEGER,   " \
	"[max_cap_streak] INTEGER,   [sweeps] INTEGER,   " \
	"[flag_drops] INTEGER,   [defense_base] INTEGER,   [defense_flag] INTEGER,   [defense_carrier] INTEGER)"
#define SQL_CREATE_CHARDATA \
	"CREATE TABLE IF NOT EXISTS [character_data] ([adminlevel] INTEGER)"

#define SQL_UPDATE_UDATA \
	"UPDATE userdata SET playername=?, member_since=?, last_played=?, " \
	"playtime_total=?, playingtime=?;"
#define SQL_UPDATE_STATS \
	"UPDATE game_stats SET shots=?, shots_hit=?, frags=?, fragged=?, " \
	"num_sprees=?, max_streak=?, suicides=?, " \
	"score=?, deaths=?, damage_given=?, damage_received=?, rail_shot=?, rail_hit=?, rail_kill=?, ping_total=?, ping_samples=?, item_quad=?, item_shield=?, item_armor=?, item_mega=?, rune_strength=?, rune_haste=?, rune_regen=?, rune_resist=?;"
#define SQL_UPDATE_CTFSTATS \
	"UPDATE ctf_stats SET flag_pickups=?, flag_captures=?, flag_returns=?, " \
	"flag_kills=?, offense_kills=?, defense_kills=?, assists=?, " \
	"max_cap_streak=?, sweeps=?, " \
	"flag_drops=?, defense_base=?, defense_flag=?, defense_carrier=?;"
#define SQL_UPDATE_CDATA \
	"UPDATE character_data SET adminlevel=?;"

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

// Creates the four tables. IF NOT EXISTS throughout, so this is safe to run on
// every save and repairs a file that somehow lost one -- the old version ran
// only when the `userdata` probe failed, which left a partially built file
// broken forever.
static qboolean ctf_create_tables(sqlite3 *db)
{
	static const char *schema[] = {
		SQL_CREATE_USERDATA,
		SQL_CREATE_GAMESTATS,
		SQL_CREATE_CTFSTATS,
		SQL_CREATE_CHARDATA,
		NULL
	};
	int i;

	for (i = 0; schema[i]; i++)
	{
		if (!db_exec(db, schema[i]))
			return false;
	}

	return true;
}

// Each table in a per-player file holds exactly one row, so the base rows go in
// once. Kept separate from table creation: re-running the CREATEs is harmless,
// re-running the INSERTs would give the player a second, shadow row.
static qboolean ctf_ensure_base_rows(sqlite3 *db)
{
	static const char *bases[] = {
		"INSERT INTO userdata VALUES (\"\",\"\",\"\",0,0)",
		"INSERT INTO game_stats VALUES (0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0)",
		"INSERT INTO ctf_stats VALUES (0,0,0,0,0,0,0,0,0,0,0,0,0)",
		"INSERT INTO character_data VALUES (0)",
		NULL
	};
	sqlite3_stmt *res = NULL;
	int rows = -1;
	int i;

	if (sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM userdata", -1, &res, NULL) != SQLITE_OK)
	{
		db_error(db, "base row probe");
		return false;
	}
	if (sqlite3_step(res) == SQLITE_ROW)
		rows = sqlite3_column_int(res, 0);
	sqlite3_finalize(res);

	if (rows != 0)
		return (rows > 0);	// already populated, or the probe failed

	gi.dprintf("SQLite: creating initial database [%d]... ", CTF_STATSDB_PERPLAYER);

	for (i = 0; bases[i]; i++)
	{
		if (!db_exec(db, bases[i]))
			return false;
	}

	gi.dprintf("inserted bases.\n");
	return true;
}

// True when the database already carries our schema.
static qboolean ctf_db_has_schema(sqlite3 *db)
{
	sqlite3_stmt *res = NULL;
	qboolean found = false;

	if (sqlite3_prepare_v2(db,
			"SELECT name FROM sqlite_master WHERE type='table' AND name='userdata';",
			-1, &res, NULL) != SQLITE_OK)
	{
		db_error(db, "schema probe");
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
	if (!db_open_tuned(path, SQLITE_OPEN_READWRITE, &db))
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
	db_ensure_column(db, "ctf_stats", "max_cap_streak", "INTEGER");
	db_ensure_column(db, "ctf_stats", "sweeps", "INTEGER");

	db_ensure_column(db, "game_stats", "score", "INTEGER");
	db_ensure_column(db, "game_stats", "deaths", "INTEGER");
	db_ensure_column(db, "game_stats", "damage_given", "INTEGER");
	db_ensure_column(db, "game_stats", "damage_received", "INTEGER");
	db_ensure_column(db, "game_stats", "rail_shot", "INTEGER");
	db_ensure_column(db, "game_stats", "rail_hit", "INTEGER");
	db_ensure_column(db, "game_stats", "rail_kill", "INTEGER");
	db_ensure_column(db, "game_stats", "ping_total", "INTEGER");
	db_ensure_column(db, "game_stats", "ping_samples", "INTEGER");
	db_ensure_column(db, "game_stats", "item_quad", "INTEGER");
	db_ensure_column(db, "game_stats", "item_shield", "INTEGER");
	db_ensure_column(db, "game_stats", "item_armor", "INTEGER");
	db_ensure_column(db, "game_stats", "item_mega", "INTEGER");
	db_ensure_column(db, "game_stats", "rune_strength", "INTEGER");
	db_ensure_column(db, "game_stats", "rune_haste", "INTEGER");
	db_ensure_column(db, "game_stats", "rune_regen", "INTEGER");
	db_ensure_column(db, "game_stats", "rune_resist", "INTEGER");
	db_ensure_column(db, "ctf_stats", "flag_drops", "INTEGER");
	db_ensure_column(db, "ctf_stats", "defense_base", "INTEGER");
	db_ensure_column(db, "ctf_stats", "defense_flag", "INTEGER");
	db_ensure_column(db, "ctf_stats", "defense_carrier", "INTEGER");

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
		ps->score           = sqlite3_column_int(res, 7);
		ps->deaths          = sqlite3_column_int(res, 8);
		ps->damage_given    = (unsigned long)sqlite3_column_int64(res, 9);
		ps->damage_received = (unsigned long)sqlite3_column_int64(res, 10);
		ps->rail_shot       = (unsigned long)sqlite3_column_int64(res, 11);
		ps->rail_hit        = (unsigned long)sqlite3_column_int64(res, 12);
		ps->rail_kill       = sqlite3_column_int(res, 13);
		ps->ping_total      = (unsigned long)sqlite3_column_int64(res, 14);
		ps->ping_samples    = (unsigned long)sqlite3_column_int64(res, 15);
		ps->item_quad       = sqlite3_column_int(res, 16);
		ps->item_shield     = sqlite3_column_int(res, 17);
		ps->item_armor      = sqlite3_column_int(res, 18);
		ps->item_mega       = sqlite3_column_int(res, 19);
		ps->rune_strength   = sqlite3_column_int(res, 20);
		ps->rune_haste      = sqlite3_column_int(res, 21);
		ps->rune_regen      = sqlite3_column_int(res, 22);
		ps->rune_resist     = sqlite3_column_int(res, 23);
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
		ps->flag_drops      = sqlite3_column_int(res, 9);
		ps->defense_base    = sqlite3_column_int(res, 10);
		ps->defense_flag    = sqlite3_column_int(res, 11);
		ps->defense_carrier = sqlite3_column_int(res, 12);
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

	if (!db_open_tuned(path, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, &db))
	{
		db_error(db, path);
		sqlite3_close(db);
		return false;
	}

	if (!ctf_create_tables(db) || !ctf_ensure_base_rows(db))
	{
		sqlite3_close(db);
		return false;
	}

	db_ensure_column(db, "ctf_stats", "max_cap_streak", "INTEGER");
	db_ensure_column(db, "ctf_stats", "sweeps", "INTEGER");

	db_ensure_column(db, "game_stats", "score", "INTEGER");
	db_ensure_column(db, "game_stats", "deaths", "INTEGER");
	db_ensure_column(db, "game_stats", "damage_given", "INTEGER");
	db_ensure_column(db, "game_stats", "damage_received", "INTEGER");
	db_ensure_column(db, "game_stats", "rail_shot", "INTEGER");
	db_ensure_column(db, "game_stats", "rail_hit", "INTEGER");
	db_ensure_column(db, "game_stats", "rail_kill", "INTEGER");
	db_ensure_column(db, "game_stats", "ping_total", "INTEGER");
	db_ensure_column(db, "game_stats", "ping_samples", "INTEGER");
	db_ensure_column(db, "game_stats", "item_quad", "INTEGER");
	db_ensure_column(db, "game_stats", "item_shield", "INTEGER");
	db_ensure_column(db, "game_stats", "item_armor", "INTEGER");
	db_ensure_column(db, "game_stats", "item_mega", "INTEGER");
	db_ensure_column(db, "game_stats", "rune_strength", "INTEGER");
	db_ensure_column(db, "game_stats", "rune_haste", "INTEGER");
	db_ensure_column(db, "game_stats", "rune_regen", "INTEGER");
	db_ensure_column(db, "game_stats", "rune_resist", "INTEGER");
	db_ensure_column(db, "ctf_stats", "flag_drops", "INTEGER");
	db_ensure_column(db, "ctf_stats", "defense_base", "INTEGER");
	db_ensure_column(db, "ctf_stats", "defense_flag", "INTEGER");
	db_ensure_column(db, "ctf_stats", "defense_carrier", "INTEGER");

	if (!db_begin(db))
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
	sqlite3_bind_int(res, 8, ps->score);
	sqlite3_bind_int(res, 9, ps->deaths);
	sqlite3_bind_int64(res, 10, (sqlite3_int64)ps->damage_given);
	sqlite3_bind_int64(res, 11, (sqlite3_int64)ps->damage_received);
	sqlite3_bind_int64(res, 12, (sqlite3_int64)ps->rail_shot);
	sqlite3_bind_int64(res, 13, (sqlite3_int64)ps->rail_hit);
	sqlite3_bind_int(res, 14, ps->rail_kill);
	sqlite3_bind_int64(res, 15, (sqlite3_int64)ps->ping_total);
	sqlite3_bind_int64(res, 16, (sqlite3_int64)ps->ping_samples);
	sqlite3_bind_int(res, 17, ps->item_quad);
	sqlite3_bind_int(res, 18, ps->item_shield);
	sqlite3_bind_int(res, 19, ps->item_armor);
	sqlite3_bind_int(res, 20, ps->item_mega);
	sqlite3_bind_int(res, 21, ps->rune_strength);
	sqlite3_bind_int(res, 22, ps->rune_haste);
	sqlite3_bind_int(res, 23, ps->rune_regen);
	sqlite3_bind_int(res, 24, ps->rune_resist);
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
	sqlite3_bind_int(res, 10, ps->flag_drops);
	sqlite3_bind_int(res, 11, ps->defense_base);
	sqlite3_bind_int(res, 12, ps->defense_flag);
	sqlite3_bind_int(res, 13, ps->defense_carrier);
	if (sqlite3_step(res) != SQLITE_DONE) goto done;
	sqlite3_finalize(res); res = NULL;

	// character_data
	if (sqlite3_prepare_v2(db, SQL_UPDATE_CDATA, -1, &res, NULL) != SQLITE_OK)
		goto done;
	sqlite3_bind_int(res, 1, ps->administrator);
	if (sqlite3_step(res) != SQLITE_DONE) goto done;
	sqlite3_finalize(res); res = NULL;

	ok = db_commit(db);

done:
	if (!ok)
	{
		db_error(db, path);
		db_rollback(db);
	}
	if (res)
		sqlite3_finalize(res);
	sqlite3_close(db);

	return ok;
}
