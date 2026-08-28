#include "sg_hook_visibility_feasibility_internal.h"

#include <math.h>

#define AUDIT_FAMILY_PI 3.14159265358979323846

static int AuditBasics(
	const sg_hook_visibility_feasibility_sources_t *sources)
{
	const sg_bsp_world_t *world = sources->collision->world;
	const sg_hook_visibility_fire_law_t *law = &sources->fire_law;
	uint32_t axis, first, second;

	for (axis = 0U; axis < 3U; axis++)
		if (sources->origins.mins[axis] > sources->origins.maxs[axis])
			return 0;
	if ((sources->stance != SG_RUNE_STANCE_STANDING &&
		sources->stance != SG_RUNE_STANCE_CROUCHING) || !law->identity ||
		law->angle_authority_id != SG_HOOK_VISIBILITY_ANGLE_AUTHORITY_ID ||
		law->standing_view_height != 22.0f ||
		law->crouching_view_height != -2.0f ||
		law->muzzle_forward != 8.0f || law->muzzle_lateral != 8.0f ||
		law->trace_epsilon != 1.0f / 32.0f ||
		law->shot_mask != SG_HOOK_VISIBILITY_MASK_SHOT ||
		!isfinite(law->maximum_range) || law->maximum_range <= 0.0f ||
		law->maximum_range * 8.0f > (float)INT32_MAX ||
		law->maximum_range * 8.0f != truncf(law->maximum_range * 8.0f))
		return 0;
	for (first = 0U; first < sources->surface_rule_count; first++)
	{
		const sg_hook_visibility_surface_rule_t *rule =
			&sources->surface_rules[first];

		if (!rule->surface_id || rule->model_index != 0U ||
			rule->brush_index >= world->brush_count ||
			rule->texinfo >= world->texinfo_count ||
			rule->classification > SG_HOOK_VISIBILITY_SURFACE_SKY)
			return 0;
		for (second = first + 1U; second < sources->surface_rule_count;
			second++)
			if (sources->surface_rules[second].surface_id == rule->surface_id ||
				sources->surface_rules[second].brush_index ==
					rule->brush_index ||
				sources->surface_rules[second].texinfo == rule->texinfo)
				return 0;
	}
	return 1;
}

static float AuditCosine(int16_t code)
{
	float degrees = (float)((double)(uint16_t)code * (360.0 / 65536.0));
	float radians = (float)((double)degrees *
		(AUDIT_FAMILY_PI * 2.0 / 360.0));

	return (float)cos((double)radians);
}

static int AuditControlSign(
	const sg_hook_visibility_feasibility_sources_t *sources, int *sign_out)
{
	uint32_t root_index;
	int sign = 0;

	for (root_index = 0U; root_index < sources->control_count; root_index++)
	{
		const sg_hook_visibility_control_root_t *root =
			&sources->controls[root_index];
		int pitch_varies, supported;
		int32_t minimum, maximum, code;

		if (root->pitch_min > root->pitch_max ||
			root->yaw_min > root->yaw_max ||
			(root->pitch_min != root->pitch_max &&
			 root->yaw_min != root->yaw_max))
			return 0;
		supported = root->pitch_min >= -1 && root->pitch_max <= 1 &&
			((root->yaw_min >= -1 && root->yaw_max <= 1) ||
			 (root->yaw_min >= 32766));
		if (!supported)
			return 0;
		pitch_varies = root->pitch_min != root->pitch_max;
		minimum = pitch_varies ? root->pitch_min : root->yaw_min;
		maximum = pitch_varies ? root->pitch_max : root->yaw_max;
		for (code = minimum; code <= maximum; code++)
		{
			int16_t pitch = pitch_varies ? (int16_t)code : root->pitch_min;
			int16_t yaw = pitch_varies ? root->yaw_min : (int16_t)code;
			float x = AuditCosine(pitch) * AuditCosine(yaw);
			int current;

			if (x == 0.0f)
				return 0;
			current = x > 0.0f ? 1 : -1;
			if (sign && sign != current)
				return 0;
			sign = current;
		}
	}
	*sign_out = sign;
	return sign != 0;
}

static int AuditLeaves(const sg_bsp_world_t *world)
{
	const sg_bsp_leaf_t *first;
	uint32_t offset;

	if (world->model_count != 1U || world->node_count != 1U ||
		world->leaf_count != 2U || world->models[0].headnode != 0 ||
		world->nodes[0].children[0] != -1 ||
		world->nodes[0].children[1] != -2)
		return 0;
	first = &world->leaves[0];
	if (first->leaf_brush_count != world->leaves[1].leaf_brush_count ||
		first->first_leaf_brush > world->leaf_brush_count ||
		first->leaf_brush_count >
			world->leaf_brush_count - first->first_leaf_brush ||
		world->leaves[1].first_leaf_brush > world->leaf_brush_count ||
		world->leaves[1].leaf_brush_count > world->leaf_brush_count -
			world->leaves[1].first_leaf_brush)
		return 0;
	for (offset = 0U; offset < first->leaf_brush_count; offset++)
		if (world->leaf_brushes[first->first_leaf_brush + offset] !=
			world->leaf_brushes[world->leaves[1].first_leaf_brush + offset] ||
			world->leaf_brushes[first->first_leaf_brush + offset] >=
				world->brush_count)
			return 0;
	return 1;
}

static int RuleIndex(const sg_hook_visibility_feasibility_sources_t *sources,
	uint32_t brush_index, uint32_t *rule_out)
{
	uint32_t rule, matches = 0U;

	for (rule = 0U; rule < sources->surface_rule_count; rule++)
		if (sources->surface_rules[rule].brush_index == brush_index)
		{
			*rule_out = rule;
			matches++;
		}
	return matches == 1U;
}

static int AuditBox(const sg_hook_visibility_feasibility_sources_t *sources,
	uint32_t brush_index, uint32_t rule_index, float minimum[3],
	float maximum[3])
{
	const sg_bsp_world_t *world = sources->collision->world;
	const sg_bsp_brush_t *brush = &world->brushes[brush_index];
	uint32_t positive[3] = {0U, 0U, 0U};
	uint32_t negative[3] = {0U, 0U, 0U};
	uint32_t axis, side_offset;

	if (brush->side_count != 6U || brush->first_side >
		world->brush_side_count || brush->side_count >
		world->brush_side_count - brush->first_side)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
	{
		minimum[axis] = -INFINITY;
		maximum[axis] = INFINITY;
	}
	for (side_offset = 0U; side_offset < brush->side_count; side_offset++)
	{
		const sg_bsp_brush_side_t *side =
			&world->brush_sides[brush->first_side + side_offset];
		const sg_bsp_plane_t *plane;
		uint32_t nonzero = 0U;

		if (side->plane >= world->plane_count || side->texinfo < 0 ||
			(uint32_t)side->texinfo !=
				sources->surface_rules[rule_index].texinfo)
			return 0;
		plane = &world->planes[side->plane];
		if (!isfinite(plane->distance) ||
			plane->distance * 8.0f != truncf(plane->distance * 8.0f))
			return 0;
		for (axis = 0U; axis < 3U; axis++)
			if (plane->normal.value[axis] != 0.0f)
			{
				if (plane->normal.value[axis] == 1.0f)
				{
					maximum[axis] = plane->distance;
					positive[axis]++;
				}
				else if (plane->normal.value[axis] == -1.0f)
				{
					minimum[axis] = -plane->distance;
					negative[axis]++;
				}
				else
					return 0;
				nonzero++;
			}
		if (nonzero != 1U)
			return 0;
	}
	for (axis = 0U; axis < 3U; axis++)
		if (positive[axis] != 1U || negative[axis] != 1U ||
			!isfinite(minimum[axis]) || !isfinite(maximum[axis]) ||
			minimum[axis] > maximum[axis])
			return 0;
	return 1;
}

int SG_HookVisibilityFeasibilityAuditFamilyValid(
	const sg_hook_visibility_feasibility_sources_t *sources)
{
	const sg_bsp_world_t *world = sources->collision->world;
	float source_min = (float)sources->origins.mins[0] * 0.125f;
	float source_max = (float)sources->origins.maxs[0] * 0.125f;
	float layer_min = 0.0f, layer_max = 0.0f;
	int sign, layer_seen = 0;
	uint32_t brush;

	if (!AuditBasics(sources) || sources->fire_law.moving_model_count ||
		(sources->scene && sources->scene->instance_count) ||
		!AuditControlSign(sources, &sign) || !AuditLeaves(world))
		return 0;
	for (brush = 0U; brush < world->brush_count; brush++)
	{
		float minimum[3], maximum[3];
		uint32_t rule;
		int target, guard;

		if (!((uint32_t)world->brushes[brush].contents &
			sources->fire_law.shot_mask))
			continue;
		if (!RuleIndex(sources, brush, &rule) ||
			!AuditBox(sources, brush, rule, minimum, maximum))
			return 0;
		target = sign > 0 ? minimum[0] > source_max :
			maximum[0] < source_min;
		guard = sign > 0 ? minimum[0] <= source_min &&
			maximum[0] < source_max : maximum[0] >= source_max &&
			minimum[0] > source_min;
		if (!target && !guard)
			return 0;
		if (!target)
			continue;
		if (!layer_seen)
		{
			layer_min = minimum[0];
			layer_max = maximum[0];
			layer_seen = 1;
		}
		else if (minimum[0] != layer_min || maximum[0] != layer_max)
			return 0;
	}
	return layer_seen;
}
