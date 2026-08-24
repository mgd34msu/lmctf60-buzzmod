#include "../q_shared.h"
#include "sg_rune_learning_game.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct learning_coord_slot_s
{
	int32_t q8[3];
	int index;
	unsigned char used;
	unsigned char ambiguous;
} learning_coord_slot_t;

typedef struct learning_edge_slot_s
{
	int from;
	int to;
	unsigned char used;
} learning_edge_slot_t;

typedef struct learning_workspace_s
{
	const sg_rune_learning_graph_t *graph;
	learning_coord_slot_t *coordinates;
	learning_edge_slot_t *edges;
	size_t coordinate_size;
	size_t edge_size;
	int *first;
	int *next;
	int *queue;
	unsigned char *seen;
} learning_workspace_t;

static uint32_t LearningHashWord(uint32_t value)
{
	value ^= value >> 16;
	value *= UINT32_C(0x7feb352d);
	value ^= value >> 15;
	value *= UINT32_C(0x846ca68b);
	return value ^ (value >> 16);
}

static uint32_t LearningCoordHash(const int32_t q8[3])
{
	uint32_t hash = LearningHashWord((uint32_t)q8[0]);

	hash ^= LearningHashWord((uint32_t)q8[1] + UINT32_C(0x9e3779b9));
	hash ^= LearningHashWord((uint32_t)q8[2] + UINT32_C(0x85ebca6b));
	return LearningHashWord(hash);
}

static uint32_t LearningEdgeHash(int from, int to)
{
	return LearningHashWord((uint32_t)from) ^
	       LearningHashWord((uint32_t)to + UINT32_C(0x9e3779b9));
}

static size_t LearningTableSize(uint32_t count)
{
	size_t size = 2U;
	size_t need = (size_t)count * 2U;

	if (need < (size_t)count)
		return 0U;
	while (size < need)
	{
		if (size > SIZE_MAX / 2U)
			return 0U;
		size *= 2U;
	}
	return size;
}

static int LearningSeedQ8(const rune_seed_t *seed, int32_t q8[3])
{
	int axis;

	for (axis = 0; axis < 3; axis++)
	{
		double scaled = (double)seed->origin[axis] * 8.0;

		if (!isfinite(scaled) || scaled < (double)INT32_MIN ||
		    scaled > (double)INT32_MAX || scaled != trunc(scaled))
			return 0;
		q8[axis] = (int32_t)scaled;
	}
	return 1;
}

static int LearningVectorQ8(const vec3_t vector, int32_t q8[3])
{
	rune_seed_t seed;

	memset(&seed, 0, sizeof(seed));
	VectorCopy(vector, seed.origin);
	return LearningSeedQ8(&seed, q8);
}

static int LearningCoordEqual(const int32_t left[3], const int32_t right[3])
{
	return left[0] == right[0] && left[1] == right[1] &&
	       left[2] == right[2];
}

static int LearningCoordInsert(learning_coord_slot_t *table, size_t size,
	const int32_t q8[3], int index)
{
	size_t slot = (size_t)LearningCoordHash(q8) & (size - 1U);
	size_t checked;

	for (checked = 0U; checked < size; checked++)
	{
		learning_coord_slot_t *entry = &table[slot];

		if (!entry->used)
		{
			entry->used = 1U;
			memcpy(entry->q8, q8, sizeof(entry->q8));
			entry->index = index;
			return 1;
		}
		if (LearningCoordEqual(entry->q8, q8))
		{
			entry->ambiguous = 1U;
			return 1;
		}
		slot = (slot + 1U) & (size - 1U);
	}
	return 0;
}

static int LearningCoordFind(const learning_workspace_t *workspace,
	const int32_t q8[3])
{
	size_t slot = (size_t)LearningCoordHash(q8) &
		(workspace->coordinate_size - 1U);
	size_t checked;

	for (checked = 0U; checked < workspace->coordinate_size; checked++)
	{
		const learning_coord_slot_t *entry = &workspace->coordinates[slot];

		if (!entry->used)
			return -1;
		if (LearningCoordEqual(entry->q8, q8))
			return entry->ambiguous ? -1 : entry->index;
		slot = (slot + 1U) & (workspace->coordinate_size - 1U);
	}
	return -1;
}

static int LearningEdgeFind(const learning_workspace_t *workspace,
	int from, int to)
{
	size_t slot = (size_t)LearningEdgeHash(from, to) &
		(workspace->edge_size - 1U);
	size_t checked;

	for (checked = 0U; checked < workspace->edge_size; checked++)
	{
		const learning_edge_slot_t *entry = &workspace->edges[slot];

		if (!entry->used)
			return 0;
		if (entry->from == from && entry->to == to)
			return 1;
		slot = (slot + 1U) & (workspace->edge_size - 1U);
	}
	return -1;
}

static int LearningEdgeInsert(learning_workspace_t *workspace,
	int from, int to)
{
	size_t slot = (size_t)LearningEdgeHash(from, to) &
		(workspace->edge_size - 1U);
	size_t checked;

	for (checked = 0U; checked < workspace->edge_size; checked++)
	{
		learning_edge_slot_t *entry = &workspace->edges[slot];

		if (!entry->used)
		{
			entry->used = 1U;
			entry->from = from;
			entry->to = to;
			return 1;
		}
		if (entry->from == from && entry->to == to)
			return 0;
		slot = (slot + 1U) & (workspace->edge_size - 1U);
	}
	return -1;
}

static int LearningReachable(learning_workspace_t *workspace,
	int start, int goal)
{
	const sg_rune_learning_graph_t *graph = workspace->graph;
	int head = 0;
	int tail = 0;
	int link_index;
	uint32_t seed_index;

	for (seed_index = 0U; seed_index < graph->seed_count; seed_index++)
		workspace->first[seed_index] = -1;
	for (link_index = 0; link_index < *graph->link_count; link_index++)
	{
		const rune_link_t *link = &graph->links[link_index];

		workspace->next[link_index] = workspace->first[link->from];
		workspace->first[link->from] = link_index;
	}
	memset(workspace->seen, 0, graph->seed_count * sizeof(*workspace->seen));
	workspace->seen[start] = 1U;
	workspace->queue[tail++] = start;
	while (head < tail)
	{
		int at = workspace->queue[head++];

		if (at == goal)
			return 1;
		for (link_index = workspace->first[at]; link_index >= 0;
		     link_index = workspace->next[link_index])
		{
			int to = graph->links[link_index].to;

			if (!workspace->seen[to])
			{
				workspace->seen[to] = 1U;
				workspace->queue[tail++] = to;
			}
		}
	}
	return 0;
}

static int LearningClosed(learning_workspace_t *workspace)
{
	const sg_rune_learning_graph_t *graph = workspace->graph;

	return LearningReachable(workspace, graph->objective[0],
	           graph->objective[1]) &&
	       LearningReachable(workspace, graph->objective[1],
	           graph->objective[0]);
}

static int LearningGraphValid(const sg_rune_learning_graph_t *graph)
{
	int index;

	if (!graph || !graph->seeds || graph->seed_count == 0U ||
	    graph->seed_count > RUNE_MAX_SEEDS || !graph->links ||
	    !graph->link_count || *graph->link_count < 0 ||
	    (uint32_t)*graph->link_count > graph->link_capacity ||
	    graph->link_capacity == 0U || graph->link_capacity > RUNE_MAX_LINKS ||
	    graph->objective[0] < 0 || graph->objective[1] < 0 ||
	    graph->objective[0] == graph->objective[1] ||
	    (uint32_t)graph->objective[0] >= graph->seed_count ||
	    (uint32_t)graph->objective[1] >= graph->seed_count)
		return 0;
	for (index = 0; index < *graph->link_count; index++)
		if (graph->links[index].from < 0 || graph->links[index].to < 0 ||
		    (uint32_t)graph->links[index].from >= graph->seed_count ||
		    (uint32_t)graph->links[index].to >= graph->seed_count)
			return 0;
	return 1;
}

static int LearningWorkspaceInit(learning_workspace_t *workspace,
	const sg_rune_learning_graph_t *graph)
{
	uint32_t index;

	memset(workspace, 0, sizeof(*workspace));
	workspace->graph = graph;
	workspace->coordinate_size = LearningTableSize(graph->seed_count);
	workspace->edge_size = LearningTableSize(graph->link_capacity);
	if (workspace->coordinate_size == 0U || workspace->edge_size == 0U)
		return 0;
	workspace->coordinates = calloc(workspace->coordinate_size,
		sizeof(*workspace->coordinates));
	workspace->edges = calloc(workspace->edge_size,
		sizeof(*workspace->edges));
	workspace->first = malloc(graph->seed_count * sizeof(*workspace->first));
	workspace->next = malloc(graph->link_capacity * sizeof(*workspace->next));
	workspace->queue = malloc(graph->seed_count * sizeof(*workspace->queue));
	workspace->seen = malloc(graph->seed_count * sizeof(*workspace->seen));
	if (!workspace->coordinates || !workspace->edges || !workspace->first ||
	    !workspace->next || !workspace->queue || !workspace->seen)
		return 0;
	for (index = 0U; index < graph->seed_count; index++)
	{
		int32_t q8[3];

		if (!LearningSeedQ8(&graph->seeds[index], q8) ||
		    !LearningCoordInsert(workspace->coordinates,
		        workspace->coordinate_size, q8, (int)index))
			return 0;
	}
	for (index = 0U; index < (uint32_t)*graph->link_count; index++)
		if (LearningEdgeInsert(workspace, graph->links[index].from,
		        graph->links[index].to) < 0)
			return 0;
	return 1;
}

static void LearningWorkspaceFree(learning_workspace_t *workspace)
{
	free(workspace->seen);
	free(workspace->queue);
	free(workspace->next);
	free(workspace->first);
	free(workspace->edges);
	free(workspace->coordinates);
	memset(workspace, 0, sizeof(*workspace));
}

static int LearningRunAppendValid(const sg_rune_learning_graph_t *graph,
	int before, int from, int to)
{
	const rune_link_t *link;

	if (*graph->link_count != before + 1)
		return 0;
	link = &graph->links[before];
	return link->from == from && link->to == to && link->action == RL_RUN &&
	       link->provenance == RL_PROVEN && link->cost_ms > 0 &&
	       link->mechanism_plan == RUNE_NO_MECHANISM_PLAN;
}

static int LearningControlValid(const vec3_t control)
{
	return isfinite(control[PITCH]) && isfinite(control[YAW]) &&
	       isfinite(control[ROLL]) &&
	       SHORT2ANGLE((short)ANGLE2SHORT(control[PITCH])) == control[PITCH] &&
	       SHORT2ANGLE((short)ANGLE2SHORT(control[YAW])) == control[YAW] &&
	       control[ROLL] >= 1.0f && control[ROLL] <= RUNE_HOOK_MAX_RAY;
}

static int LearningHookAppendValid(const sg_rune_learning_graph_t *graph,
	int before, int from, int to, uint8_t rope_count)
{
	const rune_link_t *link;
	int axis;

	if (*graph->link_count != before + 1)
		return 0;
	link = &graph->links[before];
	if (link->from != from || link->to != to ||
	    link->action != (rope_count == 1U ? RL_HOOK : RL_CHAIN_HOOK) ||
	    link->provenance != RL_PROVEN || link->cost_ms <= 0 ||
	    link->mechanism_plan != RUNE_NO_MECHANISM_PLAN ||
	    !LearningControlValid(link->anchor))
		return 0;
	if (rope_count == 2U)
		return LearningControlValid(link->mechanism_anchor);
	for (axis = 0; axis < 3; axis++)
		if (link->mechanism_anchor[axis] != 0.0f)
			return 0;
	return 1;
}

static sg_rune_learning_apply_status_t LearningAccepted(
	learning_workspace_t *workspace, int from, int to,
	sg_rune_learning_apply_report_t *report)
{
	if (LearningEdgeInsert(workspace, from, to) != 1)
		return SG_RUNE_LEARNING_FATAL;
	report->accepted_count++;
	report->closure_checks++;
	return LearningClosed(workspace) ? SG_RUNE_LEARNING_CLOSED :
		SG_RUNE_LEARNING_OPEN_IMPROVED;
}

static int LearningMapEndpoints(learning_workspace_t *workspace,
	const int32_t from_q8[3], const int32_t to_q8[3], int *from, int *to)
{
	*from = LearningCoordFind(workspace, from_q8);
	*to = LearningCoordFind(workspace, to_q8);
	return *from >= 0 && *to >= 0 && *from != *to;
}

static sg_rune_learning_apply_status_t LearningApplyRun(
	learning_workspace_t *workspace, const sg_rune_learning_owners_t *owners,
	int from, int to, const int32_t waypoint_q8[3], int has_waypoint,
	sg_rune_learning_apply_report_t *report)
{
	const sg_rune_learning_graph_t *graph = workspace->graph;
	int edge_state = LearningEdgeFind(workspace, from, to);
	int before;
	sg_rune_learning_owner_result_t result;

	if (edge_state < 0)
		return SG_RUNE_LEARNING_FATAL;
	if (edge_state != 0 || (graph->seeds[from].flags & RSF_WATER) != 0 ||
	    (graph->seeds[to].flags & RSF_WATER) != 0)
	{
		report->skipped_count++;
		return SG_RUNE_LEARNING_OPEN_UNCHANGED;
	}
	if ((uint32_t)*graph->link_count >= graph->link_capacity)
		return SG_RUNE_LEARNING_FATAL;
	report->mapped_count++;
	report->proof_calls++;
	before = *graph->link_count;
	result = owners->run(owners->context, from, to, waypoint_q8,
		has_waypoint);
	if (result == SG_RUNE_LEARNING_OWNER_FATAL ||
	    (result == SG_RUNE_LEARNING_OWNER_REJECTED &&
	     *graph->link_count != before) ||
	    (result == SG_RUNE_LEARNING_OWNER_ADDED &&
	     !LearningRunAppendValid(graph, before, from, to)))
		return SG_RUNE_LEARNING_FATAL;
	if (result == SG_RUNE_LEARNING_OWNER_REJECTED)
		return SG_RUNE_LEARNING_OPEN_UNCHANGED;
	return LearningAccepted(workspace, from, to, report);
}

static sg_rune_learning_apply_status_t LearningApplyHook(
	learning_workspace_t *workspace, const sg_rune_learning_owners_t *owners,
	int from, int to, const sg_rune_learning_hook_request_t *request,
	sg_rune_learning_apply_report_t *report)
{
	const sg_rune_learning_graph_t *graph = workspace->graph;
	int edge_state = LearningEdgeFind(workspace, from, to);
	int before;
	sg_rune_learning_owner_result_t result;

	if (edge_state < 0 || (request->rope_count != 1U &&
	    request->rope_count != 2U))
		return SG_RUNE_LEARNING_FATAL;
	if (edge_state != 0)
	{
		report->skipped_count++;
		return SG_RUNE_LEARNING_OPEN_UNCHANGED;
	}
	if ((uint32_t)*graph->link_count >= graph->link_capacity)
		return SG_RUNE_LEARNING_FATAL;
	report->mapped_count++;
	report->proof_calls++;
	before = *graph->link_count;
	result = owners->hook(owners->context, from, to, request);
	if (result == SG_RUNE_LEARNING_OWNER_FATAL ||
	    (result == SG_RUNE_LEARNING_OWNER_REJECTED &&
	     *graph->link_count != before) ||
	    (result == SG_RUNE_LEARNING_OWNER_ADDED &&
	     !LearningHookAppendValid(graph, before, from, to,
	         request->rope_count)))
		return SG_RUNE_LEARNING_FATAL;
	if (result == SG_RUNE_LEARNING_OWNER_REJECTED)
		return SG_RUNE_LEARNING_OPEN_UNCHANGED;
	return LearningAccepted(workspace, from, to, report);
}

static int LearningSourceValid(const rune_t *source)
{
	return source && source->seeds &&
	       (source->artifact.num_links == 0U || source->links) &&
	       source->artifact.route_contract == RUNE_ROUTE_CONTRACT_LOCAL_ONLY &&
	       source->artifact.num_seeds > 0U &&
	       source->artifact.num_seeds <= RUNE_MAX_SEEDS &&
	       source->artifact.num_links <= RUNE_MAX_LINKS &&
	       source->hdr.num_seeds == (int)source->artifact.num_seeds &&
	       source->hdr.num_links == (int)source->artifact.num_links;
}

static sg_rune_learning_apply_status_t LearningSourceRuns(
	const rune_t *source, learning_workspace_t *workspace,
	const sg_rune_learning_owners_t *owners,
	sg_rune_learning_apply_report_t *report)
{
	uint32_t index;

	for (index = 0U; index < source->artifact.num_links; index++)
	{
		const rune_link_t *link = &source->links[index];
		int32_t from_q8[3], to_q8[3], waypoint_q8[3];
		int from, to;
		sg_rune_learning_apply_status_t status;

		if (link->action != RL_RUN)
			continue;
		report->candidate_count++;
		if (link->from < 0 || link->to < 0 ||
		    (uint32_t)link->from >= source->artifact.num_seeds ||
		    (uint32_t)link->to >= source->artifact.num_seeds ||
		    !LearningSeedQ8(&source->seeds[link->from], from_q8) ||
		    !LearningSeedQ8(&source->seeds[link->to], to_q8) ||
		    !LearningVectorQ8(link->anchor, waypoint_q8))
			return SG_RUNE_LEARNING_FATAL;
		if (!LearningMapEndpoints(workspace, from_q8, to_q8, &from, &to))
		{
			report->skipped_count++;
			continue;
		}
		status = LearningApplyRun(workspace, owners, from, to, waypoint_q8,
			waypoint_q8[0] != 0 || waypoint_q8[1] != 0 ||
			waypoint_q8[2] != 0, report);
		if (status == SG_RUNE_LEARNING_FATAL ||
		    status == SG_RUNE_LEARNING_CLOSED)
			return status;
	}
	return report->accepted_count != 0U ? SG_RUNE_LEARNING_OPEN_IMPROVED :
		SG_RUNE_LEARNING_OPEN_UNCHANGED;
}

static sg_rune_learning_apply_status_t LearningSourceHooks(
	const rune_t *source, learning_workspace_t *workspace,
	const sg_rune_learning_owners_t *owners,
	sg_rune_learning_apply_report_t *report)
{
	uint32_t index;

	for (index = 0U; index < source->artifact.num_links; index++)
	{
		const rune_link_t *link = &source->links[index];
		sg_rune_learning_hook_request_t request;
		int32_t from_q8[3], to_q8[3];
		int from, to;
		sg_rune_learning_apply_status_t status;

		if (link->action != RL_HOOK && link->action != RL_CHAIN_HOOK)
			continue;
		report->candidate_count++;
		if (link->from < 0 || link->to < 0 ||
		    (uint32_t)link->from >= source->artifact.num_seeds ||
		    (uint32_t)link->to >= source->artifact.num_seeds ||
		    !LearningSeedQ8(&source->seeds[link->from], from_q8) ||
		    !LearningSeedQ8(&source->seeds[link->to], to_q8))
			return SG_RUNE_LEARNING_FATAL;
		if (!LearningMapEndpoints(workspace, from_q8, to_q8, &from, &to))
		{
			report->skipped_count++;
			continue;
		}
		memset(&request, 0, sizeof(request));
		request.kind = SG_RUNE_LEARNING_HOOK_EXACT_SOURCE_CONTROL;
		request.rope_count = link->action == RL_HOOK ? 1U : 2U;
		VectorCopy(link->anchor, request.control[0]);
		if (request.rope_count == 2U)
			VectorCopy(link->mechanism_anchor, request.control[1]);
		status = LearningApplyHook(workspace, owners, from, to, &request,
			report);
		if (status == SG_RUNE_LEARNING_FATAL ||
		    status == SG_RUNE_LEARNING_CLOSED)
			return status;
	}
	return report->accepted_count != 0U ? SG_RUNE_LEARNING_OPEN_IMPROVED :
		SG_RUNE_LEARNING_OPEN_UNCHANGED;
}

static sg_rune_learning_apply_status_t LearningNewRuns(
	const sg_rune_learning_evidence_t *evidence,
	learning_workspace_t *workspace,
	const sg_rune_learning_owners_t *owners,
	sg_rune_learning_apply_report_t *report)
{
	uint32_t index;

	report->candidate_count = evidence->candidate_count;
	for (index = 0U; index < evidence->candidate_count; index++)
	{
		const sg_rune_learning_candidate_t *candidate =
			&evidence->candidates[index];
		int from, to;
		sg_rune_learning_apply_status_t status;

		if (!LearningMapEndpoints(workspace, candidate->from_origin_q8,
		        candidate->to_origin_q8, &from, &to))
		{
			report->skipped_count++;
			continue;
		}
		status = LearningApplyRun(workspace, owners, from, to,
			candidate->waypoint_q8, candidate->has_waypoint != 0U, report);
		if (status == SG_RUNE_LEARNING_FATAL ||
		    status == SG_RUNE_LEARNING_CLOSED)
			return status;
	}
	return report->accepted_count != 0U ? SG_RUNE_LEARNING_OPEN_IMPROVED :
		SG_RUNE_LEARNING_OPEN_UNCHANGED;
}

static sg_rune_learning_apply_status_t LearningNewHooks(
	const sg_rune_learning_evidence_t *evidence,
	learning_workspace_t *workspace,
	const sg_rune_learning_owners_t *owners,
	sg_rune_learning_apply_report_t *report)
{
	uint32_t index;

	report->candidate_count = evidence->hook_candidate_count;
	for (index = 0U; index < evidence->hook_candidate_count; index++)
	{
		const sg_rune_learning_hook_candidate_t *candidate =
			&evidence->hook_candidates[index];
		sg_rune_learning_hook_request_t request;
		int from, to;
		sg_rune_learning_apply_status_t status;

		if (!LearningMapEndpoints(workspace, candidate->from_origin_q8,
		        candidate->to_origin_q8, &from, &to))
		{
			report->skipped_count++;
			continue;
		}
		memset(&request, 0, sizeof(request));
		request.kind = SG_RUNE_LEARNING_HOOK_DISCOVER_WORLD_BITES;
		request.rope_count = candidate->rope_count;
		memcpy(request.aim_short, candidate->aim_short,
			sizeof(request.aim_short));
		memcpy(request.bite_q8, candidate->bite_q8,
			sizeof(request.bite_q8));
		status = LearningApplyHook(workspace, owners, from, to, &request,
			report);
		if (status == SG_RUNE_LEARNING_FATAL ||
		    status == SG_RUNE_LEARNING_CLOSED)
			return status;
	}
	return report->accepted_count != 0U ? SG_RUNE_LEARNING_OPEN_IMPROVED :
		SG_RUNE_LEARNING_OPEN_UNCHANGED;
}

sg_rune_learning_apply_status_t SG_RuneLearningGameUpdate(
	const rune_t *source, const sg_rune_learning_evidence_t *evidence,
	const sg_rune_learning_graph_t *graph,
	const sg_rune_learning_owners_t *owners,
	sg_rune_learning_update_report_t *report)
{
	learning_workspace_t workspace;
	sg_rune_learning_apply_status_t status;
	int improved = 0;

	if (report)
		memset(report, 0, sizeof(*report));
	if (!report || !LearningSourceValid(source) || !evidence || !owners ||
	    !owners->run || !owners->hook || !LearningGraphValid(graph) ||
	    evidence->candidate_count > SG_RUNE_LEARNING_MAX_CANDIDATES ||
	    evidence->hook_candidate_count >
	        SG_RUNE_LEARNING_MAX_HOOK_CANDIDATES ||
	    (evidence->candidate_count != 0U && !evidence->candidates) ||
	    (evidence->hook_candidate_count != 0U &&
	     !evidence->hook_candidates))
		return SG_RUNE_LEARNING_FATAL;
	if (!LearningWorkspaceInit(&workspace, graph))
	{
		LearningWorkspaceFree(&workspace);
		return SG_RUNE_LEARNING_FATAL;
	}
	if (LearningClosed(&workspace))
	{
		LearningWorkspaceFree(&workspace);
		return SG_RUNE_LEARNING_CLOSED;
	}
	status = LearningSourceRuns(source, &workspace, owners,
		&report->source_runs);
	if (status == SG_RUNE_LEARNING_FATAL || status == SG_RUNE_LEARNING_CLOSED)
		goto done;
	improved |= status == SG_RUNE_LEARNING_OPEN_IMPROVED;
	status = LearningSourceHooks(source, &workspace, owners,
		&report->source_hooks);
	if (status == SG_RUNE_LEARNING_FATAL || status == SG_RUNE_LEARNING_CLOSED)
		goto done;
	improved |= status == SG_RUNE_LEARNING_OPEN_IMPROVED;
	status = LearningNewRuns(evidence, &workspace, owners, &report->new_runs);
	if (status == SG_RUNE_LEARNING_FATAL || status == SG_RUNE_LEARNING_CLOSED)
		goto done;
	improved |= status == SG_RUNE_LEARNING_OPEN_IMPROVED;
	status = LearningNewHooks(evidence, &workspace, owners,
		&report->new_hooks);
	if (status == SG_RUNE_LEARNING_FATAL || status == SG_RUNE_LEARNING_CLOSED)
		goto done;
	improved |= status == SG_RUNE_LEARNING_OPEN_IMPROVED;
	status = improved ? SG_RUNE_LEARNING_OPEN_IMPROVED :
		SG_RUNE_LEARNING_OPEN_UNCHANGED;

done:
	LearningWorkspaceFree(&workspace);
	return status;
}
