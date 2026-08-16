/* Focused geometry contract for phase-independent func_rotating exclusion. */
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "g_local.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_hooks.h"

game_export_t globals;
game_locals_t game;
edict_t *g_edicts;
sg_host_t sg_host;

static edict_t *test_trigger_hits[MAX_EDICTS];
static edict_t *test_solid_hits[MAX_EDICTS];
static int test_trigger_count;
static int test_solid_count;

static int TestBoxEdicts(const vec3_t mins, const vec3_t maxs,
	edict_t **list, int maxcount, int areatype)
{
	edict_t **source = areatype == AREA_TRIGGERS ? test_trigger_hits :
	                                                   test_solid_hits;
	int count = areatype == AREA_TRIGGERS ? test_trigger_count :
	                                             test_solid_count;
	int i;

	(void)mins;
	(void)maxs;
	if (!list || maxcount < count)
		return -1;
	for (i = 0; i < count; i++)
		list[i] = source[i];
	return count;
}

qboolean SG_ImmutableSupport(const edict_t *ent)
{
	return ent && ent->classname &&
	       strcmp(ent->classname, "immutable-test-support") == 0;
}

void Touch_DoorTrigger(edict_t *self, edict_t *other, cplane_t *plane,
	csurface_t *surf)
{
	(void)self; (void)other; (void)plane; (void)surf;
}

void Touch_Multi(edict_t *self, edict_t *other, cplane_t *plane,
	csurface_t *surf)
{
	(void)self; (void)other; (void)plane; (void)surf;
}

void Touch_Item(edict_t *ent, edict_t *other, cplane_t *plane,
	csurface_t *surf)
{
	(void)ent; (void)other; (void)plane; (void)surf;
}

void door_use(edict_t *self, edict_t *other, edict_t *activator)
{
	(void)self; (void)other; (void)activator;
}

void door_secret_use(edict_t *self, edict_t *other, edict_t *activator)
{
	(void)self; (void)other; (void)activator;
}

void Use_Target_Speaker(edict_t *self, edict_t *other,
	edict_t *activator)
{
	(void)self; (void)other; (void)activator;
}

void trigger_relay_use(edict_t *self, edict_t *other,
	edict_t *activator)
{
	(void)self; (void)other; (void)activator;
}

static void DummyTouch(edict_t *self, edict_t *other, cplane_t *plane,
	csurface_t *surf)
{
	(void)self; (void)other; (void)plane; (void)surf;
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

static void Rotator(edict_t *ent, int flags, float pitch, float yaw,
	float roll, const vec3_t mins, const vec3_t maxs)
{
	memset(ent, 0, sizeof(*ent));
	ent->classname = "func_rotating";
	ent->solid = SOLID_BSP;
	ent->spawnflags = flags;
	Set3(ent->s.angles, pitch, yaw, roll);
	VectorCopy(mins, ent->mins);
	VectorCopy(maxs, ent->maxs);
}

static qboolean Blocks(const vec3_t start, const vec3_t mins,
	const vec3_t maxs, const vec3_t end, int mask)
{
	return SG_OracleRotatorSweepBlocks(start, mins, maxs, end, mask);
}

static void TestReplayTriggerClassifier(void)
{
	edict_t ents[10];
	edict_t *world_ent = &ents[0], *door = &ents[1];
	edict_t *door_trigger = &ents[2], *sound_trigger = &ents[3];
	edict_t *speaker = &ents[4], *arbitrary = &ents[5];
	edict_t *source = &ents[6], *player = &ents[7];
	edict_t *support = &ents[8], *item = &ents[9];
	vec3_t from, to;
	qboolean contaminated, door_passed;

	memset(ents, 0, sizeof(ents));
	g_edicts = ents;
	globals.num_edicts = 10;
	world_ent->inuse = true;

	door->inuse = true;
	door->classname = "func_door";
	door->use = door_use;
	door->moveinfo.distance = 64.0f;
	door->moveinfo.speed = 100.0f;
	door->moveinfo.accel = 100.0f;
	door->moveinfo.decel = 100.0f;
	door->moveinfo.wait = 1.0f;
	Set3(door->mins, -8.0f, -8.0f, -8.0f);
	Set3(door->maxs, 8.0f, 8.0f, 8.0f);
	Set3(door->absmin, -8.0f, -8.0f, -8.0f);
	Set3(door->absmax, 8.0f, 8.0f, 8.0f);
	door_trigger->inuse = true;
	door_trigger->solid = SOLID_TRIGGER;
	door_trigger->movetype = MOVETYPE_NONE;
	door_trigger->touch = Touch_DoorTrigger;
	door_trigger->owner = door;
	CHECK(SG_OracleReplayTriggerEvents(door_trigger, &contaminated,
	                                  &door_passed));
	CHECK(!contaminated && !door_passed);
	Set3(from, 100.0f, 100.0f, 0.0f);
	Set3(to, 120.0f, 100.0f, 0.0f);
	CHECK(!SG_OracleReplayDoorPassage(from, to));
	Set3(from, -100.0f, 0.0f, 0.0f);
	Set3(to, 100.0f, 0.0f, 0.0f);
	CHECK(SG_OracleReplayDoorPassage(from, to));
	/* Each actual leg clears the sweep although their combined chord crosses
	 * it.  Live command-four sampling advances the stored origin after the
	 * first leg, so Boundary tests only the second (pusher) leg. */
	Set3(from, -100.0f, 100.0f, 0.0f);
	Set3(to, 100.0f, 100.0f, 0.0f);
	CHECK(!SG_OracleReplayDoorPassage(from, to));
	Set3(from, 100.0f, 100.0f, 0.0f);
	Set3(to, 100.0f, -100.0f, 0.0f);
	CHECK(!SG_OracleReplayDoorPassage(from, to));
	Set3(from, -100.0f, 100.0f, 0.0f);
	CHECK(SG_OracleReplayDoorPassage(from, to));

	sound_trigger->inuse = true;
	sound_trigger->solid = SOLID_TRIGGER;
	sound_trigger->classname = "trigger_multiple";
	sound_trigger->touch = Touch_Multi;
	sound_trigger->target = "sound-only";
	sound_trigger->wait = 0.2f;
	speaker->inuse = true;
	speaker->classname = "target_speaker";
	speaker->targetname = "sound-only";
	speaker->use = Use_Target_Speaker;
	CHECK(SG_OracleReplayTriggerEvents(sound_trigger, &contaminated,
	                                  &door_passed));
	CHECK(!contaminated && !door_passed);

	sound_trigger->touch = Touch_Item;
	CHECK(SG_OracleReplayTriggerEvents(sound_trigger, &contaminated,
	                                  &door_passed));
	CHECK(!contaminated && !door_passed);

	arbitrary->inuse = true;
	arbitrary->solid = SOLID_TRIGGER;
	arbitrary->touch = DummyTouch;
	CHECK(SG_OracleReplayTriggerEvents(arbitrary, &contaminated,
	                                  &door_passed));
	CHECK(contaminated && !door_passed);

	/* The Begin source observer consumes only BoxEdicts snapshots.  Allowed
	 * item, sound-only and safe-door triggers plus world/immutable support are
	 * clean, while an arbitrary trigger or dynamic actor contaminates. */
	source->inuse = true;
	Set3(source->s.origin, 100.0f, 100.0f, 0.0f);
	Set3(source->absmin, 84.0f, 84.0f, -24.0f);
	Set3(source->absmax, 116.0f, 116.0f, 32.0f);
	player->inuse = true;
	player->client = (gclient_t *)player;
	support->inuse = true;
	support->classname = "immutable-test-support";
	item->inuse = true;
	item->solid = SOLID_TRIGGER;
	item->touch = Touch_Item;
	sg_host.box_edicts = NULL;
	CHECK(!SG_OracleReplaySourceEvents(source, &contaminated, &door_passed));
	sg_host.box_edicts = TestBoxEdicts;
	test_trigger_hits[0] = item;
	test_trigger_hits[1] = sound_trigger;
	test_trigger_hits[2] = door_trigger;
	test_trigger_count = 3;
	test_solid_hits[0] = world_ent;
	test_solid_hits[1] = source;
	test_solid_hits[2] = support;
	test_solid_count = 3;
	CHECK(SG_OracleReplaySourceEvents(source, &contaminated, &door_passed));
	CHECK(!contaminated && !door_passed);

	test_trigger_hits[3] = arbitrary;
	test_trigger_count = 4;
	CHECK(SG_OracleReplaySourceEvents(source, &contaminated, &door_passed));
	CHECK(contaminated && !door_passed);
	test_trigger_count = 3;
	test_solid_hits[3] = player;
	test_solid_count = 4;
	CHECK(SG_OracleReplaySourceEvents(source, &contaminated, &door_passed));
	CHECK(contaminated && !door_passed);

	test_solid_hits[3] = door;
	CHECK(SG_OracleReplaySourceEvents(source, &contaminated, &door_passed));
	CHECK(!contaminated && door_passed);
	test_solid_count = 3;
	VectorClear(source->s.origin);
	CHECK(SG_OracleReplaySourceEvents(source, &contaminated, &door_passed));
	CHECK(!contaminated && door_passed);
}

/* Match the engine's transformed-brush basis exactly.  The float results are
 * intentionally retained: these regressions protect the conservative helper
 * from treating ideal double geometry as a tighter boundary than collision. */
static void EngineRotate(vec3_t angles, const vec3_t local, vec3_t out)
{
	vec3_t forward, right, up;

	AngleVectors(angles, forward, right, up);
	out[0] = local[0] * forward[0] - local[1] * right[0] +
	         local[2] * up[0];
	out[1] = local[0] * forward[1] - local[1] * right[1] +
	         local[2] * up[1];
	out[2] = local[0] * forward[2] - local[1] * right[2] +
	         local[2] * up[2];
}

static double Radius2(const vec3_t point)
{
	return (double)point[0] * point[0] +
	       (double)point[1] * point[1] +
	       (double)point[2] * point[2];
}

int main(void)
{
	edict_t ents[3];
	vec3_t brush_mins, brush_maxs, start, end, angles, local, rotated;
	vec3_t hull_mins = { -16, -16, -24 };
	vec3_t hull_maxs = { 16, 16, 32 };
	double expected_radius2;
	float large;

	TestReplayTriggerClassifier();

	memset(ents, 0, sizeof(ents));
	g_edicts = ents;
	globals.num_edicts = 1;
	Set3(brush_mins, 32, -8, -8);
	Set3(brush_maxs, 64, 8, 8);
	Rotator(&ents[0], 0, 0, 37, 0, brush_mins, brush_maxs);

	/* Default yaw/Z, X-axis roll, and Y-axis pitch all represent the same
	 * full-turn annulus after their coordinates are permuted. */
	Set3(start, 0, 0, 0); Set3(end, 100, 0, 0);
	CHECK(Blocks(start, NULL, NULL, end, MASK_OPAQUE));
	Set3(start, 0, 0, 40); Set3(end, 100, 0, 40);
	CHECK(!Blocks(start, NULL, NULL, end, MASK_OPAQUE));
	Set3(brush_mins, -8, 32, -8);
	Set3(brush_maxs, 8, 64, 8);
	Rotator(&ents[0], 4, 0, 0, 271, brush_mins, brush_maxs);
	Set3(start, 0, 0, 0); Set3(end, 0, 100, 0);
	CHECK(Blocks(start, NULL, NULL, end, MASK_PLAYERSOLID));
	Set3(start, 40, 0, 0); Set3(end, 40, 100, 0);
	CHECK(!Blocks(start, NULL, NULL, end, MASK_PLAYERSOLID));
	Set3(brush_mins, -8, -8, 32);
	Set3(brush_maxs, 8, 8, 64);
	Rotator(&ents[0], 8, 143, 0, 0, brush_mins, brush_maxs);
	Set3(start, 0, 0, 0); Set3(end, 0, 0, 100);
	CHECK(Blocks(start, NULL, NULL, end, MASK_DEADSOLID));
	Set3(start, 0, 40, 0); Set3(end, 0, 40, 100);
	CHECK(!Blocks(start, NULL, NULL, end, MASK_DEADSOLID));

	/* The dynamic phase is intentionally irrelevant.  The central annulus
	 * hole remains clear, while a radial path and exact tangency block. */
	Set3(brush_mins, 32, -8, -8);
	Set3(brush_maxs, 64, 8, 8);
	Rotator(&ents[0], 0, 0, 0, 0, brush_mins, brush_maxs);
	Set3(start, 0, 0, 0); Set3(end, 20, 0, 0);
	CHECK(!Blocks(start, NULL, NULL, end, MASK_OPAQUE));
	Set3(start, 0, 0, 0); Set3(end, 100, 0, 0);
	CHECK(Blocks(start, NULL, NULL, end, MASK_OPAQUE));
	Set3(start, -100, 64, 0); Set3(end, 100, 64, 0);
	CHECK(Blocks(start, NULL, NULL, end, MASK_OPAQUE));
	Set3(start, 0, 0, 17); Set3(end, 100, 0, 17);
	CHECK(!Blocks(start, NULL, NULL, end, MASK_OPAQUE));
	Set3(start, 0, 0, 0); Set3(end, 20, 0, 0);
	CHECK(Blocks(start, hull_mins, hull_maxs, end, MASK_OPAQUE));
	Set3(ents[0].s.angles, 0, 311, 0);
	Set3(start, -100, 32, 0); Set3(end, 100, 32, 0);
	CHECK(Blocks(start, NULL, NULL, end, MASK_OPAQUE));
	CHECK(!Blocks(start, NULL, NULL, end, CONTENTS_WATER));

	/* Any nonzero fixed Euler component, even sub-millidegree noise, selects
	 * the conservative sphere rather than treating a tilted brush as a yaw
	 * annulus with a clear central hole. */
	Rotator(&ents[0], 0, -0.0009f, 0, 0, brush_mins, brush_maxs);
	Set3(start, 0, 0, 0); Set3(end, 20, 0, 0);
	CHECK(Blocks(start, NULL, NULL, end, MASK_OPAQUE));

	/* Exact radial and sphere tangencies must block. These used to be able to
	 * round outward radii down through float sqrt/square intermediates. */
	Set3(brush_mins, 63, 63, -1);
	Set3(brush_maxs, 64, 64, 1);
	Rotator(&ents[0], 0, 0, 0, 0, brush_mins, brush_maxs);
	Set3(start, 64, 64, 0); Set3(end, 64, 64, 0);
	CHECK(Blocks(start, NULL, NULL, end, MASK_OPAQUE));
	Set3(brush_mins, 1, 6, -1);
	Set3(brush_maxs, 2, 7, 1);
	Rotator(&ents[0], 0, 0, 0, 0, brush_mins, brush_maxs);
	Set3(start, 1, 6, 0); Set3(end, 1, 6, 0);
	CHECK(Blocks(start, NULL, NULL, end, MASK_OPAQUE));
	Set3(brush_mins, -1, -1, 0);
	Set3(brush_maxs, 1, 1, 0);
	Rotator(&ents[0], 0, -0.0009f, 0, 0, brush_mins, brush_maxs);
	Set3(start, 1, 1, 0); Set3(end, 1, 1, 0);
	CHECK(Blocks(start, NULL, NULL, end, MASK_OPAQUE));

	/* The engine's own float AngleVectors can land just outside ideal double
	 * outer geometry, or just inside ideal inner geometry. Exercise yaw,
	 * pitch, and roll rather than reproducing those transforms in double. */
	Set3(brush_mins, -48, -48, -1);
	Set3(brush_maxs, 48, 48, 1);
	Set3(angles, 0, 0.001f, 0);
	Set3(local, 48, 48, 0);
	EngineRotate(angles, local, rotated);
	CHECK(Radius2(rotated) > 4608.0);
	Rotator(&ents[0], 0, angles[0], angles[1], angles[2],
	        brush_mins, brush_maxs);
	VectorCopy(rotated, start); VectorCopy(rotated, end);
	CHECK(Blocks(start, NULL, NULL, end, MASK_OPAQUE));
	Set3(brush_mins, 48, -1, 47);
	Set3(brush_maxs, 49, 1, 48);
	Set3(angles, 0.001f, 0, 0);
	Set3(local, 48, 0, 47);
	EngineRotate(angles, local, rotated);
	CHECK(Radius2(rotated) < 4513.0);
	Rotator(&ents[0], 8, angles[0], angles[1], angles[2],
	        brush_mins, brush_maxs);
	VectorCopy(rotated, start); VectorCopy(rotated, end);
	CHECK(Blocks(start, NULL, NULL, end, MASK_OPAQUE));
	Set3(brush_mins, 0, 48, 48);
	Set3(brush_maxs, 0, 48, 48);
	Set3(angles, 0, 0, 0.001f);
	Set3(local, 0, 48, 48);
	EngineRotate(angles, local, rotated);
	CHECK(Radius2(rotated) > 4608.0);
	Rotator(&ents[0], 12, angles[0], angles[1], angles[2],
	        brush_mins, brush_maxs);
	VectorCopy(rotated, start); VectorCopy(rotated, end);
	CHECK(Blocks(start, NULL, NULL, end, MASK_OPAQUE));

	/* The relative envelope is needed beyond ordinary map extents: an absolute
	 * 1/32 pad cannot cover the engine's float transformed corner at scale. */
	large = 2097152.0f;
	Set3(brush_mins, -large, -large * 0.5f, -1);
	Set3(brush_maxs, large, large * 0.5f, 1);
	Set3(angles, 0, 0.001f, 0);
	Set3(local, large, large * 0.5f, 0);
	EngineRotate(angles, local, rotated);
	expected_radius2 = (double)large * (double)large +
	                   0.25 * (double)large * (double)large;
	CHECK(Radius2(rotated) > expected_radius2);
	CHECK(sqrt(Radius2(rotated)) - sqrt(expected_radius2) > 1.0 / 32.0);
	Rotator(&ents[0], 0, angles[0], angles[1], angles[2],
	        brush_mins, brush_maxs);
	VectorCopy(rotated, start); VectorCopy(rotated, end);
	CHECK(Blocks(start, NULL, NULL, end, MASK_OPAQUE));
	/* The same scale-aware envelope protects the inner annulus and fallback
	 * sphere; their engine-float corners differ by more than 1/32 as well. */
	Set3(brush_mins, large, -1, large * 0.5f);
	Set3(brush_maxs, large + 1, 1, large * 0.5f + 1);
	Set3(angles, 0.001f, 0, 0);
	Set3(local, large, 0, large * 0.5f);
	EngineRotate(angles, local, rotated);
	expected_radius2 = (double)large * (double)large +
	                   0.25 * (double)large * (double)large;
	CHECK(sqrt(expected_radius2) - sqrt(Radius2(rotated)) > 1.0 / 32.0);
	Rotator(&ents[0], 8, angles[0], angles[1], angles[2],
	        brush_mins, brush_maxs);
	VectorCopy(rotated, start); VectorCopy(rotated, end);
	CHECK(Blocks(start, NULL, NULL, end, MASK_OPAQUE));
	Set3(brush_mins, 0, large, large * 0.5f);
	Set3(brush_maxs, 0, large, large * 0.5f);
	Set3(angles, 0, 0, 0.001f);
	Set3(local, 0, large, large * 0.5f);
	EngineRotate(angles, local, rotated);
	expected_radius2 = (double)large * (double)large +
	                   0.25 * (double)large * (double)large;
	CHECK(sqrt(Radius2(rotated)) - sqrt(expected_radius2) > 1.0 / 32.0);
	Rotator(&ents[0], 12, angles[0], angles[1], angles[2],
	        brush_mins, brush_maxs);
	VectorCopy(rotated, start); VectorCopy(rotated, end);
	CHECK(Blocks(start, NULL, NULL, end, MASK_OPAQUE));

	/* Finite float endpoints can differ by twice FLT_MAX. The full segment
	 * still crosses the pivot and must not overflow into a clear result. */
	Set3(brush_mins, 32, -8, -8);
	Set3(brush_maxs, 64, 8, 8);
	Rotator(&ents[0], 0, 0, 0, 0, brush_mins, brush_maxs);
	Set3(start, -FLT_MAX, 0, 0); Set3(end, FLT_MAX, 0, 0);
	CHECK(Blocks(start, NULL, NULL, end, MASK_OPAQUE));

	/* Both axis flags also deliberately use the conservative pivot sphere.
	 * Multiple rotators OR their exclusions. */
	Rotator(&ents[0], 12, 0, 0, 0, brush_mins, brush_maxs);
	CHECK(Blocks(start, NULL, NULL, end, MASK_OPAQUE));
	Rotator(&ents[0], 0, 0, 0, 0, brush_mins, brush_maxs);
	Rotator(&ents[1], 0, 0, 0, 0, brush_mins, brush_maxs);
	Set3(ents[1].s.origin, 256, 0, 0);
	globals.num_edicts = 2;
	Set3(start, 180, 0, 0); Set3(end, 330, 0, 0);
	CHECK(Blocks(start, NULL, NULL, end, MASK_OPAQUE));

	/* Invalid geometry fails closed. Non-BSP or non-rotating entities do not
	 * participate, even if they carry identical bounds. */
	globals.num_edicts = 1;
	Rotator(&ents[0], 0, 0, 0, 0, brush_mins, brush_maxs);
	ents[0].mins[0] = NAN;
	CHECK(Blocks(start, NULL, NULL, end, MASK_OPAQUE));
	Rotator(&ents[0], 0, 0, 0, 0, brush_mins, brush_maxs);
	ents[0].solid = SOLID_NOT;
	CHECK(!Blocks(start, NULL, NULL, end, MASK_OPAQUE));
	ents[0].solid = SOLID_BSP;
	ents[0].classname = "func_door";
	CHECK(!Blocks(start, NULL, NULL, end, MASK_OPAQUE));
	CHECK(Blocks(start, NULL, hull_maxs, end, MASK_OPAQUE));

	if (failures)
	{
		fprintf(stderr, "sg_rotator_sweep_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("sg_rotator_sweep_test: ok");
	return 0;
}
