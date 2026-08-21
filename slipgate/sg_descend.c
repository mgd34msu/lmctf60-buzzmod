/* Route selection, commitment, and descent for proved links. */
#include "g_local.h"
#include "g_ctffunc.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_action.h"
#include "slipgate/sg_combat.h"
#include "slipgate/sg_chat.h"
#include "slipgate/sg_persona.h"
#include "slipgate/sg_net.h"
#include "slipgate/sg_cvars.h"
#include "slipgate/sg_util.h"
#include "slipgate/sg_bot.h"
#include "slipgate/sg_compound_drop_game.h"
#include "slipgate/sg_bot_ping.h"
#include "slipgate/sg_carrier_cover.h"
#include "slipgate/sg_declared_door_guard.h"
#include "slipgate/sg_rune_binding.h"
#include "slipgate/sg_drop_live.h"
#include "slipgate/sg_swim_live.h"
#include "slipgate/sg_clock.h"
#include "slipgate/sg_danger.h"
#include "slipgate/sg_defense_shift.h"
#include "slipgate/sg_defense_facing.h"
#include "slipgate/sg_defense_supply.h"
#include "slipgate/sg_strike.h"
#include "slipgate/sg_traversal_transition.h"
#include "slipgate/sg_tilt.h"
#include "slipgate/sg_lead.h"
#include "slipgate/sg_price.h"
#include "slipgate/sg_route_policy.h"
#include "slipgate/sg_role_policy.h"
#include "slipgate/sg_route_dither.h"
#include "slipgate/sg_route_jitter.h"
#include "slipgate/sg_ribbon_random.h"
#include "slipgate/sg_escape_random.h"
#include "slipgate/sg_rune_handoff_policy.h"
#include "slipgate/sg_field_projection.h"
#include "slipgate/sg_descend.h"
#include "slipgate/sg_goal.h"      /* sg_grab_time, sg_push_until */
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_move.h"

void		Cmd_Kill_f(edict_t *ent);
void		ClientThink(edict_t *ent, usercmd_t *ucmd);

#define SG_DUEL_RANGE_MS	1.5f
#define SG_DUEL_COVER_MS	900.0f

static void Swim_LivePose(const edict_t *e, sg_replay_pose_t *pose)
{
	SG_SwimLivePose(pose, e && e->client ? &e->client->ps.pmove : NULL,
	    e ? e->s.origin : NULL, e ? e->velocity : NULL,
	    e && e->groundentity != NULL, e ? e->watertype : 0,
	    e ? e->waterlevel : 0);
}

static qboolean Swim_LiveArrival(const sg_swim_replay_spec_t *spec,
	const sg_replay_pose_t *pose, void *context)
{
	edict_t *e = (edict_t *)context;

	if (!spec || !pose || !e)
		return false;
	return SG_SwimArrived(pose->origin, spec->destination,
	    spec->destination_water, pose->grounded, pose->watertype,
	    pose->waterlevel, e);
}

static void Swim_LiveFallbackLog(const edict_t *e, int link_index,
	const sg_swim_live_result_t *result)
{
	if (!result || result->outcome != SG_SWIM_LIVE_FALLBACK ||
	    !sg_cv.debug->value)
		return;
	sg_host.dprint("SWIMREPLAYFALLBACK %s link=%d phase=boundary adapter=%s "
	               "replay=%s\n",
	    e && e->client ? e->client->pers.netname : "?", link_index,
	    SG_SwimLiveFailureName(result->failure),
	    SG_ReplayReasonName(result->replay_reason));
}

/* Watchdogs judge the route that owns this frame, not the organic role that
 * preceded a coordinator overlay.  A concrete ESCORT may hold near its carrier;
 * every other strike duty removes an obsolete organic escort exemption. */
static qboolean ThinkMissionHold(const sg_bot_t *bot, const sg_think_t *tc,
	const int *goal_field)
{
	int role;
	int goal_cost;
	edict_t *ordered_escort = NULL;
	qboolean ordered_terminal = false;

	if (!bot || !tc || !goal_field)
		return false;
	role = tc->escort_mission ? SG_ROLE_ESCORT : tc->role;
	if (tc->strike_active && !tc->escort_mission)
		role = SG_ROLE_ATTACK;
	goal_cost = bot->seed >= 0 && bot->seed < SG_Rune()->hdr.num_seeds
	    ? goal_field[bot->seed] : SG_FIELD_INF;
	if (!tc->strike_active && tc->escort_mission)
		ordered_escort = SG_ChatEscortTarget(tc->e);
	if (ordered_escort)
		ordered_terminal = SG_EscortTerminal(tc->e, ordered_escort);
	return SG_RoleMissionHold(role, goal_cost, ordered_terminal,
	    tc->scoop_mission);
}

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

/* The generator judges proved JUMP/DROP arrival only at 100 ms controller
 * boundaries. Use the identical envelope here before retiring the commitment;
 * seed identity and a cheaper field value are navigation hints, not proof that
 * the body landed on the demonstrated side of a wall or ledge. */
static qboolean Ballistic_Arrived(edict_t *e, const rune_link_t *l)
{
	vec3_t d, from, to;
	trace_t tr;
	qboolean accepted = false;

	VectorSubtract(SG_Rune()->seeds[l->to].origin, e->s.origin, d);
	if (d[0] * d[0] + d[1] * d[1] >= 40.0f * 40.0f ||
	    d[2] <= -72.0f || d[2] >= 72.0f ||
	    (e->waterlevel > 0 &&
	     (e->watertype & (CONTENTS_LAVA | CONTENTS_SLIME))) ||
	    ((SG_Rune()->seeds[l->to].flags & RSF_WATER) ?
	         e->waterlevel != 3 :
	         ((!e->groundentity && e->waterlevel < 2) ||
	          (e->groundentity && e->groundentity != g_edicts &&
	           !SG_ImmutableSupport(e->groundentity)))))
		goto result;
	VectorCopy(e->s.origin, from);
	VectorCopy(SG_Rune()->seeds[l->to].origin, to);
	from[2] += 16.0f;
	to[2] += 16.0f;
	tr = sg_host.trace(from, NULL, NULL, to, e, MASK_PLAYERSOLID);
	accepted = !tr.startsolid && tr.fraction >= 1.0f;
result:
	return accepted;
}

/* A DROP may touch down between 64-unit lattice seeds. The proof permits one
 * production-aligned dry landing inside this clear envelope, then owns a
 * ground-only recovery to the exact destination. This predicate is the live
 * half of that single handoff; it is deliberately not a general arrival. */
static qboolean Drop_RecoveryReady(edict_t *e, const rune_link_t *l)
{
	vec3_t d, from, to;
	trace_t tr;
	qboolean accepted = false;

	if (!e || !l || !e->groundentity ||
	    (e->groundentity != g_edicts &&
	     !SG_ImmutableSupport(e->groundentity)) ||
	    e->waterlevel != 0 ||
	    (e->watertype & (CONTENTS_LAVA | CONTENTS_SLIME)))
		goto result;
	VectorSubtract(SG_Rune()->seeds[l->to].origin, e->s.origin, d);
	if (d[0] * d[0] + d[1] * d[1] >=
	        RUNE_DROP_RECOVERY_RADIUS * RUNE_DROP_RECOVERY_RADIUS ||
	    d[2] <= -RUNE_DROP_RECOVERY_Z || d[2] >= RUNE_DROP_RECOVERY_Z)
		goto result;
	VectorCopy(e->s.origin, from);
	VectorCopy(SG_Rune()->seeds[l->to].origin, to);
	from[2] += 16.0f;
	to[2] += 16.0f;
	tr = sg_host.trace(from, NULL, NULL, to, e, MASK_PLAYERSOLID);
	accepted = !tr.startsolid && !tr.allsolid && tr.fraction >= 1.0f;
result:
	return accepted;
}

typedef struct drop_live_contact_context_s
{
	edict_t *ent;
	const rune_link_t *link;
} drop_live_contact_context_t;

static qboolean Drop_LiveSupportValid(const edict_t *e)
{
	return e && e->groundentity &&
	       (e->groundentity == g_edicts ||
	        SG_ImmutableSupport(e->groundentity));
}

static void Drop_LivePose(const edict_t *e, sg_replay_pose_t *pose)
{
	SG_DropLivePose(pose, e && e->client ? &e->client->ps.pmove : NULL,
	    e ? e->s.origin : NULL, e ? e->velocity : NULL,
	    e && e->groundentity != NULL, e ? e->watertype : 0,
	    e ? e->waterlevel : 0);
}

static qboolean Drop_LiveArrival(const sg_drop_replay_spec_t *spec,
	const sg_replay_pose_t *pose, void *context)
{
	drop_live_contact_context_t *contact =
	    (drop_live_contact_context_t *)context;

	(void)spec;
	(void)pose;
	return contact && contact->ent && contact->link &&
	       Ballistic_Arrived(contact->ent, contact->link);
}

static qboolean Drop_LiveRecovery(const sg_drop_replay_spec_t *spec,
	const sg_replay_pose_t *pose, void *context)
{
	drop_live_contact_context_t *contact =
	    (drop_live_contact_context_t *)context;

	(void)spec;
	(void)pose;
	return contact && contact->ent && contact->link &&
	       Drop_RecoveryReady(contact->ent, contact->link);
}

static void Drop_LiveSync(sg_bot_t *bot)
{
	if (!bot)
		return;
	bot->drop_walkoff = bot->drop_replay.walkoff;
	bot->drop_airborne = bot->drop_replay.airborne;
	bot->drop_recover = bot->drop_replay.recovery;
}

static void Drop_LiveBoundaryLog(const edict_t *e, int link_index,
	const sg_drop_live_result_t *result)
{
	if (!result || result->outcome == SG_DROP_LIVE_RUNNING ||
	    result->outcome == SG_DROP_LIVE_ARRIVED || !sg_cv.debug->value)
		return;
	sg_host.dprint("DROPREPLAY%s %s link=%d phase=boundary adapter=%s "
	               "replay=%s\n",
	    result->outcome == SG_DROP_LIVE_FAILED ? "FAIL" : "FALLBACK",
	    e && e->client ? e->client->pers.netname : "?", link_index,
	    SG_DropLiveFailureName(result->failure),
	    SG_ReplayReasonName(result->replay_reason));
}

/* Price proven outgoing links and select the next commitment. */
static qboolean Carrier_LinkShelved(const sg_bot_t *bot, int link);
int Think_PickLink(sg_bot_t *bot, sg_think_t *tc)
{
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
	qboolean supply_route = SG_DefenseSupplyActive(bot) && route_pure;
	vec3_t approach_flag_origin;
	sg_flag_approach_source_t approach_source = SG_FLAG_APPROACH_NONE;
	float approach_flag_distance = 0.0f;
	qboolean approach_flag_touch = false;
	int nonworsening_route_neighbors = 0, descending_run_available = 0;
	int attack_descent_link = -1, best_candidate_goal_ms = SG_FIELD_INF;
	float attack_descent_value = 1e30f;
	qboolean enemy_pressure = tc->strike_pressure;
	qboolean enemy_touch_mission = SG_EnemyFlagTouchMissionActive(
	    tc->strike_pressure, tc->scoop_mission);
	qboolean duel_route_price = SG_DuelRoutePriceAllowed(duel,
	    enemy_pressure, sg_cv.press->value != 0.0f,
	    role == SG_ROLE_CARRY, sg_cv.carrypress->value != 0.0f);
	sg_defense_supply_neighbor_t supply_neighbors[64];
	unsigned supply_neighbor_count = 0;
	/* Home is public. Astray approach uses only the team's bounded belief seed. */
	if (enemy_touch_mission && bot->seed >= 0 &&
	    bot->seed < SG_Rune()->hdr.num_seeds)
	{
		edict_t *flag = SG_EnemyFlag(team);
		sg_belief_flag_t *belief = &sg_caco_team_belief.flag[SG_TeamIdx(team)]
		    [SG_TeamIdx(SG_EnemyTeam(team))];
		qboolean home = flag && ctf_flagathome(flag);
		approach_source = SG_StrikeFlagApproachSource(enemy_touch_mission, home,
		    belief->where_seed, SG_Rune()->hdr.num_seeds);
		if (approach_source == SG_FLAG_APPROACH_HOME)
			VectorCopy(flag->s.origin, approach_flag_origin);
		else if (approach_source == SG_FLAG_APPROACH_BELIEF)
			VectorCopy(SG_Rune()->seeds[belief->where_seed].origin,
			    approach_flag_origin);
		if (approach_source != SG_FLAG_APPROACH_NONE)
		{
			approach_flag_distance = SG_DistXY(e->s.origin, approach_flag_origin);
			if (approach_flag_distance <= 600.0f &&
			    fabsf(approach_flag_origin[2] - e->s.origin[2]) <= 96.0f)
			{
				/* Distance does not grant movement through a wall or other floor;
				 * the shared hull trace still owns the real touch. */
				approach_flag_touch =
				    SG_AttackFlagDirectTouchAuthority(e, team, NULL);
			}
		}
	}
	/* Count non-worsening neighbors and prove a descending RUN exists. */
	if (bot->seed >= 0 && bot->seed < SG_Rune()->hdr.num_seeds)
		for (li = SG_Rune()->first_link[bot->seed]; li >= 0;
		     li = SG_Rune()->next_link[li])
		{
			const rune_link_t *neighbor = &SG_Rune()->links[li];
			int prior;
			qboolean duplicate = false, descends, shelved;

			if (neighbor->to < 0 || neighbor->to >= SG_Rune()->hdr.num_seeds ||
			    neighbor->to == bot->seed)
				continue;
			descends = goal_field[bot->seed] < SG_FIELD_INF &&
			    SG_RouteCandidateGoalMs(goal_field[neighbor->to],
			        Fields_LinkTraversalCostMs(neighbor), SG_FIELD_INF) <
			        goal_field[bot->seed];
			shelved = Carrier_LinkShelved(bot, li);
			if (SG_HookFootRouteAvailable(neighbor->action == RL_RUN,
			        descends, shelved))
				descending_run_available = true;
			if (route_field[bot->seed] < SG_FIELD_INF &&
			    SG_RouteCandidateGoalMs(route_field[neighbor->to],
			        Fields_LinkTraversalCostMs(neighbor), SG_FIELD_INF) <=
			        route_field[bot->seed])
			{
				/* Different actions to one seed are one route choice. */
				for (prior = SG_Rune()->first_link[bot->seed];
				     prior >= 0 && prior != li;
				     prior = SG_Rune()->next_link[prior])
					if (SG_Rune()->links[prior].to == neighbor->to)
					{
						duplicate = true;
						break;
					}
				if (!duplicate)
					nonworsening_route_neighbors++;
			}
		}

	/* life ticker for the route-jitter seed */
	if (e->health <= 0)
		bot->was_dead = 1;
	else if (bot->was_dead)
	{
		bot->was_dead = 0;
		bot->lives++;
		bot->inlinks_n = 0;     /* a new life rides in on its own roads */

		/* Add a skill-scaled orientation delay after respawn, but not on the
		 * initial level spawn where joins are already staggered. */
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
	 * so bots blend into ordinary scoreboard ranges, analytics included. */
	e->client->ping = SG_BotPingValue(bot->fake_ping,
	    bot->instance_token, level.framenum);
	/* leg ticker: a new role is a new errand -- new opinion of the map */
	if ((int)role != bot->last_role_for_legs)
	{
		bot->last_role_for_legs = (int)role;
		bot->legs++;
	}

	/* Pricing terms were resolved before Objective because its tactical
	 * waypoint search already calls Surface_At. Mega and route purity are the
	 * two values Objective contributes before this final link fan. */
	sg_route_pure_now = route_pure;

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
	if (duel_route_price)
	{
		bestval += Duel_Price(e, SG_Rune()->seeds[bot->seed].origin, duel_org,
		                      duel_want, duel_expo);
		/* The incumbent pays the same exposure term as each candidate,
		 * under the same forward-pressure gate. */
		if (duel_expo > 0.0f)
			bestval += duel_expo *
			    (float)SG_Rune()->seeds[bot->seed].area_hint * 1.8f;
	}
	/* A submerged carrier refuses descending links when a level or ascending
	 * exit exists. One-way underwater routes remain traversable. */
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

	{
		qboolean linger_hot = false;
		vec3_t car_org = { 0, 0, 0 };

		if ((sg_cv.unlinger->value > 0.0f ||
		     sg_cv.depace->value > 0.0f) &&
		    SG_AntiLingerEligible(role, tc->escort_mission))
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
				if (SG_AntiLingerCarrierNearby(SG_CanSee(e, car->s.origin,
				        car->viewheight), VectorLength(cd)))
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
		int edge_ms = Fields_LinkTraversalCostMs(l);
		int candidate_goal_ms =
		    SG_RouteCandidateGoalMs(goal_field[l->to], edge_ms, SG_FIELD_INF);
		int candidate_route_ms =
		    SG_RouteCandidateGoalMs(route_field[l->to], edge_ms, SG_FIELD_INF);
		float v = SG_RouteCandidatePrice(
		    Surface_At(tc, l->to, w, route_field, support, intercept),
		    edge_ms, Surface_ObjectiveWeight(tc, w));
		/* Route policy inherits the suffix; hook_water and the readiness branch
		 * below remain exact bare-HOOK controller checks. */
		qboolean hook_policy = SG_ActionUsesHookPolicy(l->action);
		qboolean hook_water = l->action == RL_HOOK &&
		    (SG_Rune()->seeds[l->from].flags & RSF_WATER);
		sg_rune_mechanism_binding_t mechanism_binding = { 0 };
		qboolean mechanism_bound = false;
		if (approach_source != SG_FLAG_APPROACH_NONE)
		{
			float candidate_distance = SG_DistXY(
			    SG_Rune()->seeds[l->to].origin, approach_flag_origin);
			v += SG_StrikeFlagApproachPrice(true, approach_flag_touch,
			    l->action == RL_RUN,
			    approach_flag_distance, candidate_distance,
			    SG_Rune()->seeds[l->to].origin[2] - approach_flag_origin[2],
			    goal_field[bot->seed], candidate_goal_ms);
		}
		if (SG_ActionMechanismPlanRequired(l->action))
		{
			if (!SG_RuneMechanismBindingCapture(SG_Rune(), (uint32_t)li,
			        &mechanism_binding))
				continue;
			mechanism_bound = true;
		}
		/* Never begin a second ballistic action in midair. An already committed
		 * jump/drop is restored below; new candidates wait for a landing. */
		if (e->groundentity != g_edicts &&
		    !SG_ImmutableSupport(e->groundentity) &&
		    (l->action == RL_JUMP || l->action == RL_DROP ||
		     l->action == RL_ROCKETJUMP))
			continue;
		if (SG_ActionOwnsControl(l->action) &&
		    (bot->hook_phase != 0 || SG_RocketJumpLiveOwns(&bot->rocketjump) ||
		     bot->nade_phase != 0))
			continue;       /* one exact action owns the command at a time */
		if (l->action == RL_LIFT)
		{
			edict_t *plat = mechanism_bound
			    ? mechanism_binding.mover_entity : NULL;
			/* A declared lift is executable only when its exact map entity is
			 * waiting at the bottom, or this body is already riding it. Do not
			 * walk into an empty shaft while the platform is parked above. */
			if (!plat || (e->groundentity != plat &&
			              plat->moveinfo.state != SG_PLAT_STATE_BOTTOM))
				continue;
		}
		if (l->action == RL_JUMP && l->min_speed > 0)
		{
			vec3_t source_delta;
			float speed = sqrtf(e->velocity[0] * e->velocity[0] +
			                    e->velocity[1] * e->velocity[1]);
			float heading, want_heading, delta_heading, slack, source_horiz;

			VectorSubtract(SG_Rune()->seeds[l->from].origin,
			               e->s.origin, source_delta);
			source_horiz = sqrtf(source_delta[0] * source_delta[0] +
			                     source_delta[1] * source_delta[1]);

			if (source_horiz > 6.0f || fabsf(source_delta[2]) > 4.0f ||
			    speed < (float)l->min_speed * 4.0f)
				continue;
			heading = atan2f(e->velocity[1], e->velocity[0]) *
			          180.0f / (float)M_PI;
			want_heading = l->heading * (360.0f / 256.0f);
			delta_heading = heading - want_heading;
			while (delta_heading > 180.0f) delta_heading -= 360.0f;
			while (delta_heading < -180.0f) delta_heading += 360.0f;
			slack = l->heading_slack * (360.0f / 256.0f);
			if (fabsf(delta_heading) > slack)
				continue;
		}
		/* Pmove proves geometry, while P_FallingDamage lives outside the
		 * phantom. Use the exact same conservative bound here and again at
		 * launch: otherwise an unsafe cheapest edge is selected, rejected at
		 * its source, then selected forever. */
		if ((l->action == RL_JUMP || l->action == RL_DROP) &&
		    !SG_BallisticSurvivable(e, l))
			continue;

		/* Graph hooks prove the offhand production schedule.  A
		 * weapon-held grapple has activation/fire frames and is a different
		 * controller, so it is not an executable edge in this graph. */
		if (l->action == RL_HOOK)
		{
			if (!SG_HookOffhandReady(e))
				continue;
			if (hook_water)
			{
				if ((SG_Rune()->seeds[l->to].flags & RSF_WATER) ||
				    e->waterlevel < 2 || !(e->watertype & CONTENTS_WATER) ||
				    (e->watertype & (CONTENTS_LAVA | CONTENTS_SLIME)) ||
				    (e->waterlevel >= 3 &&
				     SG_TimerRemaining(e->air_finished) <
				         ((role == SG_ROLE_CARRY) ? 8.0f : 4.0f)))
					continue;
			}
			else if (e->waterlevel > 0 ||
			         (e->groundentity != g_edicts &&
			          !SG_ImmutableSupport(e->groundentity)))
				continue;
		}

		if (linger_hot)
		{
			vec3_t ld9;

			VectorSubtract(SG_Rune()->seeds[l->to].origin, car_org, ld9);
			if (VectorLength(ld9) < 400.0f)
				v += sg_cv.unlinger->value;
		}

		/* Per-visit noise breaks near-ties without overruling one hop of route
		 * gradient. The salt remains stable until the bot changes seed. */
		if (sg_cv.routedither->value > 0.0f)
		{
			unsigned dh = bot->dither_salt ^ (unsigned)li * 2654435761u;

			dh ^= dh >> 13; dh *= 2246822519u; dh ^= dh >> 16;
			v += sg_cv.routedither->value *
			     (float)(dh & 1023) / 1023.0f;
		}

		if (SG_HookNearGoalSkipAllowed(hook_policy,
		        role == SG_ROLE_CARRY, descending_run_available,
		        goal_field[bot->seed], SG_FIELD_INF))
			continue;
		/* Under enemy pressure, prefer covered final approaches when route
		 * costs are otherwise close. */
		if (enemy_pressure && goal_field[bot->seed] < 4000 &&
		    goal_field[bot->seed] < SG_FIELD_INF)
		/* Keep exposure preference below one typical goal-field step. */
			v += 0.5f * (float)SG_Rune()->seeds[l->to].area_hint;

		/* Penalize nearby senior attackers to spread pressure across routes. */
		if (enemy_pressure && bot->seed >= 0 &&
		    goal_field[bot->seed] < SG_FIELD_INF &&
		    goal_field[bot->seed] > 2500 && goal_field[bot->seed] < 12000)
		{
			int bi6;

			for (bi6 = 0; bi6 < SG_MAXBOTS; bi6++)
			{
				sg_bot_t *mb6 = &sg_bots[bi6];
				vec3_t md6;

				if (mb6 == bot || !mb6->ent ||
				    !SG_CoordinationBodyLive(mb6->active, mb6->ent->inuse,
				        mb6->ent->deadflag, mb6->ent->health))
					continue;
				if (mb6->ent->client->ctf.teamnum != team)
					continue;
				if (!SG_StrikeEnemyPressureSnapshot(mb6))
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

		if (Carrier_LinkShelved(bot, li))
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
		/* Let breath handling own the risk while carriers prefer protected
		 * swim routes. */
		if (role == SG_ROLE_CARRY && l->action == RL_SWIM &&
		    sg_cv.watercarry->value)
			v -= 800.0f;

		/* the sink ban's teeth: 12000 exceeds the basin's worst gap
		 * (max eff link 4055 + field spread 2221), so any non-sinking
		 * candidate wins; the pre-pass above guarantees one exists */
		/* widened per the pre-registered fallback (416 analysis: two
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

		/* Charge movement into a masked sub-stand shelf, including lateral
		 * entries. Ascending exits from the shelf remain free. */
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

		if (role == SG_ROLE_CARRY && hook_policy)
		{
			/* Price the hook's stationary aim phase only when a fresh nearby
			 * contact can punish it. */
			int s2;

			for (s2 = 0; s2 < SG_MAX_ENEMY_TRACK; s2++)
			{
				sg_belief_enemy_t *en = &sg_caco_enemies[SG_TeamIdx(team)][s2];
				vec3_t pd;

				if (en->client < 0 || en->seed < 0 ||
				    SG_AgeAtLeast(en->seen_time, 4.0f))
					continue;
				/* A distant pursuer cannot punish the hook's brief aim phase. */
				VectorSubtract(SG_Rune()->seeds[en->seed].origin,
				               e->s.origin, pd);
				if (VectorLength(pd) < 700.0f)
				{
					/* A coordinator-assigned screen lowers the risk of the
					 * carrier's stationary hook aim. */
					v += SG_StrikeCarrierHookRisk(tc->carrier_screened);
					break;
				}
			}
		}
		if (role == SG_ROLE_CARRY && bot->carry_startcost < 0 &&
		    bot->seed >= 0 && goal_field[bot->seed] < SG_FIELD_INF)
			bot->carry_startcost = goal_field[bot->seed];

		/* A 2500-cost regression means the carrier left its planned route.
		 * Retire stale shelves and rebase progress at the current seed. */
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
				/* Keep shelves observed within the last three seconds. */
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
			/* Suppress flee dodging until the carrier has made enough route
			 * progress to leave the breakout area. */
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
						 * open country past the breakout, where three
						 * clock-outs at 64-98% home show the
						 * full dodge tax costs more match clock than
						 * it saves in blood */
						v += 1.5f * (400.0f - VectorLength(d));
				}
			}
		}

		else if (duel_route_price)
		{
			/* Pressing attackers and fleeing carriers do not sacrifice route
			 * progress to maintain their weapon's preferred duel range. */
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

		/* Discount links in proportion to observed human use. */
		if (sg_human_use &&
		    sg_cv.humanprior->value)
			v -= 1.5f * (float)sg_human_use[li];

		/* During live-flag play, discount links from the matching human
		 * route sample for non-carriers. */
		if (sg_human_live &&
		    sg_cv.flagprior->value &&
		    tc->role != SG_ROLE_CARRY &&
		    (sg_caco_team_belief.flag[0][0].state == SG_FLAG_ASTRAY ||
		     sg_caco_team_belief.flag[0][1].state == SG_FLAG_ASTRAY))
			/* This sample represents hunters, not carrier escape routes. */
			v -= 1.5f * sg_cv.flagprior->value *
			     (float)sg_human_live[li];

		/* Bias defenders toward high-dwell approach posts. */
		if (tc->role == SG_ROLE_DEFEND &&
		    sg_def_post[SG_TeamIdx(team)] &&
		    sg_cv.defpost->value > 0)
			v -= 1.5f * sg_cv.defpost->value *
			     (float)sg_def_post[SG_TeamIdx(team)][l->to];

		/* When the flag is stolen, bias defenders toward learned interception
		 * seeds instead of only the carrier's current position. */
		if (tc->role == SG_ROLE_DEFEND &&
		    sg_def_icept[SG_TeamIdx(team)] &&
		    sg_caco_team_belief.flag[SG_TeamIdx(team)][SG_TeamIdx(team)].state ==
		        SG_FLAG_ASTRAY &&
		    sg_cv.defreact->value > 0)
			v -= 1.5f * sg_cv.defreact->value *
			     (float)sg_def_icept[SG_TeamIdx(team)][l->to];

		/* Carrier-only prior derived from post-steal carrier trajectories. */
		if (sg_human_escape &&
		    tc->role == SG_ROLE_CARRY &&
		    sg_cv.escapeprior->value > 0)
			v -= 1.5f * sg_cv.escapeprior->value *
			     (float)sg_human_escape[li];

		/* Price exposed attack approaches against fresh nearby sightings. */
		if (enemy_pressure &&
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

		/* Price candidate endpoints visible to the selected rail threat. The
		 * surcharge remains below one hop of goal gradient. */
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

		/* Carriers pay for candidate steps visible to the freshest nearby
		 * eye-confirmed enemy. */
		if (tc->role == SG_ROLE_CARRY &&
		    sg_cv.carrycover->value > 0)
		{
			int			cs, best_cs = -1;
			float		best_t = -1.0f;

			for (cs = 0; cs < SG_MAX_ENEMY_TRACK; cs++)
			{
				sg_belief_enemy_t *en = &sg_caco_enemies[SG_TeamIdx(team)][cs];

				if (en->client >= 0 && !en->heard_only &&
				    en->seed >= 0 &&
				    en->seed < SG_Rune()->hdr.num_seeds &&
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
				/* Distant sightings do not bend the carrier route. */
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

		/* Stable per-life variety may break a near-tie, but must not scale the
		 * whole map-wide objective cost. */
		if (SG_RouteJitterAllowed(route_pure, sg_cv.routejitter->value))
		{
			unsigned rj = SG_RouteJitterDraw(bot->instance_token,
			    (unsigned)(e - g_edicts - 1), (unsigned)bot->lives,
			    (unsigned)bot->legs, (unsigned)li);

			v += SG_RouteJitterOffset(rj, sg_cv.routejitter->value);
		}

		/* Penalize a return to the previous seed when another non-worsening
		 * route exists. */
		if (SG_RouteReturnPenaltyAllowed(bot->prev_seed, l->to,
		        SG_AgeUnder(bot->prev_seed_time, 3.0f),
		        nonworsening_route_neighbors, route_pure,
		        sg_cv.nobacktrack->value))
			v *= 1.0f + sg_cv.nobacktrack->value / 100.0f;

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

		if (role == SG_ROLE_CARRY && bot->escprior_bucket >= 0 &&
		    SG_TimerPending(bot->escprior_until) && v > 0.0f)
		{
			float ex = SG_Rune()->seeds[l->to].origin[0] - bot->escprior_org[0];
			float ey = SG_Rune()->seeds[l->to].origin[1] - bot->escprior_org[1];

			if (ex * ex + ey * ey > 160.0f * 160.0f &&
			    SG_Bearing8(ex, ey) == bot->escprior_bucket)
				v *= 1.0f - bot->escprior_dose;
		}

		/* Preserve the exact admitted fan for the explicit supply route
		 * chooser.  It is collected after all action/geometry gates above,
		 * before sticky or other generic preferences can perturb the route. */
		if (supply_route &&
		    SG_DefenseSupplyActionAllowed(
		        (sg_defense_supply_phase_t)bot->def_supply_phase,
		        l->action == RL_RUN) &&
		    supply_neighbor_count <
		    sizeof(supply_neighbors) / sizeof(supply_neighbors[0]) &&
		    route_field[l->to] < SG_FIELD_INF)
		{
			supply_neighbors[supply_neighbor_count].link_index = li;
			supply_neighbors[supply_neighbor_count].to_seed = l->to;
			supply_neighbors[supply_neighbor_count].route_cost_ms =
			    candidate_route_ms;
			supply_neighbor_count++;
		}

		if (bot->sticky_link == li &&
		    sg_cv.sticky->value)
			v *= 0.85f;

		if (li == bot->sticky_link)
			incumbent_v = v;

		if (SG_AttackDescentFallbackAllowed(enemy_touch_mission,
		        l->action == RL_RUN, goal_field[bot->seed],
		        candidate_goal_ms, SG_FIELD_INF) && v < attack_descent_value)
		{
			attack_descent_link = li;
			attack_descent_value = v;
		}

		if (v < bestval)
		{
			bestval = v;
			bestlink = li;
			best_candidate_goal_ms = candidate_goal_ms;
		}
	}
	}       /* anti-linger scope */
	if (attack_descent_link >= 0 &&
	    (bestlink < 0 || SG_AttackDescentOverrideNeeded(
	        enemy_touch_mission, goal_field[bot->seed],
	        best_candidate_goal_ms, SG_FIELD_INF)))
	{
		bestlink = attack_descent_link;
		bestval = attack_descent_value;
		incumbent_v = 1e30f;
	}
	if (supply_route && route_field[bot->seed] < SG_FIELD_INF)
	{
		int exact_link = -1;

		if (supply_neighbor_count > 0)
			exact_link = SG_DefenseSupplyChooseNeighbor(
			    supply_neighbors, supply_neighbor_count,
			    route_field[bot->seed]);

		if (exact_link >= 0)
		{
			bestlink = exact_link;
			bestval = (float)SG_RouteCandidateGoalMs(
			    route_field[SG_Rune()->links[exact_link].to],
			    Fields_LinkTraversalCostMs(&SG_Rune()->links[exact_link]),
			    SG_FIELD_INF);
		}
		else
		{
			/* The exact field has no currently admitted descending edge. Do
			 * not fall through to the ordinary worth/hold argmin.  In particular,
			 * an OUTBOUND route whose next edge is a mechanism returns now rather
			 * than waiting at the post for the five-second deadline. */
			if (bot->def_supply_phase == SG_DEF_SUPPLY_OUTBOUND)
				SG_DefenseSupplyBeginReturn(bot);
			bestlink = -1;
			bestval = 0.0f;
		}
	}

	tc->bestval = bestval;
	tc->incumbent_v = incumbent_v;
	tc->rail_seed = rail_seed;
	tc->rail_client = rail_client;
	tc->rail_dose = rail_dose;
	tc->rail_hold = rail_hold;
	tc->bestlink = bestlink;
	return bestlink;
}

sg_run_completion_t SG_RunCommitCompletion(const rune_t *rune,
	const rune_link_t *link, int localized_seed, const vec3_t body_origin,
	const int *goal_field)
{
	vec3_t delta;

	if (!rune || !rune->seeds || !link || !body_origin ||
	    link->action != RL_RUN || link->from < 0 ||
	    link->from >= rune->hdr.num_seeds || link->to < 0 ||
	    link->to >= rune->hdr.num_seeds)
		return SG_RUN_INCOMPLETE;
	if (localized_seed == link->to)
		return SG_RUN_ARRIVED;
	VectorSubtract(rune->seeds[link->to].origin, body_origin, delta);
	if (VectorLength(delta) < 48.0f)
		return SG_RUN_ARRIVED;
	if (goal_field && localized_seed >= 0 &&
	    localized_seed < rune->hdr.num_seeds &&
	    goal_field[localized_seed] <= goal_field[link->to])
		return SG_RUN_OVERACHIEVED;
	return SG_RUN_INCOMPLETE;
}

qboolean SG_RunMechanismPlanCandidateValid(const rune_t *rune, int seed,
	int link_index)
{
	const rune_link_t *candidate;
	const rune_mechanism_plan_t *plan;

	if (!rune || !rune->links || !rune->mechanism_plans || seed < 0 ||
	    seed >= rune->hdr.num_seeds || link_index < 0 ||
	    link_index >= rune->hdr.num_links)
		return false;
	candidate = &rune->links[link_index];
	if (candidate->from != seed ||
	    !SG_ActionMechanismAdmitted(candidate->action) ||
	    !SG_ActionMechanismPlanRequired(candidate->action) ||
	    candidate->mechanism_plan == RUNE_NO_MECHANISM_PLAN ||
	    candidate->mechanism_plan >= rune->artifact.num_mechanism_plans)
		return false;
	plan = &rune->mechanism_plans[candidate->mechanism_plan];
	return SG_ActionMechanismPlanAllowed(candidate->action,
	    plan->controller_kind);
}

qboolean SG_RunHasMechanismSuccessor(const rune_t *rune, int seed)
{
	int link_index;
	int walked;

	if (!rune || !rune->links || !rune->first_link || !rune->next_link ||
	    !rune->mechanism_plans || rune->hdr.num_links <= 0 || seed < 0 ||
	    seed >= rune->hdr.num_seeds)
		return false;
	link_index = rune->first_link[seed];
	for (walked = 0; link_index >= 0 && walked < rune->hdr.num_links; walked++)
	{
		const rune_link_t *candidate;

		if (link_index >= rune->hdr.num_links)
			return false;
		candidate = &rune->links[link_index];
		if (candidate->from != seed)
			return false;
		if (SG_RunMechanismPlanCandidateValid(rune, seed, link_index))
			return true;
		link_index = rune->next_link[link_index];
	}
	return false;
}

static qboolean Run_HandoffBodyValid(const rune_t *rune, int destination,
	const edict_t *ent, const gclient_t *client)
{
	int water_source;

	if (!rune || !rune->seeds || destination < 0 ||
	    destination >= rune->hdr.num_seeds || !ent || !ent->inuse ||
	    !client || ent->client != client || !client->pers.connected ||
	    ent->health <= 0 ||
	    ent->deadflag != DEAD_NO || ent->solid == SOLID_NOT ||
	    !isfinite(ent->s.origin[0]) || !isfinite(ent->s.origin[1]) ||
	    !isfinite(ent->s.origin[2]))
		return false;
	water_source = (rune->seeds[destination].flags & RSF_WATER) != 0;
	if (water_source)
		return ent->waterlevel >= 2 &&
		       !(ent->watertype & (CONTENTS_LAVA | CONTENTS_SLIME));
	return ent->groundentity != NULL && ent->waterlevel == 0;
}

/* This transaction runs before link-latch/ribbon bookkeeping. A true stale-
 * seed arrival at a declared-mechanism source receives one full zero-input
 * server frame while the incoming RUN is already retired. Only a live body
 * that remains in the source's dry/water support class may publish the RUN
 * destination for next-frame pricing. Mere goal-field overachievement is not
 * authority to publish a source the body did not reach. */
qboolean SG_RunCompletionHandoff(const rune_t *rune, int completed_link,
	sg_run_completion_t completion, sg_bot_t *bot, sg_think_t *tc,
	int *next_link)
{
	const rune_link_t *completed;
	gclient_t *client;
	int previous_seed;
	int step;
	usercmd_t coast;

	if (!rune || !rune->links || !bot || !tc || !next_link ||
	    completed_link < 0 || completed_link >= rune->hdr.num_links ||
	    completion != SG_RUN_ARRIVED)
		return false;
	completed = &rune->links[completed_link];
	if (completed->action != RL_RUN || completed->from < 0 ||
	    completed->from >= rune->hdr.num_seeds || completed->to < 0 ||
	    completed->to >= rune->hdr.num_seeds ||
	    bot->commit_link != completed_link || bot->seed == completed->to ||
	    !SG_RunHasMechanismSuccessor(rune, completed->to) ||
	    !tc->e || !(client = tc->e->client))
		return false;

	/* Every candidate was priced from the stale departure identity. Retire it
	 * and the incoming RUN before the first Pmove can execute a trigger touch. */
	*next_link = -1;
	tc->bestlink = -1;
	SG_StagedTraversalCancel(bot, RL_RUN);
	memset(&tc->cmd, 0, sizeof(tc->cmd));
	memset(&coast, 0, sizeof(coast));
	coast.msec = 25;
	for (step = 0; step < 4; step++)
		ClientThink(tc->e, &coast);

	tc->think_over = true;
	previous_seed = bot->seed;
	if (!Run_HandoffBodyValid(rune, completed->to, tc->e, client))
	{
		bot->seed = -1;
		return true;
	}
	bot->prev_seed = previous_seed;
	bot->prev_seed_time = level.time;
	bot->dither_salt = SG_RouteDitherNext(bot->dither_salt,
	    previous_seed, completed->to);
	bot->seed = completed->to;
	VectorCopy(tc->e->s.origin, bot->last_origin);
	bot->seedless_active = false;
	bot->seedless_since = 0.0f;
	bot->seedless_turn_until = 0.0f;
	if (sg_cv.debug->value)
	{
		sg_host.dprint("RUNHANDOFF %s frame=%d completed=%d from=%d to=%d "
		               "outcome=published seed=%d q8=(%d %d %d)\n",
		    client->pers.netname, level.framenum, completed_link,
		    completed->from, completed->to, bot->seed,
		    (int)client->ps.pmove.origin[0],
		    (int)client->ps.pmove.origin[1],
		    (int)client->ps.pmove.origin[2]);
	}
	return true;
}

void SG_RunRetireCompletedTransaction(const rune_t *rune,
	int completed_link, sg_run_completion_t completion, sg_bot_t *bot,
	int *next_link)
{
	const rune_link_t *completed;

	if (!rune || !rune->links || !bot || !next_link ||
	    completion == SG_RUN_INCOMPLETE || completed_link < 0 ||
	    completed_link >= rune->hdr.num_links ||
	    bot->commit_link != completed_link)
		return;
	completed = &rune->links[completed_link];
	if (completed->action != RL_RUN || completed->to < 0 ||
	    completed->to >= rune->hdr.num_seeds)
		return;
	const int candidate_from = completion == SG_RUN_ARRIVED ? completed->to : bot->seed;
	if ((completion == SG_RUN_ARRIVED && bot->seed != candidate_from) || *next_link < 0 ||
	    *next_link >= rune->hdr.num_links || *next_link == completed_link ||
	    rune->links[*next_link].from != candidate_from)
		*next_link = -1;
	SG_StagedTraversalCancel(bot, RL_RUN);
}

static void DefenseShiftReset(sg_bot_t *bot, qboolean reset_previous)
{
	if (!bot)
		return;
	bot->def_shift_seed = -1;
	bot->def_shift_link = -1;
	bot->def_shift_until = 0.0f;
	if (reset_previous)
	{
		bot->def_shift_from = -1;
		bot->def_shift_next = 0.0f;
	}
}

static qboolean DefenseLocalRunReady(const sg_bot_t *bot, int link_index,
	int from_seed, int to_seed)
{
	const rune_t *rune = SG_Rune();
	const rune_link_t *link;
	int index;

	if (!bot || !rune || !rune->links || link_index < 0 ||
	    link_index >= rune->hdr.num_links || from_seed < 0 ||
	    from_seed >= rune->hdr.num_seeds || to_seed < 0 ||
	    to_seed >= rune->hdr.num_seeds)
		return false;
	link = &rune->links[link_index];
	if (link->action != RL_RUN || link->from != from_seed ||
	    link->to != to_seed)
		return false;
	for (index = 0; index < SG_BL_MAX; index++)
		if (bot->bl_link[index] == link_index &&
		    SG_TimerPending(bot->bl_until[index]))
			return false;
	return true;
}

/* Shelf and validity changes can arrive after the shift was committed. Retire
 * that exact transaction before the post hold or movement stage observes it;
 * a different commitment remains somebody else's authority. */
static qboolean DefenseShiftRetireInvalid(sg_bot_t *bot, int *bestlink,
	qboolean *selected)
{
	int shift_link;

	if (!bot || bot->def_shift_seed < 0)
		return false;
	shift_link = bot->def_shift_link;
	if (!SG_DefenseShiftRetireIfInvalid(shift_link,
	        DefenseLocalRunReady(bot, shift_link, bot->seed,
	            bot->def_shift_seed), &bot->commit_link))
		return false;
	if (bestlink && *bestlink == shift_link)
		*bestlink = -1;
	if (selected)
		*selected = false;
	DefenseShiftReset(bot, false);
	SG_TimerArm(&bot->def_shift_next, 1.25f);
	return true;
}

/* A CARRYHOLD walks to finite visible cover on the current or one proved RUN.
 * Lowest exposure wins, with distance as the tie-break. */
static int Carrier_RallyCover(sg_bot_t *bot, edict_t *e, const int *goal_field)
{
	int cover = -1;
	float best = 1e30f;
	int seed;
	if (!bot || !e || !goal_field || !SG_Rune() || !SG_Rune()->seeds)
		return -1;
	for (seed = 0; seed < SG_Rune()->hdr.num_seeds; seed++)
	{
		vec3_t delta;
		float distance, score;

		if (!SG_CarrierCoverRouteAllowed(SG_Rune(), bot->seed, seed) ||
		    goal_field[seed] < 600 || goal_field[seed] >= 2500 ||
		    !SG_CanSee(e, SG_Rune()->seeds[seed].origin, 22.0f))
			continue;
		VectorSubtract(SG_Rune()->seeds[seed].origin, e->s.origin, delta);
		distance = VectorLength(delta);
		/* area_hint is the existing exposure measure used by rallies. */
		if (distance > 1200.0f)
			continue;
		score = (float)SG_Rune()->seeds[seed].area_hint * 10000.0f + distance;
		if (score < best)
		{
			best = score;
			cover = seed;
		}
	}
	return cover;
}

static qboolean Carrier_LinkShelved(const sg_bot_t *bot, int link)
{
	int slot;

	for (slot = 0; bot && slot < SG_BL_MAX; slot++)
		if (bot->bl_link[slot] == link && SG_TimerPending(bot->bl_until[slot]))
			return true;
	return false;
}

static qboolean Objective_VisitedRecently(const sg_bot_t *bot, int seed,
	sg_field_key_t goal)
{
	int visit;

	for (visit = 0; bot && visit < SG_VISIT_RING; visit++)
		if (bot->visit_seed[visit] == seed &&
		    SG_FieldKeyMatches(bot->visit_key[visit], goal) &&
		    SG_AgeUnder(bot->visit_time[visit], 30.0f))
			return true;
	return false;
}

/* After a detected objective orbit, prefer an unshelved, unvisited RUN that
 * genuinely descends the same finite field. A one-exit corridor may have no
 * such route; its fallback deliberately keeps the shelf as evidence while
 * allowing the only finite exit to remain mobile. */
static int Objective_CycleRoute(sg_bot_t *bot, sg_field_key_t goal,
	qboolean alternate_only)
{
	const int *goal_field = goal.field;
	int link, selected = -1;
	int selected_cost = SG_FIELD_INF;
	int finite_count = 0;
	int finite_link = -1;
	int here;

	if (!bot || !goal_field || bot->seed < 0 || !SG_Rune() ||
	    !SG_Rune()->links || !SG_Rune()->first_link || !SG_Rune()->next_link)
		return -1;
	here = goal_field[bot->seed];
	if (here >= SG_FIELD_INF)
		return -1;
	for (link = SG_Rune()->first_link[bot->seed]; link >= 0;
	     link = SG_Rune()->next_link[link])
	{
		rune_link_t *candidate = &SG_Rune()->links[link];
		int cost;

		if (candidate->action != RL_RUN || candidate->from != bot->seed ||
		    candidate->to < 0 ||
		    candidate->to >= SG_Rune()->hdr.num_seeds)
			continue;
		cost = SG_RouteCandidateGoalMs(goal_field[candidate->to],
		    Fields_LinkTraversalCostMs(candidate), SG_FIELD_INF);
		if (cost >= SG_FIELD_INF)
			continue;
		finite_count++;
		finite_link = link;
		/* Shelves and visit history are bypassable only in a genuine one-exit
		 * corridor.  With another finite RUN available, re-entering the
		 * freshly shelved cycle is worse than leaving route choice to the
		 * ordinary next frame. */
		if (!alternate_only)
			continue;
		if (alternate_only &&
		    (cost >= here || Carrier_LinkShelved(bot, link) ||
		     Objective_VisitedRecently(bot, candidate->to, goal)))
			continue;
		if (cost < selected_cost)
		{
			selected = link;
			selected_cost = cost;
		}
	}
	if (!alternate_only)
		return finite_count == 1 ? finite_link : -1;
	return selected;
}

/* Clear the route ownership associated with a strike weapon action. */
static void StrikeWeaponPurposeClear(sg_bot_t *bot)
{
	if (!bot)
		return;
		/* Retire only the latch owned by the completed strike route. */
	if (bot->strike_weapon_link >= 0 &&
	    bot->sticky_link == bot->strike_weapon_link)
	{
		bot->sticky_link = -1;
		bot->latch_until = 0.0f;
	}
	bot->strike_weapon_link = -1;
	bot->strike_weapon_until = 0.0f;
	bot->strike_weapon_draining = false;
	SG_StrikeWeaponTargetClear(bot);
}

/* A door acquires its shared-mover lease at the canonical source before the
 * irreversible trigger touch.  Retirement must positively release that lease
 * before clearing local state.  Failure enters the existing bounded paused
 * safety law and ends this bot frame; it never authorizes forward activation. */
static qboolean DoorLeaseBlocksRetirement(sg_bot_t *bot, sg_think_t *tc,
	int action)
{
	sg_compound_guard_result_t result;
	sg_door_lease_retirement_t retirement;
	qboolean expired = false;
	qboolean hold_ready = false;

	if (!bot || !tc || (action != RL_DOOR && action != RL_BUTTON_DOOR) ||
	    !bot->declared_started || bot->declared_touched ||
	    bot->declared_triggered || bot->declared_activated)
		return false;
	result = SG_DeclaredDoorGuardReleaseProvedClear(bot);
	if (result != SG_COMPOUND_GUARD_OK)
	{
		if (bot->declared_door_recovery_since == 0.0f)
			SG_Mark(&bot->declared_door_recovery_since);
		else
			expired = SG_AgeAtLeast(bot->declared_door_recovery_since,
			    5.0f);
		if (!expired)
			hold_ready = SG_DeclaredDoorGuardHoldOpen(bot, 500) ==
			    SG_COMPOUND_GUARD_OK;
	}
	retirement = SG_DoorLeaseRetirement(
	    result == SG_COMPOUND_GUARD_OK, expired, hold_ready);
	if (retirement == SG_DOOR_LEASE_RELEASE)
		return false;
	if (retirement == SG_DOOR_LEASE_TERMINAL)
	{
		SG_DeclaredDoorTerminalDeath(bot);
		tc->think_over = true;
		return true;
	}
	if (!bot->declared_guard_paused)
	{
		bot->declared_guard_paused = true;
		bot->declared_guard_pause_started = level.time;
	}
	(void)SG_DeclaredDoorGuardPause(bot);
	tc->think_over = true;
	return true;
}

static qboolean StrikeWeaponPurposeReconcile(sg_bot_t *bot, sg_think_t *tc)
{
	qboolean exact;
	qboolean authority;
	qboolean physical = false;
	int action = RL_RUN;
	sg_strike_weapon_route_verdict_t verdict;

	if (!bot || !tc)
		return false;
	exact = bot->strike_weapon_link >= 0 && SG_Rune() && SG_Rune()->links &&
	    bot->strike_weapon_link < SG_Rune()->hdr.num_links &&
	    bot->commit_link == bot->strike_weapon_link;
	authority = exact && !bot->strike_weapon_draining &&
	    tc->strike_weapon_pursuit &&
	    isfinite(tc->strike_weapon_deadline) &&
	    tc->strike_weapon_deadline == bot->strike_weapon_until &&
	    isfinite(bot->strike_weapon_until) &&
	    level.time < bot->strike_weapon_until;
	if (exact)
	{
		action = SG_Rune()->links[bot->strike_weapon_link].action;
		physical = SG_TraversalControllerPhysical(bot, action);
		if (!authority && !physical &&
		    DoorLeaseBlocksRetirement(bot, tc, action))
		{
			bot->strike_weapon_draining = true;
			return true;
		}
	}
	verdict = SG_StrikeWeaponRouteVerdict(exact, authority, physical,
	    bot->strike_weapon_draining);
	if (verdict == SG_STRIKE_WEAPON_ROUTE_OWN)
	{
		bot->strike_weapon_draining = false;
		return false;
	}
	if (verdict == SG_STRIKE_WEAPON_ROUTE_DRAIN)
	{
		bot->strike_weapon_draining = true;
		return false;
	}
	if (exact)
		SG_StagedTraversalCancel(bot, action);
	StrikeWeaponPurposeClear(bot);
	return false;
}

/* This is the purpose-ownership prelude of Think_CommitLink.  Keeping the
 * ordering in one production-called seam is important: an ended exact weapon
 * route is reconciled first, then an unrelated staged transaction is retired,
 * and only then may GO discard an ordinary RUN.  A pre-existing physical
 * controller is neither relabeled nor canceled. */
static qboolean StrikeWeaponPrepareCommit(sg_bot_t *bot, sg_think_t *tc)
{
	rune_t *rune;

	if (!bot || !tc)
		return false;
	if (StrikeWeaponPurposeReconcile(bot, tc))
		return true;
	rune = SG_Rune();
	/* GO is a new enemy-field transaction even when the prior generic commit
	 * already disappeared. Retire its orphan sticky half before link selection
	 * can replace the first rush candidate. This is only route-selection state:
	 * any live hook, jump, or declared mechanism remains in its controller fields
	 * and, when still committed, drains under the physical boundary below. */
	if (tc->strike_rush)
	{
		bot->sticky_link = -1;
		bot->latch_until = 0.0f;
	}
	/* The generic sticky latch is not a physical controller. Retire it on the
	 * first frame of a weapon purpose even when no commit_link remains; otherwise
	 * link selection can replace the fresh weapon-field candidate with an
	 * unrelated old edge.  A live hook/jump/mechanism remains owned by its exact
	 * controller fields and commit identity below. */
	if (tc->strike_weapon_pursuit && bot->strike_weapon_link < 0)
	{
		bot->sticky_link = -1;
		bot->latch_until = 0.0f;
	}

	/* Entering the five-second diversion cannot spend most of that clock on an
	 * unrelated retained route.  Cancel an unowned staged transaction before
	 * restoration, but never relabel it as weapon-owned: a controller which
	 * already crossed into physics finishes under its original bounded law. */
	if (tc->strike_weapon_pursuit && bot->strike_weapon_link < 0 && rune &&
	    rune->links && bot->commit_link >= 0 &&
	    bot->commit_link < rune->hdr.num_links)
	{
		int prior_action = rune->links[bot->commit_link].action;

		if (!SG_TraversalControllerPhysical(bot, prior_action))
		{
			if (DoorLeaseBlocksRetirement(bot, tc, prior_action))
			{
				bot->strike_weapon_draining = true;
				return true;
			}
			SG_StagedTraversalCancel(bot, prior_action);
		}
	}

	/* A same-frame strike RUSH supersedes an ordinary route transaction.  A
	 * stale RL_RUN can otherwise keep a member on the prior weapon/item
	 * detour for its three-second latch.  Proved mechanism/ballistic owners
	 * are intentionally left alone; their own bounded controller must finish. */
	if (tc->strike_rush && rune && rune->links && bot->commit_link >= 0 &&
	    bot->commit_link < rune->hdr.num_links &&
	    rune->links[bot->commit_link].action == RL_RUN &&
	    !SG_TraversalControllerPhysical(bot, RL_RUN))
		SG_StagedTraversalCancel(bot, RL_RUN);
	return false;
}

/* A pure route's exact dynamic goal owns every staged traversal.  Goal changes
 * cancel nonphysical work after durable guards release; physical controllers
 * keep bounded completion. */
static qboolean PureRouteRetirementBlocksFrame(sg_bot_t *bot, sg_think_t *tc)
{
	rune_t *rune = SG_Rune();
	sg_field_key_t current_goal;
	int action;

	if (!bot || !tc || !tc->route_pure || !tc->route_field || !rune ||
	    !rune->links || bot->commit_link < 0 ||
	    bot->commit_link >= rune->hdr.num_links)
		return false;
	current_goal = SG_FieldKey(rune, tc->route_field);
	if (SG_FieldKeyMatches(bot->commit_route_goal, current_goal))
		return false;
	action = rune->links[bot->commit_link].action;
	if (SG_TraversalControllerPhysical(bot, action))
		return false;
	if (DoorLeaseBlocksRetirement(bot, tc, action))
	{
		bot->commit_retirement_pending = true;
		return true;
	}
	SG_StagedTraversalCancel(bot, action);
	return false;
}

static qboolean FlagTouchTerminalRetainsCommit(sg_bot_t *bot, sg_think_t *tc,
	qboolean touch_authorized)
{
	rune_t *rune = SG_Rune();
	int action;

	if (!bot || !tc || !touch_authorized)
		return false;
	if (!rune || !rune->links || bot->commit_link < 0 ||
	    bot->commit_link >= rune->hdr.num_links)
	{
		SG_StagedTraversalCancel(bot, RL_RUN);
		return false;
	}
	action = rune->links[bot->commit_link].action;
	if (SG_TraversalControllerPhysical(bot, action))
		return true;
	if (DoorLeaseBlocksRetirement(bot, tc, action))
	{
		bot->commit_retirement_pending = true;
		return true;
	}
	SG_StagedTraversalCancel(bot, action);
	return false;
}

/* Filter only a fresh weapon-owned selection.  Other candidate policy remains
 * in Think_CommitLink; this seam merely enforces that a weapon diversion may
 * acquire purpose identity only on a strict live-field descent. */
static int StrikeWeaponFilterFreshCandidate(const sg_bot_t *bot,
	const sg_think_t *tc, int bestlink)
{
	rune_t *rune = SG_Rune();
	const int *route_field;

	if (!bot || !tc || !tc->strike_weapon_pursuit || !tc->route_pure ||
	    bot->commit_link >= 0 || bestlink < 0)
		return bestlink;
	route_field = tc->route_field;
	if (!rune || !rune->links || bot->seed < 0 ||
	    bot->seed >= rune->hdr.num_seeds || !route_field ||
	    bestlink >= rune->hdr.num_links ||
	    route_field[bot->seed] >= SG_FIELD_INF ||
	    rune->links[bestlink].to < 0 ||
	    rune->links[bestlink].to >= rune->hdr.num_seeds ||
	    !SG_RouteCandidateDescends(route_field[bot->seed],
	        route_field[rune->links[bestlink].to],
	        Fields_LinkTraversalCostMs(&rune->links[bestlink]), SG_FIELD_INF))
		return -1;
	return bestlink;
}

/* The actual fresh-link transaction constructor used by Think_CommitLink.
 * Weapon purpose is attached here--after generic retained commitments were
 * handled--so no old controller can be retroactively labeled as a weapon
 * errand. */
static void StrikeCommitFreshLink(sg_bot_t *bot, const sg_think_t *tc,
	int bestlink)
{
	rune_t *rune = SG_Rune();
	rune_link_t *new_link;
	float hold = 3.0f;

	if (!bot || !tc || !rune || !rune->links || bot->commit_link >= 0 ||
	    bestlink < 0 || bestlink >= rune->hdr.num_links)
		return;
	new_link = &rune->links[bestlink];
	bot->commit_link = bestlink;
	bot->commit_route_goal = SG_FieldKey(rune, tc->route_field);
	bot->commit_retirement_pending = false;
	if (tc->strike_weapon_pursuit &&
	    isfinite(tc->strike_weapon_deadline) &&
	    tc->strike_weapon_deadline > level.time)
	{
		bot->strike_weapon_link = bestlink;
		bot->strike_weapon_until = tc->strike_weapon_deadline;
		bot->strike_weapon_draining = false;
	}
	bot->jump_link = (new_link->action == RL_JUMP) ? bestlink : -1;
	bot->jump_started = false;
	SG_DropLiveReset(&bot->drop_replay, &bot->drop_replay_active,
	    &bot->drop_replay_link, &bot->drop_live_events);
	SG_SwimLiveReset(&bot->swim_replay, &bot->swim_replay_active,
	    &bot->swim_replay_link, &bot->swim_validated,
	    &bot->swim_proved_ms, &bot->swim_elapsed_ms);
	bot->declared_activated = false;
	bot->declared_started = false;
	bot->declared_start_frame = -1;
	bot->declared_touched = false;
	bot->declared_touch_frame = -1;
	SG_ButtonExecutionActionReset(bot);
	bot->declared_triggered = false;
	bot->declared_trigger_frame = -1;
	bot->declared_egress_proof_frame = -1;
	bot->declared_door_retreat = false;
	bot->declared_door_suffix_ms = 0;
	bot->declared_guard_paused = false;
	bot->declared_guard_pause_started = 0.0f;
	bot->declared_door_recovery_since = 0.0f;
	/* Before a proved ballistic action starts, this is a bounded source-staging
	 * deadline. Six seconds lets a fast body brake and center; the actual
	 * JUMP/DROP rollout receives its own deadline only when it starts. */
	if (new_link->action == RL_DROP || new_link->action == RL_JUMP)
		hold = 6.0f;
	else if (new_link->action == RL_LIFT)
		/* Worst legitimate queue: approach, wait the 3 s top park, ride down
		 * empty, then ride back up. The record prices one ride. */
		hold = 8.0f + 2.0f * new_link->cost_ms * 0.001f;
	else if (new_link->action == RL_DOOR ||
	         new_link->action == RL_BUTTON_DOOR)
		/* The record starts at an exact rest source. Live selection may be one
		 * seed radius away, so source capture gets a bounded six seconds before
		 * the serialized cooldown/motion/egress budget. */
		hold = 6.5f + new_link->cost_ms * 0.001f;
	else if (new_link->cost_ms > 0 &&
	         new_link->cost_ms * 0.001f + 0.5f > hold)
		hold = new_link->cost_ms * 0.001f + 0.5f;
	SG_TimerArm(&bot->commit_until, hold);
	/* A defense shift is intentionally one short visible adjustment, not a
	 * general three-second route commitment. */
	if (new_link->action == RL_RUN && bestlink == bot->def_shift_link &&
	    new_link->to == bot->def_shift_seed &&
	    SG_TimerPending(bot->def_shift_until) &&
	    bot->commit_until > bot->def_shift_until)
		bot->commit_until = bot->def_shift_until;
}

static qboolean StrikeRailLateOverrideAllowed(const sg_bot_t *bot,
	const sg_think_t *tc)
{
	return bot && tc && SG_StrikeGenericRailAllowed(tc->strike_active) &&
	    SG_DefenseSupplyGenericRetryAllowed(
	        (sg_defense_supply_phase_t)bot->def_supply_phase,
	        bot->def_supply_armed);
}

static qboolean StrikeRailWatchdogAllowed(const sg_bot_t *bot,
	const sg_think_t *tc)
{
	return bot && tc && SG_StrikeGenericRailAllowed(tc->strike_active) &&
	    SG_DefenseSupplyGenericRetryAllowed(
	        (sg_defense_supply_phase_t)bot->def_supply_phase,
	        bot->def_supply_armed);
}

int Think_CommitLink(sg_bot_t *bot, sg_think_t *tc)
{
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
	const int *route_field = tc->route_field;
	int rail_seed = tc->rail_seed;
	int rail_client = tc->rail_client;
	int bestlink_in = tc->bestlink;
	int bestlink = bestlink_in;
	qboolean rally_hold = tc->rally_hold;
	qboolean rail_hold = tc->rail_hold;
	qboolean hold_post = false;
	qboolean defense_quiet = true;
	qboolean defense_post = false;
	qboolean defense_shift_selected = false;
	qboolean defense_patrol_selected = false;
	qboolean enemy_pressure = tc->strike_pressure;
	int defense_threat_seed = -1;
	float post_yaw = tc->post_yaw;
	float post_sight = tc->post_sight;
	vec3_t d;

	tc->patrol_walk = false;

	/* Reconcile exact purpose before generic restoration can put it back. */
	if (StrikeWeaponPrepareCommit(bot, tc))
		return bot->commit_link;
	if (PureRouteRetirementBlocksFrame(bot, tc))
		return bot->commit_link;

	/* The supply transaction is a bounded navigation owner, not a second
	 * objective.  Irrecoverable identity/life/role edges cancel it; edges that
	 * merely make leaving unsafe enter the route-pure RETURN phase. */
	if (SG_DefenseSupplyActive(bot))
	{
		qboolean invalid_owner = role != SG_ROLE_DEFEND || !bot->def_stand ||
		    tc->carrying || !e->inuse || !e->client ||
		    e->deadflag == DEAD_DEAD || e->health <= 0 ||
		    bot->def_supply_instance != bot->instance_token ||
		    bot->def_supply_phase == SG_DEF_SUPPLY_NONE;

		if (invalid_owner)
			SG_DefenseSupplyCancel(bot, true);
		else if (bot->def_supply_phase == SG_DEF_SUPPLY_OUTBOUND &&
		         (!SG_DefenseSupplyHome(team) || SG_DefenseSupplyThreat(team) ||
		          duel || bot->engaged_last || bot->lead_ent > 0 ||
		          bot->patrol_seed >= 0 || bot->def_shift_seed >= 0 ||
		          bot->tac_seed >= 0 || !SG_TimerPending(bot->def_supply_until)))
		{
			SG_DefenseSupplyBeginReturn(bot);
			/* bestlink was selected before this late edge; it is stale even
			 * when the ordinary RUN commitment had no live ticket. */
			bestlink = -1;
		}
	}

	/* Retire a completed RUN before latch and ribbon record stale ownership.
	 * A stale-seed mechanism arrival consumes one zero-input handoff frame. */
	if (bot->commit_link >= 0 && bot->commit_link < SG_Rune()->hdr.num_links)
	{
		rune_link_t *incoming = &SG_Rune()->links[bot->commit_link];

		if (incoming->action == RL_RUN)
		{
			sg_run_completion_t completion = SG_RunCommitCompletion(
			    SG_Rune(), incoming, bot->seed, e->s.origin, goal_field);
			int completed_link = bot->commit_link;

			if (completion == SG_RUN_ARRIVED && bot->seed != incoming->to &&
			    SG_RunCompletionHandoff(SG_Rune(), completed_link,
			        completion, bot, tc, &bestlink))
				return -1;
			SG_RunRetireCompletedTransaction(SG_Rune(), completed_link,
			    completion, bot, &bestlink);
			tc->bestlink = bestlink;
		}
	}

	/*
	 * Threat-responsive post movement must enter before the link latch and
	 * commitment transaction. A late bestlink override can move the body for
	 * one frame while commit_link still names a different route; that is not
	 * navigation authority. Select one guarded RUN here so the ordinary latch,
	 * timeout, shelf and arrival machinery owns exactly the step we drive.
	 */
	{
		qboolean defense_hold_eligible =
		    role == SG_ROLE_DEFEND && bot->def_stand && bot->seed >= 0 &&
		    bot->seed < SG_Rune()->hdr.num_seeds &&
		    ((float)goal_field[bot->seed] <
		         400.0f * SG_PersonaCampScale(e) ||
		     bot->patrol_seed >= 0);
		qboolean shift_allowed;
		float threat_time = -1000.0f;
		int index;

		defense_post = defense_hold_eligible &&
		    (float)goal_field[bot->seed] <
		        400.0f * SG_PersonaCampScale(e);
		if (defense_hold_eligible)
		{
			for (index = 0; index < SG_MAX_ENEMY_TRACK; index++)
			{
				sg_belief_enemy_t *enemy =
				    &sg_caco_enemies[SG_TeamIdx(team)][index];

				if (enemy->client < 0 || enemy->seed < 0 ||
				    enemy->seed >= SG_Rune()->hdr.num_seeds ||
				    !SG_AgeUnder(enemy->seen_time, 6.0f) ||
				    goal_field[enemy->seed] >= 2500)
					continue;
				defense_quiet = false;
				if (enemy->seen_time > threat_time)
				{
					threat_time = enemy->seen_time;
					defense_threat_seed = enemy->seed;
				}
			}
		}
		{
			edict_t *own_flag = SG_OwnFlag(team);
			int patrol_link = bot->patrol_link;
			sg_defense_patrol_request_t patrol_request = {
				.holds_post = role == SG_ROLE_DEFEND && bot->def_stand,
				.own_flag_home = own_flag && ctf_flagathome(own_flag),
				.quiet = defense_quiet,
				.busy = duel || bot->engaged_last || tc->strike_active ||
				    tc->strike_weapon_pursuit || SG_DefenseSupplyActive(bot) ||
				    (bot->lead_ent > 0 && bot->lead_state == SG_LEAD_WAITING),
				.armor_need = w->item[SG_FC_ARMOR],
				.health_need = w->item[SG_FC_HEALTH],
				.ammo_need = w->item[SG_FC_AMMO],
				.configured = sg_cv.patrol->value
			};
			qboolean patrol_allowed = SG_DefensePatrolAllowed(&patrol_request);

			/* Admission loss retires the patrol before the generic latch. */
			if (SG_DefensePatrolRetire(bot, patrol_allowed))
			{
				if (bestlink == patrol_link)
					bestlink = -1;
				SG_TimerArm(&bot->patrol_until, 5.0f);
				tc->bestlink = bestlink;
			}

			/* Select before commitment so the route transaction owns the patrol. */
			if (patrol_allowed &&
			    SG_DefensePatrolFinishLeg(bot->seed, &bot->patrol_seed))
			{
				bot->patrol_link = -1;
				bot->patrol_random =
				    SG_DefensePatrolRandomNext(bot->patrol_random);
				SG_TimerArm(&bot->patrol_until,
				    SG_DefensePatrolDwell(bot->patrol_random));
			}
			else if (patrol_allowed && bot->patrol_seed >= 0)
			{
				qboolean owned = bot->patrol_link >= 0 &&
				    bot->patrol_link == bot->commit_link;

				if (owned && DefenseLocalRunReady(bot,
				        bot->patrol_link, bot->seed, bot->patrol_seed))
					bestlink = bot->patrol_link;
				else
				{
					int stale_link = bot->patrol_link;

					(void)SG_DefensePatrolRetire(bot, false);
					if (bestlink == stale_link)
						bestlink = -1;
					SG_TimerArm(&bot->patrol_until, 5.0f);
				}
			}
			else if (patrol_allowed && defense_post &&
			         bot->commit_link < 0 &&
			         SG_TimerReady(bot->patrol_until))
			{
				int link_index, chosen_seed = -1, chosen_link;
				sg_defense_patrol_candidate_t candidates[64];
				size_t candidate_count = 0;

				for (link_index = SG_Rune()->first_link[bot->seed];
				     link_index >= 0 &&
				     candidate_count < sizeof(candidates) /
				         sizeof(candidates[0]);
				     link_index = SG_Rune()->next_link[link_index])
				{
					const rune_link_t *link =
					    &SG_Rune()->links[link_index];

					if (link->action != RL_RUN || link->to < 0 ||
					    link->to >= SG_Rune()->hdr.num_seeds ||
					    !DefenseLocalRunReady(bot, link_index,
					        bot->seed, link->to))
						continue;
					candidates[candidate_count].link_index = link_index;
					candidates[candidate_count].seed_index = link->to;
					candidates[candidate_count].goal_ms =
					    SG_RouteCandidateGoalMs(goal_field[link->to],
					        Fields_LinkTraversalCostMs(link),
					        SG_FIELD_INF);
					candidates[candidate_count].is_run = true;
					VectorSubtract(SG_Rune()->seeds[link->to].origin,
					    SG_Rune()->seeds[bot->seed].origin, d);
					candidates[candidate_count].distance =
					    VectorLength(d);
					candidate_count++;
				}
				bot->patrol_random =
				    SG_DefensePatrolRandomNext(bot->patrol_random);
				chosen_link = SG_DefensePatrolChoose(candidates,
				    candidate_count,
				    (int)(1000.0f * SG_PersonaCampScale(e)),
				    bot->prev_seed, bot->patrol_random, &chosen_seed);
				if (chosen_link >= 0)
				{
					bot->patrol_seed = chosen_seed;
					bot->patrol_link = chosen_link;
					bestlink = chosen_link;
					defense_patrol_selected = true;
				}
				else
					SG_TimerArm(&bot->patrol_until, 5.0f);
			}
		}
		shift_allowed = isfinite(sg_cv.defshift->value) &&
		    sg_cv.defshift->value > 0.0f && defense_post &&
		    !SG_DefenseSupplyActive(bot) &&
		    bot->lead_ent <= 0 && !defense_quiet &&
		    defense_threat_seed >= 0 && !duel && !bot->engaged_last &&
		    bot->rail_stage == 0 &&
		    sg_caco_team_belief.flag[SG_TeamIdx(team)]
		        [SG_TeamIdx(team)].state == SG_FLAG_HOME;

		if (!shift_allowed)
		{
			if (bot->def_shift_seed >= 0 &&
			    bot->commit_link == bot->def_shift_link)
			{
				bot->commit_until = level.time - 1.0f;
				bestlink = -1;
			}
			DefenseShiftReset(bot,
			    !isfinite(sg_cv.defshift->value) ||
			        sg_cv.defshift->value <= 0.0f);
		}
		else
		{
			if (bot->def_shift_seed >= 0)
			{
				qboolean finished = bot->seed == bot->def_shift_seed ||
				    level.time >= bot->def_shift_until;
				qboolean owned = bot->commit_link == bot->def_shift_link &&
				    DefenseLocalRunReady(bot, bot->def_shift_link,
				        bot->seed, bot->def_shift_seed);

				if (finished || !owned)
				{
					if (bot->commit_link == bot->def_shift_link)
						bot->commit_until = level.time - 1.0f;
					bestlink = -1;
					DefenseShiftReset(bot, false);
					SG_TimerArm(&bot->def_shift_next, 1.25f);
				}
				else
				{
					bestlink = bot->def_shift_link;
					defense_shift_selected = true;
				}
			}

			if (bot->def_shift_seed < 0 && bot->commit_link < 0 &&
			    SG_TimerReady(bot->def_shift_next))
			{
				sg_defense_shift_candidate_t candidates[32];
				sg_defense_shift_request_t request;
				vec3_t threat_delta;
				int candidate_count = 0;
				int chosen_seed = -1;
				int link_index;

				VectorSubtract(
				    SG_Rune()->seeds[defense_threat_seed].origin,
				    e->s.origin, threat_delta);
				for (link_index = SG_Rune()->first_link[bot->seed];
				     link_index >= 0 &&
				     candidate_count < (int)(sizeof(candidates) /
				         sizeof(candidates[0]));
				     link_index = SG_Rune()->next_link[link_index])
				{
					const rune_link_t *link =
					    &SG_Rune()->links[link_index];
					vec3_t delta;
					if (link->action != RL_RUN || link->to < 0 ||
					    link->to >= SG_Rune()->hdr.num_seeds ||
					    !DefenseLocalRunReady(bot, link_index,
					        bot->seed, link->to))
						continue;
					VectorSubtract(SG_Rune()->seeds[link->to].origin,
					    e->s.origin, delta);
					candidates[candidate_count].link_index = link_index;
					candidates[candidate_count].seed_index = link->to;
					candidates[candidate_count].goal_ms =
					    SG_RouteCandidateGoalMs(goal_field[link->to],
					        Fields_LinkTraversalCostMs(link), SG_FIELD_INF);
					candidates[candidate_count].delta_x = delta[0];
					candidates[candidate_count].delta_y = delta[1];
					candidates[candidate_count].delta_z = delta[2];
					candidate_count++;
				}
				request.threat_x = threat_delta[0];
				request.threat_y = threat_delta[1];
				request.max_distance = 144.0f;
				request.max_goal_ms = (int)(400.0f *
				    SG_PersonaCampScale(e));
				request.previous_seed = bot->def_shift_from;
				link_index = SG_DefenseShiftChoose(&request,
				    candidates, (size_t)candidate_count, &chosen_seed);
				if (link_index >= 0 && chosen_seed >= 0)
				{
					bot->def_shift_from = bot->seed;
					bot->def_shift_seed = chosen_seed;
					bot->def_shift_link = link_index;
					bot->def_shift_until = level.time + 1.0f;
					bestlink = link_index;
					defense_shift_selected = true;
					if (sg_cv.debug->value)
						sg_host.dprint("DEFSHIFT %s from=%d to=%d "
						    "link=%d threat=%d\n",
						    e->client->pers.netname, bot->seed,
						    chosen_seed, link_index,
						    defense_threat_seed);
				}
				else
					SG_TimerArm(&bot->def_shift_next, 1.25f);
			}
		}
	}

	/* Hold the incumbent link across near-ties until the latch expires. A
	 * missing or materially worse incumbent yields immediately. */
	if (!defense_shift_selected && !defense_patrol_selected &&
	    sg_cv.linklatch->value > 0 &&
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
		/* Sample one persistent lane offset per link and retain the closed
		 * link in the exit-asymmetry ring. */
		if (bot->ribbon_link >= 0)
		{
			bot->inlinks[bot->inlinks_n % 16] = bot->ribbon_link;
			bot->inlinks_n++;
		}
		bot->ribbon_link = bestlink;
		bot->ribbon_random = SG_RibbonRandomNext(bot->ribbon_random);
		bot->ribbon_off = SG_RibbonRandomOffset(bot->ribbon_random,
		    sg_cv.ribbon->value);
		bot->ribbon_goal = bot->ribbon_off;
	}
	/* Drift slowly within the trace-clamped lane band. */
	if (SG_TimerReady(bot->ribbon_next))
	{
		bot->ribbon_random = SG_RibbonRandomNext(bot->ribbon_random);
		bot->ribbon_goal = SG_RibbonRandomOffset(bot->ribbon_random,
		    sg_cv.ribbon->value);
		bot->ribbon_random = SG_RibbonRandomNext(bot->ribbon_random);
		SG_TimerArm(&bot->ribbon_next,
		    SG_RibbonRandomInterval(bot->ribbon_random));
	}
	bot->ribbon_off += 0.20f * (bot->ribbon_goal - bot->ribbon_off);
	bot->sticky_link = bestlink;

	/* Terminal movement requires direct-touch authority; field cost is insufficient. */
	{
		qboolean attack_touch = false;
		qboolean capture_touch = false;

		if (tc->strike_pressure &&
		    SG_AttackFlagDirectTouchAuthority(e, team, NULL))
			attack_touch = true;
		if (role == SG_ROLE_CARRY &&
		    SG_OwnHomeFlagDirectTouchAuthority(e, team, NULL))
			capture_touch = true;
		if (FlagTouchTerminalRetainsCommit(bot, tc,
		    attack_touch || capture_touch))
			return bot->commit_link;

		if (attack_touch || capture_touch)
		{
		bestlink = -1;
		bot->terminal = true;

		/* Fight a live nearby defender before touching the flag, subject to
		 * the patience and coordinated-breach rules below. */
		if (tc->strike_pressure)
		{
			int s3, room = 0;
			edict_t *live_enemy = SG_CombatLiveEnemy(e);
			qboolean live_room_enemy = live_enemy &&
			    SG_DistXY(live_enemy->s.origin, e->s.origin) < 900.0f &&
			    SG_CanSee(e, live_enemy->s.origin, live_enemy->viewheight);
			qboolean live_flag_terminal = attack_touch;

			for (s3 = 0; s3 < SG_MAX_ENEMY_TRACK; s3++)
			{
				sg_belief_enemy_t *en3 = &sg_caco_enemies[SG_TeamIdx(team)][s3];
				vec3_t dd3;

				/* Strict mode does not treat a four-second loss of sight as a
				 * cleared room. */
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

			/* In strict mode, a live enemy missing from fresh public belief is
			 * conservatively counted as one possible room defender. */
			if (sg_cv.strictgrab->value)
			{
				int s8, esz = 0, accounted = 0, i8;
				int enemy_team = SG_EnemyTeam(team);

				for (i8 = 0; i8 < game.maxclients; i8++)
				{
					edict_t *pe = g_edicts + 1 + i8;

					if (SG_StrikeLiveEnemyRosterMember(
					    pe->inuse != false, pe->client != NULL,
					    pe->client && pe->client->ctf.teamnum == enemy_team,
					    pe->deadflag == DEAD_DEAD, pe->health))
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
			/* Any live visible room defender can authorize a threshold fight. */
			if (room >= 1 && live_room_enemy && !live_flag_terminal)
			{
				/* With two attackers present, one holds the defender while the
				 * coordinator assigns the other to breach. */
				int bi5, mate_holding = 0;

				for (bi5 = 0; bi5 < SG_MAXBOTS; bi5++)
				{
					sg_bot_t *mb5 = &sg_bots[bi5];
					int mate_goal;

					if (mb5 == bot || !mb5->ent ||
					    !SG_CoordinationBodyLive(mb5->active, mb5->ent->inuse,
					        mb5->ent->deadflag, mb5->ent->health))
						continue;
					if (mb5->ent->client->ctf.teamnum != team)
						continue;
					mate_goal = SG_StrikeEnemyPressureGoalSnapshot(mb5);
					if (SG_StrikeEnemyPressureSnapshot(mb5) &&
					    mate_goal >= 0 && mate_goal < 1200 &&
					    SG_CombatWouldEngage(mb5->ent) &&
					    SG_StrikeThresholdMateOwnsHold(
					        SG_StrikeDutySnapshot(bot),
					        (int)(e - g_edicts),
					        SG_StrikeDutySnapshot(mb5),
					        (int)(mb5->ent - g_edicts)))
						mate_holding = 1;
				}
				if (!mate_holding)
				{
					if (bot->rally_since <= 0.0f)
						SG_Mark(&bot->rally_since);
					if (SG_AgeUnder(bot->rally_since, 10.0f))
						rally_hold = true;
				}

				/* Strict mode holds against any believed room defender for up to
				 * twenty seconds, regardless of a teammate's breach duty. */
				if (room >= 1 &&
				    sg_cv.strictgrab->value)
				{
					/* A room above the configured crowd limit re-arms patience
					 * instead of forcing a grab. */
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

				/* While the threshold fight holds movement, cook a grenade for
				 * the visible defender without stealing action-controller authority. */
				if (rally_hold && live_room_enemy && !live_flag_terminal &&
				    !bot->jump_started && !bot->drop_started &&
				    bot->hook_phase == 0 &&
				    !SG_RocketJumpLiveOwns(&bot->rocketjump) &&
				    bot->nade_phase == 0 &&
				    !(bestlink >= 0 &&
				      SG_ActionOwnsControl(
				          SG_Rune()->links[bestlink].action)) &&
				    !(bot->commit_link >= 0 &&
				      bot->commit_link < SG_Rune()->hdr.num_links &&
				      SG_ActionOwnsControl(
				          SG_Rune()->links[bot->commit_link].action)) &&
				    SG_TimerReady(bot->nade_next))
				{
					/* Both entry paths use the same live target and practical
					 * throw envelope. Direct flag touch always wins. */
					(void)SG_NadeArmPrebreachLiveEnemy(bot, e, team);
				}
			}
		}
	}
	}

	/* Retain a chosen link until it finishes, times out, or is shelved. */
	if (bot->commit_link >= 0 && bot->commit_link < SG_Rune()->hdr.num_links)
	{
		rune_link_t *cl = &SG_Rune()->links[bot->commit_link];
		sg_run_completion_t run_completion = SG_RUN_INCOMPLETE;
		qboolean drop_commit = false;
		qboolean ballistic_failed = false;
		qboolean proved_ballistic =
		    (cl->action == RL_JUMP || cl->action == RL_DROP);
		qboolean proved_swim = (cl->action == RL_SWIM);
			qboolean declared = (cl->action == RL_LIFT ||
			                     cl->action == RL_TELEPORT ||
			                     cl->action == RL_DOOR ||
			                     cl->action == RL_BUTTON_DOOR);
			sg_rune_mechanism_binding_t mechanism_binding = { 0 };
		qboolean swim_failed = false;
		qboolean staging_timed_out = false;
		qboolean drop_boundary_owned = false;
		qboolean drop_boundary_failed = false;
		qboolean drop_boundary_recovery_lost = false;
		sg_replay_reason_t drop_boundary_reason = SG_REPLAY_REASON_NONE;
		qboolean ballistic = (!e->groundentity && e->waterlevel < 2 &&
		    (cl->action == RL_JUMP || cl->action == RL_DROP ||
		     (cl->action == RL_ROCKETJUMP &&
		      bot->rocketjump.phase == SG_ROCKETJUMP_FLIGHT)));
			int b;

			/* A mechanism commitment never degrades into anchor-based movement.
			 * Retain the finite commitment while authority is unavailable; the
			 * execution layer will retire or safely retain its owned mover claim. */
			if (declared && !(bot->declared_started
			        ? SG_RuneMechanismBindingCaptureOwned(SG_Rune(),
			              (uint32_t)bot->commit_link, &mechanism_binding)
			        : SG_RuneMechanismBindingCapture(SG_Rune(),
			              (uint32_t)bot->commit_link, &mechanism_binding)))
				return bot->commit_link;

		/* The fourth literal command is completed here, after the intervening
		 * entity/pusher pass. Resolve every production boundary: exact cost and
		 * reducer caps are authoritative. */
		if (cl->action == RL_DOOR_DROP &&
		    bot->compound_drop_live.guard_owned &&
		    bot->compound_drop_live.command_pending)
		{
			sg_compound_drop_live_host_t host;
			sg_compound_drop_live_result_t result;
			sg_replay_pose_t pose;
			sg_replay_observation_t observation;

			if (!SG_CompoundDropGameHost(bot, &host) ||
			    !SG_CompoundDropGamePose(e, &pose) ||
			    !SG_CompoundDropGameObservation(bot, e, &observation))
			{
				SG_DeclaredDoorTerminalDeath(bot);
				return -1;
			}
			result = SG_CompoundDropLiveBoundary(
			    &bot->compound_drop_live, &host, &pose, &observation);
			SG_CompoundDropGameDebugResult(bot, "boundary", &result, &pose);
			if (result.outcome == SG_COMPOUND_DROP_LIVE_COMPLETE ||
			    result.outcome == SG_COMPOUND_DROP_LIVE_SAFE_STOPPED)
			{
				bot->commit_link = -1;
				return -1;
			}
			if (result.outcome == SG_COMPOUND_DROP_LIVE_RECOVERING)
			{
				result = SG_CompoundDropLiveRecover(
				    &bot->compound_drop_live, &host, &pose,
				    e->client->oldvelocity[2]);
				SG_CompoundDropGameDebugResult(bot, "recover", &result,
				    &pose);
				if (result.outcome == SG_COMPOUND_DROP_LIVE_SAFE_STOPPED)
				{
					bot->commit_link = -1;
					return -1;
				}
				if (result.outcome != SG_COMPOUND_DROP_LIVE_RECOVERING ||
				    bot->compound_drop_live.replay_kind ==
				        SG_COMPOUND_DROP_LIVE_REPLAY_NONE)
				{
					SG_DeclaredDoorTerminalDeath(bot);
					return -1;
				}
			}
			else if (result.outcome != SG_COMPOUND_DROP_LIVE_RUNNING)
			{
				SG_DeclaredDoorTerminalDeath(bot);
				return -1;
			}
		}
		if (cl->action == RL_DROP && bot->drop_started &&
		    bot->drop_replay_active)
		{
			drop_live_contact_context_t contact;
			sg_drop_live_events_t live_events;
			sg_replay_pose_t live_pose;
			sg_drop_live_result_t live_result;

			contact.ent = e;
			contact.link = cl;
			if (SG_OracleReplayDoorPassage(bot->drop_live_step_origin,
			        e->s.origin))
				(void)SG_DropLiveEventsLatch(&bot->drop_live_events,
				    false, true);
			live_events = bot->drop_live_events;
			memset(&bot->drop_live_events, 0,
			       sizeof(bot->drop_live_events));
			Drop_LivePose(e, &live_pose);
			live_result = SG_DropLiveBoundary(&bot->drop_replay,
			    &bot->drop_replay_active, &bot->drop_replay_link,
			    bot->commit_link, &live_pose, Drop_LiveSupportValid(e),
			    &live_events,
			    Drop_LiveArrival, Drop_LiveRecovery, &contact);
			Drop_LiveSync(bot);
			Drop_LiveBoundaryLog(e, bot->commit_link, &live_result);
			if (live_result.outcome == SG_DROP_LIVE_RUNNING)
				drop_boundary_owned = true;
			else if (live_result.outcome == SG_DROP_LIVE_ARRIVED)
			{
				drop_boundary_owned = true;
				drop_commit = true;
				bestlink = -1;
			}
			else if (live_result.outcome == SG_DROP_LIVE_FAILED)
			{
				drop_boundary_owned = true;
				drop_boundary_failed = true;
				drop_boundary_reason = live_result.replay_reason;
				drop_boundary_recovery_lost =
				    live_result.replay_reason == SG_REPLAY_REASON_RECOVERY_LOST;
				drop_commit = true;
				ballistic_failed = true;
				bestlink = -1;
			}
			else
			{
				/* Every adapter fallback retires the owned DROP. Re-entering legacy
				 * would either replay an ordered trace or execute a controller whose
				 * identity/cadence already failed. It is integration evidence, so it
				 * neither shelves nor teaches the serialized link. */
				drop_boundary_owned = true;
				drop_boundary_failed = true;
				drop_commit = true;
				bestlink = -1;
			}
		}

		if (!ballistic)
		{
			VectorSubtract(SG_Rune()->seeds[cl->to].origin, e->s.origin, d);
			if (proved_ballistic)
			{
				/* A live DROP boundary owns all terminal decisions while its
				 * reducer is running. Keep the entire proved-ballistic family out
				 * of the ordinary-action fallthrough; JUMP and an unowned DROP
				 * retain this legacy terminal path verbatim. */
				if (!(cl->action == RL_DROP && drop_boundary_owned))
				{
					qboolean action_started =
				    (cl->action == RL_JUMP && bot->jump_started) ||
				    (cl->action == RL_DROP && bot->drop_started &&
				     bot->drop_walkoff);

				if (action_started && Ballistic_Arrived(e, cl))
				{
					drop_commit = true;
					/* TrackSeed deliberately preserves the departure identity until
					 * this boundary.  The candidate selected from that old seed cannot
					 * be re-armed at the landing; localize and price afresh next frame. */
					bestlink = -1;
				}
				else if (action_started && cl->action == RL_DROP &&
				         bot->drop_recover)
				{
					/* Recovery is a single dry, supported walk. Validate its
					 * bounded clear envelope at every production boundary; losing
					 * it is a second unproved action, not another chance to fall. */
					if (!Drop_RecoveryReady(e, cl))
					{
						drop_commit = true;
						ballistic_failed = true;
					}
				}
				else if (action_started && cl->action == RL_DROP &&
				         bot->drop_airborne && Drop_RecoveryReady(e, cl))
				{
					/* The first aligned dry impact is health-priced by the same
					 * conservative source-to-destination bound used at selection
					 * and launch. From here the exact controller steers to `to`. */
					bot->drop_recover = true;
				}
				else if (action_started &&
				         (e->groundentity || e->waterlevel >= 2))
				{
					/* A proved ballistic owns one flight and one exact terminal
					 * contact. A short JUMP landing, intermediate DROP ledge, or
					 * shallow splash is not permission to splice on a second move. */
					drop_commit = true;
					ballistic_failed = true;
				}
				}
			}
			else if (proved_swim)
			{
				qboolean run_legacy_terminal = bot->swim_validated;
				qboolean legacy_arrived = false;
				qboolean arrival_sampled = false;

				/* The fourth 25 ms command remains pending across the entity/pusher
				 * pass.  This is the one production boundary where the legacy arrival
				 * predicate is sampled; reducer fallback reuses that result. */
				if (bot->swim_validated && bot->swim_replay_active)
				{
					sg_replay_pose_t live_pose;
					sg_swim_live_result_t live_result;

					Swim_LivePose(e, &live_pose);
					live_result = SG_SwimLiveBoundary(&bot->swim_replay,
					    &bot->swim_replay_active, &bot->swim_replay_link,
					    bot->commit_link, bot->swim_elapsed_ms, &live_pose,
					    Swim_LiveArrival, e);
					Swim_LiveFallbackLog(e, bot->commit_link, &live_result);
					arrival_sampled = live_result.arrival_sampled;
					legacy_arrived = live_result.legacy_arrived;
					if (live_result.outcome == SG_SWIM_LIVE_ARRIVED)
					{
						drop_commit = true;
						run_legacy_terminal = false;
					}
					else if (live_result.outcome == SG_SWIM_LIVE_RUNNING)
						run_legacy_terminal = false;
				}
				if (run_legacy_terminal)
				{
					/* A reducer that fell back before sampling must still execute the
					 * one legacy call this site owned.  A boundary fallback never traces
					 * twice: its cached result is consumed here. */
					if (!arrival_sampled)
						legacy_arrived = SG_SwimArrived(e->s.origin,
						    SG_Rune()->seeds[cl->to].origin,
						    (SG_Rune()->seeds[cl->to].flags & RSF_WATER) != 0,
						    e->groundentity != NULL, e->watertype,
						    e->waterlevel, e);
					if (legacy_arrived)
					{
						drop_commit = true;
						if (bot->swim_elapsed_ms != bot->swim_proved_ms)
							swim_failed = true;
					}
					else if (bot->swim_elapsed_ms >= bot->swim_proved_ms)
					{
						drop_commit = true;
						swim_failed = true;
					}
				}
			}
			else if (declared)
			{
				qboolean declared_arrived = false;

					if (bot->declared_started && cl->action == RL_TELEPORT)
					{
						vec3_t tele_dest, tele_delta;
						edict_t *destination =
						    SG_RuneMechanismBindingResolveDestination(
						        &mechanism_binding);

						if ((e->client->ps.pmove.pm_flags & PMF_TIME_TELEPORT) &&
						    destination)
						{
							VectorCopy(destination->s.origin, tele_dest);
							tele_dest[2] += 10.0f;
							VectorSubtract(e->s.origin, tele_dest, tele_delta);
						if (VectorLength(tele_delta) <= 2.0f)
							bot->declared_activated = true;
					}
				}
					else if (bot->declared_started && cl->action == RL_LIFT)
					{
						edict_t *plat = mechanism_binding.mover_entity;

					if (plat && SG_LiftRider(plat, e) &&
					    plat->moveinfo.state == SG_PLAT_STATE_TOP)
					{
						short top_fixed[3];
						vec3_t top_body;
						int axis;

						if (SG_LiftTopRest(plat, e, top_body))
						{
							for (axis = 0; axis < 3; axis++)
								top_fixed[axis] = (short)(top_body[axis] * 8.0f);
							if ((short)(e->s.origin[0] * 8.0f) == top_fixed[0] &&
							    (short)(e->s.origin[1] * 8.0f) == top_fixed[1] &&
							    (short)(e->s.origin[2] * 8.0f) == top_fixed[2] &&
							    (short)(e->velocity[0] * 8.0f) == 0 &&
							    (short)(e->velocity[1] * 8.0f) == 0 &&
							    (short)(e->velocity[2] * 8.0f) == 0)
								bot->declared_activated = true;
						}
					}
				}
				/* Touching the trigger or reaching the lift's top is only the
				 * mechanism event. The graph edge ends at a static seed, so retain
				 * command ownership through the short egress and retire only where
				 * the ordinary graph can truthfully continue. */
				if (bot->declared_activated)
					{
							edict_t *plat = (cl->action == RL_LIFT)
							    ? mechanism_binding.mover_entity : NULL;
							qboolean door_action = cl->action == RL_DOOR ||
							    cl->action == RL_BUTTON_DOOR;
						qboolean tele_settled = cl->action != RL_TELEPORT ||
					    (e->client->ps.pmove.pm_time == 0 &&
					     !(e->client->ps.pmove.pm_flags & PMF_TIME_TELEPORT));

					declared_arrived =
						    tele_settled &&
						    (!plat || (!SG_LiftRider(plat, e) &&
						               e->groundentity != plat)) &&
							    (!door_action ||
							     SG_BoundDoorOutsideSweep(&mechanism_binding,
							         e->s.origin)) &&
						    SG_SupportedArrived(e->s.origin,
					        SG_Rune()->seeds[cl->to].origin,
					        e->groundentity != NULL, e->watertype,
					        e->waterlevel, e);
				}
				if (declared_arrived)
				{
					drop_commit = true;
					bestlink = -1;
				}
			}
			else
			{
				if (cl->action == RL_RUN)
				{
					run_completion = SG_RunCommitCompletion(SG_Rune(), cl,
					    bot->seed, e->s.origin, goal_field);
					if (run_completion != SG_RUN_INCOMPLETE)
						drop_commit = true;
				}
				else
				{
					if (bot->seed == cl->to || VectorLength(d) < 48.0f)
						drop_commit = true;         /* arrived: step complete */
					/* or overachieved: hook landings scatter up to ~234 units
					 * from the dest seed -- if the field already prices this spot
					 * at or below the destination, the step served its purpose
					 * (holding on would re-fire the hook from its own landing zone;
					 * match 6 bounced at goal 9979 all game doing exactly that) */
					if (goal_field[bot->seed] <= goal_field[cl->to])
						drop_commit = true;
				}
			}
			if (!drop_boundary_owned &&
			    (!proved_swim || bot->swim_validated) &&
			    SG_TimerReadyStrict(bot->commit_until))
			{
				qboolean retain_door = false;

				/* Never release the sole command owner while a declared-door
				 * egress body is still inside the mover envelope. A malformed old
				 * cost or combat delay may exhaust the nominal timer; continuing
				 * toward the proved outside endpoint is safer than handing generic
				 * navigation a body that the door can occupy next mover frame. */
					if ((cl->action == RL_DOOR ||
					     cl->action == RL_BUTTON_DOOR) &&
					    bot->declared_activated)
				{
						retain_door = !SG_BoundDoorOutsideSweep(
						    &mechanism_binding, e->s.origin);
				}
				if (retain_door)
					SG_TimerArm(&bot->commit_until, 0.5f);
				else
				{
					drop_commit = true;
					if (proved_ballistic &&
					    ((cl->action == RL_JUMP && !bot->jump_started) ||
					     (cl->action == RL_DROP && !bot->drop_walkoff)))
						staging_timed_out = true;
					if (declared)
						staging_timed_out = true;
				}
			}
		}
		/* A launched witness owns a bounded flight too. Previously its expiry
		 * lived inside !ballistic, so a miss could drive the serialized heading
		 * forever while airborne. Retire/shelf at the exact deadline and spend
		 * this frame as four zero-input production commands: gravity continues,
		 * but no unproved navigation suffix is appended to the failed action. */
		if (drop_boundary_failed && ballistic)
		{
			usercmd_t coast;

			memset(&coast, 0, sizeof(coast));
			coast.msec = SG_REPLAY_STEP_MS;
			for (b = 0; b < SG_DROP_LIVE_FRAME_STEPS; b++)
				ClientThink(e, &coast);
			tc->think_over = true;
		}
		if (proved_ballistic && ballistic && !drop_boundary_owned &&
		    SG_TimerReadyStrict(bot->commit_until))
		{
			usercmd_t coast;

			drop_commit = true;
			ballistic_failed = true;
			bestlink = -1;
			memset(&coast, 0, sizeof(coast));
			coast.msec = 25;
			for (b = 0; b < 4; b++)
				ClientThink(e, &coast);
			tc->think_over = true;
		}
		for (b = 0; b < SG_BL_MAX; b++)
			if (bot->bl_link[b] == bot->commit_link &&
			    SG_TimerPending(bot->bl_until[b]))
				drop_commit = true;
		/* A declared door commitment may retire only after the shared guard
		 * positively proves the current body outside every physical member's
		 * complete sweep.  NOT_CLEAR and every observation/identity failure
		 * retain the exact action and ticket; no timeout or shelf is allowed to
		 * turn uncertainty into mover reuse. */
		if (drop_commit &&
		    (cl->action == RL_DOOR || cl->action == RL_BUTTON_DOOR) &&
		    bot->declared_started &&
		    SG_DeclaredDoorGuardReleaseProvedClear(bot) !=
		        SG_COMPOUND_GUARD_OK)
		{
			drop_commit = false;
			staging_timed_out = false;
			bestlink = bot->commit_link;
		}
		if (drop_commit && SG_TraversalControllerPhysical(bot, cl->action))
		{
			drop_commit = false;
			staging_timed_out = false;
			bestlink = bot->commit_link;
		}
		if (drop_commit)
		{
			/* Think_TrackSeed deliberately preserves the departure seed while
			 * SWIM owns the body. Do not re-arm the same departure link later in
			 * this function after shared arrival/timeout clears it; the next frame
			 * must localize the resulting body and descend from there. */
			if (proved_swim)
				bestlink = -1;
			if (staging_timed_out)
			{
				int oldest = 0;

				for (b = 0; b < SG_BL_MAX; b++)
					if (bot->bl_until[b] < bot->bl_until[oldest])
						oldest = b;
				bot->bl_link[oldest] = bot->commit_link;
				SG_TimerArm(&bot->bl_until[oldest], 10.0f);
				bestlink = -1;
			}
			if (swim_failed)
			{
				int oldest = 0;

				for (b = 0; b < SG_BL_MAX; b++)
					if (bot->bl_until[b] < bot->bl_until[oldest])
						oldest = b;
				bot->bl_link[oldest] = bot->commit_link;
				SG_TimerArm(&bot->bl_until[oldest],
				    SG_SWIM_LIVE_TIMING_SHELF_SECONDS);
				SG_TeachLinkFutility(bot->commit_link);
			}
			if (ballistic_failed)
			{
				int oldest = 0;

				for (b = 0; b < SG_BL_MAX; b++)
					if (bot->bl_until[b] < bot->bl_until[oldest])
						oldest = b;
				bot->bl_link[oldest] = bot->commit_link;
				SG_TimerArm(&bot->bl_until[oldest], 10.0f);
				if (drop_boundary_recovery_lost)
				{
					SG_TeachLinkFutility(bot->commit_link);
				}
				bestlink = -1;
				if (sg_cv.debug->value)
					sg_host.dprint("BALLISTICFAIL %s link=%d action=%d %s\n",
					           e->client->pers.netname, bot->commit_link,
					           (int)cl->action,
					           drop_boundary_reason != SG_REPLAY_REASON_NONE ?
					               SG_ReplayReasonName(drop_boundary_reason) :
					               "contact short");
			}
			bot->commit_link = -1;
			SG_RocketJumpLiveReset(&bot->rocketjump);
			bot->jump_link = -1;
			bot->jump_started = false;
			bot->drop_link = -1;
			bot->drop_started = false;
			bot->drop_walkoff = false;
			bot->drop_airborne = false;
			bot->drop_recover = false;
			SG_DropLiveReset(&bot->drop_replay, &bot->drop_replay_active,
			    &bot->drop_replay_link, &bot->drop_live_events);
			SG_SwimLiveReset(&bot->swim_replay, &bot->swim_replay_active,
			    &bot->swim_replay_link, &bot->swim_validated,
			    &bot->swim_proved_ms, &bot->swim_elapsed_ms);
			bot->declared_activated = false;
			bot->declared_started = false;
			bot->declared_start_frame = -1;
			bot->declared_touched = false;
			bot->declared_touch_frame = -1;
			SG_ButtonExecutionActionReset(bot);
			bot->declared_triggered = false;
			bot->declared_trigger_frame = -1;
			bot->declared_egress_proof_frame = -1;
			bot->declared_door_retreat = false;
			bot->declared_door_suffix_ms = 0;
			bot->declared_guard_paused = false;
			bot->declared_guard_pause_started = 0.0f;
			bot->declared_door_recovery_since = 0.0f;
		}
		else
			bestlink = bot->commit_link;
	}
	/* The supply objective is route-pure all the way to the command owner.
	 * A late generic override (rail/watchdog/hold) must not turn a blocked
	 * exact-pad/home route into a wandering RUN.  Existing mechanism owners
	 * are intentionally left alone above; this fence only rejects a fresh
	 * ordinary candidate that is not a strict live-field descent. */
	if (bot->strike_weapon_link >= 0 &&
	    bot->commit_link != bot->strike_weapon_link)
		StrikeWeaponPurposeClear(bot);
	if (SG_DefenseSupplyActive(bot) && tc->route_pure &&
	    (bot->def_supply_phase == SG_DEF_SUPPLY_OUTBOUND ||
	     bot->def_supply_phase == SG_DEF_SUPPLY_RETURN) &&
	    bot->commit_link < 0 && bestlink >= 0 && bot->seed >= 0 &&
	    bot->seed < SG_Rune()->hdr.num_seeds &&
    route_field &&
    (bestlink >= SG_Rune()->hdr.num_links ||
     !SG_DefenseSupplyActionAllowed(
         (sg_defense_supply_phase_t)bot->def_supply_phase,
         bestlink < SG_Rune()->hdr.num_links &&
         SG_Rune()->links[bestlink].action == RL_RUN) ||
     route_field[bot->seed] >= SG_FIELD_INF ||
     SG_Rune()->links[bestlink].to < 0 ||
     SG_Rune()->links[bestlink].to >= SG_Rune()->hdr.num_seeds ||
     !SG_RouteCandidateDescends(route_field[bot->seed],
         route_field[SG_Rune()->links[bestlink].to],
         Fields_LinkTraversalCostMs(&SG_Rune()->links[bestlink]),
         SG_FIELD_INF)))
		bestlink = -1;
	/* The strike weapon diversion is also an exact directed-field owner. */
	bestlink = StrikeWeaponFilterFreshCandidate(bot, tc, bestlink);
	if (bot->commit_link < 0 && bestlink >= 0)
		StrikeCommitFreshLink(bot, tc, bestlink);

	/* A rail retry owns traversal until completion or failure; the surface
	 * that selected the failed route may not interrupt its recovery. */
	if (StrikeRailLateOverrideAllowed(bot, tc) &&
	    bot->rail_stage > 0 && bot->rail_link >= 0 &&
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

	/* Shelf a stationary link, or one proved to enter a known dead door. */
	if (bot->deaddoor_ahead)
	{
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
	    /* A graph hook deliberately stops at its source, spends time aiming,
	     * then leaves the 96-unit watch ball vertically.  Its own timeout and
	     * landing verifier decide whether that traversal failed; charging those
	     * stationary proof phases to the generic orbit watch can shelve a sound
	     * hook before it has even attached. */
	    bot->hook_phase == 0 &&
	    /* Declared mechanisms can legitimately remain inside a 96-unit ball
	     * while waiting/riding. Their authoritative state machine and bounded
	     * commit deadline own failure; the generic orbit watch does not. */
	    SG_Rune()->links[bestlink].action != RL_LIFT &&
		    SG_Rune()->links[bestlink].action != RL_TELEPORT &&
		    SG_Rune()->links[bestlink].action != RL_DOOR &&
		    SG_Rune()->links[bestlink].action != RL_BUTTON_DOOR &&
	    /* Source centering is part of the exact JUMP/DROP controller and has
	     * its own bounded deadline. The generic four-second orbit watch must
	     * not shelve the link before its first proved command is submitted. */
	    !((SG_Rune()->links[bestlink].action == RL_JUMP &&
	       !bot->jump_started) ||
	      (SG_Rune()->links[bestlink].action == RL_DROP &&
	       !bot->drop_started)) &&
	    !SG_RouteFailureWatchSuppressed(role,
	        bot->seed >= 0 && bot->seed < SG_Rune()->hdr.num_seeds ?
	            goal_field[bot->seed] : SG_FIELD_INF,
	        role == SG_ROLE_ESCORT && SG_ChatEscortTarget(e) &&
	            SG_EscortTerminal(e, SG_ChatEscortTarget(e)), tc->scoop_mission,
	        duel, bot->engaged_last) &&
	    /* A defender patrol inside the post radius is intentional movement. */
	    !bot->door_held_last && !bot->mate_block_last)
	{
		/* Waiting at a commanded door is not a route failure. */
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
			/* Dead-door proof alone earns the longer 120-second shelf. */
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

	/* The body-centered watch detects stalls even when route selection flaps. */
	/* For eight seconds after a nearby grab, an escort still in the enemy
	 * base covers the carrier's exit instead of duplicating its route. */
	/* Pressure attackers cover the enemy room instead of following the carrier. */
	if (tc->rearguard &&
	    SG_AgeUnder(sg_grab_time[SG_TeamIdx(team)], 8.0f) &&
	    bot->seed >= 0)
	{
		int *att = (team == CTF_TEAM_RED) ? sg_fields.to_blue_flag
		                                  : sg_fields.to_red_flag;

		/* Pressure holds only inside the room; escorts retain the wider hold. */
		if (att && att[bot->seed] < (tc->strike_pressure ? 1500
		                                                   : 3000))
		{
			rally_hold = true;      /* stand and fight: the room is the job */
			if (bot->rally_since <= 0.0f &&
			    sg_cv.debug->value)
				sg_host.dprint("PLUG %s role=%d pressure=%d cost=%d\n",
				           e->client->pers.netname, (int)role,
				           tc->strike_pressure ? 1 : 0,
				           att[bot->seed]);
			if (bot->rally_since <= 0.0f)
				SG_Mark(&bot->rally_since);
		}
	}

	if (sg_cv.handoff->value &&
	    role == SG_ROLE_CARRY && goal_field &&
	    bot->seed >= 0 && goal_field[bot->seed] < SG_FIELD_INF &&
	    SG_TimerReady(bot->handoff_next) &&
	    (bot->engaged_last || duel))
	{
		/* Lower skill passes sooner because its carrier is less likely to finish. */
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
				if (!ctf_validateplayer(me, CTF_TEAM_ANYTEAM))
					continue;

				VectorSubtract(me->s.origin, e->s.origin, md);
				mdist = VectorLength(md);
				if (mdist > 350.0f)     /* bounded handoff range */
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

	/* Outside combat, offer a nearby unruned carrier RESIST or REGEN. The
	 * carrier's normal item pricing completes the pickup. */
	if (sg_cv.runetoss->value &&
	    SG_RuneHandoffEligible(role, tc->carrying,
	        SG_ChatOrderedRole(e), tc->strike_active,
	        tc->escort_mission) && !duel &&
	    e->client->rune &&
	    (e->client->rune->runetype == RUNE_RESIST ||
	     e->client->rune->runetype == RUNE_REGEN) &&
	    SG_TimerReady(bot->runetoss_next))
	{
		sg_belief_carrier_t *rc = &sg_caco_team_belief.carrier[SG_TeamIdx(team)];

		if (rc->client >= 0 && rc->client < game.maxclients)
		{
			edict_t *ce = g_edicts + 1 + rc->client;
			qboolean carrier_allowed = SG_RuneHandoffCarrierAllowed(team,
			    game.maxclients, rc->client, ce->inuse, ce->client != NULL,
			    ce->health, ce->deadflag != DEAD_NO,
			    ce->client ? ce->client->ctf.teamnum : 0,
			    ce->client && ClientHasFlag(ce) != NULL,
			    ce->client && ce->client->rune != NULL);

			if (carrier_allowed)
			{
				vec3_t rd14;
				float carrier_distance;
				qboolean toss_path_clear;

				VectorSubtract(ce->s.origin, e->s.origin, rd14);
				carrier_distance = VectorLength(rd14);
				toss_path_clear = SG_CanSee(e, ce->s.origin, ce->viewheight);
				if (sg_cv.debug->value &&
				    SG_TimerReady(bot->next_report - 0.9f))
					sg_host.dprint("RTCAND %s dist=%.0f\n",
					           e->client->pers.netname,
					           carrier_distance);
				if (SG_RuneHandoffTossPathAllowed(carrier_distance,
				        toss_path_clear))
				{
					float ry;

					if (SG_RuneHandoffAim(rd14[0], rd14[1], &ry))
					{
						/* Drop_Rune -> ctf_TossEnt reads v_angle now, before
						 * this command reaches Pmove.  Bind both views to the
						 * same flat carrier bearing so the physical rune follows
						 * the handoff decision instead of last frame's aim. */
						e->client->v_angle[YAW] = ry;
						e->client->v_angle[PITCH] = 0.0f;
						e->client->v_angle[ROLL] = 0.0f;
						cmd->angles[YAW] = ANGLE2SHORT(ry)
						    - e->client->ps.pmove.delta_angles[YAW];
						cmd->angles[PITCH] = ANGLE2SHORT(0.0f)
						    - e->client->ps.pmove.delta_angles[PITCH];
						cmd->angles[ROLL] = ANGLE2SHORT(0.0f)
						    - e->client->ps.pmove.delta_angles[ROLL];
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
	}

	/* When the home flag is astray, wait at a valid cover seed outside the
	 * terminal zone. Without cover, keep moving. */
	if (role == SG_ROLE_CARRY)
	{
		qboolean ours_astray =
		    sg_caco_team_belief.flag[SG_TeamIdx(team)][SG_TeamIdx(team)].state ==
		        SG_FLAG_ASTRAY;

		if (!ours_astray)
		{
			bot->rally_cover = -1;
			rally_hold = false;
		}
		else if (bot->seed >= 0 && goal_field[bot->seed] < SG_FIELD_INF &&
		         goal_field[bot->seed] < 2500)
		{
			int cover = bot->rally_cover;

			if (cover < 0 || cover >= SG_Rune()->hdr.num_seeds ||
			    !SG_CarrierCoverRouteAllowed(SG_Rune(), bot->seed, cover) ||
			    goal_field[cover] < 600 || goal_field[cover] >= 2500 ||
			    !SG_CanSee(e, SG_Rune()->seeds[cover].origin, 22.0f))
				cover = Carrier_RallyCover(bot, e, goal_field);
			bot->rally_cover = cover;
			rally_hold = cover >= 0;
			if (sg_cv.debug->value && rally_hold &&
			    SG_TimerReady(bot->next_report - 0.9f))
				sg_host.dprint("CARRYHOLD %s cost=%d cover=%d\n",
				           e->client->pers.netname, goal_field[bot->seed], cover);
		}
	}

	/* level.time restarts on map change; discard a future-map timestamp. */
	if (bot->railhold_since > level.time ||
	    bot->railhold_next > level.time + SG_RAIL_HOLD_GAP)
	{
		bot->railhold_since = 0.0f;
		bot->railhold_next = 0.0f;
		bot->railhold_enemy = -1;
	}

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
				/* Refresh identity without restarting the bounded
				 * patience clock. */
				bot->railhold_enemy = rail_client;
				if (SG_AgeUnder(bot->railhold_since,
				    bot->railhold_patience))
					rail_hold = true;
			}
		}
	}
	if (!rail_hold && bot->railhold_since > 0.0f)
	{
		/* Record whether the rail window or patience ended the hold. */
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

	/* After fifteen seconds of unexplained zero displacement, respawn rather
	 * than leave a permanently stranded bot in play. */
	VectorSubtract(e->s.origin, bot->wedge_org, d);
	if (SG_WedgeClockReset(VectorLength(d), duel, bot->engaged_last))
	{
		VectorCopy(e->s.origin, bot->wedge_org);
		SG_Mark(&bot->wedge_since);
	}
	else if (SG_AgeOver(bot->wedge_since, 15.0f) &&
	         enemy_pressure &&
	         SG_AttackFlagDirectTouchAuthority(e, team, NULL))
	{
		/* The live flag is a direct touch recovery, not a reason to respawn.
		 * Clear only a plain RUN so the next movement frame takes the live
		 * item through-line; controller-owned commitments keep their bounds. */
		bot->terminal = true;
		bot->sticky_link = -1;
		if (bot->commit_link >= 0 &&
		    bot->commit_link < SG_Rune()->hdr.num_links &&
		    SG_Rune()->links[bot->commit_link].action == RL_RUN)
		{
			bot->commit_link = -1;
			bot->commit_until = 0.0f;
		}
		SG_Mark(&bot->wedge_since);
		return -1;
	}
	else if (SG_AgeOver(bot->wedge_since, 15.0f) &&
	         !ThinkMissionHold(bot, tc, goal_field) &&
	         /* Declared mechanisms legitimately park the body while a lift
	          * queues, moves beneath it, or carries it.  Their authoritative
	          * state machine and bounded commit deadline own failure; the
	          * generic statue valve must not kill a correct rider/waiter. */
	         !(bot->commit_link >= 0 &&
	           bot->commit_link < SG_Rune()->hdr.num_links &&
		           (SG_Rune()->links[bot->commit_link].action == RL_LIFT ||
		            SG_Rune()->links[bot->commit_link].action == RL_TELEPORT ||
		            SG_Rune()->links[bot->commit_link].action == RL_DOOR ||
		            SG_Rune()->links[bot->commit_link].action ==
		                RL_BUTTON_DOOR)) &&
	         /* A LIVE CARRIER IS NEVER SUICIDED (carry analysis, 791
	          * episodes): 12 of the 19 parity carries that REACHED
	          * within 300u of home ended as WEDGEKILL orbits at the
	          * stand -- zero damage, no enemy inside 900u. The wedge
	          * valve was executing the very carries everything else
	          * exists to produce. The progress guard's shelf wipe is
	          * the carrier's remedy; a death hands the flag back. */
	         role != SG_ROLE_CARRY &&
	         /* Near-goal organic escorts and exact "cover me" arrivals were
	          * classified as mission holds above, not wedged routes. */
	         /* Rail and rally/rearguard timestamps outlive individual holds.
	          * Exempt only the same-frame intentional hold, not historical
	          * evidence that this body once waited. */
	         SG_WedgeKillHoldClear(rally_hold, rail_hold))
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
	if (VectorLength(d) > 96.0f || !bot->nav_drove || bot->engaged_last ||
	    duel ||
	    /* Waiting/riding is progress for a declared mechanism even when the
	     * body stays inside the stagnation ball.  Do not bill that intentional
	     * hold to the graph link before its own deadline can decide it. */
	    (bot->commit_link >= 0 &&
	     bot->commit_link < SG_Rune()->hdr.num_links &&
		     (SG_Rune()->links[bot->commit_link].action == RL_LIFT ||
		      SG_Rune()->links[bot->commit_link].action == RL_TELEPORT ||
		      SG_Rune()->links[bot->commit_link].action == RL_DOOR ||
		      SG_Rune()->links[bot->commit_link].action == RL_BUTTON_DOOR)))
	{
		/* Stagnation counts only while navigation owns the legs. */
		VectorCopy(e->s.origin, bot->stag_org);
		SG_Mark(&bot->stag_since);
	}
	else if (bestlink >= 0 &&
	         !ThinkMissionHold(bot, tc, goal_field) &&
	    /* A defender patrol inside the post radius is intentional movement. */
	         !bot->door_held_last && !bot->mate_block_last &&
	         SG_AgeOver(bot->stag_since, 8.0f) &&
	         SG_TimerReady(bot->stag_next))
	{
		int b, oldest = 0;

		/* Retry a stalled RUN from its proved source before shelving it. */
		if (StrikeRailWatchdogAllowed(bot, tc) &&
		    SG_Rune()->links[bestlink].action == RL_RUN &&
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
		/* Back out of a failed local pocket before retrying the approach. */
		/* jittered: identical retreats produce identical re-approaches,
		 * and an obstacle that beats one line beats it every time */
		bot->escape_random = SG_EscapeRandomNext(bot->escape_random);
		bot->escape_yaw = e->s.angles[YAW] + 180.0f +
		    (float)SG_EscapeRandomYaw(bot->escape_random);
		bot->escape_random = SG_EscapeRandomNext(bot->escape_random);
		SG_TimerArm(&bot->escape_until,
		    SG_EscapeRandomDuration(bot->escape_random));
		if (sg_cv.debug->value)
			sg_host.dprint("STAGSHELVE %s link=%d at seed=%d\n",
			           e->client->pers.netname, bestlink, bot->seed);
	}
stag_done:
	bot->nav_drove = false;         /* the movement code below re-arms it */
	/* consumed by the watch above; the feelers re-raise it if the body is
	 * still there this frame */
	bot->mate_block_last = false;

	/* Detect loops wider than the route watch from recent objective visits
	 * and the best cost reached after each one. */
	{
		int gv = (goal_field[bot->seed] < SG_FIELD_INF)
		             ? goal_field[bot->seed] : 0x7ffffff;
		sg_field_key_t goal = SG_FieldKey(SG_Rune(), goal_field);
		edict_t *enemy_flag = enemy_pressure ? SG_EnemyFlag(team) : NULL;
		qboolean enemy_flag_home = enemy_flag && ctf_flagathome(enemy_flag);
		int v;

		/* A departure onto another field or moving source starts new history. */
		if (!SG_FieldKeyMatches(bot->orbit_goal, goal))
		{
			for (v = 0; v < SG_VISIT_RING; v++)
			{
				bot->visit_seed[v] = -1;
				bot->visit_key[v] = (sg_field_key_t){ 0 };
				bot->visit_combat[v] = false;
			}
			bot->visit_head = 0;
			bot->orbit_last_seed = -1;
			bot->orbit_goal = goal;
		}

		/* every live entry keeps the best goal the bot has touched since
		 * that visit -- THIS is what distinguishes a loop from a route
		 * that passes a hallway twice. The first version compared the
		 * seed's own field value across visits, which is CONSTANT, and
		 * shelved every second visit on the map (campaign 2: steals
		 * 7 -> 1, lmctf03 shelves 434). */
		for (v = 0; v < SG_VISIT_RING; v++)
			if (bot->visit_seed[v] >= 0 &&
			    SG_FieldKeyMatches(bot->visit_key[v], goal))
			{
				if (gv < bot->visit_min[v])
					bot->visit_min[v] = gv;
				if (duel || bot->engaged_last)
					bot->visit_combat[v] = true;
			}

		if (bot->seed != bot->orbit_last_seed)
		{
			bot->orbit_last_seed = bot->seed;
			/* A carrier's loop always loses objective progress. Pressure
			 * attackers may now use the same repair only when the same field
			 * remained live, the enemy flag stayed home, and no duel/combat
			 * occurred anywhere in the interval. This preserves the campaign-3
			 * lesson: resistance must never shred a sound route. */
			for (v = 0; v < SG_VISIT_RING; v++)
				if (bot->visit_seed[v] == bot->seed &&
				    SG_FieldKeyMatches(bot->visit_key[v], goal) &&
				    SG_AgeUnder(bot->visit_time[v], 30.0f) &&
				    SG_AgeOver(bot->visit_time[v], 3.0f) &&
				    bot->visit_min[v] >= bot->visit_goal[v] &&
				    SG_ObjectiveOrbitMayShelf((int)role, enemy_pressure,
				        enemy_flag_home, bot->visit_combat[v]) &&
				    bestlink >= 0)
				{
					/* back where it was, and it never once got closer
					 * in between: an orbit, whatever its diameter */
					int b, oldest = 0;
					int cycle_link = bestlink;
					int alternate;

					for (b = 0; b < SG_BL_MAX; b++)
						if (bot->bl_until[b] < bot->bl_until[oldest])
							oldest = b;
					bot->bl_link[oldest] = cycle_link;
					SG_TimerArm(&bot->bl_until[oldest], 45.0f);
					/* A shelf must change this decision now, not merely make the
					 * next frame rediscover it.  Prefer an unvisited descending
					 * branch; when this is a one-exit corridor, permit its finite
					 * exit without deleting the fresh cycle evidence. */
					alternate = Objective_CycleRoute(bot, goal, true);
					if (alternate < 0)
						alternate = Objective_CycleRoute(bot, goal, false);
					if (bot->commit_link == cycle_link &&
					    SG_Rune()->links[cycle_link].action == RL_RUN)
					{
						bot->commit_link = -1;
						bot->commit_until = 0.0f;
					}
					if (alternate >= 0)
						bestlink = alternate;
					else
						/* A multi-exit fan with no safe alternate must not keep the
						 * freshly shelved loop alive for this command frame. */
						bestlink = -1;
					if (sg_cv.debug->value)
						sg_host.dprint("CYCLE %s seed=%d link=%d next=%d\n",
						           e->client->pers.netname, bot->seed,
						           cycle_link, bestlink);
					break;
				}
			bot->visit_seed[bot->visit_head] = bot->seed;
			bot->visit_goal[bot->visit_head] = gv;
			bot->visit_min[bot->visit_head] = gv;
			bot->visit_key[bot->visit_head] = goal;
			bot->visit_combat[bot->visit_head] = duel || bot->engaged_last;
			SG_Mark(&bot->visit_time[bot->visit_head]);
			bot->visit_head = (bot->visit_head + 1) % SG_VISIT_RING;
		}
	}

	/* Deaddoor and every later shelf/invalidation above share this retirement
	 * fence. It runs after those verdicts and before hold_post/Think_Move, so a
	 * retired lateral RUN cannot release the stand for one command frame. */
	(void)DefenseShiftRetireInvalid(bot, &bestlink, &defense_shift_selected);

	/* RETURN ends only in the real own-flag/post band.  The objective has
	 * already replaced any astray-flag/intercept goal with the fixed home
	 * field, so this fence cannot finish on the weapon pad or a mixed tactic. */
	if (bot->def_supply_armed &&
	    bot->def_supply_phase == SG_DEF_SUPPLY_RETURN &&
	    bot->seed >= 0 && bot->seed < SG_Rune()->hdr.num_seeds &&
	    goal_field[bot->seed] < 400.0f * SG_PersonaCampScale(e))
		SG_DefenseSupplyFinish(bot);

	if (bot->lead_ent > 0 && bot->lead_state == SG_LEAD_WAITING &&
	    goal_field[bot->seed] < SG_LEAD_STANDOFF)
		hold_post = true;
	else if (role == SG_ROLE_DEFEND && bot->def_stand &&
	    ((float)goal_field[bot->seed] < 400.0f * SG_PersonaCampScale(e) ||
	     bot->patrol_seed >= 0))
	{
		qboolean quiet = defense_quiet;

		if (bot->def_supply_armed)
			goto no_hold; /* The supply sortie owns movement. */

		if (quiet &&
		    (w->item[SG_FC_ARMOR] > 0.9f || w->item[SG_FC_HEALTH] > 0.9f ||
		     w->item[SG_FC_AMMO] > 0.9f))
			goto no_hold;   /* needy and unthreatened: run the errand */

		hold_post = true;

		/* The selected step already passed through the normal commitment above.
		 * Suppress the stand command only while that exact RUN owns the body. */
		if (defense_shift_selected && bot->def_shift_seed >= 0 &&
		    bot->commit_link == bot->def_shift_link)
			hold_post = false;

		if (quiet && bot->patrol_link >= 0 &&
		    bot->patrol_link == bot->commit_link &&
		    DefenseLocalRunReady(bot, bot->patrol_link,
		        bot->seed, bot->patrol_seed))
		{
			bestlink = bot->patrol_link;
			hold_post = false;
			tc->patrol_walk = true;
		}
	}

	/*
	 * The facing, shared by both holds, follows the nearest exact incoming RUN
	 * on this scoring field. Combat still owns the view when anyone appears.
	 */
	if (hold_post)
	{
		int face = SG_DefenseFacingSeed(SG_Rune(), bot->seed, goal_field,
		    SG_FIELD_INF);
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
	SG_CombatPost(e, hold_post ? post_sight : -1.0f,
	              hold_post && role == SG_ROLE_DEFEND && bot->def_stand);

	/*
	 * Whether this bot may hold a corner on a target it just lost.  The
	 * effective objective decides: pressure and recovery routes are already
	 * going that way, while carrier and escort clocks do not admit a camp.  A
	 * coordinator duty overrides the earlier organic role on the same frame,
	 * just as it overrides the route.  An uncoordinated defender may watch a
	 * doorway only while it remains on its own ground -- 2500 ms of the home
	 * field, the same order as the post's 400 and pre-spin's 1200.
	 */
	SG_CombatPursuit(e, (qboolean)(tc->combat_pursuit ||
	                               (!tc->strike_active &&
	                                role == SG_ROLE_DEFEND &&
	                                goal_field[bot->seed] < 2500)));

	if (!hold_post)
		SG_CombatAlertFromBeliefs(e, goal_field);

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

	/* HOLD/RUSH is a same-frame coordinator verdict.  Room, rail, and
	 * rearguard policy above may set their ordinary holds, but none may delay
	 * a direct-touch or real-room-death RUSH release. */
	if (tc->strike_rush)
	{
		rally_hold = false;
		rail_hold = false;
		bot->rally_since = 0.0f;
		bot->nade_phase = 0;
		SG_NadeTargetClear(bot);
		bot->nade_until = 0.0f;
	}
	tc->rally_hold = rally_hold;
	tc->rail_hold = rail_hold;
	tc->hold_post = hold_post;
	tc->post_yaw = post_yaw;
	tc->post_sight = post_sight;
	return bestlink;
}

#ifdef SG_STRIKE_TRANSITION_TEST_API
qboolean SG_StrikeTestWeaponReconcile(sg_bot_t *bot, sg_think_t *tc)
{
	return StrikeWeaponPurposeReconcile(bot, tc);
}

void SG_StrikeTestWeaponCancelStaged(sg_bot_t *bot, int action)
{
	SG_StagedTraversalCancel(bot, action);
}

qboolean SG_StrikeTestWeaponPrepareCommit(sg_bot_t *bot, sg_think_t *tc)
{
	return StrikeWeaponPrepareCommit(bot, tc);
}

int SG_StrikeTestWeaponFilterFreshCandidate(const sg_bot_t *bot,
	const sg_think_t *tc, int bestlink)
{
	return StrikeWeaponFilterFreshCandidate(bot, tc, bestlink);
}

void SG_StrikeTestCommitFreshLink(sg_bot_t *bot, const sg_think_t *tc,
	int bestlink)
{
	StrikeCommitFreshLink(bot, tc, bestlink);
}

qboolean SG_StrikeTestPureRouteRetirementBlocksFrame(sg_bot_t *bot,
	sg_think_t *tc)
{
	return PureRouteRetirementBlocksFrame(bot, tc);
}

qboolean SG_StrikeTestFlagTouchTerminalRetainsCommit(sg_bot_t *bot,
	sg_think_t *tc, qboolean touch_authorized)
{
	return FlagTouchTerminalRetainsCommit(bot, tc, touch_authorized);
}

qboolean SG_StrikeTestRailLateOverrideAllowed(const sg_bot_t *bot,
	const sg_think_t *tc)
{
	return StrikeRailLateOverrideAllowed(bot, tc);
}

qboolean SG_StrikeTestRailWatchdogAllowed(const sg_bot_t *bot,
	const sg_think_t *tc)
{
	return StrikeRailWatchdogAllowed(bot, tc);
}
#endif
