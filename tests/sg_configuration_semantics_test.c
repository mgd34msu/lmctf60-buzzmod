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
	sg_bsp_node_t nodes[3];
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

static void TestAllFacetHistoryBoundsSemantics(void)
{
	static const float facet_points[6][4][3] = {
		{{-1,-1,-1},{-1,-1,1},{-1,1,1},{-1,1,-1}},
		{{-1,1,-1},{-1,1,1},{-0.1f,1,1},{-0.1f,1,-1}},
		{{-1,-1,-1},{0.1f,-1,-1},{0.1f,-1,1},{-1,-1,1}},
		{{-1,-1,1},{0.1f,-1,1},{-0.1f,1,1},{-1,1,1}},
		{{-1,-1,-1},{-1,1,-1},{-0.1f,1,-1},{0.1f,-1,-1}},
		{{0.1f,-1,-1},{-0.1f,1,-1},{-0.1f,1,1},{0.1f,-1,1}}
	};
	fixture_t fixture = Fixture();
	sg_configuration_semantics_t *semantics = NULL;
	uint32_t face;

	SetPlane(&fixture.planes[0], -1, 0, 0, 1);
	SetPlane(&fixture.planes[1], 0, 1, 0, 1);
	SetPlane(&fixture.planes[2], 0, -1, 0, 1);
	SetPlane(&fixture.planes[3], 0, 0, 1, 1);
	SetPlane(&fixture.planes[4], 0, 0, -1, 1);
	SetPlane(&fixture.planes[5], 1, 0.1f, 0, 0);
	fixture.nodes[0].plane = 5U;
	fixture.nodes[0].children[0] = -2;
	fixture.nodes[0].children[1] = -1;
	fixture.world.plane_count = 6U;
	fixture.world.node_count = 1U;
	fixture.world.leaf_count = 2U;
	fixture.world.model_count = 1U;
	fixture.world.brush_count = 0U;
	fixture.world.brush_side_count = 0U;
	fixture.world.leaf_brush_count = 0U;
	fixture.models[0].headnode = 0;
	Set3(fixture.models[0].mins.value, -1, -1, -1);
	Set3(fixture.models[0].maxs.value, 1, 1, 1);
	fixture.leaves[0].contents = 0;
	fixture.leaves[0].cluster = 0;
	fixture.leaves[0].area = 1U;
	fixture.leaves[0].first_leaf_brush = 0U;
	fixture.leaves[0].leaf_brush_count = 0U;
	fixture.leaves[1].first_leaf_brush = 0U;
	fixture.leaves[1].leaf_brush_count = 0U;
	fixture.configuration.face_count = 6U;
	fixture.configuration.vertex_count = 24U;
	fixture.cell.face_count = 6U;
	memset(fixture.faces, 0, sizeof(fixture.faces));
	memset(fixture.vertices, 0, sizeof(fixture.vertices));
	fixture.cell.bsp_leaf.index = 0U;
	fixture.cell.bsp_area.index = 1U;
	fixture.cell.bsp_cluster.index = 0U;
	Set3(fixture.cell.bounds.mins.value, -1, -1, -1);
	Set3(fixture.cell.bounds.maxs.value, 0.1f, 1, 1);
	Set3(fixture.cell.interior_witness.value, -0.5f, 0, 0);
	for (face = 0; face < 6U; face++)
		SetFace(&fixture, face, fixture.planes[face].normal.value[0],
			fixture.planes[face].normal.value[1],
			fixture.planes[face].normal.value[2],
			fixture.planes[face].distance, SG_CONFIGURATION_PLANE_BSP,
			face, facet_points[face]);
	BindFixture(&fixture);
	CHECK(Build(&fixture, &semantics));
	SG_ConfigurationSemanticsDestroy(semantics);
}

int main(void)
{
	#if defined(SG_CONFIGURATION_SEMANTICS_TESTING)
	#endif
	TestSurfaceAuthorityAndNoPickupObstruction();
	TestActualDomainContactMarksVoidAdjacency();
	TestAllFacetHistoryBoundsSemantics();
	if (failures)
	{
		fprintf(stderr, "%d configuration semantics test(s) failed\n", failures);
		return 1;
	}
	puts("configuration semantics tests passed");
	return 0;
}
