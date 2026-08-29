/* Test-only model-assembly fixture for recorder-host integration tests. */
#ifndef SG_HUMAN_TRACE_LEARNING_HOST_GAME_TEST_H
#define SG_HUMAN_TRACE_LEARNING_HOST_GAME_TEST_H

#ifndef SG_HUMAN_TRACE_LEARNING_TEST
#error "sg_human_trace_learning_host_game_test.h is test-only"
#endif

#include "sg_human_trace.h"
#include "sg_human_trace_learning_game_test.h"
#include "sg_human_trace_learning_host_game.h"

typedef int (*sg_human_trace_learning_test_hook_kernel_locator_fn)(void *context,
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_human_trace_v3_event_t *fire,
	const sg_human_trace_v3_event_t *attach,
	sg_human_trace_learning_kernel_key_t *key_out);

typedef struct sg_human_trace_learning_test_published_runtime_s
{
	sg_human_trace_learning_runtime_t *runtime;
	const sg_rune_runtime_snapshot_t *snapshot;
	sg_level_identity_t level_identity;
	void *evidence_context;
	sg_human_trace_learning_test_hook_kernel_locator_fn locate_hook_kernel;
} sg_human_trace_learning_test_published_runtime_t;

int SG_HumanTraceLearningHostGameTestPublishRuntime(
	const sg_human_trace_learning_test_published_runtime_t *published,
	sg_human_trace_learning_host_report_t *report_out);
void SG_HumanTraceLearningHostGameTestWithdrawRuntime(
	const sg_human_trace_learning_runtime_t *runtime,
	const sg_rune_runtime_snapshot_t *snapshot);
void SG_HumanTraceLearningHostGameTestResetVisitCount(void);
uint64_t SG_HumanTraceLearningHostGameTestVisitCount(void);

#endif /* SG_HUMAN_TRACE_LEARNING_HOST_GAME_TEST_H */
