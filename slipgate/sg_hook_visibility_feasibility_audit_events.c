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

static float AuditProjection(uint32_t axis, float boundary, float origin_x,
	float face_x, const float start_offset[3], const float delta[3], float limit)
{
	float parameter;

	if (delta[0] == 0.0f)
		return NAN;
	parameter = (face_x - origin_x - start_offset[0]) / delta[0];
	if (parameter < 0.0f || parameter > limit)
		return NAN;
	return (boundary - start_offset[axis] - delta[axis] * parameter) * 8.0f;
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
	const sg_hook_visibility_feasibility_sources_t *sources,
	const sg_hook_visibility_domain_term_t *domain, uint32_t target_axis,
	float boundary, float face, const float start_offset[3],
	const float delta[3], float limit)
{
	float first = AuditProjection(target_axis, boundary,
		(float)domain->origins.mins[0] * 0.125f, face, start_offset, delta,
		limit);
	float last = AuditProjection(target_axis, boundary,
		(float)domain->origins.maxs[0] * 0.125f, face, start_offset, delta,
		limit);
	int32_t first_signature[3], last_signature[3];
	int first_finite = ProjectionSignature(first, first_signature);
	int last_finite = ProjectionSignature(last, last_signature);
	float low, high;
	int32_t event_min, event_max;

	if (first_finite != last_finite)
		return 0;
	if (!first_finite)
		return 1;
	low = first < last ? first : last;
	high = first > last ? first : last;
	if (high < (float)sources->origins.mins[target_axis] - 1.0f ||
		low > (float)sources->origins.maxs[target_axis] + 1.0f)
		return 1;
	if (memcmp(first_signature, last_signature, sizeof(first_signature)) != 0)
		return 0;
	event_min = (int32_t)floorf(low);
	event_max = (int32_t)ceilf(high);
	if (event_max < domain->origins.mins[target_axis] ||
		event_min > domain->origins.maxs[target_axis])
		return 1;
	return domain->origins.mins[target_axis] ==
		domain->origins.maxs[target_axis] &&
		domain->origins.mins[target_axis] >= event_min &&
		domain->origins.mins[target_axis] <= event_max;
}

static int RuleUniform(
	const sg_hook_visibility_feasibility_sources_t *sources,
	const sg_hook_visibility_domain_term_t *domain, uint32_t rule)
{
	float minimum[3], maximum[3], forward[3], offset[3];
	float boundary[6];
	uint32_t axis, event, clearance;

	if (!AuditBrushBounds(sources, rule, minimum, maximum))
		return 0;
	AuditMuzzle(sources, domain, forward, offset);
	for (event = 0U; event < 6U; event++)
	{
		float coordinate = event == 0U ? minimum[0] :
			(event == 1U ? maximum[0] :
			 (event == 2U ? minimum[0] - sources->fire_law.trace_epsilon :
			  (event == 3U ? minimum[0] + sources->fire_law.trace_epsilon :
			   (event == 4U ? maximum[0] - sources->fire_law.trace_epsilon :
				maximum[0] + sources->fire_law.trace_epsilon))));

		if (!FloatCutFits(domain, 0U, coordinate * 8.0f) ||
			!FloatCutFits(domain, 0U, (coordinate - offset[0]) * 8.0f) ||
			!FloatCutFits(domain, 0U, (coordinate - offset[0] -
				forward[0] * sources->fire_law.maximum_range) * 8.0f))
			return 0;
	}
	for (axis = 1U; axis < 3U; axis++)
	{
		boundary[0] = minimum[axis];
		boundary[1] = maximum[axis];
		boundary[2] = minimum[axis] - sources->fire_law.trace_epsilon;
		boundary[3] = minimum[axis] + sources->fire_law.trace_epsilon;
		boundary[4] = maximum[axis] - sources->fire_law.trace_epsilon;
		boundary[5] = maximum[axis] + sources->fire_law.trace_epsilon;
		for (event = 0U; event < 6U; event++)
			if (!FloatCutFits(domain, axis, boundary[event] * 8.0f))
				return 0;
		for (clearance = 0U; clearance < 2U; clearance++)
		{
			float delta[3], start[3], limit;
			float face;

			if (clearance)
			{
				memcpy(delta, offset, sizeof(delta));
				memset(start, 0, sizeof(start));
				limit = 1.0f;
			}
			else
			{
				memcpy(delta, forward, sizeof(delta));
				memcpy(start, offset, sizeof(start));
				limit = sources->fire_law.maximum_range;
			}
			face = delta[0] > 0.0f ? minimum[0] : maximum[0];
			for (event = 0U; event < 6U; event++)
				if (!ProjectionUniform(sources, domain, axis, boundary[event],
						face, start, delta, limit))
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
