#include "sg_rune_compact_pmove_control_wire.h"

#include <string.h>

#define SECTION_HEADER_BYTES 48U
#define IDENTITY_BYTES 120U
#define REGION_BYTES 72U
#define POTENTIAL_BYTES 24U
#define CERTIFICATE_BYTES 64U
#define TRANSITION_BYTES 24U
#define FIRST_SLICE_BYTES (SECTION_HEADER_BYTES + IDENTITY_BYTES + \
	REGION_BYTES + POTENTIAL_BYTES + CERTIFICATE_BYTES + \
	2U * TRANSITION_BYTES)

static void SetError(sg_rune_pmove_control_error_t *error_out,
	sg_rune_pmove_control_error_t error)
{
	if (error_out)
		*error_out = error;
}

static void PutU32(uint8_t **cursor, uint32_t value)
{
	(*cursor)[0] = (uint8_t)value;
	(*cursor)[1] = (uint8_t)(value >> 8U);
	(*cursor)[2] = (uint8_t)(value >> 16U);
	(*cursor)[3] = (uint8_t)(value >> 24U);
	*cursor += 4;
}

static void PutU64(uint8_t **cursor, uint64_t value)
{
	PutU32(cursor, (uint32_t)value);
	PutU32(cursor, (uint32_t)(value >> 32U));
}

static uint32_t GetU32(const uint8_t **cursor)
{
	uint32_t value = (uint32_t)(*cursor)[0] |
		((uint32_t)(*cursor)[1] << 8U) |
		((uint32_t)(*cursor)[2] << 16U) |
		((uint32_t)(*cursor)[3] << 24U);

	*cursor += 4;
	return value;
}

static uint64_t GetU64(const uint8_t **cursor)
{
	uint64_t low = GetU32(cursor);
	uint64_t high = GetU32(cursor);

	return low | (high << 32U);
}

static void PutIdentity(uint8_t **cursor,
	const sg_rune_pmove_control_identity_t *identity)
{
	uint32_t index;

	PutU32(cursor, identity->version);
	PutU32(cursor, identity->reserved);
	PutU64(cursor, identity->compact_artifact_id);
	PutU64(cursor, identity->bsp_content_id);
	for (index = 0U; index < SG_RUNE_PMOVE_CONTROL_BSP_IDENTITY_BYTES; index++)
		*(*cursor)++ = identity->bsp_identity[index];
	PutU64(cursor, identity->physics_abi_id);
	PutU64(cursor, identity->collision_law_id);
	PutU64(cursor, identity->pmove_law_id);
	PutU64(cursor, identity->pmove_behavior_id);
	PutU32(cursor, identity->frame_ms);
	PutU32(cursor, identity->substep_ms);
	PutU32(cursor, identity->substep_count);
	PutU32(cursor, identity->reserved_2);
	PutU64(cursor, identity->frame_cost_units);
	PutU64(cursor, identity->source_reserve_units);
}

static void GetIdentity(const uint8_t **cursor,
	sg_rune_pmove_control_identity_t *identity)
{
	uint32_t index;

	identity->version = GetU32(cursor);
	identity->reserved = GetU32(cursor);
	identity->compact_artifact_id = GetU64(cursor);
	identity->bsp_content_id = GetU64(cursor);
	for (index = 0U; index < SG_RUNE_PMOVE_CONTROL_BSP_IDENTITY_BYTES; index++)
		identity->bsp_identity[index] = *(*cursor)++;
	identity->physics_abi_id = GetU64(cursor);
	identity->collision_law_id = GetU64(cursor);
	identity->pmove_law_id = GetU64(cursor);
	identity->pmove_behavior_id = GetU64(cursor);
	identity->frame_ms = GetU32(cursor);
	identity->substep_ms = GetU32(cursor);
	identity->substep_count = GetU32(cursor);
	identity->reserved_2 = GetU32(cursor);
	identity->frame_cost_units = GetU64(cursor);
	identity->source_reserve_units = GetU64(cursor);
}

static void PutRegion(uint8_t **cursor,
	const sg_rune_pmove_control_region_t *region)
{
	PutU32(cursor, region->id);
	PutU32(cursor, region->cell);
	PutU32(cursor, region->target_portal);
	PutU32(cursor, region->target_cell);
	PutU32(cursor, region->potential);
	PutU32(cursor, region->certificate);
	PutU32(cursor, region->first_transition);
	PutU32(cursor, region->transition_count);
	PutU32(cursor, (uint32_t)region->longitudinal_min_q8);
	PutU32(cursor, (uint32_t)region->longitudinal_max_q8);
	PutU32(cursor, (uint32_t)region->lateral_min_q8);
	PutU32(cursor, (uint32_t)region->lateral_max_q8);
	PutU32(cursor, (uint32_t)region->velocity_forward_min_q8);
	PutU32(cursor, (uint32_t)region->velocity_forward_max_q8);
	PutU32(cursor, (uint32_t)region->velocity_lateral_min_q8);
	PutU32(cursor, (uint32_t)region->velocity_lateral_max_q8);
	PutU32(cursor, (uint32_t)region->portal_q8);
	PutU32(cursor, (uint32_t)region->lateral_center_q8);
}

static void GetRegion(const uint8_t **cursor,
	sg_rune_pmove_control_region_t *region)
{
	region->id = GetU32(cursor);
	region->cell = GetU32(cursor);
	region->target_portal = GetU32(cursor);
	region->target_cell = GetU32(cursor);
	region->potential = GetU32(cursor);
	region->certificate = GetU32(cursor);
	region->first_transition = GetU32(cursor);
	region->transition_count = GetU32(cursor);
	region->longitudinal_min_q8 = (int32_t)GetU32(cursor);
	region->longitudinal_max_q8 = (int32_t)GetU32(cursor);
	region->lateral_min_q8 = (int32_t)GetU32(cursor);
	region->lateral_max_q8 = (int32_t)GetU32(cursor);
	region->velocity_forward_min_q8 = (int32_t)GetU32(cursor);
	region->velocity_forward_max_q8 = (int32_t)GetU32(cursor);
	region->velocity_lateral_min_q8 = (int32_t)GetU32(cursor);
	region->velocity_lateral_max_q8 = (int32_t)GetU32(cursor);
	region->portal_q8 = (int32_t)GetU32(cursor);
	region->lateral_center_q8 = (int32_t)GetU32(cursor);
}

static void PutPotential(uint8_t **cursor,
	const sg_rune_pmove_control_potential_t *potential)
{
	PutU32(cursor, potential->id);
	PutU32(cursor, potential->divisor);
	PutU32(cursor, potential->distance_weight);
	PutU32(cursor, potential->reversal_velocity_weight);
	PutU32(cursor, potential->lateral_position_weight);
	PutU32(cursor, potential->lateral_velocity_weight);
}

static void GetPotential(const uint8_t **cursor,
	sg_rune_pmove_control_potential_t *potential)
{
	potential->id = GetU32(cursor);
	potential->divisor = GetU32(cursor);
	potential->distance_weight = GetU32(cursor);
	potential->reversal_velocity_weight = GetU32(cursor);
	potential->lateral_position_weight = GetU32(cursor);
	potential->lateral_velocity_weight = GetU32(cursor);
}

static void PutCertificate(uint8_t **cursor,
	const sg_rune_pmove_control_certificate_t *certificate)
{
	PutU32(cursor, certificate->id);
	PutU32(cursor, certificate->region);
	PutU32(cursor, (uint32_t)certificate->hull_half_width_q8);
	PutU32(cursor, (uint32_t)certificate->wall_clearance_q8);
	PutU32(cursor, (uint32_t)certificate->static_support_z_q8);
	PutU32(cursor, (uint32_t)certificate->maximum_velocity_q8);
	PutU32(cursor, certificate->friction_keep_numerator);
	PutU32(cursor, certificate->friction_keep_denominator);
	PutU32(cursor, (uint32_t)certificate->acceleration_per_substep_q8);
	PutU32(cursor, (uint32_t)certificate->wish_speed_q8);
	PutU64(cursor, certificate->minimum_descent_units);
	PutU32(cursor, certificate->dry);
	PutU32(cursor, certificate->static_world_support);
	PutU32(cursor, 0U);
	PutU32(cursor, 0U);
}

static void GetCertificate(const uint8_t **cursor,
	sg_rune_pmove_control_certificate_t *certificate, int *reserved_ok)
{
	certificate->id = GetU32(cursor);
	certificate->region = GetU32(cursor);
	certificate->hull_half_width_q8 = (int32_t)GetU32(cursor);
	certificate->wall_clearance_q8 = (int32_t)GetU32(cursor);
	certificate->static_support_z_q8 = (int32_t)GetU32(cursor);
	certificate->maximum_velocity_q8 = (int32_t)GetU32(cursor);
	certificate->friction_keep_numerator = GetU32(cursor);
	certificate->friction_keep_denominator = GetU32(cursor);
	certificate->acceleration_per_substep_q8 = (int32_t)GetU32(cursor);
	certificate->wish_speed_q8 = (int32_t)GetU32(cursor);
	certificate->minimum_descent_units = GetU64(cursor);
	certificate->dry = GetU32(cursor);
	certificate->static_world_support = GetU32(cursor);
	if (GetU32(cursor) != 0U || GetU32(cursor) != 0U)
		*reserved_ok = 0;
}

static void PutTransition(uint8_t **cursor,
	const sg_rune_pmove_control_transition_t *transition)
{
	PutU32(cursor, transition->source_region);
	PutU32(cursor, transition->kind);
	PutU32(cursor, transition->target_region);
	PutU32(cursor, transition->target_cell);
	PutU32(cursor, transition->portal);
	PutU32(cursor, transition->certificate);
}

static void GetTransition(const uint8_t **cursor,
	sg_rune_pmove_control_transition_t *transition)
{
	transition->source_region = GetU32(cursor);
	transition->kind = GetU32(cursor);
	transition->target_region = GetU32(cursor);
	transition->target_cell = GetU32(cursor);
	transition->portal = GetU32(cursor);
	transition->certificate = GetU32(cursor);
}

int SG_RunePmoveControlSectionMeasure(
	const sg_rune_pmove_control_model_t *model, size_t *size_out,
	sg_rune_pmove_control_error_t *error_out)
{
	if (size_out)
		*size_out = 0U;
	if (!size_out || !SG_RunePmoveControlValidate(model, error_out) ||
		model->region_count != 1U || model->potential_count != 1U ||
		model->certificate_count != 1U || model->transition_count != 2U)
		return 0;
	*size_out = FIRST_SLICE_BYTES;
	return 1;
}

int SG_RunePmoveControlSectionEncode(
	const sg_rune_pmove_control_model_t *model, void *bytes, size_t size,
	sg_rune_pmove_control_error_t *error_out)
{
	uint8_t *cursor = bytes;
	size_t measured;

	SetError(error_out, SG_RUNE_PMOVE_CONTROL_ERROR_NONE);
	if (!bytes || !SG_RunePmoveControlSectionMeasure(model, &measured, error_out) ||
		size != measured)
	{
		if (error_out && *error_out == SG_RUNE_PMOVE_CONTROL_ERROR_NONE)
			*error_out = SG_RUNE_PMOVE_CONTROL_ERROR_INVALID_ARGUMENT;
		return 0;
	}
	memset(bytes, 0, size);
	PutU32(&cursor, SG_RUNE_PMOVE_CONTROL_SECTION_TAG);
	PutU32(&cursor, SG_RUNE_PMOVE_CONTROL_VERSION);
	PutU32(&cursor, 0U);
	PutU32(&cursor, 0U);
	PutU64(&cursor, (uint64_t)(size - SECTION_HEADER_BYTES));
	PutU32(&cursor, model->region_count);
	PutU32(&cursor, model->potential_count);
	PutU32(&cursor, model->certificate_count);
	PutU32(&cursor, model->transition_count);
	PutU32(&cursor, 1U);
	PutU32(&cursor, 0U);
	PutIdentity(&cursor, &model->identity);
	PutRegion(&cursor, &model->regions[0]);
	PutPotential(&cursor, &model->potentials[0]);
	PutCertificate(&cursor, &model->certificates[0]);
	PutTransition(&cursor, &model->transitions[0]);
	PutTransition(&cursor, &model->transitions[1]);
	return (size_t)(cursor - (uint8_t *)bytes) == size;
}

int SG_RunePmoveControlSectionDecode(const void *bytes, size_t size,
	sg_rune_pmove_control_storage_t *storage,
	sg_rune_pmove_control_model_t *model_out,
	sg_rune_pmove_control_error_t *error_out)
{
	const uint8_t *cursor = bytes;
	int reserved_ok = 1;
	uint32_t tag;
	uint32_t version;
	uint64_t payload;

	SetError(error_out, SG_RUNE_PMOVE_CONTROL_ERROR_NONE);
	if (!bytes || !storage || !model_out || size != FIRST_SLICE_BYTES)
	{
		SetError(error_out, SG_RUNE_PMOVE_CONTROL_ERROR_INVALID_ARGUMENT);
		return 0;
	}
	memset(storage, 0, sizeof(*storage));
	memset(model_out, 0, sizeof(*model_out));
	tag = GetU32(&cursor);
	version = GetU32(&cursor);
	if (GetU32(&cursor) != 0U || GetU32(&cursor) != 0U)
		reserved_ok = 0;
	payload = GetU64(&cursor);
	if (tag != SG_RUNE_PMOVE_CONTROL_SECTION_TAG ||
		version != SG_RUNE_PMOVE_CONTROL_VERSION ||
		payload != size - SECTION_HEADER_BYTES || GetU32(&cursor) != 1U ||
		GetU32(&cursor) != 1U || GetU32(&cursor) != 1U ||
		GetU32(&cursor) != 2U || GetU32(&cursor) != 1U ||
		GetU32(&cursor) != 0U || !reserved_ok)
	{
		SetError(error_out, SG_RUNE_PMOVE_CONTROL_ERROR_INVALID_DOMAIN);
		return 0;
	}
	GetIdentity(&cursor, &model_out->identity);
	GetRegion(&cursor, &storage->region);
	GetPotential(&cursor, &storage->potential);
	GetCertificate(&cursor, &storage->certificate, &reserved_ok);
	GetTransition(&cursor, &storage->transitions[0]);
	GetTransition(&cursor, &storage->transitions[1]);
	if (!reserved_ok || (size_t)(cursor - (const uint8_t *)bytes) != size)
	{
		SetError(error_out, SG_RUNE_PMOVE_CONTROL_ERROR_INVALID_DOMAIN);
		return 0;
	}
	model_out->regions = &storage->region;
	model_out->region_count = 1U;
	model_out->potentials = &storage->potential;
	model_out->potential_count = 1U;
	model_out->certificates = &storage->certificate;
	model_out->certificate_count = 1U;
	model_out->transitions = storage->transitions;
	model_out->transition_count = 2U;
	return SG_RunePmoveControlValidate(model_out, error_out);
}

int SG_RunePmoveControlSectionInspect(const void *bytes, size_t size,
	sg_rune_pmove_control_identity_t *identity_out,
	sg_rune_pmove_control_error_t *error_out)
{
	sg_rune_pmove_control_storage_t storage;
	sg_rune_pmove_control_model_t model;

	if (identity_out)
		memset(identity_out, 0, sizeof(*identity_out));
	if (!identity_out || !SG_RunePmoveControlSectionDecode(bytes, size,
		&storage, &model, error_out))
		return 0;
	*identity_out = model.identity;
	return 1;
}
