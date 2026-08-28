#include "sg_ground_capability.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "sg_configuration_lattice.h"

#define SG_GROUND_LEVEL_EPSILON 0.25f
#define SG_GROUND_NORMAL_FLAT 0.999f
#define SG_GROUND_COMMAND_SPEED 400

typedef struct sg_ground_build_s
{
	const sg_host_collision_authority_t *authority;
	const sg_configuration_space_t *configuration;
	const sg_configuration_semantics_t *semantics;
	const sg_rune_phase_basis_t *phases;
	const sg_ground_phase_binding_t *bindings;
	sg_host_pmove_function_t host_pmove;
	sg_ground_capability_set_t *output;
	uint32_t capacity;
	uint32_t max_capabilities;
	uint32_t *cell_phase_offsets;
	uint32_t *cell_region_offsets;
	sg_ground_capability_error_t *error;
} sg_ground_build_t;

static void SetError(sg_ground_capability_error_t *error,
	sg_ground_capability_error_code_t code, uint32_t source_index)
{
	if (!error)
		return;
	error->code = code;
	error->source_index = source_index;
}

static int Finite3(const float value[3])
{
	return value && isfinite(value[0]) && isfinite(value[1]) &&
		isfinite(value[2]);
}

static int IdentityEqual(const sg_rune_model_identity_t *left,
	const sg_rune_model_identity_t *right)
{
	return left->bsp_content_id == right->bsp_content_id &&
		left->entity_semantics_id == right->entity_semantics_id &&
		left->physics_abi_id == right->physics_abi_id &&
		left->source_set_identity == right->source_set_identity &&
		left->schema_id == right->schema_id &&
		left->producer_identity == right->producer_identity &&
		memcmp(&left->standing_hull, &right->standing_hull,
			sizeof(left->standing_hull)) == 0 &&
		memcmp(&left->crouching_hull, &right->crouching_hull,
			sizeof(left->crouching_hull)) == 0 &&
		memcmp(&left->physics, &right->physics, sizeof(left->physics)) == 0;
}

static int SpanValid(uint32_t first, uint32_t count, uint32_t total)
{
	return first <= total && count <= total - first;
}

static int PointInsideCell(const sg_configuration_space_t *configuration,
	uint32_t cell_index, const float point[3])
{
	const sg_configuration_cell_t *cell = &configuration->cells[cell_index];
	uint32_t axis;
	uint32_t local;

	for (axis = 0U; axis < 3U; axis++)
		if (point[axis] < cell->bounds.mins.value[axis] ||
			point[axis] > cell->bounds.maxs.value[axis])
			return 0;
	if (!SpanValid(cell->first_face, cell->face_count,
			configuration->face_count))
		return 0;
	for (local = 0U; local < cell->face_count; local++)
	{
		const sg_configuration_plane_t *plane =
			&configuration->faces[cell->first_face + local].plane;
		double distance = (double)point[0] * plane->normal[0] +
			(double)point[1] * plane->normal[1] +
			(double)point[2] * plane->normal[2];

		if (!isfinite(distance) || distance > (double)plane->distance)
			return 0;
	}
	return 1;
}

static int PointInsideRegion(const sg_configuration_semantics_t *semantics,
	const sg_configuration_semantic_region_t *region, const float point[3])
{
	uint32_t axis;
	uint32_t local;

	for (axis = 0U; axis < 3U; axis++)
		if (point[axis] < region->bounds.mins.value[axis] ||
			point[axis] > region->bounds.maxs.value[axis])
			return 0;
	if (!SpanValid(region->first_face, region->face_count,
			semantics->face_count))
		return 0;
	for (local = 0U; local < region->face_count; local++)
	{
		const sg_configuration_semantic_face_t *face =
			&semantics->faces[region->first_face + local];
		double distance = (double)point[0] * face->normal[0] +
			(double)point[1] * face->normal[1] +
			(double)point[2] * face->normal[2];

		if (!isfinite(distance) || distance > (double)face->distance)
			return 0;
	}
	return 1;
}

static double Dot3(const float left[3], const float right[3])
{
	return (double)left[0] * right[0] + (double)left[1] * right[1] +
		(double)left[2] * right[2];
}

static int PortalContainsCrossing(const sg_configuration_space_t *space,
	const sg_configuration_portal_t *portal, const float start[3],
	const float end[3])
{
	float delta[3];
	float point[3];
	double denominator;
	double parameter;
	int orientation = 0;
	uint32_t vertex;
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		delta[axis] = end[axis] - start[axis];
	denominator = Dot3(delta, portal->plane.normal);
	if (denominator == 0.0 || !isfinite(denominator))
		return 0;
	parameter = ((double)portal->plane.distance -
		Dot3(start, portal->plane.normal)) / denominator;
	if (!isfinite(parameter) || parameter < 0.0 || parameter > 1.0)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		point[axis] = (float)((double)start[axis] + parameter * delta[axis]);
	for (vertex = 0U; vertex < portal->vertex_count; vertex++)
	{
		const float *first = space->vertices[
			portal->first_vertex + vertex].value;
		const float *second = space->vertices[portal->first_vertex +
			(vertex + 1U) % portal->vertex_count].value;
		float edge[3] = { second[0] - first[0], second[1] - first[1],
			second[2] - first[2] };
		float offset[3] = { point[0] - first[0], point[1] - first[1],
			point[2] - first[2] };
		float cross[3] = {
			edge[1] * offset[2] - edge[2] * offset[1],
			edge[2] * offset[0] - edge[0] * offset[2],
			edge[0] * offset[1] - edge[1] * offset[0]
		};
		double side = Dot3(cross, portal->plane.normal);
		int sign = side < 0.0 ? -1 : side > 0.0 ? 1 : 0;

		if (sign != 0 && orientation != 0 && sign != orientation)
			return 0;
		if (sign != 0)
			orientation = sign;
	}
	return orientation != 0;
}

static int SourcesValid(const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics)
{
	uint32_t cell;
	uint32_t portal;
	uint32_t region;
	uint32_t face;
	uint32_t overlap;

	if (!authority || !authority->world || !configuration || !semantics ||
		!IdentityEqual(&authority->identity, &configuration->identity) ||
		!IdentityEqual(&authority->identity, &semantics->identity) ||
		configuration->cell_count == 0U || !configuration->cells ||
		(configuration->face_count != 0U && !configuration->faces) ||
		(configuration->portal_count != 0U &&
		 (!configuration->portals || !configuration->vertices)) ||
		(configuration->stance_overlap_count != 0U &&
		 !configuration->stance_overlaps) ||
		semantics->region_count == 0U || !semantics->regions ||
		(semantics->face_count != 0U && !semantics->faces))
		return 0;
	for (cell = 0U; cell < configuration->cell_count; cell++)
	{
		const sg_configuration_cell_t *record = &configuration->cells[cell];
		sg_rune_stable_id_t expected =
			SG_RuneModelStableIdFromOrderKey(&record->order);

		if (record->stance < SG_RUNE_STANCE_STANDING ||
			record->stance >= SG_RUNE_STANCE_COUNT ||
			!SG_RuneModelOrderKeyValid(&record->order) ||
			record->order.domain != SG_RUNE_ORDER_CELL ||
			record->order.source_set_identity !=
				authority->identity.source_set_identity ||
			!SG_RuneModelStableIdEqual(&record->id.value,
				&expected) ||
			(cell != 0U && SG_RuneModelOrderKeyCompare(
				&configuration->cells[cell - 1U].order,
				&record->order) >= 0) ||
			record->bsp_leaf.index == SG_GROUND_CAPABILITY_INDEX_NONE ||
			record->bsp_area.index == SG_GROUND_CAPABILITY_INDEX_NONE ||
			!Finite3(record->bounds.mins.value) ||
			!Finite3(record->bounds.maxs.value) ||
			record->bounds.mins.value[0] > record->bounds.maxs.value[0] ||
			record->bounds.mins.value[1] > record->bounds.maxs.value[1] ||
			record->bounds.mins.value[2] > record->bounds.maxs.value[2] ||
			!Finite3(record->interior_witness.value) ||
			!SpanValid(record->first_face, record->face_count,
				configuration->face_count))
			return 0;
	}
	for (face = 0U; face < configuration->face_count; face++)
		if (!Finite3(configuration->faces[face].plane.normal) ||
			!isfinite(configuration->faces[face].plane.distance))
			return 0;
	for (portal = 0U; portal < configuration->portal_count; portal++)
	{
		const sg_configuration_portal_t *record =
			&configuration->portals[portal];
		sg_rune_stable_id_t expected =
			SG_RuneModelStableIdFromOrderKey(&record->order);

		if (record->from_cell >= configuration->cell_count ||
			record->to_cell >= configuration->cell_count ||
			record->from_cell == record->to_cell ||
			record->stance < SG_RUNE_STANCE_STANDING ||
			record->stance >= SG_RUNE_STANCE_COUNT ||
			!SG_RuneModelOrderKeyValid(&record->order) ||
			record->order.domain != SG_RUNE_ORDER_PORTAL ||
			record->order.source_set_identity !=
				authority->identity.source_set_identity ||
			!SG_RuneModelStableIdEqual(&record->id.value, &expected) ||
			(portal != 0U && SG_RuneModelOrderKeyCompare(
				&configuration->portals[portal - 1U].order,
				&record->order) >= 0) ||
			configuration->cells[record->from_cell].stance != record->stance ||
			configuration->cells[record->to_cell].stance != record->stance ||
			record->vertex_count < 3U ||
			record->vertex_count >
				SG_RUNE_MODEL_MAX_PORTAL_VERTICES_PER_PORTAL ||
			!SpanValid(record->first_vertex, record->vertex_count,
				configuration->vertex_count) ||
			!Finite3(record->plane.normal) ||
			!isfinite(record->plane.distance) ||
			!isfinite(record->clearance) || record->clearance <= 0.0f)
			return 0;
		{
			uint32_t vertex;

			for (vertex = 0U; vertex < record->vertex_count; vertex++)
				if (!Finite3(configuration->vertices[
					record->first_vertex + vertex].value))
					return 0;
		}
	}
	for (overlap = 0U; overlap < configuration->stance_overlap_count; overlap++)
	{
		const sg_configuration_stance_overlap_t *record =
			&configuration->stance_overlaps[overlap];

		if (record->standing_cell >= configuration->cell_count ||
			record->crouching_cell >= configuration->cell_count ||
			configuration->cells[record->standing_cell].stance !=
				SG_RUNE_STANCE_STANDING ||
			configuration->cells[record->crouching_cell].stance !=
				SG_RUNE_STANCE_CROUCHING ||
			!Finite3(record->interior_witness.value) ||
			!PointInsideCell(configuration, record->standing_cell,
				record->interior_witness.value) ||
			!PointInsideCell(configuration, record->crouching_cell,
				record->interior_witness.value))
			return 0;
	}
	for (face = 0U; face < semantics->face_count; face++)
		if (!Finite3(semantics->faces[face].normal) ||
			!isfinite(semantics->faces[face].distance))
			return 0;
	for (region = 0U; region < semantics->region_count; region++)
	{
		const sg_configuration_semantic_region_t *record =
			&semantics->regions[region];

		if (record->cell >= configuration->cell_count ||
			(region != 0U && semantics->regions[region - 1U].cell >
				record->cell) ||
			(region != 0U && semantics->regions[region - 1U].id >= record->id) ||
			!Finite3(record->bounds.mins.value) ||
			!Finite3(record->bounds.maxs.value) ||
			record->bounds.mins.value[0] > record->bounds.maxs.value[0] ||
			record->bounds.mins.value[1] > record->bounds.maxs.value[1] ||
			record->bounds.mins.value[2] > record->bounds.maxs.value[2] ||
			!Finite3(record->interior_witness.value) ||
			!SpanValid(record->first_face, record->face_count,
				semantics->face_count))
			return 0;
	}
	return isfinite(authority->identity.physics.gravity) &&
		authority->identity.physics.gravity >= 0.0f &&
		authority->identity.physics.gravity <= (float)SHRT_MAX &&
		truncf(authority->identity.physics.gravity) ==
			authority->identity.physics.gravity &&
		isfinite(authority->identity.physics.ground_acceleration) &&
		authority->identity.physics.ground_acceleration >= 0.0f &&
		isfinite(authority->identity.physics.max_velocity) &&
		authority->identity.physics.max_velocity > 0.0f &&
		authority->identity.physics.frame_ms != 0U &&
		authority->identity.physics.substep_ms != 0U &&
		authority->identity.physics.substep_ms <= UCHAR_MAX &&
		authority->identity.physics.frame_ms %
			authority->identity.physics.substep_ms == 0U;
}

static int BuildOffsets(sg_ground_build_t *build, size_t phase_count,
	size_t binding_count)
{
	uint32_t cell;
	uint32_t region = 0U;
	size_t binding = 0U;
	uint8_t *phase_seen;

	if (phase_count == 0U || phase_count > UINT32_MAX ||
		binding_count > UINT32_MAX)
		return 0;
	build->cell_phase_offsets = calloc(
		(size_t)build->configuration->cell_count + 1U,
		sizeof(*build->cell_phase_offsets));
	build->cell_region_offsets = calloc(
		(size_t)build->configuration->cell_count + 1U,
		sizeof(*build->cell_region_offsets));
	if (!build->cell_phase_offsets || !build->cell_region_offsets)
		return -1;
	phase_seen = calloc(phase_count, sizeof(*phase_seen));
	if (!phase_seen)
		return -1;
	for (cell = 0U; cell < build->configuration->cell_count; cell++)
	{
		uint32_t previous_phase = 0U;
		int have_previous = 0;

		build->cell_phase_offsets[cell] = (uint32_t)binding;
		while (binding < binding_count &&
			build->bindings[binding].cell == cell)
		{
			const sg_ground_phase_binding_t *record =
				&build->bindings[binding];
			const sg_rune_phase_basis_t *phase;

			if (record->phase >= phase_count || phase_seen[record->phase] ||
				(have_previous && record->phase <= previous_phase))
			{
				free(phase_seen);
				return 0;
			}
			phase = &build->phases[record->phase];
			if (!SG_RuneModelPhaseValid(phase) ||
				phase->order.source_set_identity !=
					build->authority->identity.source_set_identity ||
				phase->stance != build->configuration->cells[cell].stance)
			{
				free(phase_seen);
				return 0;
			}
			phase_seen[record->phase] = 1U;
			previous_phase = record->phase;
			have_previous = 1;
			binding++;
		}
		build->cell_region_offsets[cell] = region;
		while (region < build->semantics->region_count &&
			build->semantics->regions[region].cell == cell)
			region++;
		if (build->cell_region_offsets[cell] == region)
		{
			free(phase_seen);
			return 0;
		}
	}
	free(phase_seen);
	build->cell_phase_offsets[build->configuration->cell_count] =
		(uint32_t)binding_count;
	build->cell_region_offsets[build->configuration->cell_count] = region;
	if (binding != binding_count || region != build->semantics->region_count)
		return 0;
	return 1;
}

static int RegionPose(sg_ground_build_t *build, uint32_t region,
	sg_host_collision_pose_t *pose_out)
{
	const sg_configuration_semantic_region_t *record =
		&build->semantics->regions[region];
	const sg_configuration_cell_t *cell =
		&build->configuration->cells[record->cell];

	if (!SG_HostCollisionClassifyPose(build->authority, NULL,
		record->interior_witness.value, cell->stance, pose_out))
	{
		SetError(build->error, SG_GROUND_CAPABILITY_ERROR_HOST_DISAGREEMENT,
			region);
		return -1;
	}
	return pose_out->valid ? 1 : 0;
}

static int PhaseMatchesRegionPose(const sg_rune_phase_basis_t *phase,
	const sg_configuration_semantic_region_t *region,
	const sg_host_collision_pose_t *pose)
{
	sg_rune_medium_t medium = SG_RUNE_MEDIUM_DRY;
	int region_supported = (region->flags &
		SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED) != 0U;
	int region_airborne = (region->flags &
		SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE) != 0U;

	if (region->flags & SG_CONFIGURATION_SEMANTIC_REGION_LAVA)
		medium = SG_RUNE_MEDIUM_LAVA;
	else if (region->flags & SG_CONFIGURATION_SEMANTIC_REGION_SLIME)
		medium = SG_RUNE_MEDIUM_SLIME;
	else if (region->flags & SG_CONFIGURATION_SEMANTIC_REGION_WATER)
		medium = SG_RUNE_MEDIUM_WATER;
	if (phase->stance != pose->stance || phase->medium != medium ||
		phase->reference_frame != SG_RUNE_FRAME_WORLD ||
		region->water_level != pose->water_level ||
		region->water_type != pose->water_type ||
		region_supported != pose->supported ||
		(!pose->supported && !region_airborne) ||
		(medium != SG_RUNE_MEDIUM_DRY &&
			(!pose->supported || region->water_level != 1U)) ||
		phase->void_relation !=
			((region->flags & SG_CONFIGURATION_SEMANTIC_REGION_VOID_ADJACENT) ?
				SG_RUNE_VOID_ADJACENT : SG_RUNE_VOID_CLEAR))
		return 0;
	if (pose->supported)
		return phase->motion == SG_RUNE_MOTION_SUPPORTED &&
			phase->support == SG_RUNE_SUPPORT_SUPPORTED;
	return phase->motion == SG_RUNE_MOTION_AIRBORNE &&
		phase->support == SG_RUNE_SUPPORT_NONE;
}

static int CompareCapability(const void *left_pointer,
	const void *right_pointer);
static sg_rune_stance_t ResultStance(const sg_host_pmove_result_t *result);

static int PhaseContainsVelocity(const sg_rune_phase_basis_t *phase,
	const float velocity[3])
{
	const sg_rune_interval_t *intervals[3] = {
		&phase->velocity.x, &phase->velocity.y, &phase->velocity.z
	};
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		if (!isfinite(velocity[axis]) ||
			velocity[axis] < intervals[axis]->min_value ||
			velocity[axis] > intervals[axis]->max_value)
			return 0;
	return 1;
}

static int PhaseVelocitySample(const sg_rune_phase_basis_t *phase,
	int most_negative_z, float velocity[3])
{
	const sg_rune_interval_t *intervals[3] = {
		&phase->velocity.x, &phase->velocity.y, &phase->velocity.z
	};
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
	{
		double minimum = ceil((double)intervals[axis]->min_value * 8.0) /
			8.0;
		double maximum = floor((double)intervals[axis]->max_value * 8.0) /
			8.0;
		double sample;

		if (!isfinite(minimum) || !isfinite(maximum) || minimum > maximum)
			return 0;
		if (axis == 2U && most_negative_z)
			sample = minimum;
		else if (minimum > 0.0)
			sample = minimum;
		else if (maximum < 0.0)
			sample = maximum;
		else
			sample = 0.0;
		if (sample < (double)SHRT_MIN / 8.0 ||
			sample > (double)SHRT_MAX / 8.0)
			return 0;
		velocity[axis] = (float)sample;
	}
	return PhaseContainsVelocity(phase, velocity);
}

static int StateFromOrigin(const float origin[3], sg_rune_stance_t stance,
	float gravity, pmove_state_t *state_out)
{
	uint32_t axis;
	double gravity_fixed;

	memset(state_out, 0, sizeof(*state_out));
	state_out->pm_type = PM_NORMAL;
	gravity_fixed = nearbyint((double)gravity);
	if (!isfinite(gravity_fixed) || gravity_fixed < 0.0 ||
		gravity_fixed > (double)SHRT_MAX || gravity_fixed != (double)gravity)
		return 0;
	state_out->gravity = (short)gravity_fixed;
	if (stance == SG_RUNE_STANCE_CROUCHING)
		state_out->pm_flags = PMF_DUCKED;
	for (axis = 0U; axis < 3U; axis++)
	{
		double fixed = nearbyint((double)origin[axis] * 8.0);

		if (!isfinite(fixed) || fixed < (double)SHRT_MIN ||
			fixed > (double)SHRT_MAX || fixed / 8.0 != (double)origin[axis])
			return 0;
		state_out->origin[axis] = (short)fixed;
	}
	return 1;
}

static int EvaluateFrame(sg_ground_build_t *build, const float start[3],
	const float target[3], sg_rune_stance_t stance, int move, int jump, int crouch,
	const float initial_velocity[3], sg_host_pmove_result_t *result_out)
{
	sg_host_pmove_request_t request;
	sg_host_pmove_error_t error;
	float yaw;
	double yaw_short;

	memset(&request, 0, sizeof(request));
	if (!StateFromOrigin(start, stance,
			build->authority->identity.physics.gravity, &request.state))
	{
		SetError(build->error, SG_GROUND_CAPABILITY_ERROR_HOST_DISAGREEMENT,
			SG_GROUND_CAPABILITY_INDEX_NONE);
		return 0;
	}
	request.previous_state = request.state;
	for (uint32_t axis = 0U; axis < 3U; axis++)
	{
		double fixed = nearbyint((double)initial_velocity[axis] * 8.0);

		if (!isfinite(fixed) || fixed < (double)SHRT_MIN ||
			fixed > (double)SHRT_MAX ||
			fixed / 8.0 != (double)initial_velocity[axis])
		{
			SetError(build->error, SG_GROUND_CAPABILITY_ERROR_INVALID_PHASE,
				SG_GROUND_CAPABILITY_INDEX_NONE);
			return 0;
		}
		request.state.velocity[axis] = (short)fixed;
		request.previous_state.velocity[axis] = (short)fixed;
	}
	yaw = atan2f(target[1] - start[1], target[0] - start[0]);
	yaw_short = nearbyint((double)yaw * 65536.0 /
		(2.0 * 3.14159265358979323846));
	if (yaw_short < (double)SHRT_MIN)
		yaw_short += 65536.0;
	if (yaw_short > (double)SHRT_MAX)
		yaw_short -= 65536.0;
	request.command.angles[YAW] = (short)yaw_short;
	request.command.forwardmove = move ? SG_GROUND_COMMAND_SPEED : 0;
	request.command.upmove = jump ? SG_GROUND_COMMAND_SPEED :
		(crouch ? -SG_GROUND_COMMAND_SPEED : 0);
	if (!SG_HostPmoveEvaluateFrame(build->authority, NULL, build->host_pmove,
			&request, result_out, &error))
	{
		SetError(build->error, SG_GROUND_CAPABILITY_ERROR_HOST_DISAGREEMENT,
			SG_GROUND_CAPABILITY_INDEX_NONE);
		return 0;
	}
	build->output->pmove_frames++;
	if (error != SG_HOST_PMOVE_ERROR_NONE ||
		result_out->physics_abi_id != build->authority->identity.physics_abi_id ||
		result_out->gravity != build->authority->identity.physics.gravity)
	{
		SetError(build->error, SG_GROUND_CAPABILITY_ERROR_HOST_DISAGREEMENT,
			SG_GROUND_CAPABILITY_INDEX_NONE);
		return 0;
	}
	return
		1;
}

static sg_ground_capability_kind_t CrossingKind(
	const sg_configuration_cell_t *cell,
	const sg_host_collision_pose_t *source,
	const sg_host_collision_pose_t *destination,
	const float start[3], const float end[3])
{
	if (source->supported && destination->supported)
	{
		if (cell->stance == SG_RUNE_STANCE_CROUCHING)
			return SG_GROUND_CAPABILITY_CROUCH;
		if (source->support.plane.normal[2] < SG_GROUND_NORMAL_FLAT ||
			destination->support.plane.normal[2] < SG_GROUND_NORMAL_FLAT)
			return SG_GROUND_CAPABILITY_RAMP;
		if (fabsf(end[2] - start[2]) > SG_GROUND_LEVEL_EPSILON)
			return SG_GROUND_CAPABILITY_STEP;
		return SG_GROUND_CAPABILITY_WALK;
	}
	if (source->supported && !destination->supported &&
		end[2] <= start[2])
		return SG_GROUND_CAPABILITY_DROP;
	if (!source->supported && destination->supported)
		return SG_GROUND_CAPABILITY_LANDING;
	return SG_GROUND_CAPABILITY_KIND_COUNT;
}

static int EvaluateContinuouslySupported(sg_ground_build_t *build,
	const float start[3], const float target[3], sg_rune_stance_t stance,
	int crouch, const float initial_velocity[3],
	const sg_host_pmove_result_t *expected)
{
	sg_host_collision_authority_t substep_authority = *build->authority;
	sg_host_pmove_request_t request;
	sg_host_pmove_result_t result;
	sg_host_pmove_error_t error;
	float yaw;
	double yaw_short;
	uint32_t steps = build->authority->identity.physics.frame_ms /
		build->authority->identity.physics.substep_ms;
	uint32_t axis;
	uint32_t step;

	memset(&request, 0, sizeof(request));
	if (!StateFromOrigin(start, stance,
			build->authority->identity.physics.gravity, &request.state))
	{
		SetError(build->error, SG_GROUND_CAPABILITY_ERROR_HOST_DISAGREEMENT,
			SG_GROUND_CAPABILITY_INDEX_NONE);
		return -1;
	}
	request.previous_state = request.state;
	for (axis = 0U; axis < 3U; axis++)
	{
		double fixed = nearbyint((double)initial_velocity[axis] * 8.0);

		if (!isfinite(fixed) || fixed < (double)SHRT_MIN ||
			fixed > (double)SHRT_MAX ||
			fixed / 8.0 != (double)initial_velocity[axis])
			return -1;
		request.state.velocity[axis] = (short)fixed;
		request.previous_state.velocity[axis] = (short)fixed;
	}
	yaw = atan2f(target[1] - start[1], target[0] - start[0]);
	yaw_short = nearbyint((double)yaw * 65536.0 /
		(2.0 * 3.14159265358979323846));
	if (yaw_short < (double)SHRT_MIN)
		yaw_short += 65536.0;
	if (yaw_short > (double)SHRT_MAX)
		yaw_short -= 65536.0;
	request.command.angles[YAW] = (short)yaw_short;
	request.command.forwardmove = SG_GROUND_COMMAND_SPEED;
	request.command.upmove = crouch ? -SG_GROUND_COMMAND_SPEED : 0;
	substep_authority.identity.physics.frame_ms =
		build->authority->identity.physics.substep_ms;
	for (step = 0U; step < steps; step++)
	{
		sg_host_collision_pose_t pose;
		sg_rune_stance_t actual_stance;

		if (!SG_HostPmoveEvaluateFrame(&substep_authority, NULL,
				build->host_pmove, &request, &result, &error))
		{
			SetError(build->error,
				SG_GROUND_CAPABILITY_ERROR_HOST_DISAGREEMENT,
				SG_GROUND_CAPABILITY_INDEX_NONE);
			return -1;
		}
		build->output->pmove_frames++;
		actual_stance = ResultStance(&result);
		if (!SG_HostCollisionClassifyPose(build->authority, NULL,
				result.origin, actual_stance, &pose))
		{
			SetError(build->error,
				SG_GROUND_CAPABILITY_ERROR_HOST_DISAGREEMENT,
				SG_GROUND_CAPABILITY_INDEX_NONE);
			return -1;
		}
		if (!pose.valid || !pose.supported)
			return 0;
		request.state = result.state;
		request.previous_state = result.state;
	}
	if (memcmp(&result.state, &expected->state, sizeof(result.state)) != 0)
	{
		SetError(build->error, SG_GROUND_CAPABILITY_ERROR_HOST_DISAGREEMENT,
			SG_GROUND_CAPABILITY_INDEX_NONE);
		return -1;
	}
	return 1;
}

static int GeometricallyContinuouslySupported(sg_ground_build_t *build,
	const float start[3], const float end[3], sg_rune_stance_t stance)
{
	double maximum = 0.0;
	uint32_t axis;
	uint32_t steps;
	uint32_t step;

	for (axis = 0U; axis < 3U; axis++)
		maximum = fmax(maximum, fabs((double)end[axis] - start[axis]));
	if (!isfinite(maximum) || maximum > (double)UINT32_MAX / 8.0)
		return 0;
	steps = (uint32_t)ceil(maximum * 8.0);
	if (steps == 0U)
		steps = 1U;
	for (step = 0U; step <= steps; step++)
	{
		float point[3];
		sg_host_collision_pose_t pose;
		double fraction = (double)step / (double)steps;

		for (axis = 0U; axis < 3U; axis++)
			point[axis] = (float)((double)start[axis] +
				((double)end[axis] - start[axis]) * fraction);
		if (!SG_HostCollisionClassifyPose(build->authority, NULL, point,
				stance, &pose))
		{
			SetError(build->error,
				SG_GROUND_CAPABILITY_ERROR_HOST_DISAGREEMENT,
				SG_GROUND_CAPABILITY_INDEX_NONE);
			return -1;
		}
		if (!pose.valid || !pose.supported)
			return 0;
	}
	return 1;
}

static int GeometricallyStepSupported(sg_ground_build_t *build,
	const float start[3], const float end[3], sg_rune_stance_t stance)
{
	double horizontal = fmax(fabs((double)end[0] - start[0]),
		fabs((double)end[1] - start[1]));
	uint32_t steps;
	uint32_t step;
	float minimum_z = fminf(start[2], end[2]);
	float maximum_z = fmaxf(start[2], end[2]);

	if (!isfinite(horizontal) || horizontal > (double)UINT32_MAX / 8.0)
		return 0;
	steps = (uint32_t)ceil(horizontal * 8.0);
	if (steps == 0U)
		steps = 1U;
	for (step = 0U; step <= steps; step++)
	{
		double fraction = (double)step / (double)steps;
		int32_t minimum_q8 = (int32_t)floorf(minimum_z * 8.0f);
		int32_t maximum_q8 = (int32_t)ceilf(maximum_z * 8.0f);
		int32_t z;
		int supported = 0;

		for (z = minimum_q8; z <= maximum_q8; z++)
		{
			float point[3];
			sg_host_collision_pose_t pose;

			point[0] = (float)((double)start[0] +
				((double)end[0] - start[0]) * fraction);
			point[1] = (float)((double)start[1] +
				((double)end[1] - start[1]) * fraction);
			point[2] = (float)z * 0.125f;
			if (!SG_HostCollisionClassifyPose(build->authority, NULL, point,
					stance, &pose))
			{
				SetError(build->error,
					SG_GROUND_CAPABILITY_ERROR_HOST_DISAGREEMENT,
					SG_GROUND_CAPABILITY_INDEX_NONE);
				return -1;
			}
			if (pose.valid && pose.supported)
			{
				supported = 1;
				break;
			}
		}
		if (!supported)
			return 0;
	}
	return 1;
}

static void SetInterval(sg_rune_interval_t *interval, float value)
{
	interval->min_value = value;
	interval->max_value = value;
}

static int AppendCapability(sg_ground_build_t *build,
	const sg_ground_capability_t *capability)
{
	sg_ground_capability_t *grown;
	uint32_t next;
	uint32_t low = 0U;
	uint32_t high = build->output->capability_count;

	while (low < high)
	{
		uint32_t middle = low + (high - low) / 2U;
		int order = CompareCapability(capability,
			&build->output->capabilities[middle]);

		if (order == 0)
			return 1;
		if (order < 0)
			high = middle;
		else
			low = middle + 1U;
	}

	if (build->output->capability_count == UINT32_MAX)
	{
		SetError(build->error, SG_GROUND_CAPABILITY_ERROR_OVERFLOW,
			build->output->capability_count);
		return 0;
	}
	next = build->output->capability_count + 1U;
	if (next > build->capacity)
	{
		uint32_t capacity = build->capacity ? build->capacity : 16U;

		while (capacity < next)
		{
			if (capacity > UINT32_MAX / 2U)
			{
				capacity = UINT32_MAX;
				break;
			}
			capacity *= 2U;
		}
		if (capacity != 0U &&
			((size_t)capacity * sizeof(*grown)) / sizeof(*grown) !=
				(size_t)capacity)
		{
			SetError(build->error, SG_GROUND_CAPABILITY_ERROR_OVERFLOW,
				build->output->capability_count);
			return 0;
		}
		grown = realloc(build->output->capabilities,
			(size_t)capacity * sizeof(*grown));
		if (!grown)
		{
			SetError(build->error, SG_GROUND_CAPABILITY_ERROR_OUT_OF_MEMORY,
				build->output->capability_count);
			return 0;
		}
		build->output->capabilities = grown;
		build->capacity = capacity;
	}
	if (low < build->output->capability_count)
		memmove(&build->output->capabilities[low + 1U],
			&build->output->capabilities[low],
			(size_t)(build->output->capability_count - low) * sizeof(*capability));
	build->output->capabilities[low] = *capability;
	build->output->capability_count++;
	return 1;
}

static void FillCapability(const sg_ground_build_t *build,
	sg_ground_capability_t *capability, sg_ground_capability_kind_t kind,
	uint32_t source_cell, uint32_t destination_cell, uint32_t source_region,
	uint32_t destination_region, uint32_t portal, uint32_t source_phase,
	uint32_t destination_phase, const sg_host_collision_pose_t *source_pose,
	const float initial_velocity[3], const float observed_velocity[3],
	const float start[3], const float end[3])
{
	uint32_t axis;

	memset(capability, 0, sizeof(*capability));
	capability->source_cell = source_cell;
	capability->destination_cell = destination_cell;
	capability->source_region = source_region;
	capability->destination_region = destination_region;
	capability->portal = portal;
	capability->source_phase = source_phase;
	capability->destination_phase = destination_phase;
	capability->kind = kind;
	for (axis = 0U; axis < 3U; axis++)
	{
		float delta = end[axis] - start[axis];

		capability->source_witness.value[axis] = start[axis];
		capability->destination_witness.value[axis] = end[axis];
		capability->initial_velocity.value[axis] = initial_velocity[axis];
		capability->observed_velocity.value[axis] = observed_velocity[axis];
		if (axis == 0U)
			SetInterval(&capability->displacement.x, delta);
		else if (axis == 1U)
			SetInterval(&capability->displacement.y, delta);
		else
			SetInterval(&capability->displacement.z, delta);
	}
	SetInterval(&capability->duration_ms,
		(float)build->authority->identity.physics.frame_ms);
	capability->acceleration =
		build->authority->identity.physics.ground_acceleration;
	capability->gravity = build->authority->identity.physics.gravity;
	capability->physics_abi_id = build->authority->identity.physics_abi_id;
	capability->flags = SG_GROUND_CAPABILITY_DIRECTIONAL |
		SG_GROUND_CAPABILITY_PROVEN;
	if (source_pose->supported)
		capability->flags |= SG_GROUND_CAPABILITY_REQUIRES_SUPPORT;
	if (kind == SG_GROUND_CAPABILITY_STANCE)
		capability->flags |= SG_GROUND_CAPABILITY_CHANGES_STANCE;
	if (kind == SG_GROUND_CAPABILITY_JUMP_TAKEOFF ||
		kind == SG_GROUND_CAPABILITY_LANDING ||
		kind == SG_GROUND_CAPABILITY_DROP)
		capability->flags |= SG_GROUND_CAPABILITY_CHANGES_SUPPORT;
	if ((build->semantics->regions[source_region].flags |
		build->semantics->regions[destination_region].flags) &
		SG_CONFIGURATION_SEMANTIC_REGION_VOID_ADJACENT)
		capability->flags |= SG_GROUND_CAPABILITY_VOID_ADJACENT;
}

static int EmitForObservation(sg_ground_build_t *build,
	sg_ground_capability_kind_t kind, uint32_t source_cell,
	uint32_t destination_cell, uint32_t source_region,
	uint32_t destination_region, uint32_t portal, uint32_t source_phase,
	const sg_host_collision_pose_t *source_pose,
	const sg_host_collision_pose_t *destination_pose,
	const float initial_velocity[3],
	const float destination_velocity[3],
	const float start[3], const float end[3])
{
	uint32_t destination_binding;
	uint32_t before = build->output->capability_count;

	for (destination_binding = build->cell_phase_offsets[destination_cell];
		destination_binding < build->cell_phase_offsets[destination_cell + 1U];
		destination_binding++)
	{
		uint32_t destination_phase =
			build->bindings[destination_binding].phase;
		sg_ground_capability_t capability;

		if (!PhaseMatchesRegionPose(&build->phases[destination_phase],
				&build->semantics->regions[destination_region], destination_pose) ||
			!PhaseContainsVelocity(&build->phases[destination_phase],
				destination_velocity))
			continue;
		FillCapability(build, &capability, kind, source_cell,
			destination_cell, source_region, destination_region, portal,
			source_phase, destination_phase, source_pose, initial_velocity,
			destination_velocity, start, end);
		if (!AppendCapability(build, &capability))
			return -1;
	}
	return build->output->capability_count != before;
}

static int AddHalfspace(sg_configuration_lattice_halfspace_t *halfspaces,
	uint8_t *clearance, uint32_t capacity, uint32_t *count,
	float x, float y, float z, float distance, int requires_clearance)
{
	sg_configuration_lattice_halfspace_t *record;

	if (*count >= capacity || !isfinite(x) || !isfinite(y) || !isfinite(z) ||
		!isfinite(distance))
		return 0;
	record = &halfspaces[*count];
	record->normal[0] = x;
	record->normal[1] = y;
	record->normal[2] = z;
	record->distance = distance;
	record->open = 0;
	clearance[*count] = requires_clearance ? 1U : 0U;
	(*count)++;
	return 1;
}

static int PlanesCoplanar(const float left_normal[3], float left_distance,
	const float right_normal[3], float right_distance)
{
	double left_length = sqrt(Dot3(left_normal, left_normal));
	double right_length = sqrt(Dot3(right_normal, right_normal));
	double dot;
	double sign;

	if (!(left_length > 0.0) || !(right_length > 0.0) ||
		!isfinite(left_length) || !isfinite(right_length))
		return 0;
	dot = Dot3(left_normal, right_normal) / (left_length * right_length);
	if (fabs(fabs(dot) - 1.0) > 1.0e-5)
		return 0;
	sign = dot < 0.0 ? -1.0 : 1.0;
	return fabs((double)left_distance / left_length -
		sign * (double)right_distance / right_length) <= 1.0e-4;
}

static int PortalRegionWitness(const sg_ground_build_t *build,
	const sg_configuration_portal_t *portal, uint32_t region_index,
	float witness[3])
{
	const sg_configuration_semantic_region_t *region =
		&build->semantics->regions[region_index];
	uint64_t requested = (uint64_t)region->face_count +
		(uint64_t)portal->vertex_count + 8U;
	sg_configuration_lattice_halfspace_t *halfspaces;
	uint8_t *clearance;
	float center[3] = { 0.0f, 0.0f, 0.0f };
	float objective[3];
	int32_t point[3];
	sg_configuration_lattice_stats_t stats = { 0 };
	uint32_t capacity;
	uint32_t count = 0U;
	uint32_t face;
	uint32_t vertex;
	uint32_t axis;
	int positive_margin = 0;
	int feasible;
	int solved;

	if (requested > UINT32_MAX || requested > SIZE_MAX / sizeof(*halfspaces))
		return -3;
	capacity = (uint32_t)requested;
	halfspaces = calloc(capacity, sizeof(*halfspaces));
	clearance = calloc(capacity, sizeof(*clearance));
	if (!halfspaces || !clearance)
	{
		free(halfspaces);
		free(clearance);
		return -2;
	}
	for (face = 0U; face < region->face_count; face++)
	{
		const sg_configuration_semantic_face_t *record =
			&build->semantics->faces[region->first_face + face];

		if (!AddHalfspace(halfspaces, clearance, capacity, &count,
			record->normal[0], record->normal[1], record->normal[2],
			record->distance, !PlanesCoplanar(record->normal, record->distance,
				portal->plane.normal, portal->plane.distance)))
			goto invalid;
	}
	for (axis = 0U; axis < 3U; axis++)
	{
		float positive[3] = { 0.0f, 0.0f, 0.0f };
		float negative[3] = { 0.0f, 0.0f, 0.0f };

		positive[axis] = 1.0f;
		negative[axis] = -1.0f;
		if (!AddHalfspace(halfspaces, clearance, capacity, &count,
			positive[0], positive[1], positive[2],
			region->bounds.maxs.value[axis],
			!PlanesCoplanar(positive, region->bounds.maxs.value[axis],
				portal->plane.normal, portal->plane.distance)) ||
			!AddHalfspace(halfspaces, clearance, capacity, &count,
			negative[0], negative[1], negative[2],
			-region->bounds.mins.value[axis],
			!PlanesCoplanar(negative, -region->bounds.mins.value[axis],
				portal->plane.normal, portal->plane.distance)))
			goto invalid;
	}
	if (!AddHalfspace(halfspaces, clearance, capacity, &count,
		portal->plane.normal[0], portal->plane.normal[1],
		portal->plane.normal[2], portal->plane.distance, 0) ||
		!AddHalfspace(halfspaces, clearance, capacity, &count,
		-portal->plane.normal[0], -portal->plane.normal[1],
		-portal->plane.normal[2], -portal->plane.distance, 0))
		goto invalid;
	for (vertex = 0U; vertex < portal->vertex_count; vertex++)
		for (axis = 0U; axis < 3U; axis++)
			center[axis] += build->configuration->vertices[
				portal->first_vertex + vertex].value[axis] /
				(float)portal->vertex_count;
	for (vertex = 0U; vertex < portal->vertex_count; vertex++)
	{
		const float *a = build->configuration->vertices[
			portal->first_vertex + vertex].value;
		const float *b = build->configuration->vertices[portal->first_vertex +
			(vertex + 1U) % portal->vertex_count].value;
		float direction[3];
		float normal[3];
		float distance;
		double scale;

		for (axis = 0U; axis < 3U; axis++)
			direction[axis] = b[axis] - a[axis];
		normal[0] = direction[1] * portal->plane.normal[2] -
			direction[2] * portal->plane.normal[1];
		normal[1] = direction[2] * portal->plane.normal[0] -
			direction[0] * portal->plane.normal[2];
		normal[2] = direction[0] * portal->plane.normal[1] -
			direction[1] * portal->plane.normal[0];
		scale = fmax(fabs((double)normal[0]), fmax(fabs((double)normal[1]),
			fabs((double)normal[2])));
		if (!(scale > 0.0) || !isfinite(scale))
			continue;
		for (axis = 0U; axis < 3U; axis++)
			normal[axis] = (float)((double)normal[axis] / scale);
		distance = (float)Dot3(normal, a);
		if (Dot3(normal, center) > (double)distance)
		{
			for (axis = 0U; axis < 3U; axis++)
				normal[axis] = -normal[axis];
			distance = -distance;
		}
		if (!AddHalfspace(halfspaces, clearance, capacity, &count,
			normal[0], normal[1], normal[2], distance, 1))
			goto invalid;
	}
	for (axis = 0U; axis < 3U; axis++)
		objective[axis] = region->interior_witness.value[axis] - center[axis];
	feasible = SG_ConfigurationLatticeFind(halfspaces, count, NULL, point,
		&stats);
	if (feasible <= 0)
	{
		free(halfspaces);
		free(clearance);
		return feasible;
	}
	solved = SG_ConfigurationLatticeFindMaxClearance(halfspaces, clearance,
		count, objective, point, &positive_margin, &stats);
	free(halfspaces);
	free(clearance);
	if (solved <= 0)
		return solved;
	if (!positive_margin)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		witness[axis] = (float)point[axis] * 0.125f;
	return PointInsideRegion(build->semantics, region, witness) ? 1 : -1;

invalid:
	free(halfspaces);
	free(clearance);
	return -1;
}

static int FindRegionAtPose(const sg_ground_build_t *build, uint32_t cell,
	const float point[3], const sg_host_collision_pose_t *pose,
	uint32_t *region_out)
{
	uint32_t region;

	for (region = build->cell_region_offsets[cell];
		region < build->cell_region_offsets[cell + 1U]; region++)
	{
		const sg_configuration_semantic_region_t *record =
			&build->semantics->regions[region];
		int supported = (record->flags &
			SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED) != 0U;

		if (supported == pose->supported &&
			record->water_level == pose->water_level &&
			record->water_type == pose->water_type &&
			PointInsideRegion(build->semantics, record, point))
		{
			*region_out = region;
			return 1;
		}
	}
	return 0;
}

static int FindExactPortalSideWitness(const sg_ground_build_t *build,
	const sg_configuration_portal_t *portal, uint32_t cell,
	uint32_t region, const float portal_point[3], float witness[3])
{
	double interior_side = Dot3(
		build->configuration->cells[cell].interior_witness.value,
		portal->plane.normal) - (double)portal->plane.distance;
	int32_t portal_q8[3];
	int32_t x;
	int32_t y;
	int32_t z;
	uint32_t axis;

	if (!isfinite(interior_side) || interior_side == 0.0)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
	{
		double encoded = nearbyint((double)portal_point[axis] * 8.0);

		if (!isfinite(encoded) || encoded < (double)SHRT_MIN ||
			encoded > (double)SHRT_MAX ||
			encoded / 8.0 != (double)portal_point[axis])
			return -1;
		portal_q8[axis] = (int32_t)encoded;
	}
	for (x = -1; x <= 1; x++)
		for (y = -1; y <= 1; y++)
			for (z = -1; z <= 1; z++)
				{
					float candidate[3];
					double side;
					sg_host_collision_pose_t pose;
					int expected_supported =
						(build->semantics->regions[region].flags &
							SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED) != 0U;

					if (x == 0 && y == 0 && z == 0)
						continue;
					candidate[0] = (float)(portal_q8[0] + x) * 0.125f;
					candidate[1] = (float)(portal_q8[1] + y) * 0.125f;
					candidate[2] = (float)(portal_q8[2] + z) * 0.125f;
					side = Dot3(candidate, portal->plane.normal) -
						(double)portal->plane.distance;
					if (!isfinite(side) || side * interior_side <= 0.0 ||
						!PointInsideCell(build->configuration, cell, candidate) ||
						!PointInsideRegion(build->semantics,
							&build->semantics->regions[region], candidate))
						continue;
					if (!SG_HostCollisionClassifyPose(build->authority, NULL,
						candidate, build->configuration->cells[cell].stance, &pose))
						return -1;
					if (!pose.valid || pose.supported != expected_supported)
						continue;
					memcpy(witness, candidate, sizeof(candidate));
					return 1;
				}
	return 0;
}

static int BuildPortalDirection(sg_ground_build_t *build, uint32_t portal_index,
	uint32_t source_cell, uint32_t destination_cell)
{
	const sg_configuration_portal_t *portal =
		&build->configuration->portals[portal_index];
	uint32_t source_region;
	int proved = 0;

	for (source_region = build->cell_region_offsets[source_cell];
		source_region < build->cell_region_offsets[source_cell + 1U];
		source_region++)
	{
		float source_portal_point[3] = { 0.0f, 0.0f, 0.0f };
		float start[3];
		sg_host_collision_pose_t source_pose;
		uint32_t destination_region;
		int witness_status = PortalRegionWitness(build, portal, source_region,
			source_portal_point);

		if (witness_status < 0)
		{
			SetError(build->error,
				witness_status == -3 ? SG_GROUND_CAPABILITY_ERROR_OVERFLOW :
				witness_status == -2 ?
					SG_GROUND_CAPABILITY_ERROR_OUT_OF_MEMORY :
					SG_GROUND_CAPABILITY_ERROR_HOST_DISAGREEMENT,
				portal_index);
			return -1;
		}
		if (!witness_status)
			continue;
		witness_status = FindExactPortalSideWitness(build, portal, source_cell,
			source_region, source_portal_point, start);
		if (witness_status < 0)
		{
			SetError(build->error, SG_GROUND_CAPABILITY_ERROR_HOST_DISAGREEMENT,
				portal_index);
			return -1;
		}
		if (!witness_status)
			continue;
		if (!SG_HostCollisionClassifyPose(build->authority, NULL, start,
				portal->stance, &source_pose))
		{
			SetError(build->error, SG_GROUND_CAPABILITY_ERROR_HOST_DISAGREEMENT,
				portal_index);
			return -1;
		}
		if (!source_pose.valid)
			continue;
		for (destination_region = build->cell_region_offsets[destination_cell];
			destination_region < build->cell_region_offsets[destination_cell + 1U];
			destination_region++)
		{
			float destination_portal_point[3] = { 0.0f, 0.0f, 0.0f };
			float target[3];
			uint32_t source_binding;
			int destination_status = PortalRegionWitness(build, portal,
				destination_region, destination_portal_point);

			if (destination_status < 0)
			{
				SetError(build->error,
					destination_status == -3 ?
						SG_GROUND_CAPABILITY_ERROR_OVERFLOW :
					destination_status == -2 ?
						SG_GROUND_CAPABILITY_ERROR_OUT_OF_MEMORY :
						SG_GROUND_CAPABILITY_ERROR_HOST_DISAGREEMENT,
					portal_index);
				return -1;
			}
			if (!destination_status)
				continue;
			destination_status = FindExactPortalSideWitness(build, portal,
				destination_cell, destination_region, destination_portal_point,
				target);
			if (destination_status < 0)
			{
				SetError(build->error,
					SG_GROUND_CAPABILITY_ERROR_HOST_DISAGREEMENT, portal_index);
				return -1;
			}
			if (!destination_status ||
				!PortalContainsCrossing(build->configuration, portal, start, target))
				continue;
			for (source_binding = build->cell_phase_offsets[source_cell];
				source_binding < build->cell_phase_offsets[source_cell + 1U];
				source_binding++)
			{
				uint32_t source_phase = build->bindings[source_binding].phase;
				float initial_velocity[3];
				sg_host_pmove_result_t result;
				sg_host_collision_pose_t destination_pose;
				sg_ground_capability_kind_t kind;
				uint32_t result_region;
				int emitted;

				if (!PhaseMatchesRegionPose(&build->phases[source_phase],
						&build->semantics->regions[source_region], &source_pose))
					continue;
				if (!PhaseVelocitySample(&build->phases[source_phase], 0,
						initial_velocity))
				{
					SetError(build->error, SG_GROUND_CAPABILITY_ERROR_INVALID_PHASE,
						source_phase);
					return -1;
				}
				if (!EvaluateFrame(build, start, target, portal->stance, 1, 0,
					portal->stance == SG_RUNE_STANCE_CROUCHING,
					initial_velocity, &result))
					return -1;
				if (!PointInsideCell(build->configuration, destination_cell,
						result.origin) ||
					ResultStance(&result) !=
						build->configuration->cells[destination_cell].stance ||
					!PortalContainsCrossing(build->configuration, portal, start,
						result.origin))
					continue;
				if (!SG_HostCollisionClassifyPose(build->authority, NULL,
						result.origin, ResultStance(&result), &destination_pose))
				{
					SetError(build->error,
						SG_GROUND_CAPABILITY_ERROR_HOST_DISAGREEMENT, portal_index);
					return -1;
				}
				if (!destination_pose.valid ||
					!FindRegionAtPose(build, destination_cell, result.origin,
						&destination_pose, &result_region))
					continue;
				if (result.water_level != destination_pose.water_level ||
					result.water_type != (int)destination_pose.water_type)
					continue;
				kind = CrossingKind(&build->configuration->cells[source_cell],
					&source_pose, &destination_pose, start, result.origin);
				if (kind == SG_GROUND_CAPABILITY_KIND_COUNT)
					continue;
				if (kind == SG_GROUND_CAPABILITY_WALK ||
					kind == SG_GROUND_CAPABILITY_CROUCH ||
					kind == SG_GROUND_CAPABILITY_RAMP ||
					kind == SG_GROUND_CAPABILITY_STEP)
				{
					int continuity = EvaluateContinuouslySupported(build, start,
						target, portal->stance,
						portal->stance == SG_RUNE_STANCE_CROUCHING,
						initial_velocity, &result);

					if (continuity < 0)
						return -1;
					if (!continuity)
						continue;
					if (kind != SG_GROUND_CAPABILITY_STEP)
					{
						int geometric = GeometricallyContinuouslySupported(build,
							start, result.origin, portal->stance);

						if (geometric < 0)
							return -1;
						if (!geometric)
							continue;
					}
					else
					{
						int geometric = GeometricallyStepSupported(build, start,
							result.origin, portal->stance);

						if (geometric < 0)
							return -1;
						if (!geometric)
							continue;
					}
				}
				emitted = EmitForObservation(build, kind, source_cell,
					destination_cell, source_region, result_region, portal_index,
					source_phase, &source_pose, &destination_pose,
					initial_velocity, result.velocity, start, result.origin);
				if (emitted < 0)
					return -1;
				if (emitted > 0)
					proved = 1;
			}
		}
	}
	return proved;
}

static int BuildPortals(sg_ground_build_t *build)
{
	uint32_t portal;

	for (portal = 0U; portal < build->configuration->portal_count; portal++)
	{
		const sg_configuration_portal_t *record =
			&build->configuration->portals[portal];
		int forward = BuildPortalDirection(build, portal, record->from_cell,
			record->to_cell);
		int reverse;

		if (forward < 0)
			return 0;
		reverse = BuildPortalDirection(build, portal, record->to_cell,
			record->from_cell);
		if (reverse < 0)
			return 0;
		build->output->proved_directions += forward ? 1U : 0U;
		build->output->proved_directions += reverse ? 1U : 0U;
		build->output->rejected_directions += forward ? 0U : 1U;
		build->output->rejected_directions += reverse ? 0U : 1U;
		if (forward || reverse)
			build->output->proved_portals++;
		else
			build->output->rejected_crossings++;
	}
	return 1;
}

static sg_rune_stance_t ResultStance(const sg_host_pmove_result_t *result)
{
	return (result->state.pm_flags & PMF_DUCKED) != 0U ?
		SG_RUNE_STANCE_CROUCHING : SG_RUNE_STANCE_STANDING;
}

static int FindCellRegionAtPose(const sg_ground_build_t *build,
	const float point[3], sg_rune_stance_t stance,
	const sg_host_collision_pose_t *pose, uint32_t *cell_out,
	uint32_t *region_out)
{
	uint32_t cell;

	for (cell = 0U; cell < build->configuration->cell_count; cell++)
		if (build->configuration->cells[cell].stance == stance &&
			PointInsideCell(build->configuration, cell, point) &&
			FindRegionAtPose(build, cell, point, pose, region_out))
		{
			*cell_out = cell;
			return 1;
		}
	return 0;
}

static int BuildTakeoffsAndLandings(sg_ground_build_t *build)
{
	uint32_t cell;

	for (cell = 0U; cell < build->configuration->cell_count; cell++)
	{
		uint32_t source_region;

		for (source_region = build->cell_region_offsets[cell];
			source_region < build->cell_region_offsets[cell + 1U];
			source_region++)
		{
			const sg_configuration_semantic_region_t *source =
				&build->semantics->regions[source_region];
			sg_host_collision_pose_t source_pose;
			uint32_t source_binding;

			{
				int pose_status = RegionPose(build, source_region, &source_pose);

				if (pose_status < 0)
					return 0;
				if (!pose_status)
					continue;
			}
			for (source_binding = build->cell_phase_offsets[cell];
				source_binding < build->cell_phase_offsets[cell + 1U];
				source_binding++)
			{
				uint32_t source_phase = build->bindings[source_binding].phase;
				const sg_rune_phase_basis_t *phase =
					&build->phases[source_phase];
				float initial_velocity[3];
				sg_host_pmove_result_t result;
				sg_host_collision_pose_t destination_pose;
				uint32_t destination_region;
				uint32_t destination_cell;
				sg_ground_capability_kind_t kind;

				if (!PhaseMatchesRegionPose(phase, source, &source_pose))
					continue;
				if (!PhaseVelocitySample(phase, !source_pose.supported,
						initial_velocity))
				{
					SetError(build->error, SG_GROUND_CAPABILITY_ERROR_INVALID_PHASE,
						source_phase);
					return 0;
				}
				if (!source_pose.supported && initial_velocity[2] >= 0.0f)
					continue;
				if (!EvaluateFrame(build, source->interior_witness.value,
						source->interior_witness.value,
						build->configuration->cells[cell].stance, 0,
						source_pose.supported, 0, initial_velocity, &result))
					return 0;
				if (!SG_HostCollisionClassifyPose(build->authority, NULL,
						result.origin, ResultStance(&result), &destination_pose))
				{
					SetError(build->error,
						SG_GROUND_CAPABILITY_ERROR_HOST_DISAGREEMENT, source_region);
					return 0;
				}
				if (!destination_pose.valid)
					continue;
				if (!FindCellRegionAtPose(build, result.origin,
					ResultStance(&result), &destination_pose, &destination_cell,
					&destination_region))
					continue;
				if (result.water_level != destination_pose.water_level ||
					result.water_type != (int)destination_pose.water_type)
					continue;
				if (source_pose.supported && !result.grounded &&
					result.velocity[2] > 0.0f)
					kind = SG_GROUND_CAPABILITY_JUMP_TAKEOFF;
				else if (!source_pose.supported && result.grounded)
					kind = SG_GROUND_CAPABILITY_LANDING;
				else
					continue;
				if (EmitForObservation(build, kind, cell, destination_cell,
					source_region,
					destination_region, SG_GROUND_CAPABILITY_INDEX_NONE,
					source_phase, &source_pose, &destination_pose,
					initial_velocity, result.velocity,
					source->interior_witness.value, result.origin) < 0)
					return 0;
			}
		}
	}
	return 1;
}

static int BuildStanceDirection(sg_ground_build_t *build,
	const float witness[3], uint32_t source_cell, uint32_t destination_cell,
	uint32_t source_region, const sg_host_collision_pose_t *source_pose,
	sg_rune_stance_t source_stance, sg_rune_stance_t destination_stance,
	int crouch_command)
{
	uint32_t source_binding;

	for (source_binding = build->cell_phase_offsets[source_cell];
		source_binding < build->cell_phase_offsets[source_cell + 1U];
		source_binding++)
	{
		uint32_t source_phase = build->bindings[source_binding].phase;
		float initial_velocity[3];
		sg_host_pmove_result_t result;
		sg_host_collision_pose_t destination_pose;
		uint32_t destination_region;
		int ducked;

		if (!PhaseMatchesRegionPose(&build->phases[source_phase],
				&build->semantics->regions[source_region], source_pose))
			continue;
		if (!PhaseVelocitySample(&build->phases[source_phase], 0,
				initial_velocity))
		{
			SetError(build->error, SG_GROUND_CAPABILITY_ERROR_INVALID_PHASE,
				source_phase);
			return 0;
		}
		if (!EvaluateFrame(build, witness, witness, source_stance, 0, 0,
				crouch_command, initial_velocity, &result))
			return 0;
		ducked = (result.state.pm_flags & PMF_DUCKED) != 0U;
		if (ducked != (destination_stance == SG_RUNE_STANCE_CROUCHING) ||
			!PointInsideCell(build->configuration, destination_cell,
				result.origin))
			continue;
		if (!SG_HostCollisionClassifyPose(build->authority, NULL, result.origin,
				destination_stance, &destination_pose))
		{
			SetError(build->error,
				SG_GROUND_CAPABILITY_ERROR_HOST_DISAGREEMENT, source_region);
			return 0;
		}
		if (!destination_pose.valid ||
			!FindRegionAtPose(build, destination_cell, result.origin,
				&destination_pose, &destination_region))
			continue;
		if (EmitForObservation(build, SG_GROUND_CAPABILITY_STANCE,
			source_cell, destination_cell, source_region, destination_region,
			SG_GROUND_CAPABILITY_INDEX_NONE, source_phase, source_pose,
			&destination_pose, initial_velocity, result.velocity, witness,
			result.origin) < 0)
			return 0;
	}
	return 1;
}

static int BuildStanceOverlaps(sg_ground_build_t *build)
{
	uint32_t overlap;

	for (overlap = 0U;
		overlap < build->configuration->stance_overlap_count; overlap++)
	{
		const sg_configuration_stance_overlap_t *record =
			&build->configuration->stance_overlaps[overlap];
		sg_host_collision_pose_t standing;
		sg_host_collision_pose_t crouching;
		uint32_t standing_region;
		uint32_t crouching_region;

		if (record->standing_cell >= build->configuration->cell_count ||
			record->crouching_cell >= build->configuration->cell_count ||
			build->configuration->cells[record->standing_cell].stance !=
				SG_RUNE_STANCE_STANDING ||
			build->configuration->cells[record->crouching_cell].stance !=
				SG_RUNE_STANCE_CROUCHING ||
			!Finite3(record->interior_witness.value))
			continue;
		if (!SG_HostCollisionClassifyPose(build->authority, NULL,
				record->interior_witness.value, SG_RUNE_STANCE_STANDING,
				&standing) ||
			!SG_HostCollisionClassifyPose(build->authority, NULL,
				record->interior_witness.value, SG_RUNE_STANCE_CROUCHING,
				&crouching))
		{
			SetError(build->error,
				SG_GROUND_CAPABILITY_ERROR_HOST_DISAGREEMENT, overlap);
			return 0;
		}
		if (!standing.valid || !crouching.valid ||
			!FindRegionAtPose(build, record->standing_cell,
				record->interior_witness.value, &standing, &standing_region) ||
			!FindRegionAtPose(build, record->crouching_cell,
				record->interior_witness.value, &crouching, &crouching_region))
			continue;
		if (!BuildStanceDirection(build, record->interior_witness.value,
			record->standing_cell, record->crouching_cell, standing_region,
			&standing, SG_RUNE_STANCE_STANDING, SG_RUNE_STANCE_CROUCHING, 1))
			return 0;
		if (!BuildStanceDirection(build, record->interior_witness.value,
			record->crouching_cell, record->standing_cell, crouching_region,
			&crouching, SG_RUNE_STANCE_CROUCHING, SG_RUNE_STANCE_STANDING, 0))
			return 0;
	}
	return 1;
}

static int CompareCapability(const void *left_pointer,
	const void *right_pointer)
{
	const sg_ground_capability_t *left = left_pointer;
	const sg_ground_capability_t *right = right_pointer;

#define SG_GROUND_COMPARE(field) \
	if (left->field != right->field) \
		return left->field < right->field ? -1 : 1
	SG_GROUND_COMPARE(source_cell);
	SG_GROUND_COMPARE(destination_cell);
	SG_GROUND_COMPARE(kind);
	SG_GROUND_COMPARE(portal);
	SG_GROUND_COMPARE(source_phase);
	SG_GROUND_COMPARE(destination_phase);
	SG_GROUND_COMPARE(source_region);
	SG_GROUND_COMPARE(destination_region);
#undef SG_GROUND_COMPARE
	{
		uint32_t axis;

		for (axis = 0U; axis < 3U; axis++)
		{
			uint32_t left_bits;
			uint32_t right_bits;

			memcpy(&left_bits, &left->source_witness.value[axis],
				sizeof(left_bits));
			memcpy(&right_bits, &right->source_witness.value[axis],
				sizeof(right_bits));
			if (left_bits != right_bits)
				return left_bits < right_bits ? -1 : 1;
			memcpy(&left_bits, &left->destination_witness.value[axis],
				sizeof(left_bits));
			memcpy(&right_bits, &right->destination_witness.value[axis],
				sizeof(right_bits));
			if (left_bits != right_bits)
				return left_bits < right_bits ? -1 : 1;
			memcpy(&left_bits, &left->initial_velocity.value[axis],
				sizeof(left_bits));
			memcpy(&right_bits, &right->initial_velocity.value[axis],
				sizeof(right_bits));
			if (left_bits != right_bits)
				return left_bits < right_bits ? -1 : 1;
			memcpy(&left_bits, &left->observed_velocity.value[axis],
				sizeof(left_bits));
			memcpy(&right_bits, &right->observed_velocity.value[axis],
				sizeof(right_bits));
			if (left_bits != right_bits)
				return left_bits < right_bits ? -1 : 1;
		}
	}
	return 0;
}

void SG_GroundCapabilityDefaultLimits(
	sg_ground_capability_limits_t *limits_out)
{
	if (limits_out)
		limits_out->max_capabilities = SG_RUNE_MODEL_MAX_KERNELS;
}

int SG_GroundCapabilityBuild(
	const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	const sg_rune_phase_basis_t *phases, size_t phase_count,
	const sg_ground_phase_binding_t *bindings, size_t binding_count,
	sg_host_pmove_function_t host_pmove,
	const sg_ground_capability_limits_t *limits,
	sg_ground_capability_set_t **set_out,
	sg_ground_capability_error_t *error_out)
{
	sg_ground_build_t build;
	sg_ground_capability_limits_t defaults;
	int offsets;

	if (error_out)
		memset(error_out, 0, sizeof(*error_out));
	if (!authority || !configuration || !semantics || !phases || !bindings ||
		!host_pmove || !set_out || *set_out)
	{
		SetError(error_out, SG_GROUND_CAPABILITY_ERROR_INVALID_ARGUMENT,
			SG_GROUND_CAPABILITY_INDEX_NONE);
		return 0;
	}
	if (!SourcesValid(authority, configuration, semantics))
	{
		SetError(error_out,
			!IdentityEqual(&authority->identity, &configuration->identity) ||
			 !IdentityEqual(&authority->identity, &semantics->identity) ?
				SG_GROUND_CAPABILITY_ERROR_IDENTITY_MISMATCH :
				SG_GROUND_CAPABILITY_ERROR_INVALID_SOURCE,
			SG_GROUND_CAPABILITY_INDEX_NONE);
		return 0;
	}
	SG_GroundCapabilityDefaultLimits(&defaults);
	if (!limits)
		limits = &defaults;
	if (limits->max_capabilities == 0U)
	{
		SetError(error_out, SG_GROUND_CAPABILITY_ERROR_INVALID_ARGUMENT,
			SG_GROUND_CAPABILITY_INDEX_NONE);
		return 0;
	}
	memset(&build, 0, sizeof(build));
	build.authority = authority;
	build.configuration = configuration;
	build.semantics = semantics;
	build.phases = phases;
	build.bindings = bindings;
	build.host_pmove = host_pmove;
	build.max_capabilities = limits->max_capabilities;
	build.error = error_out;
	build.output = calloc(1, sizeof(*build.output));
	if (!build.output)
	{
		SetError(error_out, SG_GROUND_CAPABILITY_ERROR_OUT_OF_MEMORY,
			SG_GROUND_CAPABILITY_INDEX_NONE);
		return 0;
	}
	build.output->identity = authority->identity;
	offsets = BuildOffsets(&build, phase_count, binding_count);
	if (offsets <= 0)
	{
		SetError(error_out, offsets < 0 ?
			SG_GROUND_CAPABILITY_ERROR_OUT_OF_MEMORY :
			SG_GROUND_CAPABILITY_ERROR_INVALID_PHASE,
			SG_GROUND_CAPABILITY_INDEX_NONE);
		goto fail;
	}
	if (!BuildPortals(&build) || !BuildStanceOverlaps(&build) ||
		!BuildTakeoffsAndLandings(&build))
		goto fail;
	if (build.output->capability_count > build.max_capabilities)
	{
		SetError(error_out, SG_GROUND_CAPABILITY_ERROR_OVERFLOW,
			build.output->capability_count);
		goto fail;
	}
	free(build.cell_phase_offsets);
	free(build.cell_region_offsets);
	*set_out = build.output;
	SetError(error_out, SG_GROUND_CAPABILITY_ERROR_NONE,
		SG_GROUND_CAPABILITY_INDEX_NONE);
	return 1;

fail:
	free(build.cell_phase_offsets);
	free(build.cell_region_offsets);
	SG_GroundCapabilityDestroy(build.output);
	if (error_out && error_out->code == SG_GROUND_CAPABILITY_ERROR_NONE)
		SetError(error_out, SG_GROUND_CAPABILITY_ERROR_HOST_DISAGREEMENT,
			SG_GROUND_CAPABILITY_INDEX_NONE);
	return 0;
}

void SG_GroundCapabilityDestroy(sg_ground_capability_set_t *set)
{
	if (!set)
		return;
	free(set->capabilities);
	free(set);
}

const char *SG_GroundCapabilityErrorString(
	sg_ground_capability_error_code_t code)
{
	switch (code)
	{
	case SG_GROUND_CAPABILITY_ERROR_NONE: return "none";
	case SG_GROUND_CAPABILITY_ERROR_INVALID_ARGUMENT: return "invalid argument";
	case SG_GROUND_CAPABILITY_ERROR_INVALID_SOURCE: return "invalid source";
	case SG_GROUND_CAPABILITY_ERROR_IDENTITY_MISMATCH: return "identity mismatch";
	case SG_GROUND_CAPABILITY_ERROR_INVALID_PHASE: return "invalid phase binding";
	case SG_GROUND_CAPABILITY_ERROR_HOST_DISAGREEMENT: return "host disagreement";
	case SG_GROUND_CAPABILITY_ERROR_OVERFLOW: return "representation overflow";
	case SG_GROUND_CAPABILITY_ERROR_OUT_OF_MEMORY: return "out of memory";
	default: return "unknown ground capability error";
	}
}
