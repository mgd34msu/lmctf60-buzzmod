/* Frame-local dispatch for authenticated compact destination-field results. */
#ifndef SG_TACTIC_EXECUTION_H
#define SG_TACTIC_EXECUTION_H

#include "sg_tactic_contract.h"

typedef enum sg_tactic_execution_kind_e
{
	SG_TACTIC_EXECUTION_DISCONNECTED = 0,
	SG_TACTIC_EXECUTION_LOCAL_DESTINATION,
	SG_TACTIC_EXECUTION_CELL_DESTINATION,
	SG_TACTIC_EXECUTION_MECHANISMS_REQUIRED,
	SG_TACTIC_EXECUTION_BLOCKED_NOW,
	SG_TACTIC_EXECUTION_PORTAL_STEP,
	SG_TACTIC_EXECUTION_DIRECT_STEP,
	SG_TACTIC_EXECUTION_STANCE_STEP,
	SG_TACTIC_EXECUTION_KIND_COUNT
} sg_tactic_execution_kind_t;

/* The field result and every pointer it contains belong to the queried field
 * and are valid only while that field lease remains live. The dispatcher
 * copies scalar geometry needed by the command owner and never retains the
 * result. */
typedef struct sg_tactic_execution_s
{
	sg_tactic_execution_kind_t kind;
	sg_rune_compact_cell_index_t current_cell;
	sg_rune_compact_cell_index_t target_cell;
	sg_rune_compact_portal_index_t portal;
	sg_rune_compact_field_stance_t target_stance;
	sg_rune_compact_field_cost_t cost_to_go;
	sg_rune_compact_field_cost_t next_cost_to_go;
	sg_rune_compact_destination_t destination;
	uint32_t mechanism_count;
	sg_rune_compact_field_mechanism_requirement_state_t mechanism_state;
	float target_point[3];
	uint8_t target_point_present;
	uint8_t selection_present;
	uint8_t mechanism_handoff_valid;
	uint8_t reserved;
	sg_tactic_result_status_t selection_status;
	sg_tactic_failure_reason_t selection_failure;
	sg_tactic_capability_t capability;
	sg_tactic_phase_t target_phase;
	sg_host_hook_phase_t target_hook_phase;
	sg_tactic_mechanism_request_t mechanism_handoff;
} sg_tactic_execution_t;

/* Exhaustively converts the six public field-result kinds into one command-
 * owner decision. Portal and direct steps include a world-space point derived
 * only from compact geometry; no field-owned pointer escapes this call. */
int SG_TacticExecutionDispatch(const sg_rune_compact_model_t *model,
	const sg_rune_compact_field_result_t *result,
	sg_tactic_execution_t *execution_out);

/* Attach one owner-authenticated selector result to a STEP dispatch.  Retry,
 * failure, WAIT, and mechanisms remain successful commitment-preserving
 * holds.  Selection does not authorize a usercmd: that requires a separate
 * production execution witness owned outside this scalar result. */
int SG_TacticExecutionDispatchSelected(const sg_rune_compact_model_t *model,
	const sg_rune_compact_field_result_t *result,
	const sg_tactic_result_t *selection,
	sg_tactic_execution_t *execution_out);

#endif /* SG_TACTIC_EXECUTION_H */
