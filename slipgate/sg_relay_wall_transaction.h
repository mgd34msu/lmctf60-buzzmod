/* sg_relay_wall_transaction.h -- authenticated temporary-wall transaction. */
#ifndef SG_RELAY_WALL_TRANSACTION_H
#define SG_RELAY_WALL_TRANSACTION_H

#include <stdint.h>

#include "sg_mechanism_timeline.h"

typedef enum sg_relay_wall_command_e
{
	SG_RELAY_WALL_COMMAND_NONE = 0,
	SG_RELAY_WALL_COMMAND_APPROACH,
	SG_RELAY_WALL_COMMAND_ZERO,
	SG_RELAY_WALL_COMMAND_CROSS,
	SG_RELAY_WALL_COMMAND_RESTORE
} sg_relay_wall_command_t;

typedef enum sg_relay_wall_status_e
{
	SG_RELAY_WALL_IDLE = 0,
	SG_RELAY_WALL_RUNNING,
	SG_RELAY_WALL_RESTORING,
	SG_RELAY_WALL_COMPLETE,
	SG_RELAY_WALL_FAILED
} sg_relay_wall_status_t;

typedef struct sg_relay_wall_spec_s
{
	sg_mechanism_timeline_spec_t timeline;
	uint32_t wall_key;
	uint32_t activation_source_key;
	uint32_t restoration_source_key;
} sg_relay_wall_spec_t;

typedef struct sg_relay_wall_observation_s
{
	sg_mechanism_timeline_observation_t timeline;
	uint32_t wall_key;
	uint32_t event_source_key;
	uint8_t wall_open;
	uint8_t active_transition_authenticated;
	uint8_t restoration_authenticated;
	uint8_t body_clear;
} sg_relay_wall_observation_t;

typedef struct sg_relay_wall_state_s
{
	sg_relay_wall_spec_t spec;
	sg_mechanism_timeline_state_t timeline;
	sg_relay_wall_status_t status;
	sg_mechanism_timeline_reason_t terminal_reason;
	uint8_t terminal_complete;
	uint8_t wall_open;
} sg_relay_wall_state_t;

int SG_RelayWallBegin(sg_relay_wall_state_t *state,
	const sg_relay_wall_spec_t *spec,
	const sg_relay_wall_observation_t *observation);
sg_relay_wall_command_t SG_RelayWallStep(sg_relay_wall_state_t *state,
	const sg_relay_wall_observation_t *observation);

#endif /* SG_RELAY_WALL_TRANSACTION_H */
