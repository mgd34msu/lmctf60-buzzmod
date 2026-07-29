// ctf_file_io.c -- path handling and backend dispatch for the stats database.
//
// Reconstructed 2026-07. The original was compiled on 2020-07-28 but never
// committed; only Debug/ctf_file_io.obj survived. Function names, the
// "<name>.ctf" path shape and the operator messages below are taken from that
// object's symbol and string tables. The bodies are new.
//
// Differences from the 2020 original, all deliberate:
//   - player names are sanitised before they reach the filesystem. The original
//     pasted the raw netname into the path, so a name containing '/' or ".."
//     wrote outside the players directory.
//   - every buffer is bounded and every truncation is reported.

#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdio.h>

#ifdef _WIN32
#include <direct.h>
#define ctf_mkdir(p)	_mkdir(p)
#define CTF_PATHSEP		'\\'
#else
#include <unistd.h>
#define ctf_mkdir(p)	mkdir((p), 0755)
#define CTF_PATHSEP		'/'
#endif

#include "g_local.h"
#include "ctf_file_io.h"
#include "ctf_sqlite_player.h"
#include "ctf_sqlite_unidb.h"

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

qboolean CommitPlayerData(edict_t *ent)
{
	char path[CTF_MAX_DBPATH];

	if (!ent || !ent->client)
	{
		gi.dprintf("ERROR: entity not a client!! (%s)\n",
			(ent && ent->classname) ? ent->classname : "null");
		return false;
	}

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
