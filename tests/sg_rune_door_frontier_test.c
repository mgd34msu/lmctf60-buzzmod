#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "slipgate/sg_rune_door_frontier.h"

static int failures;
static int allocations_before_failure = -1;

#define CHECK(condition) do { if (!(condition)) { \
	fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); \
	failures++; } } while (0)

static void *TestAlloc(int size)
{
	if (allocations_before_failure == 0)
		return NULL;
	if (allocations_before_failure > 0)
		allocations_before_failure--;
	return malloc((size_t)size);
}

static void TestFree(void *block)
{
	free(block);
}

static void TestPointsExhaustOldWaitCap(void)
{
	sg_rune_door_point_list_t points;
	int index;

	memset(&points, 0, sizeof(points));
	CHECK(SG_RuneDoorPointListInit(&points, 65U, TestAlloc) ==
	      SG_RUNE_DOOR_FRONTIER_OK);
	for (index = 0; index < 65; index++)
	{
		float point[3] = { (float)(index * 3), 0.0f, 0.0f };

		CHECK(SG_RuneDoorPointListAppendUnique(&points, point) ==
		      SG_RUNE_DOOR_FRONTIER_OK);
	}
	CHECK(points.count == 65U);
	CHECK(points.points[64U * 3U] == 192.0f);
	SG_RuneDoorPointListFree(&points, TestFree);
}

static void TestRanksExhaustOldFansAndStayDeterministic(void)
{
	sg_rune_door_rank_list_t ranks;
	int index;

	memset(&ranks, 0, sizeof(ranks));
	CHECK(SG_RuneDoorRankListInit(&ranks, 65U, TestAlloc) ==
	      SG_RUNE_DOOR_FRONTIER_OK);
	for (index = 0; index < 65; index++)
		CHECK(SG_RuneDoorRankListAppend(&ranks, index, 64 - index,
		      (float)(64 - index)) == SG_RUNE_DOOR_FRONTIER_OK);
	SG_RuneDoorRankListSort(&ranks);
	CHECK(ranks.count == 65U);
	for (index = 0; index < 65; index++)
	{
		CHECK(ranks.items[index].value == 64 - index);
		CHECK(ranks.items[index].score == (float)index);
	}
	CHECK(SG_RuneDoorRankListAppend(&ranks, 66, 66, 66.0f) ==
	      SG_RUNE_DOOR_FRONTIER_CAPACITY);
	SG_RuneDoorRankListFree(&ranks, TestFree);
}

static void TestRanksExhaustCombinedSeedAndSuffixProducers(void)
{
	sg_rune_door_rank_list_t ranks;
	int index;

	memset(&ranks, 0, sizeof(ranks));
	CHECK(SG_RuneDoorRankListInit(&ranks, 130U, TestAlloc) ==
	      SG_RUNE_DOOR_FRONTIER_OK);
	for (index = 0; index < 65; index++)
		CHECK(SG_RuneDoorRankListAppend(&ranks, index, index,
		      (float)index) == SG_RUNE_DOOR_FRONTIER_OK);
	for (index = 0; index < 65; index++)
		CHECK(SG_RuneDoorRankListAppend(&ranks, index, 65 + index,
		      (float)(65 + index)) == SG_RUNE_DOOR_FRONTIER_OK);
	CHECK(ranks.count == 130U);
	SG_RuneDoorRankListFree(&ranks, TestFree);
}

static void TestAllocationFailsClosed(void)
{
	sg_rune_door_point_list_t points;
	sg_rune_door_rank_list_t ranks;

	memset(&points, 0, sizeof(points));
	memset(&ranks, 0, sizeof(ranks));
	allocations_before_failure = 0;
	CHECK(SG_RuneDoorPointListInit(&points, 65U, TestAlloc) ==
	      SG_RUNE_DOOR_FRONTIER_ALLOCATION);
	CHECK(!points.points && points.count == 0U);
	CHECK(SG_RuneDoorRankListInit(&ranks, 65U, TestAlloc) ==
	      SG_RUNE_DOOR_FRONTIER_ALLOCATION);
	CHECK(!ranks.items && ranks.count == 0U);
	allocations_before_failure = -1;
}

int main(void)
{
	TestPointsExhaustOldWaitCap();
	TestRanksExhaustOldFansAndStayDeterministic();
	TestRanksExhaustCombinedSeedAndSuffixProducers();
	TestAllocationFailsClosed();
	if (failures)
	{
		fprintf(stderr, "sg_rune_door_frontier_test: %d failures\n", failures);
		return 1;
	}
	puts("sg_rune_door_frontier_test: ok");
	return 0;
}
