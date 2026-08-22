/* Regression tests for the active nominal-gravity scope boundary. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "slipgate/sg_rune_proof.h"

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		        __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

int main(void)
{
	sg_rune_proof_hook_seed_t seeds[40];
	sg_rune_proof_hook_candidate_t first[32], second[32], bounded[5];
	sg_rune_proof_hook_candidate_t component_bounded[256];
	sg_rune_proof_hook_frontier_t frontier;
	uint16_t component_trials[3];
	uint16_t source_trials[40];
	size_t source_cursor[40];
	size_t component_source_cursor[3];
	size_t first_count, second_count, bounded_count;
	int i;

	/* lmctf54's lower approach -> shelf samples rise 112 and 124 units.
	 * Admit the complete local seed tier, but not a wider or taller general
	 * hook traversal; the generator's physical prover remains authoritative. */
	CHECK(SG_RuneProofHookLateralWindow(32.0f, 112.0f));
	CHECK(SG_RuneProofHookLateralWindow(96.0f, 124.0f));
	CHECK(SG_RuneProofHookLateralWindow(128.0f, 128.0f));
	CHECK(!SG_RuneProofHookLateralWindow(128.125f, 112.0f));
	CHECK(!SG_RuneProofHookLateralWindow(32.0f, 128.125f));
	CHECK(!SG_RuneProofHookLateralWindow(32.0f, 31.875f));
	CHECK(!SG_RuneProofHookLateralWindow(NAN, 112.0f));
	CHECK(!SG_RuneProofHookLateralWindow(32.0f, NAN));

	memset(seeds, 0, sizeof(seeds));
	for (i = 0; i < 40; i++)
	{
		seeds[i].origin_q8[0] = (i % 10) * 64 * 8;
		seeds[i].origin_q8[1] = (i / 10) * 64 * 8;
		seeds[i].origin_q8[2] = (i >= 20) ? 64 * 8 : 0;
		seeds[i].component = i < 20 ? 0 : (i < 30 ? 1 : 2);
		seeds[i].objective_mask = i < 20 ? 1 : 3;
		seeds[i].stable = 1;
	}
	memset(&frontier, 0, sizeof(frontier));
	frontier.seeds = seeds;
	frontier.seed_count = 40;
	frontier.component_count = 3;
	frontier.global_limit = SG_RUNE_PROOF_HOOK_FRONTIER_MAX;
	frontier.component_limit = SG_RUNE_PROOF_HOOK_FRONTIER_MAX;
	frontier.source_limit = SG_RUNE_PROOF_HOOK_FRONTIER_MAX;
	frontier.component_trials = component_trials;
	frontier.source_trials = source_trials;
	frontier.source_cursor = source_cursor;
	frontier.component_source_cursor = component_source_cursor;
	frontier.output = first;
	frontier.output_capacity = 32;
	first_count = SG_RuneProofSelectHookFrontier(&frontier);
	CHECK(first_count == 32);
	CHECK(first[0].from == 0 && first[0].to == 20 && first[0].rank == 0);
	for (i = 0; i < 40; i++)
		CHECK(source_trials[i] <= SG_RUNE_PROOF_HOOK_FRONTIER_MAX);
	for (i = 0; i < 3; i++)
		CHECK(component_trials[i] <= SG_RUNE_PROOF_HOOK_FRONTIER_MAX);
	frontier.output = second;
	second_count = SG_RuneProofSelectHookFrontier(&frontier);
	CHECK(second_count == first_count);
	CHECK(memcmp(first, second, first_count * sizeof(*first)) == 0);
	frontier.output = bounded;
	frontier.output_capacity = 5;
	bounded_count = SG_RuneProofSelectHookFrontier(&frontier);
	CHECK(bounded_count == 5);
	CHECK(memcmp(first, bounded, sizeof(bounded)) == 0);
	{
		sg_rune_proof_hook_seed_t fair_seeds[6];
		sg_rune_proof_hook_candidate_t fair_output[4];
		uint16_t fair_component_trials[2];
		uint16_t fair_source_trials[6];
		size_t fair_source_cursor[6], fair_component_cursor[2];
		sg_rune_proof_hook_frontier_t fair;

		memset(fair_seeds, 0, sizeof(fair_seeds));
		for (i = 0; i < 6; i++)
		{
			fair_seeds[i].origin_q8[0] = i * 32 * 8;
			fair_seeds[i].component = i < 3 ? 0 : 1;
			fair_seeds[i].objective_mask = 3;
			fair_seeds[i].stable = 1;
		}
		memset(&fair, 0, sizeof(fair));
		fair.seeds = fair_seeds;
		fair.seed_count = 6;
		fair.component_count = 2;
		fair.global_limit = 4;
		fair.component_limit = 4;
		fair.source_limit = 4;
		fair.component_trials = fair_component_trials;
		fair.source_trials = fair_source_trials;
		fair.source_cursor = fair_source_cursor;
		fair.component_source_cursor = fair_component_cursor;
		fair.output = fair_output;
		fair.output_capacity = 4;
		CHECK(SG_RuneProofSelectHookFrontier(&fair) == 4);
		CHECK(fair_component_trials[0] == 2);
		CHECK(fair_component_trials[1] == 2);
	}
	for (i = 0; i < 40; i++)
	{
		seeds[i].component = 0;
		seeds[i].objective_mask = 3;
	}
	frontier.component_count = 1;
	frontier.component_limit = 128;
	frontier.output = component_bounded;
	frontier.output_capacity = 256;
	CHECK(SG_RuneProofSelectHookFrontier(&frontier) ==
	      128);
	CHECK(component_trials[0] == 128);
	frontier.component_limit = SG_RUNE_PROOF_HOOK_FRONTIER_MAX;
	seeds[0].stable = 0;
	seeds[1].stable = 0;
	frontier.seed_count = 2;
	frontier.output_capacity = 1;
	CHECK(SG_RuneProofSelectHookFrontier(&frontier) == 0);
	seeds[0].water = 1;
	seeds[0].waterlevel = 2;
	seeds[1].origin_q8[2] = 160 * 8;
	CHECK(SG_RuneProofSelectHookFrontier(&frontier) == 1);
	seeds[1].water = 1;
	CHECK(SG_RuneProofSelectHookFrontier(&frontier) == 0);

	{
		float off = 0.0f;
		float on = 1.0f;
		float invalid = NAN;

		CHECK(SG_RuneFunkyGravityCompatible(&off));
		CHECK(!SG_RuneFunkyGravityCompatible(&on));
		CHECK(!SG_RuneFunkyGravityCompatible(&invalid));
		CHECK(!SG_RuneFunkyGravityCompatible(NULL));
	}

	SG_RuneProofScopeEnd();
	CHECK(!SG_RuneProofScopeActive());
	CHECK(SG_RuneProofGravity() == 800);

	CHECK(!SG_RuneProofScopeBegin(650.5f));
	CHECK(!SG_RuneProofScopeActive());
	CHECK(SG_RuneProofGravity() == 800);

	CHECK(SG_RuneProofScopeBegin(650.0f));
	CHECK(SG_RuneProofScopeActive());
	CHECK(SG_RuneProofGravity() == 650);
	CHECK(!SG_RuneProofScopeBegin(800.0f));
	CHECK(SG_RuneProofGravity() == 650);

	/* Simulate every post-begin failure funnel: End is idempotent and the next
	 * invocation starts from the nominal default, never the prior map's law. */
	SG_RuneProofScopeEnd();
	SG_RuneProofScopeEnd();
	CHECK(!SG_RuneProofScopeActive());
	CHECK(SG_RuneProofGravity() == 800);
	CHECK(SG_RuneProofScopeBegin(800.0f));
	CHECK(SG_RuneProofGravity() == 800);
	SG_RuneProofScopeEnd();
	CHECK(!SG_RuneProofScopeActive());
	CHECK(SG_RuneProofGravity() == 800);

	if (failures)
	{
		fprintf(stderr, "sg_rune_proof_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("sg_rune_proof_test: ok");
	return 0;
}
