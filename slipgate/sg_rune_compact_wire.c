#include "sg_rune_compact_wire.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define SG_WIRE_HEADER_FIXED_SIZE UINT32_C(48)
#define SG_WIRE_DESCRIPTOR_SIZE UINT32_C(24)
#define SG_WIRE_HEADER_SIZE \
	(SG_WIRE_HEADER_FIXED_SIZE + \
	 (uint32_t)SG_RUNE_COMPACT_WIRE_SECTION_COUNT * SG_WIRE_DESCRIPTOR_SIZE)
#define SG_WIRE_CHECKSUM_OFFSET UINT32_C(24)
#define SG_WIRE_ALIGNMENT UINT64_C(8)

typedef struct sg_wire_spec_s
{
	uint32_t wire_size;
	uint32_t limit;
} sg_wire_spec_t;

typedef struct sg_wire_desc_s
{
	uint32_t count;
	uint64_t offset;
} sg_wire_desc_t;

typedef struct sg_wire_source_s
{
	const void *arrays[SG_RUNE_COMPACT_WIRE_SECTION_COUNT];
	uint32_t counts[SG_RUNE_COMPACT_WIRE_SECTION_COUNT];
} sg_wire_source_t;

struct sg_rune_compact_wire_decoded_s
{
	sg_rune_compact_model_t model;
	sg_rune_compact_analytic_t analytic;
	sg_rune_compact_static_t static_data;
	uint64_t payload_alignment;
	unsigned char payload[];
};

static const uint8_t sg_wire_magic[8] = {
	UINT8_C(0x53), UINT8_C(0x47), UINT8_C(0x52), UINT8_C(0x43),
	UINT8_C(0x57), UINT8_C(0x30), UINT8_C(0x30), UINT8_C(0x31)
};

static const sg_wire_spec_t sg_wire_specs[SG_RUNE_COMPACT_WIRE_SECTION_COUNT] = {
	{ UINT32_C(252), UINT32_C(1) },
	{ UINT32_C(80), SG_RUNE_COMPACT_MAX_CELLS },
	{ UINT32_C(56), SG_RUNE_COMPACT_MAX_FACETS },
	{ UINT32_C(20), SG_RUNE_COMPACT_MAX_INCIDENCES },
	{ UINT32_C(4), SG_RUNE_COMPACT_MAX_INCIDENCES },
	{ UINT32_C(12), SG_RUNE_COMPACT_MAX_VERTICES },
	{ UINT32_C(44), SG_RUNE_COMPACT_MAX_PORTALS },
	{ UINT32_C(24), SG_RUNE_COMPACT_MAX_MOVEMENT_FIELDS },
	{ UINT32_C(20), SG_RUNE_COMPACT_MAX_WEAPON_REGIONS },
	{ UINT32_C(8), SG_RUNE_COMPACT_MAX_WEAPON_PROFILES },
	{ UINT32_C(20), SG_RUNE_COMPACT_MAX_WEAPON_KERNELS },
	{ UINT32_C(4), SG_RUNE_COMPACT_MAX_ANALYTIC_FUNCTION_REFS },
	{ UINT32_C(20), SG_RUNE_ANALYTIC_MAX_FUNCTIONS },
	{ UINT32_C(4), SG_RUNE_ANALYTIC_MAX_INPUT_DIMENSIONS },
	{ UINT32_C(4), SG_RUNE_ANALYTIC_MAX_FUNCTIONS },
	{ UINT32_C(12), SG_RUNE_ANALYTIC_MAX_FUNCTIONS },
	{ UINT32_C(4), SG_RUNE_ANALYTIC_MAX_AFFINE_SLOPES },
	{ UINT32_C(12), SG_RUNE_ANALYTIC_MAX_FUNCTIONS },
	{ UINT32_C(4), SG_RUNE_ANALYTIC_MAX_POLYNOMIAL_COEFFICIENTS },
	{ UINT32_C(12), SG_RUNE_ANALYTIC_MAX_FUNCTIONS },
	{ UINT32_C(16), SG_RUNE_ANALYTIC_MAX_FUNCTIONS },
	{ UINT32_C(16), SG_RUNE_ANALYTIC_MAX_PIECEWISE_CLAUSES },
	{ UINT32_C(100), SG_RUNE_COMPACT_MAX_MECHANISMS },
	{ UINT32_C(16), SG_RUNE_COMPACT_MAX_MECHANISM_EDGES },
	{ UINT32_C(60), SG_RUNE_COMPACT_MAX_LANDMARKS },
	{ UINT32_C(4), SG_RUNE_COMPACT_MAX_LANDMARK_CELL_REFS },
	{ UINT32_C(8), SG_RUNE_COMPACT_MAX_FACET_ANNOTATIONS },
	{ UINT32_C(15), SG_RUNE_COMPACT_MAX_PORTAL_MECHANISMS }
};

static void sg_wire_set_error(sg_rune_compact_wire_error_t *error,
	sg_rune_compact_wire_error_code_t code,
	sg_rune_compact_wire_section_t section, uint32_t record)
{
	if (error != NULL)
	{
		error->code = code;
		error->section = section;
		error->record = record;
		error->model_error.code = SG_RUNE_COMPACT_ERROR_NONE;
		error->model_error.domain = SG_RUNE_COMPACT_RECORD_MODEL;
		error->model_error.record = 0;
	}
}

static sg_rune_compact_wire_section_t sg_wire_model_section(
	sg_rune_compact_record_domain_t domain)
{
	switch (domain)
	{
	case SG_RUNE_COMPACT_RECORD_CELL:
		return SG_RUNE_COMPACT_WIRE_SECTION_CELLS;
	case SG_RUNE_COMPACT_RECORD_FACET:
		return SG_RUNE_COMPACT_WIRE_SECTION_FACETS;
	case SG_RUNE_COMPACT_RECORD_INCIDENCE:
		return SG_RUNE_COMPACT_WIRE_SECTION_INCIDENCES;
	case SG_RUNE_COMPACT_RECORD_PORTAL:
		return SG_RUNE_COMPACT_WIRE_SECTION_PORTALS;
	case SG_RUNE_COMPACT_RECORD_MOVEMENT_FIELD:
		return SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIELDS;
	case SG_RUNE_COMPACT_RECORD_WEAPON_REGION:
		return SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_REGIONS;
	case SG_RUNE_COMPACT_RECORD_WEAPON_PROFILE:
		return SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_PROFILES;
	case SG_RUNE_COMPACT_RECORD_WEAPON_KERNEL:
		return SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_KERNELS;
	case SG_RUNE_COMPACT_RECORD_MODEL:
	default:
		return SG_RUNE_COMPACT_WIRE_SECTION_COUNT;
	}
}

static void sg_wire_set_model_error(sg_rune_compact_wire_error_t *error,
	const sg_rune_compact_error_t *model_error)
{
	sg_rune_compact_wire_error_code_t code;

	if (model_error == NULL)
		return;
	code = model_error->code == SG_RUNE_COMPACT_ERROR_IDENTITY_MISMATCH ?
		SG_RUNE_COMPACT_WIRE_ERROR_IDENTITY_MISMATCH :
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_MODEL;
	sg_wire_set_error(error, code, sg_wire_model_section(model_error->domain),
		model_error->record);
	if (error != NULL)
		error->model_error = *model_error;
}

static void sg_wire_clear_error(sg_rune_compact_wire_error_t *error)
{
	sg_wire_set_error(error, SG_RUNE_COMPACT_WIRE_ERROR_NONE,
		SG_RUNE_COMPACT_WIRE_SECTION_COUNT, UINT32_MAX);
}

static uint16_t sg_wire_u16(const uint8_t *p)
{
	return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t sg_wire_u32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
		((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t sg_wire_u64(const uint8_t *p)
{
	return (uint64_t)sg_wire_u32(p) |
		((uint64_t)sg_wire_u32(p + 4) << 32);
}

static void sg_wire_put_u16(uint8_t *p, uint16_t value)
{
	p[0] = (uint8_t)value;
	p[1] = (uint8_t)(value >> 8);
}

static void sg_wire_put_u32(uint8_t *p, uint32_t value)
{
	p[0] = (uint8_t)value;
	p[1] = (uint8_t)(value >> 8);
	p[2] = (uint8_t)(value >> 16);
	p[3] = (uint8_t)(value >> 24);
}

static void sg_wire_put_u64(uint8_t *p, uint64_t value)
{
	sg_wire_put_u32(p, (uint32_t)value);
	sg_wire_put_u32(p + 4, (uint32_t)(value >> 32));
}

static int sg_wire_align(uint64_t value, uint64_t *aligned_out)
{
	if (value > UINT64_MAX - (SG_WIRE_ALIGNMENT - UINT64_C(1)))
		return 0;
	*aligned_out = (value + SG_WIRE_ALIGNMENT - UINT64_C(1)) &
		~(SG_WIRE_ALIGNMENT - UINT64_C(1));
	return 1;
}

static int sg_wire_add_product(uint64_t *value, uint32_t count,
	uint32_t size)
{
	uint64_t product = (uint64_t)count * (uint64_t)size;
	if (*value > UINT64_MAX - product)
		return 0;
	*value += product;
	return 1;
}

static uint32_t sg_wire_checksum(const uint8_t *data, size_t size)
{
	uint32_t crc = UINT32_MAX;
	size_t i;
	for (i = 0; i < size; ++i)
	{
		uint32_t byte = data[i];
		uint32_t bit;
		if (i >= SG_WIRE_CHECKSUM_OFFSET &&
			i < SG_WIRE_CHECKSUM_OFFSET + UINT32_C(4))
			byte = 0;
		crc ^= byte;
		for (bit = 0; bit < UINT32_C(8); ++bit)
			crc = (crc >> 1) ^
				(UINT32_C(0xedb88320) & (uint32_t)-(int32_t)(crc & 1));
	}
	return ~crc;
}

static int sg_wire_zero(const uint8_t *p, size_t count)
{
	size_t i;
	for (i = 0; i < count; ++i)
		if (p[i] != 0)
			return 0;
	return 1;
}

static int sg_wire_span(uint32_t first, uint32_t count, uint32_t total)
{
	return first <= total && count <= total - first;
}

static int sg_wire_ref(uint32_t value, uint32_t total, int allow_none)
{
	return value < total || (allow_none && value == SG_RUNE_COMPACT_INDEX_NONE);
}

static void sg_wire_put_vec3(uint8_t *p, const sg_rune_q8_vec3_t *value)
{
	uint32_t i;
	for (i = 0; i < UINT32_C(3); ++i)
		sg_wire_put_u32(p + i * 4, (uint32_t)value->value[i]);
}

static void sg_wire_get_vec3(const uint8_t *p, sg_rune_q8_vec3_t *value)
{
	uint32_t i;
	for (i = 0; i < UINT32_C(3); ++i)
		value->value[i] = (int32_t)sg_wire_u32(p + i * 4);
}

static void sg_wire_put_bounds(uint8_t *p, const sg_rune_q8_bounds_t *value)
{
	sg_wire_put_vec3(p, &value->mins);
	sg_wire_put_vec3(p + 12, &value->maxs);
}

static void sg_wire_get_bounds(const uint8_t *p, sg_rune_q8_bounds_t *value)
{
	sg_wire_get_vec3(p, &value->mins);
	sg_wire_get_vec3(p + 12, &value->maxs);
}

static void sg_wire_put_source(uint8_t *p,
	const sg_rune_compact_source_t *source)
{
	sg_wire_put_u32(p, (uint32_t)source->kind);
	switch (source->kind)
	{
	case SG_RUNE_COMPACT_SOURCE_DOMAIN:
		sg_wire_put_u32(p + 4, source->value.domain.axis);
		sg_wire_put_u32(p + 8, source->value.domain.maximum_side);
		break;
	case SG_RUNE_COMPACT_SOURCE_BSP_PLANE:
		sg_wire_put_u32(p + 4, source->value.bsp_plane.model);
		sg_wire_put_u32(p + 8, source->value.bsp_plane.leaf);
		sg_wire_put_u32(p + 12, source->value.bsp_plane.plane);
		break;
	case SG_RUNE_COMPACT_SOURCE_EXPANDED_BRUSH_SIDE:
		sg_wire_put_u32(p + 4, source->value.brush_side.model);
		sg_wire_put_u32(p + 8, source->value.brush_side.brush);
		sg_wire_put_u32(p + 12, source->value.brush_side.brush_side);
		sg_wire_put_u32(p + 16, source->value.brush_side.plane);
		break;
	case SG_RUNE_COMPACT_SOURCE_SPLIT:
		sg_wire_put_u32(p + 4, source->value.split.parent_facet.value);
		sg_wire_put_u32(p + 8, source->value.split.ordinal);
		break;
	default:
		break;
	}
}

static void sg_wire_get_source(const uint8_t *p,
	sg_rune_compact_source_t *source)
{
	memset(source, 0, sizeof(*source));
	source->kind = (sg_rune_compact_source_kind_t)sg_wire_u32(p);
	switch (source->kind)
	{
	case SG_RUNE_COMPACT_SOURCE_DOMAIN:
		source->value.domain.axis = sg_wire_u32(p + 4);
		source->value.domain.maximum_side = sg_wire_u32(p + 8);
		break;
	case SG_RUNE_COMPACT_SOURCE_BSP_PLANE:
		source->value.bsp_plane.model = sg_wire_u32(p + 4);
		source->value.bsp_plane.leaf = sg_wire_u32(p + 8);
		source->value.bsp_plane.plane = sg_wire_u32(p + 12);
		break;
	case SG_RUNE_COMPACT_SOURCE_EXPANDED_BRUSH_SIDE:
		source->value.brush_side.model = sg_wire_u32(p + 4);
		source->value.brush_side.brush = sg_wire_u32(p + 8);
		source->value.brush_side.brush_side = sg_wire_u32(p + 12);
		source->value.brush_side.plane = sg_wire_u32(p + 16);
		break;
	case SG_RUNE_COMPACT_SOURCE_SPLIT:
		source->value.split.parent_facet.value = sg_wire_u32(p + 4);
		source->value.split.ordinal = sg_wire_u32(p + 8);
		break;
	default:
		break;
	}
}

static int sg_wire_validate_source(const uint8_t *p, uint32_t facet_count)
{
	uint32_t kind = sg_wire_u32(p);
	if (kind >= (uint32_t)SG_RUNE_COMPACT_SOURCE_KIND_COUNT)
		return 0;
	if (kind == (uint32_t)SG_RUNE_COMPACT_SOURCE_DOMAIN)
		return sg_wire_zero(p + 12, 8);
	if (kind == (uint32_t)SG_RUNE_COMPACT_SOURCE_BSP_PLANE)
		return sg_wire_zero(p + 16, 4);
	if (kind == (uint32_t)SG_RUNE_COMPACT_SOURCE_SPLIT)
		return sg_wire_ref(sg_wire_u32(p + 4), facet_count, 0) &&
			sg_wire_zero(p + 12, 8);
	return 1;
}

static void sg_wire_put_identity(uint8_t *p,
	const sg_rune_compact_identity_t *identity)
{
	memcpy(p, identity->bsp_sha256, 32);
	sg_wire_put_u64(p + 32, identity->bsp_bytes);
	sg_wire_put_u32(p + 40, identity->bsp_checksum);
	sg_wire_put_u32(p + 44, identity->entity_crc32);
	sg_wire_put_u64(p + 48, identity->entity_semantics_id);
	sg_wire_put_u64(p + 56, identity->physics_abi_id);
	sg_wire_put_u64(p + 64, identity->collision_law_id);
	sg_wire_put_u64(p + 72, identity->pmove_law_id);
	sg_wire_put_u64(p + 80, identity->gravity_law_id);
	sg_wire_put_u64(p + 88, identity->hook_law_id);
	sg_wire_put_u64(p + 96, identity->mechanism_law_id);
	sg_wire_put_u64(p + 104, identity->weapon_law_id);
	sg_wire_put_u64(p + 112, identity->construction_id);
	sg_wire_put_u64(p + 120, identity->schema_id);
	sg_wire_put_u64(p + 128, identity->producer_identity);
	sg_wire_put_u32(p + 136, identity->source_counts.model_count);
	sg_wire_put_u32(p + 140, identity->source_counts.leaf_count);
	sg_wire_put_u32(p + 144, identity->source_counts.area_count);
	sg_wire_put_u32(p + 148, identity->source_counts.plane_count);
	sg_wire_put_u32(p + 152, identity->source_counts.brush_count);
	sg_wire_put_u32(p + 156, identity->source_counts.brush_side_count);
	sg_wire_put_u32(p + 160, identity->source_counts.entity_count);
	sg_wire_put_vec3(p + 164, &identity->standing_hull.mins);
	sg_wire_put_vec3(p + 176, &identity->standing_hull.maxs);
	sg_wire_put_vec3(p + 188, &identity->crouching_hull.mins);
	sg_wire_put_vec3(p + 200, &identity->crouching_hull.maxs);
	sg_wire_put_u32(p + 212, identity->physics.gravity_bits);
	sg_wire_put_u32(p + 216, identity->physics.ground_acceleration_bits);
	sg_wire_put_u32(p + 220, identity->physics.air_acceleration_bits);
	sg_wire_put_u32(p + 224, identity->physics.water_acceleration_bits);
	sg_wire_put_u32(p + 228, identity->physics.hook_acceleration_bits);
	sg_wire_put_u32(p + 232, identity->physics.external_acceleration_bits);
	sg_wire_put_u32(p + 236, identity->physics.water_drag_bits);
	sg_wire_put_u32(p + 240, identity->physics.max_velocity_bits);
	sg_wire_put_u32(p + 244, identity->physics.frame_ms);
	sg_wire_put_u32(p + 248, identity->physics.substep_ms);
}

static void sg_wire_get_identity(const uint8_t *p,
	sg_rune_compact_identity_t *identity)
{
	memset(identity, 0, sizeof(*identity));
	memcpy(identity->bsp_sha256, p, 32);
	identity->bsp_bytes = sg_wire_u64(p + 32);
	identity->bsp_checksum = sg_wire_u32(p + 40);
	identity->entity_crc32 = sg_wire_u32(p + 44);
	identity->entity_semantics_id = sg_wire_u64(p + 48);
	identity->physics_abi_id = sg_wire_u64(p + 56);
	identity->collision_law_id = sg_wire_u64(p + 64);
	identity->pmove_law_id = sg_wire_u64(p + 72);
	identity->gravity_law_id = sg_wire_u64(p + 80);
	identity->hook_law_id = sg_wire_u64(p + 88);
	identity->mechanism_law_id = sg_wire_u64(p + 96);
	identity->weapon_law_id = sg_wire_u64(p + 104);
	identity->construction_id = sg_wire_u64(p + 112);
	identity->schema_id = sg_wire_u64(p + 120);
	identity->producer_identity = sg_wire_u64(p + 128);
	identity->source_counts.model_count = sg_wire_u32(p + 136);
	identity->source_counts.leaf_count = sg_wire_u32(p + 140);
	identity->source_counts.area_count = sg_wire_u32(p + 144);
	identity->source_counts.plane_count = sg_wire_u32(p + 148);
	identity->source_counts.brush_count = sg_wire_u32(p + 152);
	identity->source_counts.brush_side_count = sg_wire_u32(p + 156);
	identity->source_counts.entity_count = sg_wire_u32(p + 160);
	sg_wire_get_vec3(p + 164, &identity->standing_hull.mins);
	sg_wire_get_vec3(p + 176, &identity->standing_hull.maxs);
	sg_wire_get_vec3(p + 188, &identity->crouching_hull.mins);
	sg_wire_get_vec3(p + 200, &identity->crouching_hull.maxs);
	identity->physics.gravity_bits = sg_wire_u32(p + 212);
	identity->physics.ground_acceleration_bits = sg_wire_u32(p + 216);
	identity->physics.air_acceleration_bits = sg_wire_u32(p + 220);
	identity->physics.water_acceleration_bits = sg_wire_u32(p + 224);
	identity->physics.hook_acceleration_bits = sg_wire_u32(p + 228);
	identity->physics.external_acceleration_bits = sg_wire_u32(p + 232);
	identity->physics.water_drag_bits = sg_wire_u32(p + 236);
	identity->physics.max_velocity_bits = sg_wire_u32(p + 240);
	identity->physics.frame_ms = sg_wire_u32(p + 244);
	identity->physics.substep_ms = sg_wire_u32(p + 248);
}

static int sg_wire_collect(const sg_rune_compact_model_t *model,
	sg_wire_source_t *source, sg_rune_compact_wire_error_t *error)
{
	const sg_rune_compact_analytic_t *analytic;
	const sg_rune_compact_static_t *static_data;
	uint32_t section;
	if (model == NULL || source == NULL)
	{
		sg_wire_set_error(error, SG_RUNE_COMPACT_WIRE_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_WIRE_SECTION_COUNT, UINT32_MAX);
		return 0;
	}
	if (model->version != SG_RUNE_COMPACT_MODEL_VERSION ||
		model->schema_tag != SG_RUNE_COMPACT_MODEL_SCHEMA_TAG)
	{
		sg_wire_set_error(error, SG_RUNE_COMPACT_WIRE_ERROR_UNSUPPORTED_VERSION,
			SG_RUNE_COMPACT_WIRE_SECTION_COUNT, UINT32_MAX);
		return 0;
	}
	if (model->reserved != 0)
	{
		sg_wire_set_error(error, SG_RUNE_COMPACT_WIRE_ERROR_NONZERO_RESERVED,
			SG_RUNE_COMPACT_WIRE_SECTION_COUNT, UINT32_MAX);
		return 0;
	}
	analytic = model->analytic;
	static_data = model->static_data;
	if (analytic == NULL || static_data == NULL)
	{
		sg_wire_set_error(error, SG_RUNE_COMPACT_WIRE_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_WIRE_SECTION_COUNT, UINT32_MAX);
		return 0;
	}
	if (analytic->version != SG_RUNE_COMPACT_ANALYTIC_VERSION)
	{
		sg_wire_set_error(error, SG_RUNE_COMPACT_WIRE_ERROR_UNSUPPORTED_VERSION,
			SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_FUNCTIONS, UINT32_MAX);
		return 0;
	}
	if (analytic->reserved != 0)
	{
		sg_wire_set_error(error, SG_RUNE_COMPACT_WIRE_ERROR_NONZERO_RESERVED,
			SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_FUNCTIONS, UINT32_MAX);
		return 0;
	}
	memset(source, 0, sizeof(*source));
#define SG_WIRE_SOURCE(section_name, pointer_value, count_value) \
	do { \
		source->arrays[section_name] = (pointer_value); \
		source->counts[section_name] = (count_value); \
	} while (0)
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_IDENTITY, &model->identity, 1);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_CELLS, model->cells, model->cell_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_FACETS, model->facets, model->facet_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_INCIDENCES, model->incidences, model->incidence_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_CELL_INCIDENCES, model->cell_incidences, model->cell_incidence_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_VERTICES, model->vertices, model->vertex_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_PORTALS, model->portals, model->portal_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIELDS, model->movement_fields, model->movement_field_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_REGIONS, model->weapon_regions, model->weapon_region_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_PROFILES, model->weapon_profiles, model->weapon_profile_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_KERNELS, model->weapon_kernels, model->weapon_kernel_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_FUNCTION_REFS, model->analytic_function_refs, model->analytic_function_ref_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_FUNCTIONS, analytic->functions, analytic->function_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_INPUT_DIMENSIONS, analytic->input_dimensions, analytic->input_dimension_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_CONSTANTS, analytic->constants, analytic->constant_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_AFFINES, analytic->affines, analytic->affine_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_AFFINE_SLOPES, analytic->affine_slopes, analytic->affine_slope_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_POLYNOMIALS, analytic->polynomials, analytic->polynomial_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_POLYNOMIAL_COEFFICIENTS, analytic->polynomial_coefficients, analytic->polynomial_coefficient_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_BALLISTICS, analytic->ballistics, analytic->ballistic_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_PIECEWISE, analytic->piecewise, analytic->piecewise_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_PIECEWISE_CLAUSES, analytic->piecewise_clauses, analytic->piecewise_clause_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_MECHANISMS, static_data->mechanisms, static_data->mechanism_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_EDGES, static_data->mechanism_edges, static_data->mechanism_edge_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_LANDMARKS, static_data->landmarks, static_data->landmark_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_LANDMARK_CELLS, static_data->landmark_cells, static_data->landmark_cell_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_FACET_ANNOTATIONS, static_data->facet_annotations, static_data->facet_annotation_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_PORTAL_MECHANISMS, static_data->portal_mechanisms, static_data->portal_mechanism_count);
#undef SG_WIRE_SOURCE
	for (section = 0; section < (uint32_t)SG_RUNE_COMPACT_WIRE_SECTION_COUNT;
		++section)
	{
		if (source->counts[section] > sg_wire_specs[section].limit)
		{
			sg_wire_set_error(error, SG_RUNE_COMPACT_WIRE_ERROR_LIMIT_EXCEEDED,
				(sg_rune_compact_wire_section_t)section, UINT32_MAX);
			return 0;
		}
		if (source->counts[section] != 0 && source->arrays[section] == NULL)
		{
			sg_wire_set_error(error, SG_RUNE_COMPACT_WIRE_ERROR_INVALID_ARGUMENT,
				(sg_rune_compact_wire_section_t)section, UINT32_MAX);
			return 0;
		}
	}
	return 1;
}

static int sg_wire_layout(const uint32_t *counts, sg_wire_desc_t *descs,
	uint64_t *total_out)
{
	uint64_t cursor = SG_WIRE_HEADER_SIZE;
	uint32_t section;
	for (section = 0; section < (uint32_t)SG_RUNE_COMPACT_WIRE_SECTION_COUNT;
		++section)
	{
		if (!sg_wire_align(cursor, &cursor))
			return 0;
		descs[section].count = counts[section];
		descs[section].offset = cursor;
		if (!sg_wire_add_product(&cursor, counts[section],
			sg_wire_specs[section].wire_size))
			return 0;
	}
	if (!sg_wire_align(cursor, &cursor))
		return 0;
	*total_out = cursor;
	return 1;
}

int SG_RuneCompactWireMeasure(const sg_rune_compact_model_t *model,
	size_t *size_out, sg_rune_compact_wire_error_t *error_out)
{
	sg_wire_source_t source;
	sg_wire_desc_t descs[SG_RUNE_COMPACT_WIRE_SECTION_COUNT];
	uint64_t total;
	sg_wire_clear_error(error_out);
	if (size_out == NULL)
	{
		sg_wire_set_error(error_out, SG_RUNE_COMPACT_WIRE_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_WIRE_SECTION_COUNT, UINT32_MAX);
		return 0;
	}
	*size_out = 0;
	if (!sg_wire_collect(model, &source, error_out))
		return 0;
	if (!sg_wire_layout(source.counts, descs, &total) || total > SIZE_MAX)
	{
		sg_wire_set_error(error_out, SG_RUNE_COMPACT_WIRE_ERROR_OVERFLOW,
			SG_RUNE_COMPACT_WIRE_SECTION_COUNT, UINT32_MAX);
		return 0;
	}
	*size_out = (size_t)total;
	return 1;
}

static void sg_wire_encode_arrays(const sg_wire_source_t *source,
	const sg_wire_desc_t *descs, uint8_t *image)
{
	uint32_t i;
	uint8_t *p;
	const sg_rune_compact_cell_t *cells = source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_CELLS];
	const sg_rune_compact_facet_t *facets = source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_FACETS];
	const sg_rune_compact_incidence_t *incidences = source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_INCIDENCES];
	const sg_rune_compact_incidence_index_t *cell_incidences = source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_CELL_INCIDENCES];
	const sg_rune_q8_vec3_t *vertices = source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_VERTICES];
	const sg_rune_compact_portal_t *portals = source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_PORTALS];
	const sg_rune_movement_field_attachment_t *movement_fields = source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIELDS];
	const sg_rune_weapon_response_region_t *weapon_regions = source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_REGIONS];
	const sg_rune_weapon_profile_t *weapon_profiles = source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_PROFILES];
	const sg_rune_weapon_response_kernel_t *weapon_kernels = source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_KERNELS];
	const sg_rune_analytic_function_index_t *analytic_refs = source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_FUNCTION_REFS];
	const sg_rune_analytic_function_t *functions = source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_FUNCTIONS];
	const sg_rune_analytic_input_dimension_t *dimensions = source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_INPUT_DIMENSIONS];
	const sg_rune_analytic_constant_t *constants = source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_CONSTANTS];
	const sg_rune_analytic_affine_t *affines = source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_AFFINES];
	const sg_rune_analytic_scalar_bits_t *slopes = source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_AFFINE_SLOPES];
	const sg_rune_analytic_polynomial_t *polynomials = source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_POLYNOMIALS];
	const sg_rune_analytic_scalar_bits_t *coefficients = source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_POLYNOMIAL_COEFFICIENTS];
	const sg_rune_analytic_ballistic_t *ballistics = source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_BALLISTICS];
	const sg_rune_analytic_piecewise_t *piecewise = source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_PIECEWISE];
	const sg_rune_analytic_piecewise_clause_t *clauses = source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_PIECEWISE_CLAUSES];
	const sg_rune_compact_mechanism_t *mechanisms = source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_MECHANISMS];
	const sg_rune_compact_mechanism_edge_t *edges = source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_EDGES];
	const sg_rune_compact_landmark_t *landmarks = source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_LANDMARKS];
	const sg_rune_compact_cell_index_t *landmark_cells = source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_LANDMARK_CELLS];
	const sg_rune_compact_facet_annotation_t *annotations = source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_FACET_ANNOTATIONS];
	const sg_rune_compact_portal_mechanism_t *portal_mechanisms = source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_PORTAL_MECHANISMS];

	sg_wire_put_identity(image + descs[SG_RUNE_COMPACT_WIRE_SECTION_IDENTITY].offset,
		source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_IDENTITY]);

#define SG_WIRE_RECORD(section_name, ordinal) \
	(image + descs[section_name].offset + \
	 (uint64_t)(ordinal) * sg_wire_specs[section_name].wire_size)
	for (i = 0; i < source->counts[SG_RUNE_COMPACT_WIRE_SECTION_CELLS]; ++i)
	{
		p = SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_CELLS, i);
		sg_wire_put_u32(p, cells[i].source.model);
		sg_wire_put_u32(p + 4, cells[i].source.leaf);
		sg_wire_put_u32(p + 8, cells[i].source.area);
		sg_wire_put_u32(p + 12, (uint32_t)cells[i].source.cluster);
		sg_wire_put_u32(p + 16, cells[i].source.split_ordinal);
		sg_wire_put_bounds(p + 20, &cells[i].bounds);
		sg_wire_put_u32(p + 44, cells[i].incidences.first);
		sg_wire_put_u32(p + 48, cells[i].incidences.count);
		sg_wire_put_u32(p + 52, cells[i].movement_fields.first);
		sg_wire_put_u32(p + 56, cells[i].movement_fields.count);
		sg_wire_put_u32(p + 60, cells[i].weapon_regions.first);
		sg_wire_put_u32(p + 64, cells[i].weapon_regions.count);
		sg_wire_put_u32(p + 68, cells[i].contents);
		sg_wire_put_u32(p + 72, cells[i].semantics);
		p[76] = cells[i].valid_stances;
		memcpy(p + 77, cells[i].reserved, 3);
	}
	for (i = 0; i < source->counts[SG_RUNE_COMPACT_WIRE_SECTION_FACETS]; ++i)
	{
		uint32_t j;
		p = SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_FACETS, i);
		sg_wire_put_source(p, &facets[i].source);
		for (j = 0; j < UINT32_C(3); ++j)
			sg_wire_put_u32(p + 20 + j * 4, facets[i].plane.normal_bits[j]);
		sg_wire_put_u32(p + 32, facets[i].plane.distance_bits);
		sg_wire_put_u32(p + 36, facets[i].vertices.first);
		sg_wire_put_u32(p + 40, facets[i].vertices.count);
		sg_wire_put_u32(p + 44, facets[i].incidences.first);
		sg_wire_put_u32(p + 48, facets[i].incidences.count);
		sg_wire_put_u32(p + 52, facets[i].portal.value);
	}
	for (i = 0; i < source->counts[SG_RUNE_COMPACT_WIRE_SECTION_INCIDENCES]; ++i)
	{
		p = SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_INCIDENCES, i);
		sg_wire_put_u32(p, incidences[i].cell.value);
		sg_wire_put_u32(p + 4, incidences[i].facet.value);
		sg_wire_put_u32(p + 8, incidences[i].cell_ordinal);
		sg_wire_put_u32(p + 12, (uint32_t)incidences[i].side);
		sg_wire_put_u32(p + 16, (uint32_t)incidences[i].boundary);
	}
	for (i = 0; i < source->counts[SG_RUNE_COMPACT_WIRE_SECTION_CELL_INCIDENCES]; ++i)
		sg_wire_put_u32(SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_CELL_INCIDENCES, i), cell_incidences[i].value);
	for (i = 0; i < source->counts[SG_RUNE_COMPACT_WIRE_SECTION_VERTICES]; ++i)
		sg_wire_put_vec3(SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_VERTICES, i), &vertices[i]);
	for (i = 0; i < source->counts[SG_RUNE_COMPACT_WIRE_SECTION_PORTALS]; ++i)
	{
		p = SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_PORTALS, i);
		sg_wire_put_source(p, &portals[i].source);
		sg_wire_put_u32(p + 20, portals[i].facet.value);
		sg_wire_put_u32(p + 24, portals[i].negative_incidence.value);
		sg_wire_put_u32(p + 28, portals[i].positive_incidence.value);
		sg_wire_put_u32(p + 32, portals[i].clearance_q8);
		sg_wire_put_u32(p + 36, (uint32_t)portals[i].direction);
		p[40] = portals[i].valid_stances;
		memcpy(p + 41, portals[i].reserved, 3);
	}
	for (i = 0; i < source->counts[SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIELDS]; ++i)
	{
		p = SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIELDS, i);
		sg_wire_put_u32(p, movement_fields[i].cell.value);
		sg_wire_put_u32(p + 4, movement_fields[i].boundary_portal.value);
		sg_wire_put_u32(p + 8, (uint32_t)movement_fields[i].family);
		p[12] = movement_fields[i].valid_stances;
		memcpy(p + 13, movement_fields[i].reserved, 3);
		sg_wire_put_u32(p + 16, movement_fields[i].functions.first);
		sg_wire_put_u32(p + 20, movement_fields[i].functions.count);
	}
	for (i = 0; i < source->counts[SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_REGIONS]; ++i)
	{
		p = SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_REGIONS, i);
		sg_wire_put_u32(p, weapon_regions[i].cell.value);
		sg_wire_put_u32(p + 4, weapon_regions[i].boundary_incidences.first);
		sg_wire_put_u32(p + 8, weapon_regions[i].boundary_incidences.count);
		sg_wire_put_u32(p + 12, weapon_regions[i].kernels.first);
		sg_wire_put_u32(p + 16, weapon_regions[i].kernels.count);
	}
	for (i = 0; i < source->counts[SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_PROFILES]; ++i)
	{
		p = SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_PROFILES, i);
		sg_wire_put_u32(p, weapon_profiles[i].source_profile);
		sg_wire_put_u32(p + 4, weapon_profiles[i].response_families);
	}
	for (i = 0; i < source->counts[SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_KERNELS]; ++i)
	{
		p = SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_KERNELS, i);
		sg_wire_put_u32(p, weapon_kernels[i].region.value);
		sg_wire_put_u32(p + 4, weapon_kernels[i].profile);
		sg_wire_put_u32(p + 8, (uint32_t)weapon_kernels[i].family);
		sg_wire_put_u32(p + 12, weapon_kernels[i].functions.first);
		sg_wire_put_u32(p + 16, weapon_kernels[i].functions.count);
	}
	for (i = 0; i < source->counts[SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_FUNCTION_REFS]; ++i)
		sg_wire_put_u32(SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_FUNCTION_REFS, i), analytic_refs[i].value);
	for (i = 0; i < source->counts[SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_FUNCTIONS]; ++i)
	{
		p = SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_FUNCTIONS, i);
		sg_wire_put_u32(p, functions[i].inputs.first);
		sg_wire_put_u32(p + 4, functions[i].inputs.count);
		sg_wire_put_u32(p + 8, functions[i].definition);
		sg_wire_put_u32(p + 12, (uint32_t)functions[i].output);
		sg_wire_put_u32(p + 16, (uint32_t)functions[i].form);
	}
	for (i = 0; i < source->counts[SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_INPUT_DIMENSIONS]; ++i)
		sg_wire_put_u32(SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_INPUT_DIMENSIONS, i), (uint32_t)dimensions[i]);
	for (i = 0; i < source->counts[SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_CONSTANTS]; ++i)
		sg_wire_put_u32(SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_CONSTANTS, i), constants[i].value.bits);
	for (i = 0; i < source->counts[SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_AFFINES]; ++i)
	{
		p = SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_AFFINES, i);
		sg_wire_put_u32(p, affines[i].bias.bits);
		sg_wire_put_u32(p + 4, affines[i].slopes.first);
		sg_wire_put_u32(p + 8, affines[i].slopes.count);
	}
	for (i = 0; i < source->counts[SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_AFFINE_SLOPES]; ++i)
		sg_wire_put_u32(SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_AFFINE_SLOPES, i), slopes[i].bits);
	for (i = 0; i < source->counts[SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_POLYNOMIALS]; ++i)
	{
		p = SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_POLYNOMIALS, i);
		sg_wire_put_u32(p, polynomials[i].coefficients.first);
		sg_wire_put_u32(p + 4, polynomials[i].coefficients.count);
		p[8] = polynomials[i].degree;
		memcpy(p + 9, polynomials[i].reserved, 3);
	}
	for (i = 0; i < source->counts[SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_POLYNOMIAL_COEFFICIENTS]; ++i)
		sg_wire_put_u32(SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_POLYNOMIAL_COEFFICIENTS, i), coefficients[i].bits);
	for (i = 0; i < source->counts[SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_BALLISTICS]; ++i)
	{
		p = SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_BALLISTICS, i);
		sg_wire_put_u32(p, ballistics[i].initial.bits);
		sg_wire_put_u32(p + 4, ballistics[i].first_derivative.bits);
		sg_wire_put_u32(p + 8, ballistics[i].half_second_derivative.bits);
	}
	for (i = 0; i < source->counts[SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_PIECEWISE]; ++i)
	{
		p = SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_PIECEWISE, i);
		sg_wire_put_u32(p, piecewise[i].clauses.first);
		sg_wire_put_u32(p + 4, piecewise[i].clauses.count);
		sg_wire_put_u32(p + 8, piecewise[i].default_function.value);
		sg_wire_put_u32(p + 12, piecewise[i].selector_input);
	}
	for (i = 0; i < source->counts[SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_PIECEWISE_CLAUSES]; ++i)
	{
		p = SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_PIECEWISE_CLAUSES, i);
		sg_wire_put_u32(p, clauses[i].lower.bits);
		sg_wire_put_u32(p + 4, clauses[i].upper.bits);
		sg_wire_put_u32(p + 8, clauses[i].function.value);
		sg_wire_put_u32(p + 12, (uint32_t)clauses[i].ownership);
	}
	for (i = 0; i < source->counts[SG_RUNE_COMPACT_WIRE_SECTION_MECHANISMS]; ++i)
	{
		p = SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_MECHANISMS, i);
		sg_wire_put_u32(p, mechanisms[i].source.entity_ordinal);
		sg_wire_put_u32(p + 4, mechanisms[i].controller.entity_ordinal);
		sg_wire_put_u32(p + 8, mechanisms[i].entry_cell.value);
		sg_wire_put_u32(p + 12, mechanisms[i].exit_cell.value);
		sg_wire_put_u32(p + 16, mechanisms[i].activation_landmark.value);
		sg_wire_put_bounds(p + 20, &mechanisms[i].bounds);
		sg_wire_put_u32(p + 44, mechanisms[i].topology.first);
		sg_wire_put_u32(p + 48, mechanisms[i].topology.count);
		sg_wire_put_u32(p + 52, mechanisms[i].delay_ms);
		sg_wire_put_u32(p + 56, mechanisms[i].dwell_ms);
		sg_wire_put_u32(p + 60, mechanisms[i].travel_ms);
		sg_wire_put_u32(p + 64, mechanisms[i].wait_ms);
		sg_wire_put_u32(p + 68, mechanisms[i].reset_ms);
		sg_wire_put_u32(p + 72, (uint32_t)mechanisms[i].kind);
		sg_wire_put_u32(p + 76, (uint32_t)mechanisms[i].activation);
		sg_wire_put_u32(p + 80, (uint32_t)mechanisms[i].initial_state);
		sg_wire_put_u32(p + 84, (uint32_t)mechanisms[i].activated_state);
		sg_wire_put_u32(p + 88, (uint32_t)mechanisms[i].reset_state);
		sg_wire_put_u32(p + 92, (uint32_t)mechanisms[i].recovery);
		p[96] = mechanisms[i].flags;
		memcpy(p + 97, mechanisms[i].reserved, 3);
	}
	for (i = 0; i < source->counts[SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_EDGES]; ++i)
	{
		p = SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_EDGES, i);
		sg_wire_put_u32(p, edges[i].source.entity_ordinal);
		sg_wire_put_u32(p + 4, edges[i].destination.entity_ordinal);
		sg_wire_put_u32(p + 8, edges[i].fanout_ordinal);
		sg_wire_put_u32(p + 12, (uint32_t)edges[i].kind);
	}
	for (i = 0; i < source->counts[SG_RUNE_COMPACT_WIRE_SECTION_LANDMARKS]; ++i)
	{
		p = SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_LANDMARKS, i);
		sg_wire_put_u32(p, landmarks[i].source.entity_ordinal);
		sg_wire_put_u32(p + 4, landmarks[i].cells.first);
		sg_wire_put_u32(p + 8, landmarks[i].cells.count);
		sg_wire_put_u32(p + 12, landmarks[i].mechanism.value);
		sg_wire_put_vec3(p + 16, &landmarks[i].origin);
		sg_wire_put_bounds(p + 28, &landmarks[i].bounds);
		sg_wire_put_u32(p + 52, (uint32_t)landmarks[i].kind);
		sg_wire_put_u16(p + 56, landmarks[i].variant);
		sg_wire_put_u16(p + 58, landmarks[i].reserved);
	}
	for (i = 0; i < source->counts[SG_RUNE_COMPACT_WIRE_SECTION_LANDMARK_CELLS]; ++i)
		sg_wire_put_u32(SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_LANDMARK_CELLS, i), landmark_cells[i].value);
	for (i = 0; i < source->counts[SG_RUNE_COMPACT_WIRE_SECTION_FACET_ANNOTATIONS]; ++i)
	{
		p = SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_FACET_ANNOTATIONS, i);
		sg_wire_put_u32(p, annotations[i].facet.value);
		sg_wire_put_u16(p + 4, annotations[i].attributes);
		p[6] = annotations[i].hookable_stances;
		p[7] = annotations[i].reserved;
	}
	for (i = 0; i < source->counts[SG_RUNE_COMPACT_WIRE_SECTION_PORTAL_MECHANISMS]; ++i)
	{
		p = SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_PORTAL_MECHANISMS, i);
		sg_wire_put_u32(p, portal_mechanisms[i].portal.value);
		sg_wire_put_u32(p + 4, portal_mechanisms[i].mechanism.value);
		sg_wire_put_u32(p + 8, (uint32_t)portal_mechanisms[i].kind);
		memcpy(p + 12, portal_mechanisms[i].reserved, 3);
	}
#undef SG_WIRE_RECORD
}

int SG_RuneCompactWireEncode(const sg_rune_compact_model_t *model,
	void *dest, size_t dest_size, size_t *written_out,
	sg_rune_compact_wire_error_t *error_out)
{
	sg_wire_source_t source;
	sg_wire_desc_t descs[SG_RUNE_COMPACT_WIRE_SECTION_COUNT];
	uint64_t total;
	uint32_t section;
	uint8_t *image = dest;
	sg_rune_compact_wire_info_t info;
	sg_wire_clear_error(error_out);
	if (written_out == NULL)
	{
		sg_wire_set_error(error_out, SG_RUNE_COMPACT_WIRE_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_WIRE_SECTION_COUNT, UINT32_MAX);
		return 0;
	}
	*written_out = 0;
	if (!sg_wire_collect(model, &source, error_out))
		return 0;
	if (!sg_wire_layout(source.counts, descs, &total) || total > SIZE_MAX)
	{
		sg_wire_set_error(error_out, SG_RUNE_COMPACT_WIRE_ERROR_OVERFLOW,
			SG_RUNE_COMPACT_WIRE_SECTION_COUNT, UINT32_MAX);
		return 0;
	}
	if (image == NULL || dest_size < (size_t)total)
	{
		sg_wire_set_error(error_out,
			image == NULL ? SG_RUNE_COMPACT_WIRE_ERROR_INVALID_ARGUMENT :
			SG_RUNE_COMPACT_WIRE_ERROR_TRUNCATED,
			SG_RUNE_COMPACT_WIRE_SECTION_COUNT, UINT32_MAX);
		return 0;
	}
	memset(image, 0, (size_t)total);
	memcpy(image, sg_wire_magic, sizeof(sg_wire_magic));
	sg_wire_put_u16(image + 8, SG_RUNE_COMPACT_WIRE_VERSION);
	sg_wire_put_u16(image + 10, (uint16_t)SG_WIRE_HEADER_SIZE);
	sg_wire_put_u32(image + 12, (uint32_t)SG_RUNE_COMPACT_WIRE_SECTION_COUNT);
	sg_wire_put_u64(image + 16, total);
	sg_wire_put_u16(image + 32, model->version);
	sg_wire_put_u16(image + 34, model->reserved);
	sg_wire_put_u32(image + 36, model->schema_tag);
	sg_wire_put_u16(image + 40, model->analytic->version);
	sg_wire_put_u16(image + 42, model->analytic->reserved);
	for (section = 0; section < (uint32_t)SG_RUNE_COMPACT_WIRE_SECTION_COUNT;
		++section)
	{
		uint8_t *descriptor = image + SG_WIRE_HEADER_FIXED_SIZE +
			section * SG_WIRE_DESCRIPTOR_SIZE;
		sg_wire_put_u32(descriptor, section);
		sg_wire_put_u32(descriptor + 4, sg_wire_specs[section].wire_size);
		sg_wire_put_u32(descriptor + 8, descs[section].count);
		sg_wire_put_u64(descriptor + 16, descs[section].offset);
	}
	sg_wire_encode_arrays(&source, descs, image);
	sg_wire_put_u32(image + SG_WIRE_CHECKSUM_OFFSET,
		sg_wire_checksum(image, (size_t)total));
	if (!SG_RuneCompactWireInspect(image, (size_t)total, &info, error_out))
		return 0;
	*written_out = (size_t)total;
	return 1;
}

static int sg_wire_record_error(sg_rune_compact_wire_error_t *error,
	sg_rune_compact_wire_error_code_t code, uint32_t section, uint32_t record)
{
	sg_wire_set_error(error, code, (sg_rune_compact_wire_section_t)section,
		record);
	return 0;
}

static int sg_wire_validate_records(const uint8_t *image,
	const sg_wire_desc_t *descs, sg_rune_compact_wire_error_t *error)
{
	const uint8_t *p;
	const uint8_t *profile_record;
	uint32_t i;
	uint32_t form;
	uint32_t definition_limit;
#define SG_COUNT(section_name) descs[section_name].count
#define SG_RECORD(section_name, ordinal) \
	(image + descs[section_name].offset + \
	 (uint64_t)(ordinal) * sg_wire_specs[section_name].wire_size)
#define SG_FAIL(code, section_name, ordinal) \
	return sg_wire_record_error(error, code, section_name, ordinal)
	for (i = 0; i < SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_CELLS); ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_CELLS, i);
		if (!sg_wire_span(sg_wire_u32(p + 44), sg_wire_u32(p + 48),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_CELL_INCIDENCES)) ||
			!sg_wire_span(sg_wire_u32(p + 52), sg_wire_u32(p + 56),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIELDS)) ||
			!sg_wire_span(sg_wire_u32(p + 60), sg_wire_u32(p + 64),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_REGIONS)))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_SPAN,
				SG_RUNE_COMPACT_WIRE_SECTION_CELLS, i);
		if (!sg_wire_zero(p + 77, 3))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_NONZERO_RESERVED,
				SG_RUNE_COMPACT_WIRE_SECTION_CELLS, i);
		if ((sg_wire_u32(p + 68) &
			(uint32_t)~(uint32_t)SG_RUNE_COMPACT_CONTENTS_KNOWN) != 0 ||
			(sg_wire_u32(p + 72) &
			 (uint32_t)~(uint32_t)SG_RUNE_COMPACT_CELL_SEMANTICS_KNOWN) != 0 ||
			(p[76] & (uint8_t)~SG_RUNE_STANCE_VALID_ALL) != 0)
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
				SG_RUNE_COMPACT_WIRE_SECTION_CELLS, i);
	}
	for (i = 0; i < SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_FACETS); ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_FACETS, i);
		if (!sg_wire_validate_source(p,
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_FACETS)))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_FACETS, i);
		if (!sg_wire_span(sg_wire_u32(p + 36), sg_wire_u32(p + 40),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_VERTICES)) ||
			!sg_wire_span(sg_wire_u32(p + 44), sg_wire_u32(p + 48),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_INCIDENCES)))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_SPAN,
				SG_RUNE_COMPACT_WIRE_SECTION_FACETS, i);
		if (!sg_wire_ref(sg_wire_u32(p + 52),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_PORTALS), 1))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_FACETS, i);
	}
	for (i = 0; i < SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_INCIDENCES); ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_INCIDENCES, i);
		if (!sg_wire_ref(sg_wire_u32(p),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_CELLS), 0) ||
			!sg_wire_ref(sg_wire_u32(p + 4),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_FACETS), 0))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_INCIDENCES, i);
		if (sg_wire_u32(p + 12) >= (uint32_t)SG_RUNE_FACET_SIDE_COUNT ||
			sg_wire_u32(p + 16) >= (uint32_t)SG_RUNE_BOUNDARY_OWNERSHIP_COUNT)
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
				SG_RUNE_COMPACT_WIRE_SECTION_INCIDENCES, i);
	}
	for (i = 0; i < SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_CELL_INCIDENCES); ++i)
		if (!sg_wire_ref(sg_wire_u32(SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_CELL_INCIDENCES, i)),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_INCIDENCES), 0))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_CELL_INCIDENCES, i);
	for (i = 0; i < SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_PORTALS); ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_PORTALS, i);
		if (!sg_wire_validate_source(p,
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_FACETS)) ||
			!sg_wire_ref(sg_wire_u32(p + 20),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_FACETS), 0) ||
			!sg_wire_ref(sg_wire_u32(p + 24),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_INCIDENCES), 0) ||
			!sg_wire_ref(sg_wire_u32(p + 28),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_INCIDENCES), 0))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_PORTALS, i);
		if (sg_wire_u32(p + 36) >= (uint32_t)SG_RUNE_PORTAL_CONTINUITY_COUNT)
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
				SG_RUNE_COMPACT_WIRE_SECTION_PORTALS, i);
		if (!sg_wire_zero(p + 41, 3))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_NONZERO_RESERVED,
				SG_RUNE_COMPACT_WIRE_SECTION_PORTALS, i);
		if ((p[40] & (uint8_t)~SG_RUNE_STANCE_VALID_ALL) != 0)
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
				SG_RUNE_COMPACT_WIRE_SECTION_PORTALS, i);
	}
	for (i = 0; i < SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIELDS); ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIELDS, i);
		if (!sg_wire_ref(sg_wire_u32(p),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_CELLS), 0) ||
			!sg_wire_ref(sg_wire_u32(p + 4),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_PORTALS), 1))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIELDS, i);
		if (sg_wire_u32(p + 8) >= (uint32_t)SG_RUNE_MOVEMENT_FIELD_FAMILY_COUNT)
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
				SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIELDS, i);
		if (!sg_wire_zero(p + 13, 3))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_NONZERO_RESERVED,
				SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIELDS, i);
		if ((p[12] & (uint8_t)~SG_RUNE_STANCE_VALID_ALL) != 0)
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
				SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIELDS, i);
		if (!sg_wire_span(sg_wire_u32(p + 16), sg_wire_u32(p + 20),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_FUNCTION_REFS)))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_SPAN,
				SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIELDS, i);
	}
	for (i = 0; i < SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_REGIONS); ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_REGIONS, i);
		if (!sg_wire_ref(sg_wire_u32(p),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_CELLS), 0))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_REGIONS, i);
		if (!sg_wire_span(sg_wire_u32(p + 4), sg_wire_u32(p + 8),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_CELL_INCIDENCES)) ||
			!sg_wire_span(sg_wire_u32(p + 12), sg_wire_u32(p + 16),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_KERNELS)))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_SPAN,
				SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_REGIONS, i);
	}
	for (i = 0; i < SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_PROFILES); ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_PROFILES, i);
		if (sg_wire_u32(p) == 0U || sg_wire_u32(p + 4) == 0U ||
			(sg_wire_u32(p + 4) &
				~SG_RUNE_WEAPON_RESPONSE_FAMILIES_ALL) != 0U)
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
				SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_PROFILES, i);
	}
	for (i = 0; i < SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_KERNELS); ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_KERNELS, i);
		if (!sg_wire_ref(sg_wire_u32(p),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_REGIONS), 0))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_KERNELS, i);
		if (!sg_wire_ref(sg_wire_u32(p + 4),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_PROFILES), 0))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_KERNELS, i);
		if (sg_wire_u32(p + 8) >=
			(uint32_t)SG_RUNE_WEAPON_RESPONSE_FAMILY_COUNT)
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
				SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_KERNELS, i);
		profile_record = SG_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_PROFILES,
			sg_wire_u32(p + 4));
		if ((sg_wire_u32(profile_record + 4) &
			SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(sg_wire_u32(p + 8))) == 0U)
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_KERNELS, i);
		if (!sg_wire_span(sg_wire_u32(p + 12), sg_wire_u32(p + 16),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_FUNCTION_REFS)))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_SPAN,
				SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_KERNELS, i);
	}
	for (i = 0; i < SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_FUNCTION_REFS); ++i)
		if (!sg_wire_ref(sg_wire_u32(SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_FUNCTION_REFS, i)),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_FUNCTIONS), 0))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_FUNCTION_REFS, i);
	for (i = 0; i < SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_FUNCTIONS); ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_FUNCTIONS, i);
		if (!sg_wire_span(sg_wire_u32(p), sg_wire_u32(p + 4),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_INPUT_DIMENSIONS)) ||
			sg_wire_u32(p + 4) > SG_RUNE_ANALYTIC_MAX_INPUTS)
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_SPAN,
				SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_FUNCTIONS, i);
		if (sg_wire_u32(p + 12) >= (uint32_t)SG_RUNE_ANALYTIC_OUTPUT_MEANING_COUNT)
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
				SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_FUNCTIONS, i);
		form = sg_wire_u32(p + 16);
		if (form >= (uint32_t)SG_RUNE_COMPACT_ANALYTIC_FORM_COUNT)
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
				SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_FUNCTIONS, i);
		switch ((sg_rune_compact_analytic_form_t)form)
		{
		case SG_RUNE_COMPACT_ANALYTIC_CONSTANT:
			definition_limit = SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_CONSTANTS);
			break;
		case SG_RUNE_COMPACT_ANALYTIC_AFFINE:
			definition_limit = SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_AFFINES);
			break;
		case SG_RUNE_COMPACT_ANALYTIC_POLYNOMIAL:
			definition_limit = SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_POLYNOMIALS);
			break;
		case SG_RUNE_COMPACT_ANALYTIC_BALLISTIC:
			definition_limit = SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_BALLISTICS);
			break;
		default:
			definition_limit = SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_PIECEWISE);
			break;
		}
		if (!sg_wire_ref(sg_wire_u32(p + 8), definition_limit, 0))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_FUNCTIONS, i);
	}
	for (i = 0; i < SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_INPUT_DIMENSIONS); ++i)
		if (sg_wire_u32(SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_INPUT_DIMENSIONS, i)) >=
			(uint32_t)SG_RUNE_ANALYTIC_INPUT_DIMENSION_COUNT)
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
				SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_INPUT_DIMENSIONS, i);
	for (i = 0; i < SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_AFFINES); ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_AFFINES, i);
		if (!sg_wire_span(sg_wire_u32(p + 4), sg_wire_u32(p + 8),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_AFFINE_SLOPES)))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_SPAN,
				SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_AFFINES, i);
	}
	for (i = 0; i < SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_POLYNOMIALS); ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_POLYNOMIALS, i);
		if (!sg_wire_span(sg_wire_u32(p), sg_wire_u32(p + 4),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_POLYNOMIAL_COEFFICIENTS)))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_SPAN,
				SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_POLYNOMIALS, i);
		if (!sg_wire_zero(p + 9, 3))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_NONZERO_RESERVED,
				SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_POLYNOMIALS, i);
	}
	for (i = 0; i < SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_PIECEWISE); ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_PIECEWISE, i);
		if (!sg_wire_span(sg_wire_u32(p), sg_wire_u32(p + 4),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_PIECEWISE_CLAUSES)))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_SPAN,
				SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_PIECEWISE, i);
		if (!sg_wire_ref(sg_wire_u32(p + 8),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_FUNCTIONS), 0))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_PIECEWISE, i);
	}
	for (i = 0; i < SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_PIECEWISE_CLAUSES); ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_PIECEWISE_CLAUSES, i);
		if (!sg_wire_ref(sg_wire_u32(p + 8),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_FUNCTIONS), 0))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_PIECEWISE_CLAUSES, i);
		if (sg_wire_u32(p + 12) >= (uint32_t)SG_RUNE_ANALYTIC_INTERVAL_OWNERSHIP_COUNT)
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
				SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_PIECEWISE_CLAUSES, i);
	}
	for (i = 0; i < SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_MECHANISMS); ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_MECHANISMS, i);
		if (!sg_wire_ref(sg_wire_u32(p + 8),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_CELLS), 0) ||
			!sg_wire_ref(sg_wire_u32(p + 12),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_CELLS), 0) ||
			!sg_wire_ref(sg_wire_u32(p + 16),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_LANDMARKS), 1))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_MECHANISMS, i);
		if (!sg_wire_span(sg_wire_u32(p + 44), sg_wire_u32(p + 48),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_EDGES)))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_SPAN,
				SG_RUNE_COMPACT_WIRE_SECTION_MECHANISMS, i);
		if (sg_wire_u32(p + 72) >= (uint32_t)SG_RUNE_COMPACT_MECHANISM_KIND_COUNT ||
			sg_wire_u32(p + 76) >= (uint32_t)SG_RUNE_COMPACT_MECHANISM_ACTIVATION_COUNT ||
			sg_wire_u32(p + 80) >= (uint32_t)SG_RUNE_COMPACT_MECHANISM_STATE_COUNT ||
			sg_wire_u32(p + 84) >= (uint32_t)SG_RUNE_COMPACT_MECHANISM_STATE_COUNT ||
			sg_wire_u32(p + 88) >= (uint32_t)SG_RUNE_COMPACT_MECHANISM_STATE_COUNT ||
			sg_wire_u32(p + 92) >= (uint32_t)SG_RUNE_COMPACT_MECHANISM_RECOVERY_COUNT)
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
				SG_RUNE_COMPACT_WIRE_SECTION_MECHANISMS, i);
		if (!sg_wire_zero(p + 97, 3))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_NONZERO_RESERVED,
				SG_RUNE_COMPACT_WIRE_SECTION_MECHANISMS, i);
		if ((p[96] & (uint8_t)~SG_RUNE_COMPACT_MECHANISM_FLAGS_KNOWN) != 0)
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
				SG_RUNE_COMPACT_WIRE_SECTION_MECHANISMS, i);
	}
	for (i = 0; i < SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_EDGES); ++i)
		if (sg_wire_u32(SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_EDGES, i) + 12) >=
			(uint32_t)SG_RUNE_COMPACT_MECHANISM_EDGE_KIND_COUNT)
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
				SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_EDGES, i);
	for (i = 0; i < SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_LANDMARKS); ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_LANDMARKS, i);
		if (!sg_wire_span(sg_wire_u32(p + 4), sg_wire_u32(p + 8),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_LANDMARK_CELLS)))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_SPAN,
				SG_RUNE_COMPACT_WIRE_SECTION_LANDMARKS, i);
		if (!sg_wire_ref(sg_wire_u32(p + 12),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_MECHANISMS), 1))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_LANDMARKS, i);
		if (sg_wire_u32(p + 52) >= (uint32_t)SG_RUNE_COMPACT_LANDMARK_KIND_COUNT)
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
				SG_RUNE_COMPACT_WIRE_SECTION_LANDMARKS, i);
		if (sg_wire_u16(p + 58) != 0)
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_NONZERO_RESERVED,
				SG_RUNE_COMPACT_WIRE_SECTION_LANDMARKS, i);
	}
	for (i = 0; i < SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_LANDMARK_CELLS); ++i)
		if (!sg_wire_ref(sg_wire_u32(SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_LANDMARK_CELLS, i)),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_CELLS), 0))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_LANDMARK_CELLS, i);
	for (i = 0; i < SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_FACET_ANNOTATIONS); ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_FACET_ANNOTATIONS, i);
		if (!sg_wire_ref(sg_wire_u32(p),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_FACETS), 0))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_FACET_ANNOTATIONS, i);
		if (p[7] != 0)
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_NONZERO_RESERVED,
				SG_RUNE_COMPACT_WIRE_SECTION_FACET_ANNOTATIONS, i);
		if ((sg_wire_u16(p + 4) &
			(uint16_t)~SG_RUNE_COMPACT_FACET_ATTRIBUTES_KNOWN) != 0 ||
			(p[6] & (uint8_t)~SG_RUNE_STANCE_VALID_ALL) != 0)
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
				SG_RUNE_COMPACT_WIRE_SECTION_FACET_ANNOTATIONS, i);
	}
	for (i = 0; i < SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_PORTAL_MECHANISMS); ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_PORTAL_MECHANISMS, i);
		if (!sg_wire_ref(sg_wire_u32(p),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_PORTALS), 0) ||
			!sg_wire_ref(sg_wire_u32(p + 4),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_MECHANISMS), 0))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_PORTAL_MECHANISMS, i);
		if (sg_wire_u32(p + 8) >= (uint32_t)SG_RUNE_COMPACT_PORTAL_MECHANISM_KIND_COUNT)
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
				SG_RUNE_COMPACT_WIRE_SECTION_PORTAL_MECHANISMS, i);
		if (!sg_wire_zero(p + 12, 3))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_NONZERO_RESERVED,
				SG_RUNE_COMPACT_WIRE_SECTION_PORTAL_MECHANISMS, i);
	}
#undef SG_FAIL
#undef SG_RECORD
#undef SG_COUNT
	return 1;
}

static int sg_wire_read_layout(const uint8_t *image, size_t image_size,
	sg_wire_desc_t *descs, sg_rune_compact_wire_error_t *error)
{
	uint64_t cursor;
	uint64_t total;
	uint32_t section;
	if (image_size < SG_WIRE_HEADER_FIXED_SIZE)
		return sg_wire_record_error(error,
			SG_RUNE_COMPACT_WIRE_ERROR_TRUNCATED,
			SG_RUNE_COMPACT_WIRE_SECTION_COUNT, UINT32_MAX);
	if (memcmp(image, sg_wire_magic, sizeof(sg_wire_magic)) != 0)
		return sg_wire_record_error(error,
			SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
			SG_RUNE_COMPACT_WIRE_SECTION_COUNT, UINT32_MAX);
	if (sg_wire_u16(image + 8) != SG_RUNE_COMPACT_WIRE_VERSION ||
		sg_wire_u16(image + 32) != SG_RUNE_COMPACT_MODEL_VERSION ||
		sg_wire_u32(image + 36) != SG_RUNE_COMPACT_MODEL_SCHEMA_TAG ||
		sg_wire_u16(image + 40) != SG_RUNE_COMPACT_ANALYTIC_VERSION)
		return sg_wire_record_error(error,
			SG_RUNE_COMPACT_WIRE_ERROR_UNSUPPORTED_VERSION,
			SG_RUNE_COMPACT_WIRE_SECTION_COUNT, UINT32_MAX);
	if (sg_wire_u16(image + 10) != (uint16_t)SG_WIRE_HEADER_SIZE ||
		sg_wire_u32(image + 12) !=
			(uint32_t)SG_RUNE_COMPACT_WIRE_SECTION_COUNT)
		return sg_wire_record_error(error,
			SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
			SG_RUNE_COMPACT_WIRE_SECTION_COUNT, UINT32_MAX);
	if (image_size < SG_WIRE_HEADER_SIZE)
		return sg_wire_record_error(error,
			SG_RUNE_COMPACT_WIRE_ERROR_TRUNCATED,
			SG_RUNE_COMPACT_WIRE_SECTION_COUNT, UINT32_MAX);
	total = sg_wire_u64(image + 16);
	if (total != (uint64_t)image_size)
		return sg_wire_record_error(error,
			total > (uint64_t)image_size ? SG_RUNE_COMPACT_WIRE_ERROR_TRUNCATED :
			SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
			SG_RUNE_COMPACT_WIRE_SECTION_COUNT, UINT32_MAX);
	if (sg_wire_u32(image + 28) != 0 || sg_wire_u16(image + 34) != 0 ||
		sg_wire_u16(image + 42) != 0 || sg_wire_u32(image + 44) != 0)
		return sg_wire_record_error(error,
			SG_RUNE_COMPACT_WIRE_ERROR_NONZERO_RESERVED,
			SG_RUNE_COMPACT_WIRE_SECTION_COUNT, UINT32_MAX);
	cursor = SG_WIRE_HEADER_SIZE;
	for (section = 0; section < (uint32_t)SG_RUNE_COMPACT_WIRE_SECTION_COUNT;
		++section)
	{
		const uint8_t *descriptor = image + SG_WIRE_HEADER_FIXED_SIZE +
			section * SG_WIRE_DESCRIPTOR_SIZE;
		uint64_t offset;
		uint64_t aligned;
		if (sg_wire_u32(descriptor) != section ||
			sg_wire_u32(descriptor + 4) != sg_wire_specs[section].wire_size)
			return sg_wire_record_error(error,
				SG_RUNE_COMPACT_WIRE_ERROR_INVALID_SECTION, section, UINT32_MAX);
		if (sg_wire_u32(descriptor + 12) != 0)
			return sg_wire_record_error(error,
				SG_RUNE_COMPACT_WIRE_ERROR_NONZERO_RESERVED, section, UINT32_MAX);
		descs[section].count = sg_wire_u32(descriptor + 8);
		descs[section].offset = sg_wire_u64(descriptor + 16);
		if (descs[section].count > sg_wire_specs[section].limit ||
			(section == SG_RUNE_COMPACT_WIRE_SECTION_IDENTITY &&
			 descs[section].count != 1))
			return sg_wire_record_error(error,
				SG_RUNE_COMPACT_WIRE_ERROR_LIMIT_EXCEEDED, section, UINT32_MAX);
		if (!sg_wire_align(cursor, &aligned))
			return sg_wire_record_error(error,
				SG_RUNE_COMPACT_WIRE_ERROR_OVERFLOW, section, UINT32_MAX);
		offset = descs[section].offset;
		if (offset != aligned || offset > total)
			return sg_wire_record_error(error,
				SG_RUNE_COMPACT_WIRE_ERROR_INVALID_SECTION, section, UINT32_MAX);
		if (!sg_wire_zero(image + (size_t)cursor, (size_t)(aligned - cursor)))
			return sg_wire_record_error(error,
				SG_RUNE_COMPACT_WIRE_ERROR_NONZERO_RESERVED, section, UINT32_MAX);
		cursor = offset;
		if (!sg_wire_add_product(&cursor, descs[section].count,
			sg_wire_specs[section].wire_size) || cursor > total)
			return sg_wire_record_error(error,
				SG_RUNE_COMPACT_WIRE_ERROR_OVERFLOW, section, UINT32_MAX);
	}
	{
		uint64_t aligned;
		if (!sg_wire_align(cursor, &aligned))
			return sg_wire_record_error(error,
				SG_RUNE_COMPACT_WIRE_ERROR_OVERFLOW,
				SG_RUNE_COMPACT_WIRE_SECTION_COUNT, UINT32_MAX);
		if (aligned != total ||
		!sg_wire_zero(image + (size_t)cursor,
			(size_t)(total - cursor)))
			return sg_wire_record_error(error,
				SG_RUNE_COMPACT_WIRE_ERROR_NONZERO_RESERVED,
				SG_RUNE_COMPACT_WIRE_SECTION_COUNT, UINT32_MAX);
	}
	return 1;
}

int SG_RuneCompactWireInspect(const void *image_value, size_t image_size,
	sg_rune_compact_wire_info_t *info_out,
	sg_rune_compact_wire_error_t *error_out)
{
	const uint8_t *image = image_value;
	sg_wire_desc_t descs[SG_RUNE_COMPACT_WIRE_SECTION_COUNT];
	uint32_t section;
	sg_wire_clear_error(error_out);
	if (image == NULL)
	{
		sg_wire_set_error(error_out, SG_RUNE_COMPACT_WIRE_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_WIRE_SECTION_COUNT, UINT32_MAX);
		return 0;
	}
	if (!sg_wire_read_layout(image, image_size, descs, error_out))
		return 0;
	if (sg_wire_u32(image + SG_WIRE_CHECKSUM_OFFSET) !=
		sg_wire_checksum(image, image_size))
	{
		sg_wire_set_error(error_out,
			SG_RUNE_COMPACT_WIRE_ERROR_CHECKSUM_MISMATCH,
			SG_RUNE_COMPACT_WIRE_SECTION_COUNT, UINT32_MAX);
		return 0;
	}
	if (!sg_wire_validate_records(image, descs, error_out))
		return 0;
	if (info_out != NULL)
	{
		memset(info_out, 0, sizeof(*info_out));
		info_out->wire_version = sg_wire_u16(image + 8);
		info_out->model_version = sg_wire_u16(image + 32);
		info_out->analytic_version = sg_wire_u16(image + 40);
		info_out->schema_tag = sg_wire_u32(image + 36);
		info_out->image_bytes = sg_wire_u64(image + 16);
		info_out->checksum = sg_wire_u32(image + SG_WIRE_CHECKSUM_OFFSET);
		sg_wire_get_identity(image +
			descs[SG_RUNE_COMPACT_WIRE_SECTION_IDENTITY].offset,
			&info_out->identity);
		for (section = 0;
			section < (uint32_t)SG_RUNE_COMPACT_WIRE_SECTION_COUNT; ++section)
			info_out->counts[section] = descs[section].count;
	}
	return 1;
}

static int sg_wire_storage_add(size_t *total, uint32_t count, size_t size)
{
	size_t aligned = (*total + (size_t)7) & ~(size_t)7;
	if (aligned < *total || (count != 0 && size > (SIZE_MAX - aligned) / count))
		return 0;
	*total = aligned + (size_t)count * size;
	return 1;
}

static void *sg_wire_storage_take(sg_rune_compact_wire_decoded_t *decoded,
	size_t *cursor, uint32_t count, size_t size)
{
	void *result;
	if (count == 0)
		return NULL;
	*cursor = (*cursor + (size_t)7) & ~(size_t)7;
	result = decoded->payload + *cursor;
	*cursor += (size_t)count * size;
	return result;
}

static void sg_wire_decode_arrays(const uint8_t *image,
	const sg_wire_desc_t *descs, sg_rune_compact_wire_decoded_t *decoded,
	size_t *storage_cursor)
{
	sg_rune_compact_model_t *model = &decoded->model;
	sg_rune_compact_analytic_t *analytic = &decoded->analytic;
	sg_rune_compact_static_t *static_data = &decoded->static_data;
	sg_rune_compact_cell_t *cells;
	sg_rune_compact_facet_t *facets;
	sg_rune_compact_incidence_t *incidences;
	sg_rune_compact_incidence_index_t *cell_incidences;
	sg_rune_q8_vec3_t *vertices;
	sg_rune_compact_portal_t *portals;
	sg_rune_movement_field_attachment_t *movement_fields;
	sg_rune_weapon_response_region_t *weapon_regions;
	sg_rune_weapon_profile_t *weapon_profiles;
	sg_rune_weapon_response_kernel_t *weapon_kernels;
	sg_rune_analytic_function_index_t *analytic_refs;
	sg_rune_analytic_function_t *functions;
	sg_rune_analytic_input_dimension_t *dimensions;
	sg_rune_analytic_constant_t *constants;
	sg_rune_analytic_affine_t *affines;
	sg_rune_analytic_scalar_bits_t *slopes;
	sg_rune_analytic_polynomial_t *polynomials;
	sg_rune_analytic_scalar_bits_t *coefficients;
	sg_rune_analytic_ballistic_t *ballistics;
	sg_rune_analytic_piecewise_t *piecewise;
	sg_rune_analytic_piecewise_clause_t *clauses;
	sg_rune_compact_mechanism_t *mechanisms;
	sg_rune_compact_mechanism_edge_t *edges;
	sg_rune_compact_landmark_t *landmarks;
	sg_rune_compact_cell_index_t *landmark_cells;
	sg_rune_compact_facet_annotation_t *annotations;
	sg_rune_compact_portal_mechanism_t *portal_mechanisms;
	const uint8_t *p;
	uint32_t i;

#define SG_TAKE(section_name, type_name) \
	(type_name *)sg_wire_storage_take(decoded, storage_cursor, \
		descs[section_name].count, sizeof(type_name))
#define SG_RECORD(section_name, ordinal) \
	(image + descs[section_name].offset + \
	 (uint64_t)(ordinal) * sg_wire_specs[section_name].wire_size)
	cells = SG_TAKE(SG_RUNE_COMPACT_WIRE_SECTION_CELLS, sg_rune_compact_cell_t);
	facets = SG_TAKE(SG_RUNE_COMPACT_WIRE_SECTION_FACETS, sg_rune_compact_facet_t);
	incidences = SG_TAKE(SG_RUNE_COMPACT_WIRE_SECTION_INCIDENCES, sg_rune_compact_incidence_t);
	cell_incidences = SG_TAKE(SG_RUNE_COMPACT_WIRE_SECTION_CELL_INCIDENCES, sg_rune_compact_incidence_index_t);
	vertices = SG_TAKE(SG_RUNE_COMPACT_WIRE_SECTION_VERTICES, sg_rune_q8_vec3_t);
	portals = SG_TAKE(SG_RUNE_COMPACT_WIRE_SECTION_PORTALS, sg_rune_compact_portal_t);
	movement_fields = SG_TAKE(SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIELDS, sg_rune_movement_field_attachment_t);
	weapon_regions = SG_TAKE(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_REGIONS, sg_rune_weapon_response_region_t);
	weapon_profiles = SG_TAKE(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_PROFILES, sg_rune_weapon_profile_t);
	weapon_kernels = SG_TAKE(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_KERNELS, sg_rune_weapon_response_kernel_t);
	analytic_refs = SG_TAKE(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_FUNCTION_REFS, sg_rune_analytic_function_index_t);
	functions = SG_TAKE(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_FUNCTIONS, sg_rune_analytic_function_t);
	dimensions = SG_TAKE(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_INPUT_DIMENSIONS, sg_rune_analytic_input_dimension_t);
	constants = SG_TAKE(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_CONSTANTS, sg_rune_analytic_constant_t);
	affines = SG_TAKE(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_AFFINES, sg_rune_analytic_affine_t);
	slopes = SG_TAKE(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_AFFINE_SLOPES, sg_rune_analytic_scalar_bits_t);
	polynomials = SG_TAKE(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_POLYNOMIALS, sg_rune_analytic_polynomial_t);
	coefficients = SG_TAKE(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_POLYNOMIAL_COEFFICIENTS, sg_rune_analytic_scalar_bits_t);
	ballistics = SG_TAKE(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_BALLISTICS, sg_rune_analytic_ballistic_t);
	piecewise = SG_TAKE(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_PIECEWISE, sg_rune_analytic_piecewise_t);
	clauses = SG_TAKE(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_PIECEWISE_CLAUSES, sg_rune_analytic_piecewise_clause_t);
	mechanisms = SG_TAKE(SG_RUNE_COMPACT_WIRE_SECTION_MECHANISMS, sg_rune_compact_mechanism_t);
	edges = SG_TAKE(SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_EDGES, sg_rune_compact_mechanism_edge_t);
	landmarks = SG_TAKE(SG_RUNE_COMPACT_WIRE_SECTION_LANDMARKS, sg_rune_compact_landmark_t);
	landmark_cells = SG_TAKE(SG_RUNE_COMPACT_WIRE_SECTION_LANDMARK_CELLS, sg_rune_compact_cell_index_t);
	annotations = SG_TAKE(SG_RUNE_COMPACT_WIRE_SECTION_FACET_ANNOTATIONS, sg_rune_compact_facet_annotation_t);
	portal_mechanisms = SG_TAKE(SG_RUNE_COMPACT_WIRE_SECTION_PORTAL_MECHANISMS, sg_rune_compact_portal_mechanism_t);

	model->cells = cells;
	model->cell_count = descs[SG_RUNE_COMPACT_WIRE_SECTION_CELLS].count;
	model->facets = facets;
	model->facet_count = descs[SG_RUNE_COMPACT_WIRE_SECTION_FACETS].count;
	model->incidences = incidences;
	model->incidence_count = descs[SG_RUNE_COMPACT_WIRE_SECTION_INCIDENCES].count;
	model->cell_incidences = cell_incidences;
	model->cell_incidence_count = descs[SG_RUNE_COMPACT_WIRE_SECTION_CELL_INCIDENCES].count;
	model->vertices = vertices;
	model->vertex_count = descs[SG_RUNE_COMPACT_WIRE_SECTION_VERTICES].count;
	model->portals = portals;
	model->portal_count = descs[SG_RUNE_COMPACT_WIRE_SECTION_PORTALS].count;
	model->movement_fields = movement_fields;
	model->movement_field_count = descs[SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIELDS].count;
	model->weapon_regions = weapon_regions;
	model->weapon_region_count = descs[SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_REGIONS].count;
	model->weapon_profiles = weapon_profiles;
	model->weapon_profile_count = descs[SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_PROFILES].count;
	model->weapon_kernels = weapon_kernels;
	model->weapon_kernel_count = descs[SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_KERNELS].count;
	model->analytic_function_refs = analytic_refs;
	model->analytic_function_ref_count = descs[SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_FUNCTION_REFS].count;
	model->analytic = analytic;
	model->static_data = static_data;

	analytic->functions = functions;
	analytic->function_count = descs[SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_FUNCTIONS].count;
	analytic->input_dimensions = dimensions;
	analytic->input_dimension_count = descs[SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_INPUT_DIMENSIONS].count;
	analytic->constants = constants;
	analytic->constant_count = descs[SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_CONSTANTS].count;
	analytic->affines = affines;
	analytic->affine_count = descs[SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_AFFINES].count;
	analytic->affine_slopes = slopes;
	analytic->affine_slope_count = descs[SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_AFFINE_SLOPES].count;
	analytic->polynomials = polynomials;
	analytic->polynomial_count = descs[SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_POLYNOMIALS].count;
	analytic->polynomial_coefficients = coefficients;
	analytic->polynomial_coefficient_count = descs[SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_POLYNOMIAL_COEFFICIENTS].count;
	analytic->ballistics = ballistics;
	analytic->ballistic_count = descs[SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_BALLISTICS].count;
	analytic->piecewise = piecewise;
	analytic->piecewise_count = descs[SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_PIECEWISE].count;
	analytic->piecewise_clauses = clauses;
	analytic->piecewise_clause_count = descs[SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_PIECEWISE_CLAUSES].count;

	static_data->mechanisms = mechanisms;
	static_data->mechanism_count = descs[SG_RUNE_COMPACT_WIRE_SECTION_MECHANISMS].count;
	static_data->mechanism_edges = edges;
	static_data->mechanism_edge_count = descs[SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_EDGES].count;
	static_data->landmarks = landmarks;
	static_data->landmark_count = descs[SG_RUNE_COMPACT_WIRE_SECTION_LANDMARKS].count;
	static_data->landmark_cells = landmark_cells;
	static_data->landmark_cell_count = descs[SG_RUNE_COMPACT_WIRE_SECTION_LANDMARK_CELLS].count;
	static_data->facet_annotations = annotations;
	static_data->facet_annotation_count = descs[SG_RUNE_COMPACT_WIRE_SECTION_FACET_ANNOTATIONS].count;
	static_data->portal_mechanisms = portal_mechanisms;
	static_data->portal_mechanism_count = descs[SG_RUNE_COMPACT_WIRE_SECTION_PORTAL_MECHANISMS].count;

	for (i = 0; i < model->cell_count; ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_CELLS, i);
		cells[i].source.model = sg_wire_u32(p);
		cells[i].source.leaf = sg_wire_u32(p + 4);
		cells[i].source.area = sg_wire_u32(p + 8);
		cells[i].source.cluster = (int32_t)sg_wire_u32(p + 12);
		cells[i].source.split_ordinal = sg_wire_u32(p + 16);
		sg_wire_get_bounds(p + 20, &cells[i].bounds);
		cells[i].incidences.first = sg_wire_u32(p + 44);
		cells[i].incidences.count = sg_wire_u32(p + 48);
		cells[i].movement_fields.first = sg_wire_u32(p + 52);
		cells[i].movement_fields.count = sg_wire_u32(p + 56);
		cells[i].weapon_regions.first = sg_wire_u32(p + 60);
		cells[i].weapon_regions.count = sg_wire_u32(p + 64);
		cells[i].contents = sg_wire_u32(p + 68);
		cells[i].semantics = sg_wire_u32(p + 72);
		cells[i].valid_stances = p[76];
	}
	for (i = 0; i < model->facet_count; ++i)
	{
		uint32_t j;
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_FACETS, i);
		sg_wire_get_source(p, &facets[i].source);
		for (j = 0; j < UINT32_C(3); ++j)
			facets[i].plane.normal_bits[j] = sg_wire_u32(p + 20 + j * 4);
		facets[i].plane.distance_bits = sg_wire_u32(p + 32);
		facets[i].vertices.first = sg_wire_u32(p + 36);
		facets[i].vertices.count = sg_wire_u32(p + 40);
		facets[i].incidences.first = sg_wire_u32(p + 44);
		facets[i].incidences.count = sg_wire_u32(p + 48);
		facets[i].portal.value = sg_wire_u32(p + 52);
	}
	for (i = 0; i < model->incidence_count; ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_INCIDENCES, i);
		incidences[i].cell.value = sg_wire_u32(p);
		incidences[i].facet.value = sg_wire_u32(p + 4);
		incidences[i].cell_ordinal = sg_wire_u32(p + 8);
		incidences[i].side = (sg_rune_facet_side_t)sg_wire_u32(p + 12);
		incidences[i].boundary = (sg_rune_boundary_ownership_t)sg_wire_u32(p + 16);
	}
	for (i = 0; i < model->cell_incidence_count; ++i)
		cell_incidences[i].value = sg_wire_u32(SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_CELL_INCIDENCES, i));
	for (i = 0; i < model->vertex_count; ++i)
		sg_wire_get_vec3(SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_VERTICES, i), &vertices[i]);
	for (i = 0; i < model->portal_count; ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_PORTALS, i);
		sg_wire_get_source(p, &portals[i].source);
		portals[i].facet.value = sg_wire_u32(p + 20);
		portals[i].negative_incidence.value = sg_wire_u32(p + 24);
		portals[i].positive_incidence.value = sg_wire_u32(p + 28);
		portals[i].clearance_q8 = sg_wire_u32(p + 32);
		portals[i].direction = (sg_rune_portal_continuity_t)sg_wire_u32(p + 36);
		portals[i].valid_stances = p[40];
	}
	for (i = 0; i < model->movement_field_count; ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIELDS, i);
		movement_fields[i].cell.value = sg_wire_u32(p);
		movement_fields[i].boundary_portal.value = sg_wire_u32(p + 4);
		movement_fields[i].family = (sg_rune_movement_field_family_t)sg_wire_u32(p + 8);
		movement_fields[i].valid_stances = p[12];
		movement_fields[i].functions.first = sg_wire_u32(p + 16);
		movement_fields[i].functions.count = sg_wire_u32(p + 20);
	}
	for (i = 0; i < model->weapon_region_count; ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_REGIONS, i);
		weapon_regions[i].cell.value = sg_wire_u32(p);
		weapon_regions[i].boundary_incidences.first = sg_wire_u32(p + 4);
		weapon_regions[i].boundary_incidences.count = sg_wire_u32(p + 8);
		weapon_regions[i].kernels.first = sg_wire_u32(p + 12);
		weapon_regions[i].kernels.count = sg_wire_u32(p + 16);
	}
	for (i = 0; i < model->weapon_profile_count; ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_PROFILES, i);
		weapon_profiles[i].source_profile = sg_wire_u32(p);
		weapon_profiles[i].response_families = sg_wire_u32(p + 4);
	}
	for (i = 0; i < model->weapon_kernel_count; ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_KERNELS, i);
		weapon_kernels[i].region.value = sg_wire_u32(p);
		weapon_kernels[i].profile = sg_wire_u32(p + 4);
		weapon_kernels[i].family =
			(sg_rune_weapon_response_family_t)sg_wire_u32(p + 8);
		weapon_kernels[i].functions.first = sg_wire_u32(p + 12);
		weapon_kernels[i].functions.count = sg_wire_u32(p + 16);
	}
	for (i = 0; i < model->analytic_function_ref_count; ++i)
		analytic_refs[i].value = sg_wire_u32(SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_FUNCTION_REFS, i));
	for (i = 0; i < analytic->function_count; ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_FUNCTIONS, i);
		functions[i].inputs.first = sg_wire_u32(p);
		functions[i].inputs.count = sg_wire_u32(p + 4);
		functions[i].definition = sg_wire_u32(p + 8);
		functions[i].output = (sg_rune_analytic_output_meaning_t)sg_wire_u32(p + 12);
		functions[i].form = (sg_rune_compact_analytic_form_t)sg_wire_u32(p + 16);
	}
	for (i = 0; i < analytic->input_dimension_count; ++i)
		dimensions[i] = (sg_rune_analytic_input_dimension_t)sg_wire_u32(SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_INPUT_DIMENSIONS, i));
	for (i = 0; i < analytic->constant_count; ++i)
		constants[i].value.bits = sg_wire_u32(SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_CONSTANTS, i));
	for (i = 0; i < analytic->affine_count; ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_AFFINES, i);
		affines[i].bias.bits = sg_wire_u32(p);
		affines[i].slopes.first = sg_wire_u32(p + 4);
		affines[i].slopes.count = sg_wire_u32(p + 8);
	}
	for (i = 0; i < analytic->affine_slope_count; ++i)
		slopes[i].bits = sg_wire_u32(SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_AFFINE_SLOPES, i));
	for (i = 0; i < analytic->polynomial_count; ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_POLYNOMIALS, i);
		polynomials[i].coefficients.first = sg_wire_u32(p);
		polynomials[i].coefficients.count = sg_wire_u32(p + 4);
		polynomials[i].degree = p[8];
	}
	for (i = 0; i < analytic->polynomial_coefficient_count; ++i)
		coefficients[i].bits = sg_wire_u32(SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_POLYNOMIAL_COEFFICIENTS, i));
	for (i = 0; i < analytic->ballistic_count; ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_BALLISTICS, i);
		ballistics[i].initial.bits = sg_wire_u32(p);
		ballistics[i].first_derivative.bits = sg_wire_u32(p + 4);
		ballistics[i].half_second_derivative.bits = sg_wire_u32(p + 8);
	}
	for (i = 0; i < analytic->piecewise_count; ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_PIECEWISE, i);
		piecewise[i].clauses.first = sg_wire_u32(p);
		piecewise[i].clauses.count = sg_wire_u32(p + 4);
		piecewise[i].default_function.value = sg_wire_u32(p + 8);
		piecewise[i].selector_input = sg_wire_u32(p + 12);
	}
	for (i = 0; i < analytic->piecewise_clause_count; ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_PIECEWISE_CLAUSES, i);
		clauses[i].lower.bits = sg_wire_u32(p);
		clauses[i].upper.bits = sg_wire_u32(p + 4);
		clauses[i].function.value = sg_wire_u32(p + 8);
		clauses[i].ownership = (sg_rune_analytic_interval_ownership_t)sg_wire_u32(p + 12);
	}
	for (i = 0; i < static_data->mechanism_count; ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_MECHANISMS, i);
		mechanisms[i].source.entity_ordinal = sg_wire_u32(p);
		mechanisms[i].controller.entity_ordinal = sg_wire_u32(p + 4);
		mechanisms[i].entry_cell.value = sg_wire_u32(p + 8);
		mechanisms[i].exit_cell.value = sg_wire_u32(p + 12);
		mechanisms[i].activation_landmark.value = sg_wire_u32(p + 16);
		sg_wire_get_bounds(p + 20, &mechanisms[i].bounds);
		mechanisms[i].topology.first = sg_wire_u32(p + 44);
		mechanisms[i].topology.count = sg_wire_u32(p + 48);
		mechanisms[i].delay_ms = sg_wire_u32(p + 52);
		mechanisms[i].dwell_ms = sg_wire_u32(p + 56);
		mechanisms[i].travel_ms = sg_wire_u32(p + 60);
		mechanisms[i].wait_ms = sg_wire_u32(p + 64);
		mechanisms[i].reset_ms = sg_wire_u32(p + 68);
		mechanisms[i].kind = (sg_rune_compact_mechanism_kind_t)sg_wire_u32(p + 72);
		mechanisms[i].activation = (sg_rune_compact_mechanism_activation_t)sg_wire_u32(p + 76);
		mechanisms[i].initial_state = (sg_rune_compact_mechanism_state_t)sg_wire_u32(p + 80);
		mechanisms[i].activated_state = (sg_rune_compact_mechanism_state_t)sg_wire_u32(p + 84);
		mechanisms[i].reset_state = (sg_rune_compact_mechanism_state_t)sg_wire_u32(p + 88);
		mechanisms[i].recovery = (sg_rune_compact_mechanism_recovery_t)sg_wire_u32(p + 92);
		mechanisms[i].flags = p[96];
	}
	for (i = 0; i < static_data->mechanism_edge_count; ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_EDGES, i);
		edges[i].source.entity_ordinal = sg_wire_u32(p);
		edges[i].destination.entity_ordinal = sg_wire_u32(p + 4);
		edges[i].fanout_ordinal = sg_wire_u32(p + 8);
		edges[i].kind = (sg_rune_compact_mechanism_edge_kind_t)sg_wire_u32(p + 12);
	}
	for (i = 0; i < static_data->landmark_count; ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_LANDMARKS, i);
		landmarks[i].source.entity_ordinal = sg_wire_u32(p);
		landmarks[i].cells.first = sg_wire_u32(p + 4);
		landmarks[i].cells.count = sg_wire_u32(p + 8);
		landmarks[i].mechanism.value = sg_wire_u32(p + 12);
		sg_wire_get_vec3(p + 16, &landmarks[i].origin);
		sg_wire_get_bounds(p + 28, &landmarks[i].bounds);
		landmarks[i].kind = (sg_rune_compact_landmark_kind_t)sg_wire_u32(p + 52);
		landmarks[i].variant = sg_wire_u16(p + 56);
	}
	for (i = 0; i < static_data->landmark_cell_count; ++i)
		landmark_cells[i].value = sg_wire_u32(SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_LANDMARK_CELLS, i));
	for (i = 0; i < static_data->facet_annotation_count; ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_FACET_ANNOTATIONS, i);
		annotations[i].facet.value = sg_wire_u32(p);
		annotations[i].attributes = sg_wire_u16(p + 4);
		annotations[i].hookable_stances = p[6];
	}
	for (i = 0; i < static_data->portal_mechanism_count; ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_PORTAL_MECHANISMS, i);
		portal_mechanisms[i].portal.value = sg_wire_u32(p);
		portal_mechanisms[i].mechanism.value = sg_wire_u32(p + 4);
		portal_mechanisms[i].kind = (sg_rune_compact_portal_mechanism_kind_t)sg_wire_u32(p + 8);
	}
#undef SG_RECORD
#undef SG_TAKE
}

int SG_RuneCompactWireDecode(const void *image_value, size_t image_size,
	const sg_rune_compact_identity_t *expected_identity,
	sg_rune_compact_wire_decoded_t **decoded_out,
	sg_rune_compact_wire_error_t *error_out)
{
	const uint8_t *image = image_value;
	sg_wire_desc_t descs[SG_RUNE_COMPACT_WIRE_SECTION_COUNT];
	sg_rune_compact_wire_decoded_t *decoded;
	size_t storage_size = 0;
	size_t storage_cursor = 0;
	sg_rune_compact_error_t model_error;
	uint32_t section;
	static const size_t host_sizes[SG_RUNE_COMPACT_WIRE_SECTION_COUNT] = {
		0,
		sizeof(sg_rune_compact_cell_t),
		sizeof(sg_rune_compact_facet_t),
		sizeof(sg_rune_compact_incidence_t),
		sizeof(sg_rune_compact_incidence_index_t),
		sizeof(sg_rune_q8_vec3_t),
		sizeof(sg_rune_compact_portal_t),
		sizeof(sg_rune_movement_field_attachment_t),
		sizeof(sg_rune_weapon_response_region_t),
		sizeof(sg_rune_weapon_profile_t),
		sizeof(sg_rune_weapon_response_kernel_t),
		sizeof(sg_rune_analytic_function_index_t),
		sizeof(sg_rune_analytic_function_t),
		sizeof(sg_rune_analytic_input_dimension_t),
		sizeof(sg_rune_analytic_constant_t),
		sizeof(sg_rune_analytic_affine_t),
		sizeof(sg_rune_analytic_scalar_bits_t),
		sizeof(sg_rune_analytic_polynomial_t),
		sizeof(sg_rune_analytic_scalar_bits_t),
		sizeof(sg_rune_analytic_ballistic_t),
		sizeof(sg_rune_analytic_piecewise_t),
		sizeof(sg_rune_analytic_piecewise_clause_t),
		sizeof(sg_rune_compact_mechanism_t),
		sizeof(sg_rune_compact_mechanism_edge_t),
		sizeof(sg_rune_compact_landmark_t),
		sizeof(sg_rune_compact_cell_index_t),
		sizeof(sg_rune_compact_facet_annotation_t),
		sizeof(sg_rune_compact_portal_mechanism_t)
	};
	sg_wire_clear_error(error_out);
	if (decoded_out == NULL)
	{
		sg_wire_set_error(error_out, SG_RUNE_COMPACT_WIRE_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_WIRE_SECTION_COUNT, UINT32_MAX);
		return 0;
	}
	*decoded_out = NULL;
	if (expected_identity == NULL)
	{
		sg_wire_set_error(error_out, SG_RUNE_COMPACT_WIRE_ERROR_INVALID_ARGUMENT,
			SG_RUNE_COMPACT_WIRE_SECTION_COUNT, UINT32_MAX);
		return 0;
	}
	if (!SG_RuneCompactWireInspect(image, image_size, NULL, error_out))
		return 0;
	if (!sg_wire_read_layout(image, image_size, descs, error_out))
		return 0;
	for (section = 1; section < (uint32_t)SG_RUNE_COMPACT_WIRE_SECTION_COUNT;
		++section)
		if (!sg_wire_storage_add(&storage_size, descs[section].count,
			host_sizes[section]))
		{
			sg_wire_set_error(error_out, SG_RUNE_COMPACT_WIRE_ERROR_OVERFLOW,
				(sg_rune_compact_wire_section_t)section, UINT32_MAX);
			return 0;
		}
	if (storage_size > SIZE_MAX - sizeof(*decoded))
	{
		sg_wire_set_error(error_out, SG_RUNE_COMPACT_WIRE_ERROR_OVERFLOW,
			SG_RUNE_COMPACT_WIRE_SECTION_COUNT, UINT32_MAX);
		return 0;
	}
	decoded = calloc(1, sizeof(*decoded) + storage_size);
	if (decoded == NULL)
	{
		sg_wire_set_error(error_out, SG_RUNE_COMPACT_WIRE_ERROR_OUT_OF_MEMORY,
			SG_RUNE_COMPACT_WIRE_SECTION_COUNT, UINT32_MAX);
		return 0;
	}
	decoded->model.version = sg_wire_u16(image + 32);
	decoded->model.schema_tag = sg_wire_u32(image + 36);
	decoded->analytic.version = sg_wire_u16(image + 40);
	sg_wire_get_identity(image +
		descs[SG_RUNE_COMPACT_WIRE_SECTION_IDENTITY].offset,
		&decoded->model.identity);
	sg_wire_decode_arrays(image, descs, decoded, &storage_cursor);
	if (!SG_RuneCompactModelValidateBound(&decoded->model, expected_identity,
		&model_error))
	{
		sg_wire_set_model_error(error_out, &model_error);
		free(decoded);
		return 0;
	}
	*decoded_out = decoded;
	return 1;
}

const sg_rune_compact_model_t *SG_RuneCompactWireModel(
	const sg_rune_compact_wire_decoded_t *decoded)
{
	return decoded == NULL ? NULL : &decoded->model;
}

void SG_RuneCompactWireDestroy(sg_rune_compact_wire_decoded_t *decoded)
{
	free(decoded);
}

const char *SG_RuneCompactWireErrorString(
	sg_rune_compact_wire_error_code_t code)
{
	static const char *const messages[] = {
		"no error",
		"invalid argument",
		"unsupported version",
		"truncated image",
		"invalid wire format",
		"wire limit exceeded",
		"wire arithmetic overflow",
		"checksum mismatch",
		"nonzero reserved byte",
		"invalid section",
		"invalid span",
		"invalid reference",
		"out of memory",
		"invalid compact model",
		"compact model identity mismatch"
	};
	_Static_assert(sizeof(messages) / sizeof(messages[0]) ==
		SG_RUNE_COMPACT_WIRE_ERROR_CODE_COUNT,
		"compact wire error table must cover every code");
	if ((uint32_t)code >= (uint32_t)SG_RUNE_COMPACT_WIRE_ERROR_CODE_COUNT)
		return "unknown compact wire error";
	return messages[(uint32_t)code];
}
