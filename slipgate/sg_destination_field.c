/* Directional phase-space destination fields for immutable RUNE v2 models. */

#include "sg_destination_field.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define SG_FIELD_NO_INDEX UINT32_MAX

typedef struct sg_field_work_s
{
	uint32_t *first_incoming;
	uint32_t *next_incoming;
	uint32_t *source_phase;
	uint32_t *edge_cost;
	uint32_t *edge_record;
	sg_rune_phase_transition_kind_t *edge_transition_kind;
	uint8_t *edge_is_transition;
	uint32_t *queue;
	uint32_t *selected_edge;
	uint8_t *queued;
	uint8_t *transition_claimed;
} sg_field_work_t;

static int CoordinateEqual(const sg_phase_coordinate_t *left,
	const sg_phase_coordinate_t *right)
{
	return left->phase_id == right->phase_id && left->cell_id == right->cell_id;
}

static int PoseEqual(const sg_destination_pose_t *left,
	const sg_destination_pose_t *right)
{
	uint32_t axis;

	if (!CoordinateEqual(&left->phase, &right->phase) ||
		left->sample_time_ms != right->sample_time_ms ||
		left->region_id != right->region_id)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (left->position[axis] != right->position[axis] ||
			left->velocity[axis] != right->velocity[axis])
			return 0;
	return 1;
}

static int TerminalPoseExactAtFieldTime(
	const sg_destination_field_t *field,
	const sg_destination_pose_t *source)
{
	sg_destination_pose_t destination = field->destination.pose;

	if (field->destination.motion == SG_DESTINATION_STATIC)
		destination.sample_time_ms = source->sample_time_ms;
	else if (field->computed_at_ms != destination.sample_time_ms)
		return 0;
	return PoseEqual(&destination, source);
}

static int SnapshotShapeCurrent(const sg_rune_runtime_snapshot_t *snapshot)
{
	return snapshot && snapshot->identity != 0U &&
		snapshot->topology_revision != 0U && snapshot->cell_count != 0U &&
		snapshot->phase_count != 0U && snapshot->model && snapshot->phases &&
		snapshot->model->version == SG_RUNE_MODEL_VERSION &&
		snapshot->model->schema_tag == SG_RUNE_MODEL_SCHEMA_TAG &&
		(snapshot->model->flags & (SG_RUNE_MODEL_IMMUTABLE |
		 SG_RUNE_MODEL_EXACT_BOUND | SG_RUNE_MODEL_NO_RUNTIME_ACTORS)) ==
		(SG_RUNE_MODEL_IMMUTABLE | SG_RUNE_MODEL_EXACT_BOUND |
		 SG_RUNE_MODEL_NO_RUNTIME_ACTORS) &&
		snapshot->model->completeness.state == SG_RUNE_COMPLETENESS_COMPLETE &&
		snapshot->model->cell_count == snapshot->cell_count &&
		snapshot->model->phase_count == snapshot->phase_count &&
		snapshot->model->cells && snapshot->model->phases;
}

static int FieldShapeCurrent(const sg_rune_runtime_snapshot_t *snapshot,
	const sg_destination_field_t *field)
{
	return SnapshotShapeCurrent(snapshot) && field &&
		SG_DestinationHandleValid(&field->destination) &&
		SG_PhaseCoordinateValid(snapshot, &field->destination.pose.phase) &&
		field->rune_identity == snapshot->identity &&
		field->topology_revision == snapshot->topology_revision &&
		field->generation == field->destination.generation &&
		field->computed_at_ms != 0U &&
		(field->destination.motion != SG_DESTINATION_MOVING ||
		 field->computed_at_ms >= field->destination.pose.sample_time_ms) &&
		field->complete == 1U && field->sample_count == snapshot->phase_count &&
		field->samples;
}

int SG_FieldNeedsUpdate(const sg_rune_runtime_snapshot_t *snapshot,
	const sg_destination_field_t *field,
	const sg_destination_handle_t *destination)
{
	if (!FieldShapeCurrent(snapshot, field) ||
		!SG_DestinationHandleValid(destination) ||
		!SG_PhaseCoordinateValid(snapshot, &destination->pose.phase) ||
		!SG_DestinationSameTarget(&field->destination, destination) ||
		field->destination.motion != destination->motion ||
		field->destination.generation != destination->generation ||
		!PoseEqual(&field->destination.pose, &destination->pose))
		return 1;
	return 0;
}

int SG_FieldCanReuseStatic(const sg_rune_runtime_snapshot_t *snapshot,
	const sg_destination_field_t *field,
	const sg_destination_handle_t *destination)
{
	return FieldShapeCurrent(snapshot, field) &&
		SG_DestinationHandleValid(destination) &&
		SG_PhaseCoordinateValid(snapshot, &destination->pose.phase) &&
		field->destination.motion == SG_DESTINATION_STATIC &&
		destination->motion == SG_DESTINATION_STATIC &&
		SG_DestinationSameTarget(&field->destination, destination) &&
		PoseEqual(&field->destination.pose, &destination->pose);
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

static int StableIdNone(const sg_rune_stable_id_t *id)
{
	return id->source_set_identity == UINT64_MAX && id->high == UINT64_MAX &&
		id->low == UINT64_MAX;
}

static int FindPhase(const sg_rune_model_t *model,
	const sg_rune_phase_ref_t *reference, uint32_t *index_out)
{
	uint32_t low = 0U;
	uint32_t high = model->phase_count;

	while (low < high) {
		uint32_t middle = low + (high - low) / 2U;
		int comparison = StableIdCompare(&model->phases[middle].id.value,
			&reference->value);

		if (comparison < 0)
			low = middle + 1U;
		else
			high = middle;
	}
	if (low >= model->phase_count ||
		StableIdCompare(&model->phases[low].id.value, &reference->value) != 0)
		return 0;
	*index_out = low;
	return 1;
}

static int FindCell(const sg_rune_model_t *model,
	const sg_rune_cell_ref_t *reference, uint32_t *index_out)
{
	uint32_t low = 0U;
	uint32_t high = model->cell_count;

	while (low < high) {
		uint32_t middle = low + (high - low) / 2U;
		int comparison = StableIdCompare(&model->cells[middle].id.value,
			&reference->value);

		if (comparison < 0)
			low = middle + 1U;
		else
			high = middle;
	}
	if (low >= model->cell_count ||
		StableIdCompare(&model->cells[low].id.value, &reference->value) != 0)
		return 0;
	*index_out = low;
	return 1;
}

static int FindTransition(const sg_rune_model_t *model,
	const sg_rune_phase_transition_ref_t *reference, uint32_t *index_out)
{
	uint32_t low = 0U;
	uint32_t high = model->phase_transition_count;

	while (low < high) {
		uint32_t middle = low + (high - low) / 2U;
		int comparison = StableIdCompare(
			&model->phase_transitions[middle].id.value, &reference->value);

		if (comparison < 0)
			low = middle + 1U;
		else
			high = middle;
	}
	if (low >= model->phase_transition_count ||
		StableIdCompare(&model->phase_transitions[low].id.value,
			&reference->value) != 0)
		return 0;
	*index_out = low;
	return 1;
}

static int IntervalValid(const sg_rune_interval_t *interval)
{
	return isfinite(interval->min_value) && isfinite(interval->max_value) &&
		interval->min_value <= interval->max_value;
}

static int Interval3Valid(const sg_rune_interval3_t *interval)
{
	return IntervalValid(&interval->x) && IntervalValid(&interval->y) &&
		IntervalValid(&interval->z);
}

static int OrderKeyCompare(const sg_rune_order_key_t *left,
	const sg_rune_order_key_t *right)
{
	if (left->source_set_identity != right->source_set_identity)
		return left->source_set_identity < right->source_set_identity ? -1 : 1;
	if (left->domain != right->domain)
		return left->domain < right->domain ? -1 : 1;
	if (left->source_index != right->source_index)
		return left->source_index < right->source_index ? -1 : 1;
	if (left->local_ordinal != right->local_ordinal)
		return left->local_ordinal < right->local_ordinal ? -1 : 1;
	if (left->variant != right->variant)
		return left->variant < right->variant ? -1 : 1;
	return 0;
}

static int SnapshotMatchesModel(const sg_rune_runtime_snapshot_t *snapshot)
{
	const sg_rune_model_t *model = snapshot->model;
	uint32_t cell_index;
	uint32_t phase_index;

	for (phase_index = 1U; phase_index < model->phase_count; phase_index++)
		if (StableIdCompare(&model->phases[phase_index - 1U].id.value,
			&model->phases[phase_index].id.value) >= 0)
			return 0;
	for (phase_index = 0U; phase_index < model->phase_count; phase_index++)
		if (!Interval3Valid(&model->phases[phase_index].velocity))
			return 0;
	for (cell_index = 1U; cell_index < model->cell_count; cell_index++)
		if (StableIdCompare(&model->cells[cell_index - 1U].id.value,
			&model->cells[cell_index].id.value) >= 0)
			return 0;
	for (cell_index = 0U; cell_index < model->cell_count; cell_index++) {
		const sg_rune_phase_span_t span = model->cells[cell_index].phases;

		if (span.first > model->phase_count ||
			span.count > model->phase_count - span.first)
			return 0;
		for (phase_index = span.first; phase_index < span.first + span.count;
			phase_index++)
			if (snapshot->phases[phase_index].cell_id != cell_index)
				return 0;
	}
	return 1;
}

static int AllocateWork(uint32_t phase_count, uint32_t edge_capacity,
	uint32_t transition_count, sg_field_work_t *work)
{
	memset(work, 0, sizeof(*work));
	work->first_incoming = malloc((size_t)phase_count * sizeof(uint32_t));
	work->queue = malloc((size_t)phase_count * sizeof(uint32_t));
	work->selected_edge = malloc((size_t)phase_count * sizeof(uint32_t));
	work->queued = calloc((size_t)phase_count, sizeof(uint8_t));
	if (edge_capacity != 0U) {
		work->next_incoming = malloc((size_t)edge_capacity * sizeof(uint32_t));
		work->source_phase = malloc((size_t)edge_capacity * sizeof(uint32_t));
		work->edge_cost = malloc((size_t)edge_capacity * sizeof(uint32_t));
		work->edge_record = malloc((size_t)edge_capacity * sizeof(uint32_t));
		work->edge_transition_kind = malloc((size_t)edge_capacity *
			sizeof(sg_rune_phase_transition_kind_t));
		work->edge_is_transition = malloc((size_t)edge_capacity *
			sizeof(uint8_t));
	}
	if (transition_count != 0U)
		work->transition_claimed = calloc((size_t)transition_count,
			sizeof(uint8_t));
	return work->first_incoming && work->queue && work->selected_edge &&
		work->queued && (edge_capacity == 0U ||
		(work->next_incoming && work->source_phase && work->edge_cost &&
		 work->edge_record && work->edge_transition_kind &&
		 work->edge_is_transition)) &&
		(transition_count == 0U || work->transition_claimed);
}

static void FreeWork(sg_field_work_t *work)
{
	free(work->first_incoming);
	free(work->next_incoming);
	free(work->source_phase);
	free(work->edge_cost);
	free(work->edge_record);
	free(work->edge_transition_kind);
	free(work->edge_is_transition);
	free(work->queue);
	free(work->selected_edge);
	free(work->queued);
	free(work->transition_claimed);
	memset(work, 0, sizeof(*work));
}

static int KernelCost(const sg_rune_capability_kernel_t *kernel,
	uint32_t *cost_out)
{
	double duration;
	uint64_t total;

	if (kernel->family < SG_RUNE_CAPABILITY_CONTINUOUS_SUPPORT ||
		kernel->family >= SG_RUNE_CAPABILITY_FAMILY_COUNT ||
		kernel->cost_law < SG_RUNE_COST_CONSTANT_RATE ||
		kernel->cost_law >= SG_RUNE_COST_LAW_COUNT ||
		(kernel->flags & (SG_RUNE_KERNEL_DIRECTIONAL |
		 SG_RUNE_KERNEL_PHASE_AWARE | SG_RUNE_KERNEL_PROVEN)) !=
		(SG_RUNE_KERNEL_DIRECTIONAL | SG_RUNE_KERNEL_PHASE_AWARE |
		 SG_RUNE_KERNEL_PROVEN) ||
		!Interval3Valid(&kernel->parameters.displacement) ||
		!IntervalValid(&kernel->parameters.duration_ms) ||
		kernel->parameters.duration_ms.max_value <= 0.0f)
		return 0;
	/* Runtime fields use the proven upper time bound. Fractional milliseconds
	 * round upward so the gradient never advertises an optimistic traversal. */
	duration = ceil((double)kernel->parameters.duration_ms.max_value);
	if (duration <= 0.0 || duration >= (double)SG_DESTINATION_FIELD_INF)
		return 0;
	total = (uint64_t)duration + kernel->parameters.fixed_latency_ms +
		kernel->parameters.dwell_ms;
	if (total >= SG_DESTINATION_FIELD_INF)
		return 0;
	*cost_out = (uint32_t)total;
	return 1;
}

static int TransitionCost(const sg_rune_phase_transition_t *transition,
	uint32_t *cost_out)
{
	double duration;

	if (!IntervalValid(&transition->duration_ms) ||
		transition->duration_ms.max_value <= 0.0f)
		return 0;
	duration = ceil((double)transition->duration_ms.max_value);
	if (duration <= 0.0 || duration >= (double)SG_DESTINATION_FIELD_INF)
		return 0;
	*cost_out = (uint32_t)duration;
	return 1;
}

static const sg_rune_order_key_t *EdgeOrder(const sg_rune_model_t *model,
	uint32_t record, uint8_t is_transition)
{
	if (is_transition)
		return &model->phase_transitions[record].order;
	return &model->kernels[record].order;
}

static int EdgeRecordCompare(const sg_rune_model_t *model,
	const sg_field_work_t *work, uint32_t left, uint32_t right)
{
	return OrderKeyCompare(EdgeOrder(model, work->edge_record[left],
		work->edge_is_transition[left]),
		EdgeOrder(model, work->edge_record[right],
		work->edge_is_transition[right]));
}

static void AddReverseEdge(sg_field_work_t *work, uint32_t edge,
	uint32_t record, uint32_t source, uint32_t destination, uint32_t cost,
	sg_rune_phase_transition_kind_t transition_kind, uint8_t is_transition)
{
	work->edge_record[edge] = record;
	work->source_phase[edge] = source;
	work->edge_cost[edge] = cost;
	work->edge_transition_kind[edge] = transition_kind;
	work->edge_is_transition[edge] = is_transition;
	work->next_incoming[edge] = work->first_incoming[destination];
	work->first_incoming[destination] = edge;
}

static int BuildReverseEdges(const sg_rune_model_t *model,
	sg_field_work_t *work)
{
	uint32_t edge_count = 0U;
	uint32_t index;

	for (index = 0U; index < model->phase_count; index++)
		work->first_incoming[index] = SG_FIELD_NO_INDEX;
	for (index = 0U; index < model->kernel_count; index++) {
		const sg_rune_capability_kernel_t *kernel = &model->kernels[index];
		uint32_t transition_index;

		if (index != 0U && OrderKeyCompare(&model->kernels[index - 1U].order,
			&kernel->order) >= 0)
			return 0;
		if (!StableIdNone(&kernel->transition.value)) {
			if (!FindTransition(model, &kernel->transition, &transition_index))
				return 0;
			work->transition_claimed[transition_index] = 1U;
		}
	}
	for (index = 0U; index < model->phase_transition_count; index++) {
		const sg_rune_phase_transition_t *transition =
			&model->phase_transitions[index];
		uint32_t source;
		uint32_t destination;
		uint32_t cell;
		uint32_t cost;

		if ((index != 0U && OrderKeyCompare(
			&model->phase_transitions[index - 1U].order,
			&transition->order) >= 0) ||
			!FindPhase(model, &transition->source_phase, &source) ||
			!FindPhase(model, &transition->destination_phase, &destination) ||
			!FindCell(model, &transition->cell, &cell) ||
			source < model->cells[cell].phases.first ||
			source >= model->cells[cell].phases.first +
				model->cells[cell].phases.count ||
			destination < model->cells[cell].phases.first ||
			destination >= model->cells[cell].phases.first +
				model->cells[cell].phases.count ||
			!TransitionCost(transition, &cost))
			return 0;
		if (!work->transition_claimed[index]) {
			AddReverseEdge(work, edge_count,
				index, source, destination, cost, transition->kind, 1U);
			edge_count++;
		}
	}
	for (index = 0U; index < model->kernel_count; index++) {
		const sg_rune_capability_kernel_t *kernel = &model->kernels[index];
		uint32_t source;
		uint32_t destination;
		uint32_t source_cell;
		uint32_t destination_cell;
		uint32_t cost;
		sg_rune_phase_transition_kind_t transition_kind =
			SG_RUNE_PHASE_TRANSITION_NONE;

		if (!FindPhase(model, &kernel->source_phase, &source) ||
			!FindPhase(model, &kernel->destination_phase, &destination) ||
			!FindCell(model, &kernel->source_cell, &source_cell) ||
			!FindCell(model, &kernel->destination_cell, &destination_cell) ||
			source < model->cells[source_cell].phases.first ||
			source >= model->cells[source_cell].phases.first +
				model->cells[source_cell].phases.count ||
			destination < model->cells[destination_cell].phases.first ||
			destination >= model->cells[destination_cell].phases.first +
				model->cells[destination_cell].phases.count ||
			!KernelCost(kernel, &cost))
			return 0;
		if (!StableIdNone(&kernel->transition.value)) {
			uint32_t transition_index;

			if (!FindTransition(model, &kernel->transition, &transition_index))
				return 0;
			transition_kind = model->phase_transitions[transition_index].kind;
		}
		AddReverseEdge(work, edge_count, index, source, destination, cost,
			transition_kind, 0U);
		edge_count++;
	}
	return 1;
}

static void NormalizeMidpoint(const sg_rune_interval3_t *interval,
	float out[3])
{
	double value[3];
	double length;

	value[0] = ((double)interval->x.min_value + interval->x.max_value) * 0.5;
	value[1] = ((double)interval->y.min_value + interval->y.max_value) * 0.5;
	value[2] = ((double)interval->z.min_value + interval->z.max_value) * 0.5;
	length = sqrt(value[0] * value[0] + value[1] * value[1] +
		value[2] * value[2]);
	if (!isfinite(length) || length <= 0.0) {
		out[0] = 0.0f;
		out[1] = 0.0f;
		out[2] = 0.0f;
		return;
	}
	out[0] = (float)(value[0] / length);
	out[1] = (float)(value[1] / length);
	out[2] = (float)(value[2] / length);
}

static double NormalizeDelta(const float destination[3], const float source[3],
	float out[3])
{
	double delta[3];
	double length;

	delta[0] = (double)destination[0] - source[0];
	delta[1] = (double)destination[1] - source[1];
	delta[2] = (double)destination[2] - source[2];
	length = sqrt(delta[0] * delta[0] + delta[1] * delta[1] +
		delta[2] * delta[2]);
	if (!isfinite(length) || length <= 0.0) {
		out[0] = 0.0f;
		out[1] = 0.0f;
		out[2] = 0.0f;
		return length;
	}
	out[0] = (float)(delta[0] / length);
	out[1] = (float)(delta[1] / length);
	out[2] = (float)(delta[2] / length);
	return length;
}

static void ApplyTerminalPose(const sg_destination_field_t *field,
	const sg_destination_pose_t *source, sg_field_query_result_t *result)
{
	(void)NormalizeDelta(field->destination.pose.position, source->position,
		result->sample.direction);
	(void)NormalizeDelta(field->destination.pose.velocity, source->velocity,
		result->sample.velocity_direction);
	result->sample.next_phase = field->destination.pose.phase;
	if (TerminalPoseExactAtFieldTime(field, source)) {
		result->terminal_residual.status = SG_FIELD_TERMINAL_RESIDUAL_EXACT;
		result->terminal_residual.upper_ms = 0U;
	}
}

int SG_FieldQuery(const sg_rune_runtime_snapshot_t *snapshot,
	const sg_destination_field_t *field, const sg_destination_pose_t *source,
	sg_field_query_result_t *out)
{
	uint32_t index;

	if (out)
		memset(out, 0, sizeof(*out));
	if (!out || !FieldShapeCurrent(snapshot, field) ||
		!SG_DestinationPoseValid(source) ||
		!SG_PhaseCoordinateValid(snapshot, &source->phase))
		return 0;
	index = source->phase.phase_id;
	if (!CoordinateEqual(&field->samples[index].phase,
		&snapshot->phases[index]) ||
		!SG_FieldSampleShapeValid(snapshot, &field->samples[index]))
		return 0;
	out->sample = field->samples[index];
	out->terminal_residual.status = out->sample.finite ?
		SG_FIELD_TERMINAL_RESIDUAL_UNKNOWN :
		SG_FIELD_TERMINAL_RESIDUAL_NOT_APPLICABLE;
	out->terminal_residual.upper_ms = SG_DESTINATION_FIELD_INF;
	if (out->sample.finite &&
		CoordinateEqual(&source->phase, &field->destination.pose.phase))
		ApplyTerminalPose(field, source, out);
	return 1;
}

static int InitializeSamples(const sg_rune_runtime_snapshot_t *snapshot,
	const sg_destination_handle_t *destination, sg_field_sample_t *samples,
	sg_field_work_t *work, const uint8_t *affected_phases)
{
	uint32_t index;
	uint32_t destination_index = destination->pose.phase.phase_id;

	if (affected_phases)
		for (index = 0U; index < snapshot->phase_count; index++)
			if (affected_phases[index] == 0U &&
				(!SG_FieldSampleShapeValid(snapshot, &samples[index]) ||
				 !CoordinateEqual(&samples[index].phase,
					&snapshot->phases[index])))
				return 0;
	for (index = 0U; index < snapshot->phase_count; index++) {
		if (affected_phases && affected_phases[index] == 0U) {
			work->selected_edge[index] = SG_FIELD_NO_INDEX;
			continue;
		}
		memset(&samples[index], 0, sizeof(samples[index]));
		samples[index].phase = snapshot->phases[index];
		samples[index].next_phase.phase_id = SG_DESTINATION_FIELD_NO_PHASE;
		samples[index].next_phase.cell_id = SG_DESTINATION_FIELD_NO_CELL;
		samples[index].cost_ms = SG_DESTINATION_FIELD_INF;
		work->selected_edge[index] = SG_FIELD_NO_INDEX;
	}
	samples[destination_index].next_phase = destination->pose.phase;
	samples[destination_index].cost_ms = 0U;
	samples[destination_index].finite = 1U;
	return 1;
}

static void SelectEdge(const sg_rune_runtime_snapshot_t *snapshot,
	const sg_field_work_t *work, uint32_t source, uint32_t destination,
	uint32_t edge,
	uint32_t cost, sg_field_sample_t *samples)
{
	uint32_t record = work->edge_record[edge];

	samples[source].next_phase = snapshot->phases[destination];
	samples[source].cost_ms = cost;
	samples[source].capability_families.bits = 0U;
	samples[source].phase_transition_kind =
		work->edge_transition_kind[edge];
	if (work->edge_is_transition[edge]) {
		samples[source].direction[0] = 0.0f;
		samples[source].direction[1] = 0.0f;
		samples[source].direction[2] = 0.0f;
	} else {
		const sg_rune_capability_kernel_t *kernel =
			&snapshot->model->kernels[record];

		samples[source].capability_families =
			SG_FIELD_CAPABILITY_FAMILY_BIT(kernel->family);
		NormalizeMidpoint(&kernel->parameters.displacement,
			samples[source].direction);
	}
	NormalizeMidpoint(&snapshot->model->phases[destination].velocity,
		samples[source].velocity_direction);
	samples[source].finite = 1U;
}

static void SolveFixedPoint(const sg_rune_runtime_snapshot_t *snapshot,
	sg_field_sample_t *samples, sg_field_work_t *work, uint32_t destination,
	const uint8_t *affected_phases)
{
	uint32_t head = 0U;
	uint32_t tail = 0U;
	uint32_t queued_count = 0U;

	work->queue[tail] = destination;
	tail = (tail + 1U) % snapshot->phase_count;
	queued_count++;
	work->queued[destination] = 1U;
	/* Positive edge costs make repeated reverse relaxation converge on the
	 * least phase value. Exhaust the worklist; no scheduling budget ends it. */
	while (queued_count != 0U) {
		uint32_t current = work->queue[head];
		uint32_t edge;

		head = (head + 1U) % snapshot->phase_count;
		queued_count--;
		work->queued[current] = 0U;
		for (edge = work->first_incoming[current]; edge != SG_FIELD_NO_INDEX;
			edge = work->next_incoming[edge]) {
			uint32_t source = work->source_phase[edge];
			uint32_t candidate;
			int lower_cost;

			if ((affected_phases && affected_phases[source] == 0U) ||
				samples[current].cost_ms >= SG_DESTINATION_FIELD_INF -
				work->edge_cost[edge])
				continue;
			candidate = samples[current].cost_ms + work->edge_cost[edge];
			lower_cost = candidate < samples[source].cost_ms;
			if (!lower_cost &&
				(candidate != samples[source].cost_ms ||
				 (work->selected_edge[source] != SG_FIELD_NO_INDEX &&
				  EdgeRecordCompare(snapshot->model, work, edge,
					work->selected_edge[source]) >= 0)))
				continue;
			SelectEdge(snapshot, work, source, current, edge, candidate, samples);
			work->selected_edge[source] = edge;
			if (lower_cost && !work->queued[source]) {
				work->queue[tail] = source;
				tail = (tail + 1U) % snapshot->phase_count;
				queued_count++;
				work->queued[source] = 1U;
			}
		}
	}
}

static int SolveInternal(const sg_rune_runtime_snapshot_t *snapshot,
	const sg_destination_handle_t *destination, uint64_t computed_at_ms,
	const sg_destination_field_t *before_field,
	const uint8_t *affected_phases, uint32_t affected_phase_count,
	sg_field_sample_t *samples, uint32_t sample_capacity,
	sg_destination_field_t *out)
{
	sg_field_work_t work;
	const sg_rune_model_t *model;
	uint32_t edge_capacity;
	int success = 0;

	if (out)
		memset(out, 0, sizeof(*out));
	if (!out || !SG_RuneRuntimeSnapshotValid(snapshot) ||
		!SG_DestinationHandleValid(destination) ||
		!SG_PhaseCoordinateValid(snapshot, &destination->pose.phase) ||
		computed_at_ms == 0U ||
		(destination->motion == SG_DESTINATION_MOVING &&
		 computed_at_ms < destination->pose.sample_time_ms) || !samples ||
		sample_capacity < snapshot->phase_count ||
		(affected_phases && affected_phase_count != snapshot->phase_count) ||
		(affected_phases &&
		 !SG_DestinationFieldValid(snapshot, before_field)) ||
		!SnapshotMatchesModel(snapshot))
		return 0;
	if (affected_phases) {
		uint32_t index;

		if (affected_phases[destination->pose.phase.phase_id] != 1U)
			return 0;
		for (index = 0U; index < snapshot->phase_count; index++)
			if (affected_phases[index] > 1U ||
				(before_field->samples[index].finite == 1U &&
				 before_field->samples[index].cost_ms == 0U &&
				 affected_phases[index] == 0U))
				return 0;
	}
	model = snapshot->model;
	memset(&work, 0, sizeof(work));
	if (model->kernel_count > SG_RUNE_MODEL_MAX_KERNELS ||
		model->phase_transition_count > SG_RUNE_MODEL_MAX_PHASE_TRANSITIONS ||
		(model->kernel_count != 0U && !model->kernels) ||
		(model->phase_transition_count != 0U && !model->phase_transitions) ||
		model->kernel_count > UINT32_MAX - model->phase_transition_count) {
		return 0;
	}
	edge_capacity = model->kernel_count + model->phase_transition_count;
	if (!AllocateWork(snapshot->phase_count, edge_capacity,
		model->phase_transition_count, &work)) {
		FreeWork(&work);
		return 0;
	}
	if (BuildReverseEdges(model, &work)) {
		uint32_t edge;
		int reverse_closed = 1;

		if (affected_phases) {
			uint32_t destination_index;

			for (destination_index = 0U;
				destination_index < snapshot->phase_count;
				destination_index++)
				if (affected_phases[destination_index] != 0U)
					for (edge = work.first_incoming[destination_index];
						edge != SG_FIELD_NO_INDEX;
						edge = work.next_incoming[edge])
						if (affected_phases[work.source_phase[edge]] == 0U)
							reverse_closed = 0;
		}
		if (reverse_closed && affected_phases)
			memcpy(samples, before_field->samples,
				(size_t)snapshot->phase_count * sizeof(*samples));
		if (reverse_closed && InitializeSamples(snapshot, destination, samples,
			&work, affected_phases)) {
			SolveFixedPoint(snapshot, samples, &work,
				destination->pose.phase.phase_id, affected_phases);
			*out = (sg_destination_field_t){
				.rune_identity = snapshot->identity,
				.topology_revision = snapshot->topology_revision,
				.generation = destination->generation,
				.computed_at_ms = computed_at_ms,
				.destination = *destination,
				.samples = samples,
				.sample_count = snapshot->phase_count,
				.complete = 1U
			};
			success = 1;
		}
	}
	FreeWork(&work);
	if (!success)
		memset(out, 0, sizeof(*out));
	return success;
}

int SG_DestinationFieldSolve(const sg_rune_runtime_snapshot_t *snapshot,
	const sg_destination_handle_t *destination, uint64_t computed_at_ms,
	sg_field_sample_t *samples, uint32_t sample_capacity,
	sg_destination_field_t *out)
{
	return SolveInternal(snapshot, destination, computed_at_ms, NULL, NULL, 0U,
		samples, sample_capacity, out);
}

int SG_DestinationFieldSolveAffected(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_destination_field_t *before_field,
	const sg_destination_handle_t *destination, uint64_t computed_at_ms,
	const uint8_t *affected_phases, uint32_t affected_phase_count,
	sg_field_sample_t *samples, uint32_t sample_capacity,
	sg_destination_field_t *out)
{
	if (!affected_phases) {
		if (out)
			memset(out, 0, sizeof(*out));
		return 0;
	}
	return SolveInternal(snapshot, destination, computed_at_ms, before_field,
		affected_phases, affected_phase_count, samples, sample_capacity, out);
}

int SG_DestinationFieldDependencyClosure(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_phase_coordinate_t *before,
	const sg_phase_coordinate_t *after, uint8_t *affected_phases,
	uint32_t affected_capacity)
{
	sg_field_work_t work;
	const sg_rune_model_t *model;
	uint32_t edge_capacity;
	uint32_t head = 0U;
	uint32_t tail = 0U;
	uint32_t queued_count = 0U;
	uint32_t seed;
	int success = 0;

	if (affected_phases && affected_capacity != 0U)
		memset(affected_phases, 0,
			(size_t)affected_capacity * sizeof(*affected_phases));
	if (!SG_RuneRuntimeSnapshotValid(snapshot) ||
		!SG_PhaseCoordinateValid(snapshot, before) ||
		!SG_PhaseCoordinateValid(snapshot, after) || !affected_phases ||
		affected_capacity < snapshot->phase_count ||
		!SnapshotMatchesModel(snapshot))
		return 0;
	model = snapshot->model;
	if (model->kernel_count > SG_RUNE_MODEL_MAX_KERNELS ||
		model->phase_transition_count > SG_RUNE_MODEL_MAX_PHASE_TRANSITIONS ||
		(model->kernel_count != 0U && !model->kernels) ||
		(model->phase_transition_count != 0U && !model->phase_transitions) ||
		model->kernel_count > UINT32_MAX - model->phase_transition_count)
		return 0;
	edge_capacity = model->kernel_count + model->phase_transition_count;
	memset(&work, 0, sizeof(work));
	if (!AllocateWork(snapshot->phase_count, edge_capacity,
		model->phase_transition_count, &work)) {
		FreeWork(&work);
		return 0;
	}
	if (!BuildReverseEdges(model, &work))
		goto done;
	for (seed = 0U; seed < 2U; seed++) {
		uint32_t phase = seed == 0U ? before->phase_id : after->phase_id;

		if (affected_phases[phase] != 0U)
			continue;
		affected_phases[phase] = 1U;
		work.queue[tail] = phase;
		tail = (tail + 1U) % snapshot->phase_count;
		queued_count++;
	}
	while (queued_count != 0U) {
		uint32_t current = work.queue[head];
		uint32_t edge;

		head = (head + 1U) % snapshot->phase_count;
		queued_count--;
		for (edge = work.first_incoming[current]; edge != SG_FIELD_NO_INDEX;
			edge = work.next_incoming[edge]) {
			uint32_t source = work.source_phase[edge];

			if (affected_phases[source] != 0U)
				continue;
			affected_phases[source] = 1U;
			work.queue[tail] = source;
			tail = (tail + 1U) % snapshot->phase_count;
			queued_count++;
		}
	}
	success = 1;
done:
	FreeWork(&work);
	if (!success)
		memset(affected_phases, 0,
			(size_t)snapshot->phase_count * sizeof(*affected_phases));
	return success;
}
