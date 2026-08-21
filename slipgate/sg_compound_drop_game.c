/* Game boundary for the authenticated PREOPEN RL_DOOR_DROP controller. */
#include "../g_local.h"

#include <limits.h>
#include <string.h>

#include "sg_compound_drop_game.h"
#include "sg_compound_guard.h"
#include "sg_cvars.h"
#include "sg_hooks.h"
#include "sg_local.h"
#include "sg_bot.h"
#include "sg_util.h"

typedef struct compound_drop_game_current_s
{
	rune_t *rune;
	const sg_compound_publication_binding_t *binding;
	const sg_compound_world_preopen_t *mechanism;
	edict_t *member;
} compound_drop_game_current_t;

static int CompoundDropGameCurrent(sg_bot_t *bot,
	const sg_compound_drop_live_snapshot_t *snapshot,
	compound_drop_game_current_t *current)
{
	const sg_compound_publication_binding_t *binding;
	const sg_compound_world_preopen_t *mechanism;
	rune_t *rune;
	edict_t *member;

	if (current)
		memset(current, 0, sizeof(*current));
	if (!bot || !snapshot || !current || !bot->active || !bot->ent ||
	    !(rune = SG_Rune()) || !SG_RunePhysicsCompatible(rune) ||
	    !(binding = SG_CompoundPublicationBinding(rune,
	        snapshot->binding.link_index)) ||
	    binding->link.action != RL_DOOR_DROP ||
	    memcmp(binding, &snapshot->binding, sizeof(*binding)) != 0 ||
	    !(mechanism = SG_CompoundPublicationMechanism(rune, binding)) ||
	    mechanism->trigger_key != snapshot->trigger_key ||
	    mechanism->mover_key != snapshot->mover_key ||
	    !SG_CompoundWorldResolvedMember(mechanism, &member))
		return 0;
	current->rune = rune;
	current->binding = binding;
	current->mechanism = mechanism;
	current->member = member;
	return 1;
}

static sg_compound_drop_live_host_result_t CompoundDropGameBind(
	void *context, uint32_t link_index,
	sg_compound_drop_live_snapshot_t *snapshot)
{
	sg_bot_t *bot = (sg_bot_t *)context;
	const sg_compound_publication_binding_t *binding;
	const sg_compound_world_preopen_t *mechanism;
	rune_t *rune;
	edict_t *member;

	if (!snapshot)
		return SG_COMPOUND_DROP_LIVE_HOST_ERROR;
	memset(snapshot, 0, sizeof(*snapshot));
	if (!bot || !bot->active || !bot->ent || !(rune = SG_Rune()) ||
	    !SG_RunePhysicsCompatible(rune) ||
	    !(binding = SG_CompoundPublicationBinding(rune, link_index)) ||
	    binding->link.action != RL_DOOR_DROP ||
	    !(mechanism = SG_CompoundPublicationMechanism(rune, binding)) ||
	    !SG_CompoundWorldResolvedMember(mechanism, &member) || !member)
		return SG_COMPOUND_DROP_LIVE_HOST_DENIED;
	snapshot->binding = *binding;
	snapshot->trigger_key = mechanism->trigger_key;
	snapshot->mover_key = mechanism->mover_key;
	return snapshot->trigger_key > 0 && snapshot->mover_key > 0 ?
	       SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED :
	       SG_COMPOUND_DROP_LIVE_HOST_ERROR;
}

static int CompoundDropGameCheckpoint(const edict_t *entity,
	sg_compound_publication_checkpoint_t *checkpoint)
{
	if (!checkpoint)
		return 0;
	memset(checkpoint, 0, sizeof(*checkpoint));
	if (!entity || !entity->inuse || !entity->client)
		return 0;
	checkpoint->pms = entity->client->ps.pmove;
	checkpoint->old_pms = entity->client->old_pmove;
	checkpoint->grounded = entity->groundentity != NULL;
	checkpoint->watertype = entity->watertype;
	checkpoint->waterlevel = entity->waterlevel;
	checkpoint->old_frame_z = entity->client->oldvelocity[2];
	return 1;
}

static sg_compound_drop_live_host_result_t CompoundDropGameSource(
	void *context, const sg_compound_drop_live_snapshot_t *snapshot,
	sg_compound_publication_angle_bias_t *bias)
{
	sg_bot_t *bot = (sg_bot_t *)context;
	compound_drop_game_current_t current;
	sg_compound_publication_checkpoint_t live;

	if (!CompoundDropGameCurrent(bot, snapshot, &current) ||
	    !CompoundDropGameCheckpoint(bot->ent, &live) ||
	    !SG_CompoundPublicationCaptureAngleBias(&snapshot->binding.source,
	        &live, bias))
		return SG_COMPOUND_DROP_LIVE_HOST_DENIED;
	return SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED;
}

static sg_compound_drop_live_host_result_t CompoundDropGameSuffix(
	void *context, const sg_compound_drop_live_snapshot_t *snapshot,
	const sg_compound_publication_angle_bias_t *bias)
{
	sg_bot_t *bot = (sg_bot_t *)context;
	compound_drop_game_current_t current;
	sg_compound_publication_checkpoint_t live;

	if (!CompoundDropGameCurrent(bot, snapshot, &current) ||
	    !CompoundDropGameCheckpoint(bot->ent, &live) ||
	    !SG_CompoundPublicationCheckpointMatches(&snapshot->binding.suffix,
	        &live, bias))
		return SG_COMPOUND_DROP_LIVE_HOST_DENIED;
	return SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED;
}

static sg_compound_drop_live_host_result_t CompoundDropGameAcquire(
	void *context, const sg_compound_drop_live_snapshot_t *snapshot)
{
	sg_bot_t *bot = (sg_bot_t *)context;
	compound_drop_game_current_t current;
	sg_mover_key_t key;

	if (!CompoundDropGameCurrent(bot, snapshot, &current) ||
	    snapshot->mover_key <= 0 || snapshot->mover_key > UINT16_MAX ||
	    snapshot->trigger_key <= 0)
		return SG_COMPOUND_DROP_LIVE_HOST_ERROR;
	key = (sg_mover_key_t)snapshot->mover_key;
	return SG_CompoundGuardAcquireCompoundPreopen(&bot->compound_guard,
	    &key, 1U, (int)snapshot->binding.link_index,
	    (uint32_t)snapshot->trigger_key) == SG_COMPOUND_GUARD_OK ?
	    SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED :
	    SG_COMPOUND_DROP_LIVE_HOST_DENIED;
}

static sg_compound_drop_live_host_result_t CompoundDropGameAuthorize(
	void *context, const sg_compound_drop_live_snapshot_t *snapshot)
{
	sg_bot_t *bot = (sg_bot_t *)context;
	compound_drop_game_current_t current;
	sg_mover_key_t key;

	if (!CompoundDropGameCurrent(bot, snapshot, &current) ||
	    snapshot->mover_key <= 0 || snapshot->mover_key > UINT16_MAX)
		return SG_COMPOUND_DROP_LIVE_HOST_ERROR;
	key = (sg_mover_key_t)snapshot->mover_key;
	return SG_CompoundGuardAuthorize(&bot->compound_guard,
	    SG_MOVER_LAW_COMPOUND_PREOPEN, &key, 1U,
	    (int)snapshot->binding.link_index,
	    (uint32_t)snapshot->trigger_key) == SG_COMPOUND_GUARD_OK ?
	    SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED :
	    SG_COMPOUND_DROP_LIVE_HOST_DENIED;
}

static sg_compound_drop_live_host_result_t CompoundDropGameActivate(
	void *context, const sg_compound_drop_live_snapshot_t *snapshot)
{
	if (CompoundDropGameAuthorize(context, snapshot) !=
	    SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED)
		return SG_COMPOUND_DROP_LIVE_HOST_DENIED;
	return SG_CompoundGuardAllSubjectsOutside(
	    &((sg_bot_t *)context)->compound_guard) == SG_COMPOUND_GUARD_OK ?
	    SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED :
	    SG_COMPOUND_DROP_LIVE_HOST_DENIED;
}

static sg_compound_drop_live_host_result_t CompoundDropGameAtTop(
	void *context, const sg_compound_drop_live_snapshot_t *snapshot)
{
	compound_drop_game_current_t current;

	return CompoundDropGameCurrent((sg_bot_t *)context, snapshot, &current) &&
	       SG_CompoundWorldAtTopFor(current.mechanism,
	           SG_COMPOUND_HOLD_LEASE_MS) ?
	       SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED :
	       SG_COMPOUND_DROP_LIVE_HOST_DENIED;
}

static sg_compound_drop_live_host_result_t CompoundDropGameHold(
	void *context, const sg_compound_drop_live_snapshot_t *snapshot,
	int lease_ms)
{
	sg_bot_t *bot = (sg_bot_t *)context;
	compound_drop_game_current_t current;

	if (lease_ms != SG_COMPOUND_HOLD_LEASE_MS ||
	    !CompoundDropGameCurrent(bot, snapshot, &current))
		return SG_COMPOUND_DROP_LIVE_HOST_ERROR;
	return SG_CompoundGuardMaintain(&bot->compound_guard) ==
	       SG_COMPOUND_GUARD_OK ? SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED :
	                              SG_COMPOUND_DROP_LIVE_HOST_DENIED;
}

static sg_compound_drop_live_host_result_t CompoundDropGameOutside(
	void *context, const sg_compound_drop_live_snapshot_t *snapshot,
	const sg_replay_pose_t *pose)
{
	compound_drop_game_current_t current;

	return pose && CompoundDropGameCurrent((sg_bot_t *)context, snapshot,
	           &current) &&
	       SG_CompoundWorldOutsideSweep(current.mechanism, pose->origin) ?
	       SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED :
	       SG_COMPOUND_DROP_LIVE_HOST_DENIED;
}

static sg_compound_drop_live_host_result_t CompoundDropGameSupport(
	void *context, const sg_compound_drop_live_snapshot_t *snapshot,
	const sg_replay_pose_t *pose)
{
	sg_bot_t *bot = (sg_bot_t *)context;
	compound_drop_game_current_t current;
	edict_t *entity = bot ? bot->ent : NULL;

	return pose && CompoundDropGameCurrent(bot, snapshot, &current) &&
	       entity && entity->groundentity &&
	       (entity->groundentity == g_edicts ||
	        SG_ImmutableSupport(entity->groundentity)) ?
	       SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED :
	       SG_COMPOUND_DROP_LIVE_HOST_DENIED;
}

static sg_compound_drop_live_host_result_t CompoundDropGameSegment(
	void *context, const sg_compound_drop_live_snapshot_t *snapshot,
	const vec3_t from, const vec3_t to)
{
	compound_drop_game_current_t current;

	if (!from || !to || !CompoundDropGameCurrent((sg_bot_t *)context,
	        snapshot, &current))
		return SG_COMPOUND_DROP_LIVE_HOST_ERROR;
	return SG_CompoundWorldCrossesSweep(current.mechanism, from, to) ?
	       SG_COMPOUND_DROP_LIVE_HOST_DENIED :
	       SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED;
}

static sg_compound_drop_live_host_result_t CompoundDropGameProve(
	void *context, const sg_compound_drop_live_snapshot_t *snapshot,
	const sg_replay_pose_t *pose, qboolean recovery,
	sg_compound_drop_live_proof_t *proof_out)
{
	compound_drop_game_current_t current;
	sg_compound_drop_proof_t proof;
	qboolean destination_water;

	if (proof_out)
		memset(proof_out, 0, sizeof(*proof_out));
	if (!pose || !proof_out || recovery ||
	    !CompoundDropGameCurrent((sg_bot_t *)context, snapshot, &current))
		return SG_COMPOUND_DROP_LIVE_HOST_DENIED;
	destination_water =
	    (snapshot->binding.destination_seed.flags & RSF_WATER) != 0;
	memset(&proof, 0, sizeof(proof));
	if (SG_OracleCompoundDropPreopen(snapshot->binding.source_seed.origin,
	        current.mechanism, snapshot->binding.link.mechanism_anchor,
	        snapshot->binding.destination_seed.origin,
	        snapshot->binding.link.anchor, snapshot->binding.link.heading,
	        destination_water, &proof, true) != RLR_OK)
		return SG_COMPOUND_DROP_LIVE_HOST_DENIED;
	proof_out->arrival_ms = proof.arrival_ms;
	proof_out->sweep_clear_ms = proof.sweep_clear_ms;
	proof_out->exit_speed = proof.exit_speed;
	if (sg_cv.debug && sg_cv.debug->value > 0.0f && sg_host.dprint)
		sg_host.dprint("slipgate: ddrop suffix-proved bot=%d link=%u "
		               "arrival_ms=%d clear_ms=%d origin=(%.3f %.3f %.3f)\n",
		               (int)((sg_bot_t *)context - sg_bots),
		               snapshot->binding.link_index, proof.arrival_ms,
		               proof.sweep_clear_ms, pose->origin[0], pose->origin[1],
		               pose->origin[2]);
	return SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED;
}

static sg_compound_drop_live_host_result_t CompoundDropGameRelease(
	void *context, const sg_compound_drop_live_snapshot_t *snapshot)
{
	sg_bot_t *bot = (sg_bot_t *)context;
	compound_drop_game_current_t current;

	if (!CompoundDropGameCurrent(bot, snapshot, &current))
		return SG_COMPOUND_DROP_LIVE_HOST_ERROR;
	if (SG_CompoundGuardReleaseProvedClear(&bot->compound_guard) !=
	    SG_COMPOUND_GUARD_OK)
		return SG_COMPOUND_DROP_LIVE_HOST_DENIED;
	if (sg_cv.debug && sg_cv.debug->value > 0.0f && sg_host.dprint)
		sg_host.dprint("slipgate: ddrop released bot=%d link=%u frame=%d\n",
		               (int)(bot - sg_bots), snapshot->binding.link_index,
		               level.framenum);
	return SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED;
}

static sg_compound_drop_live_host_result_t CompoundDropGameOrphanHost(
	void *context, const sg_compound_drop_live_snapshot_t *snapshot)
{
	sg_bot_t *bot = (sg_bot_t *)context;
	compound_drop_game_current_t current;

	if (!CompoundDropGameCurrent(bot, snapshot, &current))
		return SG_COMPOUND_DROP_LIVE_HOST_ERROR;
	return SG_CompoundGuardOrphan(&bot->compound_guard, 0) ==
	       SG_COMPOUND_GUARD_OK ? SG_COMPOUND_DROP_LIVE_HOST_ACCEPTED :
	                              SG_COMPOUND_DROP_LIVE_HOST_DENIED;
}

static qboolean CompoundDropGameShadow(
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

qboolean SG_CompoundDropGameHost(sg_bot_t *bot,
	sg_compound_drop_live_host_t *host)
{
	if (!bot || !host)
		return false;
	memset(host, 0, sizeof(*host));
	host->context = bot;
	host->bind = CompoundDropGameBind;
	host->source_checkpoint = CompoundDropGameSource;
	host->suffix_checkpoint = CompoundDropGameSuffix;
	host->acquire = CompoundDropGameAcquire;
	host->authorize = CompoundDropGameAuthorize;
	host->activate = CompoundDropGameActivate;
	host->at_top = CompoundDropGameAtTop;
	host->hold_open = CompoundDropGameHold;
	host->outside_sweep = CompoundDropGameOutside;
	host->ground_support = CompoundDropGameSupport;
	host->sweep_segment_clear = CompoundDropGameSegment;
	host->prove_suffix = CompoundDropGameProve;
	host->release = CompoundDropGameRelease;
	host->orphan = CompoundDropGameOrphanHost;
	host->drop_shadow = CompoundDropGameShadow;
	return true;
}

qboolean SG_CompoundDropGamePose(const edict_t *entity,
	sg_replay_pose_t *pose)
{
	if (!entity || !entity->inuse || !entity->client || !pose)
		return false;
	SG_DropLivePose(pose, &entity->client->ps.pmove, entity->s.origin,
	    entity->velocity, entity->groundentity != NULL, entity->watertype,
	    entity->waterlevel);
	return true;
}

void SG_CompoundDropGameDebugResult(sg_bot_t *bot, const char *stage,
	const sg_compound_drop_live_result_t *result,
	const sg_replay_pose_t *pose)
{
	if (!bot || !stage || !result || !pose || !sg_cv.debug ||
	    sg_cv.debug->value <= 0.0f || !sg_host.dprint)
		return;
	sg_host.dprint("slipgate: ddrop stage=%s bot=%d link=%d phase=%d "
	               "replay=%d outcome=%d failure=%s replay_reason=%d "
	               "elapsed=%d origin=(%.3f %.3f %.3f) velocity=(%.3f %.3f %.3f)\n",
	               stage, (int)(bot - sg_bots),
	               bot->compound_drop_live.drop_link,
	               bot->compound_drop_live.outer.phase,
	               bot->compound_drop_live.replay_kind, result->outcome,
	               SG_CompoundDropLiveFailureName(result->failure),
	               result->replay_reason,
	               bot->compound_drop_live.transaction_elapsed_ms,
	               pose->origin[0], pose->origin[1], pose->origin[2],
	               pose->velocity[0], pose->velocity[1], pose->velocity[2]);
}

int SG_CompoundDropGameStageAuthenticatedProbe(int link_index)
{
	rune_t *rune = SG_Rune();
	const sg_compound_publication_binding_t *binding;
	const sg_compound_world_preopen_t *mechanism;
	sg_bot_t *bot = NULL;
	edict_t *entity;
	sg_compound_drop_live_host_t host;
	sg_compound_drop_live_result_t result;
	sg_replay_pose_t pose;
	vec3_t staged_origin;
	int axis, slot;

	if (!sg_cv.debug || sg_cv.debug->value <= 0.0f || !rune ||
	    !SG_RunePhysicsCompatible(rune) ||
	    link_index < 0 || link_index >= rune->hdr.num_links ||
	    !(binding = SG_CompoundPublicationBinding(rune,
	        (uint32_t)link_index)) || binding->link.action != RL_DOOR_DROP ||
	    !(mechanism = SG_CompoundPublicationMechanism(rune, binding)) ||
	    mechanism->trigger_key <= 0 || mechanism->mover_key <= 0)
		return false;
	for (slot = 0; slot < SG_MAXBOTS; slot++)
	{
		sg_compound_guard_result_t guard_result;

		if (!sg_bots[slot].active || !sg_bots[slot].ent ||
		    !sg_bots[slot].ent->inuse || !sg_bots[slot].ent->client ||
		    sg_bots[slot].ent->deadflag != DEAD_NO ||
		    sg_bots[slot].ent->health <= 0 ||
		    sg_bots[slot].compound_drop_live.guard_owned)
			continue;
		guard_result = SG_CompoundGuardValidate(
		    &sg_bots[slot].compound_guard, NULL);
		if (guard_result == SG_COMPOUND_GUARD_NO_LEASE ||
		    guard_result == SG_COMPOUND_GUARD_NOT_ATTACHED)
		{
			bot = &sg_bots[slot];
			break;
		}
	}
	if (!bot || !gi.unlinkentity || !sg_host.linkentity)
		return false;
	entity = bot->ent;
	gi.unlinkentity(entity);
	entity->client->ps.pmove = binding->source.pms;
	entity->client->old_pmove = binding->source.old_pms;
	for (axis = 0; axis < 3; axis++)
	{
		entity->s.origin[axis] = binding->source_seed.origin[axis];
		entity->s.old_origin[axis] = binding->source_seed.origin[axis];
		entity->velocity[axis] = binding->source.pms.velocity[axis] * 0.125f;
	}
	VectorClear(entity->client->oldvelocity);
	entity->client->oldvelocity[2] = binding->source.old_frame_z;
	entity->groundentity = binding->source.grounded ? g_edicts : NULL;
	entity->groundentity_linkcount = entity->groundentity
	    ? entity->groundentity->linkcount : 0;
	entity->waterlevel = binding->source.waterlevel;
	entity->watertype = binding->source.watertype;
	memset(&bot->compound_drop_live, 0, sizeof(bot->compound_drop_live));
	bot->compound_drop_live.drop_link = -1;
	bot->seed = binding->link.from;
	VectorCopy(entity->s.origin, bot->last_origin);
	bot->commit_link = link_index;
	bot->sticky_link = link_index;
	bot->commit_until = level.time + 5.0f;
	bot->latch_until = level.time + 5.0f;
	VectorCopy(entity->s.origin, staged_origin);
	bot->stuck_time = 0.0f;
	VectorCopy(staged_origin, bot->stuck_origin);
	VectorCopy(staged_origin, bot->watch_org);
	VectorCopy(staged_origin, bot->stag_org);
	VectorCopy(staged_origin, bot->wedge_org);
	SG_Mark(&bot->watch_since);
	SG_Mark(&bot->stag_since);
	SG_Mark(&bot->wedge_since);
	bot->seedless_active = false;
	bot->seedless_since = 0.0f;
	bot->seedless_turn_until = 0.0f;
	sg_host.linkentity(entity);
	if (!SG_CompoundDropGameHost(bot, &host) ||
	    !SG_CompoundDropGamePose(entity, &pose))
		return false;
	result = SG_CompoundDropLiveBegin(&bot->compound_drop_live, &host,
	    (uint32_t)link_index, &pose);
	if (result.outcome != SG_COMPOUND_DROP_LIVE_RUNNING ||
	    !bot->compound_drop_live.guard_owned)
	{
		bot->commit_link = -1;
		bot->sticky_link = -1;
		return false;
	}
	if (sg_host.dprint)
		sg_host.dprint("slipgate: ddrop probe-staged bot=%d link=%d "
		               "source=(%.3f %.3f %.3f) destination=(%.3f %.3f %.3f)\n",
		               slot, link_index, staged_origin[0], staged_origin[1],
		               staged_origin[2],
		               binding->destination_seed.origin[0],
		               binding->destination_seed.origin[1],
		               binding->destination_seed.origin[2]);
	return true;
}

static qboolean CompoundDropGameContact(const sg_bot_t *bot,
	const edict_t *entity, qboolean recovery)
{
	const sg_compound_drop_live_state_t *state;
	const float *destination;
	vec3_t delta, from, to;
	trace_t trace;

	if (!bot || !entity || !(state = &bot->compound_drop_live)->guard_owned)
		return false;
	destination = state->snapshot.binding.destination_seed.origin;
	VectorSubtract(destination, entity->s.origin, delta);
	if (recovery)
	{
		if (!entity->groundentity || entity->waterlevel != 0 ||
		    (entity->groundentity != g_edicts &&
		     !SG_ImmutableSupport(entity->groundentity)) ||
		    delta[0] * delta[0] + delta[1] * delta[1] >=
		        RUNE_DROP_RECOVERY_RADIUS * RUNE_DROP_RECOVERY_RADIUS ||
		    delta[2] <= -RUNE_DROP_RECOVERY_Z ||
		    delta[2] >= RUNE_DROP_RECOVERY_Z)
			return false;
	}
	else if (delta[0] * delta[0] + delta[1] * delta[1] >= 40.0f * 40.0f ||
	         delta[2] <= -72.0f || delta[2] >= 72.0f)
		return false;
	VectorCopy(entity->s.origin, from);
	VectorCopy(destination, to);
	from[2] += 16.0f;
	to[2] += 16.0f;
	trace = sg_host.trace(from, NULL, NULL, to, (edict_t *)entity,
	                      MASK_PLAYERSOLID);
	return !trace.startsolid && !trace.allsolid && trace.fraction >= 1.0f;
}

qboolean SG_CompoundDropGameObservation(sg_bot_t *bot,
	const edict_t *entity, sg_replay_observation_t *observation)
{
	qboolean support;

	if (!bot || !entity || !observation)
		return false;
	memset(observation, 0, sizeof(*observation));
	support = entity->groundentity &&
	    (entity->groundentity == g_edicts ||
	     SG_ImmutableSupport(entity->groundentity));
	observation->ground_support_valid = support;
	observation->drop_arrival_contact_clear =
	    CompoundDropGameContact(bot, entity, false);
	observation->drop_recovery_contact_clear =
	    CompoundDropGameContact(bot, entity, true);
	observation->contact_clear = observation->drop_arrival_contact_clear;
	observation->contaminated = bot->compound_drop_live.drop_events.contaminated;
	observation->door_passed = bot->compound_drop_live.drop_events.door_passed;
	memset(&bot->compound_drop_live.drop_events, 0,
	       sizeof(bot->compound_drop_live.drop_events));
	return true;
}

int SG_CompoundDropGameAuthorizeTouch(sg_bot_t *bot, edict_t *source,
	edict_t *activator, int frame_serial)
{
	sg_compound_drop_live_host_t host;
	sg_replay_pose_t pose;
	sg_compound_drop_live_result_t result;

	if (!bot || !bot->compound_drop_live.guard_owned)
		return -1;
	if (!source || !activator || activator != bot->ent ||
	    source->s.number != bot->compound_drop_live.snapshot.trigger_key ||
	    !SG_CompoundDropGameHost(bot, &host) ||
	    !SG_CompoundDropGamePose(activator, &pose))
		return 0;
	result = SG_CompoundDropLiveAuthorizeTouch(&bot->compound_drop_live,
	    &host, source->s.number, &pose, frame_serial);
	if (sg_cv.debug && sg_cv.debug->value > 0.0f && sg_host.dprint)
		sg_host.dprint("slipgate: ddrop touch bot=%d link=%u frame=%d "
		               "phase=%d outcome=%d\n", (int)(bot - sg_bots),
		               bot->compound_drop_live.snapshot.binding.link_index,
		               frame_serial, bot->compound_drop_live.outer.phase,
		               result.outcome);
	return result.outcome == SG_COMPOUND_DROP_LIVE_RUNNING;
}

int SG_CompoundDropGameAuthorizeActivation(sg_bot_t *bot, edict_t *source,
	edict_t *door_master, edict_t *activator, int frame_serial)
{
	compound_drop_game_current_t current;
	sg_compound_drop_live_host_t host;
	sg_compound_drop_live_result_t result;

	if (!bot || !bot->compound_drop_live.guard_owned)
		return -1;
	if (!source || !door_master || activator != bot->ent ||
	    source->s.number != bot->compound_drop_live.snapshot.trigger_key ||
	    door_master->s.number != bot->compound_drop_live.snapshot.mover_key ||
	    frame_serial != bot->compound_drop_live.touch_frame_serial ||
	    !CompoundDropGameCurrent(bot, &bot->compound_drop_live.snapshot,
	        &current) || current.mechanism->trigger != source ||
	    current.member != door_master || !SG_CompoundDropGameHost(bot, &host))
		return 0;
	result = SG_CompoundDropLiveAuthorizeActivation(
	    &bot->compound_drop_live, &host, source->s.number,
	    door_master->s.number, frame_serial);
	if (sg_cv.debug && sg_cv.debug->value > 0.0f && sg_host.dprint)
		sg_host.dprint("slipgate: ddrop activated bot=%d link=%u frame=%d "
		               "phase=%d outcome=%d\n", (int)(bot - sg_bots),
		               bot->compound_drop_live.snapshot.binding.link_index,
		               frame_serial, bot->compound_drop_live.outer.phase,
		               result.outcome);
	return result.outcome == SG_COMPOUND_DROP_LIVE_RUNNING;
}

int SG_CompoundDropGameOwnsTargetDispatch(const sg_bot_t *bot,
	const edict_t *source)
{
	return bot && source && bot->compound_drop_live.guard_owned &&
	       source->s.number == bot->compound_drop_live.snapshot.trigger_key;
}

void SG_CompoundDropGameOrphan(sg_bot_t *bot)
{
	sg_compound_drop_live_host_t host;

	if (!bot || !bot->compound_drop_live.guard_owned ||
	    !SG_CompoundDropGameHost(bot, &host))
		return;
	(void)SG_CompoundDropLiveOrphan(&bot->compound_drop_live, &host);
}
