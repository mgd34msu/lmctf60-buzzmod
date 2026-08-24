/* Host-free transaction for an authenticated continuous station train. */
#include "sg_train_station_transaction.h"

#include <string.h>

static sg_train_station_command_t TrainStationFail(
	sg_train_station_state_t *state, sg_train_station_reason_t reason)
{
	if (state)
	{
		state->phase = SG_TRAIN_STATION_FAILED;
		state->reason = reason;
	}
	return SG_TRAIN_STATION_COMMAND_NONE;
}

static int TrainStationBoolean(uint8_t value)
{
	return value <= 1U;
}

static int TrainStationKeyValid(uint32_t key)
{
	return key != 0U && key != UINT32_MAX;
}

static int TrainStationRouteContains(const sg_train_station_spec_t *spec,
	uint32_t key)
{
	unsigned int index;

	if (!spec)
		return 0;
	for (index = 0U; index < SG_TRAIN_STATION_ROUTE_CORNERS; index++)
		if (spec->route_corner_keys[index] == key)
			return 1;
	return 0;
}

static int TrainStationSpecValid(const sg_train_station_spec_t *spec)
{
	unsigned int first;
	unsigned int second;
	unsigned int upper_count = 0U;
	unsigned int lower_count = 0U;

	if (!spec || !TrainStationKeyValid(spec->source_key) ||
	    !TrainStationKeyValid(spec->binding_identity) ||
	    !TrainStationKeyValid(spec->fanout_identity) ||
	    !TrainStationKeyValid(spec->train_identity) ||
	    !TrainStationKeyValid(spec->upper_station_key) ||
	    !TrainStationKeyValid(spec->lower_station_key) ||
	    spec->upper_station_key == spec->lower_station_key ||
	    spec->route_corner_count != SG_TRAIN_STATION_ROUTE_CORNERS ||
	    spec->start_on != 1U ||
	    spec->upper_dwell_frames != SG_TRAIN_STATION_DWELL_FRAMES ||
	    spec->lower_dwell_frames != SG_TRAIN_STATION_DWELL_FRAMES ||
	    (spec->boarding_station != SG_TRAIN_STATION_UPPER &&
	     spec->boarding_station != SG_TRAIN_STATION_LOWER))
		return 0;
	for (first = 0U; first < SG_TRAIN_STATION_ROUTE_CORNERS; first++)
	{
		uint32_t key = spec->route_corner_keys[first];

		if (!TrainStationKeyValid(key))
			return 0;
		upper_count += key == spec->upper_station_key;
		lower_count += key == spec->lower_station_key;
		for (second = first + 1U;
		     second < SG_TRAIN_STATION_ROUTE_CORNERS; second++)
			if (key == spec->route_corner_keys[second])
				return 0;
	}
	return upper_count == 1U && lower_count == 1U;
}

static int TrainStationObservationShapeValid(
	const sg_train_station_observation_t *observation)
{
	return observation &&
	       TrainStationBoolean(observation->alive) &&
	       TrainStationBoolean(observation->connected) &&
	       TrainStationBoolean(observation->binding_current) &&
	       TrainStationBoolean(observation->train_moving) &&
	       TrainStationBoolean(observation->body_aboard) &&
	       TrainStationBoolean(observation->body_clear) &&
	       TrainStationBoolean(observation->egress_arrived) &&
	       !(observation->body_aboard && observation->body_clear);
}

static int TrainStationFrameAdd(uint32_t frame, uint32_t duration,
	uint32_t *result)
{
	if (!result || duration > UINT32_MAX - frame)
		return 0;
	*result = frame + duration;
	return 1;
}

static sg_train_station_command_t TrainStationValidate(
	sg_train_station_state_t *state,
	const sg_train_station_observation_t *observation)
{
	if (!TrainStationObservationShapeValid(observation))
		return TrainStationFail(state, SG_TRAIN_STATION_REASON_INVALID);
	if (observation->frame < state->last_frame)
		return TrainStationFail(state,
		    SG_TRAIN_STATION_REASON_FRAME_REGRESSION);
	state->last_frame = observation->frame;
	if (!observation->alive)
		return TrainStationFail(state, SG_TRAIN_STATION_REASON_DEAD);
	if (!observation->connected)
		return TrainStationFail(state,
		    SG_TRAIN_STATION_REASON_DISCONNECTED);
	if (observation->source_key != state->spec.source_key)
		return TrainStationFail(state,
		    SG_TRAIN_STATION_REASON_SOURCE_DRIFT);
	if (!observation->binding_current ||
	    observation->binding_identity != state->spec.binding_identity)
		return TrainStationFail(state,
		    SG_TRAIN_STATION_REASON_BINDING_DRIFT);
	if (observation->fanout_identity != state->spec.fanout_identity)
		return TrainStationFail(state,
		    SG_TRAIN_STATION_REASON_FANOUT_DRIFT);
	if (observation->train_identity != state->spec.train_identity ||
	    !TrainStationRouteContains(&state->spec,
	        observation->train_corner_key))
		return TrainStationFail(state,
		    SG_TRAIN_STATION_REASON_TRAIN_DRIFT);
	return SG_TRAIN_STATION_COMMAND_NONE;
}

static sg_train_station_command_t TrainStationWaitSource(
	sg_train_station_state_t *state,
	const sg_train_station_observation_t *observation)
{
	if (observation->egress_arrived || observation->body_aboard ||
	    !observation->body_clear)
		return TrainStationFail(state, SG_TRAIN_STATION_REASON_INVALID);
	if (observation->train_moving)
		return SG_TRAIN_STATION_COMMAND_WAIT;
	if (observation->train_corner_key == state->destination_station_key)
		return SG_TRAIN_STATION_COMMAND_WAIT;
	if (observation->train_corner_key != state->source_station_key)
		return TrainStationFail(state,
		    SG_TRAIN_STATION_REASON_UNEXPECTED_STOP);
	state->source_arrival_frame = observation->frame;
	if (!TrainStationFrameAdd(observation->frame,
	        SG_TRAIN_STATION_DWELL_FRAMES,
	        &state->source_departure_frame))
		return TrainStationFail(state,
		    SG_TRAIN_STATION_REASON_FRAME_OVERFLOW);
	state->phase = SG_TRAIN_STATION_SOURCE_DWELL;
	return SG_TRAIN_STATION_COMMAND_BOARD;
}

static sg_train_station_command_t TrainStationSourceDwell(
	sg_train_station_state_t *state,
	const sg_train_station_observation_t *observation)
{
	if (observation->egress_arrived)
		return TrainStationFail(state, SG_TRAIN_STATION_REASON_INVALID);
	if (!observation->train_moving)
	{
		if (observation->train_corner_key != state->source_station_key)
			return TrainStationFail(state,
			    SG_TRAIN_STATION_REASON_UNEXPECTED_STOP);
		if (observation->frame > state->source_departure_frame)
			return TrainStationFail(state,
			    SG_TRAIN_STATION_REASON_DWELL_OVERRUN);
		return observation->body_aboard
			? SG_TRAIN_STATION_COMMAND_HOLD
			: SG_TRAIN_STATION_COMMAND_BOARD;
	}
	if (observation->frame != state->source_departure_frame)
		return TrainStationFail(state,
		    observation->frame < state->source_departure_frame
		        ? SG_TRAIN_STATION_REASON_EARLY_DEPARTURE
		        : SG_TRAIN_STATION_REASON_DWELL_OVERRUN);
	if (!observation->body_aboard)
		return TrainStationFail(state,
		    SG_TRAIN_STATION_REASON_BOARDING_MISSED);
	state->phase = SG_TRAIN_STATION_RIDE;
	return SG_TRAIN_STATION_COMMAND_HOLD;
}

static sg_train_station_command_t TrainStationRide(
	sg_train_station_state_t *state,
	const sg_train_station_observation_t *observation)
{
	if (observation->egress_arrived)
		return TrainStationFail(state, SG_TRAIN_STATION_REASON_INVALID);
	if (!observation->body_aboard)
		return TrainStationFail(state, SG_TRAIN_STATION_REASON_BODY_LOST);
	if (observation->train_moving)
		return SG_TRAIN_STATION_COMMAND_HOLD;
	if (observation->train_corner_key != state->destination_station_key)
		return TrainStationFail(state,
		    SG_TRAIN_STATION_REASON_UNEXPECTED_STOP);
	state->destination_arrival_frame = observation->frame;
	if (!TrainStationFrameAdd(observation->frame,
	        SG_TRAIN_STATION_DWELL_FRAMES,
	        &state->destination_departure_frame))
		return TrainStationFail(state,
		    SG_TRAIN_STATION_REASON_FRAME_OVERFLOW);
	state->phase = SG_TRAIN_STATION_DESTINATION_DWELL;
	return SG_TRAIN_STATION_COMMAND_EGRESS;
}

static sg_train_station_command_t TrainStationDestinationDwell(
	sg_train_station_state_t *state,
	const sg_train_station_observation_t *observation)
{
	if (observation->egress_arrived && observation->body_clear &&
	    !observation->body_aboard && !observation->train_moving &&
	    observation->train_corner_key == state->destination_station_key &&
	    observation->frame <= state->destination_departure_frame)
	{
		state->phase = SG_TRAIN_STATION_COMPLETE;
		return SG_TRAIN_STATION_COMMAND_NONE;
	}
	if (observation->train_moving)
	{
		if (observation->frame < state->destination_departure_frame)
			return TrainStationFail(state,
			    SG_TRAIN_STATION_REASON_EARLY_DEPARTURE);
		return TrainStationFail(state,
		    SG_TRAIN_STATION_REASON_EGRESS_EXPIRED);
	}
	if (observation->train_corner_key != state->destination_station_key)
		return TrainStationFail(state,
		    SG_TRAIN_STATION_REASON_UNEXPECTED_STOP);
	if (observation->frame > state->destination_departure_frame)
		return TrainStationFail(state,
		    SG_TRAIN_STATION_REASON_EGRESS_EXPIRED);
	return SG_TRAIN_STATION_COMMAND_EGRESS;
}

int SG_TrainStationTransactionBegin(sg_train_station_state_t *state,
	const sg_train_station_spec_t *spec,
	const sg_train_station_observation_t *observation)
{
	if (!state)
		return 0;
	memset(state, 0, sizeof(*state));
	state->source_arrival_frame = UINT32_MAX;
	state->source_departure_frame = UINT32_MAX;
	state->destination_arrival_frame = UINT32_MAX;
	state->destination_departure_frame = UINT32_MAX;
	if (!TrainStationSpecValid(spec) ||
	    !TrainStationObservationShapeValid(observation))
	{
		TrainStationFail(state, SG_TRAIN_STATION_REASON_INVALID);
		return 0;
	}
	state->spec = *spec;
	state->source_station_key = spec->boarding_station ==
		SG_TRAIN_STATION_UPPER ? spec->upper_station_key
		                         : spec->lower_station_key;
	state->destination_station_key = spec->boarding_station ==
		SG_TRAIN_STATION_UPPER ? spec->lower_station_key
		                         : spec->upper_station_key;
	state->last_frame = observation->frame;
	if (TrainStationValidate(state, observation) ==
	        SG_TRAIN_STATION_COMMAND_NONE &&
	    state->phase != SG_TRAIN_STATION_FAILED &&
	    !observation->body_aboard && observation->body_clear &&
	    !observation->egress_arrived)
	{
		state->phase = SG_TRAIN_STATION_WAIT_SOURCE;
		return 1;
	}
	if (state->phase != SG_TRAIN_STATION_FAILED)
		TrainStationFail(state, SG_TRAIN_STATION_REASON_INVALID);
	return 0;
}

sg_train_station_command_t SG_TrainStationTransactionStep(
	sg_train_station_state_t *state,
	const sg_train_station_observation_t *observation)
{
	if (!state || state->phase == SG_TRAIN_STATION_COMPLETE ||
	    state->phase == SG_TRAIN_STATION_FAILED)
		return SG_TRAIN_STATION_COMMAND_NONE;
	if (state->phase <= SG_TRAIN_STATION_IDLE ||
	    state->phase >= SG_TRAIN_STATION_COMPLETE)
		return TrainStationFail(state, SG_TRAIN_STATION_REASON_INVALID);
	(void)TrainStationValidate(state, observation);
	if (state->phase == SG_TRAIN_STATION_FAILED)
		return SG_TRAIN_STATION_COMMAND_NONE;

	switch (state->phase)
	{
	case SG_TRAIN_STATION_WAIT_SOURCE:
		return TrainStationWaitSource(state, observation);
	case SG_TRAIN_STATION_SOURCE_DWELL:
		return TrainStationSourceDwell(state, observation);
	case SG_TRAIN_STATION_RIDE:
		return TrainStationRide(state, observation);
	case SG_TRAIN_STATION_DESTINATION_DWELL:
		return TrainStationDestinationDwell(state, observation);
	default:
		return TrainStationFail(state, SG_TRAIN_STATION_REASON_INVALID);
	}
}
