#include "g_local.h"
#include "g_ctffunc.h" //surt for some nice wrapper functions
#include "g_tourney.h"
#include "bat.h"
#include "slipgate/sg_chat.h"       // BUZZKILL - SG_ChatLevelEnd from BeginIntermission
#include "ctf_sqlite_unidb.h"       // BUZZKILL - DB_SessionRecord from BeginIntermission
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

// Board_LineLen -- legacy-cap byte accounting for the ui_layout.h
// conversions below (StatboardMessage, TeamStatboardMessage,
// CTFSquadboardMessage, DeathmatchScoreboardMessage).
//
// Every producer converted here used to test its own running
// "stringlength" against a hand-picked ceiling (1024 bytes; the
// squadboard used 1000) before strcpy-ing one more formatted line in.
// ui_layout_compile() now has a bigger budget to work with
// (UI_LAYOUT_BUDGET, 1380 bytes, ui_layout.h) -- but letting that
// larger budget decide how many rows/lines survive would be a visible
// content change (more rows shown than before), which this pass does
// not make. The extra headroom is deliberately left unspent until a
// future change spends it on purpose.
//
// This measures the byte length of one already-formatted wire line
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

/*
==================
DeathmatchScoreboardMessage

==================
*/
void DeathmatchScoreboardMessage (edict_t *ent, edict_t *killer)
{
    char                storage[UI_LAYOUT_BUDGET];
    ui_buf_t            sb;
    ui_screen_t         screen;
    ui_elem_t           elems[320];
    dmscore_row_bufs_t  red_buf[DMSCORE_MAX_ROWS];
    dmscore_row_bufs_t  blue_buf[DMSCORE_MAX_ROWS];

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
    /* BUZZKILL - never hand NULL to the formatter: glibc renders it as
     * a literal "(null)" on every player's HUD (the owner's screenshot,
     * wave 266 era). A dash is the honest empty. */
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
    char*   player_rune;
    // BUZZKILL - ADVANCED ANALYTICS SCOREBOARD - END

    int     x, y;
    gclient_t   *cl;
    edict_t     *cl_ent;

    qboolean    showsmall;
    qboolean    is_red_fc;
    qboolean    is_blue_fc;

    // ui_layout.h conversion state -- see this function's banner and
    // Board_LineLen's banner (top of file). legacy_len replaces
    // "stringlength" and is checked against the SAME 1024-byte cap the
    // hand-written producer used; ui_layout_compile's own 1380-byte
    // budget (UI_LAYOUT_BUDGET) is real headroom this pass leaves
    // unspent, not a wider cap this board now gets to use.
    int         n;
    int         dropped;
    int         legacy_len;
    int         line_len;

    int     mvp_n;
    int     mvp_x[9], mvp_y[9];
    char    mvp_text[9][100];
    int     mvp_total_len;

    // Observers listing scratch -- see the conversion note at its use
    // site below for the duplication quirk this replicates.
    int         els2_n;
    int         els2_start = 0;    /* current category's slice start */
    int         els2_x[64], els2_y[64];
    char        els2_text[64][32];
    int         els2_len;
    int         sec_len;
    qboolean    gate_ok;

    int     foot_len;

    int     rows;
    int     fy;
    int     pf_len;
    char    pf_text[6][32];

    player_rune = NULL;
    is_red_fc = false;
    is_blue_fc = false;
    showsmall = false;

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

    // add the clients in sorted order
    legacy_len = 0;
    n = 0;

    if (red > 6 || red + blue + total_observers > 16)
    {
        showsmall = true;
        if (red > 21)
            red = 21;
    }

	if (blue > 6 || red + blue + total_observers > 16)
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

            elems[n].kind = UI_TEXT; elems[n].u.text.x = 0;   elems[n].u.text.y = 32; elems[n].u.text.text = "Scr Png Name        "; elems[n].u.text.highlight = true; n++;
            elems[n].kind = UI_TEXT; elems[n].u.text.x = 0;   elems[n].u.text.y = 40; elems[n].u.text.text = "------------------- "; elems[n].u.text.highlight = true; n++;
            elems[n].kind = UI_TEXT; elems[n].u.text.x = 160; elems[n].u.text.y = 32; elems[n].u.text.text = "Scr Png Name        "; elems[n].u.text.highlight = true; n++;
            elems[n].kind = UI_TEXT; elems[n].u.text.x = 160; elems[n].u.text.y = 40; elems[n].u.text.text = "------------------- "; elems[n].u.text.highlight = true; n++;
        }
    }


    for (i=0 ; i<red ; i++)
    {
        cl = &game.clients[redsorted[i]];
        cl_ent = g_edicts + 1 + redsorted[i];

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
                    break;
                legacy_len += line_len;

                elems[n].kind = UI_TEXT;
                elems[n].u.text.x = x + 32 - 136 + 80;
                elems[n].u.text.y = y;
                elems[n].u.text.text = player_rune;
                elems[n].u.text.highlight = true;
                n++;

            }

            if (cl_ent == Query_DMVP())
			{
				Com_sprintf(red_buf[i].main_text, sizeof(red_buf[i].main_text), "D%3d %3d %s", cl->resp.score, cl->ping, cl->pers.netname);
				red_buf[i].main_text[19] = 0;

				// FIXED (found during the declarative conversion): the
				// original's strcat path drew an MVP row's rune line
				// TWICE -- once from the rune block above and again
				// folded into this row's flush, because string2 was
				// never cleared between them. One rune line per row.
				line_len = Board_LineLen("xv %i yv %i string2 \"%s\" ", x, y, red_buf[i].main_text);
				if (legacy_len + line_len > 1024)
					break;
				legacy_len += line_len;

				elems[n].kind = UI_TEXT;
				elems[n].u.text.x = x;
				elems[n].u.text.y = y;
				elems[n].u.text.text = red_buf[i].main_text;
				elems[n].u.text.highlight = true;
				n++;
			}
            else if (cl_ent == Query_OMVP())
			{
				Com_sprintf(red_buf[i].main_text, sizeof(red_buf[i].main_text), "O%3d %3d %s", cl->resp.score, cl->ping, cl->pers.netname);
				red_buf[i].main_text[19] = 0;

				/* FIXED: one rune line per row (the pre-conversion strcat
				 * path drew an MVP row's rune twice; see the red DMVP site) */
				line_len = Board_LineLen("xv %i yv %i string2 \"%s\" ", x, y, red_buf[i].main_text);
				if (legacy_len + line_len > 1024)
					break;
				legacy_len += line_len;

				elems[n].kind = UI_TEXT;
				elems[n].u.text.x = x;
				elems[n].u.text.y = y;
				elems[n].u.text.text = red_buf[i].main_text;
				elems[n].u.text.highlight = true;
				n++;
			}
            else
            {
                Com_sprintf(red_buf[i].main_text, sizeof(red_buf[i].main_text), "ctf %d %d %d %ld %d ", x, y, redsorted[i],
                    stats_get(cl_ent, STATS_SCORE), cl->ping > 999 ? 999 : cl->ping);

                line_len = (int)strlen(red_buf[i].main_text);
                if (legacy_len + line_len > 1024)
                    break;
                legacy_len += line_len;

                elems[n].kind = UI_RAW;
                elems[n].u.raw.text = red_buf[i].main_text;
                n++;
            }
        }
        else
        {
            x = 0;
            y = 32 + 32 * (i%6);

            Com_sprintf(red_buf[i].main_text, sizeof(red_buf[i].main_text), "client %i %i %i %i %i %i ",
                x, y, redsorted[i], (int)stats_get(cl_ent, STATS_SCORE),
                cl->ping, (level.framenum - cl->resp.enterframe) / 600);

            line_len = (int)strlen(red_buf[i].main_text);
            if (legacy_len + line_len > 1024)
                break;
            legacy_len += line_len;

            elems[n].kind = UI_RAW;
            elems[n].u.raw.text = red_buf[i].main_text;
            n++;

            if (stats_get(cl_ent, STATS_CAPTURES))
            {
                Com_sprintf (red_buf[i].capt_text, sizeof(red_buf[i].capt_text), "C:%i", (int)stats_get(cl_ent, STATS_CAPTURES));

                line_len = Board_LineLen("xv %i yv %i string2 \"%s\" ", x+32+80, y+24, red_buf[i].capt_text);
                if (legacy_len + line_len > 1024)
                    break;
                legacy_len += line_len;

                elems[n].kind = UI_TEXT;
                elems[n].u.text.x = x+32+80;
                elems[n].u.text.y = y+24;
                elems[n].u.text.text = red_buf[i].capt_text;
                elems[n].u.text.highlight = true;
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

                Com_sprintf(red_buf[i].rune_text, sizeof(red_buf[i].rune_text), "R:%s", player_rune);

                line_len = Board_LineLen("xv %i yv %i string2 \"%s\" ", x + 32 + 80, y + 16, red_buf[i].rune_text);
                if (legacy_len + line_len > 1024)
                    break;
                legacy_len += line_len;

                elems[n].kind = UI_TEXT;
                elems[n].u.text.x = x + 32 + 80;
                elems[n].u.text.y = y + 16;
                elems[n].u.text.text = red_buf[i].rune_text;
                elems[n].u.text.highlight = true;
                n++;
            }

            if (cl_ent == Query_DMVP())
            {
                line_len = Board_LineLen("xv %d yv %d picn dmvpicon ", x, y);
                if (legacy_len + line_len > 1024)
                    break;
                legacy_len += line_len;

                elems[n].kind = UI_PIC;
                elems[n].u.pic.x = x;
                elems[n].u.pic.y = y;
                elems[n].u.pic.stat_driven = false;
                elems[n].u.pic.image.name = "dmvpicon";
                n++;
            }
            else if (cl_ent == Query_OMVP())
            {
                line_len = Board_LineLen("xv %d yv %d picn omvpicon ", x, y);
                if (legacy_len + line_len > 1024)
                    break;
                legacy_len += line_len;

                elems[n].kind = UI_PIC;
                elems[n].u.pic.x = x;
                elems[n].u.pic.y = y;
                elems[n].u.pic.stat_driven = false;
                elems[n].u.pic.image.name = "omvpicon";
                n++;
            }

        }
        // END PLAY -- LM JORM

	}

    for (i=0 ; i<blue ; i++)
    {
        cl = &game.clients[bluesorted[i]];
        cl_ent = g_edicts + 1 + bluesorted[i];

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
                    break;
                legacy_len += line_len;

                elems[n].kind = UI_TEXT;
                elems[n].u.text.x = x + 32 + 56 + 80;
                elems[n].u.text.y = y;
                elems[n].u.text.text = player_rune;
                elems[n].u.text.highlight = true;
                n++;

            }

            if (cl_ent == Query_DMVP())
			{
				Com_sprintf(blue_buf[i].main_text, sizeof(blue_buf[i].main_text), "D%3d %3d %s", cl->resp.score, cl->ping, cl->pers.netname);
				blue_buf[i].main_text[19] = 0;

				/* FIXED: one rune line per row (the pre-conversion
				 * strcat path drew an MVP row's rune twice; see the
				 * red DMVP site) */
				line_len = Board_LineLen("xv %i yv %i string2 \"%s\" ", x, y, blue_buf[i].main_text);
				if (legacy_len + line_len > 1024)
					break;
				legacy_len += line_len;

				elems[n].kind = UI_TEXT;
				elems[n].u.text.x = x;
				elems[n].u.text.y = y;
				elems[n].u.text.text = blue_buf[i].main_text;
				elems[n].u.text.highlight = true;
				n++;
			}
            else if (cl_ent == Query_OMVP())
			{
				Com_sprintf(blue_buf[i].main_text, sizeof(blue_buf[i].main_text), "O%3d %3d %s", cl->resp.score, cl->ping, cl->pers.netname);
				blue_buf[i].main_text[19] = 0;

				/* FIXED: one rune line per row (the pre-conversion
				 * strcat path drew an MVP row's rune twice; see the
				 * red DMVP site) */
				line_len = Board_LineLen("xv %i yv %i string2 \"%s\" ", x, y, blue_buf[i].main_text);
				if (legacy_len + line_len > 1024)
					break;
				legacy_len += line_len;

				elems[n].kind = UI_TEXT;
				elems[n].u.text.x = x;
				elems[n].u.text.y = y;
				elems[n].u.text.text = blue_buf[i].main_text;
				elems[n].u.text.highlight = true;
				n++;
			}
			else
			{
				Com_sprintf(blue_buf[i].main_text, sizeof(blue_buf[i].main_text),
					"ctf %d %d %d %ld %d ",
					x, y,
					bluesorted[i],
					stats_get(cl_ent, STATS_SCORE),
					cl->ping > 999 ? 999 : cl->ping);

				line_len = (int)strlen(blue_buf[i].main_text);
				if (legacy_len + line_len > 1024)
					break;
				legacy_len += line_len;

				elems[n].kind = UI_RAW;
				elems[n].u.raw.text = blue_buf[i].main_text;
				n++;
			}
        }
        else
        {
            x = 160;
            y = 32 + 32 * (i%6);

            Com_sprintf(blue_buf[i].main_text, sizeof(blue_buf[i].main_text), "client %i %i %i %i %i %i ",
                x, y, bluesorted[i], (int)stats_get(cl_ent, STATS_SCORE),
                cl->ping, (level.framenum - cl->resp.enterframe) / 600);

            line_len = (int)strlen(blue_buf[i].main_text);
            if (legacy_len + line_len > 1024)
                break;
            legacy_len += line_len;

            elems[n].kind = UI_RAW;
            elems[n].u.raw.text = blue_buf[i].main_text;
            n++;

            if (stats_get(cl_ent, STATS_CAPTURES))
            {
                Com_sprintf (blue_buf[i].capt_text, sizeof(blue_buf[i].capt_text), "C:%i", (int)stats_get(cl_ent, STATS_CAPTURES));

                line_len = Board_LineLen("xv %i yv %i string2 \"%s\" ", x+32+80, y+24, blue_buf[i].capt_text);
                if (legacy_len + line_len > 1024)
                    break;
                legacy_len += line_len;

                elems[n].kind = UI_TEXT;
                elems[n].u.text.x = x+32+80;
                elems[n].u.text.y = y+24;
                elems[n].u.text.text = blue_buf[i].capt_text;
                elems[n].u.text.highlight = true;
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

                Com_sprintf(blue_buf[i].rune_text, sizeof(blue_buf[i].rune_text), "R:%s", player_rune);

                line_len = Board_LineLen("xv %i yv %i string2 \"%s\" ", x + 32 + 80, y + 16, blue_buf[i].rune_text);
                if (legacy_len + line_len > 1024)
                    break;
                legacy_len += line_len;

                elems[n].kind = UI_TEXT;
                elems[n].u.text.x = x + 32 + 80;
                elems[n].u.text.y = y + 16;
                elems[n].u.text.text = blue_buf[i].rune_text;
                elems[n].u.text.highlight = true;
                n++;
            }

            if (cl_ent == Query_DMVP())
            {
                line_len = Board_LineLen("xv %d yv %d picn dmvpicon ", x, y);
                if (legacy_len + line_len > 1024)
                    break;
                legacy_len += line_len;

                elems[n].kind = UI_PIC;
                elems[n].u.pic.x = x;
                elems[n].u.pic.y = y;
                elems[n].u.pic.stat_driven = false;
                elems[n].u.pic.image.name = "dmvpicon";
                n++;
            }
            else if (cl_ent == Query_OMVP())
            {
                line_len = Board_LineLen("xv %d yv %d picn omvpicon ", x, y);
                if (legacy_len + line_len > 1024)
                    break;
                legacy_len += line_len;

                elems[n].kind = UI_PIC;
                elems[n].u.pic.x = x;
                elems[n].u.pic.y = y;
                elems[n].u.pic.stat_driven = false;
                elems[n].u.pic.image.name = "omvpicon";
                n++;
            }

        }
        // END PLAY -- LM JORM
    }


    y = 32 * 8;

	if(MvpDisp)
	{
		mvp_n = 0;

		Com_sprintf(mvp_text[mvp_n], sizeof(mvp_text[mvp_n]), "*** %s MVPs ***", level.mapname);
		mvp_x[mvp_n] = 80; mvp_y[mvp_n] = y; mvp_n++;
		y += 8;

    	if(Railgun_Victor)
		{
			Com_sprintf(mvp_text[mvp_n], sizeof(mvp_text[mvp_n]), "Railgod -> %s", Railgun_Victor->client->pers.netname);
			mvp_x[mvp_n] = 100; mvp_y[mvp_n] = y; mvp_n++;
			y += 8;
		}

		Com_sprintf(mvp_text[mvp_n], sizeof(mvp_text[mvp_n]), "1) %s %4ld", Highscore_Table[0].Player, Highscore_Table[0].Score);
		mvp_x[mvp_n] = 130; mvp_y[mvp_n] = y; mvp_n++;
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

			Com_sprintf(mvp_text[mvp_n], sizeof(mvp_text[mvp_n]), "%d) %15s %4ld", i + 1, Highscore_Table[i].Player, Highscore_Table[i].Score);
			mvp_x[mvp_n] = x; mvp_y[mvp_n] = y; mvp_n++;
			y += 8;
		}

		mvp_total_len = 0;
		for (i = 0; i < mvp_n; i++)
			mvp_total_len += Board_LineLen("xv %i yv %i string2 \"%s\" ", mvp_x[i], mvp_y[i], mvp_text[i]);

		if (legacy_len + mvp_total_len <= 1024)
		{
			legacy_len += mvp_total_len;

			for (i = 0; i < mvp_n; i++)
			{
				elems[n].kind = UI_TEXT;
				elems[n].u.text.x = mvp_x[i];
				elems[n].u.text.y = mvp_y[i];
				elems[n].u.text.text = mvp_text[i];
				elems[n].u.text.highlight = true;
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
		gate_ok = (red + blue + total_observers <= 16) ? true : false;

		if(red_observers)
		{
			x = 0;
			els2_x[els2_n] = x; els2_y[els2_n] = y;
			Com_sprintf(els2_text[els2_n], sizeof(els2_text[els2_n]), "Red Observers:");
			els2_n++;
			y += 8;
            if (gate_ok)
            {
                for (i = 0; i < red_observers; i++)
                {
                    cl = &game.clients[sorted_red_observers[i]];
                    cl_ent = g_edicts + 1 + sorted_red_observers[i];
                    els2_x[els2_n] = x; els2_y[els2_n] = y;
                    Com_sprintf(els2_text[els2_n], sizeof(els2_text[els2_n]), "%s", cl->pers.netname);
                    els2_n++;
                    y += 8;
                }

                sec_len = 0;
                for (i = els2_start; i < els2_n; i++)
                    sec_len += Board_LineLen("xv %i yv %i string2 \"%s\" ", els2_x[i], els2_y[i], els2_text[i]);
                els2_len = sec_len;

                if (legacy_len + els2_len <= 1024)
                {
                    for (i = els2_start; i < els2_n; i++)
                    {
                        elems[n].kind = UI_TEXT;
                        elems[n].u.text.x = els2_x[i];
                        elems[n].u.text.y = els2_y[i];
                        elems[n].u.text.text = els2_text[i];
                        elems[n].u.text.highlight = true;
                        n++;
                    }
                    legacy_len += els2_len;
                }
            }
		}

		if(blue_observers)
		{
			x = 160;
			els2_x[els2_n] = x; els2_y[els2_n] = y;
			Com_sprintf(els2_text[els2_n], sizeof(els2_text[els2_n]), "Blue Observers:");
			els2_n++;
			y += 8;
            if (gate_ok)
            {
                for (i = 0; i < blue_observers; i++)
                {
                    cl = &game.clients[sorted_blue_observers[i]];
                    cl_ent = g_edicts + 1 + sorted_blue_observers[i];
                    els2_x[els2_n] = x; els2_y[els2_n] = y;
                    Com_sprintf(els2_text[els2_n], sizeof(els2_text[els2_n]), "%s", cl->pers.netname);
                    els2_n++;
                    y += 8;
                }

                sec_len = 0;
                for (i = els2_start; i < els2_n; i++)
                    sec_len += Board_LineLen("xv %i yv %i string2 \"%s\" ", els2_x[i], els2_y[i], els2_text[i]);
                els2_len = sec_len;

                if (legacy_len + els2_len <= 1024)
                {
                    for (i = els2_start; i < els2_n; i++)
                    {
                        elems[n].kind = UI_TEXT;
                        elems[n].u.text.x = els2_x[i];
                        elems[n].u.text.y = els2_y[i];
                        elems[n].u.text.text = els2_text[i];
                        elems[n].u.text.highlight = true;
                        n++;
                    }
                    legacy_len += els2_len;
                }
            }
		}



		if(reg_observers)
		{
            if (gate_ok)
            {
                //give more space for the reg observers
                if (red_observers == 0 && blue_observers == 0)
                {
                    x = 80;
                    els2_start = els2_n;   /* FIXED: flush this category's slice only */
                    els2_x[els2_n] = x; els2_y[els2_n] = y;
                    Com_sprintf(els2_text[els2_n], sizeof(els2_text[els2_n]), "Observers:");
                    els2_n++;
                    y += 8;

                    //Do 2 obs per line

                    for (i = 0; i < reg_observers; i++)
                    {
                        x = (i % 3) * 130;

                        cl = &game.clients[sorted_reg_observers[i]];
                        cl_ent = g_edicts + 1 + sorted_reg_observers[i];
                        els2_x[els2_n] = x; els2_y[els2_n] = y;
                        Com_sprintf(els2_text[els2_n], sizeof(els2_text[els2_n]), "%s", cl->pers.netname);
                        els2_n++;

                        if ((i % 3) == 2)
                            y += 8;
                    }

                    sec_len = 0;
                    for (i = els2_start; i < els2_n; i++)
                        sec_len += Board_LineLen("xv %i yv %i string2 \"%s\" ", els2_x[i], els2_y[i], els2_text[i]);
                    els2_len = sec_len;

                    if (legacy_len + els2_len <= 1024)
                    {
                        for (i = els2_start; i < els2_n; i++)
                        {
                            elems[n].kind = UI_TEXT;
                            elems[n].u.text.x = els2_x[i];
                            elems[n].u.text.y = els2_y[i];
                            elems[n].u.text.text = els2_text[i];
                            elems[n].u.text.highlight = true;
                            n++;
                        }
                        legacy_len += els2_len;
                    }
                }
                else
                {
                    x = 80;
                    els2_start = els2_n;   /* FIXED: flush this category's slice only */
                    els2_x[els2_n] = x; els2_y[els2_n] = y;
                    Com_sprintf(els2_text[els2_n], sizeof(els2_text[els2_n]), "Observers:");
                    els2_n++;
                    y += 8;

                    for (i = 0; i < reg_observers; i++)
                    {
                        cl = &game.clients[sorted_reg_observers[i]];
                        cl_ent = g_edicts + 1 + sorted_reg_observers[i];
                        els2_x[els2_n] = x; els2_y[els2_n] = y;
                        Com_sprintf(els2_text[els2_n], sizeof(els2_text[els2_n]), "%s", cl->pers.netname);
                        els2_n++;
                        y += 8;
                    }

                    sec_len = 0;
                    for (i = els2_start; i < els2_n; i++)
                        sec_len += Board_LineLen("xv %i yv %i string2 \"%s\" ", els2_x[i], els2_y[i], els2_text[i]);
                    els2_len = sec_len;

                    if (legacy_len + els2_len <= 1024)
                    {
                        for (i = els2_start; i < els2_n; i++)
                        {
                            elems[n].kind = UI_TEXT;
                            elems[n].u.text.x = els2_x[i];
                            elems[n].u.text.y = els2_y[i];
                            elems[n].u.text.text = els2_text[i];
                            elems[n].u.text.highlight = true;
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
            + Board_LineLen("xv %i yv %i string2 \"%s\" ", 36, 8, redfc)
            + Board_LineLen("xv %i yv %i string2 \"%s\" ", 36, 16, "Runes")
            + Board_LineLen("xv %i yv %i string2 \"%s\" ", 36, 24, red_runes)
            + Board_LineLen("xv %i yv %i string2 \"%s\" ", 196, 0, "FC:")
            + Board_LineLen("xv %i yv %i string2 \"%s\" ", 196, 8, bluefc)
            + Board_LineLen("xv %i yv %i string2 \"%s\" ", 196, 16, "Runes")
            + Board_LineLen("xv %i yv %i string2 \"%s\" ", 196, 24, blue_runes);

        if (legacy_len + foot_len < 1024)
        {
            legacy_len += foot_len;

            elems[n].kind=UI_PIC; elems[n].u.pic.x=0;   elems[n].u.pic.y=0; elems[n].u.pic.stat_driven=false; elems[n].u.pic.image.name="redlion_i";   n++;
            elems[n].kind=UI_PIC; elems[n].u.pic.x=160; elems[n].u.pic.y=0; elems[n].u.pic.stat_driven=false; elems[n].u.pic.image.name="bluewolf_i"; n++;
            elems[n].kind=UI_PIC; elems[n].u.pic.x=32;  elems[n].u.pic.y=0; elems[n].u.pic.stat_driven=false; elems[n].u.pic.image.name="redtag";     n++;
            elems[n].kind=UI_PIC; elems[n].u.pic.x=192; elems[n].u.pic.y=0; elems[n].u.pic.stat_driven=false; elems[n].u.pic.image.name="bluetag";    n++;

            elems[n].kind=UI_TEXT; elems[n].u.text.x=36;  elems[n].u.text.y=0;  elems[n].u.text.text="FC:";     elems[n].u.text.highlight=true; n++;
            elems[n].kind=UI_TEXT; elems[n].u.text.x=36;  elems[n].u.text.y=8;  elems[n].u.text.text=redfc;     elems[n].u.text.highlight=true; n++;
            elems[n].kind=UI_TEXT; elems[n].u.text.x=36;  elems[n].u.text.y=16; elems[n].u.text.text="Runes";   elems[n].u.text.highlight=true; n++;
            elems[n].kind=UI_TEXT; elems[n].u.text.x=36;  elems[n].u.text.y=24; elems[n].u.text.text=red_runes; elems[n].u.text.highlight=true; n++;

            elems[n].kind=UI_TEXT; elems[n].u.text.x=196; elems[n].u.text.y=0;  elems[n].u.text.text="FC:";      elems[n].u.text.highlight=true; n++;
            elems[n].kind=UI_TEXT; elems[n].u.text.x=196; elems[n].u.text.y=8;  elems[n].u.text.text=bluefc;     elems[n].u.text.highlight=true; n++;
            elems[n].kind=UI_TEXT; elems[n].u.text.x=196; elems[n].u.text.y=16; elems[n].u.text.text="Runes";    elems[n].u.text.highlight=true; n++;
            elems[n].kind=UI_TEXT; elems[n].u.text.x=196; elems[n].u.text.y=24; elems[n].u.text.text=blue_runes; elems[n].u.text.highlight=true; n++;
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
        // The footer sits BELOW the roster, and the roster's row height
        // depends on which scoreboard this is: the small layout packs
        // players at 8 pixels (y = 48 + 8i), the big portrait layout at
        // 32 (y = 32 + 32i).  The first cut assumed 8 always -- at five
        // players a side the footer landed at y=96, printed straight
        // over the third portrait row (reported live from the big
        // board).  Compute from the layout actually in effect.
        fy = showsmall ? (48 + 8 * rows + 8)
                       : (32 + 32 * ((rows > 6 ? 6 : rows)) + 8);

        // only draw it when the roster leaves vertical room for three rows
        if (fy + 16 <= 232)
        {
            pf_len = Board_LineLen("xv 0 yv %i string2 \"RED %3i pts\" ", fy, redscore)
                + Board_LineLen("xv 0 yv %i string2 \"itm Q%2i S%2i A%2i M%2i\" ", fy + 8, red_item_quad, red_item_shield, red_item_armor, red_item_mega)
                + Board_LineLen("xv 0 yv %i string2 \"rne S%2i H%2i G%2i R%2i\" ", fy + 16, red_rune_strength, red_rune_haste, red_rune_regen, red_rune_resist)
                + Board_LineLen("xv 160 yv %i string2 \"BLUE %3i pts\" ", fy, bluescore)
                + Board_LineLen("xv 160 yv %i string2 \"itm Q%2i S%2i A%2i M%2i\" ", fy + 8, blue_item_quad, blue_item_shield, blue_item_armor, blue_item_mega)
                + Board_LineLen("xv 160 yv %i string2 \"rne S%2i H%2i G%2i R%2i\" ", fy + 16, blue_rune_strength, blue_rune_haste, blue_rune_regen, blue_rune_resist);

            if (legacy_len + pf_len < 1024)
            {
                legacy_len += pf_len;

                Com_sprintf(pf_text[0], sizeof(pf_text[0]), "RED %3i pts", redscore);
                Com_sprintf(pf_text[1], sizeof(pf_text[1]), "itm Q%2i S%2i A%2i M%2i", red_item_quad, red_item_shield, red_item_armor, red_item_mega);
                Com_sprintf(pf_text[2], sizeof(pf_text[2]), "rne S%2i H%2i G%2i R%2i", red_rune_strength, red_rune_haste, red_rune_regen, red_rune_resist);
                Com_sprintf(pf_text[3], sizeof(pf_text[3]), "BLUE %3i pts", bluescore);
                Com_sprintf(pf_text[4], sizeof(pf_text[4]), "itm Q%2i S%2i A%2i M%2i", blue_item_quad, blue_item_shield, blue_item_armor, blue_item_mega);
                Com_sprintf(pf_text[5], sizeof(pf_text[5]), "rne S%2i H%2i G%2i R%2i", blue_rune_strength, blue_rune_haste, blue_rune_regen, blue_rune_resist);

                elems[n].kind=UI_TEXT; elems[n].u.text.x=0;   elems[n].u.text.y=fy;      elems[n].u.text.text=pf_text[0]; elems[n].u.text.highlight=true; n++;
                elems[n].kind=UI_TEXT; elems[n].u.text.x=0;   elems[n].u.text.y=fy + 8;  elems[n].u.text.text=pf_text[1]; elems[n].u.text.highlight=true; n++;
                elems[n].kind=UI_TEXT; elems[n].u.text.x=0;   elems[n].u.text.y=fy + 16; elems[n].u.text.text=pf_text[2]; elems[n].u.text.highlight=true; n++;
                elems[n].kind=UI_TEXT; elems[n].u.text.x=160; elems[n].u.text.y=fy;      elems[n].u.text.text=pf_text[3]; elems[n].u.text.highlight=true; n++;
                elems[n].kind=UI_TEXT; elems[n].u.text.x=160; elems[n].u.text.y=fy + 8;  elems[n].u.text.text=pf_text[4]; elems[n].u.text.highlight=true; n++;
                elems[n].kind=UI_TEXT; elems[n].u.text.x=160; elems[n].u.text.y=fy + 16; elems[n].u.text.text=pf_text[5]; elems[n].u.text.highlight=true; n++;
            }
        }
    }

    screen.elems = elems;
    screen.count = n;

    ui_buf_init(&sb, storage, sizeof(storage));
    dropped = ui_layout_compile(&screen, &sb);
    if (dropped > 0)
    {
        gi.dprintf("DeathmatchScoreboard: %d element(s) dropped by the layout budget\n", dropped);
    }

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

void CTFSquadboardMessage (edict_t *ent, edict_t* killer) // ADC
{
	char                storage[UI_LAYOUT_BUDGET];
	ui_buf_t            sb;
	ui_screen_t         screen;
	ui_elem_t           elems[3 + 2 * CTFSQUADBOARD_MAX_ROWS];
	char                status_text[CTFSQUADBOARD_MAX_ROWS][UI_CELL_LEN];

	int		len, i, j, team, ready;
	edict_t		*cl_ent;

	gclient_t* clients [MAX_CLIENTS];
	int clientCount = 0;
	gclient_t* sortedClients [MAX_CLIENTS];
	int sortedCount = 0;

	int teamOfInterest = 0;

	char* squad = 0;
	int numCategoryLines = 0;

	char statusStart [MAX_STATUS_LEN];
	int greenStatusLen = (int)strlen (GREEN_STATUS_STR);

	int widestName = 0; // in chars

	int		row_count;			// rows actually walked (min(16, sortedCount))
	qboolean	row_has_header[CTFSQUADBOARD_MAX_ROWS];
	int		row_header_y[CTFSQUADBOARD_MAX_ROWS];
	int		row_status_y[CTFSQUADBOARD_MAX_ROWS];
	qboolean	row_ready[CTFSQUADBOARD_MAX_ROWS];
	qboolean	row_admitted[CTFSQUADBOARD_MAX_ROWS];

	int		header_actual_len;
	int		legacy_len;
	qboolean	legacy_admitted_once;
	int		entry_len;
	int		n;
	int		dropped;
	int		legacy_dropped;

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

	// Header: two team pics plus the title, unconditional -- the
	// original never guarded this against the 1000-byte cap (only the
	// row loop below does), so neither does this.
	header_actual_len = (int)strlen(teamOfInterest == 0 ?
		"xv 0 yv 0 picn redlion_i xv 32 yv 0 picn redtag " :
		"xv 0 yv 0 picn bluewolf_i xv 32 yv 0 picn bluetag ")
		+ (int)strlen("xv 48 yv 10 string \"Squad Board\" ");

	squad = 0;

	// Legacy-cap admission pass (Board_LineLen / this file's other
	// boards' banners): reproduces the original's per-row 1000-byte
	// (maxsize) budget check instead of letting ui_layout_compile's
	// bigger 1380-byte budget (UI_LAYOUT_BUDGET) admit rows the
	// original wouldn't have -- deliberately unspent headroom, not a
	// missed cap.
	//
	// Quirk carried over on purpose: the original's running length
	// ("len") is reset to 0 right after the header is written but is
	// never charged for the header's own bytes until the FIRST row
	// admission recomputes it via strlen(string) -- so the very first
	// row's admission check is tested against the full 1000-byte
	// budget, not (1000 - header bytes). Every check after that first
	// admission correctly includes the header. Reproduced exactly
	// below (legacy_admitted_once) rather than "fixed", since a fix
	// here would be a behavior change this pass does not make.
	//
	// numCategoryLines increments the moment a squad boundary is
	// crossed, whether or not that row ends up admitted -- a dropped
	// row's category header still consumes a line slot for every row
	// after it. Reproduced via row_header_y/row_status_y below, walked
	// once up front so admission and the resulting y coordinates use
	// the same slot numbering the original did.
	legacy_len = 0;
	legacy_admitted_once = false;

	row_count = (sortedCount < CTFSQUADBOARD_MAX_ROWS) ? sortedCount : CTFSQUADBOARD_MAX_ROWS;

	for (i = 0; i < row_count; i++)
	{
		row_has_header[i] = (!squad || Q_stricmp(squad, sortedClients[i]->pers.squad)) ? true : false;

		if (row_has_header[i])
		{
			squad = sortedClients[i]->pers.squad;
			row_header_y[i] = 42 + i * 8 + numCategoryLines * 8;
			numCategoryLines++;
		}
		else
		{
			row_header_y[i] = 0; // unused
		}

		row_status_y[i] = 42 + i * 8 + numCategoryLines * 8;

		strncpy(statusStart, sortedClients[i]->pers.squadStatus, greenStatusLen);
		statusStart[greenStatusLen] = 0;
		row_ready[i] = !Q_stricmp(statusStart, GREEN_STATUS_STR);

		Com_sprintf(status_text[i], UI_CELL_LEN, "   %-*s %s",
			widestName, sortedClients[i]->pers.netname,
			sortedClients[i]->pers.squadStatus);

		entry_len = (row_has_header[i] ?
				Board_LineLen("xv 0 yv %d string \"%s\" ", row_header_y[i], sortedClients[i]->pers.squad) : 0)
			+ Board_LineLen("xv 0 yv %d %s \"%s\" ", row_status_y[i],
				row_ready[i] ? "string2" : "string", status_text[i]);

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

	n = 0;

	elems[n].kind = UI_PIC;
	elems[n].u.pic.x = 0;
	elems[n].u.pic.y = 0;
	elems[n].u.pic.stat_driven = false;
	elems[n].u.pic.image.name = (teamOfInterest == 0) ? "redlion_i" : "bluewolf_i";
	n++;

	elems[n].kind = UI_PIC;
	elems[n].u.pic.x = 32;
	elems[n].u.pic.y = 0;
	elems[n].u.pic.stat_driven = false;
	elems[n].u.pic.image.name = (teamOfInterest == 0) ? "redtag" : "bluetag";
	n++;

	elems[n].kind = UI_TEXT;
	elems[n].u.text.x = 48;
	elems[n].u.text.y = 10;
	elems[n].u.text.text = "Squad Board";
	elems[n].u.text.highlight = false;
	n++;

	for (i = 0; i < row_count; i++)
	{
		if (!row_admitted[i])
			continue;

		if (row_has_header[i])
		{
			elems[n].kind = UI_TEXT;
			elems[n].u.text.x = 0;
			elems[n].u.text.y = row_header_y[i];
			elems[n].u.text.text = sortedClients[i]->pers.squad;
			elems[n].u.text.highlight = false;
			n++;
		}

		elems[n].kind = UI_TEXT;
		elems[n].u.text.x = 0;
		elems[n].u.text.y = row_status_y[i];
		elems[n].u.text.text = status_text[i];
		elems[n].u.text.highlight = row_ready[i];
		n++;
	}

	screen.elems = elems;
	screen.count = n;

	ui_buf_init(&sb, storage, sizeof(storage));
	dropped = ui_layout_compile(&screen, &sb);
	if (dropped > 0 || legacy_dropped > 0)
		gi.dprintf("CTFSquadboard: %d row(s) dropped by the legacy 1000-byte cap, %d element(s) by the layout budget\n",
			legacy_dropped, dropped);

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
// row, name plus four small integers plus the pickup total.
typedef struct
{
    edict_t *sorted[MAX_CLIENTS];
    int      count;
} statboard_rows_t;

static void Statboard_FillRow(void *userdata, int row, int num_columns,
    char cells[UI_TABLE_MAX_COLUMNS][UI_CELL_LEN])
{
    const statboard_rows_t *rows = (const statboard_rows_t *)userdata;
    edict_t     *cl_ent = rows->sorted[row];
    gclient_t   *cl = cl_ent->client;

    Com_sprintf(cells[0], UI_CELL_LEN, "%-15s %3i %2i %2i %2i %2i",
        cl->pers.netname,
        (int)stats_get(cl_ent, STATS_FRAGS),
        (int)stats_get(cl_ent, STATS_OFFENSE_CARRIER),
        (int)(stats_get(cl_ent, STATS_DEFENSE_FLAG) + stats_get(cl_ent, STATS_DEFENSE_BASE)),
        (int)stats_get(cl_ent, STATS_RETURNS),
        stats_pickup_total(cl_ent));

    (void)num_columns; // always 1 for this board
}

void StatboardMessage(edict_t* ent, edict_t* killer)
{
    char                storage[UI_LAYOUT_BUDGET];
    ui_buf_t            sb;
    ui_screen_t         screen;
    ui_elem_t           elems[4];  // 2 header pics + red table + blue table
    ui_table_col_t      column;
    statboard_rows_t    red_rows, blue_rows;

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

    int     n;
    int     dropped;
    int     legacy_len;        // mirrors the old stringlength accumulator, 1024 cap
    int     legacy_dropped;
    qboolean header_ok;
    int     header_len;
    int     line_len;
    char    cells[UI_TABLE_MAX_COLUMNS][UI_CELL_LEN];  // scratch for legacy-cap admission
    int     red_rows_shown;
    int     blue_rows_shown;

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

    red_rows.count = red;
    for (i = 0; i < red; i++)
        red_rows.sorted[i] = g_edicts + 1 + redsorted[i];

    blue_rows.count = blue;
    for (i = 0; i < blue; i++)
        blue_rows.sorted[i] = g_edicts + 1 + bluesorted[i];

    // Legacy-cap admission pass (Board_LineLen, see its banner): decides
    // how many rows survive the ORIGINAL 1024-byte cap before any of
    // this is handed to ui_layout_compile, whose own budget is bigger
    // (UI_LAYOUT_BUDGET, 1380) and would otherwise let more through.
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
    for (i = 0; i < red; i++)
    {
        Statboard_FillRow(&red_rows, i, 1, cells);
        line_len = Board_LineLen("xv %i yv %i string2 \"%s\" ", -91, 34 + 8 * i, cells[0]);
        if (legacy_len + line_len > 1024)
            break;
        legacy_len += line_len;
        red_rows_shown = i + 1;
    }

    blue_rows_shown = 0;
    for (i = 0; i < blue; i++)
    {
        Statboard_FillRow(&blue_rows, i, 1, cells);
        line_len = Board_LineLen("xv %i yv %i string2 \"%s\" ", 171, 34 + 8 * i, cells[0]);
        if (legacy_len + line_len > 1024)
            break;
        legacy_len += line_len;
        blue_rows_shown = i + 1;
    }

    legacy_dropped = (red - red_rows_shown) + (blue - blue_rows_shown);

    n = 0;

    if (header_ok)
    {
        elems[n].kind = UI_PIC;
        elems[n].u.pic.x = -102;
        elems[n].u.pic.y = -35;
        elems[n].u.pic.stat_driven = false;
        elems[n].u.pic.image.name = "pb";
        n++;

        elems[n].kind = UI_PIC;
        elems[n].u.pic.x = -102;
        elems[n].u.pic.y = -27;
        elems[n].u.pic.stat_driven = false;
        elems[n].u.pic.image.name = "pt";
        n++;
    }

    column.x_offset = 0;
    column.priority = 0;

    elems[n].kind = UI_TABLE;
    elems[n].u.table.x = -91;
    elems[n].u.table.y = 34;
    elems[n].u.table.row_dy = 8;
    elems[n].u.table.columns = &column;
    elems[n].u.table.num_columns = 1;
    elems[n].u.table.num_rows = red_rows_shown;
    elems[n].u.table.fill_row = Statboard_FillRow;
    elems[n].u.table.userdata = &red_rows;
    elems[n].u.table.highlight = true;
    elems[n].u.table.footer = NULL;
    elems[n].u.table.footer_x = 0;
    elems[n].u.table.footer_y = 0;
    elems[n].u.table.footer_highlight = false;
    n++;

    elems[n].kind = UI_TABLE;
    elems[n].u.table.x = 171;
    elems[n].u.table.y = 34;
    elems[n].u.table.row_dy = 8;
    elems[n].u.table.columns = &column;
    elems[n].u.table.num_columns = 1;
    elems[n].u.table.num_rows = blue_rows_shown;
    elems[n].u.table.fill_row = Statboard_FillRow;
    elems[n].u.table.userdata = &blue_rows;
    elems[n].u.table.highlight = true;
    elems[n].u.table.footer = NULL;
    elems[n].u.table.footer_x = 0;
    elems[n].u.table.footer_y = 0;
    elems[n].u.table.footer_highlight = false;
    n++;

    screen.elems = elems;
    screen.count = n;

    ui_buf_init(&sb, storage, sizeof(storage));
    dropped = ui_layout_compile(&screen, &sb);
    if (dropped > 0 || legacy_dropped > 0)
        gi.dprintf("Statboard: %d row(s) dropped by the legacy 1024-byte cap, %d by the layout budget\n",
            legacy_dropped, dropped);

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

/*
==================
TeamStatboardMessage

==================
*/
void TeamStatboardMessage(edict_t* ent, edict_t* killer)
{
    char                storage[UI_LAYOUT_BUDGET];
    ui_buf_t            sb;
    ui_screen_t         screen;
    // 2 header pics + 16 numeric string2 fields -- see the single
    // all-or-nothing admission block below.
    ui_elem_t           elems[18];
    char                numtext[16][16];

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

    int     n;
    int     dropped;
    int     block_len;
    qboolean block_ok;

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
    // The original built all eighteen tokens (2 pics + 16 numbers) into
    // one Com_sprintf and tested the WHOLE thing against the 1024-byte
    // cap as a single unit -- there is no per-player table here, only
    // team-wide sums, so it is one atomic block, not rows. Reproduced
    // below as one all-or-nothing admission decision (Board_LineLen,
    // see its banner) instead of letting ui_layout_compile's larger
    // 1380-byte budget (UI_LAYOUT_BUDGET) admit it when the original
    // wouldn't have -- that headroom is deliberately unspent this pass.
    Com_sprintf(numtext[0],  sizeof(numtext[0]),  "%i", red_item_quad);
    Com_sprintf(numtext[1],  sizeof(numtext[1]),  "%i", red_item_shield);
    Com_sprintf(numtext[2],  sizeof(numtext[2]),  "%i", red_item_armor);
    Com_sprintf(numtext[3],  sizeof(numtext[3]),  "%i", red_item_mega);
    Com_sprintf(numtext[4],  sizeof(numtext[4]),  "%i", blue_item_quad);
    Com_sprintf(numtext[5],  sizeof(numtext[5]),  "%i", blue_item_shield);
    Com_sprintf(numtext[6],  sizeof(numtext[6]),  "%i", blue_item_armor);
    Com_sprintf(numtext[7],  sizeof(numtext[7]),  "%i", blue_item_mega);
    Com_sprintf(numtext[8],  sizeof(numtext[8]),  "%i", red_rune_strength);
    Com_sprintf(numtext[9],  sizeof(numtext[9]),  "%i", red_rune_haste);
    Com_sprintf(numtext[10], sizeof(numtext[10]), "%i", red_rune_resist);
    Com_sprintf(numtext[11], sizeof(numtext[11]), "%i", red_rune_regen);
    Com_sprintf(numtext[12], sizeof(numtext[12]), "%i", blue_rune_strength);
    Com_sprintf(numtext[13], sizeof(numtext[13]), "%i", blue_rune_haste);
    Com_sprintf(numtext[14], sizeof(numtext[14]), "%i", blue_rune_resist);
    Com_sprintf(numtext[15], sizeof(numtext[15]), "%i", blue_rune_regen);

    block_len = Board_LineLen("xv %i yv %i picn %s ", -102, -35, "tb")
              + Board_LineLen("xv %i yv %i picn %s ", -102, -27, "tt")
              + Board_LineLen("xv %i yv %i string2 \"%s\" ", -54, -19, numtext[0])
              + Board_LineLen("xv %i yv %i string2 \"%s\" ", -54, -10, numtext[1])
              + Board_LineLen("xv %i yv %i string2 \"%s\" ", -54, -1,  numtext[2])
              + Board_LineLen("xv %i yv %i string2 \"%s\" ", -54, 8,   numtext[3])
              + Board_LineLen("xv %i yv %i string2 \"%s\" ", 209, -19, numtext[4])
              + Board_LineLen("xv %i yv %i string2 \"%s\" ", 209, -10, numtext[5])
              + Board_LineLen("xv %i yv %i string2 \"%s\" ", 209, -1,  numtext[6])
              + Board_LineLen("xv %i yv %i string2 \"%s\" ", 209, 8,   numtext[7])
              + Board_LineLen("xv %i yv %i string2 \"%s\" ", 10,  -19, numtext[8])
              + Board_LineLen("xv %i yv %i string2 \"%s\" ", 10,  -10, numtext[9])
              + Board_LineLen("xv %i yv %i string2 \"%s\" ", 10,  -1,  numtext[10])
              + Board_LineLen("xv %i yv %i string2 \"%s\" ", 10,  8,   numtext[11])
              + Board_LineLen("xv %i yv %i string2 \"%s\" ", 273, -19, numtext[12])
              + Board_LineLen("xv %i yv %i string2 \"%s\" ", 273, -10, numtext[13])
              + Board_LineLen("xv %i yv %i string2 \"%s\" ", 273, -1,  numtext[14])
              + Board_LineLen("xv %i yv %i string2 \"%s\" ", 273, 8,   numtext[15]);
    block_ok = (block_len < 1024);

    n = 0;
    if (block_ok)
    {
        static const struct { int x, y; } pic_pos[2]  = { { -102, -35 }, { -102, -27 } };
        static const char * const pic_name[2]         = { "tb", "tt" };
        static const struct { int x, y; } num_pos[16] =
        {
            { -54, -19 }, { -54, -10 }, { -54, -1 }, { -54, 8 },
            { 209, -19 }, { 209, -10 }, { 209, -1 }, { 209, 8 },
            { 10,  -19 }, { 10,  -10 }, { 10,  -1 }, { 10,  8 },
            { 273, -19 }, { 273, -10 }, { 273, -1 }, { 273, 8 },
        };

        for (i = 0; i < 2; i++)
        {
            elems[n].kind = UI_PIC;
            elems[n].u.pic.x = pic_pos[i].x;
            elems[n].u.pic.y = pic_pos[i].y;
            elems[n].u.pic.stat_driven = false;
            elems[n].u.pic.image.name = pic_name[i];
            n++;
        }

        for (i = 0; i < 16; i++)
        {
            elems[n].kind = UI_TEXT;
            elems[n].u.text.x = num_pos[i].x;
            elems[n].u.text.y = num_pos[i].y;
            elems[n].u.text.text = numtext[i];
            elems[n].u.text.highlight = true;
            n++;
        }
    }

    screen.elems = elems;
    screen.count = n;

    ui_buf_init(&sb, storage, sizeof(storage));
    dropped = ui_layout_compile(&screen, &sb);
    if (dropped > 0 || !block_ok)
        gi.dprintf("TeamStatboard: header block %sdropped by the legacy 1024-byte cap, layout budget dropped %d\n",
            block_ok ? "not " : "", dropped);

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
    edict_t *sorted[MAX_CLIENTS];
    int      count;
} railboard_rows_t;

static void Railboard_FillRow(void *userdata, int row, int num_columns,
    char cells[UI_TABLE_MAX_COLUMNS][UI_CELL_LEN])
{
    const railboard_rows_t *rows = (const railboard_rows_t *)userdata;
    edict_t     *cl_ent = rows->sorted[row];
    gclient_t   *cl = cl_ent->client;
    long        shot, hit, kill;

    shot = stats_get(cl_ent, STATS_RAIL_SHOT);
    hit  = stats_get(cl_ent, STATS_RAIL_HIT);
    kill = stats_get(cl_ent, STATS_RAIL_KILL);

    // went straight through p_stats_player, which is NULL for a client
    // that has not finished connecting. stats_get guards it.
    Com_sprintf(cells[0], UI_CELL_LEN, "%-15s %2i %2i %3i %3i",
        cl->pers.netname,
        (int)kill,
        (int)hit,
        (int)shot,
        shot == 0 ? 0 : (int)(100 * hit / shot));

    (void)num_columns; // always 1 for this board
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
    ui_screen_t         screen;
    ui_elem_t           elems[3];
    ui_table_col_t      column;
    railboard_rows_t    rows;

    int     i;
    int     j;      // sort index; int so the
                    // comparisons against k stay signed
    int     k;
    int     player = 0;
    int     rails = 0;
    int     playersorted[MAX_CLIENTS];
    int     playersortedrails[MAX_CLIENTS];
    int     dropped;

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

    rows.count = player;
    for (i = 0; i < player; i++)
        rows.sorted[i] = g_edicts + 1 + playersorted[i];

    // header: the two rail-board pics, unconditional
    elems[0].kind = UI_PIC;
    elems[0].u.pic.x = 29;
    elems[0].u.pic.y = -35;
    elems[0].u.pic.stat_driven = false;
    elems[0].u.pic.image.name = "rb";

    elems[1].kind = UI_PIC;
    elems[1].u.pic.x = 41;
    elems[1].u.pic.y = -26;
    elems[1].u.pic.stat_driven = false;
    elems[1].u.pic.image.name = "rt";

    // one highlighted row per sorted player, one packed column each --
    // reproduces the original's single string2 token per row.
    column.x_offset = 0;
    column.priority = 0;

    elems[2].kind = UI_TABLE;
    elems[2].u.table.x = 40;
    elems[2].u.table.y = -18;
    elems[2].u.table.row_dy = 8;
    elems[2].u.table.columns = &column;
    elems[2].u.table.num_columns = 1;
    elems[2].u.table.num_rows = rows.count;
    elems[2].u.table.fill_row = Railboard_FillRow;
    elems[2].u.table.userdata = &rows;
    elems[2].u.table.highlight = true;
    elems[2].u.table.footer = NULL;
    elems[2].u.table.footer_x = 0;
    elems[2].u.table.footer_y = 0;
    elems[2].u.table.footer_highlight = false;

    screen.elems = elems;
    screen.count = 3;

    ui_buf_init(&sb, storage, sizeof(storage));
    dropped = ui_layout_compile(&screen, &sb);
    if (dropped > 0)
        gi.dprintf("Railboard: %d row(s) dropped by the layout budget\n", dropped);

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
            || ent->client->showteamstatboard || ent->client->showrailboard) // ADC //BUZZKILL
            ent->client->ps.stats[STAT_LAYOUTS] |= 1;
        if (ent->client->showinventory && ent->client->pers.health > 0)
            ent->client->ps.stats[STAT_LAYOUTS] |= 2;
    }
    else
    {
        if (ent->client->showscores || ent->client->showhelp
            || ent->client->showctfhud || ent->client->showmod
			|| ent->client->showsquadboard || ent->client->showstatboard
            || ent->client->showteamstatboard || ent->client->showrailboard) // ADC // BUZZKILL
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
	
	if(level.intermissiontime || cl->showscores || cl->showmenu || cl->showrailboard || cl->showstatboard || cl->showteamstatboard)
		cl->ps.stats[STAT_LAYOUTS] |= 1;
	if (cl->showinventory && cl->pers.health > 0)
		cl->ps.stats[STAT_LAYOUTS] |= 2;

	if (cl->chase_target && cl->chase_target->inuse)
		cl->ps.stats[STAT_CHASE] = CS_PLAYERSKINS + 
			(cl->chase_target - g_edicts) - 1;
	else
		cl->ps.stats[STAT_CHASE] = 0;
}

