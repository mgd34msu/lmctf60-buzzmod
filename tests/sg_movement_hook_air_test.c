#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../slipgate/sg_movement_hook_air.h"

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

typedef struct proof_fixture_s
{
	sg_bsp_world_t world;
	sg_bsp_plane_t plane;
	sg_bsp_node_t node;
	sg_bsp_leaf_t leaves[2];
	sg_bsp_model_t model;
	sg_rune_model_identity_t identity;
	sg_host_collision_authority_t authority;
	sg_configuration_space_t *configuration;
	sg_configuration_semantics_t *semantics;
} proof_fixture_t;

static void Set3(float value[3], float x, float y, float z)
{
	value[0] = x;
	value[1] = y;
	value[2] = z;
}

static int ProofFixtureInit(proof_fixture_t *fixture)
{
	sg_host_collision_error_t host_error;
	sg_configuration_error_t configuration_error;
	sg_configuration_semantics_error_t semantics_error;
	sg_configuration_semantics_limits_t semantics_limits;

	memset(fixture, 0, sizeof(*fixture));
	Set3(fixture->plane.normal.value, 1.0f, 0.0f, 0.0f);
	fixture->plane.type = 0U;
	fixture->node.plane = 0U;
	fixture->node.children[0] = -1;
	fixture->node.children[1] = -2;
	fixture->leaves[0].cluster = 0;
	fixture->leaves[0].area = 1U;
	fixture->leaves[1].cluster = 1;
	fixture->leaves[1].area = 2U;
	fixture->model.headnode = 0;
	Set3(fixture->model.mins.value, -64.0f, -64.0f, -64.0f);
	Set3(fixture->model.maxs.value, 64.0f, 64.0f, 64.0f);
	fixture->world.planes = &fixture->plane;
	fixture->world.plane_count = 1U;
	fixture->world.nodes = &fixture->node;
	fixture->world.node_count = 1U;
	fixture->world.leaves = fixture->leaves;
	fixture->world.leaf_count = 2U;
	fixture->world.models = &fixture->model;
	fixture->world.model_count = 1U;
	fixture->identity.bsp_content_id = UINT64_C(0x6001);
	fixture->identity.entity_semantics_id = UINT64_C(0x6002);
	fixture->identity.physics_abi_id = UINT64_C(0x6003);
	fixture->identity.source_set_identity = UINT64_C(0x6004);
	fixture->identity.schema_id = UINT64_C(0x6005);
	fixture->identity.producer_identity = UINT64_C(0x6006);
	Set3(fixture->identity.standing_hull.mins.value,
		-16.0f, -16.0f, -24.0f);
	Set3(fixture->identity.standing_hull.maxs.value,
		16.0f, 16.0f, 32.0f);
	Set3(fixture->identity.crouching_hull.mins.value,
		-16.0f, -16.0f, -24.0f);
	Set3(fixture->identity.crouching_hull.maxs.value,
		16.0f, 16.0f, 4.0f);
	fixture->identity.physics.gravity = 100.0f;
	fixture->identity.physics.ground_acceleration = 10.0f;
	fixture->identity.physics.air_acceleration = 1.0f;
	fixture->identity.physics.water_acceleration = 10.0f;
	fixture->identity.physics.hook_acceleration = 800.0f;
	fixture->identity.physics.external_acceleration = 1.0f;
	fixture->identity.physics.water_drag = 1.0f;
	fixture->identity.physics.max_velocity = 2000.0f;
	fixture->identity.physics.frame_ms = 100U;
	fixture->identity.physics.substep_ms = 10U;
	if (!SG_HostCollisionInit(&fixture->authority, &fixture->world,
			&fixture->identity, &host_error))
	{
		fprintf(stderr, "host init failed: %u\n", (unsigned)host_error);
		return 0;
	}
	if (!SG_ConfigurationBuild(&fixture->authority, NULL,
			&fixture->configuration, &configuration_error))
	{
		fprintf(stderr, "configuration build failed: %s at %u\n",
			SG_ConfigurationErrorString(configuration_error.code),
			configuration_error.source_index);
		return 0;
	}
	SG_ConfigurationSemanticsDefaultLimits(&semantics_limits);
	if (!SG_ConfigurationSemanticsBuild(&fixture->authority,
			fixture->configuration, &semantics_limits, &fixture->semantics,
			&semantics_error))
	{
		fprintf(stderr, "semantics build failed: %s at %u\n",
			SG_ConfigurationSemanticsErrorString(semantics_error.code),
			semantics_error.source_index);
		return 0;
	}
	return 1;
}

static void ProofFixtureDestroy(proof_fixture_t *fixture)
{
	SG_ConfigurationSemanticsDestroy(fixture->semantics);
	SG_ConfigurationDestroy(fixture->configuration);
	memset(fixture, 0, sizeof(*fixture));
}

static void CheckWorldAxisCommands(void)
{
	usercmd_t command;
	sg_rune_vec3_t vector;

	CHECK(SG_MovementHookAirCommandForDirection(
		SG_MOVEMENT_AIR_DIRECTION_POSITIVE_X, &command, &vector));
	CHECK(command.forwardmove == 400);
	CHECK(command.sidemove == 0);
	CHECK(vector.value[0] == 400.0f && vector.value[1] == 0.0f);
	CHECK(SG_MovementHookAirCommandForDirection(
		SG_MOVEMENT_AIR_DIRECTION_NEGATIVE_X, &command, &vector));
	CHECK(command.forwardmove == -400);
	CHECK(command.sidemove == 0);
	CHECK(vector.value[0] == -400.0f && vector.value[1] == 0.0f);
	CHECK(SG_MovementHookAirCommandForDirection(
		SG_MOVEMENT_AIR_DIRECTION_POSITIVE_Y, &command, &vector));
	CHECK(command.forwardmove == 0);
	CHECK(command.sidemove == -400);
	CHECK(vector.value[0] == 0.0f && vector.value[1] == 400.0f);
	CHECK(SG_MovementHookAirCommandForDirection(
		SG_MOVEMENT_AIR_DIRECTION_NEGATIVE_Y, &command, &vector));
	CHECK(command.forwardmove == 0);
	CHECK(command.sidemove == 400);
	CHECK(vector.value[0] == 0.0f && vector.value[1] == -400.0f);
	CHECK(!SG_MovementHookAirCommandForDirection(
		SG_MOVEMENT_AIR_DIRECTION_NONE, &command, &vector));
}

static void CheckFlightChronology(void)
{
	uint32_t frames = 0U;

	CHECK(SG_MovementHookAirFlightFrameCount(64.0f, 800.0f, 100U,
		&frames));
	CHECK(frames == 0U);
	CHECK(SG_MovementHookAirFlightFrameCount(80.0f, 800.0f, 100U,
		&frames));
	CHECK(frames == 0U);
	CHECK(SG_MovementHookAirFlightFrameCount(80.125f, 800.0f, 100U,
		&frames));
	CHECK(frames == 1U);
	/* A 300 ms total flight consists of two outbound body frames followed by
	 * the attachment frame. */
	CHECK(SG_MovementHookAirFlightFrameCount(240.0f, 800.0f, 100U,
		&frames));
	CHECK(frames == 2U);
	CHECK(SG_MovementHookAirFlightFrameCount(320.0f, 800.0f, 100U,
		&frames));
	CHECK(frames == 3U);
	CHECK(!SG_MovementHookAirFlightFrameCount(0.0f, 800.0f, 100U,
		&frames));
	CHECK(!SG_MovementHookAirFlightFrameCount(1.0f, NAN, 100U, &frames));
	CHECK(!SG_MovementHookAirFlightFrameCount(1.0f, 800.0f, 0U, &frames));
}

static void CheckProofsRecomputedAndProviderBlocked(void)
{
	proof_fixture_t fixture;
	sg_movement_hook_air_sources_t sources;
	sg_movement_hook_air_set_t *set = NULL;
	sg_movement_hook_air_error_t error;
	uint32_t region_count;
	const void *opaque_storage;

	CHECK(ProofFixtureInit(&fixture));
	if (!fixture.configuration || !fixture.semantics)
		return;
	memset(&sources, 0, sizeof(sources));
	sources.collision = &fixture.authority;
	sources.configuration = fixture.configuration;
	sources.semantics = fixture.semantics;
	CHECK(!SG_MovementHookAirBuild(&sources, &set, &error));
	CHECK(set == NULL);
	CHECK(error.code ==
		SG_MOVEMENT_HOOK_AIR_ERROR_VISIBILITY_PREREQUISITE_UNAVAILABLE);
	CHECK(error.bsp_code == SG_BSP_COMPLETENESS_OK);
	CHECK(error.semantics_code == SG_CONFIGURATION_SEMANTICS_AUDIT_OK);

	region_count = fixture.semantics->region_count;
	fixture.semantics->region_count = region_count ? region_count - 1U : 1U;
	CHECK(!SG_MovementHookAirBuild(&sources, &set, &error));
	CHECK(error.code ==
		SG_MOVEMENT_HOOK_AIR_ERROR_INCOMPLETE_SEMANTICS_PROOF);
	fixture.semantics->region_count = region_count;

	fixture.configuration->cell_count--;
	CHECK(!SG_MovementHookAirBuild(&sources, &set, &error));
	CHECK(error.code == SG_MOVEMENT_HOOK_AIR_ERROR_INCOMPLETE_BSP_PROOF);
	fixture.configuration->cell_count++;

	/* Even non-null opaque storage cannot stand in for an audited provider. */
	opaque_storage = &sources;
	sources.visibility = (const sg_hook_visibility_production_publication_t *)
		opaque_storage;
	sources.host_laws = (const sg_host_law_production_publication_t *)
		opaque_storage;
	CHECK(!SG_MovementHookAirBuild(&sources, &set, &error));
	CHECK(error.code ==
		SG_MOVEMENT_HOOK_AIR_ERROR_VISIBILITY_PREREQUISITE_UNAVAILABLE);
	ProofFixtureDestroy(&fixture);
}

int main(void)
{
	CheckWorldAxisCommands();
	CheckFlightChronology();
	CheckProofsRecomputedAndProviderBlocked();
	if (failures)
		return 1;
	puts("hook-air waits for audited production visibility and host laws");
	return 0;
}
