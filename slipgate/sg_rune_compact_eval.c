#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "sg_rune_compact_eval.h"

static int SpanWithin(uint32_t first, uint32_t count, uint32_t limit)
{
	return first <= limit && count <= limit - first;
}

static int ScalarValue(sg_rune_analytic_scalar_bits_t scalar, float *value_out)
{
	float value;

	if ((scalar.bits & UINT32_C(0x7f800000)) == UINT32_C(0x7f800000) ||
		scalar.bits == UINT32_C(0x80000000))
		return 0;
	memcpy(&value, &scalar.bits, sizeof(value));
	*value_out = value;
	return 1;
}

static int LowerClosed(sg_rune_analytic_interval_ownership_t ownership)
{
	return ownership == SG_RUNE_ANALYTIC_INTERVAL_CLOSED_OPEN ||
		ownership == SG_RUNE_ANALYTIC_INTERVAL_CLOSED_CLOSED;
}

static int UpperClosed(sg_rune_analytic_interval_ownership_t ownership)
{
	return ownership == SG_RUNE_ANALYTIC_INTERVAL_OPEN_CLOSED ||
		ownership == SG_RUNE_ANALYTIC_INTERVAL_CLOSED_CLOSED;
}

static int IntervalContains(float value, float lower, float upper,
	sg_rune_analytic_interval_ownership_t ownership)
{
	int above_lower = value > lower ||
		(value == lower && LowerClosed(ownership));
	int below_upper = value < upper ||
		(value == upper && UpperClosed(ownership));

	return above_lower && below_upper;
}

static sg_rune_compact_eval_status_t ValidateQuery(
	const sg_rune_compact_eval_query_t *query)
{
	uint32_t input;

	if (query->input_count > (uint32_t)SG_RUNE_ANALYTIC_INPUT_DIMENSION_COUNT ||
		(query->input_count != 0U && query->inputs == NULL))
		return SG_RUNE_COMPACT_EVAL_INVALID_ARGUMENT;
	for (input = 0U; input < query->input_count; input++) {
		uint32_t earlier;

		if ((uint32_t)query->inputs[input].dimension >=
			(uint32_t)SG_RUNE_ANALYTIC_INPUT_DIMENSION_COUNT)
			return SG_RUNE_COMPACT_EVAL_INVALID_QUERY_DIMENSION;
		if (!isfinite(query->inputs[input].value))
			return SG_RUNE_COMPACT_EVAL_NONFINITE_INPUT;
		for (earlier = 0U; earlier < input; earlier++)
			if (query->inputs[earlier].dimension ==
				query->inputs[input].dimension)
				return SG_RUNE_COMPACT_EVAL_DUPLICATE_QUERY_DIMENSION;
	}
	return SG_RUNE_COMPACT_EVAL_OK;
}

static sg_rune_compact_eval_status_t ResolveInputs(
	const sg_rune_compact_analytic_t *analytic,
	const sg_rune_analytic_function_t *function,
	const sg_rune_compact_eval_query_t *query,
	float values[SG_RUNE_ANALYTIC_MAX_INPUTS])
{
	uint32_t offset;

	if (function->inputs.count > SG_RUNE_ANALYTIC_MAX_INPUTS ||
		!SpanWithin(function->inputs.first, function->inputs.count,
			analytic->input_dimension_count) ||
		(function->inputs.count != 0U && analytic->input_dimensions == NULL))
		return SG_RUNE_COMPACT_EVAL_INVALID_CONTRACT;
	for (offset = 0U; offset < function->inputs.count; offset++) {
		sg_rune_analytic_input_dimension_t dimension =
			analytic->input_dimensions[function->inputs.first + offset];
		uint32_t query_input;

		if ((uint32_t)dimension >=
			(uint32_t)SG_RUNE_ANALYTIC_INPUT_DIMENSION_COUNT ||
			(offset != 0U &&
			 (uint32_t)analytic->input_dimensions[
				 function->inputs.first + offset - 1U] >=
				 (uint32_t)dimension))
			return SG_RUNE_COMPACT_EVAL_INVALID_CONTRACT;
		for (query_input = 0U; query_input < query->input_count; query_input++)
			if (query->inputs[query_input].dimension == dimension)
				break;
		if (query_input == query->input_count)
			return SG_RUNE_COMPACT_EVAL_MISSING_INPUT;
		values[offset] = query->inputs[query_input].value;
	}
	return SG_RUNE_COMPACT_EVAL_OK;
}

static int FunctionsCompatible(const sg_rune_compact_analytic_t *analytic,
	const sg_rune_analytic_function_t *left,
	const sg_rune_analytic_function_t *right)
{
	uint32_t offset;

	if (left->output != right->output ||
		left->inputs.count != right->inputs.count ||
		!SpanWithin(left->inputs.first, left->inputs.count,
			analytic->input_dimension_count) ||
		!SpanWithin(right->inputs.first, right->inputs.count,
			analytic->input_dimension_count) ||
		(left->inputs.count != 0U && analytic->input_dimensions == NULL))
		return 0;
	for (offset = 0U; offset < left->inputs.count; offset++)
		if (analytic->input_dimensions[left->inputs.first + offset] !=
			analytic->input_dimensions[right->inputs.first + offset])
			return 0;
	return 1;
}

static sg_rune_compact_eval_status_t EvaluateConstant(
	const sg_rune_compact_analytic_t *analytic,
	const sg_rune_analytic_function_t *function, float *value_out)
{
	if (function->definition >= analytic->constant_count ||
		analytic->constants == NULL ||
		!ScalarValue(analytic->constants[function->definition].value, value_out))
		return SG_RUNE_COMPACT_EVAL_INVALID_CONTRACT;
	return SG_RUNE_COMPACT_EVAL_OK;
}

static sg_rune_compact_eval_status_t EvaluateAffine(
	const sg_rune_compact_analytic_t *analytic,
	const sg_rune_analytic_function_t *function, const float *inputs,
	float *value_out)
{
	const sg_rune_analytic_affine_t *affine;
	float result;
	uint32_t offset;

	if (function->inputs.count == 0U ||
		function->definition >= analytic->affine_count ||
		analytic->affines == NULL)
		return SG_RUNE_COMPACT_EVAL_INVALID_CONTRACT;
	affine = &analytic->affines[function->definition];
	if (affine->slopes.count != function->inputs.count ||
		!SpanWithin(affine->slopes.first, affine->slopes.count,
			analytic->affine_slope_count) || analytic->affine_slopes == NULL ||
		!ScalarValue(affine->bias, &result))
		return SG_RUNE_COMPACT_EVAL_INVALID_CONTRACT;
	for (offset = 0U; offset < affine->slopes.count; offset++) {
		float slope;
		float term;

		if (!ScalarValue(analytic->affine_slopes[
			affine->slopes.first + offset], &slope))
			return SG_RUNE_COMPACT_EVAL_INVALID_CONTRACT;
		term = slope * inputs[offset];
		result = result + term;
	}
	*value_out = result;
	return SG_RUNE_COMPACT_EVAL_OK;
}

static float IntegerPower(float base, uint8_t exponent)
{
	float result = 1.0f;
	uint8_t count;

	for (count = 0U; count < exponent; count++)
		result = result * base;
	return result;
}

static sg_rune_compact_eval_status_t EvaluatePolynomial(
	const sg_rune_compact_analytic_t *analytic,
	const sg_rune_analytic_function_t *function, const float *inputs,
	float *value_out)
{
	const sg_rune_analytic_polynomial_t *polynomial;
	uint8_t exponents[SG_RUNE_ANALYTIC_MAX_INPUTS];
	uint32_t expected;
	uint32_t coefficient;
	float result = 0.0f;

	if (function->definition >= analytic->polynomial_count ||
		analytic->polynomials == NULL)
		return SG_RUNE_COMPACT_EVAL_INVALID_CONTRACT;
	polynomial = &analytic->polynomials[function->definition];
	expected = SG_RuneAnalyticPolynomialCoefficientCount(
		function->inputs.count, polynomial->degree);
	if (expected == 0U || polynomial->reserved[0] != 0U ||
		polynomial->reserved[1] != 0U || polynomial->reserved[2] != 0U ||
		polynomial->coefficients.count != expected ||
		!SpanWithin(polynomial->coefficients.first,
			polynomial->coefficients.count,
			analytic->polynomial_coefficient_count) ||
		analytic->polynomial_coefficients == NULL)
		return SG_RUNE_COMPACT_EVAL_INVALID_CONTRACT;
	for (coefficient = 0U; coefficient < expected; coefficient++) {
		float term;
		uint32_t input;

		if (!SG_RuneAnalyticPolynomialExponentAt(function->inputs.count,
			polynomial->degree, coefficient, exponents,
			SG_RUNE_ANALYTIC_MAX_INPUTS) ||
			!ScalarValue(analytic->polynomial_coefficients[
				polynomial->coefficients.first + coefficient], &term))
			return SG_RUNE_COMPACT_EVAL_INVALID_CONTRACT;
		for (input = 0U; input < function->inputs.count; input++)
			term = term * IntegerPower(inputs[input], exponents[input]);
		result = result + term;
	}
	*value_out = result;
	return SG_RUNE_COMPACT_EVAL_OK;
}

static sg_rune_compact_eval_status_t EvaluateBallistic(
	const sg_rune_compact_analytic_t *analytic,
	const sg_rune_analytic_function_t *function, float time, float *value_out)
{
	const sg_rune_analytic_ballistic_t *ballistic;
	float initial;
	float first_derivative;
	float half_second_derivative;
	float linear;
	float squared;
	float quadratic;

	if (function->inputs.count != 1U || analytic->input_dimensions == NULL ||
		analytic->input_dimensions[function->inputs.first] !=
			SG_RUNE_ANALYTIC_INPUT_TIME_SECONDS ||
		function->definition >= analytic->ballistic_count ||
		analytic->ballistics == NULL)
		return SG_RUNE_COMPACT_EVAL_INVALID_CONTRACT;
	ballistic = &analytic->ballistics[function->definition];
	if (!ScalarValue(ballistic->initial, &initial) ||
		!ScalarValue(ballistic->first_derivative, &first_derivative) ||
		!ScalarValue(ballistic->half_second_derivative,
			&half_second_derivative))
		return SG_RUNE_COMPACT_EVAL_INVALID_CONTRACT;
	linear = first_derivative * time;
	squared = time * time;
	quadratic = half_second_derivative * squared;
	*value_out = (initial + linear) + quadratic;
	return SG_RUNE_COMPACT_EVAL_OK;
}

static sg_rune_compact_eval_status_t SelectPiecewiseFunction(
	const sg_rune_compact_analytic_t *analytic,
	const sg_rune_analytic_function_t *function, uint32_t function_index,
	const float *inputs, uint32_t *selected_out)
{
	const sg_rune_analytic_piecewise_t *piecewise;
	uint32_t selected;
	uint32_t clause_offset;
	float previous_upper = 0.0f;
	sg_rune_analytic_interval_ownership_t previous_ownership =
		SG_RUNE_ANALYTIC_INTERVAL_OPEN_OPEN;

	if (function->inputs.count == 0U ||
		function->definition >= analytic->piecewise_count ||
		analytic->piecewise == NULL)
		return SG_RUNE_COMPACT_EVAL_INVALID_CONTRACT;
	piecewise = &analytic->piecewise[function->definition];
	if (piecewise->selector_input >= function->inputs.count ||
		piecewise->clauses.count == 0U ||
		piecewise->clauses.count > SG_RUNE_ANALYTIC_MAX_CLAUSES_PER_PIECE ||
		!SpanWithin(piecewise->clauses.first, piecewise->clauses.count,
			analytic->piecewise_clause_count) ||
		analytic->piecewise_clauses == NULL ||
		piecewise->default_function.value >= function_index ||
		analytic->functions == NULL ||
		analytic->functions[piecewise->default_function.value].form ==
			SG_RUNE_COMPACT_ANALYTIC_PIECEWISE ||
		!FunctionsCompatible(analytic,
			&analytic->functions[piecewise->default_function.value], function))
		return SG_RUNE_COMPACT_EVAL_INVALID_CONTRACT;
	selected = piecewise->default_function.value;
	for (clause_offset = 0U; clause_offset < piecewise->clauses.count;
		clause_offset++) {
		const sg_rune_analytic_piecewise_clause_t *clause =
			&analytic->piecewise_clauses[
				piecewise->clauses.first + clause_offset];
		float lower;
		float upper;

		if ((uint32_t)clause->ownership >=
				(uint32_t)SG_RUNE_ANALYTIC_INTERVAL_OWNERSHIP_COUNT ||
			!ScalarValue(clause->lower, &lower) ||
			!ScalarValue(clause->upper, &upper) || !(lower < upper) ||
			clause->function.value >= function_index ||
			analytic->functions[clause->function.value].form ==
				SG_RUNE_COMPACT_ANALYTIC_PIECEWISE ||
			!FunctionsCompatible(analytic,
				&analytic->functions[clause->function.value], function))
			return SG_RUNE_COMPACT_EVAL_INVALID_CONTRACT;
		if (clause_offset != 0U &&
			(lower < previous_upper ||
			 (lower == previous_upper && LowerClosed(clause->ownership) &&
			  UpperClosed(previous_ownership))))
			return SG_RUNE_COMPACT_EVAL_INVALID_CONTRACT;
		if (IntervalContains(inputs[piecewise->selector_input], lower, upper,
			clause->ownership))
			selected = clause->function.value;
		previous_upper = upper;
		previous_ownership = clause->ownership;
	}
	*selected_out = selected;
	return SG_RUNE_COMPACT_EVAL_OK;
}

sg_rune_compact_eval_status_t SG_RuneCompactEval(
	const sg_rune_compact_analytic_t *analytic,
	const sg_rune_compact_eval_query_t *query,
	sg_rune_compact_eval_result_t *result_out)
{
	uint32_t function_index;

	if (analytic == NULL || query == NULL || result_out == NULL)
		return SG_RUNE_COMPACT_EVAL_INVALID_ARGUMENT;
	if (analytic->version != SG_RUNE_COMPACT_ANALYTIC_VERSION ||
		analytic->reserved != 0U ||
		analytic->function_count > SG_RUNE_ANALYTIC_MAX_FUNCTIONS ||
		analytic->input_dimension_count >
			SG_RUNE_ANALYTIC_MAX_INPUT_DIMENSIONS ||
		analytic->constant_count > analytic->function_count ||
		analytic->affine_count > analytic->function_count ||
		analytic->affine_slope_count > SG_RUNE_ANALYTIC_MAX_AFFINE_SLOPES ||
		analytic->polynomial_count > analytic->function_count ||
		analytic->polynomial_coefficient_count >
			SG_RUNE_ANALYTIC_MAX_POLYNOMIAL_COEFFICIENTS ||
		analytic->ballistic_count > analytic->function_count ||
		analytic->piecewise_count > analytic->function_count ||
		analytic->piecewise_clause_count >
			SG_RUNE_ANALYTIC_MAX_PIECEWISE_CLAUSES ||
		(analytic->function_count != 0U && analytic->functions == NULL))
		return SG_RUNE_COMPACT_EVAL_INVALID_CONTRACT;
	if (query->function.value >= analytic->function_count)
		return SG_RUNE_COMPACT_EVAL_INVALID_FUNCTION_INDEX;
	{
		sg_rune_compact_eval_status_t status = ValidateQuery(query);

		if (status != SG_RUNE_COMPACT_EVAL_OK)
			return status;
	}
	function_index = query->function.value;
	for (;;) {
		const sg_rune_analytic_function_t *function =
			&analytic->functions[function_index];
		float inputs[SG_RUNE_ANALYTIC_MAX_INPUTS] = { 0.0f };
		float value = 0.0f;
		sg_rune_compact_eval_status_t status;

		if ((uint32_t)function->output >=
				(uint32_t)SG_RUNE_ANALYTIC_OUTPUT_MEANING_COUNT ||
			(uint32_t)function->form >=
				(uint32_t)SG_RUNE_COMPACT_ANALYTIC_FORM_COUNT)
			return SG_RUNE_COMPACT_EVAL_INVALID_CONTRACT;
		status = ResolveInputs(analytic, function, query, inputs);
		if (status != SG_RUNE_COMPACT_EVAL_OK)
			return status;
		switch (function->form) {
		case SG_RUNE_COMPACT_ANALYTIC_CONSTANT:
			status = EvaluateConstant(analytic, function, &value);
			break;
		case SG_RUNE_COMPACT_ANALYTIC_AFFINE:
			status = EvaluateAffine(analytic, function, inputs, &value);
			break;
		case SG_RUNE_COMPACT_ANALYTIC_POLYNOMIAL:
			status = EvaluatePolynomial(analytic, function, inputs, &value);
			break;
		case SG_RUNE_COMPACT_ANALYTIC_BALLISTIC:
			status = EvaluateBallistic(analytic, function, inputs[0], &value);
			break;
		case SG_RUNE_COMPACT_ANALYTIC_PIECEWISE:
			status = SelectPiecewiseFunction(analytic, function,
				function_index, inputs, &function_index);
			if (status == SG_RUNE_COMPACT_EVAL_OK)
				continue;
			break;
		case SG_RUNE_COMPACT_ANALYTIC_FORM_COUNT:
		default:
			status = SG_RUNE_COMPACT_EVAL_INVALID_CONTRACT;
			break;
		}
		if (status != SG_RUNE_COMPACT_EVAL_OK)
			return status;
		if (!isfinite(value))
			return SG_RUNE_COMPACT_EVAL_NONFINITE_RESULT;
		result_out->output = function->output;
		result_out->value = value;
		return SG_RUNE_COMPACT_EVAL_OK;
	}
}

const char *SG_RuneCompactEvalStatusString(
	sg_rune_compact_eval_status_t status)
{
	switch (status) {
	case SG_RUNE_COMPACT_EVAL_OK:
		return "ok";
	case SG_RUNE_COMPACT_EVAL_INVALID_ARGUMENT:
		return "invalid argument";
	case SG_RUNE_COMPACT_EVAL_INVALID_FUNCTION_INDEX:
		return "invalid function index";
	case SG_RUNE_COMPACT_EVAL_INVALID_QUERY_DIMENSION:
		return "invalid query dimension";
	case SG_RUNE_COMPACT_EVAL_DUPLICATE_QUERY_DIMENSION:
		return "duplicate query dimension";
	case SG_RUNE_COMPACT_EVAL_MISSING_INPUT:
		return "missing input";
	case SG_RUNE_COMPACT_EVAL_NONFINITE_INPUT:
		return "nonfinite input";
	case SG_RUNE_COMPACT_EVAL_INVALID_CONTRACT:
		return "invalid compact analytic contract";
	case SG_RUNE_COMPACT_EVAL_NONFINITE_RESULT:
		return "nonfinite result";
	case SG_RUNE_COMPACT_EVAL_STATUS_COUNT:
	default:
		return "unknown compact analytic evaluation status";
	}
}
