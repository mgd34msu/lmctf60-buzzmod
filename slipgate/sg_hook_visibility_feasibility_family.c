#include "sg_hook_visibility_feasibility_internal.h"

#include <math.h>

typedef sg_hook_visibility_build_context_t hook_build_t;

static int BrushBounds(const hook_build_t *build, uint32_t brush_index,
	int32_t expected_texinfo, float minimum[3], float maximum[3])
{
	const sg_bsp_world_t *world = build->sources->collision->world;
	const sg_bsp_brush_t *brush = &world->brushes[brush_index];
	uint32_t axis, side_offset;
	uint32_t positive[3] = {0U, 0U, 0U};
	uint32_t negative[3] = {0U, 0U, 0U};

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

		if (side->plane >= world->plane_count ||
			side->texinfo != expected_texinfo)
			return 0;
		plane = &world->planes[side->plane];

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
		if (!isfinite(minimum[axis]) || !isfinite(maximum[axis]) ||
			minimum[axis] > maximum[axis] || positive[axis] != 1U ||
			negative[axis] != 1U)
			return 0;
	return 1;
}

static int RuleForBrush(const hook_build_t *build, uint32_t brush_index)
{
	uint32_t rule, matches = 0U;

	for (rule = 0U; rule < build->sources->surface_rule_count; rule++)
		matches += build->sources->surface_rules[rule].brush_index == brush_index;
	return matches == 1U;
}

static int32_t TexinfoForBrush(const hook_build_t *build,
	uint32_t brush_index)
{
	uint32_t rule;

	for (rule = 0U; rule < build->sources->surface_rule_count; rule++)
		if (build->sources->surface_rules[rule].brush_index == brush_index)
			return (int32_t)build->sources->surface_rules[rule].texinfo;
	return -1;
}

static int LeavesRepeatTheBrushProgram(const hook_build_t *build)
{
	const sg_bsp_world_t *world = build->sources->collision->world;
	const sg_bsp_leaf_t *first;
	uint32_t leaf, offset;

	if (world->model_count != 1U || world->node_count != 1U ||
		world->leaf_count != 2U || world->models[0].headnode != 0 ||
		world->nodes[0].children[0] != -1 ||
		world->nodes[0].children[1] != -2)
		return 0;
	first = &world->leaves[0];
	if (first->first_leaf_brush > world->leaf_brush_count ||
		first->leaf_brush_count >
			world->leaf_brush_count - first->first_leaf_brush)
		return 0;
	for (leaf = 1U; leaf < world->leaf_count; leaf++)
	{
		const sg_bsp_leaf_t *other = &world->leaves[leaf];

		if (other->leaf_brush_count != first->leaf_brush_count ||
			other->first_leaf_brush > world->leaf_brush_count ||
			other->leaf_brush_count >
				world->leaf_brush_count - other->first_leaf_brush)
			return 0;
		for (offset = 0U; offset < first->leaf_brush_count; offset++)
			if (world->leaf_brushes[first->first_leaf_brush + offset] !=
				world->leaf_brushes[other->first_leaf_brush + offset])
				return 0;
	}
	for (offset = 0U; offset < first->leaf_brush_count; offset++)
		if (world->leaf_brushes[first->first_leaf_brush + offset] >=
			world->brush_count)
			return 0;
	return 1;
}

static int DirectionSign(const hook_build_t *build, int *sign_out)
{
	uint32_t control;
	int sign = 0;

	for (control = 0U; control < build->sources->control_count; control++)
	{
		const sg_hook_visibility_control_root_t *root =
			&build->sources->controls[control];
		int32_t count, code;

		if (root->pitch_min != root->pitch_max &&
			root->yaw_min != root->yaw_max)
			return 0;
		count = root->pitch_min == root->pitch_max ?
			(int32_t)root->yaw_max - root->yaw_min + 1 :
			(int32_t)root->pitch_max - root->pitch_min + 1;
		for (code = 0; code < count; code++)
		{
			int16_t pitch = root->pitch_min == root->pitch_max ?
				root->pitch_min : (int16_t)(root->pitch_min + code);
			int16_t yaw = root->pitch_min == root->pitch_max ?
				(int16_t)(root->yaw_min + code) : root->yaw_min;
			float forward[3], right[3];
			int current;

			SG_HookVisibilityFeasibilityDirection(pitch, yaw, forward, right);
			if (forward[0] == 0.0f)
				return 0;
			current = forward[0] > 0.0f ? 1 : -1;
			if (sign && sign != current)
				return 0;
			sign = current;
		}
	}
	*sign_out = sign;
	return sign != 0;
}

static int LayeredBrushes(const hook_build_t *build, int direction_sign)
{
	const sg_bsp_world_t *world = build->sources->collision->world;
	float source_min = (float)build->sources->origins.mins[0] * 0.125f;
	float source_max = (float)build->sources->origins.maxs[0] * 0.125f;
	float target_min = 0.0f, target_max = 0.0f;
	int target_seen = 0;
	uint32_t brush;

	for (brush = 0U; brush < world->brush_count; brush++)
	{
		float minimum[3], maximum[3];
		int target, guard;

		if (!((uint32_t)world->brushes[brush].contents &
			build->sources->fire_law.shot_mask))
			continue;
		if (!RuleForBrush(build, brush) ||
			!BrushBounds(build, brush, TexinfoForBrush(build, brush), minimum,
				maximum))
			return 0;
		target = direction_sign > 0 ? minimum[0] > source_max :
			maximum[0] < source_min;
		guard = direction_sign > 0 ?
			minimum[0] <= source_min && maximum[0] < source_max :
			maximum[0] >= source_max && minimum[0] > source_min;
		if (!target && !guard)
			return 0;
		if (!target)
			continue;
		if (!target_seen)
		{
			target_min = minimum[0];
			target_max = maximum[0];
			target_seen = 1;
		}
		else if (minimum[0] != target_min || maximum[0] != target_max)
			return 0;
	}
	return target_seen;
}

int SG_HookVisibilityFeasibilityFamilyValid(
	sg_hook_visibility_build_context_t *build)
{
	int direction_sign;

	if (!LeavesRepeatTheBrushProgram(build) ||
		!DirectionSign(build, &direction_sign) ||
		!LayeredBrushes(build, direction_sign))
	{
		SG_HookVisibilityFeasibilitySetError(build,
			SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_UNSUPPORTED, 7U);
		return 0;
	}
	return 1;
}
