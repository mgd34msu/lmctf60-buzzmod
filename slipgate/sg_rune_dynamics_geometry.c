#include "sg_rune_dynamics_model_internal.h"

#include <float.h>
#include <limits.h>
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
 * 7! * 2^(278*7) < 2^1959. The distinct moduli exceed 2^29 each. */
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

static int NextPermutation(uint8_t permutation[SG_RUNE_STATE_DIMENSION_COUNT])
{
	size_t pivot = SG_RUNE_STATE_DIMENSION_COUNT - 1U;
	size_t successor;
	size_t left;
	size_t right;
	while (pivot != 0U && permutation[pivot - 1U] >= permutation[pivot])
		pivot--;
	if (pivot == 0U)
		return 0;
	successor = SG_RUNE_STATE_DIMENSION_COUNT - 1U;
	while (permutation[successor] <= permutation[pivot - 1U])
		successor--;
	{
		uint8_t temporary = permutation[pivot - 1U];
		permutation[pivot - 1U] = permutation[successor];
		permutation[successor] = temporary;
	}
	left = pivot;
	right = SG_RUNE_STATE_DIMENSION_COUNT - 1U;
	while (left < right)
	{
		uint8_t temporary = permutation[left];
		permutation[left] = permutation[right];
		permutation[right] = temporary;
		left++;
		right--;
	}
	return 1;
}

static sg_rune_exact_integer_t ExactDeterminant(
	sg_rune_exact_integer_t matrix[SG_RUNE_STATE_DIMENSION_COUNT]
		[SG_RUNE_STATE_DIMENSION_COUNT])
{
	uint8_t permutation[SG_RUNE_STATE_DIMENSION_COUNT];
	sg_rune_exact_integer_t determinant = { { 0U }, 0U, 0, 0 };
	uint32_t index;
	for (index = 0U; index < SG_RUNE_STATE_DIMENSION_COUNT; index++)
		permutation[index] = (uint8_t)index;
	do
	{
		sg_rune_exact_integer_t term = { { 1U }, 1U, 0, 0 };
		uint32_t row;
		uint32_t inversions = 0U;
		for (row = 0U; row < SG_RUNE_STATE_DIMENSION_COUNT; row++)
		{
			uint32_t later;
			term = ExactMultiply(&term, &matrix[row][permutation[row]]);
			for (later = row + 1U; later < SG_RUNE_STATE_DIMENSION_COUNT;
			     later++)
				if (permutation[row] > permutation[later])
					inversions++;
		}
		if ((inversions & 1U) != 0U && term.count != 0U)
			term.negative = !term.negative;
		determinant = ExactAdd(&determinant, &term);
	} while (NextPermutation(permutation));
	return determinant;
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

static sg_rune_exact_integer_t ExactSubtract(
	const sg_rune_exact_integer_t *left,
	const sg_rune_exact_integer_t *right)
{
	sg_rune_exact_integer_t negative_right = *right;
	if (negative_right.count != 0U)
		negative_right.negative = !negative_right.negative;
	return ExactAdd(left, &negative_right);
}

static int ExactSameOrientationOrBoundary(
	const sg_rune_exact_integer_t *value,
	const sg_rune_exact_integer_t *orientation)
{
	return !value->overflow && value->count == 0U ? 1 :
		!value->overflow && !orientation->overflow &&
		value->negative == orientation->negative;
}

static int RefinementPointInCellExact(
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
		if (!RefinementPointInCellExact(vertices, &point))
			return 0;
	}
	return 1;
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
			return 2;
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

static int CellFacetSeparates(
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
			if (side == 2 || side == owner_side)
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

int SG_FieldRefinementCellsProperlyMeet(
	const sg_field_refinement_vertex_t *const left[8],
	const sg_field_refinement_vertex_t *const right[8])
{
	const sg_field_refinement_vertex_t *shared_face[7];
	const sg_field_refinement_vertex_t *left_opposite = NULL;
	const sg_field_refinement_vertex_t *right_opposite = NULL;
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
			if (left[left_vertex] == right[right_vertex])
			{
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
				if (right[right_vertex] == shared_face[face_vertex])
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
	return CellFacetSeparates(left, right) || CellFacetSeparates(right, left);
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

int SG_RuneDynamicsGeometryValid(const sg_rune_dynamics_model_t *model)
{
	size_t chart_index;

	for (chart_index = 0U; chart_index < model->state_chart_count;
	     chart_index++)
	{
		const sg_rune_state_chart_t *chart = &model->state_charts[chart_index];
		size_t index;

		for (index = chart->state_vertices.first;
		     index < (size_t)chart->state_vertices.first +
			chart->state_vertices.count; index++)
			if (!VertexInsideEmbedding(&model->state_vertices[index],
				&chart->embedding))
				return 0;
		for (index = chart->simplices.first;
		     index < (size_t)chart->simplices.first +
			chart->simplices.count; index++)
			if (!SimplexFullRank(&model->state_simplices[index],
				model->state_vertices))
				return 0;
	}
	return 1;
}
