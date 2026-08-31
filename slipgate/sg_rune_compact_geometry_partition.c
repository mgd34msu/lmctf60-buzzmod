#include "sg_rune_compact_geometry_partition.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct partition_allocator_s
{
	void *context;
	sg_rune_compact_partition_allocate_fn allocate;
	sg_rune_compact_partition_release_fn release;
} partition_allocator_t;

typedef struct normalized_plane_s
{
	double normal[3];
	double distance;
} normalized_plane_t;

typedef struct partition_point_s
{
	double value[3];
	double angle;
} partition_point_t;

static void *DefaultAllocate(void *context, size_t bytes)
{
	(void)context;
	return malloc(bytes);
}

static void DefaultRelease(void *context, void *allocation)
{
	(void)context;
	free(allocation);
}

static int SelectAllocator(
	const sg_rune_compact_partition_allocator_t *source,
	partition_allocator_t *selected)
{
	if (!selected)
		return 0;
	if (!source)
	{
		selected->context = NULL;
		selected->allocate = DefaultAllocate;
		selected->release = DefaultRelease;
		return 1;
	}
	if (!source->allocate || !source->release)
		return 0;
	selected->context = source->context;
	selected->allocate = source->allocate;
	selected->release = source->release;
	return 1;
}

static void SetError(sg_rune_compact_partition_error_t *error,
	sg_rune_compact_partition_error_code_t code,
	sg_rune_compact_partition_operation_t operation, uint32_t record)
{
	if (!error)
		return;
	error->code = code;
	error->operation = operation;
	error->record = record;
}

static int CheckedBytes(size_t count, size_t element_size, size_t *bytes_out)
{
	if (!bytes_out || (element_size && count > SIZE_MAX / element_size))
		return 0;
	*bytes_out = count * element_size;
	return 1;
}

static double Dot(const double left[3], const double right[3])
{
	return left[0] * right[0] + left[1] * right[1] +
		left[2] * right[2];
}

static void Cross(const double left[3], const double right[3],
	double result[3])
{
	result[0] = left[1] * right[2] - left[2] * right[1];
	result[1] = left[2] * right[0] - left[0] * right[2];
	result[2] = left[0] * right[1] - left[1] * right[0];
}

static double CoordinateScale(const sg_rune_bounds_t *bounds)
{
	double scale = 1.0;
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
	{
		double extent = fabs((double)bounds->maxs.value[axis] -
			(double)bounds->mins.value[axis]);

		if (extent > scale)
			scale = extent;
	}
	return scale;
}

static int ValidateBounds(const sg_rune_bounds_t *bounds,
	sg_rune_compact_partition_error_code_t *failure)
{
	uint32_t axis;

	if (!bounds || !failure)
	{
		if (failure)
			*failure = SG_RUNE_COMPACT_PARTITION_ERROR_INVALID_ARGUMENT;
		return 0;
	}
	for (axis = 0U; axis < 3U; axis++)
	{
		if (!isfinite(bounds->mins.value[axis]) ||
			!isfinite(bounds->maxs.value[axis]))
		{
			*failure = SG_RUNE_COMPACT_PARTITION_ERROR_NONFINITE;
			return 0;
		}
		if (bounds->mins.value[axis] >= bounds->maxs.value[axis])
		{
			*failure = SG_RUNE_COMPACT_PARTITION_ERROR_DEGENERATE;
			return 0;
		}
	}
	return 1;
}

static int NormalizePlane(const sg_configuration_plane_t *source,
	normalized_plane_t *result,
	sg_rune_compact_partition_error_code_t *failure)
{
	double squared = 0.0;
	double length;
	uint32_t axis;

	if (!source || !result || !failure)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
	{
		if (!isfinite(source->normal[axis]))
		{
			*failure = SG_RUNE_COMPACT_PARTITION_ERROR_NONFINITE;
			return 0;
		}
		result->normal[axis] = (double)source->normal[axis];
		squared += result->normal[axis] * result->normal[axis];
	}
	if (!isfinite(source->distance) || !isfinite(squared))
	{
		*failure = SG_RUNE_COMPACT_PARTITION_ERROR_NONFINITE;
		return 0;
	}
	if (squared <= DBL_MIN)
	{
		*failure = SG_RUNE_COMPACT_PARTITION_ERROR_DEGENERATE;
		return 0;
	}
	length = sqrt(squared);
	if (!isfinite(length) || length <= DBL_MIN)
	{
		*failure = SG_RUNE_COMPACT_PARTITION_ERROR_DEGENERATE;
		return 0;
	}
	for (axis = 0U; axis < 3U; axis++)
		result->normal[axis] /= length;
	result->distance = (double)source->distance / length;
	if (!isfinite(result->distance))
	{
		*failure = SG_RUNE_COMPACT_PARTITION_ERROR_NONFINITE;
		return 0;
	}
	return 1;
}

static double PlaneResidualTolerance(const normalized_plane_t *plane,
	const double point[3])
{
	double magnitude = fabs(plane->distance) + 1.0;
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		magnitude += fabs(plane->normal[axis] * point[axis]);
	return 8.0 * (double)FLT_EPSILON * magnitude;
}

static int SamePoint(const partition_point_t *left,
	const partition_point_t *right, double epsilon)
{
	return fabs(left->value[0] - right->value[0]) <= epsilon &&
		fabs(left->value[1] - right->value[1]) <= epsilon &&
		fabs(left->value[2] - right->value[2]) <= epsilon;
}

static int LexicographicPoint(const partition_point_t *left,
	const partition_point_t *right)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
	{
		if (left->value[axis] < right->value[axis])
			return -1;
		if (left->value[axis] > right->value[axis])
			return 1;
	}
	return 0;
}

static int AnglePoint(const partition_point_t *left,
	const partition_point_t *right)
{
	if (left->angle < right->angle)
		return -1;
	if (left->angle > right->angle)
		return 1;
	return LexicographicPoint(left, right);
}

static void SortPointsByAngle(partition_point_t *points, uint32_t count)
{
	uint32_t index;

	for (index = 1U; index < count; index++)
	{
		partition_point_t value = points[index];
		uint32_t insert = index;

		while (insert > 0U && AnglePoint(&value, &points[insert - 1U]) < 0)
		{
			points[insert] = points[insert - 1U];
			insert--;
		}
		points[insert] = value;
	}
}

static void ReversePoints(partition_point_t *points, uint32_t count)
{
	uint32_t index;

	for (index = 0U; index < count / 2U; index++)
	{
		partition_point_t temporary = points[index];

		points[index] = points[count - 1U - index];
		points[count - 1U - index] = temporary;
	}
}

static void RotatePoints(partition_point_t *points, uint32_t count,
	uint32_t first)
{
	uint32_t rotations;

	for (rotations = 0U; rotations < first; rotations++)
	{
		partition_point_t value = points[0];
		uint32_t index;

		for (index = 1U; index < count; index++)
			points[index - 1U] = points[index];
		points[count - 1U] = value;
	}
}

static double Turn(const partition_point_t *before,
	const partition_point_t *point, const partition_point_t *after,
	const double normal[3])
{
	double incoming[3];
	double outgoing[3];
	double cross[3];
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
	{
		incoming[axis] = point->value[axis] - before->value[axis];
		outgoing[axis] = after->value[axis] - point->value[axis];
	}
	Cross(incoming, outgoing, cross);
	return Dot(cross, normal);
}

static uint32_t RemoveCollinear(partition_point_t *points, uint32_t count,
	const double normal[3], double epsilon)
{
	int removed = 1;

	while (removed && count >= 3U)
	{
		uint32_t index;

		removed = 0;
		for (index = 0U; index < count; index++)
		{
			uint32_t before = (index + count - 1U) % count;
			uint32_t after = (index + 1U) % count;
			double turn = Turn(&points[before], &points[index],
				&points[after], normal);

			if (fabs(turn) <= epsilon * epsilon)
			{
				uint32_t move;

				for (move = index + 1U; move < count; move++)
					points[move - 1U] = points[move];
				count--;
				removed = 1;
				break;
			}
		}
	}
	return count;
}

static int CanonicalVertices(partition_point_t *points, uint32_t count,
	const normalized_plane_t *plane, double epsilon,
	const partition_allocator_t *allocator, sg_rune_vec3_t **vertices_out,
	uint32_t *count_out,
	sg_rune_compact_partition_error_code_t *failure)
{
	double center[3] = { 0.0, 0.0, 0.0 };
	double axis[3] = { 0.0, 0.0, 0.0 };
	double basis_u[3];
	double basis_v[3];
	double basis_length;
	double twice_area = 0.0;
	sg_rune_vec3_t *vertices;
	size_t bytes;
	uint32_t unique_count = 0U;
	uint32_t index;
	uint32_t least_axis = 0U;
	uint32_t first = 0U;

	*vertices_out = NULL;
	*count_out = 0U;
	for (index = 0U; index < count; index++)
	{
		uint32_t prior;
		int duplicate = 0;

		for (prior = 0U; prior < unique_count; prior++)
			if (SamePoint(&points[index], &points[prior], epsilon))
			{
				duplicate = 1;
				break;
			}
		if (!duplicate)
			points[unique_count++] = points[index];
	}
	count = unique_count;
	if (count < 3U)
		return 1;
	for (index = 0U; index < count; index++)
	{
		center[0] += points[index].value[0];
		center[1] += points[index].value[1];
		center[2] += points[index].value[2];
	}
	center[0] /= (double)count;
	center[1] /= (double)count;
	center[2] /= (double)count;
	for (index = 1U; index < 3U; index++)
		if (fabs(plane->normal[index]) <
			fabs(plane->normal[least_axis]))
			least_axis = index;
	axis[least_axis] = 1.0;
	Cross(axis, plane->normal, basis_u);
	basis_length = sqrt(Dot(basis_u, basis_u));
	if (!isfinite(basis_length) || basis_length <= DBL_MIN)
	{
		*failure = SG_RUNE_COMPACT_PARTITION_ERROR_DEGENERATE;
		return 0;
	}
	for (index = 0U; index < 3U; index++)
		basis_u[index] /= basis_length;
	Cross(plane->normal, basis_u, basis_v);
	for (index = 0U; index < count; index++)
	{
		double delta[3];
		uint32_t coordinate;

		for (coordinate = 0U; coordinate < 3U; coordinate++)
			delta[coordinate] = points[index].value[coordinate] -
				center[coordinate];
		points[index].angle = atan2(Dot(delta, basis_v),
			Dot(delta, basis_u));
	}
	SortPointsByAngle(points, count);
	for (index = 0U; index < count; index++)
	{
		uint32_t before = (index + count - 1U) % count;
		uint32_t after = (index + 1U) % count;

		if (Turn(&points[before], &points[index], &points[after],
				plane->normal) < -epsilon * epsilon)
		{
			*failure = SG_RUNE_COMPACT_PARTITION_ERROR_DEGENERATE;
			return 0;
		}
	}
	count = RemoveCollinear(points, count, plane->normal, epsilon);
	if (count < 3U)
		return 1;
	for (index = 0U; index < count; index++)
	{
		double cross[3];

		Cross(points[index].value,
			points[(index + 1U) % count].value, cross);
		twice_area += Dot(cross, plane->normal);
	}
	if (!isfinite(twice_area) || fabs(twice_area) <= epsilon * epsilon)
		return 1;
	if (twice_area < 0.0)
		ReversePoints(points, count);
	for (index = 1U; index < count; index++)
		if (LexicographicPoint(&points[index], &points[first]) < 0)
			first = index;
	RotatePoints(points, count, first);
	if (!CheckedBytes((size_t)count, sizeof(*vertices), &bytes))
	{
		*failure = SG_RUNE_COMPACT_PARTITION_ERROR_OVERFLOW;
		return 0;
	}
	vertices = allocator->allocate(allocator->context, bytes);
	if (!vertices)
	{
		*failure = SG_RUNE_COMPACT_PARTITION_ERROR_OUT_OF_MEMORY;
		return 0;
	}
	for (index = 0U; index < count; index++)
	{
		uint32_t coordinate;

		for (coordinate = 0U; coordinate < 3U; coordinate++)
		{
			double value = points[index].value[coordinate];
			float converted;

			if (!isfinite(value) || fabs(value) > (double)FLT_MAX)
			{
				allocator->release(allocator->context, vertices);
				*failure = SG_RUNE_COMPACT_PARTITION_ERROR_OVERFLOW;
				return 0;
			}
			converted = (float)value;
			vertices[index].value[coordinate] = converted == 0.0f ?
				0.0f : converted;
		}
	}
	*vertices_out = vertices;
	*count_out = count;
	return 1;
}

static int IntersectThreePlanes(const normalized_plane_t *first,
	const normalized_plane_t *second, const normalized_plane_t *third,
	partition_point_t *point)
{
	double second_cross_third[3];
	double third_cross_first[3];
	double first_cross_second[3];
	double determinant;
	uint32_t axis;

	Cross(second->normal, third->normal, second_cross_third);
	determinant = Dot(first->normal, second_cross_third);
	if (fabs(determinant) <= 1.0e-12)
		return 0;
	Cross(third->normal, first->normal, third_cross_first);
	Cross(first->normal, second->normal, first_cross_second);
	for (axis = 0U; axis < 3U; axis++)
	{
		point->value[axis] =
			(first->distance * second_cross_third[axis] +
			second->distance * third_cross_first[axis] +
			third->distance * first_cross_second[axis]) / determinant;
		if (!isfinite(point->value[axis]))
			return 0;
	}
	point->angle = 0.0;
	return 1;
}

static int PointInside(const partition_point_t *point,
	const normalized_plane_t *planes, uint32_t plane_count,
	const sg_rune_bounds_t *bounds)
{
	uint32_t plane;
	uint32_t axis;

	for (plane = 0U; plane < plane_count; plane++)
	{
		double magnitude = fabs(planes[plane].distance) + 1.0;
		double residual;

		for (axis = 0U; axis < 3U; axis++)
			magnitude += fabs(planes[plane].normal[axis] *
				point->value[axis]);
		residual = Dot(planes[plane].normal, point->value) -
			planes[plane].distance;
		if (residual > 128.0 * DBL_EPSILON * magnitude)
			return 0;
	}
	for (axis = 0U; axis < 3U; axis++)
	{
		double magnitude = fabs(point->value[axis]) +
			fabs((double)bounds->mins.value[axis]) +
			fabs((double)bounds->maxs.value[axis]) + 1.0;
		double tolerance = 128.0 * DBL_EPSILON * magnitude;

		if (point->value[axis] <
				(double)bounds->mins.value[axis] - tolerance ||
			point->value[axis] >
				(double)bounds->maxs.value[axis] + tolerance)
			return 0;
	}
	return 1;
}

static int SamePlane(const normalized_plane_t *left,
	const normalized_plane_t *right, double epsilon)
{
	return fabs(left->normal[0] - right->normal[0]) <= epsilon &&
		fabs(left->normal[1] - right->normal[1]) <= epsilon &&
		fabs(left->normal[2] - right->normal[2]) <= epsilon &&
		fabs(left->distance - right->distance) <= epsilon;
}

static uint32_t FloatBits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static int HalfspaceKeyCompare(
	const sg_rune_compact_partition_halfspace_t *left,
	const sg_rune_compact_partition_halfspace_t *right)
{
	uint32_t axis;

#define COMPARE_FIELD(field) \
	do { \
		if (left->field < right->field) return -1; \
		if (left->field > right->field) return 1; \
	} while (0)
	COMPARE_FIELD(contributor);
	COMPARE_FIELD(source_plane_index);
	COMPARE_FIELD(plane.source_kind);
	COMPARE_FIELD(plane.source_index);
	COMPARE_FIELD(plane.source_variant);
	COMPARE_FIELD(plane.reversed);
#undef COMPARE_FIELD
	for (axis = 0U; axis < 3U; axis++)
	{
		uint32_t left_bits = FloatBits(left->plane.normal[axis]);
		uint32_t right_bits = FloatBits(right->plane.normal[axis]);

		if (left_bits < right_bits)
			return -1;
		if (left_bits > right_bits)
			return 1;
	}
	if (FloatBits(left->plane.distance) < FloatBits(right->plane.distance))
		return -1;
	if (FloatBits(left->plane.distance) > FloatBits(right->plane.distance))
		return 1;
	return 0;
}

static int CompareFaces(const void *left_pointer, const void *right_pointer)
{
	const sg_rune_compact_partition_polygon_t *left = left_pointer;
	const sg_rune_compact_partition_polygon_t *right = right_pointer;
	normalized_plane_t left_plane = { { 0.0, 0.0, 0.0 }, 0.0 };
	normalized_plane_t right_plane = { { 0.0, 0.0, 0.0 }, 0.0 };
	sg_rune_compact_partition_error_code_t failure =
		SG_RUNE_COMPACT_PARTITION_ERROR_NONE;
	uint32_t axis;

	(void)NormalizePlane(&left->plane, &left_plane, &failure);
	(void)NormalizePlane(&right->plane, &right_plane, &failure);
	for (axis = 0U; axis < 3U; axis++)
	{
		if (left_plane.normal[axis] < right_plane.normal[axis])
			return -1;
		if (left_plane.normal[axis] > right_plane.normal[axis])
			return 1;
	}
	if (left_plane.distance < right_plane.distance)
		return -1;
	if (left_plane.distance > right_plane.distance)
		return 1;
	if (left->contributor < right->contributor)
		return -1;
	if (left->contributor > right->contributor)
		return 1;
	if (left->source_plane_index < right->source_plane_index)
		return -1;
	if (left->source_plane_index > right->source_plane_index)
		return 1;
	return 0;
}

static void ReleasePolygon(sg_rune_compact_partition_polygon_t *polygon,
	const partition_allocator_t *allocator)
{
	if (!polygon || !allocator)
		return;
	if (polygon->vertices)
		allocator->release(allocator->context, polygon->vertices);
	memset(polygon, 0, sizeof(*polygon));
}

static void ReleasePolyhedron(
	sg_rune_compact_partition_polyhedron_t *polyhedron,
	const partition_allocator_t *allocator)
{
	uint32_t face;

	if (!polyhedron || !allocator)
		return;
	for (face = 0U; face < polyhedron->face_count; face++)
		ReleasePolygon(&polyhedron->faces[face], allocator);
	if (polyhedron->faces)
		allocator->release(allocator->context, polyhedron->faces);
	memset(polyhedron, 0, sizeof(*polyhedron));
}

static void ReleaseSubtraction(
	sg_rune_compact_partition_subtraction_t *subtraction,
	const partition_allocator_t *allocator)
{
	uint32_t remainder;

	if (!subtraction || !allocator)
		return;
	for (remainder = 0U; remainder < subtraction->remainder_count;
		remainder++)
		ReleasePolygon(&subtraction->remainders[remainder], allocator);
	if (subtraction->remainders)
		allocator->release(allocator->context, subtraction->remainders);
	ReleasePolygon(&subtraction->consumed, allocator);
	memset(subtraction, 0, sizeof(*subtraction));
}

static void SetDerivedBounds(sg_rune_compact_partition_polyhedron_t *result)
{
	uint32_t face;
	uint32_t vertex;
	uint32_t axis;
	int first = 1;

	for (face = 0U; face < result->face_count; face++)
		for (vertex = 0U; vertex < result->faces[face].vertex_count; vertex++)
		{
			const sg_rune_vec3_t *point =
				&result->faces[face].vertices[vertex];

			if (first)
			{
				result->bounds.mins = *point;
				result->bounds.maxs = *point;
				first = 0;
				continue;
			}
			for (axis = 0U; axis < 3U; axis++)
			{
				if (point->value[axis] < result->bounds.mins.value[axis])
					result->bounds.mins.value[axis] = point->value[axis];
				if (point->value[axis] > result->bounds.maxs.value[axis])
					result->bounds.maxs.value[axis] = point->value[axis];
			}
		}
	result->empty = first;
	if (!first)
		for (axis = 0U; axis < 3U; axis++)
			if (result->bounds.maxs.value[axis] <=
				result->bounds.mins.value[axis])
				result->empty = 1;
}

static int PolyhedronHasVolume(
	const sg_rune_compact_partition_polyhedron_t *polyhedron)
{
	const sg_rune_vec3_t *origin;
	double first[3] = { 0.0, 0.0, 0.0 };
	double second[3] = { 0.0, 0.0, 0.0 };
	double cross[3];
	double first_squared = 0.0;
	double cross_squared = 0.0;
	double determinant = 0.0;
	uint32_t face;
	uint32_t vertex;
	uint32_t axis;

	if (polyhedron->face_count == 0U ||
		polyhedron->faces[0].vertex_count == 0U)
		return 0;
	origin = &polyhedron->faces[0].vertices[0];
	for (face = 0U; face < polyhedron->face_count; face++)
		for (vertex = 0U; vertex < polyhedron->faces[face].vertex_count; vertex++)
		{
			double delta[3];
			double squared;

			for (axis = 0U; axis < 3U; axis++)
				delta[axis] = (double)polyhedron->faces[face].vertices[
					vertex].value[axis] - (double)origin->value[axis];
			squared = Dot(delta, delta);
			if (squared > first_squared)
			{
				memcpy(first, delta, sizeof(first));
				first_squared = squared;
			}
		}
	if (first_squared == 0.0)
		return 0;
	for (face = 0U; face < polyhedron->face_count; face++)
		for (vertex = 0U; vertex < polyhedron->faces[face].vertex_count; vertex++)
		{
			double delta[3];
			double candidate[3];
			double squared;

			for (axis = 0U; axis < 3U; axis++)
				delta[axis] = (double)polyhedron->faces[face].vertices[
					vertex].value[axis] - (double)origin->value[axis];
			Cross(first, delta, candidate);
			squared = Dot(candidate, candidate);
			if (squared > cross_squared)
			{
				memcpy(second, delta, sizeof(second));
				cross_squared = squared;
			}
		}
	if (cross_squared == 0.0)
		return 0;
	Cross(first, second, cross);
	for (face = 0U; face < polyhedron->face_count; face++)
		for (vertex = 0U; vertex < polyhedron->faces[face].vertex_count; vertex++)
		{
			double delta[3];
			double candidate;

			for (axis = 0U; axis < 3U; axis++)
				delta[axis] = (double)polyhedron->faces[face].vertices[
					vertex].value[axis] - (double)origin->value[axis];
			candidate = fabs(Dot(cross, delta));
			if (candidate > determinant)
				determinant = candidate;
		}
	return determinant > 128.0 * DBL_EPSILON *
		(sqrt(first_squared) * sqrt(cross_squared) + 1.0);
}

static int DeriveFacesInternal(
	const sg_rune_compact_partition_halfspace_t *halfspaces,
	uint32_t halfspace_count, const sg_rune_bounds_t *bounds,
	const partition_allocator_t *allocator,
	sg_rune_compact_partition_polyhedron_t *result,
	sg_rune_compact_partition_error_t *error,
	sg_rune_compact_partition_operation_t operation)
{
	normalized_plane_t *planes = NULL;
	partition_point_t *points = NULL;
	size_t plane_bytes;
	size_t face_bytes;
	size_t point_bytes;
	size_t pair_count;
	double coordinate_scale;
	double point_epsilon;
	double plane_epsilon = 1.0e-10;
	uint32_t target;
	sg_rune_compact_partition_error_code_t failure =
		SG_RUNE_COMPACT_PARTITION_ERROR_NONE;

	memset(result, 0, sizeof(*result));
	if (!halfspaces || !bounds)
	{
		SetError(error, SG_RUNE_COMPACT_PARTITION_ERROR_INVALID_ARGUMENT,
			operation, 0U);
		return 0;
	}
	if (halfspace_count < 4U)
	{
		SetError(error, SG_RUNE_COMPACT_PARTITION_ERROR_DEGENERATE,
			operation, 0U);
		return 0;
	}
	if (!ValidateBounds(bounds, &failure))
	{
		SetError(error, failure, operation, 0U);
		return 0;
	}
	if (!CheckedBytes((size_t)halfspace_count, sizeof(*planes),
			&plane_bytes) ||
		!CheckedBytes((size_t)halfspace_count, sizeof(*result->faces),
			&face_bytes))
	{
		SetError(error, SG_RUNE_COMPACT_PARTITION_ERROR_OVERFLOW,
			operation, 0U);
		return 0;
	}
	pair_count = (size_t)(halfspace_count - 1U);
	if (pair_count > 0U && pair_count - 1U > SIZE_MAX / pair_count)
	{
		SetError(error, SG_RUNE_COMPACT_PARTITION_ERROR_OVERFLOW,
			operation, 0U);
		return 0;
	}
	pair_count = pair_count * (pair_count - 1U) / 2U;
	if (pair_count > (size_t)UINT32_MAX)
	{
		SetError(error, SG_RUNE_COMPACT_PARTITION_ERROR_OVERFLOW,
			operation, 0U);
		return 0;
	}
	if (!CheckedBytes(pair_count, sizeof(*points), &point_bytes))
	{
		SetError(error, SG_RUNE_COMPACT_PARTITION_ERROR_OVERFLOW,
			operation, 0U);
		return 0;
	}
	planes = allocator->allocate(allocator->context, plane_bytes);
	result->faces = allocator->allocate(allocator->context, face_bytes);
	if (pair_count)
		points = allocator->allocate(allocator->context, point_bytes);
	if (!planes || !result->faces || (pair_count && !points))
	{
		SetError(error, SG_RUNE_COMPACT_PARTITION_ERROR_OUT_OF_MEMORY,
			operation, 0U);
		goto failure;
	}
	memset(result->faces, 0, face_bytes);
	for (target = 0U; target < halfspace_count; target++)
	{
		if (halfspaces[target].open > 1U)
		{
			SetError(error, SG_RUNE_COMPACT_PARTITION_ERROR_INVALID_ARGUMENT,
				operation, target);
			goto failure;
		}
		if (!NormalizePlane(&halfspaces[target].plane, &planes[target],
				&failure))
		{
			SetError(error, failure, operation, target);
			goto failure;
		}
	}
	coordinate_scale = CoordinateScale(bounds);
	point_epsilon = 128.0 * DBL_EPSILON * coordinate_scale;
	for (target = 0U; target < halfspace_count; target++)
	{
		uint32_t equivalent;
		uint32_t second;
		uint32_t point_count = 0U;
		int duplicate = 0;
		uint8_t open = halfspaces[target].open;

		for (equivalent = 0U; equivalent < halfspace_count; equivalent++)
			if (equivalent != target &&
				SamePlane(&planes[target], &planes[equivalent], plane_epsilon))
			{
				if (halfspaces[equivalent].open != 0U)
					open = 1U;
				if (HalfspaceKeyCompare(&halfspaces[equivalent],
						&halfspaces[target]) < 0 ||
					(HalfspaceKeyCompare(&halfspaces[equivalent],
						&halfspaces[target]) == 0 && equivalent < target))
					duplicate = 1;
			}
		if (duplicate)
			continue;
		for (second = 0U; second < halfspace_count; second++)
		{
			uint32_t third;

			if (second == target)
				continue;
			for (third = second + 1U; third < halfspace_count; third++)
			{
				partition_point_t point;

				if (third == target ||
					!IntersectThreePlanes(&planes[target], &planes[second],
						&planes[third], &point) ||
					!PointInside(&point, planes, halfspace_count, bounds))
					continue;
				points[point_count++] = point;
			}
		}
		if (point_count >= 3U)
		{
			sg_rune_compact_partition_polygon_t face;

			memset(&face, 0, sizeof(face));
			if (!CanonicalVertices(points, point_count, &planes[target],
					point_epsilon, allocator, &face.vertices,
					&face.vertex_count, &failure))
			{
				SetError(error, failure, operation, target);
				goto failure;
			}
			if (face.vertex_count >= 3U)
			{
				face.plane = halfspaces[target].plane;
				face.source_plane_index =
					halfspaces[target].source_plane_index;
				face.contributor = halfspaces[target].contributor;
				face.open = open;
				result->faces[result->face_count++] = face;
			}
		}
	}
	allocator->release(allocator->context, points);
	allocator->release(allocator->context, planes);
	qsort(result->faces, (size_t)result->face_count, sizeof(*result->faces),
		CompareFaces);
	SetDerivedBounds(result);
	if (!result->empty && !PolyhedronHasVolume(result))
		result->empty = 1;
	if (result->empty)
	{
		ReleasePolyhedron(result, allocator);
		memset(result, 0, sizeof(*result));
		result->empty = 1;
	}
	SetError(error, SG_RUNE_COMPACT_PARTITION_ERROR_NONE, operation, 0U);
	return 1;

failure:
	if (points)
		allocator->release(allocator->context, points);
	if (planes)
		allocator->release(allocator->context, planes);
	ReleasePolyhedron(result, allocator);
	return 0;
}

static double PolygonScale(
	const sg_rune_compact_partition_polygon_t *polygon)
{
	double scale = 1.0;
	uint32_t vertex;
	uint32_t axis;

	if (polygon->vertex_count == 0U)
		return scale;
	for (vertex = 1U; vertex < polygon->vertex_count; vertex++)
		for (axis = 0U; axis < 3U; axis++)
		{
			double value = fabs((double)polygon->vertices[vertex].value[axis] -
				(double)polygon->vertices[0].value[axis]);

			if (value > scale)
				scale = value;
		}
	return scale;
}

static int CanonicalPolygonCopy(
	const sg_rune_compact_partition_polygon_t *source,
	const partition_allocator_t *allocator,
	sg_rune_compact_partition_polygon_t *result,
	sg_rune_compact_partition_error_code_t *failure)
{
	normalized_plane_t plane;
	partition_point_t *points;
	double epsilon;
	size_t bytes;
	uint32_t vertex;

	memset(result, 0, sizeof(*result));
	if (!source || !source->vertices || source->vertex_count < 3U ||
		source->open > 1U)
	{
		*failure = SG_RUNE_COMPACT_PARTITION_ERROR_DEGENERATE;
		return 0;
	}
	if (!NormalizePlane(&source->plane, &plane, failure))
		return 0;
	if (!CheckedBytes((size_t)source->vertex_count, sizeof(*points), &bytes))
	{
		*failure = SG_RUNE_COMPACT_PARTITION_ERROR_OVERFLOW;
		return 0;
	}
	points = allocator->allocate(allocator->context, bytes);
	if (!points)
	{
		*failure = SG_RUNE_COMPACT_PARTITION_ERROR_OUT_OF_MEMORY;
		return 0;
	}
	epsilon = SG_RUNE_COMPACT_PARTITION_RELATIVE_EPSILON *
		PolygonScale(source);
	for (vertex = 0U; vertex < source->vertex_count; vertex++)
	{
		uint32_t axis;

		for (axis = 0U; axis < 3U; axis++)
		{
			if (!isfinite(source->vertices[vertex].value[axis]))
			{
				allocator->release(allocator->context, points);
				*failure = SG_RUNE_COMPACT_PARTITION_ERROR_NONFINITE;
				return 0;
			}
			points[vertex].value[axis] =
				(double)source->vertices[vertex].value[axis];
		}
		points[vertex].angle = 0.0;
		if (fabs(Dot(plane.normal, points[vertex].value) - plane.distance) >
			PlaneResidualTolerance(&plane, points[vertex].value))
		{
			allocator->release(allocator->context, points);
			*failure = SG_RUNE_COMPACT_PARTITION_ERROR_DEGENERATE;
			return 0;
		}
	}
	result->plane = source->plane;
	result->source_plane_index = source->source_plane_index;
	result->contributor = source->contributor;
	result->open = source->open;
	if (!CanonicalVertices(points, source->vertex_count, &plane, epsilon,
			allocator, &result->vertices, &result->vertex_count, failure))
	{
		allocator->release(allocator->context, points);
		return 0;
	}
	allocator->release(allocator->context, points);
	if (result->vertex_count < 3U)
	{
		ReleasePolygon(result, allocator);
		*failure = SG_RUNE_COMPACT_PARTITION_ERROR_DEGENERATE;
		return 0;
	}
	return 1;
}

static int ClipCanonicalPolygon(
	const sg_rune_compact_partition_polygon_t *source,
	const normalized_plane_t *clip, const partition_allocator_t *allocator,
	sg_rune_compact_partition_polygon_t *result,
	sg_rune_compact_partition_error_code_t *failure)
{
	normalized_plane_t source_plane;
	partition_point_t *points;
	double epsilon;
	size_t capacity;
	size_t bytes;
	uint32_t point_count = 0U;
	uint32_t edge;

	memset(result, 0, sizeof(*result));
	result->plane = source->plane;
	result->source_plane_index = source->source_plane_index;
	result->contributor = source->contributor;
	result->open = source->open;
	if (!source->vertex_count)
		return 1;
	if (source->vertex_count > UINT32_MAX / 2U)
	{
		*failure = SG_RUNE_COMPACT_PARTITION_ERROR_OVERFLOW;
		return 0;
	}
	capacity = (size_t)source->vertex_count * 2U;
	if (capacity < (size_t)source->vertex_count ||
		!CheckedBytes(capacity, sizeof(*points), &bytes))
	{
		*failure = SG_RUNE_COMPACT_PARTITION_ERROR_OVERFLOW;
		return 0;
	}
	points = allocator->allocate(allocator->context, bytes);
	if (!points)
	{
		*failure = SG_RUNE_COMPACT_PARTITION_ERROR_OUT_OF_MEMORY;
		return 0;
	}
	epsilon = SG_RUNE_COMPACT_PARTITION_RELATIVE_EPSILON *
		PolygonScale(source);
	for (edge = 0U; edge < source->vertex_count; edge++)
	{
		const sg_rune_vec3_t *start = &source->vertices[edge];
		const sg_rune_vec3_t *end = &source->vertices[
			(edge + 1U) % source->vertex_count];
		double start_value[3];
		double end_value[3];
		double start_distance;
		double end_distance;
		int start_inside;
		int end_inside;
		uint32_t axis;

		for (axis = 0U; axis < 3U; axis++)
		{
			start_value[axis] = (double)start->value[axis];
			end_value[axis] = (double)end->value[axis];
		}
		start_distance = Dot(clip->normal, start_value) - clip->distance;
		end_distance = Dot(clip->normal, end_value) - clip->distance;
		start_inside = start_distance <=
			PlaneResidualTolerance(clip, start_value);
		end_inside = end_distance <= PlaneResidualTolerance(clip, end_value);
		if (start_inside)
		{
			for (axis = 0U; axis < 3U; axis++)
				points[point_count].value[axis] = start_value[axis];
			points[point_count++].angle = 0.0;
		}
		if (start_inside != end_inside)
		{
			double denominator = start_distance - end_distance;
			double fraction;

			if (fabs(denominator) <= DBL_MIN)
				continue;
			fraction = start_distance / denominator;
			if (fraction < 0.0)
				fraction = 0.0;
			if (fraction > 1.0)
				fraction = 1.0;
			for (axis = 0U; axis < 3U; axis++)
				points[point_count].value[axis] = start_value[axis] +
					fraction * (end_value[axis] - start_value[axis]);
			points[point_count++].angle = 0.0;
		}
	}
	if (!NormalizePlane(&source->plane, &source_plane, failure) ||
		!CanonicalVertices(points, point_count, &source_plane, epsilon,
			allocator, &result->vertices, &result->vertex_count, failure))
	{
		allocator->release(allocator->context, points);
		return 0;
	}
	allocator->release(allocator->context, points);
	return 1;
}

int SG_RuneCompactPartitionDeriveFaces(
	const sg_rune_compact_partition_halfspace_t *halfspaces,
	uint32_t halfspace_count, const sg_rune_bounds_t *bounds,
	const sg_rune_compact_partition_allocator_t *allocator,
	sg_rune_compact_partition_polyhedron_t *polyhedron_out,
	sg_rune_compact_partition_error_t *error_out)
{
	partition_allocator_t selected;
	sg_rune_compact_partition_polyhedron_t result;

	if (!polyhedron_out || !SelectAllocator(allocator, &selected))
	{
		SetError(error_out, SG_RUNE_COMPACT_PARTITION_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_PARTITION_OPERATION_DERIVE_FACES, 0U);
		return 0;
	}
	if (!DeriveFacesInternal(halfspaces, halfspace_count, bounds, &selected,
			&result, error_out,
			SG_RUNE_COMPACT_PARTITION_OPERATION_DERIVE_FACES))
		return 0;
	*polyhedron_out = result;
	return 1;
}

int SG_RuneCompactPartitionClipPolygon(
	const sg_rune_compact_partition_polygon_t *polygon,
	const sg_rune_compact_partition_halfspace_t *clip,
	const sg_rune_compact_partition_allocator_t *allocator,
	sg_rune_compact_partition_polygon_t *polygon_out,
	sg_rune_compact_partition_error_t *error_out)
{
	partition_allocator_t selected;
	sg_rune_compact_partition_polygon_t canonical;
	sg_rune_compact_partition_polygon_t result;
	normalized_plane_t normalized_clip;
	sg_rune_compact_partition_error_code_t failure =
		SG_RUNE_COMPACT_PARTITION_ERROR_NONE;

	if (!polygon || !clip || !polygon_out || clip->open > 1U ||
		!SelectAllocator(allocator, &selected))
	{
		SetError(error_out, SG_RUNE_COMPACT_PARTITION_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_PARTITION_OPERATION_CLIP_POLYGON, 0U);
		return 0;
	}
	if (!NormalizePlane(&clip->plane, &normalized_clip, &failure) ||
		!CanonicalPolygonCopy(polygon, &selected, &canonical, &failure))
	{
		SetError(error_out, failure,
			SG_RUNE_COMPACT_PARTITION_OPERATION_CLIP_POLYGON, 0U);
		return 0;
	}
	if (!ClipCanonicalPolygon(&canonical, &normalized_clip, &selected,
			&result, &failure))
	{
		ReleasePolygon(&canonical, &selected);
		SetError(error_out, failure,
			SG_RUNE_COMPACT_PARTITION_OPERATION_CLIP_POLYGON, 0U);
		return 0;
	}
	ReleasePolygon(&canonical, &selected);
	SetError(error_out, SG_RUNE_COMPACT_PARTITION_ERROR_NONE,
		SG_RUNE_COMPACT_PARTITION_OPERATION_CLIP_POLYGON, 0U);
	*polygon_out = result;
	return 1;
}

int SG_RuneCompactPartitionSubtractPolygon(
	const sg_rune_compact_partition_polygon_t *face,
	const sg_rune_compact_partition_polygon_t *portal,
	const sg_rune_compact_partition_allocator_t *allocator,
	sg_rune_compact_partition_subtraction_t *subtraction_out,
	sg_rune_compact_partition_error_t *error_out)
{
	partition_allocator_t selected;
	sg_rune_compact_partition_polygon_t canonical_face;
	sg_rune_compact_partition_polygon_t canonical_portal;
	sg_rune_compact_partition_polygon_t remaining;
	sg_rune_compact_partition_subtraction_t result;
	normalized_plane_t face_plane;
	normalized_plane_t portal_plane;
	sg_rune_compact_partition_error_code_t failure =
		SG_RUNE_COMPACT_PARTITION_ERROR_NONE;
	size_t bytes;
	double parallel;
	uint32_t edge;

	memset(&canonical_face, 0, sizeof(canonical_face));
	memset(&canonical_portal, 0, sizeof(canonical_portal));
	memset(&remaining, 0, sizeof(remaining));
	memset(&result, 0, sizeof(result));
	if (!face || !portal || !subtraction_out ||
		!SelectAllocator(allocator, &selected))
	{
		SetError(error_out, SG_RUNE_COMPACT_PARTITION_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_PARTITION_OPERATION_SUBTRACT_POLYGON, 0U);
		return 0;
	}
	if (!CanonicalPolygonCopy(face, &selected, &canonical_face, &failure) ||
		!CanonicalPolygonCopy(portal, &selected, &canonical_portal, &failure) ||
		!NormalizePlane(&canonical_face.plane, &face_plane, &failure) ||
		!NormalizePlane(&canonical_portal.plane, &portal_plane, &failure))
		goto failure;
	parallel = fabs(Dot(face_plane.normal, portal_plane.normal));
	if (fabs(1.0 - parallel) > 1.0e-10)
	{
		failure = SG_RUNE_COMPACT_PARTITION_ERROR_DEGENERATE;
		goto failure;
	}
	for (edge = 0U; edge < canonical_portal.vertex_count; edge++)
	{
		double value[3];
		uint32_t axis;

		for (axis = 0U; axis < 3U; axis++)
			value[axis] = (double)canonical_portal.vertices[edge].value[axis];
		if (fabs(Dot(face_plane.normal, value) - face_plane.distance) >
			PlaneResidualTolerance(&face_plane, value))
		{
			failure = SG_RUNE_COMPACT_PARTITION_ERROR_DEGENERATE;
			goto failure;
		}
	}
	if (!CheckedBytes((size_t)canonical_portal.vertex_count,
			sizeof(*result.remainders), &bytes))
	{
		failure = SG_RUNE_COMPACT_PARTITION_ERROR_OVERFLOW;
		goto failure;
	}
	result.remainders = selected.allocate(selected.context, bytes);
	if (!result.remainders)
	{
		failure = SG_RUNE_COMPACT_PARTITION_ERROR_OUT_OF_MEMORY;
		goto failure;
	}
	memset(result.remainders, 0, bytes);
	remaining = canonical_face;
	memset(&canonical_face, 0, sizeof(canonical_face));
	for (edge = 0U; edge < canonical_portal.vertex_count; edge++)
	{
		const sg_rune_vec3_t *start = &canonical_portal.vertices[edge];
		const sg_rune_vec3_t *end = &canonical_portal.vertices[
			(edge + 1U) % canonical_portal.vertex_count];
		normalized_plane_t inside_plane;
		normalized_plane_t outside_plane;
		sg_rune_compact_partition_polygon_t inside;
		sg_rune_compact_partition_polygon_t outside;
		double direction[3];
		double point[3];
		double normal_length;
		uint32_t axis;

		memset(&inside, 0, sizeof(inside));
		memset(&outside, 0, sizeof(outside));
		for (axis = 0U; axis < 3U; axis++)
		{
			direction[axis] = (double)end->value[axis] -
				(double)start->value[axis];
			point[axis] = (double)start->value[axis];
		}
		Cross(direction, portal_plane.normal, inside_plane.normal);
		normal_length = sqrt(Dot(inside_plane.normal, inside_plane.normal));
		if (!isfinite(normal_length) || normal_length <= DBL_MIN)
		{
			failure = SG_RUNE_COMPACT_PARTITION_ERROR_DEGENERATE;
			goto failure;
		}
		for (axis = 0U; axis < 3U; axis++)
		{
			inside_plane.normal[axis] /= normal_length;
			outside_plane.normal[axis] = -inside_plane.normal[axis];
		}
		inside_plane.distance = Dot(inside_plane.normal, point);
		outside_plane.distance = -inside_plane.distance;
		if (!ClipCanonicalPolygon(&remaining, &outside_plane, &selected,
				&outside, &failure) ||
			!ClipCanonicalPolygon(&remaining, &inside_plane, &selected,
				&inside, &failure))
		{
			ReleasePolygon(&outside, &selected);
			ReleasePolygon(&inside, &selected);
			goto failure;
		}
		if (outside.vertex_count)
			result.remainders[result.remainder_count++] = outside;
		ReleasePolygon(&remaining, &selected);
		remaining = inside;
		if (!remaining.vertex_count)
			break;
	}
	remaining.plane = portal->plane;
	remaining.source_plane_index = portal->source_plane_index;
	remaining.contributor = portal->contributor;
	remaining.open = portal->open;
	result.consumed = remaining;
	memset(&remaining, 0, sizeof(remaining));
	ReleasePolygon(&canonical_portal, &selected);
	SetError(error_out, SG_RUNE_COMPACT_PARTITION_ERROR_NONE,
		SG_RUNE_COMPACT_PARTITION_OPERATION_SUBTRACT_POLYGON, 0U);
	*subtraction_out = result;
	return 1;

failure:
	ReleasePolygon(&remaining, &selected);
	ReleasePolygon(&canonical_face, &selected);
	ReleasePolygon(&canonical_portal, &selected);
	ReleaseSubtraction(&result, &selected);
	SetError(error_out, failure,
		SG_RUNE_COMPACT_PARTITION_OPERATION_SUBTRACT_POLYGON, 0U);
	return 0;
}

int SG_RuneCompactPartitionIntersectCells(
	const sg_rune_compact_partition_cell_t *left,
	const sg_rune_compact_partition_cell_t *right,
	const sg_rune_compact_partition_allocator_t *allocator,
	sg_rune_compact_partition_polyhedron_t *intersection_out,
	sg_rune_compact_partition_error_t *error_out)
{
	partition_allocator_t selected;
	sg_rune_compact_partition_polyhedron_t left_shape;
	sg_rune_compact_partition_polyhedron_t right_shape;
	sg_rune_compact_partition_polyhedron_t result;
	sg_rune_compact_partition_halfspace_t *combined = NULL;
	sg_rune_bounds_t bounds;
	uint32_t combined_count;
	size_t bytes;
	uint32_t axis;

	memset(&left_shape, 0, sizeof(left_shape));
	memset(&right_shape, 0, sizeof(right_shape));
	memset(&result, 0, sizeof(result));
	if (!left || !right || !intersection_out ||
		!SelectAllocator(allocator, &selected))
	{
		SetError(error_out, SG_RUNE_COMPACT_PARTITION_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_PARTITION_OPERATION_INTERSECT_CELLS, 0U);
		return 0;
	}
	if (!DeriveFacesInternal(left->halfspaces, left->halfspace_count,
			&left->bounds, &selected, &left_shape, error_out,
			SG_RUNE_COMPACT_PARTITION_OPERATION_INTERSECT_CELLS) ||
		!DeriveFacesInternal(right->halfspaces, right->halfspace_count,
			&right->bounds, &selected, &right_shape, error_out,
			SG_RUNE_COMPACT_PARTITION_OPERATION_INTERSECT_CELLS))
		goto failure;
	if (left_shape.empty || right_shape.empty)
	{
		SetError(error_out, SG_RUNE_COMPACT_PARTITION_ERROR_DEGENERATE,
			SG_RUNE_COMPACT_PARTITION_OPERATION_INTERSECT_CELLS, 0U);
		goto failure;
	}
	for (axis = 0U; axis < 3U; axis++)
	{
		bounds.mins.value[axis] = fmaxf(left->bounds.mins.value[axis],
			right->bounds.mins.value[axis]);
		bounds.maxs.value[axis] = fminf(left->bounds.maxs.value[axis],
			right->bounds.maxs.value[axis]);
		if (bounds.mins.value[axis] >= bounds.maxs.value[axis])
		{
			result.empty = 1;
			SetError(error_out, SG_RUNE_COMPACT_PARTITION_ERROR_NONE,
				SG_RUNE_COMPACT_PARTITION_OPERATION_INTERSECT_CELLS, 0U);
			*intersection_out = result;
			ReleasePolyhedron(&left_shape, &selected);
			ReleasePolyhedron(&right_shape, &selected);
			return 1;
		}
	}
	if (left->halfspace_count > UINT32_MAX - right->halfspace_count)
	{
		SetError(error_out, SG_RUNE_COMPACT_PARTITION_ERROR_OVERFLOW,
			SG_RUNE_COMPACT_PARTITION_OPERATION_INTERSECT_CELLS, 0U);
		goto failure;
	}
	combined_count = left->halfspace_count + right->halfspace_count;
	if (!CheckedBytes((size_t)combined_count, sizeof(*combined), &bytes))
	{
		SetError(error_out, SG_RUNE_COMPACT_PARTITION_ERROR_OVERFLOW,
			SG_RUNE_COMPACT_PARTITION_OPERATION_INTERSECT_CELLS, 0U);
		goto failure;
	}
	combined = selected.allocate(selected.context, bytes);
	if (!combined)
	{
		SetError(error_out, SG_RUNE_COMPACT_PARTITION_ERROR_OUT_OF_MEMORY,
			SG_RUNE_COMPACT_PARTITION_OPERATION_INTERSECT_CELLS, 0U);
		goto failure;
	}
	memcpy(combined, left->halfspaces,
		(size_t)left->halfspace_count * sizeof(*combined));
	memcpy(combined + left->halfspace_count, right->halfspaces,
		(size_t)right->halfspace_count * sizeof(*combined));
	if (!DeriveFacesInternal(combined, combined_count, &bounds, &selected,
			&result, error_out,
			SG_RUNE_COMPACT_PARTITION_OPERATION_INTERSECT_CELLS))
		goto failure;
	selected.release(selected.context, combined);
	ReleasePolyhedron(&left_shape, &selected);
	ReleasePolyhedron(&right_shape, &selected);
	*intersection_out = result;
	return 1;

failure:
	if (combined)
		selected.release(selected.context, combined);
	ReleasePolyhedron(&result, &selected);
	ReleasePolyhedron(&left_shape, &selected);
	ReleasePolyhedron(&right_shape, &selected);
	return 0;
}

void SG_RuneCompactPartitionPolygonDestroy(
	sg_rune_compact_partition_polygon_t *polygon,
	const sg_rune_compact_partition_allocator_t *allocator)
{
	partition_allocator_t selected;

	if (SelectAllocator(allocator, &selected))
		ReleasePolygon(polygon, &selected);
}

void SG_RuneCompactPartitionPolyhedronDestroy(
	sg_rune_compact_partition_polyhedron_t *polyhedron,
	const sg_rune_compact_partition_allocator_t *allocator)
{
	partition_allocator_t selected;

	if (SelectAllocator(allocator, &selected))
		ReleasePolyhedron(polyhedron, &selected);
}

void SG_RuneCompactPartitionSubtractionDestroy(
	sg_rune_compact_partition_subtraction_t *subtraction,
	const sg_rune_compact_partition_allocator_t *allocator)
{
	partition_allocator_t selected;

	if (SelectAllocator(allocator, &selected))
		ReleaseSubtraction(subtraction, &selected);
}

const char *SG_RuneCompactPartitionErrorString(
	sg_rune_compact_partition_error_code_t code)
{
	switch (code)
	{
	case SG_RUNE_COMPACT_PARTITION_ERROR_NONE: return "none";
	case SG_RUNE_COMPACT_PARTITION_ERROR_INVALID_ARGUMENT:
		return "invalid argument";
	case SG_RUNE_COMPACT_PARTITION_ERROR_NONFINITE: return "non-finite input";
	case SG_RUNE_COMPACT_PARTITION_ERROR_DEGENERATE: return "degenerate input";
	case SG_RUNE_COMPACT_PARTITION_ERROR_OVERFLOW: return "size overflow";
	case SG_RUNE_COMPACT_PARTITION_ERROR_OUT_OF_MEMORY:
		return "out of memory";
	case SG_RUNE_COMPACT_PARTITION_ERROR_CODE_COUNT: break;
	}
	return "unknown partition error";
}
