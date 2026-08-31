#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../slipgate/sg_rune_compact_eval.h"

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

typedef struct eval_fixture_s
{
	sg_rune_analytic_function_t functions[6];
	sg_rune_analytic_input_dimension_t dimensions[7];
	sg_rune_analytic_constant_t constants[2];
	sg_rune_analytic_affine_t affines[1];
	sg_rune_analytic_scalar_bits_t slopes[2];
	sg_rune_analytic_polynomial_t polynomials[1];
	sg_rune_analytic_scalar_bits_t coefficients[6];
	sg_rune_analytic_ballistic_t ballistics[1];
	sg_rune_analytic_piecewise_t piecewise[1];
	sg_rune_analytic_piecewise_clause_t clauses[2];
	sg_rune_compact_analytic_t analytic;
} eval_fixture_t;

static sg_rune_analytic_scalar_bits_t Scalar(float value)
{
	sg_rune_analytic_scalar_bits_t scalar;

	memcpy(&scalar.bits, &value, sizeof(scalar.bits));
	return scalar;
}

static void InitFixture(eval_fixture_t *fixture)
{
	uint32_t coefficient;

	memset(fixture, 0, sizeof(*fixture));
	fixture->dimensions[0] = SG_RUNE_ANALYTIC_INPUT_TIME_SECONDS;
	fixture->dimensions[1] = SG_RUNE_ANALYTIC_INPUT_WORLD_X;
	fixture->dimensions[2] = SG_RUNE_ANALYTIC_INPUT_WORLD_Y;
	fixture->dimensions[3] = SG_RUNE_ANALYTIC_INPUT_WORLD_X;
	fixture->dimensions[4] = SG_RUNE_ANALYTIC_INPUT_WORLD_Y;
	fixture->dimensions[5] = SG_RUNE_ANALYTIC_INPUT_TIME_SECONDS;
	fixture->dimensions[6] = SG_RUNE_ANALYTIC_INPUT_TIME_SECONDS;

	fixture->functions[0].inputs = (sg_rune_analytic_input_span_t){ 0U, 1U };
	fixture->functions[0].definition = 0U;
	fixture->functions[0].output = SG_RUNE_ANALYTIC_OUTPUT_POSITION_X;
	fixture->functions[0].form = SG_RUNE_COMPACT_ANALYTIC_CONSTANT;
	fixture->constants[0].value = Scalar(10.0f);

	fixture->functions[1].inputs = (sg_rune_analytic_input_span_t){ 1U, 0U };
	fixture->functions[1].definition = 1U;
	fixture->functions[1].output = SG_RUNE_ANALYTIC_OUTPUT_CLEARANCE;
	fixture->functions[1].form = SG_RUNE_COMPACT_ANALYTIC_CONSTANT;
	fixture->constants[1].value = Scalar(-3.0f);

	fixture->functions[2].inputs = (sg_rune_analytic_input_span_t){ 1U, 2U };
	fixture->functions[2].definition = 0U;
	fixture->functions[2].output = SG_RUNE_ANALYTIC_OUTPUT_COST;
	fixture->functions[2].form = SG_RUNE_COMPACT_ANALYTIC_AFFINE;
	fixture->affines[0].bias = Scalar(1.0f);
	fixture->affines[0].slopes =
		(sg_rune_analytic_affine_slope_span_t){ 0U, 2U };
	fixture->slopes[0] = Scalar(2.0f);
	fixture->slopes[1] = Scalar(3.0f);

	fixture->functions[3].inputs = (sg_rune_analytic_input_span_t){ 3U, 2U };
	fixture->functions[3].definition = 0U;
	fixture->functions[3].output = SG_RUNE_ANALYTIC_OUTPUT_DAMAGE;
	fixture->functions[3].form = SG_RUNE_COMPACT_ANALYTIC_POLYNOMIAL;
	fixture->polynomials[0].degree = 2U;
	fixture->polynomials[0].coefficients =
		(sg_rune_analytic_polynomial_coefficient_span_t){ 0U, 6U };
	for (coefficient = 0U; coefficient < 6U; coefficient++)
		fixture->coefficients[coefficient] = Scalar((float)coefficient + 1.0f);

	fixture->functions[4].inputs = (sg_rune_analytic_input_span_t){ 5U, 1U };
	fixture->functions[4].definition = 0U;
	fixture->functions[4].output = SG_RUNE_ANALYTIC_OUTPUT_POSITION_X;
	fixture->functions[4].form = SG_RUNE_COMPACT_ANALYTIC_BALLISTIC;
	fixture->ballistics[0].initial = Scalar(0.0f);
	fixture->ballistics[0].first_derivative = Scalar(20.0f);
	fixture->ballistics[0].half_second_derivative = Scalar(-5.0f);

	fixture->functions[5].inputs = (sg_rune_analytic_input_span_t){ 6U, 1U };
	fixture->functions[5].definition = 0U;
	fixture->functions[5].output = SG_RUNE_ANALYTIC_OUTPUT_POSITION_X;
	fixture->functions[5].form = SG_RUNE_COMPACT_ANALYTIC_PIECEWISE;
	fixture->piecewise[0].clauses =
		(sg_rune_analytic_piecewise_clause_span_t){ 0U, 1U };
	fixture->piecewise[0].default_function.value = 0U;
	fixture->piecewise[0].selector_input = 0U;
	fixture->clauses[0].lower = Scalar(0.0f);
	fixture->clauses[0].upper = Scalar(1.0f);
	fixture->clauses[0].function.value = 4U;
	fixture->clauses[0].ownership = SG_RUNE_ANALYTIC_INTERVAL_CLOSED_OPEN;

	fixture->analytic.version = SG_RUNE_COMPACT_ANALYTIC_VERSION;
	fixture->analytic.functions = fixture->functions;
	fixture->analytic.function_count = 6U;
	fixture->analytic.input_dimensions = fixture->dimensions;
	fixture->analytic.input_dimension_count = 7U;
	fixture->analytic.constants = fixture->constants;
	fixture->analytic.constant_count = 2U;
	fixture->analytic.affines = fixture->affines;
	fixture->analytic.affine_count = 1U;
	fixture->analytic.affine_slopes = fixture->slopes;
	fixture->analytic.affine_slope_count = 2U;
	fixture->analytic.polynomials = fixture->polynomials;
	fixture->analytic.polynomial_count = 1U;
	fixture->analytic.polynomial_coefficients = fixture->coefficients;
	fixture->analytic.polynomial_coefficient_count = 6U;
	fixture->analytic.ballistics = fixture->ballistics;
	fixture->analytic.ballistic_count = 1U;
	fixture->analytic.piecewise = fixture->piecewise;
	fixture->analytic.piecewise_count = 1U;
	fixture->analytic.piecewise_clauses = fixture->clauses;
	fixture->analytic.piecewise_clause_count = 1U;
}

static sg_rune_compact_eval_status_t Eval(const eval_fixture_t *fixture,
	uint32_t function, const sg_rune_compact_eval_input_t *inputs,
	uint32_t input_count, sg_rune_compact_eval_result_t *result)
{
	sg_rune_compact_eval_query_t query;

	query.function.value = function;
	query.inputs = inputs;
	query.input_count = input_count;
	return SG_RuneCompactEval(&fixture->analytic, &query, result);
}

static void TestFormsAndTypedInputs(void)
{
	eval_fixture_t fixture;
	sg_rune_compact_eval_input_t time = {
		SG_RUNE_ANALYTIC_INPUT_TIME_SECONDS, 2.0f
	};
	sg_rune_compact_eval_input_t xy[3] = {
		{ SG_RUNE_ANALYTIC_INPUT_WORLD_Y, 4.0f },
		{ SG_RUNE_ANALYTIC_INPUT_DISTANCE, 99.0f },
		{ SG_RUNE_ANALYTIC_INPUT_WORLD_X, 2.0f }
	};
	sg_rune_compact_eval_result_t result;
	sg_rune_analytic_error_t contract_error;

	InitFixture(&fixture);
	CHECK(SG_RuneCompactAnalyticValidate(&fixture.analytic, &contract_error));
	CHECK(Eval(&fixture, 0U, &time, 1U, &result) == SG_RUNE_COMPACT_EVAL_OK);
	CHECK(result.output == SG_RUNE_ANALYTIC_OUTPUT_POSITION_X &&
		result.value == 10.0f);
	CHECK(Eval(&fixture, 4U, &time, 1U, &result) == SG_RUNE_COMPACT_EVAL_OK);
	CHECK(result.value == 20.0f);
	CHECK(Eval(&fixture, 2U, xy, 3U, &result) == SG_RUNE_COMPACT_EVAL_OK);
	CHECK(result.output == SG_RUNE_ANALYTIC_OUTPUT_COST && result.value == 17.0f);
	xy[0].value = 3.0f;
	CHECK(Eval(&fixture, 3U, xy, 3U, &result) == SG_RUNE_COMPACT_EVAL_OK);
	CHECK(result.output == SG_RUNE_ANALYTIC_OUTPUT_DAMAGE &&
		result.value == 114.0f);
	CHECK(Eval(&fixture, 1U, NULL, 0U, &result) == SG_RUNE_COMPACT_EVAL_OK);
	CHECK(result.output == SG_RUNE_ANALYTIC_OUTPUT_CLEARANCE &&
		result.value == -3.0f);
}

static void CheckPiecewise(eval_fixture_t *fixture,
	sg_rune_analytic_interval_ownership_t ownership, float selector,
	float expected)
{
	sg_rune_compact_eval_input_t input = {
		SG_RUNE_ANALYTIC_INPUT_TIME_SECONDS, selector
	};
	sg_rune_compact_eval_result_t result;

	fixture->clauses[0].ownership = ownership;
	CHECK(Eval(fixture, 5U, &input, 1U, &result) == SG_RUNE_COMPACT_EVAL_OK);
	CHECK(result.output == SG_RUNE_ANALYTIC_OUTPUT_POSITION_X);
	CHECK(result.value == expected);
}

static void TestPiecewiseOwnershipAndDefault(void)
{
	eval_fixture_t fixture;

	InitFixture(&fixture);
	CheckPiecewise(&fixture, SG_RUNE_ANALYTIC_INTERVAL_CLOSED_OPEN, 0.0f, 0.0f);
	CheckPiecewise(&fixture, SG_RUNE_ANALYTIC_INTERVAL_CLOSED_OPEN, 1.0f, 10.0f);
	CheckPiecewise(&fixture, SG_RUNE_ANALYTIC_INTERVAL_OPEN_CLOSED, 0.0f, 10.0f);
	CheckPiecewise(&fixture, SG_RUNE_ANALYTIC_INTERVAL_OPEN_CLOSED, 1.0f, 15.0f);
	CheckPiecewise(&fixture, SG_RUNE_ANALYTIC_INTERVAL_OPEN_OPEN, 0.0f, 10.0f);
	CheckPiecewise(&fixture, SG_RUNE_ANALYTIC_INTERVAL_OPEN_OPEN, 1.0f, 10.0f);
	CheckPiecewise(&fixture, SG_RUNE_ANALYTIC_INTERVAL_CLOSED_CLOSED, 0.0f,
		0.0f);
	CheckPiecewise(&fixture, SG_RUNE_ANALYTIC_INTERVAL_CLOSED_CLOSED, 1.0f,
		15.0f);
	CheckPiecewise(&fixture, SG_RUNE_ANALYTIC_INTERVAL_CLOSED_CLOSED, -1.0f,
		10.0f);
}

static void TestAdjacentPiecewiseOwnership(void)
{
	eval_fixture_t fixture;
	sg_rune_compact_eval_input_t input = {
		SG_RUNE_ANALYTIC_INPUT_TIME_SECONDS, 1.0f
	};
	sg_rune_compact_eval_result_t result;

	InitFixture(&fixture);
	fixture.piecewise[0].clauses.count = 2U;
	fixture.analytic.piecewise_clause_count = 2U;
	fixture.clauses[1].lower = Scalar(1.0f);
	fixture.clauses[1].upper = Scalar(2.0f);
	fixture.clauses[1].function.value = 4U;
	fixture.clauses[1].ownership = SG_RUNE_ANALYTIC_INTERVAL_CLOSED_CLOSED;
	CHECK(Eval(&fixture, 5U, &input, 1U, &result) == SG_RUNE_COMPACT_EVAL_OK);
	CHECK(result.value == 15.0f);

	fixture.clauses[0].ownership = SG_RUNE_ANALYTIC_INTERVAL_CLOSED_CLOSED;
	fixture.clauses[1].ownership = SG_RUNE_ANALYTIC_INTERVAL_OPEN_CLOSED;
	CHECK(Eval(&fixture, 5U, &input, 1U, &result) == SG_RUNE_COMPACT_EVAL_OK);
	CHECK(result.value == 15.0f);

	fixture.clauses[1].ownership = SG_RUNE_ANALYTIC_INTERVAL_CLOSED_CLOSED;
	CHECK(Eval(&fixture, 5U, &input, 1U, &result) ==
		SG_RUNE_COMPACT_EVAL_INVALID_CONTRACT);

	fixture.clauses[0].ownership = SG_RUNE_ANALYTIC_INTERVAL_OPEN_OPEN;
	fixture.clauses[1].ownership = SG_RUNE_ANALYTIC_INTERVAL_OPEN_CLOSED;
	CHECK(Eval(&fixture, 5U, &input, 1U, &result) == SG_RUNE_COMPACT_EVAL_OK);
	CHECK(result.value == 10.0f);
}

static void TestQueryRejections(void)
{
	eval_fixture_t fixture;
	sg_rune_compact_eval_input_t inputs[2];
	sg_rune_compact_eval_result_t result = {
		SG_RUNE_ANALYTIC_OUTPUT_DAMAGE, 123.0f
	};
	sg_rune_compact_eval_query_t query = { { 0U }, NULL, 0U };

	InitFixture(&fixture);
	CHECK(SG_RuneCompactEval(NULL, &query, &result) ==
		SG_RUNE_COMPACT_EVAL_INVALID_ARGUMENT);
	CHECK(SG_RuneCompactEval(&fixture.analytic, NULL, &result) ==
		SG_RUNE_COMPACT_EVAL_INVALID_ARGUMENT);
	CHECK(SG_RuneCompactEval(&fixture.analytic, &query, NULL) ==
		SG_RUNE_COMPACT_EVAL_INVALID_ARGUMENT);
	CHECK(Eval(&fixture, 6U, NULL, 0U, &result) ==
		SG_RUNE_COMPACT_EVAL_INVALID_FUNCTION_INDEX);
	CHECK(Eval(&fixture, 0U, NULL, 0U, &result) ==
		SG_RUNE_COMPACT_EVAL_MISSING_INPUT);
	inputs[0] = (sg_rune_compact_eval_input_t){
		SG_RUNE_ANALYTIC_INPUT_TIME_SECONDS, INFINITY
	};
	CHECK(Eval(&fixture, 0U, inputs, 1U, &result) ==
		SG_RUNE_COMPACT_EVAL_NONFINITE_INPUT);
	inputs[0].value = NAN;
	CHECK(Eval(&fixture, 0U, inputs, 1U, &result) ==
		SG_RUNE_COMPACT_EVAL_NONFINITE_INPUT);
	inputs[0].value = 1.0f;
	inputs[1] = inputs[0];
	CHECK(Eval(&fixture, 0U, inputs, 2U, &result) ==
		SG_RUNE_COMPACT_EVAL_DUPLICATE_QUERY_DIMENSION);
	inputs[0].dimension = SG_RUNE_ANALYTIC_INPUT_DIMENSION_COUNT;
	CHECK(Eval(&fixture, 0U, inputs, 1U, &result) ==
		SG_RUNE_COMPACT_EVAL_INVALID_QUERY_DIMENSION);
	CHECK(result.output == SG_RUNE_ANALYTIC_OUTPUT_DAMAGE &&
		result.value == 123.0f);
}

static void TestBadContractIndices(void)
{
	eval_fixture_t fixture;
	sg_rune_compact_eval_input_t time = {
		SG_RUNE_ANALYTIC_INPUT_TIME_SECONDS, 0.5f
	};
	sg_rune_compact_eval_input_t xy[2] = {
		{ SG_RUNE_ANALYTIC_INPUT_WORLD_X, 1.0f },
		{ SG_RUNE_ANALYTIC_INPUT_WORLD_Y, 1.0f }
	};
	sg_rune_compact_eval_result_t result;

	InitFixture(&fixture);
	fixture.functions[0].definition = 2U;
	CHECK(Eval(&fixture, 0U, &time, 1U, &result) ==
		SG_RUNE_COMPACT_EVAL_INVALID_CONTRACT);
	InitFixture(&fixture);
	fixture.functions[0].inputs.first = 8U;
	CHECK(Eval(&fixture, 0U, &time, 1U, &result) ==
		SG_RUNE_COMPACT_EVAL_INVALID_CONTRACT);
	InitFixture(&fixture);
	fixture.affines[0].slopes.first = 2U;
	CHECK(Eval(&fixture, 2U, xy, 2U, &result) ==
		SG_RUNE_COMPACT_EVAL_INVALID_CONTRACT);
	InitFixture(&fixture);
	fixture.polynomials[0].coefficients.first = 1U;
	CHECK(Eval(&fixture, 3U, xy, 2U, &result) ==
		SG_RUNE_COMPACT_EVAL_INVALID_CONTRACT);
	InitFixture(&fixture);
	fixture.functions[4].definition = 1U;
	CHECK(Eval(&fixture, 4U, &time, 1U, &result) ==
		SG_RUNE_COMPACT_EVAL_INVALID_CONTRACT);
	InitFixture(&fixture);
	fixture.functions[5].definition = 1U;
	CHECK(Eval(&fixture, 5U, &time, 1U, &result) ==
		SG_RUNE_COMPACT_EVAL_INVALID_CONTRACT);
	InitFixture(&fixture);
	fixture.piecewise[0].default_function.value = 6U;
	CHECK(Eval(&fixture, 5U, &time, 1U, &result) ==
		SG_RUNE_COMPACT_EVAL_INVALID_CONTRACT);
	InitFixture(&fixture);
	fixture.clauses[0].function.value = 6U;
	CHECK(Eval(&fixture, 5U, &time, 1U, &result) ==
		SG_RUNE_COMPACT_EVAL_INVALID_CONTRACT);
	InitFixture(&fixture);
	fixture.piecewise[0].selector_input = 1U;
	CHECK(Eval(&fixture, 5U, &time, 1U, &result) ==
		SG_RUNE_COMPACT_EVAL_INVALID_CONTRACT);
}

static void TestNonfiniteResult(void)
{
	eval_fixture_t fixture;
	sg_rune_compact_eval_input_t xy[2] = {
		{ SG_RUNE_ANALYTIC_INPUT_WORLD_X, 2.0f },
		{ SG_RUNE_ANALYTIC_INPUT_WORLD_Y, 0.0f }
	};
	sg_rune_compact_eval_result_t result = {
		SG_RUNE_ANALYTIC_OUTPUT_DAMAGE, 321.0f
	};

	InitFixture(&fixture);
	fixture.slopes[0] = Scalar(FLT_MAX);
	CHECK(Eval(&fixture, 2U, xy, 2U, &result) ==
		SG_RUNE_COMPACT_EVAL_NONFINITE_RESULT);
	CHECK(result.output == SG_RUNE_ANALYTIC_OUTPUT_DAMAGE &&
		result.value == 321.0f);
}

int main(void)
{
	TestFormsAndTypedInputs();
	TestPiecewiseOwnershipAndDefault();
	TestAdjacentPiecewiseOwnership();
	TestQueryRejections();
	TestBadContractIndices();
	TestNonfiniteResult();
	CHECK(strcmp(SG_RuneCompactEvalStatusString(
		SG_RUNE_COMPACT_EVAL_MISSING_INPUT), "missing input") == 0);
	CHECK(strcmp(SG_RuneCompactEvalStatusString(
		SG_RUNE_COMPACT_EVAL_STATUS_COUNT),
		"unknown compact analytic evaluation status") == 0);
	if (failures != 0) {
		fprintf(stderr, "%d compact analytic evaluator checks failed\n",
			failures);
		return 1;
	}
	puts("compact analytic evaluator checks passed");
	return 0;
}
