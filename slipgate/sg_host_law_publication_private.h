/* Production-owner construction and evaluation seams.  Ordinary consumers
 * cannot issue, activate, or retire publications. */
#ifndef SG_HOST_LAW_PUBLICATION_PRIVATE_H
#define SG_HOST_LAW_PUBLICATION_PRIVATE_H

#include "sg_host_law_publication.h"

#ifdef SG_HOST_LAW_TESTING
sg_host_law_result_t SG_HostLawPublicationOwnerIssue(
	const sg_host_collision_authority_t *authority,
	sg_host_law_publication_t **publication_out);
sg_host_law_result_t SG_HostLawPublicationOwnerIssueStatic(
	const sg_host_static_identity_t *identity,
	sg_host_law_publication_t **publication_out);
#endif
sg_host_law_result_t SG_HostLawPublicationOwnerIssueEnginePair(
	sg_host_engine_runtime_t *runtime,
	sg_host_law_publication_t **construction_out,
	sg_host_law_publication_t **production_out);
void SG_HostLawPublicationOwnerDestroy(
	sg_host_law_publication_t *publication);
sg_host_law_result_t SG_HostLawPublicationOwnerPmove(
	const sg_host_law_publication_t *publication, uint32_t subject_index,
	const sg_host_pmove_request_t *request,
	sg_host_pmove_result_t *result_out, sg_host_pmove_error_t *error_out);
sg_host_law_result_t SG_HostLawPublicationOwnerEngineTrace(
	const sg_host_law_publication_t *publication, uint32_t subject_index,
	const float start[3], const float mins[3], const float maxs[3],
	const float end[3], sg_host_collision_contents_t mask,
	sg_host_collision_trace_t *trace_out);
sg_host_law_result_t SG_HostLawPublicationOwnerHookFire(
	const sg_host_law_publication_t *publication, uint32_t subject_index,
	uint32_t hook_index, sg_host_hook_step_t *step_out);
sg_host_law_result_t SG_HostLawPublicationOwnerHookTouch(
	const sg_host_law_publication_t *publication, uint32_t subject_index,
	uint32_t hook_index, uint32_t target_index, int32_t surface_flags,
	sg_host_hook_step_t *step_out);
sg_host_law_result_t SG_HostLawPublicationOwnerHookPullVelocity(
	const sg_host_law_publication_t *publication, uint32_t subject_index,
	uint32_t hook_index, vec3_t velocity, int *rope_length_out);

#endif /* SG_HOST_LAW_PUBLICATION_PRIVATE_H */
