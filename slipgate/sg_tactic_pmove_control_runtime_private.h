/* Owner-private live admission for v13 PMove control regions. */
#ifndef SG_TACTIC_PMOVE_CONTROL_RUNTIME_PRIVATE_H
#define SG_TACTIC_PMOVE_CONTROL_RUNTIME_PRIVATE_H

#include "sg_host_engine_runtime_private.h"
#include "sg_rune_compact_pmove_control.h"

typedef struct sg_tactic_pmove_control_result_s
{
	sg_rune_pmove_control_state_t state;
	uint64_t source_units;
	uint64_t next_units;
	uint64_t live_local_units;
	uint32_t transition;
} sg_tactic_pmove_control_result_t;

int SG_TacticPmoveControlRuntimeAdmit(
	const sg_rune_pmove_control_model_t *model,
	const sg_host_engine_runtime_t *host,
	const sg_host_engine_subject_identity_t *subject, uint32_t region,
	uint32_t selected_portal,
	const sg_rune_pmove_control_state_t *live,
	uint64_t authenticated_tail_units,
	const sg_host_pmove_replay_workspace_t *workspace,
	sg_tactic_pmove_control_result_t *result_out,
	sg_rune_pmove_control_error_t *error_out);

#endif /* SG_TACTIC_PMOVE_CONTROL_RUNTIME_PRIVATE_H */
