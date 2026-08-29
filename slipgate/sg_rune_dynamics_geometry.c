#include "sg_rune_dynamics_model_internal.h"

#include <float.h>
#include <math.h>

static long double VertexCoordinate(const sg_rune_state_vertex_t *vertex,
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
		long double coordinate = VertexCoordinate(vertex, dimension);

		if (coordinate < interval->min_value ||
		    coordinate > interval->max_value)
			return 0;
	}
	return 1;
}

static int SimplexFullRank(const sg_rune_state_simplex_t *simplex,
	const sg_rune_state_vertex_t *vertices,
	const sg_rune_state_embedding_t *embedding)
{
	long double matrix[SG_RUNE_STATE_DIMENSION_COUNT]
		[SG_RUNE_STATE_DIMENSION_COUNT];
	const long double dimension = SG_RUNE_STATE_DIMENSION_COUNT;
	const long double tolerance = LDBL_EPSILON * dimension * dimension;
	uint32_t row;
	uint32_t column;

	for (column = 0U; column < SG_RUNE_STATE_DIMENSION_COUNT; column++)
	{
		const sg_rune_interval_t *interval =
			EmbeddingInterval(embedding, column);
		long double width = (long double)interval->max_value -
			interval->min_value;

		if (width <= 0.0L)
			return 0;
		for (row = 0U; row < SG_RUNE_STATE_DIMENSION_COUNT; row++)
			matrix[row][column] =
				(VertexCoordinate(&vertices[simplex->vertices.first +
					row + 1U], column) -
				 VertexCoordinate(&vertices[simplex->vertices.first],
					column)) / width;
	}
	for (column = 0U; column < SG_RUNE_STATE_DIMENSION_COUNT; column++)
	{
		uint32_t pivot = column;
		long double pivot_size = fabsl(matrix[pivot][column]);

		for (row = column + 1U; row < SG_RUNE_STATE_DIMENSION_COUNT; row++)
		{
			long double candidate = fabsl(matrix[row][column]);
			if (candidate > pivot_size)
			{
				pivot = row;
				pivot_size = candidate;
			}
		}
		if (pivot_size <= tolerance)
			return 0;
		if (pivot != column)
			for (row = column; row < SG_RUNE_STATE_DIMENSION_COUNT; row++)
			{
				long double temporary = matrix[column][row];
				matrix[column][row] = matrix[pivot][row];
				matrix[pivot][row] = temporary;
			}
		for (row = column + 1U; row < SG_RUNE_STATE_DIMENSION_COUNT; row++)
		{
			long double factor = matrix[row][column] /
				matrix[column][column];
			uint32_t trailing;
			for (trailing = column + 1U;
			     trailing < SG_RUNE_STATE_DIMENSION_COUNT; trailing++)
				matrix[row][trailing] -=
					factor * matrix[column][trailing];
		}
	}
	return 1;
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
				model->state_vertices, &chart->embedding))
				return 0;
	}
	return 1;
}
