/* sg_move.c -- movement policy and command assembly.
 * Owns route steering, action controllers, avoidance, and user commands. */
#include "g_local.h"
#include "g_ctffunc.h"
#include "g_tourney.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_combat.h"
#include "slipgate/sg_chat.h"
#include "slipgate/sg_persona.h"
#include "slipgate/sg_defense_shift.h"
#include "slipgate/sg_defense_supply.h"
#include "slipgate/sg_net.h"
#include "slipgate/sg_cvars.h"
#include "slipgate/sg_util.h"
#include "slipgate/sg_bot.h"
#include "slipgate/sg_compound_drop_game.h"
#include "slipgate/sg_button_live.h"
#include "slipgate/sg_compound_swim_game.h"
#include "slipgate/sg_declared_door_guard.h"
#include "slipgate/sg_rune_binding.h"
#include "slipgate/sg_rune_mechanism_catalog.h"
#include "slipgate/sg_drop_live.h"
#include "slipgate/sg_hook_live.h"
#include "slipgate/sg_swim_live.h"
#include "slipgate/sg_hook_discipline.h"
#include "slipgate/sg_human_speed.h"
#include "slipgate/sg_clock.h"
#include "slipgate/sg_danger.h"
#include "slipgate/sg_weights.h"
#include "slipgate/sg_tilt.h"
#include "slipgate/sg_lead.h"
#include "slipgate/sg_move.h"
#include "slipgate/sg_role_policy.h"
#include "slipgate/sg_route_policy.h"
#include "slipgate/sg_nade_policy.h"
#include "slipgate/sg_feeler_probe.h"
#include "slipgate/sg_weave_policy.h"
#include "slipgate/sg_team_collision.h"
#include "slipgate/sg_traversal_transition.h"
#include "slipgate/sg_rocketjump_game.h"
#include "slipgate/sg_sound_policy.h"
#include "slipgate/sg_price.h"     /* tc->role */
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_strike.h"
#include "slipgate/sg_pickup_target.h"
#include <stdint.h>

void		ClientThink(edict_t *ent, usercmd_t *ucmd);
void		Cmd_Hook_f(edict_t *ent);

static int SG_TelemetryCoordinate(float coordinate)
{
	return (int)nearbyintf(coordinate);
}

/* Sound-directed splash is admitted against the authoritative client roster,
 * not the SG controller array. Humans and bots occupy the same damage space;
 * a human teammate near the heard-only belief is therefore the same veto as
 * an SG teammate. */
static qboolean SoundFireTeammateNear(edict_t *self, int team,
	const vec3_t target, float radius)
{
	int client_index;

	if (!self || !target || !isfinite(radius) || radius <= 0.0f ||
	    (team != CTF_TEAM_RED && team != CTF_TEAM_BLUE))
		return true;
	for (client_index = 1; client_index <= game.maxclients; client_index++)
	{
		edict_t *mate = &g_edicts[client_index];
		vec3_t delta;

		if (mate == self || !mate->inuse || !mate->client ||
		    mate->deadflag || mate->health <= 0 ||
		    mate->client->ctf.teamnum != team)
			continue;
		VectorSubtract(mate->s.origin, target, delta);
		if (VectorLength(delta) < radius)
			return true;
	}
	return false;
}

/* Sound fire deliberately aims at a coarse heard-only region rather than a
 * visible client.  That does not waive ordinary rocket geometry: the exact
 * weapon muzzle must leave the body cleanly, and the first collision must be
 * a useful splash surface near that region rather than a wall at the bot's
 * feet or empty space beyond it. */
static qboolean SoundFireImpactSafe(edict_t *self, int team,
	const vec3_t target)
{
	vec3_t direction, angles, forward, right, offset, muzzle, shot_end;
	vec3_t delta;
	trace_t muzzle_trace, shot_trace;

	if (!self || !self->inuse || !self->client || !target || !sg_host.trace ||
	    !isfinite(target[0]) || !isfinite(target[1]) || !isfinite(target[2]))
		return false;
	VectorSubtract(target, self->s.origin, direction);
	if (VectorNormalize(direction) < 1.0f)
		return false;
	angles[YAW] = atan2f(direction[1], direction[0]) *
	              180.0f / (float)M_PI;
	angles[PITCH] = -asinf(direction[2]) * 180.0f / (float)M_PI;
	angles[ROLL] = 0.0f;
	AngleVectors(angles, forward, right, NULL);
	VectorSet(offset, 8.0f, 8.0f, self->viewheight - 8.0f);
	if (self->client->pers.hand == LEFT_HANDED)
		offset[1] = -offset[1];
	else if (self->client->pers.hand == CENTER_HANDED)
		offset[1] = 0.0f;
	G_ProjectSource(self->s.origin, offset, forward, right, muzzle);
	muzzle_trace = sg_host.trace(self->s.origin, NULL, NULL, muzzle, self,
	                             MASK_SHOT);
	if (muzzle_trace.startsolid || muzzle_trace.allsolid ||
	    muzzle_trace.fraction < 1.0f)
		return false;

	/* The ear's maximum placement uncertainty is 300 units.  Trace through
	 * that complete region: a collision outside it cannot splash the sound
	 * source this policy claims to attack. */
	shot_end[0] = target[0] + 300.0f * forward[0];
	shot_end[1] = target[1] + 300.0f * forward[1];
	shot_end[2] = target[2] + 300.0f * forward[2];
	shot_trace = sg_host.trace(muzzle, NULL, NULL, shot_end, self, MASK_SHOT);
	if (shot_trace.startsolid || shot_trace.allsolid ||
	    shot_trace.fraction >= 1.0f ||
	    (shot_trace.surface && (shot_trace.surface->flags & SURF_SKY)))
		return false;
	if (shot_trace.ent && shot_trace.ent->client &&
	    shot_trace.ent->client->ctf.teamnum == team)
		return false;
	VectorSubtract(shot_trace.endpos, self->s.origin, delta);
	if (VectorLength(delta) < 180.0f)
		return false;
	VectorSubtract(shot_trace.endpos, target, delta);
	if (VectorLength(delta) > 300.0f)
		return false;
	return !SoundFireTeammateNear(self, team, shot_trace.endpos, 250.0f);
}

/* This controller deliberately lives at the command boundary, after combat
 * has supplied the current target and view. It is not navigation: no seed,
 * link, goal, or commitment is ever read as an authority or rewritten. */
static qboolean DefenseCombatEnemyCurrent(edict_t *e, edict_t *enemy, int team)
{
	if (!e || !e->client || !enemy || enemy == e || !enemy->inuse ||
	    !enemy->client || enemy->deadflag || enemy->health <= 0 ||
	    (team != CTF_TEAM_RED && team != CTF_TEAM_BLUE) ||
	    e->client->ctf.teamnum != team ||
	    enemy->client->ctf.teamnum == team ||
	    (enemy->client->ctf.teamnum != CTF_TEAM_RED &&
	     enemy->client->ctf.teamnum != CTF_TEAM_BLUE))
		return false;
	return SG_CombatLiveEnemy(e) == enemy;
}

static qboolean DefenseCombatCandidateSafe(edict_t *e, edict_t *stand,
	const sg_defense_combat_move_t *move)
{
	vec3_t lookahead, floor_start, floor_end, radial;
	trace_t body, floor;
	sg_defense_combat_probe_t probe;
	float distance;
	if (!e || !stand || !move || !sg_host.trace)
		return false;
	VectorCopy(e->s.origin, lookahead);
	lookahead[0] += move->x * 48.0f;
	lookahead[1] += move->y * 48.0f;
	VectorSubtract(lookahead, stand->s.origin, radial);
	radial[2] = 0.0f;
	distance = VectorLength(radial);
	body = sg_host.trace(e->s.origin, e->mins, e->maxs, lookahead, e,
	                     MASK_PLAYERSOLID);
	VectorCopy(lookahead, floor_start);
	VectorCopy(lookahead, floor_end);
	floor_start[2] += 24.0f;
	floor_end[2] -= 48.0f;
	floor = sg_host.trace(floor_start, e->mins, e->maxs, floor_end, e,
	                      MASK_PLAYERSOLID);
	memset(&probe, 0, sizeof(probe));
	probe.body_clear = !body.startsolid && !body.allsolid &&
	    body.fraction >= 1.0f;
	probe.player_clear = !(body.ent && body.ent->client) &&
	    !(floor.ent && floor.ent->client);
	probe.floor_clear = !floor.startsolid && !floor.allsolid &&
	    floor.fraction < 1.0f;
	probe.stand_distance = distance;
	probe.vertical_step = floor.endpos[2] - e->s.origin[2];
	return SG_DefenseCombatProbeAllowed(&probe);
}

static void DefenseCombatZero(usercmd_t *cmd)
{
	if (!cmd)
		return;
	cmd->forwardmove = 0;
	cmd->sidemove = 0;
	cmd->upmove = 0;
}

/* This is intentionally the one admission law for both planning and every
 * sub-step write.  Think_Emit's locals describe this outer frame, but the
 * entity, flag and combat target may change between ClientThink sub-steps. */
static qboolean DefenseCombatAuthority(edict_t *e, sg_bot_t *bot, int team,
	const sg_think_t *tc, qboolean as_ok, edict_t *planned_stand,
	edict_t *planned_enemy, unsigned long planned_enemy_ctfid,
	edict_t **stand_out, edict_t **enemy_out)
{
	edict_t *flag, *stand, *enemy;
	rune_t *rune;
	int bestlink;
	qboolean proved_control;

	if (stand_out)
		*stand_out = NULL;
	if (enemy_out)
		*enemy_out = NULL;
	if (!e || !bot || !e->client || !tc || !sg_cv.defcombat ||
	    !isfinite(sg_cv.defcombat->value) || sg_cv.defcombat->value <= 0.0f ||
	    !tc->hold_post || tc->role != SG_ROLE_DEFEND || !bot->def_stand ||
	    ClientHasFlag(e) != NULL || tc->hook_brake || tc->jump_launch ||
	    tc->door_hold || tc->have_move || as_ok || bot->jump_started ||
	    bot->drop_started || bot->hook_phase ||
	    SG_RocketJumpGameOwns(bot) || bot->nade_phase ||
	    SG_TimerPending(bot->beat_until) || bot->linger_hot ||
	    bot->term_brake < 1.0f || e->health <= 0 || e->deadflag ||
	    !e->groundentity ||
	    (e->groundentity != g_edicts && !SG_ImmutableSupport(e->groundentity)) ||
	    e->movetype != MOVETYPE_WALK || e->waterlevel ||
	    (e->client->ps.pmove.pm_flags & PMF_DUCKED) ||
	    e->client->ps.pmove.pm_time)
		goto denied;

	rune = SG_Rune();
	bestlink = tc->bestlink;
	proved_control = bestlink >= 0 && rune && rune->links &&
	    bestlink < rune->hdr.num_links &&
	    (rune->links[bestlink].action == RL_DROP ||
	     rune->links[bestlink].action == RL_JUMP ||
	     rune->links[bestlink].action == RL_SWIM ||
	     rune->links[bestlink].action == RL_LIFT ||
	     rune->links[bestlink].action == RL_TELEPORT ||
	     rune->links[bestlink].action == RL_DOOR ||
	     rune->links[bestlink].action == RL_BUTTON_DOOR);
	if (proved_control)
		goto denied;
	flag = SG_OwnFlag(team);
	stand = SG_FlagStand(team, true);
	enemy = SG_CombatLiveEnemy(e);
	if (!flag || !ctf_flagathome(flag) || !stand ||
	    !DefenseCombatEnemyCurrent(e, enemy, team) ||
	    (planned_stand && stand != planned_stand) ||
	    (planned_enemy && (enemy != planned_enemy ||
	     enemy->client->ctf.ctfid != planned_enemy_ctfid)))
		goto denied;
	if (stand_out)
		*stand_out = stand;
	if (enemy_out)
		*enemy_out = enemy;
	return true;

denied:
	/* A lease is a current-target preference only.  Never let a rejected B
	 * probe leave A's sign behind for a later reacquisition. */
	SG_DefenseCombatLeaseReset(bot);
	return false;
}

static qboolean DefenseCombatPlan(edict_t *e, sg_bot_t *bot, int team,
	const sg_think_t *tc, qboolean as_ok, qboolean engaged,
	edict_t **stand_out, edict_t **enemy_out, vec3_t move_out)
{
	edict_t *flag, *stand, *enemy;
	sg_defense_combat_request_t request;
	sg_defense_combat_move_t move, tested;
	int enemy_slot;
	int attempt, pass;
	static const float scales[] = { 1.0f, 0.5f };

	if (stand_out)
		*stand_out = NULL;
	if (enemy_out)
		*enemy_out = NULL;
	VectorClear(move_out);
	if (!engaged)
	{
		SG_DefenseCombatLeaseReset(bot);
		return false;
	}
	if (!DefenseCombatAuthority(e, bot, team, tc, as_ok, NULL, NULL, 0,
	    &stand, &enemy))
		return false;
	flag = SG_OwnFlag(team);
	if (!flag || !ctf_flagathome(flag))
	{
		SG_DefenseCombatLeaseReset(bot);
		return false;
	}
	enemy_slot = (int)(enemy - g_edicts);
	if (enemy_slot <= 0)
	{
		SG_DefenseCombatLeaseReset(bot);
		return false;
	}
	if (bot->defcombat_enemy_slot || bot->defcombat_enemy_ctfid ||
	    bot->defcombat_tangent_sign || bot->defcombat_tangent_until)
		if (bot->defcombat_enemy_slot != enemy_slot ||
		    bot->defcombat_enemy_ctfid != enemy->client->ctf.ctfid)
			SG_DefenseCombatLeaseReset(bot);
	memset(&request, 0, sizeof(request));
	request.enabled = 1;
	request.hold_post = 1;
	request.defend_stand = 1;
	request.own_flag_home = 1;
	request.engaged = 1;
	request.live_enemy = 1;
	request.identity_valid = 1;
	request.movement_clear = 1;
	request.self_x = e->s.origin[0];
	request.self_y = e->s.origin[1];
	request.stand_x = stand->s.origin[0];
	request.stand_y = stand->s.origin[1];
	request.enemy_x = enemy->s.origin[0];
	request.enemy_y = enemy->s.origin[1];
	request.camp_scale = SG_PersonaCampScale(e);
	request.identity = (int)(e - g_edicts);
	request.phase = (int)floorf(level.time * 2.0f);
	if (bot->defcombat_enemy_slot == enemy_slot &&
	    bot->defcombat_enemy_ctfid == enemy->client->ctf.ctfid &&
	    bot->defcombat_tangent_until > level.time &&
	    (bot->defcombat_tangent_sign == -1 ||
	     bot->defcombat_tangent_sign == 1))
		request.preferred_tangent_sign = bot->defcombat_tangent_sign;
	/* Keep the two established 48u probes as the first choice.  Only after
	 * both reject do we repeat their exact order at half scale: that encodes a
	 * 24u horizon in the stored world direction and a capped 80 command. */
	for (pass = 0; pass < 2; pass++)
	{
		sg_defense_combat_request_t candidate_request = request;

		for (attempt = 0; attempt < 2; attempt++)
		{
			if (attempt)
			{
				candidate_request.phase++;
				if (candidate_request.preferred_tangent_sign)
					candidate_request.preferred_tangent_sign =
					    -candidate_request.preferred_tangent_sign;
			}
			if (SG_DefenseCombatChoose(&candidate_request, &move))
			{
				tested = move;
				tested.x *= scales[pass];
				tested.y *= scales[pass];
				if (!DefenseCombatCandidateSafe(e, stand, &tested))
					continue;
				/* Persist only a trace-approved direction, and only long enough to
				 * bridge the 0.5s phase tick that previously made the orbit saw. */
				bot->defcombat_enemy_slot = enemy_slot;
				bot->defcombat_enemy_ctfid = enemy->client->ctf.ctfid;
				bot->defcombat_tangent_sign = move.tangent_sign;
				bot->defcombat_tangent_until = level.time + 1.25f;
				move_out[0] = tested.x;
				move_out[1] = tested.y;
				if (enemy_out)
					*enemy_out = enemy;
				if (stand_out)
					*stand_out = stand;
				return true;
			}
		}
	}
	SG_DefenseCombatLeaseReset(bot);
	return false;
}

void SG_DefenseCombatLeaseReset(sg_bot_t *bot)
{
	if (!bot)
		return;
	bot->defcombat_enemy_slot = 0;
	bot->defcombat_enemy_ctfid = 0;
	bot->defcombat_tangent_sign = 0;
	bot->defcombat_tangent_until = 0.0f;
}

/* Posted defense is its own lateral owner. The ordinary duel weave remains
 * useful away from a post, but it must never replace a trace-approved post
 * tangent -- nor create a blind fallback when both tangent probes failed. */
static qboolean DefenseCombatApplyDuelWeave(qboolean hold_post,
	qboolean proved_control, qboolean duel_hold, qboolean engaged,
	qboolean touch_terminal, short weave_side, usercmd_t *cmd)
{
	if (!cmd || hold_post || proved_control || !duel_hold || !engaged ||
	    touch_terminal)
		return false;
	cmd->forwardmove = 0;
	cmd->sidemove = weave_side;
	return true;
}

/* A combat or sound-fire trace authorizes one exact quantized view, not every
 * intermediate angle on the way there.  Compare the command bytes which
 * ClientThink will actually consume; float proximity is not firing authority. */
static qboolean AimedFireViewReady(const usercmd_t *cmd,
	short expected_yaw, short expected_pitch)
{
	return cmd && cmd->angles[YAW] == expected_yaw &&
	       cmd->angles[PITCH] == expected_pitch;
}

/* The final ordinary walking writer for a post tangent. It rechecks the
 * current target and support immediately before the command boundary. */
static qboolean DefenseCombatWriteFinal(edict_t *e, sg_bot_t *bot, int team,
	const sg_think_t *tc, qboolean as_ok, qboolean active, edict_t *stand,
	edict_t *enemy, unsigned long enemy_ctfid, const vec3_t direction,
	usercmd_t *cmd)
{
	vec3_t combat_basis, combat_fwd, combat_right;
	sg_defense_combat_move_t move;
	float flat;

	if (!active || !cmd)
		return false;
	if (!DefenseCombatAuthority(e, bot, team, tc, as_ok, stand, enemy,
	    enemy_ctfid, NULL, NULL))
	{
		DefenseCombatZero(cmd);
		return false;
	}
	memset(&move, 0, sizeof(move));
	move.x = direction[0];
	move.y = direction[1];
	if (!DefenseCombatCandidateSafe(e, stand, &move))
	{
		DefenseCombatZero(cmd);
		return false;
	}
	combat_basis[YAW] = bot->vy_cur;
	combat_basis[PITCH] = bot->vp_cur / 3.0f;
	combat_basis[ROLL] = 0.0f;
	AngleVectors(combat_basis, combat_fwd, combat_right, NULL);
	flat = sqrtf(combat_fwd[0] * combat_fwd[0] +
	             combat_fwd[1] * combat_fwd[1]);
	if (flat <= 0.01f)
	{
		DefenseCombatZero(cmd);
		return false;
	}
	cmd->forwardmove = (short)(160.0f *
	    (direction[0] * combat_fwd[0] + direction[1] * combat_fwd[1]) / flat);
	cmd->sidemove = (short)(160.0f *
	    (direction[0] * combat_right[0] + direction[1] * combat_right[1]));
	cmd->upmove = 0;
	return true;
}

#ifdef SG_DEFENSE_COMBAT_TEST
extern void SG_DefenseCombatTestPostPlan(edict_t *e, sg_bot_t *bot,
	sg_think_t *tc, int mutation_mask);

int SG_DefenseCombatTestAdapter(edict_t *e, sg_bot_t *bot, int team,
	int hold_post, int engaged, int duel_hold, short weave_side,
	int mutation_mask, usercmd_t *cmd)
{
	edict_t *enemy = NULL;
	edict_t *stand = NULL;
	vec3_t direction;
	sg_think_t tc;
	unsigned long enemy_ctfid = 0;
	qboolean final_as_ok = false;
	qboolean active;

	if (!cmd)
		return 0;
	memset(&tc, 0, sizeof(tc));
	tc.e = e;
	tc.team = team;
	tc.role = SG_ROLE_DEFEND;
	tc.hold_post = hold_post != 0;
	VectorClear(direction);
	active = DefenseCombatPlan(e, bot, team, &tc, false, engaged != 0,
	    &stand, &enemy, direction);
	if (enemy && enemy->client)
		enemy_ctfid = enemy->client->ctf.ctfid;
	DefenseCombatApplyDuelWeave(hold_post != 0, false, duel_hold != 0,
	    engaged != 0, false, weave_side, cmd);
	SG_DefenseCombatTestPostPlan(e, bot, &tc, mutation_mask);
	final_as_ok = (mutation_mask & 32768) != 0; /* MUT_AS test seam */
	return DefenseCombatWriteFinal(e, bot, team, &tc, final_as_ok, active, stand,
	    enemy, enemy_ctfid, direction, cmd) ? 1 : 0;
}

int SG_AimedFireViewReadyTest(short actual_yaw, short actual_pitch,
	short expected_yaw, short expected_pitch)
{
	usercmd_t cmd;

	memset(&cmd, 0, sizeof(cmd));
	cmd.angles[YAW] = actual_yaw;
	cmd.angles[PITCH] = actual_pitch;
	return AimedFireViewReady(&cmd, expected_yaw, expected_pitch) ? 1 : 0;
}
#endif

#ifdef SG_SOUND_FIRE_TEST
int SG_SoundFireTestTeammateNear(edict_t *self, int team,
	const vec3_t target, float radius)
{
	return SoundFireTeammateNear(self, team, target, radius) ? 1 : 0;
}

int SG_SoundFireTestImpactSafe(edict_t *self, int team,
	const vec3_t target)
{
	return SoundFireImpactSafe(self, team, target) ? 1 : 0;
}
#endif

/* A dropped flag is private knowledge until it is currently perceived.  Its
 * home position is public CTF state, but neither fact permits a body to cut
 * through a wall or a different floor to reach it. */
static qboolean SG_FlagPerceivable(edict_t *e, edict_t *flag)
{
	vec3_t eye, target;

	if (!e || !flag || !sg_host.in_pvs)
		return false;
	VectorCopy(e->s.origin, eye);
	eye[2] += e->viewheight;
	VectorCopy(flag->s.origin, target);
	target[2] += 16.0f;
	return sg_host.in_pvs(eye, target) &&
	       SG_CanSee(e, flag->s.origin, 16.0f);
}

/* Abandon the graph only for a same-floor, hull-clear flag contact. Home is
 * public; a dropped flag requires sight. */
qboolean SG_AttackFlagDirectTouchAuthority(edict_t *e, int team,
	edict_t **flag_out)
{
	edict_t *flag;
	trace_t body;

	if (flag_out)
		*flag_out = NULL;
	if (!e || !e->client)
		return false;
	flag = SG_EnemyFlag(team);
	if (!SG_FlagApproachAvailableTo(flag, e) ||
	    SG_DistXY(flag->s.origin, e->s.origin) >= 160.0f ||
	    fabsf(flag->s.origin[2] - e->s.origin[2]) > 64.0f)
		return false;
	if (!ctf_flagathome(flag) && !SG_FlagPerceivable(e, flag))
		return false;
	body = sg_host.trace(e->s.origin, e->mins, e->maxs, flag->s.origin, e,
	                     MASK_PLAYERSOLID);
	if (body.startsolid || body.allsolid ||
	    (body.fraction < 1.0f && body.ent != flag))
		return false;
	if (flag_out)
		*flag_out = flag;
	return true;
}

static qboolean SG_OwnDroppedFlagDirectTouchAuthority(edict_t *e, int team,
	edict_t **flag_out)
{
	edict_t *flag;
	trace_t body;

	if (flag_out)
		*flag_out = NULL;
	if (!e || !e->client || !sg_host.trace)
		return false;
	flag = SG_OwnFlag(team);
	if (!SG_FlagApproachAvailableTo(flag, e) || ctf_flagathome(flag) ||
	    SG_DistXY(flag->s.origin, e->s.origin) >= 160.0f ||
	    fabsf(flag->s.origin[2] - e->s.origin[2]) > 64.0f ||
	    !SG_FlagPerceivable(e, flag))
		return false;
	body = sg_host.trace(e->s.origin, e->mins, e->maxs, flag->s.origin, e,
	    MASK_PLAYERSOLID);
	if (body.startsolid || body.allsolid ||
	    (body.fraction < 1.0f && body.ent != flag))
		return false;
	if (flag_out)
		*flag_out = flag;
	return true;
}

/* A home flag is public CTF state, but the carrier earns terminal steering
 * only inside the same physical touch envelope as every other flag contact.
 * This is capture approach authority, not capture authority: ctf_flagtouch
 * remains the only path that scores. */
qboolean SG_OwnHomeFlagDirectTouchAuthority(edict_t *e, int team,
	edict_t **flag_out)
{
	edict_t *flag;
	trace_t body;

	if (flag_out)
		*flag_out = NULL;
	if (!e || !e->client || !sg_host.trace)
		return false;
	flag = SG_OwnFlag(team);
	if (!SG_FlagApproachAvailableTo(flag, e) || !ctf_flagathome(flag) ||
	    SG_DistXY(flag->s.origin, e->s.origin) >= 160.0f ||
	    fabsf(flag->s.origin[2] - e->s.origin[2]) > 64.0f)
		return false;
	body = sg_host.trace(e->s.origin, e->mins, e->maxs, flag->s.origin, e,
	    MASK_PLAYERSOLID);
	if (body.startsolid || body.allsolid ||
	    (body.fraction < 1.0f && body.ent != flag))
		return false;
	if (flag_out)
		*flag_out = flag;
	return true;
}

/* Terminal recovery must end at a local, trace-clear source of the admitted
 * belief field, not at an empty stand or a remote minimum across geometry. */
static int SG_TerminalFieldSeed(const rune_t *rune, const int *field,
	int current_seed)
{
	if (!rune || !rune->seeds || !field || current_seed < 0 ||
	    current_seed >= rune->hdr.num_seeds)
		return -1;
	return Rune_NearestFieldMinimumSeed(rune,
	    rune->seeds[current_seed].origin, field);
}

/*
 * An attacker who has direct-touch authority over the live enemy flag is no
 * longer navigating a graph edge.  The touch belongs to the item entity, so
 * this line deliberately runs THROUGH that entity before a CARRY may route
 * home.
 */
static qboolean SG_AttackFlagTerminalAim(edict_t *e, int team, vec3_t aim,
	edict_t **flag_out)
{
	edict_t *flag;
	vec3_t fd, wend;
	float fl;
	trace_t tr;

	if (flag_out)
		*flag_out = NULL;
	if (!SG_AttackFlagDirectTouchAuthority(e, team, &flag))
		return false;
	if (flag_out)
		*flag_out = flag;
	VectorCopy(flag->s.origin, aim);
	VectorSubtract(flag->s.origin, e->s.origin, fd);
	fd[2] = 0.0f;
	fl = VectorLength(fd);
	if (fl <= 1.0f)
		return true;
	VectorScale(fd, (fl + 150.0f) / fl, fd);
	VectorAdd(e->s.origin, fd, wend);
	wend[2] = flag->s.origin[2] + 16.0f;
	tr = sg_host.trace(flag->s.origin, e->mins, e->maxs, wend, e,
	                   MASK_PLAYERSOLID);
	if (tr.fraction < 1.0f)
		VectorCopy(tr.endpos, wend);
	VectorCopy(wend, aim);
	aim[2] = flag->s.origin[2];
	return true;
}

static void SG_FlagTouchBrake(sg_bot_t *bot, edict_t *e,
	const vec3_t target, qboolean touch_authorized)
{
	vec3_t delta, velocity;
	float distance, speed, alignment = 1.0f;

	if (!bot || !e || !target)
		return;
	VectorSubtract(target, e->s.origin, delta);
	delta[2] = 0.0f;
	distance = VectorLength(delta);
	VectorCopy(e->velocity, velocity);
	velocity[2] = 0.0f;
	speed = VectorLength(velocity);
	if (distance > 1.0f && speed > 0.0f)
		alignment = DotProduct(velocity, delta) / (speed * distance);
	bot->term_brake = SG_StrikeFlagTouchThrottle(
	    touch_authorized ? 1 : 0, distance, speed, alignment);
}

void SG_NadeTargetClear(sg_bot_t *bot)
{
	if (!bot)
		return;
	bot->nade_target_slot = 0;
	bot->nade_target_ctfid = 0;
	bot->nade_target_switch_until = 0.0f;
	bot->nade_target_cook_until = 0.0f;
}

/* Resolve the exact client life which a pre-breach transaction named.  It
 * must remain the combat system's current live enemy too: an old visible
 * body is not permission to finish a cook after combat has lost or replaced
 * that target. */
static edict_t *SG_NadeBoundLiveTarget(edict_t *e, const sg_bot_t *bot)
{
	edict_t *target;
	int team;

	if (!e || !e->client || !bot || bot->nade_target_slot <= 0 ||
	    bot->nade_target_slot > game.maxclients ||
	    bot->nade_target_ctfid == 0)
		return NULL;
	target = g_edicts + bot->nade_target_slot;
	team = e->client->ctf.teamnum;
	if ((team != CTF_TEAM_RED && team != CTF_TEAM_BLUE) ||
	    !target->inuse || !target->client || target->deadflag ||
	    target->health <= 0 ||
	    target->client->ctf.teamnum != SG_EnemyTeam(team) ||
	    target->client->ctf.ctfid != bot->nade_target_ctfid ||
	    SG_CombatLiveEnemy(e) != target)
		return NULL;
	return target;
}

/* Both pre-breach callers enter through this one transaction.  The approach
 * band may have expensive graph cost but it cannot pre-empt a direct item
 * touch; the terminal room follows the same rule.  The target is a current,
 * visible enemy body, never a danger seed or remembered stand coordinate. */
qboolean SG_NadeArmPrebreachLiveEnemy(sg_bot_t *bot, edict_t *e, int team)
{
	static gitem_t *nades;
	edict_t *target;
	vec3_t delta;
	float distance;
	int slot;

	if (!bot || !e || !e->client || bot->nade_phase != 0 ||
	    (team != CTF_TEAM_RED && team != CTF_TEAM_BLUE) ||
	    team != e->client->ctf.teamnum ||
	    SG_AttackFlagDirectTouchAuthority(e, team, NULL))
		return false;
	target = SG_CombatLiveEnemy(e);
	if (!target || !target->inuse || !target->client || target->deadflag ||
	    target->health <= 0 ||
	    target->client->ctf.teamnum != SG_EnemyTeam(team) ||
	    target->client->ctf.ctfid == 0 ||
	    !SG_CanSee(e, target->s.origin, target->viewheight))
		return false;
	slot = (int)(target - g_edicts);
	if (slot <= 0 || slot > game.maxclients)
		return false;
	VectorSubtract(target->s.origin, e->s.origin, delta);
	distance = VectorLength(delta);
	if (!SG_StrikePrebreachGrenadeDistanceAllowed(distance))
		return false;
	if (!nades)
		nades = FindItem("Grenades");
	if (!nades || e->client->pers.inventory[ITEM_INDEX(nades)] <= 0)
		return false;

	/* A new arm starts clean; no deadline or client identity may bleed from a
	 * failed prior switch into this exact weapon transaction. */
	SG_NadeTargetClear(bot);
	VectorCopy(target->s.origin, bot->nade_at);
	nades->use(e, nades);
	bot->nade_phase = 1;
	SG_TimerArm(&bot->nade_until, 0.5f);
	bot->nade_target_slot = slot;
	bot->nade_target_ctfid = target->client->ctf.ctfid;
	bot->nade_target_switch_until = bot->nade_until;
	return true;
}

/* A target binding becomes live only when the exact switch deadline armed by
 * the pre-breach transaction became this cook deadline.  Other grenade users
 * share nade_phase, so an old binding must never attach itself to their throw. */
static qboolean SG_NadeTargetSwitching(const sg_bot_t *bot)
{
	return bot && bot->nade_target_slot > 0 &&
	       bot->nade_target_ctfid != 0 &&
	       bot->nade_target_switch_until > 0.0f &&
	       bot->nade_target_switch_until == bot->nade_until;
}

static qboolean SG_NadeTargetCooking(const sg_bot_t *bot)
{
	return bot && bot->nade_target_slot > 0 &&
	       bot->nade_target_ctfid != 0 &&
	       bot->nade_target_cook_until > 0.0f &&
	       bot->nade_target_cook_until == bot->nade_until;
}

static edict_t *SG_NadeArmedTarget(edict_t *e, const sg_bot_t *bot)
{
	if (!SG_NadeTargetCooking(bot))
		return NULL;
	return SG_NadeBoundLiveTarget(e, bot);
}

static void Hook_DisciplineRetire(edict_t *e, sg_bot_t *bot, int link_index,
	float shelf_seconds, qboolean failure, const char *reason,
	int from_goal, int to_goal);

static void Hook_DiagnosticEmit(void *opaque, const char *line)
{
	(void)opaque;
	if (line)
		sg_host.dprint("%s", line);
}

static void Hook_DiagnosticAnchorQ8(const vec3_t anchor, int32_t out[3])
{
	int axis;

	for (axis = 0; axis < 3; axis++)
	{
		double scaled = (double)anchor[axis] * 8.0;

		if (!isfinite(scaled) || scaled < (double)INT32_MIN ||
		    scaled > (double)INT32_MAX)
			out[axis] = 0;
		else
			out[axis] = (int32_t)lround(scaled);
	}
}

static void Hook_DiagnosticBegin(sg_bot_t *bot, sg_role_t role)
{
	int32_t anchor_q8[3];
	const char *bot_name;

	if (!bot)
		return;
	Hook_DiagnosticAnchorQ8(bot->hook_anchor, anchor_q8);
	bot_name = bot->ent && bot->ent->client ?
	    bot->ent->client->pers.netname : "-";
	(void)SG_HookDiagnosticsBegin(&bot->hook_diagnostics,
	    sg_cv.debug && sg_cv.debug->value != 0.0f,
	    bot->speedhook ? SG_HOOK_DIAGNOSTIC_SPEED : SG_HOOK_DIAGNOSTIC_GRAPH,
	    bot->speedhook ? -1 : bot->hook_link, (int)role, bot_name,
	    SG_RuneMapName(), anchor_q8, bot->instance_token,
	    Hook_DiagnosticEmit, NULL);
}

static sg_bot_t *HumanSpeed_BotForEntity(edict_t *entity)
{
	int index;

	if (!entity)
		return NULL;
	for (index = 0; index < SG_MAXBOTS; index++)
		if (sg_bots[index].active && sg_bots[index].ent == entity)
			return &sg_bots[index];
	return NULL;
}

/* Consume the one-command ownership marker even when ClientThink exits before
 * Pmove (pause, intermission, observer state).  No later command can inherit
 * fractional landing time from a command it did not own. */
void SG_HumanSpeedClientThinkBegin(edict_t *entity)
{
	sg_bot_t *bot = HumanSpeed_BotForEntity(entity);
	qboolean skipped_owned_command;
	qboolean owned;

	if (!bot)
		return;
	skipped_owned_command = bot->as_landing_pending;
	owned = bot->as_landing_command;
	bot->as_landing_command = false;
	/* A still-pending marker means the preceding owned ClientThink returned
	 * before Pmove.  Even another chain command cannot bridge that missing
	 * physics step with the old fractional timer remainder. */
	SG_HumanSpeedCommandBoundary(&bot->as_landing,
	    owned && !skipped_owned_command);
	bot->as_landing_pending = owned;
}

void SG_HumanSpeedPmoveBegin(edict_t *entity, pmove_state_t *pmove,
	unsigned command_msec)
{
	sg_bot_t *bot = HumanSpeed_BotForEntity(entity);

	if (!bot)
		return;
	if (!bot->as_landing_pending)
	{
		SG_HumanSpeedTimerReset(&bot->as_landing);
		return;
	}
	bot->as_landing_flags_before = pmove ? pmove->pm_flags : 0;
	bot->as_landing_before = SG_HumanSpeedLandingPrepare(&bot->as_landing,
	    pmove, command_msec, true);
}

void SG_HumanSpeedPmoveEnd(edict_t *entity, const pmove_state_t *pmove,
	unsigned command_msec)
{
	sg_bot_t *bot = HumanSpeed_BotForEntity(entity);
	sg_human_speed_step_t after;

	if (!bot)
		return;
	if (!bot->as_landing_pending)
	{
		SG_HumanSpeedTimerReset(&bot->as_landing);
		return;
	}
	after = SG_HumanSpeedLandingObserve(&bot->as_landing,
	    bot->as_landing_flags_before, pmove, command_msec, true);
	if (sg_cv.debug && sg_cv.debug->value && entity->client &&
	    (bot->as_landing_before.extra_ticks ||
	     bot->as_landing_before.expired || after.began))
		sg_host.dprint("ASSTEP %s ms=%u extra=%u rem=%u "
		    "begin=%d expired=%d timer=%u\n",
		    entity->client->pers.netname, command_msec,
		    bot->as_landing_before.extra_ticks,
		    after.began ? after.remainder_ms :
		        bot->as_landing_before.remainder_ms,
		    after.began ? 1 : 0,
		    bot->as_landing_before.expired ? 1 : 0,
		    pmove ? (unsigned)pmove->pm_time : 0u);
	bot->as_landing_pending = false;
	bot->as_landing_flags_before = 0;
	memset(&bot->as_landing_before, 0, sizeof(bot->as_landing_before));
}

static int sg_hook_reproof_frame = -1;
static int sg_hook_reproof_slot = 0;
static int sg_swim_reproof_frame = -1;
static int sg_swim_reproof_slot = 0;
static uint32_t sg_mechanism_dispatch_depth;
static sg_button_callback_token_t sg_button_callbacks[MAX_EDICTS];
static int DoorStep_EdictKey(const edict_t *entity);

typedef struct door_step_approach_command_s
{
	sg_bot_t *bot;
	unsigned long long bot_instance;
	int frame;
	int substep;
	qboolean active;
} door_step_approach_command_t;

static door_step_approach_command_t sg_door_approach_command;

_Static_assert(SG_DOOR_APPROACH_MAX_MASTERS ==
	SG_RUNE_BINDING_MAX_MOVERS,
	"door approach ticket must cover every sealed mover");

static void DoorStep_ApproachTicketClear(sg_bot_t *bot)
{
	if (bot)
	{
		if (sg_door_approach_command.active &&
		    sg_door_approach_command.bot == bot)
			memset(&sg_door_approach_command, 0,
			    sizeof(sg_door_approach_command));
		memset(&bot->declared_door_ticket, 0,
		    sizeof(bot->declared_door_ticket));
	}
}

/* Once an authenticated command has run, any callback or post-command
 * mismatch permanently poisons this reducer instance.  Clearing only the
 * ephemeral ticket would let an unchanged zero-input/finalizer pose mint new
 * authority on the next frame.  The durable guard and frozen mechanism
 * identity intentionally remain intact for the normal retain/death cleanup. */
static void DoorStep_ApproachCommandFail(sg_bot_t *bot)
{
	if (bot)
		bot->declared_door_approach.phase = SG_DOOR_APPROACH_FAILED;
	DoorStep_ApproachTicketClear(bot);
}

void SG_DeclaredDoorApproachCommandClear(sg_bot_t *bot)
{
	DoorStep_ApproachTicketClear(bot);
}

_Static_assert(MAX_EDICTS == SG_BUTTON_CALLBACK_SOURCE_CAPACITY,
	"button callback source table must cover every protocol edict key");

void SG_ButtonExecutionLevelReset(void)
{
	memset(sg_button_callbacks, 0, sizeof(sg_button_callbacks));
	memset(&sg_door_approach_command, 0,
	    sizeof(sg_door_approach_command));
}

void SG_ButtonExecutionEntityFreed(edict_t *entity)
{
	int key = DoorStep_EdictKey(entity);

	if (key > 0)
		SG_ButtonCallbackTokenReset(&sg_button_callbacks[key]);
}

void SG_ButtonExecutionActionReset(sg_bot_t *bot)
{
	if (!bot)
		return;
	bot->declared_button_latched = false;
	bot->declared_button_rider = false;
	memset(bot->declared_button_start_q8, 0,
		sizeof(bot->declared_button_start_q8));
	memset(bot->declared_button_end_q8, 0,
		sizeof(bot->declared_button_end_q8));
	SG_DoorApproachReset(&bot->declared_door_approach);
	memset(&bot->declared_door_approach_identity, 0,
	    sizeof(bot->declared_door_approach_identity));
	DoorStep_ApproachTicketClear(bot);
}

void SG_DeclaredDoorTerminalDeath(sg_bot_t *bot)
{
	edict_t *ent;
	extern int meansOfDeath;

	if (!bot)
		return;
	SG_ButtonExecutionActionReset(bot);
	/* This control terminal is a lifecycle boundary too.  Retire a pending
	 * pre-breach identity before the death edge or a recycled command frame
	 * can observe it. */
	bot->nade_phase = 0;
	SG_NadeTargetClear(bot);
	if (!(ent = bot->ent) || !ent->inuse || !ent->client ||
	    (ent->deadflag && ent->solid == SOLID_NOT))
		return;
	/* On a first death, player_die begins by transferring the live mover claim
	 * into the guarded corpse lifecycle.  It is also intentionally re-entrant
	 * for damage to an existing corpse: if later entity physics moved that
	 * SOLID_BBOX corpse into a live claim/retirement, the same stock gib branch
	 * makes it nonsolid before the next mover activation. */
	ent->flags &= ~FL_GODMODE;
	ent->health = -100;
	meansOfDeath = MOD_SUICIDE;
	player_die(ent, ent, ent, 100000, vec3_origin);
}

static qboolean DoorStep_AbortDeclared(sg_bot_t *bot, int link_index)
{
	int b, oldest = 0;

	if (!bot || SG_DeclaredDoorGuardReleaseProvedClear(bot) !=
	    SG_COMPOUND_GUARD_OK)
		return false;

	/* This is live interference, not evidence that the serialized link is
	 * false.  Shelf it briefly so ordinary localization can settle without
	 * teaching permanent futility from an opponent's knockback. */
	for (b = 0; b < SG_BL_MAX; b++)
		if (bot->bl_until[b] < bot->bl_until[oldest])
			oldest = b;
	bot->bl_link[oldest] = link_index;
	SG_TimerArm(&bot->bl_until[oldest], 2.0f);
	bot->commit_link = -1;
	bot->commit_until = 0.0f;
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
	return true;
}

/* Release can fail because this body or another live SG body still occupies
 * the captured sweep, or because current declaration authority drifted.  In
 * every case retain the logical claim, renew only an exactly captured TOP set,
 * and bound compatible recovery before taking the normal guarded death path. */
static void DoorStep_RetainDeclared(sg_bot_t *bot)
{
	if (!bot)
		return;
	DoorStep_ApproachTicketClear(bot);
	if (bot->declared_door_recovery_since == 0.0f)
		SG_Mark(&bot->declared_door_recovery_since);
	else if (SG_AgeAtLeast(bot->declared_door_recovery_since, 5.0f))
	{
		SG_DeclaredDoorTerminalDeath(bot);
		return;
	}
	if (!bot->declared_guard_paused)
	{
		bot->declared_guard_paused = true;
		bot->declared_guard_pause_started = level.time;
	}
	if (SG_DeclaredDoorGuardHoldOpen(bot, 500) ==
	    SG_COMPOUND_GUARD_OK)
		(void)SG_DeclaredDoorGuardPause(bot);
	else
		SG_DeclaredDoorTerminalDeath(bot);
}

void SG_DeclaredDoorApproachExecutionRetain(sg_bot_t *bot)
{
	DoorStep_RetainDeclared(bot);
}

static qboolean DoorStep_AbortOrRetain(sg_bot_t *bot, int link_index)
{
	if (DoorStep_AbortDeclared(bot, link_index))
		return true;
	DoorStep_RetainDeclared(bot);
	return false;
}

/* Reauthorization can fail precisely because rune or activator identity
 * drifted.  Release only after the guard positively proves every live SG body
 * clear; otherwise the same physical retention law applies as any failed
 * action retirement. */
static void DoorStep_RetainFailedAuthority(sg_bot_t *bot, int link_index)
{
	(void)DoorStep_AbortOrRetain(bot, link_index);
}

/* A failed preflight most commonly means projectile knockback would carry the
 * next real Pmove into the door sweep.  Zero both copies ClientThink uses and
 * make old_pmove describe that same authoritative fixed-point state; otherwise
 * the supposedly safe tail would replay the rejected velocity (or introduce a
 * spurious snapinitial disagreement) after the declaration was retired. */
static void DoorStep_StopOutside(edict_t *e)
{
	int axis;

	VectorClear(e->velocity);
	for (axis = 0; axis < 3; axis++)
	{
		e->client->ps.pmove.origin[axis] =
		    (short)(e->s.origin[axis] * 8.0f);
		e->client->ps.pmove.velocity[axis] = 0;
	}
	e->client->old_pmove = e->client->ps.pmove;
}

static sg_bot_t *DoorStep_EventBot(edict_t *activator)
{
	int i;

	if (!activator || !activator->inuse || !activator->client ||
	    !SG_OwnsBot(activator))
		return NULL;
	for (i = 0; i < SG_MAXBOTS; i++)
		if (sg_bots[i].active && sg_bots[i].ent == activator)
			return &sg_bots[i];
	return NULL;
}

/* Pointer subtraction is undefined for an untrusted callback argument.  The
 * host array is small and this boundary runs only on a physical mechanism
 * callback, so use bounded identity comparison just like the mover guard. */
static int DoorStep_EdictKey(const edict_t *entity)
{
	int key;

	if (!entity || !g_edicts || globals.edicts != g_edicts ||
	    globals.edict_size != (int)sizeof(edict_t) || globals.num_edicts <= 1 ||
	    globals.num_edicts > MAX_EDICTS || game.maxentities <= 1 ||
	    game.maxentities > MAX_EDICTS ||
	    globals.num_edicts > game.maxentities)
		return 0;
	for (key = 1; key < globals.num_edicts; key++)
		if (entity == &g_edicts[key])
			return entity->inuse && entity->s.number == key ? key : 0;
	return 0;
}

static qboolean DoorStep_SGProvenanceActivator(const edict_t *activator)
{
	return activator && activator->client && (activator->flags & FL_BOT);
}

static qboolean DoorStep_UnownedBotActivator(edict_t *activator)
{
	return activator && activator->inuse && activator->client &&
	       (activator->flags & FL_BOT) && !SG_OwnsBot(activator);
}

static sg_button_callback_token_t *DoorStep_ButtonToken(edict_t *source,
	int *source_key_out)
{
	int source_key = DoorStep_EdictKey(source);

	if (source_key_out)
		*source_key_out = source_key;
	return source_key > 0 ? &sg_button_callbacks[source_key] : NULL;
}

static qboolean DoorStep_ButtonOrdinaryEvent(
	sg_button_callback_token_t *token, uint32_t source_key)
{
	return SG_ButtonCallbackTokenOrdinaryEvent(token, source_key)
	    ? true : false;
}

static qboolean DoorStep_VectorQ8Exact(const vec3_t value, short fixed[3])
{
	int axis;

	if (!value)
		return false;
	for (axis = 0; axis < 3; axis++)
	{
		double scaled = (double)value[axis] * 8.0;
		long rounded;

		if (!isfinite(scaled) || scaled < SHRT_MIN || scaled > SHRT_MAX)
			return false;
		rounded = lround(scaled);
		if (scaled != (double)rounded)
			return false;
		if (fixed)
			fixed[axis] = (short)rounded;
	}
	return true;
}

static qboolean DoorStep_ButtonTransactionCurrent(
	const sg_rune_mechanism_binding_t *binding, const sg_bot_t *bot)
{
	const sg_button_callback_token_t *token;
	int activator_key;

	if (!binding || !binding->entry_node || !bot ||
	    binding->entry_node->key == 0U ||
	    binding->entry_node->key >= MAX_EDICTS)
		return false;
	token = &sg_button_callbacks[binding->entry_node->key];
	activator_key = DoorStep_EdictKey(bot->ent);
	return bot->declared_triggered ||
	       (binding->entry_entity->activator == bot->ent &&
	        SG_ButtonCallbackTokenMatchesPending(token,
	            binding->entry_node->key, activator_key, bot->commit_link,
	            &bot->compound_guard.owner, &bot->compound_guard.ticket));
}

static qboolean DoorStep_ButtonPointOnSegment(const short point[3],
	const short start[3], const short end[3])
{
	int64_t dx = (int32_t)end[0] - start[0];
	int64_t dy = (int32_t)end[1] - start[1];
	int64_t dz = (int32_t)end[2] - start[2];
	int64_t px = (int32_t)point[0] - start[0];
	int64_t py = (int32_t)point[1] - start[1];
	int64_t pz = (int32_t)point[2] - start[2];
	int64_t length2 = dx * dx + dy * dy + dz * dz;
	int64_t dot;

	if (length2 <= 0)
		return false;
	/* All inputs are exact signed q8 int16 values.  Literal integer
	 * collinearity avoids a world-scale floating envelope that could admit a
	 * distinct lattice point on a long button travel. */
	if (py * dz - pz * dy != 0 ||
	    pz * dx - px * dz != 0 ||
	    px * dy - py * dx != 0)
		return false;
	dot = px * dx + py * dy + pz * dz;
	return dot >= 0 && dot <= length2;
}

sg_button_execution_anchor_state_t SG_ButtonExecutionAnchor(
	const sg_rune_mechanism_binding_t *binding, const sg_bot_t *bot,
	const edict_t *subject, const vec3_t bottom_anchor,
	const vec3_t serialized_displacement, int serialized_mode,
	vec3_t effective_anchor)
{
	edict_t *button;
	vec3_t start, end, current, displacement;
	short start_q8[3], end_q8[3], current_q8[3], displacement_q8[3];
	short anchor_q8[3], effective_q8[3];
	int axis;
	sg_button_execution_anchor_state_t state;

	if (effective_anchor)
		VectorClear(effective_anchor);
	if (!binding || !bot || !subject || !bottom_anchor ||
	    !serialized_displacement || !effective_anchor || !binding->plan ||
	    !binding->entry_node || !(button = binding->entry_entity) ||
	    subject != bot->ent || !subject->inuse ||
	    binding->entry_node->key == 0U ||
	    binding->entry_node->key >= MAX_EDICTS ||
	    binding->plan->controller_kind !=
	        SG_MECHANISM_CONTROLLER_BUTTON_DOOR ||
	    DoorStep_EdictKey(button) != (int)binding->entry_node->key ||
	    !SG_RuneMechanismBindingCurrent(binding) ||
	    !bot->declared_touched || !bot->declared_button_latched ||
	    !DoorStep_ButtonTransactionCurrent(binding, bot) ||
	    (serialized_mode != RLCM_PREOPEN && serialized_mode != RLCM_RIDE) ||
	    (bot->declared_button_rider != (serialized_mode == RLCM_RIDE)) ||
	    !DoorStep_VectorQ8Exact(bottom_anchor, anchor_q8) ||
	    !DoorStep_VectorQ8Exact(serialized_displacement, displacement_q8) ||
	    !DoorStep_VectorQ8Exact(button->moveinfo.start_origin, start_q8) ||
	    !DoorStep_VectorQ8Exact(button->moveinfo.end_origin, end_q8) ||
	    !DoorStep_VectorQ8Exact(button->s.origin, current_q8))
		return SG_BUTTON_EXECUTION_ANCHOR_INVALID;
	for (axis = 0; axis < 3; axis++)
	{
		if (start_q8[axis] != bot->declared_button_start_q8[axis] ||
		    end_q8[axis] != bot->declared_button_end_q8[axis] ||
		    end_q8[axis] - start_q8[axis] != displacement_q8[axis])
			return SG_BUTTON_EXECUTION_ANCHOR_INVALID;
		start[axis] = start_q8[axis] * 0.125f;
		end[axis] = end_q8[axis] * 0.125f;
		current[axis] = current_q8[axis] * 0.125f;
		displacement[axis] = current[axis] - start[axis];
	}
	if (VectorCompare(start, end))
		return SG_BUTTON_EXECUTION_ANCHOR_INVALID;
	switch (button->moveinfo.state)
	{
	case SG_PLAT_STATE_BOTTOM:
		if (!VectorCompare(current, start) ||
		    !VectorCompare(button->velocity, vec3_origin) ||
		    !VectorCompare(button->avelocity, vec3_origin))
			return SG_BUTTON_EXECUTION_ANCHOR_INVALID;
		state = SG_BUTTON_EXECUTION_ANCHOR_BOTTOM;
		break;
	case SG_PLAT_STATE_TOP:
		if (!VectorCompare(current, end) ||
		    !VectorCompare(button->velocity, vec3_origin) ||
		    !VectorCompare(button->avelocity, vec3_origin))
			return SG_BUTTON_EXECUTION_ANCHOR_INVALID;
		state = SG_BUTTON_EXECUTION_ANCHOR_TOP;
		break;
	case SG_PLAT_STATE_UP:
	case SG_PLAT_STATE_DOWN:
		if (!DoorStep_ButtonPointOnSegment(current_q8, start_q8, end_q8))
			return SG_BUTTON_EXECUTION_ANCHOR_INVALID;
		state = SG_BUTTON_EXECUTION_ANCHOR_MOVING;
		break;
	default:
		return SG_BUTTON_EXECUTION_ANCHOR_INVALID;
	}
	for (axis = 0; axis < 3; axis++)
		effective_anchor[axis] = bottom_anchor[axis] +
		    (bot->declared_button_rider ? displacement[axis] : 0.0f);
	if (!DoorStep_VectorQ8Exact(effective_anchor, effective_q8))
	{
		VectorClear(effective_anchor);
		return SG_BUTTON_EXECUTION_ANCHOR_INVALID;
	}
	return state;
}

qboolean SG_ButtonExecutionSupportValid(
	const sg_rune_mechanism_binding_t *binding, const sg_bot_t *bot,
	const edict_t *subject)
{
	edict_t *support;
	int ordinary;
	int button;
	int current;
	int entry;
	int rider;
	int transaction;

	if (!binding || !bot || !subject || !binding->plan ||
	    !binding->entry_node || !binding->entry_entity ||
	    binding->entry_node->key == 0U ||
	    binding->entry_node->key >= MAX_EDICTS)
		return false;
	support = subject->groundentity;
	ordinary = support &&
	    (support == g_edicts || SG_ImmutableSupport(support));
	button = binding->plan->controller_kind ==
	    SG_MECHANISM_CONTROLLER_BUTTON_DOOR;
	current = button &&
	    DoorStep_EdictKey(binding->entry_entity) ==
	        (int)binding->entry_node->key &&
	    SG_RuneMechanismBindingCurrent(binding);
	entry = current && support == binding->entry_entity;
	rider = current && !support &&
	    SG_LiftRider(binding->entry_entity, (edict_t *)subject);
	if (!button)
		return ordinary;
	/* Before the first physical touch, both serialized support modes approach
	 * from ordinary static ground.  Once latched, the wire mode is exact:
	 * STATIC may not borrow the entry pusher, and RIDER may not fall through to
	 * an unrelated world floor. */
	if (!bot->declared_touched)
		return ordinary;
	transaction = current && DoorStep_ButtonTransactionCurrent(binding, bot);
	if (!current || !transaction || !binding->link ||
	    !bot->declared_button_latched)
		return false;
	if (binding->link->mode == RLCM_PREOPEN)
		return !bot->declared_button_rider && ordinary;
	if (binding->link->mode == RLCM_RIDE)
		return bot->declared_button_rider && (entry || rider);
	return false;
}

static qboolean DoorStep_DeclaredBindingForLink(int link_index,
	qboolean owned_execution, sg_rune_mechanism_binding_t *binding_out)
{
	rune_t *rune;

	if (binding_out)
		memset(binding_out, 0, sizeof(*binding_out));
	if (!binding_out || link_index < 0 || !(rune = SG_Rune()) ||
	    !SG_RunePhysicsCompatible(rune) ||
	    !(owned_execution
	        ? SG_RuneMechanismBindingCaptureOwned(rune,
	              (uint32_t)link_index, binding_out)
	        : SG_RuneMechanismBindingCapture(rune,
	              (uint32_t)link_index, binding_out)) ||
	    !SG_RuneMechanismBindingDoorAction(binding_out))
		return false;
	return true;
}

static qboolean DoorStep_DeclaredBinding(const sg_bot_t *bot,
	sg_rune_mechanism_binding_t *binding_out)
{
	return bot && DoorStep_DeclaredBindingForLink(bot->commit_link,
	    bot->declared_started, binding_out);
}

static qboolean DoorStep_BindingContainsMover(
	const sg_rune_mechanism_binding_t *binding, const edict_t *entity)
{
	uint32_t keys[SG_RUNE_BINDING_MAX_MOVERS];
	size_t count;
	size_t index;

	if (!binding || !entity ||
	    !SG_RuneMechanismBindingMoverKeys(binding, keys, &count))
		return false;
	for (index = 0U; index < count; index++)
		if (SG_RuneMechanismBindingResolveNode(binding, keys[index]) == entity)
			return SG_RuneMechanismBindingCurrent(binding) ? true : false;
	return false;
}

/* A direct trigger can synchronously target several independent door teams.
 * Seal the unique canonical masters from the binding's authenticated mover
 * closure; team slaves never become extra callback authority. */
static qboolean DoorStep_BindingActivationMasters(
	const sg_rune_mechanism_binding_t *binding,
	uint32_t masters_out[SG_DOOR_APPROACH_MAX_MASTERS],
	size_t *master_count_out)
{
	uint32_t mover_keys[SG_RUNE_BINDING_MAX_MOVERS];
	size_t mover_count = 0U;
	size_t master_count = 0U;
	size_t index;

	if (master_count_out)
		*master_count_out = 0U;
	if (!binding || !masters_out || !master_count_out ||
	    !SG_RuneMechanismBindingMoverKeys(binding, mover_keys, &mover_count) ||
	    mover_count == 0U || mover_count > SG_DOOR_APPROACH_MAX_MASTERS)
		return false;
	memset(masters_out, 0,
	    SG_DOOR_APPROACH_MAX_MASTERS * sizeof(masters_out[0]));
	for (index = 0U; index < mover_count; index++)
	{
		edict_t *member = SG_RuneMechanismBindingResolveNode(binding,
		    mover_keys[index]);
		edict_t *master;
		uint32_t master_key;
		size_t member_index;
		size_t insert;

		if (!member || !(master = member->teammaster
		        ? member->teammaster : member) ||
		    (master_key = (uint32_t)DoorStep_EdictKey(master)) == 0U)
			return false;
		for (member_index = 0U; member_index < mover_count; member_index++)
			if (mover_keys[member_index] == master_key)
				break;
		if (member_index == mover_count)
			return false;
		for (insert = 0U; insert < master_count; insert++)
			if (masters_out[insert] >= master_key)
				break;
		if (insert < master_count && masters_out[insert] == master_key)
			continue;
		if (master_count >= SG_DOOR_APPROACH_MAX_MASTERS)
			return false;
		memmove(&masters_out[insert + 1U], &masters_out[insert],
		    (master_count - insert) * sizeof(masters_out[0]));
		masters_out[insert] = master_key;
		master_count++;
	}
	if (master_count == 0U)
		return false;
	*master_count_out = master_count;
	return SG_RuneMechanismBindingCurrent(binding) ? true : false;
}

static qboolean DoorStep_ApproachTicketMastersCurrent(
	const sg_rune_mechanism_binding_t *binding,
	const sg_door_approach_ticket_t *ticket)
{
	uint32_t keys[SG_DOOR_APPROACH_MAX_MASTERS];
	uint32_t full_mask;
	size_t count = 0U;

	if (!binding || !ticket || ticket->activation_master_count == 0U ||
	    ticket->activation_master_count > SG_DOOR_APPROACH_MAX_MASTERS ||
	    !DoorStep_BindingActivationMasters(binding, keys, &count) ||
	    count != ticket->activation_master_count ||
	    memcmp(keys, ticket->activation_master_keys,
	        count * sizeof(keys[0])) != 0)
		return false;
	full_mask = (UINT32_C(1) << ticket->activation_master_count) -
	    UINT32_C(1);
	return (ticket->activation_master_seen_mask & ~full_mask) == 0U;
}

static qboolean DoorStep_ApproachTicketMasterBit(
	const sg_door_approach_ticket_t *ticket, edict_t *door_master,
	uint32_t *bit_out)
{
	edict_t *canonical;
	uint32_t key;
	uint32_t index;

	if (bit_out)
		*bit_out = 0U;
	if (!ticket || !door_master || !bit_out ||
	    ticket->activation_master_count == 0U ||
	    ticket->activation_master_count > SG_DOOR_APPROACH_MAX_MASTERS)
		return false;
	canonical = door_master->teammaster
	    ? door_master->teammaster : door_master;
	if (canonical != door_master ||
	    (key = (uint32_t)DoorStep_EdictKey(canonical)) == 0U)
		return false;
	for (index = 0U; index < ticket->activation_master_count; index++)
		if (ticket->activation_master_keys[index] == key)
		{
			*bit_out = UINT32_C(1) << index;
			return true;
		}
	return false;
}

static qboolean DoorStep_DeclaredClaimHeld(sg_bot_t *bot)
{
	sg_mover_lease_record_t record;
	qboolean local_door;
	sg_rune_mechanism_binding_t binding;

	if (!bot)
		return false;
	local_door = bot->declared_started &&
	    DoorStep_DeclaredBinding(bot, &binding);
	if (bot->declared_guard_paused || local_door)
		return true;
	/* A malformed live source can be the very drift that made the ordinary
	 * resolver fail.  Preserve unsupported passthrough only when no durable
	 * active/paused transaction exists here; every unsupported callback also
	 * applies the process-wide claim/retirement gate below. */
	(void)SG_CompoundGuardValidate(&bot->compound_guard, &record);
	return record.law == SG_MOVER_LAW_DECLARED_DOOR &&
	       (record.state == SG_MOVER_LEASE_ACTIVE ||
	        record.state == SG_MOVER_LEASE_PAUSED);
}

static int DoorStep_BotSlot(const sg_bot_t *bot)
{
	int slot;

	if (!bot)
		return -1;
	for (slot = 0; slot < SG_MAXBOTS; slot++)
		if (bot == &sg_bots[slot])
			return slot;
	return -1;
}

static qboolean DoorStep_GuardTicketEqual(const sg_mover_ticket_t *left,
	const sg_mover_ticket_t *right)
{
	return left && right && left->epoch == right->epoch &&
	       left->serial == right->serial && left->slot == right->slot;
}

static qboolean DoorStep_StaticSupportSignature(edict_t *support,
	uint32_t *key_out, uint32_t *generation_out)
{
	if (key_out)
		*key_out = 0U;
	if (generation_out)
		*generation_out = 0U;
	if (!support || !key_out || !generation_out)
		return false;
	if (support == g_edicts)
		return true;
	if (!SG_ImmutableSupport(support))
		return false;
	return SG_MechCatalogEntityGeneration(support, key_out,
	    generation_out) ? true : false;
}

static qboolean DoorStep_LivePmoveState(const edict_t *entity,
	pmove_state_t *pms_out)
{
	short origin_q8[3], velocity_q8[3];
	int axis;

	if (pms_out)
		memset(pms_out, 0, sizeof(*pms_out));
	if (!entity || !entity->inuse || !entity->client || !pms_out ||
	    !sv_gravity || !DoorStep_VectorQ8Exact(entity->s.origin, origin_q8) ||
	    !DoorStep_VectorQ8Exact(entity->velocity, velocity_q8))
		return false;
	*pms_out = entity->client->ps.pmove;
	for (axis = 0; axis < 3; axis++)
	{
		pms_out->origin[axis] = origin_q8[axis];
		pms_out->velocity[axis] = velocity_q8[axis];
	}
	pms_out->gravity = (short)sv_gravity->value;
	return true;
}

static qboolean DoorStep_ApproachObservation(edict_t *entity,
	const sg_rune_mechanism_binding_t *binding,
	sg_door_approach_observation_t *observation)
{
	uint32_t support_key, support_generation;

	if (observation)
		memset(observation, 0, sizeof(*observation));
	if (!entity || !binding || !observation ||
	    !DoorStep_LivePmoveState(entity, &observation->pms))
		return false;
	observation->grounded = entity->groundentity ? 1 : 0;
	observation->static_support = entity->groundentity &&
	    DoorStep_StaticSupportSignature(entity->groundentity, &support_key,
	        &support_generation);
	observation->watertype = entity->watertype;
	observation->waterlevel = entity->waterlevel;
	observation->hazardous_liquid =
	    (entity->watertype & (CONTENTS_LAVA | CONTENTS_SLIME)) != 0;
	observation->population_stable = 1;
	observation->sweep_clear =
	    SG_BoundDoorOutsideSweep(binding, entity->s.origin) ? 1 : 0;
	return true;
}

static qboolean DoorStep_RunePublicationCurrent(const rune_t *rune,
	const rune_artifact_t *artifact)
{
	const rune_t *current = SG_Rune();
	const rune_artifact_t *current_artifact;

	return rune && artifact && current == rune &&
	       (current_artifact = SG_RuneArtifact(current)) != NULL &&
	       SG_RuneArtifactsEqual(current_artifact, artifact);
}

static qboolean DoorStep_ApproachIdentityCurrent(
	const sg_bot_t *bot, const sg_rune_mechanism_binding_t *binding)
{
	const sg_door_approach_identity_t *identity;
	int slot;

	if (!bot || !binding || !binding->link || !binding->plan ||
	    !binding->entry_node || !binding->mover_node ||
	    !(identity = &bot->declared_door_approach_identity)->active ||
	    (slot = DoorStep_BotSlot(bot)) < 0 || !bot->active || !bot->ent ||
	    bot->instance_token == 0ULL ||
	    identity->bot_slot != slot ||
	    identity->bot_instance != bot->instance_token ||
	    binding->link_index > INT_MAX ||
	    identity->link != bot->commit_link ||
	    identity->link != (int)binding->link_index ||
	    identity->from != binding->link->from ||
	    identity->to != binding->link->to ||
	    identity->action != RL_DOOR || binding->link->action != RL_DOOR ||
	    identity->controller_kind !=
	        SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR ||
	    binding->plan->controller_kind !=
	        SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR ||
	    identity->entry_key != binding->entry_node->key ||
	    identity->mover_key != binding->mover_node->key ||
	    identity->rune != binding->rune ||
	    !DoorStep_RunePublicationCurrent(identity->rune,
	        &identity->rune_artifact) ||
	    !SG_MoverOwnerEqual(&identity->guard_owner,
	        &bot->compound_guard.owner) ||
	    !DoorStep_GuardTicketEqual(&identity->guard_ticket,
	        &bot->compound_guard.ticket) ||
	    !SG_RuneMechanismBindingCurrent(binding))
		return false;
	return true;
}

static qboolean DoorStep_ApproachTicketRequired(const sg_bot_t *bot,
	const sg_rune_mechanism_binding_t *binding)
{
	return DoorStep_ApproachIdentityCurrent(bot, binding) &&
	       bot->declared_door_approach.phase != SG_DOOR_APPROACH_IDLE &&
	       bot->declared_door_approach.phase != SG_DOOR_APPROACH_COMPLETE &&
	       bot->declared_door_approach.phase != SG_DOOR_APPROACH_FAILED;
}

static qboolean DoorStep_ApproachCommandScoped(const sg_bot_t *bot)
{
	return bot && (bot->declared_door_ticket.armed ||
	       (sg_door_approach_command.active &&
	        sg_door_approach_command.bot == bot));
}

static qboolean DoorStep_ApproachCallbackReject(sg_bot_t *bot,
	qboolean consume_command)
{
	if (consume_command)
		DoorStep_ApproachCommandFail(bot);
	return false;
}

static qboolean DoorStep_ApproachBegin(sg_bot_t *bot,
	const sg_rune_mechanism_binding_t *binding, const short source_q8[3],
	const short anchor_q8[3])
{
	sg_door_approach_observation_t observation;
	sg_door_approach_result_t result;
	sg_door_approach_identity_t *identity;
	const rune_artifact_t *artifact;
	int slot;

	if (!bot || !binding || !source_q8 || !anchor_q8 ||
	    !binding->link || !binding->plan || !binding->entry_node ||
	    !binding->mover_node || binding->link->action != RL_DOOR ||
	    binding->plan->controller_kind !=
	        SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR ||
	    binding->rune != SG_Rune() ||
	    !(artifact = SG_RuneArtifact(binding->rune)))
		return false;
	if (bot->declared_door_approach_identity.active)
		return DoorStep_ApproachIdentityCurrent(bot, binding);
	if (bot->declared_door_approach.phase != SG_DOOR_APPROACH_IDLE ||
	    (slot = DoorStep_BotSlot(bot)) < 0 || bot->instance_token == 0ULL ||
	    !SG_MoverOwnerValid(&bot->compound_guard.owner) ||
	    !SG_MoverTicketValid(&bot->compound_guard.ticket) ||
	    !DoorStep_ApproachObservation(bot->ent, binding, &observation))
		return false;
	result = SG_DoorApproachBegin(&bot->declared_door_approach,
	    source_q8, anchor_q8, &observation);
	if (result.reason != SG_DOOR_APPROACH_REASON_NONE)
	{
		SG_DoorApproachReset(&bot->declared_door_approach);
		return false;
	}
	identity = &bot->declared_door_approach_identity;
	memset(identity, 0, sizeof(*identity));
	identity->bot_instance = bot->instance_token;
	identity->bot_slot = slot;
	identity->link = binding->link_index;
	identity->from = binding->link->from;
	identity->to = binding->link->to;
	identity->action = binding->link->action;
	identity->controller_kind = binding->plan->controller_kind;
	identity->entry_key = binding->entry_node->key;
	identity->mover_key = binding->mover_node->key;
	identity->rune = binding->rune;
	identity->rune_artifact = *artifact;
	identity->guard_owner = bot->compound_guard.owner;
	identity->guard_ticket = bot->compound_guard.ticket;
	identity->active = true;
	return true;
}

static qboolean DoorStep_ApproachTicketStateCurrent(
	const sg_bot_t *bot, const sg_rune_mechanism_binding_t *binding,
	const edict_t *entity, const sg_door_approach_ticket_t *ticket,
	qboolean require_grounded)
{
	pmove_state_t current_pms;
	short mins_q8[3], maxs_q8[3];
	uint32_t support_key = 0U, support_generation = 0U;
	qboolean grounded;
	int slot;

	if (!bot || !binding || !entity || !ticket || !ticket->armed ||
	    (slot = DoorStep_BotSlot(bot)) < 0 ||
	    !DoorStep_ApproachIdentityCurrent(bot, binding) ||
	    binding->link_index > INT_MAX ||
	    ticket->bot_slot != slot ||
	    ticket->bot_instance != bot->instance_token ||
	    ticket->link != (int)binding->link_index ||
	    ticket->from != binding->link->from ||
	    ticket->to != binding->link->to ||
	    ticket->action != binding->link->action ||
	    ticket->controller_kind != binding->plan->controller_kind ||
	    ticket->entry_key != binding->entry_node->key ||
	    ticket->mover_key != binding->mover_node->key ||
	    ticket->rune != binding->rune ||
	    ticket->rune != bot->declared_door_approach_identity.rune ||
	    !SG_RuneArtifactsEqual(&ticket->rune_artifact,
	        &bot->declared_door_approach_identity.rune_artifact) ||
	    !DoorStep_RunePublicationCurrent(ticket->rune,
	        &ticket->rune_artifact) ||
	    !DoorStep_ApproachTicketMastersCurrent(binding, ticket) ||
	    memcmp(&bot->declared_door_approach, &ticket->pre_state,
	        sizeof(ticket->pre_state)) != 0 ||
	    memcmp(&ticket->predicted_state, &ticket->sealed_predicted_state,
	        sizeof(ticket->predicted_state)) != 0 ||
	    ticket->frame != level.framenum ||
	    !sg_door_approach_command.active ||
	    sg_door_approach_command.bot != bot ||
	    sg_door_approach_command.bot_instance != bot->instance_token ||
	    sg_door_approach_command.frame != ticket->frame ||
	    sg_door_approach_command.substep != ticket->substep ||
	    !SG_MoverOwnerEqual(&ticket->guard_owner,
	        &bot->compound_guard.owner) ||
	    !DoorStep_GuardTicketEqual(&ticket->guard_ticket,
	        &bot->compound_guard.ticket) ||
	    entity != bot->ent || !entity->inuse || !entity->client ||
	    entity->health <= 0 || entity->deadflag ||
	    entity->movetype != MOVETYPE_WALK ||
	    !DoorStep_LivePmoveState(entity, &current_pms) ||
	    !SG_DoorApproachPmoveEqual(&current_pms, &ticket->expected_pms) ||
	    !DoorStep_VectorQ8Exact(entity->mins, mins_q8) ||
	    !DoorStep_VectorQ8Exact(entity->maxs, maxs_q8) ||
	    memcmp(mins_q8, ticket->expected_mins_q8, sizeof(mins_q8)) != 0 ||
	    memcmp(maxs_q8, ticket->expected_maxs_q8, sizeof(maxs_q8)) != 0 ||
	    ticket->expected_waterlevel !=
	        ticket->predicted_state.expected_waterlevel ||
	    ticket->expected_watertype !=
	        ticket->predicted_state.expected_watertype ||
	    entity->waterlevel != ticket->expected_waterlevel ||
	    entity->watertype != ticket->expected_watertype ||
	    !SG_DoorApproachWaterSafe(entity->waterlevel, entity->watertype))
		return false;
	grounded = entity->groundentity != NULL;
	if (grounded != ticket->grounded || (require_grounded && !grounded))
		return false;
	if (grounded &&
	    !DoorStep_StaticSupportSignature(entity->groundentity, &support_key,
	        &support_generation))
		return false;
	return support_key == ticket->support_key &&
	       support_generation == ticket->support_generation;
}

static qboolean DoorStep_ApproachTicketArm(sg_bot_t *bot,
	const sg_rune_mechanism_binding_t *binding,
	const sg_door_approach_prediction_t *prediction, int substep)
{
	sg_door_approach_ticket_t *ticket;
	sg_door_approach_observation_t observation;
	sg_door_approach_state_t recomputed;
	sg_door_approach_result_t transition;
	pmove_state_t current_pms;
	vec3_t predicted_origin;
	uint32_t support_key = 0U, support_generation = 0U;
	uint32_t activation_master_keys[SG_DOOR_APPROACH_MAX_MASTERS];
	size_t activation_master_count = 0U;
	qboolean grounded;
	int axis, slot;

	if (!bot)
		return false;
	if (bot->declared_door_ticket.armed || sg_door_approach_command.active)
	{
		DoorStep_ApproachCommandFail(bot);
		return false;
	}
	if (!binding || !prediction || substep < 0 || substep >= 4)
		return false;
	DoorStep_ApproachTicketClear(bot);
	if (
	    (slot = DoorStep_BotSlot(bot)) < 0 ||
	    !DoorStep_ApproachIdentityCurrent(bot, binding) ||
	    !DoorStep_LivePmoveState(bot->ent, &current_pms) ||
	    !SG_DoorApproachPmoveEqual(&current_pms,
	        &bot->declared_door_approach.expected_pms) ||
	    bot->ent->waterlevel !=
	        bot->declared_door_approach.expected_waterlevel ||
	    bot->ent->watertype !=
	        bot->declared_door_approach.expected_watertype ||
	    prediction->state.elapsed_ms !=
	        bot->declared_door_approach.elapsed_ms +
	            SG_DOOR_APPROACH_STEP_MS ||
	    memcmp(prediction->state.source_q8,
	        bot->declared_door_approach.source_q8,
	        sizeof(prediction->state.source_q8)) != 0 ||
	    memcmp(prediction->state.anchor_q8,
	        bot->declared_door_approach.anchor_q8,
	        sizeof(prediction->state.anchor_q8)) != 0 ||
	    !SG_DoorApproachPmoveEqual(&prediction->state.expected_pms,
	        &prediction->pms) ||
	    prediction->state.expected_waterlevel != prediction->waterlevel ||
	    prediction->state.expected_watertype != prediction->watertype ||
	    !SG_DoorApproachWaterSafe(prediction->waterlevel,
	        prediction->watertype) ||
	    (prediction->expected_touch && !prediction->groundentity) ||
	    !DoorStep_VectorQ8Exact(prediction->mins, NULL) ||
	    !DoorStep_VectorQ8Exact(prediction->maxs, NULL) ||
	    !DoorStep_BindingActivationMasters(binding, activation_master_keys,
	        &activation_master_count))
		return false;
	if (prediction->groundentity &&
	    !DoorStep_StaticSupportSignature(prediction->groundentity,
	        &support_key, &support_generation))
		return false;
	grounded = prediction->groundentity != NULL;
	memset(&observation, 0, sizeof(observation));
	observation.pms = prediction->pms;
	observation.grounded = grounded ? 1 : 0;
	observation.static_support = grounded ? 1 : 0;
	observation.watertype = prediction->watertype;
	observation.waterlevel = prediction->waterlevel;
	observation.hazardous_liquid =
	    (prediction->watertype & (CONTENTS_LAVA | CONTENTS_SLIME)) != 0;
	observation.population_stable = 1;
	for (axis = 0; axis < 3; axis++)
		predicted_origin[axis] = prediction->pms.origin[axis] * 0.125f;
	observation.sweep_clear =
	    SG_BoundDoorOutsideSweep(binding, predicted_origin) ? 1 : 0;
	observation.physical_touch = prediction->expected_touch ? 1 : 0;
	observation.fall_sampled =
	    ((bot->declared_door_approach.elapsed_ms +
	      SG_DOOR_APPROACH_STEP_MS) % SG_DOOR_APPROACH_FRAME_MS) == 0;
	if (observation.fall_sampled)
		observation.fall_delta = P_FallDelta(
		    bot->declared_door_approach.old_frame_z,
		    prediction->pms.velocity[2] * 0.125f, grounded,
		    prediction->waterlevel);
	recomputed = bot->declared_door_approach;
	transition = SG_DoorApproachPostStep(&recomputed, &observation,
	    SG_DOOR_APPROACH_STEP_MS);
	if (transition.reason != SG_DOOR_APPROACH_REASON_NONE ||
	    memcmp(&recomputed, &prediction->state, sizeof(recomputed)) != 0)
		return false;
	ticket = &bot->declared_door_ticket;
	ticket->expected_pms = prediction->pms;
	ticket->expected_watertype = prediction->watertype;
	ticket->expected_waterlevel = prediction->waterlevel;
	if (!DoorStep_VectorQ8Exact(prediction->mins,
	        ticket->expected_mins_q8) ||
	    !DoorStep_VectorQ8Exact(prediction->maxs,
	        ticket->expected_maxs_q8))
	{
		DoorStep_ApproachTicketClear(bot);
		return false;
	}
	ticket->support_key = support_key;
	ticket->support_generation = support_generation;
	ticket->bot_instance = bot->instance_token;
	ticket->bot_slot = slot;
	ticket->link = binding->link_index;
	ticket->from = binding->link->from;
	ticket->to = binding->link->to;
	ticket->action = binding->link->action;
	ticket->controller_kind = binding->plan->controller_kind;
	ticket->frame = level.framenum;
	ticket->substep = substep;
	ticket->entry_key = binding->entry_node->key;
	ticket->mover_key = binding->mover_node->key;
	ticket->rune = bot->declared_door_approach_identity.rune;
	ticket->rune_artifact =
	    bot->declared_door_approach_identity.rune_artifact;
	ticket->guard_owner = bot->compound_guard.owner;
	ticket->guard_ticket = bot->compound_guard.ticket;
	ticket->pre_state = bot->declared_door_approach;
	ticket->predicted_state = prediction->state;
	ticket->sealed_predicted_state = prediction->state;
	ticket->grounded = prediction->groundentity != NULL;
	ticket->expected_touch = prediction->expected_touch;
	memcpy(ticket->activation_master_keys, activation_master_keys,
	    activation_master_count * sizeof(activation_master_keys[0]));
	ticket->activation_master_count = (uint8_t)activation_master_count;
	ticket->armed = true;
	if (sg_door_approach_command.active)
	{
		DoorStep_ApproachTicketClear(bot);
		return false;
	}
	sg_door_approach_command.bot = bot;
	sg_door_approach_command.bot_instance = bot->instance_token;
	sg_door_approach_command.frame = level.framenum;
	sg_door_approach_command.substep = substep;
	sg_door_approach_command.active = true;
	return true;
}

static qboolean DoorStep_ApproachTicketFinish(sg_bot_t *bot,
	const sg_rune_mechanism_binding_t *binding, edict_t *entity)
{
	sg_door_approach_ticket_t ticket;
	qboolean current;

	if (!bot)
		return false;
	ticket = bot->declared_door_ticket;
	current = DoorStep_ApproachTicketStateCurrent(bot, binding, entity,
	    &ticket, false);
	if (!current || ticket.touch_seen != ticket.expected_touch ||
	    (ticket.activation_seen && !ticket.touch_seen) ||
	    ticket.activation_seen !=
	        (ticket.activation_master_seen_mask != 0U) ||
	    (ticket.activation_seen &&
	     ticket.activation_master_seen_mask !=
	         ((UINT32_C(1) << ticket.activation_master_count) - UINT32_C(1))) ||
	    ticket.predicted_state.phase == SG_DOOR_APPROACH_FAILED)
	{
		DoorStep_ApproachCommandFail(bot);
		return false;
	}
	DoorStep_ApproachTicketClear(bot);
	bot->declared_door_approach = ticket.predicted_state;
	return true;
}

qboolean SG_DeclaredDoorApproachExecutionBegin(sg_bot_t *bot,
	const sg_rune_mechanism_binding_t *binding, const short source_q8[3],
	const short anchor_q8[3])
{
	return DoorStep_ApproachBegin(bot, binding, source_q8, anchor_q8);
}

qboolean SG_DeclaredDoorApproachExecutionArm(sg_bot_t *bot,
	const sg_rune_mechanism_binding_t *binding,
	const sg_door_approach_prediction_t *prediction, int substep)
{
	return DoorStep_ApproachTicketArm(bot, binding, prediction, substep);
}

qboolean SG_DeclaredDoorApproachExecutionFinish(sg_bot_t *bot,
	const sg_rune_mechanism_binding_t *binding, edict_t *entity)
{
	return DoorStep_ApproachTicketFinish(bot, binding, entity);
}

/* Touch_Multi and Touch_DoorTrigger call this after their ordinary player
 * gates but before debounce, activator publication, targets, or door motion.
 * Humans and mechanisms outside the strict declared-door contract keep their
 * original behavior.  A supported activator touched by an SG body requires
 * the exact ACTIVE shared-mover claim before the callback may mutate world
 * state. */
qboolean SG_AuthorizeDoorTriggerTouch(edict_t *source, edict_t *activator)
{
	sg_rune_mechanism_binding_t binding;
	sg_bot_t *bot;
	qboolean command_scoped;
	qboolean ticket_required;

	bot = DoorStep_EventBot(activator);
	if (!bot)
		return true;
	if (SG_CompoundSwimGameOwns(bot))
		return SG_CompoundSwimGameAuthorizeTouch(source, activator);
	{
		int compound = SG_CompoundDropGameAuthorizeTouch(bot, source,
		    activator, level.framenum);

		if (compound >= 0)
			return compound ? true : false;
	}
	command_scoped = DoorStep_ApproachCommandScoped(bot);
	if (!DoorStep_DeclaredBinding(bot, &binding) ||
	    binding.entry_entity != source)
	{
		if (command_scoped)
			return DoorStep_ApproachCallbackReject(bot, true);
		return !DoorStep_DeclaredClaimHeld(bot) &&
		       !SG_DeclaredDoorGuardAnyClaim();
	}
	if (binding.plan->controller_kind ==
	        SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR &&
	    bot->declared_door_approach_identity.active &&
	    bot->declared_door_approach.phase == SG_DOOR_APPROACH_FAILED)
		return DoorStep_ApproachCallbackReject(bot, command_scoped);
	ticket_required = binding.plan->controller_kind ==
	        SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR &&
	    DoorStep_ApproachTicketRequired(bot, &binding);
	if (activator->health <= 0 || activator->deadflag ||
	    activator->movetype != MOVETYPE_WALK ||
	    activator->client->ps.pmove.pm_type != PM_NORMAL ||
	    (activator->client->ps.pmove.pm_flags & PMF_DUCKED) ||
	    activator->client->ps.pmove.pm_time || !activator->groundentity ||
	    (activator->waterlevel != 0 &&
	     !(ticket_required && command_scoped &&
	       SG_DoorApproachWaterSafe(activator->waterlevel,
	           activator->watertype))))
		return DoorStep_ApproachCallbackReject(bot, command_scoped);
	if (!bot || !bot->declared_started || bot->declared_guard_paused)
		return DoorStep_ApproachCallbackReject(bot, command_scoped);
	if (command_scoped != ticket_required)
		return DoorStep_ApproachCallbackReject(bot, command_scoped);
	if (ticket_required &&
	    (!bot->declared_door_ticket.expected_touch ||
	     bot->declared_door_ticket.touch_seen ||
	     !DoorStep_ApproachTicketStateCurrent(bot, &binding, activator,
	         &bot->declared_door_ticket, true)))
		return DoorStep_ApproachCallbackReject(bot, true);
	if (SG_DeclaredDoorGuardAuthorizeActivation(bot, bot->commit_link) !=
	        SG_COMPOUND_GUARD_OK ||
	    !SG_RuneMechanismBindingCurrent(&binding))
		return DoorStep_ApproachCallbackReject(bot, command_scoped);
	if (!SG_BoundDoorTouchMatches(&binding, activator->s.origin) ||
	    !SG_RuneMechanismBindingCurrent(&binding))
		return DoorStep_ApproachCallbackReject(bot, command_scoped);
	if (ticket_required)
		bot->declared_door_ticket.touch_seen = true;
	if (!bot->declared_triggered && !bot->declared_activated)
	{
		bot->declared_touched = true;
		bot->declared_touch_frame = level.framenum;
	}
	return true;
}

/* A declared trigger_multiple is admitted for a physical Touch_Multi event,
 * never a targeted Use_Multi callback.  Deny remote SG activation before it
 * publishes activator or consumes the trigger cooldown; unowned humans and
 * mechanisms outside the strict declaration retain stock behavior. */
qboolean SG_AuthorizeDoorTriggerUse(edict_t *source, edict_t *activator)
{
	sg_bot_t *bot = DoorStep_EventBot(activator);
	sg_rune_mechanism_binding_t binding;

	if (!bot)
		return true;
	if (DoorStep_DeclaredBinding(bot, &binding) &&
	    binding.entry_entity == source)
		return false;
	return !DoorStep_DeclaredClaimHeld(bot) &&
	       !SG_DeclaredDoorGuardAnyClaim();
}

static qboolean DoorStep_ButtonBinding(sg_bot_t *bot, edict_t *source,
	sg_rune_mechanism_binding_t *binding_out)
{
	return bot && source && binding_out &&
	       DoorStep_DeclaredBinding(bot, binding_out) &&
	       binding_out->plan->controller_kind ==
	           SG_MECHANISM_CONTROLLER_BUTTON_DOOR &&
	       binding_out->entry_entity == source;
}

qboolean SG_AuthorizeButtonTouch(edict_t *source, edict_t *activator)
{
	sg_rune_mechanism_binding_t binding;
	sg_mech_button_endpoints_t endpoints = { 0 };
	sg_button_callback_token_t *token;
	sg_bot_t *bot = DoorStep_EventBot(activator);
	sg_button_callback_state_t token_state;
	qboolean rider = false;
	short displacement_q8[3];
	int activator_key;
	int axis;
	int source_key;

	if (!bot)
	{
		token = DoorStep_ButtonToken(source, &source_key);
		if (!DoorStep_SGProvenanceActivator(activator))
			return DoorStep_ButtonOrdinaryEvent(token,
			    (uint32_t)source_key);
		return DoorStep_UnownedBotActivator(activator) &&
		       DoorStep_ButtonOrdinaryEvent(token, (uint32_t)source_key);
	}
	if (!DoorStep_ButtonBinding(bot, source, &binding))
		return !DoorStep_DeclaredClaimHeld(bot) &&
		       !SG_DeclaredDoorGuardAnyClaim();
	token = DoorStep_ButtonToken(source, &source_key);
	activator_key = DoorStep_EdictKey(activator);
	if (!token || source_key <= 0 || activator_key <= 0)
		return false;
	token_state = SG_ButtonCallbackTokenState(token);
	if (!SG_ButtonCallbackTokenPlannedTouchAllowed(token,
	        (uint32_t)source_key,
	        source->moveinfo.state == SG_PLAT_STATE_BOTTOM))
		return false;
	if (token_state == SG_BUTTON_CALLBACK_CONSUMED)
	{
		if (bot->declared_triggered)
			return false;
		SG_ButtonCallbackTokenReset(token);
		token_state = SG_BUTTON_CALLBACK_EMPTY;
	}
	if (activator->health <= 0 || activator->deadflag ||
	    activator->movetype != MOVETYPE_WALK ||
	    activator->client->ps.pmove.pm_type != PM_NORMAL ||
	    (activator->client->ps.pmove.pm_flags & PMF_DUCKED) ||
	    activator->client->ps.pmove.pm_time || !activator->groundentity ||
	    activator->waterlevel != 0 || !bot->declared_started ||
	    bot->declared_guard_paused ||
	    SG_DeclaredDoorGuardAuthorizeActivation(bot, bot->commit_link) !=
	        SG_COMPOUND_GUARD_OK ||
	    !SG_RuneMechanismBindingCurrent(&binding))
		return false;
	if (token_state == SG_BUTTON_CALLBACK_PENDING)
	{
		/* Idempotent physical overlap may recur while the same callback is in
		 * flight.  It can confirm, but never rewrite, the first-touch support
		 * mode or sealed endpoint snapshot. */
		if (!bot->declared_button_latched || !bot->declared_touched ||
		    !DoorStep_VectorQ8Exact(source->moveinfo.start_origin, NULL) ||
		    !DoorStep_VectorQ8Exact(source->moveinfo.end_origin, NULL))
			return false;
		for (axis = 0; axis < 3; axis++)
			if ((short)lround((double)source->moveinfo.start_origin[axis] * 8.0) !=
			        bot->declared_button_start_q8[axis] ||
			    (short)lround((double)source->moveinfo.end_origin[axis] * 8.0) !=
			        bot->declared_button_end_q8[axis])
				return false;
	}
	else
	{
		if (token_state != SG_BUTTON_CALLBACK_EMPTY ||
		    !SG_MechCatalogButtonBottomEndpoints((uint32_t)source_key,
		        binding.entry_node, source, &endpoints))
			return false;
		if (activator->groundentity == source)
			rider = true;
		else if (activator->groundentity != g_edicts &&
		         !SG_ImmutableSupport(activator->groundentity))
			return false;
		for (axis = 0; axis < 3; axis++)
		{
			int delta = (int)endpoints.end_q8[axis] -
			    (int)endpoints.start_q8[axis];

			if (delta < INT16_MIN || delta > INT16_MAX)
				return false;
			displacement_q8[axis] = (short)delta;
			if ((float)displacement_q8[axis] * 0.125f !=
			    binding.link->mechanism_anchor[axis])
				return false;
		}
		if ((rider && binding.link->mode != RLCM_RIDE) ||
		    (!rider && binding.link->mode != RLCM_PREOPEN))
			return false;
	}
	if (!SG_ButtonCallbackTokenBegin(token, (uint32_t)source_key,
	        activator_key, bot->commit_link, &bot->compound_guard.owner,
	        &bot->compound_guard.ticket))
		return false;
	if (token_state == SG_BUTTON_CALLBACK_EMPTY)
	{
		bot->declared_button_latched = true;
		bot->declared_button_rider = rider;
		memcpy(bot->declared_button_start_q8, endpoints.start_q8,
		    sizeof(bot->declared_button_start_q8));
		memcpy(bot->declared_button_end_q8, endpoints.end_q8,
		    sizeof(bot->declared_button_end_q8));
	}
	bot->declared_touched = true;
	bot->declared_touch_frame = level.framenum;
	return true;
}

/* The admitted button controller is physical touch only.  Targeted use and
 * damage cannot borrow a bot's active plan. */
qboolean SG_AuthorizeButtonUse(edict_t *source, edict_t *activator)
{
	sg_rune_mechanism_binding_t binding;
	sg_button_callback_token_t *token;
	sg_bot_t *bot = DoorStep_EventBot(activator);
	int source_key;

	if (!bot)
	{
		token = DoorStep_ButtonToken(source, &source_key);
		if (!DoorStep_SGProvenanceActivator(activator))
			return DoorStep_ButtonOrdinaryEvent(token,
			    (uint32_t)source_key);
		return DoorStep_UnownedBotActivator(activator) &&
		       DoorStep_ButtonOrdinaryEvent(token, (uint32_t)source_key);
	}
	if (DoorStep_ButtonBinding(bot, source, &binding))
		return false;
	return !DoorStep_DeclaredClaimHeld(bot) &&
	       !SG_DeclaredDoorGuardAnyClaim();
}

qboolean SG_AuthorizeButtonTargets(edict_t *source, edict_t *activator)
{
	sg_rune_mechanism_binding_t binding;
	sg_button_callback_token_t *token;
	sg_button_callback_result_t result;
	sg_bot_t *bot = DoorStep_EventBot(activator);
	int activator_key;
	int authority = 0;
	int source_key;

	token = DoorStep_ButtonToken(source, &source_key);
	if (SG_ButtonCallbackTokenState(token) != SG_BUTTON_CALLBACK_EMPTY)
	{
		activator_key = DoorStep_EdictKey(activator);
		if (bot && DoorStep_ButtonBinding(bot, source, &binding) &&
		    bot->declared_started && bot->declared_touched &&
		    source->activator == activator && !bot->declared_guard_paused &&
		    SG_DeclaredDoorGuardAuthorizeActivation(bot, bot->commit_link) ==
		        SG_COMPOUND_GUARD_OK &&
		    SG_RuneMechanismBindingCurrent(&binding))
			authority = 1;
		result = SG_ButtonCallbackTokenConsume(token, (uint32_t)source_key,
		    activator_key, bot ? bot->commit_link : -1,
		    bot ? &bot->compound_guard.owner : NULL,
		    bot ? &bot->compound_guard.ticket : NULL, authority);
		return result == SG_BUTTON_CALLBACK_AUTHORIZE;
	}
	if (!DoorStep_SGProvenanceActivator(activator))
		return true;
	if (!bot)
		return DoorStep_UnownedBotActivator(activator);
	/* A planned button callback cannot be reconstructed from mutable bot state
	 * after its physical-touch token was lost or consumed. */
	if (!DoorStep_ButtonBinding(bot, source, &binding))
		return !DoorStep_DeclaredClaimHeld(bot) &&
		       !SG_DeclaredDoorGuardAnyClaim();
	return false;
}

static qboolean DoorStep_BindingEntityKey(
	const sg_rune_mechanism_binding_t *binding, const edict_t *entity,
	uint32_t *key_out)
{
	uint32_t ordinal;

	if (key_out)
		*key_out = UINT32_MAX;
	if (!binding || !entity || !key_out ||
	    !SG_RuneMechanismBindingCurrent(binding))
		return false;
	if (binding->entry_entity == entity)
	{
		*key_out = binding->entry_node->key;
		return true;
	}
	if (binding->mover_entity == entity)
	{
		*key_out = binding->mover_node->key;
		return true;
	}
	for (ordinal = 0U; ordinal < binding->plan->num_edges; ordinal++)
	{
		const rune_mechanism_edge_t *edge =
			SG_RuneMechanismBindingEdgeAt(binding, ordinal);
		uint32_t keys[2];
		int endpoint;

		if (!edge)
			return false;
		keys[0] = edge->from_key;
		keys[1] = edge->to_key;
		for (endpoint = 0; endpoint < 2; endpoint++)
			if (SG_RuneMechanismBindingResolveNode(binding,
			        keys[endpoint]) == entity)
			{
				*key_out = keys[endpoint];
				return SG_RuneMechanismBindingCurrent(binding) ? true : false;
			}
	}
	return false;
}

typedef struct door_step_dispatch_s
{
	edict_t *source;
	edict_t *activator;
} door_step_dispatch_t;

static int DoorStep_InvokeBoundTarget(void *raw_context,
	struct edict_s *raw_target, uint32_t target_key,
	uint32_t target_ordinal)
{
	door_step_dispatch_t *context = raw_context;
	edict_t *target = raw_target;

	(void)target_key;
	(void)target_ordinal;
	if (!context || !context->source || !context->activator || !target)
		return 0;
	if (target->use)
		target->use(target, context->source, context->activator);
	return 1;
}

qboolean SG_HandleMechanismTargets(edict_t *source, edict_t *activator)
{
	sg_rune_mechanism_binding_t binding;
	sg_bot_t *bot = DoorStep_EventBot(activator);
	door_step_dispatch_t dispatch;
	uint32_t source_key;

	if (!bot)
		return false;
	if (SG_CompoundDropGameOwnsTargetDispatch(bot, source))
		return false;
	if (!DoorStep_DeclaredBinding(bot, &binding))
		return DoorStep_DeclaredClaimHeld(bot) ||
		       SG_DeclaredDoorGuardAnyClaim();
	if (!source || !bot->declared_started || bot->declared_guard_paused ||
	    source->delay != 0.0f || source->killtarget || source->message ||
	    !DoorStep_BindingEntityKey(&binding, source, &source_key) ||
	    SG_DeclaredDoorGuardAuthorizeActivation(bot, bot->commit_link) !=
	        SG_COMPOUND_GUARD_OK)
		return true;
	if (source == binding.entry_entity)
	{
		if (!bot->declared_touched)
			return true;
		if (binding.plan->controller_kind !=
		        SG_MECHANISM_CONTROLLER_BUTTON_DOOR &&
		    bot->declared_touch_frame != level.framenum)
			return true;
	}
	/* The guarded branch consumes the event before name-based stock traversal,
	 * executing the exact copied closure or withholding the entire fanout. */
	if (sg_mechanism_dispatch_depth >= binding.plan->num_edges)
		return true;
	dispatch.source = source;
	dispatch.activator = activator;
	sg_mechanism_dispatch_depth++;
	(void)SG_RuneMechanismBindingDispatchTargets(&binding, source_key,
		DoorStep_InvokeBoundTarget, &dispatch);
	sg_mechanism_dispatch_depth--;
	return true;
}

static qboolean MechanismStep_Binding(const sg_bot_t *bot, int action,
	sg_rune_mechanism_binding_t *binding_out)
{
	rune_t *rune;

	if (binding_out)
		memset(binding_out, 0, sizeof(*binding_out));
	return bot && binding_out && bot->commit_link >= 0 &&
	       (rune = SG_Rune()) != NULL && SG_RunePhysicsCompatible(rune) &&
	       (bot->declared_started
	           ? SG_RuneMechanismBindingCaptureOwned(rune,
	                 (uint32_t)bot->commit_link, binding_out)
	           : SG_RuneMechanismBindingCapture(rune,
	                 (uint32_t)bot->commit_link, binding_out)) &&
	       binding_out->link->action == action;
}

qboolean SG_AuthorizeLiftTouch(edict_t *source, edict_t *platform,
	edict_t *activator)
{
	sg_rune_mechanism_binding_t binding;
	sg_bot_t *bot = DoorStep_EventBot(activator);

	if (!bot)
		return true;
	if (!MechanismStep_Binding(bot, RL_LIFT, &binding) ||
	    binding.plan->controller_kind != SG_MECHANISM_CONTROLLER_PLATFORM ||
	    binding.entry_entity != source || binding.mover_entity != platform ||
	    !bot->declared_started || activator->health <= 0 ||
	    activator->deadflag || activator->movetype != MOVETYPE_WALK ||
	    activator->client->ps.pmove.pm_type != PM_NORMAL ||
	    !SG_RuneMechanismBindingCurrent(&binding))
		return false;
	bot->declared_touched = true;
	bot->declared_touch_frame = level.framenum;
	return true;
}

qboolean SG_AuthorizeLiftUse(edict_t *platform, edict_t *activator)
{
	sg_bot_t *bot = DoorStep_EventBot(activator);

	(void)platform;
	if (!bot)
		return true;
	/* The admitted platform controller is the exact synthesized center touch,
	 * never a remote Use_Plat callback. */
	return false;
}

edict_t *SG_ResolveTeleportDestination(edict_t *source,
	edict_t *activator)
{
	sg_rune_mechanism_binding_t binding;
	sg_bot_t *bot = DoorStep_EventBot(activator);
	edict_t *destination;

	if (!bot || !MechanismStep_Binding(bot, RL_TELEPORT, &binding) ||
	    binding.plan->controller_kind != SG_MECHANISM_CONTROLLER_TELEPORT ||
	    binding.entry_entity != source || binding.mover_entity != source->owner ||
	    !bot->declared_started || activator->health <= 0 || activator->deadflag ||
	    activator->movetype != MOVETYPE_WALK ||
	    activator->client->ps.pmove.pm_type != PM_NORMAL ||
	    !(destination = SG_RuneMechanismBindingResolveDestination(&binding)) ||
	    !SG_RuneMechanismBindingCurrent(&binding))
		return NULL;
	bot->declared_touched = true;
	bot->declared_touch_frame = level.framenum;
	return destination;
}

/* G_UseTargets reaches door_use synchronously from Touch_Multi, inside the
 * ClientThink that crossed the trigger.  This is the exact observation that
 * our expected trigger fired, so latch only a fully re-resolved declaration;
 * Think_Emit sees it before the next 25 ms command and makes the rest of this
 * outer frame zero-input.  A set already held TOP by another activator may be
 * accepted independently at the exact wait point, but only after live egress
 * reproof and a sufficient remaining-open-window check. */
qboolean SG_AuthorizeDoorActivation(edict_t *source, edict_t *door_master,
	edict_t *activator)
{
	sg_rune_mechanism_binding_t binding;
	sg_bot_t *bot;
	qboolean command_scoped;
	qboolean ticket_required;
	uint32_t activation_master_bit = 0U;

	bot = DoorStep_EventBot(activator);
	if (!bot)
		return true;
	if (SG_CompoundSwimGameOwns(bot))
		return SG_CompoundSwimGameAuthorizeActivation(source, door_master,
		    activator);
	{
		int compound = SG_CompoundDropGameAuthorizeActivation(bot, source,
		    door_master, activator, level.framenum);

		if (compound >= 0)
			return compound ? true : false;
	}
	command_scoped = DoorStep_ApproachCommandScoped(bot);
	if (!DoorStep_DeclaredBinding(bot, &binding) ||
	    binding.entry_entity != source)
	{
		if (command_scoped)
			return DoorStep_ApproachCallbackReject(bot, true);
		return !DoorStep_DeclaredClaimHeld(bot) &&
		       !SG_DeclaredDoorGuardAnyClaim() &&
		       SG_DeclaredDoorGuardActivationAvailable(door_master);
	}
	if (binding.plan->controller_kind ==
	        SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR &&
	    bot->declared_door_approach_identity.active &&
	    bot->declared_door_approach.phase == SG_DOOR_APPROACH_FAILED)
		return DoorStep_ApproachCallbackReject(bot, command_scoped);
	ticket_required = binding.plan->controller_kind ==
	        SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR &&
	    DoorStep_ApproachTicketRequired(bot, &binding);
	if (!door_master || activator->health <= 0 ||
	    activator->deadflag || activator->movetype != MOVETYPE_WALK ||
	    activator->client->ps.pmove.pm_type != PM_NORMAL ||
	    (activator->client->ps.pmove.pm_flags & PMF_DUCKED) ||
	    activator->client->ps.pmove.pm_time || !activator->groundentity ||
	    (activator->waterlevel != 0 &&
	     !(ticket_required && command_scoped &&
	       SG_DoorApproachWaterSafe(activator->waterlevel,
	           activator->watertype))))
		return DoorStep_ApproachCallbackReject(bot, command_scoped);
	if (!bot || !bot->declared_started || bot->declared_guard_paused)
		return DoorStep_ApproachCallbackReject(bot, command_scoped);
	if (command_scoped != ticket_required)
		return DoorStep_ApproachCallbackReject(bot, command_scoped);
	if (ticket_required &&
	    (!bot->declared_door_ticket.expected_touch ||
	     !bot->declared_door_ticket.touch_seen ||
	     bot->declared_door_ticket.activation_seen !=
	         (bot->declared_door_ticket.activation_master_seen_mask != 0U) ||
	     (bot->declared_triggered &&
	      bot->declared_door_ticket.activation_master_seen_mask == 0U) ||
	     bot->declared_activated ||
	     !DoorStep_ApproachTicketStateCurrent(bot, &binding, activator,
	         &bot->declared_door_ticket, true) ||
	     source != binding.entry_entity ||
	     !DoorStep_ApproachTicketMasterBit(&bot->declared_door_ticket,
	         door_master, &activation_master_bit) ||
	     (bot->declared_door_ticket.activation_master_seen_mask &
	         activation_master_bit) != 0U))
		return DoorStep_ApproachCallbackReject(bot, true);
	if (SG_DeclaredDoorGuardAuthorizeActivation(bot, bot->commit_link) !=
	        SG_COMPOUND_GUARD_OK ||
	    !SG_RuneMechanismBindingCurrent(&binding))
		return DoorStep_ApproachCallbackReject(bot, command_scoped);
	if (!DoorStep_BindingContainsMover(&binding, door_master) ||
	    !SG_RuneMechanismBindingCurrent(&binding))
		return DoorStep_ApproachCallbackReject(bot, command_scoped);
	if (binding.plan->controller_kind == SG_MECHANISM_CONTROLLER_BUTTON_DOOR)
	{
		if (!bot->declared_touched || source->activator != activator)
			return false;
	}
	else if (!SG_BoundDoorTouchMatches(&binding, activator->s.origin))
		return DoorStep_ApproachCallbackReject(bot, command_scoped);
	else if (ticket_required)
	{
		bot->declared_door_ticket.activation_master_seen_mask |=
		    activation_master_bit;
		bot->declared_door_ticket.activation_seen = true;
	}
	if (bot->declared_triggered || bot->declared_activated)
		return true;
	if (!bot->declared_touched ||
	    (binding.plan->controller_kind != SG_MECHANISM_CONTROLLER_BUTTON_DOOR &&
	     bot->declared_touch_frame != level.framenum))
		return false;
	bot->declared_triggered = true;
	bot->declared_trigger_frame = level.framenum;
	return true;
}

static sg_bot_t *Drop_LiveEventOwner(edict_t *activator)
{
	int i;

	if (!activator || !activator->inuse || !activator->client ||
	    !SG_OwnsBot(activator))
		return NULL;
	for (i = 0; i < SG_MAXBOTS; i++)
		if (sg_bots[i].active && sg_bots[i].ent == activator)
		{
			/* Production remains strictly reducer-owned. */
			if ((sg_bots[i].drop_started &&
			     sg_bots[i].drop_replay_active) ||
		    sg_bots[i].compound_drop_live.guard_owned)
				return &sg_bots[i];
		}
	return NULL;
}

/* These taps observe events the host has already selected for the real body.
 * They never replay a trace, touch, use, or pusher side effect. */
void SG_NoteDropTriggerContact(edict_t *source, edict_t *activator)
{
	sg_bot_t *bot = Drop_LiveEventOwner(activator);
	qboolean contaminated, door_passed;

	if (!bot)
		return;
	if (!SG_OracleReplayTriggerEvents(source, &contaminated, &door_passed))
	{
		(void)SG_DropLiveEventsLatch(&bot->drop_live_events, true, false);
		if (bot->compound_drop_live.guard_owned)
			(void)SG_DropLiveEventsLatch(
			    &bot->compound_drop_live.drop_events, true, false);
		return;
	}
	(void)SG_DropLiveEventsLatch(&bot->drop_live_events, contaminated,
	                            door_passed);
	if (bot->compound_drop_live.guard_owned)
		(void)SG_DropLiveEventsLatch(&bot->compound_drop_live.drop_events,
		    contaminated, door_passed);
}

void SG_NoteDropSolidContact(edict_t *source, edict_t *activator)
{
	sg_bot_t *bot = Drop_LiveEventOwner(activator);

	if (!bot || !source || source == g_edicts || SG_ImmutableSupport(source))
		return;
	if (source->classname &&
	    strncmp(source->classname, "func_door", 9) == 0)
	{
		(void)SG_DropLiveEventsLatch(&bot->drop_live_events, false, true);
		if (bot->compound_drop_live.guard_owned &&
		    source->s.number != bot->compound_drop_live.snapshot.mover_key)
			(void)SG_DropLiveEventsLatch(
			    &bot->compound_drop_live.drop_events, false, true);
	}
	else
	{
		(void)SG_DropLiveEventsLatch(&bot->drop_live_events, true, false);
		if (bot->compound_drop_live.guard_owned)
			(void)SG_DropLiveEventsLatch(
			    &bot->compound_drop_live.drop_events, true, false);
	}
}

static qboolean Hook_LinkWaterSource(const sg_bot_t *bot)
{
	rune_link_t *link;

	if (!bot || !SG_Rune() || bot->hook_link < 0 ||
	    bot->hook_link >= SG_Rune()->hdr.num_links)
		return false;
	link = &SG_Rune()->links[bot->hook_link];
	return link->action == RL_HOOK &&
	       (SG_Rune()->seeds[link->from].flags & RSF_WATER) != 0;
}

#define SG_AS_PERIOD	1.35f
#define SG_AS_VIEWSHARE	0.55f
#define SG_AS_VIEWMAX	32.0f
#define SG_AS_CORR		25.0f
#define SG_AS_ABORT		40.0f
#define SG_AS_RUN		320.0f
#define SG_AS_HOLD		0.70f   /* the road bar a live chain is held to */
#define SG_AS_FLOOR		240.0f
#define SG_AS_FLAGKEEP	220.0f
#define SG_AS_MINCHAIN	0.6f
#define SG_PURSUIT_MAX 8    /* seeds of chain; 8 x 128u median link covers
                             * any lookahead worth trying */
#define SG_AS_BEND	30.0f       /* degrees the chord may sit off the route */
#define SG_AS_CHORD	0.80f       /* chord / arc a road has to keep */
#define SG_WEAVE_SIDE		300
#define SG_WEAVE_HOLD		150.0f	/* a step this short is a stand, not a run */
#define SG_DROP_HEALTH_RESERVE	15

/*
 * ClientThink does not take the next Pmove origin and velocity from the
 * cached playerstate.  It rebuilds them from the authoritative entity after
 * the world/projectile loop (p_client.c).  Proved actions must therefore test
 * exactly those values: a rocket may have moved the body since its previous
 * ClientThink while ps.pmove still describes the old, standing state.
 */
static void Ballistic_SourceFixed(const rune_link_t *l, vec3_t source,
	short fixed[3])
{
	int i;

	for (i = 0; i < 3; i++)
	{
		fixed[i] = (short)(SG_Rune()->seeds[l->from].origin[i] * 8.0f);
		source[i] = fixed[i] * 0.125f;
	}
}

static qboolean Ballistic_SourceExact(edict_t *e, const short fixed[3])
{
	return (short)(e->s.origin[0] * 8.0f) == fixed[0] &&
	       (short)(e->s.origin[1] * 8.0f) == fixed[1] &&
	       (short)(e->s.origin[2] * 8.0f) == fixed[2];
}

static qboolean Ballistic_SourceRest(edict_t *e)
{
	return (short)(e->velocity[0] * 8.0f) == 0 &&
	       (short)(e->velocity[1] * 8.0f) == 0 &&
	       (short)(e->velocity[2] * 8.0f) == 0;
}

/*
 * Low-speed 25 ms Pmove is quantized to eighth-unit positions.  A staging
 * body can otherwise cross the exact source between 100 ms policy samples.
 * Staging brakes inside a two-unit capture zone; once authoritative velocity
 * is zero, canonicalize through a clear player-box sweep.  The following
 * ClientThink then consumes precisely the state the generator placed.  This
 * is a bounded controller snap inside one body's collision epsilon, not a
 * broad source tolerance or a teleport over geometry.
 */
static qboolean Ballistic_CanonicalizeSource(edict_t *e, const vec3_t source,
	const short fixed[3])
{
	short current[3];
	trace_t tr;
	int i;

	if (!Ballistic_SourceRest(e))
		return false;
	for (i = 0; i < 3; i++)
	{
		current[i] = (short)(e->s.origin[i] * 8.0f);
		if (abs((int)current[i] - (int)fixed[i]) > 16)
			return false;
	}
	tr = sg_host.trace(e->s.origin, e->mins, e->maxs, source, e,
	                   MASK_PLAYERSOLID);
	if (tr.startsolid || tr.allsolid || tr.fraction < 1.0f)
		return false;

	VectorCopy(source, e->s.origin);
	VectorClear(e->velocity);
	for (i = 0; i < 3; i++)
	{
		e->client->ps.pmove.origin[i] = fixed[i];
		e->client->ps.pmove.velocity[i] = 0;
	}
	e->client->old_pmove = e->client->ps.pmove;
	sg_host.linkentity(e);
	return true;
}

/* Mirror the candidate-time P_FallingDamage reserve at the final exact-source
 * arm.  Combat can change health during the bounded staging walk. */
qboolean SG_BallisticSurvivable(edict_t *e, const rune_link_t *l)
{
	rune_t *r = SG_Rune();
	float height, gravity, launch, delta;
	int damage;

	if (!e || !l || !r ||
	    (l->action != RL_JUMP && l->action != RL_DROP) ||
	    !SG_RunePhysicsCompatible(r))
		return false;
	if (r->seeds[l->to].flags & RSF_WATER)
	{
		int contents = sg_host.pointcontents(r->seeds[l->to].origin);

		/* A fully submerged water landing cancels falling damage. Slime and
		 * lava share MASK_WATER and movement semantics, but not survivability. */
		return !(contents & (CONTENTS_SLIME | CONTENTS_LAVA));
	}
	if (deathmatch && deathmatch->value && dmflags &&
	    ((int)dmflags->value & DF_NO_FALLING))
		return true;
	height = r->seeds[l->from].origin[2] - r->seeds[l->to].origin[2];
	gravity = r->artifact.identity.gravity;
	launch = (l->action == RL_JUMP) ? 270.0f : 0.0f;
	/* Arrival permits the body up to 72 units below the destination seed.
	 * Include that full envelope and a jump's upward launch energy; this is a
	 * survival gate, not a precise damage quote. */
	delta = (launch * launch + 2.0f * gravity * (height + 72.0f)) * 0.0001f;
	if (delta < 0.0f)
		delta = 0.0f;
	damage = delta > 30.0f ? (int)((delta - 30.0f) * 0.5f) : 0;
	if (delta > 30.0f && damage < 1)
		damage = 1;          /* P_FallingDamage's production minimum */
	return e->health > damage + SG_DROP_HEALTH_RESERVE;
}

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

static void Drop_LiveEventsClear(sg_bot_t *bot)
{
	if (!bot)
		return;
	memset(&bot->drop_live_events, 0, sizeof(bot->drop_live_events));
}

static sg_drop_live_events_t Drop_LiveEventsTake(sg_bot_t *bot)
{
	sg_drop_live_events_t events;

	memset(&events, 0, sizeof(events));
	if (bot)
	{
		events = bot->drop_live_events;
		Drop_LiveEventsClear(bot);
	}
	return events;
}

static void Drop_LiveObserveDoorPassage(sg_bot_t *bot, const edict_t *e)
{
	if (!bot || !e)
		return;
	if (!bot->drop_replay_active)
		return;
	if (SG_OracleReplayDoorPassage(bot->drop_live_step_origin, e->s.origin))
		(void)SG_DropLiveEventsLatch(&bot->drop_live_events, false, true);
	/* Preserve segmented history.  Boundary owns only the later pusher motion,
	 * never a chord that combines command four with that motion. */
	VectorCopy(e->s.origin, bot->drop_live_step_origin);
}

/* Independent shadow of the final live writer.  The callback is
 * intentionally expressed from reducer state after its handoff latch so a
 * one-field drift in either implementation is visible before ClientThink. */
static qboolean Drop_LiveShadowCommand(
	const sg_drop_replay_state_t *state, const sg_replay_pose_t *pose,
	usercmd_t *command)
{
	vec3_t direction;
	short yaw_command;
	byte msec;

	if (!state || !pose || !command)
		return false;
	msec = command->msec;
	memset(command, 0, sizeof(*command));
	command->msec = msec;
	if (msec != SG_REPLAY_STEP_MS)
		return false;
	if (state->recovery)
	{
		VectorSubtract(state->spec.destination, pose->origin, direction);
		if (!SG_DropReplayPlanarYawCommand(direction[0], direction[1],
		        pose->pms.delta_angles[YAW], &yaw_command))
			return false;
	}
	else if (state->walkoff)
		yaw_command = ANGLE2SHORT(
		    state->spec.heading * (360.0f / 256.0f)) -
		    pose->pms.delta_angles[YAW];
	else
	{
		VectorSubtract(state->spec.lip, pose->origin, direction);
		if (!SG_DropReplayPlanarYawCommand(direction[0], direction[1],
		        pose->pms.delta_angles[YAW], &yaw_command))
			return false;
	}
	command->angles[PITCH] = -pose->pms.delta_angles[PITCH];
	command->angles[YAW] = yaw_command;
	command->angles[ROLL] = -pose->pms.delta_angles[ROLL];
	command->forwardmove = 400;
	return true;
}

static void Drop_LiveSync(sg_bot_t *bot)
{
	if (!bot)
		return;
	bot->drop_walkoff = bot->drop_replay.walkoff;
	bot->drop_airborne = bot->drop_replay.airborne;
	bot->drop_recover = bot->drop_replay.recovery;
}

static void Drop_LiveResultLog(const edict_t *e, int link_index,
	const char *phase, const sg_drop_live_result_t *result)
{
	if (!result || result->outcome == SG_DROP_LIVE_RUNNING ||
	    result->outcome == SG_DROP_LIVE_ARRIVED || !sg_cv.debug->value)
		return;
	sg_host.dprint("DROPREPLAY%s %s link=%d phase=%s adapter=%s replay=%s\n",
	    result->outcome == SG_DROP_LIVE_FAILED ? "FAIL" : "FALLBACK",
	    e && e->client ? e->client->pers.netname : "?", link_index,
	    phase ? phase : "?", SG_DropLiveFailureName(result->failure),
	    SG_ReplayReasonName(result->replay_reason));
}

static void Drop_LiveResetAction(sg_bot_t *bot)
{
	if (!bot)
		return;
	bot->drop_link = -1;
	bot->drop_started = false;
	bot->drop_walkoff = false;
	bot->drop_airborne = false;
	bot->drop_recover = false;
	Drop_LiveEventsClear(bot);
	SG_DropLiveReset(&bot->drop_replay, &bot->drop_replay_active,
	    &bot->drop_replay_link, &bot->drop_live_events);
}

static void Drop_LiveCanonicalFail(edict_t *e, sg_bot_t *bot,
	int link_index, const char *phase, sg_replay_reason_t reason)
{
	int b, oldest = 0;

	if (!bot)
		return;
	if (link_index >= 0)
	{
		for (b = 0; b < SG_BL_MAX; b++)
			if (bot->bl_until[b] < bot->bl_until[oldest])
				oldest = b;
		bot->bl_link[oldest] = link_index;
		SG_TimerArm(&bot->bl_until[oldest],
		    SG_DROP_LIVE_FAILURE_SHELF_SECONDS);
		if (reason == SG_REPLAY_REASON_RECOVERY_LOST)
		{
			SG_TeachLinkFutility(link_index);
		}
	}
	bot->commit_link = -1;
	Drop_LiveResetAction(bot);
	if (sg_cv.debug->value)
		sg_host.dprint("DROPREPLAYFAIL %s link=%d phase=%s "
		               "adapter=canonical replay=%s\n",
		    e && e->client ? e->client->pers.netname : "?", link_index,
		    phase ? phase : "?", SG_ReplayReasonName(reason));
}

/* Adapter/identity/cadence drift is not evidence against the serialized link.
 * Retire its command ownership without poisoning graph policy. */
static void Drop_LiveIntegrationAbort(sg_bot_t *bot)
{
	if (!bot)
		return;
	bot->commit_link = -1;
	Drop_LiveResetAction(bot);
}

static void Drop_LiveRetireNonRunning(edict_t *e, sg_bot_t *bot,
	int link_index, const char *phase, const sg_drop_live_result_t *result)
{
	if (!result || result->outcome == SG_DROP_LIVE_RUNNING)
		return;
	if (result->outcome == SG_DROP_LIVE_FAILED)
		Drop_LiveCanonicalFail(e, bot, link_index, phase,
		                       result->replay_reason);
	else
		Drop_LiveIntegrationAbort(bot);
}

/* Air-strafe commands split rotation between view and input so the requested
 * wish direction survives view slew. Movement still passes through pmove;
 * this code never writes velocity. */
typedef sg_human_speed_air_t sg_air_t;

#define SG_AIR_ACCEL SG_HUMAN_SPEED_AIR_ACCEL

/* Derive the smallest off-velocity wish angle that keeps PM_Accelerate at its
 * per-step cap, leaning toward the route turn. */
static void SG_Strafe(usercmd_t *cmd, vec3_t fwd, vec3_t right,
                      vec3_t vel, vec3_t dir,
                      float speed2d, float frametime, float accel)
{
	vec3_t	vdir, d;
	float	wishspeed = 300.0f;     /* pm_maxspeed clamps wishspeed to this */
	float	accelspeed, c, th, sn, cs, cross;

	if (speed2d < 1.0f)
		return;

	/* The optional air-gain mode uses PM_AirAccelerate's 30-unit clamp. */
	if (accel < 5.0f && sg_cv.airgain->value)
		wishspeed = 30.0f;

	accelspeed = accel * frametime * wishspeed;

	/* dose 1 read NEGATIVE (296): the honest ~84-degree air lean turns
	 * the velocity off the route and the nav corrections eat more than
	 * the harvest pays. Dose 2 caps the lean at ~40 degrees: partial
	 * gain that stays roughly route-aligned. */
	#define SG_AIRLEAN_CAP 0.70f

	/*
	 * Below wishspeed - accelspeed there is no angle to find: addspeed is
	 * already saturated pointing straight down the route, so the input that
	 * accelerates hardest is also the one that steers, and leaning off it
	 * would only trade heading for nothing. Leave the caller's plain forward
	 * alone -- this is the whole of the low-speed case, and it is why the
	 * strafe is not a mode the bot enters and leaves.
	 */
	if (speed2d <= wishspeed - accelspeed)
		return;

	c = (wishspeed - accelspeed) / speed2d;
	if (c > 1.0f) c = 1.0f;
	if (c < -1.0f) c = -1.0f;
	th = acosf(c);

	vdir[0] = vel[0] / speed2d;
	vdir[1] = vel[1] / speed2d;
	vdir[2] = 0.0f;

	/* lean the way the route turns, so the gain also steers */
	cross = vdir[0] * dir[1] - vdir[1] * dir[0];
	if (cross < 0.0f)
		th = -th;

	if (accel < 5.0f &&
	    sg_cv.airgain->value >= 2 &&
	    th > SG_AIRLEAN_CAP)
		th = SG_AIRLEAN_CAP;

	sn = sinf(th);
	cs = cosf(th);
	d[0] = vdir[0] * cs - vdir[1] * sn;
	d[1] = vdir[0] * sn + vdir[1] * cs;
	d[2] = 0.0f;

	/*
	 * Decomposed against the basis pmove will actually build (pitch/3, see
	 * the caller), so the engine reconstructs the direction that was asked
	 * for. 400 on both axes before the clamp: wishvel is scaled down to
	 * pm_maxspeed anyway, and what matters is the direction.
	 */
	cmd->forwardmove = (short)(DotProduct(fwd, d) * 400.0f);
	cmd->sidemove = (short)(DotProduct(right, d) * 400.0f);
}

/*
 * The point `look` units of arc down the polyline org -> chain[0] -> ...
 * -> chain[n-1], measured horizontally. A runner does not stare at his
 * next footprint and not at the horizon either: he holds a point a fixed
 * stride-count down the road, and the road bends under it. The fixed
 * arc-distance is the whole of the anti-zigzag: the seed centers keep
 * arriving 3.2 times a second, but the point they define moves
 * continuously.
 */
static qboolean SG_PursuitPoint(vec3_t org, vec3_t chain[], int n,
                                float look, vec3_t out)
{
	vec3_t	cur;
	float	rem = look;
	int		i;

	if (n <= 0)
		return false;
	VectorCopy(org, cur);
	for (i = 0; i < n; i++)
	{
		vec3_t	seg;
		float	len;

		VectorSubtract(chain[i], cur, seg);
		seg[2] = 0.0f;
		len = VectorLength(seg);
		if (len < 1.0f)
		{
			VectorCopy(chain[i], cur);
			continue;
		}
		if (len >= rem)
		{
			float f = rem / len;

			out[0] = cur[0] + (chain[i][0] - cur[0]) * f;
			out[1] = cur[1] + (chain[i][1] - cur[1]) * f;
			out[2] = cur[2] + (chain[i][2] - cur[2]) * f;
			return true;
		}
		rem -= len;
		VectorCopy(chain[i], cur);
	}
	VectorCopy(cur, out);       /* chain ran out: aim at its far end */
	return true;
}

/*
 * Standing inside `keep` of either flag stand. The chain's hard veto: a
 * body that is about to touch a flag needs to be able to stop on it, and
 * an air-strafe is a commitment to a heading for the length of a flight.
 * Both stands, not just the goal one -- arriving at speed and leaving at
 * speed are the same mistake at the same place.
 */
static qboolean SG_NearAFlag(edict_t *e, float keep)
{
	int	t;

	if (!SG_Rune())
		return false;
	for (t = 0; t < 2; t++)
	{
		int		seed = t ? sg_fields.blue_flag_seed
		                 : sg_fields.red_flag_seed;
		vec3_t	fd;

		if (seed < 0)
			continue;
		VectorSubtract(SG_Rune()->seeds[seed].origin, e->s.origin, fd);
		if (VectorLength(fd) < keep)
			return true;
	}
	return false;
}

static qboolean SG_RunRoom(edict_t *e, int seed0, const int *route_field,
                           vec3_t dir, float want)
{
	vec3_t	chain[SG_PURSUIT_MAX], pp, pend, ch;
	trace_t	tr;
	float	acc = 0.0f, len;
	int		n = 0, cs = seed0;

	if (!SG_Rune() || cs < 0 || !route_field)
		return false;

	VectorCopy(SG_Rune()->seeds[cs].origin, chain[n]);
	n++;
	while (n < SG_PURSUIT_MAX && acc < want)
	{
		int			li5, nx5 = -1, nv5 = route_field[cs];
		rune_link_t	*l5;
		vec3_t		sgd;

		if (nv5 >= SG_FIELD_INF)
			break;
		for (li5 = SG_Rune()->first_link[cs]; li5 >= 0;
		     li5 = SG_Rune()->next_link[li5])
		{
			int candidate_ms;

			l5 = &SG_Rune()->links[li5];
			if (l5->action != RL_RUN)
				continue;
			if (l5->anchor[0] != 0.0f || l5->anchor[1] != 0.0f ||
			    l5->anchor[2] != 0.0f)
				continue;
			candidate_ms = SG_RouteCandidateGoalMs(route_field[l5->to],
			    Fields_LinkTraversalCostMs(l5), SG_FIELD_INF);
			if (candidate_ms < nv5)
			{
				nv5 = candidate_ms;
				nx5 = li5;
			}
		}
		if (nx5 < 0)
			break;
		cs = SG_Rune()->links[nx5].to;
		VectorCopy(SG_Rune()->seeds[cs].origin, chain[n]);
		VectorSubtract(chain[n], chain[n - 1], sgd);
		sgd[2] = 0.0f;
		acc += VectorLength(sgd);
		n++;
	}

	if (!SG_PursuitPoint(e->s.origin, chain, n, want, pp))
		return false;

	VectorSubtract(pp, e->s.origin, ch);
	ch[2] = 0.0f;
	len = VectorLength(ch);
	if (len < want * SG_AS_CHORD)
		return false;               /* the road bends inside the window */
	if ((ch[0] * dir[0] + ch[1] * dir[1]) / len <
	    cosf(SG_AS_BEND * (float)M_PI / 180.0f))
		return false;               /* and it has to go where we are going */

	/* the room, at the fan's own z-allowance: STEPSIZE, so stairs and
	 * ramps are road and not wall */
	pend[0] = pp[0];
	pend[1] = pp[1];
	pend[2] = e->s.origin[2] + 18.0f;
	tr = sg_host.trace(e->s.origin, e->mins, e->maxs, pend, e, MASK_PLAYERSOLID);
	/* A teammate is not terrain (the fan's exception, same reason). An
	 * opponent is still the exact physical obstruction the road proof must
	 * reject, including before reaction delay admits combat ownership. */
	if (tr.fraction < 1.0f && tr.ent &&
	    SG_TeammateBodyPassable(e->client->ctf.teamnum,
	        tr.ent->client != NULL, tr.ent->deadflag != 0,
	        tr.ent->client ? tr.ent->client->ctf.teamnum : 0))
		return true;
	return tr.fraction >= 1.0f;
}

static void SG_MovePolicy(edict_t *e, usercmd_t *cmd, vec3_t fwd,
                          vec3_t right, vec3_t dir,
                          qboolean open_ahead, qboolean run_link,
                          float frametime, const sg_air_t *air)
{
	float	sp2, sp, toward;
	int		pmf = e->client->ps.pmove.pm_flags;

	if (e->waterlevel > 1 || (pmf & PMF_DUCKED))
		return;

	sp2 = e->velocity[0] * e->velocity[0] + e->velocity[1] * e->velocity[1];
	if (sp2 < 200.0f * 200.0f)
		return;                 /* below this, straight ahead is the fastest
		                         * thing there is: addspeed is wide open */
	sp = sqrtf(sp2);

	/*
	 * The strafe leans off the direction of TRAVEL, so travel has to be
	 * roughly where the route wants to go before leaning off it means
	 * anything. A bot that needs to turn ninety degrees should turn, not
	 * harvest acceleration into the wall it is heading for.
	 */
	toward = (e->velocity[0] * dir[0] + e->velocity[1] * dir[1]) / sp;
	if (toward < 0.5f)
		return;

	if (e->groundentity)
	{
		/*
		 * Tap, never hold: PM_CheckJump sets PMF_JUMP_HELD when it fires and
		 * refuses every jump after it until a command arrives with upmove
		 * under 10. The caller releases after every step.
		 */
		/* Start chains near the 300-unit ground cap. An active chain jumps on
		 * the first legal grounded sub-step to avoid friction. */
		if (run_link && open_ahead && !(pmf & PMF_TIME_LAND) &&
		    (sp > 270.0f || (air && air->chain)))
			cmd->upmove = 400;

		SG_Strafe(cmd, fwd, right, e->velocity, dir, sp, frametime, 10.0f);
	}
	else
	{
		/* Hold jump while descending so a mid-command landing can relaunch.
		 * The lower speed threshold tolerates normal touchdown loss. */
		/* a chain holds the same jump for the same reason, without
		 * needing sg_landtick set: the hold IS the chain */
		if ((sg_cv.landtick->value ||
		     (air && air->chain)) &&
		    run_link && open_ahead &&
		    e->velocity[2] < 0.0f && sp > 240.0f)
			cmd->upmove = 400;

		if (air)
			SG_HumanSpeedAirCommand(cmd, air, e->velocity, sp, frametime);
		else
			SG_Strafe(cmd, fwd, right, e->velocity, dir, sp, frametime, 1.0f);
	}
}

static void SG_PlanBeam(vec3_t from, vec3_t to)
{
	sg_host.write_byte(svc_temp_entity);
	sg_host.write_byte(TE_BFG_LASER);
	sg_host.write_position(from);
	sg_host.write_position(to);
	sg_host.multicast(from, MULTICAST_ALL);
}

static void SG_DrawPlan(sg_bot_t *bot, int team, int link,
                        const int *route_field)
{
	edict_t	*e = bot->ent;
	vec3_t	a, b, c;
	int		dp, k, nx = -1;

	dp = (int)sg_cv.drawplan->value;
	if (!dp || !SG_Rune() || !e || !e->client || !e->inuse)
		return;
	if (dp > 0 && dp - 1 != (int)(e->client - game.clients))
		return;

	/*
	 * The committed link is the one this frame chose; when the final
	 * approach drops the link entirely (the last ten metres are a straight
	 * line) the incumbent is what the bot was last riding, and that is the
	 * honest thing to draw.
	 */
	if (link < 0)
		link = bot->sticky_link;

	VectorCopy(e->s.origin, a);
	a[2] += 16.0f;

	if (link >= 0 && link < SG_Rune()->hdr.num_links)
	{
		int to = SG_Rune()->links[link].to;

		VectorCopy(SG_Rune()->seeds[to].origin, b);
		b[2] += 16.0f;
		SG_PlanBeam(a, b);

		/* Extend the beam through the next descending edge so it shows the
		 * route the bot will take rather than a guessed continuation. */
		if (route_field)
		{
			int li2, nv = route_field[to];

			for (li2 = SG_Rune()->first_link[to]; li2 >= 0;
			     li2 = SG_Rune()->next_link[li2])
			{
				if (route_field[SG_Rune()->links[li2].to] < nv)
				{
					nv = route_field[SG_Rune()->links[li2].to];
					nx = li2;
				}
			}
		}
		if (nx >= 0)
		{
			VectorCopy(SG_Rune()->seeds[SG_Rune()->links[nx].to].origin, c);
			c[2] += 16.0f;
			SG_PlanBeam(b, c);
		}
	}

	/* the belief the route was priced against: one post per sighting still
	 * inside the staleness window (sg_caco.c owns the table) */
	if (team == CTF_TEAM_RED || team == CTF_TEAM_BLUE)
	{
		for (k = 0; k < SG_MAX_ENEMY_TRACK; k++)
		{
			sg_belief_enemy_t *en = &sg_caco_enemies[SG_TeamIdx(team)][k];

			if (en->client < 0 || en->seed < 0 ||
			    en->seed >= SG_Rune()->hdr.num_seeds)
				continue;
			if (SG_AgeOver(en->seen_time, SG_BELIEF_STALE))
				continue;
			VectorCopy(SG_Rune()->seeds[en->seed].origin, b);
			VectorCopy(b, c);
			c[2] += 72.0f;
			SG_PlanBeam(b, c);
		}
	}
}

static qboolean GenericRailMoveAllowed(const sg_bot_t *bot, const sg_think_t *tc)
{
	return bot && tc && SG_StrikeGenericRailAllowed(tc->strike_active) &&
	    SG_DefenseSupplyGenericRetryAllowed(
	        (sg_defense_supply_phase_t)bot->def_supply_phase, bot->def_supply_armed);
}

static void DirectTouchClaimMovement(sg_bot_t *bot, const edict_t *e,
	sg_think_t *tc, qboolean touch_terminal)
{
	if (!touch_terminal)
		return;
	bot->escape_until = 0.0f;
	bot->stuck_time = 0.0f;
	bot->hook_landbrake = 0.0f;
	VectorCopy(e->s.origin, bot->stuck_origin);
	tc->hold_post = false;
	tc->rally_hold = false;
	tc->rail_hold = false;
}

static qboolean EnemyFlagTouchMissionActive(qboolean strike_pressure,
	qboolean scoop_mission)
{
	return SG_EnemyFlagTouchMissionActive(strike_pressure, scoop_mission);
}

typedef enum
{
	SG_DOOR_DRIVE_NONE,
	SG_DOOR_DRIVE_WAIT,
	SG_DOOR_DRIVE_RETREAT,
	SG_DOOR_DRIVE_FORWARD
} sg_door_drive_t;

void Think_Move(sg_bot_t *bot, sg_think_t *tc)
{
	usercmd_t *cmd = &tc->cmd;
	edict_t *e = tc->e;
	sg_role_t role = tc->role;
	int team = tc->team;
	const int *goal_field = tc->goal_field;
	const int *route_field = tc->route_field;
	int bestlink = tc->bestlink;
	qboolean precision = tc->precision;
	qboolean hold_post = tc->hold_post;
	qboolean rally_hold = tc->rally_hold;
	qboolean rail_hold = tc->rail_hold;
	float post_yaw = tc->post_yaw;
	qboolean duel = tc->duel;

	vec3_t move_dir;
	float view_yaw = tc->view_yaw, view_pitch = tc->view_pitch;
	qboolean have_move = false, open_ahead = false, run_link = false;
	qboolean touch_terminal = false;
	sg_door_drive_t door_hold = SG_DOOR_DRIVE_NONE;
	edict_t *door_ent = NULL;
	edict_t *ordered_escort = (role == SG_ROLE_ESCORT)
	    ? SG_ChatEscortTarget(e) : NULL;
	qboolean escort_terminal_hold = ordered_escort &&
	    SG_EscortTerminal(e, ordered_escort);
	qboolean drop_yaw_locked = false;
	double drop_yaw = 0.0;
	qboolean hook_brake = false;
	qboolean jump_brake = false, jump_slow = false;
	qboolean ballistic_abort = false;
	qboolean declared_door_link = bestlink >= 0 && SG_Rune() &&
	    (SG_Rune()->links[bestlink].action == RL_DOOR ||
	     SG_Rune()->links[bestlink].action == RL_BUTTON_DOOR);
	vec3_t want, d;

	VectorClear(move_dir);
	VectorClear(want);
	VectorClear(d);

		vec3_t aim;
		qboolean have_aim = false;
		qboolean aim_is_anchor = false;
		qboolean jump_now = false;
		edict_t *terminal_flag = NULL;

		/*
		 * Just let go of a rope: the prover steered forwardmove 400 at the
		 * destination until the phantom grounded (SG_Rune().c:529-534), and
		 * the link was only recorded because that landing worked. The body
		 * flies the same approach instead of falling back down the wall it
		 * just climbed.
		 */
		/* flying the arc: the landing is the aim, as with a cut rope */
		if (bot->rocketjump.phase == SG_ROCKETJUMP_FLIGHT)
		{
			int axis;

			for (axis = 0; axis < 3; axis++)
				aim[axis] =
				    bot->rocketjump.witness.destination_q8[axis] * 0.125f;
			have_aim = true;
		}

		/* During ballistic flight, turn toward the best onward link from the
		 * landing seed without changing the trajectory. */
		if (sg_cv.preturn->value &&
		    ((bot->hook_phase == 3 && bot->flow_release) ||
		     bot->rocketjump.phase == SG_ROCKETJUMP_FLIGHT) &&
		    !e->groundentity && SG_Rune())
		{
			vec3_t ballistic_destination;
			int axis;
			int ls;

			if (bot->rocketjump.phase == SG_ROCKETJUMP_FLIGHT)
				for (axis = 0; axis < 3; axis++)
					ballistic_destination[axis] =
					    bot->rocketjump.witness.destination_q8[axis] * 0.125f;
			else
				VectorCopy(bot->hook_dest, ballistic_destination);
			ls = Rune_NearestSeed(SG_Rune(), ballistic_destination);

			if (ls >= 0)
			{
				int link_index, next_link = -1, next_value;
				const int *preturn_route_field = route_field
				    ? route_field : goal_field;

				next_value = (preturn_route_field[ls] < SG_FIELD_INF)
				    ? preturn_route_field[ls] : 0x7fffffff;
				for (link_index = SG_Rune()->first_link[ls];
				     link_index >= 0;
				     link_index = SG_Rune()->next_link[link_index])
				{
					rune_link_t *candidate =
					    &SG_Rune()->links[link_index];
					int candidate_ms;

					if (candidate->action != RL_RUN)
						continue;
					candidate_ms = SG_RouteCandidateGoalMs(
					    preturn_route_field[candidate->to],
					    Fields_LinkTraversalCostMs(candidate), SG_FIELD_INF);
					if (candidate_ms < next_value)
					{
						next_value = candidate_ms;
						next_link = link_index;
					}
				}
				if (next_link >= 0)
				{
					/* One step faces the landing toward its next tile without
					 * steering the current flight across a corridor corner. */
					VectorCopy(
					    SG_Rune()->seeds[
					        SG_Rune()->links[next_link].to].origin,
					    aim);
					have_aim = true;
				}
			}
		}

		if (bot->hook_phase == 3)
		{
			/*
			 * APEX CHAINING: hook short of the lip, cut, and at the top
			 * of the throw the next rope is already legal -- the classic
			 * chain (named exactly by the owner). A flow-cut flight ends
			 * its phase at the apex (vertical speed dying, still
			 * airborne), the surface argues the next step from the air,
			 * and if that step is a rope it fires right there.
			 */
			if (bot->flow_release && !e->groundentity &&
			    e->velocity[2] < 60.0f)
			{
				(void)SG_HookDiagnosticsFinish(&bot->hook_diagnostics,
				    "apex", "flow-release");
				SG_SpeedHookReleaseFinish(bot);
				/* ropetravel: a clean apex is a link in the chain --
				 * the next rope is legal on the next beat */
				if (sg_cv.ropetravel->value > 0.0f)
					SG_TimerArm(&bot->speedhook_next, 0.25f);
				if (sg_cv.debug->value)
					sg_host.dprint("HOOKFLOW %s apex\n",
					           e->client->pers.netname);
			}
			else if (e->groundentity || SG_TimerReadyStrict(bot->hook_deadline))
			{
				(void)SG_HookDiagnosticsFinish(&bot->hook_diagnostics,
				    e->groundentity ? "landed" : "landing_timeout",
				    "phase3");
				if (sg_cv.debug->value)
				{
					if (e->groundentity)
					{
						vec3_t ld;

						VectorSubtract(bot->hook_dest, e->s.origin, ld);
						sg_host.dprint("HOOKLAND %s dist=%.0f dz=%.0f\n",
						           e->client->pers.netname,
						           sqrtf(ld[0] * ld[0] + ld[1] * ld[1]),
						           ld[2]);
					}
					else
						/* deadline hit still airborne: the throw
						 * never came down anywhere useful */
						sg_host.dprint("HOOKFLOW %s drop\n",
						           e->client->pers.netname);
				}
				if (!bot->flow_release)
					SG_TimerArm(&bot->hook_landbrake, 0.3f);
				SG_SpeedHookReleaseFinish(bot);
				/*
				 * HOOK PING-PONG SHELF (sg_hookpong, movement behavior revisit
				 *): 29% of all back-and-forth events sit
				 * on three HOOK-heavy spans at 8-45x the human rate --
				 * grapple-decision oscillation, not field flatness,
				 * which is why pricing immediate returns had nothing
				 * to bite on. If this landing puts the body back where
				 * the PREVIOUS ride departed within 8s, the ridden
				 * link joins the shelf ring exactly like a failed
				 * anchor: the pair stops flapping for 45s and the
				 * surface argues a different road.
				 */
				if (sg_cv.hookpong->value > 0.0f &&
				    bot->hp_prev_land > 0.0f &&
				    SG_AgeUnder(bot->hp_prev_land, 8.0f) &&
				    bot->hook_link >= 0)
				{
					vec3_t hpd;

					VectorSubtract(e->s.origin, bot->hp_prev_dep, hpd);
					if (VectorLength(hpd) < 250.0f)
					{
						int b9, old9 = 0;

						for (b9 = 0; b9 < SG_BL_MAX; b9++)
							if (bot->bl_until[b9] < bot->bl_until[old9])
								old9 = b9;
						bot->bl_link[old9] = bot->hook_link;
						SG_TimerArm(&bot->bl_until[old9], 45.0f);
						SG_TeachLinkFutility(bot->hook_link);
						if (sg_cv.debug->value)
							sg_host.dprint("HOOKPONG %s link=%d\n",
							           e->client->pers.netname,
							           bot->hook_link);
					}
				}
				VectorCopy(bot->hp_cur_dep, bot->hp_prev_dep);
				SG_Mark(&bot->hp_prev_land);

				/* ropetravel: a grounded landing chains too -- the beat
				 * is slightly longer than the apex's because the legs
				 * carry a step before the next throw */
				if (sg_cv.ropetravel->value > 0.0f && e->groundentity)
					SG_TimerArm(&bot->speedhook_next, 0.35f);

				/*
				 * A ride that did not SERVE the field failed, and a
				 * failed anchor gets shelved on the spot. Without this,
				 * sibling anchors flap (each landing re-argues, picks
				 * the other, neither converts) and the 4s same-link
				 * watch never fires -- smap05's attackers rode ropes in
				 * place for 180 seconds a game at the water's edge.
				 */
				if (bot->hook_link >= 0 &&
				    bot->hook_link < SG_Rune()->hdr.num_links &&
				    bot->seed >= 0 &&
				    route_field[bot->seed] < SG_FIELD_INF)
				{
					rune_link_t *hl = &SG_Rune()->links[bot->hook_link];

					if (route_field[hl->to] < SG_FIELD_INF &&
					    route_field[bot->seed] >
					        route_field[hl->to] + 300)
					{
						int b, oldest = 0;

						for (b = 0; b < SG_BL_MAX; b++)
							if (bot->bl_until[b] < bot->bl_until[oldest])
								oldest = b;
						bot->bl_link[oldest] = bot->hook_link;
						SG_TimerArm(&bot->bl_until[oldest], 60.0f);
						if (sg_cv.debug->value)
							sg_host.dprint("HOOKFAIL %s link=%d\n",
							           e->client->pers.netname,
							           bot->hook_link);
					}
				}
				bot->hook_link = -1;
			}
			else
			{
				VectorCopy(bot->hook_dest, aim);
				have_aim = true;
			}
		}

		/*
		 * The terminal approach is a physical touch, not a graph preference.
		 * Do this before a committed RUN, ribbon, cover, or homeward lookahead
		 * can aim beside the live item.  Reset only a plain RUN commitment;
		 * declared/ballistic controllers retain their own authority.
		 */
		if (!have_aim && EnemyFlagTouchMissionActive(
		        tc->strike_pressure, tc->scoop_mission) &&
		    bot->hook_phase == 0 &&
		    !SG_RocketJumpGameOwns(bot) && bot->nade_phase == 0 &&
		    SG_AttackFlagTerminalAim(e, team, aim, &terminal_flag))
		{
			have_aim = true;
			touch_terminal = true;
			bestlink = -1;
			tc->bestlink = -1;
			rally_hold = false;
			tc->rally_hold = false;
			if (bot->commit_link >= 0 &&
			    bot->commit_link < SG_Rune()->hdr.num_links &&
			    SG_Rune()->links[bot->commit_link].action == RL_RUN)
			{
				bot->commit_link = -1;
				bot->commit_until = 0.0f;
			}
			/* Exact touch authority has already proved the item and clear hull
			 * line. Tighten only a fast misaligned turn so full throttle cannot
			 * carry the body around the pickup it is aiming through. */
			if (sg_cv.termbrake->value)
				SG_FlagTouchBrake(bot, e, terminal_flag->s.origin, true);
		}

		if (!have_aim && bestlink >= 0)
		{
			rune_link_t *l = &SG_Rune()->links[bestlink];
			VectorCopy(SG_Rune()->seeds[l->to].origin, aim);
			if (sg_cv.ribbon->value > 0.0f &&
			    SG_RouteRibbonAllowed(role == SG_ROLE_CARRY || tc->route_pure,
			        EnemyFlagTouchMissionActive(tc->strike_pressure,
			            tc->scoop_mission)) &&
			    l->action == RL_RUN && bot->ribbon_off != 0.0f)
			{
				vec3_t rdir, roff, rprobe;
				trace_t rtr;
				VectorSubtract(aim, e->s.origin, rdir);
				rdir[2] = 0.0f;
				if (VectorLength(rdir) > 32.0f)
				{
					float rl = VectorLength(rdir);

					roff[0] = -rdir[1] / rl * bot->ribbon_off;
					roff[1] = rdir[0] / rl * bot->ribbon_off;
					roff[2] = 0.0f;
					VectorAdd(aim, roff, rprobe);
					rtr = sg_host.trace(e->s.origin, e->mins, e->maxs, rprobe,
					               e, MASK_PLAYERSOLID);
					/* A blocked offset collapses to the seed line. */
					if (rtr.fraction >= 1.0f)
						VectorCopy(rprobe, aim);
				}
			}
			have_aim = true;

			/* Near a RUN destination, aim through its complete cheapest RUN. */
			if (sg_cv.lookahead->value &&
			    !sg_cv.pursuit->value &&
			    l->action == RL_RUN && !precision)
			{
				vec3_t nd0;

				VectorSubtract(SG_Rune()->seeds[l->to].origin,
				               e->s.origin, nd0);
				nd0[2] = 0.0f;
				if (VectorLength(nd0) < 160.0f)
				{
					int li2, nx = -1, nv = route_field[l->to];

					for (li2 = SG_Rune()->first_link[l->to];
					     li2 >= 0; li2 = SG_Rune()->next_link[li2])
					{
						rune_link_t *l2 = &SG_Rune()->links[li2];
						int cv;
						if (l2->action != RL_RUN)
							continue;
						cv = SG_RouteCandidateGoalMs(route_field[l2->to],
						    Fields_LinkTraversalCostMs(l2), SG_FIELD_INF);
						if (cv <= nv)
						{
							nv = cv;
							nx = li2;
						}
					}
					if (nx >= 0)
						VectorCopy(
						    SG_Rune()->seeds[SG_Rune()->links[nx].to].origin,
						    aim);
				}
			}
			/*
			 * A RUN link with a stored waypoint is one whose proof had to
			 * ROUND something -- the oracle's detour apex lives in the
			 * anchor (empty since the format was born, now earning rent).
			 * Steer via it until it is done, then at the destination; the
			 * fan still handles the last arm's-length. This is the body
			 * finally walking the line the proof actually walked, instead
			 * of the chord the proof never claimed.
			 */
			if (l->action == RL_RUN &&
			    (l->anchor[0] != 0.0f || l->anchor[1] != 0.0f ||
			     l->anchor[2] != 0.0f))
			{
				vec3_t wd;

				VectorSubtract(l->anchor, e->s.origin, wd);
				wd[2] = 0.0f;
				if (VectorLength(wd) > 48.0f)
				{
					VectorCopy(l->anchor, aim);
					aim_is_anchor = true;
				}
			}

			/* Aim along the complete cheapest RUN chain. */
			if (sg_cv.pursuit->value > 0.0f &&
			    !aim_is_anchor && l->action == RL_RUN && !precision &&
			    e->waterlevel < 2 && bot->hook_phase == 0 &&
			    !SG_RocketJumpGameOwns(bot))
			{
				vec3_t	chain[SG_PURSUIT_MAX];
				float	look = sg_cv.pursuit->value;
				int		nchain = 0, cs = l->to, k;
				float	acc = 0.0f;

				VectorCopy(SG_Rune()->seeds[cs].origin, chain[nchain]);
				nchain++;
				while (nchain < SG_PURSUIT_MAX && acc < look)
				{
					int li5, nx5 = -1, nv5 = route_field[cs];
					vec3_t sgd;

					if (nv5 >= SG_FIELD_INF)
						break;
					for (li5 = SG_Rune()->first_link[cs]; li5 >= 0;
					     li5 = SG_Rune()->next_link[li5])
					{
						rune_link_t *l5 = &SG_Rune()->links[li5];
						int cv5;

						/* Do not chord exact actions. */
						if (l5->action != RL_RUN)
							continue;
						/* A proved detour must retain its waypoint. */
						if (l5->anchor[0] != 0.0f ||
						    l5->anchor[1] != 0.0f ||
						    l5->anchor[2] != 0.0f)
							continue;
						cv5 = SG_RouteCandidateGoalMs(route_field[l5->to],
						    Fields_LinkTraversalCostMs(l5), SG_FIELD_INF);
						if (cv5 <= nv5)
						{
							nv5 = cv5;
							nx5 = li5;
						}
					}
					if (nx5 < 0)
						break;
					cs = SG_Rune()->links[nx5].to;
					VectorCopy(SG_Rune()->seeds[cs].origin, chain[nchain]);
					VectorSubtract(chain[nchain], chain[nchain - 1], sgd);
					sgd[2] = 0.0f;
					acc += VectorLength(sgd);
					nchain++;
				}

				/* Shorten the pursuit chord until the player hull fits. */
				for (k = nchain; k >= 1; k--)
				{
					vec3_t pp, pend;
					trace_t ptr;

					if (!SG_PursuitPoint(e->s.origin, chain, k, look, pp))
						continue;
					pend[0] = pp[0];
					pend[1] = pp[1];
					/* sg_pursuitz: chord z-allowance. 8 vetoed every
					 * stair and ramp (311: guard collapsed the chord
					 * to the seed center most ticks); 18 is STEPSIZE */
					pend[2] = e->s.origin[2] +
					          (sg_cv.pursuitz->value);
					ptr = sg_host.trace(e->s.origin, e->mins, e->maxs, pend,
					               e, MASK_PLAYERSOLID);
					/* a teammate is not terrain, and a door is not a
					 * wall (the fan's two exceptions, same reasons) */
					if (ptr.fraction < 1.0f && ptr.ent &&
					    ((ptr.ent->client && !ptr.ent->deadflag) ||
					     (ptr.ent->classname &&
					      strncmp(ptr.ent->classname, "func_door",
					              9) == 0)))
						ptr.fraction = 1.0f;
					if (ptr.fraction >= 1.0f)
					{
						VectorCopy(pp, aim);
						/* guard census: k==nchain is the full chord,
						 * k==1 is a collapse to today's behavior */
						if (sg_cv.debug->value &&
						    SG_TimerReady(bot->next_report - 0.9f))
							sg_host.dprint("PURSUITK %s k=%d n=%d\n",
							           e->client->pers.netname,
							           k, nchain);
						break;
					}
				}
			}
			/* Reapply the per-leg lateral offset after lookahead chooses the final
			 * road point. Collision and fall guards still bound the offset. */
			if (sg_cv.edgeride->value > 0.0f &&
			    l->action == RL_RUN && !precision &&
			    e->groundentity && bot->hook_phase == 0 &&
			    tc->role != SG_ROLE_CARRY &&
			    bot->ribbon_off != 0.0f)
			{
				vec3_t edir, eoff, eprobe;
				trace_t etr;
				float el, esc;

				VectorSubtract(aim, e->s.origin, edir);
				edir[2] = 0.0f;
				el = VectorLength(edir);
				if (el > 48.0f)
				{
					esc = sg_cv.edgeride->value /
					      ((sg_cv.ribbon->value
					        > 0.0f)
					       ? sg_cv.ribbon->value
					       : 48.0f);
					eoff[0] = -edir[1] / el * bot->ribbon_off * esc;
					eoff[1] = edir[0] / el * bot->ribbon_off * esc;
					eoff[2] = 0.0f;
					VectorAdd(aim, eoff, eprobe);
					etr = sg_host.trace(e->s.origin, e->mins, e->maxs,
					               eprobe, e, MASK_PLAYERSOLID);
					if (etr.fraction >= 1.0f)
						VectorCopy(eprobe, aim);
				}
			}

			if (l->action == RL_JUMP && e->groundentity)
			{
				/*
				 * A momentum link's proof entered at 320 u/s and jumped
				 * off that speed; hopping without it lands in the gap.
				 * Hold the run until the body carries most of what the
				 * envelope claims (from-rest links claim zero and hop
				 * as they always did).
				 */
				vec3_t js, source_fixed;
				short source_pms[3];
				qboolean source_exact, source_rest, source_snapped = false;
				float jh = sqrtf(e->velocity[0] * e->velocity[0] +
				                 e->velocity[1] * e->velocity[1]);
				float jdist, jyaw, jdelta, jslack;

				if (bot->jump_link != bestlink)
				{
					bot->jump_link = bestlink;
					bot->jump_started = false;
				}

				Ballistic_SourceFixed(l, source_fixed, source_pms);
				VectorSubtract(source_fixed, e->s.origin, js);
				jdist = sqrtf(js[0] * js[0] + js[1] * js[1]);
				source_exact = Ballistic_SourceExact(e, source_pms);
				source_rest = Ballistic_SourceRest(e);
				/* A body can be one quantized step across a water/support boundary
				 * from the dry proof source. Capture first when the sweep is clear,
				 * then spend one zero-input ClientThink to classify the snapped body;
				 * launching against the pre-snap water/ground cache would be stale. */
				if (!source_exact && source_rest && jdist <= 2.0f &&
				    fabsf(js[2]) <= 2.0f &&
				    Ballistic_CanonicalizeSource(e, source_fixed, source_pms))
				{
					source_exact = true;
					source_snapped = true;
				}
				if (source_snapped || bot->hook_phase != 0 ||
				    e->client->hookstate != 0 || e->client->hook != NULL ||
				    SG_RocketJumpGameOwns(bot) ||
				    bot->nade_phase != 0 ||
				    e->client->ps.pmove.pm_time != 0 ||
				    (e->client->ps.pmove.pm_flags &
				     (PMF_JUMP_HELD | PMF_DUCKED)) ||
				    e->movetype == MOVETYPE_NOCLIP || e->s.modelindex != 255 ||
				    e->deadflag || e->waterlevel >= 2 ||
				    (e->groundentity != g_edicts &&
				     !SG_ImmutableSupport(e->groundentity)))
				{
					/* The proof's first command is a fresh tap. One zero-upmove
					 * command releases a prior hop before this action may launch. */
					jump_brake = true;
				}
				else if (l->min_speed == 0)
				{
					/* The common jump proof starts at the exact source and at rest.
					 * Do not launch it with whatever 500-u/s cross-route momentum
					 * happened to enter the seed cell. Center, brake, then tap. */
					if (!source_exact)
					{
						VectorCopy(source_fixed, aim);
						if (jdist <= 2.0f && fabsf(js[2]) <= 2.0f)
							jump_brake = true;
						else if (jdist < 32.0f && fabsf(js[2]) < 8.0f)
							jump_slow = true;
					}
					else if (!source_rest)
						jump_brake = true;
					else if (!SG_BallisticSurvivable(e, l))
					{
						bot->commit_link = -1;
						bot->jump_link = -1;
						bot->jump_started = false;
						ballistic_abort = true;
					}
					else
						jump_now = true;
				}
				else if (jdist > 6.0f || fabsf(js[2]) > 4.0f)
				{
					/* Momentum proofs also start at the fixed source. Preserve the
					 * incoming run while centering; do not tap from an arbitrary point
					 * in the seed's Euclidean Voronoi cell. */
					VectorCopy(SG_Rune()->seeds[l->from].origin, aim);
				}
				else if (jh >= (float)(l->min_speed * 4))
				{
					jyaw = atan2f(e->velocity[1], e->velocity[0]) *
					       180.0f / (float)M_PI;
					jdelta = jyaw - l->heading * (360.0f / 256.0f);
					while (jdelta > 180.0f) jdelta -= 360.0f;
					while (jdelta < -180.0f) jdelta += 360.0f;
					jslack = l->heading_slack * (360.0f / 256.0f);
					if (fabsf(jdelta) <= jslack)
						jump_now = true;
				}
				if (jump_now)
					tc->jump_launch = true;
			}
			/* the landing hop belongs on running ground, not on a link
			 * whose traversal is itself a jump, a drop, a rope or a swim */
			if (l->action == RL_RUN)
			{
				run_link = true;

				/* Use a short grapple burst on a clear, uncontested run. */

				if (bot->hook_phase == 0 && !bot->engaged_last &&
				    SG_TimerReady(bot->speedhook_next) &&
				    e->groundentity && e->waterlevel == 0 &&
				    goal_field[bot->seed] >
				        ((sg_cv.freeride->value > 0.0f ||
				          sg_cv.ropetravel->value > 0.0f)
				             ? 2000 : 4000))
				{

					float hsp2 = e->velocity[0] * e->velocity[0]
					           + e->velocity[1] * e->velocity[1];
					float hcap = (sg_cv.ropetravel->value > 0.0f) ? 700.0f :
					             (sg_cv.freeride->value
					              > 0.0f) ? 560.0f : 480.0f;

					if (hsp2 > 220.0f * 220.0f && hsp2 < hcap * hcap)
					{
						vec3_t hd, heye, hend;
						trace_t htr;
						float hyaw;
						int hfan;

						VectorSubtract(aim, e->s.origin, hd);
						hyaw = atan2f(hd[1], hd[0]);
						heye[0] = e->s.origin[0];
						heye[1] = e->s.origin[1];
						heye[2] = e->s.origin[2] + e->viewheight;
						int hwander =
						    (sg_cv.ropetravel->value
						     >= 2.0f && (rand() % 7) == 0);

						for (hfan = 0; hfan < 3; hfan++)
						{
							float hy2 = hyaw + (hwander
							    ? ((hfan - 1) * 1.05f)
							    : ((hfan == 1) ? 0.384f :
							       (hfan == 2) ? -0.384f : 0.0f));
							float hfar = (sg_cv.freeride->value >= 2.0f) ? 300.0f : 480.0f;
							float hup  = (sg_cv.freeride->value >= 2.0f) ? 420.0f : 280.0f;

							hend[0] = heye[0] + cosf(hy2) * hfar;
							hend[1] = heye[1] + sinf(hy2) * hfar;
							hend[2] = heye[2] + hup;    /* shallow ~30deg, high ~54deg */
							htr = sg_host.trace(heye, NULL, NULL, hend, e,
							               MASK_SOLID);
							/* Hook enthusiasm lowers the minimum useful
							 * rope length. */
							if (htr.fraction >= 1.0f || htr.startsolid ||
							    htr.fraction * 560.0f <=
							        170.0f / SG_PersonaHookScale(e) ||
							    htr.plane.normal[2] >= 0.7f)
							{
								/* the side probes exist only under
								 * freeride; stock behavior is one look */
								if (sg_cv.freeride->value
								    > 0.0f ||
								    sg_cv.ropetravel->value > 0.0f)
									continue;
								break;
							}
							{
								/* A carrier keeps proved route momentum instead of
								 * stopping to aim an optional speed rope. */
								if (!SG_CarrierEscapeActive(tc->role))
								{
									VectorCopy(htr.endpos, bot->hook_anchor);
									VectorCopy(aim, bot->hook_dest);
									VectorCopy(e->s.origin, bot->hp_cur_dep);
									bot->hook_phase = 1;
									SG_TimerArm(&bot->hook_deadline, 1.0f);
									bot->speedhook = true;
									bot->speedhook_pull_applied = false;
									SG_TimerArm(&bot->speedhook_next,
									    ((sg_cv.ropetravel->value > 0.0f) ? 1.0f :
									     (sg_cv.freeride->value > 0.0f) ? 2.0f : 4.0f)
									    / SG_PersonaHookScale(e));
								}
							}
							break;
						}
					}
				}
			}

			/*
			 * A hook link executes the way the rune proved it: aim at the
			 * STORED anchor, fire, ride the flat-800 pull, release near
			 * the destination or inside the brake band (the p_weapon.c
			 * ladder starts at 120), then steer the fall onto the landing.
			 * The view is the aim: LMCTF's Weapon_Hook_Fire fires along
			 * v_angle.
			 */
			if (l->action == RL_HOOK && bot->hook_phase == 0 &&
			    !SG_RocketJumpGameOwns(bot) && bot->nade_phase == 0 &&
			    SG_HookOffhandReady(e))
			{
				qboolean source_water =
				    (SG_Rune()->seeds[l->from].flags & RSF_WATER) != 0;
				qboolean destination_water =
				    (SG_Rune()->seeds[l->to].flags & RSF_WATER) != 0;
				qboolean live_hazard =
				    (e->watertype & (CONTENTS_LAVA | CONTENTS_SLIME)) != 0;
				qboolean live_dry = e->waterlevel == 0 &&
				    (e->groundentity == g_edicts ||
				     SG_ImmutableSupport(e->groundentity));
				qboolean live_water = e->waterlevel >= 2 &&
				    (e->watertype & CONTENTS_WATER) && !live_hazard;
				qboolean air_safe = e->waterlevel < 3 ||
				    SG_TimerRemaining(e->air_finished) >=
				        ((role == SG_ROLE_CARRY) ? 8.0f : 4.0f);
				float hspd = sqrtf(e->velocity[0] * e->velocity[0] +
				                   e->velocity[1] * e->velocity[1]);

				vec3_t fsd;
				float fsdist, fsz;

				if (!SG_HookStageSourceCompatible(source_water,
				        destination_water, live_dry, live_water, air_safe))
				{
					/* The edge remains valid from its proved source.  This body is not
					 * in that source state, so release the commitment, force fresh
					 * localization, and spend no generic command toward the landing. */
					SG_StagedTraversalCancel(bot, RL_HOOK);
					bot->seed = -1;
					ballistic_abort = true;
					goto hook_stage_done;
				}

				VectorSubtract(SG_Rune()->seeds[l->from].origin,
				               e->s.origin, fsd);
				fsz = fsd[2];
				fsd[2] = 0.0f;
				fsdist = VectorLength(fsd);

				/* Reach the proved source state before firing the grapple. */
				/* The current proof approaches the source cell and brakes before taking
				 * ownership of the exact view. The final post-Pmove fire gate then
				 * re-proves from the actual fixed-point source; it does not pretend
				 * every position in this cell shares the nominal seed rollout. */
				if (fsdist > 20.0f || fabsf(fsz) > 16.0f ||
				    (!source_water && !e->groundentity) ||
				    (e->client->ps.pmove.pm_flags & PMF_DUCKED) ||
				    e->client->ps.pmove.pm_time != 0 ||
				    fabsf(e->viewheight - 22.0f) > 0.1f)
				{
					VectorCopy(SG_Rune()->seeds[l->from].origin, aim);
					have_aim = true;
				}
				else if (!source_water &&
				         (hspd > 1.0f || fabsf(e->velocity[2]) > 1.0f))
				{
					hook_brake = true;      /* fire next frame, slower */
					VectorCopy(SG_Rune()->seeds[l->from].origin, aim);
					have_aim = true;
				}
				else
				{
					vec3_t proof_source, proof_muzzle, proof_bite;
					sg_hook_ride_worth_t worth = SG_HookCurrentRideWorth(
					    route_field[l->from], route_field[l->to],
					    Fields_LinkTraversalCostMs(l));
					proof_source[0] = (short)(SG_Rune()->seeds[l->from].origin[0]
					                  * 8.0f) * 0.125f;
					proof_source[1] = (short)(SG_Rune()->seeds[l->from].origin[1]
					                  * 8.0f) * 0.125f;
					proof_source[2] = (short)(SG_Rune()->seeds[l->from].origin[2]
					                  * 8.0f) * 0.125f;
					if (!SG_HookRideLaunchAllowed(worth))
					{
						Hook_DisciplineRetire(e, bot, bestlink, 5.0f, false,
						    worth == SG_HOOK_RIDE_UNASSESSED
						        ? "value-unassessed" : "value-skip",
						    route_field[l->from], route_field[l->to]);
					}
					else if (SG_HookControlDecode(proof_source, 22.0f, RIGHT_HANDED,
					                         l->anchor, bot->hook_view,
					                         proof_muzzle, proof_bite))
					{
						VectorCopy(proof_source, bot->hook_source);
						VectorCopy(proof_bite, bot->hook_anchor);
						VectorCopy(SG_Rune()->seeds[l->to].origin,
						           bot->hook_dest);
						bot->hook_link = bestlink;
						bot->hook_bite_logged = false;
						bot->hook_attached_validated = false;
						bot->hook_phase = 1;
						/* This is the aim deadline only. Successful fire replaces
						 * it with a quantized bolt-flight deadline; attachment then
						 * starts a fresh three-second pull budget. */
						SG_TimerArm(&bot->hook_deadline, 3.0f);
					}
					else
					{
						Hook_DisciplineRetire(e, bot, bestlink, 5.0f, true,
						    "decode-retire", 0, 0);
					}
				}
			hook_stage_done: ;
			}

			/* a drop link goes via its stored lip, not the far endpoint */
			if (l->action == RL_DROP)
			{
				vec3_t lipd, walk;
				float liph, behind;

				if (bot->drop_link != bestlink)
				{
					bot->drop_link = bestlink;
					bot->drop_started = false;
					bot->drop_walkoff = false;
					bot->drop_airborne = false;
					bot->drop_recover = false;
					SG_DropLiveReset(&bot->drop_replay,
					    &bot->drop_replay_active, &bot->drop_replay_link,
					    &bot->drop_live_events);
				}
				/* The body remains on safe ground during the proved lip approach.
				 * Damage received after arming may revoke the descent until the
				 * actual walkoff; abort while that choice is still recoverable. */
				if (bot->drop_started && !bot->drop_walkoff && e->groundentity &&
				    !SG_BallisticSurvivable(e, l))
				{
					bot->commit_link = -1;
					bot->drop_link = -1;
					bot->drop_started = false;
					bot->drop_walkoff = false;
					bot->drop_airborne = false;
					bot->drop_recover = false;
					SG_DropLiveReset(&bot->drop_replay,
					    &bot->drop_replay_active, &bot->drop_replay_link,
					    &bot->drop_live_events);
					ballistic_abort = true;
				}
				if (!bot->drop_started && !ballistic_abort)
				{
					vec3_t source_delta, source_fixed;
					short source_pms[3];
					float source_horiz;
					qboolean source_exact, source_rest, source_snapped = false;

					Ballistic_SourceFixed(l, source_fixed, source_pms);
					VectorSubtract(source_fixed, e->s.origin, source_delta);
					source_horiz = sqrtf(source_delta[0] * source_delta[0] +
					                     source_delta[1] * source_delta[1]);
					source_exact = Ballistic_SourceExact(e, source_pms);
					source_rest = Ballistic_SourceRest(e);
					if (!source_exact && source_rest && source_horiz <= 2.0f &&
					    fabsf(source_delta[2]) <= 2.0f &&
					    Ballistic_CanonicalizeSource(e, source_fixed, source_pms))
					{
						source_exact = true;
						source_snapped = true;
					}
					drop_yaw_locked = true;
					drop_yaw = atan2f(source_delta[1], source_delta[0]) *
					           180.0f / M_PI;
					if (source_snapped || bot->hook_phase != 0 ||
					    e->client->hookstate != 0 || e->client->hook != NULL ||
					    SG_RocketJumpGameOwns(bot) ||
				    bot->nade_phase != 0 ||
				    (e->groundentity != g_edicts &&
				     !SG_ImmutableSupport(e->groundentity)) ||
					    e->movetype == MOVETYPE_NOCLIP || e->s.modelindex != 255 ||
					    e->deadflag || e->waterlevel >= 2 ||
					    e->client->ps.pmove.pm_time != 0 ||
					    (e->client->ps.pmove.pm_flags & PMF_DUCKED))
						jump_brake = true;
					else
					{
						if (!source_exact)
						{
							VectorCopy(source_fixed, aim);
							if (source_horiz <= 2.0f &&
							    fabsf(source_delta[2]) <= 2.0f)
								jump_brake = true;
							else if (source_horiz < 32.0f)
								jump_slow = true;
						}
						else if (!source_rest)
							jump_brake = true;
						else if (!SG_BallisticSurvivable(e, l))
						{
							/* The edge was safe when selected, but damage during
							 * staging changed that fact. Replan before walking off. */
							bot->commit_link = -1;
							bot->drop_link = -1;
							bot->drop_started = false;
							bot->drop_walkoff = false;
							bot->drop_airborne = false;
							bot->drop_recover = false;
							SG_DropLiveReset(&bot->drop_replay,
							    &bot->drop_replay_active,
							    &bot->drop_replay_link, &bot->drop_live_events);
							ballistic_abort = true;
						}
						else
						{
							sg_drop_live_events_t live_events;
							sg_replay_pose_t live_pose;
							sg_drop_live_result_t live_result;
							qboolean source_contaminated = false;
							qboolean source_door = false;

							Drop_LivePose(e, &live_pose);
							Drop_LiveEventsClear(bot);
							if (!SG_OracleReplaySourceEvents(e, &source_contaminated,
							        &source_door))
								(void)SG_DropLiveEventsLatch(&bot->drop_live_events,
								    true, false);
							else
								(void)SG_DropLiveEventsLatch(&bot->drop_live_events,
								    source_contaminated, source_door);
							live_events = Drop_LiveEventsTake(bot);
							live_result = SG_DropLiveBegin(&bot->drop_replay,
							    &bot->drop_replay_active, &bot->drop_replay_link,
							    bestlink, SG_Rune()->seeds[l->to].origin, l->anchor,
							    l->heading,
							    (SG_Rune()->seeds[l->to].flags & RSF_WATER) != 0,
							    l->cost_ms, &live_pose, Drop_LiveSupportValid(e),
							    e->client->oldvelocity[2], &live_events);
							Drop_LiveResultLog(e, bestlink, "begin", &live_result);
							if (live_result.outcome == SG_DROP_LIVE_RUNNING)
							{
								/* Begin rejects contamination only.  The frame context carries
								 * source door evidence across the first command-event clear. */
								tc->drop_source_door_pending = source_door;
								bot->drop_started = true;
								SG_TimerArm(&bot->commit_until,
								    SG_REPLAY_DROP_TOTAL_MS * 0.001f);
							}
							else
							{
								Drop_LiveRetireNonRunning(e, bot, bestlink, "begin",
								                          &live_result);
								/* No legacy command may consume a reducer rejection or
								 * adapter drift in the same frame. Think_Emit spends the
								 * complete production frame as four zero commands. */
								ballistic_abort = true;
								tc->think_over = true;
							}
						}
					}
				}
				VectorSubtract(l->anchor, e->s.origin, lipd);
				lipd[2] = 0.0f;
				liph = VectorLength(lipd);
				walk[0] = cosf(l->heading * (2.0f * (float)M_PI / 256.0f));
				walk[1] = sinf(l->heading * (2.0f * (float)M_PI / 256.0f));
				walk[2] = 0.0f;
				behind = DotProduct(lipd, walk);
				/* Think_Move runs at 10 Hz while Pmove substeps at 25 ms. A fast
				 * body can cross the eight-unit handoff entirely between Think calls;
				 * signed progress (past the lip) and airborne state are authoritative
				 * evidence that the handoff occurred. */
				if (bot->drop_started && !bot->drop_walkoff &&
				    (liph <= 8.0f || behind <= 0.0f || !e->groundentity))
					bot->drop_walkoff = true;
				/*
				 * The whole drop executes the way the continuous prover walked it:
				 * aim at the lip until the <=8-unit handoff, then hold the
				 * RECORDED heading (dd_last_heading, SG_Rune().c:734). The
				 * fan, given a railing beside the gap, deflects off the
				 * exact line the proof demonstrated -- Phase orbited a
				 * balcony's proven drops 60-140 units from their lips,
				 * with a 96-unit lock radius it never entered.
				 */
				if (bot->drop_started)
				{
					drop_yaw_locked = true;
					if (!bot->drop_walkoff)
						drop_yaw = atan2f(lipd[1], lipd[0]) * 180.0f / M_PI;
					else
						drop_yaw = l->heading * (360.0f / 256.0f);
				}
			}
			else
			{
				bot->drop_link = -1;
				bot->drop_started = false;
				bot->drop_walkoff = false;
				bot->drop_airborne = false;
				bot->drop_recover = false;
				SG_DropLiveReset(&bot->drop_replay,
				    &bot->drop_replay_active, &bot->drop_replay_link,
				    &bot->drop_live_events);
			}
		}

		/* while aiming to fire, the cmd angles ARE the anchor bearing --
		 * this overrides the navigation view for exactly one frame */
		if (bot->hook_phase == 1)
		{
			vec3_t shot;
			float ay, ap;

			/* RL_HOOK stores the eye-space control, not the muzzle ray's
			 * resulting bite. Reconstruct the same quantized command the active
			 * generator traced. */
			if (!bot->speedhook && bot->hook_link >= 0)
				VectorCopy(bot->hook_view, shot);
			else if (!SG_HookAimAngles(e->s.origin, e->viewheight,
			                               bot->hook_anchor, shot))
				VectorClear(shot);
			ay = shot[YAW];
			ap = shot[PITCH];
			cmd->angles[YAW] = ANGLE2SHORT(ay)
			                - e->client->ps.pmove.delta_angles[YAW];
			cmd->angles[PITCH] = ANGLE2SHORT(ap)
			                - e->client->ps.pmove.delta_angles[PITCH];
			view_yaw = ay;
			view_pitch = ap;
		}
		else if (bot->seed >= 0 &&
		         goal_field[bot->seed] >= SG_FIELD_INF)
		{
			/* Rejoin the nearest finite field seed instead of orbiting the goal. */
			int best = Rune_NearestFieldSeed(SG_Rune(), e->s.origin,
			    goal_field);

			if (best >= 0)
				VectorCopy(SG_Rune()->seeds[best].origin, aim);
			else
				VectorCopy(e->s.origin, aim);
			have_aim = true;
		}
		if (!have_aim)
		{
			/* last resort: the goal itself, by belief */
			edict_t *gf = NULL;
			int terminal_seed = -1;
			/* An item route can end at a pad seed before the physical pickup.
			 * Cross the final body-length to the exact live item before generic
			 * role homing turns the bot back toward a flag. */
			touch_terminal = SG_WeaponPickupTarget(bot,
			    tc->strike_weapon_pursuit, aim) || Lead_PickupTarget(bot, aim) ||
			    SG_MegaPickupTarget(tc, aim);
			if (touch_terminal)
				have_aim = true;
			/* Terminal homing uses the live flag entity rather than its spawn
			 * marker, which may be offset after droptofloor. */
			if (!have_aim && SG_OrderedEscortDirectAimAllowed(
			        ordered_escort != NULL, escort_terminal_hold))
			{
				VectorCopy(ordered_escort->s.origin, aim);
				have_aim = true;
				/* "Cover me" terminates at the named teammate, not at our
				 * own flag stand. Hold a useful body-length away instead of
				 * grinding into them; combat remains free to own view/fire. */
			}
			else if (!have_aim && role == SG_ROLE_CARRY)
			{
				qboolean flag_at_home = false;
				qboolean direct_touch = false;

				gf = SG_OwnFlag(team);
				if (gf)
					flag_at_home = ctf_flagathome(gf);
				if (flag_at_home)
					direct_touch =
					    SG_OwnHomeFlagDirectTouchAuthority(e, team, &gf);
				else if (gf)
					direct_touch =
					    SG_OwnDroppedFlagDirectTouchAuthority(e, team, &gf);
				if (!SG_StrikeCarrierOwnFlagAimAllowed(gf != NULL,
				    flag_at_home, direct_touch))
					gf = NULL;
				else if (direct_touch)
					touch_terminal = true;
			}
			else if (!have_aim && role == SG_ROLE_RECOVER)
			{
				touch_terminal =
				    SG_OwnDroppedFlagDirectTouchAuthority(e, team, &gf);
				if (!touch_terminal)
				{
					terminal_seed = SG_TerminalFieldSeed(SG_Rune(),
					    goal_field, bot->seed);
					if (terminal_seed >= 0)
					{
						VectorCopy(SG_Rune()->seeds[terminal_seed].origin,
						    aim);
						have_aim = true;
					}
				}
			}
			else if (!have_aim && role == SG_ROLE_DEFEND)
			{
				/* Defense may terminate at a corpus post, rail lane, live
				 * intercept, or the exact weapon/home half of a supply sortie.
				 * Falling back unconditionally to the flag stand discards that
				 * selected mission at the field minimum. */
				terminal_seed = SG_TerminalFieldSeed(SG_Rune(), goal_field,
				    bot->seed);
				if (terminal_seed >= 0)
				{
					VectorCopy(SG_Rune()->seeds[terminal_seed].origin, aim);
					have_aim = true;
				}
			}
			else if (!have_aim && role == SG_ROLE_ESCORT &&
			         !tc->scoop_mission)
			{
				/* An autonomous screen terminates at the selected moving
				 * carrier/formation field.  A human cover order was resolved to
				 * its exact teammate above; SCOOP retains its distinct dropped-
				 * flag belief below. */
				terminal_seed = SG_TerminalFieldSeed(SG_Rune(), goal_field,
				    bot->seed);
				if (terminal_seed >= 0)
				{
					VectorCopy(SG_Rune()->seeds[terminal_seed].origin, aim);
					have_aim = true;
				}
			}

			if (!have_aim && !gf && tc->scoop_mission)
			{
				edict_t *enemy_item = NULL;

				/* The relay follows its admitted dropped-flag belief until an
				 * exact item touch replaces it.  The enemy home stand is not a
				 * fallback for an astray flag. */
				if (SG_AttackFlagDirectTouchAuthority(e, team, &enemy_item))
					gf = enemy_item;
				else
				{
					terminal_seed = SG_TerminalFieldSeed(SG_Rune(),
					    goal_field, bot->seed);
					if (terminal_seed >= 0)
					{
						VectorCopy(SG_Rune()->seeds[terminal_seed].origin,
						    aim);
						have_aim = true;
					}
				}
			}
			else if (!have_aim && !gf && tc->rune_handoff_route)
			{
				terminal_seed = SG_TerminalFieldSeed(SG_Rune(), goal_field,
				    bot->seed);
				if (terminal_seed >= 0)
				{
					VectorCopy(SG_Rune()->seeds[terminal_seed].origin, aim);
					have_aim = true;
				}
			}
			else if (!have_aim && !gf && tc->strike_pressure)
			{
				int ti = SG_TeamIdx(team);
				int ei = SG_TeamIdx(SG_EnemyTeam(team));
				if (sg_caco_team_belief.flag[ti][ei].state == SG_FLAG_ASTRAY)
				{
					/* The strategy field was admitted from this team's dropped-
					 * flag belief. At its minimum, keep walking to that same
					 * source; substituting the empty home stand here discarded the
					 * objective on the final graphless body-length. */
					terminal_seed = SG_TerminalFieldSeed(SG_Rune(), goal_field,
					    bot->seed);
					if (terminal_seed >= 0)
					{
						VectorCopy(SG_Rune()->seeds[terminal_seed].origin, aim);
						have_aim = true;
					}
				}
				else
				{
					/* The home stand position is common knowledge. */
					edict_t *marker = SG_FlagStand(team, false);
					edict_t *enemy_item = NULL;

					gf = marker;
					/* Direct-touch authority selects the live flag entity only after
					 * proving perception, floor alignment, and hull clearance. */
					if (SG_AttackFlagDirectTouchAuthority(e, team, &enemy_item))
						gf = enemy_item;
				}
			}
			else if (!have_aim && !gf)
			{
				gf = SG_FlagStand(team, true);
			}
			if (gf)
			{
				VectorCopy(gf->s.origin, aim);
				have_aim = true;

				/*
				 * CAPTURE THROUGH: the same
				 * disease on the scoring touch -- converge, stop, cap.
				 * Inside 160 the carrier aims PAST its flag along the
				 * line it arrived on; the touch happens mid-stride.
				 */
				/* The attacker through-line is owned exclusively by
				 * SG_AttackFlagTerminalAim and its direct-touch proof.  This
				 * fallback retains capture behavior only; an attacker without
				 * that proof must never project through a stand marker. */
				if (role == SG_ROLE_CARRY && touch_terminal &&
				    bot->seed >= 0)
				{
					vec3_t fd7;
					float fl7;

					VectorSubtract(gf->s.origin, e->s.origin, fd7);
					fd7[2] = 0.0f;
					fl7 = VectorLength(fd7);
					if (fl7 > 1.0f && fl7 < 160.0f)
					{
						trace_t wtr;
						vec3_t wend;

						VectorScale(fd7, (fl7 + 150.0f) / fl7, fd7);
						VectorAdd(e->s.origin, fd7, wend);
						wend[2] = gf->s.origin[2] + 16.0f;
						/* flag against a wall (owner's edge case):
						 * the exit line stops where the room does --
						 * clamp the through-point at solid geometry,
						 * never closer than the flag itself */
						wtr = sg_host.trace(gf->s.origin, e->mins, e->maxs,
						               wend, e, MASK_PLAYERSOLID);
						if (wtr.fraction < 1.0f)
							VectorCopy(wtr.endpos, wend);
						VectorCopy(wend, aim);
						aim[2] = gf->s.origin[2];
					}
				}

				/* Throttle a misaligned carrier near the stand so its turn radius
				 * converges on the capture touch. */
				if (role == SG_ROLE_CARRY && touch_terminal &&
				    sg_cv.termbrake->value)
					SG_FlagTouchBrake(bot, e, gf->s.origin, true);
			}
		}

		DirectTouchClaimMovement(bot, e, tc, touch_terminal);
		hold_post = tc->hold_post;
		rally_hold = tc->rally_hold;
		rail_hold = tc->rail_hold;
		if (have_aim)
		{
			vec3_t probe;
			trace_t tr;
			float best_open = -1.0e30f;
			float base_yaw, chosen_yaw;
			int k;

			VectorSubtract(aim, e->s.origin, want);
			base_yaw = atan2f(want[1], want[0]) * 180.0f / M_PI;
			chosen_yaw = base_yaw;

			/*
			 * Feelers: try the goal heading first, then fan out. Take the
			 * most open heading nearest the goal line. This is what makes
			 * the local gradient walk around a doorframe instead of into
			 * it.
			 */
			if (!touch_terminal)
			{
			static const float fan_dense[11] = { 0, -15, 15, -30, 30, -60,
			                                     60, -100, 100, -145, 145 };
			static const float fan_base[9]   = { 0, -30, 30, -60, 60, -100,
			                                     100, -145, 145 };
			const float *fan = sg_cv.fandense->value
			                   ? fan_dense : fan_base;
			int fan_n = sg_cv.fandense->value ? 11 : 9;

			for (k = 0; k < fan_n; k++)
			{
				sg_feeler_probe_t feeler;
				float score, clearance;

				float reach = SG_MoveFeelerReach(e);
				feeler = SG_FeelerProbe(e, team, base_yaw + fan[k], reach,
				    k == 0);
				if (feeler.teammate_blocked)
				{
					bot->mate_block_last = true;
					base_yaw = feeler.yaw;
				}
				/* Closed doors need a direct approach to reach the trigger.
				 * Button-only failures are retired by progress shelving. */
				if (!declared_door_link && feeler.trace.fraction < 1.0f &&
				    feeler.trace.ent && feeler.trace.ent->classname &&
				    strncmp(feeler.trace.ent->classname, "func_door", 9) == 0)
				{
					int dd;
					qboolean dead = false;

					/* a door that already refused to yield from here is a
					 * wall: no fraction override, the fan walks around */
					for (dd = 0; dd < SG_DEAD_DOORS; dd++)
						if (bot->dead_door[dd] == feeler.trace.ent &&
						    SG_TimerPending(bot->dead_door_until[dd]))
							dead = true;
					if (dead && k == 0)
					{
						bot->deaddoor_ahead = true;
						VectorCopy(feeler.trace.endpos, bot->deaddoor_spot);
					}
					if (!dead &&
					    feeler.trace.ent->moveinfo.state != SG_PLAT_STATE_TOP)
					{
						/* A body in a rotating door's arc blocks its swing.
						 * Moving rotating doors require retreat; sliding doors do not. */
						if (k == 0)
						{
							if (feeler.trace.ent->moveinfo.state ==
							    SG_PLAT_STATE_BOTTOM)
								door_hold = SG_DOOR_DRIVE_FORWARD;
							else if (!strcmp(feeler.trace.ent->classname,
							                 "func_door_rotating") &&
							         feeler.trace.fraction *
							             reach < 64.0f)
								door_hold = SG_DOOR_DRIVE_RETREAT;
							else
								door_hold = SG_DOOR_DRIVE_WAIT;
							door_ent = feeler.trace.ent;
						}
						feeler.trace.fraction = 1.0f;
					}
				}
				/* Score physical clearance with a symmetric turn cost. */
				clearance = feeler.trace.fraction * reach;
				score = clearance - fabsf(fan[k]) * 0.20f;
				/* Latch a detour side briefly to prevent obstacle-side flapping. */
				if (bot->fan_side && SG_TimerPending(bot->fan_side_until) &&
				    fan[k] * (float)bot->fan_side < 0.0f)
					score -= reach * 0.35f;
				if (score > best_open)
				{
					best_open = score;
					chosen_yaw = feeler.yaw;
				}
				if (feeler.trace.fraction >= 1.0f && k == 0)
				{
					bot->fan_side = 0;  /* goal line open: latch released */
					break;
				}
			}
			}
			if (chosen_yaw != base_yaw)
			{
				int side = (chosen_yaw > base_yaw) ? 1 : -1;

				if (bot->fan_side != side || SG_TimerReady(bot->fan_side_until))
					SG_TimerArm(&bot->fan_side_until, 0.7f);
				bot->fan_side = side;
			}

			/* Slew ordinary navigation headings to suppress fan-induced flapping.
			 * Combat, precision movement, hooks, and water retain snap turns. */
			if (!touch_terminal && sg_cv.smooth->value &&
			    !duel && !precision && bot->hook_phase == 0 &&
			    e->waterlevel < 2)
			{
				float sdt = SG_Age(bot->nav_yaw_t);
				float sdy = chosen_yaw - bot->nav_yaw_cur;
				/* A value of 1 selects the default 300 degrees per second. */
				float srate = sg_cv.smooth->value;

				if (srate <= 1.0f)
					srate = 300.0f;
				while (sdy > 180.0f) sdy -= 360.0f;
				while (sdy < -180.0f) sdy += 360.0f;
				if (sdt > 0.0f && sdt < 0.5f)
				{
					float cap = srate * sdt;

					if (sdy > cap) sdy = cap;
					else if (sdy < -cap) sdy = -cap;
					chosen_yaw = bot->nav_yaw_cur + sdy;
				}
				bot->nav_yaw_cur = chosen_yaw;
				SG_Mark(&bot->nav_yaw_t);
			}
			else
			{
				bot->nav_yaw_cur = chosen_yaw;
				SG_Mark(&bot->nav_yaw_t);
			}

			/* at a drop lip the proven walk-off heading overrides the fan:
			 * the proof is a line, and the line is the record's */
			if (drop_yaw_locked)
				chosen_yaw = drop_yaw;

			/* A speedhook keeps moving during its aim phase, so write the anchor
			 * bearing here instead of using the stationary hook path. */
			if (bot->hook_phase == 1 && bot->speedhook)
			{
				vec3_t sha;

				VectorSubtract(bot->hook_anchor, e->s.origin, sha);
				chosen_yaw = atan2f(sha[1], sha[0]) * 180.0f / (float)M_PI;
			}

			/*
			 * Rail mode: the retry that trusts the proof over the fan.
			 * Stage 1 walks to the link's from-seed (the proof's start);
			 * stage 2 drives the straight from->to line with the fan
			 * silenced -- pmove slides along the slit's edges exactly as
			 * the phantom's pmove did. Arrival, a better field value, or
			 * the clock ends it; a timeout hands the link to the shelf.
			 */
			if (GenericRailMoveAllowed(bot, tc) &&
			    bot->rail_stage > 0 && bestlink == bot->rail_link &&
			    bestlink >= 0)
			{
				rune_link_t *rl = &SG_Rune()->links[bestlink];
				vec3_t rd;

				if (SG_TimerReadyStrict(bot->rail_until) ||
				    bot->seed == rl->to)
				{
					if (SG_TimerReadyStrict(bot->rail_until) &&
					    bot->seed != rl->to)
					{
						int b2, old2 = 0;

						for (b2 = 0; b2 < SG_BL_MAX; b2++)
							if (bot->bl_until[b2] < bot->bl_until[old2])
								old2 = b2;
						bot->bl_link[old2] = bestlink;
						SG_TimerArm(&bot->bl_until[old2], 45.0f);
						bot->commit_link = -1;
						SG_TeachLinkFutility(bestlink);
						if (sg_cv.debug->value)
							sg_host.dprint("RAILFAIL %s link=%d seed=%d\n",
							           e->client->pers.netname,
							           bestlink, bot->seed);
					}
					else if (sg_cv.debug->value)
						sg_host.dprint("RAILWIN %s link=%d\n",
						           e->client->pers.netname, bestlink);
					bot->rail_stage = 0;
				}
				else if (bot->rail_stage == 1)
				{
					VectorSubtract(SG_Rune()->seeds[rl->from].origin,
					               e->s.origin, rd);
					rd[2] = 0.0f;
					if (VectorLength(rd) < 24.0f)
					{
						bot->rail_stage = 2;
						SG_TimerArm(&bot->rail_until, 3.0f);
					}
					else
						chosen_yaw = atan2f(rd[1], rd[0])
						             * 180.0f / (float)M_PI;
				}
				if (bot->rail_stage == 2)
				{
					VectorSubtract(SG_Rune()->seeds[rl->to].origin,
					               e->s.origin, rd);
					chosen_yaw = atan2f(rd[1], rd[0])
					             * 180.0f / (float)M_PI;
				}
			}
			else if (bot->rail_stage > 0)
				bot->rail_stage = 0;    /* the surface moved on: stand down */

			/* backing out of a pocket overrides everything but the lip:
			 * the retreat only ends early if the goal line opens up */
			if (SG_TimerPending(bot->escape_until) && !drop_yaw_locked)
			{
				if (best_open >= 1.0f && chosen_yaw == base_yaw)
					bot->escape_until = 0.0f;
				else
					chosen_yaw = bot->escape_yaw;
			}

			/* A normal grapple aim frame owns both view and stationary command. */
			if (bot->hook_phase == 1 && !bot->speedhook)
			{
				cmd->forwardmove = 0;
				cmd->sidemove = 0;
				cmd->upmove = 0;
				have_move = false;
			}
			else
			{
			float swim_pitch = 0.0f;

			/* Underwater movement needs the target's vertical view angle. */
			if (e->waterlevel > 1 && have_aim)
			{
				vec3_t wd;
				float wh;

				VectorSubtract(aim, e->s.origin, wd);
				wh = sqrtf(wd[0] * wd[0] + wd[1] * wd[1]);
				swim_pitch = -atan2f(wd[2], wh) * 180.0f / (float)M_PI;
				if (swim_pitch > 85.0f) swim_pitch = 85.0f;
				if (swim_pitch < -85.0f) swim_pitch = -85.0f;
			}
			else if (bot->hook_phase == 1 && bot->speedhook)
			{
				vec3_t wd;
				float wh;

				VectorSubtract(bot->hook_anchor, e->s.origin, wd);
				wd[2] -= e->viewheight;
				wh = sqrtf(wd[0] * wd[0] + wd[1] * wd[1]);
				swim_pitch = -atan2f(wd[2], wh) * 180.0f / (float)M_PI;
				if (swim_pitch > 85.0f) swim_pitch = 85.0f;
				if (swim_pitch < -85.0f) swim_pitch = -85.0f;
			}

			cmd->angles[YAW] = ANGLE2SHORT(chosen_yaw)
			                - e->client->ps.pmove.delta_angles[YAW];
			cmd->angles[PITCH] = ANGLE2SHORT(swim_pitch)
			                  - e->client->ps.pmove.delta_angles[PITCH];
			cmd->forwardmove = 400;
			if (jump_now)
				cmd->upmove = 400;

			view_yaw = chosen_yaw;
			view_pitch = swim_pitch;
			bot->nav_drove = true;
			}
			move_dir[0] = cosf(chosen_yaw * (float)M_PI / 180.0f);
			move_dir[1] = sinf(chosen_yaw * (float)M_PI / 180.0f);
			move_dir[2] = 0.0f;
			have_move = true;

			/*
			 * Room to hop into. A landing jump commits the bot to whatever
			 * speed and heading it left with for the whole arc, so it is only
			 * worth taking where the way ahead is actually clear -- the same
			 * player-box trace the feelers use, run further out along the
			 * heading that was chosen.
			 */
			/* sg_carryhop may shorten the carrier's forward-clearance distance. */
			{
				float hop_reach = 160.0f;
				if (tc->role == SG_ROLE_CARRY &&
				    sg_cv.carryhop->value > 0)
					hop_reach = sg_cv.carryhop->value;
				VectorMA(e->s.origin, hop_reach, move_dir, probe);
			}
			probe[2] += 8.0f;
			tr = sg_host.trace(e->s.origin, e->mins, e->maxs, probe,
			              e, MASK_PLAYERSOLID);
			/* same rule as the feelers: a door ahead is not a wall, but
			 * do NOT hop at one -- arrive on foot, inside its trigger */
			open_ahead = (tr.fraction >= 1.0f);

		}

		/* braking for a rope: kill the run so the fire happens from the
		 * standing start the proof used */
		if (hook_brake || jump_brake)
		{
			cmd->forwardmove = 0;
			cmd->sidemove = 0;
			cmd->upmove = 0;
			have_move = false;
			bot->nav_drove = false;
		}
		else if (jump_slow)
		{
			cmd->forwardmove = 40;
			cmd->sidemove = 0;
			cmd->upmove = 0;
			/* Preserve the source-centering course in world space. Think_Emit
			 * decomposes this exact 40 magnitude through the slewed view. */
			have_move = true;
			bot->nav_drove = true;
		}

		/* Hold an ordinary landing where the proof ended. A carrier runs out
		 * immediately because the live escape route still owns its legs. */
		if (SG_TimerPending(bot->hook_landbrake) && e->groundentity &&
		    !tc->jump_launch && !bot->jump_started && !bot->drop_started &&
		    !SG_CarrierEscapeActive(tc->role))
		{
			cmd->forwardmove = 0;
			cmd->sidemove = 0;
			cmd->upmove = 0;
			bot->nav_drove = false;
		}

		/* A feeler can lose a moving brush as soon as it leaves its closed
		 * pose.  The attempt still owns the legs until the same door reaches
		 * TOP (or the absolute budget expires); otherwise the next frame drives
		 * into a pusher that the previous frame deliberately waited for. */
			if (!declared_door_link && !door_hold && bot->door_hold_ent &&
		    bot->door_hold_ent->inuse &&
		    bestlink == bot->door_hold_link &&
		    bot->door_hold_ent->moveinfo.state != SG_PLAT_STATE_TOP)
		{
			door_ent = bot->door_hold_ent;
			if (door_ent->moveinfo.state == SG_PLAT_STATE_BOTTOM)
				door_hold = SG_DOOR_DRIVE_FORWARD;
			else if (door_ent->classname &&
			         !strcmp(door_ent->classname, "func_door_rotating"))
				door_hold = SG_DOOR_DRIVE_RETREAT;
			else
				door_hold = SG_DOOR_DRIVE_WAIT;
		}

		/* Door activation/motion owns the route command: drive into a closed
		 * activator, hold for a translating brush, or yield a rotating arc.
		 * keep facing it, and let the trigger under our feet do the work.
		 * The timeout includes the door's declared angular travel. Slow map
		 * doors legitimately need much longer than the old fixed 2.5 seconds;
		 * after the bounded travel budget expires, remember it as a wall for
		 * thirty seconds and let the surface reroute. */
			if (!declared_door_link && door_hold && have_move && e->groundentity &&
		    !bot->jump_started &&
		    (!bot->drop_started || !bot->drop_walkoff))
		{
			/* Doors were held open during generation. A closed rotating door is
			 * a transient precondition failure, not permission to submit a proved
			 * jump/drop with its first command erased. Defer the action while the
			 * normal door trigger/timeout policy opens or reprices the route. */
			tc->jump_launch = false;
			if (bot->drop_started && !bot->drop_walkoff && e->groundentity)
			{
				bot->drop_started = false;
				bot->drop_walkoff = false;
				bot->drop_airborne = false;
				bot->drop_recover = false;
				SG_DropLiveReset(&bot->drop_replay,
				    &bot->drop_replay_active, &bot->drop_replay_link,
				    &bot->drop_live_events);
			}
			cmd->forwardmove = (door_hold == SG_DOOR_DRIVE_RETREAT) ? -200
			                 : (door_hold == SG_DOOR_DRIVE_FORWARD ? 400 : 0);
			cmd->sidemove = 0;
			cmd->upmove = 0;
			bot->door_held_last = true;
			bot->nav_drove = false;

			{
				float door_wait = 2.5f;

				if (door_ent && isfinite(door_ent->moveinfo.distance) &&
				    isfinite(door_ent->moveinfo.speed) &&
				    door_ent->moveinfo.speed > 0.0f)
					door_wait = fabsf(door_ent->moveinfo.distance) /
					            door_ent->moveinfo.speed + 0.75f;
				if (door_wait < 2.5f) door_wait = 2.5f;
				if (door_wait > 12.0f) door_wait = 12.0f;
				/* Key the budget to the physical door AND committed link. A
				 * rotating brush can disappear from the next feeler while it
				 * swings, but that must not buy the same attempt a fresh timeout. */
				if (door_ent != bot->door_hold_ent ||
				    bestlink != bot->door_hold_link)
				{
					bot->door_hold_ent = door_ent;
					bot->door_hold_link = bestlink;
					bot->door_hold_deadline = level.time + door_wait;
				}
				if (level.time >= bot->door_hold_deadline)
				{
					int dd, oldest = 0;

					for (dd = 0; dd < SG_DEAD_DOORS; dd++)
						if (bot->dead_door_until[dd] <
						    bot->dead_door_until[oldest])
							oldest = dd;
					bot->dead_door[oldest] = door_ent;
					SG_TimerArm(&bot->dead_door_until[oldest], 30.0f);
					bot->door_hold_ent = NULL;
					bot->door_hold_link = -1;
					bot->door_hold_deadline = 0.0f;
					door_hold = SG_DOOR_DRIVE_WAIT;
					/* a door with no trigger on this side is one-way by the
					 * mapper's hand (lmctf03: both bd doors trigger only from
					 * the base side). The 30s memory reroutes THIS bot; the
					 * field funnels the rest of the team in behind it unless
					 * the corridor repricies globally. Same cure as the wall. */
					SG_TeachFutility(bot->seed);
					if (sg_cv.debug->value)
						sg_host.dprint("DEADDOOR %s at (%.0f %.0f %.0f)\n",
							           e->client->pers.netname, e->s.origin[0],
							           e->s.origin[1], e->s.origin[2]);
				}
			}
		}
			else
			{
				/* No door command owns this frame.  A completed/invalid attempt no
				 * longer receives the progress-watch exemption. */
				if (declared_door_link)
				{
					bot->door_hold_ent = NULL;
					bot->door_hold_link = -1;
					bot->door_hold_deadline = 0.0f;
					door_hold = SG_DOOR_DRIVE_NONE;
				}
				if (bot->door_hold_ent &&
			    (!bot->door_hold_ent->inuse ||
			     bestlink != bot->door_hold_link ||
			     bot->door_hold_ent->moveinfo.state == SG_PLAT_STATE_TOP))
			{
				bot->door_hold_ent = NULL;
				bot->door_hold_link = -1;
				bot->door_hold_deadline = 0.0f;
			}
			bot->door_held_last = false;
		}

		/* Low air overrides route following until the swimmer reaches a valid
		 * surface path. An active hook pull retains control. */
		if (e->waterlevel >= 3 && bot->hook_phase != 2 &&
		    SG_TimerRemaining(e->air_finished) <
		        ((role == SG_ROLE_CARRY) ? 8.0f : 4.0f))
		{
			int air_from = bot->seed;
			int an;

			/* A submerged body can still be localized to a dry shore seed. Use
			 * the last water state owned by the exact SWIM controller, or a direct
			 * water neighbor, before falling back to straight up. */
			if (air_from < 0 ||
			    !(SG_Rune()->seeds[air_from].flags & RSF_WATER))
			{
				if (bot->swim_air_seed >= 0 &&
				    bot->swim_air_seed < SG_Rune()->hdr.num_seeds &&
				    (SG_Rune()->seeds[bot->swim_air_seed].flags & RSF_WATER))
					air_from = bot->swim_air_seed;
				else if (bot->seed >= 0)
				{
					int ali;

					for (ali = SG_Rune()->first_link[bot->seed]; ali >= 0;
					     ali = SG_Rune()->next_link[ali])
					{
						rune_link_t *al = &SG_Rune()->links[ali];

						if (al->action == RL_SWIM &&
						    (SG_Rune()->seeds[al->to].flags & RSF_WATER))
						{
							air_from = al->to;
							break;
						}
					}
				}
			}
			an = (sg_airnext && air_from >= 0) ? sg_airnext[air_from] : -1;

			if (an >= 0)
			{
				/* swim the graph's way out, not the ceiling's */
				vec3_t ad;
				float ay, ap, al;

				VectorSubtract(SG_Rune()->seeds[an].origin, e->s.origin,
				               ad);
				al = VectorLength(ad);
				ay = atan2f(ad[1], ad[0]) * 180.0f / (float)M_PI;
				ap = (al > 1.0f)
				     ? -asinf(ad[2] / al) * 180.0f / (float)M_PI : -85.0f;
				cmd->angles[YAW] = ANGLE2SHORT(ay)
				                - e->client->ps.pmove.delta_angles[YAW];
				cmd->angles[PITCH] = ANGLE2SHORT(ap)
				                  - e->client->ps.pmove.delta_angles[PITCH];
				view_pitch = ap;
			}
			else
			{
				cmd->angles[PITCH] = ANGLE2SHORT(-85.0f)
				                  - e->client->ps.pmove.delta_angles[PITCH];
				view_pitch = -85.0f;
			}
			cmd->forwardmove = 400;
			cmd->upmove = 400;
			bot->nav_drove = false;     /* not the route's fault */
		}


		if (rail_hold && have_move && bot->term_brake >= 1.0f &&
		    !tc->jump_launch && !bot->jump_started && !bot->drop_started)
		{
			cmd->forwardmove = 0;
			cmd->sidemove = 0;
			cmd->upmove = 0;
			bot->nav_drove = false;     /* the wait is not the route */
			bot->stuck_time = 0.0f;     /* nor is it being stuck */
		}

		/* rallying: get to cover first, stand there, face the push */
		if (rally_hold && have_move && !tc->jump_launch &&
		    !bot->jump_started && !bot->drop_started)
		{
			vec3_t cvd;
			qboolean cover_valid = bot->rally_cover >= 0 &&
			    bot->rally_cover < SG_Rune()->hdr.num_seeds;
			/* Legacy attacker rallies intentionally hold in place when their
			 * optional cover lookup failed.  A carrier is different: CARRYHOLD
			 * without a real standoff must never turn its homeward command into
			 * zero movement. */
			qboolean at_cover = role != SG_ROLE_CARRY;

			if (cover_valid)
			{
				VectorSubtract(SG_Rune()->seeds[bot->rally_cover].origin,
				               e->s.origin, cvd);
				cvd[2] = 0.0f;
				at_cover = (VectorLength(cvd) < 48.0f);
			}
			else if (role == SG_ROLE_CARRY)
			{
				rally_hold = false;
				tc->rally_hold = false;
			}
			if (rally_hold && at_cover)
			{
				cmd->forwardmove = 0;
				cmd->sidemove = 0;
				cmd->upmove = 0;
				bot->nav_drove = false;
				bot->stuck_time = 0.0f;
			}
			else if (rally_hold)
			{
				float cy = atan2f(cvd[1], cvd[0]) * 180.0f / (float)M_PI;

				cmd->angles[YAW] = ANGLE2SHORT(cy)
				                - e->client->ps.pmove.delta_angles[YAW];
				cmd->forwardmove = 400;
				view_yaw = cy;
				bot->nav_drove = false;     /* the wait is not the route */
			}
		}

		/* on post: whatever the descent wanted, guard duty overrides it */
		if (hold_post && !tc->jump_launch &&
		    !bot->jump_started && !bot->drop_started)
		{
			cmd->forwardmove = 0;
			cmd->sidemove = 0;
			cmd->upmove = 0;
			bot->nav_drove = false;
			cmd->angles[YAW] = ANGLE2SHORT(post_yaw)
			                - e->client->ps.pmove.delta_angles[YAW];
			cmd->angles[PITCH] = -e->client->ps.pmove.delta_angles[PITCH];
			view_yaw = post_yaw;
			view_pitch = 0.0f;
			have_move = false;
			bot->stuck_time = 0.0f;
		}

		if (ballistic_abort)
		{
			cmd->forwardmove = 0;
			cmd->sidemove = 0;
			cmd->upmove = 0;
			have_move = false;
			drop_yaw_locked = false;
			bot->nav_drove = false;
			bestlink = -1;
			tc->bestlink = -1;
		}
		if (escort_terminal_hold && !touch_terminal && !tc->jump_launch &&
		    !bot->jump_started && !bot->drop_started && bot->hook_phase == 0)
		{
			cmd->forwardmove = 0;
			cmd->sidemove = 0;
			cmd->upmove = 0;
			bot->nav_drove = false;
			bot->stuck_time = 0.0f;
			have_move = false;
		}

		/*
		 * Short-range progress has its own sample. last_origin is a 48-unit
		 * seed-localization checkpoint; using it here meant a body wedged 5-47
		 * units past that checkpoint could never satisfy the old <4 test. Sample
		 * actual route progress instead, after every intentional hold and brake
		 * has had the chance to clear nav_drove. A commanded hold resets both the
		 * clock and its origin, so waiting for a door, rope, rally, lane or post
		 * can never earn an unstick jump.
		 */
		if (!bot->nav_drove || bot->engaged_last)
		{
			bot->stuck_time = 0.0f;
			VectorCopy(e->s.origin, bot->stuck_origin);
		}
		else
		{
			VectorSubtract(e->s.origin, bot->stuck_origin, d);
			if (VectorLength(d) >= 4.0f)
			{
				bot->stuck_time = 0.0f;
				VectorCopy(e->s.origin, bot->stuck_origin);
			}
			else
			{
				bot->stuck_time += (float)cmd->msec / 1000.0f;
				if (bot->stuck_time > 1.0f && e->groundentity &&
				    !(bestlink >= 0 && SG_Rune() &&
				      (SG_Rune()->links[bestlink].action == RL_JUMP ||
				       SG_Rune()->links[bestlink].action == RL_DROP)))
					cmd->upmove = 400;   /* hop what the feelers missed */
			}
		}

	VectorCopy(move_dir, tc->move_dir);
	tc->view_yaw = view_yaw;
	tc->view_pitch = view_pitch;
	tc->have_move = have_move;
	tc->open_ahead = open_ahead;
	tc->run_link = run_link;
	tc->touch_terminal = touch_terminal;
	tc->door_hold = door_hold;
	tc->door_ent = door_ent;
	tc->drop_yaw_locked = drop_yaw_locked;
	tc->drop_yaw = drop_yaw;
	tc->hook_brake = hook_brake;
}

static qboolean Hook_GraphReleaseReady(edict_t *e, const sg_bot_t *bot)
{
	vec3_t view, forward, right, muzzle, bite, velocity, dest_dir;
	int rope;

	if (!e || !e->client || e->client->hookstate != 2 ||
	    !e->client->hook)
		return false;
	VectorCopy(bot->hook_view, view);
	AngleVectors(view, forward, right, NULL);
	CTF_HookMuzzle(e->s.origin, e->viewheight, e->client->pers.hand,
	               forward, right, muzzle);
	if (e->client->hook->hook_target)
		VectorAdd(e->client->hook->hook_target->absmin,
		          e->client->hook->hook_offset, bite);
	else
		VectorCopy(e->client->hook->s.origin, bite);
	rope = CTF_HookPullVelocity(muzzle, bite, velocity);
	VectorSubtract(bot->hook_dest, e->s.origin, dest_dir);
	return ((dest_dir[0] * dest_dir[0] + dest_dir[1] * dest_dir[1] <
	         80.0f * 80.0f && dest_dir[2] > -96.0f && dest_dir[2] < 96.0f) ||
	        rope < 130.0f);
}

static void Hook_GraphRelease(edict_t *e, sg_bot_t *bot,
	qboolean *cut_in_step)
{
	ctf_hook_abort(e);
	bot->hook_phase = 3;
	bot->flow_release = false;
	bot->hook_settle_ms = 0;
	*cut_in_step = true;
}

static void Hook_ShelveLink(sg_bot_t *bot, int link_index, float seconds)
{
	int b, oldest = 0;

	if (!SG_Rune() || link_index < 0 ||
	    link_index >= SG_Rune()->hdr.num_links)
		return;
	for (b = 0; b < SG_BL_MAX; b++)
		if (bot->bl_until[b] < bot->bl_until[oldest])
			oldest = b;
	bot->bl_link[oldest] = link_index;
	SG_TimerArm(&bot->bl_until[oldest], seconds);
}

static void Hook_Shelve(sg_bot_t *bot, float seconds)
{
	Hook_ShelveLink(bot, bot->hook_link, seconds);
}

static void Hook_LiveClearFinalGuard(sg_bot_t *bot)
{
	if (bot)
		SG_HookLiveCommandGuardClear(&bot->hook_final_guard);
}

static void Hook_GraphFailDetail(edict_t *e, sg_bot_t *bot,
	float shelf_seconds, const char *detail)
{
	(void)SG_HookDiagnosticsFinish(&bot->hook_diagnostics, "graph-fail", detail);
	if (e && e->client && e->client->hookstate != 0)
		ctf_hook_abort(e);
	Hook_Shelve(bot, shelf_seconds);
	SG_StagedTraversalCancel(bot, RL_HOOK);
	SG_HookLiveDeactivate(&bot->hook_replay, &bot->hook_replay_active,
	    &bot->hook_replay_link);
	Hook_LiveClearFinalGuard(bot);
	bot->hook_entity = NULL;
	bot->hook_legacy_settle = false;
	bot->hook_legacy_arrived = false;
	bot->hook_pull_ms = 0;
	bot->hook_settle_ms = 0;
}

static void Hook_GraphFail(edict_t *e, sg_bot_t *bot, float shelf_seconds)
{
	Hook_GraphFailDetail(e, bot, shelf_seconds, "legacy");
}

/* This is deliberately narrower than Hook_GraphFail: only a selected graph
 * link that could not be valued, decoded, or aimed owns this discipline. */
static void Hook_DisciplineRetire(edict_t *e, sg_bot_t *bot, int link_index,
	float shelf_seconds, qboolean failure, const char *reason,
	int from_goal, int to_goal)
{
	int gain = from_goal - to_goal;

	if (!bot || !SG_Rune() || link_index < 0 ||
	    link_index >= SG_Rune()->hdr.num_links ||
	    SG_Rune()->links[link_index].action != RL_HOOK)
		return;
	Hook_ShelveLink(bot, link_index, shelf_seconds);
	if (sg_cv.debug->value)
	{
		if (failure)
			sg_host.dprint("HOOKDISC %s %s link=%d shelf=%.0f\n",
			    e && e->client ? e->client->pers.netname : "?",
			    reason ? reason : "retire", link_index, shelf_seconds);
		else
			sg_host.dprint("HOOKDISC %s %s link=%d from=%d to=%d gain=%d min=%d shelf=%.0f\n",
			    e && e->client ? e->client->pers.netname : "?",
			    reason ? reason : "value-skip", link_index,
			    from_goal, to_goal, gain, SG_HOOK_DISCIPLINE_SERVED_FIELD_MS,
			    shelf_seconds);
	}
	if (e && e->client && e->client->hookstate != 0)
		ctf_hook_abort(e);
	SG_StagedTraversalCancel(bot, RL_HOOK);
	bot->hook_pull_ms = 0;
	bot->hook_settle_ms = 0;
	SG_HookLiveDeactivate(&bot->hook_replay, &bot->hook_replay_active,
	    &bot->hook_replay_link);
	Hook_LiveClearFinalGuard(bot);
	bot->hook_entity = NULL;
	bot->hook_legacy_settle = false;
	bot->hook_legacy_arrived = false;
}

qboolean SG_HookOffhandReady(edict_t *e)
{
	static gitem_t *hook;

	if (!hook)
		hook = FindItem("Grappling Hook");
	return (e && e->client && hook &&
	        ((int)ctfflags->value & CTF_OFFHAND_HOOK) &&
	        e->client->pers.hand == RIGHT_HANDED &&
	        e->client->pers.inventory[ITEM_INDEX(hook)] > 0 &&
	        e->client->pers.weapon != hook && e->client->newweapon != hook &&
	        e->client->hookstate == 0 && e->client->hook == NULL);
}

static qboolean Hook_LiveWitnessOK(const edict_t *e, const sg_bot_t *bot)
{
	return e && e->client && bot && e->health > 0 && !e->deadflag &&
	       e->health == bot->hook_source_health &&
	       e->movetype == MOVETYPE_WALK &&
	       e->client->ps.pmove.pm_type == PM_NORMAL &&
	       e->client->pers.hand == RIGHT_HANDED &&
	       !(e->client->ps.pmove.pm_flags & PMF_DUCKED) &&
	       e->client->ps.pmove.pm_time == 0 &&
	       fabsf(e->viewheight - 22.0f) <= 0.1f &&
	       !(e->waterlevel > 0 &&
	         (e->watertype & (CONTENTS_LAVA | CONTENTS_SLIME)));
}

static qboolean Hook_SourceStateOK(const edict_t *e, const sg_bot_t *bot)
{
	int i;

	if (!Hook_LiveWitnessOK(e, bot) || bot->hook_source_water ||
	    e->waterlevel != 0 || !e->groundentity ||
	    (e->groundentity != g_edicts &&
	     !SG_ImmutableSupport(e->groundentity)))
		return false;
	for (i = 0; i < 3; i++)
		if ((short)(e->s.origin[i] * 8.0f) !=
		    (short)(bot->hook_source[i] * 8.0f) ||
		    (short)(e->velocity[i] * 8.0f) !=
		    bot->hook_source_pms.velocity[i])
			return false;
	if (e->client->ps.pmove.pm_type != bot->hook_source_pms.pm_type ||
	    e->client->ps.pmove.pm_flags != bot->hook_source_pms.pm_flags ||
	    e->client->ps.pmove.pm_time != bot->hook_source_pms.pm_time ||
	    e->client->ps.pmove.gravity != bot->hook_source_pms.gravity)
		return false;
	return true;
}

enum
{
	HOOK_PROOF_FAIL = 0,
	HOOK_PROOF_OK = 1,
	HOOK_PROOF_BUSY = 2
};

/* Passing the shooter to gi.trace correctly excludes its body, but Yamagi
 * also excludes every entity owned by that passedict. A real hook bolt does
 * NOT ignore its sibling rocket/grenade, so check those separately. Keep the
 * check conservative and engine-portable: a stack fake-edict reproduces
 * Yamagi's owner rule, but API-3 proxy engines require passedict to belong to
 * the exported edict array. The linked abs bounds already include the
 * engine's one-unit clip fringe. */
static qboolean Hook_OwnedSolidBlocksShot(edict_t *owner,
	const vec3_t start, const vec3_t end)
{
	edict_t *touch[MAX_EDICTS];
	vec3_t query_min, query_max, delta;
	int axis, i, num;

	if (!owner || !sg_host.box_edicts)
		return true;                 /* exact witness unavailable: fail closed */
	for (axis = 0; axis < 3; axis++)
	{
		query_min[axis] = (start[axis] < end[axis] ? start[axis] : end[axis])
		                - 1.0f;
		query_max[axis] = (start[axis] > end[axis] ? start[axis] : end[axis])
		                + 1.0f;
		delta[axis] = end[axis] - start[axis];
	}
	num = sg_host.box_edicts(query_min, query_max, touch, MAX_EDICTS,
	                          AREA_SOLID);
	for (i = 0; i < num; i++)
	{
		edict_t *hit = touch[i];
		float enter = 0.0f, leave = 1.0f;

		if (!hit || !hit->inuse || hit == owner || hit->owner != owner ||
		    hit->solid == SOLID_NOT)
			continue;
		for (axis = 0; axis < 3; axis++)
		{
			float a, b, inv;

			if (fabsf(delta[axis]) < 0.0001f)
			{
				if (start[axis] < hit->absmin[axis] ||
				    start[axis] > hit->absmax[axis])
					break;
				continue;
			}
			inv = 1.0f / delta[axis];
			a = (hit->absmin[axis] - start[axis]) * inv;
			b = (hit->absmax[axis] - start[axis]) * inv;
			if (a > b)
			{
				float swap = a;
				a = b;
				b = swap;
			}
			if (a > enter) enter = a;
			if (b < leave) leave = b;
			if (enter > leave)
				break;
		}
		if (axis == 3 && leave >= 0.0f && enter <= 1.0f)
			return true;
	}
	return false;
}

/* Re-prove from the exact fixed-point state Cmd_Hook_f is about to consume.
 * The rune control is a planning prior; this witness is the executable
 * contract for the bot's actual position inside the source cell. */
static int Hook_OnlineProof(edict_t *e, sg_bot_t *bot,
	float nominal_distance, float *flight_distance)
{
	rune_link_t *link;
	sg_phantom_t ph;
	sg_hook_proof_t proof;
	vec3_t forward, right, muzzle, shot_end, source_to_muzzle;
	trace_t muzzle_tr, shot_tr;
	float shot_len;
	vec3_t source_delta;
	qboolean source_water;
	int i, flight_ms, proof_slot;

	if (!e || !e->client || !bot || !flight_distance || !SG_Rune() ||
	    bot->hook_link < 0 || bot->hook_link >= SG_Rune()->hdr.num_links ||
	    level.intermissiontime || GamePaused() ||
	    e->health <= 0 || e->deadflag || e->movetype != MOVETYPE_WALK ||
	    e->client->ps.pmove.pm_type != PM_NORMAL ||
	    (want_funky_gravity && want_funky_gravity->value != 0.0f) ||
	    (e->client->ps.pmove.pm_flags & ~PMF_ON_GROUND) != 0 ||
	    e->client->ps.pmove.pm_time != 0 ||
	    fabsf(e->viewheight - 22.0f) > 0.1f || !SG_HookOffhandReady(e) ||
	    SG_RocketJumpGameOwns(bot) || bot->nade_phase != 0)
		return HOOK_PROOF_FAIL;
	link = &SG_Rune()->links[bot->hook_link];
	if (link->action != RL_HOOK || bot->commit_link != bot->hook_link)
		return HOOK_PROOF_FAIL;
	source_water =
	    (SG_Rune()->seeds[link->from].flags & RSF_WATER) != 0;
	if ((source_water &&
	     ((SG_Rune()->seeds[link->to].flags & RSF_WATER) ||
	      link->heading_slack != RUNE_WATER_HOOK_CONTROL_MARKER ||
	      e->waterlevel < 2 || !(e->watertype & CONTENTS_WATER) ||
	      (e->watertype & (CONTENTS_LAVA | CONTENTS_SLIME)))) ||
	    (!source_water &&
	     (link->heading_slack != RUNE_HOOK_CONTROL_SLACK ||
	      !e->groundentity ||
	      (e->groundentity != g_edicts &&
	       !SG_ImmutableSupport(e->groundentity)) || e->waterlevel != 0)))
		return HOOK_PROOF_FAIL;
	VectorSubtract(SG_Rune()->seeds[link->from].origin, e->s.origin,
	               source_delta);
	if (source_delta[0] * source_delta[0] + source_delta[1] * source_delta[1] >
	        20.0f * 20.0f || fabsf(source_delta[2]) > 16.0f)
		return HOOK_PROOF_FAIL;
	if (!source_water)
		for (i = 0; i < 3; i++)
			if ((short)(e->velocity[i] * 8.0f) != 0)
				return HOOK_PROOF_FAIL;
	if ((short)ANGLE2SHORT(e->client->v_angle[PITCH]) !=
	        (short)ANGLE2SHORT(bot->hook_view[PITCH]) ||
	    (short)ANGLE2SHORT(e->client->v_angle[YAW]) !=
	        (short)ANGLE2SHORT(bot->hook_view[YAW]) ||
	    fabsf(e->client->v_angle[ROLL]) > 0.001f)
		return HOOK_PROOF_FAIL;
	proof_slot = (int)(bot - sg_bots);
	if (proof_slot < 0 || proof_slot >= SG_MAXBOTS)
		return HOOK_PROOF_FAIL;
	/* At most one expensive witness per server frame. Rotate the grant through
	 * sg_bots slots, the exact ascending order SG_RunFrame visits. A low slot
	 * that repeatedly finds bad local geometry
	 * must not consume every frame ahead of later bots. If no waiter exists past
	 * the last owner, one frame is left unused and the following frame wraps. */
	if (level.framenum < sg_hook_reproof_frame)
	{
		sg_hook_reproof_frame = -1; /* level-time rewind */
		sg_hook_reproof_slot = 0;
	}
	if (sg_hook_reproof_frame == level.framenum)
		return HOOK_PROOF_BUSY;
	if (sg_hook_reproof_frame == level.framenum - 1 &&
	    proof_slot <= sg_hook_reproof_slot)
		return HOOK_PROOF_BUSY;
	sg_hook_reproof_frame = level.framenum;
	sg_hook_reproof_slot = proof_slot;

	AngleVectors(e->client->v_angle, forward, right, NULL);
	CTF_HookMuzzle(e->s.origin, e->viewheight, e->client->pers.hand,
	               forward, right, muzzle);
	muzzle_tr = sg_host.trace(e->s.origin, NULL, NULL, muzzle, e, MASK_SHOT);
	if (muzzle_tr.startsolid || muzzle_tr.fraction < 1.0f)
		return HOOK_PROOF_FAIL;
	VectorNormalize(forward);
	shot_len = nominal_distance + 96.0f;
	if (shot_len < 160.0f)
		shot_len = 160.0f;
	if (shot_len > RUNE_HOOK_MAX_RAY)
		shot_len = RUNE_HOOK_MAX_RAY;
	VectorMA(muzzle, shot_len, forward, shot_end);
	shot_tr = sg_host.trace(muzzle, NULL, NULL, shot_end, e, MASK_SHOT);
	if (shot_tr.startsolid || shot_tr.fraction >= 1.0f ||
	    shot_tr.ent != g_edicts ||
	    (shot_tr.surface && (shot_tr.surface->flags & SURF_SKY)))
		return HOOK_PROOF_FAIL;
	if (Hook_OwnedSolidBlocksShot(e, muzzle, shot_tr.endpos))
		return HOOK_PROOF_FAIL;
	VectorSubtract(shot_tr.endpos, muzzle, source_to_muzzle);
	*flight_distance = DotProduct(source_to_muzzle, forward);
	if (*flight_distance < 1.0f || *flight_distance > RUNE_HOOK_MAX_RAY)
		return HOOK_PROOF_FAIL;
	VectorMA(muzzle, *flight_distance, forward, bot->hook_anchor);
	if (!SG_OracleHookFlightClear(muzzle, bot->hook_anchor))
		return HOOK_PROOF_FAIL;
	flight_ms = (int)ceilf(*flight_distance /
	                          RUNE_HOOK_FRAME_DISTANCE) * 100;

	memset(&ph, 0, sizeof(ph));
	ph.pms = e->client->ps.pmove;
	ph.old_pms = e->client->old_pmove;
	for (i = 0; i < 3; i++)
	{
		ph.pms.origin[i] = (short)(e->s.origin[i] * 8.0f);
		ph.pms.velocity[i] = (short)(e->velocity[i] * 8.0f);
		ph.origin[i] = ph.pms.origin[i] * 0.125f;
		ph.velocity[i] = ph.pms.velocity[i] * 0.125f;
	}
	ph.pms.gravity = (short)sv_gravity->value;
	ph.groundentity = e->groundentity != NULL;
	ph.watertype = e->watertype;
	ph.waterlevel = e->waterlevel;
	if (!SG_OracleHookTraverse(&ph, bot->hook_anchor, bot->hook_dest,
	                           bot->hook_view, RIGHT_HANDED, flight_ms,
	                           source_water ? RUNE_HOOK_WATER_SETTLE_MS
	                                        : RUNE_HOOK_DRY_SETTLE_MS,
	                           e->client->oldvelocity[2], &proof, e, true))
		return HOOK_PROOF_FAIL;
	if (source_water)
	{
		float available_air = e->waterlevel >= 3
		    ? SG_TimerRemaining(e->air_finished) : 12.0f;
		float action_seconds =
		    (flight_ms + proof.pull_ms + proof.settle_ms) * 0.001f + 0.2f;

		if (available_air <= action_seconds)
			return HOOK_PROOF_FAIL;
	}

	VectorCopy(e->s.origin, bot->hook_source);
	bot->hook_source[0] = (short)(bot->hook_source[0] * 8.0f) * 0.125f;
	bot->hook_source[1] = (short)(bot->hook_source[1] * 8.0f) * 0.125f;
	bot->hook_source[2] = (short)(bot->hook_source[2] * 8.0f) * 0.125f;
	bot->hook_source_pms = e->client->ps.pmove;
	for (i = 0; i < 3; i++)
	{
		bot->hook_source_pms.origin[i] = (short)(e->s.origin[i] * 8.0f);
		bot->hook_source_pms.velocity[i] = (short)(e->velocity[i] * 8.0f);
	}
	bot->hook_attach_pms = proof.attach_pms;
	bot->hook_source_water = source_water;
	bot->hook_source_health = e->health;
	bot->hook_attach_groundentity = proof.attach_groundentity;
	bot->hook_attach_watertype = proof.attach_watertype;
	bot->hook_attach_waterlevel = proof.attach_waterlevel;
	bot->hook_proved_pull_ms = proof.pull_ms;
	bot->hook_proved_release_ms = proof.release_ms;
	bot->hook_proved_arrival_ms = proof.settle_arrival_ms;
	bot->hook_proved_settle_ms = proof.settle_ms;
	return HOOK_PROOF_OK;
}

static qboolean Hook_AttachmentOK(edict_t *e, sg_bot_t *bot)
{
	vec3_t miss;
	int i;

	if (!Hook_LiveWitnessOK(e, bot) || e->client->hookstate != 2 ||
	    !e->client->hook || e->client->hook->hook_target != g_edicts ||
	    (!bot->hook_source_water && !Hook_SourceStateOK(e, bot)) ||
	    (!!e->groundentity != !!bot->hook_attach_groundentity) ||
	    (e->groundentity && e->groundentity != g_edicts &&
	     !SG_ImmutableSupport(e->groundentity)) ||
	    e->watertype != bot->hook_attach_watertype ||
	    e->waterlevel != bot->hook_attach_waterlevel)
		return false;
	VectorSubtract(e->client->hook->s.origin, bot->hook_anchor, miss);
	if (VectorLength(miss) > 0.5f)
		return false;
	for (i = 0; i < 3; i++)
		if ((short)(e->s.origin[i] * 8.0f) != bot->hook_attach_pms.origin[i] ||
		    (short)(e->velocity[i] * 8.0f) != bot->hook_attach_pms.velocity[i])
			return false;
	if (e->client->ps.pmove.pm_type != bot->hook_attach_pms.pm_type ||
	    e->client->ps.pmove.pm_flags != bot->hook_attach_pms.pm_flags ||
	    e->client->ps.pmove.pm_time != bot->hook_attach_pms.pm_time ||
	    e->client->ps.pmove.gravity != bot->hook_attach_pms.gravity ||
	    memcmp(&e->client->old_pmove, &bot->hook_attach_pms,
	           sizeof(bot->hook_attach_pms)) != 0)
		return false;
	/* Remove collision epsilon from the proved trajectory. The target is the
	 * immutable world, so keeping the target-relative offset in sync is safe. */
	VectorCopy(bot->hook_anchor, e->client->hook->s.origin);
	VectorSubtract(bot->hook_anchor, g_edicts->absmin,
	               e->client->hook->hook_offset);
	return true;
}

static qboolean Hook_AttachmentMaintained(edict_t *e, sg_bot_t *bot)
{
	vec3_t miss;

	if (!e || !e->client || e->client->hookstate != 2 ||
	    !e->client->hook || e->client->hook->hook_target != g_edicts)
		return false;
	VectorSubtract(e->client->hook->s.origin, bot->hook_anchor, miss);
	if (VectorLength(miss) > 0.5f)
		return false;
	VectorCopy(bot->hook_anchor, e->client->hook->s.origin);
	VectorSubtract(bot->hook_anchor, g_edicts->absmin,
	               e->client->hook->hook_offset);
	return true;
}

static qboolean Hook_SettleArrived(const edict_t *e, const sg_bot_t *bot)
{
	return SG_SupportedArrived(e->s.origin, bot->hook_dest,
	                           e->groundentity != NULL, e->watertype,
	                           e->waterlevel, (edict_t *)e);
}

static void Swim_LivePose(const edict_t *e, sg_replay_pose_t *pose)
{
	SG_SwimLivePose(pose, e && e->client ? &e->client->ps.pmove : NULL,
	    e ? e->s.origin : NULL, e ? e->velocity : NULL,
	    e && e->groundentity != NULL, e ? e->watertype : 0,
	    e ? e->waterlevel : 0);
}

static void Swim_LiveFallbackLog(const edict_t *e, int link_index,
	const char *phase, const sg_swim_live_result_t *result)
{
	if (!result || result->outcome != SG_SWIM_LIVE_FALLBACK ||
	    !sg_cv.debug->value)
		return;
	sg_host.dprint("SWIMREPLAYFALLBACK %s link=%d phase=%s adapter=%s "
	               "replay=%s\n",
	    e && e->client ? e->client->pers.netname : "?", link_index,
	    phase ? phase : "?", SG_SwimLiveFailureName(result->failure),
	    SG_ReplayReasonName(result->replay_reason));
}

enum
{
	SWIM_PROOF_FAIL = 0,
	SWIM_PROOF_OK = 1,
	SWIM_PROOF_BUSY = 2
};

/* Re-prove an RL_SWIM from the exact authoritative state its first live
 * ClientThink will consume. Localization identifies a useful nearby edge; it
 * is not an entry envelope for arbitrary momentum or displacement. */
static int Swim_OnlineProof(edict_t *e, sg_bot_t *bot, int link_index)
{
	rune_link_t *link;
	sg_phantom_t ph;
	sg_swim_proof_t proof;
	sg_replay_pose_t live_pose;
	sg_swim_live_result_t live_result;
	int i, proof_slot;

	if (!e || !e->client || !bot || !SG_Rune() || link_index < 0 ||
	    link_index >= SG_Rune()->hdr.num_links || bot->commit_link != link_index ||
	    level.intermissiontime || GamePaused() || e->health <= 0 || e->deadflag ||
	    e->movetype != MOVETYPE_WALK ||
	    e->client->ps.pmove.pm_type != PM_NORMAL ||
	    e->client->hookstate != 0 || e->client->hook != NULL ||
	    bot->hook_phase != 0 || SG_RocketJumpGameOwns(bot) ||
	    bot->nade_phase != 0)
		return SWIM_PROOF_FAIL;
	link = &SG_Rune()->links[link_index];
	if (link->action != RL_SWIM)
		return SWIM_PROOF_FAIL;

	proof_slot = (int)(bot - sg_bots);
	if (proof_slot < 0 || proof_slot >= SG_MAXBOTS)
		return SWIM_PROOF_FAIL;
	if (level.framenum < sg_swim_reproof_frame)
	{
		sg_swim_reproof_frame = -1;
		sg_swim_reproof_slot = 0;
	}
	if (sg_swim_reproof_frame == level.framenum)
		return SWIM_PROOF_BUSY;
	if (sg_swim_reproof_frame == level.framenum - 1 &&
	    proof_slot <= sg_swim_reproof_slot)
		return SWIM_PROOF_BUSY;
	sg_swim_reproof_frame = level.framenum;
	sg_swim_reproof_slot = proof_slot;

	memset(&ph, 0, sizeof(ph));
	ph.pms = e->client->ps.pmove;
	ph.old_pms = e->client->old_pmove;
	for (i = 0; i < 3; i++)
	{
		ph.pms.origin[i] = (short)(e->s.origin[i] * 8.0f);
		ph.pms.velocity[i] = (short)(e->velocity[i] * 8.0f);
		ph.origin[i] = ph.pms.origin[i] * 0.125f;
		ph.velocity[i] = ph.pms.velocity[i] * 0.125f;
	}
	ph.pms.gravity = (short)sv_gravity->value;
	ph.groundentity = e->groundentity != NULL;
	ph.waterlevel = e->waterlevel;
	ph.watertype = e->watertype;
	if (!SG_OracleSwimTraverse(&ph, SG_Rune()->seeds[link->to].origin,
	        (SG_Rune()->seeds[link->to].flags & RSF_WATER) != 0,
	        e->client->oldvelocity[2], &proof, e, true))
		return SWIM_PROOF_FAIL;
	bot->swim_validated = true;
	bot->swim_proved_ms = proof.arrival_ms;
	bot->swim_elapsed_ms = 0;
	Swim_LivePose(e, &live_pose);
	live_result = SG_SwimLiveBegin(&bot->swim_replay,
	    &bot->swim_replay_active, &bot->swim_replay_link, link_index,
	    SG_Rune()->seeds[link->to].origin,
	    (SG_Rune()->seeds[link->to].flags & RSF_WATER) != 0,
	    proof.arrival_ms, &live_pose, e->client->oldvelocity[2]);
	Swim_LiveFallbackLog(e, link_index, "begin", &live_result);
	SG_TimerArm(&bot->commit_until, proof.arrival_ms * 0.001f + 0.5f);
	return SWIM_PROOF_OK;
}

static int TeleportSwim_OnlineProof(edict_t *e, sg_bot_t *bot,
	int link_index)
{
	rune_link_t *link;
	edict_t *pad;
	sg_rune_mechanism_binding_t binding;
	sg_phantom_t ph;
	sg_swim_proof_t proof;
	vec3_t approach;
	int i, proof_slot;

	if (!e || !e->client || !bot || !SG_Rune() || link_index < 0 ||
	    link_index >= SG_Rune()->hdr.num_links || bot->commit_link != link_index ||
	    level.intermissiontime || GamePaused() || e->health <= 0 || e->deadflag ||
	    e->movetype != MOVETYPE_WALK ||
	    e->client->ps.pmove.pm_type != PM_NORMAL ||
	    e->client->hookstate != 0 || e->client->hook != NULL ||
	    bot->hook_phase != 0 || SG_RocketJumpGameOwns(bot) ||
	    bot->nade_phase != 0)
		return SWIM_PROOF_FAIL;
	link = &SG_Rune()->links[link_index];
	if (link->action != RL_TELEPORT ||
	    !(SG_Rune()->seeds[link->from].flags & RSF_WATER))
		return SWIM_PROOF_FAIL;
	if (!SG_RuneMechanismBindingCapture(SG_Rune(), (uint32_t)link_index,
	        &binding) || binding.link->action != RL_TELEPORT)
		return SWIM_PROOF_FAIL;
	pad = binding.mover_entity;
	if (!pad || !SG_TeleportApproachPoint(pad, approach))
		return SWIM_PROOF_FAIL;
	proof_slot = (int)(bot - sg_bots);
	if (proof_slot < 0 || proof_slot >= SG_MAXBOTS)
		return SWIM_PROOF_FAIL;
	if (level.framenum < sg_swim_reproof_frame)
	{
		sg_swim_reproof_frame = -1;
		sg_swim_reproof_slot = 0;
	}
	if (sg_swim_reproof_frame == level.framenum)
		return SWIM_PROOF_BUSY;
	if (sg_swim_reproof_frame == level.framenum - 1 &&
	    proof_slot <= sg_swim_reproof_slot)
		return SWIM_PROOF_BUSY;
	sg_swim_reproof_frame = level.framenum;
	sg_swim_reproof_slot = proof_slot;

	memset(&ph, 0, sizeof(ph));
	ph.pms = e->client->ps.pmove;
	ph.old_pms = e->client->old_pmove;
	for (i = 0; i < 3; i++)
	{
		ph.pms.origin[i] = (short)(e->s.origin[i] * 8.0f);
		ph.pms.velocity[i] = (short)(e->velocity[i] * 8.0f);
		ph.origin[i] = ph.pms.origin[i] * 0.125f;
		ph.velocity[i] = ph.pms.velocity[i] * 0.125f;
	}
	ph.pms.gravity = (short)sv_gravity->value;
	ph.groundentity = e->groundentity != NULL;
	ph.waterlevel = e->waterlevel;
	ph.watertype = e->watertype;
	if (!SG_OracleTeleportSwimApproach(&ph, approach, pad,
	        e->client->oldvelocity[2], &proof, e, true))
		return SWIM_PROOF_FAIL;
	bot->swim_validated = true;
	bot->swim_proved_ms = proof.arrival_ms;
	bot->swim_elapsed_ms = 0;
	bot->declared_started = true;
	SG_TimerArm(&bot->commit_until, proof.arrival_ms * 0.001f + 0.5f);
	return SWIM_PROOF_OK;
}

static void Swim_ProofFail(edict_t *e, sg_bot_t *bot, int link_index,
	float shelf_seconds)
{
	int b, oldest = 0;

	if (link_index >= 0)
	{
		for (b = 0; b < SG_BL_MAX; b++)
			if (bot->bl_until[b] < bot->bl_until[oldest])
				oldest = b;
		bot->bl_link[oldest] = link_index;
		SG_TimerArm(&bot->bl_until[oldest], shelf_seconds);
	}
	bot->commit_link = -1;
	SG_SwimLiveReset(&bot->swim_replay, &bot->swim_replay_active,
	    &bot->swim_replay_link, &bot->swim_validated,
	    &bot->swim_proved_ms, &bot->swim_elapsed_ms);
	if (sg_cv.debug->value)
		sg_host.dprint("SWIMREPROOFF %s link=%d\n",
		           e->client->pers.netname, link_index);
}

static void Hook_LivePose(const edict_t *e, sg_replay_pose_t *pose)
{
	SG_DropLivePose(pose, e && e->client ? &e->client->ps.pmove : NULL,
	    e ? e->s.origin : NULL, e ? e->velocity : NULL,
	    e && e->groundentity != NULL, e ? e->watertype : 0,
	    e ? e->waterlevel : 0);
}

/* A live graph-hook owner is the graph link plus the exact bolt created by
 * Cmd_Hook_f.  Once release has aborted the bolt, phase 3 deliberately owns
 * the no-bolt settlement; no recycled client hook may substitute for it. */
static qboolean Hook_LiveIdentityCurrent(const edict_t *e,
	const sg_bot_t *bot)
{
	if (!e || !e->client || !bot || !SG_Rune() || !bot->hook_replay_active ||
	    bot->hook_replay_link < 0 || bot->hook_link != bot->hook_replay_link ||
	    bot->commit_link != bot->hook_replay_link ||
	    bot->hook_replay_link >= SG_Rune()->hdr.num_links ||
	    SG_Rune()->links[bot->hook_replay_link].action != RL_HOOK)
		return false;
	if (bot->hook_phase == 2)
		return bot->hook_entity != NULL && e->client->hook == bot->hook_entity;
	return bot->hook_phase == 3 && e->client->hookstate == 0 &&
	       e->client->hook == NULL && bot->hook_entity != NULL;
}

static void Hook_LiveObservation(const edict_t *e, const sg_bot_t *bot,
	qboolean sample_settlement_contact, sg_replay_observation_t *observation)
{
	vec3_t view, forward, right, muzzle, bite, velocity;

	memset(observation, 0, sizeof(*observation));
	if (!e || !e->client || !bot)
		return;
	/* The old graph controller sampled its supported-destination predicate only
	 * at its settlement decision points.  The caller also suppresses it after
	 * its historical arrival latch, except for the final terminal check.  Do
	 * not introduce that contact (and its support trace) during bolt flight,
	 * attach, pull, or zero-fill substeps. */
	if (sample_settlement_contact && bot->hook_legacy_settle)
		observation->contact_clear = Hook_SettleArrived(e, bot);
	if (e->client->hookstate != 2 || !e->client->hook)
		return;
	VectorCopy(bot->hook_view, view);
	AngleVectors(view, forward, right, NULL);
	CTF_HookMuzzle(e->s.origin, e->viewheight, e->client->pers.hand,
	               forward, right, muzzle);
	if (e->client->hook->hook_target)
		VectorAdd(e->client->hook->hook_target->absmin,
		          e->client->hook->hook_offset, bite);
	else
		VectorCopy(e->client->hook->s.origin, bite);
	observation->hook_rope_length = CTF_HookPullVelocity(muzzle, bite, velocity);
	observation->hook_rope_valid = observation->hook_rope_length >= 0;
}

/* The adapter callback intentionally receives no host context.  The game is
 * single-threaded and this scoped pointer exists only around PreStep, so the
 * independent historical controller can use the body-owned destination,
 * fixed view, and release latch without consulting reducer phase or arrival.
 */
static const sg_bot_t *hook_legacy_command_bot;

static qboolean Hook_LiveLegacyCommand(const sg_hook_replay_state_t *state,
	const sg_replay_pose_t *pose, const sg_replay_observation_t *observation,
	usercmd_t *command)
{
	float dx, dy, dz, yaw;
	const sg_bot_t *bot = hook_legacy_command_bot;

	(void)state;
	if (!bot || !pose || !observation || !command)
		return false;
	memset(command, 0, sizeof(*command));
	command->msec = SG_REPLAY_STEP_MS;
	if (bot->hook_legacy_settle &&
	    (bot->hook_legacy_arrived || observation->contact_clear))
		return true; /* literal post-arrival zero fill */
	if (bot->hook_legacy_settle)
	{
		dx = bot->hook_dest[0] - pose->origin[0];
		dy = bot->hook_dest[1] - pose->origin[1];
		dz = bot->hook_dest[2] - pose->origin[2];
		if (!isfinite(dx) || !isfinite(dy) || !isfinite(dz))
			return false;
		yaw = atan2f(dy, dx) * 180.0f / (float)M_PI;
		command->angles[PITCH] = -pose->pms.delta_angles[PITCH];
		command->angles[YAW] = ANGLE2SHORT(yaw) - pose->pms.delta_angles[YAW];
		command->angles[ROLL] = -pose->pms.delta_angles[ROLL];
		command->forwardmove = 400;
		return true;
	}
	command->angles[PITCH] = ANGLE2SHORT(bot->hook_view[PITCH]) -
	                         pose->pms.delta_angles[PITCH];
	command->angles[YAW] = ANGLE2SHORT(bot->hook_view[YAW]) -
	                       pose->pms.delta_angles[YAW];
	command->angles[ROLL] = -pose->pms.delta_angles[ROLL];
	return true;
}

static void Hook_LiveResultLog(const edict_t *e, int link_index,
	const char *phase, const sg_hook_live_result_t *result)
{
	if (!result || result->outcome == SG_HOOK_LIVE_RUNNING ||
	    result->outcome == SG_HOOK_LIVE_ARRIVED || !sg_cv.debug->value)
		return;
	sg_host.dprint("HOOKREPLAY %s link=%d phase=%s adapter=%s replay=%s\n",
	    e && e->client ? e->client->pers.netname : "?", link_index,
	    phase ? phase : "?", SG_HookLiveFailureName(result->failure),
	    SG_ReplayReasonName(result->replay_reason));
}

static float Hook_LiveShelfSeconds(sg_hook_replay_phase_t replay_phase,
	sg_replay_reason_t reason)
{
	/* Preserve the legacy executor's three ownership shelves.  The reducer
	 * reports a reason, but the historical controller chose its retry period
	 * at the point that owned the body: launch/attach (15), pull/release (30),
	 * or settlement (60).  A boundary liquid report is the only exception
	 * while the bolt is still out: the old post-frame liquid interrupt was 30.
	 * The caller handles the old *pre-frame* liquid interrupt explicitly,
	 * because that was 30 even while phase 3 was about to settle. */
	switch (replay_phase)
	{
	case SG_HOOK_REPLAY_SETTLE:
		return 60.0f;
	case SG_HOOK_REPLAY_WAIT_PULL:
	case SG_HOOK_REPLAY_PULL_FRAME:
		return 30.0f;
	case SG_HOOK_REPLAY_FLIGHT:
	case SG_HOOK_REPLAY_WAIT_ATTACH:
	case SG_HOOK_REPLAY_ATTACH_FRAME:
	default:
		return reason == SG_REPLAY_REASON_HAZARDOUS_LIQUID ? 30.0f : 15.0f;
	}
}

#ifdef SG_STRIKE_TRANSITION_TEST_API
void SG_StrikeTestDirectTouchClaimMovement(sg_bot_t *bot, const edict_t *e,
	sg_think_t *tc, qboolean terminal)
{
	DirectTouchClaimMovement(bot, e, tc, terminal);
}

qboolean SG_StrikeTestDirectTouchDuelWeave(qboolean terminal, usercmd_t *cmd)
{
	return DefenseCombatApplyDuelWeave(false, false, true, true,
	    terminal, 160, cmd);
}

qboolean SG_StrikeTestEnemyFlagTouchMissionActive(qboolean strike_pressure,
	qboolean scoop_mission)
{
	return EnemyFlagTouchMissionActive(strike_pressure, scoop_mission);
}

int SG_StrikeTestTerminalFieldSeed(const rune_t *rune, const int *field,
	int current_seed)
{
	return SG_TerminalFieldSeed(rune, field, current_seed);
}

qboolean SG_TestGenericRailMoveAllowed(const sg_bot_t *bot, const sg_think_t *tc)
{
	return GenericRailMoveAllowed(bot, tc);
}

#endif

static void Hook_LiveSync(sg_bot_t *bot)
{
	if (!bot)
		return;
	bot->hook_pull_ms = bot->hook_replay.pull_ms;
	bot->hook_settle_ms = bot->hook_replay.settle_ms;
}

static void Hook_LiveTailCommand(const sg_bot_t *bot, qboolean settlement,
	const pmove_state_t *pms, usercmd_t *command)
{
	if (!command)
		return;
	SG_HookLiveZeroCommand(command);
	/* A legacy pull failure still consumed the remaining substeps with zero
	 * movement and its immutable rope view.  Settlement failures consumed
	 * literal zero commands. */
	if (!settlement && bot && pms)
	{
		command->angles[PITCH] = ANGLE2SHORT(bot->hook_view[PITCH]) -
		                         pms->delta_angles[PITCH];
		command->angles[YAW] = ANGLE2SHORT(bot->hook_view[YAW]) -
		                       pms->delta_angles[YAW];
		command->angles[ROLL] = -pms->delta_angles[ROLL];
	}
}

/* Hook_GraphFail deliberately zeros the public clocks.  The legacy pull loop
 * nevertheless incremented hook_pull_ms after every remaining zero-input
 * ClientThink in that same outer frame; preserve that observable fixed-step
 * history without giving settlement failures a clock they never had. */
static void Hook_LiveTailAdvance(sg_bot_t *bot, qboolean pull_clock)
{
	if (bot && pull_clock)
		bot->hook_pull_ms += SG_REPLAY_STEP_MS;
}

static qboolean Hook_LiveRetireNonRunning(edict_t *e, sg_bot_t *bot,
	int link_index, const char *phase, sg_hook_replay_phase_t replay_phase,
	const sg_hook_live_result_t *result)
{
	Hook_LiveResultLog(e, link_index, phase, result);
	if (!result || result->outcome == SG_HOOK_LIVE_RUNNING)
		return false;
	if (result->outcome == SG_HOOK_LIVE_ARRIVED)
	{
		/* The fourth settlement ClientThink has already completed; retain the
		 * exact terminal 100 ms clock the legacy body left behind. */
		(void)SG_HookDiagnosticsFinish(&bot->hook_diagnostics,
		    "arrived", "reducer");
		Hook_LiveSync(bot);
		bot->commit_link = -1;
		bot->hook_phase = 0;
		bot->hook_link = -1;
		Hook_LiveClearFinalGuard(bot);
		bot->hook_entity = NULL;
		bot->hook_legacy_settle = false;
		bot->hook_legacy_arrived = false;
		return true;
	}
	Hook_GraphFailDetail(e, bot,
	    Hook_LiveShelfSeconds(replay_phase, result->replay_reason),
	    SG_ReplayReasonName(result->replay_reason));
	return true;
}

/* The 100 ms predictor deliberately ends at an attachment acknowledgement,
 * not at a promise that the engine has already changed hookstate.  The old
 * graph body kept its one entry command unchanged for all four ClientThink
 * calls while that exact outgoing bolt remained in flight.  Snapshot pose and
 * PMove input at frame entry; the outer identity gate deliberately remains
 * authoritative for this full frame.  An attach or lost bolt during a substep
 * is observed at the next outer-frame gate, just as it was before the adapter
 * existed. */
static qboolean Hook_LiveWaitAttachFrame(sg_bot_t *bot, edict_t *e,
	int link_index)
{
	int step;
	qboolean failed = false;
	pmove_state_t entry_pms;
	sg_replay_pose_t entry_pose;
	sg_replay_observation_t observation;

	entry_pms = e->client->ps.pmove;
	Hook_LivePose(e, &entry_pose);
	entry_pose.pms = entry_pms;
	Hook_LiveObservation(e, bot, false, &observation);
	for (step = 0; step < SG_REPLAY_FRAME_MS / SG_REPLAY_STEP_MS; step++)
	{
		sg_hook_live_result_t result;
		usercmd_t command;

		if (failed)
		{
			Hook_LiveTailCommand(bot, false, &entry_pms, &command);
			ClientThink(e, &command);
			continue;
		}
		memset(&command, 0, sizeof(command));
		command.msec = SG_REPLAY_STEP_MS;
		hook_legacy_command_bot = bot;
		result = SG_HookLiveWaitAttachStep(&bot->hook_replay,
		    &bot->hook_replay_active, &bot->hook_replay_link, bot->hook_link,
		    true, &entry_pose, &observation, Hook_LiveLegacyCommand, &command,
		    &bot->hook_final_guard);
		hook_legacy_command_bot = NULL;
		if (Hook_LiveRetireNonRunning(e, bot, link_index, "wait-attach",
		    SG_HOOK_REPLAY_WAIT_ATTACH, &result))
		{
			if (result.outcome == SG_HOOK_LIVE_ARRIVED)
				return true;
			Hook_LiveTailCommand(bot, false, &entry_pms, &command);
			ClientThink(e, &command);
			failed = true;
			continue;
		}
		/* The adapter saved the canonical command before any later host writer;
		 * consume that one-shot approval immediately before ClientThink. */
		result = SG_HookLiveValidateStoredFinalCommand(&bot->hook_replay,
		    &bot->hook_replay_active, &bot->hook_replay_link, bot->hook_link,
		    true, &bot->hook_final_guard, &command);
		if (Hook_LiveRetireNonRunning(e, bot, link_index,
		    "wait-attach-final-command", SG_HOOK_REPLAY_WAIT_ATTACH, &result))
		{
			Hook_LiveTailCommand(bot, false, &entry_pms, &command);
			ClientThink(e, &command);
			failed = true;
			continue;
		}
		ClientThink(e, &command);
	}
	/* The pre-frame liquid gate is above.  This is the old post-flight gate,
	 * which observes all four frozen commands before applying its 30 s shelf. */
	if (!failed && e->waterlevel > 0 &&
	    (e->watertype & (CONTENTS_LAVA | CONTENTS_SLIME)))
		Hook_GraphFail(e, bot, 30.0f);
	return true;
}

static qboolean Hook_LiveActiveFrame(sg_bot_t *bot, edict_t *e)
{
	int step, link_index;
	sg_hook_replay_phase_t replay_phase;
	qboolean failed = false;
	qboolean release_seen = false;
	qboolean frame_settlement;
	qboolean frame_pull_clock;

	link_index = bot ? bot->hook_link : -1;
	replay_phase = bot ? bot->hook_replay.phase : SG_HOOK_REPLAY_FLIGHT;
	/* Match the legacy pre-command liquid interrupt.  It was a 30 s retry in
	 * both rope and phase-3 settlement; only a liquid divergence observed
	 * after settlement's four historical commands took its 60 s shelf. */
	if (e->waterlevel > 0 &&
	    (e->watertype & (CONTENTS_LAVA | CONTENTS_SLIME)))
	{
		Hook_GraphFail(e, bot, 30.0f);
		return true;
	}
	/* In rope ownership phases, the historical maintenance gate preceded any
	 * abstract identity law: a vanished/replaced bolt is the old 15 s
	 * attachment failure, not a 30 s pull-timing failure.  A live bolt that
	 * still satisfies that gate may then expose a link/owner identity drift to
	 * the adapter below. */
	if (!Hook_LiveIdentityCurrent(e, bot))
	{
		if ((replay_phase == SG_HOOK_REPLAY_ATTACH_FRAME ||
		     replay_phase == SG_HOOK_REPLAY_WAIT_PULL ||
		     replay_phase == SG_HOOK_REPLAY_PULL_FRAME) &&
		    !Hook_AttachmentMaintained(e, bot))
		{
			Hook_GraphFail(e, bot, 15.0f);
			return true;
		}
		else
		{
			sg_hook_live_result_t result = SG_HookLivePostStep(
			    &bot->hook_replay, &bot->hook_replay_active,
			    &bot->hook_replay_link, bot->hook_link, false, NULL, NULL);

			Hook_LiveRetireNonRunning(e, bot, link_index, "identity",
			                         replay_phase, &result);
			return true;
		}
	}
	/* Outbound bolt flight retains the old witness gate before it submits its
	 * four fixed-view commands. */
	if (replay_phase == SG_HOOK_REPLAY_FLIGHT)
	{
		if (e->client->hookstate != 1 || !e->client->hook ||
		    (!bot->hook_source_water && !Hook_SourceStateOK(e, bot)) ||
		    (bot->hook_source_water && !Hook_LiveWitnessOK(e, bot)) ||
		    SG_TimerReadyStrict(bot->hook_deadline))
		{
			Hook_GraphFail(e, bot, 15.0f);
			return true;
		}
	}
	/* WAIT_ATTACH is a reducer event barrier, not an engine attachment
	 * predicate.  Preserve the old late-bolt tolerance: while the original
	 * bolt is still outward-bound, retain its source/witness/deadline gate and
	 * spend one whole fixed-view frame before inspecting attachment again. */
	if (bot->hook_replay.phase == SG_HOOK_REPLAY_WAIT_ATTACH &&
	    e->client->hookstate == 1 && e->client->hook)
	{
		if ((!bot->hook_source_water && !Hook_SourceStateOK(e, bot)) ||
		    (bot->hook_source_water && !Hook_LiveWitnessOK(e, bot)) ||
		    SG_TimerReadyStrict(bot->hook_deadline))
		{
			Hook_GraphFail(e, bot, 15.0f);
			return true;
		}
		return Hook_LiveWaitAttachFrame(bot, e, link_index);
	}
	if (bot->hook_replay.phase == SG_HOOK_REPLAY_WAIT_ATTACH)
	{
		sg_replay_pose_t pose;
		sg_hook_live_result_t result;

		if (!Hook_AttachmentOK(e, bot))
		{
			Hook_GraphFail(e, bot, 15.0f);
			return true;
		}
		bot->hook_attached_validated = true;
		Hook_LivePose(e, &pose);
		result = SG_HookLiveAttached(&bot->hook_replay,
		    &bot->hook_replay_active, &bot->hook_replay_link, bot->hook_link,
		    true, &pose);
		if (Hook_LiveRetireNonRunning(e, bot, link_index, "attached",
		                             SG_HOOK_REPLAY_WAIT_ATTACH, &result))
			return true;
		Hook_LiveSync(bot);
		replay_phase = bot->hook_replay.phase;
	}
	/* AttachmentOK owns the first attached frame.  Every later attached and
	 * pull frame retains the legacy immutable-bite maintenance check. */
	if ((replay_phase == SG_HOOK_REPLAY_ATTACH_FRAME ||
	     replay_phase == SG_HOOK_REPLAY_WAIT_PULL ||
	     replay_phase == SG_HOOK_REPLAY_PULL_FRAME) &&
	    !Hook_AttachmentMaintained(e, bot))
	{
		Hook_GraphFail(e, bot, 15.0f);
		return true;
	}
	if (bot->hook_replay.phase == SG_HOOK_REPLAY_WAIT_PULL)
	{
		/* The one production pull is acknowledged by SG_HookLiveEndFrame,
		 * immediately after Weapon_Hook_Fire in ClientEndServerFrame. */
		Hook_GraphFail(e, bot, 30.0f);
		return true;
	}
	frame_settlement = bot->hook_legacy_settle;
	frame_pull_clock = !frame_settlement &&
	                   bot->hook_replay.phase == SG_HOOK_REPLAY_PULL_FRAME;
	for (step = 0; step < SG_REPLAY_FRAME_MS / SG_REPLAY_STEP_MS; step++)
	{
		sg_replay_pose_t pose;
		sg_replay_observation_t observation;
		sg_hook_live_result_t result;
		usercmd_t command;
		sg_hook_replay_phase_t step_phase;
		qboolean post_arrival_contact;

		if (failed)
		{
			Hook_LiveTailCommand(bot, frame_settlement,
			    &e->client->ps.pmove, &command);
			ClientThink(e, &command);
			Hook_LiveTailAdvance(bot, frame_pull_clock);
			continue;
		}

		Hook_LivePose(e, &pose);
		Hook_LiveObservation(e, bot,
		    frame_settlement && !bot->hook_legacy_arrived, &observation);
		memset(&command, 0, sizeof(command));
		command.msec = SG_REPLAY_STEP_MS;
		step_phase = bot->hook_replay.phase;
		hook_legacy_command_bot = bot;
		result = SG_HookLivePreStep(&bot->hook_replay,
		    &bot->hook_replay_active, &bot->hook_replay_link, bot->hook_link,
		    true, &pose, &observation, Hook_LiveLegacyCommand, &command,
		    &bot->hook_final_guard);
		hook_legacy_command_bot = NULL;
		if (Hook_LiveRetireNonRunning(e, bot, link_index, "prestep",
		                             step_phase, &result))
		{
			if (result.outcome == SG_HOOK_LIVE_ARRIVED)
				return true;
			Hook_LiveTailCommand(bot, frame_settlement,
			    &e->client->ps.pmove, &command);
			ClientThink(e, &command);
			Hook_LiveTailAdvance(bot, frame_pull_clock);
			failed = true;
			continue;
		}
		if (frame_settlement && observation.contact_clear)
			bot->hook_legacy_arrived = true;
		/* Do not recopy command here: the adapter owns the earlier immutable
		 * approval so a downstream writer remains visible at this boundary. */
		result = SG_HookLiveValidateStoredFinalCommand(&bot->hook_replay,
		    &bot->hook_replay_active, &bot->hook_replay_link, bot->hook_link,
		    true, &bot->hook_final_guard, &command);
		if (Hook_LiveRetireNonRunning(e, bot, link_index, "final-command",
		                             step_phase, &result))
		{
			Hook_LiveTailCommand(bot, frame_settlement,
			    &e->client->ps.pmove, &command);
			ClientThink(e, &command);
			Hook_LiveTailAdvance(bot, frame_pull_clock);
			failed = true;
			continue;
		}
		ClientThink(e, &command);
		Hook_LivePose(e, &pose);
		/* A non-arrived legacy settlement checked contact once after its
		 * submitted command.  Once arrival latched, it issued literal zeros
		 * without more contact traces until the one terminal check at substep 4.
		 * Keep both samples explicit so the host differential is historical,
		 * rather than a mirror of the reducer's arrival bit. */
		Hook_LiveObservation(e, bot,
		    frame_settlement && !bot->hook_legacy_arrived, &observation);
		post_arrival_contact = observation.contact_clear;
		if (frame_settlement &&
		    step == SG_REPLAY_FRAME_MS / SG_REPLAY_STEP_MS - 1 &&
		    (bot->hook_legacy_arrived || post_arrival_contact))
			Hook_LiveObservation(e, bot, true, &observation);
		step_phase = bot->hook_replay.phase;
		result = SG_HookLivePostStep(&bot->hook_replay,
		    &bot->hook_replay_active, &bot->hook_replay_link, bot->hook_link,
		    Hook_LiveIdentityCurrent(e, bot), &pose, &observation);
		if (Hook_LiveRetireNonRunning(e, bot, link_index, "poststep",
		                             step_phase, &result))
		{
			if (result.outcome == SG_HOOK_LIVE_ARRIVED)
				return true;
			failed = true;
			continue;
		}
		if (frame_settlement && post_arrival_contact)
			bot->hook_legacy_arrived = true;
		Hook_LiveSync(bot);
		if (bot->hook_replay.release_requested &&
		    !bot->hook_replay.release_applied)
		{
			/* Exact order: reducer requests release, production abort mutates the
			 * hook/body, then the adapter records that resulting authoritative pose. */
			ctf_hook_abort(e);
			bot->hook_phase = 3;
			bot->flow_release = false;
			bot->hook_settle_ms = 0;
			Hook_LivePose(e, &pose);
			result = SG_HookLiveReleaseApplied(&bot->hook_replay,
			    &bot->hook_replay_active, &bot->hook_replay_link, bot->hook_link,
			    Hook_LiveIdentityCurrent(e, bot), &pose);
			if (Hook_LiveRetireNonRunning(e, bot, link_index, "release",
			                             SG_HOOK_REPLAY_PULL_FRAME, &result))
			{
				failed = true;
				continue;
			}
			Hook_LiveSync(bot);
			release_seen = true;
		}
	}
	/* A release in substep 1/2/3 still finishes this outer fixed-view frame.
	 * The following frame is the first independent legacy settlement frame. */
	if (!failed && release_seen)
	{
		bot->hook_legacy_settle = true;
		bot->hook_legacy_arrived = false;
	}
	return true;
}

void SG_HookLiveEndFrame(edict_t *e)
{
	sg_bot_t *bot;
	sg_replay_pose_t pose;
	sg_hook_live_result_t result;

	if (!e || !e->client)
		return;
	bot = HumanSpeed_BotForEntity(e);
	if (bot && bot->speedhook && bot->hook_phase == 2 &&
	    e->client->hookstate == 2 && e->client->hook)
		bot->speedhook_pull_applied = true;
	if (!bot || !bot->hook_replay_active ||
	    bot->hook_replay.phase != SG_HOOK_REPLAY_WAIT_PULL)
		return;
	Hook_LivePose(e, &pose);
	result = SG_HookLivePullApplied(&bot->hook_replay,
	    &bot->hook_replay_active, &bot->hook_replay_link, bot->hook_link,
	    Hook_LiveIdentityCurrent(e, bot), &pose);
	if (!Hook_LiveRetireNonRunning(e, bot, bot->hook_link, "pull",
	    SG_HOOK_REPLAY_WAIT_PULL, &result))
		Hook_LiveSync(bot);
}

static qboolean Hook_LiveBeginAfterFire(edict_t *e, sg_bot_t *bot,
	int link_index, float flight_distance)
{
	sg_hook_replay_spec_t spec;
	sg_replay_pose_t pose;
	sg_replay_observation_t observation;
	sg_hook_live_result_t result;

	if (!e || !e->client || !bot || !SG_Rune() || link_index < 0 ||
	    link_index >= SG_Rune()->hdr.num_links ||
	    SG_Rune()->links[link_index].action != RL_HOOK ||
	    bot->hook_link != link_index || bot->commit_link != link_index ||
	    e->client->hookstate != 1 || !e->client->hook)
		return false;
	memset(&spec, 0, sizeof(spec));
	VectorCopy(bot->hook_anchor, spec.bite);
	VectorCopy(bot->hook_dest, spec.destination);
	VectorCopy(bot->hook_view, spec.view_angles);
	spec.flight_ms = (int)ceilf(flight_distance /
	                             RUNE_HOOK_FRAME_DISTANCE) * SG_REPLAY_FRAME_MS;
	spec.settle_limit_ms = bot->hook_source_water ? RUNE_HOOK_WATER_SETTLE_MS :
	                                                RUNE_HOOK_DRY_SETTLE_MS;
	spec.expected_release_ms = bot->hook_proved_release_ms;
	spec.expected_pull_ms = bot->hook_proved_pull_ms;
	spec.expected_settle_arrival_ms = bot->hook_proved_arrival_ms;
	spec.expected_settle_ms = bot->hook_proved_settle_ms;
	Hook_LiveClearFinalGuard(bot);
	bot->hook_legacy_settle = false;
	bot->hook_legacy_arrived = false;
	bot->hook_entity = e->client->hook;
	Hook_LivePose(e, &pose);
	Hook_LiveObservation(e, bot, false, &observation);
	result = SG_HookLiveBegin(&bot->hook_replay, &bot->hook_replay_active,
	    &bot->hook_replay_link, link_index, true, &spec, &pose, &observation,
	    e->client->oldvelocity[2], &bot->hook_final_guard);
	if (result.outcome == SG_HOOK_LIVE_RUNNING)
		return true;
	Hook_LiveResultLog(e, link_index, "begin", &result);
	bot->hook_entity = NULL;
	return false;
}

/* The proved graph-hook executor is intentionally outside the normal surface
 * pipeline. A tall ride can have no nearby seed, and fan/combat/holds/scalers
 * are not part of the oracle witness. */
qboolean SG_HookActiveFrame(sg_bot_t *bot, edict_t *e)
{
	usercmd_t cmd;
	int step;
	qboolean cut = false;
	qboolean failed = false;
	qboolean arrived = false;

	if (!bot || !e || !e->client || bot->speedhook || bot->hook_link < 0 ||
	    (bot->hook_phase != 2 && bot->hook_phase != 3))
		return false;
	if (bot->hook_replay_active)
		return Hook_LiveActiveFrame(bot, e);
	/* Online proof rejects harmful liquid on every 100 ms boundary. Dynamic
	 * combat can still perturb the live body after proof; retire that diverged
	 * witness before it deliberately continues through lava/slime. */
	if (e->waterlevel > 0 &&
	    (e->watertype & (CONTENTS_LAVA | CONTENTS_SLIME)))
	{
		Hook_GraphFail(e, bot, 30.0f);
		return true;
	}

	memset(&cmd, 0, sizeof(cmd));
	cmd.msec = 25;
	if (bot->hook_phase == 2)
	{
		/* Outbound flight owns no body input. Dry bodies must remain at their
		 * source; water bodies may take only the zero-input drift rolled by the
		 * witness, whose complete state is checked when attachment occurs. */
		if (e->client->hookstate == 1 && e->client->hook)
		{
			if ((!bot->hook_source_water && !Hook_SourceStateOK(e, bot)) ||
			    (bot->hook_source_water && !Hook_LiveWitnessOK(e, bot)) ||
			    SG_TimerReadyStrict(bot->hook_deadline))
			{
				Hook_GraphFail(e, bot, 15.0f);
				return true;
			}
			cmd.angles[PITCH] = ANGLE2SHORT(bot->hook_view[PITCH]) -
			                         e->client->ps.pmove.delta_angles[PITCH];
			cmd.angles[YAW] = ANGLE2SHORT(bot->hook_view[YAW]) -
			                       e->client->ps.pmove.delta_angles[YAW];
			cmd.angles[ROLL] = -e->client->ps.pmove.delta_angles[ROLL];
			for (step = 0; step < 4; step++)
				ClientThink(e, &cmd);
			if (e->waterlevel > 0 &&
			    (e->watertype & (CONTENTS_LAVA | CONTENTS_SLIME)))
				Hook_GraphFail(e, bot, 30.0f);
			return true;
		}
		if ((!bot->hook_attached_validated && !Hook_AttachmentOK(e, bot)) ||
		    (bot->hook_attached_validated && !Hook_AttachmentMaintained(e, bot)))
		{
			Hook_GraphFail(e, bot, 15.0f);
			return true;
		}
		if (!bot->hook_attached_validated)
		{
			bot->hook_attached_validated = true;
			bot->hook_pull_ms = 0;
			/* Attachment happened in the entity loop. Spend this frame's four
			 * no-op commands at the exact source; the first pull is the normal
			 * ClientEndServerFrame call that follows. */
			cmd.angles[PITCH] = ANGLE2SHORT(bot->hook_view[PITCH]) -
			                         e->client->ps.pmove.delta_angles[PITCH];
			cmd.angles[YAW] = ANGLE2SHORT(bot->hook_view[YAW]) -
			                       e->client->ps.pmove.delta_angles[YAW];
			cmd.angles[ROLL] = -e->client->ps.pmove.delta_angles[ROLL];
			for (step = 0; step < 4; step++)
				ClientThink(e, &cmd);
			if (e->waterlevel > 0 &&
			    (e->watertype & (CONTENTS_LAVA | CONTENTS_SLIME)))
				Hook_GraphFail(e, bot, 30.0f);
			return true;
		}

		for (step = 0; step < 4; step++)
		{
			qboolean ready;

			cmd.angles[PITCH] = ANGLE2SHORT(bot->hook_view[PITCH]) -
			                         e->client->ps.pmove.delta_angles[PITCH];
			cmd.angles[YAW] = ANGLE2SHORT(bot->hook_view[YAW]) -
			                       e->client->ps.pmove.delta_angles[YAW];
			cmd.angles[ROLL] = -e->client->ps.pmove.delta_angles[ROLL];
			cmd.forwardmove = cmd.sidemove = cmd.upmove = 0;
			ClientThink(e, &cmd);
			bot->hook_pull_ms += 25;
			if (failed || cut)
				continue;
			ready = Hook_GraphReleaseReady(e, bot);
			if (ready && bot->hook_pull_ms == bot->hook_proved_release_ms)
				Hook_GraphRelease(e, bot, &cut);
			else if (ready ||
			         bot->hook_pull_ms >= bot->hook_proved_release_ms)
			{
				Hook_GraphFail(e, bot, 30.0f);
				failed = true;
			}
		}
		if (failed)
			return true;
		if (e->waterlevel > 0 &&
		    (e->watertype & (CONTENTS_LAVA | CONTENTS_SLIME)))
		{
			Hook_GraphFail(e, bot, 30.0f);
			return true;
		}
		if ((cut && bot->hook_pull_ms != bot->hook_proved_pull_ms) ||
		    (!cut && bot->hook_pull_ms >= bot->hook_proved_pull_ms))
		{
			Hook_GraphFail(e, bot, 30.0f);
		}
		return true;
	}

	/* Literal oracle settlement: re-aim at the destination before every 25 ms
	 * forward command. First arrival and the fully consumed 100 ms frame are
	 * separate proof boundaries: after arrival, zero commands fill the frame,
	 * and the body must still be in the destination envelope at its end. */
	for (step = 0; step < 4; step++)
	{
		vec3_t d;
		float yaw;

		memset(&cmd, 0, sizeof(cmd));
		cmd.msec = 25;
		if (failed)
		{
			cmd.forwardmove = cmd.sidemove = cmd.upmove = 0;
			ClientThink(e, &cmd);
			continue;
		}
		if (arrived)
		{
			cmd.forwardmove = cmd.sidemove = cmd.upmove = 0;
			ClientThink(e, &cmd);
			bot->hook_settle_ms += 25;
			continue;
		}
		if (Hook_SettleArrived(e, bot))
		{
			if (bot->hook_settle_ms == bot->hook_proved_arrival_ms)
				arrived = true;
			else
			{
				Hook_GraphFail(e, bot, 60.0f);
				failed = true;
			}
			cmd.forwardmove = cmd.sidemove = cmd.upmove = 0;
			ClientThink(e, &cmd);
			if (!failed)
				bot->hook_settle_ms += 25;
			continue;
		}
		if (bot->hook_settle_ms >= bot->hook_proved_arrival_ms ||
		    bot->hook_settle_ms >= bot->hook_proved_settle_ms)
		{
			Hook_GraphFail(e, bot, 60.0f);
			failed = true;
			cmd.forwardmove = cmd.sidemove = cmd.upmove = 0;
			ClientThink(e, &cmd);
			continue;
		}
		VectorSubtract(bot->hook_dest, e->s.origin, d);
		yaw = atan2f(d[1], d[0]) * 180.0f / (float)M_PI;
		cmd.angles[PITCH] = -e->client->ps.pmove.delta_angles[PITCH];
		cmd.angles[YAW] = ANGLE2SHORT(yaw) -
		                   e->client->ps.pmove.delta_angles[YAW];
		cmd.angles[ROLL] = -e->client->ps.pmove.delta_angles[ROLL];
		cmd.forwardmove = 400;
		cmd.sidemove = cmd.upmove = 0;
		ClientThink(e, &cmd);
		bot->hook_settle_ms += 25;
		if (Hook_SettleArrived(e, bot))
		{
			if (bot->hook_settle_ms == bot->hook_proved_arrival_ms)
				arrived = true;
			else
			{
				Hook_GraphFail(e, bot, 60.0f);
				failed = true;
			}
		}
		else if (bot->hook_settle_ms >= bot->hook_proved_arrival_ms)
		{
			Hook_GraphFail(e, bot, 60.0f);
			failed = true;
		}
	}
	if (!failed && e->waterlevel > 0 &&
	    (e->watertype & (CONTENTS_LAVA | CONTENTS_SLIME)))
	{
		Hook_GraphFail(e, bot, 60.0f);
		failed = true;
	}
	if (!failed && bot->hook_settle_ms == bot->hook_proved_settle_ms)
	{
		if (arrived && Hook_SettleArrived(e, bot))
			cut = true;
		else
		{
			Hook_GraphFail(e, bot, 60.0f);
			failed = true;
		}
	}
	else if (!failed && bot->hook_settle_ms > bot->hook_proved_settle_ms)
	{
		Hook_GraphFail(e, bot, 60.0f);
		failed = true;
	}
	if (!failed && cut)
	{
		bot->commit_link = -1;
		bot->hook_phase = 0;
		bot->hook_link = -1;
	}
	return true;
}

void Think_Emit(sg_bot_t *bot, sg_think_t *tc)
{
	usercmd_t *cmd = &tc->cmd;
	edict_t *e = tc->e;
	sg_role_t role = tc->role;
	int team = tc->team;
	const int *goal_field = tc->goal_field;
	const int *route_field = tc->route_field;
	int bestlink = tc->bestlink;
	qboolean precision = tc->precision;
	qboolean hold_post = tc->hold_post;
	qboolean duel = tc->duel;
	vec_t *move_dir = tc->move_dir;
	float view_yaw = tc->view_yaw;
	float view_pitch = tc->view_pitch;
	qboolean have_move = tc->have_move;
	qboolean open_ahead = tc->open_ahead;
	qboolean run_link = tc->run_link;
	int door_hold = tc->door_hold;

	if (SG_CompoundSwimGameEmit(bot, bestlink))
		return;
	bot->beat_until = SG_SpawnBeatDeadline(bot->beat_until,
	    tc->touch_terminal);
	{
		sg_mover_lease_record_t record;
		sg_compound_guard_result_t guard_result;
		rune_t *rune = SG_Rune();

		guard_result = SG_CompoundGuardValidate(&bot->compound_guard, &record);
		if ((record.law == SG_MOVER_LAW_DECLARED_DOOR ||
		     record.law == SG_MOVER_LAW_COMPOUND_PREOPEN) &&
		    (record.state == SG_MOVER_LEASE_ACTIVE ||
		     record.state == SG_MOVER_LEASE_PAUSED))
		{
			/* The durable claim dominates rail, patrol, and watchdog rewrites.
			 * They may choose a candidate, but cannot replace the command owner
			 * between CommitLink and the mutation boundary. */
			if (guard_result != SG_COMPOUND_GUARD_OK ||
			    !rune || !rune->links || record.link_index < 0 ||
			    record.link_index >= rune->hdr.num_links ||
			    (record.law == SG_MOVER_LAW_DECLARED_DOOR &&
			     rune->links[record.link_index].action != RL_DOOR &&
			     rune->links[record.link_index].action != RL_BUTTON_DOOR) ||
			    (record.law == SG_MOVER_LAW_COMPOUND_PREOPEN &&
			     rune->links[record.link_index].action != RL_DOOR_DROP) ||
			    bot->commit_link != record.link_index)
			{
				if (sg_cv.debug && sg_cv.debug->value > 0.0f &&
				    sg_host.dprint)
					sg_host.dprint("slipgate: compound guard dominance failed "
					    "result=%d law=%d state=%d record_link=%d "
					    "commit_link=%d action=%d keys=%u\n",
					    guard_result, record.law, record.state,
					    record.link_index, bot->commit_link,
					    rune && rune->links && record.link_index >= 0 &&
					        record.link_index < rune->hdr.num_links
					        ? rune->links[record.link_index].action : -1,
					    (unsigned int)record.key_count);
				SG_DeclaredDoorTerminalDeath(bot);
				return;
			}
			bestlink = record.link_index;
			tc->bestlink = bestlink;
		}
		else if (record.law == SG_MOVER_LAW_DECLARED_DOOR &&
		         record.state == SG_MOVER_LEASE_QUARANTINED &&
		         bot->declared_started)
		{
			SG_DeclaredDoorTerminalDeath(bot);
			return;
		}
	}
	/* RL_ROCKETJUMP owns the complete four-command frame.  fire_rocket may
	 * synchronously move its reducer from ARMED to FLIGHT during the first
	 * ClientThink, so generic writers cannot run before the remaining steps. */
	if (SG_RocketJumpGameEmit(bot, bestlink))
		return;
	qboolean drop_yaw_locked = tc->drop_yaw_locked;
	qboolean proved_ballistic = (bestlink >= 0 && SG_Rune() &&
	    SG_Rune()->links && bestlink < SG_Rune()->hdr.num_links &&
	    (SG_Rune()->links[bestlink].action == RL_DROP ||
	     SG_Rune()->links[bestlink].action == RL_JUMP));
	qboolean proved_drop = (bestlink >= 0 && SG_Rune() && SG_Rune()->links &&
	    bestlink < SG_Rune()->hdr.num_links &&
	    SG_Rune()->links[bestlink].action == RL_DROP);
	qboolean proved_jump = (bestlink >= 0 && SG_Rune() && SG_Rune()->links &&
	    bestlink < SG_Rune()->hdr.num_links &&
	    SG_Rune()->links[bestlink].action == RL_JUMP);
	qboolean proved_swim = (bestlink >= 0 && SG_Rune() && SG_Rune()->links &&
	    bestlink < SG_Rune()->hdr.num_links &&
	    SG_Rune()->links[bestlink].action == RL_SWIM);
	qboolean compound_drop = (bestlink >= 0 && SG_Rune() &&
	    SG_Rune()->links && bestlink < SG_Rune()->hdr.num_links &&
	    SG_Rune()->links[bestlink].action == RL_DOOR_DROP);
	qboolean declared_control = (bestlink >= 0 && SG_Rune() &&
	    SG_Rune()->links && bestlink < SG_Rune()->hdr.num_links &&
	    (SG_Rune()->links[bestlink].action == RL_LIFT ||
	     SG_Rune()->links[bestlink].action == RL_TELEPORT ||
	     SG_Rune()->links[bestlink].action == RL_DOOR ||
	     SG_Rune()->links[bestlink].action == RL_BUTTON_DOOR));
	qboolean proved_control = proved_ballistic || proved_swim ||
	    compound_drop || declared_control;
	qboolean declared_door = declared_control &&
	    (SG_Rune()->links[bestlink].action == RL_DOOR ||
	     SG_Rune()->links[bestlink].action == RL_BUTTON_DOOR);
	sg_rune_mechanism_binding_t mechanism_binding = { 0 };
	qboolean mechanism_bound = !declared_control ||
	    (bot->declared_started
	        ? SG_RuneMechanismBindingCaptureOwned(SG_Rune(),
	              (uint32_t)bestlink, &mechanism_binding)
	        : SG_RuneMechanismBindingCapture(SG_Rune(),
	              (uint32_t)bestlink, &mechanism_binding));
	qboolean water_tele = declared_control &&
	    SG_Rune()->links[bestlink].action == RL_TELEPORT &&
	    (SG_Rune()->seeds[SG_Rune()->links[bestlink].from].flags & RSF_WATER);
	qboolean water_control = proved_swim ||
	    (water_tele && !bot->declared_activated);
	qboolean swim_hazard = water_control && e->waterlevel > 0 &&
	    (e->watertype & (CONTENTS_LAVA | CONTENTS_SLIME));
	qboolean swim_emergency = water_control && e->waterlevel >= 3 &&
	    bot->hook_phase != 2 &&
	    SG_TimerRemaining(e->air_finished) <
	        ((role == SG_ROLE_CARRY) ? 8.0f : 4.0f);

	vec3_t basis_fwd, basis_right;
	int sub_steps = 1, sub_msec = 0;
	float slew_want_y = 0.0f, slew_want_p = 0.0f, slew_rate = 0.0f;
	qboolean duel_hold = false;
	qboolean defcombat_active = false;
	qboolean aimed_fire_requested = false;
	qboolean aimed_fire_view_admitted = false;
	qboolean soundfire_owned = false;
	qboolean hook_cut_in_step = false;
	qboolean door_suffix_grant = false;
	edict_t *defcombat_enemy = NULL;
	edict_t *defcombat_stand = NULL;
	unsigned long defcombat_enemy_ctfid = 0;
	short weave_side = 0;
	short aimed_fire_yaw = 0, aimed_fire_pitch = 0;
	vec3_t d, defcombat_dir;

	VectorClear(defcombat_dir);
	if (compound_drop && !bot->compound_drop_live.guard_owned)
	{
		sg_compound_drop_live_host_t host;
		sg_compound_drop_live_result_t result;
		sg_replay_pose_t pose;

		if (!SG_CompoundDropGameHost(bot, &host) ||
		    !SG_CompoundDropGamePose(e, &pose))
		{
			bot->commit_link = -1;
			return;
		}
		result = SG_CompoundDropLiveBegin(&bot->compound_drop_live,
		    &host, (uint32_t)bestlink, &pose);
		SG_CompoundDropGameDebugResult(bot, "begin", &result, &pose);
		if (result.outcome != SG_COMPOUND_DROP_LIVE_RUNNING)
		{
			bot->commit_link = -1;
			return;
		}
		bot->commit_link = bestlink;
	}

	/* Think_Move uses this one-frame latch only when live DROP Begin could not
	 * acquire canonical ownership.  Return before any ClientThink: even a zero
	 * command can execute trigger or solid-touch side effects at a contaminated
	 * source.  Generic and legacy movement remain excluded for this frame. */
	if (tc->think_over)
		return;
	if (!mechanism_bound)
	{
		if (declared_door && bot->declared_started)
			DoorStep_RetainFailedAuthority(bot, bestlink);
		else
		{
			bot->commit_link = -1;
			bot->commit_until = 0.0f;
		}
		return;
	}

	/* Think_Move may retire a previous action after Think_CommitLink selected
	 * this frame's edge (for example, the grounded tail of a rocket jump).  The
	 * context then still names that edge, but it is no longer the bot's durable
	 * commitment.  Never acquire or execute a door from that stale snapshot.
	 * If an older declared-door transaction already exists, retire it only
	 * through the same positive whole-sweep clearance gate. */
	if (declared_door && bot->commit_link != bestlink)
	{
		if (bot->declared_started && !DoorStep_AbortOrRetain(bot, bestlink))
			return;
		return;
	}

	/* The graph's nominal SWIM proves that the local action exists. Execution
	 * begins only after the same oracle proves the actual fixed-point entry
	 * state. One rotating grant bounds worst-case Pmove work per server frame. */
	if (proved_swim && !bot->swim_validated &&
	    !swim_emergency && !swim_hazard)
	{
		int online = Swim_OnlineProof(e, bot, bestlink);
		usercmd_t wait_cmd[SG_SWIM_LIVE_FRAME_STEPS];
		int wait_step;

		SG_SwimLiveZeroFrame(wait_cmd);
		if (online == SWIM_PROOF_BUSY)
		{
			SG_TimerArm(&bot->commit_until, 3.0f);
			for (wait_step = 0; wait_step < SG_SWIM_LIVE_FRAME_STEPS;
			     wait_step++)
				ClientThink(e, &wait_cmd[wait_step]);
			return;
		}
		if (online != SWIM_PROOF_OK)
		{
			Swim_ProofFail(e, bot, bestlink, 5.0f);
			for (wait_step = 0; wait_step < SG_SWIM_LIVE_FRAME_STEPS;
			     wait_step++)
				ClientThink(e, &wait_cmd[wait_step]);
			return;
		}
	}
	if (water_tele && !bot->swim_validated &&
	    !swim_emergency && !swim_hazard)
	{
		int online = TeleportSwim_OnlineProof(e, bot, bestlink);
		usercmd_t wait_cmd;
		int wait_step;

		memset(&wait_cmd, 0, sizeof(wait_cmd));
		wait_cmd.msec = SG_SWIM_STEP_MSEC;
		if (online == SWIM_PROOF_BUSY)
		{
			SG_TimerArm(&bot->commit_until, 3.0f);
			for (wait_step = 0; wait_step < 4; wait_step++)
				ClientThink(e, &wait_cmd);
			return;
		}
		if (online != SWIM_PROOF_OK)
		{
			Swim_ProofFail(e, bot, bestlink, 5.0f);
			for (wait_step = 0; wait_step < 4; wait_step++)
				ClientThink(e, &wait_cmd);
			return;
		}
	}

	/* Drowning and hazardous liquid are safety interrupts, not optional
	 * modifiers. They invalidate this exact endpoint traversal before any
	 * command is emitted. A hazard also shelves the demonstrated link because
	 * the live world reached a state its proof explicitly rejected. */
	if (swim_emergency || swim_hazard)
	{
		bot->commit_link = -1;
		SG_SwimLiveReset(&bot->swim_replay, &bot->swim_replay_active,
		    &bot->swim_replay_link, &bot->swim_validated,
		    &bot->swim_proved_ms, &bot->swim_elapsed_ms);
	}
	if (swim_hazard)
	{
		int b, oldest = 0;

		for (b = 0; b < SG_BL_MAX; b++)
			if (bot->bl_until[b] < bot->bl_until[oldest])
				oldest = b;
		bot->bl_link[oldest] = bestlink;
		SG_TimerArm(&bot->bl_until[oldest],
		    SG_SWIM_LIVE_EARLY_HAZARD_SHELF_SECONDS);
		SG_TeachLinkFutility(bestlink);
	}
	if (water_tele && (swim_emergency || swim_hazard))
	{
		vec3_t destination;
		int air_from = SG_Rune()->links[bestlink].from;
		int air_seed = (sg_airnext && air_from >= 0)
		    ? sg_airnext[air_from] : -1;
		usercmd_t escape_cmd;
		int escape_step;

		if (swim_emergency && !swim_hazard)
		{
			int b, oldest = 0;

			for (b = 0; b < SG_BL_MAX; b++)
				if (bot->bl_until[b] < bot->bl_until[oldest])
					oldest = b;
			bot->bl_link[oldest] = bestlink;
			SG_TimerArm(&bot->bl_until[oldest], 5.0f);
		}
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

		if (air_seed >= 0 && air_seed < SG_Rune()->hdr.num_seeds)
			VectorCopy(SG_Rune()->seeds[air_seed].origin, destination);
		else
		{
			VectorCopy(e->s.origin, destination);
			destination[2] += 256.0f;
		}
		for (escape_step = 0; escape_step < 4; escape_step++)
		{
			memset(&escape_cmd, 0, sizeof(escape_cmd));
			escape_cmd.msec = SG_SWIM_STEP_MSEC;
			SG_SwimCommand(e->s.origin, destination,
			               &e->client->ps.pmove, &escape_cmd);
			ClientThink(e, &escape_cmd);
		}
		return;
	}


	{
		vec3_t basis;

		basis[PITCH] = view_pitch;
		if (basis[PITCH] > 180.0f)
			basis[PITCH] -= 360.0f;
		basis[PITCH] /= 3.0f;
		basis[YAW] = view_yaw;
		basis[ROLL] = 0.0f;
		AngleVectors(basis, basis_fwd, basis_right, NULL);
	}


	/* sg_noweave disables lateral combat oscillation for comparison. */
	if (duel && !hold_post && role != SG_ROLE_CARRY && !precision &&
	    bot->hook_phase == 0 &&
	    !sg_cv.noweave->value)
	{
		if (bestlink < 0)
			duel_hold = true;
		else
		{
			VectorSubtract(SG_Rune()->seeds[SG_Rune()->links[bestlink].to].origin,
			               e->s.origin, d);
			duel_hold = (VectorLength(d) < SG_WEAVE_HOLD);
		}

		if (duel_hold)
		{
			weave_side = (short)(SG_WeaveSideAt(bot->instance_token,
			    e->client->ctf.ctfid, level.time) * SG_WEAVE_SIDE);
		}
	}

	{
		float dose = sg_cv.breather->value;
		if (dose > 0.0f && role != SG_ROLE_CARRY && !proved_control &&
		    SG_DirectTouchOptionalPacingAllowed(tc->touch_terminal) &&
		    bot->hook_phase == 0 && !bot->engaged_last &&
		    e->groundentity != NULL)
		{
			if (SG_TimerReady(bot->breather_next))
			{
				SG_TimerArm(&bot->breather_next,
				    dose * (0.5f + (float)(rand() % 100) / 100.0f));
				SG_TimerArm(&bot->breather_until,
				    0.5f + (float)(rand() % 130) / 100.0f);
			}
			if (SG_TimerPending(bot->breather_until))
			{
				cmd->forwardmove = (short)(cmd->forwardmove * 0.35f);
				cmd->sidemove = (short)(cmd->sidemove * 0.35f);
			}
		}
		else
			bot->breather_until = 0.0f;
	}

	{
		int		total = cmd->msec;
		int		sub = (int)sg_cv.subframes->value;
		int		base, rem, step;
		short	plain_forward = cmd->forwardmove;
		short	nav_jump = cmd->upmove;
		/* combat's own answer to "is there a fight on RIGHT NOW", as opposed
		 * to the up-to-two-seconds-old belief the surface terms were priced
		 * from. The weave below needs the live one. */
		qboolean engaged = false;
		qboolean nade_release = false;  /* exact-view irreversible throw frame */
		/* sg_airstrafe, decided once for the frame and spent per sub-step */
		qboolean as_ok = false;         /* the chain is live this frame */
		qboolean as_chain = false;      /* dose 2: hop chaining as well */
		float    as_lean = 0.0f;        /* the sinusoid, -1..1 */
		qboolean drop_recovery_failed = false;
		qboolean drop_replay_failed = false;

		if (sub < 1)
			sub = 1;
		/* A graph hook is proved as four literal 25 ms client commands in
		 * its water-source aim frame as well as every flight, pull, and settle
		 * interval. Do not let sg_subframes turn those boundaries into 13/26/39
		 * ms or make proof semantics configuration-dependent. Optional speed
		 * hooks are not graph proofs and keep the general subdivision policy. */
		if (bot->hook_link >= 0 && !bot->speedhook &&
		    bot->hook_phase >= 1 && bot->hook_phase <= 3 && total == 100)
			sub = 4;
		/* RUN/JUMP/DROP proofs are also rolled as literal 25 ms commands.
		 * Keep their acceleration, jump edge and lip crossing independent of
		 * the administrator's general sg_subframes texture knob. */
		if (proved_control && total == 100)
			sub = 4;
		if (sub > total)
			sub = total;            /* a step cannot be shorter than 1ms */
		base = total / sub;
		rem = total % sub;
		sub_steps = sub;


		if (!proved_control && bot->hook_phase != 1 && bot->hook_phase != 3 &&
		    !(bot->hook_phase == 2 && !bot->speedhook) &&
		    !SG_RocketJumpGameOwns(bot) && bot->nade_phase == 0)
		{
			SG_CombatFrame(e, cmd, &engaged);
			if (cmd->buttons & BUTTON_ATTACK)
				aimed_fire_requested = true;
		}

		/*
		 * The bomb sequence owns weapon, view, and trigger while it
		 * runs: switch (0.5s), cook with the button held (1.3s, view
		 * arced 25 degrees over the target's bearing), release -- the
		 * grenade code throws on release. Combat resumes next frame
		 * and the ladder takes the weapon back.
		 */
		if (!proved_control && bot->nade_phase == 1)
		{
			qboolean target_pending = SG_NadeTargetSwitching(bot);
			edict_t *pending_target = target_pending
			    ? SG_NadeBoundLiveTarget(e, bot) : NULL;

			/* Do not hold attack until the grenade switch has completed. */
			if (target_pending &&
			    (!tc->strike_pressure ||
			     SG_AttackFlagDirectTouchAuthority(e, team, NULL) ||
			     !pending_target ||
			     !SG_CanSee(e, pending_target->s.origin,
			                pending_target->viewheight)))
			{
				/* The target and direct-touch proof are re-read before the
				 * switch becomes a cook.  A pre-breach transaction never gets
				 * to borrow one trigger frame after its premise disappeared. */
				bot->nade_phase = 0;
				SG_NadeTargetClear(bot);
				SG_TimerArm(&bot->nade_next, 4.0f);
				cmd->buttons &= ~BUTTON_ATTACK;
			}
			else if (e->client->pers.weapon &&
			    e->client->pers.weapon->pickup_name &&
			    !Q_stricmp(e->client->pers.weapon->pickup_name,
			               "Grenades"))
			{
				bot->nade_phase = 2;
				/* Release timing below compares the remaining fuse with the
				 * solved flight time. */
				SG_TimerArm(&bot->nade_until, 3.2f);
				if (target_pending)
					bot->nade_target_cook_until = bot->nade_until;
				else
					SG_NadeTargetClear(bot);
				if (sg_cv.debug->value)
					sg_host.dprint("NADE %s cooking\n",
					           e->client->pers.netname);
			}
			else if (SG_TimerReady(bot->nade_until + 1.2f))
			{
				bot->nade_phase = 0;    /* switch never landed */
				SG_NadeTargetClear(bot);
				SG_TimerArm(&bot->nade_next, 4.0f);
			}
		}
		if (!proved_control && bot->nade_phase == 2)
		{
			qboolean armed_target = SG_NadeTargetCooking(bot);
			edict_t *nade_enemy = armed_target
			    ? SG_NadeArmedTarget(e, bot) : SG_CombatLiveEnemy(e);
			vec3_t pickup_aim;

			/* Never release a pre-breach grenade into a room that has gone
			 * quiet, after a target identity change, or after losing the
			 * target's current visibility.  The item-through-line resumes on
			 * the next frame. */
			if ((armed_target &&
			     (!tc->strike_pressure ||
			      SG_AttackFlagDirectTouchAuthority(e, team, NULL))) ||
			    (!armed_target && tc->strike_pressure &&
			     SG_AttackFlagTerminalAim(e, team, pickup_aim, NULL)) ||
			    !nade_enemy ||
			    !SG_CanSee(e, nade_enemy->s.origin, nade_enemy->viewheight))
			{
				bot->nade_phase = 0;
				SG_NadeTargetClear(bot);
				SG_TimerArm(&bot->nade_next, 4.0f);
				cmd->buttons &= ~BUTTON_ATTACK;
			}
			else
			{
				/* The target body, not the old arm-time coordinate, owns each
				 * cook frame.  Airborne lead below may replace this only for the
				 * current frame, and the next one starts at this live origin. */
				if (armed_target)
					VectorCopy(nade_enemy->s.origin, bot->nade_at);
			/* Lead an airborne target to its predicted landing point. Grounded
			 * targets keep the current observed position. */
			if (sg_cv.nadelead->value)
			{
				edict_t *len9 = nade_enemy;

				if (len9 && !len9->groundentity)
				{
					vec3_t lp0, lp1;
					trace_t lltr;
					/* The runtime may be bound to a supported non-800 law.
					 * Lead the same authoritative parabola ClientThink applies. */
					float ltt, lgrav = sv_gravity
					    ? sv_gravity->value : 800.0f;
					int lseg;

					VectorCopy(len9->s.origin, lp0);
					for (lseg = 1; lseg <= 30; lseg++)
					{
						ltt = 0.05f * (float)lseg;
						lp1[0] = len9->s.origin[0] + len9->velocity[0] * ltt;
						lp1[1] = len9->s.origin[1] + len9->velocity[1] * ltt;
						lp1[2] = len9->s.origin[2] + len9->velocity[2] * ltt
						       - 0.5f * lgrav * ltt * ltt;
						lltr = sg_host.trace(lp0, len9->mins, len9->maxs, lp1,
						                len9, MASK_PLAYERSOLID);
						if (lltr.fraction < 1.0f)
						{
							VectorCopy(lltr.endpos, bot->nade_at);
							bot->nade_at[2] += 24.0f;
							break;
						}
						VectorCopy(lp1, lp0);
					}
				}
			}

			/* Solve launch pitch from current cook-scaled speed and server
			 * gravity. */
			vec3_t na;
			float nyaw, npitch, nh;
			/* the engine's clock: timer remaining if released this frame
			 * is nade_until - now (p_weapon.c: grenade_time = cook_start
			 * + TIMER + 0.2, and nade_until holds exactly that sum) */
			float ntmr = SG_TimerRemaining(bot->nade_until);
			float nheld = 3.0f - (ntmr < 0.0f ? 0.0f
			                     : (ntmr > 3.0f ? 3.0f : ntmr));
			float nsp = 400.0f + nheld * ((800.0f - 400.0f) / 3.0f);
			float ng = e->client->ps.pmove.gravity
			           ? (float)e->client->ps.pmove.gravity : 800.0f;
			float ns2 = nsp * nsp, ndisc, nfly = -1.0f;

			VectorSubtract(bot->nade_at, e->s.origin, na);
			nh = sqrtf(na[0] * na[0] + na[1] * na[1]);
			nyaw = atan2f(na[1], na[0]) * 180.0f / (float)M_PI;
			ndisc = ns2 * ns2 - ng * (ng * nh * nh + 2.0f * na[2] * ns2);
			if (ndisc > 0.0f && nh > 32.0f)
			{
				float ntan = (ns2 - sqrtf(ndisc)) / (ng * nh);

				npitch = -atanf(ntan) * 180.0f / (float)M_PI;
				/* flight time from the same closed form: horizontal
				 * range over horizontal speed */
				nfly = nh * sqrtf(1.0f + ntan * ntan) / nsp;

				/* Trace the solved arc before committing the throw. */
				{
					vec3_t ap, lp;
					trace_t atr;
					float tstep = nfly / 6.0f, tt2;
					int seg;
					float cy = cosf(nyaw * (float)M_PI / 180.0f);
					float sy = sinf(nyaw * (float)M_PI / 180.0f);
					float hv2 = nsp / sqrtf(1.0f + ntan * ntan);
					float vv2 = hv2 * ntan;

					VectorCopy(e->s.origin, lp);
					lp[2] += e->viewheight;
					for (seg = 1; seg <= 6; seg++)
					{
						tt2 = tstep * (float)seg;
						ap[0] = e->s.origin[0] + cy * hv2 * tt2;
						ap[1] = e->s.origin[1] + sy * hv2 * tt2;
						ap[2] = e->s.origin[2] + e->viewheight
						      + vv2 * tt2 - 0.5f * ng * tt2 * tt2;
						atr = sg_host.trace(lp, NULL, NULL, ap, e,
						               MASK_SOLID);
						if (atr.fraction < 1.0f)
						{
							nfly = -2.0f;   /* blocked arc */
							break;
						}
						VectorCopy(ap, lp);
					}
				}
				if (nfly < -1.5f)
				{
					if (SG_NadeBlockedArcMayCancel(
					        (e->client->buttons & BUTTON_ATTACK) != 0))
					{
						/* No held-trigger frame exists yet: this switch/cook
						 * attempt is still reversible at zero projectile cost. */
						bot->nade_phase = 0;
						SG_NadeTargetClear(bot);
						SG_TimerArm(&bot->nade_next, 4.0f);
						cmd->buttons &= ~BUTTON_ATTACK;
					}
					/* Otherwise the grenade is physically live.  Keep phase two
					 * through the owned aim/release path below; clearing attack is
					 * the throw, not a cancellation. */
				}
			}
			else
				npitch = -atan2f(na[2], nh) * 180.0f / (float)M_PI
				         - 30.0f;
			/* A pre-hold blocked arc retires the transaction above.  A live cook
			 * stays in phase two and releases through the owned path below. */
			if (bot->nade_phase == 2)
			{
			/* Keep navigation view during the cook and aim only on release. */
			if (nfly < -1.5f || !(sg_cv.flycook->value) ||
			    (nfly >= 0.0f && ntmr - 0.2f <= nfly + 0.15f) ||
			    ntmr <= 0.75f)
			{
				cmd->angles[YAW] = ANGLE2SHORT(nyaw)
				                - e->client->ps.pmove.delta_angles[YAW];
				cmd->angles[PITCH] = ANGLE2SHORT(npitch)
				                  - e->client->ps.pmove.delta_angles[PITCH];
			}
			/* Hold until the remaining fuse slightly undercuts flight time,
			 * producing an airburst before a floor bounce. */
			if (SG_NadeCookShouldHold(bot->nade_phase, ntmr, nfly))
				cmd->buttons |= BUTTON_ATTACK;
			else
			{
				cmd->buttons &= ~BUTTON_ATTACK;   /* the release throws */
				nade_release = true;
				bot->nade_phase = 0;
				SG_NadeTargetClear(bot);
				SG_TimerArm(&bot->nade_next, 8.0f);
				if (sg_cv.debug->value)
					sg_host.dprint("NADE %s thrown fly=%.2f fuse=%.2f\n",
					           e->client->pers.netname,
				           nfly, ntmr - 0.2f);
			}
			}
			}
		}
		/* Fire an opportunistic rocket toward a fresh sound-only belief when
		 * the impact is safe and the launcher is already equipped. */
		if (!proved_control && sg_cv.soundfire->value &&
		    !duel && !engaged && role != SG_ROLE_CARRY &&
		    bot->nade_phase == 0 && bot->hook_phase == 0 &&
		    SG_TimerReady(bot->soundfire_next) &&
		    e->client->pers.weapon &&
		    e->client->pers.weapon->pickup_name &&
		    !Q_stricmp(e->client->pers.weapon->pickup_name,
		               "Rocket Launcher"))
		{
			unsigned rejected15 = 0u;
			int chosen15 = -1;
			float chosen_range15 = 0.0f;
			int attempt15;

			/* Rank the earned regions independently of enemy client slot.  If
			 * the freshest candidate has no useful rocket surface, reject just
			 * that candidate and try the next-best observation. */
			for (attempt15 = 0; attempt15 < SG_MAX_ENEMY_TRACK; attempt15++)
			{
				int s15, best15 = -1;
				float best_time15 = 0.0f, best_range15 = 0.0f;

				for (s15 = 0; s15 < SG_MAX_ENEMY_TRACK; s15++)
				{
					sg_belief_enemy_t *en15 =
					    &sg_caco_enemies[SG_TeamIdx(team)][s15];
					vec3_t sd15;
					float sl15;

					if ((rejected15 & (1u << s15)) ||
					    en15->client < 0 ||
					    en15->client >= game.maxclients ||
					    en15->seed < 0 ||
					    en15->seed >= SG_Rune()->hdr.num_seeds ||
					    !en15->heard_only ||
					    !SG_SoundFireObservationFresh(level.time,
					        en15->seen_time, 2.0f))
						continue;
					VectorSubtract(SG_Rune()->seeds[en15->seed].origin,
					               e->s.origin, sd15);
					sl15 = VectorLength(sd15);
					if (!isfinite(sl15) || sl15 < 600.0f || sl15 > 1500.0f)
						continue;   /* too close = own splash; too
						             * far = pure noise */
					if (!SG_SoundFireCandidateBetter(en15->seen_time,
					        sl15, en15->client, best15 >= 0,
					        best_time15, best_range15,
					        best15 >= 0 ? sg_caco_enemies[
					            SG_TeamIdx(team)][best15].client : -1))
						continue;
					best15 = s15;
					best_time15 = en15->seen_time;
					best_range15 = sl15;
				}
				if (best15 < 0)
					break;
				/* Use the real rocket muzzle and first impact.  Range to a
				 * distant belief is not safety when a nearby wall would make
				 * the shot explode at our feet, and open space beyond the
				 * heard region cannot deliver the claimed splash. */
				if (!SoundFireImpactSafe(e, team,
				        SG_Rune()->seeds[sg_caco_enemies[
				            SG_TeamIdx(team)][best15].seed].origin))
				{
					rejected15 |= 1u << best15;
					continue;
				}
				chosen15 = best15;
				chosen_range15 = best_range15;
				break;
			}

			if (chosen15 >= 0)
				{
					sg_belief_enemy_t *en15 = &sg_caco_enemies[
					    SG_TeamIdx(team)][chosen15];
					vec3_t sd15;
					float sy15, sp15;

					VectorSubtract(SG_Rune()->seeds[en15->seed].origin,
					               e->s.origin, sd15);
					sy15 = atan2f(sd15[1], sd15[0])
					       * 180.0f / (float)M_PI;
					sp15 = -atan2f(sd15[2],
					    sqrtf(sd15[0]*sd15[0] + sd15[1]*sd15[1]))
					       * 180.0f / (float)M_PI;

					cmd->angles[YAW] = ANGLE2SHORT(sy15)
					    - e->client->ps.pmove.delta_angles[YAW];
					cmd->angles[PITCH] = ANGLE2SHORT(sp15)
					    - e->client->ps.pmove.delta_angles[PITCH];
					cmd->buttons |= BUTTON_ATTACK;
					aimed_fire_requested = true;
					soundfire_owned = true;
					if (sg_cv.debug->value)
						sg_host.dprint("SNDFIRE %s rng=%.0f\n",
						           e->client->pers.netname,
						           chosen_range15);
				}
		}

		bot->engaged_last = engaged;

		/*
		 * Combat re-aimed: rebuild the movement basis from the view pmove
		 * will ACTUALLY use. Solving the strafe against the navigation
		 * basis while flying the combat view made the engine reconstruct
		 * a different direction than the one asked for -- the bot ran
		 * down its AIM instead of its route on every engaged frame (the
		 * duel implementation's flagged coupling, now closed).
		 */
		if (engaged)
		{
			vec3_t basis;

			basis[YAW] = SHORT2ANGLE((short)(cmd->angles[YAW] +
			             e->client->ps.pmove.delta_angles[YAW]));
			basis[PITCH] = SHORT2ANGLE((short)(cmd->angles[PITCH] +
			               e->client->ps.pmove.delta_angles[PITCH]));
			if (basis[PITCH] > 180.0f)
				basis[PITCH] -= 360.0f;
			basis[PITCH] /= 3.0f;
			basis[ROLL] = 0.0f;
			AngleVectors(basis, basis_fwd, basis_right, NULL);
		}
		if (!aimed_fire_requested && !nade_release &&
		    SG_TimerPending(bot->beat_until))
		{
			/* bot->engaged_last is this same value by here -- it was
			 * assigned from `engaged` a few lines up, so the live read
			 * is the one worth making */
			if (engaged || SG_HurtSince(e, bot->beat_from))
				bot->beat_until = 0.0f;
			else
			{
				float span = bot->beat_until - bot->beat_from;
				float t = (span > 0.001f)
				          ? SG_Age(bot->beat_from) / span : 1.0f;
				float yaw = SHORT2ANGLE((short)(cmd->angles[YAW] +
				            e->client->ps.pmove.delta_angles[YAW]));

				yaw += (float)bot->beat_sign * bot->beat_arc *
				       sinf(t * 2.0f * (float)M_PI);
				cmd->angles[YAW] = ANGLE2SHORT(yaw)
				                - e->client->ps.pmove.delta_angles[YAW];
			}
		}


		{
			float dose = sg_cv.airstrafe->value;
			float sp = sqrtf(e->velocity[0] * e->velocity[0] +
			                 e->velocity[1] * e->velocity[1]);

			if (!aimed_fire_requested && !proved_control && !nade_release &&
			    dose > 0.0f && sp >= SG_AS_FLOOR && have_move &&
			    run_link && open_ahead && bestlink >= 0 && SG_Rune() &&
			    !precision && !engaged &&
			    bot->hook_phase == 0 && !SG_RocketJumpGameOwns(bot) &&
			    bot->nade_phase == 0 && bot->term_brake >= 1.0f &&
			    e->waterlevel <= 1 && SG_TimerReady(bot->beat_until) &&
			    !(e->client->ps.pmove.pm_flags & PMF_DUCKED) &&
			    !SG_NearAFlag(e, SG_AS_FLAGKEEP))
			{
				vec3_t	vdir;
				float	cross, dot, err;
				/* the bar on the road, and the lower bar a chain ALREADY
				 * running is held to. A player who has committed to a
				 * chain does not re-audit the corridor every tenth of a
				 * second and stop dead when it narrows; he finishes the
				 * hop he is in. Without the hysteresis the road test
				 * chatters on and off across the bar and no chain lives
				 * long enough to reach the speeds the technique is for. */
				float	aswant = SG_AS_RUN / SG_PersonaHookScale(e);

				if (bot->as_since != 0.0f)
					aswant *= SG_AS_HOLD;

				vdir[0] = e->velocity[0] / sp;
				vdir[1] = e->velocity[1] / sp;
				vdir[2] = 0.0f;
				/* signed error from travel to route: positive is the route
				 * lying counter-clockwise, which is the sign a positive
				 * lean rotates the wish toward */
				cross = vdir[0] * move_dir[1] - vdir[1] * move_dir[0];
				dot = vdir[0] * move_dir[0] + vdir[1] * move_dir[1];
				err = atan2f(cross, dot) * 180.0f / (float)M_PI;

				if (err > -SG_AS_ABORT && err < SG_AS_ABORT &&
				    SG_RunRoom(e, SG_Rune()->links[bestlink].to,
				               route_field, move_dir, aswant))
				{
					float dt = (float)total / 1000.0f;

					as_ok = true;
					as_chain = (dose >= 2.0f);

					/* The first lean is part of this bot life, not a squad-wide
					 * zero phase. Teammates entering one road together therefore
					 * spread across its width instead of hopping in lockstep. */
					if (bot->as_since == 0.0f)
						bot->as_phase = SG_AirStrafeInitialPhase(
						    bot->instance_token,
						    e->client->ctf.ctfid);
					bot->as_phase += 2.0f * (float)M_PI * dt / SG_AS_PERIOD;
					while (bot->as_phase > 2.0f * (float)M_PI)
						bot->as_phase -= 2.0f * (float)M_PI;

					as_lean = sinf(bot->as_phase) + err / SG_AS_CORR;
					if (as_lean > 1.0f)
						as_lean = 1.0f;
					if (as_lean < -1.0f)
						as_lean = -1.0f;
				}
			}

			if (as_ok)
			{
				/* the same closed form the command uses, at the frame's
				 * speed and one sub-step of frametime, turned into the
				 * VIEW's share of the swing and asked for through the slew
				 * below like any other heading */
				float	ft = (float)base / 1000.0f;
				float	accelspeed = SG_AIR_ACCEL * ft * 300.0f;
				float	c = (300.0f - accelspeed) / sp;
				float	lv, yaw;

				if (bot->as_since == 0.0f)
				{
					SG_Mark(&bot->as_since);
					bot->as_entry = sp;
					bot->as_peak = sp;
				}
				else if (sp > bot->as_peak)
					bot->as_peak = sp;

				if (c > 1.0f)
					c = 1.0f;
				if (c < -1.0f)
					c = -1.0f;
				lv = acosf(c) * 180.0f / (float)M_PI *
				     as_lean * SG_AS_VIEWSHARE;
				if (lv > SG_AS_VIEWMAX)
					lv = SG_AS_VIEWMAX;
				if (lv < -SG_AS_VIEWMAX)
					lv = -SG_AS_VIEWMAX;

				yaw = SHORT2ANGLE((short)(cmd->angles[YAW] +
				      e->client->ps.pmove.delta_angles[YAW]));
				yaw += lv;
				cmd->angles[YAW] = ANGLE2SHORT(yaw)
				                - e->client->ps.pmove.delta_angles[YAW];
			}
			else
			{
				if (bot->as_since != 0.0f)
				{
					float dur = SG_Age(bot->as_since);

					/* one sustained chain in eight: a fleet chaining hops
					 * would otherwise write a line a second per bot, and
					 * the log is for reading */
					if (dur >= SG_AS_MINCHAIN &&
					    sg_cv.debug->value &&
					    !(bot->as_said++ & 7))
						sg_host.dprint("ASCHAIN %s %.2fs entry=%.0f "
						           "peak=%.0f\n",
						           e->client->pers.netname, dur,
						           bot->as_entry, bot->as_peak);
					bot->as_since = 0.0f;
				}
				bot->as_phase = 0.0f;
			}
		}

		/* A proved graph ride holds the exact quantized fire view. The hook
		 * pull starts at the current-view muzzle every end frame, so letting
		 * navigation or combat rotate the eyes would change both rope length and
		 * velocity relative to the proof. */
	if (bot->hook_phase == 2 && !bot->speedhook)
	{
			bot->vy_cur = bot->hook_view[YAW];
			bot->vp_cur = bot->hook_view[PITCH];
			bot->view_on = true;
			cmd->angles[YAW] = ANGLE2SHORT(bot->hook_view[YAW])
			                - e->client->ps.pmove.delta_angles[YAW];
		cmd->angles[PITCH] = ANGLE2SHORT(bot->hook_view[PITCH])
		                  - e->client->ps.pmove.delta_angles[PITCH];
		cmd->angles[ROLL] = -e->client->ps.pmove.delta_angles[ROLL];
	}
	if (aimed_fire_requested)
	{
		aimed_fire_yaw = cmd->angles[YAW];
		aimed_fire_pitch = cmd->angles[PITCH];
	}

		/* Slew toward the requested view at the configured angular rate. */
		{
			float want_y = SHORT2ANGLE((short)(cmd->angles[YAW] +
			               e->client->ps.pmove.delta_angles[YAW]));
			float want_p = SHORT2ANGLE((short)(cmd->angles[PITCH] +
			               e->client->ps.pmove.delta_angles[PITCH]));
				float rate = SG_NadeReleaseSlewRate(nade_release,
				    sg_cv.turnrate->value);

			if (!bot->view_on || rate <= 0.0f)
			{
				bot->vy_cur = want_y;
				bot->vp_cur = want_p;
				bot->view_on = true;
			}
			slew_want_y = want_y;
			slew_want_p = want_p;
			slew_rate = rate;
		}

		if (!(as_ok && as_chain && !proved_control))
			SG_HumanSpeedTimerReset(&bot->as_landing);

		/* The defended stand normally has no leg command because Think_Move
		 * intentionally made hold_post a statue. A current combat target may
		 * borrow only this bounded leg, after every other movement owner has
		 * declined. The planner does not write cmd, view, weapon, or routing. */
	defcombat_active = DefenseCombatPlan(e, bot, team, tc, as_ok, engaged,
	    &defcombat_stand, &defcombat_enemy, defcombat_dir);
	if (defcombat_enemy && defcombat_enemy->client)
		defcombat_enemy_ctfid = defcombat_enemy->client->ctf.ctfid;

	for (step = 0; step < sub; step++)
		{
			qboolean drop_command_owned = false;
			usercmd_t drop_expected_command;

			memset(&drop_expected_command, 0, sizeof(drop_expected_command));
			cmd->msec = (byte)(base + (step < rem ? 1 : 0));
			if (!cmd->msec)
				continue;
			sub_msec = cmd->msec;

			if (slew_rate > 0.0f)
			{
				float dt = (float)cmd->msec / 1000.0f;
				float dy = slew_want_y - bot->vy_cur;
				float dp = slew_want_p - bot->vp_cur;
				float stepmax = slew_rate * dt;

				while (dy > 180.0f) dy -= 360.0f;
				while (dy < -180.0f) dy += 360.0f;
				while (dp > 180.0f) dp -= 360.0f;
				while (dp < -180.0f) dp += 360.0f;
				if (dy > stepmax) dy = stepmax;
				if (dy < -stepmax) dy = -stepmax;
				if (dp > stepmax) dp = stepmax;
				if (dp < -stepmax) dp = -stepmax;
				bot->vy_cur += dy;
				bot->vp_cur += dp;
				cmd->angles[YAW] = ANGLE2SHORT(bot->vy_cur)
				                - e->client->ps.pmove.delta_angles[YAW];
				cmd->angles[PITCH] = ANGLE2SHORT(bot->vp_cur)
				                  - e->client->ps.pmove.delta_angles[PITCH];
			}


			if (plain_forward > 0 && have_move && e->waterlevel <= 1)
			{
				vec3_t vb, vf, vr;
				float fl;

				vb[YAW] = bot->vy_cur;
				vb[PITCH] = bot->vp_cur / 3.0f;
				vb[ROLL] = 0.0f;
				AngleVectors(vb, vf, vr, NULL);
				fl = sqrtf(vf[0] * vf[0] + vf[1] * vf[1]);
				if (fl > 0.01f)
				{
					cmd->forwardmove = (short)((float)plain_forward *
					    (move_dir[0] * vf[0] + move_dir[1] * vf[1]) / fl);
					cmd->sidemove = (short)((float)plain_forward *
					    (move_dir[0] * vr[0] + move_dir[1] * vr[1]));
				}
				else
				{
					cmd->forwardmove = plain_forward;
					cmd->sidemove = 0;
				}
			}
			else
			{
				cmd->forwardmove = plain_forward;
				cmd->sidemove = 0;
			}
			/* The continuous DROP witness sends exactly forward=400 at its
			 * serialized yaw. View slew and world-course decomposition are useful
			 * for ordinary navigation, but would change this proved controller. */
			if (proved_drop)
			{
				double pyaw = tc->drop_yaw;
				short pyaw_command = 0;
				qboolean pyaw_command_valid = false;
				short drop_forward = bot->drop_started ? 400 : plain_forward;
				if (bot->drop_recover)
				{
					vec3_t recover_d;

					VectorSubtract(SG_Rune()->seeds[
					    SG_Rune()->links[bestlink].to].origin,
					    e->s.origin, recover_d);
					pyaw_command_valid = SG_DropReplayPlanarYawCommand(
					    recover_d[0], recover_d[1],
					    e->client->ps.pmove.delta_angles[YAW], &pyaw_command);
				}

				if (bot->drop_started && !bot->drop_walkoff &&
				    !bot->drop_recover)
				{
					rune_link_t *dl = &SG_Rune()->links[bestlink];
					vec3_t lipd, walk;
					float liph, behind;

					VectorSubtract(dl->anchor, e->s.origin, lipd);
					lipd[2] = 0.0f;
					liph = VectorLength(lipd);
					walk[0] = cosf(dl->heading *
					                  (2.0f * (float)M_PI / 256.0f));
					walk[1] = sinf(dl->heading *
					                  (2.0f * (float)M_PI / 256.0f));
					walk[2] = 0.0f;
					behind = DotProduct(lipd, walk);
					if (liph <= 8.0f || behind <= 0.0f || !e->groundentity)
						bot->drop_walkoff = true;
					else
						pyaw_command_valid = SG_DropReplayPlanarYawCommand(
						    lipd[0], lipd[1],
						    e->client->ps.pmove.delta_angles[YAW], &pyaw_command);
				}
				if (bot->drop_started && bot->drop_walkoff &&
				    !bot->drop_recover)
				{
					pyaw = SG_Rune()->links[bestlink].heading *
					       (360.0f / 256.0f);
					pyaw_command_valid = false;
				}

				cmd->angles[YAW] = pyaw_command_valid ? pyaw_command :
				    ANGLE2SHORT(pyaw) -
				    e->client->ps.pmove.delta_angles[YAW];
				cmd->angles[PITCH] = -e->client->ps.pmove.delta_angles[PITCH];
				cmd->angles[ROLL] = -e->client->ps.pmove.delta_angles[ROLL];
				cmd->forwardmove = drop_forward;
				cmd->sidemove = 0;
				cmd->upmove = 0;
				if (drop_recovery_failed)
					cmd->forwardmove = 0;
				if (bot->drop_started && bot->drop_replay_active &&
				    bot->commit_link == bestlink)
				{
					sg_replay_pose_t live_pose;
					sg_drop_live_result_t live_result;

					Drop_LivePose(e, &live_pose);
					live_result = SG_DropLivePreStep(&bot->drop_replay,
					    &bot->drop_replay_active, &bot->drop_replay_link,
					    bestlink, &live_pose, Drop_LiveShadowCommand, cmd);
					Drop_LiveSync(bot);
					Drop_LiveResultLog(e, bestlink, "prestep", &live_result);
					if (live_result.outcome != SG_DROP_LIVE_RUNNING)
					{
						Drop_LiveRetireNonRunning(e, bot, bestlink, "prestep",
						                          &live_result);
						drop_replay_failed = true;
						SG_DropLiveZeroCommand(cmd);
					}
					else
					{
						drop_expected_command = *cmd;
						drop_command_owned = true;
					}
				}
			}
			/* A proved jump is one direct arc. Once the launch tap is armed, mirror
			 * the oracle at every literal 25 ms boundary: re-aim at the endpoint,
			 * hold forward 400, and never manufacture a second jump on landing. */
			if (proved_jump && (bot->jump_started || tc->jump_launch))
			{
				vec3_t jumpd;
				float jyaw;

				VectorSubtract(SG_Rune()->seeds[
				    SG_Rune()->links[bestlink].to].origin,
				    e->s.origin, jumpd);
				jyaw = atan2f(jumpd[1], jumpd[0]) * 180.0f / M_PI;
				cmd->angles[YAW] = ANGLE2SHORT(jyaw) -
				                   e->client->ps.pmove.delta_angles[YAW];
				cmd->angles[PITCH] = -e->client->ps.pmove.delta_angles[PITCH];
				cmd->angles[ROLL] = -e->client->ps.pmove.delta_angles[ROLL];
				cmd->forwardmove = 400;
				cmd->sidemove = 0;
				cmd->upmove = (step == 0 && tc->jump_launch) ? 400 : 0;
			}
			if (step == 0 && !proved_drop && !proved_jump)
				cmd->upmove = nav_jump;

			/*
			 * Hop-fire has to enter pmove before the rope is fired. The former
			 * write lived after this entire ClientThink loop and was discarded
			 * when the next frame rebuilt the command. Spend the jump on the first
			 * sub-step whose slewed view is inside the eight-degree staging cone;
			 * ClientThink clears groundentity, so this remains a single tap.
			 */
			if (bot->hook_phase == 1 && bot->speedhook &&
			    !SG_TimerReadyStrict(bot->hook_deadline) &&
			    sg_cv.hopfire->value && e->groundentity)
			{
				float hdy = slew_want_y - bot->vy_cur;
				float hdp = slew_want_p - bot->vp_cur;

				while (hdy > 180.0f) hdy -= 360.0f;
				while (hdy < -180.0f) hdy += 360.0f;
				if (fabsf(hdy) < 8.0f && fabsf(hdp) < 8.0f)
					cmd->upmove = 400;
			}

			if (!DefenseCombatApplyDuelWeave(hold_post, proved_control,
				duel_hold, engaged, tc->touch_terminal, weave_side, cmd) &&
				have_move && !precision && bot->hook_phase == 0 &&
			         !proved_control)
			{
				sg_air_t		airs;
				const sg_air_t	*airp = NULL;
				vec3_t			mf, mr;

				VectorCopy(basis_fwd, mf);
				VectorCopy(basis_right, mr);
				if (as_ok)
				{
					vec3_t vb;

					airs.lean = as_lean;
					airs.chain = as_chain;
					airs.view_yaw = bot->vy_cur;
					airs.view_pitch = bot->vp_cur;
					airp = &airs;

					/*
					 * A chain swings the VIEW, so the basis the policy
					 * decomposes against has to be the swung one -- the
					 * frame basis was built from the heading that was
					 * ASKED for, and solving a lean against a view the
					 * engine is not holding points the wish somewhere
					 * else. Rebuilt per sub-step, the same way the course
					 * decomposition above is.
					 */
					vb[YAW] = bot->vy_cur;
					vb[PITCH] = bot->vp_cur;
					if (vb[PITCH] > 180.0f)
						vb[PITCH] -= 360.0f;
					vb[PITCH] /= 3.0f;
					vb[ROLL] = 0.0f;
					AngleVectors(vb, mf, mr, NULL);
				}

				if (!bot->terminal && !tc->touch_terminal)
					SG_MovePolicy(e, cmd, mf, mr, move_dir,
				              open_ahead, run_link,
				              (float)cmd->msec / 1000.0f, airp);
				if (role == SG_ROLE_CARRY && cmd->forwardmove != 0 &&
				    SG_CarrierJinkAllowed(bot->terminal,
				        tc->touch_terminal) &&
				    open_ahead &&
				    !sg_cv.noweave->value)
				{
					int s9;

					for (s9 = 0; s9 < SG_MAX_ENEMY_TRACK; s9++)
					{
						sg_belief_enemy_t *en9 =
						    &sg_caco_enemies[SG_TeamIdx(team)][s9];
						vec3_t threat_delta;

						if (en9->seed >= 0 &&
						    en9->seed < SG_Rune()->hdr.num_seeds)
						{
							VectorSubtract(
							    SG_Rune()->seeds[en9->seed].origin,
							    e->s.origin, threat_delta);
							if (!SG_CarrierJinkThreat(en9->client, en9->seed,
							        SG_Rune()->hdr.num_seeds, en9->heard_only,
							        SG_AgeUnder(en9->seen_time, 3.0f),
							        VectorLength(threat_delta)))
								continue;
							int side = SG_WeaveSideAt(bot->instance_token,
							    e->client->ctf.ctfid, level.time);

							SG_CarrierJinkApplyIfClear(e, side, cmd);
							break;
						}
					}
				}
			}

			/* Apply spawn-beat throttle after all movement reconstruction. */
			if (SG_TimerPending(bot->beat_until) && !proved_control)
			{
				cmd->forwardmove = (short)(cmd->forwardmove * 0.30f);
				cmd->sidemove = (short)(cmd->sidemove * 0.30f);
			}
			if (!tc->touch_terminal && bot->linger_hot && !proved_control &&
			    sg_cv.depace->value > 0.0f)
			{
				float dp = sg_cv.depace->value;

				cmd->forwardmove = (short)(cmd->forwardmove * dp);
				cmd->sidemove = (short)(cmd->sidemove * dp);
			}

			/* A quiet stand patrol is deliberate walking, not the full-speed
			 * micro-pacing it replaces.  Threats retire patrol authority in
			 * CommitLink before this stage, and proved mechanisms keep their
			 * exact submitted command. */
			if (tc->patrol_walk && role == SG_ROLE_DEFEND && bot->def_stand &&
			    !proved_control)
			{
				float patrol_throttle =
				    SG_DefensePatrolThrottle(sg_cv.patrol->value);

				cmd->forwardmove = (short)(cmd->forwardmove * patrol_throttle);
				cmd->sidemove = (short)(cmd->sidemove * patrol_throttle);
			}

			/* the terminal brake: cornering throttle at the stands,
			 * same final-word slot for the same reason */
			if (bot->term_brake < 1.0f && !proved_control)
			{
				cmd->forwardmove = (short)(cmd->forwardmove * bot->term_brake);
				cmd->sidemove = (short)(cmd->sidemove * bot->term_brake);
			}

			/* A graph hook's proof spends zero movement input while the rope owns
			 * velocity. Optional speed hooks remain an unproved live technique and
			 * may keep their running command. */
			if (bot->hook_phase == 2 && !bot->speedhook)
			{
				cmd->forwardmove = 0;
				cmd->sidemove = 0;
				cmd->upmove = 0;
			}
			if (step == 0 && bot->hook_phase == 2 && !bot->speedhook &&
			    Hook_GraphReleaseReady(e, bot))
				Hook_GraphRelease(e, bot, &hook_cut_in_step);

			/* The sole pull happens later in ClientEndServerFrame, exactly where
			 * humans receive it. These commands consume the previous end-frame
			 * velocity; no bot-private pre-pmove overwrite is allowed. The oracle's
			 * post-release rollout uses a zero command. Preserve
			 * the velocity it earned instead of accelerating or jumping during
			 * the remainder of this outer server frame; normal landing steering
			 * begins with phase 3 on the next frame. */
			if (hook_cut_in_step)
			{
				cmd->forwardmove = 0;
				cmd->sidemove = 0;
				cmd->upmove = 0;
			}

			/* Convert the world-space tangent against the combat-refreshed,
			 * actually slewed basis. This is intentionally the final ordinary
			 * walking writer; any identity/team/support change zeroes the leg
			 * rather than borrowing an old combat target for one more substep. */
			(void)DefenseCombatWriteFinal(e, bot, team, tc, as_ok,
				defcombat_active, defcombat_stand, defcombat_enemy,
				defcombat_enemy_ctfid, defcombat_dir, cmd);

			/* The shared feedback command is the final writer for RL_SWIM.
			 * It replaces view, movement, trigger and every optional modifier at
			 * each literal 25 ms boundary, exactly as ProveSwim submitted it. */
			if (proved_swim)
			{
				vec3_t destination;
				qboolean escape = swim_emergency || swim_hazard;
				int air_from = bot->seed;
				int air_seed;

				/* Think_TrackSeed preserves the departure identity while SWIM owns
				 * the body. On a dry-to-water edge that preserved seed has no air
				 * relaxation entry, even though the body is now submerged. The
				 * proved water endpoint is at most one local stroke away and is the
				 * correct graph state from which to escape. */
				if (escape && air_from >= 0 &&
				    !(SG_Rune()->seeds[air_from].flags & RSF_WATER) &&
				    (SG_Rune()->seeds[SG_Rune()->links[bestlink].to].flags &
				     RSF_WATER))
					air_from = SG_Rune()->links[bestlink].to;
				if (e->waterlevel >= 2 && air_from >= 0 &&
				    (SG_Rune()->seeds[air_from].flags & RSF_WATER))
					bot->swim_air_seed = air_from;
				air_seed = (escape && sg_airnext && air_from >= 0)
				         ? sg_airnext[air_from] : -1;

				if (air_seed >= 0 && air_seed < SG_Rune()->hdr.num_seeds)
					VectorCopy(SG_Rune()->seeds[air_seed].origin, destination);
				else if (escape)
				{
					VectorCopy(e->s.origin, destination);
					destination[2] += 256.0f;
				}
				else
					VectorCopy(SG_Rune()->seeds[
					    SG_Rune()->links[bestlink].to].origin, destination);
				if (!escape && bot->swim_replay_active)
				{
					sg_replay_pose_t live_pose;
					sg_swim_live_result_t live_result;

					Swim_LivePose(e, &live_pose);
					live_result = SG_SwimLivePreStep(&bot->swim_replay,
					    &bot->swim_replay_active, &bot->swim_replay_link,
					    bestlink, &live_pose, destination, SG_SwimCommand, cmd);
					Swim_LiveFallbackLog(e, bestlink, "prestep", &live_result);
				}
				else if (!SG_SwimCommand(e->s.origin, destination,
				                         &e->client->ps.pmove, cmd) && escape)
				{
					VectorCopy(e->s.origin, destination);
					destination[2] += 256.0f;
					SG_SwimCommand(e->s.origin, destination,
					               &e->client->ps.pmove, cmd);
				}
				bot->vy_cur = SHORT2ANGLE((short)(cmd->angles[YAW] +
				              e->client->ps.pmove.delta_angles[YAW]));
				bot->vp_cur = SHORT2ANGLE((short)(cmd->angles[PITCH] +
				              e->client->ps.pmove.delta_angles[PITCH]));
				bot->view_on = true;
			}
			/* Declared map mechanisms own their approach too. RL_TELEPORT walks
			 * into the serialized 16x16 pad trigger; RL_LIFT walks to the exact
			 * bottom ride point, then submits zero input while the matched plat
			 * pushes the body. Generic endpoint aim never touches either mechanism. */
			if (declared_control)
			{
				rune_link_t *decl = &SG_Rune()->links[bestlink];
				vec3_t dd, target, source;
				vec3_t door_effective_anchor;
				float yaw, horiz;
				byte msec = cmd->msec;
				qboolean hold = false;
				short source_pms[3];
				qboolean source_exact, source_rest, source_snapped = false;
				edict_t *door_trigger = NULL;
					qboolean door_wait_exact = false, door_wait_rest = false;
				qboolean door_wait_snapped = false;
				sg_button_execution_anchor_state_t button_anchor_state =
				    SG_BUTTON_EXECUTION_ANCHOR_BOTTOM;
				qboolean button_controller = declared_door &&
				    mechanism_binding.plan &&
				    mechanism_binding.plan->controller_kind ==
				        SG_MECHANISM_CONTROLLER_BUTTON_DOOR;
				qboolean direct_controller = declared_door &&
				    mechanism_binding.plan &&
				    mechanism_binding.plan->controller_kind ==
				        SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR;
				qboolean direct_drive = true;
				qboolean button_motion_hold = false;

				Ballistic_SourceFixed(decl, source, source_pms);
				VectorCopy(decl->anchor, door_effective_anchor);
				source_exact = Ballistic_SourceExact(e, source_pms);
				source_rest = Ballistic_SourceRest(e);
				if (!water_tele && !bot->declared_started &&
				    !source_exact && source_rest)
				{
					qboolean capture = true;

					if (declared_door)
					{
						vec3_t source_delta;

						VectorSubtract(source, e->s.origin, source_delta);
						capture = fabsf(source_delta[2]) <= 2.0f &&
						    source_delta[0] * source_delta[0] +
						    source_delta[1] * source_delta[1] <= 4.0f;
					}
					if (capture)
						source_snapped = Ballistic_CanonicalizeSource(e, source,
						    source_pms);
				}
				if (!water_tele && !bot->declared_started &&
				    source_exact && source_rest &&
				    (e->groundentity == g_edicts ||
				     SG_ImmutableSupport(e->groundentity)) &&
				    (declared_door
				         ? SG_OracleDoorEgressWaterSafe(
				               mechanism_binding.plan->controller_kind,
				               e->waterlevel, e->watertype)
				         : e->waterlevel == 0) &&
				    e->client->ps.pmove.pm_type == PM_NORMAL &&
				    !(e->client->ps.pmove.pm_flags & PMF_DUCKED) &&
				    !e->client->ps.pmove.pm_time && !source_snapped)
				{
					if (declared_door &&
					    (bot->hook_phase != 0 || SG_RocketJumpGameOwns(bot) ||
					     bot->nade_phase != 0 || e->client->hookstate != 0 ||
					     e->client->hook != NULL))
					{
						/* Another controller still owns the body.  No mover claim
						 * exists yet, so retire this candidate without submitting a
						 * mixed-law command or firing a rope after acquisition. */
						bot->commit_link = -1;
						bot->commit_until = 0.0f;
						return;
					}
					if (!declared_door)
						bot->declared_started = true;
					else
					{
						sg_compound_guard_result_t acquire_result =
						    SG_DeclaredDoorGuardAcquire(bot, bestlink);

						if (acquire_result == SG_COMPOUND_GUARD_OK)
						{
							bot->declared_started = true;
							bot->declared_start_frame = level.framenum;
							bot->declared_guard_paused = false;
							bot->declared_guard_pause_started = 0.0f;
							bot->declared_door_recovery_since = 0.0f;
						}
						else
						{
							/* NOT_CLEAR proves this unleased contender is already
							 * in the complete sweep.  Make it nonsolid through the
							 * normal death lifecycle before the current owner can
							 * ever release and allow the set to close. */
							if (acquire_result == SG_COMPOUND_GUARD_NOT_CLEAR)
								SG_DeclaredDoorTerminalDeath(bot);
							return;
						}
					}
				}

				if (declared_door)
				{
					short wait_fixed[3];
					qboolean binding_ok;
					int axis;

					binding_ok = DoorStep_DeclaredBindingForLink(bestlink,
					    bot->declared_started, &mechanism_binding);
					if (binding_ok)
					{
						door_trigger = mechanism_binding.entry_entity;
						button_controller = mechanism_binding.plan &&
						    mechanism_binding.plan->controller_kind ==
						        SG_MECHANISM_CONTROLLER_BUTTON_DOOR;
						direct_controller = mechanism_binding.plan &&
						    mechanism_binding.plan->controller_kind ==
						        SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR;
						if (button_controller && bot->declared_touched)
						{
							button_anchor_state = SG_ButtonExecutionAnchor(
							    &mechanism_binding, bot, e, decl->anchor,
							    decl->mechanism_anchor, decl->mode,
							    door_effective_anchor);
							if (button_anchor_state ==
							    SG_BUTTON_EXECUTION_ANCHOR_INVALID)
							{
								DoorStep_RetainFailedAuthority(bot, bestlink);
								return;
							}
							if (button_anchor_state ==
							        SG_BUTTON_EXECUTION_ANCHOR_MOVING &&
							    !(bot->declared_activated &&
							      bot->declared_door_retreat))
							{
								button_motion_hold = true;
								hold = true;
							}
						}
					}
					/* Reauthenticate the durable claim before this frame can snap,
					 * relink, arm, hold, or otherwise mutate the live body/action.
					 * Later checks deliberately remain at their immediate mutation
					 * boundaries as defense against intra-frame world drift. */
					if (bot->declared_started &&
					    SG_DeclaredDoorGuardAuthorize(bot, bestlink) !=
					        SG_COMPOUND_GUARD_OK)
					{
						DoorStep_RetainFailedAuthority(bot, bestlink);
						return;
					}
					if (bot->declared_started && !bot->declared_activated &&
					    direct_controller &&
					    bot->declared_start_frame != level.framenum)
					{
						for (axis = 0; axis < 3; axis++)
							wait_fixed[axis] =
							    (short)(door_effective_anchor[axis] * 8.0f);
						if (!SG_DeclaredDoorApproachExecutionBegin(bot,
						        &mechanism_binding,
						        source_pms, wait_fixed))
						{
							DoorStep_RetainFailedAuthority(bot, bestlink);
							return;
						}
					}
					if (!door_trigger)
					{
						if (bot->declared_started)
						{
							(void)DoorStep_AbortOrRetain(bot, bestlink);
							return;
						}
						else
							bot->commit_link = -1;
						hold = true;
					}
					else if (!bot->declared_started &&
					         (bot->declared_touched || bot->declared_triggered ||
					          bot->declared_activated))
					{
						bot->commit_link = -1;
						hold = true;
					}
					else if (bot->declared_started && !bot->declared_activated)
					{
						qboolean outside_ok;
						qboolean support_ok;

						outside_ok = SG_BoundDoorOutsideSweep(&mechanism_binding,
						    e->s.origin);
						support_ok = SG_ButtonExecutionSupportValid(
						    &mechanism_binding, bot, e);
						if (direct_controller &&
						    bot->declared_start_frame != level.framenum)
						{
							sg_door_approach_observation_t observation;
							sg_door_approach_result_t result;

							if (!DoorStep_ApproachIdentityCurrent(bot,
							        &mechanism_binding) ||
							    !DoorStep_ApproachObservation(e,
							        &mechanism_binding, &observation))
							{
								DoorStep_RetainFailedAuthority(bot, bestlink);
								return;
							}
							if (bot->declared_door_approach.phase !=
							    SG_DOOR_APPROACH_COMPLETE)
							{
								result = SG_DoorApproachPreStep(
								    &bot->declared_door_approach, &observation,
								    msec);
								if (result.reason !=
								    SG_DOOR_APPROACH_REASON_NONE)
								{
									DoorStep_RetainFailedAuthority(bot,
									    bestlink);
									return;
								}
								direct_drive = result.drive ? true : false;
								hold = !direct_drive;
							}
							else if (!Ballistic_SourceRest(e))
							{
								DoorStep_RetainFailedAuthority(bot, bestlink);
								return;
							}
						}
						else if (!outside_ok || !support_ok ||
						         !SG_OracleDoorEgressWaterSafe(
						             mechanism_binding.plan->controller_kind,
						             e->waterlevel, e->watertype) ||
						         e->client->ps.pmove.pm_type != PM_NORMAL ||
						         (e->client->ps.pmove.pm_flags & PMF_DUCKED) ||
						         e->client->ps.pmove.pm_time)
						{
							DoorStep_RetainFailedAuthority(bot, bestlink);
							return;
						}
						if (!outside_ok ||
						    !SG_OracleDoorEgressWaterSafe(
						        mechanism_binding.plan->controller_kind,
						        e->waterlevel, e->watertype))
						{
							DoorStep_RetainFailedAuthority(bot, bestlink);
							return;
						}
						if (bot->declared_start_frame == level.framenum)
							hold = true;
						else if (button_motion_hold ||
						         (button_controller && bot->declared_touched &&
						          button_anchor_state !=
						              SG_BUTTON_EXECUTION_ANCHOR_TOP))
							hold = true;
						else if (bot->commit_link >= 0 &&
						         (!direct_controller ||
						          bot->declared_door_approach.phase ==
						              SG_DOOR_APPROACH_COMPLETE))
						{
							vec3_t wait_delta;

							for (axis = 0; axis < 3; axis++)
								wait_fixed[axis] =
								    (short)(door_effective_anchor[axis] * 8.0f);
							door_wait_exact = Ballistic_SourceExact(e,
							    wait_fixed);
							door_wait_rest = Ballistic_SourceRest(e);
							VectorSubtract(door_effective_anchor, e->s.origin,
							    wait_delta);
							if (!direct_controller &&
							    bot->declared_touch_frame != level.framenum &&
							    bot->declared_trigger_frame != level.framenum &&
							    !door_wait_exact && door_wait_rest &&
							    fabsf(wait_delta[2]) <= 2.0f &&
							    wait_delta[0] * wait_delta[0] +
							    wait_delta[1] * wait_delta[1] <= 4.0f)
								door_wait_snapped = Ballistic_CanonicalizeSource(e,
								    door_effective_anchor, wait_fixed);
							if (door_wait_snapped)
							{
								door_wait_exact = true;
								door_wait_rest = true;
							}
							if (direct_controller &&
							    (!door_wait_exact || !door_wait_rest))
							{
								DoorStep_RetainFailedAuthority(bot, bestlink);
								return;
							}
							/* When our trigger contact ran inside the preceding ClientThink,
							 * stop every remaining 25 ms command in this outer frame,
							 * then resume sweep-clear anchor capture next frame.  At an
							 * exact/rest anchor, a door set already held TOP by another
							 * activator is equally usable: the live egress reproof and
							 * remaining-open-window check below are sufficient evidence. */
							if (bot->declared_touch_frame == level.framenum ||
							    bot->declared_trigger_frame == level.framenum ||
							    door_wait_snapped ||
							    (door_wait_exact && !door_wait_rest))
								hold = true;
							else if (door_wait_exact && door_wait_rest)
							{
								int egress_window_ms;

								/* Reprove from the exact live TOP pose immediately
								 * before handoff. This supplies both collision truth
								 * and the controller's exact remaining duration; a
								 * chord/distance estimate is not the serialized path.
								 * The cheap TOP gate avoids a 200-step proof while the
								 * mechanism is cooling/moving, and the frame latch caps
								 * even a failed live proof to one attempt per frame. */
								/* Egress proof, execution and CommitLink retirement must
								 * share one 100 ms phase.  If anchor/rest becomes exact
								 * later in this frame, hold and revalidate after movers on
								 * the next outer-frame boundary. */
									if (step != 0 ||
								    SG_DeclaredDoorGuardAuthorize(bot, bestlink) !=
								        SG_COMPOUND_GUARD_OK ||
								    !SG_BoundDoorAtTop(&mechanism_binding) ||
								    bot->declared_egress_proof_frame == level.framenum)
									{
										hold = true;
								}
								else
								{
									qboolean egress_ok;
									qboolean window_ok = false;

									bot->declared_egress_proof_frame = level.framenum;
										egress_ok = button_controller
										    ? SG_OracleBoundButtonDoorEgress(
										          door_effective_anchor,
										          SG_Rune()->seeds[decl->to].origin,
										          &mechanism_binding, e, &egress_window_ms,
										          decl->mode == RLCM_RIDE
										              ? SG_BUTTON_SUPPORT_RIDER
										              : SG_BUTTON_SUPPORT_STATIC)
										    : SG_OracleBoundDoorEgress(door_effective_anchor,
										          SG_Rune()->seeds[decl->to].origin,
										          &mechanism_binding, e, &egress_window_ms);
										if (egress_ok && button_controller)
										{
											int rounded = ((egress_window_ms + 99) / 100) * 100;
											float remaining = mechanism_binding.entry_entity->nextthink >
											        level.time
											    ? (mechanism_binding.entry_entity->nextthink -
											       level.time) * 1000.0f : 0.0f;

											egress_ok = rounded == decl->sweep_clear_ms &&
											    button_anchor_state ==
											        SG_BUTTON_EXECUTION_ANCHOR_TOP &&
											    remaining + 0.01f >= egress_window_ms + 100;
										}
										if (egress_ok)
										window_ok = SG_BoundDoorAtTopFor(&mechanism_binding,
										    egress_window_ms + 100);
									if (egress_ok && window_ok)
										bot->declared_activated = true;
									else
										hold = true;
								}
							}
							}
						}
						/* Activation can become true in the unactivated branch above on
						 * this same step zero.  Deliberately use a second if, rather than
						 * an else-if, so that nominal handoff is immediately covered by
						 * the authoritative suffix proof and its four-command grant. */
						if (bot->commit_link >= 0 && bot->declared_started &&
						    bot->declared_activated && !button_motion_hold)
						{
							qboolean outside =
							    SG_BoundDoorOutsideSweep(&mechanism_binding,
							        e->s.origin);

							/* A failed forward suffix latches one recovery direction.  Do
							 * not alternate across the mover on successive live snapshots;
							 * once retreat owns the action it returns to the exact declared
							 * anchor and shelves the interrupted attempt there. */
							if (bot->declared_door_retreat && outside &&
							    SG_SupportedArrived(e->s.origin, door_effective_anchor,
							        e->groundentity != NULL, e->watertype, e->waterlevel, e))
							{
								DoorStep_StopOutside(e);
								if (!DoorStep_AbortOrRetain(bot, bestlink))
									return;
								return;
							}

							/* Movers and projectiles run before SG_RunFrame.  At the first
							 * 25 ms boundary, re-prove the complete suffix from that exact
							 * authoritative state and reserve enough TOP time for it.  The
							 * following three commands consume the same grant because no
							 * entity or mover loop interleaves this four-command frame. */
							if (step == 0)
							{
								int suffix_ms = 0;
								qboolean proved = false;

								bot->declared_door_suffix_ms = 0;
								/* Continue is a literal four-by-25 ms proof.  A malformed or
								 * nonstandard outer command cannot consume any part of it. */
								if (sub != 4 || base != 25 || rem != 0)
								{
									if (outside)
									{
										DoorStep_StopOutside(e);
										if (!DoorStep_AbortOrRetain(bot, bestlink))
											return;
									}
									else
									{
										DoorStep_RetainFailedAuthority(bot, bestlink);
									}
									return;
								}
								if (!bot->declared_door_retreat &&
								    SG_OracleBoundDoorContinue(e,
								        SG_Rune()->seeds[decl->to].origin,
								        &mechanism_binding,
								        &suffix_ms) &&
								    SG_BoundDoorAtTopFor(&mechanism_binding,
								        suffix_ms + 100))
									proved = true;
								else if (!bot->declared_door_retreat)
									bot->declared_door_retreat = true;

								if (!proved && bot->declared_door_retreat &&
								    SG_OracleBoundDoorContinue(e, door_effective_anchor,
								        &mechanism_binding, &suffix_ms) &&
								    SG_BoundDoorAtTopFor(&mechanism_binding,
								        suffix_ms + 100))
									proved = true;

								if (proved && button_controller &&
								    !bot->declared_door_retreat)
								{
									float remaining =
									    mechanism_binding.entry_entity->nextthink > level.time
									    ? (mechanism_binding.entry_entity->nextthink -
									       level.time) * 1000.0f : 0.0f;

									proved = button_anchor_state ==
									    SG_BUTTON_EXECUTION_ANCHOR_TOP &&
									    remaining + 0.01f >= suffix_ms + 100;
								}

								if (proved)
								{
									bot->declared_egress_proof_frame = level.framenum;
									bot->declared_door_suffix_ms = suffix_ms;
									bot->declared_door_recovery_since = 0.0f;
									/* Keep ownership through the complete freshly-proved suffix.
									 * In particular, a retreat may leave the sweep before it reaches
									 * the anchor; the generic timeout must not steal that safe exit. */
									if (bot->declared_door_retreat)
										SG_TimerArm(&bot->commit_until,
										    suffix_ms * 0.001f + 0.5f);
									door_suffix_grant = true;
								}
								else if (outside)
								{
									/* No live controller reaches either safe endpoint, but the
									 * body has not entered the mover envelope.  Stop the external
									 * velocity and retire without submitting an unproved Pmove. */
									DoorStep_StopOutside(e);
									if (!DoorStep_AbortOrRetain(bot, bestlink))
										return;
									return;
								}
								else
								{
									/* A live body can occupy both exits.  There is no safe
									 * controller command in that state: lease the already-TOP
									 * validated set, retain ownership, and retry next frame. */
									DoorStep_RetainFailedAuthority(bot, bestlink);
									return;
								}
							}
							else if (bot->declared_egress_proof_frame == level.framenum &&
							         bot->declared_door_suffix_ms > 0)
								door_suffix_grant = true;

							/* No activated egress movement exists without this frame's
							 * authoritative suffix proof and remaining-open reservation. */
							if (!door_suffix_grant)
								return;
						}
					}

					if (!bot->declared_started)
						VectorCopy(source, target);
					else if (bot->declared_activated)
					{
						if (declared_door && bot->declared_door_retreat)
							VectorCopy(door_effective_anchor, target);
						else
							VectorCopy(SG_Rune()->seeds[decl->to].origin, target);
					}
				else if (declared_door)
					VectorCopy(door_effective_anchor, target);
				else
					VectorCopy(decl->anchor, target);
				if (water_tele && !bot->declared_activated)
				{
					edict_t *pad = mechanism_binding.mover_entity;

					if (!pad || !SG_TeleportApproachPoint(pad, target))
					{
						bot->commit_link = -1;
						hold = true;
					}
				}
				if (!bot->declared_started &&
				    (source_snapped || (source_exact && !source_rest)))
					hold = true;
				if (declared_door && bot->declared_started &&
				    bot->declared_start_frame == level.framenum)
					hold = true;
				if (decl->action == RL_LIFT && bot->declared_started &&
				    !bot->declared_activated)
				{
					edict_t *plat = mechanism_binding.mover_entity;

					if (!plat)
					{
						bot->commit_link = -1;
						hold = true;
					}
					else if (SG_LiftRider(plat, e))
					{
						/* Boarding starts at the platform edge; the center trigger is
						 * inset. Keep the exact planar controller aimed at the anchor
						 * throughout the ride. At TOP, canonicalize the carried body to
						 * the center/rest state the egress oracle injected. Descend will
						 * observe that state next outer frame before advancing phase. */
						if (plat->moveinfo.state == SG_PLAT_STATE_TOP)
						{
							vec3_t top_body;
							short top_fixed[3];
							int axis;

							if (!SG_LiftTopRest(plat, e, top_body))
							{
								bot->commit_link = -1;
								hold = true;
							}
							else for (axis = 0; axis < 3; axis++)
							{
								top_fixed[axis] = (short)(top_body[axis] * 8.0f);
							}
							if (bot->commit_link >= 0 &&
							    !Ballistic_SourceExact(e, top_fixed) &&
							    Ballistic_SourceRest(e) &&
							    Ballistic_CanonicalizeSource(e, top_body, top_fixed))
								hold = true;
							else if (bot->commit_link >= 0 &&
							         Ballistic_SourceExact(e, top_fixed) &&
							         !Ballistic_SourceRest(e))
								hold = true;
						}
					}
					else if (plat->moveinfo.state != SG_PLAT_STATE_BOTTOM)
					{
						/* At TOP a center-trigger touch postpones the return by one
						 * second forever.  While UP/DOWN the empty shaft is equally
						 * unsafe.  Leave the expanded trigger footprint, then hold
						 * outside until the exact platform is boardable again. */
						hold = !SG_LiftWaitPoint(plat, e->s.origin, target);
					}
				}
				VectorSubtract(target, e->s.origin, dd);
				dd[2] = 0.0f;
				horiz = VectorLength(dd);
				yaw = horiz > 0.01f
				    ? atan2f(dd[1], dd[0]) * 180.0f / (float)M_PI
				    : e->client->v_angle[YAW];
				cmd->msec = msec;
				if (water_tele && !bot->declared_activated)
					SG_SwimCommand(e->s.origin, target,
					               &e->client->ps.pmove, cmd);
				else
					SG_DeclaredCommand(e->s.origin, target,
					                   &e->client->ps.pmove, cmd);
				/* Match the door oracle's final braking envelope before the first
				 * accepted activator touch.  The later per-step preflight still owns
				 * the complete mover sweep and can fail this command closed. */
				if (declared_door && bot->declared_started &&
				    !bot->declared_touched && horiz <= 64.0f &&
				    cmd->forwardmove > 64)
					cmd->forwardmove = 64;
				/* Exact-source capture owns a two-unit sweep; do not let the
				 * mechanism command's ordinary four-unit arrival deadband strand
				 * staging just outside it. */
				if (!bot->declared_started && !source_exact && horiz > 2.0f &&
				    cmd->forwardmove == 0)
					cmd->forwardmove = 40;
				/* Thin door activators may begin only 0.125u before their exact
				 * serialized wait point.  Until the accepted Touch_Multi callback
				 * arrives, keep the same slow command used by the oracle instead of
				 * entering the ordinary two-unit capture deadband just outside. */
				if ((decl->action == RL_LIFT || declared_door) &&
				    bot->declared_started && !bot->declared_activated &&
				    (horiz > 2.0f ||
				     (declared_door && !bot->declared_touched && horiz > 0.01f)) &&
				    cmd->forwardmove == 0)
					cmd->forwardmove = 40;
				if (hold)
				{
					cmd->forwardmove = 0;
					cmd->sidemove = 0;
					cmd->upmove = 0;
				}
				bot->vy_cur = yaw;
				bot->vp_cur = 0.0f;
				bot->view_on = true;
			}

			/* Door motion is an explicit, bounded command owner.  Think_Move
			 * decides whether this frame enters the activator, waits, or backs
			 * out of a rotating sweep; combat aim, air-strafe, carrier jink, and
			 * pacing all run later and must not silently replace that decision.
			 * Decompose the requested signed speed into the final view frame so
			 * looking at an enemy cannot turn a door approach sideways. */
			/* A generic moving door cannot reinterpret a reducer-owned DROP.
			 * Staging handles it before walkoff; after ownership begins, retire
			 * this transient integration conflict and spend zero input. */
			if (door_hold && !declared_door && drop_command_owned)
			{
				if (sg_cv.debug->value)
					sg_host.dprint("DROPREPLAYFALLBACK %s link=%d phase=final-command "
					               "adapter=door-owner replay=invalid-control\n",
					    e && e->client ? e->client->pers.netname : "?", bestlink);
				Drop_LiveIntegrationAbort(bot);
				drop_replay_failed = true;
				drop_command_owned = false;
				SG_DropLiveZeroCommand(cmd);
			}
			if (door_hold && !declared_door && !drop_replay_failed)
			{
				short door_speed = door_hold == SG_DOOR_DRIVE_RETREAT ? -200
				                 : (door_hold == SG_DOOR_DRIVE_FORWARD ? 400 : 0);

				cmd->upmove = 0;
				if (door_speed != 0 && e->waterlevel <= 1)
				{
					vec3_t door_view, door_fwd, door_right;
					float flat;

					VectorClear(door_view);
					door_view[YAW] = SHORT2ANGLE((short)(cmd->angles[YAW] +
					    e->client->ps.pmove.delta_angles[YAW]));
					AngleVectors(door_view, door_fwd, door_right, NULL);
					flat = sqrtf(door_fwd[0] * door_fwd[0] +
					             door_fwd[1] * door_fwd[1]);
					if (flat > 0.01f)
					{
						cmd->forwardmove = (short)((float)door_speed *
						    (move_dir[0] * door_fwd[0] +
						     move_dir[1] * door_fwd[1]) / flat);
						cmd->sidemove = (short)((float)door_speed *
						    (move_dir[0] * door_right[0] +
						     move_dir[1] * door_right[1]));
					}
					else
					{
						cmd->forwardmove = door_speed;
						cmd->sidemove = 0;
					}
				}
				else
				{
					cmd->forwardmove = door_speed;
					cmd->sidemove = 0;
				}
			}

			if (drop_replay_failed)
				SG_DropLiveZeroCommand(cmd);
			{
				qboolean guard_door_step = false;
				qboolean direct_door_prediction = false;
				sg_door_approach_prediction_t door_prediction;
				sg_rune_mechanism_binding_t door_step_binding;

				memset(&door_prediction, 0, sizeof(door_prediction));
				memset(&door_step_binding, 0, sizeof(door_step_binding));

				/* Projectiles have already applied any knockback this outer frame.
				 * Preflight the exact authoritative Pmove before ClientThink can run
				 * item, flag, weapon, or arbitrary-trigger side effects. */
				if (declared_door && bot->commit_link == bestlink &&
				    !bot->declared_activated)
				{
					if (DoorStep_DeclaredBindingForLink(bestlink,
					        bot->declared_started, &door_step_binding))
					{
						if (DoorStep_ApproachTicketRequired(bot,
						        &door_step_binding))
						{
							sg_door_approach_reason_t reason;

							guard_door_step =
							    SG_OracleBoundDoorApproachStep(e,
							        &door_step_binding, cmd,
							        &bot->declared_door_approach,
							        &door_prediction, &reason) &&
							    SG_RuneMechanismBindingCurrent(
							        &door_step_binding);
							direct_door_prediction = guard_door_step;
						}
						else
							guard_door_step =
							    SG_OracleBoundDoorStepSafe(e,
							        &door_step_binding, cmd) &&
							    SG_RuneMechanismBindingCurrent(
							        &door_step_binding);
					}
					if (!guard_door_step)
					{
						DoorStep_StopOutside(e);
						(void)DoorStep_AbortOrRetain(bot, bestlink);
						return;
					}
				}
				/* No command writer is permitted after this point.  Authenticate
				 * the actual logical fields, not merely the canonical shadow used
				 * by PreStep, immediately before the engine consumes them. */
				if (drop_command_owned)
				{
					sg_drop_live_result_t live_result;

					live_result = SG_DropLiveValidateFinalCommand(
					    &bot->drop_replay, &bot->drop_replay_active,
					    &bot->drop_replay_link, bestlink,
					    &drop_expected_command, cmd);
					Drop_LiveResultLog(e, bestlink, "final-command", &live_result);
					if (live_result.outcome != SG_DROP_LIVE_RUNNING)
					{
						Drop_LiveRetireNonRunning(e, bot, bestlink,
						                          "final-command", &live_result);
						drop_replay_failed = true;
						SG_DropLiveZeroCommand(cmd);
					}
				}
				if (drop_command_owned)
				{
					if (!SG_DropLiveEventsBeginCommand(&bot->drop_live_events,
					        &tc->drop_source_door_pending))
						(void)SG_DropLiveEventsLatch(&bot->drop_live_events,
						    true, false);
					VectorCopy(e->s.origin, bot->drop_live_step_origin);
				}
				/* Physics preflight and mover authority are independent.  Re-resolve
				 * and authorize the exact physical member set immediately before
				 * every ClientThink owned by a declared door. */
				if (declared_door && bot->declared_started &&
				    bot->commit_link == bestlink &&
				    SG_DeclaredDoorGuardAuthorize(bot, bestlink) !=
				        SG_COMPOUND_GUARD_OK)
				{
					DoorStep_RetainFailedAuthority(bot, bestlink);
					return;
				}
				if (declared_door && bot->declared_started &&
				    bot->commit_link == bestlink)
					bot->declared_door_recovery_since = 0.0f;
				if (direct_door_prediction &&
				    (!SG_RuneMechanismBindingCurrent(&door_step_binding) ||
				     !SG_DeclaredDoorApproachExecutionArm(bot,
				         &door_step_binding,
				         &door_prediction, step)))
				{
					DoorStep_RetainFailedAuthority(bot, bestlink);
					return;
				}
				bot->as_landing_command = as_ok && as_chain &&
				    !proved_control && !door_hold;
				/* The safety trace was made along aimed_fire_yaw/pitch.  Slew and
				 * every later command owner may move the submitted view, so the
				 * trigger is re-authorized at the final boundary on exact command
				 * bytes.  A sound shot keeps trying while its earned belief is fresh
				 * and spends its cadence only when this command can actually fire. */
				if (aimed_fire_requested)
				{
					/* The trace was taken at the outer frame's starting pose. If
					 * the first submitted command cannot carry its exact view, wait
					 * for the next outer frame to re-trace from the new position;
					 * do not authorize a later sub-step from stale geometry. */
					if (step == 0 && AimedFireViewReady(cmd, aimed_fire_yaw,
					                                          aimed_fire_pitch))
						aimed_fire_view_admitted = true;
					if (aimed_fire_view_admitted)
					{
						cmd->buttons |= BUTTON_ATTACK;
						if (soundfire_owned &&
						    SG_TimerReady(bot->soundfire_next))
							SG_TimerArm(&bot->soundfire_next, 8.0f);
					}
					else
						cmd->buttons &= ~BUTTON_ATTACK;
				}
				if (compound_drop && bot->compound_drop_live.guard_owned)
				{
					sg_compound_drop_live_host_t host;
					sg_compound_drop_live_result_t result;
					sg_replay_pose_t pose;

					if (!SG_CompoundDropGameHost(bot, &host) ||
					    !SG_CompoundDropGamePose(e, &pose))
					{
						SG_DeclaredDoorTerminalDeath(bot);
						return;
					}
					result = SG_CompoundDropLivePreStep(
					    &bot->compound_drop_live, &host, &pose, cmd);
					SG_CompoundDropGameDebugResult(bot, "prestep", &result,
					    &pose);
					if (!result.command_ready)
					{
						SG_DeclaredDoorTerminalDeath(bot);
						return;
					}
				}
				ClientThink(e, cmd);
				if (direct_door_prediction)
				{
					if (!SG_DeclaredDoorApproachExecutionFinish(bot,
					        &door_step_binding, e))
					{
						DoorStep_RetainFailedAuthority(bot, bestlink);
						return;
					}
					if (bot->declared_door_approach.phase ==
					    SG_DOOR_APPROACH_SNAP)
					{
						sg_door_approach_observation_t observation;
						sg_door_approach_result_t result;
						vec3_t anchor;
						short anchor_q8[3];
						int axis;

						for (axis = 0; axis < 3; axis++)
						{
							anchor_q8[axis] =
							    bot->declared_door_approach.anchor_q8[axis];
							anchor[axis] = anchor_q8[axis] * 0.125f;
						}
						if (!Ballistic_CanonicalizeSource(e, anchor,
						        anchor_q8) ||
						    !DoorStep_ApproachObservation(e,
						        &door_step_binding, &observation))
						{
							DoorStep_RetainFailedAuthority(bot,
							    bestlink);
							return;
						}
						result = SG_DoorApproachSnapped(
						    &bot->declared_door_approach, &observation);
						if (result.reason !=
						        SG_DOOR_APPROACH_REASON_NONE ||
						    !DoorStep_ApproachIdentityCurrent(bot,
						        &door_step_binding))
						{
							DoorStep_RetainFailedAuthority(bot,
							    bestlink);
							return;
						}
					}
				}
				if (drop_command_owned)
					Drop_LiveObserveDoorPassage(bot, e);
			}
			if (compound_drop && bot->compound_drop_live.guard_owned &&
			    step < sub - 1)
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
					return;
				}
				result = SG_CompoundDropLivePostStep(
				    &bot->compound_drop_live, &host, &pose, &observation);
				SG_CompoundDropGameDebugResult(bot, "poststep", &result,
				    &pose);
				if (result.outcome != SG_COMPOUND_DROP_LIVE_RUNNING &&
				    result.outcome != SG_COMPOUND_DROP_LIVE_RECOVERING)
				{
					SG_DeclaredDoorTerminalDeath(bot);
					return;
				}
			}
			if (proved_swim && bot->swim_replay_active &&
			    bot->commit_link == bestlink && step < sub - 1)
			{
				sg_replay_pose_t live_pose;
				sg_swim_live_result_t live_result;

				Swim_LivePose(e, &live_pose);
				live_result = SG_SwimLivePostStep(&bot->swim_replay,
				    &bot->swim_replay_active, &bot->swim_replay_link,
				    bestlink, &live_pose);
				Swim_LiveFallbackLog(e, bestlink, "poststep",
				                     &live_result);
			}
			if (proved_drop && bot->drop_replay_active &&
			    bot->commit_link == bestlink && step < sub - 1)
			{
				sg_drop_live_events_t live_events;
				sg_replay_pose_t live_pose;
				sg_drop_live_result_t live_result;

				Drop_LivePose(e, &live_pose);
				live_events = Drop_LiveEventsTake(bot);
				live_result = SG_DropLivePostStep(&bot->drop_replay,
				    &bot->drop_replay_active, &bot->drop_replay_link,
				    bestlink, &live_pose, Drop_LiveSupportValid(e),
				    &live_events);
				Drop_LiveSync(bot);
				Drop_LiveResultLog(e, bestlink, "poststep", &live_result);
				if (live_result.outcome != SG_DROP_LIVE_RUNNING)
				{
					Drop_LiveRetireNonRunning(e, bot, bestlink, "poststep",
					                          &live_result);
					drop_replay_failed = true;
				}
			}
			if (proved_drop && bot->drop_started && bot->drop_walkoff &&
			    !e->groundentity)
				bot->drop_airborne = true;
			if (proved_drop && bot->drop_recover &&
			    !bot->drop_replay_active && !drop_replay_failed &&
			    !drop_recovery_failed &&
			    (!e->groundentity ||
			     (e->groundentity != g_edicts &&
			      !SG_ImmutableSupport(e->groundentity))))
			{
				int b, oldest = 0;

				/* Legacy DROP rejects support loss at each submitted command.
				 * The active reducer owns 25/50/75 ms in PostStep and leaves
				 * command four pending for terminal-first Boundary evaluation.
				 * Stop the remaining commands in this frame, shelf the corrupted
				 * witness, and re-localize from the resulting real body. */
				for (b = 0; b < SG_BL_MAX; b++)
					if (bot->bl_until[b] < bot->bl_until[oldest])
						oldest = b;
				bot->bl_link[oldest] = bestlink;
				SG_TimerArm(&bot->bl_until[oldest], 10.0f);
				SG_TeachLinkFutility(bestlink);
				bot->commit_link = -1;
				bot->drop_link = -1;
				bot->drop_started = false;
				bot->drop_walkoff = false;
				bot->drop_airborne = false;
				bot->drop_recover = false;
				SG_DropLiveReset(&bot->drop_replay,
				    &bot->drop_replay_active, &bot->drop_replay_link,
				    &bot->drop_live_events);
				drop_recovery_failed = true;
				drop_replay_failed = true;
			}
			if ((proved_swim || water_tele) && bot->swim_validated &&
			    !swim_emergency && !swim_hazard &&
			    bot->commit_link == bestlink)
				bot->swim_elapsed_ms += SG_SWIM_STEP_MSEC;
			if (step == 0 && proved_jump && tc->jump_launch)
			{
				/* State becomes true only after the tap was actually submitted to
				 * Pmove. Late holds may rewrite the frame policy, but can no longer
				 * leave an armed action whose launch command never existed. */
				bot->jump_started = true;
				SG_TimerArm(&bot->commit_until,
				    SG_Rune()->links[bestlink].cost_ms * 0.001f + 0.5f);
				tc->jump_launch = false;
			}

			/* The hook proof permits release between its 25 ms usercmds, but
			 * velocity is not overwritten again until the next 100 ms boundary. */
			if (!hook_cut_in_step && bot->hook_phase == 2 && !bot->speedhook &&
			    Hook_GraphReleaseReady(e, bot))
			{
				Hook_GraphRelease(e, bot, &hook_cut_in_step);
			}

			/*
			 * Let go of the jump. PM_CheckJump clears PMF_JUMP_HELD only when
			 * a command arrives with upmove under 10 and refuses to jump at
			 * all while it is set, so holding the key buys nothing and costs
			 * the next hop. The release lands inside the same tenth of a
			 * second as the press.
			 */
			if (cmd->upmove >= 10)
				cmd->upmove = 0;
		}
		cmd->msec = (byte)sub_msec;
	}

	/*
	 * Hook lifecycle, after the think so v_angle reflects this frame's
	 * aim. Fire with the game's own Cmd_Hook_f -- the same entry the
	 * console command uses -- and release before the rope enters the
	 * p_weapon.c brake band (ladder starts at 120; 200 leaves a frame of
	 * margin at pull speed). A rope that never attached by its deadline
	 * is cut loose.
	 */
	{
		qboolean wet_graph_aim = bot->hook_phase == 1 && !bot->speedhook &&
		    Hook_LinkWaterSource(bot);
		qboolean wet_aim_hazard = wet_graph_aim && e->waterlevel > 0 &&
		    (e->watertype & (CONTENTS_LAVA | CONTENTS_SLIME));
		qboolean wet_aim_emergency = wet_graph_aim && e->waterlevel >= 3 &&
		    SG_TimerRemaining(e->air_finished) <
		        ((role == SG_ROLE_CARRY) ? 8.0f : 4.0f);

		/*
		 * A rope this bot does not think it owns is a rope it cannot ever
		 * release: g_cmds.c's Cmd_Unhook_f, when the grapple happens to be
		 * pers.weapon, only forces -attack and NEVER aborts -- the live
		 * hook's short-rope dead-stop then overwrites velocity with ~0
		 * every frame (p_weapon.c:2099-2104) and p_client.c:2834 zeroes
		 * gravity, freezing the bot in place for good (Trace, 96 seconds,
		 * 4v4 match). The bot releases through ctf_hook_abort directly --
		 * the same unconditional abort p_weapon.c itself calls -- and this
		 * guard clears any rope left over from a path we did not arm.
		 */
		if (bot->hook_phase == 0 && e->client->hookstate != 0)
			ctf_hook_abort(e);

		if (wet_graph_aim &&
		    (e->waterlevel < 2 || !(e->watertype & CONTENTS_WATER) ||
		     wet_aim_hazard || wet_aim_emergency))
		{
			if (sg_cv.debug->value)
				sg_host.dprint("HOOKWATERHOLD %s link=%d\n",
				           e->client->pers.netname, bot->hook_link);
			Hook_GraphFail(e, bot, wet_aim_hazard ? 30.0f : 1.0f);
		}
		else if (bot->hook_phase == 1 && SG_TimerReadyStrict(bot->hook_deadline))
		{
			qboolean failed_speedhook = bot->speedhook;
			int failed_link = bot->hook_link;

			/* the aim never arrived (blocked slew, moving anchor line,
			 * whatever): stand down clean and force a fresh route choice.
			 * Merely clearing phase 1 leaves Think_CommitLink holding the
			 * same hook, so the next frame walks straight back into the same
			 * aim wedge. A graph hook retires its link and advances the same
			 * failure streak as other demonstrated bad rides; a speed hook
			 * keeps its RUN commitment and its own cooldown. */
			if (failed_speedhook)
			{
				/* The first cooldown was armed when aiming began, so it can
				 * already be expired by the time the strict aim deadline fires.
				 * Start a fresh cooldown here or a permanently bad sky/muzzle
				 * candidate re-enters phase 1 on the very next frame. */
				float retry = (sg_cv.ropetravel->value > 0.0f) ? 1.0f :
				              (sg_cv.freeride->value > 0.0f) ? 2.0f : 4.0f;

				SG_TimerArm(&bot->speedhook_next,
				            retry / SG_PersonaHookScale(e));
			}
			else
			{
				Hook_DisciplineRetire(e, bot, failed_link, 5.0f, true,
				    "aim-retire", 0, 0);
			}
			if (sg_cv.debug->value)
				sg_host.dprint("HOOKAIMFAIL %s link=%d\n",
				           e->client->pers.netname,
				           failed_speedhook ? -1 : failed_link);
			if (failed_speedhook)
			{
				bot->hook_phase = 0;
				bot->speedhook = false;
				bot->speedhook_pull_applied = false;
				bot->flow_release = false;
				bot->hook_link = -1;
				bot->hook_bite_logged = false;
				bot->hook_deadline = 0.0f;
			}
		}
		else if (bot->hook_phase == 1)
		{
			/* The rope fires along the ACTUAL post-Pmove view. A graph proof
			 * waits for exact quantized equality; optional speed hooks retain
			 * their looser live-technique cone. */
			vec3_t desired_view;
			float ay, ap, ddy, ddp, graph_flight_dist = 0.0f;

			if (!bot->speedhook && bot->hook_link >= 0)
				VectorCopy(bot->hook_view, desired_view);
			else if (!SG_HookAimAngles(e->s.origin, e->viewheight,
			                               bot->hook_anchor, desired_view))
				goto hook_wait;
			ay = desired_view[YAW];
			ap = desired_view[PITCH];
			ddy = ay - bot->vy_cur;
			ddp = ap - bot->vp_cur;
			while (ddy > 180.0f) ddy -= 360.0f;
			while (ddy < -180.0f) ddy += 360.0f;
			while (ddp > 180.0f) ddp -= 360.0f;
			while (ddp < -180.0f) ddp += 360.0f;
			/* (quickrope's 10-degree carrier fire read NEGATIVE
			 * pooled 216-217 -- sloppy ropes ride worse than the
			 * ritual they save. The sniper's 3 stands for all.) */
			/* Hop-fire's eight-degree staging tap is submitted inside the
			 * ClientThink loop above; this post-think block only gates fire. */
			if (!bot->speedhook && bot->hook_link >= 0)
			{
				if ((short)ANGLE2SHORT(e->client->v_angle[PITCH]) !=
				        (short)ANGLE2SHORT(bot->hook_view[PITCH]) ||
				    (short)ANGLE2SHORT(e->client->v_angle[YAW]) !=
				    (short)ANGLE2SHORT(bot->hook_view[YAW]) ||
				    fabsf(e->client->v_angle[ROLL]) > 0.001f ||
				    !SG_HookOffhandReady(e))
					goto hook_wait;
			}
			else if (slew_rate > 0.0f &&
			         (fabsf(ddy) > 3.0f || fabsf(ddp) > 3.0f))
				goto hook_wait;

			if (!bot->speedhook && bot->hook_link >= 0)
			{
				rune_t *rune = SG_Rune();
				rune_link_t *hook_link;
				sg_hook_ride_worth_t worth;
				int link_index = bot->hook_link;
				int online;

				/* Aim may outlive its route field. Reprice the complete edge at
				 * the irreversible fire boundary. */
				if (!route_field || !rune || !rune->links || link_index < 0 ||
				    link_index >= rune->hdr.num_links ||
				    (hook_link = &rune->links[link_index])->action != RL_HOOK ||
				    hook_link->from < 0 || hook_link->from >= rune->hdr.num_seeds ||
				    hook_link->to < 0 || hook_link->to >= rune->hdr.num_seeds)
				{
					Hook_GraphFail(e, bot, 5.0f);
					goto hook_wait;
				}
				worth = SG_HookCurrentRideWorth(route_field[hook_link->from],
				    route_field[hook_link->to],
				    Fields_LinkTraversalCostMs(hook_link));
				if (!SG_HookRideLaunchAllowed(worth))
				{
					Hook_DisciplineRetire(e, bot, link_index, 5.0f, false,
					    worth == SG_HOOK_RIDE_UNASSESSED
					        ? "value-fire-unassessed" : "value-fire-skip",
					    route_field[hook_link->from],
					    route_field[hook_link->to]);
					goto hook_wait;
				}
				online = Hook_OnlineProof(e, bot, hook_link->anchor[ROLL],
				    &graph_flight_dist);

				if (online == HOOK_PROOF_BUSY)
				{
					/* Queueing is not an aim failure: keep the exact zero-input view
					 * and give this bot a fresh window behind the one-proof budget. */
					SG_TimerArm(&bot->hook_deadline, 3.0f);
					goto hook_wait;
				}
				if (online != HOOK_PROOF_OK)
				{
					if (sg_cv.debug->value)
						sg_host.dprint("HOOKREPROOFF %s link=%d\n",
						           e->client->pers.netname, bot->hook_link);
					Hook_GraphFail(e, bot, 5.0f);
					goto hook_wait;
				}
			}
			else
			{
				/* Optional speed hooks are not rune proofs; retain their live ray
				 * safety gate without forcing static-world traversal verification. */
				vec3_t sdir, sright, smuzzle, shot_end, to_anchor, miss;
				trace_t str;
				trace_t muzzle_tr;
				float shot_len;

				AngleVectors(e->client->v_angle, sdir, sright, NULL);
				CTF_HookMuzzle(e->s.origin, e->viewheight,
				               e->client->pers.hand, sdir, sright, smuzzle);
				muzzle_tr = sg_host.trace(e->s.origin, NULL, NULL, smuzzle,
				                             e, MASK_SHOT);
				VectorNormalize(sdir);
				VectorSubtract(bot->hook_anchor, smuzzle, to_anchor);
				graph_flight_dist = VectorLength(to_anchor);
				shot_len = graph_flight_dist + 8.0f;
				VectorMA(smuzzle, shot_len, sdir, shot_end);
				str = sg_host.trace(smuzzle, NULL, NULL, shot_end, e, MASK_SHOT);
				VectorSubtract(str.endpos, bot->hook_anchor, miss);
				if (muzzle_tr.startsolid || muzzle_tr.fraction < 1.0f ||
				    str.startsolid || str.fraction >= 1.0f ||
				    VectorLength(miss) > 48.0f ||
				    (str.surface && (str.surface->flags & SURF_SKY)) ||
				    (str.ent && str.ent->deadflag) ||
				    (str.ent && str.ent->client &&
				     str.ent->client->ctf.teamnum == e->client->ctf.teamnum))
				{
					if (sg_cv.debug->value)
						sg_host.dprint("HOOKLINEHOLD %s\n",
						           e->client->pers.netname);
					goto hook_wait;
				}
			}
			if (bot->speedhook || bot->hook_link < 0)
				VectorCopy(e->client->v_angle, bot->hook_view);
			Cmd_Hook_f(e);
			if (e->client->hookstate != 1 || !e->client->hook)
				goto hook_wait;
			/* Latch sg_debug at the irreversible successful-fire boundary. */
			Hook_DiagnosticBegin(bot, role);
			bot->hook_phase = 2;
			if (!bot->speedhook)
			{
				if (!Hook_LiveBeginAfterFire(e, bot, bot->hook_link,
				                             graph_flight_dist))
				{
					Hook_GraphFailDetail(e, bot, 15.0f, "begin-failed");
					goto hook_wait;
				}
				/* Bolt flight is quantized in 80-unit entity frames. This clock
				 * starts only after successful fire; aim time is not charged. */
				SG_TimerArm(&bot->hook_deadline,
				    ceilf(graph_flight_dist /
				          RUNE_HOOK_FRAME_DISTANCE) * 0.1f + 0.2f);
				bot->hook_attached_validated = false;
			}
		}
		else if (bot->hook_phase == 2)
		{
			vec3_t td;
			qboolean arrived, attached;


			if (sg_cv.debug->value &&
			    e->client->hook && e->client->hook->hook_target &&
			    !bot->hook_bite_logged)
			{
				vec3_t ba;
				edict_t *ht = e->client->hook->hook_target;

				VectorSubtract(e->client->hook->s.origin,
				               bot->hook_anchor, ba);
				if (VectorLength(ba) > 96.0f)
					sg_host.dprint("HOOKBITE %s off=%.0f into=%s org=(%.0f %.0f %.0f) want=(%.0f %.0f %.0f) got=(%.0f %.0f %.0f)\n",
					           e->client->pers.netname, VectorLength(ba),
					           ht->classname ? ht->classname :
					           (ht == g_edicts ? "world" : "?"),
					           e->s.origin[0], e->s.origin[1], e->s.origin[2],
					           bot->hook_anchor[0], bot->hook_anchor[1],
					           bot->hook_anchor[2],
					           e->client->hook->s.origin[0],
					           e->client->hook->s.origin[1],
					           e->client->hook->s.origin[2]);
				bot->hook_bite_logged = true;
			}
			{

			attached = (e->client->hookstate == 2 && e->client->hook != NULL);
			/*
			 * Release the way the prover released (SG_Rune().c:494-502):
			 * horizontally near the DESTINATION with the height nearly
			 * made, or rope inside the brake band. The old rope<200 cut
			 * every climb loose below its lip -- the bot slid back down
			 * and re-fired the same anchor forever.
			 */
			VectorSubtract(bot->hook_dest, e->s.origin, td);
			arrived = (td[0] * td[0] + td[1] * td[1] < 80.0f * 80.0f &&
			           td[2] > -96.0f && td[2] < 96.0f);


			if (bot->speedhook && attached)
			{
				float hd2 = sqrtf(td[0] * td[0] + td[1] * td[1]);
				float hv2 = sqrtf(e->velocity[0] * e->velocity[0] +
				                  e->velocity[1] * e->velocity[1]);

				if (hv2 > 300.0f && hd2 > 40.0f)
				{
					float tt = hd2 / hv2;
					float toward = (e->velocity[0] * td[0] +
					                e->velocity[1] * td[1]) / (hv2 * hd2);

					/*
					 * td[2] < 160: the cut is for HORIZONTAL finishes.
					 * Cutting a vertical climb throws the body up BESIDE
					 * the ledge lip to fall straight back down -- one
					 * shaft room turned the whole fleet into confused
					 * circlers immediately. Climbs ride to the
					 * top; that is what riding is FOR. And the parabola
					 * must clear ABOVE the destination, never scrape
					 * under it.
					 */
					if (tt < 1.2f && toward > 0.82f)
					{
						float grav = e->client->ps.pmove.gravity
						             ? (float)e->client->ps.pmove.gravity
						             : 800.0f;
						float zp = e->velocity[2] * tt
						         - 0.5f * grav * tt * tt;

						if (zp - td[2] > 24.0f && zp - td[2] < 260.0f)
						{
							/* Trace the fling parabola with the player's hull. A blocked
							 * arc keeps riding the rope. */
							{
								vec3_t ap0, ap1;
								trace_t atr;
								int aseg;
								qboolean arc_clear = true;

								VectorCopy(e->s.origin, ap0);
								for (aseg = 1; aseg <= 6; aseg++)
								{
									float at = tt * (float)aseg / 6.0f;

									ap1[0] = e->s.origin[0] + e->velocity[0] * at;
									ap1[1] = e->s.origin[1] + e->velocity[1] * at;
									ap1[2] = e->s.origin[2] + e->velocity[2] * at
									       - 0.5f * grav * at * at;
									atr = sg_host.trace(ap0, e->mins, e->maxs, ap1,
									               e, MASK_PLAYERSOLID);
									/* Ignore launch and landing contact. Intermediate
									 * segments reject only head-on wall or ceiling hits. */
									if (atr.fraction < 1.0f && aseg > 1 && aseg < 6)
									{
										vec3_t sd;
										float sl;
										VectorSubtract(ap1, ap0, sd);
										sl = VectorLength(sd);
										if (sl > 1.0f)
										{
											VectorScale(sd, 1.0f / sl, sd);
											/* A descending floor contact is a landing, not a
											 * collision veto. */
											if (atr.plane.normal[2] < 0.7f &&
											    DotProduct(sd, atr.plane.normal) < -0.7f)
											{
												arc_clear = false;
												break;
											}
										}
									}
									VectorCopy(ap1, ap0);
								}
								if (!arc_clear)
								{
									if (sg_cv.debug->value)
										sg_host.dprint("HOOKARCVETO %s\n",
										           e->client->pers.netname);
									goto hook_wait;
								}
							}
							ctf_hook_abort(e);
							bot->hook_phase = 3;
							bot->flow_release = true;
							SG_TimerArm(&bot->hook_deadline, 1.4f);
							/* Stop processing after a successful early cut so the
							 * generic release path cannot cancel phase three. */
							goto hook_wait;
						}
					}
				}
			}

			if (bot->speedhook)
			{
				float bs2 = e->velocity[0] * e->velocity[0]
				          + e->velocity[1] * e->velocity[1]
				          + e->velocity[2] * e->velocity[2];
				qboolean reached_speed = bs2 > 600.0f * 600.0f;
				qboolean rope_present = e->client->hookstate != 0 ||
				    e->client->hook != NULL;
				qboolean rope_coherent = e->client->hookstate != 0 &&
				    e->client->hook != NULL;

				if (reached_speed ||
				    SG_TimerReadyStrict(bot->hook_deadline) ||
				    !rope_coherent)
				{
					sg_speedhook_terminal_t terminal =
					    SG_SpeedHookTerminalFinish(bot, reached_speed,
					        e->client->hookstate, e->client->hook != NULL);
					const char *speed_end =
					    terminal == SG_SPEEDHOOK_TERMINAL_BURST ? "burst" :
					    (terminal == SG_SPEEDHOOK_TERMINAL_NOATTACH ?
					         "noattach" : "burststall");

					(void)SG_HookDiagnosticsFinish(&bot->hook_diagnostics,
					    speed_end, "speed-terminal");
					if (rope_present)
						ctf_hook_abort(e);
					if (sg_cv.debug->value)
						sg_host.dprint("HOOKSPEED %s %s\n",
						           e->client->pers.netname,
						           speed_end);
				}
			}
			else if ((attached && Hook_GraphReleaseReady(e, bot)) ||
			    SG_TimerReadyStrict(bot->hook_deadline) || e->client->hookstate == 0)
			{
				qboolean completed = attached &&
				    (arrived || Hook_GraphReleaseReady(e, bot));

				if (!completed)
					(void)SG_HookDiagnosticsFinish(&bot->hook_diagnostics,
					    "noattach", "legacy-graph");
				if (e->client->hookstate != 0)
					ctf_hook_abort(e);
				/* a cut live rope hands off to the landing steer; a rope
				 * that never attached does not */
				if (!completed)
				{
					int failed_link = bot->hook_link;

					/* An aborted bolt must not inherit the graph commitment that
					 * selected it, or the same sky/body/door shot re-arms at 10 Hz. */
					bot->commit_link = -1;
					if (SG_Rune() && failed_link >= 0 &&
					    failed_link < SG_Rune()->hdr.num_links)
					{
						int b, oldest = 0;

						for (b = 0; b < SG_BL_MAX; b++)
							if (bot->bl_until[b] < bot->bl_until[oldest])
								oldest = b;
						bot->bl_link[oldest] = failed_link;
						SG_TimerArm(&bot->bl_until[oldest], 15.0f);
					}
					bot->hook_link = -1;
				}
				bot->hook_phase = completed ? 3 : 0;
				/* This plain rope cut must not inherit a prior flow release. */
				bot->flow_release = false;
				SG_TimerArm(&bot->hook_deadline, 1.0f);
			}
			}
		}
		else if (hook_cut_in_step)
		{
			/* Phase 3 was entered inside the pmove loop at the oracle's release
			 * boundary. The common phase-3 handler owns landing/failure checks on
			 * the next frame; do not re-enter the phase-2 release machinery. */
			bot->commit_link = -1;
		}
	}

hook_wait:;
	/* the literal emission record: what this frame's usercmd contained */
	if (sg_cv.debug->value >= 2 ||
	    (sg_cv.debug->value && SG_TimerReady(bot->next_cmdlog)))
	{
		SG_TimerArm(&bot->next_cmdlog, 1.0f);
		/* the last step of the frame: fwd/side/up are that step's command,
		 * and msec x steps is how the frame's real time was spent */
		sg_host.dprint("CMD %s: fwd=%d side=%d up=%d btn=%d yaw=%d pitch=%d msec=%d x%d\n",
		           e->client->pers.netname, cmd->forwardmove, cmd->sidemove,
		           cmd->upmove, cmd->buttons, cmd->angles[YAW],
		           cmd->angles[PITCH], cmd->msec, sub_steps);
	}

	/* once a second, the full body state: enough to reconstruct any stall
	 * offline without another instrumented rerun */
	if (sg_cv.debug->value && SG_TimerReady(bot->next_report))
	{
		float sp = sqrtf(e->velocity[0] * e->velocity[0] +
		                 e->velocity[1] * e->velocity[1]);
		/* sgoal is the static destination-field cost used for route progress;
		 * unlike the composed goal, it changes only when the body moves. */
		int sgoal = -1;
		const int *sfld = SG_StrikeEnemyPressureSnapshot(bot)
		    ? ((team == CTF_TEAM_RED) ? sg_fields.to_blue_flag
		                              : sg_fields.to_red_flag)
		    : ((team == CTF_TEAM_RED) ? sg_fields.to_red_flag
		                              : sg_fields.to_blue_flag);

		if (bot->seed >= 0 && sfld && sfld[bot->seed] < SG_FIELD_INF)
			sgoal = sfld[bot->seed];
		SG_TimerArm(&bot->next_report, 1.0f);
		sg_host.dprint("SG %s: role=%d seed=%d goal=%d sgoal=%d spd=%.0f org=(%d %d %d) link=%d "
		           "act=%d hp=%d dh=%d dl=%d st=%.1f gnd=%d eng=%d frm=%d\n",
		           e->client->pers.netname, role, bot->seed,
		           (bot->seed >= 0 && goal_field[bot->seed] < SG_FIELD_INF)
		               ? goal_field[bot->seed] : -1,
		           sgoal,
		           sp, SG_TelemetryCoordinate(e->s.origin[0]),
		           SG_TelemetryCoordinate(e->s.origin[1]),
		           SG_TelemetryCoordinate(e->s.origin[2]),
		           bestlink,
		           (bestlink >= 0) ? SG_Rune()->links[bestlink].action : -1,
		           bot->hook_phase, door_hold, (int)drop_yaw_locked,
		           bot->stuck_time, e->groundentity != NULL,
		           (int)bot->engaged_last, level.framenum);
	}

	/*
	 * The same once-a-second cadence, for the eye instead of the log
	 * (sg_drawplan). Its own clock, because the debug report above is
	 * gated on sg_debug and the two are useful separately.
	 */
	if (SG_TimerReady(bot->plan_next))
	{
		SG_TimerArm(&bot->plan_next, 1.0f);
		SG_DrawPlan(bot, team, bestlink, route_field);
	}
}
