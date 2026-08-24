#include "q_shared.h"
#include "sg_rune_reverse_boundary.h"

#include <math.h>
#include <string.h>

static unsigned int ReverseBoundaryMask(uint32_t seed,
	const uint8_t *red_reach, const uint8_t *blue_reach)
{
	return (red_reach[seed] ? 1U : 0U) |
		(blue_reach[seed] ? 2U : 0U);
}

static int ReverseBoundaryPriority(uint8_t action)
{
	if (action == RL_DROP || action == RL_HOOK)
		return 0;
	if (action == RL_ROCKETJUMP || action == RL_JUMP)
		return 1;
	if (action == RL_RUN)
		return 2;
	return 3;
}

static int ReverseBoundaryBefore(
	const sg_rune_reverse_boundary_candidate_t *left,
	const sg_rune_reverse_boundary_candidate_t *right)
{
	int left_priority = ReverseBoundaryPriority(left->boundary_action);
	int right_priority = ReverseBoundaryPriority(right->boundary_action);

	if (left_priority != right_priority)
		return left_priority < right_priority;
	if (left->distance_squared != right->distance_squared)
		return left->distance_squared < right->distance_squared;
	if (left->component_gain != right->component_gain)
		return left->component_gain > right->component_gain;
	return left->link_index < right->link_index;
}

static void ReverseBoundarySort(
	sg_rune_reverse_boundary_candidate_t *candidates, uint32_t count)
{
	for (uint32_t index = 1U; index < count; index++)
	{
		sg_rune_reverse_boundary_candidate_t candidate = candidates[index];
		uint32_t slot = index;

		while (slot > 0U &&
		       ReverseBoundaryBefore(&candidate, &candidates[slot - 1U]))
		{
			candidates[slot] = candidates[slot - 1U];
			slot--;
		}
		candidates[slot] = candidate;
	}
}

uint32_t SG_RuneReverseBoundaryRank(
	const rune_seed_t *seeds, uint32_t seed_count,
	const rune_link_t *links, uint32_t link_count,
	const int *components, uint32_t component_count,
	uint32_t *component_sizes, uint32_t component_size_capacity,
	const uint8_t *red_reach, const uint8_t *blue_reach,
	float maximum_distance_squared,
	sg_rune_reverse_boundary_candidate_t *candidates,
	uint32_t candidate_capacity,
	sg_rune_reverse_boundary_report_t *report)
{
	uint32_t count = 0U;

	if (report)
		memset(report, 0, sizeof(*report));
	if (!seeds || !links || !components || !component_sizes ||
	    !red_reach || !blue_reach || !candidates || seed_count == 0U ||
	    component_count == 0U || component_size_capacity < component_count ||
	    candidate_capacity == 0U || !isfinite(maximum_distance_squared) ||
	    maximum_distance_squared <= 0.0f)
		return 0U;
	memset(component_sizes, 0,
	       sizeof(*component_sizes) * (size_t)component_count);
	for (uint32_t seed = 0U; seed < seed_count; seed++)
	{
		if (components[seed] < 0 ||
		    (uint32_t)components[seed] >= component_count)
			return 0U;
		component_sizes[components[seed]]++;
	}
	for (uint32_t link_index = 0U; link_index < link_count; link_index++)
	{
		const rune_link_t *link = &links[link_index];
		sg_rune_reverse_boundary_candidate_t candidate;
		unsigned int from_mask;
		unsigned int to_mask;
		int existing = -1;
		float delta[3];

		if (report)
			report->scanned++;
		if (link->from < 0 || link->to < 0 ||
		    (uint32_t)link->from >= seed_count ||
		    (uint32_t)link->to >= seed_count ||
		    components[link->from] == components[link->to])
			continue;
		from_mask = ReverseBoundaryMask(link->from, red_reach, blue_reach);
		to_mask = ReverseBoundaryMask(link->to, red_reach, blue_reach);
		if (from_mask == 0U || from_mask == to_mask ||
		    (to_mask & ~from_mask) != 0U)
			continue;
		if (report)
			report->crossing++;
		memset(&candidate, 0, sizeof(candidate));
		candidate.link_index = link_index;
		candidate.from = link->from;
		candidate.to = link->to;
		candidate.from_component = components[link->from];
		candidate.to_component = components[link->to];
		candidate.boundary_action = link->action;
		candidate.component_gain =
			(uint64_t)component_sizes[candidate.from_component] *
			(uint64_t)component_sizes[candidate.to_component];
		for (int axis = 0; axis < 3; axis++)
		{
			delta[axis] = seeds[link->from].origin[axis] -
				seeds[link->to].origin[axis];
			candidate.distance_squared += delta[axis] * delta[axis];
		}
		if (report)
		{
			if (link->action < SG_RUNE_REVERSE_ACTION_COUNT)
				report->action_counts[link->action]++;
			else
				report->invalid_action++;
			if (link->provenance < SG_RUNE_REVERSE_PROVENANCE_COUNT)
				report->provenance_counts[link->provenance]++;
			else
				report->invalid_provenance++;
			if (link->provenance != RL_PROVEN)
				report->rejected_provenance++;
			if (!isfinite(candidate.distance_squared) ||
			    candidate.distance_squared > maximum_distance_squared)
				report->rejected_distance++;
			if ((seeds[link->from].flags & RSF_WATER) &&
			    (seeds[link->to].flags & RSF_WATER))
				report->rejected_endpoints++;
		}
		if (link->action >= SG_RUNE_REVERSE_ACTION_COUNT ||
		    link->provenance != RL_PROVEN ||
		    !isfinite(candidate.distance_squared) ||
		    candidate.distance_squared > maximum_distance_squared ||
		    ((seeds[link->from].flags & RSF_WATER) &&
		     (seeds[link->to].flags & RSF_WATER)))
			continue;
		for (uint32_t index = 0U; index < count; index++)
			if (candidates[index].from == candidate.from &&
			    candidates[index].to == candidate.to)
			{
				existing = (int)index;
				break;
			}
		if (existing >= 0)
		{
			if (ReverseBoundaryBefore(&candidate, &candidates[existing]))
				candidates[existing] = candidate;
			ReverseBoundarySort(candidates, count);
			continue;
		}
		if (count < candidate_capacity)
			candidates[count++] = candidate;
		else if (ReverseBoundaryBefore(&candidate, &candidates[count - 1U]))
			candidates[count - 1U] = candidate;
		ReverseBoundarySort(candidates, count);
	}
	if (report)
	{
		report->unique_ranked_pairs = count;
		report->ranked = count;
	}
	return count;
}

sg_rune_topology_status_t SG_RuneReverseBoundaryRankGraph(
	const sg_rune_topology_graph_t *graph,
	const uint8_t *red_reach, const uint8_t *blue_reach,
	float maximum_distance_squared,
	sg_rune_reverse_boundary_candidate_t *candidates,
	uint32_t candidate_capacity, uint32_t *candidate_count_out,
	sg_rune_reverse_boundary_report_t *report,
	sg_rune_topology_allocate_fn allocate,
	sg_rune_topology_release_fn release)
{
	sg_rune_topology_snapshot_t snapshot;
	uint32_t *component_sizes = NULL;
	sg_rune_topology_status_t status;

	if (candidate_count_out)
		*candidate_count_out = 0U;
	if (!graph || !candidate_count_out || !allocate || !release ||
	    graph->seed_count == 0U)
		return SG_RUNE_TOPOLOGY_INVALID;
	memset(&snapshot, 0, sizeof(snapshot));
	snapshot.components = allocate(sizeof(*snapshot.components) *
		(size_t)graph->seed_count);
	component_sizes = allocate(sizeof(*component_sizes) *
		(size_t)graph->seed_count);
	if (!snapshot.components || !component_sizes)
	{
		status = SG_RUNE_TOPOLOGY_NO_MEMORY;
		goto done;
	}
	snapshot.component_capacity = graph->seed_count;
	status = SG_RuneTopologySnapshotBuild(graph, &snapshot, allocate, release);
	if (status != SG_RUNE_TOPOLOGY_OK)
		goto done;
	*candidate_count_out = SG_RuneReverseBoundaryRank(graph->seeds,
		graph->seed_count, graph->links, (uint32_t)*graph->link_count,
		snapshot.components, snapshot.component_count, component_sizes,
		graph->seed_count, red_reach, blue_reach,
		maximum_distance_squared, candidates,
		candidate_capacity, report);
done:
	if (component_sizes)
		release(component_sizes);
	if (snapshot.components)
		release(snapshot.components);
	return status;
}
