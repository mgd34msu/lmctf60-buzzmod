#include "sg_compound_oracle_fixture.h"

static void TestTouchSubsteps(void)
{
	const vec3_t mechanism = { 160.0f, 0.0f, 0.0f };
	const vec3_t destination = { -80.0f, 0.0f, 0.0f };
	int touch;

	for (touch = 1; touch <= 4; touch++)
	{
		fixture_config_t config =
			DefaultConfig(touch, FIXTURE_SUFFIX_SUCCESS);
		sg_compound_world_preopen_t resolved;
		sg_compound_swim_proof_t proof;
		sg_phantom_t phantom;
		edict_t member_before;
		rune_reject_reason_t result;
		int expected_zero = (4 - touch) + 16;
		int link;

		ResetFixture(&config);
		CHECK(Resolve(&resolved) == RLR_OK);
		member_before = fixture_edicts[1];
		InitPhantom(&phantom, false);
		result = SG_OracleCompoundSwimPreopen(&phantom, &resolved,
			mechanism, destination, true, 0.0f, &proof, NULL, true,
			false);
		if (result != RLR_OK)
			fprintf(stderr, "touch %d result %d calls %d approach %d zero %d suffix %d links %d\n",
			        touch, result, fixture_observation.pmove_calls,
			        fixture_observation.approach_commands,
			        fixture_observation.zero_commands,
			        fixture_observation.suffix_commands,
			        fixture_observation.link_calls);
		CHECK(result == RLR_OK);
		CHECK(proof.touch_ms == touch * 25);
		CHECK(proof.touch_frame_end_ms == 100);
		CHECK(proof.mover_top_ms == 500);
		CHECK(proof.suffix_start_ms == 400);
		CHECK(proof.sweep_clear_ms == 200);
		CHECK(proof.arrival_ms == 300);
		CHECK(proof.total_cost_ms == 800);
		CHECK(proof.suffix_pms.origin[0] == 160 * 8);
		CHECK(proof.suffix_pms.gravity == 777);
		CHECK(proof.suffix_pms.delta_angles[YAW] == 321);
		CHECK(memcmp(&proof.suffix_pms, &proof.suffix_old_pms,
		             sizeof(proof.suffix_pms)) == 0);
		CHECK(proof.suffix_origin[0] == 160.0f);
		CHECK(proof.suffix_velocity[1] == 8.0f);
		CHECK(proof.suffix_watertype == CONTENTS_WATER);
		CHECK(proof.suffix_waterlevel == 3);
		CHECK(proof.suffix_old_frame_z == 0.0f);
		CHECK(fixture_observation.zero_commands == expected_zero);
		CHECK(fixture_observation.ride_zero_commands == 16);
		CHECK(fixture_observation.pmove_calls == 32);
		CHECK(proof.total_cost_ms ==
		      fixture_observation.pmove_calls * 25);
		CHECK(fixture_observation.suffix_commands == 12);
		CHECK(fixture_observation.first_snapinitial);
		CHECK(fixture_observation.later_snapinitial == 0);
		CHECK(fixture_observation.first_top_seen);
		CHECK(fixture_observation.pretop_contact_traces == 0);
		CHECK(fixture_observation.first_top_command.msec == 25);
		CHECK(!CommandZero(&fixture_observation.first_top_command));
		CHECK(fixture_observation.link_calls == 6);
		for (link = 0; link < 5; link++)
			CHECK(fixture_observation.link_origins[link] ==
			      (link == 0 ? 0.0f : (float)(link * 20)));
		CHECK(fixture_observation.link_origins[5] == 0.0f);
		CHECK(MemberRestored(&fixture_edicts[1], &member_before));
		CHECK(fixture_observation.callback_calls == 0);
		CheckStaticContextRestored();
	}
}

static void RunFailure(const fixture_config_t *config,
	rune_reject_reason_t expected, const vec3_t destination,
	qboolean damaging_fall)
{
	vec3_t mechanism;
	sg_compound_world_preopen_t resolved;
	sg_compound_swim_proof_t proof;
	sg_phantom_t phantom;
	edict_t member_before;
	byte zero[sizeof(proof)];

	memset(zero, 0, sizeof(zero));
	ResetFixture(config);
	Set3(mechanism, config->mechanism_x, 0.0f, 0.0f);
	CHECK(Resolve(&resolved) == RLR_OK);
	member_before = fixture_edicts[1];
	InitPhantom(&phantom, damaging_fall);
	CHECK(SG_OracleCompoundSwimPreopen(&phantom, &resolved, mechanism,
		destination, true, damaging_fall ? -1000.0f : 0.0f,
		&proof, NULL, true, false) == expected);
	CHECK(memcmp(&proof, zero, sizeof(proof)) == 0);
	CHECK(MemberRestored(&fixture_edicts[1], &member_before));
	CHECK(fixture_observation.callback_calls == 0);
	CheckStaticContextRestored();
}

static void TestFailureTable(void)
{
	const vec3_t destination = { -80.0f, 0.0f, 0.0f };
	const vec3_t inside_destination = { 60.0f, 0.0f, 0.0f };
	const vec3_t outside_destination = { 240.0f, 0.0f, 0.0f };
	fixture_config_t no_sweep =
		DefaultConfig(2, FIXTURE_SUFFIX_NO_SWEEP);
	fixture_config_t reentry =
		DefaultConfig(2, FIXTURE_SUFFIX_REENTRY);
	fixture_config_t arrival_before =
		DefaultConfig(2, FIXTURE_SUFFIX_ARRIVE_BEFORE_CLEAR);
	fixture_config_t always_outside =
		DefaultConfig(2, FIXTURE_SUFFIX_ALWAYS_OUTSIDE);
	fixture_config_t between_recross =
		DefaultConfig(2, FIXTURE_SUFFIX_BETWEEN_RECROSS);
	fixture_config_t trigger_contamination =
		DefaultConfig(2, FIXTURE_SUFFIX_SUCCESS);
	fixture_config_t fanout =
		DefaultConfig(2, FIXTURE_SUFFIX_SUCCESS);
	fixture_config_t hazard =
		DefaultConfig(2, FIXTURE_SUFFIX_SUCCESS);
	fixture_config_t fall =
		DefaultConfig(2, FIXTURE_SUFFIX_SUCCESS);
	fixture_config_t wrong_contact =
		DefaultConfig(2, FIXTURE_SUFFIX_SUCCESS);
	fixture_config_t inside_approach =
		DefaultConfig(2, FIXTURE_SUFFIX_SUCCESS);
	fixture_config_t crossing_approach =
		DefaultConfig(1, FIXTURE_SUFFIX_SUCCESS);
	fixture_config_t opening_drift =
		DefaultConfig(2, FIXTURE_SUFFIX_SUCCESS);

	trigger_contamination.contaminate_trigger = true;
	fanout.contaminate_solid = true;
	hazard.hazard_ride = true;
	fall.fall_ride = true;
	wrong_contact.wrong_contact = true;
	inside_approach.source_x = 0.0f;
	crossing_approach.source_x = -80.0f;
	opening_drift.opening_drift = true;

	RunFailure(&no_sweep, RLR_SUFFIX_REPLAY_FAILED, destination, false);
	RunFailure(&reentry, RLR_CLEAR_MISMATCH, destination, false);
	RunFailure(&arrival_before, RLR_CLEAR_MISMATCH,
	           inside_destination, false);
	RunFailure(&always_outside, RLR_CLEAR_MISMATCH,
	           outside_destination, false);
	RunFailure(&between_recross, RLR_CLEAR_MISMATCH,
	           destination, false);
	RunFailure(&trigger_contamination, RLR_APPROACH_REPLAY_FAILED,
	           destination, false);
	RunFailure(&fanout, RLR_APPROACH_REPLAY_FAILED, destination, false);
	RunFailure(&hazard, RLR_RIDE_REPLAY_FAILED, destination, false);
	RunFailure(&fall, RLR_RIDE_REPLAY_FAILED, destination, true);
	RunFailure(&wrong_contact, RLR_APPROACH_REPLAY_FAILED,
	           destination, false);
	RunFailure(&inside_approach, RLR_APPROACH_REPLAY_FAILED,
	           destination, false);
	RunFailure(&crossing_approach, RLR_APPROACH_REPLAY_FAILED,
	           destination, false);
	RunFailure(&opening_drift, RLR_RIDE_REPLAY_FAILED,
	           destination, false);
}

static void TestPreopenSweepChordBoundaries(void)
{
	const vec3_t mechanism = { 160.0f, 0.0f, 0.0f };
	const vec3_t destination = { -80.0f, 0.0f, 0.0f };
	fixture_config_t preclear =
		DefaultConfig(2, FIXTURE_SUFFIX_PRECLEAR_CHORD);
	fixture_config_t postclear =
		DefaultConfig(2, FIXTURE_SUFFIX_POSTCLEAR_CHORD);
	sg_compound_world_preopen_t resolved;
	sg_compound_swim_proof_t proof;
	sg_phantom_t phantom;
	edict_t member_before;

	ResetFixture(&preclear);
	CHECK(Resolve(&resolved) == RLR_OK);
	member_before = fixture_edicts[1];
	InitPhantom(&phantom, false);
	CHECK(SG_OracleCompoundSwimPreopen(&phantom, &resolved, mechanism,
	      destination, true, 0.0f, &proof, NULL, true, false) == RLR_OK);
	CHECK(proof.sweep_clear_ms == 100);
	CHECK(proof.arrival_ms == 100);
	CHECK(proof.total_cost_ms == 600);
	CHECK(MemberRestored(&fixture_edicts[1], &member_before));
	CheckStaticContextRestored();

	RunFailure(&postclear, RLR_CLEAR_MISMATCH, destination, false);
}

static void TestResolvedIdentityFailsClosed(void)
{
	const vec3_t mechanism = { 160.0f, 0.0f, 0.0f };
	const vec3_t destination = { -80.0f, 0.0f, 0.0f };
	fixture_config_t config =
		DefaultConfig(2, FIXTURE_SUFFIX_SUCCESS);
	sg_compound_world_preopen_t resolved;
	sg_compound_swim_proof_t proof;
	sg_phantom_t phantom;

	ResetFixture(&config);
	CHECK(Resolve(&resolved) == RLR_OK);
	resolved.mover_key = 4;
	InitPhantom(&phantom, false);
	CHECK(SG_OracleCompoundSwimPreopen(&phantom, &resolved, mechanism,
		destination, true, 0.0f, &proof, NULL, true, false) ==
	      RLR_MECHANISM_UNRESOLVED);
	CHECK(fixture_observation.pmove_calls == 0);
	CHECK(fixture_observation.link_calls == 0);
}

static void TestApproachArrivalSuppressedUntilTouch(void)
{
	const vec3_t mechanism = { 160.0f, 0.0f, 0.0f };
	const vec3_t destination = { -80.0f, 0.0f, 0.0f };
	fixture_config_t delayed =
		DefaultConfig(5, FIXTURE_SUFFIX_SUCCESS);
	fixture_config_t near =
		DefaultConfig(1, FIXTURE_SUFFIX_SUCCESS);
	fixture_config_t configs[2];
	int expected_touch[2] = { 125, 25 };
	int index;

	near.source_x = 123.0f;
	configs[0] = delayed;
	configs[1] = near;
	for (index = 0; index < 2; index++)
	{
		sg_compound_world_preopen_t resolved;
		sg_compound_swim_proof_t proof;
		sg_phantom_t phantom;

		ResetFixture(&configs[index]);
		CHECK(Resolve(&resolved) == RLR_OK);
		InitPhantom(&phantom, false);
		CHECK(SG_OracleCompoundSwimPreopen(&phantom, &resolved,
			mechanism, destination, true, 0.0f, &proof, NULL, true,
			false) == RLR_OK);
		CHECK(proof.touch_ms == expected_touch[index]);
		CHECK(proof.touch_frame_end_ms == (index == 0 ? 200 : 100));
		CHECK(proof.total_cost_ms == (index == 0 ? 900 : 800));
		CHECK(proof.total_cost_ms ==
		      fixture_observation.pmove_calls * 25);
	}
}

static void TestPrepareSource(void)
{
	fixture_config_t config =
		DefaultConfig(5, FIXTURE_SUFFIX_SUCCESS);
	sg_compound_world_preopen_t resolved;
	sg_compound_swim_source_t prepared;
	sg_compound_swim_source_t zero;
	vec3_t source;

	memset(&zero, 0, sizeof(zero));
	ResetFixture(&config);
	CHECK(Resolve(&resolved) == RLR_OK);
	Set3(source, config.source_x, 0.0f, 0.0f);
	CHECK(SG_OracleCompoundSwimPrepareSource(source, &resolved, -17.0f,
	      &prepared, NULL, true, false) == RLR_OK);
	CHECK(prepared.old_frame_z == -17.0f);
	CHECK(prepared.phantom.pms.origin[0] == (short)(source[0] * 8.0f));
	CHECK(prepared.phantom.old_pms.origin[0] ==
	      prepared.phantom.pms.origin[0]);
	CHECK(prepared.phantom.origin[0] == source[0]);
	CHECK(prepared.phantom.waterlevel == 3);
	CHECK(prepared.phantom.watertype == CONTENTS_WATER);
	CHECK(fixture_observation.pmove_calls == 1);
	CHECK(fixture_observation.zero_commands == 1);
	CHECK(fixture_observation.callback_calls == 0);

	config.source_hazard = true;
	ResetFixture(&config);
	CHECK(Resolve(&resolved) == RLR_OK);
	CHECK(SG_OracleCompoundSwimPrepareSource(source, &resolved, 0.0f,
	      &prepared, NULL, true, false) == RLR_APPROACH_REPLAY_FAILED);
	CHECK(memcmp(&prepared, &zero, sizeof(prepared)) == 0);
	config.source_hazard = false;
	config.source_dry = true;
	ResetFixture(&config);
	CHECK(Resolve(&resolved) == RLR_OK);
	CHECK(SG_OracleCompoundSwimPrepareSource(source, &resolved, 0.0f,
	      &prepared, NULL, true, false) == RLR_APPROACH_REPLAY_FAILED);
	CHECK(memcmp(&prepared, &zero, sizeof(prepared)) == 0);
}

static void RunContactDiscovery(float source_x, int touch_substep,
	int expected_commands)
{
	fixture_config_t config =
		DefaultConfig(touch_substep, FIXTURE_SUFFIX_SUCCESS);
	sg_compound_world_preopen_t resolved;
	sg_compound_swim_source_t prepared;
	sg_compound_swim_source_t before;
	vec3_t source, hint, anchor;
	short original_pms_x;
	rune_reject_reason_t result;

	config.source_x = source_x;
	ResetFixture(&config);
	CHECK(CanonicalHint(&resolved, hint));
	Set3(source, source_x, 0.0f, 0.0f);
	CHECK(SG_OracleCompoundSwimPrepareSource(source, &resolved, -17.0f,
	      &prepared, NULL, true, false) == RLR_OK);
	original_pms_x = prepared.phantom.pms.origin[0];
	prepared.phantom.old_pms.origin[0] += 8;
	CHECK(prepared.phantom.pms.origin[0] == original_pms_x);
	before = prepared;
	memset(&fixture_observation, 0, sizeof(fixture_observation));
	result = SG_OracleCompoundSwimDiscoverContact(&prepared, &resolved, hint,
	      anchor, NULL, true, false);
	if (result != RLR_OK)
		fprintf(stderr, "discovery source %.1f touch %d result %d calls %d approach %d hint %.3f\n",
		        source_x, touch_substep, result,
		        fixture_observation.pmove_calls,
		        fixture_observation.approach_commands, hint[0]);
	CHECK(result == RLR_OK);
	CHECK(anchor[0] == config.mechanism_x);
	CHECK(anchor[1] == 0.0f && !signbit(anchor[1]));
	CHECK(anchor[2] == 0.0f && !signbit(anchor[2]));
	CHECK(anchor[0] * 8.0f == floorf(anchor[0] * 8.0f));
	CHECK(memcmp(&prepared, &before, sizeof(prepared)) == 0);
	CHECK(fixture_observation.approach_commands == expected_commands);
	CHECK(fixture_observation.pmove_calls == expected_commands);
	CHECK(fixture_observation.first_snapinitial);
	CHECK(fixture_observation.later_snapinitial == 1);
	CHECK(fixture_observation.pretop_contact_traces == 0);
	CHECK(fixture_observation.callback_calls == 0);
	CheckStaticContextRestored();
}

static void TestContactDiscovery(void)
{
	RunContactDiscovery(200.0f, 5, 10);
	RunContactDiscovery(123.0f, 1, 2);
}

static void TestContactDiscoveryRejectsNonFixedReplay(void)
{
	fixture_config_t config =
		DefaultConfig(5, FIXTURE_SUFFIX_SUCCESS);
	sg_compound_world_preopen_t resolved;
	sg_compound_swim_source_t prepared;
	vec3_t source, hint, anchor, bad_hint;
	byte zero[sizeof(anchor)];

	memset(zero, 0, sizeof(zero));
	config.unstable_contact = true;
	ResetFixture(&config);
	CHECK(CanonicalHint(&resolved, hint));
	Set3(source, config.source_x, 0.0f, 0.0f);
	CHECK(SG_OracleCompoundSwimPrepareSource(source, &resolved, 0.0f,
	      &prepared, NULL, true, false) == RLR_OK);
	memset(&fixture_observation, 0, sizeof(fixture_observation));
	CHECK(SG_OracleCompoundSwimDiscoverContact(&prepared, &resolved, hint,
	      anchor, NULL, true, false) == RLR_APPROACH_REPLAY_FAILED);
	CHECK(memcmp(anchor, zero, sizeof(anchor)) == 0);
	CHECK(fixture_observation.approach_commands == 10);

	config.unstable_contact = false;
	ResetFixture(&config);
	CHECK(CanonicalHint(&resolved, hint));
	CHECK(SG_OracleCompoundSwimPrepareSource(source, &resolved, 0.0f,
	      &prepared, NULL, true, false) == RLR_OK);
	VectorCopy(hint, bad_hint);
	bad_hint[0] += 0.125f;
	memset(&fixture_observation, 0, sizeof(fixture_observation));
	CHECK(SG_OracleCompoundSwimDiscoverContact(&prepared, &resolved,
	      bad_hint, anchor, NULL, true, false) ==
	      RLR_BAD_MECHANISM_ANCHOR);
	CHECK(memcmp(anchor, zero, sizeof(anchor)) == 0);
	CHECK(fixture_observation.pmove_calls == 0);

	prepared.phantom.armed_door_count = 1;
	memset(&fixture_observation, 0, sizeof(fixture_observation));
	CHECK(SG_OracleCompoundSwimDiscoverContact(&prepared, &resolved, hint,
	      anchor, NULL, true, false) == RLR_APPROACH_REPLAY_FAILED);
	CHECK(memcmp(anchor, zero, sizeof(anchor)) == 0);
	CHECK(fixture_observation.pmove_calls == 0);
}

static void TestLoaderReplayNativeMask(void)
{
	const vec3_t mechanism = { 160.0f, 0.0f, 0.0f };
	const vec3_t destination = { -80.0f, 0.0f, 0.0f };
	fixture_config_t config =
		DefaultConfig(2, FIXTURE_SUFFIX_SUCCESS);
	sg_compound_world_preopen_t resolved;
	sg_compound_swim_proof_t proof;
	sg_phantom_t phantom;
	usercmd_t command;

	ResetFixture(&config);
	CHECK(Resolve(&resolved) == RLR_OK);
	InitPhantom(&phantom, false);
	CHECK(SG_OracleCompoundSwimPreopen(&phantom, &resolved, mechanism,
		destination, true, 0.0f, &proof, NULL, true, true) == RLR_OK);
	CHECK(fixture_observation.normal_pmove_masks > 0);
	CHECK(fixture_observation.stripped_pmove_masks == 0);

	fixture_observation.last_pmove_mask = 0;
	InitPhantom(&phantom, false);
	memset(&command, 0, sizeof(command));
	command.msec = 25;
	SG_OracleRun(&phantom, &command, 1);
	CHECK(fixture_observation.last_pmove_mask == MASK_PLAYERSOLID);
}

static void TestPublicSwimTraverseRegression(void)
{
	const vec3_t destination = { 160.0f, 0.0f, 0.0f };
	fixture_config_t config =
		DefaultConfig(99, FIXTURE_SUFFIX_SUCCESS);
	sg_swim_proof_t proof;
	sg_phantom_t phantom;

	ResetFixture(&config);
	InitPhantom(&phantom, false);
	CHECK(SG_OracleSwimTraverse(&phantom, destination, true, 0.0f,
	                            &proof, NULL, false));
	CHECK(proof.arrival_ms == 100);
	CHECK(fixture_observation.pmove_calls == 4);
	CHECK(fixture_observation.first_snapinitial);
	CHECK(fixture_observation.later_snapinitial == 0);
}

int SG_CompoundSwimPreopenCasesRun(void)
{
	int before = failures;

	TestTouchSubsteps();
	TestFailureTable();
	TestPreopenSweepChordBoundaries();
	TestResolvedIdentityFailsClosed();
	TestApproachArrivalSuppressedUntilTouch();
	TestPrepareSource();
	TestContactDiscovery();
	TestContactDiscoveryRejectsNonFixedReplay();
	TestLoaderReplayNativeMask();
	TestPublicSwimTraverseRegression();
	return failures - before;
}
