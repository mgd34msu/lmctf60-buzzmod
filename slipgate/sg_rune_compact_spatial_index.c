#include "sg_rune_compact_spatial_index.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SPATIAL_LEAF_ENTRIES UINT32_C(8)
#define SPATIAL_INDEX_NONE UINT32_MAX

typedef struct spatial_entry_s
{
	float mins[3];
	float maxs[3];
	uint32_t brush;
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

struct sg_rune_compact_spatial_index_s
{
	sg_rune_compact_spatial_allocator_t allocator;
	spatial_entry_t *entries;
	uint32_t entry_count;
	uint32_t bounded_entry_count;
	spatial_node_t *nodes;
	uint32_t node_count;
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

		if (!isfinite(plane->normal.value[0]) ||
			!isfinite(plane->normal.value[1]) ||
			!isfinite(plane->normal.value[2]) || !isfinite(plane->distance))
		{
			SpatialSetError(error,
				SG_RUNE_COMPACT_SPATIAL_ERROR_INVALID_WORLD, plane_index, 0U);
			return 0;
		}
	}
	for (brush_index = 0U; brush_index < world->brush_count; brush_index++)
	{
		const sg_bsp_brush_t *brush = &world->brushes[brush_index];
		uint32_t offset;

		if (brush->first_side > world->brush_side_count ||
			brush->side_count > world->brush_side_count - brush->first_side)
		{
			SpatialSetError(error,
				SG_RUNE_COMPACT_SPATIAL_ERROR_INVALID_WORLD, brush_index, 0U);
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
			SpatialSetError(error,
				SG_RUNE_COMPACT_SPATIAL_ERROR_INVALID_WORLD, leaf_index, 0U);
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

static int SpatialBrushBounds(const sg_bsp_world_t *world, uint32_t brush_index,
	float mins_out[3], float maxs_out[3])
{
	const sg_bsp_brush_t *brush = &world->brushes[brush_index];
	double mins[3] = { -INFINITY, -INFINITY, -INFINITY };
	double maxs[3] = { INFINITY, INFINITY, INFINITY };
	uint8_t has_min[3] = { 0U, 0U, 0U };
	uint8_t has_max[3] = { 0U, 0U, 0U };
	uint32_t side_offset;
	uint32_t axis;

	for (side_offset = 0U; side_offset < brush->side_count; side_offset++)
	{
		const sg_bsp_brush_side_t *side =
			&world->brush_sides[brush->first_side + side_offset];
		const sg_bsp_plane_t *plane = &world->planes[side->plane];
		uint32_t nonzero_count = 0U;
		uint32_t nonzero_axis = 0U;
		double normal = 0.0;
		double coordinate;

		for (axis = 0U; axis < 3U; axis++)
			if (plane->normal.value[axis] != 0.0f)
			{
				nonzero_count++;
				nonzero_axis = axis;
				normal = (double)plane->normal.value[axis];
			}
		if (nonzero_count != 1U)
			continue;
		coordinate = (double)plane->distance / normal;
		if (!isfinite(coordinate))
			return 0;
		if (normal < 0.0)
		{
			if (!has_min[nonzero_axis] || coordinate > mins[nonzero_axis])
				mins[nonzero_axis] = coordinate;
			has_min[nonzero_axis] = 1U;
		}
		else
		{
			if (!has_max[nonzero_axis] || coordinate < maxs[nonzero_axis])
				maxs[nonzero_axis] = coordinate;
			has_max[nonzero_axis] = 1U;
		}
	}
	for (axis = 0U; axis < 3U; axis++)
	{
		if (!has_min[axis] || !has_max[axis] || !isfinite(mins[axis]) ||
			!isfinite(maxs[axis]) || mins[axis] > maxs[axis] ||
			mins[axis] < -(double)FLT_MAX || mins[axis] > (double)FLT_MAX ||
			maxs[axis] < -(double)FLT_MAX || maxs[axis] > (double)FLT_MAX)
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
	if (left->brush < right->brush)
		return -1;
	return left->brush > right->brush ? 1 : 0;
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

static uint32_t SpatialBuildNode(sg_rune_compact_spatial_index_t *index,
	uint32_t first, uint32_t count)
{
	uint32_t node_index = index->node_count++;
	spatial_node_t *node = &index->nodes[node_index];
	uint32_t entry;
	uint32_t axis;
	uint32_t split_axis = 0U;

	node->first_entry = first;
	node->entry_count = count;
	node->left = SPATIAL_INDEX_NONE;
	node->right = SPATIAL_INDEX_NONE;
	for (axis = 0U; axis < 3U; axis++)
	{
		node->mins[axis] = index->entries[first].mins[axis];
		node->maxs[axis] = index->entries[first].maxs[axis];
	}
	for (entry = first + 1U; entry < first + count; entry++)
		for (axis = 0U; axis < 3U; axis++)
		{
			if (index->entries[entry].mins[axis] < node->mins[axis])
				node->mins[axis] = index->entries[entry].mins[axis];
			if (index->entries[entry].maxs[axis] > node->maxs[axis])
				node->maxs[axis] = index->entries[entry].maxs[axis];
		}
	if (count <= SPATIAL_LEAF_ENTRIES)
		return node_index;
	for (axis = 1U; axis < 3U; axis++)
		if ((double)node->maxs[axis] - (double)node->mins[axis] >
			(double)node->maxs[split_axis] -
				(double)node->mins[split_axis])
			split_axis = axis;
	SpatialSortEntries(index->entries, first, count, split_axis);
	{
		uint32_t left_count = count / 2U;

		node->entry_count = 0U;
		node->left = SpatialBuildNode(index, first, left_count);
		node->right = SpatialBuildNode(index, first + left_count,
			count - left_count);
	}
	return node_index;
}

static int SpatialBuildEntries(const sg_bsp_world_t *world,
	sg_rune_compact_spatial_index_t *index,
	sg_rune_compact_spatial_error_t *error)
{
	size_t bytes;
	uint32_t brush;
	uint32_t overflow_entry;

	if (!world->brush_count)
		return 1;
	if (!SpatialSize(world->brush_count, sizeof(*index->entries), &bytes))
		goto overflow;
	index->entries = index->allocator.allocate(index->allocator.context, bytes);
	if (!index->entries)
		goto out_of_memory;
	index->entry_count = world->brush_count;
	overflow_entry = world->brush_count;
	for (brush = 0U; brush < world->brush_count; brush++)
	{
		float mins[3];
		float maxs[3];
		spatial_entry_t *entry;

		if (SpatialBrushBounds(world, brush, mins, maxs))
		{
			entry = &index->entries[index->bounded_entry_count++];
			memcpy(entry->mins, mins, sizeof(entry->mins));
			memcpy(entry->maxs, maxs, sizeof(entry->maxs));
		}
		else
			entry = &index->entries[--overflow_entry];
		entry->brush = brush;
	}
	if (!index->bounded_entry_count)
		return 1;
	{
		uint32_t node_capacity = index->bounded_entry_count * 2U - 1U;

		if (!SpatialSize(node_capacity, sizeof(*index->nodes), &bytes))
			goto overflow;
		index->nodes = index->allocator.allocate(index->allocator.context,
			bytes);
		if (!index->nodes)
			goto out_of_memory;
	}
	(void)SpatialBuildNode(index, 0U, index->bounded_entry_count);
	return 1;

overflow:
	SpatialSetError(error, SG_RUNE_COMPACT_SPATIAL_ERROR_OVERFLOW,
		UINT32_MAX, 0U);
	return 0;

out_of_memory:
	SpatialSetError(error, SG_RUNE_COMPACT_SPATIAL_ERROR_OUT_OF_MEMORY,
		UINT32_MAX, 0U);
	return 0;
}

int SG_RuneCompactSpatialIndexBuild(const sg_bsp_world_t *world,
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
	if (!SpatialWorldValid(world, error_out))
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
	if (!SpatialBuildEntries(world, index, error_out))
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
			(double)query->hull.mins.value[axis] -
			(double)SG_RUNE_COMPACT_SPATIAL_EPSILON;
		maximum = (double)query->origin_bounds.maxs.value[axis] +
			(double)query->hull.maxs.value[axis] +
			(double)SG_RUNE_COMPACT_SPATIAL_EPSILON;
		if (!isfinite(minimum) || !isfinite(maximum) ||
			minimum < -(double)FLT_MAX || maximum > (double)FLT_MAX)
		{
			SpatialSetError(error,
				SG_RUNE_COMPACT_SPATIAL_ERROR_NONFINITE_BOUNDS, axis, 0U);
			return 0;
		}
		mins[axis] = nextafterf((float)minimum, -INFINITY);
		maxs[axis] = nextafterf((float)maximum, INFINITY);
	}
	return 1;
}

static void SpatialQueryNode(const sg_rune_compact_spatial_index_t *index,
	uint32_t node_index, const float mins[3], const float maxs[3],
	uint32_t *brushes, uint32_t capacity, uint32_t *count)
{
	const spatial_node_t *node = &index->nodes[node_index];

	if (!SpatialBoundsOverlap(mins, maxs, node->mins, node->maxs))
		return;
	if (node->entry_count)
	{
		uint32_t offset;

		for (offset = 0U; offset < node->entry_count; offset++)
		{
			const spatial_entry_t *entry =
				&index->entries[node->first_entry + offset];

			if (SpatialBoundsOverlap(mins, maxs, entry->mins, entry->maxs))
			{
				if (brushes && *count < capacity)
					brushes[*count] = entry->brush;
				(*count)++;
			}
		}
		return;
	}
	SpatialQueryNode(index, node->left, mins, maxs, brushes, capacity, count);
	SpatialQueryNode(index, node->right, mins, maxs, brushes, capacity, count);
}

static void SpatialSiftBrushes(uint32_t *brushes, uint32_t root,
	uint32_t count)
{
	for (;;)
	{
		uint32_t child;
		uint32_t temporary;

		if (root >= count / 2U)
			return;
		child = root * 2U + 1U;
		if (child + 1U < count && brushes[child] < brushes[child + 1U])
			child++;
		if (brushes[root] >= brushes[child])
			return;
		temporary = brushes[root];
		brushes[root] = brushes[child];
		brushes[child] = temporary;
		root = child;
	}
}

static void SpatialSortBrushes(uint32_t *brushes, uint32_t count)
{
	uint32_t index;

	if (count < 2U)
		return;
	for (index = count / 2U; index > 0U; index--)
		SpatialSiftBrushes(brushes, index - 1U, count);
	for (index = count - 1U; index > 0U; index--)
	{
		uint32_t temporary = brushes[0];

		brushes[0] = brushes[index];
		brushes[index] = temporary;
		SpatialSiftBrushes(brushes, 0U, index);
	}
}

int SG_RuneCompactSpatialIndexQuery(
	const sg_rune_compact_spatial_index_t *index,
	const sg_rune_compact_spatial_query_t *query, uint32_t *brushes_out,
	uint32_t brush_capacity, uint32_t *brush_count_out,
	sg_rune_compact_spatial_error_t *error_out)
{
	float mins[3];
	float maxs[3];
	uint32_t count = 0U;

	SpatialClearError(error_out);
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
	if (index->node_count)
		SpatialQueryNode(index, 0U, mins, maxs, brushes_out,
			brush_capacity, &count);
	{
		uint32_t entry;

		for (entry = index->bounded_entry_count; entry < index->entry_count;
			entry++)
		{
			if (brushes_out && count < brush_capacity)
				brushes_out[count] = index->entries[entry].brush;
			count++;
		}
	}
	*brush_count_out = count;
	if (!brushes_out)
		return 1;
	if (count > brush_capacity)
	{
		SpatialSetError(error_out,
			SG_RUNE_COMPACT_SPATIAL_ERROR_INSUFFICIENT_CAPACITY,
			UINT32_MAX, count);
		return 0;
	}
	SpatialSortBrushes(brushes_out, count);
	return 1;
}

void SG_RuneCompactSpatialIndexDestroy(
	sg_rune_compact_spatial_index_t *index)
{
	sg_rune_compact_spatial_allocator_t allocator;

	if (!index)
		return;
	allocator = index->allocator;
	if (index->nodes)
		allocator.release(allocator.context, index->nodes);
	if (index->entries)
		allocator.release(allocator.context, index->entries);
	allocator.release(allocator.context, index);
}

const char *SG_RuneCompactSpatialIndexErrorString(
	sg_rune_compact_spatial_error_code_t code)
{
	static const char *const messages[SG_RUNE_COMPACT_SPATIAL_ERROR_CODE_COUNT] = {
		"no error",
		"invalid argument",
		"invalid BSP world",
		"non-finite query bounds",
		"spatial index overflow",
		"out of memory",
		"insufficient candidate capacity"
	};

	if ((uint32_t)code >=
		(uint32_t)SG_RUNE_COMPACT_SPATIAL_ERROR_CODE_COUNT)
		return "unknown compact spatial index error";
	return messages[code];
}
