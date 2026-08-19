/*
 * sg_goal.c -- what the bot is FOR this frame: the live row, the carry
 * bookends, the objective switch, the intercept surface, and the
 * approach band.  Moved verbatim from sg_arach.c in the 2026-08-12
 * standards pass; Mega_Worth and the intercept hold seed are private.
 */
#include "g_local.h"
#include "g_ctffunc.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_combat.h"
#include "slipgate/sg_chat.h"
#include "slipgate/sg_persona.h"
#include "slipgate/sg_cvars.h"
#include "slipgate/sg_util.h"
#include "slipgate/sg_bot.h"
#include "slipgate/sg_clock.h"
#include "slipgate/sg_danger.h"
#include "slipgate/sg_weights.h"
#include "slipgate/sg_lead.h"
#include "slipgate/sg_price.h"
#include "slipgate/sg_role_policy.h"
#include "slipgate/sg_rune_handoff_policy.h"
#include "slipgate/sg_strike.h"
#include "slipgate/sg_goal.h"
#include "slipgate/sg_defense_supply.h"
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_move.h"

static int intercept_field[SG_MAX_SEEDS];

#define SG_MEGA_PATIENCE	12.0f   /* seconds an offer may stand unspent */
#define SG_MEGA_BACKOFF		20.0f   /* ...and the refusal after, the pad's own
                                     * respawn (SetRespawn 20, g_items.c:596) */

/*
 * Intercept micro-positioning: being ON the carrier's escape line is the
 * naive hold -- it closes at rope speed and blocks your own team's shots.
 * The right ground sits ACROSS the motion: off the axis (the carrier
 * crosses the view laterally instead of head-on), above it (a missed
 * rocket still splashes the floor, and escape ropes mostly pull UP), and
 * beside a narrow crossing (few links out = a corridor the projection
 * says they must thread, where speed stops helping them). Scored over
 * the projection set's members and their link-neighbors; the axis is the
 * set's own deepest-to-shallowest line. Falls back to the projected seed
 * itself when the set is degenerate.
 */
#ifdef SG_GOAL_TEST
#define SG_GOAL_PRIVATE
#else
#define SG_GOAL_PRIVATE static
#endif

SG_GOAL_PRIVATE int Intercept_HoldSeed(int team, int fallback)
{
	sg_proj_t *pr = &sg_caco_proj[SG_TeamIdx(team)];
	vec3_t axis;
	float axlen, bestscore = -1.0f;
	int i, best = -1;

	if (pr->n < 2 || pr->client < 0)
		return fallback;

	VectorSubtract(SG_Rune()->seeds[pr->seed[0]].origin,
	               SG_Rune()->seeds[pr->seed[pr->n - 1]].origin, axis);
	axis[2] = 0.0f;
	axlen = VectorLength(axis);
	if (axlen < 64.0f)
		return fallback;            /* no meaningful motion to be across */
	axis[0] /= axlen; axis[1] /= axlen;

	for (i = 0; i < pr->n; i++)
	{
		int p = pr->seed[i], li, fan = 0;
		float choke;

		if (p < 0 || p >= SG_Rune()->hdr.num_seeds)
			continue;
		for (li = SG_Rune()->first_link[p]; li >= 0; li = SG_Rune()->next_link[li])
			if (Fields_ActionAdmitted(SG_Rune()->links[li].action))
				fan++;
		choke = 600.0f / (4.0f + (float)fan);

		for (li = SG_Rune()->first_link[p]; li >= 0; li = SG_Rune()->next_link[li])
		{
			if (!Fields_ActionAdmitted(SG_Rune()->links[li].action))
				continue;
			int c = SG_Rune()->links[li].to;
			vec3_t off;
			float lat, dz, score;

			VectorSubtract(SG_Rune()->seeds[c].origin,
			               SG_Rune()->seeds[p].origin, off);
			dz = off[2];
			off[2] = 0.0f;
			/* perpendicular component of the offset against the axis */
			lat = fabsf(off[0] * axis[1] - off[1] * axis[0]);
			if (lat > 250.0f)
				lat = 250.0f;
			score = lat + choke;
			if (dz > 0.0f)
				score += (dz > 200.0f) ? 200.0f : dz;
			if (score > bestscore)
			{
				bestscore = score;
				best = c;
			}
		}
	}
	return (best >= 0) ? best : fallback;
}

#undef SG_GOAL_PRIVATE

/* A rally relocation is still navigation.  It may stand on the current seed
 * or walk one proved ordinary leg; it may not point the command directly at a
 * merely nearby seed on the other side of a wall, drop, or mechanism. */
#ifdef SG_GOAL_TEST
#define SG_GOAL_PRIVATE
#else
#define SG_GOAL_PRIVATE static
#endif

SG_GOAL_PRIVATE int Rally_CoverSeed(const rune_t *r, int from)
{
	int best = -1;
	float best_distance = 0.0f;
	int link;

	if (!r || !r->seeds || !r->links || !r->first_link || !r->next_link ||
	    from < 0 || from >= r->hdr.num_seeds)
		return -1;
	if (r->seeds[from].area_hint <= 60)
		return from;

	for (link = r->first_link[from]; link >= 0; link = r->next_link[link])
	{
		const rune_link_t *candidate;
		vec3_t delta;
		float distance;
		int to;

		if (link >= r->hdr.num_links)
			return -1;
		candidate = &r->links[link];
		to = candidate->to;
		if (candidate->from != from || candidate->action != RL_RUN ||
		    to < 0 || to >= r->hdr.num_seeds ||
		    r->seeds[to].area_hint > 60)
			continue;
		VectorSubtract(r->seeds[to].origin, r->seeds[from].origin, delta);
		distance = delta[0] * delta[0] + delta[1] * delta[1] +
		    delta[2] * delta[2] * 4.0f;
		if (distance >= 800.0f * 800.0f)
			continue;
		if (best < 0 || distance < best_distance ||
		    (distance == best_distance && to < best))
		{
			best = to;
			best_distance = distance;
		}
	}
	return best;
}

#undef SG_GOAL_PRIVATE

/* ----------------------------------------------------------------- the mega
 *
 * (c) THE ROLE GATE. The state half of the price lives with combat
 * (SG_WorthMega); this is the half that knows what the bot is FOR, and every
 * branch of it errs the same way -- toward not detouring. A mega taken is
 * worth 100 points of margin; a mega taken by the wrong bot at the wrong
 * moment is a flag, and those do not trade evenly.
 *
 *   CARRY    never. The carrier's job is the ground between here and home and
 *            nothing else; legcarrier dose 3 already says a healthy carrier
 *            does not shop, and this says the hurt one does not either -- a
 *            carrier stopping for 100 hp is a carrier standing still on a pad
 *            the enemy knows the location of.
 *   ESCORT   never, and for the same reason from the other side: the escort
 *            role exists only while there IS a live carrier of ours, so an
 *            escort on an errand is a carrier without a screen.
 *   RECOVER  never. Our flag is astray; there is no lull to spend.
 *   ATTACK   yes, except under the conductor's downbeat -- the push is a bar
 *            the team steps off on together and detours wait for the next one.
 *            "Pre-push" is exactly what the un-armed window is.
 *   DEFEND   yes on a lull, which is the same six seconds of no believed
 *            contact the pad wait already asks for. A defender who can hear
 *            somebody is a defender at the stand.
 *
 * And nobody in a fight: a bot with a live duel has a better use for the next
 * four seconds than a walk.
 */
static float Mega_Worth(sg_bot_t *bot, edict_t *e, sg_role_t role)
{
	int team = e->client->ctf.teamnum;

	if (!SG_MegaOn() || sg_fields.mega_count <= 0)
		return 0.0f;
	if (team != CTF_TEAM_RED && team != CTF_TEAM_BLUE)
		return 0.0f;

	if (role == SG_ROLE_CARRY || role == SG_ROLE_ESCORT ||
	    role == SG_ROLE_RECOVER)
		return 0.0f;
	if (role == SG_ROLE_ATTACK &&
	    SG_TimerPending(sg_push_until[SG_TeamIdx(team)]))
		return 0.0f;
	if (role == SG_ROLE_DEFEND)
	{
		int s;

		for (s = 0; s < SG_MAX_ENEMY_TRACK; s++)
			if (sg_caco_enemies[SG_TeamIdx(team)][s].client >= 0 &&
			    SG_AgeUnder(sg_caco_enemies[SG_TeamIdx(team)][s].seen_time, 6.0f))
				return 0.0f;    /* not a lull */
	}
	if (bot->engaged_last || SG_CombatDuel(e, NULL, NULL, NULL))
		return 0.0f;

	return SG_WorthMega(e);
}

/*
 * THE APPROACH BAND (split from SG_BotThink, 2026-08-11 standards pass;
 * body verbatim): the rally, the broadcast surge, and the flying cook --
 * everything an attacker decides between two and five seconds out.
 * Returns whether the bot holds its ground waiting on a partner.
 */
qboolean Think_ApproachBand(sg_bot_t *bot, sg_think_t *tc)
{
	/* the former parameter list, unpacked from the think context so the
	 * body below reads exactly as it did when these arrived as arguments */
	edict_t *e = tc->e;
	sg_role_t role = tc->role;
	int team = tc->team;
	const int *goal_field = tc->goal_field;
	int goal_ms = -1;

	qboolean hold = false;
	qboolean pressure_approach;

	if (bot->seed >= 0 && SG_Rune() &&
	    bot->seed < SG_Rune()->hdr.num_seeds)
		goal_ms = goal_field[bot->seed];
	pressure_approach = SG_StrikePrebreachApproachAllowed(
	    tc->strike_active, tc->strike_pressure,
	    role == SG_ROLE_ATTACK, goal_ms);

	/*
	 * THE RALLY. Arrival measurements show three quarters of all attacks
	 * reach the enemy base ALONE -- one body against three or more armed
	 * defenders at the stand, dead every time, which is why floors sit
	 * under 300 while steals remain rare. An attacker in the
	 * approach band (2-5s of field) with no partner inside 6s and at
	 * least two enemies believed alive holds its ground -- twelve
	 * seconds at most, gone the moment a mate closes or the wait times
	 * out. Solo pushes still happen; they just stop being the ONLY kind.
	 */
	/*
	 * THE CONDUCTOR (sg_wavepush). The rally waits for
	 * partners; the conductor makes partners exist. Once every 40
	 * seconds, when three or more attackers are alive and the nearest
	 * is within striking range, the team calls a downbeat: a 12-second
	 * window in which every rally releases at once and item detours
	 * stop pulling attackers sideways. Arrivals stack into a coordinated push --
	 * the census's 75-percent-alone number is the target -- and the
	 * respawn-surge rule still cancels every wait it ever cancelled.
	 */
	/*
	 * THE BROADCAST SURGE. A clock-driven metronome reduced steals from
	 * 2.2 to 1.3 in pooled measurements: a downbeat on a clock suppresses the
	 * organic rally pairing and marches under-armed groups into rooms
	 * that were never thin. The surge rule was always the true clock --
	 * a defender dead near their own stand IS the window -- but it
	 * released only the one attacker who happened to be in the band.
	 * Now the kill rings the whole team's bell: every rally releases
	 * into the same respawn-wide window, detours pause only during the
	 * eight seconds the window is actually open.
	 */
	/* A strike frame owns HOLD/RUSH.  Keep the legacy conductor and rally out
	 * of that decision, but do not skip the independent live-enemy approach
	 * action below merely because the coordinator supplied the route. */
	if (!tc->strike_active && sg_cv.wavepush->value &&
	    role == SG_ROLE_ATTACK &&
	    SG_TimerReady(sg_push_until[SG_TeamIdx(team)]))
	{
		edict_t *ef9 = SG_FlagStand(team, false);

		if (ef9 && SG_EnemyRoomDeathKnown(team, ef9->s.origin,
		    2.0f, 1200.0f))
		{
			SG_TimerArm(&sg_push_until[SG_TeamIdx(team)], 8.0f);
			if (sg_cv.debug->value)
				sg_host.dprint("PUSH team=%d surge\n", team);
		}
	}

	if (!tc->strike_active && role == SG_ROLE_ATTACK && bot->seed >= 0 &&
	    goal_field[bot->seed] > 2000 && goal_field[bot->seed] < 8000 &&
	    goal_field[bot->seed] < SG_FIELD_INF &&
	    SG_TimerPending(sg_push_until[SG_TeamIdx(team)]))
	{
		/* the bell rang: no waiting, no rally, run the window */
		bot->rally_since = 0.0f;
		goto rally_done;
	}

	if (!tc->strike_active && role == SG_ROLE_ATTACK && bot->seed >= 0 &&
	    goal_field[bot->seed] > 2000 && goal_field[bot->seed] < 5000 &&
	    goal_field[bot->seed] < SG_FIELD_INF)
	{
		int bi, mates_near = 0, mates_coming = 0;

		/*
		 * First cut waited only when two enemies were freshly SEEN and
		 * gave up after 12s -- but an attacker sneaking in alone has
		 * usually seen nobody, and a trailing mate 8-12s of field back
		 * cannot close inside the cap; the long cap paired almost nothing.
		 * The census already proved solo arrival means death against
		 * ANY defense, so the belief gate is gone. Wait exactly when a
		 * partner is genuinely en route (inside 14s of field), as long
		 * as it takes them to close -- capped at 20s -- and push solo
		 * without ceremony when nobody is coming at all.
		 */
		for (bi = 0; bi < SG_MAXBOTS; bi++)
		{
			sg_bot_t *mb = &sg_bots[bi];
			int mate_goal;

			if (mb == bot || !mb->ent ||
			    !SG_CoordinationBodyLive(mb->active, mb->ent->inuse,
			        mb->ent->deadflag, mb->ent->health))
				continue;
			if (mb->ent->client->ctf.teamnum != team)
				continue;
			if (!SG_StrikeEnemyPressureSnapshot(mb))
				continue;
			mate_goal = SG_StrikeEnemyPressureGoalSnapshot(mb);
			if (mate_goal < 0)
				continue;
			if (mate_goal < 6000)
				mates_near++;
			else if (mate_goal < 20000)
				/* THE APPEAL. The 20s horizon reduced steals from 1.6 to 1.0
				 * per measurement interval and was shrunk to a 6s sync --
				 * but that comparison ran in the corpse-wait era, when a
				 * 'partner en route' was usually a body that would never
				 * stand up. Bots respawn now; partners genuinely arrive.
				 * Retried at the full horizon on fresh evidence. */
				mates_coming++;
		}
		{
			/*
			 * THE SURGE: a defender dead near their own stand opens a
			 * respawn-wide window, and traces show the thief dying
			 * 3-5 seconds after the grab to the respawn stream -- the
			 * window is the only time the room is thin. A fresh enemy
			 * death (< 6s) within 1200 of the enemy flag cancels the
			 * wait: push NOW, paired or not.
			 */
			edict_t *ef = SG_FlagStand(team, false);

			if (ef && SG_EnemyRoomDeathKnown(team, ef->s.origin,
			    6.0f, 1200.0f))
			{
				bot->rally_since = 0.0f;
				goto rally_done;
			}
		}
		if (mates_near == 0 && mates_coming > 0)
		{
			if (bot->rally_since <= 0.0f)
			{
				int best_cover = Rally_CoverSeed(SG_Rune(), bot->seed);

				/*
				 * Seven paired pushes on lmctf09 stole
				 * nothing: the waiter froze wherever the band caught it,
				 * mid-corridor, lit, and the pairing died before it
				 * formed. The rune has measured exposure since the
				 * generator's census pass -- the wait belongs at the
				 * darkest seed within reach. "Reach" here is now current
				 * ground or one proved ordinary RUN. The former all-seed
				 * radius search could select cover through a wall or across
				 * a mechanism, then spend the whole synchronization window
				 * walking into it. With no proved cover, keep attacking.
				 */
				bot->rally_cover = best_cover;
				if (best_cover >= 0)
					SG_Mark(&bot->rally_since);
				if (sg_cv.debug->value && best_cover >= 0)
					sg_host.dprint("RALLY %s waits (%d coming, cover=%d)\n",
					           e->client->pers.netname, mates_coming,
					           best_cover);
			}
			if (bot->rally_cover >= 0 &&
			    SG_AgeUnder(bot->rally_since, 15.0f))
				hold = true;
		}
		else
		{
			if (bot->rally_since > 0.0f &&
			    sg_cv.debug->value)
				sg_host.dprint("RALLY %s released after %.1fs (near=%d)\n",
				           e->client->pers.netname,
				           SG_Age(bot->rally_since), mates_near);
			bot->rally_since = 0.0f;
		}
rally_done:;
	}
	else if (!tc->strike_active)
		bot->rally_since = 0.0f;

	/* THE FLYING COOK.  The strike coordinator replaces only rally timing;
	 * BREACH/CLEAR/PRESS still need this live-enemy arm in the same two-to-five
	 * second band.  Keeping it outside the legacy rally branch lets effective
	 * pressure override organic RECOVER/ESCORT without granting the action to a
	 * concrete recovery or escort duty. */
	if (pressure_approach && sg_cv.flycook->value &&
	    !bot->jump_started && !bot->drop_started &&
	    bot->hook_phase == 0 && bot->rj_phase == 0 &&
	    bot->nade_phase == 0 &&
	    !(SG_Rune() && bot->commit_link >= 0 &&
	      bot->commit_link < SG_Rune()->hdr.num_links &&
	      SG_ActionOwnsControl(
	          SG_Rune()->links[bot->commit_link].action)) &&
	    SG_TimerReady(bot->nade_next) &&
	    !SG_AttackFlagDirectTouchAuthority(e, team, NULL))
	{
		(void)SG_NadeArmPrebreachLiveEnemy(bot, e, team, 0.0f, 0.0f);
	}
	return hold;
}

/*
 * THE INTERCEPT SURFACE (split from SG_BotThink, 2026-08-11 standards
 * pass; body verbatim): everyone but the carrier supports the carrier,
 * and when an enemy thief is believed live, floods the hold ground
 * across its projected motion.
 */
void Think_InterceptField(sg_role_t role, int team,
                                 const int **support_out,
                                 const int **intercept_out)
{
	if (role != SG_ROLE_CARRY)
	{
		sg_belief_carrier_t *ec = &sg_caco_team_belief.enemy_carrier[SG_TeamIdx(team)];

		*support_out = sg_fields.our_carrier[SG_TeamIdx(team)];
		if (ec->seed >= 0)
		{
			int cost = 0;
			int hold = Intercept_HoldSeed(team, ec->seed);

			/* the hold ground across the thief's projected motion --
			 * or their believed position when the projection is thin */
			Field_Flood(SG_Rune(), intercept_field, &hold, &cost, 1);
			*intercept_out = intercept_field;
		}
	}
}

/*
 * THE CARRY BOOKENDS (split from SG_BotThink, 2026-08-11 standards
 * pass; body verbatim): grab and loss edges -- carry clocks, exit-lane
 * snapshot, escape priors, the unconditional last_role update.
 */
void Think_CarryBookends(sg_bot_t *bot, edict_t *e,
                                sg_role_t role, int team,
                                qboolean carrying)
{
	/* carry bookends: the STATE here is game logic, not telemetry -- the
	 * breakout gauge and the progress guard read it whether or not anyone
	 * is watching (it once lived inside the debug gate, which
	 * would have blinded both on any quiet server) */
	if (carrying && !bot->was_carrying)
	{
		SG_Mark(&bot->carry_start);
		bot->carry_startcost = -1;  /* gauged on first samples below */
		bot->carry_bestcost = -1;
		bot->carry_lost_at = 0.0f;
		SG_Mark(&sg_grab_time[SG_TeamIdx(team)]);

		/* exit-lane asymmetry: snapshot the roads ridden in on, then
		 * roll this carry's coin (sg_exitasym, default 0 = never) */
		bot->exitasym_n = (bot->inlinks_n < 16) ? bot->inlinks_n : 16;
		memcpy(bot->exitasym_set, bot->inlinks, sizeof(bot->exitasym_set));
		bot->exitasym_armed = false;
		if (sg_cv.exitasym->value > 0.0f)
			bot->exitasym_armed = (random() * 100.0f <
			                           sg_cv.exitasym->value);

		/*
		 * HUMAN ESCAPE PRIORS (sg_escapeprior, enhancement 6). The
		 * corpus says a human leaving a robbed stand does not pick a
		 * uniform direction -- on lmctf41's red stand 76% of 30 human
		 * steals left east, on smap26's 74% left north -- and it also
		 * says he does not pick the SAME one every time. An argmin
		 * carrier has the opposite failing in both directions: one
		 * exit, always, and no reason for it to be the one people use.
		 *
		 * So the exit is DRAWN, once per carry, from that map's mined
		 * distribution, and the draw only tilts pricing: the bucket
		 * drawn gets its own measured probability as a discount for
		 * the next three seconds (the window the bearings were mined
		 * over), and every other road stays exactly as priced. A
		 * bucket humans used a fifth of the time gets drawn a fifth of
		 * the time and bends the price a fifth as hard as a bucket
		 * they used always -- the distribution's shape survives into
		 * behaviour instead of collapsing to its mode.
		 *
		 * The draw is a hash of the body, its life, and the clock, not
		 * random(): two carriers grabbing at once must draw
		 * independently, and one carrier must draw the same exit for
		 * the whole three seconds no matter how many times the fan is
		 * priced in between.
		 */
		bot->escprior_bucket = -1;
		bot->escprior_until = 0.0f;
		bot->escprior_dose = 0.0f;
		if (SG_Rune() && sg_cv.escapeprior->value > 0.0f)
		{
			/* the flag this carrier now holds is the ENEMY flag, and
			 * its stand is the one he just robbed */
			int fk = SG_TeamIdx(SG_EnemyTeam(team));   /* 0 red, 1 blue */
			int stand = (team == CTF_TEAM_RED)
			                ? sg_fields.blue_flag_seed
			                : sg_fields.red_flag_seed;

			if (sg_escape_total[fk] > 0 && stand >= 0 &&
			    stand < SG_Rune()->hdr.num_seeds)
			{
				unsigned h = ((unsigned)(e - g_edicts) * 2654435761u) ^
				             ((unsigned)(bot->lives + bot->legs) * 40503u) ^
				             ((unsigned)(level.time * 10.0f) * 2246822519u);
				int b, acc = 0, pick;

				h ^= h >> 13;
				h *= 2654435761u;
				h ^= h >> 16;
				pick = (int)(h % (unsigned)sg_escape_total[fk]);
				for (b = 0; b < SG_ESC_BUCKETS - 1; b++)
				{
					acc += sg_escape_count[fk][b];
					if (pick < acc)
						break;
				}
				VectorCopy(SG_Rune()->seeds[stand].origin,
				           bot->escprior_org);
				bot->escprior_bucket = b;
				SG_TimerArm(&bot->escprior_until, 3.0f);
				bot->escprior_dose =
				    sg_cv.escapeprior->value / 100.0f *
				    ((float)sg_escape_count[fk][b] /
				     (float)sg_escape_total[fk]);
				if (bot->escprior_dose > 0.9f)
					bot->escprior_dose = 0.9f;
				if (sg_cv.debug->value)
					sg_host.dprint("ESCPRIOR %s bucket=%d p=%d/%d dose=%.2f\n",
					           e->client->pers.netname, b,
					           sg_escape_count[fk][b],
					           sg_escape_total[fk], bot->escprior_dose);
			}
		}
	}
	else if (!carrying && bot->was_carrying)
	{
		bot->exitasym_armed = false;
		bot->escprior_bucket = -1;
	}
	if (sg_cv.debug->value)
	{
		if (carrying && !bot->was_carrying)
		{
			sg_host.dprint("CARRY %s begins\n", e->client->pers.netname);
			/* the grab's honesty, on the record: how many defenders
			 * the last census believed present, and whether the
			 * patience valve had already expired (a FORCED grab into
			 * a room the hold never cleared). If parity grabs are
			 * ~all forced, the strict hold never wins at parity and
			 * the doctrine pivot is evidence, not taste. */
			sg_host.dprint("GRABMODE %s room=%d %s\n",
			           e->client->pers.netname, bot->last_room,
			           (bot->strict_since > 0.0f &&
			            SG_AgeAtLeast(bot->strict_since, 20.0f))
			               ? "forced" : "clean");
		}
		else if (!carrying && bot->was_carrying)
			sg_host.dprint("CARRY %s ends after %.1fs\n",
			           e->client->pers.netname,
			           SG_Age(bot->carry_start));
		if ((int)role != bot->last_role && role == SG_ROLE_ESCORT)
			sg_host.dprint("ESCORT %s begins\n", e->client->pers.netname);
	}
	/*
	 * Unconditionally: last_role is the observable organic role and the
	 * escort-assignment incumbent.  Rally pairing reads the pre-frame effective
	 * pressure snapshot instead, because a coordinator duty can override this
	 * role before the serial think loop.  This assignment sat inside the debug
	 * gate until the 2026-08-11 standards pass, leaving every non-debug consumer
	 * stale forever.
	 */
	bot->last_role = (int)role;
	bot->was_carrying = carrying;
}

/* ---------------------------------------------------------------- supply */

void SG_DefenseSupplyReset(sg_bot_t *bot)
{
	if (!bot)
		return;
	bot->def_supply_armed = false;
	bot->def_supply_phase = SG_DEF_SUPPLY_NONE;
	bot->def_supply_instance = 0ULL;
	bot->def_supply_ent = -1;
	bot->def_supply_target_seed = -1;
	bot->def_supply_route_ms = 0;
	VectorClear(bot->def_supply_target_org);
	bot->def_supply_until = 0.0f;
	bot->def_supply_next = 0.0f;
}

/* A phase edge may be observed after the outbound field has already selected
 * an ordinary RUN. Retire that exact RUN so the next frame can descend the
 * home field; never tear down a hook, door, jump, or other command owner here.
 */
static void DefenseSupplyRetireRun(sg_bot_t *bot)
{
	int link_index;

	if (!bot || bot->commit_link < 0 || !SG_Rune() ||
	    bot->commit_link >= SG_Rune()->hdr.num_links)
		return;
	link_index = bot->commit_link;
	if (SG_Rune()->links[link_index].action != RL_RUN)
		return;
	bot->commit_link = -1;
	bot->commit_until = 0.0f;
	if (bot->ribbon_link == link_index)
		bot->ribbon_link = -1;
	if (bot->sticky_link == link_index)
		bot->sticky_link = -1;
}

/* RETURN owns the home route.  A generic rail retry armed before that phase
 * edge cannot survive and overwrite the already-validated route later in
 * Think_CommitLink. */
static void DefenseSupplyRetireRailRetry(sg_bot_t *bot)
{
	if (!bot)
		return;
	bot->rail_link = -1;
	bot->rail_stage = 0;
	bot->rail_until = 0.0f;
}

void SG_DefenseSupplyCancel(sg_bot_t *bot, qboolean backoff)
{
	if (!bot)
		return;
	DefenseSupplyRetireRun(bot);
	bot->def_supply_armed = false;
	bot->def_supply_phase = SG_DEF_SUPPLY_NONE;
	bot->def_supply_instance = 0ULL;
	bot->def_supply_ent = -1;
	bot->def_supply_target_seed = -1;
	bot->def_supply_route_ms = 0;
	VectorClear(bot->def_supply_target_org);
	bot->def_supply_until = 0.0f;
	if (backoff)
		SG_TimerArm(&bot->def_supply_next, SG_DEF_SUPPLY_BACKOFF);
}

void SG_DefenseSupplyBeginReturn(sg_bot_t *bot)
{
	if (!bot || !bot->def_supply_armed)
		return;
	DefenseSupplyRetireRun(bot);
	DefenseSupplyRetireRailRetry(bot);
	bot->def_supply_phase = SG_DEF_SUPPLY_RETURN;
	bot->def_supply_ent = -1;
	bot->def_supply_target_seed = -1;
	bot->def_supply_route_ms = 0;
	VectorClear(bot->def_supply_target_org);
}

/* A return has reached the real home field.  Keep a short refusal window so
 * the same watchman cannot arm a second sortie on the first quiet frame after
 * touching the post.  The deadline itself is never extended here. */
void SG_DefenseSupplyFinish(sg_bot_t *bot)
{
	if (!bot)
		return;
	SG_DefenseSupplyReset(bot);
	SG_TimerArm(&bot->def_supply_next, SG_DEF_SUPPLY_BACKOFF);
}

qboolean SG_DefenseSupplyActive(const sg_bot_t *bot)
{
	return bot && bot->def_supply_armed;
}

/* Touch_Item is the final pickup authority.  Close only the touching bot's
 * exact outbound witness, whether Pickup_Weapon accepted it or proved that
 * the live game rules refuse it.  This prevents DF_WEAPONS_STAY from leaving
 * an empty defender pressed against an owned, uncollectable weapon pad. */
void SG_DefenseSupplyNoteItemTouch(edict_t *taker, edict_t *item)
{
	int item_ent, i;

	if (!taker || !item)
		return;
	item_ent = (int)(item - g_edicts);
	if (item_ent <= 0 || item_ent >= globals.num_edicts)
		return;
	for (i = 0; i < SG_MAXBOTS; i++)
	{
		sg_bot_t *bot = &sg_bots[i];

		if (!bot->active || bot->ent != taker ||
		    bot->def_supply_phase != SG_DEF_SUPPLY_OUTBOUND ||
		    bot->def_supply_instance != bot->instance_token ||
		    bot->def_supply_ent != item_ent)
			continue;
		SG_DefenseSupplyBeginReturn(bot);
		return;
	}
}

qboolean SG_DefenseSupplyHome(int team)
{
	edict_t *flag;

	if (team != CTF_TEAM_RED && team != CTF_TEAM_BLUE)
		return false;
	flag = SG_OwnFlag(team);
	return flag && flag->inuse && ctf_flagathome(flag);
}

qboolean SG_DefenseSupplyThreat(int team)
{
	const int *post;
	int ti, index;

	if ((team != CTF_TEAM_RED && team != CTF_TEAM_BLUE) || !SG_Rune())
		return false;
	ti = SG_TeamIdx(team);
	post = sg_fields.to_post[ti];
	if (!post || !sg_cv.defpost || sg_cv.defpost->value < 3.0f)
		post = (team == CTF_TEAM_RED) ? sg_fields.to_red_flag
		                              : sg_fields.to_blue_flag;
	if (!post)
		return false;
	for (index = 0; index < SG_MAX_ENEMY_TRACK; index++)
	{
		sg_belief_enemy_t *enemy = &sg_caco_enemies[ti][index];

		if (enemy->client < 0 || enemy->seed < 0 ||
		    enemy->seed >= SG_Rune()->hdr.num_seeds ||
		    !SG_AgeUnder(enemy->seen_time, 6.0f))
			continue;
		if (post[enemy->seed] < 2500)
			return true;
	}
	return false;
}

static qboolean DefenseSupplyOtherOwner(const sg_bot_t *bot,
                                        qboolean active)
{
	if (!bot)
		return true;
	/* These are already-owned missions.  A supply sortie never steals their
	 * route, and an active sortie is retired if one is acquired later. */
	if (bot->lead_ent > 0 || bot->patrol_seed >= 0 ||
	    bot->def_shift_seed >= 0 || bot->tac_seed >= 0 ||
	    bot->rail_stage > 0 || bot->rj_phase > 0 || bot->nade_phase > 0 ||
	    bot->hook_phase > 0 || bot->jump_link >= 0 || bot->drop_link >= 0)
		return true;
	/* A pre-existing route commitment belongs to another action.  Once the
	 * sortie is armed, its own commit is allowed to remain live. */
	return !active && bot->commit_link >= 0;
}

static qboolean DefenseSupplyWeaponClass(const edict_t *item,
                                         const edict_t *taker)
{
	if (!item || !item->inuse || !item->classname ||
	    strncmp(item->classname, "weapon_", 7) != 0)
		return false;
	/* The hook and hand-grenade entries are weapon_ classnames too, but neither
	 * is a usable non-blaster pickup for this errand. */
	if (strcmp(item->classname, "weapon_grappling_hook") == 0 ||
	    strcmp(item->classname, "weapon_grenades") == 0 ||
	    strcmp(item->classname, "weapon_blaster") == 0)
		return false;
	return item->solid != SOLID_NOT && Caco_ItemBelievedUp((edict_t *)item) &&
	       G_WeaponPickupEligible((edict_t *)item, (edict_t *)taker);
}

static qboolean DefenseSupplyTargetValid(const sg_bot_t *bot)
{
	edict_t *item;
	int seed;

	if (!bot || bot->def_supply_ent < 0 ||
	    bot->def_supply_ent >= globals.num_edicts || !SG_Rune())
		return false;
	item = &g_edicts[bot->def_supply_ent];
	if (!DefenseSupplyWeaponClass(item, bot->ent))
		return false;
	seed = Rune_NearestSeed(SG_Rune(), item->s.origin);
	if (seed < 0 || seed != bot->def_supply_target_seed)
		return false;
	{
		vec3_t delta;

		VectorSubtract(item->s.origin, bot->def_supply_target_org, delta);
		return VectorLength(delta) <= 1.0f;
	}
}

/* The broad class field is the live arm/reach guard.  The selected edict below
 * is the immutable target witness for this transaction; another weapon may
 * make the broad field reflow while the fixed deadline is pending, but that
 * unrelated class change is deliberately not an ownership signature. */
static qboolean DefenseSupplyWeaponFieldReachable(const sg_bot_t *bot)
{
	const int *field;

	if (!bot || !SG_Rune() || bot->seed < 0 ||
	    bot->seed >= SG_Rune()->hdr.num_seeds)
		return false;
	field = sg_fields.item[SG_FC_WEAPON];
	return field && field[bot->seed] < SG_FIELD_INF &&
	       field[bot->seed] <= SG_DEF_SUPPLY_MAX_ROUTE_MS;
}

static int sg_defense_supply_target_field[SG_MAXBOTS][SG_MAX_SEEDS];
static unsigned sg_defense_supply_target_epoch[SG_MAXBOTS];
static int sg_defense_supply_target_cached[SG_MAXBOTS];
static unsigned char sg_defense_supply_target_ready[SG_MAXBOTS];

static int DefenseSupplyBotIndex(const sg_bot_t *bot)
{
	ptrdiff_t index;

	if (!bot)
		return 0;
	index = bot - sg_bots;
	if (index < 0 || index >= SG_MAXBOTS)
		return 0;
	return (int)index;
}

static qboolean DefenseSupplyFindTarget(const sg_bot_t *bot, int *out_ent,
                                        int *out_seed, int *out_route_ms)
{
	int i, best_ent = -1, best_seed = -1, best_cost = SG_FIELD_INF;
	int bi = DefenseSupplyBotIndex(bot);

	if (!bot || !SG_Rune() || bot->seed < 0 ||
	    bot->seed >= SG_Rune()->hdr.num_seeds)
		return false;
	for (i = 0; i < globals.num_edicts; i++)
	{
		edict_t *item = &g_edicts[i];
		int seed, flood_cost = 0, cost;

		if (!DefenseSupplyWeaponClass(item, bot->ent))
			continue;
		seed = Rune_NearestSeed(SG_Rune(), item->s.origin);
		if (seed < 0)
			continue;
		Field_Flood(SG_Rune(), sg_defense_supply_target_field[bi],
		            &seed, &flood_cost, 1);
		cost = sg_defense_supply_target_field[bi][bot->seed];
		if (cost < best_cost)
		{
			best_cost = cost;
			best_ent = i;
			best_seed = seed;
		}
	}
	if (best_ent < 0 || best_cost >= SG_FIELD_INF ||
	    best_cost > SG_DEF_SUPPLY_MAX_ROUTE_MS)
		return false;
	if (out_ent)
		*out_ent = best_ent;
	if (out_seed)
		*out_seed = best_seed;
	if (out_route_ms)
		*out_route_ms = best_cost;
	return true;
}

static qboolean DefenseSupplyCoreEligible(sg_bot_t *bot, sg_think_t *tc,
                                          qboolean active)
{
	if (!bot || !tc || !tc->e || !tc->e->inuse || !tc->e->client ||
	    tc->e->deadflag == DEAD_DEAD || tc->e->health <= 0 ||
	    tc->role != SG_ROLE_DEFEND || !bot->def_stand || tc->carrying ||
	    (!active && !SG_DefenseSupplyHome(tc->team)) ||
	    (!active && SG_ChatOrderedRole(tc->e) >= 0) ||
	    (!active && DefenseSupplyOtherOwner(bot, false)))
		return false;
	if (!SG_Rune() || bot->seed < 0 ||
	    bot->seed >= SG_Rune()->hdr.num_seeds)
		return false;
	if (active && bot->def_supply_instance != bot->instance_token)
		return false;
	/* Threat/engagement cancels OUTBOUND. RETURN remains a route-pure home
	 * handoff, so it is allowed to finish while combat owns the view. */
	if (!active || bot->def_supply_phase == SG_DEF_SUPPLY_OUTBOUND)
	{
		if (SG_DefenseSupplyThreat(tc->team) ||
		    SG_CombatWouldEngage(tc->e) || bot->engaged_last)
			return false;
	}
	return true;
}

static qboolean DefenseSupplyTargetFieldReachable(const sg_bot_t *bot);

static qboolean DefenseSupplyEligible(sg_bot_t *bot, sg_think_t *tc,
                                      qboolean active)
{
	if (!DefenseSupplyCoreEligible(bot, tc, active))
		return false;
	if (!active)
		return true;
	if (bot->def_supply_phase == SG_DEF_SUPPLY_OUTBOUND)
		return DefenseSupplyTargetFieldReachable(bot) &&
		       bot->def_supply_route_ms <= SG_DEF_SUPPLY_MAX_ROUTE_MS;
	return bot->def_supply_phase == SG_DEF_SUPPLY_RETURN;
}

static qboolean DefenseSupplyReturnAllowed(const sg_bot_t *bot,
                                           const sg_think_t *tc)
{
	return bot && tc && tc->e && tc->e->inuse && tc->e->client &&
	       tc->e->deadflag != DEAD_DEAD && tc->e->health > 0 &&
	       tc->role == SG_ROLE_DEFEND && bot->def_stand && !tc->carrying &&
	       bot->def_supply_instance == bot->instance_token;
}

static const int *DefenseSupplyTargetField(sg_bot_t *bot)
{
	const int *target_field;
	int bi;
	int cost = 0;
	int target_seed;

	if (!bot || !SG_DefenseSupplyActive(bot) ||
	    bot->def_supply_phase != SG_DEF_SUPPLY_OUTBOUND ||
	    !DefenseSupplyTargetValid(bot))
		return NULL;
	bi = DefenseSupplyBotIndex(bot);
	target_seed = bot->def_supply_target_seed;
	if (!sg_defense_supply_target_ready[bi] ||
	    sg_defense_supply_target_cached[bi] != target_seed ||
	    sg_defense_supply_target_epoch[bi] !=
	        sg_fields.action_topology_epoch)
	{
		Field_Flood(SG_Rune(), sg_defense_supply_target_field[bi],
		            &target_seed, &cost, 1);
		sg_defense_supply_target_epoch[bi] =
		    sg_fields.action_topology_epoch;
		sg_defense_supply_target_cached[bi] = target_seed;
		sg_defense_supply_target_ready[bi] = 1;
	}
	target_field = sg_defense_supply_target_field[bi];
	return target_field;
}

static qboolean DefenseSupplyTargetFieldReachable(const sg_bot_t *bot)
{
	const int *field = DefenseSupplyTargetField((sg_bot_t *)bot);

	return field && bot && bot->seed >= 0 &&
	       field[bot->seed] < SG_FIELD_INF &&
	       field[bot->seed] <= SG_DEF_SUPPLY_MAX_ROUTE_MS;
}

static const int *DefenseSupplyRouteField(sg_bot_t *bot,
                                           const int *goal_field,
                                           qboolean *pure)
{
	const int *target_field;
	const int *route = NULL;

	if (!bot || !goal_field || !pure || !SG_DefenseSupplyActive(bot))
		return goal_field;
	target_field = DefenseSupplyTargetField(bot);
	if (!SG_DefenseSupplyRoute(
		    (sg_defense_supply_phase_t)bot->def_supply_phase,
		    NULL, target_field, goal_field,
		    bot->seed, SG_DEF_SUPPLY_MAX_ROUTE_MS,
		    &route))
		return goal_field;
	/* Route authority is a fresh flood from the selected valid pad.  The broad
	 * live class field remains the arm/reach gate, while this exact field avoids
	 * routing to an excluded blaster, hook, or hand-grenade source.  Topology
	 * refreshes reflood the same target; an unrelated class change never
	 * cancels this transaction. */
	*pure = true;
	return route;
}

static const int *DefenseSupplyHomeField(int team)
{
	int ti;

	if (team != CTF_TEAM_RED && team != CTF_TEAM_BLUE)
		return NULL;
	ti = SG_TeamIdx(team);
	/* Match the live defender post policy when it is configured.  The flag
	 * field is the safe fallback for maps/cvars without a distinct post. */
	if (sg_fields.to_post[ti] && sg_cv.defpost &&
	    sg_cv.defpost->value >= 3.0f)
		return sg_fields.to_post[ti];
	if (team == CTF_TEAM_RED)
		return sg_fields.to_red_flag;
	if (team == CTF_TEAM_BLUE)
		return sg_fields.to_blue_flag;
	return NULL;
}

/*
 * THE LIVE ROW (split from SG_BotThink, 2026-08-11 standards pass; body
 * verbatim): the fitted role row modulated by this bot's state -- combat
 * worths, the rune-threat bump, the patrol appetite.
 */
void Think_LiveWeights(sg_bot_t *bot, sg_think_t *tc)
{
	/* the former parameter list, unpacked from the think context; the
	 * live row is written straight into the context's copy */
	edict_t *e = tc->e;
	sg_role_t role = tc->role;
	int team = tc->team;
	sg_weights_t *live = &tc->live;
	sg_combat_weapon_state_t weapon_state;
	qboolean supply_active;

	/*
	 * The role row is a BIAS, not an absolute. What an item is actually worth
	 * to THIS bot right now -- health as its own health drops, armour by
	 * deficit, a weapon when it has none worth the name, ammo against the
	 * floor of the weapon it holds, the quad against its respawn clock, a rune
	 * it is allowed to pick up -- is state, and SG_CombatWeights supplies it
	 * from WEAPONS.md 2.3. Every worth there is derived from a cited line of
	 * this tree; the row below stays exactly as fitted and is multiplied
	 * through. The result is clamped to the same [0, 2.0] the detour decay's
	 * 1500 ms scale (Detour_Value, above) makes meaningful.
	 */
	SG_CombatWeights(e, Weights_Row(role), live);

	/* A rank-zero watchman gets one bounded, weapon-only supply sortie.  The
	 * state read is the live inventory predicate from sg_combat, while the
	 * route and deadline are explicit here so a generic item-weight threshold
	 * cannot turn the post into an unbounded shopping walk. */
	supply_active = SG_DefenseSupplyActive(bot);
	if (!SG_CombatWeaponState(e, &weapon_state))
	{
		if (supply_active)
			SG_DefenseSupplyCancel(bot, true);
		supply_active = false;
	}
	else if (supply_active)
	{
		qboolean return_ok = DefenseSupplyReturnAllowed(bot, tc);

		if (bot->def_supply_phase == SG_DEF_SUPPLY_OUTBOUND)
		{
			sg_defense_supply_step_t step;
			sg_defense_supply_phase_t next_phase;

			/* Acquisition is a phase edge, not a cancellation.  The target
			 * witness is cleared by BeginReturn and the fixed deadline remains
			 * unchanged for diagnostics and the home handoff. */
			memset(&step, 0, sizeof(step));
			step.identity_valid =
			    bot->def_supply_instance == bot->instance_token;
			step.owner_valid = return_ok;
			step.own_flag_home = SG_DefenseSupplyHome(team);
			step.threat = SG_DefenseSupplyThreat(team) ||
			              SG_CombatWouldEngage(e);
			step.engaged = bot->engaged_last;
			step.human_order = SG_ChatOrderedRole(e) >= 0;
			step.other_owner = DefenseSupplyOtherOwner(bot, true);
			step.target_valid = DefenseSupplyTargetValid(bot);
			step.weapon_field_valid = DefenseSupplyTargetFieldReachable(bot);
			step.deadline_pending = SG_TimerPending(bot->def_supply_until);
			step.weapon_available = weapon_state.nonblaster_available;
			next_phase = SG_DefenseSupplyPhaseStep(
			    SG_DEFENSE_SUPPLY_PHASE_OUTBOUND, &step);
			if (next_phase == SG_DEFENSE_SUPPLY_PHASE_RETURN)
			{
				SG_DefenseSupplyBeginReturn(bot);
			}
			else if (next_phase != SG_DEFENSE_SUPPLY_PHASE_OUTBOUND)
				SG_DefenseSupplyCancel(bot, true);
		}
		else if (bot->def_supply_phase == SG_DEF_SUPPLY_RETURN)
		{
			sg_defense_supply_step_t step;
			sg_defense_supply_phase_t next_phase;

			memset(&step, 0, sizeof(step));
			step.identity_valid =
			    bot->def_supply_instance == bot->instance_token;
			step.owner_valid = return_ok;
			next_phase = SG_DefenseSupplyPhaseStep(
			    SG_DEFENSE_SUPPLY_PHASE_RETURN, &step);
			if (next_phase != SG_DEFENSE_SUPPLY_PHASE_RETURN)
				SG_DefenseSupplyCancel(bot, true);
		}
		else
			SG_DefenseSupplyCancel(bot, true);
		supply_active = SG_DefenseSupplyActive(bot);
	}
	else if (!weapon_state.nonblaster_available &&
	         SG_TimerReady(bot->def_supply_next) &&
	         DefenseSupplyEligible(bot, tc, false) &&
	         DefenseSupplyWeaponFieldReachable(bot))
	{
		int target_ent = -1, target_seed = -1, route_ms = SG_FIELD_INF;

		/* The exact live pad is selected once at arm time.  Its per-bot flood
		 * owns the route; the broad class field was only the bounded arm/reach
		 * gate.  This identity witness tells us whether acquisition happened or
		 * the selected pad disappeared. */
		if (DefenseSupplyFindTarget(bot, &target_ent, &target_seed, &route_ms))
		{
			int bi = DefenseSupplyBotIndex(bot);

			bot->def_supply_armed = true;
			bot->def_supply_phase = SG_DEF_SUPPLY_OUTBOUND;
			bot->def_supply_instance = bot->instance_token;
			bot->def_supply_ent = target_ent;
			bot->def_supply_target_seed = target_seed;
			bot->def_supply_route_ms = route_ms;
			sg_defense_supply_target_cached[bi] = -1;
			sg_defense_supply_target_epoch[bi] = 0;
			sg_defense_supply_target_ready[bi] = 0;
			if (target_ent >= 0 && target_ent < globals.num_edicts)
				VectorCopy(g_edicts[target_ent].s.origin,
				           bot->def_supply_target_org);
			SG_TimerArm(&bot->def_supply_until, SG_DEF_SUPPLY_DEADLINE);
			supply_active = true;
		}
	}
	if (supply_active && bot->def_supply_phase == SG_DEF_SUPPLY_OUTBOUND)
	{
		/* Only the two weapon-related terms are allowed to bias this phase;
		 * route authority below is pure and does the actual leaving. */
		if (live->item[SG_FC_WEAPON] < 1.25f)
			live->item[SG_FC_WEAPON] = 1.25f;
		if (live->item[SG_FC_AMMO] < 0.90f)
			live->item[SG_FC_AMMO] = 0.90f;
	}
	/*
	 * Rune threat (WEAPONS.md 2.4-D4, the honest half): a sighted enemy
	 * glowing with RF_GLOW (p_view.c:792-794) holds SOME rune -- the glow
	 * never says which, so this is a generic bump to how much OUR side
	 * should want rune-class pickups, not the dossier's Damage-specific
	 * Resist play, which is unknowable from a sighting.
	 */
	{
		int s;

		for (s = 0; s < SG_MAX_ENEMY_TRACK; s++)
		{
			sg_belief_enemy_t *en = &sg_caco_enemies[SG_TeamIdx(team)][s];

			if (en->client >= 0 && en->runed &&
			    SG_AgeUnder(en->seen_time, 15.0f))
			{
				/* generic: someone glows, runes matter more. When the
				 * inference chain can NAME the Damage rune in enemy
				 * hands, the dossier's full Resist posture applies
				 * (WEAPONS.md 2.4-D4: x1.80). */
				live->item[SG_FC_RUNE] *=
				    Caco_EnemyHasDamageRune(team) ? 1.80f : 1.45f;
				if (live->item[SG_FC_RUNE] > 2.0f)
					live->item[SG_FC_RUNE] = 2.0f;
				break;
			}
		}
	}
	/*
	 * The patrol's circuit is an appetite, not a waypoint list: a
	 * permanently item-hungry second defender oscillates between the
	 * stand's pull and whatever armor or health just respawned nearby,
	 * which IS the patrol (it13: without this, the unpinned patrol stood
	 * at its field minimum -- 592 samples at one point -- because
	 * standing there is what minimums are for).
	 */
	if (role == SG_ROLE_DEFEND && !bot->def_stand)
	{
		if (live->item[SG_FC_ARMOR] < 1.1f)  live->item[SG_FC_ARMOR] = 1.1f;
		if (live->item[SG_FC_HEALTH] < 1.0f) live->item[SG_FC_HEALTH] = 1.0f;
		if (live->item[SG_FC_AMMO] < 1.0f)   live->item[SG_FC_AMMO] = 1.0f;
	}
}

/*
 * THE OBJECTIVE (split from SG_BotThink, 2026-08-11 standards pass;
 * body verbatim): the role-to-goal-field switch -- carrier/defender
 * stands, the scoop, the interposition and formation stations, the
 * courier, the early return, the mega offer, and the tactics waypoint.
 * Emits the goal field, the route field, and its purity.
 */
void Think_Objective(sg_bot_t *bot, sg_think_t *tc)
{
	/* the former parameter list, unpacked from the think context so the
	 * body below reads exactly as it did when these arrived as arguments */
	edict_t *e = tc->e;
	/* Strike duty is resolved before Objective.  Its effective ESCORT owns the
	 * same carrier/formation objective as an organic escort; every other duty
	 * retains the organic role until the coordinator applies its exact route. */
	sg_role_t role = (sg_role_t)SG_ObjectiveRole(tc->role,
	    tc->escort_mission);
	int team = tc->team;
	qboolean carrying = tc->carrying;
	const sg_weights_t *w = tc->w;
	const int *support = tc->support;
	const int *intercept = tc->intercept;

	const int *goal_field;
	const int *route_field;
	qboolean route_pure;

	/*
	 * The role's principal field:
	 *   carrier  -> own stand (the capture point)
	 *   defender -> own stand's surroundings (the home field IS the post)
	 *   recover  -> OUR flag where belief puts it: the -now field for our own
	 *               flag, which floods from the believed position when it is
	 *               astray and from home otherwise
	 *   escort   -> our carrier's believed position (the support field, used
	 *               here as the objective rather than as a side term)
	 *   attacker -> the enemy flag WHERE BELIEF PUTS IT (home stand, or the
	 *               spot it was last seen lying, via the -now field)
	 * When our flag is astray and its taker was seen, the intercept field
	 * (their believed position) joins the composition for non-carriers.
	 */
	if (role == SG_ROLE_CARRY || role == SG_ROLE_DEFEND)
	{
		goal_field = (team == CTF_TEAM_RED) ? sg_fields.to_red_flag
		                                    : sg_fields.to_blue_flag;

		/*
		 * FIELD-MODE DEFENSE (dose 3+). The pricing bias (doses
		 * 1-2) read null, as the extraction predicted: it bends wandering
		 * instead of choosing a post. Field mode CHOOSES: the defender's
		 * whole goal becomes the corpus's top post seed (the existing
		 * near-goal hold then keeps it there), and while our flag is
		 * astray the goal becomes the top intercept seed -- the human
		 * response's END position, not the carrier's current one.
		 */
		if (role == SG_ROLE_DEFEND)
		{
			qboolean astray =
			    (sg_caco_team_belief.flag[SG_TeamIdx(team)]
			         [SG_TeamIdx(team)].state == SG_FLAG_ASTRAY);

			if (!astray && sg_fields.to_post[SG_TeamIdx(team)] &&
			    sg_cv.defpost->value >= 3 &&
			    !(bot->def_stand && SG_DefenseSupplyActive(bot)))
				goal_field = sg_fields.to_post[SG_TeamIdx(team)];
			else if (!astray && !bot->def_stand &&
			         sg_fields.to_lane[SG_TeamIdx(team)] &&
			         sg_cv.raillane->value)
				/* the second defender holds the computed rail lane; the
				 * watchman stays on the stand (the .dpo lesson: never
				 * empty the stand for a post) */
				goal_field = sg_fields.to_lane[SG_TeamIdx(team)];
			else if (astray && sg_fields.to_icept[SG_TeamIdx(team)] &&
			         sg_cv.defreact->value >= 3)
				goal_field = sg_fields.to_icept[SG_TeamIdx(team)];
		}
	}
	else if (role == SG_ROLE_RECOVER)
		goal_field = sg_fields.to_flag_now[SG_TeamIdx(team)][SG_TeamIdx(team)];
	else if (role == SG_ROLE_ESCORT)
	{
		edict_t *ht = SG_ChatEscortTarget(e);

		/* A human cover order may name a live teammate outside the rune or in
		 * an unreachable component. Start from the team's ordinary attack
		 * objective; replace it only when the target flood is reachable from
		 * this bot, so a valid body is never misclassified as seedless. */
		goal_field = ht
		    ? sg_fields.to_flag_now[SG_TeamIdx(team)]
		        [SG_TeamIdx(SG_EnemyTeam(team))]
		    : (sg_fields.our_carrier_valid[SG_TeamIdx(team)]
		        ? sg_fields.our_carrier[SG_TeamIdx(team)]
		        : (team == CTF_TEAM_RED ? sg_fields.to_red_flag
		                                : sg_fields.to_blue_flag));

		/*
		 * THE SCOOP (sg_scoop). Across sixty-two parity drops,
		 * defense returned thirty-four, we re-scooped three. The
		 * dropped flag is a live steal lying on the ground for up to
		 * thirty seconds, the escort is standing beside the corpse --
		 * and it keeps descending a dead carrier's field while a
		 * defender walks over and touches the flag home. No live
		 * carrier plus enemy flag astray: the escort takes the
		 * attacker's -now field, which floods from the believed drop
		 * spot. First body to the flag wins the relay; ours is
		 * closest by construction.
		 */
		if (sg_cv.scoop->value &&
		    sg_caco_team_belief.carrier[SG_TeamIdx(team)].client < 0 &&
		    sg_caco_team_belief.flag[SG_TeamIdx(team)]
		        [SG_TeamIdx(SG_EnemyTeam(team))].state ==
		        SG_FLAG_ASTRAY)
		{
			goal_field = sg_fields.to_flag_now[SG_TeamIdx(team)]
			    [SG_TeamIdx(SG_EnemyTeam(team))];
			if (sg_cv.debug->value &&
			    SG_TimerReady(bot->next_report - 0.9f))
				sg_host.dprint("SCOOP %s\n", e->client->pers.netname);
		}

		/*
		 * THE INTERPOSITION (sg_interpose). The killer
		 * census's standing fact: carriers die to live defenders with
		 * escorts RIGHT THERE -- near the carrier, which is where the
		 * support field sends them, and nowhere in particular relative
		 * to the gun. A bodyguard does not stand next to the client; he
		 * stands on the line the bullet takes. With a live carrier and
		 * a fresh threat believed near it, the escort's goal becomes
		 * the MIDPOINT of carrier and threat: body on the line, rail
		 * eats the escort, carrier keeps the flag. Falls through to
		 * the ordinary screen when there is no named threat.
		 */
		{
			int interpose_mode = SG_InterposeMode(sg_cv.interpose->value);

		if (interpose_mode)
		{
			sg_belief_carrier_t *oc =
			    &sg_caco_team_belief.carrier[SG_TeamIdx(team)];

			if (oc->client >= 0 && oc->seed >= 0)
			{
				int s11, ts = -1;
				float td11, best11 = 1200.0f;

				for (s11 = 0; s11 < SG_MAX_ENEMY_TRACK; s11++)
				{
					sg_belief_enemy_t *en11 =
					    &sg_caco_enemies[SG_TeamIdx(team)][s11];
					vec3_t dd11;

					if (en11->client < 0 || en11->seed < 0 ||
					    en11->seed >= SG_Rune()->hdr.num_seeds ||
					    SG_AgeAtLeast(en11->seen_time, 4.0f))
						continue;
					VectorSubtract(
					    SG_Rune()->seeds[en11->seed].origin,
					    SG_Rune()->seeds[oc->seed].origin, dd11);
					td11 = VectorLength(dd11);
					if (td11 < best11)
					{
						best11 = td11;
						ts = s11;
					}
				}
				if (ts >= 0)
				{
					static int interpose_field[SG_MAX_SEEDS];
					vec3_t mid;
					int ms = -1, mc = 0;

					/*
					 * EXIT ESCORT (sg_interpose dose 2). The
					 * measurements rejected the midpoint: 6.8 INTERPOSE calls per
					 * carry-second, 2% of kills with a teammate on the kill
					 * line -- the midpoint of a carrier and a 269u threat is
					 * INSIDE the duel, unreachable from the escort's median
					 * 1131u start. Dose 2 occupies the EXIT: the seed a
					 * fixed cost-lead AHEAD of the carrier on its homeward
					 * field -- the door the carrier runs through next.
					 */
					/*
					 * THE FORMATION (sg_interpose dose 3). Lead and trail are
					 * STATIONS on the
					 * carrier's own route, at fixed cost-offsets that move
					 * with it: the leader sweeps the parked defenders ahead
					 * (90% of carrier kills), the trailer bodies the chasers,
					 * and the spacing keeps both out of the rail-and-splash
					 * envelope that made the midpoint useless. Station by
					 * slot parity: even leads at -1300ms, odd trails at
					 * +900ms. Dose 2 (static exit seed) kept as history.
					 */
					if (interpose_mode == 3)
					{
						int *cf = (team == CTF_TEAM_RED)
						    ? sg_fields.to_red_flag : sg_fields.to_blue_flag;
						int cc = cf[oc->seed], s13;
						int threat_seed =
						    sg_caco_enemies[SG_TeamIdx(team)][ts].seed;
						int lead = SG_InterposeLeadStation(cc,
						    cf[threat_seed]);
						int wcost = lead ? cc - 1300 : cc + 900;
						int band = 450;
						float bd13 = -1.0f;

						if (wcost < 0)
							wcost = 0;  /* carrier nearly home: lead collapses to the stand */
						for (s13 = 0; s13 < SG_Rune()->hdr.num_seeds &&
						     s13 < SG_MAX_SEEDS; s13++)
						{
							vec3_t dd13;
							float dl13;

							if (cf[s13] >= SG_FIELD_INF ||
							    cf[s13] < wcost - band || cf[s13] > wcost + band)
								continue;
							VectorSubtract(SG_Rune()->seeds[s13].origin,
							    SG_Rune()->seeds[oc->seed].origin, dd13);
							dl13 = VectorLength(dd13);
							if (bd13 < 0.0f || dl13 < bd13)
							{
								bd13 = dl13;
								ms = s13;
							}
						}
					}
					else if (interpose_mode == 2)
					{
						int *cf = (team == CTF_TEAM_RED)
						    ? sg_fields.to_red_flag : sg_fields.to_blue_flag;
						int cc = cf[oc->seed], s12;
						int want_lo = cc - 2200, want_hi = cc - 900;
						float bd12 = -1.0f;

						for (s12 = 0; s12 < SG_Rune()->hdr.num_seeds &&
						     s12 < SG_MAX_SEEDS; s12++)
						{
							vec3_t dd12;
							float dl12;

							if (cf[s12] >= SG_FIELD_INF ||
							    cf[s12] < want_lo || cf[s12] > want_hi)
								continue;
							VectorSubtract(SG_Rune()->seeds[s12].origin,
							    SG_Rune()->seeds[oc->seed].origin, dd12);
							dl12 = VectorLength(dd12);
							if (bd12 < 0.0f || dl12 < bd12)
							{
								bd12 = dl12;
								ms = s12;
							}
						}
					}

					if (ms < 0)
					{
						VectorAdd(
						    SG_Rune()->seeds[oc->seed].origin,
						    SG_Rune()->seeds[
						        sg_caco_enemies[SG_TeamIdx(team)][ts].seed].origin,
						    mid);
						VectorScale(mid, 0.5f, mid);
						ms = Rune_NearestSeed(SG_Rune(), mid);
					}
					if (ms >= 0)
					{
						Field_Flood(SG_Rune(), interpose_field,
						            &ms, &mc, 1);
						goal_field = interpose_field;
						tc->escort_formation = true;
						if (sg_cv.debug->value &&
						    SG_TimerReady(bot->next_report - 0.9f))
							sg_host.dprint("INTERPOSE %s seed=%d\n",
							           e->client->pers.netname, ms);
					}
				}
			}
		}
		}
		if (ht && ht->inuse && ht->client && !ht->deadflag)
		{
			/*
			 * Escorting the HUMAN who said "cover me": their position is
			 * team knowledge, the same rule our own carrier lives under
			 * (sg_caco.c's header). Flooded fresh each frame, the same
			 * cheap on-demand flood the intercept field uses.
			 */
			static int escort_field[SG_MAX_SEEDS];
			int hs = Rune_NearestSeed(SG_Rune(), ht->s.origin), hc = 0;

			if (hs >= 0)
			{
				Field_Flood(SG_Rune(), escort_field, &hs, &hc, 1);
				if (bot->seed >= 0 && escort_field[bot->seed] < SG_FIELD_INF)
					goal_field = escort_field;
			}
		}
	}
	else
	{
		int team_index = SG_TeamIdx(team);
		int enemy_index = SG_TeamIdx(SG_EnemyTeam(team));

		/* One role-selected escort owns the moving carrier field.  The remaining
		 * attackers keep the enemy base occupied so defenders cannot turn their
		 * full roster onto the return.  Following the enemy-flag-now field here
		 * made every attacker a second escort when the carrier was visible; when
		 * its position was unknown, that field's honest fallback could even lead
		 * the whole attack share back to our own stand. */
		if (SG_AttackObjectiveUsesFixedStand(
		        sg_caco_team_belief.carrier[team_index].client))
			goal_field = enemy_index == 0 ? sg_fields.to_red_flag
			                              : sg_fields.to_blue_flag;
		else
			goal_field = sg_fields.to_flag_now[team_index][enemy_index];
	}
	/* Only principal objective selection borrows the effective strike role.
	 * Optional item, tactics, and relay policy below continue to read the
	 * organic role plus their existing explicit strike-duty gates. */
	role = tc->role;

	/*
	 * THE RUNE COURIER. Candidacy is a lottery -- 107
	 * near-misses one rotation, zero the next -- because holders guard
	 * while carriers sprint. So candidacy itself becomes the errand: a
	 * non-carrier holding RESIST or REGEN while a live carrier runs
	 * bare re-goals onto the carrier's support field for up to eight
	 * seconds, closes, and the toss fires at 400. The rune rides to
	 * the flag on the courier's legs, not on luck.
	 */
	if (sg_cv.runetoss->value &&
	    SG_RuneHandoffEligible(role, carrying, SG_ChatOrderedRole(e),
	        tc->strike_active, tc->escort_mission) &&
	    e->client->rune &&
	    (e->client->rune->runetype == RUNE_RESIST ||
	     e->client->rune->runetype == RUNE_REGEN) &&
	    SG_TimerReady(bot->runetoss_next))
	{
		sg_belief_carrier_t *rc0 = &sg_caco_team_belief.carrier[SG_TeamIdx(team)];

		if (rc0->client >= 0 && rc0->client < game.maxclients)
		{
			edict_t *ce0 = g_edicts + 1 + rc0->client;
			qboolean carrier_allowed = SG_RuneHandoffCarrierAllowed(team,
			    game.maxclients, rc0->client, ce0->inuse, ce0->client != NULL,
			    ce0->health, ce0->deadflag != DEAD_NO,
			    ce0->client ? ce0->client->ctf.teamnum : 0,
			    ce0->client && ClientHasFlag(ce0) != NULL,
			    ce0->client && ce0->client->rune != NULL);

			if (carrier_allowed)
			{
				if (bot->runeconv_until <= 0.0f)
					SG_TimerArm(&bot->runeconv_until, 8.0f);
				if (SG_TimerPending(bot->runeconv_until) &&
				    bot->seed >= 0 &&
				    sg_fields.our_carrier[SG_TeamIdx(team)][bot->seed] <
				        SG_FIELD_INF)
					goal_field = sg_fields.our_carrier[SG_TeamIdx(team)];
				else
				{
					bot->runeconv_until = 0.0f;
					SG_TimerArm(&bot->runetoss_next, 20.0f);
				}
			}
			else
				bot->runeconv_until = 0.0f;
		}
		else
			bot->runeconv_until = 0.0f;
	}

	/*
	 * THE EARLY RETURN (sg_itemlead). Last of the
	 * goal overrides on purpose: the errand is a thing a bot does when nothing
	 * else is happening, and every branch above -- the carrier's stand, the
	 * recovery, the scoop, the interposition, the courier -- is something
	 * happening. Lead_Field refuses the errand outright while any of the jobs
	 * it names is live, so the ordering here and the gates in there say the
	 * same thing twice, which is deliberate: this line is the one a reader
	 * finds first.
	 */
	if (!tc->strike_blocks_optional)
	{
		const int *lead = Lead_Field(bot, role, carrying,
		    SG_ChatOrderedRole(e));

		if (lead)
			goal_field = lead;
	}

	/*
	 * THE MEGA OFFER (sg_megaworth), resolved once for the whole frame and
	 * BEFORE the tactical waypoint is scored -- the waypoint is committed for
	 * up to ten seconds off one Surface_At sweep, so a term that arrived after
	 * it would not reach the route until the next commitment.
	 */
	tc->mega = tc->strike_blocks_optional ? 0.0f
	                                      : Mega_Worth(bot, e, role);

	/*
	 * NO CAMPING THE PAD, and no obsession either -- the offer is bounded in
	 * TIME as well as in road.
	 *
	 * The term's shape has a well at the pad: detour is zero standing on it
	 * and grows in every direction, which is what makes the bot walk there.
	 * Ordinarily the well destroys itself -- arriving means touching the
	 * item, the health goes to 200, SG_WorthMega reads 0 on that same frame
	 * and the well is gone. But a bot that cannot quite reach the pad (a lip
	 * the body will not climb, a door it cannot open) would otherwise sit in
	 * the well indefinitely, and sitting there is worth exactly nothing:
	 * MegaHealth_think bleeds the overheal back off at 1 hp/s from the moment
	 * of pickup and the pad itself is on a 20 s respawn, so the prize is not
	 * something you can wait for the way you wait for a quad. Twelve seconds
	 * of standing offer without a pickup is a route that is not working; drop
	 * it and refuse another for the pad's own respawn period.
	 */
	if (tc->mega > 0.0f && SG_TimerPending(bot->mega_next))
		tc->mega = 0.0f;
	if (tc->mega > 0.0f)
	{
		if (!bot->mega_on)
			SG_Mark(&bot->mega_since);
		else if (SG_AgeOver(bot->mega_since, SG_MEGA_PATIENCE))
		{
			tc->mega = 0.0f;
			SG_TimerArm(&bot->mega_next, SG_MEGA_BACKOFF);
			if (SG_MegaOn() && sg_cv.debug->value)
				sg_host.dprint("MEGA %s give up: %.0fs on offer, no pickup\n",
				           e->client->pers.netname, SG_MEGA_PATIENCE);
		}
	}

	if (SG_MegaOn() && sg_cv.debug->value)
	{
		/* the commit: the frame the offer turns on. The detour reported is
		 * the best one standing from where the bot is now, in ms of extra
		 * road -- back-solved from the value, which is what the surface
		 * actually spends. */
		if (tc->mega > 0.0f && !bot->mega_on && bot->seed >= 0)
		{
			int		pad = -1;
			float	val = Mega_Detour(tc, bot->seed, goal_field, &pad);
			float	det = (val > 0.0f)
			              ? 1500.0f * (tc->mega / val - 1.0f) : -1.0f;

			if (val > 0.0f)
				sg_host.dprint("MEGA %s commit: pad %d hp %d worth %.2f "
				           "detour %.0fms pull %.0f\n",
				           e->client->pers.netname, pad, e->health,
				           tc->mega, det, 1500.0f * val);
		}
		/*
		 * The take. No pickup hook is needed and none is added: the mega is
		 * the only thing in the game that moves a player's health by 100 in
		 * one frame (count 100 with HEALTH_IGNORE_MAX, g_items.c:598-604),
		 * and a respawn cannot forge it because the dead branch above zeroes
		 * mega_hp on the way through.
		 */
		if (bot->mega_hp > 0 && e->health - bot->mega_hp >= 90)
			sg_host.dprint("MEGA %s take: hp %d -> %d\n",
			           e->client->pers.netname, bot->mega_hp, e->health);
	}
	bot->mega_on = (tc->mega > 0.0f);
	bot->mega_hp = e->health;

	bot->last_goalcost = (bot->seed >= 0 &&
	                      goal_field[bot->seed] < SG_FIELD_INF)
	                     ? goal_field[bot->seed] : -1;

	/*
	 * STRATEGY AND TACTICS (sg_tactics). The
	 * architecture: strategy is long-term and hard to change -- the role
	 * and its destination field, already sticky at 0.3 changes a minute.
	 * Tactics are room-scale goals that SERVE it: a committed waypoint
	 * picked from the band 0.8-2.5 seconds down the strategic gradient,
	 * scored ONCE with the full composed surface -- items, danger,
	 * cover, all of it -- then held. Between commitments the per-frame
	 * descent runs on the waypoint's own flood, a single stable field
	 * with nothing to tie against: strategy and tactics stop fighting
	 * in one equation at ten hertz, which is where the Brownian walk
	 * was born. The waypoint retires on arrival, on strategy change,
	 * on staleness (10s), or on unreachability -- the smooth
	 * transition, priced at the tactical boundary and nowhere else.
	 */
	route_field = goal_field;
	route_pure = false;
	if (!tc->strike_blocks_optional && sg_cv.tactics->value &&
	    role != SG_ROLE_ESCORT &&
	    /* CARRY excluded: route_pure suppresses the
	     * danger and detour terms for 10s a commit -- the exact corridors
	     * cover/carrypress/legcarrier exist to keep carriers off */
	    role != SG_ROLE_CARRY && bot->seed >= 0 &&
	    goal_field[bot->seed] < SG_FIELD_INF &&
	    goal_field[bot->seed] >= 400)
	{
		static int tac_fields[SG_MAXBOTS][SG_MAX_SEEDS];
		static unsigned tac_field_epoch[SG_MAXBOTS];
		int bi = (int)(bot - sg_bots);
		qboolean need;

		/* the waypoint must be scored with the FULL surface (the
		 * design's own guarantee) -- this global was previously
		 * whatever the prior bot in the serial frame left behind,
		 * making waypoint quality depend on iteration order */
		sg_route_pure_now = false;
		need = (!Fields_ActionTopologyCurrent(tac_field_epoch[bi]) ||
		                 bot->tac_seed < 0 ||
		                 bot->tac_role != (int)role ||
		                 /* a tac_time AHEAD of the level clock is a
		                  * previous map's timestamp (level.time resets
		                  * to 0 on changelevel; the bots[] array does
		                  * not) -- stale by definition */
		                 SG_TimerPending(bot->tac_time) ||
		                 SG_AgeOver(bot->tac_time, 10.0f) ||
		                 tac_fields[bi][bot->seed] >= SG_FIELD_INF ||
		                 tac_fields[bi][bot->seed] < 300);

		if (need)
		{
			/*
			 * The owner's five questions, as code. (1) this room's
			 * goal: the waypoint, picked from the band one room down
			 * the strategic gradient. (2) the NEXT room's goal: g2,
			 * picked the same way from the band beyond. (3)+(4) the
			 * next room reaches back into this one: each waypoint
			 * candidate pays the graph cost from itself to g2, so
			 * the door chosen out of this room is the one that faces
			 * onward -- a decision here made better because of what
			 * comes next. (5) every band descends the role's own
			 * strategic field: tactics can only ever serve strategy,
			 * never replace it.
			 */
			static int g2_field[SG_MAX_SEEDS];
			int s10, best10 = -1, g2 = -1, cur = goal_field[bot->seed];
			float bv10 = 1e30f, gv10 = 1e30f;

			for (s10 = 0; s10 < SG_Rune()->hdr.num_seeds &&
			     s10 < SG_MAX_SEEDS; s10++)
			{
				float sv;

				if (goal_field[s10] >= SG_FIELD_INF ||
				    goal_field[s10] > cur - 2500 ||
				    goal_field[s10] < cur - 4500)
					continue;
				sv = Surface_At(tc, s10, w, goal_field, support,
				                intercept);
				if (sv < gv10)
				{
					gv10 = sv;
					g2 = s10;
				}
			}
			if (g2 >= 0)
			{
				int gc = 0;

				Field_Flood(SG_Rune(), g2_field, &g2, &gc, 1);
			}
			for (s10 = 0; s10 < SG_Rune()->hdr.num_seeds &&
			     s10 < SG_MAX_SEEDS; s10++)
			{
				float sv;

				if (goal_field[s10] >= SG_FIELD_INF ||
				    goal_field[s10] > cur - 800 ||
				    goal_field[s10] < cur - 2500)
					continue;
				sv = Surface_At(tc, s10, w, goal_field, support,
				                intercept);
				if (g2 >= 0 && g2_field[s10] < SG_FIELD_INF)
					sv += 0.5f * (float)g2_field[s10];
				if (sv < bv10)
				{
					bv10 = sv;
					best10 = s10;
				}
			}
			if (best10 >= 0)
			{
				int cost10 = 0;

				bot->tac_seed = best10;
				SG_Mark(&bot->tac_time);
				bot->tac_role = (int)role;
				Field_Flood(SG_Rune(), tac_fields[bi],
				            &bot->tac_seed, &cost10, 1);
				tac_field_epoch[bi] = sg_fields.action_topology_epoch;
				if (sg_cv.debug->value)
					sg_host.dprint("TACTIC %s seed=%d strat=%d\n",
					           e->client->pers.netname,
					           best10, goal_field[best10]);
			}
			else
				bot->tac_seed = -1;     /* no room ahead: strategy raw */
		}
		if (bot->tac_seed >= 0 &&
		    Fields_ActionTopologyCurrent(tac_field_epoch[bi]) &&
		    tac_fields[bi][bot->seed] < SG_FIELD_INF)
		{
			route_field = tac_fields[bi];
			route_pure = true;      /* tactics were priced at selection:
			                         * the walk itself stays pure */
		}
	}

	/* The sortie is the one explicit route owner that may override tactics.
	 * OUTBOUND follows the current live weapon field with no detour arithmetic;
	 * RETURN follows the fixed own-flag/post field until the post-band finish
	 * fence in sg_descend.  A missing pad or field is a phase edge to RETURN,
	 * never permission to choose another item. */
	if (SG_DefenseSupplyActive(bot))
	{
		const int *home_field = DefenseSupplyHomeField(team);

		if (bot->def_supply_phase == SG_DEF_SUPPLY_RETURN)
		{
			if (home_field)
				goal_field = home_field;
			route_field = goal_field;
			route_pure = true;
		}
		else if (bot->def_supply_phase == SG_DEF_SUPPLY_OUTBOUND)
		{
			const int *weapon_field = DefenseSupplyRouteField(bot,
			                                                  goal_field,
			                                                  &route_pure);

			if (weapon_field == goal_field)
			{
				SG_DefenseSupplyBeginReturn(bot);
				if (home_field)
					goal_field = home_field;
				route_field = goal_field;
				route_pure = true;
			}
			else
			{
				/* Both halves of the descent contract must name the exact
				 * selected pad.  Leaving goal_field on the post would let
				 * late generic filters price/hold against the wrong objective
				 * even though route_field had already been replaced. */
				goal_field = weapon_field;
				route_field = weapon_field;
			}
		}
	}
	bot->last_goalcost = (bot->seed >= 0 &&
	                      goal_field[bot->seed] < SG_FIELD_INF)
	                     ? goal_field[bot->seed] : -1;
	/* SCOOP is an enemy-flag touch mission without being an attack-pressure
	 * mission.  Publish that distinction only after every later objective
	 * override: terminal movement may finish the physical relay pickup, while
	 * attack-only rally/grenade policy remains disabled for the escort. */
	tc->scoop_mission = role == SG_ROLE_ESCORT &&
	    sg_caco_team_belief.carrier[SG_TeamIdx(team)].client < 0 &&
	    sg_caco_team_belief.flag[SG_TeamIdx(team)]
	        [SG_TeamIdx(SG_EnemyTeam(team))].state == SG_FLAG_ASTRAY &&
	    goal_field == sg_fields.to_flag_now[SG_TeamIdx(team)]
	        [SG_TeamIdx(SG_EnemyTeam(team))];

	tc->goal_field = goal_field;
	tc->route_field = route_field;
	tc->route_pure = route_pure;
}
