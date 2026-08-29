/* Compatibility include retained for build manifests.  Runtime activation is
 * owner-derived through sg_host_engine_runtime.h; this header exposes no
 * token, acceptance handle, or caller-shaped identity tuple. */
#ifndef SG_HOST_ENGINE_RUNTIME_PRIVATE_H
#define SG_HOST_ENGINE_RUNTIME_PRIVATE_H

#include "sg_host_engine_runtime.h"
/* Runtime activation remains fail-closed until the downstream cutover owns an
 * opaque acceptance capability.  A constructible artifact snapshot is never
 * an activation credential. */
sg_host_engine_runtime_status_t SG_HostEngineRuntimeOwnerActivateAcceptedV2(
	sg_host_engine_runtime_t *runtime);
void SG_HostEngineRuntimeOwnerClearAcceptance(
	sg_host_engine_runtime_t *runtime);
sg_host_engine_runtime_status_t SG_HostEngineRuntimeOwnerBindActiveSubject(
	sg_host_engine_runtime_t *runtime, uint32_t subject_index);
int SG_HostEngineRuntimeOwnerHookCollision(
	const sg_host_engine_runtime_t *runtime, uint32_t target_index,
	int32_t surface_flags, sg_host_hook_collision_t *collision_out);

#endif /* SG_HOST_ENGINE_RUNTIME_PRIVATE_H */
