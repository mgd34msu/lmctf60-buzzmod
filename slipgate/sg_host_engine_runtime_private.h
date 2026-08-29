/* Compatibility include retained for build manifests.  Runtime activation is
 * owner-derived through sg_host_engine_runtime.h; this header exposes no
 * token, acceptance handle, or caller-shaped identity tuple. */
#ifndef SG_HOST_ENGINE_RUNTIME_PRIVATE_H
#define SG_HOST_ENGINE_RUNTIME_PRIVATE_H

#include "sg_host_engine_runtime.h"

/* The production owner retains the BSP.  Runtime activation calls this
 * resolver itself so no install caller can nominate a different world. */
const sg_bsp_world_t *SG_HostLawOwnerRetainedWorld(void);

/* Only sg_host_law_owner.c may issue the active RUNE/world join or resolve a
 * subject.  These symbols are intentionally absent from the public runtime
 * interface; the runtime resolves the retained world and active RUNE itself. */
sg_host_engine_runtime_status_t SG_HostEngineRuntimeOwnerInstallActiveRune(
	sg_host_engine_runtime_t *runtime);
void SG_HostEngineRuntimeOwnerClearAcceptance(
	sg_host_engine_runtime_t *runtime);
sg_host_engine_runtime_status_t SG_HostEngineRuntimeOwnerBindActiveSubject(
	sg_host_engine_runtime_t *runtime, uint32_t subject_index);

#endif /* SG_HOST_ENGINE_RUNTIME_PRIVATE_H */
