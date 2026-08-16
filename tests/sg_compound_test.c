/* Focused pure tests for compound phase ownership and suffix delegation. */
#include "q_shared.h"

#include <stdio.h>
#include <string.h>

#include "slipgate/sg_compound.h"

static int failures;
static int delegated_action = -1;
static int delegated_link = -1;

#define CHECK(x) do { if (!(x)) { \
	fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #x); \
	failures++; } } while (0)

static int CaptureSuffix(void *context, int link_index, int suffix_action)
{
	int *calls = (int *)context;

	(*calls)++;
	delegated_link = link_index;
	delegated_action = suffix_action;
	return 1;
}

static int RejectSuffix(void *context, int link_index, int suffix_action)
{
	(void)context;
	(void)link_index;
	(void)suffix_action;
	return 0;
}

static void TestPreopen(void)
{
	sg_compound_state_t state;

	SG_CompoundReset(&state);
	CHECK(SG_CompoundBegin(&state, 7, 41, RL_DOOR_SWIM, RLCM_PREOPEN));
	CHECK(SG_CompoundOwns(&state, 7, 41));
	CHECK(state.suffix_action == RL_SWIM);
	CHECK(SG_CompoundAdvance(&state, SG_COMPOUND_EVENT_APPROACH));
	CHECK(SG_CompoundAdvance(&state, SG_COMPOUND_EVENT_TOUCH));
	CHECK(SG_CompoundAdvance(&state, SG_COMPOUND_EVENT_ACTIVATE));
	CHECK(SG_CompoundAdvance(&state, SG_COMPOUND_EVENT_TOP));
	CHECK(SG_CompoundAdvance(&state, SG_COMPOUND_EVENT_SUFFIX_BEGIN));
	CHECK(SG_CompoundAdvance(&state, SG_COMPOUND_EVENT_ARRIVED));
	CHECK(SG_CompoundLeaseHeld(&state));
	CHECK(SG_CompoundAdvance(&state, SG_COMPOUND_EVENT_SWEEP_CLEAR));
	CHECK(!SG_CompoundLeaseHeld(&state));
}

static void TestRideAndCleanup(void)
{
	sg_compound_state_t state;

	SG_CompoundReset(&state);
	CHECK(SG_CompoundBegin(&state, 9, 2, RL_DOOR_DROP, RLCM_RIDE));
	CHECK(state.suffix_action == RL_DROP);
	CHECK(SG_CompoundAdvance(&state, SG_COMPOUND_EVENT_APPROACH));
	CHECK(SG_CompoundAdvance(&state, SG_COMPOUND_EVENT_TOUCH));
	CHECK(SG_CompoundAdvance(&state, SG_COMPOUND_EVENT_ACTIVATE));
	CHECK(!SG_CompoundAdvance(&state, SG_COMPOUND_EVENT_TOP));
	CHECK(SG_CompoundAdvance(&state, SG_COMPOUND_EVENT_RIDE));
	CHECK(SG_CompoundAdvance(&state, SG_COMPOUND_EVENT_TOP));
	CHECK(SG_CompoundAdvance(&state, SG_COMPOUND_EVENT_ABORT));
	CHECK(SG_CompoundLeaseHeld(&state));
	CHECK(!SG_CompoundAdvance(&state, SG_COMPOUND_EVENT_ARRIVED));
	CHECK(SG_CompoundAdvance(&state, SG_COMPOUND_EVENT_RECOVERED));
	CHECK(!SG_CompoundLeaseHeld(&state));
}

static void TestOwnershipAndDispatch(void)
{
	sg_compound_state_t a, b;
	int action;
	int calls = 0;

	SG_CompoundReset(&a);
	SG_CompoundReset(&b);
	CHECK(SG_COMPOUND_LIVE_CONTROLLER_REVISION == 0);
	for (action = RL_DOOR_DROP; action <= RL_DOOR_HOOK; action++)
		CHECK(!SG_CompoundRuntimeReady(action));
	CHECK(!SG_CompoundRuntimeReady(RL_DOOR));
	CHECK(SG_CompoundSuffixAction(RL_DOOR_DROP) == RL_DROP);
	CHECK(SG_CompoundSuffixAction(RL_DOOR_SWIM) == RL_SWIM);
	CHECK(SG_CompoundSuffixAction(RL_DOOR_HOOK) == RL_HOOK);
	CHECK(SG_CompoundSuffixAction(RL_DOOR) < 0);
	CHECK(SG_CompoundBegin(&a, 1, 8, RL_DOOR_HOOK, RLCM_PREOPEN));
	CHECK(!SG_CompoundBegin(&a, 2, 8, RL_DOOR_HOOK, RLCM_PREOPEN));
	CHECK(!SG_CompoundOwns(&a, 2, 8));
	CHECK(!SG_CompoundBegin(&b, 2, 8, RL_DOOR_SWIM, RLCM_NONE));
	CHECK(!SG_CompoundDelegateSuffix(&a, 1, 8, CaptureSuffix, &calls));
	CHECK(calls == 0);
	CHECK(SG_CompoundAdvance(&a, SG_COMPOUND_EVENT_APPROACH));
	CHECK(SG_CompoundAdvance(&a, SG_COMPOUND_EVENT_TOUCH));
	CHECK(SG_CompoundAdvance(&a, SG_COMPOUND_EVENT_ACTIVATE));
	CHECK(SG_CompoundAdvance(&a, SG_COMPOUND_EVENT_TOP));
	CHECK(!SG_CompoundDelegateSuffix(&a, 2, 8, CaptureSuffix, &calls));
	CHECK(!SG_CompoundDelegateSuffix(&a, 1, 8, RejectSuffix, NULL));
	CHECK(a.phase == SG_COMPOUND_TOP && calls == 0);
	CHECK(SG_CompoundDelegateSuffix(&a, 1, 8, CaptureSuffix, &calls));
	CHECK(calls == 1 && delegated_link == 1 &&
		delegated_action == RL_HOOK);
	CHECK(a.phase == SG_COMPOUND_SUFFIX_LEASED);
	SG_CompoundReset(&a);
	CHECK(!SG_CompoundLeaseHeld(&a));
}

static void ValidLink(sg_rune_v3_seed_t seeds[2], sg_rune_v3_link_t *link,
	int action)
{
	memset(seeds, 0, sizeof(*seeds) * 2U);
	memset(link, 0, sizeof(*link));
	seeds[1].origin[0] = 64.0f;
	link->source = 0;
	link->destination = 1;
	link->action = (uint8_t)action;
	link->provenance = RL_CONTRACTED;
	link->cost_ms = 500;
	link->sweep_clear_ms = 100;
	link->mode = RLCM_PREOPEN;
	link->mechanism_anchor[0] = 16.0f;
	if (action == RL_DOOR_DROP)
	{
		link->suffix_anchor[0] = 32.0f;
		link->heading_slack = SG_RUNE_PROOF_DROP_CONTROL_MARKER;
	}
	else
	{
		seeds[0].flags = SG_RUNE_V3_SEED_WATER;
		if (action == RL_DOOR_HOOK)
		{
			link->suffix_anchor[2] = 64.0f;
			link->heading_slack =
				SG_RUNE_PROOF_WATER_HOOK_CONTROL_MARKER;
		}
	}
}

static void TestValidationAndMalformedInput(void)
{
	sg_rune_v3_seed_t seeds[2];
	sg_rune_v3_link_t link;
	int action;

	for (action = RL_DOOR_DROP; action <= RL_DOOR_HOOK; action++)
	{
		ValidLink(seeds, &link, action);
		CHECK(SG_CompoundValidateLink(seeds, 2, &link) == RLR_OK);
	}
	ValidLink(seeds, &link, RL_DOOR_DROP);
	link.provenance = RL_PROVEN;
	CHECK(SG_CompoundValidateLink(seeds, 2, &link) ==
		RLR_PROVENANCE_FORBIDDEN);
	ValidLink(seeds, &link, RL_DOOR_DROP);
	link.mode = RLCM_NONE;
	CHECK(SG_CompoundValidateLink(seeds, 2, &link) == RLR_BAD_MODE);
	ValidLink(seeds, &link, RL_DOOR_DROP);
	link.mechanism_anchor[0] = 16.0625f;
	CHECK(SG_CompoundValidateLink(seeds, 2, &link) ==
		RLR_BAD_MECHANISM_ANCHOR);
	ValidLink(seeds, &link, RL_DOOR_DROP);
	link.sweep_clear_ms = 125;
	CHECK(SG_CompoundValidateLink(seeds, 2, &link) ==
		RLR_BAD_SWEEP_CLEAR);
	ValidLink(seeds, &link, RL_DOOR_SWIM);
	link.suffix_anchor[0] = 1.0f;
	CHECK(SG_CompoundValidateLink(seeds, 2, &link) ==
		RLR_BAD_SWIM_CONTROL);
	ValidLink(seeds, &link, RL_DOOR_HOOK);
	link.suffix_anchor[2] = 0.0f;
	CHECK(SG_CompoundValidateLink(seeds, 2, &link) ==
		RLR_BAD_HOOK_CONTROL);
}

int main(void)
{
	TestPreopen();
	TestRideAndCleanup();
	TestOwnershipAndDispatch();
	TestValidationAndMalformedInput();
	if (failures)
	{
		fprintf(stderr, "sg_compound_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("sg_compound_test: ok");
	return 0;
}
