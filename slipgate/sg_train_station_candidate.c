#include "../q_shared.h"
#include "sg_train_station_candidate.h"

#include <string.h>

static void CandidateFill(sg_train_station_candidate_t *candidate,
	uint32_t entry_key, uint32_t mover_key, uint32_t destination_key,
	uint32_t companion_key,
	const sg_train_station_plan_witness_t *witness)
{
	memset(candidate, 0, sizeof(*candidate));
	candidate->binding.entry_key = entry_key;
	candidate->binding.mover_key = mover_key;
	candidate->binding.destination_key = destination_key;
	candidate->binding.egress_key = companion_key;
	candidate->binding.controller_kind =
		SG_MECHANISM_CONTROLLER_TRAIN_STATION;
	candidate->binding.expected_members = 2U;
	candidate->binding.cooldown_ms = SG_TRAIN_STATION_DWELL_MS;
	candidate->witness = *witness;
	candidate->directions[0].source_station_key = entry_key;
	candidate->directions[0].destination_station_key = destination_key;
	candidate->directions[0].ride_train_key = mover_key;
	candidate->directions[1].source_station_key = destination_key;
	candidate->directions[1].destination_station_key = entry_key;
	candidate->directions[1].ride_train_key = companion_key;
	candidate->directions[0].source_dwell_ms =
		candidate->directions[0].destination_dwell_ms =
		candidate->directions[1].source_dwell_ms =
		candidate->directions[1].destination_dwell_ms =
		SG_TRAIN_STATION_DWELL_MS;
}

uint32_t SG_TrainStationCandidatesDiscover(
	const sg_mech_catalog_view_t *catalog,
	sg_train_station_candidate_t *candidates, uint32_t capacity)
{
	uint32_t count = 0U;
	uint32_t train_index;

	if (!catalog || !catalog->nodes || !catalog->edges ||
	    (!candidates && capacity != 0U))
		return 0U;
	for (train_index = 0U; train_index < catalog->num_nodes; train_index++)
	{
		const rune_mechanism_node_t *train = &catalog->nodes[train_index];
		uint32_t station_index;

		if (train->kind != SG_MECH_NODE_TRAIN ||
		    (train->flags & SG_MECH_NODEF_TEAM_MASTER) == 0U)
			continue;
		for (station_index = 0U; station_index < catalog->num_nodes;
		     station_index++)
		{
			const rune_mechanism_node_t *station =
				&catalog->nodes[station_index];
			sg_train_station_plan_witness_t witness;
			uint32_t destination_key;
			uint32_t companion_key;

			if (station->kind != SG_MECH_NODE_PATH_CORNER ||
			    station->wait_ms != (int32_t)SG_TRAIN_STATION_DWELL_MS ||
			    !SG_TrainStationPlanDiscover(catalog, station->key,
			        train->key, &destination_key, &companion_key, &witness))
				continue;
			if (count < capacity)
				CandidateFill(&candidates[count], station->key,
					train->key, destination_key, companion_key, &witness);
			count++;
		}
	}
	return count;
}
