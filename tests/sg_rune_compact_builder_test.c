#if defined(SG_COMPACT_BUILDER_REAL_SOURCE_AUTHORITY)
#include "g_local.h"
#undef world
#include "g_tourney.h"
#include "slipgate/sg_crc32.h"
#include "slipgate/sg_rune_source_authority_owner.h"
#endif

#include "slipgate/sg_rune_compact_builder.h"
#include "slipgate/sg_configuration_audit.h"
#include "slipgate/sg_host_law_publication_private.h"
#include "slipgate/sg_rune_source_authority.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct sg_host_law_construction_s
{
	uint32_t marker;
};

static int failures;
static int read_calls;
static int fail_final_read;
static int configuration_default_calls;
static int semantics_default_calls;
static int visibility_default_calls;
static int configuration_audit_calls;
static int semantics_audit_calls;
static int entity_audit_calls;
static int visibility_audit_calls;
static sg_host_law_construction_view_t host_view;
static int source_copy_calls;
static int source_drift_on_visibility_audit;

static const char source_entity_text[] =
	"{\n\"classname\" \"worldspawn\"\n\"message\" \"selected override\"\n}\n"
	"{\n\"classname\" \"trigger_once\"\n\"model\" \"*1\"\n}\n"
	"{\n\"classname\" \"info_player_start\"\n}\n";
static const sg_rune_source_entity_record_t source_entity_records[] = {
	{ 0U, 0 },
	{ 2U, 0 }
};

#if defined(SG_COMPACT_BUILDER_REAL_SOURCE_AUTHORITY)
static cvar_t ctfflags_value;
static cvar_t deathmatch_value;
static cvar_t fastswitch_value;
static sg_level_identity_t source_level_identity;
static int source_host_current = 1;
static uint64_t source_host_epoch = UINT64_C(17);

cvar_t *ctfflags = &ctfflags_value;
cvar_t *deathmatch = &deathmatch_value;
cvar_t *fastswitch = &fastswitch_value;
int matchstate = MATCH_INPLAY;
#else
struct sg_rune_source_authority_s
{
	uint32_t marker;
};

static struct sg_rune_source_authority_s source_authority_handle = { 1U };
static sg_rune_source_snapshot_t source_snapshot;
static sg_rune_source_status_t source_final_status = SG_RUNE_SOURCE_OK;
static int source_mutate_second_copy;
static int source_mutate_second_weapon;
#endif

typedef enum dependency_failure_e
{
	FAILURE_NONE = 0,
	FAILURE_BSP_LOAD,
	FAILURE_CONFIGURATION_BUILD,
	FAILURE_CONFIGURATION_AUDIT,
	FAILURE_SEMANTICS_BUILD,
	FAILURE_SEMANTICS_AUDIT,
	FAILURE_ENTITY_BUILD,
	FAILURE_ENTITY_AUDIT,
	FAILURE_VISIBILITY_BUILD,
	FAILURE_VISIBILITY_AUDIT
} dependency_failure_t;

static dependency_failure_t dependency_failure;

const sg_rune_v2_content_id_t SG_BSP_ENTITY_SEMANTICS_SCHEMA_ID = {
	{
		0x53U, 0x47U, 0x2dU, 0x42U, 0x53U, 0x50U, 0x2dU, 0x45U,
		0x4eU, 0x54U, 0x49U, 0x54U, 0x59U, 0x2dU, 0x53U, 0x45U,
		0x4dU, 0x41U, 0x4eU, 0x54U, 0x49U, 0x43U, 0x53U, 0x2dU,
		0x56U, 0x31U, 0U, 0U, 0U, 0U, 0U, 1U
	}
};

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static sg_host_law_result_t HostResult(sg_host_law_status_t status)
{
	sg_host_law_result_t result;

	memset(&result, 0, sizeof(result));
	result.status = status;
	result.element = SG_HOST_LAW_ELEMENT_NONE;
	return result;
}

#if defined(SG_COMPACT_BUILDER_REAL_SOURCE_AUTHORITY)
sg_identity_status_t SG_LevelIdentitySnapshot(const char *expected_mapname,
	sg_level_identity_t *out)
{
	if (!expected_mapname || !out)
		return SG_IDENTITY_INVALID_ARGUMENT;
	if (strcmp(expected_mapname, source_level_identity.mapname) != 0)
		return SG_IDENTITY_MAPNAME_MISMATCH;
	*out = source_level_identity;
	return SG_IDENTITY_OK;
}

sg_host_law_result_t SG_HostLawProductionAcquire(
	sg_host_law_runtime_authority_t *authority_out)
{
	if (!authority_out || !source_host_current)
		return HostResult(SG_HOST_LAW_HOST_UNAVAILABLE);
	memset(authority_out, 0, sizeof(*authority_out));
	authority_out->version = SG_HOST_LAW_RUNTIME_AUTHORITY_VERSION;
	authority_out->epoch = source_host_epoch;
	authority_out->epoch_complement = ~source_host_epoch;
	authority_out->view = host_view.laws;
	return HostResult(SG_HOST_LAW_OK);
}

sg_host_law_result_t SG_HostLawProductionAuthorityCurrent(
	const sg_host_law_runtime_authority_t *authority)
{
	if (!authority || !source_host_current ||
		authority->epoch != source_host_epoch ||
		authority->epoch_complement != ~authority->epoch)
		return HostResult(SG_HOST_LAW_PRODUCTION_DRIFT);
	return HostResult(SG_HOST_LAW_OK);
}
#else
sg_rune_source_status_t SG_RuneSourceAuthorityAcquire(
	sg_rune_source_authority_t **authority_out)
{
	if (!authority_out)
		return SG_RUNE_SOURCE_INVALID_ARGUMENT;
	*authority_out = &source_authority_handle;
	return SG_RUNE_SOURCE_OK;
}

sg_rune_source_status_t SG_RuneSourceAuthoritySizes(
	const sg_rune_source_authority_t *authority,
	size_t *entity_text_bytes_out, size_t *entity_record_count_out)
{
	if (authority != &source_authority_handle || !entity_text_bytes_out ||
		!entity_record_count_out)
		return SG_RUNE_SOURCE_INVALID_ARGUMENT;
	*entity_text_bytes_out = sizeof(source_entity_text);
	*entity_record_count_out = sizeof(source_entity_records) /
		sizeof(source_entity_records[0]);
	return SG_RUNE_SOURCE_OK;
}

sg_rune_source_status_t SG_RuneSourceAuthorityCopy(
	const sg_rune_source_authority_t *authority,
	sg_rune_source_snapshot_t *snapshot_out,
	char *entity_text_out, size_t entity_text_capacity,
	sg_rune_source_entity_record_t *entity_records_out,
	size_t entity_record_capacity)
{
	const size_t record_count = sizeof(source_entity_records) /
		sizeof(source_entity_records[0]);

	if (authority != &source_authority_handle || !snapshot_out ||
		!entity_text_out || !entity_records_out)
		return SG_RUNE_SOURCE_INVALID_ARGUMENT;
	if (entity_text_capacity < sizeof(source_entity_text) ||
		entity_record_capacity < record_count)
		return SG_RUNE_SOURCE_BUFFER_TOO_SMALL;
	source_copy_calls++;
	if (source_copy_calls > 1 && source_final_status != SG_RUNE_SOURCE_OK)
		return source_final_status;
	*snapshot_out = source_snapshot;
	memcpy(entity_text_out, source_entity_text, sizeof(source_entity_text));
	memcpy(entity_records_out, source_entity_records,
		sizeof(source_entity_records));
	if (source_copy_calls > 1 && source_mutate_second_copy)
		entity_text_out[1] ^= 1;
	if (source_copy_calls > 1 && source_mutate_second_weapon)
		snapshot_out->weapon_law.fast_switch_enabled ^= 1U;
	return SG_RUNE_SOURCE_OK;
}

sg_rune_source_status_t SG_RuneSourceAuthorityCurrent(
	const sg_rune_source_authority_t *authority)
{
	if (authority != &source_authority_handle)
		return SG_RUNE_SOURCE_INVALID_ARGUMENT;
	return source_final_status;
}

void SG_RuneSourceAuthorityDestroy(sg_rune_source_authority_t *authority)
{
	(void)authority;
}
#endif

#if defined(SG_COMPACT_BUILDER_REAL_BSP)
#define REAL_BSP_HEADER_BYTES (8U + SG_BSP_LUMP_COUNT * 8U)
#define REAL_BSP_CAPACITY 2048U

static uint8_t real_bsp_bytes[REAL_BSP_CAPACITY];
static uint32_t real_bsp_size;
static uint32_t real_bsp_offsets[SG_BSP_LUMP_COUNT];
static uint32_t real_bsp_lengths[SG_BSP_LUMP_COUNT];

static void WriteU16(uint8_t *bytes, uint16_t value)
{
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
}

static void WriteI16(uint8_t *bytes, int16_t value)
{
	WriteU16(bytes, (uint16_t)value);
}

static void WriteU32(uint8_t *bytes, uint32_t value)
{
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
	bytes[2] = (uint8_t)(value >> 16);
	bytes[3] = (uint8_t)(value >> 24);
}

static void WriteI32(uint8_t *bytes, int32_t value)
{
	WriteU32(bytes, (uint32_t)value);
}

static void WriteFloat(uint8_t *bytes, float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	WriteU32(bytes, bits);
}

static uint8_t *AddRealBspLump(sg_bsp_lump_t lump, uint32_t length)
{
	uint8_t *result;

	if (real_bsp_size > REAL_BSP_CAPACITY - length) {
		fputs("real BSP fixture exceeds capacity\n", stderr);
		exit(2);
	}
	real_bsp_offsets[lump] = real_bsp_size;
	real_bsp_lengths[lump] = length;
	result = real_bsp_bytes + real_bsp_size;
	real_bsp_size += length;
	memset(result, 0, length);
	return result;
}

static void BuildRealBsp(void)
{
	uint8_t *record;
	uint32_t lump;

	if (real_bsp_size != 0U)
		return;
	memset(real_bsp_bytes, 0, sizeof(real_bsp_bytes));
	real_bsp_size = REAL_BSP_HEADER_BYTES;
	for (lump = 0U; lump < SG_BSP_LUMP_COUNT; lump++)
		real_bsp_offsets[lump] = REAL_BSP_HEADER_BYTES;
	record = AddRealBspLump(SG_BSP_LUMP_ENTITIES, 4U);
	memcpy(record, "{}\n", 4U);
	record = AddRealBspLump(SG_BSP_LUMP_PLANES, 20U);
	WriteFloat(record + 8U, 1.0f);
	WriteI32(record + 16U, 2);
	record = AddRealBspLump(SG_BSP_LUMP_VERTICES, 36U);
	WriteFloat(record + 0U, -16.0f);
	WriteFloat(record + 4U, -16.0f);
	WriteFloat(record + 12U, 16.0f);
	WriteFloat(record + 16U, -16.0f);
	WriteFloat(record + 24U, 0.0f);
	WriteFloat(record + 28U, 16.0f);
	record = AddRealBspLump(SG_BSP_LUMP_VISIBILITY, 13U);
	WriteU32(record, 1U);
	WriteU32(record + 4U, 12U);
	WriteU32(record + 8U, 12U);
	record[12] = 1U;
	record = AddRealBspLump(SG_BSP_LUMP_NODES, 28U);
	WriteU32(record, 0U);
	WriteI32(record + 4U, -1);
	WriteI32(record + 8U, -2);
	WriteI16(record + 12U, -16);
	WriteI16(record + 14U, -16);
	WriteI16(record + 16U, -16);
	WriteI16(record + 18U, 16);
	WriteI16(record + 20U, 16);
	WriteI16(record + 22U, 16);
	WriteU16(record + 24U, 0U);
	WriteU16(record + 26U, 1U);
	record = AddRealBspLump(SG_BSP_LUMP_TEXINFO, 76U);
	WriteFloat(record + 0U, 1.0f);
	WriteFloat(record + 20U, 1.0f);
	WriteI32(record + 32U, 4);
	WriteI32(record + 36U, 7);
	memcpy(record + 40U, "stone", 5U);
	WriteI32(record + 72U, -1);
	record = AddRealBspLump(SG_BSP_LUMP_FACES, 20U);
	WriteU16(record, 0U);
	WriteI32(record + 4U, 0);
	WriteI16(record + 8U, 3);
	record[12] = 0U;
	record[13] = 255U;
	record[14] = 255U;
	record[15] = 255U;
	WriteI32(record + 16U, 0);
	record = AddRealBspLump(SG_BSP_LUMP_LIGHTING, 3U);
	record[0] = 10U;
	record[1] = 20U;
	record[2] = 30U;
	record = AddRealBspLump(SG_BSP_LUMP_LEAVES, 56U);
	WriteI32(record, 1);
	WriteU16(record + 4U, UINT16_MAX);
	WriteI16(record + 6U, 0);
	WriteI16(record + 8U, -16);
	WriteI16(record + 10U, -16);
	WriteI16(record + 12U, -16);
	WriteI16(record + 14U, 16);
	WriteI16(record + 16U, 16);
	WriteI16(record + 18U, 16);
	WriteU16(record + 20U, 0U);
	WriteU16(record + 22U, 1U);
	WriteU16(record + 24U, 0U);
	WriteU16(record + 26U, 1U);
	WriteI32(record + 28U, 0);
	WriteU16(record + 32U, 0U);
	WriteU16(record + 34U, 0U);
	WriteI16(record + 36U, -16);
	WriteI16(record + 38U, -16);
	WriteI16(record + 40U, -16);
	WriteI16(record + 42U, 16);
	WriteI16(record + 44U, 16);
	WriteI16(record + 46U, 16);
	record = AddRealBspLump(SG_BSP_LUMP_LEAF_FACES, 2U);
	WriteU16(record, 0U);
	record = AddRealBspLump(SG_BSP_LUMP_LEAF_BRUSHES, 2U);
	WriteU16(record, 0U);
	record = AddRealBspLump(SG_BSP_LUMP_EDGES, 12U);
	WriteU16(record + 0U, 0U);
	WriteU16(record + 2U, 1U);
	WriteU16(record + 4U, 1U);
	WriteU16(record + 6U, 2U);
	WriteU16(record + 8U, 2U);
	WriteU16(record + 10U, 0U);
	record = AddRealBspLump(SG_BSP_LUMP_SURFEDGES, 12U);
	WriteI32(record + 0U, 0);
	WriteI32(record + 4U, 1);
	WriteI32(record + 8U, 2);
	record = AddRealBspLump(SG_BSP_LUMP_MODELS, 96U);
	WriteFloat(record + 0U, -16.0f);
	WriteFloat(record + 4U, -16.0f);
	WriteFloat(record + 8U, -16.0f);
	WriteFloat(record + 12U, 16.0f);
	WriteFloat(record + 16U, 16.0f);
	WriteFloat(record + 20U, 16.0f);
	WriteI32(record + 36U, 0);
	WriteI32(record + 40U, 0);
	WriteI32(record + 44U, 1);
	WriteFloat(record + 48U, -8.0f);
	WriteFloat(record + 52U, -8.0f);
	WriteFloat(record + 56U, -8.0f);
	WriteFloat(record + 60U, 8.0f);
	WriteFloat(record + 64U, 8.0f);
	WriteFloat(record + 68U, 8.0f);
	WriteFloat(record + 72U, 32.0f);
	WriteI32(record + 84U, -1);
	WriteI32(record + 88U, 0);
	WriteI32(record + 92U, 0);
	record = AddRealBspLump(SG_BSP_LUMP_BRUSHES, 12U);
	WriteI32(record, 0);
	WriteI32(record + 4U, 1);
	WriteI32(record + 8U, 1);
	record = AddRealBspLump(SG_BSP_LUMP_BRUSH_SIDES, 4U);
	WriteU16(record, 0U);
	WriteI16(record + 2U, 0);
	(void)AddRealBspLump(SG_BSP_LUMP_POP, 256U);
	record = AddRealBspLump(SG_BSP_LUMP_AREAS, 16U);
	WriteI32(record, 1);
	WriteI32(record + 4U, 0);
	WriteI32(record + 8U, 1);
	WriteI32(record + 12U, 1);
	record = AddRealBspLump(SG_BSP_LUMP_AREAPORTALS, 16U);
	WriteI32(record, 0);
	WriteI32(record + 4U, 1);
	WriteI32(record + 8U, 0);
	WriteI32(record + 12U, 0);
	memcpy(real_bsp_bytes, "IBSP", 4U);
	WriteU32(real_bsp_bytes + 4U, SG_BSP_VERSION);
	for (lump = 0U; lump < SG_BSP_LUMP_COUNT; lump++) {
		WriteU32(real_bsp_bytes + 8U + lump * 8U,
			real_bsp_offsets[lump]);
		WriteU32(real_bsp_bytes + 12U + lump * 8U,
			real_bsp_lengths[lump]);
	}
}
#endif

sg_host_law_result_t SG_HostLawConstructionRead(
	const sg_host_law_construction_t *construction,
	sg_host_law_construction_view_t *view_out)
{
	(void)construction;
	read_calls++;
	if (fail_final_read && read_calls > 1)
		return HostResult(SG_HOST_LAW_PRODUCTION_DRIFT);
	*view_out = host_view;
	return HostResult(SG_HOST_LAW_OK);
}

sg_host_law_result_t SG_HostLawConstructionOwnerCopyBsp(
	const sg_host_law_construction_t *construction, uint8_t *bytes_out,
	size_t capacity, size_t *size_out,
	sg_host_static_identity_t *identity_out)
{
#if defined(SG_COMPACT_BUILDER_REAL_BSP)
	const uint8_t *bytes = real_bsp_bytes;
	const size_t byte_count = (size_t)real_bsp_size;
#else
	static const uint8_t bytes[4] = { 1U, 2U, 3U, 4U };
	const size_t byte_count = sizeof(bytes);
#endif

	(void)construction;
	*size_out = byte_count;
	if (!bytes_out)
		return HostResult(SG_HOST_LAW_OK);
	if (capacity < byte_count)
		return HostResult(SG_HOST_LAW_INVALID_ARGUMENT);
	memcpy(bytes_out, bytes, byte_count);
	if (identity_out)
		*identity_out = host_view.host_static_identity;
	return HostResult(SG_HOST_LAW_OK);
}

#if !defined(SG_COMPACT_BUILDER_REAL_BSP)
int SG_BspWorldLoadMemory(const void *data, size_t size,
	sg_bsp_world_t **world_out, sg_bsp_error_t *error_out)
{
	sg_bsp_world_t *world;

	(void)data;
	if (dependency_failure == FAILURE_BSP_LOAD) {
		error_out->code = SG_BSP_ERROR_OUT_OF_MEMORY;
		return 0;
	}
	world = calloc(1U, sizeof(*world));
	if (!world)
		return 0;
	world->source_bytes = malloc(size);
	if (!world->source_bytes) {
		free(world);
		return 0;
	}
	world->source_size = size;
	world->content_identity = host_view.geometry.bsp_identity;
	world->engine_checksum = host_view.geometry.engine_checksum;
	world->entity_byte_count = host_view.geometry.entity_bytes;
	world->model_count = host_view.geometry.model_count;
	world->leaf_count = host_view.geometry.leaf_count;
	world->area_count = 1U;
	world->plane_count = host_view.geometry.plane_count;
	world->node_count = host_view.geometry.node_count;
	world->texinfo_count = host_view.geometry.texinfo_count;
	world->leaf_brush_count = host_view.geometry.leaf_brush_count;
	world->brush_count = host_view.geometry.brush_count;
	world->brush_side_count = host_view.geometry.brush_side_count;
	*world_out = world;
	return 1;
}

int SG_BspWorldSourceIdentityCurrent(const sg_bsp_world_t *world)
{
	return world != NULL;
}

void SG_BspWorldDestroy(sg_bsp_world_t *world)
{
	if (!world)
		return;
	free(world->source_bytes);
	free(world);
}
#endif

int SG_HostCollisionInit(sg_host_collision_authority_t *authority,
	const sg_bsp_world_t *world, const sg_rune_model_identity_t *identity,
	sg_host_collision_error_t *error_out)
{
	(void)error_out;
	authority->world = world;
	authority->identity = *identity;
	authority->content_identity = world->content_identity;
	return 1;
}

void SG_ConfigurationDefaultLimits(sg_configuration_limits_t *limits_out)
{
	configuration_default_calls++;
	memset(limits_out, UINT8_MAX, sizeof(*limits_out));
}

int SG_ConfigurationBuild(const sg_host_collision_authority_t *authority,
	const sg_configuration_limits_t *limits,
	sg_configuration_space_t **space_out, sg_configuration_error_t *error_out)
{
	if (!limits) {
		error_out->code = SG_CONFIGURATION_ERROR_INVALID_ARGUMENT;
		return 0;
	}
	if (dependency_failure == FAILURE_CONFIGURATION_BUILD) {
		error_out->code = SG_CONFIGURATION_ERROR_OUT_OF_MEMORY;
		return 0;
	}
	*space_out = calloc(1U, sizeof(**space_out));
	if (!*space_out)
		return 0;
	(*space_out)->identity = authority->identity;
	(*space_out)->cell_count = 1U;
	return 1;
}

int SG_ConfigurationAudit(const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *space,
	sg_configuration_audit_result_t *result_out)
{
	configuration_audit_calls++;
	(void)authority;
	(void)space;
	if (dependency_failure == FAILURE_CONFIGURATION_AUDIT) {
		result_out->code = SG_CONFIGURATION_AUDIT_OUT_OF_MEMORY;
		return 0;
	}
	result_out->code = SG_CONFIGURATION_AUDIT_OK;
	return 1;
}

void SG_ConfigurationDestroy(sg_configuration_space_t *space)
{
	free(space);
}

void SG_ConfigurationSemanticsDefaultLimits(
	sg_configuration_semantics_limits_t *limits_out)
{
	semantics_default_calls++;
	memset(limits_out, UINT8_MAX, sizeof(*limits_out));
}

int SG_ConfigurationSemanticsBuild(
	const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_limits_t *limits,
	sg_configuration_semantics_t **semantics_out,
	sg_configuration_semantics_error_t *error_out)
{
	(void)authority;
	if (!limits) {
		error_out->code = SG_CONFIGURATION_SEMANTICS_ERROR_INVALID_ARGUMENT;
		return 0;
	}
	if (dependency_failure == FAILURE_SEMANTICS_BUILD) {
		error_out->code = SG_CONFIGURATION_SEMANTICS_ERROR_OUT_OF_MEMORY;
		return 0;
	}
	*semantics_out = calloc(1U, sizeof(**semantics_out));
	if (!*semantics_out)
		return 0;
	(*semantics_out)->identity = configuration->identity;
	(*semantics_out)->region_count = 1U;
	return 1;
}

int SG_ConfigurationSemanticsAudit(
	const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	sg_configuration_semantics_audit_result_t *result_out)
{
	semantics_audit_calls++;
	(void)authority;
	(void)configuration;
	(void)semantics;
	if (dependency_failure == FAILURE_SEMANTICS_AUDIT) {
		result_out->code =
			SG_CONFIGURATION_SEMANTICS_AUDIT_OUT_OF_MEMORY;
		return 0;
	}
	result_out->code = SG_CONFIGURATION_SEMANTICS_AUDIT_OK;
	return 1;
}

void SG_ConfigurationSemanticsDestroy(
	sg_configuration_semantics_t *semantics)
{
	free(semantics);
}

#if !defined(SG_COMPACT_BUILDER_REAL_ENTITY_SEMANTICS)
int SG_BspEntitySemanticsBuildEffective(const sg_bsp_world_t *world,
	const char *selected_entity_text, size_t selected_entity_text_bytes,
	const sg_rune_source_entity_record_t *survivors, size_t survivor_count,
	uint64_t source_set_identity, sg_bsp_entity_semantics_t **semantics_out,
	sg_bsp_entity_semantics_error_t *error_out)
{
	size_t index;
	size_t semantic_count;

	(void)world;
	if (!selected_entity_text || selected_entity_text_bytes == 0U ||
		!survivors || survivor_count == 0U) {
		error_out->code = SG_BSP_ENTITY_SEMANTICS_ERROR_INVALID_ARGUMENT;
		return 0;
	}
	if (dependency_failure == FAILURE_ENTITY_BUILD) {
		error_out->code = SG_BSP_ENTITY_SEMANTICS_ERROR_OUT_OF_MEMORY;
		return 0;
	}
	semantic_count = survivor_count - 1U;
	*semantics_out = calloc(1U, sizeof(**semantics_out));
	if (!*semantics_out)
		return 0;
	(*semantics_out)->entities = calloc(semantic_count,
		sizeof(*(*semantics_out)->entities));
	(*semantics_out)->strings = calloc(1U, 1U);
	if (!(*semantics_out)->entities || !(*semantics_out)->strings) {
		free((*semantics_out)->strings);
		free((*semantics_out)->entities);
		free(*semantics_out);
		*semantics_out = NULL;
		return 0;
	}
	(*semantics_out)->source_set_identity = source_set_identity;
	(*semantics_out)->world.source_set_identity = source_set_identity;
	for (index = 0U; index < semantic_count; index++) {
		(*semantics_out)->entities[index].source_set_identity =
			source_set_identity;
		(*semantics_out)->entities[index].bsp_model =
			SG_BSP_ENTITY_MODEL_NONE;
	}
	(*semantics_out)->entity_count = (uint32_t)semantic_count;
	(*semantics_out)->string_bytes = 1U;
	return 1;
}
#endif

static int AuditEffectiveSourceMatchesFixture(
	const sg_bsp_entity_semantics_source_t *source)
{
	size_t record_bytes = sizeof(source_entity_records);

	return source && source->selected_entity_text &&
		source->selected_entity_text_bytes == sizeof(source_entity_text) &&
		!memcmp(source->selected_entity_text, source_entity_text,
			source->selected_entity_text_bytes) && source->survivors &&
		source->survivor_count == sizeof(source_entity_records) /
			sizeof(source_entity_records[0]) &&
		!memcmp(source->survivors, source_entity_records, record_bytes);
}

int SG_BspEntitySemanticsAuditEffective(
	const sg_host_collision_authority_t *authority,
	const sg_bsp_entity_semantics_binding_t *binding,
	const sg_bsp_entity_semantics_source_t *source,
	const sg_bsp_entity_semantics_t *candidate,
	sg_bsp_entity_semantics_audit_result_t *result_out)
{
	entity_audit_calls++;
	(void)authority;
	(void)binding;
	(void)candidate;
	if (!result_out || !AuditEffectiveSourceMatchesFixture(source)) {
		if (result_out)
			result_out->code = SG_BSP_ENTITY_SEMANTICS_AUDIT_INVALID_ARGUMENT;
		return 0;
	}
	if (dependency_failure == FAILURE_ENTITY_AUDIT) {
		result_out->code = SG_BSP_ENTITY_SEMANTICS_AUDIT_OUT_OF_MEMORY;
		return 0;
	}
	result_out->code = SG_BSP_ENTITY_SEMANTICS_AUDIT_OK;
	return 1;
}

int SG_BspEntitySemanticsAudit(const sg_host_collision_authority_t *authority,
	const sg_bsp_entity_semantics_binding_t *binding,
	const sg_bsp_entity_semantics_t *candidate,
	sg_bsp_entity_semantics_audit_result_t *result_out)
{
	(void)authority;
	(void)binding;
	(void)candidate;
	if (!result_out)
		return 0;
	if (dependency_failure == FAILURE_ENTITY_AUDIT) {
		result_out->code = SG_BSP_ENTITY_SEMANTICS_AUDIT_OUT_OF_MEMORY;
		return 0;
	}
	result_out->code = SG_BSP_ENTITY_SEMANTICS_AUDIT_OK;
	return 1;
}

#if !defined(SG_COMPACT_BUILDER_REAL_ENTITY_SEMANTICS)
void SG_BspEntitySemanticsDestroy(sg_bsp_entity_semantics_t *semantics)
{
	if (!semantics)
		return;
	free(semantics->strings);
	free(semantics->edges);
	free(semantics->entities);
	free(semantics);
}
#endif

void SG_StaticVisibilityDefaultLimits(
	sg_static_visibility_limits_t *limits_out)
{
	visibility_default_calls++;
	memset(limits_out, UINT8_MAX, sizeof(*limits_out));
}

int SG_StaticVisibilityBuild(const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	const sg_static_visibility_limits_t *limits,
	sg_static_visibility_t **visibility_out,
	sg_static_visibility_error_t *error_out)
{
	(void)authority;
	(void)semantics;
	if (!limits) {
		error_out->code = SG_STATIC_VISIBILITY_ERROR_INVALID_ARGUMENT;
		return 0;
	}
	if (dependency_failure == FAILURE_VISIBILITY_BUILD) {
		error_out->code = SG_STATIC_VISIBILITY_ERROR_OUT_OF_MEMORY;
		return 0;
	}
	*visibility_out = calloc(1U, sizeof(**visibility_out));
	if (!*visibility_out)
		return 0;
	(*visibility_out)->identity = configuration->identity;
	(*visibility_out)->partition_count = 1U;
	if (source_drift_on_visibility_audit == 1) {
#if defined(SG_COMPACT_BUILDER_REAL_SOURCE_AUTHORITY)
		SG_RuneSourceAuthorityReset();
#else
		source_final_status = SG_RUNE_SOURCE_GENERATION_DRIFT;
#endif
	} else if (source_drift_on_visibility_audit == 2) {
#if defined(SG_COMPACT_BUILDER_REAL_SOURCE_AUTHORITY)
		deathmatch_value.value = 0.0f;
#else
		source_final_status = SG_RUNE_SOURCE_WEAPON_DRIFT;
#endif
	} else if (source_drift_on_visibility_audit == 3) {
#if !defined(SG_COMPACT_BUILDER_REAL_SOURCE_AUTHORITY)
		source_mutate_second_copy = 1;
#endif
	} else if (source_drift_on_visibility_audit == 4) {
#if !defined(SG_COMPACT_BUILDER_REAL_SOURCE_AUTHORITY)
		source_mutate_second_weapon = 1;
#endif
	}
	return 1;
}

int SG_StaticVisibilityAudit(const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	const sg_static_visibility_t *visibility,
	sg_static_visibility_audit_result_t *result_out)
{
	visibility_audit_calls++;
	(void)authority;
	(void)configuration;
	(void)semantics;
	(void)visibility;
	if (dependency_failure == FAILURE_VISIBILITY_AUDIT) {
		result_out->code = SG_STATIC_VISIBILITY_AUDIT_OUT_OF_MEMORY;
		return 0;
	}
	result_out->code = SG_STATIC_VISIBILITY_AUDIT_OK;
	return 1;
}

void SG_StaticVisibilityDestroy(sg_static_visibility_t *visibility)
{
	free(visibility);
}

#if !defined(SG_COMPACT_BUILDER_REAL_WEAPONS)
size_t SG_WeaponProfileCount(void)
{
	return (size_t)SG_WEAPON_PROFILE_COUNT - 1U;
}

int SG_WeaponProfileCatalogValid(void)
{
	return 1;
}

int SG_WeaponProfileResolve(sg_weapon_profile_id_t id,
	const sg_weapon_law_input_t *law, sg_weapon_profile_t *profile_out)
{
	memset(profile_out, 0, sizeof(*profile_out));
	profile_out->id = id;
	switch (id) {
	case SG_WEAPON_PROFILE_BLASTER:
		profile_out->family = SG_WEAPON_FAMILY_STRAIGHT_PROJECTILE;
		profile_out->effects = SG_WEAPON_EFFECT_PROJECTILE;
		break;
	case SG_WEAPON_PROFILE_SHOTGUN:
	case SG_WEAPON_PROFILE_SUPER_SHOTGUN:
		profile_out->family = SG_WEAPON_FAMILY_SPREAD;
		profile_out->effects = SG_WEAPON_EFFECT_HITSCAN |
			SG_WEAPON_EFFECT_SPREAD;
		break;
	case SG_WEAPON_PROFILE_MACHINEGUN:
	case SG_WEAPON_PROFILE_CHAINGUN:
		profile_out->family = SG_WEAPON_FAMILY_HITSCAN;
		profile_out->effects = SG_WEAPON_EFFECT_HITSCAN |
			SG_WEAPON_EFFECT_SPREAD;
		break;
	case SG_WEAPON_PROFILE_GRENADE_LAUNCHER:
	case SG_WEAPON_PROFILE_HAND_GRENADE:
		profile_out->family = SG_WEAPON_FAMILY_GRENADE_BOUNCE;
		profile_out->effects = SG_WEAPON_EFFECT_PROJECTILE |
			SG_WEAPON_EFFECT_SPLASH | SG_WEAPON_EFFECT_BOUNCE;
		break;
	case SG_WEAPON_PROFILE_ROCKET_LAUNCHER:
		profile_out->family = SG_WEAPON_FAMILY_ROCKET_SPLASH;
		profile_out->effects = SG_WEAPON_EFFECT_PROJECTILE |
			SG_WEAPON_EFFECT_SPLASH;
		break;
	case SG_WEAPON_PROFILE_HYPERBLASTER:
		profile_out->family = SG_WEAPON_FAMILY_HYPERBLASTER;
		profile_out->effects = SG_WEAPON_EFFECT_PROJECTILE;
		break;
	case SG_WEAPON_PROFILE_RAILGUN:
		profile_out->family = SG_WEAPON_FAMILY_HITSCAN;
		profile_out->effects = SG_WEAPON_EFFECT_HITSCAN |
			SG_WEAPON_EFFECT_PENETRATION;
		break;
	case SG_WEAPON_PROFILE_BFG:
		profile_out->family = SG_WEAPON_FAMILY_BFG;
		profile_out->effects = SG_WEAPON_EFFECT_PROJECTILE |
			SG_WEAPON_EFFECT_SPLASH | SG_WEAPON_EFFECT_SPECIAL;
		break;
	case SG_WEAPON_PROFILE_PLASMA_REFLECT:
		profile_out->family = SG_WEAPON_FAMILY_PLASMA_REFLECT;
		profile_out->effects = SG_WEAPON_EFFECT_PROJECTILE |
			SG_WEAPON_EFFECT_SPLASH | SG_WEAPON_EFFECT_BOUNCE;
		break;
	case SG_WEAPON_PROFILE_PLASMA_SPREAD:
		profile_out->family = SG_WEAPON_FAMILY_PLASMA_SPREAD;
		profile_out->effects = SG_WEAPON_EFFECT_PROJECTILE |
			SG_WEAPON_EFFECT_SPLASH | SG_WEAPON_EFFECT_SPREAD;
		break;
	case SG_WEAPON_PROFILE_HOOK:
		profile_out->family = SG_WEAPON_FAMILY_SPECIAL;
		profile_out->effects = SG_WEAPON_EFFECT_PROJECTILE |
			SG_WEAPON_EFFECT_SPECIAL;
		break;
	case SG_WEAPON_PROFILE_COUNT:
		return 0;
	}
	profile_out->projectile_count_min = 1U;
	profile_out->projectile_count_max = 1U;
	profile_out->requires_live_trace = 1U;
	profile_out->resolved = 1U;
	profile_out->build_identity = law->build_identity;
	profile_out->physics_abi_id = law->physics_abi_id;
	return 1;
}
#endif

static sg_rune_compact_builder_input_t Input(
	const sg_host_law_construction_t *construction)
{
	sg_rune_compact_builder_input_t input;

	memset(&input, 0, sizeof(input));
	input.construction = construction;
	return input;
}

static void SetHost(void)
{
	memset(&host_view, 0, sizeof(host_view));
	host_view.version = SG_HOST_LAW_PUBLICATION_VERSION;
	host_view.current = 1U;
	host_view.level_generation = 7U;
#if defined(SG_COMPACT_BUILDER_REAL_BSP)
	BuildRealBsp();
	if (!SG_BspWorldContentIdentity(real_bsp_bytes, (size_t)real_bsp_size,
			&host_view.host_static_identity.bsp_identity) ||
		!SG_BspWorldEngineChecksum(real_bsp_bytes, (size_t)real_bsp_size,
			&host_view.host_static_identity.engine_checksum)) {
		fputs("could not identify real BSP fixture\n", stderr);
		exit(2);
	}
	host_view.host_static_identity.bsp_bytes = real_bsp_size;
#else
	uint32_t index;

	for (index = 0U; index < SG_BSP_CONTENT_ID_BYTES; index++)
		host_view.host_static_identity.bsp_identity.bytes[index] =
			(uint8_t)(index + 1U);
	host_view.host_static_identity.bsp_bytes = 4U;
	host_view.host_static_identity.engine_checksum = UINT32_C(0x12345678);
#endif
	host_view.host_static_identity.entity_crc32 = UINT32_C(0x23456789);
	host_view.host_static_identity.host_physics_epoch = 1U;
	host_view.host_static_identity.physics_abi_id = UINT64_C(0x2222);
	host_view.host_static_identity.standing_hull.mins.value[0] = -16.0f;
	host_view.host_static_identity.standing_hull.mins.value[1] = -16.0f;
	host_view.host_static_identity.standing_hull.mins.value[2] = -24.0f;
	host_view.host_static_identity.standing_hull.maxs.value[0] = 16.0f;
	host_view.host_static_identity.standing_hull.maxs.value[1] = 16.0f;
	host_view.host_static_identity.standing_hull.maxs.value[2] = 32.0f;
	host_view.host_static_identity.crouching_hull =
		host_view.host_static_identity.standing_hull;
	host_view.host_static_identity.physics.gravity = 100.0f;
	host_view.host_static_identity.physics.ground_acceleration = 10.0f;
	host_view.host_static_identity.physics.air_acceleration = 1.0f;
	host_view.host_static_identity.physics.water_acceleration = 4.0f;
	host_view.host_static_identity.physics.hook_acceleration = 800.0f;
	host_view.host_static_identity.physics.external_acceleration = 2.0f;
	host_view.host_static_identity.physics.water_drag = 0.5f;
	host_view.host_static_identity.physics.max_velocity = 2000.0f;
	host_view.host_static_identity.physics.frame_ms = 100U;
	host_view.host_static_identity.physics.substep_ms = 10U;
	host_view.geometry.bsp_identity =
		host_view.host_static_identity.bsp_identity;
	host_view.geometry.bsp_bytes = host_view.host_static_identity.bsp_bytes;
	host_view.geometry.engine_checksum =
		host_view.host_static_identity.engine_checksum;
#if defined(SG_COMPACT_BUILDER_REAL_BSP)
	host_view.geometry.entity_bytes = 4U;
	host_view.geometry.model_count = 2U;
	host_view.geometry.leaf_count = 2U;
	host_view.geometry.plane_count = 1U;
#else
	host_view.geometry.entity_bytes = 2U;
	host_view.geometry.model_count = 1U;
	host_view.geometry.leaf_count = 2U;
	host_view.geometry.plane_count = 3U;
#endif
	host_view.geometry.node_count = 1U;
	host_view.geometry.texinfo_count = 1U;
	host_view.geometry.leaf_brush_count = 1U;
	host_view.geometry.brush_count = 1U;
	host_view.geometry.brush_side_count = 1U;
	host_view.laws.collision_law_id = UINT64_C(0x3333);
	host_view.laws.pmove_law_id = UINT64_C(0x4444);
	host_view.laws.gravity_law_id = UINT64_C(0x5555);
	host_view.laws.hook_law_id = UINT64_C(0x6666);
	host_view.laws.mechanism_law_id = UINT64_C(0x7777);
	host_view.laws.version = SG_HOST_LAW_PUBLICATION_VERSION;
	host_view.laws.bsp_identity = host_view.host_static_identity.bsp_identity;
	host_view.laws.bsp_bytes = host_view.host_static_identity.bsp_bytes;
	host_view.laws.static_identity = host_view.host_static_identity;
}

static void SetSourceAuthority(void)
{
	source_copy_calls = 0;
	source_drift_on_visibility_audit = 0;
#if defined(SG_COMPACT_BUILDER_REAL_SOURCE_AUTHORITY)
	uint32_t crc = 0U;
	size_t index;

	memset(&source_level_identity, 0, sizeof(source_level_identity));
	source_level_identity.bsp_checksum =
		host_view.host_static_identity.engine_checksum;
	source_level_identity.host_physics_id =
		host_view.host_static_identity.host_physics_epoch;
	source_level_identity.bsp_bytes =
		host_view.host_static_identity.bsp_bytes;
	memcpy(source_level_identity.bsp_sha256,
		host_view.host_static_identity.bsp_identity.bytes,
		SG_LEVEL_BSP_SHA256_BYTES);
	memcpy(source_level_identity.mapname, "command", sizeof("command"));
	if (!SG_CRC32Buffer(source_entity_text, sizeof(source_entity_text) - 1U,
			&crc))
		abort();
	source_level_identity.entity_crc32 = crc;
	host_view.host_static_identity.entity_crc32 = crc;
	host_view.laws.static_identity.entity_crc32 = crc;
	source_host_current = 1;
	source_host_epoch++;
	ctfflags_value.value = 0.0f;
	deathmatch_value.value = 1.0f;
	fastswitch_value.value = 0.0f;
	matchstate = MATCH_INPLAY;
	SG_RuneSourceAuthorityReset();
	if (SG_RuneSourceAuthorityBegin("command", source_entity_text) !=
			SG_RUNE_SOURCE_OK)
		abort();
	for (index = 0U;
		index < sizeof(source_entity_records) /
			sizeof(source_entity_records[0]); index++)
		if (SG_RuneSourceAuthorityRecord(
				source_entity_records[index].source_ordinal,
				source_entity_records[index].effective_spawnflags) !=
				SG_RUNE_SOURCE_OK)
			abort();
	if (SG_RuneSourceAuthorityPublish("command") != SG_RUNE_SOURCE_OK)
		abort();
#else
	memset(&source_snapshot, 0, sizeof(source_snapshot));
	source_snapshot.version = SG_RUNE_SOURCE_AUTHORITY_VERSION;
	source_snapshot.publication_generation = UINT64_C(23);
	source_snapshot.publication_generation_complement =
		~source_snapshot.publication_generation;
	source_snapshot.level_identity.bsp_checksum =
		host_view.host_static_identity.engine_checksum;
	source_snapshot.level_identity.entity_crc32 =
		host_view.host_static_identity.entity_crc32;
	source_snapshot.level_identity.host_physics_id =
		host_view.host_static_identity.host_physics_epoch;
	source_snapshot.level_identity.bsp_bytes =
		host_view.host_static_identity.bsp_bytes;
	memcpy(source_snapshot.level_identity.bsp_sha256,
		host_view.host_static_identity.bsp_identity.bytes,
		SG_LEVEL_BSP_SHA256_BYTES);
	memcpy(source_snapshot.level_identity.mapname, "command",
		sizeof("command"));
	source_snapshot.host_authority.version =
		SG_HOST_LAW_RUNTIME_AUTHORITY_VERSION;
	source_snapshot.host_authority.epoch = UINT64_C(17);
	source_snapshot.host_authority.epoch_complement =
		~source_snapshot.host_authority.epoch;
	source_snapshot.host_authority.view = host_view.laws;
	source_snapshot.weapon_law.weapon_balance_compiled =
		(uint8_t)SG_WEAPON_BALANCE_COMPILED;
	source_snapshot.weapon_law.deathmatch_active = 1U;
	source_snapshot.entity_text_bytes = (uint32_t)sizeof(source_entity_text);
	source_snapshot.entity_record_count = (uint32_t)
		(sizeof(source_entity_records) / sizeof(source_entity_records[0]));
	source_final_status = SG_RUNE_SOURCE_OK;
	source_mutate_second_copy = 0;
	source_mutate_second_weapon = 0;
#endif
}

static void TestDefaultsBuildFromOverrideWithInhibition(void)
{
	struct sg_host_law_construction_s construction = { 1U };
	sg_rune_compact_builder_input_t input = Input(&construction);
	sg_rune_compact_builder_t *builder = NULL;
	sg_rune_compact_builder_error_t error;
	sg_rune_compact_builder_view_t view;

	SetHost();
	SetSourceAuthority();
	read_calls = 0;
	fail_final_read = 0;
	configuration_default_calls = 0;
	semantics_default_calls = 0;
	visibility_default_calls = 0;
	configuration_audit_calls = 0;
	semantics_audit_calls = 0;
	entity_audit_calls = 0;
	visibility_audit_calls = 0;
	CHECK(SG_RuneCompactBuilderBuild(&input, &builder, &error));
	CHECK(builder != NULL);
	CHECK(error.code == SG_RUNE_COMPACT_BUILDER_ERROR_NONE);
	CHECK(configuration_default_calls == 1);
	CHECK(semantics_default_calls == 1);
	CHECK(visibility_default_calls == 1);
	CHECK(configuration_audit_calls == 0);
	CHECK(semantics_audit_calls == 0);
	CHECK(entity_audit_calls == 0);
	CHECK(visibility_audit_calls == 0);
	CHECK(read_calls == 2);
#if !defined(SG_COMPACT_BUILDER_REAL_SOURCE_AUTHORITY)
	CHECK(source_copy_calls == 2);
#endif
	memset(&view, 0, sizeof(view));
	CHECK(SG_RuneCompactBuilderRead(builder, &view));
	CHECK(view.weapon_profile_count ==
		(uint32_t)SG_WEAPON_PROFILE_COUNT - 1U);
	CHECK(view.weapon_profiles != NULL);
	CHECK(view.resolved_weapon_profiles != NULL);
	CHECK(view.identity.source_counts.entity_count == 1U);
	CHECK(view.identity.entity_crc32 ==
		host_view.host_static_identity.entity_crc32);
#if defined(SG_COMPACT_BUILDER_REAL_SOURCE_AUTHORITY)
	{
		uint32_t bsp_entity_crc = 0U;

		CHECK(SG_CRC32Buffer("{}\n", 3U, &bsp_entity_crc));
		CHECK(view.identity.entity_crc32 != bsp_entity_crc);
	}
#endif
	SG_RuneCompactBuilderDestroy(builder);
#if defined(SG_COMPACT_BUILDER_REAL_SOURCE_AUTHORITY)
	SG_RuneSourceAuthorityReset();
#endif
}

static void TestDevelopmentAuditPathIsExplicit(void)
{
	struct sg_host_law_construction_s construction = { 1U };
	sg_rune_compact_builder_input_t input = Input(&construction);
	sg_rune_compact_builder_t *builder = NULL;
	sg_rune_compact_builder_error_t error;

	SetHost();
	SetSourceAuthority();
	read_calls = 0;
	fail_final_read = 0;
	configuration_audit_calls = 0;
	semantics_audit_calls = 0;
	entity_audit_calls = 0;
	visibility_audit_calls = 0;
	CHECK(SG_RuneCompactBuilderBuildDevelopmentAudit(&input, &builder,
		&error));
	CHECK(builder != NULL);
	CHECK(configuration_audit_calls == 1);
	CHECK(semantics_audit_calls == 1);
	CHECK(entity_audit_calls == 1);
	CHECK(visibility_audit_calls == 1);
	SG_RuneCompactBuilderDestroy(builder);
#if defined(SG_COMPACT_BUILDER_REAL_SOURCE_AUTHORITY)
	SG_RuneSourceAuthorityReset();
#endif
}

static void TestSourceDriftFailsClosed(int drift,
	sg_rune_compact_builder_error_code_t expected)
{
	struct sg_host_law_construction_s construction = { 1U };
	sg_rune_compact_builder_input_t input = Input(&construction);
	sg_rune_compact_builder_t *builder = NULL;
	sg_rune_compact_builder_error_t error;

	SetHost();
	SetSourceAuthority();
	read_calls = 0;
	fail_final_read = 0;
	source_drift_on_visibility_audit = drift;
	CHECK(!SG_RuneCompactBuilderBuildDevelopmentAudit(&input, &builder,
		&error));
	CHECK(builder == NULL);
	CHECK(error.code == expected);
#if defined(SG_COMPACT_BUILDER_REAL_SOURCE_AUTHORITY)
	SG_RuneSourceAuthorityReset();
#endif
}

static void TestFinalConstructionDriftFailsClosed(void)
{
	struct sg_host_law_construction_s construction = { 1U };
	sg_rune_compact_builder_input_t input = Input(&construction);
	sg_rune_compact_builder_t *builder = NULL;
	sg_rune_compact_builder_error_t error;

	SetHost();
	SetSourceAuthority();
	read_calls = 0;
	fail_final_read = 1;
	CHECK(!SG_RuneCompactBuilderBuild(&input, &builder, &error));
	CHECK(builder == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_BUILDER_ERROR_HOST_AUTHORITY);
	fail_final_read = 0;
#if defined(SG_COMPACT_BUILDER_REAL_SOURCE_AUTHORITY)
	SG_RuneSourceAuthorityReset();
#endif
}

static void TestInvalidArgumentsDoNotReadHost(void)
{
	struct sg_host_law_construction_s construction = { 1U };
	sg_rune_compact_builder_input_t input = Input(&construction);
	sg_rune_compact_builder_t *builder = NULL;
	sg_rune_compact_builder_t *occupied =
		(sg_rune_compact_builder_t *)(void *)&construction;
	sg_rune_compact_builder_error_t error;

	read_calls = 0;
	input.construction = NULL;
	CHECK(!SG_RuneCompactBuilderBuild(&input, &builder, &error));
	CHECK(error.code == SG_RUNE_COMPACT_BUILDER_ERROR_INVALID_ARGUMENT);
	CHECK(read_calls == 0);
	input = Input(&construction);
	CHECK(!SG_RuneCompactBuilderBuild(&input, NULL, &error));
	CHECK(error.code == SG_RUNE_COMPACT_BUILDER_ERROR_INVALID_ARGUMENT);
	CHECK(read_calls == 0);
	CHECK(!SG_RuneCompactBuilderBuild(&input, &occupied, &error));
	CHECK(error.code == SG_RUNE_COMPACT_BUILDER_ERROR_INVALID_ARGUMENT);
	CHECK(read_calls == 0);
}

static void TestIdentityMismatchFailsClosed(void)
{
	struct sg_host_law_construction_s construction = { 1U };
	sg_rune_compact_builder_input_t input = Input(&construction);
	sg_rune_compact_builder_t *builder = NULL;
	sg_rune_compact_builder_error_t error;

	SetHost();
	host_view.laws.hook_law_id = 0U;
	read_calls = 0;
	fail_final_read = 0;
	CHECK(!SG_RuneCompactBuilderBuild(&input, &builder, &error));
	CHECK(builder == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_BUILDER_ERROR_IDENTITY_MISMATCH);
}

#if !defined(SG_COMPACT_BUILDER_REAL_BSP)
static void TestDependencyAllocationFailuresRemainOom(void)
{
	struct sg_host_law_construction_s construction = { 1U };
	const dependency_failure_t failures_to_try[] = {
		FAILURE_BSP_LOAD,
		FAILURE_CONFIGURATION_BUILD,
		FAILURE_CONFIGURATION_AUDIT,
		FAILURE_SEMANTICS_BUILD,
		FAILURE_SEMANTICS_AUDIT,
		FAILURE_ENTITY_BUILD,
		FAILURE_ENTITY_AUDIT,
		FAILURE_VISIBILITY_BUILD,
		FAILURE_VISIBILITY_AUDIT
	};
	size_t index;

	for (index = 0U;
		index < sizeof(failures_to_try) / sizeof(failures_to_try[0]); index++) {
		sg_rune_compact_builder_input_t input = Input(&construction);
		sg_rune_compact_builder_t *builder = NULL;
		sg_rune_compact_builder_error_t error;

		SetHost();
		SetSourceAuthority();
		read_calls = 0;
		fail_final_read = 0;
		dependency_failure = failures_to_try[index];
		CHECK(!SG_RuneCompactBuilderBuildDevelopmentAudit(&input, &builder,
			&error));
		CHECK(builder == NULL);
		CHECK(error.code == SG_RUNE_COMPACT_BUILDER_ERROR_OUT_OF_MEMORY);
	}
	dependency_failure = FAILURE_NONE;
}
#endif

int main(void)
{
	TestDefaultsBuildFromOverrideWithInhibition();
	TestDevelopmentAuditPathIsExplicit();
	TestInvalidArgumentsDoNotReadHost();
	TestIdentityMismatchFailsClosed();
	TestSourceDriftFailsClosed(1,
		SG_RUNE_COMPACT_BUILDER_ERROR_ENTITY_AUTHORITY);
	TestSourceDriftFailsClosed(2,
		SG_RUNE_COMPACT_BUILDER_ERROR_WEAPON_AUTHORITY);
#if !defined(SG_COMPACT_BUILDER_REAL_SOURCE_AUTHORITY)
	TestSourceDriftFailsClosed(3,
		SG_RUNE_COMPACT_BUILDER_ERROR_ENTITY_AUTHORITY);
	TestSourceDriftFailsClosed(4,
		SG_RUNE_COMPACT_BUILDER_ERROR_WEAPON_AUTHORITY);
#endif
	TestFinalConstructionDriftFailsClosed();
#if !defined(SG_COMPACT_BUILDER_REAL_BSP)
	TestDependencyAllocationFailuresRemainOom();
#endif
	CHECK(strcmp(SG_RuneCompactBuilderErrorString(
		SG_RUNE_COMPACT_BUILDER_ERROR_ENTITY_AUTHORITY),
		"runtime entity source authority unavailable") == 0);
	CHECK(strcmp(SG_RuneCompactBuilderErrorString(
		SG_RUNE_COMPACT_BUILDER_ERROR_WEAPON_AUTHORITY),
		"weapon source authority unavailable") == 0);
	CHECK(strcmp(SG_RuneCompactBuilderErrorString(
		SG_RUNE_COMPACT_BUILDER_ERROR_WEAPON_PROFILE),
		"weapon profile resolution failed") == 0);
	CHECK(strcmp(SG_RuneCompactBuilderErrorString(
		SG_RUNE_COMPACT_BUILDER_ERROR_CODE_COUNT),
		"unknown compact builder error") == 0);
	if (failures != 0) {
		fprintf(stderr, "%d compact builder checks failed\n", failures);
		return 1;
	}
	puts("compact BSP source builder checks passed");
	return 0;
}
