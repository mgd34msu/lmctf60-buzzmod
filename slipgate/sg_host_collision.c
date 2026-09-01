#include "sg_host_collision.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct sg_host_trace_context_s
{
	const sg_bsp_world_t *world;
	float start[3];
	float end[3];
	float offsets[8][3];
	float extents[3];
	int is_point;
	sg_host_collision_contents_t mask;
	sg_host_collision_trace_t *trace;
} sg_host_trace_context_t;

static int FiniteVector(const float value[3])
{
	return value && isfinite(value[0]) && isfinite(value[1]) &&
		isfinite(value[2]);
}

static float Dot(const float left[3], const float right[3])
{
	if (!left || !right)
		return 0.0f;
	return left[0] * right[0] + left[1] * right[1] +
		left[2] * right[2];
}

static void CopyVector(float destination[3], const float source[3])
{
	if (!destination || !source)
		return;
	destination[0] = source[0];
	destination[1] = source[1];
	destination[2] = source[2];
}

static void LerpVector(const float start[3], const float end[3], float fraction,
	float result[3])
{
	uint32_t axis;

	for (axis = 0; axis < 3; axis++)
		result[axis] = start[axis] + fraction * (end[axis] - start[axis]);
}

static int SameVector(const float left[3], const float right[3])
{
	return left[0] == right[0] && left[1] == right[1] &&
		left[2] == right[2];
}

static float ClampFraction(float value)
{
	if (value < 0.0f)
		return 0.0f;
	if (value > 1.0f)
		return 1.0f;
	return value;
}

static int HullValid(const sg_rune_hull_profile_t *hull)
{
	uint32_t axis;

	if (!hull || !FiniteVector(hull->mins.value) ||
		!FiniteVector(hull->maxs.value))
		return 0;
	for (axis = 0; axis < 3; axis++)
		if (hull->mins.value[axis] >= hull->maxs.value[axis])
			return 0;
	return 1;
}

static int PhysicsValid(const sg_rune_physics_parameters_t *physics)
{
	return physics && isfinite(physics->gravity) &&
		isfinite(physics->ground_acceleration) &&
		isfinite(physics->air_acceleration) &&
		isfinite(physics->water_acceleration) &&
		isfinite(physics->hook_acceleration) &&
		isfinite(physics->external_acceleration) &&
		isfinite(physics->water_drag) && isfinite(physics->max_velocity) &&
		physics->gravity >= 0.0f && physics->ground_acceleration >= 0.0f &&
		physics->air_acceleration >= 0.0f &&
		physics->water_acceleration >= 0.0f &&
		physics->hook_acceleration >= 0.0f &&
		physics->external_acceleration >= 0.0f &&
		physics->water_drag >= 0.0f && physics->max_velocity > 0.0f &&
		physics->frame_ms > 0 && physics->substep_ms > 0 &&
		physics->substep_ms <= physics->frame_ms;
}

static int IdentityValid(const sg_rune_model_identity_t *identity)
{
	return identity && identity->bsp_content_id != 0 &&
		identity->physics_abi_id != 0 &&
		HullValid(&identity->standing_hull) &&
		HullValid(&identity->crouching_hull) && PhysicsValid(&identity->physics);
}

static int WorldValid(const sg_bsp_world_t *world)
{
	return world && world->planes && world->plane_count && world->nodes &&
		world->node_count && world->leaves && world->leaf_count &&
		world->models && world->model_count &&
		(!world->brush_count || world->brushes) &&
		(!world->brush_side_count || world->brush_sides) &&
		(!world->leaf_brush_count || world->leaf_brushes);
}

static int TransformValid(const sg_host_collision_transform_t *transform)
{
	return !transform || (FiniteVector(transform->origin) &&
		FiniteVector(transform->angles));
}

static int ZeroTransform(const sg_host_collision_transform_t *transform)
{
	return !transform ||
		(transform->origin[0] == 0.0f && transform->origin[1] == 0.0f &&
		 transform->origin[2] == 0.0f && transform->angles[0] == 0.0f &&
		 transform->angles[1] == 0.0f && transform->angles[2] == 0.0f);
}

static void AngleAxis(const float angles[3], float axis[3][3])
{
	const double degrees_to_radians = 0.01745329251994329576923690768489;
	float yaw = (float)((double)angles[1] * degrees_to_radians);
	float pitch = (float)((double)angles[0] * degrees_to_radians);
	float roll = (float)((double)angles[2] * degrees_to_radians);
	float sy = (float)sin((double)yaw);
	float cy = (float)cos((double)yaw);
	float sp = (float)sin((double)pitch);
	float cp = (float)cos((double)pitch);
	float sr = (float)sin((double)roll);
	float cr = (float)cos((double)roll);

	axis[0][0] = cp * cy;
	axis[0][1] = cp * sy;
	axis[0][2] = -sp;
	axis[1][0] = sr * sp * cy - cr * sy;
	axis[1][1] = sr * sp * sy + cr * cy;
	axis[1][2] = sr * cp;
	axis[2][0] = cr * sp * cy + sr * sy;
	axis[2][1] = cr * sp * sy - sr * cy;
	axis[2][2] = cr * cp;
}

static void RotateVector(const float value[3], const float axis[3][3],
	float result[3])
{
	float source[3] = { 0.0f, 0.0f, 0.0f };

	CopyVector(source, value);
	result[0] = Dot(source, axis[0]);
	result[1] = Dot(source, axis[1]);
	result[2] = Dot(source, axis[2]);
}

/* SV_Push uses q_shared AngleVectors(-amove), then forms (forward dot,
 * negative right dot, up dot).  Do not route this through AngleAxis: its
 * algebraically equivalent -right row changes binary32 rounding for combined
 * pitch/yaw/roll inputs. */
static void SVPushRotateVector(const float angles[3], const float value[3],
	float result[3])
{
	const double degrees_to_radians = 0.01745329251994329576923690768489;
	float angle;
	float sr;
	float sp;
	float sy;
	float cr;
	float cp;
	float cy;
	float forward[3];
	float right[3];
	float up[3];

	angle = (float)((double)angles[1] * degrees_to_radians);
	sy = (float)sin((double)angle);
	cy = (float)cos((double)angle);
	angle = (float)((double)angles[0] * degrees_to_radians);
	sp = (float)sin((double)angle);
	cp = (float)cos((double)angle);
	angle = (float)((double)angles[2] * degrees_to_radians);
	sr = (float)sin((double)angle);
	cr = (float)cos((double)angle);
	forward[0] = cp * cy;
	forward[1] = cp * sy;
	forward[2] = -sp;
	right[0] = -1.0f * sr * sp * cy + -1.0f * cr * -sy;
	right[1] = -1.0f * sr * sp * sy + -1.0f * cr * cy;
	right[2] = -1.0f * sr * cp;
	up[0] = cr * sp * cy + -sr * -sy;
	up[1] = cr * sp * sy + -sr * cy;
	up[2] = cr * cp;
	result[0] = value[0] * forward[0] + value[1] * forward[1] +
		value[2] * forward[2];
	result[1] = -(value[0] * right[0] + value[1] * right[1] +
		value[2] * right[2]);
	result[2] = value[0] * up[0] + value[1] * up[1] + value[2] * up[2];
}

static int TransformIsRotated(const sg_host_collision_transform_t *transform)
{
	return transform && (transform->angles[0] != 0.0f ||
		transform->angles[1] != 0.0f || transform->angles[2] != 0.0f);
}

static void ToModelPoint(const float point[3],
	const sg_host_collision_transform_t *transform, float result[3])
{
	float axis[3][3];
	uint32_t coordinate;

	if (!transform)
	{
		CopyVector(result, point);
		return;
	}
	for (coordinate = 0; coordinate < 3; coordinate++)
		result[coordinate] = point[coordinate] - transform->origin[coordinate];
	if (TransformIsRotated(transform))
	{
		AngleAxis(transform->angles, axis);
		RotateVector(result, (const float (*)[3])axis, result);
	}
}

static void ToWorldVector(const float value[3],
	const sg_host_collision_transform_t *transform, float result[3])
{
	float axis[3][3];
	float transposed[3][3];
	uint32_t row, column;

	if (!transform || !TransformIsRotated(transform))
	{
		CopyVector(result, value);
		return;
	}
	AngleAxis(transform->angles, axis);
	for (row = 0; row < 3; row++)
		for (column = 0; column < 3; column++)
			transposed[row][column] = axis[column][row];
	RotateVector(value, (const float (*)[3])transposed, result);
}

static void ToWorldPoint(const float local[3],
	const sg_host_collision_transform_t *transform, float result[3])
{
	uint32_t coordinate;

	ToWorldVector(local, transform, result);
	if (!transform)
		return;
	for (coordinate = 0; coordinate < 3; coordinate++)
		result[coordinate] += transform->origin[coordinate];
}

typedef struct sg_host_collision_polygon_point_s
{
	float value[3];
} sg_host_collision_polygon_point_t;

typedef struct sg_host_collision_polygon_context_s
{
	const sg_bsp_world_t *world;
	sg_host_collision_contents_t mask;
	const sg_host_collision_polygon_point_t *portal;
	uint32_t portal_count;
	sg_host_collision_polygon_point_t *scratch_a;
	sg_host_collision_polygon_point_t *scratch_b;
	uint32_t scratch_capacity;
	int overlap;
	int valid;
} sg_host_collision_polygon_context_t;

static int SizeMultiply(size_t left, size_t right, size_t *result_out)
{
	if (result_out == NULL || (right != 0U && left > SIZE_MAX / right))
		return 0;
	*result_out = left * right;
	return 1;
}

static int PolygonHasPositiveArea(
	const sg_host_collision_polygon_point_t *points, uint32_t count)
{
	uint32_t index;

	if (points == NULL || count < 3U)
		return 0;
	for (index = 1U; index + 1U < count; index++) {
		float first[3];
		float second[3];
		float cross[3];
		uint32_t axis;

		for (axis = 0U; axis < 3U; axis++) {
			first[axis] = points[index].value[axis] - points[0].value[axis];
			second[axis] = points[index + 1U].value[axis] -
				points[0].value[axis];
		}
		cross[0] = first[1] * second[2] - first[2] * second[1];
		cross[1] = first[2] * second[0] - first[0] * second[2];
		cross[2] = first[0] * second[1] - first[1] * second[0];
		if (isfinite(cross[0]) && isfinite(cross[1]) && isfinite(cross[2]) &&
			(cross[0] != 0.0f || cross[1] != 0.0f || cross[2] != 0.0f))
			return 1;
	}
	return 0;
}

static int ClipPolygonToBrush(sg_host_collision_polygon_context_t *context,
	const sg_bsp_brush_t *brush)
{
	const sg_host_collision_polygon_point_t *current = context->portal;
	sg_host_collision_polygon_point_t *other = context->scratch_a;
	uint32_t current_count = context->portal_count;
	uint32_t side_offset;

	for (side_offset = 0U; side_offset < brush->side_count; side_offset++) {
		const sg_bsp_brush_side_t *side = &context->world->brush_sides[
			brush->first_side + side_offset];
		const sg_bsp_plane_t *plane;
		float previous[3];
		float previous_distance;
		int previous_inside;
		uint32_t index;
		uint32_t output_count = 0U;

		if (side->plane >= context->world->plane_count) {
			context->valid = 0;
			return 0;
		}
		plane = &context->world->planes[side->plane];
		if (current_count == 0U)
			return 0;
		memcpy(previous, current[current_count - 1U].value, sizeof(previous));
		previous_distance = Dot(previous, plane->normal.value) - plane->distance;
		if (!isfinite(previous_distance)) {
			context->valid = 0;
			return 0;
		}
		previous_inside = previous_distance <= 0.0f;
		for (index = 0U; index < current_count; index++) {
			const float *point = current[index].value;
			const float distance = Dot(point, plane->normal.value) -
				plane->distance;
			const int inside = distance <= 0.0f;

			if (!isfinite(distance)) {
				context->valid = 0;
				return 0;
			}
			if (inside != previous_inside) {
				const float denominator = previous_distance - distance;
				const float fraction = previous_distance / denominator;
				uint32_t axis;

				if (!isfinite(denominator) || denominator == 0.0f ||
					!isfinite(fraction) || output_count >= context->scratch_capacity) {
					context->valid = 0;
					return 0;
				}
				for (axis = 0U; axis < 3U; axis++) {
					other[output_count].value[axis] = previous[axis] + fraction *
						(point[axis] - previous[axis]);
					if (!isfinite(other[output_count].value[axis])) {
						context->valid = 0;
						return 0;
					}
				}
				output_count++;
			}
			if (inside) {
				if (output_count >= context->scratch_capacity) {
					context->valid = 0;
					return 0;
				}
				other[output_count++] = current[index];
			}
			memcpy(previous, point, sizeof(previous));
			previous_distance = distance;
			previous_inside = inside;
		}
		current = other;
		other = other == context->scratch_a ? context->scratch_b :
			context->scratch_a;
		current_count = output_count;
	}
	if (!PolygonHasPositiveArea(current, current_count))
		return 0;
	for (side_offset = 0U; side_offset < brush->side_count; side_offset++) {
		const sg_bsp_brush_side_t *side = &context->world->brush_sides[
			brush->first_side + side_offset];
		const sg_bsp_plane_t *plane = &context->world->planes[side->plane];
		int has_strict_interior = 0;
		uint32_t index;

		for (index = 0U; index < current_count; index++) {
			const float distance = Dot(current[index].value,
				plane->normal.value) - plane->distance;

			if (!isfinite(distance)) {
				context->valid = 0;
				return 0;
			}
			if (distance < 0.0f) {
				has_strict_interior = 1;
				break;
			}
		}
		if (!has_strict_interior)
			return 0;
	}
	return 1;
}

static int PolygonWalkModelBrushes(sg_host_collision_polygon_context_t *context,
	int32_t child)
{
	const sg_bsp_world_t *world = context->world;

	if (context->overlap)
		return 1;
	if (child >= 0) {
		const sg_bsp_node_t *node;

		if ((uint32_t)child >= world->node_count) {
			context->valid = 0;
			return 0;
		}
		node = &world->nodes[(uint32_t)child];
		return PolygonWalkModelBrushes(context, node->children[0]) &&
			PolygonWalkModelBrushes(context, node->children[1]);
	}
	else {
		const uint32_t leaf_index = (uint32_t)(-1 - child);
		const sg_bsp_leaf_t *leaf;
		uint32_t offset;

		if (leaf_index >= world->leaf_count) {
			context->valid = 0;
			return 0;
		}
		leaf = &world->leaves[leaf_index];
		if (!((uint32_t)leaf->contents & context->mask))
			return 1;
		if (leaf->first_leaf_brush > world->leaf_brush_count ||
			leaf->leaf_brush_count > world->leaf_brush_count -
				leaf->first_leaf_brush) {
			context->valid = 0;
			return 0;
		}
		for (offset = 0U; offset < leaf->leaf_brush_count; offset++) {
			const uint32_t brush_index = world->leaf_brushes[
				leaf->first_leaf_brush + offset];
			const sg_bsp_brush_t *brush;

			if (brush_index >= world->brush_count) {
				context->valid = 0;
				return 0;
			}
			brush = &world->brushes[brush_index];
			if (!brush->side_count ||
				!((uint32_t)brush->contents & context->mask))
				continue;
			if (brush->first_side > world->brush_side_count ||
				brush->side_count > world->brush_side_count - brush->first_side) {
				context->valid = 0;
				return 0;
			}
			if (ClipPolygonToBrush(context, brush)) {
				context->overlap = 1;
				return 1;
			}
			if (!context->valid)
				return 0;
		}
	}
	return 1;
}

int SG_HostCollisionModelPositiveAreaPolygonOverlap(
	const sg_host_collision_authority_t *authority, uint32_t model_index,
	const sg_host_collision_transform_t *transform,
	const sg_rune_vec3_t *world_vertices, uint32_t world_vertex_count,
	sg_host_collision_contents_t mask, int *overlap_out)
{
	sg_host_collision_polygon_point_t *portal = NULL;
	sg_host_collision_polygon_point_t *scratch_a = NULL;
	sg_host_collision_polygon_point_t *scratch_b = NULL;
	sg_host_collision_polygon_context_t context;
	size_t point_bytes;
	uint32_t capacity;
	uint32_t index;
	int result = 0;

	if (overlap_out == NULL)
		return 0;
	*overlap_out = 0;
	if (authority == NULL || !WorldValid(authority->world) ||
		model_index >= authority->world->model_count || mask == 0U ||
		world_vertices == NULL || world_vertex_count < 3U ||
		!TransformValid(transform) ||
		(model_index == SG_HOST_COLLISION_MODEL_WORLD && !ZeroTransform(transform)) ||
		world_vertex_count > UINT32_MAX - authority->world->brush_side_count)
		return 0;
	capacity = world_vertex_count + authority->world->brush_side_count;
	if (!SizeMultiply((size_t)capacity, sizeof(*portal), &point_bytes))
		return 0;
	portal = malloc(point_bytes);
	scratch_a = malloc(point_bytes);
	scratch_b = malloc(point_bytes);
	if (portal == NULL || scratch_a == NULL || scratch_b == NULL)
		goto done;
	for (index = 0U; index < world_vertex_count; index++) {
		if (!FiniteVector(world_vertices[index].value))
			goto done;
		ToModelPoint(world_vertices[index].value, transform, portal[index].value);
		if (!FiniteVector(portal[index].value))
			goto done;
	}
	if (!PolygonHasPositiveArea(portal, world_vertex_count)) {
		result = 1;
		goto done;
	}
	memset(&context, 0, sizeof(context));
	context.world = authority->world;
	context.mask = mask;
	context.portal = portal;
	context.portal_count = world_vertex_count;
	context.scratch_a = scratch_a;
	context.scratch_b = scratch_b;
	context.scratch_capacity = capacity;
	context.valid = 1;
	if (!PolygonWalkModelBrushes(&context,
		authority->world->models[model_index].headnode) || !context.valid)
		goto done;
	*overlap_out = context.overlap;
	result = 1;
done:
	free(scratch_b);
	free(scratch_a);
	free(portal);
	return result;
}

static int32_t PointLeaf(const sg_bsp_world_t *world, int32_t child,
	const float point[3])
{
	while (child >= 0)
	{
		const sg_bsp_node_t *node = &world->nodes[(uint32_t)child];
		const sg_bsp_plane_t *plane = &world->planes[node->plane];
		float distance = Dot(point, plane->normal.value) - plane->distance;

		child = node->children[distance < 0.0f];
	}
	return -1 - child;
}

static uint32_t PlaneSignBits(const sg_bsp_plane_t *plane)
{
	uint32_t bits = 0;
	uint32_t axis;

	for (axis = 0; axis < 3; axis++)
		if (plane->normal.value[axis] < 0.0f)
			bits |= UINT32_C(1) << axis;
	return bits;
}

static void SetTracePlane(sg_host_collision_trace_t *trace,
	const sg_bsp_plane_t *plane)
{
	if (!trace || !plane)
		return;
	CopyVector(trace->plane.normal, plane->normal.value);
	trace->plane.distance = plane->distance;
	trace->plane.type = plane->type;
}

static void SetTraceSurface(sg_host_collision_trace_t *trace,
	const sg_bsp_world_t *world, const sg_bsp_brush_side_t *side)
{
	if (!trace || !world || !side)
		return;
	if (side->texinfo < 0)
	{
		trace->texinfo = SG_HOST_COLLISION_TEXINFO_NONE;
		trace->surface_flags = 0;
		return;
	}
	trace->texinfo = (uint32_t)side->texinfo;
	trace->surface_flags = world->texinfos[trace->texinfo].flags;
}

static void ClipBoxToBrush(sg_host_trace_context_t *context,
	const sg_bsp_brush_t *brush, uint32_t brush_index)
{
	float enter_fraction = -1.0f;
	float leave_fraction = 1.0f;
	const sg_bsp_plane_t *clip_plane = NULL;
	uint32_t lead_side_index = SG_HOST_COLLISION_BRUSH_NONE;
	int get_out = 0;
	int start_out = 0;
	uint32_t side_offset;

	if (context == NULL || context->world == NULL || brush == NULL ||
		brush->side_count == 0U ||
		brush->first_side > context->world->brush_side_count ||
		brush->side_count >
			context->world->brush_side_count - brush->first_side)
		return;
	for (side_offset = 0; side_offset < brush->side_count; side_offset++)
	{
		const uint32_t side_index = brush->first_side + side_offset;
		const sg_bsp_brush_side_t *side =
			&context->world->brush_sides[side_index];
		const sg_bsp_plane_t *plane = &context->world->planes[side->plane];
		float distance = plane->distance;
		float start_distance, end_distance;

		if (!context->is_point)
			distance -= Dot(context->offsets[PlaneSignBits(plane)],
				plane->normal.value);
		start_distance = Dot(context->start, plane->normal.value) - distance;
		end_distance = Dot(context->end, plane->normal.value) - distance;
		if (end_distance > 0.0f)
			get_out = 1;
		if (start_distance > 0.0f)
			start_out = 1;
		if (start_distance > 0.0f &&
			(end_distance >= SG_HOST_COLLISION_TRACE_EPSILON ||
			 end_distance >= start_distance))
			return;
		if (start_distance <= 0.0f && end_distance <= 0.0f)
			continue;
		if (start_distance > end_distance)
		{
			float fraction = (start_distance - SG_HOST_COLLISION_TRACE_EPSILON) /
				(start_distance - end_distance);

			if (fraction < 0.0f)
				fraction = 0.0f;
			if (fraction > enter_fraction)
			{
				enter_fraction = fraction;
				clip_plane = plane;
				lead_side_index = side_index;
			}
		}
		else
		{
			float fraction = (start_distance + SG_HOST_COLLISION_TRACE_EPSILON) /
				(start_distance - end_distance);

			if (fraction > 1.0f)
				fraction = 1.0f;
			if (fraction < leave_fraction)
				leave_fraction = fraction;
		}
	}
	if (!start_out)
	{
		context->trace->startsolid = 1;
		if (!get_out)
			context->trace->allsolid = 1;
		return;
	}
	if (enter_fraction < leave_fraction && enter_fraction > -1.0f &&
		enter_fraction < context->trace->fraction && clip_plane != NULL &&
		lead_side_index < context->world->brush_side_count)
	{
		context->trace->fraction = enter_fraction;
		SetTracePlane(context->trace, clip_plane);
		SetTraceSurface(context->trace, context->world,
			&context->world->brush_sides[lead_side_index]);
		context->trace->contents = (uint32_t)brush->contents;
		context->trace->brush = brush_index;
		context->trace->brush_side = lead_side_index;
	}
}

static void TestBoxInBrush(sg_host_trace_context_t *context,
	const sg_bsp_brush_t *brush)
{
	uint32_t side_offset;

	if (!brush->side_count)
		return;
	for (side_offset = 0; side_offset < brush->side_count; side_offset++)
	{
		const sg_bsp_brush_side_t *side =
			&context->world->brush_sides[brush->first_side + side_offset];
		const sg_bsp_plane_t *plane = &context->world->planes[side->plane];
		float distance = plane->distance -
			Dot(context->offsets[PlaneSignBits(plane)], plane->normal.value);

		if (Dot(context->start, plane->normal.value) - distance > 0.0f)
			return;
	}
	context->trace->startsolid = 1;
	context->trace->allsolid = 1;
	context->trace->fraction = 0.0f;
	context->trace->contents = (uint32_t)brush->contents;
}

static void TraceLeaf(sg_host_trace_context_t *context, uint32_t leaf_index,
	int stationary)
{
	const sg_bsp_leaf_t *leaf = &context->world->leaves[leaf_index];
	uint32_t offset;

	if (!((uint32_t)leaf->contents & context->mask))
		return;
	for (offset = 0; offset < leaf->leaf_brush_count; offset++)
	{
		uint32_t brush_index =
			context->world->leaf_brushes[leaf->first_leaf_brush + offset];
		const sg_bsp_brush_t *brush = &context->world->brushes[brush_index];

		if (!((uint32_t)brush->contents & context->mask))
			continue;
		if (stationary)
			TestBoxInBrush(context, brush);
		else
			ClipBoxToBrush(context, brush, brush_index);
		if (context->trace->fraction == 0.0f)
			return;
	}
}

static int BoxPlaneSide(const float mins[3], const float maxs[3],
	const sg_bsp_plane_t *plane)
{
	float near_corner[3], far_corner[3];
	uint32_t axis;
	int side = 0;

	for (axis = 0; axis < 3; axis++)
	{
		if (plane->normal.value[axis] < 0.0f)
		{
			near_corner[axis] = maxs[axis];
			far_corner[axis] = mins[axis];
		}
		else
		{
			near_corner[axis] = mins[axis];
			far_corner[axis] = maxs[axis];
		}
	}
	if (Dot(far_corner, plane->normal.value) - plane->distance >= 0.0f)
		side |= 1;
	if (Dot(near_corner, plane->normal.value) - plane->distance < 0.0f)
		side |= 2;
	return side;
}

static void TestBoxLeaves(sg_host_trace_context_t *context, int32_t child,
	const float mins[3], const float maxs[3])
{
	while (child >= 0)
	{
		const sg_bsp_node_t *node =
			&context->world->nodes[(uint32_t)child];
		int side = BoxPlaneSide(mins, maxs,
			&context->world->planes[node->plane]);

		if (side == 1)
		{
			child = node->children[0];
			continue;
		}
		if (side == 2)
		{
			child = node->children[1];
			continue;
		}
		TestBoxLeaves(context, node->children[0], mins, maxs);
		if (context->trace->allsolid)
			return;
		child = node->children[1];
	}
	TraceLeaf(context, (uint32_t)(-1 - child), 1);
}

static void RecursiveHullCheck(sg_host_trace_context_t *context, int32_t child,
	float start_fraction, float end_fraction, const float start[3],
	const float end[3])
{
	const sg_bsp_node_t *node;
	const sg_bsp_plane_t *plane;
	float start_distance, end_distance, offset;
	float fraction, fraction2, middle_fraction;
	float middle[3];
	int side;

	if (context->trace->fraction <= start_fraction)
		return;
	if (child < 0)
	{
		TraceLeaf(context, (uint32_t)(-1 - child), 0);
		return;
	}
	node = &context->world->nodes[(uint32_t)child];
	plane = &context->world->planes[node->plane];
	if (plane->type >= 0 && plane->type < 3)
	{
		start_distance = start[plane->type] - plane->distance;
		end_distance = end[plane->type] - plane->distance;
		offset = context->extents[plane->type];
	}
	else
	{
		start_distance = Dot(start, plane->normal.value) - plane->distance;
		end_distance = Dot(end, plane->normal.value) - plane->distance;
		offset = context->is_point ? 0.0f :
			fabsf(context->extents[0] * plane->normal.value[0]) +
			fabsf(context->extents[1] * plane->normal.value[1]) +
			fabsf(context->extents[2] * plane->normal.value[2]);
	}
	if (start_distance >= offset && end_distance >= offset)
	{
		RecursiveHullCheck(context, node->children[0], start_fraction,
			end_fraction, start, end);
		return;
	}
	if (start_distance < -offset && end_distance < -offset)
	{
		RecursiveHullCheck(context, node->children[1], start_fraction,
			end_fraction, start, end);
		return;
	}
	if (start_distance < end_distance)
	{
		float inverse = 1.0f / (start_distance - end_distance);
		side = 1;
		fraction2 = (start_distance + offset + SG_HOST_COLLISION_TRACE_EPSILON) * inverse;
		fraction = (start_distance - offset + SG_HOST_COLLISION_TRACE_EPSILON) * inverse;
	}
	else if (start_distance > end_distance)
	{
		float inverse = 1.0f / (start_distance - end_distance);
		side = 0;
		fraction2 = (start_distance - offset - SG_HOST_COLLISION_TRACE_EPSILON) * inverse;
		fraction = (start_distance + offset + SG_HOST_COLLISION_TRACE_EPSILON) * inverse;
	}
	else
	{
		side = 0;
		fraction = 1.0f;
		fraction2 = 0.0f;
	}
	fraction = ClampFraction(fraction);
	fraction2 = ClampFraction(fraction2);
	middle_fraction = start_fraction +
		(end_fraction - start_fraction) * fraction;
	LerpVector(start, end, fraction, middle);
	RecursiveHullCheck(context, node->children[side], start_fraction,
		middle_fraction, start, middle);
	middle_fraction = start_fraction +
		(end_fraction - start_fraction) * fraction2;
	LerpVector(start, end, fraction2, middle);
	RecursiveHullCheck(context, node->children[side ^ 1], middle_fraction,
		end_fraction, middle, end);
}

static void TraceLocal(const sg_bsp_world_t *world, int32_t headnode,
	const float start[3], const float mins[3], const float maxs[3],
	const float end[3], sg_host_collision_contents_t mask,
	sg_host_collision_trace_t *trace)
{
	sg_host_trace_context_t context;
	uint32_t corner, axis;

	memset(&context, 0, sizeof(context));
	context.world = world;
	context.mask = mask;
	context.trace = trace;
	CopyVector(context.start, start);
	CopyVector(context.end, end);
	memset(trace, 0, sizeof(*trace));
	trace->fraction = 1.0f;
	trace->texinfo = SG_HOST_COLLISION_TEXINFO_NONE;
	trace->brush = SG_HOST_COLLISION_BRUSH_NONE;
	trace->brush_side = SG_HOST_COLLISION_BRUSH_NONE;
	for (corner = 0; corner < 8; corner++)
		for (axis = 0; axis < 3; axis++)
			context.offsets[corner][axis] =
				((corner >> axis) & 1U) ? maxs[axis] : mins[axis];
	if (SameVector(start, end))
	{
		float search_mins[3], search_maxs[3];

		for (axis = 0; axis < 3; axis++)
		{
			search_mins[axis] = start[axis] + mins[axis] - 1.0f;
			search_maxs[axis] = start[axis] + maxs[axis] + 1.0f;
		}
		TestBoxLeaves(&context, headnode, search_mins, search_maxs);
		CopyVector(trace->end, start);
		return;
	}
	context.is_point = mins[0] == 0.0f && mins[1] == 0.0f &&
		mins[2] == 0.0f && maxs[0] == 0.0f && maxs[1] == 0.0f &&
		maxs[2] == 0.0f;
	if (!context.is_point)
		for (axis = 0; axis < 3; axis++)
			context.extents[axis] = fmaxf(-mins[axis], maxs[axis]);
	RecursiveHullCheck(&context, headnode, 0.0f, 1.0f, start, end);
	LerpVector(start, end, trace->fraction, trace->end);
}

static void TransformHitPlane(sg_host_collision_trace_t *trace,
	const sg_host_collision_transform_t *transform)
{
	if (!transform || trace->fraction == 1.0f)
		return;
	if (TransformIsRotated(transform))
		ToWorldVector(trace->plane.normal, transform, trace->plane.normal);
}

static int TraceArgumentsValid(const sg_host_collision_authority_t *authority,
	uint32_t model_index, const sg_host_collision_transform_t *transform,
	const float start[3], const float mins[3], const float maxs[3],
	const float end[3], const sg_host_collision_trace_t *trace_out)
{
	uint32_t axis;

	if (!authority || !WorldValid(authority->world) ||
		model_index >= authority->world->model_count ||
		!TransformValid(transform) || !FiniteVector(start) ||
		!FiniteVector(mins) || !FiniteVector(maxs) || !FiniteVector(end) ||
		!trace_out || (model_index == 0 && !ZeroTransform(transform)))
		return 0;
	for (axis = 0; axis < 3; axis++)
		if (mins[axis] > maxs[axis])
			return 0;
	return 1;
}

static int SceneValid(const sg_host_collision_authority_t *authority,
	const sg_host_collision_scene_t *scene)
{
	size_t first, second;

	if (!scene)
		return 1;
	if (scene->instance_count && !scene->instances)
		return 0;
	for (first = 0; first < scene->instance_count; first++)
	{
		const sg_host_collision_instance_t *instance = &scene->instances[first];

		if (!instance->instance_id || instance->model_index == 0 ||
			instance->model_index >= authority->world->model_count ||
			!TransformValid(&instance->transform))
			return 0;
		for (second = first + 1; second < scene->instance_count; second++)
			if (scene->instances[second].instance_id == instance->instance_id)
				return 0;
	}
	return 1;
}

static const sg_host_collision_instance_t *NextInstance(
	const sg_host_collision_scene_t *scene, uint64_t previous)
{
	const sg_host_collision_instance_t *next = NULL;
	size_t index;

	if (!scene)
		return NULL;
	for (index = 0; index < scene->instance_count; index++)
		if (scene->instances[index].instance_id > previous &&
			(!next || scene->instances[index].instance_id < next->instance_id))
			next = &scene->instances[index];
	return next;
}

static void MergeTrace(sg_host_collision_trace_t *destination,
	const sg_host_collision_trace_t *source, uint32_t model_index,
	uint64_t instance_id)
{
	destination->allsolid |= source->allsolid;
	destination->startsolid |= source->startsolid;
	if (source->fraction < destination->fraction)
	{
		int allsolid = destination->allsolid;
		int startsolid = destination->startsolid;

		*destination = *source;
		destination->allsolid = allsolid;
		destination->startsolid = startsolid;
		destination->model_index = model_index;
		destination->instance_id = instance_id;
	}
	else if (source->allsolid || source->startsolid)
	{
		destination->model_index = model_index;
		destination->instance_id = instance_id;
	}
}

int SG_HostCollisionInit(sg_host_collision_authority_t *authority,
	const sg_bsp_world_t *world, const sg_rune_model_identity_t *identity,
	sg_host_collision_error_t *error_out)
{
	sg_host_collision_error_t error = SG_HOST_COLLISION_ERROR_NONE;

	if (!authority)
		error = SG_HOST_COLLISION_ERROR_INVALID_ARGUMENT;
	else if (!WorldValid(world))
		error = SG_HOST_COLLISION_ERROR_INVALID_WORLD;
	else if (!IdentityValid(identity))
		error = SG_HOST_COLLISION_ERROR_INVALID_IDENTITY;
	if (error_out)
		*error_out = error;
	if (error != SG_HOST_COLLISION_ERROR_NONE)
		return 0;
	authority->world = world;
	authority->content_identity = world->content_identity;
	authority->identity = *identity;
	return 1;
}

int SG_HostCollisionModelToWorldPoint(
	const sg_host_collision_authority_t *authority, uint32_t model_index,
	const sg_host_collision_transform_t *transform, const float local[3],
	float world_out[3])
{
	uint32_t coordinate;

	if (!authority || !WorldValid(authority->world) ||
		model_index >= authority->world->model_count || !FiniteVector(local) ||
		!TransformValid(transform) || !world_out ||
		(model_index == SG_HOST_COLLISION_MODEL_WORLD &&
			!ZeroTransform(transform)))
		return 0;
	ToWorldPoint(local, transform, world_out);
	if (!FiniteVector(world_out))
		return 0;
	/* Q8 callers cannot name negative zero.  Preserve the transform arithmetic
	 * then canonicalize its representationally equivalent zero result. */
	for (coordinate = 0U; coordinate < 3U; coordinate++)
		if (world_out[coordinate] == 0.0f)
			world_out[coordinate] = 0.0f;
	return 1;
}

int SG_HostCollisionWorldTransform(
	const sg_host_collision_transform_t *transform,
	sg_host_collision_world_transform_t *world_transform_out)
{
	float axis[3][3];
	uint32_t local_axis;
	uint32_t world_axis;

	if (!transform || !world_transform_out || !TransformValid(transform))
		return 0;
	if (TransformIsRotated(transform))
		AngleAxis(transform->angles, axis);
	else {
		memset(axis, 0, sizeof(axis));
		axis[0][0] = 1.0f;
		axis[1][1] = 1.0f;
		axis[2][2] = 1.0f;
	}
	/* Keep the persisted witness fail-closed even if a platform math routine
	 * were ever to produce a non-finite basis value for finite input angles. */
	for (world_axis = 0U; world_axis < 3U; world_axis++)
		for (local_axis = 0U; local_axis < 3U; local_axis++)
			if (!isfinite(axis[local_axis][world_axis]))
				return 0;
	for (world_axis = 0U; world_axis < 3U; world_axis++) {
		world_transform_out->origin[world_axis] =
			transform->origin[world_axis];
		if (world_transform_out->origin[world_axis] == 0.0f)
			world_transform_out->origin[world_axis] = 0.0f;
		for (local_axis = 0U; local_axis < 3U; local_axis++) {
			world_transform_out->axis[local_axis][world_axis] =
				axis[local_axis][world_axis];
			if (world_transform_out->axis[local_axis][world_axis] == 0.0f)
				world_transform_out->axis[local_axis][world_axis] = 0.0f;
		}
	}
	return 1;
}

int SG_HostCollisionPusherCarry(
	const sg_host_collision_transform_t *pusher_transform,
	const float move[3], const float amove[3], const float rider_start[3],
	float rider_end_out[3])
{
	sg_host_collision_transform_t inverse_delta;
	float pusher_end[3];
	float translated_rider[3];
	float relative[3];
	float rotated[3];
	uint32_t axis;

	if (!TransformValid(pusher_transform) || !FiniteVector(move) ||
		!FiniteVector(amove) || !FiniteVector(rider_start) ||
		rider_end_out == NULL)
		return 0;
	memset(&inverse_delta, 0, sizeof(inverse_delta));
	for (axis = 0U; axis < 3U; axis++) {
		pusher_end[axis] = pusher_transform->origin[axis] + move[axis];
		translated_rider[axis] = rider_start[axis] + move[axis];
		inverse_delta.angles[axis] = -amove[axis];
		if (!isfinite(pusher_end[axis]) || !isfinite(translated_rider[axis]) ||
			!isfinite(inverse_delta.angles[axis]))
			return 0;
		relative[axis] = translated_rider[axis] - pusher_end[axis];
		if (!isfinite(relative[axis]))
			return 0;
	}
	/* g_phys.c computes org2 directly from AngleVectors(-amove).  That is a
	 * model-space rotation, not the transpose used for brush-local to world
	 * conversion.  Keep its dot-product order so this is one SV_Push step. */
	SVPushRotateVector(inverse_delta.angles, relative, rotated);
	for (axis = 0U; axis < 3U; axis++) {
		rider_end_out[axis] = translated_rider[axis] +
			(rotated[axis] - relative[axis]);
		if (!isfinite(rider_end_out[axis]))
			return 0;
		if (rider_end_out[axis] == 0.0f)
			rider_end_out[axis] = 0.0f;
	}
	return 1;
}

sg_host_collision_contents_t SG_HostCollisionPointContentsModel(
	const sg_host_collision_authority_t *authority, uint32_t model_index,
	const sg_host_collision_transform_t *transform, const float point[3])
{
	float local[3];
	int32_t leaf;

	if (!authority || !WorldValid(authority->world) ||
		model_index >= authority->world->model_count || !FiniteVector(point) ||
		!TransformValid(transform) || (model_index == 0 && !ZeroTransform(transform)))
		return 0;
	ToModelPoint(point, transform, local);
	leaf = PointLeaf(authority->world,
		authority->world->models[model_index].headnode, local);
	return (uint32_t)authority->world->leaves[(uint32_t)leaf].contents;
}

sg_host_collision_contents_t SG_HostCollisionPointContents(
	const sg_host_collision_authority_t *authority,
	const sg_host_collision_scene_t *scene, const float point[3])
{
	sg_host_collision_contents_t contents;
	const sg_host_collision_instance_t *instance;
	uint64_t previous = 0;

	if (!authority || !SceneValid(authority, scene))
		return 0;
	contents = SG_HostCollisionPointContentsModel(authority, 0, NULL, point);
	while ((instance = NextInstance(scene, previous)) != NULL)
	{
		contents |= SG_HostCollisionPointContentsModel(authority,
			instance->model_index, &instance->transform, point);
		previous = instance->instance_id;
	}
	return contents;
}

sg_rune_contents_mask_t SG_HostCollisionRuneContents(
	sg_host_collision_contents_t contents)
{
	sg_rune_contents_mask_t result = SG_RUNE_CONTENTS_EMPTY;

	if (contents & SG_HOST_CONTENTS_SOLID)
		result |= SG_RUNE_CONTENTS_SOLID;
	if (contents & SG_HOST_CONTENTS_WINDOW)
		result |= SG_RUNE_CONTENTS_WINDOW;
	if (contents & SG_HOST_CONTENTS_WATER)
		result |= SG_RUNE_CONTENTS_WATER;
	if (contents & SG_HOST_CONTENTS_LAVA)
		result |= SG_RUNE_CONTENTS_LAVA;
	if (contents & SG_HOST_CONTENTS_SLIME)
		result |= SG_RUNE_CONTENTS_SLIME;
	if (contents & SG_HOST_CONTENTS_PLAYER_CLIP)
		result |= SG_RUNE_CONTENTS_PLAYER_CLIP;
	if (contents & SG_HOST_CONTENTS_CURRENT_0)
		result |= SG_RUNE_CONTENTS_CURRENT_0;
	if (contents & SG_HOST_CONTENTS_CURRENT_90)
		result |= SG_RUNE_CONTENTS_CURRENT_90;
	if (contents & SG_HOST_CONTENTS_CURRENT_180)
		result |= SG_RUNE_CONTENTS_CURRENT_180;
	if (contents & SG_HOST_CONTENTS_CURRENT_270)
		result |= SG_RUNE_CONTENTS_CURRENT_270;
	if (contents & SG_HOST_CONTENTS_CURRENT_UP)
		result |= SG_RUNE_CONTENTS_CURRENT_UP;
	if (contents & SG_HOST_CONTENTS_CURRENT_DOWN)
		result |= SG_RUNE_CONTENTS_CURRENT_DOWN;
	return result;
}

int SG_HostCollisionTraceModel(
	const sg_host_collision_authority_t *authority, uint32_t model_index,
	const sg_host_collision_transform_t *transform, const float start[3],
	const float mins[3], const float maxs[3], const float end[3],
	sg_host_collision_contents_t mask, sg_host_collision_trace_t *trace_out)
{
	float local_start[3], local_end[3];

	if (!TraceArgumentsValid(authority, model_index, transform, start, mins,
		maxs, end, trace_out))
		return 0;
	ToModelPoint(start, transform, local_start);
	ToModelPoint(end, transform, local_end);
	TraceLocal(authority->world, authority->world->models[model_index].headnode,
		local_start, mins, maxs, local_end, mask, trace_out);
	TransformHitPlane(trace_out, transform);
	LerpVector(start, end, trace_out->fraction, trace_out->end);
	trace_out->model_index = model_index;
	return 1;
}

int SG_HostCollisionTrace(const sg_host_collision_authority_t *authority,
	const sg_host_collision_scene_t *scene, const float start[3],
	const float mins[3], const float maxs[3], const float end[3],
	sg_host_collision_contents_t mask, sg_host_collision_trace_t *trace_out)
{
	const sg_host_collision_instance_t *instance;
	sg_host_collision_trace_t model_trace;
	uint64_t previous = 0;

	if (!authority || !SceneValid(authority, scene) || !trace_out ||
		!SG_HostCollisionTraceModel(authority, 0, NULL, start, mins, maxs, end,
			mask, trace_out))
		return 0;
	if (trace_out->fraction == 0.0f)
		return 1;
	while ((instance = NextInstance(scene, previous)) != NULL)
	{
		if (!SG_HostCollisionTraceModel(authority, instance->model_index,
			&instance->transform, start, mins, maxs, end, mask, &model_trace))
			return 0;
		MergeTrace(trace_out, &model_trace, instance->model_index,
			instance->instance_id);
		if (trace_out->allsolid)
			break;
		previous = instance->instance_id;
	}
	LerpVector(start, end, trace_out->fraction, trace_out->end);
	return 1;
}

static const sg_rune_hull_profile_t *StanceHull(
	const sg_host_collision_authority_t *authority, sg_rune_stance_t stance)
{
	if (stance == SG_RUNE_STANCE_STANDING)
		return &authority->identity.standing_hull;
	if (stance == SG_RUNE_STANCE_CROUCHING)
		return &authority->identity.crouching_hull;
	return NULL;
}

int SG_HostCollisionClassifyPose(
	const sg_host_collision_authority_t *authority,
	const sg_host_collision_scene_t *scene, const float origin[3],
	sg_rune_stance_t stance, sg_host_collision_pose_t *pose_out)
{
	const sg_rune_hull_profile_t *hull;
	float support_end[3], sample[3];
	float view_height;
	int sample2, sample1;
	sg_host_collision_contents_t contents;

	if (!authority || !pose_out || !FiniteVector(origin) ||
		!SceneValid(authority, scene) || !(hull = StanceHull(authority, stance)))
		return 0;
	memset(pose_out, 0, sizeof(*pose_out));
	pose_out->stance = stance;
	pose_out->hull = *hull;
	pose_out->gravity = authority->identity.physics.gravity;
	pose_out->physics_abi_id = authority->identity.physics_abi_id;
	if (!SG_HostCollisionTrace(authority, scene, origin, hull->mins.value,
		hull->maxs.value, origin, SG_HOST_MASK_PLAYER_SOLID,
		&pose_out->occupancy))
		return 0;
	/* Pmove accepts a snapped position exactly when its position trace is not
	 * allsolid.  Stationary BSP brush tests set startsolid and allsolid together. */
	pose_out->valid = !pose_out->occupancy.allsolid;
	CopyVector(support_end, origin);
	support_end[2] -= SG_HOST_GROUND_PROBE;
	if (!SG_HostCollisionTrace(authority, scene, origin, hull->mins.value,
		hull->maxs.value, support_end, SG_HOST_MASK_PLAYER_SOLID,
		&pose_out->support))
		return 0;
	pose_out->supported = pose_out->support.startsolid ||
		(pose_out->support.fraction < 1.0f &&
		 pose_out->support.plane.normal[2] >= SG_HOST_GROUND_NORMAL_Z);
	pose_out->support_is_mover = pose_out->supported &&
		pose_out->support.instance_id != 0;
	view_height = stance == SG_RUNE_STANCE_STANDING ?
		SG_HOST_STANDING_VIEW_HEIGHT : SG_HOST_CROUCHING_VIEW_HEIGHT;
	{
		float sample_height = view_height - hull->mins.value[2];

		if (sample_height < (float)INT_MIN || sample_height >= (float)INT_MAX)
			return 0;
		sample2 = (int)sample_height;
	}
	sample1 = sample2 / 2;
	CopyVector(sample, origin);
	sample[2] = origin[2] + hull->mins.value[2] + 1.0f;
	contents = SG_HostCollisionPointContents(authority, scene, sample);
	if (contents & SG_HOST_MASK_WATER)
	{
		pose_out->water_type = contents;
		pose_out->water_level = 1;
		sample[2] = origin[2] + hull->mins.value[2] + (float)sample1;
		contents = SG_HostCollisionPointContents(authority, scene, sample);
		if (contents & SG_HOST_MASK_WATER)
		{
			pose_out->water_level = 2;
			sample[2] = origin[2] + hull->mins.value[2] + (float)sample2;
			contents = SG_HostCollisionPointContents(authority, scene, sample);
			if (contents & SG_HOST_MASK_WATER)
				pose_out->water_level = 3;
		}
	}
	return 1;
}

int SG_HostCollisionTransition(
	const sg_host_collision_authority_t *authority,
	const sg_host_collision_scene_t *scene, const float start[3],
	const float end[3], sg_rune_stance_t stance,
	sg_host_collision_transition_t *transition_out)
{
	const sg_rune_hull_profile_t *hull;
	sg_host_collision_trace_t source, destination;

	if (!authority || !transition_out || !FiniteVector(start) ||
		!FiniteVector(end) || !SceneValid(authority, scene) ||
		!(hull = StanceHull(authority, stance)))
		return 0;
	memset(transition_out, 0, sizeof(*transition_out));
	if (!SG_HostCollisionTrace(authority, scene, start, hull->mins.value,
		hull->maxs.value, start, SG_HOST_MASK_PLAYER_SOLID, &source) ||
		!SG_HostCollisionTrace(authority, scene, end, hull->mins.value,
		hull->maxs.value, end, SG_HOST_MASK_PLAYER_SOLID, &destination) ||
		!SG_HostCollisionTrace(authority, scene, start, hull->mins.value,
		hull->maxs.value, end, SG_HOST_MASK_PLAYER_SOLID,
		&transition_out->sweep))
		return 0;
	transition_out->source_valid = !source.allsolid;
	transition_out->destination_valid = !destination.allsolid;
	transition_out->clear = transition_out->source_valid &&
		transition_out->destination_valid && !transition_out->sweep.startsolid &&
		!transition_out->sweep.allsolid &&
		transition_out->sweep.fraction == 1.0f;
	return 1;
}

const char *SG_HostCollisionErrorString(sg_host_collision_error_t error)
{
	switch (error)
	{
	case SG_HOST_COLLISION_ERROR_NONE: return "none";
	case SG_HOST_COLLISION_ERROR_INVALID_ARGUMENT: return "invalid argument";
	case SG_HOST_COLLISION_ERROR_INVALID_WORLD: return "invalid BSP world";
	case SG_HOST_COLLISION_ERROR_INVALID_IDENTITY: return "invalid model identity";
	case SG_HOST_COLLISION_ERROR_INVALID_MODEL: return "invalid BSP model";
	case SG_HOST_COLLISION_ERROR_INVALID_SCENE: return "invalid collision scene";
	default: return "unknown collision error";
	}
}
