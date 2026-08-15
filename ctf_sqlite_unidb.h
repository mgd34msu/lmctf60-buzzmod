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

/*
 * Settled-board queries (ui_boards.c). Both read match_players/matches --
 * the per-game rows DB_MatchRecord/DB_MatchFinish already write -- plus the
 * lifetime tables, never inventing a table of their own. Unified backend
 * only, same as everything else above the session recorder; a caller on
 * the per-player backend gets zero rows back rather than a crash.
 */

// One row per player, summed over matches whose matches.started falls in
// the rolling 30-day season window, players with fewer than min_games
// dropped. Sorted caps DESC, capped at max_rows. Returns rows filled (0 if
// the backend is not open, no match falls in the window, or nobody clears
// min_games).
typedef struct
{
	char	name[16];	// matches gclient_t.pers.netname's declared size
	int	caps;
	int	steals;		// flag pickups
	int	railkills;
	int	games;
} db_season_row_t;

int DB_SeasonTop(db_season_row_t *out, int max_rows, int min_games);

// One all-time record: the holder's name as recorded on the row that set
// it, and the value. holder[0] == 0 means no qualifying row exists yet.
typedef struct
{
	char	holder[16];
	int	value;
} db_record_t;

typedef struct
{
	db_record_t	most_caps_game;		// match_players.flag_captures
	db_record_t	most_railkills_game;	// match_players.rail_kill
	db_record_t	best_streak_game;	// match_players.max_streak
	db_record_t	most_returns_game;	// match_players.flag_returns
	db_record_t	most_caps_lifetime;	// ctf_stats.flag_captures
	db_record_t	longest_played_lifetime;	// userdata.playtime_total, minutes
} db_server_records_t;

qboolean DB_ServerRecords(db_server_records_t *out);

// One row per player, summed over match_players/matches rows whose
// matches.started falls in the rolling 7-day window -- the busiest
// players lately, by games played and total time played. Sorted games
// DESC (ties broken by minutes DESC), capped at max_rows. Returns rows
// filled (0 if the backend is not open or nobody played in the window).
typedef struct
{
	char	name[16];	// matches gclient_t.pers.netname's declared size
	int	games;
	int	minutes;	// match_players.playtime is seconds; converted on read
} db_activity_row_t;

int DB_Activity(db_activity_row_t *out, int max_rows);

// One row per player: captures in the last 7 days against matches.started,
// and captures in the 23 days before that (days 8-30 ago) -- the same
// rolling 30-day season window DB_SeasonTop uses, split at the 7-day mark.
// Both halves also carry the games played in that half, so the caller can
// turn counts into a per-game rate without a second query. Only players
// with at least min_recent_games games in the last 7 days qualify; up to
// max_rows candidate rows are returned, in no particular order -- ranking
// by "recent rate exceeds older rate" is the caller's job (ui_boards.c),
// since that ranking is a derived quantity, not something ORDER BY can
// safely express without repeating the whole CASE expression twice more.
typedef struct
{
	char	name[16];
	int	recent_caps;
	int	recent_games;
	int	older_caps;
	int	older_games;
} db_momentum_row_t;

int DB_Momentum(db_momentum_row_t *out, int max_rows, int min_recent_games);

// One player's lifetime line -- "cmd card [name]" (ctf_file_io.c). name is
// resolved exact first, then case-insensitive (see db_resolve_id in
// ctf_sqlite_unidb.c); holder[0] == 0 on the identity fields means no
// matching player, same "no qualifying row" convention as db_record_t.
// games is a COUNT(*) over match_players, which has no lifetime column of
// its own -- ctf_stats/game_stats hold running totals, not a match count.
typedef struct
{
	char	playername[16];
	char	member_since[30];
	char	last_played[30];
	int	caps;
	int	steals;		// ctf_stats.flag_pickups
	int	returns;
	int	frags;
	long	shots;
	long	shots_hit;
	int	games;
} db_card_t;

qboolean DB_PlayerCard(const char *name, db_card_t *out);

// Asker vs. one named opponent, across only the matches both of them
// appear in (match_players self-joined on match_id) -- "cmd vs <name>"
// (ctf_file_io.c). opponent_name is resolved the same exact-then-
// case-insensitive way as DB_PlayerCard. games == 0 means they have never
// shared a match (or one/both names do not resolve); the caller prints
// "no recorded games together" rather than a zeroed comparison.
typedef struct
{
	char	opponent_name[16];
	int	games;
	int	my_caps;
	int	my_frags;
	int	their_caps;
	int	their_frags;
	int	my_cap_wins;		// games where my_caps > their_caps
	int	their_cap_wins;		// games where their_caps > my_caps
} db_h2h_t;

qboolean DB_HeadToHead(const char *my_name, const char *opponent_name, db_h2h_t *out);

#endif
