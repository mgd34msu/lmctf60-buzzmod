#define failures semantic_fixture_failures
#define fixture_s semantic_fixture_s
#define fixture_t semantic_fixture_t
#define Fixture SemanticFixture
#define BindFixture SemanticBindFixture
#define Build SemanticBuild
#define SetCrouchingBottom SemanticSetCrouchingBottom
#define Set3 SemanticSet3
#define SetPlane SemanticSetPlane
#define SetFace SemanticSetFace
#define main semantic_fixture_existing_main
int semantic_fixture_existing_main(void);
#include "sg_configuration_semantics_test.c"
#undef main
#undef SetFace
#undef SetPlane
#undef Set3
#undef SetCrouchingBottom
#undef Build
#undef BindFixture
#undef Fixture
#undef fixture_t
#undef fixture_s
#undef failures
#undef CHECK

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

static void TestVisibleOccludedAndAreaPvs(void)
{
	built_fixture_t built;
	sg_static_visibility_result_t result;
	sg_static_visibility_error_t error;
	float left[3], right[3];

	CHECK(BuildFixture(&built, 0, 0, 0, 1, 0, 0.0f));
	if (built.visibility)
	{
		SidePoints(0, 0.0f, left, right);
		CHECK(SG_StaticVisibilityQueryPoints(&built.fixture.authority, &empty_scene,
			built.configuration, built.semantics, built.visibility, left, right,
			&result, &error));
		CHECK(result.classification == SG_STATIC_VISIBILITY_VISIBLE);
		DestroyFixture(&built);
	}
	CHECK(BuildFixture(&built, 1, 0, 0, 1, 0, 0.0f));
	if (built.visibility)
	{
		SidePoints(0, 0.0f, left, right);
		CHECK(SG_StaticVisibilityQueryPoints(&built.fixture.authority, &empty_scene,
			built.configuration, built.semantics, built.visibility, left, right,
			&result, &error));
		CHECK(result.classification == SG_STATIC_VISIBILITY_OCCLUDED);
		CHECK(result.reason == SG_STATIC_VISIBILITY_REASON_STATIC_WORLD);
		DestroyFixture(&built);
	}
	CHECK(BuildFixture(&built, 0, 1, 1, 1, 0, 0.0f));
	if (built.visibility)
	{
		SidePoints(0, 0.0f, left, right);
		CHECK(SG_StaticVisibilityQueryPoints(&built.fixture.authority, &empty_scene,
			built.configuration, built.semantics, built.visibility, left, right,
			&result, &error));
		CHECK(result.classification == SG_STATIC_VISIBILITY_CONDITIONAL);
		CHECK(result.reason == SG_STATIC_VISIBILITY_REASON_AREA_PORTAL_STATE);
		DestroyFixture(&built);
	}
	CHECK(BuildFixture(&built, 0, 1, 1, 0, 0, 0.0f));
	if (built.visibility)
	{
		SidePoints(0, 0.0f, left, right);
		CHECK(SG_StaticVisibilityQueryPoints(&built.fixture.authority, &empty_scene,
			built.configuration, built.semantics, built.visibility, left, right,
			&result, &error));
		CHECK(result.classification == SG_STATIC_VISIBILITY_OCCLUDED);
		CHECK(result.reason == SG_STATIC_VISIBILITY_REASON_PVS);
		DestroyFixture(&built);
	}
	CHECK(BuildFixture(&built, 0, 1, 0, 1, 0, 0.0f));
	if (built.visibility)
	{
		SidePoints(0, 0.0f, left, right);
		CHECK(SG_StaticVisibilityQueryPoints(&built.fixture.authority, &empty_scene,
			built.configuration, built.semantics, built.visibility, left, right,
			&result, &error));
		CHECK(result.classification == SG_STATIC_VISIBILITY_OCCLUDED);
		CHECK(result.reason == SG_STATIC_VISIBILITY_REASON_AREA_GRAPH);
		DestroyFixture(&built);
	}
}

static void TestConditionalMoverScene(void)
{
	built_fixture_t built;
	sg_host_collision_instance_t instance;
	sg_host_collision_scene_t scene;
	sg_host_collision_trace_t trace;
	sg_static_visibility_result_t result;
	sg_static_visibility_error_t error;
	static const float zero[3] = { 0.0f, 0.0f, 0.0f };
	float left[3], right[3];
	float same_partition[3] = { -10.0f, 0.0f, 0.0f };
	uint32_t surface;
	int found_surface = 0;

	CHECK(BuildFixture(&built, 0, 0, 0, 1, 0, 0.0f));
	if (!built.visibility)
		return;
	SidePoints(0, 0.0f, left, right);
	memset(&instance, 0, sizeof(instance));
	instance.instance_id = 17;
	instance.model_index = 1;
	instance.transform.origin[0] = -50.0f;
	scene.instances = &instance;
	scene.instance_count = 1;
	CHECK(SG_HostCollisionTraceModel(&built.fixture.authority, 0, NULL, left,
		zero, zero, right, SG_HOST_CONTENTS_SOLID, &trace));
	CHECK(trace.fraction == 1.0f);
	CHECK(SG_HostCollisionTraceModel(&built.fixture.authority, 1,
		&instance.transform, left, zero, zero, right, SG_HOST_CONTENTS_SOLID,
		&trace));
	CHECK(trace.fraction < 1.0f && trace.model_index == 1);
	CHECK(SG_StaticVisibilityQueryPoints(&built.fixture.authority, &scene,
		built.configuration, built.semantics, built.visibility, left, right,
		&result, &error));
	CHECK(result.classification == SG_STATIC_VISIBILITY_CONDITIONAL);
	CHECK(result.reason == SG_STATIC_VISIBILITY_REASON_MOVING_SUBMODEL);
	CHECK(result.trace.model_index == 1 && result.trace.instance_id == 17);
	CHECK(SG_HostCollisionTraceModel(&built.fixture.authority, 1,
		&instance.transform, left, zero, zero, same_partition,
		SG_HOST_CONTENTS_SOLID, &trace));
	CHECK(trace.fraction < 1.0f && trace.model_index == 1);
	CHECK(SG_StaticVisibilityQueryPoints(&built.fixture.authority, &scene,
		built.configuration, built.semantics, built.visibility, left,
		same_partition, &result, &error));
	CHECK(result.source_partition == result.destination_partition);
	CHECK(result.classification == SG_STATIC_VISIBILITY_CONDITIONAL);
	CHECK(result.reason == SG_STATIC_VISIBILITY_REASON_MOVING_SUBMODEL);
	CHECK(result.trace.model_index == 1 && result.trace.instance_id == 17);
	CHECK(!SG_StaticVisibilityQueryPoints(&built.fixture.authority, NULL,
		built.configuration, built.semantics, built.visibility, left, right,
		&result, &error));
	CHECK(error.code == SG_STATIC_VISIBILITY_ERROR_INVALID_ARGUMENT);
	DestroyFixture(&built);

	CHECK(BuildFixture(&built, 1, 0, 0, 1, 0, 0.0f));
	if (!built.visibility)
		return;
	SidePoints(0, 0.0f, left, right);
	CHECK(SG_StaticVisibilityQueryPoints(&built.fixture.authority, &scene,
		built.configuration, built.semantics, built.visibility, left, right,
		&result, &error));
	CHECK(result.classification == SG_STATIC_VISIBILITY_OCCLUDED);
	CHECK(result.reason == SG_STATIC_VISIBILITY_REASON_STATIC_WORLD);
	CHECK(result.trace.model_index == 0 && result.trace.instance_id == 0);
	for (surface = 0; surface < built.semantics->hook_surface_count; surface++)
	{
		const sg_configuration_hook_surface_t *record =
			&built.semantics->hook_surfaces[surface];
		float target[3] = { 0.0f, 0.0f, 0.0f };
		uint32_t vertex, axis;

		if (record->model != 0 || record->normal[0] > -0.9f)
			continue;
		for (vertex = 0; vertex < record->vertex_count; vertex++)
			for (axis = 0; axis < 3; axis++)
				target[axis] += built.semantics->hook_vertices[
					record->first_vertex + vertex].value[axis];
		for (axis = 0; axis < 3; axis++)
			target[axis] /= (float)record->vertex_count;
		CHECK(SG_StaticVisibilityQuerySurface(&built.fixture.authority, &scene,
			built.configuration, built.semantics, built.visibility, left, surface,
			target, &result, &error));
		CHECK(result.classification == SG_STATIC_VISIBILITY_CONDITIONAL);
		CHECK(result.reason == SG_STATIC_VISIBILITY_REASON_MOVING_SUBMODEL);
		CHECK(result.trace.model_index == 1 && result.trace.instance_id == 17);
		CHECK(!SG_StaticVisibilityQuerySurface(&built.fixture.authority, NULL,
			built.configuration, built.semantics, built.visibility, left, surface,
			target, &result, &error));
		CHECK(error.code == SG_STATIC_VISIBILITY_ERROR_INVALID_ARGUMENT);
		found_surface = 1;
		break;
	}
	CHECK(found_surface);
	found_surface = 0;
	for (surface = 0; surface < built.semantics->hook_surface_count; surface++)
	{
		const sg_configuration_hook_surface_t *record =
			&built.semantics->hook_surfaces[surface];
		float target[3] = { 0.0f, 0.0f, 0.0f };
		uint32_t vertex, axis;

		if (record->model != 0 || record->normal[0] < 0.9f)
			continue;
		for (vertex = 0; vertex < record->vertex_count; vertex++)
			for (axis = 0; axis < 3; axis++)
				target[axis] += built.semantics->hook_vertices[
					record->first_vertex + vertex].value[axis];
		for (axis = 0; axis < 3; axis++)
			target[axis] /= (float)record->vertex_count;
		CHECK(SG_StaticVisibilityQuerySurface(&built.fixture.authority, &scene,
			built.configuration, built.semantics, built.visibility, left, surface,
			target, &result, &error));
		CHECK(result.classification == SG_STATIC_VISIBILITY_OCCLUDED);
		CHECK(result.reason == SG_STATIC_VISIBILITY_REASON_STATIC_WORLD);
		CHECK(result.trace.model_index == 0 && result.trace.instance_id == 0);
		found_surface = 1;
		break;
	}
	CHECK(found_surface);
	DestroyFixture(&built);
}

static void TestTargetSpecificSurfaceLocation(void)
{
	fixture_t fixture = Fixture(1, 0, 0, 1, 0, 0.0f);
	built_fixture_t built;
	sg_static_visibility_result_t result;
	sg_static_visibility_error_t error;
	float source[3] = { -100.0f, 100.0f, 0.0f };
	float target[3] = { -1.0f, -100.0f, 0.0f };
	uint32_t cluster, set, surface;
	int found = 0;

	SetPlane(&fixture.planes[7], 0.0f, 1.0f, 0.0f, 0.0f);
	fixture.nodes[1].children[0] = 2;
	fixture.nodes[2].plane = 7;
	fixture.nodes[2].children[0] = -2;
	fixture.nodes[2].children[1] = -4;
	fixture.leaves[4] = fixture.leaves[3];
	fixture.leaves[3].contents = 0;
	fixture.leaves[3].cluster = 2;
	fixture.leaves[3].area = 1;
	fixture.leaves[3].first_leaf_brush = 0;
	fixture.leaves[3].leaf_brush_count = 0;
	fixture.models[1].headnode = -5;
	fixture.world.plane_count = 8;
	fixture.world.node_count = 3;
	fixture.world.leaf_count = 5;
	fixture.world.visibility.cluster_count = 3;
	fixture.world.visibility.byte_count = 31;
	memset(fixture.visibility_bytes, 0, sizeof(fixture.visibility_bytes));
	WriteU32(fixture.visibility_bytes, 3);
	for (cluster = 0; cluster < 3; cluster++)
		for (set = 0; set < SG_BSP_VISIBILITY_SET_COUNT; set++)
		{
			fixture.visibility_offsets[cluster][set] = 28U + cluster;
			WriteU32(fixture.visibility_bytes + 4U + cluster * 8U + set * 4U,
				28U + cluster);
		}
	fixture.visibility_bytes[28] = UINT8_C(1);
	fixture.visibility_bytes[29] = UINT8_C(7);
	fixture.visibility_bytes[30] = UINT8_C(7);
	CHECK(BuildPreparedFixture(&built, fixture));
	if (!built.visibility)
		return;
	for (surface = 0; surface < built.semantics->hook_surface_count; surface++)
	{
		const sg_configuration_hook_surface_t *record =
			&built.semantics->hook_surfaces[surface];

		if (record->model != 0 || record->normal[0] > -0.9f)
			continue;
		CHECK(SG_StaticVisibilityQuerySurface(&built.fixture.authority,
			&empty_scene, built.configuration, built.semantics, built.visibility,
			source, surface, target, &result, &error));
		CHECK(result.classification == SG_STATIC_VISIBILITY_OCCLUDED);
		CHECK(result.reason == SG_STATIC_VISIBILITY_REASON_PVS);
		found = 1;
		break;
	}
	CHECK(found);
	DestroyFixture(&built);
}

static void TestPvsLoaderLawAndZeroRun(void)
{
	built_fixture_t built;
	fixture_t fixture;
	sg_static_visibility_t *visibility = NULL;
	sg_static_visibility_limits_t limits;
	sg_static_visibility_error_t error;
	sg_static_visibility_result_t result;
	float left[3], right[3];
	uint32_t cluster, set;

	CHECK(BuildFixture(&built, 0, 0, 0, 1, 0, 0.0f));
	if (!built.visibility)
		return;
	SG_StaticVisibilityDestroy(built.visibility);
	built.visibility = NULL;
	built.fixture.visibility_offsets[0][0] = 0;
	WriteU32(built.fixture.visibility_bytes + 4, 0);
	SG_StaticVisibilityDefaultLimits(&limits);
	CHECK(!SG_StaticVisibilityBuild(&built.fixture.authority,
		built.configuration, built.semantics, &limits, &visibility, &error));
	CHECK(!visibility);
	CHECK(error.code == SG_STATIC_VISIBILITY_ERROR_INVALID_SOURCE);
	DestroyFixture(&built);

	fixture = Fixture(0, 0, 0, 1, 0, 0.0f);
	fixture.leaves[0].cluster = -1;
	fixture.leaves[1].cluster = -1;
	fixture.leaves[2].cluster = -1;
	fixture.world.visibility.cluster_count = 0;
	fixture.world.visibility.byte_count = 4;
	memset(fixture.visibility_bytes, 0, sizeof(fixture.visibility_bytes));
	CHECK(BuildPreparedFixture(&built, fixture));
	if (!built.visibility)
		return;
	SidePoints(0, 0.0f, left, right);
	CHECK(SG_StaticVisibilityQueryPoints(&built.fixture.authority, &empty_scene,
		built.configuration, built.semantics, built.visibility, left, right,
		&result, &error));
	CHECK(result.classification == SG_STATIC_VISIBILITY_VISIBLE);
	SG_StaticVisibilityDestroy(built.visibility);
	built.visibility = NULL;
	built.fixture.world.visibility.bit_offsets = built.fixture.visibility_offsets;
	SG_StaticVisibilityDefaultLimits(&limits);
	CHECK(!SG_StaticVisibilityBuild(&built.fixture.authority,
		built.configuration, built.semantics, &limits, &visibility, &error));
	CHECK(!visibility);
	CHECK(error.code == SG_STATIC_VISIBILITY_ERROR_INVALID_SOURCE);
	DestroyFixture(&built);

	fixture = Fixture(0, 0, 0, 1, 0, 0.0f);
	fixture.leaves[0].cluster = 8;
	fixture.world.visibility.cluster_count = 9;
	fixture.world.visibility.byte_count = 79;
	memset(fixture.visibility_bytes, 0, sizeof(fixture.visibility_bytes));
	WriteU32(fixture.visibility_bytes, 9);
	for (cluster = 0; cluster < 9; cluster++)
		for (set = 0; set < SG_BSP_VISIBILITY_SET_COUNT; set++)
		{
			fixture.visibility_offsets[cluster][set] = 76;
			WriteU32(fixture.visibility_bytes + 4U + cluster * 8U + set * 4U,
				76);
		}
	fixture.visibility_bytes[76] = UINT8_C(1);
	fixture.visibility_bytes[77] = 0;
	fixture.visibility_bytes[78] = UINT8_C(1);
	CHECK(BuildPreparedFixture(&built, fixture));
	if (!built.visibility)
		return;
	SidePoints(0, 0.0f, left, right);
	CHECK(SG_StaticVisibilityQueryPoints(&built.fixture.authority, &empty_scene,
		built.configuration, built.semantics, built.visibility, left, right,
		&result, &error));
	CHECK(result.classification == SG_STATIC_VISIBILITY_OCCLUDED);
	CHECK(result.reason == SG_STATIC_VISIBILITY_REASON_PVS);
	DestroyFixture(&built);
}

static void TestSurfaceQueries(void)
{
	built_fixture_t built;
	sg_static_visibility_result_t result;
	sg_static_visibility_error_t error;
	float left[3], right[3];
	uint32_t source_partition, surface;
	int saw_world = 0, saw_moving = 0, saw_sky = 0;

	CHECK(BuildFixture(&built, 1, 0, 0, 1, 0, 0.0f));
	if (!built.visibility)
		return;
	SidePoints(0, 0.0f, left, right);
	CHECK(SG_StaticVisibilityQueryPoints(&built.fixture.authority, &empty_scene,
		built.configuration, built.semantics, built.visibility, left, right,
		&result, &error));
	CHECK(result.classification == SG_STATIC_VISIBILITY_OCCLUDED);
	source_partition = result.source_partition;
	for (surface = 0; surface < built.semantics->hook_surface_count; surface++)
	{
		const sg_configuration_hook_surface_t *record =
			&built.semantics->hook_surfaces[surface];
		float target[3] = { 0.0f, 0.0f, 0.0f };
		uint32_t vertex, axis;

		for (vertex = 0; vertex < record->vertex_count; vertex++)
			for (axis = 0; axis < 3; axis++)
				target[axis] += built.semantics->hook_vertices[
					record->first_vertex + vertex].value[axis];
		for (axis = 0; axis < 3; axis++)
			target[axis] /= (float)record->vertex_count;
		if (record->flags & SG_CONFIGURATION_HOOK_SURFACE_MOVING_MODEL)
		{
			uint32_t saved_cluster = built.visibility->partitions[
				source_partition].bsp_cluster;

			built.visibility->partitions[source_partition].bsp_cluster =
				UINT32_MAX - 1U;
			CHECK(!SG_StaticVisibilityQuerySurface(&built.fixture.authority,
				&empty_scene, built.configuration, built.semantics,
				built.visibility, left, surface, target, &result, &error));
			CHECK(error.code == SG_STATIC_VISIBILITY_ERROR_SOURCE_MISMATCH);
			built.visibility->partitions[source_partition].bsp_cluster =
				saved_cluster;
			CHECK(SG_StaticVisibilityQuerySurface(&built.fixture.authority, &empty_scene,
				built.configuration, built.semantics, built.visibility, left,
				surface, target, &result, &error));
			CHECK(result.classification == SG_STATIC_VISIBILITY_CONDITIONAL);
			CHECK(result.reason ==
				SG_STATIC_VISIBILITY_REASON_MOVING_SUBMODEL);
			saw_moving = 1;
		}
		else if (record->model == 0 && fabsf(record->normal[0]) > 0.9f)
		{
			CHECK(SG_StaticVisibilityQuerySurface(&built.fixture.authority, &empty_scene,
				built.configuration, built.semantics, built.visibility, left,
				surface, target, &result, &error));
			saw_world = 1;
		}
		if (record->flags & SG_CONFIGURATION_HOOK_SURFACE_SKY)
			saw_sky = 1;
	}
	CHECK(saw_world);
	CHECK(saw_moving);
	CHECK(!saw_sky);
	DestroyFixture(&built);
}

static void TestAuditMutationOverflowDeterminismAndHotQueries(void)
{
	built_fixture_t built;
	sg_static_visibility_t *second = NULL, *failed = NULL;
	sg_static_visibility_limits_t limits;
	sg_static_visibility_error_t error;
	sg_static_visibility_audit_result_t audit;
	sg_static_visibility_result_t result;
	float left[3], right[3];
	uint32_t saved_area, saved_cluster, iteration;
	float saved_distance;

	CHECK(BuildFixture(&built, 1, 0, 0, 1, 0, 0.0f));
	if (!built.visibility)
		return;
	CHECK(SG_StaticVisibilityAudit(&built.fixture.authority,
		built.configuration, built.semantics, built.visibility, &audit));
	SG_StaticVisibilityDefaultLimits(&limits);
	CHECK(SG_StaticVisibilityBuild(&built.fixture.authority,
		built.configuration, built.semantics, &limits, &second, &error));
	if (second)
	{
		CHECK(second->partition_count == built.visibility->partition_count);
		CHECK(memcmp(second->partitions, built.visibility->partitions,
			(size_t)second->partition_count * sizeof(*second->partitions)) == 0);
		CHECK(second->area_count == built.visibility->area_count);
		CHECK(memcmp(second->area_components,
			built.visibility->area_components,
			(size_t)second->area_count * sizeof(*second->area_components)) == 0);
		CHECK(second->occluder_count == built.visibility->occluder_count);
		CHECK(memcmp(second->occluders, built.visibility->occluders,
			(size_t)second->occluder_count * sizeof(*second->occluders)) == 0);
	}
	limits.max_occluders = 1;
	CHECK(!SG_StaticVisibilityBuild(&built.fixture.authority,
		built.configuration, built.semantics, &limits, &failed, &error));
	CHECK(!failed);
	CHECK(error.code == SG_STATIC_VISIBILITY_ERROR_OVERFLOW);
	saved_area = built.visibility->partitions[0].bsp_area;
	built.visibility->partitions[0].bsp_area = saved_area + 1U;
	CHECK(!SG_StaticVisibilityAudit(&built.fixture.authority,
		built.configuration, built.semantics, built.visibility, &audit));
	CHECK(audit.code == SG_STATIC_VISIBILITY_AUDIT_PARTITION_DISAGREEMENT);
	built.visibility->partitions[0].bsp_area = saved_area;
	saved_distance = built.fixture.planes[1].distance;
	built.fixture.planes[1].distance = saved_distance + 0.5f;
	CHECK(!SG_StaticVisibilityAudit(&built.fixture.authority,
		built.configuration, built.semantics, built.visibility, &audit));
	built.fixture.planes[1].distance = saved_distance;
	CHECK(SG_StaticVisibilityAudit(&built.fixture.authority,
		built.configuration, built.semantics, built.visibility, &audit));
	SidePoints(0, 0.0f, left, right);
	saved_cluster = built.visibility->partitions[0].bsp_cluster;
	built.visibility->partitions[0].bsp_cluster = UINT32_MAX - 1U;
	CHECK(!SG_StaticVisibilityQueryRegions(&built.fixture.authority,
		built.configuration, built.semantics, built.visibility, 0, 0, &result,
		&error));
	CHECK(error.code == SG_STATIC_VISIBILITY_ERROR_SOURCE_MISMATCH);
	CHECK(!SG_StaticVisibilityQueryPoints(&built.fixture.authority, &empty_scene,
		built.configuration, built.semantics, built.visibility,
		built.semantics->regions[0].interior_witness.value,
		built.semantics->regions[0].interior_witness.value, &result, &error));
	CHECK(error.code == SG_STATIC_VISIBILITY_ERROR_SOURCE_MISMATCH);
	built.visibility->partitions[0].bsp_cluster = saved_cluster;
#ifdef SG_STATIC_VISIBILITY_WRAP_ALLOC
	wrapped_query_allocations = 0;
	count_wrapped_query_allocations = 1;
#endif
	for (iteration = 0; iteration < 64; iteration++)
		CHECK(SG_StaticVisibilityQueryPoints(&built.fixture.authority, &empty_scene,
			built.configuration, built.semantics, built.visibility, left, right,
			&result, &error));
#ifdef SG_STATIC_VISIBILITY_WRAP_ALLOC
	count_wrapped_query_allocations = 0;
	CHECK(wrapped_query_allocations == 0);
#endif
	SG_StaticVisibilityDestroy(second);
	DestroyFixture(&built);
}

static void TestLargeCoordinateNonAxialSurfaceLocation(void)
{
	semantic_fixture_t fixture = SemanticFixture();
	sg_configuration_semantics_t *semantics = NULL;
	sg_static_visibility_t *visibility = NULL;
	sg_static_visibility_limits_t limits;
	sg_static_visibility_error_t error;
	sg_bsp_area_t areas[2];
	const float shift = 2048.0f;
	uint32_t plane, face, vertex, model, surface;
	int found = 0, found_sky = 0;

	memset(areas, 0, sizeof(areas));
	SemanticBindFixture(&fixture);
	fixture.world.areas = areas;
	fixture.world.area_count = 2;
	fixture.cell.stance = SG_RUNE_STANCE_CROUCHING;
	fixture.faces[0].plane.source_variant = SG_RUNE_STANCE_CROUCHING;
	SemanticSetCrouchingBottom(&fixture, 0.0f, 0.6f, 0.8f, 9.6f);
	for (plane = 0; plane < fixture.world.plane_count; plane++)
		fixture.planes[plane].distance +=
			fixture.planes[plane].normal.value[0] * shift;
	for (face = 0; face < fixture.configuration.face_count; face++)
	{
		sg_configuration_plane_t *record = &fixture.faces[face].plane;
		float sign = record->reversed ? -1.0f : 1.0f;

		if (record->source_kind == SG_CONFIGURATION_PLANE_BSP)
			record->distance = sign *
				fixture.planes[record->source_index].distance;
		else if (record->source_kind ==
			SG_CONFIGURATION_PLANE_EXPANDED_BRUSH)
		{
			const sg_bsp_plane_t *source = &fixture.planes[
				fixture.sides[record->source_index].plane];
			const sg_rune_hull_profile_t *hull =
				&fixture.identity.crouching_hull;
			float minimum = 0.0f;
			uint32_t axis;

			for (axis = 0; axis < 3; axis++)
				minimum += source->normal.value[axis] >= 0.0f ?
					source->normal.value[axis] * hull->mins.value[axis] :
					source->normal.value[axis] * hull->maxs.value[axis];
			record->distance = sign * (source->distance - minimum);
		}
	}
	for (vertex = 0; vertex < fixture.configuration.vertex_count; vertex++)
		fixture.vertices[vertex].value[0] += shift;
	fixture.cell.bounds.mins.value[0] += shift;
	fixture.cell.bounds.maxs.value[0] += shift;
	fixture.cell.interior_witness.value[0] += shift;
	for (model = 0; model < fixture.world.model_count; model++)
	{
		fixture.models[model].mins.value[0] += shift;
		fixture.models[model].maxs.value[0] += shift;
		fixture.models[model].origin.value[0] += shift;
	}
	CHECK(SemanticBuild(&fixture, &semantics));
	if (!semantics)
		return;
	SG_StaticVisibilityDefaultLimits(&limits);
	CHECK(SG_StaticVisibilityBuild(&fixture.authority,
		&fixture.configuration, semantics, &limits, &visibility, &error));
	if (!visibility)
	{
		SG_ConfigurationSemanticsDestroy(semantics);
		return;
	}
	for (surface = 0; surface < semantics->hook_surface_count; surface++)
	{
		const sg_configuration_hook_surface_t *record =
			&semantics->hook_surfaces[surface];
		if (record->flags & SG_CONFIGURATION_HOOK_SURFACE_SKY)
		{
			float target[3] = { 0.0f, 0.0f, 0.0f };
			sg_static_visibility_result_t query;
			uint32_t saved_cluster = visibility->partitions[0].bsp_cluster;
			uint32_t local, axis;

			for (local = 0; local < record->vertex_count; local++)
				for (axis = 0; axis < 3; axis++)
					target[axis] += semantics->hook_vertices[
						record->first_vertex + local].value[axis];
			for (axis = 0; axis < 3; axis++)
				target[axis] /= (float)record->vertex_count;
			visibility->partitions[0].bsp_cluster = UINT32_MAX - 1U;
			CHECK(!SG_StaticVisibilityQuerySurface(&fixture.authority,
				&empty_scene, &fixture.configuration, semantics, visibility,
				semantics->regions[0].interior_witness.value, surface, target,
				&query, &error));
			CHECK(error.code == SG_STATIC_VISIBILITY_ERROR_SOURCE_MISMATCH);
			visibility->partitions[0].bsp_cluster = saved_cluster;
			CHECK(SG_StaticVisibilityQuerySurface(&fixture.authority, &empty_scene,
				&fixture.configuration, semantics, visibility,
				semantics->regions[0].interior_witness.value, surface, target,
				&query, &error));
			CHECK(query.classification == SG_STATIC_VISIBILITY_OCCLUDED);
			CHECK(query.reason == SG_STATIC_VISIBILITY_REASON_SKY);
			found_sky = 1;
		}
		if (record->model == 0 && record->brush_side == 4U)
		{
			float probe[3] = { 0.0f, 0.0f, 0.0f };
			sg_host_collision_contents_t contents;
			uint32_t local, axis;

			CHECK(record->normal[0] == 0.0f);
			CHECK(record->normal[1] == 0.6f);
			CHECK(record->normal[2] == 0.8f);
			for (local = 0; local < record->vertex_count; local++)
				for (axis = 0; axis < 3; axis++)
					probe[axis] += semantics->hook_vertices[
						record->first_vertex + local].value[axis];
			for (axis = 0; axis < 3; axis++)
				probe[axis] = probe[axis] / (float)record->vertex_count +
					record->normal[axis] * (1.0f / 32.0f);
			contents = SG_HostCollisionPointContentsModel(&fixture.authority,
				0, NULL, probe);
			CHECK((contents & (SG_HOST_CONTENTS_SOLID |
				SG_HOST_CONTENTS_WINDOW)) == 0U);
			CHECK(fabsf(probe[0]) > 2000.0f);
			found = 1;
		}
	}
	CHECK(found);
	CHECK(found_sky);
	SG_StaticVisibilityDestroy(visibility);
	SG_ConfigurationSemanticsDestroy(semantics);
}

int main(void)
{
	TestVisibleOccludedAndAreaPvs();
	TestConditionalMoverScene();
	TestTargetSpecificSurfaceLocation();
	TestPvsLoaderLawAndZeroRun();
	TestSurfaceQueries();
	TestAuditMutationOverflowDeterminismAndHotQueries();
	TestLargeCoordinateNonAxialSurfaceLocation();
	if (failures)
	{
		fprintf(stderr, "%d static visibility checks failed\n", failures);
		return 1;
	}
	puts("static visibility checks passed");
	return 0;
}
