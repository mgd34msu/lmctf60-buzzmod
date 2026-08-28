#include "sg_hook_visibility_feasibility_internal.h"

#include <math.h>
#include <stdlib.h>

#define SetError SG_HookVisibilityFeasibilitySetError

typedef sg_hook_visibility_build_context_t hook_build_t;

static int AllocationSize(hook_build_t *build, size_t count,
	size_t element_size)
{
	if (element_size && count > SIZE_MAX / element_size)
	{
		SetError(build, SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_OVERFLOW, 0U);
		return 0;
	}
	return 1;
}

static int CompareI16(const void *left, const void *right)
{
	int16_t left_value = *(const int16_t *)left;
	int16_t right_value = *(const int16_t *)right;

	return (left_value > right_value) - (left_value < right_value);
}

static int AddCut(int16_t *cuts, uint32_t *count, uint32_t capacity,
	float q8, int16_t minimum, int16_t maximum)
{
	int32_t value;

	if (q8 < (float)minimum || q8 > (float)maximum)
		return 1;
	value = (int32_t)q8;
	if (q8 != (float)value)
		return 1;
	if (*count >= capacity)
		return 0;
	cuts[(*count)++] = (int16_t)value;
	return 1;
}

static int CountAxisCuts(hook_build_t *build, uint32_t axis,
	uint32_t *capacity_out)
{
	const sg_bsp_world_t *world = build->sources->collision->world;
	uint32_t capacity = 0U, rule_index;

	for (rule_index = 0U; rule_index < build->sources->surface_rule_count;
		rule_index++)
	{
		const sg_hook_visibility_surface_rule_t *rule =
			&build->sources->surface_rules[rule_index];
		const sg_bsp_brush_t *brush = &world->brushes[rule->brush_index];
		uint32_t side_offset;

		for (side_offset = 0U; side_offset < brush->side_count; side_offset++)
		{
			const sg_bsp_brush_side_t *side =
				&world->brush_sides[brush->first_side + side_offset];
			const sg_bsp_plane_t *plane = &world->planes[side->plane];
			uint32_t additions;

			if (fabsf(plane->normal.value[axis]) != 1.0f)
				continue;
			additions = axis == 0U ? 5U :
				(axis == 1U ? SG_HOOK_VISIBILITY_HAND_COUNT : 1U);
			if (capacity > UINT32_MAX - additions)
			{
				SetError(build, SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_OVERFLOW,
					rule_index);
				return 0;
			}
			capacity += additions;
		}
	}
	*capacity_out = capacity;
	return 1;
}

static int AddAxisCuts(hook_build_t *build, uint32_t axis, int16_t *cuts,
	uint32_t capacity, uint32_t *count_out)
{
	const sg_bsp_world_t *world = build->sources->collision->world;
	float view_height = build->sources->stance == SG_RUNE_STANCE_STANDING ?
		build->sources->fire_law.standing_view_height :
		build->sources->fire_law.crouching_view_height;
	uint32_t rule_index, count = 0U;

	for (rule_index = 0U; rule_index < build->sources->surface_rule_count;
		rule_index++)
	{
		const sg_hook_visibility_surface_rule_t *rule =
			&build->sources->surface_rules[rule_index];
		const sg_bsp_brush_t *brush = &world->brushes[rule->brush_index];
		uint32_t side_offset;

		for (side_offset = 0U; side_offset < brush->side_count; side_offset++)
		{
			const sg_bsp_brush_side_t *side =
				&world->brush_sides[brush->first_side + side_offset];
			const sg_bsp_plane_t *plane = &world->planes[side->plane];
			float component = plane->normal.value[axis];
			float coordinate;
			int hand;

			if (fabsf(component) != 1.0f)
				continue;
			coordinate = plane->distance / component;
			if (axis == 0U)
			{
				float events[5];
				uint32_t event;

				events[0] = coordinate;
				events[1] = coordinate -
					build->sources->fire_law.muzzle_forward;
				events[2] = events[1] -
					build->sources->fire_law.maximum_range;
				events[3] = coordinate +
					build->sources->fire_law.muzzle_forward;
				events[4] = events[3] +
					build->sources->fire_law.maximum_range;
				for (event = 0U; event < 5U; event++)
					if (!AddCut(cuts, &count, capacity, events[event] * 8.0f,
							build->sources->origins.mins[axis],
							build->sources->origins.maxs[axis]))
						goto overflow;
				continue;
			}
			for (hand = 0; hand < (axis == 1U ?
					(int)SG_HOOK_VISIBILITY_HAND_COUNT : 1); hand++)
			{
				float hand_shift = 0.0f;
				float shifted;

				if (axis == 1U)
					hand_shift = hand == SG_HOOK_VISIBILITY_HAND_LEFT ?
						build->sources->fire_law.muzzle_lateral :
						(hand == SG_HOOK_VISIBILITY_HAND_RIGHT ?
						-build->sources->fire_law.muzzle_lateral : 0.0f);
				shifted = coordinate - (axis == 1U ? hand_shift :
					view_height - build->sources->fire_law.muzzle_forward);
				if (!AddCut(cuts, &count, capacity, shifted * 8.0f,
						build->sources->origins.mins[axis],
						build->sources->origins.maxs[axis]))
					goto overflow;
			}
		}
	}
	*count_out = count;
	return 1;

overflow:
	SetError(build, SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_OVERFLOW, rule_index);
	return 0;
}

static int AxisCuts(hook_build_t *build, uint32_t axis, int16_t **cuts_out,
	uint32_t *count_out)
{
	uint32_t capacity, count;
	int16_t *cuts;

	if (!CountAxisCuts(build, axis, &capacity) ||
		!AllocationSize(build, capacity ? capacity : 1U, sizeof(*cuts)))
		return 0;
	cuts = calloc(capacity ? capacity : 1U, sizeof(*cuts));
	if (!cuts)
	{
		SetError(build, SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_OUT_OF_MEMORY, 0U);
		return 0;
	}
	if (!AddAxisCuts(build, axis, cuts, capacity, &count))
	{
		free(cuts);
		return 0;
	}
	qsort(cuts, count, sizeof(*cuts), CompareI16);
	if (count)
	{
		uint32_t read, write = 1U;

		for (read = 1U; read < count; read++)
			if (cuts[read] != cuts[write - 1U])
				cuts[write++] = cuts[read];
		count = write;
	}
	*cuts_out = cuts;
	*count_out = count;
	return 1;
}

static int MakeSpans(hook_build_t *build, int16_t minimum, int16_t maximum,
	const int16_t *cuts, uint32_t cut_count,
	sg_hook_visibility_i16_span_t **spans_out, uint32_t *count_out)
{
	sg_hook_visibility_i16_span_t *spans;
	uint32_t capacity, count = 0U, cut;
	int32_t cursor = minimum;

	if (cut_count > (UINT32_MAX - 1U) / 2U)
	{
		SetError(build, SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_OVERFLOW, 0U);
		return 0;
	}
	capacity = cut_count * 2U + 1U;
	if (!AllocationSize(build, capacity, sizeof(*spans)))
		return 0;
	spans = calloc(capacity, sizeof(*spans));
	if (!spans)
	{
		SetError(build, SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_OUT_OF_MEMORY, 0U);
		return 0;
	}
	for (cut = 0U; cut < cut_count; cut++)
	{
		int32_t value = cuts[cut];

		if (cursor < value)
		{
			spans[count].minimum = (int16_t)cursor;
			spans[count++].maximum = (int16_t)(value - 1);
		}
		spans[count].minimum = (int16_t)value;
		spans[count++].maximum = (int16_t)value;
		cursor = value + 1;
	}
	if (cursor <= maximum)
	{
		spans[count].minimum = (int16_t)cursor;
		spans[count++].maximum = maximum;
	}
	*spans_out = spans;
	*count_out = count;
	return 1;
}

int SG_HookVisibilityFeasibilityAxisSpans(
	sg_hook_visibility_build_context_t *build, uint32_t axis,
	sg_hook_visibility_i16_span_t **spans_out, uint32_t *count_out)
{
	int16_t *cuts = NULL;
	uint32_t cut_count = 0U;
	int result;

	if (!AxisCuts(build, axis, &cuts, &cut_count))
		return 0;
	result = MakeSpans(build, build->sources->origins.mins[axis],
		build->sources->origins.maxs[axis], cuts, cut_count, spans_out,
		count_out);
	free(cuts);
	return result;
}
