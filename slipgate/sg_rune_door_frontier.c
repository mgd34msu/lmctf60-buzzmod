#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "sg_rune_door_frontier.h"

static int DoorRankCompare(const void *left, const void *right)
{
	const sg_rune_door_rank_t *a = left;
	const sg_rune_door_rank_t *b = right;

	if (a->score < b->score)
		return -1;
	if (a->score > b->score)
		return 1;
	if (a->tie < b->tie)
		return -1;
	if (a->tie > b->tie)
		return 1;
	return (a->value > b->value) - (a->value < b->value);
}

sg_rune_door_frontier_status_t SG_RuneDoorPointListInit(
	sg_rune_door_point_list_t *list, size_t capacity,
	sg_rune_door_frontier_alloc_fn allocate)
{
	if (!list || !allocate ||
	    capacity > (size_t)INT_MAX / (3U * sizeof(*list->points)))
		return SG_RUNE_DOOR_FRONTIER_INVALID;
	memset(list, 0, sizeof(*list));
	if (capacity == 0U)
		return SG_RUNE_DOOR_FRONTIER_OK;
	list->points = allocate((int)(capacity * 3U * sizeof(*list->points)));
	if (!list->points)
		return SG_RUNE_DOOR_FRONTIER_ALLOCATION;
	list->capacity = capacity;
	return SG_RUNE_DOOR_FRONTIER_OK;
}

void SG_RuneDoorPointListFree(sg_rune_door_point_list_t *list,
	sg_rune_door_frontier_free_fn deallocate)
{
	if (!list)
		return;
	if (list->points && deallocate)
		deallocate(list->points);
	memset(list, 0, sizeof(*list));
}

sg_rune_door_frontier_status_t SG_RuneDoorPointListAppendUnique(
	sg_rune_door_point_list_t *list, const float point[3])
{
	size_t index;

	if (!list || !point || !isfinite(point[0]) || !isfinite(point[1]) ||
	    !isfinite(point[2]) || (list->capacity > 0U && !list->points))
		return SG_RUNE_DOOR_FRONTIER_INVALID;
	for (index = 0U; index < list->count; index++)
	{
		const float *existing = &list->points[index * 3U];
		float dx = existing[0] - point[0];
		float dy = existing[1] - point[1];
		float dz = existing[2] - point[2];

		if (fabsf(dz) <= 2.0f && dx * dx + dy * dy <= 4.0f)
			return SG_RUNE_DOOR_FRONTIER_DUPLICATE;
	}
	if (list->count >= list->capacity)
		return SG_RUNE_DOOR_FRONTIER_CAPACITY;
	memcpy(&list->points[list->count * 3U], point,
	       3U * sizeof(*list->points));
	list->count++;
	return SG_RUNE_DOOR_FRONTIER_OK;
}

sg_rune_door_frontier_status_t SG_RuneDoorRankListInit(
	sg_rune_door_rank_list_t *list, size_t capacity,
	sg_rune_door_frontier_alloc_fn allocate)
{
	if (!list || !allocate ||
	    capacity > (size_t)INT_MAX / sizeof(*list->items))
		return SG_RUNE_DOOR_FRONTIER_INVALID;
	memset(list, 0, sizeof(*list));
	if (capacity == 0U)
		return SG_RUNE_DOOR_FRONTIER_OK;
	list->items = allocate((int)(capacity * sizeof(*list->items)));
	if (!list->items)
		return SG_RUNE_DOOR_FRONTIER_ALLOCATION;
	list->capacity = capacity;
	return SG_RUNE_DOOR_FRONTIER_OK;
}

void SG_RuneDoorRankListFree(sg_rune_door_rank_list_t *list,
	sg_rune_door_frontier_free_fn deallocate)
{
	if (!list)
		return;
	if (list->items && deallocate)
		deallocate(list->items);
	memset(list, 0, sizeof(*list));
}

sg_rune_door_frontier_status_t SG_RuneDoorRankListAppend(
	sg_rune_door_rank_list_t *list, int value, int tie, float score)
{
	if (!list || !isfinite(score) ||
	    (list->capacity > 0U && !list->items))
		return SG_RUNE_DOOR_FRONTIER_INVALID;
	if (list->count >= list->capacity)
		return SG_RUNE_DOOR_FRONTIER_CAPACITY;
	list->items[list->count++] = (sg_rune_door_rank_t){ value, tie, score };
	return SG_RUNE_DOOR_FRONTIER_OK;
}

void SG_RuneDoorRankListSort(sg_rune_door_rank_list_t *list)
{
	if (!list || list->count < 2U || !list->items)
		return;
	qsort(list->items, list->count, sizeof(*list->items), DoorRankCompare);
}
