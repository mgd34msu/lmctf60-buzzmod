#include "sg_rune_compact_response_partition.h"

#include "sg_rune_compact_builder_owner.h"
#include "sg_rune_compact_geometry_owner.h"
#include "sg_rune_compact_geometry_partition.h"
#include "sg_rune_compact_response_partition_owner.h"
#include "sg_rune_compact_source_surface_catalog.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define RESPONSE_STATE UINT32_C(0x5253504e)
#define RESPONSE_EPSILON 1.0e-7

typedef struct response_context_s
{
	sg_rune_compact_response_allocator_t allocator;
	sg_rune_compact_response_error_t *error;
} response_context_t;

typedef struct response_halfspace_vector_s
{
	sg_rune_compact_partition_halfspace_t *values;
	uint32_t count;
	uint32_t capacity;
} response_halfspace_vector_t;

typedef struct response_fragment_work_s
{
	response_halfspace_vector_t halfspaces;
	sg_rune_compact_partition_polyhedron_t polyhedron;
	sg_rune_compact_cell_index_t parent_cell;
	uint32_t static_partition;
	uint64_t static_partition_id;
	uint32_t configuration_region;
	uint32_t configuration_cell;
	uint32_t bsp_leaf;
	uint32_t bsp_area;
	uint32_t bsp_cluster;
	sg_rune_stance_validity_t valid_stances;
} response_fragment_work_t;

typedef struct response_fragment_vector_s
{
	response_fragment_work_t *values;
	uint32_t count;
	uint32_t capacity;
} response_fragment_vector_t;

typedef struct response_patch_work_s
{
	sg_rune_compact_response_patch_t patch;
	sg_rune_vec3_t *vertices;
	uint32_t query_surface;
} response_patch_work_t;

typedef struct response_patch_vector_s
{
	response_patch_work_t *values;
	uint32_t count;
	uint32_t capacity;
} response_patch_vector_t;

typedef struct response_split_vector_s
{
	sg_rune_compact_response_split_t *values;
	uint32_t count;
	uint32_t capacity;
} response_split_vector_t;

typedef struct response_occluder_vector_s
{
	sg_rune_compact_response_occluder_t *values;
	uint32_t count;
	uint32_t capacity;
} response_occluder_vector_t;

typedef struct response_occluder_side_vector_s
{
	sg_rune_compact_response_occluder_side_t *values;
	uint32_t count;
	uint32_t capacity;
} response_occluder_side_vector_t;

typedef struct response_occluder_edge_vector_s
{
	sg_rune_compact_response_occluder_edge_t *values;
	uint32_t count;
	uint32_t capacity;
} response_occluder_edge_vector_t;

typedef struct response_pair_vector_s
{
	sg_rune_compact_response_pair_t *values;
	uint32_t count;
	uint32_t capacity;
} response_pair_vector_t;

/* A response boundary is only materialized in the compact source cell whose
 * certified probe exposed it.  This keeps unrelated source/target products
 * out of the shared partition while retaining the exact boundary as a normal
 * published split reference. */
typedef struct response_refinement_s
{
	uint32_t source_fragment;
	uint32_t split;
} response_refinement_t;

typedef struct response_refinement_vector_s
{
	response_refinement_t *values;
	uint32_t count;
	uint32_t capacity;
} response_refinement_vector_t;

/* A map-only probe selects one compact source/target representative.  It
 * carries the collision owner's exact first-hit side, never an inferred plane
 * owner. */
typedef struct response_probe_s
{
	uint32_t source_fragment;
	uint32_t target_patch;
	uint32_t impact_occluder;
	uint32_t impact_side;
	uint32_t static_impact;
} response_probe_t;

typedef struct response_probe_vector_s
{
	response_probe_t *values;
	uint32_t count;
	uint32_t capacity;
} response_probe_vector_t;

typedef struct response_endpoint_index_s
{
	sg_rune_compact_response_endpoint_group_t *groups;
	uint32_t group_count;
	uint32_t group_capacity;
	uint32_t *members;
	uint32_t member_count;
	uint32_t member_capacity;
} response_endpoint_index_t;

typedef struct response_candidate_vector_s
{
	sg_rune_compact_response_candidate_group_t *values;
	uint32_t count;
	uint32_t capacity;
} response_candidate_vector_t;

static int PlaneCrossesPolyhedron(const sg_configuration_plane_t *plane,
	const sg_rune_compact_partition_polyhedron_t *polyhedron);
static int TracePlaneMatchesPublished(const sg_host_collision_plane_t *trace,
	const sg_rune_binary32_plane_t *published);

struct sg_rune_compact_response_partition_s
{
	uint32_t state;
	uint32_t state_inverse;
	const struct sg_rune_compact_response_partition_s *self;
	uint32_t reference_count;
	sg_rune_compact_response_allocator_t allocator;
	sg_rune_compact_identity_t identity;
	sg_rune_compact_response_fragment_t *source_fragments;
	uint32_t source_fragment_count;
	sg_rune_compact_response_halfspace_t *source_halfspaces;
	uint32_t source_halfspace_count;
	sg_rune_compact_response_patch_t *target_patches;
	uint32_t target_patch_count;
	sg_rune_q8_vec3_t *target_vertices;
	uint32_t target_vertex_count;
	sg_rune_compact_response_split_t *splits;
	uint32_t split_count;
	sg_rune_compact_response_pair_t *response_pairs;
	uint32_t response_pair_count;
	sg_rune_compact_response_candidate_group_t *candidate_groups;
	uint32_t candidate_group_count;
	sg_rune_compact_response_endpoint_group_t *source_endpoint_groups;
	uint32_t source_endpoint_group_count;
	uint32_t *source_endpoint_members;
	uint32_t source_endpoint_member_count;
	sg_rune_compact_response_endpoint_group_t *target_endpoint_groups;
	uint32_t target_endpoint_group_count;
	uint32_t *target_endpoint_members;
	uint32_t target_endpoint_member_count;
	uint32_t static_occluder_count;
	sg_rune_compact_response_occluder_t *static_occluders;
	sg_rune_compact_response_occluder_side_t *static_occluder_sides;
	uint32_t static_occluder_side_count;
	sg_rune_compact_response_occluder_edge_t *static_occluder_edges;
	uint32_t static_occluder_edge_count;
	uint32_t compact_facet_count;
	sg_rune_compact_facet_t *compact_facets;
	uint32_t compact_cell_count;
	sg_rune_compact_source_surface_t *compact_source_surfaces;
	uint32_t compact_source_surface_count;
	sg_rune_q8_vec3_t *compact_source_surface_vertices;
	uint32_t compact_source_surface_vertex_count;
	sg_rune_compact_response_pvs_offset_t *bsp_visibility_bit_offsets;
	uint32_t bsp_visibility_cluster_count;
	uint8_t *bsp_visibility_bytes;
	uint32_t bsp_visibility_byte_count;
	uint32_t *area_components;
	uint32_t area_component_count;
	sg_rune_compact_response_seal_t seal;
};

static int PlaneCrossesPatch(const sg_configuration_plane_t *plane,
	const response_patch_work_t *patch);
static int AllocateArray(response_context_t *context, uint32_t count,
	size_t element, void **result);
static int SpanWithin(uint32_t first, uint32_t count, uint32_t total);
static uint32_t EndpointGroupForMember(
	const sg_rune_compact_response_endpoint_group_t *groups,
	uint32_t group_count, const uint32_t *members, uint32_t member);
static int PublishedPatchCompare(
	const sg_rune_compact_response_partition_view_t *view,
	const sg_rune_compact_response_patch_t *left,
	const sg_rune_compact_response_patch_t *right);
static int Q8Bounds(const sg_rune_bounds_t *source,
	sg_rune_q8_bounds_t *result);

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

static int SelectAllocator(const sg_rune_compact_response_allocator_t *source,
	sg_rune_compact_response_allocator_t *result)
{
	if (source == NULL)
	{
		result->context = NULL;
		result->allocate = DefaultAllocate;
		result->release = DefaultRelease;
		return 1;
	}
	if (source->allocate == NULL || source->release == NULL)
		return 0;
	*result = *source;
	return 1;
}

static void ClearError(sg_rune_compact_response_error_t *error)
{
	if (error == NULL)
		return;
	error->code = SG_RUNE_COMPACT_RESPONSE_ERROR_NONE;
	error->domain = SG_RUNE_COMPACT_RESPONSE_RECORD_RESULT;
	error->record = SG_RUNE_COMPACT_RESPONSE_INDEX_NONE;
}

static void SetError(response_context_t *context,
	sg_rune_compact_response_error_code_t code,
	sg_rune_compact_response_record_domain_t domain, uint32_t record)
{
	if (context->error == NULL ||
		context->error->code != SG_RUNE_COMPACT_RESPONSE_ERROR_NONE)
		return;
	context->error->code = code;
	context->error->domain = domain;
	context->error->record = record;
}

static void *Allocate(response_context_t *context, size_t bytes)
{
	void *allocation;

	if (bytes == 0U)
		return NULL;
	allocation = context->allocator.allocate(context->allocator.context, bytes);
	if (allocation == NULL)
		SetError(context, SG_RUNE_COMPACT_RESPONSE_ERROR_OUT_OF_MEMORY,
			SG_RUNE_COMPACT_RESPONSE_RECORD_RESULT,
			SG_RUNE_COMPACT_RESPONSE_INDEX_NONE);
	return allocation;
}

static void Release(response_context_t *context, void *allocation)
{
	if (allocation != NULL)
		context->allocator.release(context->allocator.context, allocation);
}

static int BytesFor(uint32_t count, size_t element, size_t *bytes_out)
{
	if (element == 0U || (size_t)count > SIZE_MAX / element)
		return 0;
	*bytes_out = (size_t)count * element;
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
	return bits == UINT32_C(0x80000000) ? 0U : bits;
}

static float BitsFloat(uint32_t bits)
{
	float value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

static int Finite3(const float value[3])
{
	return value != NULL && isfinite(value[0]) && isfinite(value[1]) &&
		isfinite(value[2]);
}

static int ToQ8(float value, int32_t *result)
{
	double scaled;
	double rounded;

	if (!isfinite(value))
		return 0;
	scaled = (double)value * 8.0;
	if (scaled < (double)INT32_MIN || scaled > (double)INT32_MAX)
		return 0;
	rounded = nearbyint(scaled);
	if (rounded < (double)INT32_MIN || rounded > (double)INT32_MAX)
		return 0;
	*result = (int32_t)rounded;
	return 1;
}

static int PlaneFinite(const sg_configuration_plane_t *plane)
{
	double magnitude;

	if (plane == NULL || !Finite3(plane->normal) || !isfinite(plane->distance))
		return 0;
	magnitude = fabs((double)plane->normal[0]) +
		fabs((double)plane->normal[1]) + fabs((double)plane->normal[2]);
	return magnitude > DBL_MIN;
}

static int NormalizePlane(const sg_configuration_plane_t *source,
	sg_configuration_plane_t *result)
{
	double length;
	double normal[3];
	uint32_t axis;

	if (!PlaneFinite(source))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		normal[axis] = (double)source->normal[axis];
	length = sqrt(normal[0] * normal[0] + normal[1] * normal[1] +
		normal[2] * normal[2]);
	if (!isfinite(length) || !(length > DBL_MIN))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		result->normal[axis] = (float)(normal[axis] / length);
	result->distance = (float)((double)source->distance / length);
	result->source_kind = source->source_kind;
	result->source_index = source->source_index;
	result->source_variant = source->source_variant;
	return PlaneFinite(result);
}

static void CanonicalizePlane(sg_configuration_plane_t *plane)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		if (fabsf(plane->normal[axis]) > FLT_EPSILON)
		{
			if (plane->normal[axis] < 0.0f)
			{
				uint32_t flip;
				for (flip = 0U; flip < 3U; flip++)
					plane->normal[flip] = -plane->normal[flip];
				plane->distance = -plane->distance;
				plane->source_variant ^= 1U;
			}
			return;
		}
}

static void PlaneToCompact(const sg_configuration_plane_t *source,
	sg_rune_binary32_plane_t *result)
{
	uint32_t axis;

	assert(source != NULL);
	assert(result != NULL);
	for (axis = 0U; axis < 3U; axis++)
		result->normal_bits[axis] = FloatBits(source->normal[axis]);
	result->distance_bits = FloatBits(source->distance);
}

static int CompactPlaneCompare(const sg_rune_binary32_plane_t *left,
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

static void *PartitionAllocate(void *opaque, size_t bytes)
{
	return Allocate((response_context_t *)opaque, bytes);
}

static void PartitionRelease(void *opaque, void *allocation)
{
	Release((response_context_t *)opaque, allocation);
}

static void PartitionAllocator(response_context_t *context,
	sg_rune_compact_partition_allocator_t *allocator)
{
	allocator->context = context;
	allocator->allocate = PartitionAllocate;
	allocator->release = PartitionRelease;
}

static int Grow(response_context_t *context, void **values,
	uint32_t *capacity, uint32_t required, size_t element)
{
	uint32_t next;
	size_t bytes;
	void *grown;

	if (required <= *capacity)
		return 1;
	next = *capacity == 0U ? 8U : *capacity;
	while (next < required)
	{
		if (next > UINT32_MAX / 2U)
		{
			next = required;
			break;
		}
		next *= 2U;
	}
	if (!BytesFor(next, element, &bytes))
	{
		SetError(context, SG_RUNE_COMPACT_RESPONSE_ERROR_OVERFLOW,
			SG_RUNE_COMPACT_RESPONSE_RECORD_RESULT,
			SG_RUNE_COMPACT_RESPONSE_INDEX_NONE);
		return 0;
	}
	grown = Allocate(context, bytes);
	if (grown == NULL)
		return 0;
	memset(grown, 0, bytes);
	if (*values != NULL)
	{
		memcpy(grown, *values, (size_t)*capacity * element);
		Release(context, *values);
	}
	*values = grown;
	*capacity = next;
	return 1;
}

static void HalfspacesRelease(response_context_t *context,
	response_halfspace_vector_t *vector)
{
	Release(context, vector->values);
	memset(vector, 0, sizeof(*vector));
}

static int HalfspacePush(response_context_t *context,
	response_halfspace_vector_t *vector,
	const sg_rune_compact_partition_halfspace_t *value)
{
	uint32_t required;

	if (!AddU32(vector->count, 1U, &required) ||
		!Grow(context, (void **)&vector->values, &vector->capacity, required,
			sizeof(*vector->values)))
		return 0;
	vector->values[vector->count] = *value;
	vector->count = required;
	return 1;
}

static int HalfspacesCopy(response_context_t *context,
	const response_halfspace_vector_t *source,
	response_halfspace_vector_t *result)
{
	size_t bytes;

	memset(result, 0, sizeof(*result));
	if (source->count == 0U)
		return 1;
	if (!BytesFor(source->count, sizeof(*source->values), &bytes))
		return 0;
	result->values = Allocate(context, bytes);
	if (result->values == NULL)
		return 0;
	memcpy(result->values, source->values, bytes);
	result->count = source->count;
	result->capacity = source->count;
	return 1;
}

static void FragmentRelease(response_context_t *context,
	response_fragment_work_t *fragment)
{
	sg_rune_compact_partition_allocator_t allocator;

	PartitionAllocator(context, &allocator);
	HalfspacesRelease(context, &fragment->halfspaces);
	SG_RuneCompactPartitionPolyhedronDestroy(&fragment->polyhedron, &allocator);
	memset(fragment, 0, sizeof(*fragment));
}

static void FragmentsRelease(response_context_t *context,
	response_fragment_vector_t *vector)
{
	uint32_t index;

	for (index = 0U; index < vector->count; index++)
		FragmentRelease(context, &vector->values[index]);
	Release(context, vector->values);
	memset(vector, 0, sizeof(*vector));
}

static int FragmentPushOwned(response_context_t *context,
	response_fragment_vector_t *vector, response_fragment_work_t *value)
{
	uint32_t required;

	if (!AddU32(vector->count, 1U, &required) ||
		!Grow(context, (void **)&vector->values, &vector->capacity, required,
			sizeof(*vector->values)))
		return 0;
	vector->values[vector->count] = *value;
	memset(value, 0, sizeof(*value));
	vector->count = required;
	return 1;
}

static void PatchesRelease(response_context_t *context,
	response_patch_vector_t *vector)
{
	uint32_t index;

	for (index = 0U; index < vector->count; index++)
		Release(context, vector->values[index].vertices);
	Release(context, vector->values);
	memset(vector, 0, sizeof(*vector));
}

static int PatchPushOwned(response_context_t *context,
	response_patch_vector_t *vector, response_patch_work_t *value)
{
	uint32_t required;

	if (!AddU32(vector->count, 1U, &required) ||
		!Grow(context, (void **)&vector->values, &vector->capacity, required,
			sizeof(*vector->values)))
		return 0;
	vector->values[vector->count] = *value;
	memset(value, 0, sizeof(*value));
	vector->count = required;
	return 1;
}

static int SplitCompare(const sg_rune_compact_response_split_t *left,
	const sg_rune_compact_response_split_t *right)
{
	int plane = CompactPlaneCompare(&left->plane, &right->plane);

	if (plane != 0)
		return plane;
	if (left->kind != right->kind)
		return left->kind < right->kind ? -1 : 1;
	if (left->target_surface_id != right->target_surface_id)
		return left->target_surface_id < right->target_surface_id ? -1 : 1;
	if (left->occluder != right->occluder)
		return left->occluder < right->occluder ? -1 : 1;
	if (left->edge != right->edge)
		return left->edge < right->edge ? -1 : 1;
	if (left->brush_side != right->brush_side)
		return left->brush_side < right->brush_side ? -1 : 1;
	return 0;
}

static void SortSplits(response_split_vector_t *vector)
{
	uint32_t index;

	for (index = 1U; index < vector->count; index++)
	{
		sg_rune_compact_response_split_t value = vector->values[index];
		uint32_t cursor = index;

		while (cursor != 0U &&
			SplitCompare(&value, &vector->values[cursor - 1U]) < 0)
		{
			vector->values[cursor] = vector->values[cursor - 1U];
			cursor--;
		}
		vector->values[cursor] = value;
	}
}

static void DeduplicateSplits(response_split_vector_t *vector)
{
	uint32_t input;
	uint32_t output = 0U;

	for (input = 0U; input < vector->count; input++)
	{
		if (output != 0U &&
			SplitCompare(&vector->values[output - 1U],
				&vector->values[input]) == 0)
			continue;
		vector->values[output++] = vector->values[input];
	}
	vector->count = output;
}

static int FragmentWorkCompare(const response_fragment_work_t *left,
	const response_fragment_work_t *right)
{
	sg_rune_q8_bounds_t left_bounds;
	sg_rune_q8_bounds_t right_bounds;
	uint32_t halfspace;
	uint32_t axis;

#define RESPONSE_FRAGMENT_FIELD(a, b) \
	do { if ((a) != (b)) return (a) < (b) ? -1 : 1; } while (0)
	RESPONSE_FRAGMENT_FIELD(left->parent_cell.value, right->parent_cell.value);
	RESPONSE_FRAGMENT_FIELD(left->static_partition_id,
		right->static_partition_id);
	RESPONSE_FRAGMENT_FIELD(left->configuration_region,
		right->configuration_region);
	RESPONSE_FRAGMENT_FIELD(left->configuration_cell,
		right->configuration_cell);
	RESPONSE_FRAGMENT_FIELD(left->bsp_leaf, right->bsp_leaf);
	RESPONSE_FRAGMENT_FIELD(left->bsp_area, right->bsp_area);
	RESPONSE_FRAGMENT_FIELD(left->bsp_cluster, right->bsp_cluster);
	RESPONSE_FRAGMENT_FIELD(left->valid_stances, right->valid_stances);
	if (!Q8Bounds(&left->polyhedron.bounds, &left_bounds) ||
		!Q8Bounds(&right->polyhedron.bounds, &right_bounds))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
	{
		RESPONSE_FRAGMENT_FIELD(left_bounds.mins.value[axis],
			right_bounds.mins.value[axis]);
		RESPONSE_FRAGMENT_FIELD(left_bounds.maxs.value[axis],
			right_bounds.maxs.value[axis]);
	}
	RESPONSE_FRAGMENT_FIELD(left->halfspaces.count, right->halfspaces.count);
	for (halfspace = 0U; halfspace < left->halfspaces.count; halfspace++)
	{
		const sg_rune_compact_partition_halfspace_t *left_halfspace =
			&left->halfspaces.values[halfspace];
		const sg_rune_compact_partition_halfspace_t *right_halfspace =
			&right->halfspaces.values[halfspace];
		sg_rune_binary32_plane_t left_plane;
		sg_rune_binary32_plane_t right_plane;
		uint32_t left_split;
		uint32_t right_split;

		PlaneToCompact(&left_halfspace->plane, &left_plane);
		PlaneToCompact(&right_halfspace->plane, &right_plane);
		for (axis = 0U; axis < 3U; axis++)
			RESPONSE_FRAGMENT_FIELD(left_plane.normal_bits[axis],
				right_plane.normal_bits[axis]);
		RESPONSE_FRAGMENT_FIELD(left_plane.distance_bits,
			right_plane.distance_bits);
		left_split = (left_halfspace->contributor & UINT32_C(0x80000000)) != 0U ?
			left_halfspace->contributor & UINT32_C(0x7fffffff) :
			SG_RUNE_COMPACT_RESPONSE_INDEX_NONE;
		right_split =
			(right_halfspace->contributor & UINT32_C(0x80000000)) != 0U ?
			right_halfspace->contributor & UINT32_C(0x7fffffff) :
			SG_RUNE_COMPACT_RESPONSE_INDEX_NONE;
		RESPONSE_FRAGMENT_FIELD(left_split, right_split);
		RESPONSE_FRAGMENT_FIELD(left_halfspace->open, right_halfspace->open);
	}
#undef RESPONSE_FRAGMENT_FIELD
	return 0;
}

static void SortFragments(response_fragment_vector_t *fragments)
{
	uint32_t index;

	for (index = 1U; index < fragments->count; index++)
	{
		response_fragment_work_t value = fragments->values[index];
		uint32_t cursor = index;

		while (cursor != 0U &&
			FragmentWorkCompare(&value, &fragments->values[cursor - 1U]) < 0)
		{
			fragments->values[cursor] = fragments->values[cursor - 1U];
			cursor--;
		}
		fragments->values[cursor] = value;
	}
}

static int SplitPush(response_context_t *context,
	response_split_vector_t *vector,
	const sg_configuration_plane_t *input,
	sg_rune_compact_response_split_kind_t kind, uint64_t target_surface_id,
	uint32_t occluder, uint32_t edge, uint32_t brush_side)
{
	sg_configuration_plane_t plane;
	sg_rune_compact_response_split_t split;
	uint32_t required;

	if (!NormalizePlane(input, &plane))
		return 1;
	CanonicalizePlane(&plane);
	memset(&split, 0, sizeof(split));
	PlaneToCompact(&plane, &split.plane);
	split.kind = kind;
	split.target_surface_id = target_surface_id;
	split.occluder = occluder;
	split.edge = edge;
	split.brush_side = brush_side;
	if (!AddU32(vector->count, 1U, &required) ||
		!Grow(context, (void **)&vector->values, &vector->capacity, required,
			sizeof(*vector->values)))
		return 0;
	vector->values[vector->count] = split;
	vector->count = required;
	return 1;
}

static void SplitsRelease(response_context_t *context,
	response_split_vector_t *vector)
{
	Release(context, vector->values);
	memset(vector, 0, sizeof(*vector));
}

static int OccluderPush(response_context_t *context,
	response_occluder_vector_t *vector,
	const sg_rune_compact_response_occluder_t *value)
{
	uint32_t required;

	if (!AddU32(vector->count, 1U, &required) ||
		!Grow(context, (void **)&vector->values, &vector->capacity, required,
			sizeof(*vector->values)))
		return 0;
	vector->values[vector->count] = *value;
	vector->count = required;
	return 1;
}

static int OccluderSidePush(response_context_t *context,
	response_occluder_side_vector_t *vector,
	const sg_rune_compact_response_occluder_side_t *value)
{
	uint32_t required;

	if (!AddU32(vector->count, 1U, &required) ||
		!Grow(context, (void **)&vector->values, &vector->capacity, required,
			sizeof(*vector->values)))
		return 0;
	vector->values[vector->count] = *value;
	vector->count = required;
	return 1;
}

static int OccluderEdgePush(response_context_t *context,
	response_occluder_edge_vector_t *vector,
	const sg_rune_compact_response_occluder_edge_t *value)
{
	uint32_t required;

	if (!AddU32(vector->count, 1U, &required) ||
		!Grow(context, (void **)&vector->values, &vector->capacity, required,
			sizeof(*vector->values)))
		return 0;
	vector->values[vector->count] = *value;
	vector->count = required;
	return 1;
}

static void OccluderAuthorityRelease(response_context_t *context,
	response_occluder_vector_t *occluders,
	response_occluder_side_vector_t *sides,
	response_occluder_edge_vector_t *edges)
{
	Release(context, occluders->values);
	Release(context, sides->values);
	Release(context, edges->values);
	memset(occluders, 0, sizeof(*occluders));
	memset(sides, 0, sizeof(*sides));
	memset(edges, 0, sizeof(*edges));
}

static void PointToBinary32(const sg_rune_vec3_t *source,
	sg_rune_compact_response_binary32_point_t *result)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		result->value_bits[axis] = FloatBits(source->value[axis]);
}

static int Binary32PointCompare(
	const sg_rune_compact_response_binary32_point_t *left,
	const sg_rune_compact_response_binary32_point_t *right)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
	{
		if (left->value_bits[axis] < right->value_bits[axis])
			return -1;
		if (left->value_bits[axis] > right->value_bits[axis])
			return 1;
	}
	return 0;
}

static void PairsRelease(response_context_t *context,
	response_pair_vector_t *vector)
{
	Release(context, vector->values);
	memset(vector, 0, sizeof(*vector));
}

static int PairPush(response_context_t *context, response_pair_vector_t *vector,
	const sg_rune_compact_response_pair_t *value)
{
	uint32_t required;

	if (!AddU32(vector->count, 1U, &required) ||
		!Grow(context, (void **)&vector->values, &vector->capacity, required,
			sizeof(*vector->values)))
		return 0;
	vector->values[vector->count] = *value;
	vector->count = required;
	return 1;
}

static void RefinementsRelease(response_context_t *context,
	response_refinement_vector_t *vector)
{
	Release(context, vector->values);
	memset(vector, 0, sizeof(*vector));
}

static int RefinementPush(response_context_t *context,
	response_refinement_vector_t *vector, uint32_t source_fragment,
	uint32_t split)
{
	uint32_t index;

	for (index = 0U; index < vector->count; index++)
		if (vector->values[index].source_fragment == source_fragment &&
			vector->values[index].split == split)
			return 1;
	if (!Grow(context, (void **)&vector->values, &vector->capacity,
		vector->count + 1U, sizeof(*vector->values)))
		return 0;
	vector->values[vector->count].source_fragment = source_fragment;
	vector->values[vector->count].split = split;
	vector->count++;
	return 1;
}

static void SortRefinements(response_refinement_vector_t *vector)
{
	uint32_t index;

	for (index = 1U; index < vector->count; index++)
	{
		response_refinement_t value = vector->values[index];
		uint32_t cursor = index;

		while (cursor != 0U &&
			(value.source_fragment < vector->values[cursor - 1U].source_fragment ||
			 (value.source_fragment ==
				vector->values[cursor - 1U].source_fragment &&
			  value.split < vector->values[cursor - 1U].split)))
		{
			vector->values[cursor] = vector->values[cursor - 1U];
			cursor--;
		}
		vector->values[cursor] = value;
	}
}

static void ProbesRelease(response_context_t *context,
	response_probe_vector_t *vector)
{
	Release(context, vector->values);
	memset(vector, 0, sizeof(*vector));
}

static int ProbePush(response_context_t *context, response_probe_vector_t *vector,
	const response_probe_t *value)
{
	if (!Grow(context, (void **)&vector->values, &vector->capacity,
		vector->count + 1U, sizeof(*vector->values)))
		return 0;
	vector->values[vector->count++] = *value;
	return 1;
}

static void CompactPlaneToConfiguration(
	const sg_rune_binary32_plane_t *source,
	sg_configuration_plane_t *result)
{
	uint32_t axis;

	memset(result, 0, sizeof(*result));
	for (axis = 0U; axis < 3U; axis++)
		result->normal[axis] = BitsFloat(source->normal_bits[axis]);
	result->distance = BitsFloat(source->distance_bits);
}

static void FlipHalfspace(
	const sg_rune_compact_partition_halfspace_t *source,
	sg_rune_compact_partition_halfspace_t *result)
{
	uint32_t axis;

	*result = *source;
	for (axis = 0U; axis < 3U; axis++)
		result->plane.normal[axis] = -result->plane.normal[axis];
	result->plane.distance = -result->plane.distance;
	result->plane.source_variant ^= 1U;
	result->open = (uint8_t)(source->open ? 0U : 1U);
}

static int BoundsIntersect(const sg_rune_bounds_t *left,
	const sg_rune_bounds_t *right, sg_rune_bounds_t *result)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
	{
		result->mins.value[axis] = fmaxf(left->mins.value[axis],
			right->mins.value[axis]);
		result->maxs.value[axis] = fminf(left->maxs.value[axis],
			right->maxs.value[axis]);
		if (!(result->mins.value[axis] < result->maxs.value[axis]))
			return 0;
	}
	return 1;
}

static int AddGeometryCellHalfspaces(response_context_t *context,
	const sg_rune_compact_geometry_view_t *geometry, uint32_t cell_index,
	response_halfspace_vector_t *halfspaces)
{
	const sg_rune_compact_cell_t *cell = &geometry->cells[cell_index];
	uint32_t ordinal;

	if (cell->incidences.first > geometry->cell_incidence_count ||
		cell->incidences.count > geometry->cell_incidence_count -
			cell->incidences.first)
		return 0;
	for (ordinal = 0U; ordinal < cell->incidences.count; ordinal++)
	{
		const uint32_t map_index = cell->incidences.first + ordinal;
		const uint32_t incidence_index =
			geometry->cell_incidences[map_index].value;
		const sg_rune_compact_incidence_t *incidence;
		const sg_rune_compact_facet_t *facet;
		sg_rune_compact_partition_halfspace_t halfspace;

		if (incidence_index >= geometry->incidence_count)
			return 0;
		incidence = &geometry->incidences[incidence_index];
		if (incidence->cell.value != cell_index ||
			incidence->facet.value >= geometry->facet_count)
			return 0;
		facet = &geometry->facets[incidence->facet.value];
		memset(&halfspace, 0, sizeof(halfspace));
		CompactPlaneToConfiguration(&facet->plane, &halfspace.plane);
		halfspace.source_plane_index = incidence->facet.value;
		halfspace.contributor = cell_index;
		halfspace.open = incidence->boundary == SG_RUNE_BOUNDARY_OPEN ? 1U : 0U;
		if (incidence->side == SG_RUNE_FACET_POSITIVE_SIDE)
		{
			sg_rune_compact_partition_halfspace_t flipped;

			FlipHalfspace(&halfspace, &flipped);
			halfspace = flipped;
		}
		if (!HalfspacePush(context, halfspaces, &halfspace))
			return 0;
	}
	return 1;
}

static int AddSemanticRegionHalfspaces(response_context_t *context,
	const sg_configuration_semantics_t *semantics, uint32_t region_index,
	response_halfspace_vector_t *halfspaces)
{
	const sg_configuration_semantic_region_t *region =
		&semantics->regions[region_index];
	uint32_t ordinal;

	if (region->first_face > semantics->face_count ||
		region->face_count > semantics->face_count - region->first_face)
		return 0;
	for (ordinal = 0U; ordinal < region->face_count; ordinal++)
	{
		const sg_configuration_semantic_face_t *face =
			&semantics->faces[region->first_face + ordinal];
		sg_rune_compact_partition_halfspace_t halfspace;

		memset(&halfspace, 0, sizeof(halfspace));
		memcpy(halfspace.plane.normal, face->normal,
			sizeof(halfspace.plane.normal));
		halfspace.plane.distance = face->distance;
		halfspace.plane.source_index = face->source_index;
		halfspace.plane.source_kind = face->source_kind;
		halfspace.plane.source_variant = face->source_variant;
		halfspace.source_plane_index = region->first_face + ordinal;
		halfspace.contributor = region_index;
		halfspace.open = face->open;
		if (!PlaneFinite(&halfspace.plane) ||
			!HalfspacePush(context, halfspaces, &halfspace))
			return 0;
	}
	return 1;
}

static int BuildBaseFragments(response_context_t *context,
	const sg_rune_compact_builder_owner_view_t *owner,
	const sg_rune_compact_geometry_view_t *geometry,
	response_fragment_vector_t *fragments)
{
	const sg_configuration_semantics_t *semantics = owner->semantics;
	const sg_static_visibility_t *visibility = owner->visibility;
	sg_rune_compact_partition_allocator_t allocator;
	uint32_t region_index;

	PartitionAllocator(context, &allocator);
	memset(fragments, 0, sizeof(*fragments));
	if (semantics->region_count != visibility->partition_count ||
		(semantics->region_count != 0U &&
			(semantics->regions == NULL || visibility->partitions == NULL)))
		return 0;
	for (region_index = 0U; region_index < semantics->region_count;
		region_index++)
	{
		const sg_configuration_semantic_region_t *region =
			&semantics->regions[region_index];
		const sg_static_visibility_partition_t *partition =
			&visibility->partitions[region_index];
		const sg_rune_compact_geometry_cell_span_t *span;
		uint32_t member;

		if (region->cell >=
			geometry->compact_cells_for_configuration_cell_count ||
			partition->configuration_region != region_index ||
			partition->configuration_cell != region->cell)
			return 0;
		span = &geometry->compact_cells_for_configuration_cell[region->cell];
		if (span->first > geometry->configuration_cell_compact_cell_count ||
			span->count > geometry->configuration_cell_compact_cell_count -
				span->first)
			return 0;
		for (member = 0U; member < span->count; member++)
		{
			const uint32_t cell_index =
				geometry->configuration_cell_compact_cells[
					span->first + member].value;
			response_fragment_work_t fragment;
			sg_rune_bounds_t compact_bounds;
			sg_rune_bounds_t bounds;
			uint32_t axis;

			memset(&fragment, 0, sizeof(fragment));
			if (cell_index >= geometry->cell_count)
				return 0;
			for (axis = 0U; axis < 3U; axis++)
			{
				compact_bounds.mins.value[axis] =
					(float)geometry->cells[cell_index].bounds.mins.value[axis] /
					8.0f;
				compact_bounds.maxs.value[axis] =
					(float)geometry->cells[cell_index].bounds.maxs.value[axis] /
					8.0f;
			}
			if (!BoundsIntersect(&compact_bounds, &region->bounds, &bounds))
				continue;
			if (!AddGeometryCellHalfspaces(context, geometry, cell_index,
					&fragment.halfspaces) ||
				!AddSemanticRegionHalfspaces(context, semantics, region_index,
					&fragment.halfspaces) ||
				!SG_RuneCompactPartitionDeriveFaces(fragment.halfspaces.values,
					fragment.halfspaces.count, &bounds, &allocator,
					&fragment.polyhedron, NULL))
			{
				FragmentRelease(context, &fragment);
				return 0;
			}
			if (fragment.polyhedron.empty)
			{
				FragmentRelease(context, &fragment);
				continue;
			}
			fragment.parent_cell.value = cell_index;
			fragment.static_partition = region_index;
			fragment.static_partition_id = partition->id;
			fragment.configuration_region = partition->configuration_region;
			fragment.configuration_cell = partition->configuration_cell;
			fragment.bsp_leaf = partition->bsp_leaf;
			fragment.bsp_area = partition->bsp_area;
			fragment.bsp_cluster = partition->bsp_cluster;
			fragment.valid_stances = geometry->cells[cell_index].valid_stances;
			if (!FragmentPushOwned(context, fragments, &fragment))
			{
				FragmentRelease(context, &fragment);
				return 0;
			}
		}
	}
	return fragments->count != 0U;
}

static uint32_t FindParentFacet(
	const sg_rune_compact_geometry_view_t *geometry,
	const sg_static_visibility_surface_t *surface)
{
	uint32_t facet;

	for (facet = 0U; facet < geometry->facet_count; facet++)
	{
		const sg_rune_compact_source_t *source = &geometry->facets[facet].source;

		if (source->kind == SG_RUNE_COMPACT_SOURCE_EXPANDED_BRUSH_SIDE &&
			source->value.brush_side.model == surface->model &&
			source->value.brush_side.brush == surface->brush &&
			source->value.brush_side.brush_side == surface->brush_side)
			return facet;
	}
	return SG_RUNE_COMPACT_RESPONSE_INDEX_NONE;
}

static uint32_t FindSourceSurface(
	const sg_rune_compact_geometry_view_t *geometry,
	const sg_bsp_world_t *world,
	const sg_static_visibility_surface_t *surface,
	const sg_rune_binary32_plane_t *plane)
{
	const uint32_t bsp_plane =
		world->brush_sides[surface->brush_side].plane;
	const sg_rune_compact_source_surface_frame_t frame =
		surface->model == SG_HOST_COLLISION_MODEL_WORLD ?
			SG_RUNE_COMPACT_SOURCE_SURFACE_WORLD :
			SG_RUNE_COMPACT_SOURCE_SURFACE_MODEL_LOCAL;
	uint32_t index;

	for (index = 0U; index < geometry->source_surface_count; index++)
	{
		const sg_rune_compact_source_surface_t *candidate =
			&geometry->source_surfaces[index];

		if (candidate->source.model == surface->model &&
			candidate->source.brush == surface->brush &&
			candidate->source.brush_side == surface->brush_side &&
			candidate->source.plane == bsp_plane && candidate->frame == frame &&
			memcmp(&candidate->plane, plane, sizeof(*plane)) == 0)
			return index;
	}
	return SG_RUNE_COMPACT_RESPONSE_INDEX_NONE;
}

static int PatchFromPolygon(response_context_t *context,
	const sg_rune_compact_geometry_view_t *geometry,
	const sg_configuration_hook_surface_t *surface,
	const sg_static_visibility_surface_t *visibility_surface,
	uint32_t surface_index,
	const sg_rune_compact_partition_polygon_t *polygon,
	uint32_t leaf, const sg_bsp_world_t *world,
	response_patch_vector_t *patches)
{
	response_patch_work_t work;
	sg_configuration_plane_t plane;
	size_t bytes;
	uint32_t canonical_first = 0U;
	uint32_t vertex;
	uint32_t axis;

	if (polygon->vertex_count < 3U || polygon->vertices == NULL ||
		!BytesFor(polygon->vertex_count, sizeof(*work.vertices), &bytes))
		return 0;
	memset(&work, 0, sizeof(work));
	work.vertices = Allocate(context, bytes);
	if (work.vertices == NULL)
		return 0;
	for (vertex = 1U; vertex < polygon->vertex_count; vertex++)
	{
		int less = 0;

		for (axis = 0U; axis < 3U; axis++)
		{
			int32_t candidate;
			int32_t current;

			if (!ToQ8(polygon->vertices[vertex].value[axis], &candidate) ||
				!ToQ8(polygon->vertices[canonical_first].value[axis], &current))
			{
				Release(context, work.vertices);
				return 0;
			}
			if (candidate == current)
				continue;
			less = candidate < current;
			break;
		}
		if (less)
			canonical_first = vertex;
	}
	for (vertex = 0U; vertex < polygon->vertex_count; vertex++)
		work.vertices[vertex] = polygon->vertices[
			(canonical_first + vertex) % polygon->vertex_count];
	memset(&plane, 0, sizeof(plane));
	memcpy(plane.normal, surface->normal, sizeof(plane.normal));
	plane.distance = surface->distance;
	if (!NormalizePlane(&plane, &plane))
	{
		Release(context, work.vertices);
		return 0;
	}
	PlaneToCompact(&plane, &work.patch.plane);
	work.patch.visibility_surface_id = visibility_surface->id;
	work.query_surface = surface_index;
	work.patch.model = visibility_surface->model;
	work.patch.brush = visibility_surface->brush;
	work.patch.brush_side = visibility_surface->brush_side;
	work.patch.source_surface = FindSourceSurface(geometry, world,
		visibility_surface, &work.patch.plane);
	work.patch.source_frame = visibility_surface->model ==
		SG_HOST_COLLISION_MODEL_WORLD ?
		SG_RUNE_COMPACT_SOURCE_SURFACE_WORLD :
		SG_RUNE_COMPACT_SOURCE_SURFACE_MODEL_LOCAL;
	if (work.patch.source_surface == SG_RUNE_COMPACT_RESPONSE_INDEX_NONE)
	{
		Release(context, work.vertices);
		return 0;
	}
	work.patch.parent_facet.value = FindParentFacet(geometry,
		visibility_surface);
	work.patch.target_cell.value = SG_RUNE_COMPACT_RESPONSE_INDEX_NONE;
	work.patch.configuration_region = SG_RUNE_COMPACT_RESPONSE_INDEX_NONE;
	work.patch.configuration_cell = SG_RUNE_COMPACT_RESPONSE_INDEX_NONE;
	work.patch.vertex_count = polygon->vertex_count;
	work.patch.bsp_leaf = leaf;
	work.patch.bsp_area = SG_RUNE_COMPACT_RESPONSE_INDEX_NONE;
	work.patch.bsp_cluster = SG_RUNE_COMPACT_RESPONSE_INDEX_NONE;
	if (leaf != SG_RUNE_COMPACT_RESPONSE_INDEX_NONE)
	{
		if (leaf >= world->leaf_count || world->leaves[leaf].area >=
			world->area_count)
		{
			Release(context, work.vertices);
			return 0;
		}
		work.patch.bsp_area = world->leaves[leaf].area;
		if (world->leaves[leaf].cluster >= 0)
			work.patch.bsp_cluster =
				(uint32_t)world->leaves[leaf].cluster;
	}
	if ((surface->flags & SG_CONFIGURATION_HOOK_SURFACE_HOOKABLE) != 0U)
		work.patch.flags |= SG_RUNE_COMPACT_RESPONSE_PATCH_HOOKABLE;
	if ((surface->flags & SG_CONFIGURATION_HOOK_SURFACE_SKY) != 0U)
		work.patch.flags |= SG_RUNE_COMPACT_RESPONSE_PATCH_SKY;
	if ((surface->flags & SG_CONFIGURATION_HOOK_SURFACE_MOVING_MODEL) != 0U)
		work.patch.flags |= SG_RUNE_COMPACT_RESPONSE_PATCH_MOVING;
	for (axis = 0U; axis < 3U; axis++)
	{
		float minimum = INFINITY;
		float maximum = -INFINITY;

		for (vertex = 0U; vertex < polygon->vertex_count; vertex++)
		{
			const float value = polygon->vertices[vertex].value[axis];

			if (!isfinite(value))
			{
				Release(context, work.vertices);
				return 0;
			}
			minimum = fminf(minimum, value);
			maximum = fmaxf(maximum, value);
		}
		if (!ToQ8(minimum, &work.patch.bounds.mins.value[axis]) ||
			!ToQ8(maximum, &work.patch.bounds.maxs.value[axis]))
		{
			Release(context, work.vertices);
			return 0;
		}
	}
	if (!PatchPushOwned(context, patches, &work))
	{
		Release(context, work.vertices);
		return 0;
	}
	return 1;
}

static int PointInSemanticRegion(
	const sg_configuration_semantics_t *semantics, uint32_t region_index,
	const float point[3])
{
	const sg_configuration_semantic_region_t *region =
		&semantics->regions[region_index];
	uint32_t axis;
	uint32_t face;

	for (axis = 0U; axis < 3U; axis++)
		if (point[axis] < region->bounds.mins.value[axis] - 0.001f ||
			point[axis] > region->bounds.maxs.value[axis] + 0.001f)
			return 0;
	for (face = 0U; face < region->face_count; face++)
		if (!SG_ConfigurationSemanticFaceContainsPoint(
			&semantics->faces[region->first_face + face], point))
			return 0;
	return 1;
}

static int PointInCompactCell(
	const sg_rune_compact_geometry_view_t *geometry, uint32_t cell_index,
	const float point[3])
{
	const sg_rune_compact_cell_t *cell = &geometry->cells[cell_index];
	uint32_t ordinal;

	for (ordinal = 0U; ordinal < cell->incidences.count; ordinal++)
	{
		const uint32_t map_index = cell->incidences.first + ordinal;
		const uint32_t incidence_index =
			geometry->cell_incidences[map_index].value;
		const sg_rune_compact_incidence_t *incidence =
			&geometry->incidences[incidence_index];
		const sg_rune_compact_facet_t *facet =
			&geometry->facets[incidence->facet.value];
		double residual = -(double)BitsFloat(facet->plane.distance_bits);
		uint32_t axis;

		for (axis = 0U; axis < 3U; axis++)
			residual += (double)BitsFloat(facet->plane.normal_bits[axis]) *
				(double)point[axis];
		if (incidence->side == SG_RUNE_FACET_POSITIVE_SIDE)
			residual = -residual;
		if (residual > 0.001)
			return 0;
	}
	return 1;
}

static int BindTargetPatches(
	const sg_rune_compact_builder_owner_view_t *owner,
	const sg_rune_compact_geometry_view_t *geometry,
	response_patch_vector_t *patches)
{
	uint32_t patch_index;

	for (patch_index = 0U; patch_index < patches->count; patch_index++)
	{
		response_patch_work_t *work = &patches->values[patch_index];
		sg_rune_compact_response_patch_t *patch = &work->patch;
		float target[3] = { 0.0f, 0.0f, 0.0f };
		float normal[3];
		uint32_t vertex;
		uint32_t axis;
		uint32_t region;

		if (patch->parent_facet.value !=
			SG_RUNE_COMPACT_RESPONSE_INDEX_NONE)
			patch->boundary_incidences =
				geometry->facets[patch->parent_facet.value].incidences;
		for (vertex = 0U; vertex < patch->vertex_count; vertex++)
			for (axis = 0U; axis < 3U; axis++)
				target[axis] += work->vertices[vertex].value[axis] /
					(float)patch->vertex_count;
		for (axis = 0U; axis < 3U; axis++)
		{
			normal[axis] = BitsFloat(patch->plane.normal_bits[axis]);
			target[axis] += normal[axis] * 0.25f;
		}
		for (region = 0U; region < owner->semantics->region_count; region++)
		{
			const sg_configuration_semantic_region_t *semantic =
				&owner->semantics->regions[region];
			const sg_rune_compact_geometry_cell_span_t *span;
			uint32_t member;

			if (!PointInSemanticRegion(owner->semantics, region, target) ||
				semantic->cell >=
					geometry->compact_cells_for_configuration_cell_count)
				continue;
			span = &geometry->compact_cells_for_configuration_cell[
				semantic->cell];
			for (member = 0U; member < span->count; member++)
			{
				const uint32_t cell =
					geometry->configuration_cell_compact_cells[
						span->first + member].value;

				if (cell >= geometry->cell_count ||
					!PointInCompactCell(geometry, cell, target))
					continue;
				patch->target_cell.value = cell;
				patch->static_partition_id =
					owner->visibility->partitions[region].id;
				patch->configuration_region = region;
				patch->configuration_cell = semantic->cell;
				patch->valid_stances = geometry->cells[cell].valid_stances;
				break;
			}
			if (patch->target_cell.value !=
				SG_RUNE_COMPACT_RESPONSE_INDEX_NONE)
				break;
		}
	}
	return 1;
}

static int ClipSurfaceNode(response_context_t *context,
	const sg_bsp_world_t *world,
	const sg_rune_compact_geometry_view_t *geometry,
	const sg_configuration_hook_surface_t *surface,
	const sg_static_visibility_surface_t *visibility_surface,
	uint32_t surface_index, int32_t node_index,
	const sg_rune_compact_partition_polygon_t *polygon, uint32_t depth,
	response_patch_vector_t *patches)
{
	sg_rune_compact_partition_allocator_t allocator;
	const sg_bsp_node_t *node;
	const sg_bsp_plane_t *bsp_plane;
	sg_rune_compact_partition_halfspace_t back_halfspace;
	sg_rune_compact_partition_halfspace_t front_halfspace;
	sg_rune_compact_partition_polygon_t front;
	sg_rune_compact_partition_polygon_t back;
	double minimum = INFINITY;
	double maximum = -INFINITY;
	double normal_alignment = 0.0;
	uint32_t vertex;
	uint32_t axis;
	int result = 0;

	if (node_index < 0)
	{
		const uint32_t leaf = (uint32_t)(-1 - node_index);

		if (leaf >= world->leaf_count)
			return 0;
		if (((uint32_t)world->leaves[leaf].contents &
			SG_HOST_CONTENTS_SOLID) != 0U)
			return 1;
		return PatchFromPolygon(context, geometry, surface,
			visibility_surface, surface_index, polygon, leaf, world, patches);
	}
	if ((uint32_t)node_index >= world->node_count ||
		depth > world->node_count)
		return 0;
	node = &world->nodes[(uint32_t)node_index];
	if (node->plane >= world->plane_count)
		return 0;
	bsp_plane = &world->planes[node->plane];
	for (vertex = 0U; vertex < polygon->vertex_count; vertex++)
	{
		double distance = -(double)bsp_plane->distance;

		for (axis = 0U; axis < 3U; axis++)
			distance += (double)bsp_plane->normal.value[axis] *
				(double)polygon->vertices[vertex].value[axis];
		minimum = fmin(minimum, distance);
		maximum = fmax(maximum, distance);
	}
	for (axis = 0U; axis < 3U; axis++)
		normal_alignment += (double)bsp_plane->normal.value[axis] *
			(double)surface->normal[axis];
	if (fabs(minimum) <= RESPONSE_EPSILON &&
		fabs(maximum) <= RESPONSE_EPSILON)
		return ClipSurfaceNode(context, world, geometry, surface,
			visibility_surface, surface_index,
			normal_alignment >= 0.0 ? node->children[0] : node->children[1],
			polygon, depth + 1U, patches);
	if (minimum >= -RESPONSE_EPSILON)
		return ClipSurfaceNode(context, world, geometry, surface,
			visibility_surface, surface_index, node->children[0], polygon,
			depth + 1U, patches);
	if (maximum <= RESPONSE_EPSILON)
		return ClipSurfaceNode(context, world, geometry, surface,
			visibility_surface, surface_index, node->children[1], polygon,
			depth + 1U, patches);
	memset(&back_halfspace, 0, sizeof(back_halfspace));
	memcpy(back_halfspace.plane.normal, bsp_plane->normal.value,
		sizeof(back_halfspace.plane.normal));
	back_halfspace.plane.distance = bsp_plane->distance;
	back_halfspace.plane.source_index = node->plane;
	memset(&front_halfspace, 0, sizeof(front_halfspace));
	FlipHalfspace(&back_halfspace, &front_halfspace);
	front_halfspace.open = 1U;
	memset(&front, 0, sizeof(front));
	memset(&back, 0, sizeof(back));
	PartitionAllocator(context, &allocator);
	if (!SG_RuneCompactPartitionClipPolygon(polygon, &front_halfspace,
		&allocator, &front, NULL) ||
		!SG_RuneCompactPartitionClipPolygon(polygon, &back_halfspace,
			&allocator, &back, NULL))
		goto done;
	if (front.vertex_count >= 3U &&
		!ClipSurfaceNode(context, world, geometry, surface,
			visibility_surface, surface_index, node->children[0], &front,
			depth + 1U, patches))
		goto done;
	if (back.vertex_count >= 3U &&
		!ClipSurfaceNode(context, world, geometry, surface,
			visibility_surface, surface_index, node->children[1], &back,
			depth + 1U, patches))
		goto done;
	result = 1;

done:
	SG_RuneCompactPartitionPolygonDestroy(&front, &allocator);
	SG_RuneCompactPartitionPolygonDestroy(&back, &allocator);
	return result;
}

static int BuildTargetPatches(response_context_t *context,
	const sg_rune_compact_builder_owner_view_t *owner,
	const sg_rune_compact_geometry_view_t *geometry,
	response_patch_vector_t *patches)
{
	const sg_configuration_semantics_t *semantics = owner->semantics;
	const sg_static_visibility_t *visibility = owner->visibility;
	const sg_bsp_world_t *world = owner->world;
	uint32_t surface_index;

	memset(patches, 0, sizeof(*patches));
	if (semantics->hook_surface_count != visibility->surface_count ||
		(semantics->hook_surface_count != 0U &&
			(semantics->hook_surfaces == NULL ||
			semantics->hook_vertices == NULL || visibility->surfaces == NULL)))
		return 0;
	for (surface_index = 0U; surface_index < semantics->hook_surface_count;
		surface_index++)
	{
		const sg_configuration_hook_surface_t *surface =
			&semantics->hook_surfaces[surface_index];
		const sg_static_visibility_surface_t *published =
			&visibility->surfaces[surface_index];
		sg_rune_compact_partition_polygon_t polygon;

		if (surface->first_vertex > semantics->hook_vertex_count ||
			surface->vertex_count > semantics->hook_vertex_count -
				surface->first_vertex || surface->vertex_count < 3U ||
			published->semantic_surface != surface_index ||
			published->model != surface->model ||
			published->brush != surface->brush ||
			published->brush_side != surface->brush_side ||
			published->flags != surface->flags)
			return 0;
		memset(&polygon, 0, sizeof(polygon));
		memcpy(polygon.plane.normal, surface->normal,
			sizeof(polygon.plane.normal));
		polygon.plane.distance = surface->distance;
		polygon.contributor = surface_index;
		polygon.vertices = &semantics->hook_vertices[surface->first_vertex];
		polygon.vertex_count = surface->vertex_count;
		if (surface->model != SG_HOST_COLLISION_MODEL_WORLD ||
			world->models == NULL || world->model_count == 0U)
		{
			if (!PatchFromPolygon(context, geometry, surface, published,
				surface_index, &polygon,
				SG_RUNE_COMPACT_RESPONSE_INDEX_NONE, world, patches))
				return 0;
		}
		else if (!ClipSurfaceNode(context, world, geometry, surface, published,
			surface_index, world->models[0].headnode, &polygon, 0U, patches))
			return 0;
	}
	return patches->count != 0U;
}

static int PatchCompare(const response_patch_work_t *left,
	const response_patch_work_t *right)
{
	uint32_t vertex;
	uint32_t axis;

#define RESPONSE_PATCH_COMPARE(a, b) \
	do { if ((a) != (b)) return (a) < (b) ? -1 : 1; } while (0)
	RESPONSE_PATCH_COMPARE(left->patch.model, right->patch.model);
	RESPONSE_PATCH_COMPARE(left->patch.brush, right->patch.brush);
	RESPONSE_PATCH_COMPARE(left->patch.brush_side, right->patch.brush_side);
	RESPONSE_PATCH_COMPARE(left->patch.source_surface,
		right->patch.source_surface);
	RESPONSE_PATCH_COMPARE(left->patch.source_frame, right->patch.source_frame);
	RESPONSE_PATCH_COMPARE(left->patch.bsp_leaf, right->patch.bsp_leaf);
	RESPONSE_PATCH_COMPARE(left->patch.visibility_surface_id,
		right->patch.visibility_surface_id);
	RESPONSE_PATCH_COMPARE(left->patch.vertex_count, right->patch.vertex_count);
	for (vertex = 0U; vertex < left->patch.vertex_count; vertex++)
		for (axis = 0U; axis < 3U; axis++)
		{
			int32_t left_value;
			int32_t right_value;

			if (!ToQ8(left->vertices[vertex].value[axis], &left_value) ||
				!ToQ8(right->vertices[vertex].value[axis], &right_value))
				return 0;
			if (left_value != right_value)
				return left_value < right_value ? -1 : 1;
		}
#undef RESPONSE_PATCH_COMPARE
	return 0;
}

static void SortPatches(response_patch_vector_t *patches)
{
	uint32_t index;

	for (index = 1U; index < patches->count; index++)
	{
		response_patch_work_t value = patches->values[index];
		uint32_t cursor = index;

		while (cursor != 0U &&
			PatchCompare(&value, &patches->values[cursor - 1U]) < 0)
		{
			patches->values[cursor] = patches->values[cursor - 1U];
			cursor--;
		}
		patches->values[cursor] = value;
	}
}

static void EdgePlane(const sg_rune_vec3_t *from, const sg_rune_vec3_t *to,
	const float surface_normal[3], sg_configuration_plane_t *plane)
{
	double edge[3];
	double normal[3];
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		edge[axis] = (double)to->value[axis] - (double)from->value[axis];
	normal[0] = edge[1] * (double)surface_normal[2] -
		edge[2] * (double)surface_normal[1];
	normal[1] = edge[2] * (double)surface_normal[0] -
		edge[0] * (double)surface_normal[2];
	normal[2] = edge[0] * (double)surface_normal[1] -
		edge[1] * (double)surface_normal[0];
	memset(plane, 0, sizeof(*plane));
	for (axis = 0U; axis < 3U; axis++)
		plane->normal[axis] = (float)normal[axis];
	plane->distance = (float)(normal[0] * (double)from->value[0] +
		normal[1] * (double)from->value[1] +
		normal[2] * (double)from->value[2]);
}

static int AddTargetEdgeSplitsForPatch(response_context_t *context,
	const response_patch_work_t *patch, response_split_vector_t *splits)
{
	float normal[3];
	uint32_t edge;

	normal[0] = BitsFloat(patch->patch.plane.normal_bits[0]);
	normal[1] = BitsFloat(patch->patch.plane.normal_bits[1]);
	normal[2] = BitsFloat(patch->patch.plane.normal_bits[2]);
	for (edge = 0U; edge < patch->patch.vertex_count; edge++)
	{
		sg_configuration_plane_t plane;

		EdgePlane(&patch->vertices[edge],
			&patch->vertices[(edge + 1U) % patch->patch.vertex_count],
			normal, &plane);
		if (!SplitPush(context, splits,
			&plane, SG_RUNE_COMPACT_RESPONSE_SPLIT_TARGET_EDGE,
			patch->patch.visibility_surface_id,
			SG_RUNE_COMPACT_RESPONSE_INDEX_NONE, edge,
			SG_HOST_COLLISION_BRUSH_NONE))
			return 0;
	}
	return 1;
}

static int AddOccluderSplits(response_context_t *context,
	const sg_rune_compact_builder_owner_view_t *owner,
	response_occluder_vector_t *published_occluders,
	response_occluder_side_vector_t *published_sides,
	response_occluder_edge_vector_t *published_edges)
{
	const sg_bsp_world_t *world = owner->world;
	const sg_static_visibility_t *visibility = owner->visibility;
	sg_rune_compact_partition_allocator_t allocator;
	uint32_t occluder_index;

	PartitionAllocator(context, &allocator);
	for (occluder_index = 0U; occluder_index < visibility->occluder_count;
		occluder_index++)
	{
		const sg_static_visibility_occluder_t *occluder =
			&visibility->occluders[occluder_index];
		const sg_bsp_brush_t *brush;
		response_halfspace_vector_t halfspaces;
		sg_rune_compact_partition_polyhedron_t polyhedron;
		sg_rune_bounds_t bounds;
		sg_rune_compact_response_occluder_t published;
		uint32_t side_offset;
		uint32_t face_index;

		if (occluder->brush >= world->brush_count ||
			occluder->model >= world->model_count)
			return 0;
		brush = &world->brushes[occluder->brush];
		if (brush->first_side > world->brush_side_count ||
			brush->side_count > world->brush_side_count - brush->first_side)
			return 0;
		memset(&halfspaces, 0, sizeof(halfspaces));
		memset(&polyhedron, 0, sizeof(polyhedron));
		memset(&published, 0, sizeof(published));
		published.model = occluder->model;
		published.brush = occluder->brush;
		published.contents = occluder->contents;
		published.conditional = occluder->conditional;
		published.first_side = published_sides->count;
		published.first_edge = published_edges->count;
		for (side_offset = 0U; side_offset < brush->side_count; side_offset++)
		{
			const uint32_t brush_side = brush->first_side + side_offset;
			const sg_bsp_brush_side_t *side = &world->brush_sides[brush_side];
			const sg_bsp_plane_t *plane;
			sg_rune_compact_partition_halfspace_t halfspace;
			sg_rune_compact_response_occluder_side_t published_side;
			sg_configuration_plane_t canonical;

			if (side->plane >= world->plane_count)
			{
				HalfspacesRelease(context, &halfspaces);
				return 0;
			}
			plane = &world->planes[side->plane];
			memset(&halfspace, 0, sizeof(halfspace));
			memcpy(halfspace.plane.normal, plane->normal.value,
				sizeof(halfspace.plane.normal));
			halfspace.plane.distance = plane->distance;
			halfspace.plane.source_index = side->plane;
			halfspace.source_plane_index = brush_side;
			halfspace.contributor = occluder_index;
			memset(&published_side, 0, sizeof(published_side));
			published_side.occluder = occluder_index;
			published_side.model = occluder->model;
			published_side.brush = occluder->brush;
			published_side.contents = occluder->contents;
			published_side.conditional = occluder->conditional;
			published_side.brush_side = brush_side;
			published_side.bsp_plane = side->plane;
			if (!NormalizePlane(&halfspace.plane, &canonical))
			{
				HalfspacesRelease(context, &halfspaces);
				return 0;
			}
			PlaneToCompact(&canonical, &published_side.halfspace_plane);
			CanonicalizePlane(&canonical);
			PlaneToCompact(&canonical, &published_side.plane);
			if (!OccluderSidePush(context, published_sides, &published_side) ||
				!HalfspacePush(context, &halfspaces, &halfspace))
			{
				HalfspacesRelease(context, &halfspaces);
				return 0;
			}
		}
		memcpy(bounds.mins.value, world->models[occluder->model].mins.value,
			sizeof(bounds.mins.value));
		memcpy(bounds.maxs.value, world->models[occluder->model].maxs.value,
			sizeof(bounds.maxs.value));
		if (!SG_RuneCompactPartitionDeriveFaces(halfspaces.values,
			halfspaces.count, &bounds, &allocator, &polyhedron, NULL))
		{
			HalfspacesRelease(context, &halfspaces);
			return 0;
		}
		for (face_index = 0U; face_index < polyhedron.face_count; face_index++)
		{
			const sg_rune_compact_partition_polygon_t *face =
				&polyhedron.faces[face_index];
			uint32_t published_side_index;
			uint32_t edge;

			if (face->source_plane_index < brush->first_side ||
				face->source_plane_index >= brush->first_side + brush->side_count)
			{
				SG_RuneCompactPartitionPolyhedronDestroy(&polyhedron, &allocator);
				HalfspacesRelease(context, &halfspaces);
				return 0;
			}
			published_side_index = published.first_side +
				(face->source_plane_index - brush->first_side);

			for (edge = 0U; edge < face->vertex_count; edge++)
			{
				sg_rune_compact_response_occluder_edge_t published_edge;

				memset(&published_edge, 0, sizeof(published_edge));
				published_edge.occluder = occluder_index;
				published_edge.side = published_side_index;
				published_edge.ordinal = edge;
				PointToBinary32(&face->vertices[edge], &published_edge.from);
				PointToBinary32(
					&face->vertices[(edge + 1U) % face->vertex_count],
					&published_edge.to);
				if (Binary32PointCompare(&published_edge.from,
					&published_edge.to) > 0)
				{
					const sg_rune_compact_response_binary32_point_t temporary =
						published_edge.from;

					published_edge.from = published_edge.to;
					published_edge.to = temporary;
				}
				if (!OccluderEdgePush(context, published_edges, &published_edge))
				{
					SG_RuneCompactPartitionPolyhedronDestroy(&polyhedron,
						&allocator);
					HalfspacesRelease(context, &halfspaces);
					return 0;
				}
			}
		}
		SG_RuneCompactPartitionPolyhedronDestroy(&polyhedron, &allocator);
		HalfspacesRelease(context, &halfspaces);
		published.side_count = published_sides->count - published.first_side;
		published.edge_count = published_edges->count - published.first_edge;
		if (!OccluderPush(context, published_occluders, &published))
			return 0;
	}
	return published_occluders->count == visibility->occluder_count;
}

static int PvsAllows(const sg_bsp_world_t *world, uint32_t source_cluster,
	uint32_t target_cluster)
{
	uint32_t input;
	uint32_t output = 0U;
	uint32_t target_byte;
	uint8_t value = 0U;

	if (source_cluster == SG_RUNE_COMPACT_RESPONSE_INDEX_NONE ||
		target_cluster == SG_RUNE_COMPACT_RESPONSE_INDEX_NONE ||
		world->visibility.byte_count == 0U)
		return 1;
	if (source_cluster >= world->visibility.cluster_count ||
		target_cluster >= world->visibility.cluster_count ||
		world->visibility.bit_offsets == NULL ||
		world->visibility.bytes == NULL)
		return 0;
	target_byte = target_cluster >> 3;
	input = world->visibility.bit_offsets[source_cluster][0];
	while (output <= target_byte)
	{
		uint8_t run;

		if (input >= world->visibility.byte_count)
			return 0;
		value = world->visibility.bytes[input++];
		if (value != 0U)
		{
			if (output++ == target_byte)
				break;
			continue;
		}
		if (input >= world->visibility.byte_count)
			return 0;
		run = world->visibility.bytes[input++];
		if (run == 0U || output > UINT32_MAX - (uint32_t)run)
			return 0;
		if (target_byte < output + (uint32_t)run)
			return 0;
		output += (uint32_t)run;
	}
	return (value & (uint8_t)(UINT32_C(1) <<
		(target_cluster & UINT32_C(7)))) != 0U;
}

static int SplitApplies(const sg_bsp_world_t *world,
	const response_fragment_work_t *fragment,
	const response_patch_vector_t *patches,
	const sg_rune_compact_response_split_t *split)
{
	uint32_t patch;

	if (split->target_surface_id == UINT64_MAX)
		return 1;
	for (patch = 0U; patch < patches->count; patch++)
		if (patches->values[patch].patch.visibility_surface_id ==
			split->target_surface_id &&
			PvsAllows(world, fragment->bsp_cluster,
				patches->values[patch].patch.bsp_cluster))
			return 1;
	return 0;
}

static int SplitRefinesCells(const sg_rune_compact_response_split_t *split)
{
	/* Full-cell refinement is only meaningful after a map-only probe selected
	 * this boundary.  The initial pass has no such boundaries; this predicate
	 * keeps the generic splitter confined to authenticated occluder planes. */
	return split->kind == SG_RUNE_COMPACT_RESPONSE_SPLIT_OCCLUDER_PLANE;
}

static int PlaneCrossesBounds(const sg_configuration_plane_t *plane,
	const sg_rune_bounds_t *bounds)
{
	double minimum = -(double)plane->distance;
	double maximum = -(double)plane->distance;
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
	{
		const double normal = (double)plane->normal[axis];
		const double low = (double)bounds->mins.value[axis];
		const double high = (double)bounds->maxs.value[axis];

		if (normal >= 0.0)
		{
			minimum += normal * low;
			maximum += normal * high;
		}
		else
		{
			minimum += normal * high;
			maximum += normal * low;
		}
	}
	return minimum < -RESPONSE_EPSILON && maximum > RESPONSE_EPSILON;
}

static int BuildChildFragment(response_context_t *context,
	const response_fragment_work_t *parent,
	const sg_rune_compact_partition_halfspace_t *split,
	response_fragment_work_t *child)
{
	sg_rune_compact_partition_allocator_t allocator;

	memset(child, 0, sizeof(*child));
	child->parent_cell = parent->parent_cell;
	child->static_partition = parent->static_partition;
	child->static_partition_id = parent->static_partition_id;
	child->configuration_region = parent->configuration_region;
	child->configuration_cell = parent->configuration_cell;
	child->bsp_leaf = parent->bsp_leaf;
	child->bsp_area = parent->bsp_area;
	child->bsp_cluster = parent->bsp_cluster;
	child->valid_stances = parent->valid_stances;
	if (!HalfspacesCopy(context, &parent->halfspaces, &child->halfspaces) ||
		!HalfspacePush(context, &child->halfspaces, split))
	{
		FragmentRelease(context, child);
		return 0;
	}
	PartitionAllocator(context, &allocator);
	if (!SG_RuneCompactPartitionDeriveFaces(child->halfspaces.values,
		child->halfspaces.count, &parent->polyhedron.bounds, &allocator,
		&child->polyhedron, NULL))
	{
		FragmentRelease(context, child);
		return 0;
	}
	return 1;
}

static int ApplyOneSplit(response_context_t *context,
	const sg_bsp_world_t *world, const response_patch_vector_t *patches,
	const sg_rune_compact_response_split_t *split, uint32_t split_index,
	response_fragment_vector_t *fragments)
{
	response_fragment_vector_t next;
	sg_configuration_plane_t plane;
	uint32_t index;

	memset(&next, 0, sizeof(next));
	CompactPlaneToConfiguration(&split->plane, &plane);
	for (index = 0U; index < fragments->count; index++)
	{
		response_fragment_work_t *fragment = &fragments->values[index];

		if (!SplitApplies(world, fragment, patches, split) ||
			!PlaneCrossesBounds(&plane, &fragment->polyhedron.bounds))
		{
			response_fragment_work_t moved = *fragment;

			memset(fragment, 0, sizeof(*fragment));
			if (!FragmentPushOwned(context, &next, &moved))
			{
				FragmentRelease(context, &moved);
				goto failure;
			}
			continue;
		}
		{
			sg_rune_compact_partition_halfspace_t negative;
			sg_rune_compact_partition_halfspace_t positive;
			response_fragment_work_t negative_child;
			response_fragment_work_t positive_child;

			memset(&negative, 0, sizeof(negative));
			memset(&negative_child, 0, sizeof(negative_child));
			memset(&positive_child, 0, sizeof(positive_child));
			negative.plane = plane;
			negative.source_plane_index = split_index;
			negative.contributor = UINT32_C(0x80000000) | split_index;
			negative.open = 0U;
			FlipHalfspace(&negative, &positive);
			positive.open = 1U;
			if (!BuildChildFragment(context, fragment, &negative,
					&negative_child) ||
				!BuildChildFragment(context, fragment, &positive,
					&positive_child))
			{
				FragmentRelease(context, &negative_child);
				FragmentRelease(context, &positive_child);
				goto failure;
			}
			if (!negative_child.polyhedron.empty &&
				!FragmentPushOwned(context, &next, &negative_child))
			{
				FragmentRelease(context, &negative_child);
				FragmentRelease(context, &positive_child);
				goto failure;
			}
			FragmentRelease(context, &negative_child);
			if (!positive_child.polyhedron.empty &&
				!FragmentPushOwned(context, &next, &positive_child))
			{
				FragmentRelease(context, &positive_child);
				goto failure;
			}
			FragmentRelease(context, &positive_child);
		}
	}
	FragmentsRelease(context, fragments);
	*fragments = next;
	return 1;

failure:
	FragmentsRelease(context, &next);
	return 0;
}

static int ApplyLocalSplit(response_context_t *context,
	const sg_rune_compact_response_split_t *split, uint32_t split_index,
	response_fragment_vector_t *fragments)
{
	response_fragment_vector_t next;
	sg_configuration_plane_t plane;
	uint32_t index;

	memset(&next, 0, sizeof(next));
	CompactPlaneToConfiguration(&split->plane, &plane);
	for (index = 0U; index < fragments->count; index++)
	{
		response_fragment_work_t *fragment = &fragments->values[index];

		if (!PlaneCrossesPolyhedron(&plane, &fragment->polyhedron))
		{
			response_fragment_work_t moved = *fragment;

			memset(fragment, 0, sizeof(*fragment));
			if (!FragmentPushOwned(context, &next, &moved))
			{
				FragmentRelease(context, &moved);
				goto failure;
			}
			continue;
		}
		{
			sg_rune_compact_partition_halfspace_t negative;
			sg_rune_compact_partition_halfspace_t positive;
			response_fragment_work_t negative_child;
			response_fragment_work_t positive_child;

			memset(&negative, 0, sizeof(negative));
			memset(&negative_child, 0, sizeof(negative_child));
			memset(&positive_child, 0, sizeof(positive_child));
			negative.plane = plane;
			negative.source_plane_index = split_index;
			negative.contributor = UINT32_C(0x80000000) | split_index;
			negative.open = 0U;
			FlipHalfspace(&negative, &positive);
			positive.open = 1U;
			if (!BuildChildFragment(context, fragment, &negative,
					&negative_child) ||
				!BuildChildFragment(context, fragment, &positive,
					&positive_child))
			{
				FragmentRelease(context, &negative_child);
				FragmentRelease(context, &positive_child);
				goto failure;
			}
			if (!negative_child.polyhedron.empty &&
				!FragmentPushOwned(context, &next, &negative_child))
			{
				FragmentRelease(context, &negative_child);
				FragmentRelease(context, &positive_child);
				goto failure;
			}
			FragmentRelease(context, &negative_child);
			if (!positive_child.polyhedron.empty &&
				!FragmentPushOwned(context, &next, &positive_child))
			{
				FragmentRelease(context, &positive_child);
				goto failure;
			}
			FragmentRelease(context, &positive_child);
		}
	}
	FragmentsRelease(context, fragments);
	*fragments = next;
	return 1;

failure:
	FragmentsRelease(context, &next);
	return 0;
}

static int ApplyResponseRefinements(response_context_t *context,
	const response_refinement_vector_t *refinements,
	const response_split_vector_t *splits,
	response_fragment_vector_t *fragments)
{
	response_fragment_vector_t original = *fragments;
	response_fragment_vector_t next;
	uint32_t fragment;
	uint32_t refinement = 0U;

	memset(fragments, 0, sizeof(*fragments));
	memset(&next, 0, sizeof(next));
	for (fragment = 0U; fragment < original.count; fragment++)
	{
		response_fragment_vector_t pieces;

		memset(&pieces, 0, sizeof(pieces));
		if (!FragmentPushOwned(context, &pieces, &original.values[fragment]))
			goto failure;
		while (refinement < refinements->count &&
			refinements->values[refinement].source_fragment < fragment)
			refinement++;
		while (refinement < refinements->count &&
			refinements->values[refinement].source_fragment == fragment)
		{
			const uint32_t split = refinements->values[refinement].split;

			if (split >= splits->count || !ApplyLocalSplit(context,
				&splits->values[split], split, &pieces))
			{
				FragmentsRelease(context, &pieces);
				goto failure;
			}
			refinement++;
		}
		while (pieces.count != 0U)
		{
			response_fragment_work_t moved = pieces.values[pieces.count - 1U];

			memset(&pieces.values[pieces.count - 1U], 0,
				sizeof(*pieces.values));
			pieces.count--;
			if (!FragmentPushOwned(context, &next, &moved))
			{
				FragmentRelease(context, &moved);
				FragmentsRelease(context, &pieces);
				goto failure;
			}
		}
		Release(context, pieces.values);
	}
	Release(context, original.values);
	*fragments = next;
	return fragments->count != 0U;

failure:
	FragmentsRelease(context, &next);
	FragmentsRelease(context, &original);
	return 0;
}

static int ApplySplits(response_context_t *context,
	const sg_bsp_world_t *world, const response_patch_vector_t *patches,
	const response_split_vector_t *splits,
	response_fragment_vector_t *fragments)
{
	uint32_t split;

	for (split = 0U; split < splits->count; split++)
		if (SplitRefinesCells(&splits->values[split]) &&
			!ApplyOneSplit(context, world, patches, &splits->values[split],
				split, fragments))
			return 0;
	return fragments->count != 0U;
}

static int PatchBoundsUpdate(response_patch_work_t *work)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
	{
		float minimum = INFINITY;
		float maximum = -INFINITY;
		uint32_t vertex;

		for (vertex = 0U; vertex < work->patch.vertex_count; vertex++)
		{
			minimum = fminf(minimum, work->vertices[vertex].value[axis]);
			maximum = fmaxf(maximum, work->vertices[vertex].value[axis]);
		}
		if (!ToQ8(minimum, &work->patch.bounds.mins.value[axis]) ||
			!ToQ8(maximum, &work->patch.bounds.maxs.value[axis]))
			return 0;
	}
	return 1;
}

static int PatchChild(response_context_t *context,
	const response_patch_work_t *parent,
	const sg_rune_compact_partition_halfspace_t *halfspace,
	response_patch_work_t *child)
{
	sg_rune_compact_partition_allocator_t allocator;
	sg_rune_compact_partition_polygon_t input;
	sg_rune_compact_partition_polygon_t output;
	size_t bytes;

	memset(child, 0, sizeof(*child));
	memset(&input, 0, sizeof(input));
	memset(&output, 0, sizeof(output));
	CompactPlaneToConfiguration(&parent->patch.plane, &input.plane);
	input.vertices = parent->vertices;
	input.vertex_count = parent->patch.vertex_count;
	PartitionAllocator(context, &allocator);
	if (!SG_RuneCompactPartitionClipPolygon(&input, halfspace, &allocator,
		&output, NULL))
		return 0;
	if (output.vertex_count < 3U)
	{
		SG_RuneCompactPartitionPolygonDestroy(&output, &allocator);
		return 1;
	}
	if (!BytesFor(output.vertex_count, sizeof(*child->vertices), &bytes))
	{
		SG_RuneCompactPartitionPolygonDestroy(&output, &allocator);
		return 0;
	}
	child->vertices = Allocate(context, bytes);
	if (child->vertices == NULL)
	{
		SG_RuneCompactPartitionPolygonDestroy(&output, &allocator);
		return 0;
	}
	memcpy(child->vertices, output.vertices, bytes);
	child->patch = parent->patch;
	child->query_surface = parent->query_surface;
	child->patch.vertex_count = output.vertex_count;
	SG_RuneCompactPartitionPolygonDestroy(&output, &allocator);
	if (!PatchBoundsUpdate(child))
	{
		Release(context, child->vertices);
		memset(child, 0, sizeof(*child));
		return 0;
	}
	return 1;
}

static int PatchSplitApplies(const response_patch_work_t *patch,
	const sg_rune_compact_response_split_t *split)
{
	if (split->kind == SG_RUNE_COMPACT_RESPONSE_SPLIT_TARGET_EDGE)
		return 0;
	return split->kind != SG_RUNE_COMPACT_RESPONSE_SPLIT_FIRST_HIT_TIE ||
		patch->patch.visibility_surface_id == split->target_surface_id;
}

static int ApplyOnePatchSplit(response_context_t *context,
	const sg_rune_compact_response_split_t *split,
	response_patch_vector_t *patches)
{
	response_patch_vector_t next;
	sg_rune_compact_partition_halfspace_t negative;
	sg_rune_compact_partition_halfspace_t positive;
	uint32_t index;

	memset(&next, 0, sizeof(next));
	memset(&negative, 0, sizeof(negative));
	CompactPlaneToConfiguration(&split->plane, &negative.plane);
	FlipHalfspace(&negative, &positive);
	positive.open = 1U;
	for (index = 0U; index < patches->count; index++)
	{
		response_patch_work_t *patch = &patches->values[index];
		response_patch_work_t low;
		response_patch_work_t high;
		sg_configuration_plane_t split_plane;

		memset(&low, 0, sizeof(low));
		memset(&high, 0, sizeof(high));
		CompactPlaneToConfiguration(&split->plane, &split_plane);
		if (!PatchSplitApplies(patch, split) ||
			!PlaneCrossesPatch(&split_plane, patch))
		{
			response_patch_work_t moved = *patch;

			memset(patch, 0, sizeof(*patch));
			if (!PatchPushOwned(context, &next, &moved))
			{
				Release(context, moved.vertices);
				goto failure;
			}
			continue;
		}

		if (!PatchChild(context, patch, &negative, &low) ||
			!PatchChild(context, patch, &positive, &high))
		{
			Release(context, low.vertices);
			Release(context, high.vertices);
			goto failure;
		}
		if (low.patch.vertex_count != 0U && !PatchPushOwned(context, &next,
			&low))
		{
			Release(context, low.vertices);
			Release(context, high.vertices);
			goto failure;
		}
		Release(context, low.vertices);
		if (high.patch.vertex_count != 0U && !PatchPushOwned(context, &next,
			&high))
		{
			Release(context, high.vertices);
			goto failure;
		}
		Release(context, high.vertices);
	}
	PatchesRelease(context, patches);
	*patches = next;
	return 1;

failure:
	PatchesRelease(context, &next);
	return 0;
}

static int ApplyPatchSplits(response_context_t *context,
	const response_split_vector_t *splits, response_patch_vector_t *patches)
{
	uint32_t split;

	for (split = 0U; split < splits->count; split++)
		if (SplitRefinesCells(&splits->values[split]) &&
			!ApplyOnePatchSplit(context, &splits->values[split], patches))
			return 0;
	return patches->count != 0U;
}

static int PlaneCrossesPolyhedron(const sg_configuration_plane_t *plane,
	const sg_rune_compact_partition_polyhedron_t *polyhedron)
{
	double minimum = INFINITY;
	double maximum = -INFINITY;
	uint32_t face;
	uint64_t vertices = 0U;

	for (face = 0U; face < polyhedron->face_count; face++)
	{
		const sg_rune_compact_partition_polygon_t *polygon =
			&polyhedron->faces[face];
		uint32_t vertex;

		for (vertex = 0U; vertex < polygon->vertex_count; vertex++)
		{
			double residual = -(double)plane->distance;
			uint32_t axis;

			for (axis = 0U; axis < 3U; axis++)
				residual += (double)plane->normal[axis] *
					(double)polygon->vertices[vertex].value[axis];
			minimum = fmin(minimum, residual);
			maximum = fmax(maximum, residual);
			vertices++;
		}
	}
	if (vertices == 0U)
		return PlaneCrossesBounds(plane, &polyhedron->bounds);
	return minimum < -RESPONSE_EPSILON && maximum > RESPONSE_EPSILON;
}

static int PlaneCrossesPatch(const sg_configuration_plane_t *plane,
	const response_patch_work_t *patch)
{
	double minimum = INFINITY;
	double maximum = -INFINITY;
	uint32_t vertex;

	for (vertex = 0U; vertex < patch->patch.vertex_count; vertex++)
	{
		double residual = -(double)plane->distance;
		uint32_t axis;

		for (axis = 0U; axis < 3U; axis++)
			residual += (double)plane->normal[axis] *
				(double)patch->vertices[vertex].value[axis];
		minimum = fmin(minimum, residual);
		maximum = fmax(maximum, residual);
	}
	return minimum < -RESPONSE_EPSILON && maximum > RESPONSE_EPSILON;
}

static int RefinementTerminal(const sg_bsp_world_t *world,
	const response_fragment_vector_t *fragments,
	const response_patch_vector_t *patches,
	const response_split_vector_t *splits)
{
	uint32_t split;

	for (split = 0U; split < splits->count; split++)
	{
		sg_configuration_plane_t plane;
		uint32_t fragment;
		uint32_t patch;

		CompactPlaneToConfiguration(&splits->values[split].plane, &plane);
		if (!SplitRefinesCells(&splits->values[split]))
			continue;
		for (fragment = 0U; fragment < fragments->count; fragment++)
			if (SplitApplies(world, &fragments->values[fragment], patches,
				&splits->values[split]) &&
				PlaneCrossesPolyhedron(&plane,
				&fragments->values[fragment].polyhedron))
				return 0;
		for (patch = 0U; patch < patches->count; patch++)
			if (PatchSplitApplies(&patches->values[patch],
				&splits->values[split]) &&
				PlaneCrossesPatch(&plane, &patches->values[patch]))
				return 0;
	}
	return 1;
}

static int FragmentWitness(const response_fragment_work_t *fragment,
	float point[3], sg_rune_q8_vec3_t *q8_out)
{
	double sum[3] = { 0.0, 0.0, 0.0 };
	uint64_t count = 0U;
	uint32_t face;
	uint32_t axis;

	for (face = 0U; face < fragment->polyhedron.face_count; face++)
	{
		const sg_rune_compact_partition_polygon_t *polygon =
			&fragment->polyhedron.faces[face];
		uint32_t vertex;

		for (vertex = 0U; vertex < polygon->vertex_count; vertex++)
		{
			for (axis = 0U; axis < 3U; axis++)
				sum[axis] += (double)polygon->vertices[vertex].value[axis];
			count++;
		}
	}
	for (axis = 0U; axis < 3U; axis++)
	{
		if (count != 0U)
			point[axis] = (float)(sum[axis] / (double)count);
		else
			point[axis] = 0.5f *
				(fragment->polyhedron.bounds.mins.value[axis] +
				 fragment->polyhedron.bounds.maxs.value[axis]);
		if (!ToQ8(point[axis], &q8_out->value[axis]))
			return 0;
	}
	return 1;
}

static void EndpointIndexRelease(response_context_t *context,
	response_endpoint_index_t *index)
{
	Release(context, index->groups);
	Release(context, index->members);
	memset(index, 0, sizeof(*index));
}

static int EndpointGroupCompare(
	const sg_rune_compact_response_endpoint_group_t *left,
	const sg_rune_compact_response_endpoint_group_t *right)
{
	if (left->bsp_cluster != right->bsp_cluster)
		return left->bsp_cluster < right->bsp_cluster ? -1 : 1;
	if (left->bsp_area != right->bsp_area)
		return left->bsp_area < right->bsp_area ? -1 : 1;
	if (left->flags != right->flags)
		return left->flags < right->flags ? -1 : 1;
	return 0;
}

static void SortEndpointGroups(response_endpoint_index_t *index)
{
	uint32_t group;

	for (group = 1U; group < index->group_count; group++)
	{
		sg_rune_compact_response_endpoint_group_t value =
			index->groups[group];
		uint32_t cursor = group;

		while (cursor != 0U && EndpointGroupCompare(&value,
			&index->groups[cursor - 1U]) < 0)
		{
			index->groups[cursor] = index->groups[cursor - 1U];
			cursor--;
		}
		index->groups[cursor] = value;
	}
}

static int EndpointGroupAdd(response_context_t *context,
	response_endpoint_index_t *index, uint32_t cluster, uint32_t area,
	uint32_t flags)
{
	uint32_t group;

	for (group = 0U; group < index->group_count; group++)
		if (index->groups[group].bsp_cluster == cluster &&
			index->groups[group].bsp_area == area &&
			index->groups[group].flags == flags)
			return 1;
	if (!Grow(context, (void **)&index->groups, &index->group_capacity,
		index->group_count + 1U, sizeof(*index->groups)))
		return 0;
	memset(&index->groups[index->group_count], 0, sizeof(*index->groups));
	index->groups[index->group_count].bsp_cluster = cluster;
	index->groups[index->group_count].bsp_area = area;
	index->groups[index->group_count].flags = flags;
	index->group_count++;
	return 1;
}

static int BuildSourceEndpointIndex(response_context_t *context,
	const response_fragment_vector_t *fragments,
	response_endpoint_index_t *index)
{
	uint32_t fragment;
	uint32_t group;

	memset(index, 0, sizeof(*index));
	for (fragment = 0U; fragment < fragments->count; fragment++)
		if (!EndpointGroupAdd(context, index,
			fragments->values[fragment].bsp_cluster,
			fragments->values[fragment].bsp_area, 0U))
			goto failure;
	SortEndpointGroups(index);
	for (group = 0U; group < index->group_count; group++)
	{
		index->groups[group].first_member = index->member_count;
		for (fragment = 0U; fragment < fragments->count; fragment++)
			if (fragments->values[fragment].bsp_cluster ==
				index->groups[group].bsp_cluster &&
				fragments->values[fragment].bsp_area ==
				index->groups[group].bsp_area && index->groups[group].flags == 0U)
			{
				if (!Grow(context, (void **)&index->members,
					&index->member_capacity, index->member_count + 1U,
					sizeof(*index->members)))
					goto failure;
				index->members[index->member_count++] = fragment;
				index->groups[group].member_count++;
			}
	}
	return 1;

failure:
	EndpointIndexRelease(context, index);
	return 0;
}

static int BuildTargetEndpointIndex(response_context_t *context,
	const response_patch_vector_t *patches, response_endpoint_index_t *index)
{
	uint32_t patch;
	uint32_t group;

	memset(index, 0, sizeof(*index));
	for (patch = 0U; patch < patches->count; patch++)
		if ((patches->values[patch].patch.flags &
			SG_RUNE_COMPACT_RESPONSE_PATCH_SKY) == 0U &&
			!EndpointGroupAdd(context, index,
				patches->values[patch].patch.bsp_cluster,
				patches->values[patch].patch.bsp_area,
				(patches->values[patch].patch.flags &
					SG_RUNE_COMPACT_RESPONSE_PATCH_MOVING) != 0U ?
					SG_RUNE_COMPACT_RESPONSE_ENDPOINT_MOVING : 0U))
			goto failure;
	SortEndpointGroups(index);
	for (group = 0U; group < index->group_count; group++)
	{
		index->groups[group].first_member = index->member_count;
		for (patch = 0U; patch < patches->count; patch++)
			if ((patches->values[patch].patch.flags &
				SG_RUNE_COMPACT_RESPONSE_PATCH_SKY) == 0U &&
				patches->values[patch].patch.bsp_cluster ==
					index->groups[group].bsp_cluster &&
				patches->values[patch].patch.bsp_area ==
					index->groups[group].bsp_area &&
				(((patches->values[patch].patch.flags &
					SG_RUNE_COMPACT_RESPONSE_PATCH_MOVING) != 0U) ==
				 ((index->groups[group].flags &
					SG_RUNE_COMPACT_RESPONSE_ENDPOINT_MOVING) != 0U)))
			{
				if (!Grow(context, (void **)&index->members,
					&index->member_capacity, index->member_count + 1U,
					sizeof(*index->members)))
					goto failure;
				index->members[index->member_count++] = patch;
				index->groups[group].member_count++;
			}
	}
	return 1;

failure:
	EndpointIndexRelease(context, index);
	return 0;
}

static int CandidatePush(response_context_t *context,
	response_candidate_vector_t *vector,
	const sg_rune_compact_response_candidate_group_t *candidate)
{
	if (!Grow(context, (void **)&vector->values, &vector->capacity,
		vector->count + 1U, sizeof(*vector->values)))
		return 0;
	vector->values[vector->count++] = *candidate;
	return 1;
}

static int BuildCandidateGroups(response_context_t *context,
	const sg_rune_compact_builder_owner_view_t *owner,
	const response_endpoint_index_t *sources,
	const response_endpoint_index_t *targets,
	response_candidate_vector_t *candidates)
{
	uint32_t source_group;

	memset(candidates, 0, sizeof(*candidates));
	for (source_group = 0U; source_group < sources->group_count; source_group++)
	{
		uint32_t target_group;

		for (target_group = 0U; target_group < targets->group_count;
			target_group++)
		{
			const sg_rune_compact_response_endpoint_group_t *source =
				&sources->groups[source_group];
			const sg_rune_compact_response_endpoint_group_t *target =
				&targets->groups[target_group];
			sg_rune_compact_response_candidate_group_t candidate;

			if (!PvsAllows(owner->world, source->bsp_cluster,
				target->bsp_cluster))
				continue;
			if (source->bsp_area != SG_RUNE_COMPACT_RESPONSE_INDEX_NONE &&
				target->bsp_area != SG_RUNE_COMPACT_RESPONSE_INDEX_NONE &&
				(source->bsp_area >= owner->visibility->area_count ||
				 target->bsp_area >= owner->visibility->area_count ||
				 owner->visibility->area_components == NULL ||
				 owner->visibility->area_components[source->bsp_area] !=
					owner->visibility->area_components[target->bsp_area]))
				continue;

			memset(&candidate, 0, sizeof(candidate));
			candidate.source_group = source_group;
			candidate.target_group = target_group;
			candidate.classification =
				SG_RUNE_COMPACT_STATIC_VISIBILITY_CONDITIONAL;
			candidate.reason = (target->flags &
				SG_RUNE_COMPACT_RESPONSE_ENDPOINT_MOVING) != 0U ?
				SG_RUNE_COMPACT_STATIC_VISIBILITY_REASON_MOVING_SUBMODEL :
				SG_RUNE_COMPACT_STATIC_VISIBILITY_REASON_EXACT_RAY_REQUIRED;
			candidate.requires_exact_ray = 1U;
			candidate.requires_area_state =
				source->bsp_area != SG_RUNE_COMPACT_RESPONSE_INDEX_NONE &&
				target->bsp_area != SG_RUNE_COMPACT_RESPONSE_INDEX_NONE &&
				source->bsp_area != target->bsp_area;
			if (candidate.requires_area_state != 0U)
				candidate.relation_flags =
					SG_RUNE_COMPACT_STATIC_RELATION_PORTAL_CROSSING;
			if (!CandidatePush(context, candidates, &candidate))
				return 0;
		}
	}
	return 1;
}

static int PatchWitness(const response_patch_work_t *patch, float point[3],
	sg_rune_q8_vec3_t *q8_out)
{
	uint32_t vertex;
	uint32_t axis;

	if (patch->patch.vertex_count < 3U || patch->vertices == NULL)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
	{
		double sum = 0.0;

		for (vertex = 0U; vertex < patch->patch.vertex_count; vertex++)
			sum += (double)patch->vertices[vertex].value[axis];
		point[axis] = (float)(sum / (double)patch->patch.vertex_count);
		if (!ToQ8(point[axis], &q8_out->value[axis]))
			return 0;
	}
	return 1;
}

/* The builder calls this only for one canonical representative of an endpoint
 * group.  Candidates remain the sparse live-query fallback; certificates are
 * map-only facts, never a player or mover trace. */
static int ProbeResponsePair(const sg_rune_compact_builder_owner_view_t *owner,
	const response_fragment_vector_t *fragments,
	const response_patch_vector_t *patches,
	const response_endpoint_index_t *sources,
	const response_endpoint_index_t *targets,
	const sg_rune_compact_response_candidate_group_t *candidate,
	uint32_t *source_index_out, uint32_t *target_index_out,
	float source_point[3], float target_point[3],
	sg_rune_q8_vec3_t *target_q8_out,
	sg_static_visibility_result_t *result_out)
{
	const sg_rune_compact_response_endpoint_group_t *source_group;
	const sg_rune_compact_response_endpoint_group_t *target_group;
	const response_fragment_work_t *source;
	const response_patch_work_t *target;
	sg_host_collision_scene_t scene;
	sg_static_visibility_error_t error;
	sg_rune_q8_vec3_t source_q8;
	uint32_t source_index;
	uint32_t target_index;

	if (candidate->source_group >= sources->group_count ||
		candidate->target_group >= targets->group_count)
		return -1;
	source_group = &sources->groups[candidate->source_group];
	target_group = &targets->groups[candidate->target_group];
	if (source_group->member_count == 0U || target_group->member_count == 0U ||
		source_group->first_member >= sources->member_count ||
		target_group->first_member >= targets->member_count)
		return -1;
	source_index = sources->members[source_group->first_member];
	/* Pick the far canonical member, rather than multiplying every source
	 * cell by every clipped patch.  It is a stable probe that can expose a
	 * genuine first hit behind an occluder; the other members remain sparse
	 * query-time candidates. */
	target_index = targets->members[target_group->first_member +
		target_group->member_count - 1U];
	if (source_index >= fragments->count || target_index >= patches->count)
		return -1;
	source = &fragments->values[source_index];
	target = &patches->values[target_index];
	if ((target->patch.flags & (SG_RUNE_COMPACT_RESPONSE_PATCH_SKY |
		SG_RUNE_COMPACT_RESPONSE_PATCH_MOVING)) != 0U)
		return 0;
	if (!FragmentWitness(source, source_point, &source_q8) ||
		!PatchWitness(target, target_point, target_q8_out))
		return -1;
	memset(&scene, 0, sizeof(scene));
	memset(&error, 0, sizeof(error));
	if (!SG_StaticVisibilityQueryBoundSurface(owner->collision, &scene,
		owner->configuration, owner->semantics, owner->visibility,
		source->static_partition, source_point, target->query_surface,
		target_point, result_out, &error))
		return -1;
	*source_index_out = source_index;
	*target_index_out = target_index;
	return 1;
}

static int FindImpactOwner(const response_occluder_side_vector_t *sides,
	const sg_host_collision_trace_t *trace, uint32_t *occluder_out,
	uint32_t *side_out)
{
	uint32_t side;

	if (trace->model_index != SG_HOST_COLLISION_MODEL_WORLD ||
		trace->instance_id != 0U ||
		trace->brush == SG_HOST_COLLISION_BRUSH_NONE ||
		trace->brush_side == SG_HOST_COLLISION_BRUSH_NONE)
		return 0;
	for (side = 0U; side < sides->count; side++)
		if (sides->values[side].model == SG_HOST_COLLISION_MODEL_WORLD &&
			sides->values[side].brush == trace->brush &&
			sides->values[side].brush_side == trace->brush_side)
		{
			*occluder_out = sides->values[side].occluder;
			*side_out = side;
			return 1;
		}
	return 0;
}

static uint32_t FindImpactSplit(const response_split_vector_t *splits,
	uint32_t occluder, uint32_t side)
{
	uint32_t split;

	for (split = 0U; split < splits->count; split++)
		if (splits->values[split].kind ==
			SG_RUNE_COMPACT_RESPONSE_SPLIT_OCCLUDER_PLANE &&
			splits->values[split].occluder == occluder &&
			splits->values[split].edge == side)
			return split;
	return SG_RUNE_COMPACT_RESPONSE_INDEX_NONE;
}

static int BuildResponseProbes(response_context_t *context,
	const sg_rune_compact_builder_owner_view_t *owner,
	const response_fragment_vector_t *fragments,
	const response_patch_vector_t *patches,
	const response_occluder_side_vector_t *sides,
	const response_endpoint_index_t *sources,
	const response_endpoint_index_t *targets,
	const response_candidate_vector_t *candidates,
	response_probe_vector_t *probes)
{
	uint32_t candidate_index;

	memset(probes, 0, sizeof(*probes));
	for (candidate_index = 0U; candidate_index < candidates->count;
		candidate_index++)
	{
		const sg_rune_compact_response_candidate_group_t *candidate =
			&candidates->values[candidate_index];
		sg_static_visibility_result_t result;
		sg_rune_q8_vec3_t target_q8;
		float source_point[3];
		float target_point[3];
		response_probe_t probe;
		uint32_t source;
		uint32_t target;
		int outcome;

		memset(&result, 0, sizeof(result));
		outcome = ProbeResponsePair(owner, fragments, patches, sources, targets,
			candidate, &source, &target, source_point, target_point,
			&target_q8, &result);
		if (outcome < 0)
			goto failure;
		if (outcome == 0)
			continue;
		memset(&probe, 0, sizeof(probe));
		probe.source_fragment = source;
		probe.target_patch = target;
		probe.impact_occluder = SG_RUNE_COMPACT_RESPONSE_INDEX_NONE;
		probe.impact_side = SG_RUNE_COMPACT_RESPONSE_INDEX_NONE;
		if (result.classification == SG_STATIC_VISIBILITY_VISIBLE)
		{
			if (!ProbePush(context, probes, &probe))
				goto failure;
			continue;
		}
		if (result.classification != SG_STATIC_VISIBILITY_OCCLUDED ||
			result.reason != SG_STATIC_VISIBILITY_REASON_STATIC_WORLD ||
			!FindImpactOwner(sides, &result.trace, &probe.impact_occluder,
				&probe.impact_side))
			continue;
		probe.static_impact = 1U;
		if (!ProbePush(context, probes, &probe))
			goto failure;
	}
	return 1;

failure:
	ProbesRelease(context, probes);
	return 0;
}

static int AddOccluderEdgeSplitsForSide(response_context_t *context,
	const response_occluder_side_vector_t *sides,
	const response_occluder_edge_vector_t *edges, uint32_t occluder,
	uint32_t side_index, response_split_vector_t *splits)
{
	const sg_rune_compact_response_occluder_side_t *side;
	uint32_t edge_index;

	if (side_index >= sides->count || sides->values[side_index].occluder !=
		occluder)
		return 0;
	side = &sides->values[side_index];
	for (edge_index = 0U; edge_index < edges->count; edge_index++)
	{
		const sg_rune_compact_response_occluder_edge_t *edge =
			&edges->values[edge_index];
		sg_rune_vec3_t from;
		sg_rune_vec3_t to;
		sg_configuration_plane_t plane;
		float normal[3];
		uint32_t axis;

		if (edge->occluder != occluder || edge->side != side_index)
			continue;
		for (axis = 0U; axis < 3U; axis++)
		{
			from.value[axis] = BitsFloat(edge->from.value_bits[axis]);
			to.value[axis] = BitsFloat(edge->to.value_bits[axis]);
			normal[axis] = BitsFloat(side->plane.normal_bits[axis]);
		}
		EdgePlane(&from, &to, normal, &plane);
		if (!SplitPush(context, splits, &plane,
			SG_RUNE_COMPACT_RESPONSE_SPLIT_OCCLUDER_EDGE, UINT64_MAX,
			occluder, edge_index, SG_HOST_COLLISION_BRUSH_NONE))
			return 0;
	}
	return 1;
}

static int AddFirstHitTieSplit(response_context_t *context,
	const response_patch_work_t *patch,
	const sg_rune_compact_response_occluder_side_t *side, uint32_t side_index,
	response_split_vector_t *splits)
{
	sg_configuration_plane_t target_plane;
	sg_configuration_plane_t occluder_plane;
	sg_configuration_plane_t tie;
	uint32_t axis;

	CompactPlaneToConfiguration(&patch->patch.plane, &target_plane);
	CompactPlaneToConfiguration(&side->halfspace_plane, &occluder_plane);
	memset(&tie, 0, sizeof(tie));
	for (axis = 0U; axis < 3U; axis++)
		tie.normal[axis] = target_plane.normal[axis] -
			occluder_plane.normal[axis];
	tie.distance = target_plane.distance - occluder_plane.distance;
	return SplitPush(context, splits, &tie,
		SG_RUNE_COMPACT_RESPONSE_SPLIT_FIRST_HIT_TIE,
		patch->patch.visibility_surface_id, side->occluder, side_index,
		SG_HOST_COLLISION_BRUSH_NONE);
}

static int AddProbeDrivenSplits(response_context_t *context,
	const response_patch_vector_t *patches,
	const response_occluder_side_vector_t *sides,
	const response_occluder_edge_vector_t *edges,
	const response_probe_vector_t *probes, response_split_vector_t *splits)
{
	uint32_t probe_index;

	for (probe_index = 0U; probe_index < probes->count; probe_index++)
	{
		const response_probe_t *probe = &probes->values[probe_index];
		const response_patch_work_t *patch;

		if (probe->target_patch >= patches->count ||
			!AddTargetEdgeSplitsForPatch(context,
				&patches->values[probe->target_patch], splits))
			return 0;
		patch = &patches->values[probe->target_patch];
		if (probe->static_impact != 0U)
		{
			const sg_rune_compact_response_occluder_side_t *side;
			sg_configuration_plane_t plane;

			if (probe->impact_side >= sides->count ||
				sides->values[probe->impact_side].occluder !=
					probe->impact_occluder)
				return 0;
			side = &sides->values[probe->impact_side];
			CompactPlaneToConfiguration(&side->halfspace_plane, &plane);
			if (!SplitPush(context, splits, &plane,
				SG_RUNE_COMPACT_RESPONSE_SPLIT_OCCLUDER_PLANE, UINT64_MAX,
				probe->impact_occluder, probe->impact_side, side->brush_side) ||
				!AddOccluderEdgeSplitsForSide(context, sides, edges,
					probe->impact_occluder, probe->impact_side, splits) ||
				!AddFirstHitTieSplit(context, patch, side, probe->impact_side,
					splits))
				return 0;
		}
	}
	return 1;
}

static int AddRefinementIfCrossing(response_context_t *context,
	response_refinement_vector_t *refinements,
	const response_fragment_vector_t *fragments,
	const response_split_vector_t *splits, uint32_t source_fragment,
	uint32_t split)
{
	sg_configuration_plane_t plane;

	if (source_fragment >= fragments->count || split >= splits->count)
		return 0;
	CompactPlaneToConfiguration(&splits->values[split].plane, &plane);
	return !PlaneCrossesPolyhedron(&plane,
		&fragments->values[source_fragment].polyhedron) ||
		RefinementPush(context, refinements, source_fragment, split);
}

static int TargetEdgeSplitMatchesPatch(
	const sg_rune_compact_response_split_t *split,
	const response_patch_work_t *patch)
{
	sg_configuration_plane_t plane;
	sg_rune_binary32_plane_t compact;
	float normal[3];
	uint32_t axis;

	if (split->kind != SG_RUNE_COMPACT_RESPONSE_SPLIT_TARGET_EDGE ||
		split->target_surface_id != patch->patch.visibility_surface_id ||
		split->edge >= patch->patch.vertex_count)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		normal[axis] = BitsFloat(patch->patch.plane.normal_bits[axis]);
	EdgePlane(&patch->vertices[split->edge],
		&patch->vertices[(split->edge + 1U) % patch->patch.vertex_count],
		normal, &plane);
	if (!NormalizePlane(&plane, &plane))
		return 0;
	CanonicalizePlane(&plane);
	PlaneToCompact(&plane, &compact);
	return memcmp(&compact, &split->plane, sizeof(compact)) == 0;
}

static int BuildResponseRefinements(response_context_t *context,
	const response_fragment_vector_t *fragments,
	const response_patch_vector_t *patches,
	const response_split_vector_t *splits,
	const response_occluder_edge_vector_t *edges,
	const response_probe_vector_t *probes,
	response_refinement_vector_t *refinements)
{
	uint32_t probe_index;

	memset(refinements, 0, sizeof(*refinements));
	for (probe_index = 0U; probe_index < probes->count; probe_index++)
	{
		const response_probe_t *probe = &probes->values[probe_index];
		uint32_t split;

		if (probe->source_fragment >= fragments->count ||
			probe->target_patch >= patches->count)
			goto failure;
		for (split = 0U; split < splits->count; split++)
			if (TargetEdgeSplitMatchesPatch(&splits->values[split],
					&patches->values[probe->target_patch]) &&
				!AddRefinementIfCrossing(context, refinements, fragments, splits,
					probe->source_fragment, split))
				goto failure;
		if (probe->static_impact != 0U)
		{
			for (split = 0U; split < splits->count; split++)
			{
				const sg_rune_compact_response_split_t *boundary =
					&splits->values[split];

				if ((boundary->kind ==
					SG_RUNE_COMPACT_RESPONSE_SPLIT_OCCLUDER_PLANE &&
					boundary->occluder == probe->impact_occluder &&
					boundary->edge == probe->impact_side) ||
					(boundary->kind ==
					SG_RUNE_COMPACT_RESPONSE_SPLIT_OCCLUDER_EDGE &&
					boundary->edge < edges->count &&
					edges->values[boundary->edge].occluder ==
						probe->impact_occluder &&
					edges->values[boundary->edge].side ==
						probe->impact_side) ||
					(boundary->kind ==
					SG_RUNE_COMPACT_RESPONSE_SPLIT_FIRST_HIT_TIE &&
					 boundary->target_surface_id ==
						patches->values[probe->target_patch].patch.visibility_surface_id &&
					 boundary->occluder == probe->impact_occluder &&
					 boundary->edge == probe->impact_side))
					if (!AddRefinementIfCrossing(context, refinements, fragments,
						splits, probe->source_fragment, split))
						goto failure;
			}
		}
	}
	SortRefinements(refinements);
	return 1;

failure:
	RefinementsRelease(context, refinements);
	return 0;
}

static int PairWorkCompare(const response_fragment_vector_t *fragments,
	const response_patch_vector_t *patches,
	const sg_rune_compact_response_pair_t *left,
	const sg_rune_compact_response_pair_t *right)
{
	const response_fragment_work_t *left_source =
		&fragments->values[left->source_fragment];
	const response_fragment_work_t *right_source =
		&fragments->values[right->source_fragment];
	const sg_rune_compact_response_patch_t *left_target =
		&patches->values[left->target_patch].patch;
	const sg_rune_compact_response_patch_t *right_target =
		&patches->values[right->target_patch].patch;

#define RESPONSE_PAIR_FIELD(a, b) \
	do { if ((a) != (b)) return (a) < (b) ? -1 : 1; } while (0)
	RESPONSE_PAIR_FIELD(left_source->parent_cell.value,
		right_source->parent_cell.value);
	RESPONSE_PAIR_FIELD(left_target->target_cell.value,
		right_target->target_cell.value);
	RESPONSE_PAIR_FIELD(left_source->static_partition_id,
		right_source->static_partition_id);
	RESPONSE_PAIR_FIELD(left_target->static_partition_id,
		right_target->static_partition_id);
	RESPONSE_PAIR_FIELD(left_source->configuration_region,
		right_source->configuration_region);
	RESPONSE_PAIR_FIELD(left_source->configuration_cell,
		right_source->configuration_cell);
	RESPONSE_PAIR_FIELD(left_target->configuration_region,
		right_target->configuration_region);
	RESPONSE_PAIR_FIELD(left_target->configuration_cell,
		right_target->configuration_cell);
	RESPONSE_PAIR_FIELD(left_source->bsp_leaf, right_source->bsp_leaf);
	RESPONSE_PAIR_FIELD(left_source->bsp_area, right_source->bsp_area);
	RESPONSE_PAIR_FIELD(left_source->bsp_cluster, right_source->bsp_cluster);
	RESPONSE_PAIR_FIELD(left_target->bsp_leaf, right_target->bsp_leaf);
	RESPONSE_PAIR_FIELD(left_target->bsp_area, right_target->bsp_area);
	RESPONSE_PAIR_FIELD(left_target->bsp_cluster, right_target->bsp_cluster);
	/* Source incidences are fixed by parent_cell.  Target incidences travel
	 * with the patch, so retain their published tie-break position here. */
	RESPONSE_PAIR_FIELD(left_target->boundary_incidences.first,
		right_target->boundary_incidences.first);
	RESPONSE_PAIR_FIELD(left_target->boundary_incidences.count,
		right_target->boundary_incidences.count);
	RESPONSE_PAIR_FIELD(left->source_fragment, right->source_fragment);
	RESPONSE_PAIR_FIELD(left->target_patch, right->target_patch);
#undef RESPONSE_PAIR_FIELD
	return 0;
}

static void SortPairs(const response_fragment_vector_t *fragments,
	const response_patch_vector_t *patches, response_pair_vector_t *pairs)
{
	uint32_t index;

	for (index = 1U; index < pairs->count; index++)
	{
		sg_rune_compact_response_pair_t value = pairs->values[index];
		uint32_t cursor = index;

		while (cursor != 0U && PairWorkCompare(fragments, patches, &value,
			&pairs->values[cursor - 1U]) < 0)
		{
			pairs->values[cursor] = pairs->values[cursor - 1U];
			cursor--;
		}
		pairs->values[cursor] = value;
	}
}

static int BuildCertifiedPairs(response_context_t *context,
	const sg_rune_compact_builder_owner_view_t *owner,
	const response_fragment_vector_t *fragments,
	const response_patch_vector_t *patches,
	const response_split_vector_t *splits,
	const response_occluder_side_vector_t *occluder_sides,
	const response_endpoint_index_t *sources,
	const response_endpoint_index_t *targets,
	const response_candidate_vector_t *candidates,
	response_pair_vector_t *pairs)
{
	uint32_t candidate_index;

	for (candidate_index = 0U; candidate_index < candidates->count;
		candidate_index++)
	{
		const sg_rune_compact_response_candidate_group_t *candidate =
			&candidates->values[candidate_index];
		sg_static_visibility_result_t result;
		sg_rune_compact_response_pair_t pair;
		sg_rune_q8_vec3_t target_q8;
		float source_point[3];
		float target_point[3];
		uint32_t source;
		uint32_t target;
		int probe;

		memset(&result, 0, sizeof(result));
		probe = ProbeResponsePair(owner, fragments, patches, sources, targets,
			candidate, &source, &target, source_point, target_point,
			&target_q8, &result);
		if (probe < 0)
			return 0;
		if (probe == 0)
			continue;
		(void)source_point;
		(void)target_point;
		memset(&pair, 0, sizeof(pair));
		pair.source_fragment = source;
		pair.target_patch = target;
		pair.classification = SG_STATIC_VISIBILITY_CONDITIONAL;
		pair.reason = (sg_static_visibility_reason_t)candidate->reason;
		pair.first_hit_occluder = SG_RUNE_COMPACT_RESPONSE_INDEX_NONE;
		pair.requires_exact_ray = candidate->requires_exact_ray;
		pair.requires_area_state = candidate->requires_area_state;
		pair.relation_flags = candidate->relation_flags;
		pair.source_valid_stances = fragments->values[source].valid_stances;
		pair.target_valid_stances = patches->values[target].patch.valid_stances;
		pair.certificate_split = SG_RUNE_COMPACT_RESPONSE_INDEX_NONE;
		pair.target_witness = target_q8;
		pair.trace = result.trace;
		if (result.classification == SG_STATIC_VISIBILITY_VISIBLE)
		{
			pair.certificate = SG_RUNE_COMPACT_RESPONSE_CERTIFIED_DIRECT;
			pair.relation_flags |= SG_RUNE_COMPACT_STATIC_RELATION_DIRECT;
		}
		else if (result.classification == SG_STATIC_VISIBILITY_OCCLUDED &&
			result.reason == SG_STATIC_VISIBILITY_REASON_STATIC_WORLD)
		{
			uint32_t impact_occluder;
			uint32_t impact_side;
			uint32_t impact;

			if (!FindImpactOwner(occluder_sides, &result.trace,
				&impact_occluder, &impact_side))
				continue;
			impact = FindImpactSplit(splits, impact_occluder, impact_side);

			if (impact == SG_RUNE_COMPACT_RESPONSE_INDEX_NONE)
				continue;
			pair.certificate =
				SG_RUNE_COMPACT_RESPONSE_CERTIFIED_STATIC_IMPACT;
			pair.first_hit_occluder = impact_occluder;
			pair.certificate_split = impact;
			pair.relation_flags |=
				SG_RUNE_COMPACT_STATIC_RELATION_OCCLUDED_IMPACT;
		}
		else
			continue;
		if (!PairPush(context, pairs, &pair))
			return 0;
	}
	SortPairs(fragments, patches, pairs);
	return 1;
}

static int Q8Bounds(const sg_rune_bounds_t *source,
	sg_rune_q8_bounds_t *result)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		if (!ToQ8(source->mins.value[axis], &result->mins.value[axis]) ||
			!ToQ8(source->maxs.value[axis], &result->maxs.value[axis]))
			return 0;
	return 1;
}

static int AllocateArray(response_context_t *context, uint32_t count,
	size_t element, void **result)
{
	size_t bytes;

	*result = NULL;
	if (count == 0U)
		return 1;
	if (!BytesFor(count, element, &bytes))
	{
		SetError(context, SG_RUNE_COMPACT_RESPONSE_ERROR_OVERFLOW,
			SG_RUNE_COMPACT_RESPONSE_RECORD_RESULT,
			SG_RUNE_COMPACT_RESPONSE_INDEX_NONE);
		return 0;
	}
	*result = Allocate(context, bytes);
	if (*result == NULL)
		return 0;
	memset(*result, 0, bytes);
	return 1;
}

static void ReleaseResultArrays(
	sg_rune_compact_response_partition_t *partition)
{
	response_context_t context;

	memset(&context, 0, sizeof(context));
	context.allocator = partition->allocator;
	Release(&context, partition->source_fragments);
	Release(&context, partition->source_halfspaces);
	Release(&context, partition->target_patches);
	Release(&context, partition->target_vertices);
	Release(&context, partition->splits);
	Release(&context, partition->response_pairs);
	Release(&context, partition->candidate_groups);
	Release(&context, partition->source_endpoint_groups);
	Release(&context, partition->source_endpoint_members);
	Release(&context, partition->target_endpoint_groups);
	Release(&context, partition->target_endpoint_members);
	Release(&context, partition->static_occluders);
	Release(&context, partition->static_occluder_sides);
	Release(&context, partition->static_occluder_edges);
	Release(&context, partition->compact_source_surfaces);
	Release(&context, partition->compact_source_surface_vertices);
	Release(&context, partition->compact_facets);
	Release(&context, partition->bsp_visibility_bit_offsets);
	Release(&context, partition->bsp_visibility_bytes);
	Release(&context, partition->area_components);
	partition->source_fragments = NULL;
	partition->source_halfspaces = NULL;
	partition->target_patches = NULL;
	partition->target_vertices = NULL;
	partition->splits = NULL;
	partition->response_pairs = NULL;
	partition->candidate_groups = NULL;
	partition->source_endpoint_groups = NULL;
	partition->source_endpoint_members = NULL;
	partition->target_endpoint_groups = NULL;
	partition->target_endpoint_members = NULL;
	partition->static_occluders = NULL;
	partition->static_occluder_sides = NULL;
	partition->static_occluder_edges = NULL;
	partition->compact_source_surfaces = NULL;
	partition->compact_source_surface_vertices = NULL;
	partition->compact_facets = NULL;
	partition->bsp_visibility_bit_offsets = NULL;
	partition->bsp_visibility_bytes = NULL;
	partition->area_components = NULL;
}

static int FlattenResult(response_context_t *context,
	const sg_rune_compact_identity_t *identity,
	const sg_rune_compact_builder_owner_view_t *owner,
	const sg_rune_compact_geometry_view_t *geometry,
	const response_fragment_vector_t *fragments,
	const response_patch_vector_t *patches,
	const response_split_vector_t *splits,
	const response_occluder_vector_t *occluders,
	const response_occluder_side_vector_t *occluder_sides,
	const response_occluder_edge_vector_t *occluder_edges,
	const response_pair_vector_t *pairs,
	const response_endpoint_index_t *source_endpoints,
	const response_endpoint_index_t *target_endpoints,
	const response_candidate_vector_t *candidates,
	sg_rune_compact_response_partition_t **partition_out)
{
	sg_rune_compact_response_partition_t *partition;
	uint32_t halfspace_count = 0U;
	uint32_t vertex_count = 0U;
	uint32_t index;
	uint32_t halfspace_cursor = 0U;
	uint32_t vertex_cursor = 0U;
	uint32_t source_surface_vertex_cursor = 0U;
	uint32_t direct_count = 0U;
	uint32_t impact_count = 0U;
	uint32_t unresolved_count = 0U;

	for (index = 0U; index < fragments->count; index++)
		if (!AddU32(halfspace_count, fragments->values[index].halfspaces.count,
			&halfspace_count))
			return 0;
	for (index = 0U; index < patches->count; index++)
		if (!AddU32(vertex_count, patches->values[index].patch.vertex_count,
			&vertex_count))
			return 0;
	for (index = 0U; index < pairs->count; index++)
	{
		const uint32_t certificate = pairs->values[index].certificate;

		if (certificate == SG_RUNE_COMPACT_RESPONSE_CERTIFIED_DIRECT)
		{
			if (!AddU32(direct_count, 1U, &direct_count))
				return 0;
		}
		else if (certificate ==
			SG_RUNE_COMPACT_RESPONSE_CERTIFIED_STATIC_IMPACT)
		{
			if (!AddU32(impact_count, 1U, &impact_count))
				return 0;
		}
		else if (certificate == SG_RUNE_COMPACT_RESPONSE_UNRESOLVED_EXACT_RAY)
		{
			if (!AddU32(unresolved_count, 1U, &unresolved_count))
				return 0;
		}
		else
			return 0;
	}
	partition = Allocate(context, sizeof(*partition));
	if (partition == NULL)
		return 0;
	memset(partition, 0, sizeof(*partition));
	partition->allocator = context->allocator;
	partition->identity = *identity;
	partition->source_fragment_count = fragments->count;
	partition->source_halfspace_count = halfspace_count;
	partition->target_patch_count = patches->count;
	partition->target_vertex_count = vertex_count;
	partition->split_count = splits->count;
	partition->response_pair_count = pairs->count;
	partition->candidate_group_count = candidates->count;
	partition->source_endpoint_group_count = source_endpoints->group_count;
	partition->source_endpoint_member_count = source_endpoints->member_count;
	partition->target_endpoint_group_count = target_endpoints->group_count;
	partition->target_endpoint_member_count = target_endpoints->member_count;
	partition->static_occluder_count = owner->visibility->occluder_count;
	partition->static_occluder_side_count = occluder_sides->count;
	partition->static_occluder_edge_count = occluder_edges->count;
	partition->compact_facet_count = geometry->facet_count;
	partition->compact_cell_count = geometry->cell_count;
	partition->compact_source_surface_count = geometry->source_surface_count;
	partition->compact_source_surface_vertex_count =
		geometry->source_surface_vertex_count;
	partition->bsp_visibility_cluster_count =
		owner->world->visibility.cluster_count;
	partition->bsp_visibility_byte_count = owner->world->visibility.byte_count;
	partition->area_component_count = owner->visibility->area_count;
	if (!AllocateArray(context, fragments->count,
			sizeof(*partition->source_fragments),
			(void **)&partition->source_fragments) ||
		!AllocateArray(context, halfspace_count,
			sizeof(*partition->source_halfspaces),
			(void **)&partition->source_halfspaces) ||
		!AllocateArray(context, patches->count,
			sizeof(*partition->target_patches),
			(void **)&partition->target_patches) ||
		!AllocateArray(context, vertex_count,
			sizeof(*partition->target_vertices),
			(void **)&partition->target_vertices) ||
		!AllocateArray(context, splits->count, sizeof(*partition->splits),
			(void **)&partition->splits) ||
		!AllocateArray(context, pairs->count,
			sizeof(*partition->response_pairs),
			(void **)&partition->response_pairs) ||
		!AllocateArray(context, candidates->count,
			sizeof(*partition->candidate_groups),
			(void **)&partition->candidate_groups) ||
		!AllocateArray(context, source_endpoints->group_count,
			sizeof(*partition->source_endpoint_groups),
			(void **)&partition->source_endpoint_groups) ||
		!AllocateArray(context, source_endpoints->member_count,
			sizeof(*partition->source_endpoint_members),
			(void **)&partition->source_endpoint_members) ||
		!AllocateArray(context, target_endpoints->group_count,
			sizeof(*partition->target_endpoint_groups),
			(void **)&partition->target_endpoint_groups) ||
		!AllocateArray(context, target_endpoints->member_count,
			sizeof(*partition->target_endpoint_members),
			(void **)&partition->target_endpoint_members) ||
		!AllocateArray(context, occluders->count,
			sizeof(*partition->static_occluders),
			(void **)&partition->static_occluders) ||
		!AllocateArray(context, occluder_sides->count,
			sizeof(*partition->static_occluder_sides),
			(void **)&partition->static_occluder_sides) ||
		!AllocateArray(context, occluder_edges->count,
			sizeof(*partition->static_occluder_edges),
			(void **)&partition->static_occluder_edges) ||
		!AllocateArray(context, geometry->source_surface_count,
			sizeof(*partition->compact_source_surfaces),
			(void **)&partition->compact_source_surfaces) ||
		!AllocateArray(context, geometry->source_surface_vertex_count,
			sizeof(*partition->compact_source_surface_vertices),
			(void **)&partition->compact_source_surface_vertices) ||
		!AllocateArray(context, geometry->facet_count,
			sizeof(*partition->compact_facets),
			(void **)&partition->compact_facets) ||
		!AllocateArray(context, owner->world->visibility.cluster_count,
			sizeof(*partition->bsp_visibility_bit_offsets),
			(void **)&partition->bsp_visibility_bit_offsets) ||
		!AllocateArray(context, owner->world->visibility.byte_count,
			sizeof(*partition->bsp_visibility_bytes),
			(void **)&partition->bsp_visibility_bytes) ||
		!AllocateArray(context, owner->visibility->area_count,
			sizeof(*partition->area_components),
			(void **)&partition->area_components))
		goto failure;
	if (geometry->facet_count != 0U)
	{
		if (geometry->facets == NULL || partition->compact_facets == NULL)
			goto failure;
		memcpy(partition->compact_facets, geometry->facets,
			(size_t)geometry->facet_count *
				sizeof(*partition->compact_facets));
	}
	if (owner->world->visibility.cluster_count != 0U)
		memcpy(partition->bsp_visibility_bit_offsets,
			owner->world->visibility.bit_offsets,
			(size_t)owner->world->visibility.cluster_count *
				sizeof(*partition->bsp_visibility_bit_offsets));
	if (owner->world->visibility.byte_count != 0U)
		memcpy(partition->bsp_visibility_bytes,
			owner->world->visibility.bytes,
			(size_t)owner->world->visibility.byte_count);
	if (owner->visibility->area_count != 0U)
		memcpy(partition->area_components,
			owner->visibility->area_components,
			(size_t)owner->visibility->area_count *
				sizeof(*partition->area_components));
	for (index = 0U; index < geometry->source_surface_count; index++)
	{
		const sg_rune_compact_source_surface_t *source =
			&geometry->source_surfaces[index];
		sg_rune_compact_source_surface_t *destination =
			&partition->compact_source_surfaces[index];

		if (!SpanWithin(source->vertices.first, source->vertices.count,
			geometry->source_surface_vertex_count))
			goto failure;
		*destination = *source;
		destination->vertices.first = source_surface_vertex_cursor;
		if (source->vertices.count != 0U)
			memcpy(&partition->compact_source_surface_vertices[
				source_surface_vertex_cursor],
				&geometry->source_surface_vertices[source->vertices.first],
				(size_t)source->vertices.count *
					sizeof(*partition->compact_source_surface_vertices));
		source_surface_vertex_cursor += source->vertices.count;
	}
	if (source_surface_vertex_cursor != geometry->source_surface_vertex_count)
		goto failure;
	for (index = 0U; index < fragments->count; index++)
	{
		const response_fragment_work_t *source = &fragments->values[index];
		sg_rune_compact_response_fragment_t *destination =
			&partition->source_fragments[index];
		float witness[3];
		uint32_t halfspace;

		destination->parent_cell = source->parent_cell;
		destination->boundary_incidences =
			geometry->cells[source->parent_cell.value].incidences;
		destination->static_partition_id = source->static_partition_id;
		destination->configuration_region = source->configuration_region;
		destination->configuration_cell = source->configuration_cell;
		destination->first_halfspace = halfspace_cursor;
		destination->halfspace_count = source->halfspaces.count;
		destination->bsp_leaf = source->bsp_leaf;
		destination->bsp_area = source->bsp_area;
		destination->bsp_cluster = source->bsp_cluster;
		destination->valid_stances = source->valid_stances;
		if (!Q8Bounds(&source->polyhedron.bounds, &destination->bounds) ||
			!FragmentWitness(source, witness, &destination->witness))
			goto failure;
		for (halfspace = 0U; halfspace < source->halfspaces.count; halfspace++)
		{
			const sg_rune_compact_partition_halfspace_t *input =
				&source->halfspaces.values[halfspace];
			sg_rune_compact_response_halfspace_t *output =
				&partition->source_halfspaces[halfspace_cursor++];

			PlaneToCompact(&input->plane, &output->plane);
			output->open = input->open;
			output->split =
				(input->contributor & UINT32_C(0x80000000)) != 0U ?
					input->contributor & UINT32_C(0x7fffffff) :
					SG_RUNE_COMPACT_RESPONSE_INDEX_NONE;
		}
	}
	for (index = 0U; index < patches->count; index++)
	{
		const response_patch_work_t *source = &patches->values[index];
		sg_rune_compact_response_patch_t *destination =
			&partition->target_patches[index];
		uint32_t vertex;
		uint32_t axis;

		*destination = source->patch;
		destination->first_vertex = vertex_cursor;
		for (vertex = 0U; vertex < source->patch.vertex_count; vertex++)
			for (axis = 0U; axis < 3U; axis++)
				if (!ToQ8(source->vertices[vertex].value[axis],
					&partition->target_vertices[vertex_cursor + vertex]
						.value[axis]))
					goto failure;
		vertex_cursor += source->patch.vertex_count;
	}
	if (splits->count != 0U)
		memcpy(partition->splits, splits->values,
			(size_t)splits->count * sizeof(*partition->splits));
	if (pairs->count != 0U)
		memcpy(partition->response_pairs, pairs->values,
			(size_t)pairs->count * sizeof(*partition->response_pairs));
	if (candidates->count != 0U)
		memcpy(partition->candidate_groups, candidates->values,
			(size_t)candidates->count * sizeof(*partition->candidate_groups));
	if (source_endpoints->group_count != 0U)
		memcpy(partition->source_endpoint_groups, source_endpoints->groups,
			(size_t)source_endpoints->group_count *
				sizeof(*partition->source_endpoint_groups));
	if (source_endpoints->member_count != 0U)
		memcpy(partition->source_endpoint_members, source_endpoints->members,
			(size_t)source_endpoints->member_count *
				sizeof(*partition->source_endpoint_members));
	if (target_endpoints->group_count != 0U)
		memcpy(partition->target_endpoint_groups, target_endpoints->groups,
			(size_t)target_endpoints->group_count *
				sizeof(*partition->target_endpoint_groups));
	if (target_endpoints->member_count != 0U)
		memcpy(partition->target_endpoint_members, target_endpoints->members,
			(size_t)target_endpoints->member_count *
				sizeof(*partition->target_endpoint_members));
	if (occluders->count != 0U)
		memcpy(partition->static_occluders, occluders->values,
			(size_t)occluders->count * sizeof(*partition->static_occluders));
	if (occluder_sides->count != 0U)
		memcpy(partition->static_occluder_sides, occluder_sides->values,
			(size_t)occluder_sides->count *
				sizeof(*partition->static_occluder_sides));
	if (occluder_edges->count != 0U)
		memcpy(partition->static_occluder_edges, occluder_edges->values,
			(size_t)occluder_edges->count *
				sizeof(*partition->static_occluder_edges));
	partition->seal.version = SG_RUNE_COMPACT_RESPONSE_PARTITION_VERSION;
	partition->seal.flags = SG_RUNE_COMPACT_RESPONSE_SEAL_REQUIRED;
	if (unresolved_count == 0U && candidates->count == 0U)
		partition->seal.flags |=
			SG_RUNE_COMPACT_RESPONSE_SEAL_CONSTANT_RESPONSE_PAIRS;
	partition->seal.split_frontier_count = 0U;
	partition->seal.source_fragment_count = partition->source_fragment_count;
	partition->seal.target_patch_count = partition->target_patch_count;
	partition->seal.split_count = partition->split_count;
	partition->seal.response_pair_count = partition->response_pair_count;
	partition->seal.certified_direct_pair_count = direct_count;
	partition->seal.certified_static_impact_pair_count = impact_count;
	partition->seal.unresolved_response_pair_count = unresolved_count;
	partition->seal.unresolved_candidate_group_count = candidates->count;
	partition->seal.source_endpoint_group_count = source_endpoints->group_count;
	partition->seal.target_endpoint_group_count = target_endpoints->group_count;
	partition->seal.source_endpoint_member_count = source_endpoints->member_count;
	partition->seal.target_endpoint_member_count = target_endpoints->member_count;
	partition->seal.static_occluder_count = owner->visibility->occluder_count;
	partition->seal.compact_facet_count = geometry->facet_count;
	partition->seal.compact_cell_count = geometry->cell_count;
	partition->seal.compact_source_surface_count =
		geometry->source_surface_count;
	partition->seal.compact_source_surface_vertex_count =
		geometry->source_surface_vertex_count;
	partition->seal.source_surface_catalog_seal =
		SG_RuneCompactSourceSurfaceCatalogSeal(
			partition->compact_source_surfaces,
			partition->compact_source_surface_count,
			partition->compact_source_surface_vertices,
			partition->compact_source_surface_vertex_count);
	partition->state = RESPONSE_STATE;
	partition->state_inverse = ~RESPONSE_STATE;
	partition->self = partition;
	partition->reference_count = 1U;
	*partition_out = partition;
	return 1;

failure:
	ReleaseResultArrays(partition);
	Release(context, partition);
	return 0;
}

static int OwnerSourcesValid(
	const sg_rune_compact_builder_owner_view_t *owner,
	const sg_rune_compact_geometry_view_t *geometry)
{
	if (owner == NULL || geometry == NULL || owner->world == NULL ||
		owner->collision == NULL || owner->configuration == NULL ||
		owner->semantics == NULL || owner->visibility == NULL ||
		!SG_RuneCompactIdentityMatches(&owner->identity, &geometry->identity) ||
		owner->collision->world != owner->world ||
		geometry->cell_count == 0U || geometry->cells == NULL ||
		geometry->facet_count == 0U || geometry->facets == NULL ||
		geometry->incidence_count == 0U || geometry->incidences == NULL ||
		geometry->source_surface_count == 0U ||
		geometry->source_surfaces == NULL ||
		geometry->source_surface_vertex_count == 0U ||
		geometry->source_surface_vertices == NULL ||
		geometry->cell_incidence_count == 0U ||
		geometry->cell_incidences == NULL ||
		geometry->compact_cells_for_configuration_cell == NULL ||
		geometry->configuration_cell_compact_cells == NULL)
		return 0;
	if ((owner->world->visibility.cluster_count != 0U &&
		 owner->world->visibility.bit_offsets == NULL) ||
		(owner->world->visibility.byte_count != 0U &&
		 owner->world->visibility.bytes == NULL) ||
		(owner->visibility->area_count != 0U &&
		 owner->visibility->area_components == NULL) ||
		(owner->visibility->occluder_count != 0U &&
		 owner->visibility->occluders == NULL) ||
		(owner->world->model_count != 0U && owner->world->models == NULL) ||
		(owner->world->plane_count != 0U && owner->world->planes == NULL) ||
		(owner->world->brush_count != 0U && owner->world->brushes == NULL) ||
		(owner->world->brush_side_count != 0U &&
		 owner->world->brush_sides == NULL))
		return 0;
	if (owner->identity.source_counts.model_count != owner->world->model_count ||
		owner->identity.source_counts.leaf_count != owner->world->leaf_count ||
		owner->identity.source_counts.area_count != owner->world->area_count ||
		owner->identity.source_counts.plane_count != owner->world->plane_count ||
		owner->identity.source_counts.brush_count != owner->world->brush_count ||
		owner->identity.source_counts.brush_side_count !=
			owner->world->brush_side_count)
		return 0;
	return 1;
}

int SG_RuneCompactResponsePartitionOwnerBuild(
	const sg_rune_compact_builder_owner_view_t *owner,
	const sg_rune_compact_geometry_view_t *geometry,
	const sg_rune_compact_response_allocator_t *allocator,
	sg_rune_compact_response_partition_t **partition_out,
	sg_rune_compact_response_error_t *error_out)
{
	response_context_t context;
	response_fragment_vector_t fragments;
	response_patch_vector_t patches;
	response_split_vector_t splits;
	response_occluder_vector_t occluders;
	response_occluder_side_vector_t occluder_sides;
	response_occluder_edge_vector_t occluder_edges;
	response_pair_vector_t pairs;
	response_endpoint_index_t source_endpoints;
	response_endpoint_index_t target_endpoints;
	response_candidate_vector_t candidates;
	response_probe_vector_t probes;
	response_refinement_vector_t refinements;
	sg_rune_compact_response_allocator_t selected;
	int success = 0;

	ClearError(error_out);
	if (partition_out == NULL || *partition_out != NULL ||
		!SelectAllocator(allocator, &selected))
	{
		if (error_out != NULL)
		{
			error_out->code = SG_RUNE_COMPACT_RESPONSE_ERROR_INVALID_ARGUMENT;
			error_out->domain = SG_RUNE_COMPACT_RESPONSE_RECORD_RESULT;
			error_out->record = SG_RUNE_COMPACT_RESPONSE_INDEX_NONE;
		}
		return 0;
	}
	memset(&context, 0, sizeof(context));
	context.allocator = selected;
	context.error = error_out;
	memset(&fragments, 0, sizeof(fragments));
	memset(&patches, 0, sizeof(patches));
	memset(&splits, 0, sizeof(splits));
	memset(&occluders, 0, sizeof(occluders));
	memset(&occluder_sides, 0, sizeof(occluder_sides));
	memset(&occluder_edges, 0, sizeof(occluder_edges));
	memset(&pairs, 0, sizeof(pairs));
	memset(&source_endpoints, 0, sizeof(source_endpoints));
	memset(&target_endpoints, 0, sizeof(target_endpoints));
	memset(&candidates, 0, sizeof(candidates));
	memset(&probes, 0, sizeof(probes));
	memset(&refinements, 0, sizeof(refinements));
	if (!OwnerSourcesValid(owner, geometry))
	{
		SetError(&context, SG_RUNE_COMPACT_RESPONSE_ERROR_INVALID_SOURCE,
			SG_RUNE_COMPACT_RESPONSE_RECORD_RESULT,
			SG_RUNE_COMPACT_RESPONSE_INDEX_NONE);
		goto done;
	}
	if (!BuildBaseFragments(&context, owner, geometry, &fragments) ||
		!BuildTargetPatches(&context, owner, geometry, &patches))
		goto construction_failure;
	SortPatches(&patches);
	if (!BindTargetPatches(owner, geometry, &patches) ||
		!AddOccluderSplits(&context, owner, &occluders,
			&occluder_sides, &occluder_edges))
		goto construction_failure;
	goto construction_complete;

construction_failure:
	{
		if (error_out == NULL ||
			error_out->code == SG_RUNE_COMPACT_RESPONSE_ERROR_NONE)
			SetError(&context, SG_RUNE_COMPACT_RESPONSE_ERROR_INVALID_GEOMETRY,
				SG_RUNE_COMPACT_RESPONSE_RECORD_RESULT,
				SG_RUNE_COMPACT_RESPONSE_INDEX_NONE);
		goto done;
	}

construction_complete:
	SortSplits(&splits);
	DeduplicateSplits(&splits);
	if (!ApplySplits(&context, owner->world, &patches, &splits, &fragments) ||
		!ApplyPatchSplits(&context, &splits, &patches) ||
		!RefinementTerminal(owner->world, &fragments, &patches, &splits))
		goto refinement_failure;
	SortFragments(&fragments);
	SortPatches(&patches);
	if (!BindTargetPatches(owner, geometry, &patches) ||
		!BuildSourceEndpointIndex(&context, &fragments, &source_endpoints) ||
		!BuildTargetEndpointIndex(&context, &patches, &target_endpoints) ||
		!BuildCandidateGroups(&context, owner, &source_endpoints,
			&target_endpoints, &candidates) ||
		!BuildResponseProbes(&context, owner, &fragments, &patches,
			&occluder_sides, &source_endpoints, &target_endpoints, &candidates,
			&probes) ||
		!AddProbeDrivenSplits(&context, &patches, &occluder_sides,
			&occluder_edges, &probes, &splits))
		goto refinement_failure;
	SortSplits(&splits);
	DeduplicateSplits(&splits);
	if (!BuildResponseRefinements(&context, &fragments, &patches, &splits,
		&occluder_edges, &probes, &refinements))
		goto refinement_failure;
	EndpointIndexRelease(&context, &source_endpoints);
	EndpointIndexRelease(&context, &target_endpoints);
	Release(&context, candidates.values);
	memset(&candidates, 0, sizeof(candidates));
	ProbesRelease(&context, &probes);
	if (!ApplyResponseRefinements(&context, &refinements, &splits,
		&fragments))
		goto refinement_failure;
	RefinementsRelease(&context, &refinements);
	SortFragments(&fragments);
	if (!BuildSourceEndpointIndex(&context, &fragments, &source_endpoints) ||
		!BuildTargetEndpointIndex(&context, &patches, &target_endpoints) ||
		!BuildCandidateGroups(&context, owner, &source_endpoints,
			&target_endpoints, &candidates) ||
		!BuildCertifiedPairs(&context, owner, &fragments, &patches, &splits,
			&occluder_sides, &source_endpoints, &target_endpoints, &candidates,
			&pairs) ||
		!FlattenResult(&context, &owner->identity, owner, geometry, &fragments,
			&patches, &splits, &occluders, &occluder_sides, &occluder_edges,
			&pairs, &source_endpoints, &target_endpoints, &candidates,
			partition_out))
		goto refinement_failure;
	goto refinement_complete;

refinement_failure:
	{
		if (error_out == NULL ||
			error_out->code == SG_RUNE_COMPACT_RESPONSE_ERROR_NONE)
			SetError(&context, SG_RUNE_COMPACT_RESPONSE_ERROR_PARTITION,
				SG_RUNE_COMPACT_RESPONSE_RECORD_RESULT,
				SG_RUNE_COMPACT_RESPONSE_INDEX_NONE);
		goto done;
	}

refinement_complete:
	success = 1;

done:
	FragmentsRelease(&context, &fragments);
	PatchesRelease(&context, &patches);
	SplitsRelease(&context, &splits);
	OccluderAuthorityRelease(&context, &occluders, &occluder_sides,
		&occluder_edges);
	PairsRelease(&context, &pairs);
	RefinementsRelease(&context, &refinements);
	EndpointIndexRelease(&context, &source_endpoints);
	EndpointIndexRelease(&context, &target_endpoints);
	Release(&context, candidates.values);
	ProbesRelease(&context, &probes);
	return success;
}

int SG_RuneCompactResponsePartitionBuild(
	const sg_rune_compact_builder_t *builder,
	const sg_rune_compact_geometry_t *geometry,
	const sg_rune_compact_response_allocator_t *allocator,
	sg_rune_compact_response_partition_t **partition_out,
	sg_rune_compact_response_error_t *error_out)
{
	sg_rune_compact_builder_owner_view_t owner;
	sg_rune_compact_geometry_view_t geometry_view;

	if (builder == NULL || geometry == NULL || partition_out == NULL)
	{
		ClearError(error_out);
		if (error_out != NULL)
			error_out->code = SG_RUNE_COMPACT_RESPONSE_ERROR_INVALID_ARGUMENT;
		return 0;
	}
	if (!SG_RuneCompactBuilderOwnerRead(builder, &owner))
	{
		ClearError(error_out);
		if (error_out != NULL)
			error_out->code = SG_RUNE_COMPACT_RESPONSE_ERROR_BUILDER_READ;
		return 0;
	}
	if (!SG_RuneCompactGeometryRead(geometry, &geometry_view))
	{
		ClearError(error_out);
		if (error_out != NULL)
			error_out->code = SG_RUNE_COMPACT_RESPONSE_ERROR_GEOMETRY_READ;
		return 0;
	}
	return SG_RuneCompactResponsePartitionOwnerBuild(&owner, &geometry_view,
		allocator, partition_out, error_out);
}

int SG_RuneCompactResponsePartitionRead(
	const sg_rune_compact_response_partition_t *partition,
	sg_rune_compact_response_partition_view_t *view_out)
{
	if (partition == NULL || view_out == NULL ||
		partition->state != RESPONSE_STATE ||
		partition->state_inverse != ~RESPONSE_STATE ||
		partition->self != partition)
		return 0;
	memset(view_out, 0, sizeof(*view_out));
	view_out->identity = partition->identity;
	view_out->source_fragments = partition->source_fragments;
	view_out->source_fragment_count = partition->source_fragment_count;
	view_out->source_halfspaces = partition->source_halfspaces;
	view_out->source_halfspace_count = partition->source_halfspace_count;
	view_out->target_patches = partition->target_patches;
	view_out->target_patch_count = partition->target_patch_count;
	view_out->target_vertices = partition->target_vertices;
	view_out->target_vertex_count = partition->target_vertex_count;
	view_out->splits = partition->splits;
	view_out->split_count = partition->split_count;
	view_out->response_pairs = partition->response_pairs;
	view_out->response_pair_count = partition->response_pair_count;
	view_out->candidate_groups = partition->candidate_groups;
	view_out->candidate_group_count = partition->candidate_group_count;
	view_out->source_endpoint_groups = partition->source_endpoint_groups;
	view_out->source_endpoint_group_count =
		partition->source_endpoint_group_count;
	view_out->source_endpoint_members = partition->source_endpoint_members;
	view_out->source_endpoint_member_count =
		partition->source_endpoint_member_count;
	view_out->target_endpoint_groups = partition->target_endpoint_groups;
	view_out->target_endpoint_group_count =
		partition->target_endpoint_group_count;
	view_out->target_endpoint_members = partition->target_endpoint_members;
	view_out->target_endpoint_member_count =
		partition->target_endpoint_member_count;
	view_out->static_occluder_count = partition->static_occluder_count;
	view_out->static_occluders = partition->static_occluders;
	view_out->static_occluder_sides = partition->static_occluder_sides;
	view_out->static_occluder_side_count =
		partition->static_occluder_side_count;
	view_out->static_occluder_edges = partition->static_occluder_edges;
	view_out->static_occluder_edge_count =
		partition->static_occluder_edge_count;
	view_out->compact_facet_count = partition->compact_facet_count;
	view_out->compact_facets = partition->compact_facets;
	view_out->compact_cell_count = partition->compact_cell_count;
	view_out->compact_source_surface_count =
		partition->compact_source_surface_count;
	view_out->compact_source_surfaces = partition->compact_source_surfaces;
	view_out->compact_source_surface_vertices =
		partition->compact_source_surface_vertices;
	view_out->compact_source_surface_vertex_count =
		partition->compact_source_surface_vertex_count;
	view_out->bsp_visibility_bit_offsets =
		partition->bsp_visibility_bit_offsets;
	view_out->bsp_visibility_cluster_count =
		partition->bsp_visibility_cluster_count;
	view_out->bsp_visibility_bytes = partition->bsp_visibility_bytes;
	view_out->bsp_visibility_byte_count = partition->bsp_visibility_byte_count;
	view_out->area_components = partition->area_components;
	view_out->area_component_count = partition->area_component_count;
	view_out->seal = partition->seal;
	return 1;
}

int SG_RuneCompactResponsePartitionRetain(
	sg_rune_compact_response_partition_t *partition)
{
	if (partition == NULL || partition->state != RESPONSE_STATE ||
		partition->state_inverse != ~RESPONSE_STATE ||
		partition->self != partition || partition->reference_count == 0U ||
		partition->reference_count == UINT32_MAX)
		return 0;
	partition->reference_count++;
	return 1;
}

static int SpanWithin(uint32_t first, uint32_t count, uint32_t total)
{
	return first <= total && count <= total - first;
}

static int PublishedPairCompare(
	const sg_rune_compact_response_partition_view_t *view,
	const sg_rune_compact_response_pair_t *left,
	const sg_rune_compact_response_pair_t *right)
{
	const sg_rune_compact_response_fragment_t *left_source =
		&view->source_fragments[left->source_fragment];
	const sg_rune_compact_response_fragment_t *right_source =
		&view->source_fragments[right->source_fragment];
	const sg_rune_compact_response_patch_t *left_target =
		&view->target_patches[left->target_patch];
	const sg_rune_compact_response_patch_t *right_target =
		&view->target_patches[right->target_patch];

#define RESPONSE_PUBLISHED_COMPARE(a, b) \
	do { if ((a) != (b)) return (a) < (b) ? -1 : 1; } while (0)
	RESPONSE_PUBLISHED_COMPARE(left_source->parent_cell.value,
		right_source->parent_cell.value);
	RESPONSE_PUBLISHED_COMPARE(left_target->target_cell.value,
		right_target->target_cell.value);
	RESPONSE_PUBLISHED_COMPARE(left_source->static_partition_id,
		right_source->static_partition_id);
	RESPONSE_PUBLISHED_COMPARE(left_target->static_partition_id,
		right_target->static_partition_id);
	RESPONSE_PUBLISHED_COMPARE(left_source->configuration_region,
		right_source->configuration_region);
	RESPONSE_PUBLISHED_COMPARE(left_source->configuration_cell,
		right_source->configuration_cell);
	RESPONSE_PUBLISHED_COMPARE(left_target->configuration_region,
		right_target->configuration_region);
	RESPONSE_PUBLISHED_COMPARE(left_target->configuration_cell,
		right_target->configuration_cell);
	RESPONSE_PUBLISHED_COMPARE(left_source->bsp_leaf, right_source->bsp_leaf);
	RESPONSE_PUBLISHED_COMPARE(left_source->bsp_area, right_source->bsp_area);
	RESPONSE_PUBLISHED_COMPARE(left_source->bsp_cluster,
		right_source->bsp_cluster);
	RESPONSE_PUBLISHED_COMPARE(left_target->bsp_leaf, right_target->bsp_leaf);
	RESPONSE_PUBLISHED_COMPARE(left_target->bsp_area, right_target->bsp_area);
	RESPONSE_PUBLISHED_COMPARE(left_target->bsp_cluster,
		right_target->bsp_cluster);
	RESPONSE_PUBLISHED_COMPARE(left_source->boundary_incidences.first,
		right_source->boundary_incidences.first);
	RESPONSE_PUBLISHED_COMPARE(left_source->boundary_incidences.count,
		right_source->boundary_incidences.count);
	RESPONSE_PUBLISHED_COMPARE(left_target->boundary_incidences.first,
		right_target->boundary_incidences.first);
	RESPONSE_PUBLISHED_COMPARE(left_target->boundary_incidences.count,
		right_target->boundary_incidences.count);
	RESPONSE_PUBLISHED_COMPARE(left->source_fragment, right->source_fragment);
	RESPONSE_PUBLISHED_COMPARE(left->target_patch, right->target_patch);
#undef RESPONSE_PUBLISHED_COMPARE
	return 0;
}

static int EndpointGroupsValid(
	const sg_rune_compact_response_partition_view_t *view, int source)
{
	const sg_rune_compact_response_endpoint_group_t *groups = source ?
		view->source_endpoint_groups : view->target_endpoint_groups;
	const uint32_t group_count = source ? view->source_endpoint_group_count :
		view->target_endpoint_group_count;
	const uint32_t *members = source ? view->source_endpoint_members :
		view->target_endpoint_members;
	const uint32_t member_count = source ? view->source_endpoint_member_count :
		view->target_endpoint_member_count;
	uint32_t cursor = 0U;
	uint32_t group;

	for (group = 0U; group < group_count; group++)
	{
		const sg_rune_compact_response_endpoint_group_t *record =
			&groups[group];
		uint32_t ordinal;

		if (record->first_member != cursor || record->member_count == 0U ||
			(record->flags & ~UINT32_C(1)) != 0U ||
			(source && record->flags != 0U) ||
			!SpanWithin(record->first_member, record->member_count, member_count) ||
			(group != 0U && EndpointGroupCompare(&groups[group - 1U], record) >= 0))
			return 0;
		for (ordinal = 0U; ordinal < record->member_count; ordinal++)
		{
			const uint32_t member = members[record->first_member + ordinal];

			if (ordinal != 0U &&
				members[record->first_member + ordinal - 1U] >= member)
				return 0;
			if (source)
			{
				if (member >= view->source_fragment_count ||
					view->source_fragments[member].bsp_cluster !=
						record->bsp_cluster ||
					view->source_fragments[member].bsp_area != record->bsp_area)
					return 0;
			}
			else if (member >= view->target_patch_count ||
				view->target_patches[member].bsp_cluster != record->bsp_cluster ||
				view->target_patches[member].bsp_area != record->bsp_area ||
				(view->target_patches[member].flags &
					SG_RUNE_COMPACT_RESPONSE_PATCH_SKY) != 0U ||
				(((view->target_patches[member].flags &
					SG_RUNE_COMPACT_RESPONSE_PATCH_MOVING) != 0U) !=
				 ((record->flags &
					SG_RUNE_COMPACT_RESPONSE_ENDPOINT_MOVING) != 0U)))
				return 0;
		}
		cursor += record->member_count;
	}
	return cursor == member_count;
}

static int PublishedPvsAllows(
	const sg_rune_compact_response_partition_view_t *view,
	uint32_t source_cluster, uint32_t target_cluster)
{
	uint32_t input;
	uint32_t output = 0U;
	uint32_t target_byte;
	uint8_t value = 0U;

	if (source_cluster == SG_RUNE_COMPACT_RESPONSE_INDEX_NONE ||
		target_cluster == SG_RUNE_COMPACT_RESPONSE_INDEX_NONE ||
		view->bsp_visibility_byte_count == 0U)
		return 1;
	if (source_cluster >= view->bsp_visibility_cluster_count ||
		target_cluster >= view->bsp_visibility_cluster_count ||
		view->bsp_visibility_bit_offsets == NULL ||
		view->bsp_visibility_bytes == NULL)
		return 0;
	target_byte = target_cluster >> 3;
	input = view->bsp_visibility_bit_offsets[source_cluster].value[0];
	while (output <= target_byte)
	{
		uint8_t run;

		if (input >= view->bsp_visibility_byte_count)
			return 0;
		value = view->bsp_visibility_bytes[input++];
		if (value != 0U)
		{
			if (output++ == target_byte)
				break;
			continue;
		}
		if (input >= view->bsp_visibility_byte_count)
			return 0;
		run = view->bsp_visibility_bytes[input++];
		if (run == 0U || output > UINT32_MAX - (uint32_t)run)
			return 0;
		if (target_byte < output + (uint32_t)run)
			return 0;
		output += (uint32_t)run;
	}
	return (value & (uint8_t)(UINT32_C(1) <<
		(target_cluster & UINT32_C(7)))) != 0U;
}

static int CandidateGroupsValid(
	const sg_rune_compact_response_partition_view_t *view)
{
	uint32_t index = 0U;
	uint32_t source_group;

	for (source_group = 0U; source_group < view->source_endpoint_group_count;
		source_group++)
	{
		uint32_t target_group;

		for (target_group = 0U;
			target_group < view->target_endpoint_group_count; target_group++)
		{
			const sg_rune_compact_response_endpoint_group_t *source =
				&view->source_endpoint_groups[source_group];
			const sg_rune_compact_response_endpoint_group_t *target =
				&view->target_endpoint_groups[target_group];
			const sg_rune_compact_response_candidate_group_t *candidate;
			uint32_t expected_area_state;

			if (!PublishedPvsAllows(view, source->bsp_cluster,
				target->bsp_cluster))
				continue;
			if (source->bsp_area != SG_RUNE_COMPACT_RESPONSE_INDEX_NONE &&
				target->bsp_area != SG_RUNE_COMPACT_RESPONSE_INDEX_NONE &&
				(source->bsp_area >= view->area_component_count ||
				 target->bsp_area >= view->area_component_count ||
				 view->area_components == NULL ||
				 view->area_components[source->bsp_area] !=
					view->area_components[target->bsp_area]))
				continue;
			if (index >= view->candidate_group_count)
				return 0;
			candidate = &view->candidate_groups[index];
			if (candidate->source_group != source_group ||
				candidate->target_group != target_group ||
			candidate->classification !=
				SG_RUNE_COMPACT_STATIC_VISIBILITY_CONDITIONAL ||
			candidate->requires_exact_ray != 1U ||
			candidate->reserved[0] != 0U ||
			candidate->reserved[1] != 0U ||
			(candidate->relation_flags &
				~(sg_rune_compact_static_relation_flags_t)
					SG_RUNE_COMPACT_STATIC_RELATION_PORTAL_CROSSING) != 0U)
				return 0;
			if (candidate->reason !=
				((target->flags &
					SG_RUNE_COMPACT_RESPONSE_ENDPOINT_MOVING) != 0U ?
					SG_RUNE_COMPACT_STATIC_VISIBILITY_REASON_MOVING_SUBMODEL :
					SG_RUNE_COMPACT_STATIC_VISIBILITY_REASON_EXACT_RAY_REQUIRED))
				return 0;
			expected_area_state = source->bsp_area !=
				SG_RUNE_COMPACT_RESPONSE_INDEX_NONE &&
				target->bsp_area != SG_RUNE_COMPACT_RESPONSE_INDEX_NONE &&
				source->bsp_area != target->bsp_area;
			if (candidate->requires_area_state != expected_area_state ||
				((candidate->relation_flags &
					SG_RUNE_COMPACT_STATIC_RELATION_PORTAL_CROSSING) != 0U) !=
				(expected_area_state != 0U))
				return 0;
			index++;
		}
	}
	return index == view->candidate_group_count;
}

static const sg_rune_compact_response_candidate_group_t *CandidateForEndpoints(
	const sg_rune_compact_response_partition_view_t *view,
	uint32_t source_fragment, uint32_t target_patch)
{
	const uint32_t source_group = EndpointGroupForMember(
		view->source_endpoint_groups, view->source_endpoint_group_count,
		view->source_endpoint_members, source_fragment);
	const uint32_t target_group = EndpointGroupForMember(
		view->target_endpoint_groups, view->target_endpoint_group_count,
		view->target_endpoint_members, target_patch);
	uint32_t candidate;

	if (source_group == SG_RUNE_COMPACT_RESPONSE_INDEX_NONE ||
		target_group == SG_RUNE_COMPACT_RESPONSE_INDEX_NONE)
		return NULL;
	for (candidate = 0U; candidate < view->candidate_group_count; candidate++)
		if (view->candidate_groups[candidate].source_group == source_group &&
			view->candidate_groups[candidate].target_group == target_group)
			return &view->candidate_groups[candidate];
	return NULL;
}

static int PublishedPlaneValid(const sg_rune_binary32_plane_t *plane)
{
	double length_squared = 0.0;
	uint32_t axis;

	if (!isfinite(BitsFloat(plane->distance_bits)))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
	{
		const float normal = BitsFloat(plane->normal_bits[axis]);

		if (!isfinite(normal))
			return 0;
		length_squared += (double)normal * (double)normal;
	}
	return fabs(length_squared - 1.0) <= 0.0001;
}

static int PublishedPlaneCanonical(const sg_rune_binary32_plane_t *plane)
{
	uint32_t axis;

	if (!PublishedPlaneValid(plane))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (fabsf(BitsFloat(plane->normal_bits[axis])) > FLT_EPSILON)
			return BitsFloat(plane->normal_bits[axis]) > 0.0f;
	return 0;
}

static double PublishedPlaneResidual(const sg_rune_binary32_plane_t *plane,
	const sg_rune_q8_vec3_t *point)
{
	double residual = -(double)BitsFloat(plane->distance_bits);
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		residual += (double)BitsFloat(plane->normal_bits[axis]) *
			((double)point->value[axis] / 8.0);
	return residual;
}

static int PublishedFragmentCompare(
	const sg_rune_compact_response_partition_view_t *view,
	const sg_rune_compact_response_fragment_t *left,
	const sg_rune_compact_response_fragment_t *right)
{
	uint32_t halfspace;
	uint32_t axis;

#define RESPONSE_FRAGMENT_FIELD(a, b) \
	do { if ((a) != (b)) return (a) < (b) ? -1 : 1; } while (0)
	RESPONSE_FRAGMENT_FIELD(left->parent_cell.value, right->parent_cell.value);
	RESPONSE_FRAGMENT_FIELD(left->static_partition_id,
		right->static_partition_id);
	RESPONSE_FRAGMENT_FIELD(left->configuration_region,
		right->configuration_region);
	RESPONSE_FRAGMENT_FIELD(left->configuration_cell,
		right->configuration_cell);
	RESPONSE_FRAGMENT_FIELD(left->bsp_leaf, right->bsp_leaf);
	RESPONSE_FRAGMENT_FIELD(left->bsp_area, right->bsp_area);
	RESPONSE_FRAGMENT_FIELD(left->bsp_cluster, right->bsp_cluster);
	RESPONSE_FRAGMENT_FIELD(left->valid_stances, right->valid_stances);
	for (axis = 0U; axis < 3U; axis++)
	{
		RESPONSE_FRAGMENT_FIELD(left->bounds.mins.value[axis],
			right->bounds.mins.value[axis]);
		RESPONSE_FRAGMENT_FIELD(left->bounds.maxs.value[axis],
			right->bounds.maxs.value[axis]);
	}
	RESPONSE_FRAGMENT_FIELD(left->halfspace_count, right->halfspace_count);
	for (halfspace = 0U; halfspace < left->halfspace_count; halfspace++)
	{
		const sg_rune_compact_response_halfspace_t *left_halfspace =
			&view->source_halfspaces[left->first_halfspace + halfspace];
		const sg_rune_compact_response_halfspace_t *right_halfspace =
			&view->source_halfspaces[right->first_halfspace + halfspace];

		for (axis = 0U; axis < 3U; axis++)
			RESPONSE_FRAGMENT_FIELD(left_halfspace->plane.normal_bits[axis],
				right_halfspace->plane.normal_bits[axis]);
		RESPONSE_FRAGMENT_FIELD(left_halfspace->plane.distance_bits,
			right_halfspace->plane.distance_bits);
		RESPONSE_FRAGMENT_FIELD(left_halfspace->split, right_halfspace->split);
		RESPONSE_FRAGMENT_FIELD(left_halfspace->open, right_halfspace->open);
	}
#undef RESPONSE_FRAGMENT_FIELD
	return 0;
}

static int FragmentStorageValid(
	const sg_rune_compact_response_partition_view_t *view)
{
	uint32_t cursor = 0U;
	uint32_t fragment;

	for (fragment = 0U; fragment < view->source_fragment_count; fragment++)
	{
		const sg_rune_compact_response_fragment_t *record =
			&view->source_fragments[fragment];
		uint32_t axis;
		uint32_t halfspace;

		if (record->first_halfspace != cursor || record->halfspace_count == 0U ||
			record->reserved[0] != 0U || record->reserved[1] != 0U ||
			record->reserved[2] != 0U ||
			record->bsp_leaf >= view->identity.source_counts.leaf_count ||
			record->bsp_area >= view->area_component_count ||
			(view->bsp_visibility_byte_count != 0U &&
			 record->bsp_cluster != SG_RUNE_COMPACT_RESPONSE_INDEX_NONE &&
			 record->bsp_cluster >= view->bsp_visibility_cluster_count) ||
			!SpanWithin(record->first_halfspace, record->halfspace_count,
				view->source_halfspace_count))
			return 0;
		if (fragment != 0U && PublishedFragmentCompare(view,
			&view->source_fragments[fragment - 1U], record) >= 0)
			return 0;
		for (axis = 0U; axis < 3U; axis++)
			if (record->bounds.mins.value[axis] >
				record->bounds.maxs.value[axis] ||
				record->witness.value[axis] < record->bounds.mins.value[axis] ||
				record->witness.value[axis] > record->bounds.maxs.value[axis])
				return 0;
		for (halfspace = 0U; halfspace < record->halfspace_count; halfspace++)
		{
			const sg_rune_compact_response_halfspace_t *constraint =
				&view->source_halfspaces[record->first_halfspace + halfspace];

			if (!PublishedPlaneValid(&constraint->plane) ||
				constraint->open > 1U ||
				constraint->reserved[0] != 0U ||
				constraint->reserved[1] != 0U ||
				constraint->reserved[2] != 0U ||
				(constraint->split != SG_RUNE_COMPACT_RESPONSE_INDEX_NONE &&
				 constraint->split >= view->split_count) ||
				PublishedPlaneResidual(&constraint->plane, &record->witness) >
					0.126)
				return 0;
		}
		cursor += record->halfspace_count;
	}
	return cursor == view->source_halfspace_count;
}

static int SourceSurfaceTupleCompare(
	const sg_rune_compact_source_surface_t *left,
	const sg_rune_compact_source_surface_t *right)
{
	if (left->source.model != right->source.model)
		return left->source.model < right->source.model ? -1 : 1;
	if (left->source.brush != right->source.brush)
		return left->source.brush < right->source.brush ? -1 : 1;
	if (left->source.brush_side != right->source.brush_side)
		return left->source.brush_side < right->source.brush_side ? -1 : 1;
	if (left->source.plane != right->source.plane)
		return left->source.plane < right->source.plane ? -1 : 1;
	return 0;
}

static int SourceSurfaceCatalogValid(
	const sg_rune_compact_response_partition_view_t *view)
{
	uint32_t vertex_cursor = 0U;
	uint32_t index;

	if (view->compact_source_surface_count == 0U ||
		view->compact_source_surfaces == NULL ||
		view->compact_source_surface_vertex_count == 0U ||
		view->compact_source_surface_vertices == NULL ||
		(view->compact_facet_count != 0U && view->compact_facets == NULL) ||
		(view->bsp_visibility_cluster_count != 0U &&
		 view->bsp_visibility_bit_offsets == NULL) ||
		(view->bsp_visibility_byte_count != 0U &&
		 view->bsp_visibility_bytes == NULL) ||
		(view->area_component_count != 0U && view->area_components == NULL))
		return 0;
	for (index = 0U; index < view->compact_source_surface_count; index++)
	{
		const sg_rune_compact_source_surface_t *surface =
			&view->compact_source_surfaces[index];
		uint32_t vertex;

		if (surface->source.model >= view->identity.source_counts.model_count ||
			surface->source.brush >= view->identity.source_counts.brush_count ||
			surface->source.brush_side >=
				view->identity.source_counts.brush_side_count ||
			surface->source.plane >= view->identity.source_counts.plane_count ||
			surface->frame >= SG_RUNE_COMPACT_SOURCE_SURFACE_FRAME_COUNT ||
			surface->frame !=
				(surface->source.model == SG_HOST_COLLISION_MODEL_WORLD ?
					SG_RUNE_COMPACT_SOURCE_SURFACE_WORLD :
					SG_RUNE_COMPACT_SOURCE_SURFACE_MODEL_LOCAL) ||
			!PublishedPlaneValid(&surface->plane) ||
			surface->vertices.first != vertex_cursor ||
			surface->vertices.count < 3U ||
			!SpanWithin(surface->vertices.first, surface->vertices.count,
				view->compact_source_surface_vertex_count) ||
			(surface->cell.value != SG_RUNE_COMPACT_RESPONSE_INDEX_NONE &&
			 surface->cell.value >= view->compact_cell_count) ||
			(index != 0U && SourceSurfaceTupleCompare(
				&view->compact_source_surfaces[index - 1U], surface) >= 0))
			return 0;
		if (surface->parent_surface == SG_RUNE_COMPACT_RESPONSE_INDEX_NONE)
		{
			if (surface->cell.value != SG_RUNE_COMPACT_RESPONSE_INDEX_NONE ||
				surface->split_ordinal != 0U)
				return 0;
		}
		else
		{
			const sg_rune_compact_source_surface_t *parent;

			if (surface->parent_surface >= index)
				return 0;
			parent = &view->compact_source_surfaces[surface->parent_surface];
			if (SourceSurfaceTupleCompare(parent, surface) != 0 ||
				parent->frame != surface->frame ||
				memcmp(&parent->plane, &surface->plane,
					sizeof(surface->plane)) != 0)
				return 0;
		}
		for (vertex = 0U; vertex < surface->vertices.count; vertex++)
			if (fabs(PublishedPlaneResidual(&surface->plane,
				&view->compact_source_surface_vertices[
					surface->vertices.first + vertex])) > 0.126)
				return 0;
		vertex_cursor += surface->vertices.count;
	}
	return vertex_cursor == view->compact_source_surface_vertex_count;
}

static int PatchStorageValid(
	const sg_rune_compact_response_partition_view_t *view)
{
	uint32_t cursor = 0U;
	uint32_t patch;

	for (patch = 0U; patch < view->target_patch_count; patch++)
	{
		const sg_rune_compact_response_patch_t *record =
			&view->target_patches[patch];
		const sg_rune_compact_source_surface_t *source_surface;
		int32_t minimum[3] = { INT32_MAX, INT32_MAX, INT32_MAX };
		int32_t maximum[3] = { INT32_MIN, INT32_MIN, INT32_MIN };
		uint32_t vertex;
		uint32_t axis;

		if (record->first_vertex != cursor || record->vertex_count < 3U ||
			record->reserved[0] != 0U || record->reserved[1] != 0U ||
			record->reserved[2] != 0U ||
			!SpanWithin(record->first_vertex, record->vertex_count,
				view->target_vertex_count) || !PublishedPlaneValid(&record->plane) ||
			record->model >= view->identity.source_counts.model_count ||
			record->brush >= view->identity.source_counts.brush_count ||
			record->brush_side >= view->identity.source_counts.brush_side_count ||
			record->source_surface >= view->compact_source_surface_count ||
			record->source_frame >=
				SG_RUNE_COMPACT_SOURCE_SURFACE_FRAME_COUNT ||
			record->source_frame !=
				(record->model == SG_HOST_COLLISION_MODEL_WORLD ?
					SG_RUNE_COMPACT_SOURCE_SURFACE_WORLD :
					SG_RUNE_COMPACT_SOURCE_SURFACE_MODEL_LOCAL) ||
			(record->bsp_leaf == SG_RUNE_COMPACT_RESPONSE_INDEX_NONE &&
			 (record->bsp_area != SG_RUNE_COMPACT_RESPONSE_INDEX_NONE ||
			  record->bsp_cluster != SG_RUNE_COMPACT_RESPONSE_INDEX_NONE)) ||
			(record->bsp_leaf != SG_RUNE_COMPACT_RESPONSE_INDEX_NONE &&
			 (record->bsp_leaf >= view->identity.source_counts.leaf_count ||
			  record->bsp_area >= view->area_component_count ||
			  (view->bsp_visibility_byte_count != 0U &&
			   record->bsp_cluster != SG_RUNE_COMPACT_RESPONSE_INDEX_NONE &&
			   record->bsp_cluster >= view->bsp_visibility_cluster_count))))
			return 0;
		if (patch != 0U && PublishedPatchCompare(view,
			&view->target_patches[patch - 1U], record) >= 0)
			return 0;
		source_surface = &view->compact_source_surfaces[record->source_surface];
		if (source_surface->source.model != record->model ||
			source_surface->source.brush != record->brush ||
			source_surface->source.brush_side != record->brush_side ||
			source_surface->frame != record->source_frame ||
			memcmp(&source_surface->plane, &record->plane,
				sizeof(record->plane)) != 0)
			return 0;
		if (record->model == SG_HOST_COLLISION_MODEL_WORLD)
		{
			if ((record->parent_facet.value !=
					SG_RUNE_COMPACT_RESPONSE_INDEX_NONE &&
				 record->parent_facet.value >= view->compact_facet_count) ||
				(record->flags & SG_RUNE_COMPACT_RESPONSE_PATCH_MOVING) != 0U)
				return 0;
			if (record->parent_facet.value !=
				SG_RUNE_COMPACT_RESPONSE_INDEX_NONE)
			{
				const sg_rune_compact_facet_t *facet =
					&view->compact_facets[record->parent_facet.value];

				if (facet->source.kind !=
						SG_RUNE_COMPACT_SOURCE_EXPANDED_BRUSH_SIDE ||
					facet->source.value.brush_side.model != record->model ||
					facet->source.value.brush_side.brush != record->brush ||
					facet->source.value.brush_side.brush_side !=
						record->brush_side ||
					memcmp(&facet->plane, &record->plane,
						sizeof(record->plane)) != 0 ||
					facet->incidences.first !=
						record->boundary_incidences.first ||
					facet->incidences.count !=
						record->boundary_incidences.count)
					return 0;
			}
			else if (record->boundary_incidences.first != 0U ||
				record->boundary_incidences.count != 0U)
				return 0;
		}
		else if (record->parent_facet.value !=
			SG_RUNE_COMPACT_RESPONSE_INDEX_NONE ||
			(record->flags & SG_RUNE_COMPACT_RESPONSE_PATCH_MOVING) == 0U ||
			record->boundary_incidences.first != 0U ||
			record->boundary_incidences.count != 0U)
			return 0;
		for (vertex = 0U; vertex < record->vertex_count; vertex++)
		{
			const sg_rune_q8_vec3_t *point =
				&view->target_vertices[record->first_vertex + vertex];

			if (fabs(PublishedPlaneResidual(&record->plane, point)) > 0.126)
				return 0;
			for (axis = 0U; axis < 3U; axis++)
			{
				minimum[axis] = minimum[axis] < point->value[axis] ?
					minimum[axis] : point->value[axis];
				maximum[axis] = maximum[axis] > point->value[axis] ?
					maximum[axis] : point->value[axis];
			}
		}
		for (axis = 0U; axis < 3U; axis++)
			if (record->bounds.mins.value[axis] != minimum[axis] ||
				record->bounds.maxs.value[axis] != maximum[axis])
				return 0;
		cursor += record->vertex_count;
	}
	return cursor == view->target_vertex_count;
}

static int PublishedPatchCompare(
	const sg_rune_compact_response_partition_view_t *view,
	const sg_rune_compact_response_patch_t *left,
	const sg_rune_compact_response_patch_t *right)
{
	uint32_t vertex;
	uint32_t axis;

#define RESPONSE_PATCH_FIELD(a, b) \
	do { if ((a) != (b)) return (a) < (b) ? -1 : 1; } while (0)
	RESPONSE_PATCH_FIELD(left->model, right->model);
	RESPONSE_PATCH_FIELD(left->brush, right->brush);
	RESPONSE_PATCH_FIELD(left->brush_side, right->brush_side);
	RESPONSE_PATCH_FIELD(left->source_surface, right->source_surface);
	RESPONSE_PATCH_FIELD(left->source_frame, right->source_frame);
	RESPONSE_PATCH_FIELD(left->bsp_leaf, right->bsp_leaf);
	RESPONSE_PATCH_FIELD(left->visibility_surface_id,
		right->visibility_surface_id);
	RESPONSE_PATCH_FIELD(left->vertex_count, right->vertex_count);
	for (vertex = 0U; vertex < left->vertex_count; vertex++)
		for (axis = 0U; axis < 3U; axis++)
			RESPONSE_PATCH_FIELD(view->target_vertices[
				left->first_vertex + vertex].value[axis],
				view->target_vertices[
					right->first_vertex + vertex].value[axis]);
#undef RESPONSE_PATCH_FIELD
	return 0;
}

static int SurfaceForSplitValid(
	const sg_rune_compact_response_partition_view_t *view,
	uint64_t surface_id, uint32_t edge)
{
	uint32_t patch;

	for (patch = 0U; patch < view->target_patch_count; patch++)
		if (view->target_patches[patch].visibility_surface_id == surface_id &&
			edge < view->target_patches[patch].vertex_count)
			return 1;
	return 0;
}

static int Binary32PointValid(
	const sg_rune_compact_response_binary32_point_t *point)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		if (point->value_bits[axis] == UINT32_C(0x80000000) ||
			!isfinite(BitsFloat(point->value_bits[axis])))
			return 0;
	return 1;
}

static double Binary32PointPlaneResidual(
	const sg_rune_compact_response_binary32_point_t *point,
	const sg_rune_binary32_plane_t *plane)
{
	double residual = -(double)BitsFloat(plane->distance_bits);
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		residual += (double)BitsFloat(plane->normal_bits[axis]) *
			(double)BitsFloat(point->value_bits[axis]);
	return residual;
}

static double Binary32PointPlaneTolerance(
	const sg_rune_compact_response_binary32_point_t *point,
	const sg_rune_binary32_plane_t *plane)
{
	double scale = fabs((double)BitsFloat(plane->distance_bits)) + 1.0;
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		scale += fabs((double)BitsFloat(plane->normal_bits[axis]) *
			(double)BitsFloat(point->value_bits[axis]));
	return 128.0 * DBL_EPSILON * scale + 1.0e-5;
}

static int OccluderEdgeGeometryValid(
	const sg_rune_compact_response_partition_view_t *view,
	const sg_rune_compact_response_occluder_t *occluder,
	const sg_rune_compact_response_occluder_edge_t *edge)
{
	uint32_t from_boundary_count = 0U;
	uint32_t to_boundary_count = 0U;
	uint32_t shared_other_boundary_count = 0U;
	uint32_t side_index;

	if (edge->occluder >= view->static_occluder_count ||
		edge->side < occluder->first_side ||
		edge->side >= occluder->first_side + occluder->side_count ||
		!Binary32PointValid(&edge->from) ||
		!Binary32PointValid(&edge->to) ||
		Binary32PointCompare(&edge->from, &edge->to) >= 0)
		return 0;
	for (side_index = occluder->first_side;
		side_index < occluder->first_side + occluder->side_count; side_index++)
	{
		const sg_rune_binary32_plane_t *plane =
			&view->static_occluder_sides[side_index].halfspace_plane;
		const double from_residual =
			Binary32PointPlaneResidual(&edge->from, plane);
		const double to_residual =
			Binary32PointPlaneResidual(&edge->to, plane);
		const double from_tolerance =
			Binary32PointPlaneTolerance(&edge->from, plane);
		const double to_tolerance =
			Binary32PointPlaneTolerance(&edge->to, plane);
		const int from_boundary = fabs(from_residual) <= from_tolerance;
		const int to_boundary = fabs(to_residual) <= to_tolerance;

		if (from_residual > from_tolerance || to_residual > to_tolerance)
			return 0;
		if (side_index == edge->side && (!from_boundary || !to_boundary))
			return 0;
		from_boundary_count += from_boundary != 0;
		to_boundary_count += to_boundary != 0;
		if (side_index != edge->side && from_boundary && to_boundary)
			shared_other_boundary_count++;
	}
	return from_boundary_count >= 3U && to_boundary_count >= 3U &&
		shared_other_boundary_count != 0U;
}

static int OccluderAuthorityValid(
	const sg_rune_compact_response_partition_view_t *view)
{
	uint32_t side_cursor = 0U;
	uint32_t edge_cursor = 0U;
	uint32_t occluder_index;

	if ((view->static_occluder_count != 0U &&
		 view->static_occluders == NULL) ||
		(view->static_occluder_side_count != 0U &&
		 view->static_occluder_sides == NULL) ||
		(view->static_occluder_edge_count != 0U &&
		 view->static_occluder_edges == NULL))
		return 0;
	for (occluder_index = 0U; occluder_index < view->static_occluder_count;
		occluder_index++)
	{
		const sg_rune_compact_response_occluder_t *occluder =
			&view->static_occluders[occluder_index];
		uint32_t ordinal;

		if (occluder->model >= view->identity.source_counts.model_count ||
			occluder->brush >= view->identity.source_counts.brush_count ||
			occluder->conditional !=
				(occluder->model == SG_HOST_COLLISION_MODEL_WORLD ? 0U : 1U) ||
			(occluder_index != 0U &&
			 (occluder->model < view->static_occluders[occluder_index - 1U].model ||
			  (occluder->model ==
				view->static_occluders[occluder_index - 1U].model &&
			   occluder->brush <=
				view->static_occluders[occluder_index - 1U].brush))) ||
			occluder->first_side != side_cursor ||
			occluder->side_count < 4U ||
			!SpanWithin(occluder->first_side, occluder->side_count,
				view->static_occluder_side_count) ||
			occluder->first_edge != edge_cursor || occluder->edge_count == 0U ||
			!SpanWithin(occluder->first_edge, occluder->edge_count,
				view->static_occluder_edge_count))
			return 0;
		for (ordinal = 0U; ordinal < occluder->side_count; ordinal++)
		{
			const sg_rune_compact_response_occluder_side_t *side =
				&view->static_occluder_sides[occluder->first_side + ordinal];
			sg_configuration_plane_t canonical;
			sg_rune_binary32_plane_t compact;

			if (side->occluder != occluder_index ||
				side->model != occluder->model ||
				side->brush != occluder->brush ||
				side->contents != occluder->contents ||
				side->conditional != occluder->conditional ||
				side->brush_side >=
					view->identity.source_counts.brush_side_count ||
				side->bsp_plane >= view->identity.source_counts.plane_count ||
				!PublishedPlaneValid(&side->halfspace_plane) ||
				!PublishedPlaneCanonical(&side->plane) ||
				(ordinal != 0U && side->brush_side !=
					view->static_occluder_sides[
						occluder->first_side + ordinal - 1U].brush_side + 1U))
				return 0;
			CompactPlaneToConfiguration(&side->halfspace_plane, &canonical);
			CanonicalizePlane(&canonical);
			PlaneToCompact(&canonical, &compact);
			if (memcmp(&compact, &side->plane, sizeof(compact)) != 0)
				return 0;
		}
		for (ordinal = 0U; ordinal < occluder->edge_count; ordinal++)
		{
			const sg_rune_compact_response_occluder_edge_t *edge =
				&view->static_occluder_edges[occluder->first_edge + ordinal];
			const sg_rune_compact_response_occluder_edge_t *previous =
				ordinal == 0U ? NULL :
				&view->static_occluder_edges[
					occluder->first_edge + ordinal - 1U];

			if (edge->occluder != occluder_index ||
				(ordinal == 0U && edge->ordinal != 0U) ||
				(previous != NULL && edge->side == previous->side &&
				 (previous->ordinal == UINT32_MAX ||
				  edge->ordinal != previous->ordinal + 1U)) ||
				(previous != NULL && edge->side != previous->side &&
				 edge->ordinal != 0U) ||
				!OccluderEdgeGeometryValid(view, occluder, edge))
				return 0;
		}
		side_cursor += occluder->side_count;
		edge_cursor += occluder->edge_count;
	}
	return side_cursor == view->static_occluder_side_count &&
		edge_cursor == view->static_occluder_edge_count;
}

static int OccluderEdgeSplitPlane(
	const sg_rune_compact_response_partition_view_t *view, uint32_t edge_index,
	sg_rune_binary32_plane_t *plane_out)
{
	const sg_rune_compact_response_occluder_edge_t *edge;
	const sg_rune_compact_response_occluder_side_t *side;
	sg_rune_vec3_t from;
	sg_rune_vec3_t to;
	float normal[3];
	sg_configuration_plane_t plane;
	uint32_t axis;

	if (edge_index >= view->static_occluder_edge_count)
		return 0;
	edge = &view->static_occluder_edges[edge_index];
	if (edge->side >= view->static_occluder_side_count)
		return 0;
	side = &view->static_occluder_sides[edge->side];
	for (axis = 0U; axis < 3U; axis++)
	{
		from.value[axis] = BitsFloat(edge->from.value_bits[axis]);
		to.value[axis] = BitsFloat(edge->to.value_bits[axis]);
		normal[axis] = BitsFloat(side->plane.normal_bits[axis]);
	}
	EdgePlane(&from, &to, normal, &plane);
	if (!NormalizePlane(&plane, &plane))
		return 0;
	CanonicalizePlane(&plane);
	PlaneToCompact(&plane, plane_out);
	return 1;
}

static int FirstHitTiePlane(
	const sg_rune_compact_response_patch_t *patch,
	const sg_rune_compact_response_occluder_side_t *side,
	sg_rune_binary32_plane_t *plane_out)
{
	sg_configuration_plane_t target_plane;
	sg_configuration_plane_t occluder_plane;
	sg_configuration_plane_t tie;
	uint32_t axis;

	CompactPlaneToConfiguration(&patch->plane, &target_plane);
	CompactPlaneToConfiguration(&side->halfspace_plane, &occluder_plane);
	memset(&tie, 0, sizeof(tie));
	for (axis = 0U; axis < 3U; axis++)
		tie.normal[axis] = target_plane.normal[axis] -
			occluder_plane.normal[axis];
	tie.distance = target_plane.distance - occluder_plane.distance;
	if (!NormalizePlane(&tie, &tie))
		return 0;
	CanonicalizePlane(&tie);
	PlaneToCompact(&tie, plane_out);
	return 1;
}

static int FirstHitTieSplitValid(
	const sg_rune_compact_response_partition_view_t *view,
	const sg_rune_compact_response_split_t *split)
{
	const sg_rune_compact_response_occluder_side_t *side;
	uint32_t patch_index;

	if (split->edge >= view->static_occluder_side_count)
		return 0;
	side = &view->static_occluder_sides[split->edge];
	if (side->occluder != split->occluder)
		return 0;
	for (patch_index = 0U; patch_index < view->target_patch_count;
		patch_index++)
	{
		const sg_rune_compact_response_patch_t *patch =
			&view->target_patches[patch_index];
		sg_rune_binary32_plane_t compact;

		if (patch->visibility_surface_id != split->target_surface_id)
			continue;
		if (FirstHitTiePlane(patch, side, &compact) &&
			memcmp(&compact, &split->plane, sizeof(compact)) == 0)
			return 1;
	}
	return 0;
}

static int SplitStorageValid(
	const sg_rune_compact_response_partition_view_t *view)
{
	uint32_t split;

	for (split = 0U; split < view->split_count; split++)
	{
		const sg_rune_compact_response_split_t *record = &view->splits[split];

		if (!PublishedPlaneCanonical(&record->plane) ||
			record->kind >= SG_RUNE_COMPACT_RESPONSE_SPLIT_KIND_COUNT ||
			(split != 0U && SplitCompare(&view->splits[split - 1U], record) >= 0))
			return 0;
		switch (record->kind)
		{
		case SG_RUNE_COMPACT_RESPONSE_SPLIT_TARGET_EDGE:
			if (record->target_surface_id == UINT64_MAX ||
				record->occluder != SG_RUNE_COMPACT_RESPONSE_INDEX_NONE ||
				record->brush_side != SG_HOST_COLLISION_BRUSH_NONE ||
				!SurfaceForSplitValid(view, record->target_surface_id,
					record->edge))
				return 0;
			break;
		case SG_RUNE_COMPACT_RESPONSE_SPLIT_OCCLUDER_PLANE:
			if (record->target_surface_id != UINT64_MAX ||
				record->occluder >= view->static_occluder_count ||
				record->edge >= view->static_occluder_side_count ||
				view->static_occluder_sides[record->edge].occluder !=
					record->occluder ||
				record->brush_side !=
					view->static_occluder_sides[record->edge].brush_side ||
				memcmp(&record->plane,
					&view->static_occluder_sides[record->edge].plane,
					sizeof(record->plane)) != 0)
				return 0;
			break;
		case SG_RUNE_COMPACT_RESPONSE_SPLIT_OCCLUDER_EDGE:
		{
			sg_rune_binary32_plane_t edge_plane;

			if (record->target_surface_id != UINT64_MAX ||
				record->occluder >= view->static_occluder_count ||
				record->edge >= view->static_occluder_edge_count ||
				record->brush_side != SG_HOST_COLLISION_BRUSH_NONE ||
				view->static_occluder_edges[record->edge].occluder !=
					record->occluder ||
				!OccluderEdgeSplitPlane(view, record->edge, &edge_plane) ||
				memcmp(&record->plane, &edge_plane,
					sizeof(record->plane)) != 0)
				return 0;
			break;
		}
		case SG_RUNE_COMPACT_RESPONSE_SPLIT_FIRST_HIT_TIE:
			if (record->target_surface_id == UINT64_MAX ||
				record->occluder >= view->static_occluder_count ||
				record->brush_side != SG_HOST_COLLISION_BRUSH_NONE ||
				!FirstHitTieSplitValid(view, record))
				return 0;
			break;
		default:
			return 0;
		}
	}
	return 1;
}

static int TracePlaneMatchesPublished(const sg_host_collision_plane_t *trace,
	const sg_rune_binary32_plane_t *published)
{
	double same = 0.0;
	double opposite = 0.0;
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
	{
		same = fmax(same, fabs((double)trace->normal[axis] -
			(double)BitsFloat(published->normal_bits[axis])));
		opposite = fmax(opposite, fabs((double)trace->normal[axis] +
			(double)BitsFloat(published->normal_bits[axis])));
	}
	return (same <= 0.0001 &&
		fabs((double)trace->distance -
			(double)BitsFloat(published->distance_bits)) <= 0.001) ||
		(opposite <= 0.0001 &&
		 fabs((double)trace->distance +
			(double)BitsFloat(published->distance_bits)) <= 0.001);
}

static int PointInPublishedPatch(
	const sg_rune_compact_response_partition_view_t *view,
	const sg_rune_compact_response_patch_t *patch,
	const sg_rune_q8_vec3_t *point)
{
	double previous = 0.0;
	uint32_t edge;
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		if (point->value[axis] < patch->bounds.mins.value[axis] ||
			point->value[axis] > patch->bounds.maxs.value[axis])
			return 0;
	if (fabs(PublishedPlaneResidual(&patch->plane, point)) > 0.126)
		return 0;
	for (edge = 0U; edge < patch->vertex_count; edge++)
	{
		const sg_rune_q8_vec3_t *from =
			&view->target_vertices[patch->first_vertex + edge];
		const sg_rune_q8_vec3_t *to = &view->target_vertices[
			patch->first_vertex + (edge + 1U) % patch->vertex_count];
		double edge_vector[3];
		double point_vector[3];
		double cross[3];
		double side = 0.0;

		for (axis = 0U; axis < 3U; axis++)
		{
			edge_vector[axis] = (double)to->value[axis] -
				(double)from->value[axis];
			point_vector[axis] = (double)point->value[axis] -
				(double)from->value[axis];
		}
		cross[0] = edge_vector[1] * point_vector[2] -
			edge_vector[2] * point_vector[1];
		cross[1] = edge_vector[2] * point_vector[0] -
			edge_vector[0] * point_vector[2];
		cross[2] = edge_vector[0] * point_vector[1] -
			edge_vector[1] * point_vector[0];
		for (axis = 0U; axis < 3U; axis++)
			side += cross[axis] *
				(double)BitsFloat(patch->plane.normal_bits[axis]);
		if (fabs(side) <= 0.001)
			continue;
		if (previous != 0.0 && ((previous < 0.0) != (side < 0.0)))
			return 0;
		previous = side;
	}
	return 1;
}

static int TraceFiniteAndBound(
	const sg_rune_compact_response_partition_view_t *view,
	const sg_rune_compact_response_pair_t *pair)
{
	const sg_rune_compact_response_fragment_t *source =
		&view->source_fragments[pair->source_fragment];
	uint32_t axis;

	if (pair->trace.allsolid != 0 || pair->trace.startsolid != 0 ||
		!isfinite(pair->trace.fraction) || pair->trace.fraction <= 0.0f ||
		pair->trace.fraction > 1.0f ||
		!isfinite(pair->trace.plane.distance) ||
		pair->trace.model_index != SG_HOST_COLLISION_MODEL_WORLD ||
		pair->trace.instance_id != 0U ||
		!PointInPublishedPatch(view, &view->target_patches[pair->target_patch],
			&pair->target_witness))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
	{
		const double origin = (double)source->witness.value[axis] / 8.0;
		const double target = (double)pair->target_witness.value[axis] / 8.0;
		const double expected = origin +
			(double)pair->trace.fraction * (target - origin);

		if (!isfinite(pair->trace.end[axis]) ||
			!isfinite(pair->trace.plane.normal[axis]) ||
			fabs((double)pair->trace.end[axis] - expected) > 0.126)
			return 0;
	}
	return 1;
}

/* A world trace that reaches its endpoint has no impact surface.  In
 * particular, its zero plane is collision's canonical no-hit value, not an
 * implicit assertion about the target patch's plane. */
static int TraceCanonicalNoHit(const sg_host_collision_trace_t *trace)
{
	uint32_t axis;

	if (trace->allsolid != 0 || trace->startsolid != 0 ||
		FloatBits(trace->fraction) != FloatBits(1.0f) ||
		trace->contents != 0U || trace->texinfo !=
			SG_HOST_COLLISION_TEXINFO_NONE || trace->surface_flags != 0 ||
		trace->model_index != SG_HOST_COLLISION_MODEL_WORLD ||
		trace->instance_id != 0U ||
		trace->brush != SG_HOST_COLLISION_BRUSH_NONE ||
		trace->brush_side != SG_HOST_COLLISION_BRUSH_NONE ||
		FloatBits(trace->plane.distance) != 0U || trace->plane.type != 0)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (FloatBits(trace->plane.normal[axis]) != 0U)
			return 0;
	return 1;
}

static int PairCertificateValid(
	const sg_rune_compact_response_partition_view_t *view,
	const sg_rune_compact_response_pair_t *pair)
{
	if (!TraceFiniteAndBound(view, pair))
		return 0;
	if (pair->certificate == SG_RUNE_COMPACT_RESPONSE_CERTIFIED_DIRECT)
	{
		uint32_t axis;

		if (pair->certificate_split != SG_RUNE_COMPACT_RESPONSE_INDEX_NONE ||
			!TraceCanonicalNoHit(&pair->trace))
			return 0;
		for (axis = 0U; axis < 3U; axis++)
			if (fabs((double)pair->trace.end[axis] -
				(double)pair->target_witness.value[axis] / 8.0) > 0.126)
				return 0;
		return 1;
	}
	if (pair->certificate ==
		SG_RUNE_COMPACT_RESPONSE_CERTIFIED_STATIC_IMPACT)
	{
		const sg_rune_compact_response_split_t *split;
		const sg_rune_compact_response_occluder_side_t *side;
		double trace_residual;
		uint32_t axis;

		if (pair->certificate_split >= view->split_count)
			return 0;
		split = &view->splits[pair->certificate_split];
		if (split->edge >= view->static_occluder_side_count)
			return 0;
		side = &view->static_occluder_sides[split->edge];
		trace_residual = -(double)BitsFloat(split->plane.distance_bits);
		for (axis = 0U; axis < 3U; axis++)
			trace_residual +=
				(double)BitsFloat(split->plane.normal_bits[axis]) *
				(double)pair->trace.end[axis];
		return split->kind ==
			SG_RUNE_COMPACT_RESPONSE_SPLIT_OCCLUDER_PLANE &&
			split->occluder == pair->first_hit_occluder &&
			side->occluder == pair->first_hit_occluder &&
			split->brush_side == side->brush_side &&
			pair->trace.model_index == SG_HOST_COLLISION_MODEL_WORLD &&
			pair->trace.instance_id == 0U &&
			pair->trace.brush == side->brush &&
			pair->trace.brush_side == side->brush_side &&
			TracePlaneMatchesPublished(&pair->trace.plane, &split->plane) &&
			fabs(trace_residual) <= 0.001 &&
			(pair->trace.contents &
				(SG_HOST_CONTENTS_SOLID | SG_HOST_CONTENTS_WINDOW)) != 0U;
	}
	return 0;
}

static int PairInheritedGatesValid(
	const sg_rune_compact_response_candidate_group_t *candidate,
	const sg_rune_compact_response_pair_t *pair,
	sg_rune_compact_static_relation_flags_t certificate_flags)
{
	return pair->classification == SG_STATIC_VISIBILITY_CONDITIONAL &&
		pair->reason == (sg_static_visibility_reason_t)candidate->reason &&
		pair->requires_exact_ray == candidate->requires_exact_ray &&
		pair->requires_area_state == candidate->requires_area_state &&
		pair->relation_flags ==
			(candidate->relation_flags | certificate_flags);
}

int SG_RuneCompactResponsePartitionSealValid(
	const sg_rune_compact_response_partition_view_t *view)
{
	uint32_t index;
	uint32_t direct_count = 0U;
	uint32_t impact_count = 0U;
	uint32_t unresolved_count = 0U;
	uint32_t non_sky_patch_count = 0U;

	if (view == NULL ||
		view->seal.version != SG_RUNE_COMPACT_RESPONSE_PARTITION_VERSION ||
		view->seal.reserved != 0U ||
		(view->seal.flags & SG_RUNE_COMPACT_RESPONSE_SEAL_REQUIRED) !=
			SG_RUNE_COMPACT_RESPONSE_SEAL_REQUIRED ||
		(view->seal.flags &
			~(sg_rune_compact_response_seal_flags_t)
				SG_RUNE_COMPACT_RESPONSE_SEAL_KNOWN) != 0U ||
		view->seal.split_frontier_count != 0U ||
		view->seal.source_fragment_count != view->source_fragment_count ||
		view->seal.target_patch_count != view->target_patch_count ||
		view->seal.split_count != view->split_count ||
		view->seal.response_pair_count != view->response_pair_count ||
		view->seal.certified_direct_pair_count > view->response_pair_count ||
		view->seal.certified_static_impact_pair_count >
			view->response_pair_count -
			view->seal.certified_direct_pair_count ||
		view->seal.unresolved_response_pair_count !=
			view->response_pair_count -
			view->seal.certified_direct_pair_count -
			view->seal.certified_static_impact_pair_count ||
		view->seal.unresolved_response_pair_count != 0U ||
		view->seal.unresolved_candidate_group_count !=
			view->candidate_group_count ||
		view->seal.source_endpoint_group_count !=
			view->source_endpoint_group_count ||
		view->seal.target_endpoint_group_count !=
			view->target_endpoint_group_count ||
		view->seal.source_endpoint_member_count !=
			view->source_endpoint_member_count ||
		view->seal.target_endpoint_member_count !=
			view->target_endpoint_member_count ||
		view->seal.static_occluder_count != view->static_occluder_count ||
		view->seal.compact_facet_count != view->compact_facet_count ||
		view->seal.compact_cell_count != view->compact_cell_count ||
		view->seal.compact_source_surface_count !=
			view->compact_source_surface_count ||
		view->seal.compact_source_surface_vertex_count !=
			view->compact_source_surface_vertex_count ||
		view->seal.source_surface_catalog_seal == 0U ||
		view->source_fragment_count == 0U || view->source_fragments == NULL ||
		view->source_halfspaces == NULL || view->target_patch_count == 0U ||
		view->target_patches == NULL || view->target_vertices == NULL ||
		(view->split_count != 0U && view->splits == NULL) ||
		(view->response_pair_count != 0U && view->response_pairs == NULL) ||
		(view->candidate_group_count != 0U && view->candidate_groups == NULL) ||
		(view->source_endpoint_group_count != 0U &&
			view->source_endpoint_groups == NULL) ||
		(view->source_endpoint_member_count != 0U &&
			view->source_endpoint_members == NULL) ||
		(view->target_endpoint_group_count != 0U &&
			view->target_endpoint_groups == NULL) ||
		(view->target_endpoint_member_count != 0U &&
			view->target_endpoint_members == NULL) ||
		view->compact_source_surface_count == 0U ||
		view->compact_source_surfaces == NULL ||
		view->compact_source_surface_vertex_count == 0U ||
		view->compact_source_surface_vertices == NULL)
		return 0;
	if (!FragmentStorageValid(view) || !SourceSurfaceCatalogValid(view) ||
		!PatchStorageValid(view) || !OccluderAuthorityValid(view) ||
		!SplitStorageValid(view))
		return 0;
	if (view->seal.source_surface_catalog_seal !=
		SG_RuneCompactSourceSurfaceCatalogSeal(
			view->compact_source_surfaces,
			view->compact_source_surface_count,
			view->compact_source_surface_vertices,
			view->compact_source_surface_vertex_count))
		return 0;
	for (index = 0U; index < view->area_component_count; index++)
		if (view->area_components[index] >= view->area_component_count)
			return 0;
	for (index = 0U; index < view->source_fragment_count; index++)
		if (view->source_fragments[index].parent_cell.value >=
			view->compact_cell_count ||
			view->source_fragments[index].valid_stances == 0U)
			return 0;
	for (index = 0U; index < view->target_patch_count; index++)
	{
		if (view->target_patches[index].vertex_count < 3U ||
			!SpanWithin(view->target_patches[index].first_vertex,
				view->target_patches[index].vertex_count,
				view->target_vertex_count))
			return 0;
		if (view->target_patches[index].target_cell.value !=
			SG_RUNE_COMPACT_RESPONSE_INDEX_NONE &&
			view->target_patches[index].target_cell.value >=
				view->compact_cell_count)
			return 0;
		if ((view->target_patches[index].flags &
			SG_RUNE_COMPACT_RESPONSE_PATCH_SKY) == 0U)
			non_sky_patch_count++;
	}
	if (view->source_endpoint_member_count != view->source_fragment_count ||
		view->target_endpoint_member_count != non_sky_patch_count ||
		!EndpointGroupsValid(view, 1) || !EndpointGroupsValid(view, 0) ||
		!CandidateGroupsValid(view))
		return 0;
	for (index = 0U; index < view->response_pair_count; index++)
	{
		const sg_rune_compact_response_pair_t *pair =
			&view->response_pairs[index];
		const sg_rune_compact_response_candidate_group_t *candidate;

		if (pair->source_fragment >= view->source_fragment_count ||
			pair->target_patch >= view->target_patch_count ||
			pair->classification > SG_STATIC_VISIBILITY_CONDITIONAL ||
			pair->reason > SG_STATIC_VISIBILITY_REASON_SKY ||
			pair->reserved[0] != 0U || pair->reserved[1] != 0U ||
			pair->source_valid_stances !=
				view->source_fragments[pair->source_fragment].valid_stances ||
			pair->target_valid_stances !=
				view->target_patches[pair->target_patch].valid_stances ||
			(pair->relation_flags &
				~(sg_rune_compact_static_relation_flags_t)
					SG_RUNE_COMPACT_STATIC_RELATION_FLAGS_KNOWN) != 0U ||
			(index != 0U && PublishedPairCompare(view,
				&view->response_pairs[index - 1U], pair) >= 0))
			return 0;
		candidate = CandidateForEndpoints(view, pair->source_fragment,
			pair->target_patch);
		if (candidate == NULL)
			return 0;
		if (pair->certificate == SG_RUNE_COMPACT_RESPONSE_CERTIFIED_DIRECT)
		{
			if (pair->first_hit_occluder !=
					SG_RUNE_COMPACT_RESPONSE_INDEX_NONE ||
				!PairInheritedGatesValid(candidate, pair,
					SG_RUNE_COMPACT_STATIC_RELATION_DIRECT) ||
				!PairCertificateValid(view, pair))
				return 0;
			direct_count++;
		}
		else if (pair->certificate ==
			SG_RUNE_COMPACT_RESPONSE_CERTIFIED_STATIC_IMPACT)
		{
			if (pair->first_hit_occluder >= view->static_occluder_count ||
				!PairInheritedGatesValid(candidate, pair,
					SG_RUNE_COMPACT_STATIC_RELATION_OCCLUDED_IMPACT) ||
				!PairCertificateValid(view, pair))
				return 0;
			impact_count++;
		}
		else if (pair->certificate ==
			SG_RUNE_COMPACT_RESPONSE_UNRESOLVED_EXACT_RAY)
			return 0;
		else
			return 0;
	}
	return direct_count == view->seal.certified_direct_pair_count &&
		impact_count == view->seal.certified_static_impact_pair_count &&
		unresolved_count == view->seal.unresolved_response_pair_count &&
		((unresolved_count == 0U && view->candidate_group_count == 0U) ==
		 ((view->seal.flags &
			SG_RUNE_COMPACT_RESPONSE_SEAL_CONSTANT_RESPONSE_PAIRS) != 0U));
}

static uint32_t EndpointGroupForMember(
	const sg_rune_compact_response_endpoint_group_t *groups,
	uint32_t group_count, const uint32_t *members, uint32_t member)
{
	uint32_t group;

	for (group = 0U; group < group_count; group++)
	{
		uint32_t low = groups[group].first_member;
		uint32_t high = low + groups[group].member_count;

		while (low < high)
		{
			const uint32_t middle = low + (high - low) / 2U;

			if (members[middle] < member)
				low = middle + 1U;
			else
				high = middle;
		}
		if (low < groups[group].first_member + groups[group].member_count &&
			members[low] == member)
			return group;
	}
	return SG_RUNE_COMPACT_RESPONSE_INDEX_NONE;
}

int SG_RuneCompactResponsePartitionQuery(
	const sg_rune_compact_response_partition_view_t *view,
	uint32_t source_fragment, uint32_t target_patch,
	sg_rune_compact_response_pair_t *result_out)
{
	const sg_rune_compact_response_candidate_group_t *candidate;
	uint32_t index;

	if (view == NULL || result_out == NULL ||
		source_fragment >= view->source_fragment_count ||
		target_patch >= view->target_patch_count)
		return 0;
	candidate = CandidateForEndpoints(view, source_fragment, target_patch);
	if (candidate == NULL)
		return 0;
	for (index = 0U; index < view->response_pair_count; index++)
		if (view->response_pairs[index].source_fragment == source_fragment &&
			view->response_pairs[index].target_patch == target_patch)
		{
			const sg_rune_compact_response_pair_t *pair =
				&view->response_pairs[index];
			sg_rune_compact_static_relation_flags_t certificate_flags = 0U;

			if (pair->certificate ==
				SG_RUNE_COMPACT_RESPONSE_CERTIFIED_DIRECT)
				certificate_flags = SG_RUNE_COMPACT_STATIC_RELATION_DIRECT;
			else if (pair->certificate ==
				SG_RUNE_COMPACT_RESPONSE_CERTIFIED_STATIC_IMPACT)
				certificate_flags =
					SG_RUNE_COMPACT_STATIC_RELATION_OCCLUDED_IMPACT;
			else if (pair->certificate !=
				SG_RUNE_COMPACT_RESPONSE_UNRESOLVED_EXACT_RAY)
				return 0;
			if (!PairInheritedGatesValid(candidate, pair, certificate_flags))
				return 0;
			*result_out = *pair;
			return 1;
		}
	memset(result_out, 0, sizeof(*result_out));
	result_out->source_fragment = source_fragment;
	result_out->target_patch = target_patch;
	result_out->classification =
		(sg_static_visibility_class_t)candidate->classification;
	result_out->reason = (sg_static_visibility_reason_t)candidate->reason;
	result_out->first_hit_occluder = SG_RUNE_COMPACT_RESPONSE_INDEX_NONE;
	result_out->requires_exact_ray = candidate->requires_exact_ray;
	result_out->requires_area_state = candidate->requires_area_state;
	result_out->certificate =
		SG_RUNE_COMPACT_RESPONSE_UNRESOLVED_EXACT_RAY;
	result_out->relation_flags = candidate->relation_flags;
	result_out->source_valid_stances =
		view->source_fragments[source_fragment].valid_stances;
	result_out->target_valid_stances =
		view->target_patches[target_patch].valid_stances;
	result_out->certificate_split = SG_RUNE_COMPACT_RESPONSE_INDEX_NONE;
	return 1;
}

void SG_RuneCompactResponsePartitionDestroy(
	sg_rune_compact_response_partition_t *partition)
{
	sg_rune_compact_response_allocator_t allocator;

	if (partition == NULL || partition->state != RESPONSE_STATE ||
		partition->state_inverse != ~RESPONSE_STATE ||
		partition->self != partition || partition->reference_count == 0U)
		return;
	partition->reference_count--;
	if (partition->reference_count != 0U)
		return;
	allocator = partition->allocator;
	ReleaseResultArrays(partition);
	partition->state = 0U;
	partition->state_inverse = 0U;
	partition->self = NULL;
	allocator.release(allocator.context, partition);
}

const char *SG_RuneCompactResponsePartitionErrorString(
	sg_rune_compact_response_error_code_t code)
{
	static const char *const messages[] = {
		"none", "invalid argument", "builder read", "geometry read",
		"identity mismatch", "invalid source", "invalid geometry",
		"partition", "nonfinite", "q8 conversion", "overflow",
		"out of memory"
	};

	return (uint32_t)code <
		(uint32_t)SG_RUNE_COMPACT_RESPONSE_ERROR_CODE_COUNT ?
		messages[(uint32_t)code] : "unknown";
}
