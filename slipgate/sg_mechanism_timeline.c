/* sg_mechanism_timeline.c -- host-free bounded mechanism transaction. */
#include "sg_mechanism_timeline.h"

#include <string.h>

static sg_mechanism_timeline_command_t TimelineFail(
	sg_mechanism_timeline_state_t *state,
	sg_mechanism_timeline_reason_t reason)
{
	if (state)
	{
		state->phase = SG_MECHANISM_TIMELINE_FAILED;
		state->reason = reason;
	}
	return SG_MECHANISM_TIMELINE_COMMAND_NONE;
}

static int TimelineBoolean(uint8_t value)
{
	return value <= 1U;
}

static int TimelineObservationShapeValid(
	const sg_mechanism_timeline_observation_t *observation)
{
	return observation &&
	       observation->frame != SG_MECHANISM_TIMELINE_FRAME_UNSET &&
	       TimelineBoolean(observation->alive) &&
	       TimelineBoolean(observation->connected) &&
	       TimelineBoolean(observation->binding_current) &&
	       TimelineBoolean(observation->approach_arrived) &&
	       TimelineBoolean(observation->activation_authenticated) &&
	       TimelineBoolean(observation->mechanism_active) &&
	       TimelineBoolean(observation->egress_requested) &&
	       TimelineBoolean(observation->egress_arrived);
}

static int TimelineSpecValid(const sg_mechanism_timeline_spec_t *spec)
{
	return spec && spec->source_key != 0U &&
	       spec->source_key != UINT32_MAX &&
	       spec->approach_timeout_frames != 0U &&
	       spec->activation_timeout_frames != 0U &&
	       (spec->lease_frames != 0U || spec->station_wait_frames != 0U) &&
	       spec->egress_timeout_frames != 0U;
}

static int TimelineFrameAdd(uint32_t frame, uint32_t duration,
	uint32_t *result)
{
	if (!result || frame == SG_MECHANISM_TIMELINE_FRAME_UNSET ||
	    duration > UINT32_MAX - 1U - frame)
		return 0;
	*result = frame + duration;
	return 1;
}

static void TimelineUnsetFrames(sg_mechanism_timeline_state_t *state)
{
	state->approach_deadline_frame = SG_MECHANISM_TIMELINE_FRAME_UNSET;
	state->activation_deadline_frame = SG_MECHANISM_TIMELINE_FRAME_UNSET;
	state->activation_frame = SG_MECHANISM_TIMELINE_FRAME_UNSET;
	state->ready_frame = SG_MECHANISM_TIMELINE_FRAME_UNSET;
	state->cooldown_ready_frame = SG_MECHANISM_TIMELINE_FRAME_UNSET;
	state->lease_deadline_frame = SG_MECHANISM_TIMELINE_FRAME_UNSET;
	state->station_deadline_frame = SG_MECHANISM_TIMELINE_FRAME_UNSET;
	state->egress_deadline_frame = SG_MECHANISM_TIMELINE_FRAME_UNSET;
}

static sg_mechanism_timeline_command_t TimelineValidateObservation(
	sg_mechanism_timeline_state_t *state,
	const sg_mechanism_timeline_observation_t *observation)
{
	if (!TimelineObservationShapeValid(observation))
		return TimelineFail(state, SG_MECHANISM_TIMELINE_REASON_INVALID);
	if (observation->frame < state->last_frame)
		return TimelineFail(state,
		    SG_MECHANISM_TIMELINE_REASON_FRAME_REGRESSION);
	state->last_frame = observation->frame;
	if (!observation->alive)
		return TimelineFail(state, SG_MECHANISM_TIMELINE_REASON_DEAD);
	if (!observation->connected)
		return TimelineFail(state,
		    SG_MECHANISM_TIMELINE_REASON_DISCONNECTED);
	if (!observation->binding_current)
		return TimelineFail(state,
		    SG_MECHANISM_TIMELINE_REASON_BINDING_DRIFT);
	if (observation->source_key != state->spec.source_key)
		return TimelineFail(state,
		    SG_MECHANISM_TIMELINE_REASON_SOURCE_DRIFT);
	if (observation->fanout_identity != state->spec.fanout_identity)
		return TimelineFail(state,
		    SG_MECHANISM_TIMELINE_REASON_FANOUT_DRIFT);
	return SG_MECHANISM_TIMELINE_COMMAND_ZERO;
}

static sg_mechanism_timeline_reason_t TimelineActiveExpiry(
	const sg_mechanism_timeline_state_t *state, uint32_t frame)
{
	if (state->lease_deadline_frame != SG_MECHANISM_TIMELINE_FRAME_UNSET &&
	    frame >= state->lease_deadline_frame)
		return SG_MECHANISM_TIMELINE_REASON_LEASE_EXPIRED;
	if (state->station_deadline_frame != SG_MECHANISM_TIMELINE_FRAME_UNSET &&
	    frame >= state->station_deadline_frame)
		return SG_MECHANISM_TIMELINE_REASON_STATION_EXPIRED;
	return SG_MECHANISM_TIMELINE_REASON_NONE;
}

static sg_mechanism_timeline_command_t TimelineActiveStep(
	sg_mechanism_timeline_state_t *state,
	const sg_mechanism_timeline_observation_t *observation)
{
	sg_mechanism_timeline_reason_t expiry = TimelineActiveExpiry(state,
	    observation->frame);

	if (expiry != SG_MECHANISM_TIMELINE_REASON_NONE)
		return TimelineFail(state, expiry);
	if (!observation->mechanism_active)
		return TimelineFail(state,
		    SG_MECHANISM_TIMELINE_REASON_ACTIVE_LOST);
	if (observation->egress_arrived && !observation->egress_requested)
		return TimelineFail(state,
		    SG_MECHANISM_TIMELINE_REASON_UNEXPECTED_EVENT);
	if (!observation->egress_requested)
		return SG_MECHANISM_TIMELINE_COMMAND_ZERO;
	if (!TimelineFrameAdd(observation->frame,
	        state->spec.egress_timeout_frames,
	        &state->egress_deadline_frame))
		return TimelineFail(state,
		    SG_MECHANISM_TIMELINE_REASON_FRAME_OVERFLOW);
	state->phase = SG_MECHANISM_TIMELINE_EGRESS;
	if (observation->egress_arrived)
	{
		state->phase = SG_MECHANISM_TIMELINE_COMPLETE;
		return SG_MECHANISM_TIMELINE_COMMAND_NONE;
	}
	return SG_MECHANISM_TIMELINE_COMMAND_EGRESS;
}

static sg_mechanism_timeline_command_t TimelineActivate(
	sg_mechanism_timeline_state_t *state,
	const sg_mechanism_timeline_observation_t *observation)
{
	state->activation_frame = observation->frame;
	if (!TimelineFrameAdd(observation->frame,
	        state->spec.trigger_delay_frames, &state->ready_frame) ||
	    !TimelineFrameAdd(observation->frame,
	        state->spec.cooldown_frames, &state->cooldown_ready_frame) ||
	    (state->spec.lease_frames != 0U &&
	     !TimelineFrameAdd(state->ready_frame, state->spec.lease_frames,
	         &state->lease_deadline_frame)) ||
	    (state->spec.station_wait_frames != 0U &&
	     !TimelineFrameAdd(state->ready_frame, state->spec.station_wait_frames,
	         &state->station_deadline_frame)))
		return TimelineFail(state,
		    SG_MECHANISM_TIMELINE_REASON_FRAME_OVERFLOW);
	if (state->spec.trigger_delay_frames != 0U)
	{
		if (observation->mechanism_active)
			return TimelineFail(state,
			    SG_MECHANISM_TIMELINE_REASON_EARLY_ACTIVE);
		if (observation->egress_requested || observation->egress_arrived)
			return TimelineFail(state,
			    SG_MECHANISM_TIMELINE_REASON_UNEXPECTED_EVENT);
		state->phase = SG_MECHANISM_TIMELINE_DELAY_PENDING;
		return SG_MECHANISM_TIMELINE_COMMAND_ZERO;
	}
	if (!observation->mechanism_active)
		return TimelineFail(state,
		    SG_MECHANISM_TIMELINE_REASON_READY_MISSED);
	state->phase = SG_MECHANISM_TIMELINE_ACTIVE;
	return TimelineActiveStep(state, observation);
}

int SG_MechanismTimelineBegin(sg_mechanism_timeline_state_t *state,
	const sg_mechanism_timeline_spec_t *spec,
	const sg_mechanism_timeline_observation_t *observation)
{
	if (!state)
		return 0;
	memset(state, 0, sizeof(*state));
	TimelineUnsetFrames(state);
	if (!TimelineSpecValid(spec) || !TimelineObservationShapeValid(observation))
	{
		TimelineFail(state, SG_MECHANISM_TIMELINE_REASON_INVALID);
		return 0;
	}
	state->spec = *spec;
	state->begin_frame = observation->frame;
	state->last_frame = observation->frame;
	if (!observation->alive)
	{
		TimelineFail(state, SG_MECHANISM_TIMELINE_REASON_DEAD);
		return 0;
	}
	if (!observation->connected)
	{
		TimelineFail(state, SG_MECHANISM_TIMELINE_REASON_DISCONNECTED);
		return 0;
	}
	if (!observation->binding_current)
	{
		TimelineFail(state, SG_MECHANISM_TIMELINE_REASON_BINDING_DRIFT);
		return 0;
	}
	if (observation->source_key != spec->source_key)
	{
		TimelineFail(state, SG_MECHANISM_TIMELINE_REASON_SOURCE_DRIFT);
		return 0;
	}
	if (observation->fanout_identity != spec->fanout_identity)
	{
		TimelineFail(state, SG_MECHANISM_TIMELINE_REASON_FANOUT_DRIFT);
		return 0;
	}
	if (!TimelineFrameAdd(observation->frame,
	        spec->approach_timeout_frames,
	        &state->approach_deadline_frame))
	{
		TimelineFail(state, SG_MECHANISM_TIMELINE_REASON_FRAME_OVERFLOW);
		return 0;
	}
	state->phase = SG_MECHANISM_TIMELINE_APPROACH;
	return 1;
}

sg_mechanism_timeline_command_t SG_MechanismTimelineStep(
	sg_mechanism_timeline_state_t *state,
	const sg_mechanism_timeline_observation_t *observation)
{
	sg_mechanism_timeline_command_t validation;
	sg_mechanism_timeline_reason_t expiry;

	if (!state)
		return SG_MECHANISM_TIMELINE_COMMAND_NONE;
	if (state->phase == SG_MECHANISM_TIMELINE_COMPLETE ||
	    state->phase == SG_MECHANISM_TIMELINE_FAILED)
		return SG_MECHANISM_TIMELINE_COMMAND_NONE;
	if (state->phase <= SG_MECHANISM_TIMELINE_IDLE ||
	    state->phase >= SG_MECHANISM_TIMELINE_COMPLETE)
		return TimelineFail(state, SG_MECHANISM_TIMELINE_REASON_INVALID);
	validation = TimelineValidateObservation(state, observation);
	if (state->phase == SG_MECHANISM_TIMELINE_FAILED)
		return validation;

	switch (state->phase)
	{
	case SG_MECHANISM_TIMELINE_APPROACH:
		if (observation->frame >= state->approach_deadline_frame)
			return TimelineFail(state,
			    SG_MECHANISM_TIMELINE_REASON_APPROACH_TIMEOUT);
		if (!observation->approach_arrived)
		{
			if (observation->activation_authenticated ||
			    observation->mechanism_active || observation->egress_requested ||
			    observation->egress_arrived)
				return TimelineFail(state,
				    SG_MECHANISM_TIMELINE_REASON_UNEXPECTED_EVENT);
			return SG_MECHANISM_TIMELINE_COMMAND_APPROACH;
		}
		if (!TimelineFrameAdd(observation->frame,
		        state->spec.activation_timeout_frames,
		        &state->activation_deadline_frame))
			return TimelineFail(state,
			    SG_MECHANISM_TIMELINE_REASON_FRAME_OVERFLOW);
		state->phase = SG_MECHANISM_TIMELINE_ACTIVATION;
		if (observation->activation_authenticated)
			return TimelineActivate(state, observation);
		if (observation->mechanism_active || observation->egress_requested ||
		    observation->egress_arrived)
			return TimelineFail(state,
			    SG_MECHANISM_TIMELINE_REASON_UNEXPECTED_EVENT);
		return SG_MECHANISM_TIMELINE_COMMAND_ZERO;

	case SG_MECHANISM_TIMELINE_ACTIVATION:
		if (observation->frame >= state->activation_deadline_frame)
			return TimelineFail(state,
			    SG_MECHANISM_TIMELINE_REASON_ACTIVATION_TIMEOUT);
		if (!observation->activation_authenticated)
		{
			if (observation->mechanism_active || observation->egress_requested ||
			    observation->egress_arrived)
				return TimelineFail(state,
				    SG_MECHANISM_TIMELINE_REASON_UNEXPECTED_EVENT);
			return SG_MECHANISM_TIMELINE_COMMAND_ZERO;
		}
		return TimelineActivate(state, observation);

	case SG_MECHANISM_TIMELINE_DELAY_PENDING:
		if (observation->egress_requested || observation->egress_arrived)
			return TimelineFail(state,
			    SG_MECHANISM_TIMELINE_REASON_UNEXPECTED_EVENT);
		if (observation->frame < state->ready_frame)
		{
			if (observation->mechanism_active)
				return TimelineFail(state,
				    SG_MECHANISM_TIMELINE_REASON_EARLY_ACTIVE);
			return SG_MECHANISM_TIMELINE_COMMAND_ZERO;
		}
		if (observation->frame != state->ready_frame ||
		    !observation->mechanism_active)
			return TimelineFail(state,
			    SG_MECHANISM_TIMELINE_REASON_READY_MISSED);
		state->phase = SG_MECHANISM_TIMELINE_ACTIVE;
		return TimelineActiveStep(state, observation);

	case SG_MECHANISM_TIMELINE_ACTIVE:
		return TimelineActiveStep(state, observation);

	case SG_MECHANISM_TIMELINE_EGRESS:
		expiry = TimelineActiveExpiry(state, observation->frame);
		if (expiry != SG_MECHANISM_TIMELINE_REASON_NONE)
			return TimelineFail(state, expiry);
		if (!observation->mechanism_active)
			return TimelineFail(state,
			    SG_MECHANISM_TIMELINE_REASON_ACTIVE_LOST);
		if (observation->frame >= state->egress_deadline_frame)
			return TimelineFail(state,
			    SG_MECHANISM_TIMELINE_REASON_EGRESS_TIMEOUT);
		if (observation->egress_arrived)
		{
			state->phase = SG_MECHANISM_TIMELINE_COMPLETE;
			return SG_MECHANISM_TIMELINE_COMMAND_NONE;
		}
		return SG_MECHANISM_TIMELINE_COMMAND_EGRESS;

	default:
		return TimelineFail(state, SG_MECHANISM_TIMELINE_REASON_INVALID);
	}
}
