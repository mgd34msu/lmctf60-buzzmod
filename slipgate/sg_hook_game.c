/* Ordinary graph-hook game adapter and executor. */
#include "../g_local.h"
#include "../g_ctffunc.h"
#include "../g_tourney.h"
#include "sg_local.h"
#include "sg_bot.h"
#include "sg_clock.h"
#include "sg_cvars.h"
#include "sg_hook_discipline.h"
#include "sg_hook_game.h"
#include "sg_hook_live.h"
#include "sg_hook_oracle.h"
#include "sg_hooks.h"
#include "sg_rocketjump_game.h"
#include "sg_traversal_transition.h"
#include "sg_util.h"

void ClientThink(edict_t *ent, usercmd_t *ucmd);
void Cmd_Hook_f(edict_t *ent);

static int sg_hook_reproof_frame = -1;
static int sg_hook_reproof_slot = 0;

void SG_ChainHookGameReset(sg_bot_t *bot)
{
	if (!bot)
		return;
	memset(&bot->chain_hook_replay, 0, sizeof(bot->chain_hook_replay));
	bot->chain_hook_active = false;
	bot->chain_hook_link = -1;
	bot->chain_hook_leg = 0;
	bot->chain_hook_first_entity = NULL;
	memset(&bot->chain_hook_second_attach_pms, 0,
	       sizeof(bot->chain_hook_second_attach_pms));
	bot->chain_hook_second_attach_groundentity = false;
	bot->chain_hook_second_attach_watertype = 0;
	bot->chain_hook_second_attach_waterlevel = 0;
}

void SG_ChainHookGameStage(sg_bot_t *bot, int link_index)
{
	SG_ChainHookGameReset(bot);
	if (bot)
		bot->chain_hook_link = link_index;
}

qboolean SG_ChainHookGamePrepared(const sg_bot_t *bot, int link_index)
{
	return bot && link_index >= 0 && bot->chain_hook_link == link_index;
}

qboolean SG_ChainHookGameOwns(const sg_bot_t *bot)
{
	return bot && bot->chain_hook_active;
}

static sg_bot_t *HookGame_BotForEntity(edict_t *entity)
{
	int index;

	if (!entity)
		return NULL;
	for (index = 0; index < SG_MAXBOTS; index++)
		if (sg_bots[index].active && sg_bots[index].ent == entity)
			return &sg_bots[index];
	return NULL;
}

qboolean SG_HookGameReleaseReady(edict_t *e, const sg_bot_t *bot)
{
	vec3_t view, forward, right, muzzle, bite, velocity, dest_dir;
	sg_hook_replay_spec_t spec;
	sg_replay_pose_t pose;
	sg_replay_observation_t observation;
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
	if (bot->hook_proved_fling_release)
	{
		memset(&spec, 0, sizeof(spec));
		memset(&pose, 0, sizeof(pose));
		memset(&observation, 0, sizeof(observation));
		VectorCopy(bot->hook_dest, spec.destination);
		VectorCopy(e->s.origin, pose.origin);
		VectorCopy(e->velocity, pose.velocity);
		pose.pms.gravity = e->client->ps.pmove.gravity;
		observation.hook_rope_valid = true;
		observation.hook_rope_length = rope;
		return SG_HookReplayFlingReleaseReady(&spec, &pose, &observation);
	}
	return ((dest_dir[0] * dest_dir[0] + dest_dir[1] * dest_dir[1] <
	         80.0f * 80.0f && dest_dir[2] > -96.0f && dest_dir[2] < 96.0f) ||
	        rope < 130.0f);
}

void SG_HookGameRelease(edict_t *e, sg_bot_t *bot,
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

void SG_HookGameFailDetail(edict_t *e, sg_bot_t *bot,
	float shelf_seconds, const char *detail)
{
	int action = RL_HOOK;

	if (bot && SG_Rune() && bot->hook_link >= 0 &&
	    bot->hook_link < SG_Rune()->hdr.num_links &&
	    SG_Rune()->links[bot->hook_link].action == RL_CHAIN_HOOK)
		action = RL_CHAIN_HOOK;
	(void)SG_HookDiagnosticsFinish(&bot->hook_diagnostics, "graph-fail", detail);
	if (e && e->client && e->client->hookstate != 0)
		ctf_hook_abort(e);
	Hook_Shelve(bot, shelf_seconds);
	SG_StagedTraversalCancel(bot, action);
	SG_HookLiveDeactivate(&bot->hook_replay, &bot->hook_replay_active,
	    &bot->hook_replay_link);
	Hook_LiveClearFinalGuard(bot);
	bot->hook_entity = NULL;
	bot->hook_legacy_settle = false;
	bot->hook_legacy_arrived = false;
	SG_ChainHookGameReset(bot);
	bot->hook_pull_ms = 0;
	bot->hook_settle_ms = 0;
}

void SG_HookGameFail(edict_t *e, sg_bot_t *bot, float shelf_seconds)
{
	SG_HookGameFailDetail(e, bot, shelf_seconds, "legacy");
}

/* This is deliberately narrower than SG_HookGameFail: only a selected graph
 * link that could not be valued, decoded, or aimed owns this discipline. */
void SG_HookGameDisciplineRetire(edict_t *e, sg_bot_t *bot, int link_index,
	float shelf_seconds, qboolean failure, const char *reason,
	int from_goal, int to_goal)
{
	int gain = from_goal - to_goal;

	if (!bot || !SG_Rune() || link_index < 0 ||
	    link_index >= SG_Rune()->hdr.num_links ||
	    (SG_Rune()->links[link_index].action != RL_HOOK &&
	     SG_Rune()->links[link_index].action != RL_CHAIN_HOOK))
		return;
	Hook_ShelveLink(bot, link_index, shelf_seconds);
	if (sg_cv.debug->value)
	{
		if (failure)
			sg_host.dprint("HOOKDISC %s %s link=%d shelf=%.0f\n",
			    e && e->client ? e->client->pers.netname : "?",
			    reason ? reason : "retire", link_index, shelf_seconds);
		else
			sg_host.dprint(
			    "HOOKDISC %s %s link=%d from=%d to=%d gain=%d min=%d shelf=%.0f\n",
			    e && e->client ? e->client->pers.netname : "?",
			    reason ? reason : "value-skip", link_index,
			    from_goal, to_goal, gain, SG_HOOK_DISCIPLINE_SERVED_FIELD_MS,
			    shelf_seconds);
	}
	if (e && e->client && e->client->hookstate != 0)
		ctf_hook_abort(e);
	SG_StagedTraversalCancel(bot, SG_Rune()->links[link_index].action);
	bot->hook_pull_ms = 0;
	bot->hook_settle_ms = 0;
	SG_HookLiveDeactivate(&bot->hook_replay, &bot->hook_replay_active,
	    &bot->hook_replay_link);
	Hook_LiveClearFinalGuard(bot);
	bot->hook_entity = NULL;
	bot->hook_legacy_settle = false;
	bot->hook_legacy_arrived = false;
	SG_ChainHookGameReset(bot);
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

static sg_hook_game_proof_result_t Hook_ProofBudgetTake(
	const sg_bot_t *bot)
{
	int proof_slot = bot ? (int)(bot - sg_bots) : -1;

	if (proof_slot < 0 || proof_slot >= SG_MAXBOTS)
		return SG_HOOK_GAME_PROOF_FAIL;
	/* One expensive traversal witness per server frame, shared by ordinary
	 * and chain hooks. Rotate through the same ascending bot-slot order used by
	 * SG_RunFrame so a bad low slot cannot starve every later owner. */
	if (level.framenum < sg_hook_reproof_frame)
	{
		sg_hook_reproof_frame = -1;
		sg_hook_reproof_slot = 0;
	}
	if (sg_hook_reproof_frame == level.framenum ||
	    (sg_hook_reproof_frame == level.framenum - 1 &&
	     proof_slot <= sg_hook_reproof_slot))
		return SG_HOOK_GAME_PROOF_BUSY;
	sg_hook_reproof_frame = level.framenum;
	sg_hook_reproof_slot = proof_slot;
	return SG_HOOK_GAME_PROOF_OK;
}

static qboolean ChainHook_RayMatches(edict_t *e, const vec3_t view,
	const vec3_t bite);

/* Re-prove from the exact fixed-point state Cmd_Hook_f is about to consume.
 * The rune control is a planning prior; this witness is the executable
 * contract for the bot's actual position inside the source cell. */
sg_hook_game_proof_result_t SG_HookGameOnlineProof(edict_t *e, sg_bot_t *bot,
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
	int i, flight_ms;
	sg_hook_game_proof_result_t budget;

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
		return SG_HOOK_GAME_PROOF_FAIL;
	link = &SG_Rune()->links[bot->hook_link];
	if (link->action != RL_HOOK || bot->commit_link != bot->hook_link)
		return SG_HOOK_GAME_PROOF_FAIL;
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
		return SG_HOOK_GAME_PROOF_FAIL;
	VectorSubtract(SG_Rune()->seeds[link->from].origin, e->s.origin,
	               source_delta);
	if (source_delta[0] * source_delta[0] + source_delta[1] * source_delta[1] >
	        20.0f * 20.0f || fabsf(source_delta[2]) > 16.0f)
		return SG_HOOK_GAME_PROOF_FAIL;
	if (!source_water)
		for (i = 0; i < 3; i++)
			if ((short)(e->velocity[i] * 8.0f) != 0)
				return SG_HOOK_GAME_PROOF_FAIL;
	if ((short)ANGLE2SHORT(e->client->v_angle[PITCH]) !=
	        (short)ANGLE2SHORT(bot->hook_view[PITCH]) ||
	    (short)ANGLE2SHORT(e->client->v_angle[YAW]) !=
	        (short)ANGLE2SHORT(bot->hook_view[YAW]) ||
	    fabsf(e->client->v_angle[ROLL]) > 0.001f)
		return SG_HOOK_GAME_PROOF_FAIL;
	budget = Hook_ProofBudgetTake(bot);
	if (budget != SG_HOOK_GAME_PROOF_OK)
		return budget;

	AngleVectors(e->client->v_angle, forward, right, NULL);
	CTF_HookMuzzle(e->s.origin, e->viewheight, e->client->pers.hand,
	               forward, right, muzzle);
	muzzle_tr = sg_host.trace(e->s.origin, NULL, NULL, muzzle, e, MASK_SHOT);
	if (muzzle_tr.startsolid || muzzle_tr.fraction < 1.0f)
		return SG_HOOK_GAME_PROOF_FAIL;
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
		return SG_HOOK_GAME_PROOF_FAIL;
	if (Hook_OwnedSolidBlocksShot(e, muzzle, shot_tr.endpos))
		return SG_HOOK_GAME_PROOF_FAIL;
	VectorSubtract(shot_tr.endpos, muzzle, source_to_muzzle);
	*flight_distance = DotProduct(source_to_muzzle, forward);
	if (*flight_distance < 1.0f || *flight_distance > RUNE_HOOK_MAX_RAY)
		return SG_HOOK_GAME_PROOF_FAIL;
	VectorMA(muzzle, *flight_distance, forward, bot->hook_anchor);
	if (!SG_OracleHookFlightClear(muzzle, bot->hook_anchor))
		return SG_HOOK_GAME_PROOF_FAIL;
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
		return SG_HOOK_GAME_PROOF_FAIL;
	if (source_water)
	{
		float available_air = e->waterlevel >= 3
		    ? SG_TimerRemaining(e->air_finished) : 12.0f;
		float action_seconds =
		    (flight_ms + proof.pull_ms + proof.settle_ms) * 0.001f + 0.2f;

		if (available_air <= action_seconds)
			return SG_HOOK_GAME_PROOF_FAIL;
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
	bot->hook_proved_fling_release = proof.fling_release;
	bot->hook_proved_arrival_ms = proof.settle_arrival_ms;
	bot->hook_proved_settle_ms = proof.settle_ms;
	return SG_HOOK_GAME_PROOF_OK;
}

sg_hook_game_proof_result_t SG_ChainHookGameOnlineProof(edict_t *e,
	sg_bot_t *bot)
{
	rune_t *rune;
	rune_link_t *link;
	sg_chain_hook_proof_t proof;
	sg_phantom_t phantom;
	vec3_t controls[SG_CHAIN_HOOK_ROPE_COUNT], source_delta;
	sg_hook_game_proof_result_t budget;
	int i;

	if (!e || !e->client || !bot || !(rune = SG_Rune()) ||
	    !rune->links || !rune->seeds || bot->hook_link < 0 ||
	    bot->hook_link >= rune->hdr.num_links ||
	    level.intermissiontime || GamePaused() || e->health <= 0 ||
	    e->deadflag || e->movetype != MOVETYPE_WALK ||
	    e->client->ps.pmove.pm_type != PM_NORMAL ||
	    !SG_RunePhysicsCompatible(rune) ||
	    (e->client->ps.pmove.pm_flags & ~PMF_ON_GROUND) != 0 ||
	    e->client->ps.pmove.pm_time != 0 ||
	    fabsf(e->viewheight - 22.0f) > 0.1f ||
	    !SG_HookOffhandReady(e) || SG_RocketJumpGameOwns(bot) ||
	    bot->nade_phase != 0)
		return SG_HOOK_GAME_PROOF_FAIL;
	link = &rune->links[bot->hook_link];
	if (link->action != RL_CHAIN_HOOK ||
	    bot->commit_link != bot->hook_link || link->from < 0 ||
	    link->from >= rune->hdr.num_seeds || link->to < 0 ||
	    link->to >= rune->hdr.num_seeds ||
	    (rune->seeds[link->from].flags & RSF_WATER) ||
	    (rune->seeds[link->to].flags & RSF_WATER) || !e->groundentity ||
	    (e->groundentity != g_edicts &&
	     !SG_ImmutableSupport(e->groundentity)) || e->waterlevel != 0)
		return SG_HOOK_GAME_PROOF_FAIL;
	VectorSubtract(rune->seeds[link->from].origin, e->s.origin,
	               source_delta);
	if (source_delta[0] * source_delta[0] +
	        source_delta[1] * source_delta[1] > 20.0f * 20.0f ||
	    fabsf(source_delta[2]) > 16.0f)
		return SG_HOOK_GAME_PROOF_FAIL;
	for (i = 0; i < 3; i++)
		if ((short)(e->velocity[i] * 8.0f) != 0)
			return SG_HOOK_GAME_PROOF_FAIL;
	if ((short)ANGLE2SHORT(e->client->v_angle[PITCH]) !=
	        (short)ANGLE2SHORT(link->anchor[PITCH]) ||
	    (short)ANGLE2SHORT(e->client->v_angle[YAW]) !=
	        (short)ANGLE2SHORT(link->anchor[YAW]) ||
	    fabsf(e->client->v_angle[ROLL]) > 0.001f)
		return SG_HOOK_GAME_PROOF_FAIL;
	budget = Hook_ProofBudgetTake(bot);
	if (budget != SG_HOOK_GAME_PROOF_OK)
		return budget;

	memset(&phantom, 0, sizeof(phantom));
	phantom.pms = e->client->ps.pmove;
	phantom.old_pms = e->client->old_pmove;
	for (i = 0; i < 3; i++)
	{
		phantom.pms.origin[i] = (short)(e->s.origin[i] * 8.0f);
		phantom.pms.velocity[i] = (short)(e->velocity[i] * 8.0f);
		phantom.origin[i] = phantom.pms.origin[i] * 0.125f;
		phantom.velocity[i] = phantom.pms.velocity[i] * 0.125f;
	}
	phantom.pms.gravity = (short)sv_gravity->value;
	phantom.groundentity = true;
	phantom.watertype = e->watertype;
	phantom.waterlevel = e->waterlevel;
	VectorCopy(link->anchor, controls[0]);
	VectorCopy(link->mechanism_anchor, controls[1]);
	if (!SG_OracleChainHookTraverse(&phantom,
	        (const vec3_t *)controls, rune->seeds[link->to].origin,
	        RIGHT_HANDED, e->client->oldvelocity[2], &proof, e, true))
		return SG_HOOK_GAME_PROOF_FAIL;
	if (!ChainHook_RayMatches(e, proof.replay.rope[0].view_angles,
	        proof.replay.rope[0].bite))
		return SG_HOOK_GAME_PROOF_FAIL;

	SG_ChainHookGameReset(bot);
	bot->chain_hook_replay.spec = proof.replay;
	bot->chain_hook_link = bot->hook_link;
	VectorCopy(proof.replay.rope[0].bite, bot->hook_anchor);
	VectorCopy(proof.replay.rope[0].view_angles, bot->hook_view);
	VectorCopy(proof.replay.rope[0].destination, bot->hook_dest);
	VectorCopy(e->s.origin, bot->hook_source);
	for (i = 0; i < 3; i++)
		bot->hook_source[i] =
		    (short)(bot->hook_source[i] * 8.0f) * 0.125f;
	bot->hook_source_pms = e->client->ps.pmove;
	for (i = 0; i < 3; i++)
	{
		bot->hook_source_pms.origin[i] =
		    (short)(e->s.origin[i] * 8.0f);
		bot->hook_source_pms.velocity[i] =
		    (short)(e->velocity[i] * 8.0f);
	}
	bot->hook_source_water = false;
	bot->hook_source_health = e->health;
	bot->hook_attach_pms = proof.rope[0].attach_pms;
	bot->hook_attach_groundentity = proof.rope[0].attach_groundentity;
	bot->hook_attach_watertype = proof.rope[0].attach_watertype;
	bot->hook_attach_waterlevel = proof.rope[0].attach_waterlevel;
	bot->chain_hook_second_attach_pms = proof.rope[1].attach_pms;
	bot->chain_hook_second_attach_groundentity =
	    proof.rope[1].attach_groundentity;
	bot->chain_hook_second_attach_watertype = proof.rope[1].attach_watertype;
	bot->chain_hook_second_attach_waterlevel = proof.rope[1].attach_waterlevel;
	return SG_HOOK_GAME_PROOF_OK;
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

static void Hook_LivePose(const edict_t *e, sg_replay_pose_t *pose)
{
	SG_DropLivePose(pose, e && e->client ? &e->client->ps.pmove : NULL,
	    e ? e->s.origin : NULL, e ? e->velocity : NULL,
	    e && e->groundentity != NULL, e ? e->watertype : 0,
	    e ? e->waterlevel : 0);
}

static qboolean ChainHook_LinkCurrent(const sg_bot_t *bot)
{
	return bot && SG_Rune() && SG_Rune()->links && bot->chain_hook_active &&
	       bot->chain_hook_link >= 0 &&
	       bot->chain_hook_link < SG_Rune()->hdr.num_links &&
	       bot->hook_link == bot->chain_hook_link &&
	       bot->commit_link == bot->chain_hook_link &&
	       SG_Rune()->links[bot->chain_hook_link].action == RL_CHAIN_HOOK;
}

static qboolean ChainHook_BoltCurrent(const edict_t *e,
	const sg_bot_t *bot)
{
	edict_t *expected;

	if (!e || !e->client || !ChainHook_LinkCurrent(bot))
		return false;
	if (bot->chain_hook_replay.phase == SG_CHAIN_HOOK_REPLAY_SECOND_AIM ||
	    bot->chain_hook_replay.phase == SG_CHAIN_HOOK_REPLAY_WAIT_SECOND_FIRE)
		return e->client->hookstate == 0 && e->client->hook == NULL;
	if (bot->chain_hook_replay.phase != SG_CHAIN_HOOK_REPLAY_FIRST_ROPE &&
	    bot->chain_hook_replay.phase != SG_CHAIN_HOOK_REPLAY_SECOND_ROPE)
		return false;
	if (bot->chain_hook_replay.rope.phase == SG_HOOK_REPLAY_SETTLE)
		return e->client->hookstate == 0 && e->client->hook == NULL;
	expected = bot->chain_hook_leg == 0 ? bot->chain_hook_first_entity :
	                                      bot->hook_entity;
	return expected && e->client->hook == expected &&
	       (e->client->hookstate == 1 || e->client->hookstate == 2);
}

static qboolean ChainHook_AttachmentOK(edict_t *e, sg_bot_t *bot)
{
	const pmove_state_t *expected_pms;
	qboolean expected_ground;
	int expected_watertype, expected_waterlevel, i;
	vec3_t miss;

	if (!Hook_LiveWitnessOK(e, bot) || !ChainHook_BoltCurrent(e, bot) ||
	    e->client->hookstate != 2 ||
	    e->client->hook->hook_target != g_edicts)
		return false;
	expected_pms = bot->chain_hook_leg == 0 ? &bot->hook_attach_pms :
	    &bot->chain_hook_second_attach_pms;
	expected_ground = bot->chain_hook_leg == 0 ?
	    bot->hook_attach_groundentity :
	    bot->chain_hook_second_attach_groundentity;
	expected_watertype = bot->chain_hook_leg == 0 ?
	    bot->hook_attach_watertype : bot->chain_hook_second_attach_watertype;
	expected_waterlevel = bot->chain_hook_leg == 0 ?
	    bot->hook_attach_waterlevel : bot->chain_hook_second_attach_waterlevel;
	if (!!e->groundentity != !!expected_ground ||
	    (e->groundentity && e->groundentity != g_edicts &&
	     !SG_ImmutableSupport(e->groundentity)) ||
	    e->watertype != expected_watertype ||
	    e->waterlevel != expected_waterlevel)
		return false;
	VectorSubtract(e->client->hook->s.origin, bot->hook_anchor, miss);
	if (VectorLength(miss) > 0.5f)
		return false;
	for (i = 0; i < 3; i++)
		if ((short)(e->s.origin[i] * 8.0f) != expected_pms->origin[i] ||
		    (short)(e->velocity[i] * 8.0f) != expected_pms->velocity[i])
			return false;
	if (e->client->ps.pmove.pm_type != expected_pms->pm_type ||
	    e->client->ps.pmove.pm_flags != expected_pms->pm_flags ||
	    e->client->ps.pmove.pm_time != expected_pms->pm_time ||
	    e->client->ps.pmove.gravity != expected_pms->gravity ||
	    memcmp(&e->client->old_pmove, expected_pms,
	           sizeof(*expected_pms)) != 0)
		return false;
	VectorCopy(bot->hook_anchor, e->client->hook->s.origin);
	VectorSubtract(bot->hook_anchor, g_edicts->absmin,
	               e->client->hook->hook_offset);
	return true;
}

static qboolean ChainHook_AttachmentMaintained(edict_t *e, sg_bot_t *bot)
{
	return ChainHook_BoltCurrent(e, bot) && e->client->hookstate == 2 &&
	       e->client->hook->hook_target == g_edicts &&
	       Hook_AttachmentMaintained(e, bot);
}

static void ChainHook_Observation(const edict_t *e, const sg_bot_t *bot,
	sg_replay_observation_t *observation)
{
	memset(observation, 0, sizeof(*observation));
	if (!e || !e->client || !bot)
		return;
	if (bot->chain_hook_replay.phase == SG_CHAIN_HOOK_REPLAY_SECOND_ROPE &&
	    bot->chain_hook_replay.rope.phase == SG_HOOK_REPLAY_SETTLE)
		observation->contact_clear = Hook_SettleArrived(e, bot);
	if (e->client->hookstate == 2 && e->client->hook)
	{
		vec3_t view, forward, right, muzzle, bite, velocity;

		VectorCopy(bot->hook_view, view);
		AngleVectors(view, forward, right, NULL);
		CTF_HookMuzzle(e->s.origin, e->viewheight,
		               e->client->pers.hand, forward, right, muzzle);
		if (e->client->hook->hook_target)
			VectorAdd(e->client->hook->hook_target->absmin,
			          e->client->hook->hook_offset, bite);
		else
			VectorCopy(e->client->hook->s.origin, bite);
		observation->hook_rope_length =
		    CTF_HookPullVelocity(muzzle, bite, velocity);
		observation->hook_rope_valid =
		    observation->hook_rope_length >= 0;
	}
}

static qboolean ChainHook_RayMatches(edict_t *e, const vec3_t view,
	const vec3_t bite)
{
	vec3_t forward, right, muzzle, end, miss;
	trace_t muzzle_trace, shot_trace;

	if (!SG_HookOffhandReady(e) ||
	    (short)ANGLE2SHORT(e->client->v_angle[PITCH]) !=
	        (short)ANGLE2SHORT(view[PITCH]) ||
	    (short)ANGLE2SHORT(e->client->v_angle[YAW]) !=
	        (short)ANGLE2SHORT(view[YAW]) ||
	    fabsf(e->client->v_angle[ROLL]) > 0.001f)
		return false;
	AngleVectors(e->client->v_angle, forward, right, NULL);
	CTF_HookMuzzle(e->s.origin, e->viewheight, e->client->pers.hand,
	               forward, right, muzzle);
	muzzle_trace = sg_host.trace(e->s.origin, NULL, NULL, muzzle, e,
	                             MASK_SHOT);
	VectorNormalize(forward);
	VectorMA(muzzle, RUNE_HOOK_MAX_RAY, forward, end);
	shot_trace = sg_host.trace(muzzle, NULL, NULL, end, e, MASK_SHOT);
	VectorSubtract(shot_trace.endpos, bite, miss);
	return !muzzle_trace.startsolid && muzzle_trace.fraction >= 1.0f &&
	       !shot_trace.startsolid && shot_trace.fraction < 1.0f &&
	       shot_trace.ent == g_edicts &&
	       !(shot_trace.surface && (shot_trace.surface->flags & SURF_SKY)) &&
	       VectorLength(miss) <= 0.5f &&
	       !Hook_OwnedSolidBlocksShot(e, muzzle, shot_trace.endpos);
}

static qboolean ChainHook_SecondRayOK(edict_t *e, sg_bot_t *bot)
{
	const sg_hook_replay_spec_t *spec =
	    &bot->chain_hook_replay.spec.rope[1];

	return ChainHook_RayMatches(e, spec->view_angles, spec->bite);
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

/* SG_HookGameFail deliberately zeros the public clocks.  The legacy pull loop
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
	SG_HookGameFailDetail(e, bot,
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
		SG_HookGameFail(e, bot, 30.0f);
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
		SG_HookGameFail(e, bot, 30.0f);
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
			SG_HookGameFail(e, bot, 15.0f);
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
			SG_HookGameFail(e, bot, 15.0f);
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
			SG_HookGameFail(e, bot, 15.0f);
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
			SG_HookGameFail(e, bot, 15.0f);
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
		SG_HookGameFail(e, bot, 15.0f);
		return true;
	}
	if (bot->hook_replay.phase == SG_HOOK_REPLAY_WAIT_PULL)
	{
		/* The one production pull is acknowledged by SG_HookLiveEndFrame,
		 * immediately after Weapon_Hook_Fire in ClientEndServerFrame. */
		SG_HookGameFail(e, bot, 30.0f);
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

static qboolean ChainHook_Result(edict_t *e, sg_bot_t *bot,
	const char *phase, const sg_chain_hook_replay_result_t *result)
{
	if (!result || result->status == SG_REPLAY_RUNNING)
		return true;
	if (result->status == SG_REPLAY_ARRIVED)
	{
		(void)SG_HookDiagnosticsFinish(&bot->hook_diagnostics,
		    "arrived", "chain-reducer");
		bot->commit_link = -1;
		bot->hook_phase = 0;
		bot->hook_link = -1;
		bot->hook_entity = NULL;
		SG_ChainHookGameReset(bot);
		return false;
	}
	if (sg_cv.debug->value)
		sg_host.dprint("CHAINHOOKFAIL %s link=%d phase=%s reason=%s\n",
		    e && e->client ? e->client->pers.netname : "?",
		    bot ? bot->chain_hook_link : -1, phase ? phase : "?",
		    SG_ReplayReasonName(result->reason));
	SG_HookGameFailDetail(e, bot, 30.0f,
	    SG_ReplayReasonName(result->reason));
	return false;
}

qboolean SG_ChainHookGameBeginAfterFire(edict_t *e, sg_bot_t *bot,
	int link_index)
{
	sg_chain_hook_replay_spec_t spec;
	sg_chain_hook_replay_result_t result;
	sg_replay_pose_t pose;
	sg_replay_observation_t observation;

	if (!e || !e->client || !bot || !SG_Rune() || !SG_Rune()->links ||
	    link_index < 0 || link_index >= SG_Rune()->hdr.num_links ||
	    SG_Rune()->links[link_index].action != RL_CHAIN_HOOK ||
	    bot->hook_link != link_index || bot->commit_link != link_index ||
	    bot->chain_hook_link != link_index || e->client->hookstate != 1 ||
	    !e->client->hook)
		return false;
	spec = bot->chain_hook_replay.spec;
	Hook_LivePose(e, &pose);
	ChainHook_Observation(e, bot, &observation);
	result = SG_ChainHookReplayBegin(&bot->chain_hook_replay, &spec, &pose,
	    &observation, e->client->oldvelocity[2]);
	if (result.status != SG_REPLAY_RUNNING)
		return false;
	bot->chain_hook_active = true;
	bot->chain_hook_leg = 0;
	bot->chain_hook_first_entity = e->client->hook;
	bot->hook_entity = e->client->hook;
	bot->hook_phase = 2;
	bot->hook_attached_validated = false;
	SG_TimerArm(&bot->hook_deadline,
	    spec.rope[0].flight_ms * 0.001f + 0.2f);
	return true;
}

static qboolean ChainHook_WaitAttachFrame(sg_bot_t *bot, edict_t *e)
{
	sg_replay_pose_t pose;
	usercmd_t command;
	int step;

	memset(&command, 0, sizeof(command));
	command.msec = SG_REPLAY_STEP_MS;
	for (step = 0; step < SG_REPLAY_FRAME_MS / SG_REPLAY_STEP_MS; step++)
	{
		Hook_LivePose(e, &pose);
		if (!SG_HookReplayFixedViewCommand(&pose, bot->hook_view,
		        &command))
		{
			SG_HookGameFailDetail(e, bot, 15.0f, "chain-wait-control");
			return true;
		}
		ClientThink(e, &command);
	}
	return true;
}

static qboolean ChainHook_ActiveFrame(sg_bot_t *bot, edict_t *e)
{
	float frame_old_z;
	int step;

	if (!Hook_LiveWitnessOK(e, bot) || !ChainHook_LinkCurrent(bot) ||
	    !ChainHook_BoltCurrent(e, bot))
	{
		SG_HookGameFailDetail(e, bot, 15.0f, "chain-owner");
		return true;
	}
	if (bot->chain_hook_replay.phase == SG_CHAIN_HOOK_REPLAY_FIRST_ROPE ||
	    bot->chain_hook_replay.phase == SG_CHAIN_HOOK_REPLAY_SECOND_ROPE)
	{
		if (bot->chain_hook_replay.rope.phase == SG_HOOK_REPLAY_FLIGHT &&
		    ((bot->chain_hook_leg == 0 && !Hook_SourceStateOK(e, bot)) ||
		     SG_TimerReadyStrict(bot->hook_deadline)))
		{
			SG_HookGameFailDetail(e, bot, 15.0f, "chain-flight");
			return true;
		}
		if (bot->chain_hook_replay.rope.phase == SG_HOOK_REPLAY_WAIT_ATTACH &&
		    e->client->hookstate == 1)
			return ChainHook_WaitAttachFrame(bot, e);
		if (bot->chain_hook_replay.rope.phase == SG_HOOK_REPLAY_WAIT_ATTACH)
		{
			sg_chain_hook_replay_result_t result;
			sg_replay_pose_t pose;
			sg_replay_observation_t observation;

			if (!ChainHook_AttachmentOK(e, bot))
			{
				SG_HookGameFailDetail(e, bot, 15.0f,
				    "chain-attach-checkpoint");
				return true;
			}
			bot->hook_attached_validated = true;
			Hook_LivePose(e, &pose);
			ChainHook_Observation(e, bot, &observation);
			result = SG_ChainHookReplayEvent(&bot->chain_hook_replay,
			    SG_CHAIN_HOOK_REPLAY_EVENT_ATTACHED, &pose, &observation,
			    e->client->oldvelocity[2]);
			if (!ChainHook_Result(e, bot, "attached", &result))
				return true;
		}
		if ((bot->chain_hook_replay.rope.phase ==
		         SG_HOOK_REPLAY_ATTACH_FRAME ||
		     bot->chain_hook_replay.rope.phase == SG_HOOK_REPLAY_WAIT_PULL ||
		     bot->chain_hook_replay.rope.phase == SG_HOOK_REPLAY_PULL_FRAME) &&
		    !ChainHook_AttachmentMaintained(e, bot))
		{
			SG_HookGameFailDetail(e, bot, 15.0f, "chain-bite");
			return true;
		}
		if (bot->chain_hook_replay.rope.phase == SG_HOOK_REPLAY_WAIT_PULL)
		{
			SG_HookGameFailDetail(e, bot, 30.0f, "chain-pull-missed");
			return true;
		}
	}

	frame_old_z = e->client->oldvelocity[2];
	for (step = 0; step < SG_REPLAY_FRAME_MS / SG_REPLAY_STEP_MS; step++)
	{
		sg_chain_hook_replay_result_t result;
		sg_replay_pose_t pose;
		sg_replay_observation_t observation;
		usercmd_t command;

		Hook_LivePose(e, &pose);
		ChainHook_Observation(e, bot, &observation);
		memset(&command, 0, sizeof(command));
		command.msec = SG_REPLAY_STEP_MS;
		result = SG_ChainHookReplayPreStep(&bot->chain_hook_replay,
		    &pose, &observation, &command);
		if (!ChainHook_Result(e, bot, "prestep", &result))
			return true;
		ClientThink(e, &command);
		Hook_LivePose(e, &pose);
		ChainHook_Observation(e, bot, &observation);
		result = SG_ChainHookReplayPostStep(&bot->chain_hook_replay,
		    &pose, &observation, frame_old_z);
		if (!ChainHook_Result(e, bot, "poststep", &result))
			return true;
		if (result.effect == SG_CHAIN_HOOK_REPLAY_EFFECT_RELEASE)
		{
			ctf_hook_abort(e);
			Hook_LivePose(e, &pose);
			ChainHook_Observation(e, bot, &observation);
			result = SG_ChainHookReplayEvent(&bot->chain_hook_replay,
			    SG_CHAIN_HOOK_REPLAY_EVENT_RELEASE_APPLIED, &pose,
			    &observation, frame_old_z);
			if (!ChainHook_Result(e, bot, "release", &result))
				return true;
			bot->hook_phase =
			    bot->chain_hook_replay.phase == SG_CHAIN_HOOK_REPLAY_SECOND_AIM
			        ? 2 : 3;
		}
		else if (result.effect == SG_CHAIN_HOOK_REPLAY_EFFECT_FIRE_NEXT)
		{
			edict_t *first = bot->chain_hook_first_entity;

			if (!ChainHook_SecondRayOK(e, bot))
			{
				SG_HookGameFailDetail(e, bot, 30.0f,
				    "chain-second-reproof");
				return true;
			}
			VectorCopy(bot->chain_hook_replay.spec.rope[1].view_angles,
			           bot->hook_view);
			VectorCopy(bot->chain_hook_replay.spec.rope[1].bite,
			           bot->hook_anchor);
			Cmd_Hook_f(e);
			if (e->client->hookstate != 1 || !e->client->hook ||
			    e->client->hook == first)
			{
				SG_HookGameFailDetail(e, bot, 30.0f,
				    "chain-second-bolt");
				return true;
			}
			bot->hook_entity = e->client->hook;
			bot->chain_hook_leg = 1;
			bot->hook_attached_validated = false;
			result = SG_ChainHookReplayEvent(&bot->chain_hook_replay,
			    SG_CHAIN_HOOK_REPLAY_EVENT_NEXT_FIRED, &pose, &observation,
			    frame_old_z);
			if (!ChainHook_Result(e, bot, "second-fire", &result))
				return true;
			SG_TimerArm(&bot->hook_deadline,
			    bot->chain_hook_replay.spec.rope[1].flight_ms * 0.001f +
			        0.2f);
		}
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
	bot = HookGame_BotForEntity(e);
	if (bot && bot->speedhook && bot->hook_phase == 2 &&
	    e->client->hookstate == 2 && e->client->hook)
		bot->speedhook_pull_applied = true;
	if (bot && bot->chain_hook_active && ChainHook_LinkCurrent(bot) &&
	    bot->chain_hook_replay.rope.phase == SG_HOOK_REPLAY_WAIT_PULL)
	{
		sg_chain_hook_replay_result_t chain_result;
		sg_replay_observation_t observation;

		if (!ChainHook_AttachmentMaintained(e, bot))
		{
			SG_HookGameFailDetail(e, bot, 30.0f, "chain-pull-identity");
			return;
		}
		Hook_LivePose(e, &pose);
		ChainHook_Observation(e, bot, &observation);
		chain_result = SG_ChainHookReplayEvent(&bot->chain_hook_replay,
		    SG_CHAIN_HOOK_REPLAY_EVENT_PULL_APPLIED, &pose, &observation,
		    e->client->oldvelocity[2]);
		(void)ChainHook_Result(e, bot, "pull", &chain_result);
		return;
	}
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

qboolean SG_HookGameBeginAfterFire(edict_t *e, sg_bot_t *bot,
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
	spec.settle_limit_ms = bot->hook_proved_fling_release
	    ? SG_REPLAY_HOOK_FLING_SETTLE_MS
	    : (bot->hook_source_water ? RUNE_HOOK_WATER_SETTLE_MS
	                              : RUNE_HOOK_DRY_SETTLE_MS);
	spec.expected_release_ms = bot->hook_proved_release_ms;
	spec.fling_release = bot->hook_proved_fling_release;
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
	if (bot->chain_hook_active)
		return ChainHook_ActiveFrame(bot, e);
	if (bot->hook_replay_active)
		return Hook_LiveActiveFrame(bot, e);
	/* Online proof rejects harmful liquid on every 100 ms boundary. Dynamic
	 * combat can still perturb the live body after proof; retire that diverged
	 * witness before it deliberately continues through lava/slime. */
	if (e->waterlevel > 0 &&
	    (e->watertype & (CONTENTS_LAVA | CONTENTS_SLIME)))
	{
		SG_HookGameFail(e, bot, 30.0f);
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
				SG_HookGameFail(e, bot, 15.0f);
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
				SG_HookGameFail(e, bot, 30.0f);
			return true;
		}
		if ((!bot->hook_attached_validated && !Hook_AttachmentOK(e, bot)) ||
		    (bot->hook_attached_validated && !Hook_AttachmentMaintained(e, bot)))
		{
			SG_HookGameFail(e, bot, 15.0f);
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
				SG_HookGameFail(e, bot, 30.0f);
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
			ready = SG_HookGameReleaseReady(e, bot);
			if (ready && bot->hook_pull_ms == bot->hook_proved_release_ms)
				SG_HookGameRelease(e, bot, &cut);
			else if (ready ||
			         bot->hook_pull_ms >= bot->hook_proved_release_ms)
			{
				SG_HookGameFail(e, bot, 30.0f);
				failed = true;
			}
		}
		if (failed)
			return true;
		if (e->waterlevel > 0 &&
		    (e->watertype & (CONTENTS_LAVA | CONTENTS_SLIME)))
		{
			SG_HookGameFail(e, bot, 30.0f);
			return true;
		}
		if ((cut && bot->hook_pull_ms != bot->hook_proved_pull_ms) ||
		    (!cut && bot->hook_pull_ms >= bot->hook_proved_pull_ms))
		{
			SG_HookGameFail(e, bot, 30.0f);
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
				SG_HookGameFail(e, bot, 60.0f);
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
			SG_HookGameFail(e, bot, 60.0f);
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
				SG_HookGameFail(e, bot, 60.0f);
				failed = true;
			}
		}
		else if (bot->hook_settle_ms >= bot->hook_proved_arrival_ms)
		{
			SG_HookGameFail(e, bot, 60.0f);
			failed = true;
		}
	}
	if (!failed && e->waterlevel > 0 &&
	    (e->watertype & (CONTENTS_LAVA | CONTENTS_SLIME)))
	{
		SG_HookGameFail(e, bot, 60.0f);
		failed = true;
	}
	if (!failed && bot->hook_settle_ms == bot->hook_proved_settle_ms)
	{
		if (arrived && Hook_SettleArrived(e, bot))
			cut = true;
		else
		{
			SG_HookGameFail(e, bot, 60.0f);
			failed = true;
		}
	}
	else if (!failed && bot->hook_settle_ms > bot->hook_proved_settle_ms)
	{
		SG_HookGameFail(e, bot, 60.0f);
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
