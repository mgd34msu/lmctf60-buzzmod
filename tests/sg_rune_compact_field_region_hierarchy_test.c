#include "../slipgate/sg_rune_compact_field_region_hierarchy.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static void TestHierarchy(void)
{
	sg_rune_compact_cell_t cells[7];
	sg_rune_compact_model_t model;
	sg_rune_compact_field_region_hierarchy_t *hierarchy = NULL;
	uint32_t leaf0;
	uint32_t leaf1;
	uint32_t leaf2;
	uint32_t leaf3;

	memset(cells, 0, sizeof(cells));
	memset(&model, 0, sizeof(model));
	cells[0].source.model = 0U;
	cells[0].source.area = 2U;
	cells[0].source.cluster = 7;
	cells[1].source = cells[0].source;
	cells[2].source = cells[0].source;
	cells[2].source.cluster = 8;
	cells[3].source = cells[0].source;
	cells[3].source.area = 3U;
	cells[4].source = cells[3].source;
	cells[4].source.model = 1U;
	cells[5].source = cells[4].source;
	cells[5].source.cluster = -1;
	cells[6].source = cells[5].source;
	model.cells = cells;
	model.cell_count = 7U;

	CHECK(SG_RuneCompactFieldRegionHierarchyCreate(&model, &hierarchy) ==
		SG_RUNE_COMPACT_FIELD_REGION_OK);
	CHECK(hierarchy != NULL);
	CHECK(SG_RuneCompactFieldRegionLeafCount(hierarchy) == 5U);
	CHECK(SG_RuneCompactFieldRegionCoarseCount(hierarchy) == 3U);
	leaf0 = SG_RuneCompactFieldRegionCellLeaf(hierarchy, 0U);
	leaf1 = SG_RuneCompactFieldRegionCellLeaf(hierarchy, 2U);
	leaf2 = SG_RuneCompactFieldRegionCellLeaf(hierarchy, 3U);
	leaf3 = SG_RuneCompactFieldRegionCellLeaf(hierarchy, 5U);
	CHECK(leaf0 == SG_RuneCompactFieldRegionCellLeaf(hierarchy, 1U));
	CHECK(leaf0 != leaf1);
	CHECK(leaf1 != leaf2);
	CHECK(leaf3 == SG_RuneCompactFieldRegionCellLeaf(hierarchy, 6U));
	CHECK(SG_RuneCompactFieldRegionCellCoarse(hierarchy, 0U) ==
		SG_RuneCompactFieldRegionCellCoarse(hierarchy, 2U));
	CHECK(SG_RuneCompactFieldRegionCellCoarse(hierarchy, 2U) !=
		SG_RuneCompactFieldRegionCellCoarse(hierarchy, 3U));
	CHECK(SG_RuneCompactFieldRegionCellCoarse(hierarchy, 3U) !=
		SG_RuneCompactFieldRegionCellCoarse(hierarchy, 4U));
	CHECK(SG_RuneCompactFieldRegionLeafCoarse(hierarchy, leaf0) ==
		SG_RuneCompactFieldRegionCellCoarse(hierarchy, 0U));
	CHECK(SG_RuneCompactFieldRegionLeafCoarse(hierarchy, leaf1) ==
		SG_RuneCompactFieldRegionCellCoarse(hierarchy, 2U));
	CHECK(SG_RuneCompactFieldRegionLeafCoarse(hierarchy, leaf2) ==
		SG_RuneCompactFieldRegionCellCoarse(hierarchy, 3U));
	CHECK(SG_RuneCompactFieldRegionLeafCoarse(hierarchy, leaf3) ==
		SG_RuneCompactFieldRegionCellCoarse(hierarchy, 5U));
	CHECK(SG_RuneCompactFieldRegionCellLeaf(hierarchy, 7U) ==
		SG_RUNE_COMPACT_INDEX_NONE);
	CHECK(SG_RuneCompactFieldRegionCellCoarse(NULL, 0U) ==
		SG_RUNE_COMPACT_INDEX_NONE);
	SG_RuneCompactFieldRegionHierarchyDestroy(hierarchy);
}

static void TestFailures(void)
{
	sg_rune_compact_model_t model;
	sg_rune_compact_field_region_hierarchy_t *hierarchy =
		(sg_rune_compact_field_region_hierarchy_t *)(uintptr_t)1U;

	memset(&model, 0, sizeof(model));
	CHECK(SG_RuneCompactFieldRegionHierarchyCreate(NULL, &hierarchy) ==
		SG_RUNE_COMPACT_FIELD_REGION_INVALID_ARGUMENT);
	CHECK(hierarchy == NULL);
	CHECK(SG_RuneCompactFieldRegionHierarchyCreate(&model, &hierarchy) ==
		SG_RUNE_COMPACT_FIELD_REGION_INVALID_MODEL);
	CHECK(hierarchy == NULL);
	CHECK(strcmp(SG_RuneCompactFieldRegionStatusString(
		SG_RUNE_COMPACT_FIELD_REGION_ALLOCATION_FAILED),
		"allocation failed") == 0);
	CHECK(strcmp(SG_RuneCompactFieldRegionStatusString(
		(sg_rune_compact_field_region_status_t)UINT32_MAX),
		"unknown compact field region status") == 0);
}

int main(void)
{
	TestHierarchy();
	TestFailures();
	if (failures != 0)
	{
		fprintf(stderr, "%d compact field region hierarchy tests failed\n",
			failures);
		return 1;
	}
	puts("sg_rune_compact_field_region_hierarchy_test: PASS");
	return 0;
}
