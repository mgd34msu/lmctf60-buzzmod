#include "sg_rune_dynamics_model_internal.h"

#include <float.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define SG_RUNE_BINARY32_RANK_PRIME_COUNT 68U
#define SG_RUNE_EXACT_INTEGER_LIMB_COUNT 64U

_Static_assert(CHAR_BIT == 8, "binary32 rank requires eight-bit bytes");
_Static_assert(sizeof(float) == sizeof(uint32_t),
	"binary32 rank requires four-byte float");
_Static_assert(FLT_RADIX == 2 && FLT_MANT_DIG == 24 &&
	FLT_MIN_EXP == -125 && FLT_MAX_EXP == 128,
	"binary32 rank requires IEC 60559 binary32 float");
/* Aligned binary32 entries are below 2^278, so a 7x7 determinant is below
 * 7! * 2^(278*7) < 2^1959. A UINT32_MAX-term volume sum is below 2^1991;
 * 64 limbs are therefore a format-derived bound, not a work budget. */
_Static_assert(SG_RUNE_EXACT_INTEGER_LIMB_COUNT * 32U >= 1991U,
	"exact integer must cover every version-4 geometry certificate");
_Static_assert(SG_RUNE_BINARY32_RANK_PRIME_COUNT * 29U > 1959U,
	"rank moduli must exceed the exact determinant bound");

typedef struct sg_rune_binary32_dyadic_s
{
	uint32_t mantissa;
	int exponent;
	int negative;
} sg_rune_binary32_dyadic_t;

typedef struct sg_rune_exact_integer_s
{
	uint32_t limb[SG_RUNE_EXACT_INTEGER_LIMB_COUNT];
	size_t count;
	int negative;
	int overflow;
} sg_rune_exact_integer_t;

static const uint32_t rank_primes[] = {
	UINT32_C(1000000007), UINT32_C(1000000009),
	UINT32_C(1000000021), UINT32_C(1000000033),
	UINT32_C(1000000087), UINT32_C(1000000093),
	UINT32_C(1000000097), UINT32_C(1000000103),
	UINT32_C(1000000123), UINT32_C(1000000181),
	UINT32_C(1000000207), UINT32_C(1000000223),
	UINT32_C(1000000241), UINT32_C(1000000271),
	UINT32_C(1000000289), UINT32_C(1000000297),
	UINT32_C(1000000321), UINT32_C(1000000349),
	UINT32_C(1000000363), UINT32_C(1000000403),
	UINT32_C(1000000409), UINT32_C(1000000411),
	UINT32_C(1000000427), UINT32_C(1000000433),
	UINT32_C(1000000439), UINT32_C(1000000447),
	UINT32_C(1000000453), UINT32_C(1000000459),
	UINT32_C(1000000483), UINT32_C(1000000513),
	UINT32_C(1000000531), UINT32_C(1000000579),
	UINT32_C(1000000607), UINT32_C(1000000613),
	UINT32_C(1000000637), UINT32_C(1000000663),
	UINT32_C(1000000711), UINT32_C(1000000753),
	UINT32_C(1000000787), UINT32_C(1000000801),
	UINT32_C(1000000829), UINT32_C(1000000861),
	UINT32_C(1000000871), UINT32_C(1000000891),
	UINT32_C(1000000901), UINT32_C(1000000919),
	UINT32_C(1000000931), UINT32_C(1000000933),
	UINT32_C(1000000993), UINT32_C(1000001011),
	UINT32_C(1000001021), UINT32_C(1000001053),
	UINT32_C(1000001087), UINT32_C(1000001099),
	UINT32_C(1000001137), UINT32_C(1000001161),
	UINT32_C(1000001203), UINT32_C(1000001213),
	UINT32_C(1000001237), UINT32_C(1000001263),
	UINT32_C(1000001269), UINT32_C(1000001273),
	UINT32_C(1000001279), UINT32_C(1000001311),
	UINT32_C(1000001329), UINT32_C(1000001333),
	UINT32_C(1000001351), UINT32_C(1000001371)
};

_Static_assert(sizeof(rank_primes) / sizeof(rank_primes[0]) ==
	SG_RUNE_BINARY32_RANK_PRIME_COUNT, "rank modulus count changed");

static float VertexCoordinate(const sg_rune_state_vertex_t *vertex,
	uint32_t dimension)
{
	if (dimension < 3U)
		return vertex->position.value[dimension];
	if (dimension < 6U)
		return vertex->velocity.value[dimension - 3U];
	return vertex->elapsed_ms;
}

static const sg_rune_interval_t *EmbeddingInterval(
	const sg_rune_state_embedding_t *embedding, uint32_t dimension)
{
	if (dimension < 3U)
		return dimension == 0U ? &embedding->position.x :
			dimension == 1U ? &embedding->position.y :
			&embedding->position.z;
	if (dimension < 6U)
	{
		dimension -= 3U;
		return dimension == 0U ? &embedding->velocity.x :
			dimension == 1U ? &embedding->velocity.y :
			&embedding->velocity.z;
	}
	return &embedding->elapsed_ms;
}

static int VertexInsideEmbedding(const sg_rune_state_vertex_t *vertex,
	const sg_rune_state_embedding_t *embedding)
{
	uint32_t dimension;

	for (dimension = 0U; dimension < SG_RUNE_STATE_DIMENSION_COUNT;
	     dimension++)
	{
		const sg_rune_interval_t *interval =
			EmbeddingInterval(embedding, dimension);
		float coordinate = VertexCoordinate(vertex, dimension);

		if (coordinate < interval->min_value ||
		    coordinate > interval->max_value)
			return 0;
	}
	return 1;
}

static sg_rune_binary32_dyadic_t Binary32Dyadic(float value)
{
	sg_rune_binary32_dyadic_t result;
	uint32_t bits;
	uint32_t encoded_exponent;

	memcpy(&bits, &value, sizeof(bits));
	encoded_exponent = (bits >> 23U) & UINT32_C(0xff);
	result.negative = (bits >> 31U) != 0U;
	result.mantissa = bits & UINT32_C(0x7fffff);
	if (encoded_exponent == 0U)
		result.exponent = -149;
	else
	{
		result.mantissa |= UINT32_C(0x800000);
		result.exponent = (int)encoded_exponent - 150;
	}
	return result;
}

static void ExactNormalize(sg_rune_exact_integer_t *value)
{
	while (value->count != 0U && value->limb[value->count - 1U] == 0U)
		value->count--;
	if (value->count == 0U)
		value->negative = 0;
}

static int ExactMagnitudeCompare(const sg_rune_exact_integer_t *left,
	const sg_rune_exact_integer_t *right)
{
	size_t limb;
	if (left->count != right->count)
		return left->count < right->count ? -1 : 1;
	limb = left->count;
	while (limb != 0U)
	{
		limb--;
		if (left->limb[limb] != right->limb[limb])
			return left->limb[limb] < right->limb[limb] ? -1 : 1;
	}
	return 0;
}

static sg_rune_exact_integer_t ExactMagnitudeAdd(
	const sg_rune_exact_integer_t *left,
	const sg_rune_exact_integer_t *right)
{
	sg_rune_exact_integer_t result = { { 0U }, 0U, 0, 0 };
	size_t count = left->count > right->count ? left->count : right->count;
	uint64_t carry = 0U;
	size_t limb;
	for (limb = 0U; limb < count; limb++)
	{
		uint64_t sum = carry;
		if (limb < left->count)
			sum += left->limb[limb];
		if (limb < right->count)
			sum += right->limb[limb];
		result.limb[limb] = (uint32_t)sum;
		carry = sum >> 32U;
	}
	result.count = count;
	if (carry != 0U)
	{
		if (count == SG_RUNE_EXACT_INTEGER_LIMB_COUNT)
			result.overflow = 1;
		else
		{
			result.limb[count] = (uint32_t)carry;
			result.count++;
		}
	}
	return result;
}

static sg_rune_exact_integer_t ExactMagnitudeSubtract(
	const sg_rune_exact_integer_t *larger,
	const sg_rune_exact_integer_t *smaller)
{
	sg_rune_exact_integer_t result = *larger;
	uint64_t borrow = 0U;
	size_t limb;
	for (limb = 0U; limb < larger->count; limb++)
	{
		uint64_t subtrahend = borrow;
		uint64_t minuend = larger->limb[limb];
		if (limb < smaller->count)
			subtrahend += smaller->limb[limb];
		result.limb[limb] = (uint32_t)(minuend - subtrahend);
		borrow = minuend < subtrahend ? 1U : 0U;
	}
	ExactNormalize(&result);
	return result;
}

static sg_rune_exact_integer_t ExactAdd(
	const sg_rune_exact_integer_t *left,
	const sg_rune_exact_integer_t *right)
{
	sg_rune_exact_integer_t result;
	int order;
	if (left->overflow || right->overflow)
	{
		result = *left;
		result.overflow = 1;
		return result;
	}
	if (left->negative == right->negative)
	{
		result = ExactMagnitudeAdd(left, right);
		result.negative = left->negative;
		return result;
	}
	order = ExactMagnitudeCompare(left, right);
	if (order == 0)
	{
		memset(&result, 0, sizeof(result));
		return result;
	}
	result = order > 0 ? ExactMagnitudeSubtract(left, right) :
		ExactMagnitudeSubtract(right, left);
	result.negative = order > 0 ? left->negative : right->negative;
	return result;
}

static sg_rune_exact_integer_t ExactMultiply(
	const sg_rune_exact_integer_t *left,
	const sg_rune_exact_integer_t *right)
{
	sg_rune_exact_integer_t result = { { 0U }, 0U, 0, 0 };
	size_t left_limb;
	if (left->overflow || right->overflow)
	{
		result.overflow = 1;
		return result;
	}
	if (left->count == 0U || right->count == 0U)
		return result;
	if (left->count > SG_RUNE_EXACT_INTEGER_LIMB_COUNT - right->count + 1U)
	{
		result.overflow = 1;
		return result;
	}
	for (left_limb = 0U; left_limb < left->count; left_limb++)
	{
		uint64_t carry = 0U;
		size_t right_limb;
		for (right_limb = 0U; right_limb < right->count; right_limb++)
		{
			size_t output = left_limb + right_limb;
			uint64_t product = (uint64_t)left->limb[left_limb] *
				right->limb[right_limb] + result.limb[output] + carry;
			result.limb[output] = (uint32_t)product;
			carry = product >> 32U;
		}
		if (carry != 0U)
		{
			size_t output = left_limb + right->count;
			if (output >= SG_RUNE_EXACT_INTEGER_LIMB_COUNT)
			{
				result.overflow = 1;
				return result;
			}
			result.limb[output] = (uint32_t)carry;
		}
	}
	result.count = left->count + right->count;
	if (result.count > SG_RUNE_EXACT_INTEGER_LIMB_COUNT)
		result.count = SG_RUNE_EXACT_INTEGER_LIMB_COUNT;
	result.negative = left->negative != right->negative;
	ExactNormalize(&result);
	return result;
}

static sg_rune_exact_integer_t ExactMagnitudeShiftLeft(
	const sg_rune_exact_integer_t *value, uint32_t shift)
{
	sg_rune_exact_integer_t result = { { 0U }, 0U, 0, 0 };
	size_t whole = shift / 32U;
	uint32_t bits = shift % 32U;
	size_t index;
	uint64_t carry = 0U;

	if (value->overflow)
	{
		result.overflow = 1;
		return result;
	}
	if (value->count == 0U)
		return result;
	if (whole >= SG_RUNE_EXACT_INTEGER_LIMB_COUNT ||
	    value->count > SG_RUNE_EXACT_INTEGER_LIMB_COUNT - whole)
	{
		result.overflow = 1;
		return result;
	}
	for (index = 0U; index < value->count; index++)
	{
		uint64_t expanded = ((uint64_t)value->limb[index] << bits) | carry;
		result.limb[whole + index] = (uint32_t)expanded;
		carry = expanded >> 32U;
	}
	result.count = whole + value->count;
	if (carry != 0U)
	{
		if (result.count == SG_RUNE_EXACT_INTEGER_LIMB_COUNT)
			result.overflow = 1;
		else
			result.limb[result.count++] = (uint32_t)carry;
	}
	result.negative = value->negative;
	return result;
}

static uint32_t ExactTrailingZeroBits(const sg_rune_exact_integer_t *value)
{
	uint32_t result = 0U;
	size_t index = 0U;
	uint32_t word;
	if (value->count == 0U)
		return 0U;
	while (index < value->count && value->limb[index] == 0U)
	{
		result += 32U;
		index++;
	}
	word = value->limb[index];
	while ((word & 1U) == 0U)
	{
		result++;
		word >>= 1U;
	}
	return result;
}

static sg_rune_exact_integer_t ExactMagnitudeShiftRight(
	const sg_rune_exact_integer_t *value, uint32_t shift)
{
	sg_rune_exact_integer_t result = { { 0U }, 0U, 0, 0 };
	size_t whole = shift / 32U;
	uint32_t bits = shift % 32U;
	size_t output;

	if (value->overflow || whole >= value->count)
		return result;
	result.count = value->count - whole;
	for (output = 0U; output < result.count; output++)
	{
		size_t source = output + whole;
		uint64_t combined = value->limb[source];
		if (bits != 0U && source + 1U < value->count)
			combined |= (uint64_t)value->limb[source + 1U] << 32U;
		result.limb[output] = (uint32_t)(combined >> bits);
	}
	result.negative = value->negative;
	ExactNormalize(&result);
	return result;
}

static sg_rune_exact_integer_t ExactFromDyadic(
	const sg_rune_binary32_dyadic_t *value, int common_exponent)
{
	sg_rune_exact_integer_t result = { { 0U }, 0U, 0, 0 };
	uint32_t shift;
	size_t limb;
	uint32_t offset;
	uint64_t shifted;
	if (value->mantissa == 0U)
		return result;
	shift = (uint32_t)(value->exponent - common_exponent);
	limb = shift / 32U;
	offset = shift % 32U;
	shifted = (uint64_t)value->mantissa << offset;
	if (limb >= SG_RUNE_EXACT_INTEGER_LIMB_COUNT)
	{
		result.overflow = 1;
		return result;
	}
	result.limb[limb] = (uint32_t)shifted;
	result.count = limb + 1U;
	if ((shifted >> 32U) != 0U)
	{
		if (result.count == SG_RUNE_EXACT_INTEGER_LIMB_COUNT)
			result.overflow = 1;
		else
		{
			result.limb[result.count] = (uint32_t)(shifted >> 32U);
			result.count++;
		}
	}
	result.negative = value->negative;
	return result;
}

static sg_rune_exact_integer_t ExactDeterminantOrder(
	sg_rune_exact_integer_t matrix[SG_RUNE_STATE_DIMENSION_COUNT]
		[SG_RUNE_STATE_DIMENSION_COUNT], uint32_t order)
{
	sg_rune_exact_integer_t partial[UINT32_C(1) <<
		SG_RUNE_STATE_DIMENSION_COUNT];
	uint32_t mask;

	memset(partial, 0, sizeof(partial));
	partial[0].limb[0] = 1U;
	partial[0].count = 1U;
	for (mask = 1U; mask < (UINT32_C(1) << order); mask++)
	{
		uint32_t row = 0U;
		uint32_t cursor;
		uint32_t column;
		for (cursor = mask; cursor != 0U; cursor &= cursor - 1U)
			row++;
		row--;
		for (column = 0U; column < order; column++)
			if ((mask & (UINT32_C(1) << column)) != 0U)
			{
				uint32_t lower = mask & ((UINT32_C(1) << column) - 1U);
				uint32_t position = 0U;
				sg_rune_exact_integer_t term;
				for (cursor = lower; cursor != 0U; cursor &= cursor - 1U)
					position++;
				term = ExactMultiply(&partial[mask ^
					(UINT32_C(1) << column)], &matrix[row][column]);
				if (((row + position) & 1U) != 0U && term.count != 0U)
					term.negative = !term.negative;
				partial[mask] = ExactAdd(&partial[mask], &term);
			}
	}
	return partial[(UINT32_C(1) << order) - 1U];
}

static sg_rune_exact_integer_t ExactDeterminant(
	sg_rune_exact_integer_t matrix[SG_RUNE_STATE_DIMENSION_COUNT]
		[SG_RUNE_STATE_DIMENSION_COUNT])
{
	return ExactDeterminantOrder(matrix, SG_RUNE_STATE_DIMENSION_COUNT);
}

static uint32_t MultiplyModulo(uint32_t left, uint32_t right,
	uint32_t modulus)
{
	return (uint32_t)(((uint64_t)left * right) % modulus);
}

static uint32_t PowerModulo(uint32_t base, uint32_t exponent,
	uint32_t modulus)
{
	uint32_t result = 1U;

	while (exponent != 0U)
	{
		if ((exponent & 1U) != 0U)
			result = MultiplyModulo(result, base, modulus);
		base = MultiplyModulo(base, base, modulus);
		exponent >>= 1U;
	}
	return result;
}

static uint32_t DyadicModulo(const sg_rune_binary32_dyadic_t *value,
	int common_exponent, uint32_t modulus)
{
	uint32_t shift;
	uint32_t result;

	if (value->mantissa == 0U)
		return 0U;
	shift = (uint32_t)(value->exponent - common_exponent);
	result = MultiplyModulo(value->mantissa,
		PowerModulo(2U, shift, modulus), modulus);

	if (value->negative && result != 0U)
		result = modulus - result;
	return result;
}

static uint32_t SubtractModulo(uint32_t left, uint32_t right,
	uint32_t modulus)
{
	return left >= right ? left - right : left + modulus - right;
}

static int RankSevenModulo(
	sg_rune_binary32_dyadic_t coordinates
		[SG_RUNE_STATE_DIMENSION_COUNT + 1U]
		[SG_RUNE_STATE_DIMENSION_COUNT],
	const int common_exponents[SG_RUNE_STATE_DIMENSION_COUNT],
	uint32_t modulus)
{
	uint32_t matrix[SG_RUNE_STATE_DIMENSION_COUNT]
		[SG_RUNE_STATE_DIMENSION_COUNT];
	uint32_t row;
	uint32_t column;

	for (row = 0U; row < SG_RUNE_STATE_DIMENSION_COUNT; row++)
		for (column = 0U; column < SG_RUNE_STATE_DIMENSION_COUNT;
		     column++)
		{
			uint32_t value = DyadicModulo(&coordinates[row + 1U][column],
				common_exponents[column], modulus);
			uint32_t origin = DyadicModulo(&coordinates[0][column],
				common_exponents[column], modulus);
			matrix[row][column] = SubtractModulo(value, origin, modulus);
		}
	for (column = 0U; column < SG_RUNE_STATE_DIMENSION_COUNT; column++)
	{
		uint32_t pivot = column;
		uint32_t inverse;

		while (pivot < SG_RUNE_STATE_DIMENSION_COUNT &&
		       matrix[pivot][column] == 0U)
			pivot++;
		if (pivot == SG_RUNE_STATE_DIMENSION_COUNT)
			return 0;
		if (pivot != column)
			for (row = column; row < SG_RUNE_STATE_DIMENSION_COUNT; row++)
			{
				uint32_t temporary = matrix[column][row];
				matrix[column][row] = matrix[pivot][row];
				matrix[pivot][row] = temporary;
			}
		inverse = PowerModulo(matrix[column][column], modulus - 2U,
			modulus);
		for (row = column + 1U; row < SG_RUNE_STATE_DIMENSION_COUNT; row++)
		{
			uint32_t factor = MultiplyModulo(matrix[row][column], inverse,
				modulus);
			uint32_t trailing;
			for (trailing = column;
			     trailing < SG_RUNE_STATE_DIMENSION_COUNT; trailing++)
				matrix[row][trailing] = SubtractModulo(
					matrix[row][trailing],
					MultiplyModulo(factor, matrix[column][trailing],
						modulus), modulus);
		}
	}
	return 1;
}

static uint8_t OperatorRankModulo(
	sg_rune_binary32_dyadic_t
		coordinates[SG_RUNE_STATE_DIMENSION_COUNT]
			[SG_RUNE_STATE_DIMENSION_COUNT],
	const int common_exponents[SG_RUNE_STATE_DIMENSION_COUNT],
	uint32_t modulus)
{
	uint32_t matrix[SG_RUNE_STATE_DIMENSION_COUNT]
		[SG_RUNE_STATE_DIMENSION_COUNT];
	uint32_t pivot_row = 0U;
	uint32_t column;
	uint32_t row;

	for (row = 0U; row < SG_RUNE_STATE_DIMENSION_COUNT; row++)
		for (column = 0U; column < SG_RUNE_STATE_DIMENSION_COUNT; column++)
			matrix[row][column] = DyadicModulo(&coordinates[row][column],
				common_exponents[column], modulus);
	for (column = 0U; column < SG_RUNE_STATE_DIMENSION_COUNT &&
	     pivot_row < SG_RUNE_STATE_DIMENSION_COUNT; column++)
	{
		uint32_t pivot = pivot_row;
		uint32_t inverse;

		while (pivot < SG_RUNE_STATE_DIMENSION_COUNT &&
		       matrix[pivot][column] == 0U)
			pivot++;
		if (pivot == SG_RUNE_STATE_DIMENSION_COUNT)
			continue;
		if (pivot != pivot_row)
			for (row = column; row < SG_RUNE_STATE_DIMENSION_COUNT; row++)
			{
				uint32_t temporary = matrix[pivot_row][row];
				matrix[pivot_row][row] = matrix[pivot][row];
				matrix[pivot][row] = temporary;
			}
		inverse = PowerModulo(matrix[pivot_row][column], modulus - 2U,
			modulus);
		for (row = pivot_row + 1U; row < SG_RUNE_STATE_DIMENSION_COUNT;
		     row++)
		{
			uint32_t factor = MultiplyModulo(matrix[row][column], inverse,
				modulus);
			uint32_t trailing;
			for (trailing = column;
			     trailing < SG_RUNE_STATE_DIMENSION_COUNT; trailing++)
				matrix[row][trailing] = SubtractModulo(
					matrix[row][trailing],
					MultiplyModulo(factor, matrix[pivot_row][trailing],
						modulus), modulus);
		}
		pivot_row++;
	}
	return (uint8_t)pivot_row;
}

uint8_t SG_RuneAffineOperatorRankExact(
	const sg_rune_affine_state_operator_t *operator)
{
	sg_rune_binary32_dyadic_t coordinates[SG_RUNE_STATE_DIMENSION_COUNT]
		[SG_RUNE_STATE_DIMENSION_COUNT];
	int common_exponents[SG_RUNE_STATE_DIMENSION_COUNT];
	uint8_t rank = 0U;
	uint32_t column;
	uint32_t row;
	size_t prime;

	for (column = 0U; column < SG_RUNE_STATE_DIMENSION_COUNT; column++)
	{
		int have_nonzero = 0;
		common_exponents[column] = 0;
		for (row = 0U; row < SG_RUNE_STATE_DIMENSION_COUNT; row++)
		{
			coordinates[row][column] =
				Binary32Dyadic(operator->coefficient[row][column]);
			if (coordinates[row][column].mantissa != 0U &&
			    (!have_nonzero || coordinates[row][column].exponent <
				common_exponents[column]))
			{
				common_exponents[column] =
					coordinates[row][column].exponent;
				have_nonzero = 1;
			}
		}
	}
	for (prime = 0U; prime < SG_RUNE_BINARY32_RANK_PRIME_COUNT; prime++)
	{
		uint8_t candidate = OperatorRankModulo(coordinates, common_exponents,
			rank_primes[prime]);
		if (candidate > rank)
			rank = candidate;
		if (rank == SG_RUNE_STATE_DIMENSION_COUNT)
			break;
	}
	return rank;
}

static int SimplexFullRank(const sg_rune_state_simplex_t *simplex,
	const sg_rune_state_vertex_t *vertices)
{
	sg_rune_binary32_dyadic_t coordinates
		[SG_RUNE_STATE_DIMENSION_COUNT + 1U]
		[SG_RUNE_STATE_DIMENSION_COUNT];
	int common_exponents[SG_RUNE_STATE_DIMENSION_COUNT];
	uint32_t vertex;
	uint32_t dimension;
	size_t prime;

	for (dimension = 0U; dimension < SG_RUNE_STATE_DIMENSION_COUNT;
	     dimension++)
	{
		int have_nonzero = 0;

		for (vertex = 0U; vertex <= SG_RUNE_STATE_DIMENSION_COUNT; vertex++)
		{
			coordinates[vertex][dimension] = Binary32Dyadic(
				VertexCoordinate(&vertices[simplex->vertices.first + vertex],
					dimension));
			if (coordinates[vertex][dimension].mantissa != 0U &&
			    (!have_nonzero || coordinates[vertex][dimension].exponent <
				common_exponents[dimension]))
			{
				common_exponents[dimension] =
					coordinates[vertex][dimension].exponent;
				have_nonzero = 1;
			}
		}
		if (!have_nonzero)
			return 0;
	}
	for (prime = 0U; prime < SG_RUNE_BINARY32_RANK_PRIME_COUNT; prime++)
		if (RankSevenModulo(coordinates, common_exponents,
			rank_primes[prime]))
			return 1;
	return 0;
}

static float RefinementCoordinate(const sg_field_refinement_vertex_t *vertex,
	uint32_t dimension)
{
	if (dimension < 3U)
		return vertex->position.value[dimension];
	if (dimension < 6U)
		return vertex->velocity.value[dimension - 3U];
	return vertex->elapsed_ms;
}

static int RefinementCoordinatesEqual(
	const sg_field_refinement_vertex_t *left,
	const sg_field_refinement_vertex_t *right)
{
	uint32_t dimension;
	for (dimension = 0U; dimension < SG_RUNE_STATE_DIMENSION_COUNT;
	     dimension++)
	{
		uint32_t left_bits;
		uint32_t right_bits;
		float left_value = RefinementCoordinate(left, dimension);
		float right_value = RefinementCoordinate(right, dimension);
		memcpy(&left_bits, &left_value, sizeof(left_bits));
		memcpy(&right_bits, &right_value, sizeof(right_bits));
		if (left_bits != right_bits)
			return 0;
	}
	return 1;
}

static int RefinementCoordinateCompare(
	const sg_field_refinement_vertex_t *left,
	const sg_field_refinement_vertex_t *right)
{
	uint32_t dimension;
	for (dimension = 0U; dimension < SG_RUNE_STATE_DIMENSION_COUNT;
	     dimension++)
	{
		float left_value = RefinementCoordinate(left, dimension);
		float right_value = RefinementCoordinate(right, dimension);
		if (left_value < right_value)
			return -1;
		if (left_value > right_value)
			return 1;
	}
	return 0;
}

static void SortRefinementVertices(
	const sg_field_refinement_vertex_t **vertices, uint32_t count)
{
	uint32_t index;
	for (index = 1U; index < count; index++)
	{
		const sg_field_refinement_vertex_t *value = vertices[index];
		uint32_t cursor = index;
		while (cursor != 0U && RefinementCoordinateCompare(
			vertices[cursor - 1U], value) > 0)
		{
			vertices[cursor] = vertices[cursor - 1U];
			cursor--;
		}
		vertices[cursor] = value;
	}
}

static sg_rune_exact_integer_t ExactSubtract(
	const sg_rune_exact_integer_t *left,
	const sg_rune_exact_integer_t *right)
{
	sg_rune_exact_integer_t negative_right = *right;
	if (negative_right.count != 0U)
		negative_right.negative = !negative_right.negative;
	return ExactAdd(left, &negative_right);
}

int SG_FieldRefinementVertexExactMidpoint(
	const sg_field_refinement_vertex_t *middle,
	const sg_field_refinement_vertex_t *left,
	const sg_field_refinement_vertex_t *right)
{
	const sg_rune_exact_integer_t two = { { 2U }, 1U, 0, 0 };
	uint32_t dimension;

	if (!middle || !left || !right)
		return 0;
	for (dimension = 0U; dimension < SG_RUNE_STATE_DIMENSION_COUNT;
	     dimension++)
	{
		sg_rune_binary32_dyadic_t values[3];
		sg_rune_exact_integer_t exact[3];
		sg_rune_exact_integer_t endpoints;
		sg_rune_exact_integer_t doubled;
		int common_exponent = 0;
		int have_nonzero = 0;
		uint32_t item;

		values[0] = Binary32Dyadic(RefinementCoordinate(middle, dimension));
		values[1] = Binary32Dyadic(RefinementCoordinate(left, dimension));
		values[2] = Binary32Dyadic(RefinementCoordinate(right, dimension));
		for (item = 0U; item < 3U; item++)
			if (values[item].mantissa != 0U && (!have_nonzero ||
			    values[item].exponent < common_exponent))
			{
				common_exponent = values[item].exponent;
				have_nonzero = 1;
			}
		if (!have_nonzero)
			continue;
		for (item = 0U; item < 3U; item++)
			exact[item] = ExactFromDyadic(&values[item], common_exponent);
		endpoints = ExactAdd(&exact[1], &exact[2]);
		doubled = ExactMultiply(&exact[0], &two);
		if (endpoints.overflow || doubled.overflow ||
		    ExactMagnitudeCompare(&endpoints, &doubled) != 0 ||
		    endpoints.negative != doubled.negative)
			return 0;
	}
	return 1;
}

static int ExactSameOrientationOrBoundary(
	const sg_rune_exact_integer_t *value,
	const sg_rune_exact_integer_t *orientation)
{
	return !value->overflow && value->count == 0U ? 1 :
		!value->overflow && !orientation->overflow &&
		value->negative == orientation->negative;
}

int SG_FieldRefinementPointInCellExact(
	const sg_field_refinement_vertex_t *const vertices[8],
	const sg_field_refinement_vertex_t *point)
{
	sg_rune_binary32_dyadic_t dyadic[9][SG_RUNE_STATE_DIMENSION_COUNT];
	sg_rune_exact_integer_t coordinate[9][SG_RUNE_STATE_DIMENSION_COUNT];
	sg_rune_exact_integer_t matrix[SG_RUNE_STATE_DIMENSION_COUNT]
		[SG_RUNE_STATE_DIMENSION_COUNT];
	sg_rune_exact_integer_t orientation;
	sg_rune_exact_integer_t lambda_zero;
	int common_exponent[SG_RUNE_STATE_DIMENSION_COUNT];
	uint32_t dimension;
	uint32_t vertex;

	for (dimension = 0U; dimension < SG_RUNE_STATE_DIMENSION_COUNT;
	     dimension++)
	{
		int have_nonzero = 0;
		for (vertex = 0U; vertex < 9U; vertex++)
		{
			const sg_field_refinement_vertex_t *current =
				vertex < 8U ? vertices[vertex] : point;
			dyadic[vertex][dimension] = Binary32Dyadic(
				RefinementCoordinate(current, dimension));
			if (dyadic[vertex][dimension].mantissa != 0U &&
			    (!have_nonzero || dyadic[vertex][dimension].exponent <
				common_exponent[dimension]))
			{
				common_exponent[dimension] =
					dyadic[vertex][dimension].exponent;
				have_nonzero = 1;
			}
		}
		if (!have_nonzero)
			return 0;
		for (vertex = 0U; vertex < 9U; vertex++)
			coordinate[vertex][dimension] = ExactFromDyadic(
				&dyadic[vertex][dimension], common_exponent[dimension]);
	}
	for (vertex = 0U; vertex < SG_RUNE_STATE_DIMENSION_COUNT; vertex++)
		for (dimension = 0U; dimension < SG_RUNE_STATE_DIMENSION_COUNT;
		     dimension++)
			matrix[vertex][dimension] = ExactSubtract(
				&coordinate[vertex + 1U][dimension],
				&coordinate[0][dimension]);
	orientation = ExactDeterminant(matrix);
	if (orientation.overflow || orientation.count == 0U)
		return 0;
	lambda_zero = orientation;
	for (vertex = 0U; vertex < SG_RUNE_STATE_DIMENSION_COUNT; vertex++)
	{
		sg_rune_exact_integer_t saved[SG_RUNE_STATE_DIMENSION_COUNT];
		sg_rune_exact_integer_t numerator;
		for (dimension = 0U; dimension < SG_RUNE_STATE_DIMENSION_COUNT;
		     dimension++)
		{
			saved[dimension] = matrix[vertex][dimension];
			matrix[vertex][dimension] = ExactSubtract(
				&coordinate[8][dimension], &coordinate[0][dimension]);
		}
		numerator = ExactDeterminant(matrix);
		for (dimension = 0U; dimension < SG_RUNE_STATE_DIMENSION_COUNT;
		     dimension++)
			matrix[vertex][dimension] = saved[dimension];
		if (!ExactSameOrientationOrBoundary(&numerator, &orientation))
			return 0;
		lambda_zero = ExactSubtract(&lambda_zero, &numerator);
	}
	return ExactSameOrientationOrBoundary(&lambda_zero, &orientation);
}

static const sg_rune_interval_t *GeometryFlowInterval(
	const sg_rune_flow_enclosure_t *flow, uint32_t dimension)
{
	if (dimension < 3U)
		return dimension == 0U ? &flow->position.x :
			dimension == 1U ? &flow->position.y : &flow->position.z;
	if (dimension < 6U)
	{
		dimension -= 3U;
		return dimension == 0U ? &flow->velocity.x :
			dimension == 1U ? &flow->velocity.y : &flow->velocity.z;
	}
	return &flow->elapsed_ms;
}

static sg_rune_interval_t *MutableGeometryFlowInterval(
	sg_rune_flow_enclosure_t *flow, uint32_t dimension)
{
	if (dimension < 3U)
		return dimension == 0U ? &flow->position.x :
			dimension == 1U ? &flow->position.y : &flow->position.z;
	if (dimension < 6U)
	{
		dimension -= 3U;
		return dimension == 0U ? &flow->velocity.x :
			dimension == 1U ? &flow->velocity.y : &flow->velocity.z;
	}
	return &flow->elapsed_ms;
}

static void StoreRefinementCoordinate(sg_field_refinement_vertex_t *point,
	uint32_t dimension, float value)
{
	if (dimension < 3U)
		point->position.value[dimension] = value;
	else if (dimension < 6U)
		point->velocity.value[dimension - 3U] = value;
	else
		point->elapsed_ms = value;
}

typedef struct sg_rune_product_dyadic_s
{
	uint64_t mantissa;
	int exponent;
	int negative;
} sg_rune_product_dyadic_t;

static sg_rune_product_dyadic_t ProductDyadic(float left, float right)
{
	sg_rune_binary32_dyadic_t a = Binary32Dyadic(left);
	sg_rune_binary32_dyadic_t b = Binary32Dyadic(right);
	sg_rune_product_dyadic_t result;

	result.mantissa = (uint64_t)a.mantissa * b.mantissa;
	result.exponent = a.exponent + b.exponent;
	result.negative = a.negative != b.negative;
	return result;
}

static sg_rune_exact_integer_t ExactFromProduct(
	const sg_rune_product_dyadic_t *value, int common_exponent)
{
	sg_rune_exact_integer_t result = { { 0U }, 0U, 0, 0 };
	uint32_t shift;
	size_t limb;
	uint32_t offset;
	uint64_t low;
	uint64_t high;

	if (value->mantissa == 0U)
		return result;
	shift = (uint32_t)(value->exponent - common_exponent);
	limb = shift / 32U;
	offset = shift % 32U;
	if (limb >= SG_RUNE_EXACT_INTEGER_LIMB_COUNT - 1U)
	{
		result.overflow = 1;
		return result;
	}
	low = value->mantissa << offset;
	high = offset == 0U ? 0U : value->mantissa >> (64U - offset);
	result.limb[limb] = (uint32_t)low;
	result.limb[limb + 1U] = (uint32_t)(low >> 32U);
	result.count = limb + (result.limb[limb + 1U] != 0U ? 2U : 1U);
	if (high != 0U)
	{
		if (limb + 2U >= SG_RUNE_EXACT_INTEGER_LIMB_COUNT)
			result.overflow = 1;
		else
		{
			result.limb[limb + 2U] = (uint32_t)high;
			result.count = limb + 3U;
		}
	}
	result.negative = value->negative;
	ExactNormalize(&result);
	return result;
}

static int ExactSignedCompare(const sg_rune_exact_integer_t *left,
	const sg_rune_exact_integer_t *right)
{
	int order;
	if (left->negative != right->negative)
		return left->negative ? -1 : 1;
	order = ExactMagnitudeCompare(left, right);
	return left->negative ? -order : order;
}

static uint32_t ExactBitLength(const sg_rune_exact_integer_t *value)
{
	uint32_t bits;
	uint32_t top;
	if (value->count == 0U)
		return 0U;
	bits = (uint32_t)((value->count - 1U) * 32U);
	top = value->limb[value->count - 1U];
	while (top != 0U)
	{
		bits++;
		top >>= 1U;
	}
	return bits;
}

static uint32_t ExactMagnitudeShiftRightLow(
	const sg_rune_exact_integer_t *value, uint32_t shift)
{
	size_t limb = shift / 32U;
	uint32_t offset = shift % 32U;
	uint64_t result;
	if (limb >= value->count)
		return 0U;
	result = (uint64_t)value->limb[limb] >> offset;
	if (offset != 0U && limb + 1U < value->count)
		result |= (uint64_t)value->limb[limb + 1U] << (32U - offset);
	return (uint32_t)result;
}

static int ExactMagnitudeLowBitsSet(
	const sg_rune_exact_integer_t *value, uint32_t bit_count)
{
	size_t whole = bit_count / 32U;
	uint32_t partial = bit_count % 32U;
	size_t limb;
	for (limb = 0U; limb < whole && limb < value->count; limb++)
		if (value->limb[limb] != 0U)
			return 1;
	if (partial != 0U && whole < value->count &&
	    (value->limb[whole] & ((UINT32_C(1) << partial) - 1U)) != 0U)
		return 1;
	return 0;
}

static int ExactDirectedBinary32(const sg_rune_exact_integer_t *value,
	int exponent, int toward_positive, float *result_out)
{
	uint32_t bit_length;
	int power;
	int round_up;
	uint32_t magnitude;
	uint32_t bits;

	if (!result_out || value->overflow)
		return 0;
	if (value->count == 0U)
	{
		bits = 0U;
		memcpy(result_out, &bits, sizeof(bits));
		return 1;
	}
	bit_length = ExactBitLength(value);
	power = exponent + (int)bit_length - 1;
	round_up = value->negative ? !toward_positive : toward_positive;
	if (power >= -126)
	{
		uint32_t shift = bit_length > 24U ? bit_length - 24U : 0U;
		if (power > 127)
			return 0;
		magnitude = shift == 0U ? value->limb[0] << (24U - bit_length) :
			ExactMagnitudeShiftRightLow(value, shift);
		if (round_up && shift != 0U &&
		    ExactMagnitudeLowBitsSet(value, shift))
			magnitude++;
		if (magnitude == UINT32_C(0x1000000))
		{
			magnitude >>= 1U;
			power++;
			if (power > 127)
				return 0;
		}
		bits = ((uint32_t)(power + 127) << 23U) |
			(magnitude & UINT32_C(0x7fffff));
	}
	else
	{
		int shift = -149 - exponent;
		if (shift >= 0)
		{
			magnitude = ExactMagnitudeShiftRightLow(value, (uint32_t)shift);
			if (round_up && shift != 0 &&
			    ExactMagnitudeLowBitsSet(value, (uint32_t)shift))
				magnitude++;
		}
		else
			magnitude = value->limb[0] << (uint32_t)(-shift);
		if (magnitude >= UINT32_C(0x800000))
			bits = UINT32_C(0x00800000);
		else
			bits = magnitude;
	}
	if (value->negative && bits != 0U)
		bits |= UINT32_C(0x80000000);
	memcpy(result_out, &bits, sizeof(bits));
	return 1;
}

int SG_FieldOutcomeCanonicalImage(
	const sg_field_refinement_vertex_t *const vertices[8],
	const sg_field_outcome_t *outcome, sg_rune_flow_enclosure_t *image_out)
{
	uint32_t row;
	if (!vertices || !outcome || !image_out)
		return 0;
	memset(image_out, 0, sizeof(*image_out));
	for (row = 0U; row < SG_RUNE_STATE_DIMENSION_COUNT; row++)
	{
		sg_rune_product_dyadic_t products[8][SG_RUNE_STATE_DIMENSION_COUNT];
		sg_rune_binary32_dyadic_t constant = Binary32Dyadic(
			outcome->endpoint.coefficient[row][SG_RUNE_STATE_DIMENSION_COUNT]);
		const sg_rune_interval_t *remainder = GeometryFlowInterval(
			&outcome->remainder, row);
		sg_rune_binary32_dyadic_t remainder_min =
			Binary32Dyadic(remainder->min_value);
		sg_rune_binary32_dyadic_t remainder_max =
			Binary32Dyadic(remainder->max_value);
		sg_rune_exact_integer_t values[8];
		sg_rune_exact_integer_t minimum;
		sg_rune_exact_integer_t maximum;
		int common_exponent = 0;
		int have_nonzero = 0;
		uint32_t vertex;
		uint32_t column;

		for (vertex = 0U; vertex < 8U; vertex++)
			for (column = 0U; column < SG_RUNE_STATE_DIMENSION_COUNT;
			     column++)
			{
				products[vertex][column] = ProductDyadic(
					outcome->endpoint.coefficient[row][column],
					RefinementCoordinate(vertices[vertex], column));
				if (products[vertex][column].mantissa != 0U &&
				    (!have_nonzero || products[vertex][column].exponent <
					common_exponent))
				{
					common_exponent = products[vertex][column].exponent;
					have_nonzero = 1;
				}
			}
#define INCLUDE_DYADIC(value) do { \
	if ((value).mantissa != 0U && (!have_nonzero || \
	    (value).exponent < common_exponent)) { \
		common_exponent = (value).exponent; have_nonzero = 1; \
	} \
} while (0)
		INCLUDE_DYADIC(constant);
		INCLUDE_DYADIC(remainder_min);
		INCLUDE_DYADIC(remainder_max);
#undef INCLUDE_DYADIC
		if (!have_nonzero)
			common_exponent = 0;
		for (vertex = 0U; vertex < 8U; vertex++)
		{
			values[vertex] = ExactFromDyadic(&constant, common_exponent);
			for (column = 0U; column < SG_RUNE_STATE_DIMENSION_COUNT;
			     column++)
			{
				sg_rune_exact_integer_t term = ExactFromProduct(
					&products[vertex][column], common_exponent);
				values[vertex] = ExactAdd(&values[vertex], &term);
			}
			if (values[vertex].overflow)
				return 0;
		}
		minimum = values[0];
		maximum = values[0];
		for (vertex = 1U; vertex < 8U; vertex++)
		{
			if (ExactSignedCompare(&values[vertex], &minimum) < 0)
				minimum = values[vertex];
			if (ExactSignedCompare(&values[vertex], &maximum) > 0)
				maximum = values[vertex];
		}
		{
			sg_rune_exact_integer_t lower = ExactFromDyadic(
				&remainder_min, common_exponent);
			sg_rune_exact_integer_t upper = ExactFromDyadic(
				&remainder_max, common_exponent);
			minimum = ExactAdd(&minimum, &lower);
			maximum = ExactAdd(&maximum, &upper);
		}
		if (!ExactDirectedBinary32(&minimum, common_exponent, 0,
			&MutableGeometryFlowInterval(image_out, row)->min_value) ||
		    !ExactDirectedBinary32(&maximum, common_exponent, 1,
			&MutableGeometryFlowInterval(image_out, row)->max_value))
			return 0;
	}
	return 1;
}

int SG_FieldRefinementBoxInsideCell(
	const sg_field_refinement_vertex_t *const vertices[8],
	const sg_rune_flow_enclosure_t *box)
{
	uint8_t varying[SG_RUNE_STATE_DIMENSION_COUNT];
	uint32_t varying_count = 0U;
	uint32_t dimension;
	uint32_t corner;

	if (!vertices || !box)
		return 0;
	for (dimension = 0U; dimension < SG_RUNE_STATE_DIMENSION_COUNT;
	     dimension++)
	{
		const sg_rune_interval_t *interval = GeometryFlowInterval(box, dimension);
		if (interval->min_value != interval->max_value)
			varying[varying_count++] = (uint8_t)dimension;
	}
	for (corner = 0U; corner < (UINT32_C(1) << varying_count); corner++)
	{
		sg_field_refinement_vertex_t point;
		memset(&point, 0, sizeof(point));
		for (dimension = 0U; dimension < SG_RUNE_STATE_DIMENSION_COUNT;
		     dimension++)
			StoreRefinementCoordinate(&point, dimension,
				GeometryFlowInterval(box, dimension)->min_value);
		for (dimension = 0U; dimension < varying_count; dimension++)
			if ((corner & (UINT32_C(1) << dimension)) != 0U)
				StoreRefinementCoordinate(&point, varying[dimension],
					GeometryFlowInterval(box, varying[dimension])->max_value);
		if (!SG_FieldRefinementPointInCellExact(vertices, &point))
			return 0;
	}
	return 1;
}

static sg_rune_exact_integer_t RefinementCellMeasure(
	const sg_field_refinement_vertex_t *const vertices[8], int *exponent_out)
{
	sg_rune_binary32_dyadic_t dyadic[8][SG_RUNE_STATE_DIMENSION_COUNT];
	sg_rune_exact_integer_t coordinate[8][SG_RUNE_STATE_DIMENSION_COUNT];
	sg_rune_exact_integer_t matrix[SG_RUNE_STATE_DIMENSION_COUNT]
		[SG_RUNE_STATE_DIMENSION_COUNT];
	int common_exponent[SG_RUNE_STATE_DIMENSION_COUNT];
	uint32_t dimension;
	uint32_t vertex;
	int exponent = 0;

	for (dimension = 0U; dimension < SG_RUNE_STATE_DIMENSION_COUNT;
	     dimension++)
	{
		int have_nonzero = 0;
		for (vertex = 0U; vertex < 8U; vertex++)
		{
			dyadic[vertex][dimension] = Binary32Dyadic(
				RefinementCoordinate(vertices[vertex], dimension));
			if (dyadic[vertex][dimension].mantissa != 0U &&
			    (!have_nonzero || dyadic[vertex][dimension].exponent <
				common_exponent[dimension]))
			{
				common_exponent[dimension] =
					dyadic[vertex][dimension].exponent;
				have_nonzero = 1;
			}
		}
		if (!have_nonzero)
		{
			sg_rune_exact_integer_t zero = { { 0U }, 0U, 0, 0 };
			return zero;
		}
		exponent += common_exponent[dimension];
		for (vertex = 0U; vertex < 8U; vertex++)
			coordinate[vertex][dimension] = ExactFromDyadic(
				&dyadic[vertex][dimension], common_exponent[dimension]);
	}
	for (vertex = 0U; vertex < SG_RUNE_STATE_DIMENSION_COUNT; vertex++)
		for (dimension = 0U; dimension < SG_RUNE_STATE_DIMENSION_COUNT;
		     dimension++)
			matrix[vertex][dimension] = ExactSubtract(
				&coordinate[vertex + 1U][dimension],
				&coordinate[0][dimension]);
	*exponent_out = exponent;
	return ExactDeterminant(matrix);
}

static sg_rune_exact_integer_t RefinementCellDeterminant(
	const sg_field_refinement_vertex_t *const vertices[8])
{
	int exponent;
	return RefinementCellMeasure(vertices, &exponent);
}

int SG_FieldRefinementCellOrientation(
	const sg_field_refinement_vertex_t *const vertices[8])
{
	const sg_field_refinement_vertex_t *ordered[8];
	sg_rune_exact_integer_t determinant;
	uint32_t index;

	if (!vertices)
		return 0;
	for (index = 0U; index < 8U; index++)
	{
		if (!vertices[index])
			return 0;
		ordered[index] = vertices[index];
	}
	SortRefinementVertices(ordered, 8U);
	for (index = 1U; index < 8U; index++)
		if (RefinementCoordinatesEqual(ordered[index - 1U], ordered[index]))
			return 0;
	determinant = RefinementCellDeterminant(ordered);
	if (determinant.overflow || determinant.count == 0U)
		return 0;
	return determinant.negative ? -1 : 1;
}

static int RefinementFaceSideExact(
	const sg_field_refinement_vertex_t *const face[7],
	const sg_field_refinement_vertex_t *point)
{
	sg_rune_binary32_dyadic_t dyadic[8][SG_RUNE_STATE_DIMENSION_COUNT];
	sg_rune_exact_integer_t coordinate[8][SG_RUNE_STATE_DIMENSION_COUNT];
	sg_rune_exact_integer_t matrix[SG_RUNE_STATE_DIMENSION_COUNT]
		[SG_RUNE_STATE_DIMENSION_COUNT];
	sg_rune_exact_integer_t determinant;
	int common_exponent[SG_RUNE_STATE_DIMENSION_COUNT];
	uint32_t dimension;
	uint32_t vertex;

	for (dimension = 0U; dimension < SG_RUNE_STATE_DIMENSION_COUNT;
	     dimension++)
	{
		int have_nonzero = 0;
		for (vertex = 0U; vertex < 8U; vertex++)
		{
			const sg_field_refinement_vertex_t *current =
				vertex < 7U ? face[vertex] : point;
			dyadic[vertex][dimension] = Binary32Dyadic(
				RefinementCoordinate(current, dimension));
			if (dyadic[vertex][dimension].mantissa != 0U &&
			    (!have_nonzero || dyadic[vertex][dimension].exponent <
				common_exponent[dimension]))
			{
				common_exponent[dimension] =
					dyadic[vertex][dimension].exponent;
				have_nonzero = 1;
			}
		}
		if (!have_nonzero)
			common_exponent[dimension] = 0;
		for (vertex = 0U; vertex < 8U; vertex++)
			coordinate[vertex][dimension] = ExactFromDyadic(
				&dyadic[vertex][dimension], common_exponent[dimension]);
	}
	for (vertex = 0U; vertex < SG_RUNE_STATE_DIMENSION_COUNT; vertex++)
		for (dimension = 0U; dimension < SG_RUNE_STATE_DIMENSION_COUNT;
		     dimension++)
			matrix[vertex][dimension] = ExactSubtract(
				&coordinate[vertex + 1U][dimension],
				&coordinate[0][dimension]);
	determinant = ExactDeterminant(matrix);
	if (determinant.overflow)
		return 2;
	if (determinant.count == 0U)
		return 0;
	return determinant.negative ? -1 : 1;
}

static int CellFacetStrictlySeparates(
	const sg_field_refinement_vertex_t *const owner[8],
	const sg_field_refinement_vertex_t *const other[8])
{
	uint32_t omitted;
	for (omitted = 0U; omitted <= SG_RUNE_STATE_DIMENSION_COUNT; omitted++)
	{
		const sg_field_refinement_vertex_t *face[7];
		uint32_t vertex;
		uint32_t face_vertex = 0U;
		int owner_side;
		int separates = 1;
		for (vertex = 0U; vertex <= SG_RUNE_STATE_DIMENSION_COUNT; vertex++)
			if (vertex != omitted)
				face[face_vertex++] = owner[vertex];
		owner_side = RefinementFaceSideExact(face, owner[omitted]);
		if (owner_side == 0 || owner_side == 2)
			return 0;
		for (vertex = 0U; vertex <= SG_RUNE_STATE_DIMENSION_COUNT; vertex++)
		{
			int side = RefinementFaceSideExact(face, other[vertex]);
			uint32_t shared;
			if (side == 2 || side == owner_side)
			{
				separates = 0;
				break;
			}
			if (side != 0)
				continue;
			for (shared = 0U; shared < 7U; shared++)
				if (RefinementCoordinatesEqual(other[vertex], face[shared]))
					break;
			if (shared == 7U)
			{
				separates = 0;
				break;
			}
		}
		if (separates)
			return 1;
	}
	return 0;
}

static uint32_t BitCount(uint32_t value)
{
	uint32_t count = 0U;
	while (value != 0U)
	{
		value &= value - 1U;
		count++;
	}
	return count;
}

static int WeightNonnegative(const sg_rune_exact_integer_t *weight,
	const sg_rune_exact_integer_t *denominator)
{
	return !weight->overflow && (weight->count == 0U ||
		weight->negative == denominator->negative);
}

static int IntersectionHasPointOutsideSharedHull(
	const sg_field_refinement_vertex_t *const left[8],
	const sg_field_refinement_vertex_t *const right[8],
	const uint8_t left_shared[8])
{
	sg_rune_binary32_dyadic_t dyadic[16][SG_RUNE_STATE_DIMENSION_COUNT];
	sg_rune_exact_integer_t coordinate[16][SG_RUNE_STATE_DIMENSION_COUNT];
	int common_exponent[SG_RUNE_STATE_DIMENSION_COUNT];
	uint32_t dimension;
	uint32_t vertex;
	uint32_t left_mask;

	for (dimension = 0U; dimension < SG_RUNE_STATE_DIMENSION_COUNT;
	     dimension++)
	{
		int have_nonzero = 0;
		for (vertex = 0U; vertex < 16U; vertex++)
		{
			const sg_field_refinement_vertex_t *point = vertex < 8U ?
				left[vertex] : right[vertex - 8U];
			dyadic[vertex][dimension] = Binary32Dyadic(
				RefinementCoordinate(point, dimension));
			if (dyadic[vertex][dimension].mantissa != 0U &&
			    (!have_nonzero || dyadic[vertex][dimension].exponent <
				common_exponent[dimension]))
			{
				common_exponent[dimension] =
					dyadic[vertex][dimension].exponent;
				have_nonzero = 1;
			}
		}
		if (!have_nonzero)
			return -1;
		for (vertex = 0U; vertex < 16U; vertex++)
			coordinate[vertex][dimension] = ExactFromDyadic(
				&dyadic[vertex][dimension], common_exponent[dimension]);
	}
	for (left_mask = 1U; left_mask < 256U; left_mask++)
	{
		uint32_t left_count = BitCount(left_mask);
		uint32_t right_count;
		uint32_t right_mask;
		if (left_count > 8U)
			continue;
		right_count = 9U - left_count;
		if (right_count == 0U || right_count > 8U)
			continue;
		for (right_mask = 1U; right_mask < 256U; right_mask++)
		{
			sg_rune_exact_integer_t matrix[SG_RUNE_STATE_DIMENSION_COUNT]
				[SG_RUNE_STATE_DIMENSION_COUNT];
			sg_rune_exact_integer_t right_hand[SG_RUNE_STATE_DIMENSION_COUNT];
			sg_rune_exact_integer_t left_weight[8];
			sg_rune_exact_integer_t right_weight[8];
			sg_rune_exact_integer_t denominator;
			uint8_t column_side[SG_RUNE_STATE_DIMENSION_COUNT];
			uint8_t column_vertex[SG_RUNE_STATE_DIMENSION_COUNT];
			uint32_t left_anchor = 0U;
			uint32_t right_anchor = 0U;
			uint32_t column = 0U;
			int feasible = 1;

			if (BitCount(right_mask) != right_count)
				continue;
			while ((left_mask & (UINT32_C(1) << left_anchor)) == 0U)
				left_anchor++;
			while ((right_mask & (UINT32_C(1) << right_anchor)) == 0U)
				right_anchor++;
			memset(matrix, 0, sizeof(matrix));
			memset(left_weight, 0, sizeof(left_weight));
			memset(right_weight, 0, sizeof(right_weight));
			for (vertex = 0U; vertex < 8U; vertex++)
				if (vertex != left_anchor &&
				    (left_mask & (UINT32_C(1) << vertex)) != 0U)
				{
					for (dimension = 0U;
					     dimension < SG_RUNE_STATE_DIMENSION_COUNT; dimension++)
						matrix[dimension][column] = ExactSubtract(
							&coordinate[vertex][dimension],
							&coordinate[left_anchor][dimension]);
					column_side[column] = 0U;
					column_vertex[column++] = (uint8_t)vertex;
				}
			for (vertex = 0U; vertex < 8U; vertex++)
				if (vertex != right_anchor &&
				    (right_mask & (UINT32_C(1) << vertex)) != 0U)
				{
					for (dimension = 0U;
					     dimension < SG_RUNE_STATE_DIMENSION_COUNT; dimension++)
					{
						matrix[dimension][column] = ExactSubtract(
							&coordinate[8U + vertex][dimension],
							&coordinate[8U + right_anchor][dimension]);
						if (matrix[dimension][column].count != 0U)
							matrix[dimension][column].negative =
								!matrix[dimension][column].negative;
					}
					column_side[column] = 1U;
					column_vertex[column++] = (uint8_t)vertex;
				}
			if (column != SG_RUNE_STATE_DIMENSION_COUNT)
				return -1;
			for (dimension = 0U; dimension < SG_RUNE_STATE_DIMENSION_COUNT;
			     dimension++)
				right_hand[dimension] = ExactSubtract(
					&coordinate[8U + right_anchor][dimension],
					&coordinate[left_anchor][dimension]);
			denominator = ExactDeterminant(matrix);
			if (denominator.overflow)
				return -1;
			if (denominator.count == 0U)
				continue;
			for (column = 0U; column < SG_RUNE_STATE_DIMENSION_COUNT;
			     column++)
			{
				sg_rune_exact_integer_t *weight = column_side[column] == 0U ?
					&left_weight[column_vertex[column]] :
					&right_weight[column_vertex[column]];
				for (dimension = 0U;
					     dimension < SG_RUNE_STATE_DIMENSION_COUNT; dimension++)
				{
					sg_rune_exact_integer_t saved = matrix[dimension][column];
					matrix[dimension][column] = right_hand[dimension];
					right_hand[dimension] = saved;
				}
				*weight = ExactDeterminant(matrix);
				for (dimension = 0U;
					     dimension < SG_RUNE_STATE_DIMENSION_COUNT; dimension++)
				{
					sg_rune_exact_integer_t saved = matrix[dimension][column];
					matrix[dimension][column] = right_hand[dimension];
					right_hand[dimension] = saved;
				}
				if (weight->overflow || !WeightNonnegative(weight, &denominator))
					feasible = 0;
			}
			left_weight[left_anchor] = denominator;
			right_weight[right_anchor] = denominator;
			for (vertex = 0U; vertex < 8U; vertex++)
			{
				if (vertex != left_anchor &&
				    (left_mask & (UINT32_C(1) << vertex)) != 0U)
					left_weight[left_anchor] = ExactSubtract(
						&left_weight[left_anchor], &left_weight[vertex]);
				if (vertex != right_anchor &&
				    (right_mask & (UINT32_C(1) << vertex)) != 0U)
					right_weight[right_anchor] = ExactSubtract(
						&right_weight[right_anchor], &right_weight[vertex]);
			}
			if (!WeightNonnegative(&left_weight[left_anchor], &denominator) ||
			    !WeightNonnegative(&right_weight[right_anchor], &denominator))
				feasible = 0;
			if (!feasible)
				continue;
			for (vertex = 0U; vertex < 8U; vertex++)
				if ((left_mask & (UINT32_C(1) << vertex)) != 0U &&
				    !left_shared[vertex] && left_weight[vertex].count != 0U)
					return 1;
		}
	}
	return 0;
}

int SG_FieldRefinementCellsProperlyMeet(
	const sg_field_refinement_vertex_t *const left[8],
	const sg_field_refinement_vertex_t *const right[8])
{
	const sg_field_refinement_vertex_t *shared_face[7];
	const sg_field_refinement_vertex_t *left_opposite = NULL;
	const sg_field_refinement_vertex_t *right_opposite = NULL;
	uint8_t left_shared[8] = { 0U };
	uint32_t left_vertex;
	uint32_t shared = 0U;
	if (!left || !right)
		return 0;
	for (left_vertex = 0U; left_vertex <= SG_RUNE_STATE_DIMENSION_COUNT;
	     left_vertex++)
	{
		uint32_t right_vertex;
		for (right_vertex = 0U; right_vertex <= SG_RUNE_STATE_DIMENSION_COUNT;
		     right_vertex++)
			if (RefinementCoordinatesEqual(left[left_vertex],
				right[right_vertex]))
			{
				left_shared[left_vertex] = 1U;
				if (shared < SG_RUNE_STATE_DIMENSION_COUNT)
					shared_face[shared] = left[left_vertex];
				shared++;
				break;
			}
		if (right_vertex > SG_RUNE_STATE_DIMENSION_COUNT)
			left_opposite = left[left_vertex];
	}
	if (shared == 8U)
		return 0;
	if (shared == 7U)
	{
		uint32_t right_vertex;
		int left_side;
		int right_side;
		for (right_vertex = 0U; right_vertex <= SG_RUNE_STATE_DIMENSION_COUNT;
		     right_vertex++)
		{
			uint32_t face_vertex;
			for (face_vertex = 0U; face_vertex < 7U; face_vertex++)
				if (RefinementCoordinatesEqual(right[right_vertex],
					shared_face[face_vertex]))
					break;
			if (face_vertex == 7U)
			{
				right_opposite = right[right_vertex];
				break;
			}
		}
		if (!left_opposite || !right_opposite)
			return 0;
		left_side = RefinementFaceSideExact(shared_face, left_opposite);
		right_side = RefinementFaceSideExact(shared_face, right_opposite);
		return left_side != 0 && left_side != 2 && right_side != 0 &&
			right_side != 2 && left_side == -right_side;
	}
	if (CellFacetStrictlySeparates(left, right) ||
	    CellFacetStrictlySeparates(right, left))
		return 1;
	return IntersectionHasPointOutsideSharedHull(left, right, left_shared) == 0;
}

int SG_FieldRefinementCellFullRank(
	const sg_field_refinement_vertex_t *const vertices[8])
{
	sg_rune_binary32_dyadic_t coordinates
		[SG_RUNE_STATE_DIMENSION_COUNT + 1U]
		[SG_RUNE_STATE_DIMENSION_COUNT];
	int common_exponents[SG_RUNE_STATE_DIMENSION_COUNT];
	uint32_t vertex;
	uint32_t dimension;
	size_t prime;

	if (!vertices)
		return 0;
	for (dimension = 0U; dimension < SG_RUNE_STATE_DIMENSION_COUNT;
	     dimension++)
	{
		int have_nonzero = 0;
		for (vertex = 0U; vertex <= SG_RUNE_STATE_DIMENSION_COUNT; vertex++)
		{
			if (!vertices[vertex])
				return 0;
			coordinates[vertex][dimension] = Binary32Dyadic(
				RefinementCoordinate(vertices[vertex], dimension));
			if (coordinates[vertex][dimension].mantissa != 0U &&
			    (!have_nonzero || coordinates[vertex][dimension].exponent <
				common_exponents[dimension]))
			{
				common_exponents[dimension] =
					coordinates[vertex][dimension].exponent;
				have_nonzero = 1;
			}
		}
		if (!have_nonzero)
			return 0;
	}
	for (prime = 0U; prime < SG_RUNE_BINARY32_RANK_PRIME_COUNT; prime++)
		if (RankSevenModulo(coordinates, common_exponents,
			rank_primes[prime]))
			return 1;
	return 0;
}

typedef struct sg_geometry_cell_s
{
	const sg_field_refinement_vertex_t *vertices[8];
	size_t chart;
	size_t domain;
	size_t atom;
	size_t source;
	sg_rune_exact_integer_t volume;
	int volume_exponent;
} sg_geometry_cell_t;

typedef struct sg_geometry_facet_s
{
	const sg_field_refinement_vertex_t *vertices[7];
	size_t chart;
	size_t domain;
	size_t cell;
	int orientation;
} sg_geometry_facet_t;

typedef struct sg_geometry_ridge_s
{
	const sg_field_refinement_vertex_t *vertices[6];
	size_t domain;
	int orientation;
} sg_geometry_ridge_t;

static int GeometryFacetCompare(const void *left_value, const void *right_value)
{
	const sg_geometry_facet_t *left = left_value;
	const sg_geometry_facet_t *right = right_value;
	uint32_t vertex;
	for (vertex = 0U; vertex < SG_RUNE_STATE_DIMENSION_COUNT; vertex++)
	{
		int order = RefinementCoordinateCompare(left->vertices[vertex],
			right->vertices[vertex]);
		if (order != 0)
			return order;
	}
	if (left->chart != right->chart)
		return left->chart < right->chart ? -1 : 1;
	if (left->domain != right->domain)
		return left->domain < right->domain ? -1 : 1;
	if (left->cell != right->cell)
		return left->cell < right->cell ? -1 : 1;
	return 0;
}

static int GeometryFacetCoordinatesEqual(const sg_geometry_facet_t *left,

	const sg_geometry_facet_t *right)
{
	uint32_t vertex;
	if (left->chart != right->chart)
		return 0;
	for (vertex = 0U; vertex < SG_RUNE_STATE_DIMENSION_COUNT; vertex++)
		if (!RefinementCoordinatesEqual(left->vertices[vertex],
			right->vertices[vertex]))
			return 0;
	return 1;
}

static int GeometryRidgeCompare(const void *left_value, const void *right_value)
{
	const sg_geometry_ridge_t *left = left_value;
	const sg_geometry_ridge_t *right = right_value;
	uint32_t vertex;
	for (vertex = 0U; vertex < 6U; vertex++)
	{
		int order = RefinementCoordinateCompare(left->vertices[vertex],
			right->vertices[vertex]);
		if (order != 0)
			return order;
	}
	if (left->domain != right->domain)
		return left->domain < right->domain ? -1 : 1;
	return 0;
}

static int GeometryRidgeSame(const sg_geometry_ridge_t *left,
	const sg_geometry_ridge_t *right)
{
	uint32_t vertex;
	if (left->domain != right->domain)
		return 0;
	for (vertex = 0U; vertex < 6U; vertex++)
		if (!RefinementCoordinatesEqual(left->vertices[vertex],
			right->vertices[vertex]))
			return 0;
	return 1;
}

static int GeometryManifestRidgesValid(const sg_geometry_facet_t *manifest,
	size_t manifest_count)
{
	sg_geometry_ridge_t *ridges;
	size_t ridge_count;
	size_t facet;
	size_t first;
	if (manifest_count > SIZE_MAX / 7U ||
	    manifest_count * 7U > SIZE_MAX / sizeof(*ridges))
		return 0;
	ridge_count = manifest_count * 7U;
	ridges = calloc(ridge_count, sizeof(*ridges));
	if (!ridges)
		return 0;
	for (facet = 0U; facet < manifest_count; facet++)
	{
		uint32_t omitted;
		for (omitted = 0U; omitted < 7U; omitted++)
		{
			sg_geometry_ridge_t *ridge = &ridges[facet * 7U + omitted];
			uint32_t vertex;
			uint32_t output = 0U;
			ridge->domain = manifest[facet].domain;
			ridge->orientation = manifest[facet].orientation *
				((omitted & 1U) != 0U ? -1 : 1);
			for (vertex = 0U; vertex < 7U; vertex++)
				if (vertex != omitted)
					ridge->vertices[output++] =
						manifest[facet].vertices[vertex];
		}
	}
	qsort(ridges, ridge_count, sizeof(*ridges), GeometryRidgeCompare);
	first = 0U;
	while (first < ridge_count)
	{
		size_t end = first + 1U;
		while (end < ridge_count && GeometryRidgeSame(&ridges[first],
			&ridges[end]))
			end++;
		if (end - first != 2U ||
		    ridges[first].orientation == ridges[first + 1U].orientation)
		{
			free(ridges);
			return 0;
		}
		first = end;
	}
	free(ridges);
	return 1;
}

static int GeometryStableIdSame(const sg_rune_stable_id_t *left,
	const sg_rune_stable_id_t *right)
{
	return SG_RuneModelStableIdEqual(left, right);
}

static const sg_field_refinement_vertex_t *GeometryFindRefinementVertex(
	const sg_field_refinement_tree_t *tree,
	const sg_field_refinement_vertex_ref_t *reference)
{
	size_t low = 0U;
	size_t high = tree->vertex_count;
	while (low < high)
	{
		size_t middle = low + (high - low) / 2U;
		const sg_rune_stable_id_t *candidate =
			&tree->vertices[middle].id.value;
		if (candidate->high < reference->value.high ||
		    (candidate->high == reference->value.high &&
		     candidate->low < reference->value.low))
			low = middle + 1U;
		else if (candidate->high > reference->value.high ||
			 (candidate->high == reference->value.high &&
			  candidate->low > reference->value.low))
			high = middle;
		else if (candidate->source_set_identity ==
			reference->value.source_set_identity)
			return &tree->vertices[middle];
		else
			return NULL;
	}
	return NULL;
}

static size_t GeometryDomainIndex(const sg_rune_dynamics_model_t *model,
	const sg_rune_state_domain_ref_t *reference)
{
	size_t index;
	for (index = 0U; index < model->state_domain_count; index++)
		if (GeometryStableIdSame(&model->state_domains[index].id.value,
			&reference->value))
			return index;
	return SIZE_MAX;
}

static size_t GeometryChartIndex(const sg_rune_dynamics_model_t *model,
	const sg_rune_state_chart_ref_t *reference)
{
	size_t index;
	for (index = 0U; index < model->state_chart_count; index++)
		if (GeometryStableIdSame(&model->state_charts[index].id.value,
			&reference->value))
			return index;
	return SIZE_MAX;
}

static size_t GeometryAtomIndex(const sg_rune_dynamics_model_t *model,
	const sg_field_reach_atom_ref_t *reference)
{
	size_t index;
	for (index = 0U; index < model->reach_atom_count; index++)
		if (GeometryStableIdSame(&model->reach_atoms[index].id.value,
			&reference->value))
			return index;
	return SIZE_MAX;
}

static void GeometryStatePoint(const sg_rune_state_vertex_t *source,
	sg_field_refinement_vertex_t *destination)
{
	memset(destination, 0, sizeof(*destination));
	destination->position = source->position;
	destination->velocity = source->velocity;
	destination->elapsed_ms = source->elapsed_ms;
}

static int GeometryLoadNodeCell(const sg_field_refinement_tree_t *tree,
	const sg_field_refinement_node_t *node, sg_geometry_cell_t *cell)
{
	uint32_t vertex;
	for (vertex = 0U; vertex <= SG_RUNE_STATE_DIMENSION_COUNT; vertex++)
	{
		cell->vertices[vertex] = GeometryFindRefinementVertex(tree,
			&tree->node_vertices[(size_t)node->vertices.first + vertex]);
		if (!cell->vertices[vertex])
			return 0;
	}
	SortRefinementVertices(cell->vertices, 8U);
	cell->volume = RefinementCellMeasure(cell->vertices,
		&cell->volume_exponent);
	return !cell->volume.overflow && cell->volume.count != 0U;
}

int SG_RuneDynamicsLocatePointExact(const sg_rune_dynamics_model_t *model,
	const sg_rune_state_chart_ref_t *chart, const sg_rune_vec3_t *position,
	const sg_rune_vec3_t *velocity, float elapsed_ms,
	sg_rune_state_simplex_id_t *simplex_out,
	sg_field_reach_atom_id_t *atom_out,
	sg_field_refinement_node_id_t *leaf_out)
{
	sg_field_refinement_vertex_t point;
	size_t chart_index;
	size_t simplex_offset;
	size_t selected_simplex = SIZE_MAX;
	size_t selected_atom = SIZE_MAX;
	size_t node_index;

	if (!model || !chart || !position || !velocity || !simplex_out ||
	    !atom_out || !leaf_out)
		return 0;
	chart_index = GeometryChartIndex(model, chart);
	if (chart_index == SIZE_MAX)
		return 0;
	memset(&point, 0, sizeof(point));
	point.position = *position;
	point.velocity = *velocity;
	point.elapsed_ms = elapsed_ms;
	/* The authenticated simplex-owner catalog is ordered exactly like the
	 * simplex catalog. The first closed-simplex match therefore owns a shared
	 * face; this is the geometry manifest's canonical boundary rule. */
	for (simplex_offset = 0U;
	     simplex_offset < model->state_charts[chart_index].simplices.count;
	     simplex_offset++)
	{
		size_t simplex_index =
			(size_t)model->state_charts[chart_index].simplices.first +
			simplex_offset;
		const sg_rune_state_simplex_t *simplex =
			&model->state_simplices[simplex_index];
		const sg_rune_state_simplex_owner_t *owner =
			&model->simplex_owners[simplex_index];
		sg_field_refinement_vertex_t converted[8];
		const sg_field_refinement_vertex_t *vertices[8];
		size_t vertex;
		if (!GeometryStableIdSame(&simplex->id.value,
			&owner->simplex.value) || simplex->vertices.count != 8U ||
		    (size_t)simplex->vertices.first > model->state_vertex_count ||
		    model->state_vertex_count - (size_t)simplex->vertices.first < 8U)
			return 0;
		for (vertex = 0U; vertex < 8U; vertex++)
		{
			GeometryStatePoint(&model->state_vertices[
				(size_t)simplex->vertices.first + vertex],
				&converted[vertex]);
			vertices[vertex] = &converted[vertex];
		}
		if (!SG_FieldRefinementPointInCellExact(vertices, &point))
			continue;
		selected_simplex = simplex_index;
		selected_atom = GeometryAtomIndex(model, &owner->atom);
		break;
	}
	if (selected_simplex == SIZE_MAX || selected_atom == SIZE_MAX)
		return 0;
	/* Refinement nodes use the same authenticated catalog-order tie rule. */
	for (node_index = 0U; node_index < model->refinement_tree.node_count;
	     node_index++)
	{
		const sg_field_refinement_node_t *node =
			&model->refinement_tree.nodes[node_index];
		sg_geometry_cell_t cell;
		if (node->children.count != 0U ||
		    !GeometryStableIdSame(&node->atom.value,
			&model->reach_atoms[selected_atom].id.value) ||
		    !GeometryLoadNodeCell(&model->refinement_tree, node, &cell) ||
		    !SG_FieldRefinementPointInCellExact(cell.vertices, &point))
			continue;
		*simplex_out = model->state_simplices[selected_simplex].id;
		*atom_out = model->reach_atoms[selected_atom].id;
		*leaf_out = node->id;
		return 1;
	}
	return 0;
}

static int GeometryBuildFacets(const sg_geometry_cell_t *cells,
	size_t cell_count, sg_geometry_facet_t *facets)
{
	size_t cell;
	for (cell = 0U; cell < cell_count; cell++)
	{
		uint32_t omitted;
		int cell_orientation = cells[cell].volume.negative ? -1 : 1;
		for (omitted = 0U; omitted <= SG_RUNE_STATE_DIMENSION_COUNT; omitted++)
		{
			sg_geometry_facet_t *facet =
				&facets[cell * 8U + omitted];
			uint32_t vertex;
			uint32_t face_vertex = 0U;
			facet->domain = cells[cell].domain;
			facet->chart = cells[cell].chart;
			facet->cell = cell;
			facet->orientation = cell_orientation *
				((omitted & 1U) != 0U ? -1 : 1);
			for (vertex = 0U; vertex <= SG_RUNE_STATE_DIMENSION_COUNT;
			     vertex++)
				if (vertex != omitted)
					facet->vertices[face_vertex++] =
						cells[cell].vertices[vertex];
		}
	}
	qsort(facets, cell_count * 8U, sizeof(*facets), GeometryFacetCompare);
	return 1;
}

static int GeometryExactSum(const sg_geometry_cell_t *cells, size_t cell_count,
	size_t domain, sg_rune_exact_integer_t *sum_out, int *exponent_out)
{
	int common_exponent = 0;
	int have_cell = 0;
	size_t cell;
	sg_rune_exact_integer_t sum = { { 0U }, 0U, 0, 0 };
	for (cell = 0U; cell < cell_count; cell++)
		if (cells[cell].domain == domain && (!have_cell ||
		    cells[cell].volume_exponent < common_exponent))
		{
			common_exponent = cells[cell].volume_exponent;
			have_cell = 1;
		}
	if (!have_cell)
		return 0;
	for (cell = 0U; cell < cell_count; cell++)
		if (cells[cell].domain == domain)
		{
			sg_rune_exact_integer_t magnitude = cells[cell].volume;
			sg_rune_exact_integer_t aligned;
			magnitude.negative = 0;
			aligned = ExactMagnitudeShiftLeft(&magnitude,
				(uint32_t)(cells[cell].volume_exponent - common_exponent));
			sum = ExactMagnitudeAdd(&sum, &aligned);
			if (aligned.overflow || sum.overflow)
				return 0;
		}
	*sum_out = sum;
	*exponent_out = common_exponent;
	return 1;
}

static int GeometryCertificateSame(const sg_rune_dynamics_model_t *model,
	const sg_rune_exact_positive_dyadic_t *certificate,
	const sg_rune_exact_integer_t *unnormalized, int exponent)
{
	uint32_t trailing;
	sg_rune_exact_integer_t normalized;
	size_t word;
	if (unnormalized->overflow || unnormalized->count == 0U ||
	    certificate->magnitude.count == 0U ||
	    (size_t)certificate->magnitude.first + certificate->magnitude.count >
		model->exact_word_count ||
	    model->exact_words[certificate->magnitude.first] % 2U == 0U ||
	    model->exact_words[(size_t)certificate->magnitude.first +
		certificate->magnitude.count - 1U] == 0U)
		return 0;
	trailing = ExactTrailingZeroBits(unnormalized);
	normalized = ExactMagnitudeShiftRight(unnormalized, trailing);
	if (normalized.count != certificate->magnitude.count ||
	    (int64_t)exponent + trailing != certificate->exponent)
		return 0;
	for (word = 0U; word < normalized.count; word++)
		if (normalized.limb[word] != model->exact_words[
			(size_t)certificate->magnitude.first + word])
			return 0;
	return 1;
}

static int GeometryDyadicsEqual(const sg_rune_exact_integer_t *left,
	int left_exponent, const sg_rune_exact_integer_t *right,
	int right_exponent)
{
	sg_rune_exact_integer_t left_aligned = *left;
	sg_rune_exact_integer_t right_aligned = *right;
	int common = left_exponent < right_exponent ? left_exponent : right_exponent;
	left_aligned.negative = 0;
	right_aligned.negative = 0;
	left_aligned = ExactMagnitudeShiftLeft(&left_aligned,
		(uint32_t)(left_exponent - common));
	right_aligned = ExactMagnitudeShiftLeft(&right_aligned,
		(uint32_t)(right_exponent - common));
	return !left_aligned.overflow && !right_aligned.overflow &&
		ExactMagnitudeCompare(&left_aligned, &right_aligned) == 0;
}

static int GeometryParentCovered(const sg_rune_dynamics_model_t *model,
	const sg_field_refinement_node_t *parent)
{
	const sg_field_refinement_tree_t *tree = &model->refinement_tree;
	sg_geometry_cell_t parent_cell;
	sg_geometry_cell_t child[2];
	sg_rune_exact_integer_t sum;
	int common;
	uint32_t child_index;
	uint32_t vertex;
	if (parent->children.count == 0U)
		return 1;
	if (parent->children.count != 2U ||
	    !GeometryLoadNodeCell(tree, parent, &parent_cell))
		return 0;
	for (child_index = 0U; child_index < 2U; child_index++)
	{
		const sg_field_refinement_node_t *record = &tree->nodes[
			tree->children[(size_t)parent->children.first + child_index]];
		if (!GeometryLoadNodeCell(tree, record, &child[child_index]))
			return 0;
		for (vertex = 0U; vertex < 8U; vertex++)
			if (!SG_FieldRefinementPointInCellExact(parent_cell.vertices,
				child[child_index].vertices[vertex]))
				return 0;
	}
	if (!SG_FieldRefinementCellsProperlyMeet(child[0].vertices,
		child[1].vertices))
		return 0;
	common = child[0].volume_exponent < child[1].volume_exponent ?
		child[0].volume_exponent : child[1].volume_exponent;
	{
		sg_rune_exact_integer_t left = child[0].volume;
		sg_rune_exact_integer_t right = child[1].volume;
		left.negative = 0;
		right.negative = 0;
		left = ExactMagnitudeShiftLeft(&left,
			(uint32_t)(child[0].volume_exponent - common));
		right = ExactMagnitudeShiftLeft(&right,
			(uint32_t)(child[1].volume_exponent - common));
		sum = ExactMagnitudeAdd(&left, &right);
	}
	return GeometryDyadicsEqual(&sum, common, &parent_cell.volume,
		parent_cell.volume_exponent);
}

static sg_rune_exact_integer_t GeometryProjectedFacetMeasure(
	const sg_field_refinement_vertex_t *const vertices[7],
	const uint8_t dimensions[6], int *exponent_out)
{
	sg_rune_binary32_dyadic_t dyadic[7][6];
	sg_rune_exact_integer_t coordinate[7][6];
	sg_rune_exact_integer_t matrix[SG_RUNE_STATE_DIMENSION_COUNT]
		[SG_RUNE_STATE_DIMENSION_COUNT];
	int common_exponent[6];
	int exponent = 0;
	uint32_t dimension;
	uint32_t vertex;
	memset(matrix, 0, sizeof(matrix));
	for (dimension = 0U; dimension < 6U; dimension++)
	{
		int have_nonzero = 0;
		for (vertex = 0U; vertex < 7U; vertex++)
		{
			dyadic[vertex][dimension] = Binary32Dyadic(
				RefinementCoordinate(vertices[vertex], dimensions[dimension]));
			if (dyadic[vertex][dimension].mantissa != 0U && (!have_nonzero ||
			    dyadic[vertex][dimension].exponent <
				common_exponent[dimension]))
			{
				common_exponent[dimension] =
					dyadic[vertex][dimension].exponent;
				have_nonzero = 1;
			}
		}
		if (!have_nonzero)
		{
			sg_rune_exact_integer_t zero = { { 0U }, 0U, 0, 0 };
			return zero;
		}
		exponent += common_exponent[dimension];
		for (vertex = 0U; vertex < 7U; vertex++)
			coordinate[vertex][dimension] = ExactFromDyadic(
				&dyadic[vertex][dimension], common_exponent[dimension]);
	}
	for (vertex = 0U; vertex < 6U; vertex++)
		for (dimension = 0U; dimension < 6U; dimension++)
			matrix[vertex][dimension] = ExactSubtract(
				&coordinate[vertex + 1U][dimension],
				&coordinate[0][dimension]);
	*exponent_out = exponent;
	return ExactDeterminantOrder(matrix, 6U);
}

static int GeometryFacetProjection(
	const sg_field_refinement_vertex_t *const vertices[7],
	uint8_t dimensions[6], sg_rune_exact_integer_t *measure,
	int *exponent)
{
	uint32_t omitted;
	for (omitted = 0U; omitted < SG_RUNE_STATE_DIMENSION_COUNT; omitted++)
	{
		uint32_t dimension;
		uint32_t selected = 0U;
		for (dimension = 0U; dimension < SG_RUNE_STATE_DIMENSION_COUNT;
		     dimension++)
			if (dimension != omitted)
				dimensions[selected++] = (uint8_t)dimension;
		*measure = GeometryProjectedFacetMeasure(vertices, dimensions, exponent);
		if (measure->overflow)
			return 0;
		if (measure->count != 0U)
			return 1;
	}
	return 0;
}

static int GeometryPointInFacet(
	const sg_field_refinement_vertex_t *const facet[7],
	const uint8_t dimensions[6], const sg_field_refinement_vertex_t *point)
{
	sg_rune_binary32_dyadic_t dyadic[8][6];
	sg_rune_exact_integer_t coordinate[8][6];
	sg_rune_exact_integer_t matrix[SG_RUNE_STATE_DIMENSION_COUNT]
		[SG_RUNE_STATE_DIMENSION_COUNT];
	sg_rune_exact_integer_t denominator;
	sg_rune_exact_integer_t lambda_zero;
	int common_exponent[6];
	uint32_t dimension;
	uint32_t vertex;
	if (RefinementFaceSideExact(facet, point) != 0)
		return 0;
	memset(matrix, 0, sizeof(matrix));
	for (dimension = 0U; dimension < 6U; dimension++)
	{
		int have_nonzero = 0;
		for (vertex = 0U; vertex < 8U; vertex++)
		{
			const sg_field_refinement_vertex_t *current =
				vertex < 7U ? facet[vertex] : point;
			dyadic[vertex][dimension] = Binary32Dyadic(
				RefinementCoordinate(current, dimensions[dimension]));
			if (dyadic[vertex][dimension].mantissa != 0U && (!have_nonzero ||
			    dyadic[vertex][dimension].exponent <
				common_exponent[dimension]))
			{
				common_exponent[dimension] =
					dyadic[vertex][dimension].exponent;
				have_nonzero = 1;
			}
		}
		if (!have_nonzero)
			return 0;
		for (vertex = 0U; vertex < 8U; vertex++)
			coordinate[vertex][dimension] = ExactFromDyadic(
				&dyadic[vertex][dimension], common_exponent[dimension]);
	}
	for (vertex = 0U; vertex < 6U; vertex++)
		for (dimension = 0U; dimension < 6U; dimension++)
			matrix[vertex][dimension] = ExactSubtract(
				&coordinate[vertex + 1U][dimension],
				&coordinate[0][dimension]);
	denominator = ExactDeterminantOrder(matrix, 6U);
	if (denominator.overflow || denominator.count == 0U)
		return 0;
	lambda_zero = denominator;
	for (vertex = 0U; vertex < 6U; vertex++)
	{
		sg_rune_exact_integer_t saved[6];
		sg_rune_exact_integer_t numerator;
		for (dimension = 0U; dimension < 6U; dimension++)
		{
			saved[dimension] = matrix[vertex][dimension];
			matrix[vertex][dimension] = ExactSubtract(
				&coordinate[7][dimension], &coordinate[0][dimension]);
		}
		numerator = ExactDeterminantOrder(matrix, 6U);
		for (dimension = 0U; dimension < 6U; dimension++)
			matrix[vertex][dimension] = saved[dimension];
		if (!ExactSameOrientationOrBoundary(&numerator, &denominator))
			return 0;
		lambda_zero = ExactSubtract(&lambda_zero, &numerator);
	}
	return ExactSameOrientationOrBoundary(&lambda_zero, &denominator);
}

static int GeometryManifestValid(const sg_rune_dynamics_model_t *model,
	sg_geometry_facet_t *manifest)
{
	size_t next_facet = 0U;
	size_t next_vertex = 0U;
	size_t next_word = 0U;
	size_t domain;
	for (domain = 0U; domain < model->state_domain_count; domain++)
	{
		const sg_rune_domain_support_certificate_t *support =
			&model->domain_support[domain];
		size_t facet;
		if (!SG_RuneStateDomainIdValid(&support->domain) ||
		    !GeometryStableIdSame(&support->domain.value,
			&model->state_domains[domain].id.value) ||
		    !SG_RuneDomainSupportProofRefValid(&support->proof) ||
		    support->domain.value.source_set_identity !=
			model->id.value.source_set_identity ||
		    support->proof.value.source_set_identity !=
			model->id.value.source_set_identity ||
		    support->boundary_facets.first != next_facet ||
		    support->boundary_facets.count == 0U ||
		    (size_t)support->boundary_facets.first +
			support->boundary_facets.count >
			model->domain_boundary_facet_count ||
		    support->normalized_volume.magnitude.first != next_word ||
		    support->normalized_volume.magnitude.count == 0U ||
		    (size_t)support->normalized_volume.magnitude.first +
			support->normalized_volume.magnitude.count > model->exact_word_count)
			return 0;
		for (facet = support->boundary_facets.first;
		     facet < (size_t)support->boundary_facets.first +
			support->boundary_facets.count; facet++)
		{
			const sg_rune_domain_boundary_facet_t *record =
				&model->domain_boundary_facets[facet];
			uint32_t vertex;
			if (!SG_RuneStateDomainIdValid(&record->domain) ||
			    !GeometryStableIdSame(&record->domain.value,
				&support->domain.value) ||
			    record->vertices.first != next_vertex ||
			    record->vertices.count != SG_RUNE_STATE_DIMENSION_COUNT ||
			    (size_t)record->vertices.first + record->vertices.count >
				model->domain_boundary_vertex_count ||
			    (record->orientation != -1 && record->orientation != 1) ||
			    record->reserved[0] != 0U || record->reserved[1] != 0U ||
			    record->reserved[2] != 0U ||
			    !SG_RuneDomainBoundaryProofRefValid(&record->proof) ||
			    record->proof.value.source_set_identity !=
				model->id.value.source_set_identity)
				return 0;
			manifest[facet].domain = domain;
			manifest[facet].chart = GeometryChartIndex(model,
				&model->state_domains[domain].chart);
			if (manifest[facet].chart == SIZE_MAX)
				return 0;
			manifest[facet].cell = facet;
			manifest[facet].orientation = record->orientation;
			for (vertex = 0U; vertex < SG_RUNE_STATE_DIMENSION_COUNT;
			     vertex++)
			{
				manifest[facet].vertices[vertex] =
					GeometryFindRefinementVertex(&model->refinement_tree,
						&model->domain_boundary_vertices[
							(size_t)record->vertices.first + vertex]);
				if (!manifest[facet].vertices[vertex])
					return 0;
			}
			SortRefinementVertices(manifest[facet].vertices, 7U);
			for (vertex = 1U; vertex < SG_RUNE_STATE_DIMENSION_COUNT;
			     vertex++)
				if (RefinementCoordinatesEqual(
					manifest[facet].vertices[vertex - 1U],
					manifest[facet].vertices[vertex]))
					return 0;
			next_vertex += SG_RUNE_STATE_DIMENSION_COUNT;
		}
		next_facet += support->boundary_facets.count;
		next_word += support->normalized_volume.magnitude.count;
	}
	return next_facet == model->domain_boundary_facet_count &&
		next_vertex == model->domain_boundary_vertex_count &&
		next_word == model->exact_word_count &&
		GeometryManifestRidgesValid(manifest,
			model->domain_boundary_facet_count);
}

static int GeometryCellsProperlyMeet(const sg_rune_dynamics_model_t *model,
	const sg_geometry_cell_t *cells, size_t cell_count)
{
	size_t right;
	for (right = 0U; right < cell_count; right++)
	{
		size_t left;
		for (left = 0U; left < right; left++)
		{
			const sg_rune_state_domain_t *left_domain =
				&model->state_domains[cells[left].domain];
			const sg_rune_state_domain_t *right_domain =
				&model->state_domains[cells[right].domain];
			if (GeometryStableIdSame(&left_domain->chart.value,
				&right_domain->chart.value) &&
			    !SG_FieldRefinementCellsProperlyMeet(cells[left].vertices,
				cells[right].vertices))
				return 0;
		}
	}
	return 1;
}

static size_t GeometryManifestMatch(const sg_geometry_facet_t *facet,
	const sg_geometry_facet_t *manifest, size_t manifest_count)
{
	size_t match = SIZE_MAX;
	size_t candidate;
	for (candidate = 0U; candidate < manifest_count; candidate++)
		if (facet->domain == manifest[candidate].domain &&
		    facet->orientation == manifest[candidate].orientation &&
		    GeometryFacetCoordinatesEqual(facet, &manifest[candidate]))
		{
			if (match != SIZE_MAX)
				return SIZE_MAX - 1U;
			match = candidate;
		}
	return match;
}

static int GeometryBaseBoundaryValid(const sg_rune_dynamics_model_t *model,
	const sg_geometry_cell_t *cells, sg_geometry_facet_t *facets,
	const sg_geometry_facet_t *manifest)
{
	const size_t facet_count = model->state_simplex_count * 8U;
	uint8_t *matched = calloc(model->domain_boundary_facet_count,
		sizeof(*matched));
	size_t first = 0U;
	size_t domain;
	if (!matched)
		return 0;
	while (first < facet_count)
	{
		size_t end = first + 1U;
		while (end < facet_count &&
		       GeometryFacetCoordinatesEqual(&facets[first], &facets[end]))
			end++;
		if (end - first > 2U || (end - first == 2U &&
		    facets[first].orientation == facets[first + 1U].orientation))
		{
			free(matched);
			return 0;
		}
		{
			size_t incidence;
			for (incidence = first; incidence < end; incidence++)
			{
				int boundary = end - first == 1U ||
					facets[first].domain != facets[first + 1U].domain;
				if (boundary)
				{
					size_t match = GeometryManifestMatch(&facets[incidence],
						manifest, model->domain_boundary_facet_count);
					if (match >= model->domain_boundary_facet_count ||
					    matched[match])
					{
						free(matched);
						return 0;
					}
					matched[match] = 1U;
				}
			}
		}
		first = end;
	}
	for (domain = 0U; domain < model->state_domain_count; domain++)
	{
		sg_rune_exact_integer_t sum;
		int exponent;
		if (!GeometryExactSum(cells, model->state_simplex_count, domain,
			&sum, &exponent) || !GeometryCertificateSame(model,
			&model->domain_support[domain].normalized_volume, &sum, exponent))
		{
			free(matched);
			return 0;
		}
	}
	for (first = 0U; first < model->domain_boundary_facet_count; first++)
		if (!matched[first])
		{
			free(matched);
			return 0;
		}
	free(matched);
	return 1;
}

static int GeometryFacetContained(
	const sg_geometry_facet_t *candidate,
	const sg_geometry_facet_t *container, const uint8_t dimensions[6])
{
	uint32_t vertex;
	for (vertex = 0U; vertex < SG_RUNE_STATE_DIMENSION_COUNT; vertex++)
		if (!GeometryPointInFacet(container->vertices, dimensions,
			candidate->vertices[vertex]))
			return 0;
	return 1;
}

static int GeometryActiveBoundaryValid(const sg_rune_dynamics_model_t *model,
	const sg_geometry_cell_t *cells, size_t cell_count,
	sg_geometry_facet_t *facets, const sg_geometry_facet_t *manifest)
{
	const size_t facet_count = cell_count * 8U;
	size_t *assignment;
	if (facet_count > SIZE_MAX / sizeof(*assignment))
		return 0;
	assignment = malloc(facet_count * sizeof(*assignment));
	sg_rune_exact_integer_t *measure = calloc(facet_count, sizeof(*measure));
	int *measure_exponent = calloc(facet_count, sizeof(*measure_exponent));
	uint8_t (*projection)[6] = calloc(model->domain_boundary_facet_count,
		sizeof(*projection));
	sg_rune_exact_integer_t *manifest_measure = calloc(
		model->domain_boundary_facet_count, sizeof(*manifest_measure));
	int *manifest_exponent = calloc(model->domain_boundary_facet_count,
		sizeof(*manifest_exponent));
	size_t first = 0U;
	size_t manifest_index;
	int valid = 0;
	if (!assignment || !measure || !measure_exponent || !projection ||
	    !manifest_measure || !manifest_exponent)
		goto cleanup;
	for (first = 0U; first < facet_count; first++)
		assignment[first] = SIZE_MAX;
	for (manifest_index = 0U;
	     manifest_index < model->domain_boundary_facet_count; manifest_index++)
		if (!GeometryFacetProjection(manifest[manifest_index].vertices,
			projection[manifest_index], &manifest_measure[manifest_index],
			&manifest_exponent[manifest_index]))
			goto cleanup;
	first = 0U;
	while (first < facet_count)
	{
		size_t end = first + 1U;
		while (end < facet_count &&
		       GeometryFacetCoordinatesEqual(&facets[first], &facets[end]))
			end++;
		if (end - first > 2U || (end - first == 2U &&
		    facets[first].orientation == facets[first + 1U].orientation))
			goto cleanup;
		{
			size_t incidence;
			for (incidence = first; incidence < end; incidence++)
			{
				int boundary = end - first == 1U ||
					facets[first].domain != facets[first + 1U].domain;
				if (boundary)
				{
					size_t candidate;
					size_t match = SIZE_MAX;
					for (candidate = 0U;
					     candidate < model->domain_boundary_facet_count;
					     candidate++)
						if (facets[incidence].domain ==
							manifest[candidate].domain &&
						    GeometryFacetContained(&facets[incidence],
							&manifest[candidate], projection[candidate]))
						{
							if (match != SIZE_MAX)
								goto cleanup;
							match = candidate;
						}
					if (match == SIZE_MAX)
						goto cleanup;
					measure[incidence] = GeometryProjectedFacetMeasure(
						facets[incidence].vertices, projection[match],
						&measure_exponent[incidence]);
					if (measure[incidence].overflow ||
					    measure[incidence].count == 0U ||
					    facets[incidence].orientation *
						(measure[incidence].negative ? -1 : 1) !=
					    manifest[match].orientation *
						(manifest_measure[match].negative ? -1 : 1))
						goto cleanup;
					assignment[incidence] = match;
				}
			}
		}
		first = end;
	}
	for (manifest_index = 0U;
	     manifest_index < model->domain_boundary_facet_count; manifest_index++)
	{
		int common = 0;
		int have = 0;
		sg_rune_exact_integer_t sum = { { 0U }, 0U, 0, 0 };
		for (first = 0U; first < facet_count; first++)
			if (assignment[first] == manifest_index && (!have ||
			    measure_exponent[first] < common))
			{
				common = measure_exponent[first];
				have = 1;
			}
		if (!have)
			goto cleanup;
		for (first = 0U; first < facet_count; first++)
			if (assignment[first] == manifest_index)
			{
				sg_rune_exact_integer_t magnitude = measure[first];
				sg_rune_exact_integer_t aligned;
				magnitude.negative = 0;
				aligned = ExactMagnitudeShiftLeft(&magnitude,
					(uint32_t)(measure_exponent[first] - common));
				sum = ExactMagnitudeAdd(&sum, &aligned);
				if (aligned.overflow || sum.overflow)
					goto cleanup;
			}
		if (!GeometryDyadicsEqual(&sum, common,
			&manifest_measure[manifest_index],
			manifest_exponent[manifest_index]))
			goto cleanup;
	}
	{
		size_t domain;
		for (domain = 0U; domain < model->state_domain_count; domain++)
		{
			sg_rune_exact_integer_t sum;
			int exponent;
			if (!GeometryExactSum(cells, cell_count, domain, &sum, &exponent) ||
			    !GeometryCertificateSame(model,
				&model->domain_support[domain].normalized_volume,
				&sum, exponent))
				goto cleanup;
		}
	}
	valid = 1;
cleanup:
	free(assignment);
	free(measure);
	free(measure_exponent);
	free(projection);
	free(manifest_measure);
	free(manifest_exponent);
	return valid;
}

int SG_RuneDynamicsGeometryValid(const sg_rune_dynamics_model_t *model)
{
	sg_field_refinement_vertex_t *state_points = NULL;
	sg_geometry_cell_t *base_cells = NULL;
	sg_geometry_cell_t *active_cells = NULL;
	sg_geometry_facet_t *base_facets = NULL;
	sg_geometry_facet_t *active_facets = NULL;
	sg_geometry_facet_t *manifest = NULL;
	size_t active_count = 0U;
	size_t chart_index;
	size_t index;
	int valid = 0;

	if (!model || model->state_simplex_count > SIZE_MAX / 8U ||
	    model->refinement_tree.node_count > SIZE_MAX / 8U ||
	    model->state_simplex_count >
		SIZE_MAX / 8U / sizeof(*base_facets) ||
	    model->refinement_tree.node_count >
		SIZE_MAX / 8U / sizeof(*active_facets))
		return 0;
	state_points = calloc(model->state_vertex_count, sizeof(*state_points));
	base_cells = calloc(model->state_simplex_count, sizeof(*base_cells));
	base_facets = calloc(model->state_simplex_count * 8U,
		sizeof(*base_facets));
	manifest = calloc(model->domain_boundary_facet_count, sizeof(*manifest));
	if (!state_points || !base_cells || !base_facets || !manifest ||
	    !GeometryManifestValid(model, manifest))
		goto cleanup;
	for (chart_index = 0U; chart_index < model->state_chart_count;
	     chart_index++)
	{
		const sg_rune_state_chart_t *chart = &model->state_charts[chart_index];
		for (index = chart->state_vertices.first;
		     index < (size_t)chart->state_vertices.first +
			chart->state_vertices.count; index++)
			if (!VertexInsideEmbedding(&model->state_vertices[index],
				&chart->embedding))
				goto cleanup;
	}
	for (index = 0U; index < model->state_vertex_count; index++)
		GeometryStatePoint(&model->state_vertices[index], &state_points[index]);
	for (index = 0U; index < model->state_simplex_count; index++)
	{
		const sg_rune_state_simplex_t *simplex = &model->state_simplices[index];
		uint32_t vertex;
		if (!SimplexFullRank(simplex, model->state_vertices))
			goto cleanup;
		base_cells[index].domain = GeometryDomainIndex(model,
			&model->simplex_owners[index].domain);
		base_cells[index].chart = GeometryChartIndex(model, &simplex->chart);
		base_cells[index].atom = GeometryAtomIndex(model,
			&model->simplex_owners[index].atom);
		base_cells[index].source = index;
		if (base_cells[index].chart == SIZE_MAX ||
		    base_cells[index].domain == SIZE_MAX ||
		    base_cells[index].atom == SIZE_MAX)
			goto cleanup;
		for (vertex = 0U; vertex < 8U; vertex++)
			base_cells[index].vertices[vertex] = &state_points[
				(size_t)simplex->vertices.first + vertex];
		SortRefinementVertices(base_cells[index].vertices, 8U);
		base_cells[index].volume = RefinementCellMeasure(
			base_cells[index].vertices, &base_cells[index].volume_exponent);
		if (base_cells[index].volume.overflow ||
		    base_cells[index].volume.count == 0U)
			goto cleanup;
	}
	if (!GeometryCellsProperlyMeet(model, base_cells,
		model->state_simplex_count))
		goto cleanup;
	GeometryBuildFacets(base_cells, model->state_simplex_count, base_facets);
	if (!GeometryBaseBoundaryValid(model, base_cells, base_facets, manifest))
		goto cleanup;
	for (index = 0U; index < model->refinement_tree.node_count; index++)
	{
		if (!GeometryParentCovered(model, &model->refinement_tree.nodes[index]))
			goto cleanup;
		if (model->refinement_tree.nodes[index].children.count == 0U)
			active_count++;
	}
	if (active_count == 0U)
		goto cleanup;
	active_cells = calloc(active_count, sizeof(*active_cells));
	active_facets = calloc(active_count * 8U, sizeof(*active_facets));
	if (!active_cells || !active_facets)
		goto cleanup;
	active_count = 0U;
	for (index = 0U; index < model->refinement_tree.node_count; index++)
		if (model->refinement_tree.nodes[index].children.count == 0U)
		{
			const sg_field_refinement_node_t *node =
				&model->refinement_tree.nodes[index];
			size_t atom = GeometryAtomIndex(model, &node->atom);
			if (atom == SIZE_MAX || !GeometryLoadNodeCell(
				&model->refinement_tree, node, &active_cells[active_count]))
				goto cleanup;
			active_cells[active_count].atom = atom;
			active_cells[active_count].domain = GeometryDomainIndex(model,
				&model->reach_atoms[atom].domain);
			if (active_cells[active_count].domain != SIZE_MAX)
				active_cells[active_count].chart = GeometryChartIndex(model,
					&model->state_domains[
						active_cells[active_count].domain].chart);
			active_cells[active_count].source = index;
			if (active_cells[active_count].domain == SIZE_MAX ||
			    active_cells[active_count].chart == SIZE_MAX)
				goto cleanup;
			active_count++;
		}
	if (!GeometryCellsProperlyMeet(model, active_cells, active_count))
		goto cleanup;
	GeometryBuildFacets(active_cells, active_count, active_facets);
	if (!GeometryActiveBoundaryValid(model, active_cells, active_count,
		active_facets, manifest))
		goto cleanup;
	valid = 1;
cleanup:
	free(state_points);
	free(base_cells);
	free(active_cells);
	free(base_facets);
	free(active_facets);
	free(manifest);
	return valid;
}
