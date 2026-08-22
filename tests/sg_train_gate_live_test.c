#include <stdio.h>
#include <string.h>

#include "slipgate/sg_train_gate_live.h"

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		    __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static sg_train_gate_witness_t Witness(void)
{
	sg_train_gate_witness_t witness;

	memset(&witness, 0, sizeof(witness));
	witness.link_index = 7U;
	witness.button_key = 10U;
	witness.train_key = 20U;
	witness.closed_corner_key = 30U;
	witness.open_corner_key = 40U;
	witness.opening_bound_ms = 2000U;
	return witness;
}

static sg_train_gate_observation_t Observation(sg_train_gate_pose_t pose)
{
	sg_train_gate_observation_t observation;

	memset(&observation, 0, sizeof(observation));
	observation.pose = pose;
	observation.alive = 1U;
	observation.supported = 1U;
	observation.dry = 1U;
	observation.binding_current = 1U;
	return observation;
}

static void Activate(sg_train_gate_state_t *state,
	sg_train_gate_observation_t *observation)
{
	observation->button_touch_count = 1U;
	CHECK(SG_TrainGateLiveStep(state, observation, 25U) ==
		SG_TRAIN_GATE_COMMAND_ZERO);
	observation->target_dispatch_count = 1U;
	CHECK(SG_TrainGateLiveStep(state, observation, 25U) ==
		SG_TRAIN_GATE_COMMAND_ZERO);
	observation->train_use_count = 1U;
	observation->pose = SG_TRAIN_GATE_POSE_OPENING;
	CHECK(SG_TrainGateLiveStep(state, observation, 25U) ==
		SG_TRAIN_GATE_COMMAND_ZERO);
	CHECK(state->phase == SG_TRAIN_GATE_OPENING);
}

static void TestHappyPath(void)
{
	sg_train_gate_state_t state;
	sg_train_gate_witness_t witness = Witness();
	sg_train_gate_observation_t observation =
		Observation(SG_TRAIN_GATE_POSE_CLOSED);

	CHECK(SG_TrainGateLiveBegin(&state, &witness, &observation));
	CHECK(state.phase == SG_TRAIN_GATE_APPROACH);
	CHECK(SG_TrainGateLiveStep(&state, &observation, 25U) ==
		SG_TRAIN_GATE_COMMAND_TO_BUTTON);
	Activate(&state, &observation);
	observation.pose = SG_TRAIN_GATE_POSE_OPEN;
	CHECK(SG_TrainGateLiveStep(&state, &observation, 25U) ==
		SG_TRAIN_GATE_COMMAND_TO_EGRESS);
	CHECK(state.phase == SG_TRAIN_GATE_EGRESS);
	observation.body_clear = 1U;
	CHECK(SG_TrainGateLiveStep(&state, &observation, 25U) ==
		SG_TRAIN_GATE_COMMAND_TO_EGRESS);
	observation.arrived = 1U;
	CHECK(SG_TrainGateLiveStep(&state, &observation, 25U) ==
		SG_TRAIN_GATE_COMMAND_ZERO);
	CHECK(state.phase == SG_TRAIN_GATE_COMPLETE);
}

static void TestOrderedSingleCallbacks(void)
{
	sg_train_gate_state_t state;
	sg_train_gate_witness_t witness = Witness();
	sg_train_gate_observation_t observation =
		Observation(SG_TRAIN_GATE_POSE_CLOSED);

	CHECK(SG_TrainGateLiveBegin(&state, &witness, &observation));
	observation.target_dispatch_count = 1U;
	CHECK(SG_TrainGateLiveStep(&state, &observation, 25U) ==
		SG_TRAIN_GATE_COMMAND_ZERO);
	CHECK(state.phase == SG_TRAIN_GATE_FAILED);

	observation = Observation(SG_TRAIN_GATE_POSE_CLOSED);
	CHECK(SG_TrainGateLiveBegin(&state, &witness, &observation));
	observation.button_touch_count = 1U;
	observation.target_dispatch_count = 1U;
	observation.train_use_count = 1U;
	observation.pose = SG_TRAIN_GATE_POSE_OPENING;
	CHECK(SG_TrainGateLiveStep(&state, &observation, 25U) ==
		SG_TRAIN_GATE_COMMAND_ZERO);
	CHECK(state.phase == SG_TRAIN_GATE_OPENING);
	observation.train_use_count = 2U;
	CHECK(SG_TrainGateLiveStep(&state, &observation, 25U) ==
		SG_TRAIN_GATE_COMMAND_ZERO);
	CHECK(state.phase == SG_TRAIN_GATE_FAILED);
}

static void TestInvalidObservations(void)
{
	sg_train_gate_state_t state;
	sg_train_gate_witness_t witness = Witness();
	sg_train_gate_observation_t observation;
	int field;

	for (field = 0; field < 4; field++)
	{
		observation = Observation(SG_TRAIN_GATE_POSE_CLOSED);
		if (field == 0) observation.alive = 0U;
		if (field == 1) observation.supported = 0U;
		if (field == 2) observation.dry = 0U;
		if (field == 3) observation.binding_current = 0U;
		CHECK(!SG_TrainGateLiveBegin(&state, &witness, &observation));
		CHECK(state.phase == SG_TRAIN_GATE_FAILED);
	}

	observation = Observation(SG_TRAIN_GATE_POSE_CLOSED);
	CHECK(SG_TrainGateLiveBegin(&state, &witness, &observation));
	Activate(&state, &observation);
	for (field = SG_TRAIN_GATE_POSE_CLOSING;
	     field <= SG_TRAIN_GATE_POSE_INVALID; field++)
	{
		sg_train_gate_state_t trial = state;
		observation.pose = (sg_train_gate_pose_t)field;
		CHECK(SG_TrainGateLiveStep(&trial, &observation, 25U) ==
			SG_TRAIN_GATE_COMMAND_ZERO);
		CHECK(trial.phase == SG_TRAIN_GATE_FAILED);
	}
}

static void TestDriftTimeoutAndClearance(void)
{
	sg_train_gate_state_t state;
	sg_train_gate_witness_t witness = Witness();
	sg_train_gate_observation_t observation =
		Observation(SG_TRAIN_GATE_POSE_CLOSED);

	CHECK(SG_TrainGateLiveBegin(&state, &witness, &observation));
	Activate(&state, &observation);
	CHECK(SG_TrainGateLiveStep(&state, &observation, 1900U) ==
		SG_TRAIN_GATE_COMMAND_ZERO);
	CHECK(state.phase == SG_TRAIN_GATE_FAILED);

	observation = Observation(SG_TRAIN_GATE_POSE_CLOSED);
	CHECK(SG_TrainGateLiveBegin(&state, &witness, &observation));
	Activate(&state, &observation);
	observation.pose = SG_TRAIN_GATE_POSE_OPEN;
	CHECK(SG_TrainGateLiveStep(&state, &observation, 25U) ==
		SG_TRAIN_GATE_COMMAND_TO_EGRESS);
	observation.arrived = 1U;
	CHECK(SG_TrainGateLiveStep(&state, &observation, 25U) ==
		SG_TRAIN_GATE_COMMAND_TO_EGRESS);
	CHECK(state.phase == SG_TRAIN_GATE_EGRESS);
	observation.binding_current = 0U;
	CHECK(SG_TrainGateLiveStep(&state, &observation, 25U) ==
		SG_TRAIN_GATE_COMMAND_ZERO);
	CHECK(state.phase == SG_TRAIN_GATE_FAILED);
}

int main(void)
{
	TestHappyPath();
	TestOrderedSingleCallbacks();
	TestInvalidObservations();
	TestDriftTimeoutAndClearance();
	if (failures)
	{
		fprintf(stderr, "%d train-gate-live test(s) failed\n", failures);
		return 1;
	}
	puts("train-gate-live tests passed");
	return 0;
}
