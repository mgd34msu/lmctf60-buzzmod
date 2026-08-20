#ifndef CTF_FILE_IO_H
#define CTF_FILE_IO_H

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

// Opens the configured backend during server startup.
void CTF_StatsDB_Init(void);

// "sv statsdb ..." console command.
void CTF_StatsDB_Command(void);

// "cmd lifetime [name]" -- persisted player totals.
void Cmd_Lifetime_f(edict_t *ent);

// "cmd rank [column] [n]" -- leaderboard for players, not just admins.
void Cmd_Rank_f(edict_t *ent);

// "cmd card [name]" -- one player's lifetime summary.
void Cmd_Card_f(edict_t *ent);

// "cmd vs <name>" -- head-to-head totals for shared matches.
void Cmd_VS_f(edict_t *ent);

// Game-facing dispatch through the configured stats backend.
qboolean CTF_TrackStatsFor(edict_t *ent);
qboolean CommitPlayerData(edict_t *ent);
qboolean LoadPlayerData(edict_t *ent);

#endif
