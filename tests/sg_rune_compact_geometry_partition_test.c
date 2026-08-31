#include "slipgate/sg_rune_compact_geometry_partition.h"

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

typedef struct failing_allocator_s
{
	size_t calls;
	size_t fail_at;
	size_t active;
} failing_allocator_t;

static void *FailingAllocate(void *context, size_t bytes)
{
	failing_allocator_t *state = context;
	void *allocation;

	if (state->calls++ == state->fail_at)
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

static sg_rune_bounds_t Bounds(float minimum_x, float minimum_y,
	float minimum_z, float maximum_x, float maximum_y, float maximum_z)
{
	sg_rune_bounds_t result = { { { minimum_x, minimum_y, minimum_z } },
		{ { maximum_x, maximum_y, maximum_z } } };

	return result;
}

static sg_rune_compact_partition_halfspace_t AxisHalfspace(uint32_t axis,
	float normal, float coordinate, uint32_t source)
{
	sg_rune_compact_partition_halfspace_t result;

	memset(&result, 0, sizeof(result));
	result.plane.normal[axis] = normal;
	result.plane.distance = normal * coordinate;
	result.plane.source_kind = SG_CONFIGURATION_PLANE_DOMAIN;
	result.plane.source_index = source;
	result.source_plane_index = source;
	result.contributor = 100U + source;
	return result;
}

static void Cube(sg_rune_compact_partition_halfspace_t planes[6],
	float minimum_x, float minimum_y, float minimum_z, float maximum_x,
	float maximum_y, float maximum_z, uint32_t source_base)
{
	planes[0] = AxisHalfspace(0U, -1.0f, minimum_x, source_base);
	planes[1] = AxisHalfspace(0U, 1.0f, maximum_x, source_base + 1U);
	planes[2] = AxisHalfspace(1U, -1.0f, minimum_y, source_base + 2U);
	planes[3] = AxisHalfspace(1U, 1.0f, maximum_y, source_base + 3U);
	planes[4] = AxisHalfspace(2U, -1.0f, minimum_z, source_base + 4U);
	planes[5] = AxisHalfspace(2U, 1.0f, maximum_z, source_base + 5U);
}

static sg_rune_compact_partition_polygon_t Rectangle(
	sg_rune_vec3_t vertices[4], float minimum_x, float minimum_y,
	float maximum_x, float maximum_y, uint32_t source)
{
	sg_rune_compact_partition_polygon_t result;

	vertices[0].value[0] = minimum_x;
	vertices[0].value[1] = minimum_y;
	vertices[0].value[2] = 0.0f;
	vertices[1].value[0] = maximum_x;
	vertices[1].value[1] = minimum_y;
	vertices[1].value[2] = 0.0f;
	vertices[2].value[0] = maximum_x;
	vertices[2].value[1] = maximum_y;
	vertices[2].value[2] = 0.0f;
	vertices[3].value[0] = minimum_x;
	vertices[3].value[1] = maximum_y;
	vertices[3].value[2] = 0.0f;
	memset(&result, 0, sizeof(result));
	result.plane.normal[2] = 1.0f;
	result.source_plane_index = source;
	result.contributor = source + 10U;
	result.vertices = vertices;
	result.vertex_count = 4U;
	return result;
}

static double PolygonArea(const sg_rune_compact_partition_polygon_t *polygon)
{
	double twice_area = 0.0;
	uint32_t index;

	for (index = 0U; index < polygon->vertex_count; index++)
	{
		const float *a = polygon->vertices[index].value;
		const float *b = polygon->vertices[
			(index + 1U) % polygon->vertex_count].value;

		twice_area += (double)a[0] * (double)b[1] -
			(double)a[1] * (double)b[0];
	}
	return fabs(twice_area) * 0.5;
}

static int ContainsPointXY(
	const sg_rune_compact_partition_polygon_t *polygon, double x, double y)
{
	uint32_t edge;

	if (polygon->vertex_count < 3U)
		return 0;
	for (edge = 0U; edge < polygon->vertex_count; edge++)
	{
		const float *start = polygon->vertices[edge].value;
		const float *end = polygon->vertices[
			(edge + 1U) % polygon->vertex_count].value;
		double cross = ((double)end[0] - (double)start[0]) *
			(y - (double)start[1]) -
			((double)end[1] - (double)start[1]) *
			(x - (double)start[0]);

		if (cross < -0.000001)
			return 0;
	}
	return 1;
}

static int SamePolyhedron(
	const sg_rune_compact_partition_polyhedron_t *left,
	const sg_rune_compact_partition_polyhedron_t *right)
{
	uint32_t face;

	if (left->face_count != right->face_count || left->empty != right->empty ||
		memcmp(&left->bounds, &right->bounds, sizeof(left->bounds)) != 0)
		return 0;
	for (face = 0U; face < left->face_count; face++)
	{
		const sg_rune_compact_partition_polygon_t *a = &left->faces[face];
		const sg_rune_compact_partition_polygon_t *b = &right->faces[face];

		if (memcmp(&a->plane, &b->plane, sizeof(a->plane)) != 0 ||
			a->source_plane_index != b->source_plane_index ||
			a->contributor != b->contributor || a->open != b->open ||
			a->vertex_count != b->vertex_count ||
			memcmp(a->vertices, b->vertices,
				(size_t)a->vertex_count * sizeof(*a->vertices)) != 0)
			return 0;
	}
	return 1;
}

static void TestCubeFacesAndRedundancy(void)
{
	sg_rune_compact_partition_halfspace_t planes[8];
	sg_rune_compact_partition_halfspace_t reordered[8];
	sg_rune_compact_partition_polyhedron_t result;
	sg_rune_compact_partition_polyhedron_t repeated;
	sg_rune_compact_partition_error_t error;
	sg_rune_bounds_t bounds = Bounds(-1.0f, -1.0f, -1.0f,
		1.0f, 1.0f, 1.0f);
	uint32_t face;

	Cube(planes, -1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f, 0U);
	planes[0].open = 1U;
	planes[6] = AxisHalfspace(0U, 1.0f, 2.0f, 6U);
	planes[7] = planes[1];
	planes[7].source_plane_index = 7U;
	planes[7].open = 1U;
	memset(&result, 0, sizeof(result));
	CHECK(SG_RuneCompactPartitionDeriveFaces(planes,
		(uint32_t)ARRAY_COUNT(planes), &bounds, NULL, &result, &error));
	CHECK(error.code == SG_RUNE_COMPACT_PARTITION_ERROR_NONE);
	CHECK(!result.empty);
	CHECK(result.face_count == 6U);
	for (face = 0U; face < result.face_count; face++)
	{
		CHECK(result.faces[face].vertex_count == 4U);
		CHECK(result.faces[face].source_plane_index < 6U);
		if (result.faces[face].source_plane_index == 0U)
			CHECK(result.faces[face].open == 1U);
	}
	memcpy(reordered, planes, sizeof(reordered));
	reordered[1] = planes[7];
	reordered[7] = planes[1];
	memset(&repeated, 0, sizeof(repeated));
	CHECK(SG_RuneCompactPartitionDeriveFaces(reordered,
		(uint32_t)ARRAY_COUNT(reordered), &bounds, NULL, &repeated, &error));
	CHECK(SamePolyhedron(&result, &repeated));
	SG_RuneCompactPartitionPolyhedronDestroy(&repeated, NULL);
	SG_RuneCompactPartitionPolyhedronDestroy(&result, NULL);
}

static void TestExactFeasibilityAndVolume(void)
{
	sg_rune_compact_partition_halfspace_t offset[7];
	sg_rune_compact_partition_halfspace_t flat[8];
	sg_rune_compact_partition_polyhedron_t result;
	sg_rune_compact_partition_error_t error;
	sg_rune_bounds_t offset_bounds = Bounds(-1.0f, 4095.0f, 0.0f,
		0.0f, 4096.0f, 1.0f);
	sg_rune_bounds_t cube_bounds = Bounds(-1.0f, -1.0f, -1.0f,
		1.0f, 1.0f, 1.0f);

	Cube(offset, -1.0f, 4095.0f, 0.0f, 0.0f, 4096.0f, 1.0f, 0U);
	offset[6] = AxisHalfspace(0U, 1.0f, 0.0001f, 6U);
	memset(&result, 0, sizeof(result));
	CHECK(SG_RuneCompactPartitionDeriveFaces(offset,
		(uint32_t)ARRAY_COUNT(offset), &offset_bounds, NULL, &result, &error));
	CHECK(!result.empty);
	CHECK(result.face_count == 6U);
	CHECK(result.bounds.maxs.value[0] == 0.0f);
	SG_RuneCompactPartitionPolyhedronDestroy(&result, NULL);

	Cube(flat, -1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f, 0U);
	memset(&flat[6], 0, sizeof(flat[6]));
	flat[6].plane.normal[0] = 1.0f;
	flat[6].plane.normal[1] = 1.0f;
	flat[6].plane.normal[2] = 1.0f;
	flat[6].source_plane_index = 6U;
	flat[6].contributor = 106U;
	flat[7] = flat[6];
	flat[7].plane.normal[0] = -1.0f;
	flat[7].plane.normal[1] = -1.0f;
	flat[7].plane.normal[2] = -1.0f;
	flat[7].source_plane_index = 7U;
	flat[7].contributor = 107U;
	memset(&result, 0, sizeof(result));
	CHECK(SG_RuneCompactPartitionDeriveFaces(flat,
		(uint32_t)ARRAY_COUNT(flat), &cube_bounds, NULL, &result, &error));
	CHECK(result.empty);
	CHECK(result.face_count == 0U);
	SG_RuneCompactPartitionPolyhedronDestroy(&result, NULL);
}

static void TestClipAndSubtract(void)
{
	sg_rune_vec3_t face_vertices[4];
	sg_rune_vec3_t portal_vertices[4];
	sg_rune_compact_partition_polygon_t face = Rectangle(face_vertices,
		-2.0f, -2.0f, 2.0f, 2.0f, 1U);
	sg_rune_compact_partition_polygon_t portal = Rectangle(portal_vertices,
		-1.0f, -1.0f, 1.0f, 1.0f, 2U);
	sg_rune_compact_partition_halfspace_t clip =
		AxisHalfspace(0U, 1.0f, 0.0f, 3U);
	sg_rune_compact_partition_polygon_t clipped;
	sg_rune_compact_partition_subtraction_t subtraction;
	sg_rune_compact_partition_error_t error;
	double remainder_area = 0.0;
	uint32_t fragment;

	memset(&clipped, 0, sizeof(clipped));
	CHECK(SG_RuneCompactPartitionClipPolygon(&face, &clip, NULL,
		&clipped, &error));
	CHECK(clipped.vertex_count == 4U);
	CHECK(fabs(PolygonArea(&clipped) - 8.0) < 0.000001);
	SG_RuneCompactPartitionPolygonDestroy(&clipped, NULL);

	memset(&subtraction, 0, sizeof(subtraction));
	CHECK(SG_RuneCompactPartitionSubtractPolygon(&face, &portal, NULL,
		&subtraction, &error));
	CHECK(subtraction.remainder_count == 4U);
	CHECK(fabs(PolygonArea(&subtraction.consumed) - 4.0) < 0.000001);
	CHECK(subtraction.consumed.source_plane_index ==
		portal.source_plane_index);
	for (fragment = 0U; fragment < subtraction.remainder_count; fragment++)
		remainder_area += PolygonArea(&subtraction.remainders[fragment]);
	CHECK(fabs(remainder_area - 12.0) < 0.000001);
	{
		static const double samples[4] = { -1.5, -0.5, 0.5, 1.5 };
		uint32_t x_index;
		uint32_t y_index;

		for (x_index = 0U; x_index < (uint32_t)ARRAY_COUNT(samples);
			x_index++)
			for (y_index = 0U; y_index < (uint32_t)ARRAY_COUNT(samples);
				y_index++)
			{
				int consumed = ContainsPointXY(&subtraction.consumed,
					samples[x_index], samples[y_index]);
				uint32_t covering_remainders = 0U;

				for (fragment = 0U;
					fragment < subtraction.remainder_count; fragment++)
					if (ContainsPointXY(&subtraction.remainders[fragment],
							samples[x_index], samples[y_index]))
						covering_remainders++;
				CHECK(consumed + (int)covering_remainders == 1);
			}
	}

	/* Applying the same portal again consumes no positive-area remainder. */
	for (fragment = 0U; fragment < subtraction.remainder_count; fragment++)
	{
		sg_rune_compact_partition_subtraction_t repeated;
		double repeated_area = 0.0;
		uint32_t next;

		memset(&repeated, 0, sizeof(repeated));
		CHECK(SG_RuneCompactPartitionSubtractPolygon(
			&subtraction.remainders[fragment], &portal, NULL, &repeated,
			&error));
		CHECK(repeated.consumed.vertex_count == 0U);
		for (next = 0U; next < repeated.remainder_count; next++)
			repeated_area += PolygonArea(&repeated.remainders[next]);
		CHECK(fabs(repeated_area -
			PolygonArea(&subtraction.remainders[fragment])) < 0.000001);
		SG_RuneCompactPartitionSubtractionDestroy(&repeated, NULL);
	}
	SG_RuneCompactPartitionSubtractionDestroy(&subtraction, NULL);
	{
		sg_rune_vec3_t offset_face_vertices[4];
		sg_rune_vec3_t offset_portal_vertices[4];
		sg_rune_compact_partition_polygon_t offset_face = Rectangle(
			offset_face_vertices, -1.0f, 4095.0f, 1.0f, 4096.0f, 30U);
		sg_rune_compact_partition_polygon_t offset_portal = Rectangle(
			offset_portal_vertices, -0.5f, 4095.25f, 0.5f, 4095.75f, 31U);
		sg_rune_compact_partition_halfspace_t outside_clip =
			AxisHalfspace(0U, 1.0f, 0.0f, 32U);
		sg_rune_vec3_t outside_vertices[4];
		sg_rune_compact_partition_polygon_t outside = Rectangle(
			outside_vertices, 0.0001f, 4095.0f, 1.0f, 4096.0f, 33U);
		uint32_t vertex;

		for (vertex = 0U; vertex < 4U; vertex++)
			offset_portal_vertices[vertex].value[2] = 0.0001f;
		offset_portal.plane.distance = 0.0001f;
		memset(&subtraction, 0, sizeof(subtraction));
		CHECK(!SG_RuneCompactPartitionSubtractPolygon(&offset_face,
			&offset_portal, NULL, &subtraction, &error));
		CHECK(error.code == SG_RUNE_COMPACT_PARTITION_ERROR_DEGENERATE);
		memset(&clipped, 0, sizeof(clipped));
		CHECK(SG_RuneCompactPartitionClipPolygon(&outside, &outside_clip,
			NULL, &clipped, &error));
		CHECK(clipped.vertex_count == 0U);
		SG_RuneCompactPartitionPolygonDestroy(&clipped, NULL);
	}
}

static void TestCellIntersectionAndDeterminism(void)
{
	sg_rune_compact_partition_halfspace_t left_planes[6];
	sg_rune_compact_partition_halfspace_t right_planes[6];
	sg_rune_compact_partition_cell_t left;
	sg_rune_compact_partition_cell_t right;
	sg_rune_compact_partition_polyhedron_t first;
	sg_rune_compact_partition_polyhedron_t second;
	sg_rune_compact_partition_error_t error;

	Cube(left_planes, 0.0f, 0.0f, 0.0f, 2.0f, 2.0f, 2.0f, 0U);
	Cube(right_planes, 1.0f, 0.0f, 0.0f, 3.0f, 2.0f, 2.0f, 20U);
	left.halfspaces = left_planes;
	left.halfspace_count = 6U;
	left.bounds = Bounds(0.0f, 0.0f, 0.0f, 2.0f, 2.0f, 2.0f);
	right.halfspaces = right_planes;
	right.halfspace_count = 6U;
	right.bounds = Bounds(1.0f, 0.0f, 0.0f, 3.0f, 2.0f, 2.0f);
	memset(&first, 0, sizeof(first));
	memset(&second, 0, sizeof(second));
	CHECK(SG_RuneCompactPartitionIntersectCells(&left, &right, NULL,
		&first, &error));
	CHECK(SG_RuneCompactPartitionIntersectCells(&left, &right, NULL,
		&second, &error));
	CHECK(first.face_count == 6U);
	CHECK(first.bounds.mins.value[0] == 1.0f);
	CHECK(first.bounds.maxs.value[0] == 2.0f);
	CHECK(first.bounds.mins.value[1] == 0.0f);
	CHECK(first.bounds.maxs.value[2] == 2.0f);
	CHECK(SamePolyhedron(&first, &second));
	SG_RuneCompactPartitionPolyhedronDestroy(&second, NULL);
	memset(&second, 0, sizeof(second));
	CHECK(SG_RuneCompactPartitionIntersectCells(&right, &left, NULL,
		&second, &error));
	CHECK(SamePolyhedron(&first, &second));
	SG_RuneCompactPartitionPolyhedronDestroy(&second, NULL);
	SG_RuneCompactPartitionPolyhedronDestroy(&first, NULL);

	memset(&first, 0, sizeof(first));
	CHECK(SG_RuneCompactPartitionIntersectCells(&left, &left, NULL,
		&first, &error));
	CHECK(first.face_count == 6U);
	SG_RuneCompactPartitionPolyhedronDestroy(&first, NULL);

	{
		sg_rune_compact_partition_halfspace_t flat_left[7];
		sg_rune_compact_partition_halfspace_t flat_right[7];
		sg_rune_compact_partition_cell_t flat_left_cell;
		sg_rune_compact_partition_cell_t flat_right_cell;

		Cube(flat_left, -1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f, 40U);
		Cube(flat_right, -1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f, 50U);
		memset(&flat_left[6], 0, sizeof(flat_left[6]));
		flat_left[6].plane.normal[0] = 1.0f;
		flat_left[6].plane.normal[1] = 1.0f;
		flat_left[6].plane.normal[2] = 1.0f;
		flat_left[6].source_plane_index = 46U;
		flat_left[6].contributor = 146U;
		memset(&flat_right[6], 0, sizeof(flat_right[6]));
		flat_right[6].plane.normal[0] = -1.0f;
		flat_right[6].plane.normal[1] = -1.0f;
		flat_right[6].plane.normal[2] = -1.0f;
		flat_right[6].source_plane_index = 56U;
		flat_right[6].contributor = 156U;
		flat_left_cell.halfspaces = flat_left;
		flat_left_cell.halfspace_count = 7U;
		flat_left_cell.bounds = Bounds(-1.0f, -1.0f, -1.0f,
			1.0f, 1.0f, 1.0f);
		flat_right_cell.halfspaces = flat_right;
		flat_right_cell.halfspace_count = 7U;
		flat_right_cell.bounds = flat_left_cell.bounds;
		memset(&first, 0, sizeof(first));
		CHECK(SG_RuneCompactPartitionIntersectCells(&flat_left_cell,
			&flat_right_cell, NULL, &first, &error));
		CHECK(first.empty);
		SG_RuneCompactPartitionPolyhedronDestroy(&first, NULL);
	}
}

static void TestTypedFailuresAndOomSweep(void)
{
	sg_rune_compact_partition_halfspace_t planes[6];
	sg_rune_compact_partition_halfspace_t right_planes[6];
	sg_rune_bounds_t bounds = Bounds(-1.0f, -1.0f, -1.0f,
		1.0f, 1.0f, 1.0f);
	sg_rune_bounds_t right_bounds = Bounds(0.0f, -1.0f, -1.0f,
		2.0f, 1.0f, 1.0f);
	sg_rune_compact_partition_error_t error;
	sg_rune_compact_partition_polyhedron_t sentinel;
	size_t failure;
	int reached_success = 0;

	Cube(planes, -1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f, 0U);
	planes[0].plane.normal[0] = NAN;
	memset(&sentinel, 0x5a, sizeof(sentinel));
	CHECK(!SG_RuneCompactPartitionDeriveFaces(planes, 6U, &bounds, NULL,
		&sentinel, &error));
	CHECK(error.code == SG_RUNE_COMPACT_PARTITION_ERROR_NONFINITE);
	CHECK(sentinel.face_count == UINT32_C(0x5a5a5a5a));

	planes[0].plane.normal[0] = 0.0f;
	memset(&sentinel, 0x5a, sizeof(sentinel));
	CHECK(!SG_RuneCompactPartitionDeriveFaces(planes, 6U, &bounds, NULL,
		&sentinel, &error));
	CHECK(error.code == SG_RUNE_COMPACT_PARTITION_ERROR_DEGENERATE);
	CHECK(sentinel.face_count == UINT32_C(0x5a5a5a5a));
	bounds.mins.value[0] = NAN;
	memset(&sentinel, 0x5a, sizeof(sentinel));
	CHECK(!SG_RuneCompactPartitionDeriveFaces(planes, 6U, &bounds, NULL,
		&sentinel, &error));
	CHECK(error.code == SG_RUNE_COMPACT_PARTITION_ERROR_NONFINITE);
	CHECK(sentinel.face_count == UINT32_C(0x5a5a5a5a));
	bounds.mins.value[0] = -1.0f;

	Cube(planes, -1.0f, -1.0f, -1.0f, 1.0f, 1.0f, 1.0f, 0U);
	for (failure = 0U; failure < 128U; failure++)
	{
		failing_allocator_t state = { 0U, failure, 0U };
		sg_rune_compact_partition_allocator_t allocator = {
			&state, FailingAllocate, FailingRelease
		};

		memset(&sentinel, 0x5a, sizeof(sentinel));
		if (SG_RuneCompactPartitionDeriveFaces(planes, 6U, &bounds,
				&allocator, &sentinel, &error))
		{
			reached_success = 1;
			SG_RuneCompactPartitionPolyhedronDestroy(&sentinel, &allocator);
			CHECK(state.active == 0U);
			break;
		}
		CHECK(error.code == SG_RUNE_COMPACT_PARTITION_ERROR_OUT_OF_MEMORY);
		CHECK(sentinel.face_count == UINT32_C(0x5a5a5a5a));
		CHECK(state.active == 0U);
	}
	CHECK(reached_success);

	/* Sweep every allocation point in clipping. */
	{
		sg_rune_vec3_t face_vertices[4];
		sg_rune_compact_partition_polygon_t face = Rectangle(face_vertices,
			-2.0f, -2.0f, 2.0f, 2.0f, 1U);
		sg_rune_compact_partition_halfspace_t clip =
			AxisHalfspace(0U, 1.0f, 0.0f, 3U);
		int clip_success = 0;

		for (failure = 0U; failure < 128U; failure++)
		{
			failing_allocator_t state = { 0U, failure, 0U };
			sg_rune_compact_partition_allocator_t allocator = {
				&state, FailingAllocate, FailingRelease
			};
			sg_rune_compact_partition_polygon_t output;

			memset(&output, 0x5a, sizeof(output));
			if (SG_RuneCompactPartitionClipPolygon(&face, &clip, &allocator,
					&output, &error))
			{
				clip_success = 1;
				SG_RuneCompactPartitionPolygonDestroy(&output, &allocator);
				CHECK(state.active == 0U);
				break;
			}
			CHECK(error.code ==
				SG_RUNE_COMPACT_PARTITION_ERROR_OUT_OF_MEMORY);
			CHECK(output.vertex_count == UINT32_C(0x5a5a5a5a));
			CHECK(state.active == 0U);
		}
		CHECK(clip_success);
	}

	/* Sweep subtraction, whose cleanup owns partially published fragments. */
	{
		sg_rune_vec3_t face_vertices[4];
		sg_rune_vec3_t portal_vertices[4];
		sg_rune_compact_partition_polygon_t face = Rectangle(face_vertices,
			-2.0f, -2.0f, 2.0f, 2.0f, 1U);
		sg_rune_compact_partition_polygon_t portal = Rectangle(portal_vertices,
			-1.0f, -1.0f, 1.0f, 1.0f, 2U);
		int subtract_success = 0;

		for (failure = 0U; failure < 128U; failure++)
		{
			failing_allocator_t state = { 0U, failure, 0U };
			sg_rune_compact_partition_allocator_t allocator = {
				&state, FailingAllocate, FailingRelease
			};
			sg_rune_compact_partition_subtraction_t output;

			memset(&output, 0x5a, sizeof(output));
			if (SG_RuneCompactPartitionSubtractPolygon(&face, &portal,
					&allocator, &output, &error))
			{
				subtract_success = 1;
				SG_RuneCompactPartitionSubtractionDestroy(&output, &allocator);
				CHECK(state.active == 0U);
				break;
			}
			CHECK(error.code ==
				SG_RUNE_COMPACT_PARTITION_ERROR_OUT_OF_MEMORY);
			CHECK(output.remainder_count == UINT32_C(0x5a5a5a5a));
			CHECK(state.active == 0U);
		}
		CHECK(subtract_success);
	}

	/* Sweep both input validation meshes and the combined overlay mesh. */
	Cube(right_planes, 0.0f, -1.0f, -1.0f, 2.0f, 1.0f, 1.0f, 20U);
	{
		sg_rune_compact_partition_cell_t left = { planes, 6U, bounds };
		sg_rune_compact_partition_cell_t right = {
			right_planes, 6U, right_bounds
		};
		int intersect_success = 0;

		for (failure = 0U; failure < 256U; failure++)
		{
			failing_allocator_t state = { 0U, failure, 0U };
			sg_rune_compact_partition_allocator_t allocator = {
				&state, FailingAllocate, FailingRelease
			};

			memset(&sentinel, 0x5a, sizeof(sentinel));
			if (SG_RuneCompactPartitionIntersectCells(&left, &right,
					&allocator, &sentinel, &error))
			{
				intersect_success = 1;
				SG_RuneCompactPartitionPolyhedronDestroy(&sentinel,
					&allocator);
				CHECK(state.active == 0U);
				break;
			}
			CHECK(error.code ==
				SG_RUNE_COMPACT_PARTITION_ERROR_OUT_OF_MEMORY);
			CHECK(sentinel.face_count == UINT32_C(0x5a5a5a5a));
			CHECK(state.active == 0U);
		}
		CHECK(intersect_success);
	}
}

int main(void)
{
	TestCubeFacesAndRedundancy();
	TestExactFeasibilityAndVolume();
	TestClipAndSubtract();
	TestCellIntersectionAndDeterminism();
	TestTypedFailuresAndOomSweep();

	if (failures)
	{
		fprintf(stderr, "sg_rune_compact_geometry_partition_test: %d failures\n",
			failures);
		return 1;
	}
	printf("sg_rune_compact_geometry_partition_test: PASS\n");
	return 0;
}
