/* Strict unit tests for the allocation-free overlapping-mover registry. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "slipgate/sg_mover_lease.h"

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		        __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static sg_mover_owner_t Owner(int id, uint64_t generation)
{
	sg_mover_owner_t owner;

	memset(&owner, 0, sizeof(owner));
	owner.kind = SG_MOVER_OWNER_BOT;
	owner.id = id;
	owner.generation = generation;
	return owner;
}

static sg_mover_subject_t Subject(sg_mover_subject_kind_t kind, int key,
	uint64_t generation)
{
	sg_mover_subject_t subject;

	memset(&subject, 0, sizeof(subject));
	subject.kind = (uint8_t)kind;
	subject.edict_key = key;
	subject.generation = generation;
	return subject;
}

static void TestInputContract(void)
{
	sg_mover_lease_registry_t registry = {0};
	sg_mover_owner_t bot = Owner(3, 7U);
	sg_mover_owner_t invalid = bot;
	sg_mover_owner_t noncanonical = bot;
	sg_mover_ticket_t ticket;
	sg_mover_key_t good[] = {4U, 9U};
	sg_mover_key_t zero[] = {0U, 9U};
	sg_mover_key_t duplicate[] = {4U, 4U};
	sg_mover_key_t reversed[] = {9U, 4U};

	invalid.kind = SG_MOVER_OWNER_NONE;
	noncanonical.reserved[1] = 1U;
	SG_MoverLeaseInit(&registry);
	CHECK(registry.epoch == 1U);
	CHECK(SG_MoverLeaseAcquire(NULL, good, 2U, &bot,
	      SG_MOVER_LAW_DECLARED_DOOR, 1, 0U, &ticket) ==
	      SG_MOVER_LEASE_INVALID_ARGUMENT);
	CHECK(SG_MoverLeaseAcquire(&registry, good, 2U, &bot,
	      SG_MOVER_LAW_DECLARED_DOOR, 1, 0U, NULL) ==
	      SG_MOVER_LEASE_INVALID_ARGUMENT);
	CHECK(SG_MoverLeaseAcquire(&registry, good, 2U, &invalid,
	      SG_MOVER_LAW_DECLARED_DOOR, 1, 0U, &ticket) ==
	      SG_MOVER_LEASE_INVALID_OWNER);
	CHECK(!SG_MoverTicketValid(&ticket));
	CHECK(SG_MoverLeaseAcquire(&registry, good, 2U, &noncanonical,
	      SG_MOVER_LAW_DECLARED_DOOR, 1, 0U, &ticket) ==
	      SG_MOVER_LEASE_INVALID_OWNER);
	CHECK(SG_MoverLeaseAcquire(&registry, good, 2U, &bot,
	      SG_MOVER_LAW_NONE, 1, 0U, &ticket) ==
	      SG_MOVER_LEASE_INVALID_ARGUMENT);
	CHECK(SG_MoverLeaseAcquire(&registry, good, 2U, &bot,
	      SG_MOVER_LAW_DECLARED_DOOR, -1, 0U, &ticket) ==
	      SG_MOVER_LEASE_INVALID_ARGUMENT);
	CHECK(SG_MoverLeaseAcquire(&registry, NULL, 2U, &bot,
	      SG_MOVER_LAW_DECLARED_DOOR, 1, 0U, &ticket) ==
	      SG_MOVER_LEASE_INVALID_KEYS);
	CHECK(SG_MoverLeaseAcquire(&registry, good, 0U, &bot,
	      SG_MOVER_LAW_DECLARED_DOOR, 1, 0U, &ticket) ==
	      SG_MOVER_LEASE_INVALID_KEYS);
	CHECK(SG_MoverLeaseAcquire(&registry, zero, 2U, &bot,
	      SG_MOVER_LAW_DECLARED_DOOR, 1, 0U, &ticket) ==
	      SG_MOVER_LEASE_INVALID_KEYS);
	CHECK(SG_MoverLeaseAcquire(&registry, duplicate, 2U, &bot,
	      SG_MOVER_LAW_DECLARED_DOOR, 1, 0U, &ticket) ==
	      SG_MOVER_LEASE_INVALID_KEYS);
	CHECK(SG_MoverLeaseAcquire(&registry, reversed, 2U, &bot,
	      SG_MOVER_LAW_DECLARED_DOOR, 1, 0U, &ticket) ==
	      SG_MOVER_LEASE_INVALID_KEYS);
}

static void TestOverlapOwnerAndReuse(void)
{
	sg_mover_lease_registry_t registry = {0}, snapshot;
	sg_mover_owner_t first = Owner(1, 10U);
	sg_mover_owner_t second = Owner(2, 11U);
	sg_mover_ticket_t ticket, second_ticket, rejected, stale;
	sg_mover_key_t pair[] = {10U, 20U};
	sg_mover_key_t left[] = {3U, 10U};
	sg_mover_key_t right[] = {20U, 30U};
	sg_mover_key_t clear[] = {11U, 19U};
	sg_mover_key_t other[] = {40U};

	SG_MoverLeaseInit(&registry);
	CHECK(SG_MoverLeaseAcquire(&registry, pair, 2U, &first,
	      SG_MOVER_LAW_DECLARED_DOOR, 7, 0U, &ticket) ==
	      SG_MOVER_LEASE_OK);
	CHECK(ticket.epoch == 1U && SG_MoverTicketValid(&ticket));
	snapshot = registry;
	CHECK(SG_MoverLeaseAcquire(&registry, left, 2U, &second,
	      SG_MOVER_LAW_COMPOUND_PREOPEN, 8, 2U, &rejected) ==
	      SG_MOVER_LEASE_CONFLICT);
	CHECK(!SG_MoverTicketValid(&rejected));
	CHECK(memcmp(&snapshot, &registry, sizeof(registry)) == 0);
	CHECK(SG_MoverLeaseAcquire(&registry, right, 2U, &second,
	      SG_MOVER_LAW_COMPOUND_PREOPEN, 8, 2U, &rejected) ==
	      SG_MOVER_LEASE_CONFLICT);
	CHECK(SG_MoverLeaseAcquire(&registry, other, 1U, &first,
	      SG_MOVER_LAW_DECLARED_DOOR, 9, 0U, &rejected) ==
	      SG_MOVER_LEASE_OWNER_BUSY);
	CHECK(SG_MoverLeaseAcquire(&registry, clear, 2U, &second,
	      SG_MOVER_LAW_COMPOUND_PREOPEN, 8, 2U, &second_ticket) ==
	      SG_MOVER_LEASE_OK);

	stale = ticket;
	CHECK(SG_MoverLeaseReleaseProvedClear(&registry, &ticket, &first) ==
	      SG_MOVER_LEASE_OK);
	CHECK(SG_MoverLeaseValidate(&registry, &stale, &first, NULL) ==
	      SG_MOVER_LEASE_STALE_TICKET);
	CHECK(SG_MoverLeaseAcquire(&registry, pair, 2U, &first,
	      SG_MOVER_LAW_DECLARED_DOOR, 7, 0U, &ticket) ==
	      SG_MOVER_LEASE_OK);
	CHECK(ticket.slot == stale.slot && ticket.serial != stale.serial);
	CHECK(SG_MoverLeaseValidate(&registry, &stale, &first, NULL) ==
	      SG_MOVER_LEASE_STALE_TICKET);
	CHECK(SG_MoverLeaseReleaseProvedClear(&registry, &second_ticket,
	      &second) == SG_MOVER_LEASE_OK);
	CHECK(SG_MoverLeaseReleaseProvedClear(&registry, &ticket, &first) ==
	      SG_MOVER_LEASE_OK);
}

static void TestPauseOrphanSubjectsAndQuarantine(void)
{
	sg_mover_lease_registry_t registry = {0}, snapshot;
	sg_mover_owner_t bot = Owner(4, 8U);
	sg_mover_owner_t wrong = Owner(4, 9U);
	sg_mover_owner_t noncanonical_owner = bot;
	sg_mover_subject_t client = Subject(SG_MOVER_SUBJECT_CLIENT, 65, 12U);
	sg_mover_subject_t body = Subject(SG_MOVER_SUBJECT_BODY_QUEUE, 81, 13U);
	sg_mover_subject_t retyped_same =
	    Subject(SG_MOVER_SUBJECT_BODY_QUEUE, 65, 12U);
	sg_mover_subject_t bolt = Subject(SG_MOVER_SUBJECT_HOOK_BOLT, 97, 14U);
	sg_mover_subject_t aliased_bolt =
	    Subject(SG_MOVER_SUBJECT_HOOK_BOLT, 65, 12U);
	sg_mover_subject_t noncanonical = client;
	sg_mover_ticket_t ticket, rotated, stale;
	sg_mover_key_t keys[] = {7U, 8U, 12U};
	sg_mover_key_t overlaps[] = {1U, 8U};
	sg_mover_lease_record_t record;

	SG_MoverLeaseInit(&registry);
	noncanonical.reserved[0] = 1U;
	noncanonical_owner.reserved[0] = 1U;
	CHECK(SG_MoverLeaseAcquire(&registry, keys, 3U, &bot,
	      SG_MOVER_LAW_COMPOUND_PREOPEN, 22, 4U, &ticket) ==
	      SG_MOVER_LEASE_OK);
	CHECK(SG_MoverLeaseValidate(&registry, &ticket, &noncanonical_owner,
	      NULL) == SG_MOVER_LEASE_INVALID_OWNER);
	CHECK(SG_MoverLeaseOrphan(&registry, &ticket, &bot, &bolt, NULL,
	      &rotated) == SG_MOVER_LEASE_INVALID_SUBJECT);
	CHECK(SG_MoverLeaseOrphan(&registry, &ticket, &bot, NULL, &client,
	      &rotated) == SG_MOVER_LEASE_INVALID_SUBJECT);
	CHECK(SG_MoverLeaseOrphan(&registry, &ticket, &bot, &noncanonical,
	      NULL, &rotated) == SG_MOVER_LEASE_INVALID_SUBJECT);
	CHECK(SG_MoverLeaseOrphan(&registry, &ticket, &bot, &client,
	      &aliased_bolt, &rotated) == SG_MOVER_LEASE_INVALID_SUBJECT);
	CHECK(!SG_MoverTicketValid(&rotated));
	snapshot = registry;
	CHECK(SG_MoverLeaseSetState(&registry, &ticket, NULL,
	      SG_MOVER_LEASE_PAUSED) == SG_MOVER_LEASE_INVALID_OWNER);
	CHECK(SG_MoverLeaseOrphan(&registry, &ticket, NULL, &client, &bolt,
	      &rotated) == SG_MOVER_LEASE_INVALID_OWNER);
	CHECK(!SG_MoverTicketValid(&rotated));
	CHECK(SG_MoverLeaseQuarantine(&registry, &ticket, NULL) ==
	      SG_MOVER_LEASE_INVALID_OWNER);
	CHECK(SG_MoverLeaseReleaseProvedClear(&registry, &ticket, NULL) ==
	      SG_MOVER_LEASE_INVALID_OWNER);
	CHECK(memcmp(&snapshot, &registry, sizeof(registry)) == 0);
	CHECK(SG_MoverLeaseSetState(&registry, &ticket, &wrong,
	      SG_MOVER_LEASE_PAUSED) == SG_MOVER_LEASE_OWNER_MISMATCH);
	CHECK(SG_MoverLeaseSetState(&registry, &ticket, &bot,
	      SG_MOVER_LEASE_PAUSED) == SG_MOVER_LEASE_OK);
	CHECK(SG_MoverLeaseValidate(&registry, &ticket, &bot, &record) ==
	      SG_MOVER_LEASE_OK && record.state == SG_MOVER_LEASE_PAUSED);
	CHECK(SG_MoverLeaseSetState(&registry, &ticket, &bot,
	      SG_MOVER_LEASE_ACTIVE) == SG_MOVER_LEASE_OK);

	stale = ticket;
	CHECK(SG_MoverLeaseOrphan(&registry, &ticket, &bot, &client, &bolt,
	      &ticket) == SG_MOVER_LEASE_OK);
	CHECK(ticket.serial != stale.serial);
	CHECK(SG_MoverLeaseValidate(&registry, &stale, &bot, NULL) ==
	      SG_MOVER_LEASE_STALE_TICKET);
	CHECK(SG_MoverLeaseValidate(&registry, &ticket, &bot, &record) ==
	      SG_MOVER_LEASE_OK && record.state == SG_MOVER_LEASE_ORPHAN);
	CHECK(SG_MoverSubjectEqual(&record.body, &client));
	CHECK(SG_MoverSubjectEqual(&record.bolt, &bolt));
	CHECK(SG_MoverLeaseTransferSubject(&registry, &ticket, &bot,
	      &bolt, &body, &rotated) == SG_MOVER_LEASE_INVALID_SUBJECT);
	CHECK(SG_MoverLeaseTransferSubject(&registry, &ticket, &bot,
	      &client, &bolt, &rotated) == SG_MOVER_LEASE_INVALID_SUBJECT);
	CHECK(SG_MoverLeaseTransferSubject(&registry, &ticket, &bot,
	      &client, &retyped_same, &rotated) ==
	      SG_MOVER_LEASE_INVALID_SUBJECT);
	CHECK(!SG_MoverTicketValid(&rotated));
	CHECK(SG_MoverLeaseAcquire(&registry, overlaps, 2U, &wrong,
	      SG_MOVER_LAW_DECLARED_DOOR, 23, 0U, &rotated) ==
	      SG_MOVER_LEASE_CONFLICT);
	CHECK(SG_MoverLeaseTransferSubject(&registry, &ticket, NULL,
	      &client, &body, &rotated) == SG_MOVER_LEASE_INVALID_OWNER);
	CHECK(SG_MoverLeaseEvictSubject(&registry, &ticket, NULL, &client,
	      &rotated) == SG_MOVER_LEASE_INVALID_OWNER);
	CHECK(!SG_MoverTicketValid(&rotated));

	stale = ticket;
	CHECK(SG_MoverLeaseTransferSubject(&registry, &ticket, &bot,
	      &client, &body, &ticket) == SG_MOVER_LEASE_OK);
	CHECK(ticket.serial != stale.serial);
	CHECK(SG_MoverLeaseValidate(&registry, &stale, &bot, NULL) ==
	      SG_MOVER_LEASE_STALE_TICKET);
	stale = ticket;
	stale.reserved = 1U;
	CHECK(SG_MoverLeaseSetState(&registry, &stale, &bot,
	      SG_MOVER_LEASE_PAUSED) == SG_MOVER_LEASE_INVALID_ARGUMENT);
	CHECK(SG_MoverLeaseValidate(&registry, &ticket, &bot, &record) ==
	      SG_MOVER_LEASE_OK);
	CHECK(SG_MoverSubjectEqual(&record.body, &body));

	CHECK(SG_MoverLeaseEvictSubject(&registry, &ticket, &bot, &bolt,
	      &ticket) == SG_MOVER_LEASE_OK);
	CHECK(SG_MoverLeaseValidate(&registry, &ticket, &bot, &record) ==
	      SG_MOVER_LEASE_OK && record.bolt.kind == SG_MOVER_SUBJECT_NONE);
	CHECK(SG_MoverLeaseEvictSubject(&registry, &ticket, &bot, &body,
	      &ticket) == SG_MOVER_LEASE_OK);
	CHECK(SG_MoverLeaseReleaseProvedClear(&registry, &ticket, &bot) ==
	      SG_MOVER_LEASE_OK);

	CHECK(SG_MoverLeaseAcquire(&registry, keys, 3U, &bot,
	      SG_MOVER_LAW_DECLARED_DOOR, 24, 0U, &ticket) ==
	      SG_MOVER_LEASE_OK);
	CHECK(SG_MoverLeaseQuarantine(&registry, &ticket, &bot) ==
	      SG_MOVER_LEASE_OK);
	CHECK(SG_MoverLeaseReleaseProvedClear(&registry, &ticket, &bot) ==
	      SG_MOVER_LEASE_QUARANTINE_LOCKED);
	CHECK(SG_MoverLeaseAcquire(&registry, overlaps, 2U, &wrong,
	      SG_MOVER_LAW_DECLARED_DOOR, 25, 0U, &rotated) ==
	      SG_MOVER_LEASE_CONFLICT);
}

static void TestAtomicExhaustionAndReset(void)
{
	sg_mover_lease_registry_t registry = {0}, snapshot;
	sg_mover_owner_t bot = Owner(5, 15U);
	sg_mover_subject_t client = Subject(SG_MOVER_SUBJECT_CLIENT, 66, 16U);
	sg_mover_ticket_t ticket, rejected, stale;
	sg_mover_key_t key = 31U;
	sg_mover_lease_record_t record;

	SG_MoverLeaseInit(&registry);
	registry.next_serial = UINT64_MAX;
	CHECK(SG_MoverLeaseAcquire(&registry, &key, 1U, &bot,
	      SG_MOVER_LAW_DECLARED_DOOR, 1, 0U, &rejected) ==
	      SG_MOVER_LEASE_EXHAUSTED);
	CHECK(!SG_MoverTicketValid(&rejected));
	CHECK(!SG_MoverLeaseRecordAt(&registry, 0U, &record, &rejected));
	CHECK(registry.serial_exhausted != 0U);

	SG_MoverLeaseLevelReset(&registry);
	CHECK(registry.epoch == 2U && registry.serial_exhausted == 0U);
	CHECK(SG_MoverLeaseAcquire(&registry, &key, 1U, &bot,
	      SG_MOVER_LAW_DECLARED_DOOR, 1, 0U, &ticket) ==
	      SG_MOVER_LEASE_OK);
	registry.next_serial = UINT64_MAX;
	snapshot = registry;
	CHECK(SG_MoverLeaseOrphan(&registry, &ticket, &bot, &client, NULL,
	      &rejected) == SG_MOVER_LEASE_EXHAUSTED);
	CHECK(!SG_MoverTicketValid(&rejected));
	CHECK(memcmp(snapshot.records, registry.records,
	      sizeof(registry.records)) == 0);
	stale = ticket;
	CHECK(SG_MoverLeaseOrphan(&registry, &ticket, &bot, &client, NULL,
	      &ticket) == SG_MOVER_LEASE_EXHAUSTED);
	CHECK(memcmp(&ticket, &stale, sizeof(ticket)) == 0);
	CHECK(SG_MoverLeaseValidate(&registry, &ticket, &bot, NULL) ==
	      SG_MOVER_LEASE_OK);
	CHECK(SG_MoverLeaseQuarantine(&registry, &ticket, &bot) ==
	      SG_MOVER_LEASE_OK);

	registry.epoch = UINT64_MAX;
	SG_MoverLeaseLevelReset(&registry);
	CHECK(registry.epoch_exhausted != 0U);
	CHECK(SG_MoverLeaseAcquire(&registry, &key, 1U, &bot,
	      SG_MOVER_LAW_DECLARED_DOOR, 1, 0U, &rejected) ==
	      SG_MOVER_LEASE_EXHAUSTED);
	CHECK(!SG_MoverTicketValid(&rejected));
	CHECK(SG_MoverLeaseValidate(&registry, &ticket, &bot, NULL) ==
	      SG_MOVER_LEASE_STALE_TICKET);
}

static void TestCapacityInspectionAndLevelABA(void)
{
	sg_mover_lease_registry_t registry = {0};
	sg_mover_ticket_t tickets[SG_MOVER_LEASE_MAX_RECORDS];
	sg_mover_lease_record_t record;
	sg_mover_ticket_t observed, stale;
	sg_mover_owner_t owner;
	sg_mover_key_t key;
	size_t index;

	SG_MoverLeaseInit(&registry);
	for (index = 0U; index < SG_MOVER_LEASE_MAX_RECORDS; index++)
	{
		owner = Owner((int)index, (uint64_t)index + 1U);
		key = (sg_mover_key_t)(index * 2U + 1U);
		CHECK(SG_MoverLeaseAcquire(&registry, &key, 1U, &owner,
		      SG_MOVER_LAW_DECLARED_DOOR, (int)index, 0U,
		      &tickets[index]) == SG_MOVER_LEASE_OK);
	}
	owner = Owner(99, 100U);
	key = 1000U;
	CHECK(SG_MoverLeaseAcquire(&registry, &key, 1U, &owner,
	      SG_MOVER_LAW_DECLARED_DOOR, 99, 0U, &observed) ==
	      SG_MOVER_LEASE_FULL);
	CHECK(!SG_MoverTicketValid(&observed));
	for (index = 0U; index < SG_MOVER_LEASE_MAX_RECORDS; index++)
	{
		CHECK(SG_MoverLeaseRecordAt(&registry, index, &record, &observed));
		CHECK(observed.slot == index && observed.serial == record.serial);
		CHECK(record.key_count == 1U &&
		      record.keys[0] == index * 2U + 1U);
	}
	stale = tickets[0];
	SG_MoverLeaseLevelReset(&registry);
	CHECK(SG_MoverLeaseValidate(&registry, &stale, NULL, NULL) ==
	      SG_MOVER_LEASE_STALE_TICKET);
	CHECK(!SG_MoverLeaseRecordAt(&registry, 0U, &record, &observed));
}

static void TestRepeatedInitInvalidatesTickets(void)
{
	sg_mover_lease_registry_t registry = {0};
	sg_mover_owner_t owner = Owner(7, 71U);
	sg_mover_key_t key = 77U;
	sg_mover_ticket_t first, second;

	SG_MoverLeaseInit(&registry);
	CHECK(SG_MoverLeaseAcquire(&registry, &key, 1U, &owner,
	      SG_MOVER_LAW_DECLARED_DOOR, 7, 0U, &first) ==
	      SG_MOVER_LEASE_OK);
	SG_MoverLeaseInit(&registry);
	CHECK(SG_MoverLeaseAcquire(&registry, &key, 1U, &owner,
	      SG_MOVER_LAW_DECLARED_DOOR, 7, 0U, &second) ==
	      SG_MOVER_LEASE_OK);
	CHECK(first.epoch != second.epoch);
	CHECK(SG_MoverLeaseValidate(&registry, &first, &owner, NULL) ==
	      SG_MOVER_LEASE_STALE_TICKET);
}

static void TestReasons(void)
{
	int result;

	for (result = SG_MOVER_LEASE_OK;
	     result <= SG_MOVER_LEASE_QUARANTINE_LOCKED; result++)
		CHECK(strcmp(SG_MoverLeaseReason((sg_mover_lease_result_t)result),
		      "unknown mover lease result") != 0);
	CHECK(strcmp(SG_MoverLeaseReason((sg_mover_lease_result_t)-1),
	      "unknown mover lease result") == 0);
}

int main(void)
{
	TestInputContract();
	TestOverlapOwnerAndReuse();
	TestPauseOrphanSubjectsAndQuarantine();
	TestAtomicExhaustionAndReset();
	TestCapacityInspectionAndLevelABA();
	TestRepeatedInitInvalidatesTickets();
	TestReasons();
	if (failures != 0)
	{
		fprintf(stderr, "mover lease tests failed: %d\n", failures);
		return 1;
	}
	puts("sg_mover_lease_test: ok");
	return 0;
}
