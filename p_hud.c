#include "g_local.h"
#include "g_ctffunc.h" //surt for some nice wrapper functions
#include "g_tourney.h"
#include "bat.h"
#include "slipgate/sg_chat.h"       // BUZZKILL - SG_ChatLevelEnd from BeginIntermission
#include "ctf_sqlite_unidb.h"       // BUZZKILL - DB_SessionRecord from BeginIntermission
#include "ui_boards.h"              // settled boards: UI_Boards_MatchEnd from BeginIntermission
#include "ui_text.h"                // bounded appender, needed by ui_layout.h below
#include "ui_layout.h"              // declarative screen compiler; Railboard is its proof conversion

int MvpDisp;


extern int Time_Left;

edict_t *Declare_Railgun_Victor(void);


/*
======================================================================

INTERMISSION

======================================================================
*/

void MoveClientToIntermission (edict_t *ent)
{
	
	if (deathmatch->value || coop->value)
		ent->client->showscores = true;
	VectorCopy (level.intermission_origin, ent->s.origin);
	ent->client->ps.pmove.origin[0] = level.intermission_origin[0]*8;
	ent->client->ps.pmove.origin[1] = level.intermission_origin[1]*8;
	ent->client->ps.pmove.origin[2] = level.intermission_origin[2]*8;
	VectorCopy (level.intermission_angle, ent->client->ps.viewangles);
	ent->client->ps.pmove.pm_type = PM_FREEZE;
	ent->client->ps.gunindex = 0;
	ent->client->ps.blend[3] = 0;
	ent->client->ps.rdflags &= ~RDF_UNDERWATER;

	// clean up powerup info
	ent->client->quad_framenum = 0;
	ent->client->invincible_framenum = 0;
	ent->client->breather_framenum = 0;
	ent->client->enviro_framenum = 0;
	ent->client->grenade_blew_up = false;
	ent->client->grenade_time = 0;

	ent->viewheight = 0;
	ent->s.modelindex = 0;
	ent->s.modelindex2 = 0;
	ent->s.modelindex3 = 0;
	ent->s.modelindex = 0;
	ent->s.effects = 0;
	ent->s.sound = 0;
	ent->solid = SOLID_NOT;

	// add the layout

	if (deathmatch->value || coop->value)
	{
		DeathmatchScoreboardMessage (ent, NULL);
		gi.unicast (ent, true);
	}

}

void BeginIntermission (edict_t *targ)
{
	int		i, n;
	edict_t	*ent, *client;

	if (level.intermissiontime)
		return;		// already activated

	MvpDisp = 1;

    // LM_JORM -- Proclaim a victory!
    Victory();
    // END LM_JORM

	/*
	 * SLIPGATE's bots react to the result, right where the server has just
	 * announced it. This is the one place every way of ending a level meets --
	 * timelimit, fraglimit, a target_changelevel and a match end all arrive
	 * here -- and the intermissiontime guard above makes it fire once.
	 *
	 * After Victory() rather than before it because that is the reading order
	 * a player sees, and the scores are the same either side: the only thing
	 * Victory() adds is STATS_SWEEPS, which nothing here sums.
	 *
	 * Nothing is said in this call. The lines are booked and go out over the
	 * next four seconds from SG_ChatFrame, which keeps running through
	 * intermission -- well inside the five seconds a client must wait before
	 * it can end intermission (ClientThink, this file's counterpart in
	 * p_client.c).
	 */
	SG_ChatLevelEnd();

	/*
	 * The session attendance, for the same reason and in the same place: the
	 * Victory() call above has just opened and closed the match row, so the
	 * match_id the rows hang off exists, and the intermissiontime guard at
	 * the top of this function makes the write happen once. DB_SessionRecord
	 * latches the match id it wrote as well, so an end that somehow reached
	 * here twice still leaves one set of rows.
	 *
	 * Returns immediately unless sg_sessiondb is on AND the unified stats
	 * backend is the one running (ctf_statsdb 2).
	 */
	DB_SessionRecord();

	/*
	 * The settled boards (Season Top, Server Records -- ui_boards.c), right
	 * after the stats commit above: Victory() (already run, first thing in
	 * this function) has written this match's matches/match_players rows,
	 * and DB_SessionRecord() just wrote its own, so every table these boards
	 * query is current. The intermissiontime guard at the top of this
	 * function already makes this whole function run once per level, so
	 * this rebuilds once per match, matching docs/LAYOUT.md's settled tier
	 * ("rebuild exactly once per game at the match-end stats commit").
	 */
	UI_Boards_MatchEnd();

	/*
	 * The match report (MILESTONE tier, docs/LAYOUT.md): a console print
	 * stream pushed to every client summarizing the match that just ended --
	 * final score, winner, top capper, top defender, top killer, accuracy
	 * leader. Right after the settled boards rebuild above, for the same
	 * reason: this level's in-memory stats (stats_get) are still the real
	 * numbers here, before Match_Start or the next level's SpawnEntities
	 * clears them. Unlike the settled boards, this reads no database table
	 * at all -- the match that just ended is already sitting in memory.
	 */
	CTF_MatchReport();

	game.autosaved = false;

	// respawn any dead clients
	//bat
	//Too Many overflows!!!!!
	//for (i=0 ; i<maxclients->value ; i++)
	//{
	//	client = g_edicts + 1 + i;
	//	if (!client->inuse)
	//		continue;
	//	if (client->health <= 0)
	//	respawn(client);
	//}

	level.intermissiontime = level.time;
	level.changemap = targ->map;


	if (strstr(level.changemap, "*"))
	{
		if (coop->value)
		{
			for (i=0 ; i<maxclients->value ; i++)
			{
				client = g_edicts + 1 + i;
				if (!client->inuse)
					continue;
				// strip players of all keys between units
				for (n = 0; n < MAX_ITEMS; n++)
				{
					if (itemlist[n].flags & IT_KEY)
						client->client->pers.inventory[n] = 0;
				}
			}
		}
	}
	else
	{
		if (!deathmatch->value)
		{
			level.exitintermission = 1;		// go immediately to the next level
			return;
		}
	}

	level.exitintermission = 0;

	// find an intermission spot
	ent = G_Find (NULL, FOFS(classname), "info_player_intermission");
	if (!ent)
	{	// the map creator forgot to put in an intermission point...
		ent = G_Find (NULL, FOFS(classname), "info_player_start");
		if (!ent)
			ent = G_Find (NULL, FOFS(classname), "info_player_deathmatch");
	}
	else
	{	// chose one of four spots
		i = rand() & 3;
		while (i--)
		{
			ent = G_Find (ent, FOFS(classname), "info_player_intermission");
			if (!ent)	// wrap around the list
				ent = G_Find (ent, FOFS(classname), "info_player_intermission");
		}
	}

	VectorCopy (ent->s.origin, level.intermission_origin);
	VectorCopy (ent->s.angles, level.intermission_angle);


	// move all clients to the intermission point
	for (i=0 ; i<maxclients->value ; i++)
	{
		client = g_edicts + 1 + i;
		if (!client->inuse)
			continue;
		MoveClientToIntermission (client);
	}

}


void Show_String(int x, int y, char *string, char *Text)
{
	sprintf(DBuffer, "xv %i yv %i string2 \"%s\" ", x,  y, Text);
	strcat(string, DBuffer);
}

/* Preserve each board's legacy row-admission byte cap. */
// (or, for a multi-token header, one caller sums several calls) the
// same way each producer's own Com_sprintf-then-strlen call did, so
// the admission decisions below land on the exact old truncation
// point rather than a new, larger one.
static int Board_LineLen(const char *fmt, ...)
{
	char	scratch[512];
	va_list	argptr;

	va_start(argptr, fmt);
	vsnprintf(scratch, sizeof(scratch), fmt, argptr);
	va_end(argptr);

	return (int)strlen(scratch);
}

// Board_LogVariant -- shared diagnostic tail for every board producer
// that now goes through ui_layout_compile_ladder (docs/LAYOUT.md point
// 2: full/condensed/minimal, fit-verified by measurement). Logs when a
// board had to drop to a denser rung (visible content change, worth
// knowing about even when the result fits cleanly) and separately when
// even the served rung still lost rows/elements to the wire budget
// (only possible at UI_BOARD_MINIMAL, the floor of the ladder). Both
// go through gi.dprintf, the sanctioned diagnostic channel (SLIPGATE
// STYLE.md rule 12).
static void Board_LogVariant(const char *board_name, ui_board_variant_t variant_used,
	int dropped, const char *unit)
{
	static const char * const variant_names[] = { "FULL", "CONDENSED", "MINIMAL" };

	if (variant_used != UI_BOARD_FULL)
		gi.dprintf("%s: served %s -- a denser variant did not fit every %s\n",
			board_name, variant_names[variant_used], unit);

	if (dropped > 0)
		gi.dprintf("%s: %d %s(s) still dropped by the layout budget at %s\n",
			board_name, dropped, unit, variant_names[variant_used]);
}

extern edict_t *Railgun_Victor;


// DeathmatchScoreboardMessage's red/blue roster rows are NOT converted
// to a UI_TABLE: a row is a heterogeneous, conditionally-present mix
// (a raw client/ctf token, plus an optional captures line, an
// optional rune line, an optional MVP marker) rather than a fixed set
// of columns, and the original breaks the ENTIRE roster loop -- not
// just the current row -- the moment any one of those pieces doesn't
// fit. UI_TABLE's contract (ui_layout.h) is fixed columns with whole-
// row atomicity, which does not model either of those, so this
// function builds its ui_elem_t array directly, piece by piece,
// keeping the original's own nested if/else/break control flow
// unchanged (STYLE.md rule 9) rather than forcing it through a shape
// that does not fit.
#define DMSCORE_MAX_ROWS	21	// matches the original's own red/blue cap ("if (red > 21) red = 21;")

typedef struct
{
	char	main_text[80];	// this row's client/ctf raw token, or its DMVP/OMVP string2 line
	char	capt_text[24];	// "C:<n>" (portrait mode only)
	char	rune_text[8];	// "R:<code>" (portrait mode only; showsmall draws the bare code)
} dmscore_row_bufs_t;

// Context for one attempt at DeathmatchScoreboardMessage's screen.
// DMScore_BuildFull and DMScore_BuildCompact (below) are both called
// through the density ladder (ui_layout.h) with this SAME ctx
// pointer, so it carries: the per-team aggregates DeathmatchScore-
// boardMessage gathers once before the ladder runs (identical for
// every rung, so there is no reason to gather them again on a retry),
// and the scratch storage whichever rung actually gets built writes
// its ui_elem_t text into. That storage has to outlive the build()
// call: ui_layout_compile_ladder compiles the screen immediately
// after build() returns, so anything screen->elems points at must
// still be valid then.
typedef struct
{
	const int			*sorted;	// ctx->redsorted or ctx->bluesorted
	int					count;
	ui_board_variant_t	variant;
} dmscore_compact_rows_t;

typedef struct
{
	// -- aggregated inputs, gathered once by DeathmatchScoreboardMessage
	int		redsorted[MAX_CLIENTS];
	int		bluesorted[MAX_CLIENTS];
	int		red, blue;					// pre-clamp counts; DMScore_BuildFull applies the original's own 21-a-side clamp itself
	int		redscore, bluescore;
	int		redcaps, bluecaps;

	int		red_rune_strength, red_rune_haste, red_rune_regen, red_rune_resist;
	int		red_item_quad, red_item_shield, red_item_armor, red_item_mega;
	int		blue_rune_strength, blue_rune_haste, blue_rune_regen, blue_rune_resist;
	int		blue_item_quad, blue_item_shield, blue_item_armor, blue_item_mega;

	char	*redfc, *bluefc;
	char	*red_runes, *blue_runes;

	int		sorted_reg_observers[MAX_CLIENTS];
	int		sorted_red_observers[MAX_CLIENTS];
	int		sorted_blue_observers[MAX_CLIENTS];
	int		reg_observers, red_observers, blue_observers, total_observers;

	// -- scratch storage for whichever rung gets built
	ui_elem_t			elems[320];
	dmscore_row_bufs_t	red_buf[DMSCORE_MAX_ROWS];
	dmscore_row_bufs_t	blue_buf[DMSCORE_MAX_ROWS];
	char				mvp_text[9][100];
	int					mvp_x[9], mvp_y[9];
	char				els2_text[64][32];
	int					els2_x[64], els2_y[64];
	char				pf_text[6][32];

	// UI_BOARD_CONDENSED / UI_BOARD_MINIMAL: one packed column per
	// team, same table shape Railboard/Statboard use.
	ui_table_col_t			compact_column;
	dmscore_compact_rows_t	compact_red_rows;
	dmscore_compact_rows_t	compact_blue_rows;
} dmscore_ctx_t;

// Row callback for DeathmatchScoreboardMessage's UI_BOARD_CONDENSED
// and UI_BOARD_MINIMAL tables (ui_layout.h). One packed column per
// team, same shape as Railboard/Statboard's tables -- name plus
// score, with captures added back in CONDENSED (caps/frags outrank
// cosmetic columns on a CTF board) and dropped again in MINIMAL,
// which keeps only the board's headline number.
static void DMScore_CompactFillRow(void *userdata, int row, int num_columns,
	char cells[UI_TABLE_MAX_COLUMNS][UI_CELL_LEN])
{
	const dmscore_compact_rows_t *rows = (const dmscore_compact_rows_t *)userdata;
	edict_t		*cl_ent = g_edicts + 1 + rows->sorted[row];
	gclient_t	*cl = cl_ent->client;
	int			score;
	int			caps;

	score = (int)stats_get(cl_ent, STATS_SCORE);
	caps  = (int)stats_get(cl_ent, STATS_CAPTURES);

	if (rows->variant == UI_BOARD_MINIMAL)
		Com_sprintf(cells[0], UI_CELL_LEN, "%-15s %3i", cl->pers.netname, score);
	else
		Com_sprintf(cells[0], UI_CELL_LEN, "%-12s %3i %2i", cl->pers.netname, score, caps);

	(void)num_columns;
}

// Builds DeathmatchScoreboardMessage's screen for UI_BOARD_CONDENSED
// or UI_BOARD_MINIMAL: two simple name+number tables (one per team)
// instead of FULL's portrait/compact roster with its captures, rune,
// and MVP decorations. Neither variant carries a legacy admission
// pass -- both are new formats that never shipped before the density
// ladder existed, so every player is offered and ui_layout_compile's
// own budget (UI_LAYOUT_BUDGET) decides what fits. Rows are capped at
// DMSCORE_MAX_ROWS per team, the same 21-a-side ceiling UI_BOARD_FULL
// has always enforced (see DMSCORE_MAX_ROWS's own comment) -- a
// judgment call to keep one structural roster cap for this board
// rather than growing the storage for a case that has not come up.
static int DMScore_BuildCompact(dmscore_ctx_t *ctx, ui_board_variant_t variant, ui_screen_t *screen)
{
	int n;

	ctx->compact_red_rows.sorted  = ctx->redsorted;
	ctx->compact_red_rows.count   = (ctx->red < DMSCORE_MAX_ROWS) ? ctx->red : DMSCORE_MAX_ROWS;
	ctx->compact_red_rows.variant = variant;

	ctx->compact_blue_rows.sorted  = ctx->bluesorted;
	ctx->compact_blue_rows.count   = (ctx->blue < DMSCORE_MAX_ROWS) ? ctx->blue : DMSCORE_MAX_ROWS;
	ctx->compact_blue_rows.variant = variant;

	n = 0;

	ctx->elems[n].kind = UI_PIC;
	ctx->elems[n].u.pic.x = 0;
	ctx->elems[n].u.pic.y = 0;
	ctx->elems[n].u.pic.stat_driven = false;
	ctx->elems[n].u.pic.image.name = "redlion_i";
	n++;

	ctx->elems[n].kind = UI_PIC;
	ctx->elems[n].u.pic.x = 160;
	ctx->elems[n].u.pic.y = 0;
	ctx->elems[n].u.pic.stat_driven = false;
	ctx->elems[n].u.pic.image.name = "bluewolf_i";
	n++;

	ctx->elems[n].kind = UI_TEXT;
	ctx->elems[n].u.text.x = 32;
	ctx->elems[n].u.text.y = 0;
	ctx->elems[n].u.text.text = "RED";
	ctx->elems[n].u.text.highlight = true;
	n++;

	ctx->elems[n].kind = UI_TEXT;
	ctx->elems[n].u.text.x = 192;
	ctx->elems[n].u.text.y = 0;
	ctx->elems[n].u.text.text = "BLUE";
	ctx->elems[n].u.text.highlight = true;
	n++;

	ctx->compact_column.x_offset = 0;
	ctx->compact_column.priority = 0;

	ctx->elems[n].kind = UI_TABLE;
	ctx->elems[n].u.table.x = 0;
	ctx->elems[n].u.table.y = 16;
	ctx->elems[n].u.table.row_dy = 8;
	ctx->elems[n].u.table.columns = &ctx->compact_column;
	ctx->elems[n].u.table.num_columns = 1;
	ctx->elems[n].u.table.num_rows = ctx->compact_red_rows.count;
	ctx->elems[n].u.table.fill_row = DMScore_CompactFillRow;
	ctx->elems[n].u.table.userdata = &ctx->compact_red_rows;
	ctx->elems[n].u.table.highlight = true;
	ctx->elems[n].u.table.footer = NULL;
	ctx->elems[n].u.table.footer_x = 0;
	ctx->elems[n].u.table.footer_y = 0;
	ctx->elems[n].u.table.footer_highlight = false;
	n++;

	ctx->elems[n].kind = UI_TABLE;
	ctx->elems[n].u.table.x = 160;
	ctx->elems[n].u.table.y = 16;
	ctx->elems[n].u.table.row_dy = 8;
	ctx->elems[n].u.table.columns = &ctx->compact_column;
	ctx->elems[n].u.table.num_columns = 1;
	ctx->elems[n].u.table.num_rows = ctx->compact_blue_rows.count;
	ctx->elems[n].u.table.fill_row = DMScore_CompactFillRow;
	ctx->elems[n].u.table.userdata = &ctx->compact_blue_rows;
	ctx->elems[n].u.table.highlight = true;
	ctx->elems[n].u.table.footer = NULL;
	ctx->elems[n].u.table.footer_x = 0;
	ctx->elems[n].u.table.footer_y = 0;
	ctx->elems[n].u.table.footer_highlight = false;
	n++;

	screen->elems = ctx->elems;
	screen->count = n;

	return 0;
}

// Build the full deathmatch scoreboard and report its pre-filter count.
// red_rows_shown/blue_rows_shown track how far each team's roster got
// before its own 1024-byte admission check -- or the original's own
// 21-a-side clamp -- cut it short; mvp_admitted/footer_admitted/
// pf_admitted record whether their respective all-or-nothing blocks
// were drawn. None of these change what gets drawn -- they only
// observe it, the same way every other converted board's
// legacy_dropped already does.
static int DMScore_BuildFull(dmscore_ctx_t *ctx, ui_screen_t *screen)
{
    int         red, blue;
    int         i;
    int         x, y;
    gclient_t   *cl;
    edict_t     *cl_ent;
    qboolean    showsmall;
    char        *player_rune;

    int         n;
    int         legacy_len;
    int         line_len;

    int         mvp_n;
    int         mvp_total_len;

    int         els2_n;
    int         els2_start;
    int         els2_len;
    int         sec_len;
    qboolean    gate_ok;

    int         foot_len;

    int         rows;
    int         fy;
    int         pf_len;

    int         red_rows_shown, blue_rows_shown;
    int         red_clamp_dropped, blue_clamp_dropped;
    qboolean    mvp_admitted;
    qboolean    footer_admitted;
    qboolean    pf_admitted;
    int         legacy_dropped;

    red = ctx->red;
    blue = ctx->blue;

    player_rune = NULL;
    showsmall = false;
    els2_start = 0;
    mvp_n = 0;
    mvp_admitted = false;
    footer_admitted = false;
    pf_admitted = false;
    gate_ok = true;

    // add the clients in sorted order
    legacy_len = 0;
    n = 0;

    if (red > 6 || red + blue + ctx->total_observers > 16)
    {
        showsmall = true;
        if (red > 21)
            red = 21;
    }

	if (blue > 6 || red + blue + ctx->total_observers > 16)
    {
        showsmall = true;
        if (blue > 21)
            blue = 21;
    }

    if (showsmall)
    {
        int hdr_len = Board_LineLen("xv %i yv %i string2 \"%s\" ", 0, 32, "Scr Png Name        ")
            + Board_LineLen("xv %i yv %i string2 \"%s\" ", 0, 40, "------------------- ")
            + Board_LineLen("xv %i yv %i string2 \"%s\" ", 160, 32, "Scr Png Name        ")
            + Board_LineLen("xv %i yv %i string2 \"%s\" ", 160, 40, "------------------- ");

        if (legacy_len + hdr_len <= 1024)
        {
            legacy_len += hdr_len;

            ctx->elems[n].kind = UI_TEXT; ctx->elems[n].u.text.x = 0;   ctx->elems[n].u.text.y = 32; ctx->elems[n].u.text.text = "Scr Png Name        "; ctx->elems[n].u.text.highlight = true; n++;
            ctx->elems[n].kind = UI_TEXT; ctx->elems[n].u.text.x = 0;   ctx->elems[n].u.text.y = 40; ctx->elems[n].u.text.text = "------------------- "; ctx->elems[n].u.text.highlight = true; n++;
            ctx->elems[n].kind = UI_TEXT; ctx->elems[n].u.text.x = 160; ctx->elems[n].u.text.y = 32; ctx->elems[n].u.text.text = "Scr Png Name        "; ctx->elems[n].u.text.highlight = true; n++;
            ctx->elems[n].kind = UI_TEXT; ctx->elems[n].u.text.x = 160; ctx->elems[n].u.text.y = 40; ctx->elems[n].u.text.text = "------------------- "; ctx->elems[n].u.text.highlight = true; n++;
        }
    }


    red_rows_shown = red;
    blue_rows_shown = blue;
    for (i=0 ; i<red ; i++)
    {
        cl = &game.clients[ctx->redsorted[i]];
        cl_ent = g_edicts + 1 + ctx->redsorted[i];

        if (showsmall)
        {
            x = 0;
            y = 48 + 8 * i;

            if (cl->rune)
            {
                switch (cl->rune->runetype)
                {
                case 1:	 player_rune = "ST";    break;
                case 2:	 player_rune = "RS";    break;
                case 4:	 player_rune = "HA";    break;
                case 8:	 player_rune = "RG";    break;
                }

                line_len = Board_LineLen("xv %i yv %i string2 \"%s\" ", x + 32 - 136 + 80, y, player_rune);
                if (legacy_len + line_len > 1024)
                {
                    red_rows_shown = i;
                    break;
                }
                legacy_len += line_len;

                ctx->elems[n].kind = UI_TEXT;
                ctx->elems[n].u.text.x = x + 32 - 136 + 80;
                ctx->elems[n].u.text.y = y;
                ctx->elems[n].u.text.text = player_rune;
                ctx->elems[n].u.text.highlight = true;
                n++;

            }

            if (cl_ent == Query_DMVP())
			{
				Com_sprintf(ctx->red_buf[i].main_text, sizeof(ctx->red_buf[i].main_text), "D%3d %3d %s", cl->resp.score, cl->ping, cl->pers.netname);
				ctx->red_buf[i].main_text[19] = 0;

				// FIXED (found during the declarative conversion): the
				// original's strcat path drew an MVP row's rune line
				// TWICE -- once from the rune block above and again
				// folded into this row's flush, because string2 was
				// never cleared between them. One rune line per row.
				line_len = Board_LineLen("xv %i yv %i string2 \"%s\" ", x, y, ctx->red_buf[i].main_text);
				if (legacy_len + line_len > 1024)
				{
					red_rows_shown = i;
					break;
				}
				legacy_len += line_len;

				ctx->elems[n].kind = UI_TEXT;
				ctx->elems[n].u.text.x = x;
				ctx->elems[n].u.text.y = y;
				ctx->elems[n].u.text.text = ctx->red_buf[i].main_text;
				ctx->elems[n].u.text.highlight = true;
				n++;
			}
            else if (cl_ent == Query_OMVP())
			{
				Com_sprintf(ctx->red_buf[i].main_text, sizeof(ctx->red_buf[i].main_text), "O%3d %3d %s", cl->resp.score, cl->ping, cl->pers.netname);
				ctx->red_buf[i].main_text[19] = 0;

				/* FIXED: one rune line per row (the pre-conversion strcat
				 * path drew an MVP row's rune twice; see the red DMVP site) */
				line_len = Board_LineLen("xv %i yv %i string2 \"%s\" ", x, y, ctx->red_buf[i].main_text);
				if (legacy_len + line_len > 1024)
				{
					red_rows_shown = i;
					break;
				}
				legacy_len += line_len;

				ctx->elems[n].kind = UI_TEXT;
				ctx->elems[n].u.text.x = x;
				ctx->elems[n].u.text.y = y;
				ctx->elems[n].u.text.text = ctx->red_buf[i].main_text;
				ctx->elems[n].u.text.highlight = true;
				n++;
			}
            else
            {
                Com_sprintf(ctx->red_buf[i].main_text, sizeof(ctx->red_buf[i].main_text), "ctf %d %d %d %ld %d ", x, y, ctx->redsorted[i],
                    stats_get(cl_ent, STATS_SCORE), cl->ping > 999 ? 999 : cl->ping);

                line_len = (int)strlen(ctx->red_buf[i].main_text);
                if (legacy_len + line_len > 1024)
                {
                    red_rows_shown = i;
                    break;
                }
                legacy_len += line_len;

                ctx->elems[n].kind = UI_RAW;
                ctx->elems[n].u.raw.text = ctx->red_buf[i].main_text;
                n++;
            }
        }
        else
        {
            x = 0;
            y = 32 + 32 * (i%6);

            Com_sprintf(ctx->red_buf[i].main_text, sizeof(ctx->red_buf[i].main_text), "client %i %i %i %i %i %i ",
                x, y, ctx->redsorted[i], (int)stats_get(cl_ent, STATS_SCORE),
                cl->ping, (level.framenum - cl->resp.enterframe) / 600);

            line_len = (int)strlen(ctx->red_buf[i].main_text);
            if (legacy_len + line_len > 1024)
            {
                red_rows_shown = i;
                break;
            }
            legacy_len += line_len;

            ctx->elems[n].kind = UI_RAW;
            ctx->elems[n].u.raw.text = ctx->red_buf[i].main_text;
            n++;
            red_rows_shown = i + 1;

            if (stats_get(cl_ent, STATS_CAPTURES))
            {
                Com_sprintf (ctx->red_buf[i].capt_text, sizeof(ctx->red_buf[i].capt_text), "C:%i", (int)stats_get(cl_ent, STATS_CAPTURES));

                line_len = Board_LineLen("xv %i yv %i string2 \"%s\" ", x+32+80, y+24, ctx->red_buf[i].capt_text);
                if (legacy_len + line_len > 1024)
                    break;
                legacy_len += line_len;

                ctx->elems[n].kind = UI_TEXT;
                ctx->elems[n].u.text.x = x+32+80;
                ctx->elems[n].u.text.y = y+24;
                ctx->elems[n].u.text.text = ctx->red_buf[i].capt_text;
                ctx->elems[n].u.text.highlight = true;
                n++;
            }

            if (cl->rune)
            {
                switch (cl->rune->runetype)
                {
                    case 1:	 player_rune = "ST";    break;
                    case 2:	 player_rune = "RS";    break;
                    case 4:	 player_rune = "HA";    break;
                    case 8:	 player_rune = "RG";    break;
                }

                Com_sprintf(ctx->red_buf[i].rune_text, sizeof(ctx->red_buf[i].rune_text), "R:%s", player_rune);

                line_len = Board_LineLen("xv %i yv %i string2 \"%s\" ", x + 32 + 80, y + 16, ctx->red_buf[i].rune_text);
                if (legacy_len + line_len > 1024)
                    break;
                legacy_len += line_len;

                ctx->elems[n].kind = UI_TEXT;
                ctx->elems[n].u.text.x = x + 32 + 80;
                ctx->elems[n].u.text.y = y + 16;
                ctx->elems[n].u.text.text = ctx->red_buf[i].rune_text;
                ctx->elems[n].u.text.highlight = true;
                n++;
            }

            if (cl_ent == Query_DMVP())
            {
                line_len = Board_LineLen("xv %d yv %d picn dmvpicon ", x, y);
                if (legacy_len + line_len > 1024)
                    break;
                legacy_len += line_len;

                ctx->elems[n].kind = UI_PIC;
                ctx->elems[n].u.pic.x = x;
                ctx->elems[n].u.pic.y = y;
                ctx->elems[n].u.pic.stat_driven = false;
                ctx->elems[n].u.pic.image.name = "dmvpicon";
                n++;
            }
            else if (cl_ent == Query_OMVP())
            {
                line_len = Board_LineLen("xv %d yv %d picn omvpicon ", x, y);
                if (legacy_len + line_len > 1024)
                    break;
                legacy_len += line_len;

                ctx->elems[n].kind = UI_PIC;
                ctx->elems[n].u.pic.x = x;
                ctx->elems[n].u.pic.y = y;
                ctx->elems[n].u.pic.stat_driven = false;
                ctx->elems[n].u.pic.image.name = "omvpicon";
                n++;
            }

        }
        // END PLAY -- LM JORM

	}

    for (i=0 ; i<blue ; i++)
    {
        cl = &game.clients[ctx->bluesorted[i]];
        cl_ent = g_edicts + 1 + ctx->bluesorted[i];

        if (showsmall)
        {
            x = 160;
            y = 48 + 8 * i;

            if (cl->rune)
            {
                switch (cl->rune->runetype)
                {
                case 1:	 player_rune = "ST";    break;
                case 2:	 player_rune = "RS";    break;
                case 4:	 player_rune = "HA";    break;
                case 8:	 player_rune = "RG";    break;
                }

                line_len = Board_LineLen("xv %i yv %i string2 \"%s\" ", x + 32 + 56 + 80, y, player_rune);
                if (legacy_len + line_len > 1024)
                {
                    blue_rows_shown = i;
                    break;
                }
                legacy_len += line_len;

                ctx->elems[n].kind = UI_TEXT;
                ctx->elems[n].u.text.x = x + 32 + 56 + 80;
                ctx->elems[n].u.text.y = y;
                ctx->elems[n].u.text.text = player_rune;
                ctx->elems[n].u.text.highlight = true;
                n++;

            }

            if (cl_ent == Query_DMVP())
			{
				Com_sprintf(ctx->blue_buf[i].main_text, sizeof(ctx->blue_buf[i].main_text), "D%3d %3d %s", cl->resp.score, cl->ping, cl->pers.netname);
				ctx->blue_buf[i].main_text[19] = 0;

				/* FIXED: one rune line per row (the pre-conversion
				 * strcat path drew an MVP row's rune twice; see the
				 * red DMVP site) */
				line_len = Board_LineLen("xv %i yv %i string2 \"%s\" ", x, y, ctx->blue_buf[i].main_text);
				if (legacy_len + line_len > 1024)
				{
					blue_rows_shown = i;
					break;
				}
				legacy_len += line_len;

				ctx->elems[n].kind = UI_TEXT;
				ctx->elems[n].u.text.x = x;
				ctx->elems[n].u.text.y = y;
				ctx->elems[n].u.text.text = ctx->blue_buf[i].main_text;
				ctx->elems[n].u.text.highlight = true;
				n++;
			}
            else if (cl_ent == Query_OMVP())
			{
				Com_sprintf(ctx->blue_buf[i].main_text, sizeof(ctx->blue_buf[i].main_text), "O%3d %3d %s", cl->resp.score, cl->ping, cl->pers.netname);
				ctx->blue_buf[i].main_text[19] = 0;

				/* FIXED: one rune line per row (the pre-conversion
				 * strcat path drew an MVP row's rune twice; see the
				 * red DMVP site) */
				line_len = Board_LineLen("xv %i yv %i string2 \"%s\" ", x, y, ctx->blue_buf[i].main_text);
				if (legacy_len + line_len > 1024)
				{
					blue_rows_shown = i;
					break;
				}
				legacy_len += line_len;

				ctx->elems[n].kind = UI_TEXT;
				ctx->elems[n].u.text.x = x;
				ctx->elems[n].u.text.y = y;
				ctx->elems[n].u.text.text = ctx->blue_buf[i].main_text;
				ctx->elems[n].u.text.highlight = true;
				n++;
			}
			else
			{
				Com_sprintf(ctx->blue_buf[i].main_text, sizeof(ctx->blue_buf[i].main_text),
					"ctf %d %d %d %ld %d ",
					x, y,
					ctx->bluesorted[i],
					stats_get(cl_ent, STATS_SCORE),
					cl->ping > 999 ? 999 : cl->ping);

				line_len = (int)strlen(ctx->blue_buf[i].main_text);
				if (legacy_len + line_len > 1024)
				{
					blue_rows_shown = i;
					break;
				}
				legacy_len += line_len;

				ctx->elems[n].kind = UI_RAW;
				ctx->elems[n].u.raw.text = ctx->blue_buf[i].main_text;
				n++;
			}
        }
        else
        {
            x = 160;
            y = 32 + 32 * (i%6);

            Com_sprintf(ctx->blue_buf[i].main_text, sizeof(ctx->blue_buf[i].main_text), "client %i %i %i %i %i %i ",
                x, y, ctx->bluesorted[i], (int)stats_get(cl_ent, STATS_SCORE),
                cl->ping, (level.framenum - cl->resp.enterframe) / 600);

            line_len = (int)strlen(ctx->blue_buf[i].main_text);
            if (legacy_len + line_len > 1024)
            {
                blue_rows_shown = i;
                break;
            }
            legacy_len += line_len;

            ctx->elems[n].kind = UI_RAW;
            ctx->elems[n].u.raw.text = ctx->blue_buf[i].main_text;
            n++;
            blue_rows_shown = i + 1;

            if (stats_get(cl_ent, STATS_CAPTURES))
            {
                Com_sprintf (ctx->blue_buf[i].capt_text, sizeof(ctx->blue_buf[i].capt_text), "C:%i", (int)stats_get(cl_ent, STATS_CAPTURES));

                line_len = Board_LineLen("xv %i yv %i string2 \"%s\" ", x+32+80, y+24, ctx->blue_buf[i].capt_text);
                if (legacy_len + line_len > 1024)
                    break;
                legacy_len += line_len;

                ctx->elems[n].kind = UI_TEXT;
                ctx->elems[n].u.text.x = x+32+80;
                ctx->elems[n].u.text.y = y+24;
                ctx->elems[n].u.text.text = ctx->blue_buf[i].capt_text;
                ctx->elems[n].u.text.highlight = true;
                n++;
            }

            if (cl->rune)
            {
                switch (cl->rune->runetype)
                {
                    case 1:	 player_rune = "ST";    break;
                    case 2:	 player_rune = "RS";    break;
                    case 4:	 player_rune = "HA";    break;
                    case 8:	 player_rune = "RG";    break;
                }

                Com_sprintf(ctx->blue_buf[i].rune_text, sizeof(ctx->blue_buf[i].rune_text), "R:%s", player_rune);

                line_len = Board_LineLen("xv %i yv %i string2 \"%s\" ", x + 32 + 80, y + 16, ctx->blue_buf[i].rune_text);
                if (legacy_len + line_len > 1024)
                    break;
                legacy_len += line_len;

                ctx->elems[n].kind = UI_TEXT;
                ctx->elems[n].u.text.x = x + 32 + 80;
                ctx->elems[n].u.text.y = y + 16;
                ctx->elems[n].u.text.text = ctx->blue_buf[i].rune_text;
                ctx->elems[n].u.text.highlight = true;
                n++;
            }

            if (cl_ent == Query_DMVP())
            {
                line_len = Board_LineLen("xv %d yv %d picn dmvpicon ", x, y);
                if (legacy_len + line_len > 1024)
                    break;
                legacy_len += line_len;

                ctx->elems[n].kind = UI_PIC;
                ctx->elems[n].u.pic.x = x;
                ctx->elems[n].u.pic.y = y;
                ctx->elems[n].u.pic.stat_driven = false;
                ctx->elems[n].u.pic.image.name = "dmvpicon";
                n++;
            }
            else if (cl_ent == Query_OMVP())
            {
                line_len = Board_LineLen("xv %d yv %d picn omvpicon ", x, y);
                if (legacy_len + line_len > 1024)
                    break;
                legacy_len += line_len;

                ctx->elems[n].kind = UI_PIC;
                ctx->elems[n].u.pic.x = x;
                ctx->elems[n].u.pic.y = y;
                ctx->elems[n].u.pic.stat_driven = false;
                ctx->elems[n].u.pic.image.name = "omvpicon";
                n++;
            }

        }
        // END PLAY -- LM JORM
    }


    y = 32 * 8;

	if(MvpDisp)
	{
		mvp_n = 0;

		Com_sprintf(ctx->mvp_text[mvp_n], sizeof(ctx->mvp_text[mvp_n]), "*** %s MVPs ***", level.mapname);
		ctx->mvp_x[mvp_n] = 80; ctx->mvp_y[mvp_n] = y; mvp_n++;
		y += 8;

    	if(Railgun_Victor)
		{
			Com_sprintf(ctx->mvp_text[mvp_n], sizeof(ctx->mvp_text[mvp_n]), "Railgod -> %s", Railgun_Victor->client->pers.netname);
			ctx->mvp_x[mvp_n] = 100; ctx->mvp_y[mvp_n] = y; mvp_n++;
			y += 8;
		}

		Com_sprintf(ctx->mvp_text[mvp_n], sizeof(ctx->mvp_text[mvp_n]), "1) %s %4ld", Highscore_Table[0].Player, Highscore_Table[0].Score);
		ctx->mvp_x[mvp_n] = 130; ctx->mvp_y[mvp_n] = y; mvp_n++;
		y += 8;

		x = 0;

		for(i = 1; i < MAX_HIGHSCORE_ENTRIES; i++)
		{
			if(i == 4)
			{
				x = 220;
				y = 272;
    			if(Railgun_Victor)
					y += 8;
			}

			Com_sprintf(ctx->mvp_text[mvp_n], sizeof(ctx->mvp_text[mvp_n]), "%d) %15s %4ld", i + 1, Highscore_Table[i].Player, Highscore_Table[i].Score);
			ctx->mvp_x[mvp_n] = x; ctx->mvp_y[mvp_n] = y; mvp_n++;
			y += 8;
		}

		mvp_total_len = 0;
		for (i = 0; i < mvp_n; i++)
			mvp_total_len += Board_LineLen("xv %i yv %i string2 \"%s\" ", ctx->mvp_x[i], ctx->mvp_y[i], ctx->mvp_text[i]);

		if (legacy_len + mvp_total_len <= 1024)
		{
			mvp_admitted = true;
			legacy_len += mvp_total_len;

			for (i = 0; i < mvp_n; i++)
			{
				ctx->elems[n].kind = UI_TEXT;
				ctx->elems[n].u.text.x = ctx->mvp_x[i];
				ctx->elems[n].u.text.y = ctx->mvp_y[i];
				ctx->elems[n].u.text.text = ctx->mvp_text[i];
				ctx->elems[n].u.text.highlight = true;
				n++;
			}
		}
	}
	else
	{
		// Observers listing. Discovered quirk, preserved on purpose
		// (see this function's banner): the original accumulates every
		// non-empty category (red/blue/reg observers) into the SAME
		// string2 buffer without ever clearing it between them, and
		// re-copies the FULL accumulated buffer on every successful
		// per-category flush -- so when more than one category is
		// non-empty, an EARLIER category's lines get emitted again
		// inside every LATER category's flush. This is a genuine bug
		// in the shipped producer, not a deliberate repeat; it is
		// reproduced exactly below (els2_* mirrors string2, monotonic
		// across all three categories) rather than fixed, per this
		// pass's byte-for-byte requirement -- see the conversion notes.
		els2_n = 0;
		gate_ok = (red + blue + ctx->total_observers <= 16) ? true : false;

		if(ctx->red_observers)
		{
			x = 0;
			ctx->els2_x[els2_n] = x; ctx->els2_y[els2_n] = y;
			Com_sprintf(ctx->els2_text[els2_n], sizeof(ctx->els2_text[els2_n]), "Red Observers:");
			els2_n++;
			y += 8;
            if (gate_ok)
            {
                for (i = 0; i < ctx->red_observers; i++)
                {
                    cl = &game.clients[ctx->sorted_red_observers[i]];
                    cl_ent = g_edicts + 1 + ctx->sorted_red_observers[i];
                    ctx->els2_x[els2_n] = x; ctx->els2_y[els2_n] = y;
                    Com_sprintf(ctx->els2_text[els2_n], sizeof(ctx->els2_text[els2_n]), "%s", cl->pers.netname);
                    els2_n++;
                    y += 8;
                }

                sec_len = 0;
                for (i = els2_start; i < els2_n; i++)
                    sec_len += Board_LineLen("xv %i yv %i string2 \"%s\" ", ctx->els2_x[i], ctx->els2_y[i], ctx->els2_text[i]);
                els2_len = sec_len;

                if (legacy_len + els2_len <= 1024)
                {
                    for (i = els2_start; i < els2_n; i++)
                    {
                        ctx->elems[n].kind = UI_TEXT;
                        ctx->elems[n].u.text.x = ctx->els2_x[i];
                        ctx->elems[n].u.text.y = ctx->els2_y[i];
                        ctx->elems[n].u.text.text = ctx->els2_text[i];
                        ctx->elems[n].u.text.highlight = true;
                        n++;
                    }
                    legacy_len += els2_len;
                }
            }
		}

		if(ctx->blue_observers)
		{
			x = 160;
			ctx->els2_x[els2_n] = x; ctx->els2_y[els2_n] = y;
			Com_sprintf(ctx->els2_text[els2_n], sizeof(ctx->els2_text[els2_n]), "Blue Observers:");
			els2_n++;
			y += 8;
            if (gate_ok)
            {
                for (i = 0; i < ctx->blue_observers; i++)
                {
                    cl = &game.clients[ctx->sorted_blue_observers[i]];
                    cl_ent = g_edicts + 1 + ctx->sorted_blue_observers[i];
                    ctx->els2_x[els2_n] = x; ctx->els2_y[els2_n] = y;
                    Com_sprintf(ctx->els2_text[els2_n], sizeof(ctx->els2_text[els2_n]), "%s", cl->pers.netname);
                    els2_n++;
                    y += 8;
                }

                sec_len = 0;
                for (i = els2_start; i < els2_n; i++)
                    sec_len += Board_LineLen("xv %i yv %i string2 \"%s\" ", ctx->els2_x[i], ctx->els2_y[i], ctx->els2_text[i]);
                els2_len = sec_len;

                if (legacy_len + els2_len <= 1024)
                {
                    for (i = els2_start; i < els2_n; i++)
                    {
                        ctx->elems[n].kind = UI_TEXT;
                        ctx->elems[n].u.text.x = ctx->els2_x[i];
                        ctx->elems[n].u.text.y = ctx->els2_y[i];
                        ctx->elems[n].u.text.text = ctx->els2_text[i];
                        ctx->elems[n].u.text.highlight = true;
                        n++;
                    }
                    legacy_len += els2_len;
                }
            }
		}



		if(ctx->reg_observers)
		{
            if (gate_ok)
            {
                //give more space for the reg observers
                if (ctx->red_observers == 0 && ctx->blue_observers == 0)
                {
                    x = 80;
                    els2_start = els2_n;   /* FIXED: flush this category's slice only */
                    ctx->els2_x[els2_n] = x; ctx->els2_y[els2_n] = y;
                    Com_sprintf(ctx->els2_text[els2_n], sizeof(ctx->els2_text[els2_n]), "Observers:");
                    els2_n++;
                    y += 8;

                    //Do 2 obs per line

                    for (i = 0; i < ctx->reg_observers; i++)
                    {
                        x = (i % 3) * 130;

                        cl = &game.clients[ctx->sorted_reg_observers[i]];
                        cl_ent = g_edicts + 1 + ctx->sorted_reg_observers[i];
                        ctx->els2_x[els2_n] = x; ctx->els2_y[els2_n] = y;
                        Com_sprintf(ctx->els2_text[els2_n], sizeof(ctx->els2_text[els2_n]), "%s", cl->pers.netname);
                        els2_n++;

                        if ((i % 3) == 2)
                            y += 8;
                    }

                    sec_len = 0;
                    for (i = els2_start; i < els2_n; i++)
                        sec_len += Board_LineLen("xv %i yv %i string2 \"%s\" ", ctx->els2_x[i], ctx->els2_y[i], ctx->els2_text[i]);
                    els2_len = sec_len;

                    if (legacy_len + els2_len <= 1024)
                    {
                        for (i = els2_start; i < els2_n; i++)
                        {
                            ctx->elems[n].kind = UI_TEXT;
                            ctx->elems[n].u.text.x = ctx->els2_x[i];
                            ctx->elems[n].u.text.y = ctx->els2_y[i];
                            ctx->elems[n].u.text.text = ctx->els2_text[i];
                            ctx->elems[n].u.text.highlight = true;
                            n++;
                        }
                        legacy_len += els2_len;
                    }
                }
                else
                {
                    x = 80;
                    els2_start = els2_n;   /* FIXED: flush this category's slice only */
                    ctx->els2_x[els2_n] = x; ctx->els2_y[els2_n] = y;
                    Com_sprintf(ctx->els2_text[els2_n], sizeof(ctx->els2_text[els2_n]), "Observers:");
                    els2_n++;
                    y += 8;

                    for (i = 0; i < ctx->reg_observers; i++)
                    {
                        cl = &game.clients[ctx->sorted_reg_observers[i]];
                        cl_ent = g_edicts + 1 + ctx->sorted_reg_observers[i];
                        ctx->els2_x[els2_n] = x; ctx->els2_y[els2_n] = y;
                        Com_sprintf(ctx->els2_text[els2_n], sizeof(ctx->els2_text[els2_n]), "%s", cl->pers.netname);
                        els2_n++;
                        y += 8;
                    }

                    sec_len = 0;
                    for (i = els2_start; i < els2_n; i++)
                        sec_len += Board_LineLen("xv %i yv %i string2 \"%s\" ", ctx->els2_x[i], ctx->els2_y[i], ctx->els2_text[i]);
                    els2_len = sec_len;

                    if (legacy_len + els2_len <= 1024)
                    {
                        for (i = els2_start; i < els2_n; i++)
                        {
                            ctx->elems[n].kind = UI_TEXT;
                            ctx->elems[n].u.text.x = ctx->els2_x[i];
                            ctx->elems[n].u.text.y = ctx->els2_y[i];
                            ctx->elems[n].u.text.text = ctx->els2_text[i];
                            ctx->elems[n].u.text.highlight = true;
                            n++;
                        }
                        legacy_len += els2_len;
                    }
                }
            }
		}
	}


    // END PLAY -- LM JORM


    // Don't show captures graphic if TEAMS and FLAGS turned off (DM MODE)
    //Show them - bat
	//if (!((int)ctfflags->value & CTF_TEAM_NOTEAMS) ||
    //    !((int)ctfflags->value & CTF_FLAGS_NOFLAGS))
    {
        foot_len = Board_LineLen("xv %i yv %i picn %s ", 0, 0, "redlion_i")
            + Board_LineLen("xv %i yv %i picn %s ", 160, 0, "bluewolf_i")
            + Board_LineLen("xv %i yv %i picn %s ", 32, 0, "redtag")
            + Board_LineLen("xv %i yv %i picn %s ", 192, 0, "bluetag")
            + Board_LineLen("xv %i yv %i string2 \"%s\" ", 36, 0, "FC:")
            + Board_LineLen("xv %i yv %i string2 \"%s\" ", 36, 8, ctx->redfc)
            + Board_LineLen("xv %i yv %i string2 \"%s\" ", 36, 16, "Runes")
            + Board_LineLen("xv %i yv %i string2 \"%s\" ", 36, 24, ctx->red_runes)
            + Board_LineLen("xv %i yv %i string2 \"%s\" ", 196, 0, "FC:")
            + Board_LineLen("xv %i yv %i string2 \"%s\" ", 196, 8, ctx->bluefc)
            + Board_LineLen("xv %i yv %i string2 \"%s\" ", 196, 16, "Runes")
            + Board_LineLen("xv %i yv %i string2 \"%s\" ", 196, 24, ctx->blue_runes);

        if (legacy_len + foot_len < 1024)
        {
            footer_admitted = true;
            legacy_len += foot_len;

            ctx->elems[n].kind=UI_PIC; ctx->elems[n].u.pic.x=0;   ctx->elems[n].u.pic.y=0; ctx->elems[n].u.pic.stat_driven=false; ctx->elems[n].u.pic.image.name="redlion_i";   n++;
            ctx->elems[n].kind=UI_PIC; ctx->elems[n].u.pic.x=160; ctx->elems[n].u.pic.y=0; ctx->elems[n].u.pic.stat_driven=false; ctx->elems[n].u.pic.image.name="bluewolf_i"; n++;
            ctx->elems[n].kind=UI_PIC; ctx->elems[n].u.pic.x=32;  ctx->elems[n].u.pic.y=0; ctx->elems[n].u.pic.stat_driven=false; ctx->elems[n].u.pic.image.name="redtag";     n++;
            ctx->elems[n].kind=UI_PIC; ctx->elems[n].u.pic.x=192; ctx->elems[n].u.pic.y=0; ctx->elems[n].u.pic.stat_driven=false; ctx->elems[n].u.pic.image.name="bluetag";    n++;

            ctx->elems[n].kind=UI_TEXT; ctx->elems[n].u.text.x=36;  ctx->elems[n].u.text.y=0;  ctx->elems[n].u.text.text="FC:";     ctx->elems[n].u.text.highlight=true; n++;
            ctx->elems[n].kind=UI_TEXT; ctx->elems[n].u.text.x=36;  ctx->elems[n].u.text.y=8;  ctx->elems[n].u.text.text=ctx->redfc;     ctx->elems[n].u.text.highlight=true; n++;
            ctx->elems[n].kind=UI_TEXT; ctx->elems[n].u.text.x=36;  ctx->elems[n].u.text.y=16; ctx->elems[n].u.text.text="Runes";   ctx->elems[n].u.text.highlight=true; n++;
            ctx->elems[n].kind=UI_TEXT; ctx->elems[n].u.text.x=36;  ctx->elems[n].u.text.y=24; ctx->elems[n].u.text.text=ctx->red_runes; ctx->elems[n].u.text.highlight=true; n++;

            ctx->elems[n].kind=UI_TEXT; ctx->elems[n].u.text.x=196; ctx->elems[n].u.text.y=0;  ctx->elems[n].u.text.text="FC:";      ctx->elems[n].u.text.highlight=true; n++;
            ctx->elems[n].kind=UI_TEXT; ctx->elems[n].u.text.x=196; ctx->elems[n].u.text.y=8;  ctx->elems[n].u.text.text=ctx->bluefc;     ctx->elems[n].u.text.highlight=true; n++;
            ctx->elems[n].kind=UI_TEXT; ctx->elems[n].u.text.x=196; ctx->elems[n].u.text.y=16; ctx->elems[n].u.text.text="Runes";    ctx->elems[n].u.text.highlight=true; n++;
            ctx->elems[n].kind=UI_TEXT; ctx->elems[n].u.text.x=196; ctx->elems[n].u.text.y=24; ctx->elems[n].u.text.text=ctx->blue_runes; ctx->elems[n].u.text.highlight=true; n++;
        }
    }

    // END PLAY -- LM JORM

    // BUZZKILL - team pickup totals footer.
    //
    // These sixteen counters plus the two team scores were being accumulated
    // above and then thrown away -- nothing ever drew them. Rendered here.
    //
    // Each team gets the 20-character half of the 320-wide layout it already
    // owns: red at xv 0, blue at xv 160.
    //   itm  Q/S/A/M  = quad, power shield, red armor, mega health
    //   rne  S/H/G/R  = strength, haste, regen, resist
    {
        rows = (red > blue) ? red : blue;
        // Place the footer below the active scoreboard's roster rows.
        fy = showsmall ? (48 + 8 * rows + 8)
                       : (32 + 32 * ((rows > 6 ? 6 : rows)) + 8);

        // only draw it when the roster leaves vertical room for three rows
        if (fy + 16 <= 232)
        {
            pf_len = Board_LineLen("xv 0 yv %i string2 \"RED %3i pts\" ", fy, ctx->redscore)
                + Board_LineLen("xv 0 yv %i string2 \"itm Q%2i S%2i A%2i M%2i\" ", fy + 8, ctx->red_item_quad, ctx->red_item_shield, ctx->red_item_armor, ctx->red_item_mega)
                + Board_LineLen("xv 0 yv %i string2 \"rne S%2i H%2i G%2i R%2i\" ", fy + 16, ctx->red_rune_strength, ctx->red_rune_haste, ctx->red_rune_regen, ctx->red_rune_resist)
                + Board_LineLen("xv 160 yv %i string2 \"BLUE %3i pts\" ", fy, ctx->bluescore)
                + Board_LineLen("xv 160 yv %i string2 \"itm Q%2i S%2i A%2i M%2i\" ", fy + 8, ctx->blue_item_quad, ctx->blue_item_shield, ctx->blue_item_armor, ctx->blue_item_mega)
                + Board_LineLen("xv 160 yv %i string2 \"rne S%2i H%2i G%2i R%2i\" ", fy + 16, ctx->blue_rune_strength, ctx->blue_rune_haste, ctx->blue_rune_regen, ctx->blue_rune_resist);

            if (legacy_len + pf_len < 1024)
            {
                pf_admitted = true;
                legacy_len += pf_len;

                Com_sprintf(ctx->pf_text[0], sizeof(ctx->pf_text[0]), "RED %3i pts", ctx->redscore);
                Com_sprintf(ctx->pf_text[1], sizeof(ctx->pf_text[1]), "itm Q%2i S%2i A%2i M%2i", ctx->red_item_quad, ctx->red_item_shield, ctx->red_item_armor, ctx->red_item_mega);
                Com_sprintf(ctx->pf_text[2], sizeof(ctx->pf_text[2]), "rne S%2i H%2i G%2i R%2i", ctx->red_rune_strength, ctx->red_rune_haste, ctx->red_rune_regen, ctx->red_rune_resist);
                Com_sprintf(ctx->pf_text[3], sizeof(ctx->pf_text[3]), "BLUE %3i pts", ctx->bluescore);
                Com_sprintf(ctx->pf_text[4], sizeof(ctx->pf_text[4]), "itm Q%2i S%2i A%2i M%2i", ctx->blue_item_quad, ctx->blue_item_shield, ctx->blue_item_armor, ctx->blue_item_mega);
                Com_sprintf(ctx->pf_text[5], sizeof(ctx->pf_text[5]), "rne S%2i H%2i G%2i R%2i", ctx->blue_rune_strength, ctx->blue_rune_haste, ctx->blue_rune_regen, ctx->blue_rune_resist);

                ctx->elems[n].kind=UI_TEXT; ctx->elems[n].u.text.x=0;   ctx->elems[n].u.text.y=fy;      ctx->elems[n].u.text.text=ctx->pf_text[0]; ctx->elems[n].u.text.highlight=true; n++;
                ctx->elems[n].kind=UI_TEXT; ctx->elems[n].u.text.x=0;   ctx->elems[n].u.text.y=fy + 8;  ctx->elems[n].u.text.text=ctx->pf_text[1]; ctx->elems[n].u.text.highlight=true; n++;
                ctx->elems[n].kind=UI_TEXT; ctx->elems[n].u.text.x=0;   ctx->elems[n].u.text.y=fy + 16; ctx->elems[n].u.text.text=ctx->pf_text[2]; ctx->elems[n].u.text.highlight=true; n++;
                ctx->elems[n].kind=UI_TEXT; ctx->elems[n].u.text.x=160; ctx->elems[n].u.text.y=fy;      ctx->elems[n].u.text.text=ctx->pf_text[3]; ctx->elems[n].u.text.highlight=true; n++;
                ctx->elems[n].kind=UI_TEXT; ctx->elems[n].u.text.x=160; ctx->elems[n].u.text.y=fy + 8;  ctx->elems[n].u.text.text=ctx->pf_text[4]; ctx->elems[n].u.text.highlight=true; n++;
                ctx->elems[n].kind=UI_TEXT; ctx->elems[n].u.text.x=160; ctx->elems[n].u.text.y=fy + 16; ctx->elems[n].u.text.text=ctx->pf_text[5]; ctx->elems[n].u.text.highlight=true; n++;
            }
        }
    }


    red_clamp_dropped = ctx->red - red;
    blue_clamp_dropped = ctx->blue - blue;

    legacy_dropped = red_clamp_dropped + blue_clamp_dropped
                   + (red - red_rows_shown) + (blue - blue_rows_shown);

    if (MvpDisp)
    {
        if (!mvp_admitted)
            legacy_dropped += mvp_n;
    }
    else
    {
        if (!gate_ok && ctx->total_observers > 0)
            legacy_dropped += ctx->total_observers;
    }

    if (!footer_admitted)
        legacy_dropped += 1;

    if (!pf_admitted)
        legacy_dropped += 1;

    screen->elems = ctx->elems;
    screen->count = n;

    return legacy_dropped;
}

// Dispatches DeathmatchScoreboardMessage's screen build across the
// density ladder (ui_layout.h): UI_BOARD_FULL is today's format,
// unchanged; UI_BOARD_CONDENSED and UI_BOARD_MINIMAL share a simpler
// builder that differs only in how much each row shows.
static int DMScore_Build(void *userdata, ui_board_variant_t variant, ui_screen_t *screen)
{
    dmscore_ctx_t *ctx = (dmscore_ctx_t *)userdata;

    if (variant == UI_BOARD_FULL)
        return DMScore_BuildFull(ctx, screen);

    return DMScore_BuildCompact(ctx, variant, screen);
}


/*
==================
DeathmatchScoreboardMessage

==================
*/
void DeathmatchScoreboardMessage (edict_t *ent, edict_t *killer)
{
    char                storage[UI_LAYOUT_BUDGET];
    ui_buf_t            sb;
    dmscore_ctx_t       ctx;

	int     bluescore, redscore;  // TEAM PLAY -- LM_JORM
    int     bluecaps, redcaps;  // TEAM PLAY -- LM_JORM
    int     blue, red;  // TEAM PLAY -- LM_JORM

    // BUZZKILL - ADVANCED ANALYTICS SCOREBOARD - START
    int     blue_rune_strength = 0;
    int     blue_rune_haste = 0;
    int     blue_rune_regen = 0;
    int     blue_rune_resist = 0;
    int     blue_item_mega = 0;
    int     blue_item_quad = 0;
    int     blue_item_armor = 0;
    int     blue_item_shield = 0;
    int     red_rune_strength = 0;
    int     red_rune_haste = 0;
    int     red_rune_regen = 0;
    int     red_rune_resist = 0;
    int     red_item_mega = 0;
    int     red_item_quad = 0;
    int     red_item_armor = 0;
    int     red_item_shield = 0;
    int     bfctest = 0;
    int     rfctest = 0;
    /* The layout formatter requires a non-NULL empty value. */
    char*   redfc = "-";
    char*   bluefc = "-";
    char*   red_runes = "-";
    char*   blue_runes = "-";
    int     red_rune_acc = 0;
    int     blue_rune_acc = 0;
    // BUZZKILL - ADVANCED ANALYTICS SCOREBOARD - END

    int     i;
    int     j;
    int     k;
    int     l;

    int     redsorted[MAX_CLIENTS];
    int     redsortedscores[MAX_CLIENTS];
    int     bluesorted[MAX_CLIENTS];
    int     bluesortedscores[MAX_CLIENTS];
    int     sorted_reg_observers[MAX_CLIENTS];
	int     sorted_red_observers[MAX_CLIENTS];
	int     sorted_blue_observers[MAX_CLIENTS];
    int     reg_observers = 0;
    int     red_observers = 0;
    int     blue_observers = 0;
    int     total_observers = 0;

    int     score;

    // BUZZKILL - ADVANCED ANALYTICS SCOREBOARD - START
    int     rune_strength;
    int     rune_haste;
    int     rune_regen;
    int     rune_resist;
    int     item_mega;
    int     item_armor;
    int     item_shield;
    int     item_quad;
    // BUZZKILL - ADVANCED ANALYTICS SCOREBOARD - END

    gclient_t   *cl;
    edict_t     *cl_ent;

    qboolean    is_red_fc;
    qboolean    is_blue_fc;

    int         dropped;
    ui_board_variant_t variant_used;

    is_red_fc = false;
    is_blue_fc = false;

    bluescore = bluecaps = blue = 0; // TEAM PLAY -- LM_JORM
    redscore = redcaps = red = 0;  // TEAM PLAY -- LM_JORM

	// sort the clients by score -- unchanged from the hand-written version
    for (i=0; i<game.maxclients; i++)
    {
        cl_ent = g_edicts + 1 + i;
        cl = &game.clients[i];

        //if (!cl_ent->inuse || game.clients[i].resp.spectator)
        //bat allow spectators to be on teams.
		if(!cl_ent->inuse)
            continue;

		//sprintf(DBuffer, "hud t %d p %d r %d", ent->client->ctf.teamnum,
		//	ent->client->pers.spectator, ent->client->resp.spectator);
		//Debug_Show(DBuffer);


		//sprintf(DBuffer, "hud h %d", cl_ent->health);
		//Debug_Show(DBuffer);

		score = stats_get(cl_ent, STATS_SCORE);

        // BUZZKILL - ADVANCED ANALYTICS SCOREBOARD - START
        rune_strength = (int)stats_get(cl_ent, STATS_RUNE_STRENGTH);
        rune_haste    = (int)stats_get(cl_ent, STATS_RUNE_HASTE);
        rune_regen    = (int)stats_get(cl_ent, STATS_RUNE_REGEN);
        rune_resist   = (int)stats_get(cl_ent, STATS_RUNE_RESIST);
        item_quad     = (int)stats_get(cl_ent, STATS_ITEM_QUAD);
        item_shield   = (int)stats_get(cl_ent, STATS_ITEM_SHIELD);
        item_armor    = (int)stats_get(cl_ent, STATS_ITEM_ARMOR);
        item_mega     = (int)stats_get(cl_ent, STATS_ITEM_MEGA);
        // BUZZKILL - ADVANCED ANALYTICS SCOREBOARD - END

        if (cl_ent->client->ctf.teamnum == CTF_TEAM_RED) // RED TEAM
        {
            redscore += score;
            redcaps += stats_get(cl_ent, STATS_CAPTURES);

            // BUZZKILL - ADVANCED ANALYTICS SCOREBOARD - START
            red_rune_strength += rune_strength;
            red_rune_haste += rune_haste;
            red_rune_regen += rune_regen;
            red_rune_resist += rune_resist;
            red_item_quad += item_quad;
            red_item_shield += item_shield;
            red_item_armor += item_armor;
            red_item_mega += item_mega;

            if (cl->rune)
            {
                red_rune_acc += cl->rune->runetype;
            }
            else
            {
                red_rune_acc += 0;
            }

            is_red_fc = stats_get(cl_ent, STATS_IS_FC);

            if (is_red_fc)
                redfc = cl_ent->client->pers.netname;

            switch (red_rune_acc)
            {
                case 1:	 red_runes = "ST";    break;
                case 2:	 red_runes = "RS";    break;
                case 3:	 red_runes = "ST RS";    break;
                case 4:	 red_runes = "HA";    break;
                case 5:	 red_runes = "ST HA";    break;
                case 6:	 red_runes = "RS HA";    break;
                case 7:	 red_runes = "ST RS HA";    break;
                case 8:	 red_runes = "RG";    break;
                case 9:	 red_runes = "ST RG";    break;
                case 10: red_runes = "RS RG";    break;
                case 11: red_runes = "ST RS RG";    break;
                case 12: red_runes = "HA RG";    break;
                case 13: red_runes = "ST HA RG";    break;
                case 14: red_runes = "RS HA RG";    break;
                case 15: red_runes = "ST RS HA RG";    break;
                default: red_runes = " ";    break;
            }
            // BUZZKILL - ADVANCED ANALYTICS SCOREBOARD - END

            for (j=0 ; j<red ; j++)
            {
                if (score > redsortedscores[j])
                    break;
            }
            for (k=red ; k>j ; k--)
            {
                redsorted[k] = redsorted[k-1];
                redsortedscores[k] = redsortedscores[k-1];
            }
            redsorted[j] = i;
            redsortedscores[j] = score;
            red++;
            for (l = 0; l < red; l++)
            {
                rfctest += stats_get(cl_ent, STATS_IS_FC);
                if (l == red && rfctest == 0)
                    redfc = "";
            }
        }
        else if (cl_ent->client->ctf.teamnum == CTF_TEAM_BLUE) // BLUE TEAM
        {
            bluescore += score;
            bluecaps  += stats_get(cl_ent, STATS_CAPTURES);

            // BUZZKILL - ADVANCED ANALYTICS SCOREBOARD - START
            blue_rune_strength += rune_strength;
            blue_rune_haste += rune_haste;
            blue_rune_regen += rune_regen;
            blue_rune_resist += rune_resist;
            blue_item_quad += item_quad;
            blue_item_shield += item_shield;
            blue_item_armor += item_armor;
            blue_item_mega += item_mega;

            if (cl->rune)
            {
                blue_rune_acc += cl->rune->runetype;
            }
            else
            {
                blue_rune_acc += 0;
            }

            is_blue_fc = stats_get(cl_ent, STATS_IS_FC);

            if (is_blue_fc)
                bluefc = cl_ent->client->pers.netname;

            switch(blue_rune_acc)
            {
                case 1:	 blue_runes = "ST";    break;
                case 2:	 blue_runes = "RS";    break;
                case 3:	 blue_runes = "ST RS";    break;
                case 4:	 blue_runes = "HA";    break;
                case 5:	 blue_runes = "ST HA";    break;
                case 6:	 blue_runes = "RS HA";    break;
                case 7:	 blue_runes = "ST RS HA";    break;
                case 8:	 blue_runes = "RG";    break;
                case 9:	 blue_runes = "ST RG";    break;
                case 10: blue_runes = "RS RG";    break;
                case 11: blue_runes = "ST RS RG";    break;
                case 12: blue_runes = "HA RG";    break;
                case 13: blue_runes = "ST HA RG";    break;
                case 14: blue_runes = "RS HA RG";    break;
                case 15: blue_runes = "ST RS HA RG";    break;
                default: blue_runes = " ";    break;
            }
            // BUZZKILL - ADVANCED ANALYTICS SCOREBOARD - END

            for (j=0 ; j<blue ; j++)
            {
                if (score > bluesortedscores[j])
                    break;
            }
            for (k=blue ; k>j ; k--)
            {
                bluesorted[k] = bluesorted[k-1];
                bluesortedscores[k] = bluesortedscores[k-1];
            }
            bluesorted[j] = i;
            bluesortedscores[j] = score;
            blue++;
            for (l = 0; l < red; l++)
            {
                bfctest += stats_get(cl_ent, STATS_IS_FC);
                if (l == blue && bfctest == 0)
                    bluefc = "";
            }
        }
        else if (cl_ent->client->ctf.teamnum == CTF_TEAM_OBSERVER_BLUE)
		{
			sorted_blue_observers[blue_observers] = i;
			blue_observers++;
            total_observers++;
		}
        else if (cl_ent->client->ctf.teamnum == CTF_TEAM_OBSERVER_RED)
		{
			sorted_red_observers[red_observers] = i;
			red_observers++;
            total_observers++;
		}
		else
		{
			sorted_reg_observers[reg_observers] = i;
			reg_observers++;
            total_observers++;
		}
    }

    // Hand the per-team aggregates to the density ladder.
    memcpy(ctx.redsorted, redsorted, sizeof(ctx.redsorted));
    memcpy(ctx.bluesorted, bluesorted, sizeof(ctx.bluesorted));
    ctx.red = red;
    ctx.blue = blue;
    ctx.redscore = redscore;
    ctx.bluescore = bluescore;
    ctx.redcaps = redcaps;
    ctx.bluecaps = bluecaps;

    ctx.red_rune_strength = red_rune_strength;
    ctx.red_rune_haste = red_rune_haste;
    ctx.red_rune_regen = red_rune_regen;
    ctx.red_rune_resist = red_rune_resist;
    ctx.red_item_quad = red_item_quad;
    ctx.red_item_shield = red_item_shield;
    ctx.red_item_armor = red_item_armor;
    ctx.red_item_mega = red_item_mega;
    ctx.blue_rune_strength = blue_rune_strength;
    ctx.blue_rune_haste = blue_rune_haste;
    ctx.blue_rune_regen = blue_rune_regen;
    ctx.blue_rune_resist = blue_rune_resist;
    ctx.blue_item_quad = blue_item_quad;
    ctx.blue_item_shield = blue_item_shield;
    ctx.blue_item_armor = blue_item_armor;
    ctx.blue_item_mega = blue_item_mega;

    ctx.redfc = redfc;
    ctx.bluefc = bluefc;
    ctx.red_runes = red_runes;
    ctx.blue_runes = blue_runes;

    memcpy(ctx.sorted_reg_observers, sorted_reg_observers, sizeof(ctx.sorted_reg_observers));
    memcpy(ctx.sorted_red_observers, sorted_red_observers, sizeof(ctx.sorted_red_observers));
    memcpy(ctx.sorted_blue_observers, sorted_blue_observers, sizeof(ctx.sorted_blue_observers));
    ctx.reg_observers = reg_observers;
    ctx.red_observers = red_observers;
    ctx.blue_observers = blue_observers;
    ctx.total_observers = total_observers;

    ui_buf_init(&sb, storage, sizeof(storage));
    dropped = ui_layout_compile_ladder(&ctx, DMScore_Build, &sb, &variant_used);
    Board_LogVariant("DeathmatchScoreboard", variant_used, dropped, "element");

    gi.WriteByte (svc_layout);
    gi.WriteString (storage);

}


/*
==================
DeathmatchScoreboard

Draw instead of help message.
Note that it isn't that hard to overflow the 1400 byte message limit!
==================
*/
void DeathmatchScoreboard (edict_t *ent)
{
	DeathmatchScoreboardMessage (ent, ent->enemy);
	gi.unicast (ent, true);
}

// ADC
/*
==================
SquadboardMessage

==================
*/
void SquadboardMessage (edict_t *ent, edict_t *killer)
{
	CTFSquadboardMessage (ent, killer);
}
// ADC

// ADC
/*
==================
Squadboard

Draw instead of help message.
Note that it isn't that hard to overflow the 1400 byte message limit!
==================
*/
void Squadboard (edict_t *ent)
{
	SquadboardMessage (ent, ent->enemy);
	gi.unicast (ent, true);
}
// ADC

/*
==================
CTFSquadboardMessage
==================
*/
// One squad-board row can draw a category header line (only when the
// squad changes) plus the player's own status line -- up to 16 rows,
// 2 lines each, plus the 3-element header (2 pics + title).
#define CTFSQUADBOARD_MAX_ROWS	16

// Fixed screen storage plus the sorted roster every rung of the
// density ladder (ui_layout.h) draws from.
typedef struct
{
	ui_elem_t	elems[3 + 2 * CTFSQUADBOARD_MAX_ROWS];
	char		row_text[CTFSQUADBOARD_MAX_ROWS][UI_CELL_LEN];

	gclient_t	*sortedClients[MAX_CLIENTS];
	int			sortedCount;
	int			teamOfInterest;
	int			widestName;
} squadboard_ctx_t;

// Builds CTFSquadboard's screen for one rung of the density ladder.
// UI_BOARD_FULL reproduces the original's per-row 1000-byte admission
// pass (Board_LineLen, see its banner) and its category header lines,
// byte-identical to before the ladder existed -- including the quirk
// in how its running length is seeded (see the inline comment at the
// admission loop). UI_BOARD_CONDENSED drops the category header lines
// (readiness, not squad assignment, is what this board is actually
// for) and shortens the name field, keeping every player's full
// status on one line each. UI_BOARD_MINIMAL keeps name and a single
// ready/not-ready marker -- the board's headline "number" when the
// underlying value is boolean rather than numeric.
static int CTFSquadboard_Build(void *userdata, ui_board_variant_t variant, ui_screen_t *screen)
{
	squadboard_ctx_t	*ctx = (squadboard_ctx_t *)userdata;
	int		row_count;
	int		n;
	int		i;

	row_count = (ctx->sortedCount < CTFSQUADBOARD_MAX_ROWS) ? ctx->sortedCount : CTFSQUADBOARD_MAX_ROWS;

	n = 0;

	ctx->elems[n].kind = UI_PIC;
	ctx->elems[n].u.pic.x = 0;
	ctx->elems[n].u.pic.y = 0;
	ctx->elems[n].u.pic.stat_driven = false;
	ctx->elems[n].u.pic.image.name = (ctx->teamOfInterest == 0) ? "redlion_i" : "bluewolf_i";
	n++;

	ctx->elems[n].kind = UI_PIC;
	ctx->elems[n].u.pic.x = 32;
	ctx->elems[n].u.pic.y = 0;
	ctx->elems[n].u.pic.stat_driven = false;
	ctx->elems[n].u.pic.image.name = (ctx->teamOfInterest == 0) ? "redtag" : "bluetag";
	n++;

	ctx->elems[n].kind = UI_TEXT;
	ctx->elems[n].u.text.x = 48;
	ctx->elems[n].u.text.y = 10;
	ctx->elems[n].u.text.text = "Squad Board";
	ctx->elems[n].u.text.highlight = false;
	n++;

	if (variant == UI_BOARD_FULL)
	{
		qboolean	row_has_header[CTFSQUADBOARD_MAX_ROWS];
		int			row_header_y[CTFSQUADBOARD_MAX_ROWS];
		int			row_status_y[CTFSQUADBOARD_MAX_ROWS];
		qboolean	row_ready[CTFSQUADBOARD_MAX_ROWS];
		qboolean	row_admitted[CTFSQUADBOARD_MAX_ROWS];
		char		*squad;
		char		statusStart[MAX_STATUS_LEN];
		int			greenStatusLen;
		int			numCategoryLines;
		int			header_actual_len;
		int			legacy_len;
		qboolean	legacy_admitted_once;
		int			entry_len;
		int			legacy_dropped;

		squad = 0;
		numCategoryLines = 0;
		greenStatusLen = (int)strlen(GREEN_STATUS_STR);

		header_actual_len = (int)strlen(ctx->teamOfInterest == 0 ?
			"xv 0 yv 0 picn redlion_i xv 32 yv 0 picn redtag " :
			"xv 0 yv 0 picn bluewolf_i xv 32 yv 0 picn bluetag ")
			+ (int)strlen("xv 48 yv 10 string \"Squad Board\" ");

		// Quirk carried over on purpose: the running length ("legacy_len")
		// is never charged for the header's own bytes until the FIRST row
		// admission recomputes it -- so the very first row's admission
		// check is tested against the full 1000-byte budget, not
		// (1000 - header bytes). Every check after that first admission
		// correctly includes the header. Reproduced exactly (see
		// legacy_admitted_once) rather than "fixed", since a fix here
		// would be a behavior change the original ui_layout.h conversion
		// did not make and this pass does not either.
		legacy_len = 0;
		legacy_admitted_once = false;

		for (i = 0; i < row_count; i++)
		{
			row_has_header[i] = (!squad || Q_stricmp(squad, ctx->sortedClients[i]->pers.squad)) ? true : false;

			if (row_has_header[i])
			{
				squad = ctx->sortedClients[i]->pers.squad;
				row_header_y[i] = 42 + i * 8 + numCategoryLines * 8;
				numCategoryLines++;
			}
			else
			{
				row_header_y[i] = 0; // unused
			}

			row_status_y[i] = 42 + i * 8 + numCategoryLines * 8;

			strncpy(statusStart, ctx->sortedClients[i]->pers.squadStatus, greenStatusLen);
			statusStart[greenStatusLen] = 0;
			row_ready[i] = !Q_stricmp(statusStart, GREEN_STATUS_STR);

			Com_sprintf(ctx->row_text[i], UI_CELL_LEN, "   %-*s %s",
				ctx->widestName, ctx->sortedClients[i]->pers.netname,
				ctx->sortedClients[i]->pers.squadStatus);

			entry_len = (row_has_header[i] ?
					Board_LineLen("xv 0 yv %d string \"%s\" ", row_header_y[i], ctx->sortedClients[i]->pers.squad) : 0)
				+ Board_LineLen("xv 0 yv %d %s \"%s\" ", row_status_y[i],
					row_ready[i] ? "string2" : "string", ctx->row_text[i]);

			row_admitted[i] = (legacy_len + entry_len < 1000);

			if (row_admitted[i])
			{
				legacy_len = legacy_admitted_once ? (legacy_len + entry_len) : (header_actual_len + entry_len);
				legacy_admitted_once = true;
			}
		}

		legacy_dropped = 0;
		for (i = 0; i < row_count; i++)
			if (!row_admitted[i])
				legacy_dropped++;

		for (i = 0; i < row_count; i++)
		{
			if (!row_admitted[i])
				continue;

			if (row_has_header[i])
			{
				ctx->elems[n].kind = UI_TEXT;
				ctx->elems[n].u.text.x = 0;
				ctx->elems[n].u.text.y = row_header_y[i];
				ctx->elems[n].u.text.text = ctx->sortedClients[i]->pers.squad;
				ctx->elems[n].u.text.highlight = false;
				n++;
			}

			ctx->elems[n].kind = UI_TEXT;
			ctx->elems[n].u.text.x = 0;
			ctx->elems[n].u.text.y = row_status_y[i];
			ctx->elems[n].u.text.text = ctx->row_text[i];
			ctx->elems[n].u.text.highlight = row_ready[i];
			n++;
		}

		screen->elems = ctx->elems;
		screen->count = n;

		return legacy_dropped;
	}

	// UI_BOARD_CONDENSED and UI_BOARD_MINIMAL: no category header lines
	// and no legacy admission pass -- both are new formats that never
	// shipped before the ladder existed, so every player is offered and
	// ui_layout_compile's own budget (UI_LAYOUT_BUDGET) decides what
	// fits.
	{
		char		statusStart[MAX_STATUS_LEN];
		int			greenStatusLen;
		qboolean	ready;

		greenStatusLen = (int)strlen(GREEN_STATUS_STR);

		for (i = 0; i < row_count; i++)
		{
			strncpy(statusStart, ctx->sortedClients[i]->pers.squadStatus, greenStatusLen);
			statusStart[greenStatusLen] = 0;
			ready = !Q_stricmp(statusStart, GREEN_STATUS_STR);

			if (variant == UI_BOARD_MINIMAL)
				Com_sprintf(ctx->row_text[i], UI_CELL_LEN, "%-15s %s",
					ctx->sortedClients[i]->pers.netname, ready ? "RDY" : "---");
			else
				Com_sprintf(ctx->row_text[i], UI_CELL_LEN, "%-10s %s",
					ctx->sortedClients[i]->pers.netname, ctx->sortedClients[i]->pers.squadStatus);

			ctx->elems[n].kind = UI_TEXT;
			ctx->elems[n].u.text.x = 0;
			ctx->elems[n].u.text.y = 42 + i * 8;
			ctx->elems[n].u.text.text = ctx->row_text[i];
			ctx->elems[n].u.text.highlight = ready;
			n++;
		}
	}

	screen->elems = ctx->elems;
	screen->count = n;

	return 0;
}

void CTFSquadboardMessage (edict_t *ent, edict_t* killer) // ADC
{
	char                storage[UI_LAYOUT_BUDGET];
	ui_buf_t            sb;
	squadboard_ctx_t    ctx;

	int		len, i, j, team, ready;
	edict_t		*cl_ent;

	gclient_t* clients [MAX_CLIENTS];
	int clientCount = 0;
	gclient_t* sortedClients [MAX_CLIENTS];
	int sortedCount = 0;

	int teamOfInterest = 0;

	char* squad = 0;

	int widestName = 0; // in chars

	int		dropped;
	ui_board_variant_t variant_used;

	for (i = 0; i< MAX_CLIENTS; i++)
		clients [i] = sortedClients [i] = 0;

	teamOfInterest = (ent->client->ctf.teamnum == CTF_TEAM_RED) ? 0 : 1;

	for (i = 0; i< game.maxclients; i++)
	{
		cl_ent = g_edicts + 1 + i;
		if (!cl_ent->inuse)
			continue;
		if (game.clients[i].ctf.teamnum == CTF_TEAM_RED)
			team = 0;
		else if (game.clients[i].ctf.teamnum == CTF_TEAM_BLUE)
			team = 1;
		else
			continue; // unknown team?

		if (team == teamOfInterest)
		{
			len = (int)strlen(game.clients[i].pers.netname);
			clients [clientCount++] = &game.clients [i];

			if (len > widestName)
				widestName = len;
		}
	}

	// We want to put the predefined categories first
	// in the list, then any remaining ones.

	for (i = 0, ready = 1; ready; i++) // squad loop
	{
		ready = 0;

		switch (i)
		{
		case 0:	 squad = "Offense"; break;
		case 1:	 squad = "Middle";  break;
		case 2:	 squad = "Defense"; break;
		default: squad = 0;         break;
		}

		for (j = 0; j< game.maxclients; j++) // client loop
		{
			if (clients[j])
			{
				ready = 1;

				if (squad == 0)
					squad = clients[j]->pers.squad;

				if (!Q_stricmp (clients[j]->pers.squad, squad))
				{
					sortedClients [sortedCount++] = clients [j];
					clients [j] = 0;
				}
			}
		}
	}

	for (i = 0; i < sortedCount; i++)
		ctx.sortedClients[i] = sortedClients[i];
	ctx.sortedCount = sortedCount;
	ctx.teamOfInterest = teamOfInterest;
	ctx.widestName = widestName;

	ui_buf_init(&sb, storage, sizeof(storage));
	dropped = ui_layout_compile_ladder(&ctx, CTFSquadboard_Build, &sb, &variant_used);
	Board_LogVariant("CTFSquadboard", variant_used, dropped, "row");

	gi.WriteByte (svc_layout);
	gi.WriteString (storage);
}

/*
==================
Statboard

Draw instead of help message.
Note that it isn't that hard to overflow the 1400 byte message limit!
==================
*/
void Statboard(edict_t* ent)
{
    StatboardMessage(ent, ent->enemy);
    gi.unicast(ent, true);
}

/*
==================
StatboardMessage

==================
*/
/*
==================
stats_pickup_total

Every powerup and rune this player has picked up this level. StatboardMessage
gathered these eight counters inside its sort loop, where each iteration
overwrote the last, and then never read them -- so they could not have been
displayed even if something had tried. Collected here at render time instead.
==================
*/
static int stats_pickup_total(edict_t* cl_ent)
{
    return (int)(stats_get(cl_ent, STATS_RUNE_STRENGTH) +
                 stats_get(cl_ent, STATS_RUNE_HASTE) +
                 stats_get(cl_ent, STATS_RUNE_REGEN) +
                 stats_get(cl_ent, STATS_RUNE_RESIST) +
                 stats_get(cl_ent, STATS_ITEM_QUAD) +
                 stats_get(cl_ent, STATS_ITEM_SHIELD) +
                 stats_get(cl_ent, STATS_ITEM_ARMOR) +
                 stats_get(cl_ent, STATS_ITEM_MEGA));
}

// Row storage + row callback for StatboardMessage's two UI_TABLEs (red
// and blue). Same shape as Railboard_FillRow: one packed column per
// row. FULL is name plus four small integers plus the pickup total,
// byte-identical to before the density ladder existed. CONDENSED
// keeps frags (the board's headline number) and the combined defense
// count -- the two numbers that most directly say what a CTF player
// contributed -- and shortens the name field. MINIMAL keeps name and
// frags alone.
typedef struct
{
    edict_t             *sorted[MAX_CLIENTS];
    int                  count;
    ui_board_variant_t   variant;
} statboard_rows_t;

static void Statboard_FillRow(void *userdata, int row, int num_columns,
    char cells[UI_TABLE_MAX_COLUMNS][UI_CELL_LEN])
{
    const statboard_rows_t *rows = (const statboard_rows_t *)userdata;
    edict_t     *cl_ent = rows->sorted[row];
    gclient_t   *cl = cl_ent->client;
    int         frags;
    int         def;

    frags = (int)stats_get(cl_ent, STATS_FRAGS);
    def   = (int)(stats_get(cl_ent, STATS_DEFENSE_FLAG) + stats_get(cl_ent, STATS_DEFENSE_BASE));

    switch (rows->variant)
    {
    case UI_BOARD_MINIMAL:
        Com_sprintf(cells[0], UI_CELL_LEN, "%-15s %3i", cl->pers.netname, frags);
        break;

    case UI_BOARD_CONDENSED:
        Com_sprintf(cells[0], UI_CELL_LEN, "%-10s %3i %2i", cl->pers.netname, frags, def);
        break;

    default: // UI_BOARD_FULL
        Com_sprintf(cells[0], UI_CELL_LEN, "%-15s %3i %2i %2i %2i %2i",
            cl->pers.netname,
            frags,
            (int)stats_get(cl_ent, STATS_OFFENSE_CARRIER),
            def,
            (int)stats_get(cl_ent, STATS_RETURNS),
            stats_pickup_total(cl_ent));
        break;
    }

    (void)num_columns; // always 1 for this board
}

// Fixed screen parts (header pics, table geometry) plus the row
// storage for both teams -- caller-owned so it stays valid across
// every rung ui_layout_compile_ladder tries.
typedef struct
{
    ui_elem_t          elems[4];   // 2 header pics + red table + blue table
    ui_table_col_t     column;
    statboard_rows_t   red_rows;
    statboard_rows_t   blue_rows;
} statboard_ctx_t;

// Builds Statboard's screen for one rung of the density ladder
// (ui_layout.h). UI_BOARD_FULL reproduces the original's legacy 1024-
// byte admission pass (Board_LineLen, see its banner) exactly, so a
// roster that fit under the old cap renders identically; the rows it
// excludes are returned as this attempt's pre-filter count so the
// ladder can see past what ui_layout_compile alone would report.
// UI_BOARD_CONDENSED and UI_BOARD_MINIMAL are new formats that never
// shipped before the ladder existed, so they carry no legacy-cap
// obligation -- every row is offered and ui_layout_compile's own
// budget (UI_LAYOUT_BUDGET) decides what fits.
static int Statboard_Build(void *userdata, ui_board_variant_t variant, ui_screen_t *screen)
{
    statboard_ctx_t *ctx = (statboard_ctx_t *)userdata;
    int         n;
    int         red_rows_shown;
    int         blue_rows_shown;
    int         legacy_dropped;
    qboolean    header_ok;

    ctx->red_rows.variant = variant;
    ctx->blue_rows.variant = variant;

    if (variant == UI_BOARD_FULL)
    {
        int     legacy_len;
        int     header_len;
        int     line_len;
        int     i;
        char    cells[UI_TABLE_MAX_COLUMNS][UI_CELL_LEN];

        legacy_len = 0;

        // header: the two statboard pics were tested as ONE unit in the
        // original (both tokens built into one Com_sprintf, one
        // stringlength+j<1024 check) -- reproduce that all-or-nothing
        // admission rather than treating each pic independently.
        header_len = Board_LineLen("xv %i yv %i picn %s ", -102, -35, "pb")
                   + Board_LineLen("xv %i yv %i picn %s ", -102, -27, "pt");
        header_ok = (legacy_len + header_len < 1024);
        if (header_ok)
            legacy_len += header_len;

        red_rows_shown = 0;
        for (i = 0; i < ctx->red_rows.count; i++)
        {
            Statboard_FillRow(&ctx->red_rows, i, 1, cells);
            line_len = Board_LineLen("xv %i yv %i string2 \"%s\" ", -91, 34 + 8 * i, cells[0]);
            if (legacy_len + line_len > 1024)
                break;
            legacy_len += line_len;
            red_rows_shown = i + 1;
        }

        blue_rows_shown = 0;
        for (i = 0; i < ctx->blue_rows.count; i++)
        {
            Statboard_FillRow(&ctx->blue_rows, i, 1, cells);
            line_len = Board_LineLen("xv %i yv %i string2 \"%s\" ", 171, 34 + 8 * i, cells[0]);
            if (legacy_len + line_len > 1024)
                break;
            legacy_len += line_len;
            blue_rows_shown = i + 1;
        }

        legacy_dropped = (ctx->red_rows.count - red_rows_shown) + (ctx->blue_rows.count - blue_rows_shown);
    }
    else
    {
        header_ok = true;
        red_rows_shown = ctx->red_rows.count;
        blue_rows_shown = ctx->blue_rows.count;
        legacy_dropped = 0;
    }

    n = 0;

    if (header_ok)
    {
        ctx->elems[n].kind = UI_PIC;
        ctx->elems[n].u.pic.x = -102;
        ctx->elems[n].u.pic.y = -35;
        ctx->elems[n].u.pic.stat_driven = false;
        ctx->elems[n].u.pic.image.name = "pb";
        n++;

        ctx->elems[n].kind = UI_PIC;
        ctx->elems[n].u.pic.x = -102;
        ctx->elems[n].u.pic.y = -27;
        ctx->elems[n].u.pic.stat_driven = false;
        ctx->elems[n].u.pic.image.name = "pt";
        n++;
    }

    ctx->column.x_offset = 0;
    ctx->column.priority = 0;

    ctx->elems[n].kind = UI_TABLE;
    ctx->elems[n].u.table.x = -91;
    ctx->elems[n].u.table.y = 34;
    ctx->elems[n].u.table.row_dy = 8;
    ctx->elems[n].u.table.columns = &ctx->column;
    ctx->elems[n].u.table.num_columns = 1;
    ctx->elems[n].u.table.num_rows = red_rows_shown;
    ctx->elems[n].u.table.fill_row = Statboard_FillRow;
    ctx->elems[n].u.table.userdata = &ctx->red_rows;
    ctx->elems[n].u.table.highlight = true;
    ctx->elems[n].u.table.footer = NULL;
    ctx->elems[n].u.table.footer_x = 0;
    ctx->elems[n].u.table.footer_y = 0;
    ctx->elems[n].u.table.footer_highlight = false;
    n++;

    ctx->elems[n].kind = UI_TABLE;
    ctx->elems[n].u.table.x = 171;
    ctx->elems[n].u.table.y = 34;
    ctx->elems[n].u.table.row_dy = 8;
    ctx->elems[n].u.table.columns = &ctx->column;
    ctx->elems[n].u.table.num_columns = 1;
    ctx->elems[n].u.table.num_rows = blue_rows_shown;
    ctx->elems[n].u.table.fill_row = Statboard_FillRow;
    ctx->elems[n].u.table.userdata = &ctx->blue_rows;
    ctx->elems[n].u.table.highlight = true;
    ctx->elems[n].u.table.footer = NULL;
    ctx->elems[n].u.table.footer_x = 0;
    ctx->elems[n].u.table.footer_y = 0;
    ctx->elems[n].u.table.footer_highlight = false;
    n++;

    screen->elems = ctx->elems;
    screen->count = n;

    return legacy_dropped;
}

void StatboardMessage(edict_t* ent, edict_t* killer)
{
    char                storage[UI_LAYOUT_BUDGET];
    ui_buf_t            sb;
    statboard_ctx_t     ctx;

    int     blue, red;

    int     i;
    int     j;      // sort index; int so the
                    // comparisons against red/blue/k stay signed
    int     k;

    int     redsorted[MAX_CLIENTS];
    int     redsortedscores[MAX_CLIENTS];
    int     bluesorted[MAX_CLIENTS];
    int     bluesortedscores[MAX_CLIENTS];

    int     score;

    edict_t*    cl_ent;

    int     dropped;
    ui_board_variant_t variant_used;

    blue = 0;
    red = 0;

    // sort the clients by score -- unchanged from the hand-written version
    for (i = 0; i < game.maxclients; i++)
    {
        cl_ent = g_edicts + 1 + i;

        if (!cl_ent->inuse)
            continue;

        score = stats_get(cl_ent, STATS_SCORE);

        // per-player pickups are gathered at render time by stats_pickup_total()

        if (cl_ent->client->ctf.teamnum == CTF_TEAM_RED)
        {

            for (j = 0; j < red; j++)
            {
                if (score > redsortedscores[j])
                    break;
            }
            for (k = red; k > j; k--)
            {
                redsorted[k] = redsorted[k - 1];
                redsortedscores[k] = redsortedscores[k - 1];
            }
            redsorted[j] = i;
            redsortedscores[j] = score;
            red++;
        }
        else if (cl_ent->client->ctf.teamnum == CTF_TEAM_BLUE)
        {

            for (j = 0; j < blue; j++)
            {
                if (score > bluesortedscores[j])
                    break;
            }
            for (k = blue; k > j; k--)
            {
                bluesorted[k] = bluesorted[k - 1];
                bluesortedscores[k] = bluesortedscores[k - 1];
            }
            bluesorted[j] = i;
            bluesortedscores[j] = score;
            blue++;
        }

    }

    ctx.red_rows.count = red;
    for (i = 0; i < red; i++)
        ctx.red_rows.sorted[i] = g_edicts + 1 + redsorted[i];

    ctx.blue_rows.count = blue;
    for (i = 0; i < blue; i++)
        ctx.blue_rows.sorted[i] = g_edicts + 1 + bluesorted[i];

    ui_buf_init(&sb, storage, sizeof(storage));
    dropped = ui_layout_compile_ladder(&ctx, Statboard_Build, &sb, &variant_used);
    Board_LogVariant("Statboard", variant_used, dropped, "row");

    gi.WriteByte(svc_layout);
    gi.WriteString(storage);

}

/*
==================
TeamStatboard

Draw instead of help message.
Note that it isn't that hard to overflow the 1400 byte message limit!
==================
*/
void TeamStatboard(edict_t* ent)
{
    TeamStatboardMessage(ent, ent->enemy);
    gi.unicast(ent, true);
}

// Fixed screen parts plus the team-wide sums every rung of the density
// ladder (ui_layout.h) draws from -- there is no per-player roster on
// this board, only team totals, so unlike the other four boards
// density here is about how many of the sixteen numbers are shown,
// not how many rows fit.
typedef struct
{
    ui_elem_t   elems[18];         // 2 header pics + up to 16 numeric fields
    char        numtext[16][16];       // UI_BOARD_FULL: one text per number
    char        condensed_text[4][16]; // UI_BOARD_CONDENSED: itm/rne total per team
    char        minimal_text[2][16];   // UI_BOARD_MINIMAL: one combined total per team

    int     red_item_quad, red_item_shield, red_item_armor, red_item_mega;
    int     red_rune_strength, red_rune_haste, red_rune_regen, red_rune_resist;
    int     blue_item_quad, blue_item_shield, blue_item_armor, blue_item_mega;
    int     blue_rune_strength, blue_rune_haste, blue_rune_regen, blue_rune_resist;
} teamstatboard_ctx_t;

// Builds TeamStatboard's screen for one rung of the density ladder.
// UI_BOARD_FULL reproduces the original's all-or-nothing 1024-byte
// admission (Board_LineLen, see its banner) around the whole sixteen-
// number block, byte-identical to before the ladder existed -- when it
// doesn't fit, NOTHING is shown (screen->count stays 0, matching the
// original), and this returns 18 as the pre-filter count so the ladder
// knows to try a denser rung instead of serving a blank board.
// UI_BOARD_CONDENSED merges each team's four item counts into one
// "itm" total and its four rune counts into one "rne" total.
// UI_BOARD_MINIMAL keeps a single combined pickup total per team --
// the board's headline number, standing in for a name since there is
// no per-player row here.
static int TeamStatboard_Build(void *userdata, ui_board_variant_t variant, ui_screen_t *screen)
{
    teamstatboard_ctx_t *ctx = (teamstatboard_ctx_t *)userdata;
    static const struct { int x, y; } pic_pos[2] = { { -102, -35 }, { -102, -27 } };
    static const char * const pic_name[2]        = { "tb", "tt" };
    int         n;
    int         i;

    n = 0;

    if (variant == UI_BOARD_FULL)
    {
        static const struct { int x, y; } num_pos[16] =
        {
            { -54, -19 }, { -54, -10 }, { -54, -1 }, { -54, 8 },
            { 209, -19 }, { 209, -10 }, { 209, -1 }, { 209, 8 },
            { 10,  -19 }, { 10,  -10 }, { 10,  -1 }, { 10,  8 },
            { 273, -19 }, { 273, -10 }, { 273, -1 }, { 273, 8 },
        };
        int         block_len;
        qboolean    block_ok;

        Com_sprintf(ctx->numtext[0],  sizeof(ctx->numtext[0]),  "%i", ctx->red_item_quad);
        Com_sprintf(ctx->numtext[1],  sizeof(ctx->numtext[1]),  "%i", ctx->red_item_shield);
        Com_sprintf(ctx->numtext[2],  sizeof(ctx->numtext[2]),  "%i", ctx->red_item_armor);
        Com_sprintf(ctx->numtext[3],  sizeof(ctx->numtext[3]),  "%i", ctx->red_item_mega);
        Com_sprintf(ctx->numtext[4],  sizeof(ctx->numtext[4]),  "%i", ctx->blue_item_quad);
        Com_sprintf(ctx->numtext[5],  sizeof(ctx->numtext[5]),  "%i", ctx->blue_item_shield);
        Com_sprintf(ctx->numtext[6],  sizeof(ctx->numtext[6]),  "%i", ctx->blue_item_armor);
        Com_sprintf(ctx->numtext[7],  sizeof(ctx->numtext[7]),  "%i", ctx->blue_item_mega);
        Com_sprintf(ctx->numtext[8],  sizeof(ctx->numtext[8]),  "%i", ctx->red_rune_strength);
        Com_sprintf(ctx->numtext[9],  sizeof(ctx->numtext[9]),  "%i", ctx->red_rune_haste);
        Com_sprintf(ctx->numtext[10], sizeof(ctx->numtext[10]), "%i", ctx->red_rune_resist);
        Com_sprintf(ctx->numtext[11], sizeof(ctx->numtext[11]), "%i", ctx->red_rune_regen);
        Com_sprintf(ctx->numtext[12], sizeof(ctx->numtext[12]), "%i", ctx->blue_rune_strength);
        Com_sprintf(ctx->numtext[13], sizeof(ctx->numtext[13]), "%i", ctx->blue_rune_haste);
        Com_sprintf(ctx->numtext[14], sizeof(ctx->numtext[14]), "%i", ctx->blue_rune_resist);
        Com_sprintf(ctx->numtext[15], sizeof(ctx->numtext[15]), "%i", ctx->blue_rune_regen);

        block_len = Board_LineLen("xv %i yv %i picn %s ", -102, -35, "tb")
                  + Board_LineLen("xv %i yv %i picn %s ", -102, -27, "tt")
                  + Board_LineLen("xv %i yv %i string2 \"%s\" ", -54, -19, ctx->numtext[0])
                  + Board_LineLen("xv %i yv %i string2 \"%s\" ", -54, -10, ctx->numtext[1])
                  + Board_LineLen("xv %i yv %i string2 \"%s\" ", -54, -1,  ctx->numtext[2])
                  + Board_LineLen("xv %i yv %i string2 \"%s\" ", -54, 8,   ctx->numtext[3])
                  + Board_LineLen("xv %i yv %i string2 \"%s\" ", 209, -19, ctx->numtext[4])
                  + Board_LineLen("xv %i yv %i string2 \"%s\" ", 209, -10, ctx->numtext[5])
                  + Board_LineLen("xv %i yv %i string2 \"%s\" ", 209, -1,  ctx->numtext[6])
                  + Board_LineLen("xv %i yv %i string2 \"%s\" ", 209, 8,   ctx->numtext[7])
                  + Board_LineLen("xv %i yv %i string2 \"%s\" ", 10,  -19, ctx->numtext[8])
                  + Board_LineLen("xv %i yv %i string2 \"%s\" ", 10,  -10, ctx->numtext[9])
                  + Board_LineLen("xv %i yv %i string2 \"%s\" ", 10,  -1,  ctx->numtext[10])
                  + Board_LineLen("xv %i yv %i string2 \"%s\" ", 10,  8,   ctx->numtext[11])
                  + Board_LineLen("xv %i yv %i string2 \"%s\" ", 273, -19, ctx->numtext[12])
                  + Board_LineLen("xv %i yv %i string2 \"%s\" ", 273, -10, ctx->numtext[13])
                  + Board_LineLen("xv %i yv %i string2 \"%s\" ", 273, -1,  ctx->numtext[14])
                  + Board_LineLen("xv %i yv %i string2 \"%s\" ", 273, 8,   ctx->numtext[15]);
        block_ok = (block_len < 1024);

        if (block_ok)
        {
            for (i = 0; i < 2; i++)
            {
                ctx->elems[n].kind = UI_PIC;
                ctx->elems[n].u.pic.x = pic_pos[i].x;
                ctx->elems[n].u.pic.y = pic_pos[i].y;
                ctx->elems[n].u.pic.stat_driven = false;
                ctx->elems[n].u.pic.image.name = pic_name[i];
                n++;
            }

            for (i = 0; i < 16; i++)
            {
                ctx->elems[n].kind = UI_TEXT;
                ctx->elems[n].u.text.x = num_pos[i].x;
                ctx->elems[n].u.text.y = num_pos[i].y;
                ctx->elems[n].u.text.text = ctx->numtext[i];
                ctx->elems[n].u.text.highlight = true;
                n++;
            }
        }

        screen->elems = ctx->elems;
        screen->count = n;

        return block_ok ? 0 : 18;
    }

    if (variant == UI_BOARD_CONDENSED)
    {
        static const struct { int x, y; } num_pos[4] =
        {
            { -54, -10 }, { 10, -10 }, { 209, -10 }, { 273, -10 },
        };
        int red_items, red_runes, blue_items, blue_runes;

        red_items  = ctx->red_item_quad + ctx->red_item_shield + ctx->red_item_armor + ctx->red_item_mega;
        red_runes  = ctx->red_rune_strength + ctx->red_rune_haste + ctx->red_rune_regen + ctx->red_rune_resist;
        blue_items = ctx->blue_item_quad + ctx->blue_item_shield + ctx->blue_item_armor + ctx->blue_item_mega;
        blue_runes = ctx->blue_rune_strength + ctx->blue_rune_haste + ctx->blue_rune_regen + ctx->blue_rune_resist;

        Com_sprintf(ctx->condensed_text[0], sizeof(ctx->condensed_text[0]), "itm %i", red_items);
        Com_sprintf(ctx->condensed_text[1], sizeof(ctx->condensed_text[1]), "rne %i", red_runes);
        Com_sprintf(ctx->condensed_text[2], sizeof(ctx->condensed_text[2]), "itm %i", blue_items);
        Com_sprintf(ctx->condensed_text[3], sizeof(ctx->condensed_text[3]), "rne %i", blue_runes);

        for (i = 0; i < 2; i++)
        {
            ctx->elems[n].kind = UI_PIC;
            ctx->elems[n].u.pic.x = pic_pos[i].x;
            ctx->elems[n].u.pic.y = pic_pos[i].y;
            ctx->elems[n].u.pic.stat_driven = false;
            ctx->elems[n].u.pic.image.name = pic_name[i];
            n++;
        }

        for (i = 0; i < 4; i++)
        {
            ctx->elems[n].kind = UI_TEXT;
            ctx->elems[n].u.text.x = num_pos[i].x;
            ctx->elems[n].u.text.y = num_pos[i].y;
            ctx->elems[n].u.text.text = ctx->condensed_text[i];
            ctx->elems[n].u.text.highlight = true;
            n++;
        }

        screen->elems = ctx->elems;
        screen->count = n;

        return 0;
    }

    // UI_BOARD_MINIMAL: one combined pickup total per team, standing in
    // for this board's missing per-player name.
    {
        int red_total, blue_total;

        red_total  = ctx->red_item_quad + ctx->red_item_shield + ctx->red_item_armor + ctx->red_item_mega
                   + ctx->red_rune_strength + ctx->red_rune_haste + ctx->red_rune_regen + ctx->red_rune_resist;
        blue_total = ctx->blue_item_quad + ctx->blue_item_shield + ctx->blue_item_armor + ctx->blue_item_mega
                   + ctx->blue_rune_strength + ctx->blue_rune_haste + ctx->blue_rune_regen + ctx->blue_rune_resist;

        Com_sprintf(ctx->minimal_text[0], sizeof(ctx->minimal_text[0]), "%i", red_total);
        Com_sprintf(ctx->minimal_text[1], sizeof(ctx->minimal_text[1]), "%i", blue_total);

        for (i = 0; i < 2; i++)
        {
            ctx->elems[n].kind = UI_PIC;
            ctx->elems[n].u.pic.x = pic_pos[i].x;
            ctx->elems[n].u.pic.y = pic_pos[i].y;
            ctx->elems[n].u.pic.stat_driven = false;
            ctx->elems[n].u.pic.image.name = pic_name[i];
            n++;
        }

        ctx->elems[n].kind = UI_TEXT;
        ctx->elems[n].u.text.x = -54;
        ctx->elems[n].u.text.y = -10;
        ctx->elems[n].u.text.text = ctx->minimal_text[0];
        ctx->elems[n].u.text.highlight = true;
        n++;

        ctx->elems[n].kind = UI_TEXT;
        ctx->elems[n].u.text.x = 209;
        ctx->elems[n].u.text.y = -10;
        ctx->elems[n].u.text.text = ctx->minimal_text[1];
        ctx->elems[n].u.text.highlight = true;
        n++;

        screen->elems = ctx->elems;
        screen->count = n;

        return 0;
    }
}

/*
==================
TeamStatboardMessage

==================
*/
void TeamStatboardMessage(edict_t* ent, edict_t* killer)
{
    char                storage[UI_LAYOUT_BUDGET];
    ui_buf_t            sb;
    teamstatboard_ctx_t ctx;

    int     blue, red;

    int     blue_rune_strength = 0;
    int     blue_rune_haste = 0;
    int     blue_rune_regen = 0;
    int     blue_rune_resist = 0;
    int     blue_item_mega = 0;
    int     blue_item_quad = 0;
    int     blue_item_armor = 0;
    int     blue_item_shield = 0;
    int     red_rune_strength = 0;
    int     red_rune_haste = 0;
    int     red_rune_regen = 0;
    int     red_rune_resist = 0;
    int     red_item_mega = 0;
    int     red_item_quad = 0;
    int     red_item_armor = 0;
    int     red_item_shield = 0;

    int     i;
    int     j;      // sort index; int so the
                    // comparisons against red/blue/k stay signed
    int     k;

    int     redsorted[MAX_CLIENTS];
    int     redsortedscores[MAX_CLIENTS];
    int     bluesorted[MAX_CLIENTS];
    int     bluesortedscores[MAX_CLIENTS];

    int     score;

    int     rune_strength;
    int     rune_haste;
    int     rune_regen;
    int     rune_resist;
    int     item_mega;
    int     item_armor;
    int     item_shield;
    int     item_quad;

    int     dropped;
    ui_board_variant_t variant_used;

    edict_t* cl_ent;

    blue = 0;
    red = 0;

    // sort the clients by score
    for (i = 0; i < game.maxclients; i++)
    {
        cl_ent = g_edicts + 1 + i;

        if (!cl_ent->inuse)
            continue;

        score = stats_get(cl_ent, STATS_SCORE);

        rune_strength = stats_get(cl_ent, STATS_RUNE_STRENGTH);
        rune_haste = stats_get(cl_ent, STATS_RUNE_HASTE);
        rune_regen = stats_get(cl_ent, STATS_RUNE_REGEN);
        rune_resist = stats_get(cl_ent, STATS_RUNE_RESIST);
        item_quad = stats_get(cl_ent, STATS_ITEM_QUAD);
        item_shield = stats_get(cl_ent, STATS_ITEM_SHIELD);
        item_armor = stats_get(cl_ent, STATS_ITEM_ARMOR);
        item_mega = stats_get(cl_ent, STATS_ITEM_MEGA);

        if (cl_ent->client->ctf.teamnum == CTF_TEAM_RED)
        {
            red_rune_strength += rune_strength;
            red_rune_haste += rune_haste;
            red_rune_regen += rune_regen;
            red_rune_resist += rune_resist;
            red_item_quad += item_quad;
            red_item_shield += item_shield;
            red_item_armor += item_armor;
            red_item_mega += item_mega;

            for (j = 0; j < red; j++)
            {
                if (score > redsortedscores[j])
                    break;
            }
            for (k = red; k > j; k--)
            {
                redsorted[k] = redsorted[k - 1];
                redsortedscores[k] = redsortedscores[k - 1];
            }
            redsorted[j] = i;
            redsortedscores[j] = score;
            red++;
        }
        else if (cl_ent->client->ctf.teamnum == CTF_TEAM_BLUE)
        {
            blue_rune_strength += rune_strength;
            blue_rune_haste += rune_haste;
            blue_rune_regen += rune_regen;
            blue_rune_resist += rune_resist;
            blue_item_quad += item_quad;
            blue_item_shield += item_shield;
            blue_item_armor += item_armor;
            blue_item_mega += item_mega;

            for (j = 0; j < blue; j++)
            {
                if (score > bluesortedscores[j])
                    break;
            }
            for (k = blue; k > j; k--)
            {
                bluesorted[k] = bluesorted[k - 1];
                bluesortedscores[k] = bluesortedscores[k - 1];
            }
            bluesorted[j] = i;
            bluesortedscores[j] = score;
            blue++;
        }

    }

    // DRAW TEAMSTATBOARD AND ADD TEAM STATS
    //
    // TeamStatboard_Build (above) does the format work for every rung
    // of the density ladder; this hands it the team-wide sums just
    // gathered.
    ctx.red_item_quad     = red_item_quad;
    ctx.red_item_shield   = red_item_shield;
    ctx.red_item_armor    = red_item_armor;
    ctx.red_item_mega     = red_item_mega;
    ctx.red_rune_strength = red_rune_strength;
    ctx.red_rune_haste    = red_rune_haste;
    ctx.red_rune_regen    = red_rune_regen;
    ctx.red_rune_resist   = red_rune_resist;
    ctx.blue_item_quad     = blue_item_quad;
    ctx.blue_item_shield   = blue_item_shield;
    ctx.blue_item_armor    = blue_item_armor;
    ctx.blue_item_mega     = blue_item_mega;
    ctx.blue_rune_strength = blue_rune_strength;
    ctx.blue_rune_haste    = blue_rune_haste;
    ctx.blue_rune_regen    = blue_rune_regen;
    ctx.blue_rune_resist   = blue_rune_resist;

    ui_buf_init(&sb, storage, sizeof(storage));
    dropped = ui_layout_compile_ladder(&ctx, TeamStatboard_Build, &sb, &variant_used);
    Board_LogVariant("TeamStatboard", variant_used, dropped, "element");

    gi.WriteByte(svc_layout);
    gi.WriteString(storage);

}

/*
==================
Railboard

Draw instead of help message.
Note that it isn't that hard to overflow the 1400 byte message limit!
==================
*/
void Railboard(edict_t* ent)
{
    RailboardMessage(ent, ent->enemy);
    gi.unicast(ent, true);
}

// Row storage + row callback for Railboard's UI_TABLE (ui_layout.h).
// The original board never positioned its four numbers as separate
// xv-columns -- it packed name+kills+hits+shots+pct into one printf-
// padded string and drew that as a single string2 token per row. That
// is a one-column table (the row callback fills the whole packed
// line into cells[0]); UI_TABLE's multi-column x-offset/priority
// machinery exists for boards that need real per-column positioning,
// which this one does not.
typedef struct
{
    edict_t             *sorted[MAX_CLIENTS];
    int                  count;
    ui_board_variant_t   variant;
} railboard_rows_t;

// Row text shrinks with variant (ui_layout.h: density ladder), the
// table geometry (position, row height, highlight) does not -- only
// Railboard_FillRow's format string changes. FULL is untouched from
// before the ladder existed. CONDENSED keeps kills (the board's own
// headline number) and accuracy, the two numbers a rail duel is
// actually judged on, and shortens the name field to make room.
// MINIMAL keeps name and kills alone.
static void Railboard_FillRow(void *userdata, int row, int num_columns,
    char cells[UI_TABLE_MAX_COLUMNS][UI_CELL_LEN])
{
    const railboard_rows_t *rows = (const railboard_rows_t *)userdata;
    edict_t     *cl_ent = rows->sorted[row];
    gclient_t   *cl = cl_ent->client;
    long        shot, hit, kill;
    int         pct;

    shot = stats_get(cl_ent, STATS_RAIL_SHOT);
    hit  = stats_get(cl_ent, STATS_RAIL_HIT);
    kill = stats_get(cl_ent, STATS_RAIL_KILL);
    pct  = shot == 0 ? 0 : (int)(100 * hit / shot);

    switch (rows->variant)
    {
    case UI_BOARD_MINIMAL:
        Com_sprintf(cells[0], UI_CELL_LEN, "%-15s %2i", cl->pers.netname, (int)kill);
        break;

    case UI_BOARD_CONDENSED:
        Com_sprintf(cells[0], UI_CELL_LEN, "%-10s %2i %3i%%", cl->pers.netname, (int)kill, pct);
        break;

    default: // UI_BOARD_FULL -- went straight through p_stats_player,
             // which is NULL for a client that has not finished
             // connecting. stats_get guards it.
        Com_sprintf(cells[0], UI_CELL_LEN, "%-15s %2i %2i %3i %3i",
            cl->pers.netname,
            (int)kill,
            (int)hit,
            (int)shot,
            pct);
        break;
    }

    (void)num_columns; // always 1 for this board
}

// Fixed parts of Railboard's screen (header pics, table geometry) plus
// the row storage Railboard_FillRow reads -- all owned by the caller's
// stack frame (RailboardMessage) so it stays valid across every rung
// ui_layout_compile_ladder tries, not just the first.
typedef struct
{
    ui_elem_t          elems[3];
    ui_table_col_t     column;
    railboard_rows_t   rows;
} railboard_ctx_t;

// Builds Railboard's screen for one rung of the density ladder
// (ui_layout.h). Only the row text (Railboard_FillRow) changes between
// rungs -- the header pics and table position are the same at every
// density. Railboard has no legacy admission pass ahead of the
// compiler (it never had one before the ladder either), so every row
// it builds here goes to ui_layout_compile and the pre-filter count
// returned is always 0.
static int Railboard_Build(void *userdata, ui_board_variant_t variant, ui_screen_t *screen)
{
    railboard_ctx_t *ctx = (railboard_ctx_t *)userdata;

    ctx->rows.variant = variant;

    ctx->elems[0].kind = UI_PIC;
    ctx->elems[0].u.pic.x = 29;
    ctx->elems[0].u.pic.y = -35;
    ctx->elems[0].u.pic.stat_driven = false;
    ctx->elems[0].u.pic.image.name = "rb";

    ctx->elems[1].kind = UI_PIC;
    ctx->elems[1].u.pic.x = 41;
    ctx->elems[1].u.pic.y = -26;
    ctx->elems[1].u.pic.stat_driven = false;
    ctx->elems[1].u.pic.image.name = "rt";

    // one highlighted row per sorted player, one packed column each --
    // reproduces the original's single string2 token per row.
    ctx->column.x_offset = 0;
    ctx->column.priority = 0;

    ctx->elems[2].kind = UI_TABLE;
    ctx->elems[2].u.table.x = 40;
    ctx->elems[2].u.table.y = -18;
    ctx->elems[2].u.table.row_dy = 8;
    ctx->elems[2].u.table.columns = &ctx->column;
    ctx->elems[2].u.table.num_columns = 1;
    ctx->elems[2].u.table.num_rows = ctx->rows.count;
    ctx->elems[2].u.table.fill_row = Railboard_FillRow;
    ctx->elems[2].u.table.userdata = &ctx->rows;
    ctx->elems[2].u.table.highlight = true;
    ctx->elems[2].u.table.footer = NULL;
    ctx->elems[2].u.table.footer_x = 0;
    ctx->elems[2].u.table.footer_y = 0;
    ctx->elems[2].u.table.footer_highlight = false;

    screen->elems = ctx->elems;
    screen->count = 3;

    return 0;
}

/*
==================
RailboardMessage

==================
*/
void RailboardMessage(edict_t* ent, edict_t* killer)
{
    char                storage[UI_LAYOUT_BUDGET];
    ui_buf_t            sb;
    railboard_ctx_t     ctx;

    int     i;
    int     j;      // sort index; int so the
                    // comparisons against k stay signed
    int     k;
    int     player = 0;
    int     rails = 0;
    int     playersorted[MAX_CLIENTS];
    int     playersortedrails[MAX_CLIENTS];
    int     dropped;
    ui_board_variant_t variant_used;

    edict_t* cl_ent;

    // sort the clients by rail kills -- unchanged from the hand-written
    // version: same insertion sort, same CTF-team-only filter.
    for (i = 0; i < game.maxclients; i++)
    {
        cl_ent = g_edicts + 1 + i;

        if (!cl_ent->inuse)
            continue;

        rails = stats_get(cl_ent, STATS_RAIL_KILL);

        if (cl_ent->client->ctf.teamnum == CTF_TEAM_BLUE || cl_ent->client->ctf.teamnum == CTF_TEAM_RED)
        {
            for (j = 0; j < player; j++)
            {
                if (rails > playersortedrails[j])
                    break;
            }
            for (k = player; k > j; k--)
            {
                playersorted[k] = playersorted[k - 1];
                playersortedrails[k] = playersortedrails[k - 1];
            }
            playersorted[j] = i;
            playersortedrails[j] = rails;
            player++;
        }

    }

    ctx.rows.count = player;
    for (i = 0; i < player; i++)
        ctx.rows.sorted[i] = g_edicts + 1 + playersorted[i];

    ui_buf_init(&sb, storage, sizeof(storage));
    dropped = ui_layout_compile_ladder(&ctx, Railboard_Build, &sb, &variant_used);
    Board_LogVariant("Railboard", variant_used, dropped, "row");

    gi.WriteByte(svc_layout);
    gi.WriteString(storage);

}

/*
==================
Cmd_Score_f

Display the scoreboard
==================
*/
void Cmd_Score_f (edict_t *ent)
{
	ent->client->showinventory = false;
	ent->client->showhelp = false;
    ent->client->showctfhud = false;
    ent->client->showmod = false;
    ent->client->showmenu = false;
	ent->client->showsquadboard = false; // ADC
    ent->client->showstatboard = false; // BUZZKILL
    ent->client->showteamstatboard = false; // BUZZKILL
    ent->client->showrailboard = false; // BUZZKILL
    ent->client->showseason = false;
    ent->client->showrecords = false;
    ent->client->showactivity = false;
    ent->client->showmomentum = false;

	if (!deathmatch->value && !coop->value)
		return;

	if (ent->client->showscores)
	{
		ent->client->showscores = false;
		return;
	}

	ent->client->showscores = true;
	DeathmatchScoreboard (ent);
}

// ADC
/*
==================
Cmd_Squadboard_f

Display the squadboard
==================
*/
void Cmd_Squadboard_f (edict_t *ent)
{
	ent->client->showhelp = false;
	ent->client->showinventory = false;
	ent->client->showctfhud = false;
	ent->client->showmod = false;
	ent->client->showmenu = false;
	ent->client->showscores = false;

	if (!deathmatch->value && !coop->value)
		return;

	if (ent->client->showsquadboard)
	{
		ent->client->showsquadboard = false;
		return;
	}

	ent->client->showsquadboard = true;
	Squadboard (ent);
}
// ADC

// BUZZKILL
/*
==================
Cmd_Statboard_f

Display the statboard
==================
*/
void Cmd_Statboard_f(edict_t* ent)
{
    ent->client->showhelp = false;
    ent->client->showinventory = false;
    ent->client->showctfhud = false;
    ent->client->showmod = false;
    ent->client->showmenu = false;
    ent->client->showscores = false;
    ent->client->showsquadboard = false;
    ent->client->showteamstatboard = false;
    ent->client->showrailboard = false;
    ent->client->showseason = false;
    ent->client->showrecords = false;
    ent->client->showactivity = false;
    ent->client->showmomentum = false;

    if (!deathmatch->value && !coop->value)
        return;

    if (ent->client->showstatboard)
    {
        ent->client->showstatboard = false;
        return;
    }

    ent->client->showstatboard = true;
    Statboard(ent);
}

/*
==================
Cmd_TeamStatboard_f

Display the teamstatboard
==================
*/
void Cmd_TeamStatboard_f(edict_t* ent)
{
    ent->client->showinventory = false;
    ent->client->showhelp = false;
    ent->client->showctfhud = false;
    ent->client->showmod = false;
    ent->client->showmenu = false;
    ent->client->showsquadboard = false; // ADC
    ent->client->showstatboard = false; // BUZZKILL
    ent->client->showscores = false; // BUZZKILL
    ent->client->showrailboard = false; // BUZZKILL
    ent->client->showseason = false;
    ent->client->showrecords = false;
    ent->client->showactivity = false;
    ent->client->showmomentum = false;

    if (!deathmatch->value && !coop->value)
        return;

    if (ent->client->showteamstatboard)
    {
        ent->client->showteamstatboard = false;
        return;
    }

    ent->client->showteamstatboard = true;
    TeamStatboard(ent);
}

/*
==================
Cmd_Railboard_f

Display the railboard
==================
*/
void Cmd_Railboard_f(edict_t * ent)
{
    ent->client->showhelp = false;
    ent->client->showinventory = false;
    ent->client->showctfhud = false;
    ent->client->showmod = false;
    ent->client->showmenu = false;
    ent->client->showscores = false;
    ent->client->showsquadboard = false;
    ent->client->showstatboard = false;
    ent->client->showteamstatboard = false;
    ent->client->showseason = false;
    ent->client->showrecords = false;
    ent->client->showactivity = false;
    ent->client->showmomentum = false;

    if (!deathmatch->value && !coop->value)
        return;

    if (ent->client->showrailboard)
    {
        ent->client->showrailboard = false;
        return;
    }

    ent->client->showrailboard = true;
    Railboard(ent);
}

/*
==================
Cmd_Season_f

Display the Season Top 10 board (settled tier -- ui_boards.c, rebuilt once
at the last match's end, served instantly here from the cache).
==================
*/
void Cmd_Season_f(edict_t *ent)
{
    ent->client->showhelp = false;
    ent->client->showinventory = false;
    ent->client->showctfhud = false;
    ent->client->showmod = false;
    ent->client->showmenu = false;
    ent->client->showscores = false;
    ent->client->showsquadboard = false;
    ent->client->showstatboard = false;
    ent->client->showteamstatboard = false;
    ent->client->showrailboard = false;
    ent->client->showrecords = false;
    ent->client->showactivity = false;
    ent->client->showmomentum = false;

    if (!deathmatch->value && !coop->value)
        return;

    if (ent->client->showseason)
    {
        ent->client->showseason = false;
        return;
    }

    ent->client->showseason = true;
    UI_Boards_Serve(ent, UI_BOARD_SEASON_TOP);
}

/*
==================
Cmd_Records_f

Display the Server Records board (settled tier -- ui_boards.c).
==================
*/
void Cmd_Records_f(edict_t *ent)
{
    ent->client->showhelp = false;
    ent->client->showinventory = false;
    ent->client->showctfhud = false;
    ent->client->showmod = false;
    ent->client->showmenu = false;
    ent->client->showscores = false;
    ent->client->showsquadboard = false;
    ent->client->showstatboard = false;
    ent->client->showteamstatboard = false;
    ent->client->showrailboard = false;
    ent->client->showseason = false;
    ent->client->showactivity = false;
    ent->client->showmomentum = false;

    if (!deathmatch->value && !coop->value)
        return;

    if (ent->client->showrecords)
    {
        ent->client->showrecords = false;
        return;
    }

    ent->client->showrecords = true;
    UI_Boards_Serve(ent, UI_BOARD_SERVER_RECORDS);
}

/*
==================
Cmd_Activity_f

Display the Activity board (settled tier -- ui_boards.c): busiest players
over the last 7 days by games played and time played.
==================
*/
void Cmd_Activity_f(edict_t *ent)
{
    ent->client->showhelp = false;
    ent->client->showinventory = false;
    ent->client->showctfhud = false;
    ent->client->showmod = false;
    ent->client->showmenu = false;
    ent->client->showscores = false;
    ent->client->showsquadboard = false;
    ent->client->showstatboard = false;
    ent->client->showteamstatboard = false;
    ent->client->showrailboard = false;
    ent->client->showseason = false;
    ent->client->showrecords = false;
    ent->client->showmomentum = false;

    if (!deathmatch->value && !coop->value)
        return;

    if (ent->client->showactivity)
    {
        ent->client->showactivity = false;
        return;
    }

    ent->client->showactivity = true;
    UI_Boards_Serve(ent, UI_BOARD_ACTIVITY);
}

/*
==================
Cmd_Momentum_f

Display the Momentum board (settled tier -- ui_boards.c): the biggest
7-day movers, captures this week against the 23 days before.
==================
*/
void Cmd_Momentum_f(edict_t *ent)
{
    ent->client->showhelp = false;
    ent->client->showinventory = false;
    ent->client->showctfhud = false;
    ent->client->showmod = false;
    ent->client->showmenu = false;
    ent->client->showscores = false;
    ent->client->showsquadboard = false;
    ent->client->showstatboard = false;
    ent->client->showteamstatboard = false;
    ent->client->showrailboard = false;
    ent->client->showseason = false;
    ent->client->showrecords = false;
    ent->client->showactivity = false;

    if (!deathmatch->value && !coop->value)
        return;

    if (ent->client->showmomentum)
    {
        ent->client->showmomentum = false;
        return;
    }

    ent->client->showmomentum = true;
    UI_Boards_Serve(ent, UI_BOARD_MOMENTUM);
}

// BUZZKILL

/*
==================
HelpComputer

Draw help computer.
==================
*/
void HelpComputer (edict_t *ent)
{
	char	string[1024];
	char	*sk;

	if (skill->value == 0)
		sk = "easy";
	else if (skill->value == 1)
		sk = "medium";
	else if (skill->value == 2)
		sk = "hard";
	else
		sk = "hard+";

	// send the layout
	Com_sprintf (string, sizeof(string),
		"xv 32 yv 8 picn help "			// background
		"xv 202 yv 12 string2 \"%s\" "		// skill
		"xv 0 yv 24 cstring2 \"%s\" "		// level name
		"xv 0 yv 54 cstring2 \"%s\" "		// help 1
		"xv 0 yv 110 cstring2 \"%s\" "		// help 2
		"xv 50 yv 164 string2 \" kills     goals    secrets\" "
		"xv 50 yv 172 string2 \"%3i/%3i     %i/%i       %i/%i\" ", 
		sk,
		level.level_name,
		game.helpmessage1,
		game.helpmessage2,
		level.killed_monsters, level.total_monsters, 
		level.found_goals, level.total_goals,
		level.found_secrets, level.total_secrets);

	gi.WriteByte (svc_layout);
	gi.WriteString (string);
	gi.unicast (ent, true);
}


/*
==================
Cmd_Help_f

Display the current help message
==================
*/
void Cmd_Help_f (edict_t *ent)
{
	// this is for backwards compatability
	if (deathmatch->value)
	{
		Cmd_Score_f (ent);
		return;
	}

	ent->client->showinventory = false;
	ent->client->showscores = false;
    ent->client->showctfhud = false;
    ent->client->showmod = false;
    ent->client->showmenu = false;
	ent->client->showsquadboard = false; // ADC
    ent->client->showstatboard = false; // BUZZKILL
    ent->client->showteamstatboard = false; // BUZZKILL
    ent->client->showrailboard = false; // BUZZKILL
    ent->client->showseason = false;
    ent->client->showrecords = false;
    ent->client->showactivity = false;
    ent->client->showmomentum = false;

	if (ent->client->showhelp && (ent->client->pers.game_helpchanged == game.helpchanged))
	{
		ent->client->showhelp = false;
		return;
	}

	ent->client->showhelp = true;
	ent->client->pers.helpchanged = 0;
	HelpComputer (ent);
}


//=======================================================================

/*
===============
G_SetStats
===============
*/
void G_SetStats (edict_t *ent)
{
    gitem_t     *item;
    int         index, cells=0;
    int         power_armor_type;

    // TEAM PLAY -- LM_JORM
    char        *s;
    char        portrait[MAX_INFO_STRING];

int Red_Caps = 0;
int Blue_Caps = 0;
int i;
edict_t     *cl_ent;

#ifdef OLDOBSERVERCODE   
    if (ent->client->camera_target &&
        ent->client->camera_target->client)
    {
        memcpy(&ent->client->ps.stats, &ent->client->camera_target->client->ps.stats, sizeof(short)*MAX_STATS);
        ent->client->ps.stats[STAT_FRAGS] = 0;
    }
    else
#endif
    {
        ent->client->ps.stats[STAT_RED_FRAGS] = redscore;
        ent->client->ps.stats[STAT_BLUE_FRAGS] = bluescore;
		
		//bat - This should be global. :/
		for(i = 0 ; i < game.maxclients; i++)
		{
	        cl_ent = g_edicts + 1 + i;
			if(!cl_ent->inuse)
			    continue;

			if(cl_ent->client->ctf.teamnum == CTF_TEAM_RED)
				Red_Caps  += stats_get(cl_ent, STATS_CAPTURES);
	        else if(cl_ent->client->ctf.teamnum == CTF_TEAM_BLUE)
				Blue_Caps  += stats_get(cl_ent, STATS_CAPTURES);
		}
        
		ent->client->ps.stats[STAT_RED_CAPS] = Red_Caps;
		ent->client->ps.stats[STAT_BLUE_CAPS] = Blue_Caps;
		ent->client->ps.stats[STAT_MATCH_TIME] = Time_Left;
        


        //s = Info_ValueForKey (ent->client->pers.userinfo, "skin");
        
        // decide gender
        
        /*
        if (s[0] == 'f' || s[0] == 'F') // Female
        {
        if (ent->client->teamnum == 1)
        ent->client->ps.stats[STAT_TEAM_ICON] = gi.imageindex ("../players/female/red_i");
        
         if (ent->client->teamnum == 2)
         ent->client->ps.stats[STAT_TEAM_ICON] = gi.imageindex ("../players/female/blue_i");
         }
         else // male
         {
         if (ent->client->teamnum == 1)
         ent->client->ps.stats[STAT_TEAM_ICON] = gi.imageindex ("../players/male/red_i");
         
          if (ent->client->teamnum == 2)
          ent->client->ps.stats[STAT_TEAM_ICON] = gi.imageindex ("../players/male/blue_i");
          }
        */
        
        
        s = Info_ValueForKey (ent->client->pers.userinfo, "skin");
        s = strchr(s, '/');
        if (s && strlen(s) > 0)
        {
            s++;
            strcpy(portrait, s);
            strcat(portrait, "_i");
        }
        else
        {
            strcpy(portrait, "redlion_i");
        }
        
        
        switch (ent->client->ctf.compass)
        {
        default:
        case 0:
            if (redflag && (redflag->owner == ent))
                ent->client->ps.stats[STAT_TEAM_ICON] = gi.imageindex ("redflaggone");
            else if (blueflag && (blueflag->owner == ent))
                ent->client->ps.stats[STAT_TEAM_ICON] = gi.imageindex ("blueflaggone");
            else
                ent->client->ps.stats[STAT_TEAM_ICON] = gi.imageindex (portrait);
            break;
        case 1:
            ent->client->ps.stats[STAT_TEAM_ICON] = gi.imageindex (ctf_facing(ent));
            break;
        case 2:
            ent->client->ps.stats[STAT_TEAM_ICON] = gi.imageindex (ctf_faceNorth(ent));
            break;
        case 3:
            ent->client->ps.stats[STAT_TEAM_ICON] = gi.imageindex (ctf_faceEnemyFlag(ent));
            break;
        }
        
        
        // Show status of the red flag
        if (redflag)
        {
            if (redflag->owner)
                ent->client->ps.stats[STAT_RED_ICON] = gi.imageindex ("redflaggone");
            else if (!ctf_flagathome(redflag))
                ent->client->ps.stats[STAT_RED_ICON] = gi.imageindex ("redflagdown");
            else
                ent->client->ps.stats[STAT_RED_ICON] = gi.imageindex ("redlion_i");
        }
        
        if (blueflag)
        {
            if (blueflag->owner)
                ent->client->ps.stats[STAT_BLUE_ICON] = gi.imageindex ("blueflaggone");
            else if (!ctf_flagathome(blueflag))
                ent->client->ps.stats[STAT_BLUE_ICON] = gi.imageindex ("blueflagdown");
            else
                ent->client->ps.stats[STAT_BLUE_ICON] = gi.imageindex ("bluewolf_i");
        }
        
        if (ent->client->rune)
        {
            if (ent->client->rune->runetype == RUNE_DAMAGE)
                ent->client->ps.stats[STAT_RUNE_ICON] = gi.imageindex ("strength");
            if (ent->client->rune->runetype == RUNE_RESIST)
                ent->client->ps.stats[STAT_RUNE_ICON] = gi.imageindex ("resist");
            if (ent->client->rune->runetype == RUNE_HASTE)
                ent->client->ps.stats[STAT_RUNE_ICON] = gi.imageindex ("haste");
            if (ent->client->rune->runetype == RUNE_REGEN)
                ent->client->ps.stats[STAT_RUNE_ICON] = gi.imageindex ("regen");
			if (ent->client->rune->runetype == RUNE_VAMP)                          //added by Vampire
                ent->client->ps.stats[STAT_RUNE_ICON] = gi.imageindex ("k_redkey");
                //ent->client->ps.stats[STAT_RUNE_ICON] = gi.imageindex ("resist");    
        }
        else
            ent->client->ps.stats[STAT_RUNE_ICON] = 0;
        
        // END CTF CODE -- LM_JORM
        
        //
        // health
        //
        ent->client->ps.stats[STAT_HEALTH_ICON] = level.pic_health;
        ent->client->ps.stats[STAT_HEALTH] = ent->health;
        
        //
        // ammo
        //
        if (!ent->client->ammo_index /* || !ent->client->pers.inventory[ent->client->ammo_index] */)
        {
            ent->client->ps.stats[STAT_AMMO_ICON] = 0;
            ent->client->ps.stats[STAT_AMMO] = 0;
        }
        else
        {
            item = &itemlist[ent->client->ammo_index];
            ent->client->ps.stats[STAT_AMMO_ICON] = gi.imageindex (item->icon);
            ent->client->ps.stats[STAT_AMMO] = ent->client->pers.inventory[ent->client->ammo_index];
        }
        
        //
        // armor
        //
        power_armor_type = PowerArmorType (ent);
        if (power_armor_type)
        {
            cells = ent->client->pers.inventory[ITEM_INDEX(FindItem ("cells"))];
            if (cells == 0)
            {   // ran out of cells for power armor
                ent->flags &= ~FL_POWER_ARMOR;
                gi.sound(ent, CHAN_ITEM, gi.soundindex("misc/power2.wav"), 1, ATTN_NORM, 0);
                power_armor_type = 0;;
            }
        }
        
        index = ArmorIndex (ent);
        if (power_armor_type && (!index || (level.framenum & 8) ) )
        {   // flash between power armor and other armor icon
            ent->client->ps.stats[STAT_ARMOR_ICON] = gi.imageindex ("i_powershield");
            ent->client->ps.stats[STAT_ARMOR] = cells;
        }
        else if (index)
        {
            item = GetItemByIndex (index);
            ent->client->ps.stats[STAT_ARMOR_ICON] = gi.imageindex (item->icon);
            ent->client->ps.stats[STAT_ARMOR] = ent->client->pers.inventory[index];
        }
        else
        {
            ent->client->ps.stats[STAT_ARMOR_ICON] = 0;
            ent->client->ps.stats[STAT_ARMOR] = 0;
        }
        
        //
        // pickup message
        //
        if (level.time > ent->client->pickup_msg_time)
        {
            ent->client->ps.stats[STAT_PICKUP_ICON] = 0;
            ent->client->ps.stats[STAT_PICKUP_STRING] = 0;
        }
        
        //
        // timers
        //
        if (ent->client->quad_framenum > level.framenum)
        {
            ent->client->ps.stats[STAT_TIMER_ICON] = gi.imageindex ("p_quad");
            ent->client->ps.stats[STAT_TIMER] = (ent->client->quad_framenum - level.framenum)/10;
        }
        else if (ent->client->invincible_framenum > level.framenum)
        {
            ent->client->ps.stats[STAT_TIMER_ICON] = gi.imageindex ("p_invulnerability");
            ent->client->ps.stats[STAT_TIMER] = (ent->client->invincible_framenum - level.framenum)/10;
        }
        else if (ent->client->enviro_framenum > level.framenum)
        {
            ent->client->ps.stats[STAT_TIMER_ICON] = gi.imageindex ("p_envirosuit");
            ent->client->ps.stats[STAT_TIMER] = (ent->client->enviro_framenum - level.framenum)/10;
        }
        else if (ent->client->breather_framenum > level.framenum)
        {
            ent->client->ps.stats[STAT_TIMER_ICON] = gi.imageindex ("p_rebreather");
            ent->client->ps.stats[STAT_TIMER] = (ent->client->breather_framenum - level.framenum)/10;
        }
        else
        {
            ent->client->ps.stats[STAT_TIMER_ICON] = 0;
            ent->client->ps.stats[STAT_TIMER] = 0;
        }
        
        //
        // selected item
        //
        
        // Show proper flag item
        if (ent->client->pers.selected_item == -1)
            ent->client->ps.stats[STAT_SELECTED_ICON] = 0;
        else
        {
            if (blueflag && blueflag->item == &itemlist[ent->client->pers.selected_item])
            {
                ent->client->ps.stats[STAT_SELECTED_ICON] = gi.imageindex ("a_blueflag");
            }
            else if (redflag && redflag->item == &itemlist[ent->client->pers.selected_item])
            {
                ent->client->ps.stats[STAT_SELECTED_ICON] = gi.imageindex ("a_redflag");            
            }
            else
            {
                ent->client->ps.stats[STAT_SELECTED_ICON] = gi.imageindex (itemlist[ent->client->pers.selected_item].icon);
                
            }
        }
        
        ent->client->ps.stats[STAT_SELECTED_ITEM] = ent->client->pers.selected_item;
        
        //
        // frags
        //
        ent->client->ps.stats[STAT_FRAGS] = stats_get(ent, STATS_SCORE);
        
        //
        // help icon / current weapon if not shown
        //
        if (ent->client->pers.helpchanged && (level.framenum&8) )
            ent->client->ps.stats[STAT_HELPICON] = gi.imageindex ("i_help");
        else if ( (ent->client->pers.hand == CENTER_HANDED || ent->client->ps.fov > 91)
            && ent->client->pers.weapon)
            ent->client->ps.stats[STAT_HELPICON] = gi.imageindex (ent->client->pers.weapon->icon);
        else
            ent->client->ps.stats[STAT_HELPICON] = 0;
    }

    //
    // layouts
    //
    ent->client->ps.stats[STAT_LAYOUTS] = 0;

    if (deathmatch->value)
    {
        if (ent->client->pers.health <= 0 || level.intermissiontime
            || ent->client->showscores || ent->client->showctfhud
            || ent->client->showmod || ent->client->showmenu
			|| ent->client->showsquadboard || ent->client->showstatboard
            || ent->client->showteamstatboard || ent->client->showrailboard // ADC //BUZZKILL
            || ent->client->showseason || ent->client->showrecords
            || ent->client->showactivity || ent->client->showmomentum)
            ent->client->ps.stats[STAT_LAYOUTS] |= 1;
        if (ent->client->showinventory && ent->client->pers.health > 0)
            ent->client->ps.stats[STAT_LAYOUTS] |= 2;
    }
    else
    {
        if (ent->client->showscores || ent->client->showhelp
            || ent->client->showctfhud || ent->client->showmod
			|| ent->client->showsquadboard || ent->client->showstatboard
            || ent->client->showteamstatboard || ent->client->showrailboard // ADC // BUZZKILL
            || ent->client->showseason || ent->client->showrecords
            || ent->client->showactivity || ent->client->showmomentum)
            ent->client->ps.stats[STAT_LAYOUTS] |= 1;
        if (ent->client->showinventory && ent->client->pers.health > 0)
            ent->client->ps.stats[STAT_LAYOUTS] |= 2;
    }

    // LM_JORM -- Turn CTF HUD back on automatically
    /*
    if ((level.framenum - ent->client->awayframe > 10) &&
        !ent->client->showmenu)
        ent->client->showctfhud = true;
    */

	ent->client->ps.stats[STAT_SPECTATOR] = 0;
}

/*
===============
G_CheckChaseStats
===============
*/
void G_CheckChaseStats (edict_t *ent)
{
	int i;
	gclient_t *cl;

	for (i = 1; i <= maxclients->value; i++) {
		cl = g_edicts[i].client;
		if (!g_edicts[i].inuse || cl->chase_target != ent)
			continue;
		memcpy(cl->ps.stats, ent->client->ps.stats, sizeof(cl->ps.stats));
		G_SetSpectatorStats(g_edicts + i);
	}
}

/*
===============
G_SetSpectatorStats
===============
*/
void G_SetSpectatorStats (edict_t *ent)
{
	gclient_t *cl = ent->client;

	if (!cl->chase_target)
		G_SetStats (ent);

	cl->ps.stats[STAT_SPECTATOR] = 1;

	// layouts are independant in spectator
	cl->ps.stats[STAT_LAYOUTS] = 0;
	//if (cl->pers.health <= 0 || level.intermissiontime || cl->showscores)
//	if (cl->pers.health <= 0 || level.intermissiontime || cl->showscores || cl->showmenu)
	
	//bat - I think that cl->pers.health is only supposed to be checked in deathmatch
	
	if(level.intermissiontime || cl->showscores || cl->showmenu || cl->showrailboard || cl->showstatboard || cl->showteamstatboard
		|| cl->showseason || cl->showrecords || cl->showactivity || cl->showmomentum)
		cl->ps.stats[STAT_LAYOUTS] |= 1;
	if (cl->showinventory && cl->pers.health > 0)
		cl->ps.stats[STAT_LAYOUTS] |= 2;

	if (cl->chase_target && cl->chase_target->inuse)
		cl->ps.stats[STAT_CHASE] = CS_PLAYERSKINS + 
			(cl->chase_target - g_edicts) - 1;
	else
		cl->ps.stats[STAT_CHASE] = 0;
}
