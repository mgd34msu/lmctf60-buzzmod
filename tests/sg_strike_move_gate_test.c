#include "g_local.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_bot.h"
#include "slipgate/sg_crowd_pass.h"
#include "slipgate/sg_defense_supply.h"
#include "slipgate/sg_feeler_probe.h"
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_move.h"
#include "slipgate/sg_role_policy.h"

#include <stdio.h>
#include <string.h>

int SG_StrikeTestTerminalFieldSeed(const rune_t *rune, const int *field,
	int current_seed);

sg_host_t sg_host;
static qboolean block_terminal_trace;
static qboolean terminal_trace_startsolid;
static qboolean terminal_trace_allsolid;
static int crowd_trace_mode;
static int crowd_trace_calls;
static edict_t *crowd_trace_hits[2];
static float crowd_trace_fractions[2];
static vec3_t crowd_trace_ends[2];
static edict_t *own_flag;
static qboolean own_flag_available;
static qboolean own_flag_home;

edict_t *SG_OwnFlag(int team)
{
	(void)team;
	return own_flag;
}

qboolean SG_FlagApproachAvailableTo(edict_t *flag, edict_t *player)
{
	return own_flag_available && flag == own_flag && player != NULL;
}

qboolean ctf_flagathome(edict_t *flag)
{
	return flag == own_flag && own_flag_home;
}

float SG_DistXY(const vec3_t a, const vec3_t b)
{
	float dx = a[0] - b[0];
	float dy = a[1] - b[1];

	return sqrtf(dx * dx + dy * dy);
}

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
	if (crowd_trace_mode)
	{
		int call = crowd_trace_calls++;

		if (call < 2)
		{
			VectorCopy(end, crowd_trace_ends[call]);
			trace.ent = crowd_trace_hits[call];
			trace.fraction = crowd_trace_fractions[call];
			VectorCopy(end, trace.endpos);
			return trace;
		}
	}
	trace.fraction = block_terminal_trace ? 0.5f : 1.0f;
	trace.startsolid = terminal_trace_startsolid;
	trace.allsolid = terminal_trace_allsolid;
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
	edict_t mate, door, flag;
	edict_t *flag_result;
	gclient_t mate_client;
	sg_feeler_probe_t feeler;
	int pass_side;

	memset(&bot, 0, sizeof(bot));
	memset(&tc, 0, sizeof(tc));
	memset(&ent, 0, sizeof(ent));
	memset(&cmd, 0, sizeof(cmd));
	CHECK(SG_DirectTouchOptionalPacingAllowed(false));
	CHECK(!SG_DirectTouchOptionalPacingAllowed(true));
	CHECK(SG_SpawnBeatDeadline(2.0f, false) == 2.0f);
	CHECK(SG_SpawnBeatDeadline(2.0f, true) == 0.0f);
	bot.escape_until = 8.0f;
	bot.stuck_time = 2.0f;
	bot.hook_landbrake = 3.0f;
	tc.hold_post = tc.rally_hold = tc.rail_hold = true;
	VectorSet(bot.stuck_origin, 1.0f, 2.0f, 3.0f);
	VectorSet(ent.s.origin, 4.0f, 5.0f, 6.0f);
	SG_StrikeTestDirectTouchClaimMovement(&bot, &ent, &tc, false);
	CHECK(bot.escape_until == 8.0f && bot.stuck_time == 2.0f &&
	    bot.hook_landbrake == 3.0f && tc.hold_post && tc.rally_hold &&
	    tc.rail_hold);
	CHECK(bot.stuck_origin[0] == 1.0f && bot.stuck_origin[1] == 2.0f &&
	    bot.stuck_origin[2] == 3.0f);
	SG_StrikeTestDirectTouchClaimMovement(&bot, &ent, &tc, true);
	CHECK(bot.escape_until == 0.0f && bot.stuck_time == 0.0f &&
	    bot.hook_landbrake == 0.0f && !tc.hold_post && !tc.rally_hold &&
	    !tc.rail_hold);
	CHECK(bot.stuck_origin[0] == 4.0f && bot.stuck_origin[1] == 5.0f &&
	    bot.stuck_origin[2] == 6.0f);
	cmd.forwardmove = 400;
	cmd.sidemove = 0;
	CHECK(!SG_StrikeTestDirectTouchDuelWeave(true, &cmd));
	CHECK(cmd.forwardmove == 400 && cmd.sidemove == 0);
	CHECK(SG_StrikeTestDirectTouchDuelWeave(false, &cmd));
	CHECK(cmd.forwardmove == 0 && cmd.sidemove != 0);
	CHECK(!SG_StrikeTestEnemyFlagTouchMissionActive(false, false));
	CHECK(SG_StrikeTestEnemyFlagTouchMissionActive(true, false));
	CHECK(SG_StrikeTestEnemyFlagTouchMissionActive(false, true));
	CHECK(SG_StrikeTestEnemyFlagTouchMissionActive(true, true));
	CHECK(SG_TestGenericRailMoveAllowed(&bot, &tc));
	tc.strike_active = true;
	CHECK(!SG_TestGenericRailMoveAllowed(&bot, &tc));
	tc.strike_active = false;
	bot.def_supply_armed = true;
	bot.def_supply_phase = SG_DEFENSE_SUPPLY_PHASE_OUTBOUND;
	CHECK(!SG_TestGenericRailMoveAllowed(&bot, &tc));
	bot.def_supply_phase = SG_DEFENSE_SUPPLY_PHASE_RETURN;
	CHECK(!SG_TestGenericRailMoveAllowed(&bot, &tc));
	bot.def_supply_armed = false;
	bot.def_supply_phase = SG_DEFENSE_SUPPLY_PHASE_NONE;
	CHECK(SG_TestGenericRailMoveAllowed(&bot, &tc));

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

	memset(&flag, 0, sizeof(flag));
	flag.inuse = true;
	own_flag = &flag;
	own_flag_available = true;
	own_flag_home = true;
	ent.inuse = true;
	ent.health = 100;
	VectorClear(ent.s.origin);
	VectorClear(flag.s.origin);
	flag.s.origin[0] = 159.0f;
	flag_result = NULL;
	CHECK(SG_OwnHomeFlagDirectTouchAuthority(&ent, CTF_TEAM_RED,
	    &flag_result));
	CHECK(flag_result == &flag);
	flag.s.origin[0] = 160.0f;
	flag_result = &flag;
	CHECK(!SG_OwnHomeFlagDirectTouchAuthority(&ent, CTF_TEAM_RED,
	    &flag_result));
	CHECK(flag_result == NULL);
	flag.s.origin[0] = 100.0f;
	flag.s.origin[2] = 65.0f;
	CHECK(!SG_OwnHomeFlagDirectTouchAuthority(&ent, CTF_TEAM_RED, NULL));
	flag.s.origin[2] = 0.0f;
	block_terminal_trace = true;
	CHECK(!SG_OwnHomeFlagDirectTouchAuthority(&ent, CTF_TEAM_RED, NULL));
	block_terminal_trace = false;
	terminal_trace_startsolid = true;
	CHECK(!SG_OwnHomeFlagDirectTouchAuthority(&ent, CTF_TEAM_RED, NULL));
	terminal_trace_startsolid = false;
	terminal_trace_allsolid = true;
	CHECK(!SG_OwnHomeFlagDirectTouchAuthority(&ent, CTF_TEAM_RED, NULL));
	terminal_trace_allsolid = false;
	own_flag_home = false;
	CHECK(!SG_OwnHomeFlagDirectTouchAuthority(&ent, CTF_TEAM_RED, NULL));

	memset(&mate, 0, sizeof(mate));
	memset(&door, 0, sizeof(door));
	memset(&mate_client, 0, sizeof(mate_client));
	mate.client = &mate_client;
	mate_client.ctf.teamnum = CTF_TEAM_RED;
	mate_client.ctf.ctfid = 2;
	client.ctf.teamnum = CTF_TEAM_RED;
	client.ctf.ctfid = 1;
	pass_side = SG_CrowdPassSide(client.ctf.ctfid,
	    mate_client.ctf.ctfid);
	crowd_trace_mode = 1;
	crowd_trace_hits[0] = &mate;
	crowd_trace_hits[1] = &door;
	crowd_trace_fractions[0] = 0.25f;
	crowd_trace_fractions[1] = 0.5f;
	crowd_trace_calls = 0;
	feeler = SG_FeelerProbe(&ent, CTF_TEAM_RED, 0.0f, 96.0f, true);
	CHECK(crowd_trace_calls == 2);
	CHECK(feeler.teammate_blocked);
	CHECK(feeler.trace.ent == &door && feeler.trace.fraction == 0.5f);
	CHECK(fabsf(feeler.yaw - 28.0f * pass_side) < 0.001f);
	CHECK(fabsf(crowd_trace_ends[1][0] -
	    96.0f * cosf(feeler.yaw * (float)M_PI / 180.0f)) < 0.001f);
	CHECK(fabsf(crowd_trace_ends[1][1] -
	    96.0f * sinf(feeler.yaw * (float)M_PI / 180.0f)) < 0.001f);

	crowd_trace_hits[1] = &mate;
	crowd_trace_calls = 0;
	feeler = SG_FeelerProbe(&ent, CTF_TEAM_RED, 0.0f, 96.0f, true);
	CHECK(crowd_trace_calls == 2);

	crowd_trace_hits[0] = &door;
	crowd_trace_calls = 0;
	feeler = SG_FeelerProbe(&ent, CTF_TEAM_RED, 11.0f, 96.0f, true);
	CHECK(crowd_trace_calls == 1);
	CHECK(!feeler.teammate_blocked && feeler.yaw == 11.0f);
	CHECK(feeler.trace.ent == &door && feeler.trace.fraction == 0.25f);

	crowd_trace_hits[0] = &mate;
	crowd_trace_calls = 0;
	feeler = SG_FeelerProbe(&ent, CTF_TEAM_RED, 13.0f, 96.0f, false);
	CHECK(crowd_trace_calls == 1);
	CHECK(!feeler.teammate_blocked && feeler.yaw == 13.0f);

	mate_client.ctf.teamnum = CTF_TEAM_BLUE;
	crowd_trace_calls = 0;
	feeler = SG_FeelerProbe(&ent, CTF_TEAM_RED, 15.0f, 96.0f, true);
	CHECK(crowd_trace_calls == 1 && !feeler.teammate_blocked);
	mate_client.ctf.teamnum = CTF_TEAM_RED;
	mate.deadflag = DEAD_DEAD;
	crowd_trace_calls = 0;
	feeler = SG_FeelerProbe(&ent, CTF_TEAM_RED, 16.0f, 96.0f, true);
	CHECK(crowd_trace_calls == 1 && !feeler.teammate_blocked);
	mate.deadflag = DEAD_NO;

	client.ctf.ctfid = 0;
	crowd_trace_calls = 0;
	feeler = SG_FeelerProbe(&ent, CTF_TEAM_RED, 17.0f, 96.0f, true);
	CHECK(crowd_trace_calls == 1);
	CHECK(feeler.teammate_blocked && feeler.yaw == 17.0f);
	crowd_trace_mode = 0;

	if (failures)
		return 1;
	puts("sg_strike_move_gate_test: ok");
	return 0;
}
