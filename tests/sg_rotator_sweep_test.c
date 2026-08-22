/* Focused geometry contract for phase-independent func_rotating exclusion. */
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "g_local.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_rune_binding.h"
#include "slipgate/sg_rune_mechanism_catalog.h"

game_export_t globals;
game_locals_t game;
level_locals_t level;
edict_t *g_edicts;
sg_host_t sg_host;

static edict_t *test_trigger_hits[MAX_EDICTS];
static edict_t *test_solid_hits[MAX_EDICTS];
static gclient_t test_clients[1];
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

void button_touch(edict_t *self, edict_t *other, cplane_t *plane,
	csurface_t *surf)
{
	(void)self; (void)other; (void)plane; (void)surf;
}

void button_use(edict_t *self, edict_t *other, edict_t *activator)
{
	(void)self; (void)other; (void)activator;
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

int SG_MechCatalogButtonEndpoints(uint32_t key,
	const rune_mechanism_node_t *node, const edict_t *entity,
	sg_mech_button_endpoints_t *endpoints_out)
{
	(void)key;
	(void)node;
	(void)entity;
	(void)endpoints_out;
	return 0;
}

int SG_MechCatalogButtonBottomEndpoints(uint32_t key,
	const rune_mechanism_node_t *node, const edict_t *entity,
	sg_mech_button_endpoints_t *endpoints_out)
{
	return SG_MechCatalogButtonEndpoints(key, node, entity, endpoints_out);
}

int SG_RuneMechanismBindingCurrent(
	const sg_rune_mechanism_binding_t *binding)
{
	(void)binding;
	return 0;
}

int SG_RuneMechanismBindingTopologyCurrent(
	const sg_rune_mechanism_binding_t *binding)
{
	(void)binding;
	return 0;
}

edict_t *SG_RuneMechanismBindingResolveNode(
	const sg_rune_mechanism_binding_t *binding, uint32_t key)
{
	(void)binding;
	(void)key;
	return NULL;
}

edict_t *SG_RuneMechanismBindingResolveTopologyNode(
	const sg_rune_mechanism_binding_t *binding, uint32_t key)
{
	(void)binding;
	(void)key;
	return NULL;
}

int SG_RuneMechanismBindingMoverKeys(
	const sg_rune_mechanism_binding_t *binding,
	uint32_t keys_out[SG_RUNE_BINDING_MAX_MOVERS], size_t *key_count_out)
{
	(void)binding;
	(void)keys_out;
	(void)key_count_out;
	return 0;
}

int SG_RuneMechanismBindingTopologyMoverKeys(
	const sg_rune_mechanism_binding_t *binding,
	uint32_t keys_out[SG_RUNE_BINDING_MAX_MOVERS], size_t *key_count_out)
{
	(void)binding;
	(void)keys_out;
	(void)key_count_out;
	return 0;
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

static qboolean speculative_door_probe_blocked;

static trace_t ClearWorldTrace(const vec3_t start, const vec3_t mins,
	const vec3_t maxs, const vec3_t end, edict_t *passent, int contentmask)
{
	trace_t trace;

	(void)start;
	(void)mins;
	(void)maxs;
	(void)passent;
	(void)contentmask;
	memset(&trace, 0, sizeof(trace));
	trace.fraction = 1.0f;
	VectorCopy(end, trace.endpos);
	trace.ent = g_edicts;
	return trace;
}

static void SpeculativeStepPmove(pmove_t *pmove)
{
	vec3_t start, raised, forward;
	vec3_t mins = { -16.0f, -16.0f, -24.0f };
	vec3_t maxs = { 16.0f, 16.0f, 32.0f };
	trace_t trace;
	int axis;

	for (axis = 0; axis < 3; axis++)
		start[axis] = pmove->s.origin[axis] * 0.125f;
	VectorCopy(start, raised);
	raised[2] += 18.0f;
	trace = pmove->trace(start, mins, maxs, raised);
	speculative_door_probe_blocked = trace.startsolid || trace.allsolid ||
	                                trace.fraction < 1.0f;

	VectorCopy(start, forward);
	forward[0] += 1.0f;
	trace = pmove->trace(start, mins, maxs, forward);
	if (!trace.startsolid && !trace.allsolid && trace.fraction == 1.0f)
		pmove->s.origin[0] += 8;
	VectorCopy(mins, pmove->mins);
	VectorCopy(maxs, pmove->maxs);
	pmove->s.pm_flags |= PMF_ON_GROUND;
	pmove->groundentity = g_edicts;
}

static void TestDiscardedStepProbeDoesNotContaminate(void)
{
	edict_t ents[2];
	sg_phantom_t ph;
	usercmd_t cmd;
	int axis;

	memset(ents, 0, sizeof(ents));
	memset(&ph, 0, sizeof(ph));
	memset(&cmd, 0, sizeof(cmd));
	g_edicts = ents;
	globals.edicts = ents;
	globals.edict_size = sizeof(edict_t);
	globals.num_edicts = 2;
	ents[0].inuse = true;
	ents[0].s.number = 0;
	ents[1].inuse = true;
	ents[1].s.number = 1;
	ents[1].classname = "func_door_rotating";
	Set3(ents[1].mins, -14.0f, -17.0f, -68.0f);
	Set3(ents[1].maxs, 154.0f, 13.0f, 68.0f);
	VectorClear(ents[1].moveinfo.start_angles);
	Set3(ents[1].moveinfo.end_angles, 0.0f, 90.0f, 0.0f);
	VectorClear(ents[1].s.angles);
	Set3(ph.origin, 153.0f, 52.0f, -103.875f);
	for (axis = 0; axis < 3; axis++)
		ph.pms.origin[axis] = (short)(ph.origin[axis] * 8.0f);
	ph.old_pms = ph.pms;
	ph.groundentity = true;
	ph.groundentity_entity = &ents[0];
	Set3(ph.mins, -16.0f, -16.0f, -24.0f);
	Set3(ph.maxs, 16.0f, 16.0f, 32.0f);
	cmd.msec = 25;
	test_trigger_count = 0;
	test_solid_count = 0;
	speculative_door_probe_blocked = false;
	sg_host.trace = ClearWorldTrace;
	sg_host.box_edicts = TestBoxEdicts;
	sg_host.pmove = SpeculativeStepPmove;

	CHECK(SG_OracleRunWorld(&ph, &cmd, 1));
	CHECK(speculative_door_probe_blocked);
	CHECK(ph.origin[0] == 154.0f && ph.origin[1] == 52.0f &&
	      ph.origin[2] == -103.875f);
}

static void TestWorldOverlapUsesExactRotatorSweep(void)
{
	edict_t ents[2];
	sg_phantom_t ph;
	usercmd_t cmd;
	vec3_t brush_mins, brush_maxs;

	memset(ents, 0, sizeof(ents));
	memset(&ph, 0, sizeof(ph));
	memset(&cmd, 0, sizeof(cmd));
	g_edicts = ents;
	globals.num_edicts = 2;
	ents[0].inuse = true;
	Set3(brush_mins, 32.0f, -8.0f, -8.0f);
	Set3(brush_maxs, 64.0f, 8.0f, 8.0f);
	Rotator(&ents[1], 0, 0.0f, 37.0f, 0.0f,
	        brush_mins, brush_maxs);
	ents[1].inuse = true;
	test_trigger_count = 0;
	test_solid_hits[0] = &ents[1];
	test_solid_count = 1;
	sg_host.box_edicts = TestBoxEdicts;

	/* The linked radius cube reaches this point, but the authoritative
	 * full-turn brush sweep does not. */
	Set3(ph.origin, 0.0f, 0.0f, 80.0f);
	CHECK(SG_OracleRunWorld(&ph, &cmd, 0));

	/* A standing hull inside the swept annulus remains contaminated. */
	Set3(ph.origin, 48.0f, 0.0f, 0.0f);
	CHECK(!SG_OracleRunWorld(&ph, &cmd, 0));
	test_solid_count = 0;
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
	int i;

	memset(ents, 0, sizeof(ents));
	memset(&globals, 0, sizeof(globals));
	memset(&game, 0, sizeof(game));
	memset(test_clients, 0, sizeof(test_clients));
	g_edicts = ents;
	globals.edicts = ents;
	globals.edict_size = sizeof(edict_t);
	globals.num_edicts = 10;
	globals.max_edicts = 10;
	game.maxentities = 10;
	game.maxclients = 1;
	game.clients = test_clients;
	for (i = 0; i < 10; i++)
		ents[i].s.number = i;
	world_ent->inuse = true;

	door->inuse = true;
	door->classname = "func_door";
	door->use = door_use;
	door->teammaster = door;
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
	TestWorldOverlapUsesExactRotatorSweep();
	TestDiscardedStepProbeDoesNotContaminate();

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
