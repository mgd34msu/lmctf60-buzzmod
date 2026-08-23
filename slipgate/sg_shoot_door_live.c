/* sg_shoot_door_live.c -- host-free shoot-door transaction reducer. */
#include "sg_shoot_door_live.h"

#include <string.h>

static int ShootDoorBoolean(uint8_t value)
{
	return value <= 1U;
}

static int ShootDoorWitnessValid(const sg_shoot_door_witness_t *witness)
{
	return witness && witness->master_key != 0U &&
	       witness->master_key != UINT32_MAX && witness->expected_members != 0U &&
	       witness->opening_bound_ms != 0U && witness->passage_axis < 2U &&
	       (witness->source_side == SG_SHOOT_DOOR_SIDE_MIN ||
	        witness->source_side == SG_SHOOT_DOOR_SIDE_MAX);
}

static int ShootDoorObservationValid(
	const sg_shoot_door_observation_t *observation)
{
	unsigned int poses;

	if (!observation || !ShootDoorBoolean(observation->alive) ||
	    !ShootDoorBoolean(observation->supported) ||
	    !ShootDoorBoolean(observation->dry) ||
	    !ShootDoorBoolean(observation->binding_current) ||
	    !ShootDoorBoolean(observation->team_closed) ||
	    !ShootDoorBoolean(observation->team_opening) ||
	    !ShootDoorBoolean(observation->team_open) ||
	    !ShootDoorBoolean(observation->body_clear) ||
	    !ShootDoorBoolean(observation->arrived) ||
	    !ShootDoorBoolean(observation->weapon_ready) ||
	    !ShootDoorBoolean(observation->aim_contact_current) ||
	    !ShootDoorBoolean(observation->line_of_fire_clear) ||
	    observation->shot_count > 1U ||
	    observation->body_side < SG_SHOOT_DOOR_SIDE_NONE ||
	    observation->body_side > SG_SHOOT_DOOR_SIDE_MAX)
		return 0;
	poses = observation->team_closed + observation->team_opening +
		observation->team_open;
	return poses == 1U;
}

static int ShootDoorCurrent(const sg_shoot_door_observation_t *observation)
{
	return ShootDoorObservationValid(observation) && observation->alive == 1U &&
	       observation->supported == 1U && observation->dry == 1U &&
	       observation->binding_current == 1U;
}

static void ShootDoorFail(sg_shoot_door_state_t *state)
{
	if (state)
		state->phase = SG_SHOOT_DOOR_FAILED;
}

static int ShootDoorClock(sg_shoot_door_state_t *state, uint16_t step_ms)
{
	if (!state || UINT32_MAX - state->elapsed_ms < step_ms)
		return 0;
	state->elapsed_ms += step_ms;
	return state->elapsed_ms <= state->witness.opening_bound_ms;
}

int SG_ShootDoorLiveBegin(sg_shoot_door_state_t *state,
	const sg_shoot_door_witness_t *witness,
	const sg_shoot_door_observation_t *observation)
{
	if (!state)
		return 0;
	memset(state, 0, sizeof(*state));
	if (!ShootDoorWitnessValid(witness) || !ShootDoorCurrent(observation) ||
	    observation->team_closed != 1U || observation->shot_count != 0U ||
	    observation->body_side != witness->source_side)
	{
		ShootDoorFail(state);
		return 0;
	}
	state->witness = *witness;
	state->phase = SG_SHOOT_DOOR_ACTIVATE;
	return 1;
}

sg_shoot_door_command_t SG_ShootDoorLiveStep(sg_shoot_door_state_t *state,
	const sg_shoot_door_observation_t *observation, uint16_t step_ms)
{
	if (!state || state->phase == SG_SHOOT_DOOR_IDLE ||
	    state->phase == SG_SHOOT_DOOR_FAILED ||
	    state->phase == SG_SHOOT_DOOR_COMPLETE)
		return SG_SHOOT_DOOR_COMMAND_ZERO;
	if (!ShootDoorCurrent(observation) ||
	    observation->shot_count < state->shot_count ||
	    (observation->shot_count != 0U && state->shot_requested == 0U))
	{
		ShootDoorFail(state);
		return SG_SHOOT_DOOR_COMMAND_ZERO;
	}
	state->shot_count = observation->shot_count;
	if (!ShootDoorClock(state, step_ms))
	{
		ShootDoorFail(state);
		return SG_SHOOT_DOOR_COMMAND_ZERO;
	}

	if (state->phase == SG_SHOOT_DOOR_ACTIVATE)
	{
		if (state->shot_count == 0U)
		{
			if (observation->team_closed != 1U)
			{
				ShootDoorFail(state);
				return SG_SHOOT_DOOR_COMMAND_ZERO;
			}
			if (observation->weapon_ready == 0U)
				return SG_SHOOT_DOOR_COMMAND_EQUIP;
			if (observation->aim_contact_current == 0U ||
			    observation->line_of_fire_clear == 0U)
				return SG_SHOOT_DOOR_COMMAND_AIM;
			if (state->shot_requested == 0U)
			{
				state->shot_requested = 1U;
				return SG_SHOOT_DOOR_COMMAND_SHOOT;
			}
			return SG_SHOOT_DOOR_COMMAND_ZERO;
		}
		state->phase = SG_SHOOT_DOOR_OPENING;
	}

	if (state->phase == SG_SHOOT_DOOR_OPENING)
	{
		if (state->shot_count != 1U || observation->team_closed == 1U)
		{
			ShootDoorFail(state);
			return SG_SHOOT_DOOR_COMMAND_ZERO;
		}
		if (observation->team_opening == 1U)
			return SG_SHOOT_DOOR_COMMAND_ZERO;
		if (observation->team_open != 1U)
		{
			ShootDoorFail(state);
			return SG_SHOOT_DOOR_COMMAND_ZERO;
		}
		state->phase = SG_SHOOT_DOOR_CROSS;
	}

	if (state->phase == SG_SHOOT_DOOR_CROSS)
	{
		if (state->shot_count != 1U || observation->team_open != 1U)
		{
			ShootDoorFail(state);
			return SG_SHOOT_DOOR_COMMAND_ZERO;
		}
		if (observation->arrived == 1U)
		{
			if (observation->body_clear != 1U ||
			    observation->body_side == state->witness.source_side)
			{
				ShootDoorFail(state);
				return SG_SHOOT_DOOR_COMMAND_ZERO;
			}
			state->phase = SG_SHOOT_DOOR_COMPLETE;
			return SG_SHOOT_DOOR_COMMAND_ZERO;
		}
		if (observation->body_side == state->witness.source_side &&
		    observation->hull_to_sweep_gap_q8 <=
		        SG_SHOOT_DOOR_JUMP_APPROACH_Q8)
			return SG_SHOOT_DOOR_COMMAND_TO_DESTINATION_JUMP;
		return SG_SHOOT_DOOR_COMMAND_TO_DESTINATION;
	}
	return SG_SHOOT_DOOR_COMMAND_ZERO;
}
