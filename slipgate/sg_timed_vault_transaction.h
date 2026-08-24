/* sg_timed_vault_transaction.h -- pure timed-vault transaction reducer. */
#ifndef SG_TIMED_VAULT_TRANSACTION_H
#define SG_TIMED_VAULT_TRANSACTION_H

#include <stdint.h>

#define SG_TIMED_VAULT_TOUCH_TO_DISPATCH_FRAMES 2U
#define SG_TIMED_VAULT_SHORT_RELAY_FRAMES 10U
#define SG_TIMED_VAULT_SAFE_LEASE_FRAMES 90U
#define SG_TIMED_VAULT_RESTORE_RELAY_FRAMES 100U

typedef enum sg_timed_vault_command_e
{
	SG_TIMED_VAULT_COMMAND_NONE = 0,
	SG_TIMED_VAULT_COMMAND_HOLD,
	SG_TIMED_VAULT_COMMAND_ENTER,
	SG_TIMED_VAULT_COMMAND_EGRESS
} sg_timed_vault_command_t;

typedef enum sg_timed_vault_phase_e
{
	SG_TIMED_VAULT_PHASE_INVALID = 0,
	SG_TIMED_VAULT_PHASE_WAIT_DISPATCH,
	SG_TIMED_VAULT_PHASE_WAIT_READY,
	SG_TIMED_VAULT_PHASE_ACTIVE,
	SG_TIMED_VAULT_PHASE_EGRESS,
	SG_TIMED_VAULT_PHASE_WAIT_RESTORE,
	SG_TIMED_VAULT_PHASE_WAIT_BODY_CLEAR,
	SG_TIMED_VAULT_PHASE_TERMINAL
} sg_timed_vault_phase_t;

typedef enum sg_timed_vault_outcome_e
{
	SG_TIMED_VAULT_OUTCOME_PENDING = 0,
	SG_TIMED_VAULT_OUTCOME_SUCCEEDED,
	SG_TIMED_VAULT_OUTCOME_FAILED
} sg_timed_vault_outcome_t;

typedef enum sg_timed_vault_restoration_e
{
	SG_TIMED_VAULT_RESTORATION_NONE = 0,
	SG_TIMED_VAULT_RESTORATION_REQUIRED,
	SG_TIMED_VAULT_RESTORATION_OBSERVED,
	SG_TIMED_VAULT_RESTORATION_DISCHARGED
} sg_timed_vault_restoration_t;

typedef enum sg_timed_vault_reason_e
{
	SG_TIMED_VAULT_REASON_NONE = 0,
	SG_TIMED_VAULT_REASON_INVALID,
	SG_TIMED_VAULT_REASON_FRAME_DRIFT,
	SG_TIMED_VAULT_REASON_DEAD,
	SG_TIMED_VAULT_REASON_DISCONNECTED,
	SG_TIMED_VAULT_REASON_BINDING_DRIFT,
	SG_TIMED_VAULT_REASON_SOURCE_DRIFT,
	SG_TIMED_VAULT_REASON_FANOUT_DRIFT,
	SG_TIMED_VAULT_REASON_UNEXPECTED_EVENT,
	SG_TIMED_VAULT_REASON_MISSING_EVENT,
	SG_TIMED_VAULT_REASON_PARTIAL_EVENT,
	SG_TIMED_VAULT_REASON_DUPLICATE_EVENT
} sg_timed_vault_reason_t;

typedef struct sg_timed_vault_spec_s
{
	uint32_t source_key;
	uint32_t short_relay_key;
	uint32_t restore_relay_key;
	uint32_t fanout_identity;
	uint16_t dispatch_target_count;
	uint16_t device_target_count;
	uint8_t door_leaf_count;
} sg_timed_vault_spec_t;

typedef struct sg_timed_vault_observation_s
{
	uint32_t frame;
	uint32_t binding_source_key;
	uint32_t binding_fanout_identity;
	uint32_t event_source_key;
	uint32_t event_fanout_identity;
	uint16_t event_target_count;
	uint8_t door_top_count;
	uint8_t alive;
	uint8_t connected;
	uint8_t binding_current;
	uint8_t touch_authenticated;
	uint8_t dispatch_authenticated;
	uint8_t short_relay_authenticated;
	uint8_t flag_pickup;
	uint8_t restore_relay_authenticated;
	uint8_t body_clear;
} sg_timed_vault_observation_t;

typedef struct sg_timed_vault_state_s
{
	sg_timed_vault_spec_t spec;
	sg_timed_vault_phase_t phase;
	sg_timed_vault_outcome_t outcome;
	sg_timed_vault_restoration_t restoration;
	sg_timed_vault_reason_t reason;
	uint32_t touch_frame;
	uint32_t dispatch_frame;
	uint32_t ready_frame;
	uint32_t lease_deadline_frame;
	uint32_t restore_frame;
	uint32_t last_frame;
	uint8_t dispatch_seen;
	uint8_t short_relay_seen;
	uint8_t pickup_seen;
	uint8_t restore_relay_seen;
} sg_timed_vault_state_t;

typedef struct sg_timed_vault_reduction_s
{
	sg_timed_vault_state_t state;
	sg_timed_vault_command_t command;
} sg_timed_vault_reduction_t;

sg_timed_vault_reduction_t SG_TimedVaultBegin(
	const sg_timed_vault_spec_t *spec,
	const sg_timed_vault_observation_t *observation);
sg_timed_vault_reduction_t SG_TimedVaultReduce(
	const sg_timed_vault_state_t *state,
	const sg_timed_vault_observation_t *observation);

#endif /* SG_TIMED_VAULT_TRANSACTION_H */
