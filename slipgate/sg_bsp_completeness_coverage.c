#include "sg_bsp_completeness_internal.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

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
			face->plane.source_kind == SG_CONFIGURATION_PLANE_EXPANDED_BRUSH &&
			face->plane.reversed != 0U);
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
		float distance = SG_BspProofDot(point, halfspace->normal) -
			halfspace->distance;

		if ((halfspace->open && distance >= 0.0f) ||
			(!halfspace->open && distance > 0.0001f))
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
		if (volume <= 0 ||
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
	for (cell = 0; cell < proof->space->cell_count; cell++)
		for (expected = cell + 1U; expected < proof->space->cell_count;
			expected++)
			if (actual[cell].stance == actual[expected].stance &&
				actual[cell].leaf == actual[expected].leaf)
			{
				outside = SG_BspProofRegionsIntersect(proof, &actual[cell],
					&actual[expected]);
				if (outside < 0)
					goto failure;
				if (outside)
				{
					proof->result.invented_cells++;
					SG_BspProofFail(proof,
						SG_BSP_COMPLETENESS_INVENTED_CELL, expected);
					goto failure;
				}
			}
	for (expected = 0; expected < proof->expected.count; expected++)
	{
		outside = SG_BspProofRegionOutsideUnion(proof,
			&proof->expected.values[expected], actual,
			proof->space->cell_count);
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
		outside = SG_BspProofRegionOutsideUnion(proof, &actual[cell],
			proof->expected.values, proof->expected.count);
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
	free(actual);
	return 1;

failure:
	for (cell = 0; cell < proof->space->cell_count; cell++)
		SG_BspProofFreeRegion(&actual[cell]);
	free(actual);
	return 0;
}
