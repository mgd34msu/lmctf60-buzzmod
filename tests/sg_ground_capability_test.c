#define failures proof_fixture_failures
#define main proof_fixture_main
int proof_fixture_main(void);
#include "sg_bsp_completeness_proof_test.c"
#undef main
#undef failures
#undef CHECK

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../slipgate/sg_ground_capability.h"

extern void Pmove(pmove_t *pmove);
void Com_DPrintf(const char *format, ...);
void Com_Printf(char *format, ...);

static void EmptyPmove(pmove_t *pmove);
static void WrongHullPmove(pmove_t *pmove);
static void NoJumpPmove(pmove_t *pmove);
static void UngroundedPmove(pmove_t *pmove);
static void PortalBoundaryPmove(pmove_t *pmove);
static int portal_boundary_vertex;

void Com_DPrintf(const char *format, ...)
{
	(void)format;
}

void Com_Printf(char *format, ...)
{
	(void)format;
}

static void EmptyPmove(pmove_t *pmove)
{
	(void)pmove;
}

static void WrongHullPmove(pmove_t *pmove)
{
	Pmove(pmove);
	pmove->mins[0] += 1.0f;
}

static void NoJumpPmove(pmove_t *pmove)
{
	if (pmove->cmd.upmove > 0)
		pmove->cmd.upmove = 0;
	Pmove(pmove);
}

static void UngroundedPmove(pmove_t *pmove)
{
	Pmove(pmove);
	pmove->groundentity = NULL;
}

static void PortalBoundaryPmove(pmove_t *pmove)
{
	short source_x = pmove->s.origin[0];
	short source_y = pmove->s.origin[1];
	short source_z = pmove->s.origin[2];

	Pmove(pmove);
	if (pmove->cmd.forwardmove == 0)
		return;
	pmove->s.origin[0] = (short)-source_x;
	pmove->s.origin[1] = (short)(512 - source_y);
	if (portal_boundary_vertex)
		pmove->s.origin[2] = (short)-source_z;
}

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

typedef struct ground_fixture_s
{
	fixture_t world;
	sg_host_collision_authority_t authority;
	sg_configuration_space_t configuration;
	sg_configuration_cell_t cells[2];
	sg_configuration_face_t configuration_faces[2];
	sg_configuration_portal_t portal;
	sg_rune_vec3_t vertices[4];
	sg_configuration_stance_overlap_t stance_overlap;
	sg_configuration_semantics_t semantics;
	sg_configuration_semantic_region_t regions[4];
	sg_configuration_semantic_face_t semantic_faces[4];
	sg_rune_phase_basis_t phases[4];
	sg_ground_phase_binding_t bindings[4];
} ground_fixture_t;

static void SetRune3(sg_rune_vec3_t *value, float x, float y, float z)
{
	SetVector(value->value, x, y, z);
}

static sg_rune_phase_basis_t GroundPhase(
	const sg_rune_model_identity_t *identity, uint32_t ordinal,
	sg_rune_stance_t stance, sg_rune_motion_t motion)
{
	sg_rune_phase_basis_t phase;

	memset(&phase, 0, sizeof(phase));
	phase.order.source_set_identity = identity->source_set_identity;
	phase.order.domain = SG_RUNE_ORDER_PHASE;
	phase.order.source_index = ordinal;
	phase.order.local_ordinal = ordinal;
	phase.id.value = SG_RuneModelStableIdFromOrderKey(&phase.order);
	phase.stance = stance;
	phase.motion = motion;
	phase.support = motion == SG_RUNE_MOTION_SUPPORTED ?
		SG_RUNE_SUPPORT_SUPPORTED : SG_RUNE_SUPPORT_NONE;
	phase.medium = SG_RUNE_MEDIUM_DRY;
	phase.void_relation = SG_RUNE_VOID_CLEAR;
	phase.reference_frame = SG_RUNE_FRAME_WORLD;
	phase.mover = SG_RUNE_MECHANISM_REF_NONE;
	phase.velocity.x.min_value = -320.0f;
	phase.velocity.x.max_value = 320.0f;
	phase.velocity.y = phase.velocity.x;
	phase.velocity.z = phase.velocity.x;
	phase.elapsed_ms.min_value = 0.0f;
	phase.elapsed_ms.max_value = 1000.0f;
	phase.time_quantum_ms = identity->physics.substep_ms;
	phase.time_horizon_ms = 1000U;
	return phase;
}

static sg_rune_model_identity_t GroundIdentity(void)
{
	sg_rune_model_identity_t identity = Identity();

	identity.entity_semantics_id = UINT64_C(0x102);
	identity.schema_id = UINT64_C(0x105);
	identity.producer_identity = UINT64_C(0x106);
	return identity;
}

static void GroundCell(ground_fixture_t *fixture, uint32_t cell,
	float minimum_x, float maximum_x, sg_rune_stance_t stance)
{
	sg_configuration_cell_t *record = &fixture->cells[cell];

	memset(record, 0, sizeof(*record));
	record->order.source_set_identity =
		fixture->authority.identity.source_set_identity;
	record->order.domain = SG_RUNE_ORDER_CELL;
	record->order.source_index = cell;
	record->order.local_ordinal = cell;
	record->id.value = SG_RuneModelStableIdFromOrderKey(&record->order);
	record->stance = stance;
	SetRune3(&record->bounds.mins, minimum_x, -64.0f, -64.0f);
	SetRune3(&record->bounds.maxs, maximum_x, 64.0f, 64.0f);
	SetRune3(&record->interior_witness,
		(minimum_x + maximum_x) * 0.5f, 0.0f, 0.0f);
	record->bsp_leaf.index = cell;
	record->bsp_area.index = cell + 1U;
	record->bsp_cluster = SG_RUNE_BSP_CLUSTER_REF_NONE;
}

static void GroundRegion(ground_fixture_t *fixture, uint32_t region,
	uint32_t cell, float x, float minimum_z, float maximum_z,
	float witness_z, int supported)
{
	sg_configuration_semantic_region_t *record = &fixture->regions[region];

	memset(record, 0, sizeof(*record));
	record->id = (uint64_t)region + 1U;
	record->cell = cell;
	SetRune3(&record->bounds.mins, cell == 0U ? -64.0f : 0.0f,
		-64.0f, minimum_z);
	SetRune3(&record->bounds.maxs, cell == 0U ? 0.0f : 64.0f,
		64.0f, maximum_z);
	SetRune3(&record->interior_witness, x, 0.0f, witness_z);
	record->flags = supported ? SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED :
		SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE;
}

static void GroundFixtureInit(ground_fixture_t *fixture,
	const test_box_t *boxes,
	uint32_t box_count, float gravity, sg_rune_stance_t stance,
	float source_support_z, float destination_support_z)
{
	sg_rune_model_identity_t identity = GroundIdentity();
	sg_host_collision_error_t error;
	uint32_t cell;

	memset(fixture, 0, sizeof(*fixture));
	identity.physics.gravity = gravity;
	fixture->world = Fixture(boxes, box_count, SG_HOST_CONTENTS_SOLID,
		SG_HOST_CONTENTS_SOLID);
	CHECK(SG_HostCollisionInit(&fixture->authority, &fixture->world.world,
		&identity, &error));
	fixture->configuration.identity = identity;
	fixture->configuration.cells = fixture->cells;
	fixture->configuration.faces = fixture->configuration_faces;
	fixture->configuration.cell_count = 2U;
	fixture->configuration.portals = &fixture->portal;
	fixture->configuration.portal_count = 1U;
	fixture->configuration.vertices = fixture->vertices;
	fixture->configuration.vertex_count = 4U;
	GroundCell(fixture, 0U, -64.0f, 0.0f, stance);
	GroundCell(fixture, 1U, 0.0f, 64.0f, stance);
	fixture->portal.order.source_set_identity = identity.source_set_identity;
	fixture->portal.order.domain = SG_RUNE_ORDER_PORTAL;
	fixture->portal.order.source_index = 0U;
	fixture->portal.order.local_ordinal = 1U;
	fixture->portal.id.value =
		SG_RuneModelStableIdFromOrderKey(&fixture->portal.order);
	fixture->portal.from_cell = 0U;
	fixture->portal.to_cell = 1U;
	fixture->portal.stance = stance;
	SetVector(fixture->portal.plane.normal, 1.0f, 0.0f, 0.0f);
	fixture->portal.vertex_count = 4U;
	fixture->portal.clearance = 80.0f;
	SetRune3(&fixture->vertices[0], 0.0f, -32.0f, -8.0f);
	SetRune3(&fixture->vertices[1], 0.0f, 32.0f, -8.0f);
	SetRune3(&fixture->vertices[2], 0.0f, 32.0f, 48.0f);
	SetRune3(&fixture->vertices[3], 0.0f, -32.0f, 48.0f);
	fixture->semantics.identity = identity;
	fixture->semantics.regions = fixture->regions;
	fixture->semantics.region_count = 4U;
	GroundRegion(fixture, 0U, 0U, -1.0f, source_support_z - 0.25f,
		source_support_z + 0.25f,
		source_support_z, 1);
	GroundRegion(fixture, 1U, 0U, -1.0f, source_support_z + 0.375f,
		64.0f, source_support_z + 24.0f, 0);
	GroundRegion(fixture, 2U, 1U, 1.0f, destination_support_z - 0.25f,
		destination_support_z + 0.25f,
		destination_support_z, 1);
	GroundRegion(fixture, 3U, 1U, 1.0f, destination_support_z + 0.375f,
		64.0f, destination_support_z + 24.0f, 0);
	for (cell = 0U; cell < 2U; cell++)
	{
		uint32_t phase = cell * 2U;

		fixture->phases[phase] = GroundPhase(&identity, phase, stance,
			SG_RUNE_MOTION_SUPPORTED);
		fixture->phases[phase + 1U] = GroundPhase(&identity, phase + 1U,
			stance, SG_RUNE_MOTION_AIRBORNE);
		fixture->bindings[phase].cell = cell;
		fixture->bindings[phase].phase = phase;
		fixture->bindings[phase + 1U].cell = cell;
		fixture->bindings[phase + 1U].phase = phase + 1U;
	}
}

static void GroundFixtureDestroy(ground_fixture_t *fixture)
{
	DestroyFixture(&fixture->world);
	memset(fixture, 0, sizeof(*fixture));
}

static void GroundFixtureRebind(ground_fixture_t *fixture)
{
	sg_host_collision_error_t error;

	fixture->configuration.cells = fixture->cells;
	fixture->configuration.portals = &fixture->portal;
	fixture->configuration.vertices = fixture->vertices;
	fixture->configuration.stance_overlaps = &fixture->stance_overlap;
	fixture->semantics.regions = fixture->regions;
	fixture->semantics.faces = fixture->semantic_faces;
	CHECK(SG_HostCollisionInit(&fixture->authority, &fixture->world.world,
		&fixture->configuration.identity, &error));
}

static int GroundBuild(ground_fixture_t *fixture,
	sg_ground_capability_set_t **set_out,
	sg_ground_capability_error_t *error_out)
{
	return SG_GroundCapabilityBuild(&fixture->authority,
		&fixture->configuration, &fixture->semantics, fixture->phases, 4U,
		fixture->bindings, 4U, Pmove, set_out, error_out);
}

static int GroundBuildWithPmove(ground_fixture_t *fixture,
	sg_host_pmove_function_t pmove,
	sg_ground_capability_set_t **set_out,
	sg_ground_capability_error_t *error_out)
{
	return SG_GroundCapabilityBuild(&fixture->authority,
		&fixture->configuration, &fixture->semantics, fixture->phases, 4U,
		fixture->bindings, 4U, pmove, set_out, error_out);
}

static int HasKind(const sg_ground_capability_set_t *set,
	sg_ground_capability_kind_t kind)
{
	uint32_t index;

	for (index = 0U; index < set->capability_count; index++)
		if (set->capabilities[index].kind == kind)
			return 1;
	return 0;
}

static uint32_t CountKind(const sg_ground_capability_set_t *set,
	sg_ground_capability_kind_t kind)
{
	uint32_t count = 0U;
	uint32_t index;

	for (index = 0U; index < set->capability_count; index++)
		if (set->capabilities[index].kind == kind)
			count++;
	return count;
}

static int HasPortalSourcePhase(const sg_ground_capability_set_t *set,
	uint32_t source_cell, uint32_t destination_cell, uint32_t source_phase)
{
	uint32_t index;

	for (index = 0U; index < set->capability_count; index++)
		if (set->capabilities[index].portal !=
				SG_GROUND_CAPABILITY_INDEX_NONE &&
			set->capabilities[index].source_cell == source_cell &&
			set->capabilities[index].destination_cell == destination_cell &&
			set->capabilities[index].source_phase == source_phase)
			return 1;
	return 0;
}

static int IsQ8(float value)
{
	return isfinite(value) && nearbyintf(value * 8.0f) == value * 8.0f;
}

static int TestPhaseContainsVelocity(const sg_rune_phase_basis_t *phase,
	const float velocity[3])
{
	const sg_rune_interval_t *intervals[3] = {
		&phase->velocity.x, &phase->velocity.y, &phase->velocity.z
	};
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		if (velocity[axis] < intervals[axis]->min_value ||
			velocity[axis] > intervals[axis]->max_value)
			return 0;
	return 1;
}

static void TestFlatAndGravity(float gravity)
{
	const test_box_t floor = {
		{ -4096.0f, -4096.0f, -4096.0f },
		{ 4095.0f, 4095.0f, -24.1f }, SG_HOST_CONTENTS_SOLID
	};
	ground_fixture_t fixture;
	sg_ground_capability_set_t *first = NULL;
	sg_ground_capability_set_t *second = NULL;
	sg_ground_capability_error_t error;
	GroundFixtureInit(&fixture, &floor, 1U, gravity,
		SG_RUNE_STANCE_STANDING, 0.0f, 0.0f);
	GroundFixtureRebind(&fixture);

	CHECK(GroundBuild(&fixture, &first, &error));
	CHECK(first != NULL);
	if (first)
	{
		CHECK(HasKind(first, SG_GROUND_CAPABILITY_WALK));
		CHECK(HasKind(first, SG_GROUND_CAPABILITY_JUMP_TAKEOFF));
		CHECK(first->identity.physics.gravity == gravity);
		CHECK(GroundBuild(&fixture, &second, &error));
		CHECK(second != NULL);
		if (second)
		{
			CHECK(first->capability_count == second->capability_count);
			CHECK(memcmp(first->capabilities, second->capabilities,
				(size_t)first->capability_count *
					sizeof(*first->capabilities)) == 0);
		}
	}
	SG_GroundCapabilityDestroy(first);
	SG_GroundCapabilityDestroy(second);
	GroundFixtureDestroy(&fixture);
}

static void TestCrouchWallWindowAndGap(void)
{
	const test_box_t floor = {
		{ -4096.0f, -4096.0f, -4096.0f },
		{ 4095.0f, 4095.0f, -24.1f }, SG_HOST_CONTENTS_SOLID
	};
	test_box_t blocked[2] = {
		{ { -4096.0f, -4096.0f, -4096.0f },
			{ 4095.0f, 4095.0f, -24.1f }, SG_HOST_CONTENTS_SOLID },
		{ { -2.0f, -64.0f, -24.1f },
			{ 2.0f, 64.0f, 40.0f }, SG_HOST_CONTENTS_SOLID }
	};
	const test_box_t low_clearance[2] = {
		{ { -4096.0f, -4096.0f, -4096.0f },
			{ 4095.0f, 4095.0f, -24.1f }, SG_HOST_CONTENTS_SOLID },
		{ { -4096.0f, -4096.0f, 16.0f },
			{ 4095.0f, 4095.0f, 4095.0f }, SG_HOST_CONTENTS_SOLID }
	};
	const test_box_t separated_floors[2] = {
		{ { -4096.0f, -4096.0f, -4096.0f },
			{ -16.1f, 4095.0f, -24.1f }, SG_HOST_CONTENTS_SOLID },
		{ { 16.1f, -4096.0f, -4096.0f },
			{ 4095.0f, 4095.0f, -24.1f }, SG_HOST_CONTENTS_SOLID }
	};
	ground_fixture_t fixture;
	sg_ground_capability_set_t *set = NULL;
	sg_ground_capability_error_t error;
	GroundFixtureInit(&fixture, &floor, 1U, 800.0f,
		SG_RUNE_STANCE_CROUCHING, 0.0f, 0.0f);
	GroundFixtureRebind(&fixture);

	CHECK(GroundBuildWithPmove(&fixture, NoJumpPmove, &set, &error));
	CHECK(set && HasKind(set, SG_GROUND_CAPABILITY_CROUCH));
	SG_GroundCapabilityDestroy(set);
	GroundFixtureDestroy(&fixture);
	GroundFixtureInit(&fixture, low_clearance, 2U, 800.0f,
		SG_RUNE_STANCE_CROUCHING, 0.0f, 0.0f);
	GroundFixtureRebind(&fixture);
	set = NULL;
	CHECK(GroundBuild(&fixture, &set, &error));
	CHECK(set && HasKind(set, SG_GROUND_CAPABILITY_CROUCH));
	SG_GroundCapabilityDestroy(set);
	GroundFixtureDestroy(&fixture);
	GroundFixtureInit(&fixture, low_clearance, 2U, 800.0f,
		SG_RUNE_STANCE_STANDING, 0.0f, 0.0f);
	GroundFixtureRebind(&fixture);
	set = NULL;
	CHECK(GroundBuild(&fixture, &set, &error));
	CHECK(set && !HasKind(set, SG_GROUND_CAPABILITY_WALK));
	SG_GroundCapabilityDestroy(set);
	GroundFixtureDestroy(&fixture);

	GroundFixtureInit(&fixture, blocked, 2U, 800.0f,
		SG_RUNE_STANCE_STANDING, 0.0f, 0.0f);
	GroundFixtureRebind(&fixture);
	set = NULL;
	CHECK(GroundBuild(&fixture, &set, &error));
	CHECK(set && !HasKind(set, SG_GROUND_CAPABILITY_WALK));
	CHECK(set && set->rejected_crossings == 1U);
	SG_GroundCapabilityDestroy(set);
	GroundFixtureDestroy(&fixture);

	blocked[1].contents = SG_HOST_CONTENTS_WINDOW;
	GroundFixtureInit(&fixture, blocked, 2U, 800.0f,
		SG_RUNE_STANCE_STANDING, 0.0f, 0.0f);
	GroundFixtureRebind(&fixture);
	set = NULL;
	CHECK(GroundBuild(&fixture, &set, &error));
	CHECK(set && !HasKind(set, SG_GROUND_CAPABILITY_WALK));
	SG_GroundCapabilityDestroy(set);
	GroundFixtureDestroy(&fixture);

	GroundFixtureInit(&fixture, separated_floors, 2U, 800.0f,
		SG_RUNE_STANCE_STANDING, 0.0f, 0.0f);
	GroundFixtureRebind(&fixture);
	set = NULL;
	CHECK(GroundBuild(&fixture, &set, &error));
	CHECK(set && !HasKind(set, SG_GROUND_CAPABILITY_WALK));
	SG_GroundCapabilityDestroy(set);
	GroundFixtureDestroy(&fixture);

	GroundFixtureInit(&fixture, NULL, 0U, 800.0f,
		SG_RUNE_STANCE_STANDING, 0.0f, 0.0f);
	GroundFixtureRebind(&fixture);
	set = NULL;
	CHECK(GroundBuild(&fixture, &set, &error));
	CHECK(set && !HasKind(set, SG_GROUND_CAPABILITY_WALK));
	SG_GroundCapabilityDestroy(set);
	GroundFixtureDestroy(&fixture);
}

static void TestOrdinaryAndTallStep(void)
{
	const test_box_t ordinary[2] = {
		{ { -4096.0f, -4096.0f, -4096.0f },
			{ 4095.0f, 4095.0f, -24.1f }, SG_HOST_CONTENTS_SOLID },
		{ { 16.1f, -4096.0f, -24.1f },
			{ 4095.0f, 4095.0f, -8.1f }, SG_HOST_CONTENTS_SOLID }
	};
	const test_box_t tall[2] = {
		{ { -4096.0f, -4096.0f, -4096.0f },
			{ 4095.0f, 4095.0f, -24.1f }, SG_HOST_CONTENTS_SOLID },
		{ { 16.1f, -4096.0f, -24.1f },
			{ 4095.0f, 4095.0f, -0.1f }, SG_HOST_CONTENTS_SOLID }
	};
	ground_fixture_t fixture;
	sg_ground_capability_set_t *set = NULL;
	sg_ground_capability_error_t error;
	GroundFixtureInit(&fixture, ordinary, 2U, 800.0f,
		SG_RUNE_STANCE_STANDING, 0.0f, 16.0f);
	GroundFixtureRebind(&fixture);

	CHECK(GroundBuild(&fixture, &set, &error));
	CHECK(set && HasKind(set, SG_GROUND_CAPABILITY_STEP));
	SG_GroundCapabilityDestroy(set);
	GroundFixtureDestroy(&fixture);

	GroundFixtureInit(&fixture, tall, 2U, 800.0f,
		SG_RUNE_STANCE_STANDING, 0.0f, 24.0f);
	GroundFixtureRebind(&fixture);
	set = NULL;
	CHECK(GroundBuild(&fixture, &set, &error));
	CHECK(set && !HasKind(set, SG_GROUND_CAPABILITY_STEP));
	CHECK(set && !HasKind(set, SG_GROUND_CAPABILITY_WALK));
	SG_GroundCapabilityDestroy(set);
	GroundFixtureDestroy(&fixture);
}

static void TestRamp(void)
{
	const float diagonal = 0.70710677f;
	const test_box_t ramp = {
		{ -50.0f, -50.0f, -100.0f },
		{ 50.0f, 50.0f, 100.0f }, SG_HOST_CONTENTS_SOLID
	};
	ground_fixture_t fixture;
	sg_ground_capability_set_t *set = NULL;
	sg_ground_capability_error_t error;

	GroundFixtureInit(&fixture, &ramp, 1U, 100.0f,
		SG_RUNE_STANCE_STANDING, 39.0f, 41.0f);
	SetPlane(&fixture.world.planes[5], -diagonal, 0.0f, diagonal, 0.0f);
	fixture.semantics.faces = fixture.semantic_faces;
	fixture.semantics.face_count = 2U;
	SetVector(fixture.semantic_faces[0].normal, -1.0f, 0.0f, 1.0f);
	fixture.semantic_faces[0].distance = 40.25f;
	SetVector(fixture.semantic_faces[1].normal, 1.0f, 0.0f, -1.0f);
	fixture.semantic_faces[1].distance = -39.75f;
	fixture.regions[0].first_face = 0U;
	fixture.regions[0].face_count = 2U;
	fixture.regions[2].first_face = 0U;
	fixture.regions[2].face_count = 2U;
	SetRune3(&fixture.regions[0].bounds.mins, -64.0f, -64.0f, 20.0f);
	SetRune3(&fixture.regions[0].bounds.maxs, 0.0f, 64.0f, 40.25f);
	SetRune3(&fixture.regions[2].bounds.mins, 0.0f, -64.0f, 39.75f);
	SetRune3(&fixture.regions[2].bounds.maxs, 64.0f, 64.0f, 64.0f);
	SetRune3(&fixture.vertices[0], 0.0f, -32.0f, 32.0f);
	SetRune3(&fixture.vertices[1], 0.0f, 32.0f, 32.0f);
	SetRune3(&fixture.vertices[2], 0.0f, 32.0f, 48.0f);
	SetRune3(&fixture.vertices[3], 0.0f, -32.0f, 48.0f);
	GroundFixtureRebind(&fixture);
	CHECK(GroundBuild(&fixture, &set, &error));
	CHECK(set && HasKind(set, SG_GROUND_CAPABILITY_RAMP));
	SG_GroundCapabilityDestroy(set);
	GroundFixtureDestroy(&fixture);
}

static void TestDirectedDrop(void)
{
	const test_box_t ledge = {
		{ -4096.0f, -4096.0f, -4096.0f },
		{ -16.1f, 4095.0f, -24.1f }, SG_HOST_CONTENTS_SOLID
	};
	ground_fixture_t fixture;
	sg_ground_capability_set_t *set = NULL;
	sg_ground_capability_error_t error;
	GroundFixtureInit(&fixture, &ledge, 1U, 800.0f,
		SG_RUNE_STANCE_STANDING, 0.0f, -24.0f);
	GroundFixtureRebind(&fixture);

	SetRune3(&fixture.regions[3].bounds.mins, 0.0f, -64.0f, -64.0f);
	SetRune3(&fixture.regions[3].bounds.maxs, 64.0f, 64.0f, 64.0f);
	SetRune3(&fixture.regions[3].interior_witness, 1.0f, 0.0f, -4.0f);
	CHECK(GroundBuild(&fixture, &set, &error));
	CHECK(set && HasKind(set, SG_GROUND_CAPABILITY_DROP));
	CHECK(set && !HasKind(set, SG_GROUND_CAPABILITY_WALK));
	CHECK(set && set->proved_portals == 1U);
	CHECK(set && set->proved_directions == 1U);
	CHECK(set && set->rejected_directions == 1U);
	if (set)
	{
		uint32_t index;

		for (index = 0U; index < set->capability_count; index++)
			if (set->capabilities[index].kind == SG_GROUND_CAPABILITY_DROP)
				CHECK(set->capabilities[index].source_cell == 0U &&
					set->capabilities[index].destination_cell == 1U);
	}
	SG_GroundCapabilityDestroy(set);
	GroundFixtureDestroy(&fixture);
}

static void TestStanceOverlap(void)
{
	const test_box_t floor = {
		{ -4096.0f, -4096.0f, -4096.0f },
		{ 4095.0f, 4095.0f, -24.1f }, SG_HOST_CONTENTS_SOLID
	};
	ground_fixture_t fixture;
	sg_ground_capability_set_t *set = NULL;
	sg_ground_capability_error_t error;
	sg_configuration_stance_overlap_t duplicate_overlaps[2];
	sg_ground_phase_binding_t supported_bindings[2] = {
		{ 0U, 0U }, { 1U, 2U }
	};
	uint32_t region;
	GroundFixtureInit(&fixture, &floor, 1U, 800.0f,
		SG_RUNE_STANCE_STANDING, 0.0f, 0.0f);
	GroundFixtureRebind(&fixture);

	fixture.configuration.portal_count = 0U;
	fixture.configuration.vertex_count = 0U;
	fixture.configuration.stance_overlap_count = 1U;
	fixture.cells[1].stance = SG_RUNE_STANCE_CROUCHING;
	SetRune3(&fixture.cells[1].bounds.mins, -64.0f, -64.0f, -64.0f);
	SetRune3(&fixture.cells[1].bounds.maxs, 64.0f, 64.0f, 64.0f);
	SetRune3(&fixture.cells[1].interior_witness, 0.0f, 0.0f, 0.0f);
	fixture.stance_overlap.standing_cell = 0U;
	fixture.stance_overlap.crouching_cell = 1U;
	SetRune3(&fixture.stance_overlap.interior_witness, 0.0f, 0.0f, 0.0f);
	for (region = 2U; region < 4U; region++)
	{
		fixture.regions[region].bounds.mins.value[0] = -64.0f;
		fixture.regions[region].bounds.maxs.value[0] = 64.0f;
		fixture.regions[region].interior_witness.value[0] = 0.0f;
	}
	fixture.phases[2].stance = SG_RUNE_STANCE_CROUCHING;
	fixture.phases[3].stance = SG_RUNE_STANCE_CROUCHING;
	CHECK(GroundBuild(&fixture, &set, &error));
	CHECK(set && CountKind(set, SG_GROUND_CAPABILITY_STANCE) == 2U);
	SG_GroundCapabilityDestroy(set);
	set = NULL;
	duplicate_overlaps[0] = fixture.stance_overlap;
	duplicate_overlaps[1] = fixture.stance_overlap;
	fixture.configuration.stance_overlaps = duplicate_overlaps;
	fixture.configuration.stance_overlap_count = 2U;
	CHECK(SG_GroundCapabilityBuild(&fixture.authority,
		&fixture.configuration, &fixture.semantics, fixture.phases, 4U,
		supported_bindings, 2U, NoJumpPmove, &set, &error));
	CHECK(set && set->capability_count == 2U);
	SG_GroundCapabilityDestroy(set);
	GroundFixtureDestroy(&fixture);
}

static void TestPortalPlaneScalingAndSubsetBindings(void)
{
	const test_box_t floor = {
		{ -4096.0f, -4096.0f, -4096.0f },
		{ 4095.0f, 4095.0f, -24.1f }, SG_HOST_CONTENTS_SOLID
	};
	ground_fixture_t fixture;
	sg_ground_capability_set_t *unscaled = NULL;
	sg_ground_capability_set_t *scaled = NULL;
	sg_ground_capability_set_t *subset = NULL;
	sg_ground_capability_error_t error;
	sg_ground_phase_binding_t subset_bindings[2] = { { 0U, 0U }, { 1U, 2U } };
	GroundFixtureInit(&fixture, &floor, 1U, 800.0f,
		SG_RUNE_STANCE_STANDING, 0.0f, 0.0f);
	GroundFixtureRebind(&fixture);

	CHECK(GroundBuild(&fixture, &unscaled, &error));
	fixture.portal.plane.normal[0] *= 1024.0f;
	fixture.portal.plane.distance *= 1024.0f;
	CHECK(GroundBuild(&fixture, &scaled, &error));
	CHECK(unscaled && scaled &&
		unscaled->capability_count == scaled->capability_count);
	if (unscaled && scaled &&
		unscaled->capability_count == scaled->capability_count)
		CHECK(memcmp(unscaled->capabilities, scaled->capabilities,
			(size_t)unscaled->capability_count *
				sizeof(*unscaled->capabilities)) == 0);
	CHECK(SG_GroundCapabilityBuild(&fixture.authority,
		&fixture.configuration, &fixture.semantics, fixture.phases, 4U,
		subset_bindings, 2U, NoJumpPmove, &subset, &error));
	CHECK(subset && HasKind(subset, SG_GROUND_CAPABILITY_WALK));
	SG_GroundCapabilityDestroy(unscaled);
	SG_GroundCapabilityDestroy(scaled);
	SG_GroundCapabilityDestroy(subset);
	GroundFixtureDestroy(&fixture);
}

static void TestTwistedPortalRejects(void)
{
	const test_box_t floor = {
		{ -4096.0f, -4096.0f, -4096.0f },
		{ 4095.0f, 4095.0f, -24.1f }, SG_HOST_CONTENTS_SOLID
	};
	ground_fixture_t fixture;
	sg_ground_capability_set_t *set = NULL;
	sg_ground_capability_error_t error;

	GroundFixtureInit(&fixture, &floor, 1U, 800.0f,
		SG_RUNE_STANCE_STANDING, 0.0f, 0.0f);
	GroundFixtureRebind(&fixture);
	CHECK(GroundBuild(&fixture, &set, &error));
	CHECK(set && HasKind(set, SG_GROUND_CAPABILITY_WALK));
	SG_GroundCapabilityDestroy(set);
	set = NULL;
	fixture.vertices[2].value[0] = 8.0f;
	CHECK(!GroundBuild(&fixture, &set, &error));
	CHECK(set == NULL);
	CHECK(error.code == SG_GROUND_CAPABILITY_ERROR_INVALID_SOURCE);
	GroundFixtureDestroy(&fixture);
}

static void TestPhaseVelocityAuthority(void)
{
	const test_box_t floor = {
		{ -4096.0f, -4096.0f, -4096.0f },
		{ 4095.0f, 4095.0f, -24.1f }, SG_HOST_CONTENTS_SOLID
	};
	ground_fixture_t fixture;
	sg_ground_capability_set_t *set = NULL;
	sg_ground_capability_error_t error;

	GroundFixtureInit(&fixture, &floor, 1U, 800.0f,
		SG_RUNE_STANCE_STANDING, 0.0f, 0.0f);
	GroundFixtureRebind(&fixture);
	fixture.phases[0].velocity.x.min_value = -2000.0f;
	fixture.phases[0].velocity.x.max_value = -2000.0f;
	CHECK(!GroundBuild(&fixture, &set, &error));
	CHECK(set == NULL);
	CHECK(error.code == SG_GROUND_CAPABILITY_ERROR_INVALID_PHASE);
	set = NULL;

	fixture.phases[0].velocity.x.min_value = 100.0f;
	fixture.phases[0].velocity.x.max_value = 100.0f;
	fixture.phases[2].velocity.x.min_value = 1000.0f;
	fixture.phases[2].velocity.x.max_value = 2000.0f;
	CHECK(!GroundBuild(&fixture, &set, &error));
	CHECK(set == NULL);
	CHECK(error.code == SG_GROUND_CAPABILITY_ERROR_INVALID_PHASE);
	GroundFixtureDestroy(&fixture);
}

static void TestFatalOracleAndUnrepresentablePhase(void)
{
	const test_box_t floor = {
		{ -4096.0f, -4096.0f, -4096.0f },
		{ 4095.0f, 4095.0f, -24.1f }, SG_HOST_CONTENTS_SOLID
	};
	ground_fixture_t fixture;
	sg_ground_capability_set_t *set = NULL;
	sg_ground_capability_error_t error;

	GroundFixtureInit(&fixture, &floor, 1U, 800.0f,
		SG_RUNE_STANCE_STANDING, 0.0f, 0.0f);
	GroundFixtureRebind(&fixture);
	CHECK(!SG_GroundCapabilityBuild(&fixture.authority,
		&fixture.configuration, &fixture.semantics, fixture.phases, 4U,
		fixture.bindings, 4U, EmptyPmove, &set, &error));
	CHECK(set == NULL);
	CHECK(error.code == SG_GROUND_CAPABILITY_ERROR_HOST_DISAGREEMENT);
	CHECK(!SG_GroundCapabilityBuild(&fixture.authority,
		&fixture.configuration, &fixture.semantics, fixture.phases, 4U,
		fixture.bindings, 4U, WrongHullPmove, &set, &error));
	CHECK(set == NULL);
	CHECK(error.code == SG_GROUND_CAPABILITY_ERROR_HOST_DISAGREEMENT);
	fixture.phases[0].velocity.x.min_value = 0.01f;
	fixture.phases[0].velocity.x.max_value = 0.02f;
	CHECK(!GroundBuild(&fixture, &set, &error));
	CHECK(set == NULL);
	CHECK(error.code == SG_GROUND_CAPABILITY_ERROR_INVALID_PHASE);
	GroundFixtureDestroy(&fixture);
}

static void TestExactSkewPortalAndZeroMargin(void)
{
	const test_box_t floor = {
		{ -4096.0f, -4096.0f, -4096.0f },
		{ 4095.0f, 4095.0f, -24.1f }, SG_HOST_CONTENTS_SOLID
	};
	ground_fixture_t fixture;
	sg_ground_capability_set_t *set = NULL;
	sg_ground_capability_error_t error;
	uint32_t index;

	GroundFixtureInit(&fixture, &floor, 1U, 800.0f,
		SG_RUNE_STANCE_STANDING, 0.0f, 0.0f);
	GroundFixtureRebind(&fixture);
	SetVector(fixture.portal.plane.normal, 1.0f, 2.0f, 0.0f);
	fixture.portal.plane.distance = 0.125f;
	fixture.configuration.face_count = 2U;
	SetVector(fixture.configuration_faces[0].plane.normal,
		1.0f, 2.0f, 0.0f);
	fixture.configuration_faces[0].plane.distance = 0.125f;
	SetVector(fixture.configuration_faces[1].plane.normal,
		-1.0f, -2.0f, 0.0f);
	fixture.configuration_faces[1].plane.distance = -0.125f;
	fixture.cells[0].first_face = 0U;
	fixture.cells[0].face_count = 1U;
	fixture.cells[1].first_face = 1U;
	fixture.cells[1].face_count = 1U;
	fixture.cells[0].bounds.maxs.value[0] = 64.0f;
	fixture.cells[1].bounds.mins.value[0] = -64.0f;
	SetRune3(&fixture.vertices[0], 32.125f, -16.0f, -8.0f);
	SetRune3(&fixture.vertices[1], -31.875f, 16.0f, -8.0f);
	SetRune3(&fixture.vertices[2], -31.875f, 16.0f, 48.0f);
	SetRune3(&fixture.vertices[3], 32.125f, -16.0f, 48.0f);
	fixture.regions[0].bounds.mins.value[1] = -16.0f;
	fixture.regions[0].bounds.maxs.value[1] = 16.0f;
	fixture.regions[2].bounds.mins.value[1] = -16.0f;
	fixture.regions[2].bounds.maxs.value[1] = 16.0f;
	fixture.regions[0].bounds.maxs.value[0] = 64.0f;
	fixture.regions[1].bounds.maxs.value[0] = 64.0f;
	fixture.regions[2].bounds.mins.value[0] = -64.0f;
	fixture.regions[3].bounds.mins.value[0] = -64.0f;
	CHECK(GroundBuild(&fixture, &set, &error));
	CHECK(set && HasKind(set, SG_GROUND_CAPABILITY_WALK));
	if (set)
		for (index = 0U; index < set->capability_count; index++)
		{
			const sg_ground_capability_t *fact = &set->capabilities[index];
			uint32_t axis;

			for (axis = 0U; axis < 3U; axis++)
			{
				CHECK(IsQ8(fact->source_witness.value[axis]));
				CHECK(IsQ8(fact->destination_witness.value[axis]));
				CHECK(IsQ8(fact->initial_velocity.value[axis]));
				CHECK(IsQ8(fact->observed_velocity.value[axis]));
			}
			CHECK(fact->source_phase < 4U);
			CHECK(fact->destination_phase < 4U);
			if (fact->source_phase < 4U)
				CHECK(TestPhaseContainsVelocity(
					&fixture.phases[fact->source_phase],
					fact->initial_velocity.value));
			if (fact->destination_phase < 4U)
				CHECK(TestPhaseContainsVelocity(
					&fixture.phases[fact->destination_phase],
					fact->observed_velocity.value));
		}
	SG_GroundCapabilityDestroy(set);
	set = NULL;
	SetVector(fixture.portal.plane.normal, 1.0f, 0.0f, 0.0f);
	fixture.portal.plane.distance = 0.0f;
	SetRune3(&fixture.vertices[0], 0.0f, -32.0f, 0.0f);
	SetRune3(&fixture.vertices[1], 0.0f, 32.0f, 0.0f);
	SetRune3(&fixture.vertices[2], 0.0f, 32.0f, 48.0f);
	SetRune3(&fixture.vertices[3], 0.0f, -32.0f, 48.0f);
	fixture.regions[0].bounds.mins.value[2] = 0.0f;
	fixture.regions[0].bounds.maxs.value[2] = 0.0f;
	fixture.regions[2].bounds.mins.value[2] = 0.0f;
	fixture.regions[2].bounds.maxs.value[2] = 0.0f;
	CHECK(GroundBuild(&fixture, &set, &error));
	CHECK(set && !HasKind(set, SG_GROUND_CAPABILITY_WALK));
	CHECK(set && set->proved_directions == 0U);
	CHECK(set && set->rejected_directions == 2U);
	SG_GroundCapabilityDestroy(set);
	GroundFixtureDestroy(&fixture);
}

static void TestCrouchedTakeoffAndBlockedStanding(void)
{
	const test_box_t clearance[2] = {
		{ { -4096.0f, -4096.0f, -4096.0f },
			{ 4095.0f, 4095.0f, -24.1f }, SG_HOST_CONTENTS_SOLID },
		{ { -4096.0f, -4096.0f, 30.0f },
			{ 4095.0f, 4095.0f, 4095.0f }, SG_HOST_CONTENTS_SOLID }
	};
	ground_fixture_t fixture;
	sg_ground_capability_set_t *set = NULL;
	sg_ground_capability_error_t error;
	uint32_t index;

	GroundFixtureInit(&fixture, clearance, 2U, 800.0f,
		SG_RUNE_STANCE_CROUCHING, 0.0f, 0.0f);
	GroundFixtureRebind(&fixture);
	CHECK(GroundBuild(&fixture, &set, &error));
	CHECK(set && HasKind(set, SG_GROUND_CAPABILITY_JUMP_TAKEOFF));
	if (set)
		for (index = 0U; index < set->capability_count; index++)
			if (set->capabilities[index].kind ==
				SG_GROUND_CAPABILITY_JUMP_TAKEOFF)
				CHECK(fixture.phases[set->capabilities[index].destination_phase].
					stance == SG_RUNE_STANCE_CROUCHING);
	SG_GroundCapabilityDestroy(set);
	GroundFixtureDestroy(&fixture);

	GroundFixtureInit(&fixture, clearance, 2U, 800.0f,
		SG_RUNE_STANCE_STANDING, 0.0f, 0.0f);
	GroundFixtureRebind(&fixture);
	set = NULL;
	CHECK(GroundBuild(&fixture, &set, &error));
	CHECK(set && !HasKind(set, SG_GROUND_CAPABILITY_JUMP_TAKEOFF));
	SG_GroundCapabilityDestroy(set);
	GroundFixtureDestroy(&fixture);
}

static void TestShallowWaterAndVoidPhaseMatching(void)
{
	const test_box_t floor = {
		{ -4096.0f, -4096.0f, -4096.0f },
		{ 4095.0f, 4095.0f, -24.1f }, SG_HOST_CONTENTS_SOLID
	};
	ground_fixture_t fixture;
	sg_ground_capability_set_t *set = NULL;
	sg_ground_capability_error_t error;

	GroundFixtureInit(&fixture, &floor, 1U, 800.0f,
		SG_RUNE_STANCE_STANDING, 0.0f, 0.0f);
	SetPlane(&fixture.world.planes[0], 0.0f, 0.0f, 1.0f, -12.0f);
	fixture.world.leaves[1].contents = SG_HOST_CONTENTS_WATER;
	GroundFixtureRebind(&fixture);
	set = NULL;
	fixture.regions[0].flags |= SG_CONFIGURATION_SEMANTIC_REGION_WATER;
	fixture.regions[2].flags |= SG_CONFIGURATION_SEMANTIC_REGION_WATER;
	fixture.regions[0].water_level = 1U;
	fixture.regions[2].water_level = 1U;
	fixture.regions[0].water_type = SG_HOST_CONTENTS_WATER;
	fixture.regions[2].water_type = SG_HOST_CONTENTS_WATER;
	fixture.phases[0].medium = SG_RUNE_MEDIUM_WATER;
	fixture.phases[2].medium = SG_RUNE_MEDIUM_WATER;
	CHECK(GroundBuildWithPmove(&fixture, NoJumpPmove, &set, &error));
	CHECK(set && HasKind(set, SG_GROUND_CAPABILITY_WALK));
	SG_GroundCapabilityDestroy(set);
	set = NULL;
	SetPlane(&fixture.world.planes[0], 0.0f, 0.0f, 1.0f, 20.0f);
	GroundFixtureRebind(&fixture);
	fixture.regions[0].water_level = 2U;
	fixture.regions[2].water_level = 2U;
	CHECK(GroundBuildWithPmove(&fixture, NoJumpPmove, &set, &error));
	CHECK(set && !HasPortalSourcePhase(set, 0U, 1U, 0U));
	SG_GroundCapabilityDestroy(set);
	GroundFixtureDestroy(&fixture);

	GroundFixtureInit(&fixture, &floor, 1U, 800.0f,
		SG_RUNE_STANCE_STANDING, 0.0f, 0.0f);
	GroundFixtureRebind(&fixture);
	set = NULL;
	fixture.regions[0].flags |=
		SG_CONFIGURATION_SEMANTIC_REGION_VOID_ADJACENT;
	fixture.regions[2].flags |=
		SG_CONFIGURATION_SEMANTIC_REGION_VOID_ADJACENT;
	fixture.phases[0].void_relation = SG_RUNE_VOID_ADJACENT;
	fixture.phases[2].void_relation = SG_RUNE_VOID_ADJACENT;
	CHECK(GroundBuild(&fixture, &set, &error));
	CHECK(set && HasPortalSourcePhase(set, 0U, 1U, 0U));
	SG_GroundCapabilityDestroy(set);
	set = NULL;
	fixture.phases[2].void_relation = SG_RUNE_VOID_CLEAR;
	CHECK(!GroundBuild(&fixture, &set, &error));
	CHECK(set == NULL);
	CHECK(error.code == SG_GROUND_CAPABILITY_ERROR_INVALID_PHASE);
	GroundFixtureDestroy(&fixture);
}

static void TestDiscontinuousLowerLanding(void)
{
	const test_box_t ledges[2] = {
		{ { -4096.0f, -4096.0f, -4096.0f },
			{ -16.1f, 4095.0f, -20.1f }, SG_HOST_CONTENTS_SOLID },
		{ { 16.1f, -4096.0f, -4096.0f },
			{ 4095.0f, 4095.0f, -24.1f }, SG_HOST_CONTENTS_SOLID }
	};
	ground_fixture_t fixture;
	sg_ground_capability_set_t *set = NULL;
	sg_ground_capability_error_t error;
	uint32_t index;

	GroundFixtureInit(&fixture, ledges, 2U, 800.0f,
		SG_RUNE_STANCE_STANDING, 4.0f, 0.0f);
	GroundFixtureRebind(&fixture);
	CHECK(GroundBuild(&fixture, &set, &error));
	CHECK(set && !HasKind(set, SG_GROUND_CAPABILITY_STEP));
	if (set)
		for (index = 0U; index < set->capability_count; index++)
			if (set->capabilities[index].kind == SG_GROUND_CAPABILITY_DROP)
			{
				CHECK((fixture.regions[
					set->capabilities[index].destination_region].flags &
					SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED) == 0U);
			}
	SG_GroundCapabilityDestroy(set);
	GroundFixtureDestroy(&fixture);
}

static void TestAirborneStanceDoesNotRequireSupport(void)
{
	ground_fixture_t fixture;
	sg_ground_capability_set_t *set = NULL;
	sg_ground_capability_error_t error;
	uint32_t region;
	uint32_t phase;
	uint32_t index;
	sg_ground_phase_binding_t unique_bindings[2] = {
		{ 0U, 0U }, { 1U, 2U }
	};

	GroundFixtureInit(&fixture, NULL, 0U, 100.0f,
		SG_RUNE_STANCE_STANDING, 0.0f, 0.0f);
	GroundFixtureRebind(&fixture);
	fixture.configuration.portal_count = 0U;
	fixture.configuration.vertex_count = 0U;
	fixture.configuration.stance_overlap_count = 1U;
	fixture.cells[1].stance = SG_RUNE_STANCE_CROUCHING;
	SetRune3(&fixture.cells[1].bounds.mins, -64.0f, -64.0f, -64.0f);
	SetRune3(&fixture.cells[1].bounds.maxs, 64.0f, 64.0f, 64.0f);
	SetRune3(&fixture.cells[1].interior_witness, 0.0f, 0.0f, 24.0f);
	fixture.stance_overlap.standing_cell = 0U;
	fixture.stance_overlap.crouching_cell = 1U;
	SetRune3(&fixture.stance_overlap.interior_witness, 0.0f, 0.0f, 24.0f);
	for (region = 0U; region < 4U; region++)
	{
		fixture.regions[region].flags =
			SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE;
		SetRune3(&fixture.regions[region].bounds.mins, -64.0f, -64.0f,
			-64.0f);
		SetRune3(&fixture.regions[region].bounds.maxs, 64.0f, 64.0f, 64.0f);
		SetRune3(&fixture.regions[region].interior_witness, 0.0f, 0.0f, 24.0f);
	}
	SetRune3(&fixture.regions[1].bounds.maxs, 64.0f, 64.0f, -32.0f);
	SetRune3(&fixture.regions[1].interior_witness, 0.0f, 0.0f, -48.0f);
	SetRune3(&fixture.regions[3].bounds.maxs, 64.0f, 64.0f, -32.0f);
	SetRune3(&fixture.regions[3].interior_witness, 0.0f, 0.0f, -48.0f);
	for (phase = 0U; phase < 4U; phase++)
	{
		fixture.phases[phase].motion = SG_RUNE_MOTION_AIRBORNE;
		fixture.phases[phase].support = SG_RUNE_SUPPORT_NONE;
	}
	fixture.phases[2].stance = SG_RUNE_STANCE_CROUCHING;
	fixture.phases[3].stance = SG_RUNE_STANCE_CROUCHING;
	CHECK(SG_GroundCapabilityBuild(&fixture.authority,
		&fixture.configuration, &fixture.semantics, fixture.phases, 4U,
		unique_bindings, 2U, Pmove, &set, &error));
	CHECK(set && HasKind(set, SG_GROUND_CAPABILITY_STANCE));
	if (set)
		for (index = 0U; index < set->capability_count; index++)
			if (set->capabilities[index].kind == SG_GROUND_CAPABILITY_STANCE)
				CHECK((set->capabilities[index].flags &
					SG_GROUND_CAPABILITY_REQUIRES_SUPPORT) == 0U);
	SG_GroundCapabilityDestroy(set);
	GroundFixtureDestroy(&fixture);
}

static void TestDestinationLocalizationIsUniqueAndGrounded(void)
{
	const test_box_t floor = {
		{ -4096.0f, -4096.0f, -4096.0f },
		{ 4095.0f, 4095.0f, -24.1f }, SG_HOST_CONTENTS_SOLID
	};
	ground_fixture_t fixture;
	sg_ground_capability_set_t *set = NULL;
	sg_ground_capability_error_t error;
	sg_rune_phase_basis_t phases[5];
	sg_ground_phase_binding_t bindings[5] = {
		{ 0U, 0U }, { 0U, 1U }, { 1U, 2U }, { 1U, 3U }, { 1U, 4U }
	};
	sg_configuration_semantic_region_t regions[5];
	sg_configuration_cell_t cells[3];
	sg_configuration_semantic_region_t cell_regions[6];
	sg_rune_phase_basis_t cell_phases[6];
	sg_ground_phase_binding_t cell_bindings[6] = {
		{ 0U, 0U }, { 0U, 1U }, { 1U, 2U }, { 1U, 3U },
		{ 2U, 4U }, { 2U, 5U }
	};
	uint32_t index;

	GroundFixtureInit(&fixture, &floor, 1U, 800.0f,
		SG_RUNE_STANCE_STANDING, 0.0f, 0.0f);
	GroundFixtureRebind(&fixture);
	memcpy(phases, fixture.phases, sizeof(fixture.phases));
	phases[4] = GroundPhase(&fixture.configuration.identity, 4U,
		SG_RUNE_STANCE_STANDING, SG_RUNE_MOTION_SUPPORTED);
	CHECK(!SG_GroundCapabilityBuild(&fixture.authority,
		&fixture.configuration, &fixture.semantics, phases, 5U, bindings, 5U,
		Pmove, &set, &error));
	CHECK(set == NULL);
	CHECK(error.code == SG_GROUND_CAPABILITY_ERROR_INVALID_PHASE);

	memcpy(regions, fixture.regions, sizeof(fixture.regions[0]) * 2U);
	regions[2] = fixture.regions[2];
	regions[3] = fixture.regions[2];
	regions[4] = fixture.regions[3];
	for (index = 0U; index < 5U; index++)
		regions[index].id = (uint64_t)index + 1U;
	fixture.semantics.regions = regions;
	fixture.semantics.region_count = 5U;
	CHECK(!GroundBuild(&fixture, &set, &error));
	CHECK(set == NULL);
	CHECK(error.code == SG_GROUND_CAPABILITY_ERROR_INVALID_PHASE);
	fixture.semantics.regions = fixture.regions;
	fixture.semantics.region_count = 4U;
	memcpy(cells, fixture.cells, sizeof(fixture.cells));
	cells[2] = cells[0];
	cells[2].order.source_index = 2U;
	cells[2].order.local_ordinal = 2U;
	cells[2].id.value = SG_RuneModelStableIdFromOrderKey(&cells[2].order);
	memcpy(cell_regions, fixture.regions, sizeof(fixture.regions));
	cell_regions[4] = fixture.regions[0];
	cell_regions[5] = fixture.regions[1];
	cell_regions[4].cell = 2U;
	cell_regions[5].cell = 2U;
	for (index = 0U; index < 6U; index++)
		cell_regions[index].id = (uint64_t)index + 1U;
	memcpy(cell_phases, fixture.phases, sizeof(fixture.phases));
	cell_phases[4] = GroundPhase(&fixture.configuration.identity, 4U,
		SG_RUNE_STANCE_STANDING, SG_RUNE_MOTION_SUPPORTED);
	cell_phases[5] = GroundPhase(&fixture.configuration.identity, 5U,
		SG_RUNE_STANCE_STANDING, SG_RUNE_MOTION_AIRBORNE);
	fixture.configuration.cells = cells;
	fixture.configuration.cell_count = 3U;
	fixture.semantics.regions = cell_regions;
	fixture.semantics.region_count = 6U;
	CHECK(!SG_GroundCapabilityBuild(&fixture.authority,
		&fixture.configuration, &fixture.semantics, cell_phases, 6U,
		cell_bindings, 6U, Pmove, &set, &error));
	CHECK(set == NULL);
	CHECK(error.code == SG_GROUND_CAPABILITY_ERROR_INVALID_PHASE);
	fixture.configuration.cells = fixture.cells;
	fixture.configuration.cell_count = 2U;
	fixture.semantics.regions = fixture.regions;
	fixture.semantics.region_count = 4U;

	CHECK(!SG_GroundCapabilityBuild(&fixture.authority,
		&fixture.configuration, &fixture.semantics, fixture.phases, 4U,
		fixture.bindings, 4U, UngroundedPmove, &set, &error));
	CHECK(set == NULL);
	CHECK(error.code == SG_GROUND_CAPABILITY_ERROR_INVALID_PHASE);
	GroundFixtureDestroy(&fixture);
}

static void TestPortalBoundaryContactsReject(void)
{
	const test_box_t floor = {
		{ -4096.0f, -4096.0f, -4096.0f },
		{ 4095.0f, 4095.0f, -24.1f }, SG_HOST_CONTENTS_SOLID
	};
	ground_fixture_t fixture;
	sg_ground_capability_set_t *set = NULL;
	sg_ground_capability_error_t error;

	GroundFixtureInit(&fixture, &floor, 1U, 800.0f,
		SG_RUNE_STANCE_STANDING, 0.0f, 0.0f);
	fixture.configuration.identity.physics.frame_ms =
		fixture.configuration.identity.physics.substep_ms;
	fixture.semantics.identity = fixture.configuration.identity;
	GroundFixtureRebind(&fixture);
	portal_boundary_vertex = 0;
	CHECK(SG_GroundCapabilityBuild(&fixture.authority,
		&fixture.configuration, &fixture.semantics, fixture.phases, 4U,
		fixture.bindings, 4U, PortalBoundaryPmove, &set, &error));
	CHECK(set && !HasKind(set, SG_GROUND_CAPABILITY_WALK));
	CHECK(set && set->proved_directions == 0U);
	SG_GroundCapabilityDestroy(set);
	set = NULL;
	SetRune3(&fixture.vertices[0], 0.0f, -32.0f, 0.0f);
	SetRune3(&fixture.vertices[1], 0.0f, 32.0f, 0.0f);
	portal_boundary_vertex = 1;
	CHECK(SG_GroundCapabilityBuild(&fixture.authority,
		&fixture.configuration, &fixture.semantics, fixture.phases, 4U,
		fixture.bindings, 4U, PortalBoundaryPmove, &set, &error));
	CHECK(set && !HasKind(set, SG_GROUND_CAPABILITY_WALK));
	CHECK(set && set->proved_directions == 0U);
	SG_GroundCapabilityDestroy(set);
	portal_boundary_vertex = 0;
	GroundFixtureDestroy(&fixture);
}

static void TestOnlyShallowWaterUsesGroundLane(void)
{
	const test_box_t floor = {
		{ -4096.0f, -4096.0f, -4096.0f },
		{ 4095.0f, 4095.0f, -24.1f }, SG_HOST_CONTENTS_SOLID
	};
	const sg_host_collision_contents_t media[2] = {
		SG_HOST_CONTENTS_LAVA, SG_HOST_CONTENTS_SLIME
	};
	const sg_configuration_semantic_region_flags_t flags[2] = {
		SG_CONFIGURATION_SEMANTIC_REGION_LAVA,
		SG_CONFIGURATION_SEMANTIC_REGION_SLIME
	};
	const sg_rune_medium_t phase_media[2] = {
		SG_RUNE_MEDIUM_LAVA, SG_RUNE_MEDIUM_SLIME
	};
	uint32_t medium;

	for (medium = 0U; medium < 2U; medium++)
	{
		ground_fixture_t fixture;
		sg_ground_capability_set_t *set = NULL;
		sg_ground_capability_error_t error;

		GroundFixtureInit(&fixture, &floor, 1U, 800.0f,
			SG_RUNE_STANCE_STANDING, 0.0f, 0.0f);
		SetPlane(&fixture.world.planes[0], 0.0f, 0.0f, 1.0f, -12.0f);
		fixture.world.leaves[1].contents = (int32_t)media[medium];
		GroundFixtureRebind(&fixture);
		for (uint32_t region = 0U; region < 4U; region += 2U)
		{
			fixture.regions[region].flags |= flags[medium];
			fixture.regions[region].water_level = 1U;
			fixture.regions[region].water_type = media[medium];
		}
		fixture.phases[0].medium = phase_media[medium];
		fixture.phases[2].medium = phase_media[medium];
		CHECK(GroundBuild(&fixture, &set, &error));
		CHECK(set && !HasKind(set, SG_GROUND_CAPABILITY_WALK));
		SG_GroundCapabilityDestroy(set);
		GroundFixtureDestroy(&fixture);
	}
}

static void SetAllPhysics(sg_ground_capability_set_t **set,
	ground_fixture_t *fixture, float *field, float invalid)
{
	sg_ground_capability_error_t error;

	*field = invalid;
	fixture->semantics.identity = fixture->configuration.identity;
	fixture->authority.identity = fixture->configuration.identity;
	CHECK(!GroundBuild(fixture, set, &error));
	CHECK(*set == NULL);
	CHECK(error.code == SG_GROUND_CAPABILITY_ERROR_INVALID_SOURCE);
}

static void TestPhysicsValidationAndLandingLaw(void)
{
	const test_box_t floor = {
		{ -4096.0f, -4096.0f, -4096.0f },
		{ 4095.0f, 4095.0f, -24.1f }, SG_HOST_CONTENTS_SOLID
	};
	ground_fixture_t fixture;
	sg_ground_capability_set_t *set = NULL;
	sg_ground_capability_error_t error;
	uint32_t index;
	int saw_landing = 0;

	GroundFixtureInit(&fixture, &floor, 1U, 800.0f,
		SG_RUNE_STANCE_STANDING, 0.0f, 0.0f);
	fixture.configuration.identity.physics.ground_acceleration = 10.0f;
	fixture.configuration.identity.physics.air_acceleration = 2.0f;
	fixture.semantics.identity = fixture.configuration.identity;
	GroundFixtureRebind(&fixture);
	CHECK(GroundBuild(&fixture, &set, &error));
	CHECK(set && memcmp(&set->identity, &fixture.authority.identity,
		sizeof(set->identity)) == 0);
	if (set)
		for (index = 0U; index < set->capability_count; index++)
			if (set->capabilities[index].kind == SG_GROUND_CAPABILITY_LANDING)
			{
				saw_landing = 1;
				CHECK(set->capabilities[index].acceleration == 2.0f);
			}
	CHECK(saw_landing);
	SG_GroundCapabilityDestroy(set);
	set = NULL;
	SetAllPhysics(&set, &fixture,
		&fixture.configuration.identity.physics.ground_acceleration, -1.0f);
	GroundFixtureDestroy(&fixture);

#define CHECK_BAD_PHYSICS(field_name, invalid_value) do { \
	GroundFixtureInit(&fixture, &floor, 1U, 800.0f, \
		SG_RUNE_STANCE_STANDING, 0.0f, 0.0f); \
	SetAllPhysics(&set, &fixture, \
		&fixture.configuration.identity.physics.field_name, invalid_value); \
	GroundFixtureDestroy(&fixture); \
} while (0)
	CHECK_BAD_PHYSICS(air_acceleration, -1.0f);
	CHECK_BAD_PHYSICS(gravity, -1.0f);
	CHECK_BAD_PHYSICS(water_acceleration, NAN);
	CHECK_BAD_PHYSICS(hook_acceleration, -1.0f);
	CHECK_BAD_PHYSICS(external_acceleration, NAN);
	CHECK_BAD_PHYSICS(water_drag, -1.0f);
	CHECK_BAD_PHYSICS(max_velocity, 0.0f);
#undef CHECK_BAD_PHYSICS
	GroundFixtureInit(&fixture, &floor, 1U, 800.0f,
		SG_RUNE_STANCE_STANDING, 0.0f, 0.0f);
	fixture.configuration.identity.physics.frame_ms = 0U;
	fixture.semantics.identity = fixture.configuration.identity;
	fixture.authority.identity = fixture.configuration.identity;
	CHECK(!GroundBuild(&fixture, &set, &error));
	CHECK(set == NULL);
	CHECK(error.code == SG_GROUND_CAPABILITY_ERROR_INVALID_SOURCE);
	GroundFixtureDestroy(&fixture);
	GroundFixtureInit(&fixture, &floor, 1U, 800.0f,
		SG_RUNE_STANCE_STANDING, 0.0f, 0.0f);
	fixture.configuration.identity.physics.substep_ms = 0U;
	fixture.semantics.identity = fixture.configuration.identity;
	fixture.authority.identity = fixture.configuration.identity;
	CHECK(!GroundBuild(&fixture, &set, &error));
	CHECK(set == NULL);
	CHECK(error.code == SG_GROUND_CAPABILITY_ERROR_INVALID_SOURCE);
	GroundFixtureDestroy(&fixture);
}

typedef struct localization_scale_s
{
	uint64_t prepare_comparisons;
	uint64_t prepare_nodes;
	uint64_t queries;
	uint64_t query_nodes;
	uint64_t region_comparisons;
} localization_scale_t;

static localization_scale_t RunLocalizationScale(uint32_t cell_count)
{
	const test_box_t floor = {
		{ -4096.0f, -4096.0f, -4096.0f },
		{ 4095.0f, 4095.0f, -24.1f }, SG_HOST_CONTENTS_SOLID
	};
	fixture_t world = Fixture(&floor, 1U, SG_HOST_CONTENTS_SOLID,
		SG_HOST_CONTENTS_SOLID);
	sg_rune_model_identity_t identity = GroundIdentity();
	sg_host_collision_authority_t authority;
	sg_host_collision_error_t host_error;
	sg_configuration_space_t configuration;
	sg_configuration_semantics_t semantics;
	sg_configuration_cell_t *cells = calloc(cell_count, sizeof(*cells));
	sg_configuration_semantic_region_t *regions = calloc(
		(size_t)cell_count * 2U, sizeof(*regions));
	sg_rune_phase_basis_t *phases = calloc((size_t)cell_count * 2U,
		sizeof(*phases));
	sg_ground_phase_binding_t *bindings = calloc((size_t)cell_count * 2U,
		sizeof(*bindings));
	sg_ground_capability_set_t *set = NULL;
	sg_ground_capability_error_t error;
	localization_scale_t measured = { 0U, 0U, 0U, 0U, 0U };
	uint32_t cell;

	CHECK(cells && regions && phases && bindings);
	if (!cells || !regions || !phases || !bindings)
		goto done;
	identity.physics.frame_ms = identity.physics.substep_ms;
	CHECK(SG_HostCollisionInit(&authority, &world.world, &identity,
		&host_error));
	memset(&configuration, 0, sizeof(configuration));
	memset(&semantics, 0, sizeof(semantics));
	configuration.identity = identity;
	configuration.cells = cells;
	configuration.cell_count = cell_count;
	semantics.identity = identity;
	semantics.regions = regions;
	semantics.region_count = cell_count * 2U;
	for (cell = 0U; cell < cell_count; cell++)
	{
		uint32_t spatial = (cell * UINT32_C(40503)) & (cell_count - 1U);
		float minimum_x = -4096.0f + (float)spatial;
		float maximum_x = minimum_x + 0.875f;
		float center_x = minimum_x + 0.5f;
		uint32_t supported = cell * 2U;
		uint32_t airborne = supported + 1U;

		cells[cell].order.source_set_identity = identity.source_set_identity;
		cells[cell].order.domain = SG_RUNE_ORDER_CELL;
		cells[cell].order.source_index = cell;
		cells[cell].order.local_ordinal = cell;
		cells[cell].id.value =
			SG_RuneModelStableIdFromOrderKey(&cells[cell].order);
		cells[cell].stance = SG_RUNE_STANCE_STANDING;
		SetRune3(&cells[cell].bounds.mins, minimum_x, -64.0f, -64.0f);
		SetRune3(&cells[cell].bounds.maxs, maximum_x,
			64.0f, 64.0f);
		SetRune3(&cells[cell].interior_witness, center_x, 0.0f, 0.0f);
		cells[cell].bsp_leaf.index = cell;
		cells[cell].bsp_area.index = cell + 1U;
		cells[cell].bsp_cluster = SG_RUNE_BSP_CLUSTER_REF_NONE;
		regions[supported].id = (uint64_t)supported + 1U;
		regions[supported].cell = cell;
		SetRune3(&regions[supported].bounds.mins, minimum_x, -64.0f, -0.25f);
		SetRune3(&regions[supported].bounds.maxs, maximum_x, 64.0f, 0.25f);
		SetRune3(&regions[supported].interior_witness, center_x, 0.0f, 0.0f);
		regions[supported].flags =
			SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED;
		regions[airborne].id = (uint64_t)airborne + 1U;
		regions[airborne].cell = cell;
		SetRune3(&regions[airborne].bounds.mins, minimum_x, -64.0f, 0.375f);
		SetRune3(&regions[airborne].bounds.maxs, maximum_x, 64.0f, 64.0f);
		SetRune3(&regions[airborne].interior_witness, center_x, 0.0f, 24.0f);
		regions[airborne].flags =
			SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE;
		phases[supported] = GroundPhase(&identity, supported,
			SG_RUNE_STANCE_STANDING, SG_RUNE_MOTION_SUPPORTED);
		phases[airborne] = GroundPhase(&identity, airborne,
			SG_RUNE_STANCE_STANDING, SG_RUNE_MOTION_AIRBORNE);
		bindings[supported].cell = cell;
		bindings[supported].phase = supported;
		bindings[airborne].cell = cell;
		bindings[airborne].phase = airborne;
	}
	CHECK(SG_GroundCapabilityBuild(&authority, &configuration, &semantics,
		phases, (size_t)cell_count * 2U, bindings, (size_t)cell_count * 2U,
		Pmove, &set, &error));
	if (set)
	{
		measured.prepare_comparisons = set->localization_prepare_comparisons;
		measured.prepare_nodes = set->localization_prepare_nodes;
		measured.queries = set->localization_queries;
		measured.query_nodes = set->localization_nodes_examined;
		measured.region_comparisons = set->localization_region_comparisons;
		CHECK(measured.prepare_nodes == (uint64_t)cell_count * 2U - 1U);
		CHECK(measured.queries >= cell_count);
		CHECK(measured.query_nodes / measured.queries < 64U);
	}

done:
	SG_GroundCapabilityDestroy(set);
	free(cells);
	free(regions);
	free(phases);
	free(bindings);
	DestroyFixture(&world);
	return measured;
}

static localization_scale_t RunManyRegionScale(uint32_t slice_count)
{
	const test_box_t floor = {
		{ -4096.0f, -4096.0f, -4096.0f },
		{ 4095.0f, 4095.0f, -24.1f }, SG_HOST_CONTENTS_SOLID
	};
	fixture_t world = Fixture(&floor, 1U, SG_HOST_CONTENTS_SOLID,
		SG_HOST_CONTENTS_SOLID);
	sg_rune_model_identity_t identity = GroundIdentity();
	sg_host_collision_authority_t authority;
	sg_host_collision_error_t host_error;
	sg_configuration_space_t configuration;
	sg_configuration_semantics_t semantics;
	sg_configuration_cell_t cell;
	sg_configuration_semantic_region_t *regions = calloc(
		(size_t)slice_count * 2U, sizeof(*regions));
	sg_rune_phase_basis_t phases[2];
	sg_ground_phase_binding_t bindings[2] = { { 0U, 0U }, { 0U, 1U } };
	sg_ground_capability_set_t *set = NULL;
	sg_ground_capability_error_t error;
	localization_scale_t measured = { 0U, 0U, 0U, 0U, 0U };
	uint32_t slice;

	CHECK(regions != NULL);
	if (!regions)
		goto done;
	identity.physics.frame_ms = identity.physics.substep_ms;
	CHECK(SG_HostCollisionInit(&authority, &world.world, &identity,
		&host_error));
	memset(&configuration, 0, sizeof(configuration));
	memset(&semantics, 0, sizeof(semantics));
	memset(&cell, 0, sizeof(cell));
	configuration.identity = identity;
	configuration.cells = &cell;
	configuration.cell_count = 1U;
	cell.order.source_set_identity = identity.source_set_identity;
	cell.order.domain = SG_RUNE_ORDER_CELL;
	cell.id.value = SG_RuneModelStableIdFromOrderKey(&cell.order);
	cell.stance = SG_RUNE_STANCE_STANDING;
	SetRune3(&cell.bounds.mins, -2048.0f, -64.0f, -64.0f);
	SetRune3(&cell.bounds.maxs, 2048.0f, 64.0f, 64.0f);
	SetRune3(&cell.interior_witness, 0.0f, 0.0f, 0.0f);
	cell.bsp_leaf.index = 0U;
	cell.bsp_area.index = 1U;
	cell.bsp_cluster = SG_RUNE_BSP_CLUSTER_REF_NONE;
	semantics.identity = identity;
	semantics.regions = regions;
	semantics.region_count = slice_count * 2U;
	for (slice = 0U; slice < slice_count; slice++)
	{
		uint32_t supported = slice * 2U;
		uint32_t airborne = supported + 1U;
		float minimum_x = -1024.0f + (float)slice;
		float maximum_x = minimum_x + 0.875f;
		float center_x = minimum_x + 0.5f;

		regions[supported].id = (uint64_t)supported + 1U;
		regions[supported].cell = 0U;
		SetRune3(&regions[supported].bounds.mins, minimum_x, -64.0f, -0.25f);
		SetRune3(&regions[supported].bounds.maxs, maximum_x, 64.0f, 0.25f);
		SetRune3(&regions[supported].interior_witness, center_x, 0.0f, 0.0f);
		regions[supported].flags = SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED;
		regions[airborne].id = (uint64_t)airborne + 1U;
		regions[airborne].cell = 0U;
		SetRune3(&regions[airborne].bounds.mins, minimum_x, -64.0f, 0.375f);
		SetRune3(&regions[airborne].bounds.maxs, maximum_x, 64.0f, 64.0f);
		SetRune3(&regions[airborne].interior_witness, center_x, 0.0f, 24.0f);
		regions[airborne].flags = SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE;
	}
	phases[0] = GroundPhase(&identity, 0U, SG_RUNE_STANCE_STANDING,
		SG_RUNE_MOTION_SUPPORTED);
	phases[1] = GroundPhase(&identity, 1U, SG_RUNE_STANCE_STANDING,
		SG_RUNE_MOTION_AIRBORNE);
	CHECK(SG_GroundCapabilityBuild(&authority, &configuration, &semantics,
		phases, 2U, bindings, 2U, Pmove, &set, &error));
	if (set)
	{
		measured.prepare_comparisons = set->localization_prepare_comparisons;
		measured.prepare_nodes = set->localization_prepare_nodes;
		measured.queries = set->localization_queries;
		measured.query_nodes = set->localization_nodes_examined;
		measured.region_comparisons = set->localization_region_comparisons;
		CHECK(measured.queries >= slice_count);
		CHECK(measured.region_comparisons >= measured.queries);
		CHECK(measured.region_comparisons / measured.queries < 64U);
	}

done:
	SG_GroundCapabilityDestroy(set);
	free(regions);
	DestroyFixture(&world);
	return measured;
}

static void TestLocalizationIndexScaling(void)
{
	localization_scale_t two = RunLocalizationScale(2048U);
	localization_scale_t four = RunLocalizationScale(4096U);
	localization_scale_t eight = RunLocalizationScale(8192U);
	localization_scale_t many_two = RunManyRegionScale(512U);
	localization_scale_t many_four = RunManyRegionScale(1024U);
	localization_scale_t many_eight = RunManyRegionScale(2048U);

	CHECK(two.prepare_comparisons > 0U && two.query_nodes > 0U);
	CHECK(four.prepare_comparisons <= two.prepare_comparisons * 3U);
	CHECK(eight.prepare_comparisons <= four.prepare_comparisons * 3U);
	CHECK(four.query_nodes <= two.query_nodes * 3U);
	CHECK(eight.query_nodes <= four.query_nodes * 3U);
	CHECK(many_two.region_comparisons > 0U);
	CHECK(many_four.region_comparisons <= many_two.region_comparisons * 3U);
	CHECK(many_eight.region_comparisons <= many_four.region_comparisons * 3U);
}

static void TestFullIdentityAndNoPvs(void)
{
	const test_box_t floor = {
		{ -4096.0f, -4096.0f, -4096.0f },
		{ 4095.0f, 4095.0f, -24.1f }, SG_HOST_CONTENTS_SOLID
	};
	uint32_t invalid_case;

	for (invalid_case = 0U; invalid_case < 9U; invalid_case++)
	{
		ground_fixture_t fixture;
		sg_ground_capability_set_t *set = NULL;
		sg_ground_capability_error_t error;

		GroundFixtureInit(&fixture, &floor, 1U, 800.0f,
			SG_RUNE_STANCE_STANDING, 0.0f, 0.0f);
		GroundFixtureRebind(&fixture);
		if (invalid_case == 0U)
			fixture.configuration.identity.bsp_content_id = 0U;
		else if (invalid_case == 1U)
			fixture.configuration.identity.entity_semantics_id = 0U;
		else if (invalid_case == 2U)
			fixture.configuration.identity.physics_abi_id = 0U;
		else if (invalid_case == 3U)
			fixture.configuration.identity.source_set_identity = 0U;
		else if (invalid_case == 4U)
			fixture.configuration.identity.source_set_identity = UINT64_MAX;
		else if (invalid_case == 5U)
			fixture.configuration.identity.schema_id = 0U;
		else if (invalid_case == 6U)
			fixture.configuration.identity.producer_identity = 0U;
		else if (invalid_case == 7U)
			fixture.configuration.identity.standing_hull.maxs.value[0] =
				fixture.configuration.identity.standing_hull.mins.value[0];
		else
			fixture.configuration.identity.crouching_hull.mins.value[1] = NAN;
		fixture.semantics.identity = fixture.configuration.identity;
		fixture.authority.identity = fixture.configuration.identity;
		CHECK(!GroundBuild(&fixture, &set, &error));
		CHECK(set == NULL);
		CHECK(error.code == SG_GROUND_CAPABILITY_ERROR_INVALID_SOURCE);
		GroundFixtureDestroy(&fixture);
	}
	{
		ground_fixture_t fixture;
		sg_ground_capability_set_t *set = NULL;
		sg_ground_capability_error_t error;

		GroundFixtureInit(&fixture, &floor, 1U, 800.0f,
			SG_RUNE_STANCE_STANDING, 0.0f, 0.0f);
		GroundFixtureRebind(&fixture);
		CHECK(fixture.cells[0].bsp_cluster.index == UINT32_MAX);
		CHECK(fixture.cells[1].bsp_cluster.index == UINT32_MAX);
		CHECK(GroundBuild(&fixture, &set, &error));
		CHECK(set != NULL);
		SG_GroundCapabilityDestroy(set);
		GroundFixtureDestroy(&fixture);
	}
}

static void TestAtomicityIdentityAndHostileCounts(void)
{
	const test_box_t floor = {
		{ -4096.0f, -4096.0f, -4096.0f },
		{ 4095.0f, 4095.0f, -24.1f }, SG_HOST_CONTENTS_SOLID
	};
	ground_fixture_t fixture;
	sg_ground_capability_set_t *set = NULL;
	sg_ground_capability_error_t error;
	sg_configuration_cell_t saved_cells[2];
	sg_configuration_semantic_region_t saved_regions[4];
	sg_configuration_portal_t saved_portal;
	sg_configuration_stance_overlap_t saved_stance_overlap;
	sg_rune_vec3_t saved_vertices[4];
	sg_configuration_semantic_face_t saved_semantic_faces[4];
	sg_rune_phase_basis_t saved_phases[4];
	sg_ground_phase_binding_t saved_bindings[4];
	sg_configuration_space_t saved_configuration;
	sg_configuration_semantics_t saved_semantics;
	GroundFixtureInit(&fixture, &floor, 1U, 800.0f,
		SG_RUNE_STANCE_STANDING, 0.0f, 0.0f);
	GroundFixtureRebind(&fixture);

	memcpy(saved_cells, fixture.cells, sizeof(saved_cells));
	memcpy(saved_regions, fixture.regions, sizeof(saved_regions));
	saved_portal = fixture.portal;
	saved_stance_overlap = fixture.stance_overlap;
	memcpy(saved_vertices, fixture.vertices, sizeof(saved_vertices));
	memcpy(saved_semantic_faces, fixture.semantic_faces,
		sizeof(saved_semantic_faces));
	memcpy(saved_phases, fixture.phases, sizeof(saved_phases));
	memcpy(saved_bindings, fixture.bindings, sizeof(saved_bindings));
	saved_configuration = fixture.configuration;
	saved_semantics = fixture.semantics;
	CHECK(GroundBuild(&fixture, &set, &error));
	SG_GroundCapabilityDestroy(set);
	set = NULL;
	CHECK(memcmp(saved_cells, fixture.cells, sizeof(saved_cells)) == 0);
	CHECK(memcmp(saved_regions, fixture.regions, sizeof(saved_regions)) == 0);
	CHECK(memcmp(&saved_portal, &fixture.portal, sizeof(saved_portal)) == 0);
	CHECK(memcmp(&saved_stance_overlap, &fixture.stance_overlap,
		sizeof(saved_stance_overlap)) == 0);
	CHECK(memcmp(saved_vertices, fixture.vertices, sizeof(saved_vertices)) == 0);
	CHECK(memcmp(saved_semantic_faces, fixture.semantic_faces,
		sizeof(saved_semantic_faces)) == 0);
	CHECK(memcmp(saved_phases, fixture.phases, sizeof(saved_phases)) == 0);
	CHECK(memcmp(saved_bindings, fixture.bindings, sizeof(saved_bindings)) == 0);
	CHECK(memcmp(&saved_configuration, &fixture.configuration,
		sizeof(saved_configuration)) == 0);
	CHECK(memcmp(&saved_semantics, &fixture.semantics,
		sizeof(saved_semantics)) == 0);
	fixture.semantics.identity.physics_abi_id++;
	CHECK(!GroundBuild(&fixture, &set, &error));
	CHECK(error.code == SG_GROUND_CAPABILITY_ERROR_IDENTITY_MISMATCH);
	fixture.semantics.identity.physics_abi_id--;
	CHECK(!SG_GroundCapabilityBuild(&fixture.authority,
		&fixture.configuration, &fixture.semantics, fixture.phases,
		(size_t)UINT32_MAX + 1U, fixture.bindings, 4U, Pmove,
		&set, &error));
	CHECK(error.code == SG_GROUND_CAPABILITY_ERROR_INVALID_PHASE);
	GroundFixtureDestroy(&fixture);
}

int main(void)
{
	TestFlatAndGravity(100.0f);
	TestFlatAndGravity(800.0f);
	TestCrouchWallWindowAndGap();
	TestOrdinaryAndTallStep();
	TestRamp();
	TestDirectedDrop();
	TestStanceOverlap();
	TestPortalPlaneScalingAndSubsetBindings();
	TestTwistedPortalRejects();
	TestPhaseVelocityAuthority();
	TestFatalOracleAndUnrepresentablePhase();
	TestExactSkewPortalAndZeroMargin();
	TestCrouchedTakeoffAndBlockedStanding();
	TestShallowWaterAndVoidPhaseMatching();
	TestDiscontinuousLowerLanding();
	TestAirborneStanceDoesNotRequireSupport();
	TestDestinationLocalizationIsUniqueAndGrounded();
	TestPortalBoundaryContactsReject();
	TestOnlyShallowWaterUsesGroundLane();
	TestPhysicsValidationAndLandingLaw();
	TestLocalizationIndexScaling();
	TestFullIdentityAndNoPvs();
	TestAtomicityIdentityAndHostileCounts();
	if (failures)
	{
		fprintf(stderr, "%d ground capability test failure(s)\n", failures);
		return 1;
	}
	puts("ground capability checks passed");
	return 0;
}
