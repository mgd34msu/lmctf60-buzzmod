/* sg_mover_lease.h -- allocation-free ownership for overlapping movers. */
#ifndef SG_MOVER_LEASE_H
#define SG_MOVER_LEASE_H

#include <stddef.h>
#include <stdint.h>

#define SG_MOVER_LEASE_MAX_RECORDS 64U
#define SG_MOVER_LEASE_MAX_KEYS 16U
#define SG_MOVER_LEASE_INVALID_SLOT UINT16_MAX

typedef uint16_t sg_mover_key_t;

typedef enum sg_mover_owner_kind_e
{
	SG_MOVER_OWNER_NONE = 0,
	SG_MOVER_OWNER_BOT = 1
} sg_mover_owner_kind_t;

typedef struct sg_mover_owner_s
{
	uint64_t generation;
	int32_t id;
	uint8_t kind;
	uint8_t reserved[3];
} sg_mover_owner_t;

typedef enum sg_mover_subject_kind_e
{
	SG_MOVER_SUBJECT_NONE = 0,
	SG_MOVER_SUBJECT_CLIENT,
	SG_MOVER_SUBJECT_BODY_QUEUE,
	SG_MOVER_SUBJECT_HOOK_BOLT
} sg_mover_subject_kind_t;

/* A subject is an integer edict key plus an adapter-owned generation.  The
 * pure registry never retains or dereferences an edict pointer. */
typedef struct sg_mover_subject_s
{
	uint64_t generation;
	int32_t edict_key;
	uint8_t kind;
	uint8_t reserved[3];
} sg_mover_subject_t;

typedef enum sg_mover_lease_state_e
{
	SG_MOVER_LEASE_FREE = 0,
	SG_MOVER_LEASE_ACTIVE,
	SG_MOVER_LEASE_PAUSED,
	SG_MOVER_LEASE_ORPHAN,
	SG_MOVER_LEASE_QUARANTINED
} sg_mover_lease_state_t;

typedef enum sg_mover_lease_law_e
{
	SG_MOVER_LAW_NONE = 0,
	SG_MOVER_LAW_DECLARED_DOOR,
	SG_MOVER_LAW_COMPOUND_PREOPEN,
	SG_MOVER_LAW_TRAIN_GATE
} sg_mover_lease_law_t;

/* Tickets are scoped to their originating registry.  The game adapter owns
 * one shared singleton for ordinary and compound movers.  epoch rejects a
 * ticket from another level; serial rejects a ticket after slot reuse or a
 * subject transfer.  Neither counter is allowed to wrap. */
typedef struct sg_mover_ticket_s
{
	uint64_t epoch;
	uint64_t serial;
	uint16_t slot;
	uint16_t reserved;
} sg_mover_ticket_t;

typedef struct sg_mover_lease_record_s
{
	sg_mover_owner_t owner;
	sg_mover_subject_t body;
	sg_mover_subject_t bolt;
	sg_mover_key_t keys[SG_MOVER_LEASE_MAX_KEYS];
	uint64_t serial;
	int32_t link_index;
	uint32_t mechanism_index;
	uint8_t key_count;
	uint8_t state;
	uint8_t law;
	uint8_t reserved;
} sg_mover_lease_record_t;

typedef struct sg_mover_lease_registry_s
{
	uint64_t epoch;
	uint64_t next_serial;
	uint8_t initialized;
	uint8_t epoch_exhausted;
	uint8_t serial_exhausted;
	uint8_t reserved[5];
	sg_mover_lease_record_t records[SG_MOVER_LEASE_MAX_RECORDS];
} sg_mover_lease_registry_t;

typedef enum sg_mover_lease_result_e
{
	SG_MOVER_LEASE_OK = 0,
	SG_MOVER_LEASE_INVALID_ARGUMENT,
	SG_MOVER_LEASE_INVALID_OWNER,
	SG_MOVER_LEASE_INVALID_SUBJECT,
	SG_MOVER_LEASE_INVALID_KEYS,
	SG_MOVER_LEASE_CONFLICT,
	SG_MOVER_LEASE_OWNER_BUSY,
	SG_MOVER_LEASE_FULL,
	SG_MOVER_LEASE_EXHAUSTED,
	SG_MOVER_LEASE_STALE_TICKET,
	SG_MOVER_LEASE_OWNER_MISMATCH,
	SG_MOVER_LEASE_SUBJECT_MISMATCH,
	SG_MOVER_LEASE_INVALID_TRANSITION,
	SG_MOVER_LEASE_QUARANTINE_LOCKED
} sg_mover_lease_result_t;

/* The registry storage must initially be zeroed.  Init is safe to repeat: a
 * repeated call performs a level reset and therefore invalidates old tickets. */
void SG_MoverLeaseInit(sg_mover_lease_registry_t *registry);
void SG_MoverLeaseLevelReset(sg_mover_lease_registry_t *registry);

int SG_MoverOwnerValid(const sg_mover_owner_t *owner);
int SG_MoverOwnerEqual(const sg_mover_owner_t *first,
	const sg_mover_owner_t *second);
int SG_MoverSubjectValid(const sg_mover_subject_t *subject);
int SG_MoverSubjectEqual(const sg_mover_subject_t *first,
	const sg_mover_subject_t *second);
void SG_MoverTicketClear(sg_mover_ticket_t *ticket);
int SG_MoverTicketValid(const sg_mover_ticket_t *ticket);

/* Keys must be nonempty, nonzero, strictly increasing, and unique.  Any
 * overlap with a live record conflicts.  One owner may hold only one ticket. */
sg_mover_lease_result_t SG_MoverLeaseAcquire(
	sg_mover_lease_registry_t *registry, const sg_mover_key_t *keys,
	size_t key_count, const sg_mover_owner_t *owner,
	sg_mover_lease_law_t law, int link_index, uint32_t mechanism_index,
	sg_mover_ticket_t *ticket_out);

sg_mover_lease_result_t SG_MoverLeaseValidate(
	const sg_mover_lease_registry_t *registry,
	const sg_mover_ticket_t *ticket, const sg_mover_owner_t *owner,
	sg_mover_lease_record_t *record_out);

/* ACTIVE and PAUSED are reversible.  Quarantine is terminal until level
 * reset.  ORPHAN is entered only through SG_MoverLeaseOrphan. */
sg_mover_lease_result_t SG_MoverLeaseSetState(
	sg_mover_lease_registry_t *registry, const sg_mover_ticket_t *ticket,
	const sg_mover_owner_t *owner, sg_mover_lease_state_t state);

/* Orphaning and subject changes rotate the record serial.  The body slot is a
 * CLIENT or BODY_QUEUE, the bolt slot is a HOOK_BOLT, and the sole transfer is
 * CLIENT -> BODY_QUEUE during corpse copy.  ticket_out may alias ticket.
 * On an aliased failure the input ticket is preserved; a distinct output is
 * cleared.  Failure changes no lease record, though counter exhaustion may
 * latch the registry's fail-closed exhaustion bit. */
sg_mover_lease_result_t SG_MoverLeaseOrphan(
	sg_mover_lease_registry_t *registry, const sg_mover_ticket_t *ticket,
	const sg_mover_owner_t *owner, const sg_mover_subject_t *body,
	const sg_mover_subject_t *bolt, sg_mover_ticket_t *ticket_out);
sg_mover_lease_result_t SG_MoverLeaseTransferSubject(
	sg_mover_lease_registry_t *registry, const sg_mover_ticket_t *ticket,
	const sg_mover_owner_t *owner, const sg_mover_subject_t *from,
	const sg_mover_subject_t *to, sg_mover_ticket_t *ticket_out);
sg_mover_lease_result_t SG_MoverLeaseEvictSubject(
	sg_mover_lease_registry_t *registry, const sg_mover_ticket_t *ticket,
	const sg_mover_owner_t *owner, const sg_mover_subject_t *subject,
	sg_mover_ticket_t *ticket_out);

sg_mover_lease_result_t SG_MoverLeaseQuarantine(
	sg_mover_lease_registry_t *registry, const sg_mover_ticket_t *ticket,
	const sg_mover_owner_t *owner);

/* The registry cannot observe world clearance.  The deliberately explicit
 * name makes the adapter's proof obligation visible at every release site. */
sg_mover_lease_result_t SG_MoverLeaseReleaseProvedClear(
	sg_mover_lease_registry_t *registry, const sg_mover_ticket_t *ticket,
	const sg_mover_owner_t *owner);

int SG_MoverLeaseRecordAt(const sg_mover_lease_registry_t *registry,
	size_t slot, sg_mover_lease_record_t *record_out,
	sg_mover_ticket_t *ticket_out);

const char *SG_MoverLeaseReason(sg_mover_lease_result_t result);

#endif /* SG_MOVER_LEASE_H */
