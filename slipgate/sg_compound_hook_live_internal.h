#ifndef SG_COMPOUND_HOOK_LIVE_INTERNAL_H
#define SG_COMPOUND_HOOK_LIVE_INTERNAL_H

#include "../q_shared.h"
#include "sg_compound_hook_live.h"

sg_compound_hook_live_result_t CompoundHookLiveResult(
	sg_compound_hook_live_outcome_t outcome,
	sg_compound_hook_live_failure_t failure,
	sg_replay_reason_t reason, qboolean command_ready);
qboolean CompoundHookLiveHostValid(
	const sg_compound_hook_live_host_t *host);
qboolean CompoundHookLiveBoltValid(
	const sg_compound_hook_live_bolt_t *bolt);
qboolean CompoundHookLiveBoltEqual(
	const sg_compound_hook_live_bolt_t *first,
	const sg_compound_hook_live_bolt_t *second);
sg_compound_hook_live_host_result_t CompoundHookLiveCurrent(
	const sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host);
sg_compound_hook_live_host_result_t CompoundHookLiveAuthorized(
	const sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host);
sg_compound_hook_live_result_t CompoundHookLiveActive(
	const sg_compound_hook_live_state_t *state, qboolean command_ready);
sg_compound_hook_live_result_t CompoundHookLiveOwnedFailure(
	sg_compound_hook_live_state_t *state,
	sg_compound_hook_live_failure_t failure, sg_replay_reason_t reason);
qboolean CompoundHookLiveBeginSwim(
	sg_compound_hook_live_state_t *state,
	const sg_replay_pose_t *pose,
	const sg_replay_observation_t *observation, const vec3_t destination,
	qboolean destination_water, float old_frame_z,
	sg_compound_hook_live_control_t control);
qboolean CompoundHookLiveDuplicate(
	const sg_compound_hook_live_state_t *state,
	sg_compound_hook_live_event_t event,
	const sg_compound_hook_live_bolt_t *bolt, int frame_serial);
void CompoundHookLiveRemember(sg_compound_hook_live_state_t *state,
	sg_compound_hook_live_event_t event, int frame_serial);
qboolean CompoundHookLiveClear(
	const sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host);
void CompoundHookLiveClearLocal(sg_compound_hook_live_state_t *state);
sg_compound_hook_live_result_t CompoundHookLiveFinishRelease(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host, qboolean recovered);

#endif
