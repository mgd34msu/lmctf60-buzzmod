#include "sg_bsp_completeness_internal.h"
#include "sg_configuration_lattice.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

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

static sg_configuration_lattice_halfspace_t *LatticeConstraints(
	sg_bsp_proof_context_t *proof, const sg_bsp_proof_region_t *region)
{
	sg_configuration_lattice_halfspace_t *constraints;
	uint32_t index;

	constraints = calloc(region->halfspace_count, sizeof(*constraints));
	if (!constraints && region->halfspace_count)
	{
		SG_BspProofFail(proof, SG_BSP_COMPLETENESS_OUT_OF_MEMORY, 0);
		return NULL;
	}
	for (index = 0; index < region->halfspace_count; index++)
	{
		memcpy(constraints[index].normal, region->halfspaces[index].normal,
			sizeof(constraints[index].normal));
		constraints[index].distance = region->halfspaces[index].distance;
		constraints[index].open = region->halfspaces[index].open;
	}
	return constraints;
}

int SG_BspProofRegionPointWitness(sg_bsp_proof_context_t *proof,
	const sg_bsp_proof_region_t *region, int32_t witness[3])
{
	sg_configuration_lattice_halfspace_t *constraints =
		LatticeConstraints(proof, region);
	sg_configuration_lattice_stats_t stats = { 0 };
	int solved;

	if (!constraints && region->halfspace_count)
		return -1;
	solved = SG_ConfigurationLatticeFind(constraints, region->halfspace_count,
		NULL, witness, &stats);
	free(constraints);
	MergeStats(proof, &stats);
	if (solved < 0)
		SG_BspProofFail(proof, SG_BSP_COMPLETENESS_HOST_DISAGREEMENT, 0);
	return solved;
}

int SG_BspProofRegionHasProtocolPoint(sg_bsp_proof_context_t *proof,
	const sg_bsp_proof_region_t *region)
{
	int32_t witness[3];

	return SG_BspProofRegionPointWitness(proof, region, witness);
}

int SG_BspProofRegionBounds(sg_bsp_proof_context_t *proof,
	sg_bsp_proof_region_t *region)
{
	sg_configuration_lattice_halfspace_t *constraints;
	uint32_t axis;

	if (region->has_lattice_bounds)
		return 1;
	constraints = LatticeConstraints(proof, region);
	if (!constraints && region->halfspace_count)
		return -1;
	for (axis = 0; axis < 3U; axis++)
	{
		sg_configuration_lattice_stats_t stats = { 0 };
		int solved;

		solved = SG_ConfigurationLatticeCoordinateBounds(constraints,
			region->halfspace_count, axis, &region->lattice_mins[axis],
			&region->lattice_maxs[axis], &stats);
		MergeStats(proof, &stats);
		if (solved <= 0)
		{
			free(constraints);
			if (solved < 0)
				SG_BspProofFail(proof,
					SG_BSP_COMPLETENESS_HOST_DISAGREEMENT, 0);
			return solved;
		}
	}
	free(constraints);
	region->has_lattice_bounds = 1U;
	return 1;
}
