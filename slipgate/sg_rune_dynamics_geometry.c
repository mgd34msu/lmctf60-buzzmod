#include "sg_rune_dynamics_model_internal.h"

#include <float.h>
#include <limits.h>
#include <string.h>

#define SG_RUNE_BINARY32_RANK_PRIME_COUNT 68U

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
