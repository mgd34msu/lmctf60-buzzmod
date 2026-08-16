/* Host-free lifecycle tests for the shared game-side mover guard. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "slipgate/sg_compound_guard.h"

#define HOST_ENTITY_COUNT 256

typedef struct fake_entity_s
{
	uint64_t generation;
	int present;
	int solid;
	int outside;
	int identity_error;
	int solid_error;
	int outside_error;
} fake_entity_t;

typedef struct fake_host_s
{
	fake_entity_t entities[HOST_ENTITY_COUNT];
} fake_host_t;

static fake_host_t fake;
static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		        __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static fake_entity_t *Entity(int32_t key)
{
	if (key <= 0 || key >= HOST_ENTITY_COUNT)
		return NULL;
	return &fake.entities[key];
}

static void SpawnEntity(int32_t key, int solid, int outside)
{
	fake_entity_t *entity = Entity(key);

	CHECK(entity != NULL);
	if (!entity)
		return;
	if (entity->generation == UINT64_MAX)
	{
		entity->identity_error = 1;
		entity->present = 0;
		return;
	}
	entity->generation++;
	entity->present = 1;
	entity->solid = solid;
	entity->outside = outside;
	entity->identity_error = 0;
	entity->solid_error = 0;
	entity->outside_error = 0;
}

static sg_compound_guard_observation_t FakeIdentity(void *context,
	int32_t edict_key, uint64_t *generation_out)
{
	fake_host_t *host = (fake_host_t *)context;
	fake_entity_t *entity;

	if (!host || !generation_out || edict_key <= 0 ||
	    edict_key >= HOST_ENTITY_COUNT)
		return SG_COMPOUND_GUARD_OBSERVATION_ERROR;
	entity = &host->entities[edict_key];
	if (entity->identity_error)
		return SG_COMPOUND_GUARD_OBSERVATION_ERROR;
	if (!entity->present)
		return SG_COMPOUND_GUARD_NO;
	*generation_out = entity->generation;
	return SG_COMPOUND_GUARD_YES;
}

static sg_compound_guard_observation_t FakeSolid(void *context,
	const sg_mover_subject_t *subject)
{
	fake_host_t *host = (fake_host_t *)context;
	fake_entity_t *entity;

	if (!host || !subject || subject->edict_key <= 0 ||
	    subject->edict_key >= HOST_ENTITY_COUNT)
		return SG_COMPOUND_GUARD_OBSERVATION_ERROR;
	entity = &host->entities[subject->edict_key];
	if (entity->solid_error)
		return SG_COMPOUND_GUARD_OBSERVATION_ERROR;
	return entity->solid ? SG_COMPOUND_GUARD_YES
	                     : SG_COMPOUND_GUARD_NO;
}

static sg_compound_guard_observation_t FakeOutside(void *context,
	const sg_mover_subject_t *subject, const sg_mover_key_t *keys,
	size_t key_count)
{
	fake_host_t *host = (fake_host_t *)context;
	fake_entity_t *entity;

	if (!host || !subject || !keys || key_count == 0U ||
	    subject->edict_key <= 0 || subject->edict_key >= HOST_ENTITY_COUNT)
		return SG_COMPOUND_GUARD_OBSERVATION_ERROR;
	entity = &host->entities[subject->edict_key];
	if (entity->outside_error)
		return SG_COMPOUND_GUARD_OBSERVATION_ERROR;
	return entity->outside ? SG_COMPOUND_GUARD_YES
	                       : SG_COMPOUND_GUARD_NO;
}

static void ResetGuard(void)
{
	sg_compound_guard_host_t host;

	memset(&fake, 0, sizeof(fake));
	memset(&host, 0, sizeof(host));
	host.context = &fake;
	host.identity = FakeIdentity;
	host.solid = FakeSolid;
	host.outside_sweep = FakeOutside;
	CHECK(SG_CompoundGuardInit(&host) == SG_COMPOUND_GUARD_OK);
}

static int FindOwner(const sg_mover_owner_t *owner,
	sg_mover_lease_record_t *record_out, sg_mover_ticket_t *ticket_out)
{
	size_t slot;
	sg_mover_lease_record_t record;
	sg_mover_ticket_t ticket;

	if (record_out)
		memset(record_out, 0, sizeof(*record_out));
	SG_MoverTicketClear(ticket_out);
	for (slot = 0U; slot < SG_MOVER_LEASE_MAX_RECORDS; slot++)
	{
		if (!SG_CompoundGuardRecordAt(slot, &record, &ticket))
			continue;
		if (!SG_MoverOwnerEqual(owner, &record.owner))
			continue;
		if (record_out)
			*record_out = record;
		if (ticket_out)
			*ticket_out = ticket;
		return 1;
	}
	return 0;
}

static size_t RecordCount(void)
{
	size_t slot, count = 0U;
	sg_mover_lease_record_t record;
	sg_mover_ticket_t ticket;

	for (slot = 0U; slot < SG_MOVER_LEASE_MAX_RECORDS; slot++)
		if (SG_CompoundGuardRecordAt(slot, &record, &ticket))
			count++;
	return count;
}

static void Attach(sg_compound_guard_bot_t *bot, int slot, int client_key)
{
	memset(bot, 0, sizeof(*bot));
	SpawnEntity(client_key, 1, 1);
	CHECK(SG_CompoundGuardBotAttach(bot, slot, client_key) ==
	      SG_COMPOUND_GUARD_OK);
}

static void TestSharedAcquirePauseAndClear(void)
{
	sg_compound_guard_bot_t door_bot, compound_bot;
	sg_mover_key_t door_keys[] = {10U, 20U};
	sg_mover_key_t overlap_keys[] = {20U, 30U};
	sg_mover_key_t clear_keys[] = {21U, 30U};
	sg_mover_key_t wrong_keys[] = {10U, 21U};
	sg_mover_lease_record_t record;
	unsigned action_revision_gate = 0U;

	ResetGuard();
	Attach(&door_bot, 0, 1);
	Attach(&compound_bot, 1, 2);
	Entity(1)->solid = 0;
	CHECK(SG_CompoundGuardAcquireDeclaredDoor(&door_bot, door_keys, 2U,
	      17) == SG_COMPOUND_GUARD_NOT_CLEAR);
	Entity(1)->solid = 1;
	Entity(1)->outside = 0;
	CHECK(SG_CompoundGuardAcquireDeclaredDoor(&door_bot, door_keys, 2U,
	      17) == SG_COMPOUND_GUARD_NOT_CLEAR);
	Entity(1)->outside = 1;
	CHECK(SG_CompoundGuardAcquireDeclaredDoor(&door_bot, door_keys, 2U,
	      17) == SG_COMPOUND_GUARD_OK);
	CHECK(SG_CompoundGuardAcquireCompoundPreopen(&compound_bot,
	      overlap_keys, 2U, 18, 3U) == SG_COMPOUND_GUARD_CONFLICT);
	CHECK(action_revision_gate == 0U);
	CHECK(SG_CompoundGuardAcquireCompoundPreopen(&compound_bot,
	      clear_keys, 2U, 18, 3U) == SG_COMPOUND_GUARD_OK);
	CHECK(SG_CompoundGuardValidate(&door_bot, &record) ==
	      SG_COMPOUND_GUARD_OK);
	CHECK(record.law == SG_MOVER_LAW_DECLARED_DOOR &&
	      record.link_index == 17);
	CHECK(SG_CompoundGuardAuthorize(&door_bot, SG_MOVER_LAW_DECLARED_DOOR,
	      door_keys, 2U, 17, 0U) == SG_COMPOUND_GUARD_OK);
	CHECK(SG_CompoundGuardAuthorize(&door_bot, SG_MOVER_LAW_DECLARED_DOOR,
	      wrong_keys, 2U, 17, 0U) ==
	      SG_COMPOUND_GUARD_AUTHORITY_MISMATCH);
	CHECK(SG_CompoundGuardAuthorize(&door_bot,
	      SG_MOVER_LAW_COMPOUND_PREOPEN, door_keys, 2U, 17, 0U) ==
	      SG_COMPOUND_GUARD_AUTHORITY_MISMATCH);
	CHECK(SG_CompoundGuardAuthorize(&door_bot, SG_MOVER_LAW_DECLARED_DOOR,
	      door_keys, 2U, 18, 0U) ==
	      SG_COMPOUND_GUARD_AUTHORITY_MISMATCH);
	CHECK(SG_CompoundGuardPause(&door_bot) == SG_COMPOUND_GUARD_OK);
	CHECK(SG_CompoundGuardAuthorize(&door_bot, SG_MOVER_LAW_DECLARED_DOOR,
	      door_keys, 2U, 17, 0U) ==
	      SG_COMPOUND_GUARD_INVALID_TRANSITION);
	CHECK(SG_CompoundGuardValidate(&door_bot, &record) ==
	      SG_COMPOUND_GUARD_OK && record.state == SG_MOVER_LEASE_PAUSED);
	CHECK(SG_CompoundGuardResume(&door_bot) == SG_COMPOUND_GUARD_OK);
	Entity(1)->outside = 0;
	CHECK(SG_CompoundGuardReleaseProvedClear(&door_bot) ==
	      SG_COMPOUND_GUARD_NOT_CLEAR);
	Entity(1)->outside = 1;
	CHECK(SG_CompoundGuardReleaseProvedClear(&door_bot) ==
	      SG_COMPOUND_GUARD_OK);
	CHECK(!SG_MoverTicketValid(&door_bot.ticket));
	Entity(2)->solid = 0;
	CHECK(SG_CompoundGuardReleaseProvedClear(&compound_bot) ==
	      SG_COMPOUND_GUARD_OK);
	CHECK(RecordCount() == 0U);
}

static void TestIdentityDisconnectAndReset(void)
{
	sg_compound_guard_bot_t bot;
	sg_mover_key_t key = 40U;
	sg_mover_lease_record_t record;
	sg_mover_ticket_t ticket;
	sg_mover_owner_t owner;

	ResetGuard();
	Attach(&bot, 2, 3);
	owner = bot.owner;
	CHECK(SG_CompoundGuardAcquireDeclaredDoor(&bot, &key, 1U, 4) ==
	      SG_COMPOUND_GUARD_OK);
	SpawnEntity(3, 1, 0); /* same edict key, different occupant generation */
	CHECK(SG_CompoundGuardValidate(&bot, NULL) ==
	      SG_COMPOUND_GUARD_IDENTITY_STALE);
	CHECK(FindOwner(&owner, &record, &ticket));
	CHECK(record.state == SG_MOVER_LEASE_QUARANTINED);
	CHECK(SG_CompoundGuardReleaseProvedClear(&bot) ==
	      SG_COMPOUND_GUARD_QUARANTINE_LOCKED);

	/* Host failure after the physical respawn is a fail-closed lifecycle edge,
	 * not an early validation error that may leave ownership ACTIVE. */
	ResetGuard();
	Attach(&bot, 2, 3);
	CHECK(SG_CompoundGuardAcquireDeclaredDoor(&bot, &key, 1U, 4) ==
	      SG_COMPOUND_GUARD_OK);
	owner = bot.owner;
	SpawnEntity(3, 1, 1);
	Entity(3)->identity_error = 1;
	CHECK(SG_CompoundGuardBotRespawn(&bot, 3) ==
	      SG_COMPOUND_GUARD_HOST_ERROR);
	CHECK(FindOwner(&owner, &record, &ticket));
	CHECK(record.state == SG_MOVER_LEASE_QUARANTINED);

	/* The same law holds for a retained orphan when the supposedly linked new
	 * client is absent from the host identity table. */
	ResetGuard();
	Attach(&bot, 2, 3);
	CHECK(SG_CompoundGuardAcquireDeclaredDoor(&bot, &key, 1U, 4) ==
	      SG_COMPOUND_GUARD_OK);
	owner = bot.owner;
	CHECK(SG_CompoundGuardOrphan(&bot, 0) == SG_COMPOUND_GUARD_OK);
	SpawnEntity(3, 1, 1);
	Entity(3)->present = 0;
	CHECK(SG_CompoundGuardBotRespawn(&bot, 3) ==
	      SG_COMPOUND_GUARD_ENTITY_ABSENT);
	CHECK(FindOwner(&owner, &record, &ticket));
	CHECK(record.state == SG_MOVER_LEASE_QUARANTINED);

	/* A changed client identity without a preceding death/orphan transition is
	 * not a respawn; the still-live lease is quarantined. */
	ResetGuard();
	Attach(&bot, 2, 3);
	CHECK(SG_CompoundGuardAcquireDeclaredDoor(&bot, &key, 1U, 4) ==
	      SG_COMPOUND_GUARD_OK);
	owner = bot.owner;
	SpawnEntity(3, 1, 1);
	CHECK(SG_CompoundGuardBotRespawn(&bot, 3) ==
	      SG_COMPOUND_GUARD_INVALID_TRANSITION);
	CHECK(FindOwner(&owner, &record, &ticket));
	CHECK(record.state == SG_MOVER_LEASE_QUARANTINED);

	/* Once death has begun, even a pure handoff error must not leave an ACTIVE
	 * lease that Frame will ignore.  This bolt aliases the client identity. */
	ResetGuard();
	Attach(&bot, 2, 3);
	CHECK(SG_CompoundGuardAcquireDeclaredDoor(&bot, &key, 1U, 4) ==
	      SG_COMPOUND_GUARD_OK);
	owner = bot.owner;
	CHECK(SG_CompoundGuardOrphan(&bot, 3) ==
	      SG_COMPOUND_GUARD_INVALID_SUBJECT);
	CHECK(FindOwner(&owner, &record, &ticket));
	CHECK(record.state == SG_MOVER_LEASE_QUARANTINED);

	ResetGuard();
	Attach(&bot, 2, 3);
	CHECK(SG_CompoundGuardAcquireDeclaredDoor(&bot, &key, 1U, 4) ==
	      SG_COMPOUND_GUARD_OK);
	owner = bot.owner;
	CHECK(SG_CompoundGuardOrphan(&bot, -1) ==
	      SG_COMPOUND_GUARD_INVALID_ARGUMENT);
	CHECK(FindOwner(&owner, &record, &ticket));
	CHECK(record.state == SG_MOVER_LEASE_QUARANTINED);

	ResetGuard();
	Attach(&bot, 2, 3);
	CHECK(SG_CompoundGuardAcquireDeclaredDoor(&bot, &key, 1U, 4) ==
	      SG_COMPOUND_GUARD_OK);
	owner = bot.owner;
	Entity(3)->present = 0; /* clean disconnect after unlink/inuse=false */
	CHECK(SG_CompoundGuardBotDisconnected(&bot) == SG_COMPOUND_GUARD_OK);
	CHECK(!FindOwner(&owner, &record, &ticket));
	CHECK(SG_CompoundGuardBotReset(&bot) == SG_COMPOUND_GUARD_OK);
	CHECK(!bot.attached && !SG_MoverTicketValid(&bot.ticket));

	ResetGuard();
	Attach(&bot, 2, 3);
	CHECK(SG_CompoundGuardAcquireDeclaredDoor(&bot, &key, 1U, 4) ==
	      SG_COMPOUND_GUARD_OK);
	owner = bot.owner;
	SpawnEntity(3, 1, 0); /* human replacement before retirement hook */
	CHECK(SG_CompoundGuardBotDisconnected(&bot) ==
	      SG_COMPOUND_GUARD_IDENTITY_STALE);
	CHECK(FindOwner(&owner, &record, &ticket));
	CHECK(record.state == SG_MOVER_LEASE_QUARANTINED);
	CHECK(SG_CompoundGuardBotReset(&bot) == SG_COMPOUND_GUARD_OK);
	CHECK(FindOwner(&owner, &record, &ticket));

	ResetGuard();
	Attach(&bot, 2, 3);
	CHECK(SG_CompoundGuardAcquireDeclaredDoor(&bot, &key, 1U, 4) ==
	      SG_COMPOUND_GUARD_OK);
	owner = bot.owner;
	CHECK(SG_CompoundGuardBotReset(&bot) == SG_COMPOUND_GUARD_OK);
	CHECK(FindOwner(&owner, &record, &ticket));
	CHECK(record.state == SG_MOVER_LEASE_QUARANTINED);

	ResetGuard();
	Attach(&bot, 7, 8);
	CHECK(SG_CompoundGuardAcquireDeclaredDoor(&bot, &key, 1U, 21) ==
	      SG_COMPOUND_GUARD_OK);
	owner = bot.owner;
	CHECK(SG_CompoundGuardOrphan(&bot, 0) == SG_COMPOUND_GUARD_OK);
	SpawnEntity(9, 1, 1);
	CHECK(SG_CompoundGuardBotRespawn(&bot, 9) ==
	      SG_COMPOUND_GUARD_IDENTITY_STALE);
	CHECK(FindOwner(&owner, &record, &ticket));
	CHECK(record.state == SG_MOVER_LEASE_QUARANTINED);
}

static void TestOrphanBodyBoltRespawnAndFrame(void)
{
	sg_compound_guard_bot_t bot;
	sg_mover_key_t keys[] = {50U, 60U};
	sg_mover_lease_record_t record;
	sg_mover_ticket_t ticket;
	sg_mover_owner_t owner;
	uint64_t owner_generation;
	sg_compound_guard_frame_stats_t stats;

	ResetGuard();
	Attach(&bot, 3, 4);
	SpawnEntity(80, 1, 0);
	CHECK(SG_CompoundGuardAcquireCompoundPreopen(&bot, keys, 2U, 7, 2U) ==
	      SG_COMPOUND_GUARD_OK);
	owner = bot.owner;
	owner_generation = bot.owner.generation;
	Entity(4)->outside = 0;
	CHECK(SG_CompoundGuardOrphan(&bot, 80) == SG_COMPOUND_GUARD_OK);
	SpawnEntity(90, 0, 1); /* initialized queue incarnation */
	CHECK(SG_CompoundGuardBodyWillReplace(90) == SG_COMPOUND_GUARD_OK);
	SpawnEntity(90, 1, 0);
	CHECK(SG_CompoundGuardBodyDidCopy(&bot, 90) == SG_COMPOUND_GUARD_OK);
	CHECK(FindOwner(&owner, &record, &ticket));
	CHECK(record.state == SG_MOVER_LEASE_ORPHAN &&
	      record.body.kind == SG_MOVER_SUBJECT_BODY_QUEUE &&
	      record.bolt.kind == SG_MOVER_SUBJECT_HOOK_BOLT);

	SpawnEntity(4, 1, 1); /* final respawn link minted a new client generation */
	CHECK(SG_CompoundGuardBotRespawn(&bot, 4) == SG_COMPOUND_GUARD_OK);
	CHECK(bot.owner.generation == owner_generation);
	Entity(80)->present = 0;
	CHECK(SG_CompoundGuardBoltEvicted(&bot, 80) ==
	      SG_COMPOUND_GUARD_OK);
	CHECK(SG_CompoundGuardBotDisconnected(&bot) == SG_COMPOUND_GUARD_OK);
	CHECK(FindOwner(&owner, &record, &ticket));
	CHECK(record.state == SG_MOVER_LEASE_ORPHAN &&
	      record.bolt.kind == SG_MOVER_SUBJECT_NONE);

	SG_CompoundGuardFrame(&stats);
	CHECK(stats.inspected == 1U && stats.held == 1U && stats.released == 0U);
	Entity(90)->outside = 1;
	SG_CompoundGuardFrame(&stats);
	CHECK(stats.inspected == 1U && stats.evicted == 1U &&
	      stats.released == 1U);
	CHECK(!FindOwner(&owner, &record, &ticket));
	CHECK(SG_CompoundGuardValidate(&bot, NULL) ==
	      SG_COMPOUND_GUARD_NO_LEASE);
	CHECK(!SG_MoverTicketValid(&bot.ticket));
	CHECK(SG_CompoundGuardAcquireDeclaredDoor(&bot, keys, 2U, 8) ==
	      SG_COMPOUND_GUARD_OK);
	CHECK(SG_CompoundGuardReleaseProvedClear(&bot) ==
	      SG_COMPOUND_GUARD_OK);
}

static void TestDetachedOrphanAndStaleFailClosed(void)
{
	sg_compound_guard_bot_t bot;
	sg_mover_key_t key = 70U;
	sg_mover_owner_t owner;
	sg_mover_lease_record_t record;
	sg_mover_ticket_t ticket;
	sg_compound_guard_frame_stats_t stats;

	ResetGuard();
	Attach(&bot, 4, 5);
	CHECK(SG_CompoundGuardAcquireDeclaredDoor(&bot, &key, 1U, 9) ==
	      SG_COMPOUND_GUARD_OK);
	owner = bot.owner;
	Entity(5)->outside = 0;
	CHECK(SG_CompoundGuardOrphan(&bot, 0) == SG_COMPOUND_GUARD_OK);
	CHECK(SG_CompoundGuardBotReset(&bot) == SG_COMPOUND_GUARD_OK);
	CHECK(FindOwner(&owner, &record, &ticket));
	SG_CompoundGuardFrame(&stats);
	CHECK(stats.held == 1U && record.state == SG_MOVER_LEASE_ORPHAN);
	Entity(5)->outside = 1;
	SG_CompoundGuardFrame(&stats);
	CHECK(stats.released == 1U && !FindOwner(&owner, &record, &ticket));

	ResetGuard();
	Attach(&bot, 4, 5);
	CHECK(SG_CompoundGuardAcquireDeclaredDoor(&bot, &key, 1U, 9) ==
	      SG_COMPOUND_GUARD_OK);
	owner = bot.owner;
	Entity(5)->outside = 0;
	CHECK(SG_CompoundGuardOrphan(&bot, 0) == SG_COMPOUND_GUARD_OK);
	SpawnEntity(5, 1, 1);
	SG_CompoundGuardFrame(&stats);
	CHECK(stats.quarantined == 1U);
	CHECK(FindOwner(&owner, &record, &ticket));
	CHECK(record.state == SG_MOVER_LEASE_QUARANTINED);

	ResetGuard();
	Attach(&bot, 4, 5);
	CHECK(SG_CompoundGuardAcquireDeclaredDoor(&bot, &key, 1U, 9) ==
	      SG_COMPOUND_GUARD_OK);
	owner = bot.owner;
	Entity(5)->outside = 0;
	CHECK(SG_CompoundGuardOrphan(&bot, 0) == SG_COMPOUND_GUARD_OK);
	Entity(5)->outside_error = 1;
	SG_CompoundGuardFrame(&stats);
	CHECK(stats.quarantined == 1U && stats.host_errors == 1U);
	CHECK(FindOwner(&owner, &record, &ticket));
	CHECK(record.state == SG_MOVER_LEASE_QUARANTINED);
}

static void MakeBodyOrphan(sg_compound_guard_bot_t *bot, int slot,
	int client_key, int body_key, int bolt_key, sg_mover_key_t mover_key)
{
	Attach(bot, slot, client_key);
	if (bolt_key > 0)
		SpawnEntity(bolt_key, 1, 0);
	CHECK(SG_CompoundGuardAcquireDeclaredDoor(bot, &mover_key, 1U,
	      11 + slot) == SG_COMPOUND_GUARD_OK);
	Entity(client_key)->outside = 0;
	CHECK(SG_CompoundGuardOrphan(bot, bolt_key) == SG_COMPOUND_GUARD_OK);
	SpawnEntity(body_key, 0, 1); /* initialized queue incarnation */
	CHECK(SG_CompoundGuardBodyWillReplace(body_key) ==
	      SG_COMPOUND_GUARD_OK);
	SpawnEntity(body_key, 1, 0);
	CHECK(SG_CompoundGuardBodyDidCopy(bot, body_key) ==
	      SG_COMPOUND_GUARD_OK);
}

static void TestBodyReuseTransaction(void)
{
	sg_compound_guard_bot_t old_bot, new_bot;
	sg_mover_owner_t old_owner, new_owner;
	sg_mover_key_t new_key = 91U;
	sg_mover_lease_record_t record;
	sg_mover_ticket_t ticket;
	sg_compound_guard_frame_stats_t stats;

	/* A replacement outside the old sweep releases the old generation only
	 * after DidCopy closes the synchronous transaction. */
	ResetGuard();
	MakeBodyOrphan(&old_bot, 5, 6, 100, 0, 90U);
	old_owner = old_bot.owner;
	CHECK(SG_CompoundGuardBodyWillReplace(100) == SG_COMPOUND_GUARD_OK);
	SpawnEntity(100, 1, 1);
	CHECK(SG_CompoundGuardBodyDidCopy(NULL, 100) ==
	      SG_COMPOUND_GUARD_OK);
	CHECK(!FindOwner(&old_owner, &record, &ticket));

	/* A clear new corpse does not erase a still-intersecting bolt.  Only the
	 * overwritten body is evicted; Frame retains and later clears the bolt. */
	ResetGuard();
	MakeBodyOrphan(&old_bot, 5, 6, 100, 110, 90U);
	old_owner = old_bot.owner;
	CHECK(SG_CompoundGuardBodyWillReplace(100) == SG_COMPOUND_GUARD_OK);
	SpawnEntity(100, 1, 1);
	CHECK(SG_CompoundGuardBodyDidCopy(NULL, 100) ==
	      SG_COMPOUND_GUARD_OK);
	CHECK(FindOwner(&old_owner, &record, &ticket));
	CHECK(record.state == SG_MOVER_LEASE_ORPHAN &&
	      record.body.kind == SG_MOVER_SUBJECT_NONE &&
	      record.bolt.kind == SG_MOVER_SUBJECT_HOOK_BOLT);
	SG_CompoundGuardFrame(&stats);
	CHECK(stats.held == 1U && stats.quarantined == 0U);
	Entity(110)->present = 0;
	SG_CompoundGuardFrame(&stats);
	CHECK(stats.released == 1U && !FindOwner(&old_owner, &record, &ticket));

	/* The same held-old-bolt cleanup is a successful transaction when a newly
	 * dead bot is simultaneously transferred into the replacement body. */
	ResetGuard();
	MakeBodyOrphan(&old_bot, 5, 6, 100, 110, 90U);
	old_owner = old_bot.owner;
	Attach(&new_bot, 6, 7);
	CHECK(SG_CompoundGuardAcquireDeclaredDoor(&new_bot, &new_key, 1U, 18) ==
	      SG_COMPOUND_GUARD_OK);
	new_owner = new_bot.owner;
	Entity(7)->outside = 0;
	CHECK(SG_CompoundGuardOrphan(&new_bot, 0) == SG_COMPOUND_GUARD_OK);
	CHECK(SG_CompoundGuardBodyWillReplace(100) == SG_COMPOUND_GUARD_OK);
	SpawnEntity(100, 1, 1);
	CHECK(SG_CompoundGuardBodyDidCopy(&new_bot, 100) ==
	      SG_COMPOUND_GUARD_OK);
	CHECK(FindOwner(&old_owner, &record, &ticket));
	CHECK(record.state == SG_MOVER_LEASE_ORPHAN &&
	      record.body.kind == SG_MOVER_SUBJECT_NONE &&
	      record.bolt.kind == SG_MOVER_SUBJECT_HOOK_BOLT);
	CHECK(FindOwner(&new_owner, &record, &ticket));
	CHECK(record.state == SG_MOVER_LEASE_ORPHAN &&
	      record.body.kind == SG_MOVER_SUBJECT_BODY_QUEUE);

	/* Old cleanup failure and current transfer have separate outcomes.  The new
	 * corpse is inside the old sweep, so that owner terminalizes, while the
	 * nonoverlapping current claim commits and the one-shot hook returns OK. */
	ResetGuard();
	MakeBodyOrphan(&old_bot, 5, 6, 100, 0, 90U);
	old_owner = old_bot.owner;
	Attach(&new_bot, 6, 7);
	CHECK(SG_CompoundGuardAcquireDeclaredDoor(&new_bot, &new_key, 1U, 18) ==
	      SG_COMPOUND_GUARD_OK);
	new_owner = new_bot.owner;
	Entity(7)->outside = 0;
	CHECK(SG_CompoundGuardOrphan(&new_bot, 0) == SG_COMPOUND_GUARD_OK);
	CHECK(SG_CompoundGuardBodyWillReplace(100) == SG_COMPOUND_GUARD_OK);
	SpawnEntity(100, 1, 0);
	CHECK(SG_CompoundGuardBodyDidCopy(&new_bot, 100) ==
	      SG_COMPOUND_GUARD_OK);
	CHECK(FindOwner(&old_owner, &record, &ticket));
	CHECK(record.state == SG_MOVER_LEASE_QUARANTINED);
	CHECK(FindOwner(&new_owner, &record, &ticket));
	CHECK(record.state == SG_MOVER_LEASE_ORPHAN &&
	      record.body.kind == SG_MOVER_SUBJECT_BODY_QUEUE);

	/* A new solid corpse inside the old sweep cannot be silently rebound from
	 * BODY_QUEUE to BODY_QUEUE, so ownership becomes terminal for the level. */
	ResetGuard();
	MakeBodyOrphan(&old_bot, 5, 6, 100, 0, 90U);
	old_owner = old_bot.owner;
	CHECK(SG_CompoundGuardBodyWillReplace(100) == SG_COMPOUND_GUARD_OK);
	SpawnEntity(100, 1, 0);
	CHECK(SG_CompoundGuardBodyDidCopy(NULL, 100) ==
	      SG_COMPOUND_GUARD_NOT_CLEAR);
	CHECK(FindOwner(&old_owner, &record, &ticket));
	CHECK(record.state == SG_MOVER_LEASE_QUARANTINED);

	/* Even the first guarded use of a queue slot requires the generation minted
	 * by the overwrite; pose changes cannot stand in for identity advance. */
	ResetGuard();
	Attach(&new_bot, 6, 7);
	CHECK(SG_CompoundGuardAcquireDeclaredDoor(&new_bot, &new_key, 1U, 18) ==
	      SG_COMPOUND_GUARD_OK);
	new_owner = new_bot.owner;
	Entity(7)->outside = 0;
	CHECK(SG_CompoundGuardOrphan(&new_bot, 0) == SG_COMPOUND_GUARD_OK);
	SpawnEntity(100, 0, 1);
	CHECK(SG_CompoundGuardBodyWillReplace(100) == SG_COMPOUND_GUARD_OK);
	Entity(100)->solid = 1;
	Entity(100)->outside = 0;
	CHECK(SG_CompoundGuardBodyDidCopy(&new_bot, 100) ==
	      SG_COMPOUND_GUARD_IDENTITY_STALE);
	CHECK(FindOwner(&new_owner, &record, &ticket));
	CHECK(record.state == SG_MOVER_LEASE_QUARANTINED);

	/* Reuse without an identity-generation advance is an ABA fault even when
	 * the new pose appears clear. */
	ResetGuard();
	MakeBodyOrphan(&old_bot, 5, 6, 100, 0, 90U);
	old_owner = old_bot.owner;
	CHECK(SG_CompoundGuardBodyWillReplace(100) == SG_COMPOUND_GUARD_OK);
	Entity(100)->outside = 1; /* deliberately do not mint a new generation */
	CHECK(SG_CompoundGuardBodyDidCopy(NULL, 100) ==
	      SG_COMPOUND_GUARD_IDENTITY_STALE);
	CHECK(FindOwner(&old_owner, &record, &ticket));
	CHECK(record.state == SG_MOVER_LEASE_QUARANTINED);

	/* A missing/mismatched transaction completion fails the parked owner
	 * closed; it can never be mistaken for the next queue occupant. */
	ResetGuard();
	MakeBodyOrphan(&old_bot, 5, 6, 100, 0, 90U);
	old_owner = old_bot.owner;
	CHECK(SG_CompoundGuardBodyWillReplace(100) == SG_COMPOUND_GUARD_OK);
	CHECK(SG_CompoundGuardBodyDidCopy(NULL, 101) ==
	      SG_COMPOUND_GUARD_INVALID_TRANSITION);
	CHECK(FindOwner(&old_owner, &record, &ticket));
	CHECK(record.state == SG_MOVER_LEASE_QUARANTINED);

	/* The transaction is synchronous.  If the next SG frame sees it still
	 * open, DidCopy was omitted and the captured owner becomes terminal. */
	ResetGuard();
	MakeBodyOrphan(&old_bot, 5, 6, 100, 0, 90U);
	old_owner = old_bot.owner;
	CHECK(SG_CompoundGuardBodyWillReplace(100) == SG_COMPOUND_GUARD_OK);
	SG_CompoundGuardFrame(&stats);
	CHECK(stats.quarantined == 1U);
	CHECK(FindOwner(&old_owner, &record, &ticket));
	CHECK(record.state == SG_MOVER_LEASE_QUARANTINED);
}

static void TestLevelResetAndReasons(void)
{
	sg_compound_guard_bot_t bot;
	sg_mover_key_t key = 120U;
	sg_mover_lease_record_t record;
	sg_mover_ticket_t ticket;
	sg_mover_owner_t owner;

	ResetGuard();
	Attach(&bot, 6, 7);
	CHECK(SG_CompoundGuardAcquireDeclaredDoor(&bot, &key, 1U, 20) ==
	      SG_COMPOUND_GUARD_OK);
	CHECK(SG_CompoundGuardLevelReset() == SG_COMPOUND_GUARD_OK);
	CHECK(SG_CompoundGuardValidate(&bot, &record) ==
	      SG_COMPOUND_GUARD_NO_LEASE);
	CHECK(!SG_MoverTicketValid(&bot.ticket));
	CHECK(SG_CompoundGuardBotReset(&bot) == SG_COMPOUND_GUARD_OK);
	CHECK(strcmp(SG_CompoundGuardReason(SG_COMPOUND_GUARD_NOT_CLEAR),
	      "mover sweep is not proved clear") == 0);

	/* Generic lifecycle hooks are harmless for an attached bot that never
	 * acquired mover authority. */
	Attach(&bot, 7, 8);
	CHECK(SG_CompoundGuardOrphan(&bot, 0) == SG_COMPOUND_GUARD_OK);
	SpawnEntity(130, 0, 1);
	CHECK(SG_CompoundGuardBodyWillReplace(130) == SG_COMPOUND_GUARD_OK);
	SpawnEntity(130, 1, 0);
	CHECK(SG_CompoundGuardBodyDidCopy(&bot, 130) ==
	      SG_COMPOUND_GUARD_OK);
	Entity(8)->present = 0;
	CHECK(SG_CompoundGuardBotDisconnected(&bot) ==
	      SG_COMPOUND_GUARD_OK);

	/* Respawn is a lifecycle generation edge, not an identity refresh that may
	 * alias the old life.  A missed host-generation mint fails closed. */
	ResetGuard();
	Attach(&bot, 7, 8);
	CHECK(SG_CompoundGuardAcquireDeclaredDoor(&bot, &key, 1U, 21) ==
	      SG_COMPOUND_GUARD_OK);
	owner = bot.owner;
	CHECK(SG_CompoundGuardOrphan(&bot, 0) == SG_COMPOUND_GUARD_OK);
	CHECK(SG_CompoundGuardBotRespawn(&bot, 8) ==
	      SG_COMPOUND_GUARD_IDENTITY_STALE);
	CHECK(FindOwner(&owner, &record, &ticket));
	CHECK(record.state == SG_MOVER_LEASE_QUARANTINED);
}

int main(void)
{
	TestSharedAcquirePauseAndClear();
	TestIdentityDisconnectAndReset();
	TestOrphanBodyBoltRespawnAndFrame();
	TestDetachedOrphanAndStaleFailClosed();
	TestBodyReuseTransaction();
	TestLevelResetAndReasons();
	if (failures)
	{
		fprintf(stderr, "sg_compound_guard_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("sg_compound_guard_test: ok");
	return 0;
}
