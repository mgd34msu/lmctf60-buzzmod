/*
 * sg_tilt.c -- tilt: the grudge the danger dimension cannot hold.
 * Moved verbatim from sg_arach.c in the 2026-08-11 standards pass.
 */
#include "g_local.h"
#include "g_ctffunc.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_cvars.h"
#include "slipgate/sg_bot.h"
#include "slipgate/sg_tilt.h"
#include "slipgate/sg_util.h"
#include "slipgate/sg_hooks.h"

/* ------------------------------------------------------------------- tilt
 *
 * TILT: the grudge the danger dimension cannot hold.
 *
 * Danger is the TEAM's ledger and it is slow on purpose -- one death adds
 * 1200ms to one seed, decayed a percent a second, persisted across matches.
 * That is the map's reputation, and it should be slow. It is not what a
 * human does thirty seconds after being railed in the same doorway twice.
 *
 * What a human does is personal, short and lopsided: he comes out of the
 * respawn, looks at the door he just died in, and takes the other way --
 * not because the arithmetic changed but because he remembers. He plays the
 * next twenty seconds a shade smaller than the twenty before them, and if
 * the same lane kills him twice inside a minute he stays away from it twice
 * as long. Then it wears off and he is himself again.
 *
 * Three things this is NOT, stated here because every one of them is the
 * obvious next step and every one of them is wrong:
 *
 *   - it is not aim. Tilt never touches the gun. A rattled player is not a
 *     worse SHOT in any way this tree is willing to model; he goes to
 *     different places and starts fewer fights. Skill is the shooter's,
 *     tilt is the router's, and the two do not meet.
 *   - it is not danger. It never writes sg_danger, never persists, and dies
 *     with the level. A grudge that outlives the match is a weight, not a
 *     grudge.
 *   - it is not permanent. Every term here has an expiry measured in
 *     seconds. A bot that permanently refuses a lane has been lobotomised,
 *     not humanised -- and on a two-lane map it would simply stop playing.
 *
 * Off by default (sg_tilt 0): nothing is recorded, nothing is priced, and
 * the surface is the byte it was before this existed.
 */

/* tuning constants live in sg_tilt.h */

/*
 * The BFS scratch: one byte per seed, TAG_LEVEL like the rune it indexes,
 * allocated on the first death of the level and freed with it. It is shared
 * because it never outlives a single Tilt_Lane call -- the lane it produces
 * is what gets kept, per bot.
 */
static unsigned char *sg_tilt_mark;

/*
 * "Within two links" of a seed, the undirected way.
 *
 * The rune's adjacency index (first_link/next_link) runs OUTWARD only, and
 * outward alone is the wrong neighbourhood here: a bot walking INTO the
 * doorway it died in arrives on seeds that reach the death seed, which the
 * outgoing index cannot name. Most links are proven in both directions and
 * the two sets nearly coincide -- but "nearly" is how a bot ends up
 * strolling back through its own killer's sightline, so this reads both
 * endpoints of every link and treats the graph as undirected.
 *
 * Two whole passes over the link array, at most, and only when somebody
 * dies -- a few tens of thousands of comparisons a death, against a body
 * that just stopped playing for a second and a half. The lane is capped at
 * SG_TILT_LANE seeds: a two-hop ball on a dense map can be larger, and the
 * truncation is by link order rather than by distance, which is honest
 * enough for a preference and keeps the per-candidate membership test a
 * short linear walk instead of another allocation.
 */
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

/*
 * The death note. Called from the one place that already knows a bot has
 * died at a seed it still remembers -- beside Danger_Learn, before the
 * corpse's seed is dropped.
 *
 * Who did it comes from the bot's OWN damage ring (sg_caco.c): the freshest
 * entry in it is the last thing that hurt this body, which is the killer in
 * every case that is not a fall or a lava pool -- and for those the ring is
 * empty and the killer stays unnamed, which is correct. Where he was comes
 * from the enemy belief table and nowhere else: the ring names a client,
 * belief places him, and if belief cannot, the lane is still the lane. The
 * killer's seed is recorded for the debug line and for whatever reads it
 * later; the aversion itself is about the GROUND, not the man.
 */
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

/*
 * POST-DEATH CAUTION, the part combat has to know about: how far out this
 * bot is willing to start something, as a factor on whatever the persona
 * already decided. One number, read by sg_combat.c's target scan, and it is
 * 1.0 for every bot that is not a SLIPGATE bot inside its own caution
 * window -- including every legacy bot and every human, who have no window
 * and no entry in this table.
 *
 * The willingness is the whole of it. Aim, reaction, trigger cadence and
 * lead are the skill model's and tilt does not touch them: a player who
 * just died shoots exactly as well as he did a minute ago and merely picks
 * fewer fights to shoot in.
 */
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
