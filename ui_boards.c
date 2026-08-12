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

// One optional line per record: label, holder, value. Built from
// db_record_t's holder[0]==0 (owner's ruling, ctf_sqlite_unidb.h: "no
// qualifying row") -- a record nobody has set yet is left off the screen
// entirely rather than printed as "N/A" or a zero.
static qboolean UI_Records_Line(char *out, size_t outsize,
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

	if (UI_Records_Line(line_caps_game, sizeof(line_caps_game),
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

	if (UI_Records_Line(line_rail_game, sizeof(line_rail_game),
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

	if (UI_Records_Line(line_streak_game, sizeof(line_streak_game),
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

	if (UI_Records_Line(line_returns_game, sizeof(line_returns_game),
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

	if (UI_Records_Line(line_caps_life, sizeof(line_caps_life),
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
