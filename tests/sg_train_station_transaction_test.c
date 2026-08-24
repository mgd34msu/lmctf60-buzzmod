#include "slipgate/sg_train_station_transaction.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: CHECK failed: %s\n", \
		    __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static sg_train_station_spec_t Spec(
	sg_train_station_endpoint_t boarding_station)
{
	static const uint32_t corners[SG_TRAIN_STATION_ROUTE_CORNERS] = {
		28U, 29U, 30U, 31U, 32U, 33U, 34U,
		35U, 36U, 37U, 38U, 41U, 40U, 39U
	};
	sg_train_station_spec_t spec;

	memset(&spec, 0, sizeof(spec));
	spec.source_key = 900U;
	spec.binding_identity = UINT32_C(0x13572468);
	spec.fanout_identity = UINT32_C(0x24681357);
	spec.train_identity = UINT32_C(0x10203040);
	memcpy(spec.route_corner_keys, corners, sizeof(corners));
	spec.upper_station_key = 28U;
	spec.lower_station_key = 35U;
	spec.upper_dwell_frames = SG_TRAIN_STATION_DWELL_FRAMES;
	spec.lower_dwell_frames = SG_TRAIN_STATION_DWELL_FRAMES;
	spec.route_corner_count = SG_TRAIN_STATION_ROUTE_CORNERS;
	spec.start_on = 1U;
	spec.boarding_station = boarding_station;
	return spec;
}

static sg_train_station_observation_t Observation(
	const sg_train_station_spec_t *spec, uint32_t frame,
	uint32_t corner_key, int moving)
{
	sg_train_station_observation_t observation;

	memset(&observation, 0, sizeof(observation));
	observation.frame = frame;
	observation.source_key = spec->source_key;
	observation.binding_identity = spec->binding_identity;
	observation.fanout_identity = spec->fanout_identity;
	observation.train_identity = spec->train_identity;
	observation.train_corner_key = corner_key;
	observation.alive = 1U;
	observation.connected = 1U;
	observation.binding_current = 1U;
	observation.train_moving = moving != 0;
	observation.body_clear = 1U;
	return observation;
}

static void BeginRide(sg_train_station_state_t *state,
	sg_train_station_spec_t *spec,
	sg_train_station_observation_t *observation)
{
	uint32_t source = spec->boarding_station == SG_TRAIN_STATION_UPPER
		? spec->upper_station_key : spec->lower_station_key;
	uint32_t next = source == spec->upper_station_key ? 29U : 36U;

	*observation = Observation(spec, 1U, next, 1);
	CHECK(SG_TrainStationTransactionBegin(state, spec, observation));
	CHECK(state->phase == SG_TRAIN_STATION_WAIT_SOURCE);
	CHECK(SG_TrainStationTransactionStep(state, observation) ==
		SG_TRAIN_STATION_COMMAND_WAIT);

	*observation = Observation(spec, 10U, source, 0);
	CHECK(SG_TrainStationTransactionStep(state, observation) ==
		SG_TRAIN_STATION_COMMAND_BOARD);
	CHECK(state->phase == SG_TRAIN_STATION_SOURCE_DWELL);
	CHECK(state->source_arrival_frame == 10U);
	CHECK(state->source_departure_frame == 40U);

	observation->frame = 39U;
	observation->body_aboard = 1U;
	observation->body_clear = 0U;
	CHECK(SG_TrainStationTransactionStep(state, observation) ==
		SG_TRAIN_STATION_COMMAND_HOLD);

	observation->frame = 40U;
	observation->train_corner_key = next;
	observation->train_moving = 1U;
	CHECK(SG_TrainStationTransactionStep(state, observation) ==
		SG_TRAIN_STATION_COMMAND_HOLD);
	CHECK(state->phase == SG_TRAIN_STATION_RIDE);
}

static void TestHappyPath(sg_train_station_endpoint_t boarding_station)
{
	sg_train_station_state_t state;
	sg_train_station_spec_t spec = Spec(boarding_station);
	sg_train_station_observation_t observation;
	uint32_t destination = boarding_station == SG_TRAIN_STATION_UPPER
		? spec.lower_station_key : spec.upper_station_key;

	BeginRide(&state, &spec, &observation);
	observation.frame = 54U;
	observation.train_corner_key = destination == spec.lower_station_key
		? 34U : 39U;
	CHECK(SG_TrainStationTransactionStep(&state, &observation) ==
		SG_TRAIN_STATION_COMMAND_HOLD);

	observation.frame = 55U;
	observation.train_corner_key = destination;
	observation.train_moving = 0U;
	CHECK(SG_TrainStationTransactionStep(&state, &observation) ==
		SG_TRAIN_STATION_COMMAND_EGRESS);
	CHECK(state.phase == SG_TRAIN_STATION_DESTINATION_DWELL);
	CHECK(state.destination_arrival_frame == 55U);
	CHECK(state.destination_departure_frame == 85U);

	observation.frame = 56U;
	observation.body_aboard = 0U;
	CHECK(SG_TrainStationTransactionStep(&state, &observation) ==
		SG_TRAIN_STATION_COMMAND_EGRESS);
	observation.frame = 57U;
	observation.body_clear = 1U;
	observation.egress_arrived = 1U;
	CHECK(SG_TrainStationTransactionStep(&state, &observation) ==
		SG_TRAIN_STATION_COMMAND_NONE);
	CHECK(state.phase == SG_TRAIN_STATION_COMPLETE);
	CHECK(state.reason == SG_TRAIN_STATION_REASON_NONE);
}

static void TestSourceDwellAndOccupancy(void)
{
	sg_train_station_state_t state;
	sg_train_station_spec_t spec = Spec(SG_TRAIN_STATION_UPPER);
	sg_train_station_observation_t observation;

	observation = Observation(&spec, 1U, 29U, 1);
	CHECK(SG_TrainStationTransactionBegin(&state, &spec, &observation));
	observation = Observation(&spec, 10U, spec.upper_station_key, 0);
	CHECK(SG_TrainStationTransactionStep(&state, &observation) ==
		SG_TRAIN_STATION_COMMAND_BOARD);
	observation.frame = 39U;
	observation.body_aboard = 1U;
	observation.body_clear = 0U;
	CHECK(SG_TrainStationTransactionStep(&state, &observation) ==
		SG_TRAIN_STATION_COMMAND_HOLD);
	observation.frame = 41U;
	CHECK(SG_TrainStationTransactionStep(&state, &observation) ==
		SG_TRAIN_STATION_COMMAND_NONE);
	CHECK(state.reason == SG_TRAIN_STATION_REASON_DWELL_OVERRUN);

	observation = Observation(&spec, 1U, 29U, 1);
	CHECK(SG_TrainStationTransactionBegin(&state, &spec, &observation));
	observation = Observation(&spec, 10U, spec.upper_station_key, 0);
	CHECK(SG_TrainStationTransactionStep(&state, &observation) ==
		SG_TRAIN_STATION_COMMAND_BOARD);
	observation.frame = 39U;
	observation.body_aboard = 1U;
	observation.body_clear = 0U;
	CHECK(SG_TrainStationTransactionStep(&state, &observation) ==
		SG_TRAIN_STATION_COMMAND_HOLD);
	observation.frame = 41U;
	observation.train_corner_key = 29U;
	observation.train_moving = 1U;
	CHECK(SG_TrainStationTransactionStep(&state, &observation) ==
		SG_TRAIN_STATION_COMMAND_NONE);
	CHECK(state.reason == SG_TRAIN_STATION_REASON_DWELL_OVERRUN);

	observation = Observation(&spec, 1U, 29U, 1);
	CHECK(SG_TrainStationTransactionBegin(&state, &spec, &observation));
	observation = Observation(&spec, 10U, spec.upper_station_key, 0);
	CHECK(SG_TrainStationTransactionStep(&state, &observation) ==
		SG_TRAIN_STATION_COMMAND_BOARD);
	observation.frame = 39U;
	observation.train_corner_key = 29U;
	observation.train_moving = 1U;
	CHECK(SG_TrainStationTransactionStep(&state, &observation) ==
		SG_TRAIN_STATION_COMMAND_NONE);
	CHECK(state.reason == SG_TRAIN_STATION_REASON_EARLY_DEPARTURE);

	observation = Observation(&spec, 1U, 29U, 1);
	CHECK(SG_TrainStationTransactionBegin(&state, &spec, &observation));
	observation = Observation(&spec, 10U, spec.upper_station_key, 0);
	CHECK(SG_TrainStationTransactionStep(&state, &observation) ==
		SG_TRAIN_STATION_COMMAND_BOARD);
	observation.frame = 40U;
	observation.train_corner_key = 29U;
	observation.train_moving = 1U;
	CHECK(SG_TrainStationTransactionStep(&state, &observation) ==
		SG_TRAIN_STATION_COMMAND_NONE);
	CHECK(state.reason == SG_TRAIN_STATION_REASON_BOARDING_MISSED);

	BeginRide(&state, &spec, &observation);
	observation.frame = 41U;
	observation.body_aboard = 0U;
	CHECK(SG_TrainStationTransactionStep(&state, &observation) ==
		SG_TRAIN_STATION_COMMAND_NONE);
	CHECK(state.reason == SG_TRAIN_STATION_REASON_BODY_LOST);
}

static void TestDestinationDwellAndEgress(void)
{
	sg_train_station_state_t state;
	sg_train_station_spec_t spec = Spec(SG_TRAIN_STATION_UPPER);
	sg_train_station_observation_t observation;

	BeginRide(&state, &spec, &observation);
	observation.frame = 55U;
	observation.train_corner_key = spec.lower_station_key;
	observation.train_moving = 0U;
	CHECK(SG_TrainStationTransactionStep(&state, &observation) ==
		SG_TRAIN_STATION_COMMAND_EGRESS);
	observation.frame = 56U;
	observation.train_corner_key = 36U;
	observation.train_moving = 1U;
	CHECK(SG_TrainStationTransactionStep(&state, &observation) ==
		SG_TRAIN_STATION_COMMAND_NONE);
	CHECK(state.reason == SG_TRAIN_STATION_REASON_EARLY_DEPARTURE);

	BeginRide(&state, &spec, &observation);
	observation.frame = 55U;
	observation.train_corner_key = spec.lower_station_key;
	observation.train_moving = 0U;
	CHECK(SG_TrainStationTransactionStep(&state, &observation) ==
		SG_TRAIN_STATION_COMMAND_EGRESS);
	observation.frame = 86U;
	CHECK(SG_TrainStationTransactionStep(&state, &observation) ==
		SG_TRAIN_STATION_COMMAND_NONE);
	CHECK(state.reason == SG_TRAIN_STATION_REASON_EGRESS_EXPIRED);

	BeginRide(&state, &spec, &observation);
	observation.frame = 50U;
	observation.train_corner_key = 30U;
	observation.train_moving = 0U;
	CHECK(SG_TrainStationTransactionStep(&state, &observation) ==
		SG_TRAIN_STATION_COMMAND_NONE);
	CHECK(state.reason == SG_TRAIN_STATION_REASON_UNEXPECTED_STOP);
}

static void TestIdentityDrift(void)
{
	sg_train_station_state_t state;
	sg_train_station_spec_t spec = Spec(SG_TRAIN_STATION_UPPER);
	sg_train_station_observation_t base = Observation(&spec, 1U, 29U, 1);
	sg_train_station_observation_t observation;
	struct drift_case_s
	{
		uint32_t *field;
		sg_train_station_reason_t reason;
	} cases[] = {
		{ &base.source_key, SG_TRAIN_STATION_REASON_SOURCE_DRIFT },
		{ &base.binding_identity, SG_TRAIN_STATION_REASON_BINDING_DRIFT },
		{ &base.fanout_identity, SG_TRAIN_STATION_REASON_FANOUT_DRIFT },
		{ &base.train_identity, SG_TRAIN_STATION_REASON_TRAIN_DRIFT }
	};
	unsigned int index;

	for (index = 0U; index < sizeof(cases) / sizeof(cases[0]); index++)
	{
		CHECK(SG_TrainStationTransactionBegin(&state, &spec, &base));
		observation = base;
		if (cases[index].field == &base.source_key)
			observation.source_key++;
		else if (cases[index].field == &base.binding_identity)
			observation.binding_identity++;
		else if (cases[index].field == &base.fanout_identity)
			observation.fanout_identity++;
		else
			observation.train_identity++;
		CHECK(SG_TrainStationTransactionStep(&state, &observation) ==
			SG_TRAIN_STATION_COMMAND_NONE);
		CHECK(state.reason == cases[index].reason);
	}

	CHECK(SG_TrainStationTransactionBegin(&state, &spec, &base));
	observation = base;
	observation.binding_current = 0U;
	CHECK(SG_TrainStationTransactionStep(&state, &observation) ==
		SG_TRAIN_STATION_COMMAND_NONE);
	CHECK(state.reason == SG_TRAIN_STATION_REASON_BINDING_DRIFT);

	CHECK(SG_TrainStationTransactionBegin(&state, &spec, &base));
	observation = base;
	observation.train_corner_key = 777U;
	CHECK(SG_TrainStationTransactionStep(&state, &observation) ==
		SG_TRAIN_STATION_COMMAND_NONE);
	CHECK(state.reason == SG_TRAIN_STATION_REASON_TRAIN_DRIFT);
}

static void TestInvalidShapeAndFrames(void)
{
	sg_train_station_state_t state;
	sg_train_station_spec_t spec = Spec(SG_TRAIN_STATION_UPPER);
	sg_train_station_observation_t observation =
		Observation(&spec, 10U, 29U, 1);
	sg_train_station_spec_t invalid;

	invalid = spec;
	invalid.route_corner_count = 13U;
	CHECK(!SG_TrainStationTransactionBegin(&state, &invalid, &observation));
	CHECK(state.reason == SG_TRAIN_STATION_REASON_INVALID);
	invalid = spec;
	invalid.start_on = 0U;
	CHECK(!SG_TrainStationTransactionBegin(&state, &invalid, &observation));
	invalid = spec;
	invalid.upper_dwell_frames--;
	CHECK(!SG_TrainStationTransactionBegin(&state, &invalid, &observation));
	invalid = spec;
	invalid.route_corner_keys[1] = invalid.route_corner_keys[0];
	CHECK(!SG_TrainStationTransactionBegin(&state, &invalid, &observation));
	invalid = spec;
	invalid.lower_station_key = 777U;
	CHECK(!SG_TrainStationTransactionBegin(&state, &invalid, &observation));

	CHECK(SG_TrainStationTransactionBegin(&state, &spec, &observation));
	observation.frame = 9U;
	CHECK(SG_TrainStationTransactionStep(&state, &observation) ==
		SG_TRAIN_STATION_COMMAND_NONE);
	CHECK(state.reason == SG_TRAIN_STATION_REASON_FRAME_REGRESSION);

	observation = Observation(&spec, UINT32_MAX - 10U,
		spec.upper_station_key, 0);
	CHECK(SG_TrainStationTransactionBegin(&state, &spec, &observation));
	CHECK(SG_TrainStationTransactionStep(&state, &observation) ==
		SG_TRAIN_STATION_COMMAND_NONE);
	CHECK(state.reason == SG_TRAIN_STATION_REASON_FRAME_OVERFLOW);

	observation = Observation(&spec, 1U, 29U, 1);
	observation.body_aboard = 1U;
	CHECK(!SG_TrainStationTransactionBegin(&state, &spec, &observation));
	observation = Observation(&spec, 1U, 29U, 1);
	observation.alive = 2U;
	CHECK(!SG_TrainStationTransactionBegin(&state, &spec, &observation));
}

int main(void)
{
	TestHappyPath(SG_TRAIN_STATION_UPPER);
	TestHappyPath(SG_TRAIN_STATION_LOWER);
	TestSourceDwellAndOccupancy();
	TestDestinationDwellAndEgress();
	TestIdentityDrift();
	TestInvalidShapeAndFrames();
	if (failures)
	{
		fprintf(stderr, "sg_train_station_transaction_test: %d failures\n",
			failures);
		return 1;
	}
	puts("sg_train_station_transaction_test: ok");
	return 0;
}
