#include "q_shared.h"
#include "slipgate/sg_relay_wall_objective.h"

#include <float.h>
#include <string.h>

#define SG_RELAY_WALL_OBJECTIVE_CANDIDATES 32U
#define SG_RELAY_WALL_OBJECTIVE_FRONTIER 32U

typedef struct relay_wall_candidate_s
{
	uint32_t source;
	uint32_t destination;
	double score;
} relay_wall_candidate_t;

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

static void RelayWallCandidateInsert(relay_wall_candidate_t *candidates,
	uint32_t capacity, uint32_t source, uint32_t destination, double score)
{
	uint32_t index;

	for (index = 0U; index < capacity; index++)
		if (score < candidates[index].score)
		{
			uint32_t move;

			for (move = capacity - 1U; move > index; move--)
				candidates[move] = candidates[move - 1U];
			candidates[index].source = source;
			candidates[index].destination = destination;
			candidates[index].score = score;
			return;
		}
}

static void RelayWallSeedInsert(relay_wall_seed_t *frontier, uint32_t seed,
	double score)
{
	uint32_t index;

	for (index = 0U; index < SG_RELAY_WALL_OBJECTIVE_FRONTIER; index++)
		if (score < frontier[index].score)
		{
			uint32_t move;

			for (move = SG_RELAY_WALL_OBJECTIVE_FRONTIER - 1U;
			     move > index; move--)
				frontier[move] = frontier[move - 1U];
			frontier[index].seed = seed;
			frontier[index].score = score;
			return;
		}
}

int SG_RelayWallObjectiveBridge(
	const sg_relay_wall_objective_request_t *request,
	sg_relay_wall_objective_report_t *report_out)
{
	sg_relay_wall_objective_report_t report;
	uint32_t node_index;

	memset(&report, 0, sizeof(report));
	if (report_out)
		memset(report_out, 0, sizeof(*report_out));
	if (!request || !report_out || !request->catalog ||
	    !request->catalog->nodes || !request->seeds ||
	    request->seed_count == 0U || !request->components ||
	    !request->objective_masks || !request->eligible || !request->prove ||
	    !request->publish)
		return -1;
	for (node_index = 0U; node_index < request->catalog->num_nodes;
	     node_index++)
	{
		const rune_mechanism_node_t *entry =
			&request->catalog->nodes[node_index];
		sg_relay_wall_plan_witness_t witness;
		const rune_mechanism_node_t *wall;
		relay_wall_candidate_t candidates[SG_RELAY_WALL_OBJECTIVE_CANDIDATES];
		relay_wall_seed_t sources[SG_RELAY_WALL_OBJECTIVE_FRONTIER];
		relay_wall_seed_t destinations[SG_RELAY_WALL_OBJECTIVE_FRONTIER];
		uint32_t source;
		uint32_t candidate_index;
		int discovered;

		if (entry->kind != SG_MECH_NODE_BUTTON)
			continue;
		discovered = request->discover
			? request->discover(request->context, request->catalog,
				entry->key, &witness)
			: SG_RelayWallPlanDiscover(request->catalog, entry->key, &witness);
		if (discovered < 0)
			return -1;
		if (!discovered)
			continue;
		wall = RelayWallNode(request->catalog, witness.wall_key);
		if (!wall)
			return -1;
		report.mechanisms++;
		for (candidate_index = 0U;
		     candidate_index < SG_RELAY_WALL_OBJECTIVE_CANDIDATES;
		     candidate_index++)
		{
			candidates[candidate_index].source = UINT32_MAX;
			candidates[candidate_index].destination = UINT32_MAX;
			candidates[candidate_index].score = DBL_MAX;
		}
		for (candidate_index = 0U;
		     candidate_index < SG_RELAY_WALL_OBJECTIVE_FRONTIER;
		     candidate_index++)
		{
			sources[candidate_index].seed = UINT32_MAX;
			sources[candidate_index].score = DBL_MAX;
			destinations[candidate_index].seed = UINT32_MAX;
			destinations[candidate_index].score = DBL_MAX;
		}
		for (source = 0U; source < request->seed_count; source++)
		{
			if (request->eligible(request->context, source, 1))
				RelayWallSeedInsert(sources, source,
					SG_RelayWallNodeDistance2(&request->seeds[source], entry));
			if (request->eligible(request->context, source, 0))
				RelayWallSeedInsert(destinations, source,
					SG_RelayWallNodeDistance2(&request->seeds[source], wall));
		}
		for (source = 0U; source < SG_RELAY_WALL_OBJECTIVE_FRONTIER &&
		     sources[source].seed != UINT32_MAX; source++)
		{
			uint32_t destination;

			for (destination = 0U;
			     destination < SG_RELAY_WALL_OBJECTIVE_FRONTIER &&
			     destinations[destination].seed != UINT32_MAX; destination++)
			{
				uint32_t from = sources[source].seed;
				uint32_t to = destinations[destination].seed;
				double score = sources[source].score +
					destinations[destination].score;

				if (from == to || request->components[from] ==
				        request->components[to])
					continue;
				if ((request->objective_masks[to] &
				     ~request->objective_masks[from]) != 0U)
					score *= 0.5;
				RelayWallCandidateInsert(candidates,
					SG_RELAY_WALL_OBJECTIVE_CANDIDATES, from, to, score);
			}
		}
		for (candidate_index = 0U;
		     candidate_index < SG_RELAY_WALL_OBJECTIVE_CANDIDATES &&
		     candidates[candidate_index].source != UINT32_MAX;
		     candidate_index++)
		{
			sg_relay_wall_objective_proof_t proof;
			int proved;

			report.candidate_pairs++;
			memset(&proof, 0, sizeof(proof));
			report.proof_attempts++;
			proved = request->prove(request->context, &witness,
				candidates[candidate_index].source,
				candidates[candidate_index].destination, &proof);
			if (proved < 0)
				return -1;
			if (!proved)
				continue;
			if (!proof.cost_ms || !proof.egress_ms ||
			    proof.egress_ms > witness.active_window_ms ||
			    proof.sweep_clear_ms < proof.egress_ms)
				return -1;
			if (request->publish(request->context, &witness,
			        candidates[candidate_index].source,
			        candidates[candidate_index].destination, &proof) != 1)
				return -1;
			report.published = 1U;
			*report_out = report;
			return 1;
		}
	}
	*report_out = report;
	return 0;
}
