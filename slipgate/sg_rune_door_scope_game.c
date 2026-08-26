/* Game-world adapter for transactional generator door scopes. */
#include "../g_local.h"
#include "sg_local.h"
#include "sg_hooks.h"
#include "sg_rune_door_scope_game.h"

#include <limits.h>
#include <string.h>

static void *DoorScopeAllocate(void *context, size_t size)
{
	(void)context;
	if (size > (size_t)INT_MAX)
		return NULL;
	return sg_host.level_alloc((int)size);
}

static void DoorScopeDeallocate(void *context, void *block)
{
	(void)context;
	sg_host.level_free(block);
}

static int DoorScopeTargetIdentity(void *context, void *opaque, int key)
{
	edict_t *entity = opaque;

	(void)context;
	return entity && g_edicts && key >= 0 && key < globals.num_edicts &&
	       entity == &g_edicts[key] && entity->inuse && entity->classname &&
	       strncmp(entity->classname, "func_door", 9) == 0;
}

static int DoorScopeGetSolid(void *context, void *opaque)
{
	(void)context;
	return (int)((edict_t *)opaque)->solid;
}

static int DoorScopeGetLinkcount(void *context, void *opaque)
{
	(void)context;
	return ((edict_t *)opaque)->linkcount;
}

static void DoorScopeSetSolid(void *context, void *opaque, int solid)
{
	(void)context;
	((edict_t *)opaque)->solid = (solid_t)solid;
}

static void DoorScopeSetLinkcount(void *context, void *opaque, int linkcount)
{
	(void)context;
	((edict_t *)opaque)->linkcount = linkcount;
}

static void DoorScopeLinkEntity(void *context, void *opaque)
{
	(void)context;
	sg_host.linkentity((edict_t *)opaque);
}

static const sg_rune_door_scope_ops_t door_scope_ops = {
	DoorScopeAllocate,
	DoorScopeDeallocate,
	DoorScopeTargetIdentity,
	DoorScopeGetSolid,
	DoorScopeGetLinkcount,
	DoorScopeSetSolid,
	DoorScopeSetLinkcount,
	DoorScopeLinkEntity
};

sg_rune_door_scope_status_t SG_RuneDoorScopeGameOpen(
	sg_rune_door_scope_t *scope)
{
	sg_rune_door_scope_target_t *targets = NULL;
	size_t count = 0U;
	int index;
	sg_rune_door_scope_status_t status;

	if (!scope || !g_edicts || globals.num_edicts < 0)
		return SG_RUNE_DOOR_SCOPE_INVALID_ARGUMENT;
	for (index = 0; index < globals.num_edicts; index++)
	{
		edict_t *entity = &g_edicts[index];
		if (!entity->inuse || !entity->classname ||
		    strncmp(entity->classname, "func_door", 9) != 0)
			continue;
		count++;
	}
	if (count > (size_t)INT_MAX / sizeof(*targets))
		return SG_RUNE_DOOR_SCOPE_CAPACITY;
	if (count)
	{
		targets = sg_host.level_alloc((int)(count * sizeof(*targets)));
		if (!targets)
			return SG_RUNE_DOOR_SCOPE_CAPACITY;
		count = 0U;
		for (index = 0; index < globals.num_edicts; index++)
		{
			edict_t *entity = &g_edicts[index];

			if (!entity->inuse || !entity->classname ||
			    strncmp(entity->classname, "func_door", 9) != 0)
				continue;
			targets[count].entity = entity;
			targets[count].key = index;
			count++;
		}
	}
	status = SG_RuneDoorScopeOpen(scope, targets, count, (int)SOLID_NOT,
		&door_scope_ops, NULL);
	if (targets)
		sg_host.level_free(targets);
	return status;
}

sg_rune_door_scope_status_t SG_RuneDoorScopeGameRestore(
	sg_rune_door_scope_t *scope)
{
	return SG_RuneDoorScopeRestore(scope, &door_scope_ops, NULL);
}
