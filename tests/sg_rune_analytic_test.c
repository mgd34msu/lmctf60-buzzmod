/* The era-4 analytic library: polynomials over named inputs, piecewise
 * selection, validation, and the store builders. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "slipgate/sg_rune_analytic.h"

static int failures;

#define CHECK(condition) \
	do { \
		if (!(condition)) { \
			failures++; \
			fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, \
				__LINE__, #condition); \
		} \
	} while (0)

static int Near(float value, float expected)
{
	return fabsf(value - expected) <= 1e-4f * (1.0f + fabsf(expected));
}

int main(void)
{
	sg_rune_fn_store_t store;
	sg_rune_fn_table_t table;
	float inputs[SG_RUNE_FN_INPUT_COUNT];
	float value;
	uint32_t constant, affine, ballistic, piecewise, fast, slow;

	SG_RuneFnStoreInit(&store);
	memset(inputs, 0, sizeof(inputs));
	/* A constant. */
	constant = SG_RuneFnAppendConstant(&store,
		SG_RUNE_FN_OUTPUT_REACHABILITY, 1.0f);
	CHECK(constant != SG_RUNE_FN_INDEX_NONE);
	/* Cost = 0.5 + 2 * distance. */
	{
		const uint8_t in[] = { SG_RUNE_FN_INPUT_DISTANCE };
		const float slopes[] = { 2.0f };

		affine = SG_RuneFnAppendAffine(&store, SG_RUNE_FN_OUTPUT_COST,
			in, 1U, 0.5f, slopes);
	}
	CHECK(affine != SG_RUNE_FN_INDEX_NONE);
	/* Free flight: z(t) = world_z + (vz + 270) t - 400 t^2, inputs
	 * (world_z, velocity_z, time). */
	{
		const uint8_t in[] = { SG_RUNE_FN_INPUT_WORLD_Z,
			SG_RUNE_FN_INPUT_VELOCITY_Z,
			SG_RUNE_FN_INPUT_TIME_SECONDS };
		sg_rune_fn_term_t terms[4];

		memset(terms, 0, sizeof(terms));
		terms[0].coefficient = 1.0f; terms[0].exponents[0] = 1U;
		terms[1].coefficient = 1.0f; terms[1].exponents[1] = 1U;
		terms[1].exponents[2] = 1U;
		terms[2].coefficient = 270.0f; terms[2].exponents[2] = 1U;
		terms[3].coefficient = -400.0f; terms[3].exponents[2] = 2U;
		ballistic = SG_RuneFnAppendPolynomial(&store,
			SG_RUNE_FN_OUTPUT_POSITION_Z, in, 3U, terms, 4U);
	}
	CHECK(ballistic != SG_RUNE_FN_INDEX_NONE);
	/* Piecewise on distance: near uses the fast cost, far the slow one. */
	{
		const uint8_t in[] = { SG_RUNE_FN_INPUT_DISTANCE };
		const float fast_slope[] = { 1.0f };
		const float slow_slope[] = { 3.0f };
		sg_rune_fn_clause_t clauses[2];

		fast = SG_RuneFnAppendAffine(&store, SG_RUNE_FN_OUTPUT_COST,
			in, 1U, 0.0f, fast_slope);
		slow = SG_RuneFnAppendAffine(&store, SG_RUNE_FN_OUTPUT_COST,
			in, 1U, 0.0f, slow_slope);
		memset(clauses, 0, sizeof(clauses));
		clauses[0].function = fast;
		clauses[0].input = SG_RUNE_FN_INPUT_DISTANCE;
		clauses[0].lower = 0.0f;
		clauses[0].upper = 100.0f;
		clauses[1].function = slow;
		clauses[1].input = SG_RUNE_FN_INPUT_DISTANCE;
		clauses[1].lower = 100.0f;
		clauses[1].upper = INFINITY;
		piecewise = SG_RuneFnAppendPiecewise(&store,
			SG_RUNE_FN_OUTPUT_COST, clauses, 2U);
	}
	CHECK(piecewise != SG_RUNE_FN_INDEX_NONE);
	SG_RuneFnStoreView(&store, &table);
	CHECK(SG_RuneFnTableValid(&table));
	CHECK(table.function_count == 6U);

	CHECK(SG_RuneFnEvaluate(&table, constant, inputs, &value) &&
		Near(value, 1.0f));
	inputs[SG_RUNE_FN_INPUT_DISTANCE] = 10.0f;
	CHECK(SG_RuneFnEvaluate(&table, affine, inputs, &value) &&
		Near(value, 20.5f));
	inputs[SG_RUNE_FN_INPUT_WORLD_Z] = 100.0f;
	inputs[SG_RUNE_FN_INPUT_VELOCITY_Z] = 30.0f;
	inputs[SG_RUNE_FN_INPUT_TIME_SECONDS] = 0.5f;
	/* 100 + (30 + 270) * 0.5 - 400 * 0.25 = 150 */
	CHECK(SG_RuneFnEvaluate(&table, ballistic, inputs, &value) &&
		Near(value, 150.0f));
	inputs[SG_RUNE_FN_INPUT_DISTANCE] = 50.0f;
	CHECK(SG_RuneFnEvaluate(&table, piecewise, inputs, &value) &&
		Near(value, 50.0f));
	inputs[SG_RUNE_FN_INPUT_DISTANCE] = 200.0f;
	CHECK(SG_RuneFnEvaluate(&table, piecewise, inputs, &value) &&
		Near(value, 600.0f));
	/* Out of every clause: no value. */
	inputs[SG_RUNE_FN_INPUT_DISTANCE] = -1.0f;
	CHECK(!SG_RuneFnEvaluate(&table, piecewise, inputs, &value));
	/* Bad references and values are refused. */
	CHECK(!SG_RuneFnEvaluate(&table, 99U, inputs, &value));
	inputs[SG_RUNE_FN_INPUT_DISTANCE] = NAN;
	CHECK(!SG_RuneFnEvaluate(&table, affine, inputs, &value));
	{
		sg_rune_fn_table_t broken = table;
		sg_rune_fn_function_t bad = table.functions[0];

		bad.count = 99U;
		broken.functions = &bad;
		broken.function_count = 1U;
		CHECK(!SG_RuneFnTableValid(&broken));
	}
	SG_RuneFnStoreFree(&store);
	if (failures)
		return 1;
	puts("sg_rune_fn_test: ok");
	return 0;
}
