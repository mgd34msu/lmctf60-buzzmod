#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../slipgate/sg_rune_compact_model.h"
#include "../slipgate/sg_rune_compact_analytic.h"

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

typedef struct analytic_fixture_s
{
	sg_rune_analytic_function_t functions[5];
	sg_rune_analytic_input_dimension_t inputs[7];
	sg_rune_analytic_constant_t constants[1];
	sg_rune_analytic_affine_t affines[1];
	sg_rune_analytic_scalar_bits_t slopes[2];
	sg_rune_analytic_polynomial_t polynomials[1];
	sg_rune_analytic_scalar_bits_t polynomial_coefficients[6];
	sg_rune_analytic_ballistic_t ballistics[1];
	sg_rune_analytic_piecewise_t piecewise[2];
	sg_rune_analytic_piecewise_clause_t clauses[2];
	sg_rune_compact_analytic_t analytic;
} analytic_fixture_t;

static sg_rune_analytic_scalar_bits_t Scalar(float value)
{
	sg_rune_analytic_scalar_bits_t scalar;

	memcpy(&scalar.bits, &value, sizeof(scalar.bits));
	return scalar;
}

static void InitFixture(analytic_fixture_t *fixture)
{
	uint32_t index;

	memset(fixture, 0, sizeof(*fixture));
	fixture->inputs[0] = SG_RUNE_ANALYTIC_INPUT_TIME_SECONDS;
	fixture->inputs[1] = SG_RUNE_ANALYTIC_INPUT_WORLD_X;
	fixture->inputs[2] = SG_RUNE_ANALYTIC_INPUT_WORLD_Y;
	fixture->inputs[3] = SG_RUNE_ANALYTIC_INPUT_WORLD_X;
	fixture->inputs[4] = SG_RUNE_ANALYTIC_INPUT_WORLD_Y;
	fixture->inputs[5] = SG_RUNE_ANALYTIC_INPUT_TIME_SECONDS;
	fixture->inputs[6] = SG_RUNE_ANALYTIC_INPUT_TIME_SECONDS;

	fixture->functions[0].inputs = (sg_rune_analytic_input_span_t){ 0U, 1U };
	fixture->functions[0].definition = 0U;
	fixture->functions[0].output = SG_RUNE_ANALYTIC_OUTPUT_POSITION_X;
	fixture->functions[0].form = SG_RUNE_COMPACT_ANALYTIC_CONSTANT;
	fixture->constants[0].value = Scalar(2.0f);

	fixture->functions[1].inputs = (sg_rune_analytic_input_span_t){ 1U, 2U };
	fixture->functions[1].definition = 0U;
	fixture->functions[1].output = SG_RUNE_ANALYTIC_OUTPUT_COST;
	fixture->functions[1].form = SG_RUNE_COMPACT_ANALYTIC_AFFINE;
	fixture->affines[0].bias = Scalar(1.0f);
	fixture->affines[0].slopes =
		(sg_rune_analytic_affine_slope_span_t){ 0U, 2U };
	fixture->slopes[0] = Scalar(2.0f);
	fixture->slopes[1] = Scalar(3.0f);

	fixture->functions[2].inputs = (sg_rune_analytic_input_span_t){ 3U, 2U };
	fixture->functions[2].definition = 0U;
	fixture->functions[2].output = SG_RUNE_ANALYTIC_OUTPUT_DAMAGE;
	fixture->functions[2].form = SG_RUNE_COMPACT_ANALYTIC_POLYNOMIAL;
	fixture->polynomials[0].degree = 2U;
	fixture->polynomials[0].coefficients =
		(sg_rune_analytic_polynomial_coefficient_span_t){ 0U, 6U };
	for (index = 0U; index < 6U; index++)
		fixture->polynomial_coefficients[index] = Scalar((float)index + 1.0f);

	fixture->functions[3].inputs = (sg_rune_analytic_input_span_t){ 5U, 1U };
	fixture->functions[3].definition = 0U;
	fixture->functions[3].output = SG_RUNE_ANALYTIC_OUTPUT_POSITION_X;
	fixture->functions[3].form = SG_RUNE_COMPACT_ANALYTIC_BALLISTIC;
	fixture->ballistics[0].initial = Scalar(0.0f);
	fixture->ballistics[0].first_derivative = Scalar(20.0f);
	fixture->ballistics[0].half_second_derivative = Scalar(-5.0f);

	fixture->functions[4].inputs = (sg_rune_analytic_input_span_t){ 6U, 1U };
	fixture->functions[4].definition = 0U;
	fixture->functions[4].output = SG_RUNE_ANALYTIC_OUTPUT_POSITION_X;
	fixture->functions[4].form = SG_RUNE_COMPACT_ANALYTIC_PIECEWISE;
	fixture->piecewise[0].clauses =
		(sg_rune_analytic_piecewise_clause_span_t){ 0U, 2U };
	fixture->piecewise[0].default_function.value = 0U;
	fixture->piecewise[0].selector_input = 0U;
	fixture->clauses[0].lower = Scalar(0.0f);
	fixture->clauses[0].upper = Scalar(1.0f);
	fixture->clauses[0].function.value = 0U;
	fixture->clauses[0].ownership = SG_RUNE_ANALYTIC_INTERVAL_CLOSED_OPEN;
	fixture->clauses[1].lower = Scalar(1.0f);
	fixture->clauses[1].upper = Scalar(2.0f);
	fixture->clauses[1].function.value = 3U;
	fixture->clauses[1].ownership = SG_RUNE_ANALYTIC_INTERVAL_CLOSED_CLOSED;

	fixture->analytic.version = SG_RUNE_COMPACT_ANALYTIC_VERSION;
	fixture->analytic.functions = fixture->functions;
	fixture->analytic.function_count = 5U;
	fixture->analytic.input_dimensions = fixture->inputs;
	fixture->analytic.input_dimension_count = 7U;
	fixture->analytic.constants = fixture->constants;
	fixture->analytic.constant_count = 1U;
	fixture->analytic.affines = fixture->affines;
	fixture->analytic.affine_count = 1U;
	fixture->analytic.affine_slopes = fixture->slopes;
	fixture->analytic.affine_slope_count = 2U;
	fixture->analytic.polynomials = fixture->polynomials;
	fixture->analytic.polynomial_count = 1U;
	fixture->analytic.polynomial_coefficients =
		fixture->polynomial_coefficients;
	fixture->analytic.polynomial_coefficient_count = 6U;
	fixture->analytic.ballistics = fixture->ballistics;
	fixture->analytic.ballistic_count = 1U;
	fixture->analytic.piecewise = fixture->piecewise;
	fixture->analytic.piecewise_count = 1U;
	fixture->analytic.piecewise_clauses = fixture->clauses;
	fixture->analytic.piecewise_clause_count = 2U;
}

static void CheckValid(const sg_rune_compact_analytic_t *analytic)
{
	sg_rune_analytic_error_t error;

	CHECK(SG_RuneCompactAnalyticValidate(analytic, &error));
	CHECK(error.code == SG_RUNE_ANALYTIC_ERROR_NONE);
}

static void TestValidTypedLayouts(void)
{
	analytic_fixture_t fixture;
	static const uint8_t expected[6][2] = {
		{ 0U, 0U },
		{ 1U, 0U },
		{ 0U, 1U },
		{ 2U, 0U },
		{ 1U, 1U },
		{ 0U, 2U }
	};
	uint8_t exponents[2];
	uint32_t coefficient;

	InitFixture(&fixture);
	CheckValid(&fixture.analytic);
	CHECK(SG_RuneAnalyticPolynomialCoefficientCount(2U, 2U) == 6U);
	for (coefficient = 0U; coefficient < 6U; coefficient++) {
		CHECK(SG_RuneAnalyticPolynomialExponentAt(2U, 2U,
			coefficient, exponents, 2U));
		CHECK(exponents[0] == expected[coefficient][0]);
		CHECK(exponents[1] == expected[coefficient][1]);
	}
	CHECK(!SG_RuneAnalyticPolynomialExponentAt(2U, 2U, 6U,
		exponents, 2U));
	CHECK(!SG_RuneAnalyticPolynomialExponentAt(2U, 2U, 0U,
		exponents, 1U));
	CHECK(fixture.functions[0].definition == 0U);
	CHECK(fixture.functions[3].definition == 0U);
	CHECK(fixture.functions[4].definition == 0U);
	CHECK(SG_RUNE_COMPACT_ANALYTIC_FORM_COUNT == 5);
	CHECK(SG_RUNE_COMPACT_ANALYTIC_FORM_COUNT == 5);
}

static void TestWrongArity(void)
{
	analytic_fixture_t fixture;
	sg_rune_analytic_error_t error;

	InitFixture(&fixture);
	fixture.affines[0].slopes.count = 1U;
	CHECK(!SG_RuneCompactAnalyticValidate(&fixture.analytic, &error));
	CHECK(error.code == SG_RUNE_ANALYTIC_ERROR_WRONG_ARITY);
	InitFixture(&fixture);
	fixture.polynomials[0].coefficients.count = 5U;
	CHECK(!SG_RuneCompactAnalyticValidate(&fixture.analytic, &error));
	CHECK(error.code == SG_RUNE_ANALYTIC_ERROR_WRONG_ARITY);
	InitFixture(&fixture);
	fixture.functions[1].inputs.count = 0U;
	CHECK(!SG_RuneCompactAnalyticValidate(&fixture.analytic, &error));
	CHECK(error.code == SG_RUNE_ANALYTIC_ERROR_WRONG_ARITY);
}

static void TestWrongDimensionAndOutput(void)
{
	analytic_fixture_t fixture;
	sg_rune_analytic_error_t error;

	InitFixture(&fixture);
	fixture.inputs[5] = SG_RUNE_ANALYTIC_INPUT_DISTANCE;
	CHECK(!SG_RuneCompactAnalyticValidate(&fixture.analytic, &error));
	CHECK(error.code == SG_RUNE_ANALYTIC_ERROR_WRONG_ARITY);
	InitFixture(&fixture);
	fixture.inputs[2] = SG_RUNE_ANALYTIC_INPUT_WORLD_X;
	CHECK(!SG_RuneCompactAnalyticValidate(&fixture.analytic, &error));
	CHECK(error.code == SG_RUNE_ANALYTIC_ERROR_NONCANONICAL_ORDER);
	InitFixture(&fixture);
	fixture.inputs[1] = SG_RUNE_ANALYTIC_INPUT_DIMENSION_COUNT;
	CHECK(!SG_RuneCompactAnalyticValidate(&fixture.analytic, &error));
	CHECK(error.code == SG_RUNE_ANALYTIC_ERROR_INVALID_DIMENSION);
	InitFixture(&fixture);
	fixture.functions[3].output = SG_RUNE_ANALYTIC_OUTPUT_MEANING_COUNT;
	CHECK(!SG_RuneCompactAnalyticValidate(&fixture.analytic, &error));
	CHECK(error.code == SG_RUNE_ANALYTIC_ERROR_INVALID_OUTPUT);
	InitFixture(&fixture);
	fixture.functions[3].form = SG_RUNE_COMPACT_ANALYTIC_FORM_COUNT;
	CHECK(!SG_RuneCompactAnalyticValidate(&fixture.analytic, &error));
	CHECK(error.code == SG_RUNE_ANALYTIC_ERROR_INVALID_FORM);
}

static void TestWrongDomainAndOrder(void)
{
	analytic_fixture_t fixture;
	sg_rune_analytic_error_t error;

	InitFixture(&fixture);
	fixture.clauses[0].upper = Scalar(0.0f);
	CHECK(!SG_RuneCompactAnalyticValidate(&fixture.analytic, &error));
	CHECK(error.code == SG_RUNE_ANALYTIC_ERROR_INVALID_DOMAIN);
	InitFixture(&fixture);
	fixture.clauses[1].lower = Scalar(0.5f);
	CHECK(!SG_RuneCompactAnalyticValidate(&fixture.analytic, &error));
	CHECK(error.code == SG_RUNE_ANALYTIC_ERROR_NONCANONICAL_ORDER);
	InitFixture(&fixture);
	fixture.clauses[0].ownership = SG_RUNE_ANALYTIC_INTERVAL_CLOSED_CLOSED;
	CHECK(!SG_RuneCompactAnalyticValidate(&fixture.analytic, &error));
	CHECK(error.code == SG_RUNE_ANALYTIC_ERROR_INVALID_DOMAIN);
	InitFixture(&fixture);
	fixture.functions[3].definition = 1U;
	CHECK(!SG_RuneCompactAnalyticValidate(&fixture.analytic, &error));
	CHECK(error.code == SG_RUNE_ANALYTIC_ERROR_NONCANONICAL_ORDER);
}

static void TestWrongReference(void)
{
	analytic_fixture_t fixture;
	sg_rune_analytic_error_t error;

	InitFixture(&fixture);
	fixture.clauses[0].function.value = 2U;
	CHECK(!SG_RuneCompactAnalyticValidate(&fixture.analytic, &error));
	CHECK(error.code == SG_RUNE_ANALYTIC_ERROR_INVALID_REFERENCE);
	InitFixture(&fixture);
	fixture.clauses[0].function.value = 4U;
	CHECK(!SG_RuneCompactAnalyticValidate(&fixture.analytic, &error));
	CHECK(error.code == SG_RUNE_ANALYTIC_ERROR_INVALID_REFERENCE);
	InitFixture(&fixture);
	fixture.piecewise[0].default_function.value = 2U;
	CHECK(!SG_RuneCompactAnalyticValidate(&fixture.analytic, &error));
	CHECK(error.code == SG_RUNE_ANALYTIC_ERROR_INVALID_REFERENCE);
	InitFixture(&fixture);
	fixture.functions[0].output = SG_RUNE_ANALYTIC_OUTPUT_COST;
	CHECK(!SG_RuneCompactAnalyticValidate(&fixture.analytic, &error));
	CHECK(error.code == SG_RUNE_ANALYTIC_ERROR_INVALID_REFERENCE);
}

static void TestNestedPiecewiseDefault(void)
{
	analytic_fixture_t fixture;
	sg_rune_analytic_error_t error;

	InitFixture(&fixture);
	fixture.functions[3].form = SG_RUNE_COMPACT_ANALYTIC_PIECEWISE;
	fixture.functions[3].definition = 0U;
	fixture.functions[4].definition = 1U;
	fixture.piecewise[0].clauses =
		(sg_rune_analytic_piecewise_clause_span_t){ 0U, 1U };
	fixture.piecewise[0].default_function.value = 0U;
	fixture.piecewise[0].selector_input = 0U;
	fixture.piecewise[1].clauses =
		(sg_rune_analytic_piecewise_clause_span_t){ 1U, 1U };
	fixture.piecewise[1].default_function.value = 3U;
	fixture.piecewise[1].selector_input = 0U;
	fixture.clauses[1].lower = Scalar(0.0f);
	fixture.clauses[1].upper = Scalar(1.0f);
	fixture.clauses[1].function.value = 0U;
	fixture.clauses[1].ownership = SG_RUNE_ANALYTIC_INTERVAL_CLOSED_OPEN;
	fixture.analytic.ballistics = NULL;
	fixture.analytic.ballistic_count = 0U;
	fixture.analytic.piecewise_count = 2U;
	CHECK(!SG_RuneCompactAnalyticValidate(&fixture.analytic, &error));
	CHECK(error.code == SG_RUNE_ANALYTIC_ERROR_INVALID_REFERENCE);
}

static void TestNonfiniteAndNegativeZero(void)
{
	analytic_fixture_t fixture;
	sg_rune_analytic_error_t error;

	InitFixture(&fixture);
	fixture.slopes[1].bits = UINT32_C(0x7f800000);
	CHECK(!SG_RuneCompactAnalyticValidate(&fixture.analytic, &error));
	CHECK(error.code == SG_RUNE_ANALYTIC_ERROR_NONCANONICAL_SCALAR);
	InitFixture(&fixture);
	fixture.ballistics[0].initial.bits = UINT32_C(0x80000000);
	CHECK(!SG_RuneCompactAnalyticValidate(&fixture.analytic, &error));
	CHECK(error.code == SG_RUNE_ANALYTIC_ERROR_NONCANONICAL_SCALAR);
}

int main(void)
{
	TestValidTypedLayouts();
	TestWrongArity();
	TestWrongDimensionAndOutput();
	TestWrongDomainAndOrder();
	TestWrongReference();
	TestNestedPiecewiseDefault();
	TestNonfiniteAndNegativeZero();
	if (failures != 0) {
		fprintf(stderr, "%d compact analytic checks failed\n", failures);
		return 1;
	}
	puts("compact analytic checks passed");
	return 0;
}
