/* Owner-private authenticated handoff from selection to live execution. */
#ifndef SG_TACTIC_RUNTIME_PRIVATE_H
#define SG_TACTIC_RUNTIME_PRIVATE_H

#include "sg_rune_compact_field_plan_private.h"
#include "sg_strategy_runtime_bridge_private.h"
#include "sg_tactic_runtime.h"

typedef struct sg_tactic_runtime_provider_snapshot_s
{
	const sg_rune_compact_model_t *model;
	sg_rune_compact_field_service_t *field_service;
	uint64_t rune_identity;
	uint64_t topology_revision;
	uint64_t owner_epoch;
	sg_compact_localization_binding_t localization;
} sg_tactic_runtime_provider_snapshot_t;

typedef struct sg_tactic_runtime_prepared_step_s
{
	sg_tactic_result_t result;
	sg_tactic_frame_capability_t frame;
	sg_tactic_candidate_t candidate;
	sg_rune_compact_field_exact_probe_t exact_probe;
	sg_tactic_runtime_provider_snapshot_t provider;
	sg_strategy_caller_t *strategy_caller;
	sg_strategy_caller_output_t strategy_output;
	sg_strategy_caller_output_proof_t strategy_proof;
	sg_strategy_caller_output_receipt_t strategy_receipt;
	sg_strategy_runtime_caller_query_proof_t query_proof;
	sg_strategy_runtime_caller_query_snapshot_t query_snapshot;
	sg_compact_localized_state_t localized;
	sg_rune_compact_field_local_context_t local_context;
	sg_rune_compact_field_result_t field_result;
	uint8_t consumed;
	uint8_t reserved[7];
} sg_tactic_runtime_prepared_step_t;

/* The live execution owner calls Prepare directly. Callers cannot inject a
 * witness or a callback. The returned value is a byte copy of the unique
 * exact probe selected for this authenticated frame. */
sg_tactic_runtime_status_t SG_TacticRuntimePrepareStep(
	const sg_tactic_runtime_step_input_t *input,
	sg_tactic_runtime_prepared_step_t *prepared_out);

/* Prepare is tentative.  The execution owner first seals the private action,
 * then consumes the exact strategy proof.  A consumed step carries the
 * read-only receipt used for the final same-frame currentness check. */
sg_tactic_runtime_status_t SG_TacticRuntimePreparedStepConsume(
	sg_tactic_runtime_prepared_step_t *prepared);
int SG_TacticRuntimePreparedStepCurrent(
	const sg_tactic_runtime_prepared_step_t *prepared);

/* Retires the exact tentative-proof or consumed-receipt query authority after
 * execution reaches a terminal path.  The prepared value is invalidated even
 * when its authority was already stale; a mismatch cannot retire another
 * query. */
int SG_TacticRuntimePreparedStepRelease(
	sg_tactic_runtime_prepared_step_t *prepared);

int SG_TacticRuntimeProviderSnapshotCurrent(
	const sg_tactic_runtime_provider_snapshot_t *snapshot);

#endif /* SG_TACTIC_RUNTIME_PRIVATE_H */
