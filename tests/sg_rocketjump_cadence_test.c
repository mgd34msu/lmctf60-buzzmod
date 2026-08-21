#include <stdio.h>
#include <string.h>

#include "slipgate/sg_rocketjump_cadence.h"

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		        __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static void CheckSequence(float flight_ms, const int *expected, int count)
{
	sg_rocketjump_cadence_t cadence;
	int index;

	memset(&cadence, 0x5a, sizeof(cadence));
	CHECK(SG_RocketJumpCadenceBegin(&cadence, flight_ms, 100));
	for (index = 0; index < count; index++)
		CHECK((int)SG_RocketJumpCadenceNext(&cadence) == expected[index]);
	CHECK(SG_RocketJumpCadenceNext(&cadence) == SG_ROCKETJUMP_CADENCE_DONE);
}

static void TestOneProjectileFrame(void)
{
	static const int expected[] = {
		SG_ROCKETJUMP_CADENCE_BODY_STEP,
		SG_ROCKETJUMP_CADENCE_BODY_STEP,
		SG_ROCKETJUMP_CADENCE_BODY_STEP,
		SG_ROCKETJUMP_CADENCE_IMPACT,
		SG_ROCKETJUMP_CADENCE_BODY_STEP,
		SG_ROCKETJUMP_CADENCE_BODY_STEP,
		SG_ROCKETJUMP_CADENCE_BODY_STEP,
		SG_ROCKETJUMP_CADENCE_BODY_STEP,
	};

	CheckSequence(100.0f, expected,
	              (int)(sizeof(expected) / sizeof(expected[0])));
}

static void TestCeilingAndEntityBeforeBodyOrder(void)
{
	static const int expected[] = {
		SG_ROCKETJUMP_CADENCE_BODY_STEP,
		SG_ROCKETJUMP_CADENCE_BODY_STEP,
		SG_ROCKETJUMP_CADENCE_BODY_STEP,
		SG_ROCKETJUMP_CADENCE_PROJECTILE_FRAME,
		SG_ROCKETJUMP_CADENCE_BODY_STEP,
		SG_ROCKETJUMP_CADENCE_BODY_STEP,
		SG_ROCKETJUMP_CADENCE_BODY_STEP,
		SG_ROCKETJUMP_CADENCE_BODY_STEP,
		SG_ROCKETJUMP_CADENCE_IMPACT,
		SG_ROCKETJUMP_CADENCE_BODY_STEP,
		SG_ROCKETJUMP_CADENCE_BODY_STEP,
		SG_ROCKETJUMP_CADENCE_BODY_STEP,
		SG_ROCKETJUMP_CADENCE_BODY_STEP,
	};
	sg_rocketjump_cadence_t cadence;

	CheckSequence(100.01f, expected,
	              (int)(sizeof(expected) / sizeof(expected[0])));
	CHECK(!SG_RocketJumpCadenceBegin(&cadence, 0.0f, 100));
	CHECK(!SG_RocketJumpCadenceBegin(&cadence, -1.0f, 100));
	CHECK(!SG_RocketJumpCadenceBegin(&cadence, 1.0f, 0));
}

int main(void)
{
	TestOneProjectileFrame();
	TestCeilingAndEntityBeforeBodyOrder();
	if (failures)
	{
		fprintf(stderr, "sg_rocketjump_cadence_test: %d failure(s)\n",
		        failures);
		return 1;
	}
	puts("sg_rocketjump_cadence_test: ok");
	return 0;
}
