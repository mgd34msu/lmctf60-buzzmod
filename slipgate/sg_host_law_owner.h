/* Production owner for the immutable host-law publication. */
#ifndef SG_HOST_LAW_OWNER_H
#define SG_HOST_LAW_OWNER_H

#include <stddef.h>
#include <stdint.h>

#include "sg_destination.h"
#include "sg_host_law_publication.h"

#ifndef SG_HOST_ENGINE_RUNTIME_ACCEPTANCE_TYPE
#define SG_HOST_ENGINE_RUNTIME_ACCEPTANCE_TYPE
typedef struct sg_host_engine_runtime_acceptance_s
	sg_host_engine_runtime_acceptance_t;
#endif

/* Install only after the production BSP/identity bridge has supplied the
 * exact authority.  A failed install leaves the previous owner untouched. */
sg_host_law_result_t SG_HostLawProductionInstall(
	const sg_host_collision_authority_t *authority);

/* Begin a level by capturing the exact engine callback slots and committed
 * level epoch.  This deliberately does not open a BSP or publish laws. */
sg_host_law_result_t SG_HostLawProductionBeginLevel(const char *mapname);

/* Join the staged runtime to the exact active RUNE snapshot and the
 * controller-selected BSP SHA-256.  The runtime owner derives generation and
 * topology from this accepted snapshot; callers cannot nominate them. */
sg_host_law_result_t SG_HostLawProductionInstallAccepted(
	const sg_host_engine_runtime_acceptance_t *acceptance);

/* Bind a live player subject by engine edict index.  The owner resolves and
 * authenticates the edict; no raw callback or edict pointer is public. */
sg_host_law_result_t SG_HostLawProductionBindSubject(uint32_t subject_index);

/* Construction-only bridge for a controller-retained BSP byte stream.  The
 * SHA-256 is computed from these exact bytes and must match the controller's
 * selected identity before a static collision authority is published. */
sg_host_law_result_t SG_HostLawProductionInstallConstruction(
	const void *bsp_bytes, size_t bsp_size,
	const sg_bsp_content_identity_t *content_identity,
	const sg_rune_model_identity_t *identity);

/* Level teardown invalidates every borrowed BSP and its publication. */
void SG_HostLawProductionReset(void);

/* The frame owner calls this before consumers can use host laws. */
sg_host_law_result_t SG_HostLawProductionRevalidate(void);

const sg_host_law_publication_t *SG_HostLawProductionPublication(void);

/* Owner-issued read-only collision view.  The pointer is borrowed until the
 * next level reset; NULL is returned whenever the publication is absent or
 * its live identity has drifted. */
sg_host_law_result_t SG_HostLawProductionCollisionAuthority(
	const sg_host_collision_authority_t **authority_out);

/* Opaque runtime-backed collision seam for localization/water consumers. */
sg_host_law_result_t SG_HostLawProductionEngineTrace(
	const float start[3], const float mins[3], const float maxs[3],
	const float end[3], sg_host_collision_contents_t mask,
	sg_host_collision_trace_t *trace_out);
sg_host_law_result_t SG_HostLawProductionEnginePointContents(
	const float point[3], sg_host_collision_contents_t *contents_out);

#endif /* SG_HOST_LAW_OWNER_H */
