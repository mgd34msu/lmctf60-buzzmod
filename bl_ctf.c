/*
 * bl_ctf.c -- CTF integration for the bots.
 *
 * See bl_ctf.h for why this exists.
 */
#include "g_local.h"
#include "g_ctffunc.h"
#include "bl_main.h"
#include "bl_botcfg.h"
#include "bl_redirgi.h"	/* BotServerCommand */
#include "bl_spawn.h"	/* BotDestroy */
#include "bl_ctf.h"

/*
 * The bot glue reads its settings with gi.cvar() at the point of use rather
 * than caching cvar_t pointers, so these do the same. Registering here means
 * the cvars exist (and show up in a "cvarlist") from the first frame.
 */
static cvar_t *bot_ctfteam(void)  { return gi.cvar("botctfteam", "0", 0); }
static cvar_t *bot_statscvar(void) { return gi.cvar("bot_stats", "0", 0); }

qboolean BotStatsEnabled(void)
{
	cvar_t *c = bot_statscvar();
	return (c && c->value) ? true : false;
}

int BotCTFTeamSize(int team)
{
	int i, count = 0;
	edict_t *cl_ent;

	for (i = 0; i < game.maxclients; i++)
	{
		cl_ent = g_edicts + 1 + i;
		if (!cl_ent->inuse || !cl_ent->client)
			continue;
		if (cl_ent->client->ctf.teamnum == team)
			count++;
	}
	return count;
}

int BotCountInGame(void)
{
	int i, count = 0;
	edict_t *cl_ent;

	for (i = 0; i < game.maxclients; i++)
	{
		cl_ent = g_edicts + 1 + i;
		if (cl_ent->inuse && (cl_ent->flags & FL_BOT))
			count++;
	}
	return count;
}

edict_t *BotByListIndex(int n)
{
	int i, count = 0;
	edict_t *cl_ent;

	if (n < 0)
		return NULL;

	for (i = 0; i < game.maxclients; i++)
	{
		cl_ent = g_edicts + 1 + i;
		if (!cl_ent->inuse || !(cl_ent->flags & FL_BOT))
			continue;
		if (count == n)
			return cl_ent;
		count++;
	}
	return NULL;
}

void BotCTFAssignTeam(edict_t *bot)
{
	cvar_t *want;
	int team;

	if (!bot || !bot->client)
		return;

	want = bot_ctfteam();
	team = want ? (int)want->value : 0;

	/*
	 * Anything that is not red or blue means "let the mod decide". Leaving
	 * teamnum at CTF_TEAM_UNDEFINED is what makes ClientBegin call TeamJoin,
	 * which uses LMCTF's own Team_To_Join balancing -- better than anything
	 * this file could reinvent.
	 */
	if (team != CTF_TEAM_RED && team != CTF_TEAM_BLUE)
		return;

	/*
	 * Written directly rather than through ctf_SetEntTeamEx because this runs
	 * with inuse still false: ctf_SetEntTeamEx sets teamnum and then bails out
	 * of its announce/logging tail on exactly that condition, so the direct
	 * write has the same effect with less indirection. ClientBegin then sees a
	 * client that is already on a team and keeps it, penalty-free.
	 */
	bot->client->ctf.teamnum = team;
	if (bot->client->p_stats_player)
		bot->client->p_stats_player->info.teamnum = team;
}

int BotRemoveAll(void)
{
	int i, removed = 0;
	edict_t *cl_ent;

	/*
	 * Walked back to front: BotDestroy frees the client edict, and the glue
	 * may move a bot down into a freed slot, which would let a forward walk
	 * skip one.
	 */
	for (i = game.maxclients - 1; i >= 0; i--)
	{
		cl_ent = g_edicts + 1 + i;
		if (!cl_ent->inuse || !(cl_ent->flags & FL_BOT))
			continue;
		BotDestroy(cl_ent);
		removed++;
	}
	return removed;
}

qboolean BotAddNamed(char *name)
{
	bot_t *bot;

	if (!name || !*name)
		return false;

	bot = FindBotWithName(name);
	if (!bot)
		return false;

	BotServerCommand("sv", "addbot", bot->name, bot->skin,
	                 bot->charfile, bot->charname, NULL);
	return true;
}
