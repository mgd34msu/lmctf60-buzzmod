#include "sg_bsp_completeness_internal.h"
#include "sg_configuration_lattice.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef enum bsp_walk_kind_e
{
	BSP_WALK_BLOCKER = 0,
	BSP_WALK_COLLISION,
	BSP_WALK_EXPECTED
} bsp_walk_kind_t;

typedef struct bsp_walk_frame_s
{
	sg_bsp_proof_region_t region;
	int32_t child;
	uint8_t branch;
	uint8_t owns_region;
} bsp_walk_frame_t;

typedef struct bsp_walk_stack_s
{
	bsp_walk_frame_t *frames;
	uint32_t count;
	uint32_t capacity;
} bsp_walk_stack_t;

static sg_bsp_proof_halfspace_t BspPlane(const sg_bsp_plane_t *source,
	int front)
{
	sg_bsp_proof_halfspace_t plane;
	uint32_t axis;

	memset(&plane, 0, sizeof(plane));
	for (axis = 0; axis < 3U; axis++)
		plane.normal[axis] = front ? -source->normal.value[axis] :
			source->normal.value[axis];
	plane.distance = front ? -source->distance : source->distance;
	plane.open = (uint8_t)!front;
	return plane;
}

static float HullMinimum(const sg_rune_hull_profile_t *hull,
	const float normal[3])
{
	float result = 0.0f;
	uint32_t axis;

	for (axis = 0; axis < 3U; axis++)
		result += normal[axis] < 0.0f ?
			normal[axis] * hull->maxs.value[axis] :
			normal[axis] * hull->mins.value[axis];
	return result;
}

static int BrushPlane(const sg_bsp_proof_context_t *proof, uint32_t brush,
	uint32_t side_offset, sg_rune_stance_t stance,
	sg_bsp_proof_halfspace_t *plane_out)
{
	const sg_bsp_world_t *world = proof->authority->world;
	const sg_bsp_brush_t *record = &world->brushes[brush];
	uint32_t side_index = record->first_side + side_offset;
	const sg_bsp_plane_t *source;
	const sg_rune_hull_profile_t *hull;

	if (side_index >= world->brush_side_count ||
		world->brush_sides[side_index].plane >= world->plane_count)
		return 0;
	source = &world->planes[world->brush_sides[side_index].plane];
	hull = stance == SG_RUNE_STANCE_STANDING ?
		&proof->authority->identity.standing_hull :
		&proof->authority->identity.crouching_hull;
	memset(plane_out, 0, sizeof(*plane_out));
	memcpy(plane_out->normal, source->normal.value,
		sizeof(plane_out->normal));
	plane_out->distance = source->distance -
		HullMinimum(hull, source->normal.value);
	return SG_BspProofFiniteVector(plane_out->normal) &&
		isfinite(plane_out->distance);
}

static int BlockingBrush(const sg_bsp_brush_t *brush)
{
	return brush->side_count != 0U &&
		((uint32_t)brush->contents & SG_HOST_MASK_PLAYER_SOLID) != 0U;
}

static int AppendAllBrushPlanes(sg_bsp_proof_context_t *proof,
	const sg_bsp_proof_region_t *region, uint32_t brush,
	sg_bsp_proof_region_t *intersection)
{
	const sg_bsp_brush_t *record = &proof->authority->world->brushes[brush];
	sg_bsp_proof_region_t current;
	uint32_t side;

	if (!SG_BspProofCopyRegion(region, &current))
		return 0;
	for (side = 0; side < record->side_count; side++)
	{
		sg_bsp_proof_halfspace_t plane;
		sg_bsp_proof_region_t next;

		if (!BrushPlane(proof, brush, side, region->stance, &plane) ||
			!SG_BspProofAppendConstraint(&current, &plane, &next))
		{
			SG_BspProofFreeRegion(&current);
			return 0;
		}
		SG_BspProofFreeRegion(&current);
		current = next;
	}
	*intersection = current;
	return 1;
}

static float CollisionRoundingAllowance(const sg_bsp_plane_t *plane,
	const sg_rune_hull_profile_t *hull)
{
	float scale = fabsf(plane->distance) + 1.0f;
	uint32_t axis;

	for (axis = 0; axis < 3U; axis++)
	{
		float hull_extent = fmaxf(fabsf(hull->mins.value[axis]),
			fabsf(hull->maxs.value[axis]));
		float origin_extent = fmaxf(-SG_CONFIGURATION_PMOVE_ORIGIN_MIN,
			SG_CONFIGURATION_PMOVE_ORIGIN_MAX);

		scale += fabsf(plane->normal.value[axis]) *
			(origin_extent + hull_extent + 1.0f);
	}
	return scale * FLT_EPSILON * 16.0f;
}

static sg_bsp_proof_halfspace_t CollisionBspPlane(
	const sg_bsp_plane_t *source, const sg_rune_hull_profile_t *hull,
	uint32_t branch)
{
	sg_bsp_proof_halfspace_t plane;
	float corner[3];
	float corner_distance;
	float allowance = CollisionRoundingAllowance(source, hull);
	uint32_t axis;

	memset(&plane, 0, sizeof(plane));
	if (branch == 0U)
	{
		for (axis = 0; axis < 3U; axis++)
		{
			plane.normal[axis] = -source->normal.value[axis];
			corner[axis] = source->normal.value[axis] < 0.0f ?
				hull->mins.value[axis] - 1.0f :
				hull->maxs.value[axis] + 1.0f;
		}
		corner_distance = SG_BspProofDot(corner, source->normal.value);
		plane.distance = corner_distance - source->distance;
	}
	else
	{
		memcpy(plane.normal, source->normal.value, sizeof(plane.normal));
		for (axis = 0; axis < 3U; axis++)
			corner[axis] = source->normal.value[axis] < 0.0f ?
				hull->maxs.value[axis] + 1.0f :
				hull->mins.value[axis] - 1.0f;
		corner_distance = SG_BspProofDot(corner, source->normal.value);
		plane.distance = source->distance - corner_distance;
		plane.open = 1U;
	}
	plane.distance += allowance;
	return plane;
}

static int GrowWalkStack(sg_bsp_proof_context_t *proof,
	bsp_walk_stack_t *stack)
{
	bsp_walk_frame_t *grown;
	uint32_t capacity;
	size_t maximum = SIZE_MAX / sizeof(*grown);

	if (stack->count < stack->capacity)
		return 1;
	if (stack->count == UINT32_MAX)
	{
		SG_BspProofFail(proof, SG_BSP_COMPLETENESS_OVERFLOW, stack->count);
		return 0;
	}
	if (!stack->capacity)
		capacity = 64U;
	else if (stack->capacity <= UINT32_MAX / 2U)
		capacity = stack->capacity * 2U;
	else
		capacity = UINT32_MAX;
	if (capacity <= stack->count)
		capacity = stack->count + 1U;
	if (maximum < UINT32_MAX && capacity > (uint32_t)maximum)
	{
		SG_BspProofFail(proof, SG_BSP_COMPLETENESS_OVERFLOW, stack->count);
		return 0;
	}
	grown = realloc(stack->frames, (size_t)capacity * sizeof(*grown));
	if (!grown)
	{
		SG_BspProofFail(proof, SG_BSP_COMPLETENESS_OUT_OF_MEMORY, stack->count);
		return 0;
	}
	stack->frames = grown;
	stack->capacity = capacity;
	return 1;
}

static void FreeWalkStack(bsp_walk_stack_t *stack)
{
	uint32_t frame;

	for (frame = 0; frame < stack->count; frame++)
		if (stack->frames[frame].owns_region)
			SG_BspProofFreeRegion(&stack->frames[frame].region);
	free(stack->frames);
	memset(stack, 0, sizeof(*stack));
}

static int AppendBlockerLeaf(sg_bsp_proof_context_t *proof,
	const sg_bsp_proof_region_t *region, uint32_t leaf)
{
	const sg_bsp_world_t *world = proof->authority->world;
	sg_bsp_proof_region_t blocker;
	uint32_t bucket;
	int bounded;

	if (!SG_BspProofCopyRegion(region, &blocker))
		return 0;
	blocker.leaf = leaf;
	bounded = SG_BspProofRegionBounds(proof, &blocker);
	if (bounded <= 0)
	{
		SG_BspProofFreeRegion(&blocker);
		return bounded == 0;
	}
	bucket = (uint32_t)region->stance * world->leaf_count + leaf;
	proof->result.blocker_cell_candidates++;
	if (!SG_BspProofAppendOwnedRegion(proof, &proof->blockers[bucket],
			&blocker))
	{
		SG_BspProofFreeRegion(&blocker);
		return 0;
	}
	return 1;
}

static int WalkBsp(sg_bsp_proof_context_t *proof,
	const sg_bsp_proof_region_t *region, int32_t child, bsp_walk_kind_t kind);

static int AppendCollisionLeaf(sg_bsp_proof_context_t *proof,
	const sg_bsp_proof_region_t *region, uint32_t leaf_index)
{
	const sg_bsp_world_t *world = proof->authority->world;
	const sg_bsp_leaf_t *leaf = &world->leaves[leaf_index];
	uint32_t offset;

	proof->result.collision_leaf_visits++;
	if (!((uint32_t)leaf->contents & SG_HOST_MASK_PLAYER_SOLID))
		return 1;
	if (leaf->first_leaf_brush > world->leaf_brush_count ||
		leaf->leaf_brush_count > world->leaf_brush_count -
			leaf->first_leaf_brush)
		return 0;
	for (offset = 0; offset < leaf->leaf_brush_count; offset++)
	{
		uint32_t brush_index = world->leaf_brushes[
			leaf->first_leaf_brush + offset];
		sg_bsp_proof_region_t blocker;
		int present;

		proof->result.leaf_brush_candidates++;
		if (brush_index >= world->brush_count)
			return 0;
		if (!BlockingBrush(&world->brushes[brush_index]))
			continue;
		if (world->brushes[brush_index].first_side > world->brush_side_count ||
			world->brushes[brush_index].side_count > world->brush_side_count -
				world->brushes[brush_index].first_side ||
			!AppendAllBrushPlanes(proof, region, brush_index, &blocker))
			return 0;
		present = SG_BspProofRegionHasProtocolPoint(proof, &blocker);
		if (present < 0 || (present && !WalkBsp(proof, &blocker,
				world->models[0].headnode, BSP_WALK_BLOCKER)))
		{
			SG_BspProofFreeRegion(&blocker);
			return 0;
		}
		SG_BspProofFreeRegion(&blocker);
	}
	return 1;
}

static int VisitLeaf(sg_bsp_proof_context_t *proof,
	const sg_bsp_proof_region_t *region, uint32_t leaf, bsp_walk_kind_t kind)
{
	if (kind == BSP_WALK_BLOCKER)
		return AppendBlockerLeaf(proof, region, leaf);
	if (kind == BSP_WALK_COLLISION)
		return AppendCollisionLeaf(proof, region, leaf);
	{
		sg_bsp_proof_region_t terminal;
		int result;

		if (!SG_BspProofCopyRegion(region, &terminal))
			return 0;
		terminal.leaf = leaf;
		result = SG_BspProofAppendExpectedLeaf(proof, &terminal);
		SG_BspProofFreeRegion(&terminal);
		return result;
	}
}

static int WalkBsp(sg_bsp_proof_context_t *proof,
	const sg_bsp_proof_region_t *region, int32_t child, bsp_walk_kind_t kind)
{
	const sg_bsp_world_t *world = proof->authority->world;
	const sg_rune_hull_profile_t *hull =
		region->stance == SG_RUNE_STANCE_STANDING ?
		&proof->authority->identity.standing_hull :
		&proof->authority->identity.crouching_hull;
	bsp_walk_stack_t stack = { 0 };
	int result = 0;

	if (!GrowWalkStack(proof, &stack))
		return 0;
	stack.frames[0].region = *region;
	stack.frames[0].child = child;
	stack.frames[0].branch = 0U;
	stack.frames[0].owns_region = 0U;
	stack.count = 1U;
	while (stack.count)
	{
		bsp_walk_frame_t *frame = &stack.frames[stack.count - 1U];
		const sg_bsp_node_t *node;
		sg_bsp_proof_halfspace_t plane;
		sg_bsp_proof_region_t split;
		uint32_t branch;
		int present;

		if (frame->child < 0)
		{
			uint32_t leaf = ~(uint32_t)frame->child;

			if (leaf >= world->leaf_count ||
				!VisitLeaf(proof, &frame->region, leaf, kind))
				goto done;
			if (frame->owns_region)
				SG_BspProofFreeRegion(&frame->region);
			stack.count--;
			continue;
		}
		if ((uint32_t)frame->child >= world->node_count ||
			world->nodes[(uint32_t)frame->child].plane >= world->plane_count)
			goto done;
		if (frame->branch == 2U)
		{
			if (frame->owns_region)
				SG_BspProofFreeRegion(&frame->region);
			stack.count--;
			continue;
		}
		node = &world->nodes[(uint32_t)frame->child];
		branch = frame->branch++;
		plane = kind == BSP_WALK_COLLISION ?
			CollisionBspPlane(&world->planes[node->plane], hull, branch) :
			BspPlane(&world->planes[node->plane], branch == 0U);
		if (!SG_BspProofAppendConstraint(&frame->region, &plane, &split))
			goto done;
		present = kind == BSP_WALK_EXPECTED ?
			SG_BspProofRegionHasProtocolVolume(proof, &split) :
			SG_BspProofRegionHasProtocolPoint(proof, &split);
		if (present < 0)
		{
			SG_BspProofFreeRegion(&split);
			goto done;
		}
		if (!present)
		{
			SG_BspProofFreeRegion(&split);
			continue;
		}
		if (!GrowWalkStack(proof, &stack))
		{
			SG_BspProofFreeRegion(&split);
			goto done;
		}
		stack.frames[stack.count].region = split;
		stack.frames[stack.count].child = node->children[branch];
		stack.frames[stack.count].branch = 0U;
		stack.frames[stack.count].owns_region = 1U;
		stack.count++;
	}
	result = 1;

done:
	FreeWalkStack(&stack);
	return result;
}

int SG_BspProofReplayCollisionBsp(sg_bsp_proof_context_t *proof,
	const sg_bsp_proof_region_t *region, int32_t child)
{
	return WalkBsp(proof, region, child, BSP_WALK_COLLISION);
}

int SG_BspProofReplayBsp(sg_bsp_proof_context_t *proof,
	const sg_bsp_proof_region_t *region, int32_t child)
{
	return WalkBsp(proof, region, child, BSP_WALK_EXPECTED);
}
