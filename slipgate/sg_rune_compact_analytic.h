#ifndef SG_RUNE_COMPACT_ANALYTIC_H
#define SG_RUNE_COMPACT_ANALYTIC_H

#include <stdint.h>

#define SG_RUNE_COMPACT_ANALYTIC_VERSION UINT16_C(1)
#define SG_RUNE_ANALYTIC_MAX_INPUTS UINT32_C(16)
#define SG_RUNE_ANALYTIC_MAX_POLYNOMIAL_DEGREE UINT32_C(6)
#define SG_RUNE_ANALYTIC_MAX_FUNCTIONS UINT32_C(1048576)
#define SG_RUNE_ANALYTIC_MAX_INPUT_DIMENSIONS UINT32_C(16777216)
#define SG_RUNE_ANALYTIC_MAX_AFFINE_SLOPES UINT32_C(16777216)
#define SG_RUNE_ANALYTIC_MAX_POLYNOMIAL_COEFFICIENTS UINT32_C(33554432)
#define SG_RUNE_ANALYTIC_MAX_PIECEWISE_CLAUSES UINT32_C(4194304)
#define SG_RUNE_ANALYTIC_MAX_CLAUSES_PER_PIECE UINT32_C(65536)

typedef struct sg_rune_analytic_scalar_bits_s
{
	uint32_t bits;
} sg_rune_analytic_scalar_bits_t;

#define SG_RUNE_ANALYTIC_INDEX_TYPE(name) \
	typedef struct name##_s { uint32_t value; } name##_t

SG_RUNE_ANALYTIC_INDEX_TYPE(sg_rune_analytic_function_index);

#undef SG_RUNE_ANALYTIC_INDEX_TYPE

#define SG_RUNE_ANALYTIC_SPAN_TYPE(name) \
	typedef struct name##_s { uint32_t first; uint32_t count; } name##_t

SG_RUNE_ANALYTIC_SPAN_TYPE(sg_rune_analytic_input_span);
SG_RUNE_ANALYTIC_SPAN_TYPE(sg_rune_analytic_function_span);
SG_RUNE_ANALYTIC_SPAN_TYPE(sg_rune_analytic_affine_slope_span);
SG_RUNE_ANALYTIC_SPAN_TYPE(sg_rune_analytic_polynomial_coefficient_span);
SG_RUNE_ANALYTIC_SPAN_TYPE(sg_rune_analytic_piecewise_clause_span);

#undef SG_RUNE_ANALYTIC_SPAN_TYPE

typedef enum sg_rune_analytic_input_dimension_e
{
	SG_RUNE_ANALYTIC_INPUT_WORLD_X = 0,
	SG_RUNE_ANALYTIC_INPUT_WORLD_Y,
	SG_RUNE_ANALYTIC_INPUT_WORLD_Z,
	SG_RUNE_ANALYTIC_INPUT_VELOCITY_X,
	SG_RUNE_ANALYTIC_INPUT_VELOCITY_Y,
	SG_RUNE_ANALYTIC_INPUT_VELOCITY_Z,
	SG_RUNE_ANALYTIC_INPUT_DIRECTION_X,
	SG_RUNE_ANALYTIC_INPUT_DIRECTION_Y,
	SG_RUNE_ANALYTIC_INPUT_DIRECTION_Z,
	SG_RUNE_ANALYTIC_INPUT_TIME_SECONDS,
	SG_RUNE_ANALYTIC_INPUT_DISTANCE,
	SG_RUNE_ANALYTIC_INPUT_SUPPORT_DISTANCE,
	SG_RUNE_ANALYTIC_INPUT_FLUID_FRACTION,
	SG_RUNE_ANALYTIC_INPUT_MOVER_PHASE,
	SG_RUNE_ANALYTIC_INPUT_HOOK_LENGTH,
	SG_RUNE_ANALYTIC_INPUT_TARGET_RADIUS,
	SG_RUNE_ANALYTIC_INPUT_DIMENSION_COUNT
} sg_rune_analytic_input_dimension_t;

typedef enum sg_rune_analytic_output_meaning_e
{
	SG_RUNE_ANALYTIC_OUTPUT_COST = 0,
	SG_RUNE_ANALYTIC_OUTPUT_TRAVEL_TIME_SECONDS,
	SG_RUNE_ANALYTIC_OUTPUT_POSITION_X,
	SG_RUNE_ANALYTIC_OUTPUT_POSITION_Y,
	SG_RUNE_ANALYTIC_OUTPUT_POSITION_Z,
	SG_RUNE_ANALYTIC_OUTPUT_VELOCITY_X,
	SG_RUNE_ANALYTIC_OUTPUT_VELOCITY_Y,
	SG_RUNE_ANALYTIC_OUTPUT_VELOCITY_Z,
	SG_RUNE_ANALYTIC_OUTPUT_ACCELERATION_X,
	SG_RUNE_ANALYTIC_OUTPUT_ACCELERATION_Y,
	SG_RUNE_ANALYTIC_OUTPUT_ACCELERATION_Z,
	SG_RUNE_ANALYTIC_OUTPUT_DAMAGE,
	SG_RUNE_ANALYTIC_OUTPUT_HIT_PROBABILITY,
	SG_RUNE_ANALYTIC_OUTPUT_IMPULSE_X,
	SG_RUNE_ANALYTIC_OUTPUT_IMPULSE_Y,
	SG_RUNE_ANALYTIC_OUTPUT_IMPULSE_Z,
	SG_RUNE_ANALYTIC_OUTPUT_VISIBILITY_FRACTION,
	SG_RUNE_ANALYTIC_OUTPUT_CLEARANCE,
	SG_RUNE_ANALYTIC_OUTPUT_REACHABILITY_MARGIN,
	SG_RUNE_ANALYTIC_OUTPUT_FUSE_REMAINING_SECONDS,
	SG_RUNE_ANALYTIC_OUTPUT_MEANING_COUNT
} sg_rune_analytic_output_meaning_t;

typedef enum sg_rune_compact_analytic_form_e
{
	SG_RUNE_COMPACT_ANALYTIC_CONSTANT = 0,
	SG_RUNE_COMPACT_ANALYTIC_AFFINE,
	SG_RUNE_COMPACT_ANALYTIC_POLYNOMIAL,
	SG_RUNE_COMPACT_ANALYTIC_BALLISTIC,
	SG_RUNE_COMPACT_ANALYTIC_PIECEWISE,
	SG_RUNE_COMPACT_ANALYTIC_FORM_COUNT
} sg_rune_compact_analytic_form_t;

/* Each function returns one scalar with the declared meaning. Vector results
 * use one function per component. Definitions are stored in function order,
 * so definition is the zero-based ordinal among functions of the same form. */
typedef struct sg_rune_analytic_function_s
{
	sg_rune_analytic_input_span_t inputs;
	uint32_t definition;
	sg_rune_analytic_output_meaning_t output;
	sg_rune_compact_analytic_form_t form;
} sg_rune_analytic_function_t;

typedef struct sg_rune_analytic_constant_s
{
	/* Constants may declare zero through MAX_INPUTS inputs and ignore them. */
	sg_rune_analytic_scalar_bits_t value;
} sg_rune_analytic_constant_t;

/* Affine functions require at least one input. The value is bias plus the sum
 * of slopes[i] * input[i]. The slope span has exactly one entry per input and
 * stores slopes in input-span order. */
typedef struct sg_rune_analytic_affine_s
{
	sg_rune_analytic_scalar_bits_t bias;
	sg_rune_analytic_affine_slope_span_t slopes;
} sg_rune_analytic_affine_t;

/* Polynomials require one through MAX_INPUTS inputs and degree two through
 * MAX_POLYNOMIAL_DEGREE. The coefficient count is C(inputs + degree, degree).
 * Coefficients use graded lexicographic order. Total degree increases first.
 * Within one degree, earlier input exponents decrease first. For two inputs
 * through degree two, the order is 1, x0, x1, x0^2, x0*x1, x1^2. */
typedef struct sg_rune_analytic_polynomial_s
{
	sg_rune_analytic_polynomial_coefficient_span_t coefficients;
	uint8_t degree;
	uint8_t reserved[3];
} sg_rune_analytic_polynomial_t;

/* value(t) = initial + first_derivative*t + half_second_derivative*t^2.
 * Ballistic functions have exactly one TIME_SECONDS input. */
typedef struct sg_rune_analytic_ballistic_s
{
	sg_rune_analytic_scalar_bits_t initial;
	sg_rune_analytic_scalar_bits_t first_derivative;
	sg_rune_analytic_scalar_bits_t half_second_derivative;
} sg_rune_analytic_ballistic_t;

typedef enum sg_rune_analytic_interval_ownership_e
{
	SG_RUNE_ANALYTIC_INTERVAL_OPEN_OPEN = 0,
	SG_RUNE_ANALYTIC_INTERVAL_CLOSED_OPEN,
	SG_RUNE_ANALYTIC_INTERVAL_OPEN_CLOSED,
	SG_RUNE_ANALYTIC_INTERVAL_CLOSED_CLOSED,
	SG_RUNE_ANALYTIC_INTERVAL_OWNERSHIP_COUNT
} sg_rune_analytic_interval_ownership_t;

/* A clause owns one finite interval of the piecewise selector input. Clauses
 * are ordered by lower bound, cannot overlap, and may leave explicit gaps. */
typedef struct sg_rune_analytic_piecewise_clause_s
{
	sg_rune_analytic_scalar_bits_t lower;
	sg_rune_analytic_scalar_bits_t upper;
	sg_rune_analytic_function_index_t function;
	sg_rune_analytic_interval_ownership_t ownership;
} sg_rune_analytic_piecewise_clause_t;

/* Every clause function precedes its parent, has identical ordered inputs, and
 * returns the same output meaning. selector_input is an input-span ordinal. */
typedef struct sg_rune_analytic_piecewise_s
{
	sg_rune_analytic_piecewise_clause_span_t clauses;
	/* Used outside every clause, so finite clause bounds never leave an
	 * undefined query. The function precedes this piecewise parent. */
	sg_rune_analytic_function_index_t default_function;
	uint32_t selector_input;
} sg_rune_analytic_piecewise_t;

typedef struct sg_rune_compact_analytic_s
{
	uint16_t version;
	uint16_t reserved;
	const sg_rune_analytic_function_t *functions;
	uint32_t function_count;
	const sg_rune_analytic_input_dimension_t *input_dimensions;
	uint32_t input_dimension_count;
	const sg_rune_analytic_constant_t *constants;
	uint32_t constant_count;
	const sg_rune_analytic_affine_t *affines;
	uint32_t affine_count;
	const sg_rune_analytic_scalar_bits_t *affine_slopes;
	uint32_t affine_slope_count;
	const sg_rune_analytic_polynomial_t *polynomials;
	uint32_t polynomial_count;
	const sg_rune_analytic_scalar_bits_t *polynomial_coefficients;
	uint32_t polynomial_coefficient_count;
	const sg_rune_analytic_ballistic_t *ballistics;
	uint32_t ballistic_count;
	const sg_rune_analytic_piecewise_t *piecewise;
	uint32_t piecewise_count;
	const sg_rune_analytic_piecewise_clause_t *piecewise_clauses;
	uint32_t piecewise_clause_count;
} sg_rune_compact_analytic_t;

typedef enum sg_rune_analytic_error_code_e
{
	SG_RUNE_ANALYTIC_ERROR_NONE = 0,
	SG_RUNE_ANALYTIC_ERROR_INVALID_ARGUMENT,
	SG_RUNE_ANALYTIC_ERROR_UNSUPPORTED_VERSION,
	SG_RUNE_ANALYTIC_ERROR_LIMIT_EXCEEDED,
	SG_RUNE_ANALYTIC_ERROR_NONCANONICAL_SCALAR,
	SG_RUNE_ANALYTIC_ERROR_INVALID_DIMENSION,
	SG_RUNE_ANALYTIC_ERROR_INVALID_OUTPUT,
	SG_RUNE_ANALYTIC_ERROR_INVALID_FORM,
	SG_RUNE_ANALYTIC_ERROR_WRONG_ARITY,
	SG_RUNE_ANALYTIC_ERROR_INVALID_DOMAIN,
	SG_RUNE_ANALYTIC_ERROR_NONCANONICAL_ORDER,
	SG_RUNE_ANALYTIC_ERROR_INVALID_REFERENCE,
	SG_RUNE_ANALYTIC_ERROR_CODE_COUNT
} sg_rune_analytic_error_code_t;

typedef enum sg_rune_analytic_record_domain_e
{
	SG_RUNE_ANALYTIC_RECORD_CONTRACT = 0,
	SG_RUNE_ANALYTIC_RECORD_FUNCTION,
	SG_RUNE_ANALYTIC_RECORD_INPUT,
	SG_RUNE_ANALYTIC_RECORD_CONSTANT,
	SG_RUNE_ANALYTIC_RECORD_AFFINE,
	SG_RUNE_ANALYTIC_RECORD_AFFINE_SLOPE,
	SG_RUNE_ANALYTIC_RECORD_POLYNOMIAL,
	SG_RUNE_ANALYTIC_RECORD_POLYNOMIAL_COEFFICIENT,
	SG_RUNE_ANALYTIC_RECORD_BALLISTIC,
	SG_RUNE_ANALYTIC_RECORD_PIECEWISE,
	SG_RUNE_ANALYTIC_RECORD_PIECEWISE_CLAUSE
} sg_rune_analytic_record_domain_t;

typedef struct sg_rune_analytic_error_s
{
	sg_rune_analytic_error_code_t code;
	sg_rune_analytic_record_domain_t domain;
	uint32_t record;
} sg_rune_analytic_error_t;

/* Performs allocation-free validation in one pass over the stored records.
 * Piecewise input comparisons are bounded by MAX_INPUTS. */
int SG_RuneCompactAnalyticValidate(const sg_rune_compact_analytic_t *analytic,
	sg_rune_analytic_error_t *error_out);

/* Returns the dense coefficient count, including the constant term. Zero
 * means the requested input-count or degree is outside the contract. */
uint32_t SG_RuneAnalyticPolynomialCoefficientCount(uint32_t input_count,
	uint32_t degree);

/* Decodes one coefficient's exponents in the ordering above. The caller
 * supplies at least input_count exponent bytes. */
int SG_RuneAnalyticPolynomialExponentAt(uint32_t input_count, uint32_t degree,
	uint32_t coefficient, uint8_t *exponents, uint32_t exponent_capacity);

const char *SG_RuneCompactAnalyticErrorString(
	sg_rune_analytic_error_code_t code);

#endif
