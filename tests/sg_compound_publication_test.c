#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "g_local.h"
#include "slipgate/sg_compound_publication.h"
#include "slipgate/sg_local.h"

typedef enum fixture_proof_mutation_e
{
	PROOF_VALID = 0,
	PROOF_TOUCH_ZERO,
	PROOF_TOUCH_UNALIGNED,
	PROOF_FRAME_WRONG,
	PROOF_TOP_SHORT,
	PROOF_TOP_UNALIGNED,
	PROOF_SUFFIX_WRONG,
	PROOF_ARRIVAL_UNALIGNED,
	PROOF_CLEAR_ZERO,
	PROOF_CLEAR_AFTER_ARRIVAL,
	PROOF_TOTAL_WRONG,
	PROOF_COST_MISMATCH,
	PROOF_EXIT_MISMATCH,
	PROOF_CLEAR_MISMATCH,
	PROOF_BAD_SOURCE_CHECKPOINT,
	PROOF_BAD_SOURCE_OLD_Z,
	PROOF_BAD_SOURCE_WATER,
	PROOF_BAD_SUFFIX_CHECKPOINT
} fixture_proof_mutation_t;

typedef struct fixture_s
{
	int allocation_calls;
	int free_calls;
	int live_allocations;
	int fail_allocation_call;
	int resolve_calls;
	int enumerate_calls;
	int source_calls;
	int discover_calls;
	int replay_calls;
	int resolved_member_calls;
	int hint_match_calls;
	int fail_resolve;
	int fail_enumerate;
	int fail_source;
	int fail_discover;
	int fail_replay;
	int world_drift;
	int hint_drift;
	int inconsistent_second_mechanism;
	fixture_proof_mutation_t proof_mutation;
} fixture_t;

static fixture_t fixture;
static edict_t fixture_entities[4];
static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		        __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static void Set3(float value[3], float x, float y, float z)
{
	value[0] = x;
	value[1] = y;
	value[2] = z;
}

static void *FixtureAllocate(int size)
{
	void *block;

	fixture.allocation_calls++;
	if (size <= 0 || fixture.allocation_calls == fixture.fail_allocation_call)
		return NULL;
	block = malloc((size_t)size);
	if (block)
		fixture.live_allocations++;
	return block;
}

static void FixtureFree(void *block)
{
	if (!block)
		return;
	fixture.free_calls++;
	fixture.live_allocations--;
	free(block);
}

static void FillResolved(sg_compound_world_preopen_t *resolved)
{
	memset(resolved, 0, sizeof(*resolved));
	resolved->trigger = &fixture_entities[1];
	resolved->member = &fixture_entities[2];
	Set3(resolved->bottom_origin, 0.0f, 0.0f, 0.0f);
	Set3(resolved->top_origin, 64.0f, 0.0f, 0.0f);
	Set3(resolved->member_mins, -16.0f, -16.0f, -24.0f);
	Set3(resolved->member_maxs, 16.0f, 16.0f, 32.0f);
	resolved->speed = 80.0f;
	resolved->wait = 3.0f;
	resolved->trigger_key = 1;
	resolved->mover_key = 2;
	resolved->axis = 0;
}

rune_reject_reason_t SG_CompoundWorldEnumeratePreopen(
	sg_compound_world_candidate_t *candidates, int capacity,
	int *count_out)
{
	fixture.enumerate_calls++;
	if (!count_out || fixture.fail_enumerate)
		return RLR_MECHANISM_UNRESOLVED;
	*count_out = fixture.inconsistent_second_mechanism ? 2 : 1;
	if (!candidates && capacity == 0)
		return RLR_OK;
	if (!candidates || capacity < *count_out)
		return RLR_BAD_CONTROL_POLICY;
	memset(candidates, 0, sizeof(*candidates) * (size_t)*count_out);
	FillResolved(&candidates[0].resolved);
	Set3(candidates[0].hints[0], 10.0f, 0.0f, 0.0f);
	Set3(candidates[0].hints[1], 11.0f, 0.0f, 0.0f);
	candidates[0].hint_count = 2;
	if (fixture.inconsistent_second_mechanism)
	{
		candidates[1] = candidates[0];
		candidates[1].resolved.wait = 4.0f;
	}
	return RLR_OK;
}

rune_reject_reason_t SG_CompoundWorldResolvePreopen(
	const float mechanism_anchor[3],
	sg_compound_world_preopen_t *resolved)
{
	fixture.resolve_calls++;
	if (fixture.fail_resolve || !mechanism_anchor || !resolved)
		return RLR_MECHANISM_UNRESOLVED;
	FillResolved(resolved);
	if (fixture.world_drift)
		resolved->speed = 81.0f;
	if (fixture.inconsistent_second_mechanism && fixture.resolve_calls == 2)
		resolved->wait = 4.0f;
	return RLR_OK;
}

int SG_CompoundWorldResolvedMember(
	const sg_compound_world_preopen_t *resolved, edict_t **member_out)
{
	sg_compound_world_preopen_t expected;

	fixture.resolved_member_calls++;
	if (member_out)
		*member_out = NULL;
	if (!resolved || !member_out || fixture.world_drift)
		return 0;
	FillResolved(&expected);
	if (resolved->trigger != expected.trigger ||
	    resolved->member != expected.member ||
	    resolved->trigger_key != expected.trigger_key ||
	    resolved->mover_key != expected.mover_key ||
	    resolved->speed != expected.speed || resolved->wait != expected.wait)
		return 0;
	*member_out = resolved->member;
	return 1;
}

int SG_CompoundWorldPreopenHintMatches(
	const sg_compound_world_preopen_t *resolved, const float hint[3])
{
	fixture.hint_match_calls++;
	return resolved && hint && !fixture.world_drift && !fixture.hint_drift &&
	       resolved->trigger == &fixture_entities[1] &&
	       resolved->member == &fixture_entities[2] && hint[0] == 11.0f &&
	       hint[1] == 0.0f && hint[2] == 0.0f;
}

static void FillPmove(pmove_state_t *pms, const float origin[3], int delta)
{
	int axis;

	memset(pms, 0, sizeof(*pms));
	pms->pm_type = PM_NORMAL;
	pms->pm_flags = PMF_TIME_WATERJUMP;
	pms->pm_time = 3;
	pms->gravity = 800;
	for (axis = 0; axis < 3; axis++)
	{
		pms->origin[axis] = (short)(origin[axis] * 8.0f);
		pms->velocity[axis] = (short)(8 + axis);
		pms->delta_angles[axis] = (short)(delta + axis * 100);
	}
}

rune_reject_reason_t SG_OracleCompoundSwimPrepareSource(
	const vec3_t source, const sg_compound_world_preopen_t *resolved,
	float old_frame_z, sg_compound_swim_source_t *prepared,
	edict_t *passent, qboolean world_only, qboolean loader_replay)
{
	int axis;

	(void)resolved; (void)passent;
	fixture.source_calls++;
	if (fixture.fail_source || !source || !prepared || old_frame_z != 0.0f ||
	    !world_only || !loader_replay)
		return RLR_APPROACH_REPLAY_FAILED;
	memset(prepared, 0, sizeof(*prepared));
	FillPmove(&prepared->phantom.pms, source, 100);
	FillPmove(&prepared->phantom.old_pms, source, 90);
	for (axis = 0; axis < 3; axis++)
	{
		prepared->phantom.origin[axis] = source[axis];
		prepared->phantom.velocity[axis] =
			prepared->phantom.pms.velocity[axis] * 0.125f;
	}
	prepared->phantom.watertype = CONTENTS_WATER;
	prepared->phantom.waterlevel = 3;
	prepared->old_frame_z = 0.0f;
	if (fixture.proof_mutation == PROOF_BAD_SOURCE_CHECKPOINT)
		prepared->phantom.origin[0] += 0.125f;
	if (fixture.proof_mutation == PROOF_BAD_SOURCE_OLD_Z)
		prepared->old_frame_z = -0.0f;
	if (fixture.proof_mutation == PROOF_BAD_SOURCE_WATER)
		prepared->phantom.waterlevel = 1;
	return RLR_OK;
}

rune_reject_reason_t SG_OracleCompoundSwimDiscoverContact(
	const sg_compound_swim_source_t *prepared,
	const sg_compound_world_preopen_t *resolved,
	const vec3_t canonical_hint, vec3_t mechanism_anchor,
	edict_t *passent, qboolean world_only, qboolean loader_replay)
{
	(void)prepared; (void)resolved; (void)passent;
	fixture.discover_calls++;
	if (fixture.fail_discover || !canonical_hint || !mechanism_anchor ||
	    !world_only || !loader_replay)
		return RLR_APPROACH_REPLAY_FAILED;
	if (canonical_hint[0] == 11.0f)
		Set3(mechanism_anchor, 20.0f, 0.0f, 0.0f);
	else
		Set3(mechanism_anchor, 19.0f, 0.0f, 0.0f);
	return RLR_OK;
}

rune_reject_reason_t SG_OracleCompoundSwimPreopen(sg_phantom_t *phantom,
	const sg_compound_world_preopen_t *resolved,
	const vec3_t mechanism_anchor, const vec3_t destination,
	qboolean destination_water, float old_frame_z,
	sg_compound_swim_proof_t *proof, sg_replay_reason_t *replay_reason,
	edict_t *passent,
	qboolean world_only, qboolean loader_replay)
{
	int axis;

	(void)resolved; (void)mechanism_anchor; (void)replay_reason; (void)passent;
	fixture.replay_calls++;
	if (fixture.fail_replay || !phantom || !destination || !proof ||
	    !destination_water || old_frame_z != 0.0f ||
	    !world_only || !loader_replay)
		return RLR_SUFFIX_REPLAY_FAILED;
	memset(proof, 0, sizeof(*proof));
	proof->touch_ms = 25;
	proof->touch_frame_end_ms = 100;
	proof->mover_top_ms = 500;
	proof->suffix_start_ms = 400;
	proof->arrival_ms = 300;
	proof->sweep_clear_ms = 200;
	proof->total_cost_ms = 800;
	proof->exit_speed = 12;
	FillPmove(&proof->suffix_pms, mechanism_anchor, 400);
	FillPmove(&proof->suffix_old_pms, mechanism_anchor, 390);
	for (axis = 0; axis < 3; axis++)
	{
		proof->suffix_origin[axis] =
			proof->suffix_pms.origin[axis] * 0.125f;
		proof->suffix_velocity[axis] =
			proof->suffix_pms.velocity[axis] * 0.125f;
	}
	proof->suffix_watertype = CONTENTS_WATER;
	proof->suffix_waterlevel = 3;
	proof->suffix_old_frame_z = 1.0f;
	switch (fixture.proof_mutation)
	{
	case PROOF_TOUCH_ZERO: proof->touch_ms = 0; break;
	case PROOF_TOUCH_UNALIGNED: proof->touch_ms = 26; break;
	case PROOF_FRAME_WRONG: proof->touch_frame_end_ms = 200; break;
	case PROOF_TOP_SHORT: proof->mover_top_ms = 100;
		proof->suffix_start_ms = 0; break;
	case PROOF_TOP_UNALIGNED: proof->mover_top_ms = 550;
		proof->suffix_start_ms = 450; break;
	case PROOF_SUFFIX_WRONG: proof->suffix_start_ms = 300; break;
	case PROOF_ARRIVAL_UNALIGNED: proof->arrival_ms = 325; break;
	case PROOF_CLEAR_ZERO: proof->sweep_clear_ms = 0; break;
	case PROOF_CLEAR_AFTER_ARRIVAL: proof->sweep_clear_ms = 400; break;
	case PROOF_TOTAL_WRONG: proof->total_cost_ms = 900; break;
	case PROOF_COST_MISMATCH: proof->total_cost_ms = 900;
		proof->arrival_ms = 400; break;
	case PROOF_EXIT_MISMATCH: proof->exit_speed = 13; break;
	case PROOF_CLEAR_MISMATCH: proof->sweep_clear_ms = 100; break;
	case PROOF_BAD_SUFFIX_CHECKPOINT: proof->suffix_velocity[1] += 0.125f;
		break;
	default: break;
	}
	return RLR_OK;
}

static rune_link_t CompoundLink(int from, int to)
{
	rune_link_t link;

	memset(&link, 0, sizeof(link));
	link.from = from;
	link.to = to;
	link.action = RL_DOOR_SWIM;
	link.provenance = RL_CONTRACTED;
	link.exit_speed = 12;
	link.cost_ms = 800;
	Set3(link.mechanism_anchor, 20.0f, 0.0f, 0.0f);
	link.sweep_clear_ms = 200;
	link.mode = RLCM_PREOPEN;
	return link;
}

static rune_t RuneFixture(rune_seed_t seeds[3], rune_link_t links[3])
{
	rune_t rune;

	memset(&rune, 0, sizeof(rune));
	memset(seeds, 0, sizeof(rune_seed_t) * 3U);
	memset(links, 0, sizeof(rune_link_t) * 3U);
	Set3(seeds[0].origin, 0.0f, 0.0f, 0.0f);
	Set3(seeds[1].origin, 40.0f, 0.0f, 0.0f);
	Set3(seeds[2].origin, 80.0f, 0.0f, 0.0f);
	seeds[0].flags = seeds[1].flags = seeds[2].flags = RSF_WATER;
	links[0] = CompoundLink(0, 1);
	links[1].from = 1;
	links[1].to = 2;
	links[1].action = RL_SWIM;
	links[1].provenance = RL_PROVEN;
	links[1].cost_ms = 100;
	links[2] = CompoundLink(2, 1);
	rune.hdr.num_seeds = 3;
	rune.hdr.num_links = 3;
	rune.seeds = seeds;
	rune.links = links;
	return rune;
}

static void ResetFixture(void)
{
	memset(&fixture, 0, sizeof(fixture));
	memset(fixture_entities, 0, sizeof(fixture_entities));
}

static sg_compound_publication_result_t Build(rune_t *rune)
{
	return SG_CompoundPublicationBuild(rune, FixtureAllocate, FixtureFree,
	                                  &rune->compound_publication);
}

static void Destroy(rune_t *rune)
{
	SG_CompoundPublicationDestroy(rune->compound_publication);
	rune->compound_publication = NULL;
}

static void TestPositiveDedupAndMutation(void)
{
	rune_seed_t seeds[3];
	rune_link_t links[3];
	rune_t rune;
	sg_compound_publication_result_t result;
	const sg_compound_publication_binding_t *first, *second;
	sg_compound_publication_binding_t *mutable_first;
	const sg_compound_world_preopen_t *first_mechanism, *second_mechanism;
	rune_link_t saved;
	rune_seed_t saved_seed;
	int saved_total;
	int allocations_before;

	ResetFixture();
	rune = RuneFixture(seeds, links);
	result = Build(&rune);
	CHECK(result.status == SG_COMPOUND_PUBLICATION_OK);
	CHECK(result.link_index == SG_COMPOUND_PUBLICATION_INDEX_NONE);
	CHECK(SG_CompoundPublicationCount(&rune) == 2);
	CHECK(fixture.enumerate_calls == 2);
	CHECK(fixture.resolve_calls == 2);
	CHECK(fixture.source_calls == 2);
	CHECK(fixture.discover_calls == 4);
	CHECK(fixture.replay_calls == 2);
	first = SG_CompoundPublicationBinding(&rune, 0);
	second = SG_CompoundPublicationBinding(&rune, 2);
	CHECK(first != NULL && second != NULL);
	mutable_first = (sg_compound_publication_binding_t *)first;
	CHECK(SG_CompoundPublicationBinding(&rune, 1) == NULL);
	CHECK(first && first->touch_ms == 25 &&
	      first->touch_frame_end_ms == 100 && first->mover_top_ms == 500 &&
	      first->suffix_start_ms == 400 && first->arrival_ms == 300 &&
	      first->sweep_clear_ms == 200 && first->total_cost_ms == 800);
	first_mechanism = SG_CompoundPublicationMechanism(&rune, first);
	second_mechanism = SG_CompoundPublicationMechanism(&rune, second);
	CHECK(first_mechanism != NULL);
	CHECK(first_mechanism == second_mechanism);
	CHECK(SG_CompoundPublicationRevalidate(&rune).status ==
	      SG_COMPOUND_PUBLICATION_OK);
	allocations_before = fixture.allocation_calls;
	CHECK(Build(&rune).status == SG_COMPOUND_PUBLICATION_INVALID);
	CHECK(rune.compound_publication != NULL);
	CHECK(fixture.allocation_calls == allocations_before);

	saved = links[0];
	links[0].cost_ms++;
	CHECK(SG_CompoundPublicationBinding(&rune, 0) == NULL);
	links[0] = saved;
	links[0].exit_speed++;
	CHECK(SG_CompoundPublicationBinding(&rune, 0) == NULL);
	links[0] = saved;
	links[0].sweep_clear_ms += 100;
	CHECK(SG_CompoundPublicationBinding(&rune, 0) == NULL);
	links[0] = saved;
	links[0].mode = RLCM_NONE;
	CHECK(SG_CompoundPublicationBinding(&rune, 0) == NULL);
	links[0] = saved;
	links[0].anchor[1] = 0.125f;
	CHECK(SG_CompoundPublicationBinding(&rune, 0) == NULL);
	links[0] = saved;
	links[0].mechanism_anchor[1] = 0.125f;
	CHECK(SG_CompoundPublicationBinding(&rune, 0) == NULL);
	CHECK(SG_CompoundPublicationRevalidate(&rune).status ==
	      SG_COMPOUND_PUBLICATION_MISMATCH);
	links[0] = saved;
	saved_seed = seeds[0];
	seeds[0].origin[0] += 0.125f;
	CHECK(SG_CompoundPublicationBinding(&rune, 0) == NULL);
	CHECK(SG_CompoundPublicationRevalidate(&rune).status ==
	      SG_COMPOUND_PUBLICATION_MISMATCH);
	seeds[0] = saved_seed;
	saved_total = mutable_first->total_cost_ms;
	mutable_first->total_cost_ms += 100;
	CHECK(SG_CompoundPublicationBinding(&rune, 0) == NULL);
	CHECK(SG_CompoundPublicationRevalidate(&rune).status ==
	      SG_COMPOUND_PUBLICATION_MISMATCH);
	mutable_first->total_cost_ms = saved_total;

	fixture.world_drift = 1;
	result = SG_CompoundPublicationRevalidate(&rune);
	CHECK(result.status == SG_COMPOUND_PUBLICATION_WORLD_DRIFT);
	CHECK(result.link_index == 0);
	fixture.world_drift = 0;
	CHECK(SG_CompoundPublicationRevalidate(&rune).status ==
	      SG_COMPOUND_PUBLICATION_OK);
	fixture.hint_drift = 1;
	result = SG_CompoundPublicationRevalidate(&rune);
	CHECK(result.status == SG_COMPOUND_PUBLICATION_WORLD_DRIFT);
	CHECK(result.link_index == 0);
	fixture.hint_drift = 0;
	CHECK(SG_CompoundPublicationRevalidate(&rune).status ==
	      SG_COMPOUND_PUBLICATION_OK);
	Destroy(&rune);
	CHECK(fixture.allocation_calls == 4);
	CHECK(fixture.free_calls == 4);
	CHECK(fixture.live_allocations == 0);
}

static void TestAtomicFailures(void)
{
	int failed_call;

	for (failed_call = 1; failed_call <= 4; failed_call++)
	{
		rune_seed_t seeds[3];
		rune_link_t links[3];
		rune_t rune;
		sg_compound_publication_result_t result;

		ResetFixture();
		fixture.fail_allocation_call = failed_call;
		rune = RuneFixture(seeds, links);
		result = Build(&rune);
		CHECK(result.status == SG_COMPOUND_PUBLICATION_ALLOCATION);
		CHECK(rune.compound_publication == NULL);
		CHECK(fixture.live_allocations == 0);
		CHECK(fixture.free_calls == failed_call - 1);
	}
	{
		fixture_proof_mutation_t mutation;

		for (mutation = PROOF_TOUCH_ZERO;
		     mutation <= PROOF_BAD_SUFFIX_CHECKPOINT; mutation++)
		{
			rune_seed_t seeds[3];
			rune_link_t links[3];
			rune_t rune;
			sg_compound_publication_result_t result;

			ResetFixture();
			fixture.proof_mutation = mutation;
			rune = RuneFixture(seeds, links);
			result = Build(&rune);
			CHECK(result.status == SG_COMPOUND_PUBLICATION_MISMATCH);
			CHECK(result.link_index == 0);
			CHECK(rune.compound_publication == NULL);
			CHECK(fixture.live_allocations == 0);
		}
	}
	{
		rune_seed_t seeds[3];
		rune_link_t links[3];
		rune_t rune;

		ResetFixture();
		fixture.fail_enumerate = 1;
		rune = RuneFixture(seeds, links);
		CHECK(Build(&rune).status == SG_COMPOUND_PUBLICATION_MECHANISM);
		CHECK(rune.compound_publication == NULL);
		CHECK(fixture.live_allocations == 0);
		ResetFixture();
		fixture.fail_resolve = 1;
		rune = RuneFixture(seeds, links);
		CHECK(Build(&rune).status == SG_COMPOUND_PUBLICATION_MECHANISM);
		CHECK(rune.compound_publication == NULL);
		CHECK(fixture.live_allocations == 0);
		ResetFixture();
		fixture.fail_source = 1;
		rune = RuneFixture(seeds, links);
		CHECK(Build(&rune).status == SG_COMPOUND_PUBLICATION_SOURCE);
		CHECK(rune.compound_publication == NULL);
		CHECK(fixture.live_allocations == 0);
		ResetFixture();
		fixture.fail_discover = 1;
		rune = RuneFixture(seeds, links);
		CHECK(Build(&rune).status == SG_COMPOUND_PUBLICATION_MECHANISM);
		CHECK(rune.compound_publication == NULL);
		CHECK(fixture.live_allocations == 0);
		ResetFixture();
		fixture.fail_replay = 1;
		rune = RuneFixture(seeds, links);
		CHECK(Build(&rune).status == SG_COMPOUND_PUBLICATION_REPLAY);
		CHECK(rune.compound_publication == NULL);
		CHECK(fixture.live_allocations == 0);
		ResetFixture();
		fixture.inconsistent_second_mechanism = 1;
		rune = RuneFixture(seeds, links);
		CHECK(Build(&rune).status == SG_COMPOUND_PUBLICATION_MISMATCH);
		CHECK(rune.compound_publication == NULL);
		CHECK(fixture.live_allocations == 0);
	}
}

static void CheckInvalidRune(rune_t *rune)
{
	sg_compound_publication_result_t result = Build(rune);

	CHECK(result.status == SG_COMPOUND_PUBLICATION_INVALID);
	CHECK(rune->compound_publication == NULL);
	CHECK(fixture.live_allocations == 0);
	CHECK(fixture.enumerate_calls == 0);
}

static void TestCanonicalAndCountNegatives(void)
{
	rune_seed_t seeds[3];
	rune_link_t links[3];
	rune_t rune;

#define INVALID_MUTATION(statement) do { \
	ResetFixture(); \
	rune = RuneFixture(seeds, links); \
	statement; \
	CheckInvalidRune(&rune); \
} while (0)
	INVALID_MUTATION(links[0].provenance = RL_PROVEN);
	INVALID_MUTATION(links[0].mode = RLCM_NONE);
	INVALID_MUTATION(links[0].min_speed = 1);
	INVALID_MUTATION(links[0].heading = 1);
	INVALID_MUTATION(links[0].heading_slack = 1);
	INVALID_MUTATION(links[0].anchor[0] = 0.125f);
	INVALID_MUTATION(links[0].mechanism_anchor[0] = 20.01f);
	INVALID_MUTATION(links[0].from = -1);
	INVALID_MUTATION(links[0].to = links[0].from);
	INVALID_MUTATION(links[0].cost_ms = 50);
	INVALID_MUTATION(links[0].cost_ms = 850);
	INVALID_MUTATION(links[0].sweep_clear_ms = 0);
	INVALID_MUTATION(links[0].sweep_clear_ms = 900);
	INVALID_MUTATION(seeds[0].flags = 0);
	INVALID_MUTATION(seeds[0].flags = RSF_WATER | RSF_TOMBSTONE);
	INVALID_MUTATION(seeds[1].flags = RSF_WATER | RSF_TOMBSTONE);
	INVALID_MUTATION(seeds[0].origin[0] = 0.01f);
	INVALID_MUTATION(seeds[1].origin[0] = 5000.0f);
	INVALID_MUTATION(links[2].heading = 1);
#undef INVALID_MUTATION

	ResetFixture();
	rune = RuneFixture(seeds, links);
	rune.hdr.num_links = -1;
	CHECK(SG_CompoundPublicationBinding(&rune, 0) == NULL);
	CHECK(SG_CompoundPublicationCount(&rune) == 0);
	CHECK(SG_CompoundPublicationRevalidate(&rune).status ==
	      SG_COMPOUND_PUBLICATION_INVALID);
	ResetFixture();
	rune = RuneFixture(seeds, links);
	rune.links = NULL;
	CHECK(SG_CompoundPublicationBinding(&rune, 0) == NULL);
	CHECK(SG_CompoundPublicationRevalidate(&rune).status ==
	      SG_COMPOUND_PUBLICATION_INVALID);

	/* Destination seeds are finite world points, not mechanism anchors; the
	 * suffix replay, not a made-up 1/8 lattice rule, decides reachability. */
	ResetFixture();
	rune = RuneFixture(seeds, links);
	seeds[1].origin[0] = 40.01f;
	CHECK(Build(&rune).status == SG_COMPOUND_PUBLICATION_OK);
	Destroy(&rune);
	CHECK(fixture.live_allocations == 0);
}

static void AddAngle(short *value, short bias)
{
	uint16_t encoded = (uint16_t)*value + (uint16_t)bias;

	memcpy(value, &encoded, sizeof(*value));
}

static void ApplyBias(sg_compound_publication_checkpoint_t *checkpoint,
	const short bias[3])
{
	int axis;

	for (axis = 0; axis < 3; axis++)
	{
		AddAngle(&checkpoint->pms.delta_angles[axis], bias[axis]);
		AddAngle(&checkpoint->old_pms.delta_angles[axis], bias[axis]);
	}
}

static void TestDeltaBiasAndExactFields(void)
{
	rune_seed_t seeds[3];
	rune_link_t links[3];
	rune_t rune;
	const sg_compound_publication_binding_t *binding;
	sg_compound_publication_checkpoint_t live_source, live_suffix, changed;
	sg_compound_publication_angle_bias_t bias;
	const short expected_bias[3] = { 20, -30, 32760 };

	ResetFixture();
	rune = RuneFixture(seeds, links);
	CHECK(Build(&rune).status == SG_COMPOUND_PUBLICATION_OK);
	binding = SG_CompoundPublicationBinding(&rune, 0);
	CHECK(binding != NULL);
	live_source = binding->source;
	ApplyBias(&live_source, expected_bias);
	CHECK(SG_CompoundPublicationCaptureAngleBias(&binding->source,
	                                             &live_source, &bias));
	CHECK(bias.axis[0] == expected_bias[0]);
	CHECK(bias.axis[1] == expected_bias[1]);
	CHECK(bias.axis[2] == expected_bias[2]);
	live_suffix = binding->suffix;
	ApplyBias(&live_suffix, expected_bias);
	CHECK(SG_CompoundPublicationCheckpointMatches(&binding->suffix,
	                                              &live_suffix, &bias));
	AddAngle(&live_suffix.old_pms.delta_angles[1], 1);
	CHECK(!SG_CompoundPublicationCheckpointMatches(&binding->suffix,
	                                               &live_suffix, &bias));

#define CHECK_FIELD_MUTATION(statement) do { \
	changed = live_source; \
	statement; \
	CHECK(!SG_CompoundPublicationCaptureAngleBias(&binding->source, \
	                                             &changed, &bias)); \
} while (0)
	CHECK_FIELD_MUTATION(changed.pms.pm_type = PM_DEAD);
	CHECK_FIELD_MUTATION(changed.pms.origin[0]++);
	CHECK_FIELD_MUTATION(changed.pms.velocity[1]++);
	CHECK_FIELD_MUTATION(changed.pms.pm_flags++);
	CHECK_FIELD_MUTATION(changed.pms.pm_time++);
	CHECK_FIELD_MUTATION(changed.pms.gravity++);
	CHECK_FIELD_MUTATION(changed.old_pms.pm_type = PM_DEAD);
	CHECK_FIELD_MUTATION(changed.old_pms.origin[0]++);
	CHECK_FIELD_MUTATION(changed.old_pms.velocity[1]++);
	CHECK_FIELD_MUTATION(changed.old_pms.pm_flags++);
	CHECK_FIELD_MUTATION(changed.old_pms.pm_time++);
	CHECK_FIELD_MUTATION(changed.old_pms.gravity++);
	CHECK_FIELD_MUTATION(changed.grounded = true);
	CHECK_FIELD_MUTATION(changed.watertype = CONTENTS_SLIME);
	CHECK_FIELD_MUTATION(changed.waterlevel = 2);
	CHECK_FIELD_MUTATION(changed.old_frame_z = -0.0f);
#undef CHECK_FIELD_MUTATION
	changed = live_source;
	AddAngle(&changed.old_pms.delta_angles[0], 1);
	CHECK(!SG_CompoundPublicationCaptureAngleBias(&binding->source,
	                                             &changed, &bias));
	Destroy(&rune);
}

static void TestZeroCompoundIsInert(void)
{
	rune_seed_t seeds[3];
	rune_link_t links[3];
	rune_t rune;
	sg_compound_publication_result_t result;

	ResetFixture();
	rune = RuneFixture(seeds, links);
	links[0].action = RL_SWIM;
	links[0].mode = RLCM_NONE;
	links[2].action = RL_SWIM;
	links[2].mode = RLCM_NONE;
	result = Build(&rune);
	CHECK(result.status == SG_COMPOUND_PUBLICATION_OK);
	CHECK(rune.compound_publication == NULL);
	CHECK(fixture.allocation_calls == 0);
	CHECK(fixture.resolve_calls == 0);
	CHECK(fixture.enumerate_calls == 0);
	CHECK(fixture.source_calls == 0);
	CHECK(fixture.discover_calls == 0);
	CHECK(fixture.replay_calls == 0);
	CHECK(SG_CompoundPublicationCount(&rune) == 0);
	CHECK(SG_CompoundPublicationRevalidate(&rune).status ==
	      SG_COMPOUND_PUBLICATION_OK);
}

int main(void)
{
	TestPositiveDedupAndMutation();
	TestAtomicFailures();
	TestCanonicalAndCountNegatives();
	TestDeltaBiasAndExactFields();
	TestZeroCompoundIsInert();
	if (failures)
	{
		fprintf(stderr, "sg_compound_publication_test: %d failure(s)\n",
		        failures);
		return 1;
	}
	puts("sg_compound_publication_test: ok");
	return 0;
}
