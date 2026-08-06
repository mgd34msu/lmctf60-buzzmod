#ifndef CTF_SQLITE_UNIDB_H
#define CTF_SQLITE_UNIDB_H

// Unified backend: one <gamedir>/players.db shared by every player, all four
// tables keyed on char_idx.
//
// Function names are those recovered from Debug/ctf_sqlite_unidb.obj.

qboolean DB_Conn_Start(void);		// open the shared handle, build schema if new
void     DB_Conn_Cleanup(void);		// close it; safe to call when never opened

int      DB_GetID(const char *playername);	// existing char_idx, or -1
int      DB_NewID(const char *playername);	// allocate a char_idx with base rows

// admin surface, unified backend only (per-player files have no cross-player view)
qboolean DB_Reset(void);                                  // wipe every row
void     DB_Status(void);                                 // path, size, row counts
void     DB_Top(const char *field, int count);            // leaderboard for one column
qboolean DB_PrintPlayer(const char *playername);          // one player's record
qboolean DB_TopFormat(const char *field, int count, char *out, size_t outsize);
qboolean DB_Export(const char *path);                     // TSV dump
qboolean DB_Backup(const char *path);                     // live-safe file copy
qboolean DB_RenamePlayer(const char *oldname, const char *newname);
int      DB_Prune(int days);                              // rows dropped, or -1

qboolean DB_LoadPlayer(edict_t *player);
qboolean DB_SavePlayer(edict_t *player);

/*
 * Match history.
 *
 * A row per match plus a row per player per match, which is what makes recent
 * form, per-map performance and trends possible. Lifetime totals alone can only
 * ever answer "how good are they", never "how did last night go".
 *
 * Unified backend only: a per-player file has nowhere sensible to put a match
 * that several people played in.
 */
int      DB_MatchBegin(const char *mapname);   // new match row, returns match_id
void     DB_MatchRecord(edict_t *player, int match_id, int team);
qboolean DB_MatchFinish(int match_id, int red_score, int blue_score,
                        int red_caps, int blue_caps, int winner, int duration);
int      DB_MatchLastId(void);                 // id DB_MatchBegin last handed out, or -1

/*
 * SLIPGATE session recorder -- sg_session_events, one row per client per
 * match. The attendance record beside the leaderboard: bots included and
 * flagged is_bot, names that never earned a char_idx included too.
 *
 * Gated on the sg_sessiondb cvar (default 0) and on the unified backend
 * (ctf_statsdb 2); with either off, every entry point below returns at once.
 */
void     DB_SessionNewLevel(void);             // SpawnEntities: clear per-level counters
void     DB_SessionNoteChat(edict_t *ent);     // Cmd_Say_f: one line spoken by this client
int      DB_SessionRecord(void);               // BeginIntermission: write the rows

#endif
