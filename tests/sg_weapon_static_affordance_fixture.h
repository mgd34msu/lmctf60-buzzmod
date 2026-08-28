#ifndef SG_WEAPON_STATIC_AFFORDANCE_FIXTURE_H
#define SG_WEAPON_STATIC_AFFORDANCE_FIXTURE_H

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../slipgate/sg_static_visibility.h"

#ifdef SG_STATIC_VISIBILITY_WRAP_ALLOC
void *__real_malloc(size_t size);
void *__real_calloc(size_t count, size_t size);
void *__real_realloc(void *pointer, size_t size);
void *__wrap_malloc(size_t size);
void *__wrap_calloc(size_t count, size_t size);
void *__wrap_realloc(void *pointer, size_t size);

static uint64_t wrapped_query_allocations;
static int count_wrapped_query_allocations;

void *__wrap_malloc(size_t size)
{
	if (count_wrapped_query_allocations)
		wrapped_query_allocations++;
	return __real_malloc(size);
}

void *__wrap_calloc(size_t count, size_t size)
{
	if (count_wrapped_query_allocations)
		wrapped_query_allocations++;
	return __real_calloc(count, size);
}

void *__wrap_realloc(void *pointer, size_t size)
{
	if (count_wrapped_query_allocations)
		wrapped_query_allocations++;
	return __real_realloc(pointer, size);
}
#endif

static int failures;
static const sg_host_collision_scene_t empty_scene;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

typedef struct fixture_s
{
	sg_bsp_world_t world;
	sg_bsp_plane_t planes[8];
	sg_bsp_node_t nodes[3];
	sg_bsp_leaf_t leaves[5];
	uint32_t leaf_brushes[2];
	sg_bsp_model_t models[2];
	sg_bsp_brush_t brushes[2];
	sg_bsp_brush_side_t sides[12];
	sg_bsp_texinfo_t texinfos[2];
	uint32_t visibility_offsets[9][SG_BSP_VISIBILITY_SET_COUNT];
	uint8_t visibility_bytes[79];
	sg_bsp_area_t areas[3];
	sg_bsp_areaportal_t areaportals[2];
	sg_rune_model_identity_t identity;
	sg_host_collision_authority_t authority;
} fixture_t;

typedef struct built_fixture_s
{
	fixture_t fixture;
	sg_configuration_space_t *configuration;
	sg_configuration_semantics_t *semantics;
	sg_static_visibility_t *visibility;
	sg_rune_model_t model;
	sg_rune_cell_t *model_cells;
	sg_rune_phase_basis_t *model_phases;
	sg_weapon_static_binding_t binding;
	sg_weapon_static_source_audit_t source_audit;
} built_fixture_t;

static void Set3(float value[3], float x, float y, float z)
{
	value[0] = x;
	value[1] = y;
	value[2] = z;
}

static void SetPlane(sg_bsp_plane_t *plane, float x, float y, float z,
	float distance)
{
	Set3(plane->normal.value, x, y, z);
	plane->distance = distance;
}

static void WriteU32(uint8_t *bytes, uint32_t value)
{
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
	bytes[2] = (uint8_t)(value >> 16);
	bytes[3] = (uint8_t)(value >> 24);
}

static void FixtureFillContentIdentity(sg_rune_v2_content_id_t *identity,
	uint8_t seed)
{
	uint32_t index;

	for (index = 0U; index < SG_RUNE_V2_CONTENT_ID_BYTES; index++)
		identity->bytes[index] = (uint8_t)(seed + (uint8_t)index);
}

static int FixtureCellCompare(const void *left_pointer,
	const void *right_pointer)
{
	const sg_rune_cell_t *left = left_pointer;
	const sg_rune_cell_t *right = right_pointer;

	if (left->id.value.source_set_identity !=
		right->id.value.source_set_identity)
		return left->id.value.source_set_identity <
			right->id.value.source_set_identity ? -1 : 1;
	if (left->id.value.high != right->id.value.high)
		return left->id.value.high < right->id.value.high ? -1 : 1;
	if (left->id.value.low != right->id.value.low)
		return left->id.value.low < right->id.value.low ? -1 : 1;
	return 0;
}

static int BuildFixtureModelAndAudit(built_fixture_t *built)
{
	uint32_t index;

	built->model_cells = calloc(built->configuration->cell_count,
		sizeof(*built->model_cells));
	built->model_phases = calloc(built->configuration->cell_count,
		sizeof(*built->model_phases));
	if (!built->model_cells || !built->model_phases)
	{
		fprintf(stderr, "model allocation failed\n");
		return 0;
	}
	for (index = 0U; index < built->configuration->cell_count; index++)
	{
		sg_rune_cell_t *cell = &built->model_cells[index];
		sg_rune_phase_basis_t *phase = &built->model_phases[index];
		sg_rune_order_key_t phase_order = {
			.source_set_identity =
				built->fixture.identity.source_set_identity,
			.domain = SG_RUNE_ORDER_PHASE,
			.source_index = index,
			.local_ordinal = index,
			.variant = 0U
		};

		cell->id = built->configuration->cells[index].id;
		if (!SG_RuneModelStableIdToOrderKey(&cell->id.value, &cell->order))
		{
			fprintf(stderr, "cell order failed %u\n", index);
			return 0;
		}
		cell->bounds = built->configuration->cells[index].bounds;
		cell->phases.first = index;
		cell->phases.count = 1U;
		phase->order = phase_order;
		phase->id.value = SG_RuneModelStableIdFromOrderKey(&phase_order);
		phase->stance = SG_RUNE_STANCE_STANDING;
		phase->motion = SG_RUNE_MOTION_SUPPORTED;
		phase->support = SG_RUNE_SUPPORT_SUPPORTED;
		phase->medium = SG_RUNE_MEDIUM_DRY;
		phase->void_relation = SG_RUNE_VOID_CLEAR;
		phase->reference_frame = SG_RUNE_FRAME_WORLD;
		phase->mover.value = SG_RUNE_STABLE_ID_NONE;
		phase->time_quantum_ms = 8U;
		phase->time_horizon_ms = 8U;
		if (!SG_RuneModelPhaseValid(phase))
		{
			fprintf(stderr, "phase invalid %u\n", index);
			return 0;
		}
	}
	qsort(built->model_cells, built->configuration->cell_count,
		sizeof(*built->model_cells), FixtureCellCompare);
	built->model.version = SG_RUNE_MODEL_VERSION;
	built->model.schema_tag = SG_RUNE_MODEL_SCHEMA_TAG;
	built->model.flags = SG_RUNE_MODEL_IMMUTABLE | SG_RUNE_MODEL_EXACT_BOUND |
		SG_RUNE_MODEL_NO_RUNTIME_ACTORS;
	built->model.identity = built->fixture.identity;
	built->model.completeness.state = SG_RUNE_COMPLETENESS_COMPLETE;
	built->model.completeness.reason = SG_RUNE_FAILURE_NONE;
	built->model.completeness.failure_record = UINT32_MAX;
	built->model.completeness.expected_cells =
		built->configuration->cell_count;
	built->model.completeness.covered_cells =
		built->configuration->cell_count;
	built->model.cells = built->model_cells;
	built->model.cell_count = built->configuration->cell_count;
	built->model.phases = built->model_phases;
	built->model.phase_count = built->configuration->cell_count;
	FixtureFillContentIdentity(&built->binding.artifact_identity, 1U);
	FixtureFillContentIdentity(&built->binding.bsp_identity, 33U);
	FixtureFillContentIdentity(&built->binding.schema_identity, 65U);
	built->binding.source_set_identity =
		built->fixture.identity.source_set_identity;
	built->binding.visibility_revision = 9U;
	built->source_audit.binding = built->binding;
	if (!SG_StaticVisibilityAudit(&built->fixture.authority,
			built->configuration, built->semantics, built->visibility,
			&built->source_audit.visibility))
	{
		fprintf(stderr, "visibility audit failed %u/%u\n",
			(unsigned int)built->source_audit.visibility.code,
			built->source_audit.visibility.record);
		return 0;
	}
	built->source_audit.configuration_cells = built->configuration->cell_count;
	built->source_audit.semantic_regions = built->semantics->region_count;
	built->source_audit.semantic_surfaces =
		built->semantics->hook_surface_count;
	built->source_audit.semantic_surface_vertices =
		built->semantics->hook_vertex_count;
	built->source_audit.model_cells = built->model.cell_count;
	built->source_audit.model_phases = built->model.phase_count;
	return 1;
}

static fixture_t Fixture(int wall, int separate_areas, int connect_areas,
	int pvs_visible, int nonaxial, float coordinate_offset)
{
	fixture_t fixture;
	float nx = nonaxial ? 0.6f : 1.0f;
	float ny = nonaxial ? 0.8f : 0.0f;
	float tx = nonaxial ? 0.8f : 0.0f;
	float ty = nonaxial ? -0.6f : 1.0f;
	float split_distance = coordinate_offset;
	uint32_t side;

	memset(&fixture, 0, sizeof(fixture));
	SetPlane(&fixture.planes[0], nx, ny, 0.0f, split_distance);
	SetPlane(&fixture.planes[1], nx, ny, 0.0f, split_distance + 1.0f);
	SetPlane(&fixture.planes[2], -nx, -ny, 0.0f, -split_distance + 1.0f);
	SetPlane(&fixture.planes[3], tx, ty, 0.0f, 2048.0f);
	SetPlane(&fixture.planes[4], -tx, -ty, 0.0f, 2048.0f);
	SetPlane(&fixture.planes[5], 0.0f, 0.0f, 1.0f, 2048.0f);
	SetPlane(&fixture.planes[6], 0.0f, 0.0f, -1.0f, 2048.0f);
	fixture.nodes[0].plane = 1;
	fixture.nodes[0].children[0] = -1;
	fixture.nodes[0].children[1] = 1;
	fixture.nodes[1].plane = 2;
	fixture.nodes[1].children[0] = -2;
	fixture.nodes[1].children[1] = -3;
	fixture.leaves[0].cluster = 1;
	fixture.leaves[1].cluster = 0;
	fixture.leaves[2].cluster = wall ? -1 : 0;
	fixture.leaves[0].area = separate_areas ? 2U : 1U;
	fixture.leaves[1].area = 1;
	fixture.leaves[2].area = 1;
	fixture.leaves[2].contents = wall ? SG_HOST_CONTENTS_SOLID : 0;
	fixture.leaves[2].first_leaf_brush = 0;
	fixture.leaves[2].leaf_brush_count = 1;
	fixture.leaf_brushes[0] = 0;
	fixture.brushes[0].first_side = 0;
	fixture.brushes[0].side_count = 6;
	fixture.brushes[0].contents = wall ? SG_HOST_CONTENTS_SOLID : 0;
	for (side = 0; side < 6; side++)
	{
		fixture.sides[side].plane = side + 1U;
		fixture.sides[side].texinfo = 0;
		fixture.sides[side + 6U] = fixture.sides[side];
	}
	fixture.leaves[3].contents = SG_HOST_CONTENTS_SOLID;
	fixture.leaves[3].cluster = -1;
	fixture.leaves[3].area = 1;
	fixture.leaves[3].first_leaf_brush = 1;
	fixture.leaves[3].leaf_brush_count = 1;
	fixture.leaf_brushes[1] = 1;
	fixture.brushes[1].first_side = 6;
	fixture.brushes[1].side_count = 6;
	fixture.brushes[1].contents = SG_HOST_CONTENTS_SOLID;
	fixture.texinfos[0].flags = 0;
	fixture.texinfos[1].flags = SG_HOST_SURFACE_SKY;
	fixture.models[0].headnode = 0;
	Set3(fixture.models[0].mins.value, -4096.0f, -4096.0f, -4096.0f);
	Set3(fixture.models[0].maxs.value, 4095.875f, 4095.875f, 4095.875f);
	fixture.models[1] = fixture.models[0];
	fixture.models[1].headnode = -4;
	WriteU32(fixture.visibility_bytes, 2);
	fixture.visibility_offsets[0][0] = 20;
	fixture.visibility_offsets[0][1] = 20;
	fixture.visibility_offsets[1][0] = 21;
	fixture.visibility_offsets[1][1] = 21;
	WriteU32(fixture.visibility_bytes + 4, 20);
	WriteU32(fixture.visibility_bytes + 8, 20);
	WriteU32(fixture.visibility_bytes + 12, 21);
	WriteU32(fixture.visibility_bytes + 16, 21);
	fixture.visibility_bytes[20] = pvs_visible ? UINT8_C(3) : UINT8_C(1);
	fixture.visibility_bytes[21] = UINT8_C(3);
	if (connect_areas)
	{
		fixture.areas[1].first_areaportal = 0;
		fixture.areas[1].areaportal_count = 1;
		fixture.areas[2].first_areaportal = 1;
		fixture.areas[2].areaportal_count = 1;
		fixture.areaportals[0].portal_number = 0;
		fixture.areaportals[0].other_area = 2;
		fixture.areaportals[1].portal_number = 0;
		fixture.areaportals[1].other_area = 1;
	}
	fixture.world.planes = fixture.planes;
	fixture.world.plane_count = 7;
	fixture.world.nodes = fixture.nodes;
	fixture.world.node_count = 2;
	fixture.world.leaves = fixture.leaves;
	fixture.world.leaf_count = 4;
	fixture.world.leaf_brushes = fixture.leaf_brushes;
	fixture.world.leaf_brush_count = 2;
	fixture.world.models = fixture.models;
	fixture.world.model_count = 2;
	fixture.world.brushes = fixture.brushes;
	fixture.world.brush_count = 2;
	fixture.world.brush_sides = fixture.sides;
	fixture.world.brush_side_count = 12;
	fixture.world.texinfos = fixture.texinfos;
	fixture.world.texinfo_count = 2;
	fixture.world.visibility.cluster_count = 2;
	fixture.world.visibility.bit_offsets = fixture.visibility_offsets;
	fixture.world.visibility.bytes = fixture.visibility_bytes;
	fixture.world.visibility.byte_count = 22;
	fixture.world.areas = fixture.areas;
	fixture.world.area_count = 3;
	fixture.world.areaportals = fixture.areaportals;
	fixture.world.areaportal_count = connect_areas ? 2U : 0U;
	fixture.identity.bsp_content_id = UINT64_C(0x1001);
	fixture.identity.entity_semantics_id = UINT64_C(0x1002);
	fixture.identity.physics_abi_id = UINT64_C(0x1003);
	fixture.identity.source_set_identity = UINT64_C(0x1004);
	fixture.identity.schema_id = UINT64_C(0x1005);
	fixture.identity.producer_identity = UINT64_C(0x1006);
	Set3(fixture.identity.standing_hull.mins.value, -16, -16, -24);
	Set3(fixture.identity.standing_hull.maxs.value, 16, 16, 32);
	Set3(fixture.identity.crouching_hull.mins.value, -16, -16, -24);
	Set3(fixture.identity.crouching_hull.maxs.value, 16, 16, 4);
	fixture.identity.physics.gravity = 800;
	fixture.identity.physics.ground_acceleration = 10;
	fixture.identity.physics.air_acceleration = 1;
	fixture.identity.physics.water_acceleration = 10;
	fixture.identity.physics.hook_acceleration = 800;
	fixture.identity.physics.external_acceleration = 10;
	fixture.identity.physics.water_drag = 1;
	fixture.identity.physics.max_velocity = 2000;
	fixture.identity.physics.frame_ms = 100;
	fixture.identity.physics.substep_ms = 10;
	return fixture;
}

static void Rebind(fixture_t *fixture)
{
	sg_host_collision_error_t error;

	fixture->world.planes = fixture->planes;
	fixture->world.nodes = fixture->nodes;
	fixture->world.leaves = fixture->leaves;
	fixture->world.leaf_brushes = fixture->leaf_brushes;
	fixture->world.models = fixture->models;
	fixture->world.brushes = fixture->brushes;
	fixture->world.brush_sides = fixture->sides;
	fixture->world.texinfos = fixture->texinfos;
	fixture->world.visibility.bit_offsets =
		fixture->world.visibility.cluster_count ? fixture->visibility_offsets : NULL;
	fixture->world.visibility.bytes = fixture->visibility_bytes;
	fixture->world.areas = fixture->areas;
	fixture->world.areaportals = fixture->areaportals;
	CHECK(SG_HostCollisionInit(&fixture->authority, &fixture->world,
		&fixture->identity, &error));
}

static int BuildPreparedFixture(built_fixture_t *built, fixture_t fixture)
{
	sg_configuration_error_t configuration_error;
	sg_configuration_semantics_error_t semantics_error;
	sg_static_visibility_error_t visibility_error;
	sg_configuration_semantics_limits_t semantics_limits;
	sg_static_visibility_limits_t visibility_limits;

	memset(built, 0, sizeof(*built));
	built->fixture = fixture;
	Rebind(&built->fixture);
	if (!SG_ConfigurationBuild(&built->fixture.authority, NULL,
			&built->configuration, &configuration_error))
	{
		fprintf(stderr, "configuration build: %s source=%u\n",
			SG_ConfigurationErrorString(configuration_error.code),
			configuration_error.source_index);
		return 0;
	}
	SG_ConfigurationSemanticsDefaultLimits(&semantics_limits);
	if (!SG_ConfigurationSemanticsBuild(&built->fixture.authority,
			built->configuration, &semantics_limits, &built->semantics,
			&semantics_error))
	{
		fprintf(stderr, "semantics build: %s source=%u\n",
			SG_ConfigurationSemanticsErrorString(semantics_error.code),
			semantics_error.source_index);
		return 0;
	}
	SG_StaticVisibilityDefaultLimits(&visibility_limits);
	if (!SG_StaticVisibilityBuild(&built->fixture.authority,
			built->configuration, built->semantics, &visibility_limits,
			&built->visibility, &visibility_error))
	{
		fprintf(stderr, "visibility build: %s source=%u\n",
			SG_StaticVisibilityErrorString(visibility_error.code),
			visibility_error.source_index);
		return 0;
	}
	if (!BuildFixtureModelAndAudit(built))
	{
		fprintf(stderr, "fixture model/audit build failed\n");
		return 0;
	}
	return 1;
}

static int BuildFixture(built_fixture_t *built, int wall, int separate_areas,
	int connect_areas, int pvs_visible, int nonaxial, float coordinate_offset)
{
	return BuildPreparedFixture(built, Fixture(wall, separate_areas,
		connect_areas, pvs_visible, nonaxial, coordinate_offset));
}

static void DestroyFixture(built_fixture_t *built)
{
	free(built->model_phases);
	free(built->model_cells);
	SG_StaticVisibilityDestroy(built->visibility);
	SG_ConfigurationSemanticsDestroy(built->semantics);
	SG_ConfigurationDestroy(built->configuration);
}

static void SidePoints(int nonaxial, float coordinate_offset, float left[3],
	float right[3])
{
	float nx = nonaxial ? 0.6f : 1.0f;
	float ny = nonaxial ? 0.8f : 0.0f;

	Set3(left, nx * (coordinate_offset - 100.0f),
		ny * (coordinate_offset - 100.0f), 0.0f);
	Set3(right, nx * (coordinate_offset + 100.0f),
		ny * (coordinate_offset + 100.0f), 0.0f);
}

#endif /* SG_WEAPON_STATIC_AFFORDANCE_FIXTURE_H */
