#include "sg_rune_compact_geometry.h"

#include "sg_rune_compact_builder_owner.h"
#include "sg_rune_compact_geometry_owner.h"
#include "sg_rune_compact_geometry_partition.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define GEOMETRY_STATE UINT32_C(0x47454f4d)
#define GEOMETRY_INDEX_NONE UINT32_MAX
#define GEOMETRY_EPSILON 1.0e-7
#define GEOMETRY_PLANE_EPSILON 1.0e-5

typedef struct geometry_context_s
{
	sg_rune_compact_geometry_allocator_t allocator;
	sg_rune_compact_geometry_error_t *error;
} geometry_context_t;

typedef struct geometry_polygon_s
{
	sg_rune_q8_vec3_t *vertices;
	uint32_t count;
} geometry_polygon_t;

typedef struct geometry_entry_s
{
	uint32_t serial;
	uint32_t owner_region;
	uint32_t low_region;
	uint32_t high_region;
	uint32_t portal_config;
	sg_rune_compact_source_t source;
	sg_rune_binary32_plane_t plane;
	geometry_polygon_t polygon;
	sg_rune_compact_facet_kind_t kind;
	uint8_t portal;
	uint8_t shared;
	uint8_t open;
} geometry_entry_t;

typedef struct geometry_entry_vector_s
{
	geometry_entry_t *values;
	uint32_t count;
	uint32_t capacity;
} geometry_entry_vector_t;

typedef struct geometry_piece_vector_s
{
	sg_rune_compact_partition_polygon_t *values;
	uint32_t count;
	uint32_t capacity;
} geometry_piece_vector_t;

typedef struct geometry_portal_work_s
{
	uint32_t config_index;
	uint32_t low_config;
	uint32_t high_config;
	uint32_t low_face;
	uint32_t high_face;
	uint32_t low_region;
	uint32_t high_region;
	sg_rune_compact_source_t source;
	sg_rune_binary32_plane_t plane;
	geometry_polygon_t polygon;
	sg_rune_stance_validity_t valid_stances;
	sg_rune_portal_continuity_t direction;
	uint8_t open;
} geometry_portal_work_t;

typedef struct geometry_portal_vector_s
{
	geometry_portal_work_t *values;
	uint32_t count;
	uint32_t capacity;
} geometry_portal_vector_t;

typedef struct geometry_halfspace_vector_s
{
	sg_rune_compact_partition_halfspace_t *values;
	uint32_t count;
	uint32_t capacity;
} geometry_halfspace_vector_t;

typedef struct geometry_region_work_s
{
	uint32_t anchor_config;
	uint32_t config_members[2];
	uint32_t member_count;
	uint32_t split_ordinal;
	sg_rune_compact_cell_source_t source;
	sg_rune_q8_bounds_t bounds;
	sg_rune_compact_contents_mask_t contents;
	sg_rune_stance_validity_t valid_stances;
	geometry_halfspace_vector_t halfspaces;
	sg_rune_compact_partition_polyhedron_t polyhedron;
} geometry_region_work_t;

typedef struct geometry_region_vector_s
{
	geometry_region_work_t *values;
	uint32_t count;
	uint32_t capacity;
} geometry_region_vector_t;

struct sg_rune_compact_geometry_s
{
	uint32_t state;
	uint32_t state_inverse;
	const struct sg_rune_compact_geometry_s *self;
	sg_rune_compact_geometry_allocator_t allocator;
	sg_rune_compact_identity_t identity;
	sg_rune_compact_cell_t *cells;
	uint32_t cell_count;
	sg_rune_compact_facet_t *facets;
	uint32_t facet_count;
	sg_rune_compact_incidence_t *incidences;
	uint32_t incidence_count;
	sg_rune_compact_incidence_index_t *cell_incidences;
	uint32_t cell_incidence_count;
	sg_rune_q8_vec3_t *vertices;
	uint32_t vertex_count;
	sg_rune_compact_portal_t *portals;
	uint32_t portal_count;
	sg_rune_compact_geometry_cell_span_t *compact_cells_for_configuration_cell;
	uint32_t compact_cells_for_configuration_cell_count;
	sg_rune_compact_cell_index_t *configuration_cell_compact_cells;
	uint32_t configuration_cell_compact_cell_count;
};

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

static int ConfigureAllocator(
	const sg_rune_compact_geometry_allocator_t *input,
	sg_rune_compact_geometry_allocator_t *output)
{
	if (input == NULL)
	{
		output->context = NULL;
		output->allocate = DefaultAllocate;
		output->release = DefaultRelease;
		return 1;
	}
	if (input->allocate == NULL || input->release == NULL)
		return 0;
	*output = *input;
	return 1;
}

static void ClearError(sg_rune_compact_geometry_error_t *error)
{
	if (error == NULL)
		return;
	error->code = SG_RUNE_COMPACT_GEOMETRY_ERROR_NONE;
	error->domain = SG_RUNE_COMPACT_GEOMETRY_RECORD_RESULT;
	error->record = GEOMETRY_INDEX_NONE;
}

static void SetError(sg_rune_compact_geometry_error_t *error,
	sg_rune_compact_geometry_error_code_t code,
	sg_rune_compact_geometry_record_domain_t domain, uint32_t record)
{
	if (error == NULL || error->code != SG_RUNE_COMPACT_GEOMETRY_ERROR_NONE)
		return;
	error->code = code;
	error->domain = domain;
	error->record = record;
}

static void *Allocate(const geometry_context_t *context, size_t bytes)
{
	void *allocation =
		context->allocator.allocate(context->allocator.context, bytes);

	if (allocation == NULL)
		SetError(context->error, SG_RUNE_COMPACT_GEOMETRY_ERROR_OUT_OF_MEMORY,
			SG_RUNE_COMPACT_GEOMETRY_RECORD_RESULT, GEOMETRY_INDEX_NONE);
	return allocation;
}

static void Release(const geometry_context_t *context, void *allocation)
{
	if (allocation != NULL)
		context->allocator.release(context->allocator.context, allocation);
}

/* Forward declarations kept together because region construction precedes
 * final compact-array conversion in this file. */
static int BoundsToQ8(const sg_rune_bounds_t *bounds,
	sg_rune_q8_bounds_t *compact_bounds);
static int CellOrderLess(const sg_configuration_space_t *configuration,
	uint32_t left, uint32_t right);
static int SourceFromFace(const sg_configuration_face_t *face,
	const sg_configuration_cell_t *cell, const sg_bsp_world_t *world,
	sg_rune_compact_source_t *source_out);
static void SetPartitionError(const geometry_context_t *context,
	const sg_rune_compact_partition_error_t *partition_error,
	sg_rune_compact_geometry_record_domain_t domain, uint32_t record);
static int PartitionPolygonCopyFromPortal(
	const sg_configuration_space_t *configuration,
	const sg_configuration_portal_t *portal, uint32_t portal_index,
	sg_rune_compact_partition_polygon_t *polygon);
static int ConvertPartitionPolygon(const geometry_context_t *context,
	const sg_rune_compact_partition_polygon_t *source,
	const sg_rune_binary32_plane_t *plane, geometry_polygon_t *destination);
static int AllocateArray(const geometry_context_t *context, uint32_t count,
	size_t element_size, void **pointer_out,
	sg_rune_compact_geometry_record_domain_t domain);

static int SizeForCount(uint32_t count, size_t element_size, size_t *size_out)
{
	if (element_size == 0U || (size_t)count > SIZE_MAX / element_size)
		return 0;
	*size_out = (size_t)count * element_size;
	return 1;
}

static int AddU32(uint32_t left, uint32_t right, uint32_t *result)
{
	if (right > UINT32_MAX - left)
		return 0;
	*result = left + right;
	return 1;
}

static uint32_t FloatBits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	if (bits == UINT32_C(0x80000000))
		return 0U;
	return bits;
}

static float BitsFloat(uint32_t bits)
{
	float value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

static int RangeWithin(uint32_t first, uint32_t count, uint32_t total)
{
	return first <= total && count <= total - first;
}

static int FiniteVector(const float values[3])
{
	return values != NULL && isfinite(values[0]) && isfinite(values[1]) &&
		isfinite(values[2]);
}

static int ConfigurationPlaneFinite(const sg_configuration_plane_t *plane)
{
	return plane != NULL && FiniteVector(plane->normal) &&
		isfinite(plane->distance) &&
		(fabs((double)plane->normal[0]) + fabs((double)plane->normal[1]) +
			fabs((double)plane->normal[2]) > 0.0);
}

static int CompactPlaneFinite(const sg_rune_binary32_plane_t *plane)
{
	uint32_t axis;
	int nonzero = 0;

	if (plane == NULL ||
		(plane->distance_bits & UINT32_C(0x7f800000)) ==
			UINT32_C(0x7f800000) ||
		plane->distance_bits == UINT32_C(0x80000000))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
	{
		const uint32_t bits = plane->normal_bits[axis];

		if ((bits & UINT32_C(0x7f800000)) == UINT32_C(0x7f800000) ||
			bits == UINT32_C(0x80000000))
			return 0;
		if ((bits & UINT32_C(0x7fffffff)) != 0U)
			nonzero = 1;
	}
	return nonzero;
}

static void ConfigurationPlaneToCompact(const sg_configuration_plane_t *source,
	sg_rune_binary32_plane_t *destination)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		destination->normal_bits[axis] = FloatBits(source->normal[axis]);
	destination->distance_bits = FloatBits(source->distance);
}

static void FlipCompactPlane(sg_rune_binary32_plane_t *plane)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		plane->normal_bits[axis] = FloatBits(-BitsFloat(plane->normal_bits[axis]));
	plane->distance_bits = FloatBits(-BitsFloat(plane->distance_bits));
}

static int NormalizeConfigurationPlane(const sg_configuration_plane_t *plane,
	double normal[3], double *distance)
{
	double scale;
	double length;
	uint32_t axis;

	if (!ConfigurationPlaneFinite(plane))
		return 0;
	scale = fmax(fabs((double)plane->normal[0]),
		fmax(fabs((double)plane->normal[1]), fabs((double)plane->normal[2])));
	for (axis = 0U; axis < 3U; axis++)
		normal[axis] = (double)plane->normal[axis] / scale;
	length = sqrt(normal[0] * normal[0] + normal[1] * normal[1] +
		normal[2] * normal[2]);
	if (!(length > 0.0) || !isfinite(length))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		normal[axis] /= length;
	*distance = ((double)plane->distance / scale) / length;
	return isfinite(*distance);
}

static int PlanesEquivalent(const sg_configuration_plane_t *left,
	const sg_configuration_plane_t *right)
{
	double left_normal[3], right_normal[3];
	double left_distance, right_distance;
	double same_error = 0.0;
	double opposite_error = 0.0;
	uint32_t axis;

	if (!NormalizeConfigurationPlane(left, left_normal, &left_distance) ||
		!NormalizeConfigurationPlane(right, right_normal, &right_distance))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
	{
		same_error = fmax(same_error,
			fabs(left_normal[axis] - right_normal[axis]));
		opposite_error = fmax(opposite_error,
			fabs(left_normal[axis] + right_normal[axis]));
	}
	same_error = fmax(same_error, fabs(left_distance - right_distance));
	opposite_error = fmax(opposite_error,
		fabs(left_distance + right_distance));
	return same_error <= GEOMETRY_PLANE_EPSILON ||
		opposite_error <= GEOMETRY_PLANE_EPSILON;
}

static double ConfigurationPlaneSigned(const sg_configuration_plane_t *plane,
	const sg_rune_vec3_t *point)
{
	double normal[3];
	double distance;

	if (!NormalizeConfigurationPlane(plane, normal, &distance))
		return NAN;
	return normal[0] * (double)point->value[0] +
		normal[1] * (double)point->value[1] +
		normal[2] * (double)point->value[2] - distance;
}

/* The configuration builder's protocol lattice is exact at 1/8 units.  The
 * bounded nearest check only absorbs float serialization noise from a lattice
 * value; it is intentionally far below one lattice unit. */
static int ToQ8(float value, int32_t *output)
{
	double scaled;
	double fraction;
	double rounded;

	if (!isfinite(value) || output == NULL)
		return 0;
	scaled = (double)value * 8.0;
	if (!isfinite(scaled) ||
		scaled < (double)INT32_MIN - 0.5 ||
		scaled > (double)INT32_MAX + 0.5)
		return 0;
	rounded = floor(scaled);
	fraction = scaled - rounded;
	if (fraction > 0.5 ||
		(fraction == 0.5 && fmod(fabs(rounded), 2.0) == 1.0))
		rounded += 1.0;
	if (rounded < (double)INT32_MIN || rounded > (double)INT32_MAX ||
		fabs(scaled - rounded) > SG_RUNE_COMPACT_GEOMETRY_Q8_RESIDUE_LIMIT)
		return 0;
	*output = (int32_t)rounded;
	return 1;
}

static int Q8Equal(const sg_rune_q8_vec3_t *left,
	const sg_rune_q8_vec3_t *right)
{
	return left->value[0] == right->value[0] &&
		left->value[1] == right->value[1] &&
		left->value[2] == right->value[2];
}

static int Q8Compare(const sg_rune_q8_vec3_t *left,
	const sg_rune_q8_vec3_t *right)
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

static uint32_t DominantAxis(const sg_rune_binary32_plane_t *plane)
{
	double largest = fabs((double)BitsFloat(plane->normal_bits[0]));
	uint32_t axis = 0U;
	uint32_t candidate;

	for (candidate = 1U; candidate < 3U; candidate++)
	{
		const double value = fabs((double)BitsFloat(plane->normal_bits[candidate]));

		if (value > largest)
		{
			largest = value;
			axis = candidate;
		}
	}
	return axis;
}

static long double CrossDot(const sg_rune_binary32_plane_t *plane,
	const sg_rune_q8_vec3_t *origin, const sg_rune_q8_vec3_t *left,
	const sg_rune_q8_vec3_t *right)
{
	long double normal[3];
	long double left_delta[3], right_delta[3];
	long double cross[3];
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
	{
		normal[axis] = (long double)BitsFloat(plane->normal_bits[axis]);
		left_delta[axis] = (long double)left->value[axis] -
			(long double)origin->value[axis];
		right_delta[axis] = (long double)right->value[axis] -
			(long double)origin->value[axis];
	}
	cross[0] = left_delta[1] * right_delta[2] -
		left_delta[2] * right_delta[1];
	cross[1] = left_delta[2] * right_delta[0] -
		left_delta[0] * right_delta[2];
	cross[2] = left_delta[0] * right_delta[1] -
		left_delta[1] * right_delta[0];
	return cross[0] * normal[0] + cross[1] * normal[1] + cross[2] * normal[2];
}

static int PointOnCompactPlane(const sg_rune_binary32_plane_t *plane,
	const sg_rune_q8_vec3_t *point)
{
	double residual = -(double)BitsFloat(plane->distance_bits) * 8.0;
	double sum = 0.0;
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
	{
		const double normal = (double)BitsFloat(plane->normal_bits[axis]);

		residual += normal * (double)point->value[axis];
		sum += fabs(normal);
	}
	return fabs(residual) <= 0.5 * sum + 64.0 * DBL_EPSILON * (sum + 1.0);
}

static void RotateMinimum(geometry_polygon_t *polygon)
{
	uint32_t first = 0U;
	uint32_t index;

	for (index = 1U; index < polygon->count; index++)
		if (Q8Compare(&polygon->vertices[index], &polygon->vertices[first]) < 0)
			first = index;
	while (first != 0U)
	{
		sg_rune_q8_vec3_t value = polygon->vertices[0];

		memmove(&polygon->vertices[0], &polygon->vertices[1],
			(size_t)(polygon->count - 1U) * sizeof(polygon->vertices[0]));
		polygon->vertices[polygon->count - 1U] = value;
		first--;
	}
}

static int CanonicalizePolygon(geometry_polygon_t *polygon,
	const sg_rune_binary32_plane_t *plane)
{
	double center[3] = { 0.0, 0.0, 0.0 };
	const uint32_t axis = DominantAxis(plane);
	const uint32_t u = (axis + 1U) % 3U;
	const uint32_t v = (axis + 2U) % 3U;
	uint32_t index;

	if (polygon == NULL || plane == NULL || polygon->vertices == NULL ||
		polygon->count < 3U || !CompactPlaneFinite(plane))
		return 0;
	{
		uint32_t output = 0U;

		for (index = 0U; index < polygon->count; index++)
		{
			if (output != 0U &&
				Q8Equal(&polygon->vertices[output - 1U], &polygon->vertices[index]))
				continue;
			polygon->vertices[output++] = polygon->vertices[index];
		}
		if (output > 1U && Q8Equal(&polygon->vertices[0],
			&polygon->vertices[output - 1U]))
			output--;
		polygon->count = output;
	}
	for (;;)
	{
		int removed = 0;

		if (polygon->count < 3U)
			return 0;
		for (index = 0U; index < polygon->count; index++)
		{
			const uint32_t previous = index == 0U ? polygon->count - 1U : index - 1U;
			const uint32_t next = (index + 1U) % polygon->count;
			const long double turn = CrossDot(plane, &polygon->vertices[index],
				&polygon->vertices[next], &polygon->vertices[previous]);

			if (turn == 0.0L)
			{
				memmove(&polygon->vertices[index], &polygon->vertices[index + 1U],
					(size_t)(polygon->count - index - 1U) *
						sizeof(polygon->vertices[0]));
				polygon->count--;
				removed = 1;
				break;
			}
		}
		if (!removed)
			break;
	}
	for (index = 0U; index < polygon->count; index++)
	{
		center[0] += (double)polygon->vertices[index].value[0];
		center[1] += (double)polygon->vertices[index].value[1];
		center[2] += (double)polygon->vertices[index].value[2];
		if (!PointOnCompactPlane(plane, &polygon->vertices[index]))
			return 0;
	}
	for (index = 0U; index < 3U; index++)
		center[index] /= (double)polygon->count;
	for (index = 1U; index < polygon->count; index++)
	{
		sg_rune_q8_vec3_t value = polygon->vertices[index];
		const double angle = atan2((double)value.value[v] - center[v],
			(double)value.value[u] - center[u]);
		uint32_t insert = index;

		while (insert != 0U)
		{
			const sg_rune_q8_vec3_t *prior = &polygon->vertices[insert - 1U];
			const double prior_angle = atan2(
				(double)prior->value[v] - center[v],
				(double)prior->value[u] - center[u]);

			if (prior_angle < angle ||
				(prior_angle == angle && Q8Compare(&value, prior) >= 0))
				break;
			polygon->vertices[insert] = *prior;
			insert--;
		}
		polygon->vertices[insert] = value;
	}
	if (BitsFloat(plane->normal_bits[axis]) < 0.0f)
		for (index = 0U; index < polygon->count / 2U; index++)
		{
			const uint32_t opposite = polygon->count - index - 1U;
			sg_rune_q8_vec3_t value = polygon->vertices[index];

			polygon->vertices[index] = polygon->vertices[opposite];
			polygon->vertices[opposite] = value;
		}
	RotateMinimum(polygon);
	for (index = 0U; index < polygon->count; index++)
	{
		const uint32_t previous = index == 0U ? polygon->count - 1U : index - 1U;
		const uint32_t next = (index + 1U) % polygon->count;
		const long double turn = CrossDot(plane, &polygon->vertices[index],
			&polygon->vertices[next], &polygon->vertices[previous]);

		if (!(turn > 0.0L))
			return 0;
	}
	return 1;
}

static void PolygonRelease(const geometry_context_t *context,
	geometry_polygon_t *polygon)
{
	if (polygon == NULL)
		return;
	Release(context, polygon->vertices);
	polygon->vertices = NULL;
	polygon->count = 0U;
}

static int PolygonFromFloat(const geometry_context_t *context,
	const sg_rune_vec3_t *vertices, uint32_t count,
	const sg_rune_binary32_plane_t *plane, geometry_polygon_t *polygon)
{
	size_t bytes;
	uint32_t vertex;
	uint32_t axis;

	memset(polygon, 0, sizeof(*polygon));
	if (vertices == NULL || count < 3U ||
		!SizeForCount(count, sizeof(*polygon->vertices), &bytes))
		return 0;
	polygon->vertices = Allocate(context, bytes);
	if (polygon->vertices == NULL)
		return 0;
	for (vertex = 0U; vertex < count; vertex++)
		for (axis = 0U; axis < 3U; axis++)
			if (!ToQ8(vertices[vertex].value[axis],
				&polygon->vertices[vertex].value[axis]))
			{
				PolygonRelease(context, polygon);
				return 0;
			}
	polygon->count = count;
	if (!CanonicalizePolygon(polygon, plane))
	{
		PolygonRelease(context, polygon);
		return 0;
	}
	return 1;
}

static int PolygonCopy(const geometry_context_t *context,
	const geometry_polygon_t *source, geometry_polygon_t *destination)
{
	size_t bytes;

	memset(destination, 0, sizeof(*destination));
	if (source->count == 0U)
		return 1;
	if (!SizeForCount(source->count, sizeof(*source->vertices), &bytes))
		return 0;
	destination->vertices = Allocate(context, bytes);
	if (destination->vertices == NULL)
		return 0;
	memcpy(destination->vertices, source->vertices, bytes);
	destination->count = source->count;
	return 1;
}

static int PolygonSetEqual(const geometry_polygon_t *left,
	const geometry_polygon_t *right)
{
	uint32_t index;

	if (left->count != right->count)
		return 0;
	for (index = 0U; index < left->count; index++)
	{
		uint32_t other;
		int found = 0;

		for (other = 0U; other < right->count; other++)
			if (Q8Equal(&left->vertices[index], &right->vertices[other]))
			{
				found = 1;
				break;
			}
		if (!found)
			return 0;
	}
	return 1;
}

static void SetPartitionAllocator(const geometry_context_t *context,
	sg_rune_compact_partition_allocator_t *allocator)
{
	allocator->context = context->allocator.context;
	allocator->allocate = context->allocator.allocate;
	allocator->release = context->allocator.release;
}

static void PartitionPolygonRelease(const geometry_context_t *context,
	sg_rune_compact_partition_polygon_t *polygon)
{
	sg_rune_compact_partition_allocator_t allocator;

	if (polygon == NULL)
		return;
	SetPartitionAllocator(context, &allocator);
	SG_RuneCompactPartitionPolygonDestroy(polygon, &allocator);
}

static void PartitionPieceVectorRelease(const geometry_context_t *context,
	geometry_piece_vector_t *pieces)
{
	uint32_t index;

	if (pieces == NULL)
		return;
	for (index = 0U; index < pieces->count; index++)
		PartitionPolygonRelease(context, &pieces->values[index]);
	Release(context, pieces->values);
	memset(pieces, 0, sizeof(*pieces));
}

static int PartitionPieceVectorReserve(const geometry_context_t *context,
	geometry_piece_vector_t *pieces, uint32_t required)
{
	sg_rune_compact_partition_polygon_t *grown;
	size_t bytes;
	uint32_t capacity;

	if (required <= pieces->capacity)
		return 1;
	capacity = pieces->capacity == 0U ? 4U : pieces->capacity;
	while (capacity < required)
	{
		if (capacity > SG_RUNE_COMPACT_MAX_FACETS / 2U)
		{
			capacity = SG_RUNE_COMPACT_MAX_FACETS;
			break;
		}
		capacity *= 2U;
	}
	if (!SizeForCount(capacity, sizeof(*grown), &bytes))
		return 0;
	grown = (sg_rune_compact_partition_polygon_t *)Allocate(context, bytes);
	if (grown == NULL)
		return 0;
	if (pieces->count != 0U)
		memcpy(grown, pieces->values,
			(size_t)pieces->count * sizeof(*grown));
	Release(context, pieces->values);
	pieces->values = grown;
	pieces->capacity = capacity;
	return 1;
}

static int PartitionPieceVectorPush(const geometry_context_t *context,
	geometry_piece_vector_t *pieces,
	sg_rune_compact_partition_polygon_t *polygon)
{
	uint32_t required;

	if (pieces->count == UINT32_MAX || !AddU32(pieces->count, 1U, &required) ||
		required > SG_RUNE_COMPACT_MAX_FACETS ||
		!PartitionPieceVectorReserve(context, pieces, required))
		return 0;
	pieces->values[pieces->count] = *polygon;
	memset(polygon, 0, sizeof(*polygon));
	pieces->count = required;
	return 1;
}

static void EntryVectorRelease(const geometry_context_t *context,
	geometry_entry_vector_t *entries)
{
	uint32_t index;

	if (entries == NULL)
		return;
	for (index = 0U; index < entries->count; index++)
		PolygonRelease(context, &entries->values[index].polygon);
	Release(context, entries->values);
	memset(entries, 0, sizeof(*entries));
}

static int EntryVectorReserve(const geometry_context_t *context,
	geometry_entry_vector_t *entries, uint32_t required)
{
	geometry_entry_t *grown;
	size_t bytes;
	uint32_t capacity;

	if (required <= entries->capacity)
		return 1;
	capacity = entries->capacity == 0U ? 8U : entries->capacity;
	while (capacity < required)
	{
		if (capacity > SG_RUNE_COMPACT_MAX_FACETS / 2U)
		{
			capacity = SG_RUNE_COMPACT_MAX_FACETS;
			break;
		}
		capacity *= 2U;
	}
	if (!SizeForCount(capacity, sizeof(*grown), &bytes))
		return 0;
	grown = (geometry_entry_t *)Allocate(context, bytes);
	if (grown == NULL)
		return 0;
	if (entries->count != 0U)
		memcpy(grown, entries->values,
			(size_t)entries->count * sizeof(*grown));
	Release(context, entries->values);
	entries->values = grown;
	entries->capacity = capacity;
	return 1;
}

static int EntryVectorPush(const geometry_context_t *context,
	geometry_entry_vector_t *entries, geometry_entry_t *entry)
{
	uint32_t required;

	if (entries->count == UINT32_MAX || !AddU32(entries->count, 1U, &required) ||
		required > SG_RUNE_COMPACT_MAX_FACETS ||
		!EntryVectorReserve(context, entries, required))
		return 0;
	entries->values[entries->count] = *entry;
	memset(entry, 0, sizeof(*entry));
	entries->count = required;
	return 1;
}

static void PortalVectorRelease(const geometry_context_t *context,
	geometry_portal_vector_t *portals)
{
	uint32_t index;

	if (portals == NULL)
		return;
	for (index = 0U; index < portals->count; index++)
		PolygonRelease(context, &portals->values[index].polygon);
	Release(context, portals->values);
	memset(portals, 0, sizeof(*portals));
}

static int PortalVectorReserve(const geometry_context_t *context,
	geometry_portal_vector_t *portals, uint32_t required)
{
	geometry_portal_work_t *grown;
	size_t bytes;
	uint32_t capacity;

	if (required <= portals->capacity)
		return 1;
	capacity = portals->capacity == 0U ? 4U : portals->capacity;
	while (capacity < required)
	{
		if (capacity > SG_RUNE_COMPACT_MAX_PORTALS / 2U)
		{
			capacity = SG_RUNE_COMPACT_MAX_PORTALS;
			break;
		}
		capacity *= 2U;
	}
	if (!SizeForCount(capacity, sizeof(*grown), &bytes))
		return 0;
	grown = (geometry_portal_work_t *)Allocate(context, bytes);
	if (grown == NULL)
		return 0;
	if (portals->count != 0U)
		memcpy(grown, portals->values,
			(size_t)portals->count * sizeof(*grown));
	Release(context, portals->values);
	portals->values = grown;
	portals->capacity = capacity;
	return 1;
}

static int PortalVectorPush(const geometry_context_t *context,
	geometry_portal_vector_t *portals, geometry_portal_work_t *portal)
{
	uint32_t required;

	if (portals->count == UINT32_MAX ||
		!AddU32(portals->count, 1U, &required) ||
		required > SG_RUNE_COMPACT_MAX_PORTALS ||
		!PortalVectorReserve(context, portals, required))
		return 0;
	portals->values[portals->count] = *portal;
	memset(portal, 0, sizeof(*portal));
	portals->count = required;
	return 1;
}

static void HalfspaceVectorRelease(const geometry_context_t *context,
	geometry_halfspace_vector_t *halfspaces)
{
	if (halfspaces == NULL)
		return;
	Release(context, halfspaces->values);
	memset(halfspaces, 0, sizeof(*halfspaces));
}

static int HalfspaceVectorReserve(const geometry_context_t *context,
	geometry_halfspace_vector_t *halfspaces, uint32_t required)
{
	sg_rune_compact_partition_halfspace_t *grown;
	size_t bytes;
	uint32_t capacity;

	if (required <= halfspaces->capacity)
		return 1;
	capacity = halfspaces->capacity == 0U ? 8U : halfspaces->capacity;
	while (capacity < required)
	{
		if (capacity > SG_RUNE_COMPACT_MAX_FACETS / 2U)
		{
			capacity = SG_RUNE_COMPACT_MAX_FACETS;
			break;
		}
		capacity *= 2U;
	}
	if (!SizeForCount(capacity, sizeof(*grown), &bytes))
		return 0;
	grown = (sg_rune_compact_partition_halfspace_t *)Allocate(context, bytes);
	if (grown == NULL)
		return 0;
	if (halfspaces->count != 0U)
		memcpy(grown, halfspaces->values,
			(size_t)halfspaces->count * sizeof(*grown));
	Release(context, halfspaces->values);
	halfspaces->values = grown;
	halfspaces->capacity = capacity;
	return 1;
}

static int HalfspaceVectorPush(const geometry_context_t *context,
	geometry_halfspace_vector_t *halfspaces,
	const sg_rune_compact_partition_halfspace_t *value)
{
	uint32_t required;

	if (halfspaces->count == UINT32_MAX ||
		!AddU32(halfspaces->count, 1U, &required) ||
		required > SG_RUNE_COMPACT_MAX_FACETS ||
		!HalfspaceVectorReserve(context, halfspaces, required))
		return 0;
	halfspaces->values[halfspaces->count] = *value;
	halfspaces->count = required;
	return 1;
}

static int HalfspaceVectorCopy(const geometry_context_t *context,
	const geometry_halfspace_vector_t *source,
	geometry_halfspace_vector_t *destination)
{
	size_t bytes;

	memset(destination, 0, sizeof(*destination));
	if (source->count == 0U)
		return 1;
	if (!SizeForCount(source->count, sizeof(*source->values), &bytes))
		return 0;
	destination->values = (sg_rune_compact_partition_halfspace_t *)Allocate(
		context, bytes);
	if (destination->values == NULL)
		return 0;
	memcpy(destination->values, source->values, bytes);
	destination->count = source->count;
	destination->capacity = source->count;
	return 1;
}

static void RegionVectorRelease(const geometry_context_t *context,
	geometry_region_vector_t *regions)
{
	uint32_t index;
	sg_rune_compact_partition_allocator_t allocator;

	if (regions == NULL)
		return;
	SetPartitionAllocator(context, &allocator);
	for (index = 0U; index < regions->count; index++)
	{
		HalfspaceVectorRelease(context, &regions->values[index].halfspaces);
		SG_RuneCompactPartitionPolyhedronDestroy(&regions->values[index].polyhedron,
			&allocator);
	}
	Release(context, regions->values);
	memset(regions, 0, sizeof(*regions));
}

static int RegionVectorReserve(const geometry_context_t *context,
	geometry_region_vector_t *regions, uint32_t required)
{
	geometry_region_work_t *grown;
	size_t bytes;
	uint32_t capacity;

	if (required <= regions->capacity)
		return 1;
	capacity = regions->capacity == 0U ? 4U : regions->capacity;
	while (capacity < required)
	{
		if (capacity > SG_RUNE_COMPACT_MAX_CELLS / 2U)
		{
			capacity = SG_RUNE_COMPACT_MAX_CELLS;
			break;
		}
		capacity *= 2U;
	}
	if (!SizeForCount(capacity, sizeof(*grown), &bytes))
		return 0;
	grown = (geometry_region_work_t *)Allocate(context, bytes);
	if (grown == NULL)
		return 0;
	if (regions->count != 0U)
		memcpy(grown, regions->values,
			(size_t)regions->count * sizeof(*grown));
	Release(context, regions->values);
	regions->values = grown;
	regions->capacity = capacity;
	return 1;
}

static int RegionVectorPush(const geometry_context_t *context,
	geometry_region_vector_t *regions, geometry_region_work_t *region)
{
	uint32_t required;

	if (regions->count == UINT32_MAX || !AddU32(regions->count, 1U, &required) ||
		required > SG_RUNE_COMPACT_MAX_CELLS ||
		!RegionVectorReserve(context, regions, required))
		return 0;
	regions->values[regions->count] = *region;
	memset(region, 0, sizeof(*region));
	regions->count = required;
	return 1;
}

static void FlipPartitionHalfspace(
	const sg_rune_compact_partition_halfspace_t *source,
	sg_rune_compact_partition_halfspace_t *destination)
{
	uint32_t axis;

	*destination = *source;
	for (axis = 0U; axis < 3U; axis++)
		destination->plane.normal[axis] = -destination->plane.normal[axis];
	destination->plane.distance = -destination->plane.distance;
	destination->plane.reversed ^= 1U;
	destination->open ^= 1U;
}

static int BuildCellHalfspaces(const geometry_context_t *context,
	const sg_configuration_space_t *configuration, uint32_t cell_index,
	geometry_halfspace_vector_t *halfspaces)
{
	const sg_configuration_cell_t *cell = &configuration->cells[cell_index];
	uint32_t offset;

	memset(halfspaces, 0, sizeof(*halfspaces));
	for (offset = 0U; offset < cell->face_count; offset++)
	{
		sg_rune_compact_partition_halfspace_t value;
		const uint32_t face_index = cell->first_face + offset;

		memset(&value, 0, sizeof(value));
		value.plane = configuration->faces[face_index].plane;
		value.source_plane_index = face_index;
		value.contributor = cell_index;
		value.open = (uint8_t)(configuration->faces[face_index].plane.source_kind ==
			SG_CONFIGURATION_PLANE_EXPANDED_BRUSH &&
			configuration->faces[face_index].plane.reversed != 0U);
		if (!HalfspaceVectorPush(context, halfspaces, &value))
		{
			HalfspaceVectorRelease(context, halfspaces);
			SetError(context->error, SG_RUNE_COMPACT_GEOMETRY_ERROR_OVERFLOW,
				SG_RUNE_COMPACT_GEOMETRY_RECORD_CELL, cell_index);
			return 0;
		}
	}
	return 1;
}

static int BuildOverlapHalfspaces(const geometry_context_t *context,
	const sg_configuration_space_t *configuration, uint32_t overlap_index,
	geometry_halfspace_vector_t *halfspaces)
{
	const sg_configuration_stance_overlap_t *overlap =
		&configuration->stance_overlaps[overlap_index];
	uint32_t offset;

	memset(halfspaces, 0, sizeof(*halfspaces));
	for (offset = 0U; offset < overlap->face_count; offset++)
	{
		const uint32_t face_index = overlap->first_face + offset;
		const sg_configuration_face_t *face = &configuration->faces[face_index];
		sg_rune_compact_partition_halfspace_t value;

		memset(&value, 0, sizeof(value));
		value.plane = face->plane;
		value.source_plane_index = face_index;
		value.contributor = overlap->standing_cell;
		value.open = (uint8_t)(face->plane.source_kind ==
			SG_CONFIGURATION_PLANE_EXPANDED_BRUSH && face->plane.reversed != 0U);
		if (!HalfspaceVectorPush(context, halfspaces, &value))
		{
			HalfspaceVectorRelease(context, halfspaces);
			SetError(context->error, SG_RUNE_COMPACT_GEOMETRY_ERROR_OVERFLOW,
				SG_RUNE_COMPACT_GEOMETRY_RECORD_CELL, overlap_index);
			return 0;
		}
	}
	return 1;
}

static int RegionSourceFromConfiguration(const sg_configuration_space_t *configuration,
	const sg_bsp_world_t *world, uint32_t anchor_config, uint32_t split_ordinal,
	sg_rune_compact_cell_source_t *source_out)
{
	const sg_configuration_cell_t *cell = &configuration->cells[anchor_config];

	if (cell->bsp_leaf.index >= world->leaf_count ||
		cell->bsp_area.index >= world->area_count)
		return 0;
	memset(source_out, 0, sizeof(*source_out));
	source_out->model = 0U;
	source_out->leaf = cell->bsp_leaf.index;
	source_out->area = cell->bsp_area.index;
	source_out->cluster = cell->bsp_cluster.index == UINT32_MAX ?
		-1 : (int32_t)cell->bsp_cluster.index;
	source_out->split_ordinal = split_ordinal;
	return 1;
}

static int AppendDerivedRegion(const geometry_context_t *context,
	const sg_configuration_space_t *configuration, const sg_bsp_world_t *world,
	const geometry_halfspace_vector_t *halfspaces, uint32_t anchor_config,
	const uint32_t members[2], uint32_t member_count,
	sg_rune_stance_validity_t valid_stances, uint32_t split_ordinal,
	geometry_region_vector_t *regions)
{
	sg_rune_compact_partition_allocator_t allocator;
	sg_rune_compact_partition_polyhedron_t polyhedron;
	sg_rune_compact_partition_error_t partition_error;
	geometry_region_work_t region;

	memset(&polyhedron, 0, sizeof(polyhedron));
	memset(&partition_error, 0, sizeof(partition_error));
	SetPartitionAllocator(context, &allocator);
	if (!SG_RuneCompactPartitionDeriveFaces(halfspaces->values, halfspaces->count,
		&configuration->cells[anchor_config].bounds, &allocator, &polyhedron,
		&partition_error))
	{
		SetPartitionError(context, &partition_error,
			SG_RUNE_COMPACT_GEOMETRY_RECORD_CELL, anchor_config);
		return 0;
	}
	if (polyhedron.empty)
	{
		SG_RuneCompactPartitionPolyhedronDestroy(&polyhedron, &allocator);
		return 1;
	}
	memset(&region, 0, sizeof(region));
	region.anchor_config = anchor_config;
	region.config_members[0] = members[0];
	region.config_members[1] = member_count > 1U ? members[1] : members[0];
	region.member_count = member_count;
	region.split_ordinal = split_ordinal;
	region.valid_stances = valid_stances;
	region.contents = (sg_rune_compact_contents_mask_t)
		configuration->cells[anchor_config].contents;
	if (member_count > 1U)
		region.contents |= (sg_rune_compact_contents_mask_t)
			configuration->cells[members[1]].contents;
	if (!BoundsToQ8(&polyhedron.bounds, &region.bounds) ||
		!RegionSourceFromConfiguration(configuration, world, anchor_config,
			split_ordinal, &region.source))
	{
		SG_RuneCompactPartitionPolyhedronDestroy(&polyhedron, &allocator);
		SetError(context->error, SG_RUNE_COMPACT_GEOMETRY_ERROR_Q8_CONVERSION,
			SG_RUNE_COMPACT_GEOMETRY_RECORD_CELL, anchor_config);
		return 0;
	}
	region.polyhedron = polyhedron;
	if (!HalfspaceVectorCopy(context, halfspaces, &region.halfspaces))
	{
		SG_RuneCompactPartitionPolyhedronDestroy(&region.polyhedron, &allocator);
		SetError(context->error, SG_RUNE_COMPACT_GEOMETRY_ERROR_OUT_OF_MEMORY,
			SG_RUNE_COMPACT_GEOMETRY_RECORD_CELL, anchor_config);
		return 0;
	}
	if (!RegionVectorPush(context, regions, &region))
	{
		HalfspaceVectorRelease(context, &region.halfspaces);
		SG_RuneCompactPartitionPolyhedronDestroy(&region.polyhedron, &allocator);
		SetError(context->error, SG_RUNE_COMPACT_GEOMETRY_ERROR_OVERFLOW,
			SG_RUNE_COMPACT_GEOMETRY_RECORD_CELL, anchor_config);
		return 0;
	}
	return 1;
}

static int AppendIntersectionRegion(const geometry_context_t *context,
	const sg_configuration_space_t *configuration, const sg_bsp_world_t *world,
	uint32_t overlap_index, geometry_region_vector_t *regions)
{
	const sg_configuration_stance_overlap_t *overlap =
		&configuration->stance_overlaps[overlap_index];
	geometry_halfspace_vector_t overlap_halfspaces;
	sg_rune_compact_partition_polyhedron_t polyhedron;
	sg_rune_compact_partition_error_t partition_error;
	sg_rune_compact_partition_allocator_t allocator;
	geometry_region_work_t region;
	uint32_t members[2];
	uint32_t anchor;
	uint32_t split_ordinal;
	int standing_first;

	memset(&overlap_halfspaces, 0, sizeof(overlap_halfspaces));
	if (!BuildOverlapHalfspaces(context, configuration, overlap_index,
		&overlap_halfspaces))
		return 0;
	memset(&polyhedron, 0, sizeof(polyhedron));
	memset(&partition_error, 0, sizeof(partition_error));
	SetPartitionAllocator(context, &allocator);
	if (!SG_RuneCompactPartitionDeriveFaces(overlap_halfspaces.values,
		overlap_halfspaces.count, &overlap->bounds, &allocator, &polyhedron,
		&partition_error))
	{
		SetPartitionError(context, &partition_error,
			SG_RUNE_COMPACT_GEOMETRY_RECORD_CELL, overlap_index);
		HalfspaceVectorRelease(context, &overlap_halfspaces);
		return 0;
	}
	HalfspaceVectorRelease(context, &overlap_halfspaces);
	if (polyhedron.empty)
	{
		SG_RuneCompactPartitionPolyhedronDestroy(&polyhedron, &allocator);
		SetError(context->error, SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_CONFIGURATION,
			SG_RUNE_COMPACT_GEOMETRY_RECORD_CELL, overlap_index);
		return 0;
	}
	standing_first = CellOrderLess(configuration, overlap->standing_cell,
		overlap->crouching_cell);
	anchor = standing_first ? overlap->standing_cell : overlap->crouching_cell;
	members[0] = overlap->standing_cell;
	members[1] = overlap->crouching_cell;
	split_ordinal = UINT32_C(0x80000000) | overlap_index;
	memset(&region, 0, sizeof(region));
	region.anchor_config = anchor;
	region.config_members[0] = members[0];
	region.config_members[1] = members[1];
	region.member_count = 2U;
	region.split_ordinal = split_ordinal;
	region.valid_stances = SG_RUNE_STANCE_VALID_ALL;
	if (configuration->cells[members[0]].contents !=
		configuration->cells[members[1]].contents)
	{
		SG_RuneCompactPartitionPolyhedronDestroy(&polyhedron, &allocator);
		SetError(context->error,
			SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_CONFIGURATION,
			SG_RUNE_COMPACT_GEOMETRY_RECORD_CELL, overlap_index);
		return 0;
	}
	region.contents = (sg_rune_compact_contents_mask_t)
		configuration->cells[members[0]].contents;
	if (!BoundsToQ8(&polyhedron.bounds, &region.bounds) ||
		!RegionSourceFromConfiguration(configuration, world, anchor,
			split_ordinal, &region.source))
	{
		SG_RuneCompactPartitionPolyhedronDestroy(&polyhedron, &allocator);
		SetError(context->error, SG_RUNE_COMPACT_GEOMETRY_ERROR_Q8_CONVERSION,
			SG_RUNE_COMPACT_GEOMETRY_RECORD_CELL, overlap_index);
		return 0;
	}
	region.polyhedron = polyhedron;
	if (!BuildOverlapHalfspaces(context, configuration, overlap_index,
		&region.halfspaces))
	{
		SG_RuneCompactPartitionPolyhedronDestroy(&region.polyhedron, &allocator);
		return 0;
	}
	if (!RegionVectorPush(context, regions, &region))
	{
		HalfspaceVectorRelease(context, &region.halfspaces);
		SG_RuneCompactPartitionPolyhedronDestroy(&region.polyhedron, &allocator);
		SetError(context->error, SG_RUNE_COMPACT_GEOMETRY_ERROR_OVERFLOW,
			SG_RUNE_COMPACT_GEOMETRY_RECORD_CELL, overlap_index);
		return 0;
	}
	return 1;
}

static int AppendSingleRegion(const geometry_context_t *context,
	const sg_configuration_space_t *configuration, const sg_bsp_world_t *world,
	uint32_t config_index, uint32_t split_ordinal,
	geometry_region_vector_t *regions)
{
	geometry_halfspace_vector_t halfspaces;
	uint32_t members[2];
	const sg_rune_stance_validity_t valid_stances = (sg_rune_stance_validity_t)(
		UINT8_C(1) << (uint32_t)configuration->cells[config_index].stance);
	int result;

	if (!BuildCellHalfspaces(context, configuration, config_index, &halfspaces))
		return 0;
	members[0] = config_index;
	result = AppendDerivedRegion(context, configuration, world, &halfspaces,
		config_index, members, 1U, valid_stances, split_ordinal, regions);
	HalfspaceVectorRelease(context, &halfspaces);
	return result;
}


typedef struct geometry_halfspace_set_vector_s
{
	geometry_halfspace_vector_t *values;
	uint32_t count;
	uint32_t capacity;
} geometry_halfspace_set_vector_t;

static void HalfspaceSetVectorRelease(const geometry_context_t *context,
	geometry_halfspace_set_vector_t *sets)
{
	uint32_t index;

	if (sets == NULL)
		return;
	for (index = 0U; index < sets->count; index++)
		HalfspaceVectorRelease(context, &sets->values[index]);
	Release(context, sets->values);
	memset(sets, 0, sizeof(*sets));
}

static int HalfspaceSetVectorReserve(const geometry_context_t *context,
	geometry_halfspace_set_vector_t *sets, uint32_t required)
{
	geometry_halfspace_vector_t *grown;
	size_t bytes;
	uint32_t capacity;

	if (required <= sets->capacity)
		return 1;
	capacity = sets->capacity == 0U ? 4U : sets->capacity;
	while (capacity < required)
	{
		if (capacity > SG_RUNE_COMPACT_MAX_CELLS / 2U)
		{
			capacity = SG_RUNE_COMPACT_MAX_CELLS;
			break;
		}
		capacity *= 2U;
	}
	if (!SizeForCount(capacity, sizeof(*grown), &bytes))
		return 0;
	grown = (geometry_halfspace_vector_t *)Allocate(context, bytes);
	if (grown == NULL)
		return 0;
	if (sets->count != 0U)
		memcpy(grown, sets->values,
			(size_t)sets->count * sizeof(*grown));
	Release(context, sets->values);
	sets->values = grown;
	sets->capacity = capacity;
	return 1;
}

static int HalfspaceSetVectorPushOwned(const geometry_context_t *context,
	geometry_halfspace_set_vector_t *sets,
	geometry_halfspace_vector_t *value)
{
	uint32_t required;

	if (sets->count == UINT32_MAX || !AddU32(sets->count, 1U, &required) ||
		required > SG_RUNE_COMPACT_MAX_CELLS ||
		!HalfspaceSetVectorReserve(context, sets, required))
		return 0;
	sets->values[sets->count] = *value;
	memset(value, 0, sizeof(*value));
	sets->count = required;
	return 1;
}

static int HalfspacesNonempty(const geometry_context_t *context,
	const geometry_halfspace_vector_t *value, const sg_rune_bounds_t *bounds,
	uint32_t record, int *nonempty_out)
{
	sg_rune_compact_partition_allocator_t allocator;
	sg_rune_compact_partition_polyhedron_t polyhedron;
	sg_rune_compact_partition_error_t error;

	memset(&polyhedron, 0, sizeof(polyhedron));
	memset(&error, 0, sizeof(error));
	SetPartitionAllocator(context, &allocator);
	if (!SG_RuneCompactPartitionDeriveFaces(value->values, value->count, bounds,
		&allocator, &polyhedron, &error))
	{
		SetPartitionError(context, &error,
			SG_RUNE_COMPACT_GEOMETRY_RECORD_CELL, record);
		return 0;
	}
	*nonempty_out = !polyhedron.empty;
	SG_RuneCompactPartitionPolyhedronDestroy(&polyhedron, &allocator);
	return 1;
}

static int HalfspaceSetPushIfNonempty(const geometry_context_t *context,
	geometry_halfspace_set_vector_t *sets, geometry_halfspace_vector_t *value,
	const sg_rune_bounds_t *bounds, uint32_t record)
{
	int nonempty;

	if (!HalfspacesNonempty(context, value, bounds, record, &nonempty))
		return 0;
	if (!nonempty)
	{
		HalfspaceVectorRelease(context, value);
		return 1;
	}
	if (!HalfspaceSetVectorPushOwned(context, sets, value))
	{
		SetError(context->error, SG_RUNE_COMPACT_GEOMETRY_ERROR_OVERFLOW,
			SG_RUNE_COMPACT_GEOMETRY_RECORD_CELL, record);
		return 0;
	}
	return 1;
}

static int AppendDifferenceRegionsGeneral(const geometry_context_t *context,
	const sg_configuration_space_t *configuration, const sg_bsp_world_t *world,
	uint32_t config_index, uint32_t region_serial,
	geometry_region_vector_t *regions)
{
	geometry_halfspace_set_vector_t pending;
	uint32_t overlap_index;
	uint32_t split_serial = region_serial;
	const uint32_t member[2] = { config_index, config_index };

	memset(&pending, 0, sizeof(pending));
	{
		geometry_halfspace_vector_t initial;

		if (!BuildCellHalfspaces(context, configuration, config_index, &initial) ||
			!HalfspaceSetVectorPushOwned(context, &pending, &initial))
		{
			HalfspaceVectorRelease(context, &initial);
			HalfspaceSetVectorRelease(context, &pending);
			SetError(context->error, SG_RUNE_COMPACT_GEOMETRY_ERROR_OVERFLOW,
				SG_RUNE_COMPACT_GEOMETRY_RECORD_CELL, config_index);
			return 0;
		}
	}
	for (overlap_index = 0U;
		overlap_index < configuration->stance_overlap_count; overlap_index++)
	{
		const sg_configuration_stance_overlap_t *overlap =
			&configuration->stance_overlaps[overlap_index];
		geometry_halfspace_vector_t overlap_halfspaces;
		geometry_halfspace_set_vector_t next_pending;
		uint32_t pending_index;

		if (overlap->standing_cell != config_index &&
			overlap->crouching_cell != config_index)
			continue;
		if (!BuildOverlapHalfspaces(context, configuration, overlap_index,
			&overlap_halfspaces))
		{
			HalfspaceSetVectorRelease(context, &pending);
			return 0;
		}
		memset(&next_pending, 0, sizeof(next_pending));
		for (pending_index = 0U; pending_index < pending.count; pending_index++)
		{
			geometry_halfspace_vector_t prefix;
			uint32_t halfspace_index;
			int prefix_nonempty = 1;

			if (!HalfspaceVectorCopy(context, &pending.values[pending_index],
				&prefix))
			{
				HalfspaceVectorRelease(context, &overlap_halfspaces);
				HalfspaceSetVectorRelease(context, &next_pending);
				HalfspaceSetVectorRelease(context, &pending);
				SetError(context->error, SG_RUNE_COMPACT_GEOMETRY_ERROR_OVERFLOW,
					SG_RUNE_COMPACT_GEOMETRY_RECORD_CELL, config_index);
				return 0;
			}
			for (halfspace_index = 0U; prefix_nonempty &&
				halfspace_index < overlap_halfspaces.count;
				halfspace_index++)
			{
				geometry_halfspace_vector_t outside;
				sg_rune_compact_partition_halfspace_t complement;

				if (!HalfspaceVectorCopy(context, &prefix, &outside))
				{
					HalfspaceVectorRelease(context, &prefix);
					HalfspaceVectorRelease(context, &overlap_halfspaces);
					HalfspaceSetVectorRelease(context, &next_pending);
					HalfspaceSetVectorRelease(context, &pending);
					SetError(context->error,
						SG_RUNE_COMPACT_GEOMETRY_ERROR_OVERFLOW,
						SG_RUNE_COMPACT_GEOMETRY_RECORD_CELL, config_index);
					return 0;
				}
				FlipPartitionHalfspace(&overlap_halfspaces.values[halfspace_index],
					&complement);
				if (!HalfspaceVectorPush(context, &outside, &complement) ||
					!HalfspaceSetPushIfNonempty(context, &next_pending, &outside,
						&configuration->cells[config_index].bounds, config_index))
				{
					HalfspaceVectorRelease(context, &outside);
					HalfspaceVectorRelease(context, &prefix);
					HalfspaceVectorRelease(context, &overlap_halfspaces);
					HalfspaceSetVectorRelease(context, &next_pending);
					HalfspaceSetVectorRelease(context, &pending);
					SetError(context->error,
						SG_RUNE_COMPACT_GEOMETRY_ERROR_OVERFLOW,
						SG_RUNE_COMPACT_GEOMETRY_RECORD_CELL, config_index);
					return 0;
				}
				HalfspaceVectorRelease(context, &outside);
				if (!HalfspaceVectorPush(context, &prefix,
					&overlap_halfspaces.values[halfspace_index]))
				{
					HalfspaceVectorRelease(context, &prefix);
					HalfspaceVectorRelease(context, &overlap_halfspaces);
					HalfspaceSetVectorRelease(context, &next_pending);
					HalfspaceSetVectorRelease(context, &pending);
					SetError(context->error,
						SG_RUNE_COMPACT_GEOMETRY_ERROR_OVERFLOW,
						SG_RUNE_COMPACT_GEOMETRY_RECORD_CELL, config_index);
					return 0;
				}
				if (!HalfspacesNonempty(context, &prefix,
					&configuration->cells[config_index].bounds, config_index,
					&prefix_nonempty))
				{
					HalfspaceVectorRelease(context, &prefix);
					HalfspaceVectorRelease(context, &overlap_halfspaces);
					HalfspaceSetVectorRelease(context, &next_pending);
					HalfspaceSetVectorRelease(context, &pending);
					return 0;
				}
			}
			/* prefix is the portion consumed by this overlap. */
			HalfspaceVectorRelease(context, &prefix);
		}
		HalfspaceVectorRelease(context, &overlap_halfspaces);
		HalfspaceSetVectorRelease(context, &pending);
		pending = next_pending;
	}
	for (overlap_index = 0U; overlap_index < pending.count; overlap_index++)
	{
		geometry_halfspace_vector_t *piece = &pending.values[overlap_index];

		if (!AppendDerivedRegion(context, configuration, world, piece, config_index,
			member, 1U,
			(sg_rune_stance_validity_t)(UINT8_C(1) <<
				(uint32_t)configuration->cells[config_index].stance),
			UINT32_C(0x40000000) | split_serial++, regions))
		{
			HalfspaceSetVectorRelease(context, &pending);
			return 0;
		}
	}
	HalfspaceSetVectorRelease(context, &pending);
	return 1;
}

static int BuildRegions(const geometry_context_t *context,
	const sg_configuration_space_t *configuration, const sg_bsp_world_t *world,
	geometry_region_vector_t *regions)
{
	uint32_t config_index;

	memset(regions, 0, sizeof(*regions));
	if (configuration->stance_overlap_count != 0U)
		for (config_index = 0U; config_index < configuration->stance_overlap_count;
			config_index++)
			if (!AppendIntersectionRegion(context, configuration, world, config_index,
				regions))
				return 0;
	for (config_index = 0U; config_index < configuration->cell_count; config_index++)
	{
		int has_overlap = 0;
		uint32_t overlap_index;

		for (overlap_index = 0U;
			overlap_index < configuration->stance_overlap_count; overlap_index++)
			if (configuration->stance_overlaps[overlap_index].standing_cell ==
				config_index || configuration->stance_overlaps[overlap_index].crouching_cell ==
				config_index)
			{
				has_overlap = 1;
				break;
			}
		if (has_overlap)
		{
			if (!AppendDifferenceRegionsGeneral(context, configuration, world,
				config_index, config_index, regions))
				return 0;
		}
		else if (!AppendSingleRegion(context, configuration, world, config_index,
			config_index, regions))
			return 0;
	}
	if (regions->count == 0U)
	{
		SetError(context->error,
			SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_GEOMETRY,
			SG_RUNE_COMPACT_GEOMETRY_RECORD_RESULT, GEOMETRY_INDEX_NONE);
		return 0;
	}
	return 1;
}

static int RegionCompare(const geometry_region_work_t *left,
	const geometry_region_work_t *right)
{
	uint32_t axis;

	if (left->source.model != right->source.model)
		return left->source.model < right->source.model ? -1 : 1;
	if (left->source.leaf != right->source.leaf)
		return left->source.leaf < right->source.leaf ? -1 : 1;
	if (left->source.area != right->source.area)
		return left->source.area < right->source.area ? -1 : 1;
	if (left->source.cluster != right->source.cluster)
		return left->source.cluster < right->source.cluster ? -1 : 1;
	for (axis = 0U; axis < 3U; axis++)
	{
		if (left->bounds.mins.value[axis] != right->bounds.mins.value[axis])
			return left->bounds.mins.value[axis] < right->bounds.mins.value[axis] ? -1 : 1;
		if (left->bounds.maxs.value[axis] != right->bounds.maxs.value[axis])
			return left->bounds.maxs.value[axis] < right->bounds.maxs.value[axis] ? -1 : 1;
	}
	if (left->valid_stances != right->valid_stances)
		return left->valid_stances < right->valid_stances ? -1 : 1;
	if (left->anchor_config != right->anchor_config)
		return left->anchor_config < right->anchor_config ? -1 : 1;
	return left->split_ordinal < right->split_ordinal ? -1 :
		left->split_ordinal > right->split_ordinal;
}

static void SortRegions(geometry_region_vector_t *regions)
{
	uint32_t index;

	for (index = 1U; index < regions->count; index++)
	{
		geometry_region_work_t value = regions->values[index];
		uint32_t cursor = index;

		while (cursor != 0U &&
			RegionCompare(&value, &regions->values[cursor - 1U]) < 0)
		{
			regions->values[cursor] = regions->values[cursor - 1U];
			cursor--;
		}
		regions->values[cursor] = value;
	}
	for (index = 0U; index < regions->count; index++)
	{
		uint32_t ordinal = 0U;

		if (index != 0U &&
			regions->values[index - 1U].source.model == regions->values[index].source.model &&
			regions->values[index - 1U].source.leaf == regions->values[index].source.leaf &&
			regions->values[index - 1U].source.area == regions->values[index].source.area &&
			regions->values[index - 1U].source.cluster == regions->values[index].source.cluster)
			ordinal = regions->values[index - 1U].source.split_ordinal + 1U;
		regions->values[index].split_ordinal = ordinal;
		regions->values[index].source.split_ordinal = ordinal;
	}
}

static int BuildRegionCellsAndMap(const geometry_context_t *context,
	const sg_configuration_space_t *configuration,
	const geometry_region_vector_t *regions,
	sg_rune_compact_geometry_t *geometry)
{
	uint32_t *counts = NULL;
	uint32_t *cursors = NULL;
	uint32_t region_index;
	uint32_t total = 0U;

	geometry->cell_count = regions->count;
	geometry->compact_cells_for_configuration_cell_count =
		configuration->cell_count;
	if (!AllocateArray(context, geometry->cell_count, sizeof(*geometry->cells),
		(void **)&geometry->cells, SG_RUNE_COMPACT_GEOMETRY_RECORD_CELL) ||
		!AllocateArray(context, configuration->cell_count,
			sizeof(*geometry->compact_cells_for_configuration_cell),
			(void **)&geometry->compact_cells_for_configuration_cell,
			SG_RUNE_COMPACT_GEOMETRY_RECORD_CELL) ||
		!AllocateArray(context, configuration->cell_count, sizeof(*counts),
			(void **)&counts, SG_RUNE_COMPACT_GEOMETRY_RECORD_CELL) ||
		!AllocateArray(context, configuration->cell_count, sizeof(*cursors),
			(void **)&cursors, SG_RUNE_COMPACT_GEOMETRY_RECORD_CELL))
		goto failure;
	for (region_index = 0U; region_index < regions->count; region_index++)
	{
		const geometry_region_work_t *region = &regions->values[region_index];
		sg_rune_compact_cell_t *cell = &geometry->cells[region_index];
		uint32_t member;

		cell->source = region->source;
		cell->bounds = region->bounds;
		cell->contents = region->contents;
		cell->valid_stances = region->valid_stances;
		for (member = 0U; member < region->member_count; member++)
		{
			const sg_configuration_cell_t *source =
				&configuration->cells[region->config_members[member]];
			if ((source->contents & SG_RUNE_CONTENTS_SKY) != 0U)
				cell->semantics |= SG_RUNE_COMPACT_CELL_SKY_BOUNDARY;
			if ((source->contents & (SG_RUNE_CONTENTS_LAVA |
				SG_RUNE_CONTENTS_SLIME)) != 0U)
				cell->semantics |= SG_RUNE_COMPACT_CELL_HAZARD;
			if (counts[region->config_members[member]] == UINT32_MAX)
			{
				SetError(context->error, SG_RUNE_COMPACT_GEOMETRY_ERROR_OVERFLOW,
					SG_RUNE_COMPACT_GEOMETRY_RECORD_CELL, region_index);
				goto failure;
			}
			counts[region->config_members[member]]++;
		}
	}
	for (region_index = 0U; region_index < configuration->cell_count; region_index++)
	{
		geometry->compact_cells_for_configuration_cell[region_index].first = total;
		geometry->compact_cells_for_configuration_cell[region_index].count =
			counts[region_index];
		if (!AddU32(total, counts[region_index], &total))
		{
			SetError(context->error, SG_RUNE_COMPACT_GEOMETRY_ERROR_OVERFLOW,
				SG_RUNE_COMPACT_GEOMETRY_RECORD_CELL, region_index);
			goto failure;
		}
	}
	if (!AllocateArray(context, total,
		sizeof(*geometry->configuration_cell_compact_cells),
		(void **)&geometry->configuration_cell_compact_cells,
		SG_RUNE_COMPACT_GEOMETRY_RECORD_CELL))
		goto failure;
	geometry->configuration_cell_compact_cell_count = total;
	if (total == 0U || geometry->configuration_cell_compact_cells == NULL)
	{
		SetError(context->error, SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_REFERENCE,
			SG_RUNE_COMPACT_GEOMETRY_RECORD_RESULT, GEOMETRY_INDEX_NONE);
		goto failure;
	}
	for (region_index = 0U; region_index < regions->count; region_index++)
	{
		const geometry_region_work_t *region = &regions->values[region_index];
		uint32_t member;

		for (member = 0U; member < region->member_count; member++)
		{
			const uint32_t config = region->config_members[member];
			const uint32_t at =
				geometry->compact_cells_for_configuration_cell[config].first +
				cursors[config]++;
			geometry->configuration_cell_compact_cells[at].value = region_index;
		}
	}
	Release(context, counts);
	Release(context, cursors);
	return 1;

failure:
	Release(context, counts);
	Release(context, cursors);
	return 0;
}

static int PartitionPolygonOwnedCopy(const geometry_context_t *context,
	const sg_rune_compact_partition_polygon_t *source,
	sg_rune_compact_partition_polygon_t *destination)
{
	size_t bytes;

	memset(destination, 0, sizeof(*destination));
	if (source->vertices == NULL || source->vertex_count < 3U ||
		!SizeForCount(source->vertex_count, sizeof(*source->vertices), &bytes))
		return 0;
	destination->vertices = (sg_rune_vec3_t *)Allocate(context, bytes);
	if (destination->vertices == NULL)
		return 0;
	memcpy(destination->vertices, source->vertices, bytes);
	destination->plane = source->plane;
	destination->source_plane_index = source->source_plane_index;
	destination->contributor = source->contributor;
	destination->open = source->open;
	destination->vertex_count = source->vertex_count;
	return 1;
}

static int RegionContainsConfigurationCell(const geometry_region_work_t *region,
	uint32_t configuration_cell)
{
	uint32_t member;

	for (member = 0U; member < region->member_count; member++)
		if (region->config_members[member] == configuration_cell)
			return 1;
	return 0;
}

static int SourceFromOrientedFace(const sg_configuration_face_t *face,
	const sg_configuration_plane_t *oriented_plane,
	const sg_configuration_cell_t *cell, const sg_bsp_world_t *world,
	sg_rune_compact_source_t *source_out)
{
	sg_configuration_face_t oriented_source;
	double source_normal[3], output_normal[3];
	double source_distance, output_distance;
	double orientation;

	if (!NormalizeConfigurationPlane(&face->plane, source_normal,
		&source_distance) ||
		!NormalizeConfigurationPlane(oriented_plane, output_normal,
			&output_distance))
		return 0;
	orientation = source_normal[0] * output_normal[0] +
		source_normal[1] * output_normal[1] +
		source_normal[2] * output_normal[2];
	if (fabs(orientation) < 1.0 - GEOMETRY_PLANE_EPSILON ||
		fabs(output_distance - (orientation < 0.0 ?
			-source_distance : source_distance)) > GEOMETRY_PLANE_EPSILON)
		return 0;
	oriented_source = *face;
	if (orientation < 0.0)
		oriented_source.plane.source_variant ^= 1U;
	return SourceFromFace(&oriented_source, cell, world, source_out);
}

static int RegionsShareConfigurationCell(const geometry_region_work_t *left,
	const geometry_region_work_t *right)
{
	uint32_t member;

	for (member = 0U; member < left->member_count; member++)
		if (RegionContainsConfigurationCell(right,
			left->config_members[member]))
			return 1;
	return 0;
}

static int PolygonEntryFromPartition(const geometry_context_t *context,
	const sg_configuration_space_t *configuration,
	const sg_bsp_world_t *world, uint32_t owner_config,
	const sg_rune_compact_partition_polygon_t *polygon,
	uint32_t region_index, uint32_t serial, geometry_entry_t *entry)
{
	uint32_t source_face = polygon->source_plane_index;
	uint32_t source_config = polygon->contributor < configuration->cell_count ?
		polygon->contributor : owner_config;

	if (source_face >= configuration->face_count)
	{
		SetError(context->error, SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_REFERENCE,
			SG_RUNE_COMPACT_GEOMETRY_RECORD_CELL, region_index);
		return 0;
	}
	memset(entry, 0, sizeof(*entry));
	entry->serial = serial;
	entry->owner_region = region_index;
	entry->open = polygon->open;
	entry->plane.normal_bits[0] = FloatBits(polygon->plane.normal[0]);
	entry->plane.normal_bits[1] = FloatBits(polygon->plane.normal[1]);
	entry->plane.normal_bits[2] = FloatBits(polygon->plane.normal[2]);
	entry->plane.distance_bits = FloatBits(polygon->plane.distance);
	if (!CompactPlaneFinite(&entry->plane))
	{
		SetError(context->error, SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_GEOMETRY,
			SG_RUNE_COMPACT_GEOMETRY_RECORD_CELL, region_index);
		return 0;
	}
	if (!SourceFromOrientedFace(&configuration->faces[source_face],
		&polygon->plane, &configuration->cells[source_config], world,
		&entry->source))
	{
		SetError(context->error, SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_REFERENCE,
			SG_RUNE_COMPACT_GEOMETRY_RECORD_CELL, region_index);
		return 0;
	}
	return 1;
}




static int PartitionErrorMap(const sg_rune_compact_partition_error_t *error,
	sg_rune_compact_geometry_error_code_t *code_out)
{
	if (error == NULL || code_out == NULL)
		return 0;
	switch (error->code)
	{
	case SG_RUNE_COMPACT_PARTITION_ERROR_NONFINITE:
		*code_out = SG_RUNE_COMPACT_GEOMETRY_ERROR_NONFINITE_GEOMETRY;
		return 1;
	case SG_RUNE_COMPACT_PARTITION_ERROR_OVERFLOW:
		*code_out = SG_RUNE_COMPACT_GEOMETRY_ERROR_OVERFLOW;
		return 1;
	case SG_RUNE_COMPACT_PARTITION_ERROR_OUT_OF_MEMORY:
		*code_out = SG_RUNE_COMPACT_GEOMETRY_ERROR_OUT_OF_MEMORY;
		return 1;
	case SG_RUNE_COMPACT_PARTITION_ERROR_DEGENERATE:
		*code_out = SG_RUNE_COMPACT_GEOMETRY_ERROR_UNSUPPORTED_TOPOLOGY;
		return 1;
	case SG_RUNE_COMPACT_PARTITION_ERROR_INVALID_ARGUMENT:
	case SG_RUNE_COMPACT_PARTITION_ERROR_NONE:
	case SG_RUNE_COMPACT_PARTITION_ERROR_CODE_COUNT:
		break;
	}
	*code_out = SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_GEOMETRY;
	return 1;
}

static void SetPartitionError(const geometry_context_t *context,
	const sg_rune_compact_partition_error_t *partition_error,
	sg_rune_compact_geometry_record_domain_t domain, uint32_t record)
{
	sg_rune_compact_geometry_error_code_t code;

	(void)PartitionErrorMap(partition_error, &code);
	SetError(context->error, code, domain, record);
}

static int SubtractPiece(const geometry_context_t *context,
	geometry_piece_vector_t *pieces,
	const sg_rune_compact_partition_polygon_t *portal,
	sg_rune_compact_geometry_error_t *error, uint32_t portal_index);
static int PartitionPolygonOwnedCopy(const geometry_context_t *context,
	const sg_rune_compact_partition_polygon_t *source,
	sg_rune_compact_partition_polygon_t *destination);
static int RegionContainsConfigurationCell(const geometry_region_work_t *region,
	uint32_t configuration_cell);
static int PolygonEntryFromPartition(const geometry_context_t *context,
	const sg_configuration_space_t *configuration,
	const sg_bsp_world_t *world, uint32_t owner_config,
	const sg_rune_compact_partition_polygon_t *polygon,
	uint32_t region_index, uint32_t serial, geometry_entry_t *entry);

static int BuildRegionEntries(const geometry_context_t *context,
	const sg_configuration_space_t *configuration, const sg_bsp_world_t *world,
	const geometry_region_vector_t *regions,
	const geometry_portal_vector_t *portals, geometry_entry_vector_t *entries)
{
	uint32_t region_index;
	uint32_t serial = 0U;

	for (region_index = 0U; region_index < regions->count; region_index++)
	{
		const geometry_region_work_t *region = &regions->values[region_index];
		uint32_t polyface_index;

		for (polyface_index = 0U;
			polyface_index < region->polyhedron.face_count; polyface_index++)
		{
			const sg_rune_compact_partition_polygon_t *polyface =
				&region->polyhedron.faces[polyface_index];
			const uint32_t source_face = polyface->source_plane_index;
			geometry_piece_vector_t pieces;
			uint32_t portal_index;

			if (source_face >= configuration->face_count)
			{
				SetError(context->error,
					SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_GEOMETRY_RECORD_CELL, region_index);
				return 0;
			}
			memset(&pieces, 0, sizeof(pieces));
			{
				sg_rune_compact_partition_polygon_t copy;

				memset(&copy, 0, sizeof(copy));
				if (!PartitionPolygonOwnedCopy(context, polyface, &copy) ||
					!PartitionPieceVectorPush(context, &pieces, &copy))
				{
					PartitionPolygonRelease(context, &copy);
					PartitionPieceVectorRelease(context, &pieces);
					SetError(context->error,
						SG_RUNE_COMPACT_GEOMETRY_ERROR_OVERFLOW,
						SG_RUNE_COMPACT_GEOMETRY_RECORD_CELL, region_index);
					return 0;
				}
			}
			for (portal_index = 0U; portal_index < portals->count; portal_index++)
			{
				const geometry_portal_work_t *portal =
					&portals->values[portal_index];
				sg_rune_compact_partition_polygon_t portal_polygon;

				if ((!RegionContainsConfigurationCell(region, portal->low_config) &&
					!RegionContainsConfigurationCell(region, portal->high_config)) ||
					(source_face != portal->low_face && source_face != portal->high_face))
					continue;
				memset(&portal_polygon, 0, sizeof(portal_polygon));
				if (!PartitionPolygonCopyFromPortal(configuration,
					&configuration->portals[portal->config_index],
					portal->config_index, &portal_polygon) ||
					!SubtractPiece(context, &pieces, &portal_polygon,
						context->error, portal->config_index))
				{
					PartitionPieceVectorRelease(context, &pieces);
					return 0;
				}
			}
			while (pieces.count != 0U)
			{
				sg_rune_compact_partition_polygon_t piece = pieces.values[0];
				geometry_entry_t entry;

				memmove(pieces.values, pieces.values + 1U,
					(size_t)(pieces.count - 1U) * sizeof(pieces.values[0]));
				pieces.count--;
				memset(&pieces.values[pieces.count], 0,
					sizeof(pieces.values[pieces.count]));
				if (!PolygonEntryFromPartition(context, configuration, world,
					region->anchor_config, &piece, region_index, serial++, &entry) ||
					!ConvertPartitionPolygon(context, &piece, &entry.plane,
						&entry.polygon))
				{
					PartitionPolygonRelease(context, &piece);
					PartitionPieceVectorRelease(context, &pieces);
					SetError(context->error,
						SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_GEOMETRY,
						SG_RUNE_COMPACT_GEOMETRY_RECORD_CELL, region_index);
					return 0;
				}
				PartitionPolygonRelease(context, &piece);
				entry.kind = SG_RUNE_COMPACT_FACET_POLYGON;
				if (!EntryVectorPush(context, entries, &entry))
				{
					PolygonRelease(context, &entry.polygon);
					PartitionPieceVectorRelease(context, &pieces);
					SetError(context->error,
						SG_RUNE_COMPACT_GEOMETRY_ERROR_OVERFLOW,
						SG_RUNE_COMPACT_GEOMETRY_RECORD_CELL, region_index);
					return 0;
				}
			}
			Release(context, pieces.values);
		}
		for (polyface_index = 0U; polyface_index < region->halfspaces.count;
			polyface_index++)
		{
			const sg_rune_compact_partition_halfspace_t *halfspace =
				&region->halfspaces.values[polyface_index];
			const uint32_t source_face = halfspace->source_plane_index;
			const uint32_t source_config =
				halfspace->contributor < configuration->cell_count ?
					halfspace->contributor : region->anchor_config;
			geometry_entry_t entry;
			uint32_t earlier;
			int duplicate = 0;

			if (source_face >= configuration->face_count ||
				configuration->faces[source_face].kind !=
					SG_CONFIGURATION_FACE_CONSTRAINT_ONLY)
				continue;
			for (earlier = 0U; earlier < polyface_index; earlier++)
				if (region->halfspaces.values[earlier].source_plane_index ==
					source_face && PlanesEquivalent(
						&region->halfspaces.values[earlier].plane,
						&halfspace->plane))
				{
					duplicate = 1;
					break;
				}
			if (duplicate)
				continue;
			memset(&entry, 0, sizeof(entry));
			entry.serial = serial++;
			entry.owner_region = region_index;
			entry.kind = SG_RUNE_COMPACT_FACET_CONSTRAINT_ONLY;
			entry.open = halfspace->open;
			ConfigurationPlaneToCompact(&halfspace->plane, &entry.plane);
			if (!SourceFromOrientedFace(&configuration->faces[source_face],
				&halfspace->plane, &configuration->cells[source_config], world,
				&entry.source))
			{
				SetError(context->error,
					SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_GEOMETRY_RECORD_CELL, region_index);
				return 0;
			}
			if (!EntryVectorPush(context, entries, &entry))
			{
				SetError(context->error,
					SG_RUNE_COMPACT_GEOMETRY_ERROR_OVERFLOW,
					SG_RUNE_COMPACT_GEOMETRY_RECORD_CELL, region_index);
				return 0;
			}
		}
	}
	if (entries->count == 0U)
	{
		SetError(context->error,
			SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_GEOMETRY,
			SG_RUNE_COMPACT_GEOMETRY_RECORD_RESULT, GEOMETRY_INDEX_NONE);
		return 0;
	}
	return 1;
}

static int BuildRegionPortalEntries(const geometry_context_t *context,
	const geometry_portal_vector_t *portals, geometry_entry_vector_t *entries)
{
	uint32_t portal_index;

	for (portal_index = 0U; portal_index < portals->count; portal_index++)
	{
		const geometry_portal_work_t *portal = &portals->values[portal_index];
		geometry_entry_t entry;

		memset(&entry, 0, sizeof(entry));
		entry.serial = entries->count;
		entry.low_region = portal->low_region;
		entry.high_region = portal->high_region;
		entry.portal_config = portal->config_index;
		entry.portal = 1U;
		entry.kind = SG_RUNE_COMPACT_FACET_POLYGON;
		entry.source = portal->source;
		entry.plane = portal->plane;
		entry.open = portal->open;
		if (!PolygonCopy(context, &portal->polygon, &entry.polygon))
		{
			PolygonRelease(context, &entry.polygon);
			SetError(context->error,
				SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_GEOMETRY_RECORD_PORTAL, portal->config_index);
			return 0;
		}
		if (!EntryVectorPush(context, entries, &entry))
		{
			PolygonRelease(context, &entry.polygon);
			SetError(context->error, SG_RUNE_COMPACT_GEOMETRY_ERROR_OVERFLOW,
				SG_RUNE_COMPACT_GEOMETRY_RECORD_PORTAL, portal->config_index);
			return 0;
		}
	}
	return 1;
}

static int CompactPlanesOpposite(const sg_rune_binary32_plane_t *left,
	const sg_rune_binary32_plane_t *right)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		if (BitsFloat(left->normal_bits[axis]) !=
			-BitsFloat(right->normal_bits[axis]))
			return 0;
	return BitsFloat(left->distance_bits) == -BitsFloat(right->distance_bits);
}

static void MergeSharedRegionFaces(const geometry_context_t *context,
	const geometry_region_vector_t *regions, geometry_entry_vector_t *entries)
{
	uint32_t left;

	for (left = 0U; left < entries->count; left++)
	{
		geometry_entry_t *first = &entries->values[left];
		uint32_t right;

		if (first->portal || first->shared ||
			first->kind != SG_RUNE_COMPACT_FACET_POLYGON)
			continue;
		for (right = left + 1U; right < entries->count; right++)
		{
			geometry_entry_t *second = &entries->values[right];
			uint32_t low;
			uint32_t high;

			if (second->portal || second->shared ||
				second->kind != SG_RUNE_COMPACT_FACET_POLYGON ||
				first->owner_region == second->owner_region ||
				!RegionsShareConfigurationCell(
					&regions->values[first->owner_region],
					&regions->values[second->owner_region]) ||
				!CompactPlanesOpposite(&first->plane, &second->plane) ||
				!PolygonSetEqual(&first->polygon, &second->polygon))
				continue;
			low = first->owner_region < second->owner_region ?
				first->owner_region : second->owner_region;
			high = first->owner_region < second->owner_region ?
				second->owner_region : first->owner_region;
			if (second->owner_region == low)
			{
				geometry_polygon_t polygon = first->polygon;

				first->source = second->source;
				first->plane = second->plane;
				first->open = second->open;
				first->polygon = second->polygon;
				second->polygon = polygon;
			}
			first->low_region = low;
			first->high_region = high;
			first->shared = 1U;
			PolygonRelease(context, &second->polygon);
			memmove(second, second + 1U,
				(size_t)(entries->count - right - 1U) * sizeof(*second));
			entries->count--;
			break;
		}
	}
}

static int WorldReferencesValid(const sg_bsp_world_t *world)
{
	uint32_t brush;
	uint32_t side;
	uint32_t plane;

	if (world->model_count == 0U || world->models == NULL ||
		world->leaf_count == 0U || world->leaves == NULL ||
		world->area_count == 0U || world->areas == NULL ||
		world->plane_count == 0U || world->planes == NULL)
		return 0;
	if ((world->brush_count != 0U && world->brushes == NULL) ||
		(world->brush_side_count != 0U && world->brush_sides == NULL))
		return 0;
	for (plane = 0U; plane < world->plane_count; plane++)
	{
		const sg_bsp_plane_t *value = &world->planes[plane];

		if (!FiniteVector(value->normal.value) || !isfinite(value->distance) ||
			fabs((double)value->normal.value[0]) +
				fabs((double)value->normal.value[1]) +
				fabs((double)value->normal.value[2]) == 0.0)
			return 0;
	}
	for (brush = 0U; brush < world->brush_count; brush++)
		if (!RangeWithin(world->brushes[brush].first_side,
			world->brushes[brush].side_count, world->brush_side_count))
			return 0;
	for (side = 0U; side < world->brush_side_count; side++)
		if (world->brush_sides[side].plane >= world->plane_count)
			return 0;
	for (plane = 0U; plane < world->leaf_count; plane++)
	{
		const sg_bsp_leaf_t *leaf = &world->leaves[plane];

		if (leaf->area >= world->area_count || leaf->cluster < -1)
			return 0;
	}
	return 1;
}

static int SourceFromFace(const sg_configuration_face_t *face,
	const sg_configuration_cell_t *cell, const sg_bsp_world_t *world,
	sg_rune_compact_source_t *source_out)
{
	uint32_t brush;

	memset(source_out, 0, sizeof(*source_out));
	switch (face->plane.source_kind)
	{
	case SG_CONFIGURATION_PLANE_DOMAIN:
		source_out->kind = SG_RUNE_COMPACT_SOURCE_DOMAIN;
		source_out->value.domain.axis = face->plane.source_index;
		if (source_out->value.domain.axis >= 3U)
			return 0;
		source_out->value.domain.maximum_side =
			(face->plane.source_variant & 1U) == 0U ? 1U : 0U;
		return 1;
	case SG_CONFIGURATION_PLANE_BSP:
		source_out->kind = SG_RUNE_COMPACT_SOURCE_BSP_PLANE;
		source_out->value.bsp_plane.model = 0U;
		source_out->value.bsp_plane.leaf = cell->bsp_leaf.index;
		source_out->value.bsp_plane.plane = face->plane.source_index;
		return face->plane.source_index < world->plane_count;
	case SG_CONFIGURATION_PLANE_EXPANDED_BRUSH:
		if (face->plane.source_index >= world->brush_side_count)
			return 0;
		for (brush = 0U; brush < world->brush_count; brush++)
		{
			const sg_bsp_brush_t *value = &world->brushes[brush];

			if (RangeWithin(value->first_side, value->side_count,
				world->brush_side_count) &&
				face->plane.source_index >= value->first_side &&
				face->plane.source_index < value->first_side + value->side_count)
			{
				source_out->kind =
					SG_RUNE_COMPACT_SOURCE_EXPANDED_BRUSH_SIDE;
				source_out->value.brush_side.model = 0U;
				source_out->value.brush_side.brush = brush;
				source_out->value.brush_side.brush_side =
					face->plane.source_index;
				source_out->value.brush_side.plane =
					world->brush_sides[face->plane.source_index].plane;
				return 1;
			}
		}
		return 0;
	default:
		return 0;
	}
}

static int ConfigurationValid(const sg_configuration_space_t *configuration,
	const sg_bsp_world_t *world,
	const sg_rune_compact_identity_t *identity,
	sg_rune_compact_geometry_error_t *error)
{
	uint32_t cell_index;
	uint32_t face_index;
	uint32_t portal_index;

	if (configuration == NULL || world == NULL || identity == NULL)
	{
		SetError(error, SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_GEOMETRY_RECORD_RESULT, GEOMETRY_INDEX_NONE);
		return 0;
	}
	if (!WorldReferencesValid(world))
	{
		SetError(error, SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_WORLD,
			SG_RUNE_COMPACT_GEOMETRY_RECORD_WORLD, GEOMETRY_INDEX_NONE);
		return 0;
	}
	if (identity->source_counts.model_count != world->model_count ||
		identity->source_counts.leaf_count != world->leaf_count ||
		identity->source_counts.area_count != world->area_count ||
		identity->source_counts.plane_count != world->plane_count ||
		identity->source_counts.brush_count != world->brush_count ||
		identity->source_counts.brush_side_count != world->brush_side_count)
	{
		SetError(error, SG_RUNE_COMPACT_GEOMETRY_ERROR_IDENTITY_MISMATCH,
			SG_RUNE_COMPACT_GEOMETRY_RECORD_IDENTITY, 0U);
		return 0;
	}
	if (configuration->cell_count > SG_RUNE_COMPACT_MAX_CELLS ||
		configuration->face_count > SG_RUNE_COMPACT_MAX_FACETS ||
		configuration->portal_count > SG_RUNE_COMPACT_MAX_PORTALS)
	{
		SetError(error, SG_RUNE_COMPACT_GEOMETRY_ERROR_OVERFLOW,
			SG_RUNE_COMPACT_GEOMETRY_RECORD_RESULT, GEOMETRY_INDEX_NONE);
		return 0;
	}
	if (configuration->cell_count == 0U || configuration->cells == NULL ||
		configuration->face_count == 0U || configuration->faces == NULL ||
		(configuration->vertex_count != 0U && configuration->vertices == NULL) ||
		(configuration->portal_count != 0U && configuration->portals == NULL) ||
		(configuration->stance_overlap_count != 0U &&
			configuration->stance_overlaps == NULL))
	{
		SetError(error, SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_CONFIGURATION,
			SG_RUNE_COMPACT_GEOMETRY_RECORD_RESULT, GEOMETRY_INDEX_NONE);
		return 0;
	}
	for (cell_index = 0U; cell_index < configuration->cell_count; cell_index++)
	{
		const sg_configuration_cell_t *cell =
			&configuration->cells[cell_index];
		uint32_t axis;

		if (cell->stance >= SG_RUNE_STANCE_COUNT || cell->face_count == 0U ||
			!RangeWithin(cell->first_face, cell->face_count,
				configuration->face_count) || cell->bsp_leaf.index >= world->leaf_count ||
			cell->bsp_area.index >= world->area_count ||
			!FiniteVector(cell->bounds.mins.value) ||
			!FiniteVector(cell->bounds.maxs.value) ||
			!FiniteVector(cell->interior_witness.value) ||
			(cell->contents & ~(sg_rune_contents_mask_t)SG_RUNE_CONTENTS_KNOWN) != 0U)
		{
			SetError(error, SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_CONFIGURATION,
				SG_RUNE_COMPACT_GEOMETRY_RECORD_CELL, cell_index);
			return 0;
		}
		for (axis = 0U; axis < 3U; axis++)
			if (cell->bounds.mins.value[axis] > cell->bounds.maxs.value[axis])
			{
				SetError(error,
					SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_GEOMETRY,
					SG_RUNE_COMPACT_GEOMETRY_RECORD_CELL, cell_index);
				return 0;
			}
		if (cell->bsp_area.index != world->leaves[cell->bsp_leaf.index].area ||
			(cell->bsp_cluster.index != UINT32_MAX &&
				world->leaves[cell->bsp_leaf.index].cluster < 0) ||
			(cell->bsp_cluster.index != UINT32_MAX &&
				(uint32_t)world->leaves[cell->bsp_leaf.index].cluster !=
					cell->bsp_cluster.index))
		{
			SetError(error, SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_GEOMETRY_RECORD_CELL, cell_index);
			return 0;
		}
	}
	for (face_index = 0U; face_index < configuration->face_count; face_index++)
	{
		const sg_configuration_face_t *face = &configuration->faces[face_index];
		const sg_configuration_cell_t *owner = NULL;
		uint32_t owner_index;

		for (owner_index = 0U; owner_index < configuration->cell_count;
			owner_index++)
			if (RangeWithin(configuration->cells[owner_index].first_face,
				configuration->cells[owner_index].face_count,
				configuration->face_count) &&
				face_index >= configuration->cells[owner_index].first_face &&
				face_index < configuration->cells[owner_index].first_face +
					configuration->cells[owner_index].face_count)
			{
				owner = &configuration->cells[owner_index];
				break;
			}
		if (owner == NULL)
		{
			uint32_t overlap_index;

			for (overlap_index = 0U;
				overlap_index < configuration->stance_overlap_count; overlap_index++)
			{
				const sg_configuration_stance_overlap_t *overlap =
					&configuration->stance_overlaps[overlap_index];

				if (overlap->standing_cell < configuration->cell_count &&
					overlap->crouching_cell < configuration->cell_count &&
					RangeWithin(overlap->first_face, overlap->face_count,
						configuration->face_count) &&
					face_index >= overlap->first_face &&
					face_index < overlap->first_face + overlap->face_count)
				{
					owner = &configuration->cells[overlap->standing_cell];
					break;
				}
			}
		}
		if (owner == NULL || !ConfigurationPlaneFinite(&face->plane) ||
			(face->kind != SG_CONFIGURATION_FACE_FACET &&
				face->kind != SG_CONFIGURATION_FACE_CONSTRAINT_ONLY) ||
			!SourceFromFace(face, owner, world, &(sg_rune_compact_source_t){ 0 }))
		{
			SetError(error, SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_CONFIGURATION,
				SG_RUNE_COMPACT_GEOMETRY_RECORD_FACE, face_index);
			return 0;
		}
		if (face->kind == SG_CONFIGURATION_FACE_FACET)
		{
			if (face->vertex_count < 3U || !RangeWithin(face->first_vertex,
				face->vertex_count, configuration->vertex_count))
			{
				SetError(error, SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_GEOMETRY,
					SG_RUNE_COMPACT_GEOMETRY_RECORD_FACE, face_index);
				return 0;
			}
		}
		else if (face->vertex_count != 0U)
		{
			SetError(error, SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_CONFIGURATION,
				SG_RUNE_COMPACT_GEOMETRY_RECORD_FACE, face_index);
			return 0;
		}
	}
	for (portal_index = 0U; portal_index < configuration->stance_overlap_count;
		portal_index++)
	{
		const sg_configuration_stance_overlap_t *overlap =
			&configuration->stance_overlaps[portal_index];

		if (overlap->standing_cell >= configuration->cell_count ||
			overlap->crouching_cell >= configuration->cell_count ||
			overlap->standing_cell == overlap->crouching_cell ||
			configuration->cells[overlap->standing_cell].stance !=
				SG_RUNE_STANCE_STANDING ||
			configuration->cells[overlap->crouching_cell].stance !=
				SG_RUNE_STANCE_CROUCHING || overlap->face_count == 0U ||
			!RangeWithin(overlap->first_face, overlap->face_count,
				configuration->face_count) ||
			!FiniteVector(overlap->bounds.mins.value) ||
			!FiniteVector(overlap->bounds.maxs.value) ||
			!FiniteVector(overlap->interior_witness.value))
		{
			SetError(error, SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_CONFIGURATION,
				SG_RUNE_COMPACT_GEOMETRY_RECORD_CELL, portal_index);
			return 0;
		}
	}
	for (portal_index = 0U; portal_index < configuration->portal_count;
		portal_index++)
	{
		const sg_configuration_portal_t *portal =
			&configuration->portals[portal_index];
		uint32_t vertex;

		if (portal->from_cell >= configuration->cell_count ||
			portal->to_cell >= configuration->cell_count ||
			portal->from_cell == portal->to_cell ||
			portal->stance >= SG_RUNE_STANCE_COUNT ||
			configuration->cells[portal->from_cell].stance != portal->stance ||
			configuration->cells[portal->to_cell].stance != portal->stance ||
			!ConfigurationPlaneFinite(&portal->plane) || portal->vertex_count < 3U ||
			!RangeWithin(portal->first_vertex, portal->vertex_count,
				configuration->vertex_count) || !isfinite(portal->clearance) ||
			!(portal->clearance > 0.0f))
		{
			SetError(error, SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_CONFIGURATION,
				SG_RUNE_COMPACT_GEOMETRY_RECORD_PORTAL, portal_index);
			return 0;
		}
		for (vertex = 0U; vertex < portal->vertex_count; vertex++)
			if (!FiniteVector(configuration->vertices[portal->first_vertex + vertex].value))
			{
				SetError(error, SG_RUNE_COMPACT_GEOMETRY_ERROR_NONFINITE_GEOMETRY,
					SG_RUNE_COMPACT_GEOMETRY_RECORD_PORTAL, portal_index);
				return 0;
			}
	}
	return 1;
}

static int CellSourceCompare(const sg_configuration_space_t *configuration,
	uint32_t left_index, uint32_t right_index)
{
	const sg_configuration_cell_t *left = &configuration->cells[left_index];
	const sg_configuration_cell_t *right = &configuration->cells[right_index];
	int32_t left_cluster = left->bsp_cluster.index == UINT32_MAX ? -1 :
		(int32_t)left->bsp_cluster.index;
	int32_t right_cluster = right->bsp_cluster.index == UINT32_MAX ? -1 :
		(int32_t)right->bsp_cluster.index;

	if (left->bsp_leaf.index != right->bsp_leaf.index)
		return left->bsp_leaf.index < right->bsp_leaf.index ? -1 : 1;
	if (left->bsp_area.index != right->bsp_area.index)
		return left->bsp_area.index < right->bsp_area.index ? -1 : 1;
	if (left_cluster != right_cluster)
		return left_cluster < right_cluster ? -1 : 1;
	if (left_index != right_index)
		return left_index < right_index ? -1 : 1;
	return 0;
}


static int CompactSourceCompare(const sg_rune_compact_source_t *left,
	const sg_rune_compact_source_t *right)
{
	if (left->kind != right->kind)
		return left->kind < right->kind ? -1 : 1;
	switch (left->kind)
	{
	case SG_RUNE_COMPACT_SOURCE_DOMAIN:
		if (left->value.domain.axis != right->value.domain.axis)
			return left->value.domain.axis < right->value.domain.axis ? -1 : 1;
		if (left->value.domain.maximum_side !=
			right->value.domain.maximum_side)
			return left->value.domain.maximum_side <
				right->value.domain.maximum_side ? -1 : 1;
		return 0;
	case SG_RUNE_COMPACT_SOURCE_BSP_PLANE:
		if (left->value.bsp_plane.model != right->value.bsp_plane.model)
			return left->value.bsp_plane.model < right->value.bsp_plane.model ? -1 : 1;
		if (left->value.bsp_plane.leaf != right->value.bsp_plane.leaf)
			return left->value.bsp_plane.leaf < right->value.bsp_plane.leaf ? -1 : 1;
		if (left->value.bsp_plane.plane != right->value.bsp_plane.plane)
			return left->value.bsp_plane.plane < right->value.bsp_plane.plane ? -1 : 1;
		return 0;
	case SG_RUNE_COMPACT_SOURCE_EXPANDED_BRUSH_SIDE:
		if (left->value.brush_side.model != right->value.brush_side.model)
			return left->value.brush_side.model <
				right->value.brush_side.model ? -1 : 1;
		if (left->value.brush_side.brush != right->value.brush_side.brush)
			return left->value.brush_side.brush <
				right->value.brush_side.brush ? -1 : 1;
		if (left->value.brush_side.brush_side !=
			right->value.brush_side.brush_side)
			return left->value.brush_side.brush_side <
				right->value.brush_side.brush_side ? -1 : 1;
		if (left->value.brush_side.plane != right->value.brush_side.plane)
			return left->value.brush_side.plane <
				right->value.brush_side.plane ? -1 : 1;
		return 0;
	case SG_RUNE_COMPACT_SOURCE_SPLIT:
		if (left->value.split.parent_facet.value !=
			right->value.split.parent_facet.value)
			return left->value.split.parent_facet.value <
				right->value.split.parent_facet.value ? -1 : 1;
		if (left->value.split.ordinal != right->value.split.ordinal)
			return left->value.split.ordinal < right->value.split.ordinal ? -1 : 1;
		return 0;
	case SG_RUNE_COMPACT_SOURCE_KIND_COUNT:
		break;
	}
	return 0;
}

static int PlaneBitsCompare(const sg_rune_binary32_plane_t *left,
	const sg_rune_binary32_plane_t *right)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
	{
		if (left->normal_bits[axis] < right->normal_bits[axis])
			return -1;
		if (left->normal_bits[axis] > right->normal_bits[axis])
			return 1;
	}
	if (left->distance_bits < right->distance_bits)
		return -1;
	if (left->distance_bits > right->distance_bits)
		return 1;
	return 0;
}

static int EntryCompare(const geometry_entry_t *left,
	const geometry_entry_t *right)
{
	int comparison = CompactSourceCompare(&left->source, &right->source);
	uint32_t index;

	if (comparison != 0)
		return comparison;
	comparison = PlaneBitsCompare(&left->plane, &right->plane);
	if (comparison != 0)
		return comparison;
	if (left->kind != right->kind)
		return left->kind < right->kind ? -1 : 1;
	for (index = 0U; index < left->polygon.count &&
		index < right->polygon.count; index++)
	{
		comparison = Q8Compare(&left->polygon.vertices[index],
			&right->polygon.vertices[index]);
		if (comparison != 0)
			return comparison;
	}
	if (left->polygon.count != right->polygon.count)
		return left->polygon.count < right->polygon.count ? -1 : 1;
	if (left->kind == SG_RUNE_COMPACT_FACET_CONSTRAINT_ONLY &&
		left->owner_region != right->owner_region)
		return left->owner_region < right->owner_region ? -1 : 1;
	return left->serial < right->serial ? -1 : left->serial > right->serial;
}

static int SortEntries(geometry_entry_t *entries, uint32_t count,
	geometry_entry_t *temporary)
{
	uint32_t width = 1U;

	while (width < count)
	{
		uint32_t start = 0U;

		while (start < count)
		{
			uint32_t middle = start + width < count ? start + width : count;
			uint32_t end = middle + width < count ? middle + width : count;
			uint32_t left = start;
			uint32_t right = middle;
			uint32_t output = start;

			while (left < middle || right < end)
			{
				if (right == end || (left < middle &&
					EntryCompare(&entries[left], &entries[right]) <= 0))
					temporary[output++] = entries[left++];
				else
					temporary[output++] = entries[right++];
			}
			if (width > count - start)
				break;
			start += width;
			if (width > count - start)
				break;
			start += width;
		}
		memcpy(entries, temporary, (size_t)count * sizeof(entries[0]));
		if (width > count / 2U)
			break;
		width *= 2U;
	}
	return 1;
}


static int PartitionPolygonCopyFromPortal(
	const sg_configuration_space_t *configuration,
	const sg_configuration_portal_t *portal, uint32_t portal_index,
	sg_rune_compact_partition_polygon_t *polygon)
{
	memset(polygon, 0, sizeof(*polygon));
	polygon->plane = portal->plane;
	polygon->source_plane_index = portal_index;
	polygon->contributor = portal_index;
	polygon->open = 0U;
	polygon->vertices = &configuration->vertices[portal->first_vertex];
	polygon->vertex_count = portal->vertex_count;
	return 1;
}

static int ConvertPartitionPolygon(const geometry_context_t *context,
	const sg_rune_compact_partition_polygon_t *source,
	const sg_rune_binary32_plane_t *plane, geometry_polygon_t *destination)
{
	size_t bytes;
	uint32_t vertex;
	uint32_t axis;

	memset(destination, 0, sizeof(*destination));
	if (source->vertices == NULL || source->vertex_count < 3U ||
		!SizeForCount(source->vertex_count, sizeof(*destination->vertices), &bytes))
		return 0;
	destination->vertices = Allocate(context, bytes);
	if (destination->vertices == NULL)
		return 0;
	for (vertex = 0U; vertex < source->vertex_count; vertex++)
		for (axis = 0U; axis < 3U; axis++)
			if (!ToQ8(source->vertices[vertex].value[axis],
				&destination->vertices[vertex].value[axis]))
			{
				PolygonRelease(context, destination);
				return 0;
			}
	destination->count = source->vertex_count;
	if (!CanonicalizePolygon(destination, plane))
	{
		PolygonRelease(context, destination);
		return 0;
	}
	return 1;
}

static int SubtractPiece(const geometry_context_t *context,
	geometry_piece_vector_t *pieces,
	const sg_rune_compact_partition_polygon_t *portal,
	sg_rune_compact_geometry_error_t *error, uint32_t portal_index)
{
	geometry_piece_vector_t next;
	sg_rune_compact_partition_allocator_t allocator;
	uint32_t index;

	memset(&next, 0, sizeof(next));
	SetPartitionAllocator(context, &allocator);
	for (index = 0U; index < pieces->count; index++)
	{
		sg_rune_compact_partition_subtraction_t subtraction;
		sg_rune_compact_partition_error_t partition_error;
		uint32_t remainder;

		memset(&subtraction, 0, sizeof(subtraction));
		memset(&partition_error, 0, sizeof(partition_error));
		if (!SG_RuneCompactPartitionSubtractPolygon(&pieces->values[index], portal,
			&allocator, &subtraction, &partition_error))
		{
			SetPartitionError(context, &partition_error,
				SG_RUNE_COMPACT_GEOMETRY_RECORD_PORTAL, portal_index);
			SG_RuneCompactPartitionSubtractionDestroy(&subtraction, &allocator);
			PartitionPieceVectorRelease(context, &next);
			return 0;
		}
		for (remainder = 0U; remainder < subtraction.remainder_count; remainder++)
			if (!PartitionPieceVectorPush(context, &next,
				&subtraction.remainders[remainder]))
			{
				SetError(error, SG_RUNE_COMPACT_GEOMETRY_ERROR_OVERFLOW,
					SG_RUNE_COMPACT_GEOMETRY_RECORD_PORTAL, portal_index);
				SG_RuneCompactPartitionSubtractionDestroy(&subtraction, &allocator);
				PartitionPieceVectorRelease(context, &next);
				return 0;
			}
		/* Ownership of the remainders moved into next. */
		for (remainder = 0U; remainder < subtraction.remainder_count; remainder++)
			memset(&subtraction.remainders[remainder], 0,
				sizeof(subtraction.remainders[remainder]));
		SG_RuneCompactPartitionSubtractionDestroy(&subtraction, &allocator);
		PartitionPolygonRelease(context, &pieces->values[index]);
	}
	Release(context, pieces->values);
	*pieces = next;
	return 1;
}

static int FindFaceForPortal(const sg_configuration_space_t *configuration,
	const sg_configuration_portal_t *portal, uint32_t cell_index,
	uint32_t *face_out)
{
	const sg_configuration_cell_t *cell = &configuration->cells[cell_index];
	uint32_t offset;

	for (offset = 0U; offset < cell->face_count; offset++)
	{
		const uint32_t face_index = cell->first_face + offset;

		if (configuration->faces[face_index].kind ==
				SG_CONFIGURATION_FACE_FACET &&
			PlanesEquivalent(&configuration->faces[face_index].plane,
				&portal->plane))
		{
			*face_out = face_index;
			return 1;
		}
	}
	return 0;
}

static int CellOrderLess(const sg_configuration_space_t *configuration,
	uint32_t left, uint32_t right)
{
	const int comparison = CellSourceCompare(configuration, left, right);

	return comparison < 0 || (comparison == 0 && left < right);
}

static int BuildPortalWorks(const geometry_context_t *context,
	const sg_configuration_space_t *configuration,
	const sg_bsp_world_t *world,
	geometry_portal_vector_t *portals)
{
	uint32_t portal_index;

	for (portal_index = 0U; portal_index < configuration->portal_count;
		portal_index++)
	{
		const sg_configuration_portal_t *portal =
			&configuration->portals[portal_index];
		geometry_portal_work_t work;
		uint32_t low_face;
		uint32_t high_face;
		double low_signed;
		double high_signed;
		int flip;
		sg_configuration_plane_t oriented_plane;

		if (!FindFaceForPortal(configuration, portal, portal->from_cell,
			&low_face) || !FindFaceForPortal(configuration, portal, portal->to_cell,
			&high_face))
		{
			SetError(context->error,
				SG_RUNE_COMPACT_GEOMETRY_ERROR_UNSUPPORTED_TOPOLOGY,
				SG_RUNE_COMPACT_GEOMETRY_RECORD_PORTAL, portal_index);
			return 0;
		}
		memset(&work, 0, sizeof(work));
		work.config_index = portal_index;
		if (CellOrderLess(configuration, portal->from_cell, portal->to_cell))
		{
			work.low_config = portal->from_cell;
			work.high_config = portal->to_cell;
			work.low_face = low_face;
			work.high_face = high_face;
		}
		else
		{
			work.low_config = portal->to_cell;
			work.high_config = portal->from_cell;
			work.low_face = high_face;
			work.high_face = low_face;
		}
		low_signed = ConfigurationPlaneSigned(&portal->plane,
			&configuration->cells[work.low_config].interior_witness);
		high_signed = ConfigurationPlaneSigned(&portal->plane,
			&configuration->cells[work.high_config].interior_witness);
		if (!isfinite(low_signed) || !isfinite(high_signed) ||
			low_signed * high_signed >= -GEOMETRY_EPSILON)
		{
			SetError(context->error,
				SG_RUNE_COMPACT_GEOMETRY_ERROR_UNSUPPORTED_TOPOLOGY,
				SG_RUNE_COMPACT_GEOMETRY_RECORD_PORTAL, portal_index);
			return 0;
		}
		flip = low_signed > high_signed;
		oriented_plane = portal->plane;
		ConfigurationPlaneToCompact(&portal->plane, &work.plane);
		if (flip)
		{
			uint32_t axis;

			FlipCompactPlane(&work.plane);
			for (axis = 0U; axis < 3U; axis++)
				oriented_plane.normal[axis] = -oriented_plane.normal[axis];
			oriented_plane.distance = -oriented_plane.distance;
		}
		if (!CompactPlaneFinite(&work.plane))
		{
			SetError(context->error, SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_GEOMETRY,
				SG_RUNE_COMPACT_GEOMETRY_RECORD_PORTAL, portal_index);
			return 0;
		}
		if (!SourceFromOrientedFace(&configuration->faces[work.low_face],
			&oriented_plane, &configuration->cells[work.low_config], world,
			&work.source))
		{
			SetError(context->error,
				SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_GEOMETRY_RECORD_PORTAL, portal_index);
			return 0;
		}
		if (!PolygonFromFloat(context,
			&configuration->vertices[portal->first_vertex], portal->vertex_count,
			&work.plane, &work.polygon))
		{
			SetError(context->error, SG_RUNE_COMPACT_GEOMETRY_ERROR_Q8_CONVERSION,
				SG_RUNE_COMPACT_GEOMETRY_RECORD_PORTAL, portal_index);
			return 0;
		}
		work.valid_stances = (sg_rune_stance_validity_t)(
			UINT8_C(1) << (uint32_t)portal->stance);
		work.direction = SG_RUNE_PORTAL_CONTINUITY_BOTH;
		work.open = (uint8_t)(
			configuration->faces[work.low_face].plane.source_kind ==
				SG_CONFIGURATION_PLANE_EXPANDED_BRUSH &&
			configuration->faces[work.low_face].plane.reversed != 0U);
		if (!PortalVectorPush(context, portals, &work))
		{
			PolygonRelease(context, &work.polygon);
			SetError(context->error, SG_RUNE_COMPACT_GEOMETRY_ERROR_OVERFLOW,
				SG_RUNE_COMPACT_GEOMETRY_RECORD_PORTAL, portal_index);
			return 0;
		}
	}
	return 1;
}

static int ClipPolygonToRegion(const geometry_context_t *context,
	const sg_rune_compact_partition_polygon_t *source,
	const geometry_region_work_t *region,
	sg_rune_compact_partition_polygon_t *result)
{
	sg_rune_compact_partition_allocator_t allocator;
	sg_rune_compact_partition_polygon_t current;
	uint32_t face_index;

	memset(&current, 0, sizeof(current));
	if (!PartitionPolygonOwnedCopy(context, source, &current))
		return 0;
	SetPartitionAllocator(context, &allocator);
	for (face_index = 0U; face_index < region->halfspaces.count;
		face_index++)
	{
		const sg_rune_compact_partition_halfspace_t *halfspace =
			&region->halfspaces.values[face_index];
		sg_rune_compact_partition_polygon_t clipped;
		sg_rune_compact_partition_error_t error;

		memset(&clipped, 0, sizeof(clipped));
		memset(&error, 0, sizeof(error));
		if (!SG_RuneCompactPartitionClipPolygon(&current, halfspace,
			&allocator, &clipped, &error))
		{
			PartitionPolygonRelease(context, &current);
			SetPartitionError(context, &error,
				SG_RUNE_COMPACT_GEOMETRY_RECORD_PORTAL,
				source->source_plane_index);
			return 0;
		}
		PartitionPolygonRelease(context, &current);
		current = clipped;
		if (current.vertex_count == 0U)
			break;
	}
	*result = current;
	return 1;
}

static int ExpandPortalsToRegions(const geometry_context_t *context,
	const sg_configuration_space_t *configuration,
	const geometry_region_vector_t *regions,
	const geometry_portal_vector_t *base,
	geometry_portal_vector_t *expanded)
{
	uint32_t portal_index;

	memset(expanded, 0, sizeof(*expanded));
	for (portal_index = 0U; portal_index < base->count; portal_index++)
	{
		const geometry_portal_work_t *portal = &base->values[portal_index];
		sg_rune_compact_partition_polygon_t raw;
		uint32_t low_region;
		uint32_t fragment_count = 0U;

		memset(&raw, 0, sizeof(raw));
		if (!PartitionPolygonCopyFromPortal(configuration,
			&configuration->portals[portal->config_index], portal->config_index,
			&raw))
			return 0;
		for (low_region = 0U; low_region < regions->count; low_region++)
		{
			uint32_t high_region;

			if (!RegionContainsConfigurationCell(&regions->values[low_region],
				portal->low_config))
				continue;
			for (high_region = 0U; high_region < regions->count; high_region++)
			{
				sg_rune_compact_partition_polygon_t low_piece;
				sg_rune_compact_partition_polygon_t piece;
				geometry_portal_work_t value;

				if (low_region == high_region ||
					!RegionContainsConfigurationCell(&regions->values[high_region],
						portal->high_config))
					continue;
				memset(&low_piece, 0, sizeof(low_piece));
				memset(&piece, 0, sizeof(piece));
				if (!ClipPolygonToRegion(context, &raw,
					&regions->values[low_region], &low_piece))
				{
					PartitionPolygonRelease(context, &low_piece);
					return 0;
				}
				if (low_piece.vertex_count < 3U)
				{
					PartitionPolygonRelease(context, &low_piece);
					continue;
				}
				if (!ClipPolygonToRegion(context, &low_piece,
					&regions->values[high_region], &piece))
				{
					PartitionPolygonRelease(context, &low_piece);
					PartitionPolygonRelease(context, &piece);
					return 0;
				}
				PartitionPolygonRelease(context, &low_piece);
				if (piece.vertex_count < 3U)
				{
					PartitionPolygonRelease(context, &piece);
					continue;
				}
				value = *portal;
				memset(&value.polygon, 0, sizeof(value.polygon));
				value.low_region = low_region;
				value.high_region = high_region;
				if (!ConvertPartitionPolygon(context, &piece, &value.plane,
					&value.polygon))
				{
					PartitionPolygonRelease(context, &piece);
					return 0;
				}
				PartitionPolygonRelease(context, &piece);
				if (!PortalVectorPush(context, expanded, &value))
				{
					PolygonRelease(context, &value.polygon);
					SetError(context->error,
						SG_RUNE_COMPACT_GEOMETRY_ERROR_OVERFLOW,
						SG_RUNE_COMPACT_GEOMETRY_RECORD_PORTAL,
						portal->config_index);
					return 0;
				}
				fragment_count++;
			}
		}
		if (fragment_count == 0U)
		{
			SetError(context->error,
				SG_RUNE_COMPACT_GEOMETRY_ERROR_UNSUPPORTED_TOPOLOGY,
				SG_RUNE_COMPACT_GEOMETRY_RECORD_PORTAL,
				portal->config_index);
			return 0;
		}
	}
	return 1;
}

static int BoundsToQ8(const sg_rune_bounds_t *bounds,
	sg_rune_q8_bounds_t *compact_bounds)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		if (!ToQ8(bounds->mins.value[axis], &compact_bounds->mins.value[axis]) ||
			!ToQ8(bounds->maxs.value[axis], &compact_bounds->maxs.value[axis]) ||
			compact_bounds->mins.value[axis] > compact_bounds->maxs.value[axis])
			return 0;
	return 1;
}


static int AllocateArray(const geometry_context_t *context, uint32_t count,
	size_t element_size, void **pointer_out,
	sg_rune_compact_geometry_record_domain_t domain)
{
	size_t bytes;

	*pointer_out = NULL;
	if (count == 0U)
		return 1;
	if (!SizeForCount(count, element_size, &bytes))
	{
		SetError(context->error, SG_RUNE_COMPACT_GEOMETRY_ERROR_OVERFLOW,
			domain, GEOMETRY_INDEX_NONE);
		return 0;
	}
	*pointer_out = Allocate(context, bytes);
	if (*pointer_out == NULL)
	{
		SetError(context->error, SG_RUNE_COMPACT_GEOMETRY_ERROR_OUT_OF_MEMORY,
			domain, GEOMETRY_INDEX_NONE);
		return 0;
	}
	memset(*pointer_out, 0, bytes);
	return 1;
}

static const geometry_portal_work_t *PortalWorkForConfiguration(
	const geometry_portal_vector_t *portals, uint32_t configuration_portal,
	uint32_t low_region, uint32_t high_region)
{
	uint32_t index;

	for (index = 0U; index < portals->count; index++)
		if (portals->values[index].config_index == configuration_portal &&
			portals->values[index].low_region == low_region &&
			portals->values[index].high_region == high_region)
			return &portals->values[index];
	return NULL;
}

static int PolygonClearanceQ8(const geometry_polygon_t *polygon,
	uint32_t *clearance_out)
{
	long double area_vector[3] = { 0.0L, 0.0L, 0.0L };
	long double twice_area;
	long double clearance;
	long double rounded;
	long double fraction;
	uint32_t vertex;

	if (polygon == NULL || clearance_out == NULL ||
		polygon->vertices == NULL || polygon->count < 3U)
		return 0;
	for (vertex = 0U; vertex < polygon->count; vertex++)
	{
		const sg_rune_q8_vec3_t *point = &polygon->vertices[vertex];
		const sg_rune_q8_vec3_t *next =
			&polygon->vertices[(vertex + 1U) % polygon->count];

		area_vector[0] += (long double)point->value[1] * next->value[2] -
			(long double)point->value[2] * next->value[1];
		area_vector[1] += (long double)point->value[2] * next->value[0] -
			(long double)point->value[0] * next->value[2];
		area_vector[2] += (long double)point->value[0] * next->value[1] -
			(long double)point->value[1] * next->value[0];
	}
	twice_area = sqrtl(area_vector[0] * area_vector[0] +
		area_vector[1] * area_vector[1] +
		area_vector[2] * area_vector[2]);
	clearance = sqrtl(twice_area * 0.5L);
	if (!isfinite(clearance) || !(clearance > 0.0L) ||
		clearance > (long double)UINT32_MAX + 0.5L)
		return 0;
	rounded = floorl(clearance);
	fraction = clearance - rounded;
	if (fraction > 0.5L ||
		(fraction == 0.5L && fmodl(rounded, 2.0L) == 1.0L))
		rounded += 1.0L;
	if (!(rounded > 0.0L) || rounded > (long double)UINT32_MAX)
		return 0;
	*clearance_out = (uint32_t)rounded;
	return 1;
}

static int BuildFinalArrays(const geometry_context_t *context,
	const geometry_portal_vector_t *portals,
	const geometry_entry_vector_t *entries, sg_rune_compact_geometry_t *geometry)
{
	uint32_t *cell_counts = NULL;
	uint32_t *cell_cursors = NULL;
	uint32_t *cell_offsets = NULL;
	uint32_t facet_index;
	uint32_t vertex_count = 0U;
	uint32_t incidence_count = 0U;
	uint32_t output_portal_count = portals->count;
	uint32_t portal_cursor = 0U;
	size_t count_bytes;

	if (entries->count == 0U || entries->count > SG_RUNE_COMPACT_MAX_FACETS ||
		!SizeForCount(geometry->cell_count, sizeof(*cell_counts), &count_bytes))
	{
		SetError(context->error, SG_RUNE_COMPACT_GEOMETRY_ERROR_OVERFLOW,
			SG_RUNE_COMPACT_GEOMETRY_RECORD_RESULT, GEOMETRY_INDEX_NONE);
		return 0;
	}
	for (facet_index = 0U; facet_index < entries->count; facet_index++)
	{
		const geometry_entry_t *entry = &entries->values[facet_index];
		uint32_t next;

		if (entry->polygon.count != 0U &&
			!AddU32(vertex_count, entry->polygon.count, &next))
		{
			SetError(context->error, SG_RUNE_COMPACT_GEOMETRY_ERROR_OVERFLOW,
				SG_RUNE_COMPACT_GEOMETRY_RECORD_FACE, facet_index);
			return 0;
		}
		if (entry->polygon.count != 0U)
			vertex_count = next;
		if (!AddU32(incidence_count,
			(entry->portal != 0U || entry->shared != 0U) ? 2U : 1U, &next))
		{
			SetError(context->error, SG_RUNE_COMPACT_GEOMETRY_ERROR_OVERFLOW,
				SG_RUNE_COMPACT_GEOMETRY_RECORD_FACE, facet_index);
			return 0;
		}
		incidence_count = next;
		if (entry->shared != 0U &&
			!AddU32(output_portal_count, 1U, &output_portal_count))
		{
			SetError(context->error, SG_RUNE_COMPACT_GEOMETRY_ERROR_OVERFLOW,
				SG_RUNE_COMPACT_GEOMETRY_RECORD_FACE, facet_index);
			return 0;
		}
	}
	if (vertex_count > SG_RUNE_COMPACT_MAX_VERTICES ||
		incidence_count > SG_RUNE_COMPACT_MAX_INCIDENCES ||
		output_portal_count > SG_RUNE_COMPACT_MAX_PORTALS)
	{
		SetError(context->error, SG_RUNE_COMPACT_GEOMETRY_ERROR_OVERFLOW,
			SG_RUNE_COMPACT_GEOMETRY_RECORD_RESULT, GEOMETRY_INDEX_NONE);
		return 0;
	}
	if (!AllocateArray(context, geometry->cell_count, sizeof(*cell_counts),
		(void **)&cell_counts, SG_RUNE_COMPACT_GEOMETRY_RECORD_CELL) ||
		!AllocateArray(context, geometry->cell_count, sizeof(*cell_cursors),
		(void **)&cell_cursors, SG_RUNE_COMPACT_GEOMETRY_RECORD_CELL) ||
		!AllocateArray(context, geometry->cell_count, sizeof(*cell_offsets),
		(void **)&cell_offsets, SG_RUNE_COMPACT_GEOMETRY_RECORD_CELL))
		goto failure;
	for (facet_index = 0U; facet_index < entries->count; facet_index++)
	{
		const geometry_entry_t *entry = &entries->values[facet_index];
		uint32_t cell;

		cell = (entry->portal != 0U || entry->shared != 0U) ?
			entry->low_region : entry->owner_region;
		if (cell == GEOMETRY_INDEX_NONE || cell >= geometry->cell_count)
		{
			SetError(context->error, SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_GEOMETRY_RECORD_FACE, facet_index);
			goto failure;
		}
		cell_counts[cell]++;
		if (entry->portal != 0U || entry->shared != 0U)
		{
			cell = entry->high_region;
			if (cell == GEOMETRY_INDEX_NONE || cell >= geometry->cell_count)
			{
				SetError(context->error,
					SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_GEOMETRY_RECORD_FACE, facet_index);
				goto failure;
			}
			cell_counts[cell]++;
		}
	}
	{
		uint32_t cursor = 0U;

		for (facet_index = 0U; facet_index < geometry->cell_count; facet_index++)
		{
			geometry->cells[facet_index].incidences.first = cursor;
			geometry->cells[facet_index].incidences.count = cell_counts[facet_index];
			cell_offsets[facet_index] = cursor;
			if (!AddU32(cursor, cell_counts[facet_index], &cursor))
			{
				SetError(context->error, SG_RUNE_COMPACT_GEOMETRY_ERROR_OVERFLOW,
					SG_RUNE_COMPACT_GEOMETRY_RECORD_CELL, facet_index);
				goto failure;
			}
		}
		if (cursor != incidence_count)
		{
			SetError(context->error, SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_GEOMETRY_RECORD_RESULT, GEOMETRY_INDEX_NONE);
			goto failure;
		}
	}
	if (!AllocateArray(context, entries->count, sizeof(*geometry->facets),
		(void **)&geometry->facets, SG_RUNE_COMPACT_GEOMETRY_RECORD_FACE) ||
		!AllocateArray(context, incidence_count, sizeof(*geometry->incidences),
		(void **)&geometry->incidences, SG_RUNE_COMPACT_GEOMETRY_RECORD_FACE) ||
		!AllocateArray(context, incidence_count, sizeof(*geometry->cell_incidences),
		(void **)&geometry->cell_incidences, SG_RUNE_COMPACT_GEOMETRY_RECORD_FACE) ||
		!AllocateArray(context, vertex_count, sizeof(*geometry->vertices),
		(void **)&geometry->vertices, SG_RUNE_COMPACT_GEOMETRY_RECORD_FACE) ||
		!AllocateArray(context, output_portal_count, sizeof(*geometry->portals),
			(void **)&geometry->portals, SG_RUNE_COMPACT_GEOMETRY_RECORD_PORTAL))
		goto failure;
	geometry->facet_count = entries->count;
	geometry->incidence_count = incidence_count;
	geometry->cell_incidence_count = incidence_count;
	geometry->vertex_count = vertex_count;
	geometry->portal_count = output_portal_count;
	{
		uint32_t vertex_cursor = 0U;
		uint32_t incidence_cursor = 0U;

		for (facet_index = 0U; facet_index < entries->count; facet_index++)
		{
			const geometry_entry_t *entry = &entries->values[facet_index];
			sg_rune_compact_facet_t *facet = &geometry->facets[facet_index];
			uint32_t portal_clearance_q8 = 0U;
			uint32_t cell;
			uint32_t local;

			if ((entry->portal != 0U || entry->shared != 0U) &&
				!PolygonClearanceQ8(&entry->polygon, &portal_clearance_q8))
			{
				SetError(context->error,
					SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_GEOMETRY,
					SG_RUNE_COMPACT_GEOMETRY_RECORD_FACE, facet_index);
				goto failure;
			}

			facet->source = entry->source;
			facet->plane = entry->plane;
			facet->kind = entry->kind;
			facet->vertices.first = vertex_cursor;
			facet->vertices.count = entry->polygon.count;
			if (entry->polygon.count != 0U)
			{
				memcpy(&geometry->vertices[vertex_cursor], entry->polygon.vertices,
					(size_t)entry->polygon.count * sizeof(geometry->vertices[0]));
				vertex_cursor += entry->polygon.count;
			}
			facet->incidences.first = incidence_cursor;
			facet->incidences.count =
				(entry->portal != 0U || entry->shared != 0U) ? 2U : 1U;
			facet->portal.value = GEOMETRY_INDEX_NONE;
			cell = (entry->portal != 0U || entry->shared != 0U) ?
				entry->low_region : entry->owner_region;
			if (cell >= geometry->cell_count)
			{
				SetError(context->error,
					SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_GEOMETRY_RECORD_FACE, facet_index);
				goto failure;
			}
			local = cell_cursors[cell]++;
			geometry->incidences[incidence_cursor].cell.value = cell;
			geometry->incidences[incidence_cursor].facet.value = facet_index;
			geometry->incidences[incidence_cursor].cell_ordinal = local;
			geometry->incidences[incidence_cursor].side =
				(entry->portal != 0U || entry->shared != 0U) ?
					SG_RUNE_FACET_NEGATIVE_SIDE :
				SG_RUNE_FACET_NEGATIVE_SIDE;
			geometry->incidences[incidence_cursor].boundary =
				entry->open == 0U ? SG_RUNE_BOUNDARY_CLOSED :
				SG_RUNE_BOUNDARY_OPEN;
			geometry->cell_incidences[cell_offsets[cell] + local].value =
				incidence_cursor;
			if (entry->portal != 0U || entry->shared != 0U)
			{
				const uint32_t negative_incidence = incidence_cursor;
				const geometry_portal_work_t *portal = entry->portal != 0U ?
					PortalWorkForConfiguration(portals, entry->portal_config,
						entry->low_region, entry->high_region) : NULL;

				if (entry->portal != 0U && portal == NULL)
				{
					SetError(context->error,
						SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_REFERENCE,
						SG_RUNE_COMPACT_GEOMETRY_RECORD_PORTAL,
						entry->portal_config);
					goto failure;
				}

				cell = entry->high_region;
				if (cell >= geometry->cell_count)
				{
					SetError(context->error,
						SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_REFERENCE,
						SG_RUNE_COMPACT_GEOMETRY_RECORD_FACE, facet_index);
					goto failure;
				}
				local = cell_cursors[cell]++;
				incidence_cursor++;
				geometry->incidences[incidence_cursor].cell.value = cell;
				geometry->incidences[incidence_cursor].facet.value = facet_index;
				geometry->incidences[incidence_cursor].cell_ordinal = local;
				geometry->incidences[incidence_cursor].side = SG_RUNE_FACET_POSITIVE_SIDE;
				geometry->incidences[incidence_cursor].boundary =
					entry->open == 0U ? SG_RUNE_BOUNDARY_OPEN :
					SG_RUNE_BOUNDARY_CLOSED;
				geometry->cell_incidences[cell_offsets[cell] + local].value =
					incidence_cursor;
				if (entry->portal != 0U)
				{
					if (geometry->portals == NULL ||
						portal_cursor >= output_portal_count)
					{
						SetError(context->error,
							SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_REFERENCE,
							SG_RUNE_COMPACT_GEOMETRY_RECORD_PORTAL,
							portal_cursor);
						goto failure;
					}
					facet->portal.value = portal_cursor;
					geometry->portals[portal_cursor].source = entry->source;
					geometry->portals[portal_cursor].facet.value = facet_index;
					geometry->portals[portal_cursor].negative_incidence.value =
						negative_incidence;
					geometry->portals[portal_cursor].positive_incidence.value =
						incidence_cursor;
					geometry->portals[portal_cursor].clearance_q8 =
						portal_clearance_q8;
					geometry->portals[portal_cursor].direction = portal->direction;
					geometry->portals[portal_cursor].valid_stances =
						portal->valid_stances;
					portal_cursor++;
				}
				else
				{
					const sg_rune_stance_validity_t shared_stances =
						(sg_rune_stance_validity_t)(
							geometry->cells[entry->low_region].valid_stances &
							geometry->cells[entry->high_region].valid_stances);

					if (shared_stances == 0U || geometry->portals == NULL ||
						portal_cursor >= output_portal_count)
					{
						SetError(context->error,
							SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_GEOMETRY,
							SG_RUNE_COMPACT_GEOMETRY_RECORD_FACE, facet_index);
						goto failure;
					}
					facet->portal.value = portal_cursor;
					geometry->portals[portal_cursor].source = entry->source;
					geometry->portals[portal_cursor].facet.value = facet_index;
					geometry->portals[portal_cursor].negative_incidence.value =
						negative_incidence;
					geometry->portals[portal_cursor].positive_incidence.value =
						incidence_cursor;
					geometry->portals[portal_cursor].clearance_q8 =
						portal_clearance_q8;
					geometry->portals[portal_cursor].direction =
						SG_RUNE_PORTAL_CONTINUITY_BOTH;
					geometry->portals[portal_cursor].valid_stances = shared_stances;
					portal_cursor++;
				}
			}
			incidence_cursor++;
		}
	}
	if (portal_cursor != output_portal_count)
	{
		SetError(context->error, SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_REFERENCE,
			SG_RUNE_COMPACT_GEOMETRY_RECORD_RESULT, GEOMETRY_INDEX_NONE);
		goto failure;
	}
	Release(context, cell_counts);
	Release(context, cell_cursors);
	Release(context, cell_offsets);
	return 1;

failure:
	Release(context, cell_counts);
	Release(context, cell_cursors);
	Release(context, cell_offsets);
	return 0;
}


static void ReleaseGeometryArrays(sg_rune_compact_geometry_t *geometry)
{
	const sg_rune_compact_geometry_allocator_t allocator = geometry->allocator;

	if (geometry->cells != NULL)
		allocator.release(allocator.context, geometry->cells);
	if (geometry->facets != NULL)
		allocator.release(allocator.context, geometry->facets);
	if (geometry->incidences != NULL)
		allocator.release(allocator.context, geometry->incidences);
	if (geometry->cell_incidences != NULL)
		allocator.release(allocator.context, geometry->cell_incidences);
	if (geometry->vertices != NULL)
		allocator.release(allocator.context, geometry->vertices);
	if (geometry->portals != NULL)
		allocator.release(allocator.context, geometry->portals);
	if (geometry->compact_cells_for_configuration_cell != NULL)
		allocator.release(allocator.context,
			geometry->compact_cells_for_configuration_cell);
	if (geometry->configuration_cell_compact_cells != NULL)
		allocator.release(allocator.context,
			geometry->configuration_cell_compact_cells);
	geometry->cells = NULL;
	geometry->facets = NULL;
	geometry->incidences = NULL;
	geometry->cell_incidences = NULL;
	geometry->vertices = NULL;
	geometry->portals = NULL;
	geometry->compact_cells_for_configuration_cell = NULL;
	geometry->configuration_cell_compact_cells = NULL;
}

int SG_RuneCompactGeometryOwnerMaterialize(
	const sg_configuration_space_t *configuration,
	const sg_bsp_world_t *world,
	const sg_rune_compact_identity_t *identity,
	const sg_rune_compact_geometry_allocator_t *allocator,
	sg_rune_compact_geometry_t **geometry_out,
	sg_rune_compact_geometry_error_t *error_out)
{
	sg_rune_compact_geometry_allocator_t configured_allocator;
	geometry_context_t context;
	sg_rune_compact_geometry_t *geometry = NULL;
	geometry_region_vector_t regions;
	geometry_portal_vector_t base_portals;
	geometry_portal_vector_t portals;
	geometry_entry_vector_t entries;
	geometry_entry_t *temporary = NULL;
	size_t bytes;

	memset(&regions, 0, sizeof(regions));
	memset(&base_portals, 0, sizeof(base_portals));
	memset(&portals, 0, sizeof(portals));
	memset(&entries, 0, sizeof(entries));
	ClearError(error_out);
	if (geometry_out == NULL)
	{
		SetError(error_out, SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_GEOMETRY_RECORD_RESULT, GEOMETRY_INDEX_NONE);
		return 0;
	}
	if (!ConfigureAllocator(allocator, &configured_allocator))
	{
		SetError(error_out, SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_GEOMETRY_RECORD_RESULT, GEOMETRY_INDEX_NONE);
		return 0;
	}
	context.allocator = configured_allocator;
	context.error = error_out;
	if (!ConfigurationValid(configuration, world, identity, error_out))
		return 0;
	geometry = (sg_rune_compact_geometry_t *)Allocate(&context, sizeof(*geometry));
	if (geometry == NULL)
	{
		SetError(error_out, SG_RUNE_COMPACT_GEOMETRY_ERROR_OUT_OF_MEMORY,
			SG_RUNE_COMPACT_GEOMETRY_RECORD_RESULT, GEOMETRY_INDEX_NONE);
		return 0;
	}
	memset(geometry, 0, sizeof(*geometry));
	geometry->state = GEOMETRY_STATE;
	geometry->state_inverse = ~GEOMETRY_STATE;
	geometry->self = geometry;
	geometry->allocator = configured_allocator;
	geometry->identity = *identity;
	if (!BuildRegions(&context, configuration, world, &regions))
		goto failure;
	SortRegions(&regions);
	if (!BuildRegionCellsAndMap(&context, configuration, &regions, geometry))
		goto failure;
	if (!BuildPortalWorks(&context, configuration, world, &base_portals) ||
		!ExpandPortalsToRegions(&context, configuration, &regions,
			&base_portals, &portals) ||
		!BuildRegionEntries(&context, configuration, world, &regions, &portals,
			&entries))
		goto failure;
	MergeSharedRegionFaces(&context, &regions, &entries);
	if (!BuildRegionPortalEntries(&context, &portals, &entries))
		goto failure;
	if (!SizeForCount(entries.count, sizeof(*temporary), &bytes))
	{
		SetError(error_out, SG_RUNE_COMPACT_GEOMETRY_ERROR_OVERFLOW,
			SG_RUNE_COMPACT_GEOMETRY_RECORD_RESULT, GEOMETRY_INDEX_NONE);
		goto failure;
	}
	temporary = (geometry_entry_t *)Allocate(&context, bytes);
	if (temporary == NULL)
	{
		SetError(error_out, SG_RUNE_COMPACT_GEOMETRY_ERROR_OUT_OF_MEMORY,
			SG_RUNE_COMPACT_GEOMETRY_RECORD_RESULT, GEOMETRY_INDEX_NONE);
		goto failure;
	}
	SortEntries(entries.values, entries.count, temporary);
	Release(&context, temporary);
	temporary = NULL;
	if (!BuildFinalArrays(&context, &portals,
		&entries, geometry))
		goto failure;
	RegionVectorRelease(&context, &regions);
	PortalVectorRelease(&context, &base_portals);
	PortalVectorRelease(&context, &portals);
	EntryVectorRelease(&context, &entries);
	*geometry_out = geometry;
	return 1;

failure:
	Release(&context, temporary);
	RegionVectorRelease(&context, &regions);
	PortalVectorRelease(&context, &base_portals);
	PortalVectorRelease(&context, &portals);
	EntryVectorRelease(&context, &entries);
	if (geometry != NULL)
	{
		ReleaseGeometryArrays(geometry);
		geometry->state = 0U;
		geometry->state_inverse = 0U;
		geometry->self = NULL;
		configured_allocator.release(configured_allocator.context, geometry);
	}
	return 0;
}

int SG_RuneCompactGeometryMaterialize(
	const sg_rune_compact_builder_t *builder,
	const sg_rune_compact_geometry_allocator_t *allocator,
	sg_rune_compact_geometry_t **geometry_out,
	sg_rune_compact_geometry_error_t *error_out)
{
	sg_rune_compact_builder_owner_view_t owner;
	sg_rune_compact_builder_view_t view;

	if (geometry_out == NULL)
	{
		ClearError(error_out);
		SetError(error_out, SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_GEOMETRY_RECORD_RESULT, GEOMETRY_INDEX_NONE);
		return 0;
	}
	if (!SG_RuneCompactBuilderRead(builder, &view) ||
		!SG_RuneCompactBuilderOwnerRead(builder, &owner))
	{
		ClearError(error_out);
		SetError(error_out, SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_GEOMETRY_RECORD_RESULT, GEOMETRY_INDEX_NONE);
		return 0;
	}
	return SG_RuneCompactGeometryOwnerMaterialize(owner.configuration,
		owner.world, &view.identity, allocator, geometry_out, error_out);
}

int SG_RuneCompactGeometryRead(const sg_rune_compact_geometry_t *geometry,
	sg_rune_compact_geometry_view_t *view_out)
{
	if (geometry == NULL || view_out == NULL || geometry->state != GEOMETRY_STATE ||
		geometry->state_inverse != ~GEOMETRY_STATE || geometry->self != geometry)
		return 0;
	memset(view_out, 0, sizeof(*view_out));
	view_out->identity = geometry->identity;
	view_out->cells = geometry->cells;
	view_out->cell_count = geometry->cell_count;
	view_out->facets = geometry->facets;
	view_out->facet_count = geometry->facet_count;
	view_out->incidences = geometry->incidences;
	view_out->incidence_count = geometry->incidence_count;
	view_out->cell_incidences = geometry->cell_incidences;
	view_out->cell_incidence_count = geometry->cell_incidence_count;
	view_out->vertices = geometry->vertices;
	view_out->vertex_count = geometry->vertex_count;
	view_out->portals = geometry->portals;
	view_out->portal_count = geometry->portal_count;
	view_out->compact_cells_for_configuration_cell =
		geometry->compact_cells_for_configuration_cell;
	view_out->compact_cells_for_configuration_cell_count =
		geometry->compact_cells_for_configuration_cell_count;
	view_out->configuration_cell_compact_cells =
		geometry->configuration_cell_compact_cells;
	view_out->configuration_cell_compact_cell_count =
		geometry->configuration_cell_compact_cell_count;
	return 1;
}

void SG_RuneCompactGeometryDestroy(sg_rune_compact_geometry_t *geometry)
{
	sg_rune_compact_geometry_allocator_t allocator;

	if (geometry == NULL || geometry->state != GEOMETRY_STATE ||
		geometry->state_inverse != ~GEOMETRY_STATE || geometry->self != geometry)
		return;
	allocator = geometry->allocator;
	ReleaseGeometryArrays(geometry);
	geometry->state = 0U;
	geometry->state_inverse = 0U;
	geometry->self = NULL;
	allocator.release(allocator.context, geometry);
}

const char *SG_RuneCompactGeometryErrorString(
	sg_rune_compact_geometry_error_code_t code)
{
	switch (code)
	{
	case SG_RUNE_COMPACT_GEOMETRY_ERROR_NONE:
		return "none";
	case SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_ARGUMENT:
		return "invalid argument";
	case SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_CONFIGURATION:
		return "invalid configuration";
	case SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_WORLD:
		return "invalid world";
	case SG_RUNE_COMPACT_GEOMETRY_ERROR_IDENTITY_MISMATCH:
		return "identity mismatch";
	case SG_RUNE_COMPACT_GEOMETRY_ERROR_NONFINITE_GEOMETRY:
		return "nonfinite geometry";
	case SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_GEOMETRY:
		return "invalid geometry";
	case SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_REFERENCE:
		return "invalid reference";
	case SG_RUNE_COMPACT_GEOMETRY_ERROR_UNSUPPORTED_TOPOLOGY:
		return "unsupported topology";
	case SG_RUNE_COMPACT_GEOMETRY_ERROR_Q8_CONVERSION:
		return "Q8 conversion";
	case SG_RUNE_COMPACT_GEOMETRY_ERROR_OVERFLOW:
		return "overflow";
	case SG_RUNE_COMPACT_GEOMETRY_ERROR_OUT_OF_MEMORY:
		return "out of memory";
	case SG_RUNE_COMPACT_GEOMETRY_ERROR_CODE_COUNT:
		break;
	}
	return "unknown";
}
