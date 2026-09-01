#include "sg_rune_compact_pmove_control.h"

#include <limits.h>
#include <string.h>

static void SetError(sg_rune_pmove_control_error_t *error_out,
	sg_rune_pmove_control_error_t error)
{
	if (error_out)
		*error_out = error;
}

static int AddU64(uint64_t left, uint64_t right, uint64_t *out)
{
	if (!out || left > UINT64_MAX - right)
		return 0;
	*out = left + right;
	return 1;
}

static int MulU64(uint64_t left, uint64_t right, uint64_t *out)
{
	if (!out || (right != 0U && left > UINT64_MAX / right))
		return 0;
	*out = left * right;
	return 1;
}

static uint64_t AbsI32(int32_t value)
{
	return value < 0 ? (uint64_t)(-(int64_t)value) : (uint64_t)value;
}

static uint64_t AbsI64(int64_t value)
{
	return value < 0 ? (uint64_t)(-value) : (uint64_t)value;
}

static int LateralStateViable(
	const sg_rune_pmove_control_region_t *region,
	const sg_rune_pmove_control_state_t *state)
{
	uint64_t left_span;
	uint64_t right_span;
	uint64_t half_span;
	uint64_t position_debt;
	uint64_t velocity_debt;

	if (!region || !state)
		return 0;
	left_span = (uint64_t)((int64_t)region->lateral_center_q8 -
		region->lateral_min_q8);
	right_span = (uint64_t)((int64_t)region->lateral_max_q8 -
		region->lateral_center_q8);
	half_span = left_span < right_span ? left_span : right_span;
	position_debt = AbsI64((int64_t)state->origin_q8[1] -
		region->lateral_center_q8);
	velocity_debt = AbsI32(state->velocity_q8[1]);
	return position_debt *
		SG_RUNE_PMOVE_CONTROL_LATERAL_STOP_SECONDS_DENOMINATOR +
		velocity_debt < half_span *
		SG_RUNE_PMOVE_CONTROL_LATERAL_STOP_SECONDS_DENOMINATOR;
}

static int IdentityValid(const sg_rune_pmove_control_identity_t *identity)
{
	uint32_t index;
	int bsp_nonzero = 0;

	if (!identity)
		return 0;
	for (index = 0U; index < SG_RUNE_PMOVE_CONTROL_BSP_IDENTITY_BYTES; index++)
		if (identity->bsp_identity[index] != 0U)
			bsp_nonzero = 1;
	return identity && identity->version == SG_RUNE_PMOVE_CONTROL_VERSION &&
		identity->reserved == 0U && identity->reserved_2 == 0U &&
		identity->compact_artifact_id != 0U &&
		identity->bsp_content_id != 0U && bsp_nonzero &&
		identity->physics_abi_id != 0U &&
		identity->collision_law_id ==
			SG_RUNE_PMOVE_CONTROL_COLLISION_LAW_ID &&
		identity->pmove_law_id == SG_RUNE_PMOVE_CONTROL_PMOVE_LAW_ID &&
		identity->pmove_behavior_id == identity->physics_abi_id &&
		identity->frame_ms == SG_RUNE_PMOVE_CONTROL_FRAME_MS &&
		identity->substep_ms == SG_RUNE_PMOVE_CONTROL_SUBSTEP_MS &&
		identity->substep_count == SG_RUNE_PMOVE_CONTROL_SUBSTEPS &&
		identity->frame_cost_units != 0U &&
		identity->source_reserve_units ==
			SG_RUNE_PMOVE_CONTROL_SOURCE_RESERVE;
}

static int StateInRegion(const sg_rune_pmove_control_model_t *model,
	const sg_rune_pmove_control_region_t *region,
	const sg_rune_pmove_control_state_t *state)
{
	const sg_rune_pmove_control_certificate_t *certificate;

	if (!model || !region || !state || region->certificate >=
		model->certificate_count)
		return 0;
	certificate = &model->certificates[region->certificate];
	return state->cell == region->cell && state->standing == 1U &&
		state->dry == 1U && state->supported == 1U &&
		state->support_is_static_world == 1U &&
		state->origin_q8[0] >= region->longitudinal_min_q8 &&
		state->origin_q8[0] < region->longitudinal_max_q8 &&
		state->origin_q8[1] >= region->lateral_min_q8 &&
		state->origin_q8[1] < region->lateral_max_q8 &&
		state->velocity_q8[0] >= region->velocity_forward_min_q8 &&
		state->velocity_q8[0] < region->velocity_forward_max_q8 &&
		state->velocity_q8[1] >= region->velocity_lateral_min_q8 &&
		state->velocity_q8[1] < region->velocity_lateral_max_q8 &&
		LateralStateViable(region, state) &&
		state->origin_q8[2] == certificate->static_support_z_q8 &&
		state->velocity_q8[2] == 0;
}

static uint64_t MinimumForwardProgressQ8(
	const sg_rune_pmove_control_certificate_t *certificate)
{
	uint64_t velocity = 0U;
	uint64_t progress = 0U;
	uint32_t step;

	for (step = 0U; step < SG_RUNE_PMOVE_CONTROL_SUBSTEPS; step++)
	{
		uint64_t kept;
		uint64_t distance;

		kept = velocity * certificate->friction_keep_numerator /
			certificate->friction_keep_denominator;
		velocity = kept + (uint64_t)certificate->acceleration_per_substep_q8;
		if (velocity > (uint64_t)certificate->wish_speed_q8)
			velocity = (uint64_t)certificate->wish_speed_q8;
		distance = velocity * SG_RUNE_PMOVE_CONTROL_SUBSTEP_MS /
			UINT64_C(1000);
		/* One q8 unit per host snap is a conservative lower enclosure. */
		if (distance != 0U)
			distance--;
		progress += distance;
	}
	return progress;
}

static int LateralEnvelopeInvariant(
	const sg_rune_pmove_control_certificate_t *certificate)
{
	uint64_t keep_power = 1U;
	uint64_t denominator_power = 1U;
	uint64_t retained_sum = 0U;
	uint64_t step_power = 1U;
	uint32_t step;

	if (!certificate || certificate->friction_keep_denominator == 0U)
		return 0;
	for (step = 0U; step < SG_RUNE_PMOVE_CONTROL_SUBSTEPS; step++)
	{
		keep_power *= certificate->friction_keep_numerator;
		denominator_power *= certificate->friction_keep_denominator;
	}
	for (step = 0U; step < SG_RUNE_PMOVE_CONTROL_SUBSTEPS; step++)
	{
		uint32_t remaining;
		uint64_t term;

		step_power *= certificate->friction_keep_numerator;
		term = step_power;
		for (remaining = step + 1U;
			remaining < SG_RUNE_PMOVE_CONTROL_SUBSTEPS; remaining++)
			term *= certificate->friction_keep_denominator;
		retained_sum += term;
	}
	/* In q8 units the envelope is |y|+|vy|/4.  Over four 25 ms
	 * substeps, neutral lateral input gives
	 *   dy <= 25/1000 * sum((85/100)^i) * |vy|
	 * and |vy'| <= (85/100)^4 * |vy|.  Compare both sides over the
	 * common denominator 4000*100^4.  YQ2's integer snap truncates both
	 * lateral position and velocity toward zero on this obstacle-free law. */
	return (uint64_t)certificate->friction_keep_numerator <
		certificate->friction_keep_denominator &&
		(uint64_t)SG_RUNE_PMOVE_CONTROL_SUBSTEP_MS *
		SG_RUNE_PMOVE_CONTROL_LATERAL_STOP_SECONDS_DENOMINATOR *
		retained_sum + UINT64_C(1000) * keep_power <=
		UINT64_C(1000) * denominator_power;
}

static int MinimumCertifiedDescentUnits(
	const sg_rune_pmove_control_certificate_t *certificate,
	const sg_rune_pmove_control_potential_t *potential, uint64_t *units_out)
{
	uint64_t minimum;
	uint64_t keep_power_numerator;
	uint64_t keep_power_denominator;
	uint32_t step;

	if (!certificate || !potential || !units_out || potential->divisor == 0U)
		return 0;
	minimum = MinimumForwardProgressQ8(certificate) *
		potential->distance_weight / potential->divisor;
	/* On this branch-fixed host law, friction retains at most 85/100 of
	 * every horizontal velocity component each substep.  Acceleration toward
	 * the portal can only improve a reversed component.  This rational
	 * inequality proves the reversal debt dominates the full-frame maximum
	 * backward displacement for every value in the declared interval; no
	 * sampled state is generalized. */
	keep_power_numerator = 1U;
	keep_power_denominator = 1U;
	for (step = 0U; step < SG_RUNE_PMOVE_CONTROL_SUBSTEPS; step++)
	{
		keep_power_numerator *= certificate->friction_keep_numerator;
		keep_power_denominator *= certificate->friction_keep_denominator;
	}
	if ((uint64_t)potential->reversal_velocity_weight *
		(keep_power_denominator - keep_power_numerator) * UINT64_C(1000) <
		(uint64_t)potential->distance_weight *
		SG_RUNE_PMOVE_CONTROL_FRAME_MS *
		keep_power_denominator)
		return 0;
	if (minimum > 1U)
		minimum = 1U;
	*units_out = minimum;
	return 1;
}

int SG_RunePmoveControlValidate(const sg_rune_pmove_control_model_t *model,
	sg_rune_pmove_control_error_t *error_out)
{
	uint32_t index;

	SetError(error_out, SG_RUNE_PMOVE_CONTROL_ERROR_NONE);
	if (!model)
	{
		SetError(error_out, SG_RUNE_PMOVE_CONTROL_ERROR_INVALID_ARGUMENT);
		return 0;
	}
	if (!IdentityValid(&model->identity))
	{
		SetError(error_out, SG_RUNE_PMOVE_CONTROL_ERROR_INVALID_IDENTITY);
		return 0;
	}
	if (!model->regions || !model->potentials || !model->certificates ||
		!model->transitions || model->region_count == 0U ||
		model->potential_count == 0U || model->certificate_count == 0U ||
		model->transition_count == 0U)
	{
		SetError(error_out, SG_RUNE_PMOVE_CONTROL_ERROR_INVALID_DOMAIN);
		return 0;
	}
	for (index = 0U; index < model->potential_count; index++)
	{
		const sg_rune_pmove_control_potential_t *potential =
			&model->potentials[index];

		if (potential->id != index || potential->divisor != 8U ||
			potential->distance_weight != 8U ||
			potential->reversal_velocity_weight != 8U ||
			potential->lateral_position_weight != 0U ||
			potential->lateral_velocity_weight != 1U ||
			(uint64_t)potential->reversal_velocity_weight * UINT64_C(1000) <
			(uint64_t)potential->distance_weight * model->identity.frame_ms)
		{
			SetError(error_out,
				SG_RUNE_PMOVE_CONTROL_ERROR_INVALID_CERTIFICATE);
			return 0;
		}
	}
	for (index = 0U; index < model->region_count; index++)
	{
		const sg_rune_pmove_control_region_t *region = &model->regions[index];
		uint64_t left_span;
		uint64_t right_span;
		uint64_t half_span;
		uint64_t end;

		if (region->id != index || region->cell == region->target_cell ||
			region->target_portal == SG_RUNE_PMOVE_CONTROL_NONE ||
			region->potential >= model->potential_count ||
			region->certificate >= model->certificate_count ||
			region->longitudinal_min_q8 >= region->longitudinal_max_q8 ||
			region->lateral_min_q8 >= region->lateral_max_q8 ||
			(int64_t)region->longitudinal_max_q8 -
				region->longitudinal_min_q8 > INT32_MAX ||
			(int64_t)region->lateral_max_q8 -
				region->lateral_min_q8 > INT32_MAX ||
			region->velocity_forward_min_q8 >=
				region->velocity_forward_max_q8 ||
			region->velocity_lateral_min_q8 >=
				region->velocity_lateral_max_q8 ||
			region->portal_q8 <= region->longitudinal_min_q8 ||
			region->portal_q8 > region->longitudinal_max_q8 ||
			region->lateral_center_q8 < region->lateral_min_q8 ||
			region->lateral_center_q8 >= region->lateral_max_q8 ||
			!AddU64(region->first_transition, region->transition_count, &end) ||
			end > model->transition_count || region->transition_count != 2U)
		{
			SetError(error_out, SG_RUNE_PMOVE_CONTROL_ERROR_INVALID_DOMAIN);
			return 0;
		}
		left_span = (uint64_t)((int64_t)region->lateral_center_q8 -
			region->lateral_min_q8);
		right_span = (uint64_t)((int64_t)region->lateral_max_q8 -
			region->lateral_center_q8);
		half_span = left_span < right_span ? left_span : right_span;
		if (half_span *
			SG_RUNE_PMOVE_CONTROL_LATERAL_STOP_SECONDS_DENOMINATOR <=
			SG_RUNE_PMOVE_CONTROL_MAXIMUM_VELOCITY_Q8)
		{
			SetError(error_out,
				SG_RUNE_PMOVE_CONTROL_ERROR_INVALID_CERTIFICATE);
			return 0;
		}
	}
	for (index = 0U; index < model->certificate_count; index++)
	{
		const sg_rune_pmove_control_certificate_t *certificate =
			&model->certificates[index];
		const sg_rune_pmove_control_region_t *region;
		const sg_rune_pmove_control_potential_t *potential;
		uint64_t progress;
		uint64_t proved_units;

		if (certificate->id != index || certificate->region >= model->region_count ||
			certificate->hull_half_width_q8 <= 0 ||
			certificate->wall_clearance_q8 < certificate->hull_half_width_q8 ||
			certificate->maximum_velocity_q8 !=
				SG_RUNE_PMOVE_CONTROL_MAXIMUM_VELOCITY_Q8 ||
			certificate->friction_keep_numerator != 85U ||
			certificate->friction_keep_denominator != 100U ||
			certificate->acceleration_per_substep_q8 != 600 ||
			certificate->wish_speed_q8 != 2400 || certificate->dry != 1U ||
			certificate->static_world_support != 1U ||
			!LateralEnvelopeInvariant(certificate))
		{
			SetError(error_out,
				SG_RUNE_PMOVE_CONTROL_ERROR_INVALID_CERTIFICATE);
			return 0;
		}
		region = &model->regions[certificate->region];
		potential = &model->potentials[region->potential];
		if (region->certificate != index ||
			certificate->wall_clearance_q8 !=
				(region->lateral_max_q8 - region->lateral_min_q8) / 2 ||
			region->velocity_forward_min_q8 != -certificate->maximum_velocity_q8 ||
			region->velocity_forward_max_q8 != certificate->maximum_velocity_q8 + 1 ||
			region->velocity_lateral_min_q8 != -certificate->maximum_velocity_q8 ||
			region->velocity_lateral_max_q8 != certificate->maximum_velocity_q8 + 1)
		{
			SetError(error_out,
				SG_RUNE_PMOVE_CONTROL_ERROR_INVALID_CERTIFICATE);
			return 0;
		}
		progress = MinimumForwardProgressQ8(certificate);
		(void)progress;
		if (!MinimumCertifiedDescentUnits(certificate, potential,
			&proved_units))
		{
			SetError(error_out,
				SG_RUNE_PMOVE_CONTROL_ERROR_INVALID_CERTIFICATE);
			return 0;
		}
		if (proved_units < model->identity.frame_cost_units ||
			certificate->minimum_descent_units != proved_units)
		{
			SetError(error_out,
				SG_RUNE_PMOVE_CONTROL_ERROR_INVALID_CERTIFICATE);
			return 0;
		}
	}
	for (index = 0U; index < model->transition_count; index++)
	{
		const sg_rune_pmove_control_transition_t *transition =
			&model->transitions[index];
		const sg_rune_pmove_control_region_t *source;

		if (transition->source_region >= model->region_count ||
			transition->certificate >= model->certificate_count)
		{
			SetError(error_out,
				SG_RUNE_PMOVE_CONTROL_ERROR_INVALID_REFERENCE);
			return 0;
		}
		source = &model->regions[transition->source_region];
		if (transition->certificate != source->certificate ||
			(index < source->first_transition ||
			 index >= source->first_transition + source->transition_count))
		{
			SetError(error_out,
				SG_RUNE_PMOVE_CONTROL_ERROR_INVALID_REFERENCE);
			return 0;
		}
		if (transition->kind == SG_RUNE_PMOVE_CONTROL_TRANSITION_SAME_CELL)
		{
			if (transition->target_region >= model->region_count ||
				model->regions[transition->target_region].cell != source->cell ||
				transition->target_cell != source->cell ||
				transition->portal != SG_RUNE_PMOVE_CONTROL_NONE)
			{
				SetError(error_out,
					SG_RUNE_PMOVE_CONTROL_ERROR_INVALID_REFERENCE);
				return 0;
			}
		}
		else if (transition->kind == SG_RUNE_PMOVE_CONTROL_TRANSITION_PORTAL)
		{
			if (transition->target_region != SG_RUNE_PMOVE_CONTROL_NONE ||
				transition->target_cell != source->target_cell ||
				transition->portal != source->target_portal)
			{
				SetError(error_out,
					SG_RUNE_PMOVE_CONTROL_ERROR_INVALID_REFERENCE);
				return 0;
			}
		}
		else
		{
			SetError(error_out,
				SG_RUNE_PMOVE_CONTROL_ERROR_INVALID_REFERENCE);
			return 0;
		}
	}
	for (index = 0U; index < model->region_count; index++)
	{
		const sg_rune_pmove_control_region_t *region = &model->regions[index];
		const sg_rune_pmove_control_transition_t *same =
			&model->transitions[region->first_transition];
		const sg_rune_pmove_control_transition_t *portal =
			&model->transitions[region->first_transition + 1U];

		if (same->kind != SG_RUNE_PMOVE_CONTROL_TRANSITION_SAME_CELL ||
			same->source_region != index || same->target_region >=
				model->region_count ||
			model->regions[same->target_region].cell != region->cell ||
			same->target_cell != region->cell ||
			same->portal != SG_RUNE_PMOVE_CONTROL_NONE ||
			portal->kind != SG_RUNE_PMOVE_CONTROL_TRANSITION_PORTAL ||
			portal->source_region != index ||
			portal->target_region != SG_RUNE_PMOVE_CONTROL_NONE ||
			portal->target_cell != region->target_cell ||
			portal->portal != region->target_portal)
		{
			SetError(error_out,
				SG_RUNE_PMOVE_CONTROL_ERROR_INVALID_REFERENCE);
			return 0;
		}
	}
	return 1;
}

int SG_RunePmoveControlPotentialCeil(
	const sg_rune_pmove_control_model_t *model, uint32_t region_index,
	const sg_rune_pmove_control_state_t *state, uint64_t tail_units,
	uint64_t *units_out, sg_rune_pmove_control_error_t *error_out)
{
	const sg_rune_pmove_control_region_t *region;
	const sg_rune_pmove_control_potential_t *potential;
	uint64_t numerator = 0U;
	uint64_t term;
	uint64_t local;
	uint64_t distance;

	SetError(error_out, SG_RUNE_PMOVE_CONTROL_ERROR_NONE);
	if (units_out)
		*units_out = 0U;
	if (!model || !state || !units_out || region_index >= model->region_count)
	{
		SetError(error_out, SG_RUNE_PMOVE_CONTROL_ERROR_INVALID_ARGUMENT);
		return 0;
	}
	region = &model->regions[region_index];
	if (!StateInRegion(model, region, state))
	{
		SetError(error_out, SG_RUNE_PMOVE_CONTROL_ERROR_REGION_MISS);
		return 0;
	}
	potential = &model->potentials[region->potential];
	distance = state->origin_q8[0] >= region->portal_q8 ? 0U :
		(uint64_t)((int64_t)region->portal_q8 - state->origin_q8[0]);
	if (!MulU64(distance, potential->distance_weight, &numerator) ||
		!MulU64(state->velocity_q8[0] < 0 ?
			(uint64_t)(-(int64_t)state->velocity_q8[0]) : 0U,
			potential->reversal_velocity_weight, &term) ||
		!AddU64(numerator, term, &numerator) ||
		!MulU64(AbsI64((int64_t)state->origin_q8[1] -
			region->lateral_center_q8),
			potential->lateral_position_weight, &term) ||
		!AddU64(numerator, term, &numerator) ||
		!MulU64(AbsI32(state->velocity_q8[1]),
			potential->lateral_velocity_weight, &term) ||
		!AddU64(numerator, term, &numerator) ||
		!AddU64(numerator, potential->divisor - 1U, &term))
	{
		SetError(error_out, SG_RUNE_PMOVE_CONTROL_ERROR_OVERFLOW);
		return 0;
	}
	local = term / potential->divisor;
	if (!AddU64(tail_units, local, units_out))
	{
		SetError(error_out, SG_RUNE_PMOVE_CONTROL_ERROR_OVERFLOW);
		return 0;
	}
	return 1;
}

int SG_RunePmoveControlGradient(
	const sg_rune_pmove_control_model_t *model, uint32_t region_index,
	const sg_rune_pmove_control_state_t *state,
	sg_rune_pmove_control_gradient_t *gradient_out,
	sg_rune_pmove_control_error_t *error_out)
{
	const sg_rune_pmove_control_region_t *region;
	const sg_rune_pmove_control_potential_t *potential;

	SetError(error_out, SG_RUNE_PMOVE_CONTROL_ERROR_NONE);
	if (gradient_out)
		memset(gradient_out, 0, sizeof(*gradient_out));
	if (!model || !state || !gradient_out || region_index >= model->region_count)
	{
		SetError(error_out, SG_RUNE_PMOVE_CONTROL_ERROR_INVALID_ARGUMENT);
		return 0;
	}
	region = &model->regions[region_index];
	if (!StateInRegion(model, region, state))
	{
		SetError(error_out, SG_RUNE_PMOVE_CONTROL_ERROR_REGION_MISS);
		return 0;
	}
	potential = &model->potentials[region->potential];
	gradient_out->longitudinal = -(int64_t)potential->distance_weight;
	gradient_out->lateral_position = state->origin_q8[1] ==
		region->lateral_center_q8 ? 0 :
		(state->origin_q8[1] > region->lateral_center_q8 ? 1 : -1) *
		(int64_t)potential->lateral_position_weight;
	gradient_out->reversal_velocity = state->velocity_q8[0] < 0 ?
		-(int64_t)potential->reversal_velocity_weight : 0;
	gradient_out->lateral_velocity = state->velocity_q8[1] == 0 ? 0 :
		(state->velocity_q8[1] > 0 ? 1 : -1) *
		(int64_t)potential->lateral_velocity_weight;
	return 1;
}

int SG_RunePmoveControlCheckDescent(
	const sg_rune_pmove_control_model_t *model, uint32_t region_index,
	const sg_rune_pmove_control_state_t *source,
	const sg_rune_pmove_control_state_t *target, uint32_t transition_index,
	uint64_t authenticated_tail_units, uint64_t *source_units_out,
	uint64_t *next_units_out,
	sg_rune_pmove_control_error_t *error_out)
{
	const sg_rune_pmove_control_region_t *region;
	uint64_t source_units;
	uint64_t next;
	uint64_t sum;
	const sg_rune_pmove_control_transition_t *transition;

	SetError(error_out, SG_RUNE_PMOVE_CONTROL_ERROR_NONE);
	if (source_units_out)
		*source_units_out = 0U;
	if (next_units_out)
		*next_units_out = 0U;
	if (!model || !source || !target || !source_units_out || !next_units_out ||
		region_index >= model->region_count ||
		transition_index >= model->transition_count)
	{
		SetError(error_out, SG_RUNE_PMOVE_CONTROL_ERROR_INVALID_ARGUMENT);
		return 0;
	}
	region = &model->regions[region_index];
	transition = &model->transitions[transition_index];
	if (transition->source_region != region_index ||
		!SG_RunePmoveControlPotentialCeil(model, region_index, source,
			authenticated_tail_units, &source_units, error_out) ||
		!AddU64(source_units, model->identity.source_reserve_units,
			&source_units))
	{
		if (error_out && *error_out == SG_RUNE_PMOVE_CONTROL_ERROR_NONE)
			*error_out = SG_RUNE_PMOVE_CONTROL_ERROR_OVERFLOW;
		return 0;
	}
	if (transition->kind == SG_RUNE_PMOVE_CONTROL_TRANSITION_PORTAL)
	{
		if (source->origin_q8[0] >= region->portal_q8 ||
			target->cell != region->target_cell ||
			target->origin_q8[0] < region->portal_q8)
		{
			SetError(error_out, SG_RUNE_PMOVE_CONTROL_ERROR_PORTAL_MISMATCH);
			return 0;
		}
		next = authenticated_tail_units;
	}
	else if (transition->kind == SG_RUNE_PMOVE_CONTROL_TRANSITION_SAME_CELL)
	{
		if (!StateInRegion(model, region, target))
		{
			SetError(error_out, SG_RUNE_PMOVE_CONTROL_ERROR_REGION_MISS);
			return 0;
		}
		if (!SG_RunePmoveControlPotentialCeil(model, region_index,
			target, authenticated_tail_units, &next, error_out))
			return 0;
	}
	else
	{
		SetError(error_out, SG_RUNE_PMOVE_CONTROL_ERROR_INVALID_REFERENCE);
		return 0;
	}
	if (!AddU64(next, model->identity.frame_cost_units, &sum))
	{
		SetError(error_out, SG_RUNE_PMOVE_CONTROL_ERROR_OVERFLOW);
		return 0;
	}
	if (sum >= source_units)
	{
		SetError(error_out, SG_RUNE_PMOVE_CONTROL_ERROR_NO_DESCENT);
		return 0;
	}
	*source_units_out = source_units;
	*next_units_out = next;
	return 1;
}

const char *SG_RunePmoveControlErrorString(
	sg_rune_pmove_control_error_t error)
{
	switch (error)
	{
	case SG_RUNE_PMOVE_CONTROL_ERROR_NONE: return "none";
	case SG_RUNE_PMOVE_CONTROL_ERROR_INVALID_ARGUMENT: return "invalid argument";
	case SG_RUNE_PMOVE_CONTROL_ERROR_INVALID_IDENTITY: return "invalid identity";
	case SG_RUNE_PMOVE_CONTROL_ERROR_INVALID_DOMAIN: return "invalid domain";
	case SG_RUNE_PMOVE_CONTROL_ERROR_INVALID_REFERENCE: return "invalid reference";
	case SG_RUNE_PMOVE_CONTROL_ERROR_INVALID_CERTIFICATE: return "invalid certificate";
	case SG_RUNE_PMOVE_CONTROL_ERROR_REGION_MISS: return "region miss";
	case SG_RUNE_PMOVE_CONTROL_ERROR_STALE_IDENTITY: return "stale identity";
	case SG_RUNE_PMOVE_CONTROL_ERROR_INCOMPLETE_REPLAY: return "incomplete replay";
	case SG_RUNE_PMOVE_CONTROL_ERROR_DYNAMIC_COLLISION: return "dynamic collision";
	case SG_RUNE_PMOVE_CONTROL_ERROR_PORTAL_MISMATCH: return "portal mismatch";
	case SG_RUNE_PMOVE_CONTROL_ERROR_OVERFLOW: return "overflow";
	case SG_RUNE_PMOVE_CONTROL_ERROR_NO_DESCENT: return "no descent";
	default: return "unknown";
	}
}
