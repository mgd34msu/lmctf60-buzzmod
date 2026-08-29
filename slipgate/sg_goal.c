/*
 * sg_goal.c -- frame goals, carry transitions, objective selection,
 * interception, and approach behavior.
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
#include "slipgate/sg_item_route.h"
#include "slipgate/sg_field_projection.h"
#include "slipgate/sg_tactic_policy.h"
#include "slipgate/sg_intercept_policy.h"
#include "slipgate/sg_role_policy.h"
#include "slipgate/sg_rune_handoff_policy.h"
#include "slipgate/sg_escape_random.h"
#include "slipgate/sg_strike.h"
#include "slipgate/sg_goal.h"
#include "slipgate/sg_defense_supply.h"
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_move.h"
#include "slipgate/sg_pickup_target.h"

static int intercept_field[SG_MAX_SEEDS];

#define SG_MEGA_PATIENCE	12.0f   /* seconds an offer may stand unspent */
#define SG_MEGA_BACKOFF		20.0f   /* ...and the refusal after, the pad's own
                                     * respawn (SetRespawn 20, g_items.c:596) */

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

/* Resolve pre-breach actions and report whether the bot waits for support. */
qboolean Think_ApproachBand(sg_bot_t *bot, sg_think_t *tc)
{
	edict_t *e = tc->e;
	sg_role_t role = tc->role;
	int team = tc->team;
	const int *goal_field = tc->goal_field;
	int goal_ms = -1;
	qboolean hold = false, pressure_approach;
	if (tc->rune_handoff_route)
	{
		bot->rally_since = 0.0f;
		return false;
	}
	if (bot->seed >= 0 && SG_Rune() &&
	    bot->seed < SG_Rune()->hdr.num_seeds)
		goal_ms = goal_field[bot->seed];
	pressure_approach = SG_StrikePrebreachApproachAllowed(
	    tc->strike_active, tc->strike_pressure,
	    role == SG_ROLE_ATTACK, goal_ms);

	/* A strike frame owns HOLD/RUSH, outside the organic rally conductor. */
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

		/* Wait only for a live teammate that can reach the approach band. */
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
				mates_coming++;
		}
		{
			/* A recent death near the stand cancels the rally wait. */
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

				/* Rally cover is the current seed or one proved RUN away. */
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

	/* Strike pressure shares this live-enemy approach action. */
	if (pressure_approach && sg_cv.flycook->value &&
	    !bot->jump_started && !bot->drop_started &&
	    bot->hook_phase == 0 && !SG_RocketJumpLiveOwns(&bot->rocketjump) &&
	    bot->nade_phase == 0 &&
	    !(SG_Rune() && bot->commit_link >= 0 &&
	      bot->commit_link < SG_Rune()->hdr.num_links &&
	      SG_ActionOwnsControl(
	          SG_Rune()->links[bot->commit_link].action)) &&
	    SG_TimerReady(bot->nade_next) &&
	    !SG_AttackFlagDirectTouchAuthority(e, team, NULL))
	{
		(void)SG_NadeArmPrebreachLiveEnemy(bot, e, team);
	}
	return hold;
}

/* Build carrier-support and enemy-carrier interception fields. */
void Think_InterceptField(sg_role_t role, int team,
                                 const int **support_out,
                                 const int **intercept_out)
{
	if (role != SG_ROLE_CARRY)
	{
		int ti = SG_TeamIdx(team);
		sg_belief_carrier_t *ec = &sg_caco_team_belief.enemy_carrier[ti];

		*support_out = sg_fields.our_carrier[ti];
		if (ec->seed >= 0)
		{
			const int *home = team == CTF_TEAM_RED
			    ? sg_fields.to_blue_flag : sg_fields.to_red_flag;
			int cost = 0;
			int hold = SG_InterceptHoldSeed(SG_Rune(),
			    &sg_caco_proj[ti], home, ec->seed);

			/* the hold ground across the thief's projected motion --
			 * or their believed position when the projection is thin */
			Field_Flood(SG_Rune(), intercept_field, &hold, &cost, 1);
			*intercept_out = intercept_field;
		}
	}
}

/* Update carry transitions, exit choice, and the last effective role. */
void Think_CarryBookends(sg_bot_t *bot, edict_t *e,
                                sg_role_t role, int team,
                                qboolean carrying)
{
	/* Carry state is game logic and must not depend on debug output. */
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

		/* Draw one weighted escape bucket per carry and hold it for the
		 * three-second sampling window. The bot identity and life make
		 * simultaneous carriers independent without repricing the draw. */
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
				unsigned h = SG_EscapePriorDraw(bot->instance_token,
				    (unsigned)(e - g_edicts - 1), (unsigned)bot->lives,
				    (unsigned)bot->legs, (unsigned)(level.time * 10.0f));
				int b, acc = 0, pick;

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
			/* Record defender belief and whether the hold expired. */
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
	/* Coordinator duties may override this role, so rally pairing uses its
	 * separate pre-frame snapshot. */
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
	    bot->rail_stage > 0 || SG_RocketJumpLiveOwns(&bot->rocketjump) ||
	    bot->nade_phase > 0 ||
	    bot->hook_phase > 0 || bot->jump_link >= 0 || bot->drop_link >= 0)
		return true;
	/* A pre-existing route commitment belongs to another action.  Once the
	 * sortie is armed, its own commit is allowed to remain live. */
	return !active && bot->commit_link >= 0;
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

/* Defenders and strike attackers are mutually exclusive route owners, so one
 * per-bot exact-pickup flood cache serves both without another MAX_SEEDS x
 * SG_MAXBOTS allocation.  Every scan invalidates the cache because its
 * scratch floods visit several candidate pads before the winner is known. */
static int sg_weapon_target_field[SG_MAXBOTS][SG_MAX_SEEDS];
static unsigned sg_weapon_target_epoch[SG_MAXBOTS];
static int sg_weapon_target_cached[SG_MAXBOTS];
static unsigned char sg_weapon_target_ready[SG_MAXBOTS];
#define SG_WEAPON_FIELD_NONE        0
#define SG_WEAPON_FIELD_EXACT       1
#define SG_WEAPON_FIELD_COLLECTIBLE 2
#define SG_WEAPON_FIELD_SOURCES     256
static unsigned char sg_weapon_field_kind[SG_MAXBOTS];
static int sg_weapon_collectible_count[SG_MAXBOTS];
static int sg_weapon_collectible_ent[SG_MAXBOTS][SG_WEAPON_FIELD_SOURCES];
static int sg_weapon_collectible_seed[SG_MAXBOTS][SG_WEAPON_FIELD_SOURCES];
static int sg_weapon_collectible_cost[SG_MAXBOTS][SG_WEAPON_FIELD_SOURCES];
static int sg_health_collectible_field[SG_MAXBOTS][SG_MAX_SEEDS];
static unsigned sg_health_collectible_epoch[SG_MAXBOTS];
static unsigned char sg_health_collectible_ready[SG_MAXBOTS];
static int sg_health_collectible_count[SG_MAXBOTS];
static int sg_health_collectible_ent[SG_MAXBOTS][SG_WEAPON_FIELD_SOURCES];
static int sg_health_collectible_seed[SG_MAXBOTS][SG_WEAPON_FIELD_SOURCES];
static int sg_health_collectible_cost[SG_MAXBOTS][SG_WEAPON_FIELD_SOURCES];
static int sg_ammo_collectible_field[SG_MAXBOTS][SG_MAX_SEEDS];
static unsigned sg_ammo_collectible_epoch[SG_MAXBOTS];
static unsigned char sg_ammo_collectible_ready[SG_MAXBOTS];
static int sg_ammo_collectible_count[SG_MAXBOTS];
static int sg_ammo_collectible_ent[SG_MAXBOTS][SG_WEAPON_FIELD_SOURCES];
static int sg_ammo_collectible_seed[SG_MAXBOTS][SG_WEAPON_FIELD_SOURCES];
static int sg_armor_collectible_field[SG_MAXBOTS][SG_MAX_SEEDS];
static unsigned sg_armor_collectible_epoch[SG_MAXBOTS];
static unsigned char sg_armor_collectible_ready[SG_MAXBOTS];
static int sg_armor_collectible_count[SG_MAXBOTS];
static int sg_armor_collectible_ent[SG_MAXBOTS][SG_WEAPON_FIELD_SOURCES];
static int sg_armor_collectible_seed[SG_MAXBOTS][SG_WEAPON_FIELD_SOURCES];
static int sg_armor_collectible_cost[SG_MAXBOTS][SG_WEAPON_FIELD_SOURCES];
/* The aggregate armor field is useful for pricing but cannot authenticate one
 * semantic item.  Strategy prerequisites therefore use this separate exact
 * one-target field and retain the selected live edict alongside it. */
static int sg_armor_target_field[SG_MAXBOTS][SG_MAX_SEEDS];
static unsigned sg_armor_target_epoch[SG_MAXBOTS];
static int sg_armor_target_seed[SG_MAXBOTS];
static int sg_armor_target_ent[SG_MAXBOTS];

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

static const int *WeaponTargetField(sg_bot_t *bot, int target_seed)
{
	int bi, cost = 0;

	if (!bot || !SG_Rune() || target_seed < 0 ||
	    target_seed >= SG_Rune()->hdr.num_seeds)
		return NULL;
	bi = DefenseSupplyBotIndex(bot);
	if (!sg_weapon_target_ready[bi] ||
	    sg_weapon_field_kind[bi] != SG_WEAPON_FIELD_EXACT ||
	    sg_weapon_target_cached[bi] != target_seed ||
	    sg_weapon_target_epoch[bi] != sg_fields.action_topology_epoch)
	{
		Field_Flood(SG_Rune(), sg_weapon_target_field[bi],
		            &target_seed, &cost, 1);
		sg_weapon_target_epoch[bi] = sg_fields.action_topology_epoch;
		sg_weapon_target_cached[bi] = target_seed;
		sg_weapon_field_kind[bi] = SG_WEAPON_FIELD_EXACT;
		sg_weapon_target_ready[bi] = 1;
	}
	return sg_weapon_target_field[bi];
}

const int *SG_CollectibleWeaponField(sg_bot_t *bot)
{
	sg_combat_weapon_state_t weapon;
	int ents[SG_WEAPON_FIELD_SOURCES];
	int seeds[SG_WEAPON_FIELD_SOURCES];
	int costs[SG_WEAPON_FIELD_SOURCES];
	int gains[SG_WEAPON_FIELD_SOURCES];
	int bi, best_gain = 0, count = 0, i;
	qboolean same;

	if (!bot || !bot->ent || !SG_Rune() ||
	    !SG_CombatWeaponState(bot->ent, &weapon))
		return NULL;
	bi = DefenseSupplyBotIndex(bot);
	for (i = 1; i < globals.num_edicts &&
	     count < SG_WEAPON_FIELD_SOURCES; i++)
	{
		edict_t *item = &g_edicts[i];
		int seed;

		if (!SG_WeaponPickupRouteEligible(item, bot->ent))
			continue;
		gains[count] = SG_CombatWeaponPickupTier(item) -
		    weapon.available_tier;
		if (!SG_WeaponUpgradeRouteAdmission(weapon.available_tier,
		    weapon.available_tier + gains[count], true))
			continue;
		seed = Rune_NearestSeed(SG_Rune(), item->s.origin);
		if (seed < 0)
			continue;
		ents[count] = i;
		seeds[count] = seed;
		if (gains[count] > best_gain)
			best_gain = gains[count];
		count++;
	}
	if (count == 0)
	{
		sg_weapon_target_ready[bi] = 0;
		sg_weapon_field_kind[bi] = SG_WEAPON_FIELD_NONE;
		sg_weapon_collectible_count[bi] = 0;
		return NULL;
	}
	for (i = 0; i < count; i++)
		costs[i] = SG_ItemGainSourceCost(gains[i], best_gain);

	same = sg_weapon_target_ready[bi] &&
	       sg_weapon_field_kind[bi] == SG_WEAPON_FIELD_COLLECTIBLE &&
	       sg_weapon_target_epoch[bi] == sg_fields.action_topology_epoch &&
	       sg_weapon_collectible_count[bi] == count;
	for (i = 0; same && i < count; i++)
		if (sg_weapon_collectible_ent[bi][i] != ents[i] ||
		    sg_weapon_collectible_seed[bi][i] != seeds[i] ||
		    sg_weapon_collectible_cost[bi][i] != costs[i])
			same = false;
	if (!same)
	{
		Field_Flood(SG_Rune(), sg_weapon_target_field[bi], seeds,
		            costs, count);
		for (i = 0; i < count; i++)
		{
			sg_weapon_collectible_ent[bi][i] = ents[i];
			sg_weapon_collectible_seed[bi][i] = seeds[i];
			sg_weapon_collectible_cost[bi][i] = costs[i];
		}
		sg_weapon_collectible_count[bi] = count;
		sg_weapon_target_epoch[bi] = sg_fields.action_topology_epoch;
		sg_weapon_field_kind[bi] = SG_WEAPON_FIELD_COLLECTIBLE;
		sg_weapon_target_ready[bi] = 1;
	}
	return sg_weapon_target_field[bi];
}

const int *SG_CollectibleHealthField(sg_bot_t *bot)
{
	int ents[SG_WEAPON_FIELD_SOURCES];
	int seeds[SG_WEAPON_FIELD_SOURCES];
	int costs[SG_WEAPON_FIELD_SOURCES];
	int gains[SG_WEAPON_FIELD_SOURCES];
	int bi, best_gain = 0, count = 0, i;
	qboolean same;

	if (!bot || !bot->ent || !SG_Rune())
		return NULL;
	bi = DefenseSupplyBotIndex(bot);
	for (i = 1; i < globals.num_edicts &&
	     count < SG_WEAPON_FIELD_SOURCES; i++)
	{
		edict_t *item = &g_edicts[i];
		int seed;

		if (!item->inuse || !item->classname ||
		    strncmp(item->classname, "item_health", 11) != 0 ||
		    item->solid == SOLID_NOT || !Caco_ItemBelievedUp(item) ||
		    !SG_HealthClassRouteAdmission(
		        strcmp(item->classname, "item_health_mega") == 0,
		        SG_MegaOn(), G_HealthPickupEligible(item, bot->ent)))
			continue;
		gains[count] = G_HealthPickupGain(item, bot->ent);
		if (gains[count] <= 0)
			continue;
		seed = Rune_NearestSeed(SG_Rune(), item->s.origin);
		if (seed < 0)
			continue;
		ents[count] = i;
		seeds[count] = seed;
		if (gains[count] > best_gain)
			best_gain = gains[count];
		count++;
	}
	if (count == 0)
	{
		sg_health_collectible_ready[bi] = 0;
		sg_health_collectible_count[bi] = 0;
		return NULL;
	}
	for (i = 0; i < count; i++)
		costs[i] = SG_ItemGainSourceCost(gains[i], best_gain);

	same = sg_health_collectible_ready[bi] &&
	       sg_health_collectible_epoch[bi] ==
	           sg_fields.action_topology_epoch &&
	       sg_health_collectible_count[bi] == count;
	for (i = 0; same && i < count; i++)
		if (sg_health_collectible_ent[bi][i] != ents[i] ||
		    sg_health_collectible_seed[bi][i] != seeds[i] ||
		    sg_health_collectible_cost[bi][i] != costs[i])
			same = false;
	if (!same)
	{
		Field_Flood(SG_Rune(), sg_health_collectible_field[bi], seeds,
		            costs, count);
		for (i = 0; i < count; i++)
		{
			sg_health_collectible_ent[bi][i] = ents[i];
			sg_health_collectible_seed[bi][i] = seeds[i];
			sg_health_collectible_cost[bi][i] = costs[i];
		}
		sg_health_collectible_count[bi] = count;
		sg_health_collectible_epoch[bi] =
		    sg_fields.action_topology_epoch;
		sg_health_collectible_ready[bi] = 1;
	}
	return sg_health_collectible_field[bi];
}

const int *SG_CollectibleAmmoField(sg_bot_t *bot)
{
	int ents[SG_WEAPON_FIELD_SOURCES];
	int seeds[SG_WEAPON_FIELD_SOURCES];
	int costs[SG_WEAPON_FIELD_SOURCES];
	int bi, count = 0, i, held_ammo_tag;
	qboolean same;

	if (!bot || !bot->ent || !SG_Rune())
		return NULL;
	bi = DefenseSupplyBotIndex(bot);
	held_ammo_tag = SG_CombatHeldAmmoTag(bot->ent);
	if (held_ammo_tag < 0)
	{
		sg_ammo_collectible_ready[bi] = 0;
		sg_ammo_collectible_count[bi] = 0;
		return NULL;
	}
	for (i = 1; i < globals.num_edicts &&
	     count < SG_WEAPON_FIELD_SOURCES; i++)
	{
		edict_t *item = &g_edicts[i];
		int seed;

		if (!item->inuse || !item->classname || !item->item ||
		    strncmp(item->classname, "ammo_", 5) != 0 ||
		    item->solid == SOLID_NOT || !Caco_ItemBelievedUp(item) ||
		    !SG_AmmoRouteAdmission(item->item->tag, held_ammo_tag,
		        G_AmmoPickupEligible(item, bot->ent)))
			continue;
		seed = Rune_NearestSeed(SG_Rune(), item->s.origin);
		if (seed < 0)
			continue;
		ents[count] = i;
		seeds[count] = seed;
		costs[count] = 0;
		count++;
	}
	if (count == 0)
	{
		sg_ammo_collectible_ready[bi] = 0;
		sg_ammo_collectible_count[bi] = 0;
		return NULL;
	}

	same = sg_ammo_collectible_ready[bi] &&
	       sg_ammo_collectible_epoch[bi] ==
	           sg_fields.action_topology_epoch &&
	       sg_ammo_collectible_count[bi] == count;
	for (i = 0; same && i < count; i++)
		if (sg_ammo_collectible_ent[bi][i] != ents[i] ||
		    sg_ammo_collectible_seed[bi][i] != seeds[i])
			same = false;
	if (!same)
	{
		Field_Flood(SG_Rune(), sg_ammo_collectible_field[bi], seeds,
		            costs, count);
		for (i = 0; i < count; i++)
		{
			sg_ammo_collectible_ent[bi][i] = ents[i];
			sg_ammo_collectible_seed[bi][i] = seeds[i];
		}
		sg_ammo_collectible_count[bi] = count;
		sg_ammo_collectible_epoch[bi] =
		    sg_fields.action_topology_epoch;
		sg_ammo_collectible_ready[bi] = 1;
	}
	return sg_ammo_collectible_field[bi];
}

const int *SG_CollectibleArmorField(sg_bot_t *bot)
{
	int ents[SG_WEAPON_FIELD_SOURCES];
	int seeds[SG_WEAPON_FIELD_SOURCES];
	int costs[SG_WEAPON_FIELD_SOURCES];
	int gains[SG_WEAPON_FIELD_SOURCES];
	int bi, best_gain = 0, count = 0, i;
	qboolean same;

	if (!bot || !bot->ent || !SG_Rune())
		return NULL;
	bi = DefenseSupplyBotIndex(bot);
	for (i = 1; i < globals.num_edicts &&
	     count < SG_WEAPON_FIELD_SOURCES; i++)
	{
		edict_t *item = &g_edicts[i];
		int seed;

		if (!item->inuse || !item->classname ||
		    strncmp(item->classname, "item_armor", 10) != 0 ||
		    item->solid == SOLID_NOT || !Caco_ItemBelievedUp(item) ||
		    !G_ArmorPickupEligible(item, bot->ent))
			continue;
		gains[count] = G_ArmorPickupGain(item, bot->ent);
		if (gains[count] <= 0)
			continue;
		seed = Rune_NearestSeed(SG_Rune(), item->s.origin);
		if (seed < 0)
			continue;
		ents[count] = i;
		seeds[count] = seed;
		if (gains[count] > best_gain)
			best_gain = gains[count];
		count++;
	}
	if (count == 0)
	{
		sg_armor_collectible_ready[bi] = 0;
		sg_armor_collectible_count[bi] = 0;
		return NULL;
	}
	for (i = 0; i < count; i++)
		costs[i] = SG_ItemGainSourceCost(gains[i], best_gain);

	same = sg_armor_collectible_ready[bi] &&
	       sg_armor_collectible_epoch[bi] ==
	           sg_fields.action_topology_epoch &&
	       sg_armor_collectible_count[bi] == count;
	for (i = 0; same && i < count; i++)
		if (sg_armor_collectible_ent[bi][i] != ents[i] ||
		    sg_armor_collectible_seed[bi][i] != seeds[i] ||
		    sg_armor_collectible_cost[bi][i] != costs[i])
			same = false;
	if (!same)
	{
		Field_Flood(SG_Rune(), sg_armor_collectible_field[bi], seeds,
		            costs, count);
		for (i = 0; i < count; i++)
		{
			sg_armor_collectible_ent[bi][i] = ents[i];
			sg_armor_collectible_seed[bi][i] = seeds[i];
			sg_armor_collectible_cost[bi][i] = costs[i];
		}
		sg_armor_collectible_count[bi] = count;
		sg_armor_collectible_epoch[bi] =
		    sg_fields.action_topology_epoch;
		sg_armor_collectible_ready[bi] = 1;
	}
	return sg_armor_collectible_field[bi];
}

static qboolean CollectibleArmorTargetCandidate(const sg_bot_t *bot,
	int item_ent, int *seed_out, int *gain_out)
{
	edict_t *item;
	int seed;
	int gain;

	if (!bot || !bot->ent || !SG_Rune() || !seed_out || !gain_out ||
	    item_ent <= 0 || item_ent >= globals.num_edicts)
		return false;
	item = &g_edicts[item_ent];
	if (!item->inuse || !item->classname ||
	    strncmp(item->classname, "item_armor", 10) != 0 ||
	    item->solid == SOLID_NOT || !Caco_ItemBelievedUp(item) ||
	    !G_ArmorPickupEligible(item, bot->ent))
		return false;
	gain = G_ArmorPickupGain(item, bot->ent);
	if (gain <= 0)
		return false;
	seed = Rune_NearestSeed(SG_Rune(), item->s.origin);
	if (seed < 0)
		return false;
	*seed_out = seed;
	*gain_out = gain;
	return true;
}

static const int *CollectibleArmorTargetField(sg_bot_t *bot, int target_ent,
	int target_seed)
{
	int bi;
	int cost = 0;

	if (!bot || !SG_Rune() || target_ent <= 0 ||
	    target_seed < 0 || target_seed >= SG_Rune()->hdr.num_seeds)
		return NULL;
	bi = DefenseSupplyBotIndex(bot);
	if (sg_armor_target_epoch[bi] != sg_fields.action_topology_epoch ||
	    sg_armor_target_ent[bi] != target_ent ||
	    sg_armor_target_seed[bi] != target_seed)
	{
		Field_Flood(SG_Rune(), sg_armor_target_field[bi], &target_seed,
			&cost, 1);
		sg_armor_target_epoch[bi] = sg_fields.action_topology_epoch;
		sg_armor_target_ent[bi] = target_ent;
		sg_armor_target_seed[bi] = target_seed;
	}
	return sg_armor_target_field[bi];
}

const int *SG_CollectibleArmorTargetField(sg_bot_t *bot, int *target_ent_out)
{
	int best_ent = -1;
	int best_seed = -1;
	int best_gain = 0;
	int item_ent;

	if (target_ent_out)
		*target_ent_out = -1;
	if (!bot || !bot->ent || !SG_Rune())
		return NULL;
	for (item_ent = 1; item_ent < globals.num_edicts; item_ent++)
	{
		int gain;
		int seed;

		if (!CollectibleArmorTargetCandidate(bot, item_ent, &seed, &gain))
			continue;
		if (best_ent < 0 || gain > best_gain ||
		    (gain == best_gain && item_ent < best_ent))
		{
			best_ent = item_ent;
			best_seed = seed;
			best_gain = gain;
		}
	}
	if (best_ent < 0)
		return NULL;
	if (target_ent_out)
		*target_ent_out = best_ent;
	return CollectibleArmorTargetField(bot, best_ent, best_seed);
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

		if (!SG_WeaponPickupRouteEligible(item, bot->ent))
			continue;
		seed = Rune_NearestSeed(SG_Rune(), item->s.origin);
		if (seed < 0)
			continue;
		Field_Flood(SG_Rune(), sg_weapon_target_field[bi],
		            &seed, &flood_cost, 1);
		cost = sg_weapon_target_field[bi][bot->seed];
		if (cost < best_cost)
		{
			best_cost = cost;
			best_ent = i;
			best_seed = seed;
		}
	}
	sg_weapon_target_ready[bi] = 0;
	sg_weapon_field_kind[bi] = SG_WEAPON_FIELD_NONE;
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

void SG_StrikeWeaponTargetClear(sg_bot_t *bot)
{
	if (!bot)
		return;
	bot->strike_weapon_target_ent = -1;
	bot->strike_weapon_target_seed = -1;
	VectorClear(bot->strike_weapon_target_org);
}

static qboolean StrikeWeaponFindTarget(sg_bot_t *bot)
{
	int i, bi, best_ent = -1, best_seed = -1;
	int best_cost = SG_FIELD_INF;

	if (!bot || !bot->ent || !SG_Rune() || bot->seed < 0 ||
	    bot->seed >= SG_Rune()->hdr.num_seeds)
		return false;
	bi = DefenseSupplyBotIndex(bot);
	for (i = 1; i < globals.num_edicts; i++)
	{
		edict_t *item = &g_edicts[i];
		int seed, ignored = 0, cost;

		if (!SG_WeaponPickupRouteEligible(item, bot->ent))
			continue;
		seed = Rune_NearestSeed(SG_Rune(), item->s.origin);
		if (seed < 0)
			continue;
		Field_Flood(SG_Rune(), sg_weapon_target_field[bi], &seed,
		            &ignored, 1);
		cost = sg_weapon_target_field[bi][bot->seed];
		if (cost < best_cost ||
		    (cost == best_cost && (best_ent < 0 || i < best_ent)))
		{
			best_cost = cost;
			best_ent = i;
			best_seed = seed;
		}
	}
	/* The scratch buffer contains the last candidate flood, not necessarily
	 * the winner.  Force the selected seed to be reflooded before publication. */
	sg_weapon_target_ready[bi] = 0;
	sg_weapon_field_kind[bi] = SG_WEAPON_FIELD_NONE;
	if (best_ent < 0 || best_cost >= SG_FIELD_INF)
		return false;
	bot->strike_weapon_target_ent = best_ent;
	bot->strike_weapon_target_seed = best_seed;
	VectorCopy(g_edicts[best_ent].s.origin, bot->strike_weapon_target_org);
	return true;
}

const int *SG_StrikeWeaponTargetField(sg_bot_t *bot, int *route_ms)
{
	const int *field;

	if (route_ms)
		*route_ms = -1;
	if (!bot || !SG_Rune() || bot->seed < 0 ||
	    bot->seed >= SG_Rune()->hdr.num_seeds)
		return NULL;
	if (!SG_StrikeWeaponTargetValid(bot))
	{
		SG_StrikeWeaponTargetClear(bot);
		if (!StrikeWeaponFindTarget(bot))
			return NULL;
	}
	field = WeaponTargetField(bot, bot->strike_weapon_target_seed);
	if (!field || field[bot->seed] >= SG_FIELD_INF)
	{
		SG_StrikeWeaponTargetClear(bot);
		return NULL;
	}
	if (route_ms)
		*route_ms = field[bot->seed];
	return field;
}

void SG_StrikeWeaponNoteItemTouch(edict_t *taker, edict_t *item)
{
	int i, item_ent;

	if (!taker || !item)
		return;
	item_ent = (int)(item - g_edicts);
	if (item_ent <= 0 || item_ent >= globals.num_edicts)
		return;
	for (i = 0; i < SG_MAXBOTS; i++)
	{
		sg_bot_t *bot = &sg_bots[i];

		if (bot->active && bot->ent == taker &&
		    bot->strike_weapon_target_ent == item_ent)
		{
			SG_StrikeWeaponTargetClear(bot);
			return;
		}
	}
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

const int *SG_DefenseSupplyTargetField(sg_bot_t *bot)
{
	const int *target_field;
	int target_seed;

	if (!bot || !SG_DefenseSupplyActive(bot) ||
	    bot->def_supply_phase != SG_DEF_SUPPLY_OUTBOUND ||
	    !SG_DefenseSupplyTargetValid(bot))
		return NULL;
	target_seed = bot->def_supply_target_seed;
	target_field = WeaponTargetField(bot, target_seed);
	return target_field;
}

static qboolean DefenseSupplyTargetFieldReachable(const sg_bot_t *bot)
{
	const int *field = SG_DefenseSupplyTargetField((sg_bot_t *)bot);

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
	target_field = SG_DefenseSupplyTargetField(bot);
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

/* Modulate the fitted role row with current combat and objective state. */
void Think_LiveWeights(sg_bot_t *bot, sg_think_t *tc)
{
	edict_t *e = tc->e;
	sg_role_t role = tc->role;
	int team = tc->team;
	sg_weights_t *live = &tc->live;
	sg_combat_weapon_state_t weapon_state;
	qboolean supply_active;

	/* Combine the fitted role row with current inventory and combat demand. */
	SG_CombatWeights(e, Weights_Row(role), live);

	/* A rank-zero watchman may take one bounded weapon-supply sortie. */
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
			step.target_valid = SG_DefenseSupplyTargetValid(bot);
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

		/* The selected live pad owns the outbound route until its deadline. */
		if (DefenseSupplyFindTarget(bot, &target_ent, &target_seed, &route_ms))
		{
			int bi = DefenseSupplyBotIndex(bot);

			bot->def_supply_armed = true;
			bot->def_supply_phase = SG_DEF_SUPPLY_OUTBOUND;
			bot->def_supply_instance = bot->instance_token;
			bot->def_supply_ent = target_ent;
			bot->def_supply_target_seed = target_seed;
			bot->def_supply_route_ms = route_ms;
			sg_weapon_target_cached[bi] = -1;
			sg_weapon_target_epoch[bi] = 0;
			sg_weapon_target_ready[bi] = 0;
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
	/* RF_GLOW proves a rune exists but does not identify its type. */
	{
		int s;

		for (s = 0; s < SG_MAX_ENEMY_TRACK; s++)
		{
			sg_belief_enemy_t *en = &sg_caco_enemies[SG_TeamIdx(team)][s];

			if (en->client >= 0 && en->runed &&
			    SG_AgeUnder(en->seen_time, 15.0f))
			{
				/* A known Damage rune earns the stronger response. */
				live->item[SG_FC_RUNE] *=
				    Caco_EnemyHasDamageRune(team) ? 1.80f : 1.45f;
				if (live->item[SG_FC_RUNE] > 2.0f)
					live->item[SG_FC_RUNE] = 2.0f;
				break;
			}
		}
	}
	/* Item demand keeps the roaming defender moving through nearby supplies. */
	if (role == SG_ROLE_DEFEND && !bot->def_stand)
	{
		if (live->item[SG_FC_ARMOR] < 1.1f)  live->item[SG_FC_ARMOR] = 1.1f;
		if (live->item[SG_FC_HEALTH] < 1.0f) live->item[SG_FC_HEALTH] = 1.0f;
		if (live->item[SG_FC_AMMO] < 1.0f)   live->item[SG_FC_AMMO] = 1.0f;
	}
}

/* Resolve the role objective and its navigation field. */
void Think_Objective(sg_bot_t *bot, sg_think_t *tc)
{
	edict_t *e = tc->e;
	/* Strike ESCORT owns carrier formation. Other duties retain organic role
	 * until the coordinator applies their exact route. */
	sg_role_t role = (sg_role_t)SG_ObjectiveRole(tc->role,
	    tc->escort_mission);
	int team = tc->team;
	qboolean carrying = tc->carrying;
	const int *goal_field;
	const int *route_field;
	qboolean route_pure;
	tc->rune_handoff_route = false;
	tc->mega_target_ent = -1;
	if (role == SG_ROLE_CARRY || role == SG_ROLE_DEFEND)
	{
		goal_field = (team == CTF_TEAM_RED) ? sg_fields.to_red_flag
		                                    : sg_fields.to_blue_flag;

		/* Field mode selects the learned post or intercept field directly. */
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

		/* Start from the ordinary attack objective. Replace it only while the
		 * bot sees the ordered teammate and the target flood reaches this bot. */
		goal_field = ht
		    ? sg_fields.to_flag_now[SG_TeamIdx(team)]
		        [SG_TeamIdx(SG_EnemyTeam(team))]
		    : (sg_fields.our_carrier_valid[SG_TeamIdx(team)]
		        ? sg_fields.our_carrier[SG_TeamIdx(team)]
		        : (team == CTF_TEAM_RED ? sg_fields.to_red_flag
		                                : sg_fields.to_blue_flag));

		/* An escort pursues a dropped enemy flag when no carrier remains. */
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

		/* A named nearby threat selects a route-relative escort station. */
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
					/* Mode 3 selects route-bound lead and nearby trail stations. */
					if (interpose_mode == 3)
					{
						int *cf = (team == CTF_TEAM_RED)
						    ? sg_fields.to_red_flag : sg_fields.to_blue_flag;
						int cc = cf[oc->seed];
						int threat_seed =
						    sg_caco_enemies[SG_TeamIdx(team)][ts].seed;
						int lead = SG_InterposeLeadStation(cc,
						    cf[threat_seed]);
						int wcost = lead ? cc - 1300 : cc + 900;
						int band = 450;
						int cost_lo;

						if (wcost < 0)
							wcost = 0;  /* carrier nearly home: lead collapses to the stand */
						cost_lo = wcost > band ? wcost - band : 0;
						ms = lead
						    ? SG_FieldCarrierLeadStation(SG_Rune(), cf, oc->seed,
						          cost_lo, wcost + band)
						    : SG_FieldCarrierTrailStation(SG_Rune(), cf, oc->seed,
						          cost_lo, wcost + band);
					}
					else if (interpose_mode == 2)
					{
						int *cf = (team == CTF_TEAM_RED)
						    ? sg_fields.to_red_flag : sg_fields.to_blue_flag;
						int cc = cf[oc->seed];
						int want_lo = cc > 2200 ? cc - 2200 : 0;
						int want_hi = cc - 900;

						ms = SG_FieldCarrierLeadStation(SG_Rune(), cf,
						    oc->seed, want_lo, want_hi);
					}

					if (ms < 0)
					{
						VectorAdd(
						    SG_Rune()->seeds[oc->seed].origin,
						    SG_Rune()->seeds[
						        sg_caco_enemies[SG_TeamIdx(team)][ts].seed].origin,
						    mid);
						VectorScale(mid, 0.5f, mid);
						ms = SG_InterposeFallbackSeed(interpose_mode,
						    oc->seed, Rune_NearestSeed(SG_Rune(), mid));
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
		if (ht && ht->inuse && ht->client && !ht->deadflag &&
		    SG_OrderedEscortRouteAllowed(1,
		        SG_CanSee(e, ht->s.origin, ht->viewheight)))
		{
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
		/* Non-escort attackers keep pressure on the enemy stand. */
		if (SG_AttackObjectiveUsesFixedStand(
		        sg_caco_team_belief.carrier[team_index].client))
			goal_field = enemy_index == 0 ? sg_fields.to_red_flag
			                              : sg_fields.to_blue_flag;
		else
			goal_field = sg_fields.to_flag_now[team_index][enemy_index];
	}
	/* Optional policies use the organic role and explicit strike-duty gates. */
	role = tc->role;

	/* A RESIST or REGEN holder may close on a bare carrier for a handoff. */
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
				{
					goal_field = sg_fields.our_carrier[SG_TeamIdx(team)];
					tc->rune_handoff_route = true;
				}
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
	tc->strike_pressure = SG_RuneHandoffEnemyPressure(tc->rune_handoff_route,
	    tc->strike_pressure);
	if (!tc->strike_blocks_optional && SG_RuneHandoffAllowsOptional(
	    tc->rune_handoff_route))
	{
		const int *lead = Lead_Field(bot, role, carrying,
		    SG_ChatOrderedRole(e));

		if (lead)
			goal_field = lead;
	}
	tc->mega = (tc->strike_blocks_optional || !SG_RuneHandoffAllowsOptional(
	    tc->rune_handoff_route)) ? 0.0f : Mega_Worth(bot, e, role);
	if (tc->mega > 0.0f && SG_Rune() && bot->seed >= 0 &&
	    bot->seed < SG_Rune()->hdr.num_seeds)
		Mega_Detour(tc, bot->seed, goal_field, &tc->mega_target_ent);
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
		/* Report the detour when the offer turns on. */
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
		/* A 90-point health jump identifies a mega pickup. */
		if (bot->mega_hp > 0 && e->health - bot->mega_hp >= 90)
			sg_host.dprint("MEGA %s take: hp %d -> %d\n",
			           e->client->pers.netname, bot->mega_hp, e->health);
	}
	bot->mega_on = (tc->mega > 0.0f);
	bot->mega_hp = e->health;

	bot->last_goalcost = (bot->seed >= 0 &&
	                      goal_field[bot->seed] < SG_FIELD_INF)
	                     ? goal_field[bot->seed] : -1;
	route_field = goal_field;
	route_pure = tc->rune_handoff_route;
	/* Tactical waypoint selection runs after the typed strategy instruction
	 * has committed this field in Think_TacticalRoute. */

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

/* Derive a tactical waypoint only from the destination selected by the typed
 * reducer.  Its activation identity replaces the old parallel role latch. */
void Think_TacticalRoute(sg_bot_t *bot, sg_think_t *tc)
{
	static int tac_fields[SG_MAXBOTS][SG_MAX_SEEDS];
	static unsigned tac_field_epoch[SG_MAXBOTS];
	static sg_field_key_t tac_goal[SG_MAXBOTS];
	const int *goal_field;
	int bi;

	if (!bot || !tc || !tc->e || !tc->goal_field || !SG_Rune())
		return;
	goal_field = tc->goal_field;
	tc->route_field = goal_field;
	if (tc->route_pure || !SG_RuneHandoffAllowsOptional(
	        tc->rune_handoff_route) || tc->strike_blocks_optional ||
	    !sg_cv.tactics->value || tc->role == SG_ROLE_ESCORT ||
	    tc->role == SG_ROLE_CARRY || bot->seed < 0 ||
	    goal_field[bot->seed] >= SG_FIELD_INF ||
	    goal_field[bot->seed] < 400)
		return;
	bi = (int)(bot - sg_bots);
	if (bi < 0 || bi >= SG_MAXBOTS)
		return;
	{
		sg_field_key_t goal = SG_FieldKey(SG_Rune(), goal_field);
		sg_tactic_cache_t cache = {
			.topology_current = Fields_ActionTopologyCurrent(tac_field_epoch[bi]),
			.tactic_seed = bot->tac_seed,
			.cached_strategy_activation = bot->tac_strategy_activation,
			.current_strategy_activation = tc->strategy_activation_id,
			.cached_goal = tac_goal[bi], .current_goal = goal,
			.committed_at = bot->tac_time, .now = level.time,
			.route_cost = tac_fields[bi][bot->seed]
		};
		qboolean need;

		sg_route_pure_now = false;
		need = SG_TacticCacheNeedsRefresh(&cache);
		if (need)
		{
			static int g2_field[SG_MAX_SEEDS];
			int s10, best10 = -1, g2 = -1;
			int cur = goal_field[bot->seed];
			float bv10 = 1e30f, gv10 = 1e30f;

			for (s10 = 0; s10 < SG_Rune()->hdr.num_seeds &&
			     s10 < SG_MAX_SEEDS; s10++)
			{
				float sv;

				if (goal_field[s10] >= SG_FIELD_INF ||
				    goal_field[s10] > cur - 2500 ||
				    goal_field[s10] < cur - 4500)
					continue;
				sv = Surface_At(tc, s10, tc->w, goal_field,
				                tc->support, tc->intercept);
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
				sv = Surface_At(tc, s10, tc->w, goal_field,
				                tc->support, tc->intercept);
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
				bot->tac_strategy_activation = tc->strategy_activation_id;
				tac_goal[bi] = goal;
				Field_Flood(SG_Rune(), tac_fields[bi],
				            &bot->tac_seed, &cost10, 1);
				tac_field_epoch[bi] = sg_fields.action_topology_epoch;
				if (sg_cv.debug->value)
					sg_host.dprint("TACTIC %s seed=%d strat=%d\n",
					           tc->e->client->pers.netname,
					           best10, goal_field[best10]);
			}
			else
				bot->tac_seed = -1;
		}
		if (bot->tac_seed >= 0 &&
		    Fields_ActionTopologyCurrent(tac_field_epoch[bi]) &&
		    tac_fields[bi][bot->seed] < SG_FIELD_INF)
		{
			tc->route_field = tac_fields[bi];
			tc->route_pure = true;
		}
	}
}
