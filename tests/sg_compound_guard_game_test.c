/* Host fixture for adapter-owned edict incarnations and fail-closed seams. */
#include <float.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "g_local.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_bot.h"
#include "slipgate/sg_compound_world.h"
#include "slipgate/sg_compound_guard_game.h"
#include "slipgate/sg_compound_hook_game.h"
#include "slipgate/sg_rune_mechanism_catalog.h"

#define TEST_EDICTS 24

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
static int action_orphan_calls;
static int action_orphan_bolt_key;
static sg_compound_guard_result_t action_orphan_result =
    SG_COMPOUND_GUARD_OK;
static int hook_action_orphan_calls;
static sg_compound_guard_result_t hook_action_orphan_result =
    SG_COMPOUND_GUARD_OK;
static int disconnected_calls;
static int swim_retired_calls;
static int quarantine_calls;
static int bot_attach_calls;
static int bot_respawn_calls;
static int bot_reset_calls;
static int last_respawn_key;
static uint64_t last_respawn_generation;
static int sweep_outside = 1;
static int sweep_inside_key;
static int prospective_outside = 1;
static int prospective_inside_key;
static int prospective_calls;
static int prospective_pusher_valid = 1;
static int prospective_pusher_valid_calls;
static sg_compound_guard_result_t pusher_fence_result =
    SG_COMPOUND_GUARD_NO_LEASE;
static sg_mover_key_t pusher_fence_overlap_key;
static int pusher_fence_calls;
static sg_mover_key_t pusher_fence_keys[SG_MOVER_LEASE_MAX_KEYS];
static size_t pusher_fence_key_count;
static sg_compound_guard_result_t any_door_result =
    SG_COMPOUND_GUARD_NO_LEASE;
static int any_door_calls;
static int hold_member_calls;
static int hold_member_ok = 1;
static int terminal_member_calls;
static int terminal_member_ok = 1;
static int compound_hold_calls;
static int compound_hold_ok = 1;
static edict_t *compound_hold_member;
static int compound_hold_lease_ms;
static int compound_terminal_calls;
static int compound_terminal_ok = 1;
static edict_t *compound_terminal_member;
static edict_t *compound_expected_member;
static int body_die_calls;
static int body_die_clears = 1;
static int completion_reset_calls;
static int completion_forget_calls;
static int completion_transition_calls;
static edict_t *completion_forgot;
static sg_compound_guard_result_t respawn_result = SG_COMPOUND_GUARD_OK;
static sg_compound_guard_result_t validate_result =
    SG_COMPOUND_GUARD_NO_LEASE;
static sg_mover_lease_record_t validate_record;
static sg_compound_guard_run_t run_state = SG_COMPOUND_GUARD_RUN_READY;
static int global_record_present;
static sg_mover_lease_record_t global_record;

rune_t *SG_Rune(void)
{
	return NULL;
}

qboolean SG_RunePublishedShapeValid(const rune_t *rune)
{
	(void)rune;
	return false;
}

const rune_mechanism_node_t *SG_RuneMechanismNodeByKey(
	const rune_t *rune, uint32_t key)
{
	(void)rune;
	(void)key;
	return NULL;
}

int SG_MechCatalogEntityExecutionMatches(uint32_t key,
	const rune_mechanism_node_t *node, uint16_t controller_kind)
{
	(void)key;
	(void)node;
	(void)controller_kind;
	return 0;
}

edict_t *SG_MechCatalogResolveEntity(uint32_t key,
	const rune_mechanism_node_t *node)
{
	(void)key;
	(void)node;
	return NULL;
}

void SG_CompoundSwimGameClientRetired(edict_t *client)
{
	if (client != &entities[1])
		failures++;
	swim_retired_calls++;
}

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
	       subject->edict_key > 0 && subject->generation != 0U &&
	       subject->reserved[0] == 0U && subject->reserved[1] == 0U &&
	       subject->reserved[2] == 0U;
}

qboolean SG_MoverSubjectOutsideSweep(edict_t *member, edict_t *subject)
{
	return member && subject && member->inuse && subject->inuse &&
	       sweep_outside && subject->s.number != sweep_inside_key;
}

qboolean SG_MoverSubjectOutsideProspectivePush(edict_t *member,
	edict_t *subject)
{
	prospective_calls++;
	return member && subject && member->inuse && subject->inuse &&
	       prospective_outside && subject->s.number != prospective_inside_key;
}

qboolean SG_MoverProspectivePusherValid(edict_t *member)
{
	prospective_pusher_valid_calls++;
	return prospective_pusher_valid && member && member->inuse &&
	       !member->prethink;
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

int SG_CompoundWorldHoldMember(edict_t *member, int lease_ms)
{
	compound_hold_calls++;
	compound_hold_member = member;
	compound_hold_lease_ms = lease_ms;
	return compound_hold_ok && lease_ms == 500 && member && member->inuse &&
	       (!compound_expected_member || member == compound_expected_member);
}

int SG_CompoundWorldMemberTerminal(edict_t *member)
{
	compound_terminal_calls++;
	compound_terminal_member = member;
	return compound_terminal_ok && member && member->inuse &&
	       (!compound_expected_member || member == compound_expected_member);
}

void SG_MoverCompletionReset(void)
{
	completion_reset_calls++;
}

void SG_MoverCompletionForget(edict_t *member)
{
	completion_forget_calls++;
	completion_forgot = member;
}

void SG_MoverCompletionTransition(edict_t *member)
{
	CHECK(member != NULL);
	completion_transition_calls++;
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

sg_compound_guard_result_t SG_CompoundGuardDoorPusherFence(
	const sg_mover_key_t *keys, size_t key_count)
{
	size_t index;
	int overlaps = pusher_fence_overlap_key == 0U;

	pusher_fence_calls++;
	pusher_fence_key_count = key_count;
	memset(pusher_fence_keys, 0, sizeof(pusher_fence_keys));
	if (keys && key_count <= SG_MOVER_LEASE_MAX_KEYS)
		memcpy(pusher_fence_keys, keys,
		    key_count * sizeof(pusher_fence_keys[0]));
	for (index = 0U; keys && index < key_count; index++)
		if (keys[index] == pusher_fence_overlap_key)
			overlaps = 1;
	if (pusher_fence_result == SG_COMPOUND_GUARD_OK && !overlaps)
		return SG_COMPOUND_GUARD_NO_LEASE;
	return pusher_fence_result;
}

sg_compound_guard_result_t SG_CompoundGuardAnyDoorTransaction(void)
{
	any_door_calls++;
	return any_door_result;
}

sg_compound_guard_result_t SG_CompoundGuardTrainGatePusherFence(
	const sg_mover_key_t *keys, size_t key_count)
{
	(void)keys;
	(void)key_count;
	return SG_COMPOUND_GUARD_NO_LEASE;
}

int SG_MechCatalogTrainGatePose(uint32_t key,
	sg_mech_train_gate_pose_t *pose_out)
{
	(void)key;
	if (pose_out)
		*pose_out = SG_MECH_TRAIN_GATE_INVALID;
	return 0;
}

int SG_MechCatalogTrainGateSweep(uint32_t key, float mins_out[3],
	float maxs_out[3])
{
	(void)key;
	(void)mins_out;
	(void)maxs_out;
	return 0;
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

sg_compound_guard_result_t SG_CompoundDropGameOrphan(sg_bot_t *bot,
	int bolt_key)
{
	sg_compound_guard_result_t result;

	action_orphan_calls++;
	action_orphan_bolt_key = bolt_key;
	if (action_orphan_result != SG_COMPOUND_GUARD_OK)
		return action_orphan_result;
	result = SG_CompoundGuardOrphan(&bot->compound_guard, bolt_key);
	if (result == SG_COMPOUND_GUARD_OK)
	{
		bot->compound_drop_live.guard_owned = false;
		validate_result = SG_COMPOUND_GUARD_OK;
		validate_record.law = SG_MOVER_LAW_COMPOUND_PREOPEN;
		validate_record.state = SG_MOVER_LEASE_ORPHAN;
	}
	return result;
}

qboolean SG_CompoundHookGameHost(sg_bot_t *bot,
	sg_compound_hook_live_host_t *host)
{
	if (!bot || !host)
		return false;
	memset(host, 0, sizeof(*host));
	host->context = bot;
	return true;
}

void SG_CompoundHookGameDebugResult(sg_bot_t *bot, const char *stage,
	const sg_compound_hook_live_result_t *result)
{
	(void)bot;
	(void)stage;
	(void)result;
}

sg_compound_hook_live_result_t SG_CompoundHookLiveOrphan(
	sg_compound_hook_live_state_t *state,
	const sg_compound_hook_live_host_t *host)
{
	sg_compound_hook_live_result_t result;
	sg_compound_guard_result_t orphan_result;
	sg_bot_t *bot;
	int bolt_key;

	memset(&result, 0, sizeof(result));
	hook_action_orphan_calls++;
	if (hook_action_orphan_result != SG_COMPOUND_GUARD_OK)
	{
		result.outcome = SG_COMPOUND_HOOK_LIVE_RECOVERING;
		return result;
	}
	bot = host ? host->context : NULL;
	if (!state || !bot || state != &bot->compound_hook_live)
	{
		result.outcome = SG_COMPOUND_HOOK_LIVE_REJECTED;
		return result;
	}
	bolt_key = state->bolt_linked ? state->bolt.key : 0;
	orphan_result = SG_CompoundGuardOrphan(&bot->compound_guard, bolt_key);
	if (orphan_result == SG_COMPOUND_GUARD_OK)
	{
		state->guard_owned = false;
		state->local_owned = false;
		validate_result = SG_COMPOUND_GUARD_OK;
		validate_record.law = SG_MOVER_LAW_COMPOUND_PREOPEN;
		validate_record.state = SG_MOVER_LEASE_ORPHAN;
		result.outcome = SG_COMPOUND_HOOK_LIVE_SAFE_STOPPED;
		return result;
	}
	result.outcome = SG_COMPOUND_HOOK_LIVE_RECOVERING;
	return result;
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

static void LiveHook(int key, edict_t *owner)
{
	LiveEntity(key, "noclass");
	entities[key].owner = owner;
	entities[key].movetype = MOVETYPE_FLYMISSILE;
	entities[key].solid = SOLID_BBOX;
	entities[key].touch = hook_touch;
	entities[key].die = hook_die;
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
	swim_retired_calls = 0;
	hold_member_calls = 0;
	hold_member_ok = 1;
	terminal_member_calls = 0;
	terminal_member_ok = 1;
	compound_hold_calls = 0;
	compound_hold_ok = 1;
	compound_hold_member = NULL;
	compound_hold_lease_ms = 0;
	compound_terminal_calls = 0;
	compound_terminal_ok = 1;
	compound_terminal_member = NULL;
	compound_expected_member = NULL;
	body_die_calls = 0;
	body_die_clears = 1;
	completion_reset_calls = 0;
	completion_forget_calls = 0;
	completion_transition_calls = 0;
	completion_forgot = NULL;
	validate_result = SG_COMPOUND_GUARD_NO_LEASE;
	run_state = SG_COMPOUND_GUARD_RUN_READY;
	sweep_inside_key = 0;
	prospective_outside = 1;
	prospective_inside_key = 0;
	prospective_calls = 0;
	prospective_pusher_valid = 1;
	prospective_pusher_valid_calls = 0;
	pusher_fence_result = SG_COMPOUND_GUARD_NO_LEASE;
	pusher_fence_overlap_key = 0U;
	pusher_fence_calls = 0;
	pusher_fence_key_count = 0U;
	memset(pusher_fence_keys, 0, sizeof(pusher_fence_keys));
	any_door_result = SG_COMPOUND_GUARD_NO_LEASE;
	any_door_calls = 0;
	g_edicts = entities;
	game.maxentities = TEST_EDICTS;
	game.maxclients = 2;
	game.clients = clients;
	globals.edicts = entities;
	globals.edict_size = sizeof(edict_t);
	globals.num_edicts = TEST_EDICTS;
	globals.max_edicts = TEST_EDICTS;
}

static void CheckCompoundMalformedKeys(const sg_mover_key_t *keys,
	size_t key_count)
{
	int hold_before = compound_hold_calls;
	int terminal_before = compound_terminal_calls;

	CHECK(captured_host.hold_open(captured_host.context,
	      SG_MOVER_LAW_COMPOUND_PREOPEN, keys, key_count, 500) ==
	      SG_COMPOUND_GUARD_OBSERVATION_ERROR);
	CHECK(captured_host.set_terminal(captured_host.context,
	      SG_MOVER_LAW_COMPOUND_PREOPEN, keys, key_count) ==
	      SG_COMPOUND_GUARD_OBSERVATION_ERROR);
	CHECK(compound_hold_calls == hold_before);
	CHECK(compound_terminal_calls == terminal_before);
}

static void TestCompoundLifecycleDispatch(void)
{
	sg_mover_key_t singleton[] = { 10U };
	sg_mover_key_t wrong[] = { 11U };
	sg_mover_key_t multiple[] = { 10U, 12U };
	sg_mover_key_t zero[] = { 0U };
	sg_mover_key_t out_of_range[] = { TEST_EDICTS };
	sg_mover_key_t duplicate[] = { 10U, 10U };
	sg_mover_key_t reversed[] = { 12U, 10U };
	int hold_before;
	int terminal_before;

	LiveEntity(10, "func_door");
	LiveEntity(11, "not-a-door");
	LiveEntity(12, "func_door");
	compound_expected_member = &entities[10];

	CHECK(captured_host.hold_open(captured_host.context,
	      SG_MOVER_LAW_COMPOUND_PREOPEN, singleton, 1U, 500) ==
	      SG_COMPOUND_GUARD_YES);
	CHECK(compound_hold_calls == 1 &&
	      compound_hold_member == &entities[10] &&
	      compound_hold_lease_ms == 500);
	CHECK(hold_member_calls == 0);
	CHECK(captured_host.set_terminal(captured_host.context,
	      SG_MOVER_LAW_COMPOUND_PREOPEN, singleton, 1U) ==
	      SG_COMPOUND_GUARD_YES);
	CHECK(compound_terminal_calls == 1 &&
	      compound_terminal_member == &entities[10]);
	CHECK(terminal_member_calls == 0);

	/* The adapter preserves the world helper's negative observation. */
	compound_hold_ok = 0;
	CHECK(captured_host.hold_open(captured_host.context,
	      SG_MOVER_LAW_COMPOUND_PREOPEN, singleton, 1U, 500) ==
	      SG_COMPOUND_GUARD_NO);
	compound_hold_ok = 1;
	compound_terminal_ok = 0;
	CHECK(captured_host.set_terminal(captured_host.context,
	      SG_MOVER_LAW_COMPOUND_PREOPEN, singleton, 1U) ==
	      SG_COMPOUND_GUARD_NO);
	compound_terminal_ok = 1;

	/* A valid but wrong physical member is dispatched exactly and rejected by
	 * the member-level revalidation, never redirected to ordinary-door code. */
	hold_before = compound_hold_calls;
	terminal_before = compound_terminal_calls;
	CHECK(captured_host.hold_open(captured_host.context,
	      SG_MOVER_LAW_COMPOUND_PREOPEN, wrong, 1U, 500) ==
	      SG_COMPOUND_GUARD_NO);
	CHECK(captured_host.set_terminal(captured_host.context,
	      SG_MOVER_LAW_COMPOUND_PREOPEN, wrong, 1U) ==
	      SG_COMPOUND_GUARD_NO);
	CHECK(compound_hold_calls == hold_before + 1 &&
	      compound_hold_member == &entities[11]);
	CHECK(compound_terminal_calls == terminal_before + 1 &&
	      compound_terminal_member == &entities[11]);
	CHECK(hold_member_calls == 0 && terminal_member_calls == 0);

	/* COMPOUND_PREOPEN owns exactly one leaf.  A well-formed multi-key set is
	 * a negative observation and must not partially dispatch either member. */
	hold_before = compound_hold_calls;
	terminal_before = compound_terminal_calls;
	CHECK(captured_host.hold_open(captured_host.context,
	      SG_MOVER_LAW_COMPOUND_PREOPEN, multiple, 2U, 500) ==
	      SG_COMPOUND_GUARD_NO);
	CHECK(captured_host.set_terminal(captured_host.context,
	      SG_MOVER_LAW_COMPOUND_PREOPEN, multiple, 2U) ==
	      SG_COMPOUND_GUARD_NO);
	CHECK(compound_hold_calls == hold_before);
	CHECK(compound_terminal_calls == terminal_before);

	CheckCompoundMalformedKeys(NULL, 1U);
	CheckCompoundMalformedKeys(singleton, 0U);
	CheckCompoundMalformedKeys(zero, 1U);
	CheckCompoundMalformedKeys(out_of_range, 1U);
	CheckCompoundMalformedKeys(duplicate, 2U);
	CheckCompoundMalformedKeys(reversed, 2U);
	CheckCompoundMalformedKeys(singleton,
	    (size_t)SG_MOVER_LEASE_MAX_KEYS + 1U);

	/* An unknown law cannot fall through into either lifecycle backend. */
	hold_before = compound_hold_calls;
	terminal_before = compound_terminal_calls;
	CHECK(captured_host.hold_open(captured_host.context,
	      SG_MOVER_LAW_NONE, singleton, 1U, 500) ==
	      SG_COMPOUND_GUARD_OBSERVATION_ERROR);
	CHECK(captured_host.set_terminal(captured_host.context,
	      SG_MOVER_LAW_NONE, singleton, 1U) ==
	      SG_COMPOUND_GUARD_OBSERVATION_ERROR);
	CHECK(compound_hold_calls == hold_before &&
	      compound_terminal_calls == terminal_before);
	CHECK(hold_member_calls == 0 && terminal_member_calls == 0);
	compound_expected_member = NULL;
}

static void TestShootDoorAuthorityUnavailable(void)
{
	sg_mover_key_t members[] = { 10U, 12U };
	int hold_before = hold_member_calls;
	int terminal_before = terminal_member_calls;

	CHECK(captured_host.hold_open(captured_host.context,
	      SG_MOVER_LAW_TRAIN_GATE, members, 2U, 500) ==
	      SG_COMPOUND_GUARD_NO);
	CHECK(captured_host.set_terminal(captured_host.context,
	      SG_MOVER_LAW_TRAIN_GATE, members, 2U) ==
	      SG_COMPOUND_GUARD_NO);
	CHECK(hold_member_calls == hold_before);
	CHECK(terminal_member_calls == terminal_before);
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
	CHECK(completion_forgot == &foreign);
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
	CHECK(completion_forgot == &entities[1]);
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
	CHECK(SG_CompoundGuardGameHookLinked(&entities[1], &entities[11], NULL) ==
	      SG_COMPOUND_GUARD_OK);
	hook_generation = CurrentGeneration(11);
	LiveEntity(10, "func_door");
	entities[10].teammaster = &entities[10];
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
	/* The pre-pusher path reuses the same typed, two-phase cleanup, but asks
	 * only about the next exact engine push. */
	pusher_fence_result = SG_COMPOUND_GUARD_OK;
	entities[1].solid = SOLID_NOT;
	entities[1].movetype = MOVETYPE_WALK;
	entities[1].area.prev = &entities[0].area;
	prospective_outside = 1;
	CHECK(!SG_CompoundGuardGamePusherMayAdvance(&entities[10]));
	entities[1].area.prev = NULL;
	CHECK(SG_CompoundGuardGamePusherMayAdvance(&entities[10]));
	entities[1].movetype = MOVETYPE_NONE;
	prospective_outside = 0;
	body_calls_before = body_die_calls;
	CHECK(!SG_CompoundGuardGamePusherMayAdvance(&entities[10]));
	CHECK(body_die_calls == body_calls_before &&
	      entities[3].solid == SOLID_BBOX);
	entities[1].solid = SOLID_BBOX;
	prospective_outside = 1;
	prospective_inside_key = 3;
	CHECK(SG_CompoundGuardGamePusherMayAdvance(&entities[10]));
	CHECK(body_die_calls == 3 && entities[3].solid == SOLID_NOT &&
	      entities[3].takedamage == DAMAGE_NO);
	prospective_inside_key = 11;
	CHECK(!SG_CompoundGuardGamePusherMayAdvance(&entities[10]));
	prospective_inside_key = 0;
	pusher_fence_result = SG_COMPOUND_GUARD_NO_LEASE;
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
	CHECK(SG_CompoundGuardGameHookLinked(&entities[2], &entities[14], NULL) ==
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
	CHECK(swim_retired_calls == 1);
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
	CHECK(SG_CompoundGuardGameHookLinked(&entities[1], &entities[12], NULL) ==
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
	CHECK(SG_CompoundGuardGameHookLinked(&entities[1], &entities[4], NULL) ==
	      SG_COMPOUND_GUARD_INVALID_ARGUMENT);
	CHECK(quarantine_calls == quarantines_before + 1);

	LiveEntity(13, "noclass");
	entities[13].owner = &entities[1];
	entities[13].solid = SOLID_BBOX;
	entities[13].touch = hook_touch;
	entities[13].die = hook_die;
	quarantines_before = quarantine_calls;
	CHECK(SG_CompoundGuardGameHookLinked(&entities[1], &entities[13], NULL) ==
	      SG_COMPOUND_GUARD_INVALID_ARGUMENT);
	CHECK(quarantine_calls == quarantines_before + 1);
	entities[13].movetype = MOVETYPE_FLYMISSILE;
	entities[13].classname = "not-a-hook";
	quarantines_before = quarantine_calls;
	CHECK(SG_CompoundGuardGameHookLinked(&entities[1], &entities[13], NULL) ==
	      SG_COMPOUND_GUARD_INVALID_ARGUMENT);
	CHECK(quarantine_calls == quarantines_before + 1);
	entities[13].classname = "noclass";
	entities[13].s.number = 14;
	quarantines_before = quarantine_calls;
	CHECK(SG_CompoundGuardGameHookLinked(&entities[1], &entities[13], NULL) ==
	      SG_COMPOUND_GUARD_INVALID_ARGUMENT);
	CHECK(quarantine_calls == quarantines_before + 1);
	entities[13].s.number = 13;
	entities[13].owner = &entities[2];
	quarantines_before = quarantine_calls;
	CHECK(SG_CompoundGuardGameHookLinked(&entities[1], &entities[13], NULL) ==
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

static void TestHookSubjectObservation(void)
{
	edict_t *current = (edict_t *)(uintptr_t)1U;
	sg_mover_subject_t first, second, wrong;

	CHECK(SG_CompoundGuardGameHookAbsent(&entities[1]) ==
	      SG_COMPOUND_GUARD_YES);
	entities[1].client->hookstate = 1;
	CHECK(SG_CompoundGuardGameHookAbsent(&entities[1]) ==
	      SG_COMPOUND_GUARD_OBSERVATION_ERROR);
	entities[1].client->hookstate = 0;
	LiveHook(11, &entities[1]);
	memset(&first, 0xa5, sizeof(first));
	CHECK(SG_CompoundGuardGameHookLinked(&entities[1], &entities[11],
	      &first) == SG_COMPOUND_GUARD_OK);
	CHECK(first.kind == SG_MOVER_SUBJECT_HOOK_BOLT &&
	      first.edict_key == 11 && first.generation != 0U &&
	      first.reserved[0] == 0U && first.reserved[1] == 0U &&
	      first.reserved[2] == 0U);
	entities[1].client->hook = &entities[11];
	entities[1].client->hookstate = 1;
	CHECK(SG_CompoundGuardGameHookObserve(&entities[1], &first, &current) ==
	      SG_COMPOUND_GUARD_YES);
	CHECK(current == &entities[11]);
	CHECK(SG_CompoundGuardGameHookAbsent(&entities[1]) ==
	      SG_COMPOUND_GUARD_NO);
	entities[1].client->hookstate = 0;
	CHECK(SG_CompoundGuardGameHookAbsent(&entities[1]) ==
	      SG_COMPOUND_GUARD_NO);
	entities[1].client->hookstate = 1;
	entities[1].client->hook = NULL;
	CHECK(SG_CompoundGuardGameHookAbsent(&entities[1]) ==
	      SG_COMPOUND_GUARD_OBSERVATION_ERROR);
	entities[1].client->hook = &entities[11];

	wrong = first;
	wrong.generation++;
	current = (edict_t *)(uintptr_t)1U;
	CHECK(SG_CompoundGuardGameHookObserve(&entities[1], &wrong, &current) ==
	      SG_COMPOUND_GUARD_OBSERVATION_ERROR);
	CHECK(current == NULL);
	wrong = first;
	wrong.reserved[0] = 1U;
	CHECK(SG_CompoundGuardGameHookObserve(&entities[1], &wrong, &current) ==
	      SG_COMPOUND_GUARD_OBSERVATION_ERROR);
	CHECK(SG_CompoundGuardGameHookObserve((edict_t *)(uintptr_t)1U, &first,
	      &current) == SG_COMPOUND_GUARD_OBSERVATION_ERROR);
	CHECK(SG_CompoundGuardGameHookObserve(&entities[1], &first, NULL) ==
	      SG_COMPOUND_GUARD_OBSERVATION_ERROR);
	entities[1].inuse = false;
	CHECK(SG_CompoundGuardGameHookObserve(&entities[1], &first, &current) ==
	      SG_COMPOUND_GUARD_OBSERVATION_ERROR);
	entities[1].inuse = true;

	entities[11].touch = NULL;
	CHECK(SG_CompoundGuardGameHookObserve(&entities[1], &first, &current) ==
	      SG_COMPOUND_GUARD_OBSERVATION_ERROR);
	CHECK(SG_CompoundGuardGameHookAbsent(&entities[1]) ==
	      SG_COMPOUND_GUARD_OBSERVATION_ERROR);
	entities[11].touch = hook_touch;
	entities[11].owner = &entities[2];
	CHECK(SG_CompoundGuardGameHookObserve(&entities[1], &first, &current) ==
	      SG_COMPOUND_GUARD_OBSERVATION_ERROR);
	entities[11].owner = &entities[1];
	LiveHook(12, &entities[1]);
	CHECK(SG_CompoundGuardGameHookObserve(&entities[1], &first, &current) ==
	      SG_COMPOUND_GUARD_OBSERVATION_ERROR);
	CHECK(SG_CompoundGuardGameHookAbsent(&entities[1]) ==
	      SG_COMPOUND_GUARD_OBSERVATION_ERROR);
	memset(&entities[12], 0, sizeof(entities[12]));

	entities[11].inuse = false;
	SG_CompoundGuardGameEntityFreed(&entities[11]);
	CHECK(SG_CompoundGuardGameHookObserve(&entities[1], &first, &current) ==
	      SG_COMPOUND_GUARD_NO);
	CHECK(current == NULL);
	CHECK(SG_CompoundGuardGameHookAbsent(&entities[1]) ==
	      SG_COMPOUND_GUARD_OBSERVATION_ERROR);
	entities[1].client->hook = NULL;
	entities[1].client->hookstate = 0;
	CHECK(SG_CompoundGuardGameHookAbsent(&entities[1]) ==
	      SG_COMPOUND_GUARD_YES);

	LiveHook(11, &entities[1]);
	entities[1].client->hook = &entities[11];
	entities[1].client->hookstate = 1;
	CHECK(SG_CompoundGuardGameHookAbsent(&entities[1]) ==
	      SG_COMPOUND_GUARD_OBSERVATION_ERROR);
	CHECK(SG_CompoundGuardGameHookLinked(&entities[1], &entities[11],
	      &second) == SG_COMPOUND_GUARD_OK);
	CHECK(second.edict_key == first.edict_key &&
	      second.generation > first.generation);
	entities[1].client->hook = &entities[11];
	CHECK(SG_CompoundGuardGameHookObserve(&entities[1], &first, &current) ==
	      SG_COMPOUND_GUARD_OBSERVATION_ERROR);
	CHECK(SG_CompoundGuardGameHookObserve(&entities[1], &second, &current) ==
	      SG_COMPOUND_GUARD_YES);
	CHECK(current == &entities[11]);
	CHECK(SG_CompoundGuardGameHookAbsent(&entities[1]) ==
	      SG_COMPOUND_GUARD_NO);

	entities[11].inuse = false;
	SG_CompoundGuardGameEntityFreed(&entities[11]);
	entities[1].client->hook = NULL;
	entities[1].client->hookstate = 0;
	CHECK(SG_CompoundGuardGameHookAbsent(&entities[1]) ==
	      SG_COMPOUND_GUARD_YES);
}

static void TestPusherFenceBasics(void)
{
	edict_t *captain = &entities[15];
	edict_t *slave = &entities[12];
	edict_t *parts[17];
	int index;
	int calls_before;
	int transitions_before;
	int valid_before;

	LiveEntity(15, "func_door");
	LiveEntity(12, "func_door");
	captain->teamchain = slave;
	captain->teammaster = captain;
	slave->teammaster = captain;
	slave->flags |= FL_TEAMSLAVE;
	prospective_inside_key = 1;
	pusher_fence_result = SG_COMPOUND_GUARD_NO_LEASE;
	calls_before = prospective_calls;
	CHECK(SG_CompoundGuardGamePusherMayAdvance(captain));
	CHECK(prospective_calls == calls_before);
	CHECK(pusher_fence_key_count == 2U && pusher_fence_keys[0] == 12U &&
	      pusher_fence_keys[1] == 15U);
	/* A denied captain defers the complete team's absolute mover schedule once,
	 * exactly like stock blocked-pusher rollback.  Visiting the slave later in
	 * the entity loop must not add a second frame. */
	captain->nextthink = 10.0f;
	slave->nextthink = 20.0f;
	SG_CompoundGuardGameEntityDeferred(captain);
	CHECK(captain->nextthink == 10.0f + FRAMETIME);
	CHECK(slave->nextthink == 20.0f + FRAMETIME);
	SG_CompoundGuardGameEntityDeferred(slave);
	CHECK(captain->nextthink == 10.0f + FRAMETIME);
	CHECK(slave->nextthink == 20.0f + FRAMETIME);
	/* Malformed pusher state cannot be advanced or delayed as if canonical;
	 * all observed movement arms are invalidated instead. */
	prospective_pusher_valid = 0;
	transitions_before = completion_transition_calls;
	SG_CompoundGuardGameEntityDeferred(captain);
	CHECK(completion_transition_calls == transitions_before + 2);
	prospective_pusher_valid = 1;
	captain->nextthink = FLT_MAX;
	slave->nextthink = 0.0f;
	transitions_before = completion_transition_calls;
	SG_CompoundGuardGameEntityDeferred(captain);
	CHECK(captain->nextthink == FLT_MAX);
	CHECK(completion_transition_calls == transitions_before + 2);
	captain->nextthink = 0.0f;
	slave->nextthink = 0.0f;
	pusher_fence_result = SG_COMPOUND_GUARD_NOT_INITIALIZED;
	CHECK(SG_CompoundGuardGamePusherMayAdvance(captain));
	pusher_fence_result = SG_COMPOUND_GUARD_HOST_ERROR;
	CHECK(!SG_CompoundGuardGamePusherMayAdvance(captain));
	CHECK(prospective_calls == calls_before);
	pusher_fence_result = SG_COMPOUND_GUARD_OK;
	prospective_inside_key = 0;
	valid_before = prospective_pusher_valid_calls;
	CHECK(SG_CompoundGuardGamePusherMayAdvance(captain));
	CHECK(prospective_pusher_valid_calls == valid_before + 2);
	CHECK(prospective_calls == calls_before + 2);
	prospective_inside_key = 1;
	CHECK(!SG_CompoundGuardGamePusherMayAdvance(captain));
	/* The same transaction-aware gate protects a slave prethink dispatch. */
	CHECK(!SG_CompoundGuardGameEntityMayDispatch(slave));
	pusher_fence_result = SG_COMPOUND_GUARD_NO_LEASE;
	CHECK(SG_CompoundGuardGameEntityMayDispatch(slave));

	/* Member validation is population-independent: with the protected client
	 * physically skipped, malformed prospective pusher state still freezes. */
	entities[1].solid = SOLID_NOT;
	entities[1].movetype = MOVETYPE_NOCLIP;
	pusher_fence_result = SG_COMPOUND_GUARD_OK;
	prospective_pusher_valid = 0;
	CHECK(!SG_CompoundGuardGameEntityMayDispatch(captain));
	prospective_pusher_valid = 1;
	entities[1].solid = SOLID_BBOX;
	entities[1].movetype = MOVETYPE_WALK;

	/* A forward-appended leaf with standalone metadata would otherwise run a
	 * second pusher dispatch later in the entity loop.  Its non-stock backlink
	 * now freezes both the captain's first dispatch and the leaf's later one. */
	slave->flags &= ~FL_TEAMSLAVE;
	slave->teammaster = slave;
	pusher_fence_result = SG_COMPOUND_GUARD_OK;
	pusher_fence_overlap_key = 15U;
	any_door_result = SG_COMPOUND_GUARD_OK;
	prospective_inside_key = 0;
	CHECK(!SG_CompoundGuardGameEntityMayDispatch(captain));
	CHECK(!SG_CompoundGuardGameEntityMayDispatch(slave));
	pusher_fence_overlap_key = 0U;
	any_door_result = SG_COMPOUND_GUARD_NO_LEASE;
	slave->flags |= FL_TEAMSLAVE;
	slave->teammaster = captain;

	/* The canonical blocked callback dereferences teammaster on rollback.
	 * A captured singleton with a foreign or merely wrong master is therefore
	 * denied before prospective geometry can authorize SV_Push. */
	captain->teamchain = NULL;
	captain->teammaster = (edict_t *)(uintptr_t)1U;
	pusher_fence_result = SG_COMPOUND_GUARD_OK;
	pusher_fence_overlap_key = 15U;
	any_door_result = SG_COMPOUND_GUARD_OK;
	CHECK(!SG_CompoundGuardGameEntityMayDispatch(captain));
	captain->teammaster = slave;
	CHECK(!SG_CompoundGuardGameEntityMayDispatch(captain));
	captain->teammaster = captain;
	captain->teamchain = slave;
	pusher_fence_overlap_key = 0U;
	any_door_result = SG_COMPOUND_GUARD_NO_LEASE;

	/* Stock singleton trains/plats do not install a self teammaster.  Even
	 * while another door transaction exists, exact NO_LEASE keeps them live. */
	LiveEntity(14, "func_train");
	entities[14].movetype = MOVETYPE_PUSH;
	pusher_fence_result = SG_COMPOUND_GUARD_OK;
	pusher_fence_overlap_key = 15U;
	any_door_result = SG_COMPOUND_GUARD_OK;
	CHECK(SG_CompoundGuardGameEntityMayDispatch(&entities[14]));
	pusher_fence_overlap_key = 0U;
	any_door_result = SG_COMPOUND_GUARD_NO_LEASE;

	/* A corrupt cycle is irrelevant to an unguarded mapper pusher, but a
	 * guarded captain freezes without trying a partial prospective proof. */
	slave->teamchain = captain;
	pusher_fence_result = SG_COMPOUND_GUARD_NO_LEASE;
	calls_before = any_door_calls;
	CHECK(SG_CompoundGuardGamePusherMayAdvance(captain));
	CHECK(pusher_fence_key_count == 1U && pusher_fence_keys[0] == 15U &&
	      any_door_calls == calls_before + 1);
	calls_before = prospective_calls;
	pusher_fence_result = SG_COMPOUND_GUARD_OK;
	CHECK(!SG_CompoundGuardGamePusherMayAdvance(captain));
	CHECK(prospective_calls == calls_before);
	slave->teamchain = NULL;
	prospective_inside_key = 0;
	pusher_fence_result = SG_COMPOUND_GUARD_NO_LEASE;

	/* A seventeenth member is beyond the captured-key bound.  The malformed
	 * chain may run only after a global proof that its hidden suffix cannot be
	 * protected; an unrelated/hidden transaction makes it freeze closed. */
	for (index = 0; index < 17; index++)
	{
		parts[index] = &entities[index + 3];
		LiveEntity(index + 3, "func_door");
		if (index > 0)
			parts[index]->flags |= FL_TEAMSLAVE;
		if (index > 0)
			parts[index - 1]->teamchain = parts[index];
	}
	pusher_fence_result = SG_COMPOUND_GUARD_NO_LEASE;
	any_door_result = SG_COMPOUND_GUARD_NO_LEASE;
	calls_before = any_door_calls;
	CHECK(SG_CompoundGuardGamePusherMayAdvance(parts[0]));
	CHECK(pusher_fence_key_count == 1U && pusher_fence_keys[0] == 3U &&
	      any_door_calls == calls_before + 1);
	any_door_result = SG_COMPOUND_GUARD_OK;
	CHECK(!SG_CompoundGuardGamePusherMayAdvance(parts[0]));
	any_door_result = SG_COMPOUND_GUARD_HOST_ERROR;
	CHECK(!SG_CompoundGuardGamePusherMayAdvance(parts[0]));
	any_door_result = SG_COMPOUND_GUARD_NO_LEASE;
}

static void TestDisconnectAndExhaustion(void)
{
	int bot_resets_before, completion_resets_before, disconnected_before;
	int level_resets_before;
	int swim_retired_before = swim_retired_calls;
	int slot;
	uint64_t generation = 0U;

	entities[1].solid = SOLID_NOT;
	entities[1].inuse = false;
	disconnected_before = disconnected_calls;
	CHECK(SG_CompoundGuardGameClientDisconnected(&entities[1]) ==
	      SG_COMPOUND_GUARD_OK);
	CHECK(disconnected_calls == disconnected_before + 1);
	CHECK(swim_retired_calls == swim_retired_before + 1);
	CHECK(captured_host.identity(captured_host.context, 1, &generation) ==
	      SG_COMPOUND_GUARD_NO);

	for (slot = 0; slot < SG_MAXBOTS; slot++)
		sg_bots[slot].compound_guard.attached = 1U;
	bot_resets_before = bot_reset_calls;
	level_resets_before = level_resets;
	completion_resets_before = completion_reset_calls;
	SG_CompoundGuardGameStorageWillFree();
	CHECK(level_resets == level_resets_before + 1);
	CHECK(completion_reset_calls == completion_resets_before + 1);
	CHECK(bot_reset_calls == bot_resets_before + SG_MAXBOTS);
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

static void TestCompoundDropDeathOwnsSingleOrphanEdge(void)
{
	sg_compound_guard_host_t host = captured_host;
	int action_before, orphan_before, quarantine_before, respawn_before;

	SG_CompoundGuardGameStorageWillFree();
	ResetWorld();
	captured_host = host;
	CHECK(SG_CompoundGuardGameLevelReset() == SG_COMPOUND_GUARD_OK);
	LiveEntity(1, "player");
	entities[1].client = &clients[0];
	entities[1].solid = SOLID_BBOX;
	entities[1].health = 25;
	CHECK(SG_CompoundGuardGameClientSpawned(&entities[1]) ==
	      SG_COMPOUND_GUARD_OK);
	CHECK(SG_CompoundGuardGameBotAttach(&sg_bots[0].compound_guard, 0,
	      &entities[1]) == SG_COMPOUND_GUARD_OK);
	sg_bots[0].active = true;
	sg_bots[0].ent = &entities[1];
	sg_bots[0].compound_drop_live.guard_owned = true;
	LiveEntity(11, "noclass");
	entities[11].owner = &entities[1];
	entities[11].movetype = MOVETYPE_FLYMISSILE;
	entities[11].solid = SOLID_BBOX;
	entities[11].touch = hook_touch;
	entities[11].die = hook_die;
	CHECK(SG_CompoundGuardGameHookLinked(&entities[1], &entities[11], NULL) ==
	      SG_COMPOUND_GUARD_OK);
	entities[1].client->hook = &entities[11];
	entities[1].client->hookstate = 1;
	validate_result = SG_COMPOUND_GUARD_OK;
	validate_record.law = SG_MOVER_LAW_COMPOUND_PREOPEN;
	validate_record.state = SG_MOVER_LEASE_ACTIVE;
	action_orphan_result = SG_COMPOUND_GUARD_OK;
	action_before = action_orphan_calls;
	orphan_before = orphan_calls;
	CHECK(SG_CompoundGuardGamePlayerDie(&entities[1]) ==
	      SG_COMPOUND_GUARD_OK);
	CHECK(action_orphan_calls == action_before + 1 &&
	      action_orphan_bolt_key == 11);
	CHECK(orphan_calls == orphan_before + 1 && orphan_bolt_key == 11);
	CHECK(!sg_bots[0].compound_drop_live.guard_owned);
	CHECK(validate_record.law == SG_MOVER_LAW_COMPOUND_PREOPEN &&
	      validate_record.state == SG_MOVER_LEASE_ORPHAN);
	CHECK(entities[1].health == -100);

	respawn_before = bot_respawn_calls;
	CHECK(SG_CompoundGuardGameClientSpawned(&entities[1]) ==
	      SG_COMPOUND_GUARD_OK);
	CHECK(bot_respawn_calls == respawn_before + 1);
	CHECK(action_orphan_calls == action_before + 1 &&
	      orphan_calls == orphan_before + 1);
	entities[1].health = 25;
	CHECK(SG_CompoundGuardGamePlayerDie(&entities[1]) ==
	      SG_COMPOUND_GUARD_OK);
	CHECK(action_orphan_calls == action_before + 1 &&
	      orphan_calls == orphan_before + 1);

	sg_bots[0].compound_drop_live.guard_owned = true;
	validate_record.state = SG_MOVER_LEASE_ACTIVE;
	entities[1].health = 25;
	action_orphan_result = SG_COMPOUND_GUARD_HOST_ERROR;
	quarantine_before = quarantine_calls;
	CHECK(SG_CompoundGuardGamePlayerDie(&entities[1]) ==
	      SG_COMPOUND_GUARD_HOST_ERROR);
	CHECK(action_orphan_calls == action_before + 2);
	CHECK(orphan_calls == orphan_before + 1);
	CHECK(sg_bots[0].compound_drop_live.guard_owned);
	CHECK(quarantine_calls == quarantine_before + 1);
	action_orphan_result = SG_COMPOUND_GUARD_OK;
}

static void SetupCompoundHookBot(int linked)
{
	sg_compound_guard_host_t host = captured_host;
	uint64_t bolt_generation;

	SG_CompoundGuardGameStorageWillFree();
	ResetWorld();
	captured_host = host;
	CHECK(SG_CompoundGuardGameLevelReset() == SG_COMPOUND_GUARD_OK);
	LiveEntity(1, "player");
	entities[1].client = &clients[0];
	entities[1].solid = SOLID_BBOX;
	entities[1].health = 25;
	CHECK(SG_CompoundGuardGameClientSpawned(&entities[1]) ==
	    SG_COMPOUND_GUARD_OK);
	CHECK(SG_CompoundGuardGameBotAttach(&sg_bots[0].compound_guard, 0,
	    &entities[1]) == SG_COMPOUND_GUARD_OK);
	sg_bots[0].active = true;
	sg_bots[0].ent = &entities[1];
	sg_bots[0].compound_hook_live.guard_owned = true;
	sg_bots[0].compound_hook_live.local_owned = true;
	validate_result = SG_COMPOUND_GUARD_OK;
	validate_record.law = SG_MOVER_LAW_COMPOUND_PREOPEN;
	validate_record.state = SG_MOVER_LEASE_ACTIVE;
	if (!linked)
		return;
	LiveEntity(11, "noclass");
	entities[11].owner = &entities[1];
	entities[11].movetype = MOVETYPE_FLYMISSILE;
	entities[11].solid = SOLID_BBOX;
	entities[11].touch = hook_touch;
	entities[11].die = hook_die;
	CHECK(SG_CompoundGuardGameHookLinked(&entities[1], &entities[11], NULL) ==
	    SG_COMPOUND_GUARD_OK);
	CHECK(captured_host.identity(captured_host.context, 11,
	    &bolt_generation) == SG_COMPOUND_GUARD_YES);
	entities[1].client->hook = &entities[11];
	entities[1].client->hookstate = 1;
	sg_bots[0].compound_hook_live.bolt_linked = true;
	sg_bots[0].compound_hook_live.bolt.key = 11;
	sg_bots[0].compound_hook_live.bolt.generation = bolt_generation;
}

static void TestCompoundHookLifecycleOrphan(void)
{
	int action_before, orphan_before, quarantine_before, disconnected_before;
	uint64_t generation;

	SetupCompoundHookBot(0);
	action_before = hook_action_orphan_calls;
	orphan_before = orphan_calls;
	CHECK(SG_CompoundGuardGamePlayerDie(&entities[1]) ==
	    SG_COMPOUND_GUARD_OK);
	CHECK(hook_action_orphan_calls == action_before + 1 &&
	    orphan_calls == orphan_before + 1 && orphan_bolt_key == 0);
	CHECK(!sg_bots[0].compound_hook_live.guard_owned &&
	    !sg_bots[0].compound_hook_live.local_owned &&
	    validate_record.state == SG_MOVER_LEASE_ORPHAN);
	CHECK(SG_CompoundGuardGamePlayerDie(&entities[1]) ==
	    SG_COMPOUND_GUARD_OK);
	CHECK(hook_action_orphan_calls == action_before + 1 &&
	    orphan_calls == orphan_before + 1);

	SetupCompoundHookBot(1);
	action_before = hook_action_orphan_calls;
	orphan_before = orphan_calls;
	CHECK(SG_CompoundGuardGamePlayerDie(&entities[1]) ==
	    SG_COMPOUND_GUARD_OK);
	CHECK(hook_action_orphan_calls == action_before + 1 &&
	    orphan_calls == orphan_before + 1 && orphan_bolt_key == 11);
	CHECK(!sg_bots[0].compound_hook_live.guard_owned &&
	    !sg_bots[0].compound_hook_live.local_owned &&
	    validate_record.state == SG_MOVER_LEASE_ORPHAN);
	CHECK(SG_CompoundGuardGamePlayerDie(&entities[1]) ==
	    SG_COMPOUND_GUARD_OK);
	CHECK(hook_action_orphan_calls == action_before + 1 &&
	    orphan_calls == orphan_before + 1);

	SetupCompoundHookBot(1);
	sg_bots[0].compound_drop_live.guard_owned = true;
	action_before = hook_action_orphan_calls;
	orphan_before = orphan_calls;
	quarantine_before = quarantine_calls;
	CHECK(SG_CompoundGuardGamePlayerDie(&entities[1]) ==
	    SG_COMPOUND_GUARD_HOST_ERROR);
	CHECK(hook_action_orphan_calls == action_before &&
	    orphan_calls == orphan_before &&
	    quarantine_calls == quarantine_before + 1);
	CHECK(sg_bots[0].compound_drop_live.guard_owned &&
	    sg_bots[0].compound_hook_live.guard_owned);

	SetupCompoundHookBot(1);
	hook_action_orphan_result = SG_COMPOUND_GUARD_HOST_ERROR;
	action_before = hook_action_orphan_calls;
	orphan_before = orphan_calls;
	quarantine_before = quarantine_calls;
	CHECK(SG_CompoundGuardGamePlayerDie(&entities[1]) ==
	    SG_COMPOUND_GUARD_HOST_ERROR);
	CHECK(hook_action_orphan_calls == action_before + 1 &&
	    orphan_calls == orphan_before &&
	    quarantine_calls == quarantine_before + 1);
	CHECK(sg_bots[0].compound_hook_live.guard_owned &&
	    sg_bots[0].compound_hook_live.local_owned);
	hook_action_orphan_result = SG_COMPOUND_GUARD_OK;

	SetupCompoundHookBot(1);
	action_before = hook_action_orphan_calls;
	disconnected_before = disconnected_calls;
	CHECK(SG_CompoundGuardGameClientDisconnecting(&entities[1]) ==
	    SG_COMPOUND_GUARD_OK);
	CHECK(hook_action_orphan_calls == action_before + 1 &&
	    validate_record.state == SG_MOVER_LEASE_ORPHAN);
	CHECK(captured_host.identity(captured_host.context, 1, &generation) ==
	    SG_COMPOUND_GUARD_YES);
	entities[11].inuse = false;
	SG_CompoundGuardGameEntityFreed(&entities[11]);
	entities[1].client->hook = NULL;
	entities[1].client->hookstate = 0;
	CHECK(SG_CompoundGuardGameBoltEvicted(&entities[1], &entities[11]) ==
	    SG_COMPOUND_GUARD_OK);
	entities[1].solid = SOLID_NOT;
	entities[1].inuse = false;
	CHECK(SG_CompoundGuardGameClientDisconnected(&entities[1]) ==
	    SG_COMPOUND_GUARD_OK);
	CHECK(disconnected_calls == disconnected_before + 1);
	CHECK(captured_host.identity(captured_host.context, 1, &generation) ==
	    SG_COMPOUND_GUARD_NO);
}

int main(void)
{
	ResetWorld();
	CHECK(SG_CompoundGuardGameLevelReset() == SG_COMPOUND_GUARD_OK);
	CHECK(level_resets == 1);
	CHECK(completion_reset_calls == 1);
	CHECK(captured_host.identity != NULL && captured_host.solid != NULL &&
	      captured_host.outside_sweep != NULL &&
	      captured_host.all_subjects_outside != NULL &&
	      captured_host.hold_open != NULL &&
	      captured_host.set_terminal != NULL);
	TestCompoundLifecycleDispatch();
	TestShootDoorAuthorityUnavailable();
	TestIdentityABAAndBounds();
	TestHookSubjectObservation();
	TestPusherFenceBasics();
	TestBodyAndHookIncarnations();
	TestCompoundDropDeathOwnsSingleOrphanEdge();
	TestCompoundHookLifecycleOrphan();
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
