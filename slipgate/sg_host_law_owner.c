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
	char mapname[SG_LEVEL_IDENTITY_MAPNAME_BYTES];
} sg_host_law_owner_state_t;

static sg_host_law_owner_state_t sg_host_law_owner;

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
	result = RevalidateState(&next);
	if (result.status != SG_HOST_LAW_OK)
	{
		DestroyState(&next);
		return result;
	}
	previous = sg_host_law_owner;
	sg_host_law_owner = next;
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
