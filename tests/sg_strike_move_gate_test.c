#include "g_local.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_bot.h"
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_move.h"
#include "slipgate/sg_role_policy.h"

#include <stdio.h>
#include <string.h>

int SG_StrikeTestTerminalFieldSeed(const rune_t *rune, const int *field,
	int current_seed);

sg_host_t sg_host;
static qboolean block_terminal_trace;

static trace_t TerminalTrace(const vec3_t start, const vec3_t mins,
	const vec3_t maxs, const vec3_t end, edict_t *passent, int contentmask)
{
	trace_t trace;

	(void)start;
	(void)mins;
	(void)maxs;
	(void)passent;
	(void)contentmask;
	memset(&trace, 0, sizeof(trace));
	trace.fraction = block_terminal_trace ? 0.5f : 1.0f;
	VectorCopy(end, trace.endpos);
	return trace;
}

static int failures;

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, \
		    #condition); \
		failures++; \
	} \
} while (0)

int main(void)
{
	sg_bot_t bot;
	sg_think_t tc;
	edict_t ent;
	gclient_t client;
	usercmd_t cmd;
	rune_t rune;
	rune_seed_t seeds[3];
	byte linked[3] = { 1, 1, 1 };
	int field[3] = { 500, 0, 0 };

	memset(&bot, 0, sizeof(bot));
	memset(&tc, 0, sizeof(tc));
	CHECK(SG_FlagTouchOptionalPacingAllowed(false));
	CHECK(!SG_FlagTouchOptionalPacingAllowed(true));
	CHECK(SG_SpawnBeatDeadline(2.0f, false) == 2.0f);
	CHECK(SG_SpawnBeatDeadline(2.0f, true) == 0.0f);
	CHECK(SG_StrikeTestAttackFlagTerminalGenericSteeringAllowed(false));
	CHECK(!SG_StrikeTestAttackFlagTerminalGenericSteeringAllowed(true));
	CHECK(!SG_StrikeTestEnemyFlagTouchMissionActive(false, false));
	CHECK(SG_StrikeTestEnemyFlagTouchMissionActive(true, false));
	CHECK(SG_StrikeTestEnemyFlagTouchMissionActive(false, true));
	CHECK(SG_StrikeTestEnemyFlagTouchMissionActive(true, true));
	CHECK(SG_StrikeTestRailMoveAllowed(&tc));
	tc.strike_active = true;
	CHECK(!SG_StrikeTestRailMoveAllowed(&tc));

	memset(&ent, 0, sizeof(ent));
	memset(&client, 0, sizeof(client));
	memset(&cmd, 0, sizeof(cmd));
	ent.client = &client;
	bot.rj_phase = 1;
	CHECK(!SG_StrikeTestRocketJumpPhase2Command(&bot, &ent, &cmd));
	CHECK((cmd.buttons & BUTTON_ATTACK) == 0 && cmd.upmove == 0);
	bot.rj_phase = 2;
	bot.rj_aim[0] = 1.0f;
	bot.rj_aim[1] = 0.0f;
	bot.rj_aim[2] = -1.0f;
	CHECK(SG_StrikeTestRocketJumpPhase2Command(&bot, &ent, &cmd));
	CHECK((cmd.buttons & BUTTON_ATTACK) != 0);
	CHECK(cmd.upmove == 400);
	CHECK(cmd.forwardmove == 0 && cmd.sidemove == 0);

	memset(&rune, 0, sizeof(rune));
	memset(seeds, 0, sizeof(seeds));
	rune.hdr.num_seeds = 3;
	rune.seeds = seeds;
	rune.linked_seed = linked;
	sg_host.trace = TerminalTrace;
	seeds[0].origin[0] = 0.0f;
	seeds[1].origin[0] = 100.0f;
	seeds[2].origin[0] = 200.0f;
	CHECK(SG_StrikeTestTerminalFieldSeed(&rune, field, 0) == 1);
	field[1] = 20;
	CHECK(SG_StrikeTestTerminalFieldSeed(&rune, field, 0) == -1);
	seeds[2].origin[0] = 120.0f;
	CHECK(SG_StrikeTestTerminalFieldSeed(&rune, field, 0) == 2);
	block_terminal_trace = true;
	CHECK(SG_StrikeTestTerminalFieldSeed(&rune, field, 0) == -1);
	block_terminal_trace = false;
	linked[2] = 0;
	CHECK(SG_StrikeTestTerminalFieldSeed(&rune, field, 0) == -1);
	linked[2] = 1;
	seeds[2].flags = RSF_TOMBSTONE;
	CHECK(SG_StrikeTestTerminalFieldSeed(&rune, field, 0) == -1);
	CHECK(SG_StrikeTestTerminalFieldSeed(&rune, field, -1) == -1);

	if (failures)
		return 1;
	puts("sg_strike_move_gate_test: ok");
	return 0;
}
