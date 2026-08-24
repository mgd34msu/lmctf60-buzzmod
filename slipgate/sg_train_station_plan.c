#include "../q_shared.h"
#include "sg_train_station_plan.h"

#include <limits.h>
#include <string.h>

static uint32_t FindNode(const sg_mech_catalog_view_t *catalog, uint32_t key)
{
	uint32_t low = 0U;
	uint32_t high = catalog->num_nodes;

	while (low < high)
	{
		uint32_t middle = low + (high - low) / 2U;

		if (catalog->nodes[middle].key < key)
			low = middle + 1U;
		else
			high = middle;
	}
	return low < catalog->num_nodes && catalog->nodes[low].key == key
		? low : UINT32_MAX;
}

static int Executable(const rune_mechanism_node_t *node)
{
	return node && (node->flags & SG_MECH_NODEF_INVENTORY_ONLY) == 0U &&
		node->touch_callback != SG_MECH_CALLBACK_UNKNOWN &&
		node->use_callback != SG_MECH_CALLBACK_UNKNOWN &&
		node->think_callback != SG_MECH_CALLBACK_UNKNOWN &&
		node->blocked_callback != SG_MECH_CALLBACK_UNKNOWN;
}

static int TrainShape(const rune_mechanism_node_t *train,
	uint16_t team_flag, uint32_t master_key)
{
	return Executable(train) && train->kind == SG_MECH_NODE_TRAIN &&
		train->flags == (SG_MECH_NODEF_REPEATABLE | SG_MECH_NODEF_USABLE |
			SG_MECH_NODEF_MOVER | team_flag) &&
		train->team_master_key == master_key && train->spawnflags == 1U &&
		train->touch_callback == SG_MECH_CALLBACK_NONE &&
		train->use_callback == SG_MECH_CALLBACK_TRAIN_USE &&
		train->think_callback == SG_MECH_CALLBACK_TRAIN_NEXT &&
		train->blocked_callback == SG_MECH_CALLBACK_BLOCKED_TRAIN &&
		train->delay_ms == 0 && train->wait_ms == 0 &&
		train->speed_q8 != 0U && train->speed_q8 == train->accel_q8 &&
		train->speed_q8 == train->decel_q8 &&
		train->target_offset != 0U && train->targetname_offset == 0U &&
		train->killtarget_offset == 0U && train->path_target_offset == 0U &&
		train->owner_key == SG_MECH_NO_KEY;
}

static int CornerShape(const rune_mechanism_node_t *corner)
{
	return Executable(corner) && corner->kind == SG_MECH_NODE_PATH_CORNER &&
		corner->flags == (SG_MECH_NODEF_REPEATABLE |
			SG_MECH_NODEF_TOUCHABLE) && corner->spawnflags == 0U &&
		corner->touch_callback == SG_MECH_CALLBACK_PATH_CORNER_TOUCH &&
		corner->use_callback == SG_MECH_CALLBACK_NONE &&
		corner->think_callback == SG_MECH_CALLBACK_NONE &&
		corner->blocked_callback == SG_MECH_CALLBACK_NONE &&
		corner->delay_ms == 0 &&
		(corner->wait_ms == 0 || corner->wait_ms == 3000) &&
		corner->target_offset != 0U && corner->targetname_offset != 0U &&
		corner->killtarget_offset == 0U &&
		corner->owner_key == SG_MECH_NO_KEY &&
		corner->team_master_key == SG_MECH_NO_KEY;
}

static int SpeakerShape(const rune_mechanism_node_t *speaker)
{
	return Executable(speaker) &&
		speaker->kind == SG_MECH_NODE_TARGET_SPEAKER &&
		speaker->flags == (SG_MECH_NODEF_REPEATABLE | SG_MECH_NODEF_USABLE) &&
		speaker->use_callback == SG_MECH_CALLBACK_USE_TARGET_SPEAKER &&
		speaker->touch_callback == SG_MECH_CALLBACK_NONE &&
		speaker->think_callback == SG_MECH_CALLBACK_NONE &&
		speaker->blocked_callback == SG_MECH_CALLBACK_NONE &&
		(speaker->spawnflags & UINT32_C(3)) == 0U &&
		speaker->target_offset == 0U && speaker->killtarget_offset == 0U &&
		speaker->path_target_offset == 0U;
}

static uint32_t EdgeCount(const sg_mech_catalog_view_t *catalog,
	uint32_t from_key, uint16_t kind, uint32_t *first_out)
{
	uint32_t count = 0U;
	uint32_t i;

	if (first_out)
		*first_out = UINT32_MAX;
	for (i = 0U; i < catalog->num_edges; i++)
		if (catalog->edges[i].from_key == from_key &&
		    catalog->edges[i].kind == kind)
		{
			if (count == 0U && first_out)
				*first_out = i;
			count++;
		}
	return count;
}

static int AddEdge(sg_train_station_plan_witness_t *witness,
	uint32_t edge_index)
{
	if (edge_index == UINT32_MAX ||
	    witness->edge_count >= SG_TRAIN_STATION_PLAN_MAX_EDGES)
		return 0;
	witness->edge_indices[witness->edge_count++] = edge_index;
	return 1;
}

int SG_TrainStationPlanAuthenticate(const sg_mech_catalog_view_t *catalog,
	uint32_t entry_key, uint32_t mover_key, uint32_t destination_key,
	uint32_t companion_key, sg_train_station_plan_witness_t *witness_out)
{
	sg_train_station_plan_witness_t witness;
	const rune_mechanism_node_t *mover;
	const rune_mechanism_node_t *companion;
	uint32_t entry_index;
	uint32_t cursor;
	uint32_t team_edge;
	uint32_t mover_route;
	uint32_t companion_route;
	uint32_t station_count = 0U;
	uint32_t destination_position = UINT32_MAX;
	uint32_t i;

	if (witness_out)
		memset(witness_out, 0, sizeof(*witness_out));
	if (!catalog || !witness_out || !catalog->nodes || !catalog->edges ||
	    entry_key == destination_key || mover_key == companion_key ||
	    (entry_index = FindNode(catalog, entry_key)) == UINT32_MAX)
		return 0;
	memset(&witness, 0, sizeof(witness));
	i = FindNode(catalog, mover_key);
	if (i == UINT32_MAX)
		return 0;
	mover = &catalog->nodes[i];
	i = FindNode(catalog, companion_key);
	if (i == UINT32_MAX)
		return 0;
	companion = &catalog->nodes[i];
	if (!TrainShape(mover, SG_MECH_NODEF_TEAM_MASTER, mover_key) ||
	    !TrainShape(companion, SG_MECH_NODEF_TEAM_MEMBER, mover_key) ||
	    mover->speed_q8 != companion->speed_q8 ||
	    EdgeCount(catalog, mover_key, SG_MECH_EDGE_TEAM, &team_edge) != 1U ||
	    catalog->edges[team_edge].to_key != companion_key ||
	    EdgeCount(catalog, mover_key, SG_MECH_EDGE_ROUTE_TARGET,
	        &mover_route) != 1U ||
	    EdgeCount(catalog, companion_key, SG_MECH_EDGE_ROUTE_TARGET,
	        &companion_route) != 1U ||
	    EdgeCount(catalog, mover_key, SG_MECH_EDGE_KILLTARGET, NULL) != 0U ||
	    EdgeCount(catalog, companion_key, SG_MECH_EDGE_KILLTARGET, NULL) != 0U ||
	    EdgeCount(catalog, mover_key, SG_MECH_EDGE_PATH_TARGET, NULL) != 0U ||
	    EdgeCount(catalog, companion_key, SG_MECH_EDGE_PATH_TARGET, NULL) != 0U ||
	    !AddEdge(&witness, team_edge) || !AddEdge(&witness, mover_route) ||
	    !AddEdge(&witness, companion_route))
		return 0;

	cursor = entry_index;
	for (i = 0U; i < SG_TRAIN_STATION_ROUTE_CORNERS; i++)
	{
		const rune_mechanism_node_t *corner = &catalog->nodes[cursor];
		uint32_t route_edge;
		uint32_t effect_first;
		uint32_t effect_count;
		uint32_t effect;
		uint32_t next;
		uint32_t earlier;

		if (!CornerShape(corner) ||
		    EdgeCount(catalog, corner->key, SG_MECH_EDGE_KILLTARGET,
		        NULL) != 0U)
			return 0;
		for (earlier = 0U; earlier < witness.route_count; earlier++)
			if (witness.route_keys[earlier] == corner->key)
				return 0;
		witness.route_keys[witness.route_count++] = corner->key;
		if (corner->wait_ms == 3000)
		{
			if (station_count >= 2U)
				return 0;
			witness.station_keys[station_count++] = corner->key;
		}
		if (corner->key == destination_key)
			destination_position = i;
		if (EdgeCount(catalog, corner->key, SG_MECH_EDGE_ROUTE_TARGET,
		        &route_edge) != 1U || !AddEdge(&witness, route_edge))
			return 0;
		effect_count = EdgeCount(catalog, corner->key,
			SG_MECH_EDGE_PATH_TARGET, &effect_first);
		if ((corner->path_target_offset == 0U) != (effect_count == 0U))
			return 0;
		for (effect = 0U; effect < effect_count; effect++)
		{
			const rune_mechanism_edge_t *edge =
				&catalog->edges[effect_first + effect];
			uint32_t speaker = FindNode(catalog, edge->to_key);

			if (edge->from_key != corner->key ||
			    edge->kind != SG_MECH_EDGE_PATH_TARGET ||
			    edge->delay_ms != 0U || speaker == UINT32_MAX ||
			    !SpeakerShape(&catalog->nodes[speaker]) ||
			    !AddEdge(&witness, effect_first + effect))
				return 0;
		}
		next = FindNode(catalog, catalog->edges[route_edge].to_key);
		if (next == UINT32_MAX)
			return 0;
		cursor = next;
	}
	if (cursor != entry_index || witness.route_count !=
	        SG_TRAIN_STATION_ROUTE_CORNERS || station_count != 2U ||
	    witness.station_keys[0] != entry_key ||
	    witness.station_keys[1] != destination_key ||
	    destination_position != SG_TRAIN_STATION_ROUTE_CORNERS / 2U ||
	    catalog->edges[mover_route].to_key != witness.route_keys[1] ||
	    catalog->edges[companion_route].to_key !=
	        witness.route_keys[SG_TRAIN_STATION_ROUTE_CORNERS / 2U + 1U])
		return 0;
	*witness_out = witness;
	return 1;
}

int SG_TrainStationPlanDiscover(const sg_mech_catalog_view_t *catalog,
	uint32_t entry_key, uint32_t mover_key, uint32_t *destination_key_out,
	uint32_t *companion_key_out,
	sg_train_station_plan_witness_t *witness_out)
{
	uint32_t team_edge;
	uint32_t companion_key;
	uint32_t cursor = entry_key;
	uint32_t route_edge;
	uint32_t step;

	if (destination_key_out)
		*destination_key_out = SG_MECH_NO_KEY;
	if (companion_key_out)
		*companion_key_out = SG_MECH_NO_KEY;
	if (witness_out)
		memset(witness_out, 0, sizeof(*witness_out));
	if (!catalog || !destination_key_out || !companion_key_out ||
	    !witness_out ||
	    EdgeCount(catalog, mover_key, SG_MECH_EDGE_TEAM, &team_edge) != 1U)
		return 0;
	companion_key = catalog->edges[team_edge].to_key;
	for (step = 0U; step < SG_TRAIN_STATION_ROUTE_CORNERS / 2U; step++)
	{
		if (EdgeCount(catalog, cursor, SG_MECH_EDGE_ROUTE_TARGET,
		        &route_edge) != 1U)
			return 0;
		cursor = catalog->edges[route_edge].to_key;
	}
	if (!SG_TrainStationPlanAuthenticate(catalog, entry_key, mover_key,
	        cursor, companion_key, witness_out))
		return 0;
	*destination_key_out = cursor;
	*companion_key_out = companion_key;
	return 1;
}
