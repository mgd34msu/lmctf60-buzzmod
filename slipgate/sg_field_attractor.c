#include "sg_field_attractor.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

typedef struct sg_field_attractor_work_s
{
	size_t rule_count;
	size_t destination_count;
	uint32_t *remaining;
	uint32_t *maximum_rank;
	size_t *reverse_offsets;
	uint32_t *reverse_rules;
	uint32_t *queue;
} sg_field_attractor_work_t;

static int SpanWithin(sg_field_attractor_span_t span, size_t count)
{
	return (size_t)span.first <= count &&
		(size_t)span.count <= count - span.first;
}

static int AddSize(size_t left, size_t right, size_t *result)
{
	if (left > SIZE_MAX - right)
		return 0;
	*result = left + right;
	return 1;
}

static void *AllocateArray(size_t count, size_t element_size)
{
	if (count == 0U || element_size == 0U || count > SIZE_MAX / element_size)
		return NULL;
	return calloc(count, element_size);
}

static sg_field_attractor_span_t RuleSpan(
	const sg_field_attractor_graph_t *graph, size_t rule)
{
	return rule < graph->choice_count ? graph->choices[rule].destinations :
		graph->progress[rule - graph->choice_count].destinations;
}

static uint32_t RuleSource(const sg_field_attractor_graph_t *graph,
	size_t rule)
{
	return rule < graph->choice_count ? graph->choices[rule].source_state :
		graph->progress[rule - graph->choice_count].source_state;
}

static uint32_t RuleDestination(const sg_field_attractor_graph_t *graph,
	size_t rule, size_t offset)
{
	const sg_field_attractor_span_t span = RuleSpan(graph, rule);

	return rule < graph->choice_count ?
		graph->choice_destinations[(size_t)span.first + offset] :
		graph->progress_destinations[(size_t)span.first + offset];
}

static int GraphValid(const sg_field_attractor_graph_t *graph,
	size_t *rule_count, size_t *destination_count)
{
	size_t rule;
	size_t choice_cursor = 0U;
	size_t progress_cursor = 0U;

	if (!graph || graph->state_count == 0U ||
	    graph->state_count > UINT32_MAX || graph->state_count == SIZE_MAX ||
	    !graph->terminal_states ||
	    graph->choice_count > UINT32_MAX || graph->progress_count > UINT32_MAX ||
	    (graph->choice_count != 0U && !graph->choices) ||
	    (graph->choice_destination_count != 0U &&
	     !graph->choice_destinations) ||
	    (graph->progress_count != 0U && !graph->progress) ||
	    (graph->progress_destination_count != 0U &&
	     !graph->progress_destinations) ||
	    !AddSize(graph->choice_count, graph->progress_count, rule_count) ||
	    *rule_count > UINT32_MAX ||
	    !AddSize(graph->choice_destination_count,
		graph->progress_destination_count, destination_count))
		return 0;
	for (rule = 0U; rule < graph->state_count; rule++)
		if (graph->terminal_states[rule] > 1U)
			return 0;
	for (rule = 0U; rule < *rule_count; rule++)
	{
		sg_field_attractor_span_t span = RuleSpan(graph, rule);
		size_t destination;
		size_t capacity = rule < graph->choice_count ?
			graph->choice_destination_count :
			graph->progress_destination_count;

		if (RuleSource(graph, rule) >= graph->state_count || span.count == 0U ||
		    !SpanWithin(span, capacity) ||
		    (rule < graph->choice_count && span.first != choice_cursor) ||
		    (rule >= graph->choice_count && span.first != progress_cursor))
			return 0;
		if (rule < graph->choice_count)
			choice_cursor += span.count;
		else
			progress_cursor += span.count;
		for (destination = 0U; destination < span.count; destination++)
			if (RuleDestination(graph, rule, destination) >=
			    graph->state_count)
				return 0;
	}
	return choice_cursor == graph->choice_destination_count &&
		progress_cursor == graph->progress_destination_count;
}

static void WorkDestroy(sg_field_attractor_work_t *work)
{
	if (!work)
		return;
	free(work->remaining);
	free(work->maximum_rank);
	free(work->reverse_offsets);
	free(work->reverse_rules);
	free(work->queue);
	memset(work, 0, sizeof(*work));
}

static int WorkCreate(const sg_field_attractor_graph_t *graph,
	size_t rule_count, size_t destination_count,
	sg_field_attractor_work_t *work)
{
	size_t rule;
	size_t state;
	size_t *cursor;

	memset(work, 0, sizeof(*work));
	work->rule_count = rule_count;
	work->destination_count = destination_count;
	work->remaining = AllocateArray(rule_count, sizeof(*work->remaining));
	work->maximum_rank = AllocateArray(rule_count,
		sizeof(*work->maximum_rank));
	work->reverse_offsets = AllocateArray(graph->state_count + 1U,
		sizeof(*work->reverse_offsets));
	work->reverse_rules = AllocateArray(destination_count,
		sizeof(*work->reverse_rules));
	work->queue = AllocateArray(graph->state_count, sizeof(*work->queue));
	if ((rule_count != 0U && (!work->remaining || !work->maximum_rank)) ||
	    !work->reverse_offsets ||
	    (destination_count != 0U && !work->reverse_rules) || !work->queue)
		return 0;
	for (rule = 0U; rule < rule_count; rule++)
	{
		sg_field_attractor_span_t span = RuleSpan(graph, rule);
		size_t destination;
		work->remaining[rule] = span.count;
		for (destination = 0U; destination < span.count; destination++)
			work->reverse_offsets[
				(size_t)RuleDestination(graph, rule, destination) + 1U]++;
	}
	for (state = 1U; state <= graph->state_count; state++)
		work->reverse_offsets[state] += work->reverse_offsets[state - 1U];
	cursor = AllocateArray(graph->state_count, sizeof(*cursor));
	if (!cursor)
		return 0;
	memcpy(cursor, work->reverse_offsets,
		graph->state_count * sizeof(*cursor));
	for (rule = 0U; rule < rule_count; rule++)
	{
		sg_field_attractor_span_t span = RuleSpan(graph, rule);
		size_t destination;
		for (destination = 0U; destination < span.count; destination++)
		{
			uint32_t target = RuleDestination(graph, rule, destination);
			work->reverse_rules[cursor[target]++] = (uint32_t)rule;
		}
	}
	free(cursor);
	return 1;
}

static int ResultCreate(size_t state_count, sg_field_attractor_result_t *result)
{
	size_t state;

	result->state_count = state_count;
	result->reachable = AllocateArray(state_count, sizeof(*result->reachable));
	result->rank = AllocateArray(state_count, sizeof(*result->rank));
	result->witness_kind = AllocateArray(state_count,
		sizeof(*result->witness_kind));
	result->witness_index = AllocateArray(state_count,
		sizeof(*result->witness_index));
	if (!result->reachable || !result->rank || !result->witness_kind ||
	    !result->witness_index)
		return 0;
	for (state = 0U; state < state_count; state++)
	{
		result->witness_kind[state] = SG_FIELD_ATTRACTOR_WITNESS_CUT;
		result->witness_index[state] = SG_FIELD_ATTRACTOR_NO_WITNESS;
	}
	return 1;
}

sg_field_attractor_status_t SG_FieldAttractorSolve(
	const sg_field_attractor_graph_t *graph,
	sg_field_attractor_result_t *result)
{
	sg_field_attractor_work_t work = { 0 };
	size_t rule_count;
	size_t destination_count;
	size_t queue_head = 0U;
	size_t queue_tail = 0U;
	size_t state;

	if (!result || result->state_count != 0U || result->reachable ||
	    result->rank || result->witness_kind || result->witness_index ||
	    !GraphValid(graph, &rule_count, &destination_count))
		return SG_FIELD_ATTRACTOR_INVALID;
	if (!ResultCreate(graph->state_count, result) ||
	    !WorkCreate(graph, rule_count, destination_count, &work))
	{
		WorkDestroy(&work);
		SG_FieldAttractorResultDestroy(result);
		return SG_FIELD_ATTRACTOR_STORAGE_FAILURE;
	}
	for (state = 0U; state < graph->state_count; state++)
		if (graph->terminal_states[state])
		{
			result->reachable[state] = 1U;
			result->witness_kind[state] =
				SG_FIELD_ATTRACTOR_WITNESS_TERMINAL;
			work.queue[queue_tail++] = (uint32_t)state;
		}
	while (queue_head < queue_tail)
	{
		uint32_t reached = work.queue[queue_head++];
		size_t reverse;
		for (reverse = work.reverse_offsets[reached];
		     reverse < work.reverse_offsets[(size_t)reached + 1U]; reverse++)
		{
			uint32_t rule = work.reverse_rules[reverse];
			uint32_t source;
			uint32_t next_rank;
			if (work.remaining[rule] == 0U)
				continue;
			work.remaining[rule]--;
			if (result->rank[reached] > work.maximum_rank[rule])
				work.maximum_rank[rule] = result->rank[reached];
			if (work.remaining[rule] != 0U)
				continue;
			source = RuleSource(graph, rule);
			if (result->reachable[source])
				continue;
			if (work.maximum_rank[rule] == UINT32_MAX)
			{
				WorkDestroy(&work);
				SG_FieldAttractorResultDestroy(result);
				return SG_FIELD_ATTRACTOR_INVALID;
			}
			next_rank = work.maximum_rank[rule] + 1U;
			result->reachable[source] = 1U;
			result->rank[source] = next_rank;
			if ((size_t)rule < graph->choice_count)
			{
				result->witness_kind[source] =
					SG_FIELD_ATTRACTOR_WITNESS_CHOICE;
				result->witness_index[source] = rule;
			}
			else
			{
				result->witness_kind[source] =
					SG_FIELD_ATTRACTOR_WITNESS_LOCAL_PROGRESS;
				result->witness_index[source] =
					rule - (uint32_t)graph->choice_count;
			}
			work.queue[queue_tail++] = source;
		}
	}
	WorkDestroy(&work);
	if (!SG_FieldAttractorVerify(graph, result))
	{
		SG_FieldAttractorResultDestroy(result);
		return SG_FIELD_ATTRACTOR_INVALID;
	}
	return SG_FIELD_ATTRACTOR_OK;
}

static int RuleProvesState(const sg_field_attractor_graph_t *graph,
	const sg_field_attractor_result_t *result, size_t rule,
	uint32_t *maximum_rank)
{
	sg_field_attractor_span_t span = RuleSpan(graph, rule);
	size_t destination;
	uint32_t maximum = 0U;

	for (destination = 0U; destination < span.count; destination++)
	{
		uint32_t target = RuleDestination(graph, rule, destination);
		if (!result->reachable[target])
			return 0;
		if (result->rank[target] > maximum)
			maximum = result->rank[target];
	}
	if (maximum_rank)
		*maximum_rank = maximum;
	return 1;
}

int SG_FieldAttractorVerify(const sg_field_attractor_graph_t *graph,
	const sg_field_attractor_result_t *result)
{
	size_t rule_count;
	size_t destination_count;
	size_t state;
	size_t rule;

	if (!result || !GraphValid(graph, &rule_count, &destination_count) ||
	    result->state_count != graph->state_count || !result->reachable ||
	    !result->rank || !result->witness_kind || !result->witness_index)
		return 0;
	(void)destination_count;
	for (state = 0U; state < graph->state_count; state++)
	{
		uint32_t maximum_rank;
		size_t selected;
		if (graph->terminal_states[state])
		{
			if (!result->reachable[state] || result->rank[state] != 0U ||
			    result->witness_kind[state] !=
				SG_FIELD_ATTRACTOR_WITNESS_TERMINAL ||
			    result->witness_index[state] !=
				SG_FIELD_ATTRACTOR_NO_WITNESS)
				return 0;
			continue;
		}
		if (!result->reachable[state])
		{
			if (result->rank[state] != 0U ||
			    result->witness_kind[state] !=
				SG_FIELD_ATTRACTOR_WITNESS_CUT ||
			    result->witness_index[state] !=
				SG_FIELD_ATTRACTOR_NO_WITNESS)
				return 0;
			continue;
		}
		if (result->witness_kind[state] ==
		    SG_FIELD_ATTRACTOR_WITNESS_CHOICE)
		{
			if (result->witness_index[state] >= graph->choice_count)
				return 0;
			selected = result->witness_index[state];
		}
		else if (result->witness_kind[state] ==
			 SG_FIELD_ATTRACTOR_WITNESS_LOCAL_PROGRESS)
		{
			if (result->witness_index[state] >= graph->progress_count)
				return 0;
			selected = graph->choice_count + result->witness_index[state];
		}
		else
			return 0;
		if (selected >= rule_count || RuleSource(graph, selected) != state ||
		    !RuleProvesState(graph, result, selected, &maximum_rank) ||
		    maximum_rank == UINT32_MAX ||
		    result->rank[state] != maximum_rank + 1U)
			return 0;
	}
	for (rule = 0U; rule < rule_count; rule++)
		if (RuleProvesState(graph, result, rule, NULL) &&
		    !result->reachable[RuleSource(graph, rule)])
			return 0;
	return 1;
}

void SG_FieldAttractorResultDestroy(sg_field_attractor_result_t *result)
{
	if (!result)
		return;
	free(result->reachable);
	free(result->rank);
	free(result->witness_kind);
	free(result->witness_index);
	memset(result, 0, sizeof(*result));
}
