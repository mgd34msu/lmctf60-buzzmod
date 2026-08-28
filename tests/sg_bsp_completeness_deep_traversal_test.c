#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../slipgate/sg_bsp_completeness_internal.h"

#define DEEP_NODE_COUNT UINT32_C(100000)

static uint32_t expected_leaf_visits;
static uint32_t expected_first_leaf = UINT32_MAX;
static uint32_t blocker_first_leaf = UINT32_MAX;

void SG_BspProofFail(sg_bsp_proof_context_t *proof,
	sg_bsp_completeness_code_t code, uint32_t record)
{
	proof->result.code = code;
	proof->result.record = record;
}

float SG_BspProofDot(const float left[3], const float right[3])
{
	return left[0] * right[0] + left[1] * right[1] +
		left[2] * right[2];
}

int SG_BspProofFiniteVector(const float value[3])
{
	return isfinite(value[0]) && isfinite(value[1]) && isfinite(value[2]);
}

int SG_BspProofCopyRegion(const sg_bsp_proof_region_t *source,
	sg_bsp_proof_region_t *destination)
{
	*destination = *source;
	destination->halfspaces = NULL;
	return 1;
}

void SG_BspProofFreeRegion(sg_bsp_proof_region_t *region)
{
	memset(region, 0, sizeof(*region));
}

int SG_BspProofAppendConstraint(const sg_bsp_proof_region_t *source,
	const sg_bsp_proof_halfspace_t *constraint,
	sg_bsp_proof_region_t *destination)
{
	(void)constraint;
	*destination = *source;
	destination->halfspaces = NULL;
	if (source->halfspace_count == UINT32_MAX)
		return 0;
	destination->halfspace_count++;
	return 1;
}

int SG_BspProofRegionHasProtocolPoint(sg_bsp_proof_context_t *proof,
	const sg_bsp_proof_region_t *region)
{
	(void)proof;
	(void)region;
	return 1;
}

int SG_BspProofRegionHasProtocolVolume(sg_bsp_proof_context_t *proof,
	const sg_bsp_proof_region_t *region)
{
	(void)proof;
	(void)region;
	return 1;
}

int SG_BspProofRegionBounds(sg_bsp_proof_context_t *proof,
	sg_bsp_proof_region_t *region)
{
	(void)proof;
	region->has_lattice_bounds = 1U;
	return 1;
}

int SG_BspProofAppendOwnedRegion(sg_bsp_proof_context_t *proof,
	sg_bsp_proof_regions_t *regions, sg_bsp_proof_region_t *region)
{
	(void)proof;
	if (blocker_first_leaf == UINT32_MAX)
		blocker_first_leaf = region->leaf;
	regions->count++;
	memset(region, 0, sizeof(*region));
	return 1;
}

int SG_BspProofAppendExpectedLeaf(sg_bsp_proof_context_t *proof,
	const sg_bsp_proof_region_t *region)
{
	(void)proof;
	if (expected_first_leaf == UINT32_MAX)
		expected_first_leaf = region->leaf;
	expected_leaf_visits++;
	return 1;
}

static int RunDeepWalks(void)
{
	sg_bsp_node_t *nodes = calloc(DEEP_NODE_COUNT, sizeof(*nodes));
	sg_bsp_plane_t plane;
	sg_bsp_leaf_t leaves[2];
	sg_bsp_model_t model;
	sg_bsp_brush_t brush;
	sg_bsp_brush_side_t side;
	uint32_t leaf_brush = 0U;
	sg_bsp_world_t world;
	sg_host_collision_authority_t authority;
	sg_bsp_proof_context_t proof;
	sg_bsp_proof_region_t region;
	uint32_t node;
	int success = 0;

	if (!nodes)
		return 0;
	memset(&plane, 0, sizeof(plane));
	memset(leaves, 0, sizeof(leaves));
	memset(&model, 0, sizeof(model));
	memset(&brush, 0, sizeof(brush));
	memset(&side, 0, sizeof(side));
	memset(&world, 0, sizeof(world));
	memset(&authority, 0, sizeof(authority));
	memset(&proof, 0, sizeof(proof));
	memset(&region, 0, sizeof(region));
	plane.normal.value[0] = 1.0f;
	plane.distance = -10000.0f;
	for (node = 0; node < DEEP_NODE_COUNT; node++)
	{
		nodes[node].plane = 0U;
		nodes[node].children[0] = node + 1U < DEEP_NODE_COUNT ?
			(int32_t)(node + 1U) : -1;
		nodes[node].children[1] = -2;
	}
	leaves[0].contents = SG_HOST_CONTENTS_SOLID;
	leaves[0].leaf_brush_count = 1U;
	brush.side_count = 1U;
	brush.contents = SG_HOST_CONTENTS_SOLID;
	world.planes = &plane;
	world.plane_count = 1U;
	world.nodes = nodes;
	world.node_count = DEEP_NODE_COUNT;
	world.leaves = leaves;
	world.leaf_count = 2U;
	world.leaf_brushes = &leaf_brush;
	world.leaf_brush_count = 1U;
	world.models = &model;
	world.model_count = 1U;
	world.brushes = &brush;
	world.brush_count = 1U;
	world.brush_sides = &side;
	world.brush_side_count = 1U;
	authority.world = &world;
	proof.authority = &authority;
	proof.blocker_bucket_count = 4U;
	proof.blockers = calloc(proof.blocker_bucket_count,
		sizeof(*proof.blockers));
	if (!proof.blockers)
		goto done;
	region.stance = SG_RUNE_STANCE_STANDING;
	if (!SG_BspProofReplayCollisionBsp(&proof, &region, 0) ||
		proof.result.collision_leaf_visits != DEEP_NODE_COUNT + 1U ||
		proof.result.blocker_cell_candidates != DEEP_NODE_COUNT + 1U ||
		blocker_first_leaf != 0U ||
		!SG_BspProofReplayBsp(&proof, &region, 0) ||
		expected_leaf_visits != DEEP_NODE_COUNT + 1U ||
		expected_first_leaf != 0U)
		goto done;
	success = 1;

done:
	free(proof.blockers);
	free(nodes);
	return success;
}

int main(void)
{
	if (!RunDeepWalks())
	{
		fputs("deep iterative BSP traversal failed\n", stderr);
		return 1;
	}
	puts("deep iterative BSP traversal passed");
	return 0;
}
