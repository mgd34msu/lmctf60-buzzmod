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
}

static void TestCurrentRideWorthRecheck(void)
{
	int from_goal = 1001;
	int to_goal = 700;

	/* A 301 ms admission may wait for aim/proof. The fire boundary must use
	 * the refreshed field, not this stale allowance. */
	CHECK(SG_HookExpectedRideWorth(from_goal, to_goal) ==
	      SG_HOOK_RIDE_ALLOW);
	from_goal = 1000;
	CHECK(SG_HookExpectedRideWorth(from_goal, to_goal) ==
	      SG_HOOK_RIDE_REJECT);
	from_goal = 1001;
	CHECK(SG_HookExpectedRideWorth(from_goal, to_goal) ==
	      SG_HOOK_RIDE_ALLOW);
}

static void TestFailureStreak(void)
{
	int ban_seconds = -1;
	int streak;

	/* A low-value rejection never calls this policy, so its caller keeps its
	 * existing zero streak; only graph decode/aim failures advance it. */
	streak = 0;
	CHECK(streak == 0);
	streak = SG_HookFailureStreakAdvance(streak, &ban_seconds);
	CHECK(streak == 1 && ban_seconds == 0);
	streak = SG_HookFailureStreakAdvance(streak, &ban_seconds);
	CHECK(streak == 0 && ban_seconds == SG_HOOK_DISCIPLINE_BAN_SECONDS);
	/* A fresh graph failure starts a fresh two-failure sequence. */
	streak = SG_HookFailureStreakAdvance(streak, &ban_seconds);
	CHECK(streak == 1 && ban_seconds == 0);
}

int main(void)
{
	TestExpectedRideWorth();
	TestCurrentRideWorthRecheck();
	TestFailureStreak();
	if (failures)
	{
		fprintf(stderr, "%d sg_hook_discipline tests failed\n", failures);
		return 1;
	}
	printf("sg_hook_discipline tests passed\n");
	return 0;
}
