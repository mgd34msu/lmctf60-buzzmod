// Stats database path handling and backend dispatch.

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _WIN32
#include <direct.h>
#define ctf_mkdir(p)	_mkdir(p)
#define CTF_PATHSEP		'\\'
// MSVC's sys/stat.h defines S_IFDIR but not the S_ISDIR test macro
#ifndef S_ISDIR
#define S_ISDIR(m)		(((m) & S_IFMT) == S_IFDIR)
#endif
#else
#include <unistd.h>
#define ctf_mkdir(p)	mkdir((p), 0755)
#define CTF_PATHSEP		'/'
#endif

#include "g_local.h"
#include "g_ctffunc.h"
#include "ctf_file_io.h"
#include "ctf_sqlite_player.h"
#include "ctf_sqlite_unidb.h"

/* bot_stats controls whether SG bots reach the leaderboards. */
static qboolean BotStatsEnabled(void)
{
	cvar_t *c = gi.cvar("bot_stats", "0", 0);
	return (c && c->value) ? true : false;
}

static cvar_t *ctf_statsdb = NULL;

int CTF_StatsDBMode(void)
{
	if (!ctf_statsdb)
		ctf_statsdb = gi.cvar("ctf_statsdb", "0", CVAR_ARCHIVE);

	switch ((int)ctf_statsdb->value)
	{
	case CTF_STATSDB_PERPLAYER:	return CTF_STATSDB_PERPLAYER;
	case CTF_STATSDB_UNIFIED:	return CTF_STATSDB_UNIFIED;
	default:					return CTF_STATSDB_OFF;
	}
}

void CTF_FormatFileName(const char *playername, char *out, size_t outsize)
{
	size_t i = 0;

	if (!out || outsize == 0)
		return;

	if (playername)
	{
		for (; playername[i] != '\0' && i < outsize - 1; i++)
		{
			char c = playername[i];

			if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
				(c >= '0' && c <= '9') || c == '_' || c == '-')
				out[i] = c;
			else
				out[i] = '_';
		}
	}

	// never hand back an empty name -- it would produce "<gamedir>/players/.ctf"
	if (i == 0)
	{
		strncpy(out, "unnamed", outsize - 1);
		out[outsize - 1] = '\0';
		return;
	}

	out[i] = '\0';
}

qboolean ctf_get_player_file_path(const char *playername, char *out, size_t outsize)
{
	char safename[CTF_MAX_DBNAME];
	char dir[CTF_MAX_DBPATH];
	int written;

	if (!out || outsize == 0)
		return false;

	CTF_FormatFileName(playername, safename, sizeof(safename));

	written = snprintf(dir, sizeof(dir), "%s%cplayers", gamedir->string, CTF_PATHSEP);
	if (written < 0 || (size_t)written >= sizeof(dir))
	{
		gi.dprintf("ctf_get_player_file_path: game path too long.\n");
		return false;
	}

	if (!CreateDirIfNotExists(dir))
		return false;

	written = snprintf(out, outsize, "%s%c%s.ctf", dir, CTF_PATHSEP, safename);
	if (written < 0 || (size_t)written >= outsize)
	{
		gi.dprintf("ctf_get_player_file_path: path too long for %s.\n", safename);
		return false;
	}

	return true;
}

qboolean ctf_get_unified_db_path(char *out, size_t outsize)
{
	int written;

	if (!out || outsize == 0)
		return false;

	written = snprintf(out, outsize, "%s%cplayers.db", gamedir->string, CTF_PATHSEP);
	if (written < 0 || (size_t)written >= outsize)
	{
		gi.dprintf("ctf_get_unified_db_path: path too long.\n");
		return false;
	}

	return true;
}

qboolean CreateDirIfNotExists(const char *path)
{
	struct stat st;

	if (!path || path[0] == '\0')
		return false;

	if (stat(path, &st) == 0)
		return S_ISDIR(st.st_mode) ? true : false;

	if (ctf_mkdir(path) == 0)
	{
		gi.dprintf("Created directory %s.\n", path);
		return true;
	}

	// someone else may have won the race between stat and mkdir
	if (errno == EEXIST && stat(path, &st) == 0 && S_ISDIR(st.st_mode))
		return true;

	gi.dprintf("Error creating missing directory %s.\n", path);
	return false;
}

void CTF_StatsDB_Init(void)
{
	char dir[CTF_MAX_DBPATH];
	int written;

	switch (CTF_StatsDBMode())
	{
	case CTF_STATSDB_UNIFIED:
		if (DB_Conn_Start())
			gi.dprintf("stats db: unified backend ready.\n");
		else
			gi.dprintf("stats db: unified backend could not be opened, "
				"stats will not persist.\n");
		break;

	case CTF_STATSDB_PERPLAYER:
		written = snprintf(dir, sizeof(dir), "%s%cplayers", gamedir->string, CTF_PATHSEP);

		if (written > 0 && (size_t)written < sizeof(dir) && CreateDirIfNotExists(dir))
			gi.dprintf("stats db: per-player backend ready (%s).\n", dir);
		else
			gi.dprintf("stats db: per-player directory unavailable, "
				"stats will not persist.\n");
		break;

	default:
		gi.dprintf("stats db: disabled (ctf_statsdb 0).\n");
		break;
	}
}

/*
 * Whether this client's stats are persisted at all.
 *
 * Bots are excluded unless the server asks for them: a server running bots to
 * fill out a game does not want them in the leaderboards, but one that plays
 * bots seriously might. Set "bot_stats 1" (or use the bot menu) to include them.
 */
qboolean CTF_TrackStatsFor(edict_t *ent)
{
	if (!ent || !ent->client)
		return false;
	if (ent->flags & FL_BOT)
		return BotStatsEnabled();
	return true;
}

qboolean CommitPlayerData(edict_t *ent)
{
	char path[CTF_MAX_DBPATH];

	if (!ent || !ent->client)
	{
		gi.dprintf("ERROR: entity not a client!! (%s)\n",
			(ent && ent->classname) ? ent->classname : "null");
		return false;
	}

	if (!CTF_TrackStatsFor(ent))
		return true;	/* nothing to write, and that is not a failure */

	// roll this session's counters into the lifetime totals before writing
	stats_fold_session(ent);

	switch (CTF_StatsDBMode())
	{
	case CTF_STATSDB_PERPLAYER:
		if (!ctf_get_player_file_path(ent->client->pers.netname, path, sizeof(path)))
			return false;
		gi.dprintf("savePlayer called to save: %s\n", ent->client->pers.netname);
		return CTF_SavePlayer(ent, path, ent->client->pers.netname);

	case CTF_STATSDB_UNIFIED:
		return DB_SavePlayer(ent);

	default:
		return true;	// backend disabled, nothing to do
	}
}

qboolean LoadPlayerData(edict_t *ent)
{
	char path[CTF_MAX_DBPATH];

	if (!ent || !ent->client)
	{
		gi.dprintf("ERROR: entity not a client!! (%s)\n",
			(ent && ent->classname) ? ent->classname : "null");
		return false;
	}

	/* Do not create a userdata row for a bot we are not tracking. */
	if (!CTF_TrackStatsFor(ent))
		return false;

	switch (CTF_StatsDBMode())
	{
	case CTF_STATSDB_PERPLAYER:
		if (!ctf_get_player_file_path(ent->client->pers.netname, path, sizeof(path)))
			return false;
		gi.dprintf("openPlayer called to open: %s\n", ent->client->pers.netname);
		return CTF_LoadPlayer(ent, path);

	case CTF_STATSDB_UNIFIED:
		return DB_LoadPlayer(ent);

	default:
		return true;
	}
}

/*
==================
CTF_StatsDB_Command

Backs "sv statsdb <subcommand>".
==================
*/
void CTF_StatsDB_Command(void)
{
	const char *sub = gi.argv(2);
	int mode = CTF_StatsDBMode();

	if (!sub || sub[0] == '\0' || Q_stricmp(sub, "help") == 0)
	{
		gi.cprintf(NULL, PRINT_HIGH,
			"sv statsdb status              backend, path and row counts\n"
			"sv statsdb flush               write every connected player now\n"
			"sv statsdb top <column> [n]    leaderboard, default 10, max 50\n"
			"sv statsdb player <name>       one player's stored record\n"
			"sv statsdb export <file>       tab-separated dump, written to the game dir\n"
			"sv statsdb backup <file>       safe copy of the live database\n"
			"sv statsdb rename <old> <new>  relabel, or fold one record into another\n"
			"sv statsdb prune <days> confirm  drop players not seen in that long\n"
			"sv statsdb reset confirm       delete every row (cannot be undone)\n");
		return;
	}

	if (mode == CTF_STATSDB_OFF)
	{
		gi.cprintf(NULL, PRINT_HIGH,
			"statsdb: disabled. Set ctf_statsdb 1 (per-player) or 2 (unified).\n");
		return;
	}

	// flush works in either mode -- it just commits whoever is connected
	if (Q_stricmp(sub, "flush") == 0)
	{
		int i, n = 0;

		for (i = 0; i < game.maxclients; i++)
		{
			edict_t *ent = g_edicts + 1 + i;

			if (!ent->inuse || !ent->client || !ent->client->pers.connected)
				continue;

			if (CommitPlayerData(ent))
				n++;
		}

		gi.cprintf(NULL, PRINT_HIGH, "statsdb: wrote %d player(s).\n", n);
		return;
	}

	if (mode != CTF_STATSDB_UNIFIED)
	{
		gi.cprintf(NULL, PRINT_HIGH,
			"statsdb: \"%s\" needs the unified backend (ctf_statsdb 2). The "
			"per-player files hold one record each and cannot be queried across "
			"players.\n", sub);
		return;
	}

	if (Q_stricmp(sub, "status") == 0)
	{
		DB_Status();
	}
	else if (Q_stricmp(sub, "top") == 0)
	{
		int count = 10;

		if (gi.argc() < 4)
		{
			gi.cprintf(NULL, PRINT_HIGH, "Usage: sv statsdb top <column> [n]\n");
			return;
		}
		if (gi.argc() >= 5)
			count = atoi(gi.argv(4));

		DB_Top(gi.argv(3), count);
	}
	else if (Q_stricmp(sub, "player") == 0)
	{
		if (gi.argc() < 4)
		{
			gi.cprintf(NULL, PRINT_HIGH, "Usage: sv statsdb player <name>\n");
			return;
		}

		DB_PrintPlayer(gi.argv(3));
	}
	else if (Q_stricmp(sub, "export") == 0 || Q_stricmp(sub, "backup") == 0)
	{
		char path[CTF_MAX_DBPATH];

		if (gi.argc() < 4)
		{
			gi.cprintf(NULL, PRINT_HIGH, "Usage: sv statsdb %s <filename>\n", sub);
			return;
		}

		if (!ctf_safe_output_path(gi.argv(3), path, sizeof(path)))
			return;

		if (Q_stricmp(sub, "export") == 0)
			DB_Export(path);
		else
			DB_Backup(path);
	}
	else if (Q_stricmp(sub, "rename") == 0)
	{
		if (gi.argc() < 5)
		{
			gi.cprintf(NULL, PRINT_HIGH, "Usage: sv statsdb rename <oldname> <newname>\n");
			return;
		}

		DB_RenamePlayer(gi.argv(3), gi.argv(4));
	}
	else if (Q_stricmp(sub, "prune") == 0)
	{
		int days;

		if (gi.argc() < 4)
		{
			gi.cprintf(NULL, PRINT_HIGH, "Usage: sv statsdb prune <days> confirm\n");
			return;
		}

		days = atoi(gi.argv(3));
		if (days < 1)
		{
			gi.cprintf(NULL, PRINT_HIGH, "statsdb: <days> must be 1 or more.\n");
			return;
		}

		if (gi.argc() < 5 || Q_stricmp(gi.argv(4), "confirm") != 0)
		{
			gi.cprintf(NULL, PRINT_HIGH,
				"statsdb: this permanently drops every player not seen in %d day(s).\n"
				"         Type: sv statsdb prune %d confirm\n", days, days);
			return;
		}

		{
			int dropped = DB_Prune(days);

			if (dropped < 0)
				gi.cprintf(NULL, PRINT_HIGH, "statsdb: prune failed.\n");
			else
				gi.cprintf(NULL, PRINT_HIGH, "statsdb: dropped %d player(s).\n", dropped);
		}
	}
	else if (Q_stricmp(sub, "reset") == 0)
	{
		// deliberately awkward: this throws away every recorded stat
		if (gi.argc() < 4 || Q_stricmp(gi.argv(3), "confirm") != 0)
		{
			gi.cprintf(NULL, PRINT_HIGH,
				"statsdb: this deletes every stored stat and cannot be undone.\n"
				"         Type: sv statsdb reset confirm\n");
			return;
		}

		if (!DB_Reset())
			gi.cprintf(NULL, PRINT_HIGH, "statsdb: reset failed.\n");
	}
	else
	{
		gi.cprintf(NULL, PRINT_HIGH,
			"statsdb: unknown subcommand \"%s\". Try: sv statsdb help\n", sub);
	}
}

/*
==================
Cmd_Lifetime_f

"cmd lifetime [name]" -- the persisted totals. "cmd stats" only ever showed the
current level, so before this there was no way for a player to see the numbers
the database had been accumulating.

Reads client->ctfstats, which ClientBegin loads and stats_fold_session tops up,
so it needs no database round trip. The figures shown are the stored totals plus
whatever this level has added so far.
==================
*/
/*
==================
Cmd_Rank_f

"cmd rank [column] [n]" -- the leaderboard, in game, without needing an admin
at the console. Same whitelist as "sv statsdb top".
==================
*/
void Cmd_Rank_f(edict_t *ent)
{
	char buf[1024];
	const char *field = "frags";
	int count = 10;

	if (!ent || !ent->client)
		return;

	if (CTF_StatsDBMode() != CTF_STATSDB_UNIFIED)
	{
		ctf_SafePrint(ent, PRINT_HIGH,
			"Rankings need the unified stats database on this server.\n");
		return;
	}

	if (gi.argc() >= 2)
		field = gi.argv(1);
	if (gi.argc() >= 3)
		count = atoi(gi.argv(2));

	if (count < 1)
		count = 10;
	if (count > 15)
		count = 15;			// keep it inside ctf_SafePrint's per-call budget

	if (!DB_TopFormat(field, count, buf, sizeof(buf)))
	{
		ctf_SafePrint(ent, PRINT_HIGH,
			"Unknown column. Try: frags, flag_captures, sweeps, max_streak, "
			"max_cap_streak, assists, flag_returns.\n");
		return;
	}

	ctf_SafePrint(ent, PRINT_HIGH, buf);
}

void Cmd_Lifetime_f(edict_t *ent)
{
	playerstats_t *ps;
	edict_t *target = ent;
	char buf[MAX_INFO_STRING];
	char *arg;

	if (!ent || !ent->client)
		return;

	arg = gi.args();

	if (arg && strlen(arg))
	{
		char lowerarg[MAX_INFO_STRING];
		char lowername[MAX_INFO_STRING];
		int i;

		strncpy(lowerarg, arg, sizeof(lowerarg) - 1);
		lowerarg[sizeof(lowerarg) - 1] = 0;
		LowerCase(lowerarg);

		target = NULL;

		for (i = 0; i < game.maxclients; i++)
		{
			edict_t *temp = g_edicts + 1 + i;

			if (!temp->inuse || !temp->client || !temp->client->pers.connected)
				continue;

			strncpy(lowername, temp->client->pers.netname, sizeof(lowername) - 1);
			lowername[sizeof(lowername) - 1] = 0;
			LowerCase(lowername);

			if (strstr(lowername, lowerarg))
			{
				target = temp;
				break;
			}
		}

		if (!target)
		{
			ctf_SafePrint(ent, PRINT_HIGH, "Cannot find a matching player.\n");
			return;
		}
	}

	if (CTF_StatsDBMode() == CTF_STATSDB_OFF)
	{
		ctf_SafePrint(ent, PRINT_HIGH, "Lifetime stats are not being recorded on this server.\n");
		return;
	}

	ps = &target->client->ctfstats;

	Com_sprintf(buf, sizeof(buf),
		"\n--LIFETIME: %s\n"
		"Member since %s\n"
		"Frags=%u Fragged=%u Suicides=%d\n"
		"Best Streak=%d Sprees=%u\n"
		"Caps=%d Pickups=%d Returns=%d FC Kills=%d\n"
		"Off Kills=%d Def Kills=%d Assists=%d\n"
		"Best Cap Streak=%d Sweeps=%d\n"
		"Playtime=%d min\n",
		target->client->pers.netname,
		ps->member_since[0] ? ps->member_since : "(this session)",
		ps->frags, ps->fragged, ps->suicides,
		ps->max_streak, ps->num_sprees,
		ps->flag_captures, ps->flag_pickups, ps->flag_returns, ps->flag_kills,
		ps->offense_kills, ps->defense_kills, ps->assists,
		ps->max_cap_streak, ps->sweeps,
		ps->total_playtime);

	ctf_SafePrint(ent, PRINT_HIGH, buf);
}

/*
==================
Cmd_Card_f

"cmd card [name]" -- one player's lifetime line, read straight from the
unified database via DB_PlayerCard (name resolved exact then case-
insensitive, ctf_sqlite_unidb.c's db_resolve_id). Unlike Cmd_Lifetime_f
above, this works for anyone the database has ever recorded, not only
someone currently connected -- there is no client-side substring match
against the roster here, because there may be no matching client at all.
==================
*/
void Cmd_Card_f(edict_t *ent)
{
	db_card_t	card;
	char		buf[512];
	char		accbuf[16];
	const char	*name;

	if (!ent || !ent->client)
		return;

	if (CTF_StatsDBMode() != CTF_STATSDB_UNIFIED)
	{
		ctf_SafePrint(ent, PRINT_HIGH,
			"Player cards need the unified stats database on this server.\n");
		return;
	}

	name = gi.args();
	if (!name || !name[0])
		name = ent->client->pers.netname;

	if (!DB_PlayerCard(name, &card))
	{
		ctf_SafePrint(ent, PRINT_HIGH, "No recorded games for that player.\n");
		return;
	}

	if (card.shots > 0)
		Com_sprintf(accbuf, sizeof(accbuf), "%ld%%", 100 * card.shots_hit / card.shots);
	else
		strcpy(accbuf, "n/a");

	Com_sprintf(buf, sizeof(buf),
		"\n--CARD: %s\n"
		"Member since %s, last played %s\n"
		"Games=%d Caps=%d Steals=%d Returns=%d Frags=%d\n"
		"Accuracy=%s\n",
		card.playername,
		card.member_since[0] ? card.member_since : "(unknown)",
		card.last_played[0] ? card.last_played : "(unknown)",
		card.games, card.caps, card.steals, card.returns, card.frags,
		accbuf);

	ctf_SafePrint(ent, PRINT_HIGH, buf);
}

/*
==================
Cmd_VS_f

"cmd vs <name>" -- the asker against one named opponent, scored only over
the matches they both actually appeared in (DB_HeadToHead's match_players
self-join). A name argument is required: there is no meaningful default
opponent the way "cmd card" defaults to the asker.
==================
*/
void Cmd_VS_f(edict_t *ent)
{
	db_h2h_t	h2h;
	char		buf[512];
	const char	*name;

	if (!ent || !ent->client)
		return;

	if (CTF_StatsDBMode() != CTF_STATSDB_UNIFIED)
	{
		ctf_SafePrint(ent, PRINT_HIGH,
			"Head-to-head needs the unified stats database on this server.\n");
		return;
	}

	name = gi.args();
	if (!name || !name[0])
	{
		ctf_SafePrint(ent, PRINT_HIGH, "Usage: vs <name>\n");
		return;
	}

	// Covers three cases the same way on purpose: either name failed to
	// resolve, or they resolved but never share a recorded match -- either
	// way there is no honest comparison to print, only whether one exists.
	if (!DB_HeadToHead(ent->client->pers.netname, name, &h2h) || h2h.games == 0)
	{
		ctf_SafePrint(ent, PRINT_HIGH, "No recorded games together.\n");
		return;
	}

	Com_sprintf(buf, sizeof(buf),
		"\n--HEAD TO HEAD: %s vs %s\n"
		"Shared games=%d\n"
		"Caps: you %d, them %d (you out-capped them %d, they out-capped you %d)\n"
		"Frags: you %d, them %d\n",
		ent->client->pers.netname, h2h.opponent_name, h2h.games,
		h2h.my_caps, h2h.their_caps, h2h.my_cap_wins, h2h.their_cap_wins,
		h2h.my_frags, h2h.their_frags);

	ctf_SafePrint(ent, PRINT_HIGH, buf);
}

qboolean ctf_safe_output_path(const char *name, char *out, size_t outsize)
{
	int written;

	if (!name || name[0] == '\0' || !out || outsize == 0)
		return false;

	// no directory traversal, no absolute paths, no separators of either flavour
	if (strchr(name, '/') || strchr(name, '\\') || strstr(name, ".."))
	{
		gi.cprintf(NULL, PRINT_HIGH,
			"statsdb: \"%s\" must be a plain filename, no path.\n", name);
		return false;
	}

	written = snprintf(out, outsize, "%s%c%s", gamedir->string, CTF_PATHSEP, name);

	if (written < 0 || (size_t)written >= outsize)
	{
		gi.cprintf(NULL, PRINT_HIGH, "statsdb: path too long.\n");
		return false;
	}

	return true;
}
