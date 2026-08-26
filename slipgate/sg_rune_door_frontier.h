#ifndef SG_RUNE_DOOR_FRONTIER_H
#define SG_RUNE_DOOR_FRONTIER_H

#include <stddef.h>

typedef void *(*sg_rune_door_frontier_alloc_fn)(int size);
typedef void (*sg_rune_door_frontier_free_fn)(void *block);

typedef enum sg_rune_door_frontier_status_e
{
	SG_RUNE_DOOR_FRONTIER_OK = 0,
	SG_RUNE_DOOR_FRONTIER_DUPLICATE,
	SG_RUNE_DOOR_FRONTIER_INVALID,
	SG_RUNE_DOOR_FRONTIER_ALLOCATION,
	SG_RUNE_DOOR_FRONTIER_CAPACITY
} sg_rune_door_frontier_status_t;

typedef struct sg_rune_door_point_list_s
{
	float *points;
	size_t count;
	size_t capacity;
} sg_rune_door_point_list_t;

typedef struct sg_rune_door_rank_s
{
	int value;
	int tie;
	float score;
} sg_rune_door_rank_t;

typedef struct sg_rune_door_rank_list_s
{
	sg_rune_door_rank_t *items;
	size_t count;
	size_t capacity;
} sg_rune_door_rank_list_t;

sg_rune_door_frontier_status_t SG_RuneDoorPointListInit(
	sg_rune_door_point_list_t *list, size_t capacity,
	sg_rune_door_frontier_alloc_fn allocate);
void SG_RuneDoorPointListFree(sg_rune_door_point_list_t *list,
	sg_rune_door_frontier_free_fn deallocate);
sg_rune_door_frontier_status_t SG_RuneDoorPointListAppendUnique(
	sg_rune_door_point_list_t *list, const float point[3]);

sg_rune_door_frontier_status_t SG_RuneDoorRankListInit(
	sg_rune_door_rank_list_t *list, size_t capacity,
	sg_rune_door_frontier_alloc_fn allocate);
void SG_RuneDoorRankListFree(sg_rune_door_rank_list_t *list,
	sg_rune_door_frontier_free_fn deallocate);
sg_rune_door_frontier_status_t SG_RuneDoorRankListAppend(
	sg_rune_door_rank_list_t *list, int value, int tie, float score);
void SG_RuneDoorRankListSort(sg_rune_door_rank_list_t *list);

#endif
