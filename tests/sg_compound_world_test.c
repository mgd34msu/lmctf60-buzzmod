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

static void World(edict_t ents[8])
{
	memset(ents, 0, sizeof(*ents) * 8U);
	memset(&level, 0, sizeof(level));
	ResetCallbackCalls();
	g_edicts = ents;
	globals.num_edicts = 3;
	ents[0].inuse = true;
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
	door->think = door_go_down;
	door->nextthink = level.time + door->moveinfo.wait;
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

	ents[1].s.origin[1] -= 0.125f;
	CHECK(!SG_CompoundWorldAtTopFor(&resolved, 500));
	CHECK(!SG_CompoundWorldHoldOpen(&resolved,
	      SG_COMPOUND_HOLD_LEASE_MS));
	ents[1].s.origin[1] += 0.125f;
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
	TestExactSweepGeometry();
	TestSweepRejectsStaleIdentity();
	TestExactTopWindow();
	TestHoldRenewalAndNoShorten();
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
