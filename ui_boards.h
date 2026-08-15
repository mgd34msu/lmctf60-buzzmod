// ui_boards.h -- the settled-tier board registry (docs/LAYOUT.md's cheapest
// serving tier: rebuilt once per game at the match-end stats commit, cached,
// served instantly to every asker afterward).
//
// Two boards live here today: Season Top (rolling 30-day leaderboard) and
// Server Records (all-time single-game and lifetime bests). Both are read-
// only reporting screens built with ui_layout.h's compiler over data from
// ctf_sqlite_unidb.c's DB_SeasonTop/DB_ServerRecords -- see ui_boards.c for
// why those queries stayed in the DB module instead of moving here (the
// module's own shared connection and db_stmt caches are private to it).
//
// Needs qboolean and edict_t from g_local.h, but does not #include it --
// same convention as ui_layout.h and ui_text.h: g_local.h pulls in
// q_shared.h, which has no include guard, so a second #include anywhere in
// the chain redeclares every enum in it and fails the build. Include this
// header after g_local.h.
//
// UI_Records_FormatLine also needs db_record_t (ctf_sqlite_unidb.h) --
// include that header first too. This is the one function in this file
// used outside ui_boards.c: p_view.c's MOTD screen (ClientShowMOD) borrows
// it to print up to 3 server records without duplicating the "omit a
// record nobody has set yet" formatting rule a second time.

#ifndef UI_BOARDS_H
#define UI_BOARDS_H

typedef enum
{
	UI_BOARD_SEASON_TOP,
	UI_BOARD_SERVER_RECORDS,
	UI_BOARD_ACTIVITY,
	UI_BOARD_MOMENTUM,

	UI_BOARD_COUNT
} ui_board_id_t;

// Rebuilds every settled board from the database and marks them valid.
// Call exactly once per match, AFTER the session/stats DB commit (see
// BeginIntermission in p_hud.c, right after DB_SessionRecord()) -- these
// boards read match_players/matches/ctf_stats/userdata, and rebuilding
// before those tables hold the just-finished match would cache a screen
// that is missing it until the NEXT match end.
void UI_Boards_MatchEnd(void);

// Unicasts the cached buffer for board_id to ent (svc_layout, reliable).
// Rebuilds lazily first if the board has never been built this server run
// (server restart case: UI_Boards_MatchEnd has not fired yet). Does
// nothing if board_id is out of range.
void UI_Boards_Serve(edict_t *ent, int board_id);

// Formats one server-record line as "<label>: <holder> - <value>". Returns
// false (writing nothing) when rec->holder[0] == 0 -- no qualifying row --
// so the caller omits the line rather than printing a fake one.
qboolean UI_Records_FormatLine(char *out, size_t outsize, const char *label, const db_record_t *rec);

// -- The ticked tier (docs/LAYOUT.md): in-match boards ---------------------
//
// The five in-match boards (DM scoreboard, squad, stat, teamstat, rail) are
// per-viewer screens whose data changes only when a stats event fires. They
// are served push-on-change: any stats mutation calls UI_Tick_Dirty(), and
// UI_Tick_Frame() -- called once per server frame after all client frames --
// repaints every open board at most once per second while dirty. A quiet
// server sends nothing; a firefight coalesces to 1 Hz. Milestone events
// (captures, match end) call UI_Tick_Push() to skip the wait.

// A stats event changed data shown on the in-match boards.
void UI_Tick_Dirty(void);

// Serve dirty boards NOW (milestone events; also re-arms the 1 Hz gate).
void UI_Tick_Push(void);

// Once per server frame, after every ClientEndServerFrame: serves open
// boards if dirty and the 1 Hz gate allows.
void UI_Tick_Frame(void);

#endif // UI_BOARDS_H
