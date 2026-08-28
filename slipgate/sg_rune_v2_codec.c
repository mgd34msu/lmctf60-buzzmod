/* sg_rune_v2_codec.c -- canonical, allocation-free RUNE v2 codec. */
#include "sg_rune_v2_codec.h"

#include <string.h>

typedef struct codec_range_s
{
	uintptr_t begin;
	uintptr_t end;
} codec_range_t;

static void CodecPutF32(unsigned char *bytes, float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	SG_RuneV2WirePutU32(bytes, bits);
}

static float CodecGetF32(const unsigned char *bytes)
{
	uint32_t bits = SG_RuneV2WireGetU32(bytes);
	float value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

static void CodecPutStableId(unsigned char *bytes,
	const sg_rune_stable_id_t *id)
{
	SG_RuneV2WirePutU64(bytes + SG_RUNE_V2_STABLE_ID_SOURCE_OFFSET,
		id->source_set_identity);
	SG_RuneV2WirePutU64(bytes + SG_RUNE_V2_STABLE_ID_HIGH_OFFSET, id->high);
	SG_RuneV2WirePutU64(bytes + SG_RUNE_V2_STABLE_ID_LOW_OFFSET, id->low);
}

static void CodecGetStableId(const unsigned char *bytes,
	sg_rune_stable_id_t *id)
{
	id->source_set_identity = SG_RuneV2WireGetU64(bytes +
		SG_RUNE_V2_STABLE_ID_SOURCE_OFFSET);
	id->high = SG_RuneV2WireGetU64(bytes + SG_RUNE_V2_STABLE_ID_HIGH_OFFSET);
	id->low = SG_RuneV2WireGetU64(bytes + SG_RUNE_V2_STABLE_ID_LOW_OFFSET);
}

static void CodecPutOrder(unsigned char *bytes, const sg_rune_order_key_t *order)
{
	SG_RuneV2WirePutU64(bytes + SG_RUNE_V2_ORDER_SOURCE_SET_OFFSET,
		order->source_set_identity);
	SG_RuneV2WirePutU32(bytes + SG_RUNE_V2_ORDER_DOMAIN_OFFSET, order->domain);
	SG_RuneV2WirePutU32(bytes + SG_RUNE_V2_ORDER_SOURCE_INDEX_OFFSET,
		order->source_index);
	SG_RuneV2WirePutU32(bytes + SG_RUNE_V2_ORDER_LOCAL_ORDINAL_OFFSET,
		order->local_ordinal);
	SG_RuneV2WirePutU32(bytes + SG_RUNE_V2_ORDER_VARIANT_OFFSET,
		order->variant);
}

static void CodecGetOrder(const unsigned char *bytes, sg_rune_order_key_t *order)
{
	order->source_set_identity = SG_RuneV2WireGetU64(bytes +
		SG_RUNE_V2_ORDER_SOURCE_SET_OFFSET);
	order->domain = SG_RuneV2WireGetU32(bytes +
		SG_RUNE_V2_ORDER_DOMAIN_OFFSET);
	order->source_index = SG_RuneV2WireGetU32(bytes +
		SG_RUNE_V2_ORDER_SOURCE_INDEX_OFFSET);
	order->local_ordinal = SG_RuneV2WireGetU32(bytes +
		SG_RUNE_V2_ORDER_LOCAL_ORDINAL_OFFSET);
	order->variant = SG_RuneV2WireGetU32(bytes +
		SG_RUNE_V2_ORDER_VARIANT_OFFSET);
}

static void CodecPutGeometry(unsigned char *bytes,
	const sg_rune_source_geometry_ref_t *geometry)
{
	SG_RuneV2WirePutU64(bytes + SG_RUNE_V2_GEOMETRY_SOURCE_SET_OFFSET,
		geometry->source_set_identity);
	SG_RuneV2WirePutU32(bytes + SG_RUNE_V2_GEOMETRY_SOURCE_INDEX_OFFSET,
		geometry->source_index);
	SG_RuneV2WirePutU32(bytes + SG_RUNE_V2_GEOMETRY_SOURCE_ORDINAL_OFFSET,
		geometry->source_ordinal);
}

static void CodecGetGeometry(const unsigned char *bytes,
	sg_rune_source_geometry_ref_t *geometry)
{
	geometry->source_set_identity = SG_RuneV2WireGetU64(bytes +
		SG_RUNE_V2_GEOMETRY_SOURCE_SET_OFFSET);
	geometry->source_index = SG_RuneV2WireGetU32(bytes +
		SG_RUNE_V2_GEOMETRY_SOURCE_INDEX_OFFSET);
	geometry->source_ordinal = SG_RuneV2WireGetU32(bytes +
		SG_RUNE_V2_GEOMETRY_SOURCE_ORDINAL_OFFSET);
}

static void CodecPutVec3(unsigned char *bytes, const sg_rune_vec3_t *vector)
{
	CodecPutF32(bytes + SG_RUNE_V2_VEC3_X_OFFSET, vector->value[0]);
	CodecPutF32(bytes + SG_RUNE_V2_VEC3_Y_OFFSET, vector->value[1]);
	CodecPutF32(bytes + SG_RUNE_V2_VEC3_Z_OFFSET, vector->value[2]);
}

static void CodecGetVec3(const unsigned char *bytes, sg_rune_vec3_t *vector)
{
	vector->value[0] = CodecGetF32(bytes + SG_RUNE_V2_VEC3_X_OFFSET);
	vector->value[1] = CodecGetF32(bytes + SG_RUNE_V2_VEC3_Y_OFFSET);
	vector->value[2] = CodecGetF32(bytes + SG_RUNE_V2_VEC3_Z_OFFSET);
}

static void CodecPutInterval(unsigned char *bytes,
	const sg_rune_interval_t *interval)
{
	CodecPutF32(bytes + SG_RUNE_V2_INTERVAL_MIN_OFFSET, interval->min_value);
	CodecPutF32(bytes + SG_RUNE_V2_INTERVAL_MAX_OFFSET, interval->max_value);
}

static void CodecGetInterval(const unsigned char *bytes,
	sg_rune_interval_t *interval)
{
	interval->min_value = CodecGetF32(bytes + SG_RUNE_V2_INTERVAL_MIN_OFFSET);
	interval->max_value = CodecGetF32(bytes + SG_RUNE_V2_INTERVAL_MAX_OFFSET);
}

static void CodecPutInterval3(unsigned char *bytes,
	const sg_rune_interval3_t *interval)
{
	CodecPutInterval(bytes, &interval->x);
	CodecPutInterval(bytes + SG_RUNE_V2_INTERVAL_BYTES, &interval->y);
	CodecPutInterval(bytes + SG_RUNE_V2_INTERVAL_BYTES * 2U, &interval->z);
}

static void CodecGetInterval3(const unsigned char *bytes,
	sg_rune_interval3_t *interval)
{
	CodecGetInterval(bytes, &interval->x);
	CodecGetInterval(bytes + SG_RUNE_V2_INTERVAL_BYTES, &interval->y);
	CodecGetInterval(bytes + SG_RUNE_V2_INTERVAL_BYTES * 2U, &interval->z);
}

static void CodecPutSpan(unsigned char *bytes, uint32_t first, uint32_t count)
{
	SG_RuneV2WirePutU32(bytes + SG_RUNE_V2_SPAN_FIRST_OFFSET, first);
	SG_RuneV2WirePutU32(bytes + SG_RUNE_V2_SPAN_COUNT_OFFSET, count);
}

static void CodecGetSpan(const unsigned char *bytes, uint32_t *first,
	uint32_t *count)
{
	*first = SG_RuneV2WireGetU32(bytes + SG_RUNE_V2_SPAN_FIRST_OFFSET);
	*count = SG_RuneV2WireGetU32(bytes + SG_RUNE_V2_SPAN_COUNT_OFFSET);
}

static void CodecPutEntity(unsigned char *bytes,
	const sg_rune_entity_ref_t *entity)
{
	SG_RuneV2WirePutU32(bytes + SG_RUNE_V2_ENTITY_INDEX_OFFSET, entity->index);
	SG_RuneV2WirePutU32(bytes + SG_RUNE_V2_ENTITY_SPAWN_ORDINAL_OFFSET,
		entity->spawn_ordinal);
}

static void CodecGetEntity(const unsigned char *bytes,
	sg_rune_entity_ref_t *entity)
{
	entity->index = SG_RuneV2WireGetU32(bytes +
		SG_RUNE_V2_ENTITY_INDEX_OFFSET);
	entity->spawn_ordinal = SG_RuneV2WireGetU32(bytes +
		SG_RUNE_V2_ENTITY_SPAWN_ORDINAL_OFFSET);
}

static sg_rune_v2_wire_diagnostic_t CodecFailureDiagnostic(
	sg_rune_failure_reason_t reason)
{
	switch (reason)
	{
	case SG_RUNE_FAILURE_NONE: return SG_RUNE_V2_WIRE_OK;
	case SG_RUNE_FAILURE_LIMIT_EXCEEDED: return SG_RUNE_V2_WIRE_HOSTILE_COUNT;
	case SG_RUNE_FAILURE_INVALID_REFERENCE:
		return SG_RUNE_V2_WIRE_BAD_REFERENCE;
	case SG_RUNE_FAILURE_IDENTITY_MISMATCH:
		return SG_RUNE_V2_WIRE_BAD_BINDING;
	case SG_RUNE_FAILURE_INVALID_ARGUMENT:
		return SG_RUNE_V2_WIRE_INVALID_ARGUMENT;
	case SG_RUNE_FAILURE_DUPLICATE_ID:
	case SG_RUNE_FAILURE_NONDETERMINISTIC_ORDER:
	case SG_RUNE_FAILURE_MISSING_CONFIGURATION:
	case SG_RUNE_FAILURE_MISSING_PORTAL:
	case SG_RUNE_FAILURE_NONFINITE_GEOMETRY:
	case SG_RUNE_FAILURE_INVALID_PHASE:
	case SG_RUNE_FAILURE_INVALID_KERNEL:
	case SG_RUNE_FAILURE_INVALID_SEMANTICS:
	case SG_RUNE_FAILURE_UNSUPPORTED_BSP:
	case SG_RUNE_FAILURE_UNSUPPORTED_PHYSICS:
	case SG_RUNE_FAILURE_INCOMPLETE:
	case SG_RUNE_FAILURE_REASON_COUNT:
		return SG_RUNE_V2_WIRE_BAD_RECORD;
	}
	return SG_RUNE_V2_WIRE_BAD_RECORD;
}

static sg_rune_v2_wire_diagnostic_t CodecValidateCounts(
	const sg_rune_model_t *model)
{
	if (!model)
		return SG_RUNE_V2_WIRE_INVALID_ARGUMENT;
	if (model->plane_count > SG_RUNE_MODEL_MAX_PLANES ||
		model->portal_vertex_count > SG_RUNE_MODEL_MAX_PORTAL_VERTICES ||
		model->phase_count > SG_RUNE_MODEL_MAX_PHASES ||
		model->phase_transition_count > SG_RUNE_MODEL_MAX_PHASE_TRANSITIONS ||
		model->cell_count > SG_RUNE_MODEL_MAX_CELLS ||
		model->portal_count > SG_RUNE_MODEL_MAX_PORTALS ||
		model->surface_count > SG_RUNE_MODEL_MAX_SURFACES ||
		model->affordance_count > SG_RUNE_MODEL_MAX_AFFORDANCES ||
		model->kernel_count > SG_RUNE_MODEL_MAX_KERNELS ||
		model->landmark_count > SG_RUNE_MODEL_MAX_LANDMARKS ||
		model->mechanism_count > SG_RUNE_MODEL_MAX_MECHANISMS)
		return SG_RUNE_V2_WIRE_HOSTILE_COUNT;
	return SG_RUNE_V2_WIRE_OK;
}

static sg_rune_v2_wire_diagnostic_t CodecValidateModel(
	const sg_rune_model_t *model,
	const sg_rune_validation_evidence_t *evidence)
{
	sg_rune_v2_wire_diagnostic_t diagnostic = CodecValidateCounts(model);

	if (diagnostic != SG_RUNE_V2_WIRE_OK)
		return diagnostic;
	return CodecFailureDiagnostic(SG_RuneModelValidate(model, evidence));
}

static uint32_t CodecSectionCount(const sg_rune_model_t *model, uint16_t type)
{
	switch (type)
	{
	case SG_RUNE_V2_SECTION_MODEL: return 1U;
	case SG_RUNE_V2_SECTION_PLANES: return model->plane_count;
	case SG_RUNE_V2_SECTION_PORTAL_VERTICES: return model->portal_vertex_count;
	case SG_RUNE_V2_SECTION_PHASES: return model->phase_count;
	case SG_RUNE_V2_SECTION_PHASE_TRANSITIONS:
		return model->phase_transition_count;
	case SG_RUNE_V2_SECTION_CELLS: return model->cell_count;
	case SG_RUNE_V2_SECTION_PORTALS: return model->portal_count;
	case SG_RUNE_V2_SECTION_SURFACES: return model->surface_count;
	case SG_RUNE_V2_SECTION_AFFORDANCES: return model->affordance_count;
	case SG_RUNE_V2_SECTION_KERNELS: return model->kernel_count;
	case SG_RUNE_V2_SECTION_LANDMARKS: return model->landmark_count;
	case SG_RUNE_V2_SECTION_MECHANISMS: return model->mechanism_count;
	case SG_RUNE_V2_SECTION_BINDING: return 1U;
	default: return 0U;
	}
}

static int CodecAlign(uint64_t *value)
{
	uint64_t mask = SG_RUNE_V2_SECTION_ALIGNMENT - 1U;
	uint64_t added;

	return value && SG_RuneV2WireCheckedAdd(*value, mask, &added) &&
		((*value = added & ~mask), 1);
}

static sg_rune_v2_wire_diagnostic_t CodecComputeSize(
	const sg_rune_model_t *model, size_t *encoded_size_out)
{
	uint64_t total = SG_RUNE_V2_HEADER_BYTES +
		(uint64_t)SG_RUNE_V2_REQUIRED_SECTION_COUNT *
		SG_RUNE_V2_SECTION_ENTRY_BYTES;
	uint32_t index;

	for (index = 0U; index < SG_RUNE_V2_REQUIRED_SECTION_COUNT; index++)
	{
		uint16_t type = (uint16_t)(index + 1U);
		uint64_t bytes;

		if (!CodecAlign(&total) ||
			!SG_RuneV2WireCheckedMul(SG_RuneV2WireRecordBytes(type),
				CodecSectionCount(model, type), &bytes) ||
			!SG_RuneV2WireCheckedAdd(total, bytes, &total))
			return SG_RUNE_V2_WIRE_BAD_SIZE;
	}
	if (!CodecAlign(&total) || total > SG_RUNE_V2_MAX_ARTIFACT_BYTES ||
		total > SIZE_MAX)
		return SG_RUNE_V2_WIRE_BAD_SIZE;
	*encoded_size_out = (size_t)total;
	return SG_RUNE_V2_WIRE_OK;
}

sg_rune_v2_wire_diagnostic_t SG_RuneV2CodecEncodedSize(
	const sg_rune_model_t *model,
	const sg_rune_validation_evidence_t *evidence,
	size_t *encoded_size_out)
{
	sg_rune_v2_wire_diagnostic_t diagnostic;

	if (!encoded_size_out)
		return SG_RUNE_V2_WIRE_INVALID_ARGUMENT;
	diagnostic = CodecValidateModel(model, evidence);
	if (diagnostic != SG_RUNE_V2_WIRE_OK)
		return diagnostic;
	return CodecComputeSize(model, encoded_size_out);
}

static void CodecEncodeModel(unsigned char *record,
	const sg_rune_model_t *model,
	const sg_rune_validation_evidence_t *evidence)
{
	const sg_rune_model_identity_t *identity = &model->identity;
	const sg_rune_physics_parameters_t *physics = &identity->physics;
	const sg_rune_completeness_t *complete = &model->completeness;

	SG_RuneV2WirePutU16(record + SG_RUNE_V2_MODEL_VERSION_OFFSET,
		model->version);
	SG_RuneV2WirePutU16(record + SG_RUNE_V2_MODEL_RESERVED_OFFSET,
		model->reserved);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_MODEL_SCHEMA_TAG_OFFSET,
		model->schema_tag);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_MODEL_FLAGS_OFFSET, model->flags);
	SG_RuneV2WirePutU64(record + SG_RUNE_V2_MODEL_BSP_CONTENT_ID_OFFSET,
		identity->bsp_content_id);
	SG_RuneV2WirePutU64(record + SG_RUNE_V2_MODEL_ENTITY_SEMANTICS_ID_OFFSET,
		identity->entity_semantics_id);
	SG_RuneV2WirePutU64(record + SG_RUNE_V2_MODEL_PHYSICS_ABI_ID_OFFSET,
		identity->physics_abi_id);
	SG_RuneV2WirePutU64(record + SG_RUNE_V2_MODEL_SOURCE_SET_ID_OFFSET,
		identity->source_set_identity);
	SG_RuneV2WirePutU64(record + SG_RUNE_V2_MODEL_SCHEMA_ID_OFFSET,
		identity->schema_id);
	SG_RuneV2WirePutU64(record + SG_RUNE_V2_MODEL_PRODUCER_ID_OFFSET,
		identity->producer_identity);
	CodecPutVec3(record + SG_RUNE_V2_MODEL_STANDING_HULL_MINS_OFFSET,
		&identity->standing_hull.mins);
	CodecPutVec3(record + SG_RUNE_V2_MODEL_STANDING_HULL_MAXS_OFFSET,
		&identity->standing_hull.maxs);
	CodecPutVec3(record + SG_RUNE_V2_MODEL_CROUCHING_HULL_MINS_OFFSET,
		&identity->crouching_hull.mins);
	CodecPutVec3(record + SG_RUNE_V2_MODEL_CROUCHING_HULL_MAXS_OFFSET,
		&identity->crouching_hull.maxs);
	CodecPutF32(record + SG_RUNE_V2_MODEL_GRAVITY_OFFSET, physics->gravity);
	CodecPutF32(record + SG_RUNE_V2_MODEL_GROUND_ACCELERATION_OFFSET,
		physics->ground_acceleration);
	CodecPutF32(record + SG_RUNE_V2_MODEL_AIR_ACCELERATION_OFFSET,
		physics->air_acceleration);
	CodecPutF32(record + SG_RUNE_V2_MODEL_WATER_ACCELERATION_OFFSET,
		physics->water_acceleration);
	CodecPutF32(record + SG_RUNE_V2_MODEL_HOOK_ACCELERATION_OFFSET,
		physics->hook_acceleration);
	CodecPutF32(record + SG_RUNE_V2_MODEL_EXTERNAL_ACCELERATION_OFFSET,
		physics->external_acceleration);
	CodecPutF32(record + SG_RUNE_V2_MODEL_WATER_DRAG_OFFSET,
		physics->water_drag);
	CodecPutF32(record + SG_RUNE_V2_MODEL_MAX_VELOCITY_OFFSET,
		physics->max_velocity);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_MODEL_FRAME_MS_OFFSET,
		physics->frame_ms);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_MODEL_SUBSTEP_MS_OFFSET,
		physics->substep_ms);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_MODEL_COMPLETENESS_STATE_OFFSET,
		(uint32_t)complete->state);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_MODEL_COMPLETENESS_REASON_OFFSET,
		(uint32_t)complete->reason);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_MODEL_EXPECTED_CELLS_OFFSET,
		complete->expected_cells);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_MODEL_EXPECTED_PORTALS_OFFSET,
		complete->expected_portals);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_MODEL_COVERED_CELLS_OFFSET,
		complete->covered_cells);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_MODEL_COVERED_PORTALS_OFFSET,
		complete->covered_portals);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_MODEL_FAILURE_RECORD_OFFSET,
		complete->failure_record);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_MODEL_EVIDENCE_VERSION_OFFSET,
		evidence->version);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_MODEL_EVIDENCE_RESERVED_OFFSET,
		evidence->reserved);
	SG_RuneV2WirePutU64(record + SG_RUNE_V2_MODEL_VERIFIER_ID_OFFSET,
		evidence->verifier_identity);
	SG_RuneV2WirePutU64(record + SG_RUNE_V2_MODEL_EVIDENCE_BSP_ID_OFFSET,
		evidence->bsp_content_id);
	SG_RuneV2WirePutU64(record + SG_RUNE_V2_MODEL_EVIDENCE_SOURCE_SET_ID_OFFSET,
		evidence->source_set_identity);
	SG_RuneV2WirePutU64(record + SG_RUNE_V2_MODEL_FIXED_POINT_ID_OFFSET,
		evidence->fixed_point_identity);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_MODEL_FIXED_POINT_ROUNDS_OFFSET,
		evidence->fixed_point_rounds);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_MODEL_PROVED_CELLS_OFFSET,
		evidence->proved_cells);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_MODEL_PROVED_PORTALS_OFFSET,
		evidence->proved_portals);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_MODEL_OMITTED_CELLS_OFFSET,
		evidence->omitted_cells);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_MODEL_OMITTED_PORTALS_OFFSET,
		evidence->omitted_portals);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_MODEL_INVENTED_PORTALS_OFFSET,
		evidence->invented_portals);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_MODEL_PENDING_WORK_OFFSET,
		evidence->pending_work);
}

static void CodecEncodePlane(unsigned char *record,
	const sg_rune_plane_t *plane)
{
	CodecPutStableId(record + SG_RUNE_V2_RECORD_ID_OFFSET, &plane->id.value);
	CodecPutOrder(record + SG_RUNE_V2_RECORD_ORDER_OFFSET, &plane->order);
	CodecPutVec3(record + SG_RUNE_V2_PLANE_NORMAL_OFFSET, &plane->normal);
	CodecPutF32(record + SG_RUNE_V2_PLANE_DISTANCE_OFFSET, plane->distance);
}

static void CodecEncodePhase(unsigned char *record,
	const sg_rune_phase_basis_t *phase)
{
	CodecPutStableId(record + SG_RUNE_V2_RECORD_ID_OFFSET, &phase->id.value);
	CodecPutOrder(record + SG_RUNE_V2_RECORD_ORDER_OFFSET, &phase->order);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_PHASE_STANCE_OFFSET,
		(uint32_t)phase->stance);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_PHASE_MOTION_OFFSET,
		(uint32_t)phase->motion);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_PHASE_SUPPORT_OFFSET,
		(uint32_t)phase->support);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_PHASE_MEDIUM_OFFSET,
		(uint32_t)phase->medium);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_PHASE_VOID_RELATION_OFFSET,
		(uint32_t)phase->void_relation);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_PHASE_REFERENCE_FRAME_OFFSET,
		(uint32_t)phase->reference_frame);
	CodecPutStableId(record + SG_RUNE_V2_PHASE_MOVER_OFFSET,
		&phase->mover.value);
	CodecPutInterval3(record + SG_RUNE_V2_PHASE_VELOCITY_OFFSET,
		&phase->velocity);
	CodecPutInterval(record + SG_RUNE_V2_PHASE_ELAPSED_MS_OFFSET,
		&phase->elapsed_ms);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_PHASE_TIME_QUANTUM_OFFSET,
		phase->time_quantum_ms);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_PHASE_TIME_HORIZON_OFFSET,
		phase->time_horizon_ms);
}

static void CodecEncodeTransition(unsigned char *record,
	const sg_rune_phase_transition_t *transition)
{
	CodecPutStableId(record + SG_RUNE_V2_RECORD_ID_OFFSET,
		&transition->id.value);
	CodecPutOrder(record + SG_RUNE_V2_RECORD_ORDER_OFFSET, &transition->order);
	CodecPutStableId(record + SG_RUNE_V2_TRANSITION_CELL_OFFSET,
		&transition->cell.value);
	CodecPutStableId(record + SG_RUNE_V2_TRANSITION_SOURCE_PHASE_OFFSET,
		&transition->source_phase.value);
	CodecPutStableId(record + SG_RUNE_V2_TRANSITION_DESTINATION_PHASE_OFFSET,
		&transition->destination_phase.value);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_TRANSITION_KIND_OFFSET,
		(uint32_t)transition->kind);
	CodecPutInterval(record + SG_RUNE_V2_TRANSITION_DURATION_OFFSET,
		&transition->duration_ms);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_TRANSITION_FLAGS_OFFSET,
		transition->flags);
}

static void CodecEncodeCell(unsigned char *record, const sg_rune_cell_t *cell)
{
	CodecPutStableId(record + SG_RUNE_V2_RECORD_ID_OFFSET, &cell->id.value);
	CodecPutOrder(record + SG_RUNE_V2_RECORD_ORDER_OFFSET, &cell->order);
	CodecPutGeometry(record + SG_RUNE_V2_CELL_GEOMETRY_OFFSET, &cell->geometry);
	CodecPutVec3(record + SG_RUNE_V2_CELL_BOUNDS_MINS_OFFSET,
		&cell->bounds.mins);
	CodecPutVec3(record + SG_RUNE_V2_CELL_BOUNDS_MAXS_OFFSET,
		&cell->bounds.maxs);
	CodecPutSpan(record + SG_RUNE_V2_CELL_BOUNDARY_PLANES_OFFSET,
		cell->boundary_planes.first, cell->boundary_planes.count);
	CodecPutSpan(record + SG_RUNE_V2_CELL_PHASES_OFFSET,
		cell->phases.first, cell->phases.count);
	CodecPutSpan(record + SG_RUNE_V2_CELL_SURFACES_OFFSET,
		cell->surfaces.first, cell->surfaces.count);
	CodecPutSpan(record + SG_RUNE_V2_CELL_AFFORDANCES_OFFSET,
		cell->affordances.first, cell->affordances.count);
	CodecPutSpan(record + SG_RUNE_V2_CELL_KERNELS_OFFSET,
		cell->kernels.first, cell->kernels.count);
	CodecPutSpan(record + SG_RUNE_V2_CELL_LANDMARKS_OFFSET,
		cell->landmarks.first, cell->landmarks.count);
	CodecPutSpan(record + SG_RUNE_V2_CELL_MECHANISMS_OFFSET,
		cell->mechanisms.first, cell->mechanisms.count);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_CELL_BSP_LEAF_OFFSET,
		cell->bsp_leaf.index);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_CELL_BSP_AREA_OFFSET,
		cell->bsp_area.index);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_CELL_BSP_CLUSTER_OFFSET,
		cell->bsp_cluster.index);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_CELL_CONTENTS_OFFSET,
		cell->contents);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_CELL_SEMANTICS_OFFSET,
		cell->semantics);
}

static void CodecEncodePortal(unsigned char *record,
	const sg_rune_portal_t *portal)
{
	CodecPutStableId(record + SG_RUNE_V2_RECORD_ID_OFFSET, &portal->id.value);
	CodecPutOrder(record + SG_RUNE_V2_RECORD_ORDER_OFFSET, &portal->order);
	CodecPutGeometry(record + SG_RUNE_V2_PORTAL_GEOMETRY_OFFSET,
		&portal->geometry);
	CodecPutStableId(record + SG_RUNE_V2_PORTAL_FROM_CELL_OFFSET,
		&portal->from_cell.value);
	CodecPutStableId(record + SG_RUNE_V2_PORTAL_TO_CELL_OFFSET,
		&portal->to_cell.value);
	CodecPutStableId(record + SG_RUNE_V2_PORTAL_BOUNDARY_PLANE_OFFSET,
		&portal->boundary_plane.value);
	CodecPutSpan(record + SG_RUNE_V2_PORTAL_BOUNDARY_VERTICES_OFFSET,
		portal->boundary_vertices.first, portal->boundary_vertices.count);
	CodecPutSpan(record + SG_RUNE_V2_PORTAL_PHASES_OFFSET,
		portal->phases.first, portal->phases.count);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_PORTAL_DIRECTION_OFFSET,
		(uint32_t)portal->direction);
	CodecPutF32(record + SG_RUNE_V2_PORTAL_CLEARANCE_OFFSET,
		portal->clearance);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_PORTAL_CONTENTS_FROM_OFFSET,
		portal->contents_from);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_PORTAL_CONTENTS_TO_OFFSET,
		portal->contents_to);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_PORTAL_FLAGS_OFFSET, portal->flags);
}

static void CodecEncodeSurface(unsigned char *record,
	const sg_rune_surface_t *surface)
{
	CodecPutStableId(record + SG_RUNE_V2_RECORD_ID_OFFSET, &surface->id.value);
	CodecPutOrder(record + SG_RUNE_V2_RECORD_ORDER_OFFSET, &surface->order);
	CodecPutGeometry(record + SG_RUNE_V2_SURFACE_GEOMETRY_OFFSET,
		&surface->geometry);
	CodecPutStableId(record + SG_RUNE_V2_SURFACE_OWNER_CELL_OFFSET,
		&surface->owner_cell.value);
	CodecPutStableId(record + SG_RUNE_V2_SURFACE_PLANE_OFFSET,
		&surface->plane.value);
	CodecPutVec3(record + SG_RUNE_V2_SURFACE_NORMAL_OFFSET, &surface->normal);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_SURFACE_CONTENTS_OFFSET,
		surface->contents);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_SURFACE_SEMANTICS_OFFSET,
		surface->semantics);
}

static void CodecEncodeAffordance(unsigned char *record,
	const sg_rune_affordance_t *affordance)
{
	CodecPutStableId(record + SG_RUNE_V2_RECORD_ID_OFFSET,
		&affordance->id.value);
	CodecPutOrder(record + SG_RUNE_V2_RECORD_ORDER_OFFSET, &affordance->order);
	CodecPutStableId(record + SG_RUNE_V2_AFFORDANCE_OWNER_CELL_OFFSET,
		&affordance->owner_cell.value);
	CodecPutSpan(record + SG_RUNE_V2_AFFORDANCE_SURFACES_OFFSET,
		affordance->surfaces.first, affordance->surfaces.count);
	CodecPutSpan(record + SG_RUNE_V2_AFFORDANCE_PHASES_OFFSET,
		affordance->phases.first, affordance->phases.count);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_AFFORDANCE_KIND_OFFSET,
		(uint32_t)affordance->kind);
	CodecPutInterval(record + SG_RUNE_V2_AFFORDANCE_RANGE_OFFSET,
		&affordance->range);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_AFFORDANCE_FLAGS_OFFSET,
		affordance->flags);
}

static void CodecEncodeKernel(unsigned char *record,
	const sg_rune_capability_kernel_t *kernel)
{
	const sg_rune_kernel_parameters_t *parameters = &kernel->parameters;

	CodecPutStableId(record + SG_RUNE_V2_RECORD_ID_OFFSET, &kernel->id.value);
	CodecPutOrder(record + SG_RUNE_V2_RECORD_ORDER_OFFSET, &kernel->order);
	CodecPutStableId(record + SG_RUNE_V2_KERNEL_SOURCE_CELL_OFFSET,
		&kernel->source_cell.value);
	CodecPutStableId(record + SG_RUNE_V2_KERNEL_DESTINATION_CELL_OFFSET,
		&kernel->destination_cell.value);
	CodecPutStableId(record + SG_RUNE_V2_KERNEL_BOUNDARY_OFFSET,
		&kernel->boundary.value);
	CodecPutStableId(record + SG_RUNE_V2_KERNEL_AFFORDANCE_OFFSET,
		&kernel->affordance.value);
	CodecPutStableId(record + SG_RUNE_V2_KERNEL_MECHANISM_OFFSET,
		&kernel->mechanism.value);
	CodecPutStableId(record + SG_RUNE_V2_KERNEL_SOURCE_PHASE_OFFSET,
		&kernel->source_phase.value);
	CodecPutStableId(record + SG_RUNE_V2_KERNEL_DESTINATION_PHASE_OFFSET,
		&kernel->destination_phase.value);
	CodecPutStableId(record + SG_RUNE_V2_KERNEL_TRANSITION_OFFSET,
		&kernel->transition.value);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_KERNEL_FAMILY_OFFSET,
		(uint32_t)kernel->family);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_KERNEL_COST_LAW_OFFSET,
		(uint32_t)kernel->cost_law);
	CodecPutInterval3(record + SG_RUNE_V2_KERNEL_DISPLACEMENT_OFFSET,
		&parameters->displacement);
	CodecPutInterval(record + SG_RUNE_V2_KERNEL_DURATION_OFFSET,
		&parameters->duration_ms);
	CodecPutInterval(record + SG_RUNE_V2_KERNEL_SPEED_OFFSET,
		&parameters->speed);
	CodecPutInterval(record + SG_RUNE_V2_KERNEL_ACCELERATION_OFFSET,
		&parameters->acceleration);
	CodecPutInterval(record + SG_RUNE_V2_KERNEL_VERTICAL_ACCELERATION_OFFSET,
		&parameters->vertical_acceleration);
	CodecPutF32(record + SG_RUNE_V2_KERNEL_GRAVITY_OFFSET,
		parameters->gravity);
	CodecPutF32(record + SG_RUNE_V2_KERNEL_DRAG_OFFSET, parameters->drag);
	SG_RuneV2WirePutU64(record + SG_RUNE_V2_KERNEL_PHYSICS_ABI_OFFSET,
		parameters->physics_abi_id);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_KERNEL_FIXED_LATENCY_OFFSET,
		parameters->fixed_latency_ms);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_KERNEL_DWELL_OFFSET,
		parameters->dwell_ms);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_KERNEL_FLAGS_OFFSET,
		kernel->flags);
}

static void CodecEncodeLandmark(unsigned char *record,
	const sg_rune_landmark_t *landmark)
{
	CodecPutStableId(record + SG_RUNE_V2_RECORD_ID_OFFSET, &landmark->id.value);
	CodecPutOrder(record + SG_RUNE_V2_RECORD_ORDER_OFFSET, &landmark->order);
	CodecPutGeometry(record + SG_RUNE_V2_LANDMARK_GEOMETRY_OFFSET,
		&landmark->geometry);
	CodecPutStableId(record + SG_RUNE_V2_LANDMARK_CELL_OFFSET,
		&landmark->cell.value);
	CodecPutEntity(record + SG_RUNE_V2_LANDMARK_ENTITY_OFFSET,
		&landmark->entity);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_LANDMARK_KIND_OFFSET,
		(uint32_t)landmark->kind);
	CodecPutVec3(record + SG_RUNE_V2_LANDMARK_ORIGIN_OFFSET,
		&landmark->origin);
	CodecPutVec3(record + SG_RUNE_V2_LANDMARK_BOUNDS_MINS_OFFSET,
		&landmark->bounds.mins);
	CodecPutVec3(record + SG_RUNE_V2_LANDMARK_BOUNDS_MAXS_OFFSET,
		&landmark->bounds.maxs);
	CodecPutStableId(record + SG_RUNE_V2_LANDMARK_MECHANISM_OFFSET,
		&landmark->mechanism.value);
	CodecPutStableId(record + SG_RUNE_V2_LANDMARK_SURFACE_OFFSET,
		&landmark->surface.value);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_LANDMARK_SEMANTICS_OFFSET,
		landmark->semantics);
}

static void CodecEncodeMechanism(unsigned char *record,
	const sg_rune_mechanism_t *mechanism)
{
	CodecPutStableId(record + SG_RUNE_V2_RECORD_ID_OFFSET, &mechanism->id.value);
	CodecPutOrder(record + SG_RUNE_V2_RECORD_ORDER_OFFSET, &mechanism->order);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_MECHANISM_KIND_OFFSET,
		(uint32_t)mechanism->kind);
	CodecPutStableId(record + SG_RUNE_V2_MECHANISM_ENTRY_CELL_OFFSET,
		&mechanism->entry_cell.value);
	CodecPutStableId(record + SG_RUNE_V2_MECHANISM_EXIT_CELL_OFFSET,
		&mechanism->exit_cell.value);
	CodecPutStableId(record + SG_RUNE_V2_MECHANISM_ACTIVATION_LANDMARK_OFFSET,
		&mechanism->activation_landmark.value);
	CodecPutEntity(record + SG_RUNE_V2_MECHANISM_ENTITY_OFFSET,
		&mechanism->entity);
	CodecPutInterval(record + SG_RUNE_V2_MECHANISM_DWELL_OFFSET,
		&mechanism->dwell_ms);
	CodecPutInterval(record + SG_RUNE_V2_MECHANISM_TRAVEL_OFFSET,
		&mechanism->travel_ms);
	CodecPutSpan(record + SG_RUNE_V2_MECHANISM_TOPOLOGY_OFFSET,
		mechanism->topology.first, mechanism->topology.count);
	SG_RuneV2WirePutU32(record + SG_RUNE_V2_MECHANISM_FLAGS_OFFSET,
		mechanism->flags);
}

static void CodecEncodeSection(uint16_t type, unsigned char *record,
	const sg_rune_v2_wire_binding_t *binding,
	const sg_rune_model_t *model,
	const sg_rune_validation_evidence_t *evidence)
{
	uint32_t index;

	switch (type)
	{
	case SG_RUNE_V2_SECTION_MODEL:
		CodecEncodeModel(record, model, evidence);
		break;
	case SG_RUNE_V2_SECTION_PLANES:
		for (index = 0U; index < model->plane_count; index++,
			record += SG_RUNE_V2_PLANE_RECORD_BYTES)
			CodecEncodePlane(record, &model->planes[index]);
		break;
	case SG_RUNE_V2_SECTION_PORTAL_VERTICES:
		for (index = 0U; index < model->portal_vertex_count; index++,
			record += SG_RUNE_V2_PORTAL_VERTEX_RECORD_BYTES)
			CodecPutVec3(record, &model->portal_vertices[index]);
		break;
	case SG_RUNE_V2_SECTION_PHASES:
		for (index = 0U; index < model->phase_count; index++,
			record += SG_RUNE_V2_PHASE_RECORD_BYTES)
			CodecEncodePhase(record, &model->phases[index]);
		break;
	case SG_RUNE_V2_SECTION_PHASE_TRANSITIONS:
		for (index = 0U; index < model->phase_transition_count; index++,
			record += SG_RUNE_V2_PHASE_TRANSITION_RECORD_BYTES)
			CodecEncodeTransition(record, &model->phase_transitions[index]);
		break;
	case SG_RUNE_V2_SECTION_CELLS:
		for (index = 0U; index < model->cell_count; index++,
			record += SG_RUNE_V2_CELL_RECORD_BYTES)
			CodecEncodeCell(record, &model->cells[index]);
		break;
	case SG_RUNE_V2_SECTION_PORTALS:
		for (index = 0U; index < model->portal_count; index++,
			record += SG_RUNE_V2_PORTAL_RECORD_BYTES)
			CodecEncodePortal(record, &model->portals[index]);
		break;
	case SG_RUNE_V2_SECTION_SURFACES:
		for (index = 0U; index < model->surface_count; index++,
			record += SG_RUNE_V2_SURFACE_RECORD_BYTES)
			CodecEncodeSurface(record, &model->surfaces[index]);
		break;
	case SG_RUNE_V2_SECTION_AFFORDANCES:
		for (index = 0U; index < model->affordance_count; index++,
			record += SG_RUNE_V2_AFFORDANCE_RECORD_BYTES)
			CodecEncodeAffordance(record, &model->affordances[index]);
		break;
	case SG_RUNE_V2_SECTION_KERNELS:
		for (index = 0U; index < model->kernel_count; index++,
			record += SG_RUNE_V2_KERNEL_RECORD_BYTES)
			CodecEncodeKernel(record, &model->kernels[index]);
		break;
	case SG_RUNE_V2_SECTION_LANDMARKS:
		for (index = 0U; index < model->landmark_count; index++,
			record += SG_RUNE_V2_LANDMARK_RECORD_BYTES)
			CodecEncodeLandmark(record, &model->landmarks[index]);
		break;
	case SG_RUNE_V2_SECTION_MECHANISMS:
		for (index = 0U; index < model->mechanism_count; index++,
			record += SG_RUNE_V2_MECHANISM_RECORD_BYTES)
			CodecEncodeMechanism(record, &model->mechanisms[index]);
		break;
	case SG_RUNE_V2_SECTION_BINDING:
		memcpy(record + SG_RUNE_V2_BINDING_BSP_OFFSET,
			binding->bsp_identity.bytes, SG_RUNE_V2_CONTENT_ID_BYTES);
		memcpy(record + SG_RUNE_V2_BINDING_SCHEMA_OFFSET,
			binding->schema_identity.bytes, SG_RUNE_V2_CONTENT_ID_BYTES);
		break;
	default:
		break;
	}
}

static int CodecRange(const void *pointer, size_t count, size_t element_bytes,
	codec_range_t *range)
{
	size_t bytes;
	uintptr_t begin;

	if (!range || (count != 0U && !pointer) ||
		(count != 0U && element_bytes > SIZE_MAX / count))
		return 0;
	bytes = count * element_bytes;
	begin = (uintptr_t)pointer;
	if (bytes > (size_t)(UINTPTR_MAX - begin))
		return 0;
	range->begin = begin;
	range->end = begin + bytes;
	return 1;
}

static int CodecRangesOverlap(const codec_range_t *left,
	const codec_range_t *right)
{
	return left->begin < right->end && right->begin < left->end;
}

static int CodecEncodeRangesDisjoint(
	const sg_rune_v2_wire_binding_t *binding,
	const sg_rune_model_t *model,
	const sg_rune_validation_evidence_t *evidence,
	unsigned char *encoded, size_t encoded_size, size_t *encoded_size_out)
{
	codec_range_t output[2];
	codec_range_t input[14];
	const void *pointers[11];
	size_t counts[11];
	size_t sizes[11];
	size_t index;
	size_t out;

	pointers[0] = model->planes; counts[0] = model->plane_count;
	sizes[0] = sizeof(model->planes[0]);
	pointers[1] = model->portal_vertices; counts[1] = model->portal_vertex_count;
	sizes[1] = sizeof(model->portal_vertices[0]);
	pointers[2] = model->phases; counts[2] = model->phase_count;
	sizes[2] = sizeof(model->phases[0]);
	pointers[3] = model->phase_transitions;
	counts[3] = model->phase_transition_count;
	sizes[3] = sizeof(model->phase_transitions[0]);
	pointers[4] = model->cells; counts[4] = model->cell_count;
	sizes[4] = sizeof(model->cells[0]);
	pointers[5] = model->portals; counts[5] = model->portal_count;
	sizes[5] = sizeof(model->portals[0]);
	pointers[6] = model->surfaces; counts[6] = model->surface_count;
	sizes[6] = sizeof(model->surfaces[0]);
	pointers[7] = model->affordances; counts[7] = model->affordance_count;
	sizes[7] = sizeof(model->affordances[0]);
	pointers[8] = model->kernels; counts[8] = model->kernel_count;
	sizes[8] = sizeof(model->kernels[0]);
	pointers[9] = model->landmarks; counts[9] = model->landmark_count;
	sizes[9] = sizeof(model->landmarks[0]);
	pointers[10] = model->mechanisms; counts[10] = model->mechanism_count;
	sizes[10] = sizeof(model->mechanisms[0]);
	if (!CodecRange(encoded, encoded_size, 1U, &output[0]) ||
		!CodecRange(encoded_size_out, 1U, sizeof(*encoded_size_out), &output[1]) ||
		!CodecRange(binding, 1U, sizeof(*binding), &input[0]) ||
		!CodecRange(model, 1U, sizeof(*model), &input[1]) ||
		!CodecRange(evidence, 1U, sizeof(*evidence), &input[2]))
		return 0;
	for (index = 0U; index < 11U; index++)
		if (!CodecRange(pointers[index], counts[index], sizes[index],
			&input[index + 3U]))
			return 0;
	if (CodecRangesOverlap(&output[0], &output[1]))
		return 0;
	for (out = 0U; out < 2U; out++)
		for (index = 0U; index < 14U; index++)
			if (CodecRangesOverlap(&output[out], &input[index]))
				return 0;
	return 1;
}

sg_rune_v2_wire_diagnostic_t SG_RuneV2CodecEncode(
	const sg_rune_v2_wire_binding_t *binding,
	const sg_rune_model_t *model,
	const sg_rune_validation_evidence_t *evidence,
	unsigned char *encoded, size_t encoded_capacity,
	size_t *encoded_size_out)
{
	sg_rune_v2_wire_diagnostic_t diagnostic;
	sg_rune_v2_wire_view_t view;
	size_t encoded_size;
	uint64_t offset = SG_RUNE_V2_HEADER_BYTES +
		(uint64_t)SG_RUNE_V2_REQUIRED_SECTION_COUNT *
		SG_RUNE_V2_SECTION_ENTRY_BYTES;
	uint32_t index;

	if (!binding || !encoded || !encoded_size_out || binding->generation == 0U ||
		!SG_RuneV2ContentIdValid(&binding->bsp_identity) ||
		!SG_RuneV2ContentIdValid(&binding->schema_identity))
		return SG_RUNE_V2_WIRE_INVALID_ARGUMENT;
	diagnostic = CodecValidateModel(model, evidence);
	if (diagnostic != SG_RUNE_V2_WIRE_OK)
		return diagnostic;
	diagnostic = CodecComputeSize(model, &encoded_size);
	if (diagnostic != SG_RUNE_V2_WIRE_OK)
		return diagnostic;
	if (encoded_capacity < encoded_size)
		return SG_RUNE_V2_WIRE_BAD_SIZE;
	if (!CodecEncodeRangesDisjoint(binding, model, evidence, encoded,
		encoded_size, encoded_size_out))
		return SG_RUNE_V2_WIRE_INVALID_ARGUMENT;
	memset(encoded, 0, encoded_size);
	SG_RuneV2WirePutU32(encoded + SG_RUNE_V2_HEADER_MAGIC_OFFSET,
		SG_RUNE_V2_MAGIC);
	SG_RuneV2WirePutU16(encoded + SG_RUNE_V2_HEADER_VERSION_OFFSET,
		SG_RUNE_V2_VERSION);
	SG_RuneV2WirePutU16(encoded + SG_RUNE_V2_HEADER_ENDIAN_OFFSET,
		SG_RUNE_V2_ENDIAN_LITTLE);
	SG_RuneV2WirePutU16(encoded + SG_RUNE_V2_HEADER_BYTES_OFFSET,
		SG_RUNE_V2_HEADER_BYTES);
	SG_RuneV2WirePutU16(encoded + SG_RUNE_V2_HEADER_ENTRY_BYTES_OFFSET,
		SG_RUNE_V2_SECTION_ENTRY_BYTES);
	SG_RuneV2WirePutU32(encoded + SG_RUNE_V2_HEADER_SECTION_COUNT_OFFSET,
		SG_RUNE_V2_REQUIRED_SECTION_COUNT);
	SG_RuneV2WirePutU32(encoded + SG_RUNE_V2_HEADER_SCHEMA_REVISION_OFFSET,
		SG_RUNE_V2_SCHEMA_REVISION);
	SG_RuneV2WirePutU64(encoded + SG_RUNE_V2_HEADER_GENERATION_OFFSET,
		binding->generation);
	SG_RuneV2WirePutU64(encoded + SG_RUNE_V2_HEADER_TOTAL_BYTES_OFFSET,
		encoded_size);
	for (index = 0U; index < SG_RUNE_V2_REQUIRED_SECTION_COUNT; index++)
	{
		uint16_t type = (uint16_t)(index + 1U);
		uint32_t count = CodecSectionCount(model, type);
		uint32_t element_bytes = SG_RuneV2WireRecordBytes(type);
		uint64_t bytes = (uint64_t)count * element_bytes;
		unsigned char *entry = encoded + SG_RUNE_V2_HEADER_BYTES +
			(size_t)index * SG_RUNE_V2_SECTION_ENTRY_BYTES;

		if (!CodecAlign(&offset))
			return SG_RUNE_V2_WIRE_BAD_SIZE;
		SG_RuneV2WirePutU16(entry + SG_RUNE_V2_SECTION_TYPE_OFFSET, type);
		SG_RuneV2WirePutU16(entry + SG_RUNE_V2_SECTION_FLAGS_OFFSET,
			SG_RUNE_V2_SECTION_FLAG_REQUIRED);
		SG_RuneV2WirePutU32(entry + SG_RUNE_V2_SECTION_ELEMENT_BYTES_OFFSET,
			element_bytes);
		SG_RuneV2WirePutU32(entry + SG_RUNE_V2_SECTION_COUNT_OFFSET, count);
		SG_RuneV2WirePutU64(entry + SG_RUNE_V2_SECTION_OFFSET_OFFSET, offset);
		SG_RuneV2WirePutU64(entry + SG_RUNE_V2_SECTION_BYTES_OFFSET, bytes);
		CodecEncodeSection(type, encoded + (size_t)offset, binding, model,
			evidence);
		SG_RuneV2WirePutU32(entry + SG_RUNE_V2_SECTION_CRC_OFFSET,
			SG_RuneV2WireCRC32(encoded + (size_t)offset, (size_t)bytes));
		offset += bytes;
	}
	if (!CodecAlign(&offset) || offset != encoded_size)
		return SG_RUNE_V2_WIRE_BAD_SIZE;
	SG_RuneV2WirePutU32(encoded + SG_RUNE_V2_HEADER_PAYLOAD_CRC_OFFSET,
		SG_RuneV2WireCRC32(encoded + SG_RUNE_V2_HEADER_BYTES,
			encoded_size - SG_RUNE_V2_HEADER_BYTES));
	SG_RuneV2WirePutU32(encoded + SG_RUNE_V2_HEADER_CRC_OFFSET,
		SG_RuneV2WireHeaderCRC32(encoded));
	diagnostic = SG_RuneV2WireInspect(encoded, encoded_size, &view);
	if (diagnostic != SG_RUNE_V2_WIRE_OK)
		return diagnostic;
	*encoded_size_out = encoded_size;
	return SG_RUNE_V2_WIRE_OK;
}

static void CodecDecodeModel(const unsigned char *record,
	sg_rune_model_t *model, sg_rune_validation_evidence_t *evidence)
{
	sg_rune_model_identity_t *identity = &model->identity;
	sg_rune_physics_parameters_t *physics = &identity->physics;
	sg_rune_completeness_t *complete = &model->completeness;

	model->version = SG_RuneV2WireGetU16(record +
		SG_RUNE_V2_MODEL_VERSION_OFFSET);
	model->reserved = SG_RuneV2WireGetU16(record +
		SG_RUNE_V2_MODEL_RESERVED_OFFSET);
	model->schema_tag = SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_MODEL_SCHEMA_TAG_OFFSET);
	model->flags = SG_RuneV2WireGetU32(record + SG_RUNE_V2_MODEL_FLAGS_OFFSET);
	identity->bsp_content_id = SG_RuneV2WireGetU64(record +
		SG_RUNE_V2_MODEL_BSP_CONTENT_ID_OFFSET);
	identity->entity_semantics_id = SG_RuneV2WireGetU64(record +
		SG_RUNE_V2_MODEL_ENTITY_SEMANTICS_ID_OFFSET);
	identity->physics_abi_id = SG_RuneV2WireGetU64(record +
		SG_RUNE_V2_MODEL_PHYSICS_ABI_ID_OFFSET);
	identity->source_set_identity = SG_RuneV2WireGetU64(record +
		SG_RUNE_V2_MODEL_SOURCE_SET_ID_OFFSET);
	identity->schema_id = SG_RuneV2WireGetU64(record +
		SG_RUNE_V2_MODEL_SCHEMA_ID_OFFSET);
	identity->producer_identity = SG_RuneV2WireGetU64(record +
		SG_RUNE_V2_MODEL_PRODUCER_ID_OFFSET);
	CodecGetVec3(record + SG_RUNE_V2_MODEL_STANDING_HULL_MINS_OFFSET,
		&identity->standing_hull.mins);
	CodecGetVec3(record + SG_RUNE_V2_MODEL_STANDING_HULL_MAXS_OFFSET,
		&identity->standing_hull.maxs);
	CodecGetVec3(record + SG_RUNE_V2_MODEL_CROUCHING_HULL_MINS_OFFSET,
		&identity->crouching_hull.mins);
	CodecGetVec3(record + SG_RUNE_V2_MODEL_CROUCHING_HULL_MAXS_OFFSET,
		&identity->crouching_hull.maxs);
	physics->gravity = CodecGetF32(record + SG_RUNE_V2_MODEL_GRAVITY_OFFSET);
	physics->ground_acceleration = CodecGetF32(record +
		SG_RUNE_V2_MODEL_GROUND_ACCELERATION_OFFSET);
	physics->air_acceleration = CodecGetF32(record +
		SG_RUNE_V2_MODEL_AIR_ACCELERATION_OFFSET);
	physics->water_acceleration = CodecGetF32(record +
		SG_RUNE_V2_MODEL_WATER_ACCELERATION_OFFSET);
	physics->hook_acceleration = CodecGetF32(record +
		SG_RUNE_V2_MODEL_HOOK_ACCELERATION_OFFSET);
	physics->external_acceleration = CodecGetF32(record +
		SG_RUNE_V2_MODEL_EXTERNAL_ACCELERATION_OFFSET);
	physics->water_drag = CodecGetF32(record +
		SG_RUNE_V2_MODEL_WATER_DRAG_OFFSET);
	physics->max_velocity = CodecGetF32(record +
		SG_RUNE_V2_MODEL_MAX_VELOCITY_OFFSET);
	physics->frame_ms = SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_MODEL_FRAME_MS_OFFSET);
	physics->substep_ms = SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_MODEL_SUBSTEP_MS_OFFSET);
	complete->state = (sg_rune_completeness_state_t)SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_MODEL_COMPLETENESS_STATE_OFFSET);
	complete->reason = (sg_rune_failure_reason_t)SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_MODEL_COMPLETENESS_REASON_OFFSET);
	complete->expected_cells = SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_MODEL_EXPECTED_CELLS_OFFSET);
	complete->expected_portals = SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_MODEL_EXPECTED_PORTALS_OFFSET);
	complete->covered_cells = SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_MODEL_COVERED_CELLS_OFFSET);
	complete->covered_portals = SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_MODEL_COVERED_PORTALS_OFFSET);
	complete->failure_record = SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_MODEL_FAILURE_RECORD_OFFSET);
	evidence->version = SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_MODEL_EVIDENCE_VERSION_OFFSET);
	evidence->reserved = SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_MODEL_EVIDENCE_RESERVED_OFFSET);
	evidence->verifier_identity = SG_RuneV2WireGetU64(record +
		SG_RUNE_V2_MODEL_VERIFIER_ID_OFFSET);
	evidence->bsp_content_id = SG_RuneV2WireGetU64(record +
		SG_RUNE_V2_MODEL_EVIDENCE_BSP_ID_OFFSET);
	evidence->source_set_identity = SG_RuneV2WireGetU64(record +
		SG_RUNE_V2_MODEL_EVIDENCE_SOURCE_SET_ID_OFFSET);
	evidence->fixed_point_identity = SG_RuneV2WireGetU64(record +
		SG_RUNE_V2_MODEL_FIXED_POINT_ID_OFFSET);
	evidence->fixed_point_rounds = SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_MODEL_FIXED_POINT_ROUNDS_OFFSET);
	evidence->proved_cells = SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_MODEL_PROVED_CELLS_OFFSET);
	evidence->proved_portals = SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_MODEL_PROVED_PORTALS_OFFSET);
	evidence->omitted_cells = SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_MODEL_OMITTED_CELLS_OFFSET);
	evidence->omitted_portals = SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_MODEL_OMITTED_PORTALS_OFFSET);
	evidence->invented_portals = SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_MODEL_INVENTED_PORTALS_OFFSET);
	evidence->pending_work = SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_MODEL_PENDING_WORK_OFFSET);
}

static void CodecDecodePlane(const unsigned char *record,
	sg_rune_plane_t *plane)
{
	CodecGetStableId(record + SG_RUNE_V2_RECORD_ID_OFFSET, &plane->id.value);
	CodecGetOrder(record + SG_RUNE_V2_RECORD_ORDER_OFFSET, &plane->order);
	CodecGetVec3(record + SG_RUNE_V2_PLANE_NORMAL_OFFSET, &plane->normal);
	plane->distance = CodecGetF32(record + SG_RUNE_V2_PLANE_DISTANCE_OFFSET);
}

static void CodecDecodePhase(const unsigned char *record,
	sg_rune_phase_basis_t *phase)
{
	CodecGetStableId(record + SG_RUNE_V2_RECORD_ID_OFFSET, &phase->id.value);
	CodecGetOrder(record + SG_RUNE_V2_RECORD_ORDER_OFFSET, &phase->order);
	phase->stance = (sg_rune_stance_t)SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_PHASE_STANCE_OFFSET);
	phase->motion = (sg_rune_motion_t)SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_PHASE_MOTION_OFFSET);
	phase->support = (sg_rune_support_t)SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_PHASE_SUPPORT_OFFSET);
	phase->medium = (sg_rune_medium_t)SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_PHASE_MEDIUM_OFFSET);
	phase->void_relation = (sg_rune_void_relation_t)SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_PHASE_VOID_RELATION_OFFSET);
	phase->reference_frame = (sg_rune_reference_frame_t)SG_RuneV2WireGetU32(
		record + SG_RUNE_V2_PHASE_REFERENCE_FRAME_OFFSET);
	CodecGetStableId(record + SG_RUNE_V2_PHASE_MOVER_OFFSET,
		&phase->mover.value);
	CodecGetInterval3(record + SG_RUNE_V2_PHASE_VELOCITY_OFFSET,
		&phase->velocity);
	CodecGetInterval(record + SG_RUNE_V2_PHASE_ELAPSED_MS_OFFSET,
		&phase->elapsed_ms);
	phase->time_quantum_ms = SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_PHASE_TIME_QUANTUM_OFFSET);
	phase->time_horizon_ms = SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_PHASE_TIME_HORIZON_OFFSET);
}

static void CodecDecodeTransition(const unsigned char *record,
	sg_rune_phase_transition_t *transition)
{
	CodecGetStableId(record + SG_RUNE_V2_RECORD_ID_OFFSET,
		&transition->id.value);
	CodecGetOrder(record + SG_RUNE_V2_RECORD_ORDER_OFFSET, &transition->order);
	CodecGetStableId(record + SG_RUNE_V2_TRANSITION_CELL_OFFSET,
		&transition->cell.value);
	CodecGetStableId(record + SG_RUNE_V2_TRANSITION_SOURCE_PHASE_OFFSET,
		&transition->source_phase.value);
	CodecGetStableId(record + SG_RUNE_V2_TRANSITION_DESTINATION_PHASE_OFFSET,
		&transition->destination_phase.value);
	transition->kind = (sg_rune_phase_transition_kind_t)SG_RuneV2WireGetU32(
		record + SG_RUNE_V2_TRANSITION_KIND_OFFSET);
	CodecGetInterval(record + SG_RUNE_V2_TRANSITION_DURATION_OFFSET,
		&transition->duration_ms);
	transition->flags = SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_TRANSITION_FLAGS_OFFSET);
}

static void CodecDecodeCell(const unsigned char *record, sg_rune_cell_t *cell)
{
	CodecGetStableId(record + SG_RUNE_V2_RECORD_ID_OFFSET, &cell->id.value);
	CodecGetOrder(record + SG_RUNE_V2_RECORD_ORDER_OFFSET, &cell->order);
	CodecGetGeometry(record + SG_RUNE_V2_CELL_GEOMETRY_OFFSET, &cell->geometry);
	CodecGetVec3(record + SG_RUNE_V2_CELL_BOUNDS_MINS_OFFSET,
		&cell->bounds.mins);
	CodecGetVec3(record + SG_RUNE_V2_CELL_BOUNDS_MAXS_OFFSET,
		&cell->bounds.maxs);
	CodecGetSpan(record + SG_RUNE_V2_CELL_BOUNDARY_PLANES_OFFSET,
		&cell->boundary_planes.first, &cell->boundary_planes.count);
	CodecGetSpan(record + SG_RUNE_V2_CELL_PHASES_OFFSET,
		&cell->phases.first, &cell->phases.count);
	CodecGetSpan(record + SG_RUNE_V2_CELL_SURFACES_OFFSET,
		&cell->surfaces.first, &cell->surfaces.count);
	CodecGetSpan(record + SG_RUNE_V2_CELL_AFFORDANCES_OFFSET,
		&cell->affordances.first, &cell->affordances.count);
	CodecGetSpan(record + SG_RUNE_V2_CELL_KERNELS_OFFSET,
		&cell->kernels.first, &cell->kernels.count);
	CodecGetSpan(record + SG_RUNE_V2_CELL_LANDMARKS_OFFSET,
		&cell->landmarks.first, &cell->landmarks.count);
	CodecGetSpan(record + SG_RUNE_V2_CELL_MECHANISMS_OFFSET,
		&cell->mechanisms.first, &cell->mechanisms.count);
	cell->bsp_leaf.index = SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_CELL_BSP_LEAF_OFFSET);
	cell->bsp_area.index = SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_CELL_BSP_AREA_OFFSET);
	cell->bsp_cluster.index = SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_CELL_BSP_CLUSTER_OFFSET);
	cell->contents = SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_CELL_CONTENTS_OFFSET);
	cell->semantics = SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_CELL_SEMANTICS_OFFSET);
}

static void CodecDecodePortal(const unsigned char *record,
	sg_rune_portal_t *portal)
{
	CodecGetStableId(record + SG_RUNE_V2_RECORD_ID_OFFSET, &portal->id.value);
	CodecGetOrder(record + SG_RUNE_V2_RECORD_ORDER_OFFSET, &portal->order);
	CodecGetGeometry(record + SG_RUNE_V2_PORTAL_GEOMETRY_OFFSET,
		&portal->geometry);
	CodecGetStableId(record + SG_RUNE_V2_PORTAL_FROM_CELL_OFFSET,
		&portal->from_cell.value);
	CodecGetStableId(record + SG_RUNE_V2_PORTAL_TO_CELL_OFFSET,
		&portal->to_cell.value);
	CodecGetStableId(record + SG_RUNE_V2_PORTAL_BOUNDARY_PLANE_OFFSET,
		&portal->boundary_plane.value);
	CodecGetSpan(record + SG_RUNE_V2_PORTAL_BOUNDARY_VERTICES_OFFSET,
		&portal->boundary_vertices.first, &portal->boundary_vertices.count);
	CodecGetSpan(record + SG_RUNE_V2_PORTAL_PHASES_OFFSET,
		&portal->phases.first, &portal->phases.count);
	portal->direction = (sg_rune_portal_direction_t)SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_PORTAL_DIRECTION_OFFSET);
	portal->clearance = CodecGetF32(record + SG_RUNE_V2_PORTAL_CLEARANCE_OFFSET);
	portal->contents_from = SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_PORTAL_CONTENTS_FROM_OFFSET);
	portal->contents_to = SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_PORTAL_CONTENTS_TO_OFFSET);
	portal->flags = SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_PORTAL_FLAGS_OFFSET);
}

static void CodecDecodeSurface(const unsigned char *record,
	sg_rune_surface_t *surface)
{
	CodecGetStableId(record + SG_RUNE_V2_RECORD_ID_OFFSET, &surface->id.value);
	CodecGetOrder(record + SG_RUNE_V2_RECORD_ORDER_OFFSET, &surface->order);
	CodecGetGeometry(record + SG_RUNE_V2_SURFACE_GEOMETRY_OFFSET,
		&surface->geometry);
	CodecGetStableId(record + SG_RUNE_V2_SURFACE_OWNER_CELL_OFFSET,
		&surface->owner_cell.value);
	CodecGetStableId(record + SG_RUNE_V2_SURFACE_PLANE_OFFSET,
		&surface->plane.value);
	CodecGetVec3(record + SG_RUNE_V2_SURFACE_NORMAL_OFFSET, &surface->normal);
	surface->contents = SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_SURFACE_CONTENTS_OFFSET);
	surface->semantics = SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_SURFACE_SEMANTICS_OFFSET);
}

static void CodecDecodeAffordance(const unsigned char *record,
	sg_rune_affordance_t *affordance)
{
	CodecGetStableId(record + SG_RUNE_V2_RECORD_ID_OFFSET,
		&affordance->id.value);
	CodecGetOrder(record + SG_RUNE_V2_RECORD_ORDER_OFFSET, &affordance->order);
	CodecGetStableId(record + SG_RUNE_V2_AFFORDANCE_OWNER_CELL_OFFSET,
		&affordance->owner_cell.value);
	CodecGetSpan(record + SG_RUNE_V2_AFFORDANCE_SURFACES_OFFSET,
		&affordance->surfaces.first, &affordance->surfaces.count);
	CodecGetSpan(record + SG_RUNE_V2_AFFORDANCE_PHASES_OFFSET,
		&affordance->phases.first, &affordance->phases.count);
	affordance->kind = (sg_rune_affordance_kind_t)SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_AFFORDANCE_KIND_OFFSET);
	CodecGetInterval(record + SG_RUNE_V2_AFFORDANCE_RANGE_OFFSET,
		&affordance->range);
	affordance->flags = SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_AFFORDANCE_FLAGS_OFFSET);
}

static void CodecDecodeKernel(const unsigned char *record,
	sg_rune_capability_kernel_t *kernel)
{
	sg_rune_kernel_parameters_t *parameters = &kernel->parameters;

	CodecGetStableId(record + SG_RUNE_V2_RECORD_ID_OFFSET, &kernel->id.value);
	CodecGetOrder(record + SG_RUNE_V2_RECORD_ORDER_OFFSET, &kernel->order);
	CodecGetStableId(record + SG_RUNE_V2_KERNEL_SOURCE_CELL_OFFSET,
		&kernel->source_cell.value);
	CodecGetStableId(record + SG_RUNE_V2_KERNEL_DESTINATION_CELL_OFFSET,
		&kernel->destination_cell.value);
	CodecGetStableId(record + SG_RUNE_V2_KERNEL_BOUNDARY_OFFSET,
		&kernel->boundary.value);
	CodecGetStableId(record + SG_RUNE_V2_KERNEL_AFFORDANCE_OFFSET,
		&kernel->affordance.value);
	CodecGetStableId(record + SG_RUNE_V2_KERNEL_MECHANISM_OFFSET,
		&kernel->mechanism.value);
	CodecGetStableId(record + SG_RUNE_V2_KERNEL_SOURCE_PHASE_OFFSET,
		&kernel->source_phase.value);
	CodecGetStableId(record + SG_RUNE_V2_KERNEL_DESTINATION_PHASE_OFFSET,
		&kernel->destination_phase.value);
	CodecGetStableId(record + SG_RUNE_V2_KERNEL_TRANSITION_OFFSET,
		&kernel->transition.value);
	kernel->family = (sg_rune_capability_family_t)SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_KERNEL_FAMILY_OFFSET);
	kernel->cost_law = (sg_rune_cost_law_t)SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_KERNEL_COST_LAW_OFFSET);
	CodecGetInterval3(record + SG_RUNE_V2_KERNEL_DISPLACEMENT_OFFSET,
		&parameters->displacement);
	CodecGetInterval(record + SG_RUNE_V2_KERNEL_DURATION_OFFSET,
		&parameters->duration_ms);
	CodecGetInterval(record + SG_RUNE_V2_KERNEL_SPEED_OFFSET,
		&parameters->speed);
	CodecGetInterval(record + SG_RUNE_V2_KERNEL_ACCELERATION_OFFSET,
		&parameters->acceleration);
	CodecGetInterval(record + SG_RUNE_V2_KERNEL_VERTICAL_ACCELERATION_OFFSET,
		&parameters->vertical_acceleration);
	parameters->gravity = CodecGetF32(record + SG_RUNE_V2_KERNEL_GRAVITY_OFFSET);
	parameters->drag = CodecGetF32(record + SG_RUNE_V2_KERNEL_DRAG_OFFSET);
	parameters->physics_abi_id = SG_RuneV2WireGetU64(record +
		SG_RUNE_V2_KERNEL_PHYSICS_ABI_OFFSET);
	parameters->fixed_latency_ms = SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_KERNEL_FIXED_LATENCY_OFFSET);
	parameters->dwell_ms = SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_KERNEL_DWELL_OFFSET);
	kernel->flags = SG_RuneV2WireGetU32(record + SG_RUNE_V2_KERNEL_FLAGS_OFFSET);
}

static void CodecDecodeLandmark(const unsigned char *record,
	sg_rune_landmark_t *landmark)
{
	CodecGetStableId(record + SG_RUNE_V2_RECORD_ID_OFFSET, &landmark->id.value);
	CodecGetOrder(record + SG_RUNE_V2_RECORD_ORDER_OFFSET, &landmark->order);
	CodecGetGeometry(record + SG_RUNE_V2_LANDMARK_GEOMETRY_OFFSET,
		&landmark->geometry);
	CodecGetStableId(record + SG_RUNE_V2_LANDMARK_CELL_OFFSET,
		&landmark->cell.value);
	CodecGetEntity(record + SG_RUNE_V2_LANDMARK_ENTITY_OFFSET,
		&landmark->entity);
	landmark->kind = (sg_rune_landmark_kind_t)SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_LANDMARK_KIND_OFFSET);
	CodecGetVec3(record + SG_RUNE_V2_LANDMARK_ORIGIN_OFFSET,
		&landmark->origin);
	CodecGetVec3(record + SG_RUNE_V2_LANDMARK_BOUNDS_MINS_OFFSET,
		&landmark->bounds.mins);
	CodecGetVec3(record + SG_RUNE_V2_LANDMARK_BOUNDS_MAXS_OFFSET,
		&landmark->bounds.maxs);
	CodecGetStableId(record + SG_RUNE_V2_LANDMARK_MECHANISM_OFFSET,
		&landmark->mechanism.value);
	CodecGetStableId(record + SG_RUNE_V2_LANDMARK_SURFACE_OFFSET,
		&landmark->surface.value);
	landmark->semantics = SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_LANDMARK_SEMANTICS_OFFSET);
}

static void CodecDecodeMechanism(const unsigned char *record,
	sg_rune_mechanism_t *mechanism)
{
	CodecGetStableId(record + SG_RUNE_V2_RECORD_ID_OFFSET,
		&mechanism->id.value);
	CodecGetOrder(record + SG_RUNE_V2_RECORD_ORDER_OFFSET, &mechanism->order);
	mechanism->kind = (sg_rune_mechanism_kind_t)SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_MECHANISM_KIND_OFFSET);
	CodecGetStableId(record + SG_RUNE_V2_MECHANISM_ENTRY_CELL_OFFSET,
		&mechanism->entry_cell.value);
	CodecGetStableId(record + SG_RUNE_V2_MECHANISM_EXIT_CELL_OFFSET,
		&mechanism->exit_cell.value);
	CodecGetStableId(record + SG_RUNE_V2_MECHANISM_ACTIVATION_LANDMARK_OFFSET,
		&mechanism->activation_landmark.value);
	CodecGetEntity(record + SG_RUNE_V2_MECHANISM_ENTITY_OFFSET,
		&mechanism->entity);
	CodecGetInterval(record + SG_RUNE_V2_MECHANISM_DWELL_OFFSET,
		&mechanism->dwell_ms);
	CodecGetInterval(record + SG_RUNE_V2_MECHANISM_TRAVEL_OFFSET,
		&mechanism->travel_ms);
	CodecGetSpan(record + SG_RUNE_V2_MECHANISM_TOPOLOGY_OFFSET,
		&mechanism->topology.first, &mechanism->topology.count);
	mechanism->flags = SG_RuneV2WireGetU32(record +
		SG_RUNE_V2_MECHANISM_FLAGS_OFFSET);
}

static int CodecStorageFits(const sg_rune_v2_codec_storage_t *storage,
	const sg_rune_v2_wire_view_t *view)
{
#define FITS(member, capacity, section_type) \
	(view->section[(section_type) - 1U].count == 0U || \
	 (storage->member && storage->capacity >= \
	  view->section[(section_type) - 1U].count))
	return storage &&
		FITS(planes, plane_capacity, SG_RUNE_V2_SECTION_PLANES) &&
		FITS(portal_vertices, portal_vertex_capacity,
			SG_RUNE_V2_SECTION_PORTAL_VERTICES) &&
		FITS(phases, phase_capacity, SG_RUNE_V2_SECTION_PHASES) &&
		FITS(phase_transitions, phase_transition_capacity,
			SG_RUNE_V2_SECTION_PHASE_TRANSITIONS) &&
		FITS(cells, cell_capacity, SG_RUNE_V2_SECTION_CELLS) &&
		FITS(portals, portal_capacity, SG_RUNE_V2_SECTION_PORTALS) &&
		FITS(surfaces, surface_capacity, SG_RUNE_V2_SECTION_SURFACES) &&
		FITS(affordances, affordance_capacity, SG_RUNE_V2_SECTION_AFFORDANCES) &&
		FITS(kernels, kernel_capacity, SG_RUNE_V2_SECTION_KERNELS) &&
		FITS(landmarks, landmark_capacity, SG_RUNE_V2_SECTION_LANDMARKS) &&
		FITS(mechanisms, mechanism_capacity, SG_RUNE_V2_SECTION_MECHANISMS);
#undef FITS
}

static int CodecDecodeRangesDisjoint(const unsigned char *encoded,
	size_t encoded_size, const sg_rune_v2_codec_storage_t *scratch,
	const sg_rune_v2_codec_storage_t *published,
	int *accepted_out,
	sg_rune_v2_wire_binding_t *binding_out, sg_rune_model_t *model_out,
	sg_rune_validation_evidence_t *evidence_out)
{
	codec_range_t ranges[29];
	const void *pointers[22];
	size_t capacities[22];
	size_t sizes[11];
	size_t index;
	size_t other;

	pointers[0] = scratch->planes; capacities[0] = scratch->plane_capacity;
	sizes[0] = sizeof(scratch->planes[0]);
	pointers[1] = scratch->portal_vertices;
	capacities[1] = scratch->portal_vertex_capacity;
	sizes[1] = sizeof(scratch->portal_vertices[0]);
	pointers[2] = scratch->phases; capacities[2] = scratch->phase_capacity;
	sizes[2] = sizeof(scratch->phases[0]);
	pointers[3] = scratch->phase_transitions;
	capacities[3] = scratch->phase_transition_capacity;
	sizes[3] = sizeof(scratch->phase_transitions[0]);
	pointers[4] = scratch->cells; capacities[4] = scratch->cell_capacity;
	sizes[4] = sizeof(scratch->cells[0]);
	pointers[5] = scratch->portals; capacities[5] = scratch->portal_capacity;
	sizes[5] = sizeof(scratch->portals[0]);
	pointers[6] = scratch->surfaces; capacities[6] = scratch->surface_capacity;
	sizes[6] = sizeof(scratch->surfaces[0]);
	pointers[7] = scratch->affordances;
	capacities[7] = scratch->affordance_capacity;
	sizes[7] = sizeof(scratch->affordances[0]);
	pointers[8] = scratch->kernels; capacities[8] = scratch->kernel_capacity;
	sizes[8] = sizeof(scratch->kernels[0]);
	pointers[9] = scratch->landmarks; capacities[9] = scratch->landmark_capacity;
	sizes[9] = sizeof(scratch->landmarks[0]);
	pointers[10] = scratch->mechanisms;
	capacities[10] = scratch->mechanism_capacity;
	sizes[10] = sizeof(scratch->mechanisms[0]);
	pointers[11] = published->planes;
	capacities[11] = published->plane_capacity;
	pointers[12] = published->portal_vertices;
	capacities[12] = published->portal_vertex_capacity;
	pointers[13] = published->phases;
	capacities[13] = published->phase_capacity;
	pointers[14] = published->phase_transitions;
	capacities[14] = published->phase_transition_capacity;
	pointers[15] = published->cells;
	capacities[15] = published->cell_capacity;
	pointers[16] = published->portals;
	capacities[16] = published->portal_capacity;
	pointers[17] = published->surfaces;
	capacities[17] = published->surface_capacity;
	pointers[18] = published->affordances;
	capacities[18] = published->affordance_capacity;
	pointers[19] = published->kernels;
	capacities[19] = published->kernel_capacity;
	pointers[20] = published->landmarks;
	capacities[20] = published->landmark_capacity;
	pointers[21] = published->mechanisms;
	capacities[21] = published->mechanism_capacity;
	if (!CodecRange(encoded, encoded_size, 1U, &ranges[0]) ||
		!CodecRange(scratch, 1U, sizeof(*scratch), &ranges[1]) ||
		!CodecRange(published, 1U, sizeof(*published), &ranges[2]) ||
		!CodecRange(accepted_out, 1U, sizeof(*accepted_out), &ranges[3]) ||
		!CodecRange(binding_out, 1U, sizeof(*binding_out), &ranges[4]) ||
		!CodecRange(model_out, 1U, sizeof(*model_out), &ranges[5]) ||
		!CodecRange(evidence_out, 1U, sizeof(*evidence_out), &ranges[6]))
		return 0;
	for (index = 0U; index < 22U; index++)
		if (!CodecRange(pointers[index], capacities[index],
			sizes[index % 11U], &ranges[index + 7U]))
			return 0;
	for (index = 0U; index < 29U; index++)
		for (other = index + 1U; other < 29U; other++)
			if (CodecRangesOverlap(&ranges[index], &ranges[other]))
				return 0;
	return 1;
}

static void CodecDecodeArrays(const unsigned char *encoded,
	const sg_rune_v2_wire_view_t *view,
	const sg_rune_v2_codec_storage_t *storage)
{
	uint32_t index;
	const unsigned char *record;

#define SECTION_DATA(section_type) \
	SG_RuneV2WireSectionData(encoded, \
		&view->section[(section_type) - 1U])
	record = SECTION_DATA(SG_RUNE_V2_SECTION_PLANES);
	for (index = 0U; index < view->section[SG_RUNE_V2_SECTION_PLANES - 1U].count;
		index++, record += SG_RUNE_V2_PLANE_RECORD_BYTES)
	{
		memset(&storage->planes[index], 0, sizeof(storage->planes[index]));
		CodecDecodePlane(record, &storage->planes[index]);
	}
	record = SECTION_DATA(SG_RUNE_V2_SECTION_PORTAL_VERTICES);
	for (index = 0U;
		index < view->section[SG_RUNE_V2_SECTION_PORTAL_VERTICES - 1U].count;
		index++, record += SG_RUNE_V2_PORTAL_VERTEX_RECORD_BYTES)
	{
		memset(&storage->portal_vertices[index], 0,
			sizeof(storage->portal_vertices[index]));
		CodecGetVec3(record, &storage->portal_vertices[index]);
	}
	record = SECTION_DATA(SG_RUNE_V2_SECTION_PHASES);
	for (index = 0U; index < view->section[SG_RUNE_V2_SECTION_PHASES - 1U].count;
		index++, record += SG_RUNE_V2_PHASE_RECORD_BYTES)
	{
		memset(&storage->phases[index], 0, sizeof(storage->phases[index]));
		CodecDecodePhase(record, &storage->phases[index]);
	}
	record = SECTION_DATA(SG_RUNE_V2_SECTION_PHASE_TRANSITIONS);
	for (index = 0U;
		index < view->section[SG_RUNE_V2_SECTION_PHASE_TRANSITIONS - 1U].count;
		index++, record += SG_RUNE_V2_PHASE_TRANSITION_RECORD_BYTES)
	{
		memset(&storage->phase_transitions[index], 0,
			sizeof(storage->phase_transitions[index]));
		CodecDecodeTransition(record, &storage->phase_transitions[index]);
	}
	record = SECTION_DATA(SG_RUNE_V2_SECTION_CELLS);
	for (index = 0U; index < view->section[SG_RUNE_V2_SECTION_CELLS - 1U].count;
		index++, record += SG_RUNE_V2_CELL_RECORD_BYTES)
	{
		memset(&storage->cells[index], 0, sizeof(storage->cells[index]));
		CodecDecodeCell(record, &storage->cells[index]);
	}
	record = SECTION_DATA(SG_RUNE_V2_SECTION_PORTALS);
	for (index = 0U; index < view->section[SG_RUNE_V2_SECTION_PORTALS - 1U].count;
		index++, record += SG_RUNE_V2_PORTAL_RECORD_BYTES)
	{
		memset(&storage->portals[index], 0, sizeof(storage->portals[index]));
		CodecDecodePortal(record, &storage->portals[index]);
	}
	record = SECTION_DATA(SG_RUNE_V2_SECTION_SURFACES);
	for (index = 0U; index < view->section[SG_RUNE_V2_SECTION_SURFACES - 1U].count;
		index++, record += SG_RUNE_V2_SURFACE_RECORD_BYTES)
	{
		memset(&storage->surfaces[index], 0, sizeof(storage->surfaces[index]));
		CodecDecodeSurface(record, &storage->surfaces[index]);
	}
	record = SECTION_DATA(SG_RUNE_V2_SECTION_AFFORDANCES);
	for (index = 0U;
		index < view->section[SG_RUNE_V2_SECTION_AFFORDANCES - 1U].count;
		index++, record += SG_RUNE_V2_AFFORDANCE_RECORD_BYTES)
	{
		memset(&storage->affordances[index], 0,
			sizeof(storage->affordances[index]));
		CodecDecodeAffordance(record, &storage->affordances[index]);
	}
	record = SECTION_DATA(SG_RUNE_V2_SECTION_KERNELS);
	for (index = 0U; index < view->section[SG_RUNE_V2_SECTION_KERNELS - 1U].count;
		index++, record += SG_RUNE_V2_KERNEL_RECORD_BYTES)
	{
		memset(&storage->kernels[index], 0, sizeof(storage->kernels[index]));
		CodecDecodeKernel(record, &storage->kernels[index]);
	}
	record = SECTION_DATA(SG_RUNE_V2_SECTION_LANDMARKS);
	for (index = 0U;
		index < view->section[SG_RUNE_V2_SECTION_LANDMARKS - 1U].count;
		index++, record += SG_RUNE_V2_LANDMARK_RECORD_BYTES)
	{
		memset(&storage->landmarks[index], 0,
			sizeof(storage->landmarks[index]));
		CodecDecodeLandmark(record, &storage->landmarks[index]);
	}
	record = SECTION_DATA(SG_RUNE_V2_SECTION_MECHANISMS);
	for (index = 0U;
		index < view->section[SG_RUNE_V2_SECTION_MECHANISMS - 1U].count;
		index++, record += SG_RUNE_V2_MECHANISM_RECORD_BYTES)
	{
		memset(&storage->mechanisms[index], 0,
			sizeof(storage->mechanisms[index]));
		CodecDecodeMechanism(record, &storage->mechanisms[index]);
	}
#undef SECTION_DATA
}

static void CodecPublishArrays(const sg_rune_v2_codec_storage_t *scratch,
	const sg_rune_v2_codec_storage_t *published,
	const sg_rune_v2_wire_view_t *view)
{
#define PUBLISH(member, section_type) do { \
	uint32_t count_ = view->section[(section_type) - 1U].count; \
	if (count_ != 0U) \
		memcpy(published->member, scratch->member, \
			(size_t)count_ * sizeof(published->member[0])); \
} while (0)
	PUBLISH(planes, SG_RUNE_V2_SECTION_PLANES);
	PUBLISH(portal_vertices, SG_RUNE_V2_SECTION_PORTAL_VERTICES);
	PUBLISH(phases, SG_RUNE_V2_SECTION_PHASES);
	PUBLISH(phase_transitions, SG_RUNE_V2_SECTION_PHASE_TRANSITIONS);
	PUBLISH(cells, SG_RUNE_V2_SECTION_CELLS);
	PUBLISH(portals, SG_RUNE_V2_SECTION_PORTALS);
	PUBLISH(surfaces, SG_RUNE_V2_SECTION_SURFACES);
	PUBLISH(affordances, SG_RUNE_V2_SECTION_AFFORDANCES);
	PUBLISH(kernels, SG_RUNE_V2_SECTION_KERNELS);
	PUBLISH(landmarks, SG_RUNE_V2_SECTION_LANDMARKS);
	PUBLISH(mechanisms, SG_RUNE_V2_SECTION_MECHANISMS);
#undef PUBLISH
}

static void CodecAttachArrays(sg_rune_model_t *model,
	const sg_rune_v2_codec_storage_t *storage,
	const sg_rune_v2_wire_view_t *view)
{
	model->planes = storage->planes;
	model->plane_count = view->section[SG_RUNE_V2_SECTION_PLANES - 1U].count;
	model->portal_vertices = storage->portal_vertices;
	model->portal_vertex_count =
		view->section[SG_RUNE_V2_SECTION_PORTAL_VERTICES - 1U].count;
	model->phases = storage->phases;
	model->phase_count = view->section[SG_RUNE_V2_SECTION_PHASES - 1U].count;
	model->phase_transitions = storage->phase_transitions;
	model->phase_transition_count =
		view->section[SG_RUNE_V2_SECTION_PHASE_TRANSITIONS - 1U].count;
	model->cells = storage->cells;
	model->cell_count = view->section[SG_RUNE_V2_SECTION_CELLS - 1U].count;
	model->portals = storage->portals;
	model->portal_count = view->section[SG_RUNE_V2_SECTION_PORTALS - 1U].count;
	model->surfaces = storage->surfaces;
	model->surface_count = view->section[SG_RUNE_V2_SECTION_SURFACES - 1U].count;
	model->affordances = storage->affordances;
	model->affordance_count =
		view->section[SG_RUNE_V2_SECTION_AFFORDANCES - 1U].count;
	model->kernels = storage->kernels;
	model->kernel_count = view->section[SG_RUNE_V2_SECTION_KERNELS - 1U].count;
	model->landmarks = storage->landmarks;
	model->landmark_count =
		view->section[SG_RUNE_V2_SECTION_LANDMARKS - 1U].count;
	model->mechanisms = storage->mechanisms;
	model->mechanism_count =
		view->section[SG_RUNE_V2_SECTION_MECHANISMS - 1U].count;
}

static sg_rune_v2_wire_diagnostic_t CodecDecodeValidated(
	const unsigned char *encoded, size_t encoded_size,
	const sg_rune_v2_codec_storage_t *scratch,
	const sg_rune_v2_codec_storage_t *published,
	sg_rune_v2_codec_candidate_accept_fn accept_candidate,
	void *context, int *accepted_out,
	sg_rune_v2_wire_binding_t *binding_out,
	sg_rune_model_t *model_out,
	sg_rune_validation_evidence_t *evidence_out)
{
	sg_rune_v2_wire_view_t view;
	sg_rune_model_t model;
	sg_rune_validation_evidence_t evidence;
	sg_rune_v2_wire_diagnostic_t diagnostic;
	const unsigned char *model_record;

	if (!encoded || !scratch || !published || !accepted_out || !binding_out ||
		!model_out || !evidence_out)
		return SG_RUNE_V2_WIRE_INVALID_ARGUMENT;
	diagnostic = SG_RuneV2WireInspect(encoded, encoded_size, &view);
	if (diagnostic != SG_RUNE_V2_WIRE_OK)
		return diagnostic;
	if (!CodecStorageFits(scratch, &view) || !CodecStorageFits(published, &view))
		return SG_RUNE_V2_WIRE_BAD_SIZE;
	if (!CodecDecodeRangesDisjoint(encoded, encoded_size, scratch, published,
		accepted_out, binding_out, model_out, evidence_out))
		return SG_RUNE_V2_WIRE_INVALID_ARGUMENT;
	memset(&model, 0, sizeof(model));
	memset(&evidence, 0, sizeof(evidence));
	model_record = SG_RuneV2WireSectionData(encoded,
		&view.section[SG_RUNE_V2_SECTION_MODEL - 1U]);
	CodecDecodeModel(model_record, &model, &evidence);
	CodecDecodeArrays(encoded, &view, scratch);
	CodecAttachArrays(&model, scratch, &view);
	diagnostic = CodecFailureDiagnostic(SG_RuneModelValidate(&model, &evidence));
	if (diagnostic != SG_RUNE_V2_WIRE_OK)
		return diagnostic;
	if (accept_candidate &&
		!accept_candidate(&view.binding, &model, &evidence, context))
	{
		*accepted_out = 0;
		return SG_RUNE_V2_WIRE_OK;
	}
	CodecPublishArrays(scratch, published, &view);
	CodecAttachArrays(&model, published, &view);
	*binding_out = view.binding;
	*model_out = model;
	*evidence_out = evidence;
	*accepted_out = 1;
	return SG_RUNE_V2_WIRE_OK;
}

sg_rune_v2_wire_diagnostic_t SG_RuneV2CodecDecode(
	const unsigned char *encoded, size_t encoded_size,
	const sg_rune_v2_codec_storage_t *scratch,
	const sg_rune_v2_codec_storage_t *published,
	sg_rune_v2_wire_binding_t *binding_out,
	sg_rune_model_t *model_out,
	sg_rune_validation_evidence_t *evidence_out)
{
	int accepted;

	return CodecDecodeValidated(encoded, encoded_size, scratch, published,
		NULL, NULL, &accepted, binding_out, model_out, evidence_out);
}

sg_rune_v2_wire_diagnostic_t SG_RuneV2CodecDecodeValidated(
	const unsigned char *encoded, size_t encoded_size,
	const sg_rune_v2_codec_storage_t *scratch,
	const sg_rune_v2_codec_storage_t *published,
	sg_rune_v2_codec_candidate_accept_fn accept_candidate,
	void *context, int *accepted_out,
	sg_rune_v2_wire_binding_t *binding_out,
	sg_rune_model_t *model_out,
	sg_rune_validation_evidence_t *evidence_out)
{
	if (!accept_candidate)
		return SG_RUNE_V2_WIRE_INVALID_ARGUMENT;
	return CodecDecodeValidated(encoded, encoded_size, scratch, published,
		accept_candidate, context, accepted_out, binding_out, model_out,
		evidence_out);
}
