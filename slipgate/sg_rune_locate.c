#include "sg_rune_locate.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define BUCKET_UNITS 128

static float FloatBits(uint32_t bits)
{
	float value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

float SG_RuneCellViolation(const sg_rune_artifact_t *artifact, uint32_t cell,
	const float origin[3])
{
	const sg_rune_cx_view_t *cx;
	const sg_rune_cx_cell_t *record;
	float worst = 0.0f;
	uint32_t slot;

	if (!artifact || !origin)
		return INFINITY;
	cx = &artifact->complex;
	if (cell >= cx->cell_count)
		return INFINITY;
	record = &cx->cells[cell];
	for (slot = 0U; slot < record->incidences.count; slot++)
	{
		const sg_rune_cx_incidence_t *incidence =
			&cx->incidences[cx->cell_incidences[record->incidences.first + slot]];
		const sg_rune_cx_facet_t *facet = &cx->facets[incidence->facet];
		float normal[3], distance, signed_distance;

		normal[0] = FloatBits(facet->plane.normal_bits[0]);
		normal[1] = FloatBits(facet->plane.normal_bits[1]);
		normal[2] = FloatBits(facet->plane.normal_bits[2]);
		distance = FloatBits(facet->plane.distance_bits);
		signed_distance = normal[0] * origin[0] + normal[1] * origin[1] +
			normal[2] * origin[2] - distance;
		if (incidence->side == SG_RUNE_CX_NEGATIVE_SIDE)
		{
			if (signed_distance > worst)
				worst = signed_distance;
		}
		else if (-signed_distance > worst)
			worst = -signed_distance;
	}
	return worst;
}

static int BucketOf(const sg_rune_locator_t *locator, int32_t q8, uint32_t axis)
{
	int32_t index = (q8 - locator->origin_q8[axis]) / locator->bucket_q8;

	if (q8 < locator->origin_q8[axis])
		return -1;
	return index >= (int32_t)locator->dims[axis] ? -1 : index;
}

static int32_t Clamp(int32_t value, int32_t low, int32_t high)
{
	return value < low ? low : (value > high ? high : value);
}

int SG_RuneLocatorBuild(sg_rune_locator_t *locator,
	const sg_rune_artifact_t *artifact)
{
	const sg_rune_cx_view_t *cx;
	int32_t mins[3], maxs[3];
	uint32_t cell, axis, bucket_count, pass;
	uint32_t *fill;

	if (!locator)
		return 0;
	memset(locator, 0, sizeof(*locator));
	if (!artifact || artifact->complex.cell_count == 0U)
		return 0;
	cx = &artifact->complex;
	for (axis = 0U; axis < 3U; axis++)
	{
		mins[axis] = cx->cells[0].bounds.mins.value[axis];
		maxs[axis] = cx->cells[0].bounds.maxs.value[axis];
	}
	for (cell = 1U; cell < cx->cell_count; cell++)
		for (axis = 0U; axis < 3U; axis++)
		{
			if (cx->cells[cell].bounds.mins.value[axis] < mins[axis])
				mins[axis] = cx->cells[cell].bounds.mins.value[axis];
			if (cx->cells[cell].bounds.maxs.value[axis] > maxs[axis])
				maxs[axis] = cx->cells[cell].bounds.maxs.value[axis];
		}
	locator->artifact = artifact;
	locator->bucket_q8 = BUCKET_UNITS * SG_RUNE_CX_Q8_ONE;
	bucket_count = 1U;
	for (axis = 0U; axis < 3U; axis++)
	{
		locator->origin_q8[axis] = mins[axis];
		locator->dims[axis] = (uint32_t)((maxs[axis] - mins[axis]) /
			locator->bucket_q8) + 1U;
		if (bucket_count > UINT32_MAX / locator->dims[axis])
			return 0;
		bucket_count *= locator->dims[axis];
	}
	locator->first = calloc((size_t)bucket_count + 1U, sizeof(*locator->first));
	fill = calloc((size_t)bucket_count + 1U, sizeof(*fill));
	if (!locator->first || !fill)
	{
		free(fill);
		SG_RuneLocatorFree(locator);
		return 0;
	}
	for (pass = 0U; pass < 2U; pass++)
	{
		if (pass == 1U)
		{
			uint32_t bucket, total = 0U;

			for (bucket = 0U; bucket < bucket_count; bucket++)
			{
				uint32_t count = locator->first[bucket];

				locator->first[bucket] = total;
				total += count;
			}
			locator->first[bucket_count] = total;
			locator->entry_count = total;
			locator->entries = malloc((size_t)(total ? total : 1U) *
				sizeof(*locator->entries));
			if (!locator->entries)
			{
				free(fill);
				SG_RuneLocatorFree(locator);
				return 0;
			}
			memcpy(fill, locator->first, (size_t)bucket_count * sizeof(*fill));
		}
		for (cell = 0U; cell < cx->cell_count; cell++)
		{
			const sg_rune_cx_bounds_t *bounds = &cx->cells[cell].bounds;
			int32_t low[3], high[3], x, y, z;

			for (axis = 0U; axis < 3U; axis++)
			{
				low[axis] = Clamp((bounds->mins.value[axis] -
					locator->origin_q8[axis]) / locator->bucket_q8, 0,
					(int32_t)locator->dims[axis] - 1);
				high[axis] = Clamp((bounds->maxs.value[axis] -
					locator->origin_q8[axis]) / locator->bucket_q8, 0,
					(int32_t)locator->dims[axis] - 1);
			}
			for (z = low[2]; z <= high[2]; z++)
				for (y = low[1]; y <= high[1]; y++)
					for (x = low[0]; x <= high[0]; x++)
					{
						uint32_t bucket = ((uint32_t)z * locator->dims[1] +
							(uint32_t)y) * locator->dims[0] + (uint32_t)x;

						if (pass == 0U)
							locator->first[bucket]++;
						else
							locator->entries[fill[bucket]++] = cell;
					}
		}
	}
	free(fill);
	return 1;
}

void SG_RuneLocatorFree(sg_rune_locator_t *locator)
{
	if (!locator)
		return;
	free(locator->first);
	free(locator->entries);
	memset(locator, 0, sizeof(*locator));
}

uint32_t SG_RuneLocate(const sg_rune_locator_t *locator,
	const float origin[3], uint8_t stance, float slack, float *violation_out)
{
	int index[3];
	uint32_t bucket, slot, axis;
	uint32_t best = SG_RUNE_CX_INDEX_NONE;
	float best_violation = INFINITY;
	int best_stance = 0, best_supported = 0, supported;
	sg_rune_cx_stances_t wanted = 0U;

	if (violation_out)
		*violation_out = INFINITY;
	if (!locator || !locator->artifact || !origin || !isfinite(origin[0]) ||
		!isfinite(origin[1]) || !isfinite(origin[2]))
		return SG_RUNE_CX_INDEX_NONE;
	for (axis = 0U; axis < 3U; axis++)
	{
		float q8 = origin[axis] * (float)SG_RUNE_CX_Q8_ONE;

		if (q8 < (float)INT32_MIN || q8 > (float)INT32_MAX)
			return SG_RUNE_CX_INDEX_NONE;
		index[axis] = BucketOf(locator, (int32_t)q8, axis);
		if (index[axis] < 0)
			return SG_RUNE_CX_INDEX_NONE;
	}
	if (stance & SG_RUNE_MOVE_STANDING)
		wanted |= SG_RUNE_CX_STANCE_STANDING;
	if (stance & SG_RUNE_MOVE_CROUCHING)
		wanted |= SG_RUNE_CX_STANCE_CROUCHING;
	bucket = ((uint32_t)index[2] * locator->dims[1] + (uint32_t)index[1]) *
		locator->dims[0] + (uint32_t)index[0];
	for (slot = locator->first[bucket]; slot < locator->first[bucket + 1U];
		slot++)
	{
		uint32_t cell = locator->entries[slot];
		float violation = SG_RuneCellViolation(locator->artifact, cell, origin);
		int stance_ok = wanted == 0U ||
			(locator->artifact->complex.cells[cell].valid_stances & wanted) != 0U;

		if (violation > slack)
			continue;
		/* Inside a stance-valid cell beats everything; then nearer beats
		 * farther; a stance-valid near miss beats an invalid exact hit. */
		supported = (locator->artifact->complex.cells[cell].semantics &
			SG_RUNE_CX_CELL_SUPPORTED) != 0;
		if (best == SG_RUNE_CX_INDEX_NONE ||
			(stance_ok && !best_stance) ||
			(stance_ok == best_stance && violation < best_violation) ||
			(stance_ok == best_stance && violation == best_violation &&
				supported && !best_supported))
		{
			best = cell;
			best_violation = violation;
			best_stance = stance_ok;
			best_supported = supported;
			if (stance_ok && supported && violation <= 0.0f)
				break;
		}
	}
	if (violation_out)
		*violation_out = best_violation;
	return best;
}
