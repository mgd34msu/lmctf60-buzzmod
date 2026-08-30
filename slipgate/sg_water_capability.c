#include "sg_water_capability.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "sg_configuration_lattice.h"

#define SG_WATER_COMMAND_MAGNITUDE INT16_C(400)
#define SG_WATER_PLANE_DISTANCE_EPSILON 0.0001
#define SG_WATER_BOUNDS_EPSILON 0.0001f
#define SG_WATER_HOST_CURRENT_MASK \
	(SG_HOST_CONTENTS_CURRENT_0 | SG_HOST_CONTENTS_CURRENT_90 | \
	 SG_HOST_CONTENTS_CURRENT_180 | SG_HOST_CONTENTS_CURRENT_270 | \
	 SG_HOST_CONTENTS_CURRENT_UP | SG_HOST_CONTENTS_CURRENT_DOWN)

typedef struct sg_water_build_s
{
	const sg_host_collision_authority_t *authority;
	sg_host_pmove_function_t host_pmove;
	const sg_configuration_space_t *configuration;
	const sg_configuration_semantics_t *semantics;
	const sg_rune_phase_basis_t *phases;
	uint32_t phase_count;
	const sg_water_phase_binding_t *bindings;
	uint32_t binding_count;
	sg_water_capability_set_t *output;
	uint32_t fact_capacity;
	uint32_t *binding_offsets;
	uint32_t *cell_region_offsets;
	sg_water_capability_error_t error;
} sg_water_build_t;

typedef struct sg_water_face_ref_s
{
	uint32_t cell;
	uint32_t region;
	uint32_t face;
	uint32_t source_kind;
	uint32_t source_index;
	uint32_t source_variant;
	uint8_t sample_index;
	uint8_t reversed;
} sg_water_face_ref_t;

typedef struct sg_water_boundary_key_s
{
	uint32_t first_region;
	uint32_t second_region;
	uint32_t portal;
} sg_water_boundary_key_t;

typedef struct sg_water_sweep_event_s
{
	float coordinate;
	uint32_t reference;
	uint8_t starts;
} sg_water_sweep_event_t;

typedef struct sg_water_interval_node_s
{
	float low;
	float high;
	float subtree_high;
	uint32_t reference;
	uint32_t left;
	uint32_t right;
	uint32_t height;
} sg_water_interval_node_t;

static int PointInRegion(const sg_water_build_t *build, uint32_t region_index,
	const float point[3]);

static void SetError(sg_water_build_t *build,
	sg_water_capability_error_code_t code, uint32_t source_index)
{
	if (build->error.code == SG_WATER_CAPABILITY_ERROR_NONE)
	{
		build->error.code = code;
		build->error.source_index = source_index;
	}
}

static int Finite3(const float value[3])
{
	return value && isfinite(value[0]) && isfinite(value[1]) &&
		isfinite(value[2]);
}

static void Copy3(float destination[3], const float source[3])
{
	destination[0] = source[0];
	destination[1] = source[1];
	destination[2] = source[2];
}

static float Dot3(const float left[3], const float right[3])
{
	return left[0] * right[0] + left[1] * right[1] +
		left[2] * right[2];
}

static double PlaneSignedDistance(const float point[3], const float normal[3],
	float distance)
{
	double x = normal[0];
	double y = normal[1];
	double z = normal[2];
	double length = sqrt(x * x + y * y + z * z);

	return ((double)point[0] * x + (double)point[1] * y +
		(double)point[2] * z - (double)distance) / length;
}

static int AllocationFits(size_t count, size_t element_size)
{
	return element_size != 0U && count <= SIZE_MAX / element_size;
}

static void Cross3(const float left[3], const float right[3], float result[3])
{
	result[0] = left[1] * right[2] - left[2] * right[1];
	result[1] = left[2] * right[0] - left[0] * right[2];
	result[2] = left[0] * right[1] - left[1] * right[0];
}

static int HullEqual(const sg_rune_hull_profile_t *left,
	const sg_rune_hull_profile_t *right)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		if (left->mins.value[axis] != right->mins.value[axis] ||
			left->maxs.value[axis] != right->maxs.value[axis])
			return 0;
	return 1;
}

static int HullValid(const sg_rune_hull_profile_t *hull)
{
	uint32_t axis;

	if (!hull || !Finite3(hull->mins.value) || !Finite3(hull->maxs.value))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (hull->mins.value[axis] >= hull->maxs.value[axis])
			return 0;
	return 1;
}

static int PhysicsEqual(const sg_rune_physics_parameters_t *left,
	const sg_rune_physics_parameters_t *right)
{
	return left->gravity == right->gravity &&
		left->ground_acceleration == right->ground_acceleration &&
		left->air_acceleration == right->air_acceleration &&
		left->water_acceleration == right->water_acceleration &&
		left->hook_acceleration == right->hook_acceleration &&
		left->external_acceleration == right->external_acceleration &&
		left->water_drag == right->water_drag &&
		left->max_velocity == right->max_velocity &&
		left->frame_ms == right->frame_ms &&
		left->substep_ms == right->substep_ms;
}

static int IdentityEqual(const sg_rune_model_identity_t *left,
	const sg_rune_model_identity_t *right)
{
	return left && right && left->bsp_content_id == right->bsp_content_id &&
		left->entity_semantics_id == right->entity_semantics_id &&
		left->physics_abi_id == right->physics_abi_id &&
		left->source_set_identity == right->source_set_identity &&
		left->schema_id == right->schema_id &&
		left->producer_identity == right->producer_identity &&
		HullEqual(&left->standing_hull, &right->standing_hull) &&
		HullEqual(&left->crouching_hull, &right->crouching_hull) &&
		PhysicsEqual(&left->physics, &right->physics);
}

static int BoundsValid(const sg_rune_bounds_t *bounds)
{
	uint32_t axis;

	if (!bounds || !Finite3(bounds->mins.value) ||
		!Finite3(bounds->maxs.value))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (bounds->mins.value[axis] >= bounds->maxs.value[axis])
			return 0;
	return 1;
}

static int IdentityValid(const sg_rune_model_identity_t *identity)
{
	const sg_rune_physics_parameters_t *physics;

	if (!identity || identity->bsp_content_id == 0U ||
		identity->physics_abi_id == 0U || identity->source_set_identity == 0U ||
		identity->source_set_identity == UINT64_MAX ||
		!HullValid(&identity->standing_hull) ||
		!HullValid(&identity->crouching_hull))
		return 0;
	physics = &identity->physics;
	return isfinite(physics->gravity) && physics->gravity >= 0.0f &&
		isfinite(physics->ground_acceleration) &&
		physics->ground_acceleration >= 0.0f &&
		isfinite(physics->air_acceleration) &&
		physics->air_acceleration >= 0.0f &&
		isfinite(physics->water_acceleration) &&
		physics->water_acceleration >= 0.0f &&
		isfinite(physics->hook_acceleration) &&
		physics->hook_acceleration >= 0.0f &&
		isfinite(physics->external_acceleration) &&
		physics->external_acceleration >= 0.0f &&
		isfinite(physics->water_drag) && physics->water_drag >= 0.0f &&
		isfinite(physics->max_velocity) && physics->max_velocity > 0.0f &&
		physics->gravity <= (float)SHRT_MAX &&
		truncf(physics->gravity) == physics->gravity &&
		physics->frame_ms != 0U && physics->substep_ms != 0U &&
		physics->substep_ms <= UCHAR_MAX &&
		physics->substep_ms <= physics->frame_ms &&
		physics->frame_ms % physics->substep_ms == 0U;
}

static sg_rune_medium_t RegionMedium(
	const sg_configuration_semantic_region_t *region)
{
	if (region->flags & SG_CONFIGURATION_SEMANTIC_REGION_WATER)
		return SG_RUNE_MEDIUM_WATER;
	if (region->flags & SG_CONFIGURATION_SEMANTIC_REGION_LAVA)
		return SG_RUNE_MEDIUM_LAVA;
	if (region->flags & SG_CONFIGURATION_SEMANTIC_REGION_SLIME)
		return SG_RUNE_MEDIUM_SLIME;
	return SG_RUNE_MEDIUM_DRY;
}

static int RegionValid(const sg_water_build_t *build, uint32_t region_index)
{
	const sg_configuration_semantic_region_t *region =
		&build->semantics->regions[region_index];
	sg_rune_medium_t medium = RegionMedium(region);
	uint32_t medium_flags = region->flags &
		(SG_CONFIGURATION_SEMANTIC_REGION_WATER |
		 SG_CONFIGURATION_SEMANTIC_REGION_LAVA |
		 SG_CONFIGURATION_SEMANTIC_REGION_SLIME);
	uint32_t face;

	if (region->cell >= build->configuration->cell_count ||
		!BoundsValid(&region->bounds) ||
		!Finite3(region->interior_witness.value) || region->face_count < 4U ||
		region->first_face > build->semantics->face_count ||
		region->face_count > build->semantics->face_count - region->first_face ||
		(region_index != 0U &&
		 build->semantics->regions[region_index - 1U].cell > region->cell) ||
		(region_index != 0U &&
		 build->semantics->regions[region_index - 1U].id >= region->id))
		return 0;
	if ((medium == SG_RUNE_MEDIUM_DRY) != (region->water_level == 0U) ||
		region->water_level > 3U ||
		(medium_flags != 0U && (medium_flags & (medium_flags - 1U)) != 0U))
		return 0;
	if ((medium == SG_RUNE_MEDIUM_WATER) !=
		((region->water_type & SG_HOST_CONTENTS_WATER) != 0U) ||
		(medium == SG_RUNE_MEDIUM_LAVA) !=
		((region->water_type & SG_HOST_CONTENTS_LAVA) != 0U) ||
		(medium == SG_RUNE_MEDIUM_SLIME) !=
		((region->water_type & SG_HOST_CONTENTS_SLIME) != 0U))
		return 0;
	for (face = region->first_face;
		face < region->first_face + region->face_count; face++)
	{
		const sg_configuration_semantic_face_t *record =
			&build->semantics->faces[face];

		if (!Finite3(record->normal) || !isfinite(record->distance) ||
			(record->normal[0] == 0.0f && record->normal[1] == 0.0f &&
			 record->normal[2] == 0.0f) ||
			record->kind > SG_CONFIGURATION_SEMANTIC_FACE_CONSTRAINT_ONLY ||
			record->open > 1U ||
			(record->kind == SG_CONFIGURATION_SEMANTIC_FACE_FACET &&
				record->vertex_count < 3U) ||
			(record->kind == SG_CONFIGURATION_SEMANTIC_FACE_CONSTRAINT_ONLY &&
				record->vertex_count != 0U) ||
			record->first_vertex > build->semantics->vertex_count ||
			record->vertex_count > build->semantics->vertex_count -
				record->first_vertex)
			return 0;
	}
	return 1;
}

static int PhaseMatchesRegion(const sg_water_build_t *build,
	const sg_rune_phase_basis_t *phase,
	const sg_configuration_semantic_region_t *region)
{
	const sg_configuration_cell_t *cell =
		&build->configuration->cells[region->cell];
	sg_rune_medium_t medium = RegionMedium(region);
	sg_rune_motion_t motion;
	sg_rune_support_t support;
	sg_rune_void_relation_t void_relation;

	if (region->water_level >= 2U)
	{
		motion = SG_RUNE_MOTION_SWIMMING;
		support = SG_RUNE_SUPPORT_NONE;
	}
	else if (region->flags & SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED)
	{
		motion = SG_RUNE_MOTION_SUPPORTED;
		support = SG_RUNE_SUPPORT_SUPPORTED;
	}
	else
	{
		motion = SG_RUNE_MOTION_AIRBORNE;
		support = SG_RUNE_SUPPORT_NONE;
	}
	void_relation = (region->flags &
		SG_CONFIGURATION_SEMANTIC_REGION_VOID_ADJACENT) ?
		SG_RUNE_VOID_ADJACENT : SG_RUNE_VOID_CLEAR;
	return SG_RuneModelPhaseValid(phase) &&
		phase->order.source_set_identity ==
			build->authority->identity.source_set_identity &&
		phase->stance == cell->stance && phase->motion == motion &&
		phase->support == support && phase->medium == medium &&
		phase->void_relation == void_relation &&
		phase->reference_frame == SG_RUNE_FRAME_WORLD;
}

static int SourceValid(sg_water_build_t *build)
{
	uint32_t region;
	uint32_t portal;

	if (!build->authority || !build->authority->world || !build->host_pmove ||
		!build->configuration || !build->semantics || !build->phases ||
		build->phase_count == 0U || !build->bindings ||
		build->binding_count == 0U ||
		!IdentityValid(&build->authority->identity) ||
		!IdentityEqual(&build->authority->identity,
			&build->configuration->identity) ||
		!IdentityEqual(&build->authority->identity, &build->semantics->identity) ||
		build->configuration->cell_count == 0U ||
		!build->configuration->cells ||
		(build->configuration->portal_count != 0U &&
		 !build->configuration->portals) ||
		(build->configuration->vertex_count != 0U &&
		 !build->configuration->vertices) ||
		build->semantics->region_count == 0U || !build->semantics->regions ||
		!build->semantics->faces)
		return 0;
	for (region = 0U; region < build->semantics->region_count; region++)
		if (!RegionValid(build, region))
			return 0;
	for (portal = 0U; portal < build->configuration->portal_count; portal++)
	{
		const sg_configuration_portal_t *record =
			&build->configuration->portals[portal];

		if (record->from_cell >= build->configuration->cell_count ||
			record->to_cell >= build->configuration->cell_count ||
			record->from_cell == record->to_cell ||
			record->vertex_count < 3U ||
			record->first_vertex > build->configuration->vertex_count ||
			record->vertex_count >
				build->configuration->vertex_count - record->first_vertex ||
			!Finite3(record->plane.normal) ||
			!isfinite(record->plane.distance) ||
			(record->plane.normal[0] == 0.0f &&
			 record->plane.normal[1] == 0.0f &&
			 record->plane.normal[2] == 0.0f))
			return 0;
	}
	for (portal = 0U; portal < build->configuration->vertex_count; portal++)
		if (!Finite3(build->configuration->vertices[portal].value))
			return 0;
	return 1;
}

static int BuildBindingOffsets(sg_water_build_t *build)
{
	uint32_t binding = 0U;
	uint32_t region;

	if (build->semantics->region_count == UINT32_MAX)
	{
		SetError(build, SG_WATER_CAPABILITY_ERROR_OVERFLOW,
			build->semantics->region_count);
		return 0;
	}
	if (!AllocationFits((size_t)build->semantics->region_count + 1U,
		sizeof(*build->binding_offsets)))
	{
		SetError(build, SG_WATER_CAPABILITY_ERROR_OVERFLOW,
			build->semantics->region_count);
		return 0;
	}
	build->binding_offsets = calloc(
		(size_t)build->semantics->region_count + 1U,
		sizeof(*build->binding_offsets));
	if (!build->binding_offsets)
	{
		SetError(build, SG_WATER_CAPABILITY_ERROR_OUT_OF_MEMORY, 0U);
		return 0;
	}
	for (region = 0U; region < build->semantics->region_count; region++)
	{
		uint32_t first = binding;

		build->binding_offsets[region] = binding;
		while (binding < build->binding_count &&
			build->bindings[binding].semantic_region_id ==
				build->semantics->regions[region].id)
		{
			const sg_water_phase_binding_t *record = &build->bindings[binding];

			if (record->reserved != 0U || record->phase >= build->phase_count ||
				!PhaseMatchesRegion(build, &build->phases[record->phase],
					&build->semantics->regions[region]) ||
				(binding != first && build->bindings[binding - 1U].phase >=
					record->phase))
			{
				SetError(build, SG_WATER_CAPABILITY_ERROR_INVALID_PHASE, binding);
				return 0;
			}
			binding++;
		}
		if (binding == first)
		{
			SetError(build, SG_WATER_CAPABILITY_ERROR_INVALID_PHASE, region);
			return 0;
		}
	}
	build->binding_offsets[build->semantics->region_count] = binding;
	if (binding != build->binding_count)
	{
		SetError(build, SG_WATER_CAPABILITY_ERROR_INVALID_PHASE, binding);
		return 0;
	}
	return 1;
}

static int BuildCellRegionOffsets(sg_water_build_t *build)
{
	uint32_t cell;
	uint32_t region = 0U;

	if (build->configuration->cell_count == UINT32_MAX)
	{
		SetError(build, SG_WATER_CAPABILITY_ERROR_OVERFLOW,
			build->configuration->cell_count);
		return 0;
	}
	if (!AllocationFits((size_t)build->configuration->cell_count + 1U,
		sizeof(*build->cell_region_offsets)))
	{
		SetError(build, SG_WATER_CAPABILITY_ERROR_OVERFLOW,
			build->configuration->cell_count);
		return 0;
	}
	build->cell_region_offsets = calloc(
		(size_t)build->configuration->cell_count + 1U,
		sizeof(*build->cell_region_offsets));
	if (!build->cell_region_offsets)
	{
		SetError(build, SG_WATER_CAPABILITY_ERROR_OUT_OF_MEMORY, 0U);
		return 0;
	}
	for (cell = 0U; cell < build->configuration->cell_count; cell++)
	{
		build->cell_region_offsets[cell] = region;
		while (region < build->semantics->region_count &&
			build->semantics->regions[region].cell == cell)
			region++;
		if (build->cell_region_offsets[cell] == region)
		{
			SetError(build, SG_WATER_CAPABILITY_ERROR_INVALID_SOURCE, cell);
			return 0;
		}
	}
	build->cell_region_offsets[build->configuration->cell_count] = region;
	if (region != build->semantics->region_count)
	{
		SetError(build, SG_WATER_CAPABILITY_ERROR_INVALID_SOURCE, region);
		return 0;
	}
	return 1;
}

static int GrowFacts(sg_water_build_t *build)
{
	uint32_t required = build->output->fact_count + 1U;
	uint32_t capacity;
	sg_water_capability_fact_t *facts;

	if (build->output->fact_count == UINT32_MAX)
	{
		SetError(build, SG_WATER_CAPABILITY_ERROR_OVERFLOW,
			build->output->fact_count);
		return 0;
	}
	if (required <= build->fact_capacity)
		return 1;
	capacity = build->fact_capacity ? build->fact_capacity : 64U;
	while (capacity < required)
	{
		if (capacity > UINT32_MAX / 2U)
		{
			capacity = UINT32_MAX;
			break;
		}
		capacity *= 2U;
	}
	if (capacity < required)
	{
		SetError(build, SG_WATER_CAPABILITY_ERROR_OVERFLOW, required);
		return 0;
	}
	if (!AllocationFits((size_t)capacity, sizeof(*facts)))
	{
		SetError(build, SG_WATER_CAPABILITY_ERROR_OVERFLOW, capacity);
		return 0;
	}
	facts = realloc(build->output->facts, (size_t)capacity * sizeof(*facts));
	if (!facts)
	{
		SetError(build, SG_WATER_CAPABILITY_ERROR_OUT_OF_MEMORY, required);
		return 0;
	}
	build->output->facts = facts;
	build->fact_capacity = capacity;
	return 1;
}

static void DirectionVector(sg_water_direction_t direction, float result[3])
{
	memset(result, 0, 3U * sizeof(*result));
	if (direction == SG_WATER_DIRECTION_POSITIVE_X) result[0] = 1.0f;
	if (direction == SG_WATER_DIRECTION_NEGATIVE_X) result[0] = -1.0f;
	if (direction == SG_WATER_DIRECTION_POSITIVE_Y) result[1] = 1.0f;
	if (direction == SG_WATER_DIRECTION_NEGATIVE_Y) result[1] = -1.0f;
	if (direction == SG_WATER_DIRECTION_POSITIVE_Z) result[2] = 1.0f;
	if (direction == SG_WATER_DIRECTION_NEGATIVE_Z) result[2] = -1.0f;
}

static int PointToPmove(const float point[3], short output[3])
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
	{
		float scaled;

		if (!isfinite(point[axis]))
			return 0;
		scaled = point[axis] * 8.0f;
		if (scaled < (float)SHRT_MIN || scaled > (float)SHRT_MAX ||
			truncf(scaled) != scaled)
			return 0;
		output[axis] = (short)scaled;
	}
	return 1;
}

static int IntervalVelocityToPmove(const sg_rune_interval_t *interval,
	short *output)
{
	float lower;
	float upper;
	float selected;

	if (!interval || !output || !isfinite(interval->min_value) ||
		!isfinite(interval->max_value))
		return 0;
	lower = ceilf(interval->min_value * 8.0f);
	upper = floorf(interval->max_value * 8.0f);
	if (lower > upper || upper < (float)SHRT_MIN || lower > (float)SHRT_MAX)
		return 0;
	if (lower < (float)SHRT_MIN) lower = (float)SHRT_MIN;
	if (upper > (float)SHRT_MAX) upper = (float)SHRT_MAX;
	selected = lower > 0.0f ? lower : (upper < 0.0f ? upper : 0.0f);
	*output = (short)selected;
	return 1;
}

static int PhaseContainsVelocity(const sg_rune_phase_basis_t *phase,
	const float velocity[3])
{
	return velocity[0] >= phase->velocity.x.min_value &&
		velocity[0] <= phase->velocity.x.max_value &&
		velocity[1] >= phase->velocity.y.min_value &&
		velocity[1] <= phase->velocity.y.max_value &&
		velocity[2] >= phase->velocity.z.min_value &&
		velocity[2] <= phase->velocity.z.max_value;
}

static sg_rune_medium_t ResultMedium(const sg_host_pmove_result_t *result)
{
	if (result->water_level == 0)
		return SG_RUNE_MEDIUM_DRY;
	if (result->water_type & SG_HOST_CONTENTS_WATER)
		return SG_RUNE_MEDIUM_WATER;
	if (result->water_type & SG_HOST_CONTENTS_LAVA)
		return SG_RUNE_MEDIUM_LAVA;
	if (result->water_type & SG_HOST_CONTENTS_SLIME)
		return SG_RUNE_MEDIUM_SLIME;
	return SG_RUNE_MEDIUM_DRY;
}

static int ResultMatchesDestination(const sg_water_build_t *build,
	uint32_t region_index, uint32_t phase_index,
	const sg_host_pmove_result_t *result)
{
	const sg_configuration_semantic_region_t *region =
		&build->semantics->regions[region_index];
	const sg_rune_phase_basis_t *phase = &build->phases[phase_index];
	const sg_rune_hull_profile_t *hull;
	sg_rune_stance_t stance;
	sg_rune_motion_t motion;
	sg_rune_support_t support;
	int region_supported;
	uint32_t axis;

	if (!PointInRegion(build, region_index, result->origin))
		return 0;
	stance = (result->state.pm_flags & PMF_DUCKED) ?
		SG_RUNE_STANCE_CROUCHING : SG_RUNE_STANCE_STANDING;
	hull = stance == SG_RUNE_STANCE_CROUCHING ?
		&build->authority->identity.crouching_hull :
		&build->authority->identity.standing_hull;
	for (axis = 0U; axis < 3U; axis++)
		if (result->mins[axis] != hull->mins.value[axis] ||
			result->maxs[axis] != hull->maxs.value[axis])
			return 0;
	region_supported = (region->flags &
		SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED) != 0U;
	if ((result->grounded != 0) != region_supported ||
		region->water_level != (uint8_t)result->water_level ||
		region->water_type !=
			(sg_host_collision_contents_t)result->water_type)
		return 0;
	motion = result->water_level >= 2 ? SG_RUNE_MOTION_SWIMMING :
		(result->grounded ? SG_RUNE_MOTION_SUPPORTED :
			SG_RUNE_MOTION_AIRBORNE);
	support = motion == SG_RUNE_MOTION_SUPPORTED ?
		SG_RUNE_SUPPORT_SUPPORTED : SG_RUNE_SUPPORT_NONE;
	if (result->support_model_index != 0U || result->support_instance_id != 0U)
		return 0;
	return phase->stance == stance && phase->motion == motion &&
		phase->support == support && phase->medium == ResultMedium(result) &&
		phase->void_relation == ((region->flags &
			SG_CONFIGURATION_SEMANTIC_REGION_VOID_ADJACENT) ?
			SG_RUNE_VOID_ADJACENT : SG_RUNE_VOID_CLEAR) &&
		phase->reference_frame == SG_RUNE_FRAME_WORLD &&
		PhaseContainsVelocity(phase, result->velocity);
}

static void CommandForDirection(const float direction[3], usercmd_t *command)
{
	memset(command, 0, sizeof(*command));
	command->forwardmove = (short)(direction[0] *
		(float)SG_WATER_COMMAND_MAGNITUDE);
	command->sidemove = (short)(-direction[1] *
		(float)SG_WATER_COMMAND_MAGNITUDE);
	command->upmove = (short)(direction[2] *
		(float)SG_WATER_COMMAND_MAGNITUDE);
}

static int Probe(sg_water_build_t *build, uint32_t source_region,
	const float start[3], const float direction[3],
	sg_water_capability_fact_t *fact, sg_host_pmove_result_t *result_out)
{
	const sg_configuration_semantic_region_t *region =
		&build->semantics->regions[source_region];
	const sg_configuration_cell_t *cell =
		&build->configuration->cells[region->cell];
	sg_host_pmove_request_t request;
	sg_host_pmove_result_t result;
	sg_host_pmove_error_t error;
	uint32_t axis;

	memset(&request, 0, sizeof(request));
	request.state.pm_type = PM_NORMAL;
	if (cell->stance == SG_RUNE_STANCE_CROUCHING)
		request.state.pm_flags |= PMF_DUCKED;
	if (!PointToPmove(start, request.state.origin))
	{
		SetError(build, SG_WATER_CAPABILITY_ERROR_NONFINITE, source_region);
		return 0;
	}
	if (fact->source_phase >= build->phase_count ||
		!IntervalVelocityToPmove(
			&build->phases[fact->source_phase].velocity.x,
			&request.state.velocity[0]) ||
		!IntervalVelocityToPmove(
			&build->phases[fact->source_phase].velocity.y,
			&request.state.velocity[1]) ||
		!IntervalVelocityToPmove(
			&build->phases[fact->source_phase].velocity.z,
			&request.state.velocity[2]))
	{
		SetError(build, SG_WATER_CAPABILITY_ERROR_INVALID_PHASE,
			fact->source_phase);
		return 0;
	}
	request.state.gravity = (short)build->authority->identity.physics.gravity;
	request.previous_state = request.state;
	CommandForDirection(direction, &request.command);
	Copy3(fact->command_vector.value, direction);
	if (!SG_HostPmoveEvaluateFrame(build->authority, NULL, build->host_pmove,
		&request, &result, &error))
	{
		SetError(build, SG_WATER_CAPABILITY_ERROR_HOST_DISAGREEMENT,
			source_region);
		return 0;
	}
	if (result.physics_abi_id != build->authority->identity.physics_abi_id ||
		result.gravity != build->authority->identity.physics.gravity ||
		result.elapsed_ms != build->authority->identity.physics.frame_ms ||
		result.evaluated_steps != build->authority->identity.physics.frame_ms /
			build->authority->identity.physics.substep_ms ||
		result.state.pm_type != PM_NORMAL ||
		!Finite3(result.origin) || !Finite3(result.velocity) ||
		!Finite3(result.mins) || !Finite3(result.maxs) ||
		result.water_level < 0 || result.water_level > 3)
	{
		SetError(build, SG_WATER_CAPABILITY_ERROR_HOST_DISAGREEMENT,
			source_region);
		return 0;
	}
	for (axis = 0U; axis < 3U; axis++)
	{
		if (result.origin[axis] != (float)result.state.origin[axis] * 0.125f ||
			result.velocity[axis] !=
				(float)result.state.velocity[axis] * 0.125f)
		{
			SetError(build, SG_WATER_CAPABILITY_ERROR_HOST_DISAGREEMENT,
				source_region);
			return 0;
		}
		fact->observed_displacement.value[axis] = result.origin[axis] - start[axis];
		fact->observed_velocity.value[axis] = result.velocity[axis];
		fact->source_velocity.value[axis] =
			(float)request.state.velocity[axis] * 0.125f;
	}
	fact->result_pm_flags = result.state.pm_flags;
	fact->result_support_model_index = result.support_model_index;
	fact->result_support_instance_id = result.support_instance_id;
	fact->result_water_type =
		(sg_host_collision_contents_t)result.water_type;
	fact->result_grounded = (uint8_t)(result.grounded != 0);
	fact->result_water_level = (uint8_t)result.water_level;
	fact->parameters.displacement.x.min_value =
		fact->observed_displacement.value[0];
	fact->parameters.displacement.x.max_value =
		fact->observed_displacement.value[0];
	fact->parameters.displacement.y.min_value =
		fact->observed_displacement.value[1];
	fact->parameters.displacement.y.max_value =
		fact->observed_displacement.value[1];
	fact->parameters.displacement.z.min_value =
		fact->observed_displacement.value[2];
	fact->parameters.displacement.z.max_value =
		fact->observed_displacement.value[2];
	fact->parameters.duration_ms.min_value = (float)result.elapsed_ms;
	fact->parameters.duration_ms.max_value = (float)result.elapsed_ms;
	fact->parameters.speed.min_value = 0.0f;
	fact->parameters.speed.max_value =
		build->authority->identity.physics.max_velocity;
	fact->parameters.acceleration.min_value = 0.0f;
	switch (build->phases[fact->source_phase].motion)
	{
	case SG_RUNE_MOTION_SUPPORTED:
		fact->parameters.acceleration.max_value =
			build->authority->identity.physics.ground_acceleration;
		break;
	case SG_RUNE_MOTION_AIRBORNE:
		fact->parameters.acceleration.max_value =
			build->authority->identity.physics.air_acceleration;
		break;
	case SG_RUNE_MOTION_SWIMMING:
		fact->parameters.acceleration.max_value =
			build->authority->identity.physics.water_acceleration;
		break;
	case SG_RUNE_MOTION_COUNT:
		SetError(build, SG_WATER_CAPABILITY_ERROR_INVALID_PHASE,
			fact->source_phase);
		return 0;
	}
	fact->parameters.vertical_acceleration = fact->parameters.acceleration;
	fact->parameters.gravity = build->authority->identity.physics.gravity;
	fact->parameters.drag = fact->source_medium == SG_RUNE_MEDIUM_DRY ? 0.0f :
		build->authority->identity.physics.water_drag;
	fact->parameters.physics_abi_id =
		build->authority->identity.physics_abi_id;
	fact->parameters.fixed_latency_ms = build->authority->identity.physics.frame_ms;
	if (fact->source_medium != fact->destination_medium ||
		fact->source_water_level != fact->destination_water_level)
		fact->flags |= SG_WATER_CAPABILITY_STRADDLES_FRAME_LAW;
	fact->flags |= SG_WATER_CAPABILITY_HOST_PROVEN;
	build->output->host_pmove_frames++;
	if (result_out)
		*result_out = result;
	return 1;
}

static int AppendFact(sg_water_build_t *build,
	sg_water_capability_fact_t *fact)
{
	if (!GrowFacts(build))
		return 0;
	fact->order = build->output->fact_count;
	build->output->facts[build->output->fact_count++] = *fact;
	return 1;
}

static int ResolveDestinationPhase(sg_water_build_t *build, uint32_t region,
	const sg_host_pmove_result_t *result, uint32_t *phase_out)
{
	uint32_t binding;
	uint32_t matches = 0U;
	uint32_t matched_phase = 0U;

	for (binding = build->binding_offsets[region];
		binding < build->binding_offsets[region + 1U]; binding++)
	{
		uint32_t phase = build->bindings[binding].phase;

		if (!ResultMatchesDestination(build, region, phase, result))
			continue;
		matched_phase = phase;
		matches++;
		if (matches > 1U)
		{
			SetError(build, SG_WATER_CAPABILITY_ERROR_INVALID_PHASE, region);
			return -1;
		}
	}
	if (matches == 0U)
		return 0;
	*phase_out = matched_phase;
	return 1;
}

static int ProbeLocalFact(sg_water_build_t *build, uint32_t region,
	const float direction[3], sg_water_capability_fact_t *fact)
{
	sg_host_pmove_result_t result;
	uint32_t destination_phase;
	int resolved;

	if (!Probe(build, region, fact->source_witness.value, direction, fact,
		&result))
		return 0;
	if (!PointInRegion(build, region, result.origin))
		return 1;
	resolved = ResolveDestinationPhase(build, region, &result,
		&destination_phase);
	if (resolved <= 0)
		return resolved == 0;
	fact->destination_phase = destination_phase;
	return AppendFact(build, fact);
}

static void FillRegionFact(const sg_water_build_t *build, uint32_t region,
	uint32_t phase, sg_water_capability_kind_t kind,
	sg_water_direction_t direction, sg_water_capability_fact_t *fact)
{
	const sg_configuration_semantic_region_t *record =
		&build->semantics->regions[region];

	memset(fact, 0, sizeof(*fact));
	fact->source_region = region;
	fact->destination_region = region;
	fact->source_phase = phase;
	fact->destination_phase = phase;
	fact->portal = SG_WATER_CAPABILITY_INDEX_NONE;
	fact->kind = kind;
	fact->direction = direction;
	fact->source_medium = RegionMedium(record);
	fact->destination_medium = fact->source_medium;
	fact->source_contents =
		SG_HostCollisionRuneContents(record->water_type);
	fact->destination_contents = fact->source_contents;
	fact->source_water_level = record->water_level;
	fact->destination_water_level = record->water_level;
	fact->source_witness = record->interior_witness;
	fact->boundary_witness = record->interior_witness;
	fact->destination_witness = record->interior_witness;
	DirectionVector(direction, fact->direction_vector.value);
	fact->flags = SG_WATER_CAPABILITY_DIRECTIONAL;
}

static void CombinedCurrentVector(sg_rune_contents_mask_t currents,
	float vector[3])
{
	memset(vector, 0, 3U * sizeof(*vector));
	if (currents & SG_RUNE_CONTENTS_CURRENT_0) vector[0] += 1.0f;
	if (currents & SG_RUNE_CONTENTS_CURRENT_90) vector[1] += 1.0f;
	if (currents & SG_RUNE_CONTENTS_CURRENT_180) vector[0] -= 1.0f;
	if (currents & SG_RUNE_CONTENTS_CURRENT_270) vector[1] -= 1.0f;
	if (currents & SG_RUNE_CONTENTS_CURRENT_UP) vector[2] += 1.0f;
	if (currents & SG_RUNE_CONTENTS_CURRENT_DOWN) vector[2] -= 1.0f;
}

static int AppendLocalFacts(sg_water_build_t *build, uint32_t region_index)
{
	const sg_configuration_semantic_region_t *region =
		&build->semantics->regions[region_index];
	sg_host_collision_pose_t pose;
	sg_rune_contents_mask_t currents;
	uint32_t binding;

	if (region->water_level == 0U)
		return 1;
	if (!SG_HostCollisionClassifyPose(build->authority, NULL,
		region->interior_witness.value,
		build->configuration->cells[region->cell].stance, &pose) || !pose.valid)
	{
		SetError(build, SG_WATER_CAPABILITY_ERROR_INVALID_SOURCE,
			region_index);
		return 0;
	}
	currents = SG_HostCollisionRuneContents(
		(pose.water_type | pose.support.contents) &
		SG_WATER_HOST_CURRENT_MASK) & SG_RUNE_CONTENTS_CURRENT_MASK;
	build->output->wet_region_count++;
	for (binding = build->binding_offsets[region_index];
		binding < build->binding_offsets[region_index + 1U]; binding++)
	{
		uint32_t direction;

		if (region->water_level >= 2U)
		{
			sg_water_capability_fact_t fact;

			for (direction = SG_WATER_DIRECTION_POSITIVE_X;
				direction <= SG_WATER_DIRECTION_NEGATIVE_Z; direction++)
			{
				FillRegionFact(build, region_index,
					build->bindings[binding].phase,
					SG_WATER_CAPABILITY_DIRECTIONAL_SWIM,
					(sg_water_direction_t)direction, &fact);
				if (!ProbeLocalFact(build, region_index,
					fact.direction_vector.value, &fact))
					return 0;
			}
			FillRegionFact(build, region_index, build->bindings[binding].phase,
				SG_WATER_CAPABILITY_SINK, SG_WATER_DIRECTION_NEGATIVE_Z, &fact);
			memset(fact.direction_vector.value, 0,
				sizeof(fact.direction_vector.value));
			if (!ProbeLocalFact(build, region_index,
				fact.direction_vector.value, &fact))
				return 0;
			FillRegionFact(build, region_index, build->bindings[binding].phase,
				SG_WATER_CAPABILITY_SURFACE, SG_WATER_DIRECTION_POSITIVE_Z, &fact);
			if (!ProbeLocalFact(build, region_index,
				fact.direction_vector.value, &fact))
				return 0;
		}
		if (currents != 0U)
		{
			sg_water_capability_fact_t fact;
			float command[3] = { 0.0f, 0.0f, 0.0f };

			FillRegionFact(build, region_index, build->bindings[binding].phase,
				SG_WATER_CAPABILITY_CURRENT,
				SG_WATER_DIRECTION_COMBINED, &fact);
			fact.current = currents;
			fact.flags |= SG_WATER_CAPABILITY_USES_CURRENT;
			CombinedCurrentVector(currents, fact.direction_vector.value);
			if (!ProbeLocalFact(build, region_index, command, &fact))
				return 0;
		}
	}
	return 1;
}

static int SameSemanticSource(const sg_water_face_ref_t *left,
	const sg_water_face_ref_t *right)
{
	return left->cell == right->cell &&
		left->source_kind == right->source_kind &&
		left->source_index == right->source_index &&
		left->source_variant == right->source_variant &&
		left->sample_index == right->sample_index;
}

static int SameRegionSide(const sg_water_face_ref_t *left,
	const sg_water_face_ref_t *right)
{
	return left->region == right->region && left->reversed == right->reversed;
}

static int FaceRefCompare(const void *left_value, const void *right_value)
{
	const sg_water_face_ref_t *left = left_value;
	const sg_water_face_ref_t *right = right_value;

#define COMPARE(member) do { \
	if (left->member < right->member) return -1; \
	if (left->member > right->member) return 1; \
} while (0)
	COMPARE(cell);
	COMPARE(source_kind);
	COMPARE(source_index);
	COMPARE(source_variant);
	COMPARE(sample_index);
	COMPARE(reversed);
	COMPARE(region);
	COMPARE(face);
#undef COMPARE
	return 0;
}

static void CanonicalSemanticPlane(
	const sg_configuration_semantic_face_t *face, float normal[3],
	float *distance)
{
	uint32_t axis;
	uint32_t dominant = 0U;
	float scale;
	int flip;

	for (axis = 1U; axis < 3U; axis++)
		if (fabsf(face->normal[axis]) > fabsf(face->normal[dominant]))
			dominant = axis;
	scale = fabsf(face->normal[dominant]);
	flip = face->normal[dominant] < 0.0f;
	for (axis = 0U; axis < 3U; axis++)
		normal[axis] = (flip ? -face->normal[axis] : face->normal[axis]) /
			scale;
	*distance = (flip ? -face->distance : face->distance) / scale;
}

static void CanonicalConfigurationPlane(const sg_configuration_plane_t *plane,
	float normal[3], float *distance)
{
	sg_configuration_semantic_face_t face;

	memset(&face, 0, sizeof(face));
	Copy3(face.normal, plane->normal);
	face.distance = plane->distance;
	CanonicalSemanticPlane(&face, normal, distance);
}

static int SemanticPlaneCoplanar(
	const sg_configuration_semantic_face_t *face,
	const sg_configuration_plane_t *plane)
{
	float left_normal[3], right_normal[3], left_distance, right_distance;

	CanonicalSemanticPlane(face, left_normal, &left_distance);
	CanonicalConfigurationPlane(plane, right_normal, &right_distance);
	return fabsf(left_normal[0] - right_normal[0]) <=
		SG_WATER_PLANE_DISTANCE_EPSILON &&
		fabsf(left_normal[1] - right_normal[1]) <=
		SG_WATER_PLANE_DISTANCE_EPSILON &&
		fabsf(left_normal[2] - right_normal[2]) <=
		SG_WATER_PLANE_DISTANCE_EPSILON &&
		fabsf(left_distance - right_distance) <=
		SG_WATER_PLANE_DISTANCE_EPSILON;
}

static int SharedBoundaryWitness(sg_water_build_t *build,
	uint32_t first_region, uint32_t first_face, uint32_t second_region,
	uint32_t second_face, uint32_t portal_index, float witness[3])
{
	const sg_configuration_semantic_region_t *regions[2] = {
		&build->semantics->regions[first_region],
		&build->semantics->regions[second_region]
	};
	uint32_t shared_faces[2] = { first_face, second_face };
	sg_configuration_lattice_halfspace_t *halfspaces;
	uint8_t *clearance;
	sg_configuration_lattice_stats_t stats = { 0 };
	const sg_configuration_portal_t *portal =
		portal_index == SG_WATER_CAPABILITY_INDEX_NONE ? NULL :
		&build->configuration->portals[portal_index];
	uint32_t constraint_count;
	uint32_t constraint = 0U;
	uint32_t side;
	int32_t point[3];
	int positive_margin;
	int result;

	if (regions[0]->face_count > UINT32_MAX - regions[1]->face_count ||
		(portal && portal->vertex_count > UINT32_MAX - regions[0]->face_count -
			regions[1]->face_count))
	{
		SetError(build, SG_WATER_CAPABILITY_ERROR_OVERFLOW, first_region);
		return -1;
	}
	constraint_count = regions[0]->face_count + regions[1]->face_count +
		(portal ? portal->vertex_count : 0U);
	if (!AllocationFits((size_t)constraint_count, sizeof(*halfspaces)) ||
		!AllocationFits((size_t)constraint_count, sizeof(*clearance)))
	{
		SetError(build, SG_WATER_CAPABILITY_ERROR_OVERFLOW, first_region);
		return -1;
	}
	halfspaces = calloc(constraint_count, sizeof(*halfspaces));
	clearance = calloc(constraint_count, sizeof(*clearance));
	if (!halfspaces || !clearance)
	{
		free(halfspaces);
		free(clearance);
		SetError(build, SG_WATER_CAPABILITY_ERROR_OUT_OF_MEMORY, first_region);
		return -1;
	}
	for (side = 0U; side < 2U; side++)
	{
		uint32_t face;

		for (face = regions[side]->first_face;
			face < regions[side]->first_face + regions[side]->face_count; face++)
		{
			const sg_configuration_semantic_face_t *record =
				&build->semantics->faces[face];

			Copy3(halfspaces[constraint].normal, record->normal);
			halfspaces[constraint].distance = record->distance;
			halfspaces[constraint].open = record->open != 0U;
			clearance[constraint] = (uint8_t)(face != shared_faces[side]);
			constraint++;
		}
	}
	if (portal)
	{
		float center[3] = { 0.0f, 0.0f, 0.0f };
		uint32_t vertex;

		for (vertex = 0U; vertex < portal->vertex_count; vertex++)
		{
			const sg_rune_vec3_t *vertex_point = &build->configuration->vertices[
				portal->first_vertex + vertex];

			for (side = 0U; side < 3U; side++)
				center[side] += vertex_point->value[side];
		}
		for (side = 0U; side < 3U; side++)
			center[side] /= (float)portal->vertex_count;
		for (vertex = 0U; vertex < portal->vertex_count; vertex++)
		{
			const float *a = build->configuration->vertices[
				portal->first_vertex + vertex].value;
			const float *b = build->configuration->vertices[
				portal->first_vertex + (vertex + 1U) %
					portal->vertex_count].value;
			float edge[3];
			float normal[3];
			float distance;

			for (side = 0U; side < 3U; side++)
				edge[side] = b[side] - a[side];
			Cross3(edge, portal->plane.normal, normal);
			distance = Dot3(a, normal);
			if (Dot3(center, normal) > distance)
			{
				for (side = 0U; side < 3U; side++)
					normal[side] = -normal[side];
				distance = -distance;
			}
			Copy3(halfspaces[constraint].normal, normal);
			halfspaces[constraint].distance = distance;
			clearance[constraint] = 1U;
			constraint++;
		}
	}
	result = SG_ConfigurationLatticeFindMaxClearance(halfspaces, clearance,
		constraint_count, build->semantics->faces[first_face].normal, point,
		&positive_margin, &stats);
	free(halfspaces);
	free(clearance);
	build->output->lattice_solve_calls += stats.solve_calls;
	build->output->lattice_constraints += stats.constraints;
	if (stats.maximum_binary_shift >
		build->output->lattice_maximum_binary_shift)
		build->output->lattice_maximum_binary_shift =
			stats.maximum_binary_shift;
	if (result < 0)
	{
		SetError(build, SG_WATER_CAPABILITY_ERROR_SOLVER, first_region);
		return -1;
	}
	if (!result || !positive_margin)
		return 0;
	for (side = 0U; side < 3U; side++)
		witness[side] = (float)point[side] * 0.125f;
	return 1;
}

static int RegionSideWitness(sg_water_build_t *build, uint32_t region_index,
	uint32_t boundary_face, const float boundary_witness[3], float witness[3])
{
	const sg_configuration_semantic_region_t *region =
		&build->semantics->regions[region_index];
	const sg_configuration_semantic_face_t *boundary =
		&build->semantics->faces[boundary_face];
	sg_configuration_lattice_halfspace_t *halfspaces;
	sg_configuration_lattice_stats_t stats = { 0 };
	int32_t point[3];
	uint32_t local;
	uint32_t dominant = 0U;
	int result;
	int direct = 1;

	for (local = 1U; local < 3U; local++)
		if (fabsf(boundary->normal[local]) >
			fabsf(boundary->normal[dominant]))
			dominant = local;
	Copy3(witness, boundary_witness);
	witness[dominant] += boundary->normal[dominant] > 0.0f ? -0.125f : 0.125f;
	for (local = 0U; local < region->face_count; local++)
	{
		const sg_configuration_semantic_face_t *record =
			&build->semantics->faces[region->first_face + local];

		if (!SG_ConfigurationSemanticFaceContainsPoint(record, witness) ||
			(local + region->first_face == boundary_face &&
			 PlaneSignedDistance(witness, record->normal,
				record->distance) >= 0.0))
			direct = 0;
	}
	if (direct)
		return 1;

	if (region->face_count > UINT32_MAX - 6U ||
		!AllocationFits((size_t)region->face_count + 6U,
			sizeof(*halfspaces)))
	{
		SetError(build, SG_WATER_CAPABILITY_ERROR_OVERFLOW, region_index);
		return -1;
	}
	halfspaces = calloc(region->face_count + 6U, sizeof(*halfspaces));
	if (!halfspaces)
	{
		SetError(build, SG_WATER_CAPABILITY_ERROR_OUT_OF_MEMORY, region_index);
		return -1;
	}
	for (local = 0U; local < region->face_count; local++)
	{
		uint32_t face = region->first_face + local;
		const sg_configuration_semantic_face_t *record =
			&build->semantics->faces[face];

		Copy3(halfspaces[local].normal, record->normal);
		halfspaces[local].distance = record->distance;
		halfspaces[local].open = record->open != 0U || face == boundary_face;
	}
	for (local = 0U; local < 3U; local++)
	{
		uint32_t upper = region->face_count + local * 2U;
		uint32_t lower = upper + 1U;

		halfspaces[upper].normal[local] = 1.0f;
		halfspaces[upper].distance = boundary_witness[local] + 0.125f;
		halfspaces[lower].normal[local] = -1.0f;
		halfspaces[lower].distance = -boundary_witness[local] + 0.125f;
	}
	result = SG_ConfigurationLatticeFind(halfspaces, region->face_count + 6U,
		boundary->normal, point, &stats);
	free(halfspaces);
	build->output->lattice_solve_calls += stats.solve_calls;
	build->output->lattice_constraints += stats.constraints;
	if (stats.maximum_binary_shift >
		build->output->lattice_maximum_binary_shift)
		build->output->lattice_maximum_binary_shift =
			stats.maximum_binary_shift;
	if (result < 0)
	{
		SetError(build, SG_WATER_CAPABILITY_ERROR_SOLVER, region_index);
		return -1;
	}
	if (!result)
		return 0;
	for (local = 0U; local < 3U; local++)
		witness[local] = (float)point[local] * 0.125f;
	if (PlaneSignedDistance(witness, boundary->normal,
		boundary->distance) >= 0.0)
		return 0;
	for (local = 0U; local < region->face_count; local++)
	{
		const sg_configuration_semantic_face_t *record =
			&build->semantics->faces[region->first_face + local];

		if (!SG_ConfigurationSemanticFaceContainsPoint(record, witness))
			return 0;
	}
	return 1;
}

static int PointInRegion(const sg_water_build_t *build, uint32_t region_index,
	const float point[3])
{
	const sg_configuration_semantic_region_t *region =
		&build->semantics->regions[region_index];
	uint32_t face;

	for (face = region->first_face;
		face < region->first_face + region->face_count; face++)
	{
		const sg_configuration_semantic_face_t *record =
			&build->semantics->faces[face];

		if (!SG_ConfigurationSemanticFaceContainsPoint(record, point))
			return 0;
	}
	return 1;
}

static sg_water_capability_kind_t BoundaryKind(
	const sg_configuration_semantic_region_t *source,
	const sg_configuration_semantic_region_t *destination)
{
	if (source->water_level == 0U && destination->water_level != 0U)
		return SG_WATER_CAPABILITY_ENTRY;
	if (source->water_level != 0U && destination->water_level == 0U)
		return SG_WATER_CAPABILITY_EXIT;
	return SG_WATER_CAPABILITY_VOLUME_CROSSING;
}

static int AppendBoundaryDirection(sg_water_build_t *build,
	uint32_t source_region, uint32_t destination_region, uint32_t portal,
	const float source_witness[3], const float boundary_witness[3],
	const float destination_witness[3])
{
	const sg_configuration_semantic_region_t *source =
		&build->semantics->regions[source_region];
	const sg_configuration_semantic_region_t *destination =
		&build->semantics->regions[destination_region];
	sg_host_collision_transition_t transition;
	sg_host_pmove_result_t result;
	float direction[3];
	float length;
	uint32_t axis;
	uint32_t source_binding;

	if (!SG_HostCollisionTransition(build->authority, NULL,
		source_witness, destination_witness,
		build->configuration->cells[source->cell].stance, &transition) ||
		!transition.clear)
		return 1;
	for (axis = 0U; axis < 3U; axis++)
		direction[axis] = destination_witness[axis] - source_witness[axis];
	length = sqrtf(Dot3(direction, direction));
	if (!isfinite(length) || length <= 0.0f)
		return 1;
	for (axis = 0U; axis < 3U; axis++)
		direction[axis] /= length;
	for (source_binding = build->binding_offsets[source_region];
		source_binding < build->binding_offsets[source_region + 1U];
		source_binding++)
	{
		sg_water_capability_fact_t fact;
		uint32_t destination_phase;
		int resolved;

		memset(&fact, 0, sizeof(fact));
		fact.source_region = source_region;
		fact.destination_region = destination_region;
		fact.source_phase = build->bindings[source_binding].phase;
		fact.portal = portal;
		fact.kind = BoundaryKind(source, destination);
		fact.direction = SG_WATER_DIRECTION_BOUNDARY;
		fact.source_medium = RegionMedium(source);
		fact.destination_medium = RegionMedium(destination);
		fact.source_contents =
			SG_HostCollisionRuneContents(source->water_type);
		fact.destination_contents =
			SG_HostCollisionRuneContents(destination->water_type);
		fact.source_water_level = source->water_level;
		fact.destination_water_level = destination->water_level;
		Copy3(fact.source_witness.value, source_witness);
		Copy3(fact.boundary_witness.value, boundary_witness);
		Copy3(fact.destination_witness.value, destination_witness);
		Copy3(fact.direction_vector.value, direction);
		fact.flags = SG_WATER_CAPABILITY_DIRECTIONAL;
		if (fact.source_medium != fact.destination_medium)
			fact.flags |= SG_WATER_CAPABILITY_CHANGES_MEDIUM;
		if (portal != SG_WATER_CAPABILITY_INDEX_NONE)
			fact.flags |= SG_WATER_CAPABILITY_CROSSES_PORTAL;
		if (!Probe(build, source_region, source_witness, direction, &fact,
				&result))
			return 0;
		if (!PointInRegion(build, destination_region, result.origin))
			continue;
		resolved = ResolveDestinationPhase(build, destination_region,
			&result, &destination_phase);
		if (resolved < 0)
			return 0;
		if (resolved == 0)
			continue;
		fact.destination_phase = destination_phase;
		if (!AppendFact(build, &fact))
			return 0;
	}
	return 1;
}

static int AppendBoundaryPair(sg_water_build_t *build, uint32_t first_region,
	uint32_t first_face, uint32_t second_region, uint32_t second_face,
	uint32_t portal)
{
	const sg_configuration_semantic_region_t *first =
		&build->semantics->regions[first_region];
	const sg_configuration_semantic_region_t *second =
		&build->semantics->regions[second_region];
	float witness[3];
	float first_witness[3];
	float second_witness[3];
	uint32_t axis;
	int shared;

	if (first->water_level == 0U && second->water_level == 0U)
		return 1;
	for (axis = 0U; axis < 3U; axis++)
		if (fmaxf(first->bounds.mins.value[axis],
				second->bounds.mins.value[axis]) >
			fminf(first->bounds.maxs.value[axis],
				second->bounds.maxs.value[axis]) + SG_WATER_BOUNDS_EPSILON)
			return 1;
	shared = SharedBoundaryWitness(build, first_region, first_face,
		second_region, second_face, portal, witness);
	if (shared <= 0)
		return shared == 0;
	shared = RegionSideWitness(build, first_region, first_face, witness,
		first_witness);
	if (shared <= 0)
		return shared == 0;
	shared = RegionSideWitness(build, second_region, second_face, witness,
		second_witness);
	if (shared <= 0)
		return shared == 0;
	if (!AppendBoundaryDirection(build, first_region, second_region, portal,
		first_witness, witness, second_witness) ||
		!AppendBoundaryDirection(build, second_region, first_region, portal,
			second_witness, witness, first_witness))
		return 0;
	return 1;
}

static int SweepEventCompare(const void *left_value, const void *right_value)
{
	const sg_water_sweep_event_t *left = left_value;
	const sg_water_sweep_event_t *right = right_value;

	if (left->coordinate < right->coordinate) return -1;
	if (left->coordinate > right->coordinate) return 1;
	if (left->starts > right->starts) return -1;
	if (left->starts < right->starts) return 1;
	if (left->reference < right->reference) return -1;
	if (left->reference > right->reference) return 1;
	return 0;
}

static uint32_t IntervalHeight(const sg_water_interval_node_t *nodes,
	uint32_t node)
{
	return node == UINT32_MAX ? 0U : nodes[node].height;
}

static void UpdateIntervalNode(sg_water_interval_node_t *nodes, uint32_t node)
{
	uint32_t left_height = IntervalHeight(nodes, nodes[node].left);
	uint32_t right_height = IntervalHeight(nodes, nodes[node].right);
	float high = nodes[node].high;

	if (nodes[node].left != UINT32_MAX)
		high = fmaxf(high, nodes[nodes[node].left].subtree_high);
	if (nodes[node].right != UINT32_MAX)
		high = fmaxf(high, nodes[nodes[node].right].subtree_high);
	nodes[node].height = 1U + (left_height > right_height ?
		left_height : right_height);
	nodes[node].subtree_high = high;
}

static int IntervalKeyCompare(const sg_water_interval_node_t *left,
	const sg_water_interval_node_t *right)
{
	if (left->low < right->low) return -1;
	if (left->low > right->low) return 1;
	if (left->high < right->high) return -1;
	if (left->high > right->high) return 1;
	if (left->reference < right->reference) return -1;
	if (left->reference > right->reference) return 1;
	return 0;
}

static uint32_t RotateIntervalLeft(sg_water_interval_node_t *nodes,
	uint32_t root)
{
	uint32_t replacement = nodes[root].right;

	nodes[root].right = nodes[replacement].left;
	nodes[replacement].left = root;
	UpdateIntervalNode(nodes, root);
	UpdateIntervalNode(nodes, replacement);
	return replacement;
}

static uint32_t RotateIntervalRight(sg_water_interval_node_t *nodes,
	uint32_t root)
{
	uint32_t replacement = nodes[root].left;

	nodes[root].left = nodes[replacement].right;
	nodes[replacement].right = root;
	UpdateIntervalNode(nodes, root);
	UpdateIntervalNode(nodes, replacement);
	return replacement;
}

static uint32_t BalanceIntervalNode(sg_water_interval_node_t *nodes,
	uint32_t root)
{
	int balance;

	UpdateIntervalNode(nodes, root);
	balance = (int)IntervalHeight(nodes, nodes[root].left) -
		(int)IntervalHeight(nodes, nodes[root].right);
	if (balance > 1)
	{
		uint32_t left = nodes[root].left;

		if (IntervalHeight(nodes, nodes[left].right) >
			IntervalHeight(nodes, nodes[left].left))
			nodes[root].left = RotateIntervalLeft(nodes, left);
		return RotateIntervalRight(nodes, root);
	}
	if (balance < -1)
	{
		uint32_t right = nodes[root].right;

		if (IntervalHeight(nodes, nodes[right].left) >
			IntervalHeight(nodes, nodes[right].right))
			nodes[root].right = RotateIntervalRight(nodes, right);
		return RotateIntervalLeft(nodes, root);
	}
	return root;
}

static uint32_t InsertIntervalNode(sg_water_interval_node_t *nodes,
	uint32_t root, uint32_t inserted)
{
	if (root == UINT32_MAX)
		return inserted;
	if (IntervalKeyCompare(&nodes[inserted], &nodes[root]) < 0)
		nodes[root].left = InsertIntervalNode(nodes, nodes[root].left, inserted);
	else
		nodes[root].right = InsertIntervalNode(nodes, nodes[root].right, inserted);
	return BalanceIntervalNode(nodes, root);
}

static uint32_t RemoveIntervalNode(sg_water_interval_node_t *nodes,
	uint32_t root, const sg_water_interval_node_t *removed)
{
	int comparison;

	if (root == UINT32_MAX)
		return root;
	comparison = IntervalKeyCompare(removed, &nodes[root]);
	if (comparison < 0)
		nodes[root].left = RemoveIntervalNode(nodes, nodes[root].left, removed);
	else if (comparison > 0)
		nodes[root].right = RemoveIntervalNode(nodes, nodes[root].right, removed);
	else if (nodes[root].left == UINT32_MAX ||
		nodes[root].right == UINT32_MAX)
		return nodes[root].left != UINT32_MAX ?
			nodes[root].left : nodes[root].right;
	else
	{
		uint32_t successor = nodes[root].right;
		sg_water_interval_node_t key;

		while (nodes[successor].left != UINT32_MAX)
			successor = nodes[successor].left;
		key = nodes[successor];
		nodes[root].low = key.low;
		nodes[root].high = key.high;
		nodes[root].reference = key.reference;
		nodes[root].right = RemoveIntervalNode(nodes, nodes[root].right, &key);
	}
	return BalanceIntervalNode(nodes, root);
}

static int PositiveBoundsOverlap(const sg_water_build_t *build,
	const sg_water_face_ref_t *first, const sg_water_face_ref_t *second,
	uint32_t first_axis, uint32_t second_axis)
{
	const sg_rune_bounds_t *left =
		&build->semantics->regions[first->region].bounds;
	const sg_rune_bounds_t *right =
		&build->semantics->regions[second->region].bounds;

	return fmaxf(left->mins.value[first_axis],
			right->mins.value[first_axis]) <
		fminf(left->maxs.value[first_axis], right->maxs.value[first_axis]) &&
		fmaxf(left->mins.value[second_axis],
			right->mins.value[second_axis]) <
		fminf(left->maxs.value[second_axis], right->maxs.value[second_axis]);
}

static int QueryIntervalTree(sg_water_build_t *build,
	const sg_water_face_ref_t *references,
	const sg_water_interval_node_t *nodes, uint32_t root,
	uint32_t query_reference, uint32_t sweep_axis, uint32_t interval_axis)
{
	const sg_water_face_ref_t *query = &references[query_reference];
	const sg_rune_bounds_t *query_bounds =
		&build->semantics->regions[query->region].bounds;
	float low = query_bounds->mins.value[interval_axis];
	float high = query_bounds->maxs.value[interval_axis];
	const sg_water_interval_node_t *node;

	if (root == UINT32_MAX)
		return 1;
	node = &nodes[root];
	if (node->left != UINT32_MAX &&
		nodes[node->left].subtree_high > low &&
		!QueryIntervalTree(build, references, nodes, node->left,
			query_reference, sweep_axis, interval_axis))
		return 0;
	if (node->low < high && node->high > low)
	{
		const sg_water_face_ref_t *candidate =
			&references[node->reference];

		if (candidate->region != query->region &&
			PositiveBoundsOverlap(build, candidate, query,
				sweep_axis, interval_axis))
		{
			build->output->same_cell_candidate_pairs++;
			if (!AppendBoundaryPair(build, candidate->region, candidate->face,
				query->region, query->face,
				SG_WATER_CAPABILITY_INDEX_NONE))
				return 0;
		}
	}
	if (node->low < high &&
		!QueryIntervalTree(build, references, nodes, node->right,
			query_reference, sweep_axis, interval_axis))
		return 0;
	return 1;
}

static int BuildSameCellBoundaryGroup(sg_water_build_t *build,
	const sg_water_face_ref_t *references, uint32_t group_start,
	uint32_t group_end)
{
	sg_water_sweep_event_t *events = NULL;
	sg_water_interval_node_t *nodes = NULL;
	uint32_t roots[2] = { UINT32_MAX, UINT32_MAX };
	uint32_t unique_count = 0U;
	uint32_t event_count;
	uint32_t reference;
	uint32_t event;
	uint32_t dominant = 0U;
	uint32_t tangents[2];
	uint32_t sweep_axis;
	uint32_t interval_axis;
	float center_span[2];
	int result = 0;

	for (reference = group_start; reference < group_end; reference++)
		if (reference == group_start ||
			!SameRegionSide(&references[reference - 1U],
				&references[reference]))
			unique_count++;
	if (unique_count > UINT32_MAX / 2U ||
		!AllocationFits((size_t)unique_count * 2U, sizeof(*events)) ||
		!AllocationFits((size_t)unique_count, sizeof(*nodes)))
	{
		SetError(build, SG_WATER_CAPABILITY_ERROR_OVERFLOW, unique_count);
		return 0;
	}
	events = malloc((size_t)unique_count * 2U * sizeof(*events));
	nodes = calloc(unique_count, sizeof(*nodes));
	if ((!events || !nodes) && unique_count != 0U)
	{
		SetError(build, SG_WATER_CAPABILITY_ERROR_OUT_OF_MEMORY, group_start);
		goto done;
	}
	for (reference = 1U; reference < 3U; reference++)
		if (fabsf(build->semantics->faces[references[group_start].face].normal[
			reference]) > fabsf(build->semantics->faces[
				references[group_start].face].normal[dominant]))
			dominant = reference;
	tangents[0] = (dominant + 1U) % 3U;
	tangents[1] = (dominant + 2U) % 3U;
	for (reference = 0U; reference < 2U; reference++)
	{
		float minimum = FLT_MAX;
		float maximum = -FLT_MAX;
		uint32_t source;

		for (source = group_start; source < group_end; source++)
		{
			const sg_rune_bounds_t *bounds =
				&build->semantics->regions[references[source].region].bounds;
			float center = (bounds->mins.value[tangents[reference]] +
				bounds->maxs.value[tangents[reference]]) * 0.5f;

			minimum = fminf(minimum, center);
			maximum = fmaxf(maximum, center);
		}
		center_span[reference] = maximum - minimum;
	}
	sweep_axis = center_span[1] > center_span[0] ? tangents[1] : tangents[0];
	interval_axis = sweep_axis == tangents[0] ? tangents[1] : tangents[0];
	event_count = 0U;
	for (reference = group_start; reference < group_end; reference++)
	{
		const sg_rune_bounds_t *bounds;
		uint32_t node;

		if (reference != group_start &&
			SameRegionSide(&references[reference - 1U], &references[reference]))
			continue;
		node = event_count / 2U;
		bounds = &build->semantics->regions[references[reference].region].bounds;
		nodes[node].low = bounds->mins.value[interval_axis];
		nodes[node].high = bounds->maxs.value[interval_axis];
		nodes[node].subtree_high = nodes[node].high;
		nodes[node].reference = reference;
		nodes[node].left = UINT32_MAX;
		nodes[node].right = UINT32_MAX;
		nodes[node].height = 1U;
		events[event_count].coordinate = bounds->mins.value[sweep_axis];
		events[event_count].reference = node;
		events[event_count++].starts = 1U;
		events[event_count].coordinate = bounds->maxs.value[sweep_axis];
		events[event_count].reference = node;
		events[event_count++].starts = 0U;
	}
	qsort(events, event_count, sizeof(*events), SweepEventCompare);
	for (event = 0U; event < event_count; event++)
	{
		uint32_t node = events[event].reference;
		uint32_t side = references[nodes[node].reference].reversed ? 1U : 0U;

		if (events[event].starts)
		{
			if (!QueryIntervalTree(build, references, nodes, roots[1U - side],
				nodes[node].reference, sweep_axis, interval_axis))
				goto done;
			roots[side] = InsertIntervalNode(nodes, roots[side], node);
		}
		else
			roots[side] = RemoveIntervalNode(nodes, roots[side], &nodes[node]);
	}
	result = 1;
done:
	free(events);
	free(nodes);
	return result;
}

static int BuildSameCellBoundaries(sg_water_build_t *build)
{
	sg_water_face_ref_t *references;
	uint32_t reference_count = 0U;
	uint32_t region;
	uint32_t group_start;

	if (!AllocationFits((size_t)build->semantics->face_count,
		sizeof(*references)))
	{
		SetError(build, SG_WATER_CAPABILITY_ERROR_OVERFLOW,
			build->semantics->face_count);
		return 0;
	}
	references = malloc((size_t)build->semantics->face_count *
		sizeof(*references));
	if (!references && build->semantics->face_count != 0U)
	{
		SetError(build, SG_WATER_CAPABILITY_ERROR_OUT_OF_MEMORY, 0U);
		return 0;
	}
	for (region = 0U; region < build->semantics->region_count; region++)
	{
		const sg_configuration_semantic_region_t *record =
			&build->semantics->regions[region];
		uint32_t face;

		for (face = record->first_face;
			face < record->first_face + record->face_count; face++)
		{
			const sg_configuration_semantic_face_t *source =
				&build->semantics->faces[face];
			sg_water_face_ref_t *destination;

			if (source->source_kind == SG_CONFIGURATION_SEMANTIC_PLANE_CELL)
				continue;
			destination = &references[reference_count++];
			destination->cell = record->cell;
			destination->region = region;
			destination->face = face;
			destination->source_kind = source->source_kind;
			destination->source_index = source->source_index;
			destination->source_variant = source->source_variant;
			destination->sample_index = source->sample_index;
			destination->reversed = source->reversed;
		}
	}
	qsort(references, reference_count, sizeof(*references), FaceRefCompare);
	for (group_start = 0U; group_start < reference_count; )
	{
		uint32_t group_end = group_start + 1U;

		while (group_end < reference_count &&
			SameSemanticSource(&references[group_start], &references[group_end]))
			group_end++;
		if (!BuildSameCellBoundaryGroup(build, references, group_start,
			group_end))
		{
			free(references);
			return 0;
		}
		group_start = group_end;
	}
	free(references);
	return 1;
}

static uint32_t FindPortalFace(const sg_water_build_t *build,
	uint32_t region_index, const sg_configuration_portal_t *portal)
{
	const sg_configuration_semantic_region_t *region =
		&build->semantics->regions[region_index];
	uint32_t face;

	for (face = region->first_face;
		face < region->first_face + region->face_count; face++)
	{
		const sg_configuration_semantic_face_t *record =
			&build->semantics->faces[face];

		if (record->source_kind == SG_CONFIGURATION_SEMANTIC_PLANE_CELL &&
			SemanticPlaneCoplanar(record, &portal->plane))
			return face;
	}
	return SG_WATER_CAPABILITY_INDEX_NONE;
}

static int BuildPortalBoundaries(sg_water_build_t *build)
{
	uint32_t portal_index;

	for (portal_index = 0U;
		portal_index < build->configuration->portal_count; portal_index++)
	{
		const sg_configuration_portal_t *portal =
			&build->configuration->portals[portal_index];
		uint32_t first_region;

		for (first_region = build->cell_region_offsets[portal->from_cell];
			first_region < build->cell_region_offsets[portal->from_cell + 1U];
			first_region++)
		{
			uint32_t first_face;
			uint32_t second_region;

			first_face = FindPortalFace(build, first_region, portal);
			if (first_face == SG_WATER_CAPABILITY_INDEX_NONE)
				continue;
			for (second_region = build->cell_region_offsets[portal->to_cell];
				second_region < build->cell_region_offsets[portal->to_cell + 1U];
				second_region++)
			{
				uint32_t second_face;

				second_face = FindPortalFace(build, second_region, portal);
				if (second_face != SG_WATER_CAPABILITY_INDEX_NONE &&
					!AppendBoundaryPair(build, first_region, first_face,
						second_region, second_face, portal_index))
					return 0;
			}
		}
	}
	return 1;
}

static int FactCompare(const void *left_value, const void *right_value)
{
	const sg_water_capability_fact_t *left = left_value;
	const sg_water_capability_fact_t *right = right_value;

#define COMPARE(member) do { \
	if (left->member < right->member) return -1; \
	if (left->member > right->member) return 1; \
} while (0)
	COMPARE(source_region);
	COMPARE(destination_region);
	COMPARE(source_phase);
	COMPARE(destination_phase);
	COMPARE(portal);
	COMPARE(kind);
	COMPARE(direction);
	COMPARE(current);
#undef COMPARE
	return memcmp(&left->source_region, &right->source_region,
		sizeof(*left) - offsetof(sg_water_capability_fact_t, source_region));
}

static int BoundaryKeyCompare(const void *left_value, const void *right_value)
{
	const sg_water_boundary_key_t *left = left_value;
	const sg_water_boundary_key_t *right = right_value;

#define COMPARE(member) do { \
	if (left->member < right->member) return -1; \
	if (left->member > right->member) return 1; \
} while (0)
	COMPARE(first_region);
	COMPARE(second_region);
	COMPARE(portal);
#undef COMPARE
	return 0;
}

static int FinalizeFacts(sg_water_build_t *build)
{
	sg_water_capability_set_t *output = build->output;
	sg_water_boundary_key_t *boundaries;
	uint32_t fact;
	uint32_t write = 0U;
	uint32_t boundary_count = 0U;

	if (output->fact_count != 0U)
		qsort(output->facts, output->fact_count, sizeof(*output->facts),
			FactCompare);
	for (fact = 0U; fact < output->fact_count; fact++)
		if (write == 0U ||
			FactCompare(&output->facts[write - 1U], &output->facts[fact]) != 0)
			output->facts[write++] = output->facts[fact];
	output->fact_count = write;
	if (output->fact_count > SG_RUNE_MODEL_MAX_KERNELS)
	{
		SetError(build, SG_WATER_CAPABILITY_ERROR_OVERFLOW,
			output->fact_count);
		return 0;
	}
	if (!AllocationFits((size_t)output->fact_count, sizeof(*boundaries)))
	{
		SetError(build, SG_WATER_CAPABILITY_ERROR_OVERFLOW,
			output->fact_count);
		return 0;
	}
	boundaries = calloc(output->fact_count, sizeof(*boundaries));
	if (!boundaries && output->fact_count != 0U)
	{
		SetError(build, SG_WATER_CAPABILITY_ERROR_OUT_OF_MEMORY,
			output->fact_count);
		return 0;
	}
	for (fact = 0U; fact < output->fact_count; fact++)
	{
		const sg_water_capability_fact_t *record = &output->facts[fact];

		output->facts[fact].order = fact;
		if (record->source_region == record->destination_region ||
			(record->kind != SG_WATER_CAPABILITY_ENTRY &&
			 record->kind != SG_WATER_CAPABILITY_EXIT &&
			 record->kind != SG_WATER_CAPABILITY_VOLUME_CROSSING))
			continue;
		boundaries[boundary_count].first_region =
			record->source_region < record->destination_region ?
			record->source_region : record->destination_region;
		boundaries[boundary_count].second_region =
			record->source_region < record->destination_region ?
			record->destination_region : record->source_region;
		boundaries[boundary_count].portal = record->portal;
		boundary_count++;
	}
	if (boundary_count != 0U)
		qsort(boundaries, boundary_count, sizeof(*boundaries),
			BoundaryKeyCompare);
	output->boundary_count = 0U;
	for (fact = 0U; fact < boundary_count; fact++)
		if (fact == 0U || BoundaryKeyCompare(&boundaries[fact - 1U],
			&boundaries[fact]) != 0)
			output->boundary_count++;
	free(boundaries);
	return 1;
}

int SG_WaterCapabilityBuild(
	const sg_host_collision_authority_t *authority,
	sg_host_pmove_function_t host_pmove,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	const sg_rune_phase_basis_t *phases, uint32_t phase_count,
	const sg_water_phase_binding_t *bindings, uint32_t binding_count,
	sg_water_capability_set_t **capabilities_out,
	sg_water_capability_error_t *error_out)
{
	sg_water_build_t build;
	uint32_t region;
	int success = 0;

	memset(&build, 0, sizeof(build));
	build.error.source_index = SG_WATER_CAPABILITY_INDEX_NONE;
	build.authority = authority;
	build.host_pmove = host_pmove;
	build.configuration = configuration;
	build.semantics = semantics;
	build.phases = phases;
	build.phase_count = phase_count;
	build.bindings = bindings;
	build.binding_count = binding_count;
	if (!capabilities_out || *capabilities_out)
	{
		build.error.code = SG_WATER_CAPABILITY_ERROR_INVALID_ARGUMENT;
		goto done;
	}
	if (!SourceValid(&build))
	{
		build.error.code = SG_WATER_CAPABILITY_ERROR_INVALID_SOURCE;
		goto done;
	}
	build.output = calloc(1U, sizeof(*build.output));
	if (!build.output)
	{
		build.error.code = SG_WATER_CAPABILITY_ERROR_OUT_OF_MEMORY;
		goto done;
	}
	build.output->identity = authority->identity;
	if (!BuildBindingOffsets(&build) || !BuildCellRegionOffsets(&build))
		goto done;
	for (region = 0U; region < semantics->region_count; region++)
		if (!AppendLocalFacts(&build, region))
			goto done;
	if (!BuildSameCellBoundaries(&build) || !BuildPortalBoundaries(&build))
		goto done;
	if (!FinalizeFacts(&build))
		goto done;
	*capabilities_out = build.output;
	build.output = NULL;
	success = 1;

done:
	free(build.binding_offsets);
	free(build.cell_region_offsets);
	SG_WaterCapabilityDestroy(build.output);
	if (error_out)
		*error_out = build.error;
	return success;
}

void SG_WaterCapabilityDestroy(sg_water_capability_set_t *capabilities)
{
	if (!capabilities)
		return;
	free(capabilities->facts);
	free(capabilities);
}

const char *SG_WaterCapabilityErrorString(
	sg_water_capability_error_code_t code)
{
	switch (code)
	{
	case SG_WATER_CAPABILITY_ERROR_NONE: return "none";
	case SG_WATER_CAPABILITY_ERROR_INVALID_ARGUMENT: return "invalid argument";
	case SG_WATER_CAPABILITY_ERROR_INVALID_SOURCE: return "invalid source";
	case SG_WATER_CAPABILITY_ERROR_INVALID_PHASE: return "invalid phase";
	case SG_WATER_CAPABILITY_ERROR_NONFINITE: return "nonfinite input";
	case SG_WATER_CAPABILITY_ERROR_HOST_DISAGREEMENT: return "host disagreement";
	case SG_WATER_CAPABILITY_ERROR_SOLVER: return "solver failure";
	case SG_WATER_CAPABILITY_ERROR_OVERFLOW: return "representation overflow";
	case SG_WATER_CAPABILITY_ERROR_OUT_OF_MEMORY: return "out of memory";
	default: return "unknown water capability error";
	}
}
