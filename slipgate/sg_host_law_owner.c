#include "sg_host_law_owner.h"
#include "sg_host_engine_runtime_private.h"
#include "sg_identity.h"

#include <string.h>

static sg_host_law_publication_t *sg_host_law_production;
static sg_bsp_world_t *sg_host_law_production_world;
static sg_host_engine_runtime_t *sg_host_law_production_runtime;

static sg_host_law_result_t HostUnavailable(void)
{
	sg_host_law_result_t result;

	memset(&result, 0, sizeof(result));
	result.status = SG_HOST_LAW_HOST_UNAVAILABLE;
	result.field = SG_HOST_LAW_FIELD_PMOVE_ABI;
	result.element = SG_HOST_LAW_ELEMENT_NONE;
	result.expected_bits = 1U;
	return result;
}

static sg_host_law_result_t OwnerMismatch(sg_host_law_status_t status,
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

static sg_host_law_result_t HostUnavailableField(sg_host_law_field_t field,
	uint64_t expected, uint64_t observed)
{
	return OwnerMismatch(SG_HOST_LAW_HOST_UNAVAILABLE, field, expected,
		observed);
}

static void OwnerClear(void)
{
	if (sg_host_law_production)
		SG_HostLawPublicationDestroy(sg_host_law_production);
	if (sg_host_law_production_world)
		SG_BspWorldDestroy(sg_host_law_production_world);
	if (sg_host_law_production_runtime)
		SG_HostEngineRuntimeDestroy(sg_host_law_production_runtime);
	sg_host_law_production = NULL;
	sg_host_law_production_world = NULL;
	sg_host_law_production_runtime = NULL;
}

sg_host_law_result_t SG_HostLawProductionInstall(
	const sg_host_collision_authority_t *authority)
{
	sg_host_law_publication_t *publication = NULL;
	sg_host_law_result_t result;

	result = SG_HostLawPublicationIssue(authority, &publication);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	OwnerClear();
	sg_host_law_production = publication;
	return result;
}

sg_host_law_result_t SG_HostLawProductionBeginLevel(const char *mapname)
{
	sg_host_engine_runtime_status_t runtime_status;

	OwnerClear();
	runtime_status = SG_HostEngineRuntimeBegin(mapname,
		&sg_host_law_production_runtime);
	if (runtime_status == SG_HOST_ENGINE_RUNTIME_OK)
		return OwnerMismatch(SG_HOST_LAW_OK, SG_HOST_LAW_FIELD_NONE,
			0U, 0U);
	return HostUnavailableField(SG_HOST_LAW_FIELD_PMOVE_ABI, 1U,
		(uint64_t)runtime_status);
}

sg_host_law_result_t SG_HostLawProductionInstallAccepted(
	const sg_host_engine_runtime_acceptance_t *acceptance)
{
	sg_host_engine_runtime_status_t runtime_status;
	sg_host_law_publication_t *publication = NULL;
	sg_host_law_result_t result;

	if (!sg_host_law_production_runtime)
		return HostUnavailable();
	runtime_status = SG_HostEngineRuntimeJoinOwner(
		sg_host_law_production_runtime, SG_HostEngineRuntimeOwnerToken(),
		acceptance);
	if (runtime_status != SG_HOST_ENGINE_RUNTIME_OK)
	{
		OwnerClear();
		return HostUnavailableField(SG_HOST_LAW_FIELD_PMOVE_ABI, 1U,
			(uint64_t)runtime_status);
	}
	result = SG_HostLawPublicationIssueRuntime(
		sg_host_law_production_runtime, &publication);
	if (result.status != SG_HOST_LAW_OK)
	{
		OwnerClear();
		return result;
	}
	if (sg_host_law_production)
		SG_HostLawPublicationDestroy(sg_host_law_production);
	sg_host_law_production = publication;
	return result;
}

sg_host_law_result_t SG_HostLawProductionBindSubject(uint32_t subject_index)
{
	sg_host_engine_runtime_status_t runtime_status;

	if (!sg_host_law_production_runtime)
		return HostUnavailable();
	runtime_status = SG_HostEngineRuntimeBindSubjectOwner(
		sg_host_law_production_runtime, SG_HostEngineRuntimeOwnerToken(),
		subject_index);
	if (runtime_status != SG_HOST_ENGINE_RUNTIME_OK)
		return HostUnavailableField(SG_HOST_LAW_FIELD_COLLISION_LAW, 1U,
			(uint64_t)runtime_status);
	return OwnerMismatch(SG_HOST_LAW_OK, SG_HOST_LAW_FIELD_NONE, 0U, 0U);
}

sg_host_law_result_t SG_HostLawProductionInstallConstruction(
	const void *bsp_bytes, size_t bsp_size,
	const sg_bsp_content_identity_t *content_identity,
	const sg_rune_model_identity_t *identity)
{
	sg_bsp_world_t *world = NULL;
	sg_host_collision_authority_t authority;
	sg_host_collision_error_t collision_error;
	sg_bsp_error_t bsp_error;
	sg_host_law_publication_t *publication = NULL;
	sg_host_law_result_t result;

	if (!bsp_bytes || !bsp_size || !content_identity || !identity)
		return HostUnavailableField(SG_HOST_LAW_FIELD_BSP_CONTENT, 1U, 0U);
	memset(&bsp_error, 0, sizeof(bsp_error));
	if (!SG_BspWorldLoadMemory(bsp_bytes, bsp_size, &world, &bsp_error))
		return HostUnavailableField(SG_HOST_LAW_FIELD_BSP_CONTENT, 1U,
			(uint64_t)bsp_error.code);
	if (memcmp(&world->content_identity, content_identity,
		sizeof(*content_identity)) != 0)
	{
		SG_BspWorldDestroy(world);
		return OwnerMismatch(SG_HOST_LAW_PRODUCTION_DRIFT,
			SG_HOST_LAW_FIELD_BSP_CONTENT, 1U, 0U);
	}
	memset(&authority, 0, sizeof(authority));
	if (!SG_HostCollisionInit(&authority, world, identity, &collision_error))
	{
		SG_BspWorldDestroy(world);
		return HostUnavailableField(SG_HOST_LAW_FIELD_COLLISION_LAW, 1U,
			(uint64_t)collision_error);
	}
	result = SG_HostLawPublicationIssue(&authority, &publication);
	if (result.status != SG_HOST_LAW_OK)
	{
		SG_BspWorldDestroy(world);
		return result;
	}
	OwnerClear();
	sg_host_law_production_world = world;
	sg_host_law_production = publication;
	return result;
}

void SG_HostLawProductionReset(void)
{
	OwnerClear();
}

sg_host_law_result_t SG_HostLawProductionRevalidate(void)
{
	sg_host_law_result_t result;

	if (!sg_host_law_production)
		return HostUnavailable();
	result = SG_HostLawPublicationRevalidateProduction(sg_host_law_production);
	if (result.status != SG_HOST_LAW_OK)
	{
		SG_HostLawProductionReset();
		return result;
	}
	return result;
}

const sg_host_law_publication_t *SG_HostLawProductionPublication(void)
{
	return sg_host_law_production;
}

sg_host_law_result_t SG_HostLawProductionCollisionAuthority(
	const sg_host_collision_authority_t **authority_out)
{
	if (!authority_out)
	{
		sg_host_law_result_t result = HostUnavailable();

		result.status = SG_HOST_LAW_INVALID_ARGUMENT;
		result.field = SG_HOST_LAW_FIELD_COLLISION_LAW;
		return result;
	}
	*authority_out = NULL;
	if (!sg_host_law_production)
		return HostUnavailable();
	return SG_HostLawPublicationCollisionAuthority(sg_host_law_production,
		authority_out);
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
