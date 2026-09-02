/* Era-4 analytic functions.
 *
 * The RUNE stores functions over named inputs, never samples.  Two kinds
 * cover everything the builders emit: a polynomial is a list of terms, each
 * a coefficient times a monomial in up to four inputs; a piecewise function
 * selects one of its clauses by an interval of one input.  Constants,
 * affines, and ballistics are polynomials.  One evaluator serves the
 * generator's checks and the runtime. */
#ifndef SG_RUNE_FN_H
#define SG_RUNE_FN_H

#include <stdint.h>

#define SG_RUNE_FN_MAX_INPUTS 4U
#define SG_RUNE_FN_INDEX_NONE UINT32_MAX

typedef enum sg_rune_fn_input_e
{
	SG_RUNE_FN_INPUT_WORLD_X = 0,
	SG_RUNE_FN_INPUT_WORLD_Y,
	SG_RUNE_FN_INPUT_WORLD_Z,
	SG_RUNE_FN_INPUT_VELOCITY_X,
	SG_RUNE_FN_INPUT_VELOCITY_Y,
	SG_RUNE_FN_INPUT_VELOCITY_Z,
	SG_RUNE_FN_INPUT_DIRECTION_X,
	SG_RUNE_FN_INPUT_DIRECTION_Y,
	SG_RUNE_FN_INPUT_DIRECTION_Z,
	SG_RUNE_FN_INPUT_TIME_SECONDS,
	SG_RUNE_FN_INPUT_DISTANCE,
	SG_RUNE_FN_INPUT_SUPPORT_DISTANCE,
	SG_RUNE_FN_INPUT_FLUID_FRACTION,
	SG_RUNE_FN_INPUT_MOVER_PHASE,
	SG_RUNE_FN_INPUT_HOOK_LENGTH,
	SG_RUNE_FN_INPUT_COUNT
} sg_rune_fn_input_t;

typedef enum sg_rune_fn_output_e
{
	SG_RUNE_FN_OUTPUT_COST = 0,
	SG_RUNE_FN_OUTPUT_TRAVEL_TIME_SECONDS,
	SG_RUNE_FN_OUTPUT_POSITION_X,
	SG_RUNE_FN_OUTPUT_POSITION_Y,
	SG_RUNE_FN_OUTPUT_POSITION_Z,
	SG_RUNE_FN_OUTPUT_VELOCITY_X,
	SG_RUNE_FN_OUTPUT_VELOCITY_Y,
	SG_RUNE_FN_OUTPUT_VELOCITY_Z,
	SG_RUNE_FN_OUTPUT_REACHABILITY,   /* > 0 reachable, < 0 not */
	SG_RUNE_FN_OUTPUT_COUNT
} sg_rune_fn_output_t;

typedef enum sg_rune_fn_kind_e
{
	SG_RUNE_FN_POLYNOMIAL = 0,
	SG_RUNE_FN_PIECEWISE,
	SG_RUNE_FN_KIND_COUNT
} sg_rune_fn_kind_t;

/* coefficient * product over inputs[i] ^ exponents[i]. */
typedef struct sg_rune_fn_term_s
{
	float coefficient;
	uint8_t exponents[SG_RUNE_FN_MAX_INPUTS];
} sg_rune_fn_term_t;

/* Selected when lower <= input < upper (inclusive upper for the last clause
 * is the writer's business: give it +infinity). */
typedef struct sg_rune_fn_clause_s
{
	uint32_t function;
	uint8_t input;
	uint8_t reserved[3];
	float lower;
	float upper;
} sg_rune_fn_clause_t;

typedef struct sg_rune_fn_function_s
{
	uint8_t kind;             /* sg_rune_fn_kind_t */
	uint8_t output;           /* sg_rune_fn_output_t */
	uint8_t input_count;      /* polynomial: inputs used by the terms */
	uint8_t inputs[SG_RUNE_FN_MAX_INPUTS];
	uint8_t reserved;
	uint32_t first;           /* first term, or first clause */
	uint32_t count;
} sg_rune_fn_function_t;

typedef struct sg_rune_fn_table_s
{
	const sg_rune_fn_function_t *functions;
	uint32_t function_count;
	const sg_rune_fn_term_t *terms;
	uint32_t term_count;
	const sg_rune_fn_clause_t *clauses;
	uint32_t clause_count;
} sg_rune_fn_table_t;

/* Evaluates one function over the full input vector.  Returns 0 when the
 * function, a reference, or a value is invalid; the output is then 0. */
int SG_RuneFnEvaluate(const sg_rune_fn_table_t *table,
	uint32_t function, const float inputs[SG_RUNE_FN_INPUT_COUNT],
	float *value_out);

/* Every reference in range, every value finite, every clause well ordered. */
int SG_RuneFnTableValid(const sg_rune_fn_table_t *table);

/* Growable owner used by builders; the table view borrows its arrays. */
typedef struct sg_rune_fn_store_s
{
	sg_rune_fn_function_t *functions;
	uint32_t function_count, function_capacity;
	sg_rune_fn_term_t *terms;
	uint32_t term_count, term_capacity;
	sg_rune_fn_clause_t *clauses;
	uint32_t clause_count, clause_capacity;
} sg_rune_fn_store_t;

void SG_RuneFnStoreInit(sg_rune_fn_store_t *store);
void SG_RuneFnStoreFree(sg_rune_fn_store_t *store);
void SG_RuneFnStoreView(const sg_rune_fn_store_t *store,
	sg_rune_fn_table_t *table_out);

/* Appends a polynomial.  Terms are copied; exponents index the given inputs.
 * Returns the function index or SG_RUNE_FN_INDEX_NONE. */
uint32_t SG_RuneFnAppendPolynomial(sg_rune_fn_store_t *store,
	sg_rune_fn_output_t output, const uint8_t *inputs,
	uint32_t input_count, const sg_rune_fn_term_t *terms,
	uint32_t term_count);

/* A constant, and an affine (bias + slope per input), as the common cases. */
uint32_t SG_RuneFnAppendConstant(sg_rune_fn_store_t *store,
	sg_rune_fn_output_t output, float value);
uint32_t SG_RuneFnAppendAffine(sg_rune_fn_store_t *store,
	sg_rune_fn_output_t output, const uint8_t *inputs,
	uint32_t input_count, float bias, const float *slopes);

uint32_t SG_RuneFnAppendPiecewise(sg_rune_fn_store_t *store,
	sg_rune_fn_output_t output, const sg_rune_fn_clause_t *clauses,
	uint32_t clause_count);

#endif /* SG_RUNE_FN_H */
