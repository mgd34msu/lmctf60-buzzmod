/* Offline-only construction for the v13 dry supported corridor slice. */
#ifndef SG_RUNE_COMPACT_PMOVE_CONTROL_BUILD_PRIVATE_H
#define SG_RUNE_COMPACT_PMOVE_CONTROL_BUILD_PRIVATE_H

#include "sg_rune_compact_pmove_control_wire.h"

typedef struct sg_rune_pmove_control_build_input_s
{
	sg_rune_pmove_control_identity_t identity;
	uint32_t cell;
	uint32_t portal;
	uint32_t target_cell;
	int32_t corridor_min_q8[2];
	int32_t corridor_max_q8[2];
	int32_t portal_q8;
	int32_t support_z_q8;
	int32_t hull_half_width_q8;
	int32_t maximum_velocity_q8;
} sg_rune_pmove_control_build_input_t;

int SG_RunePmoveControlBuildAxisCorridorPrivate(
	const sg_rune_pmove_control_build_input_t *input,
	sg_rune_pmove_control_storage_t *storage,
	sg_rune_pmove_control_model_t *model_out,
	sg_rune_pmove_control_error_t *error_out);

#endif /* SG_RUNE_COMPACT_PMOVE_CONTROL_BUILD_PRIVATE_H */
