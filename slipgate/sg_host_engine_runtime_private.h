/* Owner-only activation and live hook observations.  This header exposes no
 * token, acceptance handle, or caller-shaped identity tuple. */
#ifndef SG_HOST_ENGINE_RUNTIME_PRIVATE_H
#define SG_HOST_ENGINE_RUNTIME_PRIVATE_H

#include "sg_host_engine_runtime.h"
/* Authenticate the runtime only from its captured engine epoch.  No caller
 * identity, artifact, callback, or world participates in this transition. */
sg_host_engine_runtime_status_t SG_HostEngineRuntimeOwnerActivate(
	sg_host_engine_runtime_t *runtime);
void SG_HostEngineRuntimeOwnerClearAcceptance(
	sg_host_engine_runtime_t *runtime);
int SG_HostEngineRuntimeOwnerSubject(
	const sg_host_engine_runtime_t *runtime, uint32_t subject_index,
	sg_host_engine_subject_identity_t *subject_out);
int SG_HostEngineRuntimeOwnerSubjectCurrent(
	const sg_host_engine_runtime_t *runtime,
	const sg_host_engine_subject_identity_t *subject);
int SG_HostEngineRuntimeOwnerSubjectState(
	const sg_host_engine_runtime_t *runtime,
	const sg_host_engine_subject_identity_t *subject,
	sg_host_pmove_state_observation_t *observation_out);
int SG_HostEngineRuntimeOwnerReplayFrame(
	const sg_host_engine_runtime_t *runtime,
	const sg_host_engine_subject_identity_t *subject,
	const sg_host_pmove_request_t *request,
	const sg_host_pmove_replay_workspace_t *workspace,
	sg_host_pmove_replay_t *replay_out, sg_host_pmove_error_t *error_out);
int SG_HostEngineRuntimeOwnerTrace(
	const sg_host_engine_runtime_t *runtime,
	const sg_host_engine_subject_identity_t *subject,
	const float start[3], const float mins[3], const float maxs[3],
	const float end[3], sg_host_collision_contents_t mask,
	sg_host_collision_trace_t *trace_out);
int SG_HostEngineRuntimeOwnerPointContents(
	const sg_host_engine_runtime_t *runtime,
	const sg_host_engine_subject_identity_t *subject, const float point[3],
	sg_host_collision_contents_t *contents_out);
int SG_HostEngineRuntimeOwnerClassifyPose(
	const sg_host_engine_runtime_t *runtime,
	const sg_host_engine_subject_identity_t *subject, const float origin[3],
	sg_rune_stance_t stance, sg_host_collision_pose_t *pose_out);
int SG_HostEngineRuntimeOwnerTransition(
	const sg_host_engine_runtime_t *runtime,
	const sg_host_engine_subject_identity_t *subject, const float start[3],
	const float end[3], sg_rune_stance_t stance,
	sg_host_collision_transition_t *transition_out);
int SG_HostEngineRuntimeOwnerHookCollision(
	const sg_host_engine_runtime_t *runtime, uint32_t subject_index,
	uint32_t hook_index, uint32_t target_index, int32_t surface_flags,
	sg_host_hook_observation_t *observation_out);
int SG_HostEngineRuntimeOwnerHookPullInputs(
	const sg_host_engine_runtime_t *runtime, uint32_t subject_index,
	uint32_t hook_index, vec3_t start_out, vec3_t bite_out);

#endif /* SG_HOST_ENGINE_RUNTIME_PRIVATE_H */
