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
int SG_FieldRefinementPointInCellExact(
	const sg_field_refinement_vertex_t *const vertices[8],
	const sg_field_refinement_vertex_t *point);
int SG_RuneDynamicsLocatePointExact(const sg_rune_dynamics_model_t *model,
	const sg_rune_state_chart_ref_t *chart, const sg_rune_vec3_t *position,
	const sg_rune_vec3_t *velocity, float elapsed_ms,
	sg_rune_state_simplex_id_t *simplex_out,
	sg_field_reach_atom_id_t *atom_out,
	sg_field_refinement_node_id_t *leaf_out);
int SG_FieldRefinementCellsProperlyMeet(
	const sg_field_refinement_vertex_t *const left[8],
	const sg_field_refinement_vertex_t *const right[8]);
int SG_FieldRefinementCellOrientation(
	const sg_field_refinement_vertex_t *const vertices[8]);
int SG_FieldRefinementVertexExactMidpoint(
	const sg_field_refinement_vertex_t *middle,
	const sg_field_refinement_vertex_t *left,
	const sg_field_refinement_vertex_t *right);
int SG_FieldOutcomeCanonicalImage(
	const sg_field_refinement_vertex_t *const vertices[8],
	const sg_field_outcome_t *outcome, sg_rune_flow_enclosure_t *image_out);

#endif
