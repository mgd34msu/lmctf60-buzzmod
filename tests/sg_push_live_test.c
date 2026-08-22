#include "g_local.h"
#include "slipgate/sg_push_live.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition_) do { \
	if (!(condition_)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, \
			#condition_); \
		failures++; \
	} \
} while (0)

static void Fixture(sg_push_witness_t *witness,
	sg_push_observation_t *observation)
{
	memset(witness, 0, sizeof(*witness));
	memset(observation, 0, sizeof(*observation));
	witness->link_index = 7;
	witness->entry_key = 17U;
	witness->source_q8[0] = observation->origin_q8[0] = 800;
	witness->source_q8[1] = observation->origin_q8[1] = -1600;
	witness->source_q8[2] = observation->origin_q8[2] = 256;
	witness->destination_q8[0] = 2400;
	witness->destination_q8[1] = -1600;
	witness->destination_q8[2] = 768;
	witness->push_velocity[0] = 0.0f;
	witness->push_velocity[1] = -59.2648315f;
	witness->push_velocity[2] = 846.765747f;
	witness->cost_ms = 1200U;
	observation->alive = true;
	observation->grounded = true;
	observation->dry = true;
#ifdef SG_PUSH_OBSERVATION_SOURCE_STATE
	observation->immutable_support = true;
	observation->at_rest = true;
	observation->ordinary_control = true;
#endif
}

static void TestNormalRunHandoffSourceEnvelope(void)
{
	sg_push_witness_t witness;
	sg_push_observation_t observation;
	sg_push_live_state_t state;

	Fixture(&witness, &observation);
	observation.origin_q8[0] += 383;
	CHECK(SG_PushLiveBegin(&state, &witness, &observation));

	Fixture(&witness, &observation);
	observation.origin_q8[0] += 384;
	CHECK(!SG_PushLiveBegin(&state, &witness, &observation));
	CHECK(state.failure == SG_PUSH_FAILURE_SOURCE);

	Fixture(&witness, &observation);
	observation.origin_q8[0] += 271;
	observation.origin_q8[1] += 271;
	CHECK(SG_PushLiveBegin(&state, &witness, &observation));

	Fixture(&witness, &observation);
	observation.origin_q8[0] += 272;
	observation.origin_q8[1] += 272;
	CHECK(!SG_PushLiveBegin(&state, &witness, &observation));
}

static void TestSourceSettleAndAdmission(void)
{
#ifdef SG_PUSH_OBSERVATION_SOURCE_STATE
	sg_push_witness_t witness;
	sg_push_observation_t observation;
	sg_push_live_state_t state;
	Fixture(&witness, &observation);
	observation.at_rest = false;
	CHECK(SG_PushLiveBegin(&state, &witness, &observation));
	CHECK(state.phase == SG_PUSH_SETTLE);
	CHECK(SG_PushLiveCommand(&state, &observation) == SG_PUSH_COMMAND_ZERO);
	CHECK(state.phase == SG_PUSH_SETTLE);
	CHECK(SG_PushLiveStep(&state, SG_PUSH_STEP_MS));
	observation.at_rest = true;
	CHECK(SG_PushLiveCommand(&state, &observation) == SG_PUSH_COMMAND_ZERO);
	CHECK(state.phase == SG_PUSH_APPROACH);

	Fixture(&witness, &observation);
	observation.immutable_support = false;
	CHECK(!SG_PushLiveBegin(&state, &witness, &observation));
	CHECK(state.failure == SG_PUSH_FAILURE_SOURCE);

	Fixture(&witness, &observation);
	observation.ordinary_control = false;
	CHECK(!SG_PushLiveBegin(&state, &witness, &observation));
	CHECK(state.failure == SG_PUSH_FAILURE_SOURCE);
#else
	CHECK(0 && "push source observation state is missing");
#endif
}

static void TestSourceSettleDriftAndTimeout(void)
{
#ifdef SG_PUSH_OBSERVATION_SOURCE_STATE
	sg_push_witness_t witness;
	sg_push_observation_t observation;
	sg_push_live_state_t state;
	int step;

	Fixture(&witness, &observation);
	observation.at_rest = false;
	CHECK(SG_PushLiveBegin(&state, &witness, &observation));
	observation.origin_q8[0] += SG_PUSH_SOURCE_RADIUS_Q8;
	CHECK(SG_PushLiveCommand(&state, &observation) == SG_PUSH_COMMAND_ZERO);
	CHECK(state.phase == SG_PUSH_FAILED);
	CHECK(state.failure == SG_PUSH_FAILURE_SOURCE);

	Fixture(&witness, &observation);
	observation.at_rest = false;
	CHECK(SG_PushLiveBegin(&state, &witness, &observation));
	for (step = 0; step < SG_PUSH_SETTLE_LIMIT_MS / SG_PUSH_STEP_MS; step++)
	{
		CHECK(SG_PushLiveCommand(&state, &observation) ==
			SG_PUSH_COMMAND_ZERO);
		CHECK(SG_PushLiveStep(&state, SG_PUSH_STEP_MS));
	}
	CHECK(!SG_PushLiveStep(&state, SG_PUSH_STEP_MS));
	CHECK(state.failure == SG_PUSH_FAILURE_TIMEOUT);
#else
	CHECK(0 && "push source settle state is missing");
#endif
}

static void TestSourceSettleAuthenticatedTouch(void)
{
#ifdef SG_PUSH_OBSERVATION_SOURCE_STATE
	sg_push_witness_t witness;
	sg_push_observation_t observation;
	sg_push_live_state_t state;
	float wrong_impulse[3];

	Fixture(&witness, &observation);
	observation.at_rest = false;
	CHECK(SG_PushLiveBegin(&state, &witness, &observation));
	CHECK(SG_PushLiveTouched(&state, witness.entry_key,
		witness.push_velocity));
	CHECK(state.phase == SG_PUSH_FLIGHT);

	Fixture(&witness, &observation);
	observation.at_rest = false;
	CHECK(SG_PushLiveBegin(&state, &witness, &observation));
	CHECK(!SG_PushLiveTouched(&state, witness.entry_key + 1U,
		witness.push_velocity));
	CHECK(state.failure == SG_PUSH_FAILURE_TOUCH);

	Fixture(&witness, &observation);
	observation.at_rest = false;
	memcpy(wrong_impulse, witness.push_velocity, sizeof(wrong_impulse));
	wrong_impulse[1] += 1.0f;
	CHECK(SG_PushLiveBegin(&state, &witness, &observation));
	CHECK(!SG_PushLiveTouched(&state, witness.entry_key, wrong_impulse));
	CHECK(state.failure == SG_PUSH_FAILURE_IMPULSE);
#else
	CHECK(0 && "push source settle touch state is missing");
#endif
}

static void TestExactTouchAndZeroFlight(void)
{
	sg_push_witness_t witness;
	sg_push_observation_t observation;
	sg_push_live_state_t state;

	Fixture(&witness, &observation);
	CHECK(SG_PushLiveBegin(&state, &witness, &observation));
	CHECK(state.phase == SG_PUSH_APPROACH);
	CHECK(SG_PushLiveOwns(&state));
	CHECK(SG_PushLiveCommand(&state, &observation) == SG_PUSH_COMMAND_ZERO);
	CHECK(SG_PushLiveTouched(&state, witness.entry_key,
		witness.push_velocity));
	CHECK(state.phase == SG_PUSH_FLIGHT);
	CHECK(SG_PushLiveTouched(&state, witness.entry_key,
		witness.push_velocity));
	CHECK(state.phase == SG_PUSH_FLIGHT);
	observation.grounded = false;
#ifdef SG_PUSH_OBSERVATION_SOURCE_STATE
	observation.immutable_support = false;
	observation.at_rest = false;
	observation.ordinary_control = false;
#endif
	CHECK(SG_PushLiveCommand(&state, &observation) == SG_PUSH_COMMAND_ZERO);
	CHECK(SG_PushLiveStep(&state, SG_PUSH_STEP_MS));
	CHECK(SG_PushLiveBoundary(&state, false, false));
	CHECK(SG_PushLiveBoundary(&state, true, true));
	CHECK(state.phase == SG_PUSH_COMPLETE);
	CHECK(!SG_PushLiveOwns(&state));
}

static void TestTouchIdentityAndRawBitsFailClosed(void)
{
	sg_push_witness_t witness;
	sg_push_observation_t observation;
	sg_push_live_state_t state;
	float actual[3];

	Fixture(&witness, &observation);
	CHECK(SG_PushLiveBegin(&state, &witness, &observation));
	CHECK(!SG_PushLiveTouched(&state, witness.entry_key + 1U,
		witness.push_velocity));
	CHECK(state.failure == SG_PUSH_FAILURE_TOUCH);

	Fixture(&witness, &observation);
	CHECK(SG_PushLiveBegin(&state, &witness, &observation));
	memcpy(actual, witness.push_velocity, sizeof(actual));
	actual[0] = -0.0f;
	CHECK(!SG_PushLiveTouched(&state, witness.entry_key, actual));
	CHECK(state.failure == SG_PUSH_FAILURE_IMPULSE);

	Fixture(&witness, &observation);
	CHECK(SG_PushLiveBegin(&state, &witness, &observation));
	CHECK(SG_PushLiveTouched(&state, witness.entry_key,
		witness.push_velocity));
	CHECK(!SG_PushLiveTouched(&state, witness.entry_key + 1U,
		witness.push_velocity));
	CHECK(state.failure == SG_PUSH_FAILURE_TOUCH);
}

static void TestBadWitnessAndTimeout(void)
{
	sg_push_witness_t witness;
	sg_push_observation_t observation;
	sg_push_live_state_t state;

	Fixture(&witness, &observation);
	witness.push_velocity[2] = NAN;
	CHECK(!SG_PushLiveBegin(&state, &witness, &observation));
	CHECK(state.failure == SG_PUSH_FAILURE_WITNESS);

	Fixture(&witness, &observation);
	CHECK(SG_PushLiveBegin(&state, &witness, &observation));
	CHECK(SG_PushLiveTouched(&state, witness.entry_key,
		witness.push_velocity));
	CHECK(!SG_PushLiveStep(&state,
		(int)witness.cost_ms + SG_PUSH_ARRIVAL_GRACE_MS + 1));
	CHECK(state.failure == SG_PUSH_FAILURE_TIMEOUT);
}

static void TestDeadArrivalFails(void)
{
	sg_push_witness_t witness;
	sg_push_observation_t observation;
	sg_push_live_state_t state;

	Fixture(&witness, &observation);
	CHECK(SG_PushLiveBegin(&state, &witness, &observation));
	CHECK(SG_PushLiveTouched(&state, witness.entry_key,
		witness.push_velocity));
	observation.grounded = false;
	CHECK(SG_PushLiveCommand(&state, &observation) == SG_PUSH_COMMAND_ZERO);
	CHECK(SG_PushLiveBoundary(&state, false, false));
	observation.alive = false;
	observation.grounded = true;
	CHECK(SG_PushLiveCommand(&state, &observation) == SG_PUSH_COMMAND_ZERO);
	CHECK(state.phase == SG_PUSH_FAILED);
	CHECK(!SG_PushLiveBoundary(&state, true, true));
	CHECK(state.phase != SG_PUSH_COMPLETE);
}

static void TestLandingHealthRequirement(void)
{
	float landing_velocity;
	float production_delta;
	int production_damage;
	int minimum_health = -1;

	CHECK(SG_PushMinimumHealth(-360.0f, -303.0f, 846.765747f, 800.0f,
		true, &minimum_health));
	CHECK(minimum_health == 38);
	landing_velocity = -sqrtf(846.765747f * 846.765747f +
		2.0f * 800.0f * (-360.0f - -303.0f + 72.0f));
	production_delta = P_FallDelta(landing_velocity, 0.0f, true, 0);
	production_damage = (int)((production_delta - 30.0f) * 0.5f);
	if (production_damage < 1)
		production_damage = 1;
	CHECK(minimum_health == production_damage + SG_PUSH_HEALTH_RESERVE + 1);
	CHECK(SG_PushMinimumHealth(0.0f, 0.0f, 0.0f, 800.0f, true,
		&minimum_health));
	CHECK(minimum_health == 16);
	CHECK(SG_PushMinimumHealth(115.5f, 0.0f, 0.0f, 800.0f, true,
		&minimum_health));
	CHECK(minimum_health == 16);
	CHECK(SG_PushMinimumHealth(116.5f, 0.0f, 0.0f, 800.0f, true,
		&minimum_health));
	CHECK(minimum_health == 17);
	CHECK(SG_PushMinimumHealth(272.0f, 0.0f, 0.0f, 800.0f, true,
		&minimum_health));
	CHECK(minimum_health == 28);
	CHECK(SG_PushMinimumHealth(-360.0f, -303.0f, 846.765747f, 800.0f,
		false, &minimum_health));
	CHECK(minimum_health == 1);
	CHECK(!SG_PushMinimumHealth(NAN, -303.0f, 846.765747f, 800.0f,
		true, &minimum_health));
}

static void TestArrivalEnvelope(void)
{
	short destination[3] = { 0, 0, 0 };
	short origin[3] = { 383, 0, 0 };

	CHECK(SG_PushArrivalEnvelope(origin, destination));
	origin[0] = 384;
	CHECK(!SG_PushArrivalEnvelope(origin, destination));
	origin[0] = 0;
	origin[2] = SG_PUSH_ARRIVAL_Z_Q8;
	CHECK(SG_PushArrivalEnvelope(origin, destination));
	origin[2]++;
	CHECK(!SG_PushArrivalEnvelope(origin, destination));
}

int main(void)
{
	TestNormalRunHandoffSourceEnvelope();
	TestSourceSettleAndAdmission();
	TestSourceSettleDriftAndTimeout();
	TestSourceSettleAuthenticatedTouch();
	TestExactTouchAndZeroFlight();
	TestTouchIdentityAndRawBitsFailClosed();
	TestBadWitnessAndTimeout();
	TestDeadArrivalFails();
	TestLandingHealthRequirement();
	TestArrivalEnvelope();
	if (failures)
	{
		fprintf(stderr, "sg_push_live_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("sg_push_live_test: ok");
	return 0;
}
