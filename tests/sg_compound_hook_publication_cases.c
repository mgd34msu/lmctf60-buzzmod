#include "sg_compound_publication_fixture.h"

#include <float.h>

static void TestHookPublication(void)
{
	rune_seed_t seeds[3];
	rune_link_t links[3];
	rune_t rune;
	const sg_compound_publication_binding_t *binding;
	sg_compound_publication_binding_t *mutable_binding;
	sg_compound_publication_result_t result;
	sg_hook_replay_spec_t spec;
	fixture_hook_source_drift_t drift;

	ResetFixture();
	fixture.prepared_old_frame_z = -37.0f;
	rune = RuneFixture(seeds, links);
	Set3(seeds[0].origin, 8.0f, 16.0f, 24.0f);
	seeds[0].flags = RSF_WATER;
	seeds[1].flags = 0;
	links[0] = CompoundHookLink(0, 1);
	rune.hdr.num_links = 1;
	CHECK(SG_CompoundPublicationRevalidate(&rune).status ==
	      SG_COMPOUND_PUBLICATION_INVALID);
	result = Build(&rune);
	CHECK(result.status == SG_COMPOUND_PUBLICATION_OK);
	CHECK(SG_CompoundPublicationCount(&rune) == 1);
	binding = SG_CompoundPublicationBinding(&rune, 0);
	CHECK(binding != NULL);
	if (binding)
	{
		CHECK(binding->link.action == RL_DOOR_HOOK);
		CHECK(binding->source.pms.origin[0] == 64);
		CHECK(binding->source.pms.origin[1] == 128);
		CHECK(binding->source.pms.origin[2] == 192);
		CHECK(binding->source.old_frame_z == -37.0f);
		CHECK(binding->suffix.pms.origin[0] == 160);
		CHECK(binding->suffix.old_frame_z == -37.0f);
		CHECK(binding->touch_ms == 25);
		CHECK(binding->touch_frame_end_ms == 100);
		CHECK(binding->mover_top_ms == 500);
		CHECK(binding->suffix_start_ms == 400);
		CHECK(binding->arrival_ms == 600);
		CHECK(binding->sweep_clear_ms == 200);
		CHECK(binding->total_cost_ms == 1100);
		CHECK(SG_CompoundHookPublicationPlan(binding, &spec));
		CHECK(memcmp(&spec, &binding->hook_proof.spec, sizeof(spec)) == 0);
		CHECK(SG_CompoundPublicationRevalidate(&rune).status ==
		      SG_COMPOUND_PUBLICATION_OK);
		mutable_binding = (sg_compound_publication_binding_t *)binding;
		mutable_binding->hook_proof.spec.bite[0] += 1.0f;
		CHECK(SG_CompoundPublicationBinding(&rune, 0) == NULL);
		CHECK(SG_CompoundPublicationRevalidate(&rune).status ==
		      SG_COMPOUND_PUBLICATION_MISMATCH);
		mutable_binding->hook_proof.spec = spec;
		CHECK(SG_CompoundPublicationBinding(&rune, 0) == binding);
		mutable_binding->hook_proof.spec.view_angles[YAW] =
			SHORT2ANGLE(ANGLE2SHORT(45.0f));
		CHECK(SG_CompoundPublicationBinding(&rune, 0) == NULL);
		mutable_binding->hook_proof.spec = spec;
		CHECK(SG_CompoundPublicationBinding(&rune, 0) == binding);
		mutable_binding->hook_proof.spec.expected_release_ms = 100;
		CHECK(SG_CompoundPublicationBinding(&rune, 0) == NULL);
		mutable_binding->hook_proof.spec = spec;
	}
	Destroy(&rune);
	CHECK(fixture.live_allocations == 0);

	for (drift = HOOK_SOURCE_PMS; drift < HOOK_SOURCE_DRIFT_COUNT; drift++)
	{
		ResetFixture();
		fixture.prepared_old_frame_z = -37.0f;
		fixture.hook_source_drift = drift;
		rune = RuneFixture(seeds, links);
		Set3(seeds[0].origin, 8.0f, 16.0f, 24.0f);
		seeds[0].flags = RSF_WATER;
		seeds[1].flags = 0;
		links[0] = CompoundHookLink(0, 1);
		rune.hdr.num_links = 1;
		result = Build(&rune);
		CHECK(result.status == SG_COMPOUND_PUBLICATION_MISMATCH);
		CHECK(rune.compound_publication == NULL);
		CHECK(fixture.live_allocations == 0);
	}
}

static void TestHookAllocationFailure(void)
{
	rune_seed_t seeds[3];
	rune_link_t links[3];
	rune_t rune;

	ResetFixture();
	fixture.fail_allocation_call = 5;
	rune = RuneFixture(seeds, links);
	seeds[0].flags = RSF_WATER;
	seeds[1].flags = 0;
	links[0] = CompoundHookLink(0, 1);
	rune.hdr.num_links = 1;
	CHECK(Build(&rune).status == SG_COMPOUND_PUBLICATION_ALLOCATION);
	CHECK(rune.compound_publication == NULL);
	CHECK(fixture.live_allocations == 0);
	CHECK(fixture.free_calls == 4);
}

static void TestHookRejectsUnboundBite(void)
{
	rune_seed_t seeds[3];
	rune_link_t links[3];
	rune_t rune;

	ResetFixture();
	fixture.hook_bite_drift = true;
	rune = RuneFixture(seeds, links);
	Set3(seeds[0].origin, 8.0f, 16.0f, 24.0f);
	seeds[0].flags = RSF_WATER;
	seeds[1].flags = 0;
	links[0] = CompoundHookLink(0, 1);
	rune.hdr.num_links = 1;
	CHECK(Build(&rune).status == SG_COMPOUND_PUBLICATION_MISMATCH);
	CHECK(rune.compound_publication == NULL);
	CHECK(fixture.live_allocations == 0);
}

static void TestHookRejectsUnsafeControl(void)
{
	rune_seed_t seeds[3];
	rune_link_t links[3];
	rune_t rune;
	const float invalid_yaws[] = { NAN, FLT_MAX };
	size_t index;

	for (index = 0; index < sizeof(invalid_yaws) / sizeof(invalid_yaws[0]);
	     index++)
	{
		ResetFixture();
		rune = RuneFixture(seeds, links);
		seeds[0].flags = RSF_WATER;
		seeds[1].flags = 0;
		links[0] = CompoundHookLink(0, 1);
		links[0].anchor[YAW] = invalid_yaws[index];
		rune.hdr.num_links = 1;
		CHECK(Build(&rune).status == SG_COMPOUND_PUBLICATION_INVALID);
		CHECK(rune.compound_publication == NULL);
		CHECK(fixture.live_allocations == 0);
	}
}

void SG_CompoundHookPublicationCasesRun(void)
{
	TestHookPublication();
	TestHookAllocationFailure();
	TestHookRejectsUnboundBite();
	TestHookRejectsUnsafeControl();
}
