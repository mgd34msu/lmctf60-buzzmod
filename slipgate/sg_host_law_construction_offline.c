#include "sg_host_law_construction_offline.h"
#include "sg_host_law_publication_private.h"

#include <stdlib.h>
#include <string.h>

typedef struct sg_host_law_offline_authority_s
{
	sg_bsp_world_t *world;
	sg_host_collision_authority_t collision;
} sg_host_law_offline_authority_t;

static sg_host_law_result_t OfflineResult(sg_host_law_status_t status,
	sg_host_law_field_t field, uint32_t element, uint64_t expected,
	uint64_t observed)
{
	sg_host_law_result_t result = { status, field, element, 0U, expected,
		observed };

	return result;
}

static sg_host_law_result_t OfflineOk(void)
{
	return OfflineResult(SG_HOST_LAW_OK, SG_HOST_LAW_FIELD_NONE,
		SG_HOST_LAW_ELEMENT_NONE, 0U, 0U);
}

static int OfflineStaticTermsMatch(
	const sg_host_static_identity_t *host,
	const sg_rune_model_identity_t *downstream)
{
	return host && downstream &&
		downstream->physics_abi_id == host->physics_abi_id &&
		memcmp(&downstream->standing_hull, &host->standing_hull,
			sizeof(downstream->standing_hull)) == 0 &&
		memcmp(&downstream->crouching_hull, &host->crouching_hull,
			sizeof(downstream->crouching_hull)) == 0 &&
		memcmp(&downstream->physics, &host->physics,
			sizeof(downstream->physics)) == 0;
}

static sg_host_law_result_t OfflineAuthorityLoad(
	const sg_host_law_construction_t *construction,
	const sg_rune_model_identity_t *downstream_identity,
	sg_host_law_offline_authority_t *authority_out)
{
	sg_host_static_identity_t static_identity;
	sg_bsp_error_t bsp_error;
	sg_host_collision_error_t collision_error;
	sg_host_law_result_t result;
	uint8_t *bytes = NULL;
	size_t bytes_required = 0U;
	size_t bytes_copied = 0U;

	if (!downstream_identity || !authority_out)
		return OfflineResult(SG_HOST_LAW_INVALID_ARGUMENT,
			SG_HOST_LAW_FIELD_COLLISION_LAW, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	memset(authority_out, 0, sizeof(*authority_out));
	memset(&static_identity, 0, sizeof(static_identity));
	result = SG_HostLawConstructionOwnerCopyBsp(construction, NULL, 0U,
		&bytes_required, NULL);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	if (bytes_required == 0U)
		return OfflineResult(SG_HOST_LAW_CORRUPT_PUBLICATION,
			SG_HOST_LAW_FIELD_BSP_CONTENT, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	bytes = malloc(bytes_required);
	if (!bytes)
		return OfflineResult(SG_HOST_LAW_ALLOCATION_FAILED,
			SG_HOST_LAW_FIELD_BSP_CONTENT, SG_HOST_LAW_ELEMENT_NONE,
			(uint64_t)bytes_required, 0U);
	result = SG_HostLawConstructionOwnerCopyBsp(construction, bytes,
		bytes_required, &bytes_copied, &static_identity);
	if (result.status != SG_HOST_LAW_OK)
		goto done;
	if (bytes_copied != bytes_required ||
		(uint64_t)bytes_copied != static_identity.bsp_bytes)
	{
		result = OfflineResult(SG_HOST_LAW_CORRUPT_PUBLICATION,
			SG_HOST_LAW_FIELD_BSP_CONTENT, SG_HOST_LAW_ELEMENT_NONE,
			static_identity.bsp_bytes, (uint64_t)bytes_copied);
		goto done;
	}
	memset(&bsp_error, 0, sizeof(bsp_error));
	if (!SG_BspWorldLoadMemory(bytes, bytes_copied, &authority_out->world,
			&bsp_error))
	{
		result = OfflineResult(SG_HOST_LAW_CORRUPT_PUBLICATION,
			SG_HOST_LAW_FIELD_BSP_CONTENT, bsp_error.record,
			SG_BSP_ERROR_NONE, (uint64_t)bsp_error.code);
		goto done;
	}
	if (memcmp(authority_out->world->content_identity.bytes,
			static_identity.bsp_identity.bytes,
			SG_BSP_CONTENT_ID_BYTES) != 0 ||
		authority_out->world->engine_checksum !=
			static_identity.engine_checksum ||
		!SG_BspWorldSourceIdentityCurrent(authority_out->world))
	{
		result = OfflineResult(SG_HOST_LAW_CORRUPT_PUBLICATION,
			SG_HOST_LAW_FIELD_BSP_CONTENT, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
		goto done;
	}
	if (!OfflineStaticTermsMatch(&static_identity, downstream_identity))
	{
		result = OfflineResult(SG_HOST_LAW_EVALUATION_FAILED,
			SG_HOST_LAW_FIELD_PHYSICS_ABI, SG_HOST_LAW_ELEMENT_NONE,
			static_identity.physics_abi_id, downstream_identity->physics_abi_id);
		goto done;
	}
	memset(&collision_error, 0, sizeof(collision_error));
	if (!SG_HostCollisionInit(&authority_out->collision, authority_out->world,
			downstream_identity, &collision_error))
	{
		result = OfflineResult(SG_HOST_LAW_EVALUATION_FAILED,
			SG_HOST_LAW_FIELD_COLLISION_LAW, SG_HOST_LAW_ELEMENT_NONE,
			SG_HOST_COLLISION_ERROR_NONE, (uint64_t)collision_error);
		goto done;
	}
	result = OfflineOk();

done:
	free(bytes);
	if (result.status != SG_HOST_LAW_OK)
	{
		SG_BspWorldDestroy(authority_out->world);
		memset(authority_out, 0, sizeof(*authority_out));
	}
	return result;
}

static void OfflineAuthorityDestroy(
	sg_host_law_offline_authority_t *authority)
{
	if (!authority)
		return;
	SG_BspWorldDestroy(authority->world);
	memset(authority, 0, sizeof(*authority));
}

sg_host_law_result_t SG_HostLawConstructionConfigurationAudit(
	const sg_host_law_construction_t *construction,
	const sg_configuration_space_t *configuration,
	sg_configuration_audit_result_t *audit_out)
{
	sg_host_law_offline_authority_t authority;
	sg_host_law_result_t result;

	if (!configuration || !audit_out)
		return OfflineResult(SG_HOST_LAW_INVALID_ARGUMENT,
			SG_HOST_LAW_FIELD_COLLISION_LAW, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	memset(audit_out, 0, sizeof(*audit_out));
	result = OfflineAuthorityLoad(construction, &configuration->identity,
		&authority);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	if (!SG_ConfigurationAudit(&authority.collision, configuration, audit_out))
		result = OfflineResult(SG_HOST_LAW_EVALUATION_FAILED,
			SG_HOST_LAW_FIELD_COLLISION_LAW, audit_out->record,
			SG_CONFIGURATION_AUDIT_OK, (uint64_t)audit_out->code);
	OfflineAuthorityDestroy(&authority);
	return result;
}

sg_host_law_result_t SG_HostLawConstructionSemanticsAudit(
	const sg_host_law_construction_t *construction,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	sg_configuration_semantics_audit_result_t *audit_out)
{
	sg_host_law_offline_authority_t authority;
	sg_host_law_result_t result;

	if (!configuration || !semantics || !audit_out)
		return OfflineResult(SG_HOST_LAW_INVALID_ARGUMENT,
			SG_HOST_LAW_FIELD_COLLISION_LAW, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	memset(audit_out, 0, sizeof(*audit_out));
	result = OfflineAuthorityLoad(construction, &configuration->identity,
		&authority);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	if (!SG_ConfigurationSemanticsAudit(&authority.collision, configuration,
			semantics, audit_out))
		result = OfflineResult(SG_HOST_LAW_EVALUATION_FAILED,
			SG_HOST_LAW_FIELD_COLLISION_LAW, audit_out->record,
			SG_CONFIGURATION_SEMANTICS_AUDIT_OK,
			(uint64_t)audit_out->code);
	OfflineAuthorityDestroy(&authority);
	return result;
}

sg_host_law_result_t SG_HostLawConstructionCompletenessProve(
	const sg_host_law_construction_t *construction,
	const sg_configuration_space_t *configuration,
	sg_bsp_completeness_result_t *proof_out)
{
	sg_host_law_offline_authority_t authority;
	sg_host_law_result_t result;

	if (!configuration || !proof_out)
		return OfflineResult(SG_HOST_LAW_INVALID_ARGUMENT,
			SG_HOST_LAW_FIELD_COLLISION_LAW, SG_HOST_LAW_ELEMENT_NONE, 1U, 0U);
	memset(proof_out, 0, sizeof(*proof_out));
	result = OfflineAuthorityLoad(construction, &configuration->identity,
		&authority);
	if (result.status != SG_HOST_LAW_OK)
		return result;
	if (!SG_BspCompletenessProve(&authority.collision, configuration, proof_out))
		result = OfflineResult(SG_HOST_LAW_EVALUATION_FAILED,
			SG_HOST_LAW_FIELD_COLLISION_LAW, proof_out->record,
			SG_BSP_COMPLETENESS_OK, (uint64_t)proof_out->code);
	OfflineAuthorityDestroy(&authority);
	return result;
}
