/* Strict host fixture for the ordinary RL_DOOR shared-mover facade. */
#include <stdio.h>
#include <string.h>

#include "g_local.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_bot.h"
#include "slipgate/sg_declared_door_guard.h"

#define TEST_EDICTS 32
#define TEST_LINK 0

game_export_t globals;
game_locals_t game;
edict_t *g_edicts;
static gclient_t clients[1];

static edict_t entities[TEST_EDICTS];
static edict_t foreign_entity;
static rune_seed_t seeds[2];
static rune_link_t links[1];
static rune_t primary_rune;
static rune_seed_t alternate_seeds[2];
static rune_link_t alternate_links[1];
static rune_t alternate_rune;
static rune_t *current_rune;
static edict_t *resolved_trigger;
static edict_t *declared_members[SG_MOVER_LEASE_MAX_KEYS];
static int declared_member_count;
static int declared_count_override;
static int swap_rune_on_members;
static int rune_compatible;
static int rune_shape_valid;
static int failures;

static int acquire_calls;
static int authorize_calls;
static int validate_calls;
static int pause_calls;
static int resume_calls;
static int release_calls;
static int quarantine_calls;
static int hold_calls;
static sg_compound_guard_bot_t *last_guard;
static sg_mover_key_t last_keys[SG_MOVER_LEASE_MAX_KEYS];
static size_t last_key_count;
static sg_mover_lease_law_t last_law;
static int last_link;
static uint32_t last_mechanism;
static sg_mover_lease_record_t guard_record;
static sg_compound_guard_result_t acquire_result;
static sg_compound_guard_result_t authorize_result;
static sg_compound_guard_result_t validate_result;
static sg_compound_guard_result_t pause_result;
static sg_compound_guard_result_t resume_result;
static sg_compound_guard_result_t release_result;
static sg_compound_guard_result_t quarantine_result;
static int hold_result;
static int outside_result;
static int outside_calls;
static int all_subjects_calls;
static sg_compound_guard_result_t all_subjects_result;
static sg_compound_guard_run_t run_state_result;
static int last_hold_lease_ms;
static edict_t *last_hold_members[SG_MOVER_LEASE_MAX_KEYS];
static int last_hold_count;
static int any_retirement;
static int retirement_overlap;
static int retirement_overlap_calls;

sg_bot_t sg_bots[SG_MAXBOTS];

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		        __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static void Set3(vec3_t value, float x, float y, float z)
{
	value[0] = x;
	value[1] = y;
	value[2] = z;
}

static void LiveEdict(int key, const char *classname)
{
	memset(&entities[key], 0, sizeof(entities[key]));
	entities[key].inuse = true;
	entities[key].s.number = key;
	entities[key].classname = (char *)classname;
}

static void RecordKeys(const sg_mover_key_t *keys, size_t key_count)
{
	memset(last_keys, 0, sizeof(last_keys));
	last_key_count = key_count;
	if (keys && key_count <= SG_MOVER_LEASE_MAX_KEYS)
		memcpy(last_keys, keys, key_count * sizeof(keys[0]));
}

static int TupleMatchesRecord(sg_mover_lease_law_t law,
	const sg_mover_key_t *keys, size_t key_count, int link_index,
	uint32_t mechanism_index)
{
	return guard_record.state == SG_MOVER_LEASE_ACTIVE &&
	       guard_record.law == (uint8_t)law &&
	       guard_record.key_count == key_count &&
	       guard_record.link_index == link_index &&
	       guard_record.mechanism_index == mechanism_index &&
	       key_count <= SG_MOVER_LEASE_MAX_KEYS &&
	       memcmp(guard_record.keys, keys,
	              key_count * sizeof(keys[0])) == 0;
}

static void ResetFixture(void)
{
	int key;

	memset(entities, 0, sizeof(entities));
	memset(&foreign_entity, 0, sizeof(foreign_entity));
	memset(seeds, 0, sizeof(seeds));
	memset(links, 0, sizeof(links));
	memset(&primary_rune, 0, sizeof(primary_rune));
	memset(alternate_seeds, 0, sizeof(alternate_seeds));
	memset(alternate_links, 0, sizeof(alternate_links));
	memset(&alternate_rune, 0, sizeof(alternate_rune));
	memset(&globals, 0, sizeof(globals));
	memset(&game, 0, sizeof(game));
	memset(clients, 0, sizeof(clients));
	memset(declared_members, 0, sizeof(declared_members));
	memset(&guard_record, 0, sizeof(guard_record));
	memset(sg_bots, 0, sizeof(sg_bots));
	memset(last_keys, 0, sizeof(last_keys));
	g_edicts = entities;
	globals.edicts = entities;
	globals.edict_size = sizeof(edict_t);
	globals.num_edicts = TEST_EDICTS;
	globals.max_edicts = TEST_EDICTS;
	game.maxentities = TEST_EDICTS;
	game.maxclients = 1;
	game.clients = clients;
	for (key = 0; key < TEST_EDICTS; key++)
		LiveEdict(key, key == 0 ? "worldspawn" : "fixture");
	Set3(seeds[0].origin, 8.0f, 16.0f, 24.0f);
	Set3(seeds[1].origin, 96.0f, 16.0f, 24.0f);
	links[0].from = 0;
	links[0].to = 1;
	links[0].action = RL_DOOR;
	Set3(links[0].anchor, 48.0f, 16.0f, 24.0f);
	primary_rune.hdr.num_seeds = 2;
	primary_rune.hdr.num_links = 1;
	primary_rune.seeds = seeds;
	primary_rune.links = links;
	memcpy(alternate_seeds, seeds, sizeof(seeds));
	memcpy(alternate_links, links, sizeof(links));
	alternate_rune.hdr.num_seeds = 2;
	alternate_rune.hdr.num_links = 1;
	alternate_rune.seeds = alternate_seeds;
	alternate_rune.links = alternate_links;
	current_rune = &primary_rune;
	resolved_trigger = &entities[29];
	declared_members[0] = &entities[12];
	declared_members[1] = &entities[10];
	declared_members[2] = &entities[12];
	declared_members[3] = &entities[11];
	declared_member_count = 4;
	declared_count_override = 0;
	swap_rune_on_members = 0;
	rune_compatible = 1;
	rune_shape_valid = 1;
	acquire_calls = 0;
	authorize_calls = 0;
	validate_calls = 0;
	pause_calls = 0;
	resume_calls = 0;
	release_calls = 0;
	quarantine_calls = 0;
	hold_calls = 0;
	last_guard = NULL;
	last_key_count = 0U;
	last_law = SG_MOVER_LAW_NONE;
	last_link = -1;
	last_mechanism = UINT32_MAX;
	acquire_result = SG_COMPOUND_GUARD_OK;
	authorize_result = SG_COMPOUND_GUARD_OK;
	validate_result = SG_COMPOUND_GUARD_OK;
	pause_result = SG_COMPOUND_GUARD_OK;
	resume_result = SG_COMPOUND_GUARD_OK;
	release_result = SG_COMPOUND_GUARD_OK;
	quarantine_result = SG_COMPOUND_GUARD_OK;
	hold_result = 1;
	outside_result = 1;
	outside_calls = 0;
	all_subjects_calls = 0;
	all_subjects_result = SG_COMPOUND_GUARD_OK;
	run_state_result = SG_COMPOUND_GUARD_RUN_READY;
	memset(last_hold_members, 0, sizeof(last_hold_members));
	last_hold_count = 0;
	last_hold_lease_ms = 0;
	any_retirement = 0;
	retirement_overlap = 0;
	retirement_overlap_calls = 0;
}

rune_t *SG_Rune(void)
{
	return current_rune;
}

qboolean SG_RunePhysicsCompatible(const rune_t *rune)
{
	return rune && rune == current_rune && rune_compatible;
}

qboolean SG_RunePublishedShapeValid(const rune_t *rune)
{
	return rune && rune == current_rune && rune_shape_valid;
}

edict_t *SG_DeclaredDoorForLink(const vec3_t anchor, const vec3_t source)
{
	CHECK(anchor != NULL);
	CHECK(source != NULL);
	if (!anchor || !source || !current_rune)
		return NULL;
	CHECK(memcmp(anchor, current_rune->links[TEST_LINK].anchor,
	             sizeof(vec3_t)) == 0);
	CHECK(memcmp(source,
	             current_rune->seeds[current_rune->links[TEST_LINK].from].origin,
	             sizeof(vec3_t)) == 0);
	return resolved_trigger;
}

int SG_DeclaredDoorMembers(edict_t *trigger, edict_t **members, int capacity)
{
	int index;
	int count = declared_count_override ? declared_count_override
	                                    : declared_member_count;

	CHECK(trigger == resolved_trigger);
	CHECK(capacity == (int)SG_MOVER_LEASE_MAX_KEYS);
	if (members && count > 0 && count <= capacity)
		for (index = 0; index < count; index++)
			members[index] = declared_members[index];
	if (swap_rune_on_members)
		current_rune = &alternate_rune;
	return count;
}

sg_compound_guard_result_t SG_CompoundGuardAcquireDeclaredDoorBound(
	sg_compound_guard_bot_t *bot, const sg_mover_key_t *keys,
	size_t key_count, int link_index, uint32_t mechanism_index)
{
	acquire_calls++;
	last_guard = bot;
	last_law = SG_MOVER_LAW_DECLARED_DOOR;
	last_link = link_index;
	last_mechanism = mechanism_index;
	RecordKeys(keys, key_count);
	if (acquire_result == SG_COMPOUND_GUARD_OK)
	{
		memset(&guard_record, 0, sizeof(guard_record));
		guard_record.law = SG_MOVER_LAW_DECLARED_DOOR;
		guard_record.state = SG_MOVER_LEASE_ACTIVE;
		guard_record.key_count = (uint8_t)key_count;
		guard_record.link_index = link_index;
		guard_record.mechanism_index = mechanism_index;
		guard_record.owner.generation = 1U;
		guard_record.owner.id = 0;
		guard_record.owner.kind = SG_MOVER_OWNER_BOT;
		guard_record.body.generation = 2U;
		guard_record.body.edict_key = 1;
		guard_record.body.kind = SG_MOVER_SUBJECT_CLIENT;
		guard_record.serial = 3U;
		memcpy(guard_record.keys, keys, key_count * sizeof(keys[0]));
	}
	return acquire_result;
}

qboolean SG_DeclaredDoorHoldMembers(edict_t *const *members, int count,
	int lease_ms)
{
	int index;

	hold_calls++;
	last_hold_count = count;
	last_hold_lease_ms = lease_ms;
	memset(last_hold_members, 0, sizeof(last_hold_members));
	if (members && count > 0 && count <= (int)SG_MOVER_LEASE_MAX_KEYS)
		for (index = 0; index < count; index++)
			last_hold_members[index] = members[index];
	return hold_result ? true : false;
}

int SG_CompoundGuardRecordAt(size_t slot,
	sg_mover_lease_record_t *record_out, sg_mover_ticket_t *ticket_out)
{
	if (record_out)
		memset(record_out, 0, sizeof(*record_out));
	if (ticket_out)
	{
		memset(ticket_out, 0, sizeof(*ticket_out));
		ticket_out->slot = SG_MOVER_LEASE_INVALID_SLOT;
	}
	if (slot != 0U || guard_record.state == SG_MOVER_LEASE_FREE)
		return 0;
	if (record_out)
		*record_out = guard_record;
	if (ticket_out)
	{
		ticket_out->epoch = 1U;
		ticket_out->serial = guard_record.serial;
		ticket_out->slot = 0U;
	}
	return 1;
}

int SG_CompoundGuardAnyRetirement(void)
{
	return any_retirement;
}

int SG_CompoundGuardRetirementOverlaps(const sg_mover_key_t *keys,
	size_t key_count)
{
	retirement_overlap_calls++;
	RecordKeys(keys, key_count);
	return retirement_overlap;
}

void door_use(edict_t *self, edict_t *other, edict_t *activator)
{
	(void)self;
	(void)other;
	(void)activator;
}

int SG_MoverTicketValid(const sg_mover_ticket_t *ticket)
{
	return ticket && ticket->epoch != 0U && ticket->serial != 0U &&
	       ticket->slot < SG_MOVER_LEASE_MAX_RECORDS &&
	       ticket->reserved == 0U;
}

int SG_MoverOwnerValid(const sg_mover_owner_t *owner)
{
	return owner && owner->generation != 0U && owner->id >= 0 &&
	       owner->kind == SG_MOVER_OWNER_BOT && owner->reserved[0] == 0U &&
	       owner->reserved[1] == 0U && owner->reserved[2] == 0U;
}

int SG_MoverSubjectValid(const sg_mover_subject_t *subject)
{
	return subject && subject->generation != 0U &&
	       subject->edict_key > 0 &&
	       subject->kind >= SG_MOVER_SUBJECT_CLIENT &&
	       subject->kind <= SG_MOVER_SUBJECT_HOOK_BOLT &&
	       subject->reserved[0] == 0U && subject->reserved[1] == 0U &&
	       subject->reserved[2] == 0U;
}

qboolean SG_MoverSubjectOutsideSweep(edict_t *member, edict_t *subject)
{
	CHECK(member != NULL);
	CHECK(subject != NULL);
	outside_calls++;
	return outside_result ? true : false;
}

sg_compound_guard_result_t SG_CompoundGuardAuthorize(
	sg_compound_guard_bot_t *bot, sg_mover_lease_law_t expected_law,
	const sg_mover_key_t *expected_keys, size_t expected_key_count,
	int expected_link_index, uint32_t expected_mechanism_index)
{
	authorize_calls++;
	last_guard = bot;
	last_law = expected_law;
	last_link = expected_link_index;
	last_mechanism = expected_mechanism_index;
	RecordKeys(expected_keys, expected_key_count);
	if (authorize_result != SG_COMPOUND_GUARD_OK)
		return authorize_result;
	return TupleMatchesRecord(expected_law, expected_keys,
	    expected_key_count, expected_link_index, expected_mechanism_index)
	    ? SG_COMPOUND_GUARD_OK
	    : SG_COMPOUND_GUARD_AUTHORITY_MISMATCH;
}

sg_compound_guard_result_t SG_CompoundGuardAllSubjectsOutside(
	sg_compound_guard_bot_t *bot)
{
	all_subjects_calls++;
	last_guard = bot;
	return all_subjects_result;
}

sg_compound_guard_run_t SG_CompoundGuardBotRunState(
	const sg_compound_guard_bot_t *bot)
{
	last_guard = (sg_compound_guard_bot_t *)bot;
	return run_state_result;
}

sg_compound_guard_result_t SG_CompoundGuardValidate(
	sg_compound_guard_bot_t *bot, sg_mover_lease_record_t *record_out)
{
	validate_calls++;
	last_guard = bot;
	if (record_out)
		*record_out = guard_record;
	if (validate_result == SG_COMPOUND_GUARD_OK &&
	    guard_record.state == SG_MOVER_LEASE_QUARANTINED)
		return SG_COMPOUND_GUARD_QUARANTINE_LOCKED;
	return validate_result;
}

sg_compound_guard_result_t SG_CompoundGuardPause(
	sg_compound_guard_bot_t *bot)
{
	pause_calls++;
	last_guard = bot;
	if (pause_result == SG_COMPOUND_GUARD_OK)
		guard_record.state = SG_MOVER_LEASE_PAUSED;
	return pause_result;
}

sg_compound_guard_result_t SG_CompoundGuardResume(
	sg_compound_guard_bot_t *bot)
{
	resume_calls++;
	last_guard = bot;
	if (resume_result == SG_COMPOUND_GUARD_OK)
		guard_record.state = SG_MOVER_LEASE_ACTIVE;
	return resume_result;
}

sg_compound_guard_result_t SG_CompoundGuardReleaseProvedClear(
	sg_compound_guard_bot_t *bot)
{
	release_calls++;
	last_guard = bot;
	return release_result;
}

sg_compound_guard_result_t SG_CompoundGuardQuarantine(
	sg_compound_guard_bot_t *bot)
{
	quarantine_calls++;
	last_guard = bot;
	if (quarantine_result == SG_COMPOUND_GUARD_OK)
		guard_record.state = SG_MOVER_LEASE_QUARANTINED;
	return quarantine_result;
}

static void CheckCanonicalTuple(const sg_bot_t *bot)
{
	CHECK(last_guard == &bot->compound_guard);
	CHECK(last_law == SG_MOVER_LAW_DECLARED_DOOR);
	CHECK(last_link == TEST_LINK);
	CHECK(last_mechanism == 29U);
	CHECK(last_key_count == 3U);
	CHECK(last_keys[0] == 10U);
	CHECK(last_keys[1] == 11U);
	CHECK(last_keys[2] == 12U);
}

static void TestAcquireCanonicalizes(void)
{
	sg_bot_t bot;

	ResetFixture();
	memset(&bot, 0, sizeof(bot));
	CHECK(SG_DeclaredDoorGuardAcquire(&bot, TEST_LINK) ==
	      SG_COMPOUND_GUARD_OK);
	CHECK(acquire_calls == 1);
	CheckCanonicalTuple(&bot);
	CHECK(guard_record.key_count == 3U);
	CHECK(guard_record.keys[0] == 10U);
	CHECK(guard_record.keys[1] == 11U);
	CHECK(guard_record.keys[2] == 12U);
}

static void TestMalformedAndStaleDeclarations(void)
{
	sg_bot_t bot;
	int index;

	ResetFixture();
	memset(&bot, 0, sizeof(bot));
	CHECK(SG_DeclaredDoorGuardAcquire(NULL, TEST_LINK) ==
	      SG_COMPOUND_GUARD_INVALID_ARGUMENT);
	CHECK(SG_DeclaredDoorGuardAcquire(&bot, -1) ==
	      SG_COMPOUND_GUARD_INVALID_ARGUMENT);
	current_rune = NULL;
	CHECK(SG_DeclaredDoorGuardAcquire(&bot, TEST_LINK) ==
	      SG_COMPOUND_GUARD_AUTHORITY_MISMATCH);
	current_rune = &primary_rune;
	rune_compatible = 0;
	CHECK(SG_DeclaredDoorGuardAcquire(&bot, TEST_LINK) ==
	      SG_COMPOUND_GUARD_AUTHORITY_MISMATCH);
	rune_compatible = 1;
	rune_shape_valid = 0;
	CHECK(SG_DeclaredDoorGuardAcquire(&bot, TEST_LINK) ==
	      SG_COMPOUND_GUARD_AUTHORITY_MISMATCH);
	rune_shape_valid = 1;
	links[0].action = RL_RUN;
	CHECK(SG_DeclaredDoorGuardAcquire(&bot, TEST_LINK) ==
	      SG_COMPOUND_GUARD_AUTHORITY_MISMATCH);
	links[0].action = RL_DOOR;

	declared_member_count = 0;
	CHECK(SG_DeclaredDoorGuardAcquire(&bot, TEST_LINK) ==
	      SG_COMPOUND_GUARD_INVALID_KEYS);
	declared_count_override = (int)SG_MOVER_LEASE_MAX_KEYS + 1;
	CHECK(SG_DeclaredDoorGuardAcquire(&bot, TEST_LINK) ==
	      SG_COMPOUND_GUARD_INVALID_KEYS);
	declared_count_override = 0;
	declared_member_count = 1;
	declared_members[0] = &foreign_entity;
	CHECK(SG_DeclaredDoorGuardAcquire(&bot, TEST_LINK) ==
	      SG_COMPOUND_GUARD_INVALID_KEYS);
	declared_members[0] = &entities[12];
	entities[12].s.number = 13;
	CHECK(SG_DeclaredDoorGuardAcquire(&bot, TEST_LINK) ==
	      SG_COMPOUND_GUARD_INVALID_KEYS);
	entities[12].s.number = 12;
	resolved_trigger = &foreign_entity;
	CHECK(SG_DeclaredDoorGuardAcquire(&bot, TEST_LINK) ==
	      SG_COMPOUND_GUARD_AUTHORITY_MISMATCH);

	ResetFixture();
	for (index = 0; index < (int)SG_MOVER_LEASE_MAX_KEYS; index++)
		declared_members[index] = &entities[index + 10];
	declared_member_count = (int)SG_MOVER_LEASE_MAX_KEYS;
	CHECK(SG_DeclaredDoorGuardAcquire(&bot, TEST_LINK) ==
	      SG_COMPOUND_GUARD_OK);
	CHECK(last_key_count == SG_MOVER_LEASE_MAX_KEYS);

	ResetFixture();
	swap_rune_on_members = 1;
	CHECK(SG_DeclaredDoorGuardAcquire(&bot, TEST_LINK) ==
	      SG_COMPOUND_GUARD_AUTHORITY_MISMATCH);
	CHECK(acquire_calls == 0);
}

static void TestCoreFailuresPassThrough(void)
{
	sg_bot_t bot;

	ResetFixture();
	memset(&bot, 0, sizeof(bot));
	acquire_result = SG_COMPOUND_GUARD_CONFLICT;
	CHECK(SG_DeclaredDoorGuardAcquire(&bot, TEST_LINK) ==
	      SG_COMPOUND_GUARD_CONFLICT);
	CHECK(acquire_calls == 1);
	acquire_result = SG_COMPOUND_GUARD_OWNER_BUSY;
	CHECK(SG_DeclaredDoorGuardAcquire(&bot, TEST_LINK) ==
	      SG_COMPOUND_GUARD_OWNER_BUSY);
	CHECK(acquire_calls == 2);
}

static void TestExactAuthorizationAndState(void)
{
	sg_bot_t bot;

	ResetFixture();
	memset(&bot, 0, sizeof(bot));
	CHECK(SG_DeclaredDoorGuardAcquire(&bot, TEST_LINK) ==
	      SG_COMPOUND_GUARD_OK);
	CHECK(SG_DeclaredDoorGuardAuthorize(&bot, TEST_LINK) ==
	      SG_COMPOUND_GUARD_OK);
	CHECK(authorize_calls == 1);
	CheckCanonicalTuple(&bot);
	guard_record.mechanism_index = 1U;
	CHECK(SG_DeclaredDoorGuardAuthorize(&bot, TEST_LINK) ==
	      SG_COMPOUND_GUARD_AUTHORITY_MISMATCH);
	guard_record.mechanism_index = 29U;
	authorize_result = SG_COMPOUND_GUARD_OWNER_MISMATCH;
	CHECK(SG_DeclaredDoorGuardAuthorize(&bot, TEST_LINK) ==
	      SG_COMPOUND_GUARD_OWNER_MISMATCH);

	authorize_result = SG_COMPOUND_GUARD_OK;
	CHECK(SG_DeclaredDoorGuardPause(&bot) == SG_COMPOUND_GUARD_OK);
	CHECK(validate_calls == 1);
	CHECK(pause_calls == 1);
	CHECK(guard_record.state == SG_MOVER_LEASE_PAUSED);
	CHECK(SG_DeclaredDoorGuardPause(&bot) == SG_COMPOUND_GUARD_OK);
	CHECK(pause_calls == 1);
	CHECK(authorize_calls == 4);
	CHECK(SG_DeclaredDoorGuardResume(&bot, TEST_LINK) ==
	      SG_COMPOUND_GUARD_OK);
	CHECK(resume_calls == 1);
	CHECK(guard_record.state == SG_MOVER_LEASE_ACTIVE);
	CheckCanonicalTuple(&bot);

	CHECK(SG_DeclaredDoorGuardPause(&bot) == SG_COMPOUND_GUARD_OK);
	guard_record.keys[0] = 9U;
	CHECK(SG_DeclaredDoorGuardResume(&bot, TEST_LINK) ==
	      SG_COMPOUND_GUARD_AUTHORITY_MISMATCH);
	CHECK(resume_calls == 1);
	guard_record.keys[0] = 10U;
	guard_record.law = SG_MOVER_LAW_COMPOUND_PREOPEN;
	guard_record.state = SG_MOVER_LEASE_ACTIVE;
	CHECK(SG_DeclaredDoorGuardPause(&bot) ==
	      SG_COMPOUND_GUARD_AUTHORITY_MISMATCH);
	CHECK(pause_calls == 2);

	guard_record.law = SG_MOVER_LAW_DECLARED_DOOR;
	guard_record.state = SG_MOVER_LEASE_PAUSED;
	authorize_result = SG_COMPOUND_GUARD_HOST_ERROR;
	CHECK(SG_DeclaredDoorGuardResume(&bot, TEST_LINK) ==
	      SG_COMPOUND_GUARD_HOST_ERROR);
	CHECK(guard_record.state == SG_MOVER_LEASE_PAUSED);
	CHECK(pause_calls == 3);
	CHECK(quarantine_calls == 0);

	guard_record.state = SG_MOVER_LEASE_PAUSED;
	pause_result = SG_COMPOUND_GUARD_INVALID_TRANSITION;
	CHECK(SG_DeclaredDoorGuardResume(&bot, TEST_LINK) ==
	      SG_COMPOUND_GUARD_HOST_ERROR);
	CHECK(quarantine_calls == 1);
	CHECK(guard_record.state == SG_MOVER_LEASE_QUARANTINED);
}

static void TestReleaseDelegatesProof(void)
{
	sg_bot_t bot;

	ResetFixture();
	memset(&bot, 0, sizeof(bot));
	CHECK(SG_DeclaredDoorGuardAcquire(&bot, TEST_LINK) ==
	      SG_COMPOUND_GUARD_OK);
	release_result = SG_COMPOUND_GUARD_NOT_CLEAR;
	CHECK(SG_DeclaredDoorGuardReleaseProvedClear(&bot) ==
	      SG_COMPOUND_GUARD_NOT_CLEAR);
	CHECK(release_calls == 1);
	CHECK(last_guard == &bot.compound_guard);
	CHECK(SG_DeclaredDoorGuardReleaseProvedClear(NULL) ==
	      SG_COMPOUND_GUARD_INVALID_ARGUMENT);
	CHECK(release_calls == 1);
}

static void TestActivationAndSchedulerBoundaries(void)
{
	sg_bot_t bot;

	ResetFixture();
	memset(&bot, 0, sizeof(bot));
	CHECK(SG_DeclaredDoorGuardAcquire(&bot, TEST_LINK) ==
	      SG_COMPOUND_GUARD_OK);
	CHECK(SG_DeclaredDoorGuardAuthorizeActivation(&bot, TEST_LINK) ==
	      SG_COMPOUND_GUARD_OK);
	CHECK(authorize_calls == 1 && all_subjects_calls == 1);
	all_subjects_result = SG_COMPOUND_GUARD_NOT_CLEAR;
	CHECK(SG_DeclaredDoorGuardAuthorizeActivation(&bot, TEST_LINK) ==
	      SG_COMPOUND_GUARD_NOT_CLEAR);
	CHECK(authorize_calls == 2 && all_subjects_calls == 2);
	run_state_result = SG_COMPOUND_GUARD_RUN_WAIT;
	CHECK(SG_DeclaredDoorGuardRunState(&bot) ==
	      SG_COMPOUND_GUARD_RUN_WAIT);
	CHECK(last_guard == &bot.compound_guard);
}

static void TestReleaseProtectsDisplacedContender(void)
{
	sg_bot_t owner;

	ResetFixture();
	memset(&owner, 0, sizeof(owner));
	CHECK(SG_DeclaredDoorGuardAcquire(&owner, TEST_LINK) ==
	      SG_COMPOUND_GUARD_OK);
	sg_bots[1].active = true;
	sg_bots[1].ent = &entities[20];
	entities[20].solid = SOLID_BBOX;
	outside_result = 0;
	CHECK(SG_DeclaredDoorGuardReleaseProvedClear(&owner) ==
	      SG_COMPOUND_GUARD_NOT_CLEAR);
	CHECK(outside_calls == 1);
	CHECK(release_calls == 0);
	outside_result = 1;
	CHECK(SG_DeclaredDoorGuardReleaseProvedClear(&owner) ==
	      SG_COMPOUND_GUARD_OK);
	CHECK(outside_calls == 4);
	CHECK(release_calls == 1);
}

static void TestProtectiveHoldAndBoundRelease(void)
{
	sg_bot_t bot;

	ResetFixture();
	memset(&bot, 0, sizeof(bot));
	CHECK(SG_DeclaredDoorGuardAcquire(&bot, TEST_LINK) ==
	      SG_COMPOUND_GUARD_OK);
	CHECK(SG_DeclaredDoorGuardHoldOpen(&bot, 500) ==
	      SG_COMPOUND_GUARD_OK);
	CHECK(hold_calls == 1 && last_hold_count == 3 &&
	      last_hold_members[0] == &entities[10] &&
	      last_hold_members[1] == &entities[11] &&
	      last_hold_members[2] == &entities[12] &&
	      last_hold_lease_ms == 500);
	CHECK(guard_record.state == SG_MOVER_LEASE_ACTIVE);

	CHECK(SG_DeclaredDoorGuardPause(&bot) == SG_COMPOUND_GUARD_OK);
	CHECK(guard_record.state == SG_MOVER_LEASE_PAUSED);
	CHECK(SG_DeclaredDoorGuardHoldOpen(&bot, 500) ==
	      SG_COMPOUND_GUARD_OK);
	CHECK(hold_calls == 2 && resume_calls == 1 &&
	      guard_record.state == SG_MOVER_LEASE_PAUSED);

	hold_result = 0;
	CHECK(SG_DeclaredDoorGuardHoldOpen(&bot, 500) ==
	      SG_COMPOUND_GUARD_HOST_ERROR);
	CHECK(hold_calls == 3 && guard_record.state == SG_MOVER_LEASE_PAUSED);
	hold_result = 1;
	pause_result = SG_COMPOUND_GUARD_INVALID_TRANSITION;
	CHECK(SG_DeclaredDoorGuardHoldOpen(&bot, 500) ==
	      SG_COMPOUND_GUARD_INVALID_TRANSITION);
	CHECK(quarantine_calls == 1 &&
	      guard_record.state == SG_MOVER_LEASE_QUARANTINED);
	pause_result = SG_COMPOUND_GUARD_OK;
	hold_result = 1;
	CHECK(SG_DeclaredDoorGuardHoldOpen(&bot, 500) ==
	      SG_COMPOUND_GUARD_OK);
	CHECK(hold_calls == 5 &&
	      guard_record.state == SG_MOVER_LEASE_QUARANTINED);

	ResetFixture();
	memset(&bot, 0, sizeof(bot));
	CHECK(SG_DeclaredDoorGuardAcquire(&bot, TEST_LINK) ==
	      SG_COMPOUND_GUARD_OK);
	declared_members[3] = &entities[13];
	CHECK(SG_DeclaredDoorGuardReleaseProvedClear(&bot) ==
	      SG_COMPOUND_GUARD_AUTHORITY_MISMATCH);
	CHECK(release_calls == 0);
	declared_members[3] = &entities[11];
	resolved_trigger->inuse = false;
	release_result = SG_COMPOUND_GUARD_OK;
	CHECK(SG_DeclaredDoorGuardReleaseProvedClear(&bot) ==
	      SG_COMPOUND_GUARD_OK);
	CHECK(release_calls == 1);

	ResetFixture();
	memset(&bot, 0, sizeof(bot));
	CHECK(SG_DeclaredDoorGuardAcquire(&bot, TEST_LINK) ==
	      SG_COMPOUND_GUARD_OK);
	resolved_trigger->inuse = false;
	CHECK(SG_DeclaredDoorGuardHoldOpen(&bot, 500) ==
	      SG_COMPOUND_GUARD_OK);
	CHECK(hold_calls == 1 && last_hold_count == 3 &&
	      guard_record.state == SG_MOVER_LEASE_ACTIVE);
	CHECK(SG_DeclaredDoorGuardPause(&bot) == SG_COMPOUND_GUARD_OK);
	CHECK(guard_record.state == SG_MOVER_LEASE_PAUSED);
	CHECK(SG_DeclaredDoorGuardHoldOpen(&bot, 500) ==
	      SG_COMPOUND_GUARD_OK);
	CHECK(hold_calls == 2 && resume_calls == 1 && pause_calls == 2 &&
	      guard_record.state == SG_MOVER_LEASE_PAUSED);

	/* Slot reuse/live malformed replacement is not absence and cannot inherit
	 * the durable activator's maintenance authority. */
	resolved_trigger->inuse = true;
	declared_count_override = -1;
	CHECK(SG_DeclaredDoorGuardHoldOpen(&bot, 500) ==
	      SG_COMPOUND_GUARD_INVALID_KEYS);
	CHECK(hold_calls == 2);
}

static void TestUnsupportedActivationRespectsGlobalLease(void)
{
	sg_bot_t bot;

	ResetFixture();
	memset(&bot, 0, sizeof(bot));
	entities[10].classname = "func_door";
	entities[11].classname = "func_door";
	entities[12].classname = "func_door_rotating";
	entities[10].use = door_use;
	entities[11].use = door_use;
	entities[12].use = door_use;
	entities[10].teammaster = &entities[10];
	entities[11].teammaster = &entities[10];
	entities[12].teammaster = &entities[10];
	entities[10].teamchain = &entities[11];
	entities[11].teamchain = &entities[12];
	CHECK(SG_DeclaredDoorGuardActivationAvailable(&entities[10]));
	CHECK(!SG_DeclaredDoorGuardAnyClaim());
	CHECK(SG_DeclaredDoorGuardAcquire(&bot, TEST_LINK) ==
	      SG_COMPOUND_GUARD_OK);
	CHECK(!SG_DeclaredDoorGuardActivationAvailable(&entities[10]));
	CHECK(SG_DeclaredDoorGuardAnyClaim());
	guard_record.keys[0] = 0U;
	CHECK(!SG_DeclaredDoorGuardActivationAvailable(&entities[10]));
	guard_record.keys[0] = (sg_mover_key_t)globals.num_edicts;
	CHECK(!SG_DeclaredDoorGuardActivationAvailable(&entities[10]));
	guard_record.keys[0] = 10U;
	guard_record.mechanism_index = (uint32_t)globals.num_edicts;
	CHECK(!SG_DeclaredDoorGuardActivationAvailable(&entities[10]));
	guard_record.mechanism_index = 9U;
	memset(&guard_record, 0, sizeof(guard_record));
	CHECK(SG_DeclaredDoorGuardActivationAvailable(&entities[10]));
	CHECK(!SG_DeclaredDoorGuardAnyClaim());
	/* A delayed/unsupported callback remains fenced after logical release.
	 * Exact overlap blocks door_use; even a disjoint retirement keeps the
	 * deliberately conservative unsupported Touch_Multi gate closed. */
	retirement_overlap_calls = 0;
	retirement_overlap = 1;
	any_retirement = 1;
	CHECK(!SG_DeclaredDoorGuardActivationAvailable(&entities[10]));
	CHECK(retirement_overlap_calls == 1 && last_key_count == 3U);
	CHECK(SG_DeclaredDoorGuardAnyClaim());
	retirement_overlap = 0;
	CHECK(SG_DeclaredDoorGuardActivationAvailable(&entities[10]));
	CHECK(SG_DeclaredDoorGuardAnyClaim());
	any_retirement = 0;
	CHECK(!SG_DeclaredDoorGuardAnyClaim());
	/* A drifted teammaster may not redirect the callback's set resolution to a
	 * valid but disjoint chain that omits the door actually being mutated. */
	entities[10].teammaster = &entities[11];
	CHECK(!SG_DeclaredDoorGuardActivationAvailable(&entities[10]));
	entities[10].teammaster = &entities[10];
	entities[12].teamchain = &entities[10];
	CHECK(!SG_DeclaredDoorGuardActivationAvailable(&entities[10]));
}

int main(void)
{
	TestAcquireCanonicalizes();
	TestMalformedAndStaleDeclarations();
	TestCoreFailuresPassThrough();
	TestExactAuthorizationAndState();
	TestActivationAndSchedulerBoundaries();
	TestReleaseDelegatesProof();
	TestReleaseProtectsDisplacedContender();
	TestProtectiveHoldAndBoundRelease();
	TestUnsupportedActivationRespectsGlobalLease();
	if (failures)
	{
		fprintf(stderr, "%d declared-door guard test(s) failed\n", failures);
		return 1;
	}
	puts("declared-door guard tests passed");
	return 0;
}
