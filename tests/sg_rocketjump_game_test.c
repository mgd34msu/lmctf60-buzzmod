#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "g_local.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_bot.h"
#include "slipgate/sg_cvars.h"
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_rocketjump_game.h"

#define TEST_EDICTS 8
#define BOT_EDICT 1
#define ROCKET_EDICT 2
#define FOREIGN_EDICT 3
#define TEST_LINK 0

game_import_t gi;
game_export_t globals;
game_locals_t game;
level_locals_t level;
edict_t *g_edicts;
gitem_t itemlist[3];
sg_bot_t sg_bots[SG_MAXBOTS];
sg_cvars_t sg_cv;
sg_host_t sg_host;

static edict_t edicts[TEST_EDICTS];
static gclient_t clients[2];
static rune_t rune_fixture;
static rune_seed_t seeds[2];
static rune_link_t links[1];
static uint32_t generations[TEST_EDICTS];
static cvar_t debug_cvar;
static edict_t *trace_hit;
static int callback_enabled;
static int callback_sent;
static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		    __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static void NoPrint(const char *format, ...)
{
	(void)format;
}

static trace_t TestTrace(const vec3_t start, const vec3_t mins,
	const vec3_t maxs, const vec3_t end, edict_t *passent, int mask)
{
	trace_t trace;

	(void)mins;
	(void)maxs;
	(void)end;
	(void)passent;
	(void)mask;
	memset(&trace, 0, sizeof(trace));
	trace.fraction = 0.25f;
	VectorCopy(start, trace.endpos);
	trace.endpos[2] -= 24.0f;
	trace.ent = trace_hit;
	return trace;
}

static void LinkEntity(edict_t *entity)
{
	(void)entity;
}

static int ModelIndex(char *name)
{
	(void)name;
	return 1;
}

static void UseLauncher(edict_t *entity, gitem_t *item)
{
	entity->client->pers.weapon = item;
	entity->client->newweapon = NULL;
	entity->client->weaponstate = WEAPON_READY;
}

gitem_t *FindItem(char *pickup_name)
{
	if (!strcmp(pickup_name, "Rocket Launcher"))
		return &itemlist[1];
	if (!strcmp(pickup_name, "Rockets"))
		return &itemlist[2];
	return NULL;
}

rune_t *SG_Rune(void)
{
	return &rune_fixture;
}

int SG_CompoundSwimGameOwns(const sg_bot_t *bot)
{
	return bot && bot->compound_swim.guard_owned;
}

qboolean SG_ImmutableSupport(edict_t *entity)
{
	return entity == &edicts[0];
}

qboolean SG_RocketJumpArrived(const vec3_t origin,
	const vec3_t destination, qboolean grounded, int waterlevel,
	edict_t *support, edict_t *passent)
{
	(void)passent;
	return grounded && waterlevel == 0 && support == &edicts[0] &&
	    origin[0] == destination[0] && origin[1] == destination[1] &&
	    origin[2] == destination[2];
}

int SG_MechCatalogEntityGeneration(const edict_t *entity,
	uint32_t *key_out, uint32_t *generation_out)
{
	ptrdiff_t key;

	if (!entity || !key_out || !generation_out)
		return 0;
	key = entity - edicts;
	if (key <= 0 || key >= TEST_EDICTS || !entity->inuse ||
	    entity->s.number != key || generations[key] == 0U)
		return 0;
	*key_out = (uint32_t)key;
	*generation_out = generations[key];
	return 1;
}

void ClientThink(edict_t *entity, usercmd_t *command)
{
	edict_t *projectile;

	if (!callback_enabled || callback_sent ||
	    !(command->buttons & BUTTON_ATTACK))
		return;
	projectile = &edicts[ROCKET_EDICT];
	projectile->inuse = true;
	projectile->s.number = ROCKET_EDICT;
	projectile->owner = entity;
	VectorCopy(entity->s.origin, projectile->s.origin);
	VectorSet(projectile->velocity, 0.0f, 0.0f, -650.0f);
	callback_sent = 1;
	SG_RocketJumpGameFired(entity, projectile);
}

static void ResetFixture(void)
{
	edict_t *entity;
	sg_bot_t *bot;

	memset(&gi, 0, sizeof(gi));
	memset(&globals, 0, sizeof(globals));
	memset(&game, 0, sizeof(game));
	memset(&level, 0, sizeof(level));
	memset(edicts, 0, sizeof(edicts));
	memset(clients, 0, sizeof(clients));
	memset(itemlist, 0, sizeof(itemlist));
	memset(sg_bots, 0, sizeof(sg_bots));
	memset(&rune_fixture, 0, sizeof(rune_fixture));
	memset(seeds, 0, sizeof(seeds));
	memset(links, 0, sizeof(links));
	memset(generations, 0, sizeof(generations));
	memset(&debug_cvar, 0, sizeof(debug_cvar));
	memset(&sg_cv, 0, sizeof(sg_cv));
	memset(&sg_host, 0, sizeof(sg_host));
	g_edicts = edicts;
	globals.edicts = edicts;
	globals.edict_size = sizeof(edict_t);
	globals.num_edicts = TEST_EDICTS;
	globals.max_edicts = TEST_EDICTS;
	game.maxentities = TEST_EDICTS;
	edicts[0].inuse = true;
	edicts[0].s.number = 0;

	itemlist[1].pickup_name = "Rocket Launcher";
	itemlist[1].view_model = "models/weapons/v_rocket/tris.md2";
	itemlist[1].use = UseLauncher;
	itemlist[2].pickup_name = "Rockets";

	entity = &edicts[BOT_EDICT];
	entity->inuse = true;
	entity->s.number = BOT_EDICT;
	entity->client = &clients[0];
	entity->groundentity = &edicts[0];
	entity->health = 100;
	entity->max_health = 100;
	entity->deadflag = DEAD_NO;
	entity->viewheight = SG_RUNE_PROOF_ROCKETJUMP_VIEWHEIGHT;
	entity->client->ps.pmove.pm_type = PM_NORMAL;
	entity->client->pers.hand = RIGHT_HANDED;
	entity->client->pers.weapon = &itemlist[1];
	entity->client->weaponstate = WEAPON_READY;
	entity->client->pers.inventory[1] = 1;
	entity->client->pers.inventory[2] = 5;
	VectorSet(entity->s.origin, 72.0f, -140.0f, -322.0f);

	bot = &sg_bots[0];
	bot->active = true;
	bot->ent = entity;
	bot->commit_link = TEST_LINK;
	bot->sticky_link = TEST_LINK;

	VectorCopy(entity->s.origin, seeds[0].origin);
	VectorSet(seeds[1].origin, 72.0f, -140.0f, -250.0f);
	links[0].from = 0;
	links[0].to = 1;
	links[0].action = RL_ROCKETJUMP;
	links[0].cost_ms = 1800;
	links[0].anchor[0] = 16384.0f;
	links[0].anchor[1] = 0.0f;
	links[0].anchor[2] = 46.0f;
	rune_fixture.hdr.num_seeds = 2;
	rune_fixture.hdr.num_links = 1;
	rune_fixture.seeds = seeds;
	rune_fixture.links = links;

	generations[ROCKET_EDICT] = 10U;
	generations[FOREIGN_EDICT] = 20U;
	debug_cvar.value = 1.0f;
	sg_cv.debug = &debug_cvar;
	sg_host.dprint = NoPrint;
	sg_host.trace = TestTrace;
	trace_hit = &edicts[0];
	gi.unlinkentity = LinkEntity;
	gi.linkentity = LinkEntity;
	gi.modelindex = ModelIndex;
	callback_enabled = 0;
	callback_sent = 0;
}

static void BeginFlight(void)
{
	callback_enabled = 1;
	CHECK(SG_RocketJumpGameEmit(&sg_bots[0], TEST_LINK));
	CHECK(sg_bots[0].rocketjump.phase == SG_ROCKETJUMP_FLIGHT);
	CHECK(sg_bots[0].rocketjump.projectile.key == ROCKET_EDICT);
	CHECK(sg_bots[0].rocketjump.projectile.generation == 10U);
}

static void TestFireCallbackOwnsFlight(void)
{
	ResetFixture();
	CHECK(SG_RocketJumpGameEmit(&sg_bots[0], TEST_LINK));
	CHECK(sg_bots[0].rocketjump.phase == SG_ROCKETJUMP_ARMED);
	callback_enabled = 1;
	CHECK(SG_RocketJumpGameEmit(&sg_bots[0], TEST_LINK));
	CHECK(sg_bots[0].rocketjump.phase == SG_ROCKETJUMP_FLIGHT);
}

static void TestFiredRequiresStaticWorldImpact(void)
{
	ResetFixture();
	edicts[FOREIGN_EDICT].inuse = true;
	edicts[FOREIGN_EDICT].s.number = FOREIGN_EDICT;
	edicts[FOREIGN_EDICT].solid = SOLID_BSP;
	edicts[FOREIGN_EDICT].movetype = MOVETYPE_PUSH;
	trace_hit = &edicts[FOREIGN_EDICT];
	CHECK(SG_RocketJumpGameEmit(&sg_bots[0], TEST_LINK));
	CHECK(sg_bots[0].rocketjump.phase == SG_ROCKETJUMP_ARMED);
	callback_enabled = 1;
	CHECK(SG_RocketJumpGameEmit(&sg_bots[0], TEST_LINK));
	CHECK(sg_bots[0].rocketjump.phase == SG_ROCKETJUMP_ARMED);
	CHECK(sg_bots[0].rocketjump.projectile.key == 0U);

	ResetFixture();
	trace_hit = &edicts[0];
	BeginFlight();
}

static void TestForeignAndRecycledProjectiles(void)
{
	edict_t *projectile;
	sg_rocketjump_live_state_t before;

	ResetFixture();
	BeginFlight();
	projectile = &edicts[FOREIGN_EDICT];
	projectile->inuse = true;
	projectile->s.number = FOREIGN_EDICT;
	projectile->owner = &edicts[FOREIGN_EDICT];
	before = sg_bots[0].rocketjump;
	SG_RocketJumpGameImpactBegin(projectile, &edicts[0], NULL);
	CHECK(!memcmp(&before, &sg_bots[0].rocketjump, sizeof(before)));

	projectile = &edicts[ROCKET_EDICT];
	generations[ROCKET_EDICT]++;
	SG_RocketJumpGameImpactBegin(projectile, &edicts[0], NULL);
	CHECK(sg_bots[0].rocketjump.phase == SG_ROCKETJUMP_FAILED);
	CHECK(sg_bots[0].rocketjump.failure ==
	      SG_ROCKETJUMP_FAILURE_PROJECTILE);
	CHECK(SG_RocketJumpGameEmit(&sg_bots[0], TEST_LINK));
	CHECK(sg_bots[0].rocketjump.phase == SG_ROCKETJUMP_IDLE);
	CHECK(sg_bots[0].commit_link == -1);
}

static void TestCompletionResetsAdapter(void)
{
	edict_t *entity;
	edict_t *projectile;
	vec3_t expected;
	int axis;

	ResetFixture();
	BeginFlight();
	entity = &edicts[BOT_EDICT];
	projectile = &edicts[ROCKET_EDICT];
	for (axis = 0; axis < 3; axis++)
		expected[axis] = sg_bots[0].rocketjump.expected_impact_q8[axis] *
		    0.125f;
	VectorCopy(expected, projectile->s.origin);
	entity->velocity[2] = 10.0f;
	SG_RocketJumpGameImpactBegin(projectile, &edicts[0], NULL);
	entity->health = 54;
	entity->velocity[2] = 100.0f;
	SG_RocketJumpGameImpactEnd(projectile);
	CHECK(sg_bots[0].rocketjump.impact_confirmed);
	VectorCopy(seeds[1].origin, entity->s.origin);
	entity->groundentity = &edicts[0];
	CHECK(SG_RocketJumpGameEmit(&sg_bots[0], TEST_LINK));
	CHECK(sg_bots[0].rocketjump.phase == SG_ROCKETJUMP_IDLE);
	CHECK(sg_bots[0].commit_link == -1);
}

static void TestCommandFailureResetsAdapter(void)
{
	ResetFixture();
	CHECK(SG_RocketJumpGameEmit(&sg_bots[0], TEST_LINK));
	CHECK(sg_bots[0].rocketjump.phase == SG_ROCKETJUMP_ARMED);
	edicts[BOT_EDICT].health = 0;
	CHECK(SG_RocketJumpGameEmit(&sg_bots[0], TEST_LINK));
	CHECK(sg_bots[0].rocketjump.phase == SG_ROCKETJUMP_IDLE);
	CHECK(sg_bots[0].commit_link == -1);
}

static void TestAuthenticatedStaging(void)
{
	sg_bot_t bot_before;
	edict_t entity_before;
	gclient_t client_before;

	ResetFixture();
	links[0].action = RL_RUN;
	callback_enabled = 1;
	CHECK(!SG_RocketJumpGameStageAuthenticatedProbe(TEST_LINK));
	CHECK(sg_bots[0].rocketjump.phase == SG_ROCKETJUMP_IDLE);
	links[0].action = RL_ROCKETJUMP;

	sg_bots[0].compound_swim.guard_owned = true;
	bot_before = sg_bots[0];
	entity_before = edicts[BOT_EDICT];
	client_before = clients[0];
	CHECK(!SG_RocketJumpGameStageAuthenticatedProbe(TEST_LINK));
	CHECK(!memcmp(&bot_before, &sg_bots[0], sizeof(bot_before)));
	CHECK(!memcmp(&entity_before, &edicts[BOT_EDICT],
	    sizeof(entity_before)));
	CHECK(!memcmp(&client_before, &clients[0], sizeof(client_before)));
	CHECK(!callback_sent);

	sg_bots[0].compound_swim.guard_owned = false;
	sg_bots[0].compound_guard.ticket.epoch = 1U;
	sg_bots[0].compound_guard.ticket.serial = 1U;
	bot_before = sg_bots[0];
	entity_before = edicts[BOT_EDICT];
	client_before = clients[0];
	CHECK(!SG_RocketJumpGameStageAuthenticatedProbe(TEST_LINK));
	CHECK(!memcmp(&bot_before, &sg_bots[0], sizeof(bot_before)));
	CHECK(!memcmp(&entity_before, &edicts[BOT_EDICT],
	    sizeof(entity_before)));
	CHECK(!memcmp(&client_before, &clients[0], sizeof(client_before)));
	CHECK(!callback_sent);

	memset(&sg_bots[0].compound_guard.ticket, 0,
	    sizeof(sg_bots[0].compound_guard.ticket));
	sg_bots[0].hook_phase = 2;
	bot_before = sg_bots[0];
	entity_before = edicts[BOT_EDICT];
	client_before = clients[0];
	CHECK(!SG_RocketJumpGameStageAuthenticatedProbe(TEST_LINK));
	CHECK(!memcmp(&bot_before, &sg_bots[0], sizeof(bot_before)));
	CHECK(!memcmp(&entity_before, &edicts[BOT_EDICT],
	    sizeof(entity_before)));
	CHECK(!memcmp(&client_before, &clients[0], sizeof(client_before)));
	CHECK(!callback_sent);

	sg_bots[0].hook_phase = 0;
	CHECK(SG_RocketJumpGameStageAuthenticatedProbe(TEST_LINK));
	CHECK(sg_bots[0].rocketjump.phase == SG_ROCKETJUMP_FLIGHT);
}

int main(void)
{
	TestFireCallbackOwnsFlight();
	TestFiredRequiresStaticWorldImpact();
	TestForeignAndRecycledProjectiles();
	TestCompletionResetsAdapter();
	TestCommandFailureResetsAdapter();
	TestAuthenticatedStaging();
	if (failures)
	{
		fprintf(stderr, "sg_rocketjump_game_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("sg_rocketjump_game_test: ok");
	return 0;
}
