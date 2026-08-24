/*
 * sg_rune_late_path.c -- pure late-stage RUNE component bridge selection.
 */

#include "../q_shared.h"
#include "sg_rune_late_path.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define LATE_INF (UINT64_MAX / UINT64_C(4))
#define LATE_NO_LINK UINT32_MAX

const char *SG_RuneLateCompletionName(sg_rune_late_completion_t completion)
{
	switch (completion)
	{
	case SG_RUNE_LATE_COMPLETION_CLOSED: return "closed";
	case SG_RUNE_LATE_COMPLETION_OPEN_EXHAUSTED: return "open-exhausted";
	case SG_RUNE_LATE_COMPLETION_OPEN_BUDGET: return "open-budget";
	default: return "fatal";
	}
}

qboolean SG_RuneLateCompletionKeepsMerges(
	sg_rune_late_completion_t completion)
{
	return completion == SG_RUNE_LATE_COMPLETION_CLOSED ||
		completion == SG_RUNE_LATE_COMPLETION_OPEN_EXHAUSTED ||
		completion == SG_RUNE_LATE_COMPLETION_OPEN_BUDGET;
}

static uint32_t LateRejectionHash(int from, int to)
{
	return (uint32_t)from * UINT32_C(2654435761) ^ (uint32_t)to;
}

qboolean SG_RuneLateRejectionsInit(sg_rune_late_rejections_t *rejections,
	int *from, int *to, uint32_t table_size, uint32_t limit)
{
	if (!rejections || !from || !to || table_size < 2U ||
	    (table_size & (table_size - 1U)) != 0U || !limit ||
	    limit > table_size)
		return false;
	memset(rejections, 0, sizeof(*rejections));
	rejections->from = from;
	rejections->to = to;
	rejections->table_size = table_size;
	rejections->limit = limit;
	memset(from, 0xff, (size_t)table_size * sizeof(*from));
	return true;
}

qboolean SG_RuneLateRejectionsContains(
	const sg_rune_late_rejections_t *rejections, int from, int to)
{
	uint32_t mask;
	uint32_t slot;
	uint32_t probe;

	if (!rejections || !rejections->from || !rejections->to ||
	    !rejections->table_size)
		return false;
	mask = rejections->table_size - 1U;
	slot = LateRejectionHash(from, to) & mask;
	for (probe = 0U; probe <= mask; probe++)
	{
		if (rejections->from[slot] < 0)
			return false;
		if (rejections->from[slot] == from && rejections->to[slot] == to)
			return true;
		slot = (slot + 1U) & mask;
	}
	return false;
}

qboolean SG_RuneLateRejectionsRecord(sg_rune_late_rejections_t *rejections,
	int from, int to)
{
	uint32_t mask;
	uint32_t slot;
	uint32_t probe;

	if (!rejections || !rejections->from || !rejections->to ||
	    rejections->count >= rejections->limit)
		return false;
	mask = rejections->table_size - 1U;
	slot = LateRejectionHash(from, to) & mask;
	for (probe = 0U; probe <= mask; probe++)
	{
		if (rejections->from[slot] < 0)
		{
			rejections->from[slot] = from;
			rejections->to[slot] = to;
			rejections->count++;
			return true;
		}
		if (rejections->from[slot] == from && rejections->to[slot] == to)
			return true;
		slot = (slot + 1U) & mask;
	}
	return false;
}

typedef struct late_adjacency_s
{
	int *out_head;
	int *in_head;
	int *out_next;
	int *in_next;
	uint32_t *region_pair;
	uint32_t region_pair_count;
} late_adjacency_t;

typedef struct late_heap_s
{
	int *node;
	int *position;
	uint32_t count;
	uint32_t capacity;
} late_heap_t;

static int LateUint32Compare(const void *left, const void *right)
{
	uint32_t a = *(const uint32_t *)left;
	uint32_t b = *(const uint32_t *)right;

	return a < b ? -1 : a > b;
}

static qboolean LateOneWay(int action)
{
	return action == RL_TELEPORT || action == RL_DROP;
}

static uint64_t LateAdd(uint64_t left, uint64_t right)
{
	if (left >= LATE_INF || right >= LATE_INF || left > LATE_INF - right)
		return LATE_INF;
	return left + right;
}

static qboolean LateGraphValid(const sg_rune_late_graph_t *graph)
{
	uint64_t pair_count;
	uint32_t i;

	if (!graph || !graph->seed_count || graph->seed_count > RUNE_MAX_SEEDS ||
		graph->link_count > RUNE_MAX_LINKS || !graph->seeds ||
		!graph->regions || !graph->region_count ||
		(graph->link_count && !graph->links))
		return false;
	pair_count = (uint64_t)graph->region_count *
		(graph->region_count - 1U);
	if ((!pair_count && graph->pair_cursor) ||
		(pair_count && graph->pair_cursor >= pair_count))
		return false;
	for (i = 0; i < graph->seed_count; i++)
	{
		if (graph->regions[i] < 0 ||
			(uint32_t)graph->regions[i] >= graph->region_count)
			return false;
	}
	for (i = 0; i < graph->link_count; i++)
	{
		const rune_link_t *link = &graph->links[i];

		if (link->from < 0 || link->to < 0 ||
			(uint32_t)link->from >= graph->seed_count ||
			(uint32_t)link->to >= graph->seed_count ||
			link->cost_ms <= 0 || link->cost_ms > RUNE_MAX_COST_MS ||
			!SG_ActionMechanismAdmitted(link->action))
			return false;
	}
	for (i = 0; i < SG_RUNE_LATE_OBJECTIVE_COUNT; i++)
	{
		if (graph->objective[i] < -1 ||
			(graph->objective[i] >= 0 &&
			 (uint32_t)graph->objective[i] >= graph->seed_count))
			return false;
	}
	return true;
}

static void LateAdjacencyFree(late_adjacency_t *adjacency)
{
	free(adjacency->out_head);
	free(adjacency->in_head);
	free(adjacency->out_next);
	free(adjacency->in_next);
	free(adjacency->region_pair);
	memset(adjacency, 0, sizeof(*adjacency));
}

static qboolean LateAdjacencyBuild(const sg_rune_late_graph_t *graph,
	late_adjacency_t *adjacency)
{
	uint32_t i;

	memset(adjacency, 0, sizeof(*adjacency));
	adjacency->out_head = malloc(graph->seed_count * sizeof(*adjacency->out_head));
	adjacency->in_head = malloc(graph->seed_count * sizeof(*adjacency->in_head));
	if (graph->link_count)
	{
		adjacency->out_next =
			malloc(graph->link_count * sizeof(*adjacency->out_next));
		adjacency->in_next =
			malloc(graph->link_count * sizeof(*adjacency->in_next));
		adjacency->region_pair =
			malloc(graph->link_count * sizeof(*adjacency->region_pair));
	}
	if (!adjacency->out_head || !adjacency->in_head ||
		(graph->link_count && (!adjacency->out_next || !adjacency->in_next ||
		 !adjacency->region_pair)))
	{
		LateAdjacencyFree(adjacency);
		return false;
	}
	for (i = 0; i < graph->seed_count; i++)
	{
		adjacency->out_head[i] = -1;
		adjacency->in_head[i] = -1;
	}
	for (i = 0; i < graph->link_count; i++)
	{
		const rune_link_t *link = &graph->links[i];
		uint32_t from_region = (uint32_t)graph->regions[link->from];
		uint32_t to_region = (uint32_t)graph->regions[link->to];

		adjacency->out_next[i] = adjacency->out_head[link->from];
		adjacency->out_head[link->from] = (int)i;
		adjacency->in_next[i] = adjacency->in_head[link->to];
		adjacency->in_head[link->to] = (int)i;
		if (from_region != to_region)
			adjacency->region_pair[adjacency->region_pair_count++] =
				from_region * (graph->region_count - 1U) +
				(to_region < from_region ? to_region : to_region - 1U);
	}
	qsort(adjacency->region_pair, adjacency->region_pair_count,
		sizeof(*adjacency->region_pair), LateUint32Compare);
	{
		uint32_t unique = 0;

		for (i = 0; i < adjacency->region_pair_count; i++)
			if (!unique || adjacency->region_pair[i] !=
			    adjacency->region_pair[unique - 1U])
				adjacency->region_pair[unique++] = adjacency->region_pair[i];
		adjacency->region_pair_count = unique;
	}
	return true;
}

static qboolean LateHeapBefore(const uint64_t *distance, int left, int right)
{
	return distance[left] < distance[right] ||
		(distance[left] == distance[right] && left < right);
}

static void LateHeapSwap(late_heap_t *heap, uint32_t left, uint32_t right)
{
	int swap = heap->node[left];

	heap->node[left] = heap->node[right];
	heap->node[right] = swap;
	heap->position[heap->node[left]] = (int)left;
	heap->position[heap->node[right]] = (int)right;
}

static void LateHeapReset(late_heap_t *heap)
{
	for (uint32_t i = 0; i < heap->capacity; i++)
		heap->position[i] = -1;
	heap->count = 0;
}

static void LateHeapOffer(late_heap_t *heap, int node,
	const uint64_t *distance)
{
	int position = heap->position[node];

	if (position == -2)
		return;
	if (position < 0)
	{
		position = (int)heap->count++;
		heap->node[position] = node;
		heap->position[node] = position;
	}
	while (position > 0)
	{
		int parent = (position - 1) / 2;

		if (!LateHeapBefore(distance, node, heap->node[parent]))
			break;
		LateHeapSwap(heap, (uint32_t)position, (uint32_t)parent);
		position = parent;
	}
}

static int LateHeapPop(late_heap_t *heap, const uint64_t *distance)
{
	int result = heap->node[0];
	int tail = heap->node[--heap->count];
	uint32_t position = 0;

	heap->position[result] = -2;
	if (!heap->count)
		return result;
	heap->node[0] = tail;
	heap->position[tail] = 0;
	for (;;)
	{
		uint32_t left = position * 2U + 1U;
		uint32_t right = left + 1U;
		uint32_t child;

		if (left >= heap->count)
			break;
		child = right < heap->count &&
			LateHeapBefore(distance, heap->node[right], heap->node[left])
			? right : left;
		if (!LateHeapBefore(distance, heap->node[child], heap->node[position]))
			break;
		LateHeapSwap(heap, position, child);
		position = child;
	}
	return result;
}

static qboolean LateRelax(uint64_t *distance, int from, int to, uint32_t cost)
{
	uint64_t next = LateAdd(distance[from], cost);

	if (next >= distance[to])
		return false;
	distance[to] = next;
	return true;
}

static void LateDijkstra(const sg_rune_late_graph_t *graph,
	const late_adjacency_t *adjacency, int start, qboolean reverse,
	uint64_t *distance, late_heap_t *heap)
{
	uint32_t i;

	for (i = 0; i < graph->seed_count; i++)
		distance[i] = LATE_INF;
	LateHeapReset(heap);
	if (start < 0)
		return;
	distance[start] = 0;
	LateHeapOffer(heap, start, distance);
	while (heap->count)
	{
		int node = LateHeapPop(heap, distance);
		int link_index;

		link_index = reverse ? adjacency->in_head[node] :
			adjacency->out_head[node];
		while (link_index >= 0)
		{
			const rune_link_t *link = &graph->links[link_index];
			int next = reverse ? link->from : link->to;

			if (LateRelax(distance, node, next, (uint32_t)link->cost_ms))
				LateHeapOffer(heap, next, distance);
			link_index = reverse ? adjacency->in_next[link_index] :
				adjacency->out_next[link_index];
		}
	}
}

static qboolean LateWeakRelax(uint64_t *distance, uint32_t *first_cut,
	int from, int to, uint32_t cost, uint32_t link_index,
	qboolean reversed)
{
	uint64_t next = LateAdd(distance[from], cost);

	if (next >= distance[to])
		return false;
	distance[to] = next;
	first_cut[to] = first_cut[from];
	if (first_cut[to] == LATE_NO_LINK && reversed)
		first_cut[to] = link_index;
	return true;
}

static void LateWeakDijkstra(const sg_rune_late_graph_t *graph,
	const late_adjacency_t *adjacency, int start, uint64_t *distance,
	uint32_t *first_cut, late_heap_t *heap)
{
	uint32_t i;

	for (i = 0; i < graph->seed_count; i++)
	{
		distance[i] = LATE_INF;
		first_cut[i] = LATE_NO_LINK;
	}
	LateHeapReset(heap);
	distance[start] = 0;
	LateHeapOffer(heap, start, distance);
	while (heap->count)
	{
		int node = LateHeapPop(heap, distance);
		int link_index;

		for (link_index = adjacency->out_head[node]; link_index >= 0;
			 link_index = adjacency->out_next[link_index])
		{
			const rune_link_t *link = &graph->links[link_index];

			if (LateWeakRelax(distance, first_cut, node, link->to,
				(uint32_t)link->cost_ms, (uint32_t)link_index, false))
				LateHeapOffer(heap, link->to, distance);
		}
		for (link_index = adjacency->in_head[node]; link_index >= 0;
			 link_index = adjacency->in_next[link_index])
		{
			const rune_link_t *link = &graph->links[link_index];

			if (LateWeakRelax(distance, first_cut, node, link->from,
				(uint32_t)link->cost_ms, (uint32_t)link_index, true))
				LateHeapOffer(heap, link->from, distance);
		}
	}
}

static qboolean LateRegionLink(const late_adjacency_t *adjacency,
	uint32_t pair)
{
	uint32_t low = 0, high = adjacency->region_pair_count;

	while (low < high)
	{
		uint32_t middle = low + (high - low) / 2U;

		if (adjacency->region_pair[middle] == pair)
			return true;
		if (adjacency->region_pair[middle] < pair)
			low = middle + 1U;
		else
			high = middle;
	}
	return false;
}

static qboolean LateReversesOneWay(const sg_rune_late_graph_t *graph,
	const late_adjacency_t *adjacency, int from, int to)
{
	int link_index;

	for (link_index = adjacency->out_head[to]; link_index >= 0;
		 link_index = adjacency->out_next[link_index])
	{
		const rune_link_t *link = &graph->links[link_index];

		if (link->to == from && LateOneWay(link->action))
			return true;
	}
	return false;
}

static void LateCandidateScore(const sg_rune_late_graph_t *graph,
	const uint64_t *from_distance[SG_RUNE_LATE_OBJECTIVE_COUNT],
	const uint64_t *to_distance[SG_RUNE_LATE_OBJECTIVE_COUNT],
	const sg_rune_late_route_t route[SG_RUNE_LATE_OBJECTIVE_COUNT],
	sg_rune_late_candidate_t *candidate)
{
	uint64_t complete_total = LATE_INF;
	uint64_t attached_total = LATE_INF;
	int i;

	for (i = 0; i < SG_RUNE_LATE_OBJECTIVE_COUNT; i++)
	{
		int other = 1 - i;
		uint64_t prefix = from_distance[i][candidate->from];
		uint64_t suffix = to_distance[other][candidate->to];
		uint64_t complete = LateAdd(LateAdd(prefix, candidate->cost_ms), suffix);

		if (prefix < LATE_INF)
			candidate->objective_touch_mask |= (byte)(1U << i);
		if (to_distance[i][candidate->to] < LATE_INF)
			candidate->objective_touch_mask |= (byte)(1U << (i + 2));
		if (!route[i].directed && complete < LATE_INF)
			candidate->objective_gain_mask |= (byte)(1U << i);
		if (!route[i].directed && complete < complete_total)
			complete_total = complete;
		if (prefix < LATE_INF)
		{
			uint64_t attached = LateAdd(prefix, candidate->cost_ms);

			if (attached < attached_total)
				attached_total = attached;
		}
		if (to_distance[i][candidate->to] < LATE_INF)
		{
			uint64_t attached = LateAdd(candidate->cost_ms,
				to_distance[i][candidate->to]);

			if (attached < attached_total)
				attached_total = attached;
		}
	}
	if (complete_total < LATE_INF)
		candidate->total_path_cost_ms = complete_total;
	else if (attached_total < LATE_INF)
		candidate->total_path_cost_ms = attached_total;
	else
		candidate->total_path_cost_ms = candidate->cost_ms;
	(void)graph;
}

static int LateFrontierCompare(const sg_rune_late_candidate_t *left,
	const sg_rune_late_candidate_t *right)
{
	if (left->cost_ms != right->cost_ms)
		return left->cost_ms < right->cost_ms ? -1 : 1;
	if (left->from != right->from)
		return left->from < right->from ? -1 : 1;
	if (left->to != right->to)
		return left->to < right->to ? -1 : 1;
	if (left->action != right->action)
		return left->action < right->action ? -1 : 1;
	return 0;
}

static void LateRouteDiagnostics(const sg_rune_late_graph_t *graph,
	const late_adjacency_t *adjacency,
	uint64_t *weak_distance, uint32_t *first_cut, late_heap_t *heap,
	sg_rune_late_report_t *report)
{
	int i;

	for (i = 0; i < SG_RUNE_LATE_OBJECTIVE_COUNT; i++)
	{
		int start = graph->objective[i];
		int goal = graph->objective[1 - i];
		uint32_t cut;
		const rune_link_t *link;

		if (start < 0 || goal < 0 || report->route[i].directed)
			continue;
		LateWeakDijkstra(graph, adjacency, start, weak_distance,
			first_cut, heap);
		cut = first_cut[goal];
		if (weak_distance[goal] >= LATE_INF || cut == LATE_NO_LINK)
			continue;
		link = &graph->links[cut];
		report->route[i].weak_cut.available = true;
		report->route[i].weak_cut.reversible = !LateOneWay(link->action);
		report->route[i].weak_cut.link_index = cut;
		report->route[i].weak_cut.original_from = link->from;
		report->route[i].weak_cut.original_to = link->to;
		report->route[i].weak_cut.action = link->action;
		report->route[i].weak_cut.weak_path_cost_ms = weak_distance[goal];
	}
}

static void LateEnumerate(const sg_rune_late_graph_t *graph,
	const late_adjacency_t *adjacency,
	const uint64_t *from_distance[SG_RUNE_LATE_OBJECTIVE_COUNT],
	const uint64_t *to_distance[SG_RUNE_LATE_OBJECTIVE_COUNT],
	sg_rune_late_eligibility_fn eligibility, void *callback_data,
	sg_rune_late_candidate_t *candidates, uint32_t capacity,
	sg_rune_late_report_t *report)
{
	uint32_t pair_count = graph->region_count * (graph->region_count - 1U);
	uint32_t remaining = pair_count - graph->pair_cursor;
	uint32_t window = capacity < remaining ? capacity : remaining;
	uint32_t from;

	report->pair_count = pair_count;
	report->next_pair_cursor = pair_count
		? (graph->pair_cursor + window) % pair_count : 0U;
	for (from = 0; from < window; from++)
		candidates[from].from = -1;
	for (from = 0; from < graph->seed_count; from++)
	{
		uint32_t to;

		for (to = 0; to < graph->seed_count; to++)
		{
			sg_rune_late_proposal_t proposal;
			sg_rune_late_candidate_t candidate;
			uint32_t from_region = (uint32_t)graph->regions[from];
			uint32_t to_region = (uint32_t)graph->regions[to];
			uint32_t pair, slot;

			if (from_region == to_region)
				continue;
			pair = from_region * (graph->region_count - 1U) +
				(to_region < from_region ? to_region : to_region - 1U);
			if (LateRegionLink(adjacency, pair) ||
				LateReversesOneWay(graph, adjacency, (int)from, (int)to))
				continue;
			slot = (pair + pair_count - graph->pair_cursor) % pair_count;
			if (slot >= window)
				continue;
			memset(&proposal, 0, sizeof(proposal));
			if (!eligibility(callback_data, graph, (int)from, (int)to,
				&proposal))
				continue;
			if (!proposal.cost_ms || proposal.cost_ms > RUNE_MAX_COST_MS ||
				!SG_ActionMechanismAdmitted(proposal.action))
				continue;
			memset(&candidate, 0, sizeof(candidate));
			candidate.from = (int)from;
			candidate.to = (int)to;
			candidate.from_region = (int)from_region;
			candidate.to_region = (int)to_region;
			candidate.action = proposal.action;
			candidate.reversible = !LateOneWay(proposal.action);
			candidate.cost_ms = proposal.cost_ms;
			LateCandidateScore(graph, from_distance, to_distance,
				report->route, &candidate);
			if (candidates[slot].from < 0 ||
				LateFrontierCompare(&candidate, &candidates[slot]) < 0)
				candidates[slot] = candidate;
		}
	}
	for (from = 0; from < window; from++)
		if (candidates[from].from >= 0)
			candidates[report->candidate_count++] = candidates[from];
}

sg_rune_late_status_t SG_RuneLatePathSelect(
	const sg_rune_late_graph_t *graph,
	sg_rune_late_eligibility_fn eligibility,
	void *callback_data,
	sg_rune_late_candidate_t *candidates,
	uint32_t candidate_capacity,
	sg_rune_late_report_t *report)
{
	late_adjacency_t adjacency;
	late_heap_t heap;
	uint64_t *distances;
	uint32_t *first_cut;
	int *heap_data;
	const uint64_t *from_distance[SG_RUNE_LATE_OBJECTIVE_COUNT];
	const uint64_t *to_distance[SG_RUNE_LATE_OBJECTIVE_COUNT];
	uint32_t count;
	int i;

	if (!report || !LateGraphValid(graph) ||
		(candidate_capacity && (!candidates || !eligibility)))
		return SG_RUNE_LATE_INVALID_GRAPH;
	memset(report, 0, sizeof(*report));
	if (!LateAdjacencyBuild(graph, &adjacency))
		return SG_RUNE_LATE_NO_MEMORY;
	count = graph->seed_count;
	distances = malloc((size_t)count * 5U * sizeof(*distances));
	first_cut = malloc((size_t)count * sizeof(*first_cut));
	heap_data = malloc((size_t)count * 2U * sizeof(*heap_data));
	if (!distances || !first_cut || !heap_data)
	{
		free(distances);
		free(first_cut);
		free(heap_data);
		LateAdjacencyFree(&adjacency);
		return SG_RUNE_LATE_NO_MEMORY;
	}
	heap.node = heap_data;
	heap.position = heap_data + count;
	heap.count = 0;
	heap.capacity = count;
	for (i = 0; i < SG_RUNE_LATE_OBJECTIVE_COUNT; i++)
	{
		uint64_t *from = distances + (size_t)i * count;
		uint64_t *to = distances + (size_t)(i + 2) * count;
		int goal = graph->objective[1 - i];

		LateDijkstra(graph, &adjacency, graph->objective[i], false,
			from, &heap);
		LateDijkstra(graph, &adjacency, graph->objective[i], true,
			to, &heap);
		from_distance[i] = from;
		to_distance[i] = to;
		if (goal >= 0 && from[goal] < LATE_INF)
		{
			report->route[i].directed = true;
			report->route[i].directed_cost_ms = from[goal];
		}
	}
	LateRouteDiagnostics(graph, &adjacency, distances + (size_t)4 * count,
		first_cut, &heap, report);
	if (candidate_capacity)
		LateEnumerate(graph, &adjacency, from_distance, to_distance,
			eligibility, callback_data, candidates, candidate_capacity, report);
	free(distances);
	free(first_cut);
	free(heap_data);
	LateAdjacencyFree(&adjacency);
	return SG_RUNE_LATE_OK;
}
