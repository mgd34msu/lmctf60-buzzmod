#include "sg_hook_visibility_feasibility_internal.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SetError SG_HookVisibilityFeasibilitySetError

typedef sg_hook_visibility_build_context_t hook_build_t;

typedef struct event_cut_set_s
{
	int16_t *values;
	uint32_t count;
	uint32_t capacity;
	int16_t *split_values;
	uint32_t split_count;
	uint32_t split_capacity;
	sg_hook_visibility_feasibility_error_code_t error;
	int16_t minimum;
	int16_t maximum;
} event_cut_set_t;

static int AllocationFits(size_t count, size_t element_size)
{
	return !element_size || count <= SIZE_MAX / element_size;
}

static int CompareI16(const void *left, const void *right)
{
	int16_t a = *(const int16_t *)left;
	int16_t b = *(const int16_t *)right;

	return (a > b) - (a < b);
}

static void AddCode(event_cut_set_t *set, int32_t value)
{
	int16_t *resized;
	uint32_t capacity;

	if (value < set->minimum || value > set->maximum || set->error)
		return;
	if (set->count == set->capacity)
	{
		if (set->capacity > UINT32_MAX / 2U)
		{
			set->error = SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_OVERFLOW;
			return;
		}
		capacity = set->capacity ? set->capacity * 2U : 64U;
		if (!AllocationFits(capacity, sizeof(*resized)))
		{
			set->error = SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_OVERFLOW;
			return;
		}
		resized = realloc(set->values, (size_t)capacity * sizeof(*resized));
		if (!resized)
		{
			set->error = SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_OUT_OF_MEMORY;
			return;
		}
		set->values = resized;
		set->capacity = capacity;
	}
	set->values[set->count++] = (int16_t)value;
}

static void AddFloat(event_cut_set_t *set, float q8)
{
	float lower, upper;

	if (!isfinite(q8) || q8 < (float)INT16_MIN - 1.0f ||
		q8 > (float)INT16_MAX + 1.0f)
		return;
	lower = floorf(q8);
	upper = ceilf(q8);
	AddCode(set, (int32_t)lower);
	AddCode(set, (int32_t)upper);
}

static void AddSplitCode(event_cut_set_t *set, int32_t value)
{
	int16_t *resized;
	uint32_t capacity;

	if (value < set->minimum || value > set->maximum || set->error)
		return;
	if (set->split_count == set->split_capacity)
	{
		if (set->split_capacity > UINT32_MAX / 2U)
		{
			set->error = SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_OVERFLOW;
			return;
		}
		capacity = set->split_capacity ? set->split_capacity * 2U : 64U;
		if (!AllocationFits(capacity, sizeof(*resized)))
		{
			set->error = SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_OVERFLOW;
			return;
		}
		resized = realloc(set->split_values,
			(size_t)capacity * sizeof(*resized));
		if (!resized)
		{
			set->error = SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_OUT_OF_MEMORY;
			return;
		}
		set->split_values = resized;
		set->split_capacity = capacity;
	}
	set->split_values[set->split_count++] = (int16_t)value;
}

static void AddEventCut(event_cut_set_t *set, float q8)
{
	float boundary;

	if (!isfinite(q8) || q8 < (float)INT16_MIN - 1.0f ||
		q8 > (float)INT16_MAX + 1.0f)
		return;
	if (q8 == truncf(q8))
	{
		AddCode(set, (int32_t)q8);
		return;
	}
	boundary = ceilf(q8);
	AddSplitCode(set, (int32_t)boundary);
}

static void BrushBounds(const hook_build_t *build, uint32_t rule,
	float minimum[3], float maximum[3])
{
	const sg_bsp_world_t *world = build->sources->collision->world;
	const sg_bsp_brush_t *brush = &world->brushes[
		build->sources->surface_rules[rule].brush_index];
	uint32_t axis, side_offset;

	for (axis = 0U; axis < 3U; axis++)
	{
		minimum[axis] = -INFINITY;
		maximum[axis] = INFINITY;
	}
	for (side_offset = 0U; side_offset < brush->side_count; side_offset++)
	{
		const sg_bsp_brush_side_t *side =
			&world->brush_sides[brush->first_side + side_offset];
		const sg_bsp_plane_t *plane = &world->planes[side->plane];

		for (axis = 0U; axis < 3U; axis++)
			if (plane->normal.value[axis] == 1.0f)
				maximum[axis] = plane->distance;
			else if (plane->normal.value[axis] == -1.0f)
				minimum[axis] = -plane->distance;
	}
}

static void MuzzleOffset(const hook_build_t *build, int16_t pitch,
	int16_t yaw, sg_hook_visibility_hand_t hand, float forward[3],
	float offset[3])
{
	float right[3];
	float lateral = build->sources->fire_law.muzzle_lateral;
	float view = build->sources->stance == SG_RUNE_STANCE_STANDING ?
		build->sources->fire_law.standing_view_height :
		build->sources->fire_law.crouching_view_height;
	uint32_t axis;

	if (hand == SG_HOOK_VISIBILITY_HAND_LEFT)
		lateral = -lateral;
	else if (hand == SG_HOOK_VISIBILITY_HAND_CENTER)
		lateral = 0.0f;
	SG_HookVisibilityFeasibilityDirection(pitch, yaw, forward, right);
	for (axis = 0U; axis < 3U; axis++)
		offset[axis] = forward[axis] *
			build->sources->fire_law.muzzle_forward + right[axis] * lateral;
	offset[2] += view - build->sources->fire_law.muzzle_forward;
}

static void BoundaryEvents(float minimum, float maximum, float epsilon,
	float events[6])
{
	events[0] = minimum;
	events[1] = maximum;
	events[2] = minimum - epsilon;
	events[3] = minimum + epsilon;
	events[4] = maximum - epsilon;
	events[5] = maximum + epsilon;
}

static float ProjectedBoundary(uint32_t varying_axis, float varying_origin,
	float face, uint32_t projected_axis, float boundary,
	const float start_offset[3], const float delta[3],
	float minimum_parameter, float maximum_parameter)
{
	float parameter;

	if (delta[varying_axis] == 0.0f)
		return NAN;
	parameter = (face - varying_origin - start_offset[varying_axis]) /
		delta[varying_axis];
	if (parameter < minimum_parameter || parameter > maximum_parameter)
		return NAN;
	return (boundary - start_offset[projected_axis] -
		delta[projected_axis] * parameter) * 8.0f;
}

static void Segment(const hook_build_t *build, const float forward[3],
	const float offset[3], int clearance, float start[3], float delta[3],
	float *limit)
{
	if (clearance)
	{
		memcpy(delta, offset, sizeof(float) * 3U);
		memset(start, 0, sizeof(float) * 3U);
		*limit = 1.0f;
	}
	else
	{
		memcpy(delta, forward, sizeof(float) * 3U);
		memcpy(start, offset, sizeof(float) * 3U);
		*limit = build->sources->fire_law.maximum_range;
	}
}

static void AddEndpointEvents(const hook_build_t *build,
	event_cut_set_t *set, uint32_t output_axis, const float minimum[3],
	const float maximum[3], const float forward[3], const float offset[3])
{
	float boundaries[6];
	uint32_t clearance, event;

	BoundaryEvents(minimum[output_axis], maximum[output_axis],
		build->sources->fire_law.trace_epsilon, boundaries);
	if (output_axis != 0U)
	{
		for (event = 0U; event < 6U; event++)
			AddFloat(set, boundaries[event] * 8.0f);
		return;
	}
	for (clearance = 0U; clearance < 2U; clearance++)
	{
		float start[3], delta[3], limit;

		Segment(build, forward, offset, (int)clearance, start, delta,
			&limit);
		for (event = 0U; event < 6U; event++)
		{
			AddFloat(set, (boundaries[event] - start[output_axis]) * 8.0f);
			AddFloat(set, (boundaries[event] - start[output_axis] -
				delta[output_axis] * limit) * 8.0f);
		}
	}
}

static void AddPairEvents(const hook_build_t *build, event_cut_set_t *set,
	uint32_t output_axis, uint32_t first_axis, uint32_t second_axis,
	const sg_hook_visibility_i16_span_t prior_spans[3],
	const float minimum[3], const float maximum[3], const float forward[3],
	const float offset[3], int clearance_only, int incoming_only)
{
	float first_boundaries[2], second_boundaries[6];
	float minimum_parameter = 0.0f, maximum_parameter = 1.0f;
	int32_t first_code, last_code;
	uint32_t clearance, first_event, second_event, first_event_count = 2U;

	if (output_axis != first_axis && output_axis != second_axis)
		return;
	if (clearance_only)
	{
		float origin_min = (float)prior_spans[0].minimum * 0.125f;
		float origin_max = (float)prior_spans[0].maximum * 0.125f;
		float x_shift = offset[0] > 0.0f ?
			-build->sources->fire_law.trace_epsilon :
			build->sources->fire_law.trace_epsilon;
		float brush_min = minimum[0] + x_shift;
		float brush_max = maximum[0] + x_shift;

		if (offset[0] > 0.0f)
		{
			minimum_parameter = (brush_min - origin_max) / offset[0];
			maximum_parameter = (brush_max - origin_min) / offset[0];
		}
		else
		{
			minimum_parameter = (brush_max - origin_min) / offset[0];
			maximum_parameter = (brush_min - origin_max) / offset[0];
		}
		if (minimum_parameter < 0.0f)
			minimum_parameter = 0.0f;
		if (maximum_parameter > 1.0f)
			maximum_parameter = 1.0f;
		if (minimum_parameter > maximum_parameter)
			return;
	}
	if (output_axis == second_axis)
	{
		first_code = prior_spans[first_axis].minimum;
		last_code = prior_spans[first_axis].maximum;
	}
	else
	{
		first_code = build->sources->origins.mins[first_axis];
		last_code = build->sources->origins.maxs[first_axis];
	}
	if (clearance_only)
	{
		float first_shift = offset[first_axis] > 0.0f ?
			-build->sources->fire_law.trace_epsilon :
			build->sources->fire_law.trace_epsilon;
		float second_shift = offset[second_axis] > 0.0f ?
			-build->sources->fire_law.trace_epsilon :
			build->sources->fire_law.trace_epsilon;

		first_boundaries[0] = (offset[first_axis] > 0.0f ?
			minimum[first_axis] : maximum[first_axis]) + first_shift;
		second_boundaries[0] = (offset[second_axis] > 0.0f ?
			maximum[second_axis] : minimum[second_axis]) + second_shift;
		first_boundaries[1] = (offset[first_axis] > 0.0f ?
			maximum[first_axis] : minimum[first_axis]) + first_shift;
		second_boundaries[1] = (offset[second_axis] > 0.0f ?
			minimum[second_axis] : maximum[second_axis]) + second_shift;
	}
	else
	{
		first_boundaries[0] = incoming_only ?
			(forward[first_axis] > 0.0f ? minimum[first_axis] :
			 maximum[first_axis]) : minimum[first_axis];
		first_boundaries[1] = maximum[first_axis];
		if (incoming_only)
			first_event_count = 1U;
		BoundaryEvents(minimum[second_axis], maximum[second_axis],
			build->sources->fire_law.trace_epsilon, second_boundaries);
	}
	for (clearance = clearance_only ? 1U : 0U; clearance < 2U; clearance++)
		for (first_event = 0U; first_event < first_event_count; first_event++)
			for (second_event = clearance_only ? first_event : 0U;
				second_event < (clearance_only ? first_event + 1U : 6U);
				second_event++)
			{
				float start[3], delta[3], limit;
				float first, last, low, high;
				float valid_first, valid_last;
				int32_t code, event_first_code, event_last_code;

				Segment(build, forward, offset, (int)clearance, start, delta,
					&limit);
				if (!clearance_only)
				{
					minimum_parameter = 0.0f;
					maximum_parameter = limit;
				}
				valid_first = (first_boundaries[first_event] -
					start[first_axis] - delta[first_axis] *
					minimum_parameter) * 8.0f;
				valid_last = (first_boundaries[first_event] -
					start[first_axis] - delta[first_axis] *
					maximum_parameter) * 8.0f;
				low = valid_first < valid_last ? valid_first : valid_last;
				high = valid_first > valid_last ? valid_first : valid_last;
				event_first_code = first_code > (int32_t)ceilf(low) ?
					first_code : (int32_t)ceilf(low);
				event_last_code = last_code < (int32_t)floorf(high) ?
					last_code : (int32_t)floorf(high);
				if (event_first_code > event_last_code)
					continue;
				first = ProjectedBoundary(first_axis,
					(float)event_first_code * 0.125f,
					first_boundaries[first_event], second_axis,
					second_boundaries[second_event], start, delta,
					minimum_parameter, maximum_parameter);
				last = ProjectedBoundary(first_axis,
					(float)event_last_code * 0.125f,
					first_boundaries[first_event], second_axis,
					second_boundaries[second_event], start, delta,
					minimum_parameter, maximum_parameter);
				if (!isfinite(first) || !isfinite(last))
					continue;
				low = first < last ? first : last;
				high = first > last ? first : last;
				if (low < (float)build->sources->origins.mins[second_axis] -
						1.0f)
					low = (float)build->sources->origins.mins[second_axis] -
						1.0f;
				if (high > (float)build->sources->origins.maxs[second_axis] +
						1.0f)
					high = (float)build->sources->origins.maxs[second_axis] +
						1.0f;
				if (low > high)
					continue;
				if (output_axis == second_axis)
				{
					AddEventCut(set, first);
					AddEventCut(set, last);
				}
				else if (first != last)
					for (code = (int32_t)ceilf(low); (float)code <= high;
						code++)
					{
						float crossing = (float)event_first_code +
							((float)code - first) *
							(float)(event_last_code - event_first_code) /
							(last - first);

						AddEventCut(set, crossing);
					}
			}
}

static void AddControlEvents(const hook_build_t *build, event_cut_set_t *set,
	uint32_t output_axis, int16_t pitch, int16_t yaw,
	sg_hook_visibility_hand_t hand,
	const sg_hook_visibility_i16_span_t prior_spans[3])
{
	uint32_t rule;

	for (rule = 0U; rule < build->sources->surface_rule_count; rule++)
	{
		float minimum[3], maximum[3], forward[3], offset[3];
		float source_min, source_max;
		float clearance_first, clearance_last, clearance_min, clearance_max;
		int guard, guard_relevant;

		BrushBounds(build, rule, minimum, maximum);
		MuzzleOffset(build, pitch, yaw, hand, forward, offset);
		source_min = (float)build->sources->origins.mins[0] * 0.125f;
		source_max = (float)build->sources->origins.maxs[0] * 0.125f;
		guard = forward[0] > 0.0f ? minimum[0] <= source_min &&
			maximum[0] < source_max : maximum[0] >= source_max &&
			minimum[0] > source_min;
		clearance_first = (float)prior_spans[0].minimum * 0.125f;
		clearance_last = (float)prior_spans[0].maximum * 0.125f;
		clearance_min = offset[0] < 0.0f ? clearance_first + offset[0] :
			clearance_first;
		clearance_max = offset[0] > 0.0f ? clearance_last + offset[0] :
			clearance_last;
		guard_relevant = guard && output_axis != 0U &&
			clearance_max >= minimum[0] -
				build->sources->fire_law.trace_epsilon &&
			clearance_min <= maximum[0] +
				build->sources->fire_law.trace_epsilon;
		AddEndpointEvents(build, set, output_axis, minimum, maximum, forward,
			offset);
		AddPairEvents(build, set, output_axis, 0U, 1U, prior_spans,
			minimum, maximum, forward, offset, 0, !guard);
		AddPairEvents(build, set, output_axis, 0U, 2U, prior_spans,
			minimum, maximum, forward, offset, 0, !guard);
		if (guard_relevant)
			AddPairEvents(build, set, output_axis, 1U, 2U, prior_spans,
				minimum, maximum, forward, offset, 1, 0);
	}
}

int SG_HookVisibilityFeasibilityEventCuts(
	sg_hook_visibility_build_context_t *build, uint32_t axis, int16_t pitch,
	int16_t yaw, sg_hook_visibility_hand_t hand,
	const sg_hook_visibility_i16_span_t prior_spans[3],
	int16_t **cuts_out, uint32_t *count_out, int16_t **split_cuts_out,
	uint32_t *split_count_out)
{
	event_cut_set_t set;
	uint32_t read, write;

	memset(&set, 0, sizeof(set));
	set.minimum = build->sources->origins.mins[axis];
	set.maximum = build->sources->origins.maxs[axis];
	AddControlEvents(build, &set, axis, pitch, yaw, hand, prior_spans);
	if (set.error)
	{
		free(set.split_values);
		free(set.values);
		SetError(build, set.error, 0U);
		return 0;
	}
	if (set.count)
		qsort(set.values, set.count, sizeof(*set.values), CompareI16);
	write = set.count ? 1U : 0U;
	for (read = 1U; read < set.count; read++)
		if (set.values[read] != set.values[write - 1U])
			set.values[write++] = set.values[read];
	set.count = write;
	if (set.split_count)
		qsort(set.split_values, set.split_count, sizeof(*set.split_values),
			CompareI16);
	write = set.split_count ? 1U : 0U;
	for (read = 1U; read < set.split_count; read++)
		if (set.split_values[read] != set.split_values[write - 1U])
			set.split_values[write++] = set.split_values[read];
	set.split_count = write;
	if (!set.values)
	{
		set.values = calloc(1U, sizeof(*set.values));
		if (!set.values)
		{
			free(set.split_values);
			SetError(build,
				SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_OUT_OF_MEMORY, 0U);
			return 0;
		}
	}
	if (!set.split_values)
	{
		set.split_values = calloc(1U, sizeof(*set.split_values));
		if (!set.split_values)
		{
			free(set.values);
			SetError(build,
				SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_OUT_OF_MEMORY, 0U);
			return 0;
		}
	}
	*cuts_out = set.values;
	*count_out = set.count;
	*split_cuts_out = set.split_values;
	*split_count_out = set.split_count;
	return 1;
}
