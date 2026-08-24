/* sg_delayed_use_ticket.h -- one-shot authenticated delayed target authority. */
#ifndef SG_DELAYED_USE_TICKET_H
#define SG_DELAYED_USE_TICKET_H

#include <stdint.h>

typedef enum sg_delayed_use_ticket_state_e
{
	SG_DELAYED_USE_TICKET_EMPTY = 0,
	SG_DELAYED_USE_TICKET_ARMED,
	SG_DELAYED_USE_TICKET_CONSUMED,
	SG_DELAYED_USE_TICKET_CANCELLED,
	SG_DELAYED_USE_TICKET_INVALID
} sg_delayed_use_ticket_state_t;

typedef enum sg_delayed_use_ticket_result_e
{
	SG_DELAYED_USE_TICKET_REJECTED = 0,
	SG_DELAYED_USE_TICKET_AUTHORIZED = 1
} sg_delayed_use_ticket_result_t;

typedef struct sg_delayed_use_ticket_spec_s
{
	uint64_t ticket_id;
	uint64_t activator_identity;
	uint32_t source_key;
	uint32_t source_generation;
	uint32_t fanout_identity;
	uint32_t link_index;
	uint16_t event_order;
	uint8_t durable;
} sg_delayed_use_ticket_spec_t;

typedef struct sg_delayed_use_ticket_observation_s
{
	uint64_t ticket_id;
	uint64_t activator_identity;
	uint32_t source_key;
	uint32_t source_generation;
	uint32_t fanout_identity;
	uint32_t link_index;
	uint16_t event_order;
	uint8_t source_current;
	uint8_t fanout_current;
	uint8_t activator_current;
} sg_delayed_use_ticket_observation_t;

typedef struct sg_delayed_use_ticket_s
{
	sg_delayed_use_ticket_spec_t spec;
	sg_delayed_use_ticket_state_t state;
} sg_delayed_use_ticket_t;

int SG_DelayedUseTicketTag(sg_delayed_use_ticket_t *ticket,
	const sg_delayed_use_ticket_spec_t *spec);
sg_delayed_use_ticket_result_t SG_DelayedUseTicketConsume(
	sg_delayed_use_ticket_t *ticket,
	const sg_delayed_use_ticket_observation_t *observation);
int SG_DelayedUseTicketRetireActivator(sg_delayed_use_ticket_t *ticket,
	uint64_t activator_identity);

#endif /* SG_DELAYED_USE_TICKET_H */
