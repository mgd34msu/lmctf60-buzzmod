#include "sg_bsp_completeness_internal.h"
#include "sg_configuration_lattice.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static int AllocationFits(uint32_t count, size_t element_size)
{
	size_t maximum;

	if (!element_size)
		return 1;
	maximum = SIZE_MAX / element_size;
	return maximum >= UINT32_MAX || count <= (uint32_t)maximum;
}

void SG_BspProofFail(sg_bsp_proof_context_t *proof,
	sg_bsp_completeness_code_t code, uint32_t record)
{
	if (proof->result.code == SG_BSP_COMPLETENESS_OK)
	{
		proof->result.code = code;
		proof->result.record = record;
	}
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

void SG_BspProofFreeRegion(sg_bsp_proof_region_t *region)
{
	if (!region)
		return;
	free(region->halfspaces);
	memset(region, 0, sizeof(*region));
}

void SG_BspProofFreeRegions(sg_bsp_proof_regions_t *regions)
{
	uint32_t index;

	if (!regions)
		return;
	for (index = 0; index < regions->count; index++)
		SG_BspProofFreeRegion(&regions->values[index]);
	free(regions->values);
	memset(regions, 0, sizeof(*regions));
}

int SG_BspProofCopyRegion(const sg_bsp_proof_region_t *source,
	sg_bsp_proof_region_t *destination)
{
	memset(destination, 0, sizeof(*destination));
	if (source->halfspace_count)
	{
		if (!AllocationFits(source->halfspace_count,
				sizeof(*destination->halfspaces)))
			return 0;
		destination->halfspaces = malloc((size_t)source->halfspace_count *
			sizeof(*destination->halfspaces));
		if (!destination->halfspaces)
			return 0;
		memcpy(destination->halfspaces, source->halfspaces,
			(size_t)source->halfspace_count * sizeof(*destination->halfspaces));
	}
	destination->halfspace_count = source->halfspace_count;
	destination->leaf = source->leaf;
	destination->stance = source->stance;
	memcpy(destination->witness, source->witness,
		sizeof(destination->witness));
	return 1;
}

int SG_BspProofAppendConstraint(const sg_bsp_proof_region_t *source,
	const sg_bsp_proof_halfspace_t *constraint,
	sg_bsp_proof_region_t *destination)
{
	size_t bytes;

	if (source->halfspace_count == UINT32_MAX)
		return 0;
	if (!AllocationFits(source->halfspace_count + 1U,
			sizeof(*destination->halfspaces)))
		return 0;
	bytes = (size_t)(source->halfspace_count + 1U) *
		sizeof(*destination->halfspaces);
	memset(destination, 0, sizeof(*destination));
	destination->halfspaces = malloc(bytes);
	if (!destination->halfspaces)
		return 0;
	if (source->halfspace_count)
		memcpy(destination->halfspaces, source->halfspaces,
			(size_t)source->halfspace_count * sizeof(*destination->halfspaces));
	destination->halfspaces[source->halfspace_count] = *constraint;
	destination->halfspace_count = source->halfspace_count + 1U;
	destination->leaf = source->leaf;
	destination->stance = source->stance;
	return 1;
}

static void MergeStats(sg_bsp_proof_context_t *proof,
	const sg_configuration_lattice_stats_t *stats)
{
	proof->result.lattice_solve_calls += stats->solve_calls;
	proof->result.lattice_constraints += stats->constraints;
	if (stats->maximum_binary_shift >
		proof->result.lattice_maximum_binary_shift)
		proof->result.lattice_maximum_binary_shift =
			stats->maximum_binary_shift;
}

int SG_BspProofRegionWitness(sg_bsp_proof_context_t *proof,
	const sg_bsp_proof_region_t *region, float witness[3])
{
	sg_configuration_lattice_halfspace_t *constraints;
	uint8_t *clearance;
	sg_configuration_lattice_stats_t stats = { 0 };
	int32_t point[3];
	int positive_margin = 0;
	uint32_t index;
	int solved;

	if (!AllocationFits(region->halfspace_count, sizeof(*constraints)) ||
		!AllocationFits(region->halfspace_count, sizeof(*clearance)))
	{
		SG_BspProofFail(proof, SG_BSP_COMPLETENESS_OVERFLOW, 0);
		return -1;
	}
	constraints = malloc((size_t)region->halfspace_count *
		sizeof(*constraints));
	clearance = malloc((size_t)region->halfspace_count * sizeof(*clearance));
	if ((!constraints || !clearance) && region->halfspace_count)
	{
		free(constraints);
		free(clearance);
		SG_BspProofFail(proof, SG_BSP_COMPLETENESS_OUT_OF_MEMORY, 0);
		return -1;
	}
	for (index = 0; index < region->halfspace_count; index++)
	{
		memcpy(constraints[index].normal, region->halfspaces[index].normal,
			sizeof(constraints[index].normal));
		constraints[index].distance = region->halfspaces[index].distance;
		constraints[index].open = region->halfspaces[index].open;
		clearance[index] = 1U;
	}
	solved = SG_ConfigurationLatticeFindMaxClearance(constraints, clearance,
		region->halfspace_count, NULL, point, &positive_margin, &stats);
	free(constraints);
	free(clearance);
	MergeStats(proof, &stats);
	if (solved < 0)
	{
		SG_BspProofFail(proof, SG_BSP_COMPLETENESS_HOST_DISAGREEMENT, 0);
		return -1;
	}
	if (!solved || !positive_margin)
		return 0;
	for (index = 0; index < 3U; index++)
		witness[index] = (float)point[index] * 0.125f;
	return 1;
}

int SG_BspProofRegionHasProtocolVolume(sg_bsp_proof_context_t *proof,
	const sg_bsp_proof_region_t *region)
{
	float witness[3];

	return SG_BspProofRegionWitness(proof, region, witness);
}

static int AppendOwnedRegion(sg_bsp_proof_context_t *proof,
	sg_bsp_proof_regions_t *regions, sg_bsp_proof_region_t *region)
{
	sg_bsp_proof_region_t *grown;
	uint32_t capacity;

	if (regions->count == UINT32_MAX)
	{
		SG_BspProofFail(proof, SG_BSP_COMPLETENESS_OVERFLOW, regions->count);
		return 0;
	}
	if (regions->count == regions->capacity)
	{
		capacity = regions->capacity ? regions->capacity * 2U : 64U;
		if (capacity < regions->capacity || capacity < regions->count + 1U)
			capacity = regions->count + 1U;
		if (!AllocationFits(capacity, sizeof(*grown)))
		{
			SG_BspProofFail(proof, SG_BSP_COMPLETENESS_OVERFLOW,
				regions->count);
			return 0;
		}
		grown = realloc(regions->values, (size_t)capacity * sizeof(*grown));
		if (!grown)
		{
			SG_BspProofFail(proof, SG_BSP_COMPLETENESS_OUT_OF_MEMORY,
				regions->count);
			return 0;
		}
		regions->values = grown;
		regions->capacity = capacity;
	}
	regions->values[regions->count++] = *region;
	memset(region, 0, sizeof(*region));
	return 1;
}

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

static int MarkWorldBrushes(sg_bsp_proof_context_t *proof)
{
	const sg_bsp_world_t *world = proof->authority->world;
	int32_t *stack;
	size_t stack_capacity;
	size_t count = 0;

	if (!world->model_count)
		return 0;
	if ((uint64_t)world->node_count + (uint64_t)world->leaf_count >
		(uint64_t)SIZE_MAX)
		return 0;
	stack_capacity = (size_t)world->node_count + (size_t)world->leaf_count;
	if (stack_capacity < 2U)
		stack_capacity = 2U;
	if (stack_capacity > SIZE_MAX / sizeof(*stack))
		return 0;
	stack = malloc(stack_capacity * sizeof(*stack));
	if (!stack)
		return 0;
	stack[count++] = world->models[0].headnode;
	while (count)
	{
		int32_t child = stack[--count];

		if (child >= 0)
		{
			const sg_bsp_node_t *node;

			if ((uint32_t)child >= world->node_count ||
				count > stack_capacity - 2U)
			{
				free(stack);
				return 0;
			}
			node = &world->nodes[(uint32_t)child];
			stack[count++] = node->children[0];
			stack[count++] = node->children[1];
		}
		else
		{
			uint32_t leaf_index = ~(uint32_t)child;
			const sg_bsp_leaf_t *leaf;
			uint32_t offset;

			if (leaf_index >= world->leaf_count)
			{
				free(stack);
				return 0;
			}
			leaf = &world->leaves[leaf_index];
			if (leaf->first_leaf_brush > world->leaf_brush_count ||
				leaf->leaf_brush_count > world->leaf_brush_count -
					leaf->first_leaf_brush)
			{
				free(stack);
				return 0;
			}
			for (offset = 0; offset < leaf->leaf_brush_count; offset++)
			{
				uint32_t brush = world->leaf_brushes[
					leaf->first_leaf_brush + offset];

				if (brush >= world->brush_count)
				{
					free(stack);
					return 0;
				}
				proof->world_brushes[brush] = 1U;
			}
		}
	}
	free(stack);
	return 1;
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

static int ReplayBrushes(sg_bsp_proof_context_t *proof,
	const sg_bsp_proof_region_t *region, uint32_t first_brush);

static int ReplayBrushSide(sg_bsp_proof_context_t *proof,
	const sg_bsp_proof_region_t *region, uint32_t brush, uint32_t side,
	uint32_t next_brush)
{
	const sg_bsp_brush_t *record = &proof->authority->world->brushes[brush];
	sg_bsp_proof_halfspace_t inside_plane;
	sg_bsp_proof_halfspace_t outside_plane;
	sg_bsp_proof_region_t outside;
	sg_bsp_proof_region_t inside;
	int volume;

	if (side == record->side_count)
		return 1;
	if (!BrushPlane(proof, brush, side, region->stance, &inside_plane))
		return 0;
	outside_plane = Complement(inside_plane);
	if (!SG_BspProofAppendConstraint(region, &outside_plane, &outside))
		return 0;
	volume = SG_BspProofRegionHasProtocolVolume(proof, &outside);
	if (volume < 0 || (volume && !ReplayBrushes(proof, &outside, next_brush)))
	{
		SG_BspProofFreeRegion(&outside);
		return 0;
	}
	SG_BspProofFreeRegion(&outside);
	if (!SG_BspProofAppendConstraint(region, &inside_plane, &inside))
		return 0;
	volume = SG_BspProofRegionHasProtocolVolume(proof, &inside);
	if (volume < 0 || (volume && !ReplayBrushSide(proof, &inside, brush,
			side + 1U, next_brush)))
	{
		SG_BspProofFreeRegion(&inside);
		return 0;
	}
	SG_BspProofFreeRegion(&inside);
	return 1;
}

static int ReplayBrushes(sg_bsp_proof_context_t *proof,
	const sg_bsp_proof_region_t *region, uint32_t first_brush)
{
	const sg_bsp_world_t *world = proof->authority->world;
	uint32_t brush;

	for (brush = first_brush; brush < world->brush_count; brush++)
		if (proof->world_brushes[brush] && BlockingBrush(&world->brushes[brush]))
		{
			sg_bsp_proof_region_t intersection;
			int volume;

			if (world->brushes[brush].first_side > world->brush_side_count ||
				world->brushes[brush].side_count > world->brush_side_count -
					world->brushes[brush].first_side ||
				!AppendAllBrushPlanes(proof, region, brush, &intersection))
				return 0;
			volume = SG_BspProofRegionHasProtocolVolume(proof, &intersection);
			SG_BspProofFreeRegion(&intersection);
			if (volume < 0)
				return 0;
			if (volume)
				return ReplayBrushSide(proof, region, brush, 0U, brush + 1U);
		}
	{
		sg_bsp_proof_region_t expected;
		sg_host_collision_pose_t pose;
		int volume;

		if (!SG_BspProofCopyRegion(region, &expected))
			return 0;
		volume = SG_BspProofRegionWitness(proof, &expected, expected.witness);
		if (volume < 0)
		{
			SG_BspProofFreeRegion(&expected);
			return 0;
		}
		if (!volume)
		{
			SG_BspProofFreeRegion(&expected);
			return 1;
		}
		if (!SG_HostCollisionClassifyPose(proof->authority, NULL,
				expected.witness, expected.stance, &pose) || !pose.valid)
		{
			SG_BspProofFreeRegion(&expected);
			SG_BspProofFail(proof, SG_BSP_COMPLETENESS_HOST_DISAGREEMENT,
				expected.leaf);
			return 0;
		}
		if (!AppendOwnedRegion(proof, &proof->expected, &expected))
		{
			SG_BspProofFreeRegion(&expected);
			return 0;
		}
	}
	return 1;
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
		result = ReplayBrushes(proof, &terminal, 0U);
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

	proof->world_brushes = calloc(proof->authority->world->brush_count ?
		proof->authority->world->brush_count : 1U,
		sizeof(*proof->world_brushes));
	if (!proof->world_brushes || !MarkWorldBrushes(proof))
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

static int AppendIfVolume(sg_bsp_proof_context_t *proof,
	sg_bsp_proof_regions_t *regions, sg_bsp_proof_region_t *region)
{
	int volume = SG_BspProofRegionHasProtocolVolume(proof, region);

	if (volume < 0)
		return 0;
	if (!volume)
	{
		SG_BspProofFreeRegion(region);
		return 1;
	}
	return AppendOwnedRegion(proof, regions, region);
}

static int SubtractConvex(sg_bsp_proof_context_t *proof,
	const sg_bsp_proof_region_t *source,
	const sg_bsp_proof_region_t *subtract,
	sg_bsp_proof_regions_t *outside)
{
	sg_bsp_proof_region_t inside;
	uint32_t constraint;
	int subset = 1;
	int volume;

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
	volume = SG_BspProofRegionHasProtocolVolume(proof, &inside);
	SG_BspProofFreeRegion(&inside);
	if (volume < 0)
		return 0;
	if (!volume)
	{
		sg_bsp_proof_region_t copy;

		if (!SG_BspProofCopyRegion(source, &copy) ||
			!AppendOwnedRegion(proof, outside, &copy))
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
		volume = SG_BspProofRegionHasProtocolVolume(proof, &difference);
		SG_BspProofFreeRegion(&difference);
		if (volume < 0)
			return 0;
		if (volume)
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
		int fragment_volume;

		if (!SG_BspProofAppendConstraint(&inside, &complement, &fragment))
			goto failure;
		if (!AppendIfVolume(proof, outside, &fragment))
		{
			SG_BspProofFreeRegion(&fragment);
			goto failure;
		}
		if (!SG_BspProofAppendConstraint(&inside,
				&subtract->halfspaces[constraint], &next))
			goto failure;
		SG_BspProofFreeRegion(&inside);
		inside = next;
		fragment_volume = SG_BspProofRegionHasProtocolVolume(proof, &inside);
		if (fragment_volume < 0)
			goto failure;
		if (!fragment_volume)
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

int SG_BspProofRegionOutsideUnion(sg_bsp_proof_context_t *proof,
	const sg_bsp_proof_region_t *source,
	const sg_bsp_proof_region_t *subtractors, uint32_t subtractor_count)
{
	sg_bsp_proof_regions_t fragments = { 0 };
	uint32_t subtractor;
	sg_bsp_proof_region_t initial;

	if (!SG_BspProofCopyRegion(source, &initial) ||
		!AppendOwnedRegion(proof, &fragments, &initial))
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
					&next))
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
