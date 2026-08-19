/* sg_strike_test.c -- host-free deterministic offense coordinator tests. */
#include "slipgate/sg_strike.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, \
		    #condition); \
		failures++; \
	} \
} while (0)

static uint32_t Bit(int slot)
{
	return (uint32_t)1u << (unsigned)slot;
}

static int CountDuty(const sg_strike_team_t *team, sg_strike_duty_t duty)
{
	int count = 0;
	int slot;

	for (slot = 0; slot < SG_STRIKE_MAX_SLOTS; slot++)
		if (team->duty[slot] == duty)
			count++;
	return count;
}

static sg_strike_frame_t Frame(float now)
{
	sg_strike_frame_t frame;
	int slot;

	memset(&frame, 0, sizeof(frame));
	frame.now = now;
	frame.own_flag_home = 1;
	frame.enemy_flag_home = 1;
	frame.carrier_slot = -1;
	for (slot = 0; slot < SG_STRIKE_MAX_SLOTS; slot++)
	{
		frame.slot[slot].enemy_flag_goal_ms = -1;
		frame.slot[slot].recover_goal_ms = -1;
		frame.slot[slot].carrier_goal_ms = -1;
	}
	return frame;
}

static void AddAttacker(sg_strike_frame_t *frame, int slot,
	uint32_t life, int weapon_tier, int enemy_goal)
{
	frame->slot[slot].present = 1;
	frame->slot[slot].alive = 1;
	frame->slot[slot].attack_eligible = 1;
	frame->slot[slot].life_id = life;
	frame->slot[slot].weapon_tier = weapon_tier;
	frame->slot[slot].enemy_flag_goal_ms = enemy_goal;
	frame->slot[slot].recover_goal_ms = enemy_goal;
	frame->slot[slot].carrier_goal_ms = enemy_goal;
}

static void TestNormalFiveKeepsTwoDefenders(void)
{
	sg_strike_team_t team;
	sg_strike_frame_t frame = Frame(10.0f);

	/* Slots zero and one are the two role-policy-reserved defenders. */
	frame.slot[0].present = frame.slot[0].alive = 1;
	frame.slot[0].life_id = 10u;
	frame.slot[0].weapon_tier = 5;
	frame.slot[0].enemy_flag_goal_ms = 100;
	frame.slot[1] = frame.slot[0];
	frame.slot[1].life_id = 11u;
	AddAttacker(&frame, 2, 12u, 1, 1000);
	AddAttacker(&frame, 3, 13u, 1, 3000);
	AddAttacker(&frame, 4, 14u, 1, 6000);

	SG_StrikeReset(&team);
	CHECK(SG_StrikeStep(&team, &frame));
	CHECK(team.member_mask == (Bit(2) | Bit(3) | Bit(4)));
	CHECK(!SG_StrikeMember(&team, 0));
	CHECK(!SG_StrikeMember(&team, 1));
	CHECK(team.duty[2] == SG_STRIKE_DUTY_BREACH);
	CHECK(team.duty[3] == SG_STRIKE_DUTY_CLEAR);
	CHECK(team.duty[4] == SG_STRIKE_DUTY_PRESS);
	CHECK(team.phase == SG_STRIKE_ARM);
}

static void TestWeaponDeadlineIsPerLifeAndImmutable(void)
{
	sg_strike_team_t team;
	sg_strike_frame_t frame = Frame(20.0f);
	float deadline;

	AddAttacker(&frame, 0, 100u, 1, 9000);
	AddAttacker(&frame, 1, 101u, 1, 11000);
	SG_StrikeReset(&team);
	CHECK(SG_StrikeStep(&team, &frame));
	deadline = team.weapon_deadline[0];
	CHECK(fabsf(deadline - 25.0f) < 0.001f);
	CHECK(SG_StrikeMemberNeedsWeapon(&team, 0, frame.now));

	frame.now = 24.9f;
	CHECK(SG_StrikeStep(&team, &frame));
	CHECK(fabsf(team.weapon_deadline[0] - deadline) < 0.001f);
	CHECK(SG_StrikeMemberNeedsWeapon(&team, 0, frame.now));
	frame.now = 25.0f;
	CHECK(SG_StrikeStep(&team, &frame));
	CHECK(!SG_StrikeMemberNeedsWeapon(&team, 0, frame.now));
	CHECK((team.mission_ready_mask & Bit(0)) != 0u);

	frame.now = 26.0f;
	frame.slot[0].life_id = 200u;
	CHECK(SG_StrikeStep(&team, &frame));
	CHECK(fabsf(team.weapon_deadline[0] - 31.0f) < 0.001f);
	CHECK(SG_StrikeMemberNeedsWeapon(&team, 0, frame.now));
	frame.now = 26.1f;
	frame.slot[0].weapon_tier = SG_STRIKE_USABLE_WEAPON_TIER;
	CHECK(SG_StrikeStep(&team, &frame));
	CHECK((team.weapon_ready_mask & Bit(0)) != 0u);
	CHECK(!SG_StrikeMemberNeedsWeapon(&team, 0, frame.now));
}

static void TestWeaponDeadlineSurvivesSameLifeReentry(void)
{
	sg_strike_team_t team;
	sg_strike_frame_t frame = Frame(30.0f);
	uint32_t epoch;
	float route_deadline;
	float role_deadline;

	AddAttacker(&frame, 0, 210u, 1, 3000);
	AddAttacker(&frame, 1, 211u, 1, 6000);
	AddAttacker(&frame, 2, 212u, 1, 9000);
	SG_StrikeReset(&team);
	CHECK(SG_StrikeStep(&team, &frame));
	epoch = team.epoch;
	route_deadline = team.weapon_deadline[0];
	role_deadline = team.weapon_deadline[1];
	CHECK(fabsf(route_deadline - 35.0f) < 0.001f);
	CHECK(fabsf(role_deadline - 35.0f) < 0.001f);

	/* Neither a transient missing route nor a transient role-policy removal
	 * grants the same life another five seconds to obtain a usable weapon. */
	frame.now = 31.0f;
	frame.slot[0].enemy_flag_goal_ms = -1;
	frame.slot[1].attack_eligible = 0;
	CHECK(SG_StrikeStep(&team, &frame));
	CHECK(team.epoch == epoch);
	CHECK(!SG_StrikeMember(&team, 0));
	CHECK(!SG_StrikeMember(&team, 1));
	CHECK(SG_StrikeMember(&team, 2));
	CHECK(team.member_life[0] == 210u);
	CHECK(team.member_life[1] == 211u);
	CHECK(fabsf(team.weapon_deadline[0] - route_deadline) < 0.001f);
	CHECK(fabsf(team.weapon_deadline[1] - role_deadline) < 0.001f);

	frame.now = 34.9f;
	frame.slot[0].enemy_flag_goal_ms = 3000;
	frame.slot[1].attack_eligible = 1;
	CHECK(SG_StrikeStep(&team, &frame));
	CHECK(team.epoch == epoch);
	CHECK(SG_StrikeMember(&team, 0));
	CHECK(SG_StrikeMember(&team, 1));
	CHECK(fabsf(team.weapon_deadline[0] - route_deadline) < 0.001f);
	CHECK(fabsf(team.weapon_deadline[1] - role_deadline) < 0.001f);
	CHECK(SG_StrikeMemberNeedsWeapon(&team, 0, frame.now));
	CHECK(SG_StrikeMemberNeedsWeapon(&team, 1, frame.now));

	frame.now = 35.0f;
	CHECK(SG_StrikeStep(&team, &frame));
	CHECK((team.mission_ready_mask & Bit(0)) != 0u);
	CHECK((team.mission_ready_mask & Bit(1)) != 0u);
	CHECK(!SG_StrikeMemberNeedsWeapon(&team, 0, frame.now));
	CHECK(!SG_StrikeMemberNeedsWeapon(&team, 1, frame.now));
}

static void TestWeaponDeadlineSurvivesPresenceAndAliveReentry(void)
{
	sg_strike_team_t team;
	sg_strike_frame_t frame = Frame(40.0f);
	uint32_t epoch;
	float present_deadline;
	float alive_deadline;

	AddAttacker(&frame, 0, 220u, 1, 3000);
	AddAttacker(&frame, 1, 221u, 1, 6000);
	AddAttacker(&frame, 2, 222u, 1, 9000);
	SG_StrikeReset(&team);
	CHECK(SG_StrikeStep(&team, &frame));
	epoch = team.epoch;
	present_deadline = team.weapon_deadline[0];
	alive_deadline = team.weapon_deadline[1];
	CHECK(fabsf(present_deadline - 45.0f) < 0.001f);
	CHECK(fabsf(alive_deadline - 45.0f) < 0.001f);

	frame.now = 41.0f;
	frame.slot[0].present = 0;
	frame.slot[1].alive = 0;
	CHECK(SG_StrikeStep(&team, &frame));
	CHECK(team.epoch == epoch);
	CHECK(!SG_StrikeMember(&team, 0));
	CHECK(!SG_StrikeMember(&team, 1));
	CHECK(SG_StrikeMember(&team, 2));
	CHECK(team.member_life[0] == 220u);
	CHECK(team.member_life[1] == 221u);
	CHECK(fabsf(team.weapon_deadline[0] - present_deadline) < 0.001f);
	CHECK(fabsf(team.weapon_deadline[1] - alive_deadline) < 0.001f);

	frame.now = 44.9f;
	frame.slot[0].present = 1;
	frame.slot[1].alive = 1;
	CHECK(SG_StrikeStep(&team, &frame));
	CHECK(team.epoch == epoch);
	CHECK(SG_StrikeMember(&team, 0));
	CHECK(SG_StrikeMember(&team, 1));
	CHECK(fabsf(team.weapon_deadline[0] - present_deadline) < 0.001f);
	CHECK(fabsf(team.weapon_deadline[1] - alive_deadline) < 0.001f);
	CHECK(SG_StrikeMemberNeedsWeapon(&team, 0, frame.now));
	CHECK(SG_StrikeMemberNeedsWeapon(&team, 1, frame.now));

	frame.now = 45.0f;
	CHECK(SG_StrikeStep(&team, &frame));
	CHECK((team.mission_ready_mask & Bit(0)) != 0u);
	CHECK((team.mission_ready_mask & Bit(1)) != 0u);
}

static void TestWeaponDeadlineRearmsAfterNewLifeReentry(void)
{
	sg_strike_team_t team;
	sg_strike_frame_t frame = Frame(60.0f);
	uint32_t epoch;
	float old_deadline;

	AddAttacker(&frame, 0, 230u, 1, 3000);
	AddAttacker(&frame, 1, 231u, 1, 9000);
	SG_StrikeReset(&team);
	CHECK(SG_StrikeStep(&team, &frame));
	epoch = team.epoch;
	old_deadline = team.weapon_deadline[0];
	CHECK(fabsf(old_deadline - 65.0f) < 0.001f);

	frame.now = 61.0f;
	frame.slot[0].alive = 0;
	CHECK(SG_StrikeStep(&team, &frame));
	CHECK(team.epoch == epoch);
	CHECK(!SG_StrikeMember(&team, 0));
	CHECK(team.member_life[0] == 230u);
	CHECK(fabsf(team.weapon_deadline[0] - old_deadline) < 0.001f);

	frame.now = 62.0f;
	frame.slot[0].alive = 1;
	frame.slot[0].life_id = 330u;
	CHECK(SG_StrikeStep(&team, &frame));
	CHECK(team.epoch == epoch);
	CHECK(SG_StrikeMember(&team, 0));
	CHECK(team.member_life[0] == 330u);
	CHECK(fabsf(team.weapon_deadline[0] - 67.0f) < 0.001f);
	CHECK(SG_StrikeMemberNeedsWeapon(&team, 0, frame.now));
}

static void TestMembershipCapsAtFourDeterministically(void)
{
	sg_strike_team_t first;
	sg_strike_team_t repeat;
	sg_strike_frame_t frame = Frame(50.0f);
	int slot;

	for (slot = 0; slot < 6; slot++)
		AddAttacker(&frame, slot, (uint32_t)(300 + slot), 2,
		    6000 - slot * 1000);
	SG_StrikeReset(&first);
	SG_StrikeReset(&repeat);
	CHECK(SG_StrikeStep(&first, &frame));
	CHECK(SG_StrikeStep(&repeat, &frame));
	CHECK(first.member_mask ==
	    (Bit(2) | Bit(3) | Bit(4) | Bit(5)));
	CHECK(first.member_mask == repeat.member_mask);

	/* Incumbent membership is stable within the epoch even if route order
	 * changes; replacements, not frame-by-frame auctions, change the squad. */
	frame.now = 50.1f;
	frame.slot[0].enemy_flag_goal_ms = 0;
	frame.slot[5].enemy_flag_goal_ms = 12000;
	CHECK(SG_StrikeStep(&first, &frame));
	CHECK(first.member_mask ==
	    (Bit(2) | Bit(3) | Bit(4) | Bit(5)));
}

static void TestSharedGoAndBoundedForm(void)
{
	sg_strike_team_t team;
	sg_strike_frame_t frame = Frame(100.0f);
	float deadline;

	AddAttacker(&frame, 0, 1u, 2, 1000);
	AddAttacker(&frame, 1, 2u, 2, 4000);
	AddAttacker(&frame, 2, 3u, 2, 9000);
	SG_StrikeReset(&team);
	CHECK(SG_StrikeStep(&team, &frame));
	CHECK(team.phase == SG_STRIKE_FORM);
	CHECK(team.hold_mask == Bit(0));
	deadline = team.form_deadline;
	CHECK(fabsf(deadline - 103.0f) < 0.001f);

	frame.now = 101.0f;
	frame.slot[1].enemy_flag_goal_ms = 5000;
	CHECK(SG_StrikeStep(&team, &frame));
	CHECK(team.phase == SG_STRIKE_FORM);
	CHECK(fabsf(team.form_deadline - deadline) < 0.001f);
	frame.now = 103.0f;
	CHECK(SG_StrikeStep(&team, &frame));
	CHECK(team.phase == SG_STRIKE_GO);
	CHECK(team.hold_mask == 0u);
	CHECK(team.rush_mask == team.member_mask);

	frame = Frame(200.0f);
	AddAttacker(&frame, 0, 11u, 2, 1000);
	AddAttacker(&frame, 1, 12u, 2, 2400);
	AddAttacker(&frame, 2, 13u, 2, 9000);
	SG_StrikeReset(&team);
	CHECK(SG_StrikeStep(&team, &frame));
	CHECK(team.phase == SG_STRIKE_GO);
	CHECK(SG_StrikeMemberRushes(&team, 0));
	CHECK(SG_StrikeMemberRushes(&team, 1));
	CHECK(SG_StrikeMemberRushes(&team, 2));
}

static void TestImmediateReleaseAndSoloNeverWaits(void)
{
	sg_strike_team_t team;
	sg_strike_frame_t frame = Frame(300.0f);

	AddAttacker(&frame, 0, 21u, 1, 1000);
	AddAttacker(&frame, 1, 22u, 1, 7000);
	frame.slot[0].direct_flag_touch = 1;
	SG_StrikeReset(&team);
	CHECK(SG_StrikeStep(&team, &frame));
	CHECK(team.phase == SG_STRIKE_GO);
	CHECK(team.hold_mask == 0u);
	CHECK(team.rush_mask == team.member_mask);

	frame = Frame(310.0f);
	AddAttacker(&frame, 0, 31u, 1, 1000);
	AddAttacker(&frame, 1, 32u, 1, 7000);
	frame.recent_enemy_room_death = 1;
	SG_StrikeReset(&team);
	CHECK(SG_StrikeStep(&team, &frame));
	CHECK(team.phase == SG_STRIKE_GO);

	frame = Frame(320.0f);
	AddAttacker(&frame, 0, 41u, 1, 1000);
	SG_StrikeReset(&team);
	CHECK(SG_StrikeStep(&team, &frame));
	CHECK(team.phase == SG_STRIKE_ARM);
	CHECK(!SG_StrikeMemberShouldHold(&team, 0));
	CHECK(SG_StrikeMemberNeedsWeapon(&team, 0, frame.now));
	frame.now = 320.1f;
	frame.slot[0].weapon_tier = 2;
	CHECK(SG_StrikeStep(&team, &frame));
	CHECK(team.phase == SG_STRIKE_GO);
	CHECK(!SG_StrikeMemberShouldHold(&team, 0));
}

static void TestOneRecovererPreservesAttack(void)
{
	sg_strike_team_t team;
	sg_strike_frame_t frame = Frame(400.0f);
	int slot;

	frame.own_flag_home = 0;
	for (slot = 0; slot < 4; slot++)
	{
		AddAttacker(&frame, slot, (uint32_t)(50 + slot), 2,
		    8000 + slot * 1000);
		frame.slot[slot].recover_goal_ms = 4000 - slot * 1000;
	}
	SG_StrikeReset(&team);
	CHECK(SG_StrikeStep(&team, &frame));
	CHECK(CountDuty(&team, SG_STRIKE_DUTY_RECOVER) == 1);
	CHECK(team.duty[3] == SG_STRIKE_DUTY_RECOVER);
	CHECK(CountDuty(&team, SG_STRIKE_DUTY_BREACH) +
	    CountDuty(&team, SG_STRIKE_DUTY_CLEAR) +
	    CountDuty(&team, SG_STRIKE_DUTY_PRESS) == 3);

	/* Role policy remains authoritative while our flag is away.  The excluded
	 * incumbent is evicted and recovery is reassigned among eligible members. */
	frame.now = 400.1f;
	frame.slot[3].attack_eligible = 0;
	CHECK(SG_StrikeStep(&team, &frame));
	CHECK(team.member_mask == (Bit(0) | Bit(1) | Bit(2)));
	CHECK(team.duty[3] == SG_STRIKE_DUTY_NONE);
	CHECK(CountDuty(&team, SG_STRIKE_DUTY_RECOVER) == 1);
	CHECK(team.duty[2] == SG_STRIKE_DUTY_RECOVER);
	CHECK(CountDuty(&team, SG_STRIKE_DUTY_BREACH) +
	    CountDuty(&team, SG_STRIKE_DUTY_CLEAR) +
	    CountDuty(&team, SG_STRIKE_DUTY_PRESS) == 2);

	frame = Frame(410.0f);
	frame.own_flag_home = 0;
	AddAttacker(&frame, 0, 60u, 2, 8000);
	AddAttacker(&frame, 1, 61u, 2, 9000);
	SG_StrikeReset(&team);
	CHECK(SG_StrikeStep(&team, &frame));
	CHECK(CountDuty(&team, SG_STRIKE_DUTY_RECOVER) == 0);
	CHECK(CountDuty(&team, SG_STRIKE_DUTY_BREACH) +
	    CountDuty(&team, SG_STRIKE_DUTY_CLEAR) == 2);
}

static void TestDutiesStayStableUntilEgress(void)
{
	sg_strike_team_t team;
	sg_strike_frame_t frame = Frame(500.0f);

	AddAttacker(&frame, 0, 70u, 2, 1000);
	AddAttacker(&frame, 1, 71u, 2, 4000);
	AddAttacker(&frame, 2, 72u, 2, 9000);
	SG_StrikeReset(&team);
	CHECK(SG_StrikeStep(&team, &frame));
	CHECK(team.duty[0] == SG_STRIKE_DUTY_BREACH);
	CHECK(team.duty[1] == SG_STRIKE_DUTY_CLEAR);
	frame.now = 500.1f;
	frame.slot[0].enemy_flag_goal_ms = 9000;
	frame.slot[1].enemy_flag_goal_ms = 100;
	frame.slot[2].enemy_flag_goal_ms = 200;
	CHECK(SG_StrikeStep(&team, &frame));
	CHECK(team.duty[0] == SG_STRIKE_DUTY_BREACH);
	CHECK(team.duty[1] == SG_STRIKE_DUTY_CLEAR);
	CHECK(team.duty[2] == SG_STRIKE_DUTY_PRESS);
}

static void TestPickupEscortLossAndReplacement(void)
{
	sg_strike_team_t team;
	sg_strike_frame_t frame = Frame(600.0f);
	uint32_t epoch;

	AddAttacker(&frame, 0, 80u, 2, 1000);
	AddAttacker(&frame, 1, 81u, 2, 3000);
	AddAttacker(&frame, 2, 82u, 2, 5000);
	SG_StrikeReset(&team);
	CHECK(SG_StrikeStep(&team, &frame));
	epoch = team.epoch;

	frame.now = 601.0f;
	frame.events = SG_STRIKE_EVENT_PICKUP;
	frame.enemy_flag_home = 0;
	frame.carrier_slot = 0;
	frame.slot[0].carrying = 1;
	frame.slot[1].carrier_goal_ms = 800;
	frame.slot[2].carrier_goal_ms = 200;
	CHECK(SG_StrikeStep(&team, &frame));
	CHECK(team.phase == SG_STRIKE_EGRESS);
	CHECK(team.duty[0] == SG_STRIKE_DUTY_CARRY);
	CHECK(team.duty[1] == SG_STRIKE_DUTY_CLEAR);
	CHECK(team.duty[2] == SG_STRIKE_DUTY_ESCORT);
	CHECK(fabsf(team.clear_until - 606.0f) < 0.001f);

	frame.now = 606.1f;
	frame.events = SG_STRIKE_EVENT_NONE;
	CHECK(SG_StrikeStep(&team, &frame));
	CHECK(CountDuty(&team, SG_STRIKE_DUTY_CLEAR) == 0);
	CHECK(team.duty[2] == SG_STRIKE_DUTY_ESCORT);
	CHECK(team.duty[1] == SG_STRIKE_DUTY_PRESS);

	frame.now = 607.0f;
	frame.events = SG_STRIKE_EVENT_CARRIER_LOSS;
	frame.carrier_slot = -1;
	frame.enemy_flag_dropped = 1;
	frame.slot[0].alive = 0;
	frame.slot[0].carrying = 0;
	frame.slot[1].enemy_flag_goal_ms = 900;
	frame.slot[2].enemy_flag_goal_ms = 300;
	AddAttacker(&frame, 3, 83u, 1, 1200);
	CHECK(SG_StrikeStep(&team, &frame));
	CHECK(team.phase == SG_STRIKE_EGRESS);
	CHECK(!SG_StrikeMember(&team, 0));
	CHECK(SG_StrikeMember(&team, 3));
	CHECK(team.duty[2] == SG_STRIKE_DUTY_BREACH);
	CHECK(team.epoch == epoch);
}

static void TestExternalCarrierDoesNotExpandRoster(void)
{
	sg_strike_team_t team;
	sg_strike_frame_t frame = Frame(650.0f);
	uint32_t roster;
	int slot;

	for (slot = 0; slot < 6; slot++)
		AddAttacker(&frame, slot, (uint32_t)(500 + slot), 2,
		    1000 + slot * 1000);
	SG_StrikeReset(&team);
	CHECK(SG_StrikeStep(&team, &frame));
	roster = Bit(0) | Bit(1) | Bit(2) | Bit(3);
	CHECK(team.member_mask == roster);
	CHECK(!SG_StrikeMember(&team, 5));

	/* A fifth eligible attacker made the actual pickup.  The stable four keep
	 * their roster and supply CLEAR/ESCORT; slot five alone owns CARRY/home. */
	frame.now = 650.1f;
	frame.events = SG_STRIKE_EVENT_PICKUP;
	frame.enemy_flag_home = 0;
	frame.carrier_slot = 5;
	frame.slot[5].carrying = 1;
	frame.slot[0].carrier_goal_ms = 300;
	frame.slot[1].carrier_goal_ms = 400;
	frame.slot[2].carrier_goal_ms = 200;
	frame.slot[3].carrier_goal_ms = 100;
	CHECK(SG_StrikeStep(&team, &frame));
	CHECK(team.phase == SG_STRIKE_EGRESS);
	CHECK(team.member_mask == roster);
	CHECK(!SG_StrikeMember(&team, 5));
	CHECK(SG_StrikeParticipant(&team, 5));
	CHECK(team.duty[5] == SG_STRIKE_DUTY_CARRY);
	CHECK(CountDuty(&team, SG_STRIKE_DUTY_CARRY) == 1);
	CHECK(CountDuty(&team, SG_STRIKE_DUTY_CLEAR) == 1);
	CHECK(CountDuty(&team, SG_STRIKE_DUTY_ESCORT) == 1);
}

static void TestWeaponRouteRetirementVerdicts(void)
{
	sg_strike_weapon_controller_state_t state;

	CHECK(SG_StrikeWeaponRouteVerdict(0, 1, 1, 0) ==
	    SG_STRIKE_WEAPON_ROUTE_CLEAR);
	CHECK(SG_StrikeWeaponRouteVerdict(1, 1, 0, 0) ==
	    SG_STRIKE_WEAPON_ROUTE_OWN);
	CHECK(SG_StrikeWeaponRouteVerdict(1, 1, 1, 0) ==
	    SG_STRIKE_WEAPON_ROUTE_OWN);
	CHECK(SG_StrikeWeaponRouteVerdict(1, 0, 0, 0) ==
	    SG_STRIKE_WEAPON_ROUTE_CLEAR);
	CHECK(SG_StrikeWeaponRouteVerdict(1, 0, 1, 0) ==
	    SG_STRIKE_WEAPON_ROUTE_DRAIN);
	/* Once DRAIN begins, a transient return of weapon authority cannot turn
	 * the exact physical controller back into OWN. */
	CHECK(SG_StrikeWeaponRouteVerdict(1, 1, 1, 1) ==
	    SG_STRIKE_WEAPON_ROUTE_DRAIN);
	CHECK(SG_StrikeWeaponRouteVerdict(1, 1, 0, 1) ==
	    SG_STRIKE_WEAPON_ROUTE_CLEAR);

	memset(&state, 0, sizeof(state));
	state.action = RL_RUN;
	state.hook_phase = 1;
	CHECK(!SG_StrikeWeaponControllerPhysical(&state));
	state.hook_phase = 2;
	CHECK(SG_StrikeWeaponControllerPhysical(&state));
	state.hook_phase = 0;
	state.action = RL_ROCKETJUMP;
	state.rocketjump_phase = 1;
	CHECK(!SG_StrikeWeaponControllerPhysical(&state));
	state.rocketjump_phase = 2;
	CHECK(SG_StrikeWeaponControllerPhysical(&state));

	/* Guard acquisition/canonical staging is cancelable before contact.  The
	 * first exact mechanism touch is the irreversible boundary. */
	memset(&state, 0, sizeof(state));
	state.action = RL_DOOR;
	state.declared_started = 1;
	CHECK(!SG_StrikeWeaponControllerPhysical(&state));
	state.declared_touched = 1;
	CHECK(SG_StrikeWeaponControllerPhysical(&state));
	memset(&state, 0, sizeof(state));
	state.action = RL_LIFT;
	state.declared_started = 1;
	CHECK(!SG_StrikeWeaponControllerPhysical(&state));
	state.declared_touched = 1;
	CHECK(SG_StrikeWeaponControllerPhysical(&state));
	memset(&state, 0, sizeof(state));
	state.action = RL_TELEPORT;
	state.declared_started = 1;
	CHECK(!SG_StrikeWeaponControllerPhysical(&state));
	state.declared_touched = 1;
	CHECK(SG_StrikeWeaponControllerPhysical(&state));
	memset(&state, 0, sizeof(state));
	state.action = RL_TELEPORT;
	state.swim_active = 1;
	CHECK(SG_StrikeWeaponControllerPhysical(&state));

	CHECK(SG_StrikeWeaponDoorRetirement(1, 0, 0) ==
	    SG_STRIKE_WEAPON_DOOR_RELEASE);
	CHECK(SG_StrikeWeaponDoorRetirement(0, 0, 1) ==
	    SG_STRIKE_WEAPON_DOOR_HOLD);
	CHECK(SG_StrikeWeaponDoorRetirement(0, 1, 1) ==
	    SG_STRIKE_WEAPON_DOOR_TERMINAL);
	CHECK(SG_StrikeWeaponDoorRetirement(0, 0, 0) ==
	    SG_STRIKE_WEAPON_DOOR_TERMINAL);
	CHECK(SG_StrikeGenericRailAllowed(0));
	CHECK(!SG_StrikeGenericRailAllowed(1));
}

static void TestReturnCaptureAndLifecycleReset(void)
{
	sg_strike_team_t team;
	sg_strike_frame_t frame = Frame(700.0f);
	uint32_t epoch;

	AddAttacker(&frame, 0, 90u, 2, 8000);
	AddAttacker(&frame, 1, 91u, 2, 9000);
	AddAttacker(&frame, 2, 92u, 2, 10000);
	frame.enemy_flag_home = 0;
	frame.enemy_flag_dropped = 1;
	SG_StrikeReset(&team);
	CHECK(SG_StrikeStep(&team, &frame));
	CHECK(team.phase == SG_STRIKE_EGRESS);
	epoch = team.epoch;

	frame.now = 701.0f;
	frame.events = SG_STRIKE_EVENT_FLAG_RETURN;
	frame.enemy_flag_home = 1;
	frame.enemy_flag_dropped = 0;
	CHECK(SG_StrikeStep(&team, &frame));
	CHECK(team.epoch == epoch + 1u);
	CHECK(team.phase == SG_STRIKE_ARM);
	epoch = team.epoch;

	frame.now = 702.0f;
	frame.events = SG_STRIKE_EVENT_PICKUP;
	frame.enemy_flag_home = 0;
	frame.carrier_slot = 0;
	frame.slot[0].carrying = 1;
	CHECK(SG_StrikeStep(&team, &frame));
	CHECK(team.phase == SG_STRIKE_EGRESS);
	frame.now = 703.0f;
	frame.events = SG_STRIKE_EVENT_CAPTURE;
	frame.enemy_flag_home = 1;
	frame.carrier_slot = -1;
	frame.slot[0].carrying = 0;
	CHECK(SG_StrikeStep(&team, &frame));
	CHECK(team.epoch == epoch + 1u);
	CHECK(team.phase == SG_STRIKE_ARM);

	frame.now = 704.0f;
	frame.events = SG_STRIKE_EVENT_NONE;
	frame.slot[1].alive = 0;
	AddAttacker(&frame, 3, 93u, 1, 7000);
	CHECK(SG_StrikeStep(&team, &frame));
	CHECK(!SG_StrikeMember(&team, 1));
	CHECK(SG_StrikeMember(&team, 3));
	CHECK(fabsf(team.weapon_deadline[3] - 709.0f) < 0.001f);
	frame.now = 705.0f;
	frame.slot[3].life_id = 193u;
	CHECK(SG_StrikeStep(&team, &frame));
	CHECK(fabsf(team.weapon_deadline[3] - 710.0f) < 0.001f);

	frame.now = 706.0f;
	frame.events = SG_STRIKE_EVENT_LEVEL_RESET;
	CHECK(SG_StrikeStep(&team, &frame));
	CHECK(team.epoch == 0u);
	CHECK(team.phase == SG_STRIKE_IDLE);
	CHECK(team.member_mask == 0u);
	CHECK(team.carrier_slot == -1);
}

static void TestInvalidInputDoesNotMutate(void)
{
	sg_strike_team_t team;
	sg_strike_frame_t frame = Frame(800.0f);
	sg_strike_team_t before;

	AddAttacker(&frame, 0, 1000u, 2, 1000);
	SG_StrikeReset(&team);
	CHECK(SG_StrikeStep(&team, &frame));
	before = team;
	frame.now = NAN;
	CHECK(!SG_StrikeStep(&team, &frame));
	CHECK(memcmp(&team, &before, sizeof(team)) == 0);
}

static void TestHomeFlagApproachPricing(void)
{
	CHECK(SG_StrikeFlagApproachPrice(1, 1, 500.0f, 300.0f, 0.0f,
	    500, 500) == -100.0f);
	CHECK(SG_StrikeFlagApproachPrice(1, 1, 400.0f, 350.0f, 0.0f,
	    500, 625) == -25.0f);
	CHECK(SG_StrikeFlagApproachPrice(1, 1, 400.0f, 385.0f, 0.0f,
	    500, 500) == 0.0f); /* less than a meaningful body step */
	CHECK(SG_StrikeFlagApproachPrice(1, 1, 160.0f, 80.0f, 0.0f,
	    200, 100) == 0.0f); /* direct-touch controller owns this band */
	CHECK(SG_StrikeFlagApproachPrice(1, 1, 601.0f, 300.0f, 0.0f,
	    700, 600) == 0.0f);
	CHECK(SG_StrikeFlagApproachPrice(1, 0, 400.0f, 200.0f, 0.0f,
	    500, 400) == 0.0f); /* mechanisms retain exact authority */
	CHECK(SG_StrikeFlagApproachPrice(1, 1, 400.0f, 200.0f, 97.0f,
	    500, 400) == 0.0f);
	CHECK(SG_StrikeFlagApproachPrice(1, 1, 400.0f, 200.0f, 0.0f,
	    500, 626) == 0.0f); /* not a near-plateau route */
	CHECK(SG_StrikeFlagApproachPrice(1, 1, NAN, 200.0f, 0.0f,
	    500, 400) == 0.0f);
}

int main(void)
{
	TestNormalFiveKeepsTwoDefenders();
	TestWeaponDeadlineIsPerLifeAndImmutable();
	TestWeaponDeadlineSurvivesSameLifeReentry();
	TestWeaponDeadlineSurvivesPresenceAndAliveReentry();
	TestWeaponDeadlineRearmsAfterNewLifeReentry();
	TestMembershipCapsAtFourDeterministically();
	TestSharedGoAndBoundedForm();
	TestImmediateReleaseAndSoloNeverWaits();
	TestOneRecovererPreservesAttack();
	TestDutiesStayStableUntilEgress();
	TestPickupEscortLossAndReplacement();
	TestExternalCarrierDoesNotExpandRoster();
	TestWeaponRouteRetirementVerdicts();
	TestReturnCaptureAndLifecycleReset();
	TestInvalidInputDoesNotMutate();
	TestHomeFlagApproachPricing();
	if (failures)
	{
		fprintf(stderr, "sg_strike_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("sg_strike_test: ok");
	return 0;
}
