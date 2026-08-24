#include "q_shared.h"
#include "slipgate/sg_relay_wall_objective.h"

#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
		failures++; \
	} \
} while (0)

typedef struct fixture_s
{
	int eligible_calls;
	int prove_calls;
	int reject_all;
	int publish_calls;
	uint32_t published_source;
	uint32_t published_destination;
} fixture_t;

int SG_RelayWallPlanDiscover(const sg_mech_catalog_view_t *catalog,
	uint32_t entry_key, sg_relay_wall_plan_witness_t *witness)
{
	(void)catalog;
	(void)entry_key;
	(void)witness;
	return 0;
}

static int Eligible(void *context, uint32_t seed, int source)
{
	fixture_t *fixture = context;

	(void)source;
	fixture->eligible_calls++;
	return seed < 4U;
}

static int Discover(void *context, const sg_mech_catalog_view_t *catalog,
	uint32_t entry_key, sg_relay_wall_plan_witness_t *witness)
{
	(void)context;
	(void)catalog;
	if (entry_key != 10U)
		return 0;
	memset(witness, 0, sizeof(*witness));
	witness->entry_key = 10U;
	witness->wall_key = 20U;
	witness->immediate_relay_key = 30U;
	witness->restore_relay_key = 40U;
	witness->touch_hold_ms = 200U;
	witness->cooldown_ms = 4000U;
	witness->active_window_ms = 4000U;
	witness->restore_ms = 4000U;
	return 1;
}

static int Prove(void *raw, const sg_relay_wall_plan_witness_t *witness,
	uint32_t source, uint32_t destination,
	sg_relay_wall_objective_proof_t *proof)
{
	fixture_t *fixture = raw;

	fixture->prove_calls++;
	CHECK(witness->touch_hold_ms == 200U);
	CHECK(witness->active_window_ms == 4000U);
	if (fixture->reject_all || source != 1U || destination != 2U)
		return 0;
	proof->anchor[0] = 25.0f;
	proof->cost_ms = 725U;
	proof->egress_ms = 500U;
	proof->sweep_clear_ms = 500U;
	return 1;
}

static int Publish(void *raw, const sg_relay_wall_plan_witness_t *witness,
	uint32_t source, uint32_t destination,
	const sg_relay_wall_objective_proof_t *proof)
{
	fixture_t *fixture = raw;

	CHECK(witness->restore_ms == 4000U);
	CHECK(proof->cost_ms == 725U && proof->egress_ms == 500U);
	fixture->publish_calls++;
	fixture->published_source = source;
	fixture->published_destination = destination;
	return 1;
}

int main(void)
{
	rune_mechanism_node_t nodes[2];
	rune_seed_t seeds[4];
	int components[4] = { 0, 0, 1, 1 };
	uint8_t masks[4] = { 1U, 1U, 2U, 2U };
	sg_mech_catalog_view_t catalog;
	sg_relay_wall_objective_request_t request;
	sg_relay_wall_objective_report_t report;
	fixture_t fixture;

	memset(nodes, 0, sizeof(nodes));
	memset(seeds, 0, sizeof(seeds));
	memset(&catalog, 0, sizeof(catalog));
	memset(&request, 0, sizeof(request));
	memset(&fixture, 0, sizeof(fixture));
	nodes[0].key = 10U;
	nodes[0].kind = SG_MECH_NODE_BUTTON;
	nodes[0].absmin_q8[0] = -8;
	nodes[0].absmax_q8[0] = 8;
	nodes[1].key = 20U;
	nodes[1].kind = SG_MECH_NODE_TOGGLE_WALL;
	nodes[1].absmin_q8[0] = 720;
	nodes[1].absmax_q8[0] = 2480;
	{
		float mins[3];
		float maxs[3];

		nodes[0].absmin_q8[2] = -5016;
		nodes[0].absmax_q8[2] = -4872;
		CHECK(SG_RelayWallNodeBounds(&nodes[0], mins, maxs));
		CHECK(mins[2] == -627.0f && maxs[2] == -609.0f);
		CHECK(SG_RelayWallSourceContactElevation(&nodes[0], -659.0f));
		CHECK(SG_RelayWallSourceContactElevation(&nodes[0], -585.0f));
		CHECK(!SG_RelayWallSourceContactElevation(&nodes[0], -584.0f));
		CHECK(!SG_RelayWallSourceContactElevation(&nodes[0], -660.0f));
		nodes[0].absmin_q8[2] = 0;
		nodes[0].absmax_q8[2] = 0;
	}
	seeds[0].origin[0] = -200.0f;
	seeds[1].origin[0] = 0.0f;
	seeds[2].origin[0] = 100.0f;
	seeds[3].origin[0] = 300.0f;
	catalog.nodes = nodes;
	catalog.num_nodes = 2U;
	request.catalog = &catalog;
	request.seeds = seeds;
	request.seed_count = 4U;
	request.components = components;
	request.objective_masks = masks;
	request.context = &fixture;
	request.eligible = Eligible;
	request.discover = Discover;
	request.prove = Prove;
	request.publish = Publish;
	CHECK(SG_RelayWallObjectiveBridge(&request, &report) == 1);
	CHECK(report.mechanisms == 1U && report.published == 1U);
	CHECK(fixture.eligible_calls <= 8);
	CHECK(fixture.prove_calls == 1);
	CHECK(fixture.publish_calls == 1);
	CHECK(fixture.published_source == 1U &&
	    fixture.published_destination == 2U);
	memset(&fixture, 0, sizeof(fixture));
	fixture.reject_all = 1;
	CHECK(SG_RelayWallObjectiveBridge(&request, &report) == 0);
	CHECK(fixture.eligible_calls <= 8 && fixture.prove_calls <= 32);
	CHECK(fixture.publish_calls == 0);
	if (failures)
		return 1;
	puts("sg_relay_wall_objective_test: PASS");
	return 0;
}
