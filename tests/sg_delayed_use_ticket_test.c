#include <stdio.h>
#include <string.h>

#include "slipgate/sg_delayed_use_ticket.h"

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		    __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static sg_delayed_use_ticket_spec_t Spec(int durable)
{
	sg_delayed_use_ticket_spec_t spec;

	memset(&spec, 0, sizeof(spec));
	spec.ticket_id = UINT64_C(0x12345678);
	spec.activator_identity = UINT64_C(0xabcdef);
	spec.source_key = 30U;
	spec.source_generation = 44U;
	spec.fanout_identity = 0xfeedU;
	spec.link_index = 9U;
	spec.event_order = 3U;
	spec.durable = durable ? 1U : 0U;
	return spec;
}

static sg_delayed_use_ticket_observation_t Observation(void)
{
	sg_delayed_use_ticket_observation_t observation;

	memset(&observation, 0, sizeof(observation));
	observation.ticket_id = UINT64_C(0x12345678);
	observation.activator_identity = UINT64_C(0xabcdef);
	observation.source_key = 30U;
	observation.source_generation = 44U;
	observation.fanout_identity = 0xfeedU;
	observation.link_index = 9U;
	observation.event_order = 3U;
	observation.source_current = 1U;
	observation.fanout_current = 1U;
	observation.activator_current = 1U;
	return observation;
}

static void TestOneShotConsume(void)
{
	sg_delayed_use_ticket_t ticket;
	sg_delayed_use_ticket_spec_t spec = Spec(0);
	sg_delayed_use_ticket_observation_t observation = Observation();

	CHECK(SG_DelayedUseTicketTag(&ticket, &spec));
	CHECK(SG_DelayedUseTicketConsume(&ticket, &observation) ==
	    SG_DELAYED_USE_TICKET_AUTHORIZED);
	CHECK(SG_DelayedUseTicketConsume(&ticket, &observation) ==
	    SG_DELAYED_USE_TICKET_REJECTED);
}

static void TestIdentityAndOrderDriftFailClosed(void)
{
	sg_delayed_use_ticket_t ticket;
	sg_delayed_use_ticket_spec_t spec = Spec(0);
	sg_delayed_use_ticket_observation_t observation = Observation();

	CHECK(SG_DelayedUseTicketTag(&ticket, &spec));
	observation.event_order++;
	CHECK(SG_DelayedUseTicketConsume(&ticket, &observation) ==
	    SG_DELAYED_USE_TICKET_REJECTED);
	CHECK(ticket.state == SG_DELAYED_USE_TICKET_INVALID);

	CHECK(SG_DelayedUseTicketTag(&ticket, &spec));
	observation = Observation();
	observation.source_generation++;
	CHECK(SG_DelayedUseTicketConsume(&ticket, &observation) ==
	    SG_DELAYED_USE_TICKET_REJECTED);

	CHECK(SG_DelayedUseTicketTag(&ticket, &spec));
	observation = Observation();
	observation.fanout_identity++;
	CHECK(SG_DelayedUseTicketConsume(&ticket, &observation) ==
	    SG_DELAYED_USE_TICKET_REJECTED);
}

static void TestDurableRestorationSurvivesActivatorRetirement(void)
{
	sg_delayed_use_ticket_t ticket;
	sg_delayed_use_ticket_spec_t spec = Spec(1);
	sg_delayed_use_ticket_observation_t observation = Observation();

	CHECK(SG_DelayedUseTicketTag(&ticket, &spec));
	CHECK(SG_DelayedUseTicketRetireActivator(&ticket,
	    spec.activator_identity));
	CHECK(ticket.state == SG_DELAYED_USE_TICKET_ARMED);
	observation.activator_current = 0U;
	CHECK(SG_DelayedUseTicketConsume(&ticket, &observation) ==
	    SG_DELAYED_USE_TICKET_AUTHORIZED);

	spec = Spec(0);
	CHECK(SG_DelayedUseTicketTag(&ticket, &spec));
	CHECK(!SG_DelayedUseTicketRetireActivator(&ticket,
	    spec.activator_identity));
	CHECK(ticket.state == SG_DELAYED_USE_TICKET_CANCELLED);
}

static void TestMalformedValuesReject(void)
{
	sg_delayed_use_ticket_t ticket;
	sg_delayed_use_ticket_spec_t spec = Spec(1);

	spec.source_key = 0U;
	CHECK(!SG_DelayedUseTicketTag(&ticket, &spec));
	spec = Spec(1);
	spec.event_order = 0U;
	CHECK(!SG_DelayedUseTicketTag(&ticket, &spec));
	spec = Spec(1);
	spec.durable = 2U;
	CHECK(!SG_DelayedUseTicketTag(&ticket, &spec));
}

int main(void)
{
	TestOneShotConsume();
	TestIdentityAndOrderDriftFailClosed();
	TestDurableRestorationSurvivesActivatorRetirement();
	TestMalformedValuesReject();
	if (failures)
	{
		fprintf(stderr, "sg_delayed_use_ticket_test: %d failures\n",
		    failures);
		return 1;
	}
	puts("sg_delayed_use_ticket_test: ok");
	return 0;
}
