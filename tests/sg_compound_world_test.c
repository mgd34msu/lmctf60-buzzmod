/* Focused live-edict fixtures for PREOPEN compound mechanism resolution. */
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "g_local.h"
#include "slipgate/sg_compound.h"
#include "slipgate/sg_compound_world.h"
#include "slipgate/sg_util.h"

game_export_t globals;
edict_t *g_edicts;
level_locals_t level;

static int touch_calls;
static int speaker_calls;
static int blocked_calls;
static int use_calls;
static int relay_calls;
static int down_calls;

static int CallbackCalls(void)
{
	return touch_calls + speaker_calls + blocked_calls + use_calls +
	       relay_calls + down_calls;
}

static void ResetCallbackCalls(void)
{
	touch_calls = 0;
	speaker_calls = 0;
	blocked_calls = 0;
	use_calls = 0;
	relay_calls = 0;
	down_calls = 0;
}

void Touch_DoorTrigger(edict_t *self, edict_t *other, cplane_t *plane,
	csurface_t *surf)
{
	(void)self; (void)other; (void)plane; (void)surf;
	touch_calls++;
}

void Use_Target_Speaker(edict_t *self, edict_t *other,
	edict_t *activator)
{
	(void)self; (void)other; (void)activator;
	speaker_calls++;
}

void door_blocked(edict_t *self, edict_t *other)
{
	(void)self; (void)other;
	blocked_calls++;
}

void door_go_down(edict_t *self)
{
	(void)self;
	down_calls++;
}

void door_hit_bottom(edict_t *self)
{
	SG_MoverCompletionPublish(self, SG_MOVER_COMPLETION_BOTTOM);
}

void door_hit_top(edict_t *self)
{
	SG_MoverCompletionPublish(self, SG_MOVER_COMPLETION_TOP);
}

void door_use(edict_t *self, edict_t *other, edict_t *activator)
{
	(void)self; (void)other; (void)activator;
	use_calls++;
}

void trigger_relay_use(edict_t *self, edict_t *other,
	edict_t *activator)
{
	(void)self; (void)other; (void)activator;
	relay_calls++;
}

static void DummyTouch(edict_t *self, edict_t *other, cplane_t *plane,
	csurface_t *surf)
{
	(void)self; (void)other; (void)plane; (void)surf;
}

static void DummyUse(edict_t *self, edict_t *other, edict_t *activator)
{
	(void)self; (void)other; (void)activator;
}

static void DummyThink(edict_t *self)
{
	(void)self;
}

edict_t *G_Find(edict_t *from, int fieldofs, char *match)
{
	edict_t *candidate = from ? from + 1 : g_edicts;

	for (; candidate < &g_edicts[globals.num_edicts]; candidate++)
	{
		char *value;

		if (!candidate->inuse)
			continue;
		value = *(char **)((byte *)candidate + fieldofs);
		if (value && !Q_stricmp(value, match))
			return candidate;
	}
	return NULL;
}

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		        __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static void Set3(vec3_t value, float x, float y, float z)
{
	value[0] = x;
	value[1] = y;
	value[2] = z;
}

static void LinkDoor(edict_t *door)
{
	int axis;

	if (!door || !g_edicts)
		return;
	door->s.number = (int)(door - g_edicts);
	door->area.prev = &g_edicts[0].area;
	door->area.next = &g_edicts[0].area;
	for (axis = 0; axis < 3; axis++)
	{
		door->size[axis] = door->maxs[axis] - door->mins[axis];
		door->absmin[axis] = door->s.origin[axis] + door->mins[axis] - 1.0f;
		door->absmax[axis] = door->s.origin[axis] + door->maxs[axis] + 1.0f;
	}
	door->linkcount++;
}

static void PublishCompletion(edict_t *door,
	sg_mover_completion_kind_t kind)
{
	LinkDoor(door);
	level.current_entity = (door->flags & FL_TEAMSLAVE)
	    ? door->teammaster : door;
	SG_MoverCompletionTransition(door);
	door->moveinfo.endfunc = kind == SG_MOVER_COMPLETION_TOP
	    ? door_hit_top : door_hit_bottom;
	SG_MoverCompletionArm(door);
	SG_MoverCompletionDispatch(door);
}

static void Door(edict_t *door, float bottom, float top)
{
	memset(door, 0, sizeof(*door));
	door->inuse = true;
	door->classname = "func_door";
	door->solid = SOLID_BSP;
	door->movetype = MOVETYPE_PUSH;
	door->use = door_use;
	door->blocked = door_blocked;
	door->teammaster = door;
	door->moveinfo.state = SG_PLAT_STATE_BOTTOM;
	door->moveinfo.speed = 200.0f;
	door->moveinfo.accel = 200.0f;
	door->moveinfo.decel = 200.0f;
	door->moveinfo.wait = 3.0f;
	door->wait = 3.0f;
	door->moveinfo.distance = fabsf(top - bottom);
	Set3(door->mins, -8.0f, -16.0f, -24.0f);
	Set3(door->maxs, 8.0f, 16.0f, 32.0f);
	Set3(door->s.origin, bottom, 0.0f, 0.0f);
	Set3(door->pos1, bottom, 0.0f, 0.0f);
	Set3(door->pos2, top, 0.0f, 0.0f);
	Set3(door->moveinfo.start_origin, bottom, 0.0f, 0.0f);
	Set3(door->moveinfo.end_origin, top, 0.0f, 0.0f);
	Set3(door->movedir, top > bottom ? 1.0f : -1.0f, 0.0f, 0.0f);
	LinkDoor(door);
}

static void Trigger(edict_t *trigger, edict_t *door)
{
	memset(trigger, 0, sizeof(*trigger));
	trigger->inuse = true;
	trigger->classname = "noclass";
	trigger->solid = SOLID_TRIGGER;
	trigger->movetype = MOVETYPE_NONE;
	trigger->touch = Touch_DoorTrigger;
	trigger->owner = door;
	Set3(trigger->absmin, -8.0f, -8.0f, -8.0f);
	Set3(trigger->absmax, 8.0f, 8.0f, 8.0f);
}

static void TriggerAt(edict_t *trigger, edict_t *door, float x)
{
	Trigger(trigger, door);
	trigger->absmin[0] += x;
	trigger->absmax[0] += x;
}

static void World(edict_t ents[8])
{
	memset(ents, 0, sizeof(*ents) * 8U);
	memset(&level, 0, sizeof(level));
	ResetCallbackCalls();
	g_edicts = ents;
	globals.num_edicts = 3;
	ents[0].inuse = true;
	SG_MoverCompletionReset();
	Door(&ents[1], 0.0f, 80.0f);
	Trigger(&ents[2], &ents[1]);
}

static rune_reject_reason_t Resolve(sg_compound_world_preopen_t *resolved)
{
	const vec3_t anchor = { 0.0f, 0.0f, 0.0f };

	return SG_CompoundWorldResolvePreopen(anchor, resolved);
}

static void TestCanonicalAndDelayOnly(void)
{
	edict_t ents[8];
	sg_compound_world_preopen_t resolved;

	World(ents);
	CHECK(Resolve(&resolved) == RLR_OK);
	CHECK(resolved.trigger == &ents[2]);
	CHECK(resolved.trigger_key == 2);
	CHECK(resolved.member == &ents[1] && resolved.mover_key == 1);
	CHECK(resolved.axis == 0 && resolved.speed == 200.0f);
	CHECK(resolved.bottom_origin[0] == 0.0f);
	CHECK(resolved.top_origin[0] == 80.0f);
	CHECK(resolved.inert_effect_delay == 0.0f);
	/* G_SetMovedir(angle=90) may retain a tiny cosine even though world-space
	 * start/end origins round to an exact cardinal move. Move_Calc derives its
	 * real direction from those authoritative origins. */
	Set3(ents[1].s.origin, 1488.0f, 1056.0f, -736.0f);
	Set3(ents[1].pos1, 1488.0f, 1056.0f, -736.0f);
	Set3(ents[1].pos2, 1488.0f, 1136.0f, -736.0f);
	Set3(ents[1].moveinfo.start_origin, 1488.0f, 1056.0f, -736.0f);
	Set3(ents[1].moveinfo.end_origin, 1488.0f, 1136.0f, -736.0f);
	Set3(ents[1].movedir, -4.371139e-8f, 1.0f, 0.0f);
	ents[1].wait = 1.0f;
	ents[1].moveinfo.wait = 1.0f;
	ents[1].delay = 2.0f;
	CHECK(Resolve(&resolved) == RLR_OK);
	CHECK(resolved.axis == 1 && resolved.top_origin[1] == 1136.0f);
	CHECK(resolved.inert_effect_delay == 2.0f);

	ents[1].delay = 5.0f;
	CHECK(SG_CompoundWorldDoorEffectsSafe(&ents[1]));
	CHECK(Resolve(&resolved) == RLR_OK);
	CHECK(resolved.inert_effect_delay == 5.0f);
	ents[1].delay = 0.001f;
	CHECK(SG_CompoundWorldDoorEffectsSafe(&ents[1]));
	ents[1].delay = 5.0001f;
	CHECK(!SG_CompoundWorldDoorEffectsSafe(&ents[1]));
	CHECK(Resolve(&resolved) == RLR_DOOR_TEAM_UNSAFE);
	ents[1].delay = -0.1f;
	CHECK(!SG_CompoundWorldDoorEffectsSafe(&ents[1]));
	ents[1].delay = NAN;
	CHECK(!SG_CompoundWorldDoorEffectsSafe(&ents[1]));
	ents[1].delay = INFINITY;
	CHECK(!SG_CompoundWorldDoorEffectsSafe(&ents[1]));
}

static void TestDelayWithAnythingRejects(void)
{
	edict_t ents[8];
	char empty[] = "";

	World(ents);
	ents[1].delay = 1.0f;
	ents[1].target = "effect";
	CHECK(!SG_CompoundWorldDoorEffectsSafe(&ents[1]));
	ents[1].target = NULL;
	ents[1].message = "effect";
	CHECK(!SG_CompoundWorldDoorEffectsSafe(&ents[1]));
	ents[1].message = NULL;
	ents[1].killtarget = "effect";
	CHECK(!SG_CompoundWorldDoorEffectsSafe(&ents[1]));
	ents[1].killtarget = NULL;
	ents[1].message = empty;
	CHECK(!SG_CompoundWorldDoorEffectsSafe(&ents[1]));
}

static void TestZeroDelayRetainsSoundOnlyPolicy(void)
{
	edict_t ents[8];
	char empty[] = "";

	World(ents);
	ents[1].target = empty;
	CHECK(!SG_CompoundWorldDoorEffectsSafe(&ents[1]));
	ents[1].target = NULL;
	ents[1].killtarget = empty;
	CHECK(!SG_CompoundWorldDoorEffectsSafe(&ents[1]));
	ents[1].killtarget = NULL;
	globals.num_edicts = 4;
	ents[1].target = "door-sound";
	ents[3].inuse = true;
	ents[3].classname = "target_speaker";
	ents[3].targetname = "door-sound";
	ents[3].use = Use_Target_Speaker;
	CHECK(SG_CompoundWorldDoorEffectsSafe(&ents[1]));
	ents[3].use = DummyUse;
	CHECK(!SG_CompoundWorldDoorEffectsSafe(&ents[1]));
	ents[3].use = Use_Target_Speaker;
	ents[3].classname = "target_explosion";
	CHECK(!SG_CompoundWorldDoorEffectsSafe(&ents[1]));

	World(ents);
	globals.num_edicts = 5;
	ents[1].target = "door-relay";
	ents[3].inuse = true;
	ents[3].classname = "trigger_relay";
	ents[3].targetname = "door-relay";
	ents[3].target = "relay-sound";
	ents[3].use = trigger_relay_use;
	ents[4].inuse = true;
	ents[4].classname = "target_speaker";
	ents[4].targetname = "relay-sound";
	ents[4].use = Use_Target_Speaker;
	CHECK(SG_CompoundWorldDoorEffectsSafe(&ents[1]));
	ents[3].killtarget = empty;
	CHECK(!SG_CompoundWorldDoorEffectsSafe(&ents[1]));
}

static void TestExplicitResolutionReasons(void)
{
	edict_t ents[8];
	sg_compound_world_preopen_t resolved;
	vec3_t bad = { NAN, 0.0f, 0.0f };

	World(ents);
	CHECK(SG_CompoundWorldResolvePreopen(bad, &resolved) ==
	      RLR_BAD_MECHANISM_ANCHOR);
	Set3(ents[2].absmin, 100.0f, 100.0f, 100.0f);
	Set3(ents[2].absmax, 110.0f, 110.0f, 110.0f);
	CHECK(Resolve(&resolved) == RLR_MECHANISM_UNRESOLVED);
	Trigger(&ents[2], &ents[1]);
	ents[2].touch = DummyTouch;
	CHECK(Resolve(&resolved) == RLR_UNSUPPORTED_ACTIVATOR);
	Trigger(&ents[2], &ents[1]);
	ents[1].moveinfo.state = SG_PLAT_STATE_TOP;
	CHECK(Resolve(&resolved) == RLR_DOOR_TEAM_UNSAFE);
	CHECK(resolved.member == NULL && resolved.mover_key == -1);
}

static void TestUnsafeDoorClasses(void)
{
	edict_t ents[8];
	sg_compound_world_preopen_t resolved;
	static const int unsafe_flags[] = { 1, 4, 32 };
	int flag;

	World(ents);
	ents[1].moveinfo.accel = 199.0f;
	CHECK(Resolve(&resolved) == RLR_DOOR_TEAM_UNSAFE);
	World(ents);
	ents[1].moveinfo.end_origin[1] = 1.0f;
	ents[1].pos2[1] = 1.0f;
	CHECK(Resolve(&resolved) == RLR_DOOR_TEAM_UNSAFE);
	World(ents);
	ents[1].s.angles[1] = 90.0f;
	ents[1].moveinfo.start_angles[1] = 90.0f;
	ents[1].moveinfo.end_angles[1] = 90.0f;
	CHECK(Resolve(&resolved) == RLR_DOOR_TEAM_UNSAFE);
	for (flag = 0; flag < 3; flag++)
	{
		World(ents);
		ents[1].spawnflags = unsafe_flags[flag];
		CHECK(Resolve(&resolved) == RLR_DOOR_TEAM_UNSAFE);
	}
	World(ents);
	ents[1].targetname = "remote";
	CHECK(Resolve(&resolved) == RLR_DOOR_TEAM_UNSAFE);
	World(ents);
	ents[1].use = DummyUse;
	CHECK(Resolve(&resolved) == RLR_DOOR_TEAM_UNSAFE);
	World(ents);
	ents[1].health = 1;
	CHECK(Resolve(&resolved) == RLR_DOOR_TEAM_UNSAFE);
	World(ents);
	ents[1].moveinfo.wait = -1.0f;
	ents[1].wait = -1.0f;
	CHECK(Resolve(&resolved) == RLR_DOOR_TEAM_UNSAFE);
	World(ents);
	ents[1].teamchain = &ents[3];
	CHECK(Resolve(&resolved) == RLR_DOOR_TEAM_UNSAFE);
}

static void TestMoverSetAmbiguity(void)
{
	edict_t ents[8];
	sg_compound_world_preopen_t resolved;

	World(ents);
	globals.num_edicts = 5;
	Door(&ents[3], 128.0f, 256.0f);
	Trigger(&ents[4], &ents[3]);
	CHECK(Resolve(&resolved) == RLR_MECHANISM_AMBIGUOUS);
	Trigger(&ents[4], &ents[1]);
	CHECK(Resolve(&resolved) == RLR_OK);
	CHECK(resolved.member == &ents[1] && resolved.trigger == &ents[2]);
}

static void CheckCandidateHints(
	const sg_compound_world_candidate_t *candidate)
{
	int hint_index;

	CHECK(candidate->hint_count > 0);
	CHECK(candidate->hint_count <= SG_COMPOUND_WORLD_PREOPEN_HINT_MAX);
	for (hint_index = 0; hint_index < candidate->hint_count; hint_index++)
	{
		sg_compound_world_preopen_t exact;
		int axis;

		for (axis = 0; axis < 3; axis++)
		{
			float scaled = candidate->hints[hint_index][axis] * 8.0f;

			CHECK(isfinite(candidate->hints[hint_index][axis]));
			CHECK(scaled == floorf(scaled));
			if (candidate->hints[hint_index][axis] == 0.0f)
				CHECK(!signbit(candidate->hints[hint_index][axis]));
		}
		CHECK(SG_CompoundWorldOutsideSweep(&candidate->resolved,
		      candidate->hints[hint_index]));
		CHECK(SG_CompoundWorldPreopenHintMatches(&candidate->resolved,
		      candidate->hints[hint_index]));
		CHECK(SG_CompoundWorldResolvePreopen(
		      candidate->hints[hint_index], &exact) == RLR_OK);
		CHECK(exact.trigger_key == candidate->resolved.trigger_key);
		CHECK(exact.mover_key == candidate->resolved.mover_key);
	}
}

static void TestDeterministicEnumeration(void)
{
	edict_t ents[8];
	sg_compound_world_candidate_t candidates[2];
	sg_compound_world_candidate_t untouched[1];
	sg_compound_world_candidate_t before[1];
	int count = -1;

	World(ents);
	globals.num_edicts = 7;
	Door(&ents[3], 256.0f, 336.0f);
	TriggerAt(&ents[4], &ents[3], 400.0f);
	/* A later safe BOTTOM leaf whose complete trigger-contact domain remains
	 * inside its sweep has no PREOPEN hint and must neither count nor write one
	 * element past an exactly sized output. */
	Door(&ents[5], 600.0f, 680.0f);
	TriggerAt(&ents[6], &ents[5], 640.0f);
	CHECK(SG_CompoundWorldEnumeratePreopen(NULL, 0, &count) == RLR_OK);
	CHECK(count == 2);
	memset(untouched, 0xa5, sizeof(untouched));
	memcpy(before, untouched, sizeof(before));
	CHECK(SG_CompoundWorldEnumeratePreopen(untouched, 1, &count) ==
	      RLR_BAD_CONTROL_POLICY);
	CHECK(count == 2);
	CHECK(memcmp(untouched, before, sizeof(untouched)) == 0);
	memset(candidates, 0, sizeof(candidates));
	CHECK(SG_CompoundWorldEnumeratePreopen(candidates, 2, &count) ==
	      RLR_OK);
	CHECK(count == 2);
	CHECK(candidates[0].resolved.trigger_key == 2);
	CHECK(candidates[0].resolved.mover_key == 1);
	CHECK(candidates[1].resolved.trigger_key == 4);
	CHECK(candidates[1].resolved.mover_key == 3);
	CHECK(candidates[0].hints[0][0] == -24.875f);
	CHECK(candidates[1].hints[0][0] == 375.125f);
	CheckCandidateHints(&candidates[0]);
	CheckCandidateHints(&candidates[1]);
	{
		vec3_t noncanonical;

		VectorCopy(candidates[0].hints[0], noncanonical);
		noncanonical[0] += 0.125f;
		CHECK(!SG_CompoundWorldPreopenHintMatches(
		      &candidates[0].resolved, noncanonical));
		VectorCopy(candidates[0].hints[0], noncanonical);
		noncanonical[1] = -0.0f;
		CHECK(!SG_CompoundWorldPreopenHintMatches(
		      &candidates[0].resolved, noncanonical));
	}
	CHECK(CallbackCalls() == 0);
}

static void TestEnumerationExclusionAndDedup(void)
{
	edict_t ents[8];
	sg_compound_world_candidate_t candidate;
	int count = -1;

	World(ents);
	globals.num_edicts = 5;
	Door(&ents[3], 256.0f, 336.0f);
	TriggerAt(&ents[4], &ents[3], 400.0f);
	ents[3].moveinfo.state = SG_PLAT_STATE_TOP;
	Set3(ents[3].s.origin, 336.0f, 0.0f, 0.0f);
	CHECK(SG_CompoundWorldEnumeratePreopen(&candidate, 1, &count) ==
	      RLR_OK);
	CHECK(count == 1);
	CHECK(candidate.resolved.trigger_key == 2);

	/* Two safe automatic triggers owning one physical leaf are not silently
	 * deduplicated by edict order. */
	World(ents);
	globals.num_edicts = 4;
	TriggerAt(&ents[3], &ents[1], 160.0f);
	memset(&candidate, 0x6b, sizeof(candidate));
	CHECK(SG_CompoundWorldEnumeratePreopen(&candidate, 1, &count) ==
	      RLR_MECHANISM_AMBIGUOUS);
	CHECK(count == 0);
	CHECK(CallbackCalls() == 0);

	CHECK(SG_CompoundWorldEnumeratePreopen(NULL, 1, &count) ==
	      RLR_BAD_CONTROL_POLICY);
	CHECK(SG_CompoundWorldEnumeratePreopen(&candidate, -1, &count) ==
	      RLR_BAD_CONTROL_POLICY);
	CHECK(SG_CompoundWorldEnumeratePreopen(&candidate, 1, NULL) ==
	      RLR_BAD_CONTROL_POLICY);
}

static void TestEnumerationClampsRepresentableContactDomain(void)
{
	edict_t ents[8];
	sg_compound_world_candidate_t candidate;
	sg_compound_world_preopen_t exact;
	vec3_t high_contact = { 4095.875f, 0.0f, 0.0f };
	int count = -1;
	int hint_index;
	int saw_high_contact = 0;

	World(ents);
	Door(&ents[1], 4040.0f, 4000.0f);
	Trigger(&ents[2], &ents[1]);
	ents[2].absmin[0] = 3972.0f;
	ents[2].absmax[0] = 4108.0f;
	CHECK(SG_CompoundWorldResolvePreopen(high_contact, &exact) == RLR_OK);
	CHECK(SG_CompoundWorldOutsideSweep(&exact, high_contact));
	CHECK(SG_CompoundWorldEnumeratePreopen(&candidate, 1, &count) ==
	      RLR_OK);
	CHECK(count == 1);
	CheckCandidateHints(&candidate);
	for (hint_index = 0; hint_index < candidate.hint_count; hint_index++)
		if (candidate.hints[hint_index][0] == high_contact[0])
			saw_high_contact = 1;
	CHECK(saw_high_contact);
	CHECK(CallbackCalls() == 0);
}

static void Lmctf01LikeWorld(edict_t ents[8],
	sg_compound_world_preopen_t *resolved)
{
	World(ents);
	Set3(ents[1].s.origin, 1488.0f, 1056.0f, -736.0f);
	Set3(ents[1].pos1, 1488.0f, 1056.0f, -736.0f);
	Set3(ents[1].pos2, 1488.0f, 1136.0f, -736.0f);
	Set3(ents[1].moveinfo.start_origin, 1488.0f, 1056.0f, -736.0f);
	Set3(ents[1].moveinfo.end_origin, 1488.0f, 1136.0f, -736.0f);
	Set3(ents[1].movedir, -4.371139e-8f, 1.0f, 0.0f);
	Set3(ents[1].mins, 0.0f, 0.0f, 0.0f);
	Set3(ents[1].maxs, 32.0f, 96.0f, 128.0f);
	ents[1].wait = 1.0f;
	ents[1].moveinfo.wait = 1.0f;
	ents[1].delay = 2.0f;
	CHECK(Resolve(resolved) == RLR_OK);
	CHECK(resolved->axis == 1 && resolved->speed == 200.0f);
	CHECK(resolved->wait == 1.0f &&
	      resolved->inert_effect_delay == 2.0f);
}

static void PutAtTop(edict_t *door)
{
	Set3(door->s.origin, door->moveinfo.end_origin[0],
	     door->moveinfo.end_origin[1], door->moveinfo.end_origin[2]);
	door->moveinfo.state = SG_PLAT_STATE_TOP;
	door->moveinfo.endfunc = door_hit_top;
	door->think = door_go_down;
	door->nextthink = level.time + door->moveinfo.wait;
	PublishCompletion(door, SG_MOVER_COMPLETION_TOP);
}

static void TestExactSweepGeometry(void)
{
	edict_t ents[8];
	sg_compound_world_preopen_t resolved;
	vec3_t inside = { 1504.0f, 1144.0f, -676.0f };
	vec3_t low_x = { 1471.0f, 1144.0f, -676.0f };
	vec3_t high_x = { 1537.0f, 1144.0f, -676.0f };
	vec3_t low_y = { 1504.0f, 1039.0f, -676.0f };
	vec3_t high_y = { 1504.0f, 1249.0f, -676.0f };
	vec3_t low_z = { 1504.0f, 1144.0f, -769.0f };
	vec3_t high_z = { 1504.0f, 1144.0f, -583.0f };
	vec3_t boundary = { 1472.0f, 1040.0f, -768.0f };
	vec3_t high_boundary = { 1536.0f, 1248.0f, -584.0f };
	vec3_t residual_boundary = { 1536.0f, 1248.125f, -584.0f };
	vec3_t cross_from = { 1504.0f, 1000.0f, -676.0f };
	vec3_t cross_to = { 1504.0f, 1300.0f, -676.0f };
	vec3_t tangent_from = { 1472.0f, 1000.0f, -676.0f };
	vec3_t tangent_to = { 1472.0f, 1300.0f, -676.0f };
	vec3_t miss_from = { 1471.0f, 1000.0f, -676.0f };
	vec3_t miss_to = { 1471.0f, 1300.0f, -676.0f };
	vec3_t invalid = { NAN, 0.0f, 0.0f };

	Lmctf01LikeWorld(ents, &resolved);
	/* Bottom (1488,1056,-736)..(1520,1152,-608), TOP shifted
	 * +80 Y, then exact player hull expansion gives:
	 * (1472,1040,-768)..(1536,1248,-584). */
	CHECK(!SG_CompoundWorldOutsideSweep(&resolved, inside));
	CHECK(!SG_CompoundWorldOutsideSweep(&resolved, boundary));
	CHECK(!SG_CompoundWorldOutsideSweep(&resolved, high_boundary));
	CHECK(SG_CompoundWorldOutsideSweep(&resolved, low_x));
	CHECK(SG_CompoundWorldOutsideSweep(&resolved, high_x));
	CHECK(SG_CompoundWorldOutsideSweep(&resolved, low_y));
	CHECK(SG_CompoundWorldOutsideSweep(&resolved, high_y));
	CHECK(SG_CompoundWorldOutsideSweep(&resolved, low_z));
	CHECK(SG_CompoundWorldOutsideSweep(&resolved, high_z));
	CHECK(!SG_CompoundWorldOutsideSweep(&resolved, invalid));
	CHECK(!SG_CompoundWorldOutsideSweep(NULL, low_x));
	CHECK(!SG_CompoundWorldOutsideSweep(&resolved, NULL));
	/* Stock pusher quantization may finish just beyond the nominal endpoint.
	 * The complete live sweep includes the authenticated current brush pose. */
	ents[1].s.origin[1] = ents[1].moveinfo.end_origin[1] + 0.125f;
	CHECK(!SG_CompoundWorldOutsideSweep(&resolved, residual_boundary));
	/* The pusher moves before RunThink.  Fence the one quantized displacement
	 * that can occur in the next entity pass, including a late final timer. */
	ents[1].s.origin[1] = ents[1].moveinfo.end_origin[1];
	ents[1].velocity[1] = ents[1].moveinfo.speed;
	CHECK(!SG_CompoundWorldOutsideSweep(&resolved, high_y));
	ents[1].velocity[1] = 0.0f;
	ents[1].s.origin[1] = ents[1].moveinfo.start_origin[1];

	CHECK(SG_CompoundWorldCrossesSweep(&resolved, cross_from, cross_to));
	CHECK(SG_CompoundWorldCrossesSweep(&resolved, cross_to, cross_from));
	CHECK(SG_CompoundWorldCrossesSweep(&resolved,
	      tangent_from, tangent_to));
	CHECK(!SG_CompoundWorldCrossesSweep(&resolved, miss_from, miss_to));
	CHECK(SG_CompoundWorldCrossesSweep(&resolved, inside, inside));
	CHECK(!SG_CompoundWorldCrossesSweep(&resolved, low_x, low_x));
	CHECK(!SG_CompoundWorldCrossesSweep(&resolved, invalid, cross_to));
	CHECK(!SG_CompoundWorldCrossesSweep(NULL, cross_from, cross_to));
	CHECK(!SG_CompoundWorldCrossesSweep(&resolved, NULL, cross_to));
	CHECK(CallbackCalls() == 0);
}

static void TestSweepRejectsStaleIdentity(void)
{
	edict_t ents[8];
	sg_compound_world_preopen_t resolved;
	sg_compound_world_preopen_t stale;
	vec3_t outside = { 1400.0f, 1144.0f, -676.0f };
	vec3_t from = { 1504.0f, 1000.0f, -676.0f };
	vec3_t to = { 1504.0f, 1300.0f, -676.0f };

	Lmctf01LikeWorld(ents, &resolved);
	CHECK(SG_CompoundWorldOutsideSweep(&resolved, outside));
	CHECK(SG_CompoundWorldCrossesSweep(&resolved, from, to));

	stale = resolved;
	stale.member = &ents[3];
	CHECK(!SG_CompoundWorldOutsideSweep(&stale, outside));
	CHECK(!SG_CompoundWorldCrossesSweep(&stale, from, to));
	stale = resolved;
	stale.mover_key = 2;
	CHECK(!SG_CompoundWorldOutsideSweep(&stale, outside));
	CHECK(!SG_CompoundWorldCrossesSweep(&stale, from, to));
	stale = resolved;
	stale.trigger = &ents[1];
	CHECK(!SG_CompoundWorldOutsideSweep(&stale, outside));
	stale = resolved;
	stale.trigger_key = 1;
	CHECK(!SG_CompoundWorldCrossesSweep(&stale, from, to));

	ents[1].mins[0] = -1.0f;
	CHECK(!SG_CompoundWorldOutsideSweep(&resolved, outside));
	CHECK(!SG_CompoundWorldCrossesSweep(&resolved, from, to));
	ents[1].mins[0] = 0.0f;
	ents[1].moveinfo.speed = 201.0f;
	ents[1].moveinfo.accel = 201.0f;
	ents[1].moveinfo.decel = 201.0f;
	CHECK(!SG_CompoundWorldOutsideSweep(&resolved, outside));
	CHECK(!SG_CompoundWorldCrossesSweep(&resolved, from, to));
	CHECK(CallbackCalls() == 0);
}

static void TestExactTopWindow(void)
{
	edict_t ents[8];
	sg_compound_world_preopen_t resolved;
	float exact_boundary;
	float think_boundary;

	Lmctf01LikeWorld(ents, &resolved);
	level.time = 10.0f;
	PutAtTop(&ents[1]);
	CHECK(SG_CompoundWorldAtTopFor(&resolved, 0));
	CHECK(SG_CompoundWorldAtTopFor(&resolved, 500));
	CHECK(SG_CompoundWorldAtTopFor(&resolved, 800));
	CHECK(!SG_CompoundWorldAtTopFor(&resolved, 900));
	CHECK(!SG_CompoundWorldAtTopFor(&resolved, -100));
	CHECK(!SG_CompoundWorldAtTopFor(&resolved, 1));
	CHECK(!SG_CompoundWorldAtTopFor(NULL, 500));

	exact_boundary = level.time + 0.5f + FRAMETIME;
	ents[1].nextthink = exact_boundary;
	CHECK(!SG_CompoundWorldAtTopFor(&resolved, 500));
	think_boundary = exact_boundary + 0.001f;
	ents[1].nextthink = think_boundary;
	CHECK(!SG_CompoundWorldAtTopFor(&resolved, 500));
	ents[1].nextthink = nextafterf(think_boundary, INFINITY);
	CHECK(SG_CompoundWorldAtTopFor(&resolved, 500));
	CHECK(CallbackCalls() == 0);
}

static void TestHoldRenewalAndNoShorten(void)
{
	edict_t ents[8];
	sg_compound_world_preopen_t resolved;
	float expected;

	Lmctf01LikeWorld(ents, &resolved);
	level.time = 10.0f;
	PutAtTop(&ents[1]);
	ents[1].nextthink = 10.2f;
	expected = level.time + 0.5f;
	CHECK(SG_CompoundWorldHoldOpen(&resolved,
	      SG_COMPOUND_HOLD_LEASE_MS));
	CHECK(ents[1].nextthink == expected);

	level.time = 10.25f;
	expected = level.time + 0.5f;
	CHECK(SG_CompoundWorldHoldOpen(&resolved,
	      SG_COMPOUND_HOLD_LEASE_MS));
	CHECK(ents[1].nextthink == expected);

	ents[1].nextthink = 12.0f;
	CHECK(SG_CompoundWorldHoldOpen(&resolved,
	      SG_COMPOUND_HOLD_LEASE_MS));
	CHECK(ents[1].nextthink == 12.0f);
	CHECK(!SG_CompoundWorldHoldOpen(&resolved, 400));
	CHECK(!SG_CompoundWorldHoldOpen(NULL,
	      SG_COMPOUND_HOLD_LEASE_MS));
	CHECK(ents[1].nextthink == 12.0f);
	CHECK(CallbackCalls() == 0);
}

static void TestMemberTopHold(void)
{
	edict_t ents[8];
	edict_t *door;
	float before;

	World(ents);
	door = &ents[1];
	level.time = 30.0f;
	PutAtTop(door);
	/* The stock TOP completion witness authenticates the motion-axis pose. */
	door->nextthink = 30.1f;
	CHECK(SG_CompoundWorldHoldMember(door,
	      SG_COMPOUND_HOLD_LEASE_MS));
	CHECK(door->nextthink == 30.5f);
	/* Beginning a transition consumes old TOP authority even if mutable edict
	 * fields are restored to the exact same bits before a new callback. */
	SG_MoverCompletionTransition(door);
	CHECK(!SG_CompoundWorldHoldMember(door,
	      SG_COMPOUND_HOLD_LEASE_MS));
	PublishCompletion(door, SG_MOVER_COMPLETION_TOP);
	door->s.origin[0] += 100000.0f;
	LinkDoor(door);
	CHECK(!SG_CompoundWorldHoldMember(door,
	      SG_COMPOUND_HOLD_LEASE_MS));
	door->s.origin[0] = door->moveinfo.end_origin[0];
	PublishCompletion(door, SG_MOVER_COMPLETION_TOP);

	/* Renewal extends a short schedule but never shortens a later close. */
	door->nextthink = 32.0f;
	CHECK(SG_CompoundWorldHoldMember(door,
	      SG_COMPOUND_HOLD_LEASE_MS));
	CHECK(door->nextthink == 32.0f);
	before = door->nextthink;
	CHECK(!SG_CompoundWorldHoldMember(door, 400));
	CHECK(!SG_CompoundWorldHoldMember(NULL,
	      SG_COMPOUND_HOLD_LEASE_MS));
	CHECK(door->nextthink == before);

	/* Endpoint callback identity and physical/static shape are revalidated.
	 * The motion-axis coordinate is authenticated by the stock completion
	 * callback, not by a nominal endpoint: timer-lattice delay can add pushes. */
	door->moveinfo.endfunc = door_hit_bottom;
	CHECK(!SG_CompoundWorldHoldMember(door,
	      SG_COMPOUND_HOLD_LEASE_MS));
	door->moveinfo.endfunc = door_hit_top;
	door->s.origin[0] = nextafterf(door->moveinfo.end_origin[0], INFINITY);
	CHECK(!SG_CompoundWorldHoldMember(door,
	      SG_COMPOUND_HOLD_LEASE_MS));
	PublishCompletion(door, SG_MOVER_COMPLETION_TOP);
	CHECK(SG_CompoundWorldHoldMember(door,
	      SG_COMPOUND_HOLD_LEASE_MS));
	door->s.origin[0] = door->moveinfo.end_origin[0];
	PublishCompletion(door, SG_MOVER_COMPLETION_TOP);
	door->s.origin[1] = 0.125f;
	CHECK(!SG_CompoundWorldHoldMember(door,
	      SG_COMPOUND_HOLD_LEASE_MS));
	door->s.origin[1] = door->moveinfo.end_origin[1];
	door->velocity[0] = 0.001f;
	CHECK(!SG_CompoundWorldHoldMember(door,
	      SG_COMPOUND_HOLD_LEASE_MS));
	door->velocity[0] = 0.0f;
	door->moveinfo.accel = 199.0f;
	CHECK(!SG_CompoundWorldHoldMember(door,
	      SG_COMPOUND_HOLD_LEASE_MS));
	door->moveinfo.accel = door->moveinfo.speed;
	door->nextthink = level.time;
	CHECK(!SG_CompoundWorldHoldMember(door,
	      SG_COMPOUND_HOLD_LEASE_MS));
	CHECK(door->nextthink == level.time);
	CHECK(CallbackCalls() == 0);
}

static void TestMemberTerminalWitnesses(void)
{
	edict_t ents[8];
	edict_t *door;

	World(ents);
	door = &ents[1];
	/* A newly spawned, untouched BOTTOM has no completion callback yet. */
	CHECK(door->moveinfo.endfunc == NULL);
	CHECK(SG_CompoundWorldMemberTerminal(door));
	SG_MoverCompletionTransition(door);
	CHECK(!SG_CompoundWorldMemberTerminal(door));
	SG_MoverCompletionForget(door);
	CHECK(SG_CompoundWorldMemberTerminal(door));
	door->s.origin[0] = 0.125f;
	CHECK(!SG_CompoundWorldMemberTerminal(door));

	/* A completed BOTTOM uses the stock direction-specific callback as its
	 * durable motion-axis witness; timer-lattice delay can add full pushes. */
	door->moveinfo.endfunc = door_hit_bottom;
	door->s.origin[0] = 0.0f;
	/* Merely invoking the endpoint callback (or dispatching it without an
	 * arm from Move_Calc/AngleMove_Calc) must not mint causal authority. */
	level.current_entity = door;
	door_hit_bottom(door);
	CHECK(!SG_CompoundWorldMemberTerminal(door));
	SG_MoverCompletionDispatch(door);
	CHECK(!SG_CompoundWorldMemberTerminal(door));
	level.current_entity = &ents[2];
	SG_MoverCompletionPublish(door, SG_MOVER_COMPLETION_BOTTOM);
	CHECK(!SG_CompoundWorldMemberTerminal(door));
	PublishCompletion(door, SG_MOVER_COMPLETION_BOTTOM);
	CHECK(SG_CompoundWorldMemberTerminal(door));
	SG_MoverCompletionTransition(door);
	CHECK(!SG_CompoundWorldMemberTerminal(door));
	PublishCompletion(door, SG_MOVER_COMPLETION_BOTTOM);
	SG_MoverCompletionForget(door);
	CHECK(!SG_CompoundWorldMemberTerminal(door));
	PublishCompletion(door, SG_MOVER_COMPLETION_BOTTOM);
	SG_MoverCompletionReset();
	CHECK(!SG_CompoundWorldMemberTerminal(door));
	PublishCompletion(door, SG_MOVER_COMPLETION_BOTTOM);
	door->s.origin[0] = 100000.0f;
	LinkDoor(door);
	CHECK(!SG_CompoundWorldMemberTerminal(door));
	door->s.origin[0] = 0.0f;
	PublishCompletion(door, SG_MOVER_COMPLETION_BOTTOM);
	door->s.origin[0] = nextafterf(0.0f, INFINITY);
	door->s.origin[1] = 0.125f;
	CHECK(!SG_CompoundWorldMemberTerminal(door));
	door->s.origin[1] = 0.0f;

	/* Wrong callbacks and live/malformed motion state fail closed. */
	door->moveinfo.endfunc = door_hit_top;
	CHECK(!SG_CompoundWorldMemberTerminal(door));
	door->moveinfo.endfunc = DummyThink;
	CHECK(!SG_CompoundWorldMemberTerminal(door));
	door->moveinfo.endfunc = door_hit_bottom;
	door->nextthink = FRAMETIME;
	CHECK(!SG_CompoundWorldMemberTerminal(door));
	door->nextthink = 0.0f;
	door->velocity[0] = 0.001f;
	CHECK(!SG_CompoundWorldMemberTerminal(door));
	door->velocity[0] = 0.0f;
	door->avelocity[2] = 0.001f;
	CHECK(!SG_CompoundWorldMemberTerminal(door));
	door->avelocity[2] = 0.0f;
	door->moveinfo.state = SG_PLAT_STATE_TOP;
	CHECK(!SG_CompoundWorldMemberTerminal(door));
	door->moveinfo.state = SG_PLAT_STATE_BOTTOM;
	door->moveinfo.decel = 199.0f;
	CHECK(!SG_CompoundWorldMemberTerminal(door));
	door->moveinfo.decel = door->moveinfo.speed;
	door->solid = SOLID_NOT;
	CHECK(!SG_CompoundWorldMemberTerminal(door));
	CHECK(!SG_CompoundWorldMemberTerminal(NULL));
	CHECK(CallbackCalls() == 0);
}

static void TestAccumulatedPusherResidual(void)
{
	edict_t ents[8];
	edict_t *door;

	World(ents);
	door = &ents[1];
	/* Every pusher delta is rounded separately.  Stock completion witnesses
	 * accept the resulting motion-axis residual while preserving exact
	 * orthogonal pose and state/callback identity. */
	door->moveinfo.speed = 74.4f;
	door->moveinfo.accel = 74.4f;
	door->moveinfo.decel = 74.4f;
	door->moveinfo.endfunc = door_hit_bottom;
	door->s.origin[0] = -0.625f;
	CHECK(!SG_CompoundWorldMemberTerminal(door));
	PublishCompletion(door, SG_MOVER_COMPLETION_BOTTOM);
	CHECK(SG_CompoundWorldMemberTerminal(door));

	PutAtTop(door);
	door->s.origin[0] = 80.625f;
	PublishCompletion(door, SG_MOVER_COMPLETION_TOP);
	CHECK(SG_CompoundWorldHoldMember(door,
	      SG_COMPOUND_HOLD_LEASE_MS));
	door->s.origin[0] = nextafterf(80.625f, INFINITY);
	PublishCompletion(door, SG_MOVER_COMPLETION_TOP);
	CHECK(SG_CompoundWorldHoldMember(door,
	      SG_COMPOUND_HOLD_LEASE_MS));

	/* Closing starts at the quantized TOP.  For this small, slow door that
	 * nearly doubles the reverse travel and produces a very asymmetric
	 * canonical BOTTOM. */
	Door(door, 0.0f, 1.0f);
	door->moveinfo.speed = 0.63f;
	door->moveinfo.accel = 0.63f;
	door->moveinfo.decel = 0.63f;
	door->moveinfo.endfunc = door_hit_bottom;
	door->s.origin[0] = -1.75f;
	PublishCompletion(door, SG_MOVER_COMPLETION_BOTTOM);
	CHECK(SG_CompoundWorldMemberTerminal(door));
	door->s.origin[0] = nextafterf(-1.75f, -INFINITY);
	PublishCompletion(door, SG_MOVER_COMPLETION_BOTTOM);
	CHECK(SG_CompoundWorldMemberTerminal(door));
	PutAtTop(door);
	door->s.origin[0] = 1.875f;
	PublishCompletion(door, SG_MOVER_COMPLETION_TOP);
	CHECK(SG_CompoundWorldHoldMember(door,
	      SG_COMPOUND_HOLD_LEASE_MS));

	/* Mirror the same engine arithmetic in the negative direction. */
	Door(door, 0.0f, -1.0f);
	door->moveinfo.speed = 0.63f;
	door->moveinfo.accel = 0.63f;
	door->moveinfo.decel = 0.63f;
	door->moveinfo.endfunc = door_hit_bottom;
	door->s.origin[0] = 1.75f;
	PublishCompletion(door, SG_MOVER_COMPLETION_BOTTOM);
	CHECK(SG_CompoundWorldMemberTerminal(door));
	PutAtTop(door);
	door->s.origin[0] = -1.875f;
	PublishCompletion(door, SG_MOVER_COMPLETION_TOP);
	CHECK(SG_CompoundWorldHoldMember(door,
	      SG_COMPOUND_HOLD_LEASE_MS));

	/* Move_Begin can leave a finite negative residual when its float frame
	 * product rounds just past the normalized distance.  Stock Move_Final
	 * consumes that signed value; it does not clamp or reject it. */
	Door(door, -3200.0f, 3195.25f);
	door->moveinfo.speed = 1776.354248046875f;
	door->moveinfo.accel = door->moveinfo.speed;
	door->moveinfo.decel = door->moveinfo.speed;
	door->moveinfo.endfunc = door_hit_bottom;
	door->s.origin[0] = -3199.625f;
	PublishCompletion(door, SG_MOVER_COMPLETION_BOTTOM);
	CHECK(SG_CompoundWorldMemberTerminal(door));
	PutAtTop(door);
	door->s.origin[0] = 3194.875f;
	PublishCompletion(door, SG_MOVER_COMPLETION_TOP);
	CHECK(SG_CompoundWorldHoldMember(door,
	      SG_COMPOUND_HOLD_LEASE_MS));

	Door(door, 3200.0f, -3195.25f);
	door->moveinfo.speed = 1776.354248046875f;
	door->moveinfo.accel = door->moveinfo.speed;
	door->moveinfo.decel = door->moveinfo.speed;
	door->moveinfo.endfunc = door_hit_bottom;
	door->s.origin[0] = 3199.625f;
	PublishCompletion(door, SG_MOVER_COMPLETION_BOTTOM);
	CHECK(SG_CompoundWorldMemberTerminal(door));
	PutAtTop(door);
	door->s.origin[0] = -3194.875f;
	PublishCompletion(door, SG_MOVER_COMPLETION_TOP);
	CHECK(SG_CompoundWorldHoldMember(door,
	      SG_COMPOUND_HOLD_LEASE_MS));

	/* The pusher's float-to-int quantizer must reject an exact 2^31 scaled
	 * displacement before the C integer conversion.  (float)INT_MAX itself
	 * rounds to 2^31, so a float-only upper-bound comparison is insufficient. */
	Door(door, 0.0f, 536870912.0f);
	door->moveinfo.speed = 2684354560.0f;
	door->moveinfo.accel = door->moveinfo.speed;
	door->moveinfo.decel = door->moveinfo.speed;
	door->moveinfo.endfunc = door_hit_bottom;
	door->s.origin[0] = door->moveinfo.start_origin[0];
	CHECK(!SG_CompoundWorldMemberTerminal(door));
	PutAtTop(door);
	CHECK(!SG_CompoundWorldHoldMember(door,
	      SG_COMPOUND_HOLD_LEASE_MS));
	CHECK(CallbackCalls() == 0);
}

static void TestTopAndHoldRejectStaleState(void)
{
	edict_t ents[8];
	sg_compound_world_preopen_t resolved;
	sg_compound_world_preopen_t stale;
	float before;

	Lmctf01LikeWorld(ents, &resolved);
	level.time = 20.0f;
	PutAtTop(&ents[1]);
	CHECK(SG_CompoundWorldAtTopFor(&resolved, 500));

	stale = resolved;
	stale.member = NULL;
	CHECK(!SG_CompoundWorldAtTopFor(&stale, 500));
	CHECK(!SG_CompoundWorldHoldOpen(&stale,
	      SG_COMPOUND_HOLD_LEASE_MS));
	stale = resolved;
	stale.mover_key = 7;
	CHECK(!SG_CompoundWorldAtTopFor(&stale, 500));
	CHECK(!SG_CompoundWorldHoldOpen(&stale,
	      SG_COMPOUND_HOLD_LEASE_MS));

	ents[1].moveinfo.state = SG_PLAT_STATE_BOTTOM;
	before = ents[1].nextthink;
	CHECK(!SG_CompoundWorldAtTopFor(&resolved, 500));
	CHECK(!SG_CompoundWorldHoldOpen(&resolved,
	      SG_COMPOUND_HOLD_LEASE_MS));
	CHECK(ents[1].nextthink == before);
	ents[1].moveinfo.state = SG_PLAT_STATE_TOP;

	ents[1].s.origin[1] = nextafterf(
	    ents[1].moveinfo.end_origin[1], -INFINITY);
	PublishCompletion(&ents[1], SG_MOVER_COMPLETION_TOP);
	CHECK(SG_CompoundWorldAtTopFor(&resolved, 500));
	CHECK(SG_CompoundWorldHoldOpen(&resolved,
	      SG_COMPOUND_HOLD_LEASE_MS));
	ents[1].s.origin[1] = ents[1].moveinfo.end_origin[1];
	PublishCompletion(&ents[1], SG_MOVER_COMPLETION_TOP);
	ents[1].s.origin[0] = nextafterf(
	    ents[1].moveinfo.end_origin[0], -INFINITY);
	CHECK(!SG_CompoundWorldAtTopFor(&resolved, 500));
	CHECK(!SG_CompoundWorldHoldOpen(&resolved,
	      SG_COMPOUND_HOLD_LEASE_MS));
	ents[1].s.origin[0] = ents[1].moveinfo.end_origin[0];
	ents[1].moveinfo.endfunc = door_hit_bottom;
	CHECK(!SG_CompoundWorldAtTopFor(&resolved, 500));
	CHECK(!SG_CompoundWorldHoldOpen(&resolved,
	      SG_COMPOUND_HOLD_LEASE_MS));
	ents[1].moveinfo.endfunc = door_hit_top;
	ents[1].solid = SOLID_NOT;
	CHECK(!SG_CompoundWorldAtTopFor(&resolved, 500));
	CHECK(!SG_CompoundWorldHoldOpen(&resolved,
	      SG_COMPOUND_HOLD_LEASE_MS));
	ents[1].solid = SOLID_BSP;

	ents[1].think = DummyThink;
	CHECK(!SG_CompoundWorldAtTopFor(&resolved, 500));
	CHECK(!SG_CompoundWorldHoldOpen(&resolved,
	      SG_COMPOUND_HOLD_LEASE_MS));
	ents[1].think = door_go_down;
	ents[1].nextthink = level.time;
	CHECK(!SG_CompoundWorldAtTopFor(&resolved, 0));
	CHECK(!SG_CompoundWorldHoldOpen(&resolved,
	      SG_COMPOUND_HOLD_LEASE_MS));
	ents[1].nextthink = NAN;
	CHECK(!SG_CompoundWorldAtTopFor(&resolved, 0));
	CHECK(!SG_CompoundWorldHoldOpen(&resolved,
	      SG_COMPOUND_HOLD_LEASE_MS));
	CHECK(CallbackCalls() == 0);
}

int main(void)
{
	TestCanonicalAndDelayOnly();
	TestDelayWithAnythingRejects();
	TestZeroDelayRetainsSoundOnlyPolicy();
	TestExplicitResolutionReasons();
	TestUnsafeDoorClasses();
	TestMoverSetAmbiguity();
	TestDeterministicEnumeration();
	TestEnumerationExclusionAndDedup();
	TestEnumerationClampsRepresentableContactDomain();
	TestExactSweepGeometry();
	TestSweepRejectsStaleIdentity();
	TestExactTopWindow();
	TestHoldRenewalAndNoShorten();
	TestMemberTopHold();
	TestMemberTerminalWitnesses();
	TestAccumulatedPusherResidual();
	TestTopAndHoldRejectStaleState();
	if (failures)
	{
		fprintf(stderr, "sg_compound_world_test: %d failure(s)\n",
		        failures);
		return 1;
	}
	puts("sg_compound_world_test: ok");
	return 0;
}
