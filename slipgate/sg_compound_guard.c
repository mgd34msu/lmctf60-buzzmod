/* sg_compound_guard.c -- game-boundary adapter for shared mover leases. */
#include "sg_compound_guard.h"

#include <limits.h>
#include <string.h>

typedef struct sg_compound_guard_singleton_s
{
	sg_mover_lease_registry_t leases;
	sg_compound_guard_host_t host;
	uint64_t next_owner_generation;
	uint8_t initialized;
	uint8_t owner_generation_exhausted;
} sg_compound_guard_singleton_t;

typedef enum subject_observation_e
{
	SUBJECT_OBSERVATION_CLEAR = 0,
	SUBJECT_OBSERVATION_HELD,
	SUBJECT_OBSERVATION_ABSENT,
	SUBJECT_OBSERVATION_STALE,
	SUBJECT_OBSERVATION_ERROR
} subject_observation_t;

typedef struct sg_compound_guard_reuse_entry_s
{
	sg_mover_owner_t owner;
	sg_mover_ticket_t ticket;
	sg_mover_lease_record_t record;
} sg_compound_guard_reuse_entry_t;

typedef struct sg_compound_guard_reuse_s
{
	uint64_t source_generation;
	int32_t body_edict_key;
	uint8_t active;
	uint8_t reserved[3];
	size_t count;
	sg_compound_guard_reuse_entry_t entries[SG_MOVER_LEASE_MAX_RECORDS];
} sg_compound_guard_reuse_t;

static sg_compound_guard_singleton_t guard;
static sg_compound_guard_reuse_t body_reuse;

static sg_compound_guard_result_t GuardResult(
	sg_mover_lease_result_t result)
{
	switch (result) {
	case SG_MOVER_LEASE_OK:
		return SG_COMPOUND_GUARD_OK;
	case SG_MOVER_LEASE_INVALID_ARGUMENT:
		return SG_COMPOUND_GUARD_INVALID_ARGUMENT;
	case SG_MOVER_LEASE_INVALID_OWNER:
		return SG_COMPOUND_GUARD_INVALID_OWNER;
	case SG_MOVER_LEASE_INVALID_SUBJECT:
		return SG_COMPOUND_GUARD_INVALID_SUBJECT;
	case SG_MOVER_LEASE_INVALID_KEYS:
		return SG_COMPOUND_GUARD_INVALID_KEYS;
	case SG_MOVER_LEASE_CONFLICT:
		return SG_COMPOUND_GUARD_CONFLICT;
	case SG_MOVER_LEASE_OWNER_BUSY:
		return SG_COMPOUND_GUARD_OWNER_BUSY;
	case SG_MOVER_LEASE_FULL:
		return SG_COMPOUND_GUARD_FULL;
	case SG_MOVER_LEASE_EXHAUSTED:
		return SG_COMPOUND_GUARD_EXHAUSTED;
	case SG_MOVER_LEASE_STALE_TICKET:
		return SG_COMPOUND_GUARD_STALE_TICKET;
	case SG_MOVER_LEASE_OWNER_MISMATCH:
		return SG_COMPOUND_GUARD_OWNER_MISMATCH;
	case SG_MOVER_LEASE_SUBJECT_MISMATCH:
		return SG_COMPOUND_GUARD_SUBJECT_MISMATCH;
	case SG_MOVER_LEASE_INVALID_TRANSITION:
		return SG_COMPOUND_GUARD_INVALID_TRANSITION;
	case SG_MOVER_LEASE_QUARANTINE_LOCKED:
		return SG_COMPOUND_GUARD_QUARANTINE_LOCKED;
	default:
		return SG_COMPOUND_GUARD_HOST_ERROR;
	}
}

static int HostValid(const sg_compound_guard_host_t *host)
{
	return host && host->identity && host->solid && host->outside_sweep;
}

static int ReservedZero(const uint8_t *reserved, size_t count)
{
	size_t index;

	if (!reserved)
		return 0;
	for (index = 0U; index < count; index++)
		if (reserved[index] != 0U)
			return 0;
	return 1;
}

static int BotAttached(const sg_compound_guard_bot_t *bot)
{
	return bot && bot->attached == 1U &&
	       ReservedZero(bot->reserved, sizeof(bot->reserved)) &&
	       SG_MoverOwnerValid(&bot->owner) &&
	       SG_MoverSubjectValid(&bot->client) &&
	       bot->client.kind == SG_MOVER_SUBJECT_CLIENT;
}

static void BotClear(sg_compound_guard_bot_t *bot)
{
	if (!bot)
		return;
	memset(bot, 0, sizeof(*bot));
	SG_MoverTicketClear(&bot->ticket);
}

static int SubjectEmpty(const sg_mover_subject_t *subject)
{
	return subject && subject->generation == 0U &&
	       subject->edict_key == 0 &&
	       subject->kind == SG_MOVER_SUBJECT_NONE &&
	       ReservedZero(subject->reserved, sizeof(subject->reserved));
}

static int KeysValid(const sg_mover_key_t *keys, size_t key_count)
{
	size_t index;

	if (!keys || key_count == 0U ||
	    key_count > SG_MOVER_LEASE_MAX_KEYS || keys[0] == 0U)
		return 0;
	for (index = 1U; index < key_count; index++)
		if (keys[index] == 0U || keys[index - 1U] >= keys[index])
			return 0;
	return 1;
}

static sg_compound_guard_result_t SnapshotSubject(
	sg_mover_subject_kind_t kind, int32_t edict_key,
	sg_mover_subject_t *subject_out)
{
	uint64_t generation = 0U;
	sg_compound_guard_observation_t observation;

	if (subject_out)
		memset(subject_out, 0, sizeof(*subject_out));
	if (!guard.initialized)
		return SG_COMPOUND_GUARD_NOT_INITIALIZED;
	if (!subject_out || edict_key <= 0 ||
	    kind < SG_MOVER_SUBJECT_CLIENT ||
	    kind > SG_MOVER_SUBJECT_HOOK_BOLT)
		return SG_COMPOUND_GUARD_INVALID_ARGUMENT;
	observation = guard.host.identity(guard.host.context, edict_key,
		&generation);
	if (observation == SG_COMPOUND_GUARD_NO)
		return SG_COMPOUND_GUARD_ENTITY_ABSENT;
	if (observation != SG_COMPOUND_GUARD_YES || generation == 0U)
		return SG_COMPOUND_GUARD_HOST_ERROR;
	subject_out->kind = (uint8_t)kind;
	subject_out->edict_key = edict_key;
	subject_out->generation = generation;
	return SG_COMPOUND_GUARD_OK;
}

static sg_compound_guard_result_t SubjectIdentityCurrent(
	const sg_mover_subject_t *subject)
{
	uint64_t generation = 0U;
	sg_compound_guard_observation_t observation;

	if (!SG_MoverSubjectValid(subject))
		return SG_COMPOUND_GUARD_INVALID_SUBJECT;
	observation = guard.host.identity(guard.host.context,
		subject->edict_key, &generation);
	if (observation == SG_COMPOUND_GUARD_NO)
		return SG_COMPOUND_GUARD_IDENTITY_STALE;
	if (observation != SG_COMPOUND_GUARD_YES || generation == 0U)
		return SG_COMPOUND_GUARD_HOST_ERROR;
	if (generation != subject->generation)
		return SG_COMPOUND_GUARD_IDENTITY_STALE;
	return SG_COMPOUND_GUARD_OK;
}

static subject_observation_t ObserveSubject(
	const sg_mover_lease_record_t *record,
	const sg_mover_subject_t *subject)
{
	uint64_t generation = 0U;
	sg_compound_guard_observation_t observation;

	if (!record || !SG_MoverSubjectValid(subject) ||
	    record->key_count == 0U ||
	    record->key_count > SG_MOVER_LEASE_MAX_KEYS)
		return SUBJECT_OBSERVATION_ERROR;
	observation = guard.host.identity(guard.host.context,
		subject->edict_key, &generation);
	if (observation == SG_COMPOUND_GUARD_NO)
		return SUBJECT_OBSERVATION_ABSENT;
	if (observation != SG_COMPOUND_GUARD_YES || generation == 0U)
		return SUBJECT_OBSERVATION_ERROR;
	if (generation != subject->generation)
		return SUBJECT_OBSERVATION_STALE;
	observation = guard.host.solid(guard.host.context, subject);
	if (observation == SG_COMPOUND_GUARD_NO)
		return SUBJECT_OBSERVATION_CLEAR;
	if (observation != SG_COMPOUND_GUARD_YES)
		return SUBJECT_OBSERVATION_ERROR;
	observation = guard.host.outside_sweep(guard.host.context, subject,
		record->keys, record->key_count);
	if (observation == SG_COMPOUND_GUARD_YES)
		return SUBJECT_OBSERVATION_CLEAR;
	if (observation == SG_COMPOUND_GUARD_NO)
		return SUBJECT_OBSERVATION_HELD;
	return SUBJECT_OBSERVATION_ERROR;
}

static sg_compound_guard_result_t QuarantineTicket(
	const sg_mover_ticket_t *ticket, const sg_mover_owner_t *owner)
{
	return GuardResult(SG_MoverLeaseQuarantine(&guard.leases, ticket,
		owner));
}

static int FindOwnerTicket(const sg_mover_owner_t *owner,
	sg_mover_ticket_t *ticket_out, sg_mover_lease_record_t *record_out)
{
	size_t slot;
	sg_mover_ticket_t ticket;
	sg_mover_lease_record_t record;

	SG_MoverTicketClear(ticket_out);
	if (record_out)
		memset(record_out, 0, sizeof(*record_out));
	if (!SG_MoverOwnerValid(owner) || !ticket_out)
		return 0;
	for (slot = 0U; slot < SG_MOVER_LEASE_MAX_RECORDS; slot++)
	{
		if (!SG_MoverLeaseRecordAt(&guard.leases, slot, &record, &ticket))
			continue;
		if (!SG_MoverOwnerEqual(owner, &record.owner))
			continue;
		*ticket_out = ticket;
		if (record_out)
			*record_out = record;
		return 1;
	}
	return 0;
}

/* Frame may rotate an orphan ticket after its bot handle was parked.  Rebind
 * only by the registry's generation-safe owner; never by slot number alone. */
static sg_compound_guard_result_t SyncTicket(
	sg_compound_guard_bot_t *bot, sg_mover_lease_record_t *record_out)
{
	sg_mover_lease_result_t lease_result;

	if (record_out)
		memset(record_out, 0, sizeof(*record_out));
	if (!BotAttached(bot))
		return SG_COMPOUND_GUARD_NOT_ATTACHED;
	if (!SG_MoverTicketValid(&bot->ticket))
		return SG_COMPOUND_GUARD_NO_LEASE;
	lease_result = SG_MoverLeaseValidate(&guard.leases, &bot->ticket,
		&bot->owner, record_out);
	if (lease_result == SG_MOVER_LEASE_OK)
		return SG_COMPOUND_GUARD_OK;
	if (lease_result != SG_MOVER_LEASE_STALE_TICKET)
		return GuardResult(lease_result);
	if (FindOwnerTicket(&bot->owner, &bot->ticket, record_out))
		return SG_COMPOUND_GUARD_OK;
	SG_MoverTicketClear(&bot->ticket);
	return SG_COMPOUND_GUARD_NO_LEASE;
}

static sg_compound_guard_result_t ValidateCurrentBot(
	sg_compound_guard_bot_t *bot, sg_mover_lease_record_t *record_out)
{
	sg_mover_lease_record_t local_record;
	sg_mover_lease_record_t *record = record_out ? record_out : &local_record;
	sg_compound_guard_result_t result;

	if (!guard.initialized)
		return SG_COMPOUND_GUARD_NOT_INITIALIZED;
	result = SyncTicket(bot, record);
	if (result != SG_COMPOUND_GUARD_OK)
		return result;
	if (record->state == SG_MOVER_LEASE_QUARANTINED)
		return SG_COMPOUND_GUARD_QUARANTINE_LOCKED;
	result = SubjectIdentityCurrent(&bot->client);
	if (result == SG_COMPOUND_GUARD_IDENTITY_STALE ||
	    result == SG_COMPOUND_GUARD_HOST_ERROR)
		(void)QuarantineTicket(&bot->ticket, &bot->owner);
	return result;
}

sg_compound_guard_result_t SG_CompoundGuardInit(
	const sg_compound_guard_host_t *host)
{
	if (!HostValid(host))
		return SG_COMPOUND_GUARD_INVALID_ARGUMENT;
	if (!guard.initialized)
	{
		memset(&guard.leases, 0, sizeof(guard.leases));
		SG_MoverLeaseInit(&guard.leases);
		guard.initialized = 1U;
	}
	else
	{
		SG_MoverLeaseLevelReset(&guard.leases);
	}
	memset(&body_reuse, 0, sizeof(body_reuse));
	guard.host = *host;
	return SG_COMPOUND_GUARD_OK;
}

sg_compound_guard_result_t SG_CompoundGuardLevelReset(void)
{
	if (!guard.initialized)
		return SG_COMPOUND_GUARD_NOT_INITIALIZED;
	memset(&body_reuse, 0, sizeof(body_reuse));
	SG_MoverLeaseLevelReset(&guard.leases);
	return SG_COMPOUND_GUARD_OK;
}

sg_compound_guard_result_t SG_CompoundGuardBotAttach(
	sg_compound_guard_bot_t *bot, int32_t bot_slot,
	int32_t client_edict_key)
{
	sg_mover_subject_t client;
	sg_compound_guard_result_t result;

	if (!guard.initialized)
		return SG_COMPOUND_GUARD_NOT_INITIALIZED;
	if (!bot || bot_slot < 0 || bot->attached ||
	    SG_MoverTicketValid(&bot->ticket))
		return SG_COMPOUND_GUARD_INVALID_ARGUMENT;
	result = SnapshotSubject(SG_MOVER_SUBJECT_CLIENT, client_edict_key,
		&client);
	if (result != SG_COMPOUND_GUARD_OK)
		return result;
	if (guard.owner_generation_exhausted ||
	    guard.next_owner_generation == UINT64_MAX)
	{
		guard.owner_generation_exhausted = 1U;
		return SG_COMPOUND_GUARD_EXHAUSTED;
	}
	BotClear(bot);
	guard.next_owner_generation++;
	bot->owner.kind = SG_MOVER_OWNER_BOT;
	bot->owner.id = bot_slot;
	bot->owner.generation = guard.next_owner_generation;
	bot->client = client;
	bot->attached = 1U;
	return SG_COMPOUND_GUARD_OK;
}

sg_compound_guard_result_t SG_CompoundGuardBotRespawn(
	sg_compound_guard_bot_t *bot, int32_t client_edict_key)
{
	sg_mover_subject_t client;
	sg_mover_lease_record_t record = {0};
	sg_compound_guard_result_t result;
	sg_compound_guard_result_t lease_status = SG_COMPOUND_GUARD_NO_LEASE;

	if (!guard.initialized)
		return SG_COMPOUND_GUARD_NOT_INITIALIZED;
	if (!BotAttached(bot))
		return SG_COMPOUND_GUARD_NOT_ATTACHED;
	if (SG_MoverTicketValid(&bot->ticket))
	{
		lease_status = SyncTicket(bot, &record);
		if (lease_status != SG_COMPOUND_GUARD_OK &&
		    lease_status != SG_COMPOUND_GUARD_NO_LEASE)
			return lease_status;
	}
	result = SnapshotSubject(SG_MOVER_SUBJECT_CLIENT, client_edict_key,
		&client);
	if (result != SG_COMPOUND_GUARD_OK)
	{
		if (lease_status == SG_COMPOUND_GUARD_OK)
			(void)QuarantineTicket(&bot->ticket, &bot->owner);
		return result;
	}
	if (client.edict_key != bot->client.edict_key ||
	    client.generation == bot->client.generation)
	{
		if (lease_status == SG_COMPOUND_GUARD_OK)
			(void)QuarantineTicket(&bot->ticket, &bot->owner);
		return SG_COMPOUND_GUARD_IDENTITY_STALE;
	}
	if (lease_status == SG_COMPOUND_GUARD_OK)
	{
		if (record.state != SG_MOVER_LEASE_ORPHAN &&
		    record.state != SG_MOVER_LEASE_QUARANTINED)
		{
			(void)QuarantineTicket(&bot->ticket, &bot->owner);
			return SG_COMPOUND_GUARD_INVALID_TRANSITION;
		}
	}
	bot->client = client;
	return SG_COMPOUND_GUARD_OK;
}

sg_compound_guard_result_t SG_CompoundGuardBotReset(
	sg_compound_guard_bot_t *bot)
{
	sg_mover_lease_record_t record;
	sg_mover_lease_result_t lease_result;
	sg_compound_guard_result_t result = SG_COMPOUND_GUARD_OK;

	if (!bot)
		return SG_COMPOUND_GUARD_INVALID_ARGUMENT;
	if (!guard.initialized)
	{
		BotClear(bot);
		return SG_COMPOUND_GUARD_NOT_INITIALIZED;
	}
	if (SG_MoverOwnerValid(&bot->owner) &&
	    SG_MoverTicketValid(&bot->ticket))
	{
		lease_result = SG_MoverLeaseValidate(&guard.leases, &bot->ticket,
			&bot->owner, &record);
		if (lease_result == SG_MOVER_LEASE_STALE_TICKET &&
		    FindOwnerTicket(&bot->owner, &bot->ticket, &record))
			lease_result = SG_MOVER_LEASE_OK;
		if (lease_result == SG_MOVER_LEASE_OK &&
		    (record.state == SG_MOVER_LEASE_ACTIVE ||
		     record.state == SG_MOVER_LEASE_PAUSED))
			result = QuarantineTicket(&bot->ticket, &bot->owner);
		else if (lease_result != SG_MOVER_LEASE_OK &&
		         lease_result != SG_MOVER_LEASE_STALE_TICKET)
			result = GuardResult(lease_result);
	}
	BotClear(bot);
	return result;
}

sg_compound_guard_result_t SG_CompoundGuardBotDisconnected(
	sg_compound_guard_bot_t *bot)
{
	sg_mover_lease_record_t record;
	subject_observation_t observation;
	sg_mover_lease_result_t lease_result;
	sg_compound_guard_result_t result;

	if (!guard.initialized)
		return SG_COMPOUND_GUARD_NOT_INITIALIZED;
	if (!BotAttached(bot))
		return SG_COMPOUND_GUARD_NOT_ATTACHED;
	if (!SG_MoverTicketValid(&bot->ticket))
		return SG_COMPOUND_GUARD_OK;
	result = SyncTicket(bot, &record);
	if (result == SG_COMPOUND_GUARD_NO_LEASE)
		return SG_COMPOUND_GUARD_OK;
	if (result != SG_COMPOUND_GUARD_OK)
		return result;
	if (record.state == SG_MOVER_LEASE_ORPHAN)
		return SG_COMPOUND_GUARD_OK;
	if (record.state == SG_MOVER_LEASE_QUARANTINED)
		return SG_COMPOUND_GUARD_QUARANTINE_LOCKED;
	if (record.state != SG_MOVER_LEASE_ACTIVE &&
	    record.state != SG_MOVER_LEASE_PAUSED)
		return SG_COMPOUND_GUARD_INVALID_TRANSITION;
	observation = ObserveSubject(&record, &bot->client);
	if (observation == SUBJECT_OBSERVATION_STALE ||
	    observation == SUBJECT_OBSERVATION_ERROR)
	{
		(void)QuarantineTicket(&bot->ticket, &bot->owner);
		return observation == SUBJECT_OBSERVATION_STALE
		    ? SG_COMPOUND_GUARD_IDENTITY_STALE
		    : SG_COMPOUND_GUARD_HOST_ERROR;
	}
	if (observation == SUBJECT_OBSERVATION_HELD)
	{
		(void)QuarantineTicket(&bot->ticket, &bot->owner);
		return SG_COMPOUND_GUARD_NOT_CLEAR;
	}
	/* ABSENT is expected after a correctly ordered disconnect.  The captured
	 * generation is still protected: a replacement reports STALE above. */
	lease_result = SG_MoverLeaseReleaseProvedClear(&guard.leases,
		&bot->ticket, &bot->owner);
	if (lease_result == SG_MOVER_LEASE_OK)
		SG_MoverTicketClear(&bot->ticket);
	return GuardResult(lease_result);
}

static sg_compound_guard_result_t Acquire(
	sg_compound_guard_bot_t *bot, const sg_mover_key_t *keys,
	size_t key_count, sg_mover_lease_law_t law, int link_index,
	uint32_t mechanism_index)
{
	sg_mover_lease_record_t record;
	sg_mover_lease_result_t lease_result;
	sg_compound_guard_observation_t observation;
	sg_compound_guard_result_t result;

	if (!guard.initialized)
		return SG_COMPOUND_GUARD_NOT_INITIALIZED;
	if (!BotAttached(bot))
		return SG_COMPOUND_GUARD_NOT_ATTACHED;
	if (!KeysValid(keys, key_count))
		return SG_COMPOUND_GUARD_INVALID_KEYS;
	if (link_index < 0)
		return SG_COMPOUND_GUARD_INVALID_ARGUMENT;
	if (SG_MoverTicketValid(&bot->ticket))
	{
		result = SyncTicket(bot, &record);
		if (result == SG_COMPOUND_GUARD_OK)
			return SG_COMPOUND_GUARD_OWNER_BUSY;
		if (result != SG_COMPOUND_GUARD_NO_LEASE)
			return result;
	}
	result = SubjectIdentityCurrent(&bot->client);
	if (result != SG_COMPOUND_GUARD_OK)
		return result;
	observation = guard.host.solid(guard.host.context, &bot->client);
	if (observation != SG_COMPOUND_GUARD_YES)
		return observation == SG_COMPOUND_GUARD_NO
		    ? SG_COMPOUND_GUARD_NOT_CLEAR
		    : SG_COMPOUND_GUARD_HOST_ERROR;
	observation = guard.host.outside_sweep(guard.host.context, &bot->client,
		keys, key_count);
	if (observation != SG_COMPOUND_GUARD_YES)
		return observation == SG_COMPOUND_GUARD_NO
		    ? SG_COMPOUND_GUARD_NOT_CLEAR
		    : SG_COMPOUND_GUARD_HOST_ERROR;
	/* Solid/outside callbacks are host code.  Recheck the exact generation so
	 * an edict recycled during observation cannot inherit the acquisition. */
	result = SubjectIdentityCurrent(&bot->client);
	if (result != SG_COMPOUND_GUARD_OK)
		return result;
	lease_result = SG_MoverLeaseAcquire(&guard.leases, keys, key_count,
		&bot->owner, law, link_index, mechanism_index, &bot->ticket);
	return GuardResult(lease_result);
}

sg_compound_guard_result_t SG_CompoundGuardAcquireDeclaredDoor(
	sg_compound_guard_bot_t *bot, const sg_mover_key_t *keys,
	size_t key_count, int link_index)
{
	return Acquire(bot, keys, key_count, SG_MOVER_LAW_DECLARED_DOOR,
		link_index, 0U);
}

sg_compound_guard_result_t SG_CompoundGuardAcquireCompoundPreopen(
	sg_compound_guard_bot_t *bot, const sg_mover_key_t *keys,
	size_t key_count, int link_index, uint32_t mechanism_index)
{
	return Acquire(bot, keys, key_count, SG_MOVER_LAW_COMPOUND_PREOPEN,
		link_index, mechanism_index);
}

sg_compound_guard_result_t SG_CompoundGuardValidate(
	sg_compound_guard_bot_t *bot, sg_mover_lease_record_t *record_out)
{
	if (record_out)
		memset(record_out, 0, sizeof(*record_out));
	return ValidateCurrentBot(bot, record_out);
}

sg_compound_guard_result_t SG_CompoundGuardAuthorize(
	sg_compound_guard_bot_t *bot, sg_mover_lease_law_t expected_law,
	const sg_mover_key_t *expected_keys, size_t expected_key_count,
	int expected_link_index, uint32_t expected_mechanism_index)
{
	sg_mover_lease_record_t record;
	sg_compound_guard_result_t result;

	if (!KeysValid(expected_keys, expected_key_count))
		return SG_COMPOUND_GUARD_INVALID_KEYS;
	if (expected_link_index < 0 ||
	    (expected_law != SG_MOVER_LAW_DECLARED_DOOR &&
	     expected_law != SG_MOVER_LAW_COMPOUND_PREOPEN))
		return SG_COMPOUND_GUARD_INVALID_ARGUMENT;
	result = ValidateCurrentBot(bot, &record);
	if (result != SG_COMPOUND_GUARD_OK)
		return result;
	if (record.state != SG_MOVER_LEASE_ACTIVE)
		return SG_COMPOUND_GUARD_INVALID_TRANSITION;
	if (record.law != (uint8_t)expected_law ||
	    record.link_index != expected_link_index ||
	    record.mechanism_index != expected_mechanism_index ||
	    record.key_count != expected_key_count ||
	    memcmp(record.keys, expected_keys,
	           expected_key_count * sizeof(expected_keys[0])) != 0)
		return SG_COMPOUND_GUARD_AUTHORITY_MISMATCH;
	return SG_COMPOUND_GUARD_OK;
}

static sg_compound_guard_result_t SetState(sg_compound_guard_bot_t *bot,
	sg_mover_lease_state_t state)
{
	sg_compound_guard_result_t result;

	result = ValidateCurrentBot(bot, NULL);
	if (result != SG_COMPOUND_GUARD_OK)
		return result;
	return GuardResult(SG_MoverLeaseSetState(&guard.leases, &bot->ticket,
		&bot->owner, state));
}

sg_compound_guard_result_t SG_CompoundGuardPause(
	sg_compound_guard_bot_t *bot)
{
	return SetState(bot, SG_MOVER_LEASE_PAUSED);
}

sg_compound_guard_result_t SG_CompoundGuardResume(
	sg_compound_guard_bot_t *bot)
{
	return SetState(bot, SG_MOVER_LEASE_ACTIVE);
}

sg_compound_guard_result_t SG_CompoundGuardOrphan(
	sg_compound_guard_bot_t *bot, int32_t bolt_edict_key)
{
	sg_mover_subject_t bolt;
	const sg_mover_subject_t *bolt_ptr = NULL;
	sg_mover_lease_result_t lease_result;
	sg_compound_guard_result_t result;

	if (!guard.initialized)
		return SG_COMPOUND_GUARD_NOT_INITIALIZED;
	if (!BotAttached(bot))
		return SG_COMPOUND_GUARD_NOT_ATTACHED;
	if (!SG_MoverTicketValid(&bot->ticket))
		return SG_COMPOUND_GUARD_OK;
	result = ValidateCurrentBot(bot, NULL);
	if (result != SG_COMPOUND_GUARD_OK)
		return result;
	if (bolt_edict_key < 0)
	{
		(void)QuarantineTicket(&bot->ticket, &bot->owner);
		return SG_COMPOUND_GUARD_INVALID_ARGUMENT;
	}
	if (bolt_edict_key > 0)
	{
		result = SnapshotSubject(SG_MOVER_SUBJECT_HOOK_BOLT,
			bolt_edict_key, &bolt);
		if (result != SG_COMPOUND_GUARD_OK)
		{
			(void)QuarantineTicket(&bot->ticket, &bot->owner);
			return result;
		}
		bolt_ptr = &bolt;
	}
	lease_result = SG_MoverLeaseOrphan(&guard.leases, &bot->ticket,
		&bot->owner, &bot->client, bolt_ptr, &bot->ticket);
	if (lease_result != SG_MOVER_LEASE_OK)
		(void)QuarantineTicket(&bot->ticket, &bot->owner);
	return GuardResult(lease_result);
}

static sg_compound_guard_result_t QuarantineObservation(
	const sg_mover_ticket_t *ticket, const sg_mover_owner_t *owner,
	subject_observation_t observation)
{
	(void)QuarantineTicket(ticket, owner);
	if (observation == SUBJECT_OBSERVATION_STALE)
		return SG_COMPOUND_GUARD_IDENTITY_STALE;
	return SG_COMPOUND_GUARD_HOST_ERROR;
}

static void AbortBodyReuse(void)
{
	size_t index;

	if (!body_reuse.active)
		return;
	for (index = 0U; index < body_reuse.count; index++)
		(void)QuarantineTicket(&body_reuse.entries[index].ticket,
			&body_reuse.entries[index].owner);
	memset(&body_reuse, 0, sizeof(body_reuse));
}

sg_compound_guard_result_t SG_CompoundGuardBodyWillReplace(
	int32_t body_edict_key)
{
	size_t slot;
	int abandoned = 0;
	sg_mover_lease_record_t record;
	sg_mover_ticket_t ticket;
	uint64_t source_generation = 0U;
	sg_compound_guard_observation_t observation;
	sg_compound_guard_result_t identity_result;
	sg_compound_guard_result_t final_result = SG_COMPOUND_GUARD_OK;

	if (!guard.initialized)
		return SG_COMPOUND_GUARD_NOT_INITIALIZED;
	if (body_edict_key <= 0)
		return SG_COMPOUND_GUARD_INVALID_ARGUMENT;
	if (body_reuse.active)
	{
		AbortBodyReuse();
		abandoned = 1;
	}
	memset(&body_reuse, 0, sizeof(body_reuse));
	body_reuse.active = 1U;
	body_reuse.body_edict_key = body_edict_key;
	observation = guard.host.identity(guard.host.context, body_edict_key,
		&source_generation);
	if (observation == SG_COMPOUND_GUARD_YES && source_generation != 0U)
		body_reuse.source_generation = source_generation;
	else if (observation == SG_COMPOUND_GUARD_NO)
		final_result = SG_COMPOUND_GUARD_ENTITY_ABSENT;
	else
		final_result = SG_COMPOUND_GUARD_HOST_ERROR;
	for (slot = 0U; slot < SG_MOVER_LEASE_MAX_RECORDS; slot++)
	{
		if (!SG_MoverLeaseRecordAt(&guard.leases, slot, &record, &ticket) ||
		    record.state != SG_MOVER_LEASE_ORPHAN ||
		    record.body.kind != SG_MOVER_SUBJECT_BODY_QUEUE ||
		    record.body.edict_key != body_edict_key)
			continue;
		identity_result = SubjectIdentityCurrent(&record.body);
		if (identity_result != SG_COMPOUND_GUARD_OK)
		{
			(void)QuarantineTicket(&ticket, &record.owner);
			final_result = identity_result;
			continue;
		}
		body_reuse.entries[body_reuse.count].owner = record.owner;
		body_reuse.entries[body_reuse.count].ticket = ticket;
		body_reuse.entries[body_reuse.count].record = record;
		body_reuse.count++;
	}
	return abandoned ? SG_COMPOUND_GUARD_INVALID_TRANSITION : final_result;
}

static sg_compound_guard_result_t FinishBodyReuse(
	const sg_mover_subject_t *new_body,
	sg_compound_guard_result_t snapshot_result)
{
	size_t index;
	sg_compound_guard_reuse_entry_t *entry;
	subject_observation_t body_observation, bolt_observation;
	sg_mover_lease_result_t lease_result;
	sg_compound_guard_result_t final_result = SG_COMPOUND_GUARD_OK;
	sg_mover_lease_record_t current;

	for (index = 0U; index < body_reuse.count; index++)
	{
		entry = &body_reuse.entries[index];
		if (snapshot_result == SG_COMPOUND_GUARD_IDENTITY_STALE)
			body_observation = SUBJECT_OBSERVATION_STALE;
		else if (snapshot_result != SG_COMPOUND_GUARD_OK || !new_body)
			body_observation = SUBJECT_OBSERVATION_ERROR;
		else if (new_body->generation == entry->record.body.generation)
			body_observation = SUBJECT_OBSERVATION_STALE;
		else
			body_observation = ObserveSubject(&entry->record, new_body);
		if (body_observation == SUBJECT_OBSERVATION_HELD ||
		    body_observation == SUBJECT_OBSERVATION_STALE ||
		    body_observation == SUBJECT_OBSERVATION_ERROR)
		{
			(void)QuarantineTicket(&entry->ticket, &entry->owner);
			if (body_observation == SUBJECT_OBSERVATION_HELD)
				final_result = SG_COMPOUND_GUARD_NOT_CLEAR;
			else if (body_observation == SUBJECT_OBSERVATION_STALE)
				final_result = SG_COMPOUND_GUARD_IDENTITY_STALE;
			else
				final_result = SG_COMPOUND_GUARD_HOST_ERROR;
			continue;
		}
		/* The old body generation was synchronously overwritten.  Evict only
		 * that subject; an independent hook bolt retains the rotated lease. */
		lease_result = SG_MoverLeaseEvictSubject(&guard.leases,
			&entry->ticket, &entry->owner, &entry->record.body,
			&entry->ticket);
		if (lease_result != SG_MOVER_LEASE_OK)
		{
			(void)QuarantineTicket(&entry->ticket, &entry->owner);
			final_result = GuardResult(lease_result);
			continue;
		}
		bolt_observation = SubjectEmpty(&entry->record.bolt)
		    ? SUBJECT_OBSERVATION_CLEAR
		    : ObserveSubject(&entry->record, &entry->record.bolt);
		if (bolt_observation == SUBJECT_OBSERVATION_STALE ||
		    bolt_observation == SUBJECT_OBSERVATION_ERROR)
		{
			(void)QuarantineTicket(&entry->ticket, &entry->owner);
			final_result = bolt_observation == SUBJECT_OBSERVATION_STALE
			    ? SG_COMPOUND_GUARD_IDENTITY_STALE
			    : SG_COMPOUND_GUARD_HOST_ERROR;
			continue;
		}
		if (!SubjectEmpty(&entry->record.bolt) &&
		    bolt_observation != SUBJECT_OBSERVATION_HELD)
		{
			lease_result = SG_MoverLeaseEvictSubject(&guard.leases,
				&entry->ticket, &entry->owner, &entry->record.bolt,
				&entry->ticket);
			if (lease_result != SG_MOVER_LEASE_OK)
			{
				(void)QuarantineTicket(&entry->ticket, &entry->owner);
				final_result = GuardResult(lease_result);
				continue;
			}
		}
		lease_result = SG_MoverLeaseValidate(&guard.leases,
			&entry->ticket, &entry->owner, &current);
		if (lease_result != SG_MOVER_LEASE_OK)
		{
			final_result = GuardResult(lease_result);
			continue;
		}
		if (!SubjectEmpty(&current.body) || !SubjectEmpty(&current.bolt))
		{
			/* A held independent bolt remains a valid ORPHAN record.  The body
			 * replacement transaction itself completed successfully. */
			continue;
		}
		lease_result = SG_MoverLeaseReleaseProvedClear(&guard.leases,
			&entry->ticket, &entry->owner);
		if (lease_result != SG_MOVER_LEASE_OK)
		{
			(void)QuarantineTicket(&entry->ticket, &entry->owner);
			final_result = GuardResult(lease_result);
		}
	}
	return final_result;
}

sg_compound_guard_result_t SG_CompoundGuardBodyDidCopy(
	sg_compound_guard_bot_t *bot, int32_t body_edict_key)
{
	sg_mover_lease_record_t record;
	sg_mover_subject_t body;
	sg_compound_guard_result_t result, reuse_result;
	uint64_t source_generation;

	if (!guard.initialized)
		return SG_COMPOUND_GUARD_NOT_INITIALIZED;
	if (!body_reuse.active || body_edict_key <= 0 ||
	    body_reuse.body_edict_key != body_edict_key)
	{
		AbortBodyReuse();
		if (bot && BotAttached(bot) && SG_MoverTicketValid(&bot->ticket))
			(void)QuarantineTicket(&bot->ticket, &bot->owner);
		return SG_COMPOUND_GUARD_INVALID_TRANSITION;
	}
	source_generation = body_reuse.source_generation;
	result = SnapshotSubject(SG_MOVER_SUBJECT_BODY_QUEUE, body_edict_key,
		&body);
	if (result == SG_COMPOUND_GUARD_OK &&
	    (source_generation == 0U || body.generation == source_generation))
		result = SG_COMPOUND_GUARD_IDENTITY_STALE;
	reuse_result = FinishBodyReuse(result == SG_COMPOUND_GUARD_OK
		? &body : NULL, result);
	memset(&body_reuse, 0, sizeof(body_reuse));
	if (!bot)
		return reuse_result;
	if (!BotAttached(bot))
		return SG_COMPOUND_GUARD_NOT_ATTACHED;
	if (!SG_MoverTicketValid(&bot->ticket))
		return reuse_result;
	if (result != SG_COMPOUND_GUARD_OK)
	{
		(void)QuarantineTicket(&bot->ticket, &bot->owner);
		return result;
	}
	result = ValidateCurrentBot(bot, &record);
	if (result != SG_COMPOUND_GUARD_OK)
		return result;
	if (record.state != SG_MOVER_LEASE_ORPHAN ||
	    !SG_MoverSubjectEqual(&record.body, &bot->client))
	{
		(void)QuarantineTicket(&bot->ticket, &bot->owner);
		return SG_COMPOUND_GUARD_INVALID_TRANSITION;
	}
	result = GuardResult(SG_MoverLeaseTransferSubject(&guard.leases,
		&bot->ticket, &bot->owner, &record.body, &body, &bot->ticket));
	if (result != SG_COMPOUND_GUARD_OK)
	{
		(void)QuarantineTicket(&bot->ticket, &bot->owner);
		return result;
	}
	/* The current CLIENT -> BODY_QUEUE handoff is a one-shot lifecycle commit.
	 * Old queue cleanup has already released or terminalized every prior claim;
	 * returning that cleanup result here would invite an unsafe retry. */
	return SG_COMPOUND_GUARD_OK;
}

sg_compound_guard_result_t SG_CompoundGuardBoltEvicted(
	sg_compound_guard_bot_t *bot, int32_t bolt_edict_key)
{
	sg_mover_lease_record_t record;
	subject_observation_t observation;
	sg_mover_lease_result_t lease_result;
	sg_compound_guard_result_t result;

	if (!guard.initialized)
		return SG_COMPOUND_GUARD_NOT_INITIALIZED;
	if (!BotAttached(bot))
		return SG_COMPOUND_GUARD_NOT_ATTACHED;
	if (!SG_MoverTicketValid(&bot->ticket))
		return SG_COMPOUND_GUARD_OK;
	result = ValidateCurrentBot(bot, &record);
	if (result != SG_COMPOUND_GUARD_OK)
		return result;
	if (bolt_edict_key <= 0 ||
	    record.bolt.kind != SG_MOVER_SUBJECT_HOOK_BOLT ||
	    record.bolt.edict_key != bolt_edict_key)
		return SG_COMPOUND_GUARD_SUBJECT_MISMATCH;
	observation = ObserveSubject(&record, &record.bolt);
	if (observation == SUBJECT_OBSERVATION_HELD)
		return SG_COMPOUND_GUARD_NOT_CLEAR;
	if (observation == SUBJECT_OBSERVATION_STALE ||
	    observation == SUBJECT_OBSERVATION_ERROR)
		return QuarantineObservation(&bot->ticket, &bot->owner,
			observation);
	lease_result = SG_MoverLeaseEvictSubject(&guard.leases,
		&bot->ticket, &bot->owner, &record.bolt, &bot->ticket);
	if (lease_result != SG_MOVER_LEASE_OK)
		(void)QuarantineTicket(&bot->ticket, &bot->owner);
	return GuardResult(lease_result);
}

static sg_compound_guard_result_t SweepOrphan(sg_mover_ticket_t *ticket,
	const sg_mover_owner_t *owner, sg_compound_guard_frame_stats_t *stats)
{
	sg_mover_lease_record_t record;
	sg_mover_subject_t body, bolt;
	subject_observation_t body_observation = SUBJECT_OBSERVATION_CLEAR;
	subject_observation_t bolt_observation = SUBJECT_OBSERVATION_CLEAR;
	sg_mover_lease_result_t lease_result;
	int body_present, bolt_present;

	lease_result = SG_MoverLeaseValidate(&guard.leases, ticket, owner,
		&record);
	if (lease_result != SG_MOVER_LEASE_OK)
		return GuardResult(lease_result);
	if (record.state != SG_MOVER_LEASE_ORPHAN)
		return record.state == SG_MOVER_LEASE_QUARANTINED
		    ? SG_COMPOUND_GUARD_QUARANTINE_LOCKED
		    : SG_COMPOUND_GUARD_INVALID_TRANSITION;
	body = record.body;
	bolt = record.bolt;
	body_present = !SubjectEmpty(&body);
	bolt_present = !SubjectEmpty(&bolt);
	if ((body_present && !SG_MoverSubjectValid(&body)) ||
	    (bolt_present && !SG_MoverSubjectValid(&bolt)))
	{
		(void)QuarantineTicket(ticket, owner);
		if (stats)
			stats->quarantined++;
		return SG_COMPOUND_GUARD_INVALID_SUBJECT;
	}
	if (body_present)
		body_observation = ObserveSubject(&record, &body);
	if (bolt_present)
		bolt_observation = ObserveSubject(&record, &bolt);
	if (body_observation == SUBJECT_OBSERVATION_STALE ||
	    bolt_observation == SUBJECT_OBSERVATION_STALE ||
	    body_observation == SUBJECT_OBSERVATION_ERROR ||
	    bolt_observation == SUBJECT_OBSERVATION_ERROR)
	{
		(void)QuarantineTicket(ticket, owner);
		if (stats)
		{
			stats->quarantined++;
			if (body_observation == SUBJECT_OBSERVATION_ERROR ||
			    bolt_observation == SUBJECT_OBSERVATION_ERROR)
				stats->host_errors++;
		}
		return body_observation == SUBJECT_OBSERVATION_STALE ||
		       bolt_observation == SUBJECT_OBSERVATION_STALE
		    ? SG_COMPOUND_GUARD_IDENTITY_STALE
		    : SG_COMPOUND_GUARD_HOST_ERROR;
	}
	if (body_present && body_observation != SUBJECT_OBSERVATION_HELD)
	{
		lease_result = SG_MoverLeaseEvictSubject(&guard.leases, ticket,
			owner, &body, ticket);
		if (lease_result != SG_MOVER_LEASE_OK)
		{
			(void)QuarantineTicket(ticket, owner);
			return GuardResult(lease_result);
		}
		if (stats)
			stats->evicted++;
	}
	if (bolt_present && bolt_observation != SUBJECT_OBSERVATION_HELD)
	{
		lease_result = SG_MoverLeaseEvictSubject(&guard.leases, ticket,
			owner, &bolt, ticket);
		if (lease_result != SG_MOVER_LEASE_OK)
		{
			(void)QuarantineTicket(ticket, owner);
			return GuardResult(lease_result);
		}
		if (stats)
			stats->evicted++;
	}
	lease_result = SG_MoverLeaseValidate(&guard.leases, ticket, owner,
		&record);
	if (lease_result != SG_MOVER_LEASE_OK)
		return GuardResult(lease_result);
	if (!SubjectEmpty(&record.body) || !SubjectEmpty(&record.bolt))
	{
		if (stats)
			stats->held++;
		return SG_COMPOUND_GUARD_NOT_CLEAR;
	}
	lease_result = SG_MoverLeaseReleaseProvedClear(&guard.leases, ticket,
		owner);
	if (lease_result == SG_MOVER_LEASE_OK)
	{
		SG_MoverTicketClear(ticket);
		if (stats)
			stats->released++;
	}
	return GuardResult(lease_result);
}

sg_compound_guard_result_t SG_CompoundGuardReleaseProvedClear(
	sg_compound_guard_bot_t *bot)
{
	sg_mover_lease_record_t record;
	subject_observation_t observation;
	sg_mover_lease_result_t lease_result;
	sg_compound_guard_result_t result;

	result = ValidateCurrentBot(bot, &record);
	if (result != SG_COMPOUND_GUARD_OK)
		return result;
	if (record.state == SG_MOVER_LEASE_ORPHAN)
		return SweepOrphan(&bot->ticket, &bot->owner, NULL);
	if (record.state == SG_MOVER_LEASE_QUARANTINED)
		return SG_COMPOUND_GUARD_QUARANTINE_LOCKED;
	if (record.state != SG_MOVER_LEASE_ACTIVE &&
	    record.state != SG_MOVER_LEASE_PAUSED)
		return SG_COMPOUND_GUARD_INVALID_TRANSITION;
	observation = ObserveSubject(&record, &bot->client);
	if (observation == SUBJECT_OBSERVATION_HELD)
		return SG_COMPOUND_GUARD_NOT_CLEAR;
	/* A vanished/replaced live owner indicates a missed lifecycle hook. */
	if (observation == SUBJECT_OBSERVATION_ABSENT ||
	    observation == SUBJECT_OBSERVATION_STALE ||
	    observation == SUBJECT_OBSERVATION_ERROR)
		return QuarantineObservation(&bot->ticket, &bot->owner,
			observation == SUBJECT_OBSERVATION_STALE
			    ? SUBJECT_OBSERVATION_STALE
			    : SUBJECT_OBSERVATION_ERROR);
	lease_result = SG_MoverLeaseReleaseProvedClear(&guard.leases,
		&bot->ticket, &bot->owner);
	if (lease_result == SG_MOVER_LEASE_OK)
		SG_MoverTicketClear(&bot->ticket);
	return GuardResult(lease_result);
}

sg_compound_guard_result_t SG_CompoundGuardQuarantine(
	sg_compound_guard_bot_t *bot)
{
	sg_compound_guard_result_t result;

	if (!guard.initialized)
		return SG_COMPOUND_GUARD_NOT_INITIALIZED;
	result = SyncTicket(bot, NULL);
	if (result != SG_COMPOUND_GUARD_OK)
		return result;
	return QuarantineTicket(&bot->ticket, &bot->owner);
}

void SG_CompoundGuardFrame(sg_compound_guard_frame_stats_t *stats_out)
{
	size_t slot;
	sg_mover_lease_record_t record;
	sg_mover_ticket_t ticket;
	sg_compound_guard_frame_stats_t stats;

	memset(&stats, 0, sizeof(stats));
	if (!guard.initialized)
	{
		if (stats_out)
			*stats_out = stats;
		return;
	}
	/* CopyToBodyQue is synchronous.  Reaching the next SG frame with an open
	 * transaction proves DidCopy was missed; park every captured claim in its
	 * terminal fail-closed state before inspecting ordinary orphans. */
	if (body_reuse.active)
	{
		stats.quarantined += body_reuse.count;
		AbortBodyReuse();
	}
	for (slot = 0U; slot < SG_MOVER_LEASE_MAX_RECORDS; slot++)
	{
		if (!SG_MoverLeaseRecordAt(&guard.leases, slot, &record, &ticket) ||
		    record.state != SG_MOVER_LEASE_ORPHAN)
			continue;
		stats.inspected++;
		(void)SweepOrphan(&ticket, &record.owner, &stats);
	}
	if (stats_out)
		*stats_out = stats;
}

int SG_CompoundGuardRecordAt(size_t slot,
	sg_mover_lease_record_t *record_out, sg_mover_ticket_t *ticket_out)
{
	if (!guard.initialized)
	{
		if (record_out)
			memset(record_out, 0, sizeof(*record_out));
		SG_MoverTicketClear(ticket_out);
		return 0;
	}
	return SG_MoverLeaseRecordAt(&guard.leases, slot, record_out,
		ticket_out);
}

const char *SG_CompoundGuardReason(sg_compound_guard_result_t result)
{
	if (result >= SG_COMPOUND_GUARD_OK &&
	    result <= SG_COMPOUND_GUARD_QUARANTINE_LOCKED)
		return SG_MoverLeaseReason((sg_mover_lease_result_t)result);
	switch (result)
	{
	case SG_COMPOUND_GUARD_NOT_INITIALIZED: return "guard not initialized";
	case SG_COMPOUND_GUARD_NOT_ATTACHED: return "bot guard not attached";
	case SG_COMPOUND_GUARD_NO_LEASE: return "bot guard has no lease";
	case SG_COMPOUND_GUARD_ENTITY_ABSENT: return "host entity absent";
	case SG_COMPOUND_GUARD_HOST_ERROR: return "host observation unavailable";
	case SG_COMPOUND_GUARD_IDENTITY_STALE: return "host identity generation changed";
	case SG_COMPOUND_GUARD_NOT_CLEAR: return "mover sweep is not proved clear";
	case SG_COMPOUND_GUARD_AUTHORITY_MISMATCH: return "mover callback authority mismatch";
	default: return "unknown compound guard result";
	}
}
