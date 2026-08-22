/* sg_rune_mechanism_catalog.c -- sealed post-spawn mechanism inventory. */
#include "../g_local.h"
#include "sg_local.h"
#include "sg_hooks.h"
#include "sg_rune_mechanism_catalog.h"
#include "sg_util.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Stock callbacks are data.  Their addresses are compared only;
 * the catalog never invokes one. */
extern void Touch_Multi(edict_t *, edict_t *, cplane_t *, csurface_t *);
extern void Touch_DoorTrigger(edict_t *, edict_t *, cplane_t *, csurface_t *);
extern void button_touch(edict_t *, edict_t *, cplane_t *, csurface_t *);
extern void Touch_Plat_Center(edict_t *, edict_t *, cplane_t *, csurface_t *);
extern void trigger_push_touch(edict_t *, edict_t *, cplane_t *, csurface_t *);
extern void teleporter_touch(edict_t *, edict_t *, cplane_t *, csurface_t *);
extern void path_corner_touch(edict_t *, edict_t *, cplane_t *, csurface_t *);
extern void Touch_Item(edict_t *, edict_t *, cplane_t *, csurface_t *);

extern void Use_Multi(edict_t *, edict_t *, edict_t *);
extern void button_use(edict_t *, edict_t *, edict_t *);
extern void trigger_relay_use(edict_t *, edict_t *, edict_t *);
extern void door_use(edict_t *, edict_t *, edict_t *);
extern void trigger_enable(edict_t *, edict_t *, edict_t *);
extern void Use_Plat(edict_t *, edict_t *, edict_t *);
extern void train_use(edict_t *, edict_t *, edict_t *);
extern void trigger_elevator_use(edict_t *, edict_t *, edict_t *);
extern void door_secret_use(edict_t *, edict_t *, edict_t *);
extern void Use_Target_Speaker(edict_t *, edict_t *, edict_t *);
extern void Use_Areaportal(edict_t *, edict_t *, edict_t *);

extern void multi_wait(edict_t *);
extern void button_wait(edict_t *);
extern void button_return(edict_t *);
extern void button_done(edict_t *);
extern void button_killed(edict_t *, edict_t *, edict_t *, int, vec3_t);
extern void Think_CalcMoveSpeed(edict_t *);
extern void Think_SpawnDoorTrigger(edict_t *);
extern void plat_go_down(edict_t *);
extern void plat_hit_top(edict_t *);
extern void plat_hit_bottom(edict_t *);
extern void door_go_down(edict_t *);
extern void door_hit_top(edict_t *);
extern void door_hit_bottom(edict_t *);
extern void Move_Begin(edict_t *);
extern void Move_Final(edict_t *);
extern void Move_Done(edict_t *);
extern void AngleMove_Begin(edict_t *);
extern void AngleMove_Final(edict_t *);
extern void AngleMove_Done(edict_t *);
extern void Think_AccelMove(edict_t *);
extern void func_train_find(edict_t *);
extern void train_next(edict_t *);
extern void train_wait(edict_t *);
extern void trigger_elevator_init(edict_t *);
extern void Think_Delay(edict_t *);

extern void door_blocked(edict_t *, edict_t *);
extern void plat_blocked(edict_t *, edict_t *);
extern void train_blocked(edict_t *, edict_t *);
extern void door_secret_blocked(edict_t *, edict_t *);

typedef struct sg_mech_source_s
{
	uint32_t ordinal;
	const char *original_classname;
	edict_t *synthetic_parent;
	sg_mech_synthetic_kind_t synthetic_kind;
} sg_mech_source_t;

typedef struct sg_mech_button_motion_s
{
	sg_mech_button_endpoints_t endpoints;
	uint8_t valid;
} sg_mech_button_motion_t;

typedef struct sg_mech_catalog_s
{
	sg_mech_catalog_status_t status;
	const char *reason;
	sg_mech_source_t *sources;
	uint32_t source_capacity;
	uint32_t next_generation;
	uint32_t *live_generations;
	uint32_t *sealed_generations;
	sg_mech_button_motion_t *button_motion;
	rune_mechanism_node_t *nodes;
	uint32_t num_nodes;
	rune_mechanism_edge_t *edges;
	uint32_t num_edges;
	unsigned char *strings;
	uint32_t string_bytes;
} sg_mech_catalog_t;

static sg_mech_catalog_t catalog;

static void *Catalog_Alloc(size_t bytes)
{
	if (bytes == 0U || bytes > (size_t)INT_MAX || !sg_host.level_alloc)
		return NULL;
	return sg_host.level_alloc((int)bytes);
}

static int Catalog_EntityIndex(const edict_t *entity, uint32_t *index_out)
{
	uintptr_t address;
	uintptr_t base;
	uintptr_t offset;
	size_t span;

	if (!entity || !g_edicts || !index_out ||
	    catalog.source_capacity == 0U)
		return 0;
	span = (size_t)catalog.source_capacity * sizeof(*g_edicts);
	if (span / sizeof(*g_edicts) != (size_t)catalog.source_capacity)
		return 0;
	address = (uintptr_t)(const void *)entity;
	base = (uintptr_t)(const void *)g_edicts;
	if (address <= base)
		return 0;
	offset = address - base;
	if (offset >= (uintptr_t)span ||
	    offset % (uintptr_t)sizeof(*g_edicts) != 0U)
		return 0;
	*index_out = (uint32_t)(offset / (uintptr_t)sizeof(*g_edicts));
	return 1;
}

void SG_MechCatalogBegin(void)
{
	size_t bytes;

	memset(&catalog, 0, sizeof(catalog));
	catalog.status = SG_MECH_CATALOG_BUILDING;
	catalog.reason = "building";
	SG_HooksInit();
	if (game.maxentities <= 1 ||
	    (size_t)game.maxentities > SIZE_MAX / sizeof(*catalog.sources))
	{
		catalog.status = SG_MECH_CATALOG_FAILED;
		catalog.reason = "invalid entity capacity";
		return;
	}
	catalog.source_capacity = (uint32_t)game.maxentities;
	bytes = (size_t)catalog.source_capacity * sizeof(*catalog.sources);
	catalog.sources = Catalog_Alloc(bytes);
	catalog.live_generations = Catalog_Alloc((size_t)catalog.source_capacity *
		sizeof(*catalog.live_generations));
	catalog.sealed_generations = Catalog_Alloc((size_t)catalog.source_capacity *
		sizeof(*catalog.sealed_generations));
	catalog.button_motion = Catalog_Alloc((size_t)catalog.source_capacity *
		sizeof(*catalog.button_motion));
	if (!catalog.sources || !catalog.live_generations ||
	    !catalog.sealed_generations || !catalog.button_motion)
	{
		catalog.status = SG_MECH_CATALOG_FAILED;
		catalog.reason = "source incarnation allocation";
		return;
	}
	memset(catalog.sources, 0, bytes);
	memset(catalog.live_generations, 0,
		(size_t)catalog.source_capacity * sizeof(*catalog.live_generations));
	memset(catalog.sealed_generations, 0,
		(size_t)catalog.source_capacity * sizeof(*catalog.sealed_generations));
	memset(catalog.button_motion, 0,
		(size_t)catalog.source_capacity * sizeof(*catalog.button_motion));
	catalog.next_generation = 1U;
}

void SG_MechCatalogEntityInitialized(edict_t *entity)
{
	uint32_t index;

	if (!catalog.live_generations || !Catalog_EntityIndex(entity, &index))
		return;
	if (catalog.next_generation == 0U)
	{
		catalog.live_generations[index] = 0U;
		if (catalog.status == SG_MECH_CATALOG_BUILDING)
		{
			catalog.status = SG_MECH_CATALOG_FAILED;
			catalog.reason = "entity generation overflow";
		}
		return;
	}
	catalog.live_generations[index] = catalog.next_generation++;
}

void SG_MechCatalogDeclared(edict_t *entity, uint32_t source_ordinal,
	const char *original_classname)
{
	uint32_t index;

	if (catalog.status != SG_MECH_CATALOG_BUILDING ||
	    !Catalog_EntityIndex(entity, &index))
		return;
	catalog.sources[index].ordinal = source_ordinal;
	catalog.sources[index].original_classname = original_classname;
	catalog.sources[index].synthetic_parent = NULL;
	catalog.sources[index].synthetic_kind = SG_MECH_SYNTHETIC_NONE;
}

void SG_MechCatalogSynthetic(edict_t *entity, edict_t *parent,
	sg_mech_synthetic_kind_t kind)
{
	uint32_t index;

	if (catalog.status != SG_MECH_CATALOG_BUILDING ||
	    kind <= SG_MECH_SYNTHETIC_NONE ||
	    kind > SG_MECH_SYNTHETIC_TELEPORT ||
	    !Catalog_EntityIndex(entity, &index))
		return;
	memset(&catalog.sources[index], 0, sizeof(catalog.sources[index]));
	catalog.sources[index].synthetic_parent = parent;
	catalog.sources[index].synthetic_kind = kind;
}

void SG_MechCatalogInvalidate(edict_t *entity)
{
	uint32_t index;

	if (!Catalog_EntityIndex(entity, &index))
		return;
	if (catalog.live_generations)
		catalog.live_generations[index] = 0U;
	if (catalog.status == SG_MECH_CATALOG_BUILDING)
		memset(&catalog.sources[index], 0, sizeof(catalog.sources[index]));
}

static const char *Catalog_Classname(uint32_t index, const edict_t *entity)
{
	(void)index;
	/* The sealed catalog describes the effective post-spawn entity, not the
	 * declaration that produced it.  Source ordinal/original classname remain
	 * private provenance and never replace runtime state. */
	return entity ? entity->classname : NULL;
}

static int Catalog_TriggeredDoorLiftPair(uint32_t key,
	uint32_t *trigger_key_out, uint32_t *mover_key_out)
{
	uint32_t trigger_key;

	if (trigger_key_out) *trigger_key_out = 0U;
	if (mover_key_out) *mover_key_out = 0U;
	if (!g_edicts || globals.num_edicts <= 1 || key == 0U ||
	    key >= (uint32_t)globals.num_edicts)
		return 0;
	for (trigger_key = 1U;
	     trigger_key < (uint32_t)globals.num_edicts; trigger_key++)
	{
		edict_t *trigger = &g_edicts[trigger_key];
		edict_t *mover = NULL;
		uint32_t mover_key = 0U;
		uint32_t destination;
		uint32_t target_count = 0U;
		int axis;

		if (!trigger->inuse || !trigger->classname ||
		    strcmp(trigger->classname, "trigger_multiple") ||
		    trigger->touch != Touch_Multi || trigger->use != Use_Multi ||
		    trigger->solid != SOLID_TRIGGER || trigger->movetype != MOVETYPE_NONE ||
		    !trigger->target || !trigger->target[0] || trigger->targetname ||
		    trigger->killtarget || trigger->pathtarget || trigger->message ||
		    trigger->delay != 0.0f ||
		    trigger->wait <= 0.0f || (trigger->spawnflags & (2 | 4)) != 0 ||
		    trigger->movedir[0] != 0.0f || trigger->movedir[1] != 0.0f ||
		    trigger->movedir[2] != 0.0f)
			continue;
		for (destination = 1U;
		     destination < (uint32_t)globals.num_edicts; destination++)
		{
			edict_t *candidate = &g_edicts[destination];

			if (!candidate->inuse || !candidate->targetname ||
			    Q_stricmp(candidate->targetname, trigger->target))
				continue;
			target_count++;
			mover = candidate;
			mover_key = destination;
		}
		if (target_count != 1U || !mover || !mover->classname ||
		    strcmp(mover->classname, "func_door") ||
		    mover->use != door_use || mover->blocked != door_blocked ||
		    mover->movetype != MOVETYPE_PUSH || mover->solid != SOLID_BSP ||
		    mover->touch || mover->health || mover->max_health ||
		    mover->takedamage || mover->team || mover->teamchain ||
		    mover->teammaster != mover || (mover->flags & FL_TEAMSLAVE) != 0 ||
		    mover->target || mover->killtarget || mover->pathtarget ||
		    !SG_RuneCarrierDoorSpawnflags((uint32_t)mover->spawnflags) ||
		    mover->moveinfo.wait <= 0.0f ||
		    fabsf(mover->moveinfo.end_origin[2] -
		        mover->moveinfo.start_origin[2]) < 8.0f)
			continue;
		for (axis = 0; axis < 2; axis++)
			if (mover->moveinfo.start_origin[axis] !=
			    mover->moveinfo.end_origin[axis])
				break;
		if (axis != 2)
			continue;
		for (axis = 0; axis < 3; axis++)
		{
			float source_min = mover->moveinfo.start_origin[axis] +
				mover->mins[axis] - 1.0f;
			float source_max = mover->moveinfo.start_origin[axis] +
				mover->maxs[axis] + 1.0f;

			if (trigger->absmin[axis] < source_min ||
			    trigger->absmax[axis] > source_max)
				break;
		}
		if (axis != 3 || (key != trigger_key && key != mover_key))
			continue;
		if (trigger_key_out) *trigger_key_out = trigger_key;
		if (mover_key_out) *mover_key_out = mover_key;
		return 1;
	}
	return 0;
}

static uint16_t Catalog_NodeKind(uint32_t index, const edict_t *entity)
{
	const char *name = Catalog_Classname(index, entity);
	sg_mech_synthetic_kind_t synthetic =
		catalog.sources[index].synthetic_kind;

	if (synthetic == SG_MECH_SYNTHETIC_AUTO_DOOR)
		return SG_MECH_NODE_AUTO_DOOR_TRIGGER;
	if (synthetic == SG_MECH_SYNTHETIC_PLATFORM)
		return SG_MECH_NODE_PLATFORM_TRIGGER;
	if (synthetic == SG_MECH_SYNTHETIC_TELEPORT)
		return SG_MECH_NODE_TELEPORT_TRIGGER;
	if (Catalog_TriggeredDoorLiftPair(index, NULL, NULL))
		return !strcmp(name, "trigger_multiple")
			? SG_MECH_NODE_PLATFORM_TRIGGER : SG_MECH_NODE_PLATFORM;
	if (!name)
		return SG_MECH_NODE_CONTEXTUAL;
	if (!strcmp(name, "func_button"))
		return SG_MECH_NODE_BUTTON;
	if (!strcmp(name, "trigger_relay"))
		return SG_MECH_NODE_RELAY;
	if (!strcmp(name, "func_door") ||
	    !strcmp(name, "func_door_rotating"))
		return (entity->flags & FL_TEAMSLAVE)
			? SG_MECH_NODE_DOOR_MEMBER
			: SG_MECH_NODE_DOOR_MASTER;
	if (!strcmp(name, "func_door_secret"))
		return SG_MECH_NODE_SECRET_DOOR;
	if (!strcmp(name, "func_plat"))
		return SG_MECH_NODE_PLATFORM;
	if (!strcmp(name, "func_train"))
		return SG_MECH_NODE_TRAIN;
	if (!strcmp(name, "path_corner"))
		return SG_MECH_NODE_PATH_CORNER;
	if (!strcmp(name, "trigger_elevator"))
		return SG_MECH_NODE_ELEVATOR;
	if (!strcmp(name, "trigger_push"))
		return SG_MECH_NODE_PUSH;
	if (!strcmp(name, "misc_teleporter"))
		return SG_MECH_NODE_TELEPORTER;
	if (!strcmp(name, "misc_teleporter_dest"))
		return SG_MECH_NODE_TELEPORT_DEST;
	if (!strcmp(name, "target_speaker"))
		return SG_MECH_NODE_TARGET_SPEAKER;
	if (!strcmp(name, "func_areaportal"))
		return SG_MECH_NODE_AREAPORTAL;
	if (!strcmp(name, "info_flag_red") ||
	    !strcmp(name, "info_flag_blue") ||
	    !strcmp(name, "item_flag_team1") ||
	    !strcmp(name, "item_flag_team2"))
		return SG_MECH_NODE_OBJECTIVE;
	if (!strncmp(name, "trigger_", 8))
		return !strcmp(name, "trigger_multiple") ||
		       !strcmp(name, "trigger_once")
			? SG_MECH_NODE_TRIGGER : SG_MECH_NODE_OTHER_TRIGGER;
	if (!strncmp(name, "func_", 5) || entity->movetype == MOVETYPE_PUSH ||
	    entity->movetype == MOVETYPE_STOP)
		return SG_MECH_NODE_OTHER_MOVER;
	return SG_MECH_NODE_CONTEXTUAL;
}

static uint16_t Catalog_TouchCallback(const edict_t *entity)
{
	if (!entity->touch) return SG_MECH_CALLBACK_NONE;
	if (entity->touch == Touch_Multi) return SG_MECH_CALLBACK_TOUCH_MULTI;
	if (entity->touch == Touch_DoorTrigger) return SG_MECH_CALLBACK_TOUCH_DOOR_TRIGGER;
	if (entity->touch == button_touch) return SG_MECH_CALLBACK_BUTTON_TOUCH;
	if (entity->touch == Touch_Plat_Center) return SG_MECH_CALLBACK_TOUCH_PLAT_CENTER;
	if (entity->touch == trigger_push_touch) return SG_MECH_CALLBACK_TRIGGER_PUSH_TOUCH;
	if (entity->touch == teleporter_touch) return SG_MECH_CALLBACK_TELEPORTER_TOUCH;
	if (entity->touch == path_corner_touch) return SG_MECH_CALLBACK_PATH_CORNER_TOUCH;
	if (entity->touch == Touch_Item) return SG_MECH_CALLBACK_TOUCH_ITEM;
	return SG_MECH_CALLBACK_UNKNOWN;
}

static uint16_t Catalog_UseCallback(const edict_t *entity)
{
	if (!entity->use) return SG_MECH_CALLBACK_NONE;
	if (entity->use == Use_Multi) return SG_MECH_CALLBACK_USE_MULTI;
	if (entity->use == button_use) return SG_MECH_CALLBACK_BUTTON_USE;
	if (entity->use == trigger_relay_use) return SG_MECH_CALLBACK_USE_TRIGGER_RELAY;
	if (entity->use == door_use) return SG_MECH_CALLBACK_USE_DOOR;
	if (entity->use == trigger_enable) return SG_MECH_CALLBACK_TRIGGER_ENABLE;
	if (entity->use == Use_Plat) return SG_MECH_CALLBACK_USE_PLAT;
	if (entity->use == train_use) return SG_MECH_CALLBACK_TRAIN_USE;
	if (entity->use == trigger_elevator_use) return SG_MECH_CALLBACK_TRIGGER_ELEVATOR_USE;
	if (entity->use == door_secret_use) return SG_MECH_CALLBACK_SECRET_DOOR_USE;
	if (entity->use == Use_Target_Speaker) return SG_MECH_CALLBACK_USE_TARGET_SPEAKER;
	if (entity->use == Use_Areaportal) return SG_MECH_CALLBACK_USE_AREAPORTAL;
	return SG_MECH_CALLBACK_UNKNOWN;
}

static uint16_t Catalog_ThinkCallback(const edict_t *entity)
{
	if (!entity->think) return SG_MECH_CALLBACK_NONE;
	if (entity->think == multi_wait) return SG_MECH_CALLBACK_THINK_MULTI_WAIT;
	if (entity->think == button_wait) return SG_MECH_CALLBACK_THINK_BUTTON_WAIT;
	if (entity->think == Think_CalcMoveSpeed) return SG_MECH_CALLBACK_THINK_CALC_MOVE_SPEED;
	if (entity->think == Think_SpawnDoorTrigger) return SG_MECH_CALLBACK_THINK_SPAWN_DOOR_TRIGGER;
	if (entity->think == plat_go_down) return SG_MECH_CALLBACK_PLAT_GO_DOWN;
	if (entity->think == func_train_find) return SG_MECH_CALLBACK_FUNC_TRAIN_FIND;
	if (entity->think == train_next) return SG_MECH_CALLBACK_TRAIN_NEXT;
	if (entity->think == train_wait) return SG_MECH_CALLBACK_TRAIN_WAIT;
	if (entity->think == trigger_elevator_init) return SG_MECH_CALLBACK_TRIGGER_ELEVATOR_INIT;
	if (entity->think == Think_Delay) return SG_MECH_CALLBACK_THINK_DELAY;
	return SG_MECH_CALLBACK_UNKNOWN;
}

static uint16_t Catalog_BlockedCallback(const edict_t *entity)
{
	if (!entity->blocked) return SG_MECH_CALLBACK_NONE;
	if (entity->blocked == door_blocked) return SG_MECH_CALLBACK_BLOCKED_DOOR;
	if (entity->blocked == plat_blocked) return SG_MECH_CALLBACK_BLOCKED_PLAT;
	if (entity->blocked == train_blocked) return SG_MECH_CALLBACK_BLOCKED_TRAIN;
	if (entity->blocked == door_secret_blocked) return SG_MECH_CALLBACK_SECRET_DOOR_BLOCKED;
	return SG_MECH_CALLBACK_UNKNOWN;
}

static uint16_t Catalog_ExecutionThinkRole(const edict_t *entity,
	const rune_mechanism_node_t *node)
{
	if (Catalog_ThinkCallback(entity) == node->think_callback)
		return SG_MECH_EXEC_THINK_SEALED;
	if (entity->think == multi_wait)
		return SG_MECH_EXEC_THINK_MULTI_WAIT;
	if (entity->think == Move_Begin)
		return SG_MECH_EXEC_THINK_LINEAR_BEGIN;
	if (entity->think == Move_Final)
		return SG_MECH_EXEC_THINK_LINEAR_FINAL;
	if (entity->think == Move_Done)
		return SG_MECH_EXEC_THINK_LINEAR_DONE;
	if (entity->think == AngleMove_Begin)
		return SG_MECH_EXEC_THINK_ANGULAR_BEGIN;
	if (entity->think == AngleMove_Final)
		return SG_MECH_EXEC_THINK_ANGULAR_FINAL;
	if (entity->think == AngleMove_Done)
		return SG_MECH_EXEC_THINK_ANGULAR_DONE;
	if (entity->think == Think_AccelMove)
		return SG_MECH_EXEC_THINK_ACCELERATED;
	if (entity->think == door_go_down)
		return SG_MECH_EXEC_THINK_DOOR_RETURN;
	if (entity->think == button_return)
		return SG_MECH_EXEC_THINK_BUTTON_RETURN;
	if (entity->think == plat_go_down)
		return SG_MECH_EXEC_THINK_PLATFORM_RETURN;
	return SG_MECH_EXEC_THINK_UNKNOWN;
}

static uint16_t Catalog_ExecutionEndRole(const edict_t *entity)
{
	if (!entity->moveinfo.endfunc)
		return SG_MECH_EXEC_END_NONE;
	if (entity->moveinfo.endfunc == door_hit_top)
		return SG_MECH_EXEC_END_DOOR_DESTINATION;
	if (entity->moveinfo.endfunc == door_hit_bottom)
		return SG_MECH_EXEC_END_DOOR_ORIGIN;
	if (entity->moveinfo.endfunc == button_wait)
		return SG_MECH_EXEC_END_BUTTON_DESTINATION;
	if (entity->moveinfo.endfunc == button_done)
		return SG_MECH_EXEC_END_BUTTON_ORIGIN;
	if (entity->moveinfo.endfunc == plat_hit_top)
		return SG_MECH_EXEC_END_PLATFORM_DESTINATION;
	if (entity->moveinfo.endfunc == plat_hit_bottom)
		return SG_MECH_EXEC_END_PLATFORM_ORIGIN;
	return SG_MECH_EXEC_END_UNKNOWN;
}

static int Catalog_ExecutionCallbacksMatch(const edict_t *entity,
	const rune_mechanism_node_t *node, uint16_t controller_kind)
{
	sg_mech_execution_state_t state;

	if (!entity || !node || !isfinite(entity->nextthink) ||
	    entity->nextthink < 0.0f)
		return 0;
	memset(&state, 0, sizeof(state));
	state.controller_kind = controller_kind;
	state.node_kind = node->kind;
	state.think_role = Catalog_ExecutionThinkRole(entity, node);
	state.end_role = Catalog_ExecutionEndRole(entity);
	if (node->kind == SG_MECH_NODE_PLATFORM &&
	    node->use_callback == SG_MECH_CALLBACK_USE_PLAT &&
	    node->blocked_callback == SG_MECH_CALLBACK_BLOCKED_PLAT)
		state.platform_profile = SG_MECH_PLATFORM_PROFILE_STOCK;
	else if (node->kind == SG_MECH_NODE_PLATFORM &&
	         node->use_callback == SG_MECH_CALLBACK_USE_DOOR &&
	         node->blocked_callback == SG_MECH_CALLBACK_BLOCKED_DOOR)
		state.platform_profile = SG_MECH_PLATFORM_PROFILE_DOOR_CARRIER;
	state.motion_state = entity->moveinfo.state;
	state.fixed_callbacks_match =
		Catalog_UseCallback(entity) == node->use_callback &&
		Catalog_BlockedCallback(entity) == node->blocked_callback;
	state.touch_matches =
		Catalog_TouchCallback(entity) == node->touch_callback;
	state.touch_cleared = !entity->touch &&
		node->touch_callback != SG_MECH_CALLBACK_NONE;
	state.nextthink_pending = entity->nextthink > 0.0f;
	state.stopped = entity->velocity[0] == 0.0f &&
		entity->velocity[1] == 0.0f && entity->velocity[2] == 0.0f &&
		entity->avelocity[0] == 0.0f &&
		entity->avelocity[1] == 0.0f && entity->avelocity[2] == 0.0f;
	return SG_MechExecutionStateValid(&state);
}

static int32_t Catalog_TimeMS(float seconds)
{
	double milliseconds;

	if (!isfinite(seconds)) return INT32_MAX;
	milliseconds = (double)seconds * 1000.0;
	if (milliseconds > (double)INT32_MAX) return INT32_MAX;
	if (milliseconds < (double)INT32_MIN) return INT32_MIN;
	return (int32_t)lround(milliseconds);
}

/* G_UseTargets treats every positive float delay as asynchronous.  Preserve
 * that semantic boundary even when the nearest integer millisecond is zero;
 * otherwise the wire would misclassify a tiny positive DelayedUse as a
 * synchronous relay. */
static int32_t Catalog_DelayMS(float seconds)
{
	int32_t milliseconds = Catalog_TimeMS(seconds);

	return seconds > 0.0f && milliseconds == 0 ? 1 : milliseconds;
}

static uint32_t Catalog_Q8(float value)
{
	double scaled;

	if (!isfinite(value) || value < 0.0f) return UINT32_MAX;
	scaled = (double)value * 8.0;
	if (scaled > SG_MECH_MAX_Q8) return UINT32_MAX;
	return (uint32_t)lround(scaled);
}

static uint32_t Catalog_MoverQ8(float moveinfo_value, float entity_value)
{
	return Catalog_Q8(moveinfo_value != 0.0f ? moveinfo_value : entity_value);
}

static int Catalog_VectorQ8Exact(const vec3_t value, int16_t fixed[3]);

static int Catalog_FrameCompleteDisplacement(
	const sg_mech_button_endpoints_t *endpoints, float *distance_out,
	uint32_t *witness_q8_out)
{
	int axis;
	int cardinal_axis = -1;
	int32_t cardinal_q8 = 0;
	double distance;
	double velocity;
	uint32_t witness;

	if (!endpoints || !distance_out || !witness_q8_out)
		return 0;
	for (axis = 0; axis < 3; axis++)
	{
		int32_t delta = (int32_t)endpoints->end_q8[axis] -
			(int32_t)endpoints->start_q8[axis];

		if (delta == 0)
			continue;
		if (cardinal_axis >= 0)
			return 0;
		cardinal_axis = axis;
		cardinal_q8 = delta < 0 ? -delta : delta;
	}
	if (cardinal_axis < 0 || cardinal_q8 <= 0)
		return 0;
	distance = (double)cardinal_q8 / 8.0;
	velocity = distance / (double)FRAMETIME;
	if (!isfinite(distance) || !isfinite(velocity) || velocity <= 0.0 ||
	    velocity * 8.0 > (double)SG_MECH_MAX_Q8)
		return 0;
	witness = (uint32_t)lround(velocity * 8.0);
	if (witness == 0U || witness > SG_MECH_MAX_Q8)
		return 0;
	*distance_out = (float)distance;
	*witness_q8_out = witness;
	return 1;
}

static int Catalog_FrameCompleteRawWitness(const edict_t *entity,
	const sg_mech_button_endpoints_t *endpoints, uint32_t *witness_q8_out)
{
	float distance;
	uint32_t witness;

	if (!entity || !Catalog_FrameCompleteDisplacement(endpoints, &distance,
	        &witness) || !isfinite(entity->moveinfo.speed) ||
	    !isfinite(entity->moveinfo.accel) ||
	    !isfinite(entity->moveinfo.decel) || entity->moveinfo.speed <= 0.0f ||
	    entity->moveinfo.accel <= 0.0f || entity->moveinfo.decel <= 0.0f ||
	    entity->moveinfo.speed != entity->moveinfo.accel ||
	    entity->moveinfo.speed != entity->moveinfo.decel ||
	    Catalog_Q8(entity->moveinfo.speed) != UINT32_MAX ||
	    Catalog_Q8(entity->moveinfo.accel) != UINT32_MAX ||
	    Catalog_Q8(entity->moveinfo.decel) != UINT32_MAX ||
	    entity->moveinfo.speed * FRAMETIME < distance)
		return 0;
	*witness_q8_out = witness;
	return 1;
}

static int Catalog_FrameCompleteSealedShape(const edict_t *entity,
	const rune_mechanism_node_t *node)
{
	const uint16_t expected_flags = SG_MECH_NODEF_REPEATABLE |
		SG_MECH_NODEF_USABLE | SG_MECH_NODEF_MOVER |
		SG_MECH_NODEF_SHOOTABLE;

	return entity && node && node->kind == SG_MECH_NODE_BUTTON &&
	       node->flags == expected_flags && entity->classname &&
	       !strcmp(entity->classname, "func_button") &&
	       entity->movetype == MOVETYPE_STOP && entity->solid == SOLID_BSP &&
	       entity->spawnflags == 0 && !entity->touch &&
	       entity->use == button_use && !entity->think && !entity->blocked &&
	       entity->die == button_killed && entity->health > 0 &&
	       entity->max_health == entity->health &&
	       entity->takedamage == DAMAGE_YES &&
	       entity->moveinfo.state == SG_PLAT_STATE_BOTTOM &&
	       !entity->moveinfo.endfunc && entity->nextthink == 0.0f &&
	       entity->velocity[0] == 0.0f && entity->velocity[1] == 0.0f &&
	       entity->velocity[2] == 0.0f && entity->avelocity[0] == 0.0f &&
	       entity->avelocity[1] == 0.0f && entity->avelocity[2] == 0.0f;
}

static int Catalog_FrameCompleteMoverCurrent(const edict_t *entity,
	const rune_mechanism_node_t *node)
{
	const sg_mech_button_motion_t *motion;
	int16_t live_start[3];
	int16_t live_end[3];
	uint32_t witness;
	const uint16_t expected_flags = SG_MECH_NODEF_REPEATABLE |
		SG_MECH_NODEF_USABLE | SG_MECH_NODEF_MOVER |
		SG_MECH_NODEF_SHOOTABLE | SG_MECH_NODEF_FRAME_COMPLETE_MOVER;

	if (!entity || !node || node->key == 0U ||
	    node->key >= catalog.source_capacity || !catalog.button_motion ||
	    node->kind != SG_MECH_NODE_BUTTON || node->flags != expected_flags ||
	    !entity->classname || strcmp(entity->classname, "func_button") ||
	    entity->movetype != MOVETYPE_STOP || entity->solid != SOLID_BSP ||
	    entity->spawnflags != 0 || entity->touch || entity->use != button_use ||
	    entity->blocked || entity->die != button_killed || entity->health <= 0 ||
	    entity->max_health != entity->health ||
	    entity->moveinfo.state < SG_PLAT_STATE_TOP ||
	    entity->moveinfo.state > SG_PLAT_STATE_DOWN ||
	    ((entity->moveinfo.state == SG_PLAT_STATE_BOTTOM ||
	      entity->moveinfo.state == SG_PLAT_STATE_DOWN)
	         ? entity->takedamage != DAMAGE_YES
	         : entity->takedamage != DAMAGE_NO))
		return 0;
	motion = &catalog.button_motion[node->key];
	if (!motion->valid ||
	    !Catalog_VectorQ8Exact(entity->moveinfo.start_origin, live_start) ||
	    !Catalog_VectorQ8Exact(entity->moveinfo.end_origin, live_end) ||
	    memcmp(live_start, motion->endpoints.start_q8,
	        sizeof(live_start)) != 0 ||
	    memcmp(live_end, motion->endpoints.end_q8, sizeof(live_end)) != 0 ||
	    !Catalog_FrameCompleteRawWitness(entity, &motion->endpoints,
	        &witness) || witness != node->speed_q8 ||
	    node->accel_q8 != witness || node->decel_q8 != witness)
		return 0;
	return Catalog_TimeMS(entity->moveinfo.wait) == node->wait_ms;
}

/* Motion values are authority only for an executable mover.  Stock mover
 * spawners finish their defaults by copying speed, accel, decel, and wait to
 * moveinfo, which is also what their runtime movement consumes.  Keep this
 * comparison separate from topology so stock position/callback transitions
 * remain valid while an authenticated controller owns the entity. */
static int Catalog_ExecutableMoverKinematicsCurrent(const edict_t *entity,
	const rune_mechanism_node_t *node)
{
	if (!entity || !node)
		return 0;
	if ((node->flags & (SG_MECH_NODEF_INVENTORY_ONLY |
	     SG_MECH_NODEF_MOVER)) != SG_MECH_NODEF_MOVER)
		return 1;
	if (node->flags & SG_MECH_NODEF_FRAME_COMPLETE_MOVER)
		return Catalog_FrameCompleteMoverCurrent(entity, node);
	return Catalog_Q8(entity->moveinfo.speed) == node->speed_q8 &&
	       Catalog_Q8(entity->moveinfo.accel) == node->accel_q8 &&
	       Catalog_Q8(entity->moveinfo.decel) == node->decel_q8 &&
	       Catalog_TimeMS(entity->moveinfo.wait) == node->wait_ms;
}

static int Catalog_VectorQ8Exact(const vec3_t value, int16_t fixed[3])
{
	const double q8_rounding_tolerance = 1.0e-4;
	int axis;

	if (!value || !fixed)
		return 0;
	for (axis = 0; axis < 3; axis++)
	{
		double scaled = (double)value[axis] * 8.0;
		long rounded;

		if (!isfinite(scaled) || scaled < INT16_MIN || scaled > INT16_MAX)
			return 0;
		rounded = lround(scaled);
		/* Cardinal movedirs are derived through angle trig.  Stock movers can
		 * therefore retain a few millionths of a unit on an orthogonal axis
		 * even though their physical endpoint is exactly on the Q8 grid.  Admit
		 * only noise far below one serialized Q8 unit; material off-grid motion
		 * remains rejected. */
		if (fabs(scaled - (double)rounded) > q8_rounding_tolerance)
			return 0;
		fixed[axis] = (int16_t)rounded;
	}
	return 1;
}

static int Catalog_BoundsQ8(const edict_t *entity, int16_t mins[3],
	int16_t maxs[3])
{
	int axis;
	int linked_bounds = entity->area.prev != NULL;

	for (axis = 0; axis < 3; axis++)
	{
		double low = (double)(linked_bounds ? entity->absmin[axis]
			: entity->s.origin[axis]) * 8.0;
		double high = (double)(linked_bounds ? entity->absmax[axis]
			: entity->s.origin[axis]) * 8.0;
		long low_q8;
		long high_q8;

		if (!isfinite(low) || !isfinite(high)) return 0;
		low_q8 = lround(low);
		high_q8 = lround(high);
		if (low_q8 < INT16_MIN || low_q8 > INT16_MAX ||
		    high_q8 < INT16_MIN || high_q8 > INT16_MAX || low_q8 > high_q8)
			return 0;
		mins[axis] = (int16_t)low_q8;
		maxs[axis] = (int16_t)high_q8;
	}
	return 1;
}

static int Catalog_StringCompare(const void *left, const void *right)
{
	const char *const *a = left;
	const char *const *b = right;
	return strcmp(*a, *b);
}

static uint32_t Catalog_StringOffset(const unsigned char *pool,
	uint32_t bytes, const char *value)
{
	uint32_t offset = 1U;

	if (!value || !value[0]) return 0U;
	while (offset < bytes)
	{
		if (!strcmp((const char *)pool + offset, value)) return offset;
		offset += (uint32_t)strlen((const char *)pool + offset) + 1U;
	}
	return UINT32_MAX;
}

static int Catalog_EdgeCompare(const void *left, const void *right)
{
	const rune_mechanism_edge_t *a = left;
	const rune_mechanism_edge_t *b = right;
	if (a->from_key != b->from_key) return a->from_key < b->from_key ? -1 : 1;
	if (a->kind != b->kind) return a->kind < b->kind ? -1 : 1;
	if (a->ordinal != b->ordinal) return a->ordinal < b->ordinal ? -1 : 1;
	if (a->to_key != b->to_key) return a->to_key < b->to_key ? -1 : 1;
	if (a->delay_ms != b->delay_ms) return a->delay_ms < b->delay_ms ? -1 : 1;
	return 0;
}

static int Catalog_AppendEdge(rune_mechanism_edge_t *edges,
	uint32_t capacity, uint32_t *count, uint32_t from, uint32_t to,
	uint16_t kind, uint32_t ordinal, uint32_t delay_ms)
{
	rune_mechanism_edge_t *edge;

	if (!count || *count >= capacity || ordinal > UINT16_MAX) return 0;
	edge = &edges[*count];
	edge->from_key = from;
	edge->to_key = to;
	edge->kind = kind;
	edge->ordinal = (uint16_t)ordinal;
	edge->delay_ms = delay_ms;
	(*count)++;
	return 1;
}

static uint32_t Catalog_PointerKey(const edict_t *pointer,
	const int *node_by_edict, uint32_t edict_count)
{
	ptrdiff_t index;

	if (!pointer || pointer < g_edicts || pointer >= g_edicts + edict_count)
		return SG_MECH_NO_KEY;
	index = pointer - g_edicts;
	return index > 0 && node_by_edict[index] >= 0
		? (uint32_t)index : SG_MECH_NO_KEY;
}

static int Catalog_SelectEntity(uint32_t index, uint32_t edict_count,
	int *node_by_edict, uint32_t *queue, uint32_t *queue_count)
{
	if (index == 0U || index >= edict_count || !node_by_edict || !queue ||
	    !queue_count || !g_edicts[index].inuse || g_edicts[index].client)
		return 1;
	if (node_by_edict[index] != -1)
		return 1;
	if (*queue_count >= RUNE_MAX_MECHANISM_NODES)
		return 0;
	node_by_edict[index] = -2;
	queue[(*queue_count)++] = index;
	return 1;
}

static int Catalog_SelectPointer(const edict_t *pointer, uint32_t edict_count,
	int *node_by_edict, uint32_t *queue, uint32_t *queue_count)
{
	ptrdiff_t index;

	if (!pointer)
		return 1;
	if (pointer < g_edicts || pointer >= g_edicts + edict_count)
		return 0;
	index = pointer - g_edicts;
	return Catalog_SelectEntity((uint32_t)index, edict_count, node_by_edict,
		queue, queue_count);
}

sg_mech_catalog_status_t SG_MechCatalogSeal(void)
{
	uint32_t edict_count;
	uint32_t index;
	uint32_t node_count = 0U;
	uint32_t edge_capacity = 0U;
	uint32_t edge_count = 0U;
	uint32_t string_count = 0U;
	uint32_t unique_strings = 0U;
	uint32_t string_bytes = 1U;
	uint32_t queue_count = 0U;
	uint32_t queue_head = 0U;
	int *node_by_edict = NULL;
	uint32_t *queue = NULL;
	const char **string_values = NULL;
	size_t bytes;

	if (catalog.status != SG_MECH_CATALOG_BUILDING)
		return catalog.status;
	if (!g_edicts || globals.num_edicts <= 1 ||
	    (uint32_t)globals.num_edicts > catalog.source_capacity)
	{
		catalog.status = SG_MECH_CATALOG_FAILED;
		catalog.reason = "invalid live entity set";
		return catalog.status;
	}
	edict_count = (uint32_t)globals.num_edicts;
	node_by_edict = Catalog_Alloc((size_t)edict_count * sizeof(*node_by_edict));
	queue = Catalog_Alloc((size_t)edict_count * sizeof(*queue));
	if (!node_by_edict || !queue)
	{
		catalog.status = SG_MECH_CATALOG_FAILED;
		catalog.reason = "closure workspace allocation";
		return catalog.status;
	}
	for (index = 0U; index < edict_count; index++) node_by_edict[index] = -1;
	/* Start from every live entity whose stock type is itself a mechanism,
	 * trigger, mover, path node, destination, or objective.  Then traverse its
	 * complete executable relation closure.  Ordinary items, clients, corpses,
	 * projectiles, and unrelated target entities never consume catalog space. */
	for (index = 1U; index < edict_count; index++)
		if (g_edicts[index].inuse && !g_edicts[index].client &&
		    Catalog_NodeKind(index, &g_edicts[index]) != SG_MECH_NODE_CONTEXTUAL &&
		    !Catalog_SelectEntity(index, edict_count, node_by_edict, queue,
		        &queue_count))
		{
			catalog.status = SG_MECH_CATALOG_FAILED;
			catalog.reason = "mechanism closure capacity";
			return catalog.status;
		}
	while (queue_head < queue_count)
	{
		edict_t *entity = &g_edicts[queue[queue_head++]];
		edict_t *member;
		uint32_t destination;

		if (!Catalog_SelectPointer(entity->owner, edict_count, node_by_edict,
		        queue, &queue_count) ||
		    !Catalog_SelectPointer(entity->movetarget, edict_count, node_by_edict,
		        queue, &queue_count) ||
		    !Catalog_SelectPointer(entity->target_ent, edict_count, node_by_edict,
		        queue, &queue_count) ||
		    !Catalog_SelectPointer(entity->enemy, edict_count, node_by_edict,
		        queue, &queue_count) ||
		    !Catalog_SelectPointer(entity->teammaster, edict_count, node_by_edict,
		        queue, &queue_count))
			goto closure_overflow;
		for (member = entity->teamchain; member; member = member->teamchain)
			if (!Catalog_SelectPointer(member, edict_count, node_by_edict,
			        queue, &queue_count))
				goto closure_overflow;
		if (entity->target)
			for (destination = 1U; destination < edict_count; destination++)
				if (g_edicts[destination].inuse &&
				    g_edicts[destination].targetname &&
				    !Q_stricmp(g_edicts[destination].targetname, entity->target) &&
				    !Catalog_SelectEntity(destination, edict_count, node_by_edict,
				        queue, &queue_count))
					goto closure_overflow;
		if (entity->pathtarget)
			for (destination = 1U; destination < edict_count; destination++)
				if (g_edicts[destination].inuse &&
				    g_edicts[destination].targetname &&
				    !Q_stricmp(g_edicts[destination].targetname,
				        entity->pathtarget) &&
				    !Catalog_SelectEntity(destination, edict_count, node_by_edict,
				        queue, &queue_count))
					goto closure_overflow;
		if (entity->killtarget)
			for (destination = 1U; destination < edict_count; destination++)
				if (g_edicts[destination].inuse &&
				    g_edicts[destination].targetname &&
				    !Q_stricmp(g_edicts[destination].targetname,
				        entity->killtarget) &&
				    !Catalog_SelectEntity(destination, edict_count, node_by_edict,
				        queue, &queue_count))
					goto closure_overflow;
	}
	for (index = 1U; index < edict_count; index++)
		if (node_by_edict[index] == -2)
		{
			if (!catalog.live_generations[index])
			{
				catalog.status = SG_MECH_CATALOG_FAILED;
				catalog.reason = "selected entity lacks incarnation";
				return catalog.status;
			}
			node_by_edict[index] = (int)node_count++;
			catalog.sealed_generations[index] =
				catalog.live_generations[index];
		}
	if (node_count == 0U)
	{
		catalog.strings = Catalog_Alloc(1U);
		if (!catalog.strings)
		{
			catalog.status = SG_MECH_CATALOG_FAILED;
			catalog.reason = "empty string pool allocation";
			return catalog.status;
		}
		catalog.strings[0] = 0U;
		catalog.string_bytes = 1U;
		catalog.status = SG_MECH_CATALOG_READY;
		catalog.reason = "ok";
		return catalog.status;
	}
	catalog.nodes = Catalog_Alloc((size_t)node_count * sizeof(*catalog.nodes));
	string_values = Catalog_Alloc((size_t)node_count * 5U * sizeof(*string_values));
	if (!catalog.nodes || !string_values)
	{
		catalog.status = SG_MECH_CATALOG_FAILED;
		catalog.reason = "activation node allocation";
		return catalog.status;
	}
	memset(catalog.nodes, 0, (size_t)node_count * sizeof(*catalog.nodes));
	for (index = 1U; index < edict_count; index++)
	{
		edict_t *entity;
		rune_mechanism_node_t *node;
		const char *values[5];
		uint32_t value_index;
		uint32_t parent_key;

		if (node_by_edict[index] < 0) continue;
		entity = &g_edicts[index];
		node = &catalog.nodes[node_by_edict[index]];
		node->key = index;
		node->kind = Catalog_NodeKind(index, entity);
		node->touch_callback = Catalog_TouchCallback(entity);
		node->use_callback = Catalog_UseCallback(entity);
		node->think_callback = Catalog_ThinkCallback(entity);
		node->blocked_callback = Catalog_BlockedCallback(entity);
		if (node->kind == SG_MECH_NODE_PUSH)
		{
			float scale = entity->speed * 10.0f;
			int axis;

			for (axis = 0; axis < 3; axis++)
				node->push_velocity[axis] = entity->movedir[axis] * scale;
		}
		if (catalog.sources[index].synthetic_kind != SG_MECH_SYNTHETIC_NONE)
			node->flags |= SG_MECH_NODEF_SYNTHETIC;
		if (entity->touch) node->flags |= SG_MECH_NODEF_TOUCHABLE;
		if (entity->use) node->flags |= SG_MECH_NODEF_USABLE;
		if (entity->movetype == MOVETYPE_PUSH || entity->movetype == MOVETYPE_STOP)
			node->flags |= SG_MECH_NODEF_MOVER;
		if (entity->teammaster == entity ||
		    node->kind == SG_MECH_NODE_DOOR_MASTER)
			node->flags |= SG_MECH_NODEF_TEAM_MASTER;
		else if (entity->flags & FL_TEAMSLAVE)
			node->flags |= SG_MECH_NODEF_TEAM_MEMBER;
		if (entity->wait >= 0.0f) node->flags |= SG_MECH_NODEF_REPEATABLE;
		if (entity->takedamage || entity->health || entity->max_health)
			node->flags |= SG_MECH_NODEF_SHOOTABLE;
		if (entity->solid == SOLID_NOT && entity->use == trigger_enable)
			node->flags |= SG_MECH_NODEF_START_DISABLED;
		if ((Catalog_Classname(index, entity) &&
		     !strcmp(Catalog_Classname(index, entity), "trigger_once")) ||
		    entity->wait < 0.0f)
			node->flags |= SG_MECH_NODEF_ONE_SHOT;
		if (node->touch_callback == SG_MECH_CALLBACK_UNKNOWN ||
		    node->use_callback == SG_MECH_CALLBACK_UNKNOWN ||
		    node->think_callback == SG_MECH_CALLBACK_UNKNOWN ||
		    node->blocked_callback == SG_MECH_CALLBACK_UNKNOWN ||
		    node->kind == SG_MECH_NODE_CONTEXTUAL ||
		    node->kind == SG_MECH_NODE_OTHER_TRIGGER ||
		    node->kind == SG_MECH_NODE_OTHER_MOVER ||
		    node->kind == SG_MECH_NODE_OBJECTIVE)
			node->flags |= SG_MECH_NODEF_INVENTORY_ONLY;
		{
			uint32_t lift_trigger;
			uint32_t lift_mover;

			parent_key = Catalog_TriggeredDoorLiftPair(index,
				&lift_trigger, &lift_mover) && index == lift_trigger
				? lift_mover : SG_MECH_NO_KEY;
		}
		if (parent_key == SG_MECH_NO_KEY)
			parent_key = Catalog_PointerKey(
				catalog.sources[index].synthetic_parent,
				node_by_edict, edict_count);
		node->owner_key = parent_key != SG_MECH_NO_KEY ? parent_key
			: Catalog_PointerKey(entity->owner, node_by_edict, edict_count);
		node->team_master_key = Catalog_PointerKey(entity->teammaster,
			node_by_edict, edict_count);
		if (node->kind == SG_MECH_NODE_DOOR_MASTER &&
		    node->team_master_key == SG_MECH_NO_KEY)
			node->team_master_key = index;
		node->spawnflags = (uint32_t)entity->spawnflags;
		node->delay_ms = Catalog_DelayMS(entity->delay);
		/* Executable movers consume moveinfo at runtime.  Other records retain
		 * the existing entity-field inventory representation. */
		node->wait_ms = (node->flags & (SG_MECH_NODEF_INVENTORY_ONLY |
		    SG_MECH_NODEF_MOVER)) == SG_MECH_NODEF_MOVER
			? Catalog_TimeMS(entity->moveinfo.wait)
			: Catalog_TimeMS(entity->wait);
		if (node->kind == SG_MECH_NODE_BUTTON && catalog.button_motion)
		{
			sg_mech_button_motion_t *motion = &catalog.button_motion[index];

			motion->valid = Catalog_VectorQ8Exact(
				entity->moveinfo.start_origin,
				motion->endpoints.start_q8) &&
			    Catalog_VectorQ8Exact(entity->moveinfo.end_origin,
				motion->endpoints.end_q8) &&
			    memcmp(motion->endpoints.start_q8,
				motion->endpoints.end_q8,
				sizeof(motion->endpoints.start_q8)) != 0;
		}
		/* Inventory-only nodes are never activation-plan movers.  Map authors
		 * sometimes use their kinematic fields for unrelated data (notably a
		 * func_explosive respawn delay), so serialize no motion authority for
		 * them rather than rejecting otherwise representable inventory. */
		if (node->flags & SG_MECH_NODEF_INVENTORY_ONLY)
			node->speed_q8 = node->accel_q8 = node->decel_q8 = 0U;
		else if (node->flags & SG_MECH_NODEF_MOVER)
		{
			uint32_t frame_witness = 0U;
			sg_mech_button_motion_t *motion =
				catalog.button_motion ? &catalog.button_motion[index] : NULL;

			if (motion && motion->valid &&
			    Catalog_FrameCompleteSealedShape(entity, node) &&
			    Catalog_FrameCompleteRawWitness(entity, &motion->endpoints,
			        &frame_witness))
			{
				node->flags |= SG_MECH_NODEF_FRAME_COMPLETE_MOVER;
				node->speed_q8 = node->accel_q8 = node->decel_q8 =
					frame_witness;
			}
			else
			{
				node->speed_q8 = Catalog_Q8(entity->moveinfo.speed);
				node->accel_q8 = Catalog_Q8(entity->moveinfo.accel);
				node->decel_q8 = Catalog_Q8(entity->moveinfo.decel);
			}
		}
		else
		{
			node->speed_q8 = Catalog_MoverQ8(entity->moveinfo.speed,
				entity->speed);
			node->accel_q8 = Catalog_MoverQ8(entity->moveinfo.accel,
				entity->accel);
			node->decel_q8 = Catalog_MoverQ8(entity->moveinfo.decel,
				entity->decel);
		}
		if (node->delay_ms == INT32_MAX || node->delay_ms == INT32_MIN ||
		    node->wait_ms == INT32_MAX || node->wait_ms == INT32_MIN ||
		    node->speed_q8 == UINT32_MAX || node->accel_q8 == UINT32_MAX ||
		    node->decel_q8 == UINT32_MAX ||
		    !isfinite(node->push_velocity[0]) ||
		    !isfinite(node->push_velocity[1]) ||
		    !isfinite(node->push_velocity[2]) ||
		    !Catalog_BoundsQ8(entity, node->absmin_q8, node->absmax_q8))
		{
			catalog.status = SG_MECH_CATALOG_FAILED;
			catalog.reason = "unrepresentable live mechanism field";
			return catalog.status;
		}
		values[0] = Catalog_Classname(index, entity);
		values[1] = entity->target;
		values[2] = entity->targetname;
		values[3] = entity->killtarget;
		values[4] = entity->pathtarget;
		for (value_index = 0U; value_index < 5U; value_index++)
			if (values[value_index] && values[value_index][0])
				string_values[string_count++] = values[value_index];
	}
	qsort(string_values, string_count, sizeof(*string_values),
		Catalog_StringCompare);
	for (index = 0U; index < string_count; index++)
		if (index == 0U || strcmp(string_values[index - 1U], string_values[index]))
		{
			size_t length = strlen(string_values[index]) + 1U;
			if (length > UINT32_MAX - string_bytes ||
			    string_bytes + (uint32_t)length > RUNE_MAX_MECHANISM_STRING_BYTES)
			{
				catalog.status = SG_MECH_CATALOG_FAILED;
				catalog.reason = "string pool capacity";
				return catalog.status;
			}
			string_values[unique_strings++] = string_values[index];
			string_bytes += (uint32_t)length;
		}
	catalog.strings = Catalog_Alloc(string_bytes);
	if (!catalog.strings)
	{
		catalog.status = SG_MECH_CATALOG_FAILED;
		catalog.reason = "string pool allocation";
		return catalog.status;
	}
	catalog.strings[0] = 0U;
	bytes = 1U;
	for (index = 0U; index < unique_strings; index++)
	{
		size_t length = strlen(string_values[index]) + 1U;
		memcpy(catalog.strings + bytes, string_values[index], length);
		bytes += length;
	}
	catalog.string_bytes = string_bytes;
	for (index = 1U; index < edict_count; index++)
	{
		edict_t *entity;
		rune_mechanism_node_t *node;
		if (node_by_edict[index] < 0) continue;
		entity = &g_edicts[index];
		node = &catalog.nodes[node_by_edict[index]];
		node->classname_offset = Catalog_StringOffset(catalog.strings,
			string_bytes, Catalog_Classname(index, entity));
		node->target_offset = Catalog_StringOffset(catalog.strings,
			string_bytes, entity->target);
		node->targetname_offset = Catalog_StringOffset(catalog.strings,
			string_bytes, entity->targetname);
		node->killtarget_offset = Catalog_StringOffset(catalog.strings,
			string_bytes, entity->killtarget);
		node->path_target_offset = Catalog_StringOffset(catalog.strings,
			string_bytes, entity->pathtarget);
		if (node->classname_offset == UINT32_MAX ||
		    node->target_offset == UINT32_MAX ||
		    node->targetname_offset == UINT32_MAX ||
		    node->killtarget_offset == UINT32_MAX ||
		    node->path_target_offset == UINT32_MAX)
		{
			catalog.status = SG_MECH_CATALOG_FAILED;
			catalog.reason = "string offset lookup";
			return catalog.status;
		}
	}
	/* Count an intentionally conservative upper bound, then fill exactly. */
	if (node_count > 0U && node_count > UINT32_MAX / node_count)
	{
		catalog.status = SG_MECH_CATALOG_FAILED;
		catalog.reason = "edge capacity overflow";
		return catalog.status;
	}
	edge_capacity = node_count * node_count;
	if (edge_capacity > RUNE_MAX_MECHANISM_EDGES)
		edge_capacity = RUNE_MAX_MECHANISM_EDGES;
	if (edge_capacity < node_count * 5U) edge_capacity = node_count * 5U;
	if (edge_capacity > RUNE_MAX_MECHANISM_EDGES)
		edge_capacity = RUNE_MAX_MECHANISM_EDGES;
	catalog.edges = Catalog_Alloc((size_t)edge_capacity * sizeof(*catalog.edges));
	if (!catalog.edges)
	{
		catalog.status = SG_MECH_CATALOG_FAILED;
		catalog.reason = "activation edge allocation";
		return catalog.status;
	}
	for (index = 1U; index < edict_count; index++)
	{
		edict_t *entity;
		uint16_t target_kind;
		uint32_t destination;
		uint32_t ordinal;
		uint32_t delay_ms;
		if (node_by_edict[index] < 0) continue;
		entity = &g_edicts[index];
		target_kind = (catalog.nodes[node_by_edict[index]].kind == SG_MECH_NODE_TRAIN ||
			catalog.nodes[node_by_edict[index]].kind == SG_MECH_NODE_PATH_CORNER)
			? SG_MECH_EDGE_ROUTE_TARGET : SG_MECH_EDGE_TARGET;
		delay_ms = entity->delay > 0.0f
		    ? (uint32_t)Catalog_DelayMS(entity->delay) : 0U;
		ordinal = 0U;
		if (entity->target)
			for (destination = 1U; destination < edict_count; destination++)
				if (node_by_edict[destination] >= 0 &&
				    g_edicts[destination].targetname &&
				    !Q_stricmp(g_edicts[destination].targetname, entity->target) &&
				    !Catalog_AppendEdge(catalog.edges, edge_capacity, &edge_count,
				        index, destination, target_kind, ordinal++, delay_ms))
					goto edge_overflow;
		ordinal = 0U;
		if (entity->pathtarget)
			for (destination = 1U; destination < edict_count; destination++)
				if (node_by_edict[destination] >= 0 &&
				    g_edicts[destination].targetname &&
				    !Q_stricmp(g_edicts[destination].targetname,
				        entity->pathtarget) &&
				    !Catalog_AppendEdge(catalog.edges, edge_capacity, &edge_count,
				        index, destination, SG_MECH_EDGE_PATH_TARGET,
				        ordinal++, delay_ms))
					goto edge_overflow;
		ordinal = 0U;
		if (entity->killtarget)
			for (destination = 1U; destination < edict_count; destination++)
				if (node_by_edict[destination] >= 0 &&
				    g_edicts[destination].targetname &&
				    !Q_stricmp(g_edicts[destination].targetname, entity->killtarget) &&
				    !Catalog_AppendEdge(catalog.edges, edge_capacity, &edge_count,
				        index, destination, SG_MECH_EDGE_KILLTARGET,
				        ordinal++, delay_ms))
					goto edge_overflow;
		if (Catalog_PointerKey(entity->owner, node_by_edict, edict_count) != SG_MECH_NO_KEY &&
		    !Catalog_AppendEdge(catalog.edges, edge_capacity, &edge_count, index,
		        Catalog_PointerKey(entity->owner, node_by_edict, edict_count),
		        SG_MECH_EDGE_OWNER, 0U, 0U)) goto edge_overflow;
		if (Catalog_PointerKey(entity->movetarget, node_by_edict, edict_count) != SG_MECH_NO_KEY &&
		    !Catalog_AppendEdge(catalog.edges, edge_capacity, &edge_count, index,
		        Catalog_PointerKey(entity->movetarget, node_by_edict, edict_count),
		        SG_MECH_EDGE_MOVE_TARGET, 0U, 0U)) goto edge_overflow;
		if (Catalog_PointerKey(entity->target_ent, node_by_edict, edict_count) != SG_MECH_NO_KEY &&
		    !Catalog_AppendEdge(catalog.edges, edge_capacity, &edge_count, index,
		        Catalog_PointerKey(entity->target_ent, node_by_edict, edict_count),
		        SG_MECH_EDGE_TARGET_ENT, 0U, 0U)) goto edge_overflow;
		if (Catalog_PointerKey(entity->enemy, node_by_edict, edict_count) != SG_MECH_NO_KEY &&
		    !Catalog_AppendEdge(catalog.edges, edge_capacity, &edge_count, index,
		        Catalog_PointerKey(entity->enemy, node_by_edict, edict_count),
		        SG_MECH_EDGE_ENEMY, 0U, 0U)) goto edge_overflow;
		if (entity->teammaster == entity)
		{
			edict_t *member;
			ordinal = 0U;
			for (member = entity->teamchain; member; member = member->teamchain)
			{
				uint32_t member_key = Catalog_PointerKey(member,
					node_by_edict, edict_count);
				if (member_key != SG_MECH_NO_KEY &&
				    !Catalog_AppendEdge(catalog.edges, edge_capacity, &edge_count,
				        index, member_key, SG_MECH_EDGE_TEAM,
				        ordinal++, 0U)) goto edge_overflow;
			}
		}
		if (catalog.sources[index].synthetic_parent &&
		    Catalog_PointerKey(catalog.sources[index].synthetic_parent,
		        node_by_edict, edict_count) != SG_MECH_NO_KEY &&
		    entity->owner != catalog.sources[index].synthetic_parent &&
		    !Catalog_AppendEdge(catalog.edges, edge_capacity, &edge_count, index,
			    Catalog_PointerKey(catalog.sources[index].synthetic_parent,
			        node_by_edict, edict_count), SG_MECH_EDGE_OWNER, 0U, 0U))
			goto edge_overflow;
		{
			uint32_t lift_trigger;
			uint32_t lift_mover;

			if (Catalog_TriggeredDoorLiftPair(index, &lift_trigger,
			        &lift_mover) && index == lift_trigger &&
			    !Catalog_AppendEdge(catalog.edges, edge_capacity, &edge_count,
			        index, lift_mover, SG_MECH_EDGE_OWNER, 0U, 0U))
				goto edge_overflow;
		}
	}
	qsort(catalog.edges, edge_count, sizeof(*catalog.edges), Catalog_EdgeCompare);
	for (index = 1U; index < edge_count; index++)
		if (Catalog_EdgeCompare(&catalog.edges[index - 1U],
		    &catalog.edges[index]) == 0)
		{
			catalog.status = SG_MECH_CATALOG_FAILED;
			catalog.reason = "duplicate inventory edge";
			return catalog.status;
		}
	catalog.num_nodes = node_count;
	catalog.num_edges = edge_count;
	catalog.status = SG_MECH_CATALOG_READY;
	catalog.reason = "ok";
	return catalog.status;

closure_overflow:
	catalog.status = SG_MECH_CATALOG_FAILED;
	catalog.reason = "mechanism closure capacity or invalid pointer";
	return catalog.status;

edge_overflow:
	catalog.status = SG_MECH_CATALOG_FAILED;
	catalog.reason = "activation edge capacity or fanout";
	return catalog.status;
}

sg_mech_catalog_status_t SG_MechCatalogSnapshot(
	sg_mech_catalog_view_t *view_out)
{
	if (view_out) memset(view_out, 0, sizeof(*view_out));
	if (!view_out || catalog.status != SG_MECH_CATALOG_READY)
		return catalog.status;
	view_out->nodes = catalog.nodes;
	view_out->num_nodes = catalog.num_nodes;
	view_out->edges = catalog.edges;
	view_out->num_edges = catalog.num_edges;
	view_out->strings = catalog.strings;
	view_out->string_bytes = catalog.string_bytes;
	return catalog.status;
}

const char *SG_MechCatalogReason(void)
{
	return catalog.reason ? catalog.reason : "not initialized";
}

static const rune_mechanism_node_t *Catalog_SealedNode(uint32_t key)
{
	uint32_t low = 0U;
	uint32_t high = catalog.num_nodes;

	while (low < high)
	{
		uint32_t middle = low + (high - low) / 2U;

		if (catalog.nodes[middle].key < key)
			low = middle + 1U;
		else
			high = middle;
	}
	return low < catalog.num_nodes && catalog.nodes[low].key == key
		? &catalog.nodes[low] : NULL;
}

static const char *Catalog_SealedString(uint32_t offset)
{
	if (!catalog.strings || offset >= catalog.string_bytes)
		return NULL;
	return (const char *)catalog.strings + offset;
}

static int Catalog_StringMatches(uint32_t offset, const char *live)
{
	const char *sealed = Catalog_SealedString(offset);

	if (!sealed)
		return 0;
	if (offset == 0U)
		return !live || !live[0];
	return live && strcmp(sealed, live) == 0;
}

static uint32_t Catalog_LivePointerKey(const edict_t *entity)
{
	ptrdiff_t index;

	if (!entity)
		return SG_MECH_NO_KEY;
	if (!g_edicts || entity < g_edicts ||
	    entity >= g_edicts + catalog.source_capacity)
		return 0U;
	index = entity - g_edicts;
	return index > 0 ? (uint32_t)index : 0U;
}

static int Catalog_EdgeGroupMatchesPointer(uint32_t from_key,
	uint16_t kind, uint32_t to_key)
{
	uint32_t i;
	uint32_t count = 0U;
	uint32_t sealed_to = SG_MECH_NO_KEY;

	for (i = 0U; i < catalog.num_edges; i++)
		if (catalog.edges[i].from_key == from_key &&
		    catalog.edges[i].kind == kind)
		{
			if (count != 0U || catalog.edges[i].ordinal != 0U)
				return 0;
			sealed_to = catalog.edges[i].to_key;
			count++;
		}
	if (to_key == SG_MECH_NO_KEY)
		return count == 0U;
	return count == 1U && sealed_to == to_key;
}

static int Catalog_EdgeGroupMatchesName(uint32_t from_key, uint16_t kind,
	const char *name, uint32_t delay_ms)
{
	uint32_t destination;
	uint32_t ordinal = 0U;
	uint32_t edge_index = 0U;

	while (edge_index < catalog.num_edges &&
	       (catalog.edges[edge_index].from_key < from_key ||
	        (catalog.edges[edge_index].from_key == from_key &&
	         catalog.edges[edge_index].kind < kind)))
		edge_index++;
	if (name && name[0])
		for (destination = 1U;
		     destination < (uint32_t)globals.num_edicts; destination++)
			if (g_edicts[destination].inuse &&
			    g_edicts[destination].targetname &&
			    !Q_stricmp(g_edicts[destination].targetname, name))
			{
				if (edge_index >= catalog.num_edges ||
				    catalog.edges[edge_index].from_key != from_key ||
				    catalog.edges[edge_index].kind != kind ||
				    catalog.edges[edge_index].ordinal != ordinal ||
				    catalog.edges[edge_index].to_key != destination ||
				    catalog.edges[edge_index].delay_ms != delay_ms)
					return 0;
				edge_index++;
				ordinal++;
			}
	return edge_index >= catalog.num_edges ||
	       catalog.edges[edge_index].from_key != from_key ||
	       catalog.edges[edge_index].kind != kind;
}

static int Catalog_EdgeGroupMatchesTeam(uint32_t from_key,
	const edict_t *entity)
{
	const edict_t *member;
	uint32_t edge_index = 0U;
	uint32_t ordinal = 0U;

	while (edge_index < catalog.num_edges &&
	       (catalog.edges[edge_index].from_key < from_key ||
	        (catalog.edges[edge_index].from_key == from_key &&
	         catalog.edges[edge_index].kind < SG_MECH_EDGE_TEAM)))
		edge_index++;
	if (entity->teammaster == entity)
		for (member = entity->teamchain; member; member = member->teamchain)
		{
			uint32_t member_key = Catalog_LivePointerKey(member);

			if (member_key == 0U || member_key == SG_MECH_NO_KEY ||
			    edge_index >= catalog.num_edges ||
			    catalog.edges[edge_index].from_key != from_key ||
			    catalog.edges[edge_index].kind != SG_MECH_EDGE_TEAM ||
			    catalog.edges[edge_index].ordinal != ordinal ||
			    catalog.edges[edge_index].to_key != member_key ||
			    catalog.edges[edge_index].delay_ms != 0U)
				return 0;
			edge_index++;
			ordinal++;
		}
	return edge_index >= catalog.num_edges ||
	       catalog.edges[edge_index].from_key != from_key ||
	       catalog.edges[edge_index].kind != SG_MECH_EDGE_TEAM;
}

int SG_MechCatalogMatches(const rune_mechanism_node_t *nodes,
	uint32_t num_nodes, const rune_mechanism_edge_t *inventory_edges,
	uint32_t num_inventory_edges, const unsigned char *strings,
	uint32_t string_bytes)
{
	uint32_t i;

	if (catalog.status != SG_MECH_CATALOG_READY ||
	    num_nodes != catalog.num_nodes ||
	    num_inventory_edges != catalog.num_edges ||
	    string_bytes != catalog.string_bytes ||
	    (num_nodes != 0U && !nodes) ||
	    (num_inventory_edges != 0U && !inventory_edges) || !strings)
		return 0;
	if (!((num_nodes == 0U || memcmp(nodes, catalog.nodes,
		(size_t)num_nodes * sizeof(*nodes)) == 0) &&
	       (num_inventory_edges == 0U || memcmp(inventory_edges,
		catalog.edges, (size_t)num_inventory_edges *
		sizeof(*inventory_edges)) == 0) &&
	       memcmp(strings, catalog.strings, string_bytes) == 0))
		return 0;
	for (i = 0U; i < num_nodes; i++)
		/* The sealed inventory is a map-spawn fact, not a promise that every
		 * contextual node lives forever.  Stock maps can intentionally retire a
		 * START_ON func_timer through an authenticated delayed killtarget while a
		 * long RUNE generation is still running.  Admit only that exact, empty
		 * retired incarnation here.  Any slot reuse still has a nonzero, different
		 * live generation and fails; every activation-plan capture below this
		 * global inventory gate continues to require live topology for each entry,
		 * mover, and closure node it actually executes. */
		if (!SG_MechCatalogEntityMatches(nodes[i].key, &nodes[i]) &&
		    !SG_MechCatalogEntityRetired(nodes[i].key, &nodes[i]))
			return 0;
	return 1;
}

int SG_MechCatalogEntityMatches(uint32_t key,
	const rune_mechanism_node_t *node)
{
	const rune_mechanism_node_t *sealed;

	sealed = Catalog_SealedNode(key);
	if (catalog.status != SG_MECH_CATALOG_READY || !node ||
	    node->key != key || key == 0U || key >= catalog.source_capacity ||
	    !sealed || memcmp(node, sealed, sizeof(*node)) != 0 ||
	    !catalog.live_generations || !catalog.sealed_generations ||
	    catalog.sealed_generations[key] == 0U ||
	    catalog.live_generations[key] != catalog.sealed_generations[key])
		return 0;
	return g_edicts && g_edicts[key].inuse && !g_edicts[key].client;
}

static int Catalog_EntityTopologyMatches(uint32_t key,
	const rune_mechanism_node_t *node, int execution,
	uint16_t controller_kind)
{
	edict_t *entity;
	uint32_t delay_ms;
	uint32_t owner_key;
	uint32_t team_master_key;
	uint16_t target_kind;

	if (!SG_MechCatalogEntityMatches(key, node))
		return 0;
	entity = &g_edicts[key];
	delay_ms = entity->delay > 0.0f
		? (uint32_t)Catalog_DelayMS(entity->delay) : 0U;
	owner_key = Catalog_LivePointerKey(entity->owner);
	if (catalog.sources[key].synthetic_parent)
		owner_key = Catalog_LivePointerKey(
			catalog.sources[key].synthetic_parent);
	{
		uint32_t lift_trigger;
		uint32_t lift_mover;

		if (Catalog_TriggeredDoorLiftPair(key, &lift_trigger, &lift_mover) &&
		    key == lift_trigger)
			owner_key = lift_mover;
	}
	team_master_key = Catalog_LivePointerKey(entity->teammaster);
	if (node->kind == SG_MECH_NODE_DOOR_MASTER &&
	    team_master_key == SG_MECH_NO_KEY)
		team_master_key = key;
	target_kind = node->kind == SG_MECH_NODE_TRAIN ||
		node->kind == SG_MECH_NODE_PATH_CORNER
		? SG_MECH_EDGE_ROUTE_TARGET : SG_MECH_EDGE_TARGET;
	if (Catalog_NodeKind(key, entity) != node->kind ||
	    (execution
	        ? !Catalog_ExecutionCallbacksMatch(entity, node,
	              controller_kind)
	        : (Catalog_TouchCallback(entity) != node->touch_callback ||
	           Catalog_UseCallback(entity) != node->use_callback ||
	           Catalog_ThinkCallback(entity) != node->think_callback ||
	           Catalog_BlockedCallback(entity) != node->blocked_callback)) ||
	    (execution && !Catalog_ExecutableMoverKinematicsCurrent(entity,
	        node)) ||
	    (uint32_t)entity->spawnflags != node->spawnflags ||
	    owner_key != node->owner_key ||
	    team_master_key != node->team_master_key ||
	    !Catalog_StringMatches(node->classname_offset, entity->classname) ||
	    !Catalog_StringMatches(node->target_offset, entity->target) ||
	    !Catalog_StringMatches(node->targetname_offset, entity->targetname) ||
	    !Catalog_StringMatches(node->killtarget_offset, entity->killtarget) ||
	    !Catalog_StringMatches(node->path_target_offset, entity->pathtarget) ||
	    !Catalog_EdgeGroupMatchesName(key, target_kind, entity->target,
	        delay_ms) ||
	    !Catalog_EdgeGroupMatchesName(key, SG_MECH_EDGE_KILLTARGET,
	        entity->killtarget, delay_ms) ||
	    !Catalog_EdgeGroupMatchesName(key, SG_MECH_EDGE_PATH_TARGET,
	        entity->pathtarget, delay_ms) ||
	    !Catalog_EdgeGroupMatchesPointer(key, SG_MECH_EDGE_OWNER,
	        owner_key) ||
	    !Catalog_EdgeGroupMatchesPointer(key, SG_MECH_EDGE_MOVE_TARGET,
	        Catalog_LivePointerKey(entity->movetarget)) ||
	    !Catalog_EdgeGroupMatchesPointer(key, SG_MECH_EDGE_TARGET_ENT,
	        Catalog_LivePointerKey(entity->target_ent)) ||
	    !Catalog_EdgeGroupMatchesPointer(key, SG_MECH_EDGE_ENEMY,
	        Catalog_LivePointerKey(entity->enemy)) ||
	    !Catalog_EdgeGroupMatchesTeam(key, entity))
		return 0;
	if (node->kind == SG_MECH_NODE_PUSH)
	{
		float current[3];
		float scale = entity->speed * 10.0f;
		int axis;

		for (axis = 0; axis < 3; axis++)
			current[axis] = entity->movedir[axis] * scale;
		if (!isfinite(current[0]) || !isfinite(current[1]) ||
		    !isfinite(current[2]) ||
		    memcmp(current, node->push_velocity, sizeof(current)) != 0)
			return 0;
	}
	return 1;
}

int SG_MechCatalogEntityTopologyMatches(uint32_t key,
	const rune_mechanism_node_t *node)
{
	return Catalog_EntityTopologyMatches(key, node, 0,
		SG_MECHANISM_CONTROLLER_NONE);
}

int SG_MechCatalogEntityExecutionMatches(uint32_t key,
	const rune_mechanism_node_t *node, uint16_t controller_kind)
{
	if (controller_kind == SG_MECHANISM_CONTROLLER_NONE ||
	    controller_kind > SG_MECHANISM_CONTROLLER_PUSH || !node ||
	    (node->flags & SG_MECH_NODEF_INVENTORY_ONLY) != 0U)
		return 0;
	return Catalog_EntityTopologyMatches(key, node, 1, controller_kind);
}

int SG_MechCatalogEntityRetired(uint32_t key,
	const rune_mechanism_node_t *node)
{
	const rune_mechanism_node_t *sealed = Catalog_SealedNode(key);

	return catalog.status == SG_MECH_CATALOG_READY && node && sealed &&
	       key != 0U && key < catalog.source_capacity && node->key == key &&
	       memcmp(node, sealed, sizeof(*node)) == 0 &&
	       catalog.sealed_generations && catalog.live_generations &&
	       catalog.sealed_generations[key] != 0U &&
	       catalog.live_generations[key] == 0U && g_edicts &&
	       !g_edicts[key].inuse;
}

int SG_MechCatalogEntityGeneration(const edict_t *entity,
	uint32_t *key_out, uint32_t *generation_out)
{
	uint32_t key;
	uint32_t generation;

	if (key_out)
		*key_out = 0U;
	if (generation_out)
		*generation_out = 0U;
	if (!key_out || !generation_out || key_out == generation_out ||
	    catalog.status != SG_MECH_CATALOG_READY ||
	    !catalog.live_generations ||
	    !Catalog_EntityIndex(entity, &key) || !g_edicts ||
	    entity != &g_edicts[key] || !entity->inuse ||
	    entity->s.number != (int)key || globals.num_edicts <= 0 ||
	    key >= (uint32_t)globals.num_edicts)
		return 0;
	generation = catalog.live_generations[key];
	if (generation == 0U)
		return 0;
	*key_out = key;
	*generation_out = generation;
	return 1;
}

edict_t *SG_MechCatalogResolveEntity(uint32_t key,
	const rune_mechanism_node_t *node)
{
	return SG_MechCatalogEntityMatches(key, node) ? &g_edicts[key] : NULL;
}

int SG_MechCatalogButtonEndpoints(uint32_t key,
	const rune_mechanism_node_t *node, const edict_t *entity,
	sg_mech_button_endpoints_t *endpoints_out)
{
	const sg_mech_button_motion_t *motion;
	int16_t live_start[3];
	int16_t live_end[3];

	if (endpoints_out)
		memset(endpoints_out, 0, sizeof(*endpoints_out));
	if (!endpoints_out || key == 0U ||
	    key >= catalog.source_capacity || !catalog.button_motion ||
	    entity != &g_edicts[key])
		return 0;
	if (!node)
		node = Catalog_SealedNode(key);
	if (!node || node->key != key || node->kind != SG_MECH_NODE_BUTTON ||
	    (node->flags & SG_MECH_NODEF_INVENTORY_ONLY) != 0U ||
	    !SG_MechCatalogEntityMatches(key, node))
		return 0;
	motion = &catalog.button_motion[key];
	if (!motion->valid ||
	    !Catalog_VectorQ8Exact(entity->moveinfo.start_origin, live_start) ||
	    !Catalog_VectorQ8Exact(entity->moveinfo.end_origin, live_end) ||
	    memcmp(live_start, motion->endpoints.start_q8,
	        sizeof(live_start)) != 0 ||
	    memcmp(live_end, motion->endpoints.end_q8, sizeof(live_end)) != 0)
		return 0;
	*endpoints_out = motion->endpoints;
	return 1;
}

int SG_MechCatalogButtonBottomEndpoints(uint32_t key,
	const rune_mechanism_node_t *node, const edict_t *entity,
	sg_mech_button_endpoints_t *endpoints_out)
{
	int16_t live_origin[3];
	int16_t live_mins[3];
	int16_t live_maxs[3];

	if (!SG_MechCatalogButtonEndpoints(key, node, entity, endpoints_out))
		return 0;
	if (!node)
		node = Catalog_SealedNode(key);
	return node && entity->moveinfo.state == SG_PLAT_STATE_BOTTOM &&
	       Catalog_VectorQ8Exact(entity->s.origin, live_origin) &&
	       memcmp(live_origin, endpoints_out->start_q8,
	           sizeof(live_origin)) == 0 &&
	       Catalog_BoundsQ8(entity, live_mins, live_maxs) &&
	       memcmp(live_mins, node->absmin_q8, sizeof(live_mins)) == 0 &&
	       memcmp(live_maxs, node->absmax_q8, sizeof(live_maxs)) == 0 &&
	       Catalog_ExecutableMoverKinematicsCurrent(entity, node);
}
