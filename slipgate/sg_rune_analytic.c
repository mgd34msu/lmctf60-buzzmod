#include "sg_rune_analytic.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static int Finite(float value)
{
	return isfinite(value) != 0;
}

static float Power(float base, uint8_t exponent)
{
	float result = 1.0f;

	while (exponent-- > 0U)
		result *= base;
	return result;
}

static int EvaluateFunction(const sg_rune_fn_table_t *table,
	uint32_t function, const float inputs[SG_RUNE_FN_INPUT_COUNT],
	float *value_out, uint32_t depth);

static int EvaluatePolynomial(const sg_rune_fn_table_t *table,
	const sg_rune_fn_function_t *record,
	const float inputs[SG_RUNE_FN_INPUT_COUNT], float *value_out)
{
	float total = 0.0f;
	uint32_t index;

	if (record->input_count > SG_RUNE_FN_MAX_INPUTS ||
		record->first > table->term_count ||
		record->count > table->term_count - record->first)
		return 0;
	for (index = 0U; index < record->input_count; index++)
		if (record->inputs[index] >= SG_RUNE_FN_INPUT_COUNT)
			return 0;
	for (index = 0U; index < record->count; index++)
	{
		const sg_rune_fn_term_t *term = &table->terms[record->first + index];
		float value = term->coefficient;
		uint32_t slot;

		for (slot = 0U; slot < record->input_count; slot++)
			if (term->exponents[slot])
				value *= Power(inputs[record->inputs[slot]], term->exponents[slot]);
		total += value;
	}
	if (!Finite(total))
		return 0;
	*value_out = total;
	return 1;
}

static int EvaluatePiecewise(const sg_rune_fn_table_t *table,
	const sg_rune_fn_function_t *record,
	const float inputs[SG_RUNE_FN_INPUT_COUNT], float *value_out,
	uint32_t depth)
{
	uint32_t index;

	if (record->first > table->clause_count ||
		record->count > table->clause_count - record->first)
		return 0;
	for (index = 0U; index < record->count; index++)
	{
		const sg_rune_fn_clause_t *clause =
			&table->clauses[record->first + index];
		float selector;

		if (clause->input >= SG_RUNE_FN_INPUT_COUNT)
			return 0;
		selector = inputs[clause->input];
		if (selector >= clause->lower && selector < clause->upper)
			return EvaluateFunction(table, clause->function, inputs, value_out,
				depth + 1U);
	}
	return 0;
}

static int EvaluateFunction(const sg_rune_fn_table_t *table,
	uint32_t function, const float inputs[SG_RUNE_FN_INPUT_COUNT],
	float *value_out, uint32_t depth)
{
	const sg_rune_fn_function_t *record;

	if (function >= table->function_count || depth > 8U)
		return 0;
	record = &table->functions[function];
	switch (record->kind)
	{
	case SG_RUNE_FN_POLYNOMIAL:
		return EvaluatePolynomial(table, record, inputs, value_out);
	case SG_RUNE_FN_PIECEWISE:
		return EvaluatePiecewise(table, record, inputs, value_out, depth);
	default:
		return 0;
	}
}

int SG_RuneFnEvaluate(const sg_rune_fn_table_t *table,
	uint32_t function, const float inputs[SG_RUNE_FN_INPUT_COUNT],
	float *value_out)
{
	uint32_t index;

	if (!value_out)
		return 0;
	*value_out = 0.0f;
	if (!table || !inputs)
		return 0;
	for (index = 0U; index < SG_RUNE_FN_INPUT_COUNT; index++)
		if (!Finite(inputs[index]))
			return 0;
	return EvaluateFunction(table, function, inputs, value_out, 0U);
}

int SG_RuneFnTableValid(const sg_rune_fn_table_t *table)
{
	uint32_t index;

	if (!table || (table->function_count && !table->functions) ||
		(table->term_count && !table->terms) ||
		(table->clause_count && !table->clauses))
		return 0;
	for (index = 0U; index < table->function_count; index++)
	{
		const sg_rune_fn_function_t *record = &table->functions[index];
		uint32_t slot;

		if (record->output >= SG_RUNE_FN_OUTPUT_COUNT ||
			record->reserved != 0U)
			return 0;
		if (record->kind == SG_RUNE_FN_POLYNOMIAL)
		{
			if (record->input_count > SG_RUNE_FN_MAX_INPUTS ||
				record->first > table->term_count ||
				record->count > table->term_count - record->first)
				return 0;
			for (slot = 0U; slot < record->input_count; slot++)
				if (record->inputs[slot] >= SG_RUNE_FN_INPUT_COUNT)
					return 0;
			for (slot = 0U; slot < record->count; slot++)
			{
				const sg_rune_fn_term_t *term =
					&table->terms[record->first + slot];
				uint32_t axis;

				if (!Finite(term->coefficient))
					return 0;
				for (axis = record->input_count;
					axis < SG_RUNE_FN_MAX_INPUTS; axis++)
					if (term->exponents[axis] != 0U)
						return 0;
			}
		}
		else if (record->kind == SG_RUNE_FN_PIECEWISE)
		{
			if (record->first > table->clause_count ||
				record->count > table->clause_count - record->first ||
				record->count == 0U)
				return 0;
			for (slot = 0U; slot < record->count; slot++)
			{
				const sg_rune_fn_clause_t *clause =
					&table->clauses[record->first + slot];

				if (clause->function >= table->function_count ||
					clause->function == index ||
					clause->input >= SG_RUNE_FN_INPUT_COUNT ||
					!(clause->lower < clause->upper) ||
					!Finite(clause->lower) || isnan(clause->upper) ||
					clause->reserved[0] || clause->reserved[1] ||
					clause->reserved[2])
					return 0;
			}
		}
		else
			return 0;
	}
	return 1;
}

/* ---- store ---------------------------------------------------------------- */

void SG_RuneFnStoreInit(sg_rune_fn_store_t *store)
{
	if (store)
		memset(store, 0, sizeof(*store));
}

void SG_RuneFnStoreFree(sg_rune_fn_store_t *store)
{
	if (!store)
		return;
	free(store->functions);
	free(store->terms);
	free(store->clauses);
	memset(store, 0, sizeof(*store));
}

void SG_RuneFnStoreView(const sg_rune_fn_store_t *store,
	sg_rune_fn_table_t *table_out)
{
	if (!table_out)
		return;
	memset(table_out, 0, sizeof(*table_out));
	if (!store)
		return;
	table_out->functions = store->functions;
	table_out->function_count = store->function_count;
	table_out->terms = store->terms;
	table_out->term_count = store->term_count;
	table_out->clauses = store->clauses;
	table_out->clause_count = store->clause_count;
}

static int Grow(void **array, uint32_t *capacity, uint32_t required,
	size_t element)
{
	uint32_t next;
	void *grown;

	if (required <= *capacity)
		return 1;
	next = *capacity ? *capacity : 64U;
	while (next < required)
	{
		if (next > UINT32_MAX / 2U)
			return 0;
		next *= 2U;
	}
	grown = realloc(*array, (size_t)next * element);
	if (!grown)
		return 0;
	*array = grown;
	*capacity = next;
	return 1;
}

static uint32_t AppendFunction(sg_rune_fn_store_t *store,
	const sg_rune_fn_function_t *record)
{
	if (!Grow((void **)&store->functions, &store->function_capacity,
		store->function_count + 1U, sizeof(*store->functions)))
		return SG_RUNE_FN_INDEX_NONE;
	store->functions[store->function_count] = *record;
	return store->function_count++;
}

uint32_t SG_RuneFnAppendPolynomial(sg_rune_fn_store_t *store,
	sg_rune_fn_output_t output, const uint8_t *inputs,
	uint32_t input_count, const sg_rune_fn_term_t *terms,
	uint32_t term_count)
{
	sg_rune_fn_function_t record;
	uint32_t index;

	if (!store || input_count > SG_RUNE_FN_MAX_INPUTS ||
		(input_count && !inputs) || (term_count && !terms) ||
		output >= SG_RUNE_FN_OUTPUT_COUNT)
		return SG_RUNE_FN_INDEX_NONE;
	for (index = 0U; index < input_count; index++)
		if (inputs[index] >= SG_RUNE_FN_INPUT_COUNT)
			return SG_RUNE_FN_INDEX_NONE;
	for (index = 0U; index < term_count; index++)
	{
		uint32_t axis;

		if (!Finite(terms[index].coefficient))
			return SG_RUNE_FN_INDEX_NONE;
		for (axis = input_count; axis < SG_RUNE_FN_MAX_INPUTS; axis++)
			if (terms[index].exponents[axis])
				return SG_RUNE_FN_INDEX_NONE;
	}
	if (!Grow((void **)&store->terms, &store->term_capacity,
		store->term_count + term_count, sizeof(*store->terms)))
		return SG_RUNE_FN_INDEX_NONE;
	memset(&record, 0, sizeof(record));
	record.kind = SG_RUNE_FN_POLYNOMIAL;
	record.output = (uint8_t)output;
	record.input_count = (uint8_t)input_count;
	for (index = 0U; index < input_count; index++)
		record.inputs[index] = inputs[index];
	record.first = store->term_count;
	record.count = term_count;
	if (term_count)
		memcpy(&store->terms[store->term_count], terms,
			(size_t)term_count * sizeof(*terms));
	store->term_count += term_count;
	return AppendFunction(store, &record);
}

uint32_t SG_RuneFnAppendConstant(sg_rune_fn_store_t *store,
	sg_rune_fn_output_t output, float value)
{
	sg_rune_fn_term_t term;

	memset(&term, 0, sizeof(term));
	term.coefficient = value;
	return SG_RuneFnAppendPolynomial(store, output, NULL, 0U, &term, 1U);
}

uint32_t SG_RuneFnAppendAffine(sg_rune_fn_store_t *store,
	sg_rune_fn_output_t output, const uint8_t *inputs,
	uint32_t input_count, float bias, const float *slopes)
{
	sg_rune_fn_term_t terms[SG_RUNE_FN_MAX_INPUTS + 1U];
	uint32_t index, count = 0U;

	if (input_count > SG_RUNE_FN_MAX_INPUTS || (input_count && !slopes))
		return SG_RUNE_FN_INDEX_NONE;
	memset(terms, 0, sizeof(terms));
	if (bias != 0.0f || input_count == 0U)
		terms[count++].coefficient = bias;
	for (index = 0U; index < input_count; index++)
	{
		if (slopes[index] == 0.0f)
			continue;
		terms[count].coefficient = slopes[index];
		terms[count].exponents[index] = 1U;
		count++;
	}
	return SG_RuneFnAppendPolynomial(store, output, inputs, input_count,
		terms, count);
}

uint32_t SG_RuneFnAppendPiecewise(sg_rune_fn_store_t *store,
	sg_rune_fn_output_t output, const sg_rune_fn_clause_t *clauses,
	uint32_t clause_count)
{
	sg_rune_fn_function_t record;

	if (!store || !clauses || !clause_count ||
		output >= SG_RUNE_FN_OUTPUT_COUNT ||
		!Grow((void **)&store->clauses, &store->clause_capacity,
			store->clause_count + clause_count, sizeof(*store->clauses)))
		return SG_RUNE_FN_INDEX_NONE;
	memset(&record, 0, sizeof(record));
	record.kind = SG_RUNE_FN_PIECEWISE;
	record.output = (uint8_t)output;
	record.first = store->clause_count;
	record.count = clause_count;
	memcpy(&store->clauses[store->clause_count], clauses,
		(size_t)clause_count * sizeof(*clauses));
	store->clause_count += clause_count;
	return AppendFunction(store, &record);
}
