#include <stdio.h>
#include <string.h>

#include "slipgate/sg_relay_wall_transaction.h"

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		    __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static sg_relay_wall_spec_t Gate(void)
{
	sg_relay_wall_spec_t spec;

	memset(&spec, 0, sizeof(spec));
	spec.timeline.source_key = 10U;
	spec.timeline.fanout_identity = 0x1234U;
	spec.timeline.approach_timeout_frames = 6U;
	spec.timeline.activation_timeout_frames = 2U;
	spec.timeline.trigger_delay_frames = 2U;
	spec.timeline.cooldown_frames = 40U;
	spec.timeline.lease_frames = 40U;
	spec.timeline.egress_timeout_frames = 8U;
	spec.wall_key = 30U;
	spec.activation_source_key = 20U;
	spec.restoration_source_key = 21U;
	return spec;
}

static sg_relay_wall_observation_t Observation(uint32_t frame)
{
	sg_relay_wall_observation_t observation;

	memset(&observation, 0, sizeof(observation));
	observation.timeline.frame = frame;
	observation.timeline.source_key = 10U;
	observation.timeline.fanout_identity = 0x1234U;
	observation.timeline.alive = 1U;
	observation.timeline.connected = 1U;
	observation.timeline.binding_current = 1U;
	observation.wall_key = 30U;
	observation.body_clear = 1U;
	return observation;
}

static void Activate(sg_relay_wall_state_t *state,
	sg_relay_wall_observation_t *observation)
{
	observation->timeline.frame = 101U;
	observation->timeline.approach_arrived = 1U;
	observation->timeline.activation_authenticated = 1U;
	CHECK(SG_RelayWallStep(state, observation) ==
	    SG_RELAY_WALL_COMMAND_ZERO);
	CHECK(state->timeline.phase == SG_MECHANISM_TIMELINE_DELAY_PENDING);
	CHECK(state->timeline.ready_frame == 103U);
	CHECK(state->timeline.cooldown_ready_frame == 141U);
	CHECK(state->timeline.lease_deadline_frame == 143U);

	observation->timeline.frame = 102U;
	CHECK(SG_RelayWallStep(state, observation) ==
	    SG_RELAY_WALL_COMMAND_ZERO);
	observation->timeline.frame = 103U;
	observation->timeline.mechanism_active = 1U;
	observation->wall_open = 1U;
	observation->active_transition_authenticated = 1U;
	observation->event_source_key = 20U;
	CHECK(SG_RelayWallStep(state, observation) ==
	    SG_RELAY_WALL_COMMAND_ZERO);
	CHECK(state->timeline.phase == SG_MECHANISM_TIMELINE_ACTIVE);
	observation->active_transition_authenticated = 0U;
	observation->event_source_key = 0U;
}

static void TestDwellLeaseCrossAndRestore(void)
{
	sg_relay_wall_state_t state;
	sg_relay_wall_spec_t spec = Gate();
	sg_relay_wall_observation_t observation = Observation(100U);

	CHECK(SG_RelayWallBegin(&state, &spec, &observation));
	CHECK(SG_RelayWallStep(&state, &observation) ==
	    SG_RELAY_WALL_COMMAND_APPROACH);
	Activate(&state, &observation);

	observation.timeline.frame = 104U;
	observation.timeline.egress_requested = 1U;
	CHECK(SG_RelayWallStep(&state, &observation) ==
	    SG_RELAY_WALL_COMMAND_CROSS);
	observation.timeline.frame = 106U;
	observation.timeline.egress_arrived = 1U;
	CHECK(SG_RelayWallStep(&state, &observation) ==
	    SG_RELAY_WALL_COMMAND_RESTORE);
	CHECK(state.status == SG_RELAY_WALL_RESTORING);

	observation.timeline.mechanism_active = 0U;
	observation.wall_open = 0U;
	observation.restoration_authenticated = 1U;
	observation.event_source_key = 21U;
	CHECK(SG_RelayWallStep(&state, &observation) ==
	    SG_RELAY_WALL_COMMAND_NONE);
	CHECK(state.status == SG_RELAY_WALL_COMPLETE);
	CHECK(state.terminal_reason == SG_MECHANISM_TIMELINE_REASON_NONE);
}

static void TestExpiryRestoresThenFails(void)
{
	sg_relay_wall_state_t state;
	sg_relay_wall_spec_t spec = Gate();
	sg_relay_wall_observation_t observation = Observation(100U);

	CHECK(SG_RelayWallBegin(&state, &spec, &observation));
	Activate(&state, &observation);
	observation.timeline.frame = 143U;
	CHECK(SG_RelayWallStep(&state, &observation) ==
	    SG_RELAY_WALL_COMMAND_RESTORE);
	CHECK(state.status == SG_RELAY_WALL_RESTORING);
	CHECK(state.terminal_reason == SG_MECHANISM_TIMELINE_REASON_LEASE_EXPIRED);

	observation.timeline.mechanism_active = 0U;
	observation.wall_open = 0U;
	observation.restoration_authenticated = 1U;
	observation.event_source_key = 21U;
	CHECK(SG_RelayWallStep(&state, &observation) ==
	    SG_RELAY_WALL_COMMAND_NONE);
	CHECK(state.status == SG_RELAY_WALL_FAILED);
}

static void TestRestorationWaitsForClearBody(void)
{
	sg_relay_wall_state_t state;
	sg_relay_wall_spec_t spec = Gate();
	sg_relay_wall_observation_t observation = Observation(100U);

	CHECK(SG_RelayWallBegin(&state, &spec, &observation));
	Activate(&state, &observation);
	observation.timeline.frame = 143U;
	observation.body_clear = 0U;
	CHECK(SG_RelayWallStep(&state, &observation) ==
	    SG_RELAY_WALL_COMMAND_ZERO);
	CHECK(state.status == SG_RELAY_WALL_RESTORING);
	observation.timeline.frame = 144U;
	observation.body_clear = 1U;
	CHECK(SG_RelayWallStep(&state, &observation) ==
	    SG_RELAY_WALL_COMMAND_RESTORE);
}

static void TestAuthenticatedIdentities(void)
{
	sg_relay_wall_state_t state;
	sg_relay_wall_spec_t spec = Gate();
	sg_relay_wall_observation_t observation = Observation(100U);

	CHECK(SG_RelayWallBegin(&state, &spec, &observation));
	observation.timeline.frame = 101U;
	observation.timeline.approach_arrived = 1U;
	observation.timeline.activation_authenticated = 1U;
	CHECK(SG_RelayWallStep(&state, &observation) ==
	    SG_RELAY_WALL_COMMAND_ZERO);
	observation.timeline.frame = 103U;
	observation.timeline.mechanism_active = 1U;
	observation.wall_open = 1U;
	observation.active_transition_authenticated = 1U;
	observation.event_source_key = 99U;
	CHECK(SG_RelayWallStep(&state, &observation) ==
	    SG_RELAY_WALL_COMMAND_RESTORE);
	CHECK(state.status == SG_RELAY_WALL_RESTORING);
	CHECK(state.terminal_reason == SG_MECHANISM_TIMELINE_REASON_SOURCE_DRIFT);

	observation.timeline.mechanism_active = 0U;
	observation.wall_open = 0U;
	observation.active_transition_authenticated = 0U;
	observation.restoration_authenticated = 1U;
	observation.event_source_key = 99U;
	CHECK(SG_RelayWallStep(&state, &observation) ==
	    SG_RELAY_WALL_COMMAND_NONE);
	CHECK(state.status == SG_RELAY_WALL_FAILED);
}

static void TestUnauthenticatedOpenFailsClosed(void)
{
	sg_relay_wall_state_t state;
	sg_relay_wall_spec_t spec = Gate();
	sg_relay_wall_observation_t observation = Observation(100U);

	CHECK(SG_RelayWallBegin(&state, &spec, &observation));
	observation.timeline.frame = 101U;
	observation.timeline.approach_arrived = 1U;
	observation.timeline.activation_authenticated = 1U;
	CHECK(SG_RelayWallStep(&state, &observation) ==
	    SG_RELAY_WALL_COMMAND_ZERO);
	observation.timeline.frame = 103U;
	observation.timeline.mechanism_active = 1U;
	observation.wall_open = 1U;
	CHECK(SG_RelayWallStep(&state, &observation) ==
	    SG_RELAY_WALL_COMMAND_RESTORE);
	CHECK(state.status == SG_RELAY_WALL_RESTORING);
	CHECK(state.terminal_reason ==
	    SG_MECHANISM_TIMELINE_REASON_UNEXPECTED_EVENT);
}

static void TestDeathAndFanoutDriftRestore(void)
{
	sg_relay_wall_state_t state;
	sg_relay_wall_spec_t spec = Gate();
	sg_relay_wall_observation_t observation = Observation(100U);

	CHECK(SG_RelayWallBegin(&state, &spec, &observation));
	Activate(&state, &observation);
	observation.timeline.frame = 104U;
	observation.timeline.alive = 0U;
	CHECK(SG_RelayWallStep(&state, &observation) ==
	    SG_RELAY_WALL_COMMAND_RESTORE);
	CHECK(state.terminal_reason == SG_MECHANISM_TIMELINE_REASON_DEAD);

	observation = Observation(100U);
	CHECK(SG_RelayWallBegin(&state, &spec, &observation));
	Activate(&state, &observation);
	observation.timeline.frame = 104U;
	observation.timeline.fanout_identity++;
	CHECK(SG_RelayWallStep(&state, &observation) ==
	    SG_RELAY_WALL_COMMAND_RESTORE);
	CHECK(state.terminal_reason ==
	    SG_MECHANISM_TIMELINE_REASON_FANOUT_DRIFT);
}

int main(void)
{
	TestDwellLeaseCrossAndRestore();
	TestExpiryRestoresThenFails();
	TestRestorationWaitsForClearBody();
	TestAuthenticatedIdentities();
	TestUnauthenticatedOpenFailsClosed();
	TestDeathAndFanoutDriftRestore();
	if (failures)
	{
		fprintf(stderr, "sg_relay_wall_transaction_test: %d failures\n",
		    failures);
		return 1;
	}
	puts("sg_relay_wall_transaction_test: ok");
	return 0;
}
