#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "sg_rune_compact_analytic.h"

typedef struct sg_rune_analytic_cursors_s
{
	uint32_t inputs;
	uint32_t constants;
	uint32_t affines;
	uint32_t slopes;
	uint32_t polynomials;
	uint32_t polynomial_coefficients;
	uint32_t ballistics;
	uint32_t piecewise;
	uint32_t clauses;
} sg_rune_analytic_cursors_t;

static void SetError(sg_rune_analytic_error_t *error,
	sg_rune_analytic_error_code_t code,
	sg_rune_analytic_record_domain_t domain, uint32_t record)
{
	if (error != NULL) {
		error->code = code;
		error->domain = domain;
		error->record = record;
	}
}

static int ArrayPresent(const void *array, uint32_t count)
{
	return count == 0U || array != NULL;
}

static int SpanWithin(uint32_t first, uint32_t count, uint32_t limit)
{
	return first <= limit && count <= limit - first;
}

static int ScalarValid(sg_rune_analytic_scalar_bits_t scalar)
{
	return (scalar.bits & UINT32_C(0x7f800000)) != UINT32_C(0x7f800000) &&
		scalar.bits != UINT32_C(0x80000000);
}

static float ScalarValue(sg_rune_analytic_scalar_bits_t scalar)
{
	float value;

	memcpy(&value, &scalar.bits, sizeof(value));
	return value;
}

static int CompareU32(uint32_t left, uint32_t right)
{
	return left < right ? -1 : left > right ? 1 : 0;
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

static uint32_t Binomial(uint32_t n, uint32_t k)
{
	uint64_t result = 1U;
	uint32_t index;

	if (k > n)
		return 0U;
	if (k > n - k)
		k = n - k;
	for (index = 1U; index <= k; index++)
		result = (result * (uint64_t)(n - k + index)) / (uint64_t)index;
	return (uint32_t)result;
}

uint32_t SG_RuneAnalyticPolynomialCoefficientCount(uint32_t input_count,
	uint32_t degree)
{
	if (input_count == 0U || input_count > SG_RUNE_ANALYTIC_MAX_INPUTS ||
		degree < 2U || degree > SG_RUNE_ANALYTIC_MAX_POLYNOMIAL_DEGREE)
		return 0U;
	return Binomial(input_count + degree, degree);
}

int SG_RuneAnalyticPolynomialExponentAt(uint32_t input_count, uint32_t degree,
	uint32_t coefficient, uint8_t *exponents, uint32_t exponent_capacity)
{
	uint32_t total_degree;
	uint32_t remaining;
	uint32_t axis;

	if (exponents == NULL || exponent_capacity < input_count ||
		coefficient >= SG_RuneAnalyticPolynomialCoefficientCount(input_count,
			degree))
		return 0;
	for (total_degree = 0U; total_degree <= degree; total_degree++) {
		uint32_t terms = Binomial(input_count + total_degree - 1U,
			total_degree);

		if (coefficient < terms)
			break;
		coefficient -= terms;
	}
	remaining = total_degree;
	for (axis = 0U; axis + 1U < input_count; axis++) {
		uint32_t exponent = remaining;
		uint32_t tail_inputs = input_count - axis - 1U;

		for (;;) {
			uint32_t tail_degree = remaining - exponent;
			uint32_t branch = Binomial(tail_degree + tail_inputs - 1U,
				tail_inputs - 1U);

			if (coefficient < branch) {
				exponents[axis] = (uint8_t)exponent;
				remaining = tail_degree;
				break;
			}
			coefficient -= branch;
			if (exponent == 0U)
				return 0;
			exponent--;
		}
	}
	exponents[input_count - 1U] = (uint8_t)remaining;
	return 1;
}

static int ValidateCounts(const sg_rune_compact_analytic_t *analytic,
	sg_rune_analytic_error_t *error)
{
	if (analytic->function_count > SG_RUNE_ANALYTIC_MAX_FUNCTIONS ||
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
			SG_RUNE_ANALYTIC_MAX_PIECEWISE_CLAUSES) {
		SetError(error, SG_RUNE_ANALYTIC_ERROR_LIMIT_EXCEEDED,
			SG_RUNE_ANALYTIC_RECORD_CONTRACT, 0U);
		return 0;
	}
	if (!ArrayPresent(analytic->functions, analytic->function_count) ||
		!ArrayPresent(analytic->input_dimensions,
			analytic->input_dimension_count) ||
		!ArrayPresent(analytic->constants, analytic->constant_count) ||
		!ArrayPresent(analytic->affines, analytic->affine_count) ||
		!ArrayPresent(analytic->affine_slopes, analytic->affine_slope_count) ||
		!ArrayPresent(analytic->polynomials, analytic->polynomial_count) ||
		!ArrayPresent(analytic->polynomial_coefficients,
			analytic->polynomial_coefficient_count) ||
		!ArrayPresent(analytic->ballistics, analytic->ballistic_count) ||
		!ArrayPresent(analytic->piecewise, analytic->piecewise_count) ||
		!ArrayPresent(analytic->piecewise_clauses,
			analytic->piecewise_clause_count)) {
		SetError(error, SG_RUNE_ANALYTIC_ERROR_INVALID_ARGUMENT,
			SG_RUNE_ANALYTIC_RECORD_CONTRACT, 0U);
		return 0;
	}
	return 1;
}

static int ValidateInputs(const sg_rune_compact_analytic_t *analytic,
	const sg_rune_analytic_function_t *function, uint32_t function_index,
	sg_rune_analytic_cursors_t *cursors, sg_rune_analytic_error_t *error)
{
	uint32_t offset;

	if (function->inputs.first != cursors->inputs ||
		function->inputs.count > SG_RUNE_ANALYTIC_MAX_INPUTS ||
		!SpanWithin(function->inputs.first, function->inputs.count,
			analytic->input_dimension_count)) {
		SetError(error, SG_RUNE_ANALYTIC_ERROR_NONCANONICAL_ORDER,
			SG_RUNE_ANALYTIC_RECORD_FUNCTION, function_index);
		return 0;
	}
	for (offset = 0U; offset < function->inputs.count; offset++) {
		uint32_t input_index = function->inputs.first + offset;
		sg_rune_analytic_input_dimension_t dimension =
			analytic->input_dimensions[input_index];

		if ((uint32_t)dimension >=
			(uint32_t)SG_RUNE_ANALYTIC_INPUT_DIMENSION_COUNT) {
			SetError(error, SG_RUNE_ANALYTIC_ERROR_INVALID_DIMENSION,
				SG_RUNE_ANALYTIC_RECORD_INPUT, input_index);
			return 0;
		}
		if (offset != 0U && (uint32_t)analytic->input_dimensions[
				input_index - 1U] >= (uint32_t)dimension) {
			SetError(error, SG_RUNE_ANALYTIC_ERROR_NONCANONICAL_ORDER,
				SG_RUNE_ANALYTIC_RECORD_INPUT, input_index);
			return 0;
		}
	}
	cursors->inputs += function->inputs.count;
	return 1;
}

static int InputsEqual(const sg_rune_compact_analytic_t *analytic,
	const sg_rune_analytic_function_t *left,
	const sg_rune_analytic_function_t *right)
{
	uint32_t offset;

	if (left->inputs.count != right->inputs.count)
		return 0;
	for (offset = 0U; offset < left->inputs.count; offset++)
		if (analytic->input_dimensions[left->inputs.first + offset] !=
			analytic->input_dimensions[right->inputs.first + offset])
			return 0;
	return 1;
}

static int ValidateConstant(const sg_rune_compact_analytic_t *analytic,
	const sg_rune_analytic_function_t *function, uint32_t function_index,
	sg_rune_analytic_cursors_t *cursors, sg_rune_analytic_error_t *error)
{
	const sg_rune_analytic_constant_t *constant;

	if (function->definition != cursors->constants ||
		function->definition >= analytic->constant_count) {
		SetError(error, SG_RUNE_ANALYTIC_ERROR_NONCANONICAL_ORDER,
			SG_RUNE_ANALYTIC_RECORD_FUNCTION, function_index);
		return 0;
	}
	constant = &analytic->constants[function->definition];
	if (!ScalarValid(constant->value)) {
		SetError(error, SG_RUNE_ANALYTIC_ERROR_NONCANONICAL_SCALAR,
			SG_RUNE_ANALYTIC_RECORD_CONSTANT, function->definition);
		return 0;
	}
	cursors->constants++;
	return 1;
}

static int ValidateAffine(const sg_rune_compact_analytic_t *analytic,
	const sg_rune_analytic_function_t *function, uint32_t function_index,
	sg_rune_analytic_cursors_t *cursors, sg_rune_analytic_error_t *error)
{
	const sg_rune_analytic_affine_t *affine;
	uint32_t slope;

	if (function->definition != cursors->affines ||
		function->definition >= analytic->affine_count) {
		SetError(error, SG_RUNE_ANALYTIC_ERROR_NONCANONICAL_ORDER,
			SG_RUNE_ANALYTIC_RECORD_FUNCTION, function_index);
		return 0;
	}
	affine = &analytic->affines[function->definition];
	if (function->inputs.count == 0U ||
		affine->slopes.count != function->inputs.count) {
		SetError(error, SG_RUNE_ANALYTIC_ERROR_WRONG_ARITY,
			SG_RUNE_ANALYTIC_RECORD_AFFINE, function->definition);
		return 0;
	}
	if (affine->slopes.first != cursors->slopes ||
		!SpanWithin(affine->slopes.first, affine->slopes.count,
			analytic->affine_slope_count)) {
		SetError(error, SG_RUNE_ANALYTIC_ERROR_NONCANONICAL_ORDER,
			SG_RUNE_ANALYTIC_RECORD_AFFINE, function->definition);
		return 0;
	}
	if (!ScalarValid(affine->bias)) {
		SetError(error, SG_RUNE_ANALYTIC_ERROR_NONCANONICAL_SCALAR,
			SG_RUNE_ANALYTIC_RECORD_AFFINE, function->definition);
		return 0;
	}
	for (slope = affine->slopes.first;
		slope < affine->slopes.first + affine->slopes.count; slope++)
		if (!ScalarValid(analytic->affine_slopes[slope])) {
			SetError(error, SG_RUNE_ANALYTIC_ERROR_NONCANONICAL_SCALAR,
				SG_RUNE_ANALYTIC_RECORD_AFFINE_SLOPE, slope);
			return 0;
		}
	cursors->affines++;
	cursors->slopes += affine->slopes.count;
	return 1;
}

static int ValidatePolynomial(const sg_rune_compact_analytic_t *analytic,
	const sg_rune_analytic_function_t *function, uint32_t function_index,
	sg_rune_analytic_cursors_t *cursors, sg_rune_analytic_error_t *error)
{
	const sg_rune_analytic_polynomial_t *polynomial;
	uint32_t expected;
	uint32_t coefficient;

	if (function->definition != cursors->polynomials ||
		function->definition >= analytic->polynomial_count) {
		SetError(error, SG_RUNE_ANALYTIC_ERROR_NONCANONICAL_ORDER,
			SG_RUNE_ANALYTIC_RECORD_FUNCTION, function_index);
		return 0;
	}
	polynomial = &analytic->polynomials[function->definition];
	expected = SG_RuneAnalyticPolynomialCoefficientCount(
		function->inputs.count, polynomial->degree);
	if (expected == 0U || polynomial->coefficients.count != expected) {
		SetError(error, SG_RUNE_ANALYTIC_ERROR_WRONG_ARITY,
			SG_RUNE_ANALYTIC_RECORD_POLYNOMIAL, function->definition);
		return 0;
	}
	if (polynomial->reserved[0] != 0U || polynomial->reserved[1] != 0U ||
		polynomial->reserved[2] != 0U ||
		polynomial->coefficients.first != cursors->polynomial_coefficients ||
		!SpanWithin(polynomial->coefficients.first,
			polynomial->coefficients.count,
			analytic->polynomial_coefficient_count)) {
		SetError(error, SG_RUNE_ANALYTIC_ERROR_NONCANONICAL_ORDER,
			SG_RUNE_ANALYTIC_RECORD_POLYNOMIAL, function->definition);
		return 0;
	}
	for (coefficient = polynomial->coefficients.first;
		coefficient < polynomial->coefficients.first +
			polynomial->coefficients.count; coefficient++)
		if (!ScalarValid(analytic->polynomial_coefficients[coefficient])) {
			SetError(error, SG_RUNE_ANALYTIC_ERROR_NONCANONICAL_SCALAR,
				SG_RUNE_ANALYTIC_RECORD_POLYNOMIAL_COEFFICIENT,
				coefficient);
			return 0;
		}
	cursors->polynomials++;
	cursors->polynomial_coefficients += polynomial->coefficients.count;
	return 1;
}

static int ValidateBallistic(const sg_rune_compact_analytic_t *analytic,
	const sg_rune_analytic_function_t *function, uint32_t function_index,
	sg_rune_analytic_cursors_t *cursors, sg_rune_analytic_error_t *error)
{
	const sg_rune_analytic_ballistic_t *ballistic;

	if (function->definition != cursors->ballistics ||
		function->definition >= analytic->ballistic_count) {
		SetError(error, SG_RUNE_ANALYTIC_ERROR_NONCANONICAL_ORDER,
			SG_RUNE_ANALYTIC_RECORD_FUNCTION, function_index);
		return 0;
	}
	if (function->inputs.count != 1U ||
		analytic->input_dimensions[function->inputs.first] !=
			SG_RUNE_ANALYTIC_INPUT_TIME_SECONDS) {
		SetError(error, SG_RUNE_ANALYTIC_ERROR_WRONG_ARITY,
			SG_RUNE_ANALYTIC_RECORD_BALLISTIC, function->definition);
		return 0;
	}
	ballistic = &analytic->ballistics[function->definition];
	if (!ScalarValid(ballistic->initial) ||
		!ScalarValid(ballistic->first_derivative) ||
		!ScalarValid(ballistic->half_second_derivative)) {
		SetError(error, SG_RUNE_ANALYTIC_ERROR_NONCANONICAL_SCALAR,
			SG_RUNE_ANALYTIC_RECORD_BALLISTIC, function->definition);
		return 0;
	}
	cursors->ballistics++;
	return 1;
}

static int ValidatePiecewise(const sg_rune_compact_analytic_t *analytic,
	const sg_rune_analytic_function_t *function, uint32_t function_index,
	sg_rune_analytic_cursors_t *cursors, sg_rune_analytic_error_t *error)
{
	const sg_rune_analytic_piecewise_t *piecewise;
	uint32_t clause_index;

	if (function->definition != cursors->piecewise ||
		function->definition >= analytic->piecewise_count) {
		SetError(error, SG_RUNE_ANALYTIC_ERROR_NONCANONICAL_ORDER,
			SG_RUNE_ANALYTIC_RECORD_FUNCTION, function_index);
		return 0;
	}
	piecewise = &analytic->piecewise[function->definition];
	if (function->inputs.count == 0U ||
		piecewise->selector_input >= function->inputs.count ||
		piecewise->clauses.count == 0U ||
		piecewise->clauses.count > SG_RUNE_ANALYTIC_MAX_CLAUSES_PER_PIECE) {
		SetError(error, SG_RUNE_ANALYTIC_ERROR_WRONG_ARITY,
			SG_RUNE_ANALYTIC_RECORD_PIECEWISE, function->definition);
		return 0;
	}
	if (piecewise->clauses.first != cursors->clauses ||
		!SpanWithin(piecewise->clauses.first, piecewise->clauses.count,
			analytic->piecewise_clause_count)) {
		SetError(error, SG_RUNE_ANALYTIC_ERROR_NONCANONICAL_ORDER,
			SG_RUNE_ANALYTIC_RECORD_PIECEWISE, function->definition);
		return 0;
	}
	if (piecewise->default_function.value >= function_index ||
		analytic->functions[piecewise->default_function.value].form ==
			SG_RUNE_COMPACT_ANALYTIC_PIECEWISE ||
		analytic->functions[piecewise->default_function.value].output !=
			function->output ||
		!InputsEqual(analytic,
			&analytic->functions[piecewise->default_function.value], function)) {
		SetError(error, SG_RUNE_ANALYTIC_ERROR_INVALID_REFERENCE,
			SG_RUNE_ANALYTIC_RECORD_PIECEWISE, function->definition);
		return 0;
	}
	for (clause_index = piecewise->clauses.first;
		clause_index < piecewise->clauses.first + piecewise->clauses.count;
		clause_index++) {
		const sg_rune_analytic_piecewise_clause_t *clause =
			&analytic->piecewise_clauses[clause_index];
		const sg_rune_analytic_function_t *child;

		if (!ScalarValid(clause->lower) || !ScalarValid(clause->upper)) {
			SetError(error, SG_RUNE_ANALYTIC_ERROR_NONCANONICAL_SCALAR,
				SG_RUNE_ANALYTIC_RECORD_PIECEWISE_CLAUSE, clause_index);
			return 0;
		}
		if ((uint32_t)clause->ownership >=
			(uint32_t)SG_RUNE_ANALYTIC_INTERVAL_OWNERSHIP_COUNT ||
			!(ScalarValue(clause->lower) < ScalarValue(clause->upper))) {
			SetError(error, SG_RUNE_ANALYTIC_ERROR_INVALID_DOMAIN,
				SG_RUNE_ANALYTIC_RECORD_PIECEWISE_CLAUSE, clause_index);
			return 0;
		}
		if (clause_index != piecewise->clauses.first) {
			const sg_rune_analytic_piecewise_clause_t *previous =
				&analytic->piecewise_clauses[clause_index - 1U];
			float previous_upper = ScalarValue(previous->upper);
			float current_lower = ScalarValue(clause->lower);

			if (current_lower < previous_upper) {
				SetError(error, SG_RUNE_ANALYTIC_ERROR_NONCANONICAL_ORDER,
					SG_RUNE_ANALYTIC_RECORD_PIECEWISE_CLAUSE,
					clause_index);
				return 0;
			}
			if (clause->lower.bits == previous->upper.bits &&
				LowerClosed(clause->ownership) &&
				UpperClosed(previous->ownership)) {
				SetError(error, SG_RUNE_ANALYTIC_ERROR_INVALID_DOMAIN,
					SG_RUNE_ANALYTIC_RECORD_PIECEWISE_CLAUSE,
					clause_index);
				return 0;
			}
		}
		if (clause->function.value >= function_index) {
			SetError(error, SG_RUNE_ANALYTIC_ERROR_INVALID_REFERENCE,
				SG_RUNE_ANALYTIC_RECORD_PIECEWISE_CLAUSE, clause_index);
			return 0;
		}
		child = &analytic->functions[clause->function.value];
		if (child->form == SG_RUNE_COMPACT_ANALYTIC_PIECEWISE ||
			child->output != function->output ||
			!InputsEqual(analytic, child, function)) {
			SetError(error, SG_RUNE_ANALYTIC_ERROR_INVALID_REFERENCE,
				SG_RUNE_ANALYTIC_RECORD_PIECEWISE_CLAUSE, clause_index);
			return 0;
		}
	}
	cursors->piecewise++;
	cursors->clauses += piecewise->clauses.count;
	return 1;
}

static int CompareInputs(const sg_rune_compact_analytic_t *analytic,
	const sg_rune_analytic_function_t *left,
	const sg_rune_analytic_function_t *right)
{
	uint32_t offset;
	int comparison = CompareU32(left->inputs.count, right->inputs.count);

	if (comparison != 0)
		return comparison;
	for (offset = 0U; offset < left->inputs.count; offset++) {
		comparison = CompareU32((uint32_t)analytic->input_dimensions[
			left->inputs.first + offset],
			(uint32_t)analytic->input_dimensions[right->inputs.first + offset]);
		if (comparison != 0)
			return comparison;
	}
	return 0;
}

static int CompareScalarSpan(const sg_rune_analytic_scalar_bits_t *values,
	uint32_t left_first, uint32_t right_first, uint32_t count)
{
	uint32_t offset;

	for (offset = 0U; offset < count; offset++) {
		const int comparison = CompareU32(values[left_first + offset].bits,
			values[right_first + offset].bits);

		if (comparison != 0)
			return comparison;
	}
	return 0;
}

static int ComparePiecewise(const sg_rune_compact_analytic_t *analytic,
	const sg_rune_analytic_piecewise_t *left,
	const sg_rune_analytic_piecewise_t *right)
{
	uint32_t offset;
	int comparison = CompareU32(left->selector_input, right->selector_input);

	if (comparison == 0)
		comparison = CompareU32(left->default_function.value,
			right->default_function.value);
	if (comparison == 0)
		comparison = CompareU32(left->clauses.count, right->clauses.count);
	if (comparison != 0)
		return comparison;
	for (offset = 0U; offset < left->clauses.count; offset++) {
		const sg_rune_analytic_piecewise_clause_t *left_clause =
			&analytic->piecewise_clauses[left->clauses.first + offset];
		const sg_rune_analytic_piecewise_clause_t *right_clause =
			&analytic->piecewise_clauses[right->clauses.first + offset];

		comparison = CompareU32(left_clause->lower.bits,
			right_clause->lower.bits);
		if (comparison == 0)
			comparison = CompareU32(left_clause->upper.bits,
				right_clause->upper.bits);
		if (comparison == 0)
			comparison = CompareU32((uint32_t)left_clause->ownership,
				(uint32_t)right_clause->ownership);
		if (comparison == 0)
			comparison = CompareU32(left_clause->function.value,
				right_clause->function.value);
		if (comparison != 0)
			return comparison;
	}
	return 0;
}

static int FunctionSemanticCompare(const sg_rune_compact_analytic_t *analytic,
	const sg_rune_analytic_function_t *left,
	const sg_rune_analytic_function_t *right)
{
	int comparison = CompareU32((uint32_t)left->form, (uint32_t)right->form);

	if (comparison == 0)
		comparison = CompareU32((uint32_t)left->output,
			(uint32_t)right->output);
	if (comparison == 0)
		comparison = CompareInputs(analytic, left, right);
	if (comparison != 0)
		return comparison;
	switch (left->form) {
	case SG_RUNE_COMPACT_ANALYTIC_CONSTANT:
		return CompareU32(analytic->constants[left->definition].value.bits,
			analytic->constants[right->definition].value.bits);
	case SG_RUNE_COMPACT_ANALYTIC_AFFINE: {
		const sg_rune_analytic_affine_t *left_value =
			&analytic->affines[left->definition];
		const sg_rune_analytic_affine_t *right_value =
			&analytic->affines[right->definition];

		comparison = CompareU32(left_value->bias.bits, right_value->bias.bits);
		return comparison != 0 ? comparison : CompareScalarSpan(
			analytic->affine_slopes, left_value->slopes.first,
			right_value->slopes.first, left_value->slopes.count);
	}
	case SG_RUNE_COMPACT_ANALYTIC_POLYNOMIAL: {
		const sg_rune_analytic_polynomial_t *left_value =
			&analytic->polynomials[left->definition];
		const sg_rune_analytic_polynomial_t *right_value =
			&analytic->polynomials[right->definition];

		comparison = CompareU32((uint32_t)left_value->degree,
			(uint32_t)right_value->degree);
		return comparison != 0 ? comparison : CompareScalarSpan(
			analytic->polynomial_coefficients,
			left_value->coefficients.first, right_value->coefficients.first,
			left_value->coefficients.count);
	}
	case SG_RUNE_COMPACT_ANALYTIC_BALLISTIC: {
		const sg_rune_analytic_ballistic_t *left_value =
			&analytic->ballistics[left->definition];
		const sg_rune_analytic_ballistic_t *right_value =
			&analytic->ballistics[right->definition];

		comparison = CompareU32(left_value->initial.bits,
			right_value->initial.bits);
		if (comparison == 0)
			comparison = CompareU32(left_value->first_derivative.bits,
				right_value->first_derivative.bits);
		return comparison != 0 ? comparison : CompareU32(
			left_value->half_second_derivative.bits,
			right_value->half_second_derivative.bits);
	}
	case SG_RUNE_COMPACT_ANALYTIC_PIECEWISE:
		return ComparePiecewise(analytic,
			&analytic->piecewise[left->definition],
			&analytic->piecewise[right->definition]);
	case SG_RUNE_COMPACT_ANALYTIC_FORM_COUNT:
		break;
	}
	return 0;
}

static int CursorsComplete(const sg_rune_compact_analytic_t *analytic,
	const sg_rune_analytic_cursors_t *cursors)
{
	return cursors->inputs == analytic->input_dimension_count &&
		cursors->constants == analytic->constant_count &&
		cursors->affines == analytic->affine_count &&
		cursors->slopes == analytic->affine_slope_count &&
		cursors->polynomials == analytic->polynomial_count &&
		cursors->polynomial_coefficients ==
			analytic->polynomial_coefficient_count &&
		cursors->ballistics == analytic->ballistic_count &&
		cursors->piecewise == analytic->piecewise_count &&
		cursors->clauses == analytic->piecewise_clause_count;
}

int SG_RuneCompactAnalyticValidate(const sg_rune_compact_analytic_t *analytic,
	sg_rune_analytic_error_t *error_out)
{
	sg_rune_analytic_cursors_t cursors;
	uint32_t function_index;

	SetError(error_out, SG_RUNE_ANALYTIC_ERROR_NONE,
		SG_RUNE_ANALYTIC_RECORD_CONTRACT, 0U);
	if (analytic == NULL) {
		SetError(error_out, SG_RUNE_ANALYTIC_ERROR_INVALID_ARGUMENT,
			SG_RUNE_ANALYTIC_RECORD_CONTRACT, 0U);
		return 0;
	}
	if (analytic->version != SG_RUNE_COMPACT_ANALYTIC_VERSION ||
		analytic->reserved != 0U) {
		SetError(error_out, SG_RUNE_ANALYTIC_ERROR_UNSUPPORTED_VERSION,
			SG_RUNE_ANALYTIC_RECORD_CONTRACT, 0U);
		return 0;
	}
	if (!ValidateCounts(analytic, error_out))
		return 0;
	memset(&cursors, 0, sizeof(cursors));
	for (function_index = 0U; function_index < analytic->function_count;
		function_index++) {
		const sg_rune_analytic_function_t *function =
			&analytic->functions[function_index];
		int valid;

		if (!ValidateInputs(analytic, function, function_index, &cursors,
			error_out))
			return 0;
		if ((uint32_t)function->output >=
			(uint32_t)SG_RUNE_ANALYTIC_OUTPUT_MEANING_COUNT) {
			SetError(error_out, SG_RUNE_ANALYTIC_ERROR_INVALID_OUTPUT,
				SG_RUNE_ANALYTIC_RECORD_FUNCTION, function_index);
			return 0;
		}
		if ((uint32_t)function->form >=
			(uint32_t)SG_RUNE_COMPACT_ANALYTIC_FORM_COUNT) {
			SetError(error_out, SG_RUNE_ANALYTIC_ERROR_INVALID_FORM,
				SG_RUNE_ANALYTIC_RECORD_FUNCTION, function_index);
			return 0;
		}
		switch (function->form) {
		case SG_RUNE_COMPACT_ANALYTIC_CONSTANT:
			valid = ValidateConstant(analytic, function, function_index,
				&cursors, error_out);
			break;
		case SG_RUNE_COMPACT_ANALYTIC_AFFINE:
			valid = ValidateAffine(analytic, function, function_index,
				&cursors, error_out);
			break;
		case SG_RUNE_COMPACT_ANALYTIC_POLYNOMIAL:
			valid = ValidatePolynomial(analytic, function, function_index,
				&cursors, error_out);
			break;
		case SG_RUNE_COMPACT_ANALYTIC_BALLISTIC:
			valid = ValidateBallistic(analytic, function, function_index,
				&cursors, error_out);
			break;
		case SG_RUNE_COMPACT_ANALYTIC_PIECEWISE:
			valid = ValidatePiecewise(analytic, function, function_index,
				&cursors, error_out);
			break;
		case SG_RUNE_COMPACT_ANALYTIC_FORM_COUNT:
		default:
			valid = 0;
			break;
		}
		if (!valid)
			return 0;
		if (function_index != 0U && FunctionSemanticCompare(analytic,
				&analytic->functions[function_index - 1U], function) >= 0) {
			SetError(error_out, SG_RUNE_ANALYTIC_ERROR_NONCANONICAL_ORDER,
				SG_RUNE_ANALYTIC_RECORD_FUNCTION, function_index);
			return 0;
		}
	}
	if (!CursorsComplete(analytic, &cursors)) {
		SetError(error_out, SG_RUNE_ANALYTIC_ERROR_INVALID_REFERENCE,
			SG_RUNE_ANALYTIC_RECORD_CONTRACT, 0U);
		return 0;
	}
	return 1;
}

const char *SG_RuneCompactAnalyticErrorString(
	sg_rune_analytic_error_code_t code)
{
	switch (code) {
	case SG_RUNE_ANALYTIC_ERROR_NONE:
		return "none";
	case SG_RUNE_ANALYTIC_ERROR_INVALID_ARGUMENT:
		return "invalid argument";
	case SG_RUNE_ANALYTIC_ERROR_UNSUPPORTED_VERSION:
		return "unsupported version";
	case SG_RUNE_ANALYTIC_ERROR_LIMIT_EXCEEDED:
		return "limit exceeded";
	case SG_RUNE_ANALYTIC_ERROR_NONCANONICAL_SCALAR:
		return "noncanonical scalar";
	case SG_RUNE_ANALYTIC_ERROR_INVALID_DIMENSION:
		return "invalid input dimension";
	case SG_RUNE_ANALYTIC_ERROR_INVALID_OUTPUT:
		return "invalid output meaning";
	case SG_RUNE_ANALYTIC_ERROR_INVALID_FORM:
		return "invalid analytic form";
	case SG_RUNE_ANALYTIC_ERROR_WRONG_ARITY:
		return "wrong analytic arity";
	case SG_RUNE_ANALYTIC_ERROR_INVALID_DOMAIN:
		return "invalid piecewise domain";
	case SG_RUNE_ANALYTIC_ERROR_NONCANONICAL_ORDER:
		return "noncanonical order";
	case SG_RUNE_ANALYTIC_ERROR_INVALID_REFERENCE:
		return "invalid reference";
	case SG_RUNE_ANALYTIC_ERROR_CODE_COUNT:
	default:
		return "unknown analytic error";
	}
}
