#include "../q_shared.h"
#include "sg_chain_hook_replay.h"

#include <math.h>
#include <string.h>

static sg_chain_hook_replay_result_t ChainResult(
	const sg_chain_hook_replay_state_t *state,
	sg_chain_hook_replay_effect_t effect)
{
	sg_chain_hook_replay_result_t result;

	result.status = state ? state->status : SG_REPLAY_FAILED;
	result.reason = state ? state->reason : SG_REPLAY_REASON_INVALID_ARGUMENT;
	result.effect = effect;
	return result;
}

static sg_chain_hook_replay_result_t ChainFail(
	sg_chain_hook_replay_state_t *state, sg_replay_reason_t reason)
{
	if (state)
	{
		state->status = SG_REPLAY_FAILED;
		state->reason = reason;
		state->phase = SG_CHAIN_HOOK_REPLAY_FAILED;
		state->aim_step_pending = false;
	}
	return ChainResult(state, SG_CHAIN_HOOK_REPLAY_EFFECT_NONE);
}

static qboolean ChainFinite3(const vec3_t value)
{
	return value && isfinite(value[0]) && isfinite(value[1]) &&
	       isfinite(value[2]);
}

static qboolean ChainPoseValid(const sg_replay_pose_t *pose)
{
	return pose && ChainFinite3(pose->origin) && ChainFinite3(pose->velocity) &&
	       (pose->grounded == false || pose->grounded == true) &&
	       pose->waterlevel >= 0 && pose->waterlevel <= 3;
}

static qboolean ChainPoseEqual(const sg_replay_pose_t *first,
	const sg_replay_pose_t *second)
{
	return first && second &&
	       memcmp(&first->pms, &second->pms, sizeof(first->pms)) == 0 &&
	       memcmp(first->origin, second->origin, sizeof(first->origin)) == 0 &&
	       memcmp(first->velocity, second->velocity,
	              sizeof(first->velocity)) == 0 &&
	       first->grounded == second->grounded &&
	       first->watertype == second->watertype &&
	       first->waterlevel == second->waterlevel;
}

static qboolean ChainCheckpointEqual(const sg_chain_hook_checkpoint_t *expected,
	const sg_replay_pose_t *pose, float old_frame_z)
{
	return expected && ChainPoseEqual(&expected->pose, pose) &&
	       memcmp(&expected->old_frame_z, &old_frame_z,
	              sizeof(old_frame_z)) == 0;
}

static sg_chain_hook_replay_result_t ChainRopeStatus(
	sg_chain_hook_replay_state_t *state, sg_replay_status_t status,
	const sg_replay_pose_t *pose)
{
	if (status == SG_REPLAY_FAILED)
		return ChainFail(state, state->rope.progress.reason);
	if (status == SG_REPLAY_ARRIVED)
	{
		if (state->phase != SG_CHAIN_HOOK_REPLAY_SECOND_ROPE)
			return ChainFail(state, SG_REPLAY_REASON_HOOK_EVENT_ORDER);
		state->status = SG_REPLAY_ARRIVED;
		state->phase = SG_CHAIN_HOOK_REPLAY_COMPLETE;
		return ChainResult(state, SG_CHAIN_HOOK_REPLAY_EFFECT_NONE);
	}
	if (status == SG_REPLAY_RELEASED)
	{
		if (state->phase != SG_CHAIN_HOOK_REPLAY_FIRST_ROPE ||
		    !ChainCheckpointEqual(&state->spec.refire_start, pose,
		                          state->rope.progress.old_frame_z))
			return ChainFail(state, SG_REPLAY_REASON_CHECKPOINT_MISMATCH);
		state->phase = SG_CHAIN_HOOK_REPLAY_SECOND_AIM;
		state->aim_step = 0;
		state->aim_step_pending = false;
		state->status = SG_REPLAY_RUNNING;
	}
	return ChainResult(state,
	    state->rope.release_requested && !state->rope.release_applied
	        ? SG_CHAIN_HOOK_REPLAY_EFFECT_RELEASE
	        : SG_CHAIN_HOOK_REPLAY_EFFECT_NONE);
}

sg_chain_hook_replay_result_t SG_ChainHookReplayBegin(
	sg_chain_hook_replay_state_t *state,
	const sg_chain_hook_replay_spec_t *spec,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation, float old_frame_z)
{
	sg_replay_status_t status;

	if (!state || !spec || !ChainPoseValid(pose) || !observation ||
	    !isfinite(old_frame_z) ||
	    spec->rope[0].terminal !=
	        SG_HOOK_REPLAY_TERMINAL_RELEASE_HANDOFF ||
	    spec->rope[1].terminal != SG_HOOK_REPLAY_TERMINAL_SETTLE ||
	    !SG_HookReplaySpecValid(&spec->rope[0]) ||
	    !SG_HookReplaySpecValid(&spec->rope[1]) ||
	    !ChainPoseValid(&spec->refire_start.pose) ||
	    !ChainPoseValid(&spec->second_fire.pose) ||
	    !isfinite(spec->refire_start.old_frame_z) ||
	    !isfinite(spec->second_fire.old_frame_z))
	{
		if (state)
		{
			memset(state, 0, sizeof(*state));
			return ChainFail(state, SG_REPLAY_REASON_INVALID_ARGUMENT);
		}
		return ChainResult(NULL, SG_CHAIN_HOOK_REPLAY_EFFECT_NONE);
	}
	memset(state, 0, sizeof(*state));
	state->spec = *spec;
	state->phase = SG_CHAIN_HOOK_REPLAY_FIRST_ROPE;
	state->status = SG_REPLAY_RUNNING;
	state->reason = SG_REPLAY_REASON_NONE;
	status = SG_HookReplayBegin(&state->rope, &state->spec.rope[0], pose,
	                            observation, old_frame_z);
	return ChainRopeStatus(state, status, pose);
}

sg_chain_hook_replay_result_t SG_ChainHookReplayPreStep(
	sg_chain_hook_replay_state_t *state, const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation, usercmd_t *command)
{
	sg_replay_status_t status;

	if (!state || state->status != SG_REPLAY_RUNNING ||
	    !ChainPoseValid(pose) || !observation || !command)
		return state ? ChainFail(state, SG_REPLAY_REASON_INVALID_STATE) :
		               ChainResult(NULL, SG_CHAIN_HOOK_REPLAY_EFFECT_NONE);
	if (state->phase == SG_CHAIN_HOOK_REPLAY_FIRST_ROPE ||
	    state->phase == SG_CHAIN_HOOK_REPLAY_SECOND_ROPE)
	{
		status = SG_HookReplayPreStep(&state->rope, pose, observation,
		                              command);
		return ChainRopeStatus(state, status, pose);
	}
	if (state->phase != SG_CHAIN_HOOK_REPLAY_SECOND_AIM ||
	    state->aim_step_pending || state->aim_step < 0 ||
	    state->aim_step >= SG_REPLAY_FRAME_MS / SG_REPLAY_STEP_MS ||
	    !SG_HookReplayFixedViewCommand(pose,
	                                  state->spec.rope[1].view_angles,
	                                  command))
		return ChainFail(state, SG_REPLAY_REASON_HOOK_EVENT_ORDER);
	state->aim_step_pending = true;
	return ChainResult(state, SG_CHAIN_HOOK_REPLAY_EFFECT_NONE);
}

sg_chain_hook_replay_result_t SG_ChainHookReplayPostStep(
	sg_chain_hook_replay_state_t *state, const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation, float old_frame_z)
{
	sg_replay_status_t status;

	if (!state || state->status != SG_REPLAY_RUNNING ||
	    !ChainPoseValid(pose) || !observation || !isfinite(old_frame_z))
		return state ? ChainFail(state, SG_REPLAY_REASON_INVALID_STATE) :
		               ChainResult(NULL, SG_CHAIN_HOOK_REPLAY_EFFECT_NONE);
	if (state->phase == SG_CHAIN_HOOK_REPLAY_FIRST_ROPE ||
	    state->phase == SG_CHAIN_HOOK_REPLAY_SECOND_ROPE)
	{
		status = SG_HookReplayPostStep(&state->rope, pose, observation);
		return ChainRopeStatus(state, status, pose);
	}
	if (state->phase != SG_CHAIN_HOOK_REPLAY_SECOND_AIM ||
	    !state->aim_step_pending)
		return ChainFail(state, SG_REPLAY_REASON_HOOK_EVENT_ORDER);
	state->aim_step_pending = false;
	state->aim_step++;
	if (state->aim_step < SG_REPLAY_FRAME_MS / SG_REPLAY_STEP_MS)
		return ChainResult(state, SG_CHAIN_HOOK_REPLAY_EFFECT_NONE);
	if (!ChainCheckpointEqual(&state->spec.second_fire, pose, old_frame_z))
		return ChainFail(state, SG_REPLAY_REASON_CHECKPOINT_MISMATCH);
	state->phase = SG_CHAIN_HOOK_REPLAY_WAIT_SECOND_FIRE;
	return ChainResult(state, SG_CHAIN_HOOK_REPLAY_EFFECT_FIRE_NEXT);
}

sg_chain_hook_replay_result_t SG_ChainHookReplayEvent(
	sg_chain_hook_replay_state_t *state, sg_chain_hook_replay_event_t event,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation, float old_frame_z)
{
	sg_replay_status_t status;

	if (!state || state->status != SG_REPLAY_RUNNING ||
	    !ChainPoseValid(pose) || !observation || !isfinite(old_frame_z))
		return state ? ChainFail(state, SG_REPLAY_REASON_INVALID_STATE) :
		               ChainResult(NULL, SG_CHAIN_HOOK_REPLAY_EFFECT_NONE);
	if (event == SG_CHAIN_HOOK_REPLAY_EVENT_NEXT_FIRED)
	{
		if (state->phase != SG_CHAIN_HOOK_REPLAY_WAIT_SECOND_FIRE ||
		    !ChainCheckpointEqual(&state->spec.second_fire, pose, old_frame_z))
			return ChainFail(state, SG_REPLAY_REASON_HOOK_EVENT_ORDER);
		state->phase = SG_CHAIN_HOOK_REPLAY_SECOND_ROPE;
		status = SG_HookReplayBegin(&state->rope, &state->spec.rope[1], pose,
		                            observation, old_frame_z);
		return ChainRopeStatus(state, status, pose);
	}
	if (state->phase != SG_CHAIN_HOOK_REPLAY_FIRST_ROPE &&
	    state->phase != SG_CHAIN_HOOK_REPLAY_SECOND_ROPE)
		return ChainFail(state, SG_REPLAY_REASON_HOOK_EVENT_ORDER);
	switch (event)
	{
	case SG_CHAIN_HOOK_REPLAY_EVENT_ATTACHED:
		status = SG_HookReplayAttached(&state->rope, pose);
		break;
	case SG_CHAIN_HOOK_REPLAY_EVENT_PULL_APPLIED:
		status = SG_HookReplayPullApplied(&state->rope, pose);
		break;
	case SG_CHAIN_HOOK_REPLAY_EVENT_RELEASE_APPLIED:
		status = SG_HookReplayReleaseApplied(&state->rope, pose);
		break;
	default:
		return ChainFail(state, SG_REPLAY_REASON_HOOK_EVENT_ORDER);
	}
	return ChainRopeStatus(state, status, pose);
}
