#include "sg_rune_compact_spatial_index.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SPATIAL_LEAF_ENTRIES UINT32_C(8)

typedef struct spatial_halfspace_s
{
	float normal[3];
	float distance;
} spatial_halfspace_t;

typedef struct spatial_entry_s
{
	float mins[3];
	float maxs[3];
	uint32_t source;
	uint32_t first_plane;
	uint32_t plane_count;
} spatial_entry_t;

typedef struct spatial_node_s
{
	float mins[3];
	float maxs[3];
	uint32_t first_entry;
	uint32_t entry_count;
	uint32_t left;
	uint32_t right;
} spatial_node_t;

typedef struct spatial_topology_cell_s
{
	uint32_t first_face;
	uint32_t face_count;
} spatial_topology_cell_t;

typedef struct spatial_topology_face_s
{
	float normal[3];
	float distance;
	uint32_t source_boundary;
	sg_rune_boundary_ownership_t ownership;
} spatial_topology_face_t;

typedef struct spatial_topology_portal_s
{
	uint32_t source_boundary;
	uint32_t negative_cell;
	uint32_t positive_cell;
} spatial_topology_portal_t;

typedef struct spatial_boundary_s
{
	uint32_t source_boundary;
	sg_rune_compact_spatial_span_t faces;
	sg_rune_compact_spatial_span_t portals;
} spatial_boundary_t;

typedef struct spatial_boundary_ref_s
{
	uint32_t source_boundary;
	uint32_t record;
} spatial_boundary_ref_t;

struct sg_rune_compact_spatial_index_s
{
	sg_rune_compact_spatial_allocator_t allocator;
	spatial_entry_t *brush_entries;
	spatial_halfspace_t *brush_planes;
	spatial_node_t *brush_nodes;
	uint32_t brush_count;
	uint32_t brush_plane_count;
	uint32_t brush_node_count;
	spatial_entry_t *cell_entries;
	spatial_node_t *cell_nodes;
	spatial_topology_cell_t *topology_cells;
	spatial_topology_face_t *topology_faces;
	spatial_topology_portal_t *topology_portals;
	spatial_boundary_t *boundaries;
	uint32_t *boundary_faces;
	uint32_t *boundary_portals;
	uint32_t topology_input_cell_count;
	uint32_t topology_cell_count;
	uint32_t topology_face_count;
	uint32_t topology_portal_count;
	uint32_t topology_boundary_count;
	uint32_t cell_node_count;
	uint8_t has_topology;
};

static void *SpatialDefaultAllocate(void *context, size_t bytes)
{
	(void)context;
	return malloc(bytes);
}

static void SpatialDefaultRelease(void *context, void *allocation)
{
	(void)context;
	free(allocation);
}

static void SpatialClearError(sg_rune_compact_spatial_error_t *error)
{
	if (error)
	{
		error->code = SG_RUNE_COMPACT_SPATIAL_ERROR_NONE;
		error->record = UINT32_MAX;
		error->required_capacity = 0U;
	}
}

static void SpatialSetError(sg_rune_compact_spatial_error_t *error,
	sg_rune_compact_spatial_error_code_t code, uint32_t record,
	uint32_t required_capacity)
{
	if (error)
	{
		error->code = code;
		error->record = record;
		error->required_capacity = required_capacity;
	}
}

static int SpatialAllocator(
	const sg_rune_compact_spatial_allocator_t *source,
	sg_rune_compact_spatial_allocator_t *result)
{
	if (!source)
	{
		result->context = NULL;
		result->allocate = SpatialDefaultAllocate;
		result->release = SpatialDefaultRelease;
		return 1;
	}
	if (!source->allocate || !source->release)
		return 0;
	*result = *source;
	return 1;
}

static int SpatialSize(uint32_t count, size_t element_size, size_t *bytes_out)
{
	if ((size_t)count > SIZE_MAX / element_size)
		return 0;
	*bytes_out = (size_t)count * element_size;
	return 1;
}

static int SpatialArrayAllocate(
	const sg_rune_compact_spatial_allocator_t *allocator, uint32_t count,
	size_t element_size, void **allocation_out)
{
	size_t bytes;

	*allocation_out = NULL;
	if (!count)
		return 1;
	if (!SpatialSize(count, element_size, &bytes))
		return 0;
	*allocation_out = allocator->allocate(allocator->context, bytes);
	return *allocation_out != NULL;
}

static int SpatialFiniteBounds(const sg_rune_bounds_t *bounds)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		if (!isfinite(bounds->mins.value[axis]) ||
			!isfinite(bounds->maxs.value[axis]) ||
			bounds->mins.value[axis] > bounds->maxs.value[axis])
			return 0;
	return 1;
}

static int SpatialWorldValid(const sg_bsp_world_t *world,
	sg_rune_compact_spatial_error_t *error)
{
	uint32_t plane_index;
	uint32_t brush_index;
	uint32_t leaf_index;

	if (!world || (world->leaf_count && !world->leaves) ||
		(world->leaf_brush_count && !world->leaf_brushes) ||
		(world->plane_count && !world->planes) ||
		(world->brush_side_count && !world->brush_sides) ||
		(world->brush_count && !world->brushes))
	{
		SpatialSetError(error, SG_RUNE_COMPACT_SPATIAL_ERROR_INVALID_WORLD,
			UINT32_MAX, 0U);
		return 0;
	}
	if (world->brush_count > SG_RUNE_COMPACT_SPATIAL_MAX_BRUSHES)
	{
		SpatialSetError(error, SG_RUNE_COMPACT_SPATIAL_ERROR_OVERFLOW,
			world->brush_count, 0U);
		return 0;
	}
	for (plane_index = 0U; plane_index < world->plane_count; plane_index++)
	{
		const sg_bsp_plane_t *plane = &world->planes[plane_index];
		double length_squared =
			(double)plane->normal.value[0] * plane->normal.value[0] +
			(double)plane->normal.value[1] * plane->normal.value[1] +
			(double)plane->normal.value[2] * plane->normal.value[2];

		if (!isfinite(plane->normal.value[0]) ||
			!isfinite(plane->normal.value[1]) ||
			!isfinite(plane->normal.value[2]) || !isfinite(plane->distance) ||
			!(length_squared > 0.0) || !isfinite(length_squared))
		{
			SpatialSetError(error, SG_RUNE_COMPACT_SPATIAL_ERROR_INVALID_WORLD,
				plane_index, 0U);
			return 0;
		}
	}
	for (brush_index = 0U; brush_index < world->brush_count; brush_index++)
	{
		const sg_bsp_brush_t *brush = &world->brushes[brush_index];
		uint32_t offset;

		if (!brush->side_count || brush->first_side > world->brush_side_count ||
			brush->side_count > world->brush_side_count - brush->first_side)
		{
			SpatialSetError(error, SG_RUNE_COMPACT_SPATIAL_ERROR_INVALID_WORLD,
				brush_index, 0U);
			return 0;
		}
		for (offset = 0U; offset < brush->side_count; offset++)
			if (world->brush_sides[brush->first_side + offset].plane >=
				world->plane_count)
			{
				SpatialSetError(error,
					SG_RUNE_COMPACT_SPATIAL_ERROR_INVALID_WORLD,
					brush_index, 0U);
				return 0;
			}
	}
	for (leaf_index = 0U; leaf_index < world->leaf_count; leaf_index++)
	{
		const sg_bsp_leaf_t *leaf = &world->leaves[leaf_index];
		uint32_t axis;
		uint32_t offset;

		if (leaf->first_leaf_brush > world->leaf_brush_count ||
			leaf->leaf_brush_count > world->leaf_brush_count -
				leaf->first_leaf_brush)
		{
			SpatialSetError(error, SG_RUNE_COMPACT_SPATIAL_ERROR_INVALID_WORLD,
				leaf_index, 0U);
			return 0;
		}
		for (axis = 0U; axis < 3U; axis++)
			if (leaf->bounds.mins[axis] > leaf->bounds.maxs[axis])
			{
				SpatialSetError(error,
					SG_RUNE_COMPACT_SPATIAL_ERROR_INVALID_WORLD,
					leaf_index, 0U);
				return 0;
			}
		for (offset = 0U; offset < leaf->leaf_brush_count; offset++)
			if (world->leaf_brushes[leaf->first_leaf_brush + offset] >=
				world->brush_count)
			{
				SpatialSetError(error,
					SG_RUNE_COMPACT_SPATIAL_ERROR_INVALID_WORLD,
					leaf_index, 0U);
				return 0;
			}
	}
	return 1;
}

static void SpatialCross3(const double left[3], const double right[3],
	double result[3])
{
	result[0] = left[1] * right[2] - left[2] * right[1];
	result[1] = left[2] * right[0] - left[0] * right[2];
	result[2] = left[0] * right[1] - left[1] * right[0];
}

static double SpatialDot3(const double left[3], const double right[3])
{
	return left[0] * right[0] + left[1] * right[1] +
		left[2] * right[2];
}

static int SpatialIntersect3(const spatial_halfspace_t *first,
	const spatial_halfspace_t *second, const spatial_halfspace_t *third,
	double point[3])
{
	double n0[3], n1[3], n2[3];
	double cross12[3], cross20[3], cross01[3];
	double determinant;
	double scale;
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
	{
		n0[axis] = (double)first->normal[axis];
		n1[axis] = (double)second->normal[axis];
		n2[axis] = (double)third->normal[axis];
	}
	SpatialCross3(n1, n2, cross12);
	SpatialCross3(n2, n0, cross20);
	SpatialCross3(n0, n1, cross01);
	determinant = SpatialDot3(n0, cross12);
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

/* Plane triples are solved in binary64 while BSP planes are binary32.  This
 * bound accounts only for that arithmetic roundoff while constructing an AABB;
 * it never changes query ownership or creates an adjacency. */
static int SpatialPointBoundsFeasible(const spatial_halfspace_t *planes,
	uint32_t plane_count, const double point[3])
{
	uint32_t plane;

	for (plane = 0U; plane < plane_count; plane++)
	{
		double terms = fabs((double)planes[plane].normal[0] * point[0]) +
			fabs((double)planes[plane].normal[1] * point[1]) +
			fabs((double)planes[plane].normal[2] * point[2]) +
			fabs((double)planes[plane].distance) + 1.0;
		double value = (double)planes[plane].normal[0] * point[0] +
			(double)planes[plane].normal[1] * point[1] +
			(double)planes[plane].normal[2] * point[2];

		if (!isfinite(value) || value - (double)planes[plane].distance >
			64.0 * DBL_EPSILON * terms)
			return 0;
	}
	return 1;
}

static int SpatialBrushBounds(const spatial_halfspace_t *planes,
	uint32_t plane_count, float mins_out[3], float maxs_out[3])
{
	double mins[3] = { INFINITY, INFINITY, INFINITY };
	double maxs[3] = { -INFINITY, -INFINITY, -INFINITY };
	uint32_t first;
	uint32_t second;
	uint32_t third;
	uint32_t axis;
	uint32_t accepted = 0U;

	for (first = 0U; first < plane_count; first++)
		for (second = first + 1U; second < plane_count; second++)
			for (third = second + 1U; third < plane_count; third++)
			{
				double point[3];

				if (!SpatialIntersect3(&planes[first], &planes[second],
						&planes[third], point) ||
					!SpatialPointBoundsFeasible(planes, plane_count, point))
					continue;
				for (axis = 0U; axis < 3U; axis++)
				{
					if (point[axis] < mins[axis])
						mins[axis] = point[axis];
					if (point[axis] > maxs[axis])
						maxs[axis] = point[axis];
				}
				accepted++;
			}
	if (!accepted)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
	{
		if (!isfinite(mins[axis]) || !isfinite(maxs[axis]) ||
			mins[axis] > maxs[axis] || mins[axis] < -(double)FLT_MAX ||
			mins[axis] > (double)FLT_MAX || maxs[axis] < -(double)FLT_MAX ||
			maxs[axis] > (double)FLT_MAX)
			return 0;
		mins_out[axis] = nextafterf((float)mins[axis], -INFINITY);
		maxs_out[axis] = nextafterf((float)maxs[axis], INFINITY);
		if (!isfinite(mins_out[axis]) || !isfinite(maxs_out[axis]))
			return 0;
	}
	return 1;
}

static int SpatialEntryCompare(const spatial_entry_t *left,
	const spatial_entry_t *right, uint32_t axis)
{
	double left_center =
		(double)left->mins[axis] + (double)left->maxs[axis];
	double right_center =
		(double)right->mins[axis] + (double)right->maxs[axis];

	if (left_center < right_center)
		return -1;
	if (left_center > right_center)
		return 1;
	if (left->source < right->source)
		return -1;
	return left->source > right->source ? 1 : 0;
}

static void SpatialSwapEntry(spatial_entry_t *left, spatial_entry_t *right)
{
	spatial_entry_t temporary = *left;

	*left = *right;
	*right = temporary;
}

static void SpatialSiftEntries(spatial_entry_t *entries, uint32_t first,
	uint32_t root, uint32_t count, uint32_t axis)
{
	for (;;)
	{
		uint32_t child;

		if (root >= count / 2U)
			return;
		child = root * 2U + 1U;
		if (child + 1U < count &&
			SpatialEntryCompare(&entries[first + child],
				&entries[first + child + 1U], axis) < 0)
			child++;
		if (SpatialEntryCompare(&entries[first + root],
				&entries[first + child], axis) >= 0)
			return;
		SpatialSwapEntry(&entries[first + root], &entries[first + child]);
		root = child;
	}
}

static void SpatialSortEntries(spatial_entry_t *entries, uint32_t first,
	uint32_t count, uint32_t axis)
{
	uint32_t index;

	if (count < 2U)
		return;
	for (index = count / 2U; index > 0U; index--)
		SpatialSiftEntries(entries, first, index - 1U, count, axis);
	for (index = count - 1U; index > 0U; index--)
	{
		SpatialSwapEntry(&entries[first], &entries[first + index]);
		SpatialSiftEntries(entries, first, 0U, index, axis);
	}
}

static uint32_t SpatialBuildNode(spatial_entry_t *entries,
	spatial_node_t *nodes, uint32_t *node_count, uint32_t first,
	uint32_t count)
{
	uint32_t node_index = (*node_count)++;
	spatial_node_t *node = &nodes[node_index];
	uint32_t entry;
	uint32_t axis;
	uint32_t split_axis = 0U;

	node->first_entry = first;
	node->entry_count = count;
	node->left = SG_RUNE_COMPACT_SPATIAL_INDEX_NONE;
	node->right = SG_RUNE_COMPACT_SPATIAL_INDEX_NONE;
	for (axis = 0U; axis < 3U; axis++)
	{
		node->mins[axis] = entries[first].mins[axis];
		node->maxs[axis] = entries[first].maxs[axis];
	}
	for (entry = first + 1U; entry < first + count; entry++)
		for (axis = 0U; axis < 3U; axis++)
		{
			if (entries[entry].mins[axis] < node->mins[axis])
				node->mins[axis] = entries[entry].mins[axis];
			if (entries[entry].maxs[axis] > node->maxs[axis])
				node->maxs[axis] = entries[entry].maxs[axis];
		}
	if (count <= SPATIAL_LEAF_ENTRIES)
		return node_index;
	for (axis = 1U; axis < 3U; axis++)
		if ((double)node->maxs[axis] - (double)node->mins[axis] >
			(double)node->maxs[split_axis] -
				(double)node->mins[split_axis])
			split_axis = axis;
	SpatialSortEntries(entries, first, count, split_axis);
	{
		uint32_t left_count = count / 2U;

		node->entry_count = 0U;
		node->left = SpatialBuildNode(entries, nodes, node_count, first,
			left_count);
		node->right = SpatialBuildNode(entries, nodes, node_count,
			first + left_count, count - left_count);
	}
	return node_index;
}

static int SpatialNodeArrayAllocate(sg_rune_compact_spatial_index_t *index,
	uint32_t entry_count, spatial_node_t **nodes_out)
{
	uint32_t node_capacity;

	*nodes_out = NULL;
	if (!entry_count)
		return 1;
	node_capacity = entry_count * 2U - 1U;
	return SpatialArrayAllocate(&index->allocator, node_capacity,
		sizeof(**nodes_out), (void **)nodes_out);
}

static int SpatialBuildBrushEntries(const sg_bsp_world_t *world,
	sg_rune_compact_spatial_index_t *index,
	sg_rune_compact_spatial_error_t *error)
{
	uint32_t brush;
	uint32_t plane_cursor = 0U;

	if (!world->brush_count)
		return 1;
	if (!SpatialArrayAllocate(&index->allocator, world->brush_count,
		sizeof(*index->brush_entries), (void **)&index->brush_entries) ||
		!SpatialArrayAllocate(&index->allocator, world->brush_side_count,
			sizeof(*index->brush_planes), (void **)&index->brush_planes) ||
		!SpatialNodeArrayAllocate(index, world->brush_count, &index->brush_nodes))
	{
		SpatialSetError(error, SG_RUNE_COMPACT_SPATIAL_ERROR_OUT_OF_MEMORY,
			UINT32_MAX, 0U);
		return 0;
	}
	for (brush = 0U; brush < world->brush_count; brush++)
	{
		const sg_bsp_brush_t *source = &world->brushes[brush];
		spatial_entry_t *entry = &index->brush_entries[brush];
		uint32_t side;

		entry->source = brush;
		entry->first_plane = plane_cursor;
		entry->plane_count = source->side_count;
		for (side = 0U; side < source->side_count; side++)
		{
			const sg_bsp_plane_t *plane = &world->planes[
				world->brush_sides[source->first_side + side].plane];
			spatial_halfspace_t *destination =
				&index->brush_planes[plane_cursor++];

			memcpy(destination->normal, plane->normal.value,
				sizeof(destination->normal));
			destination->distance = plane->distance;
		}
		if (!SpatialBrushBounds(&index->brush_planes[entry->first_plane],
			entry->plane_count, entry->mins, entry->maxs))
		{
			SpatialSetError(error, SG_RUNE_COMPACT_SPATIAL_ERROR_INVALID_WORLD,
				brush, 0U);
			return 0;
		}
	}
	index->brush_count = world->brush_count;
	index->brush_plane_count = plane_cursor;
	(void)SpatialBuildNode(index->brush_entries, index->brush_nodes,
		&index->brush_node_count, 0U, index->brush_count);
	return 1;
}

int SG_RuneCompactSpatialIndexBuild(const sg_bsp_world_t *bsp_world,
	const sg_rune_compact_spatial_allocator_t *allocator,
	sg_rune_compact_spatial_index_t **index_out,
	sg_rune_compact_spatial_error_t *error_out)
{
	sg_rune_compact_spatial_allocator_t selected;
	sg_rune_compact_spatial_index_t *index;

	SpatialClearError(error_out);
	if (!index_out || *index_out || !SpatialAllocator(allocator, &selected))
	{
		SpatialSetError(error_out,
			SG_RUNE_COMPACT_SPATIAL_ERROR_INVALID_ARGUMENT, UINT32_MAX, 0U);
		return 0;
	}
	if (!SpatialWorldValid(bsp_world, error_out))
		return 0;
	index = selected.allocate(selected.context, sizeof(*index));
	if (!index)
	{
		SpatialSetError(error_out,
			SG_RUNE_COMPACT_SPATIAL_ERROR_OUT_OF_MEMORY, UINT32_MAX, 0U);
		return 0;
	}
	memset(index, 0, sizeof(*index));
	index->allocator = selected;
	if (!SpatialBuildBrushEntries(bsp_world, index, error_out))
	{
		SG_RuneCompactSpatialIndexDestroy(index);
		return 0;
	}
	*index_out = index;
	return 1;
}

static int SpatialBoundsOverlap(const float left_mins[3],
	const float left_maxs[3], const float right_mins[3],
	const float right_maxs[3])
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		if (left_maxs[axis] < right_mins[axis] ||
			left_mins[axis] > right_maxs[axis])
			return 0;
	return 1;
}

static int SpatialSweptBounds(const sg_rune_compact_spatial_query_t *query,
	float mins[3], float maxs[3],
	sg_rune_compact_spatial_error_t *error)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
	{
		double minimum;
		double maximum;

		if (!isfinite(query->origin_bounds.mins.value[axis]) ||
			!isfinite(query->origin_bounds.maxs.value[axis]) ||
			!isfinite(query->hull.mins.value[axis]) ||
			!isfinite(query->hull.maxs.value[axis]))
		{
			SpatialSetError(error,
				SG_RUNE_COMPACT_SPATIAL_ERROR_NONFINITE_BOUNDS, axis, 0U);
			return 0;
		}
		if (query->origin_bounds.mins.value[axis] >
				query->origin_bounds.maxs.value[axis] ||
			query->hull.mins.value[axis] > query->hull.maxs.value[axis])
		{
			SpatialSetError(error,
				SG_RUNE_COMPACT_SPATIAL_ERROR_INVALID_ARGUMENT, axis, 0U);
			return 0;
		}
		minimum = (double)query->origin_bounds.mins.value[axis] +
			(double)query->hull.mins.value[axis];
		maximum = (double)query->origin_bounds.maxs.value[axis] +
			(double)query->hull.maxs.value[axis];
		if (!isfinite(minimum) || !isfinite(maximum) ||
			minimum < -(double)FLT_MAX || maximum > (double)FLT_MAX)
		{
			SpatialSetError(error,
				SG_RUNE_COMPACT_SPATIAL_ERROR_NONFINITE_BOUNDS, axis, 0U);
			return 0;
		}
		mins[axis] = (float)minimum;
		maxs[axis] = (float)maximum;
	}
	return 1;
}

static float SpatialHullMinimum(const sg_rune_hull_profile_t *hull,
	const float normal[3])
{
	float result = 0.0f;
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		result += normal[axis] < 0.0f ? normal[axis] * hull->maxs.value[axis] :
			normal[axis] * hull->mins.value[axis];
	return result;
}

static void SpatialQueryConstraint(
	const sg_rune_compact_spatial_index_t *index,
	const spatial_entry_t *entry,
	const sg_rune_compact_spatial_query_t *query, uint32_t ordinal,
	spatial_halfspace_t *constraint)
{
	uint32_t axis;

	memset(constraint, 0, sizeof(*constraint));
	if (ordinal < 6U)
	{
		axis = ordinal / 2U;
		if (ordinal & 1U)
		{
			constraint->normal[axis] = -1.0f;
			constraint->distance = -query->origin_bounds.mins.value[axis];
		}
		else
		{
			constraint->normal[axis] = 1.0f;
			constraint->distance = query->origin_bounds.maxs.value[axis];
		}
		return;
	}
	*constraint = index->brush_planes[entry->first_plane + ordinal - 6U];
	constraint->distance -= SpatialHullMinimum(&query->hull,
		constraint->normal);
}

static int SpatialPointInsideQueryConstraints(
	const sg_rune_compact_spatial_index_t *index,
	const spatial_entry_t *entry,
	const sg_rune_compact_spatial_query_t *query, uint32_t constraint_count,
	const double point[3])
{
	uint32_t constraint_index;

	for (constraint_index = 0U; constraint_index < constraint_count;
		constraint_index++)
	{
		spatial_halfspace_t constraint;
		double value;

		SpatialQueryConstraint(index, entry, query, constraint_index,
			&constraint);
		value = (double)constraint.normal[0] * point[0] +
			(double)constraint.normal[1] * point[1] +
			(double)constraint.normal[2] * point[2];
		if (!isfinite(value) || value > (double)constraint.distance)
			return 0;
	}
	return 1;
}

static int SpatialBrushIntersectsQuery(
	const sg_rune_compact_spatial_index_t *index, const spatial_entry_t *entry,
	const sg_rune_compact_spatial_query_t *query)
{
	uint32_t constraint_count;
	uint32_t first;
	uint32_t second;
	uint32_t third;

	if (entry->plane_count > UINT32_MAX - 6U)
		return 0;
	constraint_count = entry->plane_count + 6U;
	for (first = 0U; first < constraint_count; first++)
		for (second = first + 1U; second < constraint_count; second++)
			for (third = second + 1U; third < constraint_count; third++)
			{
				spatial_halfspace_t a;
				spatial_halfspace_t b;
				spatial_halfspace_t c;
				double point[3];

				SpatialQueryConstraint(index, entry, query, first, &a);
				SpatialQueryConstraint(index, entry, query, second, &b);
				SpatialQueryConstraint(index, entry, query, third, &c);
				if (SpatialIntersect3(&a, &b, &c, point) &&
					SpatialPointInsideQueryConstraints(index, entry, query,
						constraint_count, point))
					return 1;
			}
	return 0;
}

static void SpatialQueryBrushNode(
	const sg_rune_compact_spatial_index_t *index, uint32_t node_index,
	const float mins[3], const float maxs[3],
	const sg_rune_compact_spatial_query_t *query, uint32_t *brushes,
	uint32_t capacity, uint32_t *count,
	sg_rune_compact_spatial_query_statistics_t *statistics)
{
	const spatial_node_t *node = &index->brush_nodes[node_index];

	statistics->visited_nodes++;
	if (!SpatialBoundsOverlap(mins, maxs, node->mins, node->maxs))
		return;
	if (node->entry_count)
	{
		uint32_t offset;

		for (offset = 0U; offset < node->entry_count; offset++)
		{
			const spatial_entry_t *entry =
				&index->brush_entries[node->first_entry + offset];

			if (!SpatialBoundsOverlap(mins, maxs, entry->mins, entry->maxs))
				continue;
			statistics->tested_entries++;
			if (SpatialBrushIntersectsQuery(index, entry, query))
			{
				if (brushes && *count < capacity)
					brushes[*count] = entry->source;
				(*count)++;
			}
		}
		return;
	}
	SpatialQueryBrushNode(index, node->left, mins, maxs, query, brushes,
		capacity, count, statistics);
	SpatialQueryBrushNode(index, node->right, mins, maxs, query, brushes,
		capacity, count, statistics);
}

static void SpatialSiftU32(uint32_t *values, uint32_t root, uint32_t count)
{
	for (;;)
	{
		uint32_t child;
		uint32_t temporary;

		if (root >= count / 2U)
			return;
		child = root * 2U + 1U;
		if (child + 1U < count && values[child] < values[child + 1U])
			child++;
		if (values[root] >= values[child])
			return;
		temporary = values[root];
		values[root] = values[child];
		values[child] = temporary;
		root = child;
	}
}

static void SpatialSortU32(uint32_t *values, uint32_t count)
{
	uint32_t index;

	if (count < 2U)
		return;
	for (index = count / 2U; index > 0U; index--)
		SpatialSiftU32(values, index - 1U, count);
	for (index = count - 1U; index > 0U; index--)
	{
		uint32_t temporary = values[0];

		values[0] = values[index];
		values[index] = temporary;
		SpatialSiftU32(values, 0U, index);
	}
}

int SG_RuneCompactSpatialIndexQueryWithStatistics(
	const sg_rune_compact_spatial_index_t *index,
	const sg_rune_compact_spatial_query_t *query, uint32_t *brushes_out,
	uint32_t brush_capacity, uint32_t *brush_count_out,
	sg_rune_compact_spatial_query_statistics_t *statistics_out,
	sg_rune_compact_spatial_error_t *error_out)
{
	float mins[3];
	float maxs[3];
	uint32_t count = 0U;
	sg_rune_compact_spatial_query_statistics_t statistics;

	SpatialClearError(error_out);
	memset(&statistics, 0, sizeof(statistics));
	if (!index || !query || !brush_count_out ||
		(!brushes_out && brush_capacity != 0U))
	{
		SpatialSetError(error_out,
			SG_RUNE_COMPACT_SPATIAL_ERROR_INVALID_ARGUMENT, UINT32_MAX, 0U);
		return 0;
	}
	*brush_count_out = 0U;
	if (!SpatialSweptBounds(query, mins, maxs, error_out))
		return 0;
	if (index->brush_node_count)
		SpatialQueryBrushNode(index, 0U, mins, maxs, query, brushes_out,
			brush_capacity, &count, &statistics);
	*brush_count_out = count;
	if (statistics_out)
		*statistics_out = statistics;
	if (!brushes_out)
		return 1;
	if (count > brush_capacity)
	{
		SpatialSetError(error_out,
			SG_RUNE_COMPACT_SPATIAL_ERROR_INSUFFICIENT_CAPACITY,
			UINT32_MAX, count);
		return 0;
	}
	SpatialSortU32(brushes_out, count);
	return 1;
}

int SG_RuneCompactSpatialIndexQuery(
	const sg_rune_compact_spatial_index_t *index,
	const sg_rune_compact_spatial_query_t *query, uint32_t *brushes_out,
	uint32_t brush_capacity, uint32_t *brush_count_out,
	sg_rune_compact_spatial_error_t *error_out)
{
	return SG_RuneCompactSpatialIndexQueryWithStatistics(index, query,
		brushes_out, brush_capacity, brush_count_out, NULL, error_out);
}

static int SpatialTopologyPointersValid(
	const sg_rune_compact_spatial_topology_input_t *topology)
{
	return topology != NULL && (!topology->cell_count || topology->cells) &&
		(!topology->face_count || topology->faces) &&
		(!topology->portal_count || topology->portals) &&
		(!topology->split_count || topology->splits) &&
		(!topology->carried_portal_count || topology->carried_portals);
}

static int SpatialRangeValid(uint32_t first, uint32_t count, uint32_t total)
{
	return first <= total && count <= total - first;
}

static int SpatialSplitCompare(
	const sg_rune_compact_spatial_split_input_t *left,
	const sg_rune_compact_spatial_split_input_t *right)
{
#define SPATIAL_COMPARE_SPLIT_FIELD(field) \
	do { if (left->field != right->field) return left->field < right->field ? -1 : 1; } while (0)
	SPATIAL_COMPARE_SPLIT_FIELD(parent_cell);
	SPATIAL_COMPARE_SPLIT_FIELD(source_boundary);
	SPATIAL_COMPARE_SPLIT_FIELD(negative_cell);
	SPATIAL_COMPARE_SPLIT_FIELD(positive_cell);
	SPATIAL_COMPARE_SPLIT_FIELD(interior_portal);
#undef SPATIAL_COMPARE_SPLIT_FIELD
	return 0;
}

static int SpatialPortalIncident(const spatial_topology_portal_t *portal,
	uint32_t cell)
{
	return portal->negative_cell == cell || portal->positive_cell == cell;
}

static int SpatialCarriedPortalValid(
	const spatial_topology_portal_t *parent,
	const spatial_topology_portal_t *child, uint32_t parent_cell,
	uint32_t negative_cell, uint32_t positive_cell)
{
	uint32_t peer;
	uint32_t child_cell;

	if (parent->source_boundary != child->source_boundary)
		return 0;
	if (parent->negative_cell == parent_cell)
		peer = parent->positive_cell;
	else if (parent->positive_cell == parent_cell)
		peer = parent->negative_cell;
	else
		return 0;
	if (child->negative_cell == peer)
		child_cell = child->positive_cell;
	else if (child->positive_cell == peer)
		child_cell = child->negative_cell;
	else
		return 0;
	return child_cell == negative_cell || child_cell == positive_cell;
}

static int SpatialBoundaryRefCompare(const spatial_boundary_ref_t *left,
	const spatial_boundary_ref_t *right)
{
	if (left->source_boundary != right->source_boundary)
		return left->source_boundary < right->source_boundary ? -1 : 1;
	if (left->record != right->record)
		return left->record < right->record ? -1 : 1;
	return 0;
}

static void SpatialSiftBoundaryRefs(spatial_boundary_ref_t *values,
	uint32_t root, uint32_t count)
{
	for (;;)
	{
		uint32_t child;
		spatial_boundary_ref_t temporary;

		if (root >= count / 2U)
			return;
		child = root * 2U + 1U;
		if (child + 1U < count &&
			SpatialBoundaryRefCompare(&values[child], &values[child + 1U]) < 0)
			child++;
		if (SpatialBoundaryRefCompare(&values[root], &values[child]) >= 0)
			return;
		temporary = values[root];
		values[root] = values[child];
		values[child] = temporary;
		root = child;
	}
}

static void SpatialSortBoundaryRefs(spatial_boundary_ref_t *values,
	uint32_t count)
{
	uint32_t index;

	if (count < 2U)
		return;
	for (index = count / 2U; index > 0U; index--)
		SpatialSiftBoundaryRefs(values, index - 1U, count);
	for (index = count - 1U; index > 0U; index--)
	{
		spatial_boundary_ref_t temporary = values[0];

		values[0] = values[index];
		values[index] = temporary;
		SpatialSiftBoundaryRefs(values, 0U, index);
	}
}

static int SpatialTopologyAllocate(
	sg_rune_compact_spatial_index_t *index,
	const sg_rune_compact_spatial_topology_input_t *topology)
{
	if (!SpatialArrayAllocate(&index->allocator, topology->cell_count,
		sizeof(*index->topology_cells), (void **)&index->topology_cells) ||
		!SpatialArrayAllocate(&index->allocator, topology->face_count,
			sizeof(*index->topology_faces), (void **)&index->topology_faces) ||
		!SpatialArrayAllocate(&index->allocator, topology->portal_count,
			sizeof(*index->topology_portals),
			(void **)&index->topology_portals))
		return 0;
	return 1;
}

static int SpatialTopologyBuildBoundaries(
	sg_rune_compact_spatial_index_t *index, uint32_t input_portal_count,
	const uint8_t *portal_active)
{
	spatial_boundary_ref_t *face_refs = NULL;
	spatial_boundary_ref_t *portal_refs = NULL;
	uint32_t portal_count = 0U;
	uint32_t boundary_capacity;
	uint32_t face;
	uint32_t portal;
	uint32_t face_cursor = 0U;
	uint32_t portal_cursor = 0U;

	for (portal = 0U; portal < input_portal_count; portal++)
		if (portal_active[portal])
			portal_count++;
	if (index->topology_face_count > UINT32_MAX - portal_count)
		return 0;
	boundary_capacity = index->topology_face_count + portal_count;
	if (!SpatialArrayAllocate(&index->allocator, index->topology_face_count,
		sizeof(*face_refs), (void **)&face_refs) ||
		!SpatialArrayAllocate(&index->allocator, portal_count,
			sizeof(*portal_refs), (void **)&portal_refs) ||
		!SpatialArrayAllocate(&index->allocator, index->topology_face_count,
			sizeof(*index->boundary_faces), (void **)&index->boundary_faces) ||
		!SpatialArrayAllocate(&index->allocator, portal_count,
			sizeof(*index->boundary_portals),
			(void **)&index->boundary_portals) ||
		!SpatialArrayAllocate(&index->allocator, boundary_capacity,
			sizeof(*index->boundaries), (void **)&index->boundaries))
		goto out;
	for (face = 0U; face < index->topology_face_count; face++)
	{
		face_refs[face].source_boundary =
			index->topology_faces[face].source_boundary;
		face_refs[face].record = face;
	}
	for (portal = 0U; portal < input_portal_count; portal++)
		if (portal_active[portal])
		{
			portal_refs[portal_cursor].source_boundary =
				index->topology_portals[portal].source_boundary;
			portal_refs[portal_cursor].record = portal;
			portal_cursor++;
		}
	SpatialSortBoundaryRefs(face_refs, index->topology_face_count);
	SpatialSortBoundaryRefs(portal_refs, portal_count);
	portal_cursor = 0U;
	while (face_cursor < index->topology_face_count ||
		portal_cursor < portal_count)
	{
		uint32_t source_boundary;
		spatial_boundary_t *boundary =
			&index->boundaries[index->topology_boundary_count++];

		if (portal_cursor == portal_count ||
			(face_cursor < index->topology_face_count &&
			face_refs[face_cursor].source_boundary <
				portal_refs[portal_cursor].source_boundary))
			source_boundary = face_refs[face_cursor].source_boundary;
		else if (face_cursor == index->topology_face_count ||
			portal_refs[portal_cursor].source_boundary <
				face_refs[face_cursor].source_boundary)
			source_boundary = portal_refs[portal_cursor].source_boundary;
		else
			source_boundary = face_refs[face_cursor].source_boundary;
		boundary->source_boundary = source_boundary;
		boundary->faces.first = face_cursor;
		while (face_cursor < index->topology_face_count &&
			face_refs[face_cursor].source_boundary == source_boundary)
		{
			index->boundary_faces[face_cursor] = face_refs[face_cursor].record;
			face_cursor++;
		}
		boundary->faces.count = face_cursor - boundary->faces.first;
		boundary->portals.first = portal_cursor;
		while (portal_cursor < portal_count &&
			portal_refs[portal_cursor].source_boundary == source_boundary)
		{
			index->boundary_portals[portal_cursor] =
				portal_refs[portal_cursor].record;
			portal_cursor++;
		}
		boundary->portals.count = portal_cursor - boundary->portals.first;
	}
	index->topology_portal_count = portal_count;
	index->allocator.release(index->allocator.context, face_refs);
	index->allocator.release(index->allocator.context, portal_refs);
	return 1;

out:
	index->allocator.release(index->allocator.context, face_refs);
	index->allocator.release(index->allocator.context, portal_refs);
	return 0;
}

static int SpatialTopologyBuildCells(sg_rune_compact_spatial_index_t *index,
	const sg_rune_compact_spatial_topology_input_t *topology,
	const uint8_t *cell_active)
{
	uint32_t cell;

	for (cell = 0U; cell < topology->cell_count; cell++)
		if (cell_active[cell])
			index->topology_cell_count++;
	if (!SpatialArrayAllocate(&index->allocator, index->topology_cell_count,
		sizeof(*index->cell_entries), (void **)&index->cell_entries) ||
		!SpatialNodeArrayAllocate(index, index->topology_cell_count,
			&index->cell_nodes))
		return 0;
	for (cell = 0U; cell < topology->cell_count; cell++)
	{
		const sg_rune_compact_spatial_cell_input_t *source =
			&topology->cells[cell];

		if (cell_active[cell])
		{
			spatial_entry_t *entry =
				&index->cell_entries[index->cell_node_count];

			memcpy(entry->mins, source->bounds.mins.value,
				sizeof(entry->mins));
			memcpy(entry->maxs, source->bounds.maxs.value,
				sizeof(entry->maxs));
			entry->source = cell;
			entry->first_plane = 0U;
			entry->plane_count = 0U;
			index->cell_node_count++;
		}
	}
	index->cell_node_count = 0U;
	if (index->topology_cell_count)
		(void)SpatialBuildNode(index->cell_entries, index->cell_nodes,
			&index->cell_node_count, 0U, index->topology_cell_count);
	return 1;
}

int SG_RuneCompactSpatialIndexBuildTopology(
	const sg_rune_compact_spatial_topology_input_t *topology,
	const sg_rune_compact_spatial_allocator_t *allocator,
	sg_rune_compact_spatial_index_t **index_out,
	sg_rune_compact_spatial_error_t *error_out)
{
	sg_rune_compact_spatial_allocator_t selected;
	sg_rune_compact_spatial_index_t *index = NULL;
	uint8_t *cell_created = NULL;
	uint8_t *portal_created = NULL;
	uint8_t *cell_active = NULL;
	uint8_t *portal_active = NULL;
	uint32_t split_index;
	uint32_t cell;
	uint32_t portal;
	uint32_t face_cursor = 0U;
	int result = 0;

	SpatialClearError(error_out);
	if (!index_out || *index_out || !SpatialAllocator(allocator, &selected) ||
		!SpatialTopologyPointersValid(topology))
	{
		SpatialSetError(error_out,
			SG_RUNE_COMPACT_SPATIAL_ERROR_INVALID_ARGUMENT, UINT32_MAX, 0U);
		return 0;
	}
	index = selected.allocate(selected.context, sizeof(*index));
	if (!index)
	{
		SpatialSetError(error_out,
			SG_RUNE_COMPACT_SPATIAL_ERROR_OUT_OF_MEMORY, UINT32_MAX, 0U);
		return 0;
	}
	memset(index, 0, sizeof(*index));
	index->allocator = selected;
	if (!SpatialArrayAllocate(&selected, topology->cell_count, sizeof(*cell_created),
			(void **)&cell_created) ||
		!SpatialArrayAllocate(&selected, topology->portal_count,
			sizeof(*portal_created), (void **)&portal_created) ||
		!SpatialArrayAllocate(&selected, topology->cell_count, sizeof(*cell_active),
			(void **)&cell_active) ||
		!SpatialArrayAllocate(&selected, topology->portal_count,
			sizeof(*portal_active), (void **)&portal_active))
	{
		SpatialSetError(error_out, SG_RUNE_COMPACT_SPATIAL_ERROR_OUT_OF_MEMORY,
			UINT32_MAX, 0U);
		goto out;
	}
	if (topology->cell_count)
	{
		memset(cell_created, 0, topology->cell_count * sizeof(*cell_created));
		memset(cell_active, 0, topology->cell_count * sizeof(*cell_active));
	}
	if (topology->portal_count)
	{
		memset(portal_created, 0,
			topology->portal_count * sizeof(*portal_created));
		memset(portal_active, 0,
			topology->portal_count * sizeof(*portal_active));
	}
	for (split_index = 0U; split_index < topology->split_count; split_index++)
	{
		const sg_rune_compact_spatial_split_input_t *split =
			&topology->splits[split_index];
		uint32_t carried;

		if ((split_index && SpatialSplitCompare(
				&topology->splits[split_index - 1U], split) >= 0) ||
			split->parent_cell >= topology->cell_count ||
			split->negative_cell >= topology->cell_count ||
			split->positive_cell >= topology->cell_count ||
			split->parent_cell == split->negative_cell ||
			split->parent_cell == split->positive_cell ||
			split->negative_cell == split->positive_cell ||
			split->interior_portal >= topology->portal_count ||
			!SpatialRangeValid(split->first_carried_portal,
				split->carried_portal_count, topology->carried_portal_count) ||
			cell_created[split->negative_cell] ||
			cell_created[split->positive_cell] ||
			portal_created[split->interior_portal])
		{
			SpatialSetError(error_out,
				SG_RUNE_COMPACT_SPATIAL_ERROR_INVALID_TOPOLOGY, split_index, 0U);
			goto out;
		}
		cell_created[split->negative_cell] = 1U;
		cell_created[split->positive_cell] = 1U;
		portal_created[split->interior_portal] = 1U;
		for (carried = split->first_carried_portal;
			carried < split->first_carried_portal + split->carried_portal_count;
			carried++)
		{
			uint32_t child = topology->carried_portals[carried].child_portal;

			if (topology->carried_portals[carried].parent_portal >=
				topology->portal_count || child >= topology->portal_count ||
				portal_created[child])
			{
				SpatialSetError(error_out,
					SG_RUNE_COMPACT_SPATIAL_ERROR_INVALID_TOPOLOGY,
					split_index, 0U);
				goto out;
			}
			portal_created[child] = 1U;
		}
	}
	for (cell = 0U; cell < topology->cell_count; cell++)
		cell_active[cell] = (uint8_t)!cell_created[cell];
	for (portal = 0U; portal < topology->portal_count; portal++)
		portal_active[portal] = (uint8_t)!portal_created[portal];
	if (!SpatialTopologyAllocate(index, topology))
	{
		SpatialSetError(error_out, SG_RUNE_COMPACT_SPATIAL_ERROR_OUT_OF_MEMORY,
			UINT32_MAX, 0U);
		goto out;
	}
	for (portal = 0U; portal < topology->portal_count; portal++)
	{
		index->topology_portals[portal].source_boundary =
			topology->portals[portal].source_boundary;
		index->topology_portals[portal].negative_cell =
			topology->portals[portal].negative_cell;
		index->topology_portals[portal].positive_cell =
			topology->portals[portal].positive_cell;
	}
	for (split_index = 0U; split_index < topology->split_count; split_index++)
	{
		const sg_rune_compact_spatial_split_input_t *split =
			&topology->splits[split_index];
		const spatial_topology_portal_t *interior =
			&index->topology_portals[split->interior_portal];
		uint32_t carried;

		if (!cell_active[split->parent_cell] ||
			cell_active[split->negative_cell] || cell_active[split->positive_cell] ||
			portal_active[split->interior_portal] ||
			interior->source_boundary != split->source_boundary ||
			interior->negative_cell != split->negative_cell ||
			interior->positive_cell != split->positive_cell)
		{
			SpatialSetError(error_out,
				SG_RUNE_COMPACT_SPATIAL_ERROR_INVALID_TOPOLOGY, split_index, 0U);
			goto out;
		}
		for (carried = split->first_carried_portal;
			carried < split->first_carried_portal + split->carried_portal_count;
			carried++)
		{
			const sg_rune_compact_spatial_carried_portal_t *map =
				&topology->carried_portals[carried];

			if (!portal_active[map->parent_portal] ||
				portal_active[map->child_portal] ||
				!SpatialCarriedPortalValid(
					&index->topology_portals[map->parent_portal],
					&index->topology_portals[map->child_portal], split->parent_cell,
					split->negative_cell, split->positive_cell))
			{
				SpatialSetError(error_out,
					SG_RUNE_COMPACT_SPATIAL_ERROR_INVALID_TOPOLOGY,
					split_index, 0U);
				goto out;
			}
		}
		for (portal = 0U; portal < topology->portal_count; portal++)
			if (portal_active[portal] && SpatialPortalIncident(
				&index->topology_portals[portal], split->parent_cell))
			{
				uint32_t map_offset;

				for (map_offset = 0U; map_offset < split->carried_portal_count;
					map_offset++)
					if (topology->carried_portals[
						split->first_carried_portal + map_offset].parent_portal ==
						portal)
						break;
				if (map_offset == split->carried_portal_count)
				{
					SpatialSetError(error_out,
						SG_RUNE_COMPACT_SPATIAL_ERROR_INVALID_TOPOLOGY,
						split_index, 0U);
					goto out;
				}
				portal_active[portal] = 0U;
			}
		for (carried = split->first_carried_portal;
			carried < split->first_carried_portal + split->carried_portal_count;
			carried++)
			portal_active[topology->carried_portals[carried].child_portal] = 1U;
		portal_active[split->interior_portal] = 1U;
		cell_active[split->parent_cell] = 0U;
		cell_active[split->negative_cell] = 1U;
		cell_active[split->positive_cell] = 1U;
	}
	for (cell = 0U; cell < topology->cell_count; cell++)
	{
		const sg_rune_compact_spatial_cell_input_t *source =
			&topology->cells[cell];

		if (!SpatialFiniteBounds(&source->bounds) ||
			!SpatialRangeValid(source->first_face, source->face_count,
				topology->face_count) ||
			(!cell_active[cell] && source->face_count != 0U) ||
			(cell_active[cell] && (!source->face_count ||
				source->first_face != face_cursor)))
		{
			SpatialSetError(error_out,
				SG_RUNE_COMPACT_SPATIAL_ERROR_INVALID_TOPOLOGY, cell, 0U);
			goto out;
		}
		index->topology_cells[cell].first_face = source->first_face;
		index->topology_cells[cell].face_count = source->face_count;
		if (cell_active[cell])
			face_cursor += source->face_count;
	}
	if (face_cursor != topology->face_count)
	{
		SpatialSetError(error_out, SG_RUNE_COMPACT_SPATIAL_ERROR_INVALID_TOPOLOGY,
			face_cursor, 0U);
		goto out;
	}
	for (face_cursor = 0U; face_cursor < topology->face_count; face_cursor++)
	{
		const sg_rune_compact_spatial_face_input_t *source =
			&topology->faces[face_cursor];
		spatial_topology_face_t *destination =
			&index->topology_faces[face_cursor];
		double length_squared = (double)source->normal[0] * source->normal[0] +
			(double)source->normal[1] * source->normal[1] +
			(double)source->normal[2] * source->normal[2];

		if (!SpatialFiniteBounds(&source->bounds) || !isfinite(source->normal[0]) ||
			!isfinite(source->normal[1]) || !isfinite(source->normal[2]) ||
			!isfinite(source->distance) || !(length_squared > 0.0) ||
			!isfinite(length_squared) ||
			source->ownership >= SG_RUNE_BOUNDARY_OWNERSHIP_COUNT)
		{
			SpatialSetError(error_out,
				SG_RUNE_COMPACT_SPATIAL_ERROR_INVALID_TOPOLOGY, face_cursor, 0U);
			goto out;
		}
		memcpy(destination->normal, source->normal, sizeof(destination->normal));
		destination->distance = source->distance;
		destination->source_boundary = source->source_boundary;
		destination->ownership = source->ownership;
	}
	for (portal = 0U; portal < topology->portal_count; portal++)
		if (portal_active[portal] &&
			(index->topology_portals[portal].negative_cell >= topology->cell_count ||
			 index->topology_portals[portal].positive_cell >= topology->cell_count ||
			 index->topology_portals[portal].negative_cell ==
				index->topology_portals[portal].positive_cell ||
			 !cell_active[index->topology_portals[portal].negative_cell] ||
			 !cell_active[index->topology_portals[portal].positive_cell]))
		{
			SpatialSetError(error_out,
				SG_RUNE_COMPACT_SPATIAL_ERROR_INVALID_TOPOLOGY, portal, 0U);
			goto out;
		}
	index->topology_input_cell_count = topology->cell_count;
	index->topology_face_count = topology->face_count;
	if (!SpatialTopologyBuildCells(index, topology, cell_active) ||
		!SpatialTopologyBuildBoundaries(index, topology->portal_count,
			portal_active))
	{
		SpatialSetError(error_out, SG_RUNE_COMPACT_SPATIAL_ERROR_OUT_OF_MEMORY,
			UINT32_MAX, 0U);
		goto out;
	}
	index->has_topology = 1U;
	*index_out = index;
	index = NULL;
	result = 1;

out:
	selected.release(selected.context, cell_created);
	selected.release(selected.context, portal_created);
	selected.release(selected.context, cell_active);
	selected.release(selected.context, portal_active);
	SG_RuneCompactSpatialIndexDestroy(index);
	return result;
}

static int SpatialCellContains(
	const sg_rune_compact_spatial_index_t *index, uint32_t cell,
	const sg_rune_vec3_t *point)
{
	const spatial_topology_cell_t *record = &index->topology_cells[cell];
	uint32_t offset;

	for (offset = 0U; offset < record->face_count; offset++)
	{
		const spatial_topology_face_t *face =
			&index->topology_faces[record->first_face + offset];
		double signed_distance = (double)face->normal[0] * point->value[0] +
			(double)face->normal[1] * point->value[1] +
			(double)face->normal[2] * point->value[2] - face->distance;

		if (!isfinite(signed_distance) ||
			(face->ownership == SG_RUNE_BOUNDARY_OPEN ?
				!(signed_distance < 0.0) : signed_distance > 0.0))
			return 0;
	}
	return 1;
}

static void SpatialQueryCellNode(
	const sg_rune_compact_spatial_index_t *index, uint32_t node_index,
	const sg_rune_vec3_t *point, uint32_t *cells, uint32_t capacity,
	uint32_t *count)
{
	const spatial_node_t *node = &index->cell_nodes[node_index];
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		if (point->value[axis] < node->mins[axis] ||
			point->value[axis] > node->maxs[axis])
			return;
	if (node->entry_count)
	{
		uint32_t offset;

		for (offset = 0U; offset < node->entry_count; offset++)
		{
			const spatial_entry_t *entry =
				&index->cell_entries[node->first_entry + offset];

			for (axis = 0U; axis < 3U; axis++)
				if (point->value[axis] < entry->mins[axis] ||
					point->value[axis] > entry->maxs[axis])
					break;
			if (axis == 3U && SpatialCellContains(index, entry->source, point))
			{
				if (cells && *count < capacity)
					cells[*count] = entry->source;
				(*count)++;
			}
		}
		return;
	}
	SpatialQueryCellNode(index, node->left, point, cells, capacity, count);
	SpatialQueryCellNode(index, node->right, point, cells, capacity, count);
}

int SG_RuneCompactSpatialIndexQueryCells(
	const sg_rune_compact_spatial_index_t *index, const sg_rune_vec3_t *point,
	uint32_t *cells_out, uint32_t cell_capacity, uint32_t *cell_count_out,
	sg_rune_compact_spatial_error_t *error_out)
{
	uint32_t count = 0U;
	uint32_t axis;

	SpatialClearError(error_out);
	if (!index || !index->has_topology || !point || !cell_count_out ||
		(!cells_out && cell_capacity != 0U))
	{
		SpatialSetError(error_out,
			SG_RUNE_COMPACT_SPATIAL_ERROR_INVALID_ARGUMENT, UINT32_MAX, 0U);
		return 0;
	}
	for (axis = 0U; axis < 3U; axis++)
		if (!isfinite(point->value[axis]))
		{
			SpatialSetError(error_out,
				SG_RUNE_COMPACT_SPATIAL_ERROR_NONFINITE_BOUNDS, axis, 0U);
			return 0;
		}
	*cell_count_out = 0U;
	if (index->cell_node_count)
		SpatialQueryCellNode(index, 0U, point, cells_out, cell_capacity,
			&count);
	*cell_count_out = count;
	if (!cells_out)
		return 1;
	if (count > cell_capacity)
	{
		SpatialSetError(error_out,
			SG_RUNE_COMPACT_SPATIAL_ERROR_INSUFFICIENT_CAPACITY,
			UINT32_MAX, count);
		return 0;
	}
	SpatialSortU32(cells_out, count);
	return 1;
}

int SG_RuneCompactSpatialIndexBoundaryRead(
	const sg_rune_compact_spatial_index_t *index, uint32_t source_boundary,
	const uint32_t **faces_out, sg_rune_compact_spatial_span_t *faces_span_out,
	const uint32_t **portals_out,
	sg_rune_compact_spatial_span_t *portals_span_out,
	sg_rune_compact_spatial_error_t *error_out)
{
	uint32_t low = 0U;
	uint32_t high;

	SpatialClearError(error_out);
	if (!index || !index->has_topology || !faces_out || !faces_span_out ||
		!portals_out || !portals_span_out)
	{
		SpatialSetError(error_out,
			SG_RUNE_COMPACT_SPATIAL_ERROR_INVALID_ARGUMENT, UINT32_MAX, 0U);
		return 0;
	}
	high = index->topology_boundary_count;
	while (low < high)
	{
		uint32_t middle = low + (high - low) / 2U;

		if (index->boundaries[middle].source_boundary < source_boundary)
			low = middle + 1U;
		else
			high = middle;
	}
	if (low == index->topology_boundary_count ||
		index->boundaries[low].source_boundary != source_boundary)
	{
		SpatialSetError(error_out, SG_RUNE_COMPACT_SPATIAL_ERROR_NOT_FOUND,
		source_boundary, 0U);
		return 0;
	}
	*faces_span_out = index->boundaries[low].faces;
	*portals_span_out = index->boundaries[low].portals;
	*faces_out = faces_span_out->count ?
		&index->boundary_faces[faces_span_out->first] : NULL;
	*portals_out = portals_span_out->count ?
		&index->boundary_portals[portals_span_out->first] : NULL;
	return 1;
}

int SG_RuneCompactSpatialIndexCounts(
	const sg_rune_compact_spatial_index_t *index,
	sg_rune_compact_spatial_counts_t *counts_out,
	sg_rune_compact_spatial_error_t *error_out)
{
	SpatialClearError(error_out);
	if (!index || !counts_out)
	{
		SpatialSetError(error_out,
			SG_RUNE_COMPACT_SPATIAL_ERROR_INVALID_ARGUMENT, UINT32_MAX, 0U);
		return 0;
	}
	counts_out->brush_count = index->brush_count;
	counts_out->cell_count = index->topology_cell_count;
	counts_out->face_count = index->topology_face_count;
	counts_out->portal_count = index->topology_portal_count;
	counts_out->source_boundary_count = index->topology_boundary_count;
	return 1;
}

void SG_RuneCompactSpatialIndexDestroy(
	sg_rune_compact_spatial_index_t *index)
{
	sg_rune_compact_spatial_allocator_t allocator;

	if (!index)
		return;
	allocator = index->allocator;
	allocator.release(allocator.context, index->boundary_portals);
	allocator.release(allocator.context, index->boundary_faces);
	allocator.release(allocator.context, index->boundaries);
	allocator.release(allocator.context, index->topology_portals);
	allocator.release(allocator.context, index->topology_faces);
	allocator.release(allocator.context, index->topology_cells);
	allocator.release(allocator.context, index->cell_nodes);
	allocator.release(allocator.context, index->cell_entries);
	allocator.release(allocator.context, index->brush_nodes);
	allocator.release(allocator.context, index->brush_planes);
	allocator.release(allocator.context, index->brush_entries);
	allocator.release(allocator.context, index);
}

const char *SG_RuneCompactSpatialIndexErrorString(
	sg_rune_compact_spatial_error_code_t code)
{
	static const char *const messages[SG_RUNE_COMPACT_SPATIAL_ERROR_CODE_COUNT] = {
		"no error",
		"invalid argument",
		"invalid BSP world",
		"non-finite bounds",
		"spatial index overflow",
		"out of memory",
		"insufficient candidate capacity",
		"invalid spatial topology",
		"spatial record not found"
	};

	if ((uint32_t)code >=
		(uint32_t)SG_RUNE_COMPACT_SPATIAL_ERROR_CODE_COUNT)
		return "unknown compact spatial index error";
	return messages[code];
}
