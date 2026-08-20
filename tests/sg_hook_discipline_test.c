/* Focused host-free checks for graph-hook value and failure discipline. */
#include <stdio.h>

#include "slipgate/sg_hook_discipline.h"

static int failures;

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
		failures++; \
	} \
} while (0)

static void TestExpectedRideWorth(void)
{
	CHECK(SG_HookExpectedRideWorth(1001, 700) == SG_HOOK_RIDE_ALLOW);
	CHECK(SG_HookExpectedRideWorth(1000, 700) == SG_HOOK_RIDE_REJECT);
	CHECK(SG_HookExpectedRideWorth(700, 700) == SG_HOOK_RIDE_REJECT);
	CHECK(SG_HookExpectedRideWorth(699, 700) == SG_HOOK_RIDE_REJECT);
	CHECK(SG_HookExpectedRideWorth(SG_HOOK_DISCIPLINE_FIELD_INF, 0) ==
	      SG_HOOK_RIDE_UNASSESSED);
	CHECK(SG_HookExpectedRideWorth(0, SG_HOOK_DISCIPLINE_FIELD_INF) ==
	      SG_HOOK_RIDE_UNASSESSED);
	CHECK(SG_HookExpectedRideWorth(-1, 0) == SG_HOOK_RIDE_UNASSESSED);
	CHECK(SG_HookExpectedRideWorth(1000, -1) == SG_HOOK_RIDE_UNASSESSED);
	CHECK(SG_HookRideLaunchAllowed(SG_HOOK_RIDE_ALLOW));
	CHECK(!SG_HookRideLaunchAllowed(SG_HOOK_RIDE_REJECT));
	CHECK(!SG_HookRideLaunchAllowed(SG_HOOK_RIDE_UNASSESSED));
}

static void TestCurrentRideWorthRecheck(void)
{
	CHECK(SG_HookCurrentRideWorth(2500, 1000, 1500) ==
	      SG_HOOK_RIDE_ALLOW);
	CHECK(SG_HookCurrentRideWorth(2499, 1000, 1500) ==
	      SG_HOOK_RIDE_REJECT);
	CHECK(SG_HookCurrentRideWorth(3000, 1000, 1500) ==
	      SG_HOOK_RIDE_ALLOW);
	CHECK(SG_HookCurrentRideWorth(1000, 700, 200) ==
	      SG_HOOK_RIDE_REJECT);
	CHECK(SG_HookCurrentRideWorth(2500, 1000, -1) ==
	      SG_HOOK_RIDE_UNASSESSED);
	CHECK(SG_HookCurrentRideWorth(2500,
	    SG_HOOK_DISCIPLINE_FIELD_INF - 1, 2) ==
	      SG_HOOK_RIDE_UNASSESSED);
}

static void TestSourceStateAdmission(void)
{
	CHECK(SG_HookStageSourceCompatible(0, 0, 1, 0, 1));
	CHECK(!SG_HookStageSourceCompatible(0, 0, 0, 1, 1));
	CHECK(SG_HookStageSourceCompatible(1, 0, 0, 1, 1));
	CHECK(!SG_HookStageSourceCompatible(1, 1, 0, 1, 1));
	CHECK(!SG_HookStageSourceCompatible(1, 0, 1, 0, 1));
	CHECK(!SG_HookStageSourceCompatible(1, 0, 0, 1, 0));
}

int main(void)
{
	TestExpectedRideWorth();
	TestCurrentRideWorthRecheck();
	TestSourceStateAdmission();
	if (failures)
	{
		fprintf(stderr, "%d sg_hook_discipline tests failed\n", failures);
		return 1;
	}
	printf("sg_hook_discipline tests passed\n");
	return 0;
}
