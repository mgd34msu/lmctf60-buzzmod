/* sg_compound.c -- allocation-free compound RUNE transaction reducer. */
#include "q_shared.h"

#include <limits.h>
#include <math.h>
#include <string.h>

#include "slipgate/sg_action.h"
#include "slipgate/sg_compound.h"

static int CompoundVectorFinite(const float value[3])
{
	return value && isfinite(value[0]) && isfinite(value[1]) &&
	       isfinite(value[2]);
}

static int CompoundVectorPositiveZero(const float value[3])
{
	static const float zero[3] = { 0.0f, 0.0f, 0.0f };

	return value && memcmp(value, zero, sizeof(zero)) == 0;
}

static int CompoundVectorOnLattice(const float value[3])
{
	int axis;

	if (!CompoundVectorFinite(value))
		return 0;
	for (axis = 0; axis < 3; axis++)
	{
		float scaled = value[axis] *
			(float)SG_RUNE_PROOF_DOOR_ANCHOR_SCALE;

		if (scaled < (float)SG_RUNE_PROOF_WORLD_FIXED_MIN ||
		    scaled > (float)SG_RUNE_PROOF_WORLD_FIXED_MAX ||
		    scaled != (float)(int)scaled)
			return 0;
	}
	return 1;
}

static int CompoundHookAngleCanonical(float angle)
{
	short encoded;

	if (!isfinite(angle))
		return 0;
	encoded = ANGLE2SHORT(angle);
	return SHORT2ANGLE(encoded) == angle;
}

static int CompoundQuantizePusherMove(float move, float *quantized)
{
	float scaled;
	double integral;

	if (!quantized || !isfinite(move))
		return 0;
	scaled = move * 8.0f;
	if (!isfinite(scaled))
		return 0;
	if (scaled > 0.0f)
		scaled += 0.5f;
	else
		scaled -= 0.5f;
	if (!isfinite(scaled))
		return 0;
	integral = trunc((double)scaled);
	if (!isfinite(integral) || integral < (double)INT_MIN ||
	    integral > (double)INT_MAX)
		return 0;
	*quantized = 0.125f * (float)(int)integral;
	return 1;
}

static void CompoundTranslateStepReset(sg_compound_translate_step_t *step)
{
	if (step)
		memset(step, 0, sizeof(*step));
}

static void CompoundTranslateStepFinish(
	const sg_compound_translate_t *state,
	sg_compound_translate_step_t *step, const float delta[3])
{
	int axis;

	for (axis = 0; axis < 3; axis++)
	{
		step->delta[axis] = delta[axis];
		step->origin[axis] = state->origin[axis];
	}
	step->elapsed_ms = state->elapsed_ms;
	step->at_top = state->phase == SG_COMPOUND_TRANSLATE_TOP;
}

int SG_CompoundAction(int action)
{
	return action >= RL_DOOR_DROP && action <= RL_DOOR_HOOK;
}

int SG_CompoundRuntimeReady(int action)
{
	return SG_CompoundAction(action) &&
	       SG_COMPOUND_LIVE_CONTROLLER_REVISION >=
	           SG_COMPOUND_REQUIRED_CONTROLLER_REVISION;
}

int SG_CompoundSuffixAction(int action)
{
	switch (action)
	{
	case RL_DOOR_DROP: return RL_DROP;
	case RL_DOOR_SWIM: return RL_SWIM;
	case RL_DOOR_HOOK: return RL_HOOK;
	default: return -1;
	}
}

void SG_CompoundReset(sg_compound_state_t *state)
{
	if (!state)
		return;
	memset(state, 0, sizeof(*state));
	state->link_index = -1;
	state->mover_key = -1;
}

int SG_CompoundBegin(sg_compound_state_t *state, int link_index,
	int mover_key, int action, int mode)
{
	int suffix = SG_CompoundSuffixAction(action);

	if (!state || state->phase != SG_COMPOUND_NONE || link_index < 0 ||
	    mover_key < 0 || suffix < 0 || !SG_ActionAllowsMode(action, mode) ||
	    mode == RLCM_NONE)
		return 0;
	state->phase = SG_COMPOUND_SOURCE;
	state->link_index = link_index;
	state->mover_key = mover_key;
	state->action = (uint8_t)action;
	state->mode = (uint8_t)mode;
	state->suffix_action = (uint8_t)suffix;
	state->sweep_clear = 0;
	state->arrived = 0;
	return 1;
}

int SG_CompoundAdvance(sg_compound_state_t *state,
	sg_compound_event_t event)
{
	if (!state || state->phase == SG_COMPOUND_NONE)
		return 0;
	if (event == SG_COMPOUND_EVENT_ABORT)
	{
		state->phase = SG_COMPOUND_RECOVER;
		return 1;
	}
	if (event == SG_COMPOUND_EVENT_RECOVERED)
	{
		if (state->phase != SG_COMPOUND_RECOVER)
			return 0;
		SG_CompoundReset(state);
		return 1;
	}
	switch (state->phase)
	{
	case SG_COMPOUND_SOURCE:
		if (event != SG_COMPOUND_EVENT_APPROACH) return 0;
		state->phase = SG_COMPOUND_APPROACH;
		return 1;
	case SG_COMPOUND_APPROACH:
		if (event != SG_COMPOUND_EVENT_TOUCH) return 0;
		state->phase = SG_COMPOUND_TOUCHED;
		return 1;
	case SG_COMPOUND_TOUCHED:
		if (event != SG_COMPOUND_EVENT_ACTIVATE) return 0;
		state->phase = SG_COMPOUND_OPENING;
		return 1;
	case SG_COMPOUND_OPENING:
		if (state->mode == RLCM_RIDE)
		{
			if (event != SG_COMPOUND_EVENT_RIDE) return 0;
			state->phase = SG_COMPOUND_RIDE;
		}
		else
		{
			if (event != SG_COMPOUND_EVENT_TOP) return 0;
			state->phase = SG_COMPOUND_TOP;
		}
		return 1;
	case SG_COMPOUND_RIDE:
		if (event != SG_COMPOUND_EVENT_TOP) return 0;
		state->phase = SG_COMPOUND_TOP;
		return 1;
	case SG_COMPOUND_TOP:
		if (event != SG_COMPOUND_EVENT_SUFFIX_BEGIN) return 0;
		state->phase = SG_COMPOUND_SUFFIX_LEASED;
		return 1;
	case SG_COMPOUND_SUFFIX_LEASED:
		if (event == SG_COMPOUND_EVENT_ARRIVED)
		{
			state->arrived = 1;
			return 1;
		}
		if (event != SG_COMPOUND_EVENT_SWEEP_CLEAR) return 0;
		state->sweep_clear = 1;
		state->phase = SG_COMPOUND_SUFFIX_CLEAR;
		if (state->arrived)
			SG_CompoundReset(state);
		return 1;
	case SG_COMPOUND_SUFFIX_CLEAR:
		if (event != SG_COMPOUND_EVENT_ARRIVED) return 0;
		SG_CompoundReset(state);
		return 1;
	default:
		return 0;
	}
}

int SG_CompoundOwns(const sg_compound_state_t *state, int link_index,
	int mover_key)
{
	return state && state->phase != SG_COMPOUND_NONE &&
	       state->link_index == link_index && state->mover_key == mover_key;
}

int SG_CompoundLeaseHeld(const sg_compound_state_t *state)
{
	return state && state->phase != SG_COMPOUND_NONE;
}

int SG_CompoundDelegateSuffix(sg_compound_state_t *state, int link_index,
	int mover_key, sg_compound_suffix_begin_fn begin, void *context)
{
	if (!state || state->phase != SG_COMPOUND_TOP || !begin ||
	    !SG_CompoundOwns(state, link_index, mover_key) ||
	    SG_CompoundSuffixAction(state->action) != state->suffix_action)
		return 0;
	if (!begin(context, link_index, state->suffix_action))
		return 0;
	return SG_CompoundAdvance(state, SG_COMPOUND_EVENT_SUFFIX_BEGIN);
}

int SG_CompoundTranslateBegin(sg_compound_translate_t *state,
	const float start[3], const float end[3], float speed)
{
	const double frame_seconds =
		(double)SG_RUNE_PROOF_SERVER_FRAME_MS / 1000.0;
	double motion_frames;
	double frame_distance;
	float distance;
	float inverse_distance;
	float ignored;
	int axis;

	if (!state)
		return 0;
	memset(state, 0, sizeof(*state));
	if (!CompoundVectorFinite(start) || !CompoundVectorFinite(end) ||
	    !isfinite(speed) || speed <= 0.0f)
		return 0;
	for (axis = 0; axis < 3; axis++)
	{
		state->start[axis] = start[axis];
		state->end[axis] = end[axis];
		state->origin[axis] = start[axis];
		state->direction[axis] = end[axis] - start[axis];
	}
	distance = state->direction[0] * state->direction[0] +
		state->direction[1] * state->direction[1] +
		state->direction[2] * state->direction[2];
	distance = (float)sqrt(distance);
	if (!isfinite(distance) || distance <= 0.0f)
	{
		memset(state, 0, sizeof(*state));
		return 0;
	}
	inverse_distance = 1.0f / distance;
	for (axis = 0; axis < 3; axis++)
		state->direction[axis] *= inverse_distance;
	frame_distance = fmin((double)distance, (double)speed * frame_seconds);
	motion_frames = ceil((double)distance /
		((double)speed * frame_seconds));
	if (!isfinite(frame_distance) || frame_distance <= 0.0 ||
	    !isfinite(motion_frames) || motion_frames < 1.0 ||
	    motion_frames + 1.0 >
	        (double)(INT_MAX / SG_RUNE_PROOF_SERVER_FRAME_MS))
	{
		memset(state, 0, sizeof(*state));
		return 0;
	}
	for (axis = 0; axis < 3; axis++)
		if (!CompoundQuantizePusherMove(
		        state->direction[axis] * (float)frame_distance, &ignored))
		{
			memset(state, 0, sizeof(*state));
			return 0;
		}
	state->speed = speed;
	state->remaining_distance = distance;
	state->phase = SG_COMPOUND_TRANSLATE_SCHEDULED;
	return 1;
}

int SG_CompoundTranslateFrame(sg_compound_translate_t *state,
	sg_compound_translate_step_t *step)
{
	const float frame_seconds =
		(float)SG_RUNE_PROOF_SERVER_FRAME_MS / 1000.0f;
	float delta[3] = { 0.0f, 0.0f, 0.0f };
	int axis;

	CompoundTranslateStepReset(step);
	if (!state || !step ||
	    state->phase <= SG_COMPOUND_TRANSLATE_NONE ||
	    state->phase >= SG_COMPOUND_TRANSLATE_TOP ||
	    !CompoundVectorFinite(state->origin) ||
	    !CompoundVectorFinite(state->direction) ||
	    !isfinite(state->speed) || state->speed <= 0.0f ||
	    !isfinite(state->remaining_distance) ||
	    state->remaining_distance < 0.0f)
		return 0;
	if (state->elapsed_ms < 0 ||
	    state->elapsed_ms > INT_MAX - SG_RUNE_PROOF_SERVER_FRAME_MS)
		return 0;

	if (state->phase == SG_COMPOUND_TRANSLATE_SCHEDULED)
	{
		if (state->speed * frame_seconds >=
		    state->remaining_distance)
		{
			state->phase = SG_COMPOUND_TRANSLATE_FINAL;
		}
		else
		{
			float frames = (float)floor(
				(state->remaining_distance / state->speed) /
				frame_seconds);
			float next_remaining;
			int frame_count;

			if (!isfinite(frames) || frames < 1.0f ||
			    frames > (float)(INT_MAX /
			        SG_RUNE_PROOF_SERVER_FRAME_MS))
				return 0;
			frame_count = (int)frames;
			if (frame_count >
			    INT_MAX / SG_RUNE_PROOF_SERVER_FRAME_MS - 1)
				return 0;
			next_remaining = state->remaining_distance -
				frames * state->speed * frame_seconds;
			if (!isfinite(next_remaining) || next_remaining < 0.0f)
				return 0;
			state->full_frames_remaining = frame_count;
			state->remaining_distance = next_remaining;
			state->phase = SG_COMPOUND_TRANSLATE_FULL;
		}
		state->elapsed_ms += SG_RUNE_PROOF_SERVER_FRAME_MS;
		CompoundTranslateStepFinish(state, step, delta);
		return 1;
	}

	if (state->phase == SG_COMPOUND_TRANSLATE_FULL)
	{
		if (state->full_frames_remaining <= 0)
			return 0;
		for (axis = 0; axis < 3; axis++)
			if (!CompoundQuantizePusherMove(
			        state->direction[axis] * state->speed *
			            frame_seconds,
			        &delta[axis]))
				return 0;
		state->full_frames_remaining--;
		if (state->full_frames_remaining == 0)
		{
			if (state->remaining_distance == 0.0f)
				state->phase = SG_COMPOUND_TRANSLATE_TOP;
			else
				state->phase = SG_COMPOUND_TRANSLATE_FINAL;
		}
	}
	else if (state->phase == SG_COMPOUND_TRANSLATE_FINAL)
	{
		if (state->remaining_distance <= 0.0f)
			return 0;
		for (axis = 0; axis < 3; axis++)
		{
			float velocity = state->direction[axis] *
				(state->remaining_distance / frame_seconds);
			float move = velocity * frame_seconds;

			if (!CompoundQuantizePusherMove(
			        move, &delta[axis]))
				return 0;
		}
		state->remaining_distance = 0.0f;
		state->phase = SG_COMPOUND_TRANSLATE_TOP;
	}
	else
	{
		return 0;
	}
	for (axis = 0; axis < 3; axis++)
		state->origin[axis] += delta[axis];
	state->elapsed_ms += SG_RUNE_PROOF_SERVER_FRAME_MS;
	CompoundTranslateStepFinish(state, step, delta);
	return 1;
}

rune_reject_reason_t SG_CompoundValidateLink(
	const sg_rune_v3_seed_t *seeds, uint32_t num_seeds,
	const sg_rune_v3_link_t *link)
{
	const sg_rune_v3_seed_t *from;
	const sg_rune_v3_seed_t *to;
	int from_water;
	int to_water;

	if (!seeds || !link || num_seeds == 0 ||
	    link->source >= num_seeds || link->destination >= num_seeds ||
	    !SG_CompoundAction(link->action))
		return RLR_BAD_CONTROL_POLICY;
	from = &seeds[link->source];
	to = &seeds[link->destination];
	from_water = ((uint16_t)from->flags & SG_RUNE_V3_SEED_WATER) != 0;
	to_water = ((uint16_t)to->flags & SG_RUNE_V3_SEED_WATER) != 0;
	if (link->provenance != RL_CONTRACTED)
		return RLR_PROVENANCE_FORBIDDEN;
	if (!SG_ActionAllowsMode(link->action, link->mode) ||
	    link->mode == RLCM_NONE)
		return RLR_BAD_MODE;
	if (!SG_ActionEndpointAllowed(link->action, from_water, to_water))
		return RLR_BAD_ENDPOINT_POLICY;
	if (!CompoundVectorOnLattice(link->mechanism_anchor))
		return RLR_BAD_MECHANISM_ANCHOR;
	if (link->sweep_clear_ms == 0 ||
	    link->sweep_clear_ms % SG_RUNE_PROOF_SERVER_FRAME_MS != 0 ||
	    link->sweep_clear_ms > (uint16_t)link->cost_ms)
		return RLR_BAD_SWEEP_CLEAR;
	if (link->min_speed != 0)
		return RLR_BAD_CONTROL_POLICY;
	switch (link->action)
	{
	case RL_DOOR_DROP:
		if (!CompoundVectorOnLattice(link->suffix_anchor) ||
		    link->heading_slack != SG_RUNE_PROOF_DROP_CONTROL_MARKER)
			return RLR_BAD_DROP_CONTROL;
		break;
	case RL_DOOR_SWIM:
		if (link->heading != 0 || link->heading_slack != 0 ||
		    !CompoundVectorPositiveZero(link->suffix_anchor))
			return RLR_BAD_SWIM_CONTROL;
		break;
	case RL_DOOR_HOOK:
		if (link->heading_slack !=
		        SG_RUNE_PROOF_WATER_HOOK_CONTROL_MARKER ||
		    !CompoundHookAngleCanonical(link->suffix_anchor[PITCH]) ||
		    !CompoundHookAngleCanonical(link->suffix_anchor[YAW]) ||
		    link->suffix_anchor[PITCH] <
		        -(float)SG_RUNE_PROOF_HOOK_MAX_ABS_PITCH_DEGREES ||
		    link->suffix_anchor[PITCH] >
		        (float)SG_RUNE_PROOF_HOOK_MAX_ABS_PITCH_DEGREES ||
		    link->suffix_anchor[ROLL] <
		        (float)SG_RUNE_PROOF_HOOK_MIN_RAY ||
		    link->suffix_anchor[ROLL] >
		        (float)SG_RUNE_PROOF_HOOK_MAX_RAY)
			return RLR_BAD_HOOK_CONTROL;
		break;
	default:
		return RLR_ACTION_DISABLED;
	}
	return RLR_OK;
}
