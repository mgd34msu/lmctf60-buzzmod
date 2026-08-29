#include "sg_hook_visibility_feasibility_internal.h"

#include <math.h>
#include <string.h>

#define AUDIT_EVENT_PI 3.14159265358979323846

static void AuditSinCos(int16_t code, float *sine, float *cosine)
{
	float degrees = (float)((double)(uint16_t)code * (360.0 / 65536.0));
	float radians = (float)((double)degrees *
		(AUDIT_EVENT_PI * 2.0 / 360.0));

	*sine = (float)sin((double)radians);
	*cosine = (float)cos((double)radians);
}

static void AuditDirection(int16_t pitch, int16_t yaw, float forward[3],
	float right[3])
{
	float sine_pitch, cosine_pitch, sine_yaw, cosine_yaw;
	float sine_roll, cosine_roll;

	AuditSinCos(pitch, &sine_pitch, &cosine_pitch);
	AuditSinCos(yaw, &sine_yaw, &cosine_yaw);
	AuditSinCos(0, &sine_roll, &cosine_roll);
	forward[0] = cosine_pitch * cosine_yaw;
	forward[1] = cosine_pitch * sine_yaw;
	forward[2] = -sine_pitch;
	right[0] = (-1.0f * sine_roll * sine_pitch * cosine_yaw +
		-1.0f * cosine_roll * -sine_yaw);
	right[1] = (-1.0f * sine_roll * sine_pitch * sine_yaw +
		-1.0f * cosine_roll * cosine_yaw);
	right[2] = -1.0f * sine_roll * cosine_pitch;
}

static int AuditBrushBounds(
	const sg_hook_visibility_feasibility_sources_t *sources, uint32_t rule,
	float minimum[3], float maximum[3])
{
	const sg_bsp_world_t *world = sources->collision->world;
	const sg_bsp_brush_t *brush = &world->brushes[
		sources->surface_rules[rule].brush_index];
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
	return isfinite(minimum[0]) && isfinite(maximum[0]) &&
		isfinite(minimum[1]) && isfinite(maximum[1]) &&
		isfinite(minimum[2]) && isfinite(maximum[2]);
}

static sg_hook_visibility_hand_t AuditHand(
	const sg_hook_visibility_domain_term_t *domain)
{
	uint32_t hand;

	for (hand = 0U; hand < SG_HOOK_VISIBILITY_HAND_COUNT; hand++)
		if (domain->hand_mask & SG_HOOK_VISIBILITY_HAND_BIT(hand))
			return (sg_hook_visibility_hand_t)hand;
	return SG_HOOK_VISIBILITY_HAND_COUNT;
}

static void AuditMuzzle(
	const sg_hook_visibility_feasibility_sources_t *sources,
	const sg_hook_visibility_domain_term_t *domain, float forward[3],
	float offset[3])
{
	float right[3];
	float lateral = sources->fire_law.muzzle_lateral;
	float view = sources->stance == SG_RUNE_STANCE_STANDING ?
		sources->fire_law.standing_view_height :
		sources->fire_law.crouching_view_height;
	uint32_t axis;

	if (AuditHand(domain) == SG_HOOK_VISIBILITY_HAND_LEFT)
		lateral = -lateral;
	else if (AuditHand(domain) == SG_HOOK_VISIBILITY_HAND_CENTER)
		lateral = 0.0f;
	AuditDirection(domain->pitch_min, domain->yaw_min, forward, right);
	for (axis = 0U; axis < 3U; axis++)
		offset[axis] = forward[axis] * sources->fire_law.muzzle_forward +
			right[axis] * lateral;
	offset[2] += view - sources->fire_law.muzzle_forward;
}

static int CutFits(const sg_hook_visibility_domain_term_t *domain,
	uint32_t axis, int32_t cut)
{
	if (cut < domain->origins.mins[axis] ||
		cut > domain->origins.maxs[axis])
		return 1;
	return domain->origins.mins[axis] == cut &&
		domain->origins.maxs[axis] == cut;
}

static int FloatCutFits(const sg_hook_visibility_domain_term_t *domain,
	uint32_t axis, float value)
{
	if (!isfinite(value) || value < (float)INT16_MIN - 1.0f ||
		value > (float)INT16_MAX + 1.0f)
		return 1;
	return CutFits(domain, axis, (int32_t)floorf(value)) &&
		CutFits(domain, axis, (int32_t)ceilf(value));
}

static void AuditBoundaryEvents(float minimum, float maximum, float epsilon,
	float events[6])
{
	events[0] = minimum;
	events[1] = maximum;
	events[2] = minimum - epsilon;
	events[3] = minimum + epsilon;
	events[4] = maximum - epsilon;
	events[5] = maximum + epsilon;
}

static void AuditSegment(
	const sg_hook_visibility_feasibility_sources_t *sources,
	const float forward[3], const float offset[3], int clearance,
	float start[3], float delta[3], float *limit)
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
		*limit = sources->fire_law.maximum_range;
	}
}

static float AuditProjection(uint32_t varying_axis, float varying_origin,
	float face, uint32_t projected_axis, float boundary,
	const float start_offset[3], const float delta[3], float minimum_parameter,
	float maximum_parameter)
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

static int ProjectionSignature(float value, int32_t signature[3])
{
	if (!isfinite(value))
	{
		signature[0] = INT32_MIN;
		signature[1] = INT32_MIN;
		signature[2] = 0;
		return 0;
	}
	if ((double)value < (double)INT32_MIN ||
		(double)value >= (double)INT32_MAX)
	{
		signature[0] = value < 0.0f ? INT32_MIN : INT32_MAX;
		signature[1] = signature[0];
		signature[2] = 0;
		return 1;
	}
	signature[0] = (int32_t)floorf(value);
	signature[1] = (int32_t)ceilf(value);
	signature[2] = value == truncf(value);
	return 1;
}

static int ProjectionUniform(
	const sg_hook_visibility_domain_term_t *domain, uint32_t varying_axis,
	float face, uint32_t projected_axis, float boundary,
	const float start_offset[3], const float delta[3], float minimum_parameter,
	float maximum_parameter)
{
	float first = AuditProjection(varying_axis,
		(float)domain->origins.mins[varying_axis] * 0.125f, face,
		projected_axis, boundary, start_offset, delta, minimum_parameter,
		maximum_parameter);
	float last = AuditProjection(varying_axis,
		(float)domain->origins.maxs[varying_axis] * 0.125f, face,
		projected_axis, boundary, start_offset, delta, minimum_parameter,
		maximum_parameter);
	int32_t first_signature[3], last_signature[3];
	int first_finite = ProjectionSignature(first, first_signature);
	int last_finite = ProjectionSignature(last, last_signature);
	float low, high;

	if (first_finite != last_finite)
		return 0;
	if (!first_finite)
		return 1;
	low = first < last ? first : last;
	high = first > last ? first : last;
	if (high < (float)domain->origins.mins[projected_axis] - 1.0f ||
		low > (float)domain->origins.maxs[projected_axis] + 1.0f)
		return 1;
	if (memcmp(first_signature, last_signature, sizeof(first_signature)) != 0)
		return 0;
	if (first_signature[2])
	{
		if (first_signature[0] < domain->origins.mins[projected_axis] ||
			first_signature[0] > domain->origins.maxs[projected_axis])
			return 1;
		return domain->origins.mins[projected_axis] ==
			domain->origins.maxs[projected_axis];
	}
	return domain->origins.maxs[projected_axis] <= first_signature[0] ||
		domain->origins.mins[projected_axis] >= first_signature[1];
}

static int RuleUniform(
	const sg_hook_visibility_feasibility_sources_t *sources,
	const sg_hook_visibility_domain_term_t *domain, uint32_t rule)
{
	float minimum[3], maximum[3], forward[3], offset[3];
	const uint32_t pairs[3][2] = {{0U, 1U}, {0U, 2U}, {1U, 2U}};
	uint32_t axis, event, clearance, pair;

	if (!AuditBrushBounds(sources, rule, minimum, maximum))
		return 0;
	AuditMuzzle(sources, domain, forward, offset);
	for (axis = 0U; axis < 3U; axis++)
	{
		float boundaries[6];

		AuditBoundaryEvents(minimum[axis], maximum[axis],
			sources->fire_law.trace_epsilon, boundaries);
		if (axis != 0U)
		{
			for (event = 0U; event < 6U; event++)
				if (!FloatCutFits(domain, axis, boundaries[event] * 8.0f))
					return 0;
			continue;
		}
		for (clearance = 0U; clearance < 2U; clearance++)
		{
			float start[3], delta[3], limit;

			AuditSegment(sources, forward, offset, (int)clearance, start,
				delta, &limit);
			for (event = 0U; event < 6U; event++)
				if (!FloatCutFits(domain, axis,
						(boundaries[event] - start[axis]) * 8.0f) ||
					!FloatCutFits(domain, axis,
						(boundaries[event] - start[axis] -
						 delta[axis] * limit) * 8.0f))
					return 0;
		}
	}
	for (pair = 0U; pair < 3U; pair++)
		{
			uint32_t first_axis = pairs[pair][0];
			uint32_t second_axis = pairs[pair][1];
			float first_boundaries[2], second_boundaries[6];
			float source_min = (float)sources->origins.mins[0] * 0.125f;
			float source_max = (float)sources->origins.maxs[0] * 0.125f;
			float clearance_first =
				(float)domain->origins.mins[0] * 0.125f;
			float clearance_last =
				(float)domain->origins.maxs[0] * 0.125f;
			float clearance_min = offset[0] < 0.0f ?
				clearance_first + offset[0] : clearance_first;
			float clearance_max = offset[0] > 0.0f ?
				clearance_last + offset[0] : clearance_last;
			int guard = forward[0] > 0.0f ? minimum[0] <= source_min &&
				maximum[0] < source_max : maximum[0] >= source_max &&
				minimum[0] > source_min;
			int guard_relevant = guard && clearance_max >= minimum[0] -
				sources->fire_law.trace_epsilon && clearance_min <= maximum[0] +
				sources->fire_law.trace_epsilon;
			uint32_t first_event, second_event, first_event_count = 2U;

			if (first_axis != 0U && !guard_relevant)
				continue;
			if (first_axis != 0U)
			{
				float first_shift = offset[first_axis] > 0.0f ?
					-sources->fire_law.trace_epsilon :
					sources->fire_law.trace_epsilon;
				float second_shift = offset[second_axis] > 0.0f ?
					-sources->fire_law.trace_epsilon :
					sources->fire_law.trace_epsilon;

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
				first_boundaries[0] = !guard ?
					(forward[first_axis] > 0.0f ? minimum[first_axis] :
					 maximum[first_axis]) : minimum[first_axis];
				first_boundaries[1] = maximum[first_axis];
				if (!guard)
					first_event_count = 1U;
				AuditBoundaryEvents(minimum[second_axis], maximum[second_axis],
					sources->fire_law.trace_epsilon, second_boundaries);
			}
			for (clearance = first_axis != 0U ? 1U : 0U;
				clearance < 2U; clearance++)
			{
				float start[3], delta[3], limit;
				float minimum_parameter = 0.0f;
				float maximum_parameter;

				AuditSegment(sources, forward, offset, (int)clearance, start,
					delta, &limit);
				maximum_parameter = limit;
				if (first_axis != 0U)
				{
					float x_shift = offset[0] > 0.0f ?
						-sources->fire_law.trace_epsilon :
						sources->fire_law.trace_epsilon;
					float brush_min = minimum[0] + x_shift;
					float brush_max = maximum[0] + x_shift;

					if (offset[0] > 0.0f)
					{
						minimum_parameter = (brush_min - clearance_last) /
							offset[0];
						maximum_parameter = (brush_max - clearance_first) /
							offset[0];
					}
					else
					{
						minimum_parameter = (brush_max - clearance_first) /
							offset[0];
						maximum_parameter = (brush_min - clearance_last) /
							offset[0];
					}
					if (minimum_parameter < 0.0f)
						minimum_parameter = 0.0f;
					if (maximum_parameter > 1.0f)
						maximum_parameter = 1.0f;
				}
				for (first_event = 0U; first_event < first_event_count;
					first_event++)
					for (second_event = first_axis != 0U ? first_event : 0U;
						second_event < (first_axis != 0U ? first_event + 1U :
						6U); second_event++)
						if (!ProjectionUniform(domain, first_axis,
								first_boundaries[first_event], second_axis,
								second_boundaries[second_event], start, delta,
								minimum_parameter, maximum_parameter))
							return 0;
			}
		}
	return 1;
}

int SG_HookVisibilityFeasibilityAuditDomainUniform(
	const sg_hook_visibility_feasibility_sources_t *sources,
	const sg_hook_visibility_domain_term_t *domain)
{
	uint32_t rule;

	if (domain->pitch_min != domain->pitch_max ||
		domain->yaw_min != domain->yaw_max)
		return 0;
	for (rule = 0U; rule < sources->surface_rule_count; rule++)
		if (!RuleUniform(sources, domain, rule))
			return 0;
	return 1;
}
