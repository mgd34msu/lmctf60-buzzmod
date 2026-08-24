/* sg_delayed_use_ticket.c -- one-shot authenticated delayed target authority. */
#include "sg_delayed_use_ticket.h"

#include <limits.h>
#include <string.h>

static int TicketKey(uint32_t key)
{
	return key != 0U && key != UINT32_MAX;
}

static int TicketSpecValid(const sg_delayed_use_ticket_spec_t *spec)
{
	return spec && spec->ticket_id != 0U &&
	       spec->activator_identity != 0U && TicketKey(spec->source_key) &&
	       spec->source_generation != 0U && spec->fanout_identity != 0U &&
	       spec->link_index != UINT32_MAX && spec->event_order != 0U &&
	       spec->durable <= 1U;
}

int SG_DelayedUseTicketTag(sg_delayed_use_ticket_t *ticket,
	const sg_delayed_use_ticket_spec_t *spec)
{
	if (!ticket)
		return 0;
	memset(ticket, 0, sizeof(*ticket));
	if (!TicketSpecValid(spec))
	{
		ticket->state = SG_DELAYED_USE_TICKET_INVALID;
		return 0;
	}
	ticket->spec = *spec;
	ticket->state = SG_DELAYED_USE_TICKET_ARMED;
	return 1;
}

sg_delayed_use_ticket_result_t SG_DelayedUseTicketConsume(
	sg_delayed_use_ticket_t *ticket,
	const sg_delayed_use_ticket_observation_t *observation)
{
	if (!ticket || ticket->state != SG_DELAYED_USE_TICKET_ARMED ||
	    !observation)
		return SG_DELAYED_USE_TICKET_REJECTED;
	/* Consume before any caller can dispatch callbacks.  A mismatch burns the
	 * authority too: mutated delayed work must never become retryable. */
	ticket->state = SG_DELAYED_USE_TICKET_INVALID;
	if (observation->ticket_id != ticket->spec.ticket_id ||
	    observation->activator_identity != ticket->spec.activator_identity ||
	    observation->source_key != ticket->spec.source_key ||
	    observation->source_generation != ticket->spec.source_generation ||
	    observation->fanout_identity != ticket->spec.fanout_identity ||
	    observation->link_index != ticket->spec.link_index ||
	    observation->event_order != ticket->spec.event_order ||
	    observation->source_current != 1U ||
	    observation->fanout_current != 1U ||
	    (!ticket->spec.durable && observation->activator_current != 1U) ||
	    observation->activator_current > 1U)
		return SG_DELAYED_USE_TICKET_REJECTED;
	ticket->state = SG_DELAYED_USE_TICKET_CONSUMED;
	return SG_DELAYED_USE_TICKET_AUTHORIZED;
}

int SG_DelayedUseTicketRetireActivator(sg_delayed_use_ticket_t *ticket,
	uint64_t activator_identity)
{
	if (!ticket || ticket->state != SG_DELAYED_USE_TICKET_ARMED ||
	    activator_identity == 0U ||
	    ticket->spec.activator_identity != activator_identity)
		return 0;
	if (ticket->spec.durable)
		return 1;
	ticket->state = SG_DELAYED_USE_TICKET_CANCELLED;
	return 0;
}
