#ifndef SG_COMPOUND_HOOK_LIVE_INTERNAL_H
#define SG_COMPOUND_HOOK_LIVE_INTERNAL_H

#include "../q_shared.h"
#include "sg_compound_hook_live.h"

sg_compound_hook_live_result_t CompoundHookLiveResult(
	sg_compound_hook_live_outcome_t outcome,
	sg_compound_hook_live_failure_t failure,
	sg_replay_reason_t reason, qboolean command_ready);
int CompoundHookLiveTopBoundaryMs(
	const sg_compound_publication_binding_t *binding);
qboolean CompoundHookLiveHostValid(
	const sg_compound_hook_live_host_t *host);
sg_compound_hook_live_host_result_t CompoundHookLiveCurrent(
	const sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host);
sg_compound_hook_live_host_result_t CompoundHookLiveAuthorized(
	const sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host);
sg_compound_hook_live_result_t CompoundHookLiveActive(
	const sg_compound_hook_live_state_t *state, qboolean command_ready);
qboolean CompoundHookLiveObserveSweep(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host,
	const sg_replay_pose_t *pose, int next_ms);
sg_compound_hook_live_result_t CompoundHookLiveOwnedFailure(
	sg_compound_hook_live_state_t *state,
	sg_compound_hook_live_failure_t failure, sg_replay_reason_t reason);
qboolean CompoundHookLiveClear(
	const sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host);
void CompoundHookLiveClearLocal(sg_compound_hook_live_state_t *state);
sg_compound_hook_live_result_t CompoundHookLiveFinishRelease(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host, qboolean recovered);

#endif
