#include "sg_compound_publication_fixture.h"

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

static void TestDropPublication(void)
{
	rune_seed_t seeds[3];
	rune_link_t links[3];
	rune_t rune;
	const sg_compound_publication_binding_t *binding;
	sg_compound_publication_result_t result;

	ResetFixture();
	rune = RuneFixture(seeds, links);
	seeds[0].flags = 0;
	seeds[1].flags = 0;
	links[0] = CompoundDropLink(0, 1);
	rune.hdr.num_links = 1;
	result = Build(&rune);
	CHECK(result.status == SG_COMPOUND_PUBLICATION_OK);
	CHECK(SG_CompoundPublicationCount(&rune) == 1);
	binding = SG_CompoundPublicationBinding(&rune, 0);
	CHECK(binding != NULL);
	if (binding)
	{
		CHECK(binding->link.action == RL_DOOR_DROP);
		CHECK(binding->source.grounded);
		CHECK(binding->source.waterlevel == 0);
		CHECK(binding->suffix.old_frame_z == 1.0f);
		CHECK(binding->touch_ms == 25);
		CHECK(binding->arrival_ms == 300);
	}
	CHECK(SG_CompoundPublicationRevalidate(&rune).status ==
	      SG_COMPOUND_PUBLICATION_OK);
	Destroy(&rune);
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

void SG_CompoundPublicationCoreCasesRun(void)
{
	TestPositiveDedupAndMutation();
	TestDropPublication();
	TestAtomicFailures();
	TestCanonicalAndCountNegatives();
	TestDeltaBiasAndExactFields();
	TestZeroCompoundIsInert();
}
