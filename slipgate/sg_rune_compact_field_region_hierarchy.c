#include "sg_rune_compact_field_region_hierarchy.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct region_record_s
{
	uint32_t model;
	uint32_t area;
	int32_t cluster;
	uint32_t cell;
} region_record_t;

struct sg_rune_compact_field_region_hierarchy_s
{
	uint32_t cell_count;
	uint32_t leaf_count;
	uint32_t coarse_count;
	uint32_t *cell_leaves;
	uint32_t *cell_coarse;
	uint32_t *leaf_coarse;
};

static int LeafCompare(const void *left_value, const void *right_value)
{
	const region_record_t *left = (const region_record_t *)left_value;
	const region_record_t *right = (const region_record_t *)right_value;

	if (left->model != right->model)
		return left->model < right->model ? -1 : 1;
	if (left->area != right->area)
		return left->area < right->area ? -1 : 1;
	if (left->cluster != right->cluster)
		return left->cluster < right->cluster ? -1 : 1;
	if (left->cell != right->cell)
		return left->cell < right->cell ? -1 : 1;
	return 0;
}

static int CoarseCompare(const void *left_value, const void *right_value)
{
	const region_record_t *left = (const region_record_t *)left_value;
	const region_record_t *right = (const region_record_t *)right_value;

	if (left->model != right->model)
		return left->model < right->model ? -1 : 1;
	if (left->area != right->area)
		return left->area < right->area ? -1 : 1;
	if (left->cell != right->cell)
		return left->cell < right->cell ? -1 : 1;
	return 0;
}

static int SameLeaf(const region_record_t *left,
	const region_record_t *right)
{
	return left->model == right->model && left->area == right->area &&
		left->cluster == right->cluster;
}

static int SameCoarse(const region_record_t *left,
	const region_record_t *right)
{
	return left->model == right->model && left->area == right->area;
}

static void DestroyPartial(sg_rune_compact_field_region_hierarchy_t *hierarchy,
	region_record_t *records)
{
	free(records);
	if (hierarchy == NULL)
		return;
	free(hierarchy->cell_leaves);
	free(hierarchy->cell_coarse);
	free(hierarchy->leaf_coarse);
	free(hierarchy);
}

sg_rune_compact_field_region_status_t
SG_RuneCompactFieldRegionHierarchyCreate(
	const sg_rune_compact_model_t *model,
	sg_rune_compact_field_region_hierarchy_t **hierarchy_out)
{
	sg_rune_compact_field_region_hierarchy_t *hierarchy;
	region_record_t *records;
	uint32_t index;

	if (hierarchy_out != NULL)
		*hierarchy_out = NULL;
	if (model == NULL || hierarchy_out == NULL)
		return SG_RUNE_COMPACT_FIELD_REGION_INVALID_ARGUMENT;
	if (model->cell_count == 0U ||
		model->cell_count > SG_RUNE_COMPACT_MAX_CELLS || model->cells == NULL)
		return SG_RUNE_COMPACT_FIELD_REGION_INVALID_MODEL;
	hierarchy = (sg_rune_compact_field_region_hierarchy_t *)calloc(
		1U, sizeof(*hierarchy));
	records = (region_record_t *)calloc((size_t)model->cell_count,
		sizeof(*records));
	if (hierarchy == NULL || records == NULL)
	{
		DestroyPartial(hierarchy, records);
		return SG_RUNE_COMPACT_FIELD_REGION_ALLOCATION_FAILED;
	}
	hierarchy->cell_count = model->cell_count;
	hierarchy->cell_leaves = (uint32_t *)calloc((size_t)model->cell_count,
		sizeof(*hierarchy->cell_leaves));
	hierarchy->cell_coarse = (uint32_t *)calloc((size_t)model->cell_count,
		sizeof(*hierarchy->cell_coarse));
	if (hierarchy->cell_leaves == NULL || hierarchy->cell_coarse == NULL)
	{
		DestroyPartial(hierarchy, records);
		return SG_RUNE_COMPACT_FIELD_REGION_ALLOCATION_FAILED;
	}
	for (index = 0U; index < model->cell_count; index++)
	{
		records[index].model = model->cells[index].source.model;
		records[index].area = model->cells[index].source.area;
		records[index].cluster = model->cells[index].source.cluster;
		records[index].cell = index;
	}
	qsort(records, (size_t)model->cell_count, sizeof(*records), LeafCompare);
	for (index = 0U; index < model->cell_count; index++)
	{
		if (index == 0U || !SameLeaf(&records[index - 1U], &records[index]))
			hierarchy->leaf_count++;
		hierarchy->cell_leaves[records[index].cell] =
			hierarchy->leaf_count - 1U;
	}
	hierarchy->leaf_coarse = (uint32_t *)calloc((size_t)hierarchy->leaf_count,
		sizeof(*hierarchy->leaf_coarse));
	if (hierarchy->leaf_coarse == NULL)
	{
		DestroyPartial(hierarchy, records);
		return SG_RUNE_COMPACT_FIELD_REGION_ALLOCATION_FAILED;
	}
	qsort(records, (size_t)model->cell_count, sizeof(*records), CoarseCompare);
	for (index = 0U; index < model->cell_count; index++)
	{
		const uint32_t cell = records[index].cell;

		if (index == 0U || !SameCoarse(&records[index - 1U], &records[index]))
			hierarchy->coarse_count++;
		hierarchy->cell_coarse[cell] = hierarchy->coarse_count - 1U;
		hierarchy->leaf_coarse[hierarchy->cell_leaves[cell]] =
			hierarchy->coarse_count - 1U;
	}
	free(records);
	*hierarchy_out = hierarchy;
	return SG_RUNE_COMPACT_FIELD_REGION_OK;
}

void SG_RuneCompactFieldRegionHierarchyDestroy(
	sg_rune_compact_field_region_hierarchy_t *hierarchy)
{
	DestroyPartial(hierarchy, NULL);
}

uint32_t SG_RuneCompactFieldRegionLeafCount(
	const sg_rune_compact_field_region_hierarchy_t *hierarchy)
{
	return hierarchy != NULL ? hierarchy->leaf_count : 0U;
}

uint32_t SG_RuneCompactFieldRegionCoarseCount(
	const sg_rune_compact_field_region_hierarchy_t *hierarchy)
{
	return hierarchy != NULL ? hierarchy->coarse_count : 0U;
}

uint32_t SG_RuneCompactFieldRegionCellLeaf(
	const sg_rune_compact_field_region_hierarchy_t *hierarchy,
	uint32_t cell)
{
	if (hierarchy == NULL || cell >= hierarchy->cell_count)
		return SG_RUNE_COMPACT_INDEX_NONE;
	return hierarchy->cell_leaves[cell];
}

uint32_t SG_RuneCompactFieldRegionCellCoarse(
	const sg_rune_compact_field_region_hierarchy_t *hierarchy,
	uint32_t cell)
{
	if (hierarchy == NULL || cell >= hierarchy->cell_count)
		return SG_RUNE_COMPACT_INDEX_NONE;
	return hierarchy->cell_coarse[cell];
}

uint32_t SG_RuneCompactFieldRegionLeafCoarse(
	const sg_rune_compact_field_region_hierarchy_t *hierarchy,
	uint32_t leaf)
{
	if (hierarchy == NULL || leaf >= hierarchy->leaf_count)
		return SG_RUNE_COMPACT_INDEX_NONE;
	return hierarchy->leaf_coarse[leaf];
}

const char *SG_RuneCompactFieldRegionStatusString(
	sg_rune_compact_field_region_status_t status)
{
	switch (status)
	{
	case SG_RUNE_COMPACT_FIELD_REGION_OK:
		return "ok";
	case SG_RUNE_COMPACT_FIELD_REGION_INVALID_ARGUMENT:
		return "invalid argument";
	case SG_RUNE_COMPACT_FIELD_REGION_INVALID_MODEL:
		return "invalid model";
	case SG_RUNE_COMPACT_FIELD_REGION_ALLOCATION_FAILED:
		return "allocation failed";
	case SG_RUNE_COMPACT_FIELD_REGION_STATUS_COUNT:
	default:
		return "unknown compact field region status";
	}
}
