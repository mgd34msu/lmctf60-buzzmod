#include "sg_bsp_completeness_internal.h"
#include "sg_configuration_lattice.h"

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static sg_bsp_proof_halfspace_t DomainPlane(uint32_t axis, int minimum)
{
	sg_bsp_proof_halfspace_t plane;

	memset(&plane, 0, sizeof(plane));
	plane.normal[axis] = minimum ? -1.0f : 1.0f;
	plane.distance = minimum ? -SG_CONFIGURATION_PMOVE_ORIGIN_MIN :
		SG_CONFIGURATION_PMOVE_ORIGIN_MAX;
	return plane;
}

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

static sg_bsp_proof_halfspace_t Complement(
	sg_bsp_proof_halfspace_t source)
{
	uint32_t axis;

	for (axis = 0; axis < 3U; axis++)
		source.normal[axis] = -source.normal[axis];
	source.distance = -source.distance;
	source.open = (uint8_t)!source.open;
	return source;
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

static int SubtractConvex(sg_bsp_proof_context_t *proof,
	const sg_bsp_proof_region_t *source,
	const sg_bsp_proof_region_t *subtract,
	sg_bsp_proof_regions_t *outside, int exact_output);

static int BoundsOverlap(const sg_bsp_proof_region_t *left,
	const sg_bsp_proof_region_t *right)
{
	uint32_t axis;

	for (axis = 0; axis < 3U; axis++)
		if (left->lattice_maxs[axis] < right->lattice_mins[axis] ||
			right->lattice_maxs[axis] < left->lattice_mins[axis])
			return 0;
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

static int BucketBlocker(sg_bsp_proof_context_t *proof,
	const sg_bsp_proof_region_t *region, int32_t child)
{
	const sg_bsp_world_t *world = proof->authority->world;
	uint32_t branch;

	if (child < 0)
	{
		sg_bsp_proof_region_t blocker;
		uint32_t leaf = ~(uint32_t)child;
		uint32_t bucket;
		int bounded;

		if (leaf >= world->leaf_count ||
			!SG_BspProofCopyRegion(region, &blocker))
			return 0;
		blocker.leaf = leaf;
		bounded = SG_BspProofRegionBounds(proof, &blocker);
		if (bounded < 0)
		{
			SG_BspProofFreeRegion(&blocker);
			return 0;
		}
		if (!bounded)
		{
			SG_BspProofFreeRegion(&blocker);
			return 1;
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
	if ((uint32_t)child >= world->node_count ||
		world->nodes[(uint32_t)child].plane >= world->plane_count)
		return 0;
	for (branch = 0; branch < 2U; branch++)
	{
		sg_bsp_proof_halfspace_t plane = BspPlane(
			&world->planes[world->nodes[(uint32_t)child].plane], branch == 0U);
		sg_bsp_proof_region_t split;
		int present;

		if (!SG_BspProofAppendConstraint(region, &plane, &split))
			return 0;
		present = SG_BspProofRegionHasProtocolPoint(proof, &split);
		if (present < 0 || (present && !BucketBlocker(proof, &split,
				world->nodes[(uint32_t)child].children[branch])))
		{
			SG_BspProofFreeRegion(&split);
			return 0;
		}
		SG_BspProofFreeRegion(&split);
	}
	return 1;
}

static int ReplayCollisionBsp(sg_bsp_proof_context_t *proof,
	const sg_bsp_proof_region_t *region, int32_t child)
{
	const sg_bsp_world_t *world = proof->authority->world;
	const sg_rune_hull_profile_t *hull =
		region->stance == SG_RUNE_STANCE_STANDING ?
		&proof->authority->identity.standing_hull :
		&proof->authority->identity.crouching_hull;
	uint32_t branch;

	if (child < 0)
	{
		uint32_t leaf_index = ~(uint32_t)child;
		const sg_bsp_leaf_t *leaf;
		uint32_t offset;

		if (leaf_index >= world->leaf_count)
			return 0;
		proof->result.collision_leaf_visits++;
		leaf = &world->leaves[leaf_index];
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
			if (world->brushes[brush_index].first_side >
					world->brush_side_count ||
				world->brushes[brush_index].side_count >
					world->brush_side_count -
					world->brushes[brush_index].first_side ||
				!AppendAllBrushPlanes(proof, region, brush_index, &blocker))
				return 0;
			present = SG_BspProofRegionHasProtocolPoint(proof, &blocker);
			if (present < 0 || (present && !BucketBlocker(proof, &blocker,
					world->models[0].headnode)))
			{
				SG_BspProofFreeRegion(&blocker);
				return 0;
			}
			SG_BspProofFreeRegion(&blocker);
		}
		return 1;
	}
	if ((uint32_t)child >= world->node_count ||
		world->nodes[(uint32_t)child].plane >= world->plane_count)
		return 0;
	for (branch = 0; branch < 2U; branch++)
	{
		sg_bsp_proof_halfspace_t plane = CollisionBspPlane(
			&world->planes[world->nodes[(uint32_t)child].plane], hull, branch);
		sg_bsp_proof_region_t split;
		int present;

		if (!SG_BspProofAppendConstraint(region, &plane, &split))
			return 0;
		present = SG_BspProofRegionHasProtocolPoint(proof, &split);
		if (present < 0 || (present && !ReplayCollisionBsp(proof, &split,
				world->nodes[(uint32_t)child].children[branch])))
		{
			SG_BspProofFreeRegion(&split);
			return 0;
		}
		SG_BspProofFreeRegion(&split);
	}
	return 1;
}

static int AuditRemovedRegion(sg_bsp_proof_context_t *proof,
	const sg_bsp_proof_region_t *source,
	const sg_bsp_proof_region_t *blocker)
{
	sg_bsp_proof_region_t intersection;
	int32_t point[3];
	uint32_t constraint;
	int present;

	if (!SG_BspProofCopyRegion(source, &intersection))
		return 0;
	for (constraint = 0; constraint < blocker->halfspace_count; constraint++)
	{
		sg_bsp_proof_region_t next;

		if (!SG_BspProofAppendConstraint(&intersection,
				&blocker->halfspaces[constraint], &next))
		{
			SG_BspProofFreeRegion(&intersection);
			return 0;
		}
		SG_BspProofFreeRegion(&intersection);
		intersection = next;
	}
	present = SG_BspProofRegionPointWitness(proof, &intersection, point);
	SG_BspProofFreeRegion(&intersection);
	if (present > 0)
	{
		float origin[3];
		sg_host_collision_pose_t pose;

		for (constraint = 0; constraint < 3U; constraint++)
			origin[constraint] = (float)point[constraint] * 0.125f;
		proof->result.analytically_removed_pieces++;
		if (!SG_HostCollisionClassifyPose(proof->authority, NULL, origin,
				source->stance, &pose) || pose.valid)
		{
			SG_BspProofFail(proof, SG_BSP_COMPLETENESS_HOST_DISAGREEMENT,
				source->leaf);
			return 0;
		}
	}
	return present >= 0;
}

static int AppendExpectedLeaf(sg_bsp_proof_context_t *proof,
	const sg_bsp_proof_region_t *region)
{
	const sg_bsp_world_t *world = proof->authority->world;
	uint32_t bucket = (uint32_t)region->stance * world->leaf_count +
		region->leaf;
	sg_bsp_proof_regions_t fragments = { 0 };
	uint32_t blocker_index;
	sg_bsp_proof_region_t initial;

	if (!SG_BspProofCopyRegion(region, &initial) ||
		!SG_BspProofAppendOwnedRegion(proof, &fragments, &initial))
	{
		SG_BspProofFreeRegion(&initial);
		return 0;
	}
	for (blocker_index = 0;
		blocker_index < proof->blockers[bucket].count && fragments.count;
		blocker_index++)
	{
		const sg_bsp_proof_region_t *blocker =
			&proof->blockers[bucket].values[blocker_index];
		sg_bsp_proof_regions_t next = { 0 };
		uint32_t fragment;

		for (fragment = 0; fragment < fragments.count; fragment++)
		{
			sg_bsp_proof_region_t *candidate = &fragments.values[fragment];
			int bounded = SG_BspProofRegionBounds(proof, candidate);

			if (bounded < 0)
				goto failure;
			if (!bounded || !BoundsOverlap(candidate, blocker))
			{
				sg_bsp_proof_region_t copy;

				if (!SG_BspProofCopyRegion(candidate, &copy) ||
					!SG_BspProofAppendOwnedRegion(proof, &next, &copy))
				{
					SG_BspProofFreeRegion(&copy);
					goto failure;
				}
				continue;
			}
			proof->result.blocker_subtraction_candidates++;
			if (!AuditRemovedRegion(proof, candidate, blocker) ||
				!SubtractConvex(proof, candidate, blocker, &next, 0))
				goto failure;
		}
		SG_BspProofFreeRegions(&fragments);
		fragments = next;
	}
	for (blocker_index = 0; blocker_index < fragments.count; blocker_index++)
	{
		sg_bsp_proof_region_t expected = fragments.values[blocker_index];
		sg_host_collision_pose_t pose;
		int volume;

		memset(&fragments.values[blocker_index], 0,
			sizeof(fragments.values[blocker_index]));
		volume = SG_BspProofRegionWitness(proof, &expected, expected.witness);
		if (volume < 0)
		{
			SG_BspProofFreeRegion(&expected);
			goto failure;
		}
		if (!volume)
		{
			SG_BspProofFreeRegion(&expected);
			continue;
		}
		if (!SG_HostCollisionClassifyPose(proof->authority, NULL,
				expected.witness, expected.stance, &pose) || !pose.valid)
		{
			SG_BspProofFreeRegion(&expected);
			SG_BspProofFail(proof, SG_BSP_COMPLETENESS_HOST_DISAGREEMENT,
				expected.leaf);
			goto failure;
		}
		if (SG_BspProofRegionBounds(proof, &expected) <= 0 ||
			!SG_BspProofAppendOwnedRegion(proof, &proof->expected, &expected))
		{
			SG_BspProofFreeRegion(&expected);
			goto failure;
		}
	}
	SG_BspProofFreeRegions(&fragments);
	return 1;

failure:
	SG_BspProofFreeRegions(&fragments);
	return 0;
}

static int ReplayBsp(sg_bsp_proof_context_t *proof,
	const sg_bsp_proof_region_t *region, int32_t child)
{
	const sg_bsp_world_t *world = proof->authority->world;
	sg_bsp_proof_halfspace_t plane;
	sg_bsp_proof_region_t split;
	int volume;
	uint32_t branch;

	if (child < 0)
	{
		uint32_t leaf = ~(uint32_t)child;
		sg_bsp_proof_region_t terminal;
		int result;

		if (leaf >= world->leaf_count || !SG_BspProofCopyRegion(region,
				&terminal))
			return 0;
		terminal.leaf = leaf;
		result = AppendExpectedLeaf(proof, &terminal);
		SG_BspProofFreeRegion(&terminal);
		return result;
	}
	if ((uint32_t)child >= world->node_count ||
		world->nodes[(uint32_t)child].plane >= world->plane_count)
		return 0;
	for (branch = 0; branch < 2U; branch++)
	{
		plane = BspPlane(&world->planes[world->nodes[(uint32_t)child].plane],
			branch == 0U);
		if (!SG_BspProofAppendConstraint(region, &plane, &split))
			return 0;
		volume = SG_BspProofRegionHasProtocolVolume(proof, &split);
		if (volume < 0 || (volume && !ReplayBsp(proof, &split,
				world->nodes[(uint32_t)child].children[branch])))
		{
			SG_BspProofFreeRegion(&split);
			return 0;
		}
		SG_BspProofFreeRegion(&split);
	}
	return 1;
}

int SG_BspProofBuildExpected(sg_bsp_proof_context_t *proof)
{
	sg_bsp_proof_region_t domain;
	sg_rune_stance_t stance;
	uint32_t axis;

	if (proof->authority->world->leaf_count >
		UINT32_MAX / (uint32_t)SG_RUNE_STANCE_COUNT)
	{
		SG_BspProofFail(proof, SG_BSP_COMPLETENESS_OVERFLOW,
			proof->authority->world->leaf_count);
		return 0;
	}
	proof->blocker_bucket_count = proof->authority->world->leaf_count *
		(uint32_t)SG_RUNE_STANCE_COUNT;
	proof->blockers = calloc(proof->blocker_bucket_count ?
		proof->blocker_bucket_count : 1U, sizeof(*proof->blockers));
	if (!proof->blockers)
		return 0;
	for (stance = SG_RUNE_STANCE_STANDING;
		stance < SG_RUNE_STANCE_COUNT; stance++)
	{
		memset(&domain, 0, sizeof(domain));
		domain.leaf = SG_CONFIGURATION_INDEX_NONE;
		domain.stance = stance;
		for (axis = 0; axis < 3U; axis++)
		{
			sg_bsp_proof_halfspace_t maximum = DomainPlane(axis, 0);
			sg_bsp_proof_halfspace_t minimum = DomainPlane(axis, 1);
			sg_bsp_proof_region_t next;

			if (!SG_BspProofAppendConstraint(&domain, &maximum, &next))
				goto failure;
			SG_BspProofFreeRegion(&domain);
			domain = next;
			if (!SG_BspProofAppendConstraint(&domain, &minimum, &next))
				goto failure;
			SG_BspProofFreeRegion(&domain);
			domain = next;
		}
		if (!ReplayCollisionBsp(proof, &domain,
				proof->authority->world->models[0].headnode))
			goto failure;
		SG_BspProofFreeRegion(&domain);
	}
	for (stance = SG_RUNE_STANCE_STANDING;
		stance < SG_RUNE_STANCE_COUNT; stance++)
	{
		memset(&domain, 0, sizeof(domain));
		domain.leaf = SG_CONFIGURATION_INDEX_NONE;
		domain.stance = stance;
		for (axis = 0; axis < 3U; axis++)
		{
			sg_bsp_proof_halfspace_t maximum = DomainPlane(axis, 0);
			sg_bsp_proof_halfspace_t minimum = DomainPlane(axis, 1);
			sg_bsp_proof_region_t next;

			if (!SG_BspProofAppendConstraint(&domain, &maximum, &next))
				goto failure;
			SG_BspProofFreeRegion(&domain);
			domain = next;
			if (!SG_BspProofAppendConstraint(&domain, &minimum, &next))
				goto failure;
			SG_BspProofFreeRegion(&domain);
			domain = next;
		}
		if (!ReplayBsp(proof, &domain,
				proof->authority->world->models[0].headnode))
			goto failure;
		SG_BspProofFreeRegion(&domain);
	}
	proof->result.expected_cells = proof->expected.count;
	return 1;

failure:
	SG_BspProofFreeRegion(&domain);
	return 0;
}

static int AppendIfPresent(sg_bsp_proof_context_t *proof,
	sg_bsp_proof_regions_t *regions, sg_bsp_proof_region_t *region,
	int exact)
{
	int present = exact ? SG_BspProofRegionHasProtocolPoint(proof, region) :
		SG_BspProofRegionHasProtocolVolume(proof, region);

	if (present < 0)
		return 0;
	if (!present)
	{
		SG_BspProofFreeRegion(region);
		return 1;
	}
	return SG_BspProofAppendOwnedRegion(proof, regions, region);
}

static int SubtractConvex(sg_bsp_proof_context_t *proof,
	const sg_bsp_proof_region_t *source,
	const sg_bsp_proof_region_t *subtract,
	sg_bsp_proof_regions_t *outside, int exact_output)
{
	sg_bsp_proof_region_t inside;
	uint32_t constraint;
	int subset = 1;
	int present;

	if (!SG_BspProofCopyRegion(source, &inside))
		return 0;
	for (constraint = 0; constraint < subtract->halfspace_count; constraint++)
	{
		sg_bsp_proof_region_t next;

		if (!SG_BspProofAppendConstraint(&inside,
				&subtract->halfspaces[constraint], &next))
			goto failure;
		SG_BspProofFreeRegion(&inside);
		inside = next;
	}
	present = SG_BspProofRegionHasProtocolPoint(proof, &inside);
	SG_BspProofFreeRegion(&inside);
	if (present < 0)
		return 0;
	if (!present)
	{
		sg_bsp_proof_region_t copy;

		if (!SG_BspProofCopyRegion(source, &copy) ||
			!SG_BspProofAppendOwnedRegion(proof, outside, &copy))
		{
			SG_BspProofFreeRegion(&copy);
			return 0;
		}
		return 1;
	}
	for (constraint = 0; constraint < subtract->halfspace_count; constraint++)
	{
		sg_bsp_proof_halfspace_t complement =
			Complement(subtract->halfspaces[constraint]);
		sg_bsp_proof_region_t difference;

		if (!SG_BspProofAppendConstraint(source, &complement, &difference))
			return 0;
		present = SG_BspProofRegionHasProtocolPoint(proof, &difference);
		SG_BspProofFreeRegion(&difference);
		if (present < 0)
			return 0;
		if (present)
		{
			subset = 0;
			break;
		}
	}
	if (subset)
		return 1;
	if (!SG_BspProofCopyRegion(source, &inside))
		return 0;
	for (constraint = 0; constraint < subtract->halfspace_count; constraint++)
	{
		sg_bsp_proof_halfspace_t complement =
			Complement(subtract->halfspaces[constraint]);
		sg_bsp_proof_region_t fragment;
		sg_bsp_proof_region_t next;
		int inside_present;

		if (!SG_BspProofAppendConstraint(&inside, &complement, &fragment))
			goto failure;
		if (!AppendIfPresent(proof, outside, &fragment, exact_output))
		{
			SG_BspProofFreeRegion(&fragment);
			goto failure;
		}
		if (!SG_BspProofAppendConstraint(&inside,
				&subtract->halfspaces[constraint], &next))
			goto failure;
		SG_BspProofFreeRegion(&inside);
		inside = next;
		inside_present = SG_BspProofRegionHasProtocolPoint(proof, &inside);
		if (inside_present < 0)
			goto failure;
		if (!inside_present)
		{
			SG_BspProofFreeRegion(&inside);
			return 1;
		}
	}
	SG_BspProofFreeRegion(&inside);
	return 1;

failure:
	SG_BspProofFreeRegion(&inside);
	return 0;
}

static int RegionOutsideUnion(sg_bsp_proof_context_t *proof,
	const sg_bsp_proof_region_t *source,
	const sg_bsp_proof_region_t *subtractors, uint32_t subtractor_count,
	int exact)
{
	sg_bsp_proof_regions_t fragments = { 0 };
	uint32_t subtractor;
	sg_bsp_proof_region_t initial;

	if (!SG_BspProofCopyRegion(source, &initial) ||
		!SG_BspProofAppendOwnedRegion(proof, &fragments, &initial))
	{
		SG_BspProofFreeRegion(&initial);
		return -1;
	}
	for (subtractor = 0; subtractor < subtractor_count && fragments.count;
		subtractor++)
	{
		const sg_bsp_proof_region_t *candidate = &subtractors[subtractor];
		sg_bsp_proof_regions_t next = { 0 };
		uint32_t fragment;

		if (candidate->stance != source->stance ||
			candidate->leaf != source->leaf)
			continue;
		for (fragment = 0; fragment < fragments.count; fragment++)
			if (!SubtractConvex(proof, &fragments.values[fragment], candidate,
					&next, exact))
			{
				SG_BspProofFreeRegions(&next);
				SG_BspProofFreeRegions(&fragments);
				return -1;
			}
		SG_BspProofFreeRegions(&fragments);
		fragments = next;
	}
	subtractor = fragments.count;
	SG_BspProofFreeRegions(&fragments);
	return subtractor != 0U;
}

int SG_BspProofRegionOutsideUnion(sg_bsp_proof_context_t *proof,
	const sg_bsp_proof_region_t *source,
	const sg_bsp_proof_region_t *subtractors, uint32_t subtractor_count)
{
	return RegionOutsideUnion(proof, source, subtractors, subtractor_count, 0);
}

int SG_BspProofRegionOutsideUnionExact(sg_bsp_proof_context_t *proof,
	const sg_bsp_proof_region_t *source,
	const sg_bsp_proof_region_t *subtractors, uint32_t subtractor_count)
{
	return RegionOutsideUnion(proof, source, subtractors, subtractor_count, 1);
}

int SG_BspProofRegionsIntersect(sg_bsp_proof_context_t *proof,
	const sg_bsp_proof_region_t *left,
	const sg_bsp_proof_region_t *right)
{
	sg_bsp_proof_region_t intersection;
	uint32_t constraint;
	int volume;

	if (!SG_BspProofCopyRegion(left, &intersection))
		return -1;
	for (constraint = 0; constraint < right->halfspace_count; constraint++)
	{
		sg_bsp_proof_region_t next;

		if (!SG_BspProofAppendConstraint(&intersection,
				&right->halfspaces[constraint], &next))
		{
			SG_BspProofFreeRegion(&intersection);
			return -1;
		}
		SG_BspProofFreeRegion(&intersection);
		intersection = next;
	}
	volume = SG_BspProofRegionHasProtocolVolume(proof, &intersection);
	SG_BspProofFreeRegion(&intersection);
	return volume;
}
