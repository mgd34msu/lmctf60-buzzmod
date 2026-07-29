#ifndef CTF_FILE_IO_H
#define CTF_FILE_IO_H

// Reconstructed 2026-07 from the surviving Debug/ctf_file_io.obj symbol and
// string tables. The original ctf_file_io.c was never committed and is lost.

#define CTF_MAX_DBPATH		512
#define CTF_MAX_DBNAME		 64

// Which stats backend is active. Selected by the "ctf_statsdb" cvar:
//   0  off        - nothing is read or written
//   1  perplayer  - one <gamedir>/players/<name>.ctf database per player
//   2  unified    - a single <gamedir>/players.db keyed on char_idx
#define CTF_STATSDB_OFF			0
#define CTF_STATSDB_PERPLAYER	1
#define CTF_STATSDB_UNIFIED		2

int  CTF_StatsDBMode(void);

// Turns a raw player name into something safe to put in a filename.
// Anything outside [A-Za-z0-9_-] becomes '_'. Never returns an empty string.
void CTF_FormatFileName(const char *playername, char *out, size_t outsize);

// Builds "<gamedir>/players/<safename>.ctf". Returns false if it would not fit.
qboolean ctf_get_player_file_path(const char *playername, char *out, size_t outsize);

// Builds "<gamedir>/players.db" for the unified backend.
qboolean ctf_get_unified_db_path(char *out, size_t outsize);

// mkdir if absent. Returns true when the directory exists on return.
qboolean CreateDirIfNotExists(const char *path);

// Front door used by the game code. Dispatches on ctf_statsdb.
qboolean CommitPlayerData(edict_t *ent);
qboolean LoadPlayerData(edict_t *ent);

#endif
