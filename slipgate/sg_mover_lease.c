/* sg_mover_lease.c -- allocation-free ownership for overlapping movers. */
#include "sg_mover_lease.h"

#include <limits.h>
#include <string.h>

static int LeaseStateValid(sg_mover_lease_state_t state)
{
	return state >= SG_MOVER_LEASE_FREE &&
	       state <= SG_MOVER_LEASE_QUARANTINED;
}

static int LeaseLawValid(sg_mover_lease_law_t law)
{
	return law == SG_MOVER_LAW_DECLARED_DOOR ||
	       law == SG_MOVER_LAW_COMPOUND_PREOPEN;
}

static int ReservedZero(const uint8_t reserved[3])
{
	return reserved && reserved[0] == 0U && reserved[1] == 0U &&
	       reserved[2] == 0U;
}

int SG_MoverOwnerValid(const sg_mover_owner_t *owner)
{
	return owner && owner->generation != 0U && owner->id >= 0 &&
	       owner->kind == SG_MOVER_OWNER_BOT && ReservedZero(owner->reserved);
}

int SG_MoverOwnerEqual(const sg_mover_owner_t *first,
	const sg_mover_owner_t *second)
{
	return first && second && first->kind == second->kind &&
	       first->id == second->id &&
	       first->generation == second->generation;
}

int SG_MoverSubjectValid(const sg_mover_subject_t *subject)
{
	return subject && subject->generation != 0U &&
	       subject->edict_key > 0 &&
	       subject->kind >= SG_MOVER_SUBJECT_CLIENT &&
	       subject->kind <= SG_MOVER_SUBJECT_HOOK_BOLT &&
	       ReservedZero(subject->reserved);
}

int SG_MoverSubjectEqual(const sg_mover_subject_t *first,
	const sg_mover_subject_t *second)
{
	return first && second && first->kind == second->kind &&
	       first->edict_key == second->edict_key &&
	       first->generation == second->generation;
}

static int SubjectIdentityEqual(const sg_mover_subject_t *first,
	const sg_mover_subject_t *second)
{
	return SG_MoverSubjectValid(first) && SG_MoverSubjectValid(second) &&
	       first->edict_key == second->edict_key &&
	       first->generation == second->generation;
}

static int SubjectEmpty(const sg_mover_subject_t *subject)
{
	return subject && subject->kind == SG_MOVER_SUBJECT_NONE &&
	       subject->generation == 0U && subject->edict_key == 0 &&
	       ReservedZero(subject->reserved);
}

void SG_MoverTicketClear(sg_mover_ticket_t *ticket)
{
	if (!ticket)
		return;
	memset(ticket, 0, sizeof(*ticket));
	ticket->slot = SG_MOVER_LEASE_INVALID_SLOT;
}

int SG_MoverTicketValid(const sg_mover_ticket_t *ticket)
{
	return ticket && ticket->epoch != 0U && ticket->serial != 0U &&
	       ticket->slot < SG_MOVER_LEASE_MAX_RECORDS &&
	       ticket->reserved == 0U;
}

void SG_MoverLeaseInit(sg_mover_lease_registry_t *registry)
{
	if (!registry)
		return;
	if (registry->initialized)
	{
		SG_MoverLeaseLevelReset(registry);
		return;
	}
	memset(registry, 0, sizeof(*registry));
	registry->epoch = 1U;
	registry->initialized = 1U;
}

void SG_MoverLeaseLevelReset(sg_mover_lease_registry_t *registry)
{
	uint64_t epoch;

	if (!registry)
		return;
	if (!registry->initialized)
	{
		memset(registry, 0, sizeof(*registry));
		registry->epoch = 1U;
		registry->initialized = 1U;
		return;
	}
	epoch = registry->epoch;
	memset(registry->records, 0, sizeof(registry->records));
	registry->next_serial = 0U;
	registry->serial_exhausted = 0U;
	if (registry->epoch_exhausted || epoch == UINT64_MAX)
	{
		registry->epoch = UINT64_MAX;
		registry->epoch_exhausted = 1U;
		return;
	}
	registry->epoch = epoch == 0U ? 1U : epoch + 1U;
}

static int KeysValid(const sg_mover_key_t *keys, size_t key_count)
{
	size_t index;

	if (!keys || key_count == 0U || key_count > SG_MOVER_LEASE_MAX_KEYS ||
	    keys[0] == 0U)
		return 0;
	for (index = 1U; index < key_count; index++)
		if (keys[index] == 0U || keys[index - 1U] >= keys[index])
			return 0;
	return 1;
}

static int KeysOverlap(const sg_mover_key_t *first, size_t first_count,
	const sg_mover_key_t *second, size_t second_count)
{
	size_t a = 0U, b = 0U;

	while (a < first_count && b < second_count)
	{
		if (first[a] == second[b])
			return 1;
		if (first[a] < second[b])
			a++;
		else
			b++;
	}
	return 0;
}

static int SerialAvailable(sg_mover_lease_registry_t *registry)
{
	if (!registry || registry->epoch_exhausted ||
	    registry->serial_exhausted || registry->next_serial == UINT64_MAX)
	{
		if (registry)
			registry->serial_exhausted = 1U;
		return 0;
	}
	return 1;
}

static uint64_t ConsumeSerial(sg_mover_lease_registry_t *registry)
{
	registry->next_serial++;
	return registry->next_serial;
}

static sg_mover_lease_result_t Locate(
	const sg_mover_lease_registry_t *registry,
	const sg_mover_ticket_t *ticket, const sg_mover_owner_t *owner,
	const sg_mover_lease_record_t **record_out)
{
	const sg_mover_lease_record_t *record;

	if (!registry || !SG_MoverTicketValid(ticket))
		return SG_MOVER_LEASE_INVALID_ARGUMENT;
	if (owner && !SG_MoverOwnerValid(owner))
		return SG_MOVER_LEASE_INVALID_OWNER;
	if (ticket->epoch != registry->epoch)
		return SG_MOVER_LEASE_STALE_TICKET;
	record = &registry->records[ticket->slot];
	if (record->state == SG_MOVER_LEASE_FREE ||
	    record->serial != ticket->serial)
		return SG_MOVER_LEASE_STALE_TICKET;
	if (owner && !SG_MoverOwnerEqual(&record->owner, owner))
		return SG_MOVER_LEASE_OWNER_MISMATCH;
	if (record_out)
		*record_out = record;
	return SG_MOVER_LEASE_OK;
}

static void TicketForRecord(const sg_mover_lease_registry_t *registry,
	size_t slot, const sg_mover_lease_record_t *record,
	sg_mover_ticket_t *ticket)
{
	SG_MoverTicketClear(ticket);
	ticket->epoch = registry->epoch;
	ticket->serial = record->serial;
	ticket->slot = (uint16_t)slot;
}

sg_mover_lease_result_t SG_MoverLeaseAcquire(
	sg_mover_lease_registry_t *registry, const sg_mover_key_t *keys,
	size_t key_count, const sg_mover_owner_t *owner,
	sg_mover_lease_law_t law, int link_index, uint32_t mechanism_index,
	sg_mover_ticket_t *ticket_out)
{
	size_t slot, free_slot = SG_MOVER_LEASE_MAX_RECORDS;
	sg_mover_lease_record_t *record;

	SG_MoverTicketClear(ticket_out);
	if (!registry || !ticket_out || registry->epoch == 0U)
		return SG_MOVER_LEASE_INVALID_ARGUMENT;
	if (!SG_MoverOwnerValid(owner))
		return SG_MOVER_LEASE_INVALID_OWNER;
	if (!LeaseLawValid(law) || link_index < 0)
		return SG_MOVER_LEASE_INVALID_ARGUMENT;
	if (!KeysValid(keys, key_count))
		return SG_MOVER_LEASE_INVALID_KEYS;
	if (!SerialAvailable(registry))
		return SG_MOVER_LEASE_EXHAUSTED;
	for (slot = 0U; slot < SG_MOVER_LEASE_MAX_RECORDS; slot++)
	{
		record = &registry->records[slot];
		if (record->state == SG_MOVER_LEASE_FREE)
		{
			if (free_slot == SG_MOVER_LEASE_MAX_RECORDS)
				free_slot = slot;
			continue;
		}
		if (!LeaseStateValid((sg_mover_lease_state_t)record->state) ||
		    record->key_count == 0U ||
		    record->key_count > SG_MOVER_LEASE_MAX_KEYS)
			return SG_MOVER_LEASE_CONFLICT;
		if (SG_MoverOwnerEqual(owner, &record->owner))
			return SG_MOVER_LEASE_OWNER_BUSY;
		if (KeysOverlap(keys, key_count, record->keys, record->key_count))
			return SG_MOVER_LEASE_CONFLICT;
	}
	if (free_slot == SG_MOVER_LEASE_MAX_RECORDS)
		return SG_MOVER_LEASE_FULL;
	record = &registry->records[free_slot];
	memset(record, 0, sizeof(*record));
	record->owner = *owner;
	memcpy(record->keys, keys, key_count * sizeof(keys[0]));
	record->serial = ConsumeSerial(registry);
	record->link_index = link_index;
	record->mechanism_index = mechanism_index;
	record->key_count = (uint8_t)key_count;
	record->state = (uint8_t)SG_MOVER_LEASE_ACTIVE;
	record->law = (uint8_t)law;
	TicketForRecord(registry, free_slot, record, ticket_out);
	return SG_MOVER_LEASE_OK;
}

sg_mover_lease_result_t SG_MoverLeaseValidate(
	const sg_mover_lease_registry_t *registry,
	const sg_mover_ticket_t *ticket, const sg_mover_owner_t *owner,
	sg_mover_lease_record_t *record_out)
{
	const sg_mover_lease_record_t *record;
	sg_mover_lease_result_t result;

	if (record_out)
		memset(record_out, 0, sizeof(*record_out));
	result = Locate(registry, ticket, owner, &record);
	if (result != SG_MOVER_LEASE_OK)
		return result;
	if (record_out)
		*record_out = *record;
	return SG_MOVER_LEASE_OK;
}

static int TransitionAllowed(sg_mover_lease_state_t from,
	sg_mover_lease_state_t to)
{
	if (to == SG_MOVER_LEASE_QUARANTINED)
		return from == SG_MOVER_LEASE_ACTIVE ||
		       from == SG_MOVER_LEASE_PAUSED ||
		       from == SG_MOVER_LEASE_ORPHAN;
	if (from == SG_MOVER_LEASE_ACTIVE)
		return to == SG_MOVER_LEASE_PAUSED;
	if (from == SG_MOVER_LEASE_PAUSED)
		return to == SG_MOVER_LEASE_ACTIVE;
	return 0;
}

sg_mover_lease_result_t SG_MoverLeaseSetState(
	sg_mover_lease_registry_t *registry, const sg_mover_ticket_t *ticket,
	const sg_mover_owner_t *owner, sg_mover_lease_state_t state)
{
	const sg_mover_lease_record_t *found;
	sg_mover_lease_record_t *record;
	sg_mover_lease_result_t result;

	if (!registry || !LeaseStateValid(state) ||
	    state == SG_MOVER_LEASE_FREE || state == SG_MOVER_LEASE_ORPHAN)
		return SG_MOVER_LEASE_INVALID_ARGUMENT;
	if (!SG_MoverOwnerValid(owner))
		return SG_MOVER_LEASE_INVALID_OWNER;
	result = Locate(registry, ticket, owner, &found);
	if (result != SG_MOVER_LEASE_OK)
		return result;
	record = &registry->records[ticket->slot];
	if ((sg_mover_lease_state_t)record->state == state)
		return SG_MOVER_LEASE_OK;
	if (!TransitionAllowed((sg_mover_lease_state_t)record->state, state))
		return record->state == SG_MOVER_LEASE_QUARANTINED
		    ? SG_MOVER_LEASE_QUARANTINE_LOCKED
		    : SG_MOVER_LEASE_INVALID_TRANSITION;
	record->state = (uint8_t)state;
	return SG_MOVER_LEASE_OK;
}

static sg_mover_lease_result_t RotateTicket(
	sg_mover_lease_registry_t *registry, size_t slot,
	sg_mover_ticket_t *ticket_out)
{
	sg_mover_lease_record_t *record = &registry->records[slot];

	if (!SerialAvailable(registry))
		return SG_MOVER_LEASE_EXHAUSTED;
	record->serial = ConsumeSerial(registry);
	TicketForRecord(registry, slot, record, ticket_out);
	return SG_MOVER_LEASE_OK;
}

sg_mover_lease_result_t SG_MoverLeaseOrphan(
	sg_mover_lease_registry_t *registry, const sg_mover_ticket_t *ticket,
	const sg_mover_owner_t *owner, const sg_mover_subject_t *body,
	const sg_mover_subject_t *bolt, sg_mover_ticket_t *ticket_out)
{
	const sg_mover_lease_record_t *found;
	sg_mover_lease_record_t *record;
	sg_mover_lease_result_t result;
	sg_mover_ticket_t input;
	const sg_mover_ticket_t *lookup = ticket;

	if (ticket)
	{
		input = *ticket;
		lookup = &input;
	}
	if (ticket_out != ticket)
		SG_MoverTicketClear(ticket_out);
	if (!registry || !ticket_out)
		return SG_MOVER_LEASE_INVALID_ARGUMENT;
	if (!ticket)
		return SG_MOVER_LEASE_INVALID_ARGUMENT;
	if (!SG_MoverOwnerValid(owner))
		return SG_MOVER_LEASE_INVALID_OWNER;
	if (!SG_MoverSubjectValid(body) && !SG_MoverSubjectValid(bolt))
		return SG_MOVER_LEASE_INVALID_SUBJECT;
	if (body && !SG_MoverSubjectValid(body) && !SubjectEmpty(body))
		return SG_MOVER_LEASE_INVALID_SUBJECT;
	if (bolt && !SG_MoverSubjectValid(bolt) && !SubjectEmpty(bolt))
		return SG_MOVER_LEASE_INVALID_SUBJECT;
	if (body && SG_MoverSubjectValid(body) &&
	    body->kind != SG_MOVER_SUBJECT_CLIENT &&
	    body->kind != SG_MOVER_SUBJECT_BODY_QUEUE)
		return SG_MOVER_LEASE_INVALID_SUBJECT;
	if (bolt && SG_MoverSubjectValid(bolt) &&
	    bolt->kind != SG_MOVER_SUBJECT_HOOK_BOLT)
		return SG_MOVER_LEASE_INVALID_SUBJECT;
	if (SubjectIdentityEqual(body, bolt))
		return SG_MOVER_LEASE_INVALID_SUBJECT;
	result = Locate(registry, lookup, owner, &found);
	if (result != SG_MOVER_LEASE_OK)
		return result;
	if (found->state != SG_MOVER_LEASE_ACTIVE &&
	    found->state != SG_MOVER_LEASE_PAUSED)
		return found->state == SG_MOVER_LEASE_QUARANTINED
		    ? SG_MOVER_LEASE_QUARANTINE_LOCKED
		    : SG_MOVER_LEASE_INVALID_TRANSITION;
	if (!SerialAvailable(registry))
		return SG_MOVER_LEASE_EXHAUSTED;
	record = &registry->records[lookup->slot];
	memset(&record->body, 0, sizeof(record->body));
	memset(&record->bolt, 0, sizeof(record->bolt));
	if (body && SG_MoverSubjectValid(body))
		record->body = *body;
	if (bolt && SG_MoverSubjectValid(bolt))
		record->bolt = *bolt;
	record->state = (uint8_t)SG_MOVER_LEASE_ORPHAN;
	return RotateTicket(registry, lookup->slot, ticket_out);
}

static sg_mover_subject_t *FindSubject(sg_mover_lease_record_t *record,
	const sg_mover_subject_t *subject)
{
	if (SG_MoverSubjectEqual(&record->body, subject))
		return &record->body;
	if (SG_MoverSubjectEqual(&record->bolt, subject))
		return &record->bolt;
	return NULL;
}

sg_mover_lease_result_t SG_MoverLeaseTransferSubject(
	sg_mover_lease_registry_t *registry, const sg_mover_ticket_t *ticket,
	const sg_mover_owner_t *owner, const sg_mover_subject_t *from,
	const sg_mover_subject_t *to, sg_mover_ticket_t *ticket_out)
{
	const sg_mover_lease_record_t *found;
	sg_mover_lease_record_t *record;
	sg_mover_subject_t *slot;
	sg_mover_lease_result_t result;
	sg_mover_ticket_t input;
	const sg_mover_ticket_t *lookup = ticket;

	if (ticket)
	{
		input = *ticket;
		lookup = &input;
	}
	if (ticket_out != ticket)
		SG_MoverTicketClear(ticket_out);
	if (!registry || !ticket_out)
		return SG_MOVER_LEASE_INVALID_ARGUMENT;
	if (!ticket)
		return SG_MOVER_LEASE_INVALID_ARGUMENT;
	if (!SG_MoverOwnerValid(owner))
		return SG_MOVER_LEASE_INVALID_OWNER;
	if (!SG_MoverSubjectValid(from) || !SG_MoverSubjectValid(to))
		return SG_MOVER_LEASE_INVALID_SUBJECT;
	result = Locate(registry, lookup, owner, &found);
	if (result != SG_MOVER_LEASE_OK)
		return result;
	if (found->state != SG_MOVER_LEASE_ORPHAN)
		return found->state == SG_MOVER_LEASE_QUARANTINED
		    ? SG_MOVER_LEASE_QUARANTINE_LOCKED
		    : SG_MOVER_LEASE_INVALID_TRANSITION;
	record = &registry->records[lookup->slot];
	slot = FindSubject(record, from);
	if (!slot)
		return SG_MOVER_LEASE_SUBJECT_MISMATCH;
	/* The only lawful identity handoff is the client corpse copied into a
	 * body-queue edict.  Bolts are evicted, never silently rebound. */
	if (slot != &record->body ||
	    from->kind != SG_MOVER_SUBJECT_CLIENT ||
	    to->kind != SG_MOVER_SUBJECT_BODY_QUEUE ||
	    SubjectIdentityEqual(from, to) ||
	    SubjectIdentityEqual(&record->bolt, to))
		return SG_MOVER_LEASE_INVALID_SUBJECT;
	if (!SerialAvailable(registry))
		return SG_MOVER_LEASE_EXHAUSTED;
	*slot = *to;
	return RotateTicket(registry, lookup->slot, ticket_out);
}

sg_mover_lease_result_t SG_MoverLeaseEvictSubject(
	sg_mover_lease_registry_t *registry, const sg_mover_ticket_t *ticket,
	const sg_mover_owner_t *owner, const sg_mover_subject_t *subject,
	sg_mover_ticket_t *ticket_out)
{
	const sg_mover_lease_record_t *found;
	sg_mover_lease_record_t *record;
	sg_mover_subject_t *slot;
	sg_mover_lease_result_t result;
	sg_mover_ticket_t input;
	const sg_mover_ticket_t *lookup = ticket;

	if (ticket)
	{
		input = *ticket;
		lookup = &input;
	}
	if (ticket_out != ticket)
		SG_MoverTicketClear(ticket_out);
	if (!registry || !ticket_out)
		return SG_MOVER_LEASE_INVALID_ARGUMENT;
	if (!ticket)
		return SG_MOVER_LEASE_INVALID_ARGUMENT;
	if (!SG_MoverOwnerValid(owner))
		return SG_MOVER_LEASE_INVALID_OWNER;
	if (!SG_MoverSubjectValid(subject))
		return SG_MOVER_LEASE_INVALID_SUBJECT;
	result = Locate(registry, lookup, owner, &found);
	if (result != SG_MOVER_LEASE_OK)
		return result;
	if (found->state != SG_MOVER_LEASE_ORPHAN)
		return found->state == SG_MOVER_LEASE_QUARANTINED
		    ? SG_MOVER_LEASE_QUARANTINE_LOCKED
		    : SG_MOVER_LEASE_INVALID_TRANSITION;
	record = &registry->records[lookup->slot];
	slot = FindSubject(record, subject);
	if (!slot)
		return SG_MOVER_LEASE_SUBJECT_MISMATCH;
	if (!SerialAvailable(registry))
		return SG_MOVER_LEASE_EXHAUSTED;
	memset(slot, 0, sizeof(*slot));
	return RotateTicket(registry, lookup->slot, ticket_out);
}

sg_mover_lease_result_t SG_MoverLeaseQuarantine(
	sg_mover_lease_registry_t *registry, const sg_mover_ticket_t *ticket,
	const sg_mover_owner_t *owner)
{
	return SG_MoverLeaseSetState(registry, ticket, owner,
		SG_MOVER_LEASE_QUARANTINED);
}

sg_mover_lease_result_t SG_MoverLeaseReleaseProvedClear(
	sg_mover_lease_registry_t *registry, const sg_mover_ticket_t *ticket,
	const sg_mover_owner_t *owner)
{
	const sg_mover_lease_record_t *found;
	sg_mover_lease_result_t result;

	if (!registry)
		return SG_MOVER_LEASE_INVALID_ARGUMENT;
	if (!SG_MoverOwnerValid(owner))
		return SG_MOVER_LEASE_INVALID_OWNER;
	result = Locate(registry, ticket, owner, &found);
	if (result != SG_MOVER_LEASE_OK)
		return result;
	if (found->state == SG_MOVER_LEASE_QUARANTINED)
		return SG_MOVER_LEASE_QUARANTINE_LOCKED;
	memset(&registry->records[ticket->slot], 0,
	       sizeof(registry->records[ticket->slot]));
	return SG_MOVER_LEASE_OK;
}

int SG_MoverLeaseRecordAt(const sg_mover_lease_registry_t *registry,
	size_t slot, sg_mover_lease_record_t *record_out,
	sg_mover_ticket_t *ticket_out)
{
	const sg_mover_lease_record_t *record;

	if (record_out)
		memset(record_out, 0, sizeof(*record_out));
	SG_MoverTicketClear(ticket_out);
	if (!registry || slot >= SG_MOVER_LEASE_MAX_RECORDS ||
	    !record_out || !ticket_out || registry->epoch == 0U)
		return 0;
	record = &registry->records[slot];
	if (record->state == SG_MOVER_LEASE_FREE)
		return 0;
	*record_out = *record;
	TicketForRecord(registry, slot, record, ticket_out);
	return 1;
}

const char *SG_MoverLeaseReason(sg_mover_lease_result_t result)
{
	switch (result)
	{
	case SG_MOVER_LEASE_OK: return "ok";
	case SG_MOVER_LEASE_INVALID_ARGUMENT: return "invalid argument";
	case SG_MOVER_LEASE_INVALID_OWNER: return "invalid owner";
	case SG_MOVER_LEASE_INVALID_SUBJECT: return "invalid subject";
	case SG_MOVER_LEASE_INVALID_KEYS: return "invalid mover keys";
	case SG_MOVER_LEASE_CONFLICT: return "overlapping mover ownership";
	case SG_MOVER_LEASE_OWNER_BUSY: return "mover owner already holds a lease";
	case SG_MOVER_LEASE_FULL: return "mover lease table full";
	case SG_MOVER_LEASE_EXHAUSTED: return "mover lease counter exhausted";
	case SG_MOVER_LEASE_STALE_TICKET: return "stale mover ticket";
	case SG_MOVER_LEASE_OWNER_MISMATCH: return "mover owner mismatch";
	case SG_MOVER_LEASE_SUBJECT_MISMATCH: return "mover subject mismatch";
	case SG_MOVER_LEASE_INVALID_TRANSITION: return "invalid mover lease transition";
	case SG_MOVER_LEASE_QUARANTINE_LOCKED: return "quarantined mover ownership";
	default: return "unknown mover lease result";
	}
}
