/* Engine-owned post-match consumption of authenticated human-trace spools. */
#ifndef SG_HUMAN_TRACE_LEARNING_HOST_GAME_H
#define SG_HUMAN_TRACE_LEARNING_HOST_GAME_H

#include "sg_human_trace_learning_contract.h"

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

/* Post-match derives batches only from the completed recorder-owned v3 event
 * stream. Until the downstream model-assembly owner supplies its private
 * runtime handoff, accepted work remains in its authenticated durable spool
 * across level reset and process restart. */
SG_HUMAN_TRACE_LEARNING_LOCAL void SG_HumanTraceLearningHostGamePostMatch(
	sg_human_trace_learning_host_report_t *report_out);
SG_HUMAN_TRACE_LEARNING_LOCAL void SG_HumanTraceLearningHostGameReset(void);

#endif /* SG_HUMAN_TRACE_LEARNING_HOST_GAME_H */
