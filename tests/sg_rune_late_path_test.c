/* Focused tests for pure late-stage RUNE component bridge selection. */

#include <stdio.h>
#include <string.h>

#include "q_shared.h"
#include "slipgate/sg_rune_late_path.h"

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

typedef struct test_proposal_s
{
	int from;
	int to;
	qboolean eligible;
	byte action;
	uint32_t cost_ms;
} test_proposal_t;

typedef struct test_eligibility_s
{
	test_proposal_t *proposal;
	uint32_t count;
	uint32_t rejected_calls;
} test_eligibility_t;

static void SetLink(rune_link_t *link, int from, int to, int action,
	int cost_ms)
{
	memset(link, 0, sizeof(*link));
	link->from = from;
	link->to = to;
	link->action = (byte)action;
	link->cost_ms = (short)cost_ms;
}

static qboolean Eligible(void *callback_data,
	const sg_rune_late_graph_t *graph, int from, int to,
	sg_rune_late_proposal_t *result)
{
	test_eligibility_t *eligibility = callback_data;
	uint32_t i;

	(void)graph;
	for (i = 0; i < eligibility->count; i++)
	{
		test_proposal_t *proposal = &eligibility->proposal[i];

		if (proposal->from != from || proposal->to != to)
			continue;
		if (!proposal->eligible)
		{
			eligibility->rejected_calls++;
			return false;
		}
		result->action = proposal->action;
		result->cost_ms = proposal->cost_ms;
		return true;
	}
	return false;
}

static qboolean AllEligible(void *callback_data,
	const sg_rune_late_graph_t *graph, int from, int to,
	sg_rune_late_proposal_t *result)
{
	(void)callback_data;
	(void)graph;
	(void)from;
	(void)to;
	result->action = RL_RUN;
	result->cost_ms = 100;
	return true;
}

static sg_rune_late_graph_t Graph(rune_seed_t *seeds, uint32_t seed_count,
	rune_link_t *links, uint32_t link_count, int *regions,
	uint32_t region_count, int first_objective, int second_objective)
{
	sg_rune_late_graph_t graph;

	memset(&graph, 0, sizeof(graph));
	graph.seeds = seeds;
	graph.seed_count = seed_count;
	graph.links = links;
	graph.link_count = link_count;
	graph.regions = regions;
	graph.region_count = region_count;
	graph.objective[0] = first_objective;
	graph.objective[1] = second_objective;
	return graph;
}

static uint32_t PairCursor(uint32_t count, uint32_t from, uint32_t to)
{
	return from * (count - 1U) + (to < from ? to : to - 1U);
}

static void TestConnectedRoute(void)
{
	rune_seed_t seeds[4];
	rune_link_t links[4];
	int regions[4] = {0, 0, 0, 0};
	sg_rune_late_graph_t graph;
	sg_rune_late_candidate_t candidates[2];
	sg_rune_late_report_t report;
	test_eligibility_t eligibility = {0};

	memset(seeds, 0, sizeof(seeds));
	SetLink(&links[0], 0, 1, RL_RUN, 100);
	SetLink(&links[1], 1, 3, RL_RUN, 200);
	SetLink(&links[2], 3, 2, RL_RUN, 50);
	SetLink(&links[3], 2, 0, RL_RUN, 60);
	graph = Graph(seeds, 4, links, 4, regions, 1, 0, 3);
	CHECK(SG_RuneLatePathSelect(&graph, Eligible, &eligibility,
		candidates, 2, &report) == SG_RUNE_LATE_OK);
	CHECK(report.route[0].directed);
	CHECK(report.route[0].directed_cost_ms == 300);
	CHECK(report.route[1].directed);
	CHECK(report.route[1].directed_cost_ms == 110);
	CHECK(!report.route[0].weak_cut.available);
	CHECK(report.candidate_count == 0);
}

static void TestAbsentEdge(void)
{
	rune_seed_t seeds[2];
	int regions[2] = {0, 1};
	test_proposal_t proposal[] = {{0, 1, false, RL_RUN, 20}};
	test_eligibility_t eligibility = {proposal, 1, 0};
	sg_rune_late_graph_t graph = Graph(seeds, 2, NULL, 0,
		regions, 2, 0, 1);
	sg_rune_late_candidate_t candidate;
	sg_rune_late_report_t report;

	memset(seeds, 0, sizeof(seeds));
	CHECK(SG_RuneLatePathSelect(&graph, Eligible, &eligibility,
		&candidate, 1, &report) == SG_RUNE_LATE_OK);
	CHECK(!report.route[0].directed);
	CHECK(!report.route[0].weak_cut.available);
	CHECK(report.candidate_count == 0);
	CHECK(eligibility.rejected_calls == 1);
}

static void TestRejectedNearestAllowsLongerCandidate(void)
{
	rune_seed_t seeds[3];
	rune_link_t link;
	int regions[3] = {0, 0, 1};
	test_proposal_t proposals[] = {
		{0, 2, false, RL_RUN, 10},
		{1, 2, true, RL_RUN, 500}
	};
	test_eligibility_t eligibility = {proposals, 2, 0};
	sg_rune_late_graph_t graph;
	sg_rune_late_candidate_t candidate;
	sg_rune_late_report_t report;

	memset(seeds, 0, sizeof(seeds));
	SetLink(&link, 0, 1, RL_RUN, 100);
	graph = Graph(seeds, 3, &link, 1, regions, 2, 0, 2);
	CHECK(SG_RuneLatePathSelect(&graph, Eligible, &eligibility,
		&candidate, 1, &report) == SG_RUNE_LATE_OK);
	CHECK(eligibility.rejected_calls == 1);
	CHECK(report.candidate_count == 1);
	CHECK(candidate.from == 1 && candidate.to == 2);
	CHECK(candidate.objective_gain_mask == 1);
	CHECK(candidate.total_path_cost_ms == 600);
}

static void TestNeutralRegionsRemainEligible(void)
{
	rune_seed_t seeds[4];
	int regions[4] = {0, 1, 2, 3};
	test_proposal_t proposal[] = {{1, 2, true, RL_JUMP, 250}};
	test_eligibility_t eligibility = {proposal, 1, 0};
	sg_rune_late_graph_t graph = Graph(seeds, 4, NULL, 0,
		regions, 4, 0, 3);
	sg_rune_late_candidate_t candidate;
	sg_rune_late_report_t report;

	memset(seeds, 0, sizeof(seeds));
	graph.pair_cursor = PairCursor(4, 1, 2);
	CHECK(SG_RuneLatePathSelect(&graph, Eligible, &eligibility,
		&candidate, 1, &report) == SG_RUNE_LATE_OK);
	CHECK(report.candidate_count == 1);
	CHECK(candidate.from_region == 1 && candidate.to_region == 2);
	CHECK(candidate.objective_gain_mask == 0);
	CHECK(candidate.objective_touch_mask == 0);
}

static void TestNeutralFrontierIsNotStarved(void)
{
	rune_seed_t seeds[6];
	int regions[6] = {0, 0, 1, 2, 3, 4};
	test_proposal_t proposals[] = {
		{0, 5, true, RL_RUN, 10},
		{1, 5, true, RL_RUN, 20},
		{2, 3, true, RL_RUN, 30}
	};
	test_eligibility_t eligibility = {proposals, 3, 0};
	sg_rune_late_graph_t graph = Graph(seeds, 6, NULL, 0,
		regions, 5, 0, 5);
	sg_rune_late_candidate_t candidates[2];
	sg_rune_late_report_t report;
	qboolean saw_neutral = false;
	uint32_t examined = 0;

	memset(seeds, 0, sizeof(seeds));
	while (examined < 20 && !saw_neutral)
	{
		graph.pair_cursor = examined;
		CHECK(SG_RuneLatePathSelect(&graph, Eligible, &eligibility,
			candidates, 2, &report) == SG_RUNE_LATE_OK);
		for (uint32_t i = 0; i < report.candidate_count; i++)
			if (candidates[i].from_region == 1 &&
			    candidates[i].to_region == 2)
				saw_neutral = true;
		examined = report.next_pair_cursor;
	}
	CHECK(saw_neutral);
	CHECK(examined <= 6);
}

static void TestRoundRobinCrossesOldCapacity(void)
{
	rune_seed_t seeds[20];
	int regions[20];
	sg_rune_late_graph_t graph;
	sg_rune_late_candidate_t candidate;
	sg_rune_late_report_t report;
	uint32_t attempts = 0;
	qboolean saw_late_neutral = false;

	memset(seeds, 0, sizeof(seeds));
	for (int i = 0; i < 20; i++)
		regions[i] = i;
	graph = Graph(seeds, 20, NULL, 0, regions, 20, 0, 19);
	while (attempts < 380 && !saw_late_neutral)
	{
		CHECK(SG_RuneLatePathSelect(&graph, AllEligible, NULL,
			&candidate, 1, &report) == SG_RUNE_LATE_OK);
		CHECK(report.candidate_count == 1);
		attempts++;
		if (candidate.from_region == 18 && candidate.to_region == 17)
			saw_late_neutral = true;
		graph.pair_cursor = report.next_pair_cursor;
	}
	CHECK(saw_late_neutral);
	CHECK(attempts > 256 && attempts <= 380);
}

static void TestOneWayTeleportIsDiagnosticOnly(void)
{
	rune_seed_t seeds[2];
	rune_link_t link;
	int regions[2] = {0, 1};
	test_proposal_t proposal[] = {{0, 1, true, RL_RUN, 50}};
	test_eligibility_t eligibility = {proposal, 1, 0};
	sg_rune_late_graph_t graph;
	sg_rune_late_candidate_t candidate;
	sg_rune_late_report_t report;

	memset(seeds, 0, sizeof(seeds));
	SetLink(&link, 1, 0, RL_TELEPORT, 50);
	graph = Graph(seeds, 2, &link, 1, regions, 2, 0, 1);
	CHECK(SG_RuneLatePathSelect(&graph, Eligible, &eligibility,
		&candidate, 1, &report) == SG_RUNE_LATE_OK);
	CHECK(!report.route[0].directed);
	CHECK(report.route[1].directed);
	CHECK(report.route[0].weak_cut.available);
	CHECK(!report.route[0].weak_cut.reversible);
	CHECK(report.route[0].weak_cut.original_from == 1);
	CHECK(report.route[0].weak_cut.original_to == 0);
	CHECK(report.route[0].weak_cut.action == RL_TELEPORT);
	CHECK(report.candidate_count == 0);
}

static void TestParallelRegionEdgeIsSuppressed(void)
{
	rune_seed_t seeds[4];
	rune_link_t link;
	int regions[4] = {0, 0, 1, 1};
	test_proposal_t proposals[] = {
		{1, 3, true, RL_RUN, 50},
		{2, 0, true, RL_RUN, 60}
	};
	test_eligibility_t eligibility = {proposals, 2, 0};
	sg_rune_late_graph_t graph;
	sg_rune_late_candidate_t candidate;
	sg_rune_late_report_t report;

	memset(seeds, 0, sizeof(seeds));
	SetLink(&link, 0, 2, RL_RUN, 100);
	graph = Graph(seeds, 4, &link, 1, regions, 2, 0, 3);
	CHECK(SG_RuneLatePathSelect(&graph, Eligible, &eligibility,
		&candidate, 1, &report) == SG_RUNE_LATE_OK);
	CHECK(report.candidate_count == 0);
	graph.pair_cursor = PairCursor(2, 1, 0);
	CHECK(SG_RuneLatePathSelect(&graph, Eligible, &eligibility,
		&candidate, 1, &report) == SG_RUNE_LATE_OK);
	CHECK(report.candidate_count == 1);
	CHECK(candidate.from == 2 && candidate.to == 0);
}

static void TestAlternativeLimitAndOrder(void)
{
	rune_seed_t seeds[4];
	int regions[4] = {0, 1, 2, 3};
	test_proposal_t proposals[] = {
		{0, 1, true, RL_RUN, 300},
		{0, 2, true, RL_RUN, 100},
		{0, 3, true, RL_RUN, 200}
	};
	test_eligibility_t eligibility = {proposals, 3, 0};
	sg_rune_late_graph_t graph = Graph(seeds, 4, NULL, 0,
		regions, 4, -1, -1);
	sg_rune_late_candidate_t candidates[2];
	sg_rune_late_report_t report;

	memset(seeds, 0, sizeof(seeds));
	CHECK(SG_RuneLatePathSelect(&graph, Eligible, &eligibility,
		candidates, 2, &report) == SG_RUNE_LATE_OK);
	CHECK(report.candidate_count == 2);
	CHECK(candidates[0].to == 1 && candidates[0].cost_ms == 300);
	CHECK(candidates[1].to == 2 && candidates[1].cost_ms == 100);
}

static void TestIterativeRecompute(void)
{
	rune_seed_t seeds[3];
	rune_link_t links[2];
	int regions[3] = {0, 1, 2};
	test_proposal_t proposals[] = {
		{0, 1, true, RL_RUN, 100},
		{1, 2, true, RL_RUN, 200}
	};
	test_eligibility_t eligibility = {proposals, 2, 0};
	sg_rune_late_graph_t graph = Graph(seeds, 3, links, 0,
		regions, 3, 0, 2);
	sg_rune_late_candidate_t candidate;
	sg_rune_late_report_t report;
	int merges = 0;

	memset(seeds, 0, sizeof(seeds));
	CHECK(SG_RuneLatePathSelect(&graph, Eligible, &eligibility,
		&candidate, 1, &report) == SG_RUNE_LATE_OK);
	CHECK(candidate.from == 0 && candidate.to == 1);
	SetLink(&links[0], 0, 1, RL_RUN, 100);
	merges++;
	graph.link_count = 1;
	regions[1] = 0;
	regions[2] = 1;
	graph.region_count = 2;
	CHECK(SG_RuneLatePathSelect(&graph, Eligible, &eligibility,
		&candidate, 1, &report) == SG_RUNE_LATE_OK);
	CHECK(candidate.from == 1 && candidate.to == 2);
	SetLink(&links[1], 1, 2, RL_RUN, 200);
	merges++;
	graph.link_count = 2;
	regions[2] = 0;
	graph.region_count = 1;
	CHECK(SG_RuneLatePathSelect(&graph, Eligible, &eligibility,
		&candidate, 1, &report) == SG_RUNE_LATE_OK);
	CHECK(report.route[0].directed);
	CHECK(report.candidate_count == 0);
	CHECK(merges == 2);
}

static void TestRejectionLedger(void)
{
	int from[8];
	int to[8];
	sg_rune_late_rejections_t rejections;

	CHECK(!SG_RuneLateRejectionsInit(&rejections, from, to, 7, 3));
	CHECK(SG_RuneLateRejectionsInit(&rejections, from, to, 8, 3));
	CHECK(!SG_RuneLateRejectionsContains(&rejections, 1, 2));
	CHECK(SG_RuneLateRejectionsRecord(&rejections, 1, 2));
	CHECK(SG_RuneLateRejectionsContains(&rejections, 1, 2));
	CHECK(SG_RuneLateRejectionsRecord(&rejections, 1, 2));
	CHECK(rejections.count == 1U);
	CHECK(SG_RuneLateRejectionsRecord(&rejections, 2, 3));
	CHECK(SG_RuneLateRejectionsRecord(&rejections, 3, 4));
	CHECK(!SG_RuneLateRejectionsRecord(&rejections, 4, 5));
	CHECK(rejections.count == 3U);
	CHECK(strcmp(SG_RuneLateCompletionName(
	    SG_RUNE_LATE_COMPLETION_OPEN_EXHAUSTED), "open-exhausted") == 0);
	CHECK(strcmp(SG_RuneLateCompletionName(
	    SG_RUNE_LATE_COMPLETION_OPEN_BUDGET), "open-budget") == 0);
}

static void TestProductionBudgetRetainsAcceptedMerges(void)
{
	int link_mark = 10;
	int link_count = 12;

	CHECK(SG_RUNE_LATE_REJECTION_LIMIT == 1024U);
	CHECK(SG_RUNE_LATE_REJECTION_TABLE_SIZE == 2048U);
	CHECK(SG_RuneLateCompletionKeepsMerges(
	    SG_RUNE_LATE_COMPLETION_OPEN_BUDGET));
	if (!SG_RuneLateCompletionKeepsMerges(
	    SG_RUNE_LATE_COMPLETION_OPEN_BUDGET))
		link_count = link_mark;
	CHECK(link_count == 12);
	CHECK(!SG_RuneLateCompletionKeepsMerges(
	    SG_RUNE_LATE_COMPLETION_FATAL));
}

int main(void)
{
	TestConnectedRoute();
	TestAbsentEdge();
	TestRejectedNearestAllowsLongerCandidate();
	TestNeutralRegionsRemainEligible();
	TestNeutralFrontierIsNotStarved();
	TestRoundRobinCrossesOldCapacity();
	TestOneWayTeleportIsDiagnosticOnly();
	TestParallelRegionEdgeIsSuppressed();
	TestAlternativeLimitAndOrder();
	TestIterativeRecompute();
	TestRejectionLedger();
	TestProductionBudgetRetainsAcceptedMerges();

	if (failures)
	{
		fprintf(stderr, "sg_rune_late_path_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("sg_rune_late_path_test: ok");
	return 0;
}
