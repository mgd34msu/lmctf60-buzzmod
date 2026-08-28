/* Certificate-independent proof of static world-model-0 BSP configuration
 * coverage. Transformed mover instances are outside this proof's scope. */
#ifndef SG_BSP_COMPLETENESS_PROOF_H
#define SG_BSP_COMPLETENESS_PROOF_H

#include "sg_configuration_space.h"

typedef enum sg_bsp_completeness_code_e
{
	SG_BSP_COMPLETENESS_OK = 0,
	SG_BSP_COMPLETENESS_INVALID_ARGUMENT,
	SG_BSP_COMPLETENESS_INVALID_WORLD,
	SG_BSP_COMPLETENESS_INVALID_CELL,
	SG_BSP_COMPLETENESS_INVALID_PORTAL,
	SG_BSP_COMPLETENESS_OMITTED_CELL,
	SG_BSP_COMPLETENESS_INVENTED_CELL,
	SG_BSP_COMPLETENESS_OMITTED_PORTAL,
	SG_BSP_COMPLETENESS_INVENTED_PORTAL,
	SG_BSP_COMPLETENESS_HOST_DISAGREEMENT,
	SG_BSP_COMPLETENESS_OVERFLOW,
	SG_BSP_COMPLETENESS_OUT_OF_MEMORY
} sg_bsp_completeness_code_t;

typedef struct sg_bsp_completeness_result_s
{
	sg_bsp_completeness_code_t code;
	uint32_t record;
	uint32_t expected_cells;
	uint32_t represented_cells;
	uint32_t proved_cells;
	uint32_t omitted_cells;
	uint32_t invented_cells;
	uint32_t expected_portals;
	uint32_t represented_portals;
	uint32_t proved_portals;
	uint32_t omitted_portals;
	uint32_t invented_portals;
	uint32_t standing_regions;
	uint32_t crouching_regions;
	uint32_t supported_witnesses;
	uint32_t airborne_witnesses;
	uint32_t water_witnesses;
	uint32_t void_witnesses;
	/* State witness counts are diagnostics, not semantic partition proofs. */
	uint64_t collision_leaf_visits;
	uint64_t leaf_brush_candidates;
	uint64_t blocker_cell_candidates;
	uint64_t blocker_subtraction_candidates;
	uint64_t analytically_removed_pieces;
	uint64_t coverage_region_examined;
	uint64_t coverage_region_candidates;
	uint64_t cell_overlap_candidates;
	uint64_t portal_face_candidates;
	uint64_t portal_endpoint_lookups;
	uint64_t portal_lookup_candidates;
	uint64_t lattice_solve_calls;
	uint64_t lattice_constraints;
	uint32_t lattice_maximum_binary_shift;
} sg_bsp_completeness_result_t;

int SG_BspCompletenessProve(const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *space,
	sg_bsp_completeness_result_t *result_out);
const char *SG_BspCompletenessCodeString(sg_bsp_completeness_code_t code);

#endif
