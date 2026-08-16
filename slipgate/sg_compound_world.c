/* sg_compound_world.c -- strict world adapter for PREOPEN compound replay. */
#include "../g_local.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "sg_compound_world.h"
#include "sg_util.h"

void Touch_DoorTrigger(edict_t *self, edict_t *other, cplane_t *plane,
	csurface_t *surf);
void Use_Target_Speaker(edict_t *self, edict_t *other,
	edict_t *activator);
void door_blocked(edict_t *self, edict_t *other);
void door_use(edict_t *self, edict_t *other, edict_t *activator);
void trigger_relay_use(edict_t *self, edict_t *other,
	edict_t *activator);

#define SG_COMPOUND_DOOR_START_OPEN 1
#define SG_COMPOUND_DOOR_CRUSHER 4
#define SG_COMPOUND_DOOR_TOGGLE 32

static int CompoundWorldStringPresent(const char *value)
{
	return value && value[0];
}

/* Keep this copy independent of sg_oracle.c: compound admission must not
 * widen when ordinary declared-door rollout policy changes. */
static int CompoundWorldSoundOnlyTargets(edict_t *source, int depth)
{
	edict_t *target = NULL;
	int found = 0;

	/* G_UseTargets branches on pointer presence, not string contents.  An
	 * empty-but-present killtarget can still match and delete entities whose
	 * targetname is empty, so only a genuinely absent pointer is safe. */
	if (!source || depth > 4 || source->killtarget ||
	    !CompoundWorldStringPresent(source->target))
		return 0;
	while ((target = G_Find(target, (int)offsetof(edict_t, targetname),
	                        source->target)) != NULL)
	{
		if (!target->inuse || !target->classname)
			return 0;
		found = 1;
		if (!Q_stricmp(target->classname, "target_speaker") &&
		    target->use == Use_Target_Speaker)
			continue;
		if (!Q_stricmp(target->classname, "trigger_relay") &&
		    target->use == trigger_relay_use &&
		    CompoundWorldSoundOnlyTargets(target, depth + 1))
			continue;
		return 0;
	}
	return found;
}

static int CompoundWorldOrdinaryEffectsSafe(edict_t *door)
{
	edict_t *target = NULL;
	int found = 0;

	if (!door || door->killtarget || door->delay != 0.0f)
		return 0;
	if (!door->target)
		return 1;
	/* An empty string is not absence: G_UseTargets will search for the exact
	 * empty targetname and invoke every matching entity. */
	if (!door->target[0])
		return 0;
	while ((target = G_Find(target, (int)offsetof(edict_t, targetname),
	                        door->target)) != NULL)
	{
		if (!target->inuse || !target->classname)
			return 0;
		found = 1;
		/* G_UseTargets skips a door's func_areaportal; door_use updates it
		 * through door_use_areaportals instead. */
		if (!Q_stricmp(target->classname, "func_areaportal"))
			continue;
		if (!Q_stricmp(target->classname, "target_speaker") &&
		    target->use == Use_Target_Speaker)
			continue;
		if (!Q_stricmp(target->classname, "trigger_relay") &&
		    target->use == trigger_relay_use &&
		    CompoundWorldSoundOnlyTargets(target, 1))
			continue;
		return 0;
	}
	return found;
}

int SG_CompoundWorldDoorEffectsSafe(const struct edict_s *opaque_door)
{
	edict_t *door = (edict_t *)opaque_door;

	if (!door || !isfinite(door->delay))
		return 0;
	if (door->delay == 0.0f)
		return CompoundWorldOrdinaryEffectsSafe(door);
	/* G_UseTargets creates DelayedUse before inspecting these fields.  The
	 * temporary entity has no target/message/killtarget effect only when all
	 * three pointers are absent; an empty-but-present string is not treated as
	 * proof of absence. */
	return door->delay > 0.0f &&
	       door->delay <= SG_COMPOUND_WORLD_MAX_INERT_DELAY_SECONDS &&
	       !door->target && !door->message && !door->killtarget;
}

static int CompoundWorldFinite3(const float value[3])
{
	return value && isfinite(value[0]) && isfinite(value[1]) &&
	       isfinite(value[2]);
}

static int CompoundWorldVectorEqual(const float first[3],
	const float second[3])
{
	return first && second && first[0] == second[0] &&
	       first[1] == second[1] && first[2] == second[2];
}

static int CompoundWorldVectorZero(const float value[3])
{
	return value && value[0] == 0.0f && value[1] == 0.0f &&
	       value[2] == 0.0f;
}

static int CompoundWorldEntityIndex(const edict_t *entity)
{
	int index;

	if (!entity || !g_edicts || globals.num_edicts <= 0)
		return -1;
	for (index = 0; index < globals.num_edicts; index++)
		if (&g_edicts[index] == entity)
			return index;
	return -1;
}

static int CompoundWorldTriggerContains(const edict_t *trigger,
	const float origin[3])
{
	float mins[3], maxs[3];
	int axis;

	if (!trigger || !origin || !CompoundWorldFinite3(trigger->absmin) ||
	    !CompoundWorldFinite3(trigger->absmax))
		return 0;
	for (axis = 0; axis < 3; axis++)
		if (trigger->absmin[axis] > trigger->absmax[axis])
			return 0;
	mins[0] = origin[0] - 17.0f;
	mins[1] = origin[1] - 17.0f;
	mins[2] = origin[2] - 25.0f;
	maxs[0] = origin[0] + 17.0f;
	maxs[1] = origin[1] + 17.0f;
	maxs[2] = origin[2] + 33.0f;
	return maxs[0] > trigger->absmin[0] &&
	       mins[0] < trigger->absmax[0] &&
	       maxs[1] > trigger->absmin[1] &&
	       mins[1] < trigger->absmax[1] &&
	       maxs[2] > trigger->absmin[2] &&
	       mins[2] < trigger->absmax[2];
}

static int CompoundWorldAutomaticTriggerSafe(const edict_t *trigger)
{
	return trigger && trigger->inuse && trigger->solid == SOLID_TRIGGER &&
	       trigger->movetype == MOVETYPE_NONE &&
	       trigger->touch == Touch_DoorTrigger && trigger->owner &&
	       trigger->classname && !strcmp(trigger->classname, "noclass") &&
	       !trigger->target && !trigger->targetname &&
	       !trigger->killtarget && !trigger->message && !trigger->use &&
	       !trigger->think && !trigger->blocked;
}

static int CompoundWorldDoorAxis(const edict_t *door)
{
	int axis;
	int motion_axis = -1;

	if (!door || !CompoundWorldFinite3(door->moveinfo.start_origin) ||
	    !CompoundWorldFinite3(door->moveinfo.end_origin))
		return -1;
	for (axis = 0; axis < 3; axis++)
	{
		float delta = door->moveinfo.end_origin[axis] -
		              door->moveinfo.start_origin[axis];

		if (!isfinite(delta))
			return -1;
		if (delta == 0.0f)
			continue;
		if (motion_axis >= 0)
			return -1;
		motion_axis = axis;
	}
	return motion_axis;
}

static int CompoundWorldDoorSafe(edict_t *door, int *axis_out)
{
	float delta;
	int axis;
	int mover_index;

	if (!door || !axis_out)
		return 0;
	mover_index = CompoundWorldEntityIndex(door);
	axis = CompoundWorldDoorAxis(door);
	if (mover_index <= 0 || axis < 0 || !door->inuse ||
	    !door->classname || strcmp(door->classname, "func_door") != 0 ||
	    door->teammaster != door || door->teamchain ||
	    (door->flags & FL_TEAMSLAVE) || door->movetype != MOVETYPE_PUSH ||
	    door->solid != SOLID_BSP || door->use != door_use ||
	    door->blocked != door_blocked || door->touch || door->prethink ||
	    door->pain || door->die || door->health != 0 ||
	    door->max_health != 0 || door->takedamage != DAMAGE_NO ||
	    door->targetname ||
	    (door->spawnflags & (SG_COMPOUND_DOOR_START_OPEN |
	                         SG_COMPOUND_DOOR_CRUSHER |
	                         SG_COMPOUND_DOOR_TOGGLE)) ||
	    door->moveinfo.state != SG_PLAT_STATE_BOTTOM ||
	    door->nextthink != 0.0f ||
	    !CompoundWorldVectorZero(door->velocity) ||
	    !CompoundWorldVectorZero(door->avelocity) ||
	    !CompoundWorldFinite3(door->s.origin) ||
	    !CompoundWorldFinite3(door->s.angles) ||
	    !CompoundWorldFinite3(door->moveinfo.start_angles) ||
	    !CompoundWorldFinite3(door->moveinfo.end_angles) ||
	    !CompoundWorldFinite3(door->pos1) ||
	    !CompoundWorldFinite3(door->pos2) ||
	    !CompoundWorldVectorEqual(door->s.origin,
	                              door->moveinfo.start_origin) ||
	    !CompoundWorldVectorEqual(door->pos1,
	                              door->moveinfo.start_origin) ||
	    !CompoundWorldVectorEqual(door->pos2,
	                              door->moveinfo.end_origin) ||
	    !CompoundWorldVectorEqual(door->s.angles,
	                              door->moveinfo.start_angles) ||
	    !CompoundWorldVectorEqual(door->moveinfo.start_angles,
	                              door->moveinfo.end_angles) ||
	    !isfinite(door->moveinfo.distance) ||
	    door->moveinfo.distance <= 0.0f ||
	    !isfinite(door->moveinfo.speed) ||
	    door->moveinfo.speed <= 0.0f ||
	    door->moveinfo.accel != door->moveinfo.speed ||
	    door->moveinfo.decel != door->moveinfo.speed ||
	    !isfinite(door->moveinfo.wait) || door->moveinfo.wait <= 0.0f ||
	    door->wait != door->moveinfo.wait ||
	    !SG_CompoundWorldDoorEffectsSafe(door))
		return 0;
	delta = door->moveinfo.end_origin[axis] -
	        door->moveinfo.start_origin[axis];
	if (fabsf(delta) != door->moveinfo.distance)
		return 0;
	*axis_out = axis;
	return 1;
}

rune_reject_reason_t SG_CompoundWorldResolvePreopen(
	const float mechanism_anchor[3],
	sg_compound_world_preopen_t *resolved)
{
	edict_t *matched_trigger = NULL;
	edict_t *matched_member = NULL;
	int matched_axis = -1;
	int saw_unsupported = 0;
	int saw_unsafe = 0;
	int ambiguous = 0;
	int index;

	if (!resolved)
		return RLR_BAD_CONTROL_POLICY;
	memset(resolved, 0, sizeof(*resolved));
	resolved->mover_key = -1;
	resolved->axis = -1;
	if (!CompoundWorldFinite3(mechanism_anchor))
		return RLR_BAD_MECHANISM_ANCHOR;
	if (!g_edicts || globals.num_edicts <= 1)
		return RLR_MECHANISM_UNRESOLVED;
	for (index = 1; index < globals.num_edicts; index++)
	{
		edict_t *trigger = &g_edicts[index];
		edict_t *member;
		int axis;

		if (!trigger->inuse ||
		    (trigger->solid != SOLID_TRIGGER && !trigger->touch) ||
		    !CompoundWorldTriggerContains(trigger, mechanism_anchor))
			continue;
		if (!CompoundWorldAutomaticTriggerSafe(trigger))
		{
			saw_unsupported = 1;
			continue;
		}
		member = trigger->owner;
		if (!CompoundWorldDoorSafe(member, &axis))
		{
			saw_unsafe = 1;
			continue;
		}
		if (matched_member && matched_member != member)
		{
			ambiguous = 1;
			continue;
		}
		if (!matched_member)
		{
			matched_trigger = trigger;
			matched_member = member;
			matched_axis = axis;
		}
	}
	if (saw_unsupported)
		return RLR_UNSUPPORTED_ACTIVATOR;
	if (saw_unsafe)
		return RLR_DOOR_TEAM_UNSAFE;
	if (ambiguous)
		return RLR_MECHANISM_AMBIGUOUS;
	if (!matched_member)
		return RLR_MECHANISM_UNRESOLVED;
	resolved->trigger = matched_trigger;
	resolved->member = matched_member;
	memcpy(resolved->bottom_origin, matched_member->moveinfo.start_origin,
	       sizeof(resolved->bottom_origin));
	memcpy(resolved->top_origin, matched_member->moveinfo.end_origin,
	       sizeof(resolved->top_origin));
	resolved->speed = matched_member->moveinfo.speed;
	resolved->inert_effect_delay = matched_member->delay;
	resolved->mover_key = CompoundWorldEntityIndex(matched_member);
	resolved->axis = matched_axis;
	return RLR_OK;
}
