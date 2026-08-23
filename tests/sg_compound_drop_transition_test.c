/* Route-retirement boundaries cannot cancel a physically owned D_DROP. */
#include <stdio.h>
#include <string.h>

#include "g_local.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_bot.h"
#include "slipgate/sg_strike.h"
#include "slipgate/sg_traversal_transition.h"

static int failures;
static rune_t rune;
static rune_link_t links[2];
static int route_field[1];

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, \
		    #expression); \
		failures++; \
	} \
} while (0)

rune_t *SG_Rune(void)
{
	return &rune;
}

int SG_StrikeWeaponControllerPhysical(
	const sg_strike_weapon_controller_state_t *state)
{
	(void)state;
	return false;
}

void SG_DropLiveReset(sg_drop_replay_state_t *replay, qboolean *active,
	int *link, sg_drop_live_events_t *events)
{
	(void)replay;
	*active = false;
	*link = -1;
	memset(events, 0, sizeof(*events));
}

void SG_SwimLiveReset(sg_swim_replay_state_t *replay, qboolean *active,
	int *link, qboolean *validated, int *proved_ms, int *elapsed_ms)
{
	(void)replay;
	*active = false;
	*link = -1;
	*validated = false;
	*proved_ms = 0;
	*elapsed_ms = 0;
}

void SG_ButtonExecutionActionReset(sg_bot_t *bot)
{
	(void)bot;
}

int SG_TrainGateGameOwns(const sg_bot_t *bot)
{
	(void)bot;
	return 0;
}

void SG_TrainGateGameReset(sg_bot_t *bot)
{
	(void)bot;
}

static sg_bot_t CompoundDrop(qboolean owned)
{
	sg_bot_t bot;

	memset(&bot, 0, sizeof(bot));
	bot.commit_link = bot.sticky_link = bot.rail_link = 1;
	bot.commit_until = bot.latch_until = bot.rail_until = 30.0f;
	bot.commit_route_goal = (sg_field_key_t){ route_field, 0 };
	bot.rail_stage = 1;
	bot.patrol_link = 1;
	bot.patrol_seed = 9;
	bot.compound_drop_live.guard_owned = owned;
	return bot;
}

static void ResetRune(void)
{
	memset(&rune, 0, sizeof(rune));
	memset(links, 0, sizeof(links));
	rune.hdr.num_links = 2;
	rune.links = links;
	links[1].action = RL_DOOR_DROP;
}

static void TestOwnedCompoundSurvivesEveryRetirement(void)
{
	sg_bot_t bot;

	ResetRune();
	bot = CompoundDrop(true);
	SG_CarryStartRetireSupersededRoute(&bot, true);
	CHECK(bot.commit_link == 1 && bot.compound_drop_live.guard_owned);

	bot = CompoundDrop(true);
	CHECK(SG_NonCarryHandoffRetireSupersededRoute(
	    &bot, SG_ROLE_ATTACK, SG_ROLE_ESCORT));
	CHECK(bot.commit_link == 1 && bot.compound_drop_live.guard_owned);

	bot = CompoundDrop(true);
	SG_StrikeDutyRetireSupersededRoute(&bot, true);
	CHECK(bot.commit_link == 1 && bot.compound_drop_live.guard_owned);

	bot = CompoundDrop(true);
	CHECK(SG_DefensePatrolRetire(&bot, false));
	CHECK(bot.commit_link == 1 && bot.compound_drop_live.guard_owned &&
	      bot.commit_retirement_pending);
}

static void TestUnownedCompoundRemainsCancelable(void)
{
	sg_bot_t bot;

	ResetRune();
	bot = CompoundDrop(false);
	CHECK(SG_NonCarryHandoffRetireSupersededRoute(
	    &bot, SG_ROLE_ATTACK, SG_ROLE_ESCORT));
	CHECK(bot.commit_link == -1);

	bot = CompoundDrop(false);
	CHECK(SG_DefensePatrolRetire(&bot, false));
	CHECK(bot.commit_link == -1 && !bot.commit_retirement_pending);
}

int main(void)
{
	TestOwnedCompoundSurvivesEveryRetirement();
	TestUnownedCompoundRemainsCancelable();
	if (failures)
		return 1;
	puts("sg_compound_drop_transition_test: ok");
	return 0;
}
