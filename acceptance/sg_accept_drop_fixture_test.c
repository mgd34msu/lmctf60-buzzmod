/* Host-free direct-Pmove contract for the private injected pose fixtures. */
#include <stdio.h>
#include <string.h>

#define SG_ACCEPT_DROP 1
#ifndef SG_ACCEPT_DROP_LEGACY_A
#define SG_ACCEPT_DROP_LEGACY_A 0
#endif
#include "slipgate/sg_accept_drop.c"

typedef enum fixture_test_mode_e
{
	FIXTURE_TEST_VALID = 0,
	FIXTURE_TEST_DYNAMIC_SUPPORT,
	FIXTURE_TEST_HARMFUL_LIQUID,
	FIXTURE_TEST_TELEPORT,
	FIXTURE_TEST_GRAVITY_MUTATION,
	FIXTURE_TEST_HEALTH_MUTATION,
	FIXTURE_TEST_SNAPINITIAL_UNCHANGED
} fixture_test_mode_t;

sg_host_t sg_host;
edict_t *g_edicts;
level_locals_t level;

static rune_t test_rune;
static rune_seed_t test_seeds[897];
static edict_t test_edicts[4];
static gclient_t test_client;
static sg_bot_t test_bot;
static const sg_accept_drop_selector_t *test_selector;
static fixture_test_mode_t test_mode;
static int test_failures;
static int test_pmove_calls;
static int test_trace_calls;
static int test_pointcontents_calls;
static int test_link_calls;

static float BitsFloat(uint32_t bits)
{
	float value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

rune_t *SG_Rune(void)
{
	return &test_rune;
}

qboolean SG_ImmutableSupport(edict_t *ent)
{
	return ent && ent->inuse && ent->classname &&
	       strcmp(ent->classname, "fixture-dynamic") != 0;
}

static void Check(qboolean condition, const char *message)
{
	if (!condition)
	{
		fprintf(stderr, "fixture-contract: %s\n", message);
		test_failures++;
	}
}

static trace_t TestTrace(const vec3_t start, const vec3_t mins,
	const vec3_t maxs, const vec3_t end, edict_t *passent, int contentmask)
{
	trace_t trace;

	(void)start;
	(void)mins;
	(void)maxs;
	(void)end;
	memset(&trace, 0, sizeof(trace));
	test_trace_calls++;
	Check(passent == &test_edicts[1], "trace passent was not injected entity");
	Check(contentmask == (test_edicts[1].health > 0 ?
	        MASK_PLAYERSOLID : MASK_DEADSOLID),
	    "trace health mask diverged");
	trace.fraction = 1.0f;
	return trace;
}

static int TestPointContents(const vec3_t point)
{
	(void)point;
	test_pointcontents_calls++;
	if (test_mode == FIXTURE_TEST_HARMFUL_LIQUID)
		return CONTENTS_WATER | CONTENTS_LAVA;
	return test_selector->fixture_kind == SGAD_FIXTURE_WATER_DEPTH2 ?
	    CONTENTS_WATER : 0;
}

static void TestPmove(pmove_t *pm)
{
	vec3_t zero = { 0.0f, 0.0f, 0.0f };
	trace_t trace;
	int axis;

	test_pmove_calls++;
	Check(pm != NULL, "pmove received null state");
	if (!pm)
		return;
	Check(pm->cmd.msec == 0, "fixture command was not zero-ms");
	Check(pm->cmd.buttons == 0 && pm->cmd.forwardmove == 0 &&
	        pm->cmd.sidemove == 0 && pm->cmd.upmove == 0 &&
	        pm->cmd.impulse == 0 && pm->cmd.lightlevel == 0,
	    "fixture command carried movement fields");
	Check(pm->snapinitial, "fixture Pmove was not snapinitial");
	Check(pm->s.pm_type == PM_NORMAL, "fixture Pmove pm_type changed");
	Check(pm->s.gravity == (short)test_rune.v3_header.gravity,
	    "fixture Pmove gravity mismatch");
	Check(pm->numtouch == 0 && pm->groundentity == NULL &&
	        pm->watertype == 0 && pm->waterlevel == 0 &&
	        pm->viewheight == 0.0f,
	    "pmove_t was not zero-initialized");
	for (axis = 0; axis < 3; axis++)
	{
		short fixed = (short)(test_seeds[test_selector->fixture_seed].
		    origin[axis] * 8.0f);
		short expected_angle =
		    ANGLE2SHORT(test_client.v_angle[axis]) - pm->s.delta_angles[axis];

		Check(pm->s.origin[axis] == fixed && pm->s.velocity[axis] == 0,
		    "fixture origin/velocity was not authoritative fixed-point");
		Check(pm->cmd.angles[axis] == expected_angle,
		    "fixture view command was not derived from client view");
		Check(pm->mins[axis] == 0.0f && pm->maxs[axis] == 0.0f &&
		        pm->viewangles[axis] == 0.0f && pm->touchents[axis] == NULL,
		    "pmove_t output storage was not initially zero");
	}
	Check(pm->trace != NULL && pm->pointcontents != NULL,
	    "fixture callbacks were not installed");
	trace = pm->trace(zero, zero, zero, zero);
	(void)trace;
	pm->watertype = pm->pointcontents(zero);
	pm->waterlevel = test_selector->fixture_kind == SGAD_FIXTURE_WATER_DEPTH2 ?
	    2 : 0;
	pm->groundentity = test_selector->fixture_kind ==
	        SGAD_FIXTURE_DRY_SUPPORTED ? &test_edicts[0] : NULL;
	if (test_mode == FIXTURE_TEST_DYNAMIC_SUPPORT)
		pm->groundentity = &test_edicts[2];
	if (test_mode == FIXTURE_TEST_TELEPORT)
		pm->s.pm_flags |= PMF_TIME_TELEPORT;
	if (test_mode == FIXTURE_TEST_GRAVITY_MUTATION)
		pm->s.gravity++;
	if (test_mode == FIXTURE_TEST_HEALTH_MUTATION)
		test_edicts[1].health--;
	pm->mins[0] = pm->mins[1] = -16.0f;
	pm->mins[2] = -24.0f;
	pm->maxs[0] = pm->maxs[1] = 16.0f;
	pm->maxs[2] = 32.0f;
	pm->viewheight = 22.0f;
	/* A touch output is deliberate: fixture placement copies body state only
	 * and must not process gameplay touches. */
	pm->numtouch = 1;
	pm->touchents[0] = &test_edicts[3];
}

static void TestLinkEntity(edict_t *ent)
{
	test_link_calls++;
	Check(ent == &test_edicts[1], "linkentity target mismatch");
}

static void TestDPrint(const char *fmt, ...)
{
	(void)fmt;
}

static void Setup(const sg_accept_drop_selector_t *selector,
	fixture_test_mode_t mode)
{
	int axis;

	memset(&test_rune, 0, sizeof(test_rune));
	memset(test_seeds, 0, sizeof(test_seeds));
	memset(test_edicts, 0, sizeof(test_edicts));
	memset(&test_client, 0, sizeof(test_client));
	memset(&test_bot, 0, sizeof(test_bot));
	memset(&accept_drop, 0, sizeof(accept_drop));
	memset(&sg_host, 0, sizeof(sg_host));
	test_selector = selector;
	test_mode = mode;
	test_pmove_calls = 0;
	test_trace_calls = 0;
	test_pointcontents_calls = 0;
	test_link_calls = 0;
	test_rune.hdr.num_seeds = (int)(sizeof(test_seeds) /
	    sizeof(test_seeds[0]));
	test_rune.seeds = test_seeds;
	test_rune.v3_header.gravity = 800.0f;
	for (axis = 0; axis < 3; axis++)
	{
		test_seeds[selector->fixture_seed].origin[axis] =
		    BitsFloat(selector->fixture_bits[axis]);
		test_seeds[selector->destination].origin[axis] =
		    BitsFloat(selector->destination_bits[axis]);
		test_client.v_angle[axis] = 10.0f + axis * 7.0f;
		test_client.ps.pmove.delta_angles[axis] = (short)(axis + 1);
		test_client.oldvelocity[axis] = 123.0f + axis;
	}
	test_client.ps.pmove.pm_type = PM_NORMAL;
	test_client.ps.pmove.gravity = 800;
	if (mode == FIXTURE_TEST_SNAPINITIAL_UNCHANGED)
	{
		test_client.old_pmove = test_client.ps.pmove;
		for (axis = 0; axis < 3; axis++)
		{
			test_client.old_pmove.origin[axis] = (short)(
			    test_seeds[selector->fixture_seed].origin[axis] * 8.0f);
			test_client.old_pmove.velocity[axis] = 0;
		}
	}
	test_edicts[0].inuse = true;
	test_edicts[0].linkcount = 41;
	test_edicts[0].classname = "worldspawn";
	test_edicts[1].inuse = true;
	test_edicts[1].client = &test_client;
	test_edicts[1].health = 100;
	test_edicts[1].movetype = MOVETYPE_WALK;
	test_edicts[1].groundentity_linkcount = 99;
	test_edicts[2].inuse = true;
	test_edicts[2].linkcount = 77;
	test_edicts[2].classname = "fixture-dynamic";
	test_edicts[3].inuse = true;
	test_edicts[3].health = 37;
	test_bot.ent = &test_edicts[1];
	test_bot.commit_link = selector->expected_link;
	g_edicts = test_edicts;
	accept_fixture_passent = &test_edicts[3];
	sg_host.trace = TestTrace;
	sg_host.pointcontents = TestPointContents;
	sg_host.pmove = TestPmove;
	sg_host.linkentity = TestLinkEntity;
	sg_host.dprint = TestDPrint;
}

/* This is the observed supported-preair command-four shape: command four
 * consumes the exact zero-ms seed, then leaves a real dry world-supported
 * pose at -10.75/831.5/-231.875 with nonzero horizontal velocity.  The next
 * outer boundary must bind this state, not rewind it to the seed. */
static void PrepareSupportedPostCommand(
	const sg_accept_drop_selector_t *selector)
{
	static const uint32_t origin_bits[3] = {
		UINT32_C(0xc12c0000), UINT32_C(0x444fe000), UINT32_C(0xc367e000)
	};
	static const uint32_t old_origin_bits[3] = {
		UINT32_C(0xc1100000), UINT32_C(0x44500000), UINT32_C(0xc367e000)
	};
	static const uint32_t velocity_bits[3] = {
		UINT32_C(0xc2918000), UINT32_C(0xc1910000), UINT32_C(0)
	};
	int axis;

	accept_drop.injection_applied = 1;
	accept_drop.injection_step = selector->injection_step;
	accept_drop.injection_frame = 100;
	accept_drop.injection_order_stage = SGAD_ORDER_INJECTED;
	accept_drop.injection_boundary_checks = 0;
	accept_drop.injection_post_command_captures = 0;
	accept_drop.injection_post_command_validations = 0;
	accept_drop.injection_order_errors = 0;
	accept_drop.injection_entity_passes = 0;
	accept_drop.injection_sg_frames = 0;
	accept_drop.injection_snapshot_mismatch_mask = 0;
	memset(&accept_drop.post_command, 0, sizeof(accept_drop.post_command));
	accept_drop.phase = SGAD_ACTIVE;
	accept_drop.requested_case = (int)(selector - accept_selectors) + 1;
	accept_drop.armed = true;
	accept_drop.bot = &test_bot;
	accept_drop.link = selector->expected_link;
	accept_drop.historical_commands = 4;
	accept_drop.commands = 4;
	accept_drop.poses = 4;
	accept_drop.final_historical_matches = 4;
	accept_drop.final_historical_mismatches = 0;
	accept_drop.historical_pending = false;
	test_bot.commit_link = selector->expected_link;
	test_bot.drop_started = true;
	test_bot.drop_walkoff = true;
	test_bot.drop_airborne = false;
	test_bot.drop_recover = false;
	if (SG_ACCEPT_DROP_LEGACY_A)
	{
		accept_drop.observer_began = true;
		accept_drop.observer_active = true;
		accept_drop.observer.progress.status = SG_REPLAY_RUNNING;
		accept_drop.observer.progress.reason = SG_REPLAY_REASON_NONE;
		accept_drop.observer.progress.elapsed_ms = 75;
		accept_drop.observer.progress.arrival_ms = SG_REPLAY_TIME_DISCOVER;
		accept_drop.observer.progress.step_pending = true;
		accept_drop.observer.walkoff = true;
		accept_drop.observer.airborne = false;
		accept_drop.observer.recovery = false;
		accept_drop.observer_presteps = 4;
		accept_drop.observer_poststeps = 3;
		accept_drop.observer_command_matches = 4;
	}
	else
	{
		test_bot.drop_replay_active = true;
		test_bot.drop_replay_link = selector->expected_link;
		test_bot.drop_replay.progress.status = SG_REPLAY_RUNNING;
		test_bot.drop_replay.progress.reason = SG_REPLAY_REASON_NONE;
		test_bot.drop_replay.progress.elapsed_ms = 75;
		test_bot.drop_replay.progress.arrival_ms = SG_REPLAY_TIME_DISCOVER;
		test_bot.drop_replay.progress.step_pending = true;
		test_bot.drop_replay.walkoff = true;
		test_bot.drop_replay.airborne = false;
		test_bot.drop_replay.recovery = false;
	}
	for (axis = 0; axis < 3; axis++)
	{
		test_edicts[1].s.origin[axis] = BitsFloat(origin_bits[axis]);
		test_edicts[1].s.old_origin[axis] = BitsFloat(old_origin_bits[axis]);
		test_edicts[1].velocity[axis] = BitsFloat(velocity_bits[axis]);
		test_client.ps.pmove.origin[axis] =
		    (short)(test_edicts[1].s.origin[axis] * 8.0f);
		test_client.ps.pmove.velocity[axis] =
		    (short)(test_edicts[1].velocity[axis] * 8.0f);
		test_client.oldvelocity[axis] = 0.0f;
	}
	test_edicts[1].groundentity = &test_edicts[0];
	test_edicts[1].groundentity_linkcount = test_edicts[0].linkcount;
	test_edicts[1].watertype = 0;
	test_edicts[1].waterlevel = 0;
	level.framenum = accept_drop.injection_frame;
}

/* This is the non-paused ClientEndServerFrame body synchronization at the end
 * of command four.  The real routine first canonicalizes pmove from body
 * state, then rolls oldvelocity to that body velocity. */
static void EmulateClientEndServerFrame(edict_t *ent)
{
	int axis;

	if (!ent || !ent->client)
		return;
	for (axis = 0; axis < 3; axis++)
	{
		ent->client->ps.pmove.origin[axis] =
		    (short)(ent->s.origin[axis] * 8.0f);
		ent->client->ps.pmove.velocity[axis] =
		    (short)(ent->velocity[axis] * 8.0f);
	}
	VectorCopy(ent->velocity, ent->client->oldvelocity);
}

/* This is the next G_RunFrame per-entity operation.  It occurs after the
 * prior frame's ClientEndServerFrame and before EntityPass/SG_RunFrame. */
static void EmulateGRunFrameOldOriginCopy(edict_t *ent)
{
	if (!(ent->flags & FL_OLDORGNOTSET))
		VectorCopy(ent->s.origin, ent->s.old_origin);
}

static void CheckSupportedPostCommandBoundary(
	const sg_accept_drop_selector_t *selector)
{
	sg_accept_drop_post_command_snapshot_t snapshot;

	PrepareSupportedPostCommand(selector);
	Check(AcceptCapturePostCommandFixture(&test_bot, &test_edicts[1], selector),
	    "post-command fixture capture was rejected");
	Check(accept_drop.injection_post_command_captures == 1 &&
	        accept_drop.post_command.captured &&
	        accept_drop.post_command.origin_bits[0] == UINT32_C(0xc12c0000) &&
	        accept_drop.post_command.origin_bits[1] == UINT32_C(0x444fe000) &&
	        accept_drop.post_command.origin_bits[2] == UINT32_C(0xc367e000) &&
	        accept_drop.post_command.velocity_bits[0] == UINT32_C(0xc2918000) &&
	        accept_drop.post_command.velocity_bits[1] == UINT32_C(0xc1910000) &&
	        accept_drop.post_command.grounded &&
	        accept_drop.post_command.support_valid &&
	        accept_drop.post_command.waterlevel == 0,
	    "post-command fixture capture did not preserve the command-four shape");
	Check(!AcceptCapturePostCommandFixture(&test_bot, &test_edicts[1], selector),
	    "duplicate post-command fixture capture was accepted");
	snapshot = accept_drop.post_command;
	/* Reproduce the failing live shape: the next frame's old-origin copy and
	 * real private EntityPass/FrameBegin occur, but the preceding end-frame
	 * oldvelocity rollover is absent.  This must fail specifically on that
	 * lifecycle field rather than fabricate an order counter. */
	level.framenum = accept_drop.injection_frame + 1;
	EmulateGRunFrameOldOriginCopy(&test_edicts[1]);
	SG_AcceptDropEntityPass();
	SG_AcceptDropFrameBegin();
	Check(!AcceptFixtureSnapshotValid(&test_edicts[1], selector),
	    "post-command boundary accepted missing ClientEndServerFrame rollover");
	Check((AcceptFixtureSnapshotMismatch(&test_edicts[1], selector) &
	        SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_OLDVELOCITY)) != 0 &&
	        strcmp(AcceptSnapshotMismatchFirst(
	            AcceptFixtureSnapshotMismatch(&test_edicts[1], selector)),
	            "oldvelocity") == 0,
	    "missing end-frame rollover did not identify oldvelocity");
	SG_AcceptDropBoundary(&test_bot, selector->expected_link,
	    "boundary-enter-legacy", NULL, NULL);
	Check((accept_drop.injection_snapshot_mismatch_mask &
	        SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_OLDVELOCITY)) != 0 &&
	        accept_drop.injection_order_stage == SGAD_ORDER_SG_FRAME &&
	        accept_drop.injection_entity_passes == 1 &&
	        accept_drop.injection_sg_frames == 1 &&
	        accept_drop.injection_order_errors == 1,
	    "real host-order boundary did not reject missing oldvelocity rollover");

	/* Start the clean sequence again.  The actual F3 end-frame transition is
	 * before F4's old-origin copy; only then may EntityPass/SG_RunFrame reach
	 * the deferred boundary. */
	PrepareSupportedPostCommand(selector);
	Check(AcceptCapturePostCommandFixture(&test_bot, &test_edicts[1], selector),
	    "clean post-command fixture recapture was rejected");
	snapshot = accept_drop.post_command;
	EmulateClientEndServerFrame(&test_edicts[1]);
	level.framenum = accept_drop.injection_frame + 1;
	Check(!AcceptFixtureSnapshotValid(&test_edicts[1], selector),
	    "post-command boundary accepted missing G_RunFrame old-origin rollover");
	Check((AcceptFixtureSnapshotMismatch(&test_edicts[1], selector) &
	        SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_OLD_ORIGIN)) != 0 &&
	        strcmp(AcceptSnapshotMismatchFirst(
	            AcceptFixtureSnapshotMismatch(&test_edicts[1], selector)),
	            "old_origin") == 0,
	    "missing next-frame rollover did not identify old_origin");
	EmulateGRunFrameOldOriginCopy(&test_edicts[1]);
	SG_AcceptDropEntityPass();
	SG_AcceptDropFrameBegin();
	Check(AcceptFixtureSnapshotValid(&test_edicts[1], selector),
	    "post-command boundary snapshot rejected host lifecycle rollovers");
	SG_AcceptDropBoundary(&test_bot, selector->expected_link,
	    "boundary-enter-legacy", NULL, NULL);
	Check(accept_drop.injection_snapshot_mismatch_mask == 0 &&
	        accept_drop.injection_order_stage == SGAD_ORDER_SG_FRAME &&
	        accept_drop.injection_entity_passes == 1 &&
	        accept_drop.injection_sg_frames == 1 &&
	        accept_drop.injection_order_errors == 0,
	    "real host-order boundary rejected valid lifecycle rollovers");
	test_client.oldvelocity[0] += 0.125f;
	Check(!AcceptFixtureSnapshotValid(&test_edicts[1], selector) &&
	        (AcceptFixtureSnapshotMismatch(&test_edicts[1], selector) &
	         SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_OLDVELOCITY)) != 0 &&
	        strcmp(AcceptSnapshotMismatchFirst(
	            AcceptFixtureSnapshotMismatch(&test_edicts[1], selector)),
	            "oldvelocity") == 0,
	    "post-command boundary accepted wrong ClientEndServerFrame rollover");
	EmulateClientEndServerFrame(&test_edicts[1]);
	test_edicts[1].s.old_origin[0] += 0.125f;
	Check(!AcceptFixtureSnapshotValid(&test_edicts[1], selector) &&
	        (AcceptFixtureSnapshotMismatch(&test_edicts[1], selector) &
	         SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_OLD_ORIGIN)) != 0 &&
	        strcmp(AcceptSnapshotMismatchFirst(
	            AcceptFixtureSnapshotMismatch(&test_edicts[1], selector)),
	            "old_origin") == 0,
	    "post-command boundary accepted wrong G_RunFrame old-origin rollover");
	EmulateGRunFrameOldOriginCopy(&test_edicts[1]);
	test_client.ps.pmove.origin[0]++;
	Check(!AcceptFixtureSnapshotValid(&test_edicts[1], selector) &&
	        (AcceptFixtureSnapshotMismatch(&test_edicts[1], selector) &
	         SGAD_SNAPSHOT_MASK(SGAD_SNAPSHOT_PMOVE_ORIGIN)) != 0 &&
	        strcmp(AcceptSnapshotMismatchFirst(
	            AcceptFixtureSnapshotMismatch(&test_edicts[1], selector)),
	            "pmove_origin") == 0,
	    "post-command boundary accepted wrong ClientEndServerFrame pmove sync");
	EmulateClientEndServerFrame(&test_edicts[1]);
	test_edicts[1].s.origin[0] += 0.125f;
	Check(!AcceptFixtureSnapshotValid(&test_edicts[1], selector),
	    "post-command position drift was accepted");
	test_edicts[1].s.origin[0] -= 0.125f;
	test_edicts[1].velocity[0] += 0.125f;
	Check(!AcceptFixtureSnapshotValid(&test_edicts[1], selector),
	    "post-command velocity drift was accepted");
	test_edicts[1].velocity[0] -= 0.125f;
	test_edicts[1].health--;
	Check(!AcceptFixtureSnapshotValid(&test_edicts[1], selector),
	    "post-command health drift was accepted");
	test_edicts[1].health++;
	test_edicts[1].groundentity = &test_edicts[2];
	Check(!AcceptFixtureSnapshotValid(&test_edicts[1], selector),
	    "post-command support drift was accepted");
	test_edicts[1].groundentity = &test_edicts[0];
	test_edicts[1].watertype = CONTENTS_WATER;
	test_edicts[1].waterlevel = 2;
	Check(!AcceptFixtureSnapshotValid(&test_edicts[1], selector),
	    "post-command water drift was accepted");
	test_edicts[1].watertype = 0;
	test_edicts[1].waterlevel = 0;
	accept_drop.post_command.terminal_geometry =
	    !snapshot.terminal_geometry;
	Check(!AcceptFixtureSnapshotValid(&test_edicts[1], selector),
	    "post-command geometry drift was accepted");
	accept_drop.post_command = snapshot;
	accept_drop.commands++;
	Check(!AcceptFixtureSnapshotValid(&test_edicts[1], selector),
	    "post-command command-order drift was accepted");
	accept_drop.commands--;
	accept_drop.injection_post_command_captures = 0;
	Check(!AcceptFixtureSnapshotValid(&test_edicts[1], selector),
	    "missing post-command capture was accepted");
	accept_drop.injection_post_command_captures = 1;
}

static int RunPositive(const sg_accept_drop_selector_t *selector)
{
	pmove_state_t expected_pmove;
	int touched_health;
	int axis;

	Setup(selector, FIXTURE_TEST_VALID);
	touched_health = test_edicts[3].health;
	if (!AcceptInjectFixture(&test_bot, &test_edicts[1], selector))
	{
		fprintf(stderr, "fixture-contract: positive fixture seed=%d rejected "
		                "snapshot=%d calls=%d/%d/%d/%d pm_type=%d gravity=%d "
		                "ground=%d water=%d/%d health=%d commit=%d\n",
		    selector->fixture_seed,
		    AcceptFixtureSnapshotValid(&test_edicts[1], selector),
		    test_pmove_calls, test_trace_calls, test_pointcontents_calls,
		    test_link_calls, (int)test_client.ps.pmove.pm_type,
		    test_client.ps.pmove.gravity, test_edicts[1].groundentity != NULL,
		    test_edicts[1].watertype, test_edicts[1].waterlevel,
		    test_edicts[1].health, test_bot.commit_link);
		for (axis = 0; axis < 3; axis++)
			fprintf(stderr, " axis%d bits=%08x/%08x fixed=%d/%d vel=%d %.9g "
			                "oldvel=%.9g\n", axis,
			    AcceptFloatBits(test_edicts[1].s.origin[axis]),
			    selector->fixture_bits[axis],
			    test_client.ps.pmove.origin[axis],
			    (short)(test_seeds[selector->fixture_seed].origin[axis] * 8.0f),
			    test_client.ps.pmove.velocity[axis],
			    test_edicts[1].velocity[axis], test_client.oldvelocity[axis]);
		return 0;
	}
	expected_pmove = test_client.ps.pmove;
	Check(test_pmove_calls == 1 && test_trace_calls == 1 &&
	        test_pointcontents_calls == 1 && test_link_calls == 1,
	    "fixture host call counts diverged");
	Check(accept_fixture_passent == &test_edicts[3],
	    "private passent context was not restored");
	Check(memcmp(&test_client.old_pmove, &expected_pmove,
	        sizeof(expected_pmove)) == 0,
	    "old_pmove did not copy final pmove state");
	Check(test_edicts[3].health == touched_health,
	    "fixture placement processed a Pmove touch");
	Check(accept_drop.injection_health == 100 &&
	        accept_drop.injection_deadflag == 0 &&
	        accept_drop.injection_movetype == MOVETYPE_WALK &&
	        accept_drop.injection_oldvelocity_zero,
	    "fixture life/oldvelocity evidence was not recorded");
	Check(test_edicts[1].viewheight == 22.0f &&
	        test_edicts[1].mins[0] == -16.0f &&
	        test_edicts[1].maxs[2] == 32.0f,
	    "entity Pmove outputs were not copied");
	Check(test_edicts[1].groundentity ==
	        (selector->fixture_kind == SGAD_FIXTURE_DRY_SUPPORTED ?
	             &test_edicts[0] : NULL),
	    "groundentity output mismatch");
	Check(test_edicts[1].groundentity_linkcount ==
	        (selector->fixture_kind == SGAD_FIXTURE_DRY_SUPPORTED ? 41 : 0),
	    "groundentity linkcount was stale or incorrect");
	for (axis = 0; axis < 3; axis++)
		Check(test_client.oldvelocity[axis] == 0.0f &&
		        test_edicts[1].velocity[axis] == 0.0f &&
		        test_edicts[1].s.old_origin[axis] ==
		            test_edicts[1].s.origin[axis],
		    "velocity/oldvelocity/old-origin copy mismatch");
	if (selector->fixture_boundary == SGAD_FIXTURE_BOUNDARY_POST_COMMAND)
	{
		CheckSupportedPostCommandBoundary(selector);
		return test_failures == 0;
	}
	accept_drop.injection_applied = 1;
	Check(AcceptFixtureSnapshotValid(&test_edicts[1], selector),
	    "next-boundary exact fixture snapshot was rejected");
	test_edicts[1].health--;
	Check(!AcceptFixtureSnapshotValid(&test_edicts[1], selector),
	    "next-boundary health drift was accepted");
	test_edicts[1].health++;
	test_edicts[1].deadflag = DEAD_DEAD;
	Check(!AcceptFixtureSnapshotValid(&test_edicts[1], selector),
	    "next-boundary deadflag drift was accepted");
	test_edicts[1].deadflag = 0;
	test_edicts[1].movetype = MOVETYPE_NOCLIP;
	Check(!AcceptFixtureSnapshotValid(&test_edicts[1], selector),
	    "next-boundary movetype drift was accepted");
	test_edicts[1].movetype = MOVETYPE_WALK;
	test_client.oldvelocity[2] = -1.0f;
	Check(!AcceptFixtureSnapshotValid(&test_edicts[1], selector),
	    "next-boundary oldvelocity drift was accepted");
	test_client.oldvelocity[2] = 0.0f;
	return test_failures == 0;
}

static int RunNegative(const sg_accept_drop_selector_t *selector,
	fixture_test_mode_t mode, const char *name)
{
	Setup(selector, mode);
	if (AcceptInjectFixture(&test_bot, &test_edicts[1], selector))
	{
		fprintf(stderr, "fixture-contract: negative %s was accepted\n", name);
		return 0;
	}
	return 1;
}

int main(void)
{
	int ok = 1;

	test_failures = 0;
	ok &= RunPositive(&accept_selectors[2]);
	ok &= RunPositive(&accept_selectors[4]);
	ok &= RunNegative(&accept_selectors[2], FIXTURE_TEST_DYNAMIC_SUPPORT,
	    "dynamic-support");
	ok &= RunNegative(&accept_selectors[4], FIXTURE_TEST_HARMFUL_LIQUID,
	    "harmful-liquid");
	ok &= RunNegative(&accept_selectors[2], FIXTURE_TEST_TELEPORT,
	    "teleport");
	ok &= RunNegative(&accept_selectors[2], FIXTURE_TEST_GRAVITY_MUTATION,
	    "gravity-mutation");
	ok &= RunNegative(&accept_selectors[2], FIXTURE_TEST_HEALTH_MUTATION,
	    "health-mutation");
	ok &= RunNegative(&accept_selectors[2],
	    FIXTURE_TEST_SNAPINITIAL_UNCHANGED, "snapinitial-unchanged");
	if (ok && test_failures == 0)
		printf("fixture-selftest ok dry=exact depth2=exact pmove=zeroed "
		       "passent-mask=exact outputs=copied touches=ignored "
		       "boundary-drift=rejected "
		       "outer-frame-rollovers=oldvelocity-then-old-origin "
		       "negatives=6\n");
	return ok && test_failures == 0 ? 0 : 1;
}
