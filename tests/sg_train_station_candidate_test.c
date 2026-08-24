#include "q_shared.h"
#include "slipgate/sg_train_station_candidate.h"

#include <stdio.h>
#include <string.h>

#define NODE_COUNT 16U
#define EDGE_COUNT 17U

static int failures;

#define CHECK(condition_) do { \
	if (!(condition_)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, \
			#condition_); \
		failures++; \
	} \
} while (0)

static const uint32_t route[SG_TRAIN_STATION_ROUTE_CORNERS] = {
	28U, 29U, 30U, 31U, 32U, 33U, 34U,
	35U, 36U, 37U, 38U, 41U, 40U, 39U
};

static void Train(rune_mechanism_node_t *node, uint32_t key,
	uint32_t master, uint16_t team_flag)
{
	memset(node, 0, sizeof(*node));
	node->key = key;
	node->kind = SG_MECH_NODE_TRAIN;
	node->flags = SG_MECH_NODEF_REPEATABLE | SG_MECH_NODEF_USABLE |
		SG_MECH_NODEF_MOVER | team_flag;
	node->owner_key = SG_MECH_NO_KEY;
	node->team_master_key = master;
	node->spawnflags = 1U;
	node->use_callback = SG_MECH_CALLBACK_TRAIN_USE;
	node->think_callback = SG_MECH_CALLBACK_TRAIN_NEXT;
	node->blocked_callback = SG_MECH_CALLBACK_BLOCKED_TRAIN;
	node->speed_q8 = node->accel_q8 = node->decel_q8 = 3200U;
	node->target_offset = 1U;
}

static void Corner(rune_mechanism_node_t *node, uint32_t key, int station)
{
	memset(node, 0, sizeof(*node));
	node->key = key;
	node->kind = SG_MECH_NODE_PATH_CORNER;
	node->flags = SG_MECH_NODEF_REPEATABLE | SG_MECH_NODEF_TOUCHABLE;
	node->owner_key = SG_MECH_NO_KEY;
	node->team_master_key = SG_MECH_NO_KEY;
	node->touch_callback = SG_MECH_CALLBACK_PATH_CORNER_TOUCH;
	node->wait_ms = station ? (int32_t)SG_TRAIN_STATION_DWELL_MS : 0;
	node->target_offset = 1U;
	node->targetname_offset = 1U;
}

static void Edge(rune_mechanism_edge_t *edge, uint32_t from, uint32_t to,
	uint16_t kind)
{
	memset(edge, 0, sizeof(*edge));
	edge->from_key = from;
	edge->to_key = to;
	edge->kind = kind;
}

static void Fixture(rune_mechanism_node_t nodes[NODE_COUNT],
	rune_mechanism_edge_t edges[EDGE_COUNT], sg_mech_catalog_view_t *view)
{
	uint32_t i;

	memset(nodes, 0, sizeof(*nodes) * NODE_COUNT);
	memset(edges, 0, sizeof(*edges) * EDGE_COUNT);
	Train(&nodes[0], 5U, 5U, SG_MECH_NODEF_TEAM_MASTER);
	for (i = 0U; i < SG_TRAIN_STATION_ROUTE_CORNERS; i++)
		Corner(&nodes[i + 1U], 28U + i,
			route[i] == 28U || route[i] == 35U);
	for (i = 0U; i < SG_TRAIN_STATION_ROUTE_CORNERS; i++)
	{
		uint32_t j;

		for (j = i + 1U; j < SG_TRAIN_STATION_ROUTE_CORNERS; j++)
			if (nodes[j + 1U].key < nodes[i + 1U].key)
			{
				rune_mechanism_node_t swap = nodes[i + 1U];
				nodes[i + 1U] = nodes[j + 1U];
				nodes[j + 1U] = swap;
			}
	}
	Train(&nodes[15], 42U, 5U, SG_MECH_NODEF_TEAM_MEMBER);
	Edge(&edges[0], 5U, 42U, SG_MECH_EDGE_TEAM);
	Edge(&edges[1], 5U, 29U, SG_MECH_EDGE_ROUTE_TARGET);
	Edge(&edges[2], 42U, 36U, SG_MECH_EDGE_ROUTE_TARGET);
	for (i = 0U; i < SG_TRAIN_STATION_ROUTE_CORNERS; i++)
		Edge(&edges[i + 3U], route[i],
			route[(i + 1U) % SG_TRAIN_STATION_ROUTE_CORNERS],
			SG_MECH_EDGE_ROUTE_TARGET);
	for (i = 0U; i < EDGE_COUNT; i++)
	{
		uint32_t j;

		for (j = i + 1U; j < EDGE_COUNT; j++)
			if (edges[j].from_key < edges[i].from_key ||
			    (edges[j].from_key == edges[i].from_key &&
			     edges[j].kind < edges[i].kind))
			{
				rune_mechanism_edge_t swap = edges[i];
				edges[i] = edges[j];
				edges[j] = swap;
			}
	}
	memset(view, 0, sizeof(*view));
	view->nodes = nodes;
	view->num_nodes = NODE_COUNT;
	view->edges = edges;
	view->num_edges = EDGE_COUNT;
}

static void TestTwoWayDwellCandidate(void)
{
	rune_mechanism_node_t nodes[NODE_COUNT];
	rune_mechanism_edge_t edges[EDGE_COUNT];
	sg_mech_catalog_view_t view;
	sg_train_station_candidate_t candidate;

	Fixture(nodes, edges, &view);
	CHECK(SG_TrainStationCandidatesDiscover(&view, &candidate, 1U) == 1U);
	CHECK(candidate.binding.entry_key == 28U);
	CHECK(candidate.binding.mover_key == 5U);
	CHECK(candidate.binding.destination_key == 35U);
	CHECK(candidate.binding.egress_key == 42U);
	CHECK(candidate.binding.controller_kind ==
		SG_MECHANISM_CONTROLLER_TRAIN_STATION);
	CHECK(candidate.binding.expected_members == 2U);
	CHECK(candidate.binding.cooldown_ms == SG_TRAIN_STATION_DWELL_MS);
	CHECK(candidate.directions[0].source_station_key == 28U);
	CHECK(candidate.directions[0].destination_station_key == 35U);
	CHECK(candidate.directions[0].ride_train_key == 5U);
	CHECK(candidate.directions[1].source_station_key == 35U);
	CHECK(candidate.directions[1].destination_station_key == 28U);
	CHECK(candidate.directions[1].ride_train_key == 42U);
	CHECK(candidate.directions[0].source_dwell_ms == 3000U);
	CHECK(candidate.directions[0].destination_dwell_ms == 3000U);
	CHECK(candidate.directions[1].source_dwell_ms == 3000U);
	CHECK(candidate.directions[1].destination_dwell_ms == 3000U);
}

static void TestRequiresBothExactDwells(void)
{
	rune_mechanism_node_t nodes[NODE_COUNT];
	rune_mechanism_edge_t edges[EDGE_COUNT];
	sg_mech_catalog_view_t view;
	sg_train_station_candidate_t candidate;

	Fixture(nodes, edges, &view);
	nodes[8].wait_ms = 0;
	CHECK(SG_TrainStationCandidatesDiscover(&view, &candidate, 1U) == 0U);
	Fixture(nodes, edges, &view);
	nodes[8].wait_ms = 2000;
	CHECK(SG_TrainStationCandidatesDiscover(&view, &candidate, 1U) == 0U);
}

static void TestExactLmctf25CatalogCandidate(void)
{
	rune_mechanism_node_t nodes[28];
	rune_mechanism_edge_t edges[29];
	sg_mech_catalog_view_t view;
	sg_train_station_candidate_t candidate;
	static const uint32_t actual_route[SG_TRAIN_STATION_ROUTE_CORNERS] = {
		30U, 31U, 32U, 33U, 34U, 35U, 36U,
		37U, 38U, 39U, 40U, 43U, 42U, 41U
	};
	static const uint32_t effect_corners[6] = {
		30U, 31U, 34U, 37U, 38U, 43U
	};
	uint32_t node_count = 0U;
	uint32_t edge_count = 0U;
	uint32_t i;

	memset(nodes, 0, sizeof(nodes));
	memset(edges, 0, sizeof(edges));
	Train(&nodes[node_count++], 25U, 25U, SG_MECH_NODEF_TEAM_MASTER);
	for (i = 0U; i < SG_TRAIN_STATION_ROUTE_CORNERS; i++)
		Corner(&nodes[node_count++], actual_route[i],
			actual_route[i] == 30U || actual_route[i] == 37U);
	Train(&nodes[node_count++], 44U, 25U, SG_MECH_NODEF_TEAM_MEMBER);
	for (i = 0U; i < 12U; i++)
	{
		rune_mechanism_node_t *speaker = &nodes[node_count++];

		memset(speaker, 0, sizeof(*speaker));
		speaker->key = 52U + i;
		speaker->kind = SG_MECH_NODE_TARGET_SPEAKER;
		speaker->flags = SG_MECH_NODEF_REPEATABLE | SG_MECH_NODEF_USABLE;
		speaker->owner_key = SG_MECH_NO_KEY;
		speaker->team_master_key = SG_MECH_NO_KEY;
		speaker->use_callback = SG_MECH_CALLBACK_USE_TARGET_SPEAKER;
		speaker->targetname_offset = 1U;
	}
	Edge(&edges[edge_count++], 25U, 44U, SG_MECH_EDGE_TEAM);
	Edge(&edges[edge_count++], 25U, 31U, SG_MECH_EDGE_ROUTE_TARGET);
	Edge(&edges[edge_count++], 44U, 38U, SG_MECH_EDGE_ROUTE_TARGET);
	for (i = 0U; i < SG_TRAIN_STATION_ROUTE_CORNERS; i++)
		Edge(&edges[edge_count++], actual_route[i],
			actual_route[(i + 1U) % SG_TRAIN_STATION_ROUTE_CORNERS],
			SG_MECH_EDGE_ROUTE_TARGET);
	for (i = 0U; i < 6U; i++)
	{
		uint32_t node_index;

		for (node_index = 0U; node_index < node_count; node_index++)
			if (nodes[node_index].key == effect_corners[i])
				nodes[node_index].path_target_offset = 1U;
		Edge(&edges[edge_count++], effect_corners[i], 52U + i * 2U,
			SG_MECH_EDGE_PATH_TARGET);
		Edge(&edges[edge_count++], effect_corners[i], 53U + i * 2U,
			SG_MECH_EDGE_PATH_TARGET);
	}
	for (i = 0U; i < node_count; i++)
	{
		uint32_t j;

		for (j = i + 1U; j < node_count; j++)
			if (nodes[j].key < nodes[i].key)
			{
				rune_mechanism_node_t swap = nodes[i];
				nodes[i] = nodes[j];
				nodes[j] = swap;
			}
	}
	for (i = 0U; i < edge_count; i++)
	{
		uint32_t j;

		for (j = i + 1U; j < edge_count; j++)
			if (edges[j].from_key < edges[i].from_key ||
			    (edges[j].from_key == edges[i].from_key &&
			     edges[j].kind < edges[i].kind))
			{
				rune_mechanism_edge_t swap = edges[i];
				edges[i] = edges[j];
				edges[j] = swap;
			}
	}
	memset(&view, 0, sizeof(view));
	view.nodes = nodes;
	view.num_nodes = node_count;
	view.edges = edges;
	view.num_edges = edge_count;
	CHECK(node_count == 28U);
	CHECK(edge_count == 29U);
	CHECK(SG_TrainStationCandidatesDiscover(&view, &candidate, 1U) == 1U);
	CHECK(candidate.binding.entry_key == 30U);
	CHECK(candidate.binding.mover_key == 25U);
	CHECK(candidate.binding.destination_key == 37U);
	CHECK(candidate.binding.egress_key == 44U);
}

int main(void)
{
	TestTwoWayDwellCandidate();
	TestRequiresBothExactDwells();
	TestExactLmctf25CatalogCandidate();
	if (failures)
		return 1;
	puts("sg_train_station_candidate_test: PASS");
	return 0;
}
