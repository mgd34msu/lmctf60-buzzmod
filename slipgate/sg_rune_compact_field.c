#include "sg_rune_compact_field.h"
#include "sg_rune_compact_field_plan_private.h"
#include "sg_rune_compact_field_region_hierarchy.h"
#include "sg_rune_compact_mechanisms.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct sg_rune_compact_field_arc_s
{
	uint32_t source;
	uint32_t target;
	uint32_t portal;
	uint32_t capability;
	uint32_t fiber;
	uint32_t hook_target;
	sg_rune_stance_validity_t source_stance;
	sg_rune_stance_validity_t destination_stance;
} sg_rune_compact_field_arc_t;

struct sg_rune_compact_field_s
{
	const sg_rune_compact_model_t *model;
	sg_rune_compact_identity_t expected_identity;
	sg_rune_compact_field_arc_t *arcs;
	uint32_t arc_count;
	uint32_t *outgoing_offsets;
	uint32_t *outgoing_arcs;
	uint32_t *incoming_offsets;
	uint32_t *incoming_arcs;
	uint64_t *arc_costs;
	uint64_t stance_cost;
	sg_rune_compact_field_region_hierarchy_t *regions;
	/* Runtime-only portal-major projection of static mechanism-major BLOCKS
	 * records.  This is the single layout accepted from live snapshots. */
	uint32_t *portal_root_offsets;
	sg_rune_compact_portal_index_t *portal_root_portals;
	sg_rune_compact_mechanism_index_t *portal_root_mechanisms;
	uint32_t portal_root_count;
};

struct sg_rune_compact_destination_plan_s
{
	const sg_rune_compact_field_t *field;
	sg_rune_compact_destination_t destination;
	uint64_t *costs;
};

static void BuildInputs(const sg_rune_compact_field_local_context_t *context,
	float mover_phase,
	sg_rune_compact_eval_input_t inputs[SG_RUNE_ANALYTIC_INPUT_DIMENSION_COUNT]);
static sg_rune_compact_field_status_t EvaluateFiberFunctions(
	const sg_rune_compact_model_t *model,
	sg_rune_analytic_function_span_t functions,
	const sg_rune_compact_eval_input_t *inputs,
	float *cost_out, float *travel_time_out, int *reachable_out);
static sg_rune_compact_field_status_t BuildCanonicalCosts(
	sg_rune_compact_field_t *field);

static void *AllocateArray(uint32_t count, size_t element_size)
{
	if (count == 0U)
		return NULL;
	if ((size_t)count > SIZE_MAX / element_size)
		return NULL;
	return calloc((size_t)count, element_size);
}

static sg_rune_stance_validity_t StanceBit(
	sg_rune_compact_field_stance_t stance)
{
	return stance == SG_RUNE_COMPACT_FIELD_STANDING ?
		SG_RUNE_STANCE_VALID_STANDING : SG_RUNE_STANCE_VALID_CROUCHING;
}

static int ItemKind(sg_rune_compact_landmark_kind_t kind)
{
	return kind == SG_RUNE_COMPACT_LANDMARK_WEAPON ||
		kind == SG_RUNE_COMPACT_LANDMARK_AMMO ||
		kind == SG_RUNE_COMPACT_LANDMARK_ARMOR ||
		kind == SG_RUNE_COMPACT_LANDMARK_HEALTH ||
		kind == SG_RUNE_COMPACT_LANDMARK_POWERUP;
}

static sg_rune_compact_field_stance_t StanceIndex(
	sg_rune_stance_validity_t stance)
{
	return stance == SG_RUNE_STANCE_VALID_STANDING ?
		SG_RUNE_COMPACT_FIELD_STANDING : SG_RUNE_COMPACT_FIELD_CROUCHING;
}

static uint32_t HookTargetCell(const sg_rune_compact_model_t *model,
	const sg_rune_compact_movement_hook_target_t *target, uint32_t member)
{
	uint32_t patch;

	if (target->provenance !=
		SG_RUNE_MOVEMENT_HOOK_TARGET_PROVENANCE_STATIC_RESPONSE)
		return SG_RUNE_COMPACT_INDEX_NONE;
	if (target->response.kind == SG_RUNE_COMPACT_RESPONSE_REF_CERTIFIED_FACT) {
		if (member != 0U)
			return SG_RUNE_COMPACT_INDEX_NONE;
		patch = model->response.facts[target->response.index].target_patch;
	} else {
		const sg_rune_compact_response_candidate_group_t *candidate =
			&model->response.candidate_groups[target->response.index];
		const sg_rune_compact_response_endpoint_group_t *group =
			&model->response.target_endpoint_groups[candidate->target_group];

		if (member >= group->member_count)
			return SG_RUNE_COMPACT_INDEX_NONE;
		patch = model->response.target_endpoint_members[group->first_member + member];
	}
	return model->response.target_patches[patch].target_cell.value;
}

static uint32_t HookTargetCellCount(const sg_rune_compact_model_t *model,
	const sg_rune_compact_movement_hook_target_t *target)
{
	if (target->provenance !=
		SG_RUNE_MOVEMENT_HOOK_TARGET_PROVENANCE_STATIC_RESPONSE)
		return 0U;
	if (target->response.kind == SG_RUNE_COMPACT_RESPONSE_REF_CERTIFIED_FACT)
		return 1U;
	return model->response.target_endpoint_groups[model->response.candidate_groups[
		target->response.index].target_group].member_count;
}

static int ArcCandidate(const sg_rune_compact_model_t *model,
	uint32_t capability_index, uint32_t fiber_index, uint32_t hook_target_index,
	uint32_t target, uint32_t portal, sg_rune_compact_field_arc_t *arc_out)
{
	const sg_rune_movement_capability_t *capability =
		&model->movement_capabilities[capability_index];
	const sg_rune_compact_movement_fiber_t *fiber =
		&model->movement_fibers[fiber_index];
	const sg_rune_compact_movement_state_t *source_state =
		&model->movement_states[fiber->source_state.value];
	const sg_rune_compact_movement_state_t *destination_state =
		&model->movement_states[fiber->destination_state.value];
	sg_rune_stance_validity_t source_stance = (sg_rune_stance_validity_t)(
		capability->source_stances & source_state->stance &
		model->cells[capability->cell.value].valid_stances);
	sg_rune_stance_validity_t destination_stance =
		(sg_rune_stance_validity_t)(capability->destination_stances &
			destination_state->stance & model->cells[target].valid_stances);

	if (portal != SG_RUNE_COMPACT_INDEX_NONE) {
		const sg_rune_compact_portal_t *boundary = &model->portals[portal];

		source_stance = (sg_rune_stance_validity_t)(source_stance &
			boundary->valid_stances);
		destination_stance = (sg_rune_stance_validity_t)(destination_stance &
			boundary->valid_stances);
	}
	if (hook_target_index != SG_RUNE_COMPACT_INDEX_NONE) {
		const sg_rune_compact_movement_hook_target_t *hook_target =
			&model->movement_hook_targets[hook_target_index];

		source_stance = (sg_rune_stance_validity_t)(source_stance &
			hook_target->source_stances);
		destination_stance = (sg_rune_stance_validity_t)(destination_stance &
			hook_target->target_stances);
		if (hook_target->visibility_class !=
				SG_RUNE_MOVEMENT_HOOK_TARGET_VISIBLE ||
			hook_target->provenance !=
				SG_RUNE_MOVEMENT_HOOK_TARGET_PROVENANCE_STATIC_RESPONSE)
			return 0;
	}
	if (source_stance == 0U || destination_stance == 0U ||
		capability->cell.value == target)
		return 0;
	arc_out->source = capability->cell.value;
	arc_out->target = target;
	arc_out->portal = portal;
	arc_out->capability = capability_index;
	arc_out->fiber = fiber_index;
	arc_out->hook_target = hook_target_index;
	arc_out->source_stance = source_stance;
	arc_out->destination_stance = destination_stance;
	return 1;
}

static int PortalDestination(const sg_rune_compact_model_t *model,
	uint32_t portal_index, uint32_t source, uint32_t *target_out)
{
	const sg_rune_compact_portal_t *portal = &model->portals[portal_index];
	const uint32_t negative = model->incidences[
		portal->negative_incidence.value].cell.value;
	const uint32_t positive = model->incidences[
		portal->positive_incidence.value].cell.value;

	if (source == negative &&
		(portal->direction == SG_RUNE_PORTAL_CONTINUITY_BOTH ||
		 portal->direction == SG_RUNE_PORTAL_CONTINUITY_NEGATIVE_TO_POSITIVE)) {
		*target_out = positive;
		return 1;
	}
	if (source == positive &&
		(portal->direction == SG_RUNE_PORTAL_CONTINUITY_BOTH ||
		 portal->direction == SG_RUNE_PORTAL_CONTINUITY_POSITIVE_TO_NEGATIVE)) {
		*target_out = negative;
		return 1;
	}
	return 0;
}

static sg_rune_compact_field_status_t EmitArcs(
	const sg_rune_compact_model_t *model, sg_rune_compact_field_arc_t *arcs,
	uint32_t capacity, uint32_t *count_out)
{
	uint32_t count = 0U;
	uint32_t capability_index;

	for (capability_index = 0U;
		capability_index < model->movement_capability_count; capability_index++) {
		const sg_rune_movement_capability_t *capability =
			&model->movement_capabilities[capability_index];
		uint32_t fiber_index;

		for (fiber_index = capability->fibers.first;
			fiber_index < capability->fibers.first + capability->fibers.count;
			fiber_index++) {
			const sg_rune_compact_movement_fiber_t *fiber =
				&model->movement_fibers[fiber_index];

			if (fiber->kind == SG_RUNE_MOVEMENT_FIBER_HOOK) {
				uint32_t target_index;

				if (capability->boundary_portal.value !=
					SG_RUNE_COMPACT_INDEX_NONE)
					continue;

				for (target_index = fiber->hook_targets.first;
					target_index < fiber->hook_targets.first +
						fiber->hook_targets.count; target_index++) {
					const sg_rune_compact_movement_hook_target_t *hook_target =
						&model->movement_hook_targets[target_index];
					const uint32_t target_count = HookTargetCellCount(model,
						hook_target);
					uint32_t member;

					for (member = 0U; member < target_count; member++) {
						sg_rune_compact_field_arc_t candidate;
						const uint32_t target = HookTargetCell(model, hook_target,
							member);

						if (!ArcCandidate(model, capability_index, fiber_index,
							target_index, target, SG_RUNE_COMPACT_INDEX_NONE,
							&candidate))
							continue;
						if (count == UINT32_MAX)
							return SG_RUNE_COMPACT_FIELD_ALLOCATION_FAILED;
						if (arcs != NULL) {
							if (count >= capacity)
								return SG_RUNE_COMPACT_FIELD_ALLOCATION_FAILED;
							arcs[count] = candidate;
						}
						count++;
					}
				}
				continue;
			}
			/* A controller action changes mechanism authority at the activation
			 * cell.  It is not a traversal to the controlled transition's exit. */
			if (capability->kind ==
				SG_RUNE_MOVEMENT_CAPABILITY_CONTROLLER_ACTION)
				continue;
			if (fiber->kind == SG_RUNE_MOVEMENT_FIBER_MECHANISM_TRANSITION) {
				const sg_rune_compact_mechanism_transition_t *transition =
					&model->mechanism_authority_transitions[
						fiber->mechanism_transition.value];
				sg_rune_compact_field_arc_t candidate;
				uint32_t topology_target;
				uint32_t portal = capability->boundary_portal.value;

				if (transition->entry_cell.value != capability->cell.value)
					continue;
				if (capability->kind == SG_RUNE_MOVEMENT_CAPABILITY_EXTERNAL_FORCE &&
					(portal != SG_RUNE_COMPACT_INDEX_NONE ||
					 transition->kind != SG_RUNE_COMPACT_MECHANISM_TRANSITION_PUSH))
					continue;
				if (capability->kind == SG_RUNE_MOVEMENT_CAPABILITY_MOVER &&
					(portal != SG_RUNE_COMPACT_INDEX_NONE ||
					 transition->kind ==
						SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE ||
					 transition->kind == SG_RUNE_COMPACT_MECHANISM_TRANSITION_PUSH))
					continue;
				if (portal != SG_RUNE_COMPACT_INDEX_NONE &&
					!PortalDestination(model, portal, capability->cell.value,
						&topology_target))
					continue;
				if (portal != SG_RUNE_COMPACT_INDEX_NONE &&
					transition->exit_cell.value != topology_target)
					continue;
				if (!ArcCandidate(model, capability_index, fiber_index,
					SG_RUNE_COMPACT_INDEX_NONE, transition->exit_cell.value,
					portal, &candidate))
					continue;
				if (count == UINT32_MAX)
					return SG_RUNE_COMPACT_FIELD_ALLOCATION_FAILED;
				if (arcs != NULL) {
					if (count >= capacity)
						return SG_RUNE_COMPACT_FIELD_ALLOCATION_FAILED;
					arcs[count] = candidate;
				}
				count++;
				continue;
			}
			if (fiber->kind == SG_RUNE_MOVEMENT_FIBER_PMOVE &&
				capability->boundary_portal.value !=
					SG_RUNE_COMPACT_INDEX_NONE) {
				sg_rune_compact_field_arc_t candidate;
				uint32_t target;

				if (!PortalDestination(model, capability->boundary_portal.value,
					capability->cell.value, &target) ||
					!ArcCandidate(model, capability_index, fiber_index,
						SG_RUNE_COMPACT_INDEX_NONE, target,
						capability->boundary_portal.value, &candidate))
					continue;
				if (count == UINT32_MAX)
					return SG_RUNE_COMPACT_FIELD_ALLOCATION_FAILED;
				if (arcs != NULL) {
					if (count >= capacity)
						return SG_RUNE_COMPACT_FIELD_ALLOCATION_FAILED;
					arcs[count] = candidate;
				}
				count++;
			}
		}
	}
	*count_out = count;
	return SG_RUNE_COMPACT_FIELD_OK;
}

static sg_rune_compact_field_status_t BuildArcs(
	sg_rune_compact_field_t *field)
{
	const sg_rune_compact_model_t *model = field->model;
	uint32_t *outgoing_counts = NULL;
	uint32_t *incoming_counts = NULL;
	uint32_t capacity;
	uint32_t index;
	sg_rune_compact_field_status_t status;

	status = EmitArcs(model, NULL, 0U, &capacity);
	if (status != SG_RUNE_COMPACT_FIELD_OK)
		return status;
	field->arcs = AllocateArray(capacity, sizeof(*field->arcs));
	field->outgoing_offsets = AllocateArray(model->cell_count + 1U,
		sizeof(*field->outgoing_offsets));
	field->incoming_offsets = AllocateArray(model->cell_count + 1U,
		sizeof(*field->incoming_offsets));
	outgoing_counts = AllocateArray(model->cell_count, sizeof(*outgoing_counts));
	incoming_counts = AllocateArray(model->cell_count, sizeof(*incoming_counts));
	if ((capacity != 0U && field->arcs == NULL) ||
		field->outgoing_offsets == NULL || field->incoming_offsets == NULL ||
		outgoing_counts == NULL || incoming_counts == NULL) {
		free(outgoing_counts);
		free(incoming_counts);
		return SG_RUNE_COMPACT_FIELD_ALLOCATION_FAILED;
	}
	status = EmitArcs(model, field->arcs, capacity, &field->arc_count);
	if (status != SG_RUNE_COMPACT_FIELD_OK) {
		free(outgoing_counts);
		free(incoming_counts);
		return status;
	}
	for (index = 0U; index < field->arc_count; index++) {
		outgoing_counts[field->arcs[index].source]++;
		incoming_counts[field->arcs[index].target]++;
	}
	for (index = 0U; index < model->cell_count; index++) {
		field->outgoing_offsets[index + 1U] =
			field->outgoing_offsets[index] + outgoing_counts[index];
		field->incoming_offsets[index + 1U] =
			field->incoming_offsets[index] + incoming_counts[index];
		outgoing_counts[index] = field->outgoing_offsets[index];
		incoming_counts[index] = field->incoming_offsets[index];
	}
	field->outgoing_arcs = AllocateArray(field->arc_count,
		sizeof(*field->outgoing_arcs));
	field->incoming_arcs = AllocateArray(field->arc_count,
		sizeof(*field->incoming_arcs));
	if (field->arc_count != 0U &&
		(field->outgoing_arcs == NULL || field->incoming_arcs == NULL)) {
		free(outgoing_counts);
		free(incoming_counts);
		return SG_RUNE_COMPACT_FIELD_ALLOCATION_FAILED;
	}
	for (index = 0U; index < field->arc_count; index++) {
		field->outgoing_arcs[outgoing_counts[field->arcs[index].source]++] = index;
		field->incoming_arcs[incoming_counts[field->arcs[index].target]++] = index;
	}
	free(outgoing_counts);
	free(incoming_counts);
	return SG_RUNE_COMPACT_FIELD_OK;
}

static sg_rune_compact_field_status_t BuildPortalRoots(
	sg_rune_compact_field_t *field)
{
	const sg_rune_compact_model_t *model = field->model;
	const sg_rune_compact_static_t *static_data = model->static_data;
	uint32_t *cursors = NULL;
	uint32_t binding_index;
	uint32_t portal_index;

	if (model->portal_count == UINT32_MAX)
		return SG_RUNE_COMPACT_FIELD_ALLOCATION_FAILED;
	field->portal_root_offsets = AllocateArray(model->portal_count + 1U,
		sizeof(*field->portal_root_offsets));
	if (field->portal_root_offsets == NULL)
		return SG_RUNE_COMPACT_FIELD_ALLOCATION_FAILED;
	for (binding_index = 0U;
		binding_index < static_data->portal_mechanism_count; binding_index++) {
		const sg_rune_compact_portal_mechanism_t *binding =
			&static_data->portal_mechanisms[binding_index];

		if (binding->kind != SG_RUNE_COMPACT_PORTAL_MECHANISM_BLOCKS)
			continue;
		if (binding->portal.value >= model->portal_count ||
			field->portal_root_offsets[binding->portal.value + 1U] ==
				UINT32_MAX)
			return SG_RUNE_COMPACT_FIELD_ALLOCATION_FAILED;
		field->portal_root_offsets[binding->portal.value + 1U]++;
	}
	for (portal_index = 0U; portal_index < model->portal_count; portal_index++) {
		if (field->portal_root_offsets[portal_index] > UINT32_MAX -
			field->portal_root_offsets[portal_index + 1U])
			return SG_RUNE_COMPACT_FIELD_ALLOCATION_FAILED;
		field->portal_root_offsets[portal_index + 1U] +=
			field->portal_root_offsets[portal_index];
	}
	field->portal_root_count = field->portal_root_offsets[model->portal_count];
	field->portal_root_portals = AllocateArray(field->portal_root_count,
		sizeof(*field->portal_root_portals));
	field->portal_root_mechanisms = AllocateArray(field->portal_root_count,
		sizeof(*field->portal_root_mechanisms));
	cursors = AllocateArray(model->portal_count, sizeof(*cursors));
	if ((field->portal_root_count != 0U &&
		(field->portal_root_portals == NULL ||
		 field->portal_root_mechanisms == NULL)) ||
		(model->portal_count != 0U && cursors == NULL)) {
		free(cursors);
		return SG_RUNE_COMPACT_FIELD_ALLOCATION_FAILED;
	}
	for (portal_index = 0U; portal_index < model->portal_count; portal_index++)
		cursors[portal_index] = field->portal_root_offsets[portal_index];
	for (binding_index = 0U;
		binding_index < static_data->portal_mechanism_count; binding_index++) {
		const sg_rune_compact_portal_mechanism_t *binding =
			&static_data->portal_mechanisms[binding_index];
		const uint32_t portal = binding->portal.value;
		uint32_t root_index;

		if (binding->kind != SG_RUNE_COMPACT_PORTAL_MECHANISM_BLOCKS)
			continue;
		root_index = cursors[portal]++;
		field->portal_root_portals[root_index] = binding->portal;
		field->portal_root_mechanisms[root_index] = binding->mechanism;
	}
	free(cursors);
	return SG_RUNE_COMPACT_FIELD_OK;
}

sg_rune_compact_field_status_t SG_RuneCompactFieldCreate(
	const sg_rune_compact_model_t *model,
	const sg_rune_compact_identity_t *expected_identity,
	sg_rune_compact_field_t **field_out,
	sg_rune_compact_error_t *model_error_out)
{
	sg_rune_compact_field_t *field;
	sg_rune_compact_field_status_t status;
	sg_rune_compact_error_t local_model_error;
	sg_rune_compact_error_t *validation_error = model_error_out != NULL ?
		model_error_out : &local_model_error;

	if (model == NULL || expected_identity == NULL || field_out == NULL)
		return SG_RUNE_COMPACT_FIELD_INVALID_ARGUMENT;
	if (!SG_RuneCompactModelValidateBound(model, expected_identity,
		validation_error))
		return validation_error->code == SG_RUNE_COMPACT_ERROR_LIMIT_EXCEEDED ?
			SG_RUNE_COMPACT_FIELD_ALLOCATION_FAILED :
			SG_RUNE_COMPACT_FIELD_INVALID_MODEL;
	field = calloc(1U, sizeof(*field));
	if (field == NULL)
		return SG_RUNE_COMPACT_FIELD_ALLOCATION_FAILED;
	field->model = model;
	field->expected_identity = *expected_identity;
	status = BuildArcs(field);
	if (status == SG_RUNE_COMPACT_FIELD_OK)
		status = BuildCanonicalCosts(field);
	if (status == SG_RUNE_COMPACT_FIELD_OK &&
		SG_RuneCompactFieldRegionHierarchyCreate(model, &field->regions) !=
			SG_RUNE_COMPACT_FIELD_REGION_OK)
		status = SG_RUNE_COMPACT_FIELD_ALLOCATION_FAILED;
	if (status == SG_RUNE_COMPACT_FIELD_OK)
		status = BuildPortalRoots(field);
	if (status != SG_RUNE_COMPACT_FIELD_OK) {
		SG_RuneCompactFieldDestroy(field);
		return status;
	}
	*field_out = field;
	return SG_RUNE_COMPACT_FIELD_OK;
}

void SG_RuneCompactFieldDestroy(sg_rune_compact_field_t *field)
{
	if (field == NULL)
		return;
	free(field->arcs);
	free(field->outgoing_offsets);
	free(field->outgoing_arcs);
	free(field->incoming_offsets);
	free(field->incoming_arcs);
	free(field->arc_costs);
	SG_RuneCompactFieldRegionHierarchyDestroy(field->regions);
	free(field->portal_root_offsets);
	free(field->portal_root_portals);
	free(field->portal_root_mechanisms);
	free(field);
}

static void MarkDestinationCell(const sg_rune_compact_model_t *model,
	uint8_t *terminals, uint32_t cell)
{
	uint32_t stance_index;

	for (stance_index = 0U;
		stance_index < (uint32_t)SG_RUNE_COMPACT_FIELD_STANCE_COUNT;
		stance_index++)
		if ((model->cells[cell].valid_stances & StanceBit(
			(sg_rune_compact_field_stance_t)stance_index)) != 0U)
			terminals[stance_index * model->cell_count + cell] = 1U;
}

static sg_rune_compact_field_status_t ResolveDestinationStates(
	const sg_rune_compact_field_t *field,
	const sg_rune_compact_destination_t *destination, uint8_t *terminals)
{
	const sg_rune_compact_model_t *model = field->model;
	uint32_t cell;
	uint32_t state_count;

	if (model->cell_count > UINT32_MAX /
		(uint32_t)SG_RUNE_COMPACT_FIELD_STANCE_COUNT)
		return SG_RUNE_COMPACT_FIELD_ALLOCATION_FAILED;
	state_count = model->cell_count *
		(uint32_t)SG_RUNE_COMPACT_FIELD_STANCE_COUNT;
	memset(terminals, 0, (size_t)state_count * sizeof(*terminals));

	switch (destination->kind) {
	case SG_RUNE_COMPACT_DESTINATION_POINT: {
		sg_rune_compact_location_t location;
		const sg_rune_compact_localize_status_t status = SG_RuneCompactLocalize(
			model, &destination->value.point, &location);

		if (status != SG_RUNE_COMPACT_LOCALIZE_OK)
			return SG_RUNE_COMPACT_FIELD_INVALID_DESTINATION;
		MarkDestinationCell(model, terminals, location.cell.value);
		return SG_RUNE_COMPACT_FIELD_OK;
	}
	case SG_RUNE_COMPACT_DESTINATION_CELL:
		cell = destination->value.cell.value;
		if (cell >= model->cell_count)
			return SG_RUNE_COMPACT_FIELD_INVALID_DESTINATION;
		MarkDestinationCell(model, terminals, cell);
		return SG_RUNE_COMPACT_FIELD_OK;
	case SG_RUNE_COMPACT_DESTINATION_SURFACE:
		if (destination->value.surface.value >= model->incidence_count)
			return SG_RUNE_COMPACT_FIELD_INVALID_DESTINATION;
		cell = model->incidences[destination->value.surface.value].cell.value;
		MarkDestinationCell(model, terminals, cell);
		return SG_RUNE_COMPACT_FIELD_OK;
	case SG_RUNE_COMPACT_DESTINATION_ITEM: {
		const sg_rune_compact_static_t *static_data = model->static_data;
		const uint32_t landmark_index = destination->value.item.value;
		const sg_rune_compact_landmark_t *landmark;
		uint32_t offset;

		if (landmark_index >= static_data->landmark_count)
			return SG_RUNE_COMPACT_FIELD_INVALID_DESTINATION;
		landmark = &static_data->landmarks[landmark_index];
		if (!ItemKind(landmark->kind))
			return SG_RUNE_COMPACT_FIELD_INVALID_DESTINATION;
		for (offset = 0U; offset < landmark->cells.count; offset++) {
			cell = static_data->landmark_cells[
				landmark->cells.first + offset].value;
			MarkDestinationCell(model, terminals, cell);
		}
		return SG_RUNE_COMPACT_FIELD_OK;
	}
	case SG_RUNE_COMPACT_DESTINATION_KIND_COUNT:
	default:
		return SG_RUNE_COMPACT_FIELD_INVALID_DESTINATION;
	}
}

static sg_rune_compact_field_status_t QuantizeCost(float value,
	uint64_t *cost_out)
{
	const double scaled = (double)value *
		(double)SG_RUNE_COMPACT_FIELD_COST_SCALE;
	uint64_t cost;

	if (!isfinite(value) || value < 0.0f ||
		scaled >= (double)UINT64_MAX)
		return SG_RUNE_COMPACT_FIELD_INVALID_TRANSITION_VALUE;
	cost = (uint64_t)floor(scaled + 0.5);
	/* A zero-cost transition still has a strictly descending stored value.
	 * The public local cost remains the exact analytic zero. */
	*cost_out = cost == 0U ? UINT64_C(1) : cost;
	return SG_RUNE_COMPACT_FIELD_OK;
}

static sg_rune_compact_field_status_t AddCosts(uint64_t left, uint64_t right,
	uint64_t *sum_out)
{
	if (left == UINT64_MAX || right == UINT64_MAX) {
		*sum_out = UINT64_MAX;
		return SG_RUNE_COMPACT_FIELD_OK;
	}
	if (right > UINT64_MAX - UINT64_C(1) - left)
		return SG_RUNE_COMPACT_FIELD_COST_OVERFLOW;
	*sum_out = left + right;
	return SG_RUNE_COMPACT_FIELD_OK;
}

static void CellCenter(const sg_rune_compact_cell_t *cell,
	sg_rune_q8_vec3_t *center_out)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++) {
		const int64_t total = (int64_t)cell->bounds.mins.value[axis] +
			(int64_t)cell->bounds.maxs.value[axis];

		center_out->value[axis] = (int32_t)(total / 2);
	}
}

static void CanonicalArcContext(const sg_rune_compact_model_t *model,
	const sg_rune_compact_field_arc_t *arc,
	sg_rune_compact_field_stance_t stance,
	sg_rune_compact_field_local_context_t *context_out)
{
	sg_rune_q8_vec3_t target;
	float length_squared = 0.0f;
	uint32_t axis;

	memset(context_out, 0, sizeof(*context_out));
	CellCenter(&model->cells[arc->source], &context_out->origin);
	CellCenter(&model->cells[arc->target], &target);
	context_out->stance = stance;
	{
		const sg_rune_compact_movement_state_t *state =
			&model->movement_states[model->movement_fibers[
				arc->fiber].source_state.value];

		context_out->support = state->support;
		context_out->water = state->water;
		context_out->hook_phase = state->hook_phase;
		context_out->state_flags = state->flags;
		context_out->mover_mechanism = state->mover_mechanism;
	}
	for (axis = 0U; axis < 3U; axis++) {
		const float delta = (float)((int64_t)target.value[axis] -
			(int64_t)context_out->origin.value[axis]) / 8.0f;

		context_out->direction[axis] = delta;
		length_squared += delta * delta;
	}
	context_out->distance = sqrtf(length_squared);
	if (context_out->distance > 0.0f)
		for (axis = 0U; axis < 3U; axis++)
			context_out->direction[axis] /= context_out->distance;
	context_out->frame_sequence = UINT64_C(1);
}

static sg_rune_analytic_function_span_t ArcFunctions(
	const sg_rune_compact_model_t *model,
	const sg_rune_compact_field_arc_t *arc)
{
	const sg_rune_compact_movement_fiber_t *fiber =
		&model->movement_fibers[arc->fiber];

	if (arc->hook_target != SG_RUNE_COMPACT_INDEX_NONE) {
		const sg_rune_movement_capability_kind_t kind =
			model->movement_capabilities[arc->capability].kind;
		const sg_rune_compact_movement_hook_functions_t *functions =
			&model->movement_hook_targets[arc->hook_target].functions;

		switch (kind) {
		case SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BOLT:
			return functions->bolt;
		case SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BODY:
			return functions->body;
		case SG_RUNE_MOVEMENT_CAPABILITY_HOOK_PULL:
			return functions->pull;
		case SG_RUNE_MOVEMENT_CAPABILITY_HOOK_RELEASE:
			return functions->release;
		case SG_RUNE_MOVEMENT_CAPABILITY_HOOK_COAST:
			return functions->coast;
		case SG_RUNE_MOVEMENT_CAPABILITY_HOOK_RELAUNCH:
			return functions->relaunch;
		default:
			break;
		}
	}
	return fiber->functions;
}

static sg_rune_compact_field_status_t CanonicalArcCost(
	const sg_rune_compact_field_t *field,
	const sg_rune_compact_field_arc_t *arc,
	sg_rune_compact_field_stance_t stance, uint64_t *cost_out)
{
	const sg_rune_compact_model_t *model = field->model;
	sg_rune_compact_field_local_context_t context;
	sg_rune_compact_eval_input_t
		inputs[SG_RUNE_ANALYTIC_INPUT_DIMENSION_COUNT];
	float cost;
	float travel_time;
	uint64_t quantized;
	int reachable;
	sg_rune_compact_field_status_t status;

	CanonicalArcContext(model, arc, stance, &context);
	BuildInputs(&context, 0.0f, inputs);
	status = EvaluateFiberFunctions(model, ArcFunctions(model, arc), inputs, &cost,
		&travel_time, &reachable);
	if (status != SG_RUNE_COMPACT_FIELD_OK)
		return status;
	/* Canonical zero-state reachability cannot remove structural topology.
	 * Query evaluates this margin again from the authenticated live state. */
	status = QuantizeCost(cost, &quantized);
	if (status != SG_RUNE_COMPACT_FIELD_OK)
		return status;
	*cost_out = quantized;
	return SG_RUNE_COMPACT_FIELD_OK;
}

static sg_rune_compact_field_status_t BuildCanonicalCosts(
	sg_rune_compact_field_t *field)
{
	const sg_rune_compact_model_t *model = field->model;
	uint32_t stance_index;
	uint32_t arc_index;
	sg_rune_compact_field_status_t status;

	status = QuantizeCost((float)model->identity.physics.frame_ms / 1000.0f,
		&field->stance_cost);
	if (status != SG_RUNE_COMPACT_FIELD_OK)
		return status;
	if (field->arc_count > UINT32_MAX /
		(uint32_t)SG_RUNE_COMPACT_FIELD_STANCE_COUNT)
		return SG_RUNE_COMPACT_FIELD_ALLOCATION_FAILED;
	field->arc_costs = AllocateArray(field->arc_count *
		(uint32_t)SG_RUNE_COMPACT_FIELD_STANCE_COUNT,
		sizeof(*field->arc_costs));
	if (field->arc_count != 0U && field->arc_costs == NULL)
		return SG_RUNE_COMPACT_FIELD_ALLOCATION_FAILED;
	for (stance_index = 0U;
		stance_index < (uint32_t)SG_RUNE_COMPACT_FIELD_STANCE_COUNT;
		stance_index++)
		for (arc_index = 0U; arc_index < field->arc_count; arc_index++) {
			const sg_rune_compact_field_arc_t *arc = &field->arcs[arc_index];
			uint64_t *arc_cost = &field->arc_costs[
				stance_index * field->arc_count + arc_index];

			if ((arc->source_stance & StanceBit(
				(sg_rune_compact_field_stance_t)stance_index)) == 0U) {
				*arc_cost = UINT64_MAX;
				continue;
			}
			status = CanonicalArcCost(field, arc,
				(sg_rune_compact_field_stance_t)stance_index, arc_cost);
			if (status != SG_RUNE_COMPACT_FIELD_OK)
				return status;
		}
	return SG_RUNE_COMPACT_FIELD_OK;
}

static sg_rune_compact_field_status_t BuildCosts(
	const sg_rune_compact_field_t *field, uint64_t *costs)
{
	const sg_rune_compact_model_t *model = field->model;
	uint32_t stance_index;
	uint32_t arc_index;
	int changed;
	sg_rune_compact_field_status_t status;

	do {
		changed = 0;
		for (stance_index = 0U;
			stance_index < (uint32_t)SG_RUNE_COMPACT_FIELD_STANCE_COUNT;
			stance_index++) {
			const uint32_t other_stance = stance_index == 0U ? 1U : 0U;
			uint32_t cell;

			for (arc_index = 0U; arc_index < field->arc_count; arc_index++) {
				const sg_rune_compact_field_arc_t *arc = &field->arcs[arc_index];
				const uint32_t destination_stance = (uint32_t)StanceIndex(
					arc->destination_stance);
				const uint64_t target = costs[
					destination_stance * model->cell_count + arc->target];
				uint64_t candidate;
				uint64_t *source = &costs[
					stance_index * model->cell_count + arc->source];

				status = AddCosts(target, field->arc_costs[
					stance_index * field->arc_count + arc_index], &candidate);
				if (status != SG_RUNE_COMPACT_FIELD_OK)
					return status;
				if (candidate < *source) {
					*source = candidate;
					changed = 1;
				}
			}
			for (cell = 0U; cell < model->cell_count; cell++) {
				const sg_rune_stance_validity_t both =
					SG_RUNE_STANCE_VALID_ALL;
				const uint64_t target = costs[
					other_stance * model->cell_count + cell];
				uint64_t candidate;
				uint64_t *source = &costs[
					stance_index * model->cell_count + cell];

				status = AddCosts(target, field->stance_cost, &candidate);
				if (status != SG_RUNE_COMPACT_FIELD_OK)
					return status;
				if ((model->cells[cell].valid_stances & both) == both &&
					candidate < *source) {
					*source = candidate;
					changed = 1;
				}
			}
		}
	} while (changed);
	return SG_RUNE_COMPACT_FIELD_OK;
}

typedef struct field_state_queue_s
{
	const sg_rune_compact_field_t *field;
	uint32_t *state_next;
	uint8_t *queued;
	uint32_t *leaf_state_heads;
	uint32_t *leaf_state_tails;
	uint32_t *leaf_next;
	uint32_t *coarse_leaf_heads;
	uint32_t *coarse_leaf_tails;
	uint32_t *coarse_next;
	uint32_t capacity;
	uint32_t leaf_count;
	uint32_t coarse_count;
	uint32_t coarse_head;
	uint32_t coarse_tail;
	uint32_t count;
} field_state_queue_t;

static void ReportExamined(sg_rune_compact_field_refresh_report_t *report)
{
	if (report != NULL && report->examined_transition_count != UINT64_MAX)
		report->examined_transition_count++;
}

static int QueueState(field_state_queue_t *queue, uint32_t state)
{
	uint32_t cell;
	uint32_t leaf;
	uint32_t coarse;

	if (queue == NULL || state >= queue->capacity || queue->queued[state] != 0U)
		return 1;
	if (queue->count >= queue->capacity)
		return 0;
	cell = state % queue->field->model->cell_count;
	leaf = SG_RuneCompactFieldRegionCellLeaf(queue->field->regions, cell);
	if (leaf >= queue->leaf_count)
		return 0;
	coarse = SG_RuneCompactFieldRegionLeafCoarse(queue->field->regions, leaf);
	if (coarse >= queue->coarse_count)
		return 0;
	queue->state_next[state] = SG_RUNE_COMPACT_INDEX_NONE;
	if (queue->leaf_state_heads[leaf] == SG_RUNE_COMPACT_INDEX_NONE)
	{
		queue->leaf_state_heads[leaf] = state;
		queue->leaf_state_tails[leaf] = state;
		queue->leaf_next[leaf] = SG_RUNE_COMPACT_INDEX_NONE;
		if (queue->coarse_leaf_heads[coarse] == SG_RUNE_COMPACT_INDEX_NONE)
		{
			queue->coarse_leaf_heads[coarse] = leaf;
			queue->coarse_leaf_tails[coarse] = leaf;
			queue->coarse_next[coarse] = SG_RUNE_COMPACT_INDEX_NONE;
			if (queue->coarse_head == SG_RUNE_COMPACT_INDEX_NONE)
				queue->coarse_head = coarse;
			else
				queue->coarse_next[queue->coarse_tail] = coarse;
			queue->coarse_tail = coarse;
		}
		else
		{
			queue->leaf_next[queue->coarse_leaf_tails[coarse]] = leaf;
			queue->coarse_leaf_tails[coarse] = leaf;
		}
	}
	else
	{
		queue->state_next[queue->leaf_state_tails[leaf]] = state;
		queue->leaf_state_tails[leaf] = state;
	}
	queue->queued[state] = 1U;
	queue->count++;
	return 1;
}

static int PopState(field_state_queue_t *queue, uint32_t *state_out)
{
	uint32_t coarse;
	uint32_t leaf;
	uint32_t state;

	if (queue == NULL || state_out == NULL || queue->count == 0U)
		return 0;
	coarse = queue->coarse_head;
	leaf = queue->coarse_leaf_heads[coarse];
	state = queue->leaf_state_heads[leaf];
	queue->leaf_state_heads[leaf] = queue->state_next[state];
	if (queue->leaf_state_heads[leaf] == SG_RUNE_COMPACT_INDEX_NONE)
	{
		queue->leaf_state_tails[leaf] = SG_RUNE_COMPACT_INDEX_NONE;
		queue->coarse_leaf_heads[coarse] = queue->leaf_next[leaf];
		queue->leaf_next[leaf] = SG_RUNE_COMPACT_INDEX_NONE;
		if (queue->coarse_leaf_heads[coarse] == SG_RUNE_COMPACT_INDEX_NONE)
		{
			queue->coarse_leaf_tails[coarse] = SG_RUNE_COMPACT_INDEX_NONE;
			queue->coarse_head = queue->coarse_next[coarse];
			queue->coarse_next[coarse] = SG_RUNE_COMPACT_INDEX_NONE;
			if (queue->coarse_head == SG_RUNE_COMPACT_INDEX_NONE)
				queue->coarse_tail = SG_RUNE_COMPACT_INDEX_NONE;
		}
	}
	queue->state_next[state] = SG_RUNE_COMPACT_INDEX_NONE;
	queue->count--;
	queue->queued[state] = 0U;
	*state_out = state;
	return 1;
}

static void FillNone(uint32_t *values, uint32_t count)
{
	uint32_t index;

	for (index = 0U; index < count; index++)
		values[index] = SG_RUNE_COMPACT_INDEX_NONE;
}

static int InitializeQueue(const sg_rune_compact_field_t *field,
	uint32_t state_count, field_state_queue_t *queue)
{
	memset(queue, 0, sizeof(*queue));
	queue->field = field;
	queue->capacity = state_count;
	queue->leaf_count = SG_RuneCompactFieldRegionLeafCount(field->regions);
	queue->coarse_count = SG_RuneCompactFieldRegionCoarseCount(field->regions);
	queue->coarse_head = SG_RUNE_COMPACT_INDEX_NONE;
	queue->coarse_tail = SG_RUNE_COMPACT_INDEX_NONE;
	queue->state_next = AllocateArray(state_count, sizeof(*queue->state_next));
	queue->queued = AllocateArray(state_count, sizeof(*queue->queued));
	queue->leaf_state_heads = AllocateArray(queue->leaf_count,
		sizeof(*queue->leaf_state_heads));
	queue->leaf_state_tails = AllocateArray(queue->leaf_count,
		sizeof(*queue->leaf_state_tails));
	queue->leaf_next = AllocateArray(queue->leaf_count,
		sizeof(*queue->leaf_next));
	queue->coarse_leaf_heads = AllocateArray(queue->coarse_count,
		sizeof(*queue->coarse_leaf_heads));
	queue->coarse_leaf_tails = AllocateArray(queue->coarse_count,
		sizeof(*queue->coarse_leaf_tails));
	queue->coarse_next = AllocateArray(queue->coarse_count,
		sizeof(*queue->coarse_next));
	if (queue->state_next == NULL || queue->queued == NULL ||
		queue->leaf_state_heads == NULL || queue->leaf_state_tails == NULL ||
		queue->leaf_next == NULL || queue->coarse_leaf_heads == NULL ||
		queue->coarse_leaf_tails == NULL || queue->coarse_next == NULL)
		return 0;
	FillNone(queue->state_next, state_count);
	FillNone(queue->leaf_state_heads, queue->leaf_count);
	FillNone(queue->leaf_state_tails, queue->leaf_count);
	FillNone(queue->leaf_next, queue->leaf_count);
	FillNone(queue->coarse_leaf_heads, queue->coarse_count);
	FillNone(queue->coarse_leaf_tails, queue->coarse_count);
	FillNone(queue->coarse_next, queue->coarse_count);
	return 1;
}

static void DestroyQueue(field_state_queue_t *queue)
{
	if (queue == NULL)
		return;
	free(queue->state_next);
	free(queue->queued);
	free(queue->leaf_state_heads);
	free(queue->leaf_state_tails);
	free(queue->leaf_next);
	free(queue->coarse_leaf_heads);
	free(queue->coarse_leaf_tails);
	free(queue->coarse_next);
	memset(queue, 0, sizeof(*queue));
}

static uint32_t StateCell(const sg_rune_compact_field_t *field,
	uint32_t state)
{
	return state % field->model->cell_count;
}

static uint32_t StateStance(const sg_rune_compact_field_t *field,
	uint32_t state)
{
	return state / field->model->cell_count;
}

static uint32_t ArcTargetState(const sg_rune_compact_field_t *field,
	const sg_rune_compact_field_arc_t *arc)
{
	return (uint32_t)StanceIndex(arc->destination_stance) *
		field->model->cell_count + arc->target;
}

static int StateStanceChangeValid(const sg_rune_compact_field_t *field,
	uint32_t state)
{
	const uint32_t cell = StateCell(field, state);

	return (field->model->cells[cell].valid_stances &
		SG_RUNE_STANCE_VALID_ALL) == SG_RUNE_STANCE_VALID_ALL;
}

static sg_rune_compact_field_status_t StateHasRetainedSupport(
	const sg_rune_compact_destination_plan_t *previous,
	uint32_t state, const uint8_t *old_terminals,
	const uint8_t *new_terminals, const uint8_t *invalidated,
	int *supported_out, sg_rune_compact_field_refresh_report_t *report)
{
	const sg_rune_compact_field_t *field = previous->field;
	const uint32_t cell = StateCell(field, state);
	const uint32_t stance = StateStance(field, state);
	uint32_t offset;

	*supported_out = 0;
	if (old_terminals[state] != 0U && new_terminals[state] != 0U)
	{
		*supported_out = 1;
		return SG_RUNE_COMPACT_FIELD_OK;
	}
	if (previous->costs[state] == UINT64_MAX)
		return SG_RUNE_COMPACT_FIELD_OK;
	for (offset = field->outgoing_offsets[cell];
		offset < field->outgoing_offsets[cell + 1U]; offset++)
	{
		const uint32_t arc_index = field->outgoing_arcs[offset];
		const sg_rune_compact_field_arc_t *arc = &field->arcs[arc_index];
		const uint32_t target = ArcTargetState(field, arc);
		uint64_t candidate;
		sg_rune_compact_field_status_t status;

		if ((arc->source_stance & StanceBit(
			(sg_rune_compact_field_stance_t)stance)) == 0U ||
			invalidated[target] != 0U)
			continue;
		ReportExamined(report);
		status = AddCosts(previous->costs[target], field->arc_costs[
			stance * field->arc_count + arc_index], &candidate);
		if (status != SG_RUNE_COMPACT_FIELD_OK)
			return status;
		if (candidate != UINT64_MAX && candidate == previous->costs[state])
		{
			*supported_out = 1;
			return SG_RUNE_COMPACT_FIELD_OK;
		}
	}
	if (StateStanceChangeValid(field, state))
	{
		const uint32_t other = (stance == 0U ? 1U : 0U) *
			field->model->cell_count + cell;
		uint64_t candidate;
		sg_rune_compact_field_status_t status;

		if (invalidated[other] == 0U)
		{
			ReportExamined(report);
			status = AddCosts(previous->costs[other], field->stance_cost,
				&candidate);
			if (status != SG_RUNE_COMPACT_FIELD_OK)
				return status;
			if (candidate != UINT64_MAX &&
				candidate == previous->costs[state])
				*supported_out = 1;
		}
	}
	return SG_RUNE_COMPACT_FIELD_OK;
}

static sg_rune_compact_field_status_t InvalidatePredecessor(
	const sg_rune_compact_destination_plan_t *previous,
	uint32_t source, uint32_t target, uint64_t edge_cost,
	const uint8_t *old_terminals, const uint8_t *new_terminals,
	uint8_t *invalidated, field_state_queue_t *queue,
	sg_rune_compact_field_refresh_report_t *report)
{
	uint64_t candidate;
	int supported;
	sg_rune_compact_field_status_t status;

	if (invalidated[source] != 0U || previous->costs[source] == UINT64_MAX)
		return SG_RUNE_COMPACT_FIELD_OK;
	ReportExamined(report);
	status = AddCosts(previous->costs[target], edge_cost, &candidate);
	if (status != SG_RUNE_COMPACT_FIELD_OK)
		return status;
	if (candidate == UINT64_MAX || candidate != previous->costs[source])
		return SG_RUNE_COMPACT_FIELD_OK;
	status = StateHasRetainedSupport(previous, source, old_terminals,
		new_terminals, invalidated, &supported, report);
	if (status != SG_RUNE_COMPACT_FIELD_OK || supported)
		return status;
	invalidated[source] = 1U;
	if (!QueueState(queue, source))
		return SG_RUNE_COMPACT_FIELD_ALLOCATION_FAILED;
	return SG_RUNE_COMPACT_FIELD_OK;
}

static sg_rune_compact_field_status_t InvalidateRemovedTerminals(
	const sg_rune_compact_destination_plan_t *previous,
	const uint8_t *old_terminals, const uint8_t *new_terminals,
	uint8_t *invalidated, field_state_queue_t *queue,
	sg_rune_compact_field_refresh_report_t *report)
{
	const sg_rune_compact_field_t *field = previous->field;
	const uint32_t state_count = field->model->cell_count *
		(uint32_t)SG_RUNE_COMPACT_FIELD_STANCE_COUNT;
	uint32_t state;

	for (state = 0U; state < state_count; state++)
		if (old_terminals[state] != 0U && new_terminals[state] == 0U)
		{
			invalidated[state] = 1U;
			if (!QueueState(queue, state))
				return SG_RUNE_COMPACT_FIELD_ALLOCATION_FAILED;
		}
	while (PopState(queue, &state))
	{
		const uint32_t cell = StateCell(field, state);
		const uint32_t stance = StateStance(field, state);
		uint32_t offset;

		for (offset = field->incoming_offsets[cell];
			offset < field->incoming_offsets[cell + 1U]; offset++)
		{
			const uint32_t arc_index = field->incoming_arcs[offset];
			const sg_rune_compact_field_arc_t *arc = &field->arcs[arc_index];
			uint32_t source_stance;

			if ((uint32_t)StanceIndex(arc->destination_stance) != stance)
				continue;
			for (source_stance = 0U; source_stance <
				(uint32_t)SG_RUNE_COMPACT_FIELD_STANCE_COUNT; source_stance++)
			{
				const uint32_t source = source_stance *
					field->model->cell_count + arc->source;
				sg_rune_compact_field_status_t status;

				if ((arc->source_stance & StanceBit(
					(sg_rune_compact_field_stance_t)source_stance)) == 0U)
					continue;
				status = InvalidatePredecessor(previous, source, state,
					field->arc_costs[source_stance * field->arc_count +
						arc_index], old_terminals, new_terminals,
					invalidated, queue, report);
				if (status != SG_RUNE_COMPACT_FIELD_OK)
					return status;
			}
		}
		if (StateStanceChangeValid(field, state))
		{
			const uint32_t source = (stance == 0U ? 1U : 0U) *
				field->model->cell_count + cell;
			const sg_rune_compact_field_status_t status =
				InvalidatePredecessor(previous, source, state,
					field->stance_cost, old_terminals, new_terminals,
					invalidated, queue, report);

			if (status != SG_RUNE_COMPACT_FIELD_OK)
				return status;
		}
	}
	return SG_RUNE_COMPACT_FIELD_OK;
}

static int EnqueueIncomingStates(const sg_rune_compact_field_t *field,
	uint32_t target, field_state_queue_t *queue,
	sg_rune_compact_field_refresh_report_t *report)
{
	const uint32_t cell = StateCell(field, target);
	const uint32_t stance = StateStance(field, target);
	uint32_t offset;

	for (offset = field->incoming_offsets[cell];
		offset < field->incoming_offsets[cell + 1U]; offset++)
	{
		const uint32_t arc_index = field->incoming_arcs[offset];
		const sg_rune_compact_field_arc_t *arc = &field->arcs[arc_index];
		uint32_t source_stance;

		if ((uint32_t)StanceIndex(arc->destination_stance) != stance)
			continue;
		for (source_stance = 0U; source_stance <
			(uint32_t)SG_RUNE_COMPACT_FIELD_STANCE_COUNT; source_stance++)
			if ((arc->source_stance & StanceBit(
				(sg_rune_compact_field_stance_t)source_stance)) != 0U)
			{
				ReportExamined(report);
				if (!QueueState(queue, source_stance *
					field->model->cell_count + arc->source))
					return 0;
			}
	}
	if (StateStanceChangeValid(field, target))
	{
		ReportExamined(report);
		if (!QueueState(queue, (stance == 0U ? 1U : 0U) *
			field->model->cell_count + cell))
			return 0;
	}
	return 1;
}

static sg_rune_compact_field_status_t BestStateCost(
	const sg_rune_compact_field_t *field, uint32_t state,
	const uint8_t *terminals, const uint64_t *costs, uint64_t *best_out,
	sg_rune_compact_field_refresh_report_t *report)
{
	const uint32_t cell = StateCell(field, state);
	const uint32_t stance = StateStance(field, state);
	uint64_t best = terminals[state] != 0U ? 0U : UINT64_MAX;
	uint32_t offset;

	if (terminals[state] != 0U)
	{
		*best_out = 0U;
		return SG_RUNE_COMPACT_FIELD_OK;
	}
	for (offset = field->outgoing_offsets[cell];
		offset < field->outgoing_offsets[cell + 1U]; offset++)
	{
		const uint32_t arc_index = field->outgoing_arcs[offset];
		const sg_rune_compact_field_arc_t *arc = &field->arcs[arc_index];
		uint64_t candidate;
		sg_rune_compact_field_status_t status;

		if ((arc->source_stance & StanceBit(
			(sg_rune_compact_field_stance_t)stance)) == 0U)
			continue;
		ReportExamined(report);
		status = AddCosts(costs[ArcTargetState(field, arc)],
			field->arc_costs[stance * field->arc_count + arc_index],
			&candidate);
		if (status != SG_RUNE_COMPACT_FIELD_OK)
			return status;
		if (candidate < best)
			best = candidate;
	}
	if (StateStanceChangeValid(field, state))
	{
		const uint32_t other = (stance == 0U ? 1U : 0U) *
			field->model->cell_count + cell;
		uint64_t candidate;
		sg_rune_compact_field_status_t status;

		ReportExamined(report);
		status = AddCosts(costs[other], field->stance_cost, &candidate);
		if (status != SG_RUNE_COMPACT_FIELD_OK)
			return status;
		if (candidate < best)
			best = candidate;
	}
	*best_out = best;
	return SG_RUNE_COMPACT_FIELD_OK;
}

static sg_rune_compact_field_status_t CompleteRefreshReport(
	const sg_rune_compact_field_t *field,
	const uint8_t *affected, const uint8_t *invalidated,
	const uint64_t *old_costs, const uint64_t *new_costs,
	sg_rune_compact_field_refresh_report_t *report)
{
	const uint32_t state_count = field->model->cell_count *
		(uint32_t)SG_RUNE_COMPACT_FIELD_STANCE_COUNT;
	const uint32_t leaf_count = SG_RuneCompactFieldRegionLeafCount(
		field->regions);
	const uint32_t coarse_count = SG_RuneCompactFieldRegionCoarseCount(
		field->regions);
	uint8_t *leaves;
	uint8_t *coarse;
	uint32_t state;

	if (report == NULL)
		return SG_RUNE_COMPACT_FIELD_OK;
	leaves = AllocateArray(leaf_count, sizeof(*leaves));
	coarse = AllocateArray(coarse_count, sizeof(*coarse));
	if ((leaf_count != 0U && leaves == NULL) ||
		(coarse_count != 0U && coarse == NULL))
	{
		free(leaves);
		free(coarse);
		return SG_RUNE_COMPACT_FIELD_ALLOCATION_FAILED;
	}
	for (state = 0U; state < state_count; state++)
	{
		const uint32_t cell = StateCell(field, state);

		if (affected[state] != 0U)
		{
			const uint32_t leaf = SG_RuneCompactFieldRegionCellLeaf(
				field->regions, cell);
			const uint32_t parent = SG_RuneCompactFieldRegionCellCoarse(
				field->regions, cell);

			report->affected_state_count++;
			leaves[leaf] = 1U;
			coarse[parent] = 1U;
		}
		if (invalidated[state] != 0U)
			report->invalidated_state_count++;
		if (new_costs[state] < old_costs[state])
			report->decreased_state_count++;
	}
	for (state = 0U; state < leaf_count; state++)
		if (leaves[state] != 0U)
			report->affected_leaf_region_count++;
	for (state = 0U; state < coarse_count; state++)
		if (coarse[state] != 0U)
			report->affected_coarse_region_count++;
	free(leaves);
	free(coarse);
	return SG_RUNE_COMPACT_FIELD_OK;
}

sg_rune_compact_field_status_t SG_RuneCompactFieldPlanDerive(
	const sg_rune_compact_destination_plan_t *previous,
	const sg_rune_compact_destination_t *destination,
	sg_rune_compact_destination_plan_t **plan_out,
	sg_rune_compact_field_refresh_report_t *report_out)
{
	const sg_rune_compact_field_t *field;
	sg_rune_compact_destination_plan_t *plan = NULL;
	field_state_queue_t queue;
	uint8_t *old_terminals = NULL;
	uint8_t *new_terminals = NULL;
	uint8_t *invalidated = NULL;
	uint8_t *affected = NULL;
	uint32_t state_count;
	uint32_t state;
	sg_rune_compact_field_status_t status;

	if (plan_out != NULL)
		*plan_out = NULL;
	if (report_out != NULL)
		memset(report_out, 0, sizeof(*report_out));
	if (previous == NULL || previous->field == NULL || destination == NULL ||
		plan_out == NULL)
		return SG_RUNE_COMPACT_FIELD_INVALID_ARGUMENT;
	field = previous->field;
	if (field->model->cell_count > UINT32_MAX /
		(uint32_t)SG_RUNE_COMPACT_FIELD_STANCE_COUNT)
		return SG_RUNE_COMPACT_FIELD_ALLOCATION_FAILED;
	state_count = field->model->cell_count *
		(uint32_t)SG_RUNE_COMPACT_FIELD_STANCE_COUNT;
	memset(&queue, 0, sizeof(queue));
	plan = (sg_rune_compact_destination_plan_t *)calloc(1U, sizeof(*plan));
	old_terminals = AllocateArray(state_count, sizeof(*old_terminals));
	new_terminals = AllocateArray(state_count, sizeof(*new_terminals));
	invalidated = AllocateArray(state_count, sizeof(*invalidated));
	affected = AllocateArray(state_count, sizeof(*affected));
	if (plan == NULL || old_terminals == NULL || new_terminals == NULL ||
		invalidated == NULL || affected == NULL ||
		!InitializeQueue(field, state_count, &queue))
	{
		status = SG_RUNE_COMPACT_FIELD_ALLOCATION_FAILED;
		goto failed;
	}
	plan->costs = AllocateArray(state_count, sizeof(*plan->costs));
	if (plan->costs == NULL)
	{
		status = SG_RUNE_COMPACT_FIELD_ALLOCATION_FAILED;
		goto failed;
	}
	plan->field = field;
	plan->destination = *destination;
	memcpy(plan->costs, previous->costs,
		(size_t)state_count * sizeof(*plan->costs));
	status = ResolveDestinationStates(field, &previous->destination,
		old_terminals);
	if (status != SG_RUNE_COMPACT_FIELD_OK)
		goto failed;
	status = ResolveDestinationStates(field, destination, new_terminals);
	if (status != SG_RUNE_COMPACT_FIELD_OK)
		goto failed;
	if (memcmp(old_terminals, new_terminals,
		(size_t)state_count * sizeof(*old_terminals)) == 0)
		goto complete;
	status = InvalidateRemovedTerminals(previous, old_terminals,
		new_terminals, invalidated, &queue, report_out);
	if (status != SG_RUNE_COMPACT_FIELD_OK)
		goto failed;
	for (state = 0U; state < state_count; state++)
	{
		if (invalidated[state] != 0U)
		{
			plan->costs[state] = UINT64_MAX;
			affected[state] = 1U;
			if (!QueueState(&queue, state))
			{
				status = SG_RUNE_COMPACT_FIELD_ALLOCATION_FAILED;
				goto failed;
			}
		}
		if (new_terminals[state] != 0U && plan->costs[state] != 0U)
		{
			plan->costs[state] = 0U;
			affected[state] = 1U;
			if (!EnqueueIncomingStates(field, state, &queue, report_out))
			{
				status = SG_RUNE_COMPACT_FIELD_ALLOCATION_FAILED;
				goto failed;
			}
		}
	}
	while (PopState(&queue, &state))
	{
		uint64_t best;

		status = BestStateCost(field, state, new_terminals, plan->costs,
			&best, report_out);
		if (status != SG_RUNE_COMPACT_FIELD_OK)
			goto failed;
		if (best < plan->costs[state])
		{
			plan->costs[state] = best;
			affected[state] = 1U;
			if (!EnqueueIncomingStates(field, state, &queue, report_out))
			{
				status = SG_RUNE_COMPACT_FIELD_ALLOCATION_FAILED;
				goto failed;
			}
		}
	}

complete:
	status = CompleteRefreshReport(field, affected, invalidated,
		previous->costs, plan->costs, report_out);
	if (status != SG_RUNE_COMPACT_FIELD_OK)
		goto failed;
	free(old_terminals);
	free(new_terminals);
	free(invalidated);
	free(affected);
	DestroyQueue(&queue);
	*plan_out = plan;
	return SG_RUNE_COMPACT_FIELD_OK;

failed:
	free(old_terminals);
	free(new_terminals);
	free(invalidated);
	free(affected);
	DestroyQueue(&queue);
	SG_RuneCompactFieldPlanDestroy(plan);
	if (report_out != NULL)
		memset(report_out, 0, sizeof(*report_out));
	return status;
}

int SG_RuneCompactFieldPlanCostAt(
	const sg_rune_compact_destination_plan_t *plan,
	sg_rune_compact_field_stance_t stance, uint32_t cell,
	sg_rune_compact_field_cost_t *cost_out)
{
	if (plan == NULL || plan->field == NULL || cost_out == NULL ||
		stance < SG_RUNE_COMPACT_FIELD_STANDING ||
		stance >= SG_RUNE_COMPACT_FIELD_STANCE_COUNT ||
		cell >= plan->field->model->cell_count)
		return 0;
	cost_out->units = plan->costs[(uint32_t)stance *
		plan->field->model->cell_count + cell];
	return 1;
}

uint32_t SG_RuneCompactFieldRegionCount(const sg_rune_compact_field_t *field)
{
	return field != NULL ? SG_RuneCompactFieldRegionCoarseCount(field->regions) :
		0U;
}

uint32_t SG_RuneCompactFieldCellRegion(
	const sg_rune_compact_field_t *field, uint32_t cell)
{
	return field != NULL ? SG_RuneCompactFieldRegionCellCoarse(field->regions,
		cell) : SG_RUNE_COMPACT_INDEX_NONE;
}

uint32_t SG_RuneCompactFieldDestinationRegion(
	const sg_rune_compact_field_t *field,
	const sg_rune_compact_destination_t *destination)
{
	uint8_t *terminals;
	uint32_t state_count;
	uint32_t state;
	uint32_t region = SG_RUNE_COMPACT_INDEX_NONE;

	if (field == NULL || destination == NULL ||
		field->model->cell_count > UINT32_MAX /
			(uint32_t)SG_RUNE_COMPACT_FIELD_STANCE_COUNT)
		return SG_RUNE_COMPACT_INDEX_NONE;
	state_count = field->model->cell_count *
		(uint32_t)SG_RUNE_COMPACT_FIELD_STANCE_COUNT;
	terminals = AllocateArray(state_count, sizeof(*terminals));
	if (terminals == NULL || ResolveDestinationStates(field, destination,
		terminals) != SG_RUNE_COMPACT_FIELD_OK)
	{
		free(terminals);
		return SG_RUNE_COMPACT_INDEX_NONE;
	}
	for (state = 0U; state < state_count; state++)
		if (terminals[state] != 0U)
		{
			const uint32_t candidate = SG_RuneCompactFieldRegionCellCoarse(
				field->regions, StateCell(field, state));

			if (region == SG_RUNE_COMPACT_INDEX_NONE)
				region = candidate;
			else if (region != candidate)
			{
				region = SG_RUNE_COMPACT_INDEX_NONE;
				break;
			}
		}
	free(terminals);
	return region;
}

sg_rune_compact_field_status_t SG_RuneCompactFieldPlanCreate(
	const sg_rune_compact_field_t *field,
	const sg_rune_compact_destination_t *destination,
	sg_rune_compact_destination_plan_t **plan_out)
{
	sg_rune_compact_destination_plan_t *plan;
	uint8_t *terminals;
	uint32_t cost_count;
	uint32_t index;
	sg_rune_compact_field_status_t status;

	if (field == NULL || destination == NULL || plan_out == NULL)
		return SG_RUNE_COMPACT_FIELD_INVALID_ARGUMENT;
	if (field->model->cell_count > UINT32_MAX /
		(uint32_t)SG_RUNE_COMPACT_FIELD_STANCE_COUNT)
		return SG_RUNE_COMPACT_FIELD_ALLOCATION_FAILED;
	cost_count = field->model->cell_count *
		(uint32_t)SG_RUNE_COMPACT_FIELD_STANCE_COUNT;
	plan = calloc(1U, sizeof(*plan));
	terminals = AllocateArray(cost_count, sizeof(*terminals));
	if (plan == NULL || terminals == NULL) {
		free(terminals);
		free(plan);
		return SG_RUNE_COMPACT_FIELD_ALLOCATION_FAILED;
	}
	plan->costs = AllocateArray(cost_count, sizeof(*plan->costs));
	if (plan->costs == NULL) {
		free(terminals);
		free(plan);
		return SG_RUNE_COMPACT_FIELD_ALLOCATION_FAILED;
	}
	plan->field = field;
	plan->destination = *destination;
	status = ResolveDestinationStates(field, destination, terminals);
	if (status != SG_RUNE_COMPACT_FIELD_OK) {
		free(terminals);
		SG_RuneCompactFieldPlanDestroy(plan);
		return status;
	}
	for (index = 0U; index < cost_count; index++)
		plan->costs[index] = terminals[index] != 0U ? 0U : UINT64_MAX;
	free(terminals);
	status = BuildCosts(field, plan->costs);
	if (status != SG_RUNE_COMPACT_FIELD_OK) {
		SG_RuneCompactFieldPlanDestroy(plan);
		return status;
	}
	*plan_out = plan;
	return SG_RUNE_COMPACT_FIELD_OK;
}

void SG_RuneCompactFieldPlanDestroy(
	sg_rune_compact_destination_plan_t *plan)
{
	if (plan == NULL)
		return;
	free(plan->costs);
	free(plan);
}

static int ContextValid(const sg_rune_compact_field_local_context_t *context)
{
	const int mover = context->support == SG_RUNE_MOVEMENT_SUPPORT_MOVER ||
		(context->state_flags & SG_RUNE_MOVEMENT_STATE_MOVER_RELATIVE) != 0U;
	uint32_t axis;

	if (context->stance < SG_RUNE_COMPACT_FIELD_STANDING ||
		context->stance >= SG_RUNE_COMPACT_FIELD_STANCE_COUNT ||
		context->support >= SG_RUNE_MOVEMENT_SUPPORT_KIND_COUNT ||
		context->water >= SG_RUNE_MOVEMENT_WATER_KIND_COUNT ||
		(uint32_t)context->hook_phase > (uint32_t)SG_HOST_HOOK_COAST ||
		(context->state_flags &
			~(sg_rune_movement_state_flags_t)
				SG_RUNE_MOVEMENT_STATE_FLAGS_KNOWN) != 0U ||
		(mover ? context->mover_mechanism == SG_RUNE_COMPACT_INDEX_NONE :
			context->mover_mechanism != SG_RUNE_COMPACT_INDEX_NONE) ||
		context->frame_sequence == 0U ||
		!isfinite(context->time_seconds) || !isfinite(context->distance) ||
		!isfinite(context->support_distance) ||
		!isfinite(context->fluid_fraction) ||
		!isfinite(context->hook_length) ||
		!isfinite(context->target_radius))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (!isfinite(context->velocity[axis]) ||
			!isfinite(context->direction[axis]))
			return 0;
	return 1;
}

static int FiberSourceMatchesContext(const sg_rune_compact_model_t *model,
	const sg_rune_compact_field_arc_t *arc,
	const sg_rune_compact_field_local_context_t *context)
{
	const sg_rune_compact_movement_fiber_t *fiber =
		&model->movement_fibers[arc->fiber];
	const sg_rune_compact_movement_state_t *state =
		&model->movement_states[fiber->source_state.value];
	const sg_rune_movement_state_variables_t variables = fiber->state_variables;

	if ((variables & SG_RUNE_MOVEMENT_STATE_SUPPORT) != 0U &&
		(state->support != context->support ||
		 (state->flags & SG_RUNE_MOVEMENT_STATE_AIRBORNE) !=
			(context->state_flags & SG_RUNE_MOVEMENT_STATE_AIRBORNE)))
		return 0;
	if ((variables & SG_RUNE_MOVEMENT_STATE_WATER) != 0U &&
		state->water != context->water)
		return 0;
	if ((variables & SG_RUNE_MOVEMENT_STATE_HOOK) != 0U &&
		state->hook_phase != context->hook_phase)
		return 0;
	if ((variables & SG_RUNE_MOVEMENT_STATE_MOVER) != 0U &&
		(state->mover_mechanism != context->mover_mechanism ||
		 (state->flags & SG_RUNE_MOVEMENT_STATE_MOVER_RELATIVE) !=
			(context->state_flags & SG_RUNE_MOVEMENT_STATE_MOVER_RELATIVE)))
		return 0;
	if ((variables & SG_RUNE_MOVEMENT_STATE_EXTERNAL_FORCE) != 0U &&
		(state->flags & SG_RUNE_MOVEMENT_STATE_FLAG_EXTERNAL_FORCE) !=
			(context->state_flags &
				SG_RUNE_MOVEMENT_STATE_FLAG_EXTERNAL_FORCE))
		return 0;
	return 1;
}

static int PortalRootSnapshotValid(const sg_rune_compact_field_t *field,
	const sg_rune_compact_field_local_context_t *context,
	const sg_rune_compact_field_portal_root_snapshot_t *snapshot)
{
	uint32_t index;

	/* A missing owner observation deliberately means every root is UNKNOWN.
	 * It is not malformed: query can return a structured wait requirement. */
	if (snapshot == NULL)
		return 1;
	if (snapshot->model_identity == NULL || snapshot->frame_sequence == 0U ||
		snapshot->frame_sequence != context->frame_sequence ||
		!SG_RuneCompactIdentityMatches(snapshot->model_identity,
			&field->expected_identity) ||
		snapshot->root_count != field->portal_root_count ||
		(snapshot->root_count != 0U && snapshot->roots == NULL))
		return 0;
	for (index = 0U; index < snapshot->root_count; index++) {
		const sg_rune_compact_field_portal_root_t *root =
			&snapshot->roots[index];

		if (root->portal.value != field->portal_root_portals[index].value ||
			root->mechanism.value !=
				field->portal_root_mechanisms[index].value ||
			root->state >= SG_RUNE_COMPACT_FIELD_PORTAL_ROOT_STATE_COUNT)
			return 0;
	}
	return 1;
}

static sg_rune_compact_field_mechanism_requirement_state_t
PortalRequirementState(const sg_rune_compact_field_t *field,
	const sg_rune_compact_field_portal_root_snapshot_t *snapshot,
	uint32_t portal)
{
	const uint32_t first = field->portal_root_offsets[portal];
	const uint32_t last = field->portal_root_offsets[portal + 1U];
	uint32_t index;
	int blocked = 0;
	int unknown = 0;

	if (first == last)
		return SG_RUNE_COMPACT_FIELD_MECHANISM_REQUIREMENTS_STATE_COUNT;
	if (snapshot == NULL)
		return SG_RUNE_COMPACT_FIELD_MECHANISM_REQUIREMENTS_UNKNOWN;
	for (index = first; index < last; index++) {
		switch (snapshot->roots[index].state) {
		case SG_RUNE_COMPACT_FIELD_PORTAL_ROOT_UNBLOCKED:
			break;
		case SG_RUNE_COMPACT_FIELD_PORTAL_ROOT_BLOCKED:
			blocked = 1;
			break;
		case SG_RUNE_COMPACT_FIELD_PORTAL_ROOT_UNKNOWN:
			unknown = 1;
			break;
		case SG_RUNE_COMPACT_FIELD_PORTAL_ROOT_STATE_COUNT:
		default:
			unknown = 1;
			break;
		}
	}
	/* A known closed root is a concrete blocker.  UNKNOWN only wins when no
	 * root is known closed, irrespective of the field-owned root ordering. */
	if (blocked)
		return SG_RUNE_COMPACT_FIELD_MECHANISM_REQUIREMENTS_BLOCKED;
	return unknown ? SG_RUNE_COMPACT_FIELD_MECHANISM_REQUIREMENTS_UNKNOWN :
		SG_RUNE_COMPACT_FIELD_MECHANISM_REQUIREMENTS_STATE_COUNT;
}

static int MechanismSnapshotValid(const sg_rune_compact_field_t *field,
	const sg_rune_compact_field_local_context_t *context,
	const sg_rune_compact_field_mechanism_snapshot_t *snapshot)
{
	uint32_t index;

	if (snapshot == NULL)
		return 1;
	if (context == NULL || snapshot->model_identity == NULL ||
		snapshot->frame_sequence == 0U ||
		snapshot->frame_sequence != context->frame_sequence ||
		!SG_RuneCompactIdentityMatches(snapshot->model_identity,
			&field->expected_identity) ||
		(snapshot->phase_count != 0U && snapshot->phases == NULL))
		return 0;
	for (index = 0U; index < snapshot->phase_count; index++) {
		const sg_rune_compact_field_mechanism_phase_t *phase =
			&snapshot->phases[index];

		if (phase->mechanism.value >=
				field->model->mechanism_authority_count ||
			!isfinite(phase->phase) ||
			(index != 0U && snapshot->phases[index - 1U].mechanism.value >=
				phase->mechanism.value))
			return 0;
	}
	return 1;
}

static int FunctionsUseMoverPhase(const sg_rune_compact_model_t *model,
	sg_rune_analytic_function_span_t functions)
{
	uint32_t reference_offset;

	for (reference_offset = 0U;
		reference_offset < functions.count; reference_offset++) {
		const uint32_t reference = functions.first +
			reference_offset;
		const sg_rune_analytic_function_t *function =
			&model->analytic->functions[
				model->movement_fiber_function_refs[reference].value];
		uint32_t input_offset;

		for (input_offset = 0U; input_offset < function->inputs.count;
			input_offset++)
			if (model->analytic->input_dimensions[
				function->inputs.first + input_offset] ==
				SG_RUNE_ANALYTIC_INPUT_MOVER_PHASE)
				return 1;
	}
	return 0;
}

static sg_rune_compact_field_status_t FiberMechanismPhase(
	const sg_rune_compact_field_t *field,
	const sg_rune_compact_field_arc_t *arc,
	const sg_rune_compact_field_mechanism_snapshot_t *snapshot,
	sg_rune_authority_mechanism_index_t *mechanism_out, float *phase_out)
{
	const sg_rune_compact_model_t *model = field->model;
	const sg_rune_compact_movement_fiber_t *fiber =
		&model->movement_fibers[arc->fiber];
	uint32_t mechanism = SG_RUNE_COMPACT_INDEX_NONE;
	uint32_t index;

	mechanism_out->value = SG_RUNE_COMPACT_INDEX_NONE;
	*phase_out = 0.0f;
	if (!FunctionsUseMoverPhase(model, ArcFunctions(model, arc)))
		return SG_RUNE_COMPACT_FIELD_OK;
	if (fiber->kind == SG_RUNE_MOVEMENT_FIBER_MECHANISM_TRANSITION) {
		const sg_rune_compact_mechanism_transition_t *transition =
			&model->mechanism_authority_transitions[
				fiber->mechanism_transition.value];

		mechanism = transition->mechanism;
		if (arc->portal != SG_RUNE_COMPACT_INDEX_NONE &&
			(transition->kind !=
				SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE ||
			 transition->value.portal_state.portal.value != arc->portal))
			return SG_RUNE_COMPACT_FIELD_MECHANISM_PHASE_REQUIRED;
	} else if (fiber->kind == SG_RUNE_MOVEMENT_FIBER_ANGULAR_MOVER) {
		mechanism = model->movement_angular_schedules[
			fiber->angular_schedule].authority_mechanism.value;
	} else {
		return SG_RUNE_COMPACT_FIELD_MECHANISM_PHASE_REQUIRED;
	}
	if (snapshot == NULL)
		return SG_RUNE_COMPACT_FIELD_MECHANISM_PHASE_REQUIRED;
	for (index = 0U; index < snapshot->phase_count; index++)
		if (snapshot->phases[index].mechanism.value == mechanism) {
			mechanism_out->value = mechanism;
			*phase_out = snapshot->phases[index].phase;
			return SG_RUNE_COMPACT_FIELD_OK;
		}
	return SG_RUNE_COMPACT_FIELD_MECHANISM_PHASE_REQUIRED;
}

static void BuildInputs(const sg_rune_compact_field_local_context_t *context,
	float mover_phase,
	sg_rune_compact_eval_input_t inputs[SG_RUNE_ANALYTIC_INPUT_DIMENSION_COUNT])
{
	uint32_t dimension;

	for (dimension = 0U;
		dimension < (uint32_t)SG_RUNE_ANALYTIC_INPUT_DIMENSION_COUNT;
		dimension++)
		inputs[dimension].dimension =
			(sg_rune_analytic_input_dimension_t)dimension;
	inputs[SG_RUNE_ANALYTIC_INPUT_WORLD_X].value =
		(float)context->origin.value[0] / 8.0f;
	inputs[SG_RUNE_ANALYTIC_INPUT_WORLD_Y].value =
		(float)context->origin.value[1] / 8.0f;
	inputs[SG_RUNE_ANALYTIC_INPUT_WORLD_Z].value =
		(float)context->origin.value[2] / 8.0f;
	inputs[SG_RUNE_ANALYTIC_INPUT_VELOCITY_X].value = context->velocity[0];
	inputs[SG_RUNE_ANALYTIC_INPUT_VELOCITY_Y].value = context->velocity[1];
	inputs[SG_RUNE_ANALYTIC_INPUT_VELOCITY_Z].value = context->velocity[2];
	inputs[SG_RUNE_ANALYTIC_INPUT_DIRECTION_X].value = context->direction[0];
	inputs[SG_RUNE_ANALYTIC_INPUT_DIRECTION_Y].value = context->direction[1];
	inputs[SG_RUNE_ANALYTIC_INPUT_DIRECTION_Z].value = context->direction[2];
	inputs[SG_RUNE_ANALYTIC_INPUT_TIME_SECONDS].value = context->time_seconds;
	inputs[SG_RUNE_ANALYTIC_INPUT_DISTANCE].value = context->distance;
	inputs[SG_RUNE_ANALYTIC_INPUT_SUPPORT_DISTANCE].value =
		context->support_distance;
	inputs[SG_RUNE_ANALYTIC_INPUT_FLUID_FRACTION].value =
		context->fluid_fraction;
	inputs[SG_RUNE_ANALYTIC_INPUT_MOVER_PHASE].value = mover_phase;
	inputs[SG_RUNE_ANALYTIC_INPUT_HOOK_LENGTH].value = context->hook_length;
	inputs[SG_RUNE_ANALYTIC_INPUT_TARGET_RADIUS].value = context->target_radius;
}

static sg_rune_compact_field_status_t EvaluateFiberFunctions(
	const sg_rune_compact_model_t *model,
	sg_rune_analytic_function_span_t functions,
	const sg_rune_compact_eval_input_t *inputs,
	float *cost_out, float *travel_time_out, int *reachable_out)
{
	float cost = 0.0f;
	float travel_time = 0.0f;
	float reachability = 0.0f;
	int has_cost = 0;
	int has_travel_time = 0;
	int has_reachability = 0;
	uint32_t offset;

	for (offset = 0U; offset < functions.count; offset++) {
		const uint32_t reference = functions.first + offset;
		sg_rune_compact_eval_query_t query;
		sg_rune_compact_eval_result_t result;

		query.function = model->movement_fiber_function_refs[reference];
		query.inputs = inputs;
		query.input_count =
			(uint32_t)SG_RUNE_ANALYTIC_INPUT_DIMENSION_COUNT;
		if (SG_RuneCompactEval(model->analytic, &query, &result) !=
			SG_RUNE_COMPACT_EVAL_OK)
			return SG_RUNE_COMPACT_FIELD_EVALUATION_FAILED;
		switch (result.output) {
		case SG_RUNE_ANALYTIC_OUTPUT_COST:
			cost = result.value;
			has_cost = 1;
			break;
		case SG_RUNE_ANALYTIC_OUTPUT_TRAVEL_TIME_SECONDS:
			travel_time = result.value;
			has_travel_time = 1;
			break;
		case SG_RUNE_ANALYTIC_OUTPUT_REACHABILITY_MARGIN:
			reachability = result.value;
			has_reachability = 1;
			break;
		default:
			break;
		}
	}
	if (!has_cost || !has_travel_time || !has_reachability)
		return SG_RUNE_COMPACT_FIELD_EVALUATION_FAILED;
	if (!isfinite(cost) || cost < 0.0f || !isfinite(travel_time) ||
		travel_time < 0.0f)
		return SG_RUNE_COMPACT_FIELD_INVALID_TRANSITION_VALUE;
	*cost_out = cost;
	*travel_time_out = travel_time;
	*reachable_out = reachability >= 0.0f;
	return SG_RUNE_COMPACT_FIELD_OK;
}

static int CandidateEarlier(uint64_t cost,
	sg_rune_compact_field_transition_kind_t kind,
	const sg_rune_compact_field_arc_t *arc, uint64_t best_cost,
	const sg_rune_compact_field_step_t *best)
{
	if (cost != best_cost)
		return cost < best_cost;
	if (kind != best->kind)
		return kind < best->kind;
	if (kind == SG_RUNE_COMPACT_FIELD_TRANSITION_PORTAL) {
		if (arc->portal != best->value.portal.next_portal.value)
			return arc->portal < best->value.portal.next_portal.value;
		return arc->target < best->value.portal.next_cell.value;
	}
	if (kind == SG_RUNE_COMPACT_FIELD_TRANSITION_DIRECT)
		return arc->target < best->value.direct.next_cell.value;
	return 0;
}

static int RequirementEarlier(uint64_t target_cost, uint32_t portal,
	uint64_t current_target_cost, uint32_t current_portal)
{
	if (target_cost != current_target_cost)
		return target_cost < current_target_cost;
	return portal < current_portal;
}

static void InitializeBlockedResult(sg_rune_compact_field_result_t *result,
	sg_rune_compact_cell_index_t current_cell,
	sg_rune_compact_field_stance_t stance)
{
	memset(result, 0, sizeof(*result));
	result->kind = SG_RUNE_COMPACT_FIELD_BLOCKED_NOW;
	result->current_cell = current_cell;
	result->value.step.kind = SG_RUNE_COMPACT_FIELD_TRANSITION_PORTAL;
	result->value.step.target_stance = stance;
	result->value.step.cost_to_go.units = UINT64_MAX;
	result->value.step.next_cost_to_go.units = UINT64_MAX;
	result->value.step.value.portal.local_cost = INFINITY;
	result->value.step.value.portal.next_cell.value =
		SG_RUNE_COMPACT_INDEX_NONE;
	result->value.step.value.portal.next_portal.value =
		SG_RUNE_COMPACT_INDEX_NONE;
}

sg_rune_compact_field_status_t SG_RuneCompactFieldQuery(
	const sg_rune_compact_destination_plan_t *plan,
	const sg_rune_compact_field_local_context_t *context,
	sg_rune_compact_field_result_t *result_out)
{
	const sg_rune_compact_field_t *field;
	const sg_rune_compact_model_t *model;
	sg_rune_compact_location_t location;
	sg_rune_compact_field_result_t result = { 0 };
	sg_rune_compact_eval_input_t
		inputs[SG_RUNE_ANALYTIC_INPUT_DIMENSION_COUNT];
	sg_rune_stance_validity_t stance;
	uint32_t stance_index;
	uint64_t source_cost;
	uint64_t best_cost = UINT64_MAX;
	uint32_t arc_offset;
	int found = 0;
	int requirements_found = 0;
	uint64_t requirement_target_cost = UINT64_MAX;
	uint32_t requirement_portal = SG_RUNE_COMPACT_INDEX_NONE;
	sg_rune_compact_field_mechanism_requirements_t requirements;

	if (plan == NULL || context == NULL || result_out == NULL)
		return SG_RUNE_COMPACT_FIELD_INVALID_ARGUMENT;
	if (!ContextValid(context))
		return SG_RUNE_COMPACT_FIELD_INVALID_CONTEXT;
	field = plan->field;
	model = field->model;
	if (context->mover_mechanism != SG_RUNE_COMPACT_INDEX_NONE &&
		context->mover_mechanism >= model->mechanism_authority_count)
		return SG_RUNE_COMPACT_FIELD_INVALID_CONTEXT;
	if (!MechanismSnapshotValid(field, context, context->mechanisms))
		return SG_RUNE_COMPACT_FIELD_INVALID_MECHANISM_SNAPSHOT;
	if (!PortalRootSnapshotValid(field, context, context->portal_roots))
		return SG_RUNE_COMPACT_FIELD_INVALID_PORTAL_ROOT_SNAPSHOT;
	if (SG_RuneCompactLocalize(model, &context->origin, &location) !=
		SG_RUNE_COMPACT_LOCALIZE_OK)
		return SG_RUNE_COMPACT_FIELD_LOCALIZATION_FAILED;
	stance_index = (uint32_t)context->stance;
	stance = StanceBit(context->stance);
	if ((location.valid_stances & stance) == 0U)
		return SG_RUNE_COMPACT_FIELD_INVALID_CONTEXT;
	result.current_cell = location.cell;
	source_cost = plan->costs[stance_index * model->cell_count +
		location.cell.value];
	if (source_cost == UINT64_MAX) {
		result.kind = SG_RUNE_COMPACT_FIELD_DISCONNECTED;
		*result_out = result;
		return SG_RUNE_COMPACT_FIELD_OK;
	}
	if (source_cost == 0U) {
		result.kind = plan->destination.kind == SG_RUNE_COMPACT_DESTINATION_CELL ?
			SG_RUNE_COMPACT_FIELD_CELL_DESTINATION :
			SG_RUNE_COMPACT_FIELD_LOCAL_DESTINATION;
		result.value.destination = plan->destination;
		*result_out = result;
		return SG_RUNE_COMPACT_FIELD_OK;
	}
	{
		const uint32_t other_stance = stance_index == 0U ? 1U : 0U;
		const uint64_t other_cost = plan->costs[
			other_stance * model->cell_count + location.cell.value];
		uint64_t stance_cost;

		if (other_cost < source_cost && QuantizeCost(
			(float)model->identity.physics.frame_ms / 1000.0f,
			&stance_cost) == SG_RUNE_COMPACT_FIELD_OK) {
			const sg_rune_compact_field_status_t add_status = AddCosts(
				other_cost, stance_cost, &best_cost);

			if (add_status != SG_RUNE_COMPACT_FIELD_OK)
				return add_status;
			found = 1;
			result.kind = SG_RUNE_COMPACT_FIELD_STEP;
			result.value.step.kind = SG_RUNE_COMPACT_FIELD_TRANSITION_STANCE;
			result.value.step.cost_to_go.units = source_cost;
			result.value.step.next_cost_to_go.units = other_cost;
			result.value.step.target_stance =
				(sg_rune_compact_field_stance_t)other_stance;
		}
	}
	memset(&requirements, 0, sizeof(requirements));
	for (arc_offset = field->outgoing_offsets[location.cell.value];
		arc_offset < field->outgoing_offsets[location.cell.value + 1U];
		arc_offset++) {
		const sg_rune_compact_field_arc_t *arc = &field->arcs[
			field->outgoing_arcs[arc_offset]];
		const uint32_t destination_stance = (uint32_t)StanceIndex(
			arc->destination_stance);
		const uint64_t target_cost = plan->costs[
			destination_stance * model->cell_count + arc->target];
		const sg_rune_compact_field_transition_kind_t transition_kind =
			arc->portal == SG_RUNE_COMPACT_INDEX_NONE ?
				SG_RUNE_COMPACT_FIELD_TRANSITION_DIRECT :
				SG_RUNE_COMPACT_FIELD_TRANSITION_PORTAL;
		sg_rune_compact_field_mechanism_requirement_state_t requirement_state =
			SG_RUNE_COMPACT_FIELD_MECHANISM_REQUIREMENTS_STATE_COUNT;
		float arc_local_cost = INFINITY;
		uint64_t arc_local_quantized = UINT64_MAX;
		int arc_found = 0;

		if ((arc->source_stance & stance) == 0U ||
			!FiberSourceMatchesContext(model, arc, context) ||
			target_cost >= source_cost)
			continue;
		if (arc->portal != SG_RUNE_COMPACT_INDEX_NONE)
			requirement_state = PortalRequirementState(field,
				context->portal_roots, arc->portal);
		if (requirement_state !=
			SG_RUNE_COMPACT_FIELD_MECHANISM_REQUIREMENTS_STATE_COUNT) {
			if (!requirements_found || RequirementEarlier(target_cost,
				arc->portal, requirement_target_cost, requirement_portal)) {
				const uint32_t first = field->portal_root_offsets[arc->portal];
				const uint32_t count = field->portal_root_offsets[arc->portal + 1U] -
					first;

				requirements_found = 1;
				requirement_target_cost = target_cost;
				requirement_portal = arc->portal;
				requirements.portal.value = arc->portal;
				requirements.mechanisms = &field->portal_root_mechanisms[first];
				requirements.mechanism_count = count;
				requirements.state = requirement_state;
			}
			continue;
		}
		{
			float cost;
			float travel_time;
			float mover_phase;
			uint64_t quantized_cost;
			int reachable;
			sg_rune_authority_mechanism_index_t mechanism;
			sg_rune_compact_field_status_t status;

			status = FiberMechanismPhase(field, arc,
				context->mechanisms, &mechanism, &mover_phase);
			if (status != SG_RUNE_COMPACT_FIELD_OK)
				return status;
			BuildInputs(context, mover_phase, inputs);
			status = EvaluateFiberFunctions(model, ArcFunctions(model, arc), inputs,
				&cost, &travel_time, &reachable);
			if (status != SG_RUNE_COMPACT_FIELD_OK)
				return status;
			if (!reachable)
				continue;
			status = QuantizeCost(cost, &quantized_cost);
			if (status != SG_RUNE_COMPACT_FIELD_OK)
				return status;
			arc_found = 1;
			arc_local_cost = cost;
			arc_local_quantized = quantized_cost;
		}
		if (arc_found) {
			uint64_t candidate_cost;
			const sg_rune_compact_field_status_t add_status = AddCosts(
				arc_local_quantized, target_cost, &candidate_cost);

			if (add_status != SG_RUNE_COMPACT_FIELD_OK)
				return add_status;
			if (found && !CandidateEarlier(candidate_cost,
				transition_kind, arc, best_cost,
				&result.value.step))
				continue;
			found = 1;
			best_cost = candidate_cost;
			result.kind = SG_RUNE_COMPACT_FIELD_STEP;
			result.value.step.kind = transition_kind;
			result.value.step.cost_to_go.units = source_cost;
			result.value.step.next_cost_to_go.units = target_cost;
			result.value.step.target_stance =
				(sg_rune_compact_field_stance_t)destination_stance;
			if (transition_kind == SG_RUNE_COMPACT_FIELD_TRANSITION_PORTAL) {
				result.value.step.value.portal.local_cost = arc_local_cost;
				result.value.step.value.portal.next_cell.value = arc->target;
				result.value.step.value.portal.next_portal.value = arc->portal;
			} else {
				result.value.step.value.direct.local_cost = arc_local_cost;
				result.value.step.value.direct.next_cell.value = arc->target;
			}
		}
	}
	if (!found && requirements_found) {
		result.kind = SG_RUNE_COMPACT_FIELD_MECHANISMS_REQUIRED;
		result.value.requirements = requirements;
		*result_out = result;
		return SG_RUNE_COMPACT_FIELD_OK;
	}
	if (!found)
		InitializeBlockedResult(&result, location.cell, context->stance);
	*result_out = result;
	return SG_RUNE_COMPACT_FIELD_OK;
}

static int ExactStepEqual(const sg_rune_compact_field_result_t *left,
	const sg_rune_compact_field_result_t *right)
{
	if (left == NULL || right == NULL || left->kind != SG_RUNE_COMPACT_FIELD_STEP ||
		right->kind != SG_RUNE_COMPACT_FIELD_STEP ||
		left->current_cell.value != right->current_cell.value ||
		left->value.step.kind != right->value.step.kind ||
		left->value.step.cost_to_go.units != right->value.step.cost_to_go.units ||
		left->value.step.next_cost_to_go.units !=
			right->value.step.next_cost_to_go.units ||
		left->value.step.target_stance != right->value.step.target_stance)
		return 0;
	switch (left->value.step.kind) {
	case SG_RUNE_COMPACT_FIELD_TRANSITION_PORTAL:
		return memcmp(&left->value.step.value.portal.local_cost,
			&right->value.step.value.portal.local_cost, sizeof(float)) == 0 &&
			left->value.step.value.portal.next_cell.value ==
				right->value.step.value.portal.next_cell.value &&
			left->value.step.value.portal.next_portal.value ==
				right->value.step.value.portal.next_portal.value;
	case SG_RUNE_COMPACT_FIELD_TRANSITION_DIRECT:
		return memcmp(&left->value.step.value.direct.local_cost,
			&right->value.step.value.direct.local_cost, sizeof(float)) == 0 &&
			left->value.step.value.direct.next_cell.value ==
				right->value.step.value.direct.next_cell.value;
	case SG_RUNE_COMPACT_FIELD_TRANSITION_STANCE:
		return 1;
	case SG_RUNE_COMPACT_FIELD_TRANSITION_KIND_COUNT:
	default:
		return 0;
	}
}

static int ExactArcMatchesStep(const sg_rune_compact_field_arc_t *arc,
	sg_rune_compact_field_transition_kind_t kind,
	sg_rune_compact_field_stance_t stance, float local_cost,
	const sg_rune_compact_field_step_t *step)
{
	if (arc == NULL || step == NULL || kind != step->kind ||
		stance != step->target_stance)
		return 0;
	if (kind == SG_RUNE_COMPACT_FIELD_TRANSITION_PORTAL)
		return arc->target == step->value.portal.next_cell.value &&
			arc->portal == step->value.portal.next_portal.value &&
			memcmp(&local_cost, &step->value.portal.local_cost,
				sizeof(local_cost)) == 0;
	if (kind == SG_RUNE_COMPACT_FIELD_TRANSITION_DIRECT)
		return arc->target == step->value.direct.next_cell.value &&
			arc->portal == SG_RUNE_COMPACT_INDEX_NONE &&
			memcmp(&local_cost, &step->value.direct.local_cost,
				sizeof(local_cost)) == 0;
	return 0;
}

sg_rune_compact_field_status_t SG_RuneCompactFieldPlanVisitExactStepProbes(
	const sg_rune_compact_destination_plan_t *plan,
	const sg_rune_compact_field_local_context_t *context,
	const sg_rune_compact_field_result_t *expected_result,
	sg_rune_compact_field_exact_probe_visit_fn visit, void *visit_context,
	uint32_t *probe_count_out)
{
	const sg_rune_compact_field_t *field;
	const sg_rune_compact_model_t *model;
	sg_rune_compact_field_result_t actual;
	sg_rune_compact_eval_input_t
		inputs[SG_RUNE_ANALYTIC_INPUT_DIMENSION_COUNT];
	sg_rune_compact_location_t location;
	sg_rune_stance_validity_t source_stance;
	uint64_t source_cost;
	uint32_t arc_offset;
	uint32_t count = 0U;
	sg_rune_compact_field_status_t status;

	if (probe_count_out != NULL)
		*probe_count_out = 0U;
	if (plan == NULL || context == NULL || expected_result == NULL ||
		visit == NULL || probe_count_out == NULL)
		return SG_RUNE_COMPACT_FIELD_INVALID_ARGUMENT;
	field = plan->field;
	model = field->model;
	status = SG_RuneCompactFieldQuery(plan, context, &actual);
	if (status != SG_RUNE_COMPACT_FIELD_OK ||
		!ExactStepEqual(&actual, expected_result))
		return status == SG_RUNE_COMPACT_FIELD_OK ?
			SG_RUNE_COMPACT_FIELD_INVALID_TRANSITION_VALUE : status;
	if (actual.value.step.kind == SG_RUNE_COMPACT_FIELD_TRANSITION_STANCE) {
		sg_rune_compact_field_exact_probe_t probe;

		memset(&probe, 0, sizeof(probe));
		probe.transition_kind = SG_RUNE_COMPACT_FIELD_TRANSITION_STANCE;
		probe.successor_cell = actual.current_cell;
		probe.portal.value = SG_RUNE_COMPACT_INDEX_NONE;
		probe.successor_stance = actual.value.step.target_stance;
		probe.local_cost.units = field->stance_cost;
		probe.travel_time_seconds =
			(float)model->identity.physics.frame_ms / 1000.0f;
		probe.provenance.kind =
			SG_RUNE_COMPACT_FIELD_PROBE_INTRINSIC_STANCE;
		probe.provenance.value.intrinsic_stance.cell = actual.current_cell;
		probe.provenance.value.intrinsic_stance.source_stance = context->stance;
		probe.provenance.value.intrinsic_stance.destination_stance =
			actual.value.step.target_stance;
		probe.provenance.value.intrinsic_stance.frame_ms =
			model->identity.physics.frame_ms;
		if (!visit(visit_context, &probe))
			return SG_RUNE_COMPACT_FIELD_INVALID_ARGUMENT;
		*probe_count_out = 1U;
		return SG_RUNE_COMPACT_FIELD_OK;
	}
	if (SG_RuneCompactLocalize(model, &context->origin, &location) !=
		SG_RUNE_COMPACT_LOCALIZE_OK)
		return SG_RUNE_COMPACT_FIELD_LOCALIZATION_FAILED;
	source_stance = StanceBit(context->stance);
	source_cost = plan->costs[(uint32_t)context->stance * model->cell_count +
		location.cell.value];
	for (arc_offset = field->outgoing_offsets[location.cell.value];
		arc_offset < field->outgoing_offsets[location.cell.value + 1U];
		arc_offset++) {
		const uint32_t arc_index = field->outgoing_arcs[arc_offset];
		const sg_rune_compact_field_arc_t *arc = &field->arcs[arc_index];
		const sg_rune_compact_movement_fiber_t *fiber =
			&model->movement_fibers[arc->fiber];
		const uint32_t destination_stance = (uint32_t)StanceIndex(
			arc->destination_stance);
		const uint64_t target_cost = plan->costs[
			destination_stance * model->cell_count + arc->target];
		const sg_rune_compact_field_transition_kind_t transition_kind =
			arc->portal == SG_RUNE_COMPACT_INDEX_NONE ?
				SG_RUNE_COMPACT_FIELD_TRANSITION_DIRECT :
				SG_RUNE_COMPACT_FIELD_TRANSITION_PORTAL;
		sg_rune_compact_field_exact_probe_t probe;
		sg_rune_authority_mechanism_index_t mechanism;
		float mover_phase;
		float local_cost;
		float travel_time;
		uint64_t local_quantized;
		int reachable;

		if ((arc->source_stance & source_stance) == 0U ||
			!FiberSourceMatchesContext(model, arc, context) ||
			target_cost >= source_cost ||
			(arc->portal != SG_RUNE_COMPACT_INDEX_NONE &&
			 PortalRequirementState(field, context->portal_roots, arc->portal) !=
			 SG_RUNE_COMPACT_FIELD_MECHANISM_REQUIREMENTS_STATE_COUNT))
			continue;
		status = FiberMechanismPhase(field, arc, context->mechanisms,
			&mechanism, &mover_phase);
		if (status != SG_RUNE_COMPACT_FIELD_OK)
			return status;
		BuildInputs(context, mover_phase, inputs);
		status = EvaluateFiberFunctions(model, ArcFunctions(model, arc), inputs,
			&local_cost, &travel_time, &reachable);
		if (status != SG_RUNE_COMPACT_FIELD_OK)
			return status;
		if (!reachable)
			continue;
		status = QuantizeCost(local_cost, &local_quantized);
		if (status != SG_RUNE_COMPACT_FIELD_OK)
			return status;
		if (target_cost !=
			expected_result->value.step.next_cost_to_go.units ||
			!ExactArcMatchesStep(arc, transition_kind,
				(sg_rune_compact_field_stance_t)destination_stance,
				local_cost, &expected_result->value.step))
			continue;
		memset(&probe, 0, sizeof(probe));
		probe.transition_kind = transition_kind;
		probe.successor_cell.value = arc->target;
		probe.portal.value = arc->portal;
		probe.successor_stance =
			(sg_rune_compact_field_stance_t)destination_stance;
		probe.local_cost.units = local_quantized;
		probe.travel_time_seconds = travel_time;
		{
			sg_rune_compact_field_movement_probe_t *movement;

			switch (fiber->kind) {
			case SG_RUNE_MOVEMENT_FIBER_PMOVE:
				probe.provenance.kind = SG_RUNE_COMPACT_FIELD_PROBE_PMOVE;
				movement = &probe.provenance.value.pmove.movement;
				break;
			case SG_RUNE_MOVEMENT_FIBER_HOOK:
				probe.provenance.kind = SG_RUNE_COMPACT_FIELD_PROBE_HOOK;
				movement = &probe.provenance.value.hook.movement;
				probe.provenance.value.hook.hook_target = arc->hook_target;
				break;
			case SG_RUNE_MOVEMENT_FIBER_MECHANISM_TRANSITION:
				probe.provenance.kind =
					SG_RUNE_COMPACT_FIELD_PROBE_MECHANISM_TRANSITION;
				movement = &probe.provenance.value.mechanism.movement;
				probe.provenance.value.mechanism.mechanism_transition =
					fiber->mechanism_transition;
				probe.provenance.value.mechanism.controller =
					fiber->controller_action_controller;
				probe.provenance.value.mechanism.controller_target =
					fiber->controller_action_target;
				probe.provenance.value.mechanism.mechanism_kind =
					model->mechanism_authority_transitions[
						fiber->mechanism_transition.value].kind;
				break;
			case SG_RUNE_MOVEMENT_FIBER_ANGULAR_MOVER:
				probe.provenance.kind =
					SG_RUNE_COMPACT_FIELD_PROBE_ANGULAR_MOVER;
				movement = &probe.provenance.value.angular_mover.movement;
				probe.provenance.value.angular_mover.angular_schedule =
					fiber->angular_schedule;
				break;
			case SG_RUNE_MOVEMENT_FIBER_KIND_COUNT:
			default:
				return SG_RUNE_COMPACT_FIELD_INVALID_TRANSITION_VALUE;
			}
			movement->field_arc = arc_index;
			movement->capability.value = arc->capability;
			movement->fiber.value = arc->fiber;
			movement->movement_kind =
				model->movement_capabilities[arc->capability].kind;
			movement->source_state =
				model->movement_states[fiber->source_state.value];
			movement->destination_state =
				model->movement_states[fiber->destination_state.value];
		}
		if (!visit(visit_context, &probe))
			return SG_RUNE_COMPACT_FIELD_INVALID_ARGUMENT;
		if (count == UINT32_MAX)
			return SG_RUNE_COMPACT_FIELD_ALLOCATION_FAILED;
		count++;
		(void)mechanism;
	}
	*probe_count_out = count;
	return SG_RUNE_COMPACT_FIELD_OK;
}

const char *SG_RuneCompactFieldStatusString(
	sg_rune_compact_field_status_t status)
{
	static const char *const names[SG_RUNE_COMPACT_FIELD_STATUS_COUNT] = {
		"ok",
		"invalid argument",
		"invalid model",
		"invalid destination",
		"invalid local context",
		"localization failed",
		"invalid mechanism snapshot",
		"invalid portal root snapshot",
		"mechanism phase required",
		"analytic evaluation failed",
		"invalid transition value",
		"cost overflow",
		"allocation failed"
	};

	return (uint32_t)status < (uint32_t)SG_RUNE_COMPACT_FIELD_STATUS_COUNT ?
		names[status] : "unknown compact field status";
}

uint32_t SG_RuneCompactFieldPortalRootCount(
	const sg_rune_compact_field_t *field)
{
	return field != NULL ? field->portal_root_count : 0U;
}

int SG_RuneCompactFieldPortalRootAt(const sg_rune_compact_field_t *field,
	uint32_t root_index, sg_rune_compact_portal_index_t *portal_out,
	sg_rune_compact_mechanism_index_t *mechanism_out)
{
	if (field == NULL || portal_out == NULL || mechanism_out == NULL ||
		root_index >= field->portal_root_count)
		return 0;
	*portal_out = field->portal_root_portals[root_index];
	*mechanism_out = field->portal_root_mechanisms[root_index];
	return 1;
}
