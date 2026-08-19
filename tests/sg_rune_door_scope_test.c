/* Focused host tests for the generator's checked door-solid transaction. */
#include <stdio.h>
#include <string.h>

#include "slipgate/sg_rune_door_scope.h"

static int failures;

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #condition); \
		failures++; \
	} \
} while (0)

typedef struct fake_door_s
{
	int key;
	int valid;
	int solid;
	int linkcount;
} fake_door_t;

typedef struct fake_context_s
{
	int open_solid;
	int transient_open_failure_key;
	int link_calls;
} fake_context_t;

static int FakeIdentity(void *opaque, void *entity, int key)
{
	fake_context_t *context = opaque;
	fake_door_t *door = entity;

	if (!context || !door || !door->valid || door->key != key)
		return 0;
	if (context->transient_open_failure_key == key &&
	    door->solid == context->open_solid)
	{
		context->transient_open_failure_key = -1;
		return 0;
	}
	return 1;
}

static int FakeGetSolid(void *opaque, void *entity)
{
	(void)opaque;
	return ((fake_door_t *)entity)->solid;
}

static int FakeGetLinkcount(void *opaque, void *entity)
{
	(void)opaque;
	return ((fake_door_t *)entity)->linkcount;
}

static void FakeSetSolid(void *opaque, void *entity, int solid)
{
	(void)opaque;
	((fake_door_t *)entity)->solid = solid;
}

static void FakeSetLinkcount(void *opaque, void *entity, int linkcount)
{
	(void)opaque;
	((fake_door_t *)entity)->linkcount = linkcount;
}

static void FakeLink(void *opaque, void *entity)
{
	fake_context_t *context = opaque;
	fake_door_t *door = entity;

	context->link_calls++;
	door->linkcount++;
}

static const sg_rune_door_scope_ops_t fake_ops = {
	FakeIdentity,
	FakeGetSolid,
	FakeGetLinkcount,
	FakeSetSolid,
	FakeSetLinkcount,
	FakeLink
};

static void InitDoors(fake_door_t *doors,
	sg_rune_door_scope_target_t *targets, size_t count)
{
	size_t index;

	for (index = 0; index < count; index++)
	{
		doors[index].key = (int)index + 1;
		doors[index].valid = 1;
		doors[index].solid = 10 + (int)index;
		doors[index].linkcount = 100 + (int)index;
		targets[index].entity = &doors[index];
		targets[index].key = doors[index].key;
	}
}

static fake_context_t FreshContext(void)
{
	fake_context_t context;

	memset(&context, 0, sizeof(context));
	context.open_solid = -7;
	context.transient_open_failure_key = -1;
	return context;
}

static void TestCapacityAndPreflightAreMutationFree(void)
{
	fake_door_t doors[SG_RUNE_DOOR_SCOPE_MAX + 1];
	sg_rune_door_scope_target_t targets[SG_RUNE_DOOR_SCOPE_MAX + 1];
	sg_rune_door_scope_t scope;
	fake_context_t context = FreshContext();

	InitDoors(doors, targets, SG_RUNE_DOOR_SCOPE_MAX + 1);
	SG_RuneDoorScopeInit(&scope);
	CHECK(SG_RuneDoorScopeOpen(&scope, targets,
		SG_RUNE_DOOR_SCOPE_MAX + 1, context.open_solid,
		&fake_ops, &context) == SG_RUNE_DOOR_SCOPE_CAPACITY);
	CHECK(context.link_calls == 0);
	CHECK(doors[0].solid == 10 && doors[0].linkcount == 100);
	CHECK(doors[SG_RUNE_DOOR_SCOPE_MAX].solid ==
		10 + SG_RUNE_DOOR_SCOPE_MAX);
	CHECK(!SG_RuneDoorScopeActive(&scope));

	InitDoors(doors, targets, 3);
	doors[2].valid = 0;
	CHECK(SG_RuneDoorScopeOpen(&scope, targets, 3,
		context.open_solid, &fake_ops, &context) ==
		SG_RUNE_DOOR_SCOPE_PREFLIGHT_FAILED);
	CHECK(context.link_calls == 0);
	CHECK(doors[0].solid == 10 && doors[1].solid == 11 &&
		doors[2].solid == 12);
	CHECK(!SG_RuneDoorScopeActive(&scope));

	doors[2].valid = 1;
	targets[2] = targets[1];
	CHECK(SG_RuneDoorScopeOpen(&scope, targets, 3,
		context.open_solid, &fake_ops, &context) ==
		SG_RUNE_DOOR_SCOPE_PREFLIGHT_FAILED);
	CHECK(context.link_calls == 0);
}

static void TestExactSuccessfulRestore(void)
{
	fake_door_t doors[3];
	sg_rune_door_scope_target_t targets[3];
	sg_rune_door_scope_t scope;
	fake_context_t context = FreshContext();
	int index;

	InitDoors(doors, targets, 3);
	SG_RuneDoorScopeInit(&scope);
	CHECK(SG_RuneDoorScopeOpen(&scope, targets, 3,
		context.open_solid, &fake_ops, &context) ==
		SG_RUNE_DOOR_SCOPE_OK);
	CHECK(SG_RuneDoorScopeActive(&scope));
	CHECK(scope.count == 3);
	CHECK(context.link_calls == 3);
	for (index = 0; index < 3; index++)
	{
		CHECK(doors[index].solid == context.open_solid);
		CHECK(doors[index].linkcount == 101 + index);
	}
	CHECK(SG_RuneDoorScopeOpen(&scope, targets, 3,
		context.open_solid, &fake_ops, &context) ==
		SG_RUNE_DOOR_SCOPE_BUSY);
	CHECK(SG_RuneDoorScopeRestore(&scope, &fake_ops, &context) ==
		SG_RUNE_DOOR_SCOPE_OK);
	CHECK(context.link_calls == 6);
	CHECK(!SG_RuneDoorScopeActive(&scope));
	for (index = 0; index < 3; index++)
	{
		CHECK(doors[index].solid == 10 + index);
		CHECK(doors[index].linkcount == 100 + index);
	}
}

static void TestOpenFailureRollsBackEveryChangedEntry(void)
{
	fake_door_t doors[3];
	sg_rune_door_scope_target_t targets[3];
	sg_rune_door_scope_t scope;
	fake_context_t context = FreshContext();
	int index;

	InitDoors(doors, targets, 3);
	context.transient_open_failure_key = doors[1].key;
	SG_RuneDoorScopeInit(&scope);
	CHECK(SG_RuneDoorScopeOpen(&scope, targets, 3,
		context.open_solid, &fake_ops, &context) ==
		SG_RUNE_DOOR_SCOPE_OPEN_FAILED);
	CHECK(context.link_calls == 4); /* two opens, then two restorations */
	CHECK(!SG_RuneDoorScopeActive(&scope));
	for (index = 0; index < 3; index++)
	{
		CHECK(doors[index].solid == 10 + index);
		CHECK(doors[index].linkcount == 100 + index);
	}
}

static void TestRestoreFailureIsRetryable(void)
{
	fake_door_t doors[3];
	sg_rune_door_scope_target_t targets[3];
	sg_rune_door_scope_t scope;
	fake_context_t context = FreshContext();
	int index;

	InitDoors(doors, targets, 3);
	SG_RuneDoorScopeInit(&scope);
	CHECK(SG_RuneDoorScopeOpen(&scope, targets, 3,
		context.open_solid, &fake_ops, &context) ==
		SG_RUNE_DOOR_SCOPE_OK);
	/* Simulate identity drift. Restoration must report it, but must not stop
	 * before restoring this or any later snapshotted entry. */
	doors[1].valid = 0;
	CHECK(SG_RuneDoorScopeRestore(&scope, &fake_ops, &context) ==
		SG_RUNE_DOOR_SCOPE_RESTORE_FAILED);
	CHECK(context.link_calls == 6);
	CHECK(SG_RuneDoorScopeActive(&scope));
	for (index = 0; index < 3; index++)
	{
		CHECK(doors[index].solid == 10 + index);
		CHECK(doors[index].linkcount == 100 + index);
	}
	doors[1].valid = 1;
	CHECK(SG_RuneDoorScopeRestore(&scope, &fake_ops, &context) ==
		SG_RUNE_DOOR_SCOPE_OK);
	/* Only the one still-pending entry is relinked by the retry. */
	CHECK(context.link_calls == 7);
	CHECK(!SG_RuneDoorScopeActive(&scope));
}

static void TestPersistentRestoreFailureRemainsPending(void)
{
	fake_door_t doors[2];
	sg_rune_door_scope_target_t targets[2];
	sg_rune_door_scope_t scope;
	fake_context_t context = FreshContext();

	InitDoors(doors, targets, 2);
	SG_RuneDoorScopeInit(&scope);
	CHECK(SG_RuneDoorScopeOpen(&scope, targets, 2,
		context.open_solid, &fake_ops, &context) ==
		SG_RUNE_DOOR_SCOPE_OK);
	doors[0].valid = 0;
	CHECK(SG_RuneDoorScopeRestore(&scope, &fake_ops, &context) ==
		SG_RUNE_DOOR_SCOPE_RESTORE_FAILED);
	CHECK(SG_RuneDoorScopeActive(&scope));
	CHECK(SG_RuneDoorScopeRestore(&scope, &fake_ops, &context) ==
		SG_RUNE_DOOR_SCOPE_RESTORE_FAILED);
	CHECK(SG_RuneDoorScopeActive(&scope));
	CHECK(doors[0].solid == 10 && doors[0].linkcount == 100);
	CHECK(doors[1].solid == 11 && doors[1].linkcount == 101);
}

int main(void)
{
	TestCapacityAndPreflightAreMutationFree();
	TestExactSuccessfulRestore();
	TestOpenFailureRollsBackEveryChangedEntry();
	TestRestoreFailureIsRetryable();
	TestPersistentRestoreFailureRemainsPending();
	CHECK(strcmp(SG_RuneDoorScopeStatusName(
		SG_RUNE_DOOR_SCOPE_RESTORE_FAILED), "restore-failed") == 0);
	if (failures)
	{
		fprintf(stderr, "sg_rune_door_scope_test: %d failure(s)\n",
			failures);
		return 1;
	}
	puts("sg_rune_door_scope_test: ok");
	return 0;
}
