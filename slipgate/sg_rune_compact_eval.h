#ifndef SG_RUNE_COMPACT_EVAL_H
#define SG_RUNE_COMPACT_EVAL_H

#include <stdint.h>

#include "sg_rune_compact_analytic.h"

typedef struct sg_rune_compact_eval_input_s
{
	sg_rune_analytic_input_dimension_t dimension;
	float value;
} sg_rune_compact_eval_input_t;

/* Query inputs are keyed by dimension and may appear in any order. A query may
 * include dimensions that the selected function does not use. */
typedef struct sg_rune_compact_eval_query_s
{
	sg_rune_analytic_function_index_t function;
	const sg_rune_compact_eval_input_t *inputs;
	uint32_t input_count;
} sg_rune_compact_eval_query_t;

typedef struct sg_rune_compact_eval_result_s
{
	sg_rune_analytic_output_meaning_t output;
	float value;
} sg_rune_compact_eval_result_t;

typedef enum sg_rune_compact_eval_status_e
{
	SG_RUNE_COMPACT_EVAL_OK = 0,
	SG_RUNE_COMPACT_EVAL_INVALID_ARGUMENT,
	SG_RUNE_COMPACT_EVAL_INVALID_FUNCTION_INDEX,
	SG_RUNE_COMPACT_EVAL_INVALID_QUERY_DIMENSION,
	SG_RUNE_COMPACT_EVAL_DUPLICATE_QUERY_DIMENSION,
	SG_RUNE_COMPACT_EVAL_MISSING_INPUT,
	SG_RUNE_COMPACT_EVAL_NONFINITE_INPUT,
	SG_RUNE_COMPACT_EVAL_INVALID_CONTRACT,
	SG_RUNE_COMPACT_EVAL_NONFINITE_RESULT,
	SG_RUNE_COMPACT_EVAL_STATUS_COUNT
} sg_rune_compact_eval_status_t;

/* Evaluates one scalar function without allocation. The evaluator checks every
 * contract record reached by the query. Call SG_RuneCompactAnalyticValidate at
 * artifact acceptance to validate records that a particular query may not
 * reach. result_out is unchanged on failure. */
sg_rune_compact_eval_status_t SG_RuneCompactEval(
	const sg_rune_compact_analytic_t *analytic,
	const sg_rune_compact_eval_query_t *query,
	sg_rune_compact_eval_result_t *result_out);

const char *SG_RuneCompactEvalStatusString(
	sg_rune_compact_eval_status_t status);

#endif
