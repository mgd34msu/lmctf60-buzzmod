#include <stdio.h>
#include <string.h>

#include "slipgate/sg_water_forest.h"

#define CHECK(expression) do { if (!(expression)) return fail(__LINE__); } while (0)

typedef struct proof_fixture_s
{
	int allowed[8][8];
	int calls[8][8];
} proof_fixture_t;

static int fail(int line)
{
	fprintf(stderr, "sg_water_forest_test: check failed at line %d\n", line);
	return 1;
}

static int prove(void *context, int from, int to, sg_water_proof_t *proof)
{
	proof_fixture_t *fixture = context;

	fixture->calls[from][to]++;
	if (!fixture->allowed[from][to])
		return 0;
	proof->cost_ms = 100 + from * 8 + to;
	proof->exit_speed = (uint8_t)(from * 8 + to);
	return 1;
}

static void allow_pair(proof_fixture_t *fixture, int a, int b)
{
	fixture->allowed[a][b] = 1;
	fixture->allowed[b][a] = 1;
}

static int deterministic_spanning_forest(void)
{
	static const int pairs[][2] = { {0, 1}, {2, 3}, {1, 2}, {3, 0} };
	sg_water_edge_t first_edges[8], second_edges[8];
	int first_parents[4], second_parents[4];
	uint8_t first_ranks[4], second_ranks[4];
	int first_slots[16], second_slots[16];
	sg_water_forest_t first, second;
	proof_fixture_t first_fixture = {0}, second_fixture = {0};
	size_t i;

	for (i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++)
	{
		allow_pair(&first_fixture, pairs[i][0], pairs[i][1]);
		allow_pair(&second_fixture, pairs[i][0], pairs[i][1]);
	}
	CHECK(SG_WaterForestInit(&first, first_parents, first_ranks, 4,
		first_edges, 8, first_slots, 16));
	CHECK(SG_WaterForestInit(&second, second_parents, second_ranks, 4,
		second_edges, 8, second_slots, 16));
	for (i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++)
	{
		CHECK(SG_WaterForestConnect(&first, pairs[i][0], pairs[i][1],
			prove, &first_fixture) != SG_WATER_CONNECT_INVALID);
		CHECK(SG_WaterForestConnect(&second, pairs[i][0], pairs[i][1],
			prove, &second_fixture) != SG_WATER_CONNECT_INVALID);
	}
	CHECK(first.edge_count == 6);
	CHECK(second.edge_count == first.edge_count);
	CHECK(memcmp(first.edges, second.edges,
		first.edge_count * sizeof(first.edges[0])) == 0);
	CHECK(first_fixture.calls[3][0] == 0);
	CHECK(first_fixture.calls[0][3] == 0);
	return 0;
}

static int directed_merge_requires_both_proofs(void)
{
	sg_water_edge_t edges[4];
	int parents[2];
	uint8_t ranks[2];
	int slots[8];
	sg_water_forest_t forest;
	proof_fixture_t fixture = {0};

	fixture.allowed[0][1] = 1;
	CHECK(SG_WaterForestInit(&forest, parents, ranks, 2, edges, 4, slots, 8));
	CHECK(SG_WaterForestConnect(&forest, 0, 1, prove, &fixture) ==
		SG_WATER_CONNECT_RECORDED);
	CHECK(forest.edge_count == 1);
	fixture.allowed[1][0] = 1;
	CHECK(SG_WaterForestConnect(&forest, 0, 1, prove, &fixture) ==
		SG_WATER_CONNECT_RECORDED);
	CHECK(forest.edge_count == 2);
	CHECK(SG_WaterForestConnect(&forest, 1, 0, prove, &fixture) ==
		SG_WATER_CONNECT_ALREADY);
	return 0;
}

static int overflow_is_atomic(void)
{
	sg_water_edge_t edges[1];
	int parents[2];
	uint8_t ranks[2];
	int slots[2];
	sg_water_forest_t forest;
	proof_fixture_t fixture = {0};

	allow_pair(&fixture, 0, 1);
	CHECK(SG_WaterForestInit(&forest, parents, ranks, 2, edges, 1, slots, 2));
	CHECK(SG_WaterForestConnect(&forest, 0, 1, prove, &fixture) ==
		SG_WATER_CONNECT_OVERFLOW);
	CHECK(forest.edge_count == 0);
	CHECK(forest.overflow);
	return 0;
}

static int colliding_edge_slots_keep_identity(void)
{
	sg_water_edge_t edges[4];
	int parents[4], slots[8];
	uint8_t ranks[4];
	sg_water_forest_t forest;
	proof_fixture_t fixture = {0};

	fixture.allowed[0][3] = 1;
	fixture.allowed[1][0] = 1;
	CHECK(SG_WaterForestInit(&forest, parents, ranks, 4, edges, 4, slots, 8));
	CHECK(SG_WaterForestConnect(&forest, 0, 3, prove, &fixture) ==
		SG_WATER_CONNECT_RECORDED);
	CHECK(SG_WaterForestConnect(&forest, 1, 0, prove, &fixture) ==
		SG_WATER_CONNECT_RECORDED);
	CHECK(forest.edge_count == 2);
	CHECK(edges[0].from == 0 && edges[0].to == 3);
	CHECK(edges[1].from == 1 && edges[1].to == 0);
	return 0;
}

int main(void)
{
	CHECK(deterministic_spanning_forest() == 0);
	CHECK(directed_merge_requires_both_proofs() == 0);
	CHECK(overflow_is_atomic() == 0);
	CHECK(colliding_edge_slots_keep_identity() == 0);
	puts("sg_water_forest_test: ok");
	return 0;
}
