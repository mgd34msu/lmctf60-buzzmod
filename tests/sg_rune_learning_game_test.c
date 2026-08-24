#include <stdio.h>
#include <string.h>

#include "q_shared.h"
#include "slipgate/sg_rune_learning_game.h"

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

typedef struct fixture_s
{
	sg_rune_learning_graph_t graph;
	char order[32];
	int order_count;
	int reject_bite_x;
	int lie_on_hook;
} fixture_t;

static void Seed(rune_seed_t *seed, float x)
{
	memset(seed, 0, sizeof(*seed));
	seed->origin[0] = x;
}

static void Link(rune_link_t *link, int from, int to, rune_action_t action)
{
	memset(link, 0, sizeof(*link));
	link->from = from;
	link->to = to;
	link->action = (byte)action;
	link->provenance = RL_PROVEN;
	link->cost_ms = 100;
	link->heading_slack = action == RL_RUN ? 255 : RUNE_HOOK_CONTROL_SLACK;
	link->mechanism_plan = RUNE_NO_MECHANISM_PLAN;
	if (action == RL_HOOK || action == RL_CHAIN_HOOK)
	{
		link->anchor[PITCH] = SHORT2ANGLE((short)-1024);
		link->anchor[YAW] = SHORT2ANGLE((short)4096);
		link->anchor[ROLL] = 512.0f;
	}
	if (action == RL_CHAIN_HOOK)
	{
		link->mechanism_anchor[PITCH] = SHORT2ANGLE((short)-512);
		link->mechanism_anchor[YAW] = SHORT2ANGLE((short)8192);
		link->mechanism_anchor[ROLL] = 384.0f;
	}
}

static sg_rune_learning_owner_result_t RunOwner(void *opaque, int from,
	int to, const int32_t waypoint_q8[3], int has_waypoint)
{
	fixture_t *fixture = opaque;
	char phase = has_waypoint && waypoint_q8[0] == 88 ? 'R' : 'r';

	fixture->order[fixture->order_count++] = phase;
	Link(&fixture->graph.links[(*fixture->graph.link_count)++], from, to,
		RL_RUN);
	return SG_RUNE_LEARNING_OWNER_ADDED;
}

static sg_rune_learning_owner_result_t HookOwner(void *opaque, int from,
	int to, const sg_rune_learning_hook_request_t *request)
{
	fixture_t *fixture = opaque;
	rune_action_t action = request->rope_count == 1U ? RL_HOOK :
		RL_CHAIN_HOOK;

	fixture->order[fixture->order_count++] =
		request->kind == SG_RUNE_LEARNING_HOOK_EXACT_SOURCE_CONTROL ?
		'E' : 'D';
	if (request->kind == SG_RUNE_LEARNING_HOOK_DISCOVER_WORLD_BITES &&
	    request->bite_q8[0][0] == fixture->reject_bite_x)
		return SG_RUNE_LEARNING_OWNER_REJECTED;
	if (fixture->lie_on_hook)
		action = RL_RUN;
	Link(&fixture->graph.links[(*fixture->graph.link_count)++], from, to,
		action);
	return SG_RUNE_LEARNING_OWNER_ADDED;
}

static fixture_t Fixture(rune_seed_t seeds[6], rune_link_t links[16],
	int *link_count)
{
	fixture_t fixture;
	int index;

	memset(&fixture, 0, sizeof(fixture));
	for (index = 0; index < 6; index++)
		Seed(&seeds[index], (float)index * 128.0f);
	*link_count = 0;
	fixture.graph.seeds = seeds;
	fixture.graph.seed_count = 6U;
	fixture.graph.links = links;
	fixture.graph.link_count = link_count;
	fixture.graph.link_capacity = 16U;
	fixture.graph.objective[0] = 0;
	fixture.graph.objective[1] = 5;
	return fixture;
}

static void RunCandidate(sg_rune_learning_candidate_t *candidate,
	int from, int to, int waypoint_q8)
{
	memset(candidate, 0, sizeof(*candidate));
	candidate->from_origin_q8[0] = from * 1024;
	candidate->to_origin_q8[0] = to * 1024;
	candidate->waypoint_q8[0] = waypoint_q8;
	candidate->has_waypoint = waypoint_q8 != 0;
	candidate->hint = SG_RUNE_LEARNING_DRY_RUN_WAYPOINT;
}

static void HookCandidate(sg_rune_learning_hook_candidate_t *candidate,
	int from, int to, int bite_x, int aim_yaw)
{
	memset(candidate, 0, sizeof(*candidate));
	candidate->from_origin_q8[0] = from * 1024;
	candidate->to_origin_q8[0] = to * 1024;
	candidate->rope_count = 1U;
	candidate->aim_short[0][1] = (int16_t)aim_yaw;
	candidate->bite_q8[0][0] = bite_x;
}

static void Source(rune_t *source, rune_seed_t seeds[6],
	rune_link_t links[2])
{
	memset(source, 0, sizeof(*source));
	source->artifact.route_contract = RUNE_ROUTE_CONTRACT_LOCAL_ONLY;
	source->artifact.num_seeds = 6U;
	source->artifact.num_links = 2U;
	source->hdr.num_seeds = 6;
	source->hdr.num_links = 2;
	source->seeds = seeds;
	source->links = links;
	Link(&links[0], 0, 1, RL_RUN);
	links[0].anchor[0] = 11.0f;
	Link(&links[1], 1, 2, RL_HOOK);
}

static void TestTypedOrderAndRejectedVariant(void)
{
	rune_seed_t seeds[6];
	rune_link_t links[16], source_links[2];
	rune_t source;
	int link_count;
	fixture_t fixture = Fixture(seeds, links, &link_count);
	sg_rune_learning_candidate_t run;
	sg_rune_learning_hook_candidate_t hooks[2];
	sg_rune_learning_evidence_t evidence;
	sg_rune_learning_owners_t owners;
	sg_rune_learning_update_report_t report;

	Source(&source, seeds, source_links);
	RunCandidate(&run, 2, 3, 176);
	HookCandidate(&hooks[0], 3, 4, 111, 4096);
	HookCandidate(&hooks[1], 3, 4, 222, 8192);
	memset(&evidence, 0, sizeof(evidence));
	evidence.candidate_count = 1U;
	evidence.candidates = &run;
	evidence.hook_candidate_count = 2U;
	evidence.hook_candidates = hooks;
	owners.run = RunOwner;
	owners.hook = HookOwner;
	owners.context = &fixture;
	fixture.reject_bite_x = 111;
	CHECK(SG_RuneLearningGameUpdate(&source, &evidence, &fixture.graph,
		&owners, &report) == SG_RUNE_LEARNING_OPEN_IMPROVED);
	CHECK(strcmp(fixture.order, "RErDD") == 0);
	CHECK(link_count == 4);
	CHECK(links[0].action == RL_RUN && links[1].action == RL_HOOK);
	CHECK(links[2].action == RL_RUN && links[3].action == RL_HOOK);
	CHECK(report.source_runs.accepted_count == 1U);
	CHECK(report.source_hooks.accepted_count == 1U);
	CHECK(report.new_runs.accepted_count == 1U);
	CHECK(report.new_hooks.proof_calls == 2U);
	CHECK(report.new_hooks.accepted_count == 1U);
	CHECK(report.new_hooks.closure_checks == 1U);
}

static void TestHookOwnerContractIsStrict(void)
{
	rune_seed_t seeds[6];
	rune_link_t links[16];
	rune_t source;
	int link_count;
	fixture_t fixture = Fixture(seeds, links, &link_count);
	sg_rune_learning_hook_candidate_t hook;
	sg_rune_learning_evidence_t evidence;
	sg_rune_learning_owners_t owners;
	sg_rune_learning_update_report_t report;

	memset(&source, 0, sizeof(source));
	source.artifact.route_contract = RUNE_ROUTE_CONTRACT_LOCAL_ONLY;
	source.artifact.num_seeds = 6U;
	source.hdr.num_seeds = 6;
	source.seeds = seeds;
	HookCandidate(&hook, 1, 2, 333, 4096);
	memset(&evidence, 0, sizeof(evidence));
	evidence.hook_candidate_count = 1U;
	evidence.hook_candidates = &hook;
	owners.run = RunOwner;
	owners.hook = HookOwner;
	owners.context = &fixture;
	fixture.lie_on_hook = 1;
	CHECK(SG_RuneLearningGameUpdate(&source, &evidence, &fixture.graph,
		&owners, &report) == SG_RUNE_LEARNING_FATAL);
	CHECK(report.new_hooks.proof_calls == 1U);
}

int main(void)
{
	TestTypedOrderAndRejectedVariant();
	TestHookOwnerContractIsStrict();
	if (failures)
	{
		fprintf(stderr, "sg_rune_learning_game_test: %d failure(s)\n",
			failures);
		return 1;
	}
	puts("sg_rune_learning_game_test: ok");
	return 0;
}
