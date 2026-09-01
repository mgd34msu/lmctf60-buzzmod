/* Owner-private strategy output and compact field authentication. */
#ifndef SG_STRATEGY_RUNTIME_BRIDGE_PRIVATE_H
#define SG_STRATEGY_RUNTIME_BRIDGE_PRIVATE_H

#include "sg_strategy_caller_private.h"
#include "sg_strategy_runtime_bridge.h"

#define SG_STRATEGY_RUNTIME_CALLER_QUERY_PROOF_BYTES 32U

typedef struct sg_strategy_runtime_caller_query_proof_s
{
	uint8_t opaque[SG_STRATEGY_RUNTIME_CALLER_QUERY_PROOF_BYTES];
} sg_strategy_runtime_caller_query_proof_t;

/* Pointer-free semantic copy of the exact live inputs used by the field
 * query. The two digests cover every ordered mechanism phase and portal-root
 * entry; callers cannot mint or validate this snapshot themselves. */
typedef struct sg_strategy_runtime_caller_query_snapshot_s
{
	sg_rune_q8_vec3_t origin;
	sg_rune_compact_field_stance_t stance;
	sg_rune_movement_support_kind_t support;
	sg_rune_movement_water_kind_t water;
	sg_host_hook_phase_t hook_phase;
	sg_rune_movement_state_flags_t state_flags;
	uint32_t mover_mechanism;
	float velocity[3];
	float direction[3];
	float time_seconds;
	float distance;
	float support_distance;
	float fluid_fraction;
	float hook_length;
	float target_radius;
	uint64_t frame_sequence;
	uint64_t mechanism_digest[2];
	uint64_t portal_root_digest[2];
} sg_strategy_runtime_caller_query_snapshot_t;

typedef struct sg_strategy_runtime_bot_observation_s
	sg_strategy_runtime_bot_observation_t;

typedef struct sg_strategy_runtime_bot_observation_view_s
{
	sg_localization_subject_t subject;
	uint64_t host_authority_epoch;
	uint64_t frame_sequence;
	uint64_t observed_at_ms;
	sg_host_hook_phase_t hook_phase;
	float hook_length;
	float target_radius;
} sg_strategy_runtime_bot_observation_view_t;

typedef int (*sg_strategy_runtime_bot_observation_validate_fn)(void *context,
	const sg_strategy_runtime_bot_observation_t *observation,
	sg_strategy_runtime_bot_observation_view_t *view_out);
typedef int (*sg_strategy_runtime_bot_observation_current_fn)(void *context,
	const sg_strategy_runtime_bot_observation_view_t *view);

typedef struct sg_strategy_runtime_bot_observation_owner_s
{
	void *context;
	sg_strategy_runtime_bot_observation_validate_fn validate;
	sg_strategy_runtime_bot_observation_current_fn current;
} sg_strategy_runtime_bot_observation_owner_t;

/* Query and proof issuance are one boundary.  The caller and provider are
 * checked before and after the frame-local field query; failure clears every
 * output. */
int SG_StrategyRuntimeQueryCallerOutputWithContext(
	sg_strategy_caller_t *caller,
	const sg_strategy_caller_output_t *output,
	const sg_compact_localized_state_t *localized_player,
	const sg_rune_compact_field_mechanism_snapshot_t *mechanisms,
	const sg_rune_compact_field_portal_root_snapshot_t *portal_roots,
	const sg_strategy_runtime_bot_observation_t *bot_observation,
	sg_rune_compact_field_result_t *result_out,
	sg_rune_compact_field_local_context_t *local_context_out,
	sg_strategy_caller_output_proof_t *proof_out,
	sg_strategy_runtime_caller_query_proof_t *query_proof_out);

/* Pending validation rechecks the live borrowed snapshots and emits the
 * pointer-free copy retained by the prepared tactic. */
int SG_StrategyRuntimeCallerQueryProofCurrent(
	const sg_strategy_caller_t *caller,
	const sg_strategy_caller_output_t *output,
	const sg_strategy_caller_output_proof_t *output_proof,
	const sg_rune_compact_field_local_context_t *local_context,
	const sg_rune_compact_field_result_t *field_result,
	const sg_strategy_runtime_caller_query_proof_t *query_proof,
	sg_strategy_runtime_caller_query_snapshot_t *snapshot_out);

/* After strategy-proof consumption, borrowed mechanism/root pointers are no
 * longer needed. The bridge authenticates the exact pointer-free query copy
 * and field result against its outstanding query authority. */
int SG_StrategyRuntimeCallerQueryReceiptCurrent(
	const sg_strategy_caller_t *caller,
	const sg_strategy_caller_output_t *output,
	const sg_strategy_caller_output_receipt_t *output_receipt,
	const sg_strategy_runtime_caller_query_snapshot_t *snapshot,
	const sg_rune_compact_field_result_t *field_result,
	const sg_strategy_runtime_caller_query_proof_t *query_proof);

/* Retires the retained live snapshot borrows after the synchronous execution
 * owner reaches a terminal Commit/Cancel path. A mismatched receipt/proof
 * cannot clear another outstanding query authority. */
int SG_StrategyRuntimeCallerQueryReceiptRelease(
	const sg_strategy_caller_t *caller,
	const sg_strategy_caller_output_t *output,
	const sg_strategy_caller_output_receipt_t *output_receipt,
	const sg_strategy_runtime_caller_query_proof_t *query_proof);

/* Abandons a tentative/unconsumed query while leaving the strategy output
 * proof itself current for a fresh authenticated bridge query. */
int SG_StrategyRuntimeCallerQueryProofRelease(
	const sg_strategy_caller_t *caller,
	const sg_strategy_caller_output_t *output,
	const sg_strategy_caller_output_proof_t *output_proof,
	const sg_strategy_runtime_caller_query_proof_t *query_proof);

#endif /* SG_STRATEGY_RUNTIME_BRIDGE_PRIVATE_H */
