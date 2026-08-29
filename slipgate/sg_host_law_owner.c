#include "../g_local.h"
#undef world

#include "sg_host_law_owner.h"
#include "sg_host_engine_runtime.h"
#include "sg_host_engine_runtime_private.h"
#include "sg_identity.h"
#include "sg_rune.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern rune_t *SG_Rune(void);

/* This is the sole production owner.  The runtime publication owns the live
 * engine callbacks; the construction publication owns the retained BSP.  A
 * caller can borrow either view, but cannot manufacture either owner object. */
static sg_host_law_publication_t *sg_host_law_production;
static sg_host_law_publication_t *sg_host_law_production_construction;
static sg_bsp_world_t *sg_host_law_production_world;
static sg_host_engine_runtime_t *sg_host_law_production_runtime;
static char sg_host_law_production_map[SG_LEVEL_IDENTITY_MAPNAME_BYTES];

#ifdef SG_HOST_LAW_TESTING
extern const sg_bsp_world_t *SG_HostLawTestRetainedWorld(void);
#endif

const sg_bsp_world_t *SG_HostLawOwnerRetainedWorld(void)
{
	if (sg_host_law_production_world)
		return sg_host_law_production_world;
#ifdef SG_HOST_LAW_TESTING
	const sg_bsp_world_t *test_world = SG_HostLawTestRetainedWorld();

	if (test_world)
		return test_world;
#endif
	return NULL;
}

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
		SG_HostLawPublicationDestroy(sg_host_law_production);
	if (sg_host_law_production_construction)
		SG_HostLawPublicationDestroy(sg_host_law_production_construction);
	if (sg_host_law_production_runtime)
		SG_HostEngineRuntimeOwnerClearAcceptance(
			sg_host_law_production_runtime);
	if (sg_host_law_production_world)
		SG_BspWorldDestroy(sg_host_law_production_world);
	if (sg_host_law_production_runtime)
		SG_HostEngineRuntimeDestroy(sg_host_law_production_runtime);
	sg_host_law_production = NULL;
	sg_host_law_production_construction = NULL;
	sg_host_law_production_world = NULL;
	sg_host_law_production_runtime = NULL;
	memset(sg_host_law_production_map, 0,
		sizeof(sg_host_law_production_map));
}

static int ReadRetainedMap(const char *mapname, sg_bsp_world_t **world_out,
	sg_host_law_result_t *failure_out)
{
	cvar_t *gamedir_cvar;
	const char *gamedir_path;
	char path[MAX_OSPATH];
	FILE *file;
	long length;
	void *bytes;
	size_t size;
	sg_bsp_world_t *world = NULL;
	sg_bsp_error_t bsp_error;

	if (world_out)
		*world_out = NULL;
	if (!mapname || !world_out || !failure_out || !gi.cvar)
		return 0;
	gamedir_cvar = gi.cvar("gamedir", "", 0);
	gamedir_path = gamedir_cvar && gamedir_cvar->string &&
		gamedir_cvar->string[0] ? gamedir_cvar->string : ".";
	/* The map name was validated by SG_LevelIdentityBegin.  Still use a bounded
	 * path operation so a future caller cannot turn this owner into a path
	 * traversal primitive. */
	if (snprintf(path, sizeof(path), "%s/maps/%s.bsp", gamedir_path, mapname) < 0 ||
		strlen(path) >= sizeof(path))
	{
		*failure_out = Result(SG_HOST_LAW_HOST_UNAVAILABLE,
			SG_HOST_LAW_FIELD_BSP_CONTENT, 1U, 0U);
		return 0;
	}
	file = fopen(path, "rb");
	if (!file)
	{
		*failure_out = Result(SG_HOST_LAW_HOST_UNAVAILABLE,
			SG_HOST_LAW_FIELD_BSP_CONTENT, 1U, 0U);
		return 0;
	}
	if (fseek(file, 0L, SEEK_END) != 0 || (length = ftell(file)) <= 0L ||
		fseek(file, 0L, SEEK_SET) != 0)
	{
		fclose(file);
		*failure_out = Result(SG_HOST_LAW_HOST_UNAVAILABLE,
			SG_HOST_LAW_FIELD_BSP_CONTENT, 1U, 0U);
		return 0;
	}
	size = (size_t)length;
	bytes = malloc(size);
	if (!bytes)
	{
		fclose(file);
		*failure_out = Result(SG_HOST_LAW_ALLOCATION_FAILED,
			SG_HOST_LAW_FIELD_BSP_CONTENT, size, 0U);
		return 0;
	}
	if (fread(bytes, 1U, size, file) != size || ferror(file))
	{
		free(bytes);
		fclose(file);
		*failure_out = Result(SG_HOST_LAW_HOST_UNAVAILABLE,
			SG_HOST_LAW_FIELD_BSP_CONTENT, size, 0U);
		return 0;
	}
	fclose(file);
	memset(&bsp_error, 0, sizeof(bsp_error));
	if (!SG_BspWorldLoadMemory(bytes, size, &world, &bsp_error))
	{
		free(bytes);
		*failure_out = Result(SG_HOST_LAW_HOST_UNAVAILABLE,
			SG_HOST_LAW_FIELD_BSP_CONTENT, 1U, (uint64_t)bsp_error.code);
		return 0;
	}
	/* SG_BspWorldLoadMemory retained an owned copy of exactly these bytes and
	 * computed both the SHA-256 content identity and the engine checksum from
	 * that copy.  Never reopen a second source or trust a caller checksum. */
	free(bytes);
	if (!world->source_bytes || world->source_size != size)
	{
		SG_BspWorldDestroy(world);
		*failure_out = Result(SG_HOST_LAW_HOST_UNAVAILABLE,
			SG_HOST_LAW_FIELD_BSP_CONTENT, size, 0U);
		return 0;
	}
	*world_out = world;
	return 1;
}

sg_host_law_result_t SG_HostLawProductionBeginLevel(const char *mapname)
{
	sg_host_engine_runtime_status_t runtime_status;

	OwnerClear();
	runtime_status = SG_HostEngineRuntimeBegin(mapname,
		&sg_host_law_production_runtime);
	if (runtime_status != SG_HOST_ENGINE_RUNTIME_OK)
		return Result(SG_HOST_LAW_HOST_UNAVAILABLE, SG_HOST_LAW_FIELD_PMOVE_ABI,
			1U, (uint64_t)runtime_status);
	memcpy(sg_host_law_production_map, mapname,
		sizeof(sg_host_law_production_map));
	return Result(SG_HOST_LAW_OK, SG_HOST_LAW_FIELD_NONE, 0U, 0U);
}

sg_host_law_result_t SG_HostLawProductionInstallActiveRune(void)
{
	const rune_t *active;
	const rune_artifact_t *artifact;
	sg_level_identity_t level_identity;
	sg_bsp_world_t *world = NULL;
	sg_host_collision_authority_t authority;
	sg_host_collision_error_t collision_error;
	sg_host_engine_runtime_status_t runtime_status;
	sg_host_law_publication_t *construction = NULL;
	sg_host_law_publication_t *runtime_publication = NULL;
	sg_host_law_result_t result;
	const sg_rune_model_identity_t *identity;

	if (!sg_host_law_production_runtime ||
		!sg_host_law_production_map[0])
		return HostUnavailable();
	if (sg_host_law_production || sg_host_law_production_construction ||
		sg_host_law_production_world)
		return SG_HostLawProductionRevalidate();
	if (SG_LevelIdentitySnapshot(sg_host_law_production_map, &level_identity) !=
		SG_IDENTITY_OK)
		return Result(SG_HOST_LAW_HOST_UNAVAILABLE, SG_HOST_LAW_FIELD_BSP_CONTENT,
				1U, 0U);
	active = SG_Rune();
	artifact = SG_RuneArtifact(active);
	if (!active || !artifact ||
			memcmp(artifact->identity.map_name, level_identity.mapname,
		sizeof(artifact->identity.map_name)) != 0 ||
			artifact->identity.bsp_checksum != level_identity.bsp_checksum ||
			artifact->identity.entity_crc32 != level_identity.entity_crc32 ||
			artifact->identity.host_physics_id != level_identity.host_physics_id ||
		!SG_RunePhysicsCompatible(active))
		return Result(SG_HOST_LAW_PRODUCTION_DRIFT,
			SG_HOST_LAW_FIELD_ENTITY_SEMANTICS, 1U, 0U);
	result = Result(SG_HOST_LAW_HOST_UNAVAILABLE, SG_HOST_LAW_FIELD_BSP_CONTENT,
		1U, 0U);
	if (!ReadRetainedMap(level_identity.mapname, &world, &result))
		return result;
	/* This is the active-engine identity join.  The engine map checksum and the
	 * retained bytes are compared before either backend is published. */
	if (world->engine_checksum != level_identity.bsp_checksum)
	{
		uint32_t observed = world->engine_checksum;

		SG_BspWorldDestroy(world);
		return Result(SG_HOST_LAW_PRODUCTION_DRIFT,
				SG_HOST_LAW_FIELD_BSP_CONTENT, level_identity.bsp_checksum, observed);
	}
	/* Publish the retained owner slot before activation.  The runtime resolves
	 * this slot itself; the install API has no world-shaped argument. */
	sg_host_law_production_world = world;
	runtime_status = SG_HostEngineRuntimeOwnerInstallActiveRune(
		sg_host_law_production_runtime);
	if (runtime_status != SG_HOST_ENGINE_RUNTIME_OK)
	{
		sg_host_law_production_world = NULL;
		SG_BspWorldDestroy(world);
		return Result(SG_HOST_LAW_PRODUCTION_DRIFT,
			SG_HOST_LAW_FIELD_PMOVE_LAW, 1U, (uint64_t)runtime_status);
	}
	identity = SG_HostEngineRuntimeIdentity(sg_host_law_production_runtime);
	if (!identity)
	{
		SG_HostEngineRuntimeOwnerClearAcceptance(
			sg_host_law_production_runtime);
		sg_host_law_production_world = NULL;
		SG_BspWorldDestroy(world);
		return Result(SG_HOST_LAW_PRODUCTION_DRIFT,
			SG_HOST_LAW_FIELD_PMOVE_LAW, 1U, 0U);
	}
	memset(&authority, 0, sizeof(authority));
	if (!SG_HostCollisionInit(&authority, world, identity, &collision_error))
	{
		SG_HostEngineRuntimeOwnerClearAcceptance(
			sg_host_law_production_runtime);
		sg_host_law_production_world = NULL;
		SG_BspWorldDestroy(world);
		return Result(SG_HOST_LAW_HOST_UNAVAILABLE,
			SG_HOST_LAW_FIELD_COLLISION_LAW, 1U,
			(uint64_t)collision_error);
	}
	result = SG_HostLawPublicationIssue(&authority, &construction);
	if (result.status != SG_HOST_LAW_OK)
	{
		SG_HostEngineRuntimeOwnerClearAcceptance(
			sg_host_law_production_runtime);
		sg_host_law_production_world = NULL;
		SG_BspWorldDestroy(world);
		return result;
	}
	result = SG_HostLawPublicationIssueRuntime(
		sg_host_law_production_runtime, &runtime_publication);
	if (result.status != SG_HOST_LAW_OK)
	{
		SG_HostLawPublicationDestroy(construction);
		SG_HostEngineRuntimeOwnerClearAcceptance(
			sg_host_law_production_runtime);
		sg_host_law_production_world = NULL;
		SG_BspWorldDestroy(world);
		return result;
	}
	/* BeginLevel cleared the old generation.  Publish the complete pair only
	 * after both views have passed their own owner checks. */
	sg_host_law_production_construction = construction;
	sg_host_law_production = runtime_publication;
	return Result(SG_HOST_LAW_OK, SG_HOST_LAW_FIELD_NONE, 0U, 0U);
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
		return result;
	}
	result = SG_HostLawPublicationRevalidateProduction(
		sg_host_law_production_construction);
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
