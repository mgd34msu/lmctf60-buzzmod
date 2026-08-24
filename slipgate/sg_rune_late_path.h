/*
 * sg_rune_late_path.h -- pure late-stage RUNE component bridge selection.
 */

#pragma once

#include "sg_rune.h"

#define SG_RUNE_LATE_OBJECTIVE_COUNT 2
#define SG_RUNE_LATE_REJECTION_LIMIT 1024U
#define SG_RUNE_LATE_REJECTION_TABLE_SIZE \
	(SG_RUNE_LATE_REJECTION_LIMIT * 2U)

typedef enum sg_rune_late_status_e
{
	SG_RUNE_LATE_OK,
	SG_RUNE_LATE_INVALID_GRAPH,
	SG_RUNE_LATE_NO_MEMORY
} sg_rune_late_status_t;

typedef enum sg_rune_late_completion_e
{
	SG_RUNE_LATE_COMPLETION_CLOSED,
	SG_RUNE_LATE_COMPLETION_OPEN_EXHAUSTED,
	SG_RUNE_LATE_COMPLETION_OPEN_BUDGET,
	SG_RUNE_LATE_COMPLETION_FATAL
} sg_rune_late_completion_t;

const char *SG_RuneLateCompletionName(sg_rune_late_completion_t completion);
qboolean SG_RuneLateCompletionKeepsMerges(
	sg_rune_late_completion_t completion);

typedef struct sg_rune_late_rejections_s
{
	int *from;
	int *to;
	uint32_t table_size;
	uint32_t limit;
	uint32_t count;
} sg_rune_late_rejections_t;

qboolean SG_RuneLateRejectionsInit(sg_rune_late_rejections_t *rejections,
	int *from, int *to, uint32_t table_size, uint32_t limit);
qboolean SG_RuneLateRejectionsContains(
	const sg_rune_late_rejections_t *rejections, int from, int to);
qboolean SG_RuneLateRejectionsRecord(sg_rune_late_rejections_t *rejections,
	int from, int to);

typedef struct sg_rune_late_graph_s
{
	const rune_seed_t *seeds;
	uint32_t seed_count;
	const rune_link_t *links;
	uint32_t link_count;
	/* Recomputed by the caller after every accepted bridge. */
	const int *regions;
	uint32_t region_count;
	/* Cyclic ordered-region-pair window; reset after every accepted merge. */
	uint32_t pair_cursor;
	/* Either objective may be -1 when route diagnostics are unavailable. */
	int objective[SG_RUNE_LATE_OBJECTIVE_COUNT];
} sg_rune_late_graph_t;

typedef struct sg_rune_late_proposal_s
{
	byte action;
	uint32_t cost_ms;
} sg_rune_late_proposal_t;

/*
 * This callback only qualifies a possible ordered bridge. It must not publish
 * or mutate the graph. Persistent rejection state may live in callback_data,
 * allowing later calls to skip candidates which exact proof already rejected.
 */
typedef qboolean (*sg_rune_late_eligibility_fn)(
	void *callback_data,
	const sg_rune_late_graph_t *graph,
	int from,
	int to,
	sg_rune_late_proposal_t *proposal);

typedef struct sg_rune_late_candidate_s
{
	int from;
	int to;
	int from_region;
	int to_region;
	byte action;
	byte reversible;
	/* Bit i: adding this bridge closes objective i -> objective 1-i. */
	byte objective_gain_mask;
	/* Reachability attachment bits: A-from, B-from, to-A, to-B. */
	byte objective_touch_mask;
	uint32_t cost_ms;
	uint64_t total_path_cost_ms;
} sg_rune_late_candidate_t;

typedef struct sg_rune_late_weak_cut_s
{
	qboolean available;
	qboolean reversible;
	uint32_t link_index;
	int original_from;
	int original_to;
	byte action;
	uint64_t weak_path_cost_ms;
} sg_rune_late_weak_cut_t;

typedef struct sg_rune_late_route_s
{
	qboolean directed;
	uint64_t directed_cost_ms;
	sg_rune_late_weak_cut_t weak_cut;
} sg_rune_late_route_t;

typedef struct sg_rune_late_report_s
{
	sg_rune_late_route_t route[SG_RUNE_LATE_OBJECTIVE_COUNT];
	uint64_t endpoint_pair_count;
	uint32_t candidate_count;
	uint32_t pair_count;
	uint32_t next_pair_cursor;
} sg_rune_late_report_t;

/*
 * Returns the cheapest eligible bridge for each ordered region pair in one
 * cyclic window. Objectives are diagnostics only and never affect scheduling.
 * The graph, links, seeds, and region labels are never modified.
 */
sg_rune_late_status_t SG_RuneLatePathSelect(
	const sg_rune_late_graph_t *graph,
	sg_rune_late_eligibility_fn eligibility,
	void *callback_data,
	sg_rune_late_candidate_t *candidates,
	uint32_t candidate_capacity,
	sg_rune_late_report_t *report);
