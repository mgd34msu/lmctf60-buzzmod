#include <stdint.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../slipgate/sg_configuration_semantics.h"
#include "../slipgate/sg_configuration_audit.h"

static int failures;

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
	sg_bsp_plane_t planes[19];
	sg_bsp_node_t nodes[2];
	sg_bsp_leaf_t leaves[4];
	uint32_t leaf_brushes[7];
	sg_bsp_model_t models[2];
	sg_bsp_brush_t brushes[2];
	sg_bsp_brush_side_t sides[12];
	sg_bsp_texinfo_t texinfos[2];
	uint8_t entities[96];
	sg_rune_model_identity_t identity;
	sg_host_collision_authority_t authority;
	sg_configuration_space_t configuration;
	sg_configuration_cell_t cell;
	sg_configuration_face_t faces[6];
	sg_rune_vec3_t vertices[24];
} fixture_t;

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
	plane->type = z != 0.0f ? 2 : (y != 0.0f ? 1 : 0);
}

static void SetFace(fixture_t *fixture, uint32_t face, float x, float y,
	float z, float distance, uint32_t source_kind, uint32_t source_index,
	const float points[4][3])
{
	uint32_t vertex;

	Set3(fixture->faces[face].plane.normal, x, y, z);
	fixture->faces[face].plane.distance = distance;
	fixture->faces[face].plane.source_kind = source_kind;
	fixture->faces[face].plane.source_index = source_index;
	fixture->faces[face].first_vertex = face * 4U;
	fixture->faces[face].vertex_count = 4;
	for (vertex = 0; vertex < 4; vertex++)
		Set3(fixture->vertices[face * 4U + vertex].value,
			points[vertex][0], points[vertex][1], points[vertex][2]);
}

static fixture_t Fixture(void)
{
	static const char item_entity[] =
		"{\n\"classname\" \"item_flag_team1\"\n\"origin\" \"0 0 20\"\n}\n";
	static const float cube_faces[6][4][3] = {
		{{-8,-8,0},{-8,8,0},{8,8,0},{8,-8,0}},
		{{-8,-8,40},{8,-8,40},{8,8,40},{-8,8,40}},
		{{-8,-8,0},{-8,-8,40},{-8,8,40},{-8,8,0}},
		{{8,-8,0},{8,8,0},{8,8,40},{8,-8,40}},
		{{-8,-8,0},{8,-8,0},{8,-8,40},{-8,-8,40}},
		{{-8,8,0},{-8,8,40},{8,8,40},{8,8,0}}
	};
	fixture_t fixture;
	uint32_t brush, side;

	memset(&fixture, 0, sizeof(fixture));
	SetPlane(&fixture.planes[0], 0, 0, 1, -24);
	SetPlane(&fixture.planes[1], 0, 0, 1, 0);
	fixture.nodes[0].plane = 0;
	fixture.nodes[0].children[0] = 1;
	fixture.nodes[0].children[1] = -3;
	fixture.nodes[1].plane = 1;
	fixture.nodes[1].children[0] = -1;
	fixture.nodes[1].children[1] = -2;
	fixture.leaves[0].contents = 0;
	fixture.leaves[0].cluster = 0;
	fixture.leaves[0].area = 1;
	fixture.leaves[1].contents = SG_HOST_CONTENTS_WATER;
	fixture.leaves[1].cluster = 1;
	fixture.leaves[1].area = 1;
	fixture.leaves[2].contents = SG_HOST_CONTENTS_SOLID;
	fixture.leaves[2].cluster = -1;
	fixture.leaves[2].area = 1;
	for (brush = 0; brush < 3; brush++)
	{
		fixture.leaves[brush].first_leaf_brush = brush;
		fixture.leaves[brush].leaf_brush_count = 1;
		fixture.leaf_brushes[brush] = 0;
	}
	fixture.leaves[3].contents = SG_HOST_CONTENTS_SOLID;
	fixture.leaves[3].cluster = -1;
	fixture.leaves[3].area = 1;
	fixture.leaves[3].first_leaf_brush = 3;
	fixture.leaves[3].leaf_brush_count = 1;
	fixture.leaf_brushes[3] = 1;
	for (brush = 0; brush < 2; brush++)
	{
		float minz = brush == 0 ? -64.0f : 72.0f;
		float maxz = brush == 0 ? -24.0f : 80.0f;
		uint32_t first_plane = 2U + brush * 6U;
		SetPlane(&fixture.planes[first_plane], 1, 0, 0, 64);
		SetPlane(&fixture.planes[first_plane + 1U], -1, 0, 0, 64);
		SetPlane(&fixture.planes[first_plane + 2U], 0, 1, 0, 64);
		SetPlane(&fixture.planes[first_plane + 3U], 0, -1, 0, 64);
		SetPlane(&fixture.planes[first_plane + 4U], 0, 0, 1, maxz);
		SetPlane(&fixture.planes[first_plane + 5U], 0, 0, -1, -minz);
		fixture.brushes[brush].first_side = brush * 6U;
		fixture.brushes[brush].side_count = 6;
		fixture.brushes[brush].contents = SG_HOST_CONTENTS_SOLID;
		for (side = 0; side < 6; side++)
		{
			fixture.sides[brush * 6U + side].plane = first_plane + side;
			fixture.sides[brush * 6U + side].texinfo = 0;
		}
	}
	SetPlane(&fixture.planes[14], -1, 0, 0, 8);
	SetPlane(&fixture.planes[15], 1, 0, 0, 8);
	SetPlane(&fixture.planes[16], 0, -1, 0, 8);
	SetPlane(&fixture.planes[17], 0, 1, 0, 8);
	SetPlane(&fixture.planes[18], 0, 0, 1, 40);
	fixture.sides[11].texinfo = 1;
	fixture.texinfos[1].flags = SG_HOST_SURFACE_SKY;
	fixture.models[0].headnode = 0;
	Set3(fixture.models[0].mins.value, -64, -64, -64);
	Set3(fixture.models[0].maxs.value, 64, 64, 80);
	fixture.models[1] = fixture.models[0];
	fixture.models[1].headnode = -4;
	memcpy(fixture.entities, item_entity, sizeof(item_entity) - 1U);
	fixture.world.entities = fixture.entities;
	fixture.world.entity_byte_count = (uint32_t)(sizeof(item_entity) - 1U);
	fixture.world.planes = fixture.planes;
	fixture.world.plane_count = 19;
	fixture.world.nodes = fixture.nodes;
	fixture.world.node_count = 2;
	fixture.world.leaves = fixture.leaves;
	fixture.world.leaf_count = 4;
	fixture.world.leaf_brushes = fixture.leaf_brushes;
	fixture.world.leaf_brush_count = 4;
	fixture.world.models = fixture.models;
	fixture.world.model_count = 2;
	fixture.world.brushes = fixture.brushes;
	fixture.world.brush_count = 2;
	fixture.world.brush_sides = fixture.sides;
	fixture.world.brush_side_count = 12;
	fixture.world.texinfos = fixture.texinfos;
	fixture.world.texinfo_count = 2;
	fixture.identity.bsp_content_id = UINT64_C(0x1234);
	fixture.identity.entity_semantics_id = UINT64_C(0x5678);
	fixture.identity.physics_abi_id = UINT64_C(0x9abc);
	fixture.identity.source_set_identity = UINT64_C(0xdef0);
	Set3(fixture.identity.standing_hull.mins.value, -16, -16, -24);
	Set3(fixture.identity.standing_hull.maxs.value, 16, 16, 32);
	Set3(fixture.identity.crouching_hull.mins.value, -16, -16, -24);
	Set3(fixture.identity.crouching_hull.maxs.value, 16, 16, 4);
	fixture.identity.physics.max_velocity = 2000;
	fixture.identity.physics.frame_ms = 100;
	fixture.identity.physics.substep_ms = 10;
	fixture.configuration.identity = fixture.identity;
	fixture.configuration.cells = &fixture.cell;
	fixture.configuration.cell_count = 1;
	fixture.configuration.faces = fixture.faces;
	fixture.configuration.face_count = 6;
	fixture.configuration.vertices = fixture.vertices;
	fixture.configuration.vertex_count = 24;
	Set3(fixture.configuration.domain.mins.value,
		SG_CONFIGURATION_PMOVE_ORIGIN_MIN, SG_CONFIGURATION_PMOVE_ORIGIN_MIN,
		SG_CONFIGURATION_PMOVE_ORIGIN_MIN);
	Set3(fixture.configuration.domain.maxs.value,
		SG_CONFIGURATION_PMOVE_ORIGIN_MAX, SG_CONFIGURATION_PMOVE_ORIGIN_MAX,
		SG_CONFIGURATION_PMOVE_ORIGIN_MAX);
	fixture.cell.stance = SG_RUNE_STANCE_STANDING;
	fixture.cell.first_face = 0;
	fixture.cell.face_count = 6;
	Set3(fixture.cell.bounds.mins.value, -8, -8, 0);
	Set3(fixture.cell.bounds.maxs.value, 8, 8, 40);
	Set3(fixture.cell.interior_witness.value, 0, 0, 20);
	fixture.cell.bsp_leaf.index = 0;
	fixture.cell.bsp_area.index = 1;
	fixture.cell.bsp_cluster.index = 0;
	SetFace(&fixture, 0, 0, 0, -1, 0,
		SG_CONFIGURATION_PLANE_EXPANDED_BRUSH, 4, cube_faces[0]);
	fixture.faces[0].plane.reversed = 1;
	SetFace(&fixture, 1, 0, 0, 1, 40,
		SG_CONFIGURATION_PLANE_BSP, 18, cube_faces[1]);
	SetFace(&fixture, 2, -1, 0, 0, 8,
		SG_CONFIGURATION_PLANE_BSP, 14, cube_faces[2]);
	SetFace(&fixture, 3, 1, 0, 0, 8,
		SG_CONFIGURATION_PLANE_BSP, 15, cube_faces[3]);
	SetFace(&fixture, 4, 0, -1, 0, 8,
		SG_CONFIGURATION_PLANE_BSP, 16, cube_faces[4]);
	SetFace(&fixture, 5, 0, 1, 0, 8,
		SG_CONFIGURATION_PLANE_BSP, 17, cube_faces[5]);
	return fixture;
}

static void BindFixture(fixture_t *fixture)
{
	sg_host_collision_error_t error;

	fixture->world.entities = fixture->entities;
	fixture->world.planes = fixture->planes;
	fixture->world.nodes = fixture->nodes;
	fixture->world.leaves = fixture->leaves;
	fixture->world.leaf_brushes = fixture->leaf_brushes;
	fixture->world.models = fixture->models;
	fixture->world.brushes = fixture->brushes;
	fixture->world.brush_sides = fixture->sides;
	fixture->world.texinfos = fixture->texinfos;
	fixture->configuration.cells = &fixture->cell;
	fixture->configuration.faces = fixture->faces;
	fixture->configuration.vertices = fixture->vertices;
	CHECK(SG_HostCollisionInit(&fixture->authority, &fixture->world,
		&fixture->identity, &error));
}

static void SetCrouchingBottom(fixture_t *fixture, float nx, float ny,
	float nz, float expanded_distance)
{
	float hull_minimum = nx * fixture->identity.crouching_hull.mins.value[0] +
		ny * fixture->identity.crouching_hull.mins.value[1] +
		nz * fixture->identity.crouching_hull.mins.value[2];
	float minimum_z = INFINITY;
	uint32_t vertex;

	SetPlane(&fixture->planes[6], nx, ny, nz,
		expanded_distance + hull_minimum);
	Set3(fixture->faces[0].plane.normal, -nx, -ny, -nz);
	fixture->faces[0].plane.distance = -expanded_distance;
	for (vertex = 0; vertex < fixture->configuration.vertex_count; vertex++)
		if (fixture->vertices[vertex].value[2] == 0.0f)
		{
			float z = (expanded_distance -
				nx * fixture->vertices[vertex].value[0] -
				ny * fixture->vertices[vertex].value[1]) / nz;
			fixture->vertices[vertex].value[2] = z;
			if (z < minimum_z)
				minimum_z = z;
		}
	fixture->cell.bounds.mins.value[2] = minimum_z;
}

static int Build(fixture_t *fixture,
	sg_configuration_semantics_t **semantics_out)
{
	sg_configuration_semantics_limits_t limits;
	sg_configuration_semantics_error_t error;

	SG_ConfigurationSemanticsDefaultLimits(&limits);
	if (!SG_ConfigurationSemanticsBuild(&fixture->authority,
		&fixture->configuration, &limits, semantics_out, &error))
	{
		fprintf(stderr, "semantics build failed: %s source=%u\n",
			SG_ConfigurationSemanticsErrorString(error.code), error.source_index);
		return 0;
	}
	return 1;
}

static int StrictlyInsideRegion(const sg_configuration_semantics_t *semantics,
	const sg_configuration_semantic_region_t *region, const float point[3])
{
	uint32_t face;

	for (face = region->first_face; face < region->first_face +
		region->face_count; face++)
	{
		const sg_configuration_semantic_face_t *plane = &semantics->faces[face];
		float distance = point[0] * plane->normal[0] +
			point[1] * plane->normal[1] + point[2] * plane->normal[2] -
			plane->distance;
		if (distance >= -0.000001f)
			return 0;
	}
	return 1;
}

static uint32_t RegionAt(const sg_configuration_semantics_t *semantics,
	const float point[3])
{
	uint32_t region;

	for (region = 0; region < semantics->region_count; region++)
		if (StrictlyInsideRegion(semantics, &semantics->regions[region], point))
			return region;
	return UINT32_MAX;
}

static void TestThinBrushSupportFractionPartition(void)
{
	fixture_t fixture = Fixture();
	sg_configuration_semantics_t *semantics = NULL;
	sg_configuration_semantics_audit_result_t audit;
	sg_host_collision_pose_t pose_a, pose_b;
	float point_a[3] = { 0.0f, 0.0f, 0.125f };
	float point_b[3] = { -0.125f, 0.0f, 0.125f };
	uint32_t region_a, region_b, leaf;

	SetPlane(&fixture.planes[2], 1, 0, 0, 256);
	SetPlane(&fixture.planes[3], -1, 0, 0, 256);
	SetPlane(&fixture.planes[7], 0.6f, 0.0f, -0.8f, -35.2f);
	BindFixture(&fixture);
	CHECK(SG_HostCollisionClassifyPose(&fixture.authority, NULL, point_a,
		SG_RUNE_STANCE_STANDING, &pose_a));
	CHECK(SG_HostCollisionClassifyPose(&fixture.authority, NULL, point_b,
		SG_RUNE_STANCE_STANDING, &pose_b));
	CHECK(!pose_a.supported);
	CHECK(pose_b.supported);
	CHECK(Build(&fixture, &semantics));
	if (!semantics)
		return;
	region_a = RegionAt(semantics, point_a);
	region_b = RegionAt(semantics, point_b);
	CHECK(region_a < semantics->region_count);
	CHECK(region_b < semantics->region_count);
	CHECK(region_a != region_b);
	if (region_a < semantics->region_count && region_b < semantics->region_count)
	{
		CHECK((semantics->regions[region_a].flags &
			SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE) != 0U);
		CHECK((semantics->regions[region_b].flags &
			SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED) != 0U);
	}
	CHECK(SG_ConfigurationSemanticsAudit(&fixture.authority,
		&fixture.configuration, semantics, &audit));
	SG_ConfigurationSemanticsDestroy(semantics);
	semantics = NULL;
	for (leaf = 0; leaf < 3U; leaf++)
	{
		fixture.leaves[leaf].first_leaf_brush = leaf * 2U;
		fixture.leaves[leaf].leaf_brush_count = 2;
		fixture.leaf_brushes[leaf * 2U] = 0;
		fixture.leaf_brushes[leaf * 2U + 1U] = 1;
	}
	fixture.leaves[3].first_leaf_brush = 6;
	fixture.leaf_brushes[6] = 1;
	fixture.world.leaf_brush_count = 7;
	SetPlane(&fixture.planes[8], 1, 0, 0, 256);
	SetPlane(&fixture.planes[9], -1, 0, 0, 256);
	SetPlane(&fixture.planes[12], 0, 0, 1, -24);
	SetPlane(&fixture.planes[13], -0.6f, 0.0f, -0.8f, -35.2f);
	CHECK(Build(&fixture, &semantics));
	if (semantics)
		CHECK(SG_ConfigurationSemanticsAudit(&fixture.authority,
			&fixture.configuration, semantics, &audit));
	SG_ConfigurationSemanticsDestroy(semantics);
}

static void TestVolumetricWaterAndAudit(void)
{
	fixture_t fixture = Fixture();
	sg_configuration_semantics_t *semantics = NULL;
	sg_configuration_semantics_audit_result_t audit;
	uint32_t region;
	int levels[4] = { 0, 0, 0, 0 };
	int saw_sample_face = 0;
	int saw_supported = 0, saw_airborne = 0;

	BindFixture(&fixture);
	CHECK(Build(&fixture, &semantics));
	if (!semantics)
		return;
	CHECK(semantics->region_count >= 4);
	for (region = 0; region < semantics->region_count; region++)
	{
		uint8_t level = semantics->regions[region].water_level;
		uint32_t face;
		int32_t x_q8, y_q8, z_q8;
		CHECK(level <= 3);
		levels[level]++;
		CHECK(((semantics->regions[region].flags &
			SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED) != 0U) !=
			((semantics->regions[region].flags &
			SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE) != 0U));
		if (semantics->regions[region].flags &
			SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED)
			saw_supported = 1;
		if (semantics->regions[region].flags &
			SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE)
			saw_airborne = 1;
		for (face = semantics->regions[region].first_face;
			face < semantics->regions[region].first_face +
				semantics->regions[region].face_count; face++)
			if (semantics->faces[face].source_kind ==
				SG_CONFIGURATION_SEMANTIC_PLANE_CONTENTS_SAMPLE)
				saw_sample_face = 1;
		for (x_q8 = -63; x_q8 <= 63; x_q8 += 63)
			for (y_q8 = -63; y_q8 <= 63; y_q8 += 63)
				for (z_q8 = 1; z_q8 < 320; z_q8++)
				{
					float point[3] = { (float)x_q8 * 0.125f,
						(float)y_q8 * 0.125f, (float)z_q8 * 0.125f };
					sg_host_collision_pose_t pose;
					if (!StrictlyInsideRegion(semantics,
						&semantics->regions[region], point))
						continue;
					CHECK(SG_HostCollisionClassifyPose(&fixture.authority, NULL,
						point, fixture.cell.stance, &pose));
					CHECK(pose.valid);
					CHECK((pose.supported != 0) ==
						((semantics->regions[region].flags &
						SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED) != 0U));
					CHECK(pose.water_level == semantics->regions[region].water_level);
					CHECK(pose.water_type == semantics->regions[region].water_type);
				}
	}
	CHECK(levels[0] == 1);
	CHECK(levels[1] == 1);
	CHECK(levels[2] >= 1);
	CHECK(levels[3] == 0);
	CHECK(saw_sample_face);
	CHECK(saw_supported);
	CHECK(saw_airborne);
	CHECK(SG_ConfigurationSemanticsAudit(&fixture.authority,
		&fixture.configuration, semantics, &audit));
	CHECK(audit.code == SG_CONFIGURATION_SEMANTICS_AUDIT_OK);
	printf("semantic matrix: regions=%u boundaries=%u build_solves=%llu "
		"build_constraints=%llu audit_solves=%llu audit_constraints=%llu\n",
		semantics->region_count, semantics->boundary_count,
		(unsigned long long)semantics->lattice_solve_calls,
		(unsigned long long)semantics->lattice_constraints,
		(unsigned long long)audit.lattice_solve_calls,
		(unsigned long long)audit.lattice_constraints);
	SG_ConfigurationSemanticsDestroy(semantics);
}

static void TestSurfaceAuthorityAndNoPickupObstruction(void)
{
	fixture_t fixture = Fixture();
	sg_configuration_semantics_t *sky = NULL, *ordinary = NULL, *changed = NULL;
	uint32_t boundary;
	int saw_support = 0, saw_sky = 0, saw_sky_hook = 0;
	int saw_ordinary_hook = 0, saw_moving = 0;

	BindFixture(&fixture);
	CHECK(Build(&fixture, &sky));
	if (!sky)
		return;
	for (boundary = 0; boundary < sky->boundary_count; boundary++)
	{
		const sg_configuration_boundary_t *record = &sky->boundaries[boundary];
		if (record->flags & SG_CONFIGURATION_BOUNDARY_SUPPORT_CANDIDATE)
			saw_support = 1;
	}
	for (boundary = 0; boundary < sky->hook_surface_count; boundary++)
	{
		const sg_configuration_hook_surface_t *record =
			&sky->hook_surfaces[boundary];
		if (record->flags & SG_CONFIGURATION_HOOK_SURFACE_MOVING_MODEL)
			saw_moving = 1;
		if (record->flags & SG_CONFIGURATION_HOOK_SURFACE_SKY)
		{
			saw_sky = 1;
			if (record->flags & SG_CONFIGURATION_HOOK_SURFACE_HOOKABLE)
				saw_sky_hook = 1;
		}
	}
	CHECK(saw_support);
	CHECK(saw_sky);
	CHECK(!saw_sky_hook);
	CHECK(saw_moving);
	CHECK(sky->hook_surface_count > sky->boundary_count);
	fixture.texinfos[1].flags = 0;
	CHECK(Build(&fixture, &ordinary));
	if (ordinary)
		for (boundary = 0; boundary < ordinary->hook_surface_count; boundary++)
			if (ordinary->hook_surfaces[boundary].brush_side == 11 &&
				(ordinary->hook_surfaces[boundary].flags &
				 SG_CONFIGURATION_HOOK_SURFACE_HOOKABLE))
				saw_ordinary_hook = 1;
	CHECK(saw_ordinary_hook);
	memset(fixture.entities, 'X', fixture.world.entity_byte_count);
	CHECK(Build(&fixture, &changed));
	if (ordinary && changed)
	{
		CHECK(ordinary->region_count == changed->region_count);
		CHECK(ordinary->boundary_count == changed->boundary_count);
		CHECK(ordinary->hook_surface_count == changed->hook_surface_count);
		CHECK(ordinary->hook_vertex_count == changed->hook_vertex_count);
	}
	SG_ConfigurationSemanticsDestroy(changed);
	SG_ConfigurationSemanticsDestroy(ordinary);
	SG_ConfigurationSemanticsDestroy(sky);
}

static void TestActualDomainContactMarksVoidAdjacency(void)
{
	fixture_t fixture = Fixture();
	sg_configuration_semantics_t *semantics = NULL;
	uint32_t region, vertex;
	int saw_void = 0;

	BindFixture(&fixture);
	fixture.faces[2].plane.source_kind = SG_CONFIGURATION_PLANE_DOMAIN;
	fixture.faces[2].plane.source_index = 0;
	fixture.faces[2].plane.source_variant = 1;
	fixture.faces[2].plane.reversed = 1;
	fixture.faces[2].plane.distance = -SG_CONFIGURATION_PMOVE_ORIGIN_MIN;
	fixture.faces[3].plane.distance = -4080.0f;
	SetPlane(&fixture.planes[15], 1, 0, 0, -4080.0f);
	fixture.cell.bounds.mins.value[0] = SG_CONFIGURATION_PMOVE_ORIGIN_MIN;
	fixture.cell.bounds.maxs.value[0] = -4080.0f;
	fixture.cell.interior_witness.value[0] = -4088.0f;
	for (vertex = 0; vertex < fixture.configuration.vertex_count; vertex++)
		fixture.vertices[vertex].value[0] =
			fixture.vertices[vertex].value[0] < 0.0f ?
			SG_CONFIGURATION_PMOVE_ORIGIN_MIN : -4080.0f;
	CHECK(Build(&fixture, &semantics));
	if (!semantics)
		return;
	for (region = 0; region < semantics->region_count; region++)
		if (semantics->regions[region].flags &
			SG_CONFIGURATION_SEMANTIC_REGION_VOID_ADJACENT)
			saw_void = 1;
	CHECK(saw_void);
	SG_ConfigurationSemanticsDestroy(semantics);
}

static void TestCrouchingHazardRampAndLedgeAnnotations(void)
{
	fixture_t fixture = Fixture();
	sg_configuration_semantics_t *semantics = NULL;
	uint32_t region, boundary;
	int saw_hazard = 0, saw_ramp = 0, saw_vertical = 0;

	BindFixture(&fixture);
	fixture.cell.stance = SG_RUNE_STANCE_CROUCHING;
	fixture.faces[0].plane.source_variant = SG_RUNE_STANCE_CROUCHING;
	fixture.leaves[1].contents = SG_HOST_CONTENTS_LAVA;
	CHECK(Build(&fixture, &semantics));
	if (!semantics)
		return;
	CHECK(semantics->region_count >= 4);
	for (region = 0; region < semantics->region_count; region++)
		if (semantics->regions[region].flags &
			SG_CONFIGURATION_SEMANTIC_REGION_HAZARD)
			saw_hazard = 1;
	CHECK(saw_hazard);
	SG_ConfigurationSemanticsDestroy(semantics);
	semantics = NULL;
	SetCrouchingBottom(&fixture, 0.0f, 0.6f, 0.8f, 9.6f);
	CHECK(Build(&fixture, &semantics));
	if (!semantics)
		return;
	for (boundary = 0; boundary < semantics->boundary_count; boundary++)
	{
		const sg_configuration_boundary_t *record = &semantics->boundaries[boundary];
		if ((record->flags & SG_CONFIGURATION_BOUNDARY_SUPPORT_CANDIDATE) &&
			record->origin_normal[2] < -0.7f &&
			record->origin_normal[2] > -1.0f)
			saw_ramp = 1;
	}
	for (boundary = 0; boundary < semantics->hook_surface_count; boundary++)
		if (semantics->hook_surfaces[boundary].normal[2] == 0.0f)
			saw_vertical = 1;
	CHECK(saw_ramp);
	CHECK(saw_vertical);
	CHECK(SG_ConfigurationSemanticsAudit(&fixture.authority,
		&fixture.configuration, semantics,
		&(sg_configuration_semantics_audit_result_t){ 0 }));
	SG_ConfigurationSemanticsDestroy(semantics);
}

static void TestNonAxialHookSurfaceSelfAudit(void)
{
	fixture_t fixture = Fixture();
	sg_configuration_semantics_t *semantics = NULL;
	sg_configuration_semantics_audit_result_t audit;

	BindFixture(&fixture);
	fixture.cell.stance = SG_RUNE_STANCE_CROUCHING;
	fixture.faces[0].plane.source_variant = SG_RUNE_STANCE_CROUCHING;
	SetCrouchingBottom(&fixture, 0.00130148f, 0.317301f, 0.948324f, 9.6f);
	CHECK(Build(&fixture, &semantics));
	if (semantics)
		CHECK(SG_ConfigurationSemanticsAudit(&fixture.authority,
			&fixture.configuration, semantics, &audit));
	SG_ConfigurationSemanticsDestroy(semantics);
}

static void TestTransactionalOverflowAndMutationAudit(void)
{
	fixture_t fixture = Fixture();
	sg_configuration_semantics_t *semantics = NULL, *failed = NULL;
	sg_configuration_semantics_limits_t limits;
	sg_configuration_semantics_error_t error;
	sg_configuration_semantics_audit_result_t audit;
	uint8_t old_level;
	uint32_t saved_count;
	float saved_distance;
	float saved_bound;
	uint64_t saved_id;
	uint32_t face, saved_u32;
	sg_rune_vec3_t saved_vertex;
	sg_configuration_semantic_region_t *grown_regions;
	sg_configuration_semantic_face_t *grown_faces;
	sg_rune_vec3_t *grown_vertices;
	sg_configuration_boundary_t *grown_boundaries;
	uint32_t hook;

	BindFixture(&fixture);
	SG_ConfigurationSemanticsDefaultLimits(&limits);
	CHECK(limits.max_regions == UINT32_MAX);
	CHECK(limits.max_faces == UINT32_MAX);
	CHECK(limits.max_vertices == UINT32_MAX);
	CHECK(limits.max_boundaries == UINT32_MAX);
	CHECK(limits.max_hook_surfaces == UINT32_MAX);
	CHECK(limits.max_hook_vertices == UINT32_MAX);
	limits.max_regions = 1;
	CHECK(!SG_ConfigurationSemanticsBuild(&fixture.authority,
		&fixture.configuration, &limits, &failed, &error));
	CHECK(failed == NULL);
	CHECK(error.code == SG_CONFIGURATION_SEMANTICS_ERROR_OVERFLOW);
	SG_ConfigurationSemanticsDefaultLimits(&limits);
	limits.max_faces = 1;
	CHECK(!SG_ConfigurationSemanticsBuild(&fixture.authority,
		&fixture.configuration, &limits, &failed, &error));
	CHECK(failed == NULL);
	CHECK(error.code == SG_CONFIGURATION_SEMANTICS_ERROR_OVERFLOW);
	SG_ConfigurationSemanticsDefaultLimits(&limits);
	limits.max_vertices = 1;
	CHECK(!SG_ConfigurationSemanticsBuild(&fixture.authority,
		&fixture.configuration, &limits, &failed, &error));
	CHECK(failed == NULL);
	CHECK(error.code == SG_CONFIGURATION_SEMANTICS_ERROR_OVERFLOW);
	SG_ConfigurationSemanticsDefaultLimits(&limits);
	limits.max_hook_surfaces = 1;
	CHECK(!SG_ConfigurationSemanticsBuild(&fixture.authority,
		&fixture.configuration, &limits, &failed, &error));
	CHECK(failed == NULL);
	CHECK(error.code == SG_CONFIGURATION_SEMANTICS_ERROR_OVERFLOW);
	SG_ConfigurationSemanticsDefaultLimits(&limits);
	limits.max_hook_vertices = 1;
	CHECK(!SG_ConfigurationSemanticsBuild(&fixture.authority,
		&fixture.configuration, &limits, &failed, &error));
	CHECK(failed == NULL);
	CHECK(error.code == SG_CONFIGURATION_SEMANTICS_ERROR_OVERFLOW);
	CHECK(Build(&fixture, &semantics));
	if (!semantics)
		return;
	old_level = semantics->regions[0].water_level;
	semantics->regions[0].water_level = (uint8_t)(old_level == 0 ? 1 : 0);
	CHECK(!SG_ConfigurationSemanticsAudit(&fixture.authority,
		&fixture.configuration, semantics, &audit));
	CHECK(audit.code == SG_CONFIGURATION_SEMANTICS_AUDIT_REGION_DISAGREEMENT);
	semantics->regions[0].water_level = old_level;
	saved_distance = semantics->faces[semantics->regions[0].first_face].distance;
	semantics->faces[semantics->regions[0].first_face].distance += 1.0f;
	CHECK(!SG_ConfigurationSemanticsAudit(&fixture.authority,
		&fixture.configuration, semantics, &audit));
	CHECK(audit.code == SG_CONFIGURATION_SEMANTICS_AUDIT_REGION_DISAGREEMENT);
	semantics->faces[semantics->regions[0].first_face].distance = saved_distance;
	face = semantics->regions[0].first_face;
	while (face < semantics->regions[0].first_face +
		semantics->regions[0].face_count &&
		semantics->faces[face].vertex_count != 4U)
		face++;
	CHECK(face < semantics->regions[0].first_face +
		semantics->regions[0].face_count);
	if (face < semantics->regions[0].first_face +
		semantics->regions[0].face_count)
	{
		uint32_t axis, vertex;
		saved_u32 = semantics->faces[face].vertex_count;
		semantics->faces[face].vertex_count = 3U;
		CHECK(!SG_ConfigurationSemanticsAudit(&fixture.authority,
			&fixture.configuration, semantics, &audit));
		CHECK(audit.code == SG_CONFIGURATION_SEMANTICS_AUDIT_REGION_DISAGREEMENT);
		semantics->faces[face].vertex_count = saved_u32;
		saved_vertex = semantics->vertices[semantics->faces[face].first_vertex];
		for (axis = 0; axis < 3U; axis++)
		{
			float center = 0.0f;
			for (vertex = 0; vertex < semantics->faces[face].vertex_count; vertex++)
				center += semantics->vertices[
					semantics->faces[face].first_vertex + vertex].value[axis];
			semantics->vertices[semantics->faces[face].first_vertex].value[axis] =
				center / (float)semantics->faces[face].vertex_count;
		}
		CHECK(!SG_ConfigurationSemanticsAudit(&fixture.authority,
			&fixture.configuration, semantics, &audit));
		CHECK(audit.code == SG_CONFIGURATION_SEMANTICS_AUDIT_REGION_DISAGREEMENT);
		semantics->vertices[semantics->faces[face].first_vertex] = saved_vertex;
		saved_u32 = semantics->faces[face].source_index;
		semantics->faces[face].source_index++;
		CHECK(!SG_ConfigurationSemanticsAudit(&fixture.authority,
			&fixture.configuration, semantics, &audit));
		CHECK(audit.code == SG_CONFIGURATION_SEMANTICS_AUDIT_REGION_DISAGREEMENT);
		semantics->faces[face].source_index = saved_u32;
	}
	saved_id = semantics->regions[0].id;
	semantics->regions[0].id++;
	CHECK(!SG_ConfigurationSemanticsAudit(&fixture.authority,
		&fixture.configuration, semantics, &audit));
	CHECK(audit.code == SG_CONFIGURATION_SEMANTICS_AUDIT_REGION_DISAGREEMENT);
	semantics->regions[0].id = saved_id;
	saved_bound = semantics->regions[0].bounds.maxs.value[0];
	semantics->regions[0].bounds.maxs.value[0] -= 0.5f;
	CHECK(!SG_ConfigurationSemanticsAudit(&fixture.authority,
		&fixture.configuration, semantics, &audit));
	CHECK(audit.code == SG_CONFIGURATION_SEMANTICS_AUDIT_REGION_DISAGREEMENT);
	semantics->regions[0].bounds.maxs.value[0] = saved_bound;
	semantics->regions[0].bounds.maxs.value[0] = NAN;
	CHECK(!SG_ConfigurationSemanticsAudit(&fixture.authority,
		&fixture.configuration, semantics, &audit));
	CHECK(audit.code == SG_CONFIGURATION_SEMANTICS_AUDIT_REGION_DISAGREEMENT);
	semantics->regions[0].bounds.maxs.value[0] = saved_bound;
	saved_count = semantics->face_count;
	grown_faces = realloc(semantics->faces,
		(size_t)(saved_count + 1U) * sizeof(*grown_faces));
	CHECK(grown_faces != NULL);
	if (grown_faces)
	{
		semantics->faces = grown_faces;
		memset(&semantics->faces[saved_count], 0,
			sizeof(semantics->faces[saved_count]));
		semantics->face_count++;
		CHECK(!SG_ConfigurationSemanticsAudit(&fixture.authority,
			&fixture.configuration, semantics, &audit));
		CHECK(audit.code == SG_CONFIGURATION_SEMANTICS_AUDIT_REGION_DISAGREEMENT);
		semantics->face_count--;
	}
	saved_count = semantics->vertex_count;
	grown_vertices = realloc(semantics->vertices,
		(size_t)(saved_count + 1U) * sizeof(*grown_vertices));
	CHECK(grown_vertices != NULL);
	if (grown_vertices)
	{
		semantics->vertices = grown_vertices;
		memset(&semantics->vertices[saved_count], 0,
			sizeof(semantics->vertices[saved_count]));
		semantics->vertex_count++;
		CHECK(!SG_ConfigurationSemanticsAudit(&fixture.authority,
			&fixture.configuration, semantics, &audit));
		CHECK(audit.code == SG_CONFIGURATION_SEMANTICS_AUDIT_REGION_DISAGREEMENT);
		semantics->vertex_count--;
	}
	semantics->lattice_solve_calls++;
	CHECK(!SG_ConfigurationSemanticsAudit(&fixture.authority,
		&fixture.configuration, semantics, &audit));
	CHECK(audit.code == SG_CONFIGURATION_SEMANTICS_AUDIT_REGION_DISAGREEMENT);
	semantics->lattice_solve_calls--;
	saved_count = semantics->region_count;
	semantics->region_count--;
	CHECK(!SG_ConfigurationSemanticsAudit(&fixture.authority,
		&fixture.configuration, semantics, &audit));
	CHECK(audit.code == SG_CONFIGURATION_SEMANTICS_AUDIT_OMITTED_REGION);
	semantics->region_count = saved_count;
	grown_regions = realloc(semantics->regions,
		(size_t)(saved_count + 1U) * sizeof(*grown_regions));
	CHECK(grown_regions != NULL);
	if (grown_regions)
	{
		semantics->regions = grown_regions;
		semantics->regions[saved_count] = semantics->regions[0];
		semantics->region_count++;
		CHECK(!SG_ConfigurationSemanticsAudit(&fixture.authority,
			&fixture.configuration, semantics, &audit));
		CHECK(audit.code == SG_CONFIGURATION_SEMANTICS_AUDIT_INVENTED_REGION);
		semantics->region_count--;
	}
	saved_count = semantics->boundary_count;
	semantics->boundary_count--;
	CHECK(!SG_ConfigurationSemanticsAudit(&fixture.authority,
		&fixture.configuration, semantics, &audit));
	CHECK(audit.code == SG_CONFIGURATION_SEMANTICS_AUDIT_OMITTED_BOUNDARY);
	semantics->boundary_count = saved_count;
	grown_boundaries = realloc(semantics->boundaries,
		(size_t)(saved_count + 1U) * sizeof(*grown_boundaries));
	CHECK(grown_boundaries != NULL);
	if (grown_boundaries)
	{
		semantics->boundaries = grown_boundaries;
		semantics->boundaries[saved_count] = semantics->boundaries[0];
		semantics->boundary_count++;
		CHECK(!SG_ConfigurationSemanticsAudit(&fixture.authority,
			&fixture.configuration, semantics, &audit));
		CHECK(audit.code == SG_CONFIGURATION_SEMANTICS_AUDIT_INVENTED_BOUNDARY);
		semantics->boundary_count--;
	}
	saved_id = semantics->boundaries[0].id;
	semantics->boundaries[0].id++;
	CHECK(!SG_ConfigurationSemanticsAudit(&fixture.authority,
		&fixture.configuration, semantics, &audit));
	CHECK(audit.code == SG_CONFIGURATION_SEMANTICS_AUDIT_BOUNDARY_DISAGREEMENT);
	semantics->boundaries[0].id = saved_id;
	saved_u32 = semantics->boundaries[0].brush_side;
	semantics->boundaries[0].brush_side++;
	CHECK(!SG_ConfigurationSemanticsAudit(&fixture.authority,
		&fixture.configuration, semantics, &audit));
	CHECK(audit.code == SG_CONFIGURATION_SEMANTICS_AUDIT_BOUNDARY_DISAGREEMENT);
	semantics->boundaries[0].brush_side = saved_u32;
	for (hook = 0; hook < semantics->hook_surface_count; hook++)
		if (semantics->hook_surfaces[hook].vertex_count == 4U)
			break;
	CHECK(hook < semantics->hook_surface_count);
	if (hook < semantics->hook_surface_count)
	{
		semantics->hook_surfaces[hook].vertex_count = 3;
		CHECK(!SG_ConfigurationSemanticsAudit(&fixture.authority,
			&fixture.configuration, semantics, &audit));
		CHECK(audit.code ==
			SG_CONFIGURATION_SEMANTICS_AUDIT_HOOK_SURFACE_DISAGREEMENT);
		semantics->hook_surfaces[hook].vertex_count = 4;
		saved_id = semantics->hook_surfaces[hook].id;
		semantics->hook_surfaces[hook].id++;
		CHECK(!SG_ConfigurationSemanticsAudit(&fixture.authority,
			&fixture.configuration, semantics, &audit));
		CHECK(audit.code ==
			SG_CONFIGURATION_SEMANTICS_AUDIT_HOOK_SURFACE_DISAGREEMENT);
		semantics->hook_surfaces[hook].id = saved_id;
		saved_bound = semantics->hook_surfaces[hook].bounds.maxs.value[0];
		semantics->hook_surfaces[hook].bounds.maxs.value[0] += 0.5f;
		CHECK(!SG_ConfigurationSemanticsAudit(&fixture.authority,
			&fixture.configuration, semantics, &audit));
		CHECK(audit.code ==
			SG_CONFIGURATION_SEMANTICS_AUDIT_HOOK_SURFACE_DISAGREEMENT);
		semantics->hook_surfaces[hook].bounds.maxs.value[0] = saved_bound;
		semantics->hook_surfaces[hook].bounds.maxs.value[0] = NAN;
		CHECK(!SG_ConfigurationSemanticsAudit(&fixture.authority,
			&fixture.configuration, semantics, &audit));
		CHECK(audit.code ==
			SG_CONFIGURATION_SEMANTICS_AUDIT_HOOK_SURFACE_DISAGREEMENT);
		semantics->hook_surfaces[hook].bounds.maxs.value[0] = saved_bound;
		saved_vertex = semantics->hook_vertices[
			semantics->hook_surfaces[hook].first_vertex];
		semantics->hook_vertices[
			semantics->hook_surfaces[hook].first_vertex].value[0] += 0.5f;
		CHECK(!SG_ConfigurationSemanticsAudit(&fixture.authority,
			&fixture.configuration, semantics, &audit));
		CHECK(audit.code ==
			SG_CONFIGURATION_SEMANTICS_AUDIT_HOOK_SURFACE_DISAGREEMENT);
		semantics->hook_vertices[
			semantics->hook_surfaces[hook].first_vertex] = saved_vertex;
	}
	SG_ConfigurationSemanticsDestroy(semantics);
}

static void TestMalformedSourceFailsClosed(void)
{
	fixture_t fixture = Fixture();
	sg_configuration_semantics_t *output = NULL, *semantics = NULL;
	sg_configuration_semantics_limits_t limits;
	sg_configuration_semantics_error_t error;
	sg_configuration_semantics_audit_result_t audit;
	float saved;
	uint32_t saved_u32;

	BindFixture(&fixture);
	SG_ConfigurationSemanticsDefaultLimits(&limits);
	saved = fixture.faces[0].plane.distance;
	fixture.faces[0].plane.distance = NAN;
	CHECK(!SG_ConfigurationSemanticsBuild(&fixture.authority,
		&fixture.configuration, &limits, &output, &error));
	CHECK(output == NULL);
	CHECK(error.code == SG_CONFIGURATION_SEMANTICS_ERROR_INVALID_SOURCE);
	fixture.faces[0].plane.distance = saved;
	fixture.faces[0].first_vertex = fixture.configuration.vertex_count;
	CHECK(!SG_ConfigurationSemanticsBuild(&fixture.authority,
		&fixture.configuration, &limits, &output, &error));
	CHECK(output == NULL);
	CHECK(error.code == SG_CONFIGURATION_SEMANTICS_ERROR_INVALID_SOURCE);
	fixture.faces[0].first_vertex = 0;
	fixture.cell.bsp_area.index++;
	CHECK(!SG_ConfigurationSemanticsBuild(&fixture.authority,
		&fixture.configuration, &limits, &output, &error));
	CHECK(output == NULL);
	CHECK(error.code == SG_CONFIGURATION_SEMANTICS_ERROR_INVALID_SOURCE);
	fixture.cell.bsp_area.index--;
	fixture.cell.bsp_cluster.index++;
	CHECK(!SG_ConfigurationSemanticsBuild(&fixture.authority,
		&fixture.configuration, &limits, &output, &error));
	CHECK(output == NULL);
	CHECK(error.code == SG_CONFIGURATION_SEMANTICS_ERROR_INVALID_SOURCE);
	fixture.cell.bsp_cluster.index--;
	fixture.cell.contents = SG_RUNE_CONTENTS_WATER;
	CHECK(!SG_ConfigurationSemanticsBuild(&fixture.authority,
		&fixture.configuration, &limits, &output, &error));
	CHECK(output == NULL);
	CHECK(error.code == SG_CONFIGURATION_SEMANTICS_ERROR_INVALID_SOURCE);
	fixture.cell.contents = 0;
	saved_u32 = fixture.cell.bsp_leaf.index;
	fixture.cell.bsp_leaf.index = fixture.world.leaf_count;
	CHECK(!SG_ConfigurationSemanticsBuild(&fixture.authority,
		&fixture.configuration, &limits, &output, &error));
	CHECK(output == NULL);
	CHECK(error.code == SG_CONFIGURATION_SEMANTICS_ERROR_INVALID_SOURCE);
	fixture.cell.bsp_leaf.index = saved_u32;
	fixture.cell.bounds.maxs.value[0] += 1.0f;
	CHECK(!SG_ConfigurationSemanticsBuild(&fixture.authority,
		&fixture.configuration, &limits, &output, &error));
	CHECK(output == NULL);
	CHECK(error.code == SG_CONFIGURATION_SEMANTICS_ERROR_INVALID_SOURCE);
	fixture.cell.bounds.maxs.value[0] -= 1.0f;
	CHECK(Build(&fixture, &semantics));
	if (semantics)
	{
		fixture.cell.bsp_cluster.index++;
		CHECK(!SG_ConfigurationSemanticsAudit(&fixture.authority,
			&fixture.configuration, semantics, &audit));
		CHECK(audit.code == SG_CONFIGURATION_SEMANTICS_AUDIT_SOURCE_MISMATCH);
		fixture.cell.bsp_cluster.index--;
		fixture.cell.contents = SG_RUNE_CONTENTS_WATER;
		CHECK(!SG_ConfigurationSemanticsAudit(&fixture.authority,
			&fixture.configuration, semantics, &audit));
		CHECK(audit.code == SG_CONFIGURATION_SEMANTICS_AUDIT_SOURCE_MISMATCH);
		fixture.cell.contents = 0;
		fixture.faces[0].vertex_count = 3;
		CHECK(!SG_ConfigurationSemanticsAudit(&fixture.authority,
			&fixture.configuration, semantics, &audit));
		CHECK(audit.code == SG_CONFIGURATION_SEMANTICS_AUDIT_SOURCE_MISMATCH);
		fixture.faces[0].vertex_count = 4;
		fixture.faces[1].plane.source_variant = 1;
		CHECK(!SG_ConfigurationSemanticsAudit(&fixture.authority,
			&fixture.configuration, semantics, &audit));
		CHECK(audit.code == SG_CONFIGURATION_SEMANTICS_AUDIT_SOURCE_MISMATCH);
		fixture.faces[1].plane.source_variant = 0;
		fixture.faces[0].plane.reversed = 3;
		CHECK(!SG_ConfigurationSemanticsAudit(&fixture.authority,
			&fixture.configuration, semantics, &audit));
		CHECK(audit.code == SG_CONFIGURATION_SEMANTICS_AUDIT_SOURCE_MISMATCH);
		fixture.faces[0].plane.reversed = 1;
	}
	SG_ConfigurationSemanticsDestroy(semantics);
	fixture = Fixture();
	BindFixture(&fixture);
	fixture.leaves[3].contents = fixture.leaves[0].contents;
	fixture.leaves[3].cluster = fixture.leaves[0].cluster;
	fixture.leaves[3].area = fixture.leaves[0].area;
	fixture.cell.bsp_leaf.index = 3;
	CHECK(!SG_ConfigurationSemanticsBuild(&fixture.authority,
		&fixture.configuration, &limits, &output, &error));
	CHECK(output == NULL);
	CHECK(error.code == SG_CONFIGURATION_SEMANTICS_ERROR_INVALID_SOURCE);
	fixture = Fixture();
	BindFixture(&fixture);
	fixture.cell.bsp_leaf.index = 1;
	fixture.cell.bsp_cluster.index = 1;
	fixture.cell.contents = SG_RUNE_CONTENTS_WATER;
	Set3(fixture.cell.interior_witness.value, 0, 0, -1);
	CHECK(!SG_ConfigurationSemanticsBuild(&fixture.authority,
		&fixture.configuration, &limits, &output, &error));
	CHECK(output == NULL);
	CHECK(error.code == SG_CONFIGURATION_SEMANTICS_ERROR_INVALID_SOURCE);
	fixture = Fixture();
	BindFixture(&fixture);
	fixture.faces[1].plane.source_variant = 1;
	CHECK(!SG_ConfigurationSemanticsBuild(&fixture.authority,
		&fixture.configuration, &limits, &output, &error));
	CHECK(output == NULL);
	CHECK(error.code == SG_CONFIGURATION_SEMANTICS_ERROR_INVALID_SOURCE);
	fixture = Fixture();
	BindFixture(&fixture);
	fixture.faces[0].plane.reversed = 3;
	CHECK(!SG_ConfigurationSemanticsBuild(&fixture.authority,
		&fixture.configuration, &limits, &output, &error));
	CHECK(output == NULL);
	CHECK(error.code == SG_CONFIGURATION_SEMANTICS_ERROR_INVALID_SOURCE);
	fixture = Fixture();
	BindFixture(&fixture);
	fixture.faces[0].vertex_count = 3;
	CHECK(!SG_ConfigurationSemanticsBuild(&fixture.authority,
		&fixture.configuration, &limits, &output, &error));
	CHECK(output == NULL);
	CHECK(error.code == SG_CONFIGURATION_SEMANTICS_ERROR_INVALID_SOURCE);
	fixture = Fixture();
	BindFixture(&fixture);
	{
		sg_rune_vec3_t vertex = fixture.vertices[0];
		fixture.vertices[0] = fixture.vertices[4];
		fixture.vertices[4] = vertex;
	}
	CHECK(!SG_ConfigurationSemanticsBuild(&fixture.authority,
		&fixture.configuration, &limits, &output, &error));
	CHECK(output == NULL);
	CHECK(error.code == SG_CONFIGURATION_SEMANTICS_ERROR_INVALID_SOURCE);
}

static void TestConfigurationBuilderComposition(void)
{
	fixture_t fixture = Fixture();
	sg_configuration_space_t *configuration = NULL;
	sg_configuration_semantics_t *semantics = NULL;
	sg_configuration_error_t configuration_error;
	sg_configuration_audit_result_t configuration_audit;
	sg_configuration_semantics_error_t semantics_error;
	sg_configuration_semantics_audit_result_t semantics_audit;
	sg_configuration_semantics_limits_t limits;
	uint32_t leaf;

	fixture.models[0].headnode = 1;
	fixture.world.model_count = 1;
	fixture.world.brush_count = 0;
	fixture.world.brush_side_count = 0;
	fixture.world.leaf_brush_count = 0;
	for (leaf = 0; leaf < fixture.world.leaf_count; leaf++)
	{
		fixture.leaves[leaf].first_leaf_brush = 0;
		fixture.leaves[leaf].leaf_brush_count = 0;
	}
	BindFixture(&fixture);
	CHECK(SG_ConfigurationBuild(&fixture.authority, NULL, &configuration,
		&configuration_error));
	if (!configuration)
		return;
	if (!SG_ConfigurationAudit(&fixture.authority, configuration,
		&configuration_audit))
		fprintf(stderr, "configuration composition audit failed: %s record=%u\n",
			SG_ConfigurationAuditCodeString(configuration_audit.code),
			configuration_audit.record);
	CHECK(configuration_audit.code == SG_CONFIGURATION_AUDIT_OK);
	SG_ConfigurationSemanticsDefaultLimits(&limits);
	CHECK(SG_ConfigurationSemanticsBuild(&fixture.authority, configuration,
		&limits, &semantics, &semantics_error));
	if (semantics)
	{
		CHECK(SG_ConfigurationSemanticsAudit(&fixture.authority, configuration,
			semantics, &semantics_audit));
		CHECK(semantics->region_count >= configuration->cell_count);
	}
	SG_ConfigurationSemanticsDestroy(semantics);
	SG_ConfigurationDestroy(configuration);
}

int main(void)
{
	TestVolumetricWaterAndAudit();
	TestThinBrushSupportFractionPartition();
	TestSurfaceAuthorityAndNoPickupObstruction();
	TestActualDomainContactMarksVoidAdjacency();
	TestCrouchingHazardRampAndLedgeAnnotations();
	TestNonAxialHookSurfaceSelfAudit();
	TestTransactionalOverflowAndMutationAudit();
	TestMalformedSourceFailsClosed();
	TestConfigurationBuilderComposition();
	if (failures)
	{
		fprintf(stderr, "%d configuration semantics test(s) failed\n", failures);
		return 1;
	}
	puts("configuration semantics tests passed");
	return 0;
}
