/* Production owner for the immutable host-law publication. */
#ifndef SG_HOST_LAW_OWNER_H
#define SG_HOST_LAW_OWNER_H

#include <stdint.h>

#include "sg_destination.h"
#include "sg_host_law_publication.h"

/* Begin a level by capturing the exact engine callback slots and committed
 * level epoch.  This deliberately does not open a BSP or publish laws. */
sg_host_law_result_t SG_HostLawProductionBeginLevel(const char *mapname);

/* Idempotently converge the static host-law owner onto the current level.
 * Drift rebuilds A; runtime consumers remain fail-closed until the downstream
 * authenticated artifact owner binds B again. */
sg_host_law_result_t SG_HostLawProductionEnsureLevel(const char *mapname);

/* Bind a live player subject by engine edict index.  The owner resolves and
 * authenticates the edict; no raw callback or edict pointer is public. */
sg_host_law_result_t SG_HostLawProductionBindActiveSubject(uint32_t subject_index);

/* Execute bot movement and hook-fire chronology only through the current
 * owner-issued runtime publication.  Both calls authenticate the live edict
 * subject immediately before entering the captured engine callbacks. */
sg_host_law_result_t SG_HostLawProductionPmove(uint32_t subject_index,
	const sg_host_pmove_request_t *request, sg_host_pmove_result_t *result_out,
	sg_host_pmove_error_t *error_out);
sg_host_law_result_t SG_HostLawProductionHookFire(uint32_t subject_index,
	const sg_host_hook_fire_request_t *request,
	sg_host_hook_step_t *step_out);

/* Level teardown invalidates every borrowed BSP and its publication. */
void SG_HostLawProductionReset(void);

/* The frame owner calls this before consumers can use host laws. */
sg_host_law_result_t SG_HostLawProductionRevalidate(void);

const sg_host_law_publication_t *SG_HostLawProductionPublication(void);
const sg_host_law_publication_t *SG_HostLawProductionStaticPublication(void);

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
