/* Production-owner construction and evaluation seams.  Ordinary consumers
 * cannot issue, activate, or retire publications. */
#ifndef SG_HOST_LAW_PUBLICATION_PRIVATE_H
#define SG_HOST_LAW_PUBLICATION_PRIVATE_H

#include <stddef.h>
#include <stdint.h>

#include "sg_host_law_publication.h"

#if defined(__GNUC__) || defined(__clang__)
#define SG_HOST_LAW_PRIVATE_VISIBILITY \
	__attribute__((visibility("hidden")))
#else
#define SG_HOST_LAW_PRIVATE_VISIBILITY
#endif

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
sg_host_law_result_t SG_HostLawPublicationOwnerConstructionIssue(
	const sg_host_law_publication_t *publication,
	const sg_host_collision_authority_t *authority,
	sg_host_law_construction_t **construction_out);
/* Offline extensions receive only a currentness-checked byte copy and sealed
 * metadata.  No construction-owned pointer crosses the translation-unit
 * boundary.  A NULL byte buffer queries the required size. */
SG_HOST_LAW_PRIVATE_VISIBILITY sg_host_law_result_t
SG_HostLawConstructionOwnerCopyBsp(
	const sg_host_law_construction_t *construction, uint8_t *bytes_out,
	size_t capacity, size_t *size_out,
	sg_host_static_identity_t *identity_out);
void SG_HostLawPublicationOwnerDestroy(
	sg_host_law_publication_t *publication);
sg_host_law_result_t SG_HostLawPublicationOwnerPmove(
	const sg_host_law_publication_t *publication, uint32_t subject_index,
	const sg_host_pmove_request_t *request,
	sg_host_pmove_result_t *result_out, sg_host_pmove_error_t *error_out);
sg_host_law_result_t SG_HostLawPublicationOwnerSubject(
	const sg_host_law_publication_t *publication, uint32_t subject_index,
	sg_host_engine_subject_identity_t *subject_out);
sg_host_law_result_t SG_HostLawPublicationOwnerSubjectCurrent(
	const sg_host_law_publication_t *publication,
	const sg_host_engine_subject_identity_t *subject);
sg_host_law_result_t SG_HostLawPublicationOwnerSubjectState(
	const sg_host_law_publication_t *publication,
	const sg_host_engine_subject_identity_t *subject,
	sg_host_pmove_state_observation_t *observation_out);
sg_host_law_result_t SG_HostLawPublicationOwnerReplayFrame(
	const sg_host_law_publication_t *publication,
	const sg_host_engine_subject_identity_t *subject,
	const sg_host_pmove_request_t *request,
	const sg_host_pmove_replay_workspace_t *workspace,
	sg_host_pmove_replay_t *replay_out, sg_host_pmove_error_t *error_out);
sg_host_law_result_t SG_HostLawPublicationOwnerSubjectTrace(
	const sg_host_law_publication_t *publication,
	const sg_host_engine_subject_identity_t *subject,
	const float start[3], const float mins[3], const float maxs[3],
	const float end[3], sg_host_collision_contents_t mask,
	sg_host_collision_trace_t *trace_out);
sg_host_law_result_t SG_HostLawPublicationOwnerSubjectPointContents(
	const sg_host_law_publication_t *publication,
	const sg_host_engine_subject_identity_t *subject, const float point[3],
	sg_host_collision_contents_t *contents_out);
sg_host_law_result_t SG_HostLawPublicationOwnerSubjectClassifyPose(
	const sg_host_law_publication_t *publication,
	const sg_host_engine_subject_identity_t *subject, const float origin[3],
	sg_rune_stance_t stance, sg_host_collision_pose_t *pose_out);
sg_host_law_result_t SG_HostLawPublicationOwnerSubjectTransition(
	const sg_host_law_publication_t *publication,
	const sg_host_engine_subject_identity_t *subject, const float start[3],
	const float end[3], sg_rune_stance_t stance,
	sg_host_collision_transition_t *transition_out);
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

#undef SG_HOST_LAW_PRIVATE_VISIBILITY

#endif /* SG_HOST_LAW_PUBLICATION_PRIVATE_H */
