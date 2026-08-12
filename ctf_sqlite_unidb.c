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
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>

#include "g_local.h"
#include "sqlite3.h"
#include "ctf_file_io.h"
#include "ctf_sqlite_core.h"
#include "ctf_sqlite_unidb.h"
#include "slipgate/sg_cvars.h"

#define DB_CREATEUDATA \
	"CREATE TABLE IF NOT EXISTS [userdata] ([char_idx] INTEGER, [playername] CHAR(64), " \
	"[member_since] CHAR(30), [last_played] CHAR(30), [playtime_total] INTEGER,[playingtime] INTEGER)"
#define DB_CREATESTATS \
	"CREATE TABLE IF NOT EXISTS [game_stats] ([char_idx] INTEGER,  [shots] INTEGER,   " \
	"[shots_hit] INTEGER,   [frags] INTEGER,   [fragged] INTEGER,   " \
	"[num_sprees] INTEGER,   [max_streak] INTEGER,   [suicides] INTEGER,   " \
	"[score] INTEGER,   [deaths] INTEGER,   [damage_given] INTEGER,   [damage_received] INTEGER,   [rail_shot] INTEGER,   [rail_hit] INTEGER,   [rail_kill] INTEGER,   [ping_total] INTEGER,   [ping_samples] INTEGER,   [item_quad] INTEGER,   [item_shield] INTEGER,   [item_armor] INTEGER,   [item_mega] INTEGER,   [rune_strength] INTEGER,   [rune_haste] INTEGER,   [rune_regen] INTEGER,   [rune_resist] INTEGER)"
#define DB_CREATECTFSTATS \
	"CREATE TABLE IF NOT EXISTS [ctf_stats] ([char_idx] INTEGER,  [flag_pickups] INTEGER,   " \
	"[flag_captures] INTEGER,   [flag_returns] INTEGER,   [flag_kills] INTEGER,   " \
	"[offense_kills] INTEGER,   [defense_kills] INTEGER,   [assists] INTEGER,   " \
	"[max_cap_streak] INTEGER,   [sweeps] INTEGER,   " \
	"[flag_drops] INTEGER,   [defense_base] INTEGER,   [defense_flag] INTEGER,   [defense_carrier] INTEGER)"
#define DB_CREATECDATA \
	"CREATE TABLE IF NOT EXISTS [character_data] ([char_idx] INTEGER,  [adminlevel] INTEGER)"

// One row per match played.
#define DB_CREATEMATCHES \
	"CREATE TABLE IF NOT EXISTS [matches] (" \
	"[match_id] INTEGER PRIMARY KEY AUTOINCREMENT, [mapname] CHAR(64), " \
	"[started] CHAR(30), [ended] CHAR(30), [duration] INTEGER, " \
	"[red_score] INTEGER, [blue_score] INTEGER, " \
	"[red_caps] INTEGER, [blue_caps] INTEGER, [winner] INTEGER)"

// One row per player per match. Same statistics as the lifetime tables, but
// scoped to the match, so a career total is the sum of these and a recent form
// figure is the last few.
#define DB_CREATEMATCHPLAYERS \
	"CREATE TABLE IF NOT EXISTS [match_players] (" \
	"[match_id] INTEGER, [char_idx] INTEGER, [playername] CHAR(64), [team] INTEGER, " \
	"[score] INTEGER, [frags] INTEGER, [fragged] INTEGER, [deaths] INTEGER, " \
	"[suicides] INTEGER, [shots] INTEGER, [shots_hit] INTEGER, " \
	"[rail_shot] INTEGER, [rail_hit] INTEGER, [rail_kill] INTEGER, " \
	"[damage_given] INTEGER, [damage_received] INTEGER, " \
	"[max_streak] INTEGER, [num_sprees] INTEGER, " \
	"[flag_pickups] INTEGER, [flag_captures] INTEGER, [flag_returns] INTEGER, " \
	"[flag_kills] INTEGER, [flag_drops] INTEGER, " \
	"[offense_kills] INTEGER, [defense_base] INTEGER, [defense_flag] INTEGER, " \
	"[defense_carrier] INTEGER, [assists] INTEGER, " \
	"[max_cap_streak] INTEGER, [sweeps] INTEGER, " \
	"[item_quad] INTEGER, [item_shield] INTEGER, [item_armor] INTEGER, [item_mega] INTEGER, " \
	"[rune_strength] INTEGER, [rune_haste] INTEGER, [rune_regen] INTEGER, [rune_resist] INTEGER, " \
	"[ping_avg] INTEGER, [playtime] INTEGER)"

/*
 * One row per client per match, written by the SLIPGATE session recorder
 * (sg_sessiondb). Deliberately NOT the same thing as match_players:
 *
 *   - match_players is the leaderboard feed. It is keyed on char_idx, it goes
 *     through CTF_TrackStatsFor, and a bot the server is not tracking leaves
 *     no row in it at all.
 *   - this table is the attendance record. It is keyed on the name as it was
 *     worn during the match, so a client that has no char_idx -- an untracked
 *     bot, a one-visit guest -- still gets counted. is_bot is the flag a
 *     leaderboard query excludes on, which is what lets the row exist without
 *     polluting anything that reads match_players.
 *
 * char_idx is carried when the player already had one, and is -1 otherwise.
 * It is never ALLOCATED from here: creating a userdata row for a bot the
 * server declined to track is exactly what CTF_TrackStatsFor exists to stop.
 *
 * joined_at / left_at are wall clock, reconstructed from the frame counters
 * (one frame = 100ms) rather than stored as level-relative seconds, because
 * matches.started is stamped when the match is RECORDED -- at the end -- and
 * a level-relative offset would have nothing to be relative to.
 */
#define DB_CREATESESSIONEVENTS \
	"CREATE TABLE IF NOT EXISTS [sg_session_events] (" \
	"[match_id] INTEGER, [char_idx] INTEGER, [client_name] CHAR(64), " \
	"[is_bot] INTEGER, [team] INTEGER, " \
	"[caps] INTEGER, [steals] INTEGER, [returns] INTEGER, " \
	"[kills] INTEGER, [deaths] INTEGER, " \
	"[damage_given] INTEGER, [damage_taken] INTEGER, " \
	"[items_taken_major] INTEGER, [chat_lines] INTEGER, " \
	"[joined_at] CHAR(30), [left_at] CHAR(30))"

#define DB_UPDATEUDATA \
	"UPDATE userdata SET playername=?, member_since=?, last_played=?, " \
	"playtime_total=?, playingtime=? WHERE char_idx=?;"
#define DB_UPDATESTATS \
	"UPDATE game_stats SET shots=?, shots_hit=?, frags=?, fragged=?, " \
	"num_sprees=?, max_streak=?, suicides=?, " \
	"score=?, deaths=?, damage_given=?, damage_received=?, rail_shot=?, rail_hit=?, rail_kill=?, ping_total=?, ping_samples=?, item_quad=?, item_shield=?, item_armor=?, item_mega=?, rune_strength=?, rune_haste=?, rune_regen=?, rune_resist=? WHERE char_idx=?;"
#define DB_UPDATECTFSTATS \
	"UPDATE ctf_stats SET flag_pickups=?, flag_captures=?, flag_returns=?, " \
	"flag_kills=?, offense_kills=?, defense_kills=?, assists=?, " \
	"max_cap_streak=?, sweeps=?, " \
	"flag_drops=?, defense_base=?, defense_flag=?, defense_carrier=? WHERE char_idx=?;"
#define DB_UPDATECDATA \
	"UPDATE character_data SET adminlevel=? WHERE char_idx=?;"

static sqlite3 *dbconn = NULL;
static char     dbname[CTF_MAX_DBPATH];
static int      db_last_match_id = -1;

// db_stmt() caches for every statement in this file whose SQL text is fixed
// at compile time -- see the banner in ctf_sqlite_core.h for what the
// pattern is and why it is safe here specifically: dbconn is one
// connection held open for the module's whole life, so a statement
// prepared against it stays valid for as long as the cache variable does.
// File-scope on purpose, not function-local static: DB_Conn_Cleanup()
// below has to be able to reach every one of these to finalize it before
// dbconn closes, and a function-local static is invisible outside its own
// function. One variable per distinct statement, named for the function
// and (where a function uses more than one) the table it targets.
//
// A few call sites in this file are deliberately NOT here -- db_count_rows(),
// DB_Top() and DB_TopFormat() build their SQL text per call from a table or
// field argument, so there is no single fixed statement to cache; and
// DB_SessionRecord() is left on its own original prepare-per-call as the
// origin of this pattern (see the comment on that function). Each of those
// carries its own comment explaining the omission.
static sqlite3_stmt *stmt_has_schema = NULL;
static sqlite3_stmt *stmt_get_id = NULL;
static sqlite3_stmt *stmt_newid_maxid = NULL;
static sqlite3_stmt *stmt_newid_insert = NULL;
static sqlite3_stmt *stmt_newid_game = NULL;
static sqlite3_stmt *stmt_newid_ctf = NULL;
static sqlite3_stmt *stmt_newid_char = NULL;
static sqlite3_stmt *stmt_load_userdata = NULL;
static sqlite3_stmt *stmt_load_gamestats = NULL;
static sqlite3_stmt *stmt_load_ctfstats = NULL;
static sqlite3_stmt *stmt_load_chardata = NULL;
static sqlite3_stmt *stmt_save_userdata = NULL;
static sqlite3_stmt *stmt_save_gamestats = NULL;
static sqlite3_stmt *stmt_save_ctfstats = NULL;
static sqlite3_stmt *stmt_save_chardata = NULL;
static sqlite3_stmt *stmt_print_userdata = NULL;
static sqlite3_stmt *stmt_print_gamestats = NULL;
static sqlite3_stmt *stmt_print_ctfstats = NULL;
static sqlite3_stmt *stmt_export = NULL;
static sqlite3_stmt *stmt_rename = NULL;
static sqlite3_stmt *stmt_prune_count = NULL;
static sqlite3_stmt *stmt_match_begin = NULL;
static sqlite3_stmt *stmt_match_record = NULL;
static sqlite3_stmt *stmt_match_finish = NULL;

// Finalizes every cache above. Called from every path that closes dbconn,
// not just DB_Conn_Cleanup() -- a prepared statement still attached to a
// connection makes sqlite3_close() refuse to actually free it (it returns
// SQLITE_BUSY and leaves the connection running as a zombie instead), so
// skipping this on, say, DB_Conn_Start()'s schema-build failure path would
// leak the connection AND leave a cache pointing at it for nobody to ever
// close.
static void db_stmt_close_all(void)
{
	db_stmt_close(&stmt_has_schema);
	db_stmt_close(&stmt_get_id);
	db_stmt_close(&stmt_newid_maxid);
	db_stmt_close(&stmt_newid_insert);
	db_stmt_close(&stmt_newid_game);
	db_stmt_close(&stmt_newid_ctf);
	db_stmt_close(&stmt_newid_char);
	db_stmt_close(&stmt_load_userdata);
	db_stmt_close(&stmt_load_gamestats);
	db_stmt_close(&stmt_load_ctfstats);
	db_stmt_close(&stmt_load_chardata);
	db_stmt_close(&stmt_save_userdata);
	db_stmt_close(&stmt_save_gamestats);
	db_stmt_close(&stmt_save_ctfstats);
	db_stmt_close(&stmt_save_chardata);
	db_stmt_close(&stmt_print_userdata);
	db_stmt_close(&stmt_print_gamestats);
	db_stmt_close(&stmt_print_ctfstats);
	db_stmt_close(&stmt_export);
	db_stmt_close(&stmt_rename);
	db_stmt_close(&stmt_prune_count);
	db_stmt_close(&stmt_match_begin);
	db_stmt_close(&stmt_match_record);
	db_stmt_close(&stmt_match_finish);
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

static qboolean db_has_schema(void)
{
	sqlite3_stmt *res;
	qboolean found;

	res = db_stmt(dbconn, &stmt_has_schema,
		"SELECT name FROM sqlite_master WHERE type='table' AND name='userdata';");
	if (!res)
	{
		db_error(dbconn, "schema probe");
		return false;
	}

	found = (sqlite3_step(res) == SQLITE_ROW);

	return found;
}

static qboolean build_db(void)
{
	static const char *schema[] = {
		DB_CREATEUDATA,
		DB_CREATESTATS,
		DB_CREATECTFSTATS,
		DB_CREATECDATA,
		DB_CREATEMATCHES,
		DB_CREATEMATCHPLAYERS,
		DB_CREATESESSIONEVENTS,
		"CREATE INDEX IF NOT EXISTS idx_mp_char  ON match_players(char_idx)",
		"CREATE INDEX IF NOT EXISTS idx_mp_match ON match_players(match_id)",
		"CREATE INDEX IF NOT EXISTS idx_m_ended  ON matches(ended)",
		"CREATE INDEX IF NOT EXISTS idx_se_match ON sg_session_events(match_id)",
		"CREATE INDEX IF NOT EXISTS idx_se_name  ON sg_session_events(client_name)",
		NULL
	};
	int i;

	for (i = 0; schema[i]; i++)
	{
		if (!db_exec(dbconn, schema[i]))
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

	if (!db_open_tuned(dbname, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, &dbconn))
	{
		db_error(dbconn, dbname);
		sqlite3_close(dbconn);
		dbconn = NULL;
		return false;
	}

	// build_db is CREATE TABLE IF NOT EXISTS throughout, so running it every
	// time is safe and repairs a database that lost a table -- the old code
	// probed only for `userdata` and skipped the rest if that one existed.
	{
		// db_has_schema() below caches stmt_has_schema against this dbconn,
		// so any failure path from here on has to close that cache before
		// closing dbconn -- see db_stmt_close_all().
		qboolean fresh = !db_has_schema();

		if (!build_db())
		{
			db_error(dbconn, "creating schema");
			db_stmt_close_all();
			sqlite3_close(dbconn);
			dbconn = NULL;
			return false;
		}

		if (fresh)
			gi.dprintf("stats db: created %s\n", dbname);
	}

	// databases written before capture streaks and sweeps existed
	db_ensure_column(dbconn, "ctf_stats", "max_cap_streak", "INTEGER");
	db_ensure_column(dbconn, "ctf_stats", "sweeps", "INTEGER");

	// columns added when every tracked statistic started persisting
	db_ensure_column(dbconn, "game_stats", "score", "INTEGER");
	db_ensure_column(dbconn, "game_stats", "deaths", "INTEGER");
	db_ensure_column(dbconn, "game_stats", "damage_given", "INTEGER");
	db_ensure_column(dbconn, "game_stats", "damage_received", "INTEGER");
	db_ensure_column(dbconn, "game_stats", "rail_shot", "INTEGER");
	db_ensure_column(dbconn, "game_stats", "rail_hit", "INTEGER");
	db_ensure_column(dbconn, "game_stats", "rail_kill", "INTEGER");
	db_ensure_column(dbconn, "game_stats", "ping_total", "INTEGER");
	db_ensure_column(dbconn, "game_stats", "ping_samples", "INTEGER");
	db_ensure_column(dbconn, "game_stats", "item_quad", "INTEGER");
	db_ensure_column(dbconn, "game_stats", "item_shield", "INTEGER");
	db_ensure_column(dbconn, "game_stats", "item_armor", "INTEGER");
	db_ensure_column(dbconn, "game_stats", "item_mega", "INTEGER");
	db_ensure_column(dbconn, "game_stats", "rune_strength", "INTEGER");
	db_ensure_column(dbconn, "game_stats", "rune_haste", "INTEGER");
	db_ensure_column(dbconn, "game_stats", "rune_regen", "INTEGER");
	db_ensure_column(dbconn, "game_stats", "rune_resist", "INTEGER");
	db_ensure_column(dbconn, "ctf_stats", "flag_drops", "INTEGER");
	db_ensure_column(dbconn, "ctf_stats", "defense_base", "INTEGER");
	db_ensure_column(dbconn, "ctf_stats", "defense_flag", "INTEGER");
	db_ensure_column(dbconn, "ctf_stats", "defense_carrier", "INTEGER");

	return true;
}

void DB_Conn_Cleanup(void)
{
	if (!dbconn)
		return;

	// Every db_stmt() cache above is a handle prepared against THIS dbconn
	// and has to be finalized before sqlite3_close() -- see db_stmt_close_all().
	db_stmt_close_all();

	sqlite3_close(dbconn);
	dbconn = NULL;
	dbname[0] = '\0';
}

int DB_GetID(const char *playername)
{
	sqlite3_stmt *res;
	int id = -1;

	if (!dbconn || !playername || playername[0] == '\0')
		return -1;

	res = db_stmt(dbconn, &stmt_get_id, "SELECT char_idx FROM userdata WHERE playername=?");
	if (!res)
	{
		db_error(dbconn, "DB_GetID");
		return -1;
	}

	sqlite3_bind_text(res, 1, playername, -1, SQLITE_TRANSIENT);

	if (sqlite3_step(res) == SQLITE_ROW)
		id = sqlite3_column_int(res, 0);

	return id;
}

// One base row for a new char_idx: `sql` is an INSERT with a single `?` for
// the id and a literal 0 for every other column. Used three times by
// DB_NewID for game_stats, ctf_stats and character_data -- bound, not built
// with va(), because va() hands back one rotating static buffer and the
// caller was passing three live va() results to the same db_exec chain.
//
// `cache` is the caller's own file-scope statement cache: game_stats,
// ctf_stats and character_data each need their own, since each call passes
// different SQL text through the same helper and db_stmt() caches by cache
// pointer, not by SQL text.
static qboolean db_newid_base_row(sqlite3 *db, sqlite3_stmt **cache, const char *sql, int id)
{
	sqlite3_stmt *res = db_stmt(db, cache, sql);

	if (!res)
	{
		db_error(db, "DB_NewID insert");
		return false;
	}
	sqlite3_bind_int(res, 1, id);
	if (sqlite3_step(res) != SQLITE_DONE)
	{
		db_error(db, "DB_NewID insert");
		return false;
	}
	return true;
}

int DB_NewID(const char *playername)
{
	sqlite3_stmt *res;
	int id = -1;

	if (!dbconn)
		return -1;

	// MAX+1, not COUNT(*): COUNT reissues a live id the moment a row is deleted
	res = db_stmt(dbconn, &stmt_newid_maxid, "SELECT IFNULL(MAX(char_idx), -1) + 1 FROM userdata");
	if (!res)
	{
		db_error(dbconn, "DB_NewID");
		return -1;
	}

	if (sqlite3_step(res) == SQLITE_ROW)
		id = sqlite3_column_int(res, 0);

	if (id < 0)
		return -1;

	gi.dprintf("SQLite (single mode): creating initial data for player id %d..", id);

	if (!db_begin(dbconn))
		return -1;

	res = db_stmt(dbconn, &stmt_newid_insert, "INSERT INTO userdata VALUES (?,?,\"\",\"\",0,0)");
	if (!res)
	{
		db_error(dbconn, "DB_NewID insert");
		db_rollback(dbconn);
		return -1;
	}
	sqlite3_bind_int(res, 1, id);
	sqlite3_bind_text(res, 2, playername ? playername : "", -1, SQLITE_TRANSIENT);
	if (sqlite3_step(res) != SQLITE_DONE)
	{
		db_error(dbconn, "DB_NewID insert");
		db_rollback(dbconn);
		return -1;
	}

	// game_stats / ctf_stats / character_data: every column but char_idx is
	// zero for a brand new player, so the only bound parameter each of these
	// three needs is the id itself.
	if (!db_newid_base_row(dbconn, &stmt_newid_game,
			"INSERT INTO game_stats VALUES (?,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0)", id) ||
		!db_newid_base_row(dbconn, &stmt_newid_ctf,
			"INSERT INTO ctf_stats VALUES (?,0,0,0,0,0,0,0,0,0,0,0,0,0)", id) ||
		!db_newid_base_row(dbconn, &stmt_newid_char,
			"INSERT INTO character_data VALUES (?,0)", id))
	{
		db_rollback(dbconn);
		return -1;
	}

	if (!db_commit(dbconn))
	{
		db_error(dbconn, "DB_NewID commit");
		db_rollback(dbconn);
		return -1;
	}

	gi.dprintf(" inserted bases.\n");
	return id;
}

qboolean DB_LoadPlayer(edict_t *player)
{
	sqlite3_stmt *res;
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

	res = db_stmt(dbconn, &stmt_load_userdata, "SELECT * FROM userdata WHERE char_idx=?");
	if (res)
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

	res = db_stmt(dbconn, &stmt_load_gamestats, "SELECT * FROM game_stats WHERE char_idx=?");
	if (res)
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
			ps->score           = sqlite3_column_int(res, 8);
			ps->deaths          = sqlite3_column_int(res, 9);
			ps->damage_given    = (unsigned long)sqlite3_column_int64(res, 10);
			ps->damage_received = (unsigned long)sqlite3_column_int64(res, 11);
			ps->rail_shot       = (unsigned long)sqlite3_column_int64(res, 12);
			ps->rail_hit        = (unsigned long)sqlite3_column_int64(res, 13);
			ps->rail_kill       = sqlite3_column_int(res, 14);
			ps->ping_total      = (unsigned long)sqlite3_column_int64(res, 15);
			ps->ping_samples    = (unsigned long)sqlite3_column_int64(res, 16);
			ps->item_quad       = sqlite3_column_int(res, 17);
			ps->item_shield     = sqlite3_column_int(res, 18);
			ps->item_armor      = sqlite3_column_int(res, 19);
			ps->item_mega       = sqlite3_column_int(res, 20);
			ps->rune_strength   = sqlite3_column_int(res, 21);
			ps->rune_haste      = sqlite3_column_int(res, 22);
			ps->rune_regen      = sqlite3_column_int(res, 23);
			ps->rune_resist     = sqlite3_column_int(res, 24);
		}
	}

	res = db_stmt(dbconn, &stmt_load_ctfstats, "SELECT * FROM ctf_stats WHERE char_idx=?");
	if (res)
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
			ps->flag_drops      = sqlite3_column_int(res, 10);
			ps->defense_base    = sqlite3_column_int(res, 11);
			ps->defense_flag    = sqlite3_column_int(res, 12);
			ps->defense_carrier = sqlite3_column_int(res, 13);
		}
	}

	res = db_stmt(dbconn, &stmt_load_chardata, "SELECT * FROM character_data WHERE char_idx=?");
	if (res)
	{
		sqlite3_bind_int(res, 1, id);
		if (sqlite3_step(res) == SQLITE_ROW)
			ps->administrator = sqlite3_column_int(res, 1);
	}

	return true;
}

qboolean DB_SavePlayer(edict_t *player)
{
	sqlite3_stmt *res;
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

	if (!db_begin(dbconn))
		return false;

	res = db_stmt(dbconn, &stmt_save_userdata, DB_UPDATEUDATA);
	if (!res)
		goto done;
	sqlite3_bind_text(res, 1, name ? name : "", -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(res, 2, ps->member_since, -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(res, 3, ps->last_played,  -1, SQLITE_TRANSIENT);
	sqlite3_bind_int(res, 4, ps->total_playtime);
	sqlite3_bind_int(res, 5, ps->playingtime);
	sqlite3_bind_int(res, 6, id);
	if (sqlite3_step(res) != SQLITE_DONE) goto done;

	res = db_stmt(dbconn, &stmt_save_gamestats, DB_UPDATESTATS);
	if (!res)
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
	sqlite3_bind_int(res, 25, id);
	if (sqlite3_step(res) != SQLITE_DONE) goto done;

	res = db_stmt(dbconn, &stmt_save_ctfstats, DB_UPDATECTFSTATS);
	if (!res)
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
	sqlite3_bind_int(res, 14, id);
	if (sqlite3_step(res) != SQLITE_DONE) goto done;

	res = db_stmt(dbconn, &stmt_save_chardata, DB_UPDATECDATA);
	if (!res)
		goto done;
	sqlite3_bind_int(res, 1, ps->administrator);
	sqlite3_bind_int(res, 2, id);
	if (sqlite3_step(res) != SQLITE_DONE) goto done;

	ok = db_commit(dbconn);

done:
	if (!ok)
	{
		db_error(dbconn, "DB_SavePlayer");
		db_rollback(dbconn);
	}

	return ok;
}

/*
==================
Admin surface

These back the "sv statsdb" console command. Unified backend only: the
per-player files hold one row each and have no cross-player view, so a
leaderboard would mean opening every file in the directory.
==================
*/

// Columns an admin is allowed to sort on, and the table each one lives in.
// A whitelist rather than pasting gi.argv straight into the SQL -- the column
// name cannot be bound as a parameter, so it has to be checked against a
// known set instead.
static const struct { const char *field; const char *table; } db_sortable[] = {
	{ "frags",          "game_stats" },
	{ "fragged",        "game_stats" },
	{ "shots",          "game_stats" },
	{ "shots_hit",      "game_stats" },
	{ "num_sprees",     "game_stats" },
	{ "max_streak",     "game_stats" },
	{ "suicides",       "game_stats" },
	{ "flag_pickups",   "ctf_stats"  },
	{ "flag_captures",  "ctf_stats"  },
	{ "flag_returns",   "ctf_stats"  },
	{ "flag_kills",     "ctf_stats"  },
	{ "offense_kills",  "ctf_stats"  },
	{ "defense_kills",  "ctf_stats"  },
	{ "assists",        "ctf_stats"  },
	{ "max_cap_streak", "ctf_stats"  },
	{ "sweeps",         "ctf_stats"  },
	{ "playtime_total", "userdata"   },
	{ "playingtime",    "userdata"   },
	{ NULL, NULL }
};

static const char *db_table_for(const char *field)
{
	int i;

	if (!field)
		return NULL;

	for (i = 0; db_sortable[i].field; i++)
	{
		if (Q_stricmp(field, db_sortable[i].field) == 0)
			return db_sortable[i].table;
	}

	return NULL;
}

// Not converted to db_stmt(): `sql` is built fresh from `table` on every
// call, and this one function serves three different tables (see
// DB_Status below), so a single cache pointer would hand back a statement
// prepared against whichever table asked first, not the one this call
// wants. db_stmt() caches by cache-pointer identity, not by SQL text, so
// it only helps when a call site's SQL text is fixed at compile time.
static int db_count_rows(const char *table)
{
	sqlite3_stmt *res = NULL;
	char sql[128];
	int n = -1;

	snprintf(sql, sizeof(sql), "SELECT COUNT(*) FROM %s", table);

	if (sqlite3_prepare_v2(dbconn, sql, -1, &res, NULL) != SQLITE_OK)
		return -1;

	if (sqlite3_step(res) == SQLITE_ROW)
		n = sqlite3_column_int(res, 0);

	sqlite3_finalize(res);
	return n;
}

void DB_Status(void)
{
	if (!dbconn && !DB_Conn_Start())
	{
		gi.cprintf(NULL, PRINT_HIGH, "statsdb: unified backend is not open.\n");
		return;
	}

	gi.cprintf(NULL, PRINT_HIGH, "statsdb: unified, %s\n", dbname);
	gi.cprintf(NULL, PRINT_HIGH, "  players recorded : %d\n", db_count_rows("userdata"));
	gi.cprintf(NULL, PRINT_HIGH, "  game_stats rows  : %d\n", db_count_rows("game_stats"));
	gi.cprintf(NULL, PRINT_HIGH, "  ctf_stats rows   : %d\n", db_count_rows("ctf_stats"));
	{
		struct stat st;

		if (stat(dbname, &st) == 0)
			gi.cprintf(NULL, PRINT_HIGH, "  file size        : %ld bytes\n", (long)st.st_size);
	}
}

qboolean DB_Reset(void)
{
	if (!dbconn && !DB_Conn_Start())
		return false;

	if (!db_begin(dbconn))
		return false;

	if (!db_exec(dbconn, "DELETE FROM userdata;") ||
		!db_exec(dbconn, "DELETE FROM game_stats;") ||
		!db_exec(dbconn, "DELETE FROM ctf_stats;") ||
		!db_exec(dbconn, "DELETE FROM character_data;"))
	{
		db_rollback(dbconn);
		return false;
	}

	if (!db_commit(dbconn))
		return false;

	// reclaim the space rather than leaving a file full of free pages
	db_exec(dbconn, "VACUUM;");

	gi.cprintf(NULL, PRINT_HIGH, "statsdb: all rows deleted from %s\n", dbname);
	return true;
}

// Not converted to db_stmt(): the query text is assembled per call from
// `field` and `table` (see the whitelist above), so it varies with every
// admin command invocation instead of being fixed at compile time -- the
// same reason db_count_rows() above stays on plain prepare/finalize.
void DB_Top(const char *field, int count)
{
	const char *table = db_table_for(field);
	sqlite3_stmt *res = NULL;
	char sql[512];
	int rank = 0;

	if (!table)
	{
		int i;

		gi.cprintf(NULL, PRINT_HIGH, "statsdb: unknown column \"%s\". Try one of:\n",
			field ? field : "");
		for (i = 0; db_sortable[i].field; i++)
			gi.cprintf(NULL, PRINT_HIGH, "  %s\n", db_sortable[i].field);
		return;
	}

	if (!dbconn && !DB_Conn_Start())
	{
		gi.cprintf(NULL, PRINT_HIGH, "statsdb: unified backend is not open.\n");
		return;
	}

	if (count < 1)
		count = 10;
	if (count > 50)
		count = 50;

	// field and table came from the whitelist above; only the limit is bound
	if (Q_stricmp(table, "userdata") == 0)
	{
		snprintf(sql, sizeof(sql),
			"SELECT playername, %s FROM userdata ORDER BY %s DESC LIMIT ?", field, field);
	}
	else
	{
		snprintf(sql, sizeof(sql),
			"SELECT u.playername, t.%s FROM %s t "
			"JOIN userdata u ON u.char_idx = t.char_idx "
			"ORDER BY t.%s DESC LIMIT ?", field, table, field);
	}

	if (sqlite3_prepare_v2(dbconn, sql, -1, &res, NULL) != SQLITE_OK)
	{
		db_error(dbconn, "DB_Top");
		return;
	}

	sqlite3_bind_int(res, 1, count);

	gi.cprintf(NULL, PRINT_HIGH, "statsdb: top %d by %s\n", count, field);

	while (sqlite3_step(res) == SQLITE_ROW)
	{
		const unsigned char *name = sqlite3_column_text(res, 0);

		rank++;
		gi.cprintf(NULL, PRINT_HIGH, "  %2d. %-20s %d\n",
			rank, name ? (const char *)name : "(unnamed)", sqlite3_column_int(res, 1));
	}

	sqlite3_finalize(res);

	if (rank == 0)
		gi.cprintf(NULL, PRINT_HIGH, "  (no rows)\n");
}

qboolean DB_PrintPlayer(const char *playername)
{
	sqlite3_stmt *res;
	int id;

	if (!dbconn && !DB_Conn_Start())
		return false;

	id = DB_GetID(playername);
	if (id < 0)
	{
		gi.cprintf(NULL, PRINT_HIGH, "statsdb: no record for \"%s\"\n",
			playername ? playername : "");
		return false;
	}

	gi.cprintf(NULL, PRINT_HIGH, "statsdb: %s (char_idx %d)\n", playername, id);

	res = db_stmt(dbconn, &stmt_print_userdata, "SELECT * FROM userdata WHERE char_idx=?");
	if (res)
	{
		sqlite3_bind_int(res, 1, id);
		if (sqlite3_step(res) == SQLITE_ROW)
		{
			const unsigned char *since = sqlite3_column_text(res, 2);
			const unsigned char *last  = sqlite3_column_text(res, 3);

			gi.cprintf(NULL, PRINT_HIGH, "  member since %s, last played %s\n",
				since ? (const char *)since : "?", last ? (const char *)last : "?");
			gi.cprintf(NULL, PRINT_HIGH, "  playtime %d min (this session %d s)\n",
				sqlite3_column_int(res, 4), sqlite3_column_int(res, 5));
		}
	}

	res = db_stmt(dbconn, &stmt_print_gamestats, "SELECT * FROM game_stats WHERE char_idx=?");
	if (res)
	{
		sqlite3_bind_int(res, 1, id);
		if (sqlite3_step(res) == SQLITE_ROW)
		{
			gi.cprintf(NULL, PRINT_HIGH,
				"  frags %d  fragged %d  suicides %d  best streak %d  sprees %d\n",
				sqlite3_column_int(res, 3), sqlite3_column_int(res, 4),
				sqlite3_column_int(res, 7), sqlite3_column_int(res, 6),
				sqlite3_column_int(res, 5));
			gi.cprintf(NULL, PRINT_HIGH, "  shots %d  hits %d\n",
				sqlite3_column_int(res, 1), sqlite3_column_int(res, 2));
		}
	}

	res = db_stmt(dbconn, &stmt_print_ctfstats, "SELECT * FROM ctf_stats WHERE char_idx=?");
	if (res)
	{
		sqlite3_bind_int(res, 1, id);
		if (sqlite3_step(res) == SQLITE_ROW)
		{
			gi.cprintf(NULL, PRINT_HIGH,
				"  caps %d  pickups %d  returns %d  fc kills %d  assists %d\n",
				sqlite3_column_int(res, 2), sqlite3_column_int(res, 1),
				sqlite3_column_int(res, 3), sqlite3_column_int(res, 4),
				sqlite3_column_int(res, 7));
			gi.cprintf(NULL, PRINT_HIGH,
				"  off kills %d  def kills %d  best cap streak %d  sweeps %d\n",
				sqlite3_column_int(res, 5), sqlite3_column_int(res, 6),
				sqlite3_column_int(res, 8), sqlite3_column_int(res, 9));
		}
	}

	return true;
}

// Shared by "sv statsdb top" and the player-facing "cmd rank": same whitelist,
// same query, one writes to the console and the other to a client.
//
// Not converted to db_stmt(): same reason as DB_Top() above -- the SQL text
// depends on `field` and `table` and is rebuilt every call.
qboolean DB_TopFormat(const char *field, int count, char *out, size_t outsize)
{
	const char *table = db_table_for(field);
	sqlite3_stmt *res = NULL;
	char sql[512];
	char line[128];
	size_t used;
	int rank = 0;

	if (!out || outsize == 0)
		return false;

	out[0] = '\0';

	if (!table)
		return false;

	if (!dbconn && !DB_Conn_Start())
		return false;

	if (count < 1)
		count = 10;
	if (count > 50)
		count = 50;

	if (Q_stricmp(table, "userdata") == 0)
	{
		snprintf(sql, sizeof(sql),
			"SELECT playername, %s FROM userdata ORDER BY %s DESC LIMIT ?", field, field);
	}
	else
	{
		snprintf(sql, sizeof(sql),
			"SELECT u.playername, t.%s FROM %s t "
			"JOIN userdata u ON u.char_idx = t.char_idx "
			"ORDER BY t.%s DESC LIMIT ?", field, table, field);
	}

	if (sqlite3_prepare_v2(dbconn, sql, -1, &res, NULL) != SQLITE_OK)
	{
		db_error(dbconn, "DB_TopFormat");
		return false;
	}

	sqlite3_bind_int(res, 1, count);

	snprintf(line, sizeof(line), "\nTop %d by %s\n", count, field);
	strncpy(out, line, outsize - 1);
	out[outsize - 1] = '\0';

	while (sqlite3_step(res) == SQLITE_ROW)
	{
		const unsigned char *name = sqlite3_column_text(res, 0);

		rank++;
		snprintf(line, sizeof(line), "%2d. %-16s %d\n",
			rank, name ? (const char *)name : "(unnamed)", sqlite3_column_int(res, 1));

		used = strlen(out);
		if (used + strlen(line) + 1 >= outsize)
			break;					// out of room; stop cleanly rather than truncate mid-row

		strcpy(out + used, line);
	}

	sqlite3_finalize(res);

	if (rank == 0)
	{
		used = strlen(out);
		if (used + 12 < outsize)
			strcpy(out + used, "(no rows)\n");
	}

	return true;
}

qboolean DB_Export(const char *path)
{
	sqlite3_stmt *res;
	FILE *f;
	int rows = 0;

	if (!dbconn && !DB_Conn_Start())
		return false;

	f = fopen(path, "w");
	if (!f)
	{
		gi.cprintf(NULL, PRINT_HIGH, "statsdb: could not open %s for writing.\n", path);
		return false;
	}

	fprintf(f, "playername\tmember_since\tlast_played\tplaytime_total"
		"\tshots\tshots_hit\tfrags\tfragged\tnum_sprees\tmax_streak\tsuicides"
		"\tflag_pickups\tflag_captures\tflag_returns\tflag_kills"
		"\toffense_kills\tdefense_kills\tassists\tmax_cap_streak\tsweeps\n");

	res = db_stmt(dbconn, &stmt_export,
		"SELECT u.playername, u.member_since, u.last_played, u.playtime_total,"
		" g.shots, g.shots_hit, g.frags, g.fragged, g.num_sprees, g.max_streak, g.suicides,"
		" c.flag_pickups, c.flag_captures, c.flag_returns, c.flag_kills,"
		" c.offense_kills, c.defense_kills, c.assists, c.max_cap_streak, c.sweeps"
		" FROM userdata u"
		" JOIN game_stats g ON g.char_idx = u.char_idx"
		" JOIN ctf_stats  c ON c.char_idx = u.char_idx"
		" ORDER BY u.playername");
	if (!res)
	{
		db_error(dbconn, "DB_Export");
		fclose(f);
		return false;
	}

	while (sqlite3_step(res) == SQLITE_ROW)
	{
		int col;

		for (col = 0; col < 20; col++)
		{
			const unsigned char *txt = sqlite3_column_text(res, col);

			fprintf(f, "%s%s", txt ? (const char *)txt : "", col == 19 ? "\n" : "\t");
		}
		rows++;
	}

	fclose(f);

	gi.cprintf(NULL, PRINT_HIGH, "statsdb: exported %d player(s) to %s\n", rows, path);
	return true;
}

qboolean DB_Backup(const char *path)
{
	sqlite3 *dest = NULL;
	sqlite3_backup *bk;
	int rc;

	if (!dbconn && !DB_Conn_Start())
		return false;

	if (sqlite3_open_v2(path, &dest,
			SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL) != SQLITE_OK)
	{
		gi.cprintf(NULL, PRINT_HIGH, "statsdb: could not open %s for backup.\n", path);
		sqlite3_close(dest);
		return false;
	}

	// the backup API copies a live database safely; plain file copy does not
	bk = sqlite3_backup_init(dest, "main", dbconn, "main");
	if (!bk)
	{
		gi.cprintf(NULL, PRINT_HIGH, "statsdb: backup failed: %s\n", sqlite3_errmsg(dest));
		sqlite3_close(dest);
		return false;
	}

	sqlite3_backup_step(bk, -1);
	rc = sqlite3_backup_finish(bk);
	sqlite3_close(dest);

	if (rc != SQLITE_OK)
	{
		gi.cprintf(NULL, PRINT_HIGH, "statsdb: backup failed (%d).\n", rc);
		return false;
	}

	gi.cprintf(NULL, PRINT_HIGH, "statsdb: backed up to %s\n", path);
	return true;
}

qboolean DB_RenamePlayer(const char *oldname, const char *newname)
{
	sqlite3_stmt *res;
	int oldid, newid;

	if (!dbconn && !DB_Conn_Start())
		return false;

	oldid = DB_GetID(oldname);
	if (oldid < 0)
	{
		gi.cprintf(NULL, PRINT_HIGH, "statsdb: no record for \"%s\"\n", oldname);
		return false;
	}

	newid = DB_GetID(newname);

	// Simple case: the new name is unused, so just relabel the row.
	if (newid < 0)
	{
		res = db_stmt(dbconn, &stmt_rename, "UPDATE userdata SET playername=? WHERE char_idx=?");
		if (!res)
		{
			db_error(dbconn, "DB_RenamePlayer");
			return false;
		}
		sqlite3_bind_text(res, 1, newname, -1, SQLITE_TRANSIENT);
		sqlite3_bind_int(res, 2, oldid);

		if (sqlite3_step(res) != SQLITE_DONE)
		{
			db_error(dbconn, "DB_RenamePlayer");
			return false;
		}

		gi.cprintf(NULL, PRINT_HIGH, "statsdb: \"%s\" is now \"%s\"\n", oldname, newname);
		return true;
	}

	if (oldid == newid)
	{
		gi.cprintf(NULL, PRINT_HIGH, "statsdb: those are the same record.\n");
		return false;
	}

	// Both names exist, so fold the old record into the new one. Counters add,
	// bests take the larger, member_since keeps the earlier of the two.
	if (!db_begin(dbconn))
		return false;

	{
		char sql[1024];
		qboolean ok;

		snprintf(sql, sizeof(sql), "UPDATE game_stats SET"
			" shots      = shots      + (SELECT shots      FROM game_stats WHERE char_idx=%d),"
			" shots_hit  = shots_hit  + (SELECT shots_hit  FROM game_stats WHERE char_idx=%d),"
			" frags      = frags      + (SELECT frags      FROM game_stats WHERE char_idx=%d),"
			" fragged    = fragged    + (SELECT fragged    FROM game_stats WHERE char_idx=%d),"
			" num_sprees = num_sprees + (SELECT num_sprees FROM game_stats WHERE char_idx=%d),"
			" suicides   = suicides   + (SELECT suicides   FROM game_stats WHERE char_idx=%d),"
			" max_streak = MAX(max_streak, (SELECT max_streak FROM game_stats WHERE char_idx=%d))"
			" WHERE char_idx=%d",
			oldid, oldid, oldid, oldid, oldid, oldid, oldid, newid);
		ok = db_exec(dbconn, sql);

		if (ok)
		{
			snprintf(sql, sizeof(sql), "UPDATE ctf_stats SET"
				" flag_pickups  = flag_pickups  + (SELECT flag_pickups  FROM ctf_stats WHERE char_idx=%d),"
				" flag_captures = flag_captures + (SELECT flag_captures FROM ctf_stats WHERE char_idx=%d),"
				" flag_returns  = flag_returns  + (SELECT flag_returns  FROM ctf_stats WHERE char_idx=%d),"
				" flag_kills    = flag_kills    + (SELECT flag_kills    FROM ctf_stats WHERE char_idx=%d),"
				" offense_kills = offense_kills + (SELECT offense_kills FROM ctf_stats WHERE char_idx=%d),"
				" defense_kills = defense_kills + (SELECT defense_kills FROM ctf_stats WHERE char_idx=%d),"
				" assists       = assists       + (SELECT assists       FROM ctf_stats WHERE char_idx=%d),"
				" sweeps        = sweeps        + (SELECT sweeps        FROM ctf_stats WHERE char_idx=%d),"
				" max_cap_streak = MAX(max_cap_streak,"
				" (SELECT max_cap_streak FROM ctf_stats WHERE char_idx=%d))"
				" WHERE char_idx=%d",
				oldid, oldid, oldid, oldid, oldid, oldid, oldid, oldid, oldid, newid);
			ok = db_exec(dbconn, sql);
		}

		if (ok)
		{
			snprintf(sql, sizeof(sql), "UPDATE userdata SET"
				" playtime_total = playtime_total + (SELECT playtime_total FROM userdata WHERE char_idx=%d),"
				" member_since = MIN(NULLIF(member_since,''),"
				" (SELECT NULLIF(member_since,'') FROM userdata WHERE char_idx=%d))"
				" WHERE char_idx=%d", oldid, oldid, newid);
			ok = db_exec(dbconn, sql);
		}

		if (ok)
		{
			static const char *tables[] = { "userdata", "game_stats", "ctf_stats", "character_data", NULL };
			int k;

			for (k = 0; tables[k] && ok; k++)
			{
				snprintf(sql, sizeof(sql), "DELETE FROM %s WHERE char_idx=%d", tables[k], oldid);
				ok = db_exec(dbconn, sql);
			}
		}

		if (!ok)
		{
			db_rollback(dbconn);
			return false;
		}
	}

	if (!db_commit(dbconn))
		return false;

	gi.cprintf(NULL, PRINT_HIGH, "statsdb: merged \"%s\" into \"%s\"\n", oldname, newname);
	return true;
}

int DB_Prune(int days)
{
	// One cache for both the before- and after-count: same SQL text either
	// time, so they are the same statement as far as db_stmt() is concerned.
	sqlite3_stmt *res;
	char cutoff[32];
	time_t when;
	struct tm *lt;
	int before = 0, after = 0;

	if (days < 1)
		return -1;

	if (!dbconn && !DB_Conn_Start())
		return -1;

	when = time(NULL) - (time_t)days * 86400;
	lt = localtime(&when);
	if (!lt)
		return -1;

	// last_played is written by stats_fold_session in this exact layout, which
	// sorts correctly as text
	strftime(cutoff, sizeof(cutoff), "%Y-%m-%d %H:%M:%S", lt);

	res = db_stmt(dbconn, &stmt_prune_count, "SELECT COUNT(*) FROM userdata");
	if (res && sqlite3_step(res) == SQLITE_ROW)
		before = sqlite3_column_int(res, 0);

	if (!db_begin(dbconn))
		return -1;

	// Rows with an empty last_played have never been folded, so they are left
	// alone rather than treated as infinitely old.
	//
	// Built with snprintf into local buffers rather than nested va() calls:
	// va() hands back a rotating static buffer, so va(fmt, va(...)) can clobber
	// its own argument.
	{
		static const char *tables[] = { "game_stats", "ctf_stats", "character_data", NULL };
		char sql[512];
		int i;
		qboolean ok = true;

		for (i = 0; tables[i] && ok; i++)
		{
			snprintf(sql, sizeof(sql),
				"DELETE FROM %s WHERE char_idx IN (SELECT char_idx FROM userdata"
				" WHERE last_played <> '' AND last_played < '%s')", tables[i], cutoff);
			ok = db_exec(dbconn, sql);
		}

		if (ok)
		{
			snprintf(sql, sizeof(sql),
				"DELETE FROM userdata WHERE last_played <> '' AND last_played < '%s'", cutoff);
			ok = db_exec(dbconn, sql);
		}

		if (!ok)
		{
			db_rollback(dbconn);
			return -1;
		}
	}

	if (!db_commit(dbconn))
		return -1;

	res = db_stmt(dbconn, &stmt_prune_count, "SELECT COUNT(*) FROM userdata");
	if (res && sqlite3_step(res) == SQLITE_ROW)
		after = sqlite3_column_int(res, 0);

	return before - after;
}

/*
==================
Match history
==================
*/

static void db_stamp(char *out, size_t outsize, time_t when)
{
	struct tm *lt = localtime(&when);

	if (lt)
		strftime(out, outsize, "%Y-%m-%d %H:%M:%S", lt);
	else
		out[0] = '\0';
}

static void db_now(char *out, size_t outsize)
{
	db_stamp(out, outsize, time(NULL));
}

int DB_MatchBegin(const char *mapname)
{
	sqlite3_stmt *res;
	char started[32];
	int id = -1;

	if (!dbconn && !DB_Conn_Start())
		return -1;

	db_now(started, sizeof(started));

	res = db_stmt(dbconn, &stmt_match_begin,
		"INSERT INTO matches (mapname, started, ended, duration,"
		" red_score, blue_score, red_caps, blue_caps, winner)"
		" VALUES (?,?,'',0,0,0,0,0,0)");
	if (!res)
	{
		db_error(dbconn, "DB_MatchBegin");
		return -1;
	}

	sqlite3_bind_text(res, 1, mapname ? mapname : "", -1, SQLITE_TRANSIENT);
	sqlite3_bind_text(res, 2, started, -1, SQLITE_TRANSIENT);

	if (sqlite3_step(res) == SQLITE_DONE)
	{
		id = (int)sqlite3_last_insert_rowid(dbconn);
		db_last_match_id = id;
	}
	else
		db_error(dbconn, "DB_MatchBegin");

	return id;
}

/*
 * The id DB_MatchBegin last handed out, or -1 if no match has been recorded.
 * The session recorder needs it because it writes from BeginIntermission,
 * which is downstream of the Victory() call that opens the match row but is
 * not the code that holds the id.
 */
int DB_MatchLastId(void)
{
	return db_last_match_id;
}

void DB_MatchRecord(edict_t *player, int match_id, int team)
{
	sqlite3_stmt *res;
	int id;
	int i = 1;
	long samples;

	if (match_id < 0 || !player || !player->client)
		return;
	if (!CTF_TrackStatsFor(player))
		return;	/* an untracked bot leaves no row in the match */
	if (!dbconn && !DB_Conn_Start())
		return;

	// the match row references the player by char_idx, so they need one
	id = DB_GetID(player->client->pers.netname);
	if (id < 0)
		id = DB_NewID(player->client->pers.netname);
	if (id < 0)
		return;

	res = db_stmt(dbconn, &stmt_match_record,
		"INSERT INTO match_players VALUES ("
		"?,?,?,?,"                       // match, char, name, team
		"?,?,?,?,?,?,?,"                 // score frags fragged deaths suicides shots hits
		"?,?,?,"                         // rail shot hit kill
		"?,?,"                           // damage given received
		"?,?,"                           // max_streak sprees
		"?,?,?,?,?,"                     // pickups caps returns fkills drops
		"?,?,?,?,?,"                     // off_kills def_base def_flag def_carrier assists
		"?,?,"                           // cap streak sweeps
		"?,?,?,?,"                       // items
		"?,?,?,?,"                       // runes
		"?,?)");                         // ping_avg playtime
	if (!res)
	{
		db_error(dbconn, "DB_MatchRecord");
		return;
	}

	sqlite3_bind_int (res, i++, match_id);
	sqlite3_bind_int (res, i++, id);
	sqlite3_bind_text(res, i++, player->client->pers.netname, -1, SQLITE_TRANSIENT);
	sqlite3_bind_int (res, i++, team);

	sqlite3_bind_int (res, i++, (int)stats_get(player, STATS_SCORE));
	sqlite3_bind_int (res, i++, (int)stats_get(player, STATS_FRAGS));
	sqlite3_bind_int (res, i++, (int)stats_get(player, STATS_FRAGGED));
	sqlite3_bind_int (res, i++, (int)stats_get(player, STATS_DEATHS));
	sqlite3_bind_int (res, i++, (int)stats_get(player, STATS_SUICIDES));
	sqlite3_bind_int64(res, i++, (sqlite3_int64)stats_get(player, STATS_SHOTS));
	sqlite3_bind_int64(res, i++, (sqlite3_int64)stats_get(player, STATS_SHOTS_HIT));

	sqlite3_bind_int64(res, i++, (sqlite3_int64)stats_get(player, STATS_RAIL_SHOT));
	sqlite3_bind_int64(res, i++, (sqlite3_int64)stats_get(player, STATS_RAIL_HIT));
	sqlite3_bind_int (res, i++, (int)stats_get(player, STATS_RAIL_KILL));

	sqlite3_bind_int64(res, i++, (sqlite3_int64)stats_get(player, STATS_DAMAGE_GIVEN));
	sqlite3_bind_int64(res, i++, (sqlite3_int64)stats_get(player, STATS_DAMAGE_REC));

	sqlite3_bind_int (res, i++, (int)stats_get(player, STATS_MAX_STREAK));
	sqlite3_bind_int (res, i++, (int)stats_get(player, STATS_SPREES));

	sqlite3_bind_int (res, i++, (int)stats_get(player, STATS_OFFENSE_FLAG));
	sqlite3_bind_int (res, i++, (int)stats_get(player, STATS_CAPTURES));
	sqlite3_bind_int (res, i++, (int)stats_get(player, STATS_RETURNS));
	sqlite3_bind_int (res, i++, (int)stats_get(player, STATS_OFFENSE_CARRIER));
	sqlite3_bind_int (res, i++, (int)stats_get(player, STATS_OFFENSE_FLAGLOST));

	sqlite3_bind_int (res, i++, (int)stats_get(player, STATS_OFFENSE_KILLS));
	sqlite3_bind_int (res, i++, (int)stats_get(player, STATS_DEFENSE_BASE));
	sqlite3_bind_int (res, i++, (int)stats_get(player, STATS_DEFENSE_FLAG));
	sqlite3_bind_int (res, i++, (int)stats_get(player, STATS_DEFENSE_CARRIER));
	sqlite3_bind_int (res, i++, (int)stats_get(player, STATS_ASSISTS));

	sqlite3_bind_int (res, i++, (int)stats_get(player, STATS_MAX_CAPSTREAK));
	sqlite3_bind_int (res, i++, (int)stats_get(player, STATS_SWEEPS));

	sqlite3_bind_int (res, i++, (int)stats_get(player, STATS_ITEM_QUAD));
	sqlite3_bind_int (res, i++, (int)stats_get(player, STATS_ITEM_SHIELD));
	sqlite3_bind_int (res, i++, (int)stats_get(player, STATS_ITEM_ARMOR));
	sqlite3_bind_int (res, i++, (int)stats_get(player, STATS_ITEM_MEGA));

	sqlite3_bind_int (res, i++, (int)stats_get(player, STATS_RUNE_STRENGTH));
	sqlite3_bind_int (res, i++, (int)stats_get(player, STATS_RUNE_HASTE));
	sqlite3_bind_int (res, i++, (int)stats_get(player, STATS_RUNE_REGEN));
	sqlite3_bind_int (res, i++, (int)stats_get(player, STATS_RUNE_RESIST));

	// average rather than the raw total, which is meaningless without the
	// sample count travelling with it
	samples = stats_get(player, STATS_PING_SAMPLES);
	sqlite3_bind_int(res, i++,
		samples > 0 ? (int)(stats_get(player, STATS_PING_TOTAL) / samples) : 0);

	sqlite3_bind_int(res, i++,
		(int)((level.framenum - player->client->ctf.original_enterframe) / 10));

	if (sqlite3_step(res) != SQLITE_DONE)
		db_error(dbconn, "DB_MatchRecord");
}

qboolean DB_MatchFinish(int match_id, int red_score, int blue_score,
                        int red_caps, int blue_caps, int winner, int duration)
{
	sqlite3_stmt *res;
	char ended[32];
	qboolean ok = false;

	if (match_id < 0)
		return false;
	if (!dbconn && !DB_Conn_Start())
		return false;

	db_now(ended, sizeof(ended));

	res = db_stmt(dbconn, &stmt_match_finish,
		"UPDATE matches SET ended=?, duration=?, red_score=?, blue_score=?,"
		" red_caps=?, blue_caps=?, winner=? WHERE match_id=?");
	if (!res)
	{
		db_error(dbconn, "DB_MatchFinish");
		return false;
	}

	sqlite3_bind_text(res, 1, ended, -1, SQLITE_TRANSIENT);
	sqlite3_bind_int (res, 2, duration);
	sqlite3_bind_int (res, 3, red_score);
	sqlite3_bind_int (res, 4, blue_score);
	sqlite3_bind_int (res, 5, red_caps);
	sqlite3_bind_int (res, 6, blue_caps);
	sqlite3_bind_int (res, 7, winner);
	sqlite3_bind_int (res, 8, match_id);

	ok = (sqlite3_step(res) == SQLITE_DONE);
	if (!ok)
		db_error(dbconn, "DB_MatchFinish");

	return ok;
}

/* ================================================================= *
 * SLIPGATE session recorder (sg_sessiondb)
 *
 * One row per client per match in sg_session_events -- who was actually on
 * the server, on which side, and what they did while they were there.
 *
 * The counts are read from the per-client session stats array at match end
 * (stats_get), not reconstructed from the sg_eventlog stream: every figure
 * below already has a counter that the game maintains as it happens, and a
 * counter is a better source than a re-parse of the text the server printed.
 * The one exception is chat_lines, which no counter existed for -- see
 * DB_SessionNoteChat.
 *
 * OFF BY DEFAULT. sg_sessiondb gates it, and it also needs the unified
 * backend (ctf_statsdb 2), because a match_id only exists there.
 * ================================================================= */

static cvar_t *sg_sessiondb = NULL;

/*
 * Chat lines spoken this level, one slot per client index. Not part of the
 * STATS_* array on purpose: stats_add routes through Match_CanScore, which
 * refuses to count anything outside a running match, and chat before the
 * countdown ends is still chat somebody typed.
 */
static int sess_chatlines[MAX_CLIENTS];

/* the match already written, so a second call cannot double the attendance */
static int sess_written_match = -1;

static qboolean DB_SessionEnabled(void)
{
	if (!sg_sessiondb)
		sg_sessiondb = sg_cv.sessiondb;
	return sg_sessiondb->value != 0.0f;
}

/* 0-based client index for a client edict, or -1 if it is not one. */
static int sess_client_index(edict_t *ent)
{
	int i;

	if (!ent || !ent->client)
		return -1;

	i = (int)(ent - g_edicts) - 1;
	if (i < 0 || i >= MAX_CLIENTS)
		return -1;

	return i;
}

/*
 * One line said by this client. Called from Cmd_Say_f once the line has
 * cleared the spam check and is going out, so this counts lines SPOKEN --
 * a bot's chat arrives through SG_BotClientCommand -> ClientCommand ->
 * Cmd_Say_f and is counted the same way a human's is.
 *
 * Counting here rather than in SG_cprintf's say path on purpose: the print
 * path fires once per RECIPIENT, so a single line on a full server would
 * score sixteen, and it would score them against the listeners rather than
 * the speaker.
 */
void DB_SessionNoteChat(edict_t *ent)
{
	int i;

	if (!DB_SessionEnabled())
		return;

	i = sess_client_index(ent);
	if (i < 0)
		return;

	sess_chatlines[i]++;
}

/*
 * Called from SpawnEntities at every level change. The chat counters are
 * level scoped like the STATS_* array they sit beside, and the written-match
 * latch is cleared so the next match can record.
 *
 * db_last_match_id goes with them, and that one matters: without it, a level
 * that ends without Victory() opening a match row would leave the previous
 * level's id standing, and the attendance for this match would be filed
 * against the last one.
 */
void DB_SessionNewLevel(void)
{
	memset(sess_chatlines, 0, sizeof(sess_chatlines));
	sess_written_match = -1;
	db_last_match_id = -1;
}

/* the tracked major pickups, as one number */
static int sess_major_items(edict_t *ent)
{
	return (int)(stats_get(ent, STATS_ITEM_QUAD) +
	             stats_get(ent, STATS_ITEM_SHIELD) +
	             stats_get(ent, STATS_ITEM_ARMOR) +
	             stats_get(ent, STATS_ITEM_MEGA) +
	             stats_get(ent, STATS_RUNE_STRENGTH) +
	             stats_get(ent, STATS_RUNE_HASTE) +
	             stats_get(ent, STATS_RUNE_REGEN) +
	             stats_get(ent, STATS_RUNE_RESIST));
}

/*
 * Write the attendance for the match Victory() just recorded.
 *
 * Called from BeginIntermission, which is where every way of ending a level
 * converges and which has already run Victory() by the time we get here --
 * so DB_MatchLastId() names the row this attendance belongs to.
 *
 * Bots: written whatever bot_stats says, flagged is_bot=1. That does not
 * contradict CTF_TrackStatsFor, which exists to keep untracked bots out of
 * the char_idx tables -- this table is keyed on the name and takes char_idx
 * only when the player already had one, so nothing is allocated for a bot
 * the server declined to track and no leaderboard reading match_players sees
 * a bot appear. A query over this table that wants humans only says
 * "WHERE is_bot = 0".
 *
 * Not converted to db_stmt(): this function is where the prepare-once,
 * reset-and-rebind-per-row idiom that db_stmt() generalizes was proven in
 * the first place (see the banner in ctf_sqlite_core.h). It already
 * prepares INSERT INTO sg_session_events once per call and resets it for
 * every client row in the loop below; the only difference between that and
 * db_stmt() is that this prepare is scoped to one call (finalized at the
 * end) rather than cached for the connection's whole life. Moving it onto
 * db_stmt() would change when the statement gets finalized without
 * changing anything about what gets written, which is exactly the kind of
 * edit STYLE rule 9 says stays out of a behavior-neutral pass -- so this
 * stays as the pattern's origin rather than becoming its 24th call site.
 *
 * Returns the number of rows written.
 */
int DB_SessionRecord(void)
{
	sqlite3_stmt *res = NULL;
	int   match_id;
	int   i, rows = 0;
	qboolean intrans;
	time_t now;
	char  left_at[32];

	if (!DB_SessionEnabled())
		return 0;
	if (CTF_StatsDBMode() != CTF_STATSDB_UNIFIED)
		return 0;

	match_id = DB_MatchLastId();
	if (match_id < 0)
		return 0;			/* no match row to hang the session off */
	if (match_id == sess_written_match)
		return 0;			/* already written for this match */

	if (!dbconn && !DB_Conn_Start())
		return 0;

	if (sqlite3_prepare_v2(dbconn,
			"INSERT INTO sg_session_events VALUES ("
			"?,?,?,?,?,"			// match char name is_bot team
			"?,?,?,"			// caps steals returns
			"?,?,"				// kills deaths
			"?,?,"				// damage given taken
			"?,?,"				// major items, chat lines
			"?,?)",				// joined_at left_at
			-1, &res, NULL) != SQLITE_OK)
	{
		db_error(dbconn, "DB_SessionRecord");
		return 0;
	}

	now = time(NULL);
	db_stamp(left_at, sizeof(left_at), now);

	/*
	 * One transaction for the whole roster: sixteen separate commits at the
	 * moment a level changes is sixteen fsyncs the server has to wait out.
	 * If BEGIN is refused -- something else already has one open -- the rows
	 * still go in one at a time, so the only thing lost is the speed.
	 */
	intrans = db_begin(dbconn);

	for (i = 0; i < game.maxclients; i++)
	{
		edict_t *ent = g_edicts + 1 + i;
		char     joined_at[32];
		long     onserver;
		int      b = 1;

		if (!ent->inuse || !ent->client)
			continue;

		/*
		 * How long they had been here, in seconds, from the frame the client
		 * entered the game. Clamped at zero: a client that entered on this
		 * very frame would otherwise round to a negative session.
		 */
		onserver = (long)(level.framenum - ent->client->ctf.original_enterframe) / 10;
		if (onserver < 0)
			onserver = 0;
		db_stamp(joined_at, sizeof(joined_at), now - (time_t)onserver);

		sqlite3_bind_int (res, b++, match_id);
		sqlite3_bind_int (res, b++, DB_GetID(ent->client->pers.netname));
		sqlite3_bind_text(res, b++, ent->client->pers.netname, -1, SQLITE_TRANSIENT);
		sqlite3_bind_int (res, b++, (ent->flags & FL_BOT) ? 1 : 0);
		sqlite3_bind_int (res, b++, ent->client->ctf.teamnum);

		sqlite3_bind_int (res, b++, (int)stats_get(ent, STATS_CAPTURES));
		sqlite3_bind_int (res, b++, (int)stats_get(ent, STATS_OFFENSE_FLAG));
		sqlite3_bind_int (res, b++, (int)stats_get(ent, STATS_RETURNS));

		sqlite3_bind_int (res, b++, (int)stats_get(ent, STATS_FRAGS));
		sqlite3_bind_int (res, b++, (int)stats_get(ent, STATS_DEATHS));

		sqlite3_bind_int64(res, b++, (sqlite3_int64)stats_get(ent, STATS_DAMAGE_GIVEN));
		sqlite3_bind_int64(res, b++, (sqlite3_int64)stats_get(ent, STATS_DAMAGE_REC));

		sqlite3_bind_int (res, b++, sess_major_items(ent));
		sqlite3_bind_int (res, b++, sess_chatlines[i]);

		sqlite3_bind_text(res, b++, joined_at, -1, SQLITE_TRANSIENT);
		sqlite3_bind_text(res, b++, left_at, -1, SQLITE_TRANSIENT);

		if (sqlite3_step(res) == SQLITE_DONE)
			rows++;
		else
			db_error(dbconn, "DB_SessionRecord");

		sqlite3_reset(res);
		sqlite3_clear_bindings(res);
	}

	if (intrans)
		db_commit(dbconn);
	sqlite3_finalize(res);

	sess_written_match = match_id;

	if (rows)
		gi.dprintf("session db: match %i, %i client rows\n", match_id, rows);

	return rows;
}
