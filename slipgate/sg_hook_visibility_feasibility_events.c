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

static float ProjectedBoundary(uint32_t target_axis, float boundary,
	float origin_x, float face_x, const float start_offset[3],
	const float delta[3], float maximum_parameter)
{
	float parameter;

	if (delta[0] == 0.0f)
		return NAN;
	parameter = (face_x - origin_x - start_offset[0]) / delta[0];
	if (parameter < 0.0f || parameter > maximum_parameter)
		return NAN;
	return (boundary - start_offset[target_axis] -
		delta[target_axis] * parameter) * 8.0f;
}

static void AddProjectionEvents(const hook_build_t *build,
	event_cut_set_t *set, uint32_t output_axis, int16_t pitch, int16_t yaw,
	sg_hook_visibility_hand_t hand, uint32_t rule, uint32_t target_axis,
	int clearance, const sg_hook_visibility_i16_span_t *x_span)
{
	float minimum[3], maximum[3], forward[3], offset[3];
	float delta[3], start_offset[3], limit, face;
	float boundaries[6];
	int32_t first_x, last_x;
	uint32_t event;

	BrushBounds(build, rule, minimum, maximum);
	MuzzleOffset(build, pitch, yaw, hand, forward, offset);
	if (clearance)
	{
		memcpy(delta, offset, sizeof(delta));
		memset(start_offset, 0, sizeof(start_offset));
		limit = 1.0f;
	}
	else
	{
		memcpy(delta, forward, sizeof(delta));
		memcpy(start_offset, offset, sizeof(start_offset));
		limit = build->sources->fire_law.maximum_range;
	}
	face = delta[0] > 0.0f ? minimum[0] : maximum[0];
	boundaries[0] = minimum[target_axis];
	boundaries[1] = maximum[target_axis];
	boundaries[2] = boundaries[0] - build->sources->fire_law.trace_epsilon;
	boundaries[3] = boundaries[0] + build->sources->fire_law.trace_epsilon;
	boundaries[4] = boundaries[1] - build->sources->fire_law.trace_epsilon;
	boundaries[5] = boundaries[1] + build->sources->fire_law.trace_epsilon;
	first_x = x_span ? x_span->minimum : build->sources->origins.mins[0];
	last_x = x_span ? x_span->maximum : build->sources->origins.maxs[0];
	for (event = 0U; event < 6U; event++)
	{
		float first = ProjectedBoundary(target_axis, boundaries[event],
			(float)first_x * 0.125f, face, start_offset, delta, limit);
		float last = ProjectedBoundary(target_axis, boundaries[event],
			(float)last_x * 0.125f, face, start_offset, delta, limit);
		float low, high;
		int32_t boundary;

		if (!isfinite(first) || !isfinite(last))
		{
			if (isfinite(first) != isfinite(last))
			{
				AddCode(set, first_x);
				AddCode(set, last_x);
			}
			continue;
		}
		low = first < last ? first : last;
		high = first > last ? first : last;
		if (low < (float)build->sources->origins.mins[target_axis] - 1.0f)
			low = (float)build->sources->origins.mins[target_axis] - 1.0f;
		if (high > (float)build->sources->origins.maxs[target_axis] + 1.0f)
			high = (float)build->sources->origins.maxs[target_axis] + 1.0f;
		if (low > high)
			continue;
		if (output_axis == target_axis)
		{
			AddFloat(set, first);
			AddFloat(set, last);
			for (boundary = (int32_t)ceilf(low);
				(float)boundary <= high; boundary++)
				AddCode(set, boundary);
		}
		else if (first != last)
			for (boundary = (int32_t)ceilf(low);
				(float)boundary <= high; boundary++)
			{
				float crossing = (float)first_x +
					((float)boundary - first) *
					(float)(last_x - first_x) / (last - first);

				AddFloat(set, crossing);
			}
	}
}

static void AddRayXEvents(const hook_build_t *build, event_cut_set_t *set,
	int16_t pitch, int16_t yaw, sg_hook_visibility_hand_t hand, uint32_t rule)
{
	float minimum[3], maximum[3], forward[3], offset[3];
	float boundaries[6];
	uint32_t boundary;

	BrushBounds(build, rule, minimum, maximum);
	MuzzleOffset(build, pitch, yaw, hand, forward, offset);
	boundaries[0] = minimum[0];
	boundaries[1] = maximum[0];
	boundaries[2] = minimum[0] - build->sources->fire_law.trace_epsilon;
	boundaries[3] = minimum[0] + build->sources->fire_law.trace_epsilon;
	boundaries[4] = maximum[0] - build->sources->fire_law.trace_epsilon;
	boundaries[5] = maximum[0] + build->sources->fire_law.trace_epsilon;
	for (boundary = 0U; boundary < 6U; boundary++)
	{
		AddFloat(set, boundaries[boundary] * 8.0f);
		AddFloat(set, (boundaries[boundary] - offset[0]) * 8.0f);
		AddFloat(set, (boundaries[boundary] - offset[0] -
			forward[0] * build->sources->fire_law.maximum_range) * 8.0f);
	}
}

static void AddControlEvents(const hook_build_t *build, event_cut_set_t *set,
	uint32_t output_axis, int16_t pitch, int16_t yaw,
	sg_hook_visibility_hand_t hand,
	const sg_hook_visibility_i16_span_t *x_span)
{
	uint32_t rule, target_axis;

	for (rule = 0U; rule < build->sources->surface_rule_count; rule++)
	{
		float minimum[3], maximum[3];

		BrushBounds(build, rule, minimum, maximum);
		if (output_axis == 0U)
			AddRayXEvents(build, set, pitch, yaw, hand, rule);
		else
		{
			AddFloat(set, minimum[output_axis] * 8.0f);
			AddFloat(set, maximum[output_axis] * 8.0f);
			AddFloat(set, (minimum[output_axis] -
				build->sources->fire_law.trace_epsilon) * 8.0f);
			AddFloat(set, (minimum[output_axis] +
				build->sources->fire_law.trace_epsilon) * 8.0f);
			AddFloat(set, (maximum[output_axis] -
				build->sources->fire_law.trace_epsilon) * 8.0f);
			AddFloat(set, (maximum[output_axis] +
				build->sources->fire_law.trace_epsilon) * 8.0f);
		}
		for (target_axis = 1U; target_axis < 3U; target_axis++)
		{
			if (output_axis != 0U && output_axis != target_axis)
				continue;
			AddProjectionEvents(build, set, output_axis, pitch, yaw, hand,
				rule, target_axis, 0, x_span);
			AddProjectionEvents(build, set, output_axis, pitch, yaw, hand,
				rule, target_axis, 1, x_span);
		}
	}
}

int SG_HookVisibilityFeasibilityEventCuts(
	sg_hook_visibility_build_context_t *build, uint32_t axis, int16_t pitch,
	int16_t yaw, sg_hook_visibility_hand_t hand,
	const sg_hook_visibility_i16_span_t *x_span,
	int16_t **cuts_out, uint32_t *count_out)
{
	event_cut_set_t set;
	uint32_t read, write;

	memset(&set, 0, sizeof(set));
	set.minimum = build->sources->origins.mins[axis];
	set.maximum = build->sources->origins.maxs[axis];
	AddControlEvents(build, &set, axis, pitch, yaw, hand, x_span);
	if (set.error)
	{
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
	if (!set.values)
	{
		set.values = calloc(1U, sizeof(*set.values));
		if (!set.values)
		{
			SetError(build,
				SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_OUT_OF_MEMORY, 0U);
			return 0;
		}
	}
	*cuts_out = set.values;
	*count_out = write;
	return 1;
}
