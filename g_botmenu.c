/*
 * g_botmenu.c -- bot management from the referee menu.
 *
 * The bot glue shipped with a menu of its own (p_botmenu.c), built on the
 * Gladiator/Rocket Arena menu library and reaching for Rocket Arena arenas and
 * the two mission packs for half its items. None of that exists here, and this
 * mod already has a perfectly good menu system in g_menu.c with a referee tree
 * on top of it -- so the useful items from that menu are rebuilt here against
 * LMCTF's own primitives and its own team model.
 *
 * Selection works the way Ref_Kick_Menu does it: the framework hands a callback
 * only the entity, so list items carry a leading index in their text and the
 * shared handler reads it back out of localmenu[menuselect].
 */
#include "g_local.h"
#include "g_menu.h"
#include "g_ctffunc.h"
#include "bl_ctf.h"
#include "bl_botcfg.h"
#include "bl_spawn.h"

void Ref_Main_Menu(edict_t *ent);
void Ref_Bot_Menu(edict_t *ent);
void Ref_BotAdd_Menu(edict_t *ent);
void Ref_BotRemove_Menu(edict_t *ent);

/* first item of the add-bot list currently on screen, per client */
static int botmenu_first[MAX_CLIENTS];

#define BOTMENU_LAST	17	/* localmenu is [18]; Menu_Draw walks 0..17 */

static int BotMenu_ClientNum(edict_t *ent)
{
	int n = (int)(ent - g_edicts) - 1;
	if (n < 0 || n >= MAX_CLIENTS)
		return 0;
	return n;
}

/* ------------------------------------------------------------------ toggles */

static void BotMenu_Redraw(edict_t *ent, int keep)
{
	Ref_Bot_Menu(ent);
	/*
	 * Ref_Bot_Menu resets the cursor to the top. A toggle should leave it on
	 * the item that was just toggled, so put it back before the draw lands.
	 */
	if (keep > 0 && keep <= BOTMENU_LAST && ent->client->localmenu[keep].func)
	{
		ent->client->menuselect = keep;
		ent->client->menumovetime = 0;	/* let Menu_Draw run again this frame */
		Menu_Draw(ent);
	}
}

static void Bot_CycleTeam_Exec(edict_t *ent)
{
	int team = (int)gi.cvar("botctfteam", "0", 0)->value;

	/*
	 * auto -> red -> blue -> auto. The values line up with LMCTF's own
	 * CTF_TEAM_RED (1) and CTF_TEAM_BLUE (2), so the cvar needs no mapping.
	 */
	if (team == CTF_TEAM_RED)
		gi.cvar_set("botctfteam", "2");
	else if (team == CTF_TEAM_BLUE)
		gi.cvar_set("botctfteam", "0");
	else
		gi.cvar_set("botctfteam", "1");

	BotMenu_Redraw(ent, ent->client->menuselect);
}

static void Bot_CycleSkill_Exec(edict_t *ent)
{
	int skill = (int)gi.cvar("bot_skill", "4", 0)->value;
	char buf[16];

	skill++;
	if (skill < 1 || skill > 5)
		skill = 1;
	Com_sprintf(buf, sizeof buf, "%d", skill);
	gi.cvar_set("bot_skill", buf);

	BotMenu_Redraw(ent, ent->client->menuselect);
}

static void Bot_CycleFill_Exec(edict_t *ent)
{
	int fill = (int)gi.cvar("minimumplayers", "0", 0)->value;
	char buf[16];

	/* off, then 2 up to 16 in twos -- CTF wants even numbers */
	if (fill <= 0)
		fill = 2;
	else if (fill >= 16)
		fill = 0;
	else
		fill += 2;

	Com_sprintf(buf, sizeof buf, "%d", fill);
	gi.cvar_set("minimumplayers", buf);

	BotMenu_Redraw(ent, ent->client->menuselect);
}

static void Bot_ToggleChat_Exec(edict_t *ent)
{
	/* the glue's cvar is "nochat", so the menu shows the inverse */
	gi.cvar_set("nochat", gi.cvar("nochat", "0", 0)->value ? "0" : "1");
	BotMenu_Redraw(ent, ent->client->menuselect);
}

static void Bot_ToggleStats_Exec(edict_t *ent)
{
	gi.cvar_set("bot_stats", BotStatsEnabled() ? "0" : "1");
	BotMenu_Redraw(ent, ent->client->menuselect);
}

/* ------------------------------------------------------------- add / remove */

static void Bot_AddRandom_Exec(edict_t *ent)
{
	if (!AddRandomBot(ent))
		gi.cprintf(ent, PRINT_HIGH, "No bot available to add.\n");
	BotMenu_Redraw(ent, ent->client->menuselect);
}

static void Bot_RemoveAll_Exec(edict_t *ent)
{
	int n = BotRemoveAll();

	if (n)
		gi.bprintf(PRINT_HIGH, "%s removed %d bot%s\n",
			ent->client->pers.netname, n, n == 1 ? "" : "s");
	else
		gi.cprintf(ent, PRINT_HIGH, "No bots in the game.\n");

	BotMenu_Redraw(ent, ent->client->menuselect);
}

static void SelectAddBot(edict_t *ent)
{
	int idx, i;
	bot_t *bot;

	if (sscanf(ent->client->localmenu[ent->client->menuselect].text, "%d", &idx) != 1)
		return;

	for (i = 0, bot = botlist; bot && i < idx; bot = bot->next, i++)
		;
	if (!bot)
		return;

	if (!BotAddNamed(bot->name))
		gi.cprintf(ent, PRINT_HIGH, "Could not add %s.\n", bot->name);

	Ref_BotAdd_Menu(ent);
}

static void SelectRemoveBot(edict_t *ent)
{
	int idx;
	edict_t *bot;

	if (sscanf(ent->client->localmenu[ent->client->menuselect].text, "%d", &idx) != 1)
		return;

	bot = BotByListIndex(idx);
	if (!bot)
		return;

	gi.bprintf(PRINT_HIGH, "%s removed %s\n",
		ent->client->pers.netname, bot->client->pers.netname);
	BotDestroy(bot);

	Ref_BotRemove_Menu(ent);
}

static void BotAdd_Page_Exec(edict_t *ent)
{
	int n = BotMenu_ClientNum(ent);
	int total = 0;
	bot_t *bot;

	for (bot = botlist; bot; bot = bot->next)
		total++;

	botmenu_first[n] += 14;
	if (botmenu_first[n] >= total)
		botmenu_first[n] = 0;		/* wrap back to the start */

	Ref_BotAdd_Menu(ent);
}

/* ------------------------------------------------------------------- menus */

void Ref_BotAdd_Menu(edict_t *ent)
{
	char text[MAX_INFO_STRING];
	int n = BotMenu_ClientNum(ent);
	int i, idx, total = 0;
	bot_t *bot;

	for (bot = botlist; bot; bot = bot->next)
		total++;

	if (botmenu_first[n] >= total)
		botmenu_first[n] = 0;

	Menu_Free(ent);
	ent->client->menu = MENU_LOCAL;
	ent->client->menuselect = 0;

	Menu_Set(ent, 0, "LMCTF Add Bot", Ref_Bot_Menu);
	Menu_Set(ent, 1, "-------------", NULL);

	if (!total)
	{
		Menu_Set(ent, 3, "No bot file loaded.", NULL);
		Menu_Set(ent, 4, "Check the 'botfile' cvar.", NULL);
		Menu_Set(ent, 6, "Back", Ref_Bot_Menu);
		Menu_Draw(ent);
		return;
	}

	/* skip to the first entry on this page */
	for (idx = 0, bot = botlist; bot && idx < botmenu_first[n]; bot = bot->next, idx++)
		;

	i = 2;
	while (bot && i < 16)
	{
		Com_sprintf(text, sizeof text, "%d %s", idx, bot->name);
		Menu_Set(ent, i, text, SelectAddBot);
		bot = bot->next;
		idx++;
		i++;
	}

	if (total > 14)
	{
		Com_sprintf(text, sizeof text, "More... (%d-%d of %d)",
			botmenu_first[n] + 1, idx, total);
		Menu_Set(ent, 16, text, BotAdd_Page_Exec);
	}
	Menu_Set(ent, BOTMENU_LAST, "Back", Ref_Bot_Menu);

	Menu_Draw(ent);
}

void Ref_BotRemove_Menu(edict_t *ent)
{
	char text[MAX_INFO_STRING];
	int i, idx;
	edict_t *bot;

	Menu_Free(ent);
	ent->client->menu = MENU_LOCAL;
	ent->client->menuselect = 0;

	Menu_Set(ent, 0, "LMCTF Remove Bot", Ref_Bot_Menu);
	Menu_Set(ent, 1, "----------------", NULL);

	i = 2;
	idx = 0;
	while ((bot = BotByListIndex(idx)) != NULL && i < BOTMENU_LAST)
	{
		char *team;

		if (bot->client->ctf.teamnum == CTF_TEAM_RED)
			team = "red";
		else if (bot->client->ctf.teamnum == CTF_TEAM_BLUE)
			team = "blue";
		else
			team = "-";

		Com_sprintf(text, sizeof text, "%d %-16s %s", idx,
			bot->client->pers.netname, team);
		Menu_Set(ent, i, text, SelectRemoveBot);
		idx++;
		i++;
	}

	if (!idx)
		Menu_Set(ent, 3, "No bots in the game.", NULL);

	Menu_Set(ent, BOTMENU_LAST, "Back", Ref_Bot_Menu);

	Menu_Draw(ent);
}

void Ref_Bot_Menu(edict_t *ent)
{
	char text[MAX_INFO_STRING];
	int team, fill;

	Menu_Free(ent);
	ent->client->menu = MENU_LOCAL;
	ent->client->menuselect = 0;

	Menu_Set(ent, 0, "LMCTF Bot Menu", Ref_Main_Menu);
	Menu_Set(ent, 1, "--------------", NULL);

	Menu_Set(ent, 2, "Add Bot", Ref_BotAdd_Menu);
	Menu_Set(ent, 3, "Add Random Bot", Bot_AddRandom_Exec);
	Menu_Set(ent, 4, "Remove Bot", Ref_BotRemove_Menu);
	Menu_Set(ent, 5, "Remove All Bots", Bot_RemoveAll_Exec);

	Com_sprintf(text, sizeof text, "In game: %d   red %d  blue %d",
		BotCountInGame(), BotCTFTeamSize(CTF_TEAM_RED),
		BotCTFTeamSize(CTF_TEAM_BLUE));
	Menu_Set(ent, 7, text, NULL);

	team = (int)gi.cvar("botctfteam", "0", 0)->value;
	Com_sprintf(text, sizeof text, "Join team:    %s",
		team == CTF_TEAM_RED ? "red" :
		team == CTF_TEAM_BLUE ? "blue" : "auto");
	Menu_Set(ent, 9, text, Bot_CycleTeam_Exec);

	Com_sprintf(text, sizeof text, "Skill:        %d",
		(int)gi.cvar("bot_skill", "4", 0)->value);
	Menu_Set(ent, 10, text, Bot_CycleSkill_Exec);

	fill = (int)gi.cvar("minimumplayers", "0", 0)->value;
	if (fill > 0)
		Com_sprintf(text, sizeof text, "Fill to:      %d players", fill);
	else
		Com_sprintf(text, sizeof text, "Fill to:      off");
	Menu_Set(ent, 11, text, Bot_CycleFill_Exec);

	Com_sprintf(text, sizeof text, "Chat:         %s",
		gi.cvar("nochat", "0", 0)->value ? "off" : "on");
	Menu_Set(ent, 12, text, Bot_ToggleChat_Exec);

	Com_sprintf(text, sizeof text, "Track stats:  %s",
		BotStatsEnabled() ? "on" : "off");
	Menu_Set(ent, 13, text, Bot_ToggleStats_Exec);

	Menu_Set(ent, 15, "Back", Ref_Main_Menu);

	Menu_Draw(ent);
}
