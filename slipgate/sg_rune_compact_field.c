#include "sg_rune_compact_field.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>

typedef struct sg_rune_compact_field_arc_s
{
	uint32_t source;
	uint32_t target;
	uint32_t portal;
	sg_rune_stance_validity_t valid_stances;
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
};

struct sg_rune_compact_destination_plan_s
{
	const sg_rune_compact_field_t *field;
	sg_rune_compact_destination_t destination;
	uint32_t *ranks;
};

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

static int FieldApplies(const sg_rune_movement_field_attachment_t *attachment,
	uint32_t cell, uint32_t portal)
{
	return attachment->cell.value == cell &&
		(attachment->boundary_portal.value == SG_RUNE_COMPACT_INDEX_NONE ||
		 attachment->boundary_portal.value == portal);
}

static sg_rune_stance_validity_t ArcStances(
	const sg_rune_compact_model_t *model, uint32_t source, uint32_t target,
	uint32_t portal_index)
{
	const sg_rune_compact_cell_t *cell = &model->cells[source];
	const sg_rune_compact_portal_t *portal = &model->portals[portal_index];
	sg_rune_stance_validity_t attachment_stances = 0U;
	uint32_t field_index;

	for (field_index = cell->movement_fields.first;
		field_index < cell->movement_fields.first + cell->movement_fields.count;
		field_index++) {
		const sg_rune_movement_field_attachment_t *attachment =
			&model->movement_fields[field_index];

		if (FieldApplies(attachment, source, portal_index))
			attachment_stances = (sg_rune_stance_validity_t)(
				attachment_stances | attachment->valid_stances);
	}
	return (sg_rune_stance_validity_t)(attachment_stances &
		model->cells[source].valid_stances & model->cells[target].valid_stances &
		portal->valid_stances);
}

static void AppendArc(const sg_rune_compact_model_t *model,
	sg_rune_compact_field_arc_t *arcs, uint32_t *arc_count,
	uint32_t source, uint32_t target, uint32_t portal)
{
	const sg_rune_stance_validity_t stances = ArcStances(model, source, target,
		portal);

	if (stances == 0U)
		return;
	arcs[*arc_count].source = source;
	arcs[*arc_count].target = target;
	arcs[*arc_count].portal = portal;
	arcs[*arc_count].valid_stances = stances;
	(*arc_count)++;
}

static sg_rune_compact_field_status_t BuildArcs(
	sg_rune_compact_field_t *field)
{
	const sg_rune_compact_model_t *model = field->model;
	uint32_t *outgoing_counts = NULL;
	uint32_t *incoming_counts = NULL;
	uint32_t capacity;
	uint32_t portal_index;
	uint32_t index;

	if (model->portal_count > UINT32_MAX / 2U)
		return SG_RUNE_COMPACT_FIELD_ALLOCATION_FAILED;
	capacity = model->portal_count * 2U;
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
	for (portal_index = 0U; portal_index < model->portal_count; portal_index++) {
		const sg_rune_compact_portal_t *portal = &model->portals[portal_index];
		const uint32_t negative = model->incidences[
			portal->negative_incidence.value].cell.value;
		const uint32_t positive = model->incidences[
			portal->positive_incidence.value].cell.value;

		if (portal->direction == SG_RUNE_PORTAL_CONTINUITY_BOTH ||
			portal->direction ==
				SG_RUNE_PORTAL_CONTINUITY_NEGATIVE_TO_POSITIVE)
			AppendArc(model, field->arcs, &field->arc_count, negative,
				positive, portal_index);
		if (portal->direction == SG_RUNE_PORTAL_CONTINUITY_BOTH ||
			portal->direction ==
				SG_RUNE_PORTAL_CONTINUITY_POSITIVE_TO_NEGATIVE)
			AppendArc(model, field->arcs, &field->arc_count, positive,
				negative, portal_index);
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
	free(field);
}

static void SeedRank(const sg_rune_compact_model_t *model, uint32_t *ranks,
	uint32_t stance_index, uint32_t cell, uint32_t *queue,
	uint32_t *queue_count)
{
	const sg_rune_stance_validity_t stance = StanceBit(
		(sg_rune_compact_field_stance_t)stance_index);
	const uint32_t state = stance_index * model->cell_count + cell;
	uint32_t *rank = &ranks[state];

	if ((model->cells[cell].valid_stances & stance) == 0U || *rank == 0U)
		return;
	*rank = 0U;
	queue[*queue_count] = state;
	(*queue_count)++;
}

static sg_rune_compact_field_status_t ResolveAndSeed(
	const sg_rune_compact_field_t *field,
	const sg_rune_compact_destination_t *destination, uint32_t *ranks,
	uint32_t stance_index, uint32_t *queue, uint32_t *queue_count)
{
	const sg_rune_compact_model_t *model = field->model;
	uint32_t cell;

	switch (destination->kind) {
	case SG_RUNE_COMPACT_DESTINATION_POINT: {
		sg_rune_compact_location_t location;
		const sg_rune_compact_localize_status_t status = SG_RuneCompactLocalize(
			model, &destination->value.point, &location);

		if (status != SG_RUNE_COMPACT_LOCALIZE_OK)
			return SG_RUNE_COMPACT_FIELD_INVALID_DESTINATION;
		SeedRank(model, ranks, stance_index, location.cell.value, queue,
			queue_count);
		return SG_RUNE_COMPACT_FIELD_OK;
	}
	case SG_RUNE_COMPACT_DESTINATION_CELL:
		cell = destination->value.cell.value;
		if (cell >= model->cell_count)
			return SG_RUNE_COMPACT_FIELD_INVALID_DESTINATION;
		SeedRank(model, ranks, stance_index, cell, queue, queue_count);
		return SG_RUNE_COMPACT_FIELD_OK;
	case SG_RUNE_COMPACT_DESTINATION_SURFACE:
		if (destination->value.surface.value >= model->incidence_count)
			return SG_RUNE_COMPACT_FIELD_INVALID_DESTINATION;
		cell = model->incidences[destination->value.surface.value].cell.value;
		SeedRank(model, ranks, stance_index, cell, queue, queue_count);
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
			SeedRank(model, ranks, stance_index, cell, queue, queue_count);
		}
		return SG_RUNE_COMPACT_FIELD_OK;
	}
	case SG_RUNE_COMPACT_DESTINATION_KIND_COUNT:
	default:
		return SG_RUNE_COMPACT_FIELD_INVALID_DESTINATION;
	}
}

static void BuildRanks(const sg_rune_compact_field_t *field, uint32_t *ranks,
	uint32_t *queue, uint32_t queue_count)
{
	const uint32_t cell_count = field->model->cell_count;
	uint32_t head = 0U;

	while (head < queue_count) {
		const uint32_t target_state = queue[head++];
		const uint32_t stance_index = target_state / cell_count;
		const uint32_t target = target_state % cell_count;
		const uint32_t target_rank = ranks[target_state];
		const sg_rune_stance_validity_t stance = StanceBit(
			(sg_rune_compact_field_stance_t)stance_index);
		uint32_t offset;

		for (offset = field->incoming_offsets[target];
			offset < field->incoming_offsets[target + 1U]; offset++) {
			const sg_rune_compact_field_arc_t *arc = &field->arcs[
				field->incoming_arcs[offset]];
			uint32_t *source_rank =
				&ranks[stance_index * cell_count + arc->source];

			if ((arc->valid_stances & stance) == 0U ||
				*source_rank != UINT32_MAX)
				continue;
			*source_rank = target_rank + 1U;
			queue[queue_count++] = stance_index * cell_count + arc->source;
		}
		{
			const uint32_t other_stance = stance_index == 0U ? 1U : 0U;
			const sg_rune_stance_validity_t other_bit = StanceBit(
				(sg_rune_compact_field_stance_t)other_stance);
			const uint32_t other_state = other_stance * cell_count + target;

			if ((field->model->cells[target].valid_stances & stance) != 0U &&
				(field->model->cells[target].valid_stances & other_bit) != 0U &&
				ranks[other_state] == UINT32_MAX) {
				ranks[other_state] = target_rank + 1U;
				queue[queue_count++] = other_state;
			}
		}
	}
}

sg_rune_compact_field_status_t SG_RuneCompactFieldPlanCreate(
	const sg_rune_compact_field_t *field,
	const sg_rune_compact_destination_t *destination,
	sg_rune_compact_destination_plan_t **plan_out)
{
	sg_rune_compact_destination_plan_t *plan;
	uint32_t *queue;
	uint32_t rank_count;
	uint32_t index;
	uint32_t stance_index;
	uint32_t queue_count = 0U;

	if (field == NULL || destination == NULL || plan_out == NULL)
		return SG_RUNE_COMPACT_FIELD_INVALID_ARGUMENT;
	if (field->model->cell_count > UINT32_MAX /
		(uint32_t)SG_RUNE_COMPACT_FIELD_STANCE_COUNT)
		return SG_RUNE_COMPACT_FIELD_ALLOCATION_FAILED;
	rank_count = field->model->cell_count *
		(uint32_t)SG_RUNE_COMPACT_FIELD_STANCE_COUNT;
	plan = calloc(1U, sizeof(*plan));
	queue = AllocateArray(rank_count, sizeof(*queue));
	if (plan == NULL || queue == NULL) {
		free(plan);
		free(queue);
		return SG_RUNE_COMPACT_FIELD_ALLOCATION_FAILED;
	}
	plan->ranks = AllocateArray(rank_count, sizeof(*plan->ranks));
	if (plan->ranks == NULL) {
		free(queue);
		free(plan);
		return SG_RUNE_COMPACT_FIELD_ALLOCATION_FAILED;
	}
	for (index = 0U; index < rank_count; index++)
		plan->ranks[index] = UINT32_MAX;
	plan->field = field;
	plan->destination = *destination;
	for (stance_index = 0U;
		stance_index < (uint32_t)SG_RUNE_COMPACT_FIELD_STANCE_COUNT;
		stance_index++) {
		const sg_rune_compact_field_status_t status = ResolveAndSeed(field,
			destination, plan->ranks, stance_index, queue, &queue_count);

		if (status != SG_RUNE_COMPACT_FIELD_OK) {
			free(queue);
			SG_RuneCompactFieldPlanDestroy(plan);
			return status;
		}
	}
	BuildRanks(field, plan->ranks, queue, queue_count);
	free(queue);
	*plan_out = plan;
	return SG_RUNE_COMPACT_FIELD_OK;
}

void SG_RuneCompactFieldPlanDestroy(
	sg_rune_compact_destination_plan_t *plan)
{
	if (plan == NULL)
		return;
	free(plan->ranks);
	free(plan);
}

static int ContextValid(const sg_rune_compact_field_local_context_t *context)
{
	uint32_t axis;

	if (context->stance < SG_RUNE_COMPACT_FIELD_STANDING ||
		context->stance >= SG_RUNE_COMPACT_FIELD_STANCE_COUNT ||
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

static int MechanismSnapshotValid(const sg_rune_compact_field_t *field,
	const sg_rune_compact_field_mechanism_snapshot_t *snapshot)
{
	uint32_t index;

	if (snapshot == NULL)
		return 1;
	if (snapshot->model_identity == NULL ||
		!SG_RuneCompactIdentityMatches(snapshot->model_identity,
			&field->expected_identity) ||
		(snapshot->phase_count != 0U && snapshot->phases == NULL))
		return 0;
	for (index = 0U; index < snapshot->phase_count; index++) {
		const sg_rune_compact_field_mechanism_phase_t *phase =
			&snapshot->phases[index];

		if (phase->mechanism.value >=
				field->model->static_data->mechanism_count ||
			!isfinite(phase->phase) ||
			(index != 0U && snapshot->phases[index - 1U].mechanism.value >=
				phase->mechanism.value))
			return 0;
	}
	return 1;
}

static int AttachmentUsesMoverPhase(const sg_rune_compact_model_t *model,
	const sg_rune_movement_field_attachment_t *attachment)
{
	uint32_t reference_offset;

	for (reference_offset = 0U;
		reference_offset < attachment->functions.count; reference_offset++) {
		const uint32_t reference = attachment->functions.first +
			reference_offset;
		const sg_rune_analytic_function_t *function =
			&model->analytic->functions[
				model->analytic_function_refs[reference].value];
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

static sg_rune_compact_field_status_t AttachmentMechanismPhase(
	const sg_rune_compact_field_t *field,
	const sg_rune_movement_field_attachment_t *attachment,
	const sg_rune_compact_field_mechanism_snapshot_t *snapshot,
	sg_rune_compact_mechanism_index_t *mechanism_out, float *phase_out)
{
	const sg_rune_compact_static_t *static_data = field->model->static_data;
	uint32_t binding_count = 0U;
	uint32_t mechanism = SG_RUNE_COMPACT_INDEX_NONE;
	uint32_t index;

	mechanism_out->value = SG_RUNE_COMPACT_INDEX_NONE;
	*phase_out = 0.0f;
	if (!AttachmentUsesMoverPhase(field->model, attachment))
		return SG_RUNE_COMPACT_FIELD_OK;
	if (attachment->boundary_portal.value == SG_RUNE_COMPACT_INDEX_NONE)
		return SG_RUNE_COMPACT_FIELD_MECHANISM_PHASE_REQUIRED;
	for (index = 0U; index < static_data->portal_mechanism_count; index++) {
		const sg_rune_compact_portal_mechanism_t *binding =
			&static_data->portal_mechanisms[index];

		if (binding->portal.value == attachment->boundary_portal.value) {
			binding_count++;
			mechanism = binding->mechanism.value;
		}
	}
	if (binding_count != 1U || snapshot == NULL)
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

static sg_rune_compact_field_status_t EvaluateAttachment(
	const sg_rune_compact_model_t *model,
	const sg_rune_movement_field_attachment_t *attachment,
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

	for (offset = 0U; offset < attachment->functions.count; offset++) {
		const uint32_t reference = attachment->functions.first + offset;
		sg_rune_compact_eval_query_t query;
		sg_rune_compact_eval_result_t result;

		query.function = model->analytic_function_refs[reference];
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

static int CandidateEarlier(float cost, const sg_rune_compact_field_arc_t *arc,
	uint32_t field_index, const sg_rune_compact_field_portal_step_t *best)
{
	if (cost != best->local_cost)
		return cost < best->local_cost;
	if (arc->portal != best->next_portal.value)
		return arc->portal < best->next_portal.value;
	if (arc->target != best->next_cell.value)
		return arc->target < best->next_cell.value;
	return field_index < best->movement_field;
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
	uint32_t source_rank;
	uint32_t arc_offset;
	int found = 0;

	if (plan == NULL || context == NULL || result_out == NULL)
		return SG_RUNE_COMPACT_FIELD_INVALID_ARGUMENT;
	if (!ContextValid(context))
		return SG_RUNE_COMPACT_FIELD_INVALID_CONTEXT;
	field = plan->field;
	model = field->model;
	if (!MechanismSnapshotValid(field, context->mechanisms))
		return SG_RUNE_COMPACT_FIELD_INVALID_MECHANISM_SNAPSHOT;
	if (SG_RuneCompactLocalize(model, &context->origin, &location) !=
		SG_RUNE_COMPACT_LOCALIZE_OK)
		return SG_RUNE_COMPACT_FIELD_LOCALIZATION_FAILED;
	stance_index = (uint32_t)context->stance;
	stance = StanceBit(context->stance);
	if ((location.valid_stances & stance) == 0U)
		return SG_RUNE_COMPACT_FIELD_INVALID_CONTEXT;
	result.current_cell = location.cell;
	source_rank = plan->ranks[stance_index * model->cell_count +
		location.cell.value];
	if (source_rank == UINT32_MAX) {
		result.kind = SG_RUNE_COMPACT_FIELD_DISCONNECTED;
		*result_out = result;
		return SG_RUNE_COMPACT_FIELD_OK;
	}
	if (source_rank == 0U) {
		result.kind = plan->destination.kind == SG_RUNE_COMPACT_DESTINATION_CELL ?
			SG_RUNE_COMPACT_FIELD_CELL_DESTINATION :
			SG_RUNE_COMPACT_FIELD_LOCAL_DESTINATION;
		result.value.destination = plan->destination;
		*result_out = result;
		return SG_RUNE_COMPACT_FIELD_OK;
	}
	{
		const uint32_t other_stance = stance_index == 0U ? 1U : 0U;
		const uint32_t other_rank = plan->ranks[
			other_stance * model->cell_count + location.cell.value];

		if (other_rank < source_rank) {
			result.kind = SG_RUNE_COMPACT_FIELD_STEP;
			result.value.step.kind = SG_RUNE_COMPACT_FIELD_TRANSITION_STANCE;
			result.value.step.source_rank = source_rank;
			result.value.step.target_rank = other_rank;
			result.value.step.target_stance =
				(sg_rune_compact_field_stance_t)other_stance;
			*result_out = result;
			return SG_RUNE_COMPACT_FIELD_OK;
		}
	}
	result.kind = SG_RUNE_COMPACT_FIELD_BLOCKED_NOW;
	result.value.step.kind = SG_RUNE_COMPACT_FIELD_TRANSITION_PORTAL;
	result.value.step.target_stance = context->stance;
	result.value.step.value.portal.local_cost = INFINITY;
	result.value.step.value.portal.next_cell.value =
		SG_RUNE_COMPACT_INDEX_NONE;
	result.value.step.value.portal.next_portal.value =
		SG_RUNE_COMPACT_INDEX_NONE;
	result.value.step.value.portal.mechanism.value =
		SG_RUNE_COMPACT_INDEX_NONE;
	result.value.step.value.portal.movement_field =
		SG_RUNE_COMPACT_INDEX_NONE;
	for (arc_offset = field->outgoing_offsets[location.cell.value];
		arc_offset < field->outgoing_offsets[location.cell.value + 1U];
		arc_offset++) {
		const sg_rune_compact_field_arc_t *arc = &field->arcs[
			field->outgoing_arcs[arc_offset]];
		const uint32_t target_rank = plan->ranks[
			stance_index * model->cell_count + arc->target];
		const sg_rune_compact_cell_t *cell = &model->cells[arc->source];
		uint32_t field_index;

		if ((arc->valid_stances & stance) == 0U ||
			target_rank >= source_rank)
			continue;
		for (field_index = cell->movement_fields.first;
			field_index < cell->movement_fields.first +
				cell->movement_fields.count; field_index++) {
			const sg_rune_movement_field_attachment_t *attachment =
				&model->movement_fields[field_index];
			float cost;
			float travel_time;
			float mover_phase;
			int reachable;
			sg_rune_compact_mechanism_index_t mechanism;
			sg_rune_compact_field_status_t status;

			if (!FieldApplies(attachment, arc->source, arc->portal) ||
				(attachment->valid_stances & stance) == 0U)
				continue;
			status = AttachmentMechanismPhase(field, attachment,
				context->mechanisms, &mechanism, &mover_phase);
			if (status != SG_RUNE_COMPACT_FIELD_OK)
				return status;
			BuildInputs(context, mover_phase, inputs);
			status = EvaluateAttachment(model, attachment, inputs, &cost,
				&travel_time, &reachable);
			if (status != SG_RUNE_COMPACT_FIELD_OK)
				return status;
			if (!reachable || (found && !CandidateEarlier(cost, arc,
				field_index, &result.value.step.value.portal)))
				continue;
			found = 1;
			result.kind = SG_RUNE_COMPACT_FIELD_STEP;
			result.value.step.kind = SG_RUNE_COMPACT_FIELD_TRANSITION_PORTAL;
			result.value.step.source_rank = source_rank;
			result.value.step.target_rank = target_rank;
			result.value.step.target_stance = context->stance;
			result.value.step.value.portal.local_cost = cost;
			result.value.step.value.portal.travel_time_seconds = travel_time;
			result.value.step.value.portal.next_cell.value = arc->target;
			result.value.step.value.portal.next_portal.value = arc->portal;
			result.value.step.value.portal.mechanism = mechanism;
			result.value.step.value.portal.movement_field = field_index;
		}
	}
	*result_out = result;
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
		"mechanism phase required",
		"analytic evaluation failed",
		"invalid transition value",
		"allocation failed"
	};

	return (uint32_t)status < (uint32_t)SG_RUNE_COMPACT_FIELD_STATUS_COUNT ?
		names[status] : "unknown compact field status";
}
