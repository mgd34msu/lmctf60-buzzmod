#include "sg_rune_compact_composer.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct sg_rune_compact_composer_counts_s
{
	uint32_t functions;
	uint32_t inputs;
	uint32_t constants;
	uint32_t affines;
	uint32_t affine_slopes;
	uint32_t polynomials;
	uint32_t polynomial_coefficients;
	uint32_t ballistics;
	uint32_t piecewise;
	uint32_t piecewise_clauses;
} sg_rune_compact_composer_counts_t;

typedef struct sg_rune_compact_composer_analytic_source_s
{
	const sg_rune_compact_analytic_t *analytic;
	uint32_t *function_map;
} sg_rune_compact_composer_analytic_source_t;

typedef struct sg_rune_compact_composer_candidate_s
{
	const sg_rune_compact_composer_analytic_source_t *source;
	uint32_t function;
} sg_rune_compact_composer_candidate_t;

typedef enum sg_rune_compact_composer_step_result_e
{
	SG_RUNE_COMPACT_COMPOSER_STEP_OK = 0,
	SG_RUNE_COMPACT_COMPOSER_STEP_INVALID,
	SG_RUNE_COMPACT_COMPOSER_STEP_LIMIT,
	SG_RUNE_COMPACT_COMPOSER_STEP_OVERFLOW,
	SG_RUNE_COMPACT_COMPOSER_STEP_OUT_OF_MEMORY
} sg_rune_compact_composer_step_result_t;

struct sg_rune_compact_composer_s
{
	sg_rune_compact_model_t model;
	sg_rune_compact_static_t static_data;
	sg_rune_compact_analytic_t analytic;
	sg_rune_compact_cell_t *cells;
	sg_rune_compact_facet_t *facets;
	sg_rune_compact_incidence_t *incidences;
	sg_rune_compact_incidence_index_t *cell_incidences;
	sg_rune_q8_vec3_t *vertices;
	sg_rune_compact_portal_t *portals;
	sg_rune_compact_source_surface_t *source_surfaces;
	sg_rune_q8_vec3_t *source_surface_vertices;
	sg_rune_movement_capability_t *movement_capabilities;
	sg_rune_compact_movement_state_t *movement_states;
	sg_rune_compact_movement_fiber_t *movement_fibers;
	sg_rune_compact_movement_hook_target_t *movement_hook_targets;
	sg_rune_analytic_function_index_t *movement_fiber_function_refs;
	sg_rune_compact_movement_angular_schedule_t *movement_angular_schedules;
	sg_rune_weapon_profile_t *weapon_profiles;
	sg_rune_weapon_response_kernel_t *weapon_kernels;
	sg_rune_compact_weapon_field_attachment_t *weapon_attachments;
	sg_rune_compact_weapon_relation_span_t *weapon_relation_spans;
	sg_rune_compact_response_ref_t *weapon_relation_refs;
	sg_rune_weapon_function_ref_t *weapon_function_refs;
	sg_rune_compact_mechanism_authority_t *mechanism_authorities;
	sg_rune_compact_mechanism_controller_t *mechanism_authority_controllers;
	sg_rune_compact_mechanism_topology_edge_t
		*mechanism_authority_topology_edges;
	sg_rune_compact_mechanism_transition_t *mechanism_authority_transitions;
	uint32_t *mechanism_authority_transition_static_indices;
	uint32_t *static_transition_authority_indices;
	sg_rune_compact_response_fragment_t *response_fragments;
	sg_rune_compact_response_halfspace_t *response_halfspaces;
	sg_rune_compact_response_patch_t *response_patches;
	sg_rune_q8_vec3_t *response_target_vertices;
	sg_rune_compact_response_split_t *response_splits;
	sg_rune_compact_response_fact_t *response_facts;
	sg_rune_compact_response_candidate_group_t *response_candidate_groups;
	sg_rune_compact_response_endpoint_group_t *response_source_endpoint_groups;
	uint32_t *response_source_endpoint_members;
	sg_rune_compact_response_endpoint_group_t *response_target_endpoint_groups;
	uint32_t *response_target_endpoint_members;
	sg_rune_compact_static_occluder_t *response_occluders;
	sg_rune_analytic_function_t *functions;
	sg_rune_analytic_input_dimension_t *input_dimensions;
	sg_rune_analytic_constant_t *constants;
	sg_rune_analytic_affine_t *affines;
	sg_rune_analytic_scalar_bits_t *affine_slopes;
	sg_rune_analytic_polynomial_t *polynomials;
	sg_rune_analytic_scalar_bits_t *polynomial_coefficients;
	sg_rune_analytic_ballistic_t *ballistics;
	sg_rune_analytic_piecewise_t *piecewise;
	sg_rune_analytic_piecewise_clause_t *piecewise_clauses;
	sg_rune_compact_mechanism_t *mechanisms;
	sg_rune_compact_static_mechanism_controller_t *mechanism_controllers;
	sg_rune_compact_mechanism_edge_t *mechanism_edges;
	sg_rune_compact_static_transition_t *transitions;
	sg_rune_compact_landmark_t *landmarks;
	sg_rune_compact_cell_index_t *landmark_cells;
	sg_rune_compact_facet_annotation_t *facet_annotations;
	sg_rune_compact_portal_mechanism_t *portal_mechanisms;
	unsigned char *storage;
};

static int CopyMovement(const sg_rune_compact_movement_fields_view_t *movement,
	const uint32_t *function_map, sg_rune_compact_composer_t *composer);
static int CopyWeapon(const sg_rune_compact_weapon_field_view_t *weapon,
	const sg_rune_compact_builder_view_t *builder, const uint32_t *function_map,
	sg_rune_compact_composer_t *composer);
static int CopyResponse(
	const sg_rune_compact_weapon_relations_view_t *relations,
	sg_rune_compact_composer_t *composer);
static int BindCellSpans(sg_rune_compact_composer_t *composer);
static int MarkLiveAnalytics(
	const sg_rune_compact_movement_fields_view_t *movement,
	const sg_rune_compact_weapon_field_view_t *weapon, uint8_t *movement_live,
	uint8_t *weapon_live, uint32_t *live_count_out);

static void SetError(sg_rune_compact_composer_error_t *error,
	sg_rune_compact_composer_error_code_t code,
	sg_rune_compact_composer_record_domain_t domain, uint32_t record)
{
	if (error != NULL) {
		error->code = code;
		error->domain = domain;
		error->record = record;
	}
}

static int CountAdd(uint32_t left, uint32_t right, uint32_t *result)
{
	if (result == NULL || right > UINT32_MAX - left)
		return 0;
	*result = left + right;
	return 1;
}

static int SizeMultiply(size_t count, size_t size, size_t *bytes_out)
{
	if (bytes_out == NULL || (size != 0U && count > SIZE_MAX / size))
		return 0;
	*bytes_out = count * size;
	return 1;
}

static int SizeAdd(size_t left, size_t right, size_t *total_out)
{
	if (total_out == NULL || right > SIZE_MAX - left)
		return 0;
	*total_out = left + right;
	return 1;
}

static int AlignSize(size_t value, size_t alignment, size_t *aligned_out)
{
	size_t remainder;

	if (aligned_out == NULL || alignment == 0U)
		return 0;
	remainder = value % alignment;
	return remainder == 0U ? (*aligned_out = value, 1) :
		SizeAdd(value, alignment - remainder, aligned_out);
}

static int LayoutAdd(size_t *total, uint32_t count, size_t element_size)
{
	size_t aligned;
	size_t bytes;

	if (total == NULL || count == 0U)
		return total != NULL;
	if (!AlignSize(*total, _Alignof(max_align_t), &aligned) ||
		!SizeMultiply((size_t)count, element_size, &bytes) ||
		!SizeAdd(aligned, bytes, total))
		return 0;
	return 1;
}

static void *TakeArray(unsigned char *storage, size_t storage_bytes,
	size_t *cursor, uint32_t count, size_t element_size)
{
	size_t aligned;
	size_t bytes;
	size_t end;

	if (count == 0U)
		return NULL;
	if (storage == NULL || cursor == NULL ||
		!AlignSize(*cursor, _Alignof(max_align_t), &aligned) ||
		!SizeMultiply((size_t)count, element_size, &bytes) ||
		!SizeAdd(aligned, bytes, &end) || end > storage_bytes)
		return NULL;
	*cursor = end;
	return storage + aligned;
}

static int ArrayPresent(const void *array, uint32_t count)
{
	return count == 0U || array != NULL;
}

static int ResponseSealEqual(const sg_rune_compact_response_seal_t *left,
	const sg_rune_compact_response_seal_t *right)
{
	return left != NULL && right != NULL && left->version == right->version &&
		left->reserved == right->reserved && left->flags == right->flags &&
		left->split_frontier_count == right->split_frontier_count &&
		left->source_fragment_count == right->source_fragment_count &&
		left->target_patch_count == right->target_patch_count &&
		left->split_count == right->split_count &&
		left->response_pair_count == right->response_pair_count &&
		left->certified_direct_pair_count ==
			right->certified_direct_pair_count &&
		left->certified_static_impact_pair_count ==
			right->certified_static_impact_pair_count &&
		left->unresolved_response_pair_count ==
			right->unresolved_response_pair_count &&
		left->unresolved_candidate_group_count ==
			right->unresolved_candidate_group_count &&
		left->source_endpoint_group_count ==
			right->source_endpoint_group_count &&
		left->target_endpoint_group_count ==
			right->target_endpoint_group_count &&
		left->source_endpoint_member_count ==
			right->source_endpoint_member_count &&
		left->target_endpoint_member_count ==
			right->target_endpoint_member_count &&
		left->static_occluder_count == right->static_occluder_count &&
		left->compact_facet_count == right->compact_facet_count &&
		left->compact_cell_count == right->compact_cell_count &&
		left->compact_source_surface_count ==
			right->compact_source_surface_count &&
		left->compact_source_surface_vertex_count ==
			right->compact_source_surface_vertex_count &&
		left->source_surface_catalog_seal ==
			right->source_surface_catalog_seal;
}

static int ResponseProjectionStableEqual(
	const sg_rune_compact_response_projection_t *left,
	const sg_rune_compact_response_projection_t *right)
{
	return left != NULL && right != NULL &&
		left->source_fragments == right->source_fragments &&
		left->source_fragment_count == right->source_fragment_count &&
		left->source_halfspaces == right->source_halfspaces &&
		left->source_halfspace_count == right->source_halfspace_count &&
		left->target_patches == right->target_patches &&
		left->target_patch_count == right->target_patch_count &&
		left->target_vertices == right->target_vertices &&
		left->target_vertex_count == right->target_vertex_count &&
		left->splits == right->splits &&
		left->split_count == right->split_count &&
		left->facts == right->facts && left->fact_count == right->fact_count &&
		left->candidate_groups == right->candidate_groups &&
		left->candidate_group_count == right->candidate_group_count &&
		left->source_endpoint_groups == right->source_endpoint_groups &&
		left->source_endpoint_group_count ==
			right->source_endpoint_group_count &&
		left->source_endpoint_members == right->source_endpoint_members &&
		left->source_endpoint_member_count ==
			right->source_endpoint_member_count &&
		left->target_endpoint_groups == right->target_endpoint_groups &&
		left->target_endpoint_group_count ==
			right->target_endpoint_group_count &&
		left->target_endpoint_members == right->target_endpoint_members &&
		left->target_endpoint_member_count ==
			right->target_endpoint_member_count &&
		left->occluders == right->occluders &&
		left->occluder_count == right->occluder_count &&
		left->exact_live_prefire_trace_required ==
			right->exact_live_prefire_trace_required &&
		left->reserved[0] == right->reserved[0] &&
		left->reserved[1] == right->reserved[1] &&
		left->reserved[2] == right->reserved[2] &&
		ResponseSealEqual(&left->seal, &right->seal);
}

static int SpanWithin(uint32_t first, uint32_t count, uint32_t limit)
{
	return first <= limit && count <= limit - first;
}

static int CompareU32(uint32_t left, uint32_t right)
{
	return left < right ? -1 : left > right ? 1 : 0;
}

static int AnalyticCountsAdd(sg_rune_compact_composer_counts_t *total,
	const sg_rune_compact_analytic_t *analytic)
{
	return total != NULL && analytic != NULL &&
		CountAdd(total->functions, analytic->function_count,
			&total->functions) &&
		CountAdd(total->inputs, analytic->input_dimension_count,
			&total->inputs) &&
		CountAdd(total->constants, analytic->constant_count,
			&total->constants) &&
		CountAdd(total->affines, analytic->affine_count,
			&total->affines) &&
		CountAdd(total->affine_slopes, analytic->affine_slope_count,
			&total->affine_slopes) &&
		CountAdd(total->polynomials, analytic->polynomial_count,
			&total->polynomials) &&
		CountAdd(total->polynomial_coefficients,
			analytic->polynomial_coefficient_count,
			&total->polynomial_coefficients) &&
		CountAdd(total->ballistics, analytic->ballistic_count,
			&total->ballistics) &&
		CountAdd(total->piecewise, analytic->piecewise_count,
			&total->piecewise) &&
		CountAdd(total->piecewise_clauses,
			analytic->piecewise_clause_count,
			&total->piecewise_clauses);
}

static int AnalyticCountsWithinLimits(
	const sg_rune_compact_composer_counts_t *counts)
{
	return counts != NULL &&
		counts->functions <= SG_RUNE_ANALYTIC_MAX_FUNCTIONS &&
		counts->inputs <= SG_RUNE_ANALYTIC_MAX_INPUT_DIMENSIONS &&
		counts->constants <= SG_RUNE_ANALYTIC_MAX_FUNCTIONS &&
		counts->affines <= SG_RUNE_ANALYTIC_MAX_FUNCTIONS &&
		counts->affine_slopes <= SG_RUNE_ANALYTIC_MAX_AFFINE_SLOPES &&
		counts->polynomials <= SG_RUNE_ANALYTIC_MAX_FUNCTIONS &&
		counts->polynomial_coefficients <=
			SG_RUNE_ANALYTIC_MAX_POLYNOMIAL_COEFFICIENTS &&
		counts->ballistics <= SG_RUNE_ANALYTIC_MAX_FUNCTIONS &&
		counts->piecewise <= SG_RUNE_ANALYTIC_MAX_FUNCTIONS &&
		counts->piecewise_clauses <= SG_RUNE_ANALYTIC_MAX_PIECEWISE_CLAUSES;
}

static int AnalyticOutputAtLimit(const sg_rune_compact_analytic_t *analytic)
{
	return analytic->function_count >= SG_RUNE_ANALYTIC_MAX_FUNCTIONS ||
		analytic->input_dimension_count >=
			SG_RUNE_ANALYTIC_MAX_INPUT_DIMENSIONS ||
		analytic->constant_count >= SG_RUNE_ANALYTIC_MAX_FUNCTIONS ||
		analytic->affine_count >= SG_RUNE_ANALYTIC_MAX_FUNCTIONS ||
		analytic->affine_slope_count >= SG_RUNE_ANALYTIC_MAX_AFFINE_SLOPES ||
		analytic->polynomial_count >= SG_RUNE_ANALYTIC_MAX_FUNCTIONS ||
		analytic->polynomial_coefficient_count >=
			SG_RUNE_ANALYTIC_MAX_POLYNOMIAL_COEFFICIENTS ||
		analytic->ballistic_count >= SG_RUNE_ANALYTIC_MAX_FUNCTIONS ||
		analytic->piecewise_count >= SG_RUNE_ANALYTIC_MAX_FUNCTIONS ||
		analytic->piecewise_clause_count >=
			SG_RUNE_ANALYTIC_MAX_PIECEWISE_CLAUSES;
}

static const sg_rune_analytic_function_t *CandidateFunction(
	const sg_rune_compact_composer_candidate_t *candidate)
{
	return &candidate->source->analytic->functions[candidate->function];
}

static int CompareInputs(const sg_rune_compact_composer_candidate_t *left,
	const sg_rune_compact_composer_candidate_t *right)
{
	const sg_rune_compact_analytic_t *left_analytic = left->source->analytic;
	const sg_rune_compact_analytic_t *right_analytic = right->source->analytic;
	const sg_rune_analytic_function_t *left_function = CandidateFunction(left);
	const sg_rune_analytic_function_t *right_function = CandidateFunction(right);
	uint32_t offset;
	int comparison = CompareU32(left_function->inputs.count,
		right_function->inputs.count);

	if (comparison != 0)
		return comparison;
	for (offset = 0U; offset < left_function->inputs.count; offset++) {
		comparison = CompareU32((uint32_t)left_analytic->input_dimensions[
			left_function->inputs.first + offset],
			(uint32_t)right_analytic->input_dimensions[
			right_function->inputs.first + offset]);
		if (comparison != 0)
			return comparison;
	}
	return 0;
}

static int CompareScalarSpans(const sg_rune_analytic_scalar_bits_t *left,
	uint32_t left_first, const sg_rune_analytic_scalar_bits_t *right,
	uint32_t right_first, uint32_t count)
{
	uint32_t offset;

	for (offset = 0U; offset < count; offset++) {
		const int comparison = CompareU32(left[left_first + offset].bits,
			right[right_first + offset].bits);

		if (comparison != 0)
			return comparison;
	}
	return 0;
}

static int MappedFunction(const sg_rune_compact_composer_analytic_source_t
	*source, uint32_t function, uint32_t *mapped_out)
{
	if (source == NULL || mapped_out == NULL || function >=
		source->analytic->function_count || source->function_map[function] ==
		UINT32_MAX)
		return 0;
	*mapped_out = source->function_map[function];
	return 1;
}

static int ComparePiecewise(const sg_rune_compact_composer_candidate_t *left,
	const sg_rune_compact_composer_candidate_t *right, int *comparison_out)
{
	const sg_rune_compact_analytic_t *left_analytic = left->source->analytic;
	const sg_rune_compact_analytic_t *right_analytic = right->source->analytic;
	const sg_rune_analytic_piecewise_t *left_piecewise =
		&left_analytic->piecewise[CandidateFunction(left)->definition];
	const sg_rune_analytic_piecewise_t *right_piecewise =
		&right_analytic->piecewise[CandidateFunction(right)->definition];
	uint32_t left_default;
	uint32_t right_default;
	uint32_t offset;
	int comparison;

	if (comparison_out == NULL || !MappedFunction(left->source,
		left_piecewise->default_function.value, &left_default) ||
		!MappedFunction(right->source, right_piecewise->default_function.value,
			&right_default))
		return 0;
	comparison = CompareU32(left_piecewise->selector_input,
		right_piecewise->selector_input);
	if (comparison == 0)
		comparison = CompareU32(left_default, right_default);
	if (comparison == 0)
		comparison = CompareU32(left_piecewise->clauses.count,
			right_piecewise->clauses.count);
	for (offset = 0U; comparison == 0 && offset <
		left_piecewise->clauses.count; offset++) {
		const sg_rune_analytic_piecewise_clause_t *left_clause =
			&left_analytic->piecewise_clauses[
				left_piecewise->clauses.first + offset];
		const sg_rune_analytic_piecewise_clause_t *right_clause =
			&right_analytic->piecewise_clauses[
				right_piecewise->clauses.first + offset];
		uint32_t left_function;
		uint32_t right_function;

		if (!MappedFunction(left->source, left_clause->function.value,
			&left_function) || !MappedFunction(right->source,
			right_clause->function.value, &right_function))
			return 0;
		comparison = CompareU32(left_clause->lower.bits,
			right_clause->lower.bits);
		if (comparison == 0)
			comparison = CompareU32(left_clause->upper.bits,
				right_clause->upper.bits);
		if (comparison == 0)
			comparison = CompareU32((uint32_t)left_clause->ownership,
				(uint32_t)right_clause->ownership);
		if (comparison == 0)
			comparison = CompareU32(left_function, right_function);
	}
	*comparison_out = comparison;
	return 1;
}

static int CandidateCompare(const sg_rune_compact_composer_candidate_t *left,
	const sg_rune_compact_composer_candidate_t *right, int *comparison_out)
{
	const sg_rune_compact_analytic_t *left_analytic = left->source->analytic;
	const sg_rune_compact_analytic_t *right_analytic = right->source->analytic;
	const sg_rune_analytic_function_t *left_function = CandidateFunction(left);
	const sg_rune_analytic_function_t *right_function = CandidateFunction(right);
	int comparison = CompareU32((uint32_t)left_function->form,
		(uint32_t)right_function->form);

	if (comparison == 0)
		comparison = CompareU32((uint32_t)left_function->output,
			(uint32_t)right_function->output);
	if (comparison == 0)
		comparison = CompareInputs(left, right);
	if (comparison != 0) {
		*comparison_out = comparison;
		return 1;
	}
	switch (left_function->form) {
	case SG_RUNE_COMPACT_ANALYTIC_CONSTANT:
		comparison = CompareU32(left_analytic->constants[
			left_function->definition].value.bits,
			right_analytic->constants[right_function->definition].value.bits);
		break;
	case SG_RUNE_COMPACT_ANALYTIC_AFFINE: {
		const sg_rune_analytic_affine_t *left_affine =
			&left_analytic->affines[left_function->definition];
		const sg_rune_analytic_affine_t *right_affine =
			&right_analytic->affines[right_function->definition];

		comparison = CompareU32(left_affine->bias.bits,
			right_affine->bias.bits);
		if (comparison == 0)
			comparison = CompareScalarSpans(left_analytic->affine_slopes,
				left_affine->slopes.first, right_analytic->affine_slopes,
				right_affine->slopes.first, left_affine->slopes.count);
		break;
	}
	case SG_RUNE_COMPACT_ANALYTIC_POLYNOMIAL: {
		const sg_rune_analytic_polynomial_t *left_polynomial =
			&left_analytic->polynomials[left_function->definition];
		const sg_rune_analytic_polynomial_t *right_polynomial =
			&right_analytic->polynomials[right_function->definition];

		comparison = CompareU32((uint32_t)left_polynomial->degree,
			(uint32_t)right_polynomial->degree);
		if (comparison == 0)
			comparison = CompareScalarSpans(
				left_analytic->polynomial_coefficients,
				left_polynomial->coefficients.first,
				right_analytic->polynomial_coefficients,
				right_polynomial->coefficients.first,
				left_polynomial->coefficients.count);
		break;
	}
	case SG_RUNE_COMPACT_ANALYTIC_BALLISTIC: {
		const sg_rune_analytic_ballistic_t *left_ballistic =
			&left_analytic->ballistics[left_function->definition];
		const sg_rune_analytic_ballistic_t *right_ballistic =
			&right_analytic->ballistics[right_function->definition];

		comparison = CompareU32(left_ballistic->initial.bits,
			right_ballistic->initial.bits);
		if (comparison == 0)
			comparison = CompareU32(left_ballistic->first_derivative.bits,
				right_ballistic->first_derivative.bits);
		if (comparison == 0)
			comparison = CompareU32(left_ballistic->half_second_derivative.bits,
				right_ballistic->half_second_derivative.bits);
		break;
	}
	case SG_RUNE_COMPACT_ANALYTIC_PIECEWISE:
		if (!ComparePiecewise(left, right, &comparison))
			return 0;
		break;
	case SG_RUNE_COMPACT_ANALYTIC_FORM_COUNT:
		return 0;
	}
	*comparison_out = comparison;
	return 1;
}

static int SortCandidates(sg_rune_compact_composer_candidate_t *values,
	sg_rune_compact_composer_candidate_t *scratch, uint32_t count)
{
	uint32_t width;

	if (count < 2U)
		return 1;
	for (width = 1U; width < count;) {
		uint32_t first;

		for (first = 0U; first < count;) {
			const uint32_t middle = count - first < width ? count : first + width;
			const uint32_t right = count - middle < width ? count : middle + width;
			uint32_t left_cursor = first;
			uint32_t right_cursor = middle;
			uint32_t output = first;

			while (left_cursor < middle && right_cursor < right) {
				int comparison;

				if (!CandidateCompare(&values[left_cursor],
					&values[right_cursor], &comparison))
					return 0;
				if (comparison <= 0)
					scratch[output++] = values[left_cursor++];
				else
					scratch[output++] = values[right_cursor++];
			}
			while (left_cursor < middle)
				scratch[output++] = values[left_cursor++];
			while (right_cursor < right)
				scratch[output++] = values[right_cursor++];
			first = right;
		}
		memcpy(values, scratch, (size_t)count * sizeof(*values));
		if (width > count / 2U)
			break;
		width *= 2U;
	}
	return 1;
}

static int CopySourceInputs(const sg_rune_compact_composer_candidate_t *candidate,
	sg_rune_compact_composer_t *composer, sg_rune_analytic_function_t *function)
{
	const sg_rune_compact_analytic_t *source = candidate->source->analytic;
	const sg_rune_analytic_function_t *source_function = CandidateFunction(candidate);
	sg_rune_compact_analytic_t *output = &composer->analytic;
	uint32_t offset;

	if (!SpanWithin(output->input_dimension_count,
		source_function->inputs.count, SG_RUNE_ANALYTIC_MAX_INPUT_DIMENSIONS))
		return 0;
	function->inputs.first = output->input_dimension_count;
	function->inputs.count = source_function->inputs.count;
	for (offset = 0U; offset < source_function->inputs.count; offset++)
		composer->input_dimensions[
			output->input_dimension_count + offset] = source->input_dimensions[
			source_function->inputs.first + offset];
	output->input_dimension_count += source_function->inputs.count;
	return 1;
}

static int CopyAnalyticFunction(
	const sg_rune_compact_composer_candidate_t *candidate,
	sg_rune_compact_composer_t *composer, uint32_t *output_index)
{
	const sg_rune_compact_analytic_t *source = candidate->source->analytic;
	const sg_rune_analytic_function_t *source_function = CandidateFunction(candidate);
	sg_rune_compact_analytic_t *output = &composer->analytic;
	sg_rune_analytic_function_t *function;

	if (output == NULL || output_index == NULL || output->function_count >=
		SG_RUNE_ANALYTIC_MAX_FUNCTIONS)
		return 0;
	function = &composer->functions[output->function_count];
	memset(function, 0, sizeof(*function));
	function->form = source_function->form;
	function->output = source_function->output;
	if (!CopySourceInputs(candidate, composer, function))
		return 0;
	switch (function->form) {
	case SG_RUNE_COMPACT_ANALYTIC_CONSTANT:
		function->definition = output->constant_count;
		composer->constants[output->constant_count++] = source->constants[
				source_function->definition];
		break;
	case SG_RUNE_COMPACT_ANALYTIC_AFFINE: {
		const sg_rune_analytic_affine_t *source_affine =
			&source->affines[source_function->definition];
		sg_rune_analytic_affine_t *affine =
			&composer->affines[output->affine_count];

		if (!SpanWithin(output->affine_slope_count, source_affine->slopes.count,
			SG_RUNE_ANALYTIC_MAX_AFFINE_SLOPES))
			return 0;
		function->definition = output->affine_count++;
		*affine = *source_affine;
		affine->slopes.first = output->affine_slope_count;
		memcpy(composer->affine_slopes +
			output->affine_slope_count,
			source->affine_slopes + source_affine->slopes.first,
			(size_t)source_affine->slopes.count *
				sizeof(*source->affine_slopes));
		output->affine_slope_count += source_affine->slopes.count;
		break;
	}
	case SG_RUNE_COMPACT_ANALYTIC_POLYNOMIAL: {
		const sg_rune_analytic_polynomial_t *source_polynomial =
			&source->polynomials[source_function->definition];
		sg_rune_analytic_polynomial_t *polynomial =
			&composer->polynomials[output->polynomial_count];

		if (!SpanWithin(output->polynomial_coefficient_count,
			source_polynomial->coefficients.count,
			SG_RUNE_ANALYTIC_MAX_POLYNOMIAL_COEFFICIENTS))
			return 0;
		function->definition = output->polynomial_count++;
		*polynomial = *source_polynomial;
		polynomial->coefficients.first = output->polynomial_coefficient_count;
		memcpy(composer->polynomial_coefficients +
			output->polynomial_coefficient_count,
			source->polynomial_coefficients +
				source_polynomial->coefficients.first,
			(size_t)source_polynomial->coefficients.count *
				sizeof(*source->polynomial_coefficients));
		output->polynomial_coefficient_count +=
			source_polynomial->coefficients.count;
		break;
	}
	case SG_RUNE_COMPACT_ANALYTIC_BALLISTIC:
		function->definition = output->ballistic_count;
		composer->ballistics[output->ballistic_count++] = source->ballistics[
				source_function->definition];
		break;
	case SG_RUNE_COMPACT_ANALYTIC_PIECEWISE: {
		const sg_rune_analytic_piecewise_t *source_piecewise =
			&source->piecewise[source_function->definition];
		sg_rune_analytic_piecewise_t *piecewise =
			&composer->piecewise[output->piecewise_count];
		uint32_t default_function;
		uint32_t offset;

		if (!MappedFunction(candidate->source,
			source_piecewise->default_function.value, &default_function) ||
			!SpanWithin(output->piecewise_clause_count,
				source_piecewise->clauses.count,
				SG_RUNE_ANALYTIC_MAX_PIECEWISE_CLAUSES))
			return 0;
		function->definition = output->piecewise_count++;
		*piecewise = *source_piecewise;
		piecewise->default_function.value = default_function;
		piecewise->clauses.first = output->piecewise_clause_count;
		for (offset = 0U; offset < source_piecewise->clauses.count; offset++) {
			const sg_rune_analytic_piecewise_clause_t *source_clause =
				&source->piecewise_clauses[
					source_piecewise->clauses.first + offset];
			sg_rune_analytic_piecewise_clause_t *clause =
				&composer->piecewise_clauses[
					output->piecewise_clause_count + offset];
			uint32_t mapped;

			if (!MappedFunction(candidate->source,
				source_clause->function.value, &mapped))
				return 0;
			*clause = *source_clause;
			clause->function.value = mapped;
		}
		output->piecewise_clause_count += source_piecewise->clauses.count;
		break;
	}
	case SG_RUNE_COMPACT_ANALYTIC_FORM_COUNT:
		return 0;
	}
	*output_index = output->function_count++;
	return 1;
}

static sg_rune_compact_composer_step_result_t MergeAnalytics(
	const sg_rune_compact_movement_fields_view_t *movement,
	const sg_rune_compact_weapon_field_view_t *weapon,
	const sg_rune_compact_builder_view_t *builder,
	sg_rune_compact_composer_t *composer,
	sg_rune_compact_composer_counts_t *counts_out)
{
	sg_rune_compact_composer_analytic_source_t sources[2];
	sg_rune_compact_composer_candidate_t *candidates = NULL;
	sg_rune_compact_composer_candidate_t *scratch = NULL;
	uint8_t *movement_live = NULL;
	uint8_t *weapon_live = NULL;
	sg_rune_compact_composer_counts_t counts;
	uint32_t candidate_count = 0U;
	uint32_t live_count = 0U;
	uint32_t source_index;
	uint32_t form;
	sg_rune_compact_composer_step_result_t result =
		SG_RUNE_COMPACT_COMPOSER_STEP_INVALID;

	memset(&counts, 0, sizeof(counts));
	if (!AnalyticCountsAdd(&counts, &movement->analytic) ||
		!AnalyticCountsAdd(&counts, &weapon->analytic))
		return SG_RUNE_COMPACT_COMPOSER_STEP_OVERFLOW;
	memset(sources, 0, sizeof(sources));
	sources[0].analytic = &movement->analytic;
	sources[1].analytic = &weapon->analytic;
	sources[0].function_map = calloc(movement->analytic.function_count,
		sizeof(*sources[0].function_map));
	sources[1].function_map = calloc(weapon->analytic.function_count,
		sizeof(*sources[1].function_map));
	movement_live = calloc(movement->analytic.function_count,
		sizeof(*movement_live));
	weapon_live = calloc(weapon->analytic.function_count,
		sizeof(*weapon_live));
	candidates = calloc(counts.functions, sizeof(*candidates));
	scratch = calloc(counts.functions, sizeof(*scratch));
	if (sources[0].function_map == NULL || sources[1].function_map == NULL ||
		movement_live == NULL || weapon_live == NULL || candidates == NULL ||
		scratch == NULL) {
		result = SG_RUNE_COMPACT_COMPOSER_STEP_OUT_OF_MEMORY;
		goto done;
	}
	if (!MarkLiveAnalytics(movement, weapon, movement_live, weapon_live,
		&live_count))
		goto done;
	memset(sources[0].function_map, 0xff,
		(size_t)movement->analytic.function_count *
			sizeof(*sources[0].function_map));
	memset(sources[1].function_map, 0xff,
		(size_t)weapon->analytic.function_count *
			sizeof(*sources[1].function_map));
	composer->analytic.version = SG_RUNE_COMPACT_ANALYTIC_VERSION;
	for (form = 0U;
		form < (uint32_t)SG_RUNE_COMPACT_ANALYTIC_FORM_COUNT; form++) {
		uint32_t form_count = 0U;
		uint32_t index;

		for (source_index = 0U; source_index < 2U; source_index++)
			for (index = 0U; index < sources[source_index].analytic->function_count;
				index++)
				if ((uint32_t)sources[source_index].analytic->functions[index].form ==
					form && (source_index == 0U ? movement_live[index] :
						weapon_live[index]) != 0U) {
					candidates[form_count].source = &sources[source_index];
					candidates[form_count++].function = index;
				}
		if (!SortCandidates(candidates, scratch, form_count))
			goto done;
		for (index = 0U; index < form_count; index++) {
			uint32_t mapped;
			int comparison = 1;

			if (index != 0U && !CandidateCompare(&candidates[index - 1U],
				&candidates[index], &comparison))
				goto done;
			if (index != 0U && comparison == 0) {
				if (!MappedFunction(candidates[index - 1U].source,
					candidates[index - 1U].function, &mapped))
					goto done;
			} else if (!CopyAnalyticFunction(&candidates[index], composer,
				&mapped)) {
				if (AnalyticOutputAtLimit(&composer->analytic))
					result = SG_RUNE_COMPACT_COMPOSER_STEP_LIMIT;
				goto done;
			}
			candidates[index].source->function_map[candidates[index].function] =
				mapped;
			candidate_count++;
		}
	}
	if (candidate_count != live_count ||
		!SG_RuneCompactAnalyticValidate(&composer->analytic, NULL) ||
		!CopyMovement(movement, sources[0].function_map, composer) ||
		!CopyWeapon(weapon, builder, sources[1].function_map, composer) ||
		!BindCellSpans(composer))
		goto done;
	memset(counts_out, 0, sizeof(*counts_out));
	counts_out->functions = composer->analytic.function_count;
	counts_out->inputs = composer->analytic.input_dimension_count;
	counts_out->constants = composer->analytic.constant_count;
	counts_out->affines = composer->analytic.affine_count;
	counts_out->affine_slopes = composer->analytic.affine_slope_count;
	counts_out->polynomials = composer->analytic.polynomial_count;
	counts_out->polynomial_coefficients =
		composer->analytic.polynomial_coefficient_count;
	counts_out->ballistics = composer->analytic.ballistic_count;
	counts_out->piecewise = composer->analytic.piecewise_count;
	counts_out->piecewise_clauses = composer->analytic.piecewise_clause_count;
	if (!AnalyticCountsWithinLimits(counts_out)) {
		result = SG_RUNE_COMPACT_COMPOSER_STEP_LIMIT;
		goto done;
	}
	result = SG_RUNE_COMPACT_COMPOSER_STEP_OK;
done:
	free(scratch);
	free(candidates);
	free(weapon_live);
	free(movement_live);
	free(sources[1].function_map);
	free(sources[0].function_map);
	return result;
}

static int FragmentAnalyticsValid(
	const sg_rune_compact_movement_fields_view_t *movement,
	const sg_rune_compact_weapon_field_view_t *weapon)
{
	return movement != NULL && weapon != NULL &&
		SG_RuneCompactAnalyticValidate(&movement->analytic, NULL) &&
		SG_RuneCompactAnalyticValidate(&weapon->analytic, NULL) &&
		ArrayPresent(movement->capabilities, movement->capability_count) &&
		ArrayPresent(movement->states, movement->state_count) &&
		ArrayPresent(movement->fibers, movement->fiber_count) &&
		ArrayPresent(movement->hook_targets, movement->hook_target_count) &&
		ArrayPresent(movement->fiber_function_refs,
			movement->fiber_function_ref_count) &&
		ArrayPresent(movement->angular_schedules,
			movement->angular_schedule_count) &&
		ArrayPresent(weapon->kernels, weapon->kernel_count) &&
		ArrayPresent(weapon->attachments, weapon->attachment_count) &&
		ArrayPresent(weapon->relation_spans, weapon->relation_span_count) &&
		ArrayPresent(weapon->relation_refs, weapon->relation_ref_count) &&
		ArrayPresent(weapon->weapon_function_refs,
			weapon->weapon_function_ref_count);
}

static int ResponseReferenceValid(
	const sg_rune_compact_response_projection_t *response,
	const sg_rune_compact_response_ref_t *reference)
{
	if (response == NULL || reference == NULL ||
		reference->kind < 0 ||
		reference->kind >= SG_RUNE_COMPACT_RESPONSE_REF_KIND_COUNT)
		return 0;
	if (reference->kind == SG_RUNE_COMPACT_RESPONSE_REF_CANDIDATE_GROUP)
		return reference->index < response->candidate_group_count;
	return reference->index < response->fact_count;
}

static int FragmentReferencesValid(
	const sg_rune_compact_movement_fields_view_t *movement,
	const sg_rune_compact_weapon_field_view_t *weapon,
	const sg_rune_compact_response_projection_t *response,
	const sg_rune_compact_static_t *static_data,
	const sg_rune_compact_mechanisms_view_t *mechanisms, uint32_t cell_count)
{
	uint32_t index;
	uint32_t fiber_cursor = 0U;
	uint32_t target_cursor = 0U;
	uint32_t movement_ref_cursor = 0U;
	uint32_t weapon_ref_cursor = 0U;

	for (index = 0U; index < movement->capability_count; index++) {
		const sg_rune_movement_capability_t *capability =
			&movement->capabilities[index];

		if (capability->cell.value >= cell_count ||
			capability->fibers.first != fiber_cursor ||
			!SpanWithin(capability->fibers.first, capability->fibers.count,
				movement->fiber_count))
			return 0;
		fiber_cursor += capability->fibers.count;
	}
	if (fiber_cursor != movement->fiber_count)
		return 0;
	for (index = 0U; index < movement->fiber_count; index++) {
		const sg_rune_compact_movement_fiber_t *fiber = &movement->fibers[index];
		uint32_t ref;

		if (fiber->capability.value >= movement->capability_count ||
			fiber->source_state.value >= movement->state_count ||
			fiber->destination_state.value >= movement->state_count ||
			fiber->functions.first != movement_ref_cursor ||
			!SpanWithin(fiber->functions.first, fiber->functions.count,
				movement->fiber_function_ref_count) ||
			fiber->hook_targets.first != target_cursor ||
			!SpanWithin(fiber->hook_targets.first, fiber->hook_targets.count,
				movement->hook_target_count) ||
			(fiber->angular_schedule != SG_RUNE_COMPACT_INDEX_NONE &&
			 fiber->angular_schedule >= movement->angular_schedule_count) ||
			(fiber->mechanism_transition.value != SG_RUNE_COMPACT_INDEX_NONE &&
			 fiber->mechanism_transition.value >= mechanisms->transition_count))
			return 0;
		if (movement->capabilities[fiber->capability.value].kind ==
			SG_RUNE_MOVEMENT_CAPABILITY_CONTROLLER_ACTION) {
			if (fiber->controller_action_controller.value >=
					mechanisms->controller_count ||
				fiber->controller_action_target.value >=
					mechanisms->mechanism_count ||
				mechanisms->controllers[
					fiber->controller_action_controller.value].mechanism !=
					fiber->controller_action_target.value ||
				mechanisms->transitions[
					fiber->mechanism_transition.value].mechanism !=
					fiber->controller_action_target.value)
				return 0;
		} else if (fiber->controller_action_controller.value !=
				SG_RUNE_COMPACT_INDEX_NONE ||
			fiber->controller_action_target.value !=
				SG_RUNE_COMPACT_INDEX_NONE)
			return 0;
		for (ref = fiber->functions.first;
			ref < fiber->functions.first + fiber->functions.count; ref++)
			if (movement->fiber_function_refs[ref].value >=
				movement->analytic.function_count)
				return 0;
		movement_ref_cursor += fiber->functions.count;
		target_cursor += fiber->hook_targets.count;
	}
	if (target_cursor != movement->hook_target_count)
		return 0;
	for (index = 0U; index < movement->hook_target_count; index++) {
		const sg_rune_compact_movement_hook_target_t *target =
			&movement->hook_targets[index];
		const sg_rune_analytic_function_span_t spans[6] = {
			target->functions.bolt, target->functions.body,
			target->functions.pull, target->functions.release,
			target->functions.coast, target->functions.relaunch
		};
		uint32_t phase;

		if (target->fiber.value >= movement->fiber_count ||
			(target->provenance ==
				SG_RUNE_MOVEMENT_HOOK_TARGET_PROVENANCE_GENERIC ?
			 (target->response.kind !=
					SG_RUNE_COMPACT_RESPONSE_REF_KIND_COUNT ||
			  target->response.index != SG_RUNE_COMPACT_INDEX_NONE) :
			 !ResponseReferenceValid(response, &target->response)))
			return 0;
		for (phase = 0U; phase < 6U; phase++) {
			uint32_t ref;

			if (spans[phase].first != movement_ref_cursor ||
				!SpanWithin(spans[phase].first, spans[phase].count,
					movement->fiber_function_ref_count))
				return 0;
			for (ref = spans[phase].first;
				ref < spans[phase].first + spans[phase].count; ref++)
				if (movement->fiber_function_refs[ref].value >=
					movement->analytic.function_count)
					return 0;
			movement_ref_cursor += spans[phase].count;
		}
	}
	if (movement_ref_cursor != movement->fiber_function_ref_count)
		return 0;
	for (index = 0U; index < weapon->kernel_count; index++) {
		const sg_rune_weapon_response_kernel_t *kernel =
			&weapon->kernels[index];
		uint32_t ref;

		if (kernel->functions.first != weapon_ref_cursor ||
			!SpanWithin(kernel->functions.first, kernel->functions.count,
			weapon->weapon_function_ref_count))
			return 0;
		for (ref = kernel->functions.first;
			ref < kernel->functions.first + kernel->functions.count;
			ref++)
			if (weapon->weapon_function_refs[ref].function.value >=
				weapon->analytic.function_count)
				return 0;
		weapon_ref_cursor += kernel->functions.count;
	}
	if (weapon_ref_cursor != weapon->weapon_function_ref_count)
		return 0;
	for (index = 0U; index < movement->angular_schedule_count; index++)
		if (static_data == NULL ||
			movement->angular_schedules[index].static_mechanism.value >=
				static_data->mechanism_count ||
			movement->angular_schedules[index].authority_mechanism.value >=
				mechanisms->mechanism_count)
			return 0;
	return 1;
}

static int MarkLiveAnalytics(
	const sg_rune_compact_movement_fields_view_t *movement,
	const sg_rune_compact_weapon_field_view_t *weapon, uint8_t *movement_live,
	uint8_t *weapon_live, uint32_t *live_count_out)
{
	uint32_t index;
	uint32_t live_count = 0U;

	if (movement_live == NULL || weapon_live == NULL || live_count_out == NULL)
		return 0;
	for (index = 0U; index < movement->fiber_function_ref_count; index++)
		movement_live[movement->fiber_function_refs[index].value] = 1U;
	for (index = 0U; index < weapon->weapon_function_ref_count; index++)
		weapon_live[weapon->weapon_function_refs[index].function.value] = 1U;
	for (index = movement->analytic.function_count; index != 0U;) {
		const uint32_t function = --index;
		const sg_rune_analytic_function_t *definition =
			&movement->analytic.functions[function];

		if (movement_live[function] != 0U && definition->form ==
			SG_RUNE_COMPACT_ANALYTIC_PIECEWISE) {
			const sg_rune_analytic_piecewise_t *piecewise =
				&movement->analytic.piecewise[definition->definition];
			uint32_t clause;

			movement_live[piecewise->default_function.value] = 1U;
			for (clause = piecewise->clauses.first;
				clause < piecewise->clauses.first + piecewise->clauses.count;
				clause++)
				movement_live[movement->analytic.piecewise_clauses[clause].
					function.value] = 1U;
		}
	}
	for (index = weapon->analytic.function_count; index != 0U;) {
		const uint32_t function = --index;
		const sg_rune_analytic_function_t *definition =
			&weapon->analytic.functions[function];

		if (weapon_live[function] != 0U && definition->form ==
			SG_RUNE_COMPACT_ANALYTIC_PIECEWISE) {
			const sg_rune_analytic_piecewise_t *piecewise =
				&weapon->analytic.piecewise[definition->definition];
			uint32_t clause;

			weapon_live[piecewise->default_function.value] = 1U;
			for (clause = piecewise->clauses.first;
				clause < piecewise->clauses.first + piecewise->clauses.count;
				clause++)
				weapon_live[weapon->analytic.piecewise_clauses[clause].
					function.value] = 1U;
		}
	}
	for (index = 0U; index < movement->analytic.function_count; index++)
		if (movement_live[index] != 0U && !CountAdd(live_count, 1U,
			&live_count))
			return 0;
	for (index = 0U; index < weapon->analytic.function_count; index++)
		if (weapon_live[index] != 0U && !CountAdd(live_count, 1U,
			&live_count))
			return 0;
	*live_count_out = live_count;
	return live_count != 0U;
}

static int LayoutModelStorage(const sg_rune_compact_geometry_view_t *geometry,
	const sg_rune_compact_static_t *static_data,
	const sg_rune_compact_mechanisms_view_t *mechanisms,
	const sg_rune_compact_movement_fields_view_t *movement,
	const sg_rune_compact_weapon_field_view_t *weapon,
	const sg_rune_compact_response_projection_t *response,
	const sg_rune_compact_builder_view_t *builder,
	const sg_rune_compact_composer_counts_t *analytic_counts,
	size_t *bytes_out)
{
	size_t bytes = 0U;

	if (geometry == NULL || static_data == NULL || mechanisms == NULL ||
		movement == NULL ||
		weapon == NULL || response == NULL || builder == NULL ||
		analytic_counts == NULL)
		return 0;
	if (!LayoutAdd(&bytes, geometry->cell_count, sizeof(sg_rune_compact_cell_t)) ||
		!LayoutAdd(&bytes, geometry->facet_count, sizeof(sg_rune_compact_facet_t)) ||
		!LayoutAdd(&bytes, geometry->incidence_count,
			sizeof(sg_rune_compact_incidence_t)) ||
		!LayoutAdd(&bytes, geometry->cell_incidence_count,
			sizeof(sg_rune_compact_incidence_index_t)) ||
		!LayoutAdd(&bytes, geometry->vertex_count, sizeof(sg_rune_q8_vec3_t)) ||
		!LayoutAdd(&bytes, geometry->portal_count,
			sizeof(sg_rune_compact_portal_t)) ||
		!LayoutAdd(&bytes, geometry->source_surface_count,
			sizeof(sg_rune_compact_source_surface_t)) ||
		!LayoutAdd(&bytes, geometry->source_surface_vertex_count,
			sizeof(sg_rune_q8_vec3_t)) ||
		!LayoutAdd(&bytes, movement->capability_count,
			sizeof(sg_rune_movement_capability_t)) ||
		!LayoutAdd(&bytes, movement->state_count,
			sizeof(sg_rune_compact_movement_state_t)) ||
		!LayoutAdd(&bytes, movement->fiber_count,
			sizeof(sg_rune_compact_movement_fiber_t)) ||
		!LayoutAdd(&bytes, movement->hook_target_count,
			sizeof(sg_rune_compact_movement_hook_target_t)) ||
		!LayoutAdd(&bytes, movement->fiber_function_ref_count,
			sizeof(sg_rune_analytic_function_index_t)) ||
		!LayoutAdd(&bytes, movement->angular_schedule_count,
			sizeof(sg_rune_compact_movement_angular_schedule_t)) ||
		!LayoutAdd(&bytes, builder->weapon_profile_count,
			sizeof(sg_rune_weapon_profile_t)) ||
		!LayoutAdd(&bytes, weapon->kernel_count,
			sizeof(sg_rune_weapon_response_kernel_t)) ||
		!LayoutAdd(&bytes, weapon->attachment_count,
			sizeof(sg_rune_compact_weapon_field_attachment_t)) ||
		!LayoutAdd(&bytes, weapon->relation_span_count,
			sizeof(sg_rune_compact_weapon_relation_span_t)) ||
		!LayoutAdd(&bytes, weapon->relation_ref_count,
			sizeof(sg_rune_compact_response_ref_t)) ||
		!LayoutAdd(&bytes, weapon->weapon_function_ref_count,
			sizeof(sg_rune_weapon_function_ref_t)) ||
		!LayoutAdd(&bytes, mechanisms->mechanism_count,
			sizeof(sg_rune_compact_mechanism_authority_t)) ||
		!LayoutAdd(&bytes, mechanisms->controller_count,
			sizeof(sg_rune_compact_mechanism_controller_t)) ||
		!LayoutAdd(&bytes, mechanisms->topology_edge_count,
			sizeof(sg_rune_compact_mechanism_topology_edge_t)) ||
		!LayoutAdd(&bytes, mechanisms->transition_count,
			sizeof(sg_rune_compact_mechanism_transition_t)) ||
		!LayoutAdd(&bytes, mechanisms->transition_count, sizeof(uint32_t)) ||
		!LayoutAdd(&bytes, static_data->transition_count, sizeof(uint32_t)) ||
		!LayoutAdd(&bytes, response->source_fragment_count,
			sizeof(sg_rune_compact_response_fragment_t)) ||
		!LayoutAdd(&bytes, response->source_halfspace_count,
			sizeof(sg_rune_compact_response_halfspace_t)) ||
		!LayoutAdd(&bytes, response->target_patch_count,
			sizeof(sg_rune_compact_response_patch_t)) ||
		!LayoutAdd(&bytes, response->target_vertex_count,
			sizeof(sg_rune_q8_vec3_t)) ||
		!LayoutAdd(&bytes, response->split_count,
			sizeof(sg_rune_compact_response_split_t)) ||
		!LayoutAdd(&bytes, response->fact_count,
			sizeof(sg_rune_compact_response_fact_t)) ||
		!LayoutAdd(&bytes, response->candidate_group_count,
			sizeof(sg_rune_compact_response_candidate_group_t)) ||
		!LayoutAdd(&bytes, response->source_endpoint_group_count,
			sizeof(sg_rune_compact_response_endpoint_group_t)) ||
		!LayoutAdd(&bytes, response->source_endpoint_member_count,
			sizeof(uint32_t)) ||
		!LayoutAdd(&bytes, response->target_endpoint_group_count,
			sizeof(sg_rune_compact_response_endpoint_group_t)) ||
		!LayoutAdd(&bytes, response->target_endpoint_member_count,
			sizeof(uint32_t)) ||
		!LayoutAdd(&bytes, response->occluder_count,
			sizeof(sg_rune_compact_static_occluder_t)) ||
		!LayoutAdd(&bytes, analytic_counts->functions,
			sizeof(sg_rune_analytic_function_t)) ||
		!LayoutAdd(&bytes, analytic_counts->inputs,
			sizeof(sg_rune_analytic_input_dimension_t)) ||
		!LayoutAdd(&bytes, analytic_counts->constants,
			sizeof(sg_rune_analytic_constant_t)) ||
		!LayoutAdd(&bytes, analytic_counts->affines,
			sizeof(sg_rune_analytic_affine_t)) ||
		!LayoutAdd(&bytes, analytic_counts->affine_slopes,
			sizeof(sg_rune_analytic_scalar_bits_t)) ||
		!LayoutAdd(&bytes, analytic_counts->polynomials,
			sizeof(sg_rune_analytic_polynomial_t)) ||
		!LayoutAdd(&bytes, analytic_counts->polynomial_coefficients,
			sizeof(sg_rune_analytic_scalar_bits_t)) ||
		!LayoutAdd(&bytes, analytic_counts->ballistics,
			sizeof(sg_rune_analytic_ballistic_t)) ||
		!LayoutAdd(&bytes, analytic_counts->piecewise,
			sizeof(sg_rune_analytic_piecewise_t)) ||
		!LayoutAdd(&bytes, analytic_counts->piecewise_clauses,
			sizeof(sg_rune_analytic_piecewise_clause_t)) ||
		!LayoutAdd(&bytes, static_data->mechanism_count,
			sizeof(sg_rune_compact_mechanism_t)) ||
		!LayoutAdd(&bytes, static_data->mechanism_controller_count,
			sizeof(sg_rune_compact_static_mechanism_controller_t)) ||
		!LayoutAdd(&bytes, static_data->mechanism_edge_count,
			sizeof(sg_rune_compact_mechanism_edge_t)) ||
		!LayoutAdd(&bytes, static_data->transition_count,
			sizeof(sg_rune_compact_static_transition_t)) ||
		!LayoutAdd(&bytes, static_data->landmark_count,
			sizeof(sg_rune_compact_landmark_t)) ||
		!LayoutAdd(&bytes, static_data->landmark_cell_count,
			sizeof(sg_rune_compact_cell_index_t)) ||
		!LayoutAdd(&bytes, static_data->facet_annotation_count,
			sizeof(sg_rune_compact_facet_annotation_t)) ||
		!LayoutAdd(&bytes, static_data->portal_mechanism_count,
			sizeof(sg_rune_compact_portal_mechanism_t)))
		return 0;
	*bytes_out = bytes;
	return 1;
}

static sg_rune_compact_composer_step_result_t AllocateModelStorage(
	sg_rune_compact_composer_t *composer,
	const sg_rune_compact_geometry_view_t *geometry,
	const sg_rune_compact_static_t *static_data,
	const sg_rune_compact_mechanisms_view_t *mechanisms,
	const sg_rune_compact_movement_fields_view_t *movement,
	const sg_rune_compact_weapon_field_view_t *weapon,
	const sg_rune_compact_response_projection_t *response,
	const sg_rune_compact_builder_view_t *builder,
	const sg_rune_compact_composer_counts_t *analytic_counts)
{
	size_t bytes;
	size_t cursor = 0U;

	if (!LayoutModelStorage(geometry, static_data, mechanisms, movement, weapon,
		response,
		builder, analytic_counts, &bytes) || bytes == 0U)
		return SG_RUNE_COMPACT_COMPOSER_STEP_OVERFLOW;
	composer->storage = calloc(1U, bytes);
	if (composer->storage == NULL)
		return SG_RUNE_COMPACT_COMPOSER_STEP_OUT_OF_MEMORY;
	composer->cells = TakeArray(composer->storage, bytes, &cursor,
		geometry->cell_count, sizeof(*composer->cells));
	composer->facets = TakeArray(composer->storage, bytes, &cursor,
		geometry->facet_count, sizeof(*composer->facets));
	composer->incidences = TakeArray(composer->storage, bytes, &cursor,
		geometry->incidence_count, sizeof(*composer->incidences));
	composer->cell_incidences = TakeArray(composer->storage, bytes, &cursor,
		geometry->cell_incidence_count, sizeof(*composer->cell_incidences));
	composer->vertices = TakeArray(composer->storage, bytes, &cursor,
		geometry->vertex_count, sizeof(*composer->vertices));
	composer->portals = TakeArray(composer->storage, bytes, &cursor,
		geometry->portal_count, sizeof(*composer->portals));
	composer->source_surfaces = TakeArray(composer->storage, bytes, &cursor,
		geometry->source_surface_count, sizeof(*composer->source_surfaces));
	composer->source_surface_vertices = TakeArray(composer->storage, bytes,
		&cursor, geometry->source_surface_vertex_count,
		sizeof(*composer->source_surface_vertices));
	composer->movement_capabilities = TakeArray(composer->storage, bytes, &cursor,
		movement->capability_count, sizeof(*composer->movement_capabilities));
	composer->movement_states = TakeArray(composer->storage, bytes, &cursor,
		movement->state_count, sizeof(*composer->movement_states));
	composer->movement_fibers = TakeArray(composer->storage, bytes, &cursor,
		movement->fiber_count, sizeof(*composer->movement_fibers));
	composer->movement_hook_targets = TakeArray(composer->storage, bytes, &cursor,
		movement->hook_target_count, sizeof(*composer->movement_hook_targets));
	composer->movement_fiber_function_refs = TakeArray(composer->storage, bytes,
		&cursor, movement->fiber_function_ref_count,
		sizeof(*composer->movement_fiber_function_refs));
	composer->movement_angular_schedules = TakeArray(composer->storage, bytes,
		&cursor, movement->angular_schedule_count,
		sizeof(*composer->movement_angular_schedules));
	composer->weapon_profiles = TakeArray(composer->storage, bytes, &cursor,
		builder->weapon_profile_count, sizeof(*composer->weapon_profiles));
	composer->weapon_kernels = TakeArray(composer->storage, bytes, &cursor,
		weapon->kernel_count, sizeof(*composer->weapon_kernels));
	composer->weapon_attachments = TakeArray(composer->storage, bytes, &cursor,
		weapon->attachment_count, sizeof(*composer->weapon_attachments));
	composer->weapon_relation_spans = TakeArray(composer->storage, bytes,
		&cursor, weapon->relation_span_count,
		sizeof(*composer->weapon_relation_spans));
	composer->weapon_relation_refs = TakeArray(composer->storage, bytes, &cursor,
		weapon->relation_ref_count, sizeof(*composer->weapon_relation_refs));
	composer->weapon_function_refs = TakeArray(composer->storage, bytes,
		&cursor, weapon->weapon_function_ref_count,
		sizeof(*composer->weapon_function_refs));
	composer->mechanism_authorities = TakeArray(composer->storage, bytes, &cursor,
		mechanisms->mechanism_count, sizeof(*composer->mechanism_authorities));
	composer->mechanism_authority_controllers = TakeArray(composer->storage,
		bytes, &cursor, mechanisms->controller_count,
		sizeof(*composer->mechanism_authority_controllers));
	composer->mechanism_authority_topology_edges = TakeArray(composer->storage,
		bytes, &cursor, mechanisms->topology_edge_count,
		sizeof(*composer->mechanism_authority_topology_edges));
	composer->mechanism_authority_transitions = TakeArray(composer->storage,
		bytes, &cursor, mechanisms->transition_count,
		sizeof(*composer->mechanism_authority_transitions));
	composer->mechanism_authority_transition_static_indices = TakeArray(
		composer->storage, bytes, &cursor, mechanisms->transition_count,
		sizeof(*composer->mechanism_authority_transition_static_indices));
	composer->static_transition_authority_indices = TakeArray(composer->storage,
		bytes, &cursor, static_data->transition_count,
		sizeof(*composer->static_transition_authority_indices));
	composer->response_fragments = TakeArray(composer->storage, bytes, &cursor,
		response->source_fragment_count, sizeof(*composer->response_fragments));
	composer->response_halfspaces = TakeArray(composer->storage, bytes, &cursor,
		response->source_halfspace_count, sizeof(*composer->response_halfspaces));
	composer->response_patches = TakeArray(composer->storage, bytes, &cursor,
		response->target_patch_count, sizeof(*composer->response_patches));
	composer->response_target_vertices = TakeArray(composer->storage, bytes,
		&cursor, response->target_vertex_count,
		sizeof(*composer->response_target_vertices));
	composer->response_splits = TakeArray(composer->storage, bytes, &cursor,
		response->split_count, sizeof(*composer->response_splits));
	composer->response_facts = TakeArray(composer->storage, bytes, &cursor,
		response->fact_count, sizeof(*composer->response_facts));
	composer->response_candidate_groups = TakeArray(composer->storage, bytes,
		&cursor, response->candidate_group_count,
		sizeof(*composer->response_candidate_groups));
	composer->response_source_endpoint_groups = TakeArray(composer->storage,
		bytes, &cursor, response->source_endpoint_group_count,
		sizeof(*composer->response_source_endpoint_groups));
	composer->response_source_endpoint_members = TakeArray(composer->storage,
		bytes, &cursor, response->source_endpoint_member_count,
		sizeof(*composer->response_source_endpoint_members));
	composer->response_target_endpoint_groups = TakeArray(composer->storage,
		bytes, &cursor, response->target_endpoint_group_count,
		sizeof(*composer->response_target_endpoint_groups));
	composer->response_target_endpoint_members = TakeArray(composer->storage,
		bytes, &cursor, response->target_endpoint_member_count,
		sizeof(*composer->response_target_endpoint_members));
	composer->response_occluders = TakeArray(composer->storage, bytes, &cursor,
		response->occluder_count, sizeof(*composer->response_occluders));
	composer->functions = TakeArray(composer->storage, bytes, &cursor,
		analytic_counts->functions, sizeof(*composer->functions));
	composer->input_dimensions = TakeArray(composer->storage, bytes, &cursor,
		analytic_counts->inputs, sizeof(*composer->input_dimensions));
	composer->constants = TakeArray(composer->storage, bytes, &cursor,
		analytic_counts->constants, sizeof(*composer->constants));
	composer->affines = TakeArray(composer->storage, bytes, &cursor,
		analytic_counts->affines, sizeof(*composer->affines));
	composer->affine_slopes = TakeArray(composer->storage, bytes, &cursor,
		analytic_counts->affine_slopes, sizeof(*composer->affine_slopes));
	composer->polynomials = TakeArray(composer->storage, bytes, &cursor,
		analytic_counts->polynomials, sizeof(*composer->polynomials));
	composer->polynomial_coefficients = TakeArray(composer->storage, bytes,
		&cursor, analytic_counts->polynomial_coefficients,
		sizeof(*composer->polynomial_coefficients));
	composer->ballistics = TakeArray(composer->storage, bytes, &cursor,
		analytic_counts->ballistics, sizeof(*composer->ballistics));
	composer->piecewise = TakeArray(composer->storage, bytes, &cursor,
		analytic_counts->piecewise, sizeof(*composer->piecewise));
	composer->piecewise_clauses = TakeArray(composer->storage, bytes, &cursor,
		analytic_counts->piecewise_clauses,
		sizeof(*composer->piecewise_clauses));
	composer->mechanisms = TakeArray(composer->storage, bytes, &cursor,
		static_data->mechanism_count, sizeof(*composer->mechanisms));
	composer->mechanism_controllers = TakeArray(composer->storage, bytes,
		&cursor, static_data->mechanism_controller_count,
		sizeof(*composer->mechanism_controllers));
	composer->mechanism_edges = TakeArray(composer->storage, bytes, &cursor,
		static_data->mechanism_edge_count,
		sizeof(*composer->mechanism_edges));
	composer->transitions = TakeArray(composer->storage, bytes, &cursor,
		static_data->transition_count, sizeof(*composer->transitions));
	composer->landmarks = TakeArray(composer->storage, bytes, &cursor,
		static_data->landmark_count, sizeof(*composer->landmarks));
	composer->landmark_cells = TakeArray(composer->storage, bytes, &cursor,
		static_data->landmark_cell_count, sizeof(*composer->landmark_cells));
	composer->facet_annotations = TakeArray(composer->storage, bytes, &cursor,
		static_data->facet_annotation_count,
		sizeof(*composer->facet_annotations));
	composer->portal_mechanisms = TakeArray(composer->storage, bytes, &cursor,
		static_data->portal_mechanism_count,
		sizeof(*composer->portal_mechanisms));
	composer->model.cells = composer->cells;
	composer->model.facets = composer->facets;
	composer->model.incidences = composer->incidences;
	composer->model.cell_incidences = composer->cell_incidences;
	composer->model.vertices = composer->vertices;
	composer->model.portals = composer->portals;
	composer->model.source_surfaces = composer->source_surfaces;
	composer->model.source_surface_vertices = composer->source_surface_vertices;
	composer->model.movement_capabilities = composer->movement_capabilities;
	composer->model.movement_states = composer->movement_states;
	composer->model.movement_fibers = composer->movement_fibers;
	composer->model.movement_hook_targets = composer->movement_hook_targets;
	composer->model.movement_fiber_function_refs =
		composer->movement_fiber_function_refs;
	composer->model.movement_angular_schedules =
		composer->movement_angular_schedules;
	composer->model.weapon_profiles = composer->weapon_profiles;
	composer->model.weapon_kernels = composer->weapon_kernels;
	composer->model.weapon_attachments = composer->weapon_attachments;
	composer->model.weapon_relation_spans = composer->weapon_relation_spans;
	composer->model.weapon_relation_refs = composer->weapon_relation_refs;
	composer->model.weapon_function_refs = composer->weapon_function_refs;
	composer->model.mechanism_authorities = composer->mechanism_authorities;
	composer->model.mechanism_authority_controllers =
		composer->mechanism_authority_controllers;
	composer->model.mechanism_authority_topology_edges =
		composer->mechanism_authority_topology_edges;
	composer->model.mechanism_authority_transitions =
		composer->mechanism_authority_transitions;
	composer->model.mechanism_authority_transition_static_indices =
		composer->mechanism_authority_transition_static_indices;
	composer->model.static_transition_authority_indices =
		composer->static_transition_authority_indices;
	composer->model.response.source_fragments = composer->response_fragments;
	composer->model.response.source_halfspaces = composer->response_halfspaces;
	composer->model.response.target_patches = composer->response_patches;
	composer->model.response.target_vertices = composer->response_target_vertices;
	composer->model.response.splits = composer->response_splits;
	composer->model.response.facts = composer->response_facts;
	composer->model.response.candidate_groups = composer->response_candidate_groups;
	composer->model.response.source_endpoint_groups =
		composer->response_source_endpoint_groups;
	composer->model.response.source_endpoint_members =
		composer->response_source_endpoint_members;
	composer->model.response.target_endpoint_groups =
		composer->response_target_endpoint_groups;
	composer->model.response.target_endpoint_members =
		composer->response_target_endpoint_members;
	composer->model.response.occluders = composer->response_occluders;
	composer->analytic.functions = composer->functions;
	composer->analytic.input_dimensions = composer->input_dimensions;
	composer->analytic.constants = composer->constants;
	composer->analytic.affines = composer->affines;
	composer->analytic.affine_slopes = composer->affine_slopes;
	composer->analytic.polynomials = composer->polynomials;
	composer->analytic.polynomial_coefficients = composer->polynomial_coefficients;
	composer->analytic.ballistics = composer->ballistics;
	composer->analytic.piecewise = composer->piecewise;
	composer->analytic.piecewise_clauses = composer->piecewise_clauses;
	composer->static_data.mechanisms = composer->mechanisms;
	composer->static_data.mechanism_controllers =
		composer->mechanism_controllers;
	composer->static_data.mechanism_edges = composer->mechanism_edges;
	composer->static_data.transitions = composer->transitions;
	composer->static_data.landmarks = composer->landmarks;
	composer->static_data.landmark_cells = composer->landmark_cells;
	composer->static_data.facet_annotations = composer->facet_annotations;
	composer->static_data.portal_mechanisms = composer->portal_mechanisms;
	return cursor == bytes ? SG_RUNE_COMPACT_COMPOSER_STEP_OK :
		SG_RUNE_COMPACT_COMPOSER_STEP_OVERFLOW;
}

static void CopyGeometry(const sg_rune_compact_geometry_view_t *geometry,
	sg_rune_compact_composer_t *composer)
{
	if (geometry->cell_count != 0U)
		memcpy(composer->cells, geometry->cells,
			(size_t)geometry->cell_count * sizeof(*composer->cells));
	if (geometry->facet_count != 0U)
		memcpy(composer->facets, geometry->facets,
			(size_t)geometry->facet_count * sizeof(*composer->facets));
	if (geometry->incidence_count != 0U)
		memcpy(composer->incidences, geometry->incidences,
			(size_t)geometry->incidence_count * sizeof(*composer->incidences));
	if (geometry->cell_incidence_count != 0U)
		memcpy(composer->cell_incidences, geometry->cell_incidences,
			(size_t)geometry->cell_incidence_count *
				sizeof(*composer->cell_incidences));
	if (geometry->vertex_count != 0U)
		memcpy(composer->vertices, geometry->vertices,
			(size_t)geometry->vertex_count * sizeof(*composer->vertices));
	if (geometry->portal_count != 0U)
		memcpy(composer->portals, geometry->portals,
			(size_t)geometry->portal_count * sizeof(*composer->portals));
	if (geometry->source_surface_count != 0U)
		memcpy(composer->source_surfaces, geometry->source_surfaces,
			(size_t)geometry->source_surface_count *
				sizeof(*composer->source_surfaces));
	if (geometry->source_surface_vertex_count != 0U)
		memcpy(composer->source_surface_vertices,
			geometry->source_surface_vertices,
			(size_t)geometry->source_surface_vertex_count *
				sizeof(*composer->source_surface_vertices));
}

static void CopyStatic(const sg_rune_compact_static_t *source,
	sg_rune_compact_composer_t *composer)
{
	sg_rune_compact_static_t *target = &composer->static_data;
	target->mechanism_count = source->mechanism_count;
	target->mechanism_controller_count = source->mechanism_controller_count;
	target->mechanism_edge_count = source->mechanism_edge_count;
	target->transition_count = source->transition_count;
	target->landmark_count = source->landmark_count;
	target->landmark_cell_count = source->landmark_cell_count;
	target->facet_annotation_count = source->facet_annotation_count;
	target->portal_mechanism_count = source->portal_mechanism_count;
	if (source->mechanism_count != 0U)
		memcpy(composer->mechanisms, source->mechanisms,
			(size_t)source->mechanism_count * sizeof(*composer->mechanisms));
	if (source->mechanism_controller_count != 0U)
		memcpy(composer->mechanism_controllers, source->mechanism_controllers,
			(size_t)source->mechanism_controller_count *
				sizeof(*composer->mechanism_controllers));
	if (source->mechanism_edge_count != 0U)
		memcpy(composer->mechanism_edges, source->mechanism_edges,
			(size_t)source->mechanism_edge_count *
				sizeof(*composer->mechanism_edges));
	if (source->transition_count != 0U)
		memcpy(composer->transitions, source->transitions,
			(size_t)source->transition_count *
				sizeof(*composer->transitions));
	if (source->landmark_count != 0U)
		memcpy(composer->landmarks, source->landmarks,
			(size_t)source->landmark_count * sizeof(*composer->landmarks));
	if (source->landmark_cell_count != 0U)
		memcpy(composer->landmark_cells, source->landmark_cells,
			(size_t)source->landmark_cell_count *
				sizeof(*composer->landmark_cells));
	if (source->facet_annotation_count != 0U)
		memcpy(composer->facet_annotations, source->facet_annotations,
			(size_t)source->facet_annotation_count *
				sizeof(*composer->facet_annotations));
	if (source->portal_mechanism_count != 0U)
		memcpy(composer->portal_mechanisms, source->portal_mechanisms,
			(size_t)source->portal_mechanism_count *
				sizeof(*composer->portal_mechanisms));
}

static void CopyMechanismAuthorities(
	const sg_rune_compact_mechanisms_view_t *source,
	sg_rune_compact_composer_t *composer)
{
	if (source->mechanism_count != 0U)
		memcpy(composer->mechanism_authorities, source->mechanisms,
			(size_t)source->mechanism_count *
				sizeof(*composer->mechanism_authorities));
	if (source->controller_count != 0U)
		memcpy(composer->mechanism_authority_controllers, source->controllers,
			(size_t)source->controller_count *
				sizeof(*composer->mechanism_authority_controllers));
	if (source->topology_edge_count != 0U)
		memcpy(composer->mechanism_authority_topology_edges,
			source->topology_edges, (size_t)source->topology_edge_count *
				sizeof(*composer->mechanism_authority_topology_edges));
	if (source->transition_count != 0U)
		memcpy(composer->mechanism_authority_transitions, source->transitions,
			(size_t)source->transition_count *
				sizeof(*composer->mechanism_authority_transitions));
}

static int CopyTransitionProjection(
	const sg_rune_compact_static_materializer_t *static_materializer,
	uint32_t transition_count, sg_rune_compact_composer_t *composer)
{
	uint32_t authority_index;

	for (authority_index = 0U; authority_index < transition_count;
		authority_index++)
		composer->static_transition_authority_indices[authority_index] =
			SG_RUNE_COMPACT_INDEX_NONE;
	for (authority_index = 0U; authority_index < transition_count;
		authority_index++) {
		uint32_t static_index;

		if (!SG_RuneCompactStaticMaterializerAuthorityTransitionStaticIndex(
			static_materializer, authority_index, &static_index) ||
			static_index >= transition_count ||
			composer->static_transition_authority_indices[static_index] !=
				SG_RUNE_COMPACT_INDEX_NONE)
			return 0;
		composer->mechanism_authority_transition_static_indices[
			authority_index] = static_index;
		composer->static_transition_authority_indices[static_index] =
			authority_index;
	}
	return 1;
}

static int CopyMovement(const sg_rune_compact_movement_fields_view_t *movement,
	const uint32_t *function_map, sg_rune_compact_composer_t *composer)
{
	uint32_t index;

	if (movement == NULL || function_map == NULL || composer == NULL)
		return 0;
	if (movement->capability_count != 0U)
		memcpy(composer->movement_capabilities, movement->capabilities,
			(size_t)movement->capability_count *
				sizeof(*composer->movement_capabilities));
	if (movement->state_count != 0U)
		memcpy(composer->movement_states, movement->states,
			(size_t)movement->state_count * sizeof(*composer->movement_states));
	if (movement->fiber_count != 0U)
		memcpy(composer->movement_fibers, movement->fibers,
			(size_t)movement->fiber_count * sizeof(*composer->movement_fibers));
	if (movement->hook_target_count != 0U)
		memcpy(composer->movement_hook_targets, movement->hook_targets,
			(size_t)movement->hook_target_count *
				sizeof(*composer->movement_hook_targets));
	if (movement->angular_schedule_count != 0U)
		memcpy(composer->movement_angular_schedules,
			movement->angular_schedules,
			(size_t)movement->angular_schedule_count *
				sizeof(*composer->movement_angular_schedules));

	for (index = 0U; index < movement->fiber_function_ref_count; index++) {
		const uint32_t source_function =
			movement->fiber_function_refs[index].value;

		if (source_function >= movement->analytic.function_count ||
			function_map[source_function] == UINT32_MAX)
			return 0;
		composer->movement_fiber_function_refs[index].value =
			function_map[source_function];
	}
	return 1;
}

static int CopyWeapon(const sg_rune_compact_weapon_field_view_t *weapon,
	const sg_rune_compact_builder_view_t *builder, const uint32_t *function_map,
	sg_rune_compact_composer_t *composer)
{
	uint32_t index;

	if (weapon == NULL || builder == NULL || function_map == NULL ||
		composer == NULL)
		return 0;
	if (builder->weapon_profile_count != 0U)
		memcpy(composer->weapon_profiles, builder->weapon_profiles,
			(size_t)builder->weapon_profile_count *
				sizeof(*composer->weapon_profiles));
	if (weapon->kernel_count != 0U)
		memcpy(composer->weapon_kernels, weapon->kernels,
			(size_t)weapon->kernel_count * sizeof(*composer->weapon_kernels));
	if (weapon->attachment_count != 0U)
		memcpy(composer->weapon_attachments, weapon->attachments,
			(size_t)weapon->attachment_count *
				sizeof(*composer->weapon_attachments));
	if (weapon->relation_span_count != 0U)
		memcpy(composer->weapon_relation_spans, weapon->relation_spans,
			(size_t)weapon->relation_span_count *
				sizeof(*composer->weapon_relation_spans));
	if (weapon->relation_ref_count != 0U)
		memcpy(composer->weapon_relation_refs, weapon->relation_refs,
			(size_t)weapon->relation_ref_count *
				sizeof(*composer->weapon_relation_refs));
	for (index = 0U; index < weapon->weapon_function_ref_count; index++) {
		const sg_rune_weapon_function_ref_t *source =
			&weapon->weapon_function_refs[index];
		sg_rune_weapon_function_ref_t *target =
			&composer->weapon_function_refs[index];

		if (source->function.value >= weapon->analytic.function_count ||
			function_map[source->function.value] == UINT32_MAX)
			return 0;
		*target = *source;
		target->function.value = function_map[source->function.value];
	}
	return 1;
}

static int CopyResponse(
	const sg_rune_compact_weapon_relations_view_t *relations,
	sg_rune_compact_composer_t *composer)
{
	const sg_rune_compact_response_projection_t *source;
	sg_rune_compact_response_projection_t *target;

	if (relations == NULL || composer == NULL)
		return 0;
	source = &relations->response;
	target = &composer->model.response;
	if (source->source_fragment_count != 0U)
		memcpy(composer->response_fragments, source->source_fragments,
			(size_t)source->source_fragment_count *
				sizeof(*composer->response_fragments));
	if (source->source_halfspace_count != 0U)
		memcpy(composer->response_halfspaces, source->source_halfspaces,
			(size_t)source->source_halfspace_count *
				sizeof(*composer->response_halfspaces));
	if (source->target_patch_count != 0U)
		memcpy(composer->response_patches, source->target_patches,
			(size_t)source->target_patch_count *
				sizeof(*composer->response_patches));
	if (source->target_vertex_count != 0U)
		memcpy(composer->response_target_vertices, source->target_vertices,
			(size_t)source->target_vertex_count *
				sizeof(*composer->response_target_vertices));
	if (source->split_count != 0U)
		memcpy(composer->response_splits, source->splits,
			(size_t)source->split_count * sizeof(*composer->response_splits));
	if (source->fact_count != 0U)
		memcpy(composer->response_facts, source->facts,
			(size_t)source->fact_count * sizeof(*composer->response_facts));
	if (source->candidate_group_count != 0U)
		memcpy(composer->response_candidate_groups, source->candidate_groups,
			(size_t)source->candidate_group_count *
				sizeof(*composer->response_candidate_groups));
	if (source->source_endpoint_group_count != 0U)
		memcpy(composer->response_source_endpoint_groups,
			source->source_endpoint_groups,
			(size_t)source->source_endpoint_group_count *
				sizeof(*composer->response_source_endpoint_groups));
	if (source->source_endpoint_member_count != 0U)
		memcpy(composer->response_source_endpoint_members,
			source->source_endpoint_members,
			(size_t)source->source_endpoint_member_count *
				sizeof(*composer->response_source_endpoint_members));
	if (source->target_endpoint_group_count != 0U)
		memcpy(composer->response_target_endpoint_groups,
			source->target_endpoint_groups,
			(size_t)source->target_endpoint_group_count *
				sizeof(*composer->response_target_endpoint_groups));
	if (source->target_endpoint_member_count != 0U)
		memcpy(composer->response_target_endpoint_members,
			source->target_endpoint_members,
			(size_t)source->target_endpoint_member_count *
				sizeof(*composer->response_target_endpoint_members));
	if (source->occluder_count != 0U)
		memcpy(composer->response_occluders, source->occluders,
			(size_t)source->occluder_count *
				sizeof(*composer->response_occluders));
	target->source_fragments = composer->response_fragments;
	target->source_fragment_count = source->source_fragment_count;
	target->source_halfspaces = composer->response_halfspaces;
	target->source_halfspace_count = source->source_halfspace_count;
	target->target_patches = composer->response_patches;
	target->target_patch_count = source->target_patch_count;
	target->target_vertices = composer->response_target_vertices;
	target->target_vertex_count = source->target_vertex_count;
	target->splits = composer->response_splits;
	target->split_count = source->split_count;
	target->facts = composer->response_facts;
	target->fact_count = source->fact_count;
	target->candidate_groups = composer->response_candidate_groups;
	target->candidate_group_count = source->candidate_group_count;
	target->source_endpoint_groups = composer->response_source_endpoint_groups;
	target->source_endpoint_group_count = source->source_endpoint_group_count;
	target->source_endpoint_members = composer->response_source_endpoint_members;
	target->source_endpoint_member_count = source->source_endpoint_member_count;
	target->target_endpoint_groups = composer->response_target_endpoint_groups;
	target->target_endpoint_group_count = source->target_endpoint_group_count;
	target->target_endpoint_members = composer->response_target_endpoint_members;
	target->target_endpoint_member_count = source->target_endpoint_member_count;
	target->occluders = composer->response_occluders;
	target->occluder_count = source->occluder_count;
	target->seal = source->seal;
	target->exact_live_prefire_trace_required =
		source->exact_live_prefire_trace_required;
	target->reserved[0] = source->reserved[0];
	target->reserved[1] = source->reserved[1];
	target->reserved[2] = source->reserved[2];
	return 1;
}

static int BindCellSpans(sg_rune_compact_composer_t *composer)
{
	sg_rune_compact_model_t *model = &composer->model;
	uint32_t cell_index;
	uint32_t movement_cursor = 0U;

	for (cell_index = 0U; cell_index < model->cell_count; cell_index++) {
		sg_rune_compact_cell_t *cell = &composer->cells[cell_index];

		cell->movement_fields.first = movement_cursor;
		while (movement_cursor < model->movement_capability_count &&
			model->movement_capabilities[movement_cursor].cell.value == cell_index)
			movement_cursor++;
		cell->movement_fields.count = movement_cursor -
			cell->movement_fields.first;
	}
	return movement_cursor == model->movement_capability_count;
}

static int StaticArraysPresent(const sg_rune_compact_static_t *static_data)
{
	return static_data != NULL &&
		ArrayPresent(static_data->mechanisms, static_data->mechanism_count) &&
		ArrayPresent(static_data->mechanism_controllers,
			static_data->mechanism_controller_count) &&
		ArrayPresent(static_data->mechanism_edges,
			static_data->mechanism_edge_count) &&
		ArrayPresent(static_data->transitions, static_data->transition_count) &&
		ArrayPresent(static_data->landmarks, static_data->landmark_count) &&
		ArrayPresent(static_data->landmark_cells,
			static_data->landmark_cell_count) &&
		ArrayPresent(static_data->facet_annotations,
			static_data->facet_annotation_count) &&
		ArrayPresent(static_data->portal_mechanisms,
			static_data->portal_mechanism_count);
}

static int GeometryArraysPresent(const sg_rune_compact_geometry_view_t *geometry)
{
	return geometry != NULL && ArrayPresent(geometry->cells, geometry->cell_count) &&
		ArrayPresent(geometry->facets, geometry->facet_count) &&
		ArrayPresent(geometry->incidences, geometry->incidence_count) &&
		ArrayPresent(geometry->cell_incidences,
			geometry->cell_incidence_count) &&
		ArrayPresent(geometry->vertices, geometry->vertex_count) &&
		ArrayPresent(geometry->portals, geometry->portal_count) &&
		ArrayPresent(geometry->source_surfaces,
			geometry->source_surface_count) &&
		ArrayPresent(geometry->source_surface_vertices,
			geometry->source_surface_vertex_count);
}

static int GeometryCountsWithinLimits(
	const sg_rune_compact_geometry_view_t *geometry)
{
	return geometry != NULL &&
		geometry->cell_count <= SG_RUNE_COMPACT_MAX_CELLS &&
		geometry->facet_count <= SG_RUNE_COMPACT_MAX_FACETS &&
		geometry->incidence_count <= SG_RUNE_COMPACT_MAX_INCIDENCES &&
		geometry->cell_incidence_count <= SG_RUNE_COMPACT_MAX_INCIDENCES &&
		geometry->vertex_count <= SG_RUNE_COMPACT_MAX_VERTICES &&
		geometry->portal_count <= SG_RUNE_COMPACT_MAX_PORTALS &&
		geometry->source_surface_count <=
			SG_RUNE_COMPACT_MAX_SOURCE_SURFACES &&
		geometry->source_surface_vertex_count <=
			SG_RUNE_COMPACT_MAX_SOURCE_SURFACE_VERTICES;
}

static int StaticCountsWithinLimits(const sg_rune_compact_static_t *static_data)
{
	return static_data != NULL &&
		static_data->mechanism_count <= SG_RUNE_COMPACT_MAX_MECHANISMS &&
		static_data->mechanism_controller_count <=
			SG_RUNE_COMPACT_MAX_MECHANISM_CONTROLLERS &&
		static_data->mechanism_edge_count <=
			SG_RUNE_COMPACT_MAX_MECHANISM_EDGES &&
		static_data->transition_count <=
			SG_RUNE_COMPACT_MAX_MECHANISM_TRANSITIONS &&
		static_data->landmark_count <= SG_RUNE_COMPACT_MAX_LANDMARKS &&
		static_data->landmark_cell_count <=
			SG_RUNE_COMPACT_MAX_LANDMARK_CELL_REFS &&
		static_data->facet_annotation_count <=
			SG_RUNE_COMPACT_MAX_FACET_ANNOTATIONS &&
		static_data->portal_mechanism_count <=
			SG_RUNE_COMPACT_MAX_PORTAL_MECHANISMS;
}

static int ResponseProjectionCountsWithinLimits(
	const sg_rune_compact_response_projection_t *response)
{
	return response != NULL &&
		response->source_fragment_count <= SG_RUNE_COMPACT_MAX_RESPONSE_FRAGMENTS &&
		response->source_halfspace_count <= SG_RUNE_COMPACT_MAX_RESPONSE_HALFSPACES &&
		response->target_patch_count <= SG_RUNE_COMPACT_MAX_RESPONSE_PATCHES &&
		response->target_vertex_count <=
			SG_RUNE_COMPACT_MAX_RESPONSE_PATCH_VERTICES &&
		response->split_count <= SG_RUNE_COMPACT_MAX_RESPONSE_SPLITS &&
		response->fact_count <= SG_RUNE_COMPACT_MAX_RESPONSE_FACTS &&
		response->candidate_group_count <=
			SG_RUNE_COMPACT_MAX_RESPONSE_CANDIDATE_GROUPS &&
		response->source_endpoint_group_count <=
			SG_RUNE_COMPACT_MAX_RESPONSE_ENDPOINT_GROUPS &&
		response->target_endpoint_group_count <=
			SG_RUNE_COMPACT_MAX_RESPONSE_ENDPOINT_GROUPS &&
		response->source_endpoint_member_count <=
			SG_RUNE_COMPACT_MAX_RESPONSE_ENDPOINT_MEMBERS &&
		response->target_endpoint_member_count <=
			SG_RUNE_COMPACT_MAX_RESPONSE_ENDPOINT_MEMBERS &&
		response->occluder_count <= SG_RUNE_COMPACT_MAX_STATIC_OCCLUDERS;
}

static int ResponseProjectionArraysPresent(
	const sg_rune_compact_response_projection_t *response)
{
	return response != NULL &&
		ArrayPresent(response->source_fragments, response->source_fragment_count) &&
		ArrayPresent(response->source_halfspaces, response->source_halfspace_count) &&
		ArrayPresent(response->target_patches, response->target_patch_count) &&
		ArrayPresent(response->target_vertices, response->target_vertex_count) &&
		ArrayPresent(response->splits, response->split_count) &&
		ArrayPresent(response->facts, response->fact_count) &&
		ArrayPresent(response->candidate_groups,
			response->candidate_group_count) &&
		ArrayPresent(response->source_endpoint_groups,
			response->source_endpoint_group_count) &&
		ArrayPresent(response->source_endpoint_members,
			response->source_endpoint_member_count) &&
		ArrayPresent(response->target_endpoint_groups,
			response->target_endpoint_group_count) &&
		ArrayPresent(response->target_endpoint_members,
			response->target_endpoint_member_count) &&
		ArrayPresent(response->occluders, response->occluder_count);
}

static int ResponseProjectionSealShapeValid(
	const sg_rune_compact_response_projection_t *response)
{
	return response != NULL && response->exact_live_prefire_trace_required == 1U &&
		response->reserved[0] == 0U && response->reserved[1] == 0U &&
		response->reserved[2] == 0U &&
		response->seal.version == SG_RUNE_COMPACT_RESPONSE_PARTITION_VERSION &&
		response->seal.reserved == 0U &&
		(response->seal.flags & SG_RUNE_COMPACT_RESPONSE_SEAL_REQUIRED) ==
			SG_RUNE_COMPACT_RESPONSE_SEAL_REQUIRED &&
		(response->seal.flags &
			~(sg_rune_compact_response_seal_flags_t)
				SG_RUNE_COMPACT_RESPONSE_SEAL_KNOWN) == 0U &&
		response->seal.split_frontier_count == 0U &&
		response->seal.source_fragment_count == response->source_fragment_count &&
		response->seal.target_patch_count == response->target_patch_count &&
		response->seal.split_count == response->split_count &&
		response->seal.response_pair_count == response->fact_count &&
		response->seal.unresolved_response_pair_count == 0U &&
		response->seal.unresolved_candidate_group_count ==
			response->candidate_group_count &&
		response->seal.source_endpoint_group_count ==
			response->source_endpoint_group_count &&
		response->seal.target_endpoint_group_count ==
			response->target_endpoint_group_count &&
		response->seal.source_endpoint_member_count ==
			response->source_endpoint_member_count &&
		response->seal.target_endpoint_member_count ==
			response->target_endpoint_member_count &&
		response->seal.static_occluder_count == response->occluder_count;
}

static int FragmentCountsWithinLimits(
	const sg_rune_compact_movement_fields_view_t *movement,
	const sg_rune_compact_weapon_field_view_t *weapon,
	const sg_rune_compact_builder_view_t *builder,
	const sg_rune_compact_mechanisms_view_t *mechanisms,
	const sg_rune_compact_response_projection_t *response)
{
	return movement != NULL && weapon != NULL && builder != NULL &&
		mechanisms != NULL &&
		response != NULL &&
		movement->capability_count <= SG_RUNE_COMPACT_MAX_MOVEMENT_FIELDS &&
		movement->state_count <= SG_RUNE_COMPACT_MAX_MOVEMENT_STATES &&
		movement->fiber_count <= SG_RUNE_COMPACT_MAX_MOVEMENT_FIBERS &&
		movement->hook_target_count <=
			SG_RUNE_COMPACT_MAX_MOVEMENT_HOOK_TARGETS &&
		movement->fiber_function_ref_count <=
			SG_RUNE_COMPACT_MAX_MOVEMENT_FIBER_FUNCTION_REFS &&
		movement->angular_schedule_count <=
			SG_RUNE_COMPACT_MAX_MOVEMENT_ANGULAR_SCHEDULES &&
		weapon->kernel_count <= SG_RUNE_COMPACT_MAX_WEAPON_KERNELS &&
		weapon->attachment_count <= SG_RUNE_COMPACT_MAX_WEAPON_ATTACHMENTS &&
		weapon->relation_span_count <=
			SG_RUNE_COMPACT_MAX_WEAPON_RELATION_SPANS &&
		weapon->relation_ref_count <=
			SG_RUNE_COMPACT_MAX_WEAPON_RELATION_REFS &&
		weapon->weapon_function_ref_count <=
			SG_RUNE_COMPACT_MAX_WEAPON_FUNCTION_REFS &&
		builder->weapon_profile_count <=
			SG_RUNE_COMPACT_MAX_WEAPON_PROFILES &&
		mechanisms->mechanism_count <=
			SG_RUNE_COMPACT_MAX_MECHANISM_AUTHORITIES &&
		mechanisms->controller_count <=
			SG_RUNE_COMPACT_MAX_MECHANISM_AUTHORITY_CONTROLLERS &&
		mechanisms->topology_edge_count <=
			SG_RUNE_COMPACT_MAX_MECHANISM_AUTHORITY_TOPOLOGY_EDGES &&
		mechanisms->transition_count <=
			SG_RUNE_COMPACT_MAX_MECHANISM_AUTHORITY_TRANSITIONS &&
		ResponseProjectionCountsWithinLimits(response);
}

static void InitializeModel(sg_rune_compact_composer_t *composer,
	const sg_rune_compact_builder_view_t *builder,
	const sg_rune_compact_geometry_view_t *geometry,
	const sg_rune_compact_movement_fields_view_t *movement,
	const sg_rune_compact_weapon_field_view_t *weapon,
	const sg_rune_compact_mechanisms_view_t *mechanisms)
{
	sg_rune_compact_model_t *model = &composer->model;

	model->version = SG_RUNE_COMPACT_MODEL_VERSION;
	model->schema_tag = SG_RUNE_COMPACT_MODEL_SCHEMA_TAG;
	model->identity = builder->identity;
	model->cell_count = geometry->cell_count;
	model->facet_count = geometry->facet_count;
	model->incidence_count = geometry->incidence_count;
	model->cell_incidence_count = geometry->cell_incidence_count;
	model->vertex_count = geometry->vertex_count;
	model->portal_count = geometry->portal_count;
	model->source_surface_count = geometry->source_surface_count;
	model->source_surface_vertex_count =
		geometry->source_surface_vertex_count;
	model->movement_capability_count = movement->capability_count;
	model->movement_state_count = movement->state_count;
	model->movement_fiber_count = movement->fiber_count;
	model->movement_hook_target_count = movement->hook_target_count;
	model->movement_fiber_function_ref_count =
		movement->fiber_function_ref_count;
	model->movement_angular_schedule_count = movement->angular_schedule_count;
	model->movement_pmove_abi = movement->pmove_abi;
	model->movement_pmove_behavior_fingerprint =
		movement->pmove_behavior_fingerprint;
	model->movement_host_level_generation = movement->host_level_generation;
	model->movement_physics_abi_id = movement->physics_abi_id;
	model->movement_collision_law_id = movement->collision_law_id;
	model->movement_pmove_law_id = movement->pmove_law_id;
	model->movement_gravity_law_id = movement->gravity_law_id;
	model->movement_hook_law_id = movement->hook_law_id;
	model->movement_mechanism_law_id = movement->mechanism_law_id;
	model->weapon_profile_count = builder->weapon_profile_count;
	model->weapon_kernel_count = weapon->kernel_count;
	model->weapon_attachment_count = weapon->attachment_count;
	model->weapon_relation_span_count = weapon->relation_span_count;
	model->weapon_relation_ref_count = weapon->relation_ref_count;
	model->weapon_function_ref_count = weapon->weapon_function_ref_count;
	model->mechanism_authority_count = mechanisms->mechanism_count;
	model->mechanism_authority_controller_count = mechanisms->controller_count;
	model->mechanism_authority_topology_edge_count =
		mechanisms->topology_edge_count;
	model->mechanism_authority_transition_count = mechanisms->transition_count;
	model->analytic = &composer->analytic;
	model->static_data = &composer->static_data;
}

void SG_RuneCompactComposerDestroy(sg_rune_compact_composer_t *composer)
{
	if (composer == NULL)
		return;
	free(composer->storage);
	free(composer);
}

int SG_RuneCompactComposerBuild(
	const sg_rune_compact_builder_t *builder,
	const sg_rune_compact_geometry_t *geometry,
	const sg_rune_compact_mechanisms_t *mechanisms,
	const sg_rune_compact_static_materializer_t *static_materializer,
	const sg_rune_compact_movement_fields_t *movement_fields,
	const sg_rune_compact_weapon_relations_t *relations,
	const sg_rune_compact_weapon_field_t *weapon_field,
	sg_rune_compact_composer_t **composer_out,
	sg_rune_compact_composer_error_t *error_out)
{
	sg_rune_compact_builder_view_t builder_view;
	sg_rune_compact_geometry_view_t geometry_view;
	sg_rune_compact_mechanisms_view_t mechanisms_view;
	sg_rune_compact_static_t static_data;
	sg_rune_compact_movement_fields_view_t movement_view;
	sg_rune_compact_weapon_relations_view_t relations_view;
	sg_rune_compact_weapon_field_view_t weapon_view;
	sg_rune_compact_weapon_relations_view_t final_relations_view;
	sg_rune_compact_identity_t static_identity;
	sg_rune_compact_identity_t movement_identity;
	sg_rune_compact_composer_counts_t analytic_counts;
	sg_rune_compact_composer_t *candidate = NULL;
	sg_rune_compact_error_t model_error;
	sg_rune_compact_composer_step_result_t step_result;

	if (composer_out != NULL)
		*composer_out = NULL;
	SetError(error_out, SG_RUNE_COMPACT_COMPOSER_ERROR_NONE,
		SG_RUNE_COMPACT_COMPOSER_RECORD_MODEL, 0U);
	if (builder == NULL || geometry == NULL || mechanisms == NULL ||
		static_materializer == NULL ||
		movement_fields == NULL || relations == NULL || weapon_field == NULL ||
		composer_out == NULL) {
		SetError(error_out, SG_RUNE_COMPACT_COMPOSER_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_COMPOSER_RECORD_MODEL, 0U);
		return 0;
	}
	memset(&builder_view, 0, sizeof(builder_view));
	memset(&geometry_view, 0, sizeof(geometry_view));
	memset(&mechanisms_view, 0, sizeof(mechanisms_view));
	memset(&static_data, 0, sizeof(static_data));
	memset(&movement_view, 0, sizeof(movement_view));
	memset(&relations_view, 0, sizeof(relations_view));
	memset(&weapon_view, 0, sizeof(weapon_view));
	memset(&final_relations_view, 0, sizeof(final_relations_view));
	memset(&static_identity, 0, sizeof(static_identity));
	memset(&movement_identity, 0, sizeof(movement_identity));
	if (!SG_RuneCompactBuilderRead(builder, &builder_view) ||
		!SG_RuneCompactGeometryRead(geometry, &geometry_view) ||
		!SG_RuneCompactMechanismsRead(mechanisms, &mechanisms_view) ||
		!SG_RuneCompactStaticMaterializerReadBound(static_materializer,
			&static_identity, &static_data) ||
		!SG_RuneCompactMovementFieldsReadBound(movement_fields,
			&movement_identity, &movement_view) ||
		!SG_RuneCompactWeaponRelationsRead(relations, &relations_view) ||
		!SG_RuneCompactWeaponFieldReadBound(weapon_field, &weapon_view)) {
		SetError(error_out, SG_RUNE_COMPACT_COMPOSER_ERROR_OWNER_READ,
			SG_RUNE_COMPACT_COMPOSER_RECORD_MODEL, 0U);
		return 0;
	}
	if (!SG_RuneCompactIdentityMatches(&builder_view.identity,
		&geometry_view.identity) || !SG_RuneCompactIdentityMatches(
		&builder_view.identity, &mechanisms_view.identity) ||
		!SG_RuneCompactIdentityMatches(
		&builder_view.identity, &static_identity) ||
		!SG_RuneCompactIdentityMatches(&builder_view.identity,
			&movement_identity) || !SG_RuneCompactIdentityMatches(
				&builder_view.identity, &relations_view.identity) ||
			!SG_RuneCompactIdentityMatches(&builder_view.identity,
				&weapon_view.identity)) {
		SetError(error_out, SG_RUNE_COMPACT_COMPOSER_ERROR_IDENTITY_MISMATCH,
			SG_RUNE_COMPACT_COMPOSER_RECORD_IDENTITY, 0U);
		return 0;
	}
	if (relations_view.version != SG_RUNE_COMPACT_WEAPON_RELATIONS_VERSION ||
		relations_view.reserved != 0U ||
		movement_view.pmove_abi.identity != builder_view.identity.physics_abi_id ||
		movement_view.physics_abi_id != builder_view.identity.physics_abi_id ||
		movement_view.collision_law_id != builder_view.identity.collision_law_id ||
		movement_view.pmove_law_id != builder_view.identity.pmove_law_id ||
		movement_view.gravity_law_id != builder_view.identity.gravity_law_id ||
		movement_view.hook_law_id != builder_view.identity.hook_law_id ||
		movement_view.mechanism_law_id !=
			builder_view.identity.mechanism_law_id) {
		SetError(error_out, SG_RUNE_COMPACT_COMPOSER_ERROR_INVALID_FRAGMENT,
			SG_RUNE_COMPACT_COMPOSER_RECORD_MODEL, 0U);
		return 0;
	}
	if (!GeometryCountsWithinLimits(&geometry_view) ||
		!StaticCountsWithinLimits(&static_data) ||
		!FragmentCountsWithinLimits(&movement_view, &weapon_view,
			&builder_view, &mechanisms_view, &relations_view.response)) {
		SetError(error_out, SG_RUNE_COMPACT_COMPOSER_ERROR_LIMIT_EXCEEDED,
			SG_RUNE_COMPACT_COMPOSER_RECORD_MODEL, 0U);
		return 0;
	}
	if (!GeometryArraysPresent(&geometry_view) ||
		!ResponseProjectionArraysPresent(&relations_view.response) ||
		!ResponseProjectionSealShapeValid(&relations_view.response) ||
		!FragmentAnalyticsValid(&movement_view, &weapon_view) ||
		!FragmentReferencesValid(&movement_view, &weapon_view,
			&relations_view.response, &static_data, &mechanisms_view,
			geometry_view.cell_count) ||
		!ArrayPresent(mechanisms_view.mechanisms,
			mechanisms_view.mechanism_count) ||
		!ArrayPresent(mechanisms_view.controllers,
			mechanisms_view.controller_count) ||
		!ArrayPresent(mechanisms_view.topology_edges,
			mechanisms_view.topology_edge_count) ||
		!ArrayPresent(mechanisms_view.transitions,
			mechanisms_view.transition_count) ||
		!StaticArraysPresent(&static_data) ||
		!ArrayPresent(builder_view.weapon_profiles,
			builder_view.weapon_profile_count)) {
		SetError(error_out, SG_RUNE_COMPACT_COMPOSER_ERROR_INVALID_FRAGMENT,
			SG_RUNE_COMPACT_COMPOSER_RECORD_MODEL, 0U);
		return 0;
	}
	memset(&analytic_counts, 0, sizeof(analytic_counts));
	if (!AnalyticCountsAdd(&analytic_counts, &movement_view.analytic) ||
		!AnalyticCountsAdd(&analytic_counts, &weapon_view.analytic)) {
		SetError(error_out, SG_RUNE_COMPACT_COMPOSER_ERROR_OVERFLOW,
			SG_RUNE_COMPACT_COMPOSER_RECORD_ANALYTIC, 0U);
		return 0;
	}
	candidate = calloc(1U, sizeof(*candidate));
	if (candidate == NULL) {
		SetError(error_out, SG_RUNE_COMPACT_COMPOSER_ERROR_OUT_OF_MEMORY,
			SG_RUNE_COMPACT_COMPOSER_RECORD_ALLOCATION, 0U);
		return 0;
	}
	step_result = AllocateModelStorage(candidate, &geometry_view, &static_data,
		&mechanisms_view, &movement_view, &weapon_view,
		&relations_view.response, &builder_view,
		&analytic_counts);
	if (step_result != SG_RUNE_COMPACT_COMPOSER_STEP_OK) {
		SetError(error_out, step_result ==
			SG_RUNE_COMPACT_COMPOSER_STEP_OUT_OF_MEMORY ?
			SG_RUNE_COMPACT_COMPOSER_ERROR_OUT_OF_MEMORY :
			SG_RUNE_COMPACT_COMPOSER_ERROR_OVERFLOW,
			SG_RUNE_COMPACT_COMPOSER_RECORD_ALLOCATION, 0U);
		goto fail;
	}
	InitializeModel(candidate, &builder_view, &geometry_view, &movement_view,
		&weapon_view, &mechanisms_view);
	CopyGeometry(&geometry_view, candidate);
	CopyStatic(&static_data, candidate);
	CopyMechanismAuthorities(&mechanisms_view, candidate);
	if (!CopyTransitionProjection(static_materializer,
		mechanisms_view.transition_count, candidate)) {
		SetError(error_out, SG_RUNE_COMPACT_COMPOSER_ERROR_INVALID_FRAGMENT,
			SG_RUNE_COMPACT_COMPOSER_RECORD_MODEL, 0U);
		goto fail;
	}
	if (!CopyResponse(&relations_view, candidate)) {
		SetError(error_out, SG_RUNE_COMPACT_COMPOSER_ERROR_INVALID_FRAGMENT,
			SG_RUNE_COMPACT_COMPOSER_RECORD_WEAPON, 0U);
		goto fail;
	}
	step_result = MergeAnalytics(&movement_view, &weapon_view, &builder_view,
		candidate, &analytic_counts);
	if (step_result != SG_RUNE_COMPACT_COMPOSER_STEP_OK) {
		SetError(error_out, step_result ==
			SG_RUNE_COMPACT_COMPOSER_STEP_OUT_OF_MEMORY ?
			SG_RUNE_COMPACT_COMPOSER_ERROR_OUT_OF_MEMORY : step_result ==
			SG_RUNE_COMPACT_COMPOSER_STEP_LIMIT ?
			SG_RUNE_COMPACT_COMPOSER_ERROR_LIMIT_EXCEEDED : step_result ==
			SG_RUNE_COMPACT_COMPOSER_STEP_OVERFLOW ?
			SG_RUNE_COMPACT_COMPOSER_ERROR_OVERFLOW :
			SG_RUNE_COMPACT_COMPOSER_ERROR_INVALID_FRAGMENT,
			SG_RUNE_COMPACT_COMPOSER_RECORD_ANALYTIC, 0U);
		goto fail;
	}
	if (!SG_RuneCompactModelValidateBound(&candidate->model,
		&builder_view.identity, &model_error)) {
		SetError(error_out,
			model_error.code == SG_RUNE_COMPACT_ERROR_OUT_OF_MEMORY ?
			SG_RUNE_COMPACT_COMPOSER_ERROR_OUT_OF_MEMORY :
			SG_RUNE_COMPACT_COMPOSER_ERROR_MODEL_REJECTED,
			SG_RUNE_COMPACT_COMPOSER_RECORD_MODEL, model_error.record);
		goto fail;
	}
	if (!SG_RuneCompactWeaponRelationsRead(relations, &final_relations_view)) {
		SetError(error_out, SG_RUNE_COMPACT_COMPOSER_ERROR_OWNER_READ,
			SG_RUNE_COMPACT_COMPOSER_RECORD_WEAPON, 0U);
		goto fail;
	}
	if (final_relations_view.version !=
			SG_RUNE_COMPACT_WEAPON_RELATIONS_VERSION ||
		final_relations_view.reserved != 0U ||
		!SG_RuneCompactIdentityMatches(&builder_view.identity,
			&final_relations_view.identity) ||
		!ResponseProjectionStableEqual(&relations_view.response,
			&final_relations_view.response)) {
		SetError(error_out, SG_RUNE_COMPACT_COMPOSER_ERROR_IDENTITY_MISMATCH,
			SG_RUNE_COMPACT_COMPOSER_RECORD_IDENTITY, 0U);
		goto fail;
	}
	*composer_out = candidate;
	return 1;

fail:
	SG_RuneCompactComposerDestroy(candidate);
	return 0;
}

const sg_rune_compact_model_t *SG_RuneCompactComposerModel(
	const sg_rune_compact_composer_t *composer)
{
	return composer != NULL ? &composer->model : NULL;
}

const char *SG_RuneCompactComposerErrorString(
	sg_rune_compact_composer_error_code_t code)
{
	switch (code) {
	case SG_RUNE_COMPACT_COMPOSER_ERROR_NONE:
		return "none";
	case SG_RUNE_COMPACT_COMPOSER_ERROR_INVALID_ARGUMENT:
		return "invalid argument";
	case SG_RUNE_COMPACT_COMPOSER_ERROR_OWNER_READ:
		return "fragment owner read failed";
	case SG_RUNE_COMPACT_COMPOSER_ERROR_IDENTITY_MISMATCH:
		return "fragment identity mismatch";
	case SG_RUNE_COMPACT_COMPOSER_ERROR_INVALID_FRAGMENT:
		return "invalid compact fragment";
	case SG_RUNE_COMPACT_COMPOSER_ERROR_LIMIT_EXCEEDED:
		return "representation limit exceeded";
	case SG_RUNE_COMPACT_COMPOSER_ERROR_OVERFLOW:
		return "allocation size overflow";
	case SG_RUNE_COMPACT_COMPOSER_ERROR_OUT_OF_MEMORY:
		return "out of memory";
	case SG_RUNE_COMPACT_COMPOSER_ERROR_MODEL_REJECTED:
		return "assembled model rejected";
	case SG_RUNE_COMPACT_COMPOSER_ERROR_CODE_COUNT:
		break;
	}
	return "unknown compact composer error";
}
