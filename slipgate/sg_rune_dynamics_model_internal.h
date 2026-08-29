#ifndef SG_RUNE_DYNAMICS_MODEL_INTERNAL_H
#define SG_RUNE_DYNAMICS_MODEL_INTERNAL_H

#include "sg_rune_dynamics_model.h"

int SG_RuneDynamicsGeometryValid(const sg_rune_dynamics_model_t *model);
uint8_t SG_RuneAffineOperatorRankExact(
	const sg_rune_affine_state_operator_t *operator);

#endif
