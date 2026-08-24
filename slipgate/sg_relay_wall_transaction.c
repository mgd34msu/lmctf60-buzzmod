/* sg_relay_wall_transaction.c -- authenticated temporary-wall transaction. */
#include "sg_relay_wall_transaction.h"

#include <string.h>

static int RelayWallBoolean(uint8_t value)
{
	return value <= 1U;
}

static int RelayWallKey(uint32_t key)
{
	return key != 0U && key != UINT32_MAX;
}

static int RelayWallSpecValid(const sg_relay_wall_spec_t *spec)
{
	return spec && RelayWallKey(spec->wall_key) &&
	       RelayWallKey(spec->activation_source_key) &&
	       RelayWallKey(spec->restoration_source_key) &&
	       spec->wall_key != spec->activation_source_key &&
	       spec->wall_key != spec->restoration_source_key &&
	       spec->activation_source_key != spec->restoration_source_key;
}

static int RelayWallObservationShapeValid(
	const sg_relay_wall_observation_t *observation)
{
	return observation && RelayWallBoolean(observation->wall_open) &&
	       RelayWallBoolean(observation->active_transition_authenticated) &&
	       RelayWallBoolean(observation->restoration_authenticated) &&
	       RelayWallBoolean(observation->body_clear) &&
	       !(observation->active_transition_authenticated &&
	         observation->restoration_authenticated);
}

static sg_relay_wall_command_t RelayWallFinish(
	sg_relay_wall_state_t *state,
	const sg_relay_wall_observation_t *observation,
	sg_mechanism_timeline_reason_t reason, int complete)
{
	state->terminal_reason = reason;
	state->terminal_complete = complete ? 1U : 0U;
	if (observation->wall_open)
	{
		state->status = SG_RELAY_WALL_RESTORING;
		return observation->body_clear ? SG_RELAY_WALL_COMMAND_RESTORE
		                              : SG_RELAY_WALL_COMMAND_ZERO;
	}
	state->status = complete ? SG_RELAY_WALL_COMPLETE : SG_RELAY_WALL_FAILED;
	return SG_RELAY_WALL_COMMAND_NONE;
}

static sg_relay_wall_command_t RelayWallFail(
	sg_relay_wall_state_t *state,
	const sg_relay_wall_observation_t *observation,
	sg_mechanism_timeline_reason_t reason)
{
	if (state->timeline.phase != SG_MECHANISM_TIMELINE_FAILED)
	{
		state->timeline.phase = SG_MECHANISM_TIMELINE_FAILED;
		state->timeline.reason = reason;
	}
	return RelayWallFinish(state, observation, reason, 0);
}

static sg_relay_wall_command_t RelayWallRestoring(
	sg_relay_wall_state_t *state,
	const sg_relay_wall_observation_t *observation)
{
	if (observation->wall_key != state->spec.wall_key)
	{
		state->status = SG_RELAY_WALL_FAILED;
		state->terminal_reason = SG_MECHANISM_TIMELINE_REASON_BINDING_DRIFT;
		return SG_RELAY_WALL_COMMAND_NONE;
	}
	if (observation->active_transition_authenticated)
	{
		state->status = SG_RELAY_WALL_FAILED;
		state->terminal_reason = SG_MECHANISM_TIMELINE_REASON_UNEXPECTED_EVENT;
		return SG_RELAY_WALL_COMMAND_NONE;
	}
	if (observation->wall_open)
	{
		if (observation->restoration_authenticated)
		{
			state->status = SG_RELAY_WALL_FAILED;
			state->terminal_reason =
				SG_MECHANISM_TIMELINE_REASON_UNEXPECTED_EVENT;
			return SG_RELAY_WALL_COMMAND_NONE;
		}
		return observation->body_clear ? SG_RELAY_WALL_COMMAND_RESTORE
		                              : SG_RELAY_WALL_COMMAND_ZERO;
	}
	if (!observation->restoration_authenticated ||
	    observation->event_source_key != state->spec.restoration_source_key)
	{
		state->status = SG_RELAY_WALL_FAILED;
		state->terminal_reason = observation->restoration_authenticated
			? SG_MECHANISM_TIMELINE_REASON_SOURCE_DRIFT
			: SG_MECHANISM_TIMELINE_REASON_UNEXPECTED_EVENT;
		return SG_RELAY_WALL_COMMAND_NONE;
	}
	state->status = state->terminal_complete ? SG_RELAY_WALL_COMPLETE
	                                       : SG_RELAY_WALL_FAILED;
	return SG_RELAY_WALL_COMMAND_NONE;
}

int SG_RelayWallBegin(sg_relay_wall_state_t *state,
	const sg_relay_wall_spec_t *spec,
	const sg_relay_wall_observation_t *observation)
{
	if (!state)
		return 0;
	memset(state, 0, sizeof(*state));
	if (!RelayWallSpecValid(spec) ||
	    !RelayWallObservationShapeValid(observation) ||
	    observation->wall_key != spec->wall_key || observation->wall_open ||
	    observation->active_transition_authenticated ||
	    observation->restoration_authenticated ||
	    observation->event_source_key != 0U ||
	    !SG_MechanismTimelineBegin(&state->timeline, &spec->timeline,
	        &observation->timeline))
	{
		state->status = SG_RELAY_WALL_FAILED;
		state->terminal_reason = SG_MECHANISM_TIMELINE_REASON_INVALID;
		return 0;
	}
	state->spec = *spec;
	state->status = SG_RELAY_WALL_RUNNING;
	return 1;
}

sg_relay_wall_command_t SG_RelayWallStep(sg_relay_wall_state_t *state,
	const sg_relay_wall_observation_t *observation)
{
	sg_mechanism_timeline_command_t command;

	if (!state || !RelayWallObservationShapeValid(observation))
		return SG_RELAY_WALL_COMMAND_NONE;
	if (state->status == SG_RELAY_WALL_RESTORING)
		return RelayWallRestoring(state, observation);
	if (state->status != SG_RELAY_WALL_RUNNING)
		return SG_RELAY_WALL_COMMAND_NONE;
	if (observation->wall_key != state->spec.wall_key)
		return RelayWallFail(state, observation,
		    SG_MECHANISM_TIMELINE_REASON_BINDING_DRIFT);
	if (observation->restoration_authenticated)
		return RelayWallFail(state, observation,
		    SG_MECHANISM_TIMELINE_REASON_UNEXPECTED_EVENT);
	if (observation->wall_open != state->wall_open)
	{
		if (!observation->wall_open ||
		    !observation->active_transition_authenticated ||
		    !observation->timeline.mechanism_active)
			return RelayWallFail(state, observation,
			    SG_MECHANISM_TIMELINE_REASON_UNEXPECTED_EVENT);
		if (observation->event_source_key !=
		    state->spec.activation_source_key)
			return RelayWallFail(state, observation,
			    SG_MECHANISM_TIMELINE_REASON_SOURCE_DRIFT);
	}
	else if (observation->active_transition_authenticated)
		return RelayWallFail(state, observation,
		    SG_MECHANISM_TIMELINE_REASON_UNEXPECTED_EVENT);
	else if (observation->event_source_key != 0U)
		return RelayWallFail(state, observation,
		    SG_MECHANISM_TIMELINE_REASON_UNEXPECTED_EVENT);
	if (!!observation->wall_open !=
	    !!observation->timeline.mechanism_active)
		return RelayWallFail(state, observation,
		    SG_MECHANISM_TIMELINE_REASON_ACTIVE_LOST);

	command = SG_MechanismTimelineStep(&state->timeline,
	    &observation->timeline);
	if (state->timeline.phase == SG_MECHANISM_TIMELINE_FAILED)
		return RelayWallFinish(state, observation, state->timeline.reason, 0);
	if (state->timeline.phase == SG_MECHANISM_TIMELINE_COMPLETE)
		return RelayWallFinish(state, observation,
		    SG_MECHANISM_TIMELINE_REASON_NONE, 1);
	state->wall_open = observation->wall_open;
	switch (command)
	{
	case SG_MECHANISM_TIMELINE_COMMAND_APPROACH:
		return SG_RELAY_WALL_COMMAND_APPROACH;
	case SG_MECHANISM_TIMELINE_COMMAND_ZERO:
		return SG_RELAY_WALL_COMMAND_ZERO;
	case SG_MECHANISM_TIMELINE_COMMAND_EGRESS:
		return SG_RELAY_WALL_COMMAND_CROSS;
	case SG_MECHANISM_TIMELINE_COMMAND_NONE:
	default:
		return SG_RELAY_WALL_COMMAND_NONE;
	}
}
