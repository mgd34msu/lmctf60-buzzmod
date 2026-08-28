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

#endif /* SG_WEAPON_STATIC_AFFORDANCE_FIXTURE_H */
