/* Explicitly test-only construction and raw-batch regression seam. */
#ifndef SG_HUMAN_TRACE_LEARNING_GAME_TEST_H
#define SG_HUMAN_TRACE_LEARNING_GAME_TEST_H

#ifndef SG_HUMAN_TRACE_LEARNING_TEST
#error "sg_human_trace_learning_game_test.h is test-only"
#endif

#include "sg_human_trace_learning_game_private.h"

int SG_HumanTraceLearningTestRuntimeInit(sg_human_trace_learning_runtime_t *runtime,
	sg_human_trace_learning_parameters_t *parameters,
	const sg_human_trace_learning_workspace_t *workspace,
	sg_human_trace_learning_playthrough_t *playthroughs,
	uint32_t playthrough_capacity, uint64_t first_transaction_id);
sg_human_trace_learning_apply_status_t SG_HumanTraceLearningTestApplyBatch(
	sg_human_trace_learning_runtime_t *runtime,
	const sg_human_trace_learning_batch_t *batch,
	const sg_human_trace_learning_trace_v3_auth_t *authenticated_trace);

#endif /* SG_HUMAN_TRACE_LEARNING_GAME_TEST_H */
