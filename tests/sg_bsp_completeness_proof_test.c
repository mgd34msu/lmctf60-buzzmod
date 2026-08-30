#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../slipgate/sg_bsp_completeness_proof.h"
#include "../slipgate/sg_bsp_completeness_internal.h"

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

typedef struct test_box_s
{
	float mins[3];
	float maxs[3];
	int32_t contents;
} test_box_t;

typedef struct fixture_s
{
	sg_bsp_world_t world;
	sg_bsp_plane_t *planes;
	sg_bsp_node_t *nodes;
	sg_bsp_leaf_t *leaves;
	uint32_t *leaf_brushes;
	sg_bsp_model_t *models;
	sg_bsp_brush_t *brushes;
	sg_bsp_brush_side_t *brush_sides;
} fixture_t;

static void SetVector(float value[3], float x, float y, float z)
{
	value[0] = x;
	value[1] = y;
	value[2] = z;
}

static void SetPlane(sg_bsp_plane_t *plane, float x, float y, float z,
	float distance)
{
	SetVector(plane->normal.value, x, y, z);
	plane->distance = distance;
	plane->type = x == 1.0f ? 0 : (y == 1.0f ? 1 : (z == 1.0f ? 2 : 3));
}

static fixture_t Fixture(const test_box_t *boxes, uint32_t box_count,
	int32_t front_contents, int32_t back_contents)
{
	fixture_t fixture;
	uint32_t brush, side, leaf;

	memset(&fixture, 0, sizeof(fixture));
	fixture.planes = calloc(1U + box_count * 6U, sizeof(*fixture.planes));
	fixture.nodes = calloc(1U, sizeof(*fixture.nodes));
	fixture.leaves = calloc(2U, sizeof(*fixture.leaves));
	fixture.leaf_brushes = calloc(box_count * 2U,
		sizeof(*fixture.leaf_brushes));
	fixture.models = calloc(1U, sizeof(*fixture.models));
	fixture.brushes = calloc(box_count, sizeof(*fixture.brushes));
	fixture.brush_sides = calloc(box_count * 6U,
		sizeof(*fixture.brush_sides));
	if (!fixture.planes || !fixture.nodes || !fixture.leaves ||
		!fixture.leaf_brushes || !fixture.models ||
		(box_count && (!fixture.brushes || !fixture.brush_sides)))
	{
		fputs("fixture allocation failed\n", stderr);
		exit(2);
	}
	SetPlane(&fixture.planes[0], 1.0f, 0.0f, 0.0f, 0.0f);
	fixture.nodes[0].plane = 0U;
	fixture.nodes[0].children[0] = -1;
	fixture.nodes[0].children[1] = -2;
	fixture.leaves[0].contents = front_contents;
	fixture.leaves[0].cluster = 0;
	fixture.leaves[0].area = 1U;
	fixture.leaves[1].contents = back_contents;
	fixture.leaves[1].cluster = back_contents ? -1 : 1;
	fixture.leaves[1].area = 2U;
	for (leaf = 0; leaf < 2U; leaf++)
	{
		fixture.leaves[leaf].first_leaf_brush = leaf * box_count;
		fixture.leaves[leaf].leaf_brush_count = box_count;
		for (brush = 0; brush < box_count; brush++)
			fixture.leaf_brushes[leaf * box_count + brush] = brush;
	}
	for (brush = 0; brush < box_count; brush++)
	{
		uint32_t first = 1U + brush * 6U;

		SetPlane(&fixture.planes[first], 1.0f, 0.0f, 0.0f,
			boxes[brush].maxs[0]);
		SetPlane(&fixture.planes[first + 1U], -1.0f, 0.0f, 0.0f,
			-boxes[brush].mins[0]);
		SetPlane(&fixture.planes[first + 2U], 0.0f, 1.0f, 0.0f,
			boxes[brush].maxs[1]);
		SetPlane(&fixture.planes[first + 3U], 0.0f, -1.0f, 0.0f,
			-boxes[brush].mins[1]);
		SetPlane(&fixture.planes[first + 4U], 0.0f, 0.0f, 1.0f,
			boxes[brush].maxs[2]);
		SetPlane(&fixture.planes[first + 5U], 0.0f, 0.0f, -1.0f,
			-boxes[brush].mins[2]);
		fixture.brushes[brush].first_side = brush * 6U;
		fixture.brushes[brush].side_count = 6U;
		fixture.brushes[brush].contents = boxes[brush].contents;
		for (side = 0; side < 6U; side++)
		{
			fixture.brush_sides[brush * 6U + side].plane = first + side;
			fixture.brush_sides[brush * 6U + side].texinfo = -1;
		}
	}
	fixture.models[0].headnode = 0;
	SetVector(fixture.models[0].mins.value, -4096.0f, -4096.0f, -4096.0f);
	SetVector(fixture.models[0].maxs.value, 4095.875f, 4095.875f, 4095.875f);
	fixture.world.planes = fixture.planes;
	fixture.world.plane_count = 1U + box_count * 6U;
	fixture.world.nodes = fixture.nodes;
	fixture.world.node_count = 1U;
	fixture.world.leaves = fixture.leaves;
	fixture.world.leaf_count = 2U;
	fixture.world.leaf_brushes = fixture.leaf_brushes;
	fixture.world.leaf_brush_count = box_count * 2U;
	fixture.world.models = fixture.models;
	fixture.world.model_count = 1U;
	fixture.world.brushes = fixture.brushes;
	fixture.world.brush_count = box_count;
	fixture.world.brush_sides = fixture.brush_sides;
	fixture.world.brush_side_count = box_count * 6U;
	return fixture;
}

static fixture_t SlabFixture(uint32_t leaf_count)
{
	fixture_t fixture;
	uint32_t node;

	memset(&fixture, 0, sizeof(fixture));
	fixture.planes = calloc(leaf_count - 1U, sizeof(*fixture.planes));
	fixture.nodes = calloc(leaf_count - 1U, sizeof(*fixture.nodes));
	fixture.leaves = calloc(leaf_count, sizeof(*fixture.leaves));
	fixture.models = calloc(1U, sizeof(*fixture.models));
	if (!fixture.planes || !fixture.nodes || !fixture.leaves ||
		!fixture.models)
	{
		fputs("slab fixture allocation failed\n", stderr);
		exit(2);
	}
	for (node = 0; node + 1U < leaf_count; node++)
	{
		float distance = -3072.0f + (float)node * 96.0f;

		SetPlane(&fixture.planes[node], 1.0f, 0.0f, 0.0f, distance);
		fixture.nodes[node].plane = node;
		fixture.nodes[node].children[0] = node + 2U < leaf_count ?
			(int32_t)(node + 1U) : -(int32_t)leaf_count;
		fixture.nodes[node].children[1] = -(int32_t)(node + 1U);
		fixture.leaves[node].cluster = (int32_t)node;
		fixture.leaves[node].area = node + 1U;
	}
	fixture.leaves[leaf_count - 1U].cluster = (int32_t)(leaf_count - 1U);
	fixture.leaves[leaf_count - 1U].area = leaf_count;
	fixture.models[0].headnode = 0;
	SetVector(fixture.models[0].mins.value, -4096.0f, -4096.0f, -4096.0f);
	SetVector(fixture.models[0].maxs.value, 4095.875f, 4095.875f, 4095.875f);
	fixture.world.planes = fixture.planes;
	fixture.world.plane_count = leaf_count - 1U;
	fixture.world.nodes = fixture.nodes;
	fixture.world.node_count = leaf_count - 1U;
	fixture.world.leaves = fixture.leaves;
	fixture.world.leaf_count = leaf_count;
	fixture.world.models = fixture.models;
	fixture.world.model_count = 1U;
	return fixture;
}

static void DestroyFixture(fixture_t *fixture)
{
	free(fixture->planes);
	free(fixture->nodes);
	free(fixture->leaves);
	free(fixture->leaf_brushes);
	free(fixture->models);
	free(fixture->brushes);
	free(fixture->brush_sides);
	memset(fixture, 0, sizeof(*fixture));
}

static sg_rune_model_identity_t Identity(void)
{
	sg_rune_model_identity_t identity;

	memset(&identity, 0, sizeof(identity));
	identity.bsp_content_id = UINT64_C(0x12345678);
	identity.physics_abi_id = UINT64_C(0x87654321);
	identity.source_set_identity = UINT64_C(0xabcddcba);
	SetVector(identity.standing_hull.mins.value, -16.0f, -16.0f, -24.0f);
	SetVector(identity.standing_hull.maxs.value, 16.0f, 16.0f, 32.0f);
	SetVector(identity.crouching_hull.mins.value, -16.0f, -16.0f, -24.0f);
	SetVector(identity.crouching_hull.maxs.value, 16.0f, 16.0f, 4.0f);
	identity.physics.gravity = 800.0f;
	identity.physics.ground_acceleration = 10.0f;
	identity.physics.air_acceleration = 1.0f;
	identity.physics.water_acceleration = 10.0f;
	identity.physics.hook_acceleration = 800.0f;
	identity.physics.external_acceleration = 1.0f;
	identity.physics.water_drag = 1.0f;
	identity.physics.max_velocity = 2000.0f;
	identity.physics.frame_ms = 100U;
	identity.physics.substep_ms = 10U;
	return identity;
}

static int BuildWithIdentity(fixture_t *fixture,
	const sg_rune_model_identity_t *identity,
	sg_host_collision_authority_t *authority,
	sg_configuration_space_t **space_out)
{
	sg_host_collision_error_t host_error;
	sg_configuration_error_t error;

	if (!SG_HostCollisionInit(authority, &fixture->world, identity,
			&host_error))
		return 0;
	if (!SG_ConfigurationBuild(authority, NULL, space_out, &error))
	{
		fprintf(stderr, "build failed: %s source=%u\n",
			SG_ConfigurationErrorString(error.code), error.source_index);
		return 0;
	}
	return 1;
}

static int Build(fixture_t *fixture, sg_host_collision_authority_t *authority,
	sg_configuration_space_t **space_out)
{
	sg_rune_model_identity_t identity = Identity();

	return BuildWithIdentity(fixture, &identity, authority, space_out);
}

static int Prove(const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *space,
	sg_bsp_completeness_result_t *result)
{
	int proved = SG_BspCompletenessProve(authority, space, result);

	if (!proved)
		fprintf(stderr, "proof failed: %s record=%u omitted=%u/%u invented=%u/%u\n",
			SG_BspCompletenessCodeString(result->code), result->record,
			result->omitted_cells, result->omitted_portals,
			result->invented_cells, result->invented_portals);
	return proved;
}

static uint32_t DominantAxis(const float normal[3])
{
	uint32_t axis = 0U;
	uint32_t candidate;

	for (candidate = 1U; candidate < 3U; candidate++)
		if (fabsf(normal[candidate]) > fabsf(normal[axis]))
			axis = candidate;
	return axis;
}

static void TestEmptyWaterVoidAndAdversarialRecords(void)
{
	fixture_t fixture = Fixture(NULL, 0U, SG_HOST_CONTENTS_WATER,
		SG_HOST_CONTENTS_WATER);
	sg_host_collision_authority_t authority;
	sg_configuration_space_t *space = NULL;
	sg_bsp_completeness_result_t result;
	uint32_t cell_count, portal_count;
	float clearance;
	sg_configuration_certificate_node_t *certificate_nodes;
	uint32_t certificate_node_count;

	CHECK(Build(&fixture, &authority, &space));
	if (space)
	{
		CHECK(Prove(&authority, space, &result));
		CHECK(result.expected_cells == 4U);
		CHECK(result.proved_cells == space->cell_count);
		CHECK(result.proved_portals == space->portal_count);
		CHECK(result.standing_regions == 2U);
		CHECK(result.crouching_regions == 2U);
		CHECK(result.water_witnesses > 0U);
		CHECK(result.void_witnesses > 0U);
		{
			uint32_t bsp_face;

			for (bsp_face = 0; bsp_face < space->face_count; bsp_face++)
				if (space->faces[bsp_face].plane.source_kind ==
						SG_CONFIGURATION_PLANE_BSP &&
					space->faces[bsp_face].plane.reversed == 0U)
				{
					space->faces[bsp_face].plane.reversed = 1U;
					break;
				}
			CHECK(bsp_face < space->face_count);
			CHECK(!SG_BspCompletenessProve(&authority, space, &result));
			CHECK(result.code == SG_BSP_COMPLETENESS_INVENTED_CELL);
			space->faces[bsp_face].plane.reversed = 0U;
		}
		certificate_nodes = space->certificate_nodes;
		certificate_node_count = space->certificate_node_count;
		space->certificate_nodes = NULL;
		space->certificate_node_count = 0U;
		CHECK(Prove(&authority, space, &result));
		space->certificate_nodes = certificate_nodes;
		space->certificate_node_count = certificate_node_count;
		cell_count = space->cell_count;
		space->cell_count--;
		CHECK(!SG_BspCompletenessProve(&authority, space, &result));
		CHECK(result.code == SG_BSP_COMPLETENESS_OMITTED_CELL);
		space->cell_count = cell_count;
		portal_count = space->portal_count;
		space->portal_count--;
		CHECK(!SG_BspCompletenessProve(&authority, space, &result));
		CHECK(result.code == SG_BSP_COMPLETENESS_OMITTED_PORTAL);
		space->portal_count = portal_count;
		{
			uint32_t portal;

			for (portal = 0; portal < space->portal_count; portal++)
				if (DominantAxis(space->portals[portal].plane.normal) == 0U &&
					space->portals[portal].plane.distance == 0.0f)
				{
					sg_configuration_portal_t *record = &space->portals[portal];
					float *saved = calloc(record->vertex_count, sizeof(*saved));
					uint32_t offset;

					CHECK(saved != NULL);
					if (!saved)
						break;
					for (offset = 0; offset < record->vertex_count; offset++)
					{
						float *coordinate = &space->vertices[
							record->first_vertex + offset].value[0];

						saved[offset] = *coordinate;
						*coordinate = 1234.0f;
					}
					CHECK(!SG_BspCompletenessProve(&authority, space, &result));
					CHECK(result.code == SG_BSP_COMPLETENESS_INVALID_PORTAL);
					for (offset = 0; offset < record->vertex_count; offset++)
					{
						float *coordinate = &space->vertices[
							record->first_vertex + offset].value[0];

						*coordinate = saved[offset];
					}
					space->vertices[record->first_vertex].value[0] = NAN;
					CHECK(!SG_BspCompletenessProve(&authority, space, &result));
					CHECK(result.code == SG_BSP_COMPLETENESS_INVALID_PORTAL);
					space->vertices[record->first_vertex].value[0] = saved[0];
					free(saved);
					break;
				}
			CHECK(portal < space->portal_count);
		}
		{
			sg_rune_vec3_t *saved_vertices = malloc((size_t)space->vertex_count *
				sizeof(*saved_vertices));
			uint32_t vertex;

			CHECK(saved_vertices != NULL);
			if (saved_vertices)
			{
				memcpy(saved_vertices, space->vertices,
					(size_t)space->vertex_count * sizeof(*saved_vertices));
				for (vertex = 0; vertex < space->vertex_count; vertex++)
					SetVector(space->vertices[vertex].value, 0.0f, 0.0f, 0.0f);
				space->portal_count = 0U;
				CHECK(!SG_BspCompletenessProve(&authority, space, &result));
				CHECK(result.code == SG_BSP_COMPLETENESS_OMITTED_PORTAL);
				space->portal_count = portal_count;
				memcpy(space->vertices, saved_vertices,
					(size_t)space->vertex_count * sizeof(*saved_vertices));
				free(saved_vertices);
			}
		}
		{
			sg_configuration_portal_t *grown = realloc(space->portals,
				(size_t)(portal_count + 1U) * sizeof(*grown));

			CHECK(grown != NULL);
			if (grown)
			{
				space->portals = grown;
				grown[portal_count] = grown[0];
				grown[portal_count].to_cell = grown[portal_count].from_cell;
				space->portal_count++;
				CHECK(!SG_BspCompletenessProve(&authority, space, &result));
				CHECK(result.code == SG_BSP_COMPLETENESS_INVALID_PORTAL);
				space->portal_count--;
			}
		}
		clearance = space->portals[0].clearance;
		space->portals[0].clearance = 0.0f;
		CHECK(!SG_BspCompletenessProve(&authority, space, &result));
		CHECK(result.code == SG_BSP_COMPLETENESS_OMITTED_PORTAL);
		space->portals[0].clearance = clearance;
		CHECK(Prove(&authority, space, &result));
		{
			sg_configuration_cell_t *grown = realloc(space->cells,
				(size_t)(cell_count + 1U) * sizeof(*grown));

			CHECK(grown != NULL);
			if (grown)
			{
				space->cells = grown;
				grown[cell_count] = grown[0];
				space->cell_count++;
				CHECK(!SG_BspCompletenessProve(&authority, space, &result));
				CHECK(result.code == SG_BSP_COMPLETENESS_INVENTED_CELL);
				space->cell_count--;
			}
		}
	}
	SG_ConfigurationDestroy(space);
	DestroyFixture(&fixture);
}

static void TestHostLeafGatingAndExactBrushBoundary(void)
{
	const test_box_t solid = {
		{ -8, -8, -8 }, { 8, 8, 8 }, SG_HOST_CONTENTS_SOLID
	};
	fixture_t fixture = Fixture(&solid, 1U, 0, 0);
	sg_host_collision_authority_t authority;
	sg_configuration_space_t *space = NULL;
	sg_bsp_completeness_result_t result;
	sg_host_collision_pose_t pose;
	float inside[3] = { 0.0f, 0.0f, 0.0f };
	float boundary[3] = { 24.0f, 0.0f, 0.0f };
	uint32_t face;
	CHECK(Build(&fixture, &authority, &space));
	CHECK(SG_HostCollisionClassifyPose(&authority, NULL, inside,
		SG_RUNE_STANCE_STANDING, &pose));
	CHECK(pose.valid);
	if (space)
	{
		CHECK(!SG_BspCompletenessProve(&authority, space, &result));
		CHECK(result.code == SG_BSP_COMPLETENESS_OMITTED_CELL);
	}
	SG_ConfigurationDestroy(space);
	DestroyFixture(&fixture);
	fixture = Fixture(&solid, 1U, SG_HOST_CONTENTS_SOLID,
		SG_HOST_CONTENTS_SOLID);
	space = NULL;
	CHECK(Build(&fixture, &authority, &space));
	CHECK(SG_HostCollisionClassifyPose(&authority, NULL, boundary,
		SG_RUNE_STANCE_STANDING, &pose));
	CHECK(!pose.valid);
	if (space)
	{
		for (face = 0; face < space->face_count; face++)
			if (space->faces[face].plane.source_kind ==
					SG_CONFIGURATION_PLANE_EXPANDED_BRUSH &&
				space->faces[face].plane.reversed != 0U &&
				space->faces[face].plane.normal[0] == -1.0f &&
				space->faces[face].plane.distance == -24.0f)
			{
				space->faces[face].plane.reversed = 0U;
				break;
			}
		CHECK(face < space->face_count);
		CHECK(!SG_BspCompletenessProve(&authority, space, &result));
		CHECK(result.code == SG_BSP_COMPLETENESS_INVENTED_CELL);
	}
	SG_ConfigurationDestroy(space);
	DestroyFixture(&fixture);
}

static void TestEquivalentPlanePortalIndex(void)
{
	fixture_t fixture = Fixture(NULL, 0U, 0, 0);
	sg_host_collision_authority_t authority;
	sg_configuration_space_t *space = NULL;
	sg_bsp_completeness_result_t result;
	uint32_t face, axis;

	CHECK(Build(&fixture, &authority, &space));
	if (space)
	{
		for (face = 0; face < space->face_count; face++)
			if (space->faces[face].plane.source_kind ==
					SG_CONFIGURATION_PLANE_BSP)
			{
				for (axis = 0; axis < 3U; axis++)
					space->faces[face].plane.normal[axis] *= 1e20f;
				space->faces[face].plane.distance *= 1e20f;
			}
		CHECK(Prove(&authority, space, &result));
		for (face = 0; face < space->portal_count; face++)
		{
			for (axis = 0; axis < 3U; axis++)
				space->portals[face].plane.normal[axis] *= 1e20f;
			space->portals[face].plane.distance *= 1e20f;
		}
		CHECK(Prove(&authority, space, &result));
		space->portal_count = 0U;
		CHECK(!SG_BspCompletenessProve(&authority, space, &result));
		CHECK(result.code == SG_BSP_COMPLETENESS_OMITTED_PORTAL);
	}
	SG_ConfigurationDestroy(space);
	DestroyFixture(&fixture);
}

static void TestCellCoveragePlaneScaleInvariance(void)
{
	static const float scales[] = {
		0x1p-40f, 0x1p-11f, 1.0f, 0x1p40f
	};
	fixture_t fixture = Fixture(NULL, 0U, SG_HOST_CONTENTS_WATER,
		SG_HOST_CONTENTS_WATER);
	sg_host_collision_authority_t authority;
	sg_configuration_space_t *space = NULL;
	sg_bsp_completeness_result_t result;
	uint32_t scale;

	CHECK(Build(&fixture, &authority, &space));
	if (space)
	{
		uint32_t cell;
		uint32_t face = UINT32_MAX;
		uint32_t target_cell = UINT32_MAX;

		for (cell = 0; cell < space->cell_count && face == UINT32_MAX; cell++)
		{
			uint32_t offset;

			if (space->cells[cell].bsp_leaf.index != 0U)
				continue;
			for (offset = 0; offset < space->cells[cell].face_count; offset++)
			{
				uint32_t candidate = space->cells[cell].first_face + offset;
				const sg_configuration_plane_t *plane =
					&space->faces[candidate].plane;

				if (plane->source_kind == SG_CONFIGURATION_PLANE_DOMAIN &&
					plane->source_index == 0U && plane->reversed == 0U)
				{
					face = candidate;
					target_cell = cell;
					break;
				}
			}
		}
		CHECK(face != UINT32_MAX);
		CHECK(target_cell != UINT32_MAX);
		if (face != UINT32_MAX && target_cell != UINT32_MAX)
		{
			space->cells[target_cell].interior_witness.value[0] = 4096.0f;
			for (scale = 0; scale < sizeof(scales) / sizeof(scales[0]); scale++)
			{
				space->faces[face].plane.normal[0] = scales[scale];
				space->faces[face].plane.distance = 4095.875f * scales[scale];
				CHECK(!SG_BspCompletenessProve(&authority, space, &result));
				CHECK(result.code == SG_BSP_COMPLETENESS_INVALID_CELL);
				CHECK(result.record == target_cell);
			}
		}
	}
	SG_ConfigurationDestroy(space);
	DestroyFixture(&fixture);
}

static void TestTranslatedCornerRounding(void)
{
	const float origin[3] = { -3265.625f, 979.625f, -1701.125f };
	test_box_t box = {
		{ -3282.625f, 995.625f, -1685.125f },
		{ -3281.625f, 996.625f, -1684.125f }, SG_HOST_CONTENTS_SOLID
	};
	fixture_t fixture = Fixture(&box, 1U, SG_HOST_CONTENTS_SOLID, 0);
	sg_rune_model_identity_t identity = Identity();
	sg_host_collision_authority_t authority;
	sg_configuration_space_t *space = NULL;
	sg_bsp_completeness_result_t result;
	sg_host_collision_pose_t pose;
	SetPlane(&fixture.planes[0], -0.628799975f, 0.584699988f,
		0.658399999f, 1538.01318f);
	fixture.leaves[0].leaf_brush_count = 1U;
	fixture.leaves[1].leaf_brush_count = 0U;
	SetVector(identity.standing_hull.mins.value, -16.0f, -16.0f, -16.0f);
	SetVector(identity.standing_hull.maxs.value, 16.0f, 16.0f, 16.0f);
	identity.crouching_hull = identity.standing_hull;
	CHECK(BuildWithIdentity(&fixture, &identity, &authority, &space));
	CHECK(SG_HostCollisionClassifyPose(&authority, NULL, origin,
		SG_RUNE_STANCE_STANDING, &pose));
	CHECK(!pose.valid);
	if (space)
	{
		CHECK(!SG_BspCompletenessProve(&authority, space, &result));
		CHECK(result.code == SG_BSP_COMPLETENESS_OMITTED_PORTAL);
		CHECK(result.proved_cells == space->cell_count);
	}
	SG_ConfigurationDestroy(space);
	DestroyFixture(&fixture);
}

static uint32_t PortalsForStance(const sg_configuration_space_t *space,
	sg_rune_stance_t stance)
{
	uint32_t count = 0U;
	uint32_t portal;
	for (portal = 0; portal < space->portal_count; portal++)
		if (space->portals[portal].stance == stance)
			count++;
	return count;
}

static void TestLowCeilingWindowAndHalfWall(void)
{
	const test_box_t low_door[] = {
		{ { -2, -4096, -4096 }, { 2, -20, 4096 }, SG_HOST_CONTENTS_SOLID },
		{ { -2, 20, -4096 }, { 2, 4096, 4096 }, SG_HOST_CONTENTS_SOLID },
		{ { -2, -20, -4096 }, { 2, 20, -24 }, SG_HOST_CONTENTS_SOLID },
		{ { -2, -20, 12 }, { 2, 20, 4096 }, SG_HOST_CONTENTS_SOLID }
	};
	const test_box_t window[] = {
		{ { -2, -4096, -4096 }, { 2, -20, 4096 }, SG_HOST_CONTENTS_SOLID },
		{ { -2, 20, -4096 }, { 2, 4096, 4096 }, SG_HOST_CONTENTS_SOLID },
		{ { -2, -20, -4096 }, { 2, 20, 0 }, SG_HOST_CONTENTS_SOLID },
		{ { -2, -20, 20 }, { 2, 20, 4096 }, SG_HOST_CONTENTS_SOLID }
	};
	fixture_t fixture = Fixture(low_door, 4U, SG_HOST_CONTENTS_SOLID,
		SG_HOST_CONTENTS_SOLID);
	sg_host_collision_authority_t authority;
	sg_configuration_space_t *space = NULL;
	sg_bsp_completeness_result_t result;
	CHECK(Build(&fixture, &authority, &space));
	if (space)
	{
		CHECK(Prove(&authority, space, &result));
		CHECK(PortalsForStance(space, SG_RUNE_STANCE_STANDING) == 0U);
		CHECK(PortalsForStance(space, SG_RUNE_STANCE_CROUCHING) > 0U);
	}
	SG_ConfigurationDestroy(space);
	DestroyFixture(&fixture);
	fixture = Fixture(window, 4U, SG_HOST_CONTENTS_SOLID,
		SG_HOST_CONTENTS_SOLID);
	space = NULL;
	CHECK(Build(&fixture, &authority, &space));
	if (space)
	{
		CHECK(Prove(&authority, space, &result));
		CHECK(PortalsForStance(space, SG_RUNE_STANCE_STANDING) == 0U);
		CHECK(PortalsForStance(space, SG_RUNE_STANCE_CROUCHING) == 0U);
	}
	SG_ConfigurationDestroy(space);
	DestroyFixture(&fixture);
}

static void TestRampMultiHeightAndLargeCoordinates(void)
{
	test_box_t boxes[] = {
		{ { -96, -96, -4096 }, { -4, 96, -8 }, SG_HOST_CONTENTS_SOLID },
		{ { 4, -96, -4096 }, { 96, 96, 32 }, SG_HOST_CONTENTS_SOLID }
	};
	const test_box_t large = {
		{ 3400, -64, -4096 }, { 3500, 64, 128 }, SG_HOST_CONTENTS_SOLID
	};
	fixture_t fixture = Fixture(boxes, 2U, SG_HOST_CONTENTS_SOLID,
		SG_HOST_CONTENTS_SOLID);
	sg_host_collision_authority_t authority;
	sg_configuration_space_t *space = NULL;
	sg_bsp_completeness_result_t result;
	const float diagonal = 0.70710677f;

	SetPlane(&fixture.planes[5], -diagonal, 0.0f, diagonal, 16.0f);
	CHECK(Build(&fixture, &authority, &space));
	if (space)
	{
		CHECK(Prove(&authority, space, &result));
		CHECK(result.expected_cells > 4U);
		CHECK(result.airborne_witnesses > 0U);
		CHECK(result.lattice_solve_calls > 0U);
	}
	SG_ConfigurationDestroy(space);
	DestroyFixture(&fixture);
	fixture = Fixture(&large, 1U, SG_HOST_CONTENTS_SOLID,
		SG_HOST_CONTENTS_SOLID);
	space = NULL;
	CHECK(Build(&fixture, &authority, &space));
	if (space)
	{
		CHECK(Prove(&authority, space, &result));
		CHECK(result.expected_cells > 4U);
	}
	SG_ConfigurationDestroy(space);
	DestroyFixture(&fixture);
}

static void TestSupportedRoom(void)
{
	const test_box_t floor = {
		{ -5000, -5000, -4096 }, { 5000, 5000, 0 }, SG_HOST_CONTENTS_SOLID
	};
	fixture_t fixture = Fixture(&floor, 1U, SG_HOST_CONTENTS_SOLID,
		SG_HOST_CONTENTS_SOLID);
	sg_host_collision_authority_t authority;
	sg_configuration_space_t *space = NULL;
	sg_bsp_completeness_result_t result;

	CHECK(Build(&fixture, &authority, &space));
	if (space)
	{
		CHECK(Prove(&authority, space, &result));
		CHECK(result.supported_witnesses > 0U);
		CHECK(result.airborne_witnesses > 0U);
		CHECK(result.analytically_removed_pieces > 0U);
	}
	SG_ConfigurationDestroy(space);
	DestroyFixture(&fixture);
}

static void TestIndexedScaling(void)
{
	const uint32_t leaf_count = 64U;
	fixture_t fixture = SlabFixture(leaf_count);
	sg_host_collision_authority_t authority;
	sg_configuration_space_t *space = NULL;
	sg_bsp_completeness_result_t result;
	uint64_t global_cell_pairs;
	CHECK(Build(&fixture, &authority, &space));
	if (space)
	{
		CHECK(Prove(&authority, space, &result));
		CHECK(space->cell_count == leaf_count * 2U);
		global_cell_pairs = (uint64_t)space->cell_count *
			(uint64_t)(space->cell_count - 1U) / 2U;
		CHECK(result.leaf_brush_candidates == 0U);
		CHECK(result.blocker_cell_candidates == 0U);
		CHECK(result.cell_overlap_candidates == 0U);
		CHECK(result.coverage_region_candidates <=
			(uint64_t)space->cell_count * 8U);
		CHECK(result.portal_face_candidates < global_cell_pairs);
		CHECK(result.portal_face_candidates <=
			(uint64_t)space->portal_count * 2U);
		CHECK(result.portal_endpoint_lookups == space->portal_count);
		CHECK(result.portal_lookup_candidates == space->portal_count);
	}
	SG_ConfigurationDestroy(space);
	DestroyFixture(&fixture);
}

static void TestIrrelevantBrushFiltering(void)
{
	test_box_t boxes[64];
	fixture_t fixture;
	sg_host_collision_authority_t authority;
	sg_configuration_space_t *space = NULL;
	sg_bsp_completeness_result_t result;
	uint32_t box;
	for (box = 0; box < 64U; box++)
	{
		SetVector(boxes[box].mins, -8.0f, -8.0f, -8.0f);
		SetVector(boxes[box].maxs, 8.0f, 8.0f, 8.0f);
		boxes[box].contents = SG_HOST_CONTENTS_SOLID;
	}
	fixture = Fixture(boxes, 64U, 0, 0);
	CHECK(Build(&fixture, &authority, &space));
	if (space)
	{
		CHECK(!SG_BspCompletenessProve(&authority, space, &result));
		CHECK(result.code == SG_BSP_COMPLETENESS_OMITTED_CELL);
		CHECK(fixture.world.brush_count == 64U);
		CHECK(result.leaf_brush_candidates == 0U);
		CHECK(result.blocker_cell_candidates == 0U);
		CHECK(result.blocker_subtraction_candidates == 0U);
	}
	SG_ConfigurationDestroy(space);
	DestroyFixture(&fixture);
	fixture = Fixture(boxes, 64U, SG_HOST_CONTENTS_SOLID, 0);
	fixture.leaves[0].leaf_brush_count = 1U;
	fixture.leaves[1].leaf_brush_count = 0U;
	space = NULL;
	CHECK(Build(&fixture, &authority, &space));
	if (space)
	{
		CHECK(!SG_BspCompletenessProve(&authority, space, &result));
		CHECK(result.code == SG_BSP_COMPLETENESS_OMITTED_CELL);
		CHECK(fixture.world.brush_count == 64U);
		CHECK(result.leaf_brush_candidates == 2U);
		CHECK(result.leaf_brush_candidates < fixture.world.brush_count);
	}
	SG_ConfigurationDestroy(space);
	DestroyFixture(&fixture);
}

static void TestSameLeafIntervalScaling(void)
{
	test_box_t boxes[12];
	fixture_t fixture;
	sg_host_collision_authority_t authority;
	sg_configuration_space_t *space = NULL;
	sg_bsp_completeness_result_t result;
	uint64_t global_pairs;
	uint32_t box;
	for (box = 0; box < 12U; box++)
	{
		float minimum = 64.0f + (float)box * 256.0f;
		SetVector(boxes[box].mins, minimum, -4096.0f, -4096.0f);
		SetVector(boxes[box].maxs, minimum + 4.0f, 4096.0f, 4096.0f);
		boxes[box].contents = SG_HOST_CONTENTS_SOLID;
	}
	fixture = Fixture(boxes, 12U, SG_HOST_CONTENTS_SOLID,
		SG_HOST_CONTENTS_SOLID);
	CHECK(Build(&fixture, &authority, &space));
	if (space)
	{
		CHECK(Prove(&authority, space, &result));
		CHECK(space->cell_count >= 24U);
		global_pairs = (uint64_t)space->cell_count *
			(uint64_t)space->cell_count;
		CHECK(result.coverage_region_examined < global_pairs);
		CHECK(result.coverage_region_candidates <=
			(uint64_t)space->cell_count * 8U);
	}
	SG_ConfigurationDestroy(space);
	DestroyFixture(&fixture);
}

int main(void)
{
	CHECK(SG_BspProofTestZeroPolygonPortalKinds());
	TestEmptyWaterVoidAndAdversarialRecords();
	TestHostLeafGatingAndExactBrushBoundary();
	TestEquivalentPlanePortalIndex();
	TestCellCoveragePlaneScaleInvariance();
	TestTranslatedCornerRounding();
	TestLowCeilingWindowAndHalfWall();
	TestRampMultiHeightAndLargeCoordinates();
	TestSupportedRoom();
	TestIndexedScaling();
	TestIrrelevantBrushFiltering();
	TestSameLeafIntervalScaling();
	if (failures)
	{
		fprintf(stderr, "%d BSP completeness checks failed\n", failures);
		return 1;
	}
	puts("BSP completeness proof checks passed");
	return 0;
}
