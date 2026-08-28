#include "sg_bsp_completeness_internal.h"
#include "sg_configuration_lattice.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static int ExtremeWitness(sg_bsp_proof_context_t *proof,
	const sg_bsp_proof_region_t *region, const float objective[3],
	float witness[3])
{
	sg_configuration_lattice_halfspace_t *constraints;
	sg_configuration_lattice_stats_t stats = { 0 };
	int32_t point[3];
	uint32_t index;
	int solved;

	constraints = malloc((size_t)region->halfspace_count *
		sizeof(*constraints));
	if (!constraints && region->halfspace_count)
		return -1;
	for (index = 0; index < region->halfspace_count; index++)
	{
		memcpy(constraints[index].normal, region->halfspaces[index].normal,
			sizeof(constraints[index].normal));
		constraints[index].distance = region->halfspaces[index].distance;
		constraints[index].open = region->halfspaces[index].open;
	}
	solved = SG_ConfigurationLatticeFind(constraints, region->halfspace_count,
		objective, point, &stats);
	free(constraints);
	proof->result.lattice_solve_calls += stats.solve_calls;
	proof->result.lattice_constraints += stats.constraints;
	if (stats.maximum_binary_shift >
		proof->result.lattice_maximum_binary_shift)
		proof->result.lattice_maximum_binary_shift = stats.maximum_binary_shift;
	if (solved <= 0)
		return solved;
	for (index = 0; index < 3U; index++)
		witness[index] = (float)point[index] * 0.125f;
	return 1;
}

static int AuditRegionStates(sg_bsp_proof_context_t *proof,
	const sg_bsp_proof_region_t *region)
{
	static const float objectives[6][3] = {
		{ 1.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f },
		{ 0.0f, 1.0f, 0.0f }, { 0.0f, -1.0f, 0.0f },
		{ 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f }
	};
	int saw_supported = 0;
	int saw_airborne = 0;
	int saw_water = 0;
	uint32_t sample;

	for (sample = 0; sample < 7U; sample++)
	{
		float witness[3];
		sg_host_collision_pose_t pose;
		int found;

		if (sample == 0U)
		{
			memcpy(witness, region->witness, sizeof(witness));
			found = 1;
		}
		else
			found = ExtremeWitness(proof, region, objectives[sample - 1U],
				witness);
		if (found < 0)
			return 0;
		if (!found)
			continue;
		if (!SG_HostCollisionClassifyPose(proof->authority, NULL, witness,
				region->stance, &pose) || !pose.valid)
		{
			SG_BspProofFail(proof, SG_BSP_COMPLETENESS_HOST_DISAGREEMENT,
				region->leaf);
			return 0;
		}
		saw_supported |= pose.supported;
		saw_airborne |= !pose.supported;
		saw_water |= pose.water_level != 0U;
	}
	proof->result.supported_witnesses += (uint32_t)saw_supported;
	proof->result.airborne_witnesses += (uint32_t)saw_airborne;
	proof->result.water_witnesses += (uint32_t)saw_water;
	return 1;
}

int SG_BspProofAuditStates(sg_bsp_proof_context_t *proof)
{
	uint32_t region;

	for (region = 0; region < proof->expected.count; region++)
	{
		const sg_bsp_proof_region_t *expected = &proof->expected.values[region];

		if (expected->stance == SG_RUNE_STANCE_STANDING)
			proof->result.standing_regions++;
		else
			proof->result.crouching_regions++;
		if (proof->authority->world->leaves[expected->leaf].cluster < 0)
			proof->result.void_witnesses++;
		if (!AuditRegionStates(proof, expected))
			return 0;
	}
	return 1;
}
