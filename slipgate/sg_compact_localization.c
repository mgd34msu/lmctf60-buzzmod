#include "sg_compact_localization.h"

#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void ClearState(sg_compact_localized_state_t *state)
{
	if (state)
	{
		memset(state, 0, sizeof(*state));
		state->location.cell.value = SG_RUNE_COMPACT_INDEX_NONE;
	}
}

static int SubjectValid(const sg_localization_subject_t *subject)
{
	return subject && subject->reserved == 0U &&
		subject->client_id != UINT32_MAX && subject->spawn_generation != 0U;
}

static int SubjectEqual(const sg_localization_subject_t *left,
	const sg_localization_subject_t *right)
{
	return SubjectValid(left) && SubjectValid(right) &&
		left->client_id == right->client_id &&
		left->spawn_generation == right->spawn_generation;
}

static int ZeroBytes(const void *data, size_t size)
{
	const unsigned char *bytes = data;
	size_t index;

	for (index = 0U; index < size; index++)
		if (bytes[index] != 0U)
			return 0;
	return 1;
}

static int SubjectZero(const sg_localization_subject_t *subject)
{
	return subject && ZeroBytes(subject, sizeof(*subject));
}

static uint32_t FloatBits(float value)
{
	uint32_t actual;

	memcpy(&actual, &value, sizeof(actual));
	return actual;
}

static int FloatBitsEqual(float value, uint32_t bits)
{
	return FloatBits(value) == bits;
}

static int FiniteVector(const float value[3])
{
	return isfinite(value[0]) && isfinite(value[1]) && isfinite(value[2]);
}

static int ModelStampMatchesBinding(
	const sg_compact_localization_binding_t *binding,
	const sg_localization_model_stamp_t *stamp, uint64_t frame_sequence)
{
	return binding && stamp && stamp->identity == binding->rune_identity &&
		stamp->generation == binding->topology_revision &&
		stamp->frame_sequence == frame_sequence && frame_sequence != 0U;
}

static int CompactIdentityEqual(const sg_rune_compact_identity_t *left,
	const sg_rune_compact_identity_t *right)
{
	uint32_t axis;
	uint32_t digest_byte;

	if (!left || !right)
		return 0;
	for (digest_byte = 0U; digest_byte < 32U; digest_byte++)
		if (left->bsp_sha256[digest_byte] != right->bsp_sha256[digest_byte])
			return 0;
	if (left->bsp_bytes != right->bsp_bytes ||
		left->bsp_checksum != right->bsp_checksum ||
		left->entity_crc32 != right->entity_crc32 ||
		left->entity_semantics_id != right->entity_semantics_id ||
		left->physics_abi_id != right->physics_abi_id ||
		left->collision_law_id != right->collision_law_id ||
		left->pmove_law_id != right->pmove_law_id ||
		left->gravity_law_id != right->gravity_law_id ||
		left->hook_law_id != right->hook_law_id ||
		left->mechanism_law_id != right->mechanism_law_id ||
		left->weapon_law_id != right->weapon_law_id ||
		left->construction_id != right->construction_id ||
		left->schema_id != right->schema_id ||
		left->producer_identity != right->producer_identity ||
		left->weapon_profile_catalog_id != right->weapon_profile_catalog_id)
		return 0;
	if (left->source_counts.model_count != right->source_counts.model_count ||
		left->source_counts.leaf_count != right->source_counts.leaf_count ||
		left->source_counts.area_count != right->source_counts.area_count ||
		left->source_counts.plane_count != right->source_counts.plane_count ||
		left->source_counts.brush_count != right->source_counts.brush_count ||
		left->source_counts.brush_side_count !=
			right->source_counts.brush_side_count ||
		left->source_counts.entity_count != right->source_counts.entity_count)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
	{
		if (left->standing_hull.mins.value[axis] !=
			right->standing_hull.mins.value[axis] ||
			left->standing_hull.maxs.value[axis] !=
			right->standing_hull.maxs.value[axis] ||
			left->crouching_hull.mins.value[axis] !=
			right->crouching_hull.mins.value[axis] ||
			left->crouching_hull.maxs.value[axis] !=
			right->crouching_hull.maxs.value[axis])
			return 0;
	}
	return left->physics.gravity_bits == right->physics.gravity_bits &&
		left->physics.ground_acceleration_bits ==
			right->physics.ground_acceleration_bits &&
		left->physics.air_acceleration_bits ==
			right->physics.air_acceleration_bits &&
		left->physics.water_acceleration_bits ==
			right->physics.water_acceleration_bits &&
		left->physics.hook_acceleration_bits ==
			right->physics.hook_acceleration_bits &&
		left->physics.external_acceleration_bits ==
			right->physics.external_acceleration_bits &&
		left->physics.water_drag_bits == right->physics.water_drag_bits &&
		left->physics.max_velocity_bits == right->physics.max_velocity_bits &&
		left->physics.frame_ms == right->physics.frame_ms &&
		left->physics.substep_ms == right->physics.substep_ms;
}

static int CompactModelShapeValid(const sg_rune_compact_model_t *model)
{
	return model && model->version == SG_RUNE_COMPACT_MODEL_VERSION &&
		model->reserved == 0U && model->schema_tag == SG_RUNE_COMPACT_MODEL_SCHEMA_TAG &&
		model->cells && model->cell_count != 0U &&
		model->cell_count <= SG_RUNE_COMPACT_MAX_CELLS;
}

static int HostPhysicsMatchesCompact(
	const sg_rune_physics_parameters_t *host,
	const sg_rune_compact_physics_t *compact)
{
	return host && compact &&
		FloatBitsEqual(host->gravity, compact->gravity_bits) &&
		FloatBitsEqual(host->ground_acceleration,
			compact->ground_acceleration_bits) &&
		FloatBitsEqual(host->air_acceleration,
			compact->air_acceleration_bits) &&
		FloatBitsEqual(host->water_acceleration,
			compact->water_acceleration_bits) &&
		FloatBitsEqual(host->hook_acceleration,
			compact->hook_acceleration_bits) &&
		FloatBitsEqual(host->external_acceleration,
			compact->external_acceleration_bits) &&
		FloatBitsEqual(host->water_drag, compact->water_drag_bits) &&
		FloatBitsEqual(host->max_velocity, compact->max_velocity_bits) &&
		host->frame_ms == compact->frame_ms &&
		host->substep_ms == compact->substep_ms;
}

static int HostStaticIdentityMatchesCompact(
	const sg_host_static_identity_t *host,
	const sg_rune_compact_identity_t *compact)
{
	uint32_t axis;

	if (!host || !compact ||
		memcmp(host->bsp_identity.bytes, compact->bsp_sha256,
			SG_BSP_CONTENT_ID_BYTES) != 0 ||
		host->bsp_bytes != compact->bsp_bytes ||
		host->engine_checksum != compact->bsp_checksum ||
		host->entity_crc32 != compact->entity_crc32 ||
		host->physics_abi_id != compact->physics_abi_id ||
		host->host_physics_epoch == 0U || host->reserved != 0U)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
	{
		if (!FloatBitsEqual(host->standing_hull.mins.value[axis],
			FloatBits((float)compact->standing_hull.mins.value[axis] * 0.125f)) ||
			!FloatBitsEqual(host->standing_hull.maxs.value[axis],
				FloatBits((float)compact->standing_hull.maxs.value[axis] * 0.125f)) ||
			!FloatBitsEqual(host->crouching_hull.mins.value[axis],
				FloatBits((float)compact->crouching_hull.mins.value[axis] * 0.125f)) ||
			!FloatBitsEqual(host->crouching_hull.maxs.value[axis],
				FloatBits((float)compact->crouching_hull.maxs.value[axis] * 0.125f)))
			return 0;
	}
	return HostPhysicsMatchesCompact(&host->physics, &compact->physics);
}

static int HostLawMatchesIdentity(
	const sg_host_law_runtime_authority_t *host_authority,
	const sg_rune_compact_identity_t *identity)
{
	const sg_host_law_view_t *view;

	if (!host_authority || !identity)
		return 0;
	view = &host_authority->view;
	return view->version == SG_HOST_LAW_PUBLICATION_VERSION &&
		view->reserved == 0U &&
		memcmp(view->bsp_identity.bytes, identity->bsp_sha256,
			sizeof(identity->bsp_sha256)) == 0 &&
		view->bsp_bytes == identity->bsp_bytes &&
		view->collision_law_id == identity->collision_law_id &&
		view->pmove_law_id == identity->pmove_law_id &&
		view->gravity_law_id == identity->gravity_law_id &&
		view->hook_law_id == identity->hook_law_id &&
		view->mechanism_law_id == identity->mechanism_law_id &&
		view->pmove_abi.identity == identity->physics_abi_id &&
		HostStaticIdentityMatchesCompact(&view->static_identity, identity);
}

static int BindingShapeValid(const sg_compact_localization_binding_t *binding)
{
	return binding && binding->bound == 1U &&
		ZeroBytes(binding->reserved, sizeof(binding->reserved)) &&
		binding->host_authority.version ==
			SG_HOST_LAW_RUNTIME_AUTHORITY_VERSION &&
		binding->host_authority.reserved == 0U &&
		binding->host_authority.epoch != 0U &&
		binding->host_authority.epoch_complement ==
			~binding->host_authority.epoch &&
		binding->rune_identity != 0U && binding->topology_revision != 0U &&
		binding->spatial_index != NULL &&
		binding->observation_owner.validate != NULL &&
		CompactModelShapeValid(binding->model) &&
		CompactIdentityEqual(&binding->model->identity, &binding->identity) &&
		HostLawMatchesIdentity(&binding->host_authority, &binding->identity);
}

static int GravityMatches(float gravity, uint64_t gravity_law_id,
	const sg_rune_compact_identity_t *identity)
{
	if (!isfinite(gravity))
		return 0;
	if (FloatBitsEqual(gravity, identity->physics.gravity_bits))
		return 1;
	return gravity == 0.0f && gravity_law_id == SG_HOST_PMOVE_HOOK_LAW_ID;
}

static int StateOriginMatches(const pmove_state_t *state,
	const float origin[3], const float velocity[3])
{
	uint32_t axis;

	if (!state || !FiniteVector(origin) || !FiniteVector(velocity))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (state->origin[axis] * 0.125f != origin[axis] ||
			state->velocity[axis] * 0.125f != velocity[axis])
			return 0;
	return 1;
}

static int PmoveResultValid(
	const sg_compact_localization_binding_t *binding,
	const sg_host_pmove_result_t *result)
{
	const uint32_t expected_steps = binding->identity.physics.substep_ms == 0U ?
		0U : binding->identity.physics.frame_ms /
			binding->identity.physics.substep_ms;

	return result && result->state.pm_type == PM_NORMAL &&
		(result->grounded == 0 || result->grounded == 1) &&
		result->water_level >= 0 && result->water_level <= 3 &&
		result->touch_count <= MAXTOUCH &&
		result->evaluated_steps == expected_steps &&
		result->elapsed_ms == binding->identity.physics.frame_ms &&
		result->physics_abi_id == binding->identity.physics_abi_id &&
		(result->gravity_law_id == 0U ||
			result->gravity_law_id == SG_HOST_PMOVE_HOOK_LAW_ID) &&
		GravityMatches(result->gravity, result->gravity_law_id,
			&binding->identity) &&
		FloatBits((float)result->state.gravity) == FloatBits(result->gravity) &&
		StateOriginMatches(&result->state, result->origin, result->velocity);
}

static int StateObservationValid(
	const sg_compact_localization_binding_t *binding,
	const sg_host_pmove_state_observation_t *observation)
{
	float gravity;

	if (!observation || observation->state.pm_type != PM_NORMAL ||
		!StateOriginMatches(&observation->state, observation->origin,
			observation->velocity))
		return 0;
	gravity = (float)observation->state.gravity;
	return GravityMatches(gravity, 0U, &binding->identity);
}

static int StateFactsMatchResult(const sg_host_collision_pose_t *pose,
	const sg_host_pmove_result_t *result)
{
	if (!pose || !result)
		return 0;
	if (result->grounded != pose->supported ||
		result->water_level != (int)pose->water_level ||
		result->water_type != (int)pose->water_type)
		return 0;
	if (!pose->supported)
		return result->support_model_index == SG_HOST_COLLISION_MODEL_WORLD &&
			result->support_instance_id == 0U;
	return result->support_model_index == pose->support.model_index &&
		result->support_instance_id == pose->support.instance_id;
}

static int FloatToQ8(float value, int32_t *output)
{
	double scaled;

	if (!output || !isfinite(value))
		return 0;
	scaled = (double)value * 8.0;
	if (!isfinite(scaled) || scaled < (double)INT32_MIN ||
		scaled > (double)INT32_MAX || floor(scaled) != scaled)
		return 0;
	*output = (int32_t)scaled;
	return 1;
}

static int PointToQ8(const float position[3], sg_rune_q8_vec3_t *point)
{
	uint32_t axis;

	if (!point || !FiniteVector(position))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (!FloatToQ8(position[axis], &point->value[axis]))
			return 0;
	return 1;
}

static int HostStateMatchesStored(
	const sg_compact_localization_binding_t *binding,
	const sg_compact_localized_state_t *state)
{
	const float gravity = (float)state->host_state.gravity;
	const int ducked = (state->host_state.pm_flags & PMF_DUCKED) != 0;

	return state->host_state.pm_type == PM_NORMAL &&
		(ducked == (state->stance == SG_RUNE_STANCE_CROUCHING)) &&
		(FloatBitsEqual(gravity, binding->identity.physics.gravity_bits) ||
			state->host_state.gravity == 0) &&
		StateOriginMatches(&state->host_state, state->position,
			state->velocity);
}

static sg_rune_medium_t MediumForFacts(uint8_t water_level,
	sg_host_collision_contents_t water_type)
{
	if (water_level == 0U)
		return SG_RUNE_MEDIUM_DRY;
	if ((water_type & SG_HOST_CONTENTS_LAVA) != 0U)
		return SG_RUNE_MEDIUM_LAVA;
	if ((water_type & SG_HOST_CONTENTS_SLIME) != 0U)
		return SG_RUNE_MEDIUM_SLIME;
	return SG_RUNE_MEDIUM_WATER;
}

static int PreviousStateShapeValid(
	const sg_compact_localization_binding_t *binding,
	const sg_localization_subject_t *subject,
	const sg_compact_localized_state_t *state)
{
	const sg_rune_compact_cell_t *cell;

	if (!binding || !subject || !state || state->valid != 1U ||
		!ZeroBytes(state->reserved, sizeof(state->reserved)) ||
		!SubjectEqual(subject, &state->subject) ||
		!ModelStampMatchesBinding(binding, &state->model_stamp,
			state->frame_sequence) ||
		state->rune_identity != binding->rune_identity ||
		state->topology_revision != binding->topology_revision ||
		state->frame_sequence == 0U || state->localized_at_ms == 0U ||
		state->location.cell.value >= binding->model->cell_count ||
		state->stance < SG_RUNE_STANCE_STANDING ||
		state->stance >= SG_RUNE_STANCE_COUNT ||
		state->motion < SG_RUNE_MOTION_SUPPORTED ||
		state->motion >= SG_RUNE_MOTION_COUNT ||
		state->support < SG_RUNE_SUPPORT_NONE ||
		state->support >= SG_RUNE_SUPPORT_COUNT ||
		state->medium < SG_RUNE_MEDIUM_DRY ||
		state->medium >= SG_RUNE_MEDIUM_COUNT ||
		state->void_relation < SG_RUNE_VOID_CLEAR ||
		state->void_relation >= SG_RUNE_VOID_RELATION_COUNT ||
		state->reference_frame < SG_RUNE_FRAME_WORLD ||
		state->reference_frame >= SG_RUNE_FRAME_COUNT ||
		state->presence < SG_LOCALIZATION_PRESENCE_PRESENT ||
		state->presence > SG_LOCALIZATION_PRESENCE_TEMPORARILY_ABSENT ||
		state->recovery < SG_LOCALIZATION_RECOVERY_NONE ||
		state->recovery > SG_LOCALIZATION_RECOVERY_TEMPORARY_ABSENCE ||
		!ZeroBytes(state->location.reserved, sizeof(state->location.reserved)) ||
		state->water_level > 3U || !FiniteVector(state->position) ||
		!FiniteVector(state->velocity))
		return 0;
	if (!HostStateMatchesStored(binding, state))
		return 0;
	if ((state->presence == SG_LOCALIZATION_PRESENCE_TEMPORARILY_ABSENT) !=
		(state->recovery == SG_LOCALIZATION_RECOVERY_TEMPORARY_ABSENCE))
		return 0;
	if (state->presence == SG_LOCALIZATION_PRESENCE_TEMPORARILY_ABSENT ?
		(state->absence_started_at_ms == 0U ||
			state->absence_started_at_ms > state->localized_at_ms) :
		state->absence_started_at_ms != 0U)
		return 0;
	cell = &binding->model->cells[state->location.cell.value];
	if ((cell->valid_stances & (sg_rune_stance_validity_t)~
			SG_RUNE_STANCE_VALID_ALL) != 0U ||
		(cell->valid_stances & (sg_rune_stance_validity_t)(
			state->stance == SG_RUNE_STANCE_CROUCHING ?
			SG_RUNE_STANCE_VALID_CROUCHING : SG_RUNE_STANCE_VALID_STANDING)) == 0U ||
		(state->support == SG_RUNE_SUPPORT_NONE &&
			(state->support_model_index != SG_LOCALIZATION_SUPPORT_MODEL_NONE ||
				state->support_instance_id != 0U)) ||
		(state->support != SG_RUNE_SUPPORT_NONE &&
			state->support_model_index == SG_LOCALIZATION_SUPPORT_MODEL_NONE) ||
		(cell->semantics & SG_RUNE_COMPACT_CELL_MOVER_VOLUME) != 0U ||
		(state->support == SG_RUNE_SUPPORT_MOVER) !=
			(state->reference_frame == SG_RUNE_FRAME_MOVER_RELATIVE) ||
		(state->support != SG_RUNE_SUPPORT_MOVER &&
			state->reference_frame != SG_RUNE_FRAME_WORLD) ||
		state->medium != MediumForFacts(state->water_level,
			state->water_type) ||
		state->void_relation != ((cell->semantics &
			SG_RUNE_COMPACT_CELL_VOID_BOUNDARY) != 0U ?
			SG_RUNE_VOID_ADJACENT : SG_RUNE_VOID_CLEAR))
		return 0;
	if (state->water_level >= 2U)
	{
		if (state->motion != SG_RUNE_MOTION_SWIMMING ||
			state->support != SG_RUNE_SUPPORT_NONE)
			return 0;
	}
	else if (state->support == SG_RUNE_SUPPORT_NONE)
	{
		if (state->motion != SG_RUNE_MOTION_AIRBORNE)
			return 0;
	}
	else if (state->motion != SG_RUNE_MOTION_SUPPORTED)
		return 0;
	return 1;
}

static int LifeResetShapeValid(
	const sg_compact_localization_observation_view_t *view)
{
	if (!view)
		return 0;
	if (view->kind != SG_LOCALIZATION_OBSERVATION_NEW_SPAWN)
		return SubjectZero(&view->previous_subject) &&
			view->previous_frame_sequence == 0U &&
			view->previous_observed_at_ms == 0U;
	if (SubjectZero(&view->previous_subject))
		return view->previous_frame_sequence == 0U &&
			view->previous_observed_at_ms == 0U;
	return SubjectValid(&view->previous_subject) &&
		view->previous_subject.client_id == view->subject.client_id &&
		view->previous_subject.spawn_generation <
			view->subject.spawn_generation &&
		view->previous_frame_sequence != 0U &&
		view->previous_observed_at_ms != 0U;
}

static int LifeResetMatchesPrevious(
	const sg_compact_localization_observation_view_t *view,
	const sg_compact_localized_state_t *previous)
{
	return view && previous &&
		SubjectEqual(&view->previous_subject, &previous->subject) &&
		view->previous_frame_sequence == previous->frame_sequence &&
		view->previous_observed_at_ms == previous->localized_at_ms;
}

static sg_localization_status_t PreviousLifecycleStatus(
	const sg_compact_localization_binding_t *binding,
	const sg_compact_localization_observation_view_t *view,
	const sg_compact_localized_state_t *previous)
{
	if (view->kind == SG_LOCALIZATION_OBSERVATION_TELEPORTED &&
		!previous)
		return SG_LOCALIZATION_RECOVERY_PARAMETER;
	if (previous && !PreviousStateShapeValid(binding, &previous->subject,
		previous))
		return SG_LOCALIZATION_RECOVERY_REJECTED;
	if (view->kind == SG_LOCALIZATION_OBSERVATION_NEW_SPAWN)
	{
		if (previous)
		{
			if (!LifeResetMatchesPrevious(view, previous))
				return SG_LOCALIZATION_RECOVERY_REJECTED;
		}
		else if (!SubjectZero(&view->previous_subject) &&
			(view->previous_frame_sequence == 0U ||
				view->previous_observed_at_ms == 0U))
			return SG_LOCALIZATION_RECOVERY_REJECTED;
		if (previous &&
			(view->frame_sequence <= previous->frame_sequence ||
			 view->observed_at_ms <= previous->localized_at_ms))
			return SG_LOCALIZATION_STALE;
		if (!previous &&
			(view->frame_sequence <= view->previous_frame_sequence ||
			 view->observed_at_ms <= view->previous_observed_at_ms))
			return SG_LOCALIZATION_STALE;
		return SG_LOCALIZATION_OK;
	}
	if (previous && !SubjectEqual(&view->subject, &previous->subject))
		return SG_LOCALIZATION_IDENTITY_MISMATCH;
	if (previous && (view->frame_sequence <= previous->frame_sequence ||
		view->observed_at_ms <= previous->localized_at_ms))
		return SG_LOCALIZATION_STALE;
	return SG_LOCALIZATION_OK;
}

static int RecoveryRadius(float maximum_distance, int32_t *radius_out)
{
	double scaled;

	if (!radius_out || !isfinite(maximum_distance) ||
		maximum_distance < 0.0f ||
		maximum_distance > SG_COMPACT_LOCALIZATION_MAX_RECOVERY_DISTANCE)
		return 0;
	scaled = ceil((double)maximum_distance * 8.0);
	if (!isfinite(scaled) || scaled < 0.0 ||
		scaled > (double)SG_COMPACT_LOCALIZATION_MAX_RECOVERY_Q8)
		return 0;
	*radius_out = (int32_t)scaled;
	return 1;
}

static int PortalEndpointCells(const sg_rune_compact_model_t *model,
	uint32_t portal_index, uint32_t *negative_cell_out,
	uint32_t *positive_cell_out)
{
	const sg_rune_compact_portal_t *portal;
	const sg_rune_compact_incidence_t *negative;
	const sg_rune_compact_incidence_t *positive;

	if (!model || !negative_cell_out || !positive_cell_out ||
		portal_index >= model->portal_count || !model->portals ||
		!model->incidences || !model->facets)
		return 0;
	portal = &model->portals[portal_index];
	if (portal->facet.value >= model->facet_count ||
		portal->negative_incidence.value >= model->incidence_count ||
		portal->positive_incidence.value >= model->incidence_count ||
		portal->direction < SG_RUNE_PORTAL_CONTINUITY_BOTH ||
		portal->direction >= SG_RUNE_PORTAL_CONTINUITY_COUNT ||
		(portal->valid_stances & (sg_rune_stance_validity_t)
			~SG_RUNE_STANCE_VALID_ALL) != 0U || portal->valid_stances == 0U ||
		model->facets[portal->facet.value].portal.value != portal_index)
		return 0;
	negative = &model->incidences[portal->negative_incidence.value];
	positive = &model->incidences[portal->positive_incidence.value];
	if (negative->facet.value != portal->facet.value ||
		positive->facet.value != portal->facet.value ||
		negative->side != SG_RUNE_FACET_NEGATIVE_SIDE ||
		positive->side != SG_RUNE_FACET_POSITIVE_SIDE ||
		negative->cell.value >= model->cell_count ||
		positive->cell.value >= model->cell_count ||
		negative->cell.value == positive->cell.value)
		return 0;
	*negative_cell_out = negative->cell.value;
	*positive_cell_out = positive->cell.value;
	return 1;
}

static sg_rune_stance_validity_t StanceBit(sg_rune_stance_t stance)
{
	return stance == SG_RUNE_STANCE_CROUCHING ?
		SG_RUNE_STANCE_VALID_CROUCHING : SG_RUNE_STANCE_VALID_STANDING;
}

static int PortalAllowsTransition(const sg_rune_compact_portal_t *portal,
	int negative_to_positive, sg_rune_stance_t previous_stance,
	sg_rune_stance_t current_stance)
{
	const sg_rune_stance_validity_t required =
		(sg_rune_stance_validity_t)(StanceBit(previous_stance) |
			StanceBit(current_stance));

	if (!portal || (portal->valid_stances & required) != required)
		return 0;
	if (portal->direction == SG_RUNE_PORTAL_CONTINUITY_BOTH)
		return 1;
	return negative_to_positive ?
		portal->direction == SG_RUNE_PORTAL_CONTINUITY_NEGATIVE_TO_POSITIVE :
		portal->direction == SG_RUNE_PORTAL_CONTINUITY_POSITIVE_TO_NEGATIVE;
}

/* Returns one for an allowed transition, zero for no exact portal, and -1
 * when the accepted model's portal relation is malformed. */
static int CellTransitionAllowed(const sg_rune_compact_model_t *model,
	uint32_t from_cell, uint32_t to_cell, sg_rune_stance_t previous_stance,
	sg_rune_stance_t current_stance)
{
	const sg_rune_compact_cell_t *cell;
	uint32_t local;

	if (!model || from_cell >= model->cell_count || to_cell >= model->cell_count)
		return -1;
	if (from_cell == to_cell)
	{
		const sg_rune_stance_validity_t required =
			(sg_rune_stance_validity_t)(StanceBit(previous_stance) |
				StanceBit(current_stance));

		return (model->cells[from_cell].valid_stances & required) == required;
	}
	cell = &model->cells[from_cell];
	if (cell->incidences.first > model->cell_incidence_count ||
		cell->incidences.count > model->cell_incidence_count -
			cell->incidences.first || !model->cell_incidences ||
		!model->incidences || !model->facets)
		return -1;
	for (local = 0U; local < cell->incidences.count; local++)
	{
		const uint32_t reference = cell->incidences.first + local;
		const uint32_t incidence_index = model->cell_incidences[reference].value;
		const sg_rune_compact_incidence_t *incidence;
		uint32_t portal_index;
		uint32_t negative_cell;
		uint32_t positive_cell;

		if (incidence_index >= model->incidence_count)
			return -1;
		incidence = &model->incidences[incidence_index];
		if (incidence->cell.value != from_cell ||
			incidence->facet.value >= model->facet_count)
			return -1;
		portal_index = model->facets[incidence->facet.value].portal.value;
		if (portal_index == SG_RUNE_COMPACT_INDEX_NONE)
			continue;
		if (!PortalEndpointCells(model, portal_index, &negative_cell,
			&positive_cell))
			return -1;
		if (negative_cell == from_cell && positive_cell == to_cell)
		{
			if (PortalAllowsTransition(&model->portals[portal_index], 1,
				previous_stance, current_stance))
				return 1;
			continue;
		}
		if (positive_cell == from_cell && negative_cell == to_cell)
		{
			if (PortalAllowsTransition(&model->portals[portal_index], 0,
				previous_stance, current_stance))
				return 1;
		}
	}
	return 0;
}

static sg_localization_status_t BuildRecoveryCandidates(
	const sg_compact_localization_binding_t *binding,
	const sg_compact_localized_state_t *previous, sg_rune_stance_t stance,
	sg_compact_localization_scratch_t *scratch)
{
	const sg_rune_compact_cell_t *cell;
	uint32_t local;

	if (!binding || !previous || !scratch || !scratch->candidates ||
		scratch->candidate_capacity < binding->model->cell_count)
		return SG_LOCALIZATION_INVALID_ARGUMENT;
	scratch->candidate_count = 0U;
	scratch->candidates[0] = previous->location.cell.value;
	scratch->candidate_count = 1U;
	cell = &binding->model->cells[previous->location.cell.value];
	if (cell->incidences.first > binding->model->cell_incidence_count ||
		cell->incidences.count > binding->model->cell_incidence_count -
			cell->incidences.first || !binding->model->cell_incidences)
		return SG_LOCALIZATION_INVALID_BINDING;
	for (local = 0U; local < cell->incidences.count; local++)
	{
		const uint32_t reference = cell->incidences.first + local;
		const uint32_t incidence_index =
			binding->model->cell_incidences[reference].value;
		const sg_rune_compact_incidence_t *incidence;
		uint32_t portal_index;
		uint32_t negative_cell;
		uint32_t positive_cell;
		uint32_t adjacent;
		int direction;
		uint32_t index;

		if (incidence_index >= binding->model->incidence_count ||
			!binding->model->incidences || !binding->model->facets)
			return SG_LOCALIZATION_INVALID_BINDING;
		incidence = &binding->model->incidences[incidence_index];
		if (incidence->cell.value != previous->location.cell.value ||
			incidence->facet.value >= binding->model->facet_count)
			return SG_LOCALIZATION_INVALID_BINDING;
		portal_index = binding->model->facets[incidence->facet.value].portal.value;
		if (portal_index == SG_RUNE_COMPACT_INDEX_NONE)
			continue;
		if (!PortalEndpointCells(binding->model, portal_index, &negative_cell,
			&positive_cell))
			return SG_LOCALIZATION_INVALID_BINDING;
		if (negative_cell == previous->location.cell.value)
		{
			adjacent = positive_cell;
			direction = 1;
		}
		else if (positive_cell == previous->location.cell.value)
		{
			adjacent = negative_cell;
			direction = 0;
		}
		else
			continue;
		if (!PortalAllowsTransition(&binding->model->portals[portal_index],
			direction, previous->stance, stance))
			continue;
		for (index = 0U; index < scratch->candidate_count; index++)
			if (scratch->candidates[index] == adjacent)
				break;
		if (index != scratch->candidate_count)
			continue;
		if (scratch->candidate_count >= scratch->candidate_capacity)
			return SG_LOCALIZATION_CAPACITY;
		scratch->candidates[scratch->candidate_count++] = adjacent;
	}
	return SG_LOCALIZATION_OK;
}

static sg_localization_status_t TryNumericRecovery(
	const sg_compact_localization_binding_t *binding,
	const sg_rune_q8_vec3_t *point,
	const sg_compact_localized_state_t *previous,
	float maximum_distance,
	int32_t radius,
	const sg_compact_localization_scratch_t *scratch,
	sg_rune_compact_location_t *location_out)
{
	sg_rune_q8_vec3_t candidate;
	uint32_t found_cell = SG_RUNE_COMPACT_INDEX_NONE;
	int ambiguous = 0;
	int32_t delta_x;
	int32_t delta_y;
	int32_t delta_z;

	if (!binding || !point || !previous || !scratch ||
		scratch->candidate_count == 0U || !location_out)
		return SG_LOCALIZATION_INVALID_ARGUMENT;
	memset(location_out, 0, sizeof(*location_out));
	location_out->cell.value = SG_RUNE_COMPACT_INDEX_NONE;
	for (delta_x = -radius; delta_x <= radius; delta_x++)
		for (delta_y = -radius; delta_y <= radius; delta_y++)
			for (delta_z = -radius; delta_z <= radius; delta_z++)
			{
				const double distance_x = (double)delta_x * 0.125;
				const double distance_y = (double)delta_y * 0.125;
				const double distance_z = (double)delta_z * 0.125;
				int64_t value;

				if (distance_x * distance_x + distance_y * distance_y +
					distance_z * distance_z >
					(double)maximum_distance * (double)maximum_distance)
					continue;
				value = (int64_t)point->value[0] + delta_x;
				if (value < INT32_MIN || value > INT32_MAX)
					continue;
				candidate.value[0] = (int32_t)value;
				value = (int64_t)point->value[1] + delta_y;
				if (value < INT32_MIN || value > INT32_MAX)
					continue;
				candidate.value[1] = (int32_t)value;
				value = (int64_t)point->value[2] + delta_z;
				if (value < INT32_MIN || value > INT32_MAX)
					continue;
				candidate.value[2] = (int32_t)value;
				{
					uint32_t cell_index;

					for (cell_index = 0U; cell_index < scratch->candidate_count;
						cell_index++)
					{
						sg_rune_compact_cell_index_t cell;
						sg_rune_compact_location_t located;
						sg_rune_compact_localize_status_t status;

						cell.value = scratch->candidates[cell_index];
						status = SG_RuneCompactLocalizeIndexed(binding->model,
							&candidate, &cell, 1U, &located);
						if (status == SG_RUNE_COMPACT_LOCALIZE_NOT_FOUND)
							continue;
						if (status != SG_RUNE_COMPACT_LOCALIZE_OK)
							return SG_LOCALIZATION_INVALID_BINDING;
						if (found_cell == SG_RUNE_COMPACT_INDEX_NONE)
						{
							found_cell = located.cell.value;
							*location_out = located;
						}
						else if (found_cell != located.cell.value)
							ambiguous = 1;
					}
				}
			}
	if (ambiguous)
		return SG_LOCALIZATION_AMBIGUOUS_INPUT;
	return found_cell == SG_RUNE_COMPACT_INDEX_NONE ?
		SG_LOCALIZATION_OUTSIDE_CONFIGURATION : SG_LOCALIZATION_OK;
}

static sg_localization_status_t LocalizeCandidates(
	const sg_compact_localization_binding_t *binding,
	const sg_rune_q8_vec3_t *point,
	const sg_compact_localization_scratch_t *scratch,
	sg_rune_compact_location_t *location_out)
{
	uint32_t found = SG_RUNE_COMPACT_INDEX_NONE;
	uint32_t index;

	if (!binding || !point || !scratch || !location_out ||
		!scratch->candidates || scratch->candidate_count == 0U)
		return SG_LOCALIZATION_INVALID_ARGUMENT;
	memset(location_out, 0, sizeof(*location_out));
	location_out->cell.value = SG_RUNE_COMPACT_INDEX_NONE;
	for (index = 0U; index < scratch->candidate_count; index++)
	{
		sg_rune_compact_cell_index_t cell;
		sg_rune_compact_location_t located;
		sg_rune_compact_localize_status_t status;

		cell.value = scratch->candidates[index];
		status = SG_RuneCompactLocalizeIndexed(binding->model, point, &cell, 1U,
			&located);
		if (status == SG_RUNE_COMPACT_LOCALIZE_NOT_FOUND)
			continue;
		if (status != SG_RUNE_COMPACT_LOCALIZE_OK)
			return SG_LOCALIZATION_INVALID_BINDING;
		if (found != SG_RUNE_COMPACT_INDEX_NONE && found != located.cell.value)
			return SG_LOCALIZATION_AMBIGUOUS_INPUT;
		found = located.cell.value;
		*location_out = located;
	}
	return found == SG_RUNE_COMPACT_INDEX_NONE ?
		SG_LOCALIZATION_OUTSIDE_CONFIGURATION : SG_LOCALIZATION_OK;
}

static sg_localization_status_t SpatialCandidates(
	const sg_compact_localization_binding_t *binding, const float position[3],
	sg_compact_localization_scratch_t *scratch)
{
	sg_rune_compact_spatial_error_t error;
	sg_rune_vec3_t point;

	if (!binding || !position || !scratch || !scratch->candidates ||
		scratch->candidate_capacity < binding->model->cell_count)
		return SG_LOCALIZATION_INVALID_ARGUMENT;
	point.value[0] = position[0];
	point.value[1] = position[1];
	point.value[2] = position[2];
	scratch->candidate_count = 0U;
	memset(&error, 0, sizeof(error));
	if (!SG_RuneCompactSpatialIndexQueryCells(binding->spatial_index, &point,
		scratch->candidates, scratch->candidate_capacity,
		&scratch->candidate_count, &error))
		return error.code == SG_RUNE_COMPACT_SPATIAL_ERROR_NOT_FOUND ?
			SG_LOCALIZATION_OUTSIDE_CONFIGURATION :
			error.code == SG_RUNE_COMPACT_SPATIAL_ERROR_INSUFFICIENT_CAPACITY ?
				SG_LOCALIZATION_CAPACITY : SG_LOCALIZATION_INVALID_BINDING;
	return scratch->candidate_count == 0U ?
		SG_LOCALIZATION_OUTSIDE_CONFIGURATION : SG_LOCALIZATION_OK;
}

sg_localization_status_t SG_CompactLocalizationBind(
	sg_compact_localization_binding_t *binding,
	const sg_rune_compact_model_t *model,
	const sg_rune_compact_identity_t *expected_identity,
	const sg_rune_compact_spatial_index_t *spatial_index,
	const sg_compact_localization_observation_owner_t *observation_owner,
	const sg_host_law_runtime_authority_t *host_authority,
	uint64_t rune_identity, uint64_t topology_revision)
{
	sg_rune_compact_spatial_counts_t spatial_counts;
	sg_rune_compact_spatial_error_t spatial_error;

	if (binding)
		memset(binding, 0, sizeof(*binding));
	if (!binding || !CompactModelShapeValid(model) || !expected_identity ||
		!spatial_index || !observation_owner || !observation_owner->validate ||
		!host_authority || rune_identity == 0U || topology_revision == 0U)
		return SG_LOCALIZATION_INVALID_ARGUMENT;
	if (!CompactIdentityEqual(&model->identity, expected_identity))
		return SG_LOCALIZATION_IDENTITY_MISMATCH;
	if (!HostLawMatchesIdentity(host_authority, expected_identity))
		return SG_LOCALIZATION_IDENTITY_MISMATCH;
	memset(&spatial_counts, 0, sizeof(spatial_counts));
	memset(&spatial_error, 0, sizeof(spatial_error));
	if (!SG_RuneCompactSpatialIndexCounts(spatial_index, &spatial_counts,
		&spatial_error) || spatial_counts.cell_count != model->cell_count)
		return SG_LOCALIZATION_IDENTITY_MISMATCH;
	binding->model = model;
	binding->spatial_index = spatial_index;
	binding->identity = *expected_identity;
	binding->host_authority = *host_authority;
	binding->observation_owner = *observation_owner;
	binding->rune_identity = rune_identity;
	binding->topology_revision = topology_revision;
	binding->bound = 1U;
	return SG_LOCALIZATION_OK;
}

void SG_CompactLocalizationUnbind(sg_compact_localization_binding_t *binding)
{
	if (binding)
		memset(binding, 0, sizeof(*binding));
}

int SG_CompactLocalizationBindingCurrent(
	const sg_compact_localization_binding_t *binding)
{
	sg_host_law_result_t host_result;

	if (!BindingShapeValid(binding))
		return 0;
	host_result = SG_HostLawProductionAuthorityCurrent(
		&binding->host_authority);
	return host_result.status == SG_HOST_LAW_OK;
}

int SG_CompactLocalizationStateCurrent(
	const sg_compact_localization_binding_t *binding,
	const sg_localization_subject_t *subject,
	const sg_compact_localized_state_t *state)
{
	return SG_CompactLocalizationBindingCurrent(binding) &&
		PreviousStateShapeValid(binding, subject, state);
}

sg_localization_status_t SG_CompactLocalizationObserveWithScratch(
	const sg_compact_localization_binding_t *binding,
	const sg_compact_localization_sample_t *sample,
	const sg_compact_localized_state_t *previous,
	sg_compact_localization_scratch_t *scratch,
	sg_compact_localized_state_t *state_out)
{
	sg_compact_localization_observation_view_t view;
	const sg_host_pmove_result_t *pmove_result;
	const sg_host_pmove_state_observation_t *state_observation;
	const float *position;
	const float *velocity;
	sg_host_collision_pose_t pose;
	sg_rune_compact_location_t location;
	sg_rune_q8_vec3_t point;
	sg_rune_stance_t stance;
	sg_localization_recovery_t recovery = SG_LOCALIZATION_RECOVERY_NONE;
	sg_host_law_result_t host_result;
	int has_previous;
	int continuity;
	int32_t recovery_radius;
	sg_localization_status_t localization_status;

	ClearState(state_out);
	if (scratch)
		scratch->candidate_count = 0U;
	if (!state_out || !BindingShapeValid(binding) ||
		!SG_CompactLocalizationBindingCurrent(binding) || !sample || !scratch ||
		!sample->observation || !scratch->candidates ||
		scratch->candidate_capacity < binding->model->cell_count)
		return SG_LOCALIZATION_INVALID_ARGUMENT;
	memset(&view, 0, sizeof(view));
	localization_status = binding->observation_owner.validate(
		binding->observation_owner.context, &binding->host_authority,
		sample->observation, &view);
	if (localization_status != SG_LOCALIZATION_OK)
		return localization_status;
	if (!SubjectValid(&view.subject) || view.frame_sequence == 0U ||
		view.observed_at_ms == 0U ||
		view.host_authority_epoch != binding->host_authority.epoch ||
		view.kind < SG_LOCALIZATION_OBSERVATION_PRESENT ||
		view.kind >= SG_LOCALIZATION_OBSERVATION_KIND_COUNT ||
		!isfinite(view.maximum_recovery_distance) ||
		view.maximum_recovery_distance < 0.0f)
		return SG_LOCALIZATION_UNAUTHENTICATED;
	if (!ModelStampMatchesBinding(binding, &view.model_stamp,
		view.frame_sequence))
		return SG_LOCALIZATION_IDENTITY_MISMATCH;
	if (!RecoveryRadius(view.maximum_recovery_distance,
		&recovery_radius))
		return SG_LOCALIZATION_RECOVERY_PARAMETER;
	if (!LifeResetShapeValid(&view))
		return SG_LOCALIZATION_RECOVERY_REJECTED;
	host_result = SG_HostLawProductionSubjectCurrent(
		&binding->host_authority, &view.subject);
	if (host_result.status != SG_HOST_LAW_OK)
		return SG_LOCALIZATION_UNAUTHENTICATED;
	has_previous = previous != NULL;
	{
		sg_localization_status_t lifecycle_status = PreviousLifecycleStatus(
			binding, &view, previous);

		if (lifecycle_status != SG_LOCALIZATION_OK)
			return lifecycle_status;
	}
	if (view.kind == SG_LOCALIZATION_OBSERVATION_DEAD)
	{
		if (view.pmove_result || view.state_observation ||
			view.maximum_recovery_distance != 0.0f ||
			view.maximum_temporary_absence_ms != 0U)
			return SG_LOCALIZATION_RECOVERY_PARAMETER;
		state_out->subject = view.subject;
		state_out->model_stamp = view.model_stamp;
		state_out->rune_identity = binding->rune_identity;
		state_out->topology_revision = binding->topology_revision;
		state_out->frame_sequence = view.frame_sequence;
		state_out->localized_at_ms = view.observed_at_ms;
		state_out->presence = SG_LOCALIZATION_PRESENCE_DEAD;
		state_out->recovery = SG_LOCALIZATION_RECOVERY_NONE;
		state_out->valid = 1U;
		return SG_LOCALIZATION_OK;
	}
	if (view.kind == SG_LOCALIZATION_OBSERVATION_TEMPORARILY_ABSENT)
	{
		const uint64_t absence_started_at_ms = previous &&
			previous->presence == SG_LOCALIZATION_PRESENCE_TEMPORARILY_ABSENT ?
			previous->absence_started_at_ms : view.observed_at_ms;

		if (!has_previous ||
			!PreviousStateShapeValid(binding, &view.subject, previous) ||
			view.pmove_result || view.state_observation ||
			view.maximum_recovery_distance != 0.0f ||
			view.maximum_temporary_absence_ms == 0U ||
			view.frame_sequence <= previous->frame_sequence ||
			view.observed_at_ms < previous->localized_at_ms ||
			view.observed_at_ms < absence_started_at_ms ||
			view.observed_at_ms - absence_started_at_ms >
				view.maximum_temporary_absence_ms)
			return SG_LOCALIZATION_RECOVERY_REJECTED;
		if (previous->motion != SG_RUNE_MOTION_SUPPORTED ||
			previous->support != SG_RUNE_SUPPORT_SUPPORTED ||
			previous->medium != SG_RUNE_MEDIUM_DRY ||
			previous->water_level != 0U || previous->water_type != 0U ||
			previous->reference_frame != SG_RUNE_FRAME_WORLD ||
			(previous->host_state.pm_flags & PMF_ON_GROUND) == 0U ||
			previous->host_state.pm_time != 0 ||
			previous->velocity[0] != 0.0f || previous->velocity[1] != 0.0f ||
			previous->velocity[2] != 0.0f)
			return SG_LOCALIZATION_RECOVERY_REJECTED;
		*state_out = *previous;
		state_out->model_stamp = view.model_stamp;
		state_out->frame_sequence = view.frame_sequence;
		state_out->localized_at_ms = view.observed_at_ms;
		state_out->absence_started_at_ms = absence_started_at_ms;
		state_out->presence = SG_LOCALIZATION_PRESENCE_TEMPORARILY_ABSENT;
		state_out->recovery = SG_LOCALIZATION_RECOVERY_TEMPORARY_ABSENCE;
		return SG_LOCALIZATION_OK;
	}
	if (view.maximum_temporary_absence_ms != 0U ||
		((view.pmove_result != NULL) == (view.state_observation != NULL)))
		return SG_LOCALIZATION_INVALID_ARGUMENT;
	if (view.kind == SG_LOCALIZATION_OBSERVATION_PRESENT && !view.pmove_result)
		return SG_LOCALIZATION_INVALID_ARGUMENT;
	if (view.kind == SG_LOCALIZATION_OBSERVATION_NEW_SPAWN &&
		!view.pmove_result && !view.state_observation)
		return SG_LOCALIZATION_INVALID_ARGUMENT;
	if (view.kind == SG_LOCALIZATION_OBSERVATION_TELEPORTED && !view.pmove_result)
		return SG_LOCALIZATION_INVALID_ARGUMENT;
	if (view.kind != SG_LOCALIZATION_OBSERVATION_NEW_SPAWN &&
		view.state_observation)
		return SG_LOCALIZATION_INVALID_ARGUMENT;
	if ((view.kind == SG_LOCALIZATION_OBSERVATION_TELEPORTED ||
			view.kind == SG_LOCALIZATION_OBSERVATION_NEW_SPAWN) &&
		view.maximum_recovery_distance != 0.0f)
		return SG_LOCALIZATION_RECOVERY_PARAMETER;
	continuity = view.kind == SG_LOCALIZATION_OBSERVATION_PRESENT &&
		has_previous;
	pmove_result = view.pmove_result;
	state_observation = view.state_observation;
	if (pmove_result && !PmoveResultValid(binding, pmove_result))
		return SG_LOCALIZATION_UNAUTHENTICATED;
	if (state_observation && !StateObservationValid(binding, state_observation))
		return SG_LOCALIZATION_UNAUTHENTICATED;
	position = pmove_result ? pmove_result->origin : state_observation->origin;
	velocity = pmove_result ? pmove_result->velocity : state_observation->velocity;
	stance = (pmove_result ? pmove_result->state.pm_flags :
		state_observation->state.pm_flags) & PMF_DUCKED ?
		SG_RUNE_STANCE_CROUCHING : SG_RUNE_STANCE_STANDING;
	memset(&pose, 0, sizeof(pose));
	host_result = SG_HostLawProductionSubjectClassifyPose(
		&binding->host_authority, &view.subject, position, stance, &pose);
	if (host_result.status != SG_HOST_LAW_OK)
		return SG_LOCALIZATION_UNAUTHENTICATED;
	if ((pose.valid != 0 && pose.valid != 1) ||
		(pose.supported != 0 && pose.supported != 1) ||
		(pose.support_is_mover != 0 && pose.support_is_mover != 1) ||
		pose.water_level > 3U)
		return SG_LOCALIZATION_UNAUTHENTICATED;
	if (!pose.valid)
		return SG_LOCALIZATION_SOLID;
	if (pose.physics_abi_id != binding->identity.physics_abi_id ||
		!FloatBitsEqual(pose.gravity, binding->identity.physics.gravity_bits))
		return SG_LOCALIZATION_IDENTITY_MISMATCH;
	if (pmove_result && !StateFactsMatchResult(&pose, pmove_result))
		return SG_LOCALIZATION_RECOVERY_REJECTED;
	if (!PointToQ8(position, &point))
		return SG_LOCALIZATION_NONFINITE;
	if (continuity)
	{
		localization_status = BuildRecoveryCandidates(binding, previous, stance,
			scratch);
		if (localization_status != SG_LOCALIZATION_OK)
			return localization_status;
	}
	else
	{
		localization_status = SpatialCandidates(binding, position, scratch);
		if (localization_status != SG_LOCALIZATION_OK)
			return localization_status;
	}
	localization_status = LocalizeCandidates(binding, &point, scratch, &location);
	if (localization_status == SG_LOCALIZATION_OUTSIDE_CONFIGURATION &&
		continuity && view.maximum_recovery_distance != 0.0f)
	{
		localization_status = TryNumericRecovery(binding, &point, previous,
			view.maximum_recovery_distance, recovery_radius, scratch, &location);
		if (localization_status == SG_LOCALIZATION_OK)
			recovery = SG_LOCALIZATION_RECOVERY_NUMERIC_DRIFT;
	}
	if (localization_status != SG_LOCALIZATION_OK)
		return localization_status;
	if (location.cell.value >= binding->model->cell_count ||
		(binding->model->cells[location.cell.value].semantics &
			SG_RUNE_COMPACT_CELL_MOVER_VOLUME) != 0U)
		return SG_LOCALIZATION_MOVER_UNBOUND;
	if ((location.valid_stances & (sg_rune_stance_validity_t)(
			stance == SG_RUNE_STANCE_CROUCHING ?
			SG_RUNE_STANCE_VALID_CROUCHING : SG_RUNE_STANCE_VALID_STANDING)) == 0U)
		return SG_LOCALIZATION_OUTSIDE_CONFIGURATION;
	if (continuity)
	{
		const int transition = CellTransitionAllowed(binding->model,
			previous->location.cell.value, location.cell.value, previous->stance,
			stance);

		if (transition < 0)
			return SG_LOCALIZATION_INVALID_BINDING;
		if (transition == 0)
			return SG_LOCALIZATION_RECOVERY_REJECTED;
	}
	state_out->subject = view.subject;
	state_out->model_stamp = view.model_stamp;
	state_out->rune_identity = binding->rune_identity;
	state_out->topology_revision = binding->topology_revision;
	state_out->frame_sequence = view.frame_sequence;
	state_out->localized_at_ms = view.observed_at_ms;
	state_out->location = location;
	state_out->stance = stance;
	state_out->motion = pose.water_level >= 2U ? SG_RUNE_MOTION_SWIMMING :
		pose.supported ? SG_RUNE_MOTION_SUPPORTED : SG_RUNE_MOTION_AIRBORNE;
	state_out->support = pose.water_level >= 2U ? SG_RUNE_SUPPORT_NONE :
		pose.supported ? (pose.support_is_mover ? SG_RUNE_SUPPORT_MOVER :
			SG_RUNE_SUPPORT_SUPPORTED) : SG_RUNE_SUPPORT_NONE;
	state_out->medium = pose.water_level == 0U ? SG_RUNE_MEDIUM_DRY :
		(pose.water_type & SG_HOST_CONTENTS_LAVA) != 0U ? SG_RUNE_MEDIUM_LAVA :
		(pose.water_type & SG_HOST_CONTENTS_SLIME) != 0U ? SG_RUNE_MEDIUM_SLIME :
		SG_RUNE_MEDIUM_WATER;
	state_out->void_relation = (binding->model->cells[location.cell.value].semantics &
		SG_RUNE_COMPACT_CELL_VOID_BOUNDARY) != 0U ? SG_RUNE_VOID_ADJACENT :
		SG_RUNE_VOID_CLEAR;
	state_out->reference_frame = state_out->support == SG_RUNE_SUPPORT_MOVER ?
		SG_RUNE_FRAME_MOVER_RELATIVE : SG_RUNE_FRAME_WORLD;
	state_out->support_model_index = state_out->support == SG_RUNE_SUPPORT_NONE ?
		SG_LOCALIZATION_SUPPORT_MODEL_NONE : pose.support.model_index;
	state_out->support_instance_id = state_out->support == SG_RUNE_SUPPORT_NONE ?
		0U : pose.support.instance_id;
	state_out->water_level = pose.water_level;
	state_out->water_type = pose.water_type;
	memcpy(state_out->position, position, sizeof(state_out->position));
	memcpy(state_out->velocity, velocity, sizeof(state_out->velocity));
	state_out->host_state = pmove_result ? pmove_result->state :
		state_observation->state;
	state_out->presence = SG_LOCALIZATION_PRESENCE_PRESENT;
	state_out->recovery = continuity && recovery == SG_LOCALIZATION_RECOVERY_NONE ?
		SG_LOCALIZATION_RECOVERY_EXACT_CONTINUITY : recovery;
	state_out->valid = 1U;
	return SG_LOCALIZATION_OK;
}
