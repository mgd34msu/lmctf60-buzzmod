/* Owner-only activation seam for the engine-backed host runtime. */
#ifndef SG_HOST_ENGINE_RUNTIME_PRIVATE_H
#define SG_HOST_ENGINE_RUNTIME_PRIVATE_H

#include <stdint.h>

#include "sg_destination.h"
#include "sg_host_engine_runtime.h"

#ifndef SG_HOST_ENGINE_RUNTIME_ACCEPTANCE_TYPE
#define SG_HOST_ENGINE_RUNTIME_ACCEPTANCE_TYPE
typedef struct sg_host_engine_runtime_acceptance_s
	sg_host_engine_runtime_acceptance_t;
#endif

/* The public runtime type deliberately has no candidate Join operation.  The
 * production RUNE owner first issues an opaque acceptance handle from the
 * exact active snapshot and selected-BSP digest; arbitrary identity/content
 * tuples can therefore never turn a staged runtime into authority. */
const void *SG_HostEngineRuntimeOwnerToken(void);
sg_host_engine_runtime_status_t SG_HostEngineRuntimeAcceptanceIssueOwner(
	const void *owner_token, const sg_rune_runtime_snapshot_t *snapshot,
	const sg_bsp_content_identity_t *content_identity,
	sg_host_engine_runtime_acceptance_t **acceptance_out);
void SG_HostEngineRuntimeAcceptanceDestroyOwner(
	sg_host_engine_runtime_acceptance_t *acceptance);
sg_host_engine_runtime_status_t SG_HostEngineRuntimeJoinOwner(
	sg_host_engine_runtime_t *runtime, const void *owner_token,
	const sg_host_engine_runtime_acceptance_t *acceptance);

/* Resolve the subject from the engine-owned edict array.  A raw edict pointer
 * never crosses the public runtime or host-law API. */
sg_host_engine_runtime_status_t SG_HostEngineRuntimeBindSubjectOwner(
	sg_host_engine_runtime_t *runtime, const void *owner_token,
	uint32_t subject_index);

#endif /* SG_HOST_ENGINE_RUNTIME_PRIVATE_H */
