/* Dynamic authenticated mechanism execution regressions. */
#include "g_local.h"
#include "slipgate/sg_crc32.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_bot.h"
#include "slipgate/sg_compound_guard.h"
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_rune.h"
#include "slipgate/sg_rune_binding.h"
#include "slipgate/sg_rune_mechanism_catalog.h"
#include "slipgate/sg_move.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_EDICTS 28
#define TEST_LINKS 7U
#define TEST_EDGE_CAPACITY 128U

void Touch_Multi(edict_t *self, edict_t *other, cplane_t *plane,
	csurface_t *surface);
void Use_Multi(edict_t *self, edict_t *other, edict_t *activator);
void trigger_relay_use(edict_t *self, edict_t *other,
	edict_t *activator);

enum test_key_e
{
	KEY_PLATFORM = 1,
	KEY_PLATFORM_TRIGGER,
	KEY_TELEPORTER,
	KEY_TELEPORT_TRIGGER,
	KEY_TELEPORT_DESTINATION,
	KEY_AUTO_DOOR,
	KEY_AUTO_TRIGGER,
	KEY_AUTO_SPEAKER,
	KEY_DIRECT_RELAY,
	KEY_DIRECT_DOOR,
	KEY_DIRECT_TRIGGER,
	KEY_DIRECT_SPEAKER,
	KEY_BUTTON,
	KEY_BUTTON_DOOR,
	KEY_DIRECT_CLOSE_SPEAKER,
	KEY_CELLAR_DOOR,
	KEY_CELLAR_RELAY,
	KEY_CELLAR_TRIGGER,
	KEY_CELLAR_CLOSE_SPEAKER,
	KEY_GATE_DOOR,
	KEY_GATE_TRIGGER,
	KEY_GATE_OPEN_SPEAKER,
	KEY_GATE_RELAY,
	KEY_GATE_CLOSE_SPEAKER,
	KEY_BOT,
	KEY_SPARE
};

enum test_link_e
{
	LINK_PLATFORM = 0,
	LINK_TELEPORT,
	LINK_AUTO_DOOR,
	LINK_DIRECT_DOOR,
	LINK_BUTTON_DOOR,
	LINK_CELLAR_DOOR,
	LINK_GATE_DOOR
};

enum delayed_chain_e
{
	CHAIN_FRONT = 0,
	CHAIN_CELLAR,
	CHAIN_GATE,
	CHAIN_COUNT
};

game_locals_t game;
level_locals_t level;
game_import_t gi;
game_export_t globals;
edict_t *g_edicts;
sg_host_t sg_host;
sg_bot_t sg_bots[SG_MAXBOTS];
cvar_t *maxclients;

static edict_t test_edicts[TEST_EDICTS];
static gclient_t bot_client;
static cvar_t maxclients_value;
static rune_t *active_rune;
static int failures;
static int relay_g_use_targets[CHAIN_COUNT];
static int door_g_use_targets[CHAIN_COUNT];
static int door_uses[CHAIN_COUNT];
static int open_speaker_uses[CHAIN_COUNT];
static int close_speaker_uses[CHAIN_COUNT];
static char callback_order[CHAIN_COUNT][8];
static size_t callback_order_count[CHAIN_COUNT];

#define CHECK(condition_) do { \
	if (!(condition_)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, \
			#condition_); \
		failures++; \
	} \
} while (0)

#define TOUCH_CALLBACK(name_) \
	void name_(edict_t *self, edict_t *other, cplane_t *plane, \
		csurface_t *surface) \
	{ \
		(void)self; (void)other; (void)plane; (void)surface; \
	}
#define USE_CALLBACK(name_) \
	void name_(edict_t *self, edict_t *other, edict_t *activator) \
	{ \
		(void)self; (void)other; (void)activator; \
	}
#define THINK_CALLBACK(name_) \
	void name_(edict_t *self) \
	{ \
		(void)self; \
	}
#define BLOCKED_CALLBACK(name_) \
	void name_(edict_t *self, edict_t *other) \
	{ \
		(void)self; (void)other; \
	}

TOUCH_CALLBACK(Touch_DoorTrigger)
TOUCH_CALLBACK(button_touch)
TOUCH_CALLBACK(Touch_Plat_Center)
TOUCH_CALLBACK(teleporter_touch)
TOUCH_CALLBACK(path_corner_touch)
TOUCH_CALLBACK(Touch_Item)
TOUCH_CALLBACK(UnknownTouch)

USE_CALLBACK(button_use)
USE_CALLBACK(Use_Plat)
USE_CALLBACK(train_use)
USE_CALLBACK(trigger_elevator_use)
USE_CALLBACK(door_secret_use)
USE_CALLBACK(Use_Areaportal)
USE_CALLBACK(UnknownUse)

THINK_CALLBACK(button_wait)
THINK_CALLBACK(button_return)
THINK_CALLBACK(button_done)
THINK_CALLBACK(Think_CalcMoveSpeed)
THINK_CALLBACK(Think_SpawnDoorTrigger)
THINK_CALLBACK(plat_go_down)
THINK_CALLBACK(plat_hit_top)
THINK_CALLBACK(plat_hit_bottom)
THINK_CALLBACK(door_go_down)
THINK_CALLBACK(door_hit_top)
THINK_CALLBACK(door_hit_bottom)
THINK_CALLBACK(Move_Begin)
THINK_CALLBACK(Move_Final)
THINK_CALLBACK(Move_Done)
THINK_CALLBACK(AngleMove_Begin)
THINK_CALLBACK(AngleMove_Final)
THINK_CALLBACK(AngleMove_Done)
THINK_CALLBACK(Think_AccelMove)
THINK_CALLBACK(func_train_find)
THINK_CALLBACK(train_next)
THINK_CALLBACK(train_wait)
THINK_CALLBACK(trigger_elevator_init)
THINK_CALLBACK(UnknownThink)

BLOCKED_CALLBACK(door_blocked)
BLOCKED_CALLBACK(plat_blocked)
BLOCKED_CALLBACK(train_blocked)
BLOCKED_CALLBACK(door_secret_blocked)

void __real_G_UseTargets(edict_t *source, edict_t *activator);

static int RelayChain(const edict_t *source)
{
	if (source == &test_edicts[KEY_DIRECT_RELAY])
		return CHAIN_FRONT;
	if (source == &test_edicts[KEY_CELLAR_RELAY])
		return CHAIN_CELLAR;
	if (source == &test_edicts[KEY_GATE_RELAY])
		return CHAIN_GATE;
	return -1;
}

static int DoorChain(const edict_t *source)
{
	if (source == &test_edicts[KEY_DIRECT_DOOR])
		return CHAIN_FRONT;
	if (source == &test_edicts[KEY_CELLAR_DOOR])
		return CHAIN_CELLAR;
	if (source == &test_edicts[KEY_GATE_DOOR])
		return CHAIN_GATE;
	return -1;
}

static void NoteCallback(int chain, char callback)
{
	if (chain >= 0 && chain < CHAIN_COUNT &&
	    callback_order_count[chain] < sizeof(callback_order[chain]))
		callback_order[chain][callback_order_count[chain]++] = callback;
}

void __wrap_G_UseTargets(edict_t *source, edict_t *activator)
{
	int chain = RelayChain(source);

	if (chain >= 0)
	{
		relay_g_use_targets[chain]++;
		NoteCallback(chain, 'R');
	}
	chain = DoorChain(source);
	if (chain >= 0)
		door_g_use_targets[chain]++;
	__real_G_UseTargets(source, activator);
}

void door_use(edict_t *self, edict_t *other, edict_t *activator)
{
	int chain = DoorChain(self);
	edict_t *expected_trigger = NULL;

	CHECK(self == &test_edicts[KEY_DIRECT_DOOR] ||
	      self == &test_edicts[KEY_CELLAR_DOOR] ||
	      self == &test_edicts[KEY_GATE_DOOR] ||
	      self == &test_edicts[KEY_AUTO_DOOR] ||
	      self == &test_edicts[KEY_BUTTON_DOOR]);
	if (chain < 0)
		return;
	if (chain == CHAIN_FRONT)
		expected_trigger = &test_edicts[KEY_DIRECT_TRIGGER];
	else if (chain == CHAIN_CELLAR)
		expected_trigger = &test_edicts[KEY_CELLAR_TRIGGER];
	else
		expected_trigger = &test_edicts[KEY_GATE_TRIGGER];
	CHECK(other == expected_trigger);
	CHECK(activator == &test_edicts[KEY_BOT]);
	door_uses[chain]++;
	NoteCallback(chain, 'D');
	VectorClear(self->velocity);
	self->velocity[0] = 32.0f;
	self->moveinfo.state = 2;
	self->moveinfo.endfunc = door_hit_top;
	self->think = Move_Final;
	self->nextthink = 1.0f;
	G_UseTargets(self, activator);
}

void Use_Target_Speaker(edict_t *self, edict_t *other,
	edict_t *activator)
{
	int chain = -1;
	int closing = 0;

	CHECK(activator == &test_edicts[KEY_BOT]);
	if (self == &test_edicts[KEY_DIRECT_SPEAKER])
	{
		chain = CHAIN_FRONT;
		CHECK(other == &test_edicts[KEY_DIRECT_DOOR]);
	}
	else if (self == &test_edicts[KEY_GATE_OPEN_SPEAKER])
	{
		chain = CHAIN_GATE;
		CHECK(other == &test_edicts[KEY_GATE_DOOR]);
	}
	else if (self == &test_edicts[KEY_DIRECT_CLOSE_SPEAKER])
	{
		chain = CHAIN_FRONT;
		closing = 1;
	}
	else if (self == &test_edicts[KEY_CELLAR_CLOSE_SPEAKER])
	{
		chain = CHAIN_CELLAR;
		closing = 1;
	}
	else if (self == &test_edicts[KEY_GATE_CLOSE_SPEAKER])
	{
		chain = CHAIN_GATE;
		closing = 1;
	}
	if (chain >= 0 && closing)
		close_speaker_uses[chain]++;
	else if (chain >= 0)
	{
		open_speaker_uses[chain]++;
		NoteCallback(chain, 'S');
	}
}

void button_killed(edict_t *self, edict_t *inflictor, edict_t *attacker,
	int damage, vec3_t point)
{
	(void)self;
	(void)inflictor;
	(void)attacker;
	(void)damage;
	(void)point;
}

void SG_HooksInit(void)
{
}

qboolean SG_OwnsBot(edict_t *entity)
{
	return entity == &test_edicts[KEY_BOT];
}

rune_t *SG_Rune(void)
{
	return active_rune;
}

qboolean SG_RunePhysicsCompatible(const rune_t *rune)
{
	return rune && rune == active_rune;
}

sg_compound_guard_result_t SG_DeclaredDoorGuardAuthorizeActivation(
	sg_bot_t *bot, int link_index)
{
	return bot == &sg_bots[0] &&
	    (link_index == LINK_DIRECT_DOOR ||
	     link_index == LINK_CELLAR_DOOR ||
	     link_index == LINK_GATE_DOOR)
	    ? SG_COMPOUND_GUARD_OK : SG_COMPOUND_GUARD_INVALID_ARGUMENT;
}

int SG_DeclaredDoorGuardAnyClaim(void)
{
	return 0;
}

qboolean SG_BoundDoorTouchMatches(
	const sg_rune_mechanism_binding_t *binding, const vec3_t origin)
{
	(void)origin;
	return binding != NULL;
}

sg_compound_guard_result_t SG_CompoundGuardValidate(
	sg_compound_guard_bot_t *guard, sg_mover_lease_record_t *record_out)
{
	(void)guard;
	if (record_out)
		memset(record_out, 0, sizeof(*record_out));
	return SG_COMPOUND_GUARD_INVALID_ARGUMENT;
}

void SG_CompoundGuardGameEntityFreed(edict_t *entity)
{
	(void)entity;
}

static void *TestAlloc(int bytes)
{
	return bytes > 0 ? calloc(1U, (size_t)bytes) : NULL;
}

static void TestFree(void *memory)
{
	free(memory);
}

typedef struct execution_fixture_s
{
	rune_t rune;
	rune_seed_t seeds[2];
	rune_link_t links[TEST_LINKS];
	rune_mechanism_plan_t plans[TEST_LINKS];
	rune_mechanism_edge_t edges[TEST_EDGE_CAPACITY];
	int first_link[2];
	int next_link[TEST_LINKS];
	byte linked_seed[2];
	sg_mech_catalog_view_t catalog;
	uint32_t edge_count;
} execution_fixture_t;

static void PutU16(unsigned char *output, uint16_t value)
{
	output[0] = (unsigned char)(value & UINT16_C(0xff));
	output[1] = (unsigned char)(value >> 8);
}

static void PutU32(unsigned char *output, uint32_t value)
{
	output[0] = (unsigned char)(value & UINT32_C(0xff));
	output[1] = (unsigned char)((value >> 8) & UINT32_C(0xff));
	output[2] = (unsigned char)((value >> 16) & UINT32_C(0xff));
	output[3] = (unsigned char)(value >> 24);
}

static uint32_t PlanCRC(const execution_fixture_t *fixture,
	const rune_mechanism_plan_t *plan)
{
	unsigned char encoded[16];
	uint32_t state = SG_CRC32Init();
	uint32_t ordinal;

	for (ordinal = 0U; ordinal < plan->num_edges; ordinal++)
	{
		const rune_mechanism_edge_t *edge =
			&fixture->edges[plan->first_edge + ordinal];

		PutU32(encoded + 0U, edge->from_key);
		PutU32(encoded + 4U, edge->to_key);
		PutU16(encoded + 8U, edge->kind);
		PutU16(encoded + 10U, edge->ordinal);
		PutU32(encoded + 12U, edge->delay_ms);
		CHECK(SG_CRC32Update(&state, encoded, sizeof(encoded)));
	}
	return SG_CRC32Final(state);
}

static const rune_mechanism_edge_t *CatalogEdge(
	const execution_fixture_t *fixture, uint32_t from, uint32_t to,
	uint16_t kind, uint16_t ordinal)
{
	uint32_t index;

	for (index = 0U; index < fixture->catalog.num_edges; index++)
	{
		const rune_mechanism_edge_t *edge = &fixture->catalog.edges[index];

		if (edge->from_key == from && edge->to_key == to &&
		    edge->kind == kind && edge->ordinal == ordinal)
			return edge;
	}
	return NULL;
}

static void AddPlanEdge(execution_fixture_t *fixture, uint32_t from,
	uint32_t to, uint16_t kind, uint16_t ordinal)
{
	const rune_mechanism_edge_t *edge =
		CatalogEdge(fixture, from, to, kind, ordinal);

	CHECK(edge != NULL);
	CHECK(fixture->edge_count < TEST_EDGE_CAPACITY);
	if (edge && fixture->edge_count < TEST_EDGE_CAPACITY)
		fixture->edges[fixture->edge_count++] = *edge;
}

static void BeginPlan(execution_fixture_t *fixture, uint32_t plan_index,
	uint16_t controller, uint32_t entry_key, uint32_t mover_key,
	uint32_t cooldown_ms)
{
	rune_mechanism_plan_t *plan = &fixture->plans[plan_index];

	memset(plan, 0, sizeof(*plan));
	plan->entry_key = entry_key;
	plan->mover_key = mover_key;
	plan->first_edge = fixture->edge_count;
	plan->controller_kind = controller;
	plan->flags = SG_MechanismControllerPlanFlags(controller);
	plan->expected_members = 1U;
	plan->cooldown_ms = cooldown_ms;
}

static void FinishPlan(execution_fixture_t *fixture, uint32_t plan_index)
{
	rune_mechanism_plan_t *plan = &fixture->plans[plan_index];

	plan->num_edges = fixture->edge_count - plan->first_edge;
	CHECK(plan->num_edges != 0U);
	plan->closure_crc32 = PlanCRC(fixture, plan);
}

static void InitializeEntity(uint32_t key, const char *classname)
{
	edict_t *entity = &test_edicts[key];

	memset(entity, 0, sizeof(*entity));
	entity->inuse = true;
	entity->s.number = (int)key;
	entity->classname = (char *)classname;
	entity->wait = 1.0f;
	SG_MechCatalogEntityInitialized(entity);
	SG_MechCatalogDeclared(entity, key, classname);
}

static void DoorEntity(uint32_t key, const char *targetname)
{
	edict_t *door = &test_edicts[key];

	InitializeEntity(key, "func_door");
	door->targetname = (char *)targetname;
	door->movetype = MOVETYPE_PUSH;
	door->solid = SOLID_BSP;
	door->use = door_use;
	door->blocked = door_blocked;
	door->teammaster = door;
	door->moveinfo.state = 1;
	door->moveinfo.speed = 100.0f;
	door->moveinfo.accel = 100.0f;
	door->moveinfo.decel = 100.0f;
}

static void BuildLiveCatalog(execution_fixture_t *fixture)
{
	edict_t *entity;

	memset(&game, 0, sizeof(game));
	memset(&level, 0, sizeof(level));
	memset(&gi, 0, sizeof(gi));
	memset(&globals, 0, sizeof(globals));
	memset(test_edicts, 0, sizeof(test_edicts));
	memset(&bot_client, 0, sizeof(bot_client));
	memset(&maxclients_value, 0, sizeof(maxclients_value));
	memset(sg_bots, 0, sizeof(sg_bots));
	memset(&sg_host, 0, sizeof(sg_host));
	g_edicts = test_edicts;
	maxclients = &maxclients_value;
	game.maxentities = TEST_EDICTS;
	globals.edicts = test_edicts;
	globals.edict_size = (int)sizeof(test_edicts[0]);
	globals.num_edicts = KEY_SPARE + 1;
	level.framenum = 37;
	sg_host.level_alloc = TestAlloc;
	sg_host.level_free = TestFree;
	memset(relay_g_use_targets, 0, sizeof(relay_g_use_targets));
	memset(door_g_use_targets, 0, sizeof(door_g_use_targets));
	memset(door_uses, 0, sizeof(door_uses));
	memset(open_speaker_uses, 0, sizeof(open_speaker_uses));
	memset(close_speaker_uses, 0, sizeof(close_speaker_uses));
	memset(callback_order_count, 0, sizeof(callback_order_count));
	memset(callback_order, 0, sizeof(callback_order));
	SG_ButtonExecutionLevelReset();
	SG_MechCatalogBegin();

	InitializeEntity(KEY_PLATFORM, "func_plat");
	entity = &test_edicts[KEY_PLATFORM];
	entity->movetype = MOVETYPE_PUSH;
	entity->solid = SOLID_BSP;
	entity->use = Use_Plat;
	entity->blocked = plat_blocked;
	entity->moveinfo.state = 1;
	entity->moveinfo.speed = 100.0f;
	entity->moveinfo.accel = 100.0f;
	entity->moveinfo.decel = 100.0f;
	InitializeEntity(KEY_PLATFORM_TRIGGER, "noclass");
	test_edicts[KEY_PLATFORM_TRIGGER].touch = Touch_Plat_Center;
	SG_MechCatalogSynthetic(&test_edicts[KEY_PLATFORM_TRIGGER],
	    &test_edicts[KEY_PLATFORM], SG_MECH_SYNTHETIC_PLATFORM);

	InitializeEntity(KEY_TELEPORTER, "misc_teleporter");
	InitializeEntity(KEY_TELEPORT_TRIGGER, "noclass");
	test_edicts[KEY_TELEPORT_TRIGGER].touch = teleporter_touch;
	test_edicts[KEY_TELEPORT_TRIGGER].target = "tele-destination";
	SG_MechCatalogSynthetic(&test_edicts[KEY_TELEPORT_TRIGGER],
	    &test_edicts[KEY_TELEPORTER], SG_MECH_SYNTHETIC_TELEPORT);
	InitializeEntity(KEY_TELEPORT_DESTINATION, "misc_teleporter_dest");
	test_edicts[KEY_TELEPORT_DESTINATION].targetname = "tele-destination";

	DoorEntity(KEY_AUTO_DOOR, NULL);
	test_edicts[KEY_AUTO_DOOR].target = "auto-sound";
	InitializeEntity(KEY_AUTO_TRIGGER, "noclass");
	test_edicts[KEY_AUTO_TRIGGER].touch = Touch_DoorTrigger;
	SG_MechCatalogSynthetic(&test_edicts[KEY_AUTO_TRIGGER],
	    &test_edicts[KEY_AUTO_DOOR], SG_MECH_SYNTHETIC_AUTO_DOOR);
	InitializeEntity(KEY_AUTO_SPEAKER, "target_speaker");
	test_edicts[KEY_AUTO_SPEAKER].targetname = "auto-sound";
	test_edicts[KEY_AUTO_SPEAKER].use = Use_Target_Speaker;

	InitializeEntity(KEY_DIRECT_RELAY, "trigger_relay");
	test_edicts[KEY_DIRECT_RELAY].targetname = "direct-targets";
	test_edicts[KEY_DIRECT_RELAY].target = "direct-close";
	test_edicts[KEY_DIRECT_RELAY].use = trigger_relay_use;
	test_edicts[KEY_DIRECT_RELAY].delay = 311.0f;
	DoorEntity(KEY_DIRECT_DOOR, "direct-targets");
	test_edicts[KEY_DIRECT_DOOR].target = "direct-open";
	test_edicts[KEY_DIRECT_DOOR].spawnflags = 2 | 4;
	test_edicts[KEY_DIRECT_DOOR].moveinfo.wait = 300.0f;
	test_edicts[KEY_DIRECT_DOOR].moveinfo.speed = 15.0f;
	test_edicts[KEY_DIRECT_DOOR].moveinfo.accel = 15.0f;
	test_edicts[KEY_DIRECT_DOOR].moveinfo.decel = 15.0f;
	InitializeEntity(KEY_DIRECT_TRIGGER, "trigger_multiple");
	test_edicts[KEY_DIRECT_TRIGGER].target = "direct-targets";
	test_edicts[KEY_DIRECT_TRIGGER].touch = Touch_Multi;
	test_edicts[KEY_DIRECT_TRIGGER].use = Use_Multi;
	test_edicts[KEY_DIRECT_TRIGGER].wait = 312.0f;
	InitializeEntity(KEY_DIRECT_SPEAKER, "target_speaker");
	test_edicts[KEY_DIRECT_SPEAKER].targetname = "direct-open";
	test_edicts[KEY_DIRECT_SPEAKER].use = Use_Target_Speaker;
	InitializeEntity(KEY_DIRECT_CLOSE_SPEAKER, "target_speaker");
	test_edicts[KEY_DIRECT_CLOSE_SPEAKER].targetname = "direct-close";
	test_edicts[KEY_DIRECT_CLOSE_SPEAKER].use = Use_Target_Speaker;

	InitializeEntity(KEY_BUTTON, "func_button");
	entity = &test_edicts[KEY_BUTTON];
	entity->target = "button-target";
	entity->movetype = MOVETYPE_STOP;
	entity->solid = SOLID_BSP;
	entity->touch = button_touch;
	entity->use = button_use;
	entity->moveinfo.state = 1;
	entity->moveinfo.wait = 3.0f;
	DoorEntity(KEY_BUTTON_DOOR, "button-target");

	/* lmctf58 cellar shape: the physical door is engine ordinal zero and the
	 * delayed close-sound relay is ordinal one on the entry trigger. */
	DoorEntity(KEY_CELLAR_DOOR, "cellar-targets");
	test_edicts[KEY_CELLAR_DOOR].spawnflags = 2 | 4;
	test_edicts[KEY_CELLAR_DOOR].moveinfo.wait = 60.0f;
	InitializeEntity(KEY_CELLAR_RELAY, "trigger_relay");
	test_edicts[KEY_CELLAR_RELAY].targetname = "cellar-targets";
	test_edicts[KEY_CELLAR_RELAY].target = "cellar-close";
	test_edicts[KEY_CELLAR_RELAY].use = trigger_relay_use;
	test_edicts[KEY_CELLAR_RELAY].delay = 61.0f;
	InitializeEntity(KEY_CELLAR_TRIGGER, "trigger_multiple");
	test_edicts[KEY_CELLAR_TRIGGER].target = "cellar-targets";
	test_edicts[KEY_CELLAR_TRIGGER].touch = Touch_Multi;
	test_edicts[KEY_CELLAR_TRIGGER].use = Use_Multi;
	test_edicts[KEY_CELLAR_TRIGGER].wait = 63.0f;
	InitializeEntity(KEY_CELLAR_CLOSE_SPEAKER, "target_speaker");
	test_edicts[KEY_CELLAR_CLOSE_SPEAKER].targetname = "cellar-close";
	test_edicts[KEY_CELLAR_CLOSE_SPEAKER].use = Use_Target_Speaker;

	/* lmctf58 gate shape: the entry targets only the door; the door itself
	 * fans out to an immediate open speaker followed by a delayed close relay. */
	DoorEntity(KEY_GATE_DOOR, "gate-targets");
	test_edicts[KEY_GATE_DOOR].target = "gate-effects";
	test_edicts[KEY_GATE_DOOR].moveinfo.wait = 300.0f;
	test_edicts[KEY_GATE_DOOR].moveinfo.speed = 56.0f;
	test_edicts[KEY_GATE_DOOR].moveinfo.accel = 56.0f;
	test_edicts[KEY_GATE_DOOR].moveinfo.decel = 56.0f;
	InitializeEntity(KEY_GATE_TRIGGER, "trigger_multiple");
	test_edicts[KEY_GATE_TRIGGER].target = "gate-targets";
	test_edicts[KEY_GATE_TRIGGER].touch = Touch_Multi;
	test_edicts[KEY_GATE_TRIGGER].use = Use_Multi;
	test_edicts[KEY_GATE_TRIGGER].wait = 304.0f;
	InitializeEntity(KEY_GATE_OPEN_SPEAKER, "target_speaker");
	test_edicts[KEY_GATE_OPEN_SPEAKER].targetname = "gate-effects";
	test_edicts[KEY_GATE_OPEN_SPEAKER].use = Use_Target_Speaker;
	InitializeEntity(KEY_GATE_RELAY, "trigger_relay");
	test_edicts[KEY_GATE_RELAY].targetname = "gate-effects";
	test_edicts[KEY_GATE_RELAY].target = "gate-close";
	test_edicts[KEY_GATE_RELAY].use = trigger_relay_use;
	test_edicts[KEY_GATE_RELAY].delay = 301.0f;
	InitializeEntity(KEY_GATE_CLOSE_SPEAKER, "target_speaker");
	test_edicts[KEY_GATE_CLOSE_SPEAKER].targetname = "gate-close";
	test_edicts[KEY_GATE_CLOSE_SPEAKER].use = Use_Target_Speaker;

	test_edicts[KEY_BOT].inuse = true;
	test_edicts[KEY_BOT].s.number = KEY_BOT;
	test_edicts[KEY_BOT].classname = "bot";
	test_edicts[KEY_BOT].client = &bot_client;
	test_edicts[KEY_BOT].flags = FL_BOT;
	test_edicts[KEY_BOT].health = 100;

	CHECK(SG_MechCatalogSeal() == SG_MECH_CATALOG_READY);
	CHECK(SG_MechCatalogSnapshot(&fixture->catalog) ==
	    SG_MECH_CATALOG_READY);
}

static void ConfigureLink(execution_fixture_t *fixture, uint32_t index,
	int action)
{
	rune_link_t *link = &fixture->links[index];

	memset(link, 0, sizeof(*link));
	link->from = 0;
	link->to = 1;
	link->action = (byte)action;
	link->provenance = RL_DECLARED;
	link->cost_ms = 100;
	link->mechanism_plan = index;
}

static void BuildRune(execution_fixture_t *fixture)
{
	rune_t *rune = &fixture->rune;
	uint32_t index;

	fixture->edge_count = fixture->catalog.num_edges;
	CHECK(fixture->edge_count < TEST_EDGE_CAPACITY);
	memcpy(fixture->edges, fixture->catalog.edges,
	    (size_t)fixture->catalog.num_edges * sizeof(fixture->edges[0]));

	ConfigureLink(fixture, LINK_PLATFORM, RL_LIFT);
	BeginPlan(fixture, LINK_PLATFORM, SG_MECHANISM_CONTROLLER_PLATFORM,
	    KEY_PLATFORM_TRIGGER, KEY_PLATFORM, 0U);
	AddPlanEdge(fixture, KEY_PLATFORM_TRIGGER, KEY_PLATFORM,
	    SG_MECH_EDGE_OWNER, 0U);
	FinishPlan(fixture, LINK_PLATFORM);

	ConfigureLink(fixture, LINK_TELEPORT, RL_TELEPORT);
	BeginPlan(fixture, LINK_TELEPORT, SG_MECHANISM_CONTROLLER_TELEPORT,
	    KEY_TELEPORT_TRIGGER, KEY_TELEPORTER, 0U);
	AddPlanEdge(fixture, KEY_TELEPORT_TRIGGER, KEY_TELEPORTER,
	    SG_MECH_EDGE_OWNER, 0U);
	AddPlanEdge(fixture, KEY_TELEPORT_TRIGGER, KEY_TELEPORT_DESTINATION,
	    SG_MECH_EDGE_TARGET, 0U);
	FinishPlan(fixture, LINK_TELEPORT);

	ConfigureLink(fixture, LINK_AUTO_DOOR, RL_DOOR);
	BeginPlan(fixture, LINK_AUTO_DOOR, SG_MECHANISM_CONTROLLER_AUTO_DOOR,
	    KEY_AUTO_TRIGGER, KEY_AUTO_DOOR, 1000U);
	AddPlanEdge(fixture, KEY_AUTO_TRIGGER, KEY_AUTO_DOOR,
	    SG_MECH_EDGE_OWNER, 0U);
	AddPlanEdge(fixture, KEY_AUTO_DOOR, KEY_AUTO_SPEAKER,
	    SG_MECH_EDGE_TARGET, 0U);
	FinishPlan(fixture, LINK_AUTO_DOOR);

	ConfigureLink(fixture, LINK_DIRECT_DOOR, RL_DOOR);
	BeginPlan(fixture, LINK_DIRECT_DOOR,
	    SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR,
	    KEY_DIRECT_TRIGGER, KEY_DIRECT_DOOR, RUNE_MAX_COST_MS);
	AddPlanEdge(fixture, KEY_DIRECT_TRIGGER, KEY_DIRECT_RELAY,
	    SG_MECH_EDGE_TARGET, 0U);
	AddPlanEdge(fixture, KEY_DIRECT_TRIGGER, KEY_DIRECT_DOOR,
	    SG_MECH_EDGE_TARGET, 1U);
	AddPlanEdge(fixture, KEY_DIRECT_DOOR, KEY_DIRECT_SPEAKER,
	    SG_MECH_EDGE_TARGET, 0U);
	FinishPlan(fixture, LINK_DIRECT_DOOR);

	ConfigureLink(fixture, LINK_BUTTON_DOOR, RL_BUTTON_DOOR);
	BeginPlan(fixture, LINK_BUTTON_DOOR,
	    SG_MECHANISM_CONTROLLER_BUTTON_DOOR,
	    KEY_BUTTON, KEY_BUTTON_DOOR, 3000U);
	AddPlanEdge(fixture, KEY_BUTTON, KEY_BUTTON_DOOR,
	    SG_MECH_EDGE_TARGET, 0U);
	FinishPlan(fixture, LINK_BUTTON_DOOR);

	ConfigureLink(fixture, LINK_CELLAR_DOOR, RL_DOOR);
	BeginPlan(fixture, LINK_CELLAR_DOOR,
	    SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR,
	    KEY_CELLAR_TRIGGER, KEY_CELLAR_DOOR, RUNE_MAX_COST_MS);
	AddPlanEdge(fixture, KEY_CELLAR_TRIGGER, KEY_CELLAR_DOOR,
	    SG_MECH_EDGE_TARGET, 0U);
	AddPlanEdge(fixture, KEY_CELLAR_TRIGGER, KEY_CELLAR_RELAY,
	    SG_MECH_EDGE_TARGET, 1U);
	FinishPlan(fixture, LINK_CELLAR_DOOR);

	ConfigureLink(fixture, LINK_GATE_DOOR, RL_DOOR);
	BeginPlan(fixture, LINK_GATE_DOOR,
	    SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR,
	    KEY_GATE_TRIGGER, KEY_GATE_DOOR, RUNE_MAX_COST_MS);
	AddPlanEdge(fixture, KEY_GATE_TRIGGER, KEY_GATE_DOOR,
	    SG_MECH_EDGE_TARGET, 0U);
	AddPlanEdge(fixture, KEY_GATE_DOOR, KEY_GATE_OPEN_SPEAKER,
	    SG_MECH_EDGE_TARGET, 0U);
	AddPlanEdge(fixture, KEY_GATE_DOOR, KEY_GATE_RELAY,
	    SG_MECH_EDGE_TARGET, 1U);
	FinishPlan(fixture, LINK_GATE_DOOR);

	memset(rune, 0, sizeof(*rune));
	rune->artifact.magic = RUNE_ARTIFACT_MAGIC;
	rune->artifact.header_crc32 = 1U;
	rune->artifact.num_seeds = 2U;
	rune->artifact.num_links = TEST_LINKS;
	rune->artifact.num_mechanism_nodes = fixture->catalog.num_nodes;
	rune->artifact.num_inventory_edges = fixture->catalog.num_edges;
	rune->artifact.num_mechanism_edges = fixture->edge_count;
	rune->artifact.num_mechanism_plans = TEST_LINKS;
	rune->artifact.string_bytes = fixture->catalog.string_bytes;
	memcpy(rune->artifact.identity.map_name, "execution-test", 15U);
	rune->hdr.magic = (int)RUNE_ARTIFACT_MAGIC;
	rune->hdr.num_seeds = 2;
	rune->hdr.num_links = (int)TEST_LINKS;
	memcpy(rune->hdr.mapname, "execution-test", 15U);
	rune->seeds = fixture->seeds;
	rune->links = fixture->links;
	rune->mechanism_nodes = (rune_mechanism_node_t *)fixture->catalog.nodes;
	rune->mechanism_edges = fixture->edges;
	rune->mechanism_plans = fixture->plans;
	rune->mechanism_strings =
	    (unsigned char *)fixture->catalog.strings;
	rune->first_link = fixture->first_link;
	rune->next_link = fixture->next_link;
	rune->linked_seed = fixture->linked_seed;
	fixture->first_link[0] = 0;
	fixture->first_link[1] = -1;
	fixture->linked_seed[0] = 1U;
	for (index = 0U; index < TEST_LINKS; index++)
		fixture->next_link[index] = -1;
	CHECK(SG_RunePublishedShapeValid(rune));
}

static void FixtureBuild(execution_fixture_t *fixture)
{
	memset(fixture, 0, sizeof(*fixture));
	BuildLiveCatalog(fixture);
	BuildRune(fixture);
	active_rune = &fixture->rune;
}

static void SetDoorMoving(edict_t *door)
{
	VectorClear(door->velocity);
	door->velocity[0] = 32.0f;
	door->moveinfo.state = 2;
	door->moveinfo.endfunc = door_hit_top;
	door->think = Move_Final;
	door->nextthink = 1.0f;
}

typedef struct dispatch_log_s
{
	uint32_t keys[4];
	uint32_t count;
	edict_t *moving_door;
} dispatch_log_t;

static int DispatchTarget(void *raw_context, struct edict_s *raw_target,
	uint32_t target_key, uint32_t target_ordinal)
{
	dispatch_log_t *log = raw_context;
	edict_t *target = raw_target;

	CHECK(log != NULL);
	CHECK(target != NULL);
	CHECK(log && log->count == target_ordinal);
	if (!log || !target || log->count >= 4U)
		return 0;
	log->keys[log->count++] = target_key;
	if (target == log->moving_door)
		SetDoorMoving(target);
	return 1;
}

static void TestPublicationGate(execution_fixture_t *fixture)
{
	uint32_t failure_index = UINT32_MAX;
	char *saved_targetname;

	CHECK(SG_RuneMechanismBindingsReady(&fixture->rune, &failure_index));
	CHECK(failure_index == UINT32_MAX);
	saved_targetname = test_edicts[KEY_TELEPORT_DESTINATION].targetname;
	test_edicts[KEY_TELEPORT_DESTINATION].targetname = "wrong-destination";
	CHECK(!SG_RuneMechanismBindingsReady(&fixture->rune, &failure_index));
	CHECK(failure_index == LINK_TELEPORT);
	test_edicts[KEY_TELEPORT_DESTINATION].targetname = saved_targetname;
	CHECK(SG_RuneMechanismBindingsReady(&fixture->rune, &failure_index));
}

static void TestPlatform(execution_fixture_t *fixture)
{
	sg_rune_mechanism_binding_t binding;
	edict_t *platform = &test_edicts[KEY_PLATFORM];

	CHECK(SG_RuneMechanismBindingCapture(&fixture->rune, LINK_PLATFORM,
	    &binding));
	VectorClear(platform->velocity);
	platform->velocity[2] = 32.0f;
	platform->moveinfo.state = 2;
	platform->moveinfo.endfunc = plat_hit_top;
	platform->think = Move_Final;
	platform->nextthink = 1.0f;
	CHECK(SG_RuneMechanismBindingCurrent(&binding));
	CHECK(SG_RuneMechanismBindingCaptureOwned(&fixture->rune,
	    LINK_PLATFORM, &binding));
	platform->think = UnknownThink;
	CHECK(!SG_RuneMechanismBindingCurrent(&binding));
}

static void TestTeleport(execution_fixture_t *fixture)
{
	sg_rune_mechanism_binding_t binding;
	edict_t *trigger = &test_edicts[KEY_TELEPORT_TRIGGER];

	CHECK(SG_RuneMechanismBindingCapture(&fixture->rune, LINK_TELEPORT,
	    &binding));
	CHECK(SG_RuneMechanismBindingResolveDestination(&binding) ==
	    &test_edicts[KEY_TELEPORT_DESTINATION]);
	trigger->touch = UnknownTouch;
	CHECK(!SG_RuneMechanismBindingCurrent(&binding));
	CHECK(SG_RuneMechanismBindingResolveDestination(&binding) == NULL);
	trigger->touch = teleporter_touch;
	CHECK(SG_RuneMechanismBindingCurrent(&binding));
}

static void TestAutoDoor(execution_fixture_t *fixture)
{
	sg_rune_mechanism_binding_t binding;
	dispatch_log_t log;
	edict_t *door = &test_edicts[KEY_AUTO_DOOR];

	CHECK(SG_RuneMechanismBindingCapture(&fixture->rune, LINK_AUTO_DOOR,
	    &binding));
	SetDoorMoving(door);
	CHECK(SG_RuneMechanismBindingCurrent(&binding));
	memset(&log, 0, sizeof(log));
	CHECK(SG_RuneMechanismBindingDispatchTargets(&binding, KEY_AUTO_DOOR,
	    DispatchTarget, &log));
	CHECK(log.count == 1U && log.keys[0] == KEY_AUTO_SPEAKER);
	door->blocked = plat_blocked;
	CHECK(!SG_RuneMechanismBindingCurrent(&binding));
}

static void TestDirectDoor(execution_fixture_t *fixture)
{
	sg_rune_mechanism_binding_t binding;
	sg_rune_mechanism_binding_t front_binding;
	dispatch_log_t log;
	edict_t *door = &test_edicts[KEY_DIRECT_DOOR];
	edict_t *cellar_door = &test_edicts[KEY_CELLAR_DOOR];
	edict_t *gate_door = &test_edicts[KEY_GATE_DOOR];
	edict_t *relay = &test_edicts[KEY_DIRECT_RELAY];
	char *saved_target;
	void (*saved_use)(edict_t *, edict_t *, edict_t *);

	CHECK(SG_RuneMechanismBindingCapture(&fixture->rune, LINK_DIRECT_DOOR,
	    &binding));
	memset(&log, 0, sizeof(log));
	log.moving_door = door;
	CHECK(SG_RuneMechanismBindingDispatchTargets(&binding,
	    KEY_DIRECT_TRIGGER, DispatchTarget, &log));
	CHECK(log.count == 2U);
	CHECK(log.keys[0] == KEY_DIRECT_RELAY);
	CHECK(log.keys[1] == KEY_DIRECT_DOOR);
	CHECK(!SG_RuneMechanismBindingDispatchTargets(&binding,
	    KEY_DIRECT_RELAY, DispatchTarget, &log));
	CHECK(log.count == 2U);
	memset(&log, 0, sizeof(log));
	CHECK(SG_RuneMechanismBindingDispatchTargets(&binding,
	    KEY_DIRECT_DOOR, DispatchTarget, &log));
	CHECK(log.count == 1U && log.keys[0] == KEY_DIRECT_SPEAKER);
	CHECK(SG_RuneMechanismBindingCurrent(&binding));
	front_binding = binding;

	CHECK(SG_RuneMechanismBindingCapture(&fixture->rune,
	    LINK_CELLAR_DOOR, &binding));
	memset(&log, 0, sizeof(log));
	log.moving_door = cellar_door;
	CHECK(SG_RuneMechanismBindingDispatchTargets(&binding,
	    KEY_CELLAR_TRIGGER, DispatchTarget, &log));
	CHECK(log.count == 2U);
	CHECK(log.keys[0] == KEY_CELLAR_DOOR);
	CHECK(log.keys[1] == KEY_CELLAR_RELAY);
	CHECK(!SG_RuneMechanismBindingDispatchTargets(&binding,
	    KEY_CELLAR_RELAY, DispatchTarget, &log));
	CHECK(log.count == 2U);
	CHECK(SG_RuneMechanismBindingCurrent(&binding));

	CHECK(SG_RuneMechanismBindingCapture(&fixture->rune,
	    LINK_GATE_DOOR, &binding));
	memset(&log, 0, sizeof(log));
	log.moving_door = gate_door;
	CHECK(SG_RuneMechanismBindingDispatchTargets(&binding,
	    KEY_GATE_TRIGGER, DispatchTarget, &log));
	CHECK(log.count == 1U && log.keys[0] == KEY_GATE_DOOR);
	memset(&log, 0, sizeof(log));
	CHECK(SG_RuneMechanismBindingDispatchTargets(&binding,
	    KEY_GATE_DOOR, DispatchTarget, &log));
	CHECK(log.count == 2U);
	CHECK(log.keys[0] == KEY_GATE_OPEN_SPEAKER);
	CHECK(log.keys[1] == KEY_GATE_RELAY);
	CHECK(!SG_RuneMechanismBindingDispatchTargets(&binding,
	    KEY_GATE_RELAY, DispatchTarget, &log));
	CHECK(log.count == 2U);
	CHECK(SG_RuneMechanismBindingCurrent(&binding));

	binding = front_binding;
	saved_target = test_edicts[KEY_DIRECT_TRIGGER].target;
	test_edicts[KEY_DIRECT_TRIGGER].target = "wrong-targets";
	CHECK(!SG_RuneMechanismBindingCurrent(&binding));
	test_edicts[KEY_DIRECT_TRIGGER].target = saved_target;
	CHECK(SG_RuneMechanismBindingCurrent(&binding));
	relay->delay = 310.0f;
	CHECK(!SG_RuneMechanismBindingCurrent(&binding));
	relay->delay = 311.0f;
	CHECK(SG_RuneMechanismBindingCurrent(&binding));
	saved_use = relay->use;
	relay->use = UnknownUse;
	CHECK(!SG_RuneMechanismBindingCurrent(&binding));
	relay->use = saved_use;
	CHECK(SG_RuneMechanismBindingCurrent(&binding));
	saved_target = relay->target;
	relay->target = "wrong-close-target";
	CHECK(!SG_RuneMechanismBindingCurrent(&binding));
	relay->target = saved_target;
	CHECK(SG_RuneMechanismBindingCurrent(&binding));
	door->use = UnknownUse;
	CHECK(!SG_RuneMechanismBindingCurrent(&binding));
}

static void TestProductionDelayedRelayDispatch(execution_fixture_t *fixture,
	int chain, int link_index, uint32_t trigger_key,
	const char *expected_order, int expected_open_speakers)
{
	sg_rune_mechanism_binding_t binding;
	sg_bot_t *bot = &sg_bots[0];
	int initial_num_edicts = globals.num_edicts;
	size_t expected_count = strlen(expected_order);

	memset(bot, 0, sizeof(*bot));
	bot->active = true;
	bot->ent = &test_edicts[KEY_BOT];
	bot->commit_link = link_index;
	bot->declared_started = true;
	bot->declared_touched = true;
	bot->declared_touch_frame = level.framenum;
	CHECK(SG_RuneMechanismBindingCaptureOwned(&fixture->rune,
	    (uint32_t)link_index, &binding));
	CHECK(SG_HandleMechanismTargets(&test_edicts[trigger_key],
	    &test_edicts[KEY_BOT]));
	CHECK(chain >= 0 && chain < CHAIN_COUNT);
	CHECK(relay_g_use_targets[chain] == 1);
	CHECK(door_g_use_targets[chain] == 1);
	CHECK(door_uses[chain] == 1);
	CHECK(open_speaker_uses[chain] == expected_open_speakers);
	CHECK(close_speaker_uses[chain] == 0);
	CHECK(callback_order_count[chain] == expected_count);
	CHECK(memcmp(callback_order[chain], expected_order, expected_count) == 0);
	CHECK(globals.num_edicts == initial_num_edicts);
	CHECK(!test_edicts[KEY_SPARE].inuse);
	CHECK(test_edicts[KEY_SPARE].classname == NULL);
	CHECK(SG_RuneMechanismBindingCurrent(&binding));
}

static void TestButtonDoor(execution_fixture_t *fixture)
{
	sg_rune_mechanism_binding_t binding;
	dispatch_log_t log;
	uint32_t mover_keys[SG_RUNE_BINDING_MAX_MOVERS];
	size_t mover_count = 0U;
	edict_t *button = &test_edicts[KEY_BUTTON];
	edict_t *door = &test_edicts[KEY_BUTTON_DOOR];

	CHECK(SG_RuneMechanismBindingCapture(&fixture->rune, LINK_BUTTON_DOOR,
	    &binding));
	CHECK(SG_RuneMechanismBindingMoverKeys(&binding, mover_keys,
	    &mover_count));
	CHECK(mover_count == 1U && mover_keys[0] == KEY_BUTTON_DOOR);
	CHECK(mover_keys[0] != KEY_BUTTON);
	button->moveinfo.state = 0;
	button->moveinfo.endfunc = button_wait;
	button->think = Move_Done;
	button->nextthink = 0.0f;
	CHECK(SG_RuneMechanismBindingCaptureOwned(&fixture->rune,
	    LINK_BUTTON_DOOR, &binding));
	memset(&log, 0, sizeof(log));
	log.moving_door = door;
	CHECK(SG_RuneMechanismBindingDispatchTargets(&binding, KEY_BUTTON,
	    DispatchTarget, &log));
	CHECK(log.count == 1U && log.keys[0] == KEY_BUTTON_DOOR);
	CHECK(SG_RuneMechanismBindingCurrent(&binding));
	button->think = button_return;
	button->nextthink = 3.0f;
	CHECK(SG_RuneMechanismBindingCurrent(&binding));
	button->think = UnknownThink;
	CHECK(!SG_RuneMechanismBindingCurrent(&binding));
}

int main(void)
{
	execution_fixture_t fixture;

	FixtureBuild(&fixture);
	TestPublicationGate(&fixture);
	TestPlatform(&fixture);
	TestTeleport(&fixture);
	TestAutoDoor(&fixture);
	TestDirectDoor(&fixture);
	TestButtonDoor(&fixture);
	/* Rebuild the sealed live catalog after the independent drift tests, then
	 * execute the real binding -> SG_Handle -> trigger_relay_use ->
	 * G_UseTargets reentrant path in one composition. */
	FixtureBuild(&fixture);
	TestProductionDelayedRelayDispatch(&fixture, CHAIN_FRONT,
	    LINK_DIRECT_DOOR, KEY_DIRECT_TRIGGER, "RDS", 1);
	TestProductionDelayedRelayDispatch(&fixture, CHAIN_CELLAR,
	    LINK_CELLAR_DOOR, KEY_CELLAR_TRIGGER, "DR", 0);
	TestProductionDelayedRelayDispatch(&fixture, CHAIN_GATE,
	    LINK_GATE_DOOR, KEY_GATE_TRIGGER, "DSR", 1);
	if (failures != 0)
	{
		fprintf(stderr, "%d mechanism execution test(s) failed\n", failures);
		return 1;
	}
	puts("mechanism execution tests passed");
	return 0;
}
