/* sg_compound_world.c -- strict world adapter for PREOPEN compound replay. */
#include "../g_local.h"

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "sg_compound.h"
#include "sg_compound_world.h"
#include "sg_util.h"

void Touch_DoorTrigger(edict_t *self, edict_t *other, cplane_t *plane,
	csurface_t *surf);
void Use_Target_Speaker(edict_t *self, edict_t *other,
	edict_t *activator);
void door_blocked(edict_t *self, edict_t *other);
void door_go_down(edict_t *self);
void door_hit_bottom(edict_t *self);
void door_hit_top(edict_t *self);
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

	if (!entity || !g_edicts || globals.num_edicts <= 0 ||
	    globals.num_edicts > MAX_EDICTS)
		return -1;
	for (index = 0; index < globals.num_edicts; index++)
		if (&g_edicts[index] == entity)
			return index;
	return -1;
}

typedef enum compound_world_completion_phase_e
{
	COMPOUND_WORLD_COMPLETION_EMPTY = 0,
	COMPOUND_WORLD_COMPLETION_MOVING,
	COMPOUND_WORLD_COMPLETION_TOP,
	COMPOUND_WORLD_COMPLETION_BOTTOM
} compound_world_completion_phase_t;

typedef struct compound_world_completion_s
{
	edict_t *member;
	edict_t *teammaster;
	edict_t *teamchain;
	const char *classname;
	float origin[3];
	float angles[3];
	float absmin[3];
	float absmax[3];
	float size[3];
	float mins[3];
	float maxs[3];
	float start_origin[3];
	float end_origin[3];
	float start_angles[3];
	float end_angles[3];
	float direction[3];
	float remaining_distance;
	float speed;
	float accel;
	float decel;
	float distance;
	float wait;
	void (*endfunc)(edict_t *);
	void (*armed_endfunc)(edict_t *);
	int key;
	int entity_number;
	int linkcount;
	solid_t solid;
	int movetype;
	int flags;
	int spawnflags;
	uint8_t phase;
} compound_world_completion_t;

static compound_world_completion_t
	compound_world_completions[MAX_EDICTS];

typedef struct compound_world_completion_scope_s
{
	edict_t *member;
	void (*endfunc)(edict_t *);
	int key;
	uint8_t active;
	uint8_t consumed;
	uint8_t poisoned;
} compound_world_completion_scope_t;

static compound_world_completion_scope_t compound_world_completion_scope;

/* A separate numeric mover generation would duplicate the lifecycle without
 * strengthening it: map doors retain one edict incarnation, G_FreeEdict calls
 * Forget before that key can be reused, and every level/storage reset clears
 * the table before old TAG_LEVEL pointers disappear.  Key + exact pointer +
 * s.number therefore bind the record to that one live incarnation. */

/* Match the exact transaction produced by SV_LinkEdict.  Area-list neighbor
 * pointers legitimately change when unrelated entities link, so they prove
 * current linkage but are not frozen into the completion witness. */
static int CompoundWorldLinkedPoseValid(const edict_t *member)
{
	float radius = 0.0f;
	int rotated;
	int axis;

	if (!member || member->linkcount <= 0 || !member->area.prev ||
	    !member->area.next || member->area.prev == &member->area ||
	    member->area.next == &member->area ||
	    !CompoundWorldFinite3(member->s.origin) ||
	    !CompoundWorldFinite3(member->s.angles) ||
	    !CompoundWorldFinite3(member->mins) ||
	    !CompoundWorldFinite3(member->maxs) ||
	    !CompoundWorldFinite3(member->size) ||
	    !CompoundWorldFinite3(member->absmin) ||
	    !CompoundWorldFinite3(member->absmax))
		return 0;
	rotated = member->solid == SOLID_BSP &&
	          (member->s.angles[0] != 0.0f ||
	           member->s.angles[1] != 0.0f ||
	           member->s.angles[2] != 0.0f);
	if (rotated)
		for (axis = 0; axis < 3; axis++)
		{
			float low = fabsf(member->mins[axis]);
			float high = fabsf(member->maxs[axis]);

			if (low > radius)
				radius = low;
			if (high > radius)
				radius = high;
		}
	for (axis = 0; axis < 3; axis++)
	{
		float size = member->maxs[axis] - member->mins[axis];
		float absmin = member->s.origin[axis] +
		               (rotated ? -radius : member->mins[axis]) - 1.0f;
		float absmax = member->s.origin[axis] +
		               (rotated ? radius : member->maxs[axis]) + 1.0f;

		if (!isfinite(size) || !isfinite(absmin) || !isfinite(absmax) ||
		    member->size[axis] != size ||
		    member->absmin[axis] != absmin ||
		    member->absmax[axis] != absmax)
			return 0;
	}
	return 1;
}

static compound_world_completion_phase_t CompoundWorldCompletionPhase(
	sg_mover_completion_kind_t kind)
{
	if (kind == SG_MOVER_COMPLETION_TOP)
		return COMPOUND_WORLD_COMPLETION_TOP;
	if (kind == SG_MOVER_COMPLETION_BOTTOM)
		return COMPOUND_WORLD_COMPLETION_BOTTOM;
	return COMPOUND_WORLD_COMPLETION_EMPTY;
}

void SG_MoverCompletionTransition(edict_t *member)
{
	compound_world_completion_t *completion;
	int key = CompoundWorldEntityIndex(member);

	if (key <= 0 || key >= MAX_EDICTS)
		return;
	completion = &compound_world_completions[key];
	memset(completion, 0, sizeof(*completion));
	completion->member = member;
	completion->key = key;
	completion->phase = COMPOUND_WORLD_COMPLETION_MOVING;
}

void SG_MoverCompletionArm(edict_t *member)
{
	compound_world_completion_t *completion;
	int key = CompoundWorldEntityIndex(member);

	if (key <= 0 || key >= MAX_EDICTS || !member || !member->inuse)
		return;
	completion = &compound_world_completions[key];
	completion->armed_endfunc = NULL;
	if (completion->phase != COMPOUND_WORLD_COMPLETION_MOVING ||
	    completion->member != member || completion->key != key ||
	    (member->moveinfo.endfunc != door_hit_top &&
	     member->moveinfo.endfunc != door_hit_bottom))
		return;
	completion->armed_endfunc = member->moveinfo.endfunc;
}

void SG_MoverCompletionDispatch(edict_t *member)
{
	compound_world_completion_t *completion;
	void (*endfunc)(edict_t *);
	int key = CompoundWorldEntityIndex(member);

	if (!member || !member->moveinfo.endfunc)
		return;
	endfunc = member->moveinfo.endfunc;
	if (compound_world_completion_scope.active)
	{
		/* A nested completion is not a stock mover transition.  Preserve the
		 * callback behavior but poison the outer scope so neither callback can
		 * mint physical authority. */
		compound_world_completion_scope.poisoned = 1;
		endfunc(member);
		return;
	}
	if (key <= 0 || key >= MAX_EDICTS)
	{
		endfunc(member);
		return;
	}
	completion = &compound_world_completions[key];
	if (completion->phase != COMPOUND_WORLD_COMPLETION_MOVING ||
	    completion->member != member || completion->key != key ||
	    completion->armed_endfunc != endfunc)
	{
		endfunc(member);
		return;
	}

	/* Consume the movement arm before invoking the callback.  Publish is a
	 * one-shot capability valid only during this exact stock completion call. */
	completion->armed_endfunc = NULL;
	memset(&compound_world_completion_scope, 0,
	       sizeof(compound_world_completion_scope));
	compound_world_completion_scope.member = member;
	compound_world_completion_scope.endfunc = endfunc;
	compound_world_completion_scope.key = key;
	compound_world_completion_scope.active = 1;
	endfunc(member);
	memset(&compound_world_completion_scope, 0,
	       sizeof(compound_world_completion_scope));
}

void SG_MoverCompletionPublish(edict_t *member,
	sg_mover_completion_kind_t kind)
{
	compound_world_completion_t *completion;
	compound_world_completion_phase_t phase =
		CompoundWorldCompletionPhase(kind);
	edict_t *captain;
	int key;

	/* A malformed completion must retire any older authority for this slot. */
	SG_MoverCompletionTransition(member);
	key = CompoundWorldEntityIndex(member);
	if (phase == COMPOUND_WORLD_COMPLETION_EMPTY || key <= 0 ||
	    key >= MAX_EDICTS || !member->inuse || member->s.number != key ||
	    !member->classname ||
	    (strcmp(member->classname, "func_door") != 0 &&
	     strcmp(member->classname, "func_door_rotating") != 0) ||
	    member->solid != SOLID_BSP || member->movetype != MOVETYPE_PUSH ||
	    !CompoundWorldVectorZero(member->velocity) ||
	    !CompoundWorldVectorZero(member->avelocity) ||
	    !CompoundWorldFinite3(member->moveinfo.start_origin) ||
	    !CompoundWorldFinite3(member->moveinfo.end_origin) ||
	    !CompoundWorldFinite3(member->moveinfo.start_angles) ||
	    !CompoundWorldFinite3(member->moveinfo.end_angles) ||
	    !CompoundWorldFinite3(member->moveinfo.dir) ||
	    !isfinite(member->moveinfo.remaining_distance) ||
	    !isfinite(member->moveinfo.speed) ||
	    !isfinite(member->moveinfo.accel) ||
	    !isfinite(member->moveinfo.decel) ||
	    !isfinite(member->moveinfo.distance) ||
	    !isfinite(member->moveinfo.wait) ||
	    !CompoundWorldLinkedPoseValid(member))
		return;
	if (!compound_world_completion_scope.active ||
	    compound_world_completion_scope.consumed ||
	    compound_world_completion_scope.poisoned ||
	    compound_world_completion_scope.member != member ||
	    compound_world_completion_scope.key != key ||
	    compound_world_completion_scope.endfunc !=
	        member->moveinfo.endfunc)
		return;
	compound_world_completion_scope.consumed = 1;
	captain = (member->flags & FL_TEAMSLAVE) ? member->teammaster : member;
	/* Completion callbacks reached through the real pusher pass run under the
	 * exact captain selected by G_RunFrame.  Oracle/staging calls do not mint
	 * physical authority merely by invoking the same C function. */
	if (!captain || level.current_entity != captain)
		return;
	if ((phase == COMPOUND_WORLD_COMPLETION_TOP &&
	     (member->moveinfo.state != SG_PLAT_STATE_TOP ||
	      member->moveinfo.endfunc != door_hit_top)) ||
	    (phase == COMPOUND_WORLD_COMPLETION_BOTTOM &&
	     (member->moveinfo.state != SG_PLAT_STATE_BOTTOM ||
	      member->moveinfo.endfunc != door_hit_bottom)))
		return;

	completion = &compound_world_completions[key];
	completion->member = member;
	completion->teammaster = member->teammaster;
	completion->teamchain = member->teamchain;
	completion->classname = member->classname;
	memcpy(completion->origin, member->s.origin,
	       sizeof(completion->origin));
	memcpy(completion->angles, member->s.angles,
	       sizeof(completion->angles));
	memcpy(completion->absmin, member->absmin,
	       sizeof(completion->absmin));
	memcpy(completion->absmax, member->absmax,
	       sizeof(completion->absmax));
	memcpy(completion->size, member->size, sizeof(completion->size));
	memcpy(completion->mins, member->mins, sizeof(completion->mins));
	memcpy(completion->maxs, member->maxs, sizeof(completion->maxs));
	memcpy(completion->start_origin, member->moveinfo.start_origin,
	       sizeof(completion->start_origin));
	memcpy(completion->end_origin, member->moveinfo.end_origin,
	       sizeof(completion->end_origin));
	memcpy(completion->start_angles, member->moveinfo.start_angles,
	       sizeof(completion->start_angles));
	memcpy(completion->end_angles, member->moveinfo.end_angles,
	       sizeof(completion->end_angles));
	memcpy(completion->direction, member->moveinfo.dir,
	       sizeof(completion->direction));
	completion->remaining_distance = member->moveinfo.remaining_distance;
	completion->speed = member->moveinfo.speed;
	completion->accel = member->moveinfo.accel;
	completion->decel = member->moveinfo.decel;
	completion->distance = member->moveinfo.distance;
	completion->wait = member->moveinfo.wait;
	completion->endfunc = member->moveinfo.endfunc;
	completion->key = key;
	completion->entity_number = member->s.number;
	completion->linkcount = member->linkcount;
	completion->solid = member->solid;
	completion->movetype = member->movetype;
	completion->flags = member->flags;
	completion->spawnflags = member->spawnflags;
	completion->phase = (uint8_t)phase;
}

int SG_MoverCompletionMatches(const edict_t *member,
	sg_mover_completion_kind_t kind)
{
	const compound_world_completion_t *completion;
	compound_world_completion_phase_t phase =
		CompoundWorldCompletionPhase(kind);
	int key = CompoundWorldEntityIndex(member);

	if (phase == COMPOUND_WORLD_COMPLETION_EMPTY || key <= 0 ||
	    key >= MAX_EDICTS || !member || !member->inuse ||
	    !CompoundWorldLinkedPoseValid(member))
		return 0;
	completion = &compound_world_completions[key];
	return completion->phase == (uint8_t)phase &&
	       completion->member == member && completion->key == key &&
	       completion->entity_number == member->s.number &&
	       completion->linkcount == member->linkcount &&
	       completion->solid == member->solid &&
	       completion->movetype == member->movetype &&
	       completion->flags == member->flags &&
	       completion->spawnflags == member->spawnflags &&
	       completion->teammaster == member->teammaster &&
	       completion->teamchain == member->teamchain &&
	       completion->classname == member->classname &&
	       completion->endfunc == member->moveinfo.endfunc &&
	       memcmp(completion->origin, member->s.origin,
	              sizeof(completion->origin)) == 0 &&
	       memcmp(completion->angles, member->s.angles,
	              sizeof(completion->angles)) == 0 &&
	       memcmp(completion->absmin, member->absmin,
	              sizeof(completion->absmin)) == 0 &&
	       memcmp(completion->absmax, member->absmax,
	              sizeof(completion->absmax)) == 0 &&
	       memcmp(completion->size, member->size,
	              sizeof(completion->size)) == 0 &&
	       memcmp(completion->mins, member->mins,
	              sizeof(completion->mins)) == 0 &&
	       memcmp(completion->maxs, member->maxs,
	              sizeof(completion->maxs)) == 0 &&
	       memcmp(completion->start_origin,
	              member->moveinfo.start_origin,
	              sizeof(completion->start_origin)) == 0 &&
	       memcmp(completion->end_origin, member->moveinfo.end_origin,
	              sizeof(completion->end_origin)) == 0 &&
	       memcmp(completion->start_angles,
	              member->moveinfo.start_angles,
	              sizeof(completion->start_angles)) == 0 &&
	       memcmp(completion->end_angles, member->moveinfo.end_angles,
	              sizeof(completion->end_angles)) == 0 &&
	       memcmp(completion->direction, member->moveinfo.dir,
	              sizeof(completion->direction)) == 0 &&
	       memcmp(&completion->remaining_distance,
	              &member->moveinfo.remaining_distance,
	              sizeof(completion->remaining_distance)) == 0 &&
	       memcmp(&completion->speed, &member->moveinfo.speed,
	              sizeof(completion->speed)) == 0 &&
	       memcmp(&completion->accel, &member->moveinfo.accel,
	              sizeof(completion->accel)) == 0 &&
	       memcmp(&completion->decel, &member->moveinfo.decel,
	              sizeof(completion->decel)) == 0 &&
	       memcmp(&completion->distance, &member->moveinfo.distance,
	              sizeof(completion->distance)) == 0 &&
	       memcmp(&completion->wait, &member->moveinfo.wait,
	              sizeof(completion->wait)) == 0;
}

int SG_MoverCompletionUntouched(const edict_t *member)
{
	int key = CompoundWorldEntityIndex(member);

	return key > 0 && key < MAX_EDICTS && member && member->inuse &&
	       compound_world_completions[key].phase ==
	           COMPOUND_WORLD_COMPLETION_EMPTY;
}

void SG_MoverCompletionForget(edict_t *member)
{
	int key = CompoundWorldEntityIndex(member);

	if (compound_world_completion_scope.member == member)
		memset(&compound_world_completion_scope, 0,
		       sizeof(compound_world_completion_scope));
	if (key > 0 && key < MAX_EDICTS)
		memset(&compound_world_completions[key], 0,
		       sizeof(compound_world_completions[key]));
}

void SG_MoverCompletionReset(void)
{
	memset(&compound_world_completion_scope, 0,
	       sizeof(compound_world_completion_scope));
	memset(compound_world_completions, 0,
	       sizeof(compound_world_completions));
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

static int CompoundWorldBoundsSafe(const edict_t *door)
{
	int axis;

	if (!door || !CompoundWorldFinite3(door->mins) ||
	    !CompoundWorldFinite3(door->maxs))
		return 0;
	for (axis = 0; axis < 3; axis++)
		if (door->mins[axis] > door->maxs[axis])
			return 0;
	return 1;
}

static int CompoundWorldQuantizedMove(float velocity, float *move_out);

static int CompoundWorldDoorStaticSafe(edict_t *door, int *axis_out)
{
	float delta;
	float full_move;
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
	    !CompoundWorldBoundsSafe(door) ||
	    !CompoundWorldFinite3(door->s.angles) ||
	    !CompoundWorldVectorZero(door->s.angles) ||
	    !CompoundWorldFinite3(door->moveinfo.start_angles) ||
	    !CompoundWorldFinite3(door->moveinfo.end_angles) ||
	    !CompoundWorldFinite3(door->pos1) ||
	    !CompoundWorldFinite3(door->pos2) ||
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
	    !CompoundWorldQuantizedMove(door->moveinfo.speed, &full_move) ||
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

/* Reproduce the stock constant-speed Move_Calc/SV_Push path exactly.  The
 * pusher quantizes every 100 ms displacement independently, so multiplying a
 * nominal endpoint error by a frame count is neither exact nor sufficient:
 * the closing Move_Calc starts at the already-quantized physical TOP. */
static int CompoundWorldQuantizedMove(float velocity, float *move_out)
{
	float move, scaled;
	double integral;

	if (!move_out || !isfinite(velocity))
		return 0;
	move = velocity * FRAMETIME;
	scaled = move * 8.0f;
	if (!isfinite(move) || !isfinite(scaled))
		return 0;
	if (scaled > 0.0f)
		scaled += 0.5f;
	else
		scaled -= 0.5f;
	if (!isfinite(scaled))
		return 0;
	/* Converting INT_MAX to float rounds it up to 2^31 on binary32.  Compare
	 * the truncated value in double before the integer conversion, matching
	 * the stock-pusher quantizer used by the compound proof reducer. */
	integral = trunc((double)scaled);
	if (!isfinite(integral) || integral < (double)INT_MIN ||
	    integral > (double)INT_MAX)
		return 0;
	*move_out = 0.125f * (float)(int)integral;
	return isfinite(*move_out);
}

static int CompoundWorldDoorBottomSafe(edict_t *door, int *axis_out)
{
	return CompoundWorldDoorStaticSafe(door, axis_out) &&
	       door->moveinfo.state == SG_PLAT_STATE_BOTTOM &&
	       door->nextthink == 0.0f &&
	       CompoundWorldVectorZero(door->velocity) &&
	       CompoundWorldVectorZero(door->avelocity) &&
	       CompoundWorldFinite3(door->s.origin) &&
	       CompoundWorldVectorEqual(door->s.origin,
	                                door->moveinfo.start_origin);
}

static void CompoundWorldResolvedFrom(edict_t *trigger, edict_t *member,
	int axis, sg_compound_world_preopen_t *resolved)
{
	memset(resolved, 0, sizeof(*resolved));
	resolved->trigger = trigger;
	resolved->member = member;
	memcpy(resolved->bottom_origin, member->moveinfo.start_origin,
	       sizeof(resolved->bottom_origin));
	memcpy(resolved->top_origin, member->moveinfo.end_origin,
	       sizeof(resolved->top_origin));
	memcpy(resolved->member_mins, member->mins,
	       sizeof(resolved->member_mins));
	memcpy(resolved->member_maxs, member->maxs,
	       sizeof(resolved->member_maxs));
	memcpy(resolved->fixed_angles, member->s.angles,
	       sizeof(resolved->fixed_angles));
	resolved->speed = member->moveinfo.speed;
	resolved->wait = member->moveinfo.wait;
	resolved->inert_effect_delay = member->delay;
	resolved->trigger_key = CompoundWorldEntityIndex(trigger);
	resolved->mover_key = CompoundWorldEntityIndex(member);
	resolved->axis = axis;
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
	resolved->trigger_key = -1;
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
		if (!CompoundWorldDoorBottomSafe(member, &axis))
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
	CompoundWorldResolvedFrom(matched_trigger, matched_member, matched_axis,
	                          resolved);
	return RLR_OK;
}

static int CompoundWorldTriggerCentreUnits(const edict_t *trigger,
	int low_units[3], int high_units[3])
{
	static const float player_mins[3] = { -17.0f, -17.0f, -25.0f };
	static const float player_maxs[3] = { 17.0f, 17.0f, 33.0f };
	int axis;

	if (!trigger || !low_units || !high_units ||
	    !CompoundWorldFinite3(trigger->absmin) ||
	    !CompoundWorldFinite3(trigger->absmax))
		return 0;
	for (axis = 0; axis < 3; axis++)
	{
		double lower = ((double)trigger->absmin[axis] -
		                (double)player_maxs[axis]) * 8.0;
		double upper = ((double)trigger->absmax[axis] -
		                (double)player_mins[axis]) * 8.0;
		double first = floor(lower) + 1.0;
		double last = ceil(upper) - 1.0;

		if (!isfinite(lower) || !isfinite(upper) || first > last ||
		    last < -32768.0 || first > 32767.0)
			return 0;
		if (first < -32768.0)
			first = -32768.0;
		if (last > 32767.0)
			last = 32767.0;
		low_units[axis] = (int)first;
		high_units[axis] = (int)last;
	}
	return 1;
}

static int CompoundWorldBuildCandidate(edict_t *trigger, edict_t *member,
	int axis, sg_compound_world_candidate_t *candidate)
{
	sg_compound_world_preopen_t exact;
	int low_units[3], high_units[3], middle_units[3];
	int face, coordinate;

	if (!candidate ||
	    !CompoundWorldTriggerCentreUnits(trigger, low_units, high_units))
		return 0;
	memset(candidate, 0, sizeof(*candidate));
	CompoundWorldResolvedFrom(trigger, member, axis, &candidate->resolved);
	for (coordinate = 0; coordinate < 3; coordinate++)
		middle_units[coordinate] = low_units[coordinate] +
			(high_units[coordinate] - low_units[coordinate]) / 2;
	/* Stable face order: X-low, X-high, Y-low, Y-high, Z-low,
	 * Z-high.  Each face uses the first/last representable player centre
	 * strictly overlapping the trigger on that axis. */
	for (face = 0; face < SG_COMPOUND_WORLD_PREOPEN_HINT_MAX; face++)
	{
		float hint[3];
		int hint_axis = face / 2;
		int duplicate = 0;
		int hint_index;

		for (coordinate = 0; coordinate < 3; coordinate++)
		{
			int units = middle_units[coordinate];

			if (coordinate == hint_axis)
				units = (face & 1) ? high_units[coordinate] :
				        low_units[coordinate];
			hint[coordinate] = (float)units * 0.125f;
			if (hint[coordinate] == 0.0f)
				hint[coordinate] = 0.0f;
		}
		if (!CompoundWorldTriggerContains(trigger, hint) ||
		    !SG_CompoundWorldOutsideSweep(&candidate->resolved, hint) ||
		    SG_CompoundWorldResolvePreopen(hint, &exact) != RLR_OK ||
		    exact.trigger_key != candidate->resolved.trigger_key ||
		    exact.mover_key != candidate->resolved.mover_key)
			continue;
		for (hint_index = 0; hint_index < candidate->hint_count;
		     hint_index++)
			if (CompoundWorldVectorEqual(candidate->hints[hint_index],
			                             hint))
				duplicate = 1;
		if (duplicate)
			continue;
		memcpy(candidate->hints[candidate->hint_count], hint,
		       sizeof(hint));
		candidate->hint_count++;
	}
	return candidate->hint_count > 0;
}

rune_reject_reason_t SG_CompoundWorldEnumeratePreopen(
	sg_compound_world_candidate_t *candidates, int capacity,
	int *count_out)
{
	int count = 0;
	int index;

	if (count_out)
		*count_out = 0;
	if (!count_out || capacity < 0 || (!candidates && capacity != 0))
		return RLR_BAD_CONTROL_POLICY;
	if (!g_edicts || globals.num_edicts <= 1)
		return RLR_OK;
	/* More than one admitted automatic trigger for the same physical member
	 * has no unique stable mechanism identity.  Reject the complete snapshot
	 * rather than choosing one by incidental discovery geometry. */
	for (index = 1; index < globals.num_edicts; index++)
	{
		edict_t *trigger = &g_edicts[index];
		int axis;
		int prior;

		if (!CompoundWorldAutomaticTriggerSafe(trigger) ||
		    !CompoundWorldDoorBottomSafe(trigger->owner, &axis))
			continue;
		for (prior = 1; prior < index; prior++)
		{
			edict_t *other = &g_edicts[prior];
			int other_axis;

			if (other->owner == trigger->owner &&
			    CompoundWorldAutomaticTriggerSafe(other) &&
			    CompoundWorldDoorBottomSafe(other->owner, &other_axis))
				return RLR_MECHANISM_AMBIGUOUS;
		}
	}
	for (index = 1; index < globals.num_edicts; index++)
	{
		edict_t *trigger = &g_edicts[index];
		sg_compound_world_candidate_t candidate;
		int axis;

		if (CompoundWorldAutomaticTriggerSafe(trigger) &&
		    CompoundWorldDoorBottomSafe(trigger->owner, &axis) &&
		    CompoundWorldBuildCandidate(trigger, trigger->owner, axis,
		                                &candidate))
			count++;
	}
	*count_out = count;
	if (!candidates)
		return RLR_OK;
	if (capacity < count)
		return RLR_BAD_CONTROL_POLICY;
	count = 0;
	for (index = 1; index < globals.num_edicts; index++)
	{
		edict_t *trigger = &g_edicts[index];
		sg_compound_world_candidate_t candidate;
		int axis;

		if (CompoundWorldAutomaticTriggerSafe(trigger) &&
		    CompoundWorldDoorBottomSafe(trigger->owner, &axis) &&
		    CompoundWorldBuildCandidate(trigger, trigger->owner, axis,
		                                &candidate))
			candidates[count++] = candidate;
	}
	return RLR_OK;
}

int SG_CompoundWorldPreopenHintMatches(
	const sg_compound_world_preopen_t *resolved, const float hint[3])
{
	sg_compound_world_candidate_t candidate;
	edict_t *member = NULL;
	int axis;
	int hint_index;

	if (!resolved || !hint ||
	    !SG_CompoundWorldResolvedMember(resolved, &member) ||
	    member != resolved->member ||
	    !CompoundWorldDoorBottomSafe(member, &axis) ||
	    axis != resolved->axis ||
	    !CompoundWorldBuildCandidate(resolved->trigger, member, axis,
	                                &candidate))
		return 0;
	for (hint_index = 0; hint_index < candidate.hint_count; hint_index++)
		if (memcmp(candidate.hints[hint_index], hint,
		           sizeof(candidate.hints[hint_index])) == 0)
			return 1;
	return 0;
}

int SG_CompoundWorldResolvedMember(
	const sg_compound_world_preopen_t *resolved, edict_t **member_out)
{
	edict_t *member;
	edict_t *trigger;
	int axis;

	if (member_out)
		*member_out = NULL;
	if (!resolved || !member_out || !g_edicts ||
	    resolved->trigger_key <= 0 ||
	    resolved->trigger_key >= globals.num_edicts ||
	    resolved->mover_key <= 0 ||
	    resolved->mover_key >= globals.num_edicts ||
	    resolved->trigger_key == resolved->mover_key)
		return 0;
	trigger = &g_edicts[resolved->trigger_key];
	member = &g_edicts[resolved->mover_key];
	if (resolved->trigger != trigger || resolved->member != member ||
	    !CompoundWorldAutomaticTriggerSafe(trigger) ||
	    trigger->owner != member ||
	    !CompoundWorldDoorStaticSafe(member, &axis) ||
	    resolved->axis != axis ||
	    !CompoundWorldVectorEqual(resolved->bottom_origin,
	                              member->moveinfo.start_origin) ||
	    !CompoundWorldVectorEqual(resolved->top_origin,
	                              member->moveinfo.end_origin) ||
	    !CompoundWorldVectorEqual(resolved->member_mins, member->mins) ||
	    !CompoundWorldVectorEqual(resolved->member_maxs, member->maxs) ||
	    !CompoundWorldVectorEqual(resolved->fixed_angles,
	                              member->s.angles) ||
	    resolved->speed != member->moveinfo.speed ||
	    resolved->wait != member->moveinfo.wait ||
	    resolved->inert_effect_delay != member->delay)
		return 0;
	*member_out = member;
	return 1;
}

static int CompoundWorldSweepBounds(
	const sg_compound_world_preopen_t *resolved,
	float mins[3], float maxs[3])
{
	static const float hull_mins[3] = { -16.0f, -16.0f, -24.0f };
	static const float hull_maxs[3] = { 16.0f, 16.0f, 32.0f };
	edict_t *member;
	int axis;

	if (!mins || !maxs ||
	    !SG_CompoundWorldResolvedMember(resolved, &member))
		return 0;
	(void)member;
	for (axis = 0; axis < 3; axis++)
	{
		float next_move;
		float next_origin;
		float start_min = resolved->bottom_origin[axis] +
		                  resolved->member_mins[axis];
		float start_max = resolved->bottom_origin[axis] +
		                  resolved->member_maxs[axis];
		float end_min = resolved->top_origin[axis] +
		                resolved->member_mins[axis];
		float end_max = resolved->top_origin[axis] +
		                resolved->member_maxs[axis];
		float current_min = member->s.origin[axis] +
		                    resolved->member_mins[axis];
		float current_max = member->s.origin[axis] +
		                    resolved->member_maxs[axis];
		float next_min;
		float next_max;

		/* The pusher moves before its think callback.  Include the one
		 * quantized displacement that can occur in the next entity pass, so
		 * a late Move_Final timer cannot outrun the prior frame's subject
		 * clearance proof.  A blocked push only makes this conservative. */
		if (!CompoundWorldQuantizedMove(member->velocity[axis], &next_move))
			return 0;
		next_origin = member->s.origin[axis] + next_move;
		next_min = next_origin + resolved->member_mins[axis];
		next_max = next_origin + resolved->member_maxs[axis];
		if (!isfinite(start_min) || !isfinite(start_max) ||
		    !isfinite(end_min) || !isfinite(end_max) ||
		    !isfinite(current_min) || !isfinite(current_max) ||
		    !isfinite(next_origin) || !isfinite(next_min) ||
		    !isfinite(next_max))
			return 0;
		mins[axis] = (start_min < end_min ? start_min : end_min) -
		             hull_maxs[axis];
		maxs[axis] = (start_max > end_max ? start_max : end_max) -
		             hull_mins[axis];
		if (current_min - hull_maxs[axis] < mins[axis])
			mins[axis] = current_min - hull_maxs[axis];
		if (current_max - hull_mins[axis] > maxs[axis])
			maxs[axis] = current_max - hull_mins[axis];
		if (next_min - hull_maxs[axis] < mins[axis])
			mins[axis] = next_min - hull_maxs[axis];
		if (next_max - hull_mins[axis] > maxs[axis])
			maxs[axis] = next_max - hull_mins[axis];
		if (!isfinite(mins[axis]) || !isfinite(maxs[axis]) ||
		    mins[axis] > maxs[axis])
			return 0;
	}
	return 1;
}

int SG_CompoundWorldOutsideSweep(
	const sg_compound_world_preopen_t *resolved, const float origin[3])
{
	float mins[3], maxs[3];
	int axis;

	if (!CompoundWorldFinite3(origin) ||
	    !CompoundWorldSweepBounds(resolved, mins, maxs))
		return 0;
	for (axis = 0; axis < 3; axis++)
		if (origin[axis] < mins[axis] || origin[axis] > maxs[axis])
			return 1;
	return 0;
}

int SG_CompoundWorldCrossesSweep(
	const sg_compound_world_preopen_t *resolved, const float from[3],
	const float to[3])
{
	float mins[3], maxs[3];
	double low = 0.0;
	double high = 1.0;
	int axis;

	if (!CompoundWorldFinite3(from) || !CompoundWorldFinite3(to) ||
	    !CompoundWorldSweepBounds(resolved, mins, maxs))
		return 0;
	for (axis = 0; axis < 3; axis++)
	{
		double start = (double)from[axis];
		double delta = (double)to[axis] - start;

		if (delta == 0.0)
		{
			if (start < (double)mins[axis] ||
			    start > (double)maxs[axis])
				return 0;
		}
		else
		{
			double first = ((double)mins[axis] - start) / delta;
			double last = ((double)maxs[axis] - start) / delta;

			if (first > last)
			{
				double swap = first;
				first = last;
				last = swap;
			}
			if (first > low)
				low = first;
			if (last < high)
				high = last;
			if (low > high)
				return 0;
		}
	}
	return 1;
}

static int CompoundWorldTopPhysicalMember(edict_t *member)
{
	int axis;

	return member && CompoundWorldDoorStaticSafe(member, &axis) &&
	       member->solid == SOLID_BSP &&
	       member->moveinfo.state == SG_PLAT_STATE_TOP &&
	       SG_MoverCompletionMatches(member, SG_MOVER_COMPLETION_TOP) &&
	       CompoundWorldFinite3(member->s.origin) &&
	       member->s.origin[(axis + 1) % 3] ==
	           member->moveinfo.end_origin[(axis + 1) % 3] &&
	       member->s.origin[(axis + 2) % 3] ==
	           member->moveinfo.end_origin[(axis + 2) % 3] &&
	       CompoundWorldVectorZero(member->velocity) &&
	       CompoundWorldVectorZero(member->avelocity) &&
	       member->moveinfo.endfunc == door_hit_top &&
	       member->think == door_go_down && isfinite(member->nextthink) &&
	       isfinite(level.time) && member->nextthink > level.time;
}

static int CompoundWorldTopMember(
	const sg_compound_world_preopen_t *resolved, edict_t **member_out)
{
	edict_t *member;

	if (!SG_CompoundWorldResolvedMember(resolved, &member) ||
	    !CompoundWorldTopPhysicalMember(member))
		return 0;
	*member_out = member;
	return 1;
}

int SG_CompoundWorldAtTopFor(
	const sg_compound_world_preopen_t *resolved, int window_ms)
{
	edict_t *member;
	float until;

	if (window_ms < 0 || window_ms > RUNE_MAX_COST_MS ||
	    window_ms % SG_RUNE_PROOF_SERVER_FRAME_MS != 0 ||
	    !CompoundWorldTopMember(resolved, &member))
		return 0;
	/* SV_RunThink executes an entity whose nextthink is at most the current
	 * frame time plus 1 ms.  Include that exact engine tolerance after the
	 * safety frame; equality is not proof that TOP survives the boundary. */
	until = level.time + (float)window_ms * 0.001f + FRAMETIME;
	until += 0.001f;
	return isfinite(until) && member->nextthink > until;
}

int SG_CompoundWorldHoldOpen(
	const sg_compound_world_preopen_t *resolved, int lease_ms)
{
	edict_t *member;

	if (lease_ms != SG_COMPOUND_HOLD_LEASE_MS ||
	    !CompoundWorldTopMember(resolved, &member))
		return 0;
	return SG_CompoundWorldHoldMember(member, lease_ms);
}

int SG_CompoundWorldHoldMember(edict_t *member, int lease_ms)
{
	float until;

	if (lease_ms != SG_COMPOUND_HOLD_LEASE_MS ||
	    !CompoundWorldTopPhysicalMember(member))
		return 0;
	until = level.time + (float)lease_ms * 0.001f;
	if (!isfinite(until))
		return 0;
	if (member->nextthink < until)
		member->nextthink = until;
	return 1;
}

int SG_CompoundWorldMemberTerminal(edict_t *member)
{
	int axis;

	if (!member || !CompoundWorldDoorStaticSafe(member, &axis) ||
	    !CompoundWorldLinkedPoseValid(member) ||
	    !CompoundWorldVectorZero(member->velocity) ||
	    !CompoundWorldVectorZero(member->avelocity) ||
	    member->nextthink != 0.0f ||
	    member->moveinfo.state != SG_PLAT_STATE_BOTTOM ||
	    !CompoundWorldFinite3(member->s.origin) ||
	    member->s.origin[(axis + 1) % 3] !=
	        member->moveinfo.start_origin[(axis + 1) % 3] ||
	    member->s.origin[(axis + 2) % 3] !=
	        member->moveinfo.start_origin[(axis + 2) % 3])
		return 0;
	/* Quantized residuals can diverge across repeated cycles, so callback fields
	 * alone are not physical authority.  A completed BOTTOM must match the
	 * out-of-edict snapshot minted by the real callback.  The only exception is
	 * an exact initial BOTTOM before any observed transition. */
	return (member->moveinfo.endfunc == door_hit_bottom &&
	        SG_MoverCompletionMatches(member,
	                                  SG_MOVER_COMPLETION_BOTTOM)) ||
	       (!member->moveinfo.endfunc &&
	        SG_MoverCompletionUntouched(member) &&
	        CompoundWorldVectorEqual(member->s.origin,
	                                 member->moveinfo.start_origin) &&
	        CompoundWorldVectorEqual(member->s.angles,
	                                 member->moveinfo.start_angles));
}
