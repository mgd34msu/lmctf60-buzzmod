#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../g_local.h"
#include "../slipgate/sg_compound_gen_game.h"
#include "../slipgate/sg_compound_world.h"
#include "../slipgate/sg_hooks.h"
#include "../slipgate/sg_local.h"

static int failures;
static int rejected_destination = -1;
static int reject_nearest;
static int only_provable_destination = -1;
static int allocation_calls;
static int fail_allocation_call = -1;
static int crossing_min_destination;
static int mock_mechanism_count = 1;
static int reverse_mechanisms;
static sg_compound_world_preopen_t mock_world;
sg_host_t sg_host;

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		        __FILE__, __LINE__, #condition); \
		failures++; \
	} \
} while (0)

static void *Allocate(int size)
{
	allocation_calls++;
	if (allocation_calls == fail_allocation_call)
		return NULL;
	return malloc((size_t)size);
}

static void Deallocate(void *block)
{
	free(block);
}

static void Dprint(const char *format, ...)
{
	(void)format;
}

rune_reject_reason_t SG_CompoundWorldEnumeratePreopen(
	sg_compound_world_candidate_t *candidates, int capacity, int *count_out)
{
	int index;

	if (!count_out)
		return RLR_BAD_CONTROL_POLICY;
	*count_out = mock_mechanism_count;
	if (!candidates && capacity == 0)
		return RLR_OK;
	if (!candidates || capacity < mock_mechanism_count)
		return RLR_BAD_CONTROL_POLICY;
	memset(candidates, 0,
	    (size_t)mock_mechanism_count * sizeof(*candidates));
	for (index = 0; index < mock_mechanism_count; index++)
	{
		int identity = reverse_mechanisms ?
		    mock_mechanism_count - index - 1 : index;

		candidates[index].resolved = mock_world;
		candidates[index].resolved.trigger_key += identity * 2;
		candidates[index].resolved.mover_key += identity * 2;
		candidates[index].hint_count = 1;
		candidates[index].hints[0][0] = 0.125f * (float)(identity + 1);
	}
	return RLR_OK;
}

rune_reject_reason_t SG_CompoundWorldResolvePreopen(
	const float mechanism_anchor[3], sg_compound_world_preopen_t *resolved)
{
	int identity;

	if (!mechanism_anchor || !resolved || mechanism_anchor[0] < 0.125f)
		return RLR_MECHANISM_UNRESOLVED;
	*resolved = mock_world;
	identity = (int)(mechanism_anchor[0] * 8.0f) - 1;
	resolved->trigger_key += identity * 2;
	resolved->mover_key += identity * 2;
	return RLR_OK;
}

int SG_CompoundWorldCrossesSweep(
	const sg_compound_world_preopen_t *resolved,
	const vec3_t from, const vec3_t to)
{
	(void)resolved;
	(void)from;
	return to && (int)to[0] >= crossing_min_destination;
}

rune_reject_reason_t SG_OracleCompoundSwimPrepareSource(
	const vec3_t source, const sg_compound_world_preopen_t *resolved,
	float old_frame_z, sg_compound_swim_source_t *prepared,
	edict_t *passent, qboolean world_only, qboolean loader_replay)
{
	(void)source;
	(void)resolved;
	(void)old_frame_z;
	(void)passent;
	(void)world_only;
	(void)loader_replay;
	memset(prepared, 0, sizeof(*prepared));
	return RLR_OK;
}

rune_reject_reason_t SG_OracleCompoundSwimDiscoverContact(
	const sg_compound_swim_source_t *prepared,
	const sg_compound_world_preopen_t *resolved,
	const vec3_t canonical_hint, vec3_t mechanism_anchor,
	edict_t *passent, qboolean world_only, qboolean loader_replay)
{
	(void)prepared;
	(void)resolved;
	(void)passent;
	(void)world_only;
	(void)loader_replay;
	VectorCopy(canonical_hint, mechanism_anchor);
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
	int nearest = (int)(mechanism_anchor[0] + 0.5f);

	(void)phantom;
	(void)resolved;
	(void)mechanism_anchor;
	(void)destination_water;
	(void)old_frame_z;
	if (replay_reason)
		*replay_reason = SG_REPLAY_REASON_NONE;
	(void)passent;
	(void)world_only;
	(void)loader_replay;
	if (nearest < 1)
		nearest = 1;
	if ((only_provable_destination >= 0 &&
	     (int)destination[0] != only_provable_destination) ||
	    (int)destination[0] == rejected_destination ||
	    (reject_nearest && (int)destination[0] == nearest))
	{
		if (replay_reason)
			*replay_reason = SG_REPLAY_REASON_ACTION_TIMEOUT;
		return RLR_SUFFIX_REPLAY_FAILED;
	}
	memset(proof, 0, sizeof(*proof));
	proof->touch_ms = 25;
	proof->touch_frame_end_ms = 100;
	proof->mover_top_ms = 500;
	proof->suffix_start_ms = 400;
	proof->arrival_ms = 100;
	proof->sweep_clear_ms = 100;
	proof->total_cost_ms = 600;
	return RLR_OK;
}

static void Fixture(rune_seed_t *seeds, int *components,
	uint8_t *objective_masks, rune_link_t *links, size_t *link_count)
{
	size_t index;

	memset(seeds, 0, 300U * sizeof(*seeds));
	memset(components, 0, 300U * sizeof(*components));
	memset(objective_masks, 0, 300U * sizeof(*objective_masks));
	memset(links, 0, 400U * sizeof(*links));
	seeds[0].flags = RSF_WATER;
	for (index = 1U; index < 300U; index++)
	{
		seeds[index].origin[0] = (float)index;
		links[index - 1U].from = (int)index;
		links[index - 1U].to = 0;
		if (index <= 100U)
			objective_masks[index] = 1U;
		else if (index <= 200U)
			objective_masks[index] = 2U;
		else
			components[index] = 1;
	}
	*link_count = 299U;
}

static sg_compound_gen_game_result_t Build(rune_seed_t *seeds,
	int *components, uint8_t *objective_masks, rune_link_t *links,
	size_t *link_count, size_t link_capacity)
{
	sg_compound_gen_game_request_t request;

	allocation_calls = 0;
	memset(&request, 0, sizeof(request));
	request.seeds = seeds;
	request.seed_count = 300U;
	request.links = links;
	request.link_count = link_count;
	request.link_capacity = link_capacity;
	request.components = components;
	request.objective_masks = objective_masks;
	request.allocate = Allocate;
	request.deallocate = Deallocate;
	return SG_CompoundGenGameBuild(&request);
}

static void TestBoundedCategoriesAndFallback(void)
{
	rune_seed_t seeds[300];
	int components[300];
	uint8_t objective_masks[300];
	rune_link_t links[400];
	size_t link_count;
	sg_compound_gen_game_result_t result;

	Fixture(seeds, components, objective_masks, links, &link_count);
	mock_mechanism_count = 1;
	reverse_mechanisms = 0;
	reject_nearest = 0;
	rejected_destination = 1;
	result = Build(seeds, components, objective_masks, links, &link_count, 320U);
	CHECK(result.status == SG_COMPOUND_GEN_OK);
	CHECK(result.candidates == 299U);
	CHECK(result.proof_calls == 299U);
	CHECK(result.proof_rejection == RLR_SUFFIX_REPLAY_FAILED);
	CHECK(result.proof_rejections == 1U);
	CHECK(result.replay_rejection == SG_REPLAY_REASON_ACTION_TIMEOUT);
	CHECK(result.replay_rejections == 1U);
	CHECK(result.emitted == 3U && link_count == 302U);
	CHECK(links[299].to == 2);
	CHECK(links[300].to == 101);
	CHECK(links[301].to == 201);
}

static void TestLocalShortcutWithoutTopologyGain(void)
{
	rune_seed_t seeds[300];
	int components[300];
	uint8_t objective_masks[300];
	rune_link_t links[400];
	size_t link_count;
	sg_compound_gen_game_result_t result;
	size_t index;

	Fixture(seeds, components, objective_masks, links, &link_count);
	for (index = 0U; index < 300U; index++)
	{
		components[index] = 0;
		objective_masks[index] = 3U;
	}
	mock_mechanism_count = 1;
	reverse_mechanisms = 0;
	reject_nearest = 0;
	rejected_destination = -1;
	crossing_min_destination = 5;
	result = Build(seeds, components, objective_masks, links, &link_count, 400U);
	CHECK(result.status == SG_COMPOUND_GEN_OK);
	CHECK(result.candidates == 295U && result.proof_calls == 295U);
	CHECK(result.emitted == 1U && links[299].to == 5);
	crossing_min_destination = 0;
}

static void TestDeterministicAndAtomicCapacity(void)
{
	rune_seed_t seeds[300];
	int components[300];
	uint8_t objective_masks[300];
	rune_link_t first[400], second[400], before[400];
	size_t first_count, second_count;
	sg_compound_gen_game_result_t first_result, second_result;

	Fixture(seeds, components, objective_masks, first, &first_count);
	memcpy(second, first, sizeof(second));
	second_count = first_count;
	rejected_destination = -1;
	reject_nearest = 0;
	mock_mechanism_count = 1;
	reverse_mechanisms = 0;
	first_result = Build(seeds, components, objective_masks, first,
	    &first_count, 320U);
	second_result = Build(seeds, components, objective_masks, second,
	    &second_count, 320U);
	CHECK(first_result.status == SG_COMPOUND_GEN_OK);
	CHECK(second_result.status == SG_COMPOUND_GEN_OK);
	CHECK(first_count == second_count);
	CHECK(memcmp(first, second, first_count * sizeof(first[0])) == 0);

	Fixture(seeds, components, objective_masks, first, &first_count);
	memcpy(before, first, sizeof(before));
	first_result = Build(seeds, components, objective_masks, first,
	    &first_count, first_count);
	CHECK(first_result.status == SG_COMPOUND_GEN_CAPACITY);
	CHECK(first_result.emitted == 0U && first_count == 299U);
	CHECK(memcmp(first, before, sizeof(first)) == 0);
}

static void TestThirtyFiveContactsAreExhaustiveAndDeterministic(void)
{
	rune_seed_t seeds[300];
	int components[300];
	uint8_t objective_masks[300];
	rune_link_t forward[400], reverse[400];
	size_t forward_count, reverse_count;
	sg_compound_gen_game_result_t forward_result, reverse_result;
	size_t index;

	Fixture(seeds, components, objective_masks, forward, &forward_count);
	memcpy(reverse, forward, sizeof(reverse));
	reverse_count = forward_count;
	mock_mechanism_count = 35;
	rejected_destination = -1;
	reject_nearest = 1;
	reverse_mechanisms = 0;
	forward_result = Build(seeds, components, objective_masks, forward,
	    &forward_count, 400U);
	reverse_mechanisms = 1;
	reverse_result = Build(seeds, components, objective_masks, reverse,
	    &reverse_count, 400U);
	CHECK(forward_result.status == SG_COMPOUND_GEN_OK);
	CHECK(reverse_result.status == SG_COMPOUND_GEN_OK);
	CHECK(forward_result.candidates == 35U * 299U);
	CHECK(forward_result.candidates == reverse_result.candidates);
	CHECK(forward_result.proof_calls == forward_result.candidates);
	CHECK(forward_result.candidates > 0U);
	CHECK(forward_result.selected > 0U);
	CHECK(forward_result.emitted > 0U);
	CHECK(forward_count == reverse_count);
	CHECK(memcmp(forward, reverse,
	    forward_count * sizeof(forward[0])) == 0);
	for (index = 299U; index < forward_count; index++)
		CHECK(forward[index].to !=
		    (int)(forward[index].mechanism_anchor[0] + 0.5f));
}

static void TestProofBeyondFormerQuotaIsReached(void)
{
	rune_seed_t seeds[300];
	int components[300];
	uint8_t objective_masks[300];
	rune_link_t links[400];
	size_t link_count;
	sg_compound_gen_game_result_t result;
	size_t index;

	Fixture(seeds, components, objective_masks, links, &link_count);
	for (index = 0U; index < 300U; index++)
	{
		components[index] = 0;
		objective_masks[index] = 3U;
	}
	mock_mechanism_count = 1;
	reverse_mechanisms = 0;
	rejected_destination = -1;
	reject_nearest = 0;
	only_provable_destination = 299;
	result = Build(seeds, components, objective_masks, links, &link_count, 400U);
	CHECK(result.status == SG_COMPOUND_GEN_OK);
	CHECK(result.candidates == 299U);
	CHECK(result.proof_calls == 299U);
	CHECK(result.emitted == 1U && link_count == 300U);
	CHECK(links[299].to == 299);
	only_provable_destination = -1;
}

static void TestAllocationFailureIsAtomic(void)
{
	rune_seed_t seeds[300];
	int components[300];
	uint8_t objective_masks[300];
	rune_link_t links[400], before[400];
	size_t link_count;
	sg_compound_gen_game_result_t result;

	Fixture(seeds, components, objective_masks, links, &link_count);
	memcpy(before, links, sizeof(before));
	mock_mechanism_count = 1;
	reverse_mechanisms = 0;
	rejected_destination = -1;
	reject_nearest = 0;
	only_provable_destination = -1;
	fail_allocation_call = 2;
	result = Build(seeds, components, objective_masks, links, &link_count, 400U);
	CHECK(result.status != SG_COMPOUND_GEN_OK);
	CHECK(result.proof_calls == 0U && result.emitted == 0U);
	CHECK(link_count == 299U);
	CHECK(memcmp(links, before, sizeof(links)) == 0);
	fail_allocation_call = -1;
}

static void TestContactsBeyondLegacyCapAreExhausted(void)
{
	rune_seed_t seeds[300];
	int components[300];
	uint8_t objective_masks[300];
	rune_link_t links[400];
	size_t link_count;
	sg_compound_gen_game_result_t result;

	Fixture(seeds, components, objective_masks, links, &link_count);
	mock_mechanism_count = 90;
	reverse_mechanisms = 0;
	rejected_destination = -1;
	reject_nearest = 0;
	result = Build(seeds, components, objective_masks, links, &link_count,
	    400U);
	CHECK(result.status == SG_COMPOUND_GEN_OK);
	CHECK(result.candidates == 90U * 299U);
	CHECK(result.proof_calls == result.candidates);
	CHECK(result.selected > 256U);
	CHECK(result.emitted > 0U && link_count > 299U);
}

static void TestProductionWrapperEnabled(void)
{
	rune_seed_t seeds[300];
	rune_link_t links[400];
	int components[300], link_count;
	uint8_t objective_masks[300];
	sg_compound_gen_game_topology_t topology;
	size_t fixture_count;

	Fixture(seeds, components, objective_masks, links, &fixture_count);
	link_count = (int)fixture_count;
	topology.component = components;
	topology.objective_mask = objective_masks;
	mock_mechanism_count = 1;
	rejected_destination = -1;
	reject_nearest = 0;
	sg_host.dprint = Dprint;
	CHECK(SG_ActionMechanismAdmitted(RL_DOOR_SWIM));
	CHECK(SG_CompoundGenGameGenerate(seeds, 300U, links, &link_count, 400U,
	    &topology, Allocate, Deallocate));
	CHECK(link_count > (int)fixture_count);
}

static void TestProductionWrapperAcceptsExhaustedNoProof(void)
{
	rune_seed_t seeds[300];
	rune_link_t links[400], before[400];
	int components[300], link_count;
	uint8_t objective_masks[300];
	sg_compound_gen_game_topology_t topology;
	size_t fixture_count;

	Fixture(seeds, components, objective_masks, links, &fixture_count);
	memcpy(before, links, sizeof(before));
	link_count = (int)fixture_count;
	topology.component = components;
	topology.objective_mask = objective_masks;
	mock_mechanism_count = 1;
	rejected_destination = -1;
	reject_nearest = 0;
	only_provable_destination = 999;
	allocation_calls = 0;
	CHECK(SG_CompoundGenGameGenerate(seeds, 300U, links, &link_count, 400U,
	    &topology, Allocate, Deallocate));
	CHECK(link_count == (int)fixture_count);
	CHECK(memcmp(links, before, sizeof(links)) == 0);
	only_provable_destination = -1;
	fail_allocation_call = 2;
	allocation_calls = 0;
	CHECK(!SG_CompoundGenGameGenerate(seeds, 300U, links, &link_count, 400U,
	    &topology, Allocate, Deallocate));
	CHECK(link_count == (int)fixture_count);
	CHECK(memcmp(links, before, sizeof(links)) == 0);
	fail_allocation_call = -1;
}

int main(void)
{
	memset(&mock_world, 0, sizeof(mock_world));
	mock_world.trigger_key = 40;
	mock_world.mover_key = 41;
	TestBoundedCategoriesAndFallback();
	TestLocalShortcutWithoutTopologyGain();
	TestDeterministicAndAtomicCapacity();
	TestThirtyFiveContactsAreExhaustiveAndDeterministic();
	TestProofBeyondFormerQuotaIsReached();
	TestAllocationFailureIsAtomic();
	TestContactsBeyondLegacyCapAreExhausted();
	TestProductionWrapperEnabled();
	TestProductionWrapperAcceptsExhaustedNoProof();
	if (failures)
	{
		fprintf(stderr, "sg_compound_gen_game_test: %d failures\n", failures);
		return 1;
	}
	puts("sg_compound_gen_game_test: ok");
	return 0;
}
