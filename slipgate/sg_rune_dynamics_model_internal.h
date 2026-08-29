#ifndef SG_RUNE_DYNAMICS_MODEL_INTERNAL_H
#define SG_RUNE_DYNAMICS_MODEL_INTERNAL_H

#include "sg_rune_dynamics_model.h"

int SG_RuneDynamicsGeometryValid(const sg_rune_dynamics_model_t *model);
uint8_t SG_RuneAffineOperatorRankExact(
	const sg_rune_affine_state_operator_t *operator);
int SG_FieldRefinementCellFullRank(
	const sg_field_refinement_vertex_t *const vertices[8]);
int SG_FieldRefinementBoxInsideCell(
	const sg_field_refinement_vertex_t *const vertices[8],
	const sg_rune_flow_enclosure_t *box);
int SG_FieldRefinementCellsProperlyMeet(
	const sg_field_refinement_vertex_t *const left[8],
	const sg_field_refinement_vertex_t *const right[8]);

#endif
