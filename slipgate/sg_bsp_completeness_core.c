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

int SG_BspProofCanonicalPlane(const sg_configuration_plane_t *plane,
	sg_bsp_proof_canonical_plane_t *canonical_out)
{
	float scale;
	uint32_t axis;
	int flip = 0;

	if (!plane || !canonical_out ||
		!SG_BspProofFiniteVector(plane->normal) ||
		!isfinite(plane->distance))
		return 0;
	scale = fmaxf(fabsf(plane->normal[0]),
		fmaxf(fabsf(plane->normal[1]), fabsf(plane->normal[2])));
	if (scale == 0.0f || !isfinite(scale))
		return 0;
	for (axis = 0; axis < 3U; axis++)
		if (plane->normal[axis] != 0.0f)
		{
			flip = plane->normal[axis] < 0.0f;
			break;
		}
	for (axis = 0; axis < 3U; axis++)
	{
		canonical_out->normal[axis] =
			(double)(flip ? -plane->normal[axis] : plane->normal[axis]) /
			(double)scale;
		if (canonical_out->normal[axis] == 0.0)
			canonical_out->normal[axis] = 0.0;
	}
	canonical_out->distance =
		(double)(flip ? -plane->distance : plane->distance) / (double)scale;
	if (canonical_out->distance == 0.0)
		canonical_out->distance = 0.0;
	canonical_out->orientation = (uint8_t)flip;
	return 1;
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
	memcpy(destination->lattice_mins, source->lattice_mins,
		sizeof(destination->lattice_mins));
	memcpy(destination->lattice_maxs, source->lattice_maxs,
		sizeof(destination->lattice_maxs));
	destination->has_lattice_bounds = source->has_lattice_bounds;
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

int SG_BspProofAppendOwnedRegion(sg_bsp_proof_context_t *proof,
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
