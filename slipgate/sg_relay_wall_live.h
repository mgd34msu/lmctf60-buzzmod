/* sg_relay_wall_live.h -- delayed-event adapter for temporary relay walls. */
#ifndef SG_RELAY_WALL_LIVE_H
#define SG_RELAY_WALL_LIVE_H

#include <stdint.h>

#include "sg_delayed_use_ticket.h"
#include "sg_relay_wall_transaction.h"

typedef struct sg_relay_wall_live_spec_s
{
	sg_relay_wall_spec_t transaction;
	uint32_t link_index;
	uint32_t button_generation;
	uint32_t restoration_generation;
	uint64_t activator_identity;
	uint64_t dwell_ticket_id;
	uint64_t restoration_ticket_id;
	uint16_t speaker_events_per_fanout;
} sg_relay_wall_live_spec_t;

typedef struct sg_relay_wall_live_effect_s
{
	uint16_t speaker_events;
	uint8_t wall_open;
	uint8_t hurt_disabled;
} sg_relay_wall_live_effect_t;

typedef int (*sg_relay_wall_live_dispatch_fn)(void *context,
	uint32_t source_key, uint16_t event_order,
	sg_relay_wall_live_effect_t *effect_out);
typedef int (*sg_relay_wall_live_body_clear_fn)(void *context,
	uint32_t wall_key);

typedef enum sg_relay_wall_live_restore_result_e
{
	SG_RELAY_WALL_LIVE_REJECTED = 0,
	SG_RELAY_WALL_LIVE_WAIT_CLEAR,
	SG_RELAY_WALL_LIVE_RESTORED
} sg_relay_wall_live_restore_result_t;

typedef struct sg_relay_wall_live_state_s
{
	sg_relay_wall_live_spec_t spec;
	sg_relay_wall_state_t transaction;
	sg_delayed_use_ticket_t dwell_ticket;
	sg_delayed_use_ticket_t restoration_ticket;
} sg_relay_wall_live_state_t;

int SG_RelayWallLiveBegin(sg_relay_wall_live_state_t *state,
	const sg_relay_wall_live_spec_t *spec,
	const sg_relay_wall_observation_t *observation);
int SG_RelayWallLiveArmDwell(sg_relay_wall_live_state_t *state,
	const sg_relay_wall_observation_t *observation);
int SG_RelayWallLiveConsumeDwell(sg_relay_wall_live_state_t *state,
	const sg_delayed_use_ticket_observation_t *ticket_observation,
	void *context, sg_relay_wall_live_dispatch_fn dispatch);
int SG_RelayWallLiveEgress(sg_relay_wall_live_state_t *state,
	const sg_relay_wall_observation_t *observation);
int SG_RelayWallLiveAbort(sg_relay_wall_live_state_t *state,
	const sg_relay_wall_observation_t *observation);
int SG_RelayWallLiveRetireActivator(sg_relay_wall_live_state_t *state,
	uint64_t activator_identity);
sg_relay_wall_live_restore_result_t SG_RelayWallLiveConsumeRestoration(
	sg_relay_wall_live_state_t *state,
	const sg_delayed_use_ticket_observation_t *ticket_observation,
	void *context, sg_relay_wall_live_body_clear_fn body_clear,
	sg_relay_wall_live_dispatch_fn dispatch);

#endif /* SG_RELAY_WALL_LIVE_H */
