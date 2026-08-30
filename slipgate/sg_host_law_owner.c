#include "../g_local.h"
#undef world

#include "sg_host_law_owner.h"
#include "sg_host_law_owner_internal.h"
#include "sg_host_law_publication_private.h"
#include "sg_host_engine_runtime.h"

#include <string.h>

typedef struct sg_host_law_owner_state_s
{
	sg_host_law_publication_t *production;
	sg_host_law_publication_t *construction;
	sg_host_engine_runtime_t *runtime;
	uint64_t epoch;
	char mapname[SG_LEVEL_IDENTITY_MAPNAME_BYTES];
} sg_host_law_owner_state_t;

static sg_host_law_owner_state_t sg_host_law_owner;
static uint64_t sg_host_law_last_epoch;

static sg_host_law_result_t Result(sg_host_law_status_t status,
	sg_host_law_field_t field, uint64_t expected, uint64_t observed)
{
	sg_host_law_result_t result;

	memset(&result, 0, sizeof(result));
	result.status = status;
	result.field = field;
	result.element = SG_HOST_LAW_ELEMENT_NONE;
	result.expected_bits = expected;
	result.observed_bits = observed;
	return result;
}

static sg_host_law_result_t Ok(void)
{
	return Result(SG_HOST_LAW_OK, SG_HOST_LAW_FIELD_NONE, 0U, 0U);
}

static sg_host_law_result_t HostUnavailable(void)
{
	return Result(SG_HOST_LAW_HOST_UNAVAILABLE, SG_HOST_LAW_FIELD_PMOVE_ABI,
		1U, 0U);
}

static void DestroyState(sg_host_law_owner_state_t *state)
{
	if (!state)
		return;
	if (state->production)
		SG_HostLawPublicationOwnerDestroy(state->production);
	if (state->construction)
		SG_HostLawPublicationOwnerDestroy(state->construction);
	if (state->runtime)
		SG_HostEngineRuntimeDestroy(state->runtime);
	memset(state, 0, sizeof(*state));
}

static sg_host_law_result_t RevalidateState(
	const sg_host_law_owner_state_t *state)
{
	sg_host_law_result_t result;

	if (!state || !state->runtime || !state->production ||
		!state->construction || !state->mapname[0] ||
		state->epoch == 0U ||
		!SG_HostEngineRuntimeCurrent(state->runtime))
		return HostUnavailable();
	result = SG_HostLawPublicationRevalidateProduction(state->production);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	return SG_HostLawPublicationRevalidateProduction(state->construction);
}

sg_host_law_result_t SG_HostLawProductionBeginLevel(const char *mapname)
{
	sg_host_law_owner_state_t next;
	sg_host_law_owner_state_t previous;
	sg_host_engine_runtime_status_t runtime_status;
	sg_host_law_result_t result;
	size_t mapname_length;

	if (!mapname || !mapname[0])
		return Result(SG_HOST_LAW_INVALID_ARGUMENT,
			SG_HOST_LAW_FIELD_BSP_CONTENT, 1U, 0U);
	mapname_length = strlen(mapname);
	if (mapname_length >= sizeof(next.mapname))
		return Result(SG_HOST_LAW_INVALID_ARGUMENT,
			SG_HOST_LAW_FIELD_BSP_CONTENT, sizeof(next.mapname) - 1U,
			mapname_length);
	memset(&next, 0, sizeof(next));
	runtime_status = SG_HostEngineRuntimeBegin(mapname, &next.runtime);
	if (runtime_status != SG_HOST_ENGINE_RUNTIME_OK)
		return Result(SG_HOST_LAW_HOST_UNAVAILABLE,
			SG_HOST_LAW_FIELD_PMOVE_ABI, SG_HOST_ENGINE_RUNTIME_OK,
			(uint64_t)runtime_status);
	result = SG_HostLawPublicationOwnerIssueEnginePair(next.runtime,
		&next.construction, &next.production);
	if (result.status != SG_HOST_LAW_OK)
	{
		DestroyState(&next);
		return result;
	}
	memcpy(next.mapname, mapname, mapname_length + 1U);
	if (sg_host_law_last_epoch == UINT64_MAX)
	{
		DestroyState(&next);
		return HostUnavailable();
	}
	next.epoch = sg_host_law_last_epoch + 1U;
	result = RevalidateState(&next);
	if (result.status != SG_HOST_LAW_OK)
	{
		DestroyState(&next);
		return result;
	}
	previous = sg_host_law_owner;
	sg_host_law_owner = next;
	sg_host_law_last_epoch = next.epoch;
	DestroyState(&previous);
	return Ok();
}

sg_host_law_result_t SG_HostLawProductionEnsureLevel(const char *mapname)
{
	sg_host_law_result_t result;

	if (!mapname || !mapname[0])
		return Result(SG_HOST_LAW_INVALID_ARGUMENT,
			SG_HOST_LAW_FIELD_BSP_CONTENT, 1U, 0U);
	if (strcmp(sg_host_law_owner.mapname, mapname) == 0)
	{
		result = SG_HostLawProductionRevalidate();
		if (result.status == SG_HOST_LAW_OK)
			return result;
	}
	return SG_HostLawProductionBeginLevel(mapname);
}

sg_host_law_result_t SG_HostLawProductionPmove(uint32_t subject_index,
	const sg_host_pmove_request_t *request, sg_host_pmove_result_t *result_out,
	sg_host_pmove_error_t *error_out)
{
	sg_host_law_result_t result = SG_HostLawProductionRevalidate();

	if (result.status != SG_HOST_LAW_OK)
		return result;
	return SG_HostLawPublicationOwnerPmove(sg_host_law_owner.production,
		subject_index, request, result_out, error_out);
}

sg_host_law_result_t SG_HostLawProductionRead(sg_host_law_view_t *view_out)
{
	sg_host_law_result_t result = SG_HostLawProductionRevalidate();

	if (result.status != SG_HOST_LAW_OK)
		return result;
	return SG_HostLawPublicationRead(sg_host_law_owner.production, view_out);
}

static int RuntimeAuthorityShapeValid(
	const sg_host_law_runtime_authority_t *authority)
{
	return authority &&
		authority->version == SG_HOST_LAW_RUNTIME_AUTHORITY_VERSION &&
		authority->reserved == 0U && authority->epoch != 0U &&
		authority->epoch_complement == ~authority->epoch;
}

static sg_host_law_result_t RuntimeAuthorityState(
	const sg_host_law_runtime_authority_t *authority,
	const sg_host_law_publication_t **publication_out)
{
	sg_host_law_result_t result;

	if (publication_out)
		*publication_out = NULL;
	if (!RuntimeAuthorityShapeValid(authority))
		return Result(SG_HOST_LAW_INVALID_ARGUMENT,
			SG_HOST_LAW_FIELD_VERSION,
			SG_HOST_LAW_RUNTIME_AUTHORITY_VERSION,
			authority ? authority->version : 0U);
	result = SG_HostLawProductionRevalidate();
	if (result.status != SG_HOST_LAW_OK)
		return result;
	if (authority->epoch != sg_host_law_owner.epoch)
		return Result(SG_HOST_LAW_PRODUCTION_DRIFT,
			SG_HOST_LAW_FIELD_VERSION, sg_host_law_owner.epoch,
			authority->epoch);
	result = SG_HostLawPublicationMatch(sg_host_law_owner.production,
		&authority->view);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	if (publication_out)
		*publication_out = sg_host_law_owner.production;
	return Ok();
}

sg_host_law_result_t SG_HostLawProductionAcquire(
	sg_host_law_runtime_authority_t *authority_out)
{
	sg_host_law_result_t result;

	if (!authority_out)
		return Result(SG_HOST_LAW_INVALID_ARGUMENT,
			SG_HOST_LAW_FIELD_VERSION, 1U, 0U);
	memset(authority_out, 0, sizeof(*authority_out));
	result = SG_HostLawProductionRevalidate();
	if (result.status != SG_HOST_LAW_OK)
		return result;
	authority_out->version = SG_HOST_LAW_RUNTIME_AUTHORITY_VERSION;
	authority_out->epoch = sg_host_law_owner.epoch;
	authority_out->epoch_complement = ~sg_host_law_owner.epoch;
	result = SG_HostLawPublicationRead(sg_host_law_owner.production,
		&authority_out->view);
	if (result.status != SG_HOST_LAW_OK)
		memset(authority_out, 0, sizeof(*authority_out));
	return result;
}

sg_host_law_result_t SG_HostLawProductionAuthorityCurrent(
	const sg_host_law_runtime_authority_t *authority)
{
	return RuntimeAuthorityState(authority, NULL);
}

sg_host_law_result_t SG_HostLawProductionSubject(
	const sg_host_law_runtime_authority_t *authority, uint32_t subject_index,
	sg_host_law_subject_t *subject_out)
{
	const sg_host_law_publication_t *publication;
	sg_host_law_result_t result = RuntimeAuthorityState(authority,
		&publication);

	if (result.status != SG_HOST_LAW_OK)
		return result;
	return SG_HostLawPublicationOwnerSubject(publication,
		subject_index, subject_out);
}

sg_host_law_result_t SG_HostLawProductionSubjectCurrent(
	const sg_host_law_runtime_authority_t *authority,
	const sg_host_law_subject_t *subject)
{
	const sg_host_law_publication_t *publication;
	sg_host_law_result_t result = RuntimeAuthorityState(authority,
		&publication);

	if (result.status != SG_HOST_LAW_OK)
		return result;
	return SG_HostLawPublicationOwnerSubjectCurrent(
		publication, subject);
}

sg_host_law_result_t SG_HostLawProductionSubjectState(
	const sg_host_law_runtime_authority_t *authority,
	const sg_host_law_subject_t *subject,
	sg_host_pmove_state_observation_t *observation_out)
{
	const sg_host_law_publication_t *publication;
	sg_host_law_result_t result = RuntimeAuthorityState(authority,
		&publication);

	if (result.status != SG_HOST_LAW_OK)
		return result;
	return SG_HostLawPublicationOwnerSubjectState(publication, subject,
		observation_out);
}

sg_host_law_result_t SG_HostLawProductionReplayFrame(
	const sg_host_law_runtime_authority_t *authority,
	const sg_host_law_subject_t *subject,
	const sg_host_pmove_request_t *request,
	const sg_host_pmove_replay_workspace_t *workspace,
	sg_host_pmove_replay_t *replay_out, sg_host_pmove_error_t *error_out)
{
	const sg_host_law_publication_t *publication;
	sg_host_law_result_t result = RuntimeAuthorityState(authority,
		&publication);

	if (result.status != SG_HOST_LAW_OK)
		return result;
	return SG_HostLawPublicationOwnerReplayFrame(publication,
		subject, request, workspace, replay_out, error_out);
}

sg_host_law_result_t SG_HostLawProductionSubjectTrace(
	const sg_host_law_runtime_authority_t *authority,
	const sg_host_law_subject_t *subject, const float start[3],
	const float mins[3], const float maxs[3], const float end[3],
	sg_host_collision_contents_t mask,
	sg_host_collision_trace_t *trace_out)
{
	const sg_host_law_publication_t *publication;
	sg_host_law_result_t result = RuntimeAuthorityState(authority,
		&publication);

	if (result.status != SG_HOST_LAW_OK)
		return result;
	return SG_HostLawPublicationOwnerSubjectTrace(
		publication, subject, start, mins, maxs, end, mask,
		trace_out);
}

sg_host_law_result_t SG_HostLawProductionSubjectPointContents(
	const sg_host_law_runtime_authority_t *authority,
	const sg_host_law_subject_t *subject, const float point[3],
	sg_host_collision_contents_t *contents_out)
{
	const sg_host_law_publication_t *publication;
	sg_host_law_result_t result = RuntimeAuthorityState(authority,
		&publication);

	if (result.status != SG_HOST_LAW_OK)
		return result;
	return SG_HostLawPublicationOwnerSubjectPointContents(
		publication, subject, point, contents_out);
}

sg_host_law_result_t SG_HostLawProductionSubjectClassifyPose(
	const sg_host_law_runtime_authority_t *authority,
	const sg_host_law_subject_t *subject, const float origin[3],
	sg_rune_stance_t stance, sg_host_collision_pose_t *pose_out)
{
	const sg_host_law_publication_t *publication;
	sg_host_law_result_t result = RuntimeAuthorityState(authority,
		&publication);

	if (result.status != SG_HOST_LAW_OK)
		return result;
	return SG_HostLawPublicationOwnerSubjectClassifyPose(
		publication, subject, origin, stance, pose_out);
}

sg_host_law_result_t SG_HostLawProductionSubjectTransition(
	const sg_host_law_runtime_authority_t *authority,
	const sg_host_law_subject_t *subject, const float start[3],
	const float end[3], sg_rune_stance_t stance,
	sg_host_collision_transition_t *transition_out)
{
	const sg_host_law_publication_t *publication;
	sg_host_law_result_t result = RuntimeAuthorityState(authority,
		&publication);

	if (result.status != SG_HOST_LAW_OK)
		return result;
	return SG_HostLawPublicationOwnerSubjectTransition(
		publication, subject, start, end, stance,
		transition_out);
}

sg_host_law_result_t SG_HostLawProductionHookFire(uint32_t subject_index,
	uint32_t hook_index, sg_host_hook_step_t *step_out)
{
	sg_host_law_result_t result = SG_HostLawProductionRevalidate();

	if (result.status != SG_HOST_LAW_OK)
		return result;
	return SG_HostLawPublicationOwnerHookFire(sg_host_law_owner.production,
		subject_index, hook_index, step_out);
}

sg_host_law_result_t SG_HostLawProductionHookTouch(uint32_t subject_index,
	uint32_t hook_index, uint32_t target_index, int32_t surface_flags,
	sg_host_hook_step_t *step_out)
{
	sg_host_law_result_t result = SG_HostLawProductionRevalidate();

	if (result.status != SG_HOST_LAW_OK)
		return result;
	return SG_HostLawPublicationOwnerHookTouch(sg_host_law_owner.production,
		subject_index, hook_index, target_index, surface_flags, step_out);
}

sg_host_law_result_t SG_HostLawProductionHookPullVelocity(
	uint32_t subject_index, uint32_t hook_index, vec3_t velocity,
	int *rope_length_out)
{
	sg_host_law_result_t result = SG_HostLawProductionRevalidate();

	if (result.status != SG_HOST_LAW_OK)
		return result;
	return SG_HostLawPublicationOwnerHookPullVelocity(
		sg_host_law_owner.production, subject_index, hook_index, velocity,
		rope_length_out);
}

void SG_HostLawProductionReset(void)
{
	DestroyState(&sg_host_law_owner);
}

sg_host_law_result_t SG_HostLawProductionRevalidate(void)
{
	sg_host_law_result_t result = RevalidateState(&sg_host_law_owner);

	if (result.status != SG_HOST_LAW_OK)
	{
		DestroyState(&sg_host_law_owner);
		if (result.status == SG_HOST_LAW_CORRUPT_PUBLICATION)
			result.status = SG_HOST_LAW_PRODUCTION_DRIFT;
	}
	return result;
}

const sg_host_law_publication_t *SG_HostLawProductionPublication(void)
{
	return sg_host_law_owner.production;
}

const sg_host_law_publication_t *SG_HostLawProductionStaticPublication(void)
{
	return sg_host_law_owner.construction;
}

sg_host_law_result_t SG_HostLawProductionConstructionIssue(
	const sg_host_collision_authority_t *authority,
	sg_host_law_construction_t **construction_out)
{
	sg_host_law_result_t result = SG_HostLawProductionRevalidate();

	if (result.status != SG_HOST_LAW_OK)
		return result;
	return SG_HostLawPublicationOwnerConstructionIssue(
		sg_host_law_owner.construction, authority, construction_out);
}

sg_host_law_result_t SG_HostLawProductionCollisionAuthority(
	const sg_host_collision_authority_t **authority_out)
{
	if (!authority_out)
		return Result(SG_HOST_LAW_INVALID_ARGUMENT,
			SG_HOST_LAW_FIELD_COLLISION_LAW, 1U, 0U);
	*authority_out = NULL;
	if (!sg_host_law_owner.construction)
		return HostUnavailable();
	return SG_HostLawPublicationCollisionAuthority(
		sg_host_law_owner.construction, authority_out);
}

sg_host_law_result_t SG_HostLawProductionEngineTrace(uint32_t subject_index,
	const float start[3], const float mins[3], const float maxs[3],
	const float end[3], sg_host_collision_contents_t mask,
	sg_host_collision_trace_t *trace_out)
{
	sg_host_law_result_t result = SG_HostLawProductionRevalidate();

	if (result.status != SG_HOST_LAW_OK)
		return result;
	return SG_HostLawPublicationOwnerEngineTrace(sg_host_law_owner.production,
		subject_index, start, mins, maxs, end, mask, trace_out);
}
