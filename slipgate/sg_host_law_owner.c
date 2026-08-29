#include "../g_local.h"
#undef world

#include "sg_host_law_owner.h"
#include "sg_host_law_owner_internal.h"
#include "sg_host_law_publication_private.h"
#include "sg_host_engine_runtime.h"
#include "sg_host_engine_runtime_private.h"
#include "sg_identity.h"

#include <string.h>

/* This is the sole production owner for upstream A.  It publishes exact BSP
 * and host law while retaining the live callback epoch.  Runtime B remains
 * NULL until the downstream cutover supplies an opaque acceptance capability. */
static sg_host_law_publication_t *sg_host_law_production;
static sg_host_law_publication_t *sg_host_law_production_construction;
static sg_host_engine_runtime_t *sg_host_law_production_runtime;
static char sg_host_law_production_map[SG_LEVEL_IDENTITY_MAPNAME_BYTES];

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

static sg_host_law_result_t HostUnavailable(void)
{
	return Result(SG_HOST_LAW_HOST_UNAVAILABLE, SG_HOST_LAW_FIELD_PMOVE_ABI,
		1U, 0U);
}

static void OwnerClear(void)
{
	if (sg_host_law_production)
		SG_HostLawPublicationOwnerDestroy(sg_host_law_production);
	if (sg_host_law_production_construction)
		SG_HostLawPublicationOwnerDestroy(sg_host_law_production_construction);
	if (sg_host_law_production_runtime)
		SG_HostEngineRuntimeDestroy(sg_host_law_production_runtime);
	sg_host_law_production = NULL;
	sg_host_law_production_construction = NULL;
	sg_host_law_production_runtime = NULL;
	memset(sg_host_law_production_map, 0,
		sizeof(sg_host_law_production_map));
}

static int BuildStaticIdentity(const sg_level_identity_t *level_identity,
	const sg_bsp_content_identity_t *bsp_identity,
	sg_host_static_identity_t *identity_out)
{
	if (!level_identity || !bsp_identity || !identity_out)
		return 0;
	memset(identity_out, 0, sizeof(*identity_out));
	identity_out->bsp_identity = *bsp_identity;
	identity_out->bsp_bytes = level_identity->bsp_bytes;
	identity_out->engine_checksum = level_identity->bsp_checksum;
	identity_out->entity_crc32 = level_identity->entity_crc32;
	identity_out->host_physics_epoch = level_identity->host_physics_id;
	identity_out->physics_abi_id = SG_HOST_ENGINE_PMOVE_ABI_ID;
	if (!SG_HostEngineHullProfiles(&identity_out->standing_hull,
		&identity_out->crouching_hull) ||
		!SG_HostEnginePhysicsLaw(&identity_out->physics))
		return 0;
	return 1;
}

sg_host_law_result_t SG_HostLawProductionBeginLevel(const char *mapname)
{
	sg_level_identity_t level_identity;
	sg_host_engine_runtime_t *runtime = NULL;
	sg_bsp_content_identity_t bsp_identity;
	sg_host_static_identity_t identity;
	sg_host_engine_runtime_status_t runtime_status;
	sg_host_law_publication_t *construction = NULL;
	sg_host_law_result_t result;

	OwnerClear();
	if (SG_LevelIdentitySnapshot(mapname, &level_identity) != SG_IDENTITY_OK)
		return Result(SG_HOST_LAW_HOST_UNAVAILABLE,
			SG_HOST_LAW_FIELD_BSP_CONTENT, 1U, 0U);
	runtime_status = SG_HostEngineRuntimeBegin(mapname, &runtime);
	if (runtime_status != SG_HOST_ENGINE_RUNTIME_OK)
		return Result(SG_HOST_LAW_HOST_UNAVAILABLE, SG_HOST_LAW_FIELD_PMOVE_ABI,
			1U, (uint64_t)runtime_status);
	memcpy(bsp_identity.bytes, level_identity.bsp_sha256,
		sizeof(bsp_identity.bytes));
	if (level_identity.bsp_bytes == 0U ||
		!BuildStaticIdentity(&level_identity, &bsp_identity, &identity))
	{
		SG_HostEngineRuntimeDestroy(runtime);
		return HostUnavailable();
	}
	result = SG_HostLawPublicationOwnerIssueStatic(&identity, &construction);
	if (result.status != SG_HOST_LAW_OK)
	{
		SG_HostEngineRuntimeDestroy(runtime);
		return result;
	}
	/* Publish A atomically.  It is complete before any accepted artifact
	 * generation exists and is the only input exposed to complete-model seal. */
	sg_host_law_production_runtime = runtime;
	sg_host_law_production_construction = construction;
	memcpy(sg_host_law_production_map, level_identity.mapname,
		sizeof(sg_host_law_production_map));
	return Result(SG_HOST_LAW_OK, SG_HOST_LAW_FIELD_NONE, 0U, 0U);
}

sg_host_law_result_t SG_HostLawProductionEnsureLevel(const char *mapname)
{
	sg_host_law_result_t result;

	if (!mapname || !mapname[0])
		return Result(SG_HOST_LAW_INVALID_ARGUMENT,
			SG_HOST_LAW_FIELD_BSP_CONTENT, 1U, 0U);
	if (sg_host_law_production_construction &&
		strcmp(sg_host_law_production_map, mapname) == 0 &&
		SG_HostEngineRuntimeCurrent(sg_host_law_production_runtime))
	{
		result = SG_HostLawPublicationRevalidateProduction(
			sg_host_law_production_construction);
		if (result.status == SG_HOST_LAW_OK)
			return result;
	}
	return SG_HostLawProductionBeginLevel(mapname);
}

sg_host_law_result_t SG_HostLawProductionBindActiveSubject(uint32_t subject_index)
{
	sg_host_engine_runtime_status_t runtime_status;

	if (!sg_host_law_production_runtime)
		return HostUnavailable();
	runtime_status = SG_HostEngineRuntimeOwnerBindActiveSubject(
		sg_host_law_production_runtime, subject_index);
	if (runtime_status != SG_HOST_ENGINE_RUNTIME_OK)
		return Result(SG_HOST_LAW_HOST_UNAVAILABLE,
			SG_HOST_LAW_FIELD_COLLISION_LAW, 1U, (uint64_t)runtime_status);
	return Result(SG_HOST_LAW_OK, SG_HOST_LAW_FIELD_NONE, 0U, 0U);
}

sg_host_law_result_t SG_HostLawProductionPmove(uint32_t subject_index,
	const sg_host_pmove_request_t *request, sg_host_pmove_result_t *result_out,
	sg_host_pmove_error_t *error_out)
{
	sg_host_law_result_t result = SG_HostLawProductionRevalidate();

	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = SG_HostLawProductionBindActiveSubject(subject_index);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	return SG_HostLawPublicationPmove(sg_host_law_production, NULL, request,
		result_out, error_out);
}

sg_host_law_result_t SG_HostLawProductionHookFire(uint32_t subject_index,
	const sg_host_hook_fire_request_t *request,
	sg_host_hook_step_t *step_out)
{
	sg_host_law_result_t result = SG_HostLawProductionRevalidate();

	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = SG_HostLawProductionBindActiveSubject(subject_index);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	return SG_HostLawPublicationHookFire(sg_host_law_production, NULL,
		request, step_out);
}

sg_host_law_result_t SG_HostLawProductionHookTouch(uint32_t subject_index,
	uint32_t target_index, int32_t surface_flags, int attached, uint32_t frame,
	uint32_t last_damage_frame, sg_host_hook_step_t *step_out)
{
	sg_host_law_result_t result = SG_HostLawProductionRevalidate();

	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = SG_HostLawProductionBindActiveSubject(subject_index);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	return SG_HostLawPublicationOwnerHookTouch(sg_host_law_production,
		target_index, surface_flags, attached, frame, last_damage_frame,
		step_out);
}

sg_host_law_result_t SG_HostLawProductionHookPullVelocity(
	uint32_t subject_index, const vec3_t start, const vec3_t bite,
	vec3_t velocity, int *rope_length_out)
{
	sg_host_law_result_t result = SG_HostLawProductionRevalidate();

	if (result.status != SG_HOST_LAW_OK)
		return result;
	result = SG_HostLawProductionBindActiveSubject(subject_index);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	return SG_HostLawPublicationHookPullVelocity(sg_host_law_production,
		start, bite, velocity, rope_length_out);
}

void SG_HostLawProductionReset(void)
{
	OwnerClear();
}

sg_host_law_result_t SG_HostLawProductionRevalidate(void)
{
	sg_host_law_result_t result;

	if (!sg_host_law_production || !sg_host_law_production_construction)
		return HostUnavailable();
	result = SG_HostLawPublicationRevalidateProduction(sg_host_law_production);
	if (result.status != SG_HOST_LAW_OK)
	{
		SG_HostLawProductionReset();
		if (result.status == SG_HOST_LAW_CORRUPT_PUBLICATION)
			result.status = SG_HOST_LAW_PRODUCTION_DRIFT;
		return result;
	}
	result = SG_HostLawPublicationRevalidateProduction(
		sg_host_law_production_construction);
	if (result.status != SG_HOST_LAW_OK)
	{
		SG_HostLawProductionReset();
		if (result.status == SG_HOST_LAW_CORRUPT_PUBLICATION)
			result.status = SG_HOST_LAW_PRODUCTION_DRIFT;
		return result;
	}
	return result;
}

const sg_host_law_publication_t *SG_HostLawProductionPublication(void)
{
	return sg_host_law_production;
}

const sg_host_law_publication_t *SG_HostLawProductionStaticPublication(void)
{
	return sg_host_law_production_construction;
}

sg_host_law_result_t SG_HostLawProductionCollisionAuthority(
	const sg_host_collision_authority_t **authority_out)
{
	if (!authority_out)
		return Result(SG_HOST_LAW_INVALID_ARGUMENT,
			SG_HOST_LAW_FIELD_COLLISION_LAW, 1U, 0U);
	*authority_out = NULL;
	if (!sg_host_law_production_construction)
		return HostUnavailable();
	return SG_HostLawPublicationCollisionAuthority(
		sg_host_law_production_construction, authority_out);
}

sg_host_law_result_t SG_HostLawProductionEngineTrace(
	const float start[3], const float mins[3], const float maxs[3],
	const float end[3], sg_host_collision_contents_t mask,
	sg_host_collision_trace_t *trace_out)
{
	if (!sg_host_law_production)
		return HostUnavailable();
	return SG_HostLawPublicationEngineTrace(sg_host_law_production, start,
		mins, maxs, end, mask, trace_out);
}

sg_host_law_result_t SG_HostLawProductionEnginePointContents(
	const float point[3], sg_host_collision_contents_t *contents_out)
{
	if (!sg_host_law_production)
		return HostUnavailable();
	return SG_HostLawPublicationEnginePointContents(sg_host_law_production,
		point, contents_out);
}
