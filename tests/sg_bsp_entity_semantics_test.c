#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../slipgate/sg_bsp_entity_semantics.h"

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

typedef struct world_fixture_s
{
	sg_bsp_world_t world;
	uint8_t *entities;
	sg_bsp_model_t models[12];
} world_fixture_t;

static void InitWorld(world_fixture_t *fixture, const char *text)
{
	size_t length = strlen(text) + 1U;
	uint32_t index;

	memset(fixture, 0, sizeof(*fixture));
	fixture->entities = malloc(length);
	CHECK(fixture->entities != NULL);
	if (!fixture->entities)
		return;
	memcpy(fixture->entities, text, length);
	fixture->world.entities = fixture->entities;
	fixture->world.entity_byte_count = (uint32_t)length;
	fixture->world.models = fixture->models;
	fixture->world.model_count =
		(uint32_t)(sizeof(fixture->models) / sizeof(fixture->models[0]));
	for (index = 0; index < fixture->world.model_count; index++)
	{
		float extent = 16.0f + (float)index;

		fixture->models[index].mins.value[0] = -extent;
		fixture->models[index].mins.value[1] = -extent;
		fixture->models[index].mins.value[2] = -1.0f;
		fixture->models[index].maxs.value[0] = extent;
		fixture->models[index].maxs.value[1] = extent;
		fixture->models[index].maxs.value[2] = 65.0f;
	}
}

static void DestroyWorld(world_fixture_t *fixture)
{
	free(fixture->entities);
	memset(fixture, 0, sizeof(*fixture));
}

static sg_bsp_entity_semantics_t *Build(world_fixture_t *fixture)
{
	sg_bsp_entity_semantics_t *semantics = NULL;
	sg_bsp_entity_semantics_error_t error;

	CHECK(SG_BspEntitySemanticsBuild(&fixture->world,
		UINT64_C(0x5354415449434253), &semantics, &error));
	CHECK(error.code == SG_BSP_ENTITY_SEMANTICS_ERROR_NONE);
	return semantics;
}

static const sg_bsp_entity_semantic_t *FindEntity(
	const sg_bsp_entity_semantics_t *semantics, const char *classname)
{
	uint32_t index;

	for (index = 0; index < semantics->entity_count; index++)
		if (!strcmp(SG_BspEntitySemanticsString(semantics,
			semantics->entities[index].classname), classname))
			return &semantics->entities[index];
	return NULL;
}

static const sg_bsp_entity_semantic_t *FindModel(
	const sg_bsp_entity_semantics_t *semantics, uint32_t model)
{
	uint32_t index;

	for (index = 0; index < semantics->entity_count; index++)
		if (semantics->entities[index].bsp_model == model)
			return &semantics->entities[index];
	return NULL;
}

static int SameOutput(const sg_bsp_entity_semantics_t *left,
	const sg_bsp_entity_semantics_t *right, int ignore_source_ordinals)
{
	uint32_t index;

	if (!left || !right ||
		left->source_set_identity != right->source_set_identity ||
		memcmp(&left->world, &right->world, sizeof(left->world)) ||
		left->entity_count != right->entity_count ||
		left->edge_count != right->edge_count ||
		left->string_bytes != right->string_bytes ||
		(left->edge_count && memcmp(left->edges, right->edges,
			(size_t)left->edge_count * sizeof(*left->edges))) ||
		(left->string_bytes && memcmp(left->strings, right->strings,
			left->string_bytes)))
		return 0;
	for (index = 0; index < left->entity_count; index++)
	{
		sg_bsp_entity_semantic_t a = left->entities[index];
		sg_bsp_entity_semantic_t b = right->entities[index];

		if (ignore_source_ordinals)
		{
			a.source_entity_ordinal = 0U;
			b.source_entity_ordinal = 0U;
		}
		if (memcmp(&a, &b, sizeof(a)))
			return 0;
	}
	return 1;
}

static void TestLandmarksAndCanonicalOrder(void)
{
	static const char first[] =
		"{\n\"classname\" \"worldspawn\"\n}\n"
		"{\n\"classname\" \"weapon_railgun\"\n\"origin\" \"4 5 6\"\n}\n"
		"{\n\"classname\" \"item_armor_body\"\n\"origin\" \"1 2 3\"\n}\n"
		"{\n\"classname\" \"item_quad\"\n}\n"
		"{\n\"classname\" \"haste_rune\"\n}\n"
		"{\n\"classname\" \"item_health_mega\"\n}\n"
		"{\n\"classname\" \"ammo_rockets\"\n}\n"
		"{\n\"classname\" \"info_flag_red\"\n\"origin\" \"10 20 30\"\n}\n"
		"{\n\"classname\" \"item_flag_team2\"\n\"origin\" \"40 50 60\"\n}\n"
		"{\n\"classname\" \"info_player_deathmatch\"\n\"origin\" \"7 8 9\"\n}\n";
	static const char reordered[] =
		"{\n\"classname\" \"worldspawn\"\n}\n"
		"{\n\"classname\" \"item_flag_team2\"\n\"origin\" \"40 50 60\"\n}\n"
		"{\n\"classname\" \"ammo_rockets\"\n}\n"
		"{\n\"classname\" \"info_player_deathmatch\"\n\"origin\" \"7 8 9\"\n}\n"
		"{\n\"classname\" \"item_quad\"\n}\n"
		"{\n\"classname\" \"haste_rune\"\n}\n"
		"{\n\"classname\" \"info_flag_red\"\n\"origin\" \"10 20 30\"\n}\n"
		"{\n\"classname\" \"item_health_mega\"\n}\n"
		"{\n\"classname\" \"weapon_railgun\"\n\"origin\" \"4 5 6\"\n}\n"
		"{\n\"classname\" \"item_armor_body\"\n\"origin\" \"1 2 3\"\n}\n";
	world_fixture_t a;
	world_fixture_t b;
	sg_bsp_entity_semantics_t *left;
	sg_bsp_entity_semantics_t *right;
	const sg_bsp_entity_semantic_t *record;

	InitWorld(&a, first);
	InitWorld(&b, reordered);
	left = Build(&a);
	right = Build(&b);
	CHECK(left && right);
	if (left && right)
	{
		CHECK(left->entity_count == 9U);
		CHECK(SameOutput(left, right, 1));
		record = FindEntity(left, "info_flag_red");
		CHECK(record != NULL);
		CHECK(record && record->landmark_kind == SG_RUNE_LANDMARK_FLAG_STAND);
		CHECK(record && (record->flags & SG_BSP_ENTITY_FLAG_RED));
		record = FindEntity(left, "item_flag_team2");
		CHECK(record && (record->flags & SG_BSP_ENTITY_FLAG_BLUE));
		record = FindEntity(left, "weapon_railgun");
		CHECK(record && record->landmark_kind == SG_RUNE_LANDMARK_WEAPON);
		record = FindEntity(left, "item_armor_body");
		CHECK(record && record->landmark_kind == SG_RUNE_LANDMARK_ARMOR);
		record = FindEntity(left, "item_quad");
		CHECK(record && record->landmark_kind == SG_RUNE_LANDMARK_POWERUP);
		record = FindEntity(left, "haste_rune");
		CHECK(record && record->landmark_kind == SG_RUNE_LANDMARK_POWERUP);
		record = FindEntity(left, "info_player_deathmatch");
		CHECK(record && record->landmark_kind ==
			SG_RUNE_LANDMARK_DEFENSIVE_POSITION);
	}
	SG_BspEntitySemanticsDestroy(left);
	SG_BspEntitySemanticsDestroy(right);
	DestroyWorld(&a);
	DestroyWorld(&b);
}

static void TestMechanismsAndTopology(void)
{
	static const char text[] =
		"{ \"classname\" \"worldspawn\" }\n"
		"{ \"classname\" \"func_door\" \"model\" \"*1\" \"targetname\" \"gate\" \"wait\" \"2\" \"speed\" \"100\" \"angle\" \"-1\" \"lip\" \"8\" }\n"
		"{ \"classname\" \"trigger_multiple\" \"model\" \"*2\" \"target\" \"relay\" \"wait\" \"0.5\" \"delay\" \"0.25\" }\n"
		"{ \"classname\" \"trigger_relay\" \"targetname\" \"relay\" \"target\" \"gate\" \"delay\" \"1\" }\n"
		"{ \"classname\" \"func_button\" \"model\" \"*3\" \"target\" \"gate\" \"pathtarget\" \"corner2\" \"wait\" \"1.5\" \"angles\" \"0 90 0\" \"lip\" \"4\" }\n"
		"{ \"classname\" \"func_train\" \"model\" \"*4\" \"targetname\" \"train\" \"target\" \"corner\" \"speed\" \"200\" }\n"
		"{ \"classname\" \"path_corner\" \"targetname\" \"corner\" \"target\" \"corner2\" \"wait\" \"2\" }\n"
		"{ \"classname\" \"path_corner\" \"targetname\" \"corner2\" \"target\" \"corner\" }\n"
		"{ \"classname\" \"misc_teleporter\" \"target\" \"dest\" \"origin\" \"1 2 3\" }\n"
		"{ \"classname\" \"misc_teleporter_dest\" \"targetname\" \"dest\" \"origin\" \"100 200 300\" }\n";
	world_fixture_t fixture;
	sg_bsp_entity_semantics_t *semantics;
	const sg_bsp_entity_semantic_t *record;
	uint32_t index;
	uint32_t target_edges = 0U;
	uint32_t path_edges = 0U;

	InitWorld(&fixture, text);
	semantics = Build(&fixture);
	CHECK(semantics != NULL);
	if (semantics)
	{
		CHECK(semantics->entity_count == 9U);
		CHECK(semantics->edge_count == 7U);
		record = FindEntity(semantics, "func_door");
		CHECK(record && record->mechanism_kind == SG_RUNE_MECHANISM_DOOR);
		CHECK(record && record->bsp_model == 1U);
		CHECK(record && (record->flags & SG_BSP_ENTITY_HAS_BOUNDS));
		CHECK(record && record->bounds.mins.value[0] == -17.0f);
		CHECK(record && record->bounds.maxs.value[2] == 65.0f);
		CHECK(record && record->dwell_ms == 2000.0f);
		CHECK(record && record->move_direction.value[2] == 1.0f);
		CHECK(record && record->lip == 8.0f);
		record = FindEntity(semantics, "trigger_multiple");
		CHECK(record && record->mechanism_role == SG_MECH_NODE_TRIGGER);
		CHECK(record && record->landmark_kind == SG_RUNE_LANDMARK_TRIGGER);
		CHECK(record && record->delay_ms == 250.0f);
		CHECK(record && record->dwell_ms == 500.0f);
		record = FindEntity(semantics, "func_train");
		CHECK(record && record->mechanism_kind == SG_RUNE_MECHANISM_TRAIN);
		record = FindEntity(semantics, "misc_teleporter");
		CHECK(record && record->mechanism_kind == SG_RUNE_MECHANISM_TELEPORT);
		for (index = 0; index < semantics->edge_count; index++)
		{
			if (semantics->edges[index].kind == SG_MECH_EDGE_TARGET)
				target_edges++;
			if (semantics->edges[index].kind == SG_MECH_EDGE_PATH_TARGET)
				path_edges++;
		}
		CHECK(target_edges == 7U);
		CHECK(path_edges == 0U);
	}
	SG_BspEntitySemanticsDestroy(semantics);
	DestroyWorld(&fixture);
}

static void TestWorldAndMovementPhysics(void)
{
	static const char text[] =
		"{ \"classname\" \"worldspawn\" \"gravity\" \"100\" }\n"
		"{ \"classname\" \"trigger_gravity\" \"model\" \"*1\" \"gravity\" \"2\" }\n"
		"{ \"classname\" \"trigger_push\" \"model\" \"*2\" \"angle\" \"90\" \"speed\" \"1200\" }\n"
		"{ \"classname\" \"trigger_monsterjump\" \"model\" \"*3\" \"angle\" \"180\" \"speed\" \"250\" \"height\" \"300\" }\n"
		"{ \"classname\" \"trigger_hurt\" \"model\" \"*4\" \"dmg\" \"12\" }\n"
		"{ \"classname\" \"func_conveyor\" \"model\" \"*5\" \"speed\" \"140\" }\n"
		"{ \"classname\" \"target_laser\" \"angle\" \"45\" \"dmg\" \"9\" }\n";
	world_fixture_t fixture;
	sg_bsp_entity_semantics_t *semantics;
	const sg_bsp_entity_semantic_t *record;

	InitWorld(&fixture, text);
	semantics = Build(&fixture);
	CHECK(semantics != NULL);
	if (semantics)
	{
		CHECK(semantics->world.gravity == 100.0f);
		CHECK(semantics->world.flags & SG_BSP_WORLD_GRAVITY_EXPLICIT);
		record = FindEntity(semantics, "trigger_gravity");
		CHECK(record && record->gravity == 2.0f);
		CHECK(record && record->physics_kind ==
			SG_BSP_ENTITY_PHYSICS_GRAVITY);
		CHECK(record && (record->flags & SG_BSP_ENTITY_TOUCH_ACTIVATED));
		CHECK(record && record->landmark_kind == SG_RUNE_LANDMARK_TRIGGER);
		CHECK(record && (record->flags & SG_BSP_ENTITY_GRAVITY_DEFINED));
		CHECK(record && (record->flags & SG_BSP_ENTITY_INITIALLY_ACTIVE));
		record = FindEntity(semantics, "trigger_push");
		CHECK(record && record->mechanism_kind == SG_RUNE_MECHANISM_PUSH);
		CHECK(record && record->physics_kind == SG_BSP_ENTITY_PHYSICS_PUSH);
		CHECK(record && record->speed == 1200.0f);
		CHECK(record && (record->flags & SG_BSP_ENTITY_INITIALLY_ACTIVE));
		record = FindEntity(semantics, "trigger_monsterjump");
		CHECK(record && record->physics_kind ==
			SG_BSP_ENTITY_PHYSICS_MONSTER_JUMP);
		CHECK(record && (record->flags & SG_BSP_ENTITY_TOUCH_ACTIVATED));
		CHECK(record && record->landmark_kind == SG_RUNE_LANDMARK_TRIGGER);
		CHECK(record && record->height == 300.0f);
		CHECK(record && record->speed == 250.0f);
		CHECK(record && (record->flags & SG_BSP_ENTITY_INITIALLY_ACTIVE));
		record = FindEntity(semantics, "trigger_hurt");
		CHECK(record && record->damage == 12);
		CHECK(record && record->physics_kind ==
			SG_BSP_ENTITY_PHYSICS_DAMAGE_VOLUME);
		CHECK(record && record->mechanism_role == SG_MECH_NODE_TRIGGER_HURT);
		CHECK(record && (record->flags & SG_BSP_ENTITY_TOUCH_ACTIVATED));
		CHECK(record && (record->flags & SG_BSP_ENTITY_INITIALLY_ACTIVE));
		CHECK(record && !(record->flags & SG_BSP_ENTITY_USE_ACTIVATED));
		record = FindEntity(semantics, "func_conveyor");
		CHECK(record && record->physics_kind ==
			SG_BSP_ENTITY_PHYSICS_CONVEYOR);
		CHECK(record && record->mechanism_role == SG_MECH_NODE_OTHER_MOVER);
		CHECK(record && record->mechanism_kind == SG_RUNE_MECHANISM_KIND_COUNT);
		CHECK(record && !(record->flags &
			SG_BSP_ENTITY_CANONICAL_MECHANISM_KIND));
		record = FindEntity(semantics, "target_laser");
		CHECK(record && record->damage == 9);
		CHECK(record && record->physics_kind ==
			SG_BSP_ENTITY_PHYSICS_DAMAGE_BEAM);
		CHECK(record && record->mechanism_role == SG_MECH_NODE_TARGET_LASER);
	}
	SG_BspEntitySemanticsDestroy(semantics);
	DestroyWorld(&fixture);
}

static void TestTeamsAndFanout(void)
{
	static const char text[] =
		"{ \"classname\" \"worldspawn\" }\n"
		"{ \"classname\" \"func_door\" \"model\" \"*2\" \"team\" \"pair\" \"targetname\" \"gate\" }\n"
		"{ \"classname\" \"func_door\" \"model\" \"*1\" \"team\" \"pair\" \"targetname\" \"gate\" }\n"
		"{ \"classname\" \"trigger_multiple\" \"model\" \"*3\" \"target\" \"gate\" }\n"
		"{ \"classname\" \"path_corner\" \"targetname\" \"path\" \"pathtarget\" \"gate\" }\n";
	world_fixture_t fixture;
	sg_bsp_entity_semantics_t *semantics;
	uint32_t index;
	uint32_t target_edges = 0U;
	uint32_t team_edges = 0U;
	uint32_t path_edges = 0U;

	InitWorld(&fixture, text);
	semantics = Build(&fixture);
	CHECK(semantics != NULL);
	if (semantics)
	{
		for (index = 0; index < semantics->edge_count; index++)
		{
			if (semantics->edges[index].kind == SG_MECH_EDGE_TARGET)
			{
				target_edges++;
				if (semantics->entities[
					semantics->edges[index].destination].bsp_model == 1U)
					CHECK(semantics->edges[index].fanout_ordinal == 1U);
				if (semantics->entities[
					semantics->edges[index].destination].bsp_model == 2U)
					CHECK(semantics->edges[index].fanout_ordinal == 0U);
			}
			if (semantics->edges[index].kind == SG_MECH_EDGE_TEAM)
			{
				team_edges++;
				CHECK(semantics->entities[
					semantics->edges[index].source].bsp_model == 1U);
				CHECK(semantics->entities[
					semantics->edges[index].destination].bsp_model == 2U);
			}
			if (semantics->edges[index].kind == SG_MECH_EDGE_PATH_TARGET)
			{
				path_edges++;
				if (semantics->entities[
					semantics->edges[index].destination].bsp_model == 1U)
					CHECK(semantics->edges[index].fanout_ordinal == 1U);
				if (semantics->entities[
					semantics->edges[index].destination].bsp_model == 2U)
					CHECK(semantics->edges[index].fanout_ordinal == 0U);
			}
		}
		CHECK(target_edges == 2U);
		CHECK(team_edges == 1U);
		CHECK(path_edges == 2U);
	}
	SG_BspEntitySemanticsDestroy(semantics);
	DestroyWorld(&fixture);
}

static void TestSourceFirstTargets(void)
{
	static const char text[] =
		"{ \"classname\" \"worldspawn\" }\n"
		"{ \"classname\" \"misc_teleporter\" \"target\" \"dest\" }\n"
		"{ \"classname\" \"misc_teleporter_dest\" \"targetname\" \"dest\" \"origin\" \"200 0 0\" }\n"
		"{ \"classname\" \"misc_teleporter_dest\" \"targetname\" \"dest\" \"origin\" \"100 0 0\" }\n"
		"{ \"classname\" \"target_laser\" \"target\" \"aim\" }\n"
		"{ \"classname\" \"info_notnull\" \"targetname\" \"aim\" \"origin\" \"400 0 0\" }\n"
		"{ \"classname\" \"info_notnull\" \"targetname\" \"aim\" \"origin\" \"300 0 0\" }\n";
	world_fixture_t fixture;
	sg_bsp_entity_semantics_t *semantics;
	const sg_bsp_entity_semantic_t *teleporter;
	const sg_bsp_entity_semantic_t *laser;
	uint32_t index;
	uint32_t teleporter_edges = 0U;
	uint32_t laser_edges = 0U;

	InitWorld(&fixture, text);
	semantics = Build(&fixture);
	CHECK(semantics != NULL);
	if (semantics)
	{
		teleporter = FindEntity(semantics, "misc_teleporter");
		laser = FindEntity(semantics, "target_laser");
		CHECK(teleporter != NULL);
		CHECK(teleporter &&
			(teleporter->flags & SG_BSP_ENTITY_INITIALLY_ACTIVE));
		CHECK(laser != NULL);
		for (index = 0; index < semantics->edge_count; index++)
		{
			const sg_bsp_entity_semantic_edge_t *edge = &semantics->edges[index];
			uint32_t destination_source;

			if (edge->kind != SG_MECH_EDGE_TARGET)
				continue;
			destination_source = semantics->entities[
				edge->destination].source_entity_ordinal;
			if (teleporter && edge->source == teleporter->canonical_ordinal)
			{
				teleporter_edges++;
				CHECK(destination_source == 2U);
				CHECK(edge->fanout_ordinal == 0U);
			}
			if (laser && edge->source == laser->canonical_ordinal)
			{
				laser_edges++;
				CHECK(destination_source == 5U);
				CHECK(edge->fanout_ordinal == 0U);
			}
		}
		CHECK(teleporter_edges == 1U);
		CHECK(laser_edges == 1U);
	}
	SG_BspEntitySemanticsDestroy(semantics);
	DestroyWorld(&fixture);
}

static void TestHostOrderedPickFanout(void)
{
	static const char text[] =
		"{ \"classname\" \"worldspawn\" }\n"
		"{ \"classname\" \"func_train\" \"model\" \"*1\" \"target\" \"next\" }\n"
		"{ \"classname\" \"path_corner\" \"targetname\" \"start\" \"target\" \"next\" \"origin\" \"-1 0 0\" }\n"
		"{ \"classname\" \"path_corner\" \"targetname\" \"next\" \"origin\" \"90 0 0\" }\n"
		"{ \"classname\" \"path_corner\" \"targetname\" \"next\" \"origin\" \"80 0 0\" }\n"
		"{ \"classname\" \"path_corner\" \"targetname\" \"next\" \"origin\" \"70 0 0\" }\n"
		"{ \"classname\" \"path_corner\" \"targetname\" \"next\" \"origin\" \"60 0 0\" }\n"
		"{ \"classname\" \"path_corner\" \"targetname\" \"next\" \"origin\" \"50 0 0\" }\n"
		"{ \"classname\" \"path_corner\" \"targetname\" \"next\" \"origin\" \"40 0 0\" }\n"
		"{ \"classname\" \"path_corner\" \"targetname\" \"next\" \"origin\" \"30 0 0\" }\n"
		"{ \"classname\" \"path_corner\" \"targetname\" \"next\" \"origin\" \"20 0 0\" }\n"
		"{ \"classname\" \"path_corner\" \"targetname\" \"next\" \"origin\" \"10 0 0\" }\n";
	world_fixture_t fixture;
	sg_bsp_entity_semantics_t *semantics;
	uint32_t index;
	uint32_t train_source = UINT32_MAX;
	uint32_t path_source = UINT32_MAX;
	uint32_t train_edges = 0U;
	uint32_t path_edges = 0U;

	InitWorld(&fixture, text);
	semantics = Build(&fixture);
	CHECK(semantics != NULL);
	if (semantics)
	{
		for (index = 0; index < semantics->entity_count; index++)
		{
			const sg_bsp_entity_semantic_t *record = &semantics->entities[index];
			const char *classname = SG_BspEntitySemanticsString(
				semantics, record->classname);
			const char *targetname = SG_BspEntitySemanticsString(
				semantics, record->targetname);

			if (!strcmp(classname, "func_train"))
				train_source = index;
			if (!strcmp(classname, "path_corner") && targetname &&
				!strcmp(targetname, "start"))
				path_source = index;
		}
		CHECK(train_source != UINT32_MAX);
		CHECK(path_source != UINT32_MAX);
		for (index = 0; index < semantics->edge_count; index++)
		{
			const sg_bsp_entity_semantic_edge_t *edge = &semantics->edges[index];
			uint32_t source_ordinal;

			if (edge->kind != SG_MECH_EDGE_TARGET ||
				(edge->source != train_source && edge->source != path_source))
				continue;
			source_ordinal = semantics->entities[
				edge->destination].source_entity_ordinal;
			CHECK(source_ordinal >= 3U && source_ordinal <= 10U);
			CHECK(edge->fanout_ordinal == source_ordinal - 3U);
			if (edge->source == train_source)
				train_edges++;
			else
				path_edges++;
		}
		CHECK(train_edges == 8U);
		CHECK(path_edges == 8U);
	}
	SG_BspEntitySemanticsDestroy(semantics);
	DestroyWorld(&fixture);
}

static void TestHostSpawnClassSemantics(void)
{
	static const char text[] =
		"{ \"classname\" \"worldspawn\" }\n"
		"{ \"classname\" \"func_water\" \"model\" \"*1\" \"targetname\" \"water\" \"speed\" \"25\" \"wait\" \"-1\" \"lip\" \"0\" }\n"
		"{ \"classname\" \"func_timer\" \"target\" \"event\" \"wait\" \"2\" \"random\" \"0.5\" \"delay\" \"0.1\" \"pausetime\" \"3\" \"spawnflags\" \"1\" }\n"
		"{ \"classname\" \"target_speaker\" \"targetname\" \"event\" }\n"
		"{ \"classname\" \"func_object\" \"model\" \"*2\" \"spawnflags\" \"2\" \"dmg\" \"100\" }\n"
		"{ \"classname\" \"func_explosive\" \"model\" \"*3\" \"speed\" \"5\" \"health\" \"100\" \"dmg\" \"20\" }\n"
		"{ \"classname\" \"trigger_always\" \"target\" \"event\" \"delay\" \"0.2\" }\n"
		"{ \"classname\" \"trigger_key\" \"target\" \"event\" \"item\" \"key_data_cd\" }\n"
		"{ \"classname\" \"trigger_counter\" \"target\" \"event\" \"count\" \"3\" }\n"
		"{ \"classname\" \"trigger_elevator\" \"target\" \"train\" }\n"
		"{ \"classname\" \"func_train\" \"model\" \"*4\" \"targetname\" \"train\" \"target\" \"corner\" }\n"
		"{ \"classname\" \"path_corner\" \"targetname\" \"corner\" }\n"
		"{ \"classname\" \"func_door\" \"model\" \"*5\" }\n"
		"{ \"classname\" \"func_rotating\" \"model\" \"*6\" \"spawnflags\" \"1\" }\n"
		"{ \"classname\" \"func_conveyor\" \"model\" \"*7\" \"spawnflags\" \"1\" }\n"
		"{ \"classname\" \"func_wall\" \"model\" \"*8\" }\n"
		"{ \"classname\" \"func_wall\" \"model\" \"*9\" \"spawnflags\" \"4\" }\n"
		"{ \"classname\" \"target_laser\" \"spawnflags\" \"1\" \"angle\" \"90\" }\n"
		"{ \"classname\" \"trigger_hurt\" \"model\" \"*10\" \"spawnflags\" \"3\" }\n";
	world_fixture_t fixture;
	sg_bsp_entity_semantics_t *semantics;
	const sg_bsp_entity_semantic_t *record;

	InitWorld(&fixture, text);
	semantics = Build(&fixture);
	CHECK(semantics != NULL);
	if (semantics)
	{
		record = FindEntity(semantics, "func_water");
		CHECK(record && record->mechanism_kind == SG_RUNE_MECHANISM_DOOR);
		CHECK(record && record->mechanism_role == SG_MECH_NODE_DOOR_MASTER);
		CHECK(record && (record->flags & SG_BSP_ENTITY_USE_ACTIVATED));
		CHECK(record && (record->flags & SG_BSP_ENTITY_SPEED_DEFINED));
		CHECK(record && (record->flags & SG_BSP_ENTITY_DWELL_DEFINED));
		CHECK(record && (record->flags & SG_BSP_ENTITY_LIP_DEFINED));
		record = FindEntity(semantics, "func_timer");
		CHECK(record && record->mechanism_kind == SG_RUNE_MECHANISM_TRIGGER);
		CHECK(record && record->mechanism_role == SG_MECH_NODE_CONTEXTUAL);
		CHECK(record && record->pause_ms == 3000.0f);
		CHECK(record && (record->flags & SG_BSP_ENTITY_PAUSE_DEFINED));
		CHECK(record && (record->flags & SG_BSP_ENTITY_AUTO_ACTIVATED));
		CHECK(record && (record->flags & SG_BSP_ENTITY_INITIALLY_ACTIVE));
		CHECK(record && (record->flags & SG_BSP_ENTITY_USE_ACTIVATED));
		record = FindEntity(semantics, "func_object");
		CHECK(record && record->mechanism_role == SG_MECH_NODE_OTHER_MOVER);
		CHECK(record && record->mechanism_kind == SG_RUNE_MECHANISM_KIND_COUNT);
		CHECK(record && (record->flags & SG_BSP_ENTITY_AUTO_ACTIVATED));
		CHECK(record && (record->flags & SG_BSP_ENTITY_INITIALLY_ACTIVE));
		CHECK(record && !(record->flags & SG_BSP_ENTITY_USE_ACTIVATED));
		record = FindEntity(semantics, "func_explosive");
		CHECK(record && record->mechanism_role == SG_MECH_NODE_OTHER_MOVER);
		CHECK(record && (record->flags & SG_BSP_ENTITY_DAMAGE_ACTIVATED));
		record = FindEntity(semantics, "trigger_always");
		CHECK(record && (record->flags & SG_BSP_ENTITY_AUTO_ACTIVATED));
		CHECK(record && !(record->flags & SG_BSP_ENTITY_TOUCH_ACTIVATED));
		record = FindEntity(semantics, "trigger_key");
		CHECK(record && (record->flags & SG_BSP_ENTITY_INVENTORY_GATED));
		CHECK(record && !strcmp(SG_BspEntitySemanticsString(semantics,
			record->required_item), "key_data_cd"));
		record = FindEntity(semantics, "trigger_counter");
		CHECK(record && (record->flags & SG_BSP_ENTITY_USE_ACTIVATED));
		record = FindEntity(semantics, "trigger_elevator");
		CHECK(record && record->mechanism_kind == SG_RUNE_MECHANISM_LIFT);
		CHECK(record && (record->flags & SG_BSP_ENTITY_USE_ACTIVATED));
		record = FindEntity(semantics, "func_door");
		CHECK(record && record->speed == 0.0f && record->dwell_ms == 0.0f);
		CHECK(record && !(record->flags & SG_BSP_ENTITY_SPEED_DEFINED));
		CHECK(record && !(record->flags & SG_BSP_ENTITY_DWELL_DEFINED));
		record = FindModel(semantics, 6U);
		CHECK(record && (record->flags & SG_BSP_ENTITY_USE_ACTIVATED));
		CHECK(record && (record->flags & SG_BSP_ENTITY_INITIALLY_ACTIVE));
		record = FindModel(semantics, 7U);
		CHECK(record && (record->flags & SG_BSP_ENTITY_USE_ACTIVATED));
		CHECK(record && (record->flags & SG_BSP_ENTITY_INITIALLY_ACTIVE));
		record = FindModel(semantics, 8U);
		CHECK(record && (record->flags & SG_BSP_ENTITY_INITIALLY_ACTIVE));
		CHECK(record && !(record->flags & SG_BSP_ENTITY_USE_ACTIVATED));
		record = FindModel(semantics, 9U);
		CHECK(record && (record->flags & SG_BSP_ENTITY_INITIALLY_ACTIVE));
		CHECK(record && (record->flags & SG_BSP_ENTITY_USE_ACTIVATED));
		record = FindEntity(semantics, "target_laser");
		CHECK(record && (record->flags & SG_BSP_ENTITY_USE_ACTIVATED));
		CHECK(record && (record->flags & SG_BSP_ENTITY_INITIALLY_ACTIVE));
		record = FindModel(semantics, 10U);
		CHECK(record && record->mechanism_role == SG_MECH_NODE_TRIGGER_HURT);
		CHECK(record && (record->flags & SG_BSP_ENTITY_TOUCH_ACTIVATED));
		CHECK(record && (record->flags & SG_BSP_ENTITY_USE_ACTIVATED));
		CHECK(record && !(record->flags & SG_BSP_ENTITY_INITIALLY_ACTIVE));
	}
	SG_BspEntitySemanticsDestroy(semantics);
	DestroyWorld(&fixture);
}

static void TestHostTopologyPolicy(void)
{
	static const char text[] =
		"{ \"classname\" \"worldspawn\" }\n"
		"{ \"classname\" \"info_null\" \"pathtarget\" \"event\" }\n"
		"{ \"classname\" \"target_speaker\" \"targetname\" \"event\" }\n"
		"{ \"classname\" \"trigger_relay\" \"targetname\" \"self\" \"target\" \"self\" \"killtarget\" \"absent\" }\n"
		"{ \"classname\" \"path_corner\" \"targetname\" \"path\" \"pathtarget\" \"absent\" }\n"
		"{ \"classname\" \"target_spawner\" \"target\" \"event\" }\n"
		"{ \"classname\" \"func_door\" \"model\" \"*1\" \"target\" \"portal\" }\n"
		"{ \"classname\" \"func_areaportal\" \"targetname\" \"portal\" }\n"
		"{ \"classname\" \"func_train\" \"model\" \"*2\" }\n"
		"{ \"classname\" \"func_train\" \"model\" \"*3\" \"target\" \"unresolved\" }\n"
		"{ \"classname\" \"trigger_elevator\" \"target\" \"unresolved\" }\n"
		"{ \"classname\" \"misc_teleporter\" }\n"
		"{ \"classname\" \"trigger_key\" \"target\" \"event\" }\n"
		"{ \"classname\" \"trigger_key\" \"target\" \"event\" \"item\" \"key_not_registered\" }\n"
		"{ \"classname\" \"misc_satellite_dish\" \"target\" \"event\" \"killtarget\" \"event\" }\n"
		"{ \"classname\" \"target_help\" \"targetname\" \"event\" }\n"
		"{ \"classname\" \"target_changelevel\" \"targetname\" \"event\" }\n"
		"{ \"classname\" \"target_changelevel\" \"map\" \"nextmap\" }\n"
		"{ \"classname\" \"unknown_runtime\" \"target\" \"event\" }\n";
	world_fixture_t fixture;
	sg_bsp_entity_semantics_t *semantics;
	uint32_t index;

	InitWorld(&fixture, text);
	semantics = Build(&fixture);
	CHECK(semantics != NULL);
	if (semantics)
	{
		CHECK(FindEntity(semantics, "info_null") == NULL);
		CHECK(FindEntity(semantics, "unknown_runtime") == NULL);
		CHECK(FindEntity(semantics, "misc_teleporter") == NULL);
		CHECK(FindEntity(semantics, "trigger_key") == NULL);
		CHECK(FindEntity(semantics, "target_help") == NULL);
		{
			const sg_bsp_entity_semantic_t *train =
				FindEntity(semantics, "func_train");

			CHECK(train != NULL);
			CHECK(train && (train->flags & SG_BSP_ENTITY_USE_ACTIVATED));
			CHECK(train && !(train->flags & SG_BSP_ENTITY_AUTO_ACTIVATED));
			CHECK(train && !(train->flags & SG_BSP_ENTITY_INITIALLY_ACTIVE));
		}
		{
			const sg_bsp_entity_semantic_t *train = FindModel(semantics, 3U);
			const sg_bsp_entity_semantic_t *elevator =
				FindEntity(semantics, "trigger_elevator");

			CHECK(train && !(train->flags & SG_BSP_ENTITY_AUTO_ACTIVATED));
			CHECK(train && !(train->flags & SG_BSP_ENTITY_INITIALLY_ACTIVE));
			CHECK(elevator && !(elevator->flags & SG_BSP_ENTITY_USE_ACTIVATED));
		}
		{
			const sg_bsp_entity_semantic_t *spawner =
				FindEntity(semantics, "target_spawner");

			CHECK(spawner != NULL);
			CHECK(spawner && !strcmp(SG_BspEntitySemanticsString(semantics,
				spawner->spawned_classname), "event"));
		}
		{
			const sg_bsp_entity_semantic_t *changelevel =
				FindEntity(semantics, "target_changelevel");

			CHECK(changelevel && !strcmp(SG_BspEntitySemanticsString(semantics,
				changelevel->destination_map), "nextmap"));
		}
		CHECK(semantics->edge_count == 0U);
		for (index = 0U; index < semantics->edge_count; index++)
			CHECK(semantics->edges[index].source !=
				semantics->edges[index].destination);
	}
	SG_BspEntitySemanticsDestroy(semantics);
	DestroyWorld(&fixture);
}

static void TestActivationParityAndKeys(void)
{
	static const char text[] =
		"{ \"classname\" \"worldspawn\" }\n"
		"{ \"classname\" \"trigger_multiple\" \"model\" \"*1\" }\n"
		"{ \"classname\" \"trigger_multiple\" \"model\" \"*2\" \"spawnflags\" \"4\" }\n"
		"{ \"classname\" \"trigger_once\" \"model\" \"*3\" \"spawnflags\" \"1\" }\n"
		"{ \"classname\" \"func_door_secret\" \"model\" \"*4\" }\n"
		"{ \"classname\" \"func_train\" \"model\" \"*5\" \"target\" \"next\" }\n"
		"{ \"classname\" \"func_train\" \"model\" \"*6\" \"targetname\" \"held\" \"target\" \"next\" }\n"
		"{ \"classname\" \"path_corner\" \"targetname\" \"next\" }\n"
		"{ \"classname\" \"trigger_gravity\" \"model\" \"*7\" \"gravity\" \"0\" }\n"
		"{ \"classname\" \"key_data_cd\" \"origin\" \"1 2 3\" }\n";
	world_fixture_t fixture;
	sg_bsp_entity_semantics_t *semantics;
	const sg_bsp_entity_semantic_t *record;

	InitWorld(&fixture, text);
	semantics = Build(&fixture);
	CHECK(semantics != NULL);
	if (semantics)
	{
		record = FindModel(semantics, 1U);
		CHECK(record && (record->flags & SG_BSP_ENTITY_TOUCH_ACTIVATED));
		CHECK(record && (record->flags & SG_BSP_ENTITY_USE_ACTIVATED));
		CHECK(record && (record->flags & SG_BSP_ENTITY_INITIALLY_ACTIVE));
		record = FindModel(semantics, 2U);
		CHECK(record && (record->flags & SG_BSP_ENTITY_TOUCH_ACTIVATED));
		CHECK(record && (record->flags & SG_BSP_ENTITY_USE_ACTIVATED));
		CHECK(record && !(record->flags & SG_BSP_ENTITY_INITIALLY_ACTIVE));
		record = FindModel(semantics, 3U);
		CHECK(record && !(record->flags & SG_BSP_ENTITY_INITIALLY_ACTIVE));
		record = FindModel(semantics, 4U);
		CHECK(record && (record->flags & SG_BSP_ENTITY_DAMAGE_ACTIVATED));
		CHECK(record && !(record->flags & SG_BSP_ENTITY_TOUCH_ACTIVATED));
		record = FindModel(semantics, 5U);
		CHECK(record && (record->flags & SG_BSP_ENTITY_USE_ACTIVATED));
		CHECK(record && (record->flags & SG_BSP_ENTITY_AUTO_ACTIVATED));
		CHECK(record && (record->flags & SG_BSP_ENTITY_INITIALLY_ACTIVE));
		record = FindModel(semantics, 6U);
		CHECK(record && (record->flags & SG_BSP_ENTITY_USE_ACTIVATED));
		CHECK(record && !(record->flags & SG_BSP_ENTITY_INITIALLY_ACTIVE));
		record = FindModel(semantics, 7U);
		CHECK(record && record->gravity == 0.0f);
		CHECK(record && (record->flags & SG_BSP_ENTITY_GRAVITY_DEFINED));
		record = FindEntity(semantics, "key_data_cd");
		CHECK(record && record->landmark_kind == SG_RUNE_LANDMARK_ITEM);
	}
	SG_BspEntitySemanticsDestroy(semantics);
	DestroyWorld(&fixture);
}

static void TestAdditionalTargetSelectionPolicies(void)
{
	static const char text[] =
		"{ \"classname\" \"worldspawn\" }\n"
		"{ \"classname\" \"point_combat\" \"target\" \"route\" }\n"
		"{ \"classname\" \"misc_viper\" \"target\" \"route\" }\n"
		"{ \"classname\" \"misc_strogg_ship\" \"target\" \"route\" }\n"
		"{ \"classname\" \"path_corner\" \"targetname\" \"route\" \"origin\" \"20 0 0\" }\n"
		"{ \"classname\" \"path_corner\" \"targetname\" \"route\" \"origin\" \"10 0 0\" }\n"
		"{ \"classname\" \"func_clock\" \"target\" \"clock\" }\n"
		"{ \"classname\" \"target_speaker\" \"targetname\" \"clock\" \"origin\" \"200 0 0\" }\n"
		"{ \"classname\" \"target_speaker\" \"targetname\" \"clock\" \"origin\" \"100 0 0\" }\n"
		"{ \"classname\" \"func_clock\" \"target\" \"clock\" \"spawnflags\" \"2\" }\n"
		"{ \"classname\" \"target_lightramp\" \"target\" \"lamp\" \"message\" \"az\" }\n"
		"{ \"classname\" \"light\" \"targetname\" \"lamp\" \"origin\" \"400 0 0\" }\n"
		"{ \"classname\" \"light\" \"targetname\" \"lamp\" \"origin\" \"300 0 0\" }\n"
		"{ \"classname\" \"target_lightramp\" \"target\" \"lamp\" \"message\" \"aa\" }\n";
	world_fixture_t fixture;
	sg_bsp_entity_semantics_t *semantics;
	uint32_t index;
	uint32_t point_edges = 0U;
	uint32_t viper_edges = 0U;
	uint32_t ship_edges = 0U;
	uint32_t clock_edges = 0U;
	uint32_t ramp_edges = 0U;
	uint32_t clock_records = 0U;
	const sg_bsp_entity_semantic_t *point;
	const sg_bsp_entity_semantic_t *viper;
	const sg_bsp_entity_semantic_t *ship;
	const sg_bsp_entity_semantic_t *clock;
	const sg_bsp_entity_semantic_t *ramp;

	InitWorld(&fixture, text);
	semantics = Build(&fixture);
	CHECK(semantics != NULL);
	if (semantics)
	{
		point = FindEntity(semantics, "point_combat");
		viper = FindEntity(semantics, "misc_viper");
		ship = FindEntity(semantics, "misc_strogg_ship");
		clock = FindEntity(semantics, "func_clock");
		ramp = FindEntity(semantics, "target_lightramp");
		for (index = 0U; index < semantics->entity_count; index++)
			if (!strcmp(SG_BspEntitySemanticsString(semantics,
				semantics->entities[index].classname), "func_clock"))
				clock_records++;
		for (index = 0U; index < semantics->edge_count; index++)
		{
			const sg_bsp_entity_semantic_edge_t *edge = &semantics->edges[index];

			if (edge->kind != SG_MECH_EDGE_TARGET)
				continue;
			if (point && edge->source == point->canonical_ordinal)
				point_edges++;
			if (viper && edge->source == viper->canonical_ordinal)
				viper_edges++;
			if (ship && edge->source == ship->canonical_ordinal)
				ship_edges++;
			if (clock && edge->source == clock->canonical_ordinal)
			{
				clock_edges++;
				CHECK(semantics->entities[
					edge->destination].source_entity_ordinal == 7U);
			}
			if (ramp && edge->source == ramp->canonical_ordinal)
			{
				ramp_edges++;
				CHECK(semantics->entities[
					edge->destination].source_entity_ordinal == 12U);
			}
		}
		CHECK(point_edges == 2U);
		CHECK(viper_edges == 2U);
		CHECK(ship_edges == 2U);
		CHECK(clock_edges == 1U);
		CHECK(clock_records == 1U);
		CHECK(ramp_edges == 1U);
	}
	SG_BspEntitySemanticsDestroy(semantics);
	DestroyWorld(&fixture);
}

static void TestExactRegisteredLandmarks(void)
{
	static const char text[] =
		"{ \"classname\" \"worldspawn\" }\n"
		"{ \"classname\" \"flag\" }\n"
		"{ \"classname\" \"weapon_not_registered\" }\n"
		"{ \"classname\" \"ammo_not_registered\" }\n"
		"{ \"classname\" \"item_not_registered\" }\n"
		"{ \"classname\" \"key_not_registered\" }\n"
		"{ \"classname\" \"mystery_rune\" }\n"
		"{ \"classname\" \"info_player_not_registered\" }\n"
		"{ \"classname\" \"info_position_not_registered\" }\n";
	world_fixture_t fixture;
	sg_bsp_entity_semantics_t *semantics;
	const sg_bsp_entity_semantic_t *record;

	InitWorld(&fixture, text);
	semantics = Build(&fixture);
	CHECK(semantics != NULL);
	CHECK(semantics && semantics->entity_count == 1U);
	record = semantics ? FindEntity(semantics, "flag") : NULL;
	CHECK(record && record->landmark_kind == SG_RUNE_LANDMARK_FLAG_STAND);
	SG_BspEntitySemanticsDestroy(semantics);
	DestroyWorld(&fixture);
}

static void TestDelayedUseTopology(void)
{
	static const char text[] =
		"{ \"classname\" \"worldspawn\" }\n"
		"{ \"classname\" \"func_door\" \"model\" \"*1\" \"targetname\" \"portal\" \"target\" \"portal\" \"delay\" \"1\" }\n"
		"{ \"classname\" \"func_areaportal\" \"targetname\" \"portal\" }\n"
		"{ \"classname\" \"trigger_relay\" \"targetname\" \"self\" \"target\" \"self\" \"delay\" \"1\" }\n";
	world_fixture_t fixture;
	sg_bsp_entity_semantics_t *semantics;
	uint32_t index;
	uint32_t target_edges = 0U;

	InitWorld(&fixture, text);
	semantics = Build(&fixture);
	CHECK(semantics != NULL);
	if (semantics)
	{
		for (index = 0U; index < semantics->edge_count; index++)
			if (semantics->edges[index].kind == SG_MECH_EDGE_TARGET)
				target_edges++;
		CHECK(target_edges == 3U);
	}
	SG_BspEntitySemanticsDestroy(semantics);
	DestroyWorld(&fixture);
}

static void TestOrderIndependentTargetSemantics(void)
{
	static const char first[] =
		"{ \"classname\" \"worldspawn\" }\n"
		"{ \"classname\" \"trigger_relay\" \"target\" \"alpha\" \"delay\" \"1\" }\n"
		"{ \"classname\" \"trigger_relay\" \"target\" \"beta\" \"delay\" \"1\" }\n"
		"{ \"classname\" \"target_speaker\" \"targetname\" \"alpha\" }\n"
		"{ \"classname\" \"target_speaker\" \"targetname\" \"beta\" }\n"
		"{ \"classname\" \"path_corner\" \"targetname\" \"path\" \"pathtarget\" \"alpha\" }\n";
	static const char reordered[] =
		"{ \"classname\" \"worldspawn\" }\n"
		"{ \"classname\" \"target_speaker\" \"targetname\" \"beta\" }\n"
		"{ \"classname\" \"path_corner\" \"targetname\" \"path\" \"pathtarget\" \"alpha\" }\n"
		"{ \"classname\" \"trigger_relay\" \"target\" \"beta\" \"delay\" \"1\" }\n"
		"{ \"classname\" \"target_speaker\" \"targetname\" \"alpha\" }\n"
		"{ \"classname\" \"trigger_relay\" \"target\" \"alpha\" \"delay\" \"1\" }\n";
	world_fixture_t a;
	world_fixture_t b;
	sg_bsp_entity_semantics_t *left;
	sg_bsp_entity_semantics_t *right;
	uint32_t index;
	uint32_t path_edges = 0U;

	InitWorld(&a, first);
	InitWorld(&b, reordered);
	left = Build(&a);
	right = Build(&b);
	CHECK(SameOutput(left, right, 1));
	if (left)
		for (index = 0; index < left->edge_count; index++)
			if (left->edges[index].kind == SG_MECH_EDGE_PATH_TARGET)
				path_edges++;
	CHECK(path_edges == 1U);
	SG_BspEntitySemanticsDestroy(left);
	SG_BspEntitySemanticsDestroy(right);
	DestroyWorld(&a);
	DestroyWorld(&b);
}

static void TestHostParserIdentityParity(void)
{
	static const char escaped[] =
		"{ \"classname\" \"worldspawn\" }\n"
		"{ \"classname\" \"trigger_relay\" \"target\" \"go\\\\q\" }\n"
		"{ \"classname\" \"target_speaker\" \"targetname\" \"go\\\\q\" }\n";
	char target[201];
	char targetname[201];
	char text[640];
	world_fixture_t fixture;
	sg_bsp_entity_semantics_t *semantics;
	size_t index;

	InitWorld(&fixture, escaped);
	semantics = Build(&fixture);
	CHECK(semantics != NULL);
	if (semantics)
	{
		CHECK(semantics->edge_count == 1U);
		CHECK(semantics->edge_count == 0U || !strcmp(
			SG_BspEntitySemanticsString(semantics, semantics->edges[0].name),
			"go\\q"));
	}
	SG_BspEntitySemanticsDestroy(semantics);
	DestroyWorld(&fixture);

	for (index = 0U; index < sizeof(target) - 1U; index++)
	{
		target[index] = 'a';
		targetname[index] = 'a';
	}
	target[sizeof(target) - 1U] = '\0';
	targetname[sizeof(targetname) - 2U] = 'b';
	targetname[sizeof(targetname) - 1U] = '\0';
	CHECK(snprintf(text, sizeof(text),
		"{ \"classname\" \"worldspawn\" } "
		"{ \"classname\" \"trigger_relay\" \"target\" \"%s\" } "
		"{ \"classname\" \"target_speaker\" \"targetname\" \"%s\" }",
		target, targetname) > 0);
	InitWorld(&fixture, text);
	semantics = Build(&fixture);
	CHECK(semantics && semantics->edge_count == 1U);
	CHECK(semantics && semantics->edge_count == 1U &&
		strlen(SG_BspEntitySemanticsString(
			semantics, semantics->edges[0].name)) == 127U);
	SG_BspEntitySemanticsDestroy(semantics);
	DestroyWorld(&fixture);
}

static void ExpectFailure(const char *text,
	sg_bsp_entity_semantics_error_code_t expected)
{
	world_fixture_t fixture;
	sg_bsp_entity_semantics_t sentinel;
	sg_bsp_entity_semantics_t *output = &sentinel;
	sg_bsp_entity_semantics_error_t error;

	InitWorld(&fixture, text);
	CHECK(!SG_BspEntitySemanticsBuild(&fixture.world,
		UINT64_C(0x5354415449434253), &output, &error));
	CHECK(error.code == expected);
	CHECK(output == &sentinel);
	DestroyWorld(&fixture);
}

static void TestFailClosedInputs(void)
{
	ExpectFailure("{\"classname\" \"worldspawn\" }",
		SG_BSP_ENTITY_SEMANTICS_ERROR_MALFORMED_TEXT);
	ExpectFailure("{ \"classname\" \"worldspawn\" } { \"classname\" \"trigger_relay\" \"target\" \"bad\\\" }",
		SG_BSP_ENTITY_SEMANTICS_ERROR_MALFORMED_TEXT);
	ExpectFailure("{ \"classname\" \"worldspawn\" \"}suffix\" \"ignored\" }",
		SG_BSP_ENTITY_SEMANTICS_ERROR_MALFORMED_TEXT);
	ExpectFailure("{ \"classname\" \"worldspawn\" \"message\" \"}suffix\" }",
		SG_BSP_ENTITY_SEMANTICS_ERROR_MALFORMED_TEXT);
	ExpectFailure("{ \"classname\" \"worldspawn\" ",
		SG_BSP_ENTITY_SEMANTICS_ERROR_MALFORMED_TEXT);
	ExpectFailure("{ \"classname\" \"weapon_railgun\" \"ClassName\" \"weapon_bfg\" }",
		SG_BSP_ENTITY_SEMANTICS_ERROR_DUPLICATE_KEY);
	ExpectFailure("{ \"classname\" \"worldspawn\" } { \"classname\" \"func_door\" \"model\" \"*+1\" }",
		SG_BSP_ENTITY_SEMANTICS_ERROR_INVALID_MODEL);
	ExpectFailure("{ \"classname\" \"worldspawn\" } { \"classname\" \"func_door\" \"model\" \"*1\" } { \"classname\" \"func_button\" \"model\" \"*1\" }",
		SG_BSP_ENTITY_SEMANTICS_ERROR_DUPLICATE_MODEL);
	ExpectFailure("{ \"classname\" \"worldspawn\" } { \"classname\" \"item_quad\" \"origin\" \"nan 0 0\" }",
		SG_BSP_ENTITY_SEMANTICS_ERROR_INVALID_VALUE);
	ExpectFailure("{ \"classname\" \"item_quad\" }",
		SG_BSP_ENTITY_SEMANTICS_ERROR_AMBIGUOUS_IDENTITY);
	ExpectFailure("{ \"classname\" \"worldspawn\" } { \"classname\" \"worldspawn\" }",
		SG_BSP_ENTITY_SEMANTICS_ERROR_AMBIGUOUS_IDENTITY);
	ExpectFailure("{ \"classname\" \"worldspawn\" } { \"classname\" \"trigger_push\" \"model\" \"*1\" \"angle\" \"90\" \"angles\" \"0 90 0\" }",
		SG_BSP_ENTITY_SEMANTICS_ERROR_AMBIGUOUS_IDENTITY);
	ExpectFailure("{ \"classname\" \"worldspawn\" } { \"origin\" \"1 2 3\" }",
		SG_BSP_ENTITY_SEMANTICS_ERROR_AMBIGUOUS_IDENTITY);
	CHECK(!SG_BspEntitySemanticsCountsRepresentable(
		(size_t)UINT32_MAX + 1U, 0U, 0U));
	CHECK(!SG_BspEntitySemanticsCountsRepresentable(
		0U, (size_t)UINT32_MAX + 1U, 0U));
	CHECK(!SG_BspEntitySemanticsCountsRepresentable(
		0U, 0U, (size_t)UINT32_MAX + 1U));
}

static void TestRuntimeStateCannotEnterOutput(void)
{
	static const char text[] =
		"// entity authority\n"
		"{ \"classname\" /* key separator */ \"worldspawn\" }\n"
		"{ \"classname\" \"weapon_railgun\" \"origin\" \"1 2 3\" }\n";
	static const char runtime_keys[] =
		"{ \"classname\" \"worldspawn\" }\n"
		"{ \"classname\" \"weapon_railgun\" \"origin\" \"1 2 3\" "
		"\"owner\" \"999\" \"nextthink\" \"42\" \"inuse\" \"0\" }\n";
	struct runtime_state_s
	{
		int inuse;
		float nextthink;
		uint32_t owner;
	} runtime = { 1, 10.0f, 4U };
	world_fixture_t fixture;
	world_fixture_t ignored;
	sg_bsp_entity_semantics_t *before;
	sg_bsp_entity_semantics_t *after;
	sg_bsp_entity_semantics_t *with_runtime_keys;

	InitWorld(&fixture, text);
	InitWorld(&ignored, runtime_keys);
	before = Build(&fixture);
	runtime.inuse = 0;
	runtime.nextthink = 999.0f;
	runtime.owner = UINT32_MAX;
	after = Build(&fixture);
	with_runtime_keys = Build(&ignored);
	CHECK(runtime.owner == UINT32_MAX);
	CHECK(SameOutput(before, after, 0));
	CHECK(SameOutput(before, with_runtime_keys, 0));
	SG_BspEntitySemanticsDestroy(before);
	SG_BspEntitySemanticsDestroy(after);
	SG_BspEntitySemanticsDestroy(with_runtime_keys);
	DestroyWorld(&fixture);
	DestroyWorld(&ignored);
}

int main(void)
{
	TestLandmarksAndCanonicalOrder();
	TestMechanismsAndTopology();
	TestWorldAndMovementPhysics();
	TestTeamsAndFanout();
	TestSourceFirstTargets();
	TestHostOrderedPickFanout();
	TestHostSpawnClassSemantics();
	TestHostTopologyPolicy();
	TestActivationParityAndKeys();
	TestAdditionalTargetSelectionPolicies();
	TestExactRegisteredLandmarks();
	TestDelayedUseTopology();
	TestOrderIndependentTargetSemantics();
	TestHostParserIdentityParity();
	TestFailClosedInputs();
	TestRuntimeStateCannotEnterOutput();
	if (failures)
	{
		fprintf(stderr, "%d BSP entity semantics checks failed\n", failures);
		return 1;
	}
	puts("BSP entity semantics checks passed");
	return 0;
}
