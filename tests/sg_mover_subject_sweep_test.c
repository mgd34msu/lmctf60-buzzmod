/* Focused game-boundary contract for retained subjects versus full movers. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "g_local.h"
#include "slipgate/sg_local.h"

#define TEST_EDICTS 16
#define MOVER_INDEX 11
#define HOOK_INDEX 12

game_export_t globals;
game_locals_t game;
edict_t *g_edicts;

static edict_t ents[TEST_EDICTS];
static gclient_t clients[2];
static int failures;

void door_use(edict_t *self, edict_t *other, edict_t *activator)
{
	(void)self;
	(void)other;
	(void)activator;
}

void door_secret_use(edict_t *self, edict_t *other, edict_t *activator)
{
	(void)self;
	(void)other;
	(void)activator;
}

void hook_touch(edict_t *self, edict_t *other, cplane_t *plane,
	csurface_t *surf)
{
	(void)self;
	(void)other;
	(void)plane;
	(void)surf;
}

void hook_die(edict_t *self, edict_t *inflictor, edict_t *attacker,
	int damage, vec3_t point)
{
	(void)self;
	(void)inflictor;
	(void)attacker;
	(void)damage;
	(void)point;
}

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

static void SetLinkedBounds(edict_t *ent)
{
	float radius = 0.0f;
	qboolean rotated_bsp;
	int axis;

	rotated_bsp = ent->solid == SOLID_BSP &&
	              (ent->s.angles[0] != 0.0f || ent->s.angles[1] != 0.0f ||
	               ent->s.angles[2] != 0.0f);
	if (rotated_bsp)
	{
		for (axis = 0; axis < 3; axis++)
		{
			float lo = fabsf(ent->mins[axis]);
			float hi = fabsf(ent->maxs[axis]);

			if (lo > radius) radius = lo;
			if (hi > radius) radius = hi;
		}
	}
	for (axis = 0; axis < 3; axis++)
	{
		ent->size[axis] = ent->maxs[axis] - ent->mins[axis];
		ent->absmin[axis] = ent->s.origin[axis] +
		                    (rotated_bsp ? -radius : ent->mins[axis]) - 1.0f;
		ent->absmax[axis] = ent->s.origin[axis] +
		                    (rotated_bsp ? radius : ent->maxs[axis]) + 1.0f;
	}
	ent->area.prev = &ents[0].area;
	ent->area.next = &ents[0].area;
}

static void LiveEdict(edict_t *ent, int number, const char *classname)
{
	memset(ent, 0, sizeof(*ent));
	ent->s.number = number;
	ent->inuse = true;
	ent->linkcount = 1;
	ent->classname = (char *)classname;
}

static void ResetWorld(void)
{
	memset(ents, 0, sizeof(ents));
	memset(clients, 0, sizeof(clients));
	memset(&globals, 0, sizeof(globals));
	memset(&game, 0, sizeof(game));
	g_edicts = ents;
	game.maxentities = TEST_EDICTS;
	game.maxclients = 2;
	game.clients = clients;
	globals.edicts = ents;
	globals.edict_size = sizeof(edict_t);
	globals.num_edicts = HOOK_INDEX + 1;
	globals.max_edicts = TEST_EDICTS;
	LiveEdict(&ents[0], 0, "worldspawn");
}

static edict_t *TranslationMover(void)
{
	edict_t *mover = &ents[MOVER_INDEX];

	LiveEdict(mover, MOVER_INDEX, "func_door");
	mover->solid = SOLID_BSP;
	mover->movetype = MOVETYPE_PUSH;
	mover->use = door_use;
	Set3(mover->mins, -8.0f, -8.0f, 0.0f);
	Set3(mover->maxs, 8.0f, 8.0f, 8.0f);
	VectorClear(mover->s.origin);
	VectorClear(mover->moveinfo.start_origin);
	Set3(mover->moveinfo.end_origin, 64.0f, 0.0f, 0.0f);
	VectorClear(mover->moveinfo.start_angles);
	VectorClear(mover->moveinfo.end_angles);
	SetLinkedBounds(mover);
	return mover;
}

static edict_t *PlayerSubject(float x, float y, float z, int movetype,
	float max_z)
{
	edict_t *player = &ents[1];

	LiveEdict(player, 1, "player");
	player->client = &clients[0];
	player->solid = SOLID_BBOX;
	player->movetype = movetype;
	Set3(player->s.origin, x, y, z);
	Set3(player->mins, -16.0f, -16.0f, -24.0f);
	Set3(player->maxs, 16.0f, 16.0f, max_z);
	SetLinkedBounds(player);
	return player;
}

static edict_t *BodySubject(float x, float y, float z)
{
	edict_t *body = &ents[3];

	LiveEdict(body, 3, "bodyque");
	body->solid = SOLID_BBOX;
	body->movetype = MOVETYPE_TOSS;
	Set3(body->s.origin, x, y, z);
	Set3(body->mins, -16.0f, -16.0f, -24.0f);
	Set3(body->maxs, 16.0f, 16.0f, -8.0f);
	SetLinkedBounds(body);
	return body;
}

static edict_t *HookSubject(float x, float y, float z, int solid)
{
	edict_t *owner = PlayerSubject(-128.0f, -128.0f, 0.0f,
	                               MOVETYPE_WALK, 32.0f);
	edict_t *hook = &ents[HOOK_INDEX];

	LiveEdict(hook, HOOK_INDEX, "noclass");
	hook->solid = solid;
	hook->movetype = MOVETYPE_FLYMISSILE;
	hook->owner = owner;
	hook->touch = hook_touch;
	hook->die = hook_die;
	Set3(hook->s.origin, x, y, z);
	VectorClear(hook->mins);
	VectorClear(hook->maxs);
	SetLinkedBounds(hook);
	owner->client->hook = hook;
	return hook;
}

static void RotatingPoint(edict_t *mover, const vec3_t local,
	int angle_axis, float angle, vec3_t point)
{
	vec3_t angles, forward, right, up;

	VectorCopy(mover->moveinfo.start_angles, angles);
	angles[angle_axis] = angle;
	AngleVectors(angles, forward, right, up);
	point[0] = mover->s.origin[0] + local[0] * forward[0] -
	           local[1] * right[0] + local[2] * up[0];
	point[1] = mover->s.origin[1] + local[0] * forward[1] -
	           local[1] * right[1] + local[2] * up[1];
	point[2] = mover->s.origin[2] + local[0] * forward[2] -
	           local[1] * right[2] + local[2] * up[2];
}

static void TestTranslationAndBoundary(void)
{
	edict_t *mover, *player, *body, *hook;

	ResetWorld();
	mover = TranslationMover();
	player = PlayerSubject(100.0f, 0.0f, 0.0f, MOVETYPE_WALK, 32.0f);
	CHECK(SG_MoverSubjectOutsideSweep(mover, player));
	Set3(player->s.origin, 0.0f, 0.0f, 0.0f);
	SetLinkedBounds(player);
	CHECK(!SG_MoverSubjectOutsideSweep(mover, player));

	/* The translating brush ends at X=64 with a local max of 8.  A player
	 * origin at 88 puts its -16 face exactly on that boundary: still unsafe. */
	Set3(player->s.origin, 88.0f, 0.0f, 0.0f);
	SetLinkedBounds(player);
	CHECK(!SG_MoverSubjectOutsideSweep(mover, player));
	player->s.origin[0] = 88.125f;
	SetLinkedBounds(player);
	CHECK(SG_MoverSubjectOutsideSweep(mover, player));

	/* Use the actual corpse height.  At the same origin the standing player
	 * reaches into the brush, while the copied body's -8 top remains below it. */
	player = PlayerSubject(0.0f, 0.0f, 0.0f, MOVETYPE_WALK, 32.0f);
	body = BodySubject(0.0f, 0.0f, 0.0f);
	CHECK(!SG_MoverSubjectOutsideSweep(mover, player));
	CHECK(SG_MoverSubjectOutsideSweep(mover, body));
	player = PlayerSubject(0.0f, 0.0f, 0.0f, MOVETYPE_TOSS, -8.0f);
	CHECK(SG_MoverSubjectOutsideSweep(mover, player));

	hook = HookSubject(72.0f, 0.0f, 4.0f, SOLID_BBOX);
	CHECK(!SG_MoverSubjectOutsideSweep(mover, hook));
	hook->s.origin[0] = 72.125f;
	SetLinkedBounds(hook);
	CHECK(SG_MoverSubjectOutsideSweep(mover, hook));
	hook->solid = SOLID_TRIGGER;
	CHECK(SG_MoverSubjectOutsideSweep(mover, hook));
}

static void TestRotatingAndSecretSweeps(void)
{
	edict_t *mover, *hook;

	ResetWorld();
	mover = TranslationMover();
	mover->classname = "func_door_rotating";
	Set3(mover->mins, 32.0f, -8.0f, -8.0f);
	Set3(mover->maxs, 64.0f, 8.0f, 8.0f);
	VectorClear(mover->moveinfo.start_origin);
	VectorClear(mover->moveinfo.end_origin);
	VectorClear(mover->moveinfo.start_angles);
	Set3(mover->moveinfo.end_angles, 0.0f, 90.0f, 0.0f);
	SetLinkedBounds(mover);
	hook = HookSubject(48.0f, 48.0f, 0.0f, SOLID_BBOX);
	CHECK(!SG_MoverSubjectOutsideSweep(mover, hook));
	Set3(hook->s.origin, -80.0f, -80.0f, 0.0f);
	SetLinkedBounds(hook);
	CHECK(SG_MoverSubjectOutsideSweep(mover, hook));

	ResetWorld();
	mover = TranslationMover();
	mover->use = door_secret_use;
	VectorClear(mover->moveinfo.end_origin);
	Set3(mover->pos1, 0.0f, 64.0f, 0.0f);
	Set3(mover->pos2, 64.0f, 64.0f, 0.0f);
	hook = HookSubject(64.0f, 64.0f, 4.0f, SOLID_BBOX);
	CHECK(!SG_MoverSubjectOutsideSweep(mover, hook));
	Set3(hook->s.origin, 100.0f, 100.0f, 4.0f);
	SetLinkedBounds(hook);
	CHECK(SG_MoverSubjectOutsideSweep(mover, hook));
}

static void TestRotatingBoundsRoundOutward(void)
{
	static const float samples[] = {
		0.0f, 22.5f, 45.0f, 89.875f, 90.0f,
		132.6239929f, 179.875f, 180.0f
	};
	int angle_axis, corner, sample;

	for (angle_axis = 0; angle_axis < 3; angle_axis++)
	{
		edict_t *mover, *hook;

		ResetWorld();
		mover = TranslationMover();
		mover->classname = "func_door_rotating";
		Set3(mover->mins, 32.0f, 38.0f, -81.0f);
		Set3(mover->maxs, 101.0f, 88.0f, 22.0f);
		VectorClear(mover->moveinfo.start_origin);
		VectorClear(mover->moveinfo.end_origin);
		VectorClear(mover->moveinfo.start_angles);
		VectorClear(mover->moveinfo.end_angles);
		mover->moveinfo.end_angles[angle_axis] = 180.0f;
		VectorClear(mover->s.angles);
		SetLinkedBounds(mover);
		hook = HookSubject(0.0f, 0.0f, 0.0f, SOLID_BBOX);

		for (corner = 0; corner < 8; corner++)
		{
			vec3_t local;

			local[0] = (corner & 1) ? mover->maxs[0] : mover->mins[0];
			local[1] = (corner & 2) ? mover->maxs[1] : mover->mins[1];
			local[2] = (corner & 4) ? mover->maxs[2] : mover->mins[2];
			for (sample = 0;
			     sample < (int)(sizeof(samples) / sizeof(samples[0]));
			     sample++)
			{
				RotatingPoint(mover, local, angle_axis, samples[sample],
				              hook->s.origin);
				SetLinkedBounds(hook);
				CHECK(!SG_MoverSubjectOutsideSweep(mover, hook));
			}
		}
	}

	/* Coefficient recovery used to lose two float steps against this large
	 * origin.  One nextafter still left the direct AngleVectors corner outside
	 * the supposedly inclusive partial-arc bound. */
	{
		edict_t *mover, *hook;
		vec3_t local;

		ResetWorld();
		mover = TranslationMover();
		mover->classname = "func_door_rotating";
		Set3(mover->s.origin, -6173.0f, 13968.0f, 7176.0f);
		Set3(mover->mins, 25.0f, -1869.0f, -252.0f);
		Set3(mover->maxs, 1427.0f, 3828.0f, 2660.0f);
		VectorClear(mover->moveinfo.start_origin);
		VectorClear(mover->moveinfo.end_origin);
		VectorClear(mover->moveinfo.start_angles);
		Set3(mover->moveinfo.end_angles, 0.0f, -230.0f, 0.0f);
		VectorClear(mover->s.angles);
		SetLinkedBounds(mover);
		hook = HookSubject(0.0f, 0.0f, 0.0f, SOLID_BBOX);
		Set3(local, 1427.0f, 3828.0f, -252.0f);
		RotatingPoint(mover, local, YAW, -69.5574875f, hook->s.origin);
		SetLinkedBounds(hook);
		CHECK(!SG_MoverSubjectOutsideSweep(mover, hook));
	}

	/* Pose validation admits the bounded stock AngleMove final-roundoff past
	 * an endpoint.  At a large leaf radius that tiny angular sliver spans more
	 * than the generic numeric padding, so the sweep must explicitly include
	 * the authenticated current pose rather than stopping at the nominal end. */
	{
		edict_t *mover, *hook;
		vec3_t local;

		ResetWorld();
		mover = TranslationMover();
		mover->classname = "func_door_rotating";
		VectorClear(mover->mins);
		Set3(mover->maxs, 100000.0f, 0.0f, 0.0f);
		VectorClear(mover->moveinfo.start_origin);
		VectorClear(mover->moveinfo.end_origin);
		VectorClear(mover->moveinfo.start_angles);
		Set3(mover->moveinfo.end_angles, 0.0f, 200.0f, 0.0f);
		Set3(mover->s.angles, 0.0f, 200.0015f, 0.0f);
		SetLinkedBounds(mover);
		hook = HookSubject(0.0f, 0.0f, 0.0f, SOLID_BBOX);
		Set3(local, 100000.0f, 0.0f, 0.0f);
		RotatingPoint(mover, local, YAW, mover->s.angles[YAW],
		              hook->s.origin);
		SetLinkedBounds(hook);
		CHECK(!SG_MoverSubjectOutsideSweep(mover, hook));
	}
}

static void TestInvalidIdentitiesFailClosed(void)
{
	edict_t *mover, *player, *hook;
	edict_t clone;

	ResetWorld();
	mover = TranslationMover();
	player = PlayerSubject(100.0f, 0.0f, 0.0f, MOVETYPE_WALK, 32.0f);
	CHECK(SG_MoverSubjectOutsideSweep(mover, player));

	clone = *mover;
	CHECK(!SG_MoverSubjectOutsideSweep(&clone, player));
	clone = *player;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, &clone));
	ents[2] = *mover;
	ents[2].s.number = 2;
	ents[2].area.prev = &ents[0].area;
	ents[2].area.next = &ents[0].area;
	CHECK(!SG_MoverSubjectOutsideSweep(&ents[2], player));
	ents[4] = *player;
	ents[4].s.number = 4;
	ents[4].area.prev = &ents[0].area;
	ents[4].area.next = &ents[0].area;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, &ents[4]));
	player->client = &clients[1];
	CHECK(!SG_MoverSubjectOutsideSweep(mover, player));
	player->client = &clients[0];
	CHECK(SG_MoverSubjectOutsideSweep(mover, player));
	mover->s.number = MOVER_INDEX + 1;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, player));
	mover->s.number = MOVER_INDEX;
	player->s.number = 7;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, player));
	player->s.number = 1;

	mover->inuse = false;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, player));
	mover->inuse = true;
	mover->linkcount = 0;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, player));
	mover->linkcount = 1;
	mover->solid = SOLID_BBOX;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, player));
	mover->solid = SOLID_BSP;
	mover->movetype = MOVETYPE_STOP;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, player));
	mover->movetype = MOVETYPE_PUSH;
	mover->classname = "func_train";
	CHECK(!SG_MoverSubjectOutsideSweep(mover, player));
	mover->classname = "func_door";
	mover->use = NULL;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, player));
	mover->use = door_use;
	mover->mins[0] = NAN;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, player));
	mover->mins[0] = -8.0f;
	mover->absmin[0] = mover->absmax[0] + 1.0f;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, player));
	SetLinkedBounds(mover);
	mover->s.origin[0] = 1000.0f;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, player));
	mover->s.origin[0] = 0.0f;
	SetLinkedBounds(mover);
	mover->classname = "func_door_rotating";
	VectorClear(mover->moveinfo.start_angles);
	Set3(mover->moveinfo.end_angles, 0.0f, 90.0f, 0.0f);
	Set3(mover->s.angles, 0.0f, 180.0f, 0.0f);
	SetLinkedBounds(mover);
	CHECK(!SG_MoverSubjectOutsideSweep(mover, player));
	mover->classname = "func_door";
	VectorClear(mover->moveinfo.end_angles);
	VectorClear(mover->s.angles);
	SetLinkedBounds(mover);

	player->solid = SOLID_NOT;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, player));
	player->solid = SOLID_BBOX;
	player->classname = "item_armor_body";
	CHECK(!SG_MoverSubjectOutsideSweep(mover, player));
	player->classname = "player";
	player->movetype = MOVETYPE_NOCLIP;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, player));
	player->movetype = MOVETYPE_WALK;
	player->mins[0] = NAN;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, player));
	player->mins[0] = -16.0f;
	player->linkcount = 0;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, player));
	player->linkcount = 1;
	player->area.prev = NULL;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, player));
	SetLinkedBounds(player);
	player->size[0] += 0.125f;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, player));
	SetLinkedBounds(player);

	hook = HookSubject(100.0f, 0.0f, 4.0f, SOLID_BBOX);
	CHECK(SG_MoverSubjectOutsideSweep(mover, hook));
	hook->owner->client->hook = NULL;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, hook));
	hook->owner->client->hook = hook;
	CHECK(SG_MoverSubjectOutsideSweep(mover, hook));
	hook->touch = NULL;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, hook));
	hook->touch = hook_touch;
	hook->die = NULL;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, hook));
	hook->die = hook_die;
	CHECK(SG_MoverSubjectOutsideSweep(mover, hook));

	globals.edicts = &ents[1];
	CHECK(!SG_MoverSubjectOutsideSweep(mover, hook));
	globals.edicts = ents;
	globals.num_edicts = TEST_EDICTS + 1;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, hook));
	globals.num_edicts = HOOK_INDEX + 1;
	globals.max_edicts = TEST_EDICTS - 1;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, hook));
	globals.max_edicts = TEST_EDICTS;
	game.maxentities = 4;
	CHECK(!SG_MoverSubjectOutsideSweep(mover, hook));
}

int main(void)
{
	TestTranslationAndBoundary();
	TestRotatingAndSecretSweeps();
	TestRotatingBoundsRoundOutward();
	TestInvalidIdentitiesFailClosed();
	if (failures)
	{
		fprintf(stderr, "sg_mover_subject_sweep_test: %d failure(s)\n",
		        failures);
		return 1;
	}
	puts("sg_mover_subject_sweep_test: ok");
	return 0;
}
