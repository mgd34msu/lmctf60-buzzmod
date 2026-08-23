#include "slipgate/sg_shoot_door_live.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
		    #expression); \
		failures++; \
	} \
} while (0)

static sg_shoot_door_witness_t Witness(void)
{
	sg_shoot_door_witness_t witness;

	memset(&witness, 0, sizeof(witness));
	witness.link_index = 7U;
	witness.master_key = 11U;
	witness.expected_members = 2U;
	witness.opening_bound_ms = 1000U;
	witness.passage_axis = 0U;
	witness.source_side = SG_SHOOT_DOOR_SIDE_MIN;
	return witness;
}

static sg_shoot_door_observation_t Observation(void)
{
	sg_shoot_door_observation_t observation;

	memset(&observation, 0, sizeof(observation));
	observation.alive = 1U;
	observation.supported = 1U;
	observation.dry = 1U;
	observation.binding_current = 1U;
	observation.team_closed = 1U;
	observation.body_side = SG_SHOOT_DOOR_SIDE_MIN;
	return observation;
}

static void TestHappyPath(void)
{
	sg_shoot_door_state_t state;
	sg_shoot_door_witness_t witness = Witness();
	sg_shoot_door_observation_t observation = Observation();

	CHECK(SG_ShootDoorLiveBegin(&state, &witness, &observation));
	CHECK(SG_ShootDoorLiveStep(&state, &observation, 25U) ==
	    SG_SHOOT_DOOR_COMMAND_EQUIP);
	observation.weapon_ready = 1U;
	CHECK(SG_ShootDoorLiveStep(&state, &observation, 25U) ==
	    SG_SHOOT_DOOR_COMMAND_AIM);
	observation.aim_contact_current = 1U;
	observation.line_of_fire_clear = 1U;
	CHECK(SG_ShootDoorLiveStep(&state, &observation, 25U) ==
	    SG_SHOOT_DOOR_COMMAND_SHOOT);
	CHECK(SG_ShootDoorLiveStep(&state, &observation, 25U) ==
	    SG_SHOOT_DOOR_COMMAND_ZERO);
	observation.shot_count = 1U;
	observation.team_closed = 0U;
	observation.team_opening = 1U;
	CHECK(SG_ShootDoorLiveStep(&state, &observation, 25U) ==
	    SG_SHOOT_DOOR_COMMAND_ZERO);
	observation.team_opening = 0U;
	observation.team_open = 1U;
	observation.hull_to_sweep_gap_q8 = 65U * 8U;
	CHECK(SG_ShootDoorLiveStep(&state, &observation, 25U) ==
	    SG_SHOOT_DOOR_COMMAND_TO_DESTINATION);
	observation.hull_to_sweep_gap_q8 = 64U * 8U;
	CHECK(SG_ShootDoorLiveStep(&state, &observation, 25U) ==
	    SG_SHOOT_DOOR_COMMAND_TO_DESTINATION_JUMP);
	observation.body_side = SG_SHOOT_DOOR_SIDE_MAX;
	CHECK(SG_ShootDoorLiveStep(&state, &observation, 25U) ==
	    SG_SHOOT_DOOR_COMMAND_TO_DESTINATION);
	observation.arrived = 1U;
	observation.body_clear = 1U;
	CHECK(SG_ShootDoorLiveStep(&state, &observation, 25U) ==
	    SG_SHOOT_DOOR_COMMAND_ZERO);
	CHECK(state.phase == SG_SHOOT_DOOR_COMPLETE);
}

static void TestSymmetryAndFailures(void)
{
	sg_shoot_door_state_t state;
	sg_shoot_door_witness_t witness = Witness();
	sg_shoot_door_observation_t observation = Observation();

	witness.source_side = SG_SHOOT_DOOR_SIDE_MAX;
	observation.body_side = SG_SHOOT_DOOR_SIDE_MAX;
	CHECK(SG_ShootDoorLiveBegin(&state, &witness, &observation));
	observation.weapon_ready = 1U;
	observation.aim_contact_current = 1U;
	observation.line_of_fire_clear = 1U;
	CHECK(SG_ShootDoorLiveStep(&state, &observation, 25U) ==
	    SG_SHOOT_DOOR_COMMAND_SHOOT);
	observation.shot_count = 1U;
	observation.team_closed = 0U;
	observation.team_open = 1U;
	observation.hull_to_sweep_gap_q8 = 64U * 8U;
	CHECK(SG_ShootDoorLiveStep(&state, &observation, 25U) ==
	    SG_SHOOT_DOOR_COMMAND_TO_DESTINATION_JUMP);

	observation.shot_count = 0U;
	CHECK(SG_ShootDoorLiveStep(&state, &observation, 25U) ==
	    SG_SHOOT_DOOR_COMMAND_ZERO);
	CHECK(state.phase == SG_SHOOT_DOOR_FAILED);

	witness = Witness();
	observation = Observation();
	CHECK(SG_ShootDoorLiveBegin(&state, &witness, &observation));
	observation.binding_current = 2U;
	CHECK(SG_ShootDoorLiveStep(&state, &observation, 25U) ==
	    SG_SHOOT_DOOR_COMMAND_ZERO);
	CHECK(state.phase == SG_SHOOT_DOOR_FAILED);

	witness.expected_members = 0U;
	observation = Observation();
	CHECK(!SG_ShootDoorLiveBegin(&state, &witness, &observation));

	witness = Witness();
	observation = Observation();
	CHECK(SG_ShootDoorLiveBegin(&state, &witness, &observation));
	CHECK(SG_ShootDoorLiveStep(&state, &observation, 1001U) ==
	    SG_SHOOT_DOOR_COMMAND_ZERO);
	CHECK(state.phase == SG_SHOOT_DOOR_FAILED);
}

int main(void)
{
	TestHappyPath();
	TestSymmetryAndFailures();
	if (failures)
		return 1;
	puts("sg_shoot_door_live_test: PASS");
	return 0;
}
