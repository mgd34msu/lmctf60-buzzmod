/* Production owner for the immutable host-law publication. */
#ifndef SG_HOST_LAW_OWNER_H
#define SG_HOST_LAW_OWNER_H

#include <stdint.h>

#include "sg_destination.h"
#include "sg_host_law_publication.h"

typedef sg_host_engine_subject_identity_t sg_host_law_subject_t;

#define SG_HOST_LAW_RUNTIME_AUTHORITY_VERSION UINT32_C(1)

/* Owner-issued authority for one exact production publication lifetime.
 * The epoch is not a law fingerprint: resetting or replacing an otherwise
 * byte-identical publication permanently revokes every previously issued
 * authority. */
typedef struct sg_host_law_runtime_authority_s
{
	uint32_t version;
	uint32_t reserved;
	uint64_t epoch;
	uint64_t epoch_complement;
	const sg_host_law_publication_t *publication;
	sg_host_law_view_t view;
} sg_host_law_runtime_authority_t;

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

/* Read the currently accepted production publication and derive one exact
 * live bot life.  Operations below reject a stale slot, respawned client,
 * reset level, or replaced engine callback before entering host code. */
sg_host_law_result_t SG_HostLawProductionRead(
	sg_host_law_view_t *view_out);
sg_host_law_result_t SG_HostLawProductionAcquire(
	sg_host_law_runtime_authority_t *authority_out);
sg_host_law_result_t SG_HostLawProductionAuthorityCurrent(
	const sg_host_law_runtime_authority_t *authority);
sg_host_law_result_t SG_HostLawProductionSubject(
	const sg_host_law_runtime_authority_t *authority,
	uint32_t subject_index, sg_host_law_subject_t *subject_out);
sg_host_law_result_t SG_HostLawProductionSubjectCurrent(
	const sg_host_law_runtime_authority_t *authority,
	const sg_host_law_subject_t *subject);
sg_host_law_result_t SG_HostLawProductionReplayFrame(
	const sg_host_law_runtime_authority_t *authority,
	const sg_host_law_subject_t *subject,
	const sg_host_pmove_request_t *request,
	const sg_host_pmove_replay_workspace_t *workspace,
	sg_host_pmove_replay_t *replay_out, sg_host_pmove_error_t *error_out);
sg_host_law_result_t SG_HostLawProductionSubjectTrace(
	const sg_host_law_runtime_authority_t *authority,
	const sg_host_law_subject_t *subject, const float start[3],
	const float mins[3], const float maxs[3], const float end[3],
	sg_host_collision_contents_t mask,
	sg_host_collision_trace_t *trace_out);
sg_host_law_result_t SG_HostLawProductionSubjectPointContents(
	const sg_host_law_runtime_authority_t *authority,
	const sg_host_law_subject_t *subject, const float point[3],
	sg_host_collision_contents_t *contents_out);
sg_host_law_result_t SG_HostLawProductionSubjectClassifyPose(
	const sg_host_law_runtime_authority_t *authority,
	const sg_host_law_subject_t *subject, const float origin[3],
	sg_rune_stance_t stance, sg_host_collision_pose_t *pose_out);
sg_host_law_result_t SG_HostLawProductionSubjectTransition(
	const sg_host_law_runtime_authority_t *authority,
	const sg_host_law_subject_t *subject, const float start[3],
	const float end[3], sg_rune_stance_t stance,
	sg_host_collision_transition_t *transition_out);

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
