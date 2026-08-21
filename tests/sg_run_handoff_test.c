#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "g_local.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_action.h"
#include "slipgate/sg_bot.h"
#include "slipgate/sg_cvars.h"
#include "slipgate/sg_descend.h"
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_route_dither.h"

#include "slipgate/sg_traversal_transition.c"

level_locals_t level;
sg_cvars_t sg_cv;
sg_host_t sg_host;

static int failures;
static int think_calls;
static int expected_callback_seed;
static sg_bot_t *callback_bot;
static edict_t *callback_ent;
static int expected_published_seed = -1;
static int trigger_callbacks;
static int unsupported_frames;
static int death_frames;
static int invalidate_on_call;
static int invalidate_mode;
static float floor_z;
static edict_t pmove_world;
static csurface_t pmove_surface;
static cvar_t debug_cvar;
static int dprint_calls;
static char dprint_line[256];
static int completion_field[1];

void SG_DropLiveReset(sg_drop_replay_state_t *replay, qboolean *active,
	int *replay_link, sg_drop_live_events_t *events)
{
	(void)replay;
	(void)active;
	(void)replay_link;
	(void)events;
	abort();
}

void SG_SwimLiveReset(sg_swim_replay_state_t *replay, qboolean *active,
	int *replay_link, qboolean *validated, int *proved_ms, int *elapsed_ms)
{
	(void)replay;
	(void)active;
	(void)replay_link;
	(void)validated;
	(void)proved_ms;
	(void)elapsed_ms;
	abort();
}

void SG_ButtonExecutionActionReset(sg_bot_t *bot)
{
	(void)bot;
	abort();
}

void Pmove(pmove_t *pmove);

enum
{
	INVALIDATE_NONE = 0,
	INVALIDATE_DEATH,
	INVALIDATE_DISCONNECT,
	INVALIDATE_SUPPORT
};

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		        __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

void Com_DPrintf(const char *fmt, ...)
{
	(void)fmt;
}

static void TestDprint(const char *fmt, ...)
{
	va_list args;

	if (expected_published_seed >= 0)
	{
		CHECK(callback_bot != NULL);
		CHECK(callback_ent != NULL);
		CHECK(callback_bot->seed == expected_published_seed);
		CHECK(callback_bot->prev_seed == expected_callback_seed);
		CHECK(VectorCompare(callback_bot->last_origin,
		          callback_ent->s.origin));
		CHECK(!callback_bot->seedless_active);
		CHECK(callback_bot->seedless_since == 0.0f);
		CHECK(callback_bot->seedless_turn_until == 0.0f);
	}
	dprint_calls++;
	va_start(args, fmt);
	vsnprintf(dprint_line, sizeof(dprint_line), fmt, args);
	va_end(args);
}

static void ResetDebugCapture(float value)
{
	memset(&debug_cvar, 0, sizeof(debug_cvar));
	memset(dprint_line, 0, sizeof(dprint_line));
	debug_cvar.value = value;
	sg_cv.debug = &debug_cvar;
	sg_host.dprint = TestDprint;
	dprint_calls = 0;
}

static trace_t FlatFloorTrace(vec3_t start, vec3_t mins, vec3_t maxs,
	vec3_t end)
{
	trace_t trace;
	float start_bottom;
	float end_bottom;

	(void)maxs;
	memset(&trace, 0, sizeof(trace));
	trace.fraction = 1.0f;
	VectorCopy(end, trace.endpos);
	start_bottom = start[2] + mins[2];
	end_bottom = end[2] + mins[2];
	if (start_bottom < floor_z)
	{
		trace.startsolid = true;
		trace.allsolid = end_bottom < floor_z;
		trace.fraction = 0.0f;
		VectorCopy(start, trace.endpos);
		trace.ent = &pmove_world;
		trace.contents = CONTENTS_SOLID;
		trace.surface = &pmove_surface;
		trace.plane.normal[2] = 1.0f;
		trace.plane.dist = floor_z;
		return trace;
	}
	if (end_bottom < floor_z)
	{
		float distance = start_bottom - end_bottom;
		float fraction = (start_bottom - floor_z - 0.03125f) / distance;

		if (fraction < 0.0f)
			fraction = 0.0f;
		if (fraction > 1.0f)
			fraction = 1.0f;
		trace.fraction = fraction;
		trace.endpos[0] = start[0] + fraction * (end[0] - start[0]);
		trace.endpos[1] = start[1] + fraction * (end[1] - start[1]);
		trace.endpos[2] = start[2] + fraction * (end[2] - start[2]);
		trace.ent = &pmove_world;
		trace.contents = CONTENTS_SOLID;
		trace.surface = &pmove_surface;
		trace.plane.normal[2] = 1.0f;
		trace.plane.dist = floor_z;
	}
	return trace;
}

static int DryPointContents(vec3_t point)
{
	(void)point;
	return 0;
}

static qboolean TouchesRedTeleportTrigger(const edict_t *ent)
{
	return ent && ent->s.origin[0] >= 231.0f &&
	       ent->s.origin[0] <= 281.0f && ent->s.origin[1] >= -1593.0f &&
	       ent->s.origin[1] <= -1543.0f && ent->s.origin[2] >= -1160.0f &&
	       ent->s.origin[2] <= -1080.0f;
}

/* This is the exact game boundary used by SG_RunCompletionHandoff. It invokes
 * the vendored production-engine Pmove for every 25 ms command, then performs
 * the game-side support/alive/trigger observations independently. */
void ClientThink(edict_t *ent, usercmd_t *cmd)
{
	usercmd_t expected;
	pmove_t pmove;
	int axis;

	memset(&expected, 0, sizeof(expected));
	expected.msec = 25;
	CHECK(ent != NULL);
	CHECK(cmd != NULL);
	CHECK(memcmp(cmd, &expected, sizeof(expected)) == 0);
	CHECK(callback_bot != NULL);
	CHECK(callback_bot->commit_link == -1);
	CHECK(callback_bot->seed == expected_callback_seed);
	CHECK(!callback_bot->declared_started);
	CHECK(!callback_bot->declared_touched);
	CHECK(!callback_bot->declared_triggered);
	think_calls++;

	memset(&pmove, 0, sizeof(pmove));
	pmove.s = ent->client->ps.pmove;
	for (axis = 0; axis < 3; axis++)
	{
		pmove.s.origin[axis] = (short)(ent->s.origin[axis] * 8.0f);
		pmove.s.velocity[axis] = (short)(ent->velocity[axis] * 8.0f);
	}
	pmove.s.pm_type = PM_NORMAL;
	pmove.s.gravity = 800;
	pmove.snapinitial = memcmp(&ent->client->old_pmove, &pmove.s,
	    sizeof(pmove.s)) != 0;
	pmove.cmd = *cmd;
	pmove.trace = FlatFloorTrace;
	pmove.pointcontents = DryPointContents;
	Pmove(&pmove);
	ent->client->ps.pmove = pmove.s;
	ent->client->old_pmove = pmove.s;
	for (axis = 0; axis < 3; axis++)
	{
		ent->s.origin[axis] = pmove.s.origin[axis] * 0.125f;
		ent->velocity[axis] = pmove.s.velocity[axis] * 0.125f;
	}
	VectorCopy(pmove.mins, ent->mins);
	VectorCopy(pmove.maxs, ent->maxs);
	ent->groundentity = pmove.groundentity;
	ent->watertype = pmove.watertype;
	ent->waterlevel = pmove.waterlevel;
	if (invalidate_on_call == think_calls)
	{
		if (invalidate_mode == INVALIDATE_DEATH)
		{
			ent->health = 0;
			ent->deadflag = DEAD_DEAD;
		}
		else if (invalidate_mode == INVALIDATE_DISCONNECT)
			ent->client->pers.connected = false;
		else if (invalidate_mode == INVALIDATE_SUPPORT)
			ent->groundentity = NULL;
	}
	if (!ent->groundentity)
		unsupported_frames++;
	if (!ent->inuse || ent->health <= 0 || ent->deadflag != DEAD_NO)
		death_frames++;
	if (TouchesRedTeleportTrigger(ent))
		trigger_callbacks++;
}

enum
{
	SEED_DEPARTURE = 0,
	SEED_TELE_SOURCE,
	SEED_TELE_DESTINATION,
	SEED_ORDINARY,
	TEST_SEEDS
};

enum
{
	LINK_RUN_TO_SOURCE = 0,
	LINK_TELEPORT,
	LINK_RUN_FROM_SOURCE,
	TEST_LINKS
};

static void ArmRunTransaction(sg_bot_t *bot, int seed)
{
	bot->seed = seed;
	bot->commit_link = bot->sticky_link = bot->rail_link = LINK_RUN_TO_SOURCE;
	bot->commit_until = bot->latch_until = bot->rail_until = 30.0f;
	bot->rail_stage = 2;
	bot->commit_route_goal = (sg_field_key_t){ completion_field, 0 };
	bot->commit_retirement_pending = true;
}

typedef struct run_fixture_s
{
	rune_t rune;
	rune_seed_t seeds[TEST_SEEDS];
	rune_link_t links[TEST_LINKS];
	rune_mechanism_plan_t plans[1];
	int first_link[TEST_SEEDS];
	int next_link[TEST_LINKS];
} run_fixture_t;

static void SetLink(rune_link_t *link, int from, int to, int action)
{
	memset(link, 0, sizeof(*link));
	link->from = from;
	link->to = to;
	link->action = (byte)action;
	link->mechanism_plan = RUNE_NO_MECHANISM_PLAN;
}

static void FixtureInit(run_fixture_t *fixture)
{
	int seed;

	memset(fixture, 0, sizeof(*fixture));
	fixture->rune.hdr.num_seeds = TEST_SEEDS;
	fixture->rune.hdr.num_links = TEST_LINKS;
	fixture->rune.artifact.num_seeds = TEST_SEEDS;
	fixture->rune.artifact.num_links = TEST_LINKS;
	fixture->rune.artifact.num_mechanism_plans = 1U;
	fixture->rune.seeds = fixture->seeds;
	fixture->rune.links = fixture->links;
	fixture->rune.mechanism_plans = fixture->plans;
	fixture->rune.first_link = fixture->first_link;
	fixture->rune.next_link = fixture->next_link;
	for (seed = 0; seed < TEST_SEEDS; seed++)
		fixture->first_link[seed] = -1;
	for (seed = 0; seed < TEST_LINKS; seed++)
		fixture->next_link[seed] = -1;

	/* lmctf02c red direction, reduced to the authenticated graph identities:
	 * RUN15839 reaches source10, whose declared successor is TELE38296. */
	VectorSet(fixture->seeds[SEED_DEPARTURE].origin,
	    392.0f, -1375.875f, -1127.875f);
	VectorSet(fixture->seeds[SEED_TELE_SOURCE].origin,
	    256.0f, -1496.0f, -1127.875f);
	VectorSet(fixture->seeds[SEED_TELE_DESTINATION].origin,
	    256.0f, -3760.0f, -135.875f);
	VectorSet(fixture->seeds[SEED_ORDINARY].origin,
	    512.0f, -1496.0f, -1127.875f);

	SetLink(&fixture->links[LINK_RUN_TO_SOURCE], SEED_DEPARTURE,
	    SEED_TELE_SOURCE, RL_RUN);
	SetLink(&fixture->links[LINK_TELEPORT], SEED_TELE_SOURCE,
	    SEED_TELE_DESTINATION, RL_TELEPORT);
	fixture->links[LINK_TELEPORT].mechanism_plan = 0U;
	SetLink(&fixture->links[LINK_RUN_FROM_SOURCE], SEED_TELE_SOURCE,
	    SEED_ORDINARY, RL_RUN);
	fixture->plans[0].controller_kind = SG_MECHANISM_CONTROLLER_TELEPORT;

	fixture->first_link[SEED_DEPARTURE] = LINK_RUN_TO_SOURCE;
	fixture->first_link[SEED_TELE_SOURCE] = LINK_TELEPORT;
	fixture->next_link[LINK_TELEPORT] = LINK_RUN_FROM_SOURCE;
}

static void DisableMechanism(run_fixture_t *fixture)
{
	fixture->links[LINK_TELEPORT].action = RL_RUN;
	fixture->links[LINK_TELEPORT].mechanism_plan = RUNE_NO_MECHANISM_PLAN;
}

static void PreparePmoveBody(edict_t *ent, gclient_t *client,
	const vec3_t origin, const vec3_t velocity)
{
	int axis;

	memset(ent, 0, sizeof(*ent));
	memset(client, 0, sizeof(*client));
	memset(&pmove_world, 0, sizeof(pmove_world));
	memset(&pmove_surface, 0, sizeof(pmove_surface));
	ent->client = client;
	strcpy(client->pers.netname, "Arach");
	client->pers.connected = true;
	ent->inuse = true;
	ent->health = 100;
	ent->deadflag = DEAD_NO;
	ent->solid = SOLID_BBOX;
	ent->s.modelindex = 255;
	ent->movetype = MOVETYPE_WALK;
	ent->groundentity = &pmove_world;
	pmove_world.inuse = true;
	pmove_world.solid = SOLID_BSP;
	VectorCopy(origin, ent->s.origin);
	VectorCopy(velocity, ent->velocity);
	floor_z = origin[2] - 24.125f;
	client->ps.pmove.pm_type = PM_NORMAL;
	client->ps.pmove.pm_flags = PMF_ON_GROUND;
	client->ps.pmove.gravity = 800;
	for (axis = 0; axis < 3; axis++)
	{
		client->ps.pmove.origin[axis] = (short)(origin[axis] * 8.0f);
		client->ps.pmove.velocity[axis] = (short)(velocity[axis] * 8.0f);
	}
	client->old_pmove = client->ps.pmove;
}

static void CheckCompletionPredicates(void)
{
	run_fixture_t fixture;
	int field[TEST_SEEDS] = { 300, 200, 0, 100 };
	vec3_t body;
	int action;

	FixtureInit(&fixture);
	VectorSet(body, 299.5f, -1508.375f, -1127.875f);
	CHECK(SG_RunCommitCompletion(&fixture.rune,
	          &fixture.links[LINK_RUN_TO_SOURCE], SEED_DEPARTURE, body,
	          field) == SG_RUN_ARRIVED);
	VectorCopy(fixture.seeds[SEED_DEPARTURE].origin, body);
	CHECK(SG_RunCommitCompletion(&fixture.rune,
	          &fixture.links[LINK_RUN_TO_SOURCE], SEED_TELE_SOURCE, body,
	          field) == SG_RUN_ARRIVED);
	CHECK(SG_RunCommitCompletion(&fixture.rune,
	          &fixture.links[LINK_RUN_TO_SOURCE], SEED_ORDINARY, body,
	          field) == SG_RUN_OVERACHIEVED);
	field[SEED_ORDINARY] = 400;
	CHECK(SG_RunCommitCompletion(&fixture.rune,
	          &fixture.links[LINK_RUN_TO_SOURCE], SEED_ORDINARY, body,
	          field) == SG_RUN_INCOMPLETE);

	for (action = RL_JUMP; action <= RL_BUTTON_DOOR; action++)
	{
		rune_link_t non_run = fixture.links[LINK_RUN_TO_SOURCE];

		if (action == RL_RUN)
			continue;
		non_run.action = (byte)action;
		CHECK(SG_RunCommitCompletion(&fixture.rune, &non_run,
		          SEED_TELE_SOURCE, body, field) == SG_RUN_INCOMPLETE);
	}
}

static void CheckCompoundDropCommitOwnership(void)
{
	CHECK(SG_CompoundDropCommitRetained(RL_DOOR_DROP, true));
	CHECK(!SG_CompoundDropCommitRetained(RL_DOOR_DROP, false));
	CHECK(!SG_CompoundDropCommitRetained(RL_DROP, true));
	CHECK(!SG_CompoundDropCommitRetained(RL_DOOR, true));
	CHECK(!SG_CompoundDropCommitRetained(RL_RUN, true));
}

static void CheckHighSpeedTeleportHandoff(void)
{
	run_fixture_t fixture;
	sg_bot_t bot;
	sg_think_t think;
	edict_t ent;
	gclient_t client;
	int field[TEST_SEEDS] = { 300, 200, 0, 100 };
	int next_link = LINK_RUN_TO_SOURCE;
	sg_run_completion_t completion;
	unsigned expected_dither;
	int expected_random;
	usercmd_t zero;
	char expected_line[256];
	vec3_t origin = { 299.5f, -1508.375f, -1127.875f };
	vec3_t velocity = { -205.0f, -220.0f, 0.0f };

	FixtureInit(&fixture);
	memset(&bot, 0, sizeof(bot));
	memset(&think, 0, sizeof(think));
	memset(&zero, 0, sizeof(zero));
	level.time = 42.0f;
	level.framenum = 4242;
	PreparePmoveBody(&ent, &client, origin, velocity);
	bot.seed = SEED_DEPARTURE;
	bot.prev_seed = -1;
	bot.commit_link = bot.sticky_link = bot.rail_link = LINK_RUN_TO_SOURCE;
	bot.commit_until = bot.latch_until = bot.rail_until = 30.0f;
	bot.rail_stage = 2;
	bot.commit_route_goal = (sg_field_key_t){ field, SEED_TELE_SOURCE };
	bot.commit_retirement_pending = true;
	think.e = &ent;
	think.bestlink = next_link;
	think.cmd.forwardmove = 400;
	think.cmd.buttons = BUTTON_ATTACK;
	callback_bot = &bot;
	callback_ent = &ent;
	expected_callback_seed = SEED_DEPARTURE;
	expected_published_seed = SEED_TELE_SOURCE;
	think_calls = 0;
	trigger_callbacks = 0;
	unsupported_frames = 0;
	death_frames = 0;
	invalidate_on_call = 0;
	invalidate_mode = INVALIDATE_NONE;
	ResetDebugCapture(1.0f);
	expected_dither = SG_RouteDitherNext(0U, SEED_DEPARTURE,
	    SEED_TELE_SOURCE);
	CHECK(expected_dither == 0x0c572cbcu);
	srand(271828U);
	expected_random = rand();
	srand(271828U);

	completion = SG_RunCommitCompletion(&fixture.rune,
	    &fixture.links[LINK_RUN_TO_SOURCE], bot.seed, ent.s.origin, field);
	CHECK(completion == SG_RUN_ARRIVED);
	CHECK(SG_RunHasMechanismSuccessor(&fixture.rune,
	          SEED_TELE_SOURCE));
	CHECK(SG_RunCompletionHandoff(&fixture.rune, LINK_RUN_TO_SOURCE,
	          completion, &bot, &think, &next_link));
	CHECK(think_calls == 4);
	CHECK(trigger_callbacks == 0);
	CHECK(unsupported_frames == 0);
	CHECK(death_frames == 0);
	CHECK(next_link == -1);
	CHECK(think.bestlink == -1);
	CHECK(think.think_over);
	CHECK(memcmp(&think.cmd, &zero, sizeof(zero)) == 0);
	CHECK(bot.commit_link == -1 && bot.commit_until == 0.0f &&
	    bot.commit_route_goal.field == NULL);
	CHECK(bot.sticky_link == -1 && bot.latch_until == 0.0f);
	CHECK(bot.rail_link == -1 && bot.rail_stage == 0 && bot.rail_until == 0.0f);
	CHECK(!bot.commit_retirement_pending);
	CHECK(bot.seed == SEED_TELE_SOURCE);
	CHECK(bot.prev_seed == SEED_DEPARTURE);
	CHECK(bot.prev_seed_time == level.time);
	CHECK(bot.dither_salt == expected_dither);
	CHECK(rand() == expected_random);
	CHECK(VectorCompare(bot.last_origin, ent.s.origin));
	CHECK(!bot.seedless_active);
	CHECK(bot.seedless_since == 0.0f);
	CHECK(bot.seedless_turn_until == 0.0f);
	CHECK(ent.s.origin[1] > -1543.0f);
	CHECK(VectorLength(ent.velocity) < VectorLength(velocity));
	snprintf(expected_line, sizeof(expected_line),
	    "RUNHANDOFF Arach frame=4242 completed=0 from=0 to=1 "
	    "outcome=published seed=1 q8=(%d %d %d)\n",
	    (int)client.ps.pmove.origin[0], (int)client.ps.pmove.origin[1],
	    (int)client.ps.pmove.origin[2]);
	CHECK(dprint_calls == 1);
	CHECK(strcmp(dprint_line, expected_line) == 0);
	CHECK(strstr(dprint_line, "next=") == NULL);
	CHECK(strstr(dprint_line, "act=") == NULL);
	/* Next-frame pricing starts from the exact authenticated source. The
	 * fixture intentionally also has an ordinary successor, proving the
	 * boundary did not choose a mechanism arbitrarily; PickLink remains owner,
	 * while the TELE candidate passes the action/plan admission gate.
	 * Full live PickLink/binding selection remains an end-to-end proof. */
	CHECK(fixture.first_link[bot.seed] == LINK_TELEPORT);
	CHECK(fixture.links[fixture.first_link[bot.seed]].action == RL_TELEPORT);
	CHECK(SG_RunMechanismPlanCandidateValid(&fixture.rune, bot.seed,
	          LINK_TELEPORT));
	CHECK(!SG_RunMechanismPlanCandidateValid(&fixture.rune, bot.seed,
	          LINK_RUN_FROM_SOURCE));

	callback_bot = NULL;
	callback_ent = NULL;
	expected_published_seed = -1;
}

static void CheckDebugOffSuccessSilent(void)
{
	run_fixture_t fixture;
	sg_bot_t bot;
	sg_think_t think;
	edict_t ent;
	gclient_t client;
	vec3_t origin = { 299.5f, -1508.375f, -1127.875f };
	vec3_t velocity = { -205.0f, -220.0f, 0.0f };
	int next_link = LINK_RUN_TO_SOURCE;

	FixtureInit(&fixture);
	memset(&bot, 0, sizeof(bot));
	memset(&think, 0, sizeof(think));
	PreparePmoveBody(&ent, &client, origin, velocity);
	bot.seed = SEED_DEPARTURE;
	bot.commit_link = LINK_RUN_TO_SOURCE;
	think.e = &ent;
	think.bestlink = next_link;
	callback_bot = &bot;
	callback_ent = &ent;
	expected_callback_seed = SEED_DEPARTURE;
	expected_published_seed = -1;
	think_calls = 0;
	trigger_callbacks = 0;
	unsupported_frames = 0;
	death_frames = 0;
	invalidate_on_call = 0;
	invalidate_mode = INVALIDATE_NONE;
	ResetDebugCapture(0.0f);
	CHECK(SG_RunCompletionHandoff(&fixture.rune, LINK_RUN_TO_SOURCE,
	          SG_RUN_ARRIVED, &bot, &think, &next_link));
	CHECK(bot.seed == SEED_TELE_SOURCE);
	CHECK(think_calls == 4);
	CHECK(dprint_calls == 0);
	CHECK(dprint_line[0] == '\0');
	callback_bot = NULL;
	callback_ent = NULL;
}

static void CheckLateBoundaryTouchesTrigger(void)
{
	sg_bot_t bot;
	edict_t ent;
	gclient_t client;
	usercmd_t coast;
	vec3_t origin = { 278.5f, -1529.75f, -1119.875f };
	vec3_t velocity = { -205.0f, -220.0f, 0.0f };
	int step;

	memset(&bot, 0, sizeof(bot));
	memset(&coast, 0, sizeof(coast));
	PreparePmoveBody(&ent, &client, origin, velocity);
	bot.seed = SEED_DEPARTURE;
	bot.commit_link = -1;
	callback_bot = &bot;
	expected_callback_seed = SEED_DEPARTURE;
	think_calls = 0;
	trigger_callbacks = 0;
	unsupported_frames = 0;
	death_frames = 0;
	invalidate_on_call = 0;
	invalidate_mode = INVALIDATE_NONE;
	coast.msec = 25;
	for (step = 0; step < 4; step++)
		ClientThink(&ent, &coast);
	CHECK(think_calls == 4);
	CHECK(trigger_callbacks > 0);
	CHECK(unsupported_frames == 0);
	CHECK(death_frames == 0);
	callback_bot = NULL;
}

static void CheckOnePostCoastInvalidation(int mode)
{
	run_fixture_t fixture;
	sg_bot_t bot;
	sg_think_t think;
	edict_t ent;
	gclient_t client;
	vec3_t origin = { 299.5f, -1508.375f, -1127.875f };
	vec3_t velocity = { -205.0f, -220.0f, 0.0f };
	int next_link = LINK_RUN_TO_SOURCE;

	FixtureInit(&fixture);
	memset(&bot, 0, sizeof(bot));
	memset(&think, 0, sizeof(think));
	PreparePmoveBody(&ent, &client, origin, velocity);
	bot.seed = SEED_DEPARTURE;
	bot.prev_seed = -1;
	bot.dither_salt = 0x13579U;
	VectorSet(bot.last_origin, 7.0f, 8.0f, 9.0f);
	bot.commit_link = LINK_RUN_TO_SOURCE;
	think.e = &ent;
	think.bestlink = next_link;
	callback_bot = &bot;
	callback_ent = &ent;
	expected_callback_seed = SEED_DEPARTURE;
	expected_published_seed = -1;
	think_calls = 0;
	trigger_callbacks = 0;
	unsupported_frames = 0;
	death_frames = 0;
	invalidate_on_call = 4;
	invalidate_mode = mode;
	ResetDebugCapture(1.0f);
	CHECK(SG_RunCompletionHandoff(&fixture.rune, LINK_RUN_TO_SOURCE,
	          SG_RUN_ARRIVED, &bot, &think, &next_link));
	CHECK(think_calls == 4);
	if (mode == INVALIDATE_DEATH)
		CHECK(death_frames >= 1);
	else if (mode == INVALIDATE_DISCONNECT)
		CHECK(!client.pers.connected);
	else if (mode == INVALIDATE_SUPPORT)
		CHECK(ent.groundentity == NULL);
	CHECK(bot.seed == -1);
	CHECK(bot.prev_seed == -1);
	CHECK(bot.dither_salt == 0x13579U);
	CHECK(bot.last_origin[0] == 7.0f);
	CHECK(bot.last_origin[1] == 8.0f);
	CHECK(bot.last_origin[2] == 9.0f);
	CHECK(bot.commit_link == -1);
	CHECK(think.think_over);
	CHECK(dprint_calls == 0);
	CHECK(dprint_line[0] == '\0');
	callback_bot = NULL;
	callback_ent = NULL;
	invalidate_on_call = 0;
	invalidate_mode = INVALIDATE_NONE;
}

static void CheckPostCoastInvalidationFailsClosed(void)
{
	CheckOnePostCoastInvalidation(INVALIDATE_DEATH);
	CheckOnePostCoastInvalidation(INVALIDATE_DISCONNECT);
	CheckOnePostCoastInvalidation(INVALIDATE_SUPPORT);
}

static void CheckOrdinaryCompletionRetiresTransaction(void)
{
	run_fixture_t fixture;
	sg_bot_t bot;
	int next_link;

	FixtureInit(&fixture);
	DisableMechanism(&fixture);
	memset(&bot, 0, sizeof(bot));

	ArmRunTransaction(&bot, SEED_TELE_SOURCE);
	next_link = LINK_RUN_FROM_SOURCE;
	SG_RunRetireCompletedTransaction(&fixture.rune,
	    LINK_RUN_TO_SOURCE, SG_RUN_ARRIVED, &bot, &next_link);
	CHECK(next_link == LINK_RUN_FROM_SOURCE);
	CHECK(bot.commit_link == -1 && bot.commit_until == 0.0f &&
	    bot.commit_route_goal.field == NULL && !bot.commit_retirement_pending);
	CHECK(bot.sticky_link == -1 && bot.latch_until == 0.0f &&
	    bot.rail_link == -1 && bot.rail_stage == 0 && bot.rail_until == 0.0f);
	CHECK(bot.seed == SEED_TELE_SOURCE);

	ArmRunTransaction(&bot, SEED_DEPARTURE);
	next_link = LINK_RUN_TO_SOURCE;
	SG_RunRetireCompletedTransaction(&fixture.rune,
	    LINK_RUN_TO_SOURCE, SG_RUN_ARRIVED, &bot, &next_link);
	CHECK(next_link == -1 && bot.seed == SEED_DEPARTURE);

	SetLink(&fixture.links[LINK_RUN_FROM_SOURCE], SEED_ORDINARY,
	    SEED_TELE_DESTINATION, RL_RUN);
	ArmRunTransaction(&bot, SEED_ORDINARY);
	next_link = LINK_RUN_FROM_SOURCE;
	SG_RunRetireCompletedTransaction(&fixture.rune,
	    LINK_RUN_TO_SOURCE, SG_RUN_OVERACHIEVED, &bot, &next_link);
	CHECK(next_link == LINK_RUN_FROM_SOURCE && bot.commit_link == -1 &&
	    bot.seed == SEED_ORDINARY);

	fixture.links[LINK_RUN_FROM_SOURCE].from = SEED_DEPARTURE;
	ArmRunTransaction(&bot, SEED_ORDINARY);
	next_link = LINK_RUN_FROM_SOURCE;
	SG_RunRetireCompletedTransaction(&fixture.rune,
	    LINK_RUN_TO_SOURCE, SG_RUN_OVERACHIEVED, &bot, &next_link);
	CHECK(next_link == -1 && bot.commit_link == -1);

	ArmRunTransaction(&bot, SEED_ORDINARY);
	next_link = LINK_RUN_TO_SOURCE;
	SG_RunRetireCompletedTransaction(&fixture.rune,
	    LINK_RUN_TO_SOURCE, SG_RUN_OVERACHIEVED, &bot, &next_link);
	CHECK(next_link == -1 && bot.commit_link == -1 &&
	    bot.seed == SEED_ORDINARY);

	ArmRunTransaction(&bot, SEED_DEPARTURE);
	next_link = LINK_RUN_TO_SOURCE;
	SG_RunRetireCompletedTransaction(&fixture.rune,
	    LINK_RUN_TO_SOURCE, SG_RUN_INCOMPLETE, &bot, &next_link);
	CHECK(next_link == LINK_RUN_TO_SOURCE && bot.commit_link == LINK_RUN_TO_SOURCE &&
	    bot.sticky_link == LINK_RUN_TO_SOURCE && bot.rail_stage == 2);
	fixture.links[LINK_RUN_TO_SOURCE].action = RL_DROP;
	SG_RunRetireCompletedTransaction(&fixture.rune,
	    LINK_RUN_TO_SOURCE, SG_RUN_ARRIVED, &bot, &next_link);
	CHECK(next_link == LINK_RUN_TO_SOURCE && bot.commit_link == LINK_RUN_TO_SOURCE);
}

static void CheckMalformedMechanismFailClosed(void)
{
	run_fixture_t fixture;
	sg_bot_t bot;
	sg_think_t think;
	edict_t ent;
	gclient_t client;
	int next_link = LINK_RUN_TO_SOURCE;

	FixtureInit(&fixture);
	memset(&bot, 0, sizeof(bot));
	memset(&think, 0, sizeof(think));
	memset(&ent, 0, sizeof(ent));
	memset(&client, 0, sizeof(client));
	ent.client = &client;
	think.e = &ent;
	bot.seed = SEED_DEPARTURE;
	bot.commit_link = LINK_RUN_TO_SOURCE;
	ResetDebugCapture(1.0f);
	fixture.links[LINK_TELEPORT].mechanism_plan = RUNE_NO_MECHANISM_PLAN;
	CHECK(!SG_RunHasMechanismSuccessor(&fixture.rune,
	          SEED_TELE_SOURCE));
	CHECK(!SG_RunCompletionHandoff(&fixture.rune, LINK_RUN_TO_SOURCE,
	          SG_RUN_ARRIVED, &bot, &think, &next_link));
	CHECK(dprint_calls == 0);
	fixture.links[LINK_TELEPORT].mechanism_plan = 0U;
	fixture.next_link[LINK_TELEPORT] = TEST_LINKS;
	/* A valid first mechanism is enough; the helper is a predicate and never
	 * consumes or chooses among the remainder of the fan. */
	CHECK(SG_RunHasMechanismSuccessor(&fixture.rune,
	          SEED_TELE_SOURCE));
}

int main(void)
{
	CheckCompletionPredicates();
	CheckCompoundDropCommitOwnership();
	CheckHighSpeedTeleportHandoff();
	CheckDebugOffSuccessSilent();
	CheckLateBoundaryTouchesTrigger();
	CheckPostCoastInvalidationFailsClosed();
	CheckOrdinaryCompletionRetiresTransaction();
	CheckMalformedMechanismFailClosed();
	if (failures)
	{
		fprintf(stderr, "sg_run_handoff_test: %d failure(s)\n", failures);
		return EXIT_FAILURE;
	}
	puts("sg_run_handoff_test: ok");
	return EXIT_SUCCESS;
}
