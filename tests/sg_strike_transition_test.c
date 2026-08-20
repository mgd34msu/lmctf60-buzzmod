/* Host-free execution of the real strike route-transition seams. */
#include "g_local.h"
#include "g_ctffunc.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_bot.h"
#include "slipgate/sg_descend.h"
#include "slipgate/sg_declared_door_guard.h"
#include "slipgate/sg_defense_supply.h"
#include "slipgate/sg_strike.h"
#include "slipgate/sg_death_belief.h"
#include "slipgate/sg_role_policy.h"
#include "slipgate/sg_route_policy.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

level_locals_t level;
sg_fields_t sg_fields;

void SG_StrikeTestSetRune(rune_t *rune);
qboolean SG_StrikeTestDeclaredDoorGuardRestore(sg_bot_t *bot);
qboolean SG_StrikeTestApplyDutyRoute(sg_think_t *tc,
	sg_strike_duty_t duty, int team);
void SG_StrikeTestRetireGenericRail(sg_bot_t *bot, const sg_think_t *tc);
qboolean SG_StrikeTestApplyRallyPolicy(sg_bot_t *bot,
	const sg_think_t *tc, qboolean *rally_hold);
qboolean SG_StrikeTestAttackEligible(sg_role_t role, qboolean carrying,
	int ordered_role);
void SG_StrikeTestPureRoutePrepareCommit(sg_bot_t *bot,
	const sg_think_t *tc);

static int failures;

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, \
		    #condition); \
		failures++; \
	} \
} while (0)

enum guard_call_e
{
	CALL_RELEASE = 1,
	CALL_HOLD_OPEN,
	CALL_PAUSE,
	CALL_RESUME,
	CALL_AUTHORIZE,
	CALL_TERMINAL
};

static int guard_calls[32];
static int guard_call_count;
static sg_compound_guard_result_t release_result;
static sg_compound_guard_result_t hold_result;
static sg_compound_guard_result_t pause_result;
static sg_compound_guard_result_t resume_result;
static sg_compound_guard_result_t authorize_result;
static sg_compound_guard_result_t validate_result;
static sg_mover_lease_record_t durable_record;
static int terminal_count;

static rune_t test_rune;
static rune_seed_t test_seeds[3];
static rune_link_t test_links[3];
static int weapon_field[3];
static int enemy_field[3];
static int home_field[3];
static int recover_field[3];
static int carrier_field[3];
static int formation_field[3];

sg_team_belief_t sg_caco_team_belief;

static void GuardCall(int call)
{
	if (guard_call_count < (int)(sizeof(guard_calls) / sizeof(guard_calls[0])))
		guard_calls[guard_call_count++] = call;
}

static void GuardReset(void)
{
	memset(guard_calls, 0, sizeof(guard_calls));
	guard_call_count = 0;
	release_result = SG_COMPOUND_GUARD_OK;
	hold_result = SG_COMPOUND_GUARD_OK;
	pause_result = SG_COMPOUND_GUARD_OK;
	resume_result = SG_COMPOUND_GUARD_OK;
	authorize_result = SG_COMPOUND_GUARD_OK;
	validate_result = SG_COMPOUND_GUARD_OK;
	terminal_count = 0;
	memset(&durable_record, 0, sizeof(durable_record));
	durable_record.law = SG_MOVER_LAW_DECLARED_DOOR;
	durable_record.state = SG_MOVER_LEASE_ACTIVE;
	durable_record.link_index = 1;
	durable_record.mechanism_index = 1U;
}

static void CheckCalls(const int *expected, int count)
{
	int index;

	CHECK(guard_call_count == count);
	for (index = 0; index < count && index < guard_call_count; index++)
		CHECK(guard_calls[index] == expected[index]);
}

sg_compound_guard_result_t SG_DeclaredDoorGuardReleaseProvedClear(
	sg_bot_t *bot)
{
	(void)bot;
	GuardCall(CALL_RELEASE);
	return release_result;
}

sg_compound_guard_result_t SG_DeclaredDoorGuardHoldOpen(sg_bot_t *bot,
	int lease_ms)
{
	(void)bot;
	CHECK(lease_ms == 500);
	GuardCall(CALL_HOLD_OPEN);
	return hold_result;
}

sg_compound_guard_result_t SG_DeclaredDoorGuardPause(sg_bot_t *bot)
{
	(void)bot;
	GuardCall(CALL_PAUSE);
	return pause_result;
}

sg_compound_guard_result_t SG_DeclaredDoorGuardResume(sg_bot_t *bot,
	int link_index)
{
	(void)bot;
	CHECK(link_index == 1);
	GuardCall(CALL_RESUME);
	return resume_result;
}

sg_compound_guard_result_t SG_DeclaredDoorGuardAuthorize(sg_bot_t *bot,
	int link_index)
{
	(void)bot;
	CHECK(link_index == 1);
	GuardCall(CALL_AUTHORIZE);
	return authorize_result;
}

sg_compound_guard_result_t SG_CompoundGuardValidate(
	sg_compound_guard_bot_t *bot, sg_mover_lease_record_t *record_out)
{
	(void)bot;
	if (record_out)
		*record_out = durable_record;
	return validate_result;
}

void SG_DeclaredDoorTerminalDeath(sg_bot_t *bot)
{
	(void)bot;
	terminal_count++;
	GuardCall(CALL_TERMINAL);
}

void SG_ButtonExecutionActionReset(sg_bot_t *bot)
{
	if (!bot)
		return;
	bot->declared_button_latched = false;
	bot->declared_button_rider = false;
	memset(bot->declared_button_start_q8, 0,
	    sizeof(bot->declared_button_start_q8));
	memset(bot->declared_button_end_q8, 0,
	    sizeof(bot->declared_button_end_q8));
}

/* The transition harness isolates route-purpose ownership from live item
 * selection. Production clears the exact pad witness at this boundary; this
 * stub preserves that observable state transition without linking sg_goal.c. */
void SG_StrikeWeaponTargetClear(sg_bot_t *bot)
{
	if (!bot)
		return;
	bot->strike_weapon_target_ent = -1;
	bot->strike_weapon_target_seed = -1;
	memset(bot->strike_weapon_target_org, 0,
	    sizeof(bot->strike_weapon_target_org));
}

static void WorldReset(void)
{
	memset(&sg_caco_team_belief, 0, sizeof(sg_caco_team_belief));
	sg_caco_team_belief.carrier[0].client = -1;
	sg_caco_team_belief.carrier[1].client = -1;
	memset(&test_rune, 0, sizeof(test_rune));
	memset(test_seeds, 0, sizeof(test_seeds));
	memset(test_links, 0, sizeof(test_links));
	test_rune.hdr.num_seeds = 3;
	test_rune.hdr.num_links = 3;
	test_rune.seeds = test_seeds;
	test_rune.links = test_links;
	test_links[0].from = 0;
	test_links[0].to = 1;
	test_links[0].action = RL_RUN;
	test_links[0].cost_ms = 100;
	test_links[1].from = 0;
	test_links[1].to = 1;
	test_links[1].action = RL_RUN;
	test_links[1].cost_ms = 200;
	test_links[2].from = 0;
	test_links[2].to = 2;
	test_links[2].action = RL_RUN;
	test_links[2].cost_ms = 300;
	weapon_field[0] = 1000;
	weapon_field[1] = 400;
	weapon_field[2] = 900;
	enemy_field[0] = 1000;
	enemy_field[1] = 900;
	enemy_field[2] = 100;
	home_field[0] = 100;
	home_field[1] = 600;
	home_field[2] = 900;
	recover_field[0] = 800;
	recover_field[1] = 500;
	recover_field[2] = 0;
	carrier_field[0] = 300;
	carrier_field[1] = 100;
	carrier_field[2] = 800;
	memset(&sg_fields, 0, sizeof(sg_fields));
	sg_fields.to_red_flag = home_field;
	sg_fields.to_blue_flag = enemy_field;
	/* A live standoff re-floods our current flag field from the enemy thief.
	 * It must route RECOVER, never the carrier's homeward egress. */
	sg_fields.to_flag_now[0][0] = recover_field;
	sg_fields.our_carrier[0] = carrier_field;
	sg_fields.our_carrier_valid[0] = true;
	level.time = 10.0f;
	SG_StrikeTestSetRune(&test_rune);
	GuardReset();
}

static sg_bot_t Bot(void)
{
	sg_bot_t bot;
	int index;

	memset(&bot, 0, sizeof(bot));
	bot.seed = 0;
	bot.commit_link = -1;
	bot.strike_weapon_link = -1;
	bot.sticky_link = -1;
	bot.hook_link = -1;
	bot.jump_link = -1;
	bot.drop_link = -1;
	bot.drop_replay_link = -1;
	bot.swim_replay_link = -1;
	bot.rail_link = -1;
	bot.def_shift_link = -1;
	bot.def_shift_seed = -1;
	bot.declared_start_frame = -1;
	bot.declared_touch_frame = -1;
	bot.declared_trigger_frame = -1;
	bot.declared_egress_proof_frame = -1;
	for (index = 0; index < SG_BL_MAX; index++)
		bot.bl_link[index] = -1;
	return bot;
}

static sg_think_t Think(void)
{
	sg_think_t tc;

	memset(&tc, 0, sizeof(tc));
	tc.goal_field = weapon_field;
	tc.route_field = weapon_field;
	return tc;
}

static void ArmExact(sg_bot_t *bot, sg_think_t *tc, int action)
{
	*bot = Bot();
	*tc = Think();
	test_links[1].action = (byte)action;
	bot->commit_link = 1;
	bot->commit_until = 30.0f;
	bot->commit_route_field = weapon_field;
	bot->strike_weapon_link = 1;
	bot->strike_weapon_until = 20.0f;
	bot->strike_weapon_target_ent = 6;
	bot->strike_weapon_target_seed = 2;
	bot->strike_weapon_target_org[0] = 128.0f;
	bot->sticky_link = 1;
	bot->latch_until = 25.0f;
	bot->bl_link[0] = 77;
	bot->bl_until[0] = 88.0f;
	tc->strike_weapon_deadline = 20.0f;
}

static void CheckCleared(const sg_bot_t *bot)
{
	CHECK(bot->commit_link == -1);
	CHECK(bot->commit_until == 0.0f);
	CHECK(bot->sticky_link == -1);
	CHECK(bot->latch_until == 0.0f);
	CHECK(bot->strike_weapon_link == -1);
	CHECK(bot->strike_weapon_until == 0.0f);
	CHECK(!bot->strike_weapon_draining);
	CHECK(bot->strike_weapon_target_ent == -1);
	CHECK(bot->strike_weapon_target_seed == -1);
	CHECK(bot->strike_weapon_target_org[0] == 0.0f);
	CHECK(bot->bl_link[0] == 77);
	CHECK(bot->bl_until[0] == 88.0f);
}

static void CheckDraining(const sg_bot_t *bot)
{
	CHECK(bot->commit_link == 1);
	CHECK(bot->commit_until == 30.0f);
	CHECK(bot->strike_weapon_link == 1);
	CHECK(bot->strike_weapon_until == 20.0f);
	CHECK(bot->strike_weapon_draining);
	CHECK(bot->bl_link[0] == 77);
	CHECK(bot->bl_until[0] == 88.0f);
}

static void TestFreshTagAndOldCommitment(void)
{
	sg_bot_t bot;
	sg_think_t tc;
	int candidate;

	WorldReset();
	bot = Bot();
	tc = Think();
	tc.strike_weapon_pursuit = true;
	tc.strike_weapon_deadline = 15.0f;
	tc.route_pure = true;
	candidate = SG_StrikeTestWeaponFilterFreshCandidate(&bot, &tc, 1);
	CHECK(candidate == 1);
	SG_StrikeTestCommitFreshLink(&bot, &tc, candidate);
	CHECK(bot.commit_link == 1);
	CHECK(bot.commit_route_field == weapon_field);
	CHECK(bot.strike_weapon_link == 1);
	CHECK(bot.strike_weapon_until == 15.0f);
	CHECK(!bot.strike_weapon_draining);

	/* A generic sticky latch can outlive its commit.  Fresh weapon purpose
	 * retires that unowned half-transaction before candidate filtering/tagging. */
	bot = Bot();
	bot.sticky_link = 0;
	bot.latch_until = 14.0f;
	CHECK(!SG_StrikeTestWeaponPrepareCommit(&bot, &tc));
	CHECK(bot.commit_link == -1);
	CHECK(bot.sticky_link == -1 && bot.latch_until == 0.0f);
	candidate = SG_StrikeTestWeaponFilterFreshCandidate(&bot, &tc, 1);
	CHECK(candidate == 1);
	SG_StrikeTestCommitFreshLink(&bot, &tc, candidate);
	CHECK(bot.commit_link == 1 && bot.strike_weapon_link == 1);

	/* A staged old RUN is canceled before selection and is never retro-tagged. */
	bot = Bot();
	bot.commit_link = 0;
	bot.commit_until = 14.0f;
	bot.sticky_link = 0;
	bot.latch_until = 14.0f;
	CHECK(!SG_StrikeTestWeaponPrepareCommit(&bot, &tc));
	CHECK(bot.commit_link == -1);
	CHECK(bot.strike_weapon_link == -1);
	candidate = SG_StrikeTestWeaponFilterFreshCandidate(&bot, &tc, 1);
	SG_StrikeTestCommitFreshLink(&bot, &tc, candidate);
	CHECK(bot.commit_link == 1 && bot.strike_weapon_link == 1);

	/* A reversible rocket-jump overlay on that old RUN is part of the same
	 * staged transaction and must not leak into the fresh weapon route. */
	bot = Bot();
	bot.commit_link = 0;
	bot.commit_until = 14.0f;
	bot.sticky_link = 0;
	bot.latch_until = 14.0f;
	bot.rj_phase = 1;
	bot.rj_deadline = 16.0f;
	bot.rj_fire_until = 17.0f;
	bot.rj_use_next = 18.0f;
	CHECK(!SG_StrikeTestWeaponPrepareCommit(&bot, &tc));
	CHECK(bot.commit_link == -1 && bot.sticky_link == -1);
	CHECK(bot.rj_phase == 0 && bot.rj_deadline == 0.0f);
	CHECK(bot.rj_fire_until == 0.0f && bot.rj_use_next == 0.0f);
	candidate = SG_StrikeTestWeaponFilterFreshCandidate(&bot, &tc, 1);
	SG_StrikeTestCommitFreshLink(&bot, &tc, candidate);
	CHECK(bot.commit_link == 1 && bot.strike_weapon_link == 1);

	/* Once phase two can have emitted fire, the same layered controller is
	 * physical: fresh weapon authority may neither cancel nor relabel it. */
	bot = Bot();
	bot.commit_link = 0;
	bot.commit_until = 14.0f;
	bot.sticky_link = 0;
	bot.latch_until = 14.0f;
	bot.rj_phase = 2;
	bot.rj_deadline = 16.0f;
	CHECK(!SG_StrikeTestWeaponPrepareCommit(&bot, &tc));
	CHECK(bot.sticky_link == -1 && bot.latch_until == 0.0f);
	CHECK(bot.commit_link == 0 && bot.commit_until == 14.0f);
	CHECK(bot.rj_phase == 2 && bot.rj_deadline == 16.0f);
	CHECK(bot.strike_weapon_link == -1 && !bot.strike_weapon_draining);
	SG_StrikeTestCommitFreshLink(&bot, &tc, 1);
	CHECK(bot.commit_link == 0 && bot.strike_weapon_link == -1);

	/* A layered physical controller drains under its old owner: no cancellation
	 * and no weapon-purpose relabel. */
	bot = Bot();
	bot.commit_link = 0;
	bot.commit_until = 14.0f;
	bot.hook_phase = 2;
	bot.hook_link = 0;
	CHECK(!SG_StrikeTestWeaponPrepareCommit(&bot, &tc));
	CHECK(bot.commit_link == 0);
	CHECK(bot.hook_phase == 2);
	CHECK(bot.strike_weapon_link == -1);
	SG_StrikeTestCommitFreshLink(&bot, &tc, 1);
	CHECK(bot.commit_link == 0 && bot.strike_weapon_link == -1);

	/* A non-descending candidate cannot receive a weapon purpose identity. */
	weapon_field[1] = weapon_field[0];
	bot = Bot();
	CHECK(SG_StrikeTestWeaponFilterFreshCandidate(&bot, &tc, 1) == -1);
}

static void TestPureRouteChangeRetiresOnlyReversibleRun(void)
{
	sg_bot_t bot;
	sg_think_t tc;

	WorldReset();
	bot = Bot();
	tc = Think();
	tc.route_pure = true;
	SG_StrikeTestCommitFreshLink(&bot, &tc, 1);
	bot.sticky_link = 1;
	bot.latch_until = 25.0f;
	CHECK(bot.commit_route_field == weapon_field);

	/* PRESS replacing a weapon/recovery route must move on the enemy field
	 * this frame, not after the generic three-second commitment expires. */
	tc.goal_field = enemy_field;
	tc.route_field = enemy_field;
	SG_StrikeTestPureRoutePrepareCommit(&bot, &tc);
	CHECK(bot.commit_link == -1 && bot.commit_until == 0.0f);
	CHECK(bot.commit_route_field == NULL);
	CHECK(bot.sticky_link == -1 && bot.latch_until == 0.0f);

	/* The same field retains its anti-flap commitment. */
	bot = Bot();
	tc = Think();
	tc.route_pure = true;
	SG_StrikeTestCommitFreshLink(&bot, &tc, 1);
	SG_StrikeTestPureRoutePrepareCommit(&bot, &tc);
	CHECK(bot.commit_link == 1);
	CHECK(bot.commit_route_field == weapon_field);

	/* A composed surface is not an exact purpose change. */
	tc.route_pure = false;
	tc.route_field = enemy_field;
	SG_StrikeTestPureRoutePrepareCommit(&bot, &tc);
	CHECK(bot.commit_link == 1);

	/* A speed hook which already fired owns physics even though it is layered
	 * on an ordinary RUN; the new mission waits for its bounded landing. */
	tc.route_pure = true;
	bot.hook_phase = 2;
	bot.hook_link = 1;
	SG_StrikeTestPureRoutePrepareCommit(&bot, &tc);
	CHECK(bot.commit_link == 1 && bot.hook_phase == 2);

	/* A selected or aiming hook has not fired. Positive progress toward its old
	 * endpoint is not current-purpose authority, so the new field cancels it. */
	bot = Bot();
	tc = Think();
	tc.route_pure = true;
	test_links[1].action = RL_HOOK;
	SG_StrikeTestCommitFreshLink(&bot, &tc, 1);
	bot.hook_phase = 1;
	bot.hook_link = 1;
	tc.route_field = enemy_field;
	SG_StrikeTestPureRoutePrepareCommit(&bot, &tc);
	CHECK(bot.commit_link == -1 && bot.hook_phase == 0);
	CHECK(bot.hook_link == -1 && bot.commit_route_field == NULL);

	/* Once fired, the same graph hook is physical and retains its bounded
	 * attach/pull/landing lifecycle across a mission change. */
	bot = Bot();
	tc = Think();
	tc.route_pure = true;
	test_links[1].action = RL_HOOK;
	SG_StrikeTestCommitFreshLink(&bot, &tc, 1);
	bot.hook_phase = 2;
	bot.hook_link = 1;
	tc.route_field = enemy_field;
	SG_StrikeTestPureRoutePrepareCommit(&bot, &tc);
	CHECK(bot.commit_link == 1 && bot.hook_phase == 2);
}

static void TestDeadlineGoAndCurrentCandidate(void)
{
	sg_bot_t bot;
	sg_think_t tc;

	WorldReset();
	ArmExact(&bot, &tc, RL_RUN);
	level.time = 20.0f;
	tc.strike_weapon_pursuit = true;
	CHECK(!SG_StrikeTestWeaponPrepareCommit(&bot, &tc));
	CheckCleared(&bot);

	WorldReset();
	ArmExact(&bot, &tc, RL_RUN);
	tc.strike_rush = true;
	tc.goal_field = enemy_field;
	tc.route_field = enemy_field;
	CHECK(!SG_StrikeTestWeaponPrepareCommit(&bot, &tc));
	CheckCleared(&bot);
	/* The same-frame enemy-field candidate now owns the ordinary commit and is
	 * not mislabeled as the ended weapon diversion. */
	CHECK(SG_StrikeTestWeaponFilterFreshCandidate(&bot, &tc, 2) == 2);
	SG_StrikeTestCommitFreshLink(&bot, &tc, 2);
	CHECK(bot.commit_link == 2);
	CHECK(bot.strike_weapon_link == -1);

	/* GO must retire both halves of an unrelated ordinary transaction.  A
	 * pending sticky latch may not resurrect the old route over the current
	 * enemy-field candidate later in Think_CommitLink. */
	WorldReset();
	bot = Bot();
	tc = Think();
	bot.commit_link = 0;
	bot.commit_until = 30.0f;
	bot.sticky_link = 0;
	bot.latch_until = 25.0f;
	tc.strike_rush = true;
	tc.goal_field = enemy_field;
	tc.route_field = enemy_field;
	CHECK(!SG_StrikeTestWeaponPrepareCommit(&bot, &tc));
	CHECK(bot.commit_link == -1 && bot.commit_until == 0.0f);
	CHECK(bot.sticky_link == -1 && bot.latch_until == 0.0f);
	CHECK(SG_StrikeTestWeaponFilterFreshCandidate(&bot, &tc, 2) == 2);
	SG_StrikeTestCommitFreshLink(&bot, &tc, 2);
	CHECK(bot.commit_link == 2 && bot.strike_weapon_link == -1);

	/* GO also retires an orphan generic latch when its commit disappeared before
	 * this frame.  The first enemy-field candidate must survive selection. */
	WorldReset();
	bot = Bot();
	tc = Think();
	bot.sticky_link = 0;
	bot.latch_until = 25.0f;
	tc.strike_rush = true;
	tc.goal_field = enemy_field;
	tc.route_field = enemy_field;
	CHECK(!SG_StrikeTestWeaponPrepareCommit(&bot, &tc));
	CHECK(bot.commit_link == -1 && bot.commit_until == 0.0f);
	CHECK(bot.sticky_link == -1 && bot.latch_until == 0.0f);
	CHECK(SG_StrikeTestWeaponFilterFreshCandidate(&bot, &tc, 2) == 2);
	SG_StrikeTestCommitFreshLink(&bot, &tc, 2);
	CHECK(bot.commit_link == 2 && bot.strike_weapon_link == -1);

	/* Purpose completion itself preserves a different current generic latch; a
	 * same-frame GO owns the stronger boundary and retires it before selection. */
	WorldReset();
	ArmExact(&bot, &tc, RL_RUN);
	bot.commit_link = -1;
	bot.commit_until = 0.0f;
	bot.sticky_link = 0;
	bot.latch_until = 25.0f;
	CHECK(!SG_StrikeTestWeaponReconcile(&bot, &tc));
	CHECK(bot.strike_weapon_link == -1);
	CHECK(bot.sticky_link == 0 && bot.latch_until == 25.0f);

	WorldReset();
	ArmExact(&bot, &tc, RL_RUN);
	bot.commit_link = -1;
	bot.commit_until = 0.0f;
	bot.sticky_link = 0;
	bot.latch_until = 25.0f;
	tc.strike_rush = true;
	tc.goal_field = enemy_field;
	tc.route_field = enemy_field;
	CHECK(!SG_StrikeTestWeaponPrepareCommit(&bot, &tc));
	CHECK(bot.strike_weapon_link == -1);
	CHECK(bot.sticky_link == -1 && bot.latch_until == 0.0f);

	/* The GO boundary retires only generic selection authority.  A pre-existing
	 * physical controller keeps its commit and controller state and is not
	 * retroactively tagged as weapon-owned. */
	WorldReset();
	bot = Bot();
	tc = Think();
	bot.commit_link = 0;
	bot.commit_until = 30.0f;
	bot.sticky_link = 0;
	bot.latch_until = 25.0f;
	bot.hook_phase = 2;
	bot.hook_link = 0;
	tc.strike_rush = true;
	CHECK(!SG_StrikeTestWeaponPrepareCommit(&bot, &tc));
	CHECK(bot.sticky_link == -1 && bot.latch_until == 0.0f);
	CHECK(bot.commit_link == 0 && bot.commit_until == 30.0f);
	CHECK(bot.hook_phase == 2 && bot.hook_link == 0);
	CHECK(bot.strike_weapon_link == -1 && !bot.strike_weapon_draining);

	/* The same rule applies to an exact weapon route already crossing its
	 * physical speed-hook boundary: it enters sticky DRAIN, while only the
	 * generic link latch is retired. */
	WorldReset();
	ArmExact(&bot, &tc, RL_RUN);
	bot.hook_phase = 2;
	bot.hook_link = 1;
	tc.strike_rush = true;
	CHECK(!SG_StrikeTestWeaponPrepareCommit(&bot, &tc));
	CHECK(bot.sticky_link == -1 && bot.latch_until == 0.0f);
	CHECK(bot.commit_link == 1 && bot.commit_until == 30.0f);
	CHECK(bot.hook_phase == 2 && bot.hook_link == 1);
	CHECK(bot.strike_weapon_link == 1 && bot.strike_weapon_draining);

	WorldReset();
	ArmExact(&bot, &tc, RL_RUN);
	bot.rj_phase = 2;
	bot.rj_deadline = 40.0f;
	tc.strike_rush = true;
	CHECK(!SG_StrikeTestWeaponPrepareCommit(&bot, &tc));
	CHECK(bot.sticky_link == -1 && bot.latch_until == 0.0f);
	CHECK(bot.commit_link == 1 && bot.commit_until == 30.0f);
	CHECK(bot.rj_phase == 2 && bot.rj_deadline == 40.0f);
	CHECK(bot.strike_weapon_link == 1 && bot.strike_weapon_draining);

	/* Phase-one overlays have not emitted their physical command and therefore
	 * cancel with the stale RUN on GO. */
	WorldReset();
	bot = Bot();
	tc = Think();
	bot.commit_link = 0;
	bot.commit_until = 30.0f;
	bot.sticky_link = 0;
	bot.latch_until = 25.0f;
	bot.hook_phase = 1;
	bot.hook_link = 0;
	bot.hook_deadline = 40.0f;
	bot.speedhook = true;
	bot.flow_release = true;
	tc.strike_rush = true;
	CHECK(!SG_StrikeTestWeaponPrepareCommit(&bot, &tc));
	CHECK(bot.commit_link == -1 && bot.commit_until == 0.0f);
	CHECK(bot.sticky_link == -1 && bot.latch_until == 0.0f);
	CHECK(bot.hook_phase == 0 && bot.hook_link == -1);
	CHECK(bot.hook_deadline == 0.0f && !bot.speedhook && !bot.flow_release);

	WorldReset();
	bot = Bot();
	tc = Think();
	bot.commit_link = 0;
	bot.commit_until = 30.0f;
	bot.sticky_link = 0;
	bot.latch_until = 25.0f;
	bot.rj_phase = 1;
	bot.rj_deadline = 40.0f;
	bot.rj_fire_until = 41.0f;
	bot.rj_use_next = 42.0f;
	tc.strike_rush = true;
	CHECK(!SG_StrikeTestWeaponPrepareCommit(&bot, &tc));
	CHECK(bot.commit_link == -1 && bot.commit_until == 0.0f);
	CHECK(bot.sticky_link == -1 && bot.latch_until == 0.0f);
	CHECK(bot.rj_phase == 0 && bot.rj_deadline == 0.0f);
	CHECK(bot.rj_fire_until == 0.0f && bot.rj_use_next == 0.0f);
}

static void TestSpeedHookAndStickyDrain(void)
{
	sg_bot_t bot;
	sg_think_t tc;

	WorldReset();
	ArmExact(&bot, &tc, RL_RUN);
	bot.hook_phase = 1;
	bot.hook_link = 1;
	bot.hook_deadline = 40.0f;
	bot.speedhook = true;
	bot.flow_release = true;
	CHECK(!SG_StrikeTestWeaponReconcile(&bot, &tc));
	CheckCleared(&bot);
	CHECK(bot.hook_phase == 0 && bot.hook_link == -1);
	CHECK(bot.hook_deadline == 0.0f);
	CHECK(!bot.speedhook && !bot.flow_release);

	for (int phase = 2; phase <= 3; phase++)
	{
		WorldReset();
		ArmExact(&bot, &tc, RL_RUN);
		bot.hook_phase = phase;
		bot.hook_link = 1;
		bot.speedhook = true;
		CHECK(!SG_StrikeTestWeaponReconcile(&bot, &tc));
		CheckDraining(&bot);
		CHECK(bot.hook_phase == phase && bot.hook_link == 1);
		/* Authority returning before the immutable deadline cannot undo DRAIN. */
		tc.strike_weapon_pursuit = true;
		tc.strike_weapon_deadline = 20.0f;
		CHECK(!SG_StrikeTestWeaponReconcile(&bot, &tc));
		CheckDraining(&bot);
		bot.hook_phase = 0;
		bot.commit_link = -1;
		CHECK(!SG_StrikeTestWeaponReconcile(&bot, &tc));
		CHECK(bot.strike_weapon_link == -1);
		CHECK(!bot.strike_weapon_draining);
		CHECK(bot.sticky_link == -1 && bot.latch_until == 0.0f);
	}
}

static void TestRocketJumpBoundaries(void)
{
	sg_bot_t bot;
	sg_think_t tc;

	WorldReset();
	ArmExact(&bot, &tc, RL_ROCKETJUMP);
	bot.rj_phase = 1;
	bot.rj_deadline = 40.0f;
	bot.rj_fire_until = 41.0f;
	bot.rj_use_next = 42.0f;
	CHECK(!SG_StrikeTestWeaponReconcile(&bot, &tc));
	CheckCleared(&bot);
	CHECK(bot.rj_phase == 0 && bot.rj_deadline == 0.0f);
	CHECK(bot.rj_fire_until == 0.0f && bot.rj_use_next == 0.0f);

	for (int phase = 2; phase <= 3; phase++)
	{
		WorldReset();
		ArmExact(&bot, &tc, RL_ROCKETJUMP);
		bot.rj_phase = phase;
		bot.rj_deadline = 40.0f;
		CHECK(!SG_StrikeTestWeaponReconcile(&bot, &tc));
		CheckDraining(&bot);
		CHECK(bot.rj_phase == phase && bot.rj_deadline == 40.0f);
		tc.strike_weapon_pursuit = true;
		CHECK(!SG_StrikeTestWeaponReconcile(&bot, &tc));
		CheckDraining(&bot);
		bot.rj_phase = 0;
		bot.commit_link = -1;
		CHECK(!SG_StrikeTestWeaponReconcile(&bot, &tc));
		CHECK(bot.strike_weapon_link == -1);
		CHECK(bot.sticky_link == -1 && bot.latch_until == 0.0f);
	}
}

static void RunSimpleBoundary(int action, int physical)
{
	sg_bot_t bot;
	sg_think_t tc;
	int original_hook = 0;
	qboolean original_started = false;
	qboolean original_active = false;

	WorldReset();
	ArmExact(&bot, &tc, action);
	switch (action)
	{
	case RL_JUMP:
		bot.jump_link = 1;
		bot.jump_started = physical;
		original_started = bot.jump_started;
		break;
	case RL_DROP:
		bot.drop_link = 1;
		bot.drop_started = physical;
		original_started = bot.drop_started;
		break;
	case RL_HOOK:
		bot.hook_link = 1;
		bot.hook_phase = physical ? 2 : 1;
		original_hook = bot.hook_phase;
		break;
	case RL_SWIM:
		bot.swim_validated = physical;
		original_active = bot.swim_validated;
		break;
	default:
		CHECK(0);
		return;
	}
	CHECK(!SG_StrikeTestWeaponReconcile(&bot, &tc));
	if (physical)
	{
		CheckDraining(&bot);
		if (action == RL_JUMP)
			CHECK(bot.jump_started == original_started);
		else if (action == RL_DROP)
			CHECK(bot.drop_started == original_started);
		else if (action == RL_HOOK)
			CHECK(bot.hook_phase == original_hook);
		else if (action == RL_SWIM)
			CHECK(bot.swim_validated == original_active);
	}
	else
		CheckCleared(&bot);
}

static void TestActionTable(void)
{
	sg_bot_t bot;
	sg_think_t tc;

	RunSimpleBoundary(RL_JUMP, 0);
	RunSimpleBoundary(RL_JUMP, 1);
	RunSimpleBoundary(RL_DROP, 0);
	RunSimpleBoundary(RL_DROP, 1);
	RunSimpleBoundary(RL_HOOK, 0);
	RunSimpleBoundary(RL_HOOK, 1);
	RunSimpleBoundary(RL_SWIM, 0);
	RunSimpleBoundary(RL_SWIM, 1);

	WorldReset();
	ArmExact(&bot, &tc, RL_HOOK);
	bot.hook_phase = 3;
	bot.hook_link = 1;
	CHECK(!SG_StrikeTestWeaponReconcile(&bot, &tc));
	CheckDraining(&bot);
	CHECK(bot.hook_phase == 3);

	WorldReset();
	ArmExact(&bot, &tc, RL_SWIM);
	bot.swim_replay_active = true;
	CHECK(!SG_StrikeTestWeaponReconcile(&bot, &tc));
	CheckDraining(&bot);
	CHECK(bot.swim_replay_active);
}

static void RunDeclaredBoundary(int action, int touched, int triggered,
	int activated, int expect_drain)
{
	sg_bot_t bot;
	sg_think_t tc;

	WorldReset();
	ArmExact(&bot, &tc, action);
	bot.declared_started = true;
	bot.declared_touched = touched;
	bot.declared_triggered = triggered;
	bot.declared_activated = activated;
	CHECK(!SG_StrikeTestWeaponReconcile(&bot, &tc));
	if (expect_drain)
	{
		CheckDraining(&bot);
		CHECK(bot.declared_started);
		CHECK((int)bot.declared_touched == touched);
		CHECK((int)bot.declared_triggered == triggered);
		CHECK((int)bot.declared_activated == activated);
	}
	else
		CheckCleared(&bot);
}

static void TestLiftTeleportBoundaries(void)
{
	sg_bot_t bot;
	sg_think_t tc;

	for (int action = RL_LIFT; action <= RL_TELEPORT; action++)
	{
		RunDeclaredBoundary(action, 0, 0, 0, 0);
		RunDeclaredBoundary(action, 1, 0, 0, 1);
		RunDeclaredBoundary(action, 0, 1, 0, 1);
		RunDeclaredBoundary(action, 0, 0, 1, 1);
	}
	WorldReset();
	ArmExact(&bot, &tc, RL_TELEPORT);
	bot.declared_started = true;
	bot.swim_validated = true;
	CHECK(!SG_StrikeTestWeaponReconcile(&bot, &tc));
	CheckDraining(&bot);
	WorldReset();
	ArmExact(&bot, &tc, RL_TELEPORT);
	bot.declared_started = true;
	bot.swim_replay_active = true;
	CHECK(!SG_StrikeTestWeaponReconcile(&bot, &tc));
	CheckDraining(&bot);
}

static void ArmDoor(sg_bot_t *bot, sg_think_t *tc, int action)
{
	ArmExact(bot, tc, action);
	bot->declared_started = true;
	bot->declared_start_frame = 9;
	durable_record.link_index = 1;
}

static void TestDoorLeaseRetirement(void)
{
	sg_bot_t bot;
	sg_think_t tc;
	static const int door_actions[] = { RL_DOOR, RL_BUTTON_DOOR };
	static const int release_only[] = { CALL_RELEASE };
	static const int hold_pause[] = {
		CALL_RELEASE, CALL_HOLD_OPEN, CALL_PAUSE
	};
	static const int restore_hold[] = { CALL_RELEASE, CALL_HOLD_OPEN };
	static const int terminal_calls[] = {
		CALL_RELEASE, CALL_HOLD_OPEN, CALL_TERMINAL
	};
	static const int expired_calls[] = { CALL_RELEASE, CALL_TERMINAL };

	/* Both declared door actions release a pre-touch ACTIVE claim before any
	 * local route fields are cleared. */
	for (size_t action_index = 0;
	     action_index < sizeof(door_actions) / sizeof(door_actions[0]);
	     action_index++)
	{
		int action = door_actions[action_index];

		WorldReset();
		ArmDoor(&bot, &tc, action);
		CHECK(!SG_StrikeTestWeaponReconcile(&bot, &tc));
		CheckCalls(release_only, 1);
		CheckCleared(&bot);
		CHECK(!bot.declared_started);
	}

	WorldReset();
	ArmDoor(&bot, &tc, RL_DOOR);
	release_result = SG_COMPOUND_GUARD_NOT_CLEAR;
	CHECK(SG_StrikeTestWeaponReconcile(&bot, &tc));
	CheckCalls(hold_pause, 3);
	CHECK(tc.think_over);
	CheckDraining(&bot);
	CHECK(bot.declared_started && bot.declared_guard_paused);

	/* The actual next-frame restore retries proof and TOP maintenance only.  An
	 * ACTIVE durable record is paused again, never resumed/re-authorized. */
	GuardReset();
	release_result = SG_COMPOUND_GUARD_NOT_CLEAR;
	durable_record.state = SG_MOVER_LEASE_ACTIVE;
	level.time = 10.05f;
	CHECK(!SG_StrikeTestDeclaredDoorGuardRestore(&bot));
	CheckCalls(hold_pause, 3);
	CheckDraining(&bot);
	CHECK(bot.declared_guard_paused);

	/* A durable PAUSED record needs no second Pause, and likewise may not
	 * Resume or Authorize the ended errand. */
	GuardReset();
	release_result = SG_COMPOUND_GUARD_NOT_CLEAR;
	durable_record.state = SG_MOVER_LEASE_PAUSED;
	level.time = 10.1f;
	CHECK(!SG_StrikeTestDeclaredDoorGuardRestore(&bot));
	CheckCalls(restore_hold, 2);
	CheckDraining(&bot);
	CHECK(bot.declared_guard_paused);

	/* Once all subjects are proved clear, restore retires action and purpose. */
	GuardReset();
	release_result = SG_COMPOUND_GUARD_OK;
	level.time = 10.2f;
	CHECK(SG_StrikeTestDeclaredDoorGuardRestore(&bot));
	CheckCalls(release_only, 1);
	CHECK(bot.commit_link == -1 && bot.strike_weapon_link == -1);
	CHECK(bot.sticky_link == -1 && bot.latch_until == 0.0f);
	CHECK(!bot.declared_started && !bot.declared_guard_paused);

	WorldReset();
	ArmDoor(&bot, &tc, RL_BUTTON_DOOR);
	release_result = SG_COMPOUND_GUARD_NOT_CLEAR;
	hold_result = SG_COMPOUND_GUARD_HOST_ERROR;
	CHECK(SG_StrikeTestWeaponReconcile(&bot, &tc));
	CheckCalls(terminal_calls, 3);
	CHECK(terminal_count == 1);
	CHECK(bot.commit_link == 1 && bot.strike_weapon_link == 1);

	WorldReset();
	ArmDoor(&bot, &tc, RL_DOOR);
	bot.declared_door_recovery_since = 5.0f;
	level.time = 10.0f;
	release_result = SG_COMPOUND_GUARD_NOT_CLEAR;
	CHECK(SG_StrikeTestWeaponReconcile(&bot, &tc));
	CheckCalls(expired_calls, 2);
	CHECK(terminal_count == 1);
	CHECK(bot.commit_link == 1 && bot.strike_weapon_link == 1);

	/* Physical mechanism boundaries drain and never attempt staged release. */
	for (size_t action_index = 0;
	     action_index < sizeof(door_actions) / sizeof(door_actions[0]);
	     action_index++)
	{
		int action = door_actions[action_index];

		for (int boundary = 0; boundary < 3; boundary++)
		{
			WorldReset();
			ArmDoor(&bot, &tc, action);
			bot.declared_touched = boundary == 0;
			bot.declared_triggered = boundary == 1;
			bot.declared_activated = boundary == 2;
			CHECK(!SG_StrikeTestWeaponReconcile(&bot, &tc));
			CHECK(guard_call_count == 0);
			CheckDraining(&bot);
		}
	}
}

static void TestRailAndCarrierRoute(void)
{
	sg_bot_t bot;
	sg_think_t tc;
	qboolean rally_hold = true;

	WorldReset();
	bot = Bot();
	tc = Think();
	bot.rail_link = 0;
	bot.rail_stage = 2;
	bot.rail_until = 99.0f;
	tc.strike_active = true;
	SG_StrikeTestRetireGenericRail(&bot, &tc);
	CHECK(bot.rail_link == -1 && bot.rail_stage == 0);
	CHECK(bot.rail_until == 0.0f);
	CHECK(!SG_StrikeTestRailLateOverrideAllowed(&bot, &tc));
	CHECK(!SG_StrikeTestRailWatchdogAllowed(&bot, &tc));

	bot.rail_link = 0;
	bot.rail_stage = 2;
	bot.rail_until = 99.0f;
	tc.strike_active = false;
	SG_StrikeTestRetireGenericRail(&bot, &tc);
	CHECK(bot.rail_link == 0 && bot.rail_stage == 2);
	CHECK(bot.rail_until == 99.0f);
	CHECK(SG_StrikeTestRailLateOverrideAllowed(&bot, &tc));
	CHECK(SG_StrikeTestRailWatchdogAllowed(&bot, &tc));

	/* Accepted defender RETURN state remains an independent immutable owner. */
	bot.def_supply_phase = SG_DEFENSE_SUPPLY_PHASE_RETURN;
	bot.def_supply_armed = true;
	CHECK(!SG_StrikeTestRailLateOverrideAllowed(&bot, &tc));
	CHECK(!SG_StrikeTestRailWatchdogAllowed(&bot, &tc));
	CHECK(bot.def_supply_phase == SG_DEFENSE_SUPPLY_PHASE_RETURN);
	CHECK(bot.def_supply_armed);

	/* In a flag standoff the carrier still uses HOME while RECOVER uses the
	 * thief-bound dynamic own-flag field. */
	tc = Think();
	tc.strike_active = true;
	CHECK(SG_StrikeTestApplyDutyRoute(&tc, SG_STRIKE_DUTY_CARRY,
	    CTF_TEAM_RED));
	CHECK(tc.goal_field == home_field && tc.route_field == home_field);
	CHECK(tc.route_pure);
	CHECK(SG_StrikeTestApplyDutyRoute(&tc, SG_STRIKE_DUTY_RECOVER,
	    CTF_TEAM_RED));
	CHECK(tc.goal_field == recover_field && tc.route_field == recover_field);
	CHECK(tc.route_pure);
	/* PRESS is the enemy base while our carrier owns the flag. It must not
	 * collapse into a duplicate carrier-support route. */
	sg_fields.to_flag_now[0][1] = carrier_field;
	sg_caco_team_belief.carrier[0].client = 3;
	CHECK(SG_StrikeTestApplyDutyRoute(&tc, SG_STRIKE_DUTY_PRESS,
	    CTF_TEAM_RED));
	CHECK(tc.goal_field == enemy_field && tc.route_field == enemy_field);
	sg_caco_team_belief.carrier[0].client = -1;
	CHECK(SG_StrikeTestApplyDutyRoute(&tc, SG_STRIKE_DUTY_PRESS,
	    CTF_TEAM_RED));
	CHECK(tc.goal_field == carrier_field && tc.route_field == carrier_field);
	/* Carrier support is not flooded until the frame after pickup.  The
	 * transient fallback remains homeward, never thief-bound. */
	sg_fields.our_carrier_valid[0] = false;
	CHECK(SG_StrikeTestApplyDutyRoute(&tc, SG_STRIKE_DUTY_ESCORT,
	    CTF_TEAM_RED));
	CHECK(tc.goal_field == home_field && tc.route_field == home_field);
	CHECK(tc.route_pure);
	sg_fields.our_carrier_valid[0] = true;
	/* Objective runs after effective strike duty is known.  A strike-assigned
	 * escort that already resolved a live lead/trail station must retain that
	 * formation; the generic carrier overlay used to erase it here. */
	tc.goal_field = formation_field;
	tc.route_field = formation_field;
	tc.route_pure = false;
	tc.escort_mission = true;
	tc.escort_formation = true;
	tc.scoop_mission = true;
	CHECK(SG_StrikeTestApplyDutyRoute(&tc, SG_STRIKE_DUTY_ESCORT,
	    CTF_TEAM_RED));
	CHECK(tc.goal_field == formation_field &&
	    tc.route_field == formation_field);
	CHECK(tc.route_pure);
	CHECK(!tc.scoop_mission);
	/* A generic effective escort still receives the ordinary carrier field. */
	tc.escort_formation = false;
	CHECK(SG_StrikeTestApplyDutyRoute(&tc, SG_STRIKE_DUTY_ESCORT,
	    CTF_TEAM_RED));
	CHECK(tc.goal_field == carrier_field && tc.route_field == carrier_field);
	CHECK(tc.route_pure);
	/* Restore the carrier duty for the downstream weapon/rally assertions. */
	CHECK(SG_StrikeTestApplyDutyRoute(&tc, SG_STRIKE_DUTY_CARRY,
	    CTF_TEAM_RED));
	CHECK(!tc.strike_weapon_pursuit);
	bot.rally_since = 42.0f;
	CHECK(SG_StrikeTestApplyRallyPolicy(&bot, &tc, &rally_hold));
	CHECK(bot.rally_since == 0.0f);
	CHECK(!rally_hold);
}

static void TestHumanOrderOwnsStrikeAdmission(void)
{
	CHECK(SG_StrikeTestAttackEligible(SG_ROLE_ATTACK, false, -1));
	CHECK(!SG_StrikeTestAttackEligible(SG_ROLE_DEFEND, false, -1));
	CHECK(!SG_StrikeTestAttackEligible(SG_ROLE_ATTACK, false,
	    SG_ROLE_ATTACK));
	CHECK(!SG_StrikeTestAttackEligible(SG_ROLE_ESCORT, false,
	    SG_ROLE_ESCORT));
	CHECK(!SG_StrikeTestAttackEligible(SG_ROLE_RECOVER, false,
	    SG_ROLE_RECOVER));
	/* Flag possession remains physical authority even when an old order is
	 * still inside its ninety-second lease. */
	CHECK(SG_StrikeTestAttackEligible(SG_ROLE_CARRY, true,
	    SG_ROLE_DEFEND));
}

static void TestDeathLocationRequiresPriorBelief(void)
{
	sg_belief_enemy_t records[3];

	memset(records, 0, sizeof(records));
	records[0].client = 4;
	records[0].seed = 1;
	records[0].seen_time = 8.0f;
	records[1].client = 4;
	records[1].seed = 2;
	records[1].seen_time = 9.0f;
	records[2].client = 5;
	records[2].seed = 0;
	records[2].seen_time = 9.5f;
	CHECK(SG_DeathBeliefSeed(records, 3, 4, 10.0f, 8.0f, 3) == 2);
	CHECK(SG_DeathBeliefSeed(records, 3, 6, 10.0f, 8.0f, 3) == -1);
	CHECK(SG_DeathBeliefSeed(records, 3, 4, 20.0f, 8.0f, 3) == -1);
	records[1].seen_time = 11.0f;
	CHECK(SG_DeathBeliefSeed(records, 3, 4, 10.0f, 8.0f, 3) == 1);
	records[0].seed = 3;
	CHECK(SG_DeathBeliefSeed(records, 3, 4, 10.0f, 8.0f, 3) == -1);
}

static void TestMissionHoldSurvivesGenericWedgeValve(void)
{
	CHECK(SG_RoleMissionHold(SG_ROLE_DEFEND, 1499, false, false));
	CHECK(SG_RoleMissionHold(SG_ROLE_ESCORT, 1499, false, false));
	CHECK(!SG_RoleMissionHold(SG_ROLE_ESCORT, 1499, false, true));
	CHECK(!SG_RoleMissionHold(SG_ROLE_ESCORT, 1500, false, false));
	CHECK(!SG_RoleMissionHold(SG_ROLE_ATTACK, 100, false, false));
	CHECK(SG_RoleMissionHold(SG_ROLE_ESCORT, SG_FIELD_INF, true, false));
	CHECK(SG_OptionalItemDetourAllowed(0, 0, SG_ROLE_ATTACK, 100, 3.0f));
	CHECK(!SG_OptionalItemDetourAllowed(1, 0, SG_ROLE_ATTACK, 100, 3.0f));
	CHECK(!SG_OptionalItemDetourAllowed(0, 1, SG_ROLE_ATTACK, 100, 3.0f));
	CHECK(!SG_OptionalItemDetourAllowed(0, 0, SG_ROLE_CARRY, 61, 3.0f));
	CHECK(SG_OptionalItemDetourAllowed(0, 0, SG_ROLE_CARRY, 60, 3.0f));
	CHECK(SG_OptionalItemDetourAllowed(0, 0, SG_ROLE_CARRY, 100, 2.99f));
}

static void TestCombatActivityResetsGenericWedgeClock(void)
{
	CHECK(!SG_WedgeClockReset(96.0f, false, false));
	CHECK(SG_WedgeClockReset(96.01f, false, false));
	CHECK(SG_WedgeClockReset(0.0f, true, false));
	CHECK(SG_WedgeClockReset(0.0f, false, true));
	CHECK(SG_WedgeKillHoldClear(false, false));
	CHECK(!SG_WedgeKillHoldClear(true, false));
	CHECK(!SG_WedgeKillHoldClear(false, true));
	CHECK(!SG_WedgeKillHoldClear(true, true));
	CHECK(SG_ObjectiveOrbitMayShelf(SG_ROLE_CARRY, false, false, true));
	CHECK(SG_ObjectiveOrbitMayShelf(SG_ROLE_ATTACK, true, true, false));
	CHECK(!SG_ObjectiveOrbitMayShelf(SG_ROLE_ATTACK, true, true, true));
	CHECK(!SG_ObjectiveOrbitMayShelf(SG_ROLE_ATTACK, true, false, false));
	CHECK(!SG_ObjectiveOrbitMayShelf(SG_ROLE_ATTACK, false, true, false));
	CHECK(!SG_ObjectiveOrbitMayShelf(SG_ROLE_DEFEND, true, true, false));
}

static void TestMissionAndCombatCannotShelveRoutes(void)
{
	CHECK(SG_RouteFailureWatchSuppressed(
	    SG_ROLE_ESCORT, 1499, false, false, false, false));
	CHECK(!SG_RouteFailureWatchSuppressed(
	    SG_ROLE_ESCORT, 1499, false, true, false, false));
	CHECK(SG_RouteFailureWatchSuppressed(
	    SG_ROLE_ESCORT, SG_FIELD_INF, true, false, false, false));
	CHECK(SG_RouteFailureWatchSuppressed(
	    SG_ROLE_ATTACK, 2000, false, false, true, false));
	CHECK(SG_RouteFailureWatchSuppressed(
	    SG_ROLE_ATTACK, 2000, false, false, false, true));
	CHECK(!SG_RouteFailureWatchSuppressed(
	    SG_ROLE_ATTACK, 2000, false, false, false, false));
	CHECK(SG_EnemyFlagTouchMissionActive(false, true));
	CHECK(SG_EnemyFlagTouchMissionActive(true, false));
	CHECK(!SG_EnemyFlagTouchMissionActive(false, false));
	CHECK(!SG_EnemyFlagTouchMissionActive(2, false));
	CHECK(SG_AttackDescentFallbackAllowed(
	    SG_EnemyFlagTouchMissionActive(false, true), true, 600, 500,
	    SG_FIELD_INF));
	CHECK(SG_RoleOwnsDefenseState(SG_ROLE_DEFEND));
	CHECK(!SG_RoleOwnsDefenseState(SG_ROLE_ATTACK));
	CHECK(!SG_RoleOwnsDefenseState(SG_ROLE_RECOVER));
	CHECK(!SG_RoleOwnsDefenseState(SG_ROLE_ESCORT));
}

int main(void)
{
	TestFreshTagAndOldCommitment();
	TestPureRouteChangeRetiresOnlyReversibleRun();
	TestDeadlineGoAndCurrentCandidate();
	TestSpeedHookAndStickyDrain();
	TestRocketJumpBoundaries();
	TestActionTable();
	TestLiftTeleportBoundaries();
	TestDoorLeaseRetirement();
	TestRailAndCarrierRoute();
	TestHumanOrderOwnsStrikeAdmission();
	TestDeathLocationRequiresPriorBelief();
	TestMissionHoldSurvivesGenericWedgeValve();
	TestCombatActivityResetsGenericWedgeClock();
	TestMissionAndCombatCannotShelveRoutes();
	if (failures)
		return 1;
	puts("sg_strike_transition_test: ok");
	return 0;
}
