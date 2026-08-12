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

// Builds "<gamedir>/<name>" for an admin-supplied output filename. Rejects
// anything with a path separator or "..", so "sv statsdb export" cannot be
// talked into writing outside the game directory.
qboolean ctf_safe_output_path(const char *name, char *out, size_t outsize);

// mkdir if absent. Returns true when the directory exists on return.
qboolean CreateDirIfNotExists(const char *path);

// Opens (and if needed creates) whichever backend ctf_statsdb selects, at
// server start rather than on the first player event, so a bad path or a
// permission problem shows up in the console instead of mid-match.
void CTF_StatsDB_Init(void);

// "sv statsdb ..." console command.
void CTF_StatsDB_Command(void);

// "cmd lifetime [name]" -- a player's persisted totals, as opposed to "cmd
// stats", which only ever shows the current level.
void Cmd_Lifetime_f(edict_t *ent);

// "cmd rank [column] [n]" -- leaderboard for players, not just admins.
void Cmd_Rank_f(edict_t *ent);

// "cmd card [name]" -- one player's lifetime line, read straight from the
// unified database (works for anyone it has ever recorded, not just
// someone currently connected). Defaults to the asker when name is
// omitted. Console print stream, request-driven (docs/LAYOUT.md).
void Cmd_Card_f(edict_t *ent);

// "cmd vs <name>" -- the asker against one named opponent, across only the
// matches they both appeared in. Console print stream, request-driven.
void Cmd_VS_f(edict_t *ent);

// Front door used by the game code. Dispatches on ctf_statsdb.
qboolean CTF_TrackStatsFor(edict_t *ent);
qboolean CommitPlayerData(edict_t *ent);
qboolean LoadPlayerData(edict_t *ent);

#endif
