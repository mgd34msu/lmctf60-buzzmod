/* sg_train_gate_live.c -- host-free train-gate transaction reducer. */
#include "sg_train_gate_live.h"

#include <string.h>

void SG_TrainGateReverseTouchBegin(sg_train_gate_reverse_touch_t *selection)
{
	if (!selection)
		return;
	selection->source = -1;
	selection->destination = -1;
	selection->matches = 0U;
}

void SG_TrainGateReverseTouchConsider(sg_train_gate_reverse_touch_t *selection,
	int preopen_touch, int source, int destination)
{
	if (!selection || !preopen_touch || source < 0 || destination < 0)
		return;
	if (selection->matches == 0U)
	{
		selection->source = source;
		selection->destination = destination;
	}
	if (selection->matches != UINT32_MAX)
		selection->matches++;
}

int SG_TrainGateReverseTouchResult(
	const sg_train_gate_reverse_touch_t *selection,
	int *source_out, int *destination_out)
{
	if (source_out)
		*source_out = -1;
	if (destination_out)
		*destination_out = -1;
	if (!selection || !source_out || !destination_out ||
	    selection->matches != 1U)
		return 0;
	*source_out = selection->source;
	*destination_out = selection->destination;
	return 1;
}

sg_train_gate_side_t SG_TrainGateSweepSide(const float bounds_mins[3],
	const float bounds_maxs[3], const float sweep_mins[3],
	const float sweep_maxs[3])
{
	sg_train_gate_side_t side = SG_TRAIN_GATE_SIDE_NONE;
	unsigned int matches = 0U;

	if (!bounds_mins || !bounds_maxs || !sweep_mins || !sweep_maxs)
		return SG_TRAIN_GATE_SIDE_NONE;
#define SG_TRAIN_GATE_MATCH(candidate, expression) do { \
	if (expression) { side = candidate; matches++; } \
} while (0)
	SG_TRAIN_GATE_MATCH(SG_TRAIN_GATE_SIDE_X_MIN,
	    bounds_maxs[0] <= sweep_mins[0]);
	SG_TRAIN_GATE_MATCH(SG_TRAIN_GATE_SIDE_X_MAX,
	    bounds_mins[0] >= sweep_maxs[0]);
	SG_TRAIN_GATE_MATCH(SG_TRAIN_GATE_SIDE_Y_MIN,
	    bounds_maxs[1] <= sweep_mins[1]);
	SG_TRAIN_GATE_MATCH(SG_TRAIN_GATE_SIDE_Y_MAX,
	    bounds_mins[1] >= sweep_maxs[1]);
	SG_TRAIN_GATE_MATCH(SG_TRAIN_GATE_SIDE_Z_MIN,
	    bounds_maxs[2] <= sweep_mins[2]);
	SG_TRAIN_GATE_MATCH(SG_TRAIN_GATE_SIDE_Z_MAX,
	    bounds_mins[2] >= sweep_maxs[2]);
#undef SG_TRAIN_GATE_MATCH
	return matches == 1U ? side : SG_TRAIN_GATE_SIDE_NONE;
}

sg_train_gate_side_t SG_TrainGateOppositeSide(sg_train_gate_side_t side)
{
	switch (side)
	{
	case SG_TRAIN_GATE_SIDE_X_MIN: return SG_TRAIN_GATE_SIDE_X_MAX;
	case SG_TRAIN_GATE_SIDE_X_MAX: return SG_TRAIN_GATE_SIDE_X_MIN;
	case SG_TRAIN_GATE_SIDE_Y_MIN: return SG_TRAIN_GATE_SIDE_Y_MAX;
	case SG_TRAIN_GATE_SIDE_Y_MAX: return SG_TRAIN_GATE_SIDE_Y_MIN;
	case SG_TRAIN_GATE_SIDE_Z_MIN: return SG_TRAIN_GATE_SIDE_Z_MAX;
	case SG_TRAIN_GATE_SIDE_Z_MAX: return SG_TRAIN_GATE_SIDE_Z_MIN;
	default: return SG_TRAIN_GATE_SIDE_NONE;
	}
}

sg_train_gate_side_t SG_TrainGateSweepAxisSide(const float bounds_mins[3],
	const float bounds_maxs[3], const float sweep_mins[3],
	const float sweep_maxs[3], unsigned int axis)
{
	static const sg_train_gate_side_t lower[3] = {
		SG_TRAIN_GATE_SIDE_X_MIN,
		SG_TRAIN_GATE_SIDE_Y_MIN,
		SG_TRAIN_GATE_SIDE_Z_MIN
	};
	static const sg_train_gate_side_t upper[3] = {
		SG_TRAIN_GATE_SIDE_X_MAX,
		SG_TRAIN_GATE_SIDE_Y_MAX,
		SG_TRAIN_GATE_SIDE_Z_MAX
	};

	if (!bounds_mins || !bounds_maxs || !sweep_mins || !sweep_maxs ||
	    axis >= 3U || bounds_mins[axis] > bounds_maxs[axis] ||
	    sweep_mins[axis] >= sweep_maxs[axis])
		return SG_TRAIN_GATE_SIDE_NONE;
	if (bounds_maxs[axis] <= sweep_mins[axis])
		return lower[axis];
	if (bounds_mins[axis] >= sweep_maxs[axis])
		return upper[axis];
	return SG_TRAIN_GATE_SIDE_NONE;
}

sg_train_gate_side_t SG_TrainGateUniqueSourceSide(uint32_t side_mask)
{
	const uint32_t known_mask =
		(1U << SG_TRAIN_GATE_SIDE_X_MIN) |
		(1U << SG_TRAIN_GATE_SIDE_X_MAX) |
		(1U << SG_TRAIN_GATE_SIDE_Y_MIN) |
		(1U << SG_TRAIN_GATE_SIDE_Y_MAX) |
		(1U << SG_TRAIN_GATE_SIDE_Z_MIN) |
		(1U << SG_TRAIN_GATE_SIDE_Z_MAX);
	sg_train_gate_side_t found = SG_TRAIN_GATE_SIDE_NONE;
	unsigned int side;

	if (side_mask == 0U || (side_mask & ~known_mask) != 0U)
		return SG_TRAIN_GATE_SIDE_NONE;
	for (side = SG_TRAIN_GATE_SIDE_X_MIN;
	     side <= SG_TRAIN_GATE_SIDE_Z_MAX; side++)
	{
		if ((side_mask & (1U << side)) == 0U)
			continue;
		if (found != SG_TRAIN_GATE_SIDE_NONE)
			return SG_TRAIN_GATE_SIDE_NONE;
		found = (sg_train_gate_side_t)side;
	}
	return found;
}

int SG_TrainGateUniquePassageAxis(uint32_t axis_mask,
	unsigned int motion_axis)
{
	int found = -1;
	unsigned int axis;

	if (motion_axis >= 3U || axis_mask == 0U || (axis_mask & ~0x7U) != 0U ||
	    (axis_mask & (1U << motion_axis)) != 0U)
		return -1;
	for (axis = 0U; axis < 3U; axis++)
	{
		if ((axis_mask & (1U << axis)) == 0U)
			continue;
		if (found >= 0)
			return -1;
		found = (int)axis;
	}
	return found;
}

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
	     witness->activation != SG_TRAIN_GATE_ACTIVATION_SHOOT) ||
	    (witness->mode != SG_TRAIN_GATE_MODE_CROSS &&
	     witness->mode != SG_TRAIN_GATE_MODE_RIDE) ||
	    (witness->mode == SG_TRAIN_GATE_MODE_CROSS &&
	     witness->ride_direction != SG_TRAIN_GATE_RIDE_NONE) ||
	    (witness->mode == SG_TRAIN_GATE_MODE_RIDE &&
	     (witness->activation != SG_TRAIN_GATE_ACTIVATION_TOUCH ||
	      (witness->ride_direction != SG_TRAIN_GATE_RIDE_OPEN_TO_CLOSED &&
	       witness->ride_direction != SG_TRAIN_GATE_RIDE_CLOSED_TO_OPEN))))
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
	       TrainBoolean(observation->entry_arrived) &&
	       TrainBoolean(observation->cross_arrived) &&
	       TrainBoolean(observation->arrived) &&
	       TrainBoolean(observation->weapon_ready) &&
	       TrainBoolean(observation->aim_contact_current) &&
	       TrainBoolean(observation->line_of_fire_clear) &&
	       TrainBoolean(observation->riding) &&
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

static sg_train_gate_command_t TrainRideStep(sg_train_gate_state_t *state,
	const sg_train_gate_observation_t *observation, uint16_t step_ms)
{
	sg_train_gate_pose_t start_pose =
		state->witness.ride_direction == SG_TRAIN_GATE_RIDE_OPEN_TO_CLOSED
		? SG_TRAIN_GATE_POSE_OPEN : SG_TRAIN_GATE_POSE_CLOSED;
	sg_train_gate_pose_t moving_pose =
		state->witness.ride_direction == SG_TRAIN_GATE_RIDE_OPEN_TO_CLOSED
		? SG_TRAIN_GATE_POSE_CLOSING : SG_TRAIN_GATE_POSE_OPENING;
	sg_train_gate_pose_t end_pose =
		state->witness.ride_direction == SG_TRAIN_GATE_RIDE_OPEN_TO_CLOSED
		? SG_TRAIN_GATE_POSE_CLOSED : SG_TRAIN_GATE_POSE_OPEN;

	if (state->phase == SG_TRAIN_GATE_APPROACH)
	{
		if (!TrainActivated(state))
		{
			if (observation->pose != start_pose ||
			    observation->riding != 0U)
			{
				TrainFail(state);
				return SG_TRAIN_GATE_COMMAND_ZERO;
			}
			return SG_TRAIN_GATE_COMMAND_TO_BUTTON;
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
			if (observation->pose != start_pose)
			{
				TrainFail(state);
				return SG_TRAIN_GATE_COMMAND_ZERO;
			}
			if (observation->riding != 1U ||
			    observation->entry_arrived != 1U)
				return SG_TRAIN_GATE_COMMAND_TO_ENTRY;
			return SG_TRAIN_GATE_COMMAND_ZERO;
		}
		if (observation->riding != 1U ||
		    observation->entry_arrived != 1U)
		{
			TrainFail(state);
			return SG_TRAIN_GATE_COMMAND_ZERO;
		}
		if (state->target_dispatch_count != 1U ||
		    observation->pose != moving_pose)
		{
			TrainFail(state);
			return SG_TRAIN_GATE_COMMAND_ZERO;
		}
		state->phase = SG_TRAIN_GATE_OPENING;
	}

	if (state->phase == SG_TRAIN_GATE_OPENING)
	{
		if (!TrainOpeningClock(state, step_ms) || !TrainActivated(state) ||
		    state->target_dispatch_count != 1U ||
		    state->train_use_count != 1U || observation->riding != 1U)
		{
			TrainFail(state);
			return SG_TRAIN_GATE_COMMAND_ZERO;
		}
		if (observation->pose == moving_pose)
			return SG_TRAIN_GATE_COMMAND_ZERO;
		if (observation->pose != end_pose ||
		    observation->cross_arrived != 1U)
		{
			TrainFail(state);
			return SG_TRAIN_GATE_COMMAND_ZERO;
		}
		state->phase = SG_TRAIN_GATE_EGRESS;
	}

	if (state->phase == SG_TRAIN_GATE_EGRESS)
	{
		if (observation->pose != end_pose ||
		    !TrainActivated(state) || state->target_dispatch_count != 1U ||
		    state->train_use_count != 1U)
		{
			TrainFail(state);
			return SG_TRAIN_GATE_COMMAND_ZERO;
		}
		if (observation->arrived == 0U)
			return SG_TRAIN_GATE_COMMAND_TO_EGRESS;
		if (observation->riding != 0U || observation->body_clear != 1U)
		{
			TrainFail(state);
			return SG_TRAIN_GATE_COMMAND_ZERO;
		}
		state->phase = SG_TRAIN_GATE_COMPLETE;
	}
	return SG_TRAIN_GATE_COMMAND_ZERO;
}

int SG_TrainGateLiveBegin(sg_train_gate_state_t *state,
	const sg_train_gate_witness_t *witness,
	const sg_train_gate_observation_t *observation)
{
	if (!state)
		return 0;
	memset(state, 0, sizeof(*state));
	if (!TrainWitnessValid(witness) || !TrainObservationCurrent(observation) ||
	    observation->pose != (witness->mode != SG_TRAIN_GATE_MODE_RIDE ||
	        witness->ride_direction == SG_TRAIN_GATE_RIDE_CLOSED_TO_OPEN
	        ? SG_TRAIN_GATE_POSE_CLOSED : SG_TRAIN_GATE_POSE_OPEN) ||
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
	if (state->witness.mode == SG_TRAIN_GATE_MODE_RIDE)
		return TrainRideStep(state, observation, step_ms);

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
		state->phase = SG_TRAIN_GATE_ENTRY;
	}

	if (state->phase == SG_TRAIN_GATE_ENTRY)
	{
		if (observation->pose != SG_TRAIN_GATE_POSE_OPEN ||
		    !TrainActivated(state) ||
		    state->target_dispatch_count != 1U || state->train_use_count != 1U)
		{
			TrainFail(state);
			return SG_TRAIN_GATE_COMMAND_ZERO;
		}
		if (observation->entry_arrived == 0U)
			return SG_TRAIN_GATE_COMMAND_TO_ENTRY;
		state->phase = SG_TRAIN_GATE_CROSS;
	}

	if (state->phase == SG_TRAIN_GATE_CROSS)
	{
		if (observation->pose != SG_TRAIN_GATE_POSE_OPEN ||
		    !TrainActivated(state) ||
		    state->target_dispatch_count != 1U || state->train_use_count != 1U)
		{
			TrainFail(state);
			return SG_TRAIN_GATE_COMMAND_ZERO;
		}
		if (observation->cross_arrived == 0U)
			return SG_TRAIN_GATE_COMMAND_TO_CROSS;
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
