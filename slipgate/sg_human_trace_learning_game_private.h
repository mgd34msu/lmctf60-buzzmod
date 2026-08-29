/* Host-local runtime state. Model assembly has no public application bridge. */
#ifndef SG_HUMAN_TRACE_LEARNING_GAME_PRIVATE_H
#define SG_HUMAN_TRACE_LEARNING_GAME_PRIVATE_H

#include "sg_human_trace_learning.h"

#if defined(__GNUC__) || defined(__clang__)
#define SG_HUMAN_TRACE_LEARNING_INTERNAL __attribute__((visibility("hidden")))
#else
#define SG_HUMAN_TRACE_LEARNING_INTERNAL
#endif

typedef struct sg_human_trace_learning_playthrough_s
{
	sg_human_trace_learning_trace_id_t terminal_sha256;
	uint32_t client_id;
	uint64_t spawn_generation;
	uint32_t last_frame;
	uint64_t last_order;
	uint8_t used;
} sg_human_trace_learning_playthrough_t;

typedef struct sg_human_trace_learning_runtime_s
{
	sg_human_trace_learning_parameters_t *parameters;
	sg_human_trace_learning_workspace_t workspace;
	sg_human_trace_learning_playthrough_t *playthroughs;
	uint32_t playthrough_capacity;
	uint64_t next_transaction_id;
} sg_human_trace_learning_runtime_t;

typedef enum sg_human_trace_learning_apply_status_e
{
	SG_HUMAN_TRACE_LEARNING_APPLY_REJECTED = 0,
	SG_HUMAN_TRACE_LEARNING_APPLY_COMMITTED
} sg_human_trace_learning_apply_status_t;

#endif /* SG_HUMAN_TRACE_LEARNING_GAME_PRIVATE_H */
