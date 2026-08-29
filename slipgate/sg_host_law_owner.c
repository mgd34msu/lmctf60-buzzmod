#include "../g_local.h"
#include "sg_host_law_owner.h"
#include "sg_hooks.h"
#include "sg_identity.h"

#ifdef world
#undef world
#endif

#include <math.h>
#include <stdio.h>
#include <string.h>

static sg_host_law_publication_t *sg_host_law_production;
static sg_bsp_world_t *sg_host_law_production_world;

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

static sg_host_law_result_t OwnerResult(sg_host_law_status_t status,
	sg_host_law_field_t field, uint64_t observed)
{
	sg_host_law_result_t result;

	memset(&result, 0, sizeof(result));
	result.status = status;
	result.field = field;
	result.element = SG_HOST_LAW_ELEMENT_NONE;
	result.expected_bits = 1U;
	result.observed_bits = observed;
	return result;
}

static void OwnerClear(void)
{
	if (sg_host_law_production)
		SG_HostLawPublicationDestroy(sg_host_law_production);
	if (sg_host_law_production_world)
		SG_BspWorldDestroy(sg_host_law_production_world);
	sg_host_law_production = NULL;
	sg_host_law_production_world = NULL;
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

static uint64_t LevelIdentityPart(uint32_t high, uint32_t low)
{
	uint64_t value = ((uint64_t)high << 32) | (uint64_t)low;

	return value == 0U || value == UINT64_MAX ? value ^ UINT64_C(1) : value;
}

static int BuildLevelIdentity(const sg_level_identity_t *level_identity,
	sg_rune_model_identity_t *identity_out)
{
	cvar_t *airaccelerate;

	if (!level_identity || !identity_out || !sg_host.cvar || !sv_gravity ||
		!sv_maxvelocity)
		return 0;
	airaccelerate = sg_host.cvar("sv_airaccelerate", "0", 0);
	if (!airaccelerate || !isfinite(sv_gravity->value) ||
		!isfinite(sv_maxvelocity->value) || !isfinite(airaccelerate->value))
		return 0;
	memset(identity_out, 0, sizeof(*identity_out));
	identity_out->bsp_content_id =
		LevelIdentityPart(level_identity->bsp_checksum,
			level_identity->bsp_checksum ^ UINT32_C(0x42535031));
	identity_out->entity_semantics_id =
		LevelIdentityPart(level_identity->entity_crc32,
			level_identity->entity_crc32 ^ UINT32_C(0x454e5431));
	identity_out->physics_abi_id = SG_HOST_ENGINE_PMOVE_ABI_ID;
	identity_out->source_set_identity = LevelIdentityPart(
		level_identity->bsp_checksum ^ level_identity->entity_crc32,
		level_identity->host_physics_id ^ UINT32_C(0x53524331));
	identity_out->schema_id = (uint64_t)SG_RUNE_MODEL_SCHEMA_TAG;
	identity_out->producer_identity = UINT64_C(0x5347484f53544c31);
	identity_out->standing_hull.mins.value[0] = -16.0f;
	identity_out->standing_hull.mins.value[1] = -16.0f;
	identity_out->standing_hull.mins.value[2] = -24.0f;
	identity_out->standing_hull.maxs.value[0] = 16.0f;
	identity_out->standing_hull.maxs.value[1] = 16.0f;
	identity_out->standing_hull.maxs.value[2] = 32.0f;
	identity_out->crouching_hull.mins = identity_out->standing_hull.mins;
	identity_out->crouching_hull.maxs = identity_out->standing_hull.maxs;
	identity_out->crouching_hull.maxs.value[2] = 4.0f;
	identity_out->physics.gravity = sv_gravity->value;
	identity_out->physics.ground_acceleration = 10.0f;
	identity_out->physics.air_acceleration = 1.0f;
	identity_out->physics.water_acceleration = 10.0f;
	identity_out->physics.hook_acceleration = 800.0f;
	identity_out->physics.external_acceleration = 1.0f;
	identity_out->physics.water_drag = 1.0f;
	identity_out->physics.max_velocity = sv_maxvelocity->value;
	identity_out->physics.frame_ms = SG_HOST_ENGINE_FRAME_MS;
	identity_out->physics.substep_ms = SG_HOST_ENGINE_PMOVE_SUBSTEP_MS;
	return 1;
}

sg_host_law_result_t SG_HostLawProductionInstallLevel(const char *mapname)
{
	sg_level_identity_t level_identity;
	sg_rune_model_identity_t identity;
	sg_host_collision_authority_t authority;
	sg_host_collision_error_t collision_error;
	sg_bsp_world_t *world = NULL;
	sg_bsp_error_t bsp_error;
	sg_host_law_publication_t *publication = NULL;
	sg_host_law_result_t result;
	cvar_t *game_directory_cvar;
	const char *game_directory;
	char path[1024];
	int path_length;

	if (!mapname || SG_LevelIdentitySnapshot(mapname, &level_identity) !=
		SG_IDENTITY_OK || !BuildLevelIdentity(&level_identity, &identity))
		return HostUnavailable();
	game_directory_cvar = sg_host.cvar ? sg_host.cvar("gamedir", "", 0) : NULL;
	game_directory = game_directory_cvar && game_directory_cvar->string &&
		game_directory_cvar->string[0] ? game_directory_cvar->string : ".";
	path_length = snprintf(path, sizeof(path), "%s/maps/%s.bsp", game_directory,
		mapname);
	if (path_length < 0 || (size_t)path_length >= sizeof(path))
		return HostUnavailable();
	memset(&bsp_error, 0, sizeof(bsp_error));
	if (!SG_BspWorldLoadFile(path, &world, &bsp_error))
		return OwnerResult(SG_HOST_LAW_HOST_UNAVAILABLE,
			SG_HOST_LAW_FIELD_BSP_CONTENT, (uint64_t)bsp_error.code);
	if (!SG_HostCollisionInit(&authority, world, &identity, &collision_error))
	{
		SG_BspWorldDestroy(world);
		return OwnerResult(SG_HOST_LAW_INVALID_ARGUMENT,
			SG_HOST_LAW_FIELD_COLLISION_LAW, (uint64_t)collision_error);
	}
	result = SG_HostLawPublicationIssue(&authority, &publication);
	if (result.status != SG_HOST_LAW_OK)
	{
		SG_BspWorldDestroy(world);
		return result;
	}
	OwnerClear();
	sg_host_law_production = publication;
	sg_host_law_production_world = world;
	return OwnerResult(SG_HOST_LAW_OK, SG_HOST_LAW_FIELD_NONE, 0U);
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
