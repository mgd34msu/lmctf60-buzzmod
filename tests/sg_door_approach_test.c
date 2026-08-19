/* Pure DIRECT_TRIGGER_DOOR transient-air reducer regressions. */
#include "g_local.h"
#include "slipgate/sg_door_approach.h"

#include <stdio.h>
#include <string.h>

#define CELLAR_WITNESS_WATERTYPE 0x18000020

static int failures;

#define CHECK(condition_) do { \
	if (!(condition_)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, \
		    #condition_); \
		failures++; \
	} \
} while (0)

static void Observation(sg_door_approach_observation_t *observation,
	const pmove_state_t *pms, int grounded)
{
	memset(observation, 0, sizeof(*observation));
	observation->pms = *pms;
	observation->grounded = grounded;
	observation->static_support = grounded;
	observation->population_stable = 1;
	observation->sweep_clear = 1;
}

static void Water(sg_door_approach_observation_t *observation,
	int waterlevel, int watertype)
{
	observation->waterlevel = waterlevel;
	observation->watertype = watertype;
	observation->hazardous_liquid =
	    (watertype & (CONTENTS_LAVA | CONTENTS_SLIME)) != 0;
}

static void Initial(pmove_state_t *pms, const short source[3])
{
	memset(pms, 0, sizeof(*pms));
	pms->pm_type = PM_NORMAL;
	pms->gravity = 800;
	memcpy(pms->origin, source, sizeof(pms->origin));
}

static sg_door_approach_result_t OneStep(
	sg_door_approach_state_t *state,
	sg_door_approach_observation_t *observation,
	const pmove_state_t *post, int grounded, int touch,
	int fall_sampled, float fall_delta)
{
	sg_door_approach_result_t result;

	result = SG_DoorApproachPreStep(state, observation,
	    SG_DOOR_APPROACH_STEP_MS);
	if (result.reason != SG_DOOR_APPROACH_REASON_NONE)
		return result;
	Observation(observation, post, grounded);
	observation->physical_touch = touch;
	observation->fall_sampled = fall_sampled;
	observation->fall_delta = fall_delta;
	return SG_DoorApproachPostStep(state, observation,
	    SG_DOOR_APPROACH_STEP_MS);
}

static void TestLmctf58Case8Finalizer(void)
{
	const short source[3] = { -23640, 17312, -2719 };
	const short anchor[3] = { -24152, 17312, -2888 };
	sg_door_approach_observation_t observation;
	sg_door_approach_result_t result;
	sg_door_approach_state_t state;
	pmove_state_t pms, post;
	int step;

	Initial(&pms, source);
	Observation(&observation, &pms, 1);
	result = SG_DoorApproachBegin(&state, source, anchor, &observation);
	CHECK(result.reason == SG_DOOR_APPROACH_REASON_NONE);
	for (step = 1; step <= 79; step++)
	{
		post = observation.pms;
		post.origin[0] = (short)(source[0] +
		    ((int)(anchor[0] - source[0]) * step) / 79);
		post.origin[2] = (short)(source[2] +
		    ((int)(anchor[2] - source[2]) * step) / 79);
		post.velocity[0] = step == 79 ? 0 : -32;
		post.velocity[2] = step == 79 ? 0 : -8;
		result = OneStep(&state, &observation, &post, 1,
		    step == 30, (step % 4) == 0, 0.0f);
		CHECK(result.reason == SG_DOOR_APPROACH_REASON_NONE);
		if (result.reason != SG_DOOR_APPROACH_REASON_NONE)
			break;
	}
	CHECK(state.first_touch_ms == 750);
	CHECK(state.resume_ms == 800);
	CHECK(state.phase == SG_DOOR_APPROACH_SNAP);
	CHECK(state.elapsed_ms == 1975);
	result = SG_DoorApproachSnapped(&state, &observation);
	CHECK(result.reason == SG_DOOR_APPROACH_REASON_NONE);
	pms = state.expected_pms;
	Observation(&observation, &pms, 1);
	result = OneStep(&state, &observation, &pms, 1, 1, 1, 0.0f);
	CHECK(result.reason == SG_DOOR_APPROACH_REASON_NONE);
	CHECK(state.phase == SG_DOOR_APPROACH_COMPLETE);
	CHECK(state.elapsed_ms == 2000);
}

static void TestBoundaryFinalizer(void)
{
	const short source[3] = { 0, 0, 0 };
	const short anchor[3] = { 8, 0, 0 };
	sg_door_approach_observation_t observation;
	sg_door_approach_result_t result;
	sg_door_approach_state_t state;
	pmove_state_t pms;
	int step;

	Initial(&pms, source);
	Observation(&observation, &pms, 1);
	CHECK(SG_DoorApproachBegin(&state, source, anchor,
	    &observation).reason == SG_DOOR_APPROACH_REASON_NONE);
	state.elapsed_ms = 1975;
	state.touched = 1;
	state.first_touch_ms = 100;
	state.resume_ms = 100;
	pms.origin[0] = anchor[0];
	result = OneStep(&state, &observation, &pms, 1, 0, 1, 0.0f);
	CHECK(result.snap_required && state.finalize_ms == 2100);
	CHECK(SG_DoorApproachSnapped(&state, &observation).reason ==
	    SG_DOOR_APPROACH_REASON_NONE);
	pms = state.expected_pms;
	Observation(&observation, &pms, 1);
	for (step = 0; step < 4; step++)
	{
		result = OneStep(&state, &observation, &pms, 1, 0,
		    step == 3, 0.0f);
		CHECK(result.reason == SG_DOOR_APPROACH_REASON_NONE);
	}
	CHECK(state.phase == SG_DOOR_APPROACH_COMPLETE);
	CHECK(state.elapsed_ms == 2100);
}

static void TestAirAndFallBounds(void)
{
	const short source[3] = { 0, 0, 0 };
	const short anchor[3] = { 800, 0, 0 };
	sg_door_approach_observation_t observation;
	sg_door_approach_result_t result;
	sg_door_approach_state_t state;
	pmove_state_t pms;
	int step;

	Initial(&pms, source);
	Observation(&observation, &pms, 1);
	CHECK(SG_DoorApproachBegin(&state, source, anchor,
	    &observation).reason == SG_DOOR_APPROACH_REASON_NONE);
	for (step = 1; step <= 12; step++)
	{
		pms.origin[0] = (short)step;
		result = OneStep(&state, &observation, &pms, 0, 0,
		    (step % 4) == 0, 0.0f);
		CHECK(result.reason == SG_DOOR_APPROACH_REASON_NONE);
	}
	CHECK(state.consecutive_air_ms == 300);
	pms.origin[0]++;
	result = OneStep(&state, &observation, &pms, 0, 0, 0, 0.0f);
	CHECK(result.reason == SG_DOOR_APPROACH_REASON_AIR_TIME);

	Initial(&pms, source);
	Observation(&observation, &pms, 1);
	CHECK(SG_DoorApproachBegin(&state, source, anchor,
	    &observation).reason == SG_DOOR_APPROACH_REASON_NONE);
	for (step = 1; step <= 4; step++)
	{
		pms.origin[0] = (short)step;
		result = OneStep(&state, &observation, &pms, 1, 0,
		    step == 4, step == 4 ? 30.0f : 0.0f);
		CHECK(result.reason == SG_DOOR_APPROACH_REASON_NONE);
	}
	for (step = 1; step <= 4; step++)
	{
		pms.origin[0]++;
		result = OneStep(&state, &observation, &pms, 1, 0,
		    step == 4, step == 4 ? 30.01f : 0.0f);
	}
	CHECK(result.reason == SG_DOOR_APPROACH_REASON_FALL);
}

static void TestCapsuleAndFailClosedInputs(void)
{
	const short source[3] = { 0, 0, 0 };
	const short anchor[3] = { 800, 0, 0 };
	short point[3] = { 400, SG_DOOR_APPROACH_CAPSULE_Q8, 0 };
	sg_door_approach_observation_t observation;
	sg_door_approach_state_t state;
	pmove_state_t pms;

	CHECK(SG_DoorApproachInsideCapsule(source, anchor, point));
	point[1]++;
	CHECK(!SG_DoorApproachInsideCapsule(source, anchor, point));
	Initial(&pms, source);
	Observation(&observation, &pms, 1);
	CHECK(SG_DoorApproachBegin(&state, source, anchor,
	    &observation).reason == SG_DOOR_APPROACH_REASON_NONE);
	CHECK(SG_DoorApproachPreStep(&state, &observation, 24).reason ==
	    SG_DOOR_APPROACH_REASON_CADENCE);
	Water(&observation, 2, CONTENTS_WATER);
	CHECK(SG_DoorApproachPreStep(&state, &observation, 25).reason ==
	    SG_DOOR_APPROACH_REASON_WATER);
	Water(&observation, 0, 0);
	observation.population_stable = 0;
	CHECK(SG_DoorApproachPreStep(&state, &observation, 25).reason ==
	    SG_DOOR_APPROACH_REASON_POPULATION);
	observation.population_stable = 1;
	observation.sweep_clear = 0;
	CHECK(SG_DoorApproachPreStep(&state, &observation, 25).reason ==
	    SG_DOOR_APPROACH_REASON_SWEEP);
	observation.sweep_clear = 1;
	observation.pms.origin[0]++;
	CHECK(SG_DoorApproachPreStep(&state, &observation, 25).reason ==
	    SG_DOOR_APPROACH_REASON_POSE);
}

static void TestShallowWaterState(void)
{
	const short source[3] = { 0, 0, 0 };
	const short anchor[3] = { 800, 0, 0 };
	sg_door_approach_observation_t observation;
	sg_door_approach_result_t result;
	sg_door_approach_state_t state;
	pmove_state_t pms;

	CHECK(SG_DoorApproachWaterSafe(0, 0));
	CHECK(SG_DoorApproachWaterSafe(1, CONTENTS_WATER));
	CHECK(SG_DoorApproachWaterSafe(1, CELLAR_WITNESS_WATERTYPE));
	CHECK(!SG_DoorApproachWaterSafe(1, 0));
	CHECK(!SG_DoorApproachWaterSafe(1, CONTENTS_MIST));
	CHECK(!SG_DoorApproachWaterSafe(2, CONTENTS_WATER));
	CHECK(!SG_DoorApproachWaterSafe(1, CONTENTS_WATER | CONTENTS_LAVA));
	CHECK(!SG_DoorApproachWaterSafe(1, CONTENTS_WATER | CONTENTS_SLIME));

	Initial(&pms, source);
	Observation(&observation, &pms, 1);
	Water(&observation, 1, CELLAR_WITNESS_WATERTYPE);
	CHECK(SG_DoorApproachBegin(&state, source, anchor,
	    &observation).reason == SG_DOOR_APPROACH_REASON_NONE);
	CHECK(state.expected_waterlevel == 1);
	CHECK(state.expected_watertype == CELLAR_WITNESS_WATERTYPE);
	CHECK(SG_DoorApproachPreStep(&state, &observation, 25).reason ==
	    SG_DOOR_APPROACH_REASON_NONE);

	/* An externally changed liquid classification is state drift even when the
	 * fixed-point Pmove bytes are unchanged. */
	Water(&observation, 1, CONTENTS_WATER);
	CHECK(SG_DoorApproachPreStep(&state, &observation, 25).reason ==
	    SG_DOOR_APPROACH_REASON_POSE);

	Initial(&pms, source);
	Observation(&observation, &pms, 1);
	CHECK(SG_DoorApproachBegin(&state, source, anchor,
	    &observation).reason == SG_DOOR_APPROACH_REASON_NONE);
	pms.origin[0]++;
	CHECK(SG_DoorApproachPreStep(&state, &observation, 25).reason ==
	    SG_DOOR_APPROACH_REASON_NONE);
	Observation(&observation, &pms, 1);
	Water(&observation, 1, CELLAR_WITNESS_WATERTYPE);
	result = SG_DoorApproachPostStep(&state, &observation, 25);
	CHECK(result.reason == SG_DOOR_APPROACH_REASON_NONE);
	CHECK(state.expected_waterlevel == 1);
	CHECK(state.expected_watertype == CELLAR_WITNESS_WATERTYPE);
	CHECK(SG_DoorApproachPreStep(&state, &observation, 25).reason ==
	    SG_DOOR_APPROACH_REASON_NONE);

	Initial(&pms, source);
	Observation(&observation, &pms, 1);
	Water(&observation, 2, CONTENTS_WATER);
	CHECK(SG_DoorApproachBegin(&state, source, anchor,
	    &observation).reason == SG_DOOR_APPROACH_REASON_WATER);
	Observation(&observation, &pms, 1);
	Water(&observation, 1, CONTENTS_WATER | CONTENTS_LAVA);
	CHECK(SG_DoorApproachBegin(&state, source, anchor,
	    &observation).reason == SG_DOOR_APPROACH_REASON_WATER);
}

int main(void)
{
	TestLmctf58Case8Finalizer();
	TestBoundaryFinalizer();
	TestAirAndFallBounds();
	TestCapsuleAndFailClosedInputs();
	TestShallowWaterState();
	if (failures)
	{
		fprintf(stderr, "sg_door_approach_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("sg_door_approach_test: ok");
	return 0;
}
