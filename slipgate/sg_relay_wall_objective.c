#include "q_shared.h"
#include "slipgate/sg_relay_wall_objective.h"

#include <float.h>
#include <stdlib.h>
#include <string.h>

typedef struct relay_wall_seed_s
{
	uint32_t seed;
	double score;
} relay_wall_seed_t;

static const rune_mechanism_node_t *RelayWallNode(
	const sg_mech_catalog_view_t *catalog, uint32_t key)
{
	uint32_t low = 0U;
	uint32_t high;

	if (!catalog || !catalog->nodes)
		return NULL;
	high = catalog->num_nodes;
	while (low < high)
	{
		uint32_t middle = low + (high - low) / 2U;

		if (catalog->nodes[middle].key < key)
			low = middle + 1U;
		else
			high = middle;
	}
	return low < catalog->num_nodes && catalog->nodes[low].key == key
		? &catalog->nodes[low] : NULL;
}

int SG_RelayWallNodeBounds(const rune_mechanism_node_t *node,
	float mins_out[3], float maxs_out[3])
{
	int axis;

	if (!node || !mins_out || !maxs_out)
		return 0;
	for (axis = 0; axis < 3; axis++)
	{
		mins_out[axis] = (float)node->absmin_q8[axis] * (1.0f / 8.0f);
		maxs_out[axis] = (float)node->absmax_q8[axis] * (1.0f / 8.0f);
		if (mins_out[axis] > maxs_out[axis])
			return 0;
	}
	return 1;
}

int SG_RelayWallSourceContactElevation(
	const rune_mechanism_node_t *entry, float origin_z)
{
	float mins[3];
	float maxs[3];

	return entry && entry->kind == SG_MECH_NODE_BUTTON &&
	       SG_RelayWallNodeBounds(entry, mins, maxs) &&
	       origin_z >= mins[2] - 32.0f && origin_z <= maxs[2] + 24.0f;
}

double SG_RelayWallNodeDistance2(const rune_seed_t *seed,
	const rune_mechanism_node_t *node)
{
	float mins[3];
	float maxs[3];
	double score = 0.0;
	int axis;

	if (!seed || !SG_RelayWallNodeBounds(node, mins, maxs))
		return DBL_MAX;
	mins[0] -= 16.0f;
	mins[1] -= 16.0f;
	mins[2] -= 32.0f;
	maxs[0] += 16.0f;
	maxs[1] += 16.0f;
	maxs[2] += 24.0f;
	for (axis = 0; axis < 3; axis++)
	{
		double delta = 0.0;

		if ((double)seed->origin[axis] < (double)mins[axis])
			delta = (double)mins[axis] - (double)seed->origin[axis];
		else if ((double)seed->origin[axis] > (double)maxs[axis])
			delta = (double)seed->origin[axis] - (double)maxs[axis];
		score += delta * delta;
	}
	return score;
}

static int RelayWallSeedCompare(const void *left_raw, const void *right_raw)
{
	const relay_wall_seed_t *left = left_raw;
	const relay_wall_seed_t *right = right_raw;

	return left->score < right->score ? -1 :
		left->score > right->score ? 1 :
		left->seed < right->seed ? -1 : left->seed > right->seed;
}

static int RelayWallSeedAppend(relay_wall_seed_t *frontier,
	uint32_t *count, uint32_t capacity, uint32_t seed, double score)
{
	if (!frontier || !count || *count >= capacity || score == DBL_MAX)
		return 0;
	frontier[*count].seed = seed;
	frontier[*count].score = score;
	(*count)++;
	return 1;
}

int SG_RelayWallObjectiveBridge(
	const sg_relay_wall_objective_request_t *request,
	sg_relay_wall_objective_report_t *report_out)
{
	sg_relay_wall_objective_report_t report;
	relay_wall_seed_t *sources;
	relay_wall_seed_t *destinations;
	uint32_t node_index;

	memset(&report, 0, sizeof(report));
	if (report_out)
		memset(report_out, 0, sizeof(*report_out));
	if (!request || !report_out || !request->catalog ||
	    !request->catalog->nodes || !request->seeds ||
	    request->seed_count == 0U || request->seed_count > RUNE_MAX_SEEDS ||
	    !request->components ||
	    !request->objective_masks || !request->eligible || !request->linked ||
	    !request->prove || !request->publish)
		return -1;
	sources = malloc((size_t)request->seed_count * sizeof(*sources));
	destinations = malloc((size_t)request->seed_count * sizeof(*destinations));
	if (!sources || !destinations)
	{
		free(sources);
		free(destinations);
		return -1;
	}
	for (node_index = 0U; node_index < request->catalog->num_nodes;
	     node_index++)
	{
		const rune_mechanism_node_t *entry =
			&request->catalog->nodes[node_index];
		sg_relay_wall_plan_witness_t witness;
		const rune_mechanism_node_t *wall;
		uint32_t source_count = 0U;
		uint32_t destination_count = 0U;
		uint32_t source_index;
		int discovered;

		if (entry->kind != SG_MECH_NODE_BUTTON)
			continue;
		discovered = request->discover
			? request->discover(request->context, request->catalog,
				entry->key, &witness)
			: SG_RelayWallPlanDiscover(request->catalog, entry->key, &witness);
		if (discovered < 0)
			goto fatal;
		if (!discovered)
			continue;
		wall = RelayWallNode(request->catalog, witness.wall_key);
		if (!wall)
			goto fatal;
		report.mechanisms++;
		/* Distance is only a canonical traversal order.  The old fixed
		 * frontiers silently dropped every eligible seed after rank 32; keep
		 * all finite endpoints so a later exact proof can still be selected. */
		for (source_index = 0U; source_index < request->seed_count;
		     source_index++)
		{
			int eligible;

			eligible = request->eligible(request->context, source_index, 1);
			if (eligible < 0)
				goto fatal;
			if (eligible)
				RelayWallSeedAppend(sources, &source_count,
					request->seed_count, source_index,
					SG_RelayWallNodeDistance2(
						&request->seeds[source_index], entry));
			eligible = request->eligible(request->context, source_index, 0);
			if (eligible < 0)
				goto fatal;
			if (eligible)
				RelayWallSeedAppend(destinations, &destination_count,
					request->seed_count, source_index,
					SG_RelayWallNodeDistance2(
						&request->seeds[source_index], wall));
		}
		qsort(sources, source_count, sizeof(*sources), RelayWallSeedCompare);
		qsort(destinations, destination_count, sizeof(*destinations),
			RelayWallSeedCompare);
		for (source_index = 0U; source_index < source_count; source_index++)
		{
			uint32_t destination_index;

			for (destination_index = 0U;
			     destination_index < destination_count; destination_index++)
			{
				uint32_t from = sources[source_index].seed;
				uint32_t to = destinations[destination_index].seed;

				if (from == to || request->components[from] ==
				        request->components[to])
					continue;
				{
					sg_relay_wall_objective_proof_t proof;
					int linked;
					int proved;

					linked = request->linked(request->context, from, to);
					if (linked < 0)
						goto fatal;
					if (linked)
						continue;
					report.candidate_pairs++;
					memset(&proof, 0, sizeof(proof));
					report.proof_attempts++;
					proved = request->prove(request->context, &witness,
						from, to, &proof);
					if (proved < 0)
						goto fatal;
					if (!proved)
						continue;
					if (!proof.cost_ms || !proof.egress_ms ||
					    proof.egress_ms > witness.active_window_ms ||
					    proof.sweep_clear_ms < proof.egress_ms)
						goto fatal;
					if (request->publish(request->context, &witness,
					        from, to, &proof) != 1)
						goto fatal;
					report.published = 1U;
					*report_out = report;
					free(sources);
					free(destinations);
					return 1;
				}
			}
		}
	}
	*report_out = report;
	free(sources);
	free(destinations);
	return 0;

fatal:
	free(sources);
	free(destinations);
	return -1;
}
