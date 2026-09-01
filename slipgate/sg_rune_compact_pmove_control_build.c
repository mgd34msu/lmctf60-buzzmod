#include "sg_rune_compact_pmove_control_build_private.h"

#include <limits.h>
#include <string.h>

static void SetError(sg_rune_pmove_control_error_t *error_out,
	sg_rune_pmove_control_error_t error)
{
	if (error_out)
		*error_out = error;
}

int SG_RunePmoveControlBuildAxisCorridorPrivate(
	const sg_rune_pmove_control_build_input_t *input,
	sg_rune_pmove_control_storage_t *storage,
	sg_rune_pmove_control_model_t *model_out,
	sg_rune_pmove_control_error_t *error_out)
{
	SetError(error_out, SG_RUNE_PMOVE_CONTROL_ERROR_NONE);
	if (!input || !storage || !model_out)
	{
		SetError(error_out, SG_RUNE_PMOVE_CONTROL_ERROR_INVALID_ARGUMENT);
		return 0;
	}
	if (input->cell == input->target_cell ||
		input->portal == SG_RUNE_PMOVE_CONTROL_NONE ||
		input->corridor_min_q8[0] >= input->corridor_max_q8[0] ||
		input->corridor_min_q8[1] >= input->corridor_max_q8[1] ||
		input->portal_q8 <= input->corridor_min_q8[0] ||
		input->portal_q8 > input->corridor_max_q8[0] ||
		input->hull_half_width_q8 <= 0 ||
		input->maximum_velocity_q8 !=
			SG_RUNE_PMOVE_CONTROL_MAXIMUM_VELOCITY_Q8 ||
		(int64_t)input->corridor_max_q8[1] -
			input->corridor_min_q8[1] > INT32_MAX ||
		(int64_t)input->corridor_max_q8[1] -
			input->corridor_min_q8[1] <
			(int64_t)input->hull_half_width_q8 * 2)
	{
		SetError(error_out, SG_RUNE_PMOVE_CONTROL_ERROR_INVALID_DOMAIN);
		return 0;
	}
	memset(storage, 0, sizeof(*storage));
	memset(model_out, 0, sizeof(*model_out));
	storage->potential.id = 0U;
	storage->potential.divisor = 8U;
	storage->potential.distance_weight = 8U;
	storage->potential.reversal_velocity_weight = 8U;
	storage->potential.lateral_position_weight = 0U;
	storage->potential.lateral_velocity_weight = 1U;
	storage->region.id = 0U;
	storage->region.cell = input->cell;
	storage->region.target_portal = input->portal;
	storage->region.target_cell = input->target_cell;
	storage->region.potential = 0U;
	storage->region.certificate = 0U;
	storage->region.first_transition = 0U;
	storage->region.transition_count = 2U;
	storage->region.longitudinal_min_q8 = input->corridor_min_q8[0];
	storage->region.longitudinal_max_q8 = input->corridor_max_q8[0];
	storage->region.lateral_min_q8 = input->corridor_min_q8[1];
	storage->region.lateral_max_q8 = input->corridor_max_q8[1];
	storage->region.velocity_forward_min_q8 = -input->maximum_velocity_q8;
	storage->region.velocity_forward_max_q8 =
		input->maximum_velocity_q8 + 1;
	storage->region.velocity_lateral_min_q8 = -input->maximum_velocity_q8;
	storage->region.velocity_lateral_max_q8 =
		input->maximum_velocity_q8 + 1;
	storage->region.portal_q8 = input->portal_q8;
	storage->region.lateral_center_q8 = input->corridor_min_q8[1] +
		(input->corridor_max_q8[1] - input->corridor_min_q8[1]) / 2;
	storage->certificate.id = 0U;
	storage->certificate.region = 0U;
	storage->certificate.hull_half_width_q8 = input->hull_half_width_q8;
	storage->certificate.wall_clearance_q8 =
		(input->corridor_max_q8[1] - input->corridor_min_q8[1]) / 2;
	storage->certificate.static_support_z_q8 = input->support_z_q8;
	storage->certificate.maximum_velocity_q8 = input->maximum_velocity_q8;
	storage->certificate.friction_keep_numerator = 85U;
	storage->certificate.friction_keep_denominator = 100U;
	storage->certificate.acceleration_per_substep_q8 = 600;
	storage->certificate.wish_speed_q8 = 2400;
	storage->certificate.minimum_descent_units = 1U;
	storage->certificate.dry = 1U;
	storage->certificate.static_world_support = 1U;
	storage->transitions[0].source_region = 0U;
	storage->transitions[0].kind =
		SG_RUNE_PMOVE_CONTROL_TRANSITION_SAME_CELL;
	storage->transitions[0].target_region = 0U;
	storage->transitions[0].target_cell = input->cell;
	storage->transitions[0].portal = SG_RUNE_PMOVE_CONTROL_NONE;
	storage->transitions[0].certificate = 0U;
	storage->transitions[1].source_region = 0U;
	storage->transitions[1].kind = SG_RUNE_PMOVE_CONTROL_TRANSITION_PORTAL;
	storage->transitions[1].target_region = SG_RUNE_PMOVE_CONTROL_NONE;
	storage->transitions[1].target_cell = input->target_cell;
	storage->transitions[1].portal = input->portal;
	storage->transitions[1].certificate = 0U;
	model_out->identity = input->identity;
	model_out->regions = &storage->region;
	model_out->region_count = 1U;
	model_out->potentials = &storage->potential;
	model_out->potential_count = 1U;
	model_out->certificates = &storage->certificate;
	model_out->certificate_count = 1U;
	model_out->transitions = storage->transitions;
	model_out->transition_count = 2U;
	return SG_RunePmoveControlValidate(model_out, error_out);
}
