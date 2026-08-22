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
	witness.activation = SG_TRAIN_GATE_ACTIVATION_TOUCH;
	witness.opening_bound_ms = 2000U;
	return witness;
}

static sg_train_gate_witness_t ShootWitness(void)
{
	sg_train_gate_witness_t witness = Witness();

	witness.activation = SG_TRAIN_GATE_ACTIVATION_SHOOT;
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

static void TestSweepSides(void)
{
	const float sweep_mins[3] = { -32.0f, -48.0f, 0.0f };
	const float sweep_maxs[3] = { 32.0f, 48.0f, 256.0f };
	const float west_mins[3] = { -80.0f, -16.0f, 24.0f };
	const float west_maxs[3] = { -48.0f, 16.0f, 80.0f };
	const float east_mins[3] = { 48.0f, -16.0f, 24.0f };
	const float east_maxs[3] = { 80.0f, 16.0f, 80.0f };
	const float north_mins[3] = { -16.0f, 64.0f, 24.0f };
	const float north_maxs[3] = { 16.0f, 96.0f, 80.0f };
	const float south_mins[3] = { -16.0f, -96.0f, 24.0f };
	const float south_maxs[3] = { 16.0f, -64.0f, 80.0f };
	const float inside_mins[3] = { -16.0f, -16.0f, 24.0f };
	const float inside_maxs[3] = { 16.0f, 16.0f, 80.0f };
	const float corner_mins[3] = { -80.0f, 64.0f, 24.0f };
	const float corner_maxs[3] = { -48.0f, 96.0f, 80.0f };
	const float below_mins[3] = { -16.0f, -16.0f, -64.0f };
	const float below_maxs[3] = { 16.0f, 16.0f, -8.0f };
	const float above_mins[3] = { -16.0f, -16.0f, 272.0f };
	const float above_maxs[3] = { 16.0f, 16.0f, 328.0f };

	CHECK(SG_TrainGateSweepSide(west_mins, west_maxs, sweep_mins,
	    sweep_maxs) == SG_TRAIN_GATE_SIDE_X_MIN);
	CHECK(SG_TrainGateSweepSide(east_mins, east_maxs, sweep_mins,
	    sweep_maxs) == SG_TRAIN_GATE_SIDE_X_MAX);
	CHECK(SG_TrainGateSweepSide(north_mins, north_maxs, sweep_mins,
	    sweep_maxs) == SG_TRAIN_GATE_SIDE_Y_MAX);
	CHECK(SG_TrainGateSweepSide(south_mins, south_maxs, sweep_mins,
	    sweep_maxs) == SG_TRAIN_GATE_SIDE_Y_MIN);
	CHECK(SG_TrainGateSweepSide(inside_mins, inside_maxs, sweep_mins,
	    sweep_maxs) == SG_TRAIN_GATE_SIDE_NONE);
	CHECK(SG_TrainGateSweepSide(corner_mins, corner_maxs, sweep_mins,
	    sweep_maxs) == SG_TRAIN_GATE_SIDE_NONE);
	CHECK(SG_TrainGateSweepSide(below_mins, below_maxs, sweep_mins,
	    sweep_maxs) == SG_TRAIN_GATE_SIDE_Z_MIN);
	CHECK(SG_TrainGateSweepSide(above_mins, above_maxs, sweep_mins,
	    sweep_maxs) == SG_TRAIN_GATE_SIDE_Z_MAX);
	CHECK(SG_TrainGateOppositeSide(SG_TRAIN_GATE_SIDE_X_MIN) ==
	    SG_TRAIN_GATE_SIDE_X_MAX);
	CHECK(SG_TrainGateOppositeSide(SG_TRAIN_GATE_SIDE_Y_MAX) ==
	    SG_TRAIN_GATE_SIDE_Y_MIN);
	CHECK(SG_TrainGateOppositeSide(SG_TRAIN_GATE_SIDE_Z_MIN) ==
	    SG_TRAIN_GATE_SIDE_Z_MAX);
	CHECK(SG_TrainGateOppositeSide(SG_TRAIN_GATE_SIDE_NONE) ==
	    SG_TRAIN_GATE_SIDE_NONE);
	CHECK(SG_TrainGateSweepAxisSide(corner_mins, corner_maxs, sweep_mins,
	    sweep_maxs, 0U) == SG_TRAIN_GATE_SIDE_X_MIN);
	CHECK(SG_TrainGateSweepAxisSide(corner_mins, corner_maxs, sweep_mins,
	    sweep_maxs, 1U) == SG_TRAIN_GATE_SIDE_Y_MAX);
	CHECK(SG_TrainGateSweepAxisSide(below_mins, below_maxs, sweep_mins,
	    sweep_maxs, 2U) == SG_TRAIN_GATE_SIDE_Z_MIN);
	CHECK(SG_TrainGateSweepAxisSide(inside_mins, inside_maxs, sweep_mins,
	    sweep_maxs, 2U) == SG_TRAIN_GATE_SIDE_NONE);
	CHECK(SG_TrainGateSweepAxisSide(west_mins, west_maxs, sweep_mins,
	    sweep_maxs, 3U) == SG_TRAIN_GATE_SIDE_NONE);
	CHECK(SG_TrainGateUniqueSourceSide(
	    1U << SG_TRAIN_GATE_SIDE_Z_MIN) == SG_TRAIN_GATE_SIDE_Z_MIN);
	CHECK(SG_TrainGateUniqueSourceSide(
	    (1U << SG_TRAIN_GATE_SIDE_Z_MIN) |
	    (1U << SG_TRAIN_GATE_SIDE_Z_MAX)) == SG_TRAIN_GATE_SIDE_NONE);
	CHECK(SG_TrainGateUniqueSourceSide(0U) == SG_TRAIN_GATE_SIDE_NONE);
	CHECK(SG_TrainGateUniqueSourceSide(1U << 31) ==
	    SG_TRAIN_GATE_SIDE_NONE);
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

static void TestShootHappyPath(void)
{
	sg_train_gate_state_t state;
	sg_train_gate_witness_t witness = ShootWitness();
	sg_train_gate_observation_t observation =
		Observation(SG_TRAIN_GATE_POSE_CLOSED);

	CHECK(SG_TrainGateLiveBegin(&state, &witness, &observation));
	CHECK(SG_TrainGateLiveStep(&state, &observation, 25U) ==
		SG_TRAIN_GATE_COMMAND_EQUIP);
	observation.weapon_ready = 1U;
	CHECK(SG_TrainGateLiveStep(&state, &observation, 25U) ==
		SG_TRAIN_GATE_COMMAND_AIM_BUTTON);
	observation.aim_contact_current = 1U;
	observation.line_of_fire_clear = 1U;
	CHECK(SG_TrainGateLiveStep(&state, &observation, 25U) ==
		SG_TRAIN_GATE_COMMAND_SHOOT_BUTTON);
	CHECK(state.shot_requested == 1U);
	CHECK(SG_TrainGateLiveStep(&state, &observation, 25U) ==
		SG_TRAIN_GATE_COMMAND_ZERO);
	observation.button_shot_count = 1U;
	CHECK(SG_TrainGateLiveStep(&state, &observation, 25U) ==
		SG_TRAIN_GATE_COMMAND_ZERO);
	observation.target_dispatch_count = 1U;
	CHECK(SG_TrainGateLiveStep(&state, &observation, 25U) ==
		SG_TRAIN_GATE_COMMAND_ZERO);
	observation.train_use_count = 1U;
	observation.pose = SG_TRAIN_GATE_POSE_OPENING;
	CHECK(SG_TrainGateLiveStep(&state, &observation, 25U) ==
		SG_TRAIN_GATE_COMMAND_ZERO);
	CHECK(state.phase == SG_TRAIN_GATE_OPENING);
	observation.pose = SG_TRAIN_GATE_POSE_OPEN;
	CHECK(SG_TrainGateLiveStep(&state, &observation, 25U) ==
		SG_TRAIN_GATE_COMMAND_TO_EGRESS);
	observation.body_clear = 1U;
	observation.arrived = 1U;
	CHECK(SG_TrainGateLiveStep(&state, &observation, 25U) ==
		SG_TRAIN_GATE_COMMAND_ZERO);
	CHECK(state.phase == SG_TRAIN_GATE_COMPLETE);
}

static void TestActivationMethodsStayDistinct(void)
{
	sg_train_gate_state_t state;
	sg_train_gate_witness_t witness = Witness();
	sg_train_gate_observation_t observation =
		Observation(SG_TRAIN_GATE_POSE_CLOSED);

	CHECK(SG_TrainGateLiveBegin(&state, &witness, &observation));
	observation.button_shot_count = 1U;
	CHECK(SG_TrainGateLiveStep(&state, &observation, 25U) ==
		SG_TRAIN_GATE_COMMAND_ZERO);
	CHECK(state.phase == SG_TRAIN_GATE_FAILED);

	witness = ShootWitness();
	observation = Observation(SG_TRAIN_GATE_POSE_CLOSED);
	CHECK(SG_TrainGateLiveBegin(&state, &witness, &observation));
	observation.button_touch_count = 1U;
	CHECK(SG_TrainGateLiveStep(&state, &observation, 25U) ==
		SG_TRAIN_GATE_COMMAND_ZERO);
	CHECK(state.phase == SG_TRAIN_GATE_FAILED);

	observation = Observation(SG_TRAIN_GATE_POSE_CLOSED);
	CHECK(SG_TrainGateLiveBegin(&state, &witness, &observation));
	observation.weapon_ready = 1U;
	observation.aim_contact_current = 1U;
	CHECK(SG_TrainGateLiveStep(&state, &observation, 25U) ==
		SG_TRAIN_GATE_COMMAND_AIM_BUTTON);
	CHECK(state.shot_requested == 0U);
	observation.line_of_fire_clear = 1U;
	CHECK(SG_TrainGateLiveStep(&state, &observation, 25U) ==
		SG_TRAIN_GATE_COMMAND_SHOOT_BUTTON);
	CHECK(SG_TrainGateLiveStep(&state, &observation, 2000U) ==
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
	CHECK(SG_TrainGateLiveStep(&state, &observation, 2000U) ==
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
	TestSweepSides();
	TestHappyPath();
	TestOrderedSingleCallbacks();
	TestShootHappyPath();
	TestActivationMethodsStayDistinct();
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
