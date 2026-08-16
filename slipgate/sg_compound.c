/* sg_compound.c -- allocation-free compound RUNE transaction reducer. */
#include "q_shared.h"

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
