/* Engine-owned post-match consumption of authenticated human-trace spools. */
#ifndef SG_HUMAN_TRACE_LEARNING_HOST_GAME_H
#define SG_HUMAN_TRACE_LEARNING_HOST_GAME_H

#include "sg_human_trace.h"
#include "sg_human_trace_learning_game_private.h"

typedef struct sg_human_trace_learning_host_report_s
{
	uint64_t queued_batches;
	uint64_t derived_batches;
	uint64_t derived_records;
	uint64_t committed_batches;
	uint64_t rejected_batches;
	uint64_t pending_batches;
	uint8_t trace_authenticated;
	uint8_t runtime_published;
} sg_human_trace_learning_host_report_t;

typedef int (*sg_human_trace_learning_hook_kernel_locator_fn)(void *context,
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_human_trace_v3_event_t *fire,
	const sg_human_trace_v3_event_t *attach,
	sg_human_trace_learning_kernel_key_t *key_out);

typedef struct sg_human_trace_learning_host_runtime_publication_s
{
	sg_human_trace_learning_runtime_t *runtime;
	const sg_rune_runtime_snapshot_t *snapshot;
	sg_level_identity_t level_identity;
	void *evidence_context;
	sg_human_trace_learning_hook_kernel_locator_fn locate_hook_kernel;
} sg_human_trace_learning_host_runtime_publication_t;

/* The model-assembly owner publishes a live runtime through this durable
 * boundary. Existing state is restored before evidence is considered, and a
 * scope becomes consumed only in the same atomic state image as its learned
 * parameters. */
SG_HUMAN_TRACE_LEARNING_LOCAL int SG_HumanTraceLearningHostGamePublishRuntime(
	const sg_human_trace_learning_host_runtime_publication_t *publication,
	sg_human_trace_learning_host_report_t *report_out);
SG_HUMAN_TRACE_LEARNING_LOCAL void SG_HumanTraceLearningHostGameWithdrawRuntime(
	const sg_human_trace_learning_runtime_t *runtime,
	const sg_rune_runtime_snapshot_t *snapshot);

/* Post-match derives batches only from the completed recorder-owned v3 event
 * stream. Without a published model runtime, accepted work remains in its
 * authenticated durable spool across level reset and process restart. */
SG_HUMAN_TRACE_LEARNING_LOCAL void SG_HumanTraceLearningHostGamePostMatch(
	sg_human_trace_learning_host_report_t *report_out);
SG_HUMAN_TRACE_LEARNING_LOCAL void SG_HumanTraceLearningHostGameReset(void);

#endif /* SG_HUMAN_TRACE_LEARNING_HOST_GAME_H */
