/* sg_compound_gen_test.c -- pure topology planner contract. */
#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "../q_shared.h"
#include "../slipgate/sg_compound_gen.h"

static int failures;

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		        __FILE__, __LINE__, #condition); \
		failures++; \
	} \
} while (0)

typedef struct proof_context_s
{
	int calls;
	int reject_destination;
	int invalid_destination;
} proof_context_t;

static rune_reject_reason_t Prove(void *opaque,
	const sg_compound_gen_candidate_t *candidate,
	sg_compound_gen_proof_t *proof)
{
	proof_context_t *context = (proof_context_t *)opaque;

	context->calls++;
	if (candidate->destination == context->reject_destination)
		return RLR_SUFFIX_REPLAY_FAILED;
	proof->touch_ms = 25;
	proof->touch_frame_end_ms = 100;
	proof->mover_top_ms = 500;
	proof->suffix_start_ms = 400;
	proof->total_cost_ms = 600 + candidate->destination * 100;
	proof->arrival_ms = 100 + candidate->destination * 100;
	proof->sweep_clear_ms = 200;
	proof->exit_speed = (uint8_t)(10 + candidate->destination);
	if (candidate->destination == context->invalid_destination)
		proof->sweep_clear_ms = proof->arrival_ms + 100;
	return RLR_OK;
}

static rune_reject_reason_t ProveImpossible(void *opaque,
	const sg_compound_gen_candidate_t *candidate,
	sg_compound_gen_proof_t *proof)
{
	proof_context_t *context = (proof_context_t *)opaque;

	(void)candidate;
	context->calls++;
	memset(proof, 0, sizeof(*proof));
	proof->touch_ms = 25;
	proof->touch_frame_end_ms = 100;
	proof->mover_top_ms = 200;
	proof->suffix_start_ms = 100;
	proof->arrival_ms = 100;
	proof->sweep_clear_ms = 100;
	proof->total_cost_ms = 100; /* exact composition would be 300 */
	return RLR_OK;
}

static rune_reject_reason_t ProveFixed(void *opaque,
	const sg_compound_gen_candidate_t *candidate,
	sg_compound_gen_proof_t *proof)
{
	proof_context_t *context = (proof_context_t *)opaque;

	(void)candidate;
	context->calls++;
	memset(proof, 0, sizeof(*proof));
	proof->touch_ms = 25;
	proof->touch_frame_end_ms = 100;
	proof->mover_top_ms = 500;
	proof->suffix_start_ms = 400;
	proof->total_cost_ms = 600;
	proof->arrival_ms = 100;
	proof->sweep_clear_ms = 100;
	proof->exit_speed = 10;
	return RLR_OK;
}

static rune_reject_reason_t ProveOverflow(void *opaque,
	const sg_compound_gen_candidate_t *candidate,
	sg_compound_gen_proof_t *proof)
{
	proof_context_t *context = (proof_context_t *)opaque;

	(void)candidate;
	context->calls++;
	memset(proof, 0, sizeof(*proof));
	proof->touch_ms = INT_MAX - 22; /* positive and 25 ms aligned */
	proof->touch_frame_end_ms = 100;
	proof->mover_top_ms = 200;
	proof->suffix_start_ms = 100;
	proof->arrival_ms = 100;
	proof->sweep_clear_ms = 100;
	proof->total_cost_ms = 300;
	return RLR_OK;
}

static void Seed(sg_compound_gen_seed_t *seed, int component,
	unsigned int mask, int water, int incoming, int outgoing)
{
	memset(seed, 0, sizeof(*seed));
	seed->component = component;
	seed->objective_mask = (uint8_t)mask;
	seed->water = (uint8_t)water;
	seed->has_incoming = (uint8_t)incoming;
	seed->has_outgoing = (uint8_t)outgoing;
}

static void Candidate(sg_compound_gen_candidate_t *candidate,
	int source, int destination, int trigger, int mover, uint32_t rank,
	float x)
{
	memset(candidate, 0, sizeof(*candidate));
	candidate->source = source;
	candidate->destination = destination;
	candidate->trigger_key = trigger;
	candidate->mover_key = mover;
	candidate->local_rank = rank;
	candidate->mechanism_anchor[0] = x;
	candidate->mechanism_anchor[1] = 20.0f;
	candidate->mechanism_anchor[2] = 30.0f;
}

static sg_compound_gen_request_t Request(
	const sg_compound_gen_seed_t *seeds, size_t seed_count,
	const sg_compound_gen_candidate_t *candidates, size_t candidate_count,
	rune_link_t *output, size_t output_capacity, proof_context_t *context)
{
	sg_compound_gen_request_t request;

	memset(&request, 0, sizeof(request));
	request.seeds = seeds;
	request.seed_count = seed_count;
	request.candidates = candidates;
	request.candidate_count = candidate_count;
	request.output = output;
	request.output_capacity = output_capacity;
	request.prove = Prove;
	request.context = context;
	request.production_enabled = 1;
	return request;
}

static int LinkCanonical(const rune_link_t *link, int from, int to)
{
	static const float zero[3] = { 0.0f, 0.0f, 0.0f };

	return link->from == from && link->to == to &&
	       link->action == RL_DOOR_SWIM &&
	       link->provenance == RL_CONTRACTED &&
	       link->min_speed == 0 && link->heading == 0 &&
	       link->heading_slack == 0 &&
	       link->mechanism_plan == RUNE_NO_MECHANISM_PLAN &&
	       memcmp(link->anchor, zero, sizeof(zero)) == 0 &&
	       link->mechanism_anchor[0] == 10.0f &&
	       link->mechanism_anchor[1] == 20.0f &&
	       link->mechanism_anchor[2] == 30.0f &&
	       link->sweep_clear_ms == 200 && link->mode == RLCM_PREOPEN;
}

static void TestDisabledIsInert(void)
{
	sg_compound_gen_request_t request;
	proof_context_t context;
	rune_link_t output;
	rune_link_t before;
	sg_compound_gen_result_t result;

	memset(&request, 0xa5, sizeof(request));
	memset(&context, 0, sizeof(context));
	memset(&output, 0x5a, sizeof(output));
	before = output;
	request.production_enabled = 0;
	request.output = &output;
	request.output_capacity = 1;
	request.prove = Prove;
	request.context = &context;
	result = SG_CompoundGenPlan(&request);
	CHECK(result.status == SG_COMPOUND_GEN_DISABLED);
	CHECK(result.proof_calls == 0 && result.emitted == 0);
	CHECK(context.calls == 0);
	CHECK(memcmp(&output, &before, sizeof(output)) == 0);
	CHECK(SG_CompoundGenPlan(NULL).status == SG_COMPOUND_GEN_DISABLED);
}

static void TestTopologyAndProofFallback(void)
{
	sg_compound_gen_seed_t seeds[5];
	sg_compound_gen_candidate_t candidates[4];
	proof_context_t context;
	rune_link_t output[4];
	sg_compound_gen_request_t request;
	sg_compound_gen_result_t result;

	Seed(&seeds[0], 0, 0, 1, 1, 1);
	Seed(&seeds[1], 1, 0, 1, 1, 1); /* cheapest cross, rejected */
	Seed(&seeds[2], 0, 1, 1, 1, 1); /* missing red */
	Seed(&seeds[3], 0, 2, 0, 1, 1); /* missing blue */
	Seed(&seeds[4], 2, 0, 1, 1, 1); /* cross fallback */
	Candidate(&candidates[0], 0, 3, 40, 41, 30, 10.0f);
	Candidate(&candidates[1], 0, 1, 40, 41, 10, 10.0f);
	Candidate(&candidates[2], 0, 4, 40, 41, 11, 10.0f);
	Candidate(&candidates[3], 0, 2, 40, 41, 20, 10.0f);
	memset(&context, 0, sizeof(context));
	context.reject_destination = 1;
	context.invalid_destination = -1;
	memset(output, 0, sizeof(output));
	request = Request(seeds, 5, candidates, 4, output, 4, &context);
	result = SG_CompoundGenPlan(&request);

	CHECK(result.status == SG_COMPOUND_GEN_OK);
	CHECK(result.proof_calls == 4 && context.calls == 4);
	CHECK(result.selected == 3 && result.emitted == 3);
	CHECK(LinkCanonical(&output[0], 0, 2));
	CHECK(LinkCanonical(&output[1], 0, 3));
	CHECK(LinkCanonical(&output[2], 0, 4));
	CHECK(output[0].cost_ms == 800 && output[0].exit_speed == 12);
	CHECK(output[1].cost_ms == 900 && output[1].exit_speed == 13);
	CHECK(output[2].cost_ms == 1000 && output[2].exit_speed == 14);
}

static void TestLocalShortcutAndNoProof(void)
{
	sg_compound_gen_seed_t seeds[2];
	sg_compound_gen_candidate_t candidate;
	proof_context_t context;
	rune_link_t output;
	sg_compound_gen_request_t request;
	sg_compound_gen_result_t result;

	Seed(&seeds[0], 0, 3, 1, 1, 1);
	Seed(&seeds[1], 0, 3, 1, 1, 1);
	Candidate(&candidate, 0, 1, 20, 21, 1, 10.0f);
	memset(&context, 0, sizeof(context));
	context.reject_destination = -1;
	context.invalid_destination = -1;
	request = Request(seeds, 2, &candidate, 1, &output, 1, &context);
	result = SG_CompoundGenPlan(&request);
	CHECK(result.status == SG_COMPOUND_GEN_OK);
	CHECK(result.proof_calls == 1 && context.calls == 1);
	CHECK(result.selected == 1 && result.emitted == 1);
	CHECK(LinkCanonical(&output, 0, 1));

	seeds[1].component = 1;
	context.reject_destination = 1;
	result = SG_CompoundGenPlan(&request);
	CHECK(result.status == SG_COMPOUND_GEN_NO_PROOF);
	CHECK(result.proof_calls == 1 && result.emitted == 0);
}

static void TestMalformedAndAtomicFailures(void)
{
	sg_compound_gen_seed_t seeds[2];
	sg_compound_gen_candidate_t candidates[2];
	proof_context_t context;
	rune_link_t output[2];
	rune_link_t before[2];
	sg_compound_gen_request_t request;
	sg_compound_gen_result_t result;

	Seed(&seeds[0], 0, 0, 1, 1, 1);
	Seed(&seeds[1], 1, 3, 1, 1, 1);
	Candidate(&candidates[0], 0, 1, 20, 21, 1, 10.0f);
	candidates[1] = candidates[0];
	memset(&context, 0, sizeof(context));
	context.reject_destination = -1;
	context.invalid_destination = -1;
	memset(output, 0x5a, sizeof(output));
	memcpy(before, output, sizeof(output));
	request = Request(seeds, 2, candidates, 2, output, 2, &context);
	result = SG_CompoundGenPlan(&request);
	CHECK(result.status == SG_COMPOUND_GEN_DUPLICATE);
	CHECK(context.calls == 0);
	CHECK(memcmp(output, before, sizeof(output)) == 0);

	request.candidate_count = 1;
	context.invalid_destination = 1;
	result = SG_CompoundGenPlan(&request);
	CHECK(result.status == SG_COMPOUND_GEN_BAD_PROOF);
	CHECK(result.emitted == 0);
	CHECK(memcmp(output, before, sizeof(output)) == 0);
	request.prove = ProveImpossible;
	result = SG_CompoundGenPlan(&request);
	CHECK(result.status == SG_COMPOUND_GEN_BAD_PROOF);
	CHECK(result.emitted == 0);
	CHECK(memcmp(output, before, sizeof(output)) == 0);
	request.prove = ProveOverflow;
	result = SG_CompoundGenPlan(&request);
	CHECK(result.status == SG_COMPOUND_GEN_BAD_PROOF);
	CHECK(result.emitted == 0);
	CHECK(memcmp(output, before, sizeof(output)) == 0);
	request.prove = Prove;

	context.invalid_destination = -1;
	request.output_capacity = 0;
	request.output = NULL;
	result = SG_CompoundGenPlan(&request);
	CHECK(result.status == SG_COMPOUND_GEN_CAPACITY);
	CHECK(result.emitted == 0);
	CHECK(memcmp(output, before, sizeof(output)) == 0);

	request.output = output;
	request.output_capacity = 2;
	candidates[0].mechanism_anchor[0] = -0.0f;
	result = SG_CompoundGenPlan(&request);
	CHECK(result.status == SG_COMPOUND_GEN_INVALID);
	candidates[0].mechanism_anchor[0] = 10.0f;
	candidates[0].destination = 0;
	result = SG_CompoundGenPlan(&request);
	CHECK(result.status == SG_COMPOUND_GEN_INVALID);
	candidates[0].destination = 1;
	seeds[0].water = 2;
	result = SG_CompoundGenPlan(&request);
	CHECK(result.status == SG_COMPOUND_GEN_INVALID);
	seeds[0].water = 1;
}

static void TestDeterminismAndCompetingMechanisms(void)
{
	sg_compound_gen_seed_t seeds[3];
	sg_compound_gen_candidate_t forward[2], reversed[2];
	proof_context_t first_context, second_context;
	rune_link_t first_output[2], second_output[2];
	sg_compound_gen_request_t request;
	sg_compound_gen_result_t first, second;

	Seed(&seeds[0], 0, 0, 1, 1, 1);
	Seed(&seeds[1], 1, 3, 1, 1, 1);
	Seed(&seeds[2], 2, 0, 1, 1, 1);
	Candidate(&forward[0], 0, 1, 30, 31, 8, 11.0f);
	Candidate(&forward[1], 0, 1, 20, 21, 8, 10.0f);
	reversed[0] = forward[1];
	reversed[1] = forward[0];
	memset(&first_context, 0, sizeof(first_context));
	memset(&second_context, 0, sizeof(second_context));
	first_context.reject_destination = second_context.reject_destination = -1;
	first_context.invalid_destination = second_context.invalid_destination = -1;
	request = Request(seeds, 3, forward, 2, first_output, 2,
	                  &first_context);
	first = SG_CompoundGenPlan(&request);
	request.candidates = reversed;
	request.output = second_output;
	request.context = &second_context;
	second = SG_CompoundGenPlan(&request);
	CHECK(first.status == SG_COMPOUND_GEN_OK);
	CHECK(second.status == SG_COMPOUND_GEN_OK);
	CHECK(first.emitted == 1 && second.emitted == 1);
	CHECK(memcmp(first_output, second_output, sizeof(rune_link_t)) == 0);
	/* Equal proof/rank chooses the lower trigger/mover/anchor tuple. */
	CHECK(first_output[0].mechanism_anchor[0] == 10.0f);
}

static void TestCandidatesBeyondLegacyCapAreExhausted(void)
{
	sg_compound_gen_seed_t seeds[258];
	sg_compound_gen_candidate_t candidates[257];
	proof_context_t context;
	rune_link_t output[4];
	sg_compound_gen_request_t request;
	sg_compound_gen_result_t result;
	size_t index;

	Seed(&seeds[0], 0, 0, 1, 1, 1);
	for (index = 1U; index < 258U; index++)
	{
		Seed(&seeds[index], 0, 0, 1, 1, 1);
		Candidate(&candidates[index - 1U], 0, (int)index, 1, 2,
		          (uint32_t)index, 10.0f);
	}
	memset(&context, 0, sizeof(context));
	request = Request(seeds, 258U, candidates, 257U, output, 4U, &context);
	request.prove = ProveFixed;
	result = SG_CompoundGenPlan(&request);
	CHECK(result.status == SG_COMPOUND_GEN_OK);
	CHECK(result.proof_calls == 257U && context.calls == 257);
	CHECK(result.emitted == 1U && output[0].to == 1);
}

int main(void)
{
	TestDisabledIsInert();
	TestTopologyAndProofFallback();
	TestLocalShortcutAndNoProof();
	TestMalformedAndAtomicFailures();
	TestDeterminismAndCompetingMechanisms();
	TestCandidatesBeyondLegacyCapAreExhausted();
	if (failures)
	{
		fprintf(stderr, "sg_compound_gen_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("sg_compound_gen_test: ok");
	return 0;
}
