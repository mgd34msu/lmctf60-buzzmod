/*
 * sg_descend.c -- the descent and the commitment: the candidate walk
 * over every proven link, and everything between the argmin and the
 * aim.  Moved verbatim from sg_arach.c in the 2026-08-12 standards
 * pass; Duel_Price and the bearing bucket are private to it.
 */
#include "g_local.h"
#include "g_ctffunc.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_combat.h"
#include "slipgate/sg_chat.h"
#include "slipgate/sg_persona.h"
#include "slipgate/sg_net.h"
#include "slipgate/sg_cvars.h"
#include "slipgate/sg_util.h"
#include "slipgate/sg_bot.h"
#include "slipgate/sg_clock.h"
#include "slipgate/sg_danger.h"
#include "slipgate/sg_tilt.h"
#include "slipgate/sg_lead.h"
#include "slipgate/sg_price.h"
#include "slipgate/sg_descend.h"
#include "slipgate/sg_goal.h"      /* sg_grab_time, sg_push_until */
#include "slipgate/sg_hooks.h"

void		Cmd_Kill_f(edict_t *ent);

#define SG_DUEL_RANGE_MS	1.5f
#define SG_DUEL_COVER_MS	900.0f

/*
 * The compass bucket of a planar direction: 45 degrees per bucket, bucket
 * 0 centred on +x and buckets advancing counter-clockwise (E NE N NW W SW
 * S SE). The fold into 0..2pi happens BEFORE the scale for the same
 * reason Heading_Quantize (SG_Rune().c) folds -- a negative angle scaled and
 * truncated is not a wrap. escapepriors.py bearing_bucket() is this
 * function; the two must agree or the mined buckets name other exits.
 */
static int SG_Bearing8(float dx, float dy)
{
	float a = atan2f(dy, dx) * (180.0f / (float)M_PI) + 22.5f;

	while (a < 0.0f)
		a += 360.0f;
	return ((int)(a / 45.0f)) & 7;
}

/*
 * What one seed is worth to a bot in a fight, in the same milliseconds
 * Surface_At speaks. Applied to the seed the bot is STANDING on as well as to
 * every candidate: a term that only prices the alternatives makes staying put
 * free, and a bot at the wrong range that finds every step more expensive than
 * standing still is a bot that has been argued into never moving. The current
 * seed is measured from its own origin rather than from the bot's exact
 * position, so both sides of the comparison are the same measurement.
 */
static float Duel_Price(edict_t *e, vec3_t seed_org, vec3_t enemy_org,
                        float want, float expo)
{
	vec3_t	d, eyepoint;
	float	v;

	VectorSubtract(seed_org, enemy_org, d);
	v = SG_DUEL_RANGE_MS * fabsf(VectorLength(d) - want);

	if (expo > 0.0f)
	{
		trace_t tr;

		VectorCopy(seed_org, eyepoint);
		eyepoint[2] += e->viewheight;
		tr = sg_host.trace(eyepoint, NULL, NULL, enemy_org, e, MASK_OPAQUE);
		if (tr.fraction >= 1.0f)
			v += expo * SG_DUEL_COVER_MS;
	}
	return v;
}

/*
 * THE DESCENT (split from SG_BotThink, 2026-08-11 standards pass; body
 * verbatim): the incumbent's re-price, the candidate walk over every
 * proven link off this seed -- rail rhythm, latch, no-ropes-in-the-house,
 * shadow pricing, the anti-linger surcharge -- and the argmin that names
 * the next commitment. Returns the chosen link; emits the values the
 * later stages read.
 */
int Think_PickLink(sg_bot_t *bot, sg_think_t *tc)
{
	/* the former parameter list, unpacked from the think context so the
	 * body below reads exactly as it did when these arrived as arguments.
	 * Four of the old parameters -- carrying, live, precision, rally_hold
	 * -- turned out never to be read by this body and have no unpack. */
	edict_t *e = tc->e;
	sg_role_t role = tc->role;
	int team = tc->team;
	const sg_weights_t *w = tc->w;
	const int *goal_field = tc->goal_field;
	const int *route_field = tc->route_field;
	qboolean route_pure = tc->route_pure;
	const int *support = tc->support;
	const int *intercept = tc->intercept;
	qboolean duel = tc->duel;
	vec_t *duel_org = tc->duel_org;
	float duel_want = tc->duel_want;
	float duel_expo = tc->duel_expo;

	int bestlink = -1;
	int li;
	vec3_t d;
	float bestval = 0.0f;
	float incumbent_v = 1e30f;
	int rail_seed = -1;
	int rail_client = -1;
	float rail_dose = 0.0f;
	qboolean rail_hold = false;

	/* life ticker for the route-jitter seed */
	if (e->health <= 0)
		bot->was_dead = 1;
	else if (bot->was_dead)
	{
		bot->was_dead = 0;
		bot->lives++;
		bot->inlinks_n = 0;     /* a new life rides in on its own roads */

		/*
		 * THE SPAWN BEAT (sg_spawnbeat, enhancement 7). Watched in
		 * chase-cam, the tell is not the route, it is the START of the
		 * route: the bot materialises and is already at full pace down a
		 * corridor it cannot have looked at yet. A player spawns, checks
		 * a shoulder, and THEN goes -- half a second of orientation that
		 * every human pays and no bot ever did.
		 *
		 * Half a second is the whole feature. The beat is skill-scaled
		 * because the better player pays less of it (0.9s at bot_skill
		 * 0, 0.4s at 4), the cvar is a multiplier on that so the beat
		 * can be widened without touching the ladder, and 0 -- the
		 * default -- is the fleet exactly as it shipped.
		 *
		 * Never on the first spawn of a level: joins already arrive
		 * staggered across their own greeting window, and sixteen bots
		 * all pausing on the opening whistle is a tell of its own.
		 * beat_ready is what makes that test honest (see its field).
		 */
		{
			float	mult = sg_cv.spawnbeat->value;

			if (mult > 0.0f && bot->beat_ready)
			{
				float	sk = (float)SG_CombatSkill(e) / 100.0f; /* 0..4 */
				float	dur = (0.9f - 0.5f * (sk / 4.0f)) * mult;

				if (dur > 2.0f)
					dur = 2.0f;     /* a knob, not a nap */
				SG_Mark(&bot->beat_from);
				SG_TimerArm(&bot->beat_until, dur);
				/* 60 to 120 degrees of sweep, stated as its half-width */
				bot->beat_arc = 30.0f + (float)(rand() % 31);
				bot->beat_sign = (rand() & 1) ? 1 : -1;
			}
			else
				bot->beat_until = 0.0f;
		}
	}
	/* the scoreboard ping a human would show from a near-local connection:
	 * stable per-session base with a +/-1 flicker, never outside 5-15
	 * (owner's ruling 2026-08-05: bots blend in everywhere, analytics
	 * included) */
	e->client->ping = bot->fake_ping + (rand() % 3) - 1;
	if (e->client->ping < 5) e->client->ping = 5;
	if (e->client->ping > 15) e->client->ping = 15;
	/* leg ticker: a new role is a new errand -- new opinion of the map */
	if ((int)role != bot->last_role_for_legs)
	{
		bot->last_role_for_legs = (int)role;
		bot->legs++;
	}

	/* the pricing terms, resolved once for the frame; the mega worth was
	 * settled by the objective stage and already rides the context */
	tc->health = e->health;
	tc->danger = Danger_Field(team);       /* the danger dimension, ours */
	/* downbeat live: attackers march, detours wait for the next bar */
	tc->push = (role == SG_ROLE_ATTACK &&
	            SG_TimerPending(sg_push_until[SG_TeamIdx(team)]));
	sg_route_pure_now = route_pure;

	/*
	 * THE RAIL RHYTHM, resolved ONCE for the whole fan (sg_railrhythm).
	 * The candidate loop runs about twenty-five links wide at ten hertz;
	 * scanning the sighting table inside it would pay for the same answer
	 * twenty-five times. Off by default: SG_RailThreat returns false on
	 * the cvar read before it touches anything, and both the pricing term
	 * and the hold below are dead behind rail_seed < 0.
	 *
	 * Four seconds of sighting age, the same freshness the approach-cover
	 * term uses, and eye or ear both count. An ear placement is a region
	 * up to three hundred units wide, which is a room -- coarse for
	 * shooting at and good enough for "do not walk into that doorway
	 * yet", which is the only thing it is asked here.
	 */
	if (SG_RailThreat(team, 4.0f, &rail_client, &rail_seed))
	{
		/* a carrier is what rails punish: 274-279 put rails at the top of
		 * the carrier kill ledger, 2998 damage to rocket-direct's 2317.
		 * The dose it pays for a lit step is half again the rest of the
		 * team's. */
		rail_dose = sg_cv.railrhythm->value *
		            ((role == SG_ROLE_CARRY) ? 1.5f : 1.0f);
	}
	else
	{
		rail_seed = -1;
		rail_client = -1;
	}

	bestval = Surface_At(tc, bot->seed, w, route_field, support, intercept);
	if (duel)
		bestval += Duel_Price(e, SG_Rune()->seeds[bot->seed].origin, duel_org,
		                      duel_want, duel_expo);
			/* the exposure dimension as a cover prior: a seed the map
			 * says everyone can SEE costs more while hurting, before
			 * any runtime trace confirms who is looking (area_hint,
			 * written by generation; 0 on old runes = no opinion) */
			if (duel_expo > 0.0f)
				bestval += duel_expo *
				    (float)SG_Rune()->seeds[bot->seed].area_hint * 1.8f;
	/*
	 * THE CARRIER DOES NOT SINK (pit forensics, waves 383-411: 83
	 * unopposed smap05 carries, 87% touched the mid-map basin, 33 ended
	 * "sank like a rock"). The flood's cheapest way out of that basin
	 * fires its long ropes from the BOTTOM of the water (seeds 541/545/
	 * 551/554 at z=-744), so the descent walks a carrier 250 units DOWN
	 * to reach a rope it then has four seconds of air to land. Breath
	 * doctrine is a motor override at the gurgle; it cannot un-choose
	 * the step that spent the air. A wet carrier prices every downward
	 * step out of contention -- but ONLY when some candidate is not
	 * downward, so a genuine one-way underwater tunnel still runs and
	 * no carrier is ever stranded.
	 */
	{
		qboolean sink_ban = false;

		if (role == SG_ROLE_CARRY && bot->seed >= 0 && e->waterlevel > 0)
		{
			int li2;
			float z0 = SG_Rune()->seeds[bot->seed].origin[2];

			for (li2 = SG_Rune()->first_link[bot->seed]; li2 >= 0;
			     li2 = SG_Rune()->next_link[li2])
				if (SG_Rune()->seeds[SG_Rune()->links[li2].to].origin[2] >=
				    z0 - 16.0f)
				{
					sink_ban = true;
					break;
				}
		}
		bot->sink_ban = sink_ban;
	}

	/*
	 * ANTI-LINGER (sg_unlinger, rung-4 cut #3). The forensics' surviving
	 * lead after two nulls: bot single-mate contact streaks beside the
	 * carrier run 3-10x longer than human ones (3.85-7.69s vs 0.68-
	 * 1.39s). The mechanism is not attraction -- the role gate and the
	 * support pull both nulled -- it is LINGERING: identical pacing on
	 * identical cheapest roads means a teammate that falls in beside the
	 * carrier simply stays there. Humans pass their carrier constantly
	 * (the relay pattern); they do not co-jog. So the cut is targeted:
	 * a non-escort continuously within 400u of its own carrier for
	 * >1.5s pays a surcharge on links that KEEP it there, until it
	 * separates. Passing stays free; only the co-jog is priced. The
	 * escort is exempt -- lingering is its entire job.
	 */
	{
		qboolean linger_hot = false;
		vec3_t car_org = { 0, 0, 0 };

		if ((sg_cv.unlinger->value > 0.0f ||
		     sg_cv.depace->value > 0.0f) &&
		    role != SG_ROLE_CARRY && role != SG_ROLE_ESCORT)
		{
			static gitem_t *lg_flag;
			edict_t *car = NULL;
			int ci;

			if (!lg_flag)
				lg_flag = FindItem("Enemy Flag");
			if (lg_flag)
				for (ci = 1; ci <= game.maxclients; ci++)
				{
					edict_t *ce = g_edicts + ci;

					if (!ce->inuse || !ce->client || ce == e ||
					    ce->client->ctf.teamnum != team || ce->deadflag)
						continue;
					if (ce->client->pers.inventory[
					        ITEM_INDEX(lg_flag)] > 0)
					{
						car = ce;
						break;
					}
				}
			if (car)
			{
				vec3_t cd;

				VectorCopy(car->s.origin, car_org);
				VectorSubtract(e->s.origin, car_org, cd);
				if (VectorLength(cd) < 400.0f)
				{
					if (bot->linger_since <= 0.0f)
						SG_Mark(&bot->linger_since);
					else if (SG_AgeOver(bot->linger_since, 1.5f))
						linger_hot = true;
				}
				else
					bot->linger_since = 0.0f;
			}
			else
				bot->linger_since = 0.0f;
		}
		else
			bot->linger_since = 0.0f;
		bot->linger_hot = linger_hot;

	for (li = SG_Rune()->first_link[bot->seed]; li >= 0; li = SG_Rune()->next_link[li])
	{
		rune_link_t *l = &SG_Rune()->links[li];
		float v = Surface_At(tc, l->to, w, route_field, support, intercept);
		int b;

		if (linger_hot)
		{
			vec3_t ld9;

			VectorSubtract(SG_Rune()->seeds[l->to].origin, car_org, ld9);
			if (VectorLength(ld9) < 400.0f)
				v += sg_cv.unlinger->value;
		}

		/*
		 * ROUTE DITHER (sg_routedither, rung-2 set #1 tell #2): the
		 * transition matrices show p=1.0 cells -- at a given seed this
		 * body always makes the identical next choice, and a judge
		 * reads the determinism off the sheet. A human's tie-break
		 * varies. Per-visit pseudo-noise under one hop of gradient
		 * (~125ms at dose 120): ties and near-ties resolve differently
		 * on different visits, the gradient itself never overruled.
		 * The salt rerolls on seed entry so the choice HOLDS within a
		 * visit -- no flip-flop -- and varies across visits.
		 */
		if (sg_cv.routedither->value > 0.0f)
		{
			unsigned dh = bot->dither_salt ^ (unsigned)li * 2654435761u;

			dh ^= dh >> 13; dh *= 2246822519u; dh ^= dh >> 16;
			v += sg_cv.routedither->value *
			     (float)(dh & 1023) / 1023.0f;
		}

		/*
		 * No ropes in the house. Wave 96, watched live: an attacker
		 * spinning in the flag room firing hooks at the walls while the
		 * flag sat unguarded a body-length away -- in-room hook links
		 * ping-pong a bot around the goal minimum, and rope-fire counts
		 * tripled the day the slew made firing cheap. Inside 600ms of
		 * the objective the legs beat any rope ritual; only a fleeing
		 * carrier keeps the choice.
		 */
		if (l->action == RL_HOOK && role != SG_ROLE_CARRY &&
		    goal_field[bot->seed] < 600 &&
		    goal_field[bot->seed] < SG_FIELD_INF)
			continue;
		/*
		 * COVER ON THE APPROACH. lmctf58's attack front dies at 3.3s out
		 * with no stalls and no wedges -- moving freely into a covered
		 * sightline, game after game (waves 112-115). The rune has
		 * carried measured exposure on every seed since the census pass;
		 * it priced cover for hurting duelists only. Now the final
		 * approach pays for visible ground too: an attacker inside 4s of
		 * the goal, and a carrier anywhere on the run home, prefers the
		 * corridor to the courtyard whenever the costs are close.
		 */
		if (role == SG_ROLE_ATTACK && goal_field[bot->seed] < 4000 &&
		    goal_field[bot->seed] < SG_FIELD_INF)
			/* 0.5, not 2.5: the lmctf58 audit caught this surcharge
			 * out-arguing the ~125/hop goal gradient (exposure bytes run
			 * 200+ on open approaches) -- six attackers orbited a pure
			 * flat run to the flag for ten minutes behind a wall made of
			 * preference. A preference stays UNDER the gradient. */
			v += 0.5f * (float)SG_Rune()->seeds[l->to].area_hint;

		/*
		 * SPREAD THE AXES. Two attackers on the same cheapest gradient
		 * arrive down the same corridor into the same sightline -- the
		 * perimeter maps (lmctf58, mactf06 before the pair-split) eat
		 * that single file forever. En route, the junior of any attacker
		 * pair pays for steps NEAR its senior: pressure splits into two
		 * axes with no explicit corridor model at all, and the sentry's
		 * dilemma starts before the threshold.
		 */
		if (role == SG_ROLE_ATTACK && bot->seed >= 0 &&
		    goal_field[bot->seed] < SG_FIELD_INF &&
		    goal_field[bot->seed] > 2500 && goal_field[bot->seed] < 12000)
		{
			int bi6;

			for (bi6 = 0; bi6 < SG_MAXBOTS; bi6++)
			{
				sg_bot_t *mb6 = &sg_bots[bi6];
				vec3_t md6;

				if (!mb6->active || mb6 == bot || !mb6->ent ||
				    !mb6->ent->inuse)
					continue;
				if (mb6->ent->client->ctf.teamnum != team)
					continue;
				if (mb6->last_role != (int)SG_ROLE_ATTACK)
					continue;
				if ((int)(mb6->ent - g_edicts) >= (int)(e - g_edicts))
					continue;       /* only the junior spreads */
				VectorSubtract(SG_Rune()->seeds[l->to].origin,
				               mb6->ent->s.origin, md6);
				if (VectorLength(md6) < 400.0f)
				{
					v += 150.0f;    /* was 800: six times the hop
					                 * gradient welded juniors to the
					                 * midfield (same audit) */
					break;
				}
			}
		}
		else if (role == SG_ROLE_CARRY)
			v += 0.4f * (float)SG_Rune()->seeds[l->to].area_hint; /* was 2.0: same audit */

		if (l->action == RL_HOOK && SG_TimerPending(bot->hookban_until) &&
		    e->waterlevel < 2)
			continue;           /* the rope is confiscated: walk -- but
			                     * never underwater, where walking does
			                     * not exist and the ban was a drowning
			                     * sentence (10 wedge deaths on the
			                     * lmctf05 pool floor, wave 111) */

		for (b = 0; b < SG_BL_MAX; b++)
			if (bot->bl_link[b] == li && SG_TimerPending(bot->bl_until[b]))
				break;
		if (b < SG_BL_MAX)
			continue;               /* shelved: the body could not run it */

		/*
		 * A rocket jump is bought with health (the proof's worst-case
		 * price rides in anchor[2]) and needs the launcher and a rocket.
		 * A candidate the body cannot pay for is not a candidate.
		 */
		if (l->action == RL_ROCKETJUMP)
		{
			static gitem_t *rj_rl, *rj_ammo;

			if (!rj_rl)
			{
				rj_rl = FindItem("Rocket Launcher");
				rj_ammo = FindItem("Rockets");
			}
			if (!rj_rl || !rj_ammo ||
			    !e->client->pers.inventory[ITEM_INDEX(rj_rl)] ||
			    e->client->pers.inventory[ITEM_INDEX(rj_ammo)] < 1 ||
			    e->health <= (int)l->anchor[2] + 25)
				continue;
		}

		/*
		 * The carrier's flee doctrine gets ears: a step toward a fresh
		 * believed contact -- seen or heard -- is priced as if it cost
		 * extra travel, up to ~1200ms for walking straight into them.
		 * Everyone else fights; the carrier's job is the capture point.
		 */
		/*
		 * THE WET ROUTE (sg_watercarry, wave 253). The lmctf01 census:
		 * thirteen of thirteen carrier deaths were rails on the dry
		 * corridors, while humans convert 71 percent there by swimming
		 * the moat -- underwater is the one country without railguns.
		 * A carrier prices swim links 800 cheaper; breath doctrine
		 * already owns the drowning risk.
		 */
		if (role == SG_ROLE_CARRY && l->action == RL_SWIM &&
		    sg_cv.watercarry->value)
			v -= 800.0f;

		/* the sink ban's teeth: 12000 exceeds the basin's worst gap
		 * (max eff link 4055 + field spread 2221), so any non-sinking
		 * candidate wins; the pre-pass above guarantees one exists */
		/* widened per the pre-registered fallback (416 forensics: two
		 * carriers FELL dry straight into the cistern -- the wet-trigger
		 * never saw a choice -- then hooked until the air ran out): a
		 * DESTINATION inside water counts as sinking regardless of how
		 * dry the carrier currently is, whenever it is also downward. */
		if ((bot->sink_ban ||
		     (role == SG_ROLE_CARRY &&
		      (l->action == RL_SWIM ||
		       (SG_Rune()->seeds[l->to].flags & RSF_WATER)))) &&
		    SG_Rune()->seeds[l->to].origin[2] <
		        SG_Rune()->seeds[bot->seed].origin[2] - 16.0f)
			v += 12000.0f;

		/*
		 * THE SHELF PAYS ITS CLIFF, at the layer that actually walks
		 * (sg_shelfcost, steal-genesis study). The first cut priced the
		 * waypoint surface and read a flat null in three waves: between
		 * commitments the descent runs on the flood alone, and the flood
		 * happily steps DOWN onto the zero-yield floor under the enemy
		 * stand (101 close approaches there, 91% dead in 1.2s, zero
		 * steals). Fourth cut, per PITTRACE: 74 of 89 pit entries were
		 * plain attack-role link descent, LATERAL at floor height -- and
		 * the field-layer surcharge alone made it worse, because link
		 * selection scores the DESTINATION's potential and the pit basin
		 * stays cheap (its hook out is free by design) while the corridor
		 * around it got dearer. So the movement layer now charges ANY
		 * step whose destination is a masked sub-stand seed, downward or
		 * flat; steps OUT of the pit still pay nothing -- a knocked-in
		 * bot climbs like it means it.
		 */
		if (sg_cv.shelfcost->value > 0.0f)
		{
			int shti = SG_TeamIdx(team);

			if (sg_fields.shelf_cliff[shti] &&
			    sg_fields.shelf_cliff[shti][l->to] > 0 &&
			    !(bot->seed >= 0 &&
			      sg_fields.shelf_cliff[shti][bot->seed] > 0 &&
			      SG_Rune()->seeds[l->to].origin[2] >
			          SG_Rune()->seeds[bot->seed].origin[2] + 16.0f))
				/* 60000, not 12000 (fifth cut): the flood surcharge
				 * props the whole low corridor to ~12000+ field units,
				 * so a 12000 step charge on the pit's tiny base was
				 * arithmetically COMPETITIVE with turning back -- the
				 * two layers cancelled at the lip. The step charge must
				 * dominate the field spread it created. */
				v += sg_cv.shelfcost->value * 60000.0f;
		}

		if (role == SG_ROLE_CARRY && l->action == RL_HOOK)
		{
			/*
			 * The carrier's ROPE is not everyone's rope: phase 1 is a
			 * standing aim frame with the flag on its back, and a miss
			 * re-runs the ritual (wave 58: 17s at 10ms/s against a 43%
			 * land rate). But the blanket surcharge overcorrected --
			 * waves 58-66 show five of fourteen carriers dying of the
			 * CLOCK, legs too slow for the long returns, while the rope
			 * at 800 u/s is the fastest thing in clear water. The aim
			 * frame is only deadly when somebody is watching: the
			 * surcharge now applies under fresh contact and stands down
			 * when the country is quiet.
			 */
			int s2;

			for (s2 = 0; s2 < SG_MAX_ENEMY_TRACK; s2++)
			{
				sg_belief_enemy_t *en = &sg_caco_enemies[SG_TeamIdx(team)][s2];
				vec3_t pd;

				if (en->client < 0 || en->seed < 0 ||
				    SG_AgeAtLeast(en->seen_time, 4.0f))
					continue;
				/*
				 * CLOSE contact only. 'Seen recently anywhere' kept
				 * pursued carriers on their legs the whole run home,
				 * and 32 games of the fast-respawn meta produced zero
				 * captures with rail-armed pursuit running them down.
				 * A pursuer 1500 units back cannot punish a half-second
				 * aim stand -- the rope at 800 u/s GAINS on them. Only
				 * an enemy believed inside 700 makes the standing frame
				 * a real gamble.
				 */
				VectorSubtract(SG_Rune()->seeds[en->seed].origin,
				               e->s.origin, pd);
				if (VectorLength(pd) < 700.0f)
				{
					/*
					 * THE FAST CARRY (sg_fastcarry, A/B wave 205+).
					 * The human corpus set the bar: a successful
					 * carry is 14 seconds of covering the WHOLE
					 * route, and humans convert 12.8 percent of
					 * steals doing it. Our carriers survive human
					 * lengths (interpose) and cover a third of the
					 * ground -- this 2000ms rope tax under contact
					 * was tuned in the era before escorts, screens,
					 * or the scoop existed to spend it. With a
					 * bodyguard on the line, the aim-stand gamble is
					 * priced at 500: the rope comes back to the run
					 * home.
					 */
					v += sg_cv.fastcarry->value
					     ? 500.0f : 2000.0f;
					break;
				}
			}
		}
		if (role == SG_ROLE_CARRY && bot->carry_startcost < 0 &&
		    bot->seed >= 0 && goal_field[bot->seed] < SG_FIELD_INF)
			bot->carry_startcost = goal_field[bot->seed];

		/*
		 * THE PROGRESS GUARD. Wave 140's carry traces: the 53-second 5v1
		 * carry ran its cost 12800 down to 6400, fell into the pool, and
		 * finished the fight at 8623 -- a third of the route home handed
		 * back in one drop, then a crawl. A carrier that loses ground it
		 * already paid for is off its route (act=-1 frames, 61 of them
		 * that game); the shelf that priced the old position is stale
		 * testimony there. On a 2500-cost regression from the carry's
		 * best: wipe the shelf, re-arm the breakout gauge from here, and
		 * say so in the log. The descent replans from where the body
		 * actually is, not where the plan thought it would be.
		 */
		if (role == SG_ROLE_CARRY && bot->seed >= 0 &&
		    goal_field[bot->seed] < SG_FIELD_INF)
		{
			int cc = goal_field[bot->seed];

			if (bot->carry_bestcost < 0 || cc < bot->carry_bestcost)
				bot->carry_bestcost = cc;
			else if (cc > bot->carry_bestcost + 2500 &&
			         SG_TimerReadyStrict(bot->carry_lost_at + 2.0f))
			{
				int b2, was_best = bot->carry_bestcost;

				SG_Mark(&bot->carry_lost_at);
				/* wipe STALE testimony only: a shelf priced at the
				 * old position is hearsay here, but one recorded in
				 * the last three seconds is the body reporting from
				 * where it stands now, and un-shelving those sent
				 * carriers into retry-fail churn (offgraph frames
				 * 0->3->5->12%% across waves 141-144). Shelves live
				 * 120s, so age reads off the expiry. */
				for (b2 = 0; b2 < SG_BL_MAX; b2++)
					if (bot->bl_until[b2] < level.time + 117.0f)
						bot->bl_until[b2] = 0.0f;
				bot->carry_startcost = cc;
				bot->carry_bestcost = cc;
				if (sg_cv.debug->value)
					sg_host.dprint("CARRYLOST %s best=%d now=%d org=(%.0f %.0f %.0f)\n",
					           e->client->pers.netname,
					           was_best, cc,
					           e->s.origin[0], e->s.origin[1],
					           e->s.origin[2]);
			}
		}
		if (role == SG_ROLE_CARRY &&
		    !(bot->carry_startcost > 0 && bot->seed >= 0 &&
		      goal_field[bot->seed] < SG_FIELD_INF &&
		      goal_field[bot->seed] >
		          bot->carry_startcost / 2))
		{
			/*
			 * THE BREAKOUT, gauged by STATE, not clock. Wave 69: nine
			 * carries, every one pinned at 0-10% of the way home, the
			 * flee doctrine's own pricing surcharging every exit of a
			 * hot flag room until the argmin oscillated between doors.
			 * A 10-second window (wave 70) freed the ones that broke
			 * fast and re-pinned the ones that didn't -- Trace, 72s at
			 * 1% -- so the clock is gone: the dodge stays silent until
			 * the carrier has actually cleared a quarter of the route
			 * home, and resumes in open country, where it was ever
			 * wise. lmctf05's carrier rode the silent window to 44%,
			 * three times the old ceiling; the gate now follows the
			 * body instead of the wall clock.
			 */
			int s;

			for (s = 0; s < SG_MAX_ENEMY_TRACK; s++)
			{
				sg_belief_enemy_t *en = &sg_caco_enemies[SG_TeamIdx(team)][s];

				if (en->client >= 0 && en->seed >= 0 &&
				    SG_AgeUnder(en->seen_time, 4.0f))
				{
					VectorSubtract(SG_Rune()->seeds[l->to].origin,
					               SG_Rune()->seeds[en->seed].origin, d);
					if (VectorLength(d) < 400.0f)
						/* 1.5, was 3.0: this loop now only speaks in
						 * open country past the breakout, where wave
						 * 71's three clock-outs at 64-98% home say the
						 * full dodge tax costs more match clock than
						 * it saves in blood */
						v += 1.5f * (400.0f - VectorLength(d));
				}
			}
		}
		/*
		 * The fighter's two terms, the mirror of the carrier's one.
		 *
		 * Range control: a candidate is priced by how far it puts the bot from
		 * the range the weapon in hand actually wants -- WEAPONS.md 2.1's
		 * ladders, read back out as a distance by SG_CombatDuel.
		 *
		 * Cover: a candidate the target can SEE costs what this bot's own
		 * state says being seen is worth -- near nothing when healthy and in
		 * band, the full 900 ms when hurt or holding the wrong gun for the
		 * distance. One MASK_OPAQUE ray per candidate, the same mask and the
		 * same shape as the sight gate itself (sg_caco.c:100-114). A fan runs
		 * about 25 links wide, and the whole term is skipped on every frame
		 * there is no fight, which is most of them.
		 */
		else if (duel &&
		         !(role == SG_ROLE_ATTACK &&
		           sg_cv.press->value) &&
		         /* CARRIER PRESS (sg_carrypress, wave 280+). The carry
		          * traces (274-279): 61%% of carrier frames make no
		          * homeward progress at ~190 u/s, and 48 of 49 carries
		          * die before the route's final tenth -- the carrier
		          * was still holding duel range against pursuers, the
		          * receding behavior the press cured for attackers in
		          * the only parity-cap A/B ever won. A fleeing carrier
		          * has no business pricing weapon range: forward. */
		         !(role == SG_ROLE_CARRY &&
		           sg_cv.carrypress->value))
		{
			/*
			 * THE PRESS (sg_press, A/B wave 169+). The travel
			 * decomposition (waves 164-168): twenty percent of ALL
			 * attacker distance is spent actively receding from the
			 * goal -- and range control is the suspect with the
			 * motive: an engaged attacker prices its steps toward the
			 * range its weapon wants, which for the long guns means
			 * BACKWARD, and at parity engagement never ends. Under
			 * press, attackers keep the aim, keep the weave, and keep
			 * walking forward; only defenders and escorts hold range
			 * discipline. The escort's 3.1 efficiency against the
			 * attacker's 1.6 was always this contrast.
			 */
			v += Duel_Price(e, SG_Rune()->seeds[l->to].origin, duel_org,
			                duel_want, duel_expo);
			/* the exposure dimension as a cover prior: a seed the map
			 * says everyone can SEE costs more while hurting, before
			 * any runtime trace confirms who is looking (area_hint,
			 * written by generation; 0 on old runes = no opinion) */
			if (duel_expo > 0.0f)
				v += duel_expo *
				    (float)SG_Rune()->seeds[l->to].area_hint * 1.8f;
		}

		/*
		 * THE HUMAN PRIOR (sg_humanprior, A/B wave 188+). Fifty-nine
		 * hours of recorded play, log-tiered per link: a candidate on
		 * a human highway prices up to ~380ms cheaper. Humans
		 * concentrate (top 1%% of transitions carry up to 13%% of all
		 * traffic) and their concentration encodes twenty years of
		 * knowing which roads survive contact -- the discount lets
		 * the descent inherit that without a single scripted route.
		 */
		if (sg_human_use &&
		    sg_cv.humanprior->value)
			v -= 1.5f * (float)sg_human_use[li];

		/*
		 * THE FLAG-LIVE PRIOR (sg_flagprior, A/B wave 213+). The
		 * global prior nulled -- but the corpus shows humans run 60%%
		 * DIFFERENT roads while a flag is out (carrywindows census),
		 * and those are the twenty seconds that decide every game.
		 * The discount applies only inside the window the evidence
		 * came from: either flag astray, up to ~380ms off the roads
		 * humans run when it matters.
		 */
		if (sg_human_live &&
		    sg_cv.flagprior->value &&
		    tc->role != SG_ROLE_CARRY &&
		    (sg_caco_team_belief.flag[0].state == SG_FLAG_ASTRAY ||
		     sg_caco_team_belief.flag[1].state == SG_FLAG_ASTRAY))
			/* the cvar IS the dose. Wave 214 (dose 2): carrier route
			 * coverage FELL under the discount -- the window corpus
			 * is hunters' roads, not escapees' (POV-agnostic cut).
			 * The roads go to the roles they came from: hunters
			 * inherit them, the carrier keeps its pure homeward
			 * pricing. */
			v -= 1.5f * sg_cv.flagprior->value *
			     (float)sg_human_live[li];

		/*
		 * DEFENSE DWELL (sg_defpost, wave 286+). The corpus inverted
		 * the stand-freeze doctrine: only 19% of human defensive
		 * standing time is within 250u of the stand -- humans post on
		 * the APPROACHES. Cheap first cut per the extraction's own
		 * sequencing: defenders price steps toward high-dwell seeds
		 * cheaper (their team's plane), same idiom as every prior.
		 */
		if (tc->role == SG_ROLE_DEFEND &&
		    sg_def_post[SG_TeamIdx(team)] &&
		    sg_cv.defpost->value > 0)
			v -= 1.5f * sg_cv.defpost->value *
			     (float)sg_def_post[SG_TeamIdx(team)][l->to];

		/*
		 * DEFENSE INTERCEPT (sg_defreact, wave 295+). The response
		 * census, n=1044: on a steal humans leave the post in 0.9s
		 * and run the ESCAPE CORRIDOR toward where the carrier will
		 * be -- aim-at-lead 0.48-0.68 vs aim-at-now ~0. Our defender
		 * already chases the believed CURRENT position (the flag
		 * field re-floods from it); this term bends that pursuit
		 * toward the corpus's learned cut-off seeds while our flag
		 * is astray. Direct chase is 8% of human responses.
		 */
		if (tc->role == SG_ROLE_DEFEND &&
		    sg_def_icept[SG_TeamIdx(team)] &&
		    sg_caco_team_belief.flag[SG_TeamIdx(team)].state == SG_FLAG_ASTRAY &&
		    sg_cv.defreact->value > 0)
			v -= 1.5f * sg_cv.defreact->value *
			     (float)sg_def_icept[SG_TeamIdx(team)][l->to];

		/*
		 * THE ESCAPE PRIOR (sg_escapeprior, wave 284+). The missing
		 * corpus cut: .hml was POV-agnostic and therefore mostly the
		 * HUNTERS' roads (re-tested null twice). This one is only the
		 * flag carrier's own entity trajectory in the 20s after each
		 * steal -- the roads humans actually flee on. Applied to the
		 * carry role alone; cvar value is the dose, same scale as the
		 * other priors (1.5ms per tier point per dose).
		 */
		if (sg_human_escape &&
		    tc->role == SG_ROLE_CARRY &&
		    sg_cv.escapeprior->value > 0)
			v -= 1.5f * sg_cv.escapeprior->value *
			     (float)sg_human_escape[li];

		/*
		 * APPROACH COVER (sg_approachcover, wave 314+). The carry
		 * forensics moved the fight: 90% of early carrier kills came
		 * from defenders ALREADY PARKED within 1000u of the robbed
		 * stand (81% there five seconds before), rail 47% from
		 * grounded shooters at 237u -- and the nearest enemy is 210u
		 * away at the grab. Cover bought after the grab arrives too
		 * late; the line must be chosen on the way IN. Same trace,
		 * same book as the carrier's, applied to the attacker against
		 * every fresh eye sighting near the target stand.
		 */
		if (tc->role == SG_ROLE_ATTACK &&
		    sg_cv.approachcover->value > 0)
		{
			int acs;

			for (acs = 0; acs < SG_MAX_ENEMY_TRACK; acs++)
			{
				sg_belief_enemy_t *aen =
				    &sg_caco_enemies[SG_TeamIdx(team)][acs];
				vec3_t aeye, athr, aspan;
				trace_t actr;

				if (aen->client < 0 || aen->heard_only ||
				    SG_AgeOver(aen->seen_time, 4.0f) ||
				    aen->seed < 0)
					continue;
				VectorCopy(SG_Rune()->seeds[l->to].origin, aeye);
				aeye[2] += 22.0f;
				VectorCopy(SG_Rune()->seeds[aen->seed].origin, athr);
				athr[2] += 22.0f;
				VectorSubtract(athr, aeye, aspan);
				if (VectorLength(aspan) > 900.0f)
					continue;
				actr = sg_host.trace(aeye, NULL, NULL, athr, e, MASK_SOLID);
				if (actr.fraction >= 1.0f)
				{
					v += sg_cv.approachcover->value;
					break;  /* one exposure is enough to price */
				}
			}
		}

		/*
		 * RAIL COVER (sg_railrhythm). The other half of the counter-play,
		 * and the half that runs when there is no lane to time: a burst
		 * that ENDS somewhere the railer can see is a burst that ends in
		 * front of a loaded gun. The trace is the approach-cover trace --
		 * same eye height, same mask, same 900-unit gate, MASK_SOLID from
		 * the candidate seed to the believed post -- but the sighting was
		 * chosen once for the whole fan above, so this costs exactly one
		 * ray per candidate rather than one per candidate per enemy.
		 *
		 * Every role pays it. Approach cover is an attacker's term and
		 * carrier cover is a carrier's; a rail lane is neither, it is a
		 * fact about the room, and the defender walking back to a post
		 * across it dies the same way. The carrier's dose is the larger
		 * one, folded in where the sighting was resolved.
		 *
		 * A PREFERENCE, not a wall, for the reason the lmctf58 audit
		 * wrote down two terms above: the dose is the cvar's and it
		 * belongs under the ~125/hop goal gradient. There is no branch
		 * here that can make a seed unreachable.
		 */
		if (rail_seed >= 0)
		{
			vec3_t	reye, rthr, rspan;
			trace_t	rtr;

			VectorCopy(SG_Rune()->seeds[l->to].origin, reye);
			reye[2] += 22.0f;
			VectorCopy(SG_Rune()->seeds[rail_seed].origin, rthr);
			rthr[2] += 22.0f;
			VectorSubtract(rthr, reye, rspan);
			if (VectorLength(rspan) < 900.0f)
			{
				rtr = sg_host.trace(reye, NULL, NULL, rthr, e, MASK_SOLID);
				if (rtr.fraction >= 1.0f)
					v += rail_dose;
			}
		}

		/*
		 * CARRIER COVER (sg_carrycover, wave 274+). The 268-273 DMG
		 * ledger: rails are still the carrier's top killer (2998 dmg
		 * to rocket-direct's 2317), fired mostly by GROUNDED defenders
		 * at 135-415 units -- standing shots down clear lines. A rail
		 * needs line of sight; a human carrier buys cover with corners
		 * the way this graph buys speed with links. For the carrier
		 * only, while the team's freshest EYE sighting is under 3s
		 * old, a candidate step the sighted enemy can see costs the
		 * cvar's value in ms extra. One trace per candidate, against
		 * the one sighting that matters most.
		 */
		if (tc->role == SG_ROLE_CARRY &&
		    sg_cv.carrycover->value > 0)
		{
			int			cs, best_cs = -1;
			float		best_t = -1.0f;

			for (cs = 0; cs < SG_MAX_ENEMY_TRACK; cs++)
			{
				sg_belief_enemy_t *en = &sg_caco_enemies[SG_TeamIdx(team)][cs];

				if (en->client >= 0 && !en->heard_only &&
				    SG_AgeUnder(en->seen_time, 3.0f) &&
				    en->seen_time > best_t)
				{
					best_t = en->seen_time;
					best_cs = cs;
				}
			}
			if (best_cs >= 0)
			{
				vec3_t	eye, thr, span;
				trace_t	ctr;

				VectorCopy(SG_Rune()->seeds[l->to].origin, eye);
				eye[2] += 22.0f;
				VectorCopy(SG_Rune()->seeds[
				    sg_caco_enemies[SG_TeamIdx(team)][best_cs].seed].origin, thr);
				thr[2] += 22.0f;
				/* range gate (wave 279): the 268-277 ledger kills all
				 * sit inside ~800u -- a sighting across the map must
				 * not bend the route (dose 1200 showed the failure:
				 * 53-second carries that never arrive). */
				VectorSubtract(thr, eye, span);
				if (VectorLength(span) < 900.0f)
				{
					ctr = sg_host.trace(eye, NULL, NULL, thr, e, MASK_SOLID);
					if (ctr.fraction >= 1.0f)
						/* CLOCKPLAY scales the price, not the rule: a
						 * late lead pays 1.3x for the corner because
						 * the flag only has to get home once more, a
						 * late deficit pays 0.8x because a carrier
						 * still alive at the horn scored nothing.
						 * Exactly 1.0x -- the same float, the same
						 * route -- with sg_clockplay off. */
						v += sg_cv.carrycover->value *
						     Clock_CoverScale(team);
				}
			}
		}

		/*
		 * THE SWITCHING COST (sg_sticky, A/B wave 168+). The owner's
		 * diagnosis, measured: offense converts 1.6 ms of progress per
		 * unit walked against the escort's 3.1 on the same maps -- half
		 * of all offensive walking buys nothing -- and the chosen link
		 * changes every ~2.4 seconds. The surface offers ties, and the
		 * per-frame argmin flips between them: a bot following a LINE
		 * on the gradient, not the gradient. The incumbent route now
		 * holds its seat unless a challenger beats it by 15 percent --
		 * a mind-change gets priced at the moment it is made, which is
		 * the owner's wasted-distance penalty moved to where it can
		 * steer. Shelved, blocked, or completed incumbents pay nothing:
		 * displacement stays free when the route is actually dead.
		 */
		/*
		 * ROUTE JITTER (sg_routejitter, wave 359). The film verdict
		 * chain: rope-vs-brush (calibrated 8/8 judge) -> ribbon v1
		 * (lanes, not a band) -> ribbon v2+dose (pooled null: the
		 * steering re-centers whatever the aim does). The band humans
		 * paint is ROUTE diversity, not in-lane wander: near-optimal
		 * link chains differ per player and per run, where our argmin
		 * rides the single optimum every time. Each bot-life gets a
		 * deterministic per-link pricing tilt (cvar = max percent);
		 * ties and near-ties then split the population across
		 * different roads. Deterministic per life: no per-frame noise,
		 * no flapping -- a LIFE rides one opinion of the map.
		 */
		if (sg_cv.routejitter->value > 0.0f)
		{
			unsigned rj = ((unsigned)li * 2654435761u) ^
			              ((unsigned)(e - g_edicts) * 40503u) ^
			              ((unsigned)(bot->lives + bot->legs) * 9176u);

			rj = (rj >> 4) & 1023u;
			v *= 1.0f + ((float)rj / 1023.0f - 0.5f) * 0.02f *
			     sg_cv.routejitter->value;
		}

		/*
		 * NO IMMEDIATE BACKTRACK (sg_nobacktrack, wave 392 trial). The
		 * smap05 map-center orbit -- and the chronic ~130 suicides a
		 * wave behind it -- is two seeds on a field plateau electing
		 * each other forever at full sprint. The latch (wave 385-390)
		 * pooled null against it: holding a link longer does not help
		 * when the flap is BETWEEN legs. This prices the one link that
		 * returns to the seed just departed, for a few seconds, unless
		 * pricing leaves no other finite way down. A human does turn
		 * around sometimes; a human does not do-si-do.
		 */
		if (li >= 0 && SG_Rune()->links[li].to == bot->prev_seed &&
		    SG_AgeUnder(bot->prev_seed_time, 3.0f))
			v *= 1.0f + sg_cv.nobacktrack->value / 100.0f;

		/*
		 * NOT THROUGH THERE AGAIN (sg_tilt). The lane the last life
		 * ended in costs a third more for the first twenty-five
		 * seconds of this one -- fifty if the same lane took two
		 * lives inside a minute. A third is deliberately a
		 * PREFERENCE and not a wall: where the map offers a second
		 * road the bot takes it, and where it does not, the tilt
		 * loses to the gradient and the bot walks the only corridor
		 * there is, which is also what the human does after standing
		 * at the respawn swearing about it. Nothing here is
		 * permanent and nothing here is written down: the window
		 * runs out, the lane is forgotten, and the map's real
		 * lessons stay in the danger dimension where they belong.
		 */
		if (bot->tilt_lane_n > 0 && SG_TimerPending(bot->tilt_until) &&
		    sg_cv.tilt->value > 0.0f &&
		    Tilt_InLane(bot, l->to))
		{
			v *= SG_TILT_PRICE;

			/* one line in sixteen: a bot in a two-hop ball prices
			 * every candidate it owns, every frame, and the log is
			 * for reading */
			if (sg_cv.debug->value &&
			    !(bot->tilt_said++ & 15))
				sg_host.dprint("TILTAVOID %s link=%d to=%d dseed=%d "
				           "left=%.1f%s\n",
				           e->client->pers.netname, li, l->to,
				           bot->tilt_seed,
				           SG_TimerRemaining(bot->tilt_until),
				           SG_TimerPending(bot->tilt_caution_until)
				           ? " cautious" : "");
		}

		/*
		 * POST-DEATH CAUTION, the routing half. The approach-cover
		 * term below already teaches an attacker on the last leg to
		 * pay for open ground; for the few seconds after a respawn
		 * EVERY role pays it, at the same dose, whatever it is doing.
		 * A player who just died walks the wall side of the room for
		 * a while -- and that is the whole of the behaviour: he is
		 * not slower, not worse, and not hiding. The willingness half
		 * lives in sg_combat.c, through SG_TiltCaution.
		 */
		if (SG_TimerPending(bot->tilt_caution_until) &&
		    sg_cv.tilt->value > 0.0f)
			v += SG_TILT_COVER * (float)SG_Rune()->seeds[l->to].area_hint;

		/* EXIT-LANE ASYMMETRY (sg_exitasym). Humans tend to leave by a
		 * different lane than they came in on, but not always -- a coin
		 * flipped once per carry, the dose set by the cvar. */
		if (role == SG_ROLE_CARRY && bot->exitasym_armed)
		{
			int ea;

			for (ea = 0; ea < bot->exitasym_n; ea++)
				if (bot->exitasym_set[ea] == li)
				{
					v *= 1.5f;
					break;
				}
		}

		/*
		 * THE ESCAPE PRIOR (sg_escapeprior). The exit drawn at the
		 * grab, spent here: a candidate that leaves the robbed stand
		 * on the drawn compass bearing is cheaper by the human
		 * probability of that bearing, for the three seconds the
		 * bearings were mined over. The bearing is measured from the
		 * STAND, not from the body -- that is what the corpus
		 * measured, and it keeps the whole first leg pointed at one
		 * exit instead of re-deciding as the carrier drifts.
		 *
		 * Candidates inside 160 units of the stand carry no bearing
		 * worth the name and are left alone -- the same displacement
		 * floor escapepriors.py MIN_RUN_U demanded before it would
		 * believe a human's bearing. The v > 0 guard is for the one
		 * case a multiplicative discount inverts: a candidate the
		 * human-highway prior has already priced below zero would be
		 * made MORE expensive by scaling toward zero.
		 */
		if (role == SG_ROLE_CARRY && bot->escprior_bucket >= 0 &&
		    SG_TimerPending(bot->escprior_until) && v > 0.0f)
		{
			float ex = SG_Rune()->seeds[l->to].origin[0] - bot->escprior_org[0];
			float ey = SG_Rune()->seeds[l->to].origin[1] - bot->escprior_org[1];

			if (ex * ex + ey * ey > 160.0f * 160.0f &&
			    SG_Bearing8(ex, ey) == bot->escprior_bucket)
				v *= 1.0f - bot->escprior_dose;
		}

		if (bot->sticky_link == li &&
		    sg_cv.sticky->value)
			v *= 0.85f;

		if (li == bot->sticky_link)
			incumbent_v = v;

		if (v < bestval)
		{
			bestval = v;
			bestlink = li;
		}
	}
	}       /* anti-linger scope */

	tc->bestval = bestval;
	tc->incumbent_v = incumbent_v;
	tc->rail_seed = rail_seed;
	tc->rail_client = rail_client;
	tc->rail_dose = rail_dose;
	tc->rail_hold = rail_hold;
	tc->bestlink = bestlink;
	return bestlink;
}

/*
 * THE COMMITMENT (split from SG_BotThink, 2026-08-11 standards pass;
 * body verbatim): everything between the argmin and the aim -- the link
 * latch, saddle commitment, dead-door shelving, the straight-line and
 * see-the-flag terminal overrides, the clean grab, the defender post,
 * and the rail hold. Returns the link the body will actually ride.
 */
int Think_CommitLink(sg_bot_t *bot, sg_think_t *tc)
{
	/* the former parameter list, unpacked from the think context; cmd
	 * stays a real parameter until the movement stage speaks context.
	 * The six in/out pointers became direct context access in the body. */
	usercmd_t *cmd = &tc->cmd;
	edict_t *e = tc->e;
	sg_role_t role = tc->role;
	int team = tc->team;
	const sg_weights_t *w = tc->w;
	const int *goal_field = tc->goal_field;
	qboolean precision = tc->precision;
	qboolean duel = tc->duel;
	float bestval = tc->bestval;
	float incumbent_v = tc->incumbent_v;
	int rail_seed = tc->rail_seed;
	int rail_client = tc->rail_client;
	int bestlink_in = tc->bestlink;
	/* six of the former parameters -- carrying, live, duel_org,
	 * duel_want, duel_expo, rail_dose -- turned out never to be read by
	 * this body and have no unpack */
	int bestlink = bestlink_in;
	int li;
	qboolean rally_hold = tc->rally_hold;
	qboolean rail_hold = tc->rail_hold;
	qboolean hold_post = false;
	float post_yaw = tc->post_yaw;
	float post_sight = tc->post_sight;
	vec3_t d;

	/*
	 * THE LINK LATCH (sg_linklatch, wave 289+). The demo census: 87
	 * deg/s of heading noise, a 49% reversal rate, a full 180 every
	 * nine seconds -- a 10Hz argmin flapping across noise-level ties
	 * on a surface whose item terms refresh at 1Hz. The incumbent
	 * keeps its seat for the cvar's milliseconds unless a challenger
	 * beats it by 15%; a dead incumbent (infinite v, no longer offered
	 * from this seed) abdicates immediately. This is the re-decision
	 * cadence matched to the information's own refresh rate.
	 */
	if (sg_cv.linklatch->value > 0 &&
	    bestlink >= 0 && bot->sticky_link >= 0 &&
	    bestlink != bot->sticky_link &&
	    SG_TimerPending(bot->latch_until) &&
	    incumbent_v < 1e29f &&
	    bestval > incumbent_v * 0.85f)
	{
		bestlink = bot->sticky_link;
	}
	else if (bestlink != bot->sticky_link)
	{
		SG_TimerArm(&bot->latch_until,
		    sg_cv.linklatch->value / 1000.0f);
	}
	if (bestlink >= 0 && bestlink != bot->ribbon_link)
	{
		/* new leg: sample the lane offset once and hold it. The film
		 * verdict (calibrated blind judge, 8/8): every traversal lands
		 * on the SAME polyline -- a rope, where humans paint a 50-150u
		 * brush. Per-tick noise would be jitter, not diversity; the
		 * offset must PERSIST across the leg. */
		/* the leg just closed out goes into the exit-lane ring
		 * (sg_exitasym): this rollover is the only true per-link
		 * advance -- the role-change ticker fires far too rarely
		 * to remember an inbound route */
		if (bot->ribbon_link >= 0)
		{
			bot->inlinks[bot->inlinks_n % 16] = bot->ribbon_link;
			bot->inlinks_n++;
		}
		bot->ribbon_link = bestlink;
		bot->ribbon_off = ((float)(rand() % 2001) / 1000.0f - 1.0f) *
		                  sg_cv.ribbon->value;
		bot->ribbon_goal = bot->ribbon_off;
	}
	/* v2 drift: the film judge's verdict on v1 -- a fixed per-leg lane
	 * quantizes into railroads; a human band needs the offset to WANDER
	 * along the run. Low-frequency, trace-clamped downstream. */
	if (SG_TimerReady(bot->ribbon_next))
	{
		bot->ribbon_goal = ((float)(rand() % 2001) / 1000.0f - 1.0f) *
		                   sg_cv.ribbon->value;
		SG_TimerArm(&bot->ribbon_next,
		    1.0f + (float)(rand() % 100) / 100.0f);
	}
	bot->ribbon_off += 0.20f * (bot->ribbon_goal - bot->ribbon_off);
	bot->sticky_link = bestlink;

	/*
	 * THE LAST TEN METERS ARE A STRAIGHT LINE. An attacker at the goal
	 * minimum kept arguing with the link graph -- the argmin flaps
	 * between near-equal links and the bot orbits a flag it could
	 * TOUCH (wave 96, live witness: every defender dead, the attacker
	 * spinning beside the stand). Inside 400ms the graph has nothing
	 * left to teach: drop the link and let the aim fall through to the
	 * goal-entity fallback -- a straight walk, a touch, done. The
	 * carrier gets the same grace at its own stand.
	 */
	/*
	 * SEE THE FLAG, GO THROUGH THE FLAG (owner's order, 2026-08-11,
	 * sharpening wave 96): the 400ms cost gate still let a bot steer
	 * at seed centers while the flag stood in plain sight across the
	 * room. Cost is not the trigger anymore -- SIGHT is. An attacker
	 * with line of sight to the standing flag inside 512 drops the
	 * graph immediately and the aim falls through to the flag item
	 * (and through-extension past it). Seeing it is earned perception,
	 * so a visible dropped enemy flag qualifies the same (Rule 19).
	 */
	{
		qboolean flag_los = false;

		if (role == SG_ROLE_ATTACK)
		{
			edict_t *fent = SG_EnemyFlag(team);

			if (fent &&
			    SG_DistXY(fent->s.origin, e->s.origin) < 512.0f &&
			    SG_CanSee(e, fent->s.origin, 16.0f))
				flag_los = true;
		}

		if ((role == SG_ROLE_ATTACK || role == SG_ROLE_CARRY) &&
		    bot->seed >= 0 &&
		    ((goal_field[bot->seed] < SG_FIELD_INF &&
		      goal_field[bot->seed] < 400) || flag_los))
		{
		bestlink = -1;
		bot->terminal = true;

		/*
		 * THE CLEAN GRAB. Fifty-one of fifty-four carriers died at a
		 * median five percent of the way home (waves 111-113) --
		 * grabbing a hot room hands the flag to the respawn stream
		 * within seconds. A human clears the room first. An attacker
		 * inside touch range now holds at the threshold while a
		 * defender is believed alive within 900 of the stand -- combat
		 * runs free from the hold, the room fight happens BEFORE the
		 * grab -- and takes the flag the moment the room dies (the
		 * surge cancels every hold when a defender drops). Ten seconds
		 * caps the patience: a stalemate grab beats no grab.
		 */
		if (role == SG_ROLE_ATTACK)
		{
			int s3, room = 0;

			for (s3 = 0; s3 < SG_MAX_ENEMY_TRACK; s3++)
			{
				sg_belief_enemy_t *en3 = &sg_caco_enemies[SG_TeamIdx(team)][s3];
				vec3_t dd3;

				/* strict mode remembers twice as long: a sentry who
				 * ducks behind the pedestal for four seconds vanished
				 * from this count while remaining entirely alive, and
				 * the "cleared" room killed its carrier at 5%% of the
				 * route (waves 151-153). Absence of sighting is not
				 * evidence of death; eight seconds is patience, not
				 * paranoia. */
				if (en3->client < 0 || en3->seed < 0 ||
				    SG_AgeAtLeast(en3->seen_time,
				        (sg_cv.strictgrab->value
				             ? 8.0f : 4.0f)))
					continue;
				VectorSubtract(SG_Rune()->seeds[en3->seed].origin,
				               e->s.origin, dd3);
				if (VectorLength(dd3) < 900.0f)
					room++;
			}

			/*
			 * THE UNACCOUNTED MAN (strict only). The killer-recency
			 * census (waves 151-154): ten of thirteen carrier killers
			 * had not recently died -- live defenders the sighting
			 * census never saw, not the respawn stream. A room cannot
			 * be SIGHTED clear; but the scoreboard is public: count
			 * the enemy roster, subtract everyone believed anywhere
			 * fresh, and if a man is missing from the ledger, assume
			 * exactly one of the missing is home. The 20s patience
			 * valve still forces the grab eventually.
			 */
			if (sg_cv.strictgrab->value)
			{
				int s8, esz = 0, accounted = 0, i8;

				for (i8 = 0; i8 < game.maxclients; i8++)
				{
					edict_t *pe = g_edicts + 1 + i8;

					if (pe->inuse && pe->client &&
					    pe->client->ctf.teamnum == SG_EnemyTeam(team))
						esz++;
				}
				for (s8 = 0; s8 < SG_MAX_ENEMY_TRACK; s8++)
				{
					sg_belief_enemy_t *en8 =
					    &sg_caco_enemies[SG_TeamIdx(team)][s8];

					if (en8->client >= 0 &&
					    SG_AgeUnder(en8->seen_time, 8.0f))
						accounted++;
				}
				if (esz > accounted)
					room++;
			}
			bot->last_room = room;
			/*
			 * Hold only when OUTNUMBERED at the stand. Wave 114: mactf06
			 * attackers reached 250 of the flag and stole nothing all
			 * game -- the threshold hold against a single sentry is a
			 * stalemate the sentry wins by existing. One defender: take
			 * the grab and make them turn their back to chase. Two or
			 * more: the room fight first, as before.
			 */
			/*
			 * A/B, waves 118+: ALWAYS fight the room first. The killer
			 * census flipped the theory -- all 93 carrier deaths across
			 * seven waves came from SURVIVORS, zero from the respawn
			 * stream. Grabbing past a live sentry hands them a free rail
			 * into a fleeing back; the sprint never mattered. The fight
			 * happens before the flag moves, at any defender count, and
			 * the surge still grabs the instant one drops.
			 */
			if (room >= 1)
			{
				/*
				 * THE PAIR SPLITS THE SENTRY. When two attackers stand
				 * at the threshold, holding them BOTH just gives the
				 * sentry one target at a time. The lower client index
				 * fights -- holds the sentry's eyes -- and the other
				 * skips the hold entirely and circles to the grab. A
				 * sentry cannot watch both; whichever it picks loses
				 * something. Solo attackers fight first, as the killer
				 * census demands.
				 */
				int bi5, mate_holding = 0;

				for (bi5 = 0; bi5 < SG_MAXBOTS; bi5++)
				{
					sg_bot_t *mb5 = &sg_bots[bi5];

					if (!mb5->active || mb5 == bot || !mb5->ent ||
					    !mb5->ent->inuse)
						continue;
					if (mb5->ent->client->ctf.teamnum != team)
						continue;
					if (mb5->last_role == (int)SG_ROLE_ATTACK &&
					    mb5->last_goalcost >= 0 &&
					    mb5->last_goalcost < 1200 &&
					    (int)(mb5->ent - g_edicts) <
					        (int)(e - g_edicts))
						mate_holding = 1;
				}
				if (!mate_holding)
				{
					if (bot->rally_since <= 0.0f)
						SG_Mark(&bot->rally_since);
					if (SG_AgeUnder(bot->rally_since, 10.0f))
						rally_hold = true;
				}

				/*
				 * THE STRICT GRAB (sg_strictgrab 1, A/B wave 151+).
				 * Wave 150's verdict on the current doctrine: parity
				 * carriers die at a median ZERO percent of the route --
				 * at the pedestal -- because both sanctioned grabs
				 * take the flag under live guns: the 10s stalemate
				 * grab, and the pair-split circle-grab into a watched
				 * room. Strict mode holds while ANY defender is
				 * believed alive in the room, mate or no mate, twenty
				 * seconds of patience before conceding to the old
				 * rule. Three 5v5 servers run strict against two on
				 * current; the steals-vs-caps trade decides.
				 */
				if (room >= 1 &&
				    sg_cv.strictgrab->value)
				{
					/*
					 * THE CROWD VALVE (sg_crowdhold, wave 343). The 7v7
					 * forensics: carriers there die at 4.2s median with the
					 * WHOLE route left and 2+ enemies in the room -- the 20s
					 * patience expires into a crowd the room fight can never
					 * clear at that density, and the forced grab is a death
					 * sentence (1 cap in 23 carries; 5v5 converts 36%). With
					 * the valve, patience only concedes while the room holds
					 * at most the cvar's count; a fuller room re-arms the
					 * clock -- no grab into a crowd, ever.
					 */
					if (sg_cv.crowdhold->value > 0 &&
					    room > (int)sg_cv.crowdhold->value)
						SG_Mark(&bot->strict_since);
					if (bot->strict_since <= 0.0f)
						SG_Mark(&bot->strict_since);
					if (SG_AgeUnder(bot->strict_since, 20.0f))
						rally_hold = true;
				}
				else
					bot->strict_since = 0.0f;

				/*
				 * THE PRE-BREACH BOMB. Threshold duels run 99-58 against
				 * us: a posted rail beats an arriving one, structurally.
				 * The unfair tool has sat in the loadout unthrown all
				 * campaign -- five hand grenades a spawn. During a
				 * threshold fight, cook one and lob it onto the sentry's
				 * believed post THROUGH cover. No line of sight, no duel:
				 * the room softens before the breach.
				 */
				/* THE FLYING COOK (sg_flycook, wave 228): the owner
				 * cooks on approach, not at a standstill -- the last
				 * seconds of the run double as the fuse, a death
				 * drops the live grenade where the fight is, and the
				 * threshold ceremony disappears. The cook engages in
				 * motion inside the approach band; the throw target
				 * stays the stand, which is where the run points
				 * anyway, so the view-pull steers nothing wrong. */
				if (rally_hold &&
				    bot->nade_phase == 0 &&
				    SG_TimerReady(bot->nade_next))
				{
					static gitem_t *nades;
					int s7;

					if (!nades)
						nades = FindItem("Grenades");
					if (nades &&
					    e->client->pers.inventory[ITEM_INDEX(nades)] > 0)
					{
						for (s7 = 0; s7 < SG_MAX_ENEMY_TRACK; s7++)
						{
							sg_belief_enemy_t *en7 =
							    &sg_caco_enemies[SG_TeamIdx(team)][s7];
							vec3_t nd7;
							float nl7;

							if (en7->client < 0 || en7->seed < 0 ||
							    SG_AgeAtLeast(en7->seen_time, 5.0f))
								continue;
							VectorSubtract(
							    SG_Rune()->seeds[en7->seed].origin,
							    e->s.origin, nd7);
							nl7 = VectorLength(nd7);
							if (nl7 > 250.0f && nl7 < 800.0f)
							{
								/* NADEPOP's verdict on the stand doctrine
								 * (wave 140): 25 pops, mean 4847 units
								 * from the nearest enemy, two inside the
								 * blast radius -- the airburst shells a
								 * pedestal nobody stands on. The bomb now
								 * takes a FRESH sighting (under 2s) at
								 * face value and falls back to the stand
								 * only when the belief has gone stale --
								 * the ghost was the wrong target at ten
								 * seconds old, not at one. */
								if (SG_AgeUnder(en7->seen_time, 2.0f))
									VectorCopy(
									    SG_Rune()->seeds[en7->seed].origin,
									    bot->nade_at);
								else
								{
									edict_t *nf = SG_FlagStand(team, false);

									if (nf)
										VectorCopy(nf->s.origin,
										           bot->nade_at);
									else
										VectorCopy(
										    SG_Rune()->seeds[en7->seed].origin,
										    bot->nade_at);
								}
								nades->use(e, nades);
								bot->nade_phase = 1;
								SG_TimerArm(&bot->nade_until, 0.5f);
								break;
							}
						}
					}
				}
			}
		}
	}
	}

	/*
	 * Commitment. The composed surface has saddles -- goal one way, a
	 * shotgun the other, health a third, the item terms refreshed every
	 * second -- and a per-frame argmin at a saddle flaps between near-equal
	 * links. Both t2 attackers churned a full match at one such point
	 * (seeds 429/430, the room north of the rotating door). A chosen step
	 * is HELD until it finishes, times out, or gets shelved; the surface
	 * proposes, the body disposes.
	 */
	if (bot->commit_link >= 0 && bot->commit_link < SG_Rune()->hdr.num_links)
	{
		rune_link_t *cl = &SG_Rune()->links[bot->commit_link];
		qboolean drop_commit = false;
		int b;

		VectorSubtract(SG_Rune()->seeds[cl->to].origin, e->s.origin, d);
		if (bot->seed == cl->to || VectorLength(d) < 48.0f)
			drop_commit = true;             /* arrived: step complete */
		/* or overachieved: hook landings scatter up to ~234 units from the
		 * dest seed -- if the field already prices this spot at or below
		 * the destination, the step served its purpose (holding on would
		 * re-fire the hook from its own landing zone; match 6 bounced at
		 * goal 9979 all game doing exactly that) */
		if (goal_field[bot->seed] <= goal_field[cl->to])
			drop_commit = true;
		if (SG_TimerReadyStrict(bot->commit_until))
			drop_commit = true;
		for (b = 0; b < SG_BL_MAX; b++)
			if (bot->bl_link[b] == bot->commit_link &&
			    SG_TimerPending(bot->bl_until[b]))
				drop_commit = true;
		if (drop_commit)
			bot->commit_link = -1;
		else
			bestlink = bot->commit_link;
	}
	if (bot->commit_link < 0 && bestlink >= 0)
	{
		bot->commit_link = bestlink;
		SG_TimerArm(&bot->commit_until, 3.0f);
	}

	/*
	 * A rail attempt outranks the argmin outright. Wave 53's ledger: 16
	 * RAILTRY, 1 RAILFAIL, 0 RAILWIN -- fifteen attempts silently stood
	 * down because futility and the shelf reshaped the surface mid-walk
	 * and the argmin handed back a different link before the proof's line
	 * got walked. The retry exists precisely because the surface's local
	 * answer failed here; letting the surface interrupt it is circular.
	 */
	if (bot->rail_stage > 0 && bot->rail_link >= 0 &&
	    bot->rail_link < SG_Rune()->hdr.num_links)
	{
		int b3;
		qboolean shelved = false;

		for (b3 = 0; b3 < SG_BL_MAX; b3++)
			if (bot->bl_link[b3] == bot->rail_link &&
			    SG_TimerPending(bot->bl_until[b3]))
				shelved = true;
		if (!shelved)
			bestlink = bot->rail_link;
		else
			bot->rail_stage = 0;
	}

	/*
	 * Progress watch. The same link chosen for four seconds while the bot
	 * stays inside a 96-unit ball is an orbit -- a lip behind a railing,
	 * a door the rune cannot see, a ledge the feelers cannot round. The
	 * cause does not matter here: shelve the link for thirty seconds and
	 * the surface reroutes through the next-best gradient. (Field orbited
	 * one drop lip for a full match; the generator fix removes that class,
	 * this removes every class.)
	 */
	/*
	 * Route through a door already known dead: no 4-second trial needed,
	 * the verdict is in. Shelve on sight -- one link per frame drains a
	 * 25-link doorway fan in seconds, where the watch alone drained it
	 * slower than the shelf refilled (Trace, 117 shelves at seed 662,
	 * match 12: a shelve-expire-reshelve treadmill).
	 */
	if (bot->deaddoor_ahead)
	{
		/*
		 * Shelve ONLY a link that actually heads into the dead door. The
		 * first version shelved whatever bestlink was current whenever a
		 * dead door lay on the goal line -- at ten frames a second that
		 * emptied seed 429's whole fan into a 120-second shelf and left
		 * the bot orbiting on link=-1 for 23 aggregate minutes (batch,
		 * ports 28446-49). The goal line pointing at a door is a fact
		 * about the door; it is not a verdict on a link that leaves in
		 * another direction.
		 */
		if (bestlink >= 0)
		{
			vec3_t to_door, to_dest;
			float dy, ly;

			VectorSubtract(bot->deaddoor_spot, e->s.origin, to_door);
			VectorSubtract(SG_Rune()->seeds[SG_Rune()->links[bestlink].to].origin,
			               e->s.origin, to_dest);
			dy = atan2f(to_door[1], to_door[0]);
			ly = atan2f(to_dest[1], to_dest[0]);
			dy = dy - ly;
			while (dy > M_PI) dy -= 2.0f * (float)M_PI;
			while (dy < -M_PI) dy += 2.0f * (float)M_PI;
			if (fabsf(dy) < 0.6f)       /* ~35 degrees: the doorway cone */
			{
				int b, oldest = 0;

				for (b = 0; b < SG_BL_MAX; b++)
					if (bot->bl_until[b] < bot->bl_until[oldest])
						oldest = b;
				bot->bl_link[oldest] = bestlink;
				SG_TimerArm(&bot->bl_until[oldest], 120.0f);
			}
		}
		bot->deaddoor_ahead = false;    /* one frame's verdict, once */
	}

	if (bestlink >= 0 && bestlink == bot->watch_link &&
	    !(role == SG_ROLE_DEFEND && goal_field[bot->seed] < 1500) &&
	    /* 1500, not 400: a PATROLLING defender runs full speed inside a
	     * confined orbit -- Slip circled seed 1704 at 250 u/s, goal 700,
	     * and the 400 cutoff fed the whole patrol to the shelf (iter 44,
	     * lmctf58: 314 firings, defense routes in rags). The patrol
	     * radius is part of the post. */
	    !bot->door_held_last && !bot->mate_block_last)
	{
		/* door_held_last: standing at a door on command is not the link's
		 * failure -- billing it to the link shelved seed 429's whole fan
		 * through the WATCH path even after the fast-drain was gated
		 * (batch 2: 753 attacker-seconds on link=-1) */
		VectorSubtract(e->s.origin, bot->watch_org, d);
		if (VectorLength(d) > 96.0f)
		{
			VectorCopy(e->s.origin, bot->watch_org);
			SG_Mark(&bot->watch_since);
		}
		else if (SG_AgeOver(bot->watch_since, 4.0f))
		{
			int b, oldest = 0;

			for (b = 0; b < SG_BL_MAX; b++)
				if (bot->bl_until[b] < bot->bl_until[oldest])
					oldest = b;
			bot->bl_link[oldest] = bestlink;
			/* an honest traversal failure: 45s. The 120s figure is for
			 * links proven to head into a dead door, nothing else. */
			SG_TimerArm(&bot->bl_until[oldest], 45.0f);
			if (sg_cv.debug->value)
				sg_host.dprint("SHELVE %s link=%d at seed=%d\n",
				           e->client->pers.netname, bestlink, bot->seed);
			bot->watch_link = -1;
		}
	}
	else
	{
		bot->watch_link = bestlink;
		SG_Mark(&bot->watch_since);
		VectorCopy(e->s.origin, bot->watch_org);
	}

	/*
	 * The identity watch above cannot see a flap: commit holds a link for
	 * three seconds, the argmin at a saddle then hands back the OTHER
	 * near-equal link, and the four-second clock resets every swap while
	 * the body stands still for minutes (Fiend and Trace, one drop lip
	 * each, the whole of lmctf01 iter 41). This ball is on the body. Parked
	 * eight seconds -> shelve whatever link is current, then one more every
	 * two seconds while still parked: the flap-set at a saddle is two or
	 * three links and drains in seconds, nothing like the doorway-fan
	 * drain this system got burned by (that one shelved at 10Hz).
	 */
	/*
	 * THE REARGUARD. Waves 88-90: nineteen steals, zero captures, ten of
	 * fifteen carriers dead within ten percent of home -- killed in the
	 * flag room by the respawn stream while their escort dutifully
	 * followed them toward the exit, duplicating the carrier's path when
	 * the carrier needed the ROOM plugged behind it. For eight seconds
	 * after a grab, an escort still deep in the enemy base stands and
	 * fights where it is -- combat runs free, navigation holds -- and
	 * the respawn stream meets a gun instead of a fleeing back. Then it
	 * escorts, as before, in country where escorting means something.
	 */
	/*
	 * ...and not only the ESCORT. The route-fraction census (waves
	 * 141-148, 83 parity carries): 77%% of carriers die inside the first
	 * quarter of the route, median at 3%% -- the room, not the road. At
	 * the grab moment the fighter still wears ATTACK, and an attacker's
	 * post-grab field is the enemy flag ON OUR CARRIER'S BACK: it pulls
	 * him into the carrier's wake out the same door, a second target on
	 * one rail line. The hold now catches ATTACK too -- the fighter
	 * plugs the room he is already standing in, which was the pair-split
	 * doctrine's second half all along.
	 */
	if ((role == SG_ROLE_ESCORT || role == SG_ROLE_ATTACK) &&
	    SG_AgeUnder(sg_grab_time[SG_TeamIdx(team)], 8.0f) &&
	    bot->seed >= 0)
	{
		int *att = (team == CTF_TEAM_RED) ? sg_fields.to_blue_flag
		                                  : sg_fields.to_red_flag;

		/* escorts hold at 3000 as before; an ATTACKER holds only from
		 * INSIDE the room (the threshold fighter reads under ~1200) --
		 * at 3000 the hold would freeze attackers still mid-corridor,
		 * parked on the rail lines they were built to cross */
		if (att && att[bot->seed] < (role == SG_ROLE_ATTACK ? 1500
		                                                    : 3000))
		{
			rally_hold = true;      /* stand and fight: the room is the job */
			/* once per engagement, not per frame: the hold's own
			 * evidence trail -- wave 149 moved no census and nothing
			 * could say whether the plug ever engaged at all */
			if (bot->rally_since <= 0.0f &&
			    sg_cv.debug->value)
				sg_host.dprint("PLUG %s role=%d cost=%d\n",
				           e->client->pers.netname, (int)role,
				           att[bot->seed]);
			if (bot->rally_since <= 0.0f)
				SG_Mark(&bot->rally_since);
		}
	}

	/*
	 * THE FLAG HANDOFF (sg_handoff, census gap 10). The owner's rulings:
	 * "flag handoff can use drop, but it would be better to use the buzzmod
	 * toss", then "it is another valid way to pass the flag besides drop"
	 * and "we should probably limit the range of the flag toss a bit". A
	 * carrier about to die gives the flag to a teammate who is nearer home
	 * than it is, instead of dying with it in the open and handing the
	 * defense a free return.
	 *
	 * WHAT drop AND toss ACTUALLY DO HERE. They are the same act. "toss
	 * flag" is special-cased in Cmd_ItemToss_f (g_cmds.c) into
	 * ctf_playerdropflag because the flag item's toss slot is NULL; "drop
	 * flag" reaches ctf_playerdropflag through the item's drop slot
	 * (g_items.c). Both end in ctf_TossEnt, which lobs at a FIXED
	 * forward*200 with z=300 -- about 150 units of ground range, not
	 * settable from the command. So the command word is the owner's
	 * preference, and the RANGE CAP below is the whole of "limit the range
	 * a bit": the carrier does not attempt a pass it cannot make. Widening
	 * the lob itself would mean editing ctf_TossEnt, which also throws
	 * runes and every death-drop -- game code, not a bot decision.
	 *
	 * The receiver is priced on the CARRIER'S OWN home field (goal_field is
	 * to_{red,blue}_flag for a CARRY bot, set above and not reassigned for
	 * this role), so "nearer home" means nearer along the route rather than
	 * nearer in a straight line through a wall, and the line of sight is
	 * checked so the flag is not lobbed into a doorframe.
	 */
	if (sg_cv.handoff->value &&
	    role == SG_ROLE_CARRY && goal_field &&
	    bot->seed >= 0 && goal_field[bot->seed] < SG_FIELD_INF &&
	    SG_TimerReady(bot->handoff_next) &&
	    (bot->engaged_last || duel))
	{
		/*
		 * The bail-out health, skill-scaled: skill 4 backs itself to live
		 * and holds the flag down to 35; skill 0 lets go at 60. The low
		 * skill passes EARLIER on purpose -- it is the one least likely to
		 * finish the run, so its flag is worth more in better hands.
		 */
		float	sk = (float)SG_CombatSkill(e) / 100.0f;     /* 0 .. 4 */
		float	hp_thr = 60.0f + (35.0f - 60.0f) * (sk / 4.0f);

		if ((float)e->health < hp_thr)
		{
			int		mi, best = -1;
			int		my_cost = goal_field[bot->seed];
			int		best_cost = my_cost;
			vec3_t	eye;

			VectorCopy(e->s.origin, eye);
			eye[2] += e->viewheight;

			for (mi = 1; mi <= game.maxclients; mi++)
			{
				edict_t	*me = g_edicts + mi;
				vec3_t	md;
				float	mdist;
				int		ms, mc;
				trace_t	mtr;

				if (me == e || !me->inuse || !me->client)
					continue;
				if (me->deadflag || me->health <= 0)
					continue;
				if (me->client->ctf.teamnum != team)
					continue;
				/* observers and the not-yet-joined cannot receive */
				if (!ctf_validateplayer(me, CTF_TEAM_ANYTEAM))
					continue;

				VectorSubtract(me->s.origin, e->s.origin, md);
				mdist = VectorLength(md);
				if (mdist > 350.0f)     /* the cap (owner's ruling) */
					continue;

				ms = Rune_NearestSeed(SG_Rune(), me->s.origin);
				if (ms < 0 || goal_field[ms] >= SG_FIELD_INF)
					continue;
				mc = goal_field[ms];
				/* beat the carrier by a real margin, not field noise */
				if (mc + 300 >= best_cost)
					continue;

				mtr = sg_host.trace(eye, NULL, NULL, me->s.origin, e,
				               MASK_SOLID);
				if (mtr.fraction < 1.0f && mtr.ent != me)
					continue;

				best = mi;
				best_cost = mc;
			}

			if (best > 0)
			{
				edict_t	*re = g_edicts + best;
				vec3_t	hd;
				float	hy, hdist;
				char	*word;

				VectorSubtract(re->s.origin, e->s.origin, hd);
				hdist = VectorLength(hd);
				hy = atan2f(hd[1], hd[0]) * 180.0f / (float)M_PI;

				/*
				 * ctf_TossEnt aims the lob with client->v_angle, NOT with
				 * the usercmd -- pmove has not run yet this frame, so
				 * steering by cmd->angles alone would throw the flag along
				 * LAST frame's facing. Set the view angle the release
				 * actually reads (pitch flat, so the arc clears the floor
				 * instead of burying itself), and set the usercmd too so
				 * the body ends the frame facing where it threw.
				 */
				e->client->v_angle[YAW] = hy;
				e->client->v_angle[PITCH] = 0.0f;
				cmd->angles[YAW] = ANGLE2SHORT(hy)
				    - e->client->ps.pmove.delta_angles[YAW];

				/* the owner named both words: near enough to place it in
				 * a mate's hands is a drop, past that it is a throw */
				word = (hdist <= 150.0f) ? "drop" : "toss";
				SG_BotClientCommand((int)(e - g_edicts) - 1,
				                    word, "flag", NULL);

				/*
				 * The carry gauges belong to the carry that just ended --
				 * the same three the grab resets. The role itself follows
				 * next think, because SG_Role derives CARRY from actually
				 * holding the flag.
				 */
				bot->carry_startcost = -1;
				bot->carry_bestcost = -1;
				bot->carry_lost_at = 0.0f;
				SG_TimerArm(&bot->handoff_next, 10.0f);

				if (sg_cv.debug->value)
					sg_host.dprint("HANDOFF %s -> %s %s dist=%.0f cost "
					           "%d->%d hp=%d thr=%.0f\n",
					           e->client->pers.netname,
					           re->client->pers.netname, word, hdist,
					           my_cost, best_cost, e->health, hp_thr);
			}
			else if (sg_cv.debug->value &&
			         SG_TimerReady(bot->next_report - 0.9f))
				sg_host.dprint("HANDOFF %s no receiver hp=%d thr=%.0f\n",
				           e->client->pers.netname, e->health, hp_thr);
		}
	}

	/*
	 * THE RUNE HANDOFF (sg_runetoss, wave 240 -- the owner's recovered
	 * "extremely important": a teammate holding a defensive rune gives
	 * it to the carrier). A bot with RESIST or REGEN, within 300 of our
	 * live carrier who holds nothing better, faces the carrier for one
	 * frame and drops the rune into its path; the carrier's own item
	 * pricing (SG_FC_RUNE) takes it from the floor. One toss per bot
	 * per 20s; combat frames exempt -- a fight is not the moment.
	 */
	if (sg_cv.runetoss->value &&
	    role != SG_ROLE_CARRY && !duel &&
	    e->client->rune &&
	    (e->client->rune->runetype == RUNE_RESIST ||
	     e->client->rune->runetype == RUNE_REGEN) &&
	    SG_TimerReady(bot->runetoss_next))
	{
		sg_belief_carrier_t *rc = &sg_caco_team_belief.carrier[SG_TeamIdx(team)];

		if (rc->client >= 0)
		{
			edict_t *ce = g_edicts + 1 + rc->client;

			if (ce->inuse && ce->client && ce->health > 0 &&
			    (!ce->client->rune ||
			     (ce->client->rune->runetype != RUNE_RESIST &&
			      ce->client->rune->runetype != RUNE_REGEN)))
			{
				vec3_t rd14;

				VectorSubtract(ce->s.origin, e->s.origin, rd14);
				if (sg_cv.debug->value &&
				    SG_TimerReady(bot->next_report - 0.9f))
					sg_host.dprint("RTCAND %s dist=%.0f\n",
					           e->client->pers.netname,
					           VectorLength(rd14));
				if (VectorLength(rd14) < 400.0f)
				{
					/* face the carrier for the toss frame: the
					 * flick, same as the bomb release */
					float ry = atan2f(rd14[1], rd14[0])
					           * 180.0f / (float)M_PI;

					cmd->angles[YAW] = ANGLE2SHORT(ry)
					    - e->client->ps.pmove.delta_angles[YAW];
					Drop_Rune(e, e->client->rune->item);
					SG_TimerArm(&bot->runetoss_next, 20.0f);
					if (sg_cv.debug->value)
						sg_host.dprint("RUNETOSS %s to %s\n",
						           e->client->pers.netname,
						           ce->client->pers.netname);
				}
			}
		}
	}

	/*
	 * HOLD SHORT OF AN UNCAPPABLE STAND (both-flags doctrine, wave 175).
	 * A carrier whose own flag is astray cannot score by touching the
	 * stand -- but it marched there anyway and camped the most
	 * predictable spot on the map until something killed it. Now it
	 * closes to earshot of home (2500 of field) and HOLDS off-stand,
	 * fighting from wherever it stands, until the team returns the
	 * flag; the last steps happen when they can score. The standoff
	 * breaks on exactly one event, and ours must survive to convert it.
	 */
	if (role == SG_ROLE_CARRY &&
	    sg_caco_team_belief.flag[SG_TeamIdx(team)].state == SG_FLAG_ASTRAY &&
	    bot->seed >= 0 && goal_field[bot->seed] < SG_FIELD_INF &&
	    goal_field[bot->seed] < 2500)
	{
		rally_hold = true;
		if (sg_cv.debug->value &&
		    SG_TimerReady(bot->next_report - 0.9f))
			sg_host.dprint("CARRYHOLD %s cost=%d\n",
			           e->client->pers.netname, goal_field[bot->seed]);
	}

	/*
	 * A railhold clock AHEAD of the level clock is a previous map's
	 * timestamp: level.time restarts at zero on changelevel and sg_bots[]
	 * does not, the same trap the tactical waypoint's tac_time documents.
	 * Stale by definition, and cleared before anything below reads it.
	 */
	if (bot->railhold_since > level.time ||
	    bot->railhold_next > level.time + SG_RAIL_HOLD_GAP)
	{
		bot->railhold_since = 0.0f;
		bot->railhold_next = 0.0f;
		bot->railhold_enemy = -1;
	}

	/*
	 * TIMING THE CROSSING (sg_railrhythm). Last of the holds and
	 * deliberately the weakest of them: everything above -- the room
	 * fight, the plug, the standoff, a defender's post -- is a decision
	 * about the game, and this is a decision about one doorway. It yields
	 * to all of them and never argues with the terminal brake or an item
	 * errand.
	 *
	 * THE SHAPE OF IT. The step the surface just chose enters a believed
	 * railer's sight line, this bot is standing somewhere that same
	 * railer cannot see, and his last heard shot is old enough that the
	 * gun is loaded again. That is the moment a human waits -- not for
	 * long, and not for the shot to be aimed at him. Any rail going off
	 * anywhere opens the window (SG_RailCold reads the shot table, which
	 * the ear stamps for every slug in the PHS), and when it does the
	 * hold releases on the next frame and the crossing happens inside the
	 * reload.
	 *
	 * TWO TRACES, and only when a railer is already known and a step is
	 * already chosen. Both are the approach-cover ray: candidate seed to
	 * believed post, and body to believed post.
	 *
	 * THE CAP IS THE POINT. Patience runs 0.8s at skill 0 to 1.5s at
	 * skill 4, a carrier takes the top of that band, and the wait cannot
	 * be renewed while it is running -- so the worst this feature can
	 * cost a capture is a second and a half of one leg, once, against a
	 * lane that was going to be crossed in front of a loaded rail.
	 */
	if (rail_seed >= 0 && rail_client >= 0 && bestlink >= 0 &&
	    !rally_hold && !precision && bot->lead_ent == 0 &&
	    bot->seed >= 0 &&
	    (bot->railhold_since > 0.0f || SG_TimerReady(bot->railhold_next)))
	{
		vec3_t	rthr, rstep, rbody;
		trace_t	rtr;

		VectorCopy(SG_Rune()->seeds[rail_seed].origin, rthr);
		rthr[2] += 22.0f;
		VectorCopy(SG_Rune()->seeds[SG_Rune()->links[bestlink].to].origin,
		           rstep);
		rstep[2] += 22.0f;
		VectorCopy(e->s.origin, rbody);
		rbody[2] += e->viewheight;

		/* is the crossing imminent -- does the next step enter his lane? */
		rtr = sg_host.trace(rstep, NULL, NULL, rthr, e, MASK_SOLID);
		if (rtr.fraction >= 1.0f)
		{
			/* and is there cover to wait in, here, right now? A bot
			 * already standing in his line gains nothing by stopping in
			 * it: waiting in the open is the worst of both. */
			rtr = sg_host.trace(rbody, NULL, NULL, rthr, e, MASK_SOLID);
			if (rtr.fraction < 1.0f && !SG_RailCold(team, rail_client))
			{
				if (bot->railhold_since <= 0.0f)
				{
					float sk = (float)SG_CombatSkill(e) / 100.0f;  /* 0..4 */

					SG_Mark(&bot->railhold_since);
					bot->railhold_patience =
					    (role == SG_ROLE_CARRY)
					        ? 1.5f
					        : 0.8f + (1.5f - 0.8f) * (sk / 4.0f);
					if (sg_cv.debug->value)
						sg_host.dprint("RAILHOLD %s at seed=%d waits on "
						           "cl=%d seed=%d patience=%.1f%s\n",
						           e->client->pers.netname, bot->seed,
						           rail_client, rail_seed,
						           bot->railhold_patience,
						           (role == SG_ROLE_CARRY)
						               ? " carrier" : "");
				}
				/* re-stamped every waiting frame, not only at the arm:
				 * if the freshest railer changes identity mid-wait the
				 * release line must name the man actually waited out --
				 * and the patience clock deliberately does NOT restart,
				 * or a room with two railers in it would have no cap */
				bot->railhold_enemy = rail_client;
				if (SG_AgeUnder(bot->railhold_since,
				    bot->railhold_patience))
					rail_hold = true;
			}
		}
	}
	if (!rail_hold && bot->railhold_since > 0.0f)
	{
		/* one line per crossing, and it says which of the two things
		 * ended the wait: the rail going off (the window is open and the
		 * crossing is timed) or the patience running out (humans do not
		 * wait forever, and neither does this) */
		if (sg_cv.debug->value)
			sg_host.dprint("RAILCROSS %s waited %.1fs on cl=%d (%s)\n",
			           e->client->pers.netname,
			           SG_Age(bot->railhold_since),
			           bot->railhold_enemy,
			           SG_RailCold(team, bot->railhold_enemy)
			               ? "window" : "patience");
		bot->railhold_since = 0.0f;
		bot->railhold_enemy = -1;
		SG_TimerArm(&bot->railhold_next, SG_RAIL_HOLD_GAP);
	}

	/*
	 * THE UNSTICK OF LAST RESORT. A rope through a doorway parked a bot
	 * on a wall ledge off the navigable mesh (wave 97, screenshot in
	 * hand) and every clever layer beneath this line -- watchdog,
	 * escape, futility, rail -- churned without physically freeing it.
	 * Fifteen seconds of true zero displacement, standing exempted only
	 * for a defender on post or a rally hold, and the bot does what
	 * every stuck player has done since 1997: kill, respawn, rejoin the
	 * war. A death costs less than a statue.
	 */
	VectorSubtract(e->s.origin, bot->wedge_org, d);
	if (VectorLength(d) > 96.0f)
	{
		VectorCopy(e->s.origin, bot->wedge_org);
		SG_Mark(&bot->wedge_since);
	}
	else if (SG_AgeOver(bot->wedge_since, 15.0f) &&
	         !(role == SG_ROLE_DEFEND &&
	           goal_field[bot->seed >= 0 ? bot->seed : 0] < 1500) &&
	         /* A LIVE CARRIER IS NEVER SUICIDED (carry forensics, 791
	          * episodes): 12 of the 19 parity carries that REACHED
	          * within 300u of home ended as WEDGEKILL orbits at the
	          * stand -- zero damage, no enemy inside 900u. The wedge
	          * valve was executing the very carries everything else
	          * exists to produce. The progress guard's shelf wipe is
	          * the carrier's remedy; a death hands the flag back. */
	         role != SG_ROLE_CARRY &&
	         /* and a rail-rhythm wait is the same class of standing as a
	          * rally: parked on purpose, briefly, by a bot that knows
	          * exactly why. It cannot reach fifteen seconds on its own --
	          * 1.5s of wait per 5.5s of refractory -- but a bot must never
	          * be killed for a stand this file asked it to make */
	         bot->railhold_since <= 0.0f &&
	         bot->rally_since <= 0.0f)
	{
		sg_host.dprint("WEDGEKILL %s at (%.0f %.0f %.0f)\n",
		           e->client->pers.netname, e->s.origin[0],
		           e->s.origin[1], e->s.origin[2]);
		Cmd_Kill_f(e);
		SG_Mark(&bot->wedge_since);
		tc->think_over = true;
		return bestlink;
	}

	VectorSubtract(e->s.origin, bot->stag_org, d);
	if (VectorLength(d) > 96.0f || !bot->nav_drove || bot->engaged_last)
	{
		/*
		 * Not displacement alone: the clock runs ONLY through frames where
		 * navigation was actually driving the legs and no fight owned the
		 * body. The first cut counted every parked second -- corner holds,
		 * duels, posts -- and shelved the fleet's routes to rags in one
		 * wave (iter 43: 784 firings, zero steals anywhere, kills gutted).
		 * Parked-while-driving is the deadlock class; parked-on-purpose
		 * is not a link's fault.
		 */
		VectorCopy(e->s.origin, bot->stag_org);
		SG_Mark(&bot->stag_since);
	}
	else if (bestlink >= 0 &&
	         !(role == SG_ROLE_DEFEND && goal_field[bot->seed] < 1500) &&
	    /* 1500, not 400: a PATROLLING defender runs full speed inside a
	     * confined orbit -- Slip circled seed 1704 at 250 u/s, goal 700,
	     * and the 400 cutoff fed the whole patrol to the shelf (iter 44,
	     * lmctf58: 314 firings, defense routes in rags). The patrol
	     * radius is part of the post. */
	         !bot->door_held_last && !bot->mate_block_last &&
	         SG_AgeOver(bot->stag_since, 8.0f) &&
	         SG_TimerReady(bot->stag_next))
	{
		int b, oldest = 0;

		/*
		 * A RUN link earns one retry THE PROOF'S WAY before the shelf.
		 * Seed 327's passage is a slit the phantom threads dead-center
		 * from the seed origin -- proofs deviate under 48 units, so no
		 * waypoint, and the feelers deflect off the slit's edges away
		 * from the one line that works (iter 51: 135 firings, zero
		 * waypointed links to store). Rail mode walks to the from-seed
		 * exactly as the proof did, then drives the straight line with
		 * the fan silenced. If THAT fails, the shelf and the futility
		 * lesson follow as before.
		 */
		if (SG_Rune()->links[bestlink].action == RL_RUN &&
		    bot->rail_link != bestlink)
		{
			bot->rail_link = bestlink;
			bot->rail_stage = 1;
			SG_TimerArm(&bot->rail_until, 4.0f);
			if (sg_cv.debug->value)
				sg_host.dprint("RAILTRY %s link=%d seed=%d\n",
				           e->client->pers.netname, bestlink, bot->seed);
			SG_TimerArm(&bot->stag_next, 2.0f);
			VectorCopy(e->s.origin, bot->stag_org);
			SG_Mark(&bot->stag_since);
			goto stag_done;
		}
		for (b = 0; b < SG_BL_MAX; b++)
			if (bot->bl_until[b] < bot->bl_until[oldest])
				oldest = b;
		bot->bl_link[oldest] = bestlink;
		SG_TimerArm(&bot->bl_until[oldest], 45.0f);
		SG_TimerArm(&bot->stag_next, 2.0f);
		bot->commit_link = -1;
		SG_TeachLinkFutility(bestlink); /* the LINK failed, not the ground */
		/*
		 * A U-pocket defeats even the side latch: each detour side walks
		 * into the pocket's own wall and the latch just alternates walls
		 * (iter 48: Field, 102 firings at lmctf01 seed 1239, one pocket,
		 * one game). By the time the watchdog fires, local navigation has
		 * definitively failed -- so back OUT along the reverse of the
		 * current facing for 1.2s and retry the approach from open ground.
		 */
		/* jittered: identical retreats produce identical re-approaches,
		 * and an obstacle that beats one line beats it every time */
		bot->escape_yaw = e->s.angles[YAW] + 180.0f + (float)(rand() % 81 - 40);
		SG_TimerArm(&bot->escape_until, 1.0f + (float)(rand() % 9) * 0.1f);
		if (sg_cv.debug->value)
			sg_host.dprint("STAGSHELVE %s link=%d at seed=%d\n",
			           e->client->pers.netname, bestlink, bot->seed);
	}
stag_done:
	bot->nav_drove = false;         /* the movement code below re-arms it */
	/* consumed by the watch above; the feelers re-raise it if the body is
	 * still there this frame */
	bot->mate_block_last = false;

	/*
	 * The wide-orbit detector. On arriving at a seed, check the ring: if
	 * this seed was here within 30 seconds and the goal has not improved
	 * since, the route is a cycle -- shelve the link about to be taken
	 * from it. Loops wider than the watch's ball (the lmctf01 carrier's
	 * 250-unit hook triangle) die here; honest revisits (a defender
	 * patrolling, a fight's back-and-forth) pass because their goal
	 * values move or their clocks expire.
	 */
	{
		static int last_seed_seen[SG_MAXBOTS];
		int me = (int)(bot - sg_bots);
		int gv = (goal_field[bot->seed] < SG_FIELD_INF)
		             ? goal_field[bot->seed] : 0x7ffffff;
		int v;

		/* every live entry keeps the best goal the bot has touched since
		 * that visit -- THIS is what distinguishes a loop from a route
		 * that passes a hallway twice. The first version compared the
		 * seed's own field value across visits, which is CONSTANT, and
		 * shelved every second visit on the map (campaign 2: steals
		 * 7 -> 1, lmctf03 shelves 434). */
		for (v = 0; v < SG_VISIT_RING; v++)
			if (bot->visit_seed[v] >= 0 && gv < bot->visit_min[v])
				bot->visit_min[v] = gv;

		if (me >= 0 && me < SG_MAXBOTS && bot->seed != last_seed_seen[me])
		{
			last_seed_seen[me] = bot->seed;
			/*
			 * CARRIERS ONLY. Even with the min-since test, a fighter
			 * repelled by live defense revisits without progress -- that
			 * is resistance, not a bad link, and no signal here can tell
			 * them apart (campaign 3: 4099 fires, 164 a game, routes
			 * shredded map-wide). A carrier's loop loses the flag and its
			 * fights are ones it fled; for the carrier the test is sound.
			 */
			for (v = 0; role == SG_ROLE_CARRY && v < SG_VISIT_RING; v++)
				if (bot->visit_seed[v] == bot->seed &&
				    SG_AgeUnder(bot->visit_time[v], 30.0f) &&
				    SG_AgeOver(bot->visit_time[v], 3.0f) &&
				    bot->visit_min[v] >= bot->visit_goal[v] &&
				    bestlink >= 0)
				{
					/* back where it was, and it never once got closer
					 * in between: an orbit, whatever its diameter */
					int b, oldest = 0;

					for (b = 0; b < SG_BL_MAX; b++)
						if (bot->bl_until[b] < bot->bl_until[oldest])
							oldest = b;
					bot->bl_link[oldest] = bestlink;
					SG_TimerArm(&bot->bl_until[oldest], 45.0f);
					if (sg_cv.debug->value)
						sg_host.dprint("CYCLE %s seed=%d link=%d\n",
						           e->client->pers.netname, bot->seed,
						           bestlink);
					break;
				}
			bot->visit_seed[bot->visit_head] = bot->seed;
			bot->visit_goal[bot->visit_head] = gv;
			bot->visit_min[bot->visit_head] = gv;
			SG_Mark(&bot->visit_time[bot->visit_head]);
			bot->visit_head = (bot->visit_head + 1) % SG_VISIT_RING;
		}
	}

	/*
	 * A carrier must NEVER be stranded by its own shelf: trapped with the
	 * flag is the flag lost. Every link at a finite seed on the shelf and
	 * nothing improving left? Wipe the shelf and retry the least-bad
	 * option -- an orbit risked beats a guaranteed strand (mactf06 g3:
	 * the carrier hung airborne on link=-1 at goal 6684 until the flag
	 * timed out).
	 */
	if (role == SG_ROLE_CARRY && bestlink < 0 &&
	    goal_field[bot->seed] < SG_FIELD_INF)
	{
		int b, any = 0;

		for (b = 0; b < SG_BL_MAX; b++)
			if (SG_TimerPending(bot->bl_until[b]))
				any++;
		if (any)
		{
			memset(bot->bl_until, 0, sizeof(bot->bl_until));
			if (sg_cv.debug->value)
				sg_host.dprint("CLEARSHELF %s (carrying, stranded at %d)\n",
				           e->client->pers.netname, bot->seed);
		}
	}

	/*
	 * A defender that has reached its post stands it. The stand is the
	 * surface's minimum, so descent has nowhere left to go -- pushing
	 * forwardmove into the pedestal just grinds the wall (Caco spent 66
	 * straight seconds at spd=68 doing exactly that). Inside 400ms of the
	 * post: stop, and face the seed an attacker descending on the stand
	 * would arrive through -- the neighbor whose field value sits closest
	 * above this one. Combat still owns the view the moment anyone shows.
	 *
	 * That 400 is the pin radius, and it is therefore the one live number on
	 * this path that means "willingness to hold a post" -- so it is what
	 * camp_tendency scales. Gate widens it by up to 15% and Fiend narrows it
	 * by the same: the camper settles from farther out, the roamer keeps
	 * walking until it is standing on the thing. The errand release below is
	 * untouched, so a needy bot still leaves whatever its persona says. 400
	 * exactly when no persona applies.
	 */
	/*
	 * THE PAD WAIT (sg_itemlead). An errand that has ARRIVED is the same
	 * problem the post solved: the goal field's minimum is the pedestal, and
	 * descent with nowhere left to go grinds the body into it. A player who
	 * came back early does not stand ON the pad either -- he stops short of
	 * it, where the approaches are in front of him rather than behind, and
	 * waits. The standoff is 400ms of field, which at pm_maxspeed is about
	 * 120 units, and the facing below is the post's own: the neighbour an
	 * arrival would descend through, overridden by wherever a fresh contact
	 * says the noise actually is.
	 *
	 * First in the chain, so an errand's hold outranks the defender's -- a
	 * defender on an errand is standing the pad, not the stand, and its
	 * goal field says so.
	 */
	if (bot->lead_ent > 0 && goal_field[bot->seed] < SG_LEAD_STANDOFF)
		hold_post = true;
	else if (role == SG_ROLE_DEFEND && bot->def_stand &&
	    (float)goal_field[bot->seed] < 400.0f * SG_PersonaCampScale(e))
	{
		qboolean quiet = true;
		int s;

		/*
		 * A quiet post permits an errand. Quiet means no believed
		 * contact -- eye or ear -- in six seconds; the errand means the
		 * hold releases and the surface runs, and the surface already
		 * knows the way: the defend objective pulls back toward the
		 * stand, the need-weighted item terms pull toward the armor the
		 * defender is missing, and the sum walks out, grabs, and walks
		 * back without a single scripted step. The hold only pins a
		 * defender who has nothing worth fetching or no peace to fetch
		 * it in.
		 */
		for (s = 0; s < SG_MAX_ENEMY_TRACK; s++)
			if (sg_caco_enemies[SG_TeamIdx(team)][s].client >= 0 &&
			    SG_AgeUnder(sg_caco_enemies[SG_TeamIdx(team)][s].seen_time, 6.0f))
				quiet = false;
		if (quiet &&
		    (w->item[SG_FC_ARMOR] > 0.9f || w->item[SG_FC_HEALTH] > 0.9f ||
		     w->item[SG_FC_AMMO] > 0.9f))
			goto no_hold;   /* needy and unthreatened: run the errand */

		hold_post = true;

		/*
		 * THE PATROL (sg_patrol). Measurement found posted defenders
		 * covering their no-idle obligation by pacing in place --
		 * high-effort movement going nowhere, a shuffle no human
		 * produces. A human at post either stands or walks a
		 * deliberate loop. When the post is quiet, walk one: pick a
		 * neighbouring seed that stays inside the post band, ride the
		 * proven link there, hold and face, and after a few unhurried
		 * seconds walk another leg. Contact cancels the leg the frame
		 * it appears -- the quiet test above already gates entry, and
		 * combat owns the view the moment anyone shows.
		 */
		if (sg_cv.patrol->value > 0.0f)
		{
			if (bot->patrol_seed >= 0 && bot->seed != bot->patrol_seed)
			{
				/* mid-leg: ride the direct link if one exists */
				int pli;

				for (pli = SG_Rune()->first_link[bot->seed]; pli >= 0;
				     pli = SG_Rune()->next_link[pli])
					if (SG_Rune()->links[pli].to == bot->patrol_seed &&
					    SG_Rune()->links[pli].action == RL_RUN)
					{
						bestlink = pli;
						hold_post = false;
						break;
					}
				if (hold_post)
					bot->patrol_seed = -1;  /* leg unreachable: stand */
			}
			else if (SG_TimerReady(bot->patrol_until))
			{
				/* pick the next leg: a RUN neighbour still in the band */
				int pli, cand[8], nc = 0;

				for (pli = SG_Rune()->first_link[bot->seed]; pli >= 0;
				     pli = SG_Rune()->next_link[pli])
				{
					rune_link_t *pl = &SG_Rune()->links[pli];

					if (pl->action == RL_RUN && nc < 8 &&
					    goal_field[pl->to] < SG_FIELD_INF &&
					    goal_field[pl->to] <
					        400.0f * SG_PersonaCampScale(e))
						cand[nc++] = pl->to;
				}
				if (nc > 0)
				{
					bot->patrol_seed = cand[rand() % nc];
					SG_TimerArm(&bot->patrol_until, 5.0f
					                  + random() * 7.0f);
				}
				else
					SG_TimerArm(&bot->patrol_until, 5.0f);
			}
		}
	}

	/*
	 * The facing, shared by both holds: whichever seed an arrival would
	 * descend on this one through -- the neighbour whose field value sits
	 * closest above ours. Combat still owns the view the moment anyone shows.
	 */
	if (hold_post)
	{
		int facev = 0x7fffffff, face = -1;

		for (li = SG_Rune()->first_link[bot->seed]; li >= 0;
		     li = SG_Rune()->next_link[li])
		{
			rune_link_t *l = &SG_Rune()->links[li];
			int v = goal_field[l->to];

			if (v > goal_field[bot->seed] && v < facev)
			{
				facev = v;
				face = l->to;
			}
		}
		if (face >= 0)
		{
			vec3_t	pdir, peye, pend;
			trace_t ptr;

			VectorSubtract(SG_Rune()->seeds[face].origin, e->s.origin, d);
			post_yaw = atan2f(d[1], d[0]) * 180.0f / M_PI;

			/*
			 * How far the post can SEE down that approach. WEAPONS.md 2.4-D3
			 * picks the pre-held weapon from this length and nothing else,
			 * because 1.1's spread saturation distances are hard numbers: a
			 * super shotgun is a sub-160 weapon and a railgun is the only
			 * thing that does not degrade past 900. MASK_OPAQUE is the same
			 * mask the sight gate uses (sg_caco.c:100-114), and 2000 is the
			 * engage range combat already refuses to fight beyond.
			 */
			VectorCopy(e->s.origin, peye);
			peye[2] += e->viewheight;
			pdir[0] = cosf(post_yaw * (float)M_PI / 180.0f);
			pdir[1] = sinf(post_yaw * (float)M_PI / 180.0f);
			pdir[2] = 0.0f;
			VectorMA(peye, 2000.0f, pdir, pend);
			ptr = sg_host.trace(peye, NULL, NULL, pend, e, MASK_OPAQUE);
			post_sight = 2000.0f * ptr.fraction;
		}

		/*
		 * A fresh contact on the belief table -- an ear included -- beats
		 * the static approach guess: face where the noise IS, not where
		 * the map says trouble usually comes from. The pre-held weapon
		 * keeps following post_sight; only the facing swings.
		 */
		{
			int s, best = -1;
			float bestt = 0.0f;

			for (s = 0; s < SG_MAX_ENEMY_TRACK; s++)
			{
				sg_belief_enemy_t *en = &sg_caco_enemies[SG_TeamIdx(team)][s];

				if (en->client >= 0 && en->seed >= 0 &&
				    SG_AgeUnder(en->seen_time, 4.0f) &&
				    goal_field[en->seed] < 2500 &&
				    en->seen_time > bestt)
				{
					bestt = en->seen_time;
					best = en->seed;
				}
			}
			if (best >= 0)
			{
				VectorSubtract(SG_Rune()->seeds[best].origin, e->s.origin, d);
				post_yaw = atan2f(d[1], d[0]) * 180.0f / M_PI;
			}
		}
	}
no_hold:;

	/*
	 * Combat holds a weapon whether or not anyone is in sight, and a posted
	 * defender's is chosen by the sightline above (rule D3b: pre-select, do
	 * not pre-fire -- holding the right weapon costs nothing, raising one
	 * mid-contact costs a full weapon cycle). A negative sightline is "not
	 * posted", which has to be said every frame or a bot that leaves its stand
	 * would keep pre-selecting for a post it no longer holds.
	 */
	SG_CombatPost(e, hold_post ? post_sight : -1.0f);

	/*
	 * Whether this bot may hold a corner on a target it just lost. The role
	 * decides and nothing else: an attacker and a recoverer are already going
	 * that way, a defender may watch a doorway only while it is still on its
	 * own ground -- 2500 ms of the home field, the same order as the post's own
	 * 400 and the pre-spin's 1200 -- and the carrier and its escort never do,
	 * because both have a clock running that a camp does not serve. Said every
	 * frame, so a role change ends a hold on the frame it happens.
	 */
	SG_CombatPursuit(e, (qboolean)(role == SG_ROLE_ATTACK ||
	                               role == SG_ROLE_RECOVER ||
	                               (role == SG_ROLE_DEFEND &&
	                                goal_field[bot->seed] < 2500)));

	/*
	 * The ear (and teammates' eyes) arm everyone else too: a fresh contact
	 * on the belief table within a second and a half of travel means the
	 * idle hand should already hold the weapon that meeting calls for.
	 * Range is estimated by the straight line to the believed seed --
	 * corridors bend it, but a band estimate only has to be right to
	 * within a band.
	 */
	if (!hold_post)
	{
		int s;

		for (s = 0; s < SG_MAX_ENEMY_TRACK; s++)
		{
			sg_belief_enemy_t *en = &sg_caco_enemies[SG_TeamIdx(team)][s];

			if (en->client >= 0 && en->seed >= 0 &&
			    SG_AgeUnder(en->seen_time, 3.0f) &&
			    goal_field[en->seed] < SG_FIELD_INF &&
			    sg_fields.item[0] != NULL)   /* fields alive */
			{
				vec3_t ed;
				float dist;

				VectorSubtract(SG_Rune()->seeds[en->seed].origin,
				               e->s.origin, ed);
				dist = VectorLength(ed);
				if (dist < 1200.0f)
				{
					SG_CombatAlert(e, dist);
					break;
				}
			}
		}
	}

	/*
	 * Chaingun pre-spin (WEAPONS.md 2.4-D3a): the gun fires slow for its
	 * first second of spin-up (p_weapon.c Chaingun frames), so a defender
	 * who believes an enemy is closing on the post -- a teammate SAW one
	 * recently, within ~1200ms of travel by the post's own field -- starts
	 * the barrels before the corner, trading a few bullets for the full
	 * rate at first contact. Belief only: no sighting, no spin.
	 */
	if (hold_post && e->client->pers.weapon)
	{
		static gitem_t *cgitem;
		int s;

		if (!cgitem)
			cgitem = FindItem("Chaingun");
		if (e->client->pers.weapon == cgitem)
			for (s = 0; s < SG_MAX_ENEMY_TRACK; s++)
			{
				sg_belief_enemy_t *en = &sg_caco_enemies[SG_TeamIdx(team)][s];

				if (en->client >= 0 && en->seed >= 0 &&
				    SG_AgeUnder(en->seen_time, 3.0f) &&
				    goal_field[en->seed] < 1200)
				{
					cmd->buttons |= BUTTON_ATTACK;
					break;
				}
			}
	}


	tc->rally_hold = rally_hold;
	tc->rail_hold = rail_hold;
	tc->hold_post = hold_post;
	tc->post_yaw = post_yaw;
	tc->post_sight = post_sight;
	return bestlink;
}
