#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../slipgate/sg_configuration_audit.h"
#include "../slipgate/sg_configuration_lattice.h"

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

static float FaceProjectedArea(const sg_configuration_space_t *space,
	const sg_configuration_face_t *face, uint32_t axis)
{
	uint32_t u = (axis + 1U) % 3U;
	uint32_t v = (axis + 2U) % 3U;
	uint32_t point;
	float area = 0.0f;

	for (point = 0; point < face->vertex_count; point++)
	{
		const float *a = space->vertices[face->first_vertex + point].value;
		const float *b = space->vertices[face->first_vertex +
			(point + 1U) % face->vertex_count].value;

		area += a[u] * b[v] - a[v] * b[u];
	}
	return area;
}

static fixture_t Fixture(const test_box_t *boxes, uint32_t box_count,
	int32_t front_contents, int32_t back_contents)
{
	fixture_t fixture;
	uint32_t brush, side, leaf;

	memset(&fixture, 0, sizeof(fixture));
	fixture.planes = calloc(1U + box_count * 6U, sizeof(*fixture.planes));
	fixture.nodes = calloc(1, sizeof(*fixture.nodes));
	fixture.leaves = calloc(2, sizeof(*fixture.leaves));
	fixture.leaf_brushes = calloc(box_count * 2U,
		sizeof(*fixture.leaf_brushes));
	fixture.models = calloc(1, sizeof(*fixture.models));
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
	fixture.nodes[0].plane = 0;
	fixture.nodes[0].children[0] = -1;
	fixture.nodes[0].children[1] = -2;
	fixture.leaves[0].contents = front_contents;
	fixture.leaves[0].cluster = 0;
	fixture.leaves[0].area = 1;
	fixture.leaves[1].contents = back_contents;
	fixture.leaves[1].cluster = back_contents ? -1 : 1;
	fixture.leaves[1].area = 2;
	for (leaf = 0; leaf < 2; leaf++)
	{
		fixture.leaves[leaf].first_leaf_brush = leaf * box_count;
		fixture.leaves[leaf].leaf_brush_count = box_count;
		for (brush = 0; brush < box_count; brush++)
			fixture.leaf_brushes[leaf * box_count + brush] = brush;
	}
	for (brush = 0; brush < box_count; brush++)
	{
		uint32_t first = 1U + brush * 6U;

		SetPlane(&fixture.planes[first], 1, 0, 0, boxes[brush].maxs[0]);
		SetPlane(&fixture.planes[first + 1U], -1, 0, 0,
			-boxes[brush].mins[0]);
		SetPlane(&fixture.planes[first + 2U], 0, 1, 0,
			boxes[brush].maxs[1]);
		SetPlane(&fixture.planes[first + 3U], 0, -1, 0,
			-boxes[brush].mins[1]);
		SetPlane(&fixture.planes[first + 4U], 0, 0, 1,
			boxes[brush].maxs[2]);
		SetPlane(&fixture.planes[first + 5U], 0, 0, -1,
			-boxes[brush].mins[2]);
		fixture.brushes[brush].first_side = brush * 6U;
		fixture.brushes[brush].side_count = 6;
		fixture.brushes[brush].contents = boxes[brush].contents;
		for (side = 0; side < 6; side++)
		{
			fixture.brush_sides[brush * 6U + side].plane = first + side;
			fixture.brush_sides[brush * 6U + side].texinfo = -1;
		}
	}
	fixture.models[0].headnode = 0;
	SetVector(fixture.models[0].mins.value, -64, -64, -64);
	SetVector(fixture.models[0].maxs.value, 64, 64, 64);
	fixture.world.planes = fixture.planes;
	fixture.world.plane_count = 1U + box_count * 6U;
	fixture.world.nodes = fixture.nodes;
	fixture.world.node_count = 1;
	fixture.world.leaves = fixture.leaves;
	fixture.world.leaf_count = 2;
	fixture.world.leaf_brushes = fixture.leaf_brushes;
	fixture.world.leaf_brush_count = box_count * 2U;
	fixture.world.models = fixture.models;
	fixture.world.model_count = 1;
	fixture.world.brushes = fixture.brushes;
	fixture.world.brush_count = box_count;
	fixture.world.brush_sides = fixture.brush_sides;
	fixture.world.brush_side_count = box_count * 6U;
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

static void SetReferencedBrushes(fixture_t *fixture,
	const uint32_t *brushes, uint32_t brush_count)
{
	uint32_t leaf, brush;

	for (leaf = 0U; leaf < fixture->world.leaf_count; leaf++)
	{
		fixture->leaves[leaf].first_leaf_brush = leaf * brush_count;
		fixture->leaves[leaf].leaf_brush_count = brush_count;
		for (brush = 0U; brush < brush_count; brush++)
			fixture->leaf_brushes[leaf * brush_count + brush] = brushes[brush];
	}
	fixture->world.leaf_brush_count = fixture->world.leaf_count * brush_count;
}

static sg_rune_model_identity_t Identity(void)
{
	sg_rune_model_identity_t identity;

	memset(&identity, 0, sizeof(identity));
	identity.bsp_content_id = UINT64_C(0x12345678);
	identity.physics_abi_id = UINT64_C(0x87654321);
	identity.source_set_identity = UINT64_C(0xabcddcba);
	SetVector(identity.standing_hull.mins.value, -16, -16, -24);
	SetVector(identity.standing_hull.maxs.value, 16, 16, 32);
	SetVector(identity.crouching_hull.mins.value, -16, -16, -24);
	SetVector(identity.crouching_hull.maxs.value, 16, 16, 4);
	identity.physics.gravity = 800;
	identity.physics.ground_acceleration = 10;
	identity.physics.air_acceleration = 1;
	identity.physics.water_acceleration = 10;
	identity.physics.hook_acceleration = 800;
	identity.physics.external_acceleration = 1;
	identity.physics.water_drag = 1;
	identity.physics.max_velocity = 2000;
	identity.physics.frame_ms = 100;
	identity.physics.substep_ms = 10;
	return identity;
}

static int Build(fixture_t *fixture, const sg_configuration_limits_t *limits,
	sg_configuration_space_t **space_out,
	sg_configuration_audit_result_t *audit_out)
{
	sg_rune_model_identity_t identity = Identity();
	sg_host_collision_authority_t authority;
	sg_host_collision_error_t host_error;
	sg_configuration_error_t error;

	if (!SG_HostCollisionInit(&authority, &fixture->world, &identity,
			&host_error))
		return 0;
	if (!SG_ConfigurationBuild(&authority, limits, space_out, &error))
	{
		fprintf(stderr, "build failed: %s source=%u\n",
			SG_ConfigurationErrorString(error.code), error.source_index);
		return 0;
	}
	if (!SG_ConfigurationAudit(&authority, *space_out, audit_out))
	{
		fprintf(stderr, "audit failed: %s record=%u\n",
			SG_ConfigurationAuditCodeString(audit_out->code), audit_out->record);
		return 0;
	}
	return 1;
}

static uint32_t CountPortalStance(const sg_configuration_space_t *space,
	sg_rune_stance_t stance)
{
	uint32_t count = 0, portal;

	for (portal = 0; portal < space->portal_count; portal++)
		if (space->portals[portal].stance == stance)
			count++;
	return count;
}

static int SameConfigurationGeometry(const sg_configuration_space_t *left,
	const sg_configuration_space_t *right)
{
	return left->cell_count == right->cell_count &&
		left->face_count == right->face_count &&
		left->vertex_count == right->vertex_count &&
		left->portal_count == right->portal_count &&
		left->stance_overlap_count == right->stance_overlap_count &&
		left->certificate_node_count == right->certificate_node_count &&
		memcmp(left->certificate_roots, right->certificate_roots,
			sizeof(left->certificate_roots)) == 0 &&
		memcmp(left->cells, right->cells,
			(size_t)left->cell_count * sizeof(*left->cells)) == 0 &&
		memcmp(left->faces, right->faces,
			(size_t)left->face_count * sizeof(*left->faces)) == 0 &&
		memcmp(left->vertices, right->vertices,
			(size_t)left->vertex_count * sizeof(*left->vertices)) == 0 &&
		memcmp(left->portals, right->portals,
			(size_t)left->portal_count * sizeof(*left->portals)) == 0 &&
		memcmp(left->stance_overlaps, right->stance_overlaps,
			(size_t)left->stance_overlap_count *
				sizeof(*left->stance_overlaps)) == 0 &&
		memcmp(left->certificate_nodes, right->certificate_nodes,
			(size_t)left->certificate_node_count *
				sizeof(*left->certificate_nodes)) == 0;
}

static void CheckTopologyIndex(const sg_configuration_space_t *space)
{
	sg_rune_compact_spatial_error_t error;
	uint32_t *cells = malloc((size_t)space->cell_count * sizeof(*cells));
	uint32_t cell, portal;

	CHECK(space->topology_index != NULL);
	CHECK(cells != NULL || space->cell_count == 0U);
	if (!space->topology_index || (!cells && space->cell_count))
		goto done;
	for (cell = 0U; cell < space->cell_count; cell++)
	{
		uint32_t count = 0U, found = 0U, index;

		CHECK(SG_RuneCompactSpatialIndexQueryCells(space->topology_index,
			&space->cells[cell].interior_witness, cells, space->cell_count,
			&count, &error));
		for (index = 0U; index < count; index++)
			found |= cells[index] == cell;
		CHECK(found);
	}
	for (portal = 0U; portal < space->portal_count; portal++)
	{
		const sg_configuration_portal_t *record = &space->portals[portal];
		sg_rune_vec3_t center = { { 0.0f, 0.0f, 0.0f } };
		uint32_t count = 0U, index, endpoint_count = 0U, axis, vertex;

		for (vertex = 0U; vertex < record->vertex_count; vertex++)
			for (axis = 0U; axis < 3U; axis++)
				center.value[axis] += space->vertices[
					record->first_vertex + vertex].value[axis];
		for (axis = 0U; axis < 3U; axis++)
			center.value[axis] /= (float)record->vertex_count;
		CHECK(SG_RuneCompactSpatialIndexQueryCells(space->topology_index,
			&center, cells, space->cell_count, &count, &error));
		for (index = 0U; index < count; index++)
			endpoint_count += cells[index] == record->from_cell ||
				cells[index] == record->to_cell;
		CHECK(endpoint_count == 1U);
	}

done:
	free(cells);
}

static int ReplaceBoundaryWithInteriorTriangle(sg_configuration_space_t *space,
	uint32_t cell_index, const sg_configuration_plane_t *boundary)
{
	const sg_configuration_cell_t *cell = &space->cells[cell_index];
	uint32_t face_offset;

	for (face_offset = 0U; face_offset < cell->face_count; face_offset++)
	{
		sg_configuration_face_t *face = &space->faces[
			cell->first_face + face_offset];
		uint32_t vertex;

		if (face->kind != SG_CONFIGURATION_FACE_FACET ||
			face->plane.source_kind != boundary->source_kind ||
			face->plane.source_index != boundary->source_index ||
			face->plane.source_variant != boundary->source_variant)
			continue;
		for (vertex = 0U; vertex < face->vertex_count; vertex++)
		{
			float *point = space->vertices[
				face->first_vertex + vertex].value;

			memcpy(point, cell->interior_witness.value,
				sizeof(cell->interior_witness.value));
			point[vertex % 3U] += 0.01f;
		}
		return 1;
	}
	return 0;
}

static void TestFullDomainCorridorAndWater(void)
{
	fixture_t fixture = Fixture(NULL, 0, SG_HOST_CONTENTS_WATER, 0);
	sg_configuration_space_t *space = NULL;
	sg_configuration_audit_result_t audit = { 0 };
	uint32_t cell;
	int saw_min = 0, saw_max = 0, saw_water = 0, saw_air = 0;
	sg_rune_model_identity_t identity = Identity();
	sg_host_collision_authority_t authority;
	sg_host_collision_error_t host_error;
	uint32_t portal_count;

	CHECK(Build(&fixture, NULL, &space, &audit));
	if (space)
	{
		CHECK(space->cell_count == 4U);
		CHECK(space->portal_count == 2U);
		CHECK(space->stance_overlap_count == 2U);
		for (cell = 0; cell < space->cell_count; cell++)
		{
			saw_min |= space->cells[cell].bounds.mins.value[0] ==
				SG_CONFIGURATION_PMOVE_ORIGIN_MIN;
			saw_max |= space->cells[cell].bounds.maxs.value[0] ==
				SG_CONFIGURATION_PMOVE_ORIGIN_MAX;
			saw_water |= (space->cells[cell].contents &
				SG_RUNE_CONTENTS_WATER) != 0U;
			saw_air |= (space->cells[cell].witness_pose_flags &
				SG_CONFIGURATION_POSE_AIRBORNE) != 0U;
		}
		CHECK(saw_min && saw_max && saw_water && saw_air);
		CHECK(audit.proved_cells == space->cell_count);
		CHECK(SG_HostCollisionInit(&authority, &fixture.world, &identity,
			&host_error));
		portal_count = space->portal_count;
		{
			sg_configuration_portal_t saved = space->portals[0];
			sg_rune_vec3_t *saved_vertices = malloc(
				(size_t)saved.vertex_count * sizeof(*saved_vertices));
			uint32_t vertex, axis, drop = 0U;
			float center[3] = { 0.0f, 0.0f, 0.0f };
			float normal_length;

			CHECK(saved_vertices != NULL);
			if (saved_vertices)
			{
				memcpy(saved_vertices, &space->vertices[saved.first_vertex],
					(size_t)saved.vertex_count * sizeof(*saved_vertices));
				space->portals[0].clearance = 0.0f;
				CHECK(!SG_ConfigurationAudit(&authority, space, &audit));
				CHECK(audit.code == SG_CONFIGURATION_AUDIT_INVENTED_PORTAL);
				space->portals[0] = saved;
				space->portals[0].clearance = NAN;
				CHECK(!SG_ConfigurationAudit(&authority, space, &audit));
				CHECK(audit.code == SG_CONFIGURATION_AUDIT_INVENTED_PORTAL);
				space->portals[0] = saved;
				space->portals[0].stance = saved.stance ==
					SG_RUNE_STANCE_STANDING ? SG_RUNE_STANCE_CROUCHING :
					SG_RUNE_STANCE_STANDING;
				CHECK(!SG_ConfigurationAudit(&authority, space, &audit));
				CHECK(audit.code == SG_CONFIGURATION_AUDIT_INVENTED_PORTAL);
				space->portals[0] = saved;
				for (axis = 1U; axis < 3U; axis++)
					if (fabsf(saved.plane.normal[axis]) >
						fabsf(saved.plane.normal[drop]))
						drop = axis;
				for (vertex = 0U; vertex < saved.vertex_count; vertex++)
					for (axis = 0U; axis < 3U; axis++)
						center[axis] += saved_vertices[vertex].value[axis] /
							(float)saved.vertex_count;
				for (vertex = 0U; vertex < 3U; vertex++)
				{
					float *point = space->vertices[
						saved.first_vertex + vertex].value;
					uint32_t u = (drop + 1U) % 3U;
					uint32_t v = (drop + 2U) % 3U;

					memcpy(point, center, sizeof(center));
					if (vertex == 1U)
						point[u] += 0.01f;
					if (vertex == 2U)
						point[v] += 0.01f;
					point[drop] = (saved.plane.distance -
						saved.plane.normal[u] * point[u] -
						saved.plane.normal[v] * point[v]) /
						saved.plane.normal[drop];
				}
				normal_length = sqrtf(saved.plane.normal[0] *
					saved.plane.normal[0] + saved.plane.normal[1] *
					saved.plane.normal[1] + saved.plane.normal[2] *
					saved.plane.normal[2]);
				space->portals[0].vertex_count = 3U;
				space->portals[0].clearance = sqrtf(0.00005f *
					normal_length / fabsf(saved.plane.normal[drop]));
				CHECK(!SG_ConfigurationAudit(&authority, space, &audit));
				CHECK(audit.code ==
					SG_CONFIGURATION_AUDIT_HOST_PORTAL_DISAGREEMENT);
				space->portals[0] = saved;
				memcpy(&space->vertices[saved.first_vertex], saved_vertices,
					(size_t)saved.vertex_count * sizeof(*saved_vertices));
				{
					sg_configuration_portal_t *grown = realloc(space->portals,
						(size_t)(portal_count + 1U) * sizeof(*grown));

					CHECK(grown != NULL);
					if (grown)
					{
						space->portals = grown;
						space->portals[portal_count] = saved;
						space->portal_count = portal_count + 1U;
						CHECK(!SG_ConfigurationAudit(&authority, space, &audit));
						CHECK(audit.code ==
							SG_CONFIGURATION_AUDIT_INVENTED_PORTAL);
						space->portal_count = portal_count;
					}
				}
			}
			free(saved_vertices);
		}
		{
			const sg_configuration_portal_t boundary = space->portals[0];
			sg_rune_vec3_t *saved_vertices = malloc(
				(size_t)space->vertex_count * sizeof(*saved_vertices));
			float saved_bound =
				space->cells[boundary.from_cell].bounds.mins.value[0];

			CHECK(saved_vertices != NULL);
			if (saved_vertices)
			{
				memcpy(saved_vertices, space->vertices,
					(size_t)space->vertex_count * sizeof(*saved_vertices));
				CHECK(ReplaceBoundaryWithInteriorTriangle(space,
					boundary.from_cell, &boundary.plane));
				CHECK(ReplaceBoundaryWithInteriorTriangle(space,
					boundary.to_cell, &boundary.plane));
				CHECK(!SG_ConfigurationAudit(&authority, space, &audit));
				CHECK(audit.code == SG_CONFIGURATION_AUDIT_INVALID_CELL);
				memcpy(space->vertices, saved_vertices,
					(size_t)space->vertex_count * sizeof(*saved_vertices));
				space->cells[boundary.from_cell].bounds.mins.value[0] += 0.125f;
				CHECK(!SG_ConfigurationAudit(&authority, space, &audit));
				CHECK(audit.code == SG_CONFIGURATION_AUDIT_INVALID_CELL);
				space->cells[boundary.from_cell].bounds.mins.value[0] = saved_bound;
			}
			free(saved_vertices);
		}
		space->portal_count--;
		CHECK(!SG_ConfigurationAudit(&authority, space, &audit));
		CHECK(audit.code == SG_CONFIGURATION_AUDIT_OMITTED_PORTAL);
		space->portal_count = portal_count;
		{
			uint32_t saved_from = space->portals[0].from_cell;

			space->portals[0].from_cell = space->portals[0].to_cell;
			CHECK(!SG_ConfigurationAudit(&authority, space, &audit));
			CHECK(audit.code == SG_CONFIGURATION_AUDIT_OMITTED_PORTAL);
			space->portals[0].from_cell = saved_from;
		}
	}
	SG_ConfigurationDestroy(space);
	DestroyFixture(&fixture);
}

static void TestProtocolSliverAndOutsideModelBounds(void)
{
	const test_box_t boxes[] = {
		{ { -5000, -5000, -5000 }, { -16, 5000, 5000 },
			SG_HOST_CONTENTS_SOLID },
		{ { 16.05f, -5000, -5000 }, { 100, 5000, 5000 },
			SG_HOST_CONTENTS_SOLID }
	};
	fixture_t fixture = Fixture(boxes, 2, 0, 0);
	sg_configuration_space_t *space = NULL;
	sg_configuration_audit_result_t audit = { 0 };
	uint32_t cell;
	int saw_outside_model = 0;

	CHECK(Build(&fixture, NULL, &space, &audit));
	if (space)
	{
		for (cell = 0; cell < space->cell_count; cell++)
		{
			float x = space->cells[cell].interior_witness.value[0];

			CHECK(!(x > 0.0f && x < 0.05f));
			if (x > fixture.models[0].maxs.value[0])
				saw_outside_model = 1;
		}
		CHECK(saw_outside_model);
		CHECK(space->lattice_solve_calls > 0U);
		CHECK(audit.lattice_solve_calls > 0U);
	}
	SG_ConfigurationDestroy(space);
	DestroyFixture(&fixture);
}

static void TestDisconnectedWallAndFailureAudit(void)
{
	const test_box_t wall = {
		{ -2, -5000, -5000 }, { 2, 5000, 5000 }, SG_HOST_CONTENTS_SOLID
	};
	fixture_t fixture = Fixture(&wall, 1, 0, 0);
	sg_configuration_space_t *space = NULL;
	sg_configuration_audit_result_t audit = { 0 };
	sg_rune_model_identity_t identity = Identity();
	sg_host_collision_authority_t authority;
	sg_host_collision_error_t host_error;
	uint32_t portal_count;

	CHECK(Build(&fixture, NULL, &space, &audit));
	if (space)
	{
		CHECK(space->cell_count >= 4U);
		CHECK(CountPortalStance(space, SG_RUNE_STANCE_STANDING) == 0U);
		CHECK(CountPortalStance(space, SG_RUNE_STANCE_CROUCHING) == 0U);
		CHECK(SG_HostCollisionInit(&authority, &fixture.world, &identity,
			&host_error));
		portal_count = space->portal_count;
		if (portal_count)
		{
			space->portal_count--;
			CHECK(!SG_ConfigurationAudit(&authority, space, &audit));
			CHECK(audit.code == SG_CONFIGURATION_AUDIT_OMITTED_PORTAL);
			space->portal_count = portal_count;
		}
	}
	SG_ConfigurationDestroy(space);
	DestroyFixture(&fixture);
}

static void TestCrouchDoorwayAndWindow(void)
{
	const test_box_t crouch_wall[] = {
		{ { -2, -5000, -5000 }, { 2, -20, 5000 }, SG_HOST_CONTENTS_SOLID },
		{ { -2, 20, -5000 }, { 2, 5000, 5000 }, SG_HOST_CONTENTS_SOLID },
		{ { -2, -20, -5000 }, { 2, 20, -24 }, SG_HOST_CONTENTS_SOLID },
		{ { -2, -20, 12 }, { 2, 20, 5000 }, SG_HOST_CONTENTS_SOLID }
	};
	const test_box_t window_wall[] = {
		{ { -2, -5000, -5000 }, { 2, -20, 5000 }, SG_HOST_CONTENTS_SOLID },
		{ { -2, 20, -5000 }, { 2, 5000, 5000 }, SG_HOST_CONTENTS_SOLID },
		{ { -2, -20, -5000 }, { 2, 20, 0 }, SG_HOST_CONTENTS_SOLID },
		{ { -2, -20, 20 }, { 2, 20, 5000 }, SG_HOST_CONTENTS_SOLID }
	};
	fixture_t fixture = Fixture(crouch_wall, 4, 0, 0);
	sg_configuration_space_t *space = NULL;
	sg_configuration_audit_result_t audit = { 0 };

	CHECK(Build(&fixture, NULL, &space, &audit));
	if (space)
	{
		CHECK(CountPortalStance(space, SG_RUNE_STANCE_STANDING) == 0U);
		CHECK(CountPortalStance(space, SG_RUNE_STANCE_CROUCHING) > 0U);
	}
	SG_ConfigurationDestroy(space);
	DestroyFixture(&fixture);
	fixture = Fixture(window_wall, 4, 0, 0);
	space = NULL;
	CHECK(Build(&fixture, NULL, &space, &audit));
	if (space)
	{
		CHECK(CountPortalStance(space, SG_RUNE_STANCE_STANDING) == 0U);
		CHECK(CountPortalStance(space, SG_RUNE_STANCE_CROUCHING) == 0U);
	}
	SG_ConfigurationDestroy(space);
	DestroyFixture(&fixture);
}

static void TestRampLedgeAndOverflowAtomicity(void)
{
	test_box_t boxes[] = {
		{ { -48, -48, -48 }, { -4, 48, -8 }, SG_HOST_CONTENTS_SOLID },
		{ { 4, -48, -48 }, { 48, 48, 8 }, SG_HOST_CONTENTS_SOLID }
	};
	fixture_t fixture = Fixture(boxes, 2, 0, 0);
	sg_configuration_space_t *space = NULL;
	sg_configuration_audit_result_t audit = { 0 };
	sg_configuration_limits_t limits;
	sg_rune_model_identity_t identity = Identity();
	sg_host_collision_authority_t authority;
	sg_host_collision_error_t host_error;
	sg_configuration_error_t error;
	const float diagonal = 0.70710677f;

	/* Give the first ledge a sloped top plane. */
	SetPlane(&fixture.planes[5], -diagonal, 0, diagonal, 16);
	CHECK(Build(&fixture, NULL, &space, &audit));
	CHECK(space != NULL && space->cell_count > 0U);
	SG_ConfigurationDestroy(space);
	space = NULL;
	SG_ConfigurationDefaultLimits(&limits);
	limits.max_cells = 1;
	CHECK(SG_HostCollisionInit(&authority, &fixture.world, &identity,
		&host_error));
	CHECK(!SG_ConfigurationBuild(&authority, &limits, &space, &error));
	CHECK(error.code == SG_CONFIGURATION_ERROR_OVERFLOW);
	CHECK(space == NULL);
	CHECK(!SG_ConfigurationBuild(&authority, NULL, &space, NULL) || space != NULL);
	SG_ConfigurationDestroy(space);
	space = NULL;
	fixture.models[0].headnode = 99;
	CHECK(!SG_ConfigurationBuild(&authority, NULL, &space, &error));
	CHECK(error.code == SG_CONFIGURATION_ERROR_INVALID_WORLD);
	CHECK(space == NULL);
	fixture.models[0].headnode = 0;
	fixture.planes[0].distance = NAN;
	CHECK(!SG_ConfigurationBuild(&authority, NULL, &space, &error));
	CHECK(error.code == SG_CONFIGURATION_ERROR_INVALID_WORLD);
	CHECK(space == NULL);
	DestroyFixture(&fixture);
}

static void TestEquivalentBspAndScaledBrushPlane(void)
{
	test_box_t wall = {
		{ -48.0f, -16.0f, -48.0f }, { -16.0f, 16.0f, 48.0f },
		SG_HOST_CONTENTS_SOLID
	};
	fixture_t fixture = Fixture(&wall, 1, 0, 0);
	sg_configuration_space_t *space = NULL;
	sg_configuration_audit_result_t audit = { 0 };
	uint32_t portal;
	int saw_scaled_brush_boundary = 0;

	/* After hull expansion this 2x brush plane is x=0, the same geometry as
	 * BSP plane 0 but with different scale, index, and provenance. */
	SetPlane(&fixture.planes[1], 2.0f, 0.0f, 0.0f, -32.0f);
	CHECK(Build(&fixture, NULL, &space, &audit));
	if (space)
	{
		CHECK(space->portal_count > 0U);
		CHECK(audit.proved_portals == space->portal_count);
		for (portal = 0; portal < space->portal_count; portal++)
			saw_scaled_brush_boundary |=
				space->portals[portal].plane.source_kind ==
				SG_CONFIGURATION_PLANE_EXPANDED_BRUSH;
		CHECK(saw_scaled_brush_boundary);
	}
	SG_ConfigurationDestroy(space);
	DestroyFixture(&fixture);
}

static void TestIndexedBrushPruningAndOrder(void)
{
	test_box_t boxes[33];
	fixture_t indexed_fixture;
	fixture_t permuted_fixture;
	sg_configuration_space_t *indexed = NULL;
	sg_configuration_space_t *permuted = NULL;
	sg_configuration_audit_result_t audit = { 0 };
	uint32_t brush, leaf;

	memset(boxes, 0, sizeof(boxes));
	SetVector(boxes[0].mins, -8.0f, -64.0f, -64.0f);
	SetVector(boxes[0].maxs, 8.0f, 64.0f, 64.0f);
	boxes[0].contents = SG_HOST_CONTENTS_SOLID;
	for (brush = 1U; brush < 33U; brush++)
	{
		float x = -3600.0f + (float)brush * 210.0f;

		SetVector(boxes[brush].mins, x, 512.0f, -8.0f);
		SetVector(boxes[brush].maxs, x + 4.0f, 516.0f, 8.0f);
		boxes[brush].contents = SG_HOST_CONTENTS_WATER;
	}
	indexed_fixture = Fixture(boxes, 33U, 0, 0);
	permuted_fixture = Fixture(boxes, 33U, 0, 0);
	for (leaf = 0U; leaf < 2U; leaf++)
		for (brush = 0U; brush < 33U / 2U; brush++)
		{
			uint32_t left = leaf * 33U + brush;
			uint32_t right = leaf * 33U + 32U - brush;
			uint32_t temporary = permuted_fixture.leaf_brushes[left];

			permuted_fixture.leaf_brushes[left] =
				permuted_fixture.leaf_brushes[right];
			permuted_fixture.leaf_brushes[right] = temporary;
		}
	CHECK(Build(&indexed_fixture, NULL, &indexed, &audit));
	CHECK(Build(&permuted_fixture, NULL, &permuted, &audit));
	if (indexed && permuted)
	{
		CHECK(indexed->brush_index_queries > 0U);
		CHECK(indexed->brush_index_minimum_tested_entries <
			indexed_fixture.world.brush_count);
		CHECK(indexed->brush_index_tested_entries <
			indexed->brush_index_queries * indexed_fixture.world.brush_count);
		CHECK(SameConfigurationGeometry(indexed, permuted));
		CHECK(indexed->topology_split_count > 0U);
		CHECK(indexed->topology_carried_portal_count > 0U);
		CheckTopologyIndex(indexed);
		CheckTopologyIndex(permuted);
	}
	SG_ConfigurationDestroy(permuted);
	SG_ConfigurationDestroy(indexed);
	DestroyFixture(&permuted_fixture);
	DestroyFixture(&indexed_fixture);
}

static void TestBrushIndexAdmissionDomain(void)
{
	test_box_t boxes[4];
	uint32_t referenced[2] = { 0U, 1U };
	fixture_t expected_fixture;
	fixture_t irrelevant_fixture;
	sg_configuration_space_t *expected = NULL;
	sg_configuration_space_t *irrelevant = NULL;
	sg_configuration_audit_result_t audit = { 0 };

	memset(boxes, 0, sizeof(boxes));
	SetVector(boxes[0].mins, -8.0f, -64.0f, -64.0f);
	SetVector(boxes[0].maxs, 8.0f, 64.0f, 64.0f);
	boxes[0].contents = SG_HOST_CONTENTS_SOLID;
	SetVector(boxes[1].mins, 24.0f, -8.0f, -8.0f);
	SetVector(boxes[1].maxs, 32.0f, 8.0f, 8.0f);
	boxes[1].contents = SG_HOST_CONTENTS_WATER;
	SetVector(boxes[2].mins, 40.0f, -8.0f, -8.0f);
	SetVector(boxes[2].maxs, 48.0f, 8.0f, 8.0f);
	boxes[2].contents = SG_HOST_CONTENTS_SOLID;
	SetVector(boxes[3].mins, 56.0f, -8.0f, -8.0f);
	SetVector(boxes[3].maxs, 64.0f, 8.0f, 8.0f);
	boxes[3].contents = SG_HOST_CONTENTS_SOLID;
	expected_fixture = Fixture(boxes, 2U, 0, 0);
	irrelevant_fixture = Fixture(boxes, 4U, 0, 0);
	SetReferencedBrushes(&irrelevant_fixture, referenced, 2U);
	irrelevant_fixture.brushes[3].side_count = 0U;
	CHECK(Build(&expected_fixture, NULL, &expected, &audit));
	CHECK(Build(&irrelevant_fixture, NULL, &irrelevant, &audit));
	if (expected && irrelevant)
	{
		CHECK(expected->brush_index_entry_count == 1U);
		CHECK(irrelevant->brush_index_entry_count == 1U);
		CHECK(SameConfigurationGeometry(expected, irrelevant));
	}
	SG_ConfigurationDestroy(irrelevant);
	SG_ConfigurationDestroy(expected);
	DestroyFixture(&irrelevant_fixture);
	DestroyFixture(&expected_fixture);
}

static void TestExactLatticeBoundaries(void)
{
	sg_configuration_lattice_halfspace_t halfspace = {
		{ 1.0f, -0.0f, 0.0f }, 1.0f, 0
	};
	sg_configuration_lattice_stats_t stats = { 0 };
	const float objective[3] = { 1.0f, -0.0f, 0.0f };
	int32_t point[3] = { 0, 0, 0 };
	uint64_t calls, constraints;

	CHECK(SG_ConfigurationLatticeFind(&halfspace, 1, objective, point,
		&stats) == 1);
	CHECK(point[0] == 8);
	halfspace.open = 1;
	CHECK(SG_ConfigurationLatticeFind(&halfspace, 1, objective, point,
		&stats) == 1);
	CHECK(point[0] == 7);
	halfspace.normal[0] = FLT_TRUE_MIN;
	halfspace.distance = FLT_TRUE_MIN;
	halfspace.open = 0;
	CHECK(SG_ConfigurationLatticeFind(&halfspace, 1, objective, point,
		&stats) == 1);
	CHECK(point[0] == 8);
	halfspace.open = 1;
	CHECK(SG_ConfigurationLatticeFind(&halfspace, 1, objective, point,
		&stats) == 1);
	CHECK(point[0] == 7);
	calls = stats.solve_calls;
	halfspace.open = 2;
	CHECK(SG_ConfigurationLatticeFind(&halfspace, 1, objective, point,
		&stats) == -1);
	CHECK(stats.solve_calls == calls);
	halfspace.open = 0;
	halfspace.normal[0] = 0.0f;
	CHECK(SG_ConfigurationLatticeFind(&halfspace, 1, objective, point,
		&stats) == -1);
	halfspace.normal[0] = INFINITY;
	CHECK(SG_ConfigurationLatticeFind(&halfspace, 1, objective, point,
		&stats) == -1);
	{
		sg_configuration_lattice_halfspace_t interval[2] = {
			{ { 1.0f, 0.0f, 0.0f }, 1.0f, 0 },
			{ { -1.0f, 0.0f, 0.0f }, 1.0f, 0 }
		};
		uint8_t clearance[2] = { 1U, 1U };
		int32_t minimum = 0, maximum = 0;
		int positive_margin = 0;

		CHECK(SG_ConfigurationLatticeCoordinateBounds(interval, 2U, 0U,
			&minimum, &maximum, &stats) == 1);
		CHECK(minimum == -8 && maximum == 8);
		interval[0].open = 1;
		interval[1].open = 1;
		CHECK(SG_ConfigurationLatticeCoordinateBounds(interval, 2U, 0U,
			&minimum, &maximum, &stats) == 1);
		CHECK(minimum == -7 && maximum == 7);
		interval[0].normal[0] = 4096.0f;
		interval[0].distance = 4096.0f;
		interval[1].normal[0] = -8192.0f;
		interval[1].distance = 8192.0f;
		CHECK(SG_ConfigurationLatticeCoordinateBounds(interval, 2U, 0U,
			&minimum, &maximum, &stats) == 1);
		CHECK(minimum == -7 && maximum == 7);
		CHECK(SG_ConfigurationLatticeCoordinateBounds(interval, 2U, 3U,
			&minimum, &maximum, &stats) == -1);
		interval[0].normal[0] = 1.0f;
		interval[0].distance = -2.0f;
		interval[0].open = 0;
		interval[1].normal[0] = -1.0f;
		interval[1].distance = 1.0f;
		interval[1].open = 0;
		CHECK(SG_ConfigurationLatticeCoordinateBounds(interval, 2U, 0U,
			&minimum, &maximum, &stats) == 0);

		interval[0].distance = 1.0f;
		interval[1].distance = 1.0f;
		CHECK(SG_ConfigurationLatticeFindMaxClearance(interval, clearance, 2,
			NULL, point, &positive_margin, &stats) == 1);
		CHECK(positive_margin);
		CHECK(point[0] == 0);
	}
	{
		const sg_configuration_lattice_halfspace_t box[6] = {
			{ { 1.0f, 0.0f, 0.0f }, 1.0f, 0 },
			{ { -1.0f, 0.0f, 0.0f }, 1.0f, 0 },
			{ { 0.0f, 1.0f, 0.0f }, 1.0f, 0 },
			{ { 0.0f, -1.0f, 0.0f }, 1.0f, 0 },
			{ { 0.0f, 0.0f, 1.0f }, 1.0f, 0 },
			{ { 0.0f, 0.0f, -1.0f }, 1.0f, 0 }
		};
		const float oblique[3] = {
			0.628799975f, -0.584699988f, -0.658399999f
		};
		const float tied_objective[3] = { 1.0f, 0.0f, 0.0f };
		const uint8_t x_clearance[6] = { 1U, 1U, 0U, 0U, 0U, 0U };
		const int32_t comparator[3] = { 7, -8, -8 };
		float optimum_dot, comparator_dot;
		int positive_margin = 0;

		calls = stats.solve_calls;
		constraints = stats.constraints;
		CHECK(SG_ConfigurationLatticeFind(box, 6U, oblique, point,
			&stats) == 1);
		CHECK(point[0] == 8 && point[1] == -8 && point[2] == -8);
		optimum_dot = oblique[0] * (float)point[0] +
			oblique[1] * (float)point[1] + oblique[2] * (float)point[2];
		comparator_dot = oblique[0] * (float)comparator[0] +
			oblique[1] * (float)comparator[1] +
			oblique[2] * (float)comparator[2];
		CHECK(optimum_dot >= comparator_dot);
		CHECK(stats.solve_calls == calls + 2U);
		CHECK(stats.constraints == constraints + 25U);
		calls = stats.solve_calls;
		constraints = stats.constraints;
		CHECK(SG_ConfigurationLatticeFindMaxClearance(box, x_clearance, 6U,
			oblique, point, &positive_margin, &stats) == 1);
		CHECK(positive_margin);
		CHECK(point[0] == 0 && point[1] == -8 && point[2] == -8);
		CHECK(stats.solve_calls == calls + 3U);
		CHECK(stats.constraints == constraints + 39U);
		CHECK(SG_ConfigurationLatticeFind(box, 6U, tied_objective, point,
			&stats) == 1);
		CHECK(point[0] == 8 && point[1] == 8 && point[2] == 8);
		CHECK(SG_ConfigurationLatticeFindMaxClearance(box, x_clearance, 6U,
			tied_objective, point, &positive_margin, &stats) == 1);
		CHECK(positive_margin);
		CHECK(point[0] == 0 && point[1] == 8 && point[2] == 8);
	}
}

static int HostPointInOnePlaneBrush(const int32_t q[3], const float normal[3],
	float distance)
{
	test_box_t placeholder = {
		{ -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f },
		SG_HOST_CONTENTS_SOLID
	};
	fixture_t fixture = Fixture(&placeholder, 1, SG_HOST_CONTENTS_SOLID,
		SG_HOST_CONTENTS_SOLID);
	sg_rune_model_identity_t identity = Identity();
	sg_host_collision_authority_t authority;
	sg_host_collision_error_t error;
	sg_host_collision_trace_t trace;
	float point[3], zero[3] = { 0.0f, 0.0f, 0.0f };
	uint32_t axis;
	int result;

	SetPlane(&fixture.planes[1], normal[0], normal[1], normal[2], distance);
	fixture.brushes[0].side_count = 1U;
	for (axis = 0; axis < 3U; axis++)
		point[axis] = (float)q[axis] * 0.125f;
	result = SG_HostCollisionInit(&authority, &fixture.world, &identity,
		&error) && SG_HostCollisionTraceModel(&authority, 0, NULL, point, zero,
		zero, point, SG_HOST_MASK_PLAYER_SOLID, &trace) && trace.allsolid;
	DestroyFixture(&fixture);
	return result;
}

static int ExactPlaneAcceptsPinnedPoint(const int32_t q[3],
	const float normal[3], float distance)
{
	sg_configuration_lattice_halfspace_t constraints[7];
	sg_configuration_lattice_stats_t stats = { 0 };
	int32_t point[3];
	uint32_t axis;

	memset(constraints, 0, sizeof(constraints));
	for (axis = 0; axis < 3U; axis++)
	{
		constraints[axis * 2U].normal[axis] = 1.0f;
		constraints[axis * 2U].distance = (float)q[axis] * 0.125f;
		constraints[axis * 2U + 1U].normal[axis] = -1.0f;
		constraints[axis * 2U + 1U].distance =
			-(float)q[axis] * 0.125f;
	}
	SetVector(constraints[6].normal, normal[0], normal[1], normal[2]);
	constraints[6].distance = distance;
	return SG_ConfigurationLatticeFind(constraints, 7, NULL, point, &stats) == 1;
}

static int PinnedPointHasPositiveMargin(const int32_t q[3],
	const float normal[3], float distance)
{
	sg_configuration_lattice_halfspace_t constraints[7];
	sg_configuration_lattice_stats_t stats = { 0 };
	uint8_t clearance[7] = { 1U, 1U, 1U, 1U, 1U, 1U, 1U };
	int32_t point[3];
	int positive_margin = 0;
	uint32_t axis;

	memset(constraints, 0, sizeof(constraints));
	for (axis = 0; axis < 3U; axis++)
	{
		constraints[axis * 2U].normal[axis] = 1.0f;
		constraints[axis * 2U].distance = (float)q[axis] * 0.125f;
		constraints[axis * 2U + 1U].normal[axis] = -1.0f;
		constraints[axis * 2U + 1U].distance =
			-(float)q[axis] * 0.125f;
	}
	SetVector(constraints[6].normal, normal[0], normal[1], normal[2]);
	constraints[6].distance = distance;
	CHECK(SG_ConfigurationLatticeFindMaxClearance(constraints, clearance, 7,
		NULL, point, &positive_margin, &stats) == 1);
	return positive_margin;
}

static void TestZeroVolumeDuplicatePlaneLeaf(void)
{
	fixture_t fixture = Fixture(NULL, 0, 0, 0);
	sg_bsp_plane_t *planes = realloc(fixture.planes, 2U * sizeof(*planes));
	sg_bsp_node_t *nodes = realloc(fixture.nodes, 2U * sizeof(*nodes));
	sg_bsp_leaf_t *leaves = realloc(fixture.leaves, 3U * sizeof(*leaves));
	sg_configuration_space_t *space = NULL;
	sg_configuration_audit_result_t audit = { 0 };

	if (!planes || !nodes || !leaves)
	{
		fputs("duplicate-plane fixture allocation failed\n", stderr);
		exit(2);
	}
	fixture.planes = planes;
	fixture.nodes = nodes;
	fixture.leaves = leaves;
	fixture.world.planes = planes;
	fixture.world.nodes = nodes;
	fixture.world.leaves = leaves;
	fixture.world.plane_count = 2U;
	fixture.world.node_count = 2U;
	fixture.world.leaf_count = 3U;
	fixture.planes[1] = fixture.planes[0];
	memset(&fixture.nodes[1], 0, sizeof(fixture.nodes[1]));
	fixture.nodes[0].children[0] = 1;
	fixture.nodes[0].children[1] = -1;
	fixture.nodes[1].plane = 1U;
	fixture.nodes[1].children[0] = -2;
	fixture.nodes[1].children[1] = -3;
	memset(&fixture.leaves[2], 0, sizeof(fixture.leaves[2]));
	fixture.leaves[2].cluster = 2;
	fixture.leaves[2].area = 3;
	CHECK(Build(&fixture, NULL, &space, &audit));
	if (space)
	{
		CHECK(space->cell_count == 4U);
		CHECK(space->portal_count == 2U);
		CHECK(audit.proved_cells == space->cell_count);
		CHECK(audit.proved_portals == space->portal_count);
	}
	SG_ConfigurationDestroy(space);
	DestroyFixture(&fixture);
}

static void TestHostFloatBoundaryLocalization(void)
{
	const int32_t host_inside_q[3] = { 21839, -31436, 6375 };
	const float host_inside_normal[3] = {
		0.7102766633033752f, 0.8635693788528442f,
		-0.45951032638549805f
	};
	const float host_inside_distance = -1820.601806640625f;
	const int32_t host_outside_q[3] = { -11255, 6727, -31542 };
	const float host_outside_normal[3] = {
		-0.6994550824165344f, -0.5417740345001221f,
		0.07827655225992203f
	};
	const float host_outside_distance = 219.85679626464844f;
	const float selector_solid_normal[3] = {
		0.7102766633033752f, 0.8635693788528442f,
		-0.45951032638549805f
	};
	const float selector_solid_distance = -1820.601806640625f;

	/* The first point is rationally outside but binary32 Dot rounds onto the
	 * solid boundary. The second is rationally inside but host Dot rounds out. */
	CHECK(!ExactPlaneAcceptsPinnedPoint(host_inside_q, host_inside_normal,
		host_inside_distance));
	CHECK(HostPointInOnePlaneBrush(host_inside_q, host_inside_normal,
		host_inside_distance));
	CHECK(ExactPlaneAcceptsPinnedPoint(host_outside_q, host_outside_normal,
		host_outside_distance));
	CHECK(!HostPointInOnePlaneBrush(host_outside_q, host_outside_normal,
		host_outside_distance));
	CHECK(!PinnedPointHasPositiveMargin(host_outside_q, host_outside_normal,
		host_outside_distance));
	{
		sg_configuration_lattice_halfspace_t constraints[7] = {
			{ { 1.0f, 0.0f, 0.0f }, 21841.0f * 0.125f, 0 },
			{ { -1.0f, 0.0f, 0.0f }, -21839.0f * 0.125f, 0 },
			{ { 0.0f, 1.0f, 0.0f }, -31435.0f * 0.125f, 0 },
			{ { 0.0f, -1.0f, 0.0f }, 31437.0f * 0.125f, 0 },
			{ { 0.0f, 0.0f, 1.0f }, 6376.0f * 0.125f, 0 },
			{ { 0.0f, 0.0f, -1.0f }, -6374.0f * 0.125f, 0 },
			{ { -0.7102766633033752f, -0.8635693788528442f,
				0.45951032638549805f }, 1820.601806640625f, 1 }
		};
		const uint8_t clearance[7] = { 1U, 1U, 1U, 1U, 1U, 1U, 0U };
		const float objective[3] = {
			-0.7102766633033752f, -0.8635693788528442f,
			0.45951032638549805f
		};
		const int32_t expected_extreme[3] = { 21839, -31436, 6375 };
		const int32_t expected_clearance[3] = { 21840, -31436, 6375 };
		sg_configuration_lattice_stats_t stats = { 0 };
		int32_t extreme[3], safe[3];
		int positive_margin = 0;

		CHECK(SG_ConfigurationLatticeFind(constraints, 7U, objective, extreme,
			&stats) == 1);
		CHECK(memcmp(extreme, expected_extreme, sizeof(extreme)) == 0);
		CHECK(HostPointInOnePlaneBrush(extreme, selector_solid_normal,
			selector_solid_distance));
		CHECK(SG_ConfigurationLatticeFindMaxClearance(constraints, clearance,
			7U, NULL, safe, &positive_margin, &stats) == 1);
		CHECK(positive_margin);
		CHECK(memcmp(safe, expected_clearance, sizeof(safe)) == 0);
		CHECK(!HostPointInOnePlaneBrush(safe, selector_solid_normal,
			selector_solid_distance));
		{
			test_box_t placeholder = {
				{ -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f },
				SG_HOST_CONTENTS_SOLID
			};
			fixture_t fixture = Fixture(&placeholder, 1,
				SG_HOST_CONTENTS_SOLID, SG_HOST_CONTENTS_SOLID);
			sg_rune_model_identity_t identity = Identity();
			sg_host_collision_authority_t authority;
			sg_host_collision_error_t host_error;
			sg_host_collision_transition_t transition;
			float selected[3], other_endpoint[3];
			uint32_t axis;

			SetPlane(&fixture.planes[1], selector_solid_normal[0],
				selector_solid_normal[1], selector_solid_normal[2],
				selector_solid_distance);
			fixture.brushes[0].side_count = 1U;
			for (axis = 0U; axis < 3U; axis++)
			{
				identity.standing_hull.mins.value[axis] = -FLT_TRUE_MIN;
				identity.standing_hull.maxs.value[axis] = FLT_TRUE_MIN;
				identity.crouching_hull.mins.value[axis] = -FLT_TRUE_MIN;
				identity.crouching_hull.maxs.value[axis] = FLT_TRUE_MIN;
				other_endpoint[axis] = (float)safe[axis] * 0.125f;
			}
			other_endpoint[0] += 0.125f;
			CHECK(SG_HostCollisionInit(&authority, &fixture.world, &identity,
				&host_error));
			CHECK(SG_ConfigurationTestHostValidatedCandidate(&authority,
				SG_RUNE_STANCE_STANDING, extreme, safe, selected) == 1);
			for (axis = 0U; axis < 3U; axis++)
				CHECK(selected[axis] == (float)safe[axis] * 0.125f);
			CHECK(SG_HostCollisionTransition(&authority, NULL, selected,
				other_endpoint, SG_RUNE_STANCE_STANDING, &transition));
			CHECK(transition.clear);
			CHECK(SG_ConfigurationTestHostValidatedCandidate(&authority,
				SG_RUNE_STANCE_STANDING, extreme, extreme, selected) == -1);
			DestroyFixture(&fixture);
		}
	}
	{
		test_box_t placeholder = {
			{ -1.0f, -1.0f, -1.0f }, { 1.0f, 1.0f, 1.0f },
			SG_HOST_CONTENTS_SOLID
		};
		fixture_t fixture = Fixture(&placeholder, 1, SG_HOST_CONTENTS_SOLID,
			SG_HOST_CONTENTS_SOLID);
		sg_rune_model_identity_t identity = Identity();
		sg_host_collision_authority_t authority;
		sg_host_collision_error_t host_error;
		sg_configuration_space_t *space = NULL;
		sg_configuration_audit_result_t audit = { 0 };
		sg_configuration_error_t build_error;
		float origin[3];
		uint32_t cell, portal, axis;
		int saw_rounding_point = 0;

		SetPlane(&fixture.planes[1], host_outside_normal[0],
			host_outside_normal[1], host_outside_normal[2],
			host_outside_distance);
		fixture.brushes[0].side_count = 1U;
		for (axis = 0; axis < 3U; axis++)
		{
			identity.standing_hull.mins.value[axis] = -FLT_TRUE_MIN;
			identity.standing_hull.maxs.value[axis] = FLT_TRUE_MIN;
			identity.crouching_hull.mins.value[axis] = -FLT_TRUE_MIN;
			identity.crouching_hull.maxs.value[axis] = FLT_TRUE_MIN;
			origin[axis] = (float)host_outside_q[axis] * 0.125f;
		}
		CHECK(SG_HostCollisionInit(&authority, &fixture.world, &identity,
			&host_error));
		CHECK(SG_ConfigurationBuild(&authority, NULL, &space, &build_error));
		if (space)
		{
			uint32_t constraint_count = 0U;
			int saw_constraint_extent = 0;

			CHECK(SG_ConfigurationAudit(&authority, space, &audit));
			for (cell = 0; cell < space->cell_count; cell++)
			{
				uint32_t face;
				int cell_has_constraint = 0;

				saw_rounding_point |= space->cells[cell].interior_witness.value[0] ==
					origin[0] && space->cells[cell].interior_witness.value[1] ==
					origin[1] &&
					space->cells[cell].interior_witness.value[2] == origin[2];
				for (face = 0; face < space->cells[cell].face_count; face++)
				{
					const sg_configuration_face_t *value = &space->faces[
						space->cells[cell].first_face + face];

					if (value->kind == SG_CONFIGURATION_FACE_CONSTRAINT_ONLY)
					{
						constraint_count++;
						cell_has_constraint = 1;
						continue;
					}
					{
						uint32_t dominant = 0U;

						if (fabsf(value->plane.normal[1]) >
							fabsf(value->plane.normal[dominant]))
							dominant = 1U;
						if (fabsf(value->plane.normal[2]) >
							fabsf(value->plane.normal[dominant]))
							dominant = 2U;
						CHECK((FaceProjectedArea(space, value, dominant) < 0.0f) ==
							(value->plane.normal[dominant] < 0.0f));
					}
				}
				for (axis = 0; cell_has_constraint && axis < 3U; axis++)
					saw_constraint_extent |=
						space->cells[cell].bounds.maxs.value[axis] -
						space->cells[cell].bounds.mins.value[axis] >= 0.125f;
			}
			CHECK(constraint_count > 0U);
			CHECK(saw_constraint_extent);
			CHECK(!saw_rounding_point);
			for (portal = 0; portal < space->portal_count; portal++)
				CHECK(space->portals[portal].plane.source_kind !=
					SG_CONFIGURATION_PLANE_EXPANDED_BRUSH);
		}
		SG_ConfigurationDestroy(space);
		DestroyFixture(&fixture);
	}
}

static void TestConstraintOnlyPortal(void)
{
	fixture_t fixture = Fixture(NULL, 0U, 0, 0);
	sg_rune_model_identity_t identity = Identity();
	sg_host_collision_authority_t authority;
	sg_host_collision_error_t error;

	CHECK(SG_HostCollisionInit(&authority, &fixture.world, &identity, &error));
	CHECK(SG_ConfigurationTestConstraintPortal(&authority));
	DestroyFixture(&fixture);
}

int main(void)
{
	CHECK(SG_ConfigurationAuditTestTangentPortalGeometry());
	CHECK(SG_ConfigurationTestConstraintFacetWinding());
	CHECK(SG_ConfigurationTestCompleteFinalIncidence());
	CHECK(SG_ConfigurationTestFinalRepresentationBounds());
	CHECK(SG_ConfigurationTestTopologyMappingValidation());
	TestExactLatticeBoundaries();
	TestHostFloatBoundaryLocalization();
	TestConstraintOnlyPortal();
	TestZeroVolumeDuplicatePlaneLeaf();
	TestFullDomainCorridorAndWater();
	TestDisconnectedWallAndFailureAudit();
	TestCrouchDoorwayAndWindow();
	TestRampLedgeAndOverflowAtomicity();
	TestEquivalentBspAndScaledBrushPlane();
	TestIndexedBrushPruningAndOrder();
	TestBrushIndexAdmissionDomain();
	TestProtocolSliverAndOutsideModelBounds();
	if (failures)
	{
		fprintf(stderr, "%d configuration-space checks failed\n", failures);
		return 1;
	}
	puts("configuration-space checks passed");
	return 0;
}
