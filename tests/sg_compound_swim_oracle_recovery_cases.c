#include "sg_compound_oracle_fixture.h"

static qboolean RecoveryProofZero(
	const sg_compound_swim_recovery_proof_t *proof)
{
	sg_compound_swim_recovery_proof_t zero;

	memset(&zero, 0, sizeof(zero));
	return memcmp(proof, &zero, sizeof(zero)) == 0;
}

static void TestRecoveryFromLiveTop(void)
{
	const vec3_t destination = { -80.0f, 0.0f, 0.0f };
	static const int suffix_steps[] = { 1, 4, 5 };
	static const int expected_clear[] = { 200, 100, 100 };
	static const int expected_arrival[] = { 200, 200, 100 };
	int index;

	for (index = 0; index < 3; index++)
	{
		fixture_config_t config = DefaultConfig(2, FIXTURE_SUFFIX_SUCCESS);
		sg_compound_world_preopen_t resolved;
		sg_compound_swim_recovery_proof_t proof;
		sg_phantom_t phantom;
		edict_t member_before;
		edict_t *passent;

		ResetFixture(&config);
		CHECK(Resolve(&resolved) == RLR_OK);
		passent = InitRecoveryState(&phantom, &resolved,
		                            suffix_steps[index]);
		member_before = fixture_edicts[1];
		CHECK(SG_OracleCompoundSwimRecover(&phantom, &resolved, destination,
		      true, 0.0f, &proof, passent) == RLR_OK);
		CHECK(proof.sweep_clear_ms == expected_clear[index]);
		CHECK(proof.arrival_ms == expected_arrival[index]);
		CHECK(proof.sweep_clear_ms <= proof.arrival_ms);
		CHECK(fixture_observation.first_snapinitial);
		CHECK(fixture_observation.normal_pmove_masks > 0);
		CHECK(fixture_observation.stripped_pmove_masks == 0);
		CHECK(memcmp(&fixture_edicts[1], &member_before,
		             sizeof(member_before)) == 0);
		CHECK(fixture_observation.callback_calls == 0);
		CheckStaticContextRestored();
	}
}

static void TestRecoveryAcceptsStockTopAxialResidual(void)
{
	const vec3_t destination = { -80.0f, 0.0f, 0.0f };
	fixture_config_t config = DefaultConfig(2, FIXTURE_SUFFIX_SUCCESS);
	sg_compound_world_preopen_t resolved;
	sg_compound_swim_recovery_proof_t proof;
	sg_phantom_t phantom;
	edict_t member_before;
	edict_t *member;
	edict_t *passent;

	ResetFixture(&config);
	member = &fixture_edicts[1];
	/* At the binary32 server-time boundary, a stock 10 u/s pusher can run one
	 * extra 1-unit frame before Move_Final.  The direction-specific completion
	 * callback authenticates that live motion-axis terminal; the fixed axes and
	 * every copied static identity field must still match exactly. */
	member->moveinfo.speed = 10.0f;
	member->moveinfo.accel = 10.0f;
	member->moveinfo.decel = 10.0f;
	CHECK(Resolve(&resolved) == RLR_OK);
	passent = InitRecoveryState(&phantom, &resolved, 4);
	member->s.origin[resolved.axis] =
		resolved.top_origin[resolved.axis] + 1.0f;
	HostLinkEntity(member);
	PublishDoorCompletion(member, SG_MOVER_COMPLETION_TOP);
	member_before = *member;
	memset(&proof, 0, sizeof(proof));
	CHECK(SG_OracleCompoundSwimRecover(&phantom, &resolved, destination,
	      true, 0.0f, &proof, passent) == RLR_OK);
	CHECK(proof.sweep_clear_ms == 100);
	CHECK(proof.arrival_ms == 200);
	CHECK(proof.exit_speed == 2);
	CHECK(fixture_observation.pmove_calls == 8);
	CHECK(memcmp(member, &member_before, sizeof(member_before)) == 0);
	CHECK(fixture_observation.callback_calls == 0);
	CheckStaticContextRestored();
}

static void TestContinueFromLiveTop(void)
{
	const vec3_t destination = { -80.0f, 0.0f, 0.0f };
	fixture_config_t config = DefaultConfig(2, FIXTURE_SUFFIX_SUCCESS);
	sg_compound_world_preopen_t resolved;
	sg_compound_swim_recovery_proof_t proof;
	sg_phantom_t phantom;
	edict_t member_before;
	edict_t *passent;

	ResetFixture(&config);
	CHECK(Resolve(&resolved) == RLR_OK);
	passent = InitRecoveryState(&phantom, &resolved, 0);
	phantom.pms.origin[0] = (short)(160 * 8);
	phantom.origin[0] = 160.0f;
	phantom.old_pms = phantom.pms;
	phantom.old_pms.origin[0] += 8;
	SyncRecoveryPassent(&phantom, passent);
	member_before = fixture_edicts[1];
	CHECK(SG_OracleCompoundSwimContinue(&phantom, &resolved, destination,
	      true, 0.0f, &proof, passent) == RLR_OK);
	CHECK(proof.sweep_clear_ms > 0);
	CHECK(proof.sweep_clear_ms <= proof.arrival_ms);
	CHECK(fixture_observation.first_snapinitial);
	CHECK(fixture_observation.normal_pmove_masks > 0);
	CHECK(fixture_observation.stripped_pmove_masks == 0);
	CHECK(memcmp(&fixture_edicts[1], &member_before,
	             sizeof(member_before)) == 0);
	CHECK(fixture_observation.callback_calls == 0);
	CheckStaticContextRestored();
}

static void TestDropContinueFromLiveTop(void)
{
	const vec3_t destination = { -80.0f, 0.0f, 0.0f };
	const vec3_t lip = { 80.0f, 0.0f, 0.0f };
	fixture_config_t config = DefaultConfig(2, FIXTURE_SUFFIX_SUCCESS);
	sg_compound_world_preopen_t resolved;
	sg_compound_drop_proof_t proof;
	sg_phantom_t phantom;
	edict_t member_before;
	edict_t *passent;
	rune_reject_reason_t result;

	ResetFixture(&config);
	fixture_config.drop_suffix = true;
	CHECK(Resolve(&resolved) == RLR_OK);
	passent = InitRecoveryState(&phantom, &resolved, 0);
	phantom.pms.origin[0] = (short)(160 * 8);
	phantom.origin[0] = 160.0f;
	phantom.old_pms = phantom.pms;
	phantom.old_pms.origin[0] += 8;
	passent->client->oldvelocity[2] = -37.0f;
	SyncRecoveryPassent(&phantom, passent);
	member_before = fixture_edicts[1];
	result = SG_OracleCompoundDropContinue(&phantom, &resolved, destination,
	    lip, 128, true, -37.0f, &proof, passent);
	if (result != RLR_OK)
		fprintf(stderr, "drop continue got=%d pmove=%d suffix=%d\n",
		        result, fixture_observation.pmove_calls,
		        fixture_observation.suffix_commands);
	CHECK(result == RLR_OK);
	CHECK(proof.sweep_clear_ms > 0);
	CHECK(proof.sweep_clear_ms <= proof.arrival_ms);
	CHECK(fixture_observation.first_snapinitial);
	CHECK(fixture_observation.normal_pmove_masks > 0);
	CHECK(fixture_observation.stripped_pmove_masks == 0);
	CHECK(memcmp(&fixture_edicts[1], &member_before,
	             sizeof(member_before)) == 0);
	CHECK(fixture_observation.callback_calls == 0);
	CheckStaticContextRestored();
}

static qboolean DropProofZero(const sg_compound_drop_proof_t *proof);

static void TestDropRecoverFromLiveTop(void)
{
	const vec3_t destination = { -80.0f, 0.0f, 0.0f };
	const vec3_t lip = { 80.0f, 0.0f, 0.0f };
	fixture_config_t config = DefaultConfig(2, FIXTURE_SUFFIX_SUCCESS);
	sg_compound_world_preopen_t resolved;
	sg_compound_drop_proof_t proof;
	sg_phantom_t phantom;
	edict_t member_before;
	edict_t *passent;

	ResetFixture(&config);
	fixture_config.drop_suffix = true;
	CHECK(Resolve(&resolved) == RLR_OK);
	passent = InitRecoveryState(&phantom, &resolved, 0);
	passent->client->oldvelocity[2] = -37.0f;
	SyncRecoveryPassent(&phantom, passent);
	member_before = fixture_edicts[1];
	CHECK(SG_OracleCompoundDropRecover(&phantom, &resolved, destination,
	      lip, 128, true, -37.0f, &proof, passent) == RLR_OK);
	CHECK(proof.sweep_clear_ms > 0);
	CHECK(proof.sweep_clear_ms <= proof.arrival_ms);
	CHECK(fixture_observation.first_snapinitial);
	CHECK(fixture_observation.normal_pmove_masks > 0);
	CHECK(fixture_observation.stripped_pmove_masks == 0);
	CHECK(memcmp(&fixture_edicts[1], &member_before,
	             sizeof(member_before)) == 0);
	CHECK(fixture_observation.callback_calls == 0);
	CheckStaticContextRestored();

	ResetFixture(&config);
	fixture_config.drop_suffix = true;
	CHECK(Resolve(&resolved) == RLR_OK);
	passent = InitRecoveryState(&phantom, &resolved, 0);
	phantom.pms.origin[0] = (short)(160 * 8);
	phantom.origin[0] = 160.0f;
	phantom.old_pms = phantom.pms;
	SyncRecoveryPassent(&phantom, passent);
	member_before = fixture_edicts[1];
	memset(&proof, 0xa5, sizeof(proof));
	CHECK(SG_OracleCompoundDropRecover(&phantom, &resolved, destination,
	      lip, 128, true, 0.0f, &proof, passent) ==
	      RLR_SUFFIX_REPLAY_FAILED);
	CHECK(DropProofZero(&proof));
	CHECK(fixture_observation.pmove_calls == 0);
	CHECK(memcmp(&fixture_edicts[1], &member_before,
	             sizeof(member_before)) == 0);
	CheckStaticContextRestored();
}

static qboolean DropProofZero(const sg_compound_drop_proof_t *proof)
{
	sg_compound_drop_proof_t zero;

	memset(&zero, 0, sizeof(zero));
	return memcmp(proof, &zero, sizeof(zero)) == 0;
}

static void RunDropContinueFailure(const fixture_config_t *config,
	const vec3_t destination, rune_reject_reason_t expected)
{
	const vec3_t lip = { 80.0f, 0.0f, 0.0f };
	sg_compound_world_preopen_t resolved;
	sg_compound_drop_proof_t proof;
	sg_phantom_t phantom;
	edict_t member_before;
	edict_t *passent;
	rune_reject_reason_t result;

	ResetFixture(config);
	fixture_config.drop_suffix = true;
	CHECK(Resolve(&resolved) == RLR_OK);
	passent = InitRecoveryState(&phantom, &resolved, 0);
	phantom.pms.origin[0] = (short)(160 * 8);
	phantom.origin[0] = 160.0f;
	phantom.old_pms = phantom.pms;
	phantom.old_pms.origin[0] += 8;
	SyncRecoveryPassent(&phantom, passent);
	member_before = fixture_edicts[1];
	memset(&proof, 0xa5, sizeof(proof));
	result = SG_OracleCompoundDropContinue(&phantom, &resolved, destination,
	    lip, 128, true, 0.0f, &proof, passent);
	if (result != expected)
		fprintf(stderr, "drop failure mode=%d got=%d want=%d pmove=%d\n",
		        config->suffix, result, expected,
		        fixture_observation.pmove_calls);
	CHECK(result == expected);
	CHECK(DropProofZero(&proof));
	CHECK(memcmp(&fixture_edicts[1], &member_before,
	             sizeof(member_before)) == 0);
	CHECK(fixture_observation.callback_calls == 0);
	CheckStaticContextRestored();
}

static void TestDropContinueTrajectoryFailures(void)
{
	const vec3_t destination = { -80.0f, 0.0f, 0.0f };
	const vec3_t inside_destination = { 60.0f, 0.0f, 0.0f };
	fixture_config_t foreign_trigger =
		DefaultConfig(2, FIXTURE_SUFFIX_SUCCESS);
	fixture_config_t foreign_solid =
		DefaultConfig(2, FIXTURE_SUFFIX_SUCCESS);
	fixture_config_t reentry =
		DefaultConfig(2, FIXTURE_SUFFIX_REENTRY);
	fixture_config_t arrival_before =
		DefaultConfig(2, FIXTURE_SUFFIX_ARRIVE_BEFORE_CLEAR);
	fixture_config_t no_clear =
		DefaultConfig(2, FIXTURE_SUFFIX_NO_SWEEP);

	foreign_trigger.force_foreign_trigger = true;
	foreign_solid.contaminate_solid = true;
	RunDropContinueFailure(&foreign_trigger, destination,
	                       RLR_SUFFIX_REPLAY_FAILED);
	RunDropContinueFailure(&foreign_solid, destination,
	                       RLR_SUFFIX_REPLAY_FAILED);
	RunDropContinueFailure(&reentry, destination, RLR_CLEAR_MISMATCH);
	RunDropContinueFailure(&arrival_before, inside_destination,
	                       RLR_CLEAR_MISMATCH);
	RunDropContinueFailure(&no_clear, destination,
	                       RLR_SUFFIX_REPLAY_FAILED);
}

static void TestDropContinueRejectsInvalidLiveState(void)
{
	const vec3_t destination = { -80.0f, 0.0f, 0.0f };
	const vec3_t lip = { 80.0f, 0.0f, 0.0f };
	int mutation;

	for (mutation = 0; mutation < 8; mutation++)
	{
		fixture_config_t config = DefaultConfig(2, FIXTURE_SUFFIX_SUCCESS);
		sg_compound_world_preopen_t resolved;
		sg_compound_drop_proof_t proof;
		sg_phantom_t phantom;
		edict_t member_before;
		edict_t *passent;
		edict_t *argument;
		float old_frame_z = 0.0f;

		ResetFixture(&config);
		fixture_config.drop_suffix = true;
		CHECK(Resolve(&resolved) == RLR_OK);
		passent = InitRecoveryState(&phantom, &resolved, 0);
		phantom.pms.origin[0] = (short)(160 * 8);
		phantom.origin[0] = 160.0f;
		phantom.old_pms = phantom.pms;
		phantom.old_pms.origin[0] += 8;
		SyncRecoveryPassent(&phantom, passent);
		argument = passent;
		switch (mutation)
		{
		case 0: argument = NULL; break;
		case 1: passent->inuse = false; break;
		case 2: passent->s.origin[0] += 0.125f; break;
		case 3: passent->velocity[0] += 0.125f; break;
		case 4: passent->client->old_pmove.origin[0]++; break;
		case 5: phantom.origin[0] += 0.125f; break;
		case 6: old_frame_z = 1.0f; break;
		case 7: phantom.armed_door_count = 1; break;
		default: break;
		}
		member_before = fixture_edicts[1];
		memset(&proof, 0xa5, sizeof(proof));
		CHECK(SG_OracleCompoundDropContinue(&phantom, &resolved,
		      destination, lip, 128, true, old_frame_z, &proof,
		      argument) == RLR_BAD_CONTROL_POLICY);
		CHECK(DropProofZero(&proof));
		CHECK(fixture_observation.pmove_calls == 0);
		CHECK(memcmp(&fixture_edicts[1], &member_before,
		             sizeof(member_before)) == 0);
		CheckStaticContextRestored();
	}
}

static void TestDropContinueRejectsTopAuthorityDrift(void)
{
	const vec3_t destination = { -80.0f, 0.0f, 0.0f };
	const vec3_t lip = { 80.0f, 0.0f, 0.0f };
	int mutation;

	for (mutation = 0; mutation < 6; mutation++)
	{
		fixture_config_t config = DefaultConfig(2, FIXTURE_SUFFIX_SUCCESS);
		sg_compound_world_preopen_t resolved;
		sg_compound_drop_proof_t proof;
		sg_phantom_t phantom;
		edict_t member_before;
		edict_t *passent;

		ResetFixture(&config);
		fixture_config.drop_suffix = true;
		CHECK(Resolve(&resolved) == RLR_OK);
		passent = InitRecoveryState(&phantom, &resolved, 0);
		phantom.pms.origin[0] = (short)(160 * 8);
		phantom.origin[0] = 160.0f;
		phantom.old_pms = phantom.pms;
		phantom.old_pms.origin[0] += 8;
		SyncRecoveryPassent(&phantom, passent);
		switch (mutation)
		{
		case 0: resolved.mover_key = 4; break;
		case 1: fixture_edicts[1].moveinfo.state = SG_PLAT_STATE_BOTTOM; break;
		case 2: fixture_edicts[1].s.origin[1] += 1.0f; break;
		case 3: fixture_edicts[1].velocity[0] = 1.0f; break;
		case 4: resolved.trigger = &fixture_edicts[3]; break;
		case 5: resolved.member = &fixture_edicts[4]; break;
		default: break;
		}
		member_before = fixture_edicts[1];
		memset(&proof, 0xa5, sizeof(proof));
		CHECK(SG_OracleCompoundDropContinue(&phantom, &resolved,
		      destination, lip, 128, true, 0.0f, &proof, passent) ==
		      RLR_MECHANISM_UNRESOLVED);
		CHECK(DropProofZero(&proof));
		CHECK(fixture_observation.pmove_calls == 0);
		CHECK(memcmp(&fixture_edicts[1], &member_before,
		             sizeof(member_before)) == 0);
		CheckStaticContextRestored();
	}
}

static void TestDropRecoverRejectsTopAuthorityDrift(void)
{
	const vec3_t destination = { -80.0f, 0.0f, 0.0f };
	const vec3_t lip = { 80.0f, 0.0f, 0.0f };
	int mutation;

	for (mutation = 0; mutation < 6; mutation++)
	{
		fixture_config_t config = DefaultConfig(2, FIXTURE_SUFFIX_SUCCESS);
		sg_compound_world_preopen_t resolved;
		sg_compound_drop_proof_t proof;
		sg_phantom_t phantom;
		edict_t member_before;
		edict_t *passent;

		ResetFixture(&config);
		fixture_config.drop_suffix = true;
		CHECK(Resolve(&resolved) == RLR_OK);
		passent = InitRecoveryState(&phantom, &resolved, 0);
		switch (mutation)
		{
		case 0: resolved.mover_key = 4; break;
		case 1: fixture_edicts[1].moveinfo.state = SG_PLAT_STATE_BOTTOM; break;
		case 2: fixture_edicts[1].s.origin[1] += 1.0f; break;
		case 3: fixture_edicts[1].velocity[0] = 1.0f; break;
		case 4: resolved.trigger = &fixture_edicts[3]; break;
		case 5: resolved.member = &fixture_edicts[4]; break;
		default: break;
		}
		member_before = fixture_edicts[1];
		memset(&proof, 0xa5, sizeof(proof));
		CHECK(SG_OracleCompoundDropRecover(&phantom, &resolved,
		      destination, lip, 128, true, 0.0f, &proof, passent) ==
		      RLR_MECHANISM_UNRESOLVED);
		CHECK(DropProofZero(&proof));
		CHECK(fixture_observation.pmove_calls == 0);
		CHECK(memcmp(&fixture_edicts[1], &member_before,
		             sizeof(member_before)) == 0);
		CheckStaticContextRestored();
	}
}

static void TestDropContinueRejectsInsideSweepAndMidstepTopDrift(void)
{
	const vec3_t destination = { -80.0f, 0.0f, 0.0f };
	const vec3_t lip = { 80.0f, 0.0f, 0.0f };
	fixture_config_t config = DefaultConfig(2, FIXTURE_SUFFIX_SUCCESS);
	sg_compound_world_preopen_t resolved;
	sg_compound_drop_proof_t proof;
	sg_phantom_t phantom;
	edict_t member_before;
	edict_t *passent;

	ResetFixture(&config);
	fixture_config.drop_suffix = true;
	CHECK(Resolve(&resolved) == RLR_OK);
	passent = InitRecoveryState(&phantom, &resolved, 0);
	member_before = fixture_edicts[1];
	memset(&proof, 0xa5, sizeof(proof));
	CHECK(SG_OracleCompoundDropContinue(&phantom, &resolved, destination,
	      lip, 128, true, 0.0f, &proof, passent) ==
	      RLR_SUFFIX_REPLAY_FAILED);
	CHECK(DropProofZero(&proof));
	CHECK(fixture_observation.pmove_calls == 0);
	CHECK(memcmp(&fixture_edicts[1], &member_before,
	             sizeof(member_before)) == 0);
	CheckStaticContextRestored();

	ResetFixture(&config);
	fixture_config.drop_suffix = true;
	fixture_config.top_drift_at_command = 3;
	CHECK(Resolve(&resolved) == RLR_OK);
	passent = InitRecoveryState(&phantom, &resolved, 0);
	phantom.pms.origin[0] = (short)(160 * 8);
	phantom.origin[0] = 160.0f;
	phantom.old_pms = phantom.pms;
	phantom.old_pms.origin[0] += 8;
	SyncRecoveryPassent(&phantom, passent);
	member_before = fixture_edicts[1];
	memset(&proof, 0xa5, sizeof(proof));
	CHECK(SG_OracleCompoundDropContinue(&phantom, &resolved, destination,
	      lip, 128, true, 0.0f, &proof, passent) ==
	      RLR_SUFFIX_REPLAY_FAILED);
	CHECK(DropProofZero(&proof));
	CHECK(fixture_observation.pmove_calls == 3);
	fixture_edicts[1].velocity[0] = member_before.velocity[0];
	CHECK(memcmp(&fixture_edicts[1], &member_before,
	             sizeof(member_before)) == 0);
	CHECK(fixture_observation.callback_calls == 0);
	CheckStaticContextRestored();
}

static void RunRecoveryFailure(const fixture_config_t *config,
	int suffix_commands, const vec3_t destination,
	rune_reject_reason_t expected)
{
	sg_compound_world_preopen_t resolved;
	sg_compound_swim_recovery_proof_t proof;
	sg_phantom_t phantom;
	edict_t member_before;
	edict_t *passent;
	rune_reject_reason_t result;

	ResetFixture(config);
	CHECK(Resolve(&resolved) == RLR_OK);
	passent = InitRecoveryState(&phantom, &resolved, suffix_commands);
	member_before = fixture_edicts[1];
	memset(&proof, 0xa5, sizeof(proof));
	result = SG_OracleCompoundSwimRecover(&phantom, &resolved, destination,
	                                    true, 0.0f, &proof, passent);
	if (result != expected)
		fprintf(stderr, "recovery failure suffix=%d mode=%d got=%d want=%d\n",
		        suffix_commands, (int)config->suffix, result, expected);
	CHECK(result == expected);
	CHECK(RecoveryProofZero(&proof));
	CHECK(memcmp(&fixture_edicts[1], &member_before,
	             sizeof(member_before)) == 0);
	CHECK(fixture_observation.callback_calls == 0);
	CheckStaticContextRestored();
}

static void TestRecoveryTrajectoryFailures(void)
{
	const vec3_t destination = { -80.0f, 0.0f, 0.0f };
	const vec3_t inside_destination = { 0.0f, 0.0f, 0.0f };
	fixture_config_t foreign_trigger =
		DefaultConfig(2, FIXTURE_SUFFIX_SUCCESS);
	fixture_config_t foreign_solid =
		DefaultConfig(2, FIXTURE_SUFFIX_SUCCESS);
	fixture_config_t hazard = DefaultConfig(2, FIXTURE_SUFFIX_SUCCESS);
	fixture_config_t arrival_before =
		DefaultConfig(2, FIXTURE_SUFFIX_SUCCESS);
	fixture_config_t reentry = DefaultConfig(2, FIXTURE_SUFFIX_REENTRY);
	fixture_config_t no_clear =
		DefaultConfig(2, FIXTURE_SUFFIX_ARRIVE_BEFORE_CLEAR);
	fixture_config_t outside = DefaultConfig(2, FIXTURE_SUFFIX_SUCCESS);
	sg_compound_world_preopen_t resolved;
	sg_compound_swim_recovery_proof_t proof;
	sg_phantom_t phantom;
	edict_t *passent;

	foreign_trigger.force_foreign_trigger = true;
	foreign_solid.contaminate_solid = true;
	hazard.suffix_hazard = true;
	RunRecoveryFailure(&foreign_trigger, 4, destination,
	                   RLR_SUFFIX_REPLAY_FAILED);
	RunRecoveryFailure(&foreign_solid, 4, destination,
	                   RLR_SUFFIX_REPLAY_FAILED);
	RunRecoveryFailure(&hazard, 4, destination, RLR_SUFFIX_REPLAY_FAILED);
	RunRecoveryFailure(&arrival_before, 1, inside_destination,
	                   RLR_CLEAR_MISMATCH);
	RunRecoveryFailure(&reentry, 4, destination, RLR_CLEAR_MISMATCH);
	RunRecoveryFailure(&no_clear, 1, destination, RLR_SUFFIX_REPLAY_FAILED);

	ResetFixture(&outside);
	CHECK(Resolve(&resolved) == RLR_OK);
	passent = InitRecoveryState(&phantom, &resolved, 4);
	phantom.pms.origin[0] = 160 * 8;
	phantom.origin[0] = 160.0f;
	phantom.old_pms = phantom.pms;
	phantom.old_pms.origin[0] += 8;
	SyncRecoveryPassent(&phantom, passent);
	memset(&proof, 0xa5, sizeof(proof));
	CHECK(SG_OracleCompoundSwimRecover(&phantom, &resolved, destination,
	      true, 0.0f, &proof, passent) == RLR_SUFFIX_REPLAY_FAILED);
	CHECK(RecoveryProofZero(&proof));
	CHECK(fixture_observation.pmove_calls == 0);
	CheckStaticContextRestored();
}

static void TestRecoverySweepChordBoundaries(void)
{
	const vec3_t destination = { -80.0f, 0.0f, 0.0f };
	fixture_config_t preclear =
		DefaultConfig(2, FIXTURE_SUFFIX_PRECLEAR_CHORD);
	fixture_config_t postclear =
		DefaultConfig(2, FIXTURE_SUFFIX_POSTCLEAR_CHORD);
	sg_compound_world_preopen_t resolved;
	sg_compound_swim_recovery_proof_t proof;
	sg_phantom_t phantom;
	edict_t member_before;
	edict_t *passent;

	ResetFixture(&preclear);
	CHECK(Resolve(&resolved) == RLR_OK);
	passent = InitRecoveryState(&phantom, &resolved, 0);
	member_before = fixture_edicts[1];
	CHECK(SG_OracleCompoundSwimRecover(&phantom, &resolved, destination,
	      true, 0.0f, &proof, passent) == RLR_OK);
	CHECK(proof.sweep_clear_ms == 100);
	CHECK(proof.arrival_ms == 100);
	CHECK(memcmp(&fixture_edicts[1], &member_before,
	             sizeof(member_before)) == 0);
	CheckStaticContextRestored();

	RunRecoveryFailure(&postclear, 0, destination, RLR_CLEAR_MISMATCH);
}

static void TestRecoveryRejectsUnauthenticatedLiveState(void)
{
	const vec3_t destination = { -80.0f, 0.0f, 0.0f };
	int mutation;

	for (mutation = 0; mutation < 30; mutation++)
	{
		fixture_config_t config = DefaultConfig(2, FIXTURE_SUFFIX_SUCCESS);
		sg_compound_world_preopen_t resolved;
		sg_compound_swim_recovery_proof_t proof;
		sg_phantom_t phantom;
		edict_t member_before;
		edict_t *passent;
		edict_t *argument;
		float old_frame_z = 0.0f;

		ResetFixture(&config);
		CHECK(Resolve(&resolved) == RLR_OK);
		passent = InitRecoveryState(&phantom, &resolved, 4);
		argument = passent;
		switch (mutation)
		{
		case 0: argument = NULL; break;
		case 1: passent->inuse = false; break;
		case 2: passent->health = 0; break;
		case 3: passent->deadflag = 1; break;
		case 4: passent->movetype = MOVETYPE_TOSS; break;
		case 5: passent->s.modelindex = 0; break;
		case 6: passent->client->chase_target = &fixture_edicts[0]; break;
		case 7: passent->client->hookstate = 1; break;
		case 8: passent->client->hook = &fixture_edicts[4]; break;
		case 9: passent->client->ps.pmove.pm_type = PM_DEAD; break;
		case 10: fixture_gravity.value = 778.0f; break;
		case 11: passent->s.origin[0] += 0.125f; break;
		case 12: passent->velocity[0] += 0.125f; break;
		case 13: passent->client->old_pmove.origin[0]++; break;
		case 14: phantom.origin[0] += 0.125f; break;
		case 15: phantom.velocity[0] += 0.125f; break;
		case 16: passent->groundentity = &fixture_edicts[0]; break;
		case 17: passent->watertype = CONTENTS_LAVA; break;
		case 18: old_frame_z = 1.0f; break;
		case 19: sv_gravity = NULL; break;
		case 20: passent->s.origin[0] = NAN; break;
		case 21: passent->velocity[0] = INFINITY; break;
		case 22: passent->s.origin[0] = 4096.0f; break;
		case 23: fixture_gravity.value = NAN; break;
		case 24: passent->waterlevel = 2; break;
		case 25: phantom.armed_door_count = 1; break;
		case 26: passent->s.origin[1] = 0.124f; break;
		case 27: passent->s.origin[1] = -0.124f; break;
		case 28: passent->velocity[2] = 0.124f; break;
		case 29: passent->velocity[2] = -0.124f; break;
		default: break;
		}
		member_before = fixture_edicts[1];
		memset(&proof, 0xa5, sizeof(proof));
		CHECK(SG_OracleCompoundSwimRecover(&phantom, &resolved, destination,
		      true, old_frame_z, &proof, argument) == RLR_BAD_CONTROL_POLICY);
		CHECK(RecoveryProofZero(&proof));
		CHECK(fixture_observation.pmove_calls == 0);
		CHECK(memcmp(&fixture_edicts[1], &member_before,
		             sizeof(member_before)) == 0);
		CheckStaticContextRestored();
	}
}

static void TestRecoveryRejectsTopAuthorityDrift(void)
{
	const vec3_t destination = { -80.0f, 0.0f, 0.0f };
	int mutation;

	for (mutation = 0; mutation < 8; mutation++)
	{
		fixture_config_t config = DefaultConfig(2, FIXTURE_SUFFIX_SUCCESS);
		sg_compound_world_preopen_t resolved;
		sg_compound_swim_recovery_proof_t proof;
		sg_phantom_t phantom;
		edict_t member_before;
		edict_t *passent;

		ResetFixture(&config);
		CHECK(Resolve(&resolved) == RLR_OK);
		passent = InitRecoveryState(&phantom, &resolved, 4);
		switch (mutation)
		{
		case 0: resolved.mover_key = 4; break;
		case 1: fixture_edicts[1].moveinfo.state = SG_PLAT_STATE_BOTTOM; break;
		case 2: fixture_edicts[1].s.origin[1] += 1.0f; break;
		case 3: fixture_edicts[1].velocity[0] = 1.0f; break;
		case 4: fixture_edicts[1].think = NULL; break;
		case 5:
			fixture_edicts[1].nextthink = level.time + FRAMETIME;
			fixture_edicts[1].nextthink += 0.001f;
			break;
		case 6: resolved.trigger = &fixture_edicts[3]; break;
		case 7: resolved.member = &fixture_edicts[4]; break;
		default: break;
		}
		member_before = fixture_edicts[1];
		memset(&proof, 0xa5, sizeof(proof));
		CHECK(SG_OracleCompoundSwimRecover(&phantom, &resolved, destination,
		      true, 0.0f, &proof, passent) == RLR_MECHANISM_UNRESOLVED);
		CHECK(RecoveryProofZero(&proof));
		CHECK(fixture_observation.pmove_calls == 0);
		CHECK(memcmp(&fixture_edicts[1], &member_before,
		             sizeof(member_before)) == 0);
		CheckStaticContextRestored();
	}
}


int SG_CompoundSwimRecoveryCasesRun(void)
{
	int before = failures;

	TestRecoveryFromLiveTop();
	TestRecoveryAcceptsStockTopAxialResidual();
	TestContinueFromLiveTop();
	TestDropContinueFromLiveTop();
	TestDropRecoverFromLiveTop();
	TestDropContinueTrajectoryFailures();
	TestDropContinueRejectsInvalidLiveState();
	TestDropContinueRejectsTopAuthorityDrift();
	TestDropRecoverRejectsTopAuthorityDrift();
	TestDropContinueRejectsInsideSweepAndMidstepTopDrift();
	TestRecoveryTrajectoryFailures();
	TestRecoverySweepChordBoundaries();
	TestRecoveryRejectsUnauthenticatedLiveState();
	TestRecoveryRejectsTopAuthorityDrift();
	return failures - before;
}
