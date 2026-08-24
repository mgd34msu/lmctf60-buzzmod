/* Exact-owner adapter for source-bound human route nominations. */
#ifndef SG_RUNE_LEARNING_GAME_H
#define SG_RUNE_LEARNING_GAME_H

#include "sg_rune_learning.h"

typedef enum sg_rune_learning_owner_result_e
{
	SG_RUNE_LEARNING_OWNER_REJECTED,
	SG_RUNE_LEARNING_OWNER_ADDED,
	SG_RUNE_LEARNING_OWNER_FATAL
} sg_rune_learning_owner_result_t;

typedef enum sg_rune_learning_apply_status_e
{
	SG_RUNE_LEARNING_CLOSED,
	SG_RUNE_LEARNING_OPEN_IMPROVED,
	SG_RUNE_LEARNING_OPEN_UNCHANGED,
	SG_RUNE_LEARNING_FATAL
} sg_rune_learning_apply_status_t;

typedef struct sg_rune_learning_graph_s
{
	const rune_seed_t *seeds;
	uint32_t seed_count;
	rune_link_t *links;
	int *link_count;
	uint32_t link_capacity;
	int objective[2];
} sg_rune_learning_graph_t;

typedef enum sg_rune_learning_hook_request_kind_e
{
	SG_RUNE_LEARNING_HOOK_DISCOVER_WORLD_BITES,
	SG_RUNE_LEARNING_HOOK_EXACT_SOURCE_CONTROL
} sg_rune_learning_hook_request_kind_t;

typedef struct sg_rune_learning_hook_request_s
{
	sg_rune_learning_hook_request_kind_t kind;
	uint8_t rope_count;
	int16_t aim_short[2][2];
	int32_t bite_q8[2][3];
	vec3_t control[2];
} sg_rune_learning_hook_request_t;

typedef sg_rune_learning_owner_result_t (*sg_rune_learning_run_owner_fn)(
	void *context, int from, int to, const int32_t waypoint_q8[3],
	int has_waypoint);

typedef sg_rune_learning_owner_result_t (*sg_rune_learning_hook_owner_fn)(
	void *context, int from, int to,
	const sg_rune_learning_hook_request_t *request);

typedef struct sg_rune_learning_owners_s
{
	sg_rune_learning_run_owner_fn run;
	sg_rune_learning_hook_owner_fn hook;
	void *context;
} sg_rune_learning_owners_t;

typedef struct sg_rune_learning_apply_report_s
{
	uint32_t candidate_count;
	uint32_t mapped_count;
	uint32_t skipped_count;
	uint32_t proof_calls;
	uint32_t accepted_count;
	uint32_t closure_checks;
} sg_rune_learning_apply_report_t;

typedef struct sg_rune_learning_update_report_s
{
	sg_rune_learning_apply_report_t source_runs;
	sg_rune_learning_apply_report_t source_hooks;
	sg_rune_learning_apply_report_t new_runs;
	sg_rune_learning_apply_report_t new_hooks;
} sg_rune_learning_update_report_t;

sg_rune_learning_apply_status_t SG_RuneLearningGameUpdate(
	const rune_t *source, const sg_rune_learning_evidence_t *evidence,
	const sg_rune_learning_graph_t *graph,
	const sg_rune_learning_owners_t *owners,
	sg_rune_learning_update_report_t *report);

#endif /* SG_RUNE_LEARNING_GAME_H */
