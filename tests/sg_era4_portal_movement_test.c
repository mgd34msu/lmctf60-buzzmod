/* Era 4: ordinary player movement is published across portals.
 *
 * The era-3 movement harness is included only for its construction fixture
 * (cells, facets, portals, regions, host law). Its own main is renamed and
 * never run; every assertion below is era 4's statement of the behaviour. */
int era3_movement_fields_main(void);
#define main era3_movement_fields_main
#include "sg_rune_compact_movement_fields_test.c"
#undef main

static void Era4CheckLevelCrossing(void)
{
	movement_fixture_t fixture;
	sg_rune_compact_movement_fields_t *fields = NULL;
	sg_rune_compact_movement_fields_view_t view;
	sg_rune_compact_movement_fields_error_t error;

	InitFixture(&fixture);
	CHECK(SG_RuneCompactMovementFieldsBuild(&fixture.input, &fields, &error));
	if (fields != NULL && SG_RuneCompactMovementFieldsRead(fields, &view)) {
		const sg_rune_movement_capability_t *walk = FindFieldForStance(&view,
			0U, 0U, SG_RUNE_MOVEMENT_CAPABILITY_WALK,
			SG_RUNE_STANCE_VALID_STANDING);
		const sg_rune_movement_capability_t *crouch = FindFieldForStance(
			&view, 1U, 0U, SG_RUNE_MOVEMENT_CAPABILITY_CROUCH,
			SG_RUNE_STANCE_VALID_CROUCHING);

		/* One vertical portal between level supported cells, both stances
		 * admitted: each direction gets a standing WALK and a crouching
		 * CROUCH. Nothing is stepped, sunk, airborne, or water on both
		 * sides, so no other family applies. RAMP waits on the support
		 * plane's slope. */
		CHECK(CountFamily(&view, SG_RUNE_MOVEMENT_CAPABILITY_WALK) == 2U);
		CHECK(CountFamily(&view, SG_RUNE_MOVEMENT_CAPABILITY_CROUCH) == 2U);
		CHECK(CountFamily(&view, SG_RUNE_MOVEMENT_CAPABILITY_RAMP) == 0U);
		CHECK(CountFamily(&view, SG_RUNE_MOVEMENT_CAPABILITY_JUMP) == 0U);
		CHECK(CountFamily(&view, SG_RUNE_MOVEMENT_CAPABILITY_DROP) == 0U);
		CHECK(CountFamily(&view, SG_RUNE_MOVEMENT_CAPABILITY_SWIM) == 0U);
		CHECK(CountFamily(&view,
			SG_RUNE_MOVEMENT_CAPABILITY_AIR_CONTROL) == 0U);
		/* A crossing keeps its stance when the far side allows it. */
		CHECK(walk != NULL && walk->destination_stances ==
			SG_RUNE_STANCE_VALID_STANDING && walk->fibers.count != 0U);
		CHECK(crouch != NULL && crouch->destination_stances ==
			SG_RUNE_STANCE_VALID_CROUCHING && crouch->fibers.count != 0U);
	}
	SG_RuneCompactMovementFieldsDestroy(fields);
}

/* Some places are reachable only crouched. A portal admitting just the
 * crouching hull is still crossed, as CROUCH, in both directions, and is
 * never published as a walk. */
static void Era4CheckCrouchOnlyPassage(void)
{
	movement_fixture_t fixture;
	sg_rune_compact_movement_fields_t *fields = NULL;
	sg_rune_compact_movement_fields_view_t view;
	sg_rune_compact_movement_fields_error_t error;

	InitFixture(&fixture);
	fixture.portals[0].valid_stances = SG_RUNE_STANCE_VALID_CROUCHING;
	CHECK(SG_RuneCompactMovementFieldsBuild(&fixture.input, &fields, &error));
	if (fields != NULL && SG_RuneCompactMovementFieldsRead(fields, &view)) {
		const sg_rune_movement_capability_t *forward = FindFieldForStance(
			&view, 0U, 0U, SG_RUNE_MOVEMENT_CAPABILITY_CROUCH,
			SG_RUNE_STANCE_VALID_CROUCHING);
		const sg_rune_movement_capability_t *back = FindFieldForStance(
			&view, 1U, 0U, SG_RUNE_MOVEMENT_CAPABILITY_CROUCH,
			SG_RUNE_STANCE_VALID_CROUCHING);

		CHECK(CountFamily(&view, SG_RUNE_MOVEMENT_CAPABILITY_WALK) == 0U);
		CHECK(CountFamily(&view, SG_RUNE_MOVEMENT_CAPABILITY_CROUCH) == 2U);
		CHECK(forward != NULL && forward->destination_stances ==
			SG_RUNE_STANCE_VALID_CROUCHING);
		CHECK(back != NULL && back->destination_stances ==
			SG_RUNE_STANCE_VALID_CROUCHING);
	}
	SG_RuneCompactMovementFieldsDestroy(fields);
}

/* A current on the water cell does not make a crossing with a dry far side
 * swim, and does not remove walking. */
static void Era4CheckCurrentDoesNotRemoveWalking(void)
{
	movement_fixture_t fixture;
	sg_rune_compact_movement_fields_t *fields = NULL;
	sg_rune_compact_movement_fields_view_t view;
	sg_rune_compact_movement_fields_error_t error;

	InitFixture(&fixture);
	fixture.cells[1].contents |= SG_RUNE_COMPACT_CONTENTS_CURRENT_90;
	CHECK(SG_RuneCompactMovementFieldsBuild(&fixture.input, &fields, &error));
	if (fields != NULL && SG_RuneCompactMovementFieldsRead(fields, &view)) {
		CHECK(CountFamily(&view, SG_RUNE_MOVEMENT_CAPABILITY_SWIM) == 0U);
		CHECK(CountFamily(&view, SG_RUNE_MOVEMENT_CAPABILITY_WALK) == 2U);
	}
	SG_RuneCompactMovementFieldsDestroy(fields);
}

/* A ledge higher than a jump reaches but lower than a rocket jump's peak
 * is crossed upward only by ROCKET_JUMP, and downward by DROP.  Rocket jump
 * is a thing a player can do, so it is a thing the RUNE says exists; whether
 * a bot has the launcher, a rocket, and the health is the tactic's call. */
static void Era4CheckRocketJumpLedge(void)
{
	movement_fixture_t fixture;
	sg_rune_compact_movement_fields_t *fields = NULL;
	sg_rune_compact_movement_fields_view_t view;
	sg_rune_compact_movement_fields_error_t error;

	InitFixture(&fixture);
	fixture.cells[1].bounds.mins.value[2] = 100;
	fixture.cells[1].bounds.maxs.value[2] = 100 + 128;
	fixture.regions[1].bounds.mins.value[2] = 100.0f;
	fixture.regions[1].bounds.maxs.value[2] = 100.0f + 128.0f;
	CHECK(SG_RuneCompactMovementFieldsBuild(&fixture.input, &fields, &error));
	if (fields != NULL && SG_RuneCompactMovementFieldsRead(fields, &view)) {
		const sg_rune_movement_capability_t *up = FindFieldForStance(&view,
			0U, 0U, SG_RUNE_MOVEMENT_CAPABILITY_ROCKET_JUMP,
			SG_RUNE_STANCE_VALID_STANDING);
		const sg_rune_movement_capability_t *down = FindFieldForStance(&view,
			1U, 0U, SG_RUNE_MOVEMENT_CAPABILITY_DROP,
			SG_RUNE_STANCE_VALID_STANDING);

		CHECK(CountFamily(&view, SG_RUNE_MOVEMENT_CAPABILITY_WALK) == 0U);
		CHECK(CountFamily(&view, SG_RUNE_MOVEMENT_CAPABILITY_JUMP) == 0U);
		CHECK(CountFamily(&view,
			SG_RUNE_MOVEMENT_CAPABILITY_ROCKET_JUMP) == 2U);
		CHECK(CountFamily(&view, SG_RUNE_MOVEMENT_CAPABILITY_DROP) == 2U);
		CHECK(up != NULL && up->cell.value == 0U && up->fibers.count != 0U);
		CHECK(down != NULL && down->cell.value == 1U);
	}
	SG_RuneCompactMovementFieldsDestroy(fields);
}

/* Where a plain jump reaches, the rocket jump is offered beside it: two ways
 * to make the same crossing, ranked at runtime by cost and by what the body
 * carries.  A ledge beyond the rocket jump's peak is not crossed upward. */
static void Era4CheckRocketJumpOffersOverJump(void)
{
	movement_fixture_t fixture;
	sg_rune_compact_movement_fields_t *fields = NULL;
	sg_rune_compact_movement_fields_view_t view;
	sg_rune_compact_movement_fields_error_t error;

	InitFixture(&fixture);
	fixture.cells[1].bounds.mins.value[2] = 40;
	fixture.cells[1].bounds.maxs.value[2] = 40 + 128;
	fixture.regions[1].bounds.mins.value[2] = 40.0f;
	fixture.regions[1].bounds.maxs.value[2] = 40.0f + 128.0f;
	CHECK(SG_RuneCompactMovementFieldsBuild(&fixture.input, &fields, &error));
	if (fields != NULL && SG_RuneCompactMovementFieldsRead(fields, &view)) {
		CHECK(CountFamily(&view, SG_RUNE_MOVEMENT_CAPABILITY_JUMP) == 2U);
		CHECK(CountFamily(&view,
			SG_RUNE_MOVEMENT_CAPABILITY_ROCKET_JUMP) == 2U);
		CHECK(CountFamily(&view, SG_RUNE_MOVEMENT_CAPABILITY_DROP) == 2U);
	}
	SG_RuneCompactMovementFieldsDestroy(fields);
	fields = NULL;

	InitFixture(&fixture);
	fixture.cells[1].bounds.mins.value[2] = 400;
	fixture.cells[1].bounds.maxs.value[2] = 400 + 128;
	fixture.regions[1].bounds.mins.value[2] = 400.0f;
	fixture.regions[1].bounds.maxs.value[2] = 400.0f + 128.0f;
	CHECK(SG_RuneCompactMovementFieldsBuild(&fixture.input, &fields, &error));
	if (fields != NULL && SG_RuneCompactMovementFieldsRead(fields, &view)) {
		CHECK(CountFamily(&view, SG_RUNE_MOVEMENT_CAPABILITY_JUMP) == 0U);
		CHECK(CountFamily(&view,
			SG_RUNE_MOVEMENT_CAPABILITY_ROCKET_JUMP) == 0U);
		CHECK(CountFamily(&view, SG_RUNE_MOVEMENT_CAPABILITY_DROP) == 2U);
	}
	SG_RuneCompactMovementFieldsDestroy(fields);
}

int main(void)
{
	Era4CheckLevelCrossing();
	Era4CheckCrouchOnlyPassage();
	Era4CheckCurrentDoesNotRemoveWalking();
	Era4CheckRocketJumpLedge();
	Era4CheckRocketJumpOffersOverJump();
	if (failures != 0)
		return 1;
	puts("sg_era4_portal_movement_test: ok");
	return 0;
}
