// ui_boards.c -- settled-tier boards: Season Top 10 and Server Records.
//
// docs/LAYOUT.md's cheapest serving tier: rebuild once per game at the
// match-end stats commit (UI_Boards_MatchEnd, called from BeginIntermission
// in p_hud.c right after DB_SessionRecord()), cache the compiled layout
// string, and unicast the same cached buffer to every asker afterward --
// one DB query per map instead of one per request. UI_Boards_Serve adds a
// lazy rebuild for the one case MatchEnd cannot cover: a server that was
// just restarted has never run a match-end since coming up, so the first
// request rebuilds instead of unicasting an empty buffer.
//
// The DB_SeasonTop/DB_ServerRecords queries themselves live in
// ctf_sqlite_unidb.c, not here (SLIPGATE STYLE.md rule 1: one topic per
// translation unit) -- the unified backend's connection and its db_stmt
// caches are that module's private state, and this module's job is only
// to turn rows into a compiled layout screen.

#include <string.h>
#include <stdlib.h>	// qsort -- Momentum board ranks candidates by a derived rate, not a column

#include "g_local.h"
#include "ui_text.h"
#include "ui_layout.h"
#include "ctf_file_io.h"            // CTF_StatsDBMode -- these boards are unified-backend only
#include "ctf_sqlite_unidb.h"
#include "ui_boards.h"

// -- Season Top 10 --------------------------------------------------------

#define SEASON_MAX_ROWS  10
#define SEASON_MIN_GAMES 3	// avoid one-game wonders topping a small sample

// Row storage + row callback for the Season Top table (ui_layout.h). One
// packed column per row, same shape as Railboard_FillRow (p_hud.c) -- no
// board converted through the compiler so far needs true multi-column
// positioning, and packing name+numbers into one string keeps the digits
// lined up under a hand-aligned header without four separate xv columns.
typedef struct
{
	const db_season_row_t	*rows;
	int						count;
} season_rows_t;

static void UI_SeasonTop_FillRow(void *userdata, int row, int num_columns,
	char cells[UI_TABLE_MAX_COLUMNS][UI_CELL_LEN])
{
	const season_rows_t	*data = (const season_rows_t *)userdata;
	const db_season_row_t	*r = &data->rows[row];

	Com_sprintf(cells[0], UI_CELL_LEN, "%-15.15s %3d %3d %4d %3d",
		r->name, r->caps, r->steals, r->railkills, r->games);

	(void)num_columns;	// always 1 for this board
}

static void UI_Build_SeasonTop(ui_buf_t *out)
{
	ui_screen_t		screen;
	ui_elem_t		elems[3];
	ui_table_col_t	column;
	db_season_row_t	rows[SEASON_MAX_ROWS];
	season_rows_t	rowdata;
	char			header[UI_CELL_LEN];
	int				n;
	int				dropped;

	// matches/match_players (what DB_SeasonTop reads) only ever gets rows
	// under the unified backend -- g_tourney.c's Victory() only calls
	// DB_MatchBegin/DB_MatchRecord/DB_MatchFinish when CTF_StatsDBMode() ==
	// CTF_STATSDB_UNIFIED. Skipping the query (rather than calling it and
	// getting zero rows back) also skips DB_Conn_Start(), so a server
	// running the per-player backend never gets an unwanted players.db
	// opened just because this board got asked for.
	n = (CTF_StatsDBMode() == CTF_STATSDB_UNIFIED)
		? DB_SeasonTop(rows, SEASON_MAX_ROWS, SEASON_MIN_GAMES)
		: 0;

	rowdata.rows  = rows;
	rowdata.count = n;

	// same field widths as UI_SeasonTop_FillRow's Com_sprintf above, so the
	// numbers line up under their labels on the client's monospace conchars
	// font.
	Com_sprintf(header, sizeof(header), "%-15s %3s %3s %4s %3s",
		"Name", "Cap", "Stl", "Rail", "GP");

	elems[0].kind = UI_TEXT;
	elems[0].u.text.x = 0;
	elems[0].u.text.y = -60;
	elems[0].u.text.text = "Season Top 10";
	elems[0].u.text.highlight = true;

	elems[1].kind = UI_TEXT;
	elems[1].u.text.x = 0;
	elems[1].u.text.y = -44;
	elems[1].u.text.text = header;
	elems[1].u.text.highlight = false;

	column.x_offset = 0;
	column.priority = 0;

	elems[2].kind = UI_TABLE;
	elems[2].u.table.x = 0;
	elems[2].u.table.y = -28;
	elems[2].u.table.row_dy = 8;
	elems[2].u.table.columns = &column;
	elems[2].u.table.num_columns = 1;
	elems[2].u.table.num_rows = n;
	elems[2].u.table.fill_row = UI_SeasonTop_FillRow;
	elems[2].u.table.userdata = &rowdata;
	elems[2].u.table.highlight = false;
	// Per-viewer highlighting (marking the requesting player's own row) was
	// left out of v1 on purpose: this screen is one shared cache serving
	// every asker (docs/LAYOUT.md's settled tier), and highlighting would
	// need a per-viewer variant, which defeats that cache. A future stat-
	// indirection pass (a client stat pointing at "my row's y", drawn as a
	// separate cursor element) could add it without giving up the shared
	// cache -- noted here rather than solved here.
	elems[2].u.table.footer = (n > 0)
		? "Last 30 days -- minimum 3 games played"
		: "No qualifying players yet this season";
	elems[2].u.table.footer_x = 0;
	elems[2].u.table.footer_y = -28 + n * 8 + 8;
	elems[2].u.table.footer_highlight = false;

	screen.elems = elems;
	screen.count = 3;

	dropped = ui_layout_compile(&screen, out);
	if (dropped > 0)
		gi.dprintf("Season Top: %d row(s) dropped by the layout budget\n", dropped);
}

// -- Server Records --------------------------------------------------------

// One optional line per record: label, holder, value. An empty holder means
// there is no qualifying row, so the record is omitted.
//
// Not static: p_view.c's MOTD screen (ClientShowMOD) also calls this, to
// append up to 3 records without a second copy of the omission rule.
qboolean UI_Records_FormatLine(char *out, size_t outsize,
	const char *label, const db_record_t *rec)
{
	if (!rec->holder[0])
		return false;

	Com_sprintf(out, (int)outsize, "%s: %s - %d", label, rec->holder, rec->value);
	return true;
}

static void UI_Build_ServerRecords(ui_buf_t *out)
{
	ui_screen_t			screen;
	ui_elem_t			elems[7];	// title + up to 6 record lines
	db_server_records_t	rec;
	char				line_caps_game[UI_CELL_LEN];
	char				line_rail_game[UI_CELL_LEN];
	char				line_streak_game[UI_CELL_LEN];
	char				line_returns_game[UI_CELL_LEN];
	char				line_caps_life[UI_CELL_LEN];
	char				line_playtime_life[UI_CELL_LEN];
	int					n;
	int					y;
	int					dropped;

	// same reasoning as UI_Build_SeasonTop above: match_players, ctf_stats
	// and userdata are the unified backend's own tables (ctf_sqlite_unidb.c)
	// and only get written under CTF_STATSDB_UNIFIED, so skip the query --
	// and the DB_Conn_Start() it would trigger -- on any other backend.
	memset(&rec, 0, sizeof(rec));
	if (CTF_StatsDBMode() == CTF_STATSDB_UNIFIED)
		DB_ServerRecords(&rec);

	n = 0;
	y = -60;

	elems[n].kind = UI_TEXT;
	elems[n].u.text.x = 0;
	elems[n].u.text.y = y;
	elems[n].u.text.text = "Server Records";
	elems[n].u.text.highlight = true;
	n++;
	y += 16;

	if (UI_Records_FormatLine(line_caps_game, sizeof(line_caps_game),
			"Most Caps, One Game", &rec.most_caps_game))
	{
		elems[n].kind = UI_TEXT;
		elems[n].u.text.x = 0;
		elems[n].u.text.y = y;
		elems[n].u.text.text = line_caps_game;
		elems[n].u.text.highlight = false;
		n++;
		y += 8;
	}

	if (UI_Records_FormatLine(line_rail_game, sizeof(line_rail_game),
			"Most Rail Kills, One Game", &rec.most_railkills_game))
	{
		elems[n].kind = UI_TEXT;
		elems[n].u.text.x = 0;
		elems[n].u.text.y = y;
		elems[n].u.text.text = line_rail_game;
		elems[n].u.text.highlight = false;
		n++;
		y += 8;
	}

	if (UI_Records_FormatLine(line_streak_game, sizeof(line_streak_game),
			"Best Kill Streak, One Game", &rec.best_streak_game))
	{
		elems[n].kind = UI_TEXT;
		elems[n].u.text.x = 0;
		elems[n].u.text.y = y;
		elems[n].u.text.text = line_streak_game;
		elems[n].u.text.highlight = false;
		n++;
		y += 8;
	}

	if (UI_Records_FormatLine(line_returns_game, sizeof(line_returns_game),
			"Most Flag Returns, One Game", &rec.most_returns_game))
	{
		elems[n].kind = UI_TEXT;
		elems[n].u.text.x = 0;
		elems[n].u.text.y = y;
		elems[n].u.text.text = line_returns_game;
		elems[n].u.text.highlight = false;
		n++;
		y += 8;
	}

	if (UI_Records_FormatLine(line_caps_life, sizeof(line_caps_life),
			"Most Total Caps, Career", &rec.most_caps_lifetime))
	{
		elems[n].kind = UI_TEXT;
		elems[n].u.text.x = 0;
		elems[n].u.text.y = y;
		elems[n].u.text.text = line_caps_life;
		elems[n].u.text.highlight = false;
		n++;
		y += 8;
	}

	// userdata.playtime_total is minutes (g_local.h: "Total playing time in
	// minutes"; p_stats.c adds session_seconds/60 to it), so this is the one
	// record needing a unit conversion before it reads like something a
	// player asked for rather than a database column.
	if (rec.longest_played_lifetime.holder[0])
	{
		Com_sprintf(line_playtime_life, sizeof(line_playtime_life),
			"Longest Played, Career: %s - %dh %02dm",
			rec.longest_played_lifetime.holder,
			rec.longest_played_lifetime.value / 60,
			rec.longest_played_lifetime.value % 60);

		elems[n].kind = UI_TEXT;
		elems[n].u.text.x = 0;
		elems[n].u.text.y = y;
		elems[n].u.text.text = line_playtime_life;
		elems[n].u.text.highlight = false;
		n++;
		y += 8;
	}

	screen.elems = elems;
	screen.count = n;

	dropped = ui_layout_compile(&screen, out);
	if (dropped > 0)
		gi.dprintf("Server Records: %d line(s) dropped by the layout budget\n", dropped);
}

// -- Activity ---------------------------------------------------------------

#define ACTIVITY_MAX_ROWS 10

// Row storage + row callback, same shape as Season Top's above.
typedef struct
{
	const db_activity_row_t	*rows;
	int							count;
} activity_rows_t;

static void UI_Activity_FillRow(void *userdata, int row, int num_columns,
	char cells[UI_TABLE_MAX_COLUMNS][UI_CELL_LEN])
{
	const activity_rows_t	*data = (const activity_rows_t *)userdata;
	const db_activity_row_t	*r = &data->rows[row];

	Com_sprintf(cells[0], UI_CELL_LEN, "%-15.15s %3d %5d",
		r->name, r->games, r->minutes);

	(void)num_columns;	// always 1 for this board
}

static void UI_Build_Activity(ui_buf_t *out)
{
	ui_screen_t			screen;
	ui_elem_t			elems[3];
	ui_table_col_t		column;
	db_activity_row_t	rows[ACTIVITY_MAX_ROWS];
	activity_rows_t		rowdata;
	char				header[UI_CELL_LEN];
	int					n;
	int					dropped;

	// same reasoning as UI_Build_SeasonTop: match_players only gets rows
	// under the unified backend, so skip the query (and the DB_Conn_Start()
	// it would trigger) on any other backend.
	n = (CTF_StatsDBMode() == CTF_STATSDB_UNIFIED)
		? DB_Activity(rows, ACTIVITY_MAX_ROWS)
		: 0;

	rowdata.rows  = rows;
	rowdata.count = n;

	Com_sprintf(header, sizeof(header), "%-15s %3s %5s", "Name", "GP", "Min");

	elems[0].kind = UI_TEXT;
	elems[0].u.text.x = 0;
	elems[0].u.text.y = -60;
	elems[0].u.text.text = "Activity -- Last 7 Days";
	elems[0].u.text.highlight = true;

	elems[1].kind = UI_TEXT;
	elems[1].u.text.x = 0;
	elems[1].u.text.y = -44;
	elems[1].u.text.text = header;
	elems[1].u.text.highlight = false;

	column.x_offset = 0;
	column.priority = 0;

	elems[2].kind = UI_TABLE;
	elems[2].u.table.x = 0;
	elems[2].u.table.y = -28;
	elems[2].u.table.row_dy = 8;
	elems[2].u.table.columns = &column;
	elems[2].u.table.num_columns = 1;
	elems[2].u.table.num_rows = n;
	elems[2].u.table.fill_row = UI_Activity_FillRow;
	elems[2].u.table.userdata = &rowdata;
	elems[2].u.table.highlight = false;
	elems[2].u.table.footer = (n > 0)
		? "Last 7 days -- games played and minutes played"
		: "No games played in the last 7 days";
	elems[2].u.table.footer_x = 0;
	elems[2].u.table.footer_y = -28 + n * 8 + 8;
	elems[2].u.table.footer_highlight = false;

	screen.elems = elems;
	screen.count = 3;

	dropped = ui_layout_compile(&screen, out);
	if (dropped > 0)
		gi.dprintf("Activity: %d row(s) dropped by the layout budget\n", dropped);
}

// -- Momentum -----------------------------------------------------------

#define MOMENTUM_MAX_ROWS         10
// Generous against any realistic 30-day active roster on a private server
// (DB_Momentum's own comment on why an unordered LIMIT this size is safe).
#define MOMENTUM_CANDIDATE_MAX    64
#define MOMENTUM_MIN_RECENT_GAMES 3

// Unlike Season Top/Activity, this board's rank is not a database column --
// it is "recent capture rate minus older capture rate," computed once per
// candidate here rather than in SQL (DB_Momentum's comment explains why).
typedef struct
{
	char	name[16];
	int		recent_caps;
	int		recent_games;
	int		older_caps;
	int		older_games;
	float	rate_diff;
} momentum_row_t;

typedef struct
{
	const momentum_row_t	*rows;
	int						count;
} momentum_rows_t;

static void UI_Momentum_FillRow(void *userdata, int row, int num_columns,
	char cells[UI_TABLE_MAX_COLUMNS][UI_CELL_LEN])
{
	const momentum_rows_t	*data = (const momentum_rows_t *)userdata;
	const momentum_row_t	*r = &data->rows[row];

	Com_sprintf(cells[0], UI_CELL_LEN, "%-15.15s %3d/%-3d %3d/%-3d",
		r->name, r->recent_caps, r->recent_games, r->older_caps, r->older_games);

	(void)num_columns;	// always 1 for this board
}

// qsort comparator: descending by rate_diff (the biggest riser first).
static int UI_Momentum_Compare(const void *a, const void *b)
{
	const momentum_row_t *ra = (const momentum_row_t *)a;
	const momentum_row_t *rb = (const momentum_row_t *)b;

	if (ra->rate_diff > rb->rate_diff)
		return -1;
	if (ra->rate_diff < rb->rate_diff)
		return 1;
	return 0;
}

static void UI_Build_Momentum(ui_buf_t *out)
{
	ui_screen_t			screen;
	ui_elem_t			elems[3];
	ui_table_col_t		column;
	db_momentum_row_t	candidates[MOMENTUM_CANDIDATE_MAX];
	momentum_row_t		rows[MOMENTUM_CANDIDATE_MAX];
	momentum_rows_t		rowdata;
	char				header[UI_CELL_LEN];
	int					n;
	int					i;
	int					shown;
	int					dropped;

	n = (CTF_StatsDBMode() == CTF_STATSDB_UNIFIED)
		? DB_Momentum(candidates, MOMENTUM_CANDIDATE_MAX, MOMENTUM_MIN_RECENT_GAMES)
		: 0;

	for (i = 0; i < n; i++)
	{
		float recent_rate, older_rate;

		// both name[] arrays are the same declared size (netname's, per
		// db_momentum_row_t's comment) and candidates[i].name is already
		// NUL-terminated within it (db_copy_text), so a flat copy carries
		// the terminator along with no separate truncation to reason about.
		memcpy(rows[i].name, candidates[i].name, sizeof(rows[i].name));
		rows[i].recent_caps  = candidates[i].recent_caps;
		rows[i].recent_games = candidates[i].recent_games;
		rows[i].older_caps   = candidates[i].older_caps;
		rows[i].older_games  = candidates[i].older_games;

		// recent_games is always >= MOMENTUM_MIN_RECENT_GAMES (DB_Momentum's
		// HAVING clause), so this never divides by zero. older_games CAN be
		// zero -- a player brand new this month -- and a 0 rate for a window
		// they never played in is exactly what "no older form to compare
		// against" should read as, not a missing value to special-case.
		recent_rate = (float)rows[i].recent_caps / (float)rows[i].recent_games;
		older_rate  = (rows[i].older_games > 0)
			? (float)rows[i].older_caps / (float)rows[i].older_games
			: 0.0f;

		rows[i].rate_diff = recent_rate - older_rate;
	}

	if (n > 1)
		qsort(rows, (size_t)n, sizeof(rows[0]), UI_Momentum_Compare);

	shown = (n < MOMENTUM_MAX_ROWS) ? n : MOMENTUM_MAX_ROWS;

	rowdata.rows  = rows;
	rowdata.count = shown;

	Com_sprintf(header, sizeof(header), "%-15s %-7s %-7s", "Name", "Recent", "Prior");

	elems[0].kind = UI_TEXT;
	elems[0].u.text.x = 0;
	elems[0].u.text.y = -60;
	elems[0].u.text.text = "Momentum -- Biggest 7-Day Movers";
	elems[0].u.text.highlight = true;

	elems[1].kind = UI_TEXT;
	elems[1].u.text.x = 0;
	elems[1].u.text.y = -44;
	elems[1].u.text.text = header;
	elems[1].u.text.highlight = false;

	column.x_offset = 0;
	column.priority = 0;

	elems[2].kind = UI_TABLE;
	elems[2].u.table.x = 0;
	elems[2].u.table.y = -28;
	elems[2].u.table.row_dy = 8;
	elems[2].u.table.columns = &column;
	elems[2].u.table.num_columns = 1;
	elems[2].u.table.num_rows = shown;
	elems[2].u.table.fill_row = UI_Momentum_FillRow;
	elems[2].u.table.userdata = &rowdata;
	elems[2].u.table.highlight = false;
	elems[2].u.table.footer = (shown > 0)
		? "Caps this week vs. the 23 days before -- minimum 3 recent games"
		: "No qualifying players this week";
	elems[2].u.table.footer_x = 0;
	elems[2].u.table.footer_y = -28 + shown * 8 + 8;
	elems[2].u.table.footer_highlight = false;

	screen.elems = elems;
	screen.count = 3;

	dropped = ui_layout_compile(&screen, out);
	if (dropped > 0)
		gi.dprintf("Momentum: %d row(s) dropped by the layout budget\n", dropped);
}

// -- registry ---------------------------------------------------------------

typedef struct
{
	char		storage[UI_LAYOUT_BUDGET];
	ui_buf_t	buf;
	qboolean	valid;
	void		(*rebuild)(ui_buf_t *out);
} ui_board_entry_t;

static ui_board_entry_t	ui_boards[UI_BOARD_COUNT];
static qboolean			ui_boards_registered = false;

// Field-by-field, not an aggregate initializer: this tree's strictest CI
// compiler builds as legacy C, where a plain (non-designated) aggregate
// initializer for a struct containing a function pointer still has to name
// every member in declaration order, and this tree's established pattern
// (ui_layout.h's ui_pic_t comment, every hand-built ui_elem_t in p_hud.c) is
// to assign field-by-field instead of relying on positional order surviving
// a future member reorder unnoticed.
static void UI_Boards_Register(void)
{
	if (ui_boards_registered)
		return;

	ui_boards[UI_BOARD_SEASON_TOP].valid = false;
	ui_boards[UI_BOARD_SEASON_TOP].rebuild = UI_Build_SeasonTop;
	ui_buf_init(&ui_boards[UI_BOARD_SEASON_TOP].buf,
		ui_boards[UI_BOARD_SEASON_TOP].storage,
		sizeof(ui_boards[UI_BOARD_SEASON_TOP].storage));

	ui_boards[UI_BOARD_SERVER_RECORDS].valid = false;
	ui_boards[UI_BOARD_SERVER_RECORDS].rebuild = UI_Build_ServerRecords;
	ui_buf_init(&ui_boards[UI_BOARD_SERVER_RECORDS].buf,
		ui_boards[UI_BOARD_SERVER_RECORDS].storage,
		sizeof(ui_boards[UI_BOARD_SERVER_RECORDS].storage));

	ui_boards[UI_BOARD_ACTIVITY].valid = false;
	ui_boards[UI_BOARD_ACTIVITY].rebuild = UI_Build_Activity;
	ui_buf_init(&ui_boards[UI_BOARD_ACTIVITY].buf,
		ui_boards[UI_BOARD_ACTIVITY].storage,
		sizeof(ui_boards[UI_BOARD_ACTIVITY].storage));

	ui_boards[UI_BOARD_MOMENTUM].valid = false;
	ui_boards[UI_BOARD_MOMENTUM].rebuild = UI_Build_Momentum;
	ui_buf_init(&ui_boards[UI_BOARD_MOMENTUM].buf,
		ui_boards[UI_BOARD_MOMENTUM].storage,
		sizeof(ui_boards[UI_BOARD_MOMENTUM].storage));

	ui_boards_registered = true;
}

static void UI_Boards_Rebuild(int board_id)
{
	ui_board_entry_t *b = &ui_boards[board_id];

	ui_buf_init(&b->buf, b->storage, sizeof(b->storage));
	b->rebuild(&b->buf);
	b->valid = true;
}

void UI_Boards_MatchEnd(void)
{
	int i;

	UI_Boards_Register();

	for (i = 0; i < UI_BOARD_COUNT; i++)
		UI_Boards_Rebuild(i);
}

void UI_Boards_Serve(edict_t *ent, int board_id)
{
	ui_board_entry_t *b;

	if (!ent || board_id < 0 || board_id >= UI_BOARD_COUNT)
		return;

	UI_Boards_Register();

	b = &ui_boards[board_id];
	if (!b->valid)
		UI_Boards_Rebuild(board_id);

	gi.WriteByte(svc_layout);
	gi.WriteString(b->storage);
	gi.unicast(ent, true);
}

// -- The ticked tier -------------------------------------------------------
//
// One dirty flag covers all five in-match boards: their data overlaps
// heavily (frags, captures, accuracy, roster), and the 1 Hz coalescing gate
// already bounds the cost at one per-viewer repaint per second, so per-board
// dirty tracking would complicate every stats call site to save almost
// nothing. The boards are per-viewer screens (each highlights or ranks
// relative to the asker), which is why the serve loop rebuilds per client
// instead of caching one buffer the way the settled tier does.

static qboolean ui_tick_dirty;
static float    ui_tick_next;	// level.time gate for the next allowed serve

void UI_Tick_Dirty(void)
{
	ui_tick_dirty = true;
}

static void UI_Tick_Serve(void)
{
	int i;

	for (i = 1; i <= game.maxclients; i++)
	{
		edict_t *ent = g_edicts + i;

		if (!ent->inuse || !ent->client)
			continue;

		if (ent->client->showscores)
			DeathmatchScoreboardMessage(ent, ent->enemy);
		else if (ent->client->showsquadboard)
			SquadboardMessage(ent, ent->enemy);
		else if (ent->client->showstatboard)
			StatboardMessage(ent, ent->enemy);
		else if (ent->client->showteamstatboard)
			TeamStatboardMessage(ent, ent->enemy);
		else if (ent->client->showrailboard)
			RailboardMessage(ent, ent->enemy);
		else
			continue;

		gi.unicast(ent, false);
	}

	ui_tick_dirty = false;
}

void UI_Tick_Frame(void)
{
	if (!deathmatch->value || !ui_tick_dirty)
		return;

	// level.time restarts at zero on a map change; a stale gate from the
	// previous map would otherwise silence the boards for its remainder
	if (ui_tick_next > level.time + 1.0f)
		ui_tick_next = 0;

	if (level.time < ui_tick_next)
		return;

	ui_tick_next = level.time + 1.0f;
	UI_Tick_Serve();
}

void UI_Tick_Push(void)
{
	if (!ui_tick_dirty)
		return;

	ui_tick_next = level.time + 1.0f;
	UI_Tick_Serve();
}
