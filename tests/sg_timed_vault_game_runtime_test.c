/* Focused live lifecycle regression for the timed-vault engine bridge. */
#include "g_local.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_bot.h"
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_rune_binding.h"
#include "slipgate/sg_rune_contract.h"
#include "slipgate/sg_rune_mechanism_catalog.h"
#include "slipgate/sg_timed_vault_game.h"
#include "slipgate/sg_timed_vault_game_runtime.h"
#include "slipgate/sg_util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum test_key_e
{
	KEY_BUTTON = 1,
	KEY_SHORT,
	KEY_RESTORE,
	KEY_DOOR_A,
	KEY_DOOR_B,
	KEY_LASER_0,
	KEY_LASER_1,
	KEY_LASER_2,
	KEY_LASER_3,
	KEY_LASER_4,
	KEY_LASER_5,
	KEY_LASER_6,
	KEY_LASER_7,
	KEY_SPEAKER,
	KEY_DELAYED_SHORT,
	KEY_DELAYED_RESTORE,
	KEY_BODY,
	KEY_BOT,
	TEST_EDICTS
};

game_locals_t game;
level_locals_t level;
game_import_t gi;
game_export_t globals;
edict_t *g_edicts;
sg_host_t sg_host;
sg_bot_t sg_bots[SG_MAXBOTS];
cvar_t *maxclients;
cvar_t *sv_gravity;
int meansOfDeath;

static edict_t edicts[TEST_EDICTS];
static gclient_t bot_client;
static rune_t active_rune;
static rune_link_t active_link;
static rune_mechanism_plan_t active_plan;
static rune_mechanism_node_t nodes[5];
static rune_mechanism_edge_t edges[25];
static int failures;
static int binding_current;
static int bot_owned;
static int held_flag;
static int speaker_uses;
static int frees;
static int fail_entry_after_relays;
static int fail_short_after_four;
static int fail_restore_after_four;

#define CHECK(condition_) do { \
	if (!(condition_)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, \
			#condition_); \
		failures++; \
	} \
} while (0)

static void *TestAlloc(int size)
{
	return calloc(1U, (size_t)size);
}

static void TestFree(void *memory)
{
	free(memory);
	frees++;
}

static edict_t *Entity(uint32_t key)
{
	return key < TEST_EDICTS ? &edicts[key] : NULL;
}

static void InitEntity(uint32_t key, const char *classname)
{
	edict_t *entity = Entity(key);

	memset(entity, 0, sizeof(*entity));
	entity->inuse = true;
	entity->s.number = (int)key;
	entity->classname = (char *)classname;
}

static void RelayUse(edict_t *self, edict_t *other, edict_t *activator)
{
	(void)other;
	G_UseTargets(self, activator);
}

static void DoorUse(edict_t *self, edict_t *other, edict_t *activator)
{
	(void)other;
	(void)activator;
	self->moveinfo.state = SG_PLAT_STATE_TOP;
}

static void DeviceUse(edict_t *self, edict_t *other, edict_t *activator)
{
	(void)other;
	(void)activator;
	if (self->s.number == KEY_SPEAKER)
	{
		speaker_uses++;
		self->s.sound = self->s.sound ? 0 : self->noise_index;
	}
	else
		self->spawnflags ^= 1;
}

void Think_Delay(edict_t *entity)
{
	G_UseTargets(entity, entity->activator);
	G_FreeEdict(entity);
}

void G_FreeEdict(edict_t *entity)
{
	if (!entity || !entity->inuse)
		return;
	SG_TimedVaultRuntimeEntityFreed(entity);
	entity->inuse = false;
}

void G_UseTargets(edict_t *source, edict_t *activator)
{
	sg_timed_vault_runtime_target_result_t result =
		SG_TimedVaultRuntimeHandleTargets(source, activator);
	edict_t *delayed;

	if (result == SG_TIMED_VAULT_RUNTIME_HANDLED)
		return;
	if (source->classname && strcmp(source->classname, "DelayedUse") == 0)
	{
		if (source->target && strcmp(source->target, "human-effect") == 0)
			DeviceUse(Entity(KEY_LASER_0), source, activator);
		return;
	}
	if (!source->delay)
		return;
	delayed = result == SG_TIMED_VAULT_RUNTIME_ALLOW_STOCK &&
	    source->s.number == KEY_RESTORE
		? Entity(KEY_DELAYED_RESTORE) : Entity(KEY_DELAYED_SHORT);
	InitEntity((uint32_t)(delayed - edicts), "DelayedUse");
	delayed->think = Think_Delay;
	delayed->activator = activator;
	delayed->target = source->target;
	delayed->message = source->message;
	delayed->killtarget = source->killtarget;
	if (SG_OwnsBot(activator))
		delayed->spawnflags = SG_DELAYED_USE_BOT_ACTIVATOR;
	SG_TimedVaultRuntimeTagDelayedTarget(source, activator, delayed);
}

qboolean SG_OwnsBot(edict_t *entity)
{
	return bot_owned && entity == Entity(KEY_BOT);
}

rune_t *SG_Rune(void)
{
	return &active_rune;
}

qboolean SG_RunePhysicsCompatible(const rune_t *rune)
{
	return rune == &active_rune;
}

sg_compound_guard_result_t SG_DeclaredDoorGuardAuthorizeActivation(
	sg_bot_t *bot, int link_index)
{
	return bot == &sg_bots[0] && link_index == 0
		? SG_COMPOUND_GUARD_OK : SG_COMPOUND_GUARD_INVALID_ARGUMENT;
}

edict_t *ClientHasFlag(edict_t *entity)
{
	return entity == Entity(KEY_BOT) && held_flag ? Entity(KEY_BUTTON) : NULL;
}

int SG_MechCatalogEntityGeneration(const edict_t *entity, uint32_t *key_out,
	uint32_t *generation_out)
{
	if (!entity || !entity->inuse || !key_out || !generation_out)
		return 0;
	*key_out = (uint32_t)entity->s.number;
	*generation_out = (uint32_t)entity->s.number + 100U;
	return 1;
}

int SG_RuneMechanismBindingCaptureOwned(const rune_t *rune,
	uint32_t link_index, sg_rune_mechanism_binding_t *binding)
{
	if (!binding_current || rune != &active_rune || link_index != 0U ||
	    !binding)
		return 0;
	*binding = (sg_rune_mechanism_binding_t){
		.rune = &active_rune,
		.link = &active_link,
		.plan = &active_plan,
		.entry_node = &nodes[0],
		.destination_node = &nodes[1],
		.egress_node = &nodes[2],
		.mover_node = &nodes[3],
		.entry_entity = Entity(KEY_BUTTON),
		.destination_entity = Entity(KEY_SHORT),
		.egress_entity = Entity(KEY_RESTORE),
		.mover_entity = Entity(KEY_DOOR_A),
		.link_index = 0U
	};
	return 1;
}

int SG_RuneMechanismBindingCurrent(
	const sg_rune_mechanism_binding_t *binding)
{
	return binding_current && binding && binding->rune == &active_rune;
}

const rune_mechanism_edge_t *SG_RuneMechanismBindingEdgeAt(
	const sg_rune_mechanism_binding_t *binding, uint32_t ordinal)
{
	return binding && ordinal < active_plan.num_edges ? &edges[ordinal] : NULL;
}

edict_t *SG_RuneMechanismBindingResolveNode(
	const sg_rune_mechanism_binding_t *binding, uint32_t key)
{
	return SG_RuneMechanismBindingCurrent(binding) ? Entity(key) : NULL;
}

int SG_RuneMechanismBindingMoverKeys(
	const sg_rune_mechanism_binding_t *binding,
	uint32_t keys[SG_RUNE_BINDING_MAX_MOVERS], size_t *count_out)
{
	if (!SG_RuneMechanismBindingCurrent(binding) || !keys || !count_out)
		return 0;
	keys[0] = KEY_DOOR_A;
	keys[1] = KEY_DOOR_B;
	*count_out = 2U;
	return 1;
}

static int VisitTargets(const sg_rune_mechanism_binding_t *binding,
	uint32_t source_key, sg_rune_mechanism_target_visitor_fn visitor,
	void *context, const uint32_t *targets, size_t count)
{
	size_t index;

	for (index = 0U; index < count; index++)
	{
		if (!visitor(context, Entity(targets[index]), targets[index],
		        (uint32_t)index))
			return 0;
		if (source_key == KEY_BUTTON && fail_entry_after_relays && index == 1U)
		{
			binding_current = 0;
			return 0;
		}
		if (source_key == KEY_SHORT && fail_short_after_four && index == 3U)
		{
			binding_current = 0;
			return 0;
		}
		if (source_key == KEY_RESTORE && fail_restore_after_four &&
		    index == 3U)
		{
			binding_current = 0;
			return 0;
		}
		if (!SG_RuneMechanismBindingCurrent(binding))
			return 0;
	}
	return 1;
}

int SG_RuneMechanismBindingDispatchTargets(
	const sg_rune_mechanism_binding_t *binding, uint32_t source_key,
	sg_rune_mechanism_target_visitor_fn visitor, void *context)
{
	static const uint32_t button_targets[] = {
		KEY_SHORT, KEY_RESTORE, KEY_DOOR_A, KEY_DOOR_B
	};
	static const uint32_t device_targets[] = {
		KEY_LASER_0, KEY_LASER_1, KEY_LASER_2, KEY_LASER_3,
		KEY_LASER_4, KEY_LASER_5, KEY_LASER_6, KEY_LASER_7,
		KEY_SPEAKER
	};

	if (!SG_RuneMechanismBindingCurrent(binding) || !visitor)
		return 0;
	if (source_key == KEY_BUTTON)
		return VisitTargets(binding, source_key, visitor, context,
		    button_targets, sizeof(button_targets) / sizeof(button_targets[0]));
	if (source_key == KEY_SHORT || source_key == KEY_RESTORE)
		return VisitTargets(binding, source_key, visitor, context,
		    device_targets, sizeof(device_targets) / sizeof(device_targets[0]));
	return 0;
}

static void Edge(uint32_t index, uint32_t from, uint32_t to,
	uint16_t kind, uint16_t ordinal)
{
	edges[index] = (rune_mechanism_edge_t){
		.from_key = from,
		.to_key = to,
		.kind = kind,
		.ordinal = ordinal
	};
}

static void BuildFixture(void)
{
	uint32_t index;

	memset(edicts, 0, sizeof(edicts));
	memset(sg_bots, 0, sizeof(sg_bots));
	memset(&active_rune, 0, sizeof(active_rune));
	memset(&active_link, 0, sizeof(active_link));
	memset(&active_plan, 0, sizeof(active_plan));
	memset(nodes, 0, sizeof(nodes));
	memset(edges, 0, sizeof(edges));
	g_edicts = edicts;
	globals.num_edicts = TEST_EDICTS;
	game.maxentities = TEST_EDICTS;
	game.maxclients = 1;
	sg_host.level_alloc = TestAlloc;
	sg_host.level_free = TestFree;
	binding_current = 1;
	bot_owned = 1;
	held_flag = 0;
	speaker_uses = 0;
	fail_entry_after_relays = 0;
	fail_short_after_four = 0;
	fail_restore_after_four = 0;
	level.framenum = 100;
	level.time = 10.0f;
	active_link.action = RL_BUTTON_DOOR;
	active_link.mode = RLCM_PREOPEN;
	active_plan.entry_key = KEY_BUTTON;
	active_plan.mover_key = KEY_DOOR_A;
	active_plan.first_edge = 0U;
	active_plan.num_edges = 25U;
	active_plan.controller_kind = SG_MECHANISM_CONTROLLER_TIMED_VAULT;
	active_plan.expected_members = 2U;
	active_plan.cooldown_ms = 10000U;
	active_plan.closure_crc32 = UINT32_C(0x12345678);
	nodes[0].key = KEY_BUTTON;
	nodes[1].key = KEY_SHORT;
	nodes[2].key = KEY_RESTORE;
	nodes[3].key = KEY_DOOR_A;
	nodes[4].key = KEY_DOOR_B;
	Edge(0U, KEY_BUTTON, KEY_SHORT, SG_MECH_EDGE_TARGET, 0U);
	Edge(1U, KEY_BUTTON, KEY_RESTORE, SG_MECH_EDGE_TARGET, 1U);
	Edge(2U, KEY_BUTTON, KEY_DOOR_A, SG_MECH_EDGE_TARGET, 2U);
	Edge(3U, KEY_BUTTON, KEY_DOOR_B, SG_MECH_EDGE_TARGET, 3U);
	for (index = 0U; index < 9U; index++)
	{
		Edge(4U + index, KEY_SHORT, KEY_LASER_0 + index,
		    SG_MECH_EDGE_TARGET, (uint16_t)index);
		Edge(13U + index, KEY_RESTORE, KEY_LASER_0 + index,
		    SG_MECH_EDGE_TARGET, (uint16_t)index);
	}
	Edge(22U, KEY_DOOR_A, KEY_DOOR_B, SG_MECH_EDGE_TEAM, 0U);
	Edge(23U, KEY_DOOR_A, KEY_SPEAKER, SG_MECH_EDGE_TARGET, 0U);
	Edge(24U, KEY_DOOR_B, KEY_SPEAKER, SG_MECH_EDGE_TARGET, 0U);

	InitEntity(KEY_BUTTON, "func_button");
	Entity(KEY_BUTTON)->activator = Entity(KEY_BOT);
	InitEntity(KEY_SHORT, "trigger_relay");
	Entity(KEY_SHORT)->delay = 1.0f;
	Entity(KEY_SHORT)->target = "vault-devices";
	Entity(KEY_SHORT)->use = RelayUse;
	InitEntity(KEY_RESTORE, "trigger_relay");
	Entity(KEY_RESTORE)->delay = 10.0f;
	Entity(KEY_RESTORE)->target = "vault-devices";
	Entity(KEY_RESTORE)->use = RelayUse;
	InitEntity(KEY_DOOR_A, "func_door");
	InitEntity(KEY_DOOR_B, "func_door");
	Entity(KEY_DOOR_A)->use = DoorUse;
	Entity(KEY_DOOR_B)->use = DoorUse;
	Entity(KEY_DOOR_A)->moveinfo.state = SG_PLAT_STATE_BOTTOM;
	Entity(KEY_DOOR_B)->moveinfo.state = SG_PLAT_STATE_BOTTOM;
	VectorSet(Entity(KEY_DOOR_A)->absmin, -16.0f, -16.0f, -16.0f);
	VectorSet(Entity(KEY_DOOR_A)->absmax, 16.0f, 16.0f, 16.0f);
	VectorSet(Entity(KEY_DOOR_B)->absmin, 16.0f, -16.0f, -16.0f);
	VectorSet(Entity(KEY_DOOR_B)->absmax, 48.0f, 16.0f, 16.0f);
	for (index = KEY_LASER_0; index <= KEY_LASER_7; index++)
	{
		InitEntity(index, "target_laser");
		Entity(index)->use = DeviceUse;
		Entity(index)->spawnflags = 1;
	}
	InitEntity(KEY_SPEAKER, "target_speaker");
	Entity(KEY_SPEAKER)->use = DeviceUse;
	Entity(KEY_SPEAKER)->noise_index = 1;
	Entity(KEY_SPEAKER)->s.sound = 1;
	InitEntity(KEY_BOT, "player");
	Entity(KEY_BOT)->client = &bot_client;
	Entity(KEY_BOT)->health = 100;
	Entity(KEY_BOT)->solid = SOLID_BBOX;
	VectorSet(Entity(KEY_BOT)->absmin, 100.0f, 100.0f, 100.0f);
	VectorSet(Entity(KEY_BOT)->absmax, 120.0f, 120.0f, 120.0f);
	sg_bots[0].active = true;
	sg_bots[0].ent = Entity(KEY_BOT);
	sg_bots[0].instance_token = 77U;
	sg_bots[0].commit_link = 0;
	sg_bots[0].declared_started = true;
	sg_bots[0].declared_touched = true;
	sg_bots[0].declared_touch_frame = 100;
}

static void TestLifecycle(void)
{
	usercmd_t command;
	uint32_t index;

	BuildFixture();
	level.framenum = 102;
	level.time = 10.2f;
	CHECK(SG_TimedVaultRuntimeHandleTargets(Entity(KEY_BUTTON),
	    Entity(KEY_BOT)) == SG_TIMED_VAULT_RUNTIME_HANDLED);
	CHECK(Entity(KEY_DELAYED_SHORT)->sg_timed_vault_live != NULL);
	CHECK(Entity(KEY_DELAYED_RESTORE)->sg_timed_vault_live != NULL);
	memset(&command, 0, sizeof(command));
	command.forwardmove = command.sidemove = command.upmove = 200;
	CHECK(SG_TimedVaultRuntimeApplyCommand(Entity(KEY_BOT), &command));
	CHECK(command.forwardmove == 0 && command.sidemove == 0 &&
	    command.upmove == 0);

	level.framenum = 112;
	level.time = 11.2f;
	CHECK(SG_TimedVaultRuntimeHandleTargets(Entity(KEY_DELAYED_SHORT),
	    Entity(KEY_BOT)) == SG_TIMED_VAULT_RUNTIME_HANDLED);
	for (index = KEY_LASER_0; index <= KEY_LASER_7; index++)
		CHECK((Entity(index)->spawnflags & 1) == 0);
	CHECK(speaker_uses == 1);
	CHECK(SG_TimedVaultRuntimeCommandFor(Entity(KEY_BOT)) ==
	    SG_TIMED_VAULT_COMMAND_ENTER);
	SG_TimedVaultRuntimeEntityFreed(Entity(KEY_DELAYED_SHORT));
	Entity(KEY_DELAYED_SHORT)->inuse = false;

	held_flag = 1;
	level.framenum = 113;
	level.time = 11.3f;
	memset(&command, 0, sizeof(command));
	command.forwardmove = 300;
	CHECK(!SG_TimedVaultRuntimeApplyCommand(Entity(KEY_BOT), &command));
	CHECK(command.forwardmove == 300);
	CHECK(SG_TimedVaultRuntimeCommandFor(Entity(KEY_BOT)) ==
	    SG_TIMED_VAULT_COMMAND_EGRESS);
	level.framenum = 114;
	level.time = 11.4f;
	command.forwardmove = 300;
	CHECK(!SG_TimedVaultRuntimeApplyCommand(Entity(KEY_BOT), &command));
	CHECK(command.forwardmove == 300);

	SG_TimedVaultRuntimeRetireActivator(Entity(KEY_DELAYED_RESTORE),
	    Entity(KEY_BOT));
	bot_owned = 0;
	Entity(KEY_BOT)->inuse = false;
	CHECK(SG_TimedVaultRuntimeDelayedUseDurable(
	    Entity(KEY_DELAYED_RESTORE)));
	CHECK(Entity(KEY_DELAYED_RESTORE)->activator == NULL);

	InitEntity(KEY_BODY, "body");
	Entity(KEY_BODY)->solid = SOLID_BBOX;
	VectorSet(Entity(KEY_BODY)->absmin, -4.0f, -4.0f, -4.0f);
	VectorSet(Entity(KEY_BODY)->absmax, 4.0f, 4.0f, 4.0f);
	level.framenum = 202;
	level.time = 20.2f;
	CHECK(SG_TimedVaultRuntimeHandleTargets(Entity(KEY_DELAYED_RESTORE),
	    NULL) == SG_TIMED_VAULT_RUNTIME_HANDLED);
	for (index = KEY_LASER_0; index <= KEY_LASER_7; index++)
		CHECK((Entity(index)->spawnflags & 1) != 0);
	CHECK(speaker_uses == 2);
	CHECK(SG_TimedVaultRuntimeDelayedUseDeferred(
	    Entity(KEY_DELAYED_RESTORE)));

	Entity(KEY_BODY)->inuse = false;
	level.framenum = 203;
	level.time = 20.3f;
	CHECK(SG_TimedVaultRuntimeHandleTargets(Entity(KEY_DELAYED_RESTORE),
	    NULL) == SG_TIMED_VAULT_RUNTIME_HANDLED);
	CHECK(SG_TimedVaultRuntimeDelayedUseDeferred(
	    Entity(KEY_DELAYED_RESTORE)));
	Entity(KEY_DOOR_A)->moveinfo.state = SG_PLAT_STATE_BOTTOM;
	Entity(KEY_DOOR_B)->moveinfo.state = SG_PLAT_STATE_BOTTOM;
	level.framenum = 204;
	level.time = 20.4f;
	CHECK(SG_TimedVaultRuntimeHandleTargets(Entity(KEY_DELAYED_RESTORE),
	    NULL) == SG_TIMED_VAULT_RUNTIME_HANDLED);
	CHECK(!SG_TimedVaultRuntimeDelayedUseDeferred(
	    Entity(KEY_DELAYED_RESTORE)));
	CHECK(speaker_uses == 2);
	SG_TimedVaultRuntimeEntityFreed(Entity(KEY_DELAYED_RESTORE));
	Entity(KEY_DELAYED_RESTORE)->inuse = false;
}

static void TestHumanBypassAndFailedEntryRetainsRestore(void)
{
	BuildFixture();
	CHECK(SG_TimedVaultRuntimeHandleTargets(Entity(KEY_BUTTON), NULL) ==
	    SG_TIMED_VAULT_RUNTIME_NOT_OWNED);
	InitEntity(KEY_BODY, "trigger_relay");
	Entity(KEY_BODY)->delay = 1.0f;
	Entity(KEY_BODY)->target = "human-effect";
	bot_owned = 0;
	G_UseTargets(Entity(KEY_BODY), Entity(KEY_BOT));
	CHECK(Entity(KEY_DELAYED_SHORT)->inuse);
	CHECK(Entity(KEY_DELAYED_SHORT)->sg_timed_vault_live == NULL);
	CHECK((Entity(KEY_DELAYED_SHORT)->spawnflags &
	    SG_DELAYED_USE_BOT_ACTIVATOR) == 0);
	Think_Delay(Entity(KEY_DELAYED_SHORT));
	CHECK(!Entity(KEY_DELAYED_SHORT)->inuse);
	CHECK((Entity(KEY_LASER_0)->spawnflags & 1) == 0);

	BuildFixture();
	fail_entry_after_relays = 1;
	level.framenum = 102;
	level.time = 10.2f;
	CHECK(SG_TimedVaultRuntimeHandleTargets(Entity(KEY_BUTTON),
	    Entity(KEY_BOT)) == SG_TIMED_VAULT_RUNTIME_HANDLED);
	CHECK(!Entity(KEY_DELAYED_SHORT)->inuse);
	CHECK(Entity(KEY_DELAYED_RESTORE)->inuse);
	CHECK(SG_TimedVaultRuntimeDelayedUseDurable(
	    Entity(KEY_DELAYED_RESTORE)));
	binding_current = 1;
	level.framenum = 202;
	level.time = 20.2f;
	CHECK(SG_TimedVaultRuntimeHandleTargets(Entity(KEY_DELAYED_RESTORE),
	    Entity(KEY_BOT)) == SG_TIMED_VAULT_RUNTIME_HANDLED);
	CHECK(!SG_TimedVaultRuntimeDelayedUseDeferred(
	    Entity(KEY_DELAYED_RESTORE)));
	CHECK(speaker_uses == 0);
	SG_TimedVaultRuntimeEntityFreed(Entity(KEY_DELAYED_RESTORE));
	Entity(KEY_DELAYED_RESTORE)->inuse = false;
}

static void TestPartialShortFanoutNormalizesRestore(void)
{
	usercmd_t command;
	uint32_t index;

	BuildFixture();
	level.framenum = 102;
	level.time = 10.2f;
	CHECK(SG_TimedVaultRuntimeHandleTargets(Entity(KEY_BUTTON),
	    Entity(KEY_BOT)) == SG_TIMED_VAULT_RUNTIME_HANDLED);
	fail_short_after_four = 1;
	level.framenum = 112;
	level.time = 11.2f;
	CHECK(SG_TimedVaultRuntimeHandleTargets(Entity(KEY_DELAYED_SHORT),
	    Entity(KEY_BOT)) == SG_TIMED_VAULT_RUNTIME_HANDLED);
	CHECK((Entity(KEY_LASER_0)->spawnflags & 1) == 0);
	CHECK((Entity(KEY_LASER_3)->spawnflags & 1) == 0);
	CHECK((Entity(KEY_LASER_4)->spawnflags & 1) != 0);
	memset(&command, 0, sizeof(command));
	command.forwardmove = 300;
	CHECK(SG_TimedVaultRuntimeApplyCommand(Entity(KEY_BOT), &command));
	CHECK(command.forwardmove == 0);
	CHECK(SG_TimedVaultRuntimeCommandFor(Entity(KEY_BOT)) ==
	    SG_TIMED_VAULT_COMMAND_HOLD);
	SG_TimedVaultRuntimeEntityFreed(Entity(KEY_DELAYED_SHORT));
	Entity(KEY_DELAYED_SHORT)->inuse = false;
	binding_current = 1;
	Entity(KEY_DOOR_A)->moveinfo.state = SG_PLAT_STATE_BOTTOM;
	Entity(KEY_DOOR_B)->moveinfo.state = SG_PLAT_STATE_BOTTOM;
	level.framenum = 202;
	level.time = 20.2f;
	CHECK(SG_TimedVaultRuntimeHandleTargets(Entity(KEY_DELAYED_RESTORE),
	    Entity(KEY_BOT)) == SG_TIMED_VAULT_RUNTIME_HANDLED);
	for (index = KEY_LASER_0; index <= KEY_LASER_7; index++)
		CHECK((Entity(index)->spawnflags & 1) != 0);
	CHECK(Entity(KEY_SPEAKER)->s.sound != 0);
	SG_TimedVaultRuntimeEntityFreed(Entity(KEY_DELAYED_RESTORE));
	Entity(KEY_DELAYED_RESTORE)->inuse = false;
}

static void OpenVaultAndRunShort(void)
{
	level.framenum = 102;
	level.time = 10.2f;
	CHECK(SG_TimedVaultRuntimeHandleTargets(Entity(KEY_BUTTON),
	    Entity(KEY_BOT)) == SG_TIMED_VAULT_RUNTIME_HANDLED);
	level.framenum = 112;
	level.time = 11.2f;
	CHECK(SG_TimedVaultRuntimeHandleTargets(Entity(KEY_DELAYED_SHORT),
	    Entity(KEY_BOT)) == SG_TIMED_VAULT_RUNTIME_HANDLED);
	SG_TimedVaultRuntimeEntityFreed(Entity(KEY_DELAYED_SHORT));
	Entity(KEY_DELAYED_SHORT)->inuse = false;
	Entity(KEY_DOOR_A)->moveinfo.state = SG_PLAT_STATE_BOTTOM;
	Entity(KEY_DOOR_B)->moveinfo.state = SG_PLAT_STATE_BOTTOM;
}

static void TestRestoreFailurePersistenceAndRetry(void)
{
	uint32_t index;

	BuildFixture();
	OpenVaultAndRunShort();
	binding_current = 0;
	level.framenum = 202;
	level.time = 20.2f;
	CHECK(SG_TimedVaultRuntimeHandleTargets(Entity(KEY_DELAYED_RESTORE),
	    Entity(KEY_BOT)) == SG_TIMED_VAULT_RUNTIME_HANDLED);
	CHECK(SG_TimedVaultRuntimeDelayedUseDeferred(
	    Entity(KEY_DELAYED_RESTORE)));
	CHECK((Entity(KEY_LASER_0)->spawnflags & 1) == 0);
	binding_current = 1;
	level.framenum = 203;
	level.time = 20.3f;
	CHECK(SG_TimedVaultRuntimeHandleTargets(Entity(KEY_DELAYED_RESTORE),
	    Entity(KEY_BOT)) == SG_TIMED_VAULT_RUNTIME_HANDLED);
	CHECK(!SG_TimedVaultRuntimeDelayedUseDeferred(
	    Entity(KEY_DELAYED_RESTORE)));
	for (index = KEY_LASER_0; index <= KEY_LASER_7; index++)
		CHECK((Entity(index)->spawnflags & 1) != 0);
	SG_TimedVaultRuntimeEntityFreed(Entity(KEY_DELAYED_RESTORE));
	Entity(KEY_DELAYED_RESTORE)->inuse = false;

	BuildFixture();
	OpenVaultAndRunShort();
	Entity(KEY_DELAYED_RESTORE)->sg_delayed_source_generation++;
	level.framenum = 202;
	level.time = 20.2f;
	CHECK(SG_TimedVaultRuntimeHandleTargets(Entity(KEY_DELAYED_RESTORE),
	    Entity(KEY_BOT)) == SG_TIMED_VAULT_RUNTIME_HANDLED);
	CHECK(SG_TimedVaultRuntimeDelayedUseDeferred(
	    Entity(KEY_DELAYED_RESTORE)));
	level.framenum = 203;
	level.time = 20.3f;
	CHECK(SG_TimedVaultRuntimeHandleTargets(Entity(KEY_DELAYED_RESTORE),
	    Entity(KEY_BOT)) == SG_TIMED_VAULT_RUNTIME_HANDLED);
	CHECK(SG_TimedVaultRuntimeDelayedUseDeferred(
	    Entity(KEY_DELAYED_RESTORE)));
	CHECK((Entity(KEY_LASER_0)->spawnflags & 1) == 0);
	SG_TimedVaultRuntimeEntityFreed(Entity(KEY_DELAYED_RESTORE));
	Entity(KEY_DELAYED_RESTORE)->inuse = false;

	BuildFixture();
	OpenVaultAndRunShort();
	fail_restore_after_four = 1;
	level.framenum = 202;
	level.time = 20.2f;
	CHECK(SG_TimedVaultRuntimeHandleTargets(Entity(KEY_DELAYED_RESTORE),
	    Entity(KEY_BOT)) == SG_TIMED_VAULT_RUNTIME_HANDLED);
	CHECK(SG_TimedVaultRuntimeDelayedUseDeferred(
	    Entity(KEY_DELAYED_RESTORE)));
	CHECK((Entity(KEY_LASER_0)->spawnflags & 1) != 0);
	CHECK((Entity(KEY_LASER_4)->spawnflags & 1) == 0);
	fail_restore_after_four = 0;
	binding_current = 1;
	level.framenum = 203;
	level.time = 20.3f;
	CHECK(SG_TimedVaultRuntimeHandleTargets(Entity(KEY_DELAYED_RESTORE),
	    Entity(KEY_BOT)) == SG_TIMED_VAULT_RUNTIME_HANDLED);
	CHECK(!SG_TimedVaultRuntimeDelayedUseDeferred(
	    Entity(KEY_DELAYED_RESTORE)));
	for (index = KEY_LASER_0; index <= KEY_LASER_7; index++)
		CHECK((Entity(index)->spawnflags & 1) != 0);
	SG_TimedVaultRuntimeEntityFreed(Entity(KEY_DELAYED_RESTORE));
	Entity(KEY_DELAYED_RESTORE)->inuse = false;
}

int main(void)
{
	TestLifecycle();
	TestHumanBypassAndFailedEntryRetainsRestore();
	TestPartialShortFanoutNormalizesRestore();
	TestRestoreFailurePersistenceAndRetry();
	if (failures)
	{
		fprintf(stderr, "%d failure(s)\n", failures);
		return 1;
	}
	printf("sg_timed_vault_game_runtime_test: ok (frees=%d)\n", frees);
	return 0;
}
