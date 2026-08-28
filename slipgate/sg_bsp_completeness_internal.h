#ifndef SG_BSP_COMPLETENESS_INTERNAL_H
#define SG_BSP_COMPLETENESS_INTERNAL_H

#include "sg_bsp_completeness_proof.h"

typedef struct sg_bsp_proof_halfspace_s
{
	float normal[3];
	float distance;
	uint8_t open;
	uint8_t reserved[3];
} sg_bsp_proof_halfspace_t;

typedef struct sg_bsp_proof_region_s
{
	sg_bsp_proof_halfspace_t *halfspaces;
	uint32_t halfspace_count;
	uint32_t leaf;
	sg_rune_stance_t stance;
	float witness[3];
	int32_t lattice_mins[3];
	int32_t lattice_maxs[3];
	uint8_t has_lattice_bounds;
} sg_bsp_proof_region_t;

typedef struct sg_bsp_proof_regions_s
{
	sg_bsp_proof_region_t *values;
	uint32_t count;
	uint32_t capacity;
} sg_bsp_proof_regions_t;

typedef struct sg_bsp_proof_context_s
{
	const sg_host_collision_authority_t *authority;
	const sg_configuration_space_t *space;
	sg_bsp_completeness_result_t result;
	sg_bsp_proof_regions_t expected;
	sg_bsp_proof_regions_t *blockers;
	uint32_t blocker_bucket_count;
} sg_bsp_proof_context_t;

typedef struct sg_bsp_proof_face_ref_s
{
	uint32_t cell;
	uint32_t face;
	uint32_t stance;
	uint32_t dominant;
	int64_t plane_bucket;
	uint8_t orientation;
	uint8_t reserved[3];
	float sweep_min;
	float sweep_max;
	float other_min;
	float other_max;
	sg_rune_vec3_t *vertices;
	uint32_t vertex_count;
} sg_bsp_proof_face_ref_t;

typedef struct sg_bsp_proof_portal_ref_s
{
	uint32_t low_cell;
	uint32_t high_cell;
	uint32_t stance;
	uint32_t portal;
} sg_bsp_proof_portal_ref_t;

void SG_BspProofFail(sg_bsp_proof_context_t *proof,
	sg_bsp_completeness_code_t code, uint32_t record);
float SG_BspProofDot(const float left[3], const float right[3]);
int SG_BspProofFiniteVector(const float value[3]);
void SG_BspProofFreeRegion(sg_bsp_proof_region_t *region);
void SG_BspProofFreeRegions(sg_bsp_proof_regions_t *regions);
int SG_BspProofCopyRegion(const sg_bsp_proof_region_t *source,
	sg_bsp_proof_region_t *destination);
int SG_BspProofAppendConstraint(const sg_bsp_proof_region_t *source,
	const sg_bsp_proof_halfspace_t *constraint,
	sg_bsp_proof_region_t *destination);
int SG_BspProofRegionWitness(sg_bsp_proof_context_t *proof,
	const sg_bsp_proof_region_t *region, float witness[3]);
int SG_BspProofRegionHasProtocolVolume(sg_bsp_proof_context_t *proof,
	const sg_bsp_proof_region_t *region);
int SG_BspProofRegionHasProtocolPoint(sg_bsp_proof_context_t *proof,
	const sg_bsp_proof_region_t *region);
int SG_BspProofRegionPointWitness(sg_bsp_proof_context_t *proof,
	const sg_bsp_proof_region_t *region, int32_t witness[3]);
int SG_BspProofRegionBounds(sg_bsp_proof_context_t *proof,
	sg_bsp_proof_region_t *region);
int SG_BspProofAppendOwnedRegion(sg_bsp_proof_context_t *proof,
	sg_bsp_proof_regions_t *regions, sg_bsp_proof_region_t *region);
int SG_BspProofCellRegion(const sg_configuration_space_t *space,
	uint32_t cell_index, sg_bsp_proof_region_t *region_out);
int SG_BspProofBuildExpected(sg_bsp_proof_context_t *proof);
int SG_BspProofAuditStates(sg_bsp_proof_context_t *proof);
int SG_BspProofRegionOutsideUnion(sg_bsp_proof_context_t *proof,
	const sg_bsp_proof_region_t *source,
	const sg_bsp_proof_region_t *subtractors, uint32_t subtractor_count);
int SG_BspProofRegionOutsideUnionExact(sg_bsp_proof_context_t *proof,
	const sg_bsp_proof_region_t *source,
	const sg_bsp_proof_region_t *subtractors, uint32_t subtractor_count);
int SG_BspProofRegionsIntersect(sg_bsp_proof_context_t *proof,
	const sg_bsp_proof_region_t *left,
	const sg_bsp_proof_region_t *right);
int SG_BspProofAuditCoverage(sg_bsp_proof_context_t *proof);
int SG_BspProofAuditPortals(sg_bsp_proof_context_t *proof);
int SG_BspProofBuildFaceRefs(sg_bsp_proof_context_t *proof,
	sg_bsp_proof_face_ref_t **refs_out, uint32_t *count_out);
void SG_BspProofFreeFaceRefs(sg_bsp_proof_face_ref_t *refs, uint32_t count);
int SG_BspProofBuildPortalRefs(sg_bsp_proof_context_t *proof,
	sg_bsp_proof_portal_ref_t **refs_out);
uint32_t SG_BspProofPortalLowerBound(const sg_bsp_proof_portal_ref_t *refs,
	uint32_t count, uint32_t low_cell, uint32_t high_cell, uint32_t stance);

#endif
