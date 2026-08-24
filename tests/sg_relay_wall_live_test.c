#include <stdio.h>
#include <string.h>

#include "slipgate/sg_relay_wall_live.h"

typedef struct host_s
{
	int dispatch_count;
	uint32_t source[2];
	uint16_t order[2];
	int body_clear;
} host_t;

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		    __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static int Dispatch(void *context, uint32_t source_key, uint16_t event_order,
	sg_relay_wall_live_effect_t *effect)
{
	host_t *host = context;
	int index;

	if (!host || !effect || host->dispatch_count >= 2)
		return 0;
	index = host->dispatch_count++;
	host->source[index] = source_key;
	host->order[index] = event_order;
	memset(effect, 0, sizeof(*effect));
	effect->wall_open = index == 0;
	effect->hurt_disabled = index == 0;
	effect->speaker_events = 2U;
	return 1;
}

static int BodyClear(void *context, uint32_t wall_key)
{
	host_t *host = context;

	return host && wall_key == 30U && host->body_clear;
}

static sg_relay_wall_live_spec_t Spec(void)
{
	sg_relay_wall_live_spec_t spec;

	memset(&spec, 0, sizeof(spec));
	spec.transaction.timeline.source_key = 10U;
	spec.transaction.timeline.fanout_identity = 0xfeedU;
	spec.transaction.timeline.approach_timeout_frames = 6U;
	spec.transaction.timeline.activation_timeout_frames = 2U;
	spec.transaction.timeline.trigger_delay_frames = 2U;
	spec.transaction.timeline.cooldown_frames = 40U;
	spec.transaction.timeline.lease_frames = 40U;
	spec.transaction.timeline.egress_timeout_frames = 8U;
	spec.transaction.wall_key = 30U;
	spec.transaction.activation_source_key = 20U;
	spec.transaction.restoration_source_key = 21U;
	spec.link_index = 9U;
	spec.button_generation = 110U;
	spec.restoration_generation = 121U;
	spec.activator_identity = UINT64_C(0xabc);
	spec.dwell_ticket_id = UINT64_C(0x1001);
	spec.restoration_ticket_id = UINT64_C(0x1002);
	spec.speaker_events_per_fanout = 2U;
	return spec;
}

static sg_relay_wall_observation_t Observation(uint32_t frame)
{
	sg_relay_wall_observation_t observation;

	memset(&observation, 0, sizeof(observation));
	observation.timeline.frame = frame;
	observation.timeline.source_key = 10U;
	observation.timeline.fanout_identity = 0xfeedU;
	observation.timeline.alive = 1U;
	observation.timeline.connected = 1U;
	observation.timeline.binding_current = 1U;
	observation.wall_key = 30U;
	observation.body_clear = 1U;
	return observation;
}

static sg_delayed_use_ticket_observation_t TicketObservation(
	const sg_delayed_use_ticket_t *ticket, int activator_current)
{
	sg_delayed_use_ticket_observation_t observation;

	memset(&observation, 0, sizeof(observation));
	observation.ticket_id = ticket->spec.ticket_id;
	observation.activator_identity = ticket->spec.activator_identity;
	observation.source_key = ticket->spec.source_key;
	observation.source_generation = ticket->spec.source_generation;
	observation.fanout_identity = ticket->spec.fanout_identity;
	observation.link_index = ticket->spec.link_index;
	observation.event_order = ticket->spec.event_order;
	observation.source_current = 1U;
	observation.fanout_current = 1U;
	observation.activator_current = activator_current ? 1U : 0U;
	return observation;
}

static void ArmAndOpen(sg_relay_wall_live_state_t *state, host_t *host)
{
	sg_relay_wall_live_spec_t spec = Spec();
	sg_relay_wall_observation_t observation = Observation(100U);
	sg_delayed_use_ticket_observation_t ticket;

	CHECK(SG_RelayWallLiveBegin(state, &spec, &observation));
	observation.timeline.frame = 101U;
	observation.timeline.approach_arrived = 1U;
	observation.timeline.activation_authenticated = 1U;
	CHECK(SG_RelayWallLiveArmDwell(state, &observation));
	CHECK(state->dwell_ticket.state == SG_DELAYED_USE_TICKET_ARMED);
	ticket = TicketObservation(&state->dwell_ticket, 1);
	CHECK(SG_RelayWallLiveConsumeDwell(state, &ticket, host, Dispatch));
	CHECK(host->dispatch_count == 1);
	CHECK(host->source[0] == 20U && host->order[0] == 1U);
	CHECK(state->transaction.status == SG_RELAY_WALL_RUNNING);
	CHECK(state->transaction.wall_open == 1U);
	CHECK(state->restoration_ticket.state == SG_DELAYED_USE_TICKET_ARMED);
}

static void TestHappyPathAndOneShotOrder(void)
{
	sg_relay_wall_live_state_t state;
	sg_relay_wall_observation_t observation;
	sg_delayed_use_ticket_observation_t ticket;
	host_t host = { 0 };

	host.body_clear = 1;
	ArmAndOpen(&state, &host);
	observation = Observation(106U);
	observation.timeline.mechanism_active = 1U;
	observation.wall_open = 1U;
	observation.timeline.egress_requested = 1U;
	observation.timeline.egress_arrived = 1U;
	CHECK(SG_RelayWallLiveEgress(&state, &observation));
	ticket = TicketObservation(&state.restoration_ticket, 1);
	CHECK(SG_RelayWallLiveConsumeRestoration(&state, &ticket, &host,
	    BodyClear, Dispatch) == SG_RELAY_WALL_LIVE_RESTORED);
	CHECK(host.dispatch_count == 2);
	CHECK(host.source[1] == 21U && host.order[1] == 2U);
	CHECK(state.transaction.status == SG_RELAY_WALL_COMPLETE);
	CHECK(SG_RelayWallLiveConsumeRestoration(&state, &ticket, &host,
	    BodyClear, Dispatch) == SG_RELAY_WALL_LIVE_REJECTED);
}

static void TestBlockedBodyAndDisconnectStillRestore(void)
{
	sg_relay_wall_live_state_t state;
	sg_relay_wall_observation_t observation;
	sg_delayed_use_ticket_observation_t ticket;
	host_t host = { 0 };

	ArmAndOpen(&state, &host);
	observation = Observation(104U);
	observation.timeline.mechanism_active = 1U;
	observation.wall_open = 1U;
	observation.timeline.connected = 0U;
	CHECK(SG_RelayWallLiveAbort(&state, &observation));
	CHECK(SG_RelayWallLiveRetireActivator(&state, UINT64_C(0xabc)));
	ticket = TicketObservation(&state.restoration_ticket, 0);
	CHECK(SG_RelayWallLiveConsumeRestoration(&state, &ticket, &host,
	    BodyClear, Dispatch) == SG_RELAY_WALL_LIVE_WAIT_CLEAR);
	CHECK(state.restoration_ticket.state == SG_DELAYED_USE_TICKET_ARMED);
	host.body_clear = 1;
	CHECK(SG_RelayWallLiveConsumeRestoration(&state, &ticket, &host,
	    BodyClear, Dispatch) == SG_RELAY_WALL_LIVE_RESTORED);
	CHECK(state.transaction.status == SG_RELAY_WALL_FAILED);
}

static void TestFanoutAndEffectDriftReject(void)
{
	sg_relay_wall_live_state_t state;
	sg_relay_wall_live_spec_t spec = Spec();
	sg_relay_wall_observation_t observation = Observation(100U);
	sg_delayed_use_ticket_observation_t ticket;
	host_t host = { 0 };

	CHECK(SG_RelayWallLiveBegin(&state, &spec, &observation));
	observation.timeline.frame = 101U;
	observation.timeline.approach_arrived = 1U;
	observation.timeline.activation_authenticated = 1U;
	CHECK(SG_RelayWallLiveArmDwell(&state, &observation));
	ticket = TicketObservation(&state.dwell_ticket, 1);
	ticket.fanout_identity++;
	CHECK(!SG_RelayWallLiveConsumeDwell(&state, &ticket, &host, Dispatch));
	CHECK(host.dispatch_count == 0);
}

int main(void)
{
	TestHappyPathAndOneShotOrder();
	TestBlockedBodyAndDisconnectStillRestore();
	TestFanoutAndEffectDriftReject();
	if (failures)
	{
		fprintf(stderr, "sg_relay_wall_live_test: %d failures\n", failures);
		return 1;
	}
	puts("sg_relay_wall_live_test: ok");
	return 0;
}
