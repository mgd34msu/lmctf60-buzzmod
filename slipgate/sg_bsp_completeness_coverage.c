#include "sg_bsp_completeness_internal.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct region_ref_s
{
	uint32_t index;
	uint32_t key;
	int32_t mins[3];
	int32_t maxs[3];
	int32_t subtree_max_x;
} region_ref_t;

static uint32_t RegionKey(const sg_bsp_proof_region_t *region,
	uint32_t leaf_count)
{
	return (uint32_t)region->stance * leaf_count + region->leaf;
}

static int CompareRegionRef(const void *left_pointer,
	const void *right_pointer)
{
	const region_ref_t *left = left_pointer;
	const region_ref_t *right = right_pointer;
	uint32_t axis;

	if (left->key != right->key)
		return left->key < right->key ? -1 : 1;
	for (axis = 0; axis < 3U; axis++)
		if (left->mins[axis] != right->mins[axis])
			return left->mins[axis] < right->mins[axis] ? -1 : 1;
	if (left->index == right->index)
		return 0;
	return left->index < right->index ? -1 : 1;
}

static int32_t BuildIntervalMax(region_ref_t *refs, uint32_t first,
	uint32_t count)
{
	uint32_t left_count;
	uint32_t middle;
	int32_t maximum;

	if (!count)
		return INT32_MIN;
	left_count = count / 2U;
	middle = first + left_count;
	maximum = refs[middle].maxs[0];
	if (left_count)
	{
		int32_t left_max = BuildIntervalMax(refs, first, left_count);

		if (left_max > maximum)
			maximum = left_max;
	}
	if (count - left_count - 1U)
	{
		uint32_t right_first = middle + 1U;
		uint32_t right_count = count - left_count - 1U;
		int32_t right_max = BuildIntervalMax(refs, right_first, right_count);

		if (right_max > maximum)
			maximum = right_max;
	}
	refs[middle].subtree_max_x = maximum;
	return maximum;
}

static region_ref_t *BuildRegionRefs(const sg_bsp_proof_region_t *regions,
	uint32_t count, uint32_t leaf_count)
{
	region_ref_t *refs;
	uint32_t index;

	refs = calloc(count ? count : 1U, sizeof(*refs));
	if (!refs)
		return NULL;
	for (index = 0; index < count; index++)
	{
		refs[index].index = index;
		refs[index].key = RegionKey(&regions[index], leaf_count);
		memcpy(refs[index].mins, regions[index].lattice_mins,
			sizeof(refs[index].mins));
		memcpy(refs[index].maxs, regions[index].lattice_maxs,
			sizeof(refs[index].maxs));
	}
	qsort(refs, count, sizeof(*refs), CompareRegionRef);
	for (index = 0; index < count; )
	{
		uint32_t end = index + 1U;

		while (end < count && refs[end].key == refs[index].key)
			end++;
		(void)BuildIntervalMax(refs, index, end - index);
		index = end;
	}
	return refs;
}

static uint32_t LowerBoundKey(const region_ref_t *refs, uint32_t count,
	uint32_t key)
{
	uint32_t first = 0U;
	uint32_t length = count;

	while (length)
	{
		uint32_t half = length / 2U;
		uint32_t middle = first + half;

		if (refs[middle].key < key)
		{
			first = middle + 1U;
			length -= half + 1U;
		}
		else
			length = half;
	}
	return first;
}

static int RefBoundsOverlap(const region_ref_t *left,
	const sg_bsp_proof_region_t *right)
{
	uint32_t axis;

	for (axis = 0; axis < 3U; axis++)
		if (left->maxs[axis] < right->lattice_mins[axis] ||
			right->lattice_maxs[axis] < left->mins[axis])
			return 0;
	return 1;
}

static void QueryCandidateRegions(sg_bsp_proof_context_t *proof,
	const sg_bsp_proof_region_t *source, const region_ref_t *refs,
	uint32_t first, uint32_t count, const sg_bsp_proof_region_t *regions,
	sg_bsp_proof_region_t *candidates, uint32_t *candidate_count)
{
	uint32_t left_count;
	uint32_t middle;

	if (!count)
		return;
	left_count = count / 2U;
	middle = first + left_count;
	if (left_count && refs[first + left_count / 2U].subtree_max_x >=
			source->lattice_mins[0])
		QueryCandidateRegions(proof, source, refs, first, left_count, regions,
			candidates, candidate_count);
	proof->result.coverage_region_examined++;
	if (refs[middle].mins[0] <= source->lattice_maxs[0] &&
		RefBoundsOverlap(&refs[middle], source))
	{
		candidates[*candidate_count] = regions[refs[middle].index];
		(*candidate_count)++;
		proof->result.coverage_region_candidates++;
	}
	if (count - left_count - 1U && refs[middle + 1U].mins[0] <=
			source->lattice_maxs[0])
		QueryCandidateRegions(proof, source, refs, middle + 1U,
			count - left_count - 1U, regions, candidates, candidate_count);
}

static uint32_t CandidateRegions(sg_bsp_proof_context_t *proof,
	const sg_bsp_proof_region_t *source, const region_ref_t *refs,
	uint32_t ref_count, const sg_bsp_proof_region_t *regions,
	sg_bsp_proof_region_t *candidates)
{
	uint32_t key = RegionKey(source, proof->authority->world->leaf_count);
	uint32_t first = LowerBoundKey(refs, ref_count, key);
	uint32_t end = LowerBoundKey(refs, ref_count, key + 1U);
	uint32_t candidate_count = 0U;

	QueryCandidateRegions(proof, source, refs, first, end - first, regions,
		candidates, &candidate_count);
	return candidate_count;
}

int SG_BspProofCellRegion(const sg_configuration_space_t *space,
	uint32_t cell_index, sg_bsp_proof_region_t *region_out)
{
	const sg_configuration_cell_t *cell;
	uint32_t offset;

	memset(region_out, 0, sizeof(*region_out));
	if (cell_index >= space->cell_count)
		return 0;
	cell = &space->cells[cell_index];
	if (cell->stance >= SG_RUNE_STANCE_COUNT ||
		cell->first_face > space->face_count ||
		cell->face_count > space->face_count - cell->first_face ||
		!cell->face_count || !SG_BspProofFiniteVector(cell->interior_witness.value))
		return 0;
	region_out->halfspaces = calloc(cell->face_count,
		sizeof(*region_out->halfspaces));
	if (!region_out->halfspaces)
		return 0;
	region_out->halfspace_count = cell->face_count;
	region_out->leaf = cell->bsp_leaf.index;
	region_out->stance = cell->stance;
	memcpy(region_out->witness, cell->interior_witness.value,
		sizeof(region_out->witness));
	for (offset = 0; offset < cell->face_count; offset++)
	{
		const sg_configuration_face_t *face =
			&space->faces[cell->first_face + offset];
		sg_bsp_proof_halfspace_t *halfspace = &region_out->halfspaces[offset];

		if (!SG_BspProofFiniteVector(face->plane.normal) ||
			!isfinite(face->plane.distance) ||
			(face->plane.normal[0] == 0.0f &&
			 face->plane.normal[1] == 0.0f &&
			 face->plane.normal[2] == 0.0f) ||
			face->first_vertex > space->vertex_count ||
			face->vertex_count < 3U ||
			face->vertex_count > space->vertex_count - face->first_vertex)
		{
			SG_BspProofFreeRegion(region_out);
			return 0;
		}
		memcpy(halfspace->normal, face->plane.normal,
			sizeof(halfspace->normal));
		halfspace->distance = face->plane.distance;
		halfspace->open = (uint8_t)(
			(face->plane.source_kind == SG_CONFIGURATION_PLANE_BSP &&
			 face->plane.reversed == 0U) ||
			(face->plane.source_kind == SG_CONFIGURATION_PLANE_EXPANDED_BRUSH &&
			 face->plane.reversed != 0U));
	}
	return 1;
}

static int PointInRegion(const sg_bsp_proof_region_t *region,
	const float point[3])
{
	uint32_t constraint;

	for (constraint = 0; constraint < region->halfspace_count; constraint++)
	{
		const sg_bsp_proof_halfspace_t *halfspace =
			&region->halfspaces[constraint];
		sg_configuration_plane_t plane = { 0 };
		double normal[3], plane_distance;
		double distance;

		memcpy(plane.normal, halfspace->normal, sizeof(plane.normal));
		plane.distance = halfspace->distance;
		if (!SG_BspProofOrientedPlane(&plane, normal, &plane_distance))
			return 0;
		distance = (double)point[0] * normal[0] +
			(double)point[1] * normal[1] +
			(double)point[2] * normal[2] - plane_distance;

		if ((halfspace->open && distance >= 0.0) ||
			(!halfspace->open && distance > 0.0001))
			return 0;
	}
	return 1;
}

static int PointLeaf(const sg_bsp_world_t *world, const float point[3],
	uint32_t *leaf_out)
{
	int32_t child = world->models[0].headnode;

	while (child >= 0)
	{
		const sg_bsp_node_t *node;
		const sg_bsp_plane_t *plane;

		if ((uint32_t)child >= world->node_count)
			return 0;
		node = &world->nodes[(uint32_t)child];
		if (node->plane >= world->plane_count)
			return 0;
		plane = &world->planes[node->plane];
		child = node->children[SG_BspProofDot(point, plane->normal.value) -
			plane->distance < 0.0f];
	}
	*leaf_out = ~(uint32_t)child;
	return *leaf_out < world->leaf_count;
}

int SG_BspProofAuditCoverage(sg_bsp_proof_context_t *proof)
{
	sg_bsp_proof_region_t *actual;
	sg_bsp_proof_region_t *candidates = NULL;
	region_ref_t *actual_refs = NULL;
	region_ref_t *expected_refs = NULL;
	uint32_t cell;
	uint32_t expected;
	int outside;

	actual = calloc(proof->space->cell_count ? proof->space->cell_count : 1U,
		sizeof(*actual));
	if (!actual)
		return 0;
	for (cell = 0; cell < proof->space->cell_count; cell++)
	{
		sg_host_collision_pose_t pose;
		uint32_t leaf;
		sg_rune_contents_mask_t contents;
		int volume;

		if (!SG_BspProofCellRegion(proof->space, cell, &actual[cell]) ||
			actual[cell].leaf >= proof->authority->world->leaf_count)
		{
			SG_BspProofFail(proof, SG_BSP_COMPLETENESS_INVALID_CELL, cell);
			goto failure;
		}
		volume = SG_BspProofRegionHasProtocolVolume(proof, &actual[cell]);
		if (volume <= 0 || SG_BspProofRegionBounds(proof, &actual[cell]) <= 0 ||
			!PointInRegion(&actual[cell], actual[cell].witness) ||
			!PointLeaf(proof->authority->world, actual[cell].witness, &leaf) ||
			leaf != actual[cell].leaf ||
			!SG_HostCollisionClassifyPose(proof->authority, NULL,
				actual[cell].witness, actual[cell].stance, &pose) || !pose.valid)
		{
			SG_BspProofFail(proof, SG_BSP_COMPLETENESS_INVALID_CELL, cell);
			goto failure;
		}
		contents = SG_HostCollisionRuneContents(SG_HostCollisionPointContents(
			proof->authority, NULL, actual[cell].witness));
		if (contents != proof->space->cells[cell].contents ||
			proof->authority->world->leaves[leaf].area !=
				proof->space->cells[cell].bsp_area.index ||
			(proof->authority->world->leaves[leaf].cluster < 0 ? UINT32_MAX :
				(uint32_t)proof->authority->world->leaves[leaf].cluster) !=
				proof->space->cells[cell].bsp_cluster.index)
		{
			SG_BspProofFail(proof, SG_BSP_COMPLETENESS_INVALID_CELL, cell);
			goto failure;
		}
	}
	actual_refs = BuildRegionRefs(actual, proof->space->cell_count,
		proof->authority->world->leaf_count);
	expected_refs = BuildRegionRefs(proof->expected.values,
		proof->expected.count, proof->authority->world->leaf_count);
	candidates = calloc(proof->space->cell_count > proof->expected.count ?
		proof->space->cell_count : proof->expected.count, sizeof(*candidates));
	if (!actual_refs || !expected_refs ||
		(!candidates && (proof->space->cell_count || proof->expected.count)))
		goto failure;
	for (cell = 0; cell < proof->space->cell_count; )
	{
		uint32_t end = cell + 1U;

		while (end < proof->space->cell_count &&
			actual_refs[end].key == actual_refs[cell].key)
			end++;
		for (; cell < end; cell++)
			for (expected = cell + 1U; expected < end &&
				actual_refs[expected].mins[0] <= actual_refs[cell].maxs[0];
				expected++)
			{
				const region_ref_t *left = &actual_refs[cell];
				const region_ref_t *right = &actual_refs[expected];

				if (left->maxs[1] < right->mins[1] ||
					right->maxs[1] < left->mins[1] ||
					left->maxs[2] < right->mins[2] ||
					right->maxs[2] < left->mins[2])
					continue;
				proof->result.cell_overlap_candidates++;
				outside = SG_BspProofRegionsIntersect(proof,
					&actual[left->index], &actual[right->index]);
				if (outside < 0)
					goto failure;
				if (outside)
				{
					proof->result.invented_cells++;
					SG_BspProofFail(proof,
						SG_BSP_COMPLETENESS_INVENTED_CELL, right->index);
					goto failure;
				}
			}
	}
	for (expected = 0; expected < proof->expected.count; expected++)
	{
		uint32_t candidate_count = CandidateRegions(proof,
			&proof->expected.values[expected], actual_refs,
			proof->space->cell_count, actual, candidates);

		outside = SG_BspProofRegionOutsideUnion(proof,
			&proof->expected.values[expected], candidates, candidate_count);
		if (outside < 0)
			goto failure;
		if (outside)
		{
			proof->result.omitted_cells++;
			SG_BspProofFail(proof, SG_BSP_COMPLETENESS_OMITTED_CELL, expected);
			goto failure;
		}
	}
	for (cell = 0; cell < proof->space->cell_count; cell++)
	{
		uint32_t candidate_count = CandidateRegions(proof, &actual[cell],
			expected_refs, proof->expected.count, proof->expected.values,
			candidates);

		outside = SG_BspProofRegionOutsideUnion(proof, &actual[cell],
			candidates, candidate_count);
		if (outside < 0)
			goto failure;
		if (outside)
		{
			proof->result.invented_cells++;
			SG_BspProofFail(proof, SG_BSP_COMPLETENESS_INVENTED_CELL, cell);
			goto failure;
		}
	}
	for (expected = 0; expected < proof->expected.count; expected++)
	{
		uint32_t candidate_count = CandidateRegions(proof,
			&proof->expected.values[expected], actual_refs,
			proof->space->cell_count, actual, candidates);

		outside = SG_BspProofRegionOutsideUnionExact(proof,
			&proof->expected.values[expected], candidates, candidate_count);
		if (outside < 0)
			goto failure;
		if (outside)
		{
			proof->result.omitted_cells++;
			SG_BspProofFail(proof, SG_BSP_COMPLETENESS_OMITTED_CELL, expected);
			goto failure;
		}
	}
	for (cell = 0; cell < proof->space->cell_count; cell++)
	{
		uint32_t candidate_count = CandidateRegions(proof, &actual[cell],
			expected_refs, proof->expected.count, proof->expected.values,
			candidates);

		outside = SG_BspProofRegionOutsideUnionExact(proof, &actual[cell],
			candidates, candidate_count);
		if (outside < 0)
			goto failure;
		if (outside)
		{
			proof->result.invented_cells++;
			SG_BspProofFail(proof, SG_BSP_COMPLETENESS_INVENTED_CELL, cell);
			goto failure;
		}
	}
	proof->result.proved_cells = proof->space->cell_count;
	for (cell = 0; cell < proof->space->cell_count; cell++)
		SG_BspProofFreeRegion(&actual[cell]);
	free(candidates);
	free(actual_refs);
	free(expected_refs);
	free(actual);
	return 1;

failure:
	for (cell = 0; cell < proof->space->cell_count; cell++)
		SG_BspProofFreeRegion(&actual[cell]);
	free(candidates);
	free(actual_refs);
	free(expected_refs);
	free(actual);
	return 0;
}
