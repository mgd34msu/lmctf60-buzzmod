/* Focused pure tests for compound phase ownership and suffix delegation. */
#include "q_shared.h"

#include <limits.h>
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

static void TestSuffixHoldSchedule(void)
{
	static const int invalid_elapsed[] = {
		INT_MIN, -100, -1, 1, 99, 101,
		SG_RUNE_V3_MAX_COST_MS + 1, INT_MAX
	};
	static const int invalid_clear[] = {
		INT_MIN, -100, -1, 0, 1, 99, 101,
		SG_RUNE_V3_MAX_COST_MS - 1,
		SG_RUNE_V3_MAX_COST_MS + 100, INT_MAX
	};
	int clear_ms;
	int elapsed_ms;
	int index;

	CHECK(SG_COMPOUND_HOLD_LEASE_MS == 500);
	CHECK(SG_COMPOUND_POST_CLEAR_MARGIN_MS == 100);
	CHECK(SG_COMPOUND_POST_CLEAR_MARGIN_MS ==
	      SG_RUNE_PROOF_SERVER_FRAME_MS);
	CHECK(SG_COMPOUND_HOLD_LEASE_MS %
	      SG_RUNE_PROOF_SERVER_FRAME_MS == 0);
	CHECK(SG_COMPOUND_HOLD_LEASE_MS -
	      SG_RUNE_PROOF_SERVER_FRAME_MS >=
	      SG_COMPOUND_POST_CLEAR_MARGIN_MS);

	/* Exhaust every valid pair in the v3 cost domain, not just examples:
	 * renewal ends exactly on the serialized clear boundary. */
	for (clear_ms = SG_RUNE_PROOF_SERVER_FRAME_MS;
	     clear_ms <= SG_RUNE_V3_MAX_COST_MS;
	     clear_ms += SG_RUNE_PROOF_SERVER_FRAME_MS)
		for (elapsed_ms = 0;
		     elapsed_ms <= SG_RUNE_V3_MAX_COST_MS;
		     elapsed_ms += SG_RUNE_PROOF_SERVER_FRAME_MS)
			CHECK(SG_CompoundSuffixNeedsHold(elapsed_ms, clear_ms) ==
			      (elapsed_ms < clear_ms));

	for (index = 0;
	     index < (int)(sizeof(invalid_elapsed) /
	                   sizeof(invalid_elapsed[0]));
	     index++)
		CHECK(SG_CompoundSuffixNeedsHold(invalid_elapsed[index], 100));
	for (index = 0;
	     index < (int)(sizeof(invalid_clear) /
	                   sizeof(invalid_clear[0]));
	     index++)
		CHECK(SG_CompoundSuffixNeedsHold(300, invalid_clear[index]));

	CHECK(SG_CompoundSuffixNeedsHold(0, 100));
	CHECK(!SG_CompoundSuffixNeedsHold(100, 100));
	CHECK(!SG_CompoundSuffixNeedsHold(200, 100));
	CHECK(SG_CompoundSuffixNeedsHold(400, 500));
	CHECK(!SG_CompoundSuffixNeedsHold(500, 500));
}

static void CheckTranslateStep(const sg_compound_translate_step_t *step,
	float delta_x, float origin_x, int elapsed_ms, int at_top)
{
	CHECK(step->delta[0] == delta_x);
	CHECK(step->delta[1] == 0.0f && step->delta[2] == 0.0f);
	CHECK(step->origin[0] == origin_x);
	CHECK(step->origin[1] == 0.0f && step->origin[2] == 0.0f);
	CHECK(step->elapsed_ms == elapsed_ms);
	CHECK(step->at_top == at_top);
}

static void TestTranslateExactFullFrames(void)
{
	static const float expected_delta[] =
		{ 0.0f, 20.0f, 20.0f, 20.0f, 20.0f };
	static const float expected_origin[] =
		{ 0.0f, 20.0f, 40.0f, 60.0f, 80.0f };
	const float start[3] = { 0.0f, 0.0f, 0.0f };
	const float end[3] = { 80.0f, 0.0f, 0.0f };
	sg_compound_translate_t state;
	sg_compound_translate_step_t step;
	int frame;

	CHECK(SG_CompoundTranslateBegin(&state, start, end, 200.0f));
	CHECK(state.phase == SG_COMPOUND_TRANSLATE_SCHEDULED);
	for (frame = 0; frame < 5; frame++)
	{
		CHECK(SG_CompoundTranslateFrame(&state, &step));
		CheckTranslateStep(&step, expected_delta[frame],
			expected_origin[frame], (frame + 1) * 100,
			frame == 4);
	}
	CHECK(state.phase == SG_COMPOUND_TRANSLATE_TOP);
	CHECK(state.origin[0] == 80.0f && state.elapsed_ms == 500);
	CHECK(!SG_CompoundTranslateFrame(&state, &step));
	CHECK(step.elapsed_ms == 0 && !step.at_top);
}

static void TestTranslateFractionalFinal(void)
{
	static const float expected_delta[] =
		{ 0.0f, 20.0f, 20.0f, 20.0f, 20.0f, 20.0f, 20.0f, 8.0f };
	static const float expected_origin[] =
		{ 0.0f, 20.0f, 40.0f, 60.0f, 80.0f, 100.0f, 120.0f,
		  128.0f };
	const float start[3] = { 0.0f, 0.0f, 0.0f };
	const float end[3] = { 128.0f, 0.0f, 0.0f };
	sg_compound_translate_t state;
	sg_compound_translate_step_t step;
	int frame;

	CHECK(SG_CompoundTranslateBegin(&state, start, end, 200.0f));
	for (frame = 0; frame < 8; frame++)
	{
		CHECK(SG_CompoundTranslateFrame(&state, &step));
		CheckTranslateStep(&step, expected_delta[frame],
			expected_origin[frame], (frame + 1) * 100,
			frame == 7);
		if (frame == 6)
			CHECK(state.phase == SG_COMPOUND_TRANSLATE_FINAL);
	}
	CHECK(state.phase == SG_COMPOUND_TRANSLATE_TOP);
	CHECK(state.origin[0] == 128.0f && state.elapsed_ms == 800);
}

static void TestTranslateFloatFrameLaw(void)
{
	const float start[3] = { 0.0f, 0.0f, 0.0f };
	const float end[3] = { 80.0f, 0.0f, 0.0f };
	sg_compound_translate_t state;
	sg_compound_translate_step_t step;
	int frame;

	/* Move_Begin evaluates the division in float before floor().  This case
	 * distinguishes that law from a widened double calculation. */
	CHECK(SG_CompoundTranslateBegin(&state, start, end, 160.0f));
	for (frame = 0; frame < 6; frame++)
	{
		CHECK(SG_CompoundTranslateFrame(&state, &step));
		CheckTranslateStep(&step, frame ? 16.0f : 0.0f,
			frame * 16.0f, (frame + 1) * 100, frame == 5);
	}
	CHECK(state.phase == SG_COMPOUND_TRANSLATE_TOP);
	CHECK(state.remaining_distance == 0.0f);
}

static void TestTranslateNumericBounds(void)
{
	const float zero[3] = { 0.0f, 0.0f, 0.0f };
	const float huge_quantized[3] = { 1.0e10f, 0.0f, 0.0f };
	const float huge_elapsed[3] = { 214748364.8f, 0.0f, 0.0f };
	sg_compound_translate_t state;
	sg_compound_translate_step_t step;

	CHECK(!SG_CompoundTranslateBegin(&state, zero, huge_quantized, 1.0e11f));
	CHECK(state.phase == SG_COMPOUND_TRANSLATE_NONE);
	CHECK(!SG_CompoundTranslateBegin(&state, zero, huge_elapsed, 1.0f));
	CHECK(state.phase == SG_COMPOUND_TRANSLATE_NONE);
	CHECK(!SG_CompoundTranslateBegin(&state, zero, zero, 200.0f));
	CHECK(state.phase == SG_COMPOUND_TRANSLATE_NONE);
	CHECK(SG_CompoundTranslateBegin(&state,
		(const float[3]){ 0.0f, 0.0f, 0.0f },
		(const float[3]){ 80.0f, 0.0f, 0.0f }, 200.0f));
	state.phase = SG_COMPOUND_TRANSLATE_FULL;
	state.full_frames_remaining = 0;
	CHECK(!SG_CompoundTranslateFrame(&state, &step));
	CHECK(state.elapsed_ms == 0);
}

static void TestTranslateFinalFloatLaw(void)
{
	const float start[3] = { 0.0f, 0.0f, 0.0f };
	const float end[3] = {
		-0x1.8c5f14p+11f, -0x1.ae8c9cp+11f, -0x1.6fd8p+2f
	};
	sg_compound_translate_t state;
	sg_compound_translate_step_t step;

	CHECK(SG_CompoundTranslateBegin(&state, start, end, 0x1.93431p+11f));
	CHECK(SG_CompoundTranslateFrame(&state, &step));
	while (state.phase == SG_COMPOUND_TRANSLATE_FULL)
		CHECK(SG_CompoundTranslateFrame(&state, &step));
	CHECK(state.phase == SG_COMPOUND_TRANSLATE_FINAL);
	CHECK(SG_CompoundTranslateFrame(&state, &step));
	/* Move_Final rounds through velocity before SV_Push rounds the move. */
	CHECK(step.delta[1] == -0x1.e68p+6f);
	CHECK(state.phase == SG_COMPOUND_TRANSLATE_TOP);
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
	TestSuffixHoldSchedule();
	TestTranslateExactFullFrames();
	TestTranslateFractionalFinal();
	TestTranslateFloatFrameLaw();
	TestTranslateNumericBounds();
	TestTranslateFinalFloatLaw();
	TestValidationAndMalformedInput();
	if (failures)
	{
		fprintf(stderr, "sg_compound_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("sg_compound_test: ok");
	return 0;
}
