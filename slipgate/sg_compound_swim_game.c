#include "g_local.h"

#include <limits.h>
#include <stddef.h>
#include <string.h>

#include "slipgate/sg_local.h"
#include "slipgate/sg_bot.h"
#include "slipgate/sg_compound_guard.h"
#include "slipgate/sg_compound_publication.h"
#include "slipgate/sg_compound_swim_game.h"
#include "slipgate/sg_compound_world.h"
#include "slipgate/sg_cvars.h"
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_util.h"

void ClientThink(edict_t *ent, usercmd_t *ucmd);

typedef struct sg_compound_swim_game_context_s
{
	sg_bot_t *bot;
} sg_compound_swim_game_context_t;

static void GameEvent(sg_compound_swim_game_context_t *context,
	uint32_t link_index, sg_compound_swim_live_event_t event,
	sg_compound_swim_live_failure_t failure,
	sg_replay_reason_t replay_reason)
{
	ptrdiff_t slot;

	if (!context || !context->bot || !sg_host.dprint)
		return;
	slot = context->bot - sg_bots;
	if (slot < 0 || slot >= SG_MAXBOTS)
		return;
	sg_host.dprint("slipgate: dswim event=%s bot=%d instance=%llu "
	               "link=%u frame=%d reason=%s replay=%d\n",
	               SG_CompoundSwimLiveEventName(event), (int)slot,
	               context->bot->instance_token, (unsigned int)link_index,
	               level.framenum, SG_CompoundSwimLiveFailureName(failure),
	               (int)replay_reason);
}

static void GameTransition(void *opaque,
	const sg_compound_swim_live_state_t *state,
	sg_compound_swim_live_event_t event,
	sg_compound_swim_live_failure_t failure,
	sg_replay_reason_t replay_reason)
{
	sg_compound_swim_game_context_t *context = opaque;

	if (!state)
		return;
	GameEvent(context, state->snapshot.binding.link_index, event, failure,
	          replay_reason);
}

static void GameFailure(sg_compound_swim_game_context_t *context,
	uint32_t link_index, qboolean was_recovering,
	const sg_compound_swim_live_result_t *result)
{
	if (!result || was_recovering ||
	    result->failure == SG_COMPOUND_SWIM_LIVE_FAILURE_NONE)
		return;
	GameEvent(context, link_index, SG_COMPOUND_SWIM_LIVE_EVENT_FAILURE,
	          result->failure, result->replay_reason);
}

static sg_compound_swim_live_host_result_t GameHostResult(
	sg_compound_guard_result_t result)
{
	if (result == SG_COMPOUND_GUARD_OK)
		return SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED;
	if (result == SG_COMPOUND_GUARD_CONFLICT ||
	    result == SG_COMPOUND_GUARD_OWNER_BUSY ||
	    result == SG_COMPOUND_GUARD_FULL ||
	    result == SG_COMPOUND_GUARD_NOT_CLEAR)
		return SG_COMPOUND_SWIM_LIVE_HOST_DENIED;
	return SG_COMPOUND_SWIM_LIVE_HOST_ERROR;
}

static int GameEdictKey(const edict_t *entity)
{
	ptrdiff_t key;

	if (!entity || !g_edicts)
		return 0;
	key = entity - g_edicts;
	return key > 0 && key < globals.num_edicts ? (int)key : 0;
}

static qboolean GamePose(const sg_bot_t *bot, sg_replay_pose_t *pose)
{
	edict_t *entity;

	if (pose)
		memset(pose, 0, sizeof(*pose));
	if (!bot || !pose || !(entity = bot->ent) || !entity->inuse ||
	    !entity->client)
		return false;
	pose->pms = entity->client->ps.pmove;
	VectorCopy(entity->s.origin, pose->origin);
	VectorCopy(entity->velocity, pose->velocity);
	pose->grounded = entity->groundentity ? true : false;
	pose->watertype = entity->watertype;
	pose->waterlevel = entity->waterlevel;
	return true;
}

static qboolean GameStart(const sg_bot_t *bot,
	sg_compound_swim_live_start_t *start)
{
	if (start)
		memset(start, 0, sizeof(*start));
	if (!bot || !bot->ent || !bot->ent->client || !start ||
	    !GamePose(bot, &start->pose))
		return false;
	start->old_pms = bot->ent->client->old_pmove;
	start->old_frame_z = bot->ent->client->oldvelocity[2];
	return isfinite(start->old_frame_z);
}

static qboolean GamePhantom(const sg_bot_t *bot, sg_phantom_t *phantom,
	float *old_frame_z)
{
	sg_compound_swim_live_start_t start;

	if (phantom)
		memset(phantom, 0, sizeof(*phantom));
	if (!phantom || !old_frame_z || !GameStart(bot, &start))
		return false;
	phantom->pms = start.pose.pms;
	phantom->old_pms = start.old_pms;
	VectorCopy(start.pose.origin, phantom->origin);
	VectorCopy(start.pose.velocity, phantom->velocity);
	phantom->groundentity = start.pose.grounded;
	phantom->groundentity_entity = bot->ent->groundentity;
	phantom->watertype = start.pose.watertype;
	phantom->waterlevel = start.pose.waterlevel;
	VectorCopy(bot->ent->mins, phantom->mins);
	VectorCopy(bot->ent->maxs, phantom->maxs);
	*old_frame_z = start.old_frame_z;
	return true;
}

static qboolean GameResolveSnapshot(sg_compound_swim_game_context_t *context,
	const sg_compound_swim_live_snapshot_t *snapshot,
	const sg_compound_publication_binding_t **binding_out,
	const sg_compound_world_preopen_t **resolved_out)
{
	rune_t *rune = SG_Rune();
	const sg_compound_publication_binding_t *binding;
	const sg_compound_world_preopen_t *resolved;

	if (binding_out)
		*binding_out = NULL;
	if (resolved_out)
		*resolved_out = NULL;
	if (!context || !context->bot || !snapshot || !rune ||
	    !(binding = SG_CompoundPublicationBinding(rune,
	        snapshot->binding.link_index)) ||
	    memcmp(binding, &snapshot->binding, sizeof(*binding)) != 0 ||
	    !(resolved = SG_CompoundPublicationMechanism(rune, binding)) ||
	    resolved->trigger_key != snapshot->trigger_key ||
	    resolved->mover_key != snapshot->mover_key)
		return false;
	if (binding_out)
		*binding_out = binding;
	if (resolved_out)
		*resolved_out = resolved;
	return true;
}

static sg_compound_swim_live_host_result_t GameBind(void *opaque,
	uint32_t link_index, sg_compound_swim_live_snapshot_t *snapshot)
{
	sg_compound_swim_game_context_t *context = opaque;
	rune_t *rune = SG_Rune();
	const sg_compound_publication_binding_t *binding;
	const sg_compound_world_preopen_t *resolved;

	if (!context || !context->bot || !snapshot || !rune ||
	    !(binding = SG_CompoundPublicationBinding(rune, link_index)) ||
	    !(resolved = SG_CompoundPublicationMechanism(rune, binding)))
		return SG_COMPOUND_SWIM_LIVE_HOST_DENIED;
	memset(snapshot, 0, sizeof(*snapshot));
	snapshot->binding = *binding;
	snapshot->trigger_key = resolved->trigger_key;
	snapshot->mover_key = resolved->mover_key;
	return SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED;
}

static sg_compound_swim_live_host_result_t GameAuthorize(void *opaque,
	const sg_compound_swim_live_snapshot_t *snapshot)
{
	sg_compound_swim_game_context_t *context = opaque;
	sg_mover_key_t key;

	if (!GameResolveSnapshot(context, snapshot, NULL, NULL) ||
	    snapshot->mover_key <= 0 || snapshot->mover_key > UINT16_MAX)
		return SG_COMPOUND_SWIM_LIVE_HOST_ERROR;
	key = (sg_mover_key_t)snapshot->mover_key;
	return GameHostResult(SG_CompoundGuardAuthorize(
	    &context->bot->compound_guard, SG_MOVER_LAW_COMPOUND_PREOPEN,
	    &key, 1U, (int)snapshot->binding.link_index,
	    snapshot->binding.mechanism_index));
}

static sg_compound_swim_live_host_result_t GameAcquire(void *opaque,
	const sg_compound_swim_live_snapshot_t *snapshot)
{
	sg_compound_swim_game_context_t *context = opaque;
	sg_mover_key_t key;

	if (!GameResolveSnapshot(context, snapshot, NULL, NULL) ||
	    snapshot->mover_key <= 0 || snapshot->mover_key > UINT16_MAX)
		return SG_COMPOUND_SWIM_LIVE_HOST_ERROR;
	key = (sg_mover_key_t)snapshot->mover_key;
	return GameHostResult(SG_CompoundGuardAcquireCompoundPreopen(
	    &context->bot->compound_guard, &key, 1U,
	    (int)snapshot->binding.link_index,
	    snapshot->binding.mechanism_index));
}

static sg_compound_swim_live_host_result_t GamePrepare(void *opaque,
	const sg_compound_swim_live_snapshot_t *snapshot,
	const sg_compound_swim_live_start_t *start,
	sg_compound_swim_live_plan_t *plan)
{
	sg_compound_swim_game_context_t *context = opaque;
	const sg_compound_world_preopen_t *resolved;
	sg_compound_swim_proof_t proof;
	sg_phantom_t phantom;
	sg_replay_reason_t replay_reason;
	rune_reject_reason_t reason;
	qboolean destination_water;

	if (!start || !plan ||
	    GameAuthorize(opaque, snapshot) !=
	        SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED ||
	    !GameResolveSnapshot(context, snapshot, NULL, &resolved))
		return SG_COMPOUND_SWIM_LIVE_HOST_ERROR;
	memset(&phantom, 0, sizeof(phantom));
	phantom.pms = start->pose.pms;
	phantom.old_pms = start->old_pms;
	VectorCopy(start->pose.origin, phantom.origin);
	VectorCopy(start->pose.velocity, phantom.velocity);
	phantom.groundentity = start->pose.grounded;
	phantom.groundentity_entity = context->bot->ent->groundentity;
	phantom.watertype = start->pose.watertype;
	phantom.waterlevel = start->pose.waterlevel;
	VectorCopy(context->bot->ent->mins, phantom.mins);
	VectorCopy(context->bot->ent->maxs, phantom.maxs);
	destination_water =
	    (snapshot->binding.destination_seed.flags & RSF_WATER) != 0;
	memset(plan, 0, sizeof(*plan));
	reason = SG_OracleCompoundSwimPreopen(&phantom, resolved,
	    snapshot->binding.link.mechanism_anchor,
	    snapshot->binding.destination_seed.origin, destination_water,
	    start->old_frame_z, &proof, &replay_reason, context->bot->ent,
	    true, false);
	if (reason != RLR_OK)
		return SG_COMPOUND_SWIM_LIVE_HOST_DENIED;
	VectorCopy(snapshot->binding.link.mechanism_anchor,
	    plan->mechanism_anchor);
	plan->suffix.pms = proof.suffix_pms;
	plan->suffix.old_pms = proof.suffix_old_pms;
	plan->suffix.grounded = proof.suffix_groundentity;
	plan->suffix.watertype = proof.suffix_watertype;
	plan->suffix.waterlevel = proof.suffix_waterlevel;
	plan->suffix.old_frame_z = proof.suffix_old_frame_z;
	plan->touch_ms = proof.touch_ms;
	plan->touch_frame_end_ms = proof.touch_frame_end_ms;
	plan->mover_top_ms = proof.mover_top_ms;
	plan->suffix_start_ms = proof.suffix_start_ms;
	plan->arrival_ms = proof.arrival_ms;
	plan->sweep_clear_ms = proof.sweep_clear_ms;
	plan->total_cost_ms = proof.total_cost_ms;
	plan->exit_speed = proof.exit_speed;
	return SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED;
}

static sg_compound_swim_live_host_result_t GameAtTop(void *opaque,
	const sg_compound_swim_live_snapshot_t *snapshot)
{
	sg_compound_swim_game_context_t *context = opaque;
	const sg_compound_world_preopen_t *resolved;

	return GameResolveSnapshot(context, snapshot, NULL, &resolved) &&
	       SG_CompoundWorldAtTopFor(resolved, SG_COMPOUND_HOLD_LEASE_MS)
	    ? SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED
	    : SG_COMPOUND_SWIM_LIVE_HOST_DENIED;
}

static sg_compound_swim_live_host_result_t GameHold(void *opaque,
	const sg_compound_swim_live_snapshot_t *snapshot, int lease_ms)
{
	sg_compound_swim_game_context_t *context = opaque;

	if (lease_ms != SG_COMPOUND_HOLD_LEASE_MS ||
	    !GameResolveSnapshot(context, snapshot, NULL, NULL))
		return SG_COMPOUND_SWIM_LIVE_HOST_ERROR;
	return GameHostResult(SG_CompoundGuardMaintain(
	    &context->bot->compound_guard));
}

static sg_compound_swim_live_host_result_t GameOutside(void *opaque,
	const sg_compound_swim_live_snapshot_t *snapshot,
	const sg_replay_pose_t *pose)
{
	sg_compound_swim_game_context_t *context = opaque;
	const sg_compound_world_preopen_t *resolved;

	return pose && GameResolveSnapshot(context, snapshot, NULL, &resolved) &&
	       SG_CompoundWorldOutsideSweep(resolved, pose->origin)
	    ? SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED
	    : SG_COMPOUND_SWIM_LIVE_HOST_DENIED;
}

static sg_compound_swim_live_host_result_t GameSegment(void *opaque,
	const sg_compound_swim_live_snapshot_t *snapshot,
	const vec3_t from, const vec3_t to)
{
	sg_compound_swim_game_context_t *context = opaque;
	const sg_compound_world_preopen_t *resolved;

	return from && to && GameResolveSnapshot(context, snapshot, NULL,
	                                        &resolved) &&
	       !SG_CompoundWorldCrossesSweep(resolved, from, to)
	    ? SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED
	    : SG_COMPOUND_SWIM_LIVE_HOST_DENIED;
}

static sg_compound_swim_live_host_result_t GameSuffixCheckpoint(void *opaque,
	const sg_compound_swim_live_snapshot_t *snapshot,
	const sg_compound_swim_live_plan_t *plan)
{
	sg_compound_swim_game_context_t *context = opaque;
	sg_compound_publication_checkpoint_t live;
	sg_compound_publication_angle_bias_t bias;
	sg_compound_swim_live_start_t start;

	if (!plan || !GameResolveSnapshot(context, snapshot, NULL, NULL) ||
	    !GameStart(context->bot, &start))
		return SG_COMPOUND_SWIM_LIVE_HOST_ERROR;
	memset(&live, 0, sizeof(live));
	live.pms = start.pose.pms;
	live.old_pms = start.old_pms;
	live.grounded = start.pose.grounded;
	live.watertype = start.pose.watertype;
	live.waterlevel = start.pose.waterlevel;
	live.old_frame_z = start.old_frame_z;
	if (!SG_CompoundPublicationCaptureAngleBias(&plan->suffix, &live,
	                                           &bias) ||
	    bias.axis[0] || bias.axis[1] || bias.axis[2])
		return SG_COMPOUND_SWIM_LIVE_HOST_DENIED;
	return SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED;
}

static sg_compound_swim_live_host_result_t GameProof(void *opaque,
	const sg_compound_swim_live_snapshot_t *snapshot,
	const sg_replay_pose_t *pose, qboolean recovery,
	sg_compound_swim_live_proof_t *proof)
{
	sg_compound_swim_game_context_t *context = opaque;
	const sg_compound_world_preopen_t *resolved;
	sg_compound_swim_recovery_proof_t oracle_proof;
	sg_phantom_t phantom;
	float old_frame_z;
	rune_reject_reason_t reason;
	qboolean destination_water;

	if (!pose || !proof ||
	    !GameResolveSnapshot(context, snapshot, NULL, &resolved) ||
	    !GamePhantom(context->bot, &phantom, &old_frame_z))
		return SG_COMPOUND_SWIM_LIVE_HOST_ERROR;
	destination_water =
	    (snapshot->binding.destination_seed.flags & RSF_WATER) != 0;
	reason = recovery
	    ? SG_OracleCompoundSwimRecover(&phantom, resolved,
	        snapshot->binding.destination_seed.origin, destination_water,
	        old_frame_z, &oracle_proof, context->bot->ent)
	    : SG_OracleCompoundSwimContinue(&phantom, resolved,
	        snapshot->binding.destination_seed.origin, destination_water,
	        old_frame_z, &oracle_proof, context->bot->ent);
	if (reason != RLR_OK)
		return SG_COMPOUND_SWIM_LIVE_HOST_DENIED;
	proof->arrival_ms = oracle_proof.arrival_ms;
	proof->sweep_clear_ms = oracle_proof.sweep_clear_ms;
	proof->exit_speed = oracle_proof.exit_speed;
	return SG_COMPOUND_SWIM_LIVE_HOST_ACCEPTED;
}

static sg_compound_swim_live_host_result_t GameRelease(void *opaque,
	const sg_compound_swim_live_snapshot_t *snapshot)
{
	sg_compound_swim_game_context_t *context = opaque;

	if (!GameResolveSnapshot(context, snapshot, NULL, NULL))
		return SG_COMPOUND_SWIM_LIVE_HOST_ERROR;
	return GameHostResult(SG_CompoundGuardReleaseProvedClear(
	    &context->bot->compound_guard));
}

static sg_compound_swim_live_host_t GameHost(
	sg_compound_swim_game_context_t *context)
{
	sg_compound_swim_live_host_t host;

	memset(&host, 0, sizeof(host));
	host.context = context;
	host.bind = GameBind;
	host.prepare = GamePrepare;
	host.suffix_checkpoint = GameSuffixCheckpoint;
	host.acquire = GameAcquire;
	host.authorize = GameAuthorize;
	host.at_top = GameAtTop;
	host.hold_open = GameHold;
	host.outside_sweep = GameOutside;
	host.sweep_segment_clear = GameSegment;
	host.prove_suffix = GameProof;
	host.release = GameRelease;
	host.transition = GameTransition;
	return host;
}

static sg_replay_observation_t GameObservation(const sg_bot_t *bot,
	const sg_compound_swim_live_state_t *state,
	const sg_replay_pose_t *pose)
{
	sg_replay_observation_t observation;
	qboolean destination_water;

	memset(&observation, 0, sizeof(observation));
	if (!bot || !state || !pose)
		return observation;
	destination_water =
	    (state->snapshot.binding.destination_seed.flags & RSF_WATER) != 0;
	observation.contact_clear = SG_SwimArrived(pose->origin,
	    state->snapshot.binding.destination_seed.origin, destination_water,
	    pose->grounded, pose->watertype, pose->waterlevel, bot->ent);
	return observation;
}

int SG_CompoundSwimGameOwns(const sg_bot_t *bot)
{
	return bot && bot->compound_swim.guard_owned;
}

int SG_CompoundSwimGameEmit(sg_bot_t *bot, int link_index)
{
	sg_compound_swim_game_context_t context;
	sg_compound_swim_live_host_t host;
	sg_compound_swim_live_result_t result;
	sg_compound_swim_live_start_t start;
	sg_replay_pose_t pose;
	sg_replay_observation_t observation;
	rune_t *rune = SG_Rune();
	int step;
	qboolean was_recovering;

	if (bot && bot->compound_swim.guard_owned)
		link_index = (int)bot->compound_swim.snapshot.binding.link_index;
	if (!bot || !bot->ent || !bot->ent->client || !rune ||
	    link_index < 0 || link_index >= rune->hdr.num_links)
		return false;
	context.bot = bot;
	host = GameHost(&context);
	if (!bot->compound_swim.guard_owned)
	{
		if (rune->links[link_index].action != RL_DOOR_SWIM)
			return false;
		if (!GameStart(bot, &start))
			return true;
		result = SG_CompoundSwimLiveBegin(&bot->compound_swim, &host,
		    (uint32_t)link_index, &start);
		GameFailure(&context, (uint32_t)link_index, false, &result);
		if (result.outcome != SG_COMPOUND_SWIM_LIVE_RUNNING)
			return true;
	}
	if (!GamePose(bot, &pose))
		return true;
	observation = GameObservation(bot, &bot->compound_swim, &pose);
	if (bot->compound_swim.command_pending)
	{
		was_recovering = bot->compound_swim.recovering;
		result = SG_CompoundSwimLiveBoundary(&bot->compound_swim, &host,
		    &pose, &observation);
		GameFailure(&context, (uint32_t)link_index, was_recovering, &result);
		if (!bot->compound_swim.guard_owned ||
		    result.outcome == SG_COMPOUND_SWIM_LIVE_COMPLETE ||
		    result.outcome == SG_COMPOUND_SWIM_LIVE_SAFE_STOPPED)
			return true;
	}
	if (bot->compound_swim.recovering &&
	    bot->compound_swim.replay_kind ==
	        SG_COMPOUND_SWIM_LIVE_REPLAY_NONE)
	{
		was_recovering = bot->compound_swim.recovering;
		result = SG_CompoundSwimLiveRecover(&bot->compound_swim, &host,
		    &pose, bot->ent->client->oldvelocity[2]);
		GameFailure(&context, (uint32_t)link_index, was_recovering, &result);
		if (!bot->compound_swim.guard_owned ||
		    result.outcome == SG_COMPOUND_SWIM_LIVE_SAFE_STOPPED)
			return true;
	}
	for (step = 0; step < 4 && bot->compound_swim.guard_owned; step++)
	{
		usercmd_t command;

		if (!GamePose(bot, &pose))
			return true;
		was_recovering = bot->compound_swim.recovering;
		result = SG_CompoundSwimLivePreStep(&bot->compound_swim, &host,
		    &pose, &command);
		GameFailure(&context, (uint32_t)link_index, was_recovering, &result);
		if (!result.command_ready)
			return true;
		ClientThink(bot->ent, &command);
		if (step == 3)
			break;
		if (!GamePose(bot, &pose))
			return true;
		observation = GameObservation(bot, &bot->compound_swim, &pose);
		was_recovering = bot->compound_swim.recovering;
		result = SG_CompoundSwimLivePostStep(&bot->compound_swim, &host,
		    &pose, &observation);
		GameFailure(&context, (uint32_t)link_index, was_recovering, &result);
		if (!bot->compound_swim.guard_owned ||
		    result.outcome == SG_COMPOUND_SWIM_LIVE_COMPLETE ||
		    result.outcome == SG_COMPOUND_SWIM_LIVE_SAFE_STOPPED)
			break;
	}
	return true;
}

int SG_CompoundSwimGameStageAuthenticatedProbe(int link_index)
{
	rune_t *rune = SG_Rune();
	const sg_compound_publication_binding_t *binding;
	const sg_compound_world_preopen_t *mechanism;
	sg_bot_t *bot = NULL;
	edict_t *entity;
	vec3_t staged_origin;
	int axis, slot;

	if (!sg_cv.debug || sg_cv.debug->value <= 0.0f || !rune ||
	    link_index < 0 || link_index >= rune->hdr.num_links ||
	    !(binding = SG_CompoundPublicationBinding(rune,
	        (uint32_t)link_index)) ||
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
		    sg_bots[slot].compound_swim.guard_owned)
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
	memset(&bot->compound_swim, 0, sizeof(bot->compound_swim));
	bot->seed = binding->link.from;
	VectorCopy(entity->s.origin, bot->last_origin);
	bot->commit_link = link_index;
	bot->sticky_link = link_index;
	bot->commit_until = level.time + 5.0f;
	bot->latch_until = level.time + 5.0f;
	VectorCopy(entity->s.origin, staged_origin);
	sg_host.linkentity(entity);
	if (!SG_CompoundSwimGameEmit(bot, link_index) ||
	    !bot->compound_swim.guard_owned)
	{
		bot->commit_link = -1;
		bot->sticky_link = -1;
		return false;
	}
	if (sg_host.dprint)
		sg_host.dprint("slipgate: dswim probe-staged bot=%d link=%d "
		               "source=(%.3f %.3f %.3f) destination=(%.3f %.3f %.3f)\n",
		               slot, link_index, staged_origin[0], staged_origin[1],
		               staged_origin[2],
		               binding->destination_seed.origin[0],
		               binding->destination_seed.origin[1],
		               binding->destination_seed.origin[2]);
	return true;
}

static sg_bot_t *GameBotForEntity(edict_t *entity)
{
	int slot;

	for (slot = 0; slot < SG_MAXBOTS; slot++)
		if (sg_bots[slot].active && sg_bots[slot].ent == entity)
			return &sg_bots[slot];
	return NULL;
}

int SG_CompoundSwimGameAuthorizeTouch(edict_t *trigger,
	edict_t *activator)
{
	sg_bot_t *bot = GameBotForEntity(activator);
	sg_compound_swim_game_context_t context;
	sg_compound_swim_live_host_t host;
	sg_compound_swim_live_result_t result;
	sg_replay_pose_t pose;
	qboolean was_recovering;
	qboolean suppress_touch;

	if (!bot || !bot->compound_swim.guard_owned)
		return true;
	context.bot = bot;
	host = GameHost(&context);
	if (!GamePose(bot, &pose))
		return false;
	suppress_touch = bot->compound_swim.outer.phase !=
	    SG_COMPOUND_APPROACH;
	if (sg_cv.debug && sg_cv.debug->value > 0.0f && sg_host.dprint)
		sg_host.dprint("slipgate: dswim touch-observed bot=%d link=%u "
		               "phase=%d replay_ms=%d touch_ms=%d pending=%d "
		               "trigger=%d expected_trigger=%d "
		               "origin=(%.3f %.3f %.3f) anchor=(%.3f %.3f %.3f)\n",
		               (int)(bot - sg_bots),
		               bot->compound_swim.snapshot.binding.link_index,
		               bot->compound_swim.outer.phase,
		               bot->compound_swim.replay.progress.elapsed_ms,
		               bot->compound_swim.plan.touch_ms,
		               bot->compound_swim.command_pending,
		               GameEdictKey(trigger),
		               bot->compound_swim.snapshot.trigger_key,
		               pose.origin[0], pose.origin[1], pose.origin[2],
		               bot->compound_swim.plan.mechanism_anchor[0],
		               bot->compound_swim.plan.mechanism_anchor[1],
		               bot->compound_swim.plan.mechanism_anchor[2]);
	was_recovering = bot->compound_swim.recovering;
	result = SG_CompoundSwimLiveAuthorizeTouch(&bot->compound_swim, &host,
	    GameEdictKey(trigger), &pose, level.framenum);
	GameFailure(&context, bot->compound_swim.snapshot.binding.link_index,
	            was_recovering, &result);
	return !suppress_touch &&
	    result.outcome == SG_COMPOUND_SWIM_LIVE_RUNNING;
}

int SG_CompoundSwimGameAuthorizeActivation(edict_t *trigger,
	edict_t *mover, edict_t *activator)
{
	sg_bot_t *bot = GameBotForEntity(activator);
	sg_compound_swim_game_context_t context;
	sg_compound_swim_live_host_t host;
	sg_compound_swim_live_result_t result;
	qboolean was_recovering;

	if (!bot || !bot->compound_swim.guard_owned)
		return true;
	context.bot = bot;
	host = GameHost(&context);
	was_recovering = bot->compound_swim.recovering;
	result = SG_CompoundSwimLiveAuthorizeActivation(&bot->compound_swim,
	    &host, GameEdictKey(trigger), GameEdictKey(mover), level.framenum);
	GameFailure(&context, bot->compound_swim.snapshot.binding.link_index,
	            was_recovering, &result);
	return result.outcome == SG_COMPOUND_SWIM_LIVE_RUNNING;
}

void SG_CompoundSwimGameOrphaned(sg_bot_t *bot)
{
	sg_mover_lease_record_t record;

	if (!bot || !bot->compound_swim.guard_owned)
		return;
	memset(&record, 0, sizeof(record));
	if (SG_CompoundGuardValidate(&bot->compound_guard, &record) ==
	        SG_COMPOUND_GUARD_OK &&
	    record.law == SG_MOVER_LAW_COMPOUND_PREOPEN &&
	    record.state == SG_MOVER_LEASE_ORPHAN && record.key_count == 1U)
		(void)SG_CompoundSwimLiveOrphaned(&bot->compound_swim,
		    (uint32_t)record.link_index, record.keys[0]);
}

void SG_CompoundSwimGameClientRetired(edict_t *client)
{
	sg_bot_t *bot = GameBotForEntity(client);
	sg_mover_lease_record_t record;
	sg_compound_guard_result_t result;

	if (!bot || !bot->compound_swim.guard_owned)
		return;
	memset(&record, 0, sizeof(record));
	result = SG_CompoundGuardValidate(&bot->compound_guard, &record);
	if (result == SG_COMPOUND_GUARD_OK &&
	    record.law == SG_MOVER_LAW_COMPOUND_PREOPEN &&
	    record.state == SG_MOVER_LEASE_ORPHAN && record.key_count == 1U)
		(void)SG_CompoundSwimLiveOrphaned(&bot->compound_swim,
		    (uint32_t)record.link_index, record.keys[0]);
	else if (result == SG_COMPOUND_GUARD_NO_LEASE)
		(void)SG_CompoundSwimLiveOrphaned(&bot->compound_swim,
		    bot->compound_swim.snapshot.binding.link_index,
		    bot->compound_swim.snapshot.mover_key);
}

void SG_CompoundSwimGameReset(sg_bot_t *bot)
{
	sg_compound_guard_result_t result;

	if (!bot)
		return;
	if (bot->compound_swim.guard_owned)
	{
		result = SG_CompoundGuardValidate(&bot->compound_guard, NULL);
		if (result != SG_COMPOUND_GUARD_NO_LEASE &&
		    result != SG_COMPOUND_GUARD_NOT_ATTACHED)
			return;
		(void)SG_CompoundSwimLiveOrphaned(&bot->compound_swim,
		    bot->compound_swim.snapshot.binding.link_index,
		    bot->compound_swim.snapshot.mover_key);
	}
	memset(&bot->compound_swim, 0, sizeof(bot->compound_swim));
}
