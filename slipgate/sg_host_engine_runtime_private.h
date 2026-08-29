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
int SG_HostEngineRuntimeOwnerHookCollision(
	const sg_host_engine_runtime_t *runtime, uint32_t subject_index,
	uint32_t hook_index, uint32_t target_index, int32_t surface_flags,
	sg_host_hook_observation_t *observation_out);
int SG_HostEngineRuntimeOwnerHookPullInputs(
	const sg_host_engine_runtime_t *runtime, uint32_t subject_index,
	uint32_t hook_index, vec3_t start_out, vec3_t bite_out);

#endif /* SG_HOST_ENGINE_RUNTIME_PRIVATE_H */
