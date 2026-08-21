/* Focused generation contract for compound door links. */
#include <stdio.h>
#include <math.h>
#include <string.h>

#include "../slipgate/sg_compound_action_gen.h"

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
	int bad_timing;
} proof_context_t;

static rune_reject_reason_t Prove(void *opaque, int action,
	const sg_compound_action_gen_candidate_t *candidate,
	sg_compound_action_gen_proof_t *proof)
{
	proof_context_t *context = (proof_context_t *)opaque;

	context->calls++;
	memset(proof, 0, sizeof(*proof));
	proof->touch_ms = 25;
	proof->touch_frame_end_ms = 100;
	proof->mover_top_ms = 500;
	proof->suffix_start_ms = 400;
	proof->arrival_ms = 400;
	proof->sweep_clear_ms = 200;
	proof->total_cost_ms = context->bad_timing ? 800 : 900;
	proof->exit_speed = 31;
	if (action == RL_DOOR_DROP)
	{
		proof->suffix_anchor[0] = 64.0f;
		proof->suffix_anchor[1] = 32.0f;
		proof->suffix_anchor[2] = 16.0f;
		proof->heading = 64;
		proof->heading_slack = SG_RUNE_PROOF_DROP_CONTROL_MARKER;
	}
	else if (action == RL_DOOR_HOOK)
	{
		proof->suffix_anchor[PITCH] =
			SHORT2ANGLE((short)ANGLE2SHORT(-15.0f));
		proof->suffix_anchor[YAW] = SHORT2ANGLE(ANGLE2SHORT(90.0f));
		proof->suffix_anchor[ROLL] = 512.0f;
		proof->heading_slack = SG_RUNE_PROOF_WATER_HOOK_CONTROL_MARKER;
	}
	(void)candidate;
	return RLR_OK;
}

static void Seed(sg_compound_action_gen_seed_t *seed, int component,
	unsigned int objective_mask, int water, int incoming, int outgoing)
{
	memset(seed, 0, sizeof(*seed));
	seed->component = component;
	seed->objective_mask = (uint8_t)objective_mask;
	seed->water = (uint8_t)water;
	seed->has_incoming = (uint8_t)incoming;
	seed->has_outgoing = (uint8_t)outgoing;
}

static void Candidate(sg_compound_action_gen_candidate_t *candidate,
	int source, int destination, int mode)
{
	memset(candidate, 0, sizeof(*candidate));
	candidate->source = source;
	candidate->destination = destination;
	candidate->trigger_key = 40;
	candidate->mover_key = 41;
	candidate->mechanism_anchor[0] = 8.0f;
	candidate->mechanism_anchor[1] = 16.0f;
	candidate->mechanism_anchor[2] = 24.0f;
	candidate->mode = (uint8_t)mode;
	candidate->local_rank = 7;
}

static sg_compound_action_gen_request_t Request(int action,
	const sg_compound_action_gen_seed_t *seeds, size_t seed_count,
	const sg_compound_action_gen_candidate_t *candidate, rune_link_t *output,
	proof_context_t *context)
{
	sg_compound_action_gen_request_t request;

	memset(&request, 0, sizeof(request));
	request.action = action;
	request.seeds = seeds;
	request.seed_count = seed_count;
	request.candidates = candidate;
	request.candidate_count = 1;
	request.output = output;
	request.output_capacity = 1;
	request.prove = Prove;
	request.context = context;
	request.production_enabled = 1;
	return request;
}

static void CheckCommonLink(const rune_link_t *link, int action, int source,
	int destination, int mode)
{
	CHECK(link->from == source);
	CHECK(link->to == destination);
	CHECK(link->action == action);
	CHECK(link->provenance == RL_CONTRACTED);
	CHECK(link->cost_ms == 900);
	CHECK(link->exit_speed == 31);
	CHECK(link->mechanism_anchor[0] == 8.0f);
	CHECK(link->mechanism_anchor[1] == 16.0f);
	CHECK(link->mechanism_anchor[2] == 24.0f);
	CHECK(link->sweep_clear_ms == 200);
	CHECK(link->mode == mode);
	CHECK(link->mechanism_plan == RUNE_NO_MECHANISM_PLAN);
}

static void TestDoorDrop(void)
{
	sg_compound_action_gen_seed_t seeds[2];
	sg_compound_action_gen_candidate_t candidate;
	sg_compound_action_gen_request_t request;
	sg_compound_action_gen_result_t result;
	proof_context_t context = { 0, 0 };
	rune_link_t output;

	Seed(&seeds[0], 0, 0, 0, 1, 1);
	Seed(&seeds[1], 1, 1, 0, 1, 1);
	Candidate(&candidate, 0, 1, RLCM_RIDE);
	memset(&output, 0xa5, sizeof(output));
	request = Request(RL_DOOR_DROP, seeds, 2, &candidate, &output, &context);
	result = SG_CompoundActionGenPlan(&request);
	CHECK(result.status == SG_COMPOUND_ACTION_GEN_OK);
	CHECK(result.proof_calls == 1 && result.emitted == 1);
	CHECK(context.calls == 1);
	CheckCommonLink(&output, RL_DOOR_DROP, 0, 1, RLCM_RIDE);
	CHECK(output.anchor[0] == 64.0f && output.anchor[1] == 32.0f &&
	      output.anchor[2] == 16.0f);
	CHECK(output.heading == 64);
	CHECK(output.heading_slack == SG_RUNE_PROOF_DROP_CONTROL_MARKER);
}

static void TestDoorHook(void)
{
	sg_compound_action_gen_seed_t seeds[2];
	sg_compound_action_gen_candidate_t candidate;
	sg_compound_action_gen_request_t request;
	sg_compound_action_gen_result_t result;
	proof_context_t context = { 0, 0 };
	rune_link_t output;

	Seed(&seeds[0], 0, 0, 1, 1, 1);
	Seed(&seeds[1], 1, 2, 0, 1, 1);
	Candidate(&candidate, 0, 1, RLCM_PREOPEN);
	memset(&output, 0xa5, sizeof(output));
	request = Request(RL_DOOR_HOOK, seeds, 2, &candidate, &output, &context);
	result = SG_CompoundActionGenPlan(&request);
	CHECK(result.status == SG_COMPOUND_ACTION_GEN_OK);
	CHECK(result.proof_calls == 1 && result.emitted == 1);
	CheckCommonLink(&output, RL_DOOR_HOOK, 0, 1, RLCM_PREOPEN);
	CHECK(output.anchor[PITCH] ==
	      SHORT2ANGLE((short)ANGLE2SHORT(-15.0f)));
	CHECK(output.anchor[YAW] == SHORT2ANGLE(ANGLE2SHORT(90.0f)));
	CHECK(output.anchor[ROLL] == 512.0f);
	CHECK(output.heading_slack == SG_RUNE_PROOF_WATER_HOOK_CONTROL_MARKER);
}

static void TestLocallyCheapestSameComponent(void)
{
	sg_compound_action_gen_seed_t seeds[2];
	sg_compound_action_gen_candidate_t candidate;
	sg_compound_action_gen_request_t request;
	sg_compound_action_gen_result_t result;
	proof_context_t context = { 0, 0 };
	rune_link_t output;

	Seed(&seeds[0], 3, 1, 0, 1, 1);
	Seed(&seeds[1], 3, 1, 0, 1, 1);
	Candidate(&candidate, 0, 1, RLCM_PREOPEN);
	request = Request(RL_DOOR_DROP, seeds, 2, &candidate, &output, &context);
	result = SG_CompoundActionGenPlan(&request);
	CHECK(result.status == SG_COMPOUND_ACTION_GEN_OK);
	CHECK(result.proof_calls == 1 && result.emitted == 1);
	CHECK(output.from == 0 && output.to == 1);
}

static void TestDisabledAndFailuresAreAtomic(void)
{
	sg_compound_action_gen_seed_t seeds[2];
	sg_compound_action_gen_candidate_t candidate;
	sg_compound_action_gen_request_t request;
	sg_compound_action_gen_result_t result;
	proof_context_t context = { 0, 0 };
	rune_link_t output;
	rune_link_t before;

	CHECK(SG_CompoundActionGenPlan(NULL).status ==
	      SG_COMPOUND_ACTION_GEN_INVALID);

	Seed(&seeds[0], 0, 0, 0, 1, 1);
	Seed(&seeds[1], 1, 1, 0, 1, 1);
	Candidate(&candidate, 0, 1, RLCM_PREOPEN);
	memset(&output, 0x5a, sizeof(output));
	before = output;
	request = Request(RL_DOOR_DROP, seeds, 2, &candidate, &output, &context);
	request.production_enabled = 0;
	result = SG_CompoundActionGenPlan(&request);
	CHECK(result.status == SG_COMPOUND_ACTION_GEN_DISABLED);
	CHECK(context.calls == 0 && memcmp(&output, &before, sizeof(output)) == 0);

	request.production_enabled = 1;
	context.bad_timing = 1;
	result = SG_CompoundActionGenPlan(&request);
	CHECK(result.status == SG_COMPOUND_ACTION_GEN_BAD_PROOF);
	CHECK(result.proof_calls == 1 && context.calls == 1 &&
	      memcmp(&output, &before, sizeof(output)) == 0);

	context.bad_timing = 0;
	candidate.source = 1; /* D_DROP requires a dry source; make it water. */
	candidate.destination = 0;
	seeds[1].water = 1;
	result = SG_CompoundActionGenPlan(&request);
	CHECK(result.status == SG_COMPOUND_ACTION_GEN_INVALID);
	CHECK(context.calls == 1 && memcmp(&output, &before, sizeof(output)) == 0);

	candidate.source = 0;
	candidate.destination = 1;
	seeds[1].water = 0;
	candidate.mechanism_anchor[0] = NAN;
	result = SG_CompoundActionGenPlan(&request);
	CHECK(result.status == SG_COMPOUND_ACTION_GEN_INVALID);
	CHECK(context.calls == 1 && memcmp(&output, &before, sizeof(output)) == 0);
}

int main(void)
{
	TestDoorDrop();
	TestDoorHook();
	TestLocallyCheapestSameComponent();
	TestDisabledAndFailuresAreAtomic();
	if (failures)
	{
		fprintf(stderr, "sg_compound_action_gen_test: %d failure(s)\n",
		        failures);
		return 1;
	}
	puts("sg_compound_action_gen_test: ok");
	return 0;
}
