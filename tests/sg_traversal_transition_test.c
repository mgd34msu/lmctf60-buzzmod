#include "g_local.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_bot.h"
#include "slipgate/sg_traversal_transition.h"

#include <stdio.h>
#include <string.h>

static int failures;
static rune_link_t links[2];
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
	bot->commit_route_field = (const int *)links;
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
		    bot.commit_route_field == NULL);
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
	bot.commit_route_field = (const int *)links;
	SG_StrikeDutyRetireSupersededRoute(&bot, true);
	CHECK(bot.tac_seed == -1 && bot.tac_time == 0.0f);
	CHECK(bot.commit_link == -1 && bot.commit_until == 0.0f &&
	    bot.commit_route_field == NULL);
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

int SG_TraversalTransitionTests(void)
{
	TestCarryStartRetiresOnlyReversibleTraversal();
	TestStrikeDutyRetiresSupersededRoute();
	return failures;
}
