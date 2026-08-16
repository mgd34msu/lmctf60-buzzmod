/* Host fixture for adapter-owned edict incarnations and fail-closed seams. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "g_local.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_bot.h"
#include "slipgate/sg_compound_guard_game.h"

#define TEST_EDICTS 16

game_export_t globals;
game_locals_t game;
edict_t *g_edicts;
sg_bot_t sg_bots[SG_MAXBOTS];

static edict_t entities[TEST_EDICTS];
static gclient_t clients[2];
static sg_compound_guard_host_t captured_host;
static int failures;
static int level_resets;
static int body_will_key;
static int body_did_key;
static int orphan_bolt_key;
static int orphan_calls;
static int disconnected_calls;
static int quarantine_calls;
static int bot_attach_calls;
static int bot_respawn_calls;
static int bot_reset_calls;
static int last_respawn_key;
static uint64_t last_respawn_generation;
static int sweep_outside = 1;
static int sweep_inside_key;
static int hold_member_calls;
static int hold_member_ok = 1;
static int terminal_member_calls;
static int terminal_member_ok = 1;
static int body_die_calls;
static int body_die_clears = 1;
static sg_compound_guard_result_t respawn_result = SG_COMPOUND_GUARD_OK;
static sg_compound_guard_result_t validate_result =
    SG_COMPOUND_GUARD_NO_LEASE;
static sg_mover_lease_record_t validate_record;
static sg_compound_guard_run_t run_state = SG_COMPOUND_GUARD_RUN_READY;
static int global_record_present;
static sg_mover_lease_record_t global_record;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		        __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

void hook_touch(edict_t *self, edict_t *other, cplane_t *plane,
	csurface_t *surf)
{
	(void)self;
	(void)other;
	(void)plane;
	(void)surf;
}

void hook_die(edict_t *self, edict_t *inflictor, edict_t *attacker,
	int damage, vec3_t point)
{
	(void)self;
	(void)inflictor;
	(void)attacker;
	(void)damage;
	(void)point;
}

void body_die(edict_t *self, edict_t *inflictor, edict_t *attacker,
	int damage, vec3_t point)
{
	(void)inflictor;
	(void)attacker;
	(void)damage;
	(void)point;
	body_die_calls++;
	if (self && self->health < -40 && body_die_clears)
	{
		self->solid = SOLID_NOT;
		self->takedamage = DAMAGE_NO;
	}
}

int SG_MoverSubjectValid(const sg_mover_subject_t *subject)
{
	return subject && subject->kind >= SG_MOVER_SUBJECT_CLIENT &&
	       subject->kind <= SG_MOVER_SUBJECT_HOOK_BOLT &&
	       subject->edict_key > 0 && subject->generation != 0U;
}

qboolean SG_MoverSubjectOutsideSweep(edict_t *member, edict_t *subject)
{
	return member && subject && member->inuse && subject->inuse &&
	       sweep_outside && subject->s.number != sweep_inside_key;
}

qboolean SG_DeclaredDoorHoldMembers(edict_t *const *members, int count,
	int lease_ms)
{
	int index;

	hold_member_calls++;
	if (!members || count <= 0 || lease_ms <= 0 || !hold_member_ok)
		return false;
	for (index = 0; index < count; index++)
		if (!members[index] || !members[index]->inuse)
			return false;
	return true;
}

qboolean SG_DeclaredDoorMembersTerminal(edict_t *const *members, int count)
{
	int index;

	terminal_member_calls++;
	if (!members || count <= 0 || !terminal_member_ok)
		return false;
	for (index = 0; index < count; index++)
		if (!members[index] || !members[index]->inuse)
			return false;
	return true;
}

sg_compound_guard_result_t SG_CompoundGuardInit(
	const sg_compound_guard_host_t *host)
{
	CHECK(host != NULL);
	if (!host)
		return SG_COMPOUND_GUARD_INVALID_ARGUMENT;
	captured_host = *host;
	return SG_COMPOUND_GUARD_OK;
}

sg_compound_guard_result_t SG_CompoundGuardLevelReset(void)
{
	level_resets++;
	return SG_COMPOUND_GUARD_OK;
}

sg_compound_guard_result_t SG_CompoundGuardBotReset(
	sg_compound_guard_bot_t *bot)
{
	if (!bot)
		return SG_COMPOUND_GUARD_INVALID_ARGUMENT;
	bot_reset_calls++;
	memset(bot, 0, sizeof(*bot));
	return SG_COMPOUND_GUARD_OK;
}

sg_compound_guard_result_t SG_CompoundGuardBotAttach(
	sg_compound_guard_bot_t *bot, int32_t slot, int32_t client_key)
{
	uint64_t generation = 0U;

	if (!bot || slot < 0 ||
	    captured_host.identity(captured_host.context, client_key,
	    &generation) != SG_COMPOUND_GUARD_YES)
		return SG_COMPOUND_GUARD_HOST_ERROR;
	bot_attach_calls++;
	memset(bot, 0, sizeof(*bot));
	bot->attached = 1U;
	bot->client.kind = SG_MOVER_SUBJECT_CLIENT;
	bot->client.edict_key = client_key;
	bot->client.generation = generation;
	return SG_COMPOUND_GUARD_OK;
}

sg_compound_guard_result_t SG_CompoundGuardBotRespawn(
	sg_compound_guard_bot_t *bot, int32_t client_key)
{
	uint64_t generation = 0U;

	bot_respawn_calls++;
	last_respawn_key = client_key;
	if (captured_host.identity(captured_host.context, client_key,
	    &generation) == SG_COMPOUND_GUARD_YES)
		last_respawn_generation = generation;
	if (!bot || !bot->attached)
		return SG_COMPOUND_GUARD_NOT_ATTACHED;
	if (respawn_result == SG_COMPOUND_GUARD_OK)
	{
		bot->client.edict_key = client_key;
		bot->client.generation = generation;
	}
	return respawn_result;
}

int SG_CompoundGuardRecordAt(size_t slot,
	sg_mover_lease_record_t *record_out, sg_mover_ticket_t *ticket_out)
{
	if (record_out)
		memset(record_out, 0, sizeof(*record_out));
	if (ticket_out)
		memset(ticket_out, 0, sizeof(*ticket_out));
	if (slot != 0U || !global_record_present)
		return 0;
	if (record_out)
		*record_out = global_record;
	return 1;
}

sg_compound_guard_result_t SG_CompoundGuardBodyWillReplace(int32_t body_key)
{
	body_will_key = body_key;
	return SG_COMPOUND_GUARD_OK;
}

sg_compound_guard_result_t SG_CompoundGuardBodyDidCopy(
	sg_compound_guard_bot_t *bot, int32_t body_key)
{
	(void)bot;
	body_did_key = body_key;
	return SG_COMPOUND_GUARD_OK;
}

sg_compound_guard_result_t SG_CompoundGuardOrphan(
	sg_compound_guard_bot_t *bot, int32_t bolt_key)
{
	CHECK(bot != NULL && bot->attached);
	orphan_calls++;
	orphan_bolt_key = bolt_key;
	return bolt_key < 0 ? SG_COMPOUND_GUARD_INVALID_ARGUMENT
	                    : SG_COMPOUND_GUARD_OK;
}

sg_compound_guard_result_t SG_CompoundGuardValidate(
	sg_compound_guard_bot_t *bot, sg_mover_lease_record_t *record_out)
{
	(void)bot;
	if (record_out)
		*record_out = validate_record;
	return validate_result;
}

sg_compound_guard_run_t SG_CompoundGuardBotRunState(
	const sg_compound_guard_bot_t *bot)
{
	(void)bot;
	return run_state;
}

sg_compound_guard_result_t SG_CompoundGuardBoltEvicted(
	sg_compound_guard_bot_t *bot, int32_t bolt_key)
{
	(void)bot;
	(void)bolt_key;
	return SG_COMPOUND_GUARD_OK;
}

sg_compound_guard_result_t SG_CompoundGuardBotDisconnected(
	sg_compound_guard_bot_t *bot)
{
	CHECK(bot != NULL && bot->attached);
	disconnected_calls++;
	return SG_COMPOUND_GUARD_OK;
}

sg_compound_guard_result_t SG_CompoundGuardQuarantine(
	sg_compound_guard_bot_t *bot)
{
	(void)bot;
	quarantine_calls++;
	return SG_COMPOUND_GUARD_OK;
}

void SG_CompoundGuardFrame(sg_compound_guard_frame_stats_t *stats)
{
	if (stats)
		memset(stats, 0, sizeof(*stats));
}

static void LiveEntity(int key, const char *classname)
{
	edict_t *entity = &entities[key];

	memset(entity, 0, sizeof(*entity));
	entity->inuse = true;
	entity->s.number = key;
	entity->classname = (char *)classname;
}

static void ResetWorld(void)
{
	memset(entities, 0, sizeof(entities));
	memset(clients, 0, sizeof(clients));
	memset(sg_bots, 0, sizeof(sg_bots));
	memset(&globals, 0, sizeof(globals));
	memset(&game, 0, sizeof(game));
	memset(&captured_host, 0, sizeof(captured_host));
	memset(&validate_record, 0, sizeof(validate_record));
	memset(&global_record, 0, sizeof(global_record));
	global_record_present = 0;
	hold_member_calls = 0;
	hold_member_ok = 1;
	terminal_member_calls = 0;
	terminal_member_ok = 1;
	body_die_calls = 0;
	body_die_clears = 1;
	validate_result = SG_COMPOUND_GUARD_NO_LEASE;
	run_state = SG_COMPOUND_GUARD_RUN_READY;
	sweep_inside_key = 0;
	g_edicts = entities;
	game.maxentities = TEST_EDICTS;
	game.maxclients = 2;
	game.clients = clients;
	globals.edicts = entities;
	globals.edict_size = sizeof(edict_t);
	globals.num_edicts = TEST_EDICTS;
	globals.max_edicts = TEST_EDICTS;
}

static uint64_t CurrentGeneration(int key)
{
	uint64_t generation = 0U;

	CHECK(captured_host.identity(captured_host.context, key, &generation) ==
	      SG_COMPOUND_GUARD_YES);
	return generation;
}

static void TestIdentityABAAndBounds(void)
{
	edict_t foreign;
	uint64_t first_generation, second_generation, ignored = 0U;

	LiveEntity(1, "player");
	entities[1].client = &clients[0];
	entities[1].solid = SOLID_BBOX;
	CHECK(SG_CompoundGuardGameClientSpawned(&entities[1]) ==
	      SG_COMPOUND_GUARD_OK);
	first_generation = CurrentGeneration(1);
	CHECK(SG_CompoundGuardGameBotAttach(&sg_bots[0].compound_guard, 0,
	      &entities[1]) == SG_COMPOUND_GUARD_OK);
	CHECK(bot_attach_calls == 1 && sg_bots[0].compound_guard.attached == 1U);
	CHECK(sg_bots[0].compound_guard.client.edict_key == 1 &&
	      sg_bots[0].compound_guard.client.generation == first_generation);
	sg_bots[0].active = true;
	sg_bots[0].ent = &entities[1];

	SG_CompoundGuardGameTestSetEntityGeneration(1, 0U, 1);
	CHECK(captured_host.identity(captured_host.context, 1, &ignored) ==
	      SG_COMPOUND_GUARD_OBSERVATION_ERROR);
	SG_CompoundGuardGameTestSetEntityGeneration(1, first_generation, 1);
	CHECK(CurrentGeneration(1) == first_generation);

	foreign = entities[1];
	CHECK(SG_CompoundGuardGameClientSpawned(&foreign) ==
	      SG_COMPOUND_GUARD_INVALID_ARGUMENT);
	CHECK(SG_CompoundGuardGameClientSpawned((edict_t *)(uintptr_t)1U) ==
	      SG_COMPOUND_GUARD_INVALID_ARGUMENT);
	SG_CompoundGuardGameEntityFreed(&foreign);
	CHECK(CurrentGeneration(1) == first_generation);
	entities[1].s.number = 7;
	CHECK(captured_host.identity(captured_host.context, 1, &ignored) ==
	      SG_COMPOUND_GUARD_OBSERVATION_ERROR);
	CHECK(SG_CompoundGuardGameClientSpawned(&entities[1]) ==
	      SG_COMPOUND_GUARD_INVALID_ARGUMENT);
	entities[1].s.number = 1;

	LiveEntity(2, "player");
	entities[2].client = &clients[0];
	entities[2].solid = SOLID_BBOX;
	CHECK(SG_CompoundGuardGameClientSpawned(&entities[2]) ==
	      SG_COMPOUND_GUARD_INVALID_ARGUMENT);

	entities[1].inuse = false;
	SG_CompoundGuardGameEntityFreed(&entities[1]);
	CHECK(captured_host.identity(captured_host.context, 1, &ignored) ==
	      SG_COMPOUND_GUARD_NO);
	LiveEntity(1, "player");
	entities[1].client = &clients[0];
	entities[1].solid = SOLID_BBOX;
	CHECK(SG_CompoundGuardGameClientSpawned(&entities[1]) ==
	      SG_COMPOUND_GUARD_OK);
	second_generation = CurrentGeneration(1);
	CHECK(second_generation > first_generation);
	CHECK(bot_respawn_calls == 1 && last_respawn_key == 1 &&
	      last_respawn_generation == second_generation);
	CHECK(sg_bots[0].compound_guard.client.generation == second_generation);
}

static void TestBodyAndHookIncarnations(void)
{
	edict_t foreign_body;
	int quarantines_before;
	int body_calls_before;
	sg_mover_key_t mover_key = 10U;
	sg_mover_subject_t subject;
	uint64_t body_first, body_second, hook_generation;

	LiveEntity(3, "bodyque");
	entities[3].solid = SOLID_BBOX;
	CHECK(SG_CompoundGuardGameBodyQueueInit(&entities[3]) ==
	      SG_COMPOUND_GUARD_OK);
	body_first = CurrentGeneration(3);
	CHECK(SG_CompoundGuardGameBodyWillReplace(&entities[3]) ==
	      SG_COMPOUND_GUARD_OK);
	CHECK(body_will_key == 3);
	CHECK(SG_CompoundGuardGameBodyDidCopy(&entities[1], &entities[3]) ==
	      SG_COMPOUND_GUARD_OK);
	entities[3].movetype = MOVETYPE_TOSS;
	entities[3].die = body_die;
	entities[3].takedamage = DAMAGE_YES;
	CHECK(body_did_key == 3);
	body_second = CurrentGeneration(3);
	CHECK(body_second > body_first);
	entities[3].s.number = 4;
	CHECK(captured_host.identity(captured_host.context, 3, &hook_generation) ==
	      SG_COMPOUND_GUARD_OBSERVATION_ERROR);
	CHECK(SG_CompoundGuardGameBodyQueueInit(&entities[3]) ==
	      SG_COMPOUND_GUARD_INVALID_ARGUMENT);
	entities[3].s.number = 3;
	LiveEntity(13, "bodyque");
	entities[13].solid = SOLID_BBOX;
	CHECK(SG_CompoundGuardGameBodyQueueInit(&entities[13]) ==
	      SG_COMPOUND_GUARD_INVALID_ARGUMENT);

	LiveEntity(11, "noclass");
	entities[11].owner = &entities[1];
	entities[11].movetype = MOVETYPE_FLYMISSILE;
	entities[11].solid = SOLID_BBOX;
	entities[11].touch = hook_touch;
	entities[11].die = hook_die;
	CHECK(SG_CompoundGuardGameHookLinked(&entities[1], &entities[11]) ==
	      SG_COMPOUND_GUARD_OK);
	hook_generation = CurrentGeneration(11);
	LiveEntity(10, "func_door");
	/* Record-wide clearance covers each bot-derived physical kind in isolation,
	 * not only the lease owner's current client edict. */
	sweep_outside = 1;
	sweep_inside_key = 1;
	CHECK(captured_host.all_subjects_outside(captured_host.context,
	      &mover_key, 1U) == SG_COMPOUND_GUARD_NO);
	/* A later non-body blocker makes the whole preflight negative without
	 * partially terminalizing an earlier body-queue candidate. */
	entities[1].solid = SOLID_NOT;
	sweep_outside = 0;
	sweep_inside_key = 0;
	CHECK(captured_host.all_subjects_outside(captured_host.context,
	      &mover_key, 1U) == SG_COMPOUND_GUARD_NO);
	CHECK(body_die_calls == 0 && entities[3].solid == SOLID_BBOX);
	entities[1].solid = SOLID_BBOX;
	sweep_outside = 1;
	sweep_inside_key = 3;
	CHECK(captured_host.all_subjects_outside(captured_host.context,
	      &mover_key, 1U) == SG_COMPOUND_GUARD_YES);
	CHECK(body_die_calls == 1 && entities[3].solid == SOLID_NOT &&
	      entities[3].takedamage == DAMAGE_NO);
	CHECK(captured_host.all_subjects_outside(captured_host.context,
	      &mover_key, 1U) == SG_COMPOUND_GUARD_YES);
	CHECK(body_die_calls == 1);
	/* A malformed or ineffective corpse terminal callback cannot turn an
	 * occupied sweep into a positive proof. */
	entities[3].solid = SOLID_BBOX;
	entities[3].takedamage = DAMAGE_YES;
	entities[3].die = NULL;
	CHECK(captured_host.all_subjects_outside(captured_host.context,
	      &mover_key, 1U) == SG_COMPOUND_GUARD_OBSERVATION_ERROR);
	CHECK(body_die_calls == 1);
	entities[3].die = body_die;
	body_die_clears = 0;
	CHECK(captured_host.all_subjects_outside(captured_host.context,
	      &mover_key, 1U) == SG_COMPOUND_GUARD_OBSERVATION_ERROR);
	CHECK(body_die_calls == 2 && entities[3].solid == SOLID_BBOX);
	body_die_clears = 1;
	entities[3].solid = SOLID_NOT;
	entities[3].takedamage = DAMAGE_NO;
	sweep_inside_key = 11;
	CHECK(captured_host.all_subjects_outside(captured_host.context,
	      &mover_key, 1U) == SG_COMPOUND_GUARD_NO);
	/* Human client/body/hook generations are observable but intentionally do
	 * not become SG-protected subjects. */
	LiveEntity(2, "player");
	entities[2].client = &clients[1];
	entities[2].solid = SOLID_BBOX;
	CHECK(SG_CompoundGuardGameClientSpawned(&entities[2]) ==
	      SG_COMPOUND_GUARD_OK);
	LiveEntity(4, "bodyque");
	entities[4].solid = SOLID_BBOX;
	CHECK(SG_CompoundGuardGameBodyQueueInit(&entities[4]) ==
	      SG_COMPOUND_GUARD_OK);
	CHECK(SG_CompoundGuardGameBodyWillReplace(&entities[4]) ==
	      SG_COMPOUND_GUARD_OK);
	CHECK(SG_CompoundGuardGameBodyDidCopy(&entities[2], &entities[4]) ==
	      SG_COMPOUND_GUARD_OK);
	entities[4].movetype = MOVETYPE_TOSS;
	entities[4].die = body_die;
	entities[4].takedamage = DAMAGE_YES;
	LiveEntity(14, "noclass");
	entities[14].owner = &entities[2];
	entities[14].movetype = MOVETYPE_FLYMISSILE;
	entities[14].solid = SOLID_BBOX;
	entities[14].touch = hook_touch;
	entities[14].die = hook_die;
	CHECK(SG_CompoundGuardGameHookLinked(&entities[2], &entities[14]) ==
	      SG_COMPOUND_GUARD_OK);
	sweep_inside_key = 2;
	CHECK(captured_host.all_subjects_outside(captured_host.context,
	      &mover_key, 1U) == SG_COMPOUND_GUARD_YES);
	sweep_inside_key = 4;
	body_calls_before = body_die_calls;
	CHECK(captured_host.all_subjects_outside(captured_host.context,
	      &mover_key, 1U) == SG_COMPOUND_GUARD_YES);
	CHECK(body_die_calls == body_calls_before &&
	      entities[4].solid == SOLID_BBOX);
	sweep_inside_key = 14;
	CHECK(captured_host.all_subjects_outside(captured_host.context,
	      &mover_key, 1U) == SG_COMPOUND_GUARD_YES);
	sweep_inside_key = 0;
	CHECK(captured_host.hold_open(captured_host.context,
	      SG_MOVER_LAW_DECLARED_DOOR, &mover_key, 1U, 500) ==
	      SG_COMPOUND_GUARD_YES);
	CHECK(hold_member_calls == 1);
	CHECK(captured_host.set_terminal(captured_host.context,
	      SG_MOVER_LAW_DECLARED_DOOR, &mover_key, 1U) ==
	      SG_COMPOUND_GUARD_YES);
	CHECK(terminal_member_calls == 1);
	memset(&subject, 0, sizeof(subject));
	subject.kind = SG_MOVER_SUBJECT_HOOK_BOLT;
	subject.edict_key = 11;
	subject.generation = hook_generation;
	CHECK(captured_host.solid(captured_host.context, &subject) ==
	      SG_COMPOUND_GUARD_YES);

	sg_bots[0].active = true;
	sg_bots[0].ent = &entities[1];
	sg_bots[0].compound_guard.attached = 1U;
	quarantines_before = quarantine_calls;
	respawn_result = SG_COMPOUND_GUARD_HOST_ERROR;
	CHECK(SG_CompoundGuardGameClientSpawned(&entities[1]) ==
	      SG_COMPOUND_GUARD_HOST_ERROR);
	CHECK(quarantine_calls == quarantines_before + 1);
	respawn_result = SG_COMPOUND_GUARD_OK;
	entities[1].client->hook = &entities[11];
	entities[1].client->hookstate = 1;
	entities[1].health = 25;
	validate_result = SG_COMPOUND_GUARD_OK;
	validate_record.law = SG_MOVER_LAW_DECLARED_DOOR;
	validate_record.state = SG_MOVER_LEASE_ACTIVE;
	CHECK(SG_CompoundGuardGamePlayerDie(&entities[1]) ==
	      SG_COMPOUND_GUARD_OK);
	CHECK(orphan_calls == 1 && orphan_bolt_key == 11);
	CHECK(entities[1].health == -100);
	/* A later life can still carry the parked owner while its earlier corpse is
	 * an ORPHAN.  Re-entering the death hook must not attempt ORPHAN->ORPHAN or
	 * quarantine the valid record.  A retirement/foreign-set terminal result
	 * still forces this new body through the nonsolid stock gib path. */
	validate_record.state = SG_MOVER_LEASE_ORPHAN;
	entities[1].health = 25;
	run_state = SG_COMPOUND_GUARD_RUN_READY;
	quarantines_before = quarantine_calls;
	CHECK(SG_CompoundGuardGamePlayerDie(&entities[1]) ==
	      SG_COMPOUND_GUARD_OK);
	CHECK(orphan_calls == 1 && quarantine_calls == quarantines_before);
	CHECK(entities[1].health == 25);
	run_state = SG_COMPOUND_GUARD_RUN_TERMINAL;
	CHECK(SG_CompoundGuardGamePlayerDie(&entities[1]) ==
	      SG_COMPOUND_GUARD_OK);
	CHECK(orphan_calls == 1 && quarantine_calls == quarantines_before);
	CHECK(entities[1].health == -100);
	run_state = SG_COMPOUND_GUARD_RUN_READY;
	validate_result = SG_COMPOUND_GUARD_NO_LEASE;
	memset(&validate_record, 0, sizeof(validate_record));
	/* A bot with no claim of its own is still made nonsolid when its dying
	 * body occupies another live captured set. */
	global_record_present = 1;
	global_record.state = SG_MOVER_LEASE_ACTIVE;
	global_record.law = SG_MOVER_LAW_DECLARED_DOOR;
	global_record.key_count = 1U;
	global_record.keys[0] = 10U;
	sweep_outside = 0;
	entities[1].health = 25;
	CHECK(SG_CompoundGuardGamePlayerDie(&entities[1]) ==
	      SG_COMPOUND_GUARD_OK);
	CHECK(entities[1].health == -100);
	global_record_present = 0;
	sweep_outside = 1;

	LiveEntity(12, "noclass");
	entities[12].owner = &entities[1];
	entities[12].movetype = MOVETYPE_FLYMISSILE;
	entities[12].solid = SOLID_BBOX;
	entities[12].touch = hook_touch;
	entities[12].die = hook_die;
	CHECK(SG_CompoundGuardGameHookLinked(&entities[1], &entities[12]) ==
	      SG_COMPOUND_GUARD_OK);
	entities[1].health = 25;
	quarantines_before = quarantine_calls;
	CHECK(SG_CompoundGuardGamePlayerDie(&entities[1]) ==
	      SG_COMPOUND_GUARD_INVALID_ARGUMENT);
	CHECK(orphan_calls == 3 && orphan_bolt_key == -1);
	CHECK(quarantine_calls == quarantines_before + 1);
	CHECK(entities[1].health == 25);

	LiveEntity(4, "noclass");
	entities[4].owner = &entities[1];
	entities[4].movetype = MOVETYPE_FLYMISSILE;
	entities[4].solid = SOLID_BBOX;
	entities[4].touch = hook_touch;
	entities[4].die = hook_die;
	quarantines_before = quarantine_calls;
	CHECK(SG_CompoundGuardGameHookLinked(&entities[1], &entities[4]) ==
	      SG_COMPOUND_GUARD_INVALID_ARGUMENT);
	CHECK(quarantine_calls == quarantines_before + 1);

	LiveEntity(13, "noclass");
	entities[13].owner = &entities[1];
	entities[13].solid = SOLID_BBOX;
	entities[13].touch = hook_touch;
	entities[13].die = hook_die;
	quarantines_before = quarantine_calls;
	CHECK(SG_CompoundGuardGameHookLinked(&entities[1], &entities[13]) ==
	      SG_COMPOUND_GUARD_INVALID_ARGUMENT);
	CHECK(quarantine_calls == quarantines_before + 1);
	entities[13].movetype = MOVETYPE_FLYMISSILE;
	entities[13].classname = "not-a-hook";
	quarantines_before = quarantine_calls;
	CHECK(SG_CompoundGuardGameHookLinked(&entities[1], &entities[13]) ==
	      SG_COMPOUND_GUARD_INVALID_ARGUMENT);
	CHECK(quarantine_calls == quarantines_before + 1);
	entities[13].classname = "noclass";
	entities[13].s.number = 14;
	quarantines_before = quarantine_calls;
	CHECK(SG_CompoundGuardGameHookLinked(&entities[1], &entities[13]) ==
	      SG_COMPOUND_GUARD_INVALID_ARGUMENT);
	CHECK(quarantine_calls == quarantines_before + 1);
	entities[13].s.number = 13;
	entities[13].owner = &entities[2];
	quarantines_before = quarantine_calls;
	CHECK(SG_CompoundGuardGameHookLinked(&entities[1], &entities[13]) ==
	      SG_COMPOUND_GUARD_INVALID_ARGUMENT);
	CHECK(quarantine_calls == quarantines_before + 1);
	entities[13].owner = &entities[1];

	foreign_body = entities[3];
	quarantines_before = quarantine_calls;
	CHECK(SG_CompoundGuardGameBodyDidCopy(&entities[1], &foreign_body) ==
	      SG_COMPOUND_GUARD_INVALID_ARGUMENT);
	CHECK(quarantine_calls == quarantines_before + 1);
	quarantines_before = quarantine_calls;
	CHECK(SG_CompoundGuardGameBoltEvicted(&entities[1],
	      (edict_t *)(uintptr_t)1U) == SG_COMPOUND_GUARD_INVALID_ARGUMENT);
	CHECK(quarantine_calls == quarantines_before + 1);

	entities[11].inuse = false;
	SG_CompoundGuardGameEntityFreed(&entities[11]);
	CHECK(captured_host.identity(captured_host.context, 11, &hook_generation) ==
	      SG_COMPOUND_GUARD_NO);
}

static void TestDisconnectAndExhaustion(void)
{
	int resets_before, level_resets_before;
	int slot;
	uint64_t generation = 0U;

	entities[1].solid = SOLID_NOT;
	entities[1].inuse = false;
	CHECK(SG_CompoundGuardGameClientDisconnected(&entities[1]) ==
	      SG_COMPOUND_GUARD_OK);
	CHECK(disconnected_calls == 1);
	CHECK(captured_host.identity(captured_host.context, 1, &generation) ==
	      SG_COMPOUND_GUARD_NO);

	for (slot = 0; slot < SG_MAXBOTS; slot++)
		sg_bots[slot].compound_guard.attached = 1U;
	resets_before = bot_reset_calls;
	level_resets_before = level_resets;
	SG_CompoundGuardGameStorageWillFree();
	CHECK(level_resets == level_resets_before + 1);
	CHECK(bot_reset_calls == resets_before + SG_MAXBOTS);
	for (slot = 0; slot < SG_MAXBOTS; slot++)
		CHECK(sg_bots[slot].compound_guard.attached == 0U);
	CHECK(captured_host.identity(captured_host.context, 3, &generation) ==
	      SG_COMPOUND_GUARD_NO);

	SG_CompoundGuardGameTestSetNextGeneration(UINT64_MAX);
	LiveEntity(2, "player");
	entities[2].client = &clients[1];
	entities[2].solid = SOLID_BBOX;
	CHECK(SG_CompoundGuardGameClientSpawned(&entities[2]) ==
	      SG_COMPOUND_GUARD_EXHAUSTED);
	CHECK(captured_host.identity(captured_host.context, 3, &generation) ==
	      SG_COMPOUND_GUARD_OBSERVATION_ERROR);
	CHECK(SG_CompoundGuardGameLevelReset() == SG_COMPOUND_GUARD_OK);
	CHECK(captured_host.identity(captured_host.context, 3, &generation) ==
	      SG_COMPOUND_GUARD_OBSERVATION_ERROR);
	CHECK(SG_CompoundGuardGameClientSpawned(&entities[2]) ==
	      SG_COMPOUND_GUARD_EXHAUSTED);
}

int main(void)
{
	ResetWorld();
	CHECK(SG_CompoundGuardGameLevelReset() == SG_COMPOUND_GUARD_OK);
	CHECK(level_resets == 1);
	CHECK(captured_host.identity != NULL && captured_host.solid != NULL &&
	      captured_host.outside_sweep != NULL &&
	      captured_host.all_subjects_outside != NULL &&
	      captured_host.hold_open != NULL &&
	      captured_host.set_terminal != NULL);
	TestIdentityABAAndBounds();
	TestBodyAndHookIncarnations();
	TestDisconnectAndExhaustion();
	if (failures)
	{
		fprintf(stderr, "sg_compound_guard_game_test: %d failure(s)\n",
		        failures);
		return 1;
	}
	puts("sg_compound_guard_game_test: ok");
	return 0;
}
