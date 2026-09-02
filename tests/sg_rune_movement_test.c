/* Era-4 movement: which capability each crossing gets, from the crossing's
 * own facts, and the profiles they share. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "slipgate/sg_rune_movement.h"

static int failures;

#define CHECK(condition) \
	do { \
		if (!(condition)) { \
			failures++; \
			fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, \
				__LINE__, #condition); \
		} \
	} while (0)

static sg_rune_move_crossing_t Level(void)
{
	sg_rune_move_crossing_t crossing;

	memset(&crossing, 0, sizeof(crossing));
	crossing.cell = 0U;
	crossing.other_cell = 1U;
	crossing.portal = 0U;
	crossing.cell_stances = SG_RUNE_MOVE_STANDING | SG_RUNE_MOVE_CROUCHING;
	crossing.other_stances = crossing.cell_stances;
	crossing.portal_stances = crossing.cell_stances;
	crossing.source_supported = 1;
	crossing.target_supported = 1;
	crossing.vertical_facet = 1;
	return crossing;
}

static uint32_t Count(const sg_rune_move_store_t *store,
	sg_rune_move_kind_t kind)
{
	uint32_t index, count = 0U;

	for (index = 0U; index < store->capability_count; index++)
		if (store->capabilities[index].kind == kind)
			count++;
	return count;
}

static void Reset(sg_rune_move_store_t *store)
{
	store->capability_count = 0U;
}

int main(void)
{
	sg_rune_move_store_t store;
	sg_rune_move_law_t law = { 800.0f, 100U, 25U };
	sg_rune_move_crossing_t crossing;
	sg_rune_move_table_t table;
	float inputs[SG_RUNE_FN_INPUT_COUNT];
	float value;

	CHECK(SG_RuneMoveStoreInit(&store, &law));
	CHECK(store.profile_count == 5U);
	CHECK(fabsf(store.jump_rise - 45.5625f) < 1e-3f);
	CHECK(store.rocket_rise > 200.0f && store.rocket_rise < 240.0f);

	/* Level, both stances: a standing walk and a crouching crouch. */
	crossing = Level();
	CHECK(SG_RuneMoveEmitCrossing(&store, &crossing));
	CHECK(Count(&store, SG_RUNE_MOVE_WALK) == 1U);
	CHECK(Count(&store, SG_RUNE_MOVE_CROUCH) == 1U);
	CHECK(store.capability_count == 2U);
	CHECK(store.capabilities[0].source_stances == SG_RUNE_MOVE_STANDING);
	CHECK(store.capabilities[0].destination_stances == SG_RUNE_MOVE_STANDING);

	/* A crouch-only far side keeps the walk out and crouches through. */
	Reset(&store);
	crossing.other_stances = SG_RUNE_MOVE_CROUCHING;
	crossing.portal_stances = SG_RUNE_MOVE_CROUCHING;
	CHECK(SG_RuneMoveEmitCrossing(&store, &crossing));
	CHECK(Count(&store, SG_RUNE_MOVE_WALK) == 0U);
	CHECK(Count(&store, SG_RUNE_MOVE_CROUCH) == 1U);

	/* A step within reach walks; a ledge within the jump rise jumps and is
	 * offered a rocket jump beside it; beyond the jump rise only the rocket
	 * jump; beyond the rocket rise nothing upward. */
	Reset(&store);
	crossing = Level();
	crossing.floor_delta = 16.0f;
	CHECK(SG_RuneMoveEmitCrossing(&store, &crossing));
	CHECK(Count(&store, SG_RUNE_MOVE_WALK) == 1U);
	Reset(&store);
	crossing.floor_delta = 40.0f;
	CHECK(SG_RuneMoveEmitCrossing(&store, &crossing));
	CHECK(Count(&store, SG_RUNE_MOVE_JUMP) == 2U);
	CHECK(Count(&store, SG_RUNE_MOVE_ROCKET_JUMP) == 2U);
	Reset(&store);
	crossing.floor_delta = 100.0f;
	CHECK(SG_RuneMoveEmitCrossing(&store, &crossing));
	CHECK(Count(&store, SG_RUNE_MOVE_JUMP) == 0U);
	CHECK(Count(&store, SG_RUNE_MOVE_ROCKET_JUMP) == 2U);
	Reset(&store);
	crossing.floor_delta = 400.0f;
	CHECK(SG_RuneMoveEmitCrossing(&store, &crossing));
	CHECK(store.capability_count == 0U);
	/* Downward is a drop. */
	Reset(&store);
	crossing.floor_delta = -100.0f;
	CHECK(SG_RuneMoveEmitCrossing(&store, &crossing));
	CHECK(Count(&store, SG_RUNE_MOVE_DROP) == 2U);

	/* Water both sides swims; one side water does not. */
	Reset(&store);
	crossing = Level();
	crossing.source_water = 1;
	crossing.target_water = 1;
	CHECK(SG_RuneMoveEmitCrossing(&store, &crossing));
	CHECK(Count(&store, SG_RUNE_MOVE_SWIM) == 2U);
	Reset(&store);
	crossing.target_water = 0;
	CHECK(SG_RuneMoveEmitCrossing(&store, &crossing));
	CHECK(Count(&store, SG_RUNE_MOVE_SWIM) == 0U);
	CHECK(Count(&store, SG_RUNE_MOVE_WALK) == 1U);

	/* Off an edge through a partition drops; up through a floor jumps and
	 * may rocket jump; airborne into anything is air control. */
	Reset(&store);
	crossing = Level();
	crossing.target_supported = 0;
	CHECK(SG_RuneMoveEmitCrossing(&store, &crossing));
	CHECK(Count(&store, SG_RUNE_MOVE_DROP) == 2U);
	Reset(&store);
	crossing.vertical_facet = 0;
	CHECK(SG_RuneMoveEmitCrossing(&store, &crossing));
	CHECK(Count(&store, SG_RUNE_MOVE_JUMP) == 2U);
	CHECK(Count(&store, SG_RUNE_MOVE_ROCKET_JUMP) == 2U);
	Reset(&store);
	crossing = Level();
	crossing.source_supported = 0;
	CHECK(SG_RuneMoveEmitCrossing(&store, &crossing));
	CHECK(Count(&store, SG_RUNE_MOVE_AIR_CONTROL) == 2U);

	/* The jump profile: z after 0.1 s from rest is 22 exactly, the
	 * substep-exact value, and the velocity is 190. */
	SG_RuneMoveStoreView(&store, &table);
	CHECK(SG_RuneFnTableValid(&table.analytic));
	memset(inputs, 0, sizeof(inputs));
	inputs[SG_RUNE_FN_INPUT_TIME_SECONDS] = 0.1f;
	CHECK(SG_RuneFnEvaluate(&table.analytic,
		table.profiles[3].position[2], inputs, &value));
	CHECK(fabsf(value - 22.0f) < 1e-3f);
	CHECK(SG_RuneFnEvaluate(&table.analytic,
		table.profiles[3].velocity[2], inputs, &value));
	CHECK(fabsf(value - 190.0f) < 1e-3f);
	/* Walking cost is distance over the speed clamp. */
	inputs[SG_RUNE_FN_INPUT_DISTANCE] = 300.0f;
	CHECK(SG_RuneFnEvaluate(&table.analytic, table.profiles[0].cost,
		inputs, &value));
	CHECK(fabsf(value - 1.0f) < 1e-4f);

	SG_RuneMoveStoreFree(&store);
	if (failures)
		return 1;
	puts("sg_rune_move_test: ok");
	return 0;
}
