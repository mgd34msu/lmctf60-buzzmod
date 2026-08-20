#include "g_local.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_bot.h"
#include "slipgate/sg_descend.h"
#include "slipgate/sg_traversal_transition.h"

#include <stdio.h>
#include <string.h>

static int failures;
static rune_link_t links[2];
static int route_field[1];
static rune_t rune;

void SG_StrikeTestSetRune(rune_t *value);

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		    __FILE__, __LINE__, #condition); \
		failures++; \
	} \
} while (0)

static sg_bot_t Bot(void)
{
	sg_bot_t bot;

	memset(&bot, 0, sizeof(bot));
	bot.commit_link = -1;
	bot.sticky_link = -1;
	bot.hook_link = -1;
	bot.jump_link = -1;
	bot.drop_link = -1;
	return bot;
}

static void ResetWorld(void)
{
	memset(links, 0, sizeof(links));
	memset(&rune, 0, sizeof(rune));
	rune.hdr.num_links = 2;
	rune.links = links;
	SG_StrikeTestSetRune(&rune);
}

static void ArmBallistic(sg_bot_t *bot, int action, qboolean physical)
{
	*bot = Bot();
	links[1].action = action;
	bot->commit_link = bot->sticky_link = 1;
	bot->commit_until = 30.0f;
	bot->latch_until = 25.0f;
	bot->commit_route_goal = (sg_field_key_t){ route_field, 0 };
	if (action == RL_JUMP)
	{
		bot->jump_link = 1;
		bot->jump_started = physical;
	}
	else if (action == RL_DROP)
	{
		bot->drop_link = 1;
		bot->drop_started = physical;
	}
	else
	{
		bot->rj_phase = physical ? 2 : 1;
		bot->rj_deadline = 30.0f;
	}
}

static sg_bot_t SpeedHookRun(void)
{
	sg_bot_t bot = Bot();

	links[1].action = RL_RUN;
	bot.hook_deadline = 40.0f;
	bot.speedhook = true;
	bot.commit_link = bot.sticky_link = bot.rail_link = 1;
	bot.commit_until = bot.latch_until = bot.rail_until = 30.0f;
	bot.rail_stage = 1;
	bot.commit_route_goal = (sg_field_key_t){ route_field, 0 };
	return bot;
}

static sg_bot_t AimingSpeedHook(void)
{
	sg_bot_t bot = SpeedHookRun();

	bot.hook_phase = 1;
	return bot;
}

static sg_bot_t PullingSpeedHook(void)
{
	sg_bot_t bot = SpeedHookRun();

	bot.hook_phase = 2;
	bot.speedhook_pull_applied = true;
	return bot;
}

static sg_bot_t ArmedSpeedHook(void)
{
	sg_bot_t bot = SpeedHookRun();

	bot.hook_phase = 2;
	bot.hook_link = 9;
	bot.hook_bite_logged = true;
	bot.hook_attached_validated = true;
	bot.flow_release = true;
	return bot;
}

static void TestCarryStartRetiresOnlyReversibleTraversal(void)
{
	const int actions[] = { RL_JUMP, RL_DROP, RL_ROCKETJUMP };
	sg_bot_t bot;
	size_t index;

	for (index = 0; index < sizeof(actions) / sizeof(actions[0]); index++)
	{
		ResetWorld();
		ArmBallistic(&bot, actions[index], false);
		SG_CarryStartRetireSupersededRoute(&bot, true);
		CHECK(bot.commit_link == -1 && bot.commit_until == 0.0f &&
		    bot.commit_route_goal.field == NULL);
		CHECK(bot.sticky_link == -1 && bot.latch_until == 0.0f);
		if (actions[index] == RL_JUMP)
			CHECK(bot.jump_link == -1 && !bot.jump_started);
		else if (actions[index] == RL_DROP)
			CHECK(bot.drop_link == -1 && !bot.drop_started);
		else
			CHECK(bot.rj_phase == 0 && bot.rj_deadline == 0.0f);
		ResetWorld();
		ArmBallistic(&bot, actions[index], true);
		SG_CarryStartRetireSupersededRoute(&bot, true);
		CHECK(bot.commit_link == 1);
	}
	ArmBallistic(&bot, RL_JUMP, false);
	SG_CarryStartRetireSupersededRoute(&bot, false);
	CHECK(bot.commit_link == 1 && bot.jump_link == 1);
	ResetWorld();
	bot = Bot();
	links[1].action = RL_HOOK;
	bot.commit_link = bot.hook_link = 1;
	bot.hook_phase = 1;
	SG_CarryStartRetireSupersededRoute(&bot, true);
	CHECK(bot.commit_link == -1 && bot.hook_phase == 0);

	bot = AimingSpeedHook();
	SG_CarryStartRetireSupersededRoute(&bot, true);
	CHECK(bot.commit_link == -1 && bot.commit_until == 0.0f &&
	    bot.commit_route_goal.field == NULL);
	CHECK(bot.hook_phase == 0 && !bot.speedhook &&
	    bot.hook_deadline == 0.0f);
	CHECK(bot.hook_link == -1 && !bot.hook_bite_logged &&
	    !bot.hook_attached_validated);
	CHECK(bot.sticky_link == -1 && bot.latch_until == 0.0f &&
	    bot.rail_link == -1 && bot.rail_stage == 0 && bot.rail_until == 0.0f);

	bot = PullingSpeedHook();
	SG_CarryStartRetireSupersededRoute(&bot, true);
	CHECK(bot.commit_link == 1 && bot.hook_phase == 2 && bot.speedhook &&
	    bot.speedhook_pull_applied);
}

static void TestStrikeDutyRetiresSupersededRoute(void)
{
	sg_bot_t bot;

	ResetWorld();
	bot = Bot();
	bot.tac_seed = 7;
	bot.tac_time = 12.0f;
	bot.commit_link = bot.sticky_link = 0;
	bot.commit_until = bot.latch_until = 30.0f;
	bot.commit_route_goal = (sg_field_key_t){ route_field, 0 };
	SG_StrikeDutyRetireSupersededRoute(&bot, true);
	CHECK(bot.tac_seed == -1 && bot.tac_time == 0.0f);
	CHECK(bot.commit_link == -1 && bot.commit_until == 0.0f &&
	    bot.commit_route_goal.field == NULL);
	CHECK(bot.sticky_link == -1 && bot.latch_until == 0.0f);

	bot = Bot();
	bot.tac_seed = 7;
	bot.commit_link = bot.sticky_link = 0;
	SG_StrikeDutyRetireSupersededRoute(&bot, false);
	CHECK(bot.tac_seed == 7 && bot.commit_link == 0 && bot.sticky_link == 0);
	bot.hook_phase = 2;
	links[0].action = RL_HOOK;
	SG_StrikeDutyRetireSupersededRoute(&bot, true);
	CHECK(bot.tac_seed == -1 && bot.commit_link == 0 && bot.hook_phase == 2);
}

static void TestDoorLeaseRetirement(void)
{
	CHECK(SG_DoorLeaseRetirement(1, 0, 0) == SG_DOOR_LEASE_RELEASE);
	CHECK(SG_DoorLeaseRetirement(0, 0, 1) == SG_DOOR_LEASE_HOLD);
	CHECK(SG_DoorLeaseRetirement(0, 1, 1) == SG_DOOR_LEASE_TERMINAL);
	CHECK(SG_DoorLeaseRetirement(0, 0, 0) == SG_DOOR_LEASE_TERMINAL);
}

static void TestFlagTouchRetiresReversibleCommitment(void)
{
	sg_bot_t bot;
	sg_think_t tc;

	ResetWorld();
	memset(&tc, 0, sizeof(tc));
	bot = Bot();
	links[1].action = RL_RUN;
	bot.commit_link = bot.sticky_link = bot.rail_link = 1;
	bot.commit_until = bot.latch_until = bot.rail_until = 30.0f;
	bot.rail_stage = 1;
	bot.commit_route_goal = (sg_field_key_t){ route_field, 0 };
	CHECK(!SG_StrikeTestFlagTouchTerminalRetainsCommit(&bot, &tc, true));
	CHECK(bot.commit_link == -1 && bot.sticky_link == -1 &&
	    bot.rail_link == -1 && bot.commit_route_goal.field == NULL);

	bot = Bot();
	bot.commit_link = bot.sticky_link = 1;
	CHECK(!SG_StrikeTestFlagTouchTerminalRetainsCommit(&bot, &tc, false));
	CHECK(bot.commit_link == 1 && bot.sticky_link == 1);

	bot.hook_link = 1;
	bot.hook_phase = 1;
	links[1].action = RL_HOOK;
	CHECK(!SG_StrikeTestFlagTouchTerminalRetainsCommit(&bot, &tc, true));
	CHECK(bot.commit_link == -1 && bot.hook_phase == 0);

	bot = Bot();
	links[1].action = RL_HOOK;
	bot.commit_link = bot.sticky_link = bot.hook_link = 1;
	bot.hook_phase = 2;
	CHECK(SG_StrikeTestFlagTouchTerminalRetainsCommit(&bot, &tc, true));
	CHECK(bot.commit_link == 1 && bot.hook_phase == 2);
}

static void TestSpeedHookTerminalFinish(void)
{
	sg_bot_t bot;

	ResetWorld();
	bot = ArmedSpeedHook();
	CHECK(SG_SpeedHookTerminalFinish(&bot, false, 0, false) ==
	    SG_SPEEDHOOK_TERMINAL_NOATTACH);
	CHECK(bot.hook_phase == 0 && bot.hook_link == -1 &&
	    bot.hook_deadline == 0.0f && !bot.speedhook && !bot.flow_release &&
	    !bot.hook_bite_logged && !bot.hook_attached_validated);
	CHECK(bot.commit_link == 1 && bot.commit_until == 30.0f &&
	    bot.commit_route_goal.field == route_field);
	CHECK(bot.sticky_link == 1 && bot.latch_until == 30.0f &&
	    bot.rail_link == 1 && bot.rail_stage == 1 && bot.rail_until == 30.0f);

	bot = ArmedSpeedHook();
	bot.speedhook_pull_applied = true;
	CHECK(SG_SpeedHookTerminalFinish(&bot, false, 0, false) ==
	    SG_SPEEDHOOK_TERMINAL_BURSTSTALL);
	CHECK(bot.commit_link == -1 && bot.commit_until == 0.0f &&
	    bot.commit_route_goal.field == NULL);
	CHECK(bot.sticky_link == -1 && bot.latch_until == 0.0f &&
	    bot.rail_link == -1 && bot.rail_stage == 0 && bot.rail_until == 0.0f);
	CHECK(!bot.speedhook_pull_applied);

	bot = ArmedSpeedHook();
	CHECK(SG_SpeedHookTerminalFinish(&bot, true, 0, false) ==
	    SG_SPEEDHOOK_TERMINAL_BURST);
	CHECK(bot.commit_link == -1 && bot.hook_phase == 0 && !bot.speedhook);

	bot = ArmedSpeedHook();
	CHECK(SG_SpeedHookTerminalFinish(&bot, false, 1, true) ==
	    SG_SPEEDHOOK_TERMINAL_BURSTSTALL);
	CHECK(bot.commit_link == -1 && bot.hook_phase == 0 && !bot.speedhook);

	bot = ArmedSpeedHook();
	bot.commit_link = 0;
	links[0].action = RL_HOOK;
	CHECK(SG_SpeedHookTerminalFinish(&bot, false, 0, false) ==
	    SG_SPEEDHOOK_TERMINAL_NOATTACH);
	CHECK(bot.commit_link == -1);

	bot = ArmedSpeedHook();
	CHECK(SG_SpeedHookTerminalFinish(&bot, false, 0, true) ==
	    SG_SPEEDHOOK_TERMINAL_BURSTSTALL);
	CHECK(bot.commit_link == -1);

	bot = ArmedSpeedHook();
	CHECK(SG_SpeedHookTerminalFinish(&bot, false, 1, false) ==
	    SG_SPEEDHOOK_TERMINAL_BURSTSTALL);
	CHECK(bot.commit_link == -1);
}

static void TestSpeedHookReleaseFinishRetiresRun(void)
{
	sg_bot_t bot = ArmedSpeedHook();

	bot.hook_phase = 3;
	SG_SpeedHookReleaseFinish(&bot);
	CHECK(bot.hook_phase == 0 && bot.hook_deadline == 0.0f);
	CHECK(!bot.speedhook && !bot.speedhook_pull_applied && !bot.flow_release);
	CHECK(!bot.hook_bite_logged && !bot.hook_attached_validated);
	CHECK(bot.commit_link == -1 && bot.commit_until == 0.0f &&
	    bot.commit_route_goal.field == NULL && bot.hook_link == 9);
	CHECK(bot.sticky_link == -1 && bot.latch_until == 0.0f);
	CHECK(bot.rail_link == -1 && bot.rail_stage == 0 && bot.rail_until == 0.0f);
}

static void TestDefensePatrolRetirement(void)
{
	sg_bot_t bot = SpeedHookRun();

	bot.speedhook = false;
	bot.patrol_link = 1;
	bot.patrol_seed = 9;
	CHECK(!SG_DefensePatrolRetire(&bot, true));
	CHECK(bot.patrol_link == 1 && bot.commit_link == 1);
	CHECK(SG_DefensePatrolRetire(&bot, false));
	CHECK(bot.patrol_link == -1 && bot.patrol_seed == -1);
	CHECK(bot.commit_link == -1 && bot.commit_route_goal.field == NULL);
	CHECK(bot.sticky_link == -1 && bot.latch_until == 0.0f);
	CHECK(bot.rail_link == -1 && bot.rail_stage == 0 && bot.rail_until == 0.0f);

	bot = PullingSpeedHook();
	bot.patrol_link = 1;
	bot.patrol_seed = 9;
	CHECK(SG_DefensePatrolRetire(&bot, false));
	CHECK(bot.patrol_link == -1 && bot.patrol_seed == -1);
	CHECK(bot.commit_link == 1 && bot.commit_until == 30.0f &&
	    bot.sticky_link == 1 && bot.hook_phase == 2 &&
	    bot.commit_retirement_pending);

	bot = SpeedHookRun();
	bot.hook_phase = 2;
	bot.patrol_link = 1;
	bot.patrol_seed = 9;
	CHECK(SG_DefensePatrolRetire(&bot, false));
	CHECK(bot.commit_retirement_pending && bot.commit_link == 1);
	CHECK(SG_SpeedHookTerminalFinish(&bot, false, 0, false) ==
	    SG_SPEEDHOOK_TERMINAL_NOATTACH);
	CHECK(bot.commit_link == -1 && bot.sticky_link == -1 &&
	    bot.rail_link == -1 && !bot.commit_retirement_pending);

	bot = SpeedHookRun();
	bot.speedhook = false;
	bot.patrol_link = 0;
	bot.patrol_seed = 9;
	bot.sticky_link = 0;
	bot.rail_link = 0;
	CHECK(SG_DefensePatrolRetire(&bot, false));
	CHECK(bot.patrol_link == -1 && bot.patrol_seed == -1);
	CHECK(bot.commit_link == 1 && bot.commit_until == 30.0f &&
	    bot.commit_route_goal.field == route_field);
	CHECK(bot.sticky_link == -1 && bot.latch_until == 0.0f);
	CHECK(bot.rail_link == -1 && bot.rail_stage == 0 && bot.rail_until == 0.0f);

	bot = SpeedHookRun();
	bot.speedhook = false;
	bot.patrol_link = 0;
	bot.patrol_seed = 9;
	CHECK(SG_DefensePatrolRetire(&bot, false));
	CHECK(bot.commit_link == 1 && bot.commit_until == 30.0f &&
	    bot.commit_route_goal.field == route_field);
	CHECK(bot.sticky_link == 1 && bot.latch_until == 30.0f);
	CHECK(bot.rail_link == 1 && bot.rail_stage == 1 && bot.rail_until == 30.0f);
}

int SG_TraversalTransitionTests(void)
{
	TestCarryStartRetiresOnlyReversibleTraversal();
	TestStrikeDutyRetiresSupersededRoute();
	TestDoorLeaseRetirement();
	TestFlagTouchRetiresReversibleCommitment();
	TestSpeedHookTerminalFinish();
	TestSpeedHookReleaseFinishRetiresRun();
	TestDefensePatrolRetirement();
	return failures;
}
