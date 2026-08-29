/* Production owner for the immutable host-law publication. */
#ifndef SG_HOST_LAW_OWNER_H
#define SG_HOST_LAW_OWNER_H

#include <stdint.h>

#include "sg_destination.h"
#include "sg_host_law_publication.h"

/* Capture the protected level identity and exact engine callback slots, then
 * publish construction and bot-runtime views as one owner transaction. */
sg_host_law_result_t SG_HostLawProductionBeginLevel(const char *mapname);

/* Reuse a current provider or rebuild both views after a level change. */
sg_host_law_result_t SG_HostLawProductionEnsureLevel(const char *mapname);

/* Execute bot movement and hook-fire chronology only through the current
 * owner-issued runtime publication.  Both calls authenticate the live edict
 * subject immediately before entering the captured engine callbacks. */
sg_host_law_result_t SG_HostLawProductionPmove(uint32_t subject_index,
	const sg_host_pmove_request_t *request, sg_host_pmove_result_t *result_out,
	sg_host_pmove_error_t *error_out);
sg_host_law_result_t SG_HostLawProductionHookFire(uint32_t subject_index,
	uint32_t hook_index,
	sg_host_hook_step_t *step_out);

/* Level teardown invalidates both publications and their captured runtime. */
void SG_HostLawProductionReset(void);

/* The frame owner calls this before consumers use the engine provider. */
sg_host_law_result_t SG_HostLawProductionRevalidate(void);

const sg_host_law_publication_t *SG_HostLawProductionPublication(void);
const sg_host_law_publication_t *SG_HostLawProductionStaticPublication(void);

/* Legacy controller-backed construction view.  Engine-static publications do
 * not manufacture a reparsed collision authority and return unavailable. */
sg_host_law_result_t SG_HostLawProductionCollisionAuthority(
	const sg_host_collision_authority_t **authority_out);

/* Runtime-backed trace for an engine-owned bot subject. */
sg_host_law_result_t SG_HostLawProductionEngineTrace(
	uint32_t subject_index, const float start[3], const float mins[3],
	const float maxs[3],
	const float end[3], sg_host_collision_contents_t mask,
	sg_host_collision_trace_t *trace_out);

#endif /* SG_HOST_LAW_OWNER_H */
