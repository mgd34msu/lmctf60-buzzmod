/* sg_rune_door_scope.c -- checked temporary door-solid transaction. */
#include "sg_rune_door_scope.h"

#include <string.h>

static int DoorScopeOpsValid(const sg_rune_door_scope_ops_t *ops)
{
	return ops && ops->identity_matches && ops->get_solid &&
	       ops->get_linkcount && ops->set_solid && ops->set_linkcount &&
	       ops->link_entity;
}

static int DoorScopeRestoreEntries(sg_rune_door_scope_t *scope,
	const sg_rune_door_scope_ops_t *ops, void *context)
{
	size_t index;
	int restored = 1;
	int pending = 0;

	if (!scope || !DoorScopeOpsValid(ops))
		return 0;
	/* Do not stop at an identity mismatch.  The generator is synchronous and
	 * every stored pointer still names its snapshotted edict slot; restoring all
	 * scalar and link state is safer than abandoning later entries half-open. */
	for (index = scope->count; index > 0; index--)
	{
		sg_rune_door_scope_entry_t *entry = &scope->entries[index - 1];
		int verified;

		if (!entry->changed)
			continue;
		ops->set_solid(context, entry->entity, entry->solid);
		ops->link_entity(context, entry->entity);
		/* Relinking increments an edict's observable link generation.  Generation
		 * is a temporary proof transaction, so restore that generation exactly. */
		ops->set_linkcount(context, entry->entity, entry->linkcount);
		verified = ops->identity_matches(context, entry->entity, entry->key) &&
		           ops->get_solid(context, entry->entity) == entry->solid &&
		           ops->get_linkcount(context, entry->entity) ==
		               entry->linkcount;
		if (!verified)
		{
			restored = 0;
			pending = 1;
		}
		else
			entry->changed = 0;
	}
	/* A failed verification remains an active restoration obligation.  The
	 * caller may repair transient identity/link state and retry without
	 * re-touching entries that already verified. */
	scope->active = pending;
	return restored;
}

void SG_RuneDoorScopeInit(sg_rune_door_scope_t *scope)
{
	if (scope)
		memset(scope, 0, sizeof(*scope));
}

int SG_RuneDoorScopeActive(const sg_rune_door_scope_t *scope)
{
	return scope && scope->active;
}

sg_rune_door_scope_status_t SG_RuneDoorScopeOpen(
	sg_rune_door_scope_t *scope,
	const sg_rune_door_scope_target_t *targets, size_t target_count,
	int open_solid, const sg_rune_door_scope_ops_t *ops, void *context)
{
	size_t index, prior;

	if (!scope || !DoorScopeOpsValid(ops) ||
	    (target_count != 0 && !targets))
		return SG_RUNE_DOOR_SCOPE_INVALID_ARGUMENT;
	if (scope->active)
		return SG_RUNE_DOOR_SCOPE_BUSY;
	if (target_count > SG_RUNE_DOOR_SCOPE_MAX)
		return SG_RUNE_DOOR_SCOPE_CAPACITY;

	/* Snapshot and validate the complete target set before the first mutation.
	 * Capacity, duplicate, or identity failure therefore leaves every door in
	 * its original linked state. */
	memset(scope->entries, 0, sizeof(scope->entries));
	scope->count = 0;
	for (index = 0; index < target_count; index++)
	{
		sg_rune_door_scope_entry_t *entry = &scope->entries[index];

		if (!targets[index].entity || targets[index].key < 0 ||
		    !ops->identity_matches(context, targets[index].entity,
		                           targets[index].key))
			return SG_RUNE_DOOR_SCOPE_PREFLIGHT_FAILED;
		for (prior = 0; prior < index; prior++)
			if (targets[prior].entity == targets[index].entity ||
			    targets[prior].key == targets[index].key)
				return SG_RUNE_DOOR_SCOPE_PREFLIGHT_FAILED;
		entry->entity = targets[index].entity;
		entry->key = targets[index].key;
		entry->solid = ops->get_solid(context, entry->entity);
		entry->linkcount = ops->get_linkcount(context, entry->entity);
		scope->count = index + 1;
	}

	scope->active = 1;
	for (index = 0; index < scope->count; index++)
	{
		sg_rune_door_scope_entry_t *entry = &scope->entries[index];

		/* Mark before calling out: even a callback that partially mutates and
		 * then exposes failed verification is covered by the rollback. */
		entry->changed = 1;
		ops->set_solid(context, entry->entity, open_solid);
		ops->link_entity(context, entry->entity);
		if (!ops->identity_matches(context, entry->entity, entry->key) ||
		    ops->get_solid(context, entry->entity) != open_solid)
		{
			if (!DoorScopeRestoreEntries(scope, ops, context))
				return SG_RUNE_DOOR_SCOPE_RESTORE_FAILED;
			return SG_RUNE_DOOR_SCOPE_OPEN_FAILED;
		}
	}
	return SG_RUNE_DOOR_SCOPE_OK;
}

sg_rune_door_scope_status_t SG_RuneDoorScopeRestore(
	sg_rune_door_scope_t *scope,
	const sg_rune_door_scope_ops_t *ops, void *context)
{
	if (!scope || !DoorScopeOpsValid(ops))
		return SG_RUNE_DOOR_SCOPE_INVALID_ARGUMENT;
	if (!scope->active)
		return SG_RUNE_DOOR_SCOPE_BUSY;
	return DoorScopeRestoreEntries(scope, ops, context)
	    ? SG_RUNE_DOOR_SCOPE_OK : SG_RUNE_DOOR_SCOPE_RESTORE_FAILED;
}

const char *SG_RuneDoorScopeStatusName(sg_rune_door_scope_status_t status)
{
	switch (status)
	{
	case SG_RUNE_DOOR_SCOPE_OK: return "ok";
	case SG_RUNE_DOOR_SCOPE_INVALID_ARGUMENT: return "invalid-argument";
	case SG_RUNE_DOOR_SCOPE_BUSY: return "busy";
	case SG_RUNE_DOOR_SCOPE_CAPACITY: return "capacity";
	case SG_RUNE_DOOR_SCOPE_PREFLIGHT_FAILED: return "preflight-failed";
	case SG_RUNE_DOOR_SCOPE_OPEN_FAILED: return "open-failed";
	case SG_RUNE_DOOR_SCOPE_RESTORE_FAILED: return "restore-failed";
	default: return "unknown";
	}
}
