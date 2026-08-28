#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../slipgate/sg_bsp_completeness_proof.h"

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static void SetVector(float value[3], float x, float y, float z)
{
	value[0] = x;
	value[1] = y;
	value[2] = z;
}

static sg_rune_model_identity_t Identity(void)
{
	sg_rune_model_identity_t identity;

	memset(&identity, 0, sizeof(identity));
	identity.bsp_content_id = UINT64_C(1);
	identity.physics_abi_id = UINT64_C(2);
	SetVector(identity.standing_hull.mins.value, -16.0f, -16.0f, -24.0f);
	SetVector(identity.standing_hull.maxs.value, 16.0f, 16.0f, 32.0f);
	SetVector(identity.crouching_hull.mins.value, -16.0f, -16.0f, -24.0f);
	SetVector(identity.crouching_hull.maxs.value, 16.0f, 16.0f, 4.0f);
	identity.physics.gravity = 800.0f;
	identity.physics.ground_acceleration = 10.0f;
	identity.physics.air_acceleration = 1.0f;
	identity.physics.water_acceleration = 10.0f;
	identity.physics.hook_acceleration = 800.0f;
	identity.physics.external_acceleration = 1.0f;
	identity.physics.water_drag = 1.0f;
	identity.physics.max_velocity = 2000.0f;
	identity.physics.frame_ms = 100U;
	identity.physics.substep_ms = 10U;
	return identity;
}

static sg_configuration_space_t EmptySpace(
	const sg_rune_model_identity_t *identity)
{
	sg_configuration_space_t space;
	uint32_t axis;

	memset(&space, 0, sizeof(space));
	space.identity = *identity;
	for (axis = 0; axis < 3U; axis++)
	{
		space.domain.mins.value[axis] = SG_CONFIGURATION_PMOVE_ORIGIN_MIN;
		space.domain.maxs.value[axis] = SG_CONFIGURATION_PMOVE_ORIGIN_MAX;
	}
	return space;
}

static void BaseWorld(sg_bsp_world_t *world, sg_bsp_plane_t *plane,
	sg_bsp_node_t *nodes, uint32_t node_count, sg_bsp_leaf_t leaves[2],
	sg_bsp_model_t *model)
{
	memset(world, 0, sizeof(*world));
	memset(plane, 0, sizeof(*plane));
	memset(nodes, 0, (size_t)node_count * sizeof(*nodes));
	memset(leaves, 0, 2U * sizeof(*leaves));
	memset(model, 0, sizeof(*model));
	plane->normal.value[0] = 1.0f;
	model->headnode = 0;
	world->planes = plane;
	world->plane_count = 1U;
	world->nodes = nodes;
	world->node_count = node_count;
	world->leaves = leaves;
	world->leaf_count = 2U;
	world->models = model;
	world->model_count = 1U;
}

static void ExpectInvalidGraph(sg_bsp_world_t *world,
	const sg_rune_model_identity_t *identity)
{
	sg_host_collision_authority_t authority;
	sg_host_collision_error_t host_error;
	sg_configuration_space_t space = EmptySpace(identity);
	sg_bsp_completeness_result_t result;

	CHECK(SG_HostCollisionInit(&authority, world, identity, &host_error));
	CHECK(!SG_BspCompletenessProve(&authority, &space, &result));
	CHECK(result.code == SG_BSP_COMPLETENESS_INVALID_WORLD);
}

static void TestMalformedGraphs(void)
{
	sg_rune_model_identity_t identity = Identity();
	sg_bsp_world_t world;
	sg_bsp_plane_t plane;
	sg_bsp_node_t nodes[2];
	sg_bsp_leaf_t leaves[2];
	sg_bsp_model_t model;

	BaseWorld(&world, &plane, nodes, 1U, leaves, &model);
	nodes[0].children[0] = 0;
	nodes[0].children[1] = -1;
	ExpectInvalidGraph(&world, &identity);

	BaseWorld(&world, &plane, nodes, 2U, leaves, &model);
	nodes[0].children[0] = 1;
	nodes[0].children[1] = -1;
	nodes[1].children[0] = -2;
	nodes[1].children[1] = 0;
	ExpectInvalidGraph(&world, &identity);

	BaseWorld(&world, &plane, nodes, 1U, leaves, &model);
	nodes[0].children[0] = 1;
	nodes[0].children[1] = -1;
	ExpectInvalidGraph(&world, &identity);

	BaseWorld(&world, &plane, nodes, 1U, leaves, &model);
	nodes[0].children[0] = -3;
	nodes[0].children[1] = -1;
	ExpectInvalidGraph(&world, &identity);
}

static void TestRegionKeyOverflowDiagnostic(void)
{
	sg_rune_model_identity_t identity = Identity();
	sg_bsp_world_t world;
	sg_bsp_leaf_t leaf;
	sg_bsp_model_t model;
	sg_host_collision_authority_t authority;
	sg_configuration_space_t space = EmptySpace(&identity);
	sg_bsp_completeness_result_t result;

	memset(&world, 0, sizeof(world));
	memset(&leaf, 0, sizeof(leaf));
	memset(&model, 0, sizeof(model));
	model.headnode = -1;
	world.leaves = &leaf;
	world.leaf_count = UINT32_MAX / (uint32_t)SG_RUNE_STANCE_COUNT + 1U;
	world.models = &model;
	world.model_count = 1U;
	authority.world = &world;
	authority.identity = identity;
	CHECK(!SG_BspCompletenessProve(&authority, &space, &result));
	CHECK(result.code == SG_BSP_COMPLETENESS_OVERFLOW);
	CHECK(result.record == world.leaf_count);
}

int main(void)
{
	TestMalformedGraphs();
	TestRegionKeyOverflowDiagnostic();
	if (failures)
	{
		fprintf(stderr, "%d BSP world guard checks failed\n", failures);
		return 1;
	}
	puts("BSP world guard checks passed");
	return 0;
}
