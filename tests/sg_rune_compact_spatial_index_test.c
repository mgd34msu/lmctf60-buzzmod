#include "slipgate/sg_rune_compact_spatial_index.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARRAY_COUNT(values) (sizeof(values) / sizeof((values)[0]))

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

typedef struct spatial_fixture_s
{
	sg_bsp_world_t world;
	sg_bsp_leaf_t leaves[3];
	uint32_t leaf_brushes[14];
	sg_bsp_brush_t brushes[12];
	sg_bsp_brush_side_t brush_sides[12 * 6];
	sg_bsp_plane_t planes[12 * 6];
} spatial_fixture_t;

typedef struct failing_allocator_s
{
	size_t calls;
	size_t fail_after;
	size_t active;
} failing_allocator_t;

static void *FailingAllocate(void *context, size_t bytes)
{
	failing_allocator_t *state = context;
	void *allocation;

	if (state->calls++ == state->fail_after)
		return NULL;
	allocation = malloc(bytes);
	if (allocation)
		state->active++;
	return allocation;
}

static void FailingRelease(void *context, void *allocation)
{
	failing_allocator_t *state = context;

	if (!allocation)
		return;
	CHECK(state->active > 0U);
	state->active--;
	free(allocation);
}

static void SetLeaf(sg_bsp_leaf_t *leaf, int16_t minimum_x,
	int16_t maximum_x, uint32_t first_brush, uint32_t brush_count)
{
	uint32_t axis;

	memset(leaf, 0, sizeof(*leaf));
	leaf->bounds.mins[0] = minimum_x;
	leaf->bounds.maxs[0] = maximum_x;
	for (axis = 1U; axis < 3U; axis++)
	{
		leaf->bounds.mins[axis] = -10;
		leaf->bounds.maxs[axis] = 10;
	}
	leaf->first_leaf_brush = first_brush;
	leaf->leaf_brush_count = brush_count;
}

static void SetPlane(sg_bsp_plane_t *plane, uint32_t axis, float normal,
	float coordinate)
{
	memset(plane, 0, sizeof(*plane));
	plane->normal.value[axis] = normal;
	plane->distance = normal * coordinate;
}

static void SetBrushBox(spatial_fixture_t *fixture, uint32_t brush,
	float minimum_x, float maximum_x)
{
	static const float minimum[3] = { 0.0f, -10.0f, -10.0f };
	static const float maximum[3] = { 0.0f, 10.0f, 10.0f };
	uint32_t axis;
	uint32_t first = brush * 6U;

	fixture->brushes[brush].first_side = first;
	fixture->brushes[brush].side_count = 6U;
	for (axis = 0U; axis < 3U; axis++)
	{
		float low = axis == 0U ? minimum_x : minimum[axis];
		float high = axis == 0U ? maximum_x : maximum[axis];

		fixture->brush_sides[first + axis * 2U].plane =
			first + axis * 2U;
		fixture->brush_sides[first + axis * 2U + 1U].plane =
			first + axis * 2U + 1U;
		SetPlane(&fixture->planes[first + axis * 2U], axis, -1.0f, low);
		SetPlane(&fixture->planes[first + axis * 2U + 1U], axis, 1.0f,
			high);
	}
}

static void InitFixture(spatial_fixture_t *fixture)
{
	static const uint32_t leaf_brushes[14] = {
		9U, 2U, 2U, 7U,
		7U, 1U, 10U,
		0U, 3U, 4U, 5U, 6U, 8U, 11U
	};

	memset(fixture, 0, sizeof(*fixture));
	SetLeaf(&fixture->leaves[0], -20, 0, 0U, 4U);
	SetLeaf(&fixture->leaves[1], 0, 20, 4U, 3U);
	SetLeaf(&fixture->leaves[2], 100, 120, 7U, 7U);
	memcpy(fixture->leaf_brushes, leaf_brushes, sizeof(leaf_brushes));
	SetBrushBox(fixture, 0U, 100.0f, 120.0f);
	SetBrushBox(fixture, 1U, 0.0f, 20.0f);
	SetBrushBox(fixture, 2U, -20.0f, 0.0f);
	SetBrushBox(fixture, 3U, 100.0f, 120.0f);
	SetBrushBox(fixture, 4U, 100.0f, 120.0f);
	SetBrushBox(fixture, 5U, 100.0f, 120.0f);
	SetBrushBox(fixture, 6U, 100.0f, 120.0f);
	SetBrushBox(fixture, 7U, -20.0f, 20.0f);
	SetBrushBox(fixture, 8U, 100.0f, 120.0f);
	SetBrushBox(fixture, 9U, -20.0f, 0.0f);
	SetBrushBox(fixture, 10U, 0.0f, 20.0f);
	SetBrushBox(fixture, 11U, 100.0f, 120.0f);
	fixture->world.leaves = fixture->leaves;
	fixture->world.leaf_count = (uint32_t)ARRAY_COUNT(fixture->leaves);
	fixture->world.leaf_brushes = fixture->leaf_brushes;
	fixture->world.leaf_brush_count =
		(uint32_t)ARRAY_COUNT(fixture->leaf_brushes);
	fixture->world.brushes = fixture->brushes;
	fixture->world.brush_count = (uint32_t)ARRAY_COUNT(fixture->brushes);
	fixture->world.brush_sides = fixture->brush_sides;
	fixture->world.brush_side_count =
		(uint32_t)ARRAY_COUNT(fixture->brush_sides);
	fixture->world.planes = fixture->planes;
	fixture->world.plane_count = (uint32_t)ARRAY_COUNT(fixture->planes);
}

static sg_rune_compact_spatial_query_t PointQuery(float x)
{
	sg_rune_compact_spatial_query_t query;
	uint32_t axis;

	memset(&query, 0, sizeof(query));
	query.origin_bounds.mins.value[0] = x;
	query.origin_bounds.maxs.value[0] = x;
	for (axis = 1U; axis < 3U; axis++)
	{
		query.origin_bounds.mins.value[axis] = 0.0f;
		query.origin_bounds.maxs.value[axis] = 0.0f;
	}
	return query;
}

static sg_rune_compact_spatial_query_t PointQuery3(float x, float y, float z)
{
	sg_rune_compact_spatial_query_t query = PointQuery(x);

	query.origin_bounds.mins.value[1] = y;
	query.origin_bounds.maxs.value[1] = y;
	query.origin_bounds.mins.value[2] = z;
	query.origin_bounds.maxs.value[2] = z;
	return query;
}

static int QueryContains(const sg_rune_compact_spatial_index_t *index,
	const sg_rune_compact_spatial_query_t *query, uint32_t wanted)
{
	sg_rune_compact_spatial_error_t error;
	uint32_t *brushes = NULL;
	uint32_t count = 0U;
	uint32_t index_in_result;
	int found = 0;

	if (!SG_RuneCompactSpatialIndexQuery(index, query, NULL, 0U, &count,
		&error))
		return 0;
	if (count)
	{
		brushes = malloc((size_t)count * sizeof(*brushes));
		if (!brushes || !SG_RuneCompactSpatialIndexQuery(index, query, brushes,
			count, &count, &error))
		{
			free(brushes);
			return 0;
		}
	}
	for (index_in_result = 0U; index_in_result < count; index_in_result++)
		if (brushes[index_in_result] == wanted)
			found = 1;
	free(brushes);
	return found;
}

static void TestLmctf76Brush67(const char *path)
{
	sg_bsp_world_t *world = NULL;
	sg_bsp_error_t bsp_error;
	sg_rune_compact_spatial_index_t *index = NULL;
	sg_rune_compact_spatial_error_t spatial_error;
	sg_rune_compact_spatial_query_t query =
		PointQuery3(-2160.0f, -448.0f, 150.0f);

	CHECK(SG_BspWorldLoadFile(path, &world, &bsp_error));
	if (!world)
		return;
	CHECK(world->brush_count > 67U);
	CHECK(SG_RuneCompactSpatialIndexBuild(world, NULL, &index,
		&spatial_error));
	if (index)
		CHECK(QueryContains(index, &query, 67U));
	SG_RuneCompactSpatialIndexDestroy(index);
	SG_BspWorldDestroy(world);
}

static void Cross3(const double left[3], const double right[3],
	double result[3])
{
	result[0] = left[1] * right[2] - left[2] * right[1];
	result[1] = left[2] * right[0] - left[0] * right[2];
	result[2] = left[0] * right[1] - left[1] * right[0];
}

static double Dot3(const double left[3], const double right[3])
{
	return left[0] * right[0] + left[1] * right[1] +
		left[2] * right[2];
}

static int IntersectPlanes(const sg_bsp_plane_t *first,
	const sg_bsp_plane_t *second, const sg_bsp_plane_t *third,
	double point[3])
{
	double n0[3], n1[3], n2[3];
	double cross12[3], cross20[3], cross01[3];
	double determinant;
	double scale;
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
	{
		n0[axis] = (double)first->normal.value[axis];
		n1[axis] = (double)second->normal.value[axis];
		n2[axis] = (double)third->normal.value[axis];
	}
	Cross3(n1, n2, cross12);
	Cross3(n2, n0, cross20);
	Cross3(n0, n1, cross01);
	determinant = Dot3(n0, cross12);
	scale = fabs(n0[0] * cross12[0]) + fabs(n0[1] * cross12[1]) +
		fabs(n0[2] * cross12[2]);
	if (!isfinite(determinant) || fabs(determinant) <=
		DBL_EPSILON * fmax(1.0, scale))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
	{
		point[axis] = ((double)first->distance * cross12[axis] +
			(double)second->distance * cross20[axis] +
			(double)third->distance * cross01[axis]) / determinant;
		if (!isfinite(point[axis]))
			return 0;
	}
	return 1;
}

static int PointInsideBrush(const sg_bsp_world_t *world, uint32_t brush,
	const double point[3], double epsilon)
{
	const sg_bsp_brush_t *record = &world->brushes[brush];
	uint32_t side_offset;

	for (side_offset = 0U; side_offset < record->side_count; side_offset++)
	{
		const sg_bsp_brush_side_t *side =
			&world->brush_sides[record->first_side + side_offset];
		const sg_bsp_plane_t *plane = &world->planes[side->plane];
		double normal[3] = {
			(double)plane->normal.value[0],
			(double)plane->normal.value[1],
			(double)plane->normal.value[2]
		};

		if (Dot3(normal, point) > (double)plane->distance + epsilon)
			return 0;
	}
	return record->side_count != 0U;
}

static int BrushInteriorPoint(const sg_bsp_world_t *world, uint32_t brush,
	double point[3])
{
	const sg_bsp_brush_t *record = &world->brushes[brush];
	double sum[3] = { 0.0, 0.0, 0.0 };
	size_t accepted = 0U;
	uint32_t first_side;
	uint32_t second_side;
	uint32_t third_side;
	uint32_t axis;

	for (first_side = 0U; first_side < record->side_count; first_side++)
		for (second_side = first_side + 1U;
			second_side < record->side_count; second_side++)
			for (third_side = second_side + 1U;
				third_side < record->side_count; third_side++)
			{
				const sg_bsp_plane_t *first = &world->planes[
					world->brush_sides[record->first_side + first_side].plane];
				const sg_bsp_plane_t *second = &world->planes[
					world->brush_sides[record->first_side + second_side].plane];
				const sg_bsp_plane_t *third = &world->planes[
					world->brush_sides[record->first_side + third_side].plane];
				double vertex[3];

				if (!IntersectPlanes(first, second, third, vertex) ||
					!PointInsideBrush(world, brush, vertex, 0.00001))
					continue;
				for (axis = 0U; axis < 3U; axis++)
					sum[axis] += vertex[axis];
				accepted++;
			}
	if (!accepted)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		point[axis] = sum[axis] / (double)accepted;
	return PointInsideBrush(world, brush, point, 0.00001);
}

static int SortedContains(const uint32_t *brushes, uint32_t count,
	uint32_t wanted)
{
	uint32_t low = 0U;
	uint32_t high = count;

	while (low < high)
	{
		uint32_t middle = low + (high - low) / 2U;

		if (brushes[middle] < wanted)
			low = middle + 1U;
		else
			high = middle;
	}
	return low < count && brushes[low] == wanted;
}

static void TestRealMapDifferential(const char *path)
{
	sg_bsp_world_t *world = NULL;
	sg_bsp_error_t bsp_error;
	sg_rune_compact_spatial_index_t *index = NULL;
	sg_rune_compact_spatial_error_t spatial_error;
	uint32_t *candidates = NULL;
	uint32_t brush;
	uint32_t query_count = 0U;
	uint64_t exact_matches = 0U;

	CHECK(SG_BspWorldLoadFile(path, &world, &bsp_error));
	if (!world)
		return;
	CHECK(SG_RuneCompactSpatialIndexBuild(world, NULL, &index,
		&spatial_error));
	if (!index)
		goto done;
	if (world->brush_count)
	{
		candidates = malloc((size_t)world->brush_count * sizeof(*candidates));
		CHECK(candidates != NULL);
		if (!candidates)
			goto done;
	}
	for (brush = 0U; brush < world->brush_count; brush++)
	{
		double point[3];
		double rounded_point[3];
		sg_rune_compact_spatial_query_t query;
		uint32_t candidate_count = 0U;
		uint32_t expected;
		uint32_t expected_count = 0U;
		uint32_t offset;
		int has_point = BrushInteriorPoint(world, brush, point);

		CHECK(has_point);
		if (!has_point)
			continue;
		query = PointQuery3((float)point[0], (float)point[1],
			(float)point[2]);
		rounded_point[0] = (double)query.origin_bounds.mins.value[0];
		rounded_point[1] = (double)query.origin_bounds.mins.value[1];
		rounded_point[2] = (double)query.origin_bounds.mins.value[2];
		CHECK(SG_RuneCompactSpatialIndexQuery(index, &query, candidates,
			world->brush_count, &candidate_count, &spatial_error));
		for (offset = 1U; offset < candidate_count; offset++)
			CHECK(candidates[offset - 1U] < candidates[offset]);
		for (expected = 0U; expected < world->brush_count; expected++)
			if (PointInsideBrush(world, expected, rounded_point, 0.0))
			{
				CHECK(SortedContains(candidates, candidate_count, expected));
				expected_count++;
				exact_matches++;
			}
		CHECK(candidate_count == expected_count);
		query_count++;
	}
	CHECK(query_count == world->brush_count);
	fprintf(stdout, "%s: %u brush points, %llu exact memberships covered\n",
		path, (unsigned)query_count, (unsigned long long)exact_matches);

done:
	free(candidates);
	SG_RuneCompactSpatialIndexDestroy(index);
	SG_BspWorldDestroy(world);
}

static void CheckQuery(const sg_rune_compact_spatial_index_t *index,
	const sg_rune_compact_spatial_query_t *query, const uint32_t *expected,
	uint32_t expected_count)
{
	sg_rune_compact_spatial_error_t error;
	uint32_t actual[16];
	uint32_t required = UINT32_MAX;
	uint32_t count = UINT32_MAX;

	memset(actual, 0xff, sizeof(actual));
	CHECK(SG_RuneCompactSpatialIndexQuery(index, query, NULL, 0U,
		&required, &error));
	CHECK(error.code == SG_RUNE_COMPACT_SPATIAL_ERROR_NONE);
	CHECK(required == expected_count);
	CHECK(SG_RuneCompactSpatialIndexQuery(index, query, actual,
		(uint32_t)ARRAY_COUNT(actual), &count, &error));
	CHECK(error.code == SG_RUNE_COMPACT_SPATIAL_ERROR_NONE);
	CHECK(count == expected_count);
	if (count == expected_count)
		CHECK(memcmp(actual, expected,
			(size_t)expected_count * sizeof(*expected)) == 0);
}

static void TestLeafQueries(void)
{
	static const uint32_t first_leaf[] = { 2U, 7U, 9U };
	static const uint32_t adjacent_leaves[] = { 1U, 2U, 7U, 9U, 10U };
	static const uint32_t boundary_leaves[] = { 1U, 2U, 7U, 9U, 10U };
	spatial_fixture_t fixture;
	sg_rune_compact_spatial_index_t *index = NULL;
	sg_rune_compact_spatial_error_t error;
	sg_rune_compact_spatial_query_t query;
	uint32_t no_brushes[1] = { UINT32_MAX };

	InitFixture(&fixture);
	CHECK(SG_RuneCompactSpatialIndexBuild(&fixture.world, NULL, &index,
		&error));
	CHECK(error.code == SG_RUNE_COMPACT_SPATIAL_ERROR_NONE);
	query = PointQuery(-5.0f);
	CheckQuery(index, &query, first_leaf, (uint32_t)ARRAY_COUNT(first_leaf));

	query = PointQuery(-2.0f);
	query.hull.mins.value[0] = -1.0f;
	query.hull.maxs.value[0] = 3.0f;
	CheckQuery(index, &query, adjacent_leaves,
		(uint32_t)ARRAY_COUNT(adjacent_leaves));

	query = PointQuery(0.0f);
	CheckQuery(index, &query, boundary_leaves,
		(uint32_t)ARRAY_COUNT(boundary_leaves));

	query = PointQuery(-20.00005f);
	CheckQuery(index, &query, no_brushes, 0U);
	query = PointQuery(-20.001f);
	CheckQuery(index, &query, no_brushes, 0U);
	query = PointQuery(50.0f);
	CheckQuery(index, &query, no_brushes, 0U);
	{
		sg_rune_compact_spatial_query_statistics_t statistics;
		uint32_t brushes[16];
		uint32_t count = 0U;

		query = PointQuery(-5.0f);
		CHECK(SG_RuneCompactSpatialIndexQueryWithStatistics(index, &query,
			brushes, (uint32_t)ARRAY_COUNT(brushes), &count, &statistics,
			&error));
		CHECK(count == 3U);
		CHECK(statistics.tested_entries < fixture.world.brush_count);
	}
	SG_RuneCompactSpatialIndexDestroy(index);
}

static void TestPlaneBoundsAndOverflow(void)
{
	static const uint32_t negative_brushes[] = { 2U, 7U, 9U };
	static const uint32_t no_brushes[] = { UINT32_MAX };
	static const float oblique = 0.577350269f;
	spatial_fixture_t fixture;
	sg_rune_compact_spatial_index_t *index = NULL;
	sg_rune_compact_spatial_error_t error;
	sg_rune_compact_spatial_query_t query;
	uint32_t first;

	InitFixture(&fixture);
	fixture.leaves[0].bounds.mins[0] = -10;
	first = fixture.brushes[11].first_side;
	fixture.brushes[11].side_count = 4U;
	memset(&fixture.planes[first], 0, 4U * sizeof(fixture.planes[first]));
	fixture.planes[first + 0U].normal.value[0] = oblique;
	fixture.planes[first + 0U].normal.value[1] = oblique;
	fixture.planes[first + 0U].normal.value[2] = oblique;
	fixture.planes[first + 1U].normal.value[0] = -oblique;
	fixture.planes[first + 1U].normal.value[1] = -oblique;
	fixture.planes[first + 1U].normal.value[2] = oblique;
	fixture.planes[first + 2U].normal.value[0] = -oblique;
	fixture.planes[first + 2U].normal.value[1] = oblique;
	fixture.planes[first + 2U].normal.value[2] = -oblique;
	fixture.planes[first + 3U].normal.value[0] = oblique;
	fixture.planes[first + 3U].normal.value[1] = -oblique;
	fixture.planes[first + 3U].normal.value[2] = -oblique;
	fixture.planes[first + 0U].distance = oblique;
	fixture.planes[first + 1U].distance = oblique;
	fixture.planes[first + 2U].distance = oblique;
	fixture.planes[first + 3U].distance = oblique;
	CHECK(SG_RuneCompactSpatialIndexBuild(&fixture.world, NULL, &index,
		&error));
	query = PointQuery(-15.0f);
	CheckQuery(index, &query, negative_brushes,
		(uint32_t)ARRAY_COUNT(negative_brushes));
	query = PointQuery(50.0f);
	CheckQuery(index, &query, no_brushes, 0U);
	query = PointQuery(0.0f);
	CHECK(QueryContains(index, &query, 11U));
	SG_RuneCompactSpatialIndexDestroy(index);
	index = NULL;

	InitFixture(&fixture);
	fixture.world.leaf_count = 0U;
	fixture.world.leaf_brush_count = 0U;
	CHECK(SG_RuneCompactSpatialIndexBuild(&fixture.world, NULL, &index,
		&error));
	query = PointQuery(-5.0f);
	CHECK(QueryContains(index, &query, 2U));
	CHECK(QueryContains(index, &query, 7U));
	CHECK(QueryContains(index, &query, 9U));
	SG_RuneCompactSpatialIndexDestroy(index);
}

static void SetTopologyCell(sg_rune_compact_spatial_cell_input_t *cell,
	uint32_t first_face, uint32_t face_count, float minimum_x, float maximum_x)
{
	uint32_t axis;

	memset(cell, 0, sizeof(*cell));
	cell->first_face = first_face;
	cell->face_count = face_count;
	cell->bounds.mins.value[0] = minimum_x;
	cell->bounds.maxs.value[0] = maximum_x;
	for (axis = 1U; axis < 3U; axis++)
	{
		cell->bounds.mins.value[axis] = -1.0f;
		cell->bounds.maxs.value[axis] = 1.0f;
	}
}

static void SetTopologyFaces(sg_rune_compact_spatial_face_input_t *faces,
	const sg_rune_compact_spatial_cell_input_t *cell, uint32_t source_base)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
	{
		sg_rune_compact_spatial_face_input_t *upper = &faces[axis * 2U];
		sg_rune_compact_spatial_face_input_t *lower = upper + 1;

		memset(upper, 0, sizeof(*upper));
		memset(lower, 0, sizeof(*lower));
		upper->bounds = cell->bounds;
		lower->bounds = cell->bounds;
		upper->normal[axis] = 1.0f;
		upper->distance = cell->bounds.maxs.value[axis];
		upper->source_boundary = source_base + axis * 2U;
		upper->ownership = SG_RUNE_BOUNDARY_CLOSED;
		lower->normal[axis] = -1.0f;
		lower->distance = -cell->bounds.mins.value[axis];
		lower->source_boundary = source_base + axis * 2U + 1U;
		lower->ownership = SG_RUNE_BOUNDARY_CLOSED;
	}
}

static void CheckTopologyCell(const sg_rune_compact_spatial_index_t *index,
	float x, uint32_t wanted)
{
	sg_rune_compact_spatial_error_t error;
	sg_rune_vec3_t point = { { x, 0.0f, 0.0f } };
	uint32_t actual[2];
	uint32_t count = 0U;

	CHECK(SG_RuneCompactSpatialIndexQueryCells(index, &point, actual,
		(uint32_t)ARRAY_COUNT(actual), &count, &error));
	CHECK(error.code == SG_RUNE_COMPACT_SPATIAL_ERROR_NONE);
	CHECK(count == 1U);
	if (count == 1U)
		CHECK(actual[0] == wanted);
}

static void TestTopologyIndex(void)
{
	sg_rune_compact_spatial_cell_input_t cells[5];
	sg_rune_compact_spatial_face_input_t faces[24];
	sg_rune_compact_spatial_portal_input_t portals[5];
	sg_rune_compact_spatial_carried_portal_t carried[2] = {
		{ 0U, 2U }, { 1U, 3U }
	};
	sg_rune_compact_spatial_split_input_t split;
	sg_rune_compact_spatial_topology_input_t topology;
	sg_rune_compact_spatial_index_t *first = NULL;
	sg_rune_compact_spatial_index_t *second = NULL;
	sg_rune_compact_spatial_index_t *invalid = NULL;
	sg_rune_compact_spatial_error_t error;
	sg_rune_compact_spatial_counts_t counts;
	const uint32_t *boundary_faces = NULL;
	const uint32_t *boundary_portals = NULL;
	sg_rune_compact_spatial_span_t face_span;
	sg_rune_compact_spatial_span_t portal_span;

	SetTopologyCell(&cells[0], 0U, 0U, -2.0f, 2.0f);
	SetTopologyCell(&cells[1], 0U, 6U, -2.0f, 0.0f);
	SetTopologyCell(&cells[2], 6U, 6U, 0.0f, 2.0f);
	SetTopologyCell(&cells[3], 12U, 6U, 2.0f, 4.0f);
	SetTopologyCell(&cells[4], 18U, 6U, -4.0f, -2.0f);
	SetTopologyFaces(&faces[0], &cells[1], 100U);
	SetTopologyFaces(&faces[6], &cells[2], 110U);
	SetTopologyFaces(&faces[12], &cells[3], 120U);
	SetTopologyFaces(&faces[18], &cells[4], 130U);
	/* Each shared plane is owned by exactly one closed cell. */
	faces[0].ownership = SG_RUNE_BOUNDARY_OPEN;
	faces[7].ownership = SG_RUNE_BOUNDARY_CLOSED;
	faces[6].ownership = SG_RUNE_BOUNDARY_OPEN;
	faces[13].ownership = SG_RUNE_BOUNDARY_CLOSED;
	faces[1].ownership = SG_RUNE_BOUNDARY_OPEN;
	faces[18].ownership = SG_RUNE_BOUNDARY_CLOSED;
	memset(portals, 0, sizeof(portals));
	portals[0].source_boundary = 20U;
	portals[0].negative_cell = 0U;
	portals[0].positive_cell = 3U;
	portals[1].source_boundary = 10U;
	portals[1].negative_cell = 4U;
	portals[1].positive_cell = 0U;
	portals[2].source_boundary = 20U;
	portals[2].negative_cell = 2U;
	portals[2].positive_cell = 3U;
	portals[3].source_boundary = 10U;
	portals[3].negative_cell = 4U;
	portals[3].positive_cell = 1U;
	portals[4].source_boundary = 30U;
	portals[4].negative_cell = 1U;
	portals[4].positive_cell = 2U;
	memset(&split, 0, sizeof(split));
	split.parent_cell = 0U;
	split.negative_cell = 1U;
	split.positive_cell = 2U;
	split.source_boundary = 30U;
	split.interior_portal = 4U;
	split.carried_portal_count = (uint32_t)ARRAY_COUNT(carried);
	memset(&topology, 0, sizeof(topology));
	topology.cells = cells;
	topology.cell_count = (uint32_t)ARRAY_COUNT(cells);
	topology.faces = faces;
	topology.face_count = (uint32_t)ARRAY_COUNT(faces);
	topology.portals = portals;
	topology.portal_count = (uint32_t)ARRAY_COUNT(portals);
	topology.splits = &split;
	topology.split_count = 1U;
	topology.carried_portals = carried;
	topology.carried_portal_count = (uint32_t)ARRAY_COUNT(carried);
	CHECK(SG_RuneCompactSpatialIndexBuildTopology(&topology, NULL, &first,
		&error));
	CHECK(SG_RuneCompactSpatialIndexBuildTopology(&topology, NULL, &second,
		&error));
	CHECK(SG_RuneCompactSpatialIndexCounts(first, &counts, &error));
	CHECK(counts.brush_count == 0U);
	CHECK(counts.cell_count == 4U);
	CHECK(counts.face_count == 24U);
	CHECK(counts.portal_count == 3U);
	CHECK(counts.source_boundary_count == 27U);
	CheckTopologyCell(first, -3.0f, 4U);
	CheckTopologyCell(first, -2.0f, 4U);
	CheckTopologyCell(first, -1.0f, 1U);
	CheckTopologyCell(first, 0.0f, 2U);
	CheckTopologyCell(first, 2.0f, 3U);
	CHECK(SG_RuneCompactSpatialIndexBoundaryRead(first, 20U,
		&boundary_faces, &face_span, &boundary_portals, &portal_span, &error));
	CHECK(face_span.count == 0U);
	CHECK(boundary_faces == NULL);
	CHECK(portal_span.count == 1U);
	CHECK(boundary_portals != NULL && boundary_portals[0] == 2U);
	CHECK(SG_RuneCompactSpatialIndexBoundaryRead(first, 30U,
		&boundary_faces, &face_span, &boundary_portals, &portal_span, &error));
	CHECK(portal_span.count == 1U);
	CHECK(boundary_portals != NULL && boundary_portals[0] == 4U);
	CHECK(!SG_RuneCompactSpatialIndexBoundaryRead(first, 999U,
		&boundary_faces, &face_span, &boundary_portals, &portal_span, &error));
	CHECK(error.code == SG_RUNE_COMPACT_SPATIAL_ERROR_NOT_FOUND);
	split.carried_portal_count = 1U;
	CHECK(!SG_RuneCompactSpatialIndexBuildTopology(&topology, NULL, &invalid,
		&error));
	CHECK(error.code == SG_RUNE_COMPACT_SPATIAL_ERROR_INVALID_TOPOLOGY);
	CHECK(invalid == NULL);
	CheckTopologyCell(second, 0.0f, 2U);
	SG_RuneCompactSpatialIndexDestroy(second);
	SG_RuneCompactSpatialIndexDestroy(first);
}

static void TestCapacityAndArguments(void)
{
	spatial_fixture_t fixture;
	sg_rune_compact_spatial_index_t *index = NULL;
	sg_rune_compact_spatial_index_t *occupied = (void *)&fixture;
	sg_rune_compact_spatial_error_t error;
	sg_rune_compact_spatial_query_t query;
	uint32_t brushes[2];
	uint32_t count;

	InitFixture(&fixture);
	CHECK(!SG_RuneCompactSpatialIndexBuild(&fixture.world, NULL, &occupied,
		&error));
	CHECK(error.code == SG_RUNE_COMPACT_SPATIAL_ERROR_INVALID_ARGUMENT);
	CHECK(SG_RuneCompactSpatialIndexBuild(&fixture.world, NULL, &index,
		&error));
	query = PointQuery(-5.0f);
	CHECK(!SG_RuneCompactSpatialIndexQuery(index, &query, brushes,
		(uint32_t)ARRAY_COUNT(brushes), &count, &error));
	CHECK(count == 3U);
	CHECK(error.code ==
		SG_RUNE_COMPACT_SPATIAL_ERROR_INSUFFICIENT_CAPACITY);
	CHECK(error.required_capacity == 3U);
	CHECK(!SG_RuneCompactSpatialIndexQuery(index, &query, NULL, 1U,
		&count, &error));
	CHECK(error.code == SG_RUNE_COMPACT_SPATIAL_ERROR_INVALID_ARGUMENT);
	query.origin_bounds.mins.value[0] = INFINITY;
	CHECK(!SG_RuneCompactSpatialIndexQuery(index, &query, NULL, 0U,
		&count, &error));
	CHECK(error.code == SG_RUNE_COMPACT_SPATIAL_ERROR_NONFINITE_BOUNDS);
	query = PointQuery(1.0f);
	query.origin_bounds.maxs.value[0] = 0.0f;
	CHECK(!SG_RuneCompactSpatialIndexQuery(index, &query, NULL, 0U,
		&count, &error));
	CHECK(error.code == SG_RUNE_COMPACT_SPATIAL_ERROR_INVALID_ARGUMENT);
	SG_RuneCompactSpatialIndexDestroy(index);
}

static void TestWorldFailures(void)
{
	spatial_fixture_t fixture;
	sg_rune_compact_spatial_index_t *index = NULL;
	sg_rune_compact_spatial_error_t error;
	sg_bsp_world_t overflow_world;
	sg_bsp_brush_t dummy_brush;

	InitFixture(&fixture);
	fixture.leaves[0].leaf_brush_count = fixture.world.leaf_brush_count + 1U;
	CHECK(!SG_RuneCompactSpatialIndexBuild(&fixture.world, NULL, &index,
		&error));
	CHECK(error.code == SG_RUNE_COMPACT_SPATIAL_ERROR_INVALID_WORLD);
	CHECK(error.record == 0U);

	memset(&overflow_world, 0, sizeof(overflow_world));
	overflow_world.brush_count = SG_RUNE_COMPACT_SPATIAL_MAX_BRUSHES + 1U;
	overflow_world.brushes = &dummy_brush;
	CHECK(!SG_RuneCompactSpatialIndexBuild(&overflow_world, NULL, &index,
		&error));
	CHECK(error.code == SG_RUNE_COMPACT_SPATIAL_ERROR_OVERFLOW);

	InitFixture(&fixture);
	fixture.brushes[0].first_side = fixture.world.brush_side_count;
	fixture.brushes[0].side_count = 1U;
	CHECK(!SG_RuneCompactSpatialIndexBuild(&fixture.world, NULL, &index,
		&error));
	CHECK(error.code == SG_RUNE_COMPACT_SPATIAL_ERROR_INVALID_WORLD);
	CHECK(error.record == 0U);

	InitFixture(&fixture);
	fixture.brush_sides[0].plane = fixture.world.plane_count;
	CHECK(!SG_RuneCompactSpatialIndexBuild(&fixture.world, NULL, &index,
		&error));
	CHECK(error.code == SG_RUNE_COMPACT_SPATIAL_ERROR_INVALID_WORLD);
	CHECK(error.record == 0U);

	InitFixture(&fixture);
	fixture.planes[0].distance = INFINITY;
	CHECK(!SG_RuneCompactSpatialIndexBuild(&fixture.world, NULL, &index,
		&error));
	CHECK(error.code == SG_RUNE_COMPACT_SPATIAL_ERROR_INVALID_WORLD);
	CHECK(error.record == 0U);
}

static void TestAllocationFailures(void)
{
	spatial_fixture_t fixture;
	size_t failure;
	int reached_success = 0;

	InitFixture(&fixture);
	for (failure = 0U; ; failure++)
	{
		failing_allocator_t state = { 0U, failure, 0U };
		sg_rune_compact_spatial_allocator_t allocator = {
			&state, FailingAllocate, FailingRelease
		};
		sg_rune_compact_spatial_index_t *index = NULL;
		sg_rune_compact_spatial_error_t error;

		if (SG_RuneCompactSpatialIndexBuild(&fixture.world, &allocator,
			&index, &error))
		{
			reached_success = 1;
			SG_RuneCompactSpatialIndexDestroy(index);
			CHECK(state.active == 0U);
			break;
		}
		CHECK(index == NULL);
		CHECK(error.code == SG_RUNE_COMPACT_SPATIAL_ERROR_OUT_OF_MEMORY);
		CHECK(state.active == 0U);
	}
	CHECK(reached_success);
}

static void TestEmptyAndDeterministic(void)
{
	static const uint32_t first_leaf[] = { 2U, 7U, 9U };
	static const float points[] = {
		-20.0f, -5.0f, 0.0f, 10.0f, 50.0f, 100.0f, 120.0f
	};
	spatial_fixture_t fixture;
	sg_bsp_world_t empty_world;
	sg_rune_compact_spatial_index_t *first = NULL;
	sg_rune_compact_spatial_index_t *second = NULL;
	sg_rune_compact_spatial_index_t *empty = NULL;
	sg_rune_compact_spatial_error_t error;
	uint32_t pass;

	InitFixture(&fixture);
	memset(&empty_world, 0, sizeof(empty_world));
	CHECK(SG_RuneCompactSpatialIndexBuild(&fixture.world, NULL, &first,
		&error));
	CHECK(SG_RuneCompactSpatialIndexBuild(&fixture.world, NULL, &second,
		&error));
	CHECK(SG_RuneCompactSpatialIndexBuild(&empty_world, NULL, &empty,
		&error));
	memset(fixture.leaves, 0, sizeof(fixture.leaves));
	memset(fixture.leaf_brushes, 0, sizeof(fixture.leaf_brushes));
	memset(fixture.brushes, 0, sizeof(fixture.brushes));
	memset(fixture.brush_sides, 0, sizeof(fixture.brush_sides));
	memset(fixture.planes, 0, sizeof(fixture.planes));
	{
		sg_rune_compact_spatial_query_t query = PointQuery(-5.0f);

		CheckQuery(first, &query, first_leaf,
			(uint32_t)ARRAY_COUNT(first_leaf));
	}
	for (pass = 0U; pass < 16U; pass++)
	{
		uint32_t point;

		for (point = 0U; point < (uint32_t)ARRAY_COUNT(points); point++)
		{
			sg_rune_compact_spatial_query_t query = PointQuery(points[point]);
			uint32_t first_brushes[16];
			uint32_t second_brushes[16];
			uint32_t first_count;
			uint32_t second_count;

			CHECK(SG_RuneCompactSpatialIndexQuery(first, &query,
				first_brushes, (uint32_t)ARRAY_COUNT(first_brushes),
				&first_count, &error));
			CHECK(SG_RuneCompactSpatialIndexQuery(second, &query,
				second_brushes, (uint32_t)ARRAY_COUNT(second_brushes),
				&second_count, &error));
			CHECK(first_count == second_count);
			if (first_count == second_count)
				CHECK(memcmp(first_brushes, second_brushes,
					(size_t)first_count * sizeof(*first_brushes)) == 0);
		}
	}
	{
		sg_rune_compact_spatial_query_t query = PointQuery(0.0f);
		uint32_t count = UINT32_MAX;

		CHECK(SG_RuneCompactSpatialIndexQuery(empty, &query, NULL, 0U,
			&count, &error));
		CHECK(count == 0U);
	}
	SG_RuneCompactSpatialIndexDestroy(empty);
	SG_RuneCompactSpatialIndexDestroy(second);
	SG_RuneCompactSpatialIndexDestroy(first);
}

static void TestErrorStrings(void)
{
	uint32_t code;

	for (code = 0U;
		code < (uint32_t)SG_RUNE_COMPACT_SPATIAL_ERROR_CODE_COUNT; code++)
		CHECK(strcmp(SG_RuneCompactSpatialIndexErrorString(
			(sg_rune_compact_spatial_error_code_t)code),
			"unknown compact spatial index error") != 0);
	CHECK(strcmp(SG_RuneCompactSpatialIndexErrorString(
		(sg_rune_compact_spatial_error_code_t)UINT32_MAX),
		"unknown compact spatial index error") == 0);
}

int main(int argc, char **argv)
{
	int argument;

	TestLeafQueries();
	TestPlaneBoundsAndOverflow();
	TestTopologyIndex();
	TestCapacityAndArguments();
	TestWorldFailures();
	TestAllocationFailures();
	TestEmptyAndDeterministic();
	TestErrorStrings();
	for (argument = 1; argument < argc; argument++)
	{
		if (strstr(argv[argument], "lmctf76.bsp") != NULL)
			TestLmctf76Brush67(argv[argument]);
		TestRealMapDifferential(argv[argument]);
	}
	if (failures)
	{
		fprintf(stderr, "%d compact spatial index checks failed\n", failures);
		return 1;
	}
	printf("compact spatial index checks passed\n");
	return 0;
}
