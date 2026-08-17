/* sg_compound_guard.c -- game-boundary adapter for shared mover leases. */
#include "sg_compound_guard.h"

#include <limits.h>
#include <string.h>

#define SG_COMPOUND_GUARD_MAX_RETIREMENTS 1024U

typedef struct sg_compound_guard_retirement_s
{
	sg_mover_key_t keys[SG_MOVER_LEASE_MAX_KEYS];
	size_t key_count;
	sg_mover_lease_law_t law;
	uint8_t active;
} sg_compound_guard_retirement_t;

typedef struct sg_compound_guard_singleton_s
{
	sg_mover_lease_registry_t leases;
	sg_compound_guard_host_t host;
	sg_compound_guard_retirement_t
		retirements[SG_COMPOUND_GUARD_MAX_RETIREMENTS];
	size_t retirement_count;
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
	return host && host->identity && host->solid && host->outside_sweep &&
	       host->all_subjects_outside && host->hold_open && host->set_terminal;
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

static int KeysOverlap(const sg_mover_key_t *first, size_t first_count,
	const sg_mover_key_t *second, size_t second_count)
{
	size_t first_index = 0U, second_index = 0U;

	if (!KeysValid(first, first_count) || !KeysValid(second, second_count))
		return 1;
	while (first_index < first_count && second_index < second_count)
	{
		if (first[first_index] == second[second_index])
			return 1;
		if (first[first_index] < second[second_index])
			first_index++;
		else
			second_index++;
	}
	return 0;
}

static int RetirementsOverlap(const sg_mover_key_t *keys, size_t key_count)
{
	size_t slot;

	for (slot = 0U; slot < SG_COMPOUND_GUARD_MAX_RETIREMENTS; slot++)
		if (guard.retirements[slot].active &&
		    KeysOverlap(keys, key_count, guard.retirements[slot].keys,
		        guard.retirements[slot].key_count))
			return 1;
	return 0;
}

static int DoorTransactionLaw(sg_mover_lease_law_t law)
{
	return law == SG_MOVER_LAW_DECLARED_DOOR ||
	       law == SG_MOVER_LAW_COMPOUND_PREOPEN;
}

static int LeaseRecordCanonical(const sg_mover_lease_record_t *record,
	const sg_mover_ticket_t *ticket, size_t slot)
{
	size_t index;
	int body_empty, bolt_empty;

	if (!record || !ticket || slot >= SG_MOVER_LEASE_MAX_RECORDS ||
	    !SG_MoverTicketValid(ticket) || ticket->slot != slot ||
	    ticket->epoch != guard.leases.epoch ||
	    ticket->serial != record->serial ||
	    !SG_MoverOwnerValid(&record->owner) ||
	    record->state < SG_MOVER_LEASE_ACTIVE ||
	    record->state > SG_MOVER_LEASE_QUARANTINED ||
	    !DoorTransactionLaw((sg_mover_lease_law_t)record->law) ||
	    record->link_index < 0 || record->reserved != 0U ||
	    !KeysValid(record->keys, record->key_count))
		return 0;
	for (index = record->key_count; index < SG_MOVER_LEASE_MAX_KEYS; index++)
		if (record->keys[index] != 0U)
			return 0;
	body_empty = SubjectEmpty(&record->body);
	bolt_empty = SubjectEmpty(&record->bolt);
	if ((!body_empty && (!SG_MoverSubjectValid(&record->body) ||
	     (record->body.kind != SG_MOVER_SUBJECT_CLIENT &&
	      record->body.kind != SG_MOVER_SUBJECT_BODY_QUEUE))) ||
	    (!bolt_empty && (!SG_MoverSubjectValid(&record->bolt) ||
	     record->bolt.kind != SG_MOVER_SUBJECT_HOOK_BOLT)) ||
	    (!body_empty && !bolt_empty &&
	     record->body.edict_key == record->bolt.edict_key &&
	     record->body.generation == record->bolt.generation) ||
	    ((record->state == SG_MOVER_LEASE_ACTIVE ||
	      record->state == SG_MOVER_LEASE_PAUSED) &&
	     (!body_empty || !bolt_empty)))
		return 0;
	return 1;
}

static int DoorTransactionLeasePresent(void)
{
	size_t slot;
	sg_mover_lease_record_t record;
	sg_mover_ticket_t ticket;

	for (slot = 0U; slot < SG_MOVER_LEASE_MAX_RECORDS; slot++)
	{
		if (!SG_MoverLeaseRecordAt(&guard.leases, slot, &record, &ticket))
			continue;
		if (DoorTransactionLaw((sg_mover_lease_law_t)record.law))
			return 1;
	}
	return 0;
}

static int DoorTransactionRetirementPresent(void)
{
	size_t slot;

	for (slot = 0U; slot < SG_COMPOUND_GUARD_MAX_RETIREMENTS; slot++)
		if (guard.retirements[slot].active &&
		    DoorTransactionLaw(guard.retirements[slot].law))
			return 1;
	return 0;
}

static sg_compound_guard_result_t ReserveRetirement(
	const sg_mover_lease_record_t *record, size_t *slot_out, int *created_out)
{
	size_t slot, free_slot = SG_COMPOUND_GUARD_MAX_RETIREMENTS;

	if (slot_out)
		*slot_out = SG_COMPOUND_GUARD_MAX_RETIREMENTS;
	if (created_out)
		*created_out = 0;
	if (!record || !slot_out || !created_out ||
	    !KeysValid(record->keys, record->key_count) ||
	    record->law <= SG_MOVER_LAW_NONE ||
	    record->law > SG_MOVER_LAW_COMPOUND_PREOPEN)
		return SG_COMPOUND_GUARD_INVALID_ARGUMENT;
	for (slot = 0U; slot < SG_COMPOUND_GUARD_MAX_RETIREMENTS; slot++)
	{
		sg_compound_guard_retirement_t *retirement =
		    &guard.retirements[slot];

		if (!retirement->active)
		{
			if (free_slot == SG_COMPOUND_GUARD_MAX_RETIREMENTS)
				free_slot = slot;
			continue;
		}
		if (retirement->law == (sg_mover_lease_law_t)record->law &&
		    retirement->key_count == record->key_count &&
		    memcmp(retirement->keys, record->keys,
		        record->key_count * sizeof(record->keys[0])) == 0)
		{
			*slot_out = slot;
			return SG_COMPOUND_GUARD_OK;
		}
		/* A live lease registry forbids overlapping records.  Overlap with a
		 * retirement means a caller bypassed the acquisition fence; preserve the
		 * older physical fence and fail closed instead of merging unequal sets. */
		if (KeysOverlap(retirement->keys, retirement->key_count,
		    record->keys, record->key_count))
			return SG_COMPOUND_GUARD_CONFLICT;
	}
	if (free_slot == SG_COMPOUND_GUARD_MAX_RETIREMENTS)
		return SG_COMPOUND_GUARD_FULL;
	guard.retirements[free_slot].law =
	    (sg_mover_lease_law_t)record->law;
	guard.retirements[free_slot].key_count = record->key_count;
	memcpy(guard.retirements[free_slot].keys, record->keys,
	    record->key_count * sizeof(record->keys[0]));
	guard.retirements[free_slot].active = 1U;
	guard.retirement_count++;
	*slot_out = free_slot;
	*created_out = 1;
	return SG_COMPOUND_GUARD_OK;
}

static void CancelRetirement(size_t slot, int created)
{
	if (!created || slot >= SG_COMPOUND_GUARD_MAX_RETIREMENTS ||
	    !guard.retirements[slot].active)
		return;
	memset(&guard.retirements[slot], 0,
	    sizeof(guard.retirements[slot]));
	if (guard.retirement_count > 0U)
		guard.retirement_count--;
}

static void RefreshRetirements(void)
{
	size_t slot;

	for (slot = 0U; slot < SG_COMPOUND_GUARD_MAX_RETIREMENTS; slot++)
	{
		sg_compound_guard_retirement_t *retirement =
		    &guard.retirements[slot];
		sg_compound_guard_observation_t observation;

		if (!retirement->active)
			continue;
		observation = guard.host.set_terminal(guard.host.context,
		    retirement->law, retirement->keys, retirement->key_count);
		if (observation != SG_COMPOUND_GUARD_YES)
			continue;
		/* Entity physics precedes this frame hook.  A later pusher/projectile in
		 * that same entity pass may have put a tracked body or bolt into a set
		 * whose mover already reached its terminal pose.  Retire only when both
		 * observations are positive in this one guard frame. */
		observation = guard.host.all_subjects_outside(guard.host.context,
		    retirement->keys, retirement->key_count);
		if (observation != SG_COMPOUND_GUARD_YES)
			continue;
		memset(retirement, 0, sizeof(*retirement));
		if (guard.retirement_count > 0U)
			guard.retirement_count--;
	}
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

static sg_compound_guard_result_t AllSubjectsOutside(
	const sg_mover_key_t *keys, size_t key_count)
{
	sg_compound_guard_observation_t observation;

	if (!KeysValid(keys, key_count))
		return SG_COMPOUND_GUARD_INVALID_KEYS;
	observation = guard.host.all_subjects_outside(guard.host.context,
		keys, key_count);
	if (observation == SG_COMPOUND_GUARD_YES)
		return SG_COMPOUND_GUARD_OK;
	if (observation == SG_COMPOUND_GUARD_NO)
		return SG_COMPOUND_GUARD_NOT_CLEAR;
	return SG_COMPOUND_GUARD_HOST_ERROR;
}

static sg_compound_guard_result_t MaintainRecord(
	const sg_mover_lease_record_t *record)
{
	sg_compound_guard_observation_t observation;

	if (!record || !KeysValid(record->keys, record->key_count))
		return SG_COMPOUND_GUARD_INVALID_KEYS;
	observation = guard.host.hold_open(guard.host.context,
		(sg_mover_lease_law_t)record->law, record->keys,
		record->key_count, 500);
	return observation == SG_COMPOUND_GUARD_YES
	    ? SG_COMPOUND_GUARD_OK : SG_COMPOUND_GUARD_HOST_ERROR;
}

static sg_compound_guard_result_t ReleaseRecord(
	const sg_mover_lease_record_t *record, sg_mover_ticket_t *ticket,
	const sg_mover_owner_t *owner)
{
	sg_mover_lease_result_t lease_result;
	sg_compound_guard_result_t result;
	size_t retirement_slot;
	int retirement_created;

	result = ReserveRetirement(record, &retirement_slot,
	    &retirement_created);
	if (result != SG_COMPOUND_GUARD_OK)
		return result;
	lease_result = SG_MoverLeaseReleaseProvedClear(&guard.leases, ticket,
	    owner);
	if (lease_result != SG_MOVER_LEASE_OK)
	{
		CancelRetirement(retirement_slot, retirement_created);
		return GuardResult(lease_result);
	}
	SG_MoverTicketClear(ticket);
	return SG_COMPOUND_GUARD_OK;
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
	memset(guard.retirements, 0, sizeof(guard.retirements));
	guard.retirement_count = 0U;
	guard.host = *host;
	return SG_COMPOUND_GUARD_OK;
}

sg_compound_guard_result_t SG_CompoundGuardLevelReset(void)
{
	if (!guard.initialized)
		return SG_COMPOUND_GUARD_NOT_INITIALIZED;
	memset(&body_reuse, 0, sizeof(body_reuse));
	SG_MoverLeaseLevelReset(&guard.leases);
	memset(guard.retirements, 0, sizeof(guard.retirements));
	guard.retirement_count = 0U;
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
	result = AllSubjectsOutside(record.keys, record.key_count);
	if (result == SG_COMPOUND_GUARD_NOT_CLEAR)
	{
		/* The disconnecting client is already absent/nonsolid, but another
		 * generation-tracked subject still occupies the captured set.  Hand the
		 * claim to Frame as a releasable ORPHAN instead of terminal quarantine. */
		lease_result = SG_MoverLeaseOrphan(&guard.leases, &bot->ticket,
			&bot->owner, &bot->client, NULL, &bot->ticket);
		if (lease_result != SG_MOVER_LEASE_OK)
			(void)QuarantineTicket(&bot->ticket, &bot->owner);
		return GuardResult(lease_result);
	}
	if (result != SG_COMPOUND_GUARD_OK)
	{
		(void)QuarantineTicket(&bot->ticket, &bot->owner);
		return result;
	}
	/* ABSENT is expected after a correctly ordered disconnect.  The captured
	 * generation is still protected: a replacement reports STALE above. */
	return ReleaseRecord(&record, &bot->ticket, &bot->owner);
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
	RefreshRetirements();
	if (RetirementsOverlap(keys, key_count))
		return SG_COMPOUND_GUARD_CONFLICT;
	/* The live compound controller has one process-wide door transaction.
	 * Neither controller can safely interleave trigger/Pmove callbacks with a
	 * disjoint declared or PREOPEN set, and a logically released set remains in
	 * that transaction until its physical retirement clears.  Keep this policy
	 * explicit to these two laws so future unrelated mover laws retain the
	 * registry's ordinary key-overlap semantics. */
	if (DoorTransactionLaw(law) &&
	    (DoorTransactionLeasePresent() ||
	     DoorTransactionRetirementPresent()))
		return SG_COMPOUND_GUARD_CONFLICT;
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
	result = AllSubjectsOutside(keys, key_count);
	/* The owner-specific check above gives NOT_CLEAR its narrow runtime meaning:
	 * this acquiring body is already in the sweep.  A record-wide blocker is a
	 * safe contender/conflict wait, never evidence to kill this owner. */
	if (result == SG_COMPOUND_GUARD_NOT_CLEAR)
		return SG_COMPOUND_GUARD_CONFLICT;
	if (result != SG_COMPOUND_GUARD_OK)
		return result;
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

sg_compound_guard_result_t SG_CompoundGuardAcquireDeclaredDoorBound(
	sg_compound_guard_bot_t *bot, const sg_mover_key_t *keys,
	size_t key_count, int link_index, uint32_t mechanism_index)
{
	if (mechanism_index == 0U)
		return SG_COMPOUND_GUARD_INVALID_ARGUMENT;
	return Acquire(bot, keys, key_count, SG_MOVER_LAW_DECLARED_DOOR,
		link_index, mechanism_index);
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

sg_compound_guard_run_t SG_CompoundGuardBotRunState(
	const sg_compound_guard_bot_t *bot)
{
	size_t slot;
	sg_mover_lease_record_t record;
	sg_mover_ticket_t ticket;
	subject_observation_t observation;
	int owns_active = 0;
	int wait = 0;

	if (!guard.initialized)
		return SG_COMPOUND_GUARD_RUN_WAIT;
	if (!BotAttached(bot))
		return SG_COMPOUND_GUARD_RUN_WAIT;
	for (slot = 0U; slot < SG_MOVER_LEASE_MAX_RECORDS; slot++)
	{
		if (!SG_MoverLeaseRecordAt(&guard.leases, slot, &record, &ticket))
			continue;
		if ((record.state == SG_MOVER_LEASE_ACTIVE ||
		     record.state == SG_MOVER_LEASE_PAUSED) &&
		    memcmp(&record.owner, &bot->owner, sizeof(record.owner)) == 0 &&
		    memcmp(&ticket, &bot->ticket, sizeof(ticket)) == 0)
		{
			owns_active = 1;
			break;
		}
	}
	/* A released set remains a no-entry region until the host proves the real
	 * brushes stationary and closed.  A direct/external shove into that region
	 * is terminal; an outside bot waits.  Existing owners remain runnable so a
	 * disjoint in-flight lease can finish rather than lose its TOP maintenance. */
	for (slot = 0U; slot < SG_COMPOUND_GUARD_MAX_RETIREMENTS; slot++)
	{
		if (!guard.retirements[slot].active)
			continue;
		if (!KeysValid(guard.retirements[slot].keys,
		    guard.retirements[slot].key_count))
		{
			wait = 1;
			continue;
		}
		memset(&record, 0, sizeof(record));
		record.key_count =
		    (uint8_t)guard.retirements[slot].key_count;
		memcpy(record.keys, guard.retirements[slot].keys,
		    record.key_count * sizeof(record.keys[0]));
		observation = ObserveSubject(&record, &bot->client);
		if (observation == SUBJECT_OBSERVATION_HELD)
			return SG_COMPOUND_GUARD_RUN_TERMINAL;
		if (observation != SUBJECT_OBSERVATION_CLEAR)
			wait = 1;
		else if (!owns_active)
			wait = 1;
	}
	for (slot = 0U; slot < SG_MOVER_LEASE_MAX_RECORDS; slot++)
	{
		if (!SG_MoverLeaseRecordAt(&guard.leases, slot, &record, &ticket))
			continue;
		if (BotAttached(bot) &&
		    (record.state == SG_MOVER_LEASE_ACTIVE ||
		     record.state == SG_MOVER_LEASE_PAUSED) &&
		    memcmp(&record.owner, &bot->owner, sizeof(record.owner)) == 0 &&
		    memcmp(&ticket, &bot->ticket, sizeof(ticket)) == 0)
			continue;
		observation = ObserveSubject(&record, &bot->client);
		if (observation == SUBJECT_OBSERVATION_HELD)
			return SG_COMPOUND_GUARD_RUN_TERMINAL;
		if (observation == SUBJECT_OBSERVATION_STALE ||
		    observation == SUBJECT_OBSERVATION_ERROR ||
		    observation == SUBJECT_OBSERVATION_ABSENT)
		{
			wait = 1;
			continue;
		}
		/* ACTIVE/PAUSED and ORPHAN sets remain a no-entry region.  A terminal
		 * quarantine is physically renewed by Frame and has no runnable owner;
		 * an outside bot may continue ordinary play without deadlocking the map. */
		if (!owns_active &&
		    (record.state == SG_MOVER_LEASE_ACTIVE ||
		     record.state == SG_MOVER_LEASE_PAUSED ||
		     record.state == SG_MOVER_LEASE_ORPHAN))
			wait = 1;
	}
	return wait ? SG_COMPOUND_GUARD_RUN_WAIT
	            : SG_COMPOUND_GUARD_RUN_READY;
}

sg_compound_guard_result_t SG_CompoundGuardAllSubjectsOutside(
	sg_compound_guard_bot_t *bot)
{
	sg_mover_lease_record_t record;
	sg_compound_guard_result_t result;

	result = ValidateCurrentBot(bot, &record);
	if (result != SG_COMPOUND_GUARD_OK)
		return result;
	if (record.state != SG_MOVER_LEASE_ACTIVE &&
	    record.state != SG_MOVER_LEASE_PAUSED)
		return SG_COMPOUND_GUARD_INVALID_TRANSITION;
	return AllSubjectsOutside(record.keys, record.key_count);
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

sg_compound_guard_result_t SG_CompoundGuardMaintain(
	sg_compound_guard_bot_t *bot)
{
	sg_mover_lease_record_t record;
	sg_compound_guard_result_t result;

	if (!guard.initialized)
		return SG_COMPOUND_GUARD_NOT_INITIALIZED;
	result = SyncTicket(bot, &record);
	if (result != SG_COMPOUND_GUARD_OK)
		return result;
	if (!LeaseRecordCanonical(&record, &bot->ticket,
	        (size_t)bot->ticket.slot))
		return SG_COMPOUND_GUARD_AUTHORITY_MISMATCH;
	if (record.state == SG_MOVER_LEASE_ORPHAN)
		return SG_COMPOUND_GUARD_INVALID_TRANSITION;
	if (record.state != SG_MOVER_LEASE_ACTIVE &&
	    record.state != SG_MOVER_LEASE_PAUSED &&
	    record.state != SG_MOVER_LEASE_QUARANTINED)
		return SG_COMPOUND_GUARD_INVALID_TRANSITION;
	result = MaintainRecord(&record);
	if (result != SG_COMPOUND_GUARD_OK &&
	    record.state != SG_MOVER_LEASE_QUARANTINED)
	{
		sg_compound_guard_result_t quarantine_result =
		    QuarantineTicket(&bot->ticket, &bot->owner);

		if (quarantine_result != SG_COMPOUND_GUARD_OK)
			return quarantine_result;
	}
	return result;
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
		{
			sg_compound_guard_result_t clear_result;

			clear_result = AllSubjectsOutside(current.keys,
				current.key_count);
			if (clear_result != SG_COMPOUND_GUARD_OK)
			{
				if (clear_result != SG_COMPOUND_GUARD_NOT_CLEAR)
					(void)QuarantineTicket(&entry->ticket,
						&entry->owner);
				final_result = clear_result;
				continue;
			}
		}
		{
			sg_compound_guard_result_t release_result;

			release_result = ReleaseRecord(&current, &entry->ticket,
			    &entry->owner);
			if (release_result != SG_COMPOUND_GUARD_OK)
			{
				(void)QuarantineTicket(&entry->ticket, &entry->owner);
				final_result = release_result;
			}
		}
		if (final_result != SG_COMPOUND_GUARD_OK)
		{
			/* The cleanup loop continues so every captured old claim reaches a
			 * terminal state even when one retirement cannot be reserved. */
			continue;
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
	{
		sg_compound_guard_result_t clear_result;

		clear_result = AllSubjectsOutside(record.keys, record.key_count);
		if (clear_result != SG_COMPOUND_GUARD_OK)
		{
			if (stats)
			{
				if (clear_result == SG_COMPOUND_GUARD_NOT_CLEAR)
					stats->held++;
				else
					stats->host_errors++;
			}
			if (clear_result != SG_COMPOUND_GUARD_NOT_CLEAR)
			{
				(void)QuarantineTicket(ticket, owner);
				if (stats)
					stats->quarantined++;
			}
			return clear_result;
		}
	}
	{
		sg_compound_guard_result_t release_result;

		release_result = ReleaseRecord(&record, ticket, owner);
		if (release_result == SG_COMPOUND_GUARD_OK)
		{
			if (stats)
				stats->released++;
		}
		return release_result;
	}
}

sg_compound_guard_result_t SG_CompoundGuardReleaseProvedClear(
	sg_compound_guard_bot_t *bot)
{
	sg_mover_lease_record_t record;
	subject_observation_t observation;
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
	result = AllSubjectsOutside(record.keys, record.key_count);
	if (result == SG_COMPOUND_GUARD_NOT_CLEAR)
		return result;
	if (result != SG_COMPOUND_GUARD_OK)
	{
		(void)QuarantineTicket(&bot->ticket, &bot->owner);
		return result;
	}
	return ReleaseRecord(&record, &bot->ticket, &bot->owner);
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
	/* Movers already ran for this server frame.  Retire a release fence only
	 * after the host proves the captured physical set actually closed; elapsed
	 * frames and expired TOP timers are not closure evidence. */
	RefreshRetirements();
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
		sg_compound_guard_result_t result;

		if (!SG_MoverLeaseRecordAt(&guard.leases, slot, &record, &ticket) ||
		    (record.state != SG_MOVER_LEASE_ORPHAN &&
		     record.state != SG_MOVER_LEASE_QUARANTINED))
			continue;
		stats.inspected++;
		if (record.state == SG_MOVER_LEASE_ORPHAN)
			result = SweepOrphan(&ticket, &record.owner, &stats);
		else
			result = SG_COMPOUND_GUARD_QUARANTINE_LOCKED;
		if (result == SG_COMPOUND_GUARD_OK)
			continue;
		/* The route/trigger owner may already be dead or detached.  The captured
		 * physical set remains sufficient to renew an exact TOP lease while any
		 * tracked client, corpse, or bolt prevents safe retirement. */
		if (MaintainRecord(&record) == SG_COMPOUND_GUARD_OK)
		{
			if (record.state == SG_MOVER_LEASE_QUARANTINED)
				stats.held++;
		}
		else
			stats.host_errors++;
	}
	if (stats_out)
		*stats_out = stats;
}

int SG_CompoundGuardAnyRetirement(void)
{
	size_t slot;

	if (!guard.initialized ||
	    guard.retirement_count > SG_COMPOUND_GUARD_MAX_RETIREMENTS)
		return 1;
	for (slot = 0U; slot < SG_COMPOUND_GUARD_MAX_RETIREMENTS; slot++)
		if (guard.retirements[slot].active)
			return 1;
	return 0;
}

int SG_CompoundGuardRetirementOverlaps(const sg_mover_key_t *keys,
	size_t key_count)
{
	if (!guard.initialized || !KeysValid(keys, key_count) ||
	    guard.retirement_count > SG_COMPOUND_GUARD_MAX_RETIREMENTS)
		return 1;
	return RetirementsOverlap(keys, key_count);
}

static sg_compound_guard_result_t DoorPusherFenceScan(
	const sg_mover_key_t *keys, size_t key_count, int match_any)
{
	size_t slot, active_retirements = 0U;
	sg_mover_lease_record_t record;
	sg_mover_lease_record_t empty_record;
	sg_mover_ticket_t ticket;
	int protected = 0;

	/* No lease can predate initialization.  This exception is what keeps the
	 * guard interlock invisible to ordinary human-driven pushers. */
	if (!guard.initialized)
		return SG_COMPOUND_GUARD_NOT_INITIALIZED;
	if (!match_any && !KeysValid(keys, key_count))
		return SG_COMPOUND_GUARD_INVALID_KEYS;
	if (guard.initialized != 1U || guard.leases.initialized != 1U ||
	    guard.leases.epoch == 0U || !HostValid(&guard.host) ||
	    guard.leases.epoch_exhausted > 1U ||
	    guard.leases.serial_exhausted > 1U ||
	    !ReservedZero(guard.leases.reserved,
	        sizeof(guard.leases.reserved)) ||
	    guard.retirement_count > SG_COMPOUND_GUARD_MAX_RETIREMENTS)
		return SG_COMPOUND_GUARD_HOST_ERROR;
	memset(&empty_record, 0, sizeof(empty_record));
	for (slot = 0U; slot < SG_MOVER_LEASE_MAX_RECORDS; slot++)
	{
		const sg_mover_lease_record_t *stored =
		    &guard.leases.records[slot];

		if (stored->state == SG_MOVER_LEASE_FREE)
		{
			if (memcmp(stored, &empty_record, sizeof(*stored)) != 0)
				return SG_COMPOUND_GUARD_HOST_ERROR;
			continue;
		}
		if (!SG_MoverLeaseRecordAt(&guard.leases, slot, &record, &ticket) ||
		    !LeaseRecordCanonical(&record, &ticket, slot))
			return SG_COMPOUND_GUARD_HOST_ERROR;
		if (match_any ||
		    KeysOverlap(keys, key_count, record.keys, record.key_count))
			protected = 1;
	}
	for (slot = 0U; slot < SG_COMPOUND_GUARD_MAX_RETIREMENTS; slot++)
	{
		const sg_compound_guard_retirement_t *retirement =
		    &guard.retirements[slot];

		if (retirement->active > 1U)
			return SG_COMPOUND_GUARD_HOST_ERROR;
		if (!retirement->active)
		{
			size_t key_index;

			if (retirement->key_count != 0U ||
			    retirement->law != SG_MOVER_LAW_NONE)
				return SG_COMPOUND_GUARD_HOST_ERROR;
			for (key_index = 0U;
			     key_index < SG_MOVER_LEASE_MAX_KEYS; key_index++)
				if (retirement->keys[key_index] != 0U)
					return SG_COMPOUND_GUARD_HOST_ERROR;
			continue;
		}
		active_retirements++;
		if (!DoorTransactionLaw(retirement->law) ||
		    !KeysValid(retirement->keys, retirement->key_count))
			return SG_COMPOUND_GUARD_HOST_ERROR;
		if (match_any || KeysOverlap(keys, key_count, retirement->keys,
		    retirement->key_count))
			protected = 1;
	}
	if (active_retirements != guard.retirement_count)
		return SG_COMPOUND_GUARD_HOST_ERROR;
	return protected ? SG_COMPOUND_GUARD_OK : SG_COMPOUND_GUARD_NO_LEASE;
}

sg_compound_guard_result_t SG_CompoundGuardDoorPusherFence(
	const sg_mover_key_t *keys, size_t key_count)
{
	return DoorPusherFenceScan(keys, key_count, 0);
}

sg_compound_guard_result_t SG_CompoundGuardAnyDoorTransaction(void)
{
	return DoorPusherFenceScan(NULL, 0U, 1);
}

int SG_CompoundGuardCompoundOverlaps(const sg_mover_key_t *keys,
	size_t key_count)
{
	size_t slot, active_retirements = 0U;
	sg_mover_lease_record_t record;
	sg_mover_lease_record_t empty_record;
	sg_mover_ticket_t ticket;

	if (guard.initialized != 1U || guard.leases.initialized != 1U ||
	    guard.leases.epoch == 0U || !HostValid(&guard.host) ||
	    guard.leases.epoch_exhausted > 1U ||
	    guard.leases.serial_exhausted > 1U ||
	    !ReservedZero(guard.leases.reserved,
	        sizeof(guard.leases.reserved)) ||
	    !KeysValid(keys, key_count) ||
	    guard.retirement_count > SG_COMPOUND_GUARD_MAX_RETIREMENTS)
		return 1;
	memset(&empty_record, 0, sizeof(empty_record));
	for (slot = 0U; slot < SG_MOVER_LEASE_MAX_RECORDS; slot++)
	{
		const sg_mover_lease_record_t *stored =
		    &guard.leases.records[slot];

		if (stored->state == SG_MOVER_LEASE_FREE)
		{
			if (memcmp(stored, &empty_record, sizeof(*stored)) != 0)
				return 1;
			continue;
		}
		if (!SG_MoverLeaseRecordAt(&guard.leases, slot, &record, &ticket))
			return 1;
		if (!LeaseRecordCanonical(&record, &ticket, slot))
			return 1;
		if (record.law == SG_MOVER_LAW_COMPOUND_PREOPEN &&
		    KeysOverlap(keys, key_count, record.keys, record.key_count))
			return 1;
	}
	for (slot = 0U; slot < SG_COMPOUND_GUARD_MAX_RETIREMENTS; slot++)
	{
		const sg_compound_guard_retirement_t *retirement =
		    &guard.retirements[slot];

		if (retirement->active > 1U)
			return 1;
		if (!retirement->active)
		{
			size_t key_index;

			if (retirement->key_count != 0U ||
			    retirement->law != SG_MOVER_LAW_NONE)
				return 1;
			for (key_index = 0U;
			     key_index < SG_MOVER_LEASE_MAX_KEYS; key_index++)
				if (retirement->keys[key_index] != 0U)
					return 1;
			continue;
		}
		active_retirements++;
		if (!DoorTransactionLaw(retirement->law) ||
		    !KeysValid(retirement->keys, retirement->key_count))
			return 1;
		if (retirement->law == SG_MOVER_LAW_COMPOUND_PREOPEN &&
		    KeysOverlap(keys, key_count, retirement->keys,
		        retirement->key_count))
			return 1;
	}
	return active_retirements != guard.retirement_count;
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
