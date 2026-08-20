/* sg_tilt.c -- short-lived opponent pressure not represented by danger. */
#include "g_local.h"
#include "g_ctffunc.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_cvars.h"
#include "slipgate/sg_bot.h"
#include "slipgate/sg_tilt.h"
#include "slipgate/sg_util.h"
#include "slipgate/sg_hooks.h"



/* tuning constants live in sg_tilt.h */

/*
 * The BFS scratch: one byte per seed, TAG_LEVEL like the rune it indexes,
 * allocated on the first death of the level and freed with it. It is shared
 * because it never outlives a single Tilt_Lane call -- the lane it produces
 * is what gets kept, per bot.
 */
static unsigned char *sg_tilt_mark;


void Tilt_Lane(sg_bot_t *bot, int seed)
{
	rune_t	*r = SG_Rune();
	int		hop, li, n = 0;

	bot->tilt_lane_n = 0;
	if (!r || !r->links || seed < 0 || seed >= r->hdr.num_seeds)
		return;
	if (!sg_tilt_mark)
		sg_tilt_mark = sg_host.level_alloc(r->hdr.num_seeds);
	if (!sg_tilt_mark)
		return;

	/* mark holds distance+1, so zero can keep meaning "not reached" */
	memset(sg_tilt_mark, 0, r->hdr.num_seeds);
	sg_tilt_mark[seed] = 1;
	bot->tilt_lane[n++] = seed;

	for (hop = 1; hop <= 2 && n < SG_TILT_LANE; hop++)
	{
		for (li = 0; li < r->hdr.num_links && n < SG_TILT_LANE; li++)
		{
			rune_link_t	*l = &r->links[li];
			int			a = l->from, b = l->to;

			if (a < 0 || b < 0 ||
			    a >= r->hdr.num_seeds || b >= r->hdr.num_seeds)
				continue;
			/* a node marked THIS pass reads hop+1 and is therefore not
			 * expanded again until the next one: level order, one pass
			 * per level, no queue */
			if (sg_tilt_mark[a] == hop && !sg_tilt_mark[b])
			{
				sg_tilt_mark[b] = (unsigned char)(hop + 1);
				bot->tilt_lane[n++] = b;
			}
			else if (sg_tilt_mark[b] == hop && !sg_tilt_mark[a])
			{
				sg_tilt_mark[a] = (unsigned char)(hop + 1);
				bot->tilt_lane[n++] = a;
			}
		}
	}
	bot->tilt_lane_n = n;
}

qboolean Tilt_InLane(const sg_bot_t *bot, int seed)
{
	int i;

	for (i = 0; i < bot->tilt_lane_n; i++)
		if (bot->tilt_lane[i] == seed)
			return true;
	return false;
}


void Tilt_Note(edict_t *e, sg_bot_t *bot)
{
	float	window = SG_TILT_WINDOW, best = -1.0f;
	int		team = e->client->ctf.teamnum;
	int		ci, k, killer = -1;

	if (sg_cv.tilt->value <= 0.0f)
		return;

	ci = (int)(e->client - game.clients);
	if (ci >= 0 && ci < SG_DMG_CLIENTS)
		for (k = 0; k < SG_DMG_RING; k++)
			if (sg_caco_damage[ci][k].attacker >= 0 &&
			    sg_caco_damage[ci][k].time > best)
			{
				best = sg_caco_damage[ci][k].time;
				killer = sg_caco_damage[ci][k].attacker;
			}

	bot->tilt_killer_seed = -1;
	if (killer >= 0 && (team == CTF_TEAM_RED || team == CTF_TEAM_BLUE))
		for (k = 0; k < SG_MAX_ENEMY_TRACK; k++)
			if (sg_caco_enemies[SG_TeamIdx(team)][k].client == killer)
			{
				bot->tilt_killer_seed = sg_caco_enemies[SG_TeamIdx(team)][k].seed;
				break;
			}

	Tilt_Lane(bot, bot->seed);

	/*
	 * REPEAT-DEATH ESCALATION. Twice in the same lane inside a minute is
	 * not bad luck, it is somebody sitting there. The test is the new
	 * lane against the OLD death seed -- the lane was just built around
	 * where this life ended, so asking whether the previous death is
	 * inside it is exactly "within two links of each other" without
	 * building a second neighbourhood to compare.
	 */
	if (bot->tilt_seed >= 0 &&
	    SG_AgeUnder(bot->tilt_death_time, SG_TILT_REPEAT) &&
	    Tilt_InLane(bot, bot->tilt_seed))
		window *= 2.0f;

	bot->tilt_seed = bot->seed;
	SG_Mark(&bot->tilt_death_time);
	bot->tilt_window = window;
	/* the windows themselves are ARMED at respawn, not here: the clock a
	 * human runs on is "the first N seconds of the next life", and the
	 * corpse's second and a half on the floor is not part of it */

	if (sg_cv.debug->value)
		sg_host.dprint("TILT %s died seed=%d lane=%d killer=%d kseed=%d "
		           "window=%.0f%s\n",
		           e->client->pers.netname, bot->tilt_seed,
		           bot->tilt_lane_n, killer, bot->tilt_killer_seed,
		           window, window > SG_TILT_WINDOW ? " REPEAT" : "");
}


float SG_TiltCaution(edict_t *ent)
{
	int i;

	if (sg_cv.tilt->value <= 0.0f)
		return 1.0f;
	for (i = 0; i < SG_MAXBOTS; i++)
		if (sg_bots[i].active && sg_bots[i].ent == ent)
			return SG_TimerPending(sg_bots[i].tilt_caution_until)
			       ? SG_TILT_ENGAGE : 1.0f;
	return 1.0f;
}

/* TAG_LEVEL memory dies with the level; forget the pointer with it */
void Tilt_LevelReset(void)
{
	sg_tilt_mark = NULL;
}
