#include "sg_cell_phase_localization.h"

#include <math.h>
#include <string.h>

static void SetStatus(sg_localization_status_t *status_out,
	sg_localization_status_t status)
{
	if (status_out)
		*status_out = status;
}

static int ZeroBytes(const uint8_t *bytes, size_t count)
{
	size_t index;

	for (index = 0U; index < count; index++)
		if (bytes[index] != 0U)
			return 0;
	return 1;
}

static int Finite3(const float value[3])
{
	return value && isfinite(value[0]) && isfinite(value[1]) &&
		isfinite(value[2]);
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
	return left && right &&
		left->bsp_content_id == right->bsp_content_id &&
		left->entity_semantics_id == right->entity_semantics_id &&
		left->physics_abi_id == right->physics_abi_id &&
		left->source_set_identity == right->source_set_identity &&
		left->schema_id == right->schema_id &&
		left->producer_identity == right->producer_identity &&
		HullEqual(&left->standing_hull, &right->standing_hull) &&
		HullEqual(&left->crouching_hull, &right->crouching_hull) &&
		PhysicsEqual(&left->physics, &right->physics);
}

static int StableIdCompare(const sg_rune_stable_id_t *left,
	const sg_rune_stable_id_t *right)
{
	if (left->source_set_identity != right->source_set_identity)
		return left->source_set_identity < right->source_set_identity ? -1 : 1;
	if (left->high != right->high)
		return left->high < right->high ? -1 : 1;
	if (left->low != right->low)
		return left->low < right->low ? -1 : 1;
	return 0;
}

static int FindRuntimeCell(const sg_rune_model_t *model,
	const sg_rune_cell_ref_t *reference, uint32_t *cell_out,
	uint64_t *comparisons)
{
	uint32_t first = 0U;
	uint32_t last;

	if (!model || !reference || !cell_out ||
		!SG_RuneModelStableIdValid(&reference->value))
		return 0;
	last = model->cell_count;
	while (first < last)
	{
		uint32_t middle = first + (last - first) / 2U;
		int comparison;

		if (comparisons)
			(*comparisons)++;
		comparison = StableIdCompare(&model->cells[middle].id.value,
			&reference->value);

		if (comparison == 0)
		{
			*cell_out = middle;
			return 1;
		}
		if (comparison < 0)
			first = middle + 1U;
		else
			last = middle;
	}
	return 0;
}

static int FindRuntimePhase(const sg_rune_model_t *model,
	const sg_rune_phase_ref_t *reference, uint32_t *phase_out,
	uint64_t *comparisons)
{
	uint32_t first = 0U;
	uint32_t last;

	if (!model || !reference || !phase_out ||
		!SG_RuneModelStableIdValid(&reference->value))
		return 0;
	last = model->phase_count;
	while (first < last)
	{
		uint32_t middle = first + (last - first) / 2U;
		int comparison;

		if (comparisons)
			(*comparisons)++;
		comparison = StableIdCompare(&model->phases[middle].id.value,
			&reference->value);
		if (comparison == 0)
		{
			*phase_out = middle;
			return 1;
		}
		if (comparison < 0)
			first = middle + 1U;
		else
			last = middle;
	}
	return 0;
}

static int FindMechanism(const sg_rune_model_t *model,
	const sg_rune_mechanism_ref_t *reference, uint32_t *mechanism_out)
{
	uint32_t first = 0U;
	uint32_t last;

	if (!model || !reference ||
		!SG_RuneModelStableIdValid(&reference->value))
		return 0;
	last = model->mechanism_count;
	while (first < last)
	{
		uint32_t middle = first + (last - first) / 2U;
		int comparison = StableIdCompare(
			&model->mechanisms[middle].id.value, &reference->value);

		if (comparison == 0)
		{
			if (mechanism_out)
				*mechanism_out = middle;
			return 1;
		}
		if (comparison < 0)
			first = middle + 1U;
		else
			last = middle;
	}
	return 0;
}

static const sg_localization_mover_binding_t *FindMoverBinding(
	const sg_cell_phase_locator_t *locator, uint64_t instance_id)
{
	size_t first = 0U;
	size_t last = locator->mover_binding_count;

	while (first < last)
	{
		size_t middle = first + (last - first) / 2U;
		uint64_t candidate = locator->mover_bindings[middle].instance_id;

		if (candidate == instance_id)
			return &locator->mover_bindings[middle];
		if (candidate < instance_id)
			first = middle + 1U;
		else
			last = middle;
	}
	return NULL;
}

static int EntityEqual(const sg_rune_entity_ref_t *left,
	const sg_rune_entity_ref_t *right)
{
	return left && right && left->index == right->index &&
		left->spawn_ordinal == right->spawn_ordinal;
}

static int EntityNone(const sg_rune_entity_ref_t *entity)
{
	return entity && entity->index == UINT32_MAX &&
		entity->spawn_ordinal == UINT32_MAX;
}

static int BindMoverAuthority(const sg_host_collision_authority_t *authority,
	const sg_rune_model_t *model,
	const sg_localization_mover_binding_t *bindings, size_t binding_count)
{
	size_t index;

	if (binding_count != 0U && !bindings)
		return 0;
	for (index = 0U; index < binding_count; index++)
	{
		const sg_localization_mover_binding_t *binding = &bindings[index];
		const sg_rune_mechanism_t *mechanism;
		uint32_t mechanism_index;

		if (binding->instance_id == 0U || binding->model_index == 0U ||
			binding->model_index >= authority->world->model_count ||
			binding->reserved != 0U ||
			(index != 0U && bindings[index - 1U].instance_id >=
				binding->instance_id) ||
			!FindMechanism(model, &binding->mechanism, &mechanism_index))
			return 0;
		mechanism = &model->mechanisms[mechanism_index];
		if (EntityNone(&binding->entity) ||
			!EntityEqual(&binding->entity, &mechanism->entity))
			return 0;
	}
	return 1;
}

static int ConfigurationShapeValid(
	const sg_configuration_space_t *configuration)
{
	uint32_t stance;

	if (!configuration || configuration->cell_count == 0U ||
		!configuration->cells || !configuration->faces ||
		configuration->certificate_node_count == 0U ||
		!configuration->certificate_nodes)
		return 0;
	for (stance = 0U; stance < SG_RUNE_STANCE_COUNT; stance++)
		if (configuration->certificate_roots[stance] >=
			configuration->certificate_node_count)
			return 0;
	return 1;
}

static int SemanticsShapeValid(
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics)
{
	uint32_t region;

	if (!semantics || semantics->region_count == 0U || !semantics->regions ||
		!semantics->faces)
		return 0;
	for (region = 0U; region < semantics->region_count; region++)
	{
		const sg_configuration_semantic_region_t *record =
			&semantics->regions[region];

		if (record->cell >= configuration->cell_count ||
			record->face_count < 4U ||
			record->first_face > semantics->face_count ||
			record->face_count > semantics->face_count - record->first_face ||
			!Finite3(record->bounds.mins.value) ||
			!Finite3(record->bounds.maxs.value) ||
			(region != 0U && semantics->regions[region - 1U].id >= record->id) ||
			(region != 0U && semantics->regions[region - 1U].cell >
				record->cell))
			return 0;
	}
	return 1;
}

static int RuntimeShapeValid(const sg_rune_runtime_snapshot_t *snapshot)
{
	const sg_rune_model_t *model;
	uint32_t cell;
	uint32_t phase;

	if (!SG_RuneRuntimeSnapshotValid(snapshot))
		return 0;
	model = snapshot->model;
	if (model->mechanism_count != 0U && !model->mechanisms)
		return 0;
	for (phase = 0U; phase < model->phase_count; phase++)
		if (!SG_RuneModelPhaseValid(&model->phases[phase]) ||
			model->phases[phase].order.source_set_identity !=
				model->identity.source_set_identity ||
			(phase != 0U && SG_RuneModelOrderKeyCompare(
				&model->phases[phase - 1U].order,
				&model->phases[phase].order) >= 0))
			return 0;
	for (cell = 0U; cell < model->cell_count; cell++)
	{
		const sg_rune_cell_t *record = &model->cells[cell];
		sg_rune_stable_id_t expected =
			SG_RuneModelStableIdFromOrderKey(&record->order);

		if (!SG_RuneModelOrderKeyValid(&record->order) ||
			record->order.domain != SG_RUNE_ORDER_CELL ||
			record->order.source_set_identity !=
				model->identity.source_set_identity ||
			!SG_RuneModelStableIdEqual(&record->id.value, &expected) ||
			(cell != 0U && SG_RuneModelOrderKeyCompare(
				&model->cells[cell - 1U].order, &record->order) >= 0) ||
			record->phases.first > model->phase_count ||
			record->phases.count == 0U ||
			record->phases.count >
				model->phase_count - record->phases.first)
			return 0;
		for (phase = record->phases.first;
			phase < record->phases.first + record->phases.count; phase++)
			if (snapshot->phases[phase].cell_id != cell)
				return 0;
	}
	for (cell = 0U; cell < model->mechanism_count; cell++)
	{
		const sg_rune_mechanism_t *record = &model->mechanisms[cell];
		sg_rune_stable_id_t expected =
			SG_RuneModelStableIdFromOrderKey(&record->order);

		if (!SG_RuneModelOrderKeyValid(&record->order) ||
			record->order.domain != SG_RUNE_ORDER_MECHANISM ||
			record->order.source_set_identity !=
				model->identity.source_set_identity ||
			!SG_RuneModelStableIdEqual(&record->id.value, &expected) ||
			(cell != 0U && SG_RuneModelOrderKeyCompare(
				&model->mechanisms[cell - 1U].order,
				&record->order) >= 0))
			return 0;
	}
	return 1;
}

static int BindRegions(const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_localization_region_binding_t *bindings, size_t binding_count,
	sg_localization_workspace_t *workspace, sg_cell_phase_locator_t *locator)
{
	uint32_t cell;
	uint32_t region = 0U;

	if (!bindings || binding_count != semantics->region_count || !workspace ||
		!workspace->cell_region_offsets || !workspace->region_indices ||
		!workspace->region_runtime_cells || !workspace->region_runtime_regions ||
		workspace->cell_region_offset_capacity <
			(size_t)configuration->cell_count + 1U ||
		workspace->region_index_capacity < semantics->region_count ||
		workspace->region_runtime_cell_capacity < semantics->region_count ||
		workspace->region_runtime_region_capacity < semantics->region_count)
		return 0;
	for (cell = 0U; cell < configuration->cell_count; cell++)
	{
		uint32_t first = region;

		locator->prepare_cell_steps++;
		workspace->cell_region_offsets[cell] = region;
		while (region < semantics->region_count &&
			semantics->regions[region].cell == cell)
		{
			locator->prepare_region_steps++;
			workspace->region_indices[region] = region;
			region++;
		}
		if (first == region)
			return 0;
	}
	workspace->cell_region_offsets[configuration->cell_count] = region;
	if (region != semantics->region_count)
		return 0;
	for (region = 0U; region < semantics->region_count; region++)
	{
		uint32_t runtime_cell;
		const sg_localization_region_binding_t *binding = &bindings[region];

		locator->prepare_binding_checks++;
		if (binding->semantic_region_id != semantics->regions[region].id ||
			!FindRuntimeCell(snapshot->model, &binding->rune_cell,
				&runtime_cell, &locator->prepare_runtime_cell_comparisons))
			return 0;
		if (binding->reserved != 0U ||
			binding->runtime_region >= snapshot->region_count)
			return 0;
		workspace->region_runtime_cells[region] = runtime_cell;
		workspace->region_runtime_regions[region] = binding->runtime_region;
	}
	return 1;
}

static int ConfigurationPortalValid(
	const sg_configuration_space_t *configuration,
	const sg_configuration_portal_t *portal)
{
	uint32_t vertex;

	if (!portal || portal->from_cell >= configuration->cell_count ||
		portal->to_cell >= configuration->cell_count ||
		portal->from_cell == portal->to_cell ||
		portal->stance < SG_RUNE_STANCE_STANDING ||
		portal->stance >= SG_RUNE_STANCE_COUNT ||
		configuration->cells[portal->from_cell].stance != portal->stance ||
		configuration->cells[portal->to_cell].stance != portal->stance ||
		portal->vertex_count < 3U || !configuration->vertices ||
		portal->first_vertex > configuration->vertex_count ||
		portal->vertex_count >
			configuration->vertex_count - portal->first_vertex ||
		!Finite3(portal->plane.normal) ||
		(portal->plane.normal[0] == 0.0f &&
		 portal->plane.normal[1] == 0.0f &&
		 portal->plane.normal[2] == 0.0f) ||
		!isfinite(portal->plane.distance) ||
		!isfinite(portal->clearance) || portal->clearance <= 0.0f)
		return 0;
	for (vertex = 0U; vertex < portal->vertex_count; vertex++)
		if (!Finite3(configuration->vertices[
			portal->first_vertex + vertex].value))
			return 0;
	return 1;
}

static int BindConfigurationPortals(
	const sg_configuration_space_t *configuration,
	sg_localization_workspace_t *workspace, sg_cell_phase_locator_t *locator)
{
	size_t adjacency_count;
	uint32_t cell;
	uint32_t portal_index;

	if (configuration->portal_count > UINT32_MAX / 2U)
		return 0;
	if (configuration->portal_count != 0U && !configuration->portals)
		return 0;
	adjacency_count = (size_t)configuration->portal_count * 2U;
	if (!workspace->cell_portal_offsets || !workspace->cell_portal_cursors ||
		workspace->cell_portal_offset_capacity <
			(size_t)configuration->cell_count + 1U ||
		workspace->cell_portal_cursor_capacity < configuration->cell_count ||
		(adjacency_count != 0U && (!workspace->portal_indices ||
			workspace->portal_index_capacity < adjacency_count)))
		return 0;
	for (cell = 0U; cell <= configuration->cell_count; cell++)
		workspace->cell_portal_offsets[cell] = 0U;
	for (portal_index = 0U; portal_index < configuration->portal_count;
		portal_index++)
	{
		const sg_configuration_portal_t *portal =
			&configuration->portals[portal_index];

		locator->prepare_portal_steps++;
		if (!ConfigurationPortalValid(configuration, portal))
			return 0;
		workspace->cell_portal_offsets[portal->from_cell + 1U]++;
		workspace->cell_portal_offsets[portal->to_cell + 1U]++;
	}
	for (cell = 0U; cell < configuration->cell_count; cell++)
	{
		workspace->cell_portal_offsets[cell + 1U] +=
			workspace->cell_portal_offsets[cell];
		workspace->cell_portal_cursors[cell] =
			workspace->cell_portal_offsets[cell];
	}
	for (portal_index = 0U; portal_index < configuration->portal_count;
		portal_index++)
	{
		const sg_configuration_portal_t *portal =
			&configuration->portals[portal_index];

		workspace->portal_indices[
			workspace->cell_portal_cursors[portal->from_cell]++] = portal_index;
		workspace->portal_indices[
			workspace->cell_portal_cursors[portal->to_cell]++] = portal_index;
		locator->prepare_portal_adjacency_steps += 2U;
	}
	return 1;
}

static int StanceOverlapValid(const sg_configuration_space_t *configuration,
	const sg_configuration_stance_overlap_t *overlap)
{
	return overlap && overlap->standing_cell < configuration->cell_count &&
		overlap->crouching_cell < configuration->cell_count &&
		configuration->cells[overlap->standing_cell].stance ==
			SG_RUNE_STANCE_STANDING &&
		configuration->cells[overlap->crouching_cell].stance ==
			SG_RUNE_STANCE_CROUCHING && overlap->face_count >= 4U &&
		overlap->first_face <= configuration->face_count &&
		overlap->face_count <=
			configuration->face_count - overlap->first_face &&
		Finite3(overlap->bounds.mins.value) &&
		Finite3(overlap->bounds.maxs.value) &&
		Finite3(overlap->interior_witness.value);
}

static int BindStanceOverlaps(const sg_configuration_space_t *configuration,
	sg_localization_workspace_t *workspace)
{
	size_t adjacency_count;
	uint32_t cell;
	uint32_t overlap_index;

	if (configuration->stance_overlap_count > UINT32_MAX / 2U ||
		(configuration->stance_overlap_count != 0U &&
		 !configuration->stance_overlaps))
		return 0;
	adjacency_count = (size_t)configuration->stance_overlap_count * 2U;
	if (!workspace->stance_overlap_offsets ||
		!workspace->stance_overlap_cursors ||
		workspace->stance_overlap_offset_capacity <
			(size_t)configuration->cell_count + 1U ||
		workspace->stance_overlap_cursor_capacity < configuration->cell_count ||
		(adjacency_count != 0U && (!workspace->stance_overlap_indices ||
		 workspace->stance_overlap_index_capacity < adjacency_count)))
		return 0;
	for (cell = 0U; cell <= configuration->cell_count; cell++)
		workspace->stance_overlap_offsets[cell] = 0U;
	for (overlap_index = 0U;
		overlap_index < configuration->stance_overlap_count; overlap_index++)
	{
		const sg_configuration_stance_overlap_t *overlap =
			&configuration->stance_overlaps[overlap_index];

		if (!StanceOverlapValid(configuration, overlap))
			return 0;
		workspace->stance_overlap_offsets[overlap->standing_cell + 1U]++;
		workspace->stance_overlap_offsets[overlap->crouching_cell + 1U]++;
	}
	for (cell = 0U; cell < configuration->cell_count; cell++)
	{
		workspace->stance_overlap_offsets[cell + 1U] +=
			workspace->stance_overlap_offsets[cell];
		workspace->stance_overlap_cursors[cell] =
			workspace->stance_overlap_offsets[cell];
	}
	for (overlap_index = 0U;
		overlap_index < configuration->stance_overlap_count; overlap_index++)
	{
		const sg_configuration_stance_overlap_t *overlap =
			&configuration->stance_overlaps[overlap_index];

		workspace->stance_overlap_indices[
			workspace->stance_overlap_cursors[overlap->standing_cell]++] =
			overlap_index;
		workspace->stance_overlap_indices[
			workspace->stance_overlap_cursors[overlap->crouching_cell]++] =
			overlap_index;
	}
	return 1;
}

static int IntervalEqual(const sg_rune_interval_t *left,
	const sg_rune_interval_t *right)
{
	return left->min_value == right->min_value &&
		left->max_value == right->max_value;
}

static int VelocityIntervalsEqual(const sg_rune_phase_basis_t *left,
	const sg_rune_phase_basis_t *right)
{
	return IntervalEqual(&left->velocity.x, &right->velocity.x) &&
		IntervalEqual(&left->velocity.y, &right->velocity.y) &&
		IntervalEqual(&left->velocity.z, &right->velocity.z);
}

static int PhaseDiscreteEqual(const sg_rune_phase_basis_t *left,
	const sg_rune_phase_basis_t *right)
{
	return left->stance == right->stance && left->motion == right->motion &&
		left->support == right->support && left->medium == right->medium &&
		left->void_relation == right->void_relation &&
		left->reference_frame == right->reference_frame &&
		SG_RuneModelStableIdEqual(&left->mover.value, &right->mover.value);
}

static int PhaseClockEqual(const sg_rune_phase_basis_t *left,
	const sg_rune_phase_basis_t *right)
{
	return left->time_quantum_ms == right->time_quantum_ms &&
		left->time_horizon_ms == right->time_horizon_ms;
}

static int PhaseTransitionSemanticsValid(
	const sg_rune_phase_transition_t *transition,
	const sg_rune_phase_basis_t *source,
	const sg_rune_phase_basis_t *destination)
{
	int discrete = PhaseDiscreteEqual(source, destination);
	int clock = PhaseClockEqual(source, destination);
	int velocity = VelocityIntervalsEqual(source, destination);
	int elapsed = IntervalEqual(&source->elapsed_ms, &destination->elapsed_ms);

	if (source->medium != destination->medium)
		return 0;
	switch (transition->kind)
	{
	case SG_RUNE_PHASE_TRANSITION_STANCE:
		return source->stance != destination->stance &&
			source->motion == destination->motion &&
			source->support == destination->support &&
			source->medium == destination->medium &&
			source->void_relation == destination->void_relation &&
			source->reference_frame == destination->reference_frame &&
			SG_RuneModelStableIdEqual(&source->mover.value,
				&destination->mover.value) && velocity && elapsed && clock;
	case SG_RUNE_PHASE_TRANSITION_ACCELERATION:
		return discrete && clock && !velocity && elapsed;
	case SG_RUNE_PHASE_TRANSITION_TIME:
		return discrete && clock && velocity && !elapsed;
	case SG_RUNE_PHASE_TRANSITION_MOVER_DWELL:
		return discrete && clock && source->support == SG_RUNE_SUPPORT_MOVER &&
			velocity && !elapsed;
	case SG_RUNE_PHASE_TRANSITION_TAKEOFF:
		return source->motion == SG_RUNE_MOTION_SUPPORTED &&
			source->support != SG_RUNE_SUPPORT_NONE &&
			destination->motion == SG_RUNE_MOTION_AIRBORNE &&
			destination->support == SG_RUNE_SUPPORT_NONE &&
			source->stance == destination->stance &&
			source->void_relation == destination->void_relation && clock &&
			destination->reference_frame == SG_RUNE_FRAME_WORLD &&
			SG_RuneModelStableIdEqual(&destination->mover.value,
				&SG_RUNE_MECHANISM_REF_NONE.value);
	case SG_RUNE_PHASE_TRANSITION_RELAUNCH:
		return source->motion == SG_RUNE_MOTION_AIRBORNE &&
			destination->motion == SG_RUNE_MOTION_AIRBORNE && discrete && clock &&
			(!velocity || !elapsed);
	case SG_RUNE_PHASE_TRANSITION_SUPPORT:
		return source->motion == SG_RUNE_MOTION_AIRBORNE &&
			source->support == SG_RUNE_SUPPORT_NONE &&
			destination->motion == SG_RUNE_MOTION_SUPPORTED &&
			destination->support != SG_RUNE_SUPPORT_NONE &&
			source->stance == destination->stance &&
			source->void_relation == destination->void_relation && clock;
	case SG_RUNE_PHASE_TRANSITION_NONE:
	case SG_RUNE_PHASE_TRANSITION_KIND_COUNT:
		return 0;
	}
	return 0;
}

static int TransitionRecordValid(const sg_rune_model_t *model,
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_rune_phase_transition_t *transition,
	const sg_rune_order_key_t *previous, uint32_t *source_out,
	uint32_t *destination_out, uint64_t *comparisons)
{
	sg_rune_stable_id_t expected;
	uint32_t cell;

	if (!transition || !SG_RuneModelOrderKeyValid(&transition->order) ||
		transition->order.domain != SG_RUNE_ORDER_PHASE_TRANSITION ||
		transition->order.source_set_identity !=
			model->identity.source_set_identity ||
		(previous && SG_RuneModelOrderKeyCompare(previous,
			&transition->order) >= 0))
		return 0;
	expected = SG_RuneModelStableIdFromOrderKey(&transition->order);
	if (!SG_RuneModelStableIdEqual(&expected, &transition->id.value) ||
		transition->kind <= SG_RUNE_PHASE_TRANSITION_NONE ||
		transition->kind >= SG_RUNE_PHASE_TRANSITION_KIND_COUNT ||
		!isfinite(transition->duration_ms.min_value) ||
		!isfinite(transition->duration_ms.max_value) ||
		transition->duration_ms.min_value < 0.0f ||
		transition->duration_ms.max_value < transition->duration_ms.min_value ||
		transition->duration_ms.max_value <= 0.0f || transition->flags != 0U ||
		!FindRuntimeCell(model, &transition->cell, &cell, comparisons) ||
		!FindRuntimePhase(model, &transition->source_phase, source_out,
			comparisons) ||
		!FindRuntimePhase(model, &transition->destination_phase,
			destination_out, comparisons) || *source_out == *destination_out ||
		snapshot->phases[*source_out].cell_id != cell ||
		snapshot->phases[*destination_out].cell_id != cell ||
		!PhaseTransitionSemanticsValid(transition,
			&model->phases[*source_out], &model->phases[*destination_out]))
		return 0;
	return 1;
}

static int BindPhaseTransitions(const sg_rune_runtime_snapshot_t *snapshot,
	sg_localization_workspace_t *workspace, sg_cell_phase_locator_t *locator)
{
	const sg_rune_model_t *model = snapshot->model;
	const sg_rune_order_key_t *previous = NULL;
	uint32_t transition_index;
	uint32_t phase;

	if (!workspace->phase_transition_offsets ||
		!workspace->phase_transition_cursors ||
		workspace->phase_transition_offset_capacity <
			(size_t)model->phase_count + 1U ||
		workspace->phase_transition_cursor_capacity < model->phase_count ||
		(model->phase_transition_count != 0U &&
			(!model->phase_transitions || !workspace->phase_transition_indices ||
			 workspace->phase_transition_index_capacity <
				model->phase_transition_count)))
		return 0;
	for (phase = 0U; phase <= model->phase_count; phase++)
		workspace->phase_transition_offsets[phase] = 0U;
	for (transition_index = 0U;
		transition_index < model->phase_transition_count; transition_index++)
	{
		const sg_rune_phase_transition_t *transition =
			&model->phase_transitions[transition_index];
		uint32_t source;
		uint32_t destination;

		locator->prepare_phase_transition_steps++;
		if (!TransitionRecordValid(model, snapshot, transition, previous,
			&source, &destination,
			&locator->prepare_transition_lookup_comparisons))
			return 0;
		(void)destination;
		workspace->phase_transition_offsets[source + 1U]++;
		previous = &transition->order;
	}
	for (phase = 0U; phase < model->phase_count; phase++)
	{
		workspace->phase_transition_offsets[phase + 1U] +=
			workspace->phase_transition_offsets[phase];
		workspace->phase_transition_cursors[phase] =
			workspace->phase_transition_offsets[phase];
	}
	for (transition_index = 0U;
		transition_index < model->phase_transition_count; transition_index++)
	{
		uint32_t source;

		if (!FindRuntimePhase(model,
			&model->phase_transitions[transition_index].source_phase,
			&source, &locator->prepare_transition_lookup_comparisons))
			return 0;
		workspace->phase_transition_indices[
			workspace->phase_transition_cursors[source]++] = transition_index;
	}
	return 1;
}

static int KernelEndpointBinding(const sg_rune_runtime_snapshot_t *snapshot,
	const sg_rune_capability_kernel_t *kernel, uint32_t *source_phase_out,
	int *cross_cell_out, uint64_t *comparisons)
{
	const sg_rune_model_t *model = snapshot->model;
	uint32_t source_cell;
	uint32_t destination_cell;
	uint32_t destination_phase;

	if (!FindRuntimeCell(model, &kernel->source_cell, &source_cell,
			comparisons) ||
		!FindRuntimeCell(model, &kernel->destination_cell,
			&destination_cell, comparisons) ||
		!FindRuntimePhase(model, &kernel->source_phase, source_phase_out,
			comparisons) ||
		!FindRuntimePhase(model, &kernel->destination_phase,
			&destination_phase, comparisons) ||
		snapshot->phases[*source_phase_out].cell_id != source_cell ||
		snapshot->phases[destination_phase].cell_id != destination_cell)
		return 0;
	*cross_cell_out = source_cell != destination_cell;
	return 1;
}

static int BindPhaseKernels(const sg_rune_runtime_snapshot_t *snapshot,
	sg_localization_workspace_t *workspace, sg_cell_phase_locator_t *locator)
{
	const sg_rune_model_t *model = snapshot->model;
	uint32_t kernel_index;
	uint32_t phase;

	if (!workspace->phase_kernel_offsets ||
		!workspace->phase_kernel_cursors ||
		workspace->phase_kernel_offset_capacity <
			(size_t)model->phase_count + 1U ||
		workspace->phase_kernel_cursor_capacity < model->phase_count ||
		(model->kernel_count != 0U && (!model->kernels ||
		 !workspace->phase_kernel_indices ||
		 workspace->phase_kernel_index_capacity < model->kernel_count)))
		return 0;
	for (phase = 0U; phase <= model->phase_count; phase++)
		workspace->phase_kernel_offsets[phase] = 0U;
	for (kernel_index = 0U; kernel_index < model->kernel_count; kernel_index++)
	{
		uint32_t source_phase;
		int cross_cell;

		locator->prepare_kernel_steps++;
		if (!KernelEndpointBinding(snapshot, &model->kernels[kernel_index],
			&source_phase, &cross_cell,
			&locator->prepare_kernel_lookup_comparisons))
			return 0;
		if (cross_cell)
			workspace->phase_kernel_offsets[source_phase + 1U]++;
	}
	for (phase = 0U; phase < model->phase_count; phase++)
	{
		workspace->phase_kernel_offsets[phase + 1U] +=
			workspace->phase_kernel_offsets[phase];
		workspace->phase_kernel_cursors[phase] =
			workspace->phase_kernel_offsets[phase];
	}
	for (kernel_index = 0U; kernel_index < model->kernel_count; kernel_index++)
	{
		uint32_t source_phase;
		int cross_cell;

		if (!KernelEndpointBinding(snapshot, &model->kernels[kernel_index],
			&source_phase, &cross_cell,
			&locator->prepare_kernel_lookup_comparisons))
			return 0;
		if (!cross_cell)
			continue;
		workspace->phase_kernel_indices[
			workspace->phase_kernel_cursors[source_phase]++] = kernel_index;
	}
	return 1;
}

int SG_CellPhaseLocatorPrepare(
	const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_localization_region_binding_t *bindings,
	size_t binding_count,
	const sg_localization_mover_binding_t *mover_bindings,
	size_t mover_binding_count, sg_localization_workspace_t *workspace,
	sg_cell_phase_locator_t *locator_out,
	sg_localization_status_t *status_out)
{
	if (locator_out)
		memset(locator_out, 0, sizeof(*locator_out));
	SetStatus(status_out, SG_LOCALIZATION_INVALID_ARGUMENT);
	if (!locator_out || !authority || !authority->world ||
		!ConfigurationShapeValid(configuration) ||
		!SemanticsShapeValid(configuration, semantics) ||
		!RuntimeShapeValid(snapshot))
		return 0;
	if (!IdentityEqual(&authority->identity, &configuration->identity) ||
		!IdentityEqual(&authority->identity, &semantics->identity) ||
		!IdentityEqual(&authority->identity, &snapshot->model->identity))
	{
		SetStatus(status_out, SG_LOCALIZATION_IDENTITY_MISMATCH);
		return 0;
	}
	if (!workspace || configuration->portal_count > UINT32_MAX / 2U ||
		configuration->stance_overlap_count > UINT32_MAX / 2U ||
		workspace->cell_region_offset_capacity <
			(size_t)configuration->cell_count + 1U ||
		workspace->region_index_capacity < semantics->region_count ||
		workspace->region_runtime_cell_capacity < semantics->region_count ||
		workspace->region_runtime_region_capacity < semantics->region_count ||
		workspace->cell_portal_offset_capacity <
			(size_t)configuration->cell_count + 1U ||
		workspace->cell_portal_cursor_capacity < configuration->cell_count ||
		workspace->portal_index_capacity <
			(size_t)configuration->portal_count * 2U ||
		workspace->stance_overlap_offset_capacity <
			(size_t)configuration->cell_count + 1U ||
		workspace->stance_overlap_cursor_capacity < configuration->cell_count ||
		workspace->stance_overlap_index_capacity <
			(size_t)configuration->stance_overlap_count * 2U ||
		workspace->phase_transition_offset_capacity <
			(size_t)snapshot->phase_count + 1U ||
		workspace->phase_transition_cursor_capacity < snapshot->phase_count ||
		workspace->phase_transition_index_capacity <
			snapshot->model->phase_transition_count ||
		workspace->phase_kernel_offset_capacity <
			(size_t)snapshot->phase_count + 1U ||
		workspace->phase_kernel_cursor_capacity < snapshot->phase_count ||
		workspace->phase_kernel_index_capacity < snapshot->model->kernel_count)
	{
		SetStatus(status_out, SG_LOCALIZATION_CAPACITY);
		return 0;
	}
	if (!BindRegions(configuration, semantics, snapshot, bindings,
		binding_count, workspace, locator_out) ||
		!BindConfigurationPortals(configuration, workspace, locator_out) ||
		!BindStanceOverlaps(configuration, workspace) ||
		!BindPhaseTransitions(snapshot, workspace, locator_out) ||
		!BindPhaseKernels(snapshot, workspace, locator_out) ||
		!BindMoverAuthority(authority, snapshot->model, mover_bindings,
			mover_binding_count))
	{
		SetStatus(status_out, SG_LOCALIZATION_INVALID_BINDING);
		return 0;
	}
	locator_out->authority = authority;
	locator_out->configuration = configuration;
	locator_out->semantics = semantics;
	locator_out->snapshot = snapshot;
	locator_out->cell_region_offsets = workspace->cell_region_offsets;
	locator_out->region_indices = workspace->region_indices;
	locator_out->region_runtime_cells = workspace->region_runtime_cells;
	locator_out->region_runtime_regions = workspace->region_runtime_regions;
	locator_out->cell_portal_offsets = workspace->cell_portal_offsets;
	locator_out->portal_indices = workspace->portal_indices;
	locator_out->stance_overlap_offsets = workspace->stance_overlap_offsets;
	locator_out->stance_overlap_indices = workspace->stance_overlap_indices;
	locator_out->phase_transition_offsets =
		workspace->phase_transition_offsets;
	locator_out->phase_transition_indices =
		workspace->phase_transition_indices;
	locator_out->phase_kernel_offsets = workspace->phase_kernel_offsets;
	locator_out->phase_kernel_indices = workspace->phase_kernel_indices;
	locator_out->mover_bindings = mover_bindings;
	locator_out->mover_binding_count = mover_binding_count;
	locator_out->rune_identity = snapshot->identity;
	locator_out->topology_revision = snapshot->topology_revision;
	locator_out->configuration_cell_count = configuration->cell_count;
	locator_out->semantic_region_count = semantics->region_count;
	locator_out->runtime_cell_count = snapshot->cell_count;
	locator_out->runtime_phase_count = snapshot->phase_count;
	locator_out->configuration_portal_count = configuration->portal_count;
	locator_out->configuration_stance_overlap_count =
		configuration->stance_overlap_count;
	locator_out->runtime_phase_transition_count =
		snapshot->model->phase_transition_count;
	locator_out->runtime_kernel_count = snapshot->model->kernel_count;
	SetStatus(status_out, SG_LOCALIZATION_OK);
	return 1;
}

static int LocatorCurrent(const sg_cell_phase_locator_t *locator)
{
	return locator && locator->authority && locator->configuration &&
		locator->semantics && locator->snapshot && locator->snapshot->model &&
		locator->cell_region_offsets && locator->region_indices &&
		locator->region_runtime_cells && locator->region_runtime_regions &&
		locator->cell_portal_offsets && locator->phase_transition_offsets &&
		locator->stance_overlap_offsets && locator->phase_kernel_offsets &&
		(locator->configuration_portal_count == 0U ||
			locator->portal_indices) &&
		(locator->configuration_stance_overlap_count == 0U ||
			locator->stance_overlap_indices) &&
		(locator->mover_binding_count == 0U || locator->mover_bindings) &&
		(locator->runtime_phase_transition_count == 0U ||
			locator->phase_transition_indices) &&
		(locator->runtime_kernel_count == 0U ||
			locator->phase_kernel_indices) &&
		locator->rune_identity == locator->snapshot->identity &&
		locator->topology_revision == locator->snapshot->topology_revision &&
		locator->configuration_cell_count ==
			locator->configuration->cell_count &&
		locator->semantic_region_count == locator->semantics->region_count &&
		locator->runtime_cell_count == locator->snapshot->cell_count &&
		locator->runtime_phase_count == locator->snapshot->phase_count &&
		locator->configuration_portal_count ==
			locator->configuration->portal_count &&
		locator->configuration_stance_overlap_count ==
			locator->configuration->stance_overlap_count &&
		locator->runtime_phase_transition_count ==
			locator->snapshot->model->phase_transition_count &&
		locator->runtime_kernel_count ==
			locator->snapshot->model->kernel_count;
}

static int ObservationValid(const sg_cell_phase_locator_t *locator,
	const sg_localization_request_t *request,
	const sg_localization_observation_t *observation,
	const sg_localization_environment_t *environment,
	sg_localization_status_t *status_out)
{
	if (!SubjectValid(&request->expected_subject) ||
		!SubjectValid(&observation->subject) || observation->frame_sequence == 0U ||
		observation->kind < SG_LOCALIZATION_OBSERVATION_PRESENT ||
		observation->kind >= SG_LOCALIZATION_OBSERVATION_KIND_COUNT ||
		observation->stance < SG_RUNE_STANCE_STANDING ||
		observation->stance >= SG_RUNE_STANCE_COUNT ||
		request->reserved != 0U ||
		!ZeroBytes(observation->reserved, sizeof(observation->reserved)) ||
		!ZeroBytes(environment->reserved, sizeof(environment->reserved)))
	{
		SetStatus(status_out, SG_LOCALIZATION_AMBIGUOUS_INPUT);
		return 0;
	}
	if (observation->authenticated != 1U || environment->authenticated != 1U ||
		observation->authenticated_at_ms != observation->observed_at_ms ||
		environment->authenticated_at_ms != environment->sampled_at_ms)
	{
		SetStatus(status_out, SG_LOCALIZATION_UNAUTHENTICATED);
		return 0;
	}
	if (!SubjectEqual(&request->expected_subject, &observation->subject) ||
		observation->rune_identity != locator->rune_identity ||
		observation->topology_revision != locator->topology_revision ||
		environment->rune_identity != locator->rune_identity ||
		environment->topology_revision != locator->topology_revision ||
		environment->frame_sequence != observation->frame_sequence)
	{
		SetStatus(status_out, SG_LOCALIZATION_IDENTITY_MISMATCH);
		return 0;
	}
	if (observation->observed_at_ms > request->now_ms ||
		request->now_ms - observation->observed_at_ms >
			request->max_observation_age_ms ||
		observation->frame_sequence < request->minimum_frame_sequence ||
		environment->sampled_at_ms != observation->observed_at_ms)
	{
		SetStatus(status_out, SG_LOCALIZATION_STALE);
		return 0;
	}
	if (observation->kind != SG_LOCALIZATION_OBSERVATION_TEMPORARILY_ABSENT &&
		observation->kind != SG_LOCALIZATION_OBSERVATION_DEAD &&
		(!Finite3(observation->position) || !Finite3(observation->velocity)))
	{
		SetStatus(status_out, SG_LOCALIZATION_NONFINITE);
		return 0;
	}
	return 1;
}

static int CertificateCell(const sg_configuration_space_t *configuration,
	const float point[3], sg_rune_stance_t stance, uint32_t *cell_out)
{
	uint32_t node_index = configuration->certificate_roots[stance];
	uint32_t visited = 0U;

	while (node_index < configuration->certificate_node_count &&
		visited++ < configuration->certificate_node_count)
	{
		const sg_configuration_certificate_node_t *node =
			&configuration->certificate_nodes[node_index];
		float distance;

		if (node->stance != stance)
			return 0;
		if (node->kind == SG_CONFIGURATION_CERTIFICATE_VALID)
		{
			if (node->cell >= configuration->cell_count)
				return 0;
			*cell_out = node->cell;
			return 1;
		}
		if (node->kind != SG_CONFIGURATION_CERTIFICATE_SPLIT ||
			node->front >= configuration->certificate_node_count ||
			node->back >= configuration->certificate_node_count)
			return 0;
		distance = point[0] * node->plane.normal[0] +
			point[1] * node->plane.normal[1] +
			point[2] * node->plane.normal[2] - node->plane.distance;
		if (!isfinite(distance))
			return 0;
		/* BSP zero belongs to the front child. Expanded-brush zero is solid
		 * under the host's d <= 0 brush test and belongs to the back child. */
		if (node->plane.source_kind == SG_CONFIGURATION_PLANE_EXPANDED_BRUSH)
			node_index = distance > 0.0f ? node->front : node->back;
		else
			node_index = distance < 0.0f ? node->back : node->front;
	}
	return 0;
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
	if (cell->first_face > configuration->face_count ||
		cell->face_count > configuration->face_count - cell->first_face)
		return 0;
	for (local = 0U; local < cell->face_count; local++)
	{
		const sg_configuration_plane_t *plane =
			&configuration->faces[cell->first_face + local].plane;
		float distance = point[0] * plane->normal[0] +
			point[1] * plane->normal[1] + point[2] * plane->normal[2];

		if (distance > plane->distance)
			return 0;
	}
	return 1;
}

static int CellWithinRecoveryDistance(
	const sg_configuration_space_t *configuration, uint32_t cell_index,
	const float point[3], float maximum_distance)
{
	const sg_configuration_cell_t *cell = &configuration->cells[cell_index];
	uint32_t axis;
	uint32_t local;

	for (axis = 0U; axis < 3U; axis++)
		if (cell->bounds.mins.value[axis] - point[axis] > maximum_distance ||
			point[axis] - cell->bounds.maxs.value[axis] > maximum_distance)
			return 0;
	if (cell->first_face > configuration->face_count ||
		cell->face_count > configuration->face_count - cell->first_face)
		return 0;
	for (local = 0U; local < cell->face_count; local++)
	{
		const sg_configuration_plane_t *plane =
			&configuration->faces[cell->first_face + local].plane;
		double normal_squared = (double)plane->normal[0] * plane->normal[0] +
			(double)plane->normal[1] * plane->normal[1] +
			(double)plane->normal[2] * plane->normal[2];
		double excess = (double)point[0] * plane->normal[0] +
			(double)point[1] * plane->normal[1] +
			(double)point[2] * plane->normal[2] - plane->distance;

		if (!(normal_squared > 0.0) || !isfinite(normal_squared) ||
			!isfinite(excess) || (excess > 0.0 && excess * excess >
				(double)maximum_distance * maximum_distance * normal_squared))
			return 0;
	}
	return 1;
}

static int PointInsideRegion(
	const sg_configuration_semantics_t *semantics,
	const sg_configuration_semantic_region_t *region, const float point[3])
{
	uint32_t axis;
	uint32_t local;

	for (axis = 0U; axis < 3U; axis++)
		if (point[axis] < region->bounds.mins.value[axis] ||
			point[axis] > region->bounds.maxs.value[axis])
			return 0;
	for (local = 0U; local < region->face_count; local++)
	{
		const sg_configuration_semantic_face_t *face =
			&semantics->faces[region->first_face + local];
		float distance = point[0] * face->normal[0] +
			point[1] * face->normal[1] + point[2] * face->normal[2];

		if (distance > face->distance)
			return 0;
	}
	return 1;
}

static int RequestRecoveryValid(const sg_localization_request_t *request,
	const sg_localization_observation_t *observation)
{
	if (!request->previous)
		return request->maximum_recovery_distance == 0.0f &&
			request->maximum_temporary_absence_ms == 0U;
	if (observation->kind == SG_LOCALIZATION_OBSERVATION_PRESENT)
		return isfinite(request->maximum_recovery_distance) &&
			request->maximum_recovery_distance >= 0.0f &&
			request->maximum_temporary_absence_ms == 0U;
	if (observation->kind == SG_LOCALIZATION_OBSERVATION_TEMPORARILY_ABSENT)
		return request->maximum_recovery_distance == 0.0f &&
			request->maximum_temporary_absence_ms != 0U;
	return request->maximum_recovery_distance == 0.0f &&
		request->maximum_temporary_absence_ms == 0U;
}

static double Dot3(const float left[3], const float right[3]);
static int MoverValid(const sg_cell_phase_locator_t *locator,
	const sg_localization_mover_t *mover, uint64_t observed_at_ms);

static void LocalizationAngleAxis(const float angles[3], float axis[3][3])
{
	const float radians = 0.01745329251994329577f;
	float sy = sinf(angles[1] * radians);
	float cy = cosf(angles[1] * radians);
	float sp = sinf(angles[0] * radians);
	float cp = cosf(angles[0] * radians);
	float sr = sinf(angles[2] * radians);
	float cr = cosf(angles[2] * radians);

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

static void LocalizationToModelPoint(const float point[3],
	const sg_host_collision_transform_t *transform, float result[3])
{
	float axis[3][3];
	float translated[3];
	uint32_t row;
	uint32_t column;

	for (column = 0U; column < 3U; column++)
		translated[column] = point[column] - transform->origin[column];
	LocalizationAngleAxis(transform->angles, axis);
	for (row = 0U; row < 3U; row++)
		result[row] = (float)Dot3(translated, axis[row]);
}

static int ExactMoverCarry(const sg_cell_phase_locator_t *locator,
	const sg_localization_environment_t *environment,
	const sg_localized_player_state_t *previous,
	const sg_localization_observation_t *observation,
	const sg_host_collision_pose_t *destination_pose)
{
	const sg_localization_mover_t *current = NULL;
	float source_local[3];
	float destination_local[3];
	size_t index;

	if (previous->support != SG_RUNE_SUPPORT_MOVER ||
		!destination_pose->support_is_mover ||
		destination_pose->support.instance_id != previous->support_instance_id ||
		destination_pose->support.model_index != previous->support_model_index)
		return 0;
	for (index = 0U; index < environment->mover_count; index++)
		if (environment->movers[index].instance_id ==
				previous->support_instance_id)
			current = &environment->movers[index];
	if (!current || !MoverValid(locator, current,
			observation->observed_at_ms) ||
		!SG_RuneModelStableIdEqual(&current->mechanism.value,
			&previous->mover.value) ||
		!EntityEqual(&current->entity, &previous->mover_entity))
		return 0;
	LocalizationToModelPoint(previous->field_pose.position,
		&previous->support_transform, source_local);
	LocalizationToModelPoint(observation->position, &current->transform,
		destination_local);
	return memcmp(source_local, destination_local, sizeof(source_local)) == 0;
}

static int ClearRecoveryTransition(const sg_cell_phase_locator_t *locator,
	const sg_localization_environment_t *environment,
	const sg_localized_player_state_t *previous,
	const sg_localization_observation_t *observation)
{
	sg_host_collision_transition_t transition;

	return SG_HostCollisionTransition(locator->authority, environment->scene,
			previous->field_pose.position, observation->position,
			observation->stance, &transition) && transition.clear;
}

static int PointInsideOverlap(const sg_configuration_space_t *configuration,
	const sg_configuration_stance_overlap_t *overlap, const float point[3])
{
	uint32_t axis;
	uint32_t local;

	for (axis = 0U; axis < 3U; axis++)
		if (point[axis] < overlap->bounds.mins.value[axis] ||
			point[axis] > overlap->bounds.maxs.value[axis])
			return 0;
	for (local = 0U; local < overlap->face_count; local++)
	{
		const sg_configuration_plane_t *plane =
			&configuration->faces[overlap->first_face + local].plane;
		double distance = Dot3(point, plane->normal);

		if (distance > plane->distance)
			return 0;
	}
	return 1;
}

static int RepresentedStanceTransition(
	const sg_cell_phase_locator_t *locator,
	const sg_localized_player_state_t *previous, uint32_t destination_cell,
	const sg_localization_observation_t *observation)
{
	const sg_configuration_space_t *configuration = locator->configuration;
	uint32_t first = locator->stance_overlap_offsets[
		previous->configuration_cell];
	uint32_t last = locator->stance_overlap_offsets[
		previous->configuration_cell + 1U];
	uint32_t offset;

	if (previous->stance == observation->stance)
		return previous->configuration_cell == destination_cell;
	if (first > last ||
		last > configuration->stance_overlap_count * 2U)
		return 0;
	for (offset = first; offset < last; offset++)
	{
		uint32_t index = locator->stance_overlap_indices[offset];
		const sg_configuration_stance_overlap_t *overlap;
		int connects;

		if (index >= configuration->stance_overlap_count)
			return 0;
		overlap = &configuration->stance_overlaps[index];
		connects = previous->stance == SG_RUNE_STANCE_STANDING &&
			observation->stance == SG_RUNE_STANCE_CROUCHING &&
			overlap->standing_cell == previous->configuration_cell &&
			overlap->crouching_cell == destination_cell;
		connects |= previous->stance == SG_RUNE_STANCE_CROUCHING &&
			observation->stance == SG_RUNE_STANCE_STANDING &&
			overlap->crouching_cell == previous->configuration_cell &&
			overlap->standing_cell == destination_cell;
		if (connects && PointInsideOverlap(configuration, overlap,
				observation->position))
			return 1;
	}
	return 0;
}

static int RegionWithinRecoveryDistance(
	const sg_configuration_semantics_t *semantics,
	const sg_configuration_semantic_region_t *region, const float point[3],
	float maximum_distance)
{
	uint32_t axis;
	uint32_t local;

	for (axis = 0U; axis < 3U; axis++)
		if (region->bounds.mins.value[axis] - point[axis] > maximum_distance ||
			point[axis] - region->bounds.maxs.value[axis] > maximum_distance)
			return 0;
	for (local = 0U; local < region->face_count; local++)
	{
		const sg_configuration_semantic_face_t *face =
			&semantics->faces[region->first_face + local];
		double normal_squared = (double)face->normal[0] * face->normal[0] +
			(double)face->normal[1] * face->normal[1] +
			(double)face->normal[2] * face->normal[2];
		double excess = (double)point[0] * face->normal[0] +
			(double)point[1] * face->normal[1] +
			(double)point[2] * face->normal[2] - face->distance;

		if (!(normal_squared > 0.0) || !isfinite(normal_squared) ||
			!isfinite(excess) || (excess > 0.0 && excess * excess >
				(double)maximum_distance * maximum_distance * normal_squared))
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

	if (!ConfigurationPortalValid(space, portal))
		return 0;
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

static int RepresentedPortalTransition(const sg_cell_phase_locator_t *locator,
	const sg_localized_player_state_t *previous,
	const sg_localization_observation_t *observation, uint32_t destination_cell,
	uint32_t *candidates_examined, sg_rune_portal_id_t *portal_out)
{
	const sg_configuration_space_t *space = locator->configuration;
	uint32_t first = locator->cell_portal_offsets[
		previous->configuration_cell];
	uint32_t last = locator->cell_portal_offsets[
		previous->configuration_cell + 1U];
	uint32_t offset;

	if (first > last || last > space->portal_count * 2U)
		return 0;
	for (offset = first; offset < last; offset++)
	{
		uint32_t index = locator->portal_indices[offset];
		const sg_configuration_portal_t *portal;

		(*candidates_examined)++;
		if (index >= space->portal_count)
			return 0;
		portal = &space->portals[index];
		int connects = (portal->from_cell == previous->configuration_cell &&
			portal->to_cell == destination_cell) ||
			(portal->to_cell == previous->configuration_cell &&
			portal->from_cell == destination_cell);

		if (connects && portal->stance == observation->stance &&
			PortalContainsCrossing(space, portal,
				previous->field_pose.position, observation->position))
		{
			*portal_out = portal->id;
			return 1;
		}
	}
	return 0;
}

static int RepresentedPhaseTransition(const sg_cell_phase_locator_t *locator,
	const sg_localized_player_state_t *previous, uint32_t destination_phase,
	uint64_t observed_at_ms, uint32_t *candidates_examined)
{
	const sg_rune_model_t *model = locator->snapshot->model;
	uint32_t source_phase = previous->field_pose.phase.phase_id;
	uint32_t first = locator->phase_transition_offsets[source_phase];
	uint32_t last = locator->phase_transition_offsets[source_phase + 1U];
	uint64_t elapsed = observed_at_ms - previous->field_pose.sample_time_ms;
	uint32_t offset;

	if (first > last || last > model->phase_transition_count)
		return 0;
	for (offset = first; offset < last; offset++)
	{
		uint32_t index = locator->phase_transition_indices[offset];
		const sg_rune_phase_transition_t *transition;

		(*candidates_examined)++;
		if (index >= model->phase_transition_count)
			return 0;
		transition = &model->phase_transitions[index];
		if (SG_RuneModelStableIdEqual(&transition->destination_phase.value,
				&model->phases[destination_phase].id.value) &&
			(double)elapsed >= transition->duration_ms.min_value &&
			(double)elapsed <= transition->duration_ms.max_value)
			return 1;
	}
	return 0;
}

static int RepresentedRuntimeCellTransition(
	const sg_cell_phase_locator_t *locator,
	const sg_localized_player_state_t *previous, uint32_t destination_cell,
	uint32_t destination_phase, const sg_rune_portal_id_t *crossed_portal,
	uint32_t *candidates_examined)
{
	const sg_rune_model_t *model = locator->snapshot->model;
	uint32_t source_phase = previous->field_pose.phase.phase_id;
	uint32_t first = locator->phase_kernel_offsets[source_phase];
	uint32_t last = locator->phase_kernel_offsets[source_phase + 1U];
	uint32_t offset;

	if (first > last || last > model->kernel_count)
		return 0;
	for (offset = first; offset < last; offset++)
	{
		uint32_t index = locator->phase_kernel_indices[offset];
		const sg_rune_capability_kernel_t *kernel;

		(*candidates_examined)++;
		if (index >= model->kernel_count)
			return 0;
		kernel = &model->kernels[index];
		if (SG_RuneModelStableIdEqual(&kernel->destination_cell.value,
				&model->cells[destination_cell].id.value) &&
			SG_RuneModelStableIdEqual(&kernel->destination_phase.value,
				&model->phases[destination_phase].id.value) &&
			SG_RuneModelStableIdEqual(&kernel->boundary.value,
				&crossed_portal->value))
			return 1;
	}
	return 0;
}

static sg_rune_medium_t PoseMedium(const sg_host_collision_pose_t *pose)
{
	if (pose->water_level == 0U)
		return SG_RUNE_MEDIUM_DRY;
	if (pose->water_type & SG_HOST_CONTENTS_LAVA)
		return SG_RUNE_MEDIUM_LAVA;
	if (pose->water_type & SG_HOST_CONTENTS_SLIME)
		return SG_RUNE_MEDIUM_SLIME;
	return SG_RUNE_MEDIUM_WATER;
}

static int RegionFactsMatch(
	const sg_configuration_semantic_region_t *region,
	const sg_host_collision_pose_t *pose)
{
	const uint32_t support_flags = SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED |
		SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE;
	uint32_t expected_support;

	if (region->water_level != pose->water_level ||
		(region->water_type & SG_HOST_MASK_WATER) !=
			(pose->water_type & SG_HOST_MASK_WATER) ||
		(region->flags & support_flags) == 0U ||
		(region->flags & support_flags) == support_flags)
		return 0;
	if (pose->support_is_mover || pose->water_level >= 2U)
		return 1;
	expected_support = pose->supported ?
		SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED :
		SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE;
	return (region->flags & support_flags) == expected_support;
}

static int FindSemanticRegion(const sg_cell_phase_locator_t *locator,
	uint32_t cell, const float point[3],
	const sg_host_collision_pose_t *pose, uint32_t *region_out);

static int StoredStateMatchesPhase(const sg_cell_phase_locator_t *locator,
	const sg_localized_player_state_t *state)
{
	const sg_rune_phase_basis_t *phase;
	uint32_t axis;

	if (!SG_DestinationPoseValid(&state->field_pose) ||
		!SG_PhaseCoordinateValid(locator->snapshot, &state->field_pose.phase))
		return 0;
	phase = &locator->snapshot->model->phases[
		state->field_pose.phase.phase_id];
	if (phase->stance != state->stance || phase->motion != state->motion ||
		phase->support != state->support || phase->medium != state->medium ||
		phase->void_relation != state->void_relation ||
		phase->reference_frame != state->reference_frame ||
		!SG_RuneModelStableIdEqual(&phase->mover.value, &state->mover.value) ||
		state->phase_started_at_ms > state->field_pose.sample_time_ms ||
		state->field_pose.sample_time_ms - state->phase_started_at_ms !=
			state->phase_elapsed_ms ||
		(double)state->phase_elapsed_ms < phase->elapsed_ms.min_value ||
		(double)state->phase_elapsed_ms > phase->elapsed_ms.max_value ||
		state->phase_elapsed_ms > phase->time_horizon_ms ||
		state->time_quantum_index !=
			state->phase_elapsed_ms / phase->time_quantum_ms ||
		!Finite3(state->phase_velocity) || !Finite3(state->reference_velocity))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
	{
		const sg_rune_interval_t *interval = axis == 0U ? &phase->velocity.x :
			axis == 1U ? &phase->velocity.y : &phase->velocity.z;

		if (state->phase_velocity[axis] < interval->min_value ||
			state->phase_velocity[axis] > interval->max_value ||
			state->field_pose.velocity[axis] -
				state->reference_velocity[axis] != state->phase_velocity[axis] ||
			(state->reference_frame == SG_RUNE_FRAME_WORLD &&
				state->reference_velocity[axis] != 0.0f))
			return 0;
	}
	return 1;
}

static int StoredStateFactsValid(const sg_cell_phase_locator_t *locator,
	const sg_localized_player_state_t *state)
{
	const sg_configuration_semantic_region_t *region =
		&locator->semantics->regions[state->semantic_region];
	sg_host_collision_pose_t pose;
	sg_rune_medium_t medium;
	int mover_support = state->support == SG_RUNE_SUPPORT_MOVER;

	memset(&pose, 0, sizeof(pose));
	pose.water_level = state->water_level;
	pose.water_type = state->water_type;
	pose.supported = state->support != SG_RUNE_SUPPORT_NONE;
	pose.support_is_mover = mover_support;
	medium = PoseMedium(&pose);
	if (medium != state->medium || !RegionFactsMatch(region, &pose) ||
		(((region->flags &
			SG_CONFIGURATION_SEMANTIC_REGION_VOID_ADJACENT) != 0U) !=
		 (state->void_relation == SG_RUNE_VOID_ADJACENT)))
		return 0;
	if (state->water_level >= 2U)
		return state->motion == SG_RUNE_MOTION_SWIMMING &&
			state->support == SG_RUNE_SUPPORT_NONE &&
			state->reference_frame == SG_RUNE_FRAME_WORLD &&
			state->support_model_index == SG_LOCALIZATION_SUPPORT_MODEL_NONE &&
			state->support_instance_id == 0U &&
			EntityNone(&state->mover_entity) &&
			memcmp(&state->support_transform,
				&(sg_host_collision_transform_t){ { 0 }, { 0 } },
				sizeof(state->support_transform)) == 0;
	if (state->support == SG_RUNE_SUPPORT_NONE)
		return state->motion == SG_RUNE_MOTION_AIRBORNE &&
			state->reference_frame == SG_RUNE_FRAME_WORLD &&
			state->support_model_index == SG_LOCALIZATION_SUPPORT_MODEL_NONE &&
			state->support_instance_id == 0U &&
			EntityNone(&state->mover_entity) &&
			memcmp(&state->support_transform,
				&(sg_host_collision_transform_t){ { 0 }, { 0 } },
				sizeof(state->support_transform)) == 0;
	if (state->motion != SG_RUNE_MOTION_SUPPORTED ||
		state->support_model_index == SG_LOCALIZATION_SUPPORT_MODEL_NONE)
		return 0;
	if (mover_support)
	{
		const sg_localization_mover_binding_t *binding =
			FindMoverBinding(locator, state->support_instance_id);

		return binding && binding->model_index == state->support_model_index &&
			SG_RuneModelStableIdEqual(&binding->mechanism.value,
				&state->mover.value) &&
			EntityEqual(&binding->entity, &state->mover_entity) &&
			Finite3(state->support_transform.origin) &&
			Finite3(state->support_transform.angles) &&
			state->reference_frame == SG_RUNE_FRAME_MOVER_RELATIVE &&
			state->support_instance_id != 0U &&
			SG_RuneModelStableIdValid(&state->mover.value);
	}
	return state->support == SG_RUNE_SUPPORT_SUPPORTED &&
		state->reference_frame == SG_RUNE_FRAME_WORLD &&
		state->support_instance_id == 0U &&
		EntityNone(&state->mover_entity) &&
		memcmp(&state->support_transform,
			&(sg_host_collision_transform_t){ { 0 }, { 0 } },
			sizeof(state->support_transform)) == 0;
}

static sg_localization_status_t PreviousStateStatus(
	const sg_cell_phase_locator_t *locator,
	const sg_localization_observation_t *observation,
	const sg_localized_player_state_t *previous, int path_step)
{
	sg_host_collision_pose_t pose;
	uint32_t authenticated_cell;
	uint32_t authenticated_region;

	if (!previous)
		return SG_LOCALIZATION_RECOVERY_PARAMETER;
	if (!SubjectEqual(&previous->subject, &observation->subject) ||
		previous->rune_identity != locator->rune_identity ||
		previous->topology_revision != locator->topology_revision)
		return SG_LOCALIZATION_IDENTITY_MISMATCH;
	if (previous->frame_sequence == 0U ||
		previous->frame_sequence > observation->frame_sequence ||
		(!path_step &&
		 previous->frame_sequence == observation->frame_sequence) ||
		previous->localized_at_ms < previous->field_pose.sample_time_ms ||
		previous->localized_at_ms > observation->observed_at_ms)
		return SG_LOCALIZATION_STALE;
	if (previous->configuration_cell >= locator->configuration_cell_count ||
		previous->semantic_region >= locator->semantic_region_count ||
		previous->runtime_region >= locator->snapshot->region_count)
		return SG_LOCALIZATION_RECOVERY_REJECTED;
	memset(&pose, 0, sizeof(pose));
	pose.water_level = previous->water_level;
	pose.water_type = previous->water_type;
	pose.supported = previous->support != SG_RUNE_SUPPORT_NONE;
	pose.support_is_mover = previous->support == SG_RUNE_SUPPORT_MOVER;
	if (!CertificateCell(locator->configuration,
			previous->field_pose.position, previous->stance,
			&authenticated_cell) ||
		authenticated_cell != previous->configuration_cell ||
		!FindSemanticRegion(locator, authenticated_cell,
			previous->field_pose.position, &pose, &authenticated_region) ||
		authenticated_region != previous->semantic_region)
		return SG_LOCALIZATION_RECOVERY_REJECTED;
	return previous->stance >= SG_RUNE_STANCE_STANDING &&
		previous->stance < SG_RUNE_STANCE_COUNT &&
		locator->configuration->cells[previous->configuration_cell].stance ==
			previous->stance &&
		ZeroBytes(previous->reserved, sizeof(previous->reserved)) &&
		previous->reserved2 == 0U &&
		previous->recovery >= SG_LOCALIZATION_RECOVERY_NONE &&
		previous->recovery <= SG_LOCALIZATION_RECOVERY_TEMPORARY_ABSENCE &&
		previous->portal_candidates_examined <=
			locator->configuration_portal_count &&
		previous->phase_transition_candidates_examined <=
			locator->runtime_phase_transition_count &&
		previous->kernel_candidates_examined <=
			locator->runtime_kernel_count &&
		locator->semantics->regions[previous->semantic_region].cell ==
			previous->configuration_cell &&
		locator->region_runtime_regions[previous->semantic_region] ==
			previous->runtime_region &&
		previous->field_pose.region_id == previous->runtime_region &&
		locator->region_runtime_cells[previous->semantic_region] ==
			previous->field_pose.phase.cell_id &&
		PointInsideCell(locator->configuration, previous->configuration_cell,
			previous->field_pose.position) &&
		PointInsideRegion(locator->semantics,
			&locator->semantics->regions[previous->semantic_region],
			previous->field_pose.position) &&
		StoredStateMatchesPhase(locator, previous) &&
		StoredStateFactsValid(locator, previous) ? SG_LOCALIZATION_OK :
			SG_LOCALIZATION_RECOVERY_REJECTED;
}

static int FindSemanticRegion(const sg_cell_phase_locator_t *locator,
	uint32_t cell, const float point[3],
	const sg_host_collision_pose_t *pose, uint32_t *region_out)
{
	uint32_t first = locator->cell_region_offsets[cell];
	uint32_t last = locator->cell_region_offsets[cell + 1U];
	uint32_t best = UINT32_MAX;
	uint32_t offset;

	if (first > last || last > locator->semantic_region_count)
		return 0;
	for (offset = first; offset < last; offset++)
	{
		uint32_t index = locator->region_indices[offset];
		const sg_configuration_semantic_region_t *region;

		if (index >= locator->semantic_region_count)
			return 0;
		region = &locator->semantics->regions[index];
		if (RegionFactsMatch(region, pose) &&
			PointInsideRegion(locator->semantics, region, point) &&
			(best == UINT32_MAX || region->id <
				locator->semantics->regions[best].id))
			best = index;
	}
	if (best == UINT32_MAX)
		return 0;
	*region_out = best;
	return 1;
}

static int MoverValid(const sg_cell_phase_locator_t *locator,
	const sg_localization_mover_t *mover, uint64_t observed_at_ms)
{
	const sg_localization_mover_binding_t *binding;

	binding = mover ? FindMoverBinding(locator, mover->instance_id) : NULL;
	return mover && mover->authenticated == 1U &&
		ZeroBytes(mover->reserved, sizeof(mover->reserved)) &&
		mover->reserved2 == 0U && mover->sampled_at_ms == observed_at_ms &&
		mover->instance_id != 0U && mover->model_index != 0U &&
		Finite3(mover->velocity) && Finite3(mover->transform.origin) &&
		Finite3(mover->transform.angles) && binding &&
		binding->model_index == mover->model_index &&
		SG_RuneModelStableIdEqual(&binding->mechanism.value,
			&mover->mechanism.value) &&
		EntityEqual(&binding->entity, &mover->entity);
}

static int TransformEqual(const sg_host_collision_transform_t *left,
	const sg_host_collision_transform_t *right)
{
	return memcmp(left, right, sizeof(*left)) == 0;
}

static const sg_host_collision_instance_t *FindSceneInstance(
	const sg_host_collision_scene_t *scene, uint64_t instance_id)
{
	size_t index;

	if (!scene)
		return NULL;
	for (index = 0U; index < scene->instance_count; index++)
		if (scene->instances[index].instance_id == instance_id)
			return &scene->instances[index];
	return NULL;
}

static int EnvironmentMoversValid(const sg_cell_phase_locator_t *locator,
	const sg_localization_environment_t *environment,
	uint64_t observed_at_ms)
{
	size_t index;
	size_t scene_count = environment->scene ?
		environment->scene->instance_count : 0U;

	if (environment->mover_count != scene_count ||
		(environment->mover_count != 0U && !environment->movers) ||
		(scene_count != 0U && !environment->scene->instances))
		return 0;
	for (index = 0U; index < environment->mover_count; index++)
	{
		const sg_localization_mover_t *mover = &environment->movers[index];
		const sg_host_collision_instance_t *instance;

		if ((index != 0U && environment->movers[index - 1U].instance_id >=
			mover->instance_id) || !MoverValid(locator, mover, observed_at_ms))
			return 0;
		instance = FindSceneInstance(environment->scene, mover->instance_id);
		if (!instance || instance->model_index != mover->model_index ||
			!TransformEqual(&instance->transform, &mover->transform))
			return 0;
	}
	return 1;
}

static int ResolveMover(const sg_localization_environment_t *environment,
	const sg_host_collision_pose_t *pose,
	const sg_localization_mover_t **mover_out,
	sg_localization_status_t *status_out)
{
	size_t index;
	const sg_localization_mover_t *match = NULL;

	*mover_out = NULL;
	if (!pose->support_is_mover)
		return 1;
	if (!environment->movers || environment->mover_count == 0U)
	{
		SetStatus(status_out, SG_LOCALIZATION_MOVER_UNBOUND);
		return 0;
	}
	for (index = 0U; index < environment->mover_count; index++)
	{
		const sg_localization_mover_t *candidate = &environment->movers[index];

		if (candidate->instance_id != pose->support.instance_id ||
			candidate->model_index != pose->support.model_index)
			continue;
		if (match)
		{
			SetStatus(status_out, SG_LOCALIZATION_AMBIGUOUS_INPUT);
			return 0;
		}
		match = candidate;
	}
	if (!match)
	{
		SetStatus(status_out, SG_LOCALIZATION_MOVER_UNBOUND);
		return 0;
	}
	*mover_out = match;
	return 1;
}

static int IntervalContains(const sg_rune_interval_t *interval, double value)
{
	return value >= (double)interval->min_value &&
		value <= (double)interval->max_value;
}

static int PhaseNarrower(const sg_rune_phase_basis_t *candidate,
	const sg_rune_phase_basis_t *best)
{
	float candidate_width;
	float best_width;
	uint32_t axis;
	const sg_rune_interval_t *candidate_axis[3] = {
		&candidate->velocity.x, &candidate->velocity.y, &candidate->velocity.z
	};
	const sg_rune_interval_t *best_axis[3] = {
		&best->velocity.x, &best->velocity.y, &best->velocity.z
	};

	candidate_width = candidate->elapsed_ms.max_value -
		candidate->elapsed_ms.min_value;
	best_width = best->elapsed_ms.max_value - best->elapsed_ms.min_value;
	if (candidate_width != best_width)
		return candidate_width < best_width;
	for (axis = 0U; axis < 3U; axis++)
	{
		candidate_width = candidate_axis[axis]->max_value -
			candidate_axis[axis]->min_value;
		best_width = best_axis[axis]->max_value - best_axis[axis]->min_value;
		if (candidate_width != best_width)
			return candidate_width < best_width;
	}
	return SG_RuneModelOrderKeyCompare(&candidate->order, &best->order) < 0;
}

static int FindPhase(const sg_cell_phase_locator_t *locator,
	uint32_t runtime_cell, sg_rune_stance_t stance,
	sg_rune_motion_t motion, sg_rune_support_t support,
	sg_rune_medium_t medium, sg_rune_void_relation_t void_relation,
	sg_rune_reference_frame_t reference_frame,
	const sg_rune_mechanism_ref_t *mover, const float phase_velocity[3],
	uint64_t elapsed_ms, uint32_t *phase_out)
{
	const sg_rune_model_t *model = locator->snapshot->model;
	const sg_rune_cell_t *cell = &model->cells[runtime_cell];
	uint32_t best = UINT32_MAX;
	uint32_t local;

	if (cell->phases.first > model->phase_count ||
		cell->phases.count > model->phase_count - cell->phases.first)
		return 0;
	for (local = 0U; local < cell->phases.count; local++)
	{
		uint32_t index = cell->phases.first + local;
		const sg_rune_phase_basis_t *phase = &model->phases[index];

		if (phase->stance != stance || phase->motion != motion ||
			phase->support != support || phase->medium != medium ||
			phase->void_relation != void_relation ||
			phase->reference_frame != reference_frame ||
			!SG_RuneModelStableIdEqual(&phase->mover.value, &mover->value) ||
			!IntervalContains(&phase->velocity.x, phase_velocity[0]) ||
			!IntervalContains(&phase->velocity.y, phase_velocity[1]) ||
			!IntervalContains(&phase->velocity.z, phase_velocity[2]) ||
			!IntervalContains(&phase->elapsed_ms, (double)elapsed_ms) ||
			elapsed_ms > phase->time_horizon_ms ||
			locator->snapshot->phases[index].cell_id != runtime_cell)
			continue;
		if (best == UINT32_MAX || PhaseNarrower(phase, &model->phases[best]))
			best = index;
	}
	if (best == UINT32_MAX)
		return 0;
	*phase_out = best;
	return 1;
}

static int LocalizeOne(const sg_cell_phase_locator_t *locator,
	const sg_localization_request_t *request,
	const sg_localization_observation_t *observation,
	const sg_localization_environment_t *environment,
	sg_localized_player_state_t *state_out,
	sg_localization_status_t *status_out, int path_step)
{
	sg_host_collision_pose_t host_pose;
	const sg_localization_mover_t *live_mover;
	sg_rune_mechanism_ref_t mover = SG_RUNE_MECHANISM_REF_NONE;
	sg_rune_portal_id_t crossed_portal = SG_RUNE_PORTAL_REF_NONE;
	sg_rune_motion_t motion;
	sg_rune_support_t support;
	sg_rune_medium_t medium;
	sg_rune_void_relation_t void_relation;
	sg_rune_reference_frame_t reference_frame;
	float phase_velocity[3];
	uint64_t elapsed_ms;
	uint64_t phase_started_at_ms;
	uint32_t configuration_cell;
	uint32_t semantic_region;
	uint32_t runtime_cell;
	uint32_t phase;
	uint32_t axis;
	uint32_t portal_candidates_examined = 0U;
	uint32_t phase_transition_candidates_examined = 0U;
	uint32_t kernel_candidates_examined = 0U;
	sg_localization_recovery_t recovery = SG_LOCALIZATION_RECOVERY_NONE;
	int continuity;
	int exact_configuration;
	sg_localization_status_t previous_status;

	if (state_out)
		memset(state_out, 0, sizeof(*state_out));
	SetStatus(status_out, SG_LOCALIZATION_INVALID_ARGUMENT);
	if (!state_out || !request || !observation || !environment ||
		!LocatorCurrent(locator))
		return 0;
	if (!ObservationValid(locator, request, observation, environment,
			status_out))
		return 0;
	if (!EnvironmentMoversValid(locator, environment,
			observation->observed_at_ms))
	{
		SetStatus(status_out, SG_LOCALIZATION_MOVER_UNBOUND);
		return 0;
	}
	if (!RequestRecoveryValid(request, observation))
	{
		SetStatus(status_out, SG_LOCALIZATION_RECOVERY_PARAMETER);
		return 0;
	}
	if (observation->kind == SG_LOCALIZATION_OBSERVATION_DEAD)
	{
		SetStatus(status_out, SG_LOCALIZATION_RESET_REQUIRED);
		return 0;
	}
	if (observation->kind == SG_LOCALIZATION_OBSERVATION_TEMPORARILY_ABSENT)
	{
		previous_status = PreviousStateStatus(locator, observation,
			request->previous, 0);
		if (previous_status != SG_LOCALIZATION_OK)
		{
			SetStatus(status_out, previous_status);
			return 0;
		}
		if (observation->observed_at_ms <
				request->previous->field_pose.sample_time_ms ||
			observation->observed_at_ms -
				request->previous->field_pose.sample_time_ms >
				request->maximum_temporary_absence_ms)
		{
			SetStatus(status_out, SG_LOCALIZATION_STALE);
			return 0;
		}
		*state_out = *request->previous;
		state_out->frame_sequence = observation->frame_sequence;
		state_out->localized_at_ms = observation->observed_at_ms;
		state_out->recovery = SG_LOCALIZATION_RECOVERY_TEMPORARY_ABSENCE;
		SetStatus(status_out, SG_LOCALIZATION_OK);
		return 1;
	}
	continuity = observation->kind == SG_LOCALIZATION_OBSERVATION_PRESENT &&
		request->previous != NULL;
	previous_status = continuity ? PreviousStateStatus(locator, observation,
		request->previous, path_step) : SG_LOCALIZATION_OK;
	if (previous_status != SG_LOCALIZATION_OK)
	{
		SetStatus(status_out, previous_status);
		return 0;
	}
	if (!SG_HostCollisionClassifyPose(locator->authority, environment->scene,
			observation->position, observation->stance, &host_pose))
	{
		SetStatus(status_out, SG_LOCALIZATION_AMBIGUOUS_INPUT);
		return 0;
	}
	if (!host_pose.valid)
	{
		SetStatus(status_out, SG_LOCALIZATION_SOLID);
		return 0;
	}
	if (continuity &&
		!ClearRecoveryTransition(locator, environment, request->previous,
			observation) &&
		!ExactMoverCarry(locator, environment, request->previous, observation,
			&host_pose))
	{
		SetStatus(status_out, SG_LOCALIZATION_RECOVERY_REJECTED);
		return 0;
	}
	exact_configuration = CertificateCell(locator->configuration,
		observation->position, observation->stance, &configuration_cell) &&
		PointInsideCell(locator->configuration, configuration_cell,
			observation->position);
	if (!exact_configuration)
	{
		if (!continuity ||
			!CellWithinRecoveryDistance(locator->configuration,
				request->previous->configuration_cell, observation->position,
				request->maximum_recovery_distance) ||
			!RegionFactsMatch(&locator->semantics->regions[
				request->previous->semantic_region], &host_pose) ||
			!RegionWithinRecoveryDistance(locator->semantics,
				&locator->semantics->regions[
					request->previous->semantic_region],
				observation->position,
				request->maximum_recovery_distance))
		{
			SetStatus(status_out, continuity ?
				SG_LOCALIZATION_RECOVERY_REJECTED :
				SG_LOCALIZATION_OUTSIDE_CONFIGURATION);
			return 0;
		}
		configuration_cell = request->previous->configuration_cell;
		semantic_region = request->previous->semantic_region;
		recovery = SG_LOCALIZATION_RECOVERY_NUMERIC_DRIFT;
	}
	else
	{
		if (continuity && observation->stance != request->previous->stance &&
			!RepresentedStanceTransition(locator, request->previous,
				configuration_cell, observation))
		{
			SetStatus(status_out, SG_LOCALIZATION_RECOVERY_REJECTED);
			return 0;
		}
		if (continuity && observation->stance == request->previous->stance &&
			configuration_cell != request->previous->configuration_cell &&
			!RepresentedPortalTransition(locator, request->previous,
				observation, configuration_cell,
				&portal_candidates_examined, &crossed_portal))
		{
			SetStatus(status_out, SG_LOCALIZATION_RECOVERY_REJECTED);
			return 0;
		}
		if (!FindSemanticRegion(locator, configuration_cell,
				observation->position, &host_pose, &semantic_region))
		{
			if (!continuity || configuration_cell !=
					request->previous->configuration_cell ||
				!RegionFactsMatch(&locator->semantics->regions[
					request->previous->semantic_region], &host_pose) ||
				!RegionWithinRecoveryDistance(locator->semantics,
					&locator->semantics->regions[
						request->previous->semantic_region],
					observation->position,
					request->maximum_recovery_distance))
			{
				SetStatus(status_out, continuity ?
					SG_LOCALIZATION_RECOVERY_REJECTED :
					SG_LOCALIZATION_NO_SEMANTIC_REGION);
				return 0;
			}
			semantic_region = request->previous->semantic_region;
			recovery = SG_LOCALIZATION_RECOVERY_NUMERIC_DRIFT;
		}
		else if (continuity)
			recovery = SG_LOCALIZATION_RECOVERY_EXACT_CONTINUITY;
	}
	live_mover = NULL;
	if (host_pose.water_level < 2U &&
		!ResolveMover(environment, &host_pose, &live_mover, status_out))
		return 0;
	runtime_cell = locator->region_runtime_cells[semantic_region];
	if (runtime_cell >= locator->runtime_cell_count)
	{
		SetStatus(status_out, SG_LOCALIZATION_INVALID_BINDING);
		return 0;
	}
	medium = PoseMedium(&host_pose);
	if (host_pose.water_level >= 2U)
	{
		motion = SG_RUNE_MOTION_SWIMMING;
		support = SG_RUNE_SUPPORT_NONE;
		reference_frame = SG_RUNE_FRAME_WORLD;
	}
	else if (host_pose.supported)
	{
		motion = SG_RUNE_MOTION_SUPPORTED;
		support = host_pose.support_is_mover ? SG_RUNE_SUPPORT_MOVER :
			SG_RUNE_SUPPORT_SUPPORTED;
		reference_frame = host_pose.support_is_mover ?
			SG_RUNE_FRAME_MOVER_RELATIVE : SG_RUNE_FRAME_WORLD;
	}
	else
	{
		motion = SG_RUNE_MOTION_AIRBORNE;
		support = SG_RUNE_SUPPORT_NONE;
		reference_frame = SG_RUNE_FRAME_WORLD;
	}
	for (axis = 0U; axis < 3U; axis++)
		phase_velocity[axis] = observation->velocity[axis] -
			(live_mover ? live_mover->velocity[axis] : 0.0f);
	if (live_mover)
		mover = live_mover->mechanism;
	void_relation = (locator->semantics->regions[semantic_region].flags &
		SG_CONFIGURATION_SEMANTIC_REGION_VOID_ADJACENT) ?
		SG_RUNE_VOID_ADJACENT : SG_RUNE_VOID_CLEAR;
	if (!continuity)
	{
		phase_started_at_ms = observation->observed_at_ms;
		elapsed_ms = 0U;
		if (!FindPhase(locator, runtime_cell, observation->stance, motion,
			support, medium, void_relation, reference_frame, &mover,
			phase_velocity, elapsed_ms, &phase))
		{
			SetStatus(status_out, SG_LOCALIZATION_NO_PHASE);
			return 0;
		}
	}
	else
	{
		phase_started_at_ms = request->previous->phase_started_at_ms;
		elapsed_ms = observation->observed_at_ms - phase_started_at_ms;
		if (!FindPhase(locator, runtime_cell, observation->stance, motion,
				support, medium, void_relation, reference_frame, &mover,
				phase_velocity, elapsed_ms, &phase) ||
			phase != request->previous->field_pose.phase.phase_id)
		{
			phase_started_at_ms =
				request->previous->field_pose.sample_time_ms;
			elapsed_ms = observation->observed_at_ms - phase_started_at_ms;
			if (!FindPhase(locator, runtime_cell, observation->stance, motion,
					support, medium, void_relation, reference_frame, &mover,
					phase_velocity, elapsed_ms, &phase) ||
				phase == request->previous->field_pose.phase.phase_id)
			{
				SetStatus(status_out, SG_LOCALIZATION_NO_PHASE);
				return 0;
			}
		}
	}
	if (continuity && runtime_cell !=
			request->previous->field_pose.phase.cell_id &&
		!RepresentedRuntimeCellTransition(locator, request->previous,
			runtime_cell, phase, &crossed_portal,
			&kernel_candidates_examined))
	{
		SetStatus(status_out, SG_LOCALIZATION_RECOVERY_REJECTED);
		return 0;
	}
	if (continuity && runtime_cell ==
			request->previous->field_pose.phase.cell_id &&
		phase != request->previous->field_pose.phase.phase_id &&
		!RepresentedPhaseTransition(locator, request->previous, phase,
			observation->observed_at_ms,
			&phase_transition_candidates_examined))
	{
		SetStatus(status_out, SG_LOCALIZATION_RECOVERY_REJECTED);
		return 0;
	}
	if (continuity && phase == request->previous->field_pose.phase.phase_id &&
		elapsed_ms < request->previous->phase_elapsed_ms)
	{
		SetStatus(status_out, SG_LOCALIZATION_RECOVERY_REJECTED);
		return 0;
	}
	if (continuity && support == SG_RUNE_SUPPORT_MOVER &&
		request->previous->support == SG_RUNE_SUPPORT_MOVER &&
		SG_RuneModelStableIdEqual(&mover.value,
			&request->previous->mover.value) &&
		(host_pose.support.instance_id !=
			request->previous->support_instance_id ||
		 host_pose.support.model_index !=
			request->previous->support_model_index))
	{
		SetStatus(status_out, SG_LOCALIZATION_RECOVERY_REJECTED);
		return 0;
	}
	state_out->field_pose.phase = locator->snapshot->phases[phase];
	for (axis = 0U; axis < 3U; axis++)
	{
		state_out->field_pose.position[axis] = observation->position[axis];
		state_out->field_pose.velocity[axis] = observation->velocity[axis];
		state_out->phase_velocity[axis] = phase_velocity[axis];
		state_out->reference_velocity[axis] = live_mover ?
			live_mover->velocity[axis] : 0.0f;
	}
	state_out->field_pose.sample_time_ms = observation->observed_at_ms;
	state_out->field_pose.region_id =
		locator->region_runtime_regions[semantic_region];
	state_out->subject = observation->subject;
	state_out->rune_identity = observation->rune_identity;
	state_out->topology_revision = observation->topology_revision;
	state_out->frame_sequence = observation->frame_sequence;
	state_out->localized_at_ms = observation->observed_at_ms;
	state_out->phase_started_at_ms = phase_started_at_ms;
	state_out->phase_elapsed_ms = elapsed_ms;
	state_out->time_quantum_index = elapsed_ms /
		locator->snapshot->model->phases[phase].time_quantum_ms;
	state_out->configuration_cell = configuration_cell;
	state_out->semantic_region = semantic_region;
	state_out->runtime_region = state_out->field_pose.region_id;
	state_out->stance = observation->stance;
	state_out->motion = motion;
	state_out->support = support;
	state_out->medium = medium;
	state_out->void_relation = void_relation;
	state_out->reference_frame = reference_frame;
	state_out->mover = mover;
	state_out->water_level = host_pose.water_level;
	state_out->water_type = host_pose.water_type;
	state_out->support_model_index = support != SG_RUNE_SUPPORT_NONE ?
		host_pose.support.model_index : SG_LOCALIZATION_SUPPORT_MODEL_NONE;
	state_out->support_instance_id = support != SG_RUNE_SUPPORT_NONE ?
		host_pose.support.instance_id : 0U;
	state_out->mover_entity = live_mover ? live_mover->entity :
		SG_RUNE_ENTITY_REF_NONE;
	if (live_mover)
		state_out->support_transform = live_mover->transform;
	state_out->recovery = recovery;
	state_out->portal_candidates_examined = portal_candidates_examined;
	state_out->phase_transition_candidates_examined =
		phase_transition_candidates_examined;
	state_out->kernel_candidates_examined = kernel_candidates_examined;
	SetStatus(status_out, SG_LOCALIZATION_OK);
	return 1;
}

static int ContinuitySampleValid(const sg_cell_phase_locator_t *locator,
	const sg_localization_observation_t *observation,
	const sg_localization_continuity_sample_t *sample)
{
	return sample && sample->authenticated == 1U &&
		ZeroBytes(sample->reserved, sizeof(sample->reserved)) &&
		sample->stance >= SG_RUNE_STANCE_STANDING &&
		sample->stance < SG_RUNE_STANCE_COUNT &&
		sample->rune_identity == locator->rune_identity &&
		sample->topology_revision == locator->topology_revision &&
		sample->frame_sequence == observation->frame_sequence &&
		sample->authenticated_at_ms == sample->sampled_at_ms &&
		Finite3(sample->position) && Finite3(sample->velocity);
}

static int FinalContinuitySampleEqual(
	const sg_localization_continuity_sample_t *sample,
	const sg_localization_observation_t *observation,
	const sg_localization_environment_t *environment)
{
	return sample->stance == observation->stance &&
		sample->sampled_at_ms == observation->observed_at_ms &&
		memcmp(sample->position, observation->position,
			sizeof(sample->position)) == 0 &&
		memcmp(sample->velocity, observation->velocity,
			sizeof(sample->velocity)) == 0 && sample->scene == environment->scene &&
		sample->movers == environment->movers &&
		sample->mover_count == environment->mover_count;
}

int SG_CellPhaseLocalize(const sg_cell_phase_locator_t *locator,
	const sg_localization_request_t *request,
	const sg_localization_observation_t *observation,
	const sg_localization_environment_t *environment,
	sg_localized_player_state_t *state_out,
	sg_localization_status_t *status_out)
{
	sg_localized_player_state_t first;
	sg_localized_player_state_t second;
	const sg_localized_player_state_t *previous;
	size_t index;

	if (!request || !observation || !environment ||
		observation->kind != SG_LOCALIZATION_OBSERVATION_PRESENT ||
		request->previous == NULL)
		return LocalizeOne(locator, request, observation, environment,
			state_out, status_out, 0);
	if (!state_out || !environment->continuity_samples ||
		environment->continuity_sample_count == 0U)
	{
		if (state_out)
			memset(state_out, 0, sizeof(*state_out));
		SetStatus(status_out, SG_LOCALIZATION_RECOVERY_REJECTED);
		return 0;
	}
	previous = request->previous;
	for (index = 0U; index < environment->continuity_sample_count; index++)
	{
		const sg_localization_continuity_sample_t *sample =
			&environment->continuity_samples[index];
		sg_localization_observation_t step = *observation;
		sg_localization_environment_t step_environment = *environment;
		sg_localization_request_t step_request = *request;
		sg_localized_player_state_t *output = (index & 1U) ? &second : &first;

		if (!ContinuitySampleValid(locator, observation, sample) ||
			sample->sampled_at_ms < previous->field_pose.sample_time_ms ||
			sample->sampled_at_ms > observation->observed_at_ms)
		{
			memset(state_out, 0, sizeof(*state_out));
			SetStatus(status_out, SG_LOCALIZATION_RECOVERY_REJECTED);
			return 0;
		}
		step.stance = sample->stance;
		step.observed_at_ms = sample->sampled_at_ms;
		step.authenticated_at_ms = sample->authenticated_at_ms;
		memcpy(step.position, sample->position, sizeof(step.position));
		memcpy(step.velocity, sample->velocity, sizeof(step.velocity));
		step_environment.sampled_at_ms = sample->sampled_at_ms;
		step_environment.authenticated_at_ms = sample->authenticated_at_ms;
		step_environment.scene = sample->scene;
		step_environment.movers = sample->movers;
		step_environment.mover_count = sample->mover_count;
		step_environment.continuity_samples = NULL;
		step_environment.continuity_sample_count = 0U;
		step_request.previous = previous;
		step_request.now_ms = sample->sampled_at_ms;
		if (!LocalizeOne(locator, &step_request, &step, &step_environment,
				output, status_out, index != 0U))
		{
			memset(state_out, 0, sizeof(*state_out));
			return 0;
		}
		previous = output;
	}
	if (!FinalContinuitySampleEqual(
			&environment->continuity_samples[
				environment->continuity_sample_count - 1U],
			observation, environment))
	{
		memset(state_out, 0, sizeof(*state_out));
		SetStatus(status_out, SG_LOCALIZATION_RECOVERY_REJECTED);
		return 0;
	}
	*state_out = *previous;
	return 1;
}

const char *SG_LocalizationStatusString(sg_localization_status_t status)
{
	switch (status)
	{
	case SG_LOCALIZATION_OK: return "ok";
	case SG_LOCALIZATION_INVALID_ARGUMENT: return "invalid argument";
	case SG_LOCALIZATION_INVALID_BINDING: return "invalid binding";
	case SG_LOCALIZATION_CAPACITY: return "insufficient workspace capacity";
	case SG_LOCALIZATION_UNAUTHENTICATED: return "unauthenticated observation";
	case SG_LOCALIZATION_IDENTITY_MISMATCH: return "identity mismatch";
	case SG_LOCALIZATION_STALE: return "stale observation";
	case SG_LOCALIZATION_NONFINITE: return "nonfinite observation";
	case SG_LOCALIZATION_SOLID: return "solid pose";
	case SG_LOCALIZATION_OUTSIDE_CONFIGURATION:
		return "outside configuration space";
	case SG_LOCALIZATION_NO_SEMANTIC_REGION: return "no semantic region";
	case SG_LOCALIZATION_AMBIGUOUS_INPUT: return "ambiguous input";
	case SG_LOCALIZATION_MOVER_UNBOUND: return "unbound mover";
	case SG_LOCALIZATION_NO_PHASE: return "no matching phase";
	case SG_LOCALIZATION_RECOVERY_PARAMETER:
		return "invalid recovery parameter";
	case SG_LOCALIZATION_RECOVERY_REJECTED: return "recovery rejected";
	case SG_LOCALIZATION_RESET_REQUIRED: return "localization reset required";
	}
	return "unknown localization status";
}
