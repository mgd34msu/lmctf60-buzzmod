#include "q_shared.h"
#include "slipgate/sg_train_station_plan.h"

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
	node->wait_ms = station ? 3000 : 0;
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
	/* Restore sorted node keys for the non-numeric tail of the route. */
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
	/* The production inventory is sorted by from key, kind, ordinal. */
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

int main(void)
{
	rune_mechanism_node_t nodes[NODE_COUNT];
	rune_mechanism_edge_t edges[EDGE_COUNT];
	sg_mech_catalog_view_t view;
	sg_train_station_plan_witness_t witness;
	uint32_t destination;
	uint32_t companion;

	Fixture(nodes, edges, &view);
	CHECK(SG_TrainStationPlanAuthenticate(&view, 28U, 5U, 35U, 42U,
		&witness));
	CHECK(witness.edge_count == EDGE_COUNT);
	CHECK(witness.route_count == SG_TRAIN_STATION_ROUTE_CORNERS);
	CHECK(witness.station_keys[0] == 28U);
	CHECK(witness.station_keys[1] == 35U);
	CHECK(SG_TrainStationPlanDiscover(&view, 28U, 5U, &destination,
		&companion, &witness));
	CHECK(destination == 35U);
	CHECK(companion == 42U);

	Fixture(nodes, edges, &view);
	nodes[8].wait_ms = 0;
	CHECK(!SG_TrainStationPlanAuthenticate(&view, 28U, 5U, 35U, 42U,
		&witness));

	Fixture(nodes, edges, &view);
	nodes[2].wait_ms = 3000;
	CHECK(!SG_TrainStationPlanAuthenticate(&view, 28U, 5U, 35U, 42U,
		&witness));

	Fixture(nodes, edges, &view);
	edges[0].to_key = 41U;
	CHECK(!SG_TrainStationPlanAuthenticate(&view, 28U, 5U, 35U, 42U,
		&witness));

	Fixture(nodes, edges, &view);
	nodes[15].team_master_key = 42U;
	CHECK(!SG_TrainStationPlanAuthenticate(&view, 28U, 5U, 35U, 42U,
		&witness));

	if (failures)
		return 1;
	puts("sg_train_station_plan_test: PASS");
	return 0;
}
