/* Production owner for same-frame compact tactic selection. */
#ifndef SG_TACTIC_RUNTIME_H
#define SG_TACTIC_RUNTIME_H

#include "sg_compact_localization.h"
#include "sg_strategy_caller.h"
#include "sg_tactic_contract.h"

struct sg_strategy_caller_output_proof_s;
struct sg_strategy_runtime_caller_query_proof_s;

typedef enum sg_tactic_runtime_status_e
{
	SG_TACTIC_RUNTIME_OK = 0,
	SG_TACTIC_RUNTIME_INVALID_ARGUMENT,
	SG_TACTIC_RUNTIME_NOT_CURRENT,
	SG_TACTIC_RUNTIME_STALE_FRAME,
	SG_TACTIC_RUNTIME_FIELD_REJECTED,
	SG_TACTIC_RUNTIME_PROBE_REJECTED,
	SG_TACTIC_RUNTIME_AMBIGUOUS_SUCCESSOR,
	SG_TACTIC_RUNTIME_NO_LEGAL_CAPABILITY,
	SG_TACTIC_RUNTIME_STATUS_COUNT
} sg_tactic_runtime_status_t;

typedef struct sg_tactic_runtime_step_input_s
{
	const sg_rune_compact_model_t *model;
	sg_strategy_caller_t *strategy_caller;
	const sg_strategy_caller_output_t *strategy_output;
	const struct sg_strategy_caller_output_proof_s *strategy_proof;
	const struct sg_strategy_runtime_caller_query_proof_s *query_proof;
	const sg_compact_localized_state_t *localized;
	const sg_rune_compact_field_local_context_t *local_context;
	const sg_rune_compact_field_result_t *field_result;
} sg_tactic_runtime_step_input_t;

/* Install/clear are called only by the accepted compact level owner. */
int SG_TacticRuntimeProviderInstall(const sg_rune_compact_model_t *model,
	sg_rune_compact_field_service_t *field_service,
	const sg_compact_localization_binding_t *localization,
	uint64_t rune_identity,
	uint64_t topology_revision);
void SG_TacticRuntimeProviderClear(
	sg_rune_compact_field_service_t *field_service);
int SG_TacticRuntimeProviderCurrent(
	const sg_rune_compact_field_service_t *field_service);

/* Terminal field results remain strategy-owned.  This operation accepts only
 * one authenticated EXECUTE/STEP and returns one selector result. */
sg_tactic_runtime_status_t SG_TacticRuntimeSelectStep(
	const sg_tactic_runtime_step_input_t *input,
	sg_tactic_result_t *result_out);

const char *SG_TacticRuntimeStatusString(sg_tactic_runtime_status_t status);

#endif /* SG_TACTIC_RUNTIME_H */
