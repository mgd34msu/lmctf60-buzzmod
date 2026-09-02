#if defined(SG_COMPACT_BUILDER_REAL_SOURCE_AUTHORITY)
#include "g_local.h"
#undef world
#include "g_tourney.h"
#include "slipgate/sg_crc32.h"
#include "slipgate/sg_rune_source_authority_owner.h"
#endif

#include "slipgate/sg_rune_compact_builder.h"
#include "slipgate/sg_rune_compact_builder_owner.h"
#include "slipgate/sg_host_law_publication_private.h"
#include "slipgate/sg_rune_source_authority.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct sg_host_law_construction_s
{
	uint32_t marker;
};

struct sg_host_law_pmove_evaluator_s
{
	uint32_t marker;
};

static int failures;
static int read_calls;
static int fail_final_read;
static int configuration_default_calls;
static int semantics_default_calls;
static int visibility_default_calls;
static int entity_audit_calls;
static int visibility_audit_calls;
static sg_host_law_construction_view_t host_view;
static int source_copy_calls;
static int source_drift_on_visibility_audit;
static int pmove_evaluator_acquire_calls;
static int pmove_evaluator_run_calls;
static int pmove_evaluator_destroy_calls;
static sg_host_law_status_t pmove_evaluator_current_status =
	SG_HOST_LAW_OK;
#if !defined(SG_COMPACT_BUILDER_REAL_ENTITY_SEMANTICS)
static int fake_invalid_mover_model;
static sg_rune_mechanism_kind_t fake_mover_kind;
static sg_mech_node_kind_t fake_mover_role;
static sg_bsp_entity_angular_mover_kind_t fake_angular_mover_kind;
static sg_bsp_entity_semantics_t *fake_last_entity_semantics;
static uint32_t fake_mover_spawnflags;
static float fake_mover_move_direction_x;
static float fake_mover_origin_x;
static float fake_mover_height;
static int fake_train_graph;
static int fake_train_graph_malformed;
static int fake_door_team;
static int fake_door_team_malformed;
#endif
static int fake_model_trace_allsolid;
static float fake_model_trace_origin_x;
static int fake_carried_support_enabled;
static int fake_geometry_read_enabled;
static int fake_identity_matches;
static sg_rune_compact_geometry_view_t fake_geometry_view;

static const char source_entity_text[] =
	"{\n\"classname\" \"worldspawn\"\n\"message\" \"selected override\"\n}\n"
	"{\n\"classname\" \"trigger_once\"\n\"model\" \"*1\"\n}\n"
	"{\n\"classname\" \"info_player_start\"\n}\n";
static const sg_rune_source_entity_record_t source_entity_records[] = {
	{ 0U, 0 },
	{ 2U, 0 }
};
#if !defined(SG_COMPACT_BUILDER_REAL_SOURCE_AUTHORITY)
static sg_rune_source_entity_record_t fake_source_entity_records[5];
static size_t fake_source_entity_record_count;
#endif

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
	FAILURE_SEMANTICS_BUILD,
	FAILURE_ENTITY_BUILD,
	FAILURE_ENTITY_AUDIT,
	FAILURE_VISIBILITY_BUILD,
	FAILURE_VISIBILITY_AUDIT,
	FAILURE_PMOVE_EVALUATOR_ACQUIRE
} dependency_failure_t;

static dependency_failure_t dependency_failure;

const sg_rune_v2_content_id_t SG_BSP_ENTITY_SEMANTICS_SCHEMA_ID = {
	{
		0x53U, 0x47U, 0x2dU, 0x42U, 0x53U, 0x50U, 0x2dU, 0x45U,
		0x4eU, 0x54U, 0x49U, 0x54U, 0x59U, 0x2dU, 0x53U, 0x45U,
		0x4dU, 0x41U, 0x4eU, 0x54U, 0x49U, 0x43U, 0x53U, 0x2dU,
		0x56U, 0x32U, 0U, 0U, 0U, 0U, 0U, 2U
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
	*entity_record_count_out = fake_source_entity_record_count;
	return SG_RUNE_SOURCE_OK;
}

sg_rune_source_status_t SG_RuneSourceAuthorityCopy(
	const sg_rune_source_authority_t *authority,
	sg_rune_source_snapshot_t *snapshot_out,
	char *entity_text_out, size_t entity_text_capacity,
	sg_rune_source_entity_record_t *entity_records_out,
	size_t entity_record_capacity)
{
	const size_t record_count = fake_source_entity_record_count;

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
	memcpy(entity_records_out, fake_source_entity_records,
		record_count * sizeof(*fake_source_entity_records));
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

sg_host_law_result_t SG_HostLawConstructionOwnerPmoveEvaluatorAcquire(
	const sg_host_law_construction_t *construction,
	sg_host_law_pmove_evaluator_t **evaluator_out)
{
	sg_host_law_pmove_evaluator_t *evaluator;

	if (!construction || !evaluator_out || *evaluator_out)
		return HostResult(SG_HOST_LAW_INVALID_ARGUMENT);
	pmove_evaluator_acquire_calls++;
	if (dependency_failure == FAILURE_PMOVE_EVALUATOR_ACQUIRE)
		return HostResult(SG_HOST_LAW_ALLOCATION_FAILED);
	evaluator = calloc(1U, sizeof(*evaluator));
	if (!evaluator)
		return HostResult(SG_HOST_LAW_ALLOCATION_FAILED);
	evaluator->marker = construction->marker;
	*evaluator_out = evaluator;
	return HostResult(SG_HOST_LAW_OK);
}

sg_host_law_result_t SG_HostLawPmoveEvaluatorCurrent(
	const sg_host_law_pmove_evaluator_t *evaluator)
{
	if (!evaluator)
		return HostResult(SG_HOST_LAW_INVALID_ARGUMENT);
	return HostResult(pmove_evaluator_current_status);
}

sg_host_law_result_t SG_HostLawPmoveEvaluatorRun(
	const sg_host_law_pmove_evaluator_t *evaluator,
	const sg_host_collision_authority_t *authority,
	const sg_host_collision_scene_t *scene,
	const sg_host_pmove_request_t *request,
	sg_host_pmove_result_t *result_out,
	sg_host_pmove_error_t *error_out)
{
	(void)scene;
	if (!evaluator || !authority || !request || !result_out)
		return HostResult(SG_HOST_LAW_INVALID_ARGUMENT);
	pmove_evaluator_run_calls++;
	memset(result_out, 0, sizeof(*result_out));
	if (error_out)
		*error_out = SG_HOST_PMOVE_ERROR_NONE;
	return HostResult(SG_HOST_LAW_OK);
}

sg_host_law_result_t SG_HostLawPmoveEvaluatorReplayFrame(
	const sg_host_law_pmove_evaluator_t *evaluator,
	const sg_host_collision_authority_t *authority,
	const sg_host_collision_scene_t *scene,
	const sg_host_pmove_request_t *request,
	const sg_host_pmove_replay_workspace_t *workspace,
	sg_host_pmove_replay_t *replay_out,
	sg_host_pmove_error_t *error_out)
{
	(void)evaluator;
	(void)authority;
	(void)scene;
	(void)request;
	(void)workspace;
	(void)replay_out;
	(void)error_out;
	return HostResult(SG_HOST_LAW_INVALID_ARGUMENT);
}

void SG_HostLawPmoveEvaluatorDestroy(
	sg_host_law_pmove_evaluator_t *evaluator)
{
	if (evaluator)
		pmove_evaluator_destroy_calls++;
	free(evaluator);
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
	world->models = calloc(host_view.geometry.model_count,
		sizeof(*world->models));
	world->planes = calloc(host_view.geometry.plane_count,
		sizeof(*world->planes));
	world->brushes = calloc(host_view.geometry.brush_count,
		sizeof(*world->brushes));
	world->brush_sides = calloc(host_view.geometry.brush_side_count,
		sizeof(*world->brush_sides));
	if (!world->source_bytes || !world->models || !world->planes ||
		!world->brushes || !world->brush_sides) {
		free(world->brush_sides);
		free(world->brushes);
		free(world->planes);
		free(world->models);
		free(world->source_bytes);
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
	world->planes[0].normal.value[2] = 1.0f;
	world->planes[0].distance = 1.0f;
	world->planes[1].normal.value[2] = -1.0f;
	world->planes[1].distance = 10.0f;
	world->planes[2].normal.value[2] = -1.0f;
	world->planes[2].distance = -10.0f;
	world->brushes[0].first_side = 0U;
	world->brushes[0].side_count = 2U;
	world->brushes[0].contents = SG_HOST_CONTENTS_SOLID;
	world->brushes[1].first_side = 2U;
	world->brushes[1].side_count = 1U;
	world->brushes[1].contents = SG_HOST_CONTENTS_SOLID;
	world->brush_sides[0].plane = 0U;
	world->brush_sides[1].plane = 1U;
	world->brush_sides[2].plane = 2U;
	if (world->model_count > 1U) {
		world->models[1].mins.value[0] = -8.0f;
		world->models[1].mins.value[1] = -8.0f;
		world->models[1].mins.value[2] = -8.0f;
		world->models[1].maxs.value[0] = 8.0f;
		world->models[1].maxs.value[1] = 8.0f;
		world->models[1].maxs.value[2] = 8.0f;
	}
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
	free(world->brush_sides);
	free(world->brushes);
	free(world->planes);
	free(world->models);
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

/* The focused builder unit fixture owns no collision world.  Keep this link
 * shim algebraically identical to the collision forward transform; the
 * production collision implementation is separately differential-tested. */
int SG_HostCollisionWorldTransform(
	const sg_host_collision_transform_t *transform,
	sg_host_collision_world_transform_t *world_transform_out)
{
	const double degrees_to_radians = 0.01745329251994329576923690768489;
	float sy;
	float cy;
	float sp;
	float cp;
	float sr;
	float cr;
	uint32_t local_axis;
	uint32_t world_axis;

	if (!transform || !world_transform_out)
		return 0;
	for (world_axis = 0U; world_axis < 3U; world_axis++)
		if (!isfinite(transform->origin[world_axis]) ||
			!isfinite(transform->angles[world_axis]))
			return 0;
	sy = (float)sin((double)(float)((double)transform->angles[1] * degrees_to_radians));
	cy = (float)cos((double)(float)((double)transform->angles[1] * degrees_to_radians));
	sp = (float)sin((double)(float)((double)transform->angles[0] * degrees_to_radians));
	cp = (float)cos((double)(float)((double)transform->angles[0] * degrees_to_radians));
	sr = (float)sin((double)(float)((double)transform->angles[2] * degrees_to_radians));
	cr = (float)cos((double)(float)((double)transform->angles[2] * degrees_to_radians));
	world_transform_out->axis[0][0] = cp * cy;
	world_transform_out->axis[0][1] = cp * sy;
	world_transform_out->axis[0][2] = -sp;
	world_transform_out->axis[1][0] = sr * sp * cy - cr * sy;
	world_transform_out->axis[1][1] = sr * sp * sy + cr * cy;
	world_transform_out->axis[1][2] = sr * cp;
	world_transform_out->axis[2][0] = cr * sp * cy + sr * sy;
	world_transform_out->axis[2][1] = cr * sp * sy - sr * cy;
	world_transform_out->axis[2][2] = cr * cp;
	for (world_axis = 0U; world_axis < 3U; world_axis++) {
		world_transform_out->origin[world_axis] =
			transform->origin[world_axis];
		if (world_transform_out->origin[world_axis] == 0.0f)
			world_transform_out->origin[world_axis] = 0.0f;
		for (local_axis = 0U; local_axis < 3U; local_axis++)
			if (world_transform_out->axis[local_axis][world_axis] == 0.0f)
				world_transform_out->axis[local_axis][world_axis] = 0.0f;
	}
	return 1;
}

int SG_HostCollisionPusherCarry(
	const sg_host_collision_transform_t *pusher_transform,
	const float move[3], const float amove[3], const float rider_start[3],
	float rider_end_out[3])
{
	const double degrees_to_radians = 0.01745329251994329576923690768489;
	float inverse[3];
	float axis[3][3];
	float translated[3];
	float relative[3];
	float rotated[3];
	float sy, cy, sp, cp, sr, cr;
	uint32_t coordinate;

	if (!pusher_transform || !move || !amove || !rider_start ||
		!rider_end_out)
		return 0;
	for (coordinate = 0U; coordinate < 3U; coordinate++) {
		if (!isfinite(pusher_transform->origin[coordinate]) ||
			!isfinite(move[coordinate]) || !isfinite(amove[coordinate]) ||
			!isfinite(rider_start[coordinate]))
			return 0;
		inverse[coordinate] = -amove[coordinate];
		translated[coordinate] = rider_start[coordinate] + move[coordinate];
		relative[coordinate] = translated[coordinate] -
			(pusher_transform->origin[coordinate] + move[coordinate]);
	}
	sy = (float)sin((double)(float)((double)inverse[1] * degrees_to_radians));
	cy = (float)cos((double)(float)((double)inverse[1] * degrees_to_radians));
	sp = (float)sin((double)(float)((double)inverse[0] * degrees_to_radians));
	cp = (float)cos((double)(float)((double)inverse[0] * degrees_to_radians));
	sr = (float)sin((double)(float)((double)inverse[2] * degrees_to_radians));
	cr = (float)cos((double)(float)((double)inverse[2] * degrees_to_radians));
	axis[0][0] = cp * cy; axis[0][1] = cp * sy; axis[0][2] = -sp;
	axis[1][0] = sr * sp * cy - cr * sy;
	axis[1][1] = sr * sp * sy + cr * cy; axis[1][2] = sr * cp;
	axis[2][0] = cr * sp * cy + sr * sy;
	axis[2][1] = cr * sp * sy - sr * cy; axis[2][2] = cr * cp;
	for (coordinate = 0U; coordinate < 3U; coordinate++) {
		rotated[coordinate] = relative[0] * axis[coordinate][0] +
			relative[1] * axis[coordinate][1] +
			relative[2] * axis[coordinate][2];
		rider_end_out[coordinate] = translated[coordinate] +
			(rotated[coordinate] - relative[coordinate]);
		if (!isfinite(rider_end_out[coordinate]))
			return 0;
		if (rider_end_out[coordinate] == 0.0f)
			rider_end_out[coordinate] = 0.0f;
	}
	return 1;
}

int SG_HostCollisionModelToWorldPoint(
	const sg_host_collision_authority_t *authority, uint32_t model_index,
	const sg_host_collision_transform_t *transform, const float local[3],
	float world_out[3])
{
	const double degrees_to_radians = 0.01745329251994329576923690768489;
	float axis[3][3];
	float source[3];
	float sy;
	float cy;
	float sp;
	float cp;
	float sr;
	float cr;
	uint32_t coordinate;

	if (!authority || !authority->world || !transform || !local ||
		!world_out || model_index == 0U ||
		model_index >= authority->world->model_count)
		return 0;
	for (coordinate = 0U; coordinate < 3U; coordinate++)
		if (!isfinite(local[coordinate]) || !isfinite(transform->origin[coordinate]) ||
			!isfinite(transform->angles[coordinate]))
			return 0;
	sy = (float)sin((double)(float)((double)transform->angles[1] * degrees_to_radians));
	cy = (float)cos((double)(float)((double)transform->angles[1] * degrees_to_radians));
	sp = (float)sin((double)(float)((double)transform->angles[0] * degrees_to_radians));
	cp = (float)cos((double)(float)((double)transform->angles[0] * degrees_to_radians));
	sr = (float)sin((double)(float)((double)transform->angles[2] * degrees_to_radians));
	cr = (float)cos((double)(float)((double)transform->angles[2] * degrees_to_radians));
	axis[0][0] = cp * cy;
	axis[0][1] = cp * sy;
	axis[0][2] = -sp;
	axis[1][0] = sr * sp * cy - cr * sy;
	axis[1][1] = sr * sp * sy + cr * cy;
	axis[1][2] = sr * cp;
	axis[2][0] = cr * sp * cy + sr * sy;
	axis[2][1] = cr * sp * sy - sr * cy;
	axis[2][2] = cr * cp;
	memcpy(source, local, sizeof(source));
	for (coordinate = 0U; coordinate < 3U; coordinate++)
	{
		world_out[coordinate] = source[0] * axis[0][coordinate] +
			source[1] * axis[1][coordinate] +
			source[2] * axis[2][coordinate] +
			transform->origin[coordinate];
		if (!isfinite(world_out[coordinate]))
			return 0;
		if (world_out[coordinate] == 0.0f)
			world_out[coordinate] = 0.0f;
	}
	return 1;
}

int SG_HostCollisionClassifyPose(
	const sg_host_collision_authority_t *authority,
	const sg_host_collision_scene_t *scene, const float origin[3],
	sg_rune_stance_t stance, sg_host_collision_pose_t *pose_out)
{
	if (!authority || !scene || scene->instance_count != 1U ||
		!scene->instances || !origin || !pose_out ||
		!fake_carried_support_enabled)
		return 0;
	memset(pose_out, 0, sizeof(*pose_out));
	pose_out->valid = 1;
	pose_out->stance = stance;
	pose_out->supported = 1;
	pose_out->support.instance_id = scene->instances[0].instance_id;
	return 1;
}

int SG_HostCollisionModelPositiveAreaPolygonOverlap(
	const sg_host_collision_authority_t *authority, uint32_t model_index,
	const sg_host_collision_transform_t *transform,
	const sg_rune_vec3_t *world_vertices, uint32_t world_vertex_count,
	sg_host_collision_contents_t mask, int *overlap_out)
{
	const int blocked = fake_model_trace_allsolid && authority && transform &&
		isfinite(fake_model_trace_origin_x) &&
		transform->origin[0] == fake_model_trace_origin_x;

	(void)model_index;
	(void)world_vertices;
	(void)world_vertex_count;
	(void)mask;
	if (overlap_out == NULL)
		return 0;
	*overlap_out = blocked;
	return 1;
}

int SG_HostCollisionTraceModel(
	const sg_host_collision_authority_t *authority, uint32_t model_index,
	const sg_host_collision_transform_t *transform, const float start[3],
	const float mins[3], const float maxs[3], const float end[3],
	sg_host_collision_contents_t mask, sg_host_collision_trace_t *trace_out)
{
	const int blocked = fake_model_trace_allsolid && authority && transform &&
		isfinite(fake_model_trace_origin_x) &&
		transform->origin[0] == fake_model_trace_origin_x;

	(void)model_index;
	(void)start;
	(void)mins;
	(void)maxs;
	(void)end;
	(void)mask;
	if (trace_out == NULL)
		return 0;
	memset(trace_out, 0, sizeof(*trace_out));
	trace_out->fraction = blocked ? 0.0f : 1.0f;
	trace_out->startsolid = blocked;
	trace_out->allsolid = blocked;
	return 1;
}

int SG_HostCollisionTransition(
	const sg_host_collision_authority_t *authority,
	const sg_host_collision_scene_t *scene, const float start[3],
	const float end[3], sg_rune_stance_t stance,
	sg_host_collision_transition_t *transition_out)
{
	(void)scene;
	(void)start;
	(void)end;
	(void)stance;
	if (!authority || !transition_out || !fake_carried_support_enabled)
		return 0;
	memset(transition_out, 0, sizeof(*transition_out));
	transition_out->source_valid = 1;
	transition_out->destination_valid = 1;
	transition_out->clear = 1;
	transition_out->sweep.fraction = 1.0f;
	return 1;
}

int SG_RuneCompactGeometryRead(const sg_rune_compact_geometry_t *geometry,
	sg_rune_compact_geometry_view_t *view_out)
{
	if (!geometry || !view_out || !fake_geometry_read_enabled)
		return 0;
	*view_out = fake_geometry_view;
	return 1;
}

int SG_RuneCompactIdentityMatches(
	const sg_rune_compact_identity_t *actual,
	const sg_rune_compact_identity_t *expected)
{
	(void)actual;
	(void)expected;
	return fake_identity_matches;
}

void SG_ConfigurationDefaultLimits(sg_configuration_limits_t *limits_out)
{
	configuration_default_calls++;
	memset(limits_out, UINT8_MAX, sizeof(*limits_out));
}

int SG_ConfigurationBuildWithProgress(
	const sg_host_collision_authority_t *authority,
	const sg_configuration_limits_t *limits,
	sg_configuration_progress_fn progress, void *progress_context,
	sg_configuration_space_t **space_out, sg_configuration_error_t *error_out)
{
	(void)progress;
	(void)progress_context;
	return SG_ConfigurationBuild(authority, limits, space_out, error_out);
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
		(*semantics_out)->entities[index].source_entity_ordinal =
			survivors[index + 1U].source_ordinal;
		(*semantics_out)->entities[index].canonical_ordinal = (uint32_t)index;
		(*semantics_out)->entities[index].bsp_model =
			fake_invalid_mover_model ? 2U : 1U;
		(*semantics_out)->entities[index].flags =
			SG_BSP_ENTITY_HAS_BRUSH_MODEL |
			(fake_mover_kind == SG_RUNE_MECHANISM_LIFT ?
				SG_BSP_ENTITY_HEIGHT_DEFINED : 0U);
		(*semantics_out)->entities[index].mechanism_kind = fake_mover_kind;
		(*semantics_out)->entities[index].mechanism_role = fake_mover_role;
		(*semantics_out)->entities[index].spawnflags = fake_mover_spawnflags;
		(*semantics_out)->entities[index].height = fake_mover_height;
		(*semantics_out)->entities[index].origin.value[0] = fake_mover_origin_x;
		(*semantics_out)->entities[index].origin.value[1] = 50.0f;
		(*semantics_out)->entities[index].origin.value[2] = 5.0f;
		(*semantics_out)->entities[index].angles.value[1] = 90.0f;
		(*semantics_out)->entities[index].move_direction.value[0] =
			fake_mover_move_direction_x;
	}
	if (fake_angular_mover_kind != SG_BSP_ENTITY_ANGULAR_MOVER_NONE) {
		sg_bsp_entity_semantic_t *mover = &(*semantics_out)->entities[0];

		mover->mechanism_kind = SG_RUNE_MECHANISM_ROTATOR;
		mover->mechanism_role = SG_MECH_NODE_DOOR_MASTER;
		mover->angular_mover.kind = fake_angular_mover_kind;
		if (fake_angular_mover_kind ==
			SG_BSP_ENTITY_ANGULAR_MOVER_FINITE_DOOR) {
			mover->angular_mover.flags =
				SG_BSP_ENTITY_ANGULAR_MOVER_START_OPEN;
			mover->angular_mover.schedule.finite_door.inactive_angles.value[1] =
				-90.0f;
			mover->angular_mover.schedule.finite_door.active_angles.value[1] =
				0.0f;
			mover->angular_mover.schedule.finite_door.axis.value[1] = 1.0f;
			mover->angular_mover.schedule.finite_door.angular_displacement.value[1] =
				90.0f;
			mover->angular_mover.schedule.finite_door.speed = 100.0f;
			mover->angular_mover.schedule.finite_door.acceleration = 100.0f;
			mover->angular_mover.schedule.finite_door.deceleration = 100.0f;
			mover->angular_mover.schedule.finite_door.frame_ms = 100U;
		}
		else {
			mover->angular_mover.schedule.continuous_rotator.initial_angles.value[1] =
				90.0f;
			mover->angular_mover.schedule.continuous_rotator.axis.value[1] =
				1.0f;
			mover->angular_mover.schedule.continuous_rotator.angular_velocity.value[1] =
				100.0f;
			mover->angular_mover.schedule.continuous_rotator.frame_angular_delta.value[1] =
				10.0f;
			mover->angular_mover.schedule.continuous_rotator.speed = 100.0f;
			mover->angular_mover.schedule.continuous_rotator.frame_ms = 100U;
		}
	}
	if (fake_train_graph) {
		if (semantic_count != 4U) {
			free((*semantics_out)->strings);
			free((*semantics_out)->entities);
			free(*semantics_out);
			*semantics_out = NULL;
			error_out->code = SG_BSP_ENTITY_SEMANTICS_ERROR_INVALID_ARGUMENT;
			return 0;
		}
		/* Keep two distinct ordinal branches from the same source/destination:
		 * the builder boundary must bind the selected fanout, not merely the
		 * endpoint pair. */
		(*semantics_out)->edges = calloc(5U,
			sizeof(*(*semantics_out)->edges));
		if ((*semantics_out)->edges == NULL) {
			free((*semantics_out)->strings);
			free((*semantics_out)->entities);
			free(*semantics_out);
			*semantics_out = NULL;
			error_out->code = SG_BSP_ENTITY_SEMANTICS_ERROR_OUT_OF_MEMORY;
			return 0;
		}
		(*semantics_out)->entities[0].mechanism_kind =
			SG_RUNE_MECHANISM_TRAIN;
		(*semantics_out)->entities[0].mechanism_role = SG_MECH_NODE_TRAIN;
		for (index = 1U; index < semantic_count; index++) {
			(*semantics_out)->entities[index].flags = 0U;
			(*semantics_out)->entities[index].bsp_model =
				SG_BSP_ENTITY_MODEL_NONE;
			(*semantics_out)->entities[index].mechanism_kind =
				SG_RUNE_MECHANISM_TRAIN;
			(*semantics_out)->entities[index].mechanism_role =
				SG_MECH_NODE_PATH_CORNER;
		}
		(*semantics_out)->edges[0].source = 0U;
		(*semantics_out)->edges[0].destination = 1U;
		(*semantics_out)->edges[0].kind = SG_MECH_EDGE_TARGET;
		(*semantics_out)->edges[1].source = 1U;
		(*semantics_out)->edges[1].destination = 2U;
		(*semantics_out)->edges[1].kind = SG_MECH_EDGE_TARGET;
		(*semantics_out)->edges[1].fanout_ordinal = 3U;
		(*semantics_out)->edges[2].source = 2U;
		(*semantics_out)->edges[2].destination = 3U;
		(*semantics_out)->edges[2].kind = SG_MECH_EDGE_TARGET;
		(*semantics_out)->edges[2].fanout_ordinal = 5U;
		(*semantics_out)->edges[3].source = 3U;
		(*semantics_out)->edges[3].destination = 1U;
		(*semantics_out)->edges[3].kind = SG_MECH_EDGE_TARGET;
		(*semantics_out)->edges[3].fanout_ordinal = 7U;
		(*semantics_out)->edges[4].source = 3U;
		(*semantics_out)->edges[4].destination = 1U;
		(*semantics_out)->edges[4].kind = SG_MECH_EDGE_TARGET;
		(*semantics_out)->edges[4].fanout_ordinal = 8U;
		if (fake_train_graph_malformed)
			(*semantics_out)->edges[1].destination = 0U;
		(*semantics_out)->edge_count = 5U;
	}
	if (fake_door_team) {
		if (semantic_count != 2U) {
			free((*semantics_out)->strings);
			free((*semantics_out)->entities);
			free(*semantics_out);
			*semantics_out = NULL;
			error_out->code = SG_BSP_ENTITY_SEMANTICS_ERROR_INVALID_ARGUMENT;
			return 0;
		}
		(*semantics_out)->edges = calloc(1U, sizeof(*(*semantics_out)->edges));
		if ((*semantics_out)->edges == NULL) {
			free((*semantics_out)->strings);
			free((*semantics_out)->entities);
			free(*semantics_out);
			*semantics_out = NULL;
			error_out->code = SG_BSP_ENTITY_SEMANTICS_ERROR_OUT_OF_MEMORY;
			return 0;
		}
		(*semantics_out)->entities[0].mechanism_kind = SG_RUNE_MECHANISM_DOOR;
		(*semantics_out)->entities[0].mechanism_role = SG_MECH_NODE_DOOR_MASTER;
		(*semantics_out)->entities[1].mechanism_kind = SG_RUNE_MECHANISM_DOOR;
		(*semantics_out)->entities[1].mechanism_role = SG_MECH_NODE_DOOR_MASTER;
		(*semantics_out)->entities[1].origin.value[0] = 70.0f;
		(*semantics_out)->edges[0].source = fake_door_team_malformed ? 0U : 1U;
		(*semantics_out)->edges[0].destination = 0U;
		(*semantics_out)->edges[0].kind = SG_MECH_EDGE_TEAM;
		(*semantics_out)->edge_count = 1U;
	}
	(*semantics_out)->entity_count = (uint32_t)semantic_count;
	(*semantics_out)->string_bytes = 1U;
	fake_last_entity_semantics = *semantics_out;
	return 1;
}

const sg_bsp_entity_angular_mover_t *SG_BspEntitySemanticsAngularMover(
	const sg_bsp_entity_semantics_t *semantics, uint32_t canonical_ordinal)
{
	const sg_bsp_entity_semantic_t *entity;

	if (!semantics || canonical_ordinal >= semantics->entity_count)
		return NULL;
	entity = &semantics->entities[canonical_ordinal];
	if (entity->canonical_ordinal != canonical_ordinal ||
		entity->angular_mover.kind == SG_BSP_ENTITY_ANGULAR_MOVER_NONE ||
		entity->angular_mover.kind >= SG_BSP_ENTITY_ANGULAR_MOVER_KIND_COUNT)
		return NULL;
	return &entity->angular_mover;
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
	if (fake_last_entity_semantics == semantics)
		fake_last_entity_semantics = NULL;
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
#if !defined(SG_COMPACT_BUILDER_REAL_ENTITY_SEMANTICS)
	fake_invalid_mover_model = 0;
	fake_mover_kind = SG_RUNE_MECHANISM_DOOR;
	fake_mover_role = SG_MECH_NODE_DOOR_MASTER;
	fake_angular_mover_kind = SG_BSP_ENTITY_ANGULAR_MOVER_NONE;
	fake_mover_spawnflags = 0U;
	fake_mover_move_direction_x = 0.0f;
	fake_mover_origin_x = 50.0f;
	fake_mover_height = 0.0f;
	fake_train_graph = 0;
	fake_train_graph_malformed = 0;
	fake_door_team = 0;
	fake_door_team_malformed = 0;
#endif
	fake_model_trace_allsolid = 0;
	fake_model_trace_origin_x = INFINITY;
	fake_carried_support_enabled = 0;
	fake_geometry_read_enabled = 0;
	fake_identity_matches = 0;
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
	host_view.geometry.model_count = 2U;
	host_view.geometry.leaf_count = 2U;
	host_view.geometry.plane_count = 3U;
#endif
	host_view.geometry.node_count = 1U;
	host_view.geometry.texinfo_count = 1U;
	host_view.geometry.leaf_brush_count = 1U;
#if defined(SG_COMPACT_BUILDER_REAL_BSP)
	host_view.geometry.brush_count = 1U;
	host_view.geometry.brush_side_count = 1U;
#else
	host_view.geometry.brush_count = 2U;
	host_view.geometry.brush_side_count = 3U;
#endif
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
	fake_source_entity_record_count = sizeof(source_entity_records) /
		sizeof(source_entity_records[0]);
	memcpy(fake_source_entity_records, source_entity_records,
		sizeof(source_entity_records));
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
	source_snapshot.entity_record_count = (uint32_t)fake_source_entity_record_count;
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
	sg_rune_compact_builder_owner_view_t owner_view;
	sg_host_pmove_request_t pmove_request;
	sg_host_pmove_result_t pmove_result;
	sg_host_pmove_error_t pmove_error;
	sg_host_law_result_t pmove_law_result;

	SetHost();
	SetSourceAuthority();
	read_calls = 0;
	fail_final_read = 0;
	configuration_default_calls = 0;
	semantics_default_calls = 0;
	visibility_default_calls = 0;
	entity_audit_calls = 0;
	visibility_audit_calls = 0;
	pmove_evaluator_acquire_calls = 0;
	pmove_evaluator_run_calls = 0;
	pmove_evaluator_destroy_calls = 0;
	CHECK(SG_RuneCompactBuilderBuild(&input, &builder, &error));
	CHECK(builder != NULL);
	CHECK(error.code == SG_RUNE_COMPACT_BUILDER_ERROR_NONE);
	CHECK(configuration_default_calls == 1);
	CHECK(semantics_default_calls == 1);
	CHECK(visibility_default_calls == 1);
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
	memset(&owner_view, 0, sizeof(owner_view));
	CHECK(SG_RuneCompactBuilderOwnerRead(builder, &owner_view));
	CHECK(memcmp(&owner_view.identity, &view.identity,
		sizeof(owner_view.identity)) == 0);
	CHECK(owner_view.host_law != NULL);
	CHECK(memcmp(owner_view.host_law, &host_view.laws,
		sizeof(*owner_view.host_law)) == 0);
	CHECK(owner_view.weapon_law != NULL);
#if defined(SG_COMPACT_BUILDER_REAL_SOURCE_AUTHORITY)
	CHECK(owner_view.weapon_law->deathmatch_active == 1U);
	CHECK(owner_view.weapon_law->fast_switch_enabled == 0U);
#else
	CHECK(memcmp(owner_view.weapon_law, &source_snapshot.weapon_law,
		sizeof(*owner_view.weapon_law)) == 0);
#endif
	memset(&pmove_request, 0, sizeof(pmove_request));
	memset(&pmove_result, 0, sizeof(pmove_result));
	pmove_law_result = SG_RuneCompactBuilderOwnerPmove(builder, NULL,
		&pmove_request, &pmove_result, &pmove_error);
	CHECK(pmove_law_result.status == SG_HOST_LAW_OK);
	CHECK(pmove_evaluator_acquire_calls == 1);
	CHECK(pmove_evaluator_run_calls == 1);
#if defined(SG_COMPACT_BUILDER_REAL_SOURCE_AUTHORITY)
	{
		uint32_t bsp_entity_crc = 0U;

		CHECK(SG_CRC32Buffer("{}\n", 3U, &bsp_entity_crc));
		CHECK(view.identity.entity_crc32 != bsp_entity_crc);
	}
#endif
	SG_RuneCompactBuilderDestroy(builder);
	CHECK(pmove_evaluator_destroy_calls == 1);
#if defined(SG_COMPACT_BUILDER_REAL_SOURCE_AUTHORITY)
	SG_RuneSourceAuthorityReset();
#endif
}

static void TestOwnerReadFailsAfterAuthorityRevocation(void)
{
	struct sg_host_law_construction_s construction = { 1U };
	sg_rune_compact_builder_input_t input = Input(&construction);
	sg_rune_compact_builder_t *builder = NULL;
	sg_rune_compact_builder_error_t error;
	sg_rune_compact_builder_owner_view_t view;
	sg_rune_compact_builder_owner_view_t unchanged;

	SetHost();
	SetSourceAuthority();
	pmove_evaluator_current_status = SG_HOST_LAW_OK;
	CHECK(SG_RuneCompactBuilderBuild(&input, &builder, &error));
	CHECK(builder != NULL);
	memset(&view, 0xa5, sizeof(view));
	unchanged = view;
	pmove_evaluator_current_status = SG_HOST_LAW_PRODUCTION_DRIFT;
	CHECK(!SG_RuneCompactBuilderOwnerRead(builder, &view));
	CHECK(memcmp(&view, &unchanged, sizeof(view)) == 0);
	pmove_evaluator_current_status = SG_HOST_LAW_OK;
	memset(&view, 0xa5, sizeof(view));
	unchanged = view;
#if defined(SG_COMPACT_BUILDER_REAL_SOURCE_AUTHORITY)
	source_host_current = 0;
#else
	source_final_status = SG_RUNE_SOURCE_GENERATION_DRIFT;
#endif
	CHECK(!SG_RuneCompactBuilderOwnerRead(builder, &view));
	CHECK(memcmp(&view, &unchanged, sizeof(view)) == 0);
	SG_RuneCompactBuilderDestroy(builder);
	pmove_evaluator_current_status = SG_HOST_LAW_OK;
#if defined(SG_COMPACT_BUILDER_REAL_SOURCE_AUTHORITY)
	source_host_current = 1;
	SG_RuneSourceAuthorityReset();
#else
	source_final_status = SG_RUNE_SOURCE_OK;
#endif
}

#if !defined(SG_COMPACT_BUILDER_REAL_ENTITY_SEMANTICS)
static void TestOwnerModelLocalQ8Transform(void)
{
	struct sg_host_law_construction_s construction = { 1U };
	sg_rune_compact_builder_input_t input = Input(&construction);
	sg_rune_compact_builder_t *builder = NULL;
	sg_rune_compact_builder_error_t error;
	sg_rune_q8_vec3_t local[2];
	sg_rune_vec3_t world[2];
	sg_rune_bounds_t bounds;
	sg_rune_vec3_t unchanged[2];
	sg_rune_bounds_t unchanged_bounds;
	sg_host_law_result_t result;

	SetHost();
	SetSourceAuthority();
	pmove_evaluator_current_status = SG_HOST_LAW_OK;
	CHECK(SG_RuneCompactBuilderBuild(&input, &builder, &error));
	CHECK(builder != NULL);
	memset(local, 0, sizeof(local));
	local[0].value[0] = 32;
	local[0].value[1] = -16;
	local[1].value[1] = 64;
	memset(world, 0xa5, sizeof(world));
	memset(&bounds, 0xa5, sizeof(bounds));
	result = SG_RuneCompactBuilderOwnerTransformModelLocalQ8(builder, 0U,
		local, 2U, world, &bounds);
	CHECK(result.status == SG_HOST_LAW_OK);
	CHECK(world[0].value[0] == 52.0f && world[0].value[1] == 54.0f &&
		world[0].value[2] == 5.0f);
	CHECK(world[1].value[0] == 42.0f && world[1].value[1] == 50.0f &&
		world[1].value[2] == 5.0f);
	CHECK(bounds.mins.value[0] == 42.0f && bounds.mins.value[1] == 50.0f &&
		bounds.mins.value[2] == 5.0f);
	CHECK(bounds.maxs.value[0] == 52.0f && bounds.maxs.value[1] == 54.0f &&
		bounds.maxs.value[2] == 5.0f);

	memset(unchanged, 0x5a, sizeof(unchanged));
	memset(&unchanged_bounds, 0x5a, sizeof(unchanged_bounds));
	memcpy(world, unchanged, sizeof(world));
	bounds = unchanged_bounds;
	pmove_evaluator_current_status = SG_HOST_LAW_PRODUCTION_DRIFT;
	result = SG_RuneCompactBuilderOwnerTransformModelLocalQ8(builder, 0U,
		local, 2U, world, &bounds);
	CHECK(result.status == SG_HOST_LAW_PRODUCTION_DRIFT);
	CHECK(memcmp(world, unchanged, sizeof(world)) == 0);
	CHECK(memcmp(&bounds, &unchanged_bounds, sizeof(bounds)) == 0);
	pmove_evaluator_current_status = SG_HOST_LAW_OK;
	result = SG_RuneCompactBuilderOwnerTransformModelLocalQ8(builder, 1U,
		local, 2U, world, &bounds);
	CHECK(result.status == SG_HOST_LAW_EVALUATION_FAILED);

	SG_RuneCompactBuilderDestroy(builder);
	builder = NULL;
	fake_invalid_mover_model = 1;
	CHECK(SG_RuneCompactBuilderBuild(&input, &builder, &error));
	CHECK(builder != NULL);
	result = SG_RuneCompactBuilderOwnerTransformModelLocalQ8(builder, 0U,
		local, 2U, world, &bounds);
	CHECK(result.status == SG_HOST_LAW_EVALUATION_FAILED);
	SG_RuneCompactBuilderDestroy(builder);
	fake_invalid_mover_model = 0;
}

static void TestOwnerTransformUsesSpawnResolvedAngularPose(void)
{
	struct sg_host_law_construction_s construction = { 1U };
	sg_rune_compact_builder_input_t input = Input(&construction);
	sg_rune_compact_builder_t *builder = NULL;
	sg_rune_compact_builder_error_t error;
	sg_rune_q8_vec3_t local;
	sg_rune_vec3_t world;
	sg_rune_bounds_t bounds;
	sg_host_law_result_t result;

	SetHost();
	SG_HostMechanismLawDefault(&host_view.laws.mechanism);
	SetSourceAuthority();
	/* Raw entity angles remain +90 in the fixture.  The finite START_OPEN
	 * schedule publishes -90 as the authenticated initial pose. */
	fake_angular_mover_kind = SG_BSP_ENTITY_ANGULAR_MOVER_FINITE_DOOR;
	CHECK(SG_RuneCompactBuilderBuild(&input, &builder, &error));
	CHECK(builder != NULL);
	memset(&local, 0, sizeof(local));
	local.value[0] = 8;
	result = SG_RuneCompactBuilderOwnerTransformModelLocalQ8(builder, 0U,
		&local, 1U, &world, &bounds);
	CHECK(result.status == SG_HOST_LAW_OK);
	CHECK(world.value[0] == 50.0f && world.value[1] == 49.0f &&
		world.value[2] == 5.0f);
	SG_RuneCompactBuilderDestroy(builder);
	builder = NULL;

	/* A continuous func_rotating schedule has no finite endpoint, so the
	 * opaque local-Q8 endpoint query must fail closed. */
	SetHost();
	SetSourceAuthority();
	SG_HostMechanismLawDefault(&host_view.laws.mechanism);
	fake_angular_mover_kind =
		SG_BSP_ENTITY_ANGULAR_MOVER_CONTINUOUS_ROTATOR;
	CHECK(SG_RuneCompactBuilderBuild(&input, &builder, &error));
	CHECK(builder != NULL);
	result = SG_RuneCompactBuilderOwnerTransformModelLocalQ8(builder, 0U,
		&local, 1U, &world, &bounds);
	CHECK(result.status == SG_HOST_LAW_EVALUATION_FAILED);
	SG_RuneCompactBuilderDestroy(builder);
	builder = NULL;

	/* A secret door's stock path has two legs.  It must not leak its raw map
	 * origin through the generic transform path before an exact schedule exists. */
	SetHost();
	SetSourceAuthority();
	SG_HostMechanismLawDefault(&host_view.laws.mechanism);
	fake_angular_mover_kind = SG_BSP_ENTITY_ANGULAR_MOVER_NONE;
	fake_mover_role = SG_MECH_NODE_SECRET_DOOR;
	CHECK(SG_RuneCompactBuilderBuild(&input, &builder, &error));
	CHECK(builder != NULL);
	result = SG_RuneCompactBuilderOwnerTransformModelLocalQ8(builder, 0U,
		&local, 1U, &world, &bounds);
	CHECK(result.status == SG_HOST_LAW_EVALUATION_FAILED);
	SG_RuneCompactBuilderDestroy(builder);
	fake_mover_role = SG_MECH_NODE_DOOR_MASTER;
	fake_angular_mover_kind = SG_BSP_ENTITY_ANGULAR_MOVER_NONE;
}

static void TestAngularMoverIdentityRejectsDirtyInactiveBytes(void)
{
	struct sg_host_law_construction_s construction = { 1U };
	sg_rune_compact_builder_input_t input = Input(&construction);
	sg_rune_compact_builder_t *builder = NULL;
	sg_rune_compact_builder_error_t error;
	sg_rune_compact_builder_owner_view_t view;
	sg_rune_compact_builder_owner_view_t unchanged;
	sg_bsp_entity_angular_mover_t saved;
	unsigned char *schedule;
	size_t schedule_bytes;

	/* NONE owns no union arm.  A dirty byte must change the builder's
	 * currentness identity even though publication rejects the record first. */
	SetHost();
	SetSourceAuthority();
	SG_HostMechanismLawDefault(&host_view.laws.mechanism);
	fake_angular_mover_kind = SG_BSP_ENTITY_ANGULAR_MOVER_NONE;
	CHECK(SG_RuneCompactBuilderBuild(&input, &builder, &error));
	CHECK(builder != NULL && fake_last_entity_semantics != NULL);
	if (builder != NULL && fake_last_entity_semantics != NULL)
	{
		saved = fake_last_entity_semantics->entities[0].angular_mover;
		schedule = (unsigned char *)&fake_last_entity_semantics->entities[0]
			.angular_mover.schedule;
		schedule_bytes = sizeof(saved.schedule);
		schedule[schedule_bytes - 1U] = 1U;
		memset(&view, 0xa5, sizeof(view));
		unchanged = view;
		CHECK(!SG_RuneCompactBuilderOwnerRead(builder, &view));
		CHECK(memcmp(&view, &unchanged, sizeof(view)) == 0);
		fake_last_entity_semantics->entities[0].angular_mover = saved;
		CHECK(SG_RuneCompactBuilderOwnerRead(builder, &view));
	}
	SG_RuneCompactBuilderDestroy(builder);
	builder = NULL;

	/* Inactive union bytes are part of the identity witness and cannot collide. */
	SetHost();
	SetSourceAuthority();
	SG_HostMechanismLawDefault(&host_view.laws.mechanism);
	fake_angular_mover_kind =
		SG_BSP_ENTITY_ANGULAR_MOVER_CONTINUOUS_ROTATOR;
	CHECK(SG_RuneCompactBuilderBuild(&input, &builder, &error));
	CHECK(builder != NULL && fake_last_entity_semantics != NULL);
	if (builder != NULL && fake_last_entity_semantics != NULL)
	{
		saved = fake_last_entity_semantics->entities[0].angular_mover;
		schedule = (unsigned char *)&fake_last_entity_semantics->entities[0]
			.angular_mover.schedule;
		schedule_bytes = sizeof(saved.schedule);
		CHECK(schedule_bytes > sizeof(saved.schedule.continuous_rotator));
		if (schedule_bytes > sizeof(saved.schedule.continuous_rotator))
		{
			schedule[schedule_bytes - 1U] = 1U;
			memset(&view, 0xa5, sizeof(view));
			unchanged = view;
			CHECK(!SG_RuneCompactBuilderOwnerRead(builder, &view));
			CHECK(memcmp(&view, &unchanged, sizeof(view)) == 0);
			fake_last_entity_semantics->entities[0].angular_mover = saved;
			CHECK(SG_RuneCompactBuilderOwnerRead(builder, &view));
		}
	}
	SG_RuneCompactBuilderDestroy(builder);
	fake_angular_mover_kind = SG_BSP_ENTITY_ANGULAR_MOVER_NONE;
}

static void TestOwnerMoverTransportCandidateStates(void)
{
	struct sg_host_law_construction_s construction = { 1U };
	sg_rune_compact_builder_input_t input = Input(&construction);
	sg_rune_compact_builder_t *builder = NULL;
	sg_rune_compact_builder_error_t error;
	sg_rune_compact_source_surface_t surface;
	sg_rune_q8_vec3_t vertices[4];
	sg_rune_compact_builder_mover_request_t request;
	sg_rune_compact_builder_mover_result_t result;
	sg_rune_compact_builder_mover_result_t unchanged;
	const sg_rune_compact_geometry_t *geometry =
		(const sg_rune_compact_geometry_t *)(const void *)&surface;
	sg_host_law_result_t host_result;

	SetHost();
	SetSourceAuthority();
	SG_HostMechanismLawDefault(&host_view.laws.mechanism);
	fake_mover_kind = SG_RUNE_MECHANISM_LIFT;
	pmove_evaluator_current_status = SG_HOST_LAW_OK;
	CHECK(SG_RuneCompactBuilderBuild(&input, &builder, &error));
	CHECK(builder != NULL);
	memset(&surface, 0, sizeof(surface));
	memset(vertices, 0, sizeof(vertices));
	vertices[0].value[0] = -32;
	vertices[0].value[1] = -32;
	vertices[1].value[0] = 32;
	vertices[1].value[1] = -32;
	vertices[2].value[0] = 32;
	vertices[2].value[1] = 32;
	vertices[3].value[0] = -32;
	vertices[3].value[1] = 32;
	surface.frame = SG_RUNE_COMPACT_SOURCE_SURFACE_MODEL_LOCAL;
	surface.source.model = 0U;
	surface.vertices.count = 4U;
	memset(&fake_geometry_view, 0, sizeof(fake_geometry_view));
	fake_geometry_view.source_surfaces = &surface;
	fake_geometry_view.source_surface_count = 1U;
	fake_geometry_view.source_surface_vertices = vertices;
	fake_geometry_view.source_surface_vertex_count = 4U;
	fake_geometry_read_enabled = 1;
	fake_identity_matches = 1;
	memset(&request, 0, sizeof(request));
	request.mode = SG_RUNE_COMPACT_BUILDER_MOVER_MODE_CARRIED_SUPPORT;
	request.source_state = SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE;
	request.destination_state = SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	request.source_surface_ordinal = 0U;
	request.portal_ordinal = SG_RUNE_COMPACT_INDEX_NONE;
	request.entry_cell.value = SG_RUNE_COMPACT_INDEX_NONE;
	request.exit_cell.value = SG_RUNE_COMPACT_INDEX_NONE;
	request.source_endpoint_entity_ordinal = SG_RUNE_COMPACT_INDEX_NONE;
	request.destination_endpoint_entity_ordinal = SG_RUNE_COMPACT_INDEX_NONE;
	request.route_fanout_ordinal = SG_RUNE_COMPACT_INDEX_NONE;
	request.support_pose_mode =
		SG_RUNE_COMPACT_BUILDER_SUPPORT_POSE_CANONICAL;
	request.stance = SG_RUNE_STANCE_STANDING;
	memset(&result, 0xa5, sizeof(result));
	host_result = SG_RuneCompactBuilderOwnerMoverTransport(builder, geometry,
		&request, &result);
	CHECK(host_result.status == SG_HOST_LAW_OK && !result.applicable &&
		result.failure == SG_RUNE_COMPACT_BUILDER_MOVER_FAILURE_NONE);
	surface.source.model = 1U;
	host_result = SG_RuneCompactBuilderOwnerMoverTransport(builder, geometry,
		&request, &result);
	CHECK(host_result.status == SG_HOST_LAW_OK && result.applicable &&
		result.failure == SG_RUNE_COMPACT_BUILDER_MOVER_FAILURE_NO_LANDING &&
		!result.start_supported && !result.end_supported &&
		!result.swept_static_clear);
	memset(&unchanged, 0x5a, sizeof(unchanged));
	result = unchanged;
	pmove_evaluator_current_status = SG_HOST_LAW_PRODUCTION_DRIFT;
	host_result = SG_RuneCompactBuilderOwnerMoverTransport(builder, geometry,
		&request, &result);
	CHECK(host_result.status == SG_HOST_LAW_PRODUCTION_DRIFT &&
		memcmp(&result, &unchanged, sizeof(result)) == 0);
	pmove_evaluator_current_status = SG_HOST_LAW_OK;
	SG_RuneCompactBuilderDestroy(builder);
	fake_geometry_read_enabled = 0;
	fake_identity_matches = 0;
	fake_mover_kind = SG_RUNE_MECHANISM_DOOR;
}

static int CollisionWorldTransformFiniteCanonical(
	const sg_host_collision_world_transform_t *transform)
{
	uint32_t local_axis;
	uint32_t world_axis;

	if (!transform)
		return 0;
	for (world_axis = 0U; world_axis < 3U; world_axis++) {
		if (!isfinite(transform->origin[world_axis]) ||
			(transform->origin[world_axis] == 0.0f &&
				signbit(transform->origin[world_axis])))
			return 0;
		for (local_axis = 0U; local_axis < 3U; local_axis++)
			if (!isfinite(transform->axis[local_axis][world_axis]) ||
				(transform->axis[local_axis][world_axis] == 0.0f &&
					signbit(transform->axis[local_axis][world_axis])))
				return 0;
	}
	return 1;
}

static void TestOwnerMoverTransportTransformProvenance(void)
{
	struct sg_host_law_construction_s construction = { 1U };
	sg_rune_compact_builder_input_t input = Input(&construction);
	sg_rune_compact_builder_t *builder = NULL;
	sg_rune_compact_builder_error_t error;
	sg_rune_compact_source_surface_t surface;
	sg_rune_q8_vec3_t vertices[4];
	sg_rune_compact_cell_t cell;
	sg_rune_compact_facet_t facet;
	sg_rune_compact_incidence_t incidence;
	sg_rune_compact_incidence_index_t cell_incidence;
	sg_rune_compact_builder_mover_request_t request;
	sg_rune_compact_builder_mover_result_t result;
	sg_rune_vec3_t replayed_world;
	sg_rune_vec3_t unchanged_world;
	sg_host_collision_world_transform_t noncanonical_transform;
	const sg_rune_compact_geometry_t *geometry =
		(const sg_rune_compact_geometry_t *)(const void *)&surface;
	sg_host_law_result_t host_result;

	SetHost();
	SG_HostMechanismLawDefault(&host_view.laws.mechanism);
	SetSourceAuthority();
	/* At 2^24, one binary32 ULP is two world units.  The selected local
	 * support pose contributes exactly that step through the host AngleAxis
	 * operation order, while the lift's terminal carry transform changes Z. */
	fake_mover_kind = SG_RUNE_MECHANISM_LIFT;
	fake_mover_origin_x = 16777216.0f;
	fake_mover_height = 100.0f;
	fake_carried_support_enabled = 1;
	CHECK(SG_RuneCompactBuilderBuild(&input, &builder, &error));
	CHECK(builder != NULL);
	memset(&surface, 0, sizeof(surface));
	memset(vertices, 0, sizeof(vertices));
	memset(&cell, 0, sizeof(cell));
	memset(&facet, 0, sizeof(facet));
	memset(&incidence, 0, sizeof(incidence));
	memset(&cell_incidence, 0, sizeof(cell_incidence));
	/* The horizontal centroid is local (0, -2, 0).  With the authenticated
	 * yaw of 90 degrees it becomes world X = origin + 2, exactly one ULP. */
	vertices[0] = (sg_rune_q8_vec3_t){ { -32, -24, 0 } };
	vertices[1] = (sg_rune_q8_vec3_t){ { 32, -24, 0 } };
	vertices[2] = (sg_rune_q8_vec3_t){ { 32, -8, 0 } };
	vertices[3] = (sg_rune_q8_vec3_t){ { -32, -8, 0 } };
	surface.frame = SG_RUNE_COMPACT_SOURCE_SURFACE_MODEL_LOCAL;
	surface.source.model = 1U;
	surface.vertices.count = 4U;
	/* A single compact cell owns both exact-Q8 endpoint poses.  Its one
	 * constraint is x >= 0, so the localizer exercises its halfspace rule,
	 * not an AABB-only shortcut. */
	cell.bounds.mins = (sg_rune_q8_vec3_t){ { 134217600, 0, -1024 } };
	cell.bounds.maxs = (sg_rune_q8_vec3_t){ { 134217856, 800, 512 } };
	cell.incidences.first = 0U;
	cell.incidences.count = 1U;
	cell.valid_stances = SG_RUNE_STANCE_VALID_STANDING;
	facet.plane.normal_bits[0] = UINT32_C(0x3f800000);
	facet.kind = SG_RUNE_COMPACT_FACET_CONSTRAINT_ONLY;
	incidence.cell.value = 0U;
	incidence.facet.value = 0U;
	incidence.side = SG_RUNE_FACET_POSITIVE_SIDE;
	incidence.boundary = SG_RUNE_BOUNDARY_CLOSED;
	cell_incidence.value = 0U;
	memset(&fake_geometry_view, 0, sizeof(fake_geometry_view));
	fake_geometry_view.cells = &cell;
	fake_geometry_view.cell_count = 1U;
	fake_geometry_view.facets = &facet;
	fake_geometry_view.facet_count = 1U;
	fake_geometry_view.incidences = &incidence;
	fake_geometry_view.incidence_count = 1U;
	fake_geometry_view.cell_incidences = &cell_incidence;
	fake_geometry_view.cell_incidence_count = 1U;
	fake_geometry_view.source_surfaces = &surface;
	fake_geometry_view.source_surface_count = 1U;
	fake_geometry_view.source_surface_vertices = vertices;
	fake_geometry_view.source_surface_vertex_count = 4U;
	fake_geometry_read_enabled = 1;
	fake_identity_matches = 1;
	memset(&request, 0, sizeof(request));
	request.mode = SG_RUNE_COMPACT_BUILDER_MOVER_MODE_CARRIED_SUPPORT;
	request.source_state = SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE;
	request.destination_state = SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	request.source_surface_ordinal = 0U;
	request.portal_ordinal = SG_RUNE_COMPACT_INDEX_NONE;
	request.entry_cell.value = SG_RUNE_COMPACT_INDEX_NONE;
	request.exit_cell.value = SG_RUNE_COMPACT_INDEX_NONE;
	request.source_endpoint_entity_ordinal = SG_RUNE_COMPACT_INDEX_NONE;
	request.destination_endpoint_entity_ordinal = SG_RUNE_COMPACT_INDEX_NONE;
	request.route_fanout_ordinal = SG_RUNE_COMPACT_INDEX_NONE;
	request.support_pose_mode =
		SG_RUNE_COMPACT_BUILDER_SUPPORT_POSE_CANONICAL;
	request.stance = SG_RUNE_STANCE_STANDING;
	memset(&result, 0xa5, sizeof(result));
	host_result = SG_RuneCompactBuilderOwnerMoverTransport(builder, geometry,
		&request, &result);
	CHECK(host_result.status == SG_HOST_LAW_OK && result.applicable &&
		result.failure == SG_RUNE_COMPACT_BUILDER_MOVER_FAILURE_NONE &&
		result.start_supported && result.end_supported &&
		result.swept_static_clear);
	CHECK(CollisionWorldTransformFiniteCanonical(&result.source_mover_transform) &&
		CollisionWorldTransformFiniteCanonical(&result.destination_mover_transform));
	CHECK(result.source_mover_transform.origin[0] == 16777216.0f &&
		result.destination_mover_transform.origin[0] == 16777216.0f &&
		result.source_mover_transform.origin[2] == -95.0f &&
		result.destination_mover_transform.origin[2] == 5.0f);
	CHECK(result.source_mover_transform.axis[0][1] > 0.0f &&
		result.source_mover_transform.axis[1][0] < 0.0f &&
		result.destination_mover_transform.axis[0][1] > 0.0f &&
		result.destination_mover_transform.axis[1][0] < 0.0f);
	CHECK(result.source_support_local.value[2] == 0);
	CHECK(result.source_player_local.value[2] == 193);
	CHECK(result.source_player_world.value[0] ==
		nextafterf(fake_mover_origin_x, INFINITY) &&
		result.destination_player_world.value[0] ==
		nextafterf(fake_mover_origin_x, INFINITY));
	host_result = SG_RuneCompactBuilderOwnerReplayLocalQ8Pose(builder, 0U,
		&result.source_mover_transform, &result.source_player_local,
		&replayed_world);
	CHECK(host_result.status == SG_HOST_LAW_OK &&
		!memcmp(&replayed_world, &result.source_player_world,
			sizeof(replayed_world)));
	host_result = SG_RuneCompactBuilderOwnerReplayLocalQ8Pose(builder, 0U,
		&result.destination_mover_transform, &result.destination_player_local,
		&replayed_world);
	CHECK(host_result.status == SG_HOST_LAW_OK &&
		!memcmp(&replayed_world, &result.destination_player_world,
			sizeof(replayed_world)));
	/* Result transforms carry a single finite representation.  The owner
	 * refuses a forged negative-zero bit rather than silently normalizing the
	 * replay input, leaving the caller's world witness untouched. */
	noncanonical_transform = result.source_mover_transform;
	noncanonical_transform.axis[0][2] = -0.0f;
	memset(&replayed_world, 0xa5, sizeof(replayed_world));
	unchanged_world = replayed_world;
	host_result = SG_RuneCompactBuilderOwnerReplayLocalQ8Pose(builder, 0U,
		&noncanonical_transform, &result.source_player_local,
		&replayed_world);
	CHECK(host_result.status == SG_HOST_LAW_EVALUATION_FAILED &&
		!memcmp(&replayed_world, &unchanged_world, sizeof(replayed_world)));
	request.support_pose_mode = SG_RUNE_COMPACT_BUILDER_SUPPORT_POSE_EXPLICIT;
	request.support_local_pose = result.source_support_local;
	request.player_local_pose = result.source_player_local;
	host_result = SG_RuneCompactBuilderOwnerMoverTransport(builder, geometry,
		&request, &result);
	CHECK(host_result.status == SG_HOST_LAW_OK && result.applicable &&
		result.failure == SG_RUNE_COMPACT_BUILDER_MOVER_FAILURE_NONE);
	request.player_local_pose.value[2]--;
	host_result = SG_RuneCompactBuilderOwnerMoverTransport(builder, geometry,
		&request, &result);
	CHECK(host_result.status == SG_HOST_LAW_OK && result.applicable &&
		result.failure == SG_RUNE_COMPACT_BUILDER_MOVER_FAILURE_NO_LANDING);
	SG_RuneCompactBuilderDestroy(builder);
	fake_carried_support_enabled = 0;
	fake_geometry_read_enabled = 0;
	fake_identity_matches = 0;
	fake_mover_kind = SG_RUNE_MECHANISM_DOOR;
}

#if !defined(SG_COMPACT_BUILDER_REAL_SOURCE_AUTHORITY)
/* Exercise the concrete builder-owned train graph rather than the transition
 * layer's host stub.  The graph is train -> 1 -> 2 -> 3 -> 1, and has two
 * distinct 3 -> 1 TARGET fanouts. */
static void TestOwnerTrainRouteCertification(void)
{
	struct sg_host_law_construction_s construction = { 1U };
	sg_rune_compact_builder_input_t input = Input(&construction);
	sg_rune_compact_builder_t *builder = NULL;
	sg_rune_compact_builder_error_t error;
	sg_rune_compact_source_surface_t surface;
	sg_rune_q8_vec3_t vertices[4];
	sg_rune_compact_builder_mover_request_t request;
	sg_rune_compact_builder_mover_result_t result;
	sg_rune_compact_builder_mover_result_t unchanged;
	const sg_rune_compact_geometry_t *geometry =
		(const sg_rune_compact_geometry_t *)(const void *)&surface;
	sg_host_law_result_t host_result;
	size_t index;

	SetHost();
	SetSourceAuthority();
	/* The fake semantics adapter gives every survivor after worldspawn one
	 * canonical entity ordinal.  Supply train plus three path-corners. */
	fake_source_entity_record_count = 5U;
	for (index = 0U; index < fake_source_entity_record_count; index++) {
		fake_source_entity_records[index].source_ordinal = (uint32_t)index;
		fake_source_entity_records[index].effective_spawnflags = 0U;
	}
	source_snapshot.entity_record_count =
		(uint32_t)fake_source_entity_record_count;
	SG_HostMechanismLawDefault(&host_view.laws.mechanism);
	fake_train_graph = 1;
	CHECK(SG_RuneCompactBuilderBuild(&input, &builder, &error));
	CHECK(builder != NULL);
	memset(&surface, 0, sizeof(surface));
	memset(vertices, 0, sizeof(vertices));
	vertices[0].value[0] = -32;
	vertices[0].value[1] = -32;
	vertices[1].value[0] = 32;
	vertices[1].value[1] = -32;
	vertices[2].value[0] = 32;
	vertices[2].value[1] = 32;
	vertices[3].value[0] = -32;
	vertices[3].value[1] = 32;
	surface.frame = SG_RUNE_COMPACT_SOURCE_SURFACE_MODEL_LOCAL;
	surface.source.model = 1U;
	surface.vertices.count = 4U;
	memset(&fake_geometry_view, 0, sizeof(fake_geometry_view));
	fake_geometry_view.source_surfaces = &surface;
	fake_geometry_view.source_surface_count = 1U;
	fake_geometry_view.source_surface_vertices = vertices;
	fake_geometry_view.source_surface_vertex_count = 4U;
	fake_geometry_read_enabled = 1;
	fake_identity_matches = 1;
	memset(&request, 0, sizeof(request));
	request.mode = SG_RUNE_COMPACT_BUILDER_MOVER_MODE_CARRIED_SUPPORT;
	request.source_state = SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	request.destination_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	request.source_surface_ordinal = 0U;
	request.portal_ordinal = SG_RUNE_COMPACT_INDEX_NONE;
	request.entry_cell.value = SG_RUNE_COMPACT_INDEX_NONE;
	request.exit_cell.value = SG_RUNE_COMPACT_INDEX_NONE;
	request.source_endpoint_entity_ordinal = 3U;
	request.destination_endpoint_entity_ordinal = 1U;
	request.route_fanout_ordinal = 7U;
	request.support_pose_mode =
		SG_RUNE_COMPACT_BUILDER_SUPPORT_POSE_CANONICAL;
	request.stance = SG_RUNE_STANCE_STANDING;
	/* The source corner is reached only through the three-corner walk.  The
	 * closing edge's specific fanout is echoed even though static collision
	 * intentionally rejects the fake rider's landing. */
	host_result = SG_RuneCompactBuilderOwnerMoverTransport(builder, geometry,
		&request, &result);
	CHECK(host_result.status == SG_HOST_LAW_OK && result.applicable &&
		result.failure == SG_RUNE_COMPACT_BUILDER_MOVER_FAILURE_NO_LANDING &&
		result.source_endpoint_entity_ordinal == 3U &&
		result.destination_endpoint_entity_ordinal == 1U &&
		result.route_fanout_ordinal == 7U);
	/* The duplicate endpoint has a separate valid occurrence. */
	request.route_fanout_ordinal = 8U;
	host_result = SG_RuneCompactBuilderOwnerMoverTransport(builder, geometry,
		&request, &result);
	CHECK(host_result.status == SG_HOST_LAW_OK && result.applicable &&
		result.route_fanout_ordinal == 8U);
	/* A guessed fanout cannot reuse either endpoint certificate. */
	request.route_fanout_ordinal = 6U;
	host_result = SG_RuneCompactBuilderOwnerMoverTransport(builder, geometry,
		&request, &result);
	CHECK(host_result.status == SG_HOST_LAW_OK && !result.applicable &&
		result.failure == SG_RUNE_COMPACT_BUILDER_MOVER_FAILURE_NONE);
#if defined(SG_COMPACT_BUILDER_TEST_HOOKS)
	/* Traversal allocation failure is a typed host error and must not become
	 * the ordinary candidate miss used for an unreachable route. */
	request.route_fanout_ordinal = 7U;
	memset(&unchanged, 0x5a, sizeof(unchanged));
	result = unchanged;
	SG_RuneCompactBuilderTestFailNextTrainRouteAllocation();
	host_result = SG_RuneCompactBuilderOwnerMoverTransport(builder, geometry,
		&request, &result);
	CHECK(host_result.status == SG_HOST_LAW_ALLOCATION_FAILED &&
		memcmp(&result, &unchanged, sizeof(result)) == 0);
#endif
	SG_RuneCompactBuilderDestroy(builder);
	builder = NULL;

	SetHost();
	SetSourceAuthority();
	fake_source_entity_record_count = 5U;
	for (index = 0U; index < fake_source_entity_record_count; index++) {
		fake_source_entity_records[index].source_ordinal = (uint32_t)index;
		fake_source_entity_records[index].effective_spawnflags = 0U;
	}
	source_snapshot.entity_record_count =
		(uint32_t)fake_source_entity_record_count;
	SG_HostMechanismLawDefault(&host_view.laws.mechanism);
	fake_train_graph = 1;
	fake_train_graph_malformed = 1;
	CHECK(SG_RuneCompactBuilderBuild(&input, &builder, &error));
	CHECK(builder != NULL);
	/* Reinstall geometry after SetHost's test-state reset. */
	fake_geometry_view.source_surfaces = &surface;
	fake_geometry_view.source_surface_count = 1U;
	fake_geometry_view.source_surface_vertices = vertices;
	fake_geometry_view.source_surface_vertex_count = 4U;
	fake_geometry_read_enabled = 1;
	fake_identity_matches = 1;
	request.route_fanout_ordinal = 7U;
	memset(&result, 0, sizeof(result));
	host_result = SG_RuneCompactBuilderOwnerMoverTransport(builder, geometry,
		&request, &result);
	CHECK(host_result.status == SG_HOST_LAW_EVALUATION_FAILED &&
		!result.applicable);
	SG_RuneCompactBuilderDestroy(builder);
	fake_geometry_read_enabled = 0;
	fake_identity_matches = 0;
	fake_train_graph = 0;
	fake_train_graph_malformed = 0;
}
#endif

static void TestOwnerPortalPolygonCertification(void)
{
	struct sg_host_law_construction_s construction = { 1U };
	sg_rune_compact_builder_input_t input = Input(&construction);
	sg_rune_compact_builder_t *builder = NULL;
	sg_rune_compact_builder_error_t error;
	sg_rune_compact_source_surface_t surface;
	sg_rune_compact_source_surface_t mover_surfaces[4];
	sg_rune_q8_vec3_t source_vertices[3];
	sg_rune_q8_vec3_t mover_vertices[12];
	sg_rune_q8_vec3_t portal_vertices[3];
	sg_rune_compact_facet_t facet;
	sg_rune_compact_portal_t portal;
	sg_rune_compact_incidence_t incidences[2];
	sg_rune_compact_cell_t cells[2];
	sg_rune_compact_builder_mover_request_t request;
	sg_rune_compact_builder_mover_result_t result;
	sg_rune_vec3_t source_world[3];
	sg_rune_vec3_t destination_world[3];
	const sg_rune_compact_geometry_t *geometry =
		(const sg_rune_compact_geometry_t *)(const void *)&surface;
	sg_host_law_result_t host_result;

	SetHost();
	SetSourceAuthority();
	SG_HostMechanismLawDefault(&host_view.laws.mechanism);
	fake_mover_kind = SG_RUNE_MECHANISM_DOOR;
	fake_mover_spawnflags = 1U;
	fake_mover_move_direction_x = 1.0f;
	pmove_evaluator_current_status = SG_HOST_LAW_OK;
	CHECK(SG_RuneCompactBuilderBuild(&input, &builder, &error));
	CHECK(builder != NULL);
	memset(&surface, 0, sizeof(surface));
	memset(source_vertices, 0, sizeof(source_vertices));
	memset(portal_vertices, 0, sizeof(portal_vertices));
	memset(&facet, 0, sizeof(facet));
	memset(&portal, 0, sizeof(portal));
	memset(incidences, 0, sizeof(incidences));
	memset(cells, 0, sizeof(cells));
	/* This catalog root is deliberately one unit above the portal plane.  It
	 * describes a thick door face, so face coplanarity must not decide closure;
	 * the authenticated model-volume trace below does. */
	source_vertices[0].value[2] = 8;
	source_vertices[1].value[2] = 8;
	source_vertices[2].value[2] = 8;
	source_vertices[1].value[1] = -16;
	source_vertices[2].value[0] = 16;
	surface.frame = SG_RUNE_COMPACT_SOURCE_SURFACE_MODEL_LOCAL;
	surface.source.model = 1U;
	surface.vertices.count = 3U;
	facet.kind = SG_RUNE_COMPACT_FACET_POLYGON;
	facet.vertices.count = 3U;
	portal.facet.value = 0U;
	portal.negative_incidence.value = 0U;
	portal.positive_incidence.value = 1U;
	incidences[0].cell.value = 0U;
	incidences[1].cell.value = 1U;
	memset(&fake_geometry_view, 0, sizeof(fake_geometry_view));
	fake_geometry_view.cells = cells;
	fake_geometry_view.cell_count = 2U;
	fake_geometry_view.facets = &facet;
	fake_geometry_view.facet_count = 1U;
	fake_geometry_view.incidences = incidences;
	fake_geometry_view.incidence_count = 2U;
	fake_geometry_view.vertices = portal_vertices;
	fake_geometry_view.vertex_count = 3U;
	fake_geometry_view.portals = &portal;
	fake_geometry_view.portal_count = 1U;
	fake_geometry_view.source_surfaces = &surface;
	fake_geometry_view.source_surface_count = 1U;
	fake_geometry_view.source_surface_vertices = source_vertices;
	fake_geometry_view.source_surface_vertex_count = 3U;
	fake_geometry_read_enabled = 1;
	fake_identity_matches = 1;
	memset(&request, 0, sizeof(request));
	request.mode = SG_RUNE_COMPACT_BUILDER_MOVER_MODE_PORTAL_STATE;
	request.source_state = SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE;
	request.destination_state = SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	request.source_surface_ordinal = 0U;
	request.portal_ordinal = 0U;
	request.entry_cell.value = 0U;
	request.exit_cell.value = 1U;
	request.source_endpoint_entity_ordinal = SG_RUNE_COMPACT_INDEX_NONE;
	request.destination_endpoint_entity_ordinal = SG_RUNE_COMPACT_INDEX_NONE;
	request.route_fanout_ordinal = SG_RUNE_COMPACT_INDEX_NONE;
	request.source_world_vertices_out = source_world;
	request.destination_world_vertices_out = destination_world;
	request.world_vertex_capacity = 3U;
	/* The portal lies at z=5 while the selected face is at z=6.  Only the
	 * destination bmodel volume blocks its strict interior probe. */
	portal_vertices[0].value[0] = 402;
	portal_vertices[0].value[1] = 402;
	portal_vertices[0].value[2] = 40;
	portal_vertices[1].value[0] = 410;
	portal_vertices[1].value[1] = 402;
	portal_vertices[1].value[2] = 40;
	portal_vertices[2].value[0] = 402;
	portal_vertices[2].value[1] = 410;
	portal_vertices[2].value[2] = 40;
	fake_model_trace_allsolid = 1;
	fake_model_trace_origin_x = 50.0f;
	host_result = SG_RuneCompactBuilderOwnerMoverTransport(builder, geometry,
		&request, &result);
	CHECK(host_result.status == SG_HOST_LAW_OK && result.applicable &&
		result.failure == SG_RUNE_COMPACT_BUILDER_MOVER_FAILURE_NONE &&
		!result.source_portal_blocked && result.destination_portal_blocked);
	CHECK(source_world[0].value[0] == 58.0f &&
		destination_world[0].value[0] == 50.0f);
	CHECK(source_world[0].value[2] == 6.0f &&
		destination_world[0].value[2] == 6.0f);
	/* A mover may expose several model-local roots.  Whole-model collision at
	 * the portal must not let the first, unrelated root claim provenance for
	 * another root that actually covers the blocked patch. */
	mover_surfaces[0] = surface;
	mover_surfaces[1] = surface;
	mover_surfaces[2] = surface;
	mover_surfaces[3] = surface;
	mover_surfaces[0].vertices.first = 0U;
	mover_surfaces[1].vertices.first = 3U;
	mover_surfaces[2].vertices.first = 6U;
	mover_surfaces[2].source.brush_side = 1U;
	mover_surfaces[2].source.plane = 1U;
	mover_surfaces[3].vertices.first = 9U;
	mover_surfaces[3].source.brush = 1U;
	mover_surfaces[3].source.brush_side = 2U;
	mover_surfaces[3].source.plane = 2U;
	memcpy(mover_vertices, source_vertices, sizeof(source_vertices));
	memcpy(mover_vertices + 3U, source_vertices, sizeof(source_vertices));
	memcpy(mover_vertices + 6U, source_vertices, sizeof(source_vertices));
	memcpy(mover_vertices + 9U, source_vertices, sizeof(source_vertices));
	mover_vertices[0].value[1] += 160;
	mover_vertices[1].value[1] += 160;
	mover_vertices[2].value[1] += 160;
	mover_vertices[6].value[2] = -80;
	mover_vertices[7].value[2] = -80;
	mover_vertices[8].value[2] = -80;
	mover_vertices[9].value[2] = 80;
	mover_vertices[10].value[2] = 80;
	mover_vertices[11].value[2] = 80;
	fake_geometry_view.source_surfaces = mover_surfaces;
	fake_geometry_view.source_surface_count = 4U;
	fake_geometry_view.source_surface_vertices = mover_vertices;
	fake_geometry_view.source_surface_vertex_count = 12U;
	request.source_surface_ordinal = 0U;
	host_result = SG_RuneCompactBuilderOwnerMoverTransport(builder, geometry,
		&request, &result);
	CHECK(host_result.status == SG_HOST_LAW_OK && !result.applicable &&
		result.failure == SG_RUNE_COMPACT_BUILDER_MOVER_FAILURE_NONE);
	request.source_surface_ordinal = 1U;
	host_result = SG_RuneCompactBuilderOwnerMoverTransport(builder, geometry,
		&request, &result);
	CHECK(host_result.status == SG_HOST_LAW_OK && result.applicable &&
		!result.source_portal_blocked && result.destination_portal_blocked);
	/* This opposite face belongs to the blocking brush and has the same
	 * projected footprint, but it is not the nearest brush-side boundary along
	 * the occupied portal patch's normal. */
	request.source_surface_ordinal = 2U;
	host_result = SG_RuneCompactBuilderOwnerMoverTransport(builder, geometry,
		&request, &result);
	CHECK(host_result.status == SG_HOST_LAW_OK && !result.applicable &&
		result.failure == SG_RUNE_COMPACT_BUILDER_MOVER_FAILURE_NONE);
	/* A parallel face from a separate brush cannot borrow the first brush's
	 * model-wide collision result either. */
	request.source_surface_ordinal = 3U;
	host_result = SG_RuneCompactBuilderOwnerMoverTransport(builder, geometry,
		&request, &result);
	CHECK(host_result.status == SG_HOST_LAW_OK && !result.applicable &&
		result.failure == SG_RUNE_COMPACT_BUILDER_MOVER_FAILURE_NONE);
	fake_geometry_view.source_surfaces = &surface;
	fake_geometry_view.source_surface_count = 1U;
	fake_geometry_view.source_surface_vertices = source_vertices;
	fake_geometry_view.source_surface_vertex_count = 3U;
	request.source_surface_ordinal = 0U;
	/* Candidate AABBs alone cannot certify a closure when collision finds no
	 * solid volume behind the portal's strict interior. */
	fake_model_trace_allsolid = 0;
	fake_model_trace_origin_x = INFINITY;
	portal_vertices[0].value[0] = 416;
	portal_vertices[0].value[1] = 416;
	portal_vertices[0].value[2] = 40;
	portal_vertices[1].value[0] = 400;
	portal_vertices[1].value[1] = 416;
	portal_vertices[1].value[2] = 40;
	portal_vertices[2].value[0] = 416;
	portal_vertices[2].value[1] = 400;
	portal_vertices[2].value[2] = 40;
	host_result = SG_RuneCompactBuilderOwnerMoverTransport(builder, geometry,
		&request, &result);
	CHECK(host_result.status == SG_HOST_LAW_OK && !result.applicable &&
		result.failure == SG_RUNE_COMPACT_BUILDER_MOVER_FAILURE_NONE);
	/* Broad bounds can also meet at one vertex without a blocked portal patch. */
	portal_vertices[0].value[0] = 400;
	portal_vertices[0].value[1] = 416;
	portal_vertices[0].value[2] = 40;
	portal_vertices[1].value[0] = 384;
	portal_vertices[1].value[1] = 416;
	portal_vertices[1].value[2] = 40;
	portal_vertices[2].value[0] = 400;
	portal_vertices[2].value[1] = 432;
	portal_vertices[2].value[2] = 40;
	host_result = SG_RuneCompactBuilderOwnerMoverTransport(builder, geometry,
		&request, &result);
	CHECK(host_result.status == SG_HOST_LAW_OK && !result.applicable &&
		result.failure == SG_RUNE_COMPACT_BUILDER_MOVER_FAILURE_NONE);
	/* A noncoplanar world facet is still only a candidate until collision's
	 * bmodel-volume proof succeeds. */
	portal_vertices[0].value[0] = 400;
	portal_vertices[0].value[1] = 400;
	portal_vertices[0].value[2] = 40;
	portal_vertices[1].value[0] = 416;
	portal_vertices[1].value[1] = 400;
	portal_vertices[1].value[2] = 56;
	portal_vertices[2].value[0] = 400;
	portal_vertices[2].value[1] = 400;
	portal_vertices[2].value[2] = 56;
	host_result = SG_RuneCompactBuilderOwnerMoverTransport(builder, geometry,
		&request, &result);
	CHECK(host_result.status == SG_HOST_LAW_OK && !result.applicable &&
		result.failure == SG_RUNE_COMPACT_BUILDER_MOVER_FAILURE_NONE);
	SG_RuneCompactBuilderDestroy(builder);
	builder = NULL;

	/* A func_door_secret's stock sideways-then-forward path cannot be reduced
	 * to this endpoint pair.  It remains a normal non-applicable root even if
	 * its final bmodel volume would block the portal. */
	SetHost();
	SetSourceAuthority();
	SG_HostMechanismLawDefault(&host_view.laws.mechanism);
	fake_mover_kind = SG_RUNE_MECHANISM_DOOR;
	fake_mover_role = SG_MECH_NODE_SECRET_DOOR;
	fake_mover_spawnflags = 1U;
	fake_mover_move_direction_x = 1.0f;
	CHECK(SG_RuneCompactBuilderBuild(&input, &builder, &error));
	CHECK(builder != NULL);
	portal_vertices[0].value[0] = 402;
	portal_vertices[0].value[1] = 402;
	portal_vertices[0].value[2] = 40;
	portal_vertices[1].value[0] = 410;
	portal_vertices[1].value[1] = 402;
	portal_vertices[1].value[2] = 40;
	portal_vertices[2].value[0] = 402;
	portal_vertices[2].value[1] = 410;
	portal_vertices[2].value[2] = 40;
	memset(&fake_geometry_view, 0, sizeof(fake_geometry_view));
	fake_geometry_view.cells = cells;
	fake_geometry_view.cell_count = 2U;
	fake_geometry_view.facets = &facet;
	fake_geometry_view.facet_count = 1U;
	fake_geometry_view.incidences = incidences;
	fake_geometry_view.incidence_count = 2U;
	fake_geometry_view.vertices = portal_vertices;
	fake_geometry_view.vertex_count = 3U;
	fake_geometry_view.portals = &portal;
	fake_geometry_view.portal_count = 1U;
	fake_geometry_view.source_surfaces = &surface;
	fake_geometry_view.source_surface_count = 1U;
	fake_geometry_view.source_surface_vertices = source_vertices;
	fake_geometry_view.source_surface_vertex_count = 3U;
	fake_geometry_read_enabled = 1;
	fake_identity_matches = 1;
	fake_model_trace_allsolid = 1;
	fake_model_trace_origin_x = 50.0f;
	host_result = SG_RuneCompactBuilderOwnerMoverTransport(builder, geometry,
		&request, &result);
	CHECK(host_result.status == SG_HOST_LAW_OK && !result.applicable &&
		result.failure == SG_RUNE_COMPACT_BUILDER_MOVER_FAILURE_NONE);
	SG_RuneCompactBuilderDestroy(builder);
	fake_geometry_read_enabled = 0;
	fake_identity_matches = 0;
	fake_model_trace_allsolid = 0;
	fake_model_trace_origin_x = INFINITY;
}

/* The selected catalog root covers the portal, while only the authenticated
 * TEAM master collision instance closes it at the source state.  A successful
 * certificate therefore requires both exact surface provenance and host-side
 * group aggregation. */
static void TestOwnerTeamPortalCertification(void)
{
	struct sg_host_law_construction_s construction = { 1U };
	sg_rune_compact_builder_input_t input = Input(&construction);
	sg_rune_compact_builder_t *builder = NULL;
	sg_rune_compact_builder_error_t error;
	sg_rune_compact_source_surface_t surface;
	sg_rune_q8_vec3_t source_vertices[3];
	sg_rune_q8_vec3_t portal_vertices[3];
	sg_rune_compact_facet_t facet;
	sg_rune_compact_portal_t portal;
	sg_rune_compact_incidence_t incidences[2];
	sg_rune_compact_cell_t cells[2];
	sg_rune_compact_builder_mover_request_t request;
	sg_rune_compact_builder_mover_result_t result;
	sg_rune_vec3_t source_world[3];
	sg_rune_vec3_t destination_world[3];
	const sg_rune_compact_geometry_t *geometry =
		(const sg_rune_compact_geometry_t *)(const void *)&surface;
	sg_host_law_result_t host_result;
	size_t index;

	SetHost();
	SetSourceAuthority();
	fake_source_entity_record_count = 3U;
	for (index = 0U; index < fake_source_entity_record_count; index++) {
		fake_source_entity_records[index].source_ordinal = (uint32_t)index;
		fake_source_entity_records[index].effective_spawnflags = 0U;
	}
	source_snapshot.entity_record_count =
		(uint32_t)fake_source_entity_record_count;
	SG_HostMechanismLawDefault(&host_view.laws.mechanism);
	fake_mover_kind = SG_RUNE_MECHANISM_DOOR;
	fake_mover_move_direction_x = 1.0f;
	fake_door_team = 1;
	CHECK(SG_RuneCompactBuilderBuild(&input, &builder, &error));
	CHECK(builder != NULL);
	memset(&surface, 0, sizeof(surface));
	memset(source_vertices, 0, sizeof(source_vertices));
	memset(portal_vertices, 0, sizeof(portal_vertices));
	memset(&facet, 0, sizeof(facet));
	memset(&portal, 0, sizeof(portal));
	memset(incidences, 0, sizeof(incidences));
	memset(cells, 0, sizeof(cells));
	source_vertices[0].value[2] = 8;
	source_vertices[1].value[2] = 8;
	source_vertices[2].value[2] = 8;
	source_vertices[1].value[1] = 8;
	source_vertices[2].value[0] = 8;
	source_vertices[0].value[1] += 152;
	source_vertices[1].value[1] += 152;
	source_vertices[2].value[1] += 152;
	surface.frame = SG_RUNE_COMPACT_SOURCE_SURFACE_MODEL_LOCAL;
	surface.source.model = 1U;
	surface.vertices.count = 3U;
	facet.kind = SG_RUNE_COMPACT_FACET_POLYGON;
	facet.vertices.count = 3U;
	portal.facet.value = 0U;
	portal.negative_incidence.value = 0U;
	portal.positive_incidence.value = 1U;
	incidences[0].cell.value = 0U;
	incidences[1].cell.value = 1U;
	portal_vertices[0].value[0] = 400;
	portal_vertices[0].value[1] = 400;
	portal_vertices[0].value[2] = 40;
	portal_vertices[1].value[0] = 416;
	portal_vertices[1].value[1] = 400;
	portal_vertices[1].value[2] = 40;
	portal_vertices[2].value[0] = 400;
	portal_vertices[2].value[1] = 416;
	portal_vertices[2].value[2] = 40;
	memset(&fake_geometry_view, 0, sizeof(fake_geometry_view));
	fake_geometry_view.cells = cells;
	fake_geometry_view.cell_count = 2U;
	fake_geometry_view.facets = &facet;
	fake_geometry_view.facet_count = 1U;
	fake_geometry_view.incidences = incidences;
	fake_geometry_view.incidence_count = 2U;
	fake_geometry_view.vertices = portal_vertices;
	fake_geometry_view.vertex_count = 3U;
	fake_geometry_view.portals = &portal;
	fake_geometry_view.portal_count = 1U;
	fake_geometry_view.source_surfaces = &surface;
	fake_geometry_view.source_surface_count = 1U;
	fake_geometry_view.source_surface_vertices = source_vertices;
	fake_geometry_view.source_surface_vertex_count = 3U;
	fake_geometry_read_enabled = 1;
	fake_identity_matches = 1;
	memset(&request, 0, sizeof(request));
	request.mode = SG_RUNE_COMPACT_BUILDER_MOVER_MODE_PORTAL_STATE;
	request.team_portal = 1;
	request.team_master_entity_ordinal = 0U;
	request.mover_entity_ordinal = 1U;
	request.source_state = SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE;
	request.destination_state = SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	request.source_surface_ordinal = 0U;
	request.portal_ordinal = 0U;
	request.entry_cell.value = 0U;
	request.exit_cell.value = 1U;
	request.source_endpoint_entity_ordinal = SG_RUNE_COMPACT_INDEX_NONE;
	request.destination_endpoint_entity_ordinal = SG_RUNE_COMPACT_INDEX_NONE;
	request.route_fanout_ordinal = SG_RUNE_COMPACT_INDEX_NONE;
	request.source_world_vertices_out = source_world;
	request.destination_world_vertices_out = destination_world;
	request.world_vertex_capacity = 3U;
	fake_model_trace_allsolid = 1;
	fake_model_trace_origin_x = 50.0f;
	host_result = SG_RuneCompactBuilderOwnerMoverTransport(builder, geometry,
		&request, &result);
	CHECK(host_result.status == SG_HOST_LAW_OK && result.applicable &&
		result.team_portal && result.team_master_entity_ordinal == 0U &&
		result.mover_model == 1U && result.source_surface_ordinal == 0U &&
		result.source_portal_blocked && !result.destination_portal_blocked &&
		result.elapsed_ms == 200U);
	CHECK(source_world[0].value[0] == 51.0f &&
		destination_world[0].value[0] == 59.0f);
	SG_RuneCompactBuilderDestroy(builder);
	builder = NULL;

	/* A self-referential TEAM edge is malformed host topology, not an ordinary
	 * no-overlap candidate miss. */
	SetHost();
	SetSourceAuthority();
	fake_source_entity_record_count = 3U;
	for (index = 0U; index < fake_source_entity_record_count; index++) {
		fake_source_entity_records[index].source_ordinal = (uint32_t)index;
		fake_source_entity_records[index].effective_spawnflags = 0U;
	}
	source_snapshot.entity_record_count =
		(uint32_t)fake_source_entity_record_count;
	SG_HostMechanismLawDefault(&host_view.laws.mechanism);
	fake_mover_kind = SG_RUNE_MECHANISM_DOOR;
	fake_mover_move_direction_x = 1.0f;
	fake_door_team = 1;
	fake_door_team_malformed = 1;
	CHECK(SG_RuneCompactBuilderBuild(&input, &builder, &error));
	CHECK(builder != NULL);
	fake_geometry_view.cells = cells;
	fake_geometry_view.cell_count = 2U;
	fake_geometry_view.facets = &facet;
	fake_geometry_view.facet_count = 1U;
	fake_geometry_view.incidences = incidences;
	fake_geometry_view.incidence_count = 2U;
	fake_geometry_view.vertices = portal_vertices;
	fake_geometry_view.vertex_count = 3U;
	fake_geometry_view.portals = &portal;
	fake_geometry_view.portal_count = 1U;
	fake_geometry_view.source_surfaces = &surface;
	fake_geometry_view.source_surface_count = 1U;
	fake_geometry_view.source_surface_vertices = source_vertices;
	fake_geometry_view.source_surface_vertex_count = 3U;
	fake_geometry_read_enabled = 1;
	fake_identity_matches = 1;
	memset(&result, 0, sizeof(result));
	host_result = SG_RuneCompactBuilderOwnerMoverTransport(builder, geometry,
		&request, &result);
	CHECK(host_result.status == SG_HOST_LAW_EVALUATION_FAILED);
	SG_RuneCompactBuilderDestroy(builder);
	fake_door_team = 0;
	fake_door_team_malformed = 0;
	fake_geometry_read_enabled = 0;
	fake_identity_matches = 0;
	fake_model_trace_allsolid = 0;
	fake_model_trace_origin_x = INFINITY;
}
#endif

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
	entity_audit_calls = 0;
	visibility_audit_calls = 0;
	CHECK(SG_RuneCompactBuilderBuildDevelopmentAudit(&input, &builder,
		&error));
	CHECK(builder != NULL);
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
		FAILURE_SEMANTICS_BUILD,
		FAILURE_ENTITY_BUILD,
		FAILURE_ENTITY_AUDIT,
		FAILURE_VISIBILITY_BUILD,
		FAILURE_VISIBILITY_AUDIT,
		FAILURE_PMOVE_EVALUATOR_ACQUIRE
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
	TestOwnerReadFailsAfterAuthorityRevocation();
#if !defined(SG_COMPACT_BUILDER_REAL_ENTITY_SEMANTICS)
	TestOwnerModelLocalQ8Transform();
	TestOwnerTransformUsesSpawnResolvedAngularPose();
	TestAngularMoverIdentityRejectsDirtyInactiveBytes();
	TestOwnerMoverTransportCandidateStates();
	TestOwnerMoverTransportTransformProvenance();
#if !defined(SG_COMPACT_BUILDER_REAL_SOURCE_AUTHORITY)
	TestOwnerTrainRouteCertification();
#endif
	TestOwnerPortalPolygonCertification();
	TestOwnerTeamPortalCertification();
#endif
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
