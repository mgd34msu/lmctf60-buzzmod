#include "sg_water_capability.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "sg_configuration_lattice.h"

#define SG_WATER_COMMAND_MAGNITUDE INT16_C(400)
#define SG_WATER_PLANE_EPSILON 0.0001f

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
	const sg_water_capability_limits_t *limits;
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
		isfinite(physics->water_acceleration) &&
		physics->water_acceleration >= 0.0f &&
		isfinite(physics->water_drag) && physics->water_drag >= 0.0f &&
		isfinite(physics->max_velocity) && physics->max_velocity > 0.0f &&
		physics->gravity <= (float)SHRT_MAX &&
		truncf(physics->gravity) == physics->gravity &&
		physics->frame_ms != 0U && physics->substep_ms != 0U &&
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
			 record->normal[2] == 0.0f))
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
		build->binding_count == 0U || !build->limits ||
		build->limits->max_facts == 0U ||
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
			!isfinite(record->plane.distance))
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
		!Finite3(result.origin) || !Finite3(result.velocity))
	{
		SetError(build, SG_WATER_CAPABILITY_ERROR_HOST_DISAGREEMENT,
			source_region);
		return 0;
	}
	for (axis = 0U; axis < 3U; axis++)
	{
		fact->observed_displacement.value[axis] = result.origin[axis] - start[axis];
		fact->observed_velocity.value[axis] = result.velocity[axis];
	}
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
	fact->parameters.acceleration.max_value =
		build->authority->identity.physics.water_acceleration;
	fact->parameters.vertical_acceleration = fact->parameters.acceleration;
	fact->parameters.gravity = build->authority->identity.physics.gravity;
	fact->parameters.drag = build->authority->identity.physics.water_drag;
	fact->parameters.physics_abi_id =
		build->authority->identity.physics_abi_id;
	fact->parameters.fixed_latency_ms = build->authority->identity.physics.frame_ms;
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

static int ProbeLocalFact(sg_water_build_t *build, uint32_t region,
	const float direction[3], sg_water_capability_fact_t *fact)
{
	sg_host_pmove_result_t result;

	if (!Probe(build, region, fact->source_witness.value, direction, fact,
		&result))
		return 0;
	if (!PointInRegion(build, region, result.origin))
		return 1;
	if (!PhaseContainsVelocity(&build->phases[fact->destination_phase],
		result.velocity))
		return 1;
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

static int AppendLocalFacts(sg_water_build_t *build, uint32_t region_index)
{
	static const sg_rune_contents_mask_t currents[] = {
		SG_RUNE_CONTENTS_CURRENT_0,
		SG_RUNE_CONTENTS_CURRENT_90,
		SG_RUNE_CONTENTS_CURRENT_180,
		SG_RUNE_CONTENTS_CURRENT_270,
		SG_RUNE_CONTENTS_CURRENT_UP,
		SG_RUNE_CONTENTS_CURRENT_DOWN
	};
	static const sg_water_direction_t current_directions[] = {
		SG_WATER_DIRECTION_POSITIVE_X,
		SG_WATER_DIRECTION_POSITIVE_Y,
		SG_WATER_DIRECTION_NEGATIVE_X,
		SG_WATER_DIRECTION_NEGATIVE_Y,
		SG_WATER_DIRECTION_POSITIVE_Z,
		SG_WATER_DIRECTION_NEGATIVE_Z
	};
	const sg_configuration_semantic_region_t *region =
		&build->semantics->regions[region_index];
	uint32_t binding;

	if (region->water_level < 2U)
		return 1;
	build->output->wet_region_count++;
	for (binding = build->binding_offsets[region_index];
		binding < build->binding_offsets[region_index + 1U]; binding++)
	{
		uint32_t direction;

		for (direction = SG_WATER_DIRECTION_POSITIVE_X;
			direction <= SG_WATER_DIRECTION_NEGATIVE_Z; direction++)
		{
			sg_water_capability_fact_t fact;

			FillRegionFact(build, region_index, build->bindings[binding].phase,
				SG_WATER_CAPABILITY_DIRECTIONAL_SWIM,
				(sg_water_direction_t)direction, &fact);
			if (!ProbeLocalFact(build, region_index,
				fact.direction_vector.value, &fact))
				return 0;
		}
		{
			sg_water_capability_fact_t fact;

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
		for (direction = 0U;
			direction < sizeof(currents) / sizeof(currents[0]); direction++)
			if ((SG_HostCollisionRuneContents(region->water_type) &
				currents[direction]) != 0U)
			{
				sg_water_capability_fact_t fact;

				FillRegionFact(build, region_index,
					build->bindings[binding].phase,
					SG_WATER_CAPABILITY_CURRENT,
					current_directions[direction], &fact);
				fact.current = currents[direction];
				fact.flags |= SG_WATER_CAPABILITY_USES_CURRENT;
				memset(fact.direction_vector.value, 0,
					sizeof(fact.direction_vector.value));
				if (!ProbeLocalFact(build, region_index,
					fact.direction_vector.value, &fact))
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
	return fabsf(left_normal[0] - right_normal[0]) <= SG_WATER_PLANE_EPSILON &&
		fabsf(left_normal[1] - right_normal[1]) <= SG_WATER_PLANE_EPSILON &&
		fabsf(left_normal[2] - right_normal[2]) <= SG_WATER_PLANE_EPSILON &&
		fabsf(left_distance - right_distance) <= SG_WATER_PLANE_EPSILON;
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

		if (Dot3(witness, record->normal) > record->distance ||
			(local + region->first_face == boundary_face &&
			 Dot3(witness, record->normal) >= record->distance))
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
		halfspaces[local].open = face == boundary_face;
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
	if (Dot3(witness, boundary->normal) >= boundary->distance)
		return 0;
	for (local = 0U; local < region->face_count; local++)
	{
		const sg_configuration_semantic_face_t *record =
			&build->semantics->faces[region->first_face + local];

		if (Dot3(witness, record->normal) > record->distance)
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

		if (Dot3(point, record->normal) - record->distance >
			SG_WATER_PLANE_EPSILON)
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
		uint32_t destination_binding;

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
		for (destination_binding = build->binding_offsets[destination_region];
			destination_binding <
				build->binding_offsets[destination_region + 1U];
			destination_binding++)
		{
			fact.destination_phase = build->bindings[destination_binding].phase;
			if (!PhaseContainsVelocity(
				&build->phases[fact.destination_phase], result.velocity))
				continue;
			if (!AppendFact(build, &fact))
				return 0;
		}
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
				second->bounds.maxs.value[axis]) + SG_WATER_PLANE_EPSILON)
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
		uint32_t left;
		uint32_t right;

		while (group_end < reference_count &&
			SameSemanticSource(&references[group_start], &references[group_end]))
			group_end++;
		for (left = group_start; left < group_end; left++)
		{
			if (left != group_start &&
				SameRegionSide(&references[left - 1U], &references[left]))
				continue;
			for (right = left + 1U; right < group_end; right++)
			{
				if (right != left + 1U &&
					SameRegionSide(&references[right - 1U], &references[right]))
					continue;
				if (references[left].reversed != references[right].reversed &&
					references[left].region != references[right].region &&
					!AppendBoundaryPair(build, references[left].region,
						references[left].face, references[right].region,
						references[right].face,
						SG_WATER_CAPABILITY_INDEX_NONE))
				{
					free(references);
					return 0;
				}
			}
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
	if (output->fact_count > build->limits->max_facts)
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

void SG_WaterCapabilityDefaultLimits(
	sg_water_capability_limits_t *limits_out)
{
	if (limits_out)
		limits_out->max_facts = SG_RUNE_MODEL_MAX_KERNELS;
}

int SG_WaterCapabilityBuild(
	const sg_host_collision_authority_t *authority,
	sg_host_pmove_function_t host_pmove,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	const sg_rune_phase_basis_t *phases, uint32_t phase_count,
	const sg_water_phase_binding_t *bindings, uint32_t binding_count,
	const sg_water_capability_limits_t *limits,
	sg_water_capability_set_t **capabilities_out,
	sg_water_capability_error_t *error_out)
{
	sg_water_capability_limits_t defaults;
	sg_water_build_t build;
	uint32_t region;
	int success = 0;

	memset(&build, 0, sizeof(build));
	build.error.source_index = SG_WATER_CAPABILITY_INDEX_NONE;
	if (!limits)
	{
		SG_WaterCapabilityDefaultLimits(&defaults);
		limits = &defaults;
	}
	build.authority = authority;
	build.host_pmove = host_pmove;
	build.configuration = configuration;
	build.semantics = semantics;
	build.phases = phases;
	build.phase_count = phase_count;
	build.bindings = bindings;
	build.binding_count = binding_count;
	build.limits = limits;
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
