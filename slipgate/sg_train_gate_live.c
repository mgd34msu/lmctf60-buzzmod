/* sg_train_gate_live.c -- host-free train-gate transaction reducer. */
#include "sg_train_gate_live.h"

#include <string.h>

static int TrainBoolean(uint8_t value)
{
	return value <= 1U;
}

static int TrainWitnessValid(const sg_train_gate_witness_t *witness)
{
	uint32_t keys[4];
	unsigned int first;
	unsigned int second;

	if (!witness || witness->opening_bound_ms == 0U ||
	    (witness->activation != SG_TRAIN_GATE_ACTIVATION_TOUCH &&
	     witness->activation != SG_TRAIN_GATE_ACTIVATION_SHOOT))
		return 0;
	keys[0] = witness->button_key;
	keys[1] = witness->train_key;
	keys[2] = witness->closed_corner_key;
	keys[3] = witness->open_corner_key;
	for (first = 0U; first < 4U; first++)
	{
		if (keys[first] == 0U || keys[first] == UINT32_MAX)
			return 0;
		for (second = first + 1U; second < 4U; second++)
			if (keys[first] == keys[second])
				return 0;
	}
	return 1;
}

static int TrainObservationShapeValid(
	const sg_train_gate_observation_t *observation)
{
	return observation && observation->pose >= SG_TRAIN_GATE_POSE_CLOSED &&
	       observation->pose <= SG_TRAIN_GATE_POSE_INVALID &&
	       TrainBoolean(observation->alive) &&
	       TrainBoolean(observation->supported) &&
	       TrainBoolean(observation->dry) &&
	       TrainBoolean(observation->binding_current) &&
	       TrainBoolean(observation->body_clear) &&
	       TrainBoolean(observation->arrived) &&
	       TrainBoolean(observation->weapon_ready) &&
	       TrainBoolean(observation->aim_contact_current) &&
	       TrainBoolean(observation->line_of_fire_clear) &&
	       observation->button_touch_count <= 1U &&
	       observation->button_shot_count <= 1U &&
	       observation->target_dispatch_count <= 1U &&
	       observation->train_use_count <= 1U;
}

static int TrainObservationCurrent(
	const sg_train_gate_observation_t *observation)
{
	return TrainObservationShapeValid(observation) && observation->alive == 1U &&
	       observation->supported == 1U && observation->dry == 1U &&
	       observation->binding_current == 1U;
}

static void TrainFail(sg_train_gate_state_t *state)
{
	if (state)
		state->phase = SG_TRAIN_GATE_FAILED;
}

static int TrainCallbacksAdvance(sg_train_gate_state_t *state,
	const sg_train_gate_observation_t *observation)
{
	if (!state || !observation ||
	    observation->button_touch_count < state->button_touch_count ||
	    observation->button_shot_count < state->button_shot_count ||
	    observation->target_dispatch_count < state->target_dispatch_count ||
	    observation->train_use_count < state->train_use_count)
		return 0;
	if (state->witness.activation == SG_TRAIN_GATE_ACTIVATION_TOUCH)
	{
		if (observation->button_shot_count != 0U ||
		    observation->button_touch_count <
	        observation->target_dispatch_count ||
		    observation->target_dispatch_count < observation->train_use_count)
			return 0;
	}
	else if (observation->button_touch_count != 0U ||
	         observation->button_shot_count <
	             observation->target_dispatch_count ||
	         observation->target_dispatch_count < observation->train_use_count ||
	         (observation->button_shot_count != 0U &&
	          state->shot_requested == 0U))
		return 0;
	state->button_touch_count = observation->button_touch_count;
	state->button_shot_count = observation->button_shot_count;
	state->target_dispatch_count = observation->target_dispatch_count;
	state->train_use_count = observation->train_use_count;
	return 1;
}

static int TrainActivated(const sg_train_gate_state_t *state)
{
	return state &&
	       ((state->witness.activation == SG_TRAIN_GATE_ACTIVATION_TOUCH &&
	         state->button_touch_count == 1U) ||
	        (state->witness.activation == SG_TRAIN_GATE_ACTIVATION_SHOOT &&
	         state->button_shot_count == 1U));
}

static int TrainOpeningClock(sg_train_gate_state_t *state, uint16_t step_ms)
{
	if (!state)
		return 0;
	if (UINT32_MAX - state->opening_elapsed_ms < step_ms)
		return 0;
	state->opening_elapsed_ms += step_ms;
	return state->opening_elapsed_ms <= state->witness.opening_bound_ms;
}

int SG_TrainGateLiveBegin(sg_train_gate_state_t *state,
	const sg_train_gate_witness_t *witness,
	const sg_train_gate_observation_t *observation)
{
	if (!state)
		return 0;
	memset(state, 0, sizeof(*state));
	if (!TrainWitnessValid(witness) || !TrainObservationCurrent(observation) ||
	    observation->pose != SG_TRAIN_GATE_POSE_CLOSED ||
	    observation->button_touch_count != 0U ||
	    observation->button_shot_count != 0U ||
	    observation->target_dispatch_count != 0U ||
	    observation->train_use_count != 0U)
	{
		TrainFail(state);
		return 0;
	}
	state->witness = *witness;
	state->phase = SG_TRAIN_GATE_APPROACH;
	return 1;
}

sg_train_gate_command_t SG_TrainGateLiveStep(sg_train_gate_state_t *state,
	const sg_train_gate_observation_t *observation, uint16_t step_ms)
{
	if (!state || state->phase == SG_TRAIN_GATE_IDLE ||
	    state->phase == SG_TRAIN_GATE_FAILED ||
	    state->phase == SG_TRAIN_GATE_COMPLETE)
		return SG_TRAIN_GATE_COMMAND_ZERO;
	if (!TrainObservationCurrent(observation) ||
	    !TrainCallbacksAdvance(state, observation))
	{
		TrainFail(state);
		return SG_TRAIN_GATE_COMMAND_ZERO;
	}

	if (state->phase == SG_TRAIN_GATE_APPROACH)
	{
		if (!TrainActivated(state))
		{
			if (observation->pose != SG_TRAIN_GATE_POSE_CLOSED)
				TrainFail(state);
			if (state->phase == SG_TRAIN_GATE_FAILED)
				return SG_TRAIN_GATE_COMMAND_ZERO;
			if (state->witness.activation == SG_TRAIN_GATE_ACTIVATION_TOUCH)
				return SG_TRAIN_GATE_COMMAND_TO_BUTTON;
			if (!TrainOpeningClock(state, step_ms))
			{
				TrainFail(state);
				return SG_TRAIN_GATE_COMMAND_ZERO;
			}
			if (observation->weapon_ready == 0U)
				return SG_TRAIN_GATE_COMMAND_EQUIP;
			if (observation->aim_contact_current == 0U ||
			    observation->line_of_fire_clear == 0U)
				return SG_TRAIN_GATE_COMMAND_AIM_BUTTON;
			if (state->shot_requested == 0U)
			{
				state->shot_requested = 1U;
				return SG_TRAIN_GATE_COMMAND_SHOOT_BUTTON;
			}
			return SG_TRAIN_GATE_COMMAND_ZERO;
		}
		state->phase = SG_TRAIN_GATE_DISPATCH;
	}

	if (state->phase == SG_TRAIN_GATE_DISPATCH)
	{
		if (!TrainOpeningClock(state, step_ms))
		{
			TrainFail(state);
			return SG_TRAIN_GATE_COMMAND_ZERO;
		}
		if (state->train_use_count == 0U)
		{
			if (observation->pose != SG_TRAIN_GATE_POSE_CLOSED)
				TrainFail(state);
			return SG_TRAIN_GATE_COMMAND_ZERO;
		}
		if (state->target_dispatch_count != 1U ||
		    observation->pose != SG_TRAIN_GATE_POSE_OPENING)
		{
			TrainFail(state);
			return SG_TRAIN_GATE_COMMAND_ZERO;
		}
		state->phase = SG_TRAIN_GATE_OPENING;
		return SG_TRAIN_GATE_COMMAND_ZERO;
	}

	if (state->phase == SG_TRAIN_GATE_OPENING)
	{
		if (!TrainOpeningClock(state, step_ms) ||
		    !TrainActivated(state) ||
		    state->target_dispatch_count != 1U || state->train_use_count != 1U)
		{
			TrainFail(state);
			return SG_TRAIN_GATE_COMMAND_ZERO;
		}
		if (observation->pose == SG_TRAIN_GATE_POSE_OPENING)
			return SG_TRAIN_GATE_COMMAND_ZERO;
		if (observation->pose != SG_TRAIN_GATE_POSE_OPEN)
		{
			TrainFail(state);
			return SG_TRAIN_GATE_COMMAND_ZERO;
		}
		state->phase = SG_TRAIN_GATE_EGRESS;
	}

	if (state->phase == SG_TRAIN_GATE_EGRESS)
	{
		if (observation->pose != SG_TRAIN_GATE_POSE_OPEN ||
		    !TrainActivated(state) ||
		    state->target_dispatch_count != 1U || state->train_use_count != 1U)
		{
			TrainFail(state);
			return SG_TRAIN_GATE_COMMAND_ZERO;
		}
		if (observation->arrived == 1U && observation->body_clear == 1U)
		{
			state->phase = SG_TRAIN_GATE_COMPLETE;
			return SG_TRAIN_GATE_COMMAND_ZERO;
		}
		return SG_TRAIN_GATE_COMMAND_TO_EGRESS;
	}

	TrainFail(state);
	return SG_TRAIN_GATE_COMMAND_ZERO;
}
