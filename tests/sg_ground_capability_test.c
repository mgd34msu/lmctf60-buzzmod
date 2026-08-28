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

void Com_DPrintf(const char *format, ...)
{
	(void)format;
}

void Com_Printf(char *format, ...)
{
	(void)format;
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
	sg_rune_model_identity_t identity = Identity();
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
	SetRune3(&fixture->vertices[0], 0.0f, -32.0f, 0.0f);
	SetRune3(&fixture->vertices[1], 0.0f, 32.0f, 0.0f);
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
	const sg_ground_capability_limits_t *limits,
	sg_ground_capability_set_t **set_out,
	sg_ground_capability_error_t *error_out)
{
	return SG_GroundCapabilityBuild(&fixture->authority,
		&fixture->configuration, &fixture->semantics, fixture->phases, 4U,
		fixture->bindings, 4U, Pmove, limits, set_out, error_out);
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

	CHECK(GroundBuild(&fixture, NULL, &first, &error));
	CHECK(first != NULL);
	if (first)
	{
		CHECK(HasKind(first, SG_GROUND_CAPABILITY_WALK));
		CHECK(HasKind(first, SG_GROUND_CAPABILITY_JUMP_TAKEOFF));
		CHECK(first->identity.physics.gravity == gravity);
		CHECK(GroundBuild(&fixture, NULL, &second, &error));
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

	CHECK(GroundBuild(&fixture, NULL, &set, &error));
	CHECK(set && HasKind(set, SG_GROUND_CAPABILITY_CROUCH));
	SG_GroundCapabilityDestroy(set);
	GroundFixtureDestroy(&fixture);
	GroundFixtureInit(&fixture, low_clearance, 2U, 800.0f,
		SG_RUNE_STANCE_CROUCHING, 0.0f, 0.0f);
	GroundFixtureRebind(&fixture);
	set = NULL;
	CHECK(GroundBuild(&fixture, NULL, &set, &error));
	CHECK(set && HasKind(set, SG_GROUND_CAPABILITY_CROUCH));
	SG_GroundCapabilityDestroy(set);
	GroundFixtureDestroy(&fixture);
	GroundFixtureInit(&fixture, low_clearance, 2U, 800.0f,
		SG_RUNE_STANCE_STANDING, 0.0f, 0.0f);
	GroundFixtureRebind(&fixture);
	set = NULL;
	CHECK(GroundBuild(&fixture, NULL, &set, &error));
	CHECK(set && !HasKind(set, SG_GROUND_CAPABILITY_WALK));
	SG_GroundCapabilityDestroy(set);
	GroundFixtureDestroy(&fixture);

	GroundFixtureInit(&fixture, blocked, 2U, 800.0f,
		SG_RUNE_STANCE_STANDING, 0.0f, 0.0f);
	GroundFixtureRebind(&fixture);
	set = NULL;
	CHECK(GroundBuild(&fixture, NULL, &set, &error));
	CHECK(set && !HasKind(set, SG_GROUND_CAPABILITY_WALK));
	CHECK(set && set->rejected_crossings == 1U);
	SG_GroundCapabilityDestroy(set);
	GroundFixtureDestroy(&fixture);

	blocked[1].contents = SG_HOST_CONTENTS_WINDOW;
	GroundFixtureInit(&fixture, blocked, 2U, 800.0f,
		SG_RUNE_STANCE_STANDING, 0.0f, 0.0f);
	GroundFixtureRebind(&fixture);
	set = NULL;
	CHECK(GroundBuild(&fixture, NULL, &set, &error));
	CHECK(set && !HasKind(set, SG_GROUND_CAPABILITY_WALK));
	SG_GroundCapabilityDestroy(set);
	GroundFixtureDestroy(&fixture);

	GroundFixtureInit(&fixture, separated_floors, 2U, 800.0f,
		SG_RUNE_STANCE_STANDING, 0.0f, 0.0f);
	GroundFixtureRebind(&fixture);
	set = NULL;
	CHECK(GroundBuild(&fixture, NULL, &set, &error));
	CHECK(set && !HasKind(set, SG_GROUND_CAPABILITY_WALK));
	SG_GroundCapabilityDestroy(set);
	GroundFixtureDestroy(&fixture);

	GroundFixtureInit(&fixture, NULL, 0U, 800.0f,
		SG_RUNE_STANCE_STANDING, 0.0f, 0.0f);
	GroundFixtureRebind(&fixture);
	set = NULL;
	CHECK(GroundBuild(&fixture, NULL, &set, &error));
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

	CHECK(GroundBuild(&fixture, NULL, &set, &error));
	CHECK(set && HasKind(set, SG_GROUND_CAPABILITY_STEP));
	SG_GroundCapabilityDestroy(set);
	GroundFixtureDestroy(&fixture);

	GroundFixtureInit(&fixture, tall, 2U, 800.0f,
		SG_RUNE_STANCE_STANDING, 0.0f, 24.0f);
	GroundFixtureRebind(&fixture);
	set = NULL;
	CHECK(GroundBuild(&fixture, NULL, &set, &error));
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
	CHECK(GroundBuild(&fixture, NULL, &set, &error));
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
	CHECK(GroundBuild(&fixture, NULL, &set, &error));
	CHECK(set && HasKind(set, SG_GROUND_CAPABILITY_DROP));
	CHECK(set && !HasKind(set, SG_GROUND_CAPABILITY_WALK));
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
	sg_ground_capability_limits_t limits = { 2U };
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
	CHECK(GroundBuild(&fixture, NULL, &set, &error));
	CHECK(set && CountKind(set, SG_GROUND_CAPABILITY_STANCE) == 2U);
	SG_GroundCapabilityDestroy(set);
	set = NULL;
	duplicate_overlaps[0] = fixture.stance_overlap;
	duplicate_overlaps[1] = fixture.stance_overlap;
	fixture.configuration.stance_overlaps = duplicate_overlaps;
	fixture.configuration.stance_overlap_count = 2U;
	CHECK(SG_GroundCapabilityBuild(&fixture.authority,
		&fixture.configuration, &fixture.semantics, fixture.phases, 4U,
		supported_bindings, 2U, Pmove, &limits, &set, &error));
	CHECK(set && set->capability_count == 2U);
	SG_GroundCapabilityDestroy(set);
	set = NULL;
	limits.max_capabilities = 1U;
	CHECK(!SG_GroundCapabilityBuild(&fixture.authority,
		&fixture.configuration, &fixture.semantics, fixture.phases, 4U,
		supported_bindings, 2U, Pmove, &limits, &set, &error));
	CHECK(set == NULL);
	CHECK(error.code == SG_GROUND_CAPABILITY_ERROR_OVERFLOW);
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

	CHECK(GroundBuild(&fixture, NULL, &unscaled, &error));
	fixture.portal.plane.normal[0] *= 1024.0f;
	fixture.portal.plane.distance *= 1024.0f;
	CHECK(GroundBuild(&fixture, NULL, &scaled, &error));
	CHECK(unscaled && scaled &&
		unscaled->capability_count == scaled->capability_count);
	if (unscaled && scaled &&
		unscaled->capability_count == scaled->capability_count)
		CHECK(memcmp(unscaled->capabilities, scaled->capabilities,
			(size_t)unscaled->capability_count *
				sizeof(*unscaled->capabilities)) == 0);
	CHECK(SG_GroundCapabilityBuild(&fixture.authority,
		&fixture.configuration, &fixture.semantics, fixture.phases, 4U,
		subset_bindings, 2U, Pmove, NULL, &subset, &error));
	CHECK(subset && HasKind(subset, SG_GROUND_CAPABILITY_WALK));
	SG_GroundCapabilityDestroy(unscaled);
	SG_GroundCapabilityDestroy(scaled);
	SG_GroundCapabilityDestroy(subset);
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
	CHECK(GroundBuild(&fixture, NULL, &set, &error));
	CHECK(set && !HasPortalSourcePhase(set, 0U, 1U, 0U));
	SG_GroundCapabilityDestroy(set);
	set = NULL;

	fixture.phases[0].velocity.x.min_value = 100.0f;
	fixture.phases[0].velocity.x.max_value = 100.0f;
	fixture.phases[2].velocity.x.min_value = 1000.0f;
	fixture.phases[2].velocity.x.max_value = 2000.0f;
	CHECK(GroundBuild(&fixture, NULL, &set, &error));
	CHECK(set && !HasPortalSourcePhase(set, 0U, 1U, 0U));
	CHECK(set && set->proved_portals == 0U);
	CHECK(set && set->rejected_crossings == 1U);
	SG_GroundCapabilityDestroy(set);
	GroundFixtureDestroy(&fixture);
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
	sg_ground_capability_limits_t limits = { 1U };
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
	CHECK(GroundBuild(&fixture, NULL, &set, &error));
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
	CHECK(!GroundBuild(&fixture, &limits, &set, &error));
	CHECK(set == NULL);
	CHECK(error.code == SG_GROUND_CAPABILITY_ERROR_OVERFLOW);
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
	CHECK(!GroundBuild(&fixture, NULL, &set, &error));
	CHECK(error.code == SG_GROUND_CAPABILITY_ERROR_IDENTITY_MISMATCH);
	fixture.semantics.identity.physics_abi_id--;
	CHECK(!SG_GroundCapabilityBuild(&fixture.authority,
		&fixture.configuration, &fixture.semantics, fixture.phases,
		(size_t)UINT32_MAX + 1U, fixture.bindings, 4U, Pmove, NULL,
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
	TestPhaseVelocityAuthority();
	TestAtomicityIdentityAndHostileCounts();
	if (failures)
	{
		fprintf(stderr, "%d ground capability test failure(s)\n", failures);
		return 1;
	}
	puts("ground capability checks passed");
	return 0;
}
