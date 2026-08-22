#include "q_shared.h"
#include "slipgate/sg_push_live.h"

#include <limits.h>
#include <math.h>
#include <string.h>

static qboolean PushBoolean(qboolean value)
{
	return value == false || value == true;
}

static qboolean PushObservationValid(const sg_push_observation_t *observation)
{
	return observation && PushBoolean(observation->alive) &&
	       PushBoolean(observation->grounded) &&
	       PushBoolean(observation->dry);
}

static qboolean PushSourceExact(const sg_push_witness_t *witness,
	const sg_push_observation_t *observation)
{
	return witness && observation &&
	       memcmp(witness->source_q8, observation->origin_q8,
	           sizeof(witness->source_q8)) == 0;
}

static qboolean PushFail(sg_push_live_state_t *state,
	sg_push_failure_t failure)
{
	if (state)
	{
		state->phase = SG_PUSH_FAILED;
		state->failure = failure;
	}
	return false;
}

void SG_PushLiveReset(sg_push_live_state_t *state)
{
	if (state)
		memset(state, 0, sizeof(*state));
}

qboolean SG_PushArrivalEnvelope(const short origin_q8[3],
	const short destination_q8[3])
{
	int64_t dx, dy, dz;

	if (!origin_q8 || !destination_q8)
		return false;
	dx = (int64_t)destination_q8[0] - origin_q8[0];
	dy = (int64_t)destination_q8[1] - origin_q8[1];
	dz = (int64_t)destination_q8[2] - origin_q8[2];
	return dx * dx + dy * dy <
	           (int64_t)SG_PUSH_ARRIVAL_RADIUS_Q8 *
	               SG_PUSH_ARRIVAL_RADIUS_Q8 &&
	       dz >= -SG_PUSH_ARRIVAL_Z_Q8 && dz <= SG_PUSH_ARRIVAL_Z_Q8;
}

qboolean SG_PushMinimumHealth(float source_z, float destination_z,
	float push_z, float gravity, qboolean falling_damage,
	int *minimum_health_out)
{
	float delta;
	int damage = 0;

	if (!minimum_health_out || !isfinite(source_z) ||
	    !isfinite(destination_z) || !isfinite(push_z) ||
	    !isfinite(gravity) || gravity <= 0.0f ||
	    !PushBoolean(falling_damage))
		return false;
	if (!falling_damage)
	{
		*minimum_health_out = 1;
		return true;
	}
	delta = (push_z * push_z + 2.0f * gravity *
	    (source_z - destination_z + SG_PUSH_ARRIVAL_Z_Q8 / 8.0f)) *
	    0.0001f;
	if (!isfinite(delta))
		return false;
	if (delta < 0.0f)
		delta = 0.0f;
	if (delta > 30.0f)
	{
		float priced = (delta - 30.0f) * 0.5f;

		if (priced > INT_MAX - SG_PUSH_HEALTH_RESERVE - 1)
			return false;
		damage = (int)priced;
		if (damage < 1)
			damage = 1;
	}
	*minimum_health_out = damage + SG_PUSH_HEALTH_RESERVE + 1;
	return true;
}

qboolean SG_PushLiveBegin(sg_push_live_state_t *state,
	const sg_push_witness_t *witness,
	const sg_push_observation_t *observation)
{
	qboolean different = false;
	qboolean impulse = false;
	int axis;

	if (!state)
		return false;
	SG_PushLiveReset(state);
	if (!witness || !PushObservationValid(observation))
		return PushFail(state, SG_PUSH_FAILURE_ARGUMENT);
	for (axis = 0; axis < 3; axis++)
	{
		if (witness->source_q8[axis] != witness->destination_q8[axis])
			different = true;
		if (!isfinite(witness->push_velocity[axis]))
			return PushFail(state, SG_PUSH_FAILURE_WITNESS);
		if (witness->push_velocity[axis] != 0.0f)
			impulse = true;
	}
	if (witness->link_index < 0 || witness->entry_key == 0U ||
	    witness->entry_key == UINT32_MAX || !different || !impulse ||
	    witness->cost_ms == 0U || witness->cost_ms > 30000U ||
	    witness->cost_ms % SG_PUSH_STEP_MS != 0U)
		return PushFail(state, SG_PUSH_FAILURE_WITNESS);
	state->witness = *witness;
	if (!PushSourceExact(witness, observation) || !observation->alive ||
	    !observation->grounded || !observation->dry)
		return PushFail(state, SG_PUSH_FAILURE_SOURCE);
	state->phase = SG_PUSH_APPROACH;
	return true;
}

sg_push_command_t SG_PushLiveCommand(sg_push_live_state_t *state,
	const sg_push_observation_t *observation)
{
	if (!state || !PushObservationValid(observation))
	{
		PushFail(state, SG_PUSH_FAILURE_ARGUMENT);
		return SG_PUSH_COMMAND_ZERO;
	}
	if (state->phase == SG_PUSH_APPROACH &&
	    (!observation->alive || !observation->grounded || !observation->dry))
		PushFail(state, SG_PUSH_FAILURE_SOURCE);
	else if (state->phase == SG_PUSH_FLIGHT &&
	         (!observation->alive || !observation->dry))
		PushFail(state, SG_PUSH_FAILURE_LANDING);
	return SG_PUSH_COMMAND_ZERO;
}

qboolean SG_PushLiveTouched(sg_push_live_state_t *state,
	uint32_t entry_key, const float push_velocity[3])
{
	if (!state || !push_velocity ||
	    (state->phase != SG_PUSH_APPROACH &&
	     state->phase != SG_PUSH_FLIGHT))
		return PushFail(state, SG_PUSH_FAILURE_TOUCH);
	if (entry_key != state->witness.entry_key)
		return PushFail(state, SG_PUSH_FAILURE_TOUCH);
	if (memcmp(push_velocity, state->witness.push_velocity,
	    sizeof(state->witness.push_velocity)) != 0)
		return PushFail(state, SG_PUSH_FAILURE_IMPULSE);
	if (state->phase == SG_PUSH_APPROACH)
		state->phase = SG_PUSH_FLIGHT;
	return true;
}

qboolean SG_PushLiveStep(sg_push_live_state_t *state, int step_ms)
{
	int deadline;

	if (!state || step_ms <= 0 ||
	    (state->phase != SG_PUSH_APPROACH &&
	     state->phase != SG_PUSH_FLIGHT))
		return PushFail(state, SG_PUSH_FAILURE_ARGUMENT);
	if (state->elapsed_ms > INT_MAX - step_ms)
		return PushFail(state, SG_PUSH_FAILURE_TIMEOUT);
	state->elapsed_ms += step_ms;
	deadline = (int)state->witness.cost_ms + SG_PUSH_ARRIVAL_GRACE_MS;
	if (state->elapsed_ms > deadline)
		return PushFail(state, SG_PUSH_FAILURE_TIMEOUT);
	return true;
}

qboolean SG_PushLiveBoundary(sg_push_live_state_t *state,
	qboolean arrived, qboolean grounded)
{
	if (!state || !PushBoolean(arrived) || !PushBoolean(grounded) ||
	    state->phase != SG_PUSH_FLIGHT)
		return PushFail(state, SG_PUSH_FAILURE_ARGUMENT);
	if (arrived)
	{
		if (!grounded || !state->airborne_seen)
			return PushFail(state, SG_PUSH_FAILURE_LANDING);
		state->phase = SG_PUSH_COMPLETE;
		return true;
	}
	if (!grounded)
		state->airborne_seen = true;
	else if (state->airborne_seen)
		return PushFail(state, SG_PUSH_FAILURE_LANDING);
	return true;
}

qboolean SG_PushLiveOwns(const sg_push_live_state_t *state)
{
	return state && (state->phase == SG_PUSH_APPROACH ||
	                 state->phase == SG_PUSH_FLIGHT);
}

const char *SG_PushLiveFailureName(sg_push_failure_t failure)
{
	switch (failure)
	{
	case SG_PUSH_FAILURE_NONE: return "none";
	case SG_PUSH_FAILURE_ARGUMENT: return "argument";
	case SG_PUSH_FAILURE_WITNESS: return "witness";
	case SG_PUSH_FAILURE_SOURCE: return "source";
	case SG_PUSH_FAILURE_TOUCH: return "touch";
	case SG_PUSH_FAILURE_IMPULSE: return "impulse";
	case SG_PUSH_FAILURE_TIMEOUT: return "timeout";
	case SG_PUSH_FAILURE_LANDING: return "landing";
	default: return "unknown";
	}
}
