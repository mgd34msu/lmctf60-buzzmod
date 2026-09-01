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

static uint32_t FloatBits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

typedef struct fixture_s
{
	sg_bsp_world_t world;
	uint8_t *embedded_text;
	sg_bsp_model_t models[4];
} fixture_t;

static void ExpectFailure(fixture_t *fixture, const char *text,
	size_t text_bytes, const sg_rune_source_entity_record_t *survivors,
	size_t survivor_count, sg_bsp_entity_semantics_error_code_t code);

static void InitFixture(fixture_t *fixture, const char *embedded_text)
{
	size_t bytes = strlen(embedded_text) + 1U;
	uint32_t model;

	memset(fixture, 0, sizeof(*fixture));
	fixture->embedded_text = malloc(bytes);
	CHECK(fixture->embedded_text != NULL);
	if (!fixture->embedded_text)
		return;
	memcpy(fixture->embedded_text, embedded_text, bytes);
	fixture->world.entities = fixture->embedded_text;
	fixture->world.entity_byte_count = (uint32_t)bytes;
	fixture->world.models = fixture->models;
	fixture->world.model_count =
		(uint32_t)(sizeof(fixture->models) / sizeof(fixture->models[0]));
	for (model = 0U; model < fixture->world.model_count; model++)
	{
		float extent = (float)(model + 8U);

		fixture->models[model].mins.value[0] = -extent;
		fixture->models[model].mins.value[1] = -extent;
		fixture->models[model].mins.value[2] = -4.0f;
		fixture->models[model].maxs.value[0] = extent;
		fixture->models[model].maxs.value[1] = extent;
		fixture->models[model].maxs.value[2] = 20.0f;
	}
}

static void DestroyFixture(fixture_t *fixture)
{
	free(fixture->embedded_text);
	memset(fixture, 0, sizeof(*fixture));
}

static const sg_bsp_entity_semantic_t *FindEntity(
	const sg_bsp_entity_semantics_t *semantics, const char *classname)
{
	uint32_t index;

	for (index = 0U; index < semantics->entity_count; index++)
		if (!strcmp(SG_BspEntitySemanticsString(semantics,
			semantics->entities[index].classname), classname))
			return &semantics->entities[index];
	return NULL;
}

static int SameOutput(const sg_bsp_entity_semantics_t *left,
	const sg_bsp_entity_semantics_t *right)
{
	return left && right &&
		left->source_set_identity == right->source_set_identity &&
		!memcmp(&left->world, &right->world, sizeof(left->world)) &&
		left->entity_count == right->entity_count &&
		left->edge_count == right->edge_count &&
		left->string_bytes == right->string_bytes &&
		(!left->entity_count || !memcmp(left->entities, right->entities,
			(size_t)left->entity_count * sizeof(*left->entities))) &&
		(!left->edge_count || !memcmp(left->edges, right->edges,
			(size_t)left->edge_count * sizeof(*left->edges))) &&
		(!left->string_bytes || !memcmp(left->strings, right->strings,
			left->string_bytes));
}

static sg_bsp_entity_semantics_t *BuildEffective(fixture_t *fixture,
	const char *text, size_t text_bytes,
	const sg_rune_source_entity_record_t *survivors, size_t survivor_count,
	sg_bsp_entity_semantics_error_t *error)
{
	sg_bsp_entity_semantics_t *semantics = NULL;

	CHECK(SG_BspEntitySemanticsBuildEffective(&fixture->world, text,
		text_bytes, survivors, survivor_count,
		UINT64_C(0x4546464543544956), &semantics, error));
	return semantics;
}

static void TestSelectedTextAndOverlayAreTheOnlyEntitySource(void)
{
	static const char embedded[] =
		"{ \"classname\" \"worldspawn\" \"gravity\" \"800\" }\n"
		"{ \"classname\" \"weapon_railgun\" \"origin\" \"1 1 1\" }\n";
	static const char selected[] =
		"{ \"classname\" \"worldspawn\" \"gravity\" \"650\" }\n"
		"{ \"classname\" \"func_wall\" \"model\" \"*1\" "
			"\"origin\" \"10 20 30\" \"spawnflags\" \"0\" }\n"
		"{ \"classname\" \"weapon_bfg\" \"origin\" \"4 5 6\" }\n";
	static const sg_rune_source_entity_record_t survivors[] = {
		{ 0U, 0 }, { 1U, 5 }
	};
	fixture_t fixture;
	sg_bsp_entity_semantics_error_t error;
	sg_bsp_entity_semantics_t *first;
	sg_bsp_entity_semantics_t *second;
	const sg_bsp_entity_semantic_t *wall;

	InitFixture(&fixture, embedded);
	fixture.world.entities = NULL;
	fixture.world.entity_byte_count = 0U;
	first = BuildEffective(&fixture, selected, sizeof(selected), survivors,
		sizeof(survivors) / sizeof(survivors[0]), &error);
	second = BuildEffective(&fixture, selected, sizeof(selected), survivors,
		sizeof(survivors) / sizeof(survivors[0]), &error);
	CHECK(first != NULL);
	CHECK(second != NULL);
	CHECK(SameOutput(first, second));
	CHECK(first && first->world.source_entity_ordinal == 0U);
	CHECK(first && first->world.gravity == 650.0f);
	CHECK(first && first->entity_count == 1U);
	CHECK(first && FindEntity(first, "weapon_railgun") == NULL);
	CHECK(first && FindEntity(first, "weapon_bfg") == NULL);
	wall = first ? FindEntity(first, "func_wall") : NULL;
	CHECK(wall != NULL);
	CHECK(wall && wall->source_entity_ordinal == 1U);
	CHECK(wall && wall->spawnflags == 5U);
	CHECK(wall && (wall->flags & SG_BSP_ENTITY_SPAWNFLAGS_DEFINED));
	CHECK(wall && (wall->flags & SG_BSP_ENTITY_USE_ACTIVATED));
	CHECK(wall && (wall->flags & SG_BSP_ENTITY_INITIALLY_ACTIVE));
	CHECK(wall && wall->bsp_model == 1U);
	CHECK(wall && wall->origin.value[0] == 10.0f);
	CHECK(wall && wall->bounds.mins.value[0] == 1.0f);
	SG_BspEntitySemanticsDestroy(second);
	SG_BspEntitySemanticsDestroy(first);
	DestroyFixture(&fixture);
}

static void TestAppendedDeclarationAndInhibitedGapKeepSourceOrdinals(void)
{
	static const char embedded[] =
		"{ \"classname\" \"worldspawn\" }\n";
	static const char selected[] =
		"{ \"classname\" \"worldspawn\" }\n"
		"{ \"classname\" \"weapon_railgun\" \"origin\" \"1 2 3\" }\n"
		"{ \"classname\" \"weapon_bfg\" \"origin\" \"7 8 9\" }\n";
	static const sg_rune_source_entity_record_t survivors[] = {
		{ 0U, 0 }, { 2U, 0 }
	};
	fixture_t fixture;
	sg_bsp_entity_semantics_error_t error;
	sg_bsp_entity_semantics_t *semantics;
	const sg_bsp_entity_semantic_t *bfg;

	InitFixture(&fixture, embedded);
	semantics = BuildEffective(&fixture, selected, sizeof(selected), survivors,
		sizeof(survivors) / sizeof(survivors[0]), &error);
	CHECK(semantics && semantics->entity_count == 1U);
	CHECK(semantics && FindEntity(semantics, "weapon_railgun") == NULL);
	bfg = semantics ? FindEntity(semantics, "weapon_bfg") : NULL;
	CHECK(bfg && bfg->source_entity_ordinal == 2U);
	CHECK(bfg && bfg->origin.value[2] == 9.0f);
	SG_BspEntitySemanticsDestroy(semantics);
	DestroyFixture(&fixture);
}

static void TestEffectiveAngularMoverSchedules(void)
{
	static const char embedded[] =
		"{ \"classname\" \"worldspawn\" }\n";
	static const char selected[] =
		"{ \"classname\" \"worldspawn\" }\n"
		"{ \"classname\" \"func_door_rotating\" \"model\" \"*1\" "
			"\"angles\" \"10 20 30\" \"distance\" \"45\" "
			"\"speed\" \"120\" \"accel\" \"0\" \"decel\" \"7\" "
			"\"spawnflags\" \"0\" }\n"
		"{ \"classname\" \"func_rotating\" \"model\" \"*2\" "
			"\"angles\" \"1 2 3\" \"spawnflags\" \"0\" }\n"
		"{ \"classname\" \"func_door_rotating\" \"model\" \"*3\" "
			"\"distance\" \"90\" }\n";
	static const sg_rune_source_entity_record_t survivors[] = {
		{ 0U, 0 }, { 1U, 231 }, { 2U, 55 }
	};
	fixture_t fixture;
	sg_bsp_entity_semantics_error_t error;
	sg_bsp_entity_semantics_t *semantics;
	const sg_bsp_entity_semantic_t *door;
	const sg_bsp_entity_semantic_t *rotator;
	const sg_bsp_entity_angular_mover_t *mover;

	InitFixture(&fixture, embedded);
	semantics = BuildEffective(&fixture, selected, sizeof(selected), survivors,
		sizeof(survivors) / sizeof(survivors[0]), &error);
	CHECK(semantics != NULL);
	CHECK(semantics && semantics->entity_count == 2U);
	door = semantics ? FindEntity(semantics, "func_door_rotating") : NULL;
	rotator = semantics ? FindEntity(semantics, "func_rotating") : NULL;
	CHECK(door != NULL);
	CHECK(rotator != NULL);
	mover = door ? SG_BspEntitySemanticsAngularMover(semantics,
		door->canonical_ordinal) : NULL;
	CHECK(mover == (door ? &door->angular_mover : NULL));
	CHECK(mover && mover->kind == SG_BSP_ENTITY_ANGULAR_MOVER_FINITE_DOOR);
	CHECK(mover && mover->flags == (SG_BSP_ENTITY_ANGULAR_MOVER_START_OPEN |
		SG_BSP_ENTITY_ANGULAR_MOVER_REVERSE |
		SG_BSP_ENTITY_ANGULAR_MOVER_TOGGLE |
		SG_BSP_ENTITY_ANGULAR_MOVER_CRUSHER));
	CHECK(mover && mover->schedule.finite_door.frame_ms == 100U);
	CHECK(mover && FloatBits(mover->schedule.finite_door.axis.value[2]) ==
		UINT32_C(0x3f800000));
	CHECK(mover && FloatBits(mover->schedule.finite_door.inactive_angles.value[0]) ==
		UINT32_C(0));
	CHECK(mover && FloatBits(mover->schedule.finite_door.inactive_angles.value[1]) ==
		UINT32_C(0));
	CHECK(mover && FloatBits(mover->schedule.finite_door.active_angles.value[0]) ==
		UINT32_C(0));
	CHECK(mover && FloatBits(mover->schedule.finite_door.active_angles.value[1]) ==
		UINT32_C(0));
	CHECK(mover && FloatBits(mover->schedule.finite_door.inactive_angles.value[2]) ==
		UINT32_C(0xc2340000));
	CHECK(mover && FloatBits(mover->schedule.finite_door.active_angles.value[2]) ==
		UINT32_C(0));
	CHECK(mover && FloatBits(mover->schedule.finite_door.angular_displacement.value[2]) ==
		UINT32_C(0x42340000));
	CHECK(mover && FloatBits(mover->schedule.finite_door.speed) ==
		UINT32_C(0x42f00000));
	CHECK(mover && FloatBits(mover->schedule.finite_door.acceleration) ==
		UINT32_C(0x42f00000));
	CHECK(mover && FloatBits(mover->schedule.finite_door.deceleration) ==
		UINT32_C(0x40e00000));
	mover = rotator ? SG_BspEntitySemanticsAngularMover(semantics,
		rotator->canonical_ordinal) : NULL;
	CHECK(mover == (rotator ? &rotator->angular_mover : NULL));
	CHECK(mover && mover->kind ==
		SG_BSP_ENTITY_ANGULAR_MOVER_CONTINUOUS_ROTATOR);
	CHECK(mover && mover->flags == (SG_BSP_ENTITY_ANGULAR_MOVER_START_ON |
		SG_BSP_ENTITY_ANGULAR_MOVER_REVERSE |
		SG_BSP_ENTITY_ANGULAR_MOVER_STOP_ON_BLOCK |
		SG_BSP_ENTITY_ANGULAR_MOVER_TOUCH_DAMAGE));
	CHECK(mover && mover->schedule.continuous_rotator.frame_ms == 100U);
	CHECK(mover && FloatBits(mover->schedule.continuous_rotator.speed) ==
		UINT32_C(0x42c80000));
	CHECK(mover && FloatBits(mover->schedule.continuous_rotator.initial_angles.value[0]) ==
		UINT32_C(0x3f800000));
	CHECK(mover && FloatBits(mover->schedule.continuous_rotator.axis.value[2]) ==
		UINT32_C(0xbf800000));
	CHECK(mover && FloatBits(mover->schedule.continuous_rotator.angular_velocity.value[2]) ==
		UINT32_C(0xc2c80000));
	CHECK(mover && FloatBits(mover->schedule.continuous_rotator.frame_angular_delta.value[2]) ==
		UINT32_C(0xc1200000));
	CHECK(SG_BspEntitySemanticsAngularMover(semantics, UINT32_MAX) == NULL);
	SG_BspEntitySemanticsDestroy(semantics);
	DestroyFixture(&fixture);
}

static void TestMalformedEffectiveAngularMoverFailsTransactionally(void)
{
	static const char embedded[] =
		"{ \"classname\" \"worldspawn\" }\n";
	static const char selected[] =
		"{ \"classname\" \"worldspawn\" }\n"
		"{ \"classname\" \"func_rotating\" \"model\" \"*1\" "
			"\"speed\" \"1e9999\" }\n";
	static const sg_rune_source_entity_record_t survivors[] = {
		{ 0U, 0 }, { 1U, 0 }
	};
	fixture_t fixture;

	InitFixture(&fixture, embedded);
	ExpectFailure(&fixture, selected, sizeof(selected), survivors,
		sizeof(survivors) / sizeof(survivors[0]),
		SG_BSP_ENTITY_SEMANTICS_ERROR_INVALID_VALUE);
	DestroyFixture(&fixture);
}

static void ExpectFailure(fixture_t *fixture, const char *text,
	size_t text_bytes, const sg_rune_source_entity_record_t *survivors,
	size_t survivor_count, sg_bsp_entity_semantics_error_code_t code)
{
	sg_bsp_entity_semantics_t sentinel;
	sg_bsp_entity_semantics_t *output = &sentinel;
	sg_bsp_entity_semantics_error_t error;

	CHECK(!SG_BspEntitySemanticsBuildEffective(&fixture->world, text,
		text_bytes, survivors, survivor_count,
		UINT64_C(0x4546464543544956), &output, &error));
	CHECK(output == &sentinel);
	CHECK(error.code == code);
}

static void TestMalformedEffectiveSourceFailsTransactionally(void)
{
	static const char embedded[] =
		"{ \"classname\" \"worldspawn\" }\n";
	static const char selected[] =
		"{ \"classname\" \"worldspawn\" }\n"
		"{ \"classname\" \"weapon_bfg\" }\n";
	static const char no_world[] =
		"{ \"classname\" \"weapon_bfg\" }\n";
	static const sg_rune_source_entity_record_t valid[] = {
		{ 0U, 0 }, { 1U, 0 }
	};
	static const sg_rune_source_entity_record_t missing_world[] = {
		{ 1U, 0 }
	};
	static const sg_rune_source_entity_record_t ordinal_zero[] = {
		{ 0U, 0 }
	};
	static const sg_rune_source_entity_record_t duplicate[] = {
		{ 0U, 0 }, { 1U, 0 }, { 1U, 4 }
	};
	static const sg_rune_source_entity_record_t descending[] = {
		{ 0U, 0 }, { 2U, 0 }, { 1U, 0 }
	};
	static const sg_rune_source_entity_record_t out_of_range[] = {
		{ 0U, 0 }, { 2U, 0 }
	};
	char embedded_nul[sizeof(selected) + 1U];
	fixture_t fixture;

	InitFixture(&fixture, embedded);
	ExpectFailure(&fixture, selected, sizeof(selected), NULL, 1U,
		SG_BSP_ENTITY_SEMANTICS_ERROR_INVALID_ARGUMENT);
	ExpectFailure(&fixture, selected, sizeof(selected), valid, 0U,
		SG_BSP_ENTITY_SEMANTICS_ERROR_MISSING_WORLD_RECORD);
	ExpectFailure(&fixture, selected, sizeof(selected), missing_world,
		sizeof(missing_world) / sizeof(missing_world[0]),
		SG_BSP_ENTITY_SEMANTICS_ERROR_MISSING_WORLD_RECORD);
	ExpectFailure(&fixture, selected, sizeof(selected), duplicate,
		sizeof(duplicate) / sizeof(duplicate[0]),
		SG_BSP_ENTITY_SEMANTICS_ERROR_INVALID_RECORD_ORDER);
	ExpectFailure(&fixture, selected, sizeof(selected), descending,
		sizeof(descending) / sizeof(descending[0]),
		SG_BSP_ENTITY_SEMANTICS_ERROR_INVALID_RECORD_ORDER);
	ExpectFailure(&fixture, selected, sizeof(selected), out_of_range,
		sizeof(out_of_range) / sizeof(out_of_range[0]),
		SG_BSP_ENTITY_SEMANTICS_ERROR_INVALID_RECORD_RANGE);
	ExpectFailure(&fixture, selected, sizeof(selected) - 1U, valid,
		sizeof(valid) / sizeof(valid[0]),
		SG_BSP_ENTITY_SEMANTICS_ERROR_MALFORMED_TEXT);
	memcpy(embedded_nul, selected, sizeof(selected));
	embedded_nul[4] = '\0';
	embedded_nul[sizeof(selected)] = '\0';
	ExpectFailure(&fixture, embedded_nul, sizeof(embedded_nul), valid,
		sizeof(valid) / sizeof(valid[0]),
		SG_BSP_ENTITY_SEMANTICS_ERROR_MALFORMED_TEXT);
	ExpectFailure(&fixture, no_world, sizeof(no_world),
		ordinal_zero, sizeof(ordinal_zero) / sizeof(ordinal_zero[0]),
		SG_BSP_ENTITY_SEMANTICS_ERROR_MISSING_WORLD_RECORD);
	DestroyFixture(&fixture);
}

int main(void)
{
	TestSelectedTextAndOverlayAreTheOnlyEntitySource();
	TestAppendedDeclarationAndInhibitedGapKeepSourceOrdinals();
	TestEffectiveAngularMoverSchedules();
	TestMalformedEffectiveAngularMoverFailsTransactionally();
	TestMalformedEffectiveSourceFailsTransactionally();
	if (failures)
	{
		fprintf(stderr, "%d effective entity semantics checks failed\n",
			failures);
		return 1;
	}
	puts("effective entity semantics checks passed");
	return 0;
}
