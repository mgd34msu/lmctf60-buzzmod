#include "sg_rune_compact_wire.h"
#include "sg_rune_compact_binary32.h"
#include "sg_rune_compact_mechanisms.h"
#include "sg_rune_compact_weapon_field.h"

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
	const sg_rune_compact_model_t *model;
	const void *arrays[SG_RUNE_COMPACT_WIRE_SECTION_COUNT];
	uint32_t counts[SG_RUNE_COMPACT_WIRE_SECTION_COUNT];
	uint8_t response_exact_live_prefire_trace_required;
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
	{ UINT32_C(260), UINT32_C(1) },
	{ UINT32_C(80), SG_RUNE_COMPACT_MAX_CELLS },
	{ UINT32_C(60), SG_RUNE_COMPACT_MAX_FACETS },
	{ UINT32_C(20), SG_RUNE_COMPACT_MAX_INCIDENCES },
	{ UINT32_C(4), SG_RUNE_COMPACT_MAX_INCIDENCES },
	{ UINT32_C(12), SG_RUNE_COMPACT_MAX_VERTICES },
	{ UINT32_C(44), SG_RUNE_COMPACT_MAX_PORTALS },
	{ UINT32_C(24), SG_RUNE_COMPACT_MAX_MOVEMENT_FIELDS },
	{ UINT32_C(24), SG_RUNE_COMPACT_MAX_MOVEMENT_STATES },
	{ UINT32_C(52), SG_RUNE_COMPACT_MAX_MOVEMENT_FIBERS },
	{ UINT32_C(76), SG_RUNE_COMPACT_MAX_MOVEMENT_HOOK_TARGETS },
	{ UINT32_C(4), SG_RUNE_COMPACT_MAX_MOVEMENT_FIBER_FUNCTION_REFS },
	{ UINT32_C(76), SG_RUNE_COMPACT_MAX_MOVEMENT_ANGULAR_SCHEDULES },
	{ UINT32_C(108), UINT32_C(1) },
	{ UINT32_C(88), SG_RUNE_COMPACT_MAX_RESPONSE_FRAGMENTS },
	{ UINT32_C(24), SG_RUNE_COMPACT_MAX_RESPONSE_HALFSPACES },
	{ UINT32_C(128), SG_RUNE_COMPACT_MAX_RESPONSE_PATCHES },
	{ UINT32_C(12), SG_RUNE_COMPACT_MAX_RESPONSE_PATCH_VERTICES },
	{ UINT32_C(40), SG_RUNE_COMPACT_MAX_RESPONSE_SPLITS },
	{ UINT32_C(124), SG_RUNE_COMPACT_MAX_RESPONSE_FACTS },
	{ UINT32_C(24), SG_RUNE_COMPACT_MAX_RESPONSE_CANDIDATE_GROUPS },
	{ UINT32_C(20), SG_RUNE_COMPACT_MAX_RESPONSE_ENDPOINT_GROUPS },
	{ UINT32_C(4), SG_RUNE_COMPACT_MAX_RESPONSE_ENDPOINT_MEMBERS },
	{ UINT32_C(20), SG_RUNE_COMPACT_MAX_RESPONSE_ENDPOINT_GROUPS },
	{ UINT32_C(4), SG_RUNE_COMPACT_MAX_RESPONSE_ENDPOINT_MEMBERS },
	{ UINT32_C(92), UINT32_C(1) },
	{ UINT32_C(16), SG_RUNE_COMPACT_MAX_STATIC_OCCLUDERS },
	{ UINT32_C(16), SG_RUNE_COMPACT_MAX_WEAPON_PROFILES },
	{ UINT32_C(24), SG_RUNE_COMPACT_MAX_WEAPON_KERNELS },
	{ UINT32_C(12), SG_RUNE_COMPACT_MAX_WEAPON_FUNCTION_REFS },
	{ UINT32_C(32), SG_RUNE_COMPACT_MAX_WEAPON_ATTACHMENTS },
	{ UINT32_C(8), SG_RUNE_COMPACT_MAX_WEAPON_RELATION_SPANS },
	{ UINT32_C(8), SG_RUNE_COMPACT_MAX_WEAPON_RELATION_REFS },
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
	{ UINT32_C(152), SG_RUNE_COMPACT_MAX_MECHANISMS },
	{ UINT32_C(52), SG_RUNE_COMPACT_MAX_MECHANISM_CONTROLLERS },
	{ UINT32_C(16), SG_RUNE_COMPACT_MAX_MECHANISM_EDGES },
	{ UINT32_C(248), SG_RUNE_COMPACT_MAX_MECHANISM_TRANSITIONS },
	{ UINT32_C(60), SG_RUNE_COMPACT_MAX_LANDMARKS },
	{ UINT32_C(4), SG_RUNE_COMPACT_MAX_LANDMARK_CELL_REFS },
	{ UINT32_C(16), SG_RUNE_COMPACT_MAX_FACET_ANNOTATIONS },
	{ UINT32_C(16), SG_RUNE_COMPACT_MAX_PORTAL_MECHANISMS },
	{ UINT32_C(56), SG_RUNE_COMPACT_MAX_SOURCE_SURFACES },
	{ UINT32_C(12), SG_RUNE_COMPACT_MAX_SOURCE_SURFACE_VERTICES },
	{ UINT32_C(124), SG_RUNE_COMPACT_MAX_MECHANISM_AUTHORITIES },
	{ UINT32_C(76), SG_RUNE_COMPACT_MAX_MECHANISM_AUTHORITY_CONTROLLERS },
	{ UINT32_C(16), SG_RUNE_COMPACT_MAX_MECHANISM_AUTHORITY_TOPOLOGY_EDGES },
	{ UINT32_C(248), SG_RUNE_COMPACT_MAX_MECHANISM_AUTHORITY_TRANSITIONS },
	{ UINT32_C(4), SG_RUNE_COMPACT_MAX_MECHANISM_AUTHORITY_TRANSITIONS },
	{ UINT32_C(4), SG_RUNE_COMPACT_MAX_MECHANISM_TRANSITIONS }
};

static const char *const sg_wire_section_names[
	SG_RUNE_COMPACT_WIRE_SECTION_COUNT] = {
	"identity",
	"cells",
	"facets",
	"incidences",
	"cell_incidences",
	"vertices",
	"portals",
	"movement_capabilities",
	"movement_states",
	"movement_fibers",
	"movement_hook_targets",
	"movement_fiber_function_refs",
	"movement_angular_schedules",
	"movement_runtime",
	"response_fragments",
	"response_halfspaces",
	"response_patches",
	"response_target_vertices",
	"response_splits",
	"response_facts",
	"response_candidate_groups",
	"response_source_endpoint_groups",
	"response_source_endpoint_members",
	"response_target_endpoint_groups",
	"response_target_endpoint_members",
	"response_seal",
	"static_occluders",
	"weapon_profiles",
	"weapon_kernels",
	"weapon_function_refs",
	"weapon_attachments",
	"weapon_relation_spans",
	"weapon_relation_refs",
	"analytic_functions",
	"analytic_input_dimensions",
	"analytic_constants",
	"analytic_affines",
	"analytic_affine_slopes",
	"analytic_polynomials",
	"analytic_polynomial_coefficients",
	"analytic_ballistics",
	"analytic_piecewise",
	"analytic_piecewise_clauses",
	"mechanisms",
	"mechanism_controllers",
	"mechanism_edges",
	"transitions",
	"landmarks",
	"landmark_cells",
	"facet_annotations",
	"portal_mechanisms",
	"source_surfaces",
	"source_surface_vertices",
	"mechanism_authorities",
	"mechanism_authority_controllers",
	"mechanism_authority_topology_edges",
	"mechanism_authority_transitions",
	"mechanism_authority_transition_static_indices",
	"static_transition_authority_indices"
};

_Static_assert(sizeof(sg_wire_section_names) /
	sizeof(sg_wire_section_names[0]) == SG_RUNE_COMPACT_WIRE_SECTION_COUNT,
	"compact wire section names must cover every section");

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
	case SG_RUNE_COMPACT_RECORD_SOURCE_SURFACE:
		return SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACES;
	case SG_RUNE_COMPACT_RECORD_MOVEMENT_FIELD:
		return SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_CAPABILITIES;
	case SG_RUNE_COMPACT_RECORD_MECHANISM_AUTHORITY:
		return SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITIES;
	case SG_RUNE_COMPACT_RECORD_MECHANISM_CONTROLLER:
		return SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_CONTROLLERS;
	case SG_RUNE_COMPACT_RECORD_MECHANISM_TOPOLOGY:
		return SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TOPOLOGY_EDGES;
	case SG_RUNE_COMPACT_RECORD_MECHANISM_TRANSITION:
		return SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TRANSITIONS;
	case SG_RUNE_COMPACT_RECORD_RESPONSE:
		return SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FACTS;
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
		model_error->code == SG_RUNE_COMPACT_ERROR_OUT_OF_MEMORY ?
		SG_RUNE_COMPACT_WIRE_ERROR_OUT_OF_MEMORY :
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

static int32_t sg_wire_i32(const uint8_t *p)
{
	return (int32_t)sg_wire_u32(p);
}

static int sg_wire_bounds_valid(const uint8_t *p)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; ++axis)
		if (sg_wire_i32(p + axis * 4U) >=
			sg_wire_i32(p + 12U + axis * 4U))
			return 0;
	return 1;
}

static int sg_wire_bounds_closed_valid(const uint8_t *p)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; ++axis)
		if (sg_wire_i32(p + axis * 4U) >
			sg_wire_i32(p + 12U + axis * 4U))
			return 0;
	return 1;
}

static int sg_wire_response_patch_bounds_valid(const uint8_t *patch,
	const uint8_t *vertices)
{
	uint32_t vertex;
	uint32_t axis;
	const uint32_t vertex_count = sg_wire_u32(patch + 80U);

	if (vertex_count < 3U || !sg_wire_bounds_closed_valid(patch + 84U))
		return 0;
	for (axis = 0U; axis < 3U; ++axis) {
		int32_t minimum = sg_wire_i32(vertices + axis * 4U);
		int32_t maximum = minimum;

		for (vertex = 1U; vertex < vertex_count; ++vertex) {
			const int32_t value = sg_wire_i32(vertices + vertex * 12U +
				axis * 4U);

			if (value < minimum)
				minimum = value;
			if (value > maximum)
				maximum = value;
		}
		if (sg_wire_i32(patch + 84U + axis * 4U) != minimum ||
			sg_wire_i32(patch + 96U + axis * 4U) != maximum)
			return 0;
	}
	return 1;
}

static int sg_wire_point_in_bounds(const uint8_t *point,
	const uint8_t *bounds)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; ++axis)
	{
		const int32_t value = sg_wire_i32(point + axis * 4U);

		if (value < sg_wire_i32(bounds + axis * 4U) ||
			value > sg_wire_i32(bounds + 12U + axis * 4U))
			return 0;
	}
	return 1;
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

uint64_t SG_RuneCompactWireImageLimit(void)
{
	uint64_t size = SG_WIRE_HEADER_SIZE;
	uint64_t aligned;
	uint32_t section;

	for (section = 0;
		section < (uint32_t)SG_RUNE_COMPACT_WIRE_SECTION_COUNT; ++section)
	{
		if (!sg_wire_align(size, &aligned) ||
			!sg_wire_add_product(&aligned, sg_wire_specs[section].limit,
				sg_wire_specs[section].wire_size))
			return UINT64_MAX;
		size = aligned;
	}
	if (!sg_wire_align(size, &aligned))
		return UINT64_MAX;
	return aligned < SG_RUNE_COMPACT_WIRE_MAX_IMAGE_BYTES ? aligned :
		SG_RUNE_COMPACT_WIRE_MAX_IMAGE_BYTES;
}

static uint32_t sg_wire_checksum(const uint8_t *data, size_t size)
{
	uint32_t crc = UINT32_MAX;
	size_t i;
	for (i = 0; i < size; ++i)
	{
		uint32_t octet = data[i];
		uint32_t bit;
		if (i >= SG_WIRE_CHECKSUM_OFFSET &&
			i < SG_WIRE_CHECKSUM_OFFSET + UINT32_C(4))
			octet = 0;
		crc ^= octet;
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

static int sg_wire_binary32_canonical_finite(uint32_t bits)
{
	return (bits & UINT32_C(0x7f800000)) != UINT32_C(0x7f800000) &&
		bits != UINT32_C(0x80000000);
}

static int sg_wire_binary32_nonnegative(uint32_t bits)
{
	return sg_wire_binary32_canonical_finite(bits) &&
		(bits & UINT32_C(0x80000000)) == 0U;
}

static int sg_wire_binary32_finite_allow_signed_zero(uint32_t bits)
{
	return (bits & UINT32_C(0x7f800000)) != UINT32_C(0x7f800000);
}

static int sg_wire_source_surface_provenance_compare(const uint8_t *left,
	const uint8_t *right)
{
	uint32_t offset;

	for (offset = 0U; offset < 16U; offset += 4U) {
		const uint32_t left_value = sg_wire_u32(left + offset);
		const uint32_t right_value = sg_wire_u32(right + offset);

		if (left_value != right_value)
			return left_value < right_value ? -1 : 1;
	}
	return 0;
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

static uint32_t sg_wire_float_bits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static float sg_wire_float_from_bits(uint32_t bits)
{
	float value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

/* Published response/source planes are binary32; vertices are Q8.  This is
 * the same inclusive residual bound used by the standalone readers, avoiding
 * a false exact-equality requirement after Q8 quantization. */
static int sg_wire_q8_vertex_on_plane(const uint8_t *plane,
	const uint8_t *vertex)
{
	double residual = -(double)sg_wire_float_from_bits(sg_wire_u32(plane + 12U));
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		residual += (double)sg_wire_float_from_bits(
			sg_wire_u32(plane + axis * 4U)) *
			((double)sg_wire_i32(vertex + axis * 4U) / 8.0);
	return residual >= -0.126 && residual <= 0.126;
}

static int sg_wire_published_plane_valid(const uint8_t *plane)
{
	double length_squared = 0.0;
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++) {
		const uint32_t bits = sg_wire_u32(plane + axis * 4U);
		const double component = (double)sg_wire_float_from_bits(bits);

		if (!sg_wire_binary32_canonical_finite(bits))
			return 0;
		length_squared += component * component;
	}
	return sg_wire_binary32_canonical_finite(sg_wire_u32(plane + 12U)) &&
		length_squared >= 0.9999 && length_squared <= 1.0001;
}

static double sg_wire_abs(double value)
{
	return value < 0.0 ? -value : value;
}

static int sg_wire_response_patch_contains_point(const uint8_t *patch,
	const uint8_t *vertices, const uint8_t *point)
{
	double previous = 0.0;
	double normal[3];
	double residual;
	uint32_t edge;
	uint32_t axis;

	if (!sg_wire_point_in_bounds(point, patch + 84U))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		normal[axis] = (double)sg_wire_float_from_bits(
			sg_wire_u32(patch + 60U + axis * 4U));
	residual = -(double)sg_wire_float_from_bits(sg_wire_u32(patch + 72U));
	for (axis = 0U; axis < 3U; axis++)
		residual += normal[axis] *
			((double)sg_wire_i32(point + axis * 4U) / 8.0);
	if (sg_wire_abs(residual) > 0.126)
		return 0;
	for (edge = 0U; edge < sg_wire_u32(patch + 80U); edge++) {
		const uint8_t *from = vertices + edge * 12U;
		const uint8_t *to = vertices +
			((edge + 1U) % sg_wire_u32(patch + 80U)) * 12U;
		double edge_vector[3];
		double point_vector[3];
		double cross[3];
		double side = 0.0;

		for (axis = 0U; axis < 3U; axis++) {
			edge_vector[axis] = (double)sg_wire_i32(to + axis * 4U) -
				(double)sg_wire_i32(from + axis * 4U);
			point_vector[axis] = (double)sg_wire_i32(point + axis * 4U) -
				(double)sg_wire_i32(from + axis * 4U);
		}
		cross[0] = edge_vector[1] * point_vector[2] -
			edge_vector[2] * point_vector[1];
		cross[1] = edge_vector[2] * point_vector[0] -
			edge_vector[0] * point_vector[2];
		cross[2] = edge_vector[0] * point_vector[1] -
			edge_vector[1] * point_vector[0];
		for (axis = 0U; axis < 3U; axis++)
			side += cross[axis] * normal[axis];
		if (sg_wire_abs(side) <= 0.001)
			continue;
		if (previous != 0.0 && ((previous < 0.0) != (side < 0.0)))
			return 0;
		previous = side;
	}
	return 1;
}

static int sg_wire_response_trace_finite_and_bound(const uint8_t *fact,
	const uint8_t *fragment, const uint8_t *patch, const uint8_t *vertices)
{
	const uint8_t *trace = fact + 48U;
	const float fraction = sg_wire_float_from_bits(sg_wire_u32(trace + 8U));
	uint32_t axis;

	if (sg_wire_u32(trace) != 0U || sg_wire_u32(trace + 4U) != 0U ||
		!sg_wire_binary32_canonical_finite(sg_wire_u32(trace + 8U)) ||
		fraction <= 0.0f || fraction > 1.0f ||
		!sg_wire_binary32_canonical_finite(sg_wire_u32(trace + 36U)) ||
		sg_wire_u32(trace + 56U) != SG_HOST_COLLISION_MODEL_WORLD ||
		sg_wire_u64(trace + 60U) != 0U ||
		!sg_wire_response_patch_contains_point(patch, vertices, fact + 28U))
		return 0;
	for (axis = 0U; axis < 3U; axis++) {
		const double origin = (double)sg_wire_i32(fragment + 60U + axis * 4U) /
			8.0;
		const double target = (double)sg_wire_i32(fact + 28U + axis * 4U) /
			8.0;
		const double expected = origin + (double)fraction *
			(target - origin);
		const uint32_t end_bits = sg_wire_u32(trace + 12U + axis * 4U);
		const uint32_t normal_bits = sg_wire_u32(trace + 24U + axis * 4U);

		if (!sg_wire_binary32_canonical_finite(end_bits) ||
			!sg_wire_binary32_canonical_finite(normal_bits) ||
			sg_wire_abs((double)sg_wire_float_from_bits(end_bits) - expected) >
				0.126)
			return 0;
	}
	return 1;
}

static int sg_wire_response_trace_canonical_no_hit(const uint8_t *trace)
{
	uint32_t axis;

	if (sg_wire_u32(trace) != 0U || sg_wire_u32(trace + 4U) != 0U ||
		sg_wire_u32(trace + 8U) != sg_wire_float_bits(1.0f) ||
		sg_wire_u32(trace + 44U) != 0U ||
		sg_wire_u32(trace + 48U) != SG_HOST_COLLISION_TEXINFO_NONE ||
		sg_wire_u32(trace + 52U) != 0U ||
		sg_wire_u32(trace + 56U) != SG_HOST_COLLISION_MODEL_WORLD ||
		sg_wire_u64(trace + 60U) != 0U ||
		sg_wire_u32(trace + 68U) != SG_HOST_COLLISION_BRUSH_NONE ||
		sg_wire_u32(trace + 72U) != SG_HOST_COLLISION_BRUSH_NONE ||
		sg_wire_u32(trace + 36U) != 0U || sg_wire_u32(trace + 40U) != 0U)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (sg_wire_u32(trace + 24U + axis * 4U) != 0U)
			return 0;
	return 1;
}

static int sg_wire_response_trace_ends_at_target(const uint8_t *trace,
	const uint8_t *target)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		if (sg_wire_abs((double)sg_wire_float_from_bits(
			sg_wire_u32(trace + 12U + axis * 4U)) -
			(double)sg_wire_i32(target + axis * 4U) / 8.0) > 0.126)
			return 0;
	return 1;
}

static int sg_wire_response_trace_plane_matches_split(const uint8_t *trace,
	const uint8_t *split)
{
	double same = 0.0;
	double opposite = 0.0;
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++) {
		const double trace_normal = (double)sg_wire_float_from_bits(
			sg_wire_u32(trace + 24U + axis * 4U));
		const double split_normal = (double)sg_wire_float_from_bits(
			sg_wire_u32(split + axis * 4U));
		const double aligned = sg_wire_abs(trace_normal - split_normal);
		const double opposed = sg_wire_abs(trace_normal + split_normal);

		if (aligned > same)
			same = aligned;
		if (opposed > opposite)
			opposite = opposed;
	}
	return (same <= 0.0001 && sg_wire_abs(
		(double)sg_wire_float_from_bits(sg_wire_u32(trace + 36U)) -
		(double)sg_wire_float_from_bits(sg_wire_u32(split + 12U))) <= 0.001) ||
		(opposite <= 0.0001 && sg_wire_abs(
		(double)sg_wire_float_from_bits(sg_wire_u32(trace + 36U)) +
		(double)sg_wire_float_from_bits(sg_wire_u32(split + 12U))) <= 0.001);
}

/* The tagged transport record carries its authenticated transforms so a
 * reader can reject a relocated endpoint without consulting the decoded
 * static model. */
static int sg_wire_transport_derive_world(const uint8_t *local,
	const uint8_t *origin, const uint8_t *axis, uint32_t world_bits[3])
{
	float origin_value[3];
	float local_value[3];
	float axis_value[3][3];
	float world_value[3];
	uint32_t row;
	uint32_t column;

	for (column = 0U; column < 3U; ++column) {
		if (!sg_wire_binary32_canonical_finite(
			sg_wire_u32(origin + column * 4U)))
			return 0;
		origin_value[column] = sg_wire_float_from_bits(
			sg_wire_u32(origin + column * 4U));
		local_value[column] = (float)sg_wire_i32(
			local + column * 4U) * 0.125f;
	}
	for (row = 0U; row < 3U; ++row)
		for (column = 0U; column < 3U; ++column) {
			const uint32_t bits = sg_wire_u32(axis +
				(row * 3U + column) * 4U);

			if (!sg_wire_binary32_canonical_finite(bits))
				return 0;
			axis_value[row][column] = sg_wire_float_from_bits(bits);
		}
	if (!SG_RuneCompactBinary32TransformPoint(local_value, origin_value,
		(const float (*)[3])axis_value, world_value))
		return 0;
	for (column = 0U; column < 3U; ++column) {
		world_bits[column] = sg_wire_float_bits(world_value[column]);
		if (!sg_wire_binary32_canonical_finite(world_bits[column]))
			return 0;
	}
	return 1;
}

static void sg_wire_put_trace(uint8_t *p,
	const sg_host_collision_trace_t *trace)
{
	uint32_t axis;

	sg_wire_put_u32(p, (uint32_t)trace->allsolid);
	sg_wire_put_u32(p + 4, (uint32_t)trace->startsolid);
	sg_wire_put_u32(p + 8, sg_wire_float_bits(trace->fraction));
	for (axis = 0U; axis < 3U; axis++)
		sg_wire_put_u32(p + 12U + axis * 4U,
			sg_wire_float_bits(trace->end[axis]));
	for (axis = 0U; axis < 3U; axis++)
		sg_wire_put_u32(p + 24U + axis * 4U,
			sg_wire_float_bits(trace->plane.normal[axis]));
	sg_wire_put_u32(p + 36, sg_wire_float_bits(trace->plane.distance));
	sg_wire_put_u32(p + 40, (uint32_t)trace->plane.type);
	sg_wire_put_u32(p + 44, trace->contents);
	sg_wire_put_u32(p + 48, trace->texinfo);
	sg_wire_put_u32(p + 52, (uint32_t)trace->surface_flags);
	sg_wire_put_u32(p + 56, trace->model_index);
	sg_wire_put_u64(p + 60, trace->instance_id);
	sg_wire_put_u32(p + 68, trace->brush);
	sg_wire_put_u32(p + 72, trace->brush_side);
}

static void sg_wire_get_trace(const uint8_t *p,
	sg_host_collision_trace_t *trace)
{
	uint32_t axis;

	memset(trace, 0, sizeof(*trace));
	trace->allsolid = (int)sg_wire_u32(p);
	trace->startsolid = (int)sg_wire_u32(p + 4);
	trace->fraction = sg_wire_float_from_bits(sg_wire_u32(p + 8));
	for (axis = 0U; axis < 3U; axis++)
		trace->end[axis] = sg_wire_float_from_bits(
			sg_wire_u32(p + 12U + axis * 4U));
	for (axis = 0U; axis < 3U; axis++)
		trace->plane.normal[axis] = sg_wire_float_from_bits(
			sg_wire_u32(p + 24U + axis * 4U));
	trace->plane.distance = sg_wire_float_from_bits(sg_wire_u32(p + 36));
	trace->plane.type = (int32_t)sg_wire_u32(p + 40);
	trace->contents = sg_wire_u32(p + 44);
	trace->texinfo = sg_wire_u32(p + 48);
	trace->surface_flags = (int32_t)sg_wire_u32(p + 52);
	trace->model_index = sg_wire_u32(p + 56);
	trace->instance_id = sg_wire_u64(p + 60);
	trace->brush = sg_wire_u32(p + 68);
	trace->brush_side = sg_wire_u32(p + 72);
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
	sg_wire_put_u64(p + 252, identity->weapon_profile_catalog_id);
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
	identity->weapon_profile_catalog_id = sg_wire_u64(p + 252);
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
	source->model = model;
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
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACES,
		model->source_surfaces, model->source_surface_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACE_VERTICES,
		model->source_surface_vertices, model->source_surface_vertex_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_CAPABILITIES,
		model->movement_capabilities, model->movement_capability_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_STATES,
		model->movement_states, model->movement_state_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBERS,
		model->movement_fibers, model->movement_fiber_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_HOOK_TARGETS,
		model->movement_hook_targets, model->movement_hook_target_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBER_FUNCTION_REFS,
		model->movement_fiber_function_refs,
		model->movement_fiber_function_ref_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_ANGULAR_SCHEDULES,
		model->movement_angular_schedules, model->movement_angular_schedule_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_RUNTIME,
		&model->movement_pmove_abi, UINT32_C(1));
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FRAGMENTS,
		model->response.source_fragments, model->response.source_fragment_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_HALFSPACES,
		model->response.source_halfspaces, model->response.source_halfspace_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_PATCHES,
		model->response.target_patches, model->response.target_patch_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_TARGET_VERTICES,
		model->response.target_vertices, model->response.target_vertex_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SPLITS,
		model->response.splits, model->response.split_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FACTS,
		model->response.facts, model->response.fact_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_CANDIDATE_GROUPS,
		model->response.candidate_groups, model->response.candidate_group_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SOURCE_ENDPOINT_GROUPS,
		model->response.source_endpoint_groups,
		model->response.source_endpoint_group_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SOURCE_ENDPOINT_MEMBERS,
		model->response.source_endpoint_members,
		model->response.source_endpoint_member_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_TARGET_ENDPOINT_GROUPS,
		model->response.target_endpoint_groups,
		model->response.target_endpoint_group_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_TARGET_ENDPOINT_MEMBERS,
		model->response.target_endpoint_members,
		model->response.target_endpoint_member_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SEAL,
		&model->response.seal, UINT32_C(1));
	source->response_exact_live_prefire_trace_required =
		model->response.exact_live_prefire_trace_required;
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_STATIC_OCCLUDERS,
		model->response.occluders, model->response.occluder_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_PROFILES, model->weapon_profiles, model->weapon_profile_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_KERNELS, model->weapon_kernels, model->weapon_kernel_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_FUNCTION_REFS, model->weapon_function_refs, model->weapon_function_ref_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_ATTACHMENTS,
		model->weapon_attachments, model->weapon_attachment_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_RELATION_SPANS,
		model->weapon_relation_spans, model->weapon_relation_span_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_RELATION_REFS,
		model->weapon_relation_refs, model->weapon_relation_ref_count);
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
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_CONTROLLERS, static_data->mechanism_controllers, static_data->mechanism_controller_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_EDGES, static_data->mechanism_edges, static_data->mechanism_edge_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS, static_data->transitions, static_data->transition_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_LANDMARKS, static_data->landmarks, static_data->landmark_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_LANDMARK_CELLS, static_data->landmark_cells, static_data->landmark_cell_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_FACET_ANNOTATIONS, static_data->facet_annotations, static_data->facet_annotation_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_PORTAL_MECHANISMS, static_data->portal_mechanisms, static_data->portal_mechanism_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITIES,
		model->mechanism_authorities, model->mechanism_authority_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_CONTROLLERS,
		model->mechanism_authority_controllers,
		model->mechanism_authority_controller_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TOPOLOGY_EDGES,
		model->mechanism_authority_topology_edges,
		model->mechanism_authority_topology_edge_count);
	SG_WIRE_SOURCE(SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TRANSITIONS,
		model->mechanism_authority_transitions,
		model->mechanism_authority_transition_count);
	SG_WIRE_SOURCE(
		SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TRANSITION_STATIC_INDICES,
		model->mechanism_authority_transition_static_indices,
		model->mechanism_authority_transition_count);
	SG_WIRE_SOURCE(
		SG_RUNE_COMPACT_WIRE_SECTION_STATIC_TRANSITION_AUTHORITY_INDICES,
		model->static_transition_authority_indices,
		static_data->transition_count);
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

static void sg_wire_put_authority_transition(uint8_t *p,
	const sg_rune_compact_mechanism_transition_t *transition)
{
	uint32_t axis;

	sg_wire_put_u32(p, transition->mechanism);
	sg_wire_put_u32(p + 4, (uint32_t)transition->kind);
	sg_wire_put_u32(p + 8, transition->entry_cell.value);
	sg_wire_put_u32(p + 12, transition->exit_cell.value);
	sg_wire_put_u32(p + 16, (uint32_t)transition->source_state);
	sg_wire_put_u32(p + 20, (uint32_t)transition->destination_state);
	sg_wire_put_u64(p + 24, transition->elapsed_ms);
	switch (transition->kind)
	{
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE:
		sg_wire_put_u32(p + 32, transition->value.portal_state.portal.value);
		sg_wire_put_u32(p + 36, transition->value.portal_state.mover_model);
		sg_wire_put_u32(p + 40, transition->value.portal_state.delay_ms);
		sg_wire_put_u32(p + 44, transition->value.portal_state.dwell_ms);
		sg_wire_put_u32(p + 48, transition->value.portal_state.pause_ms);
		sg_wire_put_u32(p + 52, transition->value.portal_state.travel_ms);
		sg_wire_put_u32(p + 56, transition->value.portal_state.recovery_ms);
		p[60] = transition->value.portal_state.source_blocked;
		p[61] = transition->value.portal_state.destination_blocked;
		break;
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_TELEPORT:
		sg_wire_put_u32(p + 32,
			transition->value.teleport.destination.entity_ordinal);
		sg_wire_put_u32(p + 36, transition->value.teleport.fanout_ordinal);
		sg_wire_put_vec3(p + 40, &transition->value.teleport.approach_witness);
		sg_wire_put_vec3(p + 52, &transition->value.teleport.entry_witness);
		sg_wire_put_vec3(p + 64, &transition->value.teleport.exit_witness);
		for (axis = 0U; axis < 3U; axis++)
			sg_wire_put_u32(p + 76U + axis * 4U,
				transition->value.teleport.arrival_velocity_bits[axis]);
		break;
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_PUSH:
		sg_wire_put_vec3(p + 32, &transition->value.push.approach_witness);
		sg_wire_put_vec3(p + 44, &transition->value.push.entry_witness);
		sg_wire_put_vec3(p + 56, &transition->value.push.exit_witness);
		for (axis = 0U; axis < 3U; axis++)
			sg_wire_put_u32(p + 68U + axis * 4U,
				transition->value.push.launch_velocity_bits[axis]);
		sg_wire_put_u32(p + 80, transition->value.push.gravity_bits);
		sg_wire_put_u32(p + 84, transition->value.push.flight_ms);
		break;
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT:
	{
		uint32_t row;

		sg_wire_put_u32(p + 32, transition->value.transport.mover_model);
		sg_wire_put_u32(p + 36,
			transition->value.transport.source_surface_ordinal);
		sg_wire_put_vec3(p + 40,
			&transition->value.transport.source_player_local);
		sg_wire_put_vec3(p + 52,
			&transition->value.transport.destination_player_local);
		sg_wire_put_vec3(p + 64,
			&transition->value.transport.source_support_local);
		sg_wire_put_vec3(p + 76,
			&transition->value.transport.destination_support_local);
		for (axis = 0U; axis < 3U; axis++) {
			sg_wire_put_u32(p + 88U + axis * 4U,
				transition->value.transport.source_player_world_bits[axis]);
			sg_wire_put_u32(p + 100U + axis * 4U,
				transition->value.transport.destination_player_world_bits[axis]);
			sg_wire_put_u32(p + 112U + axis * 4U,
				transition->value.transport.source_support_world_bits[axis]);
			sg_wire_put_u32(p + 124U + axis * 4U,
				transition->value.transport.destination_support_world_bits[axis]);
			sg_wire_put_u32(p + 136U + axis * 4U,
				transition->value.transport.source_mover_origin_bits[axis]);
			sg_wire_put_u32(p + 184U + axis * 4U,
				transition->value.transport.destination_mover_origin_bits[axis]);
		}
		for (row = 0U; row < 3U; row++)
			for (axis = 0U; axis < 3U; axis++) {
				sg_wire_put_u32(p + 148U + (row * 3U + axis) * 4U,
					transition->value.transport.source_mover_axis_bits[row][axis]);
				sg_wire_put_u32(p + 196U + (row * 3U + axis) * 4U,
					transition->value.transport.destination_mover_axis_bits[row][axis]);
			}
		sg_wire_put_u32(p + 232,
			transition->value.transport.source_endpoint.entity_ordinal);
		sg_wire_put_u32(p + 236,
			transition->value.transport.destination_endpoint.entity_ordinal);
		sg_wire_put_u32(p + 240, transition->value.transport.fanout_ordinal);
		p[244] = transition->value.transport.swept_static_clear;
		p[245] = transition->value.transport.start_supported;
		p[246] = transition->value.transport.end_supported;
		p[247] = transition->value.transport.stance;
		break;
	}
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_KIND_COUNT:
		break;
	}
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
	if (total > SG_RuneCompactWireImageLimit()) {
		sg_wire_set_error(error_out, SG_RUNE_COMPACT_WIRE_ERROR_LIMIT_EXCEEDED,
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
	const sg_rune_compact_source_surface_t *source_surfaces =
		source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACES];
	const sg_rune_q8_vec3_t *source_surface_vertices =
		source->arrays[
			SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACE_VERTICES];
	const sg_rune_movement_capability_t *movement_capabilities =
		source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_CAPABILITIES];
	const sg_rune_compact_movement_state_t *movement_states =
		source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_STATES];
	const sg_rune_compact_movement_fiber_t *movement_fibers =
		source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBERS];
	const sg_rune_compact_movement_hook_target_t *movement_hook_targets =
		source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_HOOK_TARGETS];
	const sg_rune_analytic_function_index_t *movement_fiber_function_refs =
		source->arrays[
			SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBER_FUNCTION_REFS];
	const sg_rune_compact_movement_angular_schedule_t *movement_angular_schedules =
		source->arrays[
			SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_ANGULAR_SCHEDULES];
	const sg_rune_compact_response_fragment_t *response_fragments =
		source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FRAGMENTS];
	const sg_rune_compact_response_halfspace_t *response_halfspaces =
		source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_HALFSPACES];
	const sg_rune_compact_response_patch_t *response_patches =
		source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_PATCHES];
	const sg_rune_q8_vec3_t *response_target_vertices =
		source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_TARGET_VERTICES];
	const sg_rune_compact_response_split_t *response_splits =
		source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SPLITS];
	const sg_rune_compact_response_fact_t *response_facts =
		source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FACTS];
	const sg_rune_compact_response_candidate_group_t *response_candidates =
		source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_CANDIDATE_GROUPS];
	const sg_rune_compact_response_endpoint_group_t *response_source_groups =
		source->arrays[
			SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SOURCE_ENDPOINT_GROUPS];
	const uint32_t *response_source_members = source->arrays[
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SOURCE_ENDPOINT_MEMBERS];
	const sg_rune_compact_response_endpoint_group_t *response_target_groups =
		source->arrays[
			SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_TARGET_ENDPOINT_GROUPS];
	const uint32_t *response_target_members = source->arrays[
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_TARGET_ENDPOINT_MEMBERS];
	const sg_rune_compact_response_seal_t *response_seal = source->arrays[
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SEAL];
	const sg_rune_compact_static_occluder_t *static_occluders =
		source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_STATIC_OCCLUDERS];
	const sg_rune_weapon_profile_t *weapon_profiles = source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_PROFILES];
	const sg_rune_weapon_response_kernel_t *weapon_kernels = source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_KERNELS];
	const sg_rune_weapon_function_ref_t *weapon_function_refs = source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_FUNCTION_REFS];
	const sg_rune_compact_weapon_field_attachment_t *weapon_attachments =
		source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_ATTACHMENTS];
	const sg_rune_compact_weapon_relation_span_t *weapon_relation_spans =
		source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_RELATION_SPANS];
	const sg_rune_compact_response_ref_t *weapon_relation_refs =
		source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_RELATION_REFS];
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
	const sg_rune_compact_static_mechanism_controller_t *controllers =
		source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_CONTROLLERS];
	const sg_rune_compact_mechanism_edge_t *edges = source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_EDGES];
	const sg_rune_compact_static_transition_t *transitions =
		source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS];
	const sg_rune_compact_landmark_t *landmarks = source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_LANDMARKS];
	const sg_rune_compact_cell_index_t *landmark_cells = source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_LANDMARK_CELLS];
	const sg_rune_compact_facet_annotation_t *annotations = source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_FACET_ANNOTATIONS];
	const sg_rune_compact_portal_mechanism_t *portal_mechanisms = source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_PORTAL_MECHANISMS];
	const sg_rune_compact_mechanism_authority_t *mechanism_authorities =
		source->arrays[SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITIES];
	const sg_rune_compact_mechanism_controller_t *authority_controllers =
		source->arrays[
			SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_CONTROLLERS];
	const sg_rune_compact_mechanism_topology_edge_t *authority_edges =
		source->arrays[
			SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TOPOLOGY_EDGES];
	const sg_rune_compact_mechanism_transition_t *authority_transitions =
		source->arrays[
			SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TRANSITIONS];
	const uint32_t *authority_transition_static_indices = source->arrays[
		SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TRANSITION_STATIC_INDICES];
	const uint32_t *static_transition_authority_indices = source->arrays[
		SG_RUNE_COMPACT_WIRE_SECTION_STATIC_TRANSITION_AUTHORITY_INDICES];

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
			sg_wire_put_u32(p + 60, 0U);
			sg_wire_put_u32(p + 64, 0U);
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
		sg_wire_put_u32(p + 56, (uint32_t)facets[i].kind);
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
	for (i = 0; i < source->counts[
		SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACES]; ++i)
	{
		uint32_t axis;

		p = SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACES, i);
		sg_wire_put_u32(p, source_surfaces[i].source.model);
		sg_wire_put_u32(p + 4, source_surfaces[i].source.brush);
		sg_wire_put_u32(p + 8, source_surfaces[i].source.brush_side);
		sg_wire_put_u32(p + 12, source_surfaces[i].source.plane);
		sg_wire_put_u32(p + 16, (uint32_t)source_surfaces[i].frame);
		sg_wire_put_u32(p + 20, source_surfaces[i].cell.value);
		sg_wire_put_u32(p + 24, source_surfaces[i].parent_surface);
		sg_wire_put_u32(p + 28, source_surfaces[i].split_ordinal);
		for (axis = 0U; axis < 3U; axis++)
			sg_wire_put_u32(p + 32U + axis * 4U,
				source_surfaces[i].plane.normal_bits[axis]);
		sg_wire_put_u32(p + 44, source_surfaces[i].plane.distance_bits);
		sg_wire_put_u32(p + 48, source_surfaces[i].vertices.first);
		sg_wire_put_u32(p + 52, source_surfaces[i].vertices.count);
	}
	for (i = 0; i < source->counts[
		SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACE_VERTICES]; ++i)
		sg_wire_put_vec3(SG_WIRE_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACE_VERTICES, i),
			&source_surface_vertices[i]);
	for (i = 0; i < source->counts[
		SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_CAPABILITIES]; ++i)
	{
		p = SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_CAPABILITIES, i);
		sg_wire_put_u32(p, movement_capabilities[i].cell.value);
		sg_wire_put_u32(p + 4, movement_capabilities[i].boundary_portal.value);
		sg_wire_put_u32(p + 8, (uint32_t)movement_capabilities[i].kind);
		p[12] = movement_capabilities[i].source_stances;
		p[13] = movement_capabilities[i].destination_stances;
		memcpy(p + 14, movement_capabilities[i].reserved, 2);
		sg_wire_put_u32(p + 16, movement_capabilities[i].fibers.first);
		sg_wire_put_u32(p + 20, movement_capabilities[i].fibers.count);
	}
	for (i = 0U; i < source->counts[
		SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_STATES]; i++) {
		p = SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_STATES, i);
		p[0] = movement_states[i].stance;
		memcpy(p + 1, movement_states[i].reserved, 3);
		sg_wire_put_u32(p + 4, (uint32_t)movement_states[i].support);
		sg_wire_put_u32(p + 8, (uint32_t)movement_states[i].water);
		sg_wire_put_u32(p + 12, (uint32_t)movement_states[i].hook_phase);
		sg_wire_put_u32(p + 16, movement_states[i].flags);
		sg_wire_put_u32(p + 20, movement_states[i].mover_mechanism);
	}
	for (i = 0U; i < source->counts[
		SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBERS]; i++) {
		p = SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBERS, i);
		sg_wire_put_u32(p, movement_fibers[i].capability.value);
		sg_wire_put_u32(p + 4, (uint32_t)movement_fibers[i].kind);
		sg_wire_put_u32(p + 8, movement_fibers[i].state_variables);
		sg_wire_put_u32(p + 12, movement_fibers[i].source_state.value);
		sg_wire_put_u32(p + 16, movement_fibers[i].destination_state.value);
		sg_wire_put_u32(p + 20, movement_fibers[i].functions.first);
		sg_wire_put_u32(p + 24, movement_fibers[i].functions.count);
		sg_wire_put_u32(p + 28, movement_fibers[i].hook_targets.first);
		sg_wire_put_u32(p + 32, movement_fibers[i].hook_targets.count);
		sg_wire_put_u32(p + 36,
			movement_fibers[i].mechanism_transition.value);
		sg_wire_put_u32(p + 40, movement_fibers[i].angular_schedule);
		sg_wire_put_u32(p + 44,
			movement_fibers[i].controller_action_controller.value);
		sg_wire_put_u32(p + 48,
			movement_fibers[i].controller_action_target.value);
	}
	for (i = 0U; i < source->counts[
		SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_HOOK_TARGETS]; i++) {
		const sg_rune_compact_movement_hook_functions_t *hook_functions =
			&movement_hook_targets[i].functions;
		const sg_rune_analytic_function_span_t spans[6] = {
			hook_functions->bolt, hook_functions->body, hook_functions->pull,
			hook_functions->release, hook_functions->coast,
			hook_functions->relaunch
		};
		uint32_t phase;

		p = SG_WIRE_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_HOOK_TARGETS, i);
		sg_wire_put_u32(p, movement_hook_targets[i].fiber.value);
		sg_wire_put_u32(p + 4, (uint32_t)movement_hook_targets[i].target_kind);
		sg_wire_put_u32(p + 8, (uint32_t)movement_hook_targets[i].provenance);
		sg_wire_put_u32(p + 12, (uint32_t)movement_hook_targets[i].response.kind);
		sg_wire_put_u32(p + 16, movement_hook_targets[i].response.index);
		sg_wire_put_u32(p + 20,
			(uint32_t)movement_hook_targets[i].visibility_class);
		p[24] = movement_hook_targets[i].source_stances;
		p[25] = movement_hook_targets[i].target_stances;
		memcpy(p + 26, movement_hook_targets[i].reserved, 2);
		for (phase = 0U; phase < 6U; phase++) {
			sg_wire_put_u32(p + 28U + phase * 8U, spans[phase].first);
			sg_wire_put_u32(p + 32U + phase * 8U, spans[phase].count);
		}
	}
	for (i = 0U; i < source->counts[
		SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBER_FUNCTION_REFS]; i++)
		sg_wire_put_u32(SG_WIRE_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBER_FUNCTION_REFS, i),
			movement_fiber_function_refs[i].value);
	for (i = 0U; i < source->counts[
		SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_ANGULAR_SCHEDULES]; i++) {
		uint32_t axis;

		p = SG_WIRE_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_ANGULAR_SCHEDULES, i);
		sg_wire_put_u32(p,
			movement_angular_schedules[i].static_mechanism.value);
		sg_wire_put_u32(p + 4, movement_angular_schedules[i].source_entity);
		sg_wire_put_u32(p + 8, movement_angular_schedules[i].mover_model);
		sg_wire_put_u32(p + 12, movement_angular_schedules[i].flags);
		for (axis = 0U; axis < 3U; axis++) {
			sg_wire_put_u32(p + 16U + axis * 4U,
				movement_angular_schedules[i].initial_angles_bits[axis]);
			sg_wire_put_u32(p + 28U + axis * 4U,
				movement_angular_schedules[i].axis_bits[axis]);
			sg_wire_put_u32(p + 40U + axis * 4U,
				movement_angular_schedules[i].angular_velocity_bits[axis]);
			sg_wire_put_u32(p + 52U + axis * 4U,
				movement_angular_schedules[i].frame_angular_delta_bits[axis]);
		}
		sg_wire_put_u32(p + 64, movement_angular_schedules[i].speed_bits);
		sg_wire_put_u32(p + 68, movement_angular_schedules[i].frame_ms);
		sg_wire_put_u32(p + 72,
			movement_angular_schedules[i].authority_mechanism.value);
	}
	p = SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_RUNTIME, 0U);
	sg_wire_put_u32(p, source->model->movement_pmove_abi.version);
	sg_wire_put_u32(p + 4, source->model->movement_pmove_abi.game_api_version);
	sg_wire_put_u32(p + 8, source->model->movement_pmove_abi.import_size);
	sg_wire_put_u32(p + 12, source->model->movement_pmove_abi.pmove_offset);
	sg_wire_put_u32(p + 16, source->model->movement_pmove_abi.pmove_size);
	sg_wire_put_u32(p + 20, source->model->movement_pmove_abi.state_size);
	sg_wire_put_u32(p + 24, source->model->movement_pmove_abi.command_size);
	sg_wire_put_u32(p + 28, source->model->movement_pmove_abi.fraction_bits);
	sg_wire_put_u32(p + 32, source->model->movement_pmove_abi.substep_ms);
	sg_wire_put_u64(p + 36, source->model->movement_pmove_abi.identity);
	sg_wire_put_u64(p + 44,
		source->model->movement_pmove_behavior_fingerprint);
	sg_wire_put_u64(p + 52, source->model->movement_host_level_generation);
	sg_wire_put_u64(p + 60, source->model->movement_physics_abi_id);
	sg_wire_put_u64(p + 68, source->model->movement_collision_law_id);
	sg_wire_put_u64(p + 76, source->model->movement_pmove_law_id);
	sg_wire_put_u64(p + 84, source->model->movement_gravity_law_id);
	sg_wire_put_u64(p + 92, source->model->movement_hook_law_id);
	sg_wire_put_u64(p + 100, source->model->movement_mechanism_law_id);
	for (i = 0U; i < source->counts[
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FRAGMENTS]; i++)
	{
		p = SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FRAGMENTS, i);
		sg_wire_put_u32(p, response_fragments[i].parent_cell.value);
		sg_wire_put_u32(p + 4, response_fragments[i].boundary_incidences.first);
		sg_wire_put_u32(p + 8, response_fragments[i].boundary_incidences.count);
		sg_wire_put_u64(p + 12, response_fragments[i].static_partition_id);
		sg_wire_put_u32(p + 20, response_fragments[i].configuration_region);
		sg_wire_put_u32(p + 24, response_fragments[i].configuration_cell);
		sg_wire_put_u32(p + 28, response_fragments[i].first_halfspace);
		sg_wire_put_u32(p + 32, response_fragments[i].halfspace_count);
		sg_wire_put_bounds(p + 36, &response_fragments[i].bounds);
		sg_wire_put_vec3(p + 60, &response_fragments[i].witness);
		sg_wire_put_u32(p + 72, response_fragments[i].bsp_leaf);
		sg_wire_put_u32(p + 76, response_fragments[i].bsp_area);
		sg_wire_put_u32(p + 80, response_fragments[i].bsp_cluster);
		p[84] = response_fragments[i].valid_stances;
		memcpy(p + 85, response_fragments[i].reserved, 3);
	}
	for (i = 0U; i < source->counts[
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_HALFSPACES]; i++)
	{
		uint32_t axis;
		p = SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_HALFSPACES, i);
		for (axis = 0U; axis < 3U; axis++)
			sg_wire_put_u32(p + axis * 4U,
				response_halfspaces[i].plane.normal_bits[axis]);
		sg_wire_put_u32(p + 12, response_halfspaces[i].plane.distance_bits);
		sg_wire_put_u32(p + 16, response_halfspaces[i].split);
		p[20] = response_halfspaces[i].open;
		memcpy(p + 21, response_halfspaces[i].reserved, 3);
	}
	for (i = 0U; i < source->counts[
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_PATCHES]; i++)
	{
		uint32_t axis;
		p = SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_PATCHES, i);
		sg_wire_put_u64(p, response_patches[i].visibility_surface_id);
		sg_wire_put_u32(p + 8, response_patches[i].model);
		sg_wire_put_u32(p + 12, response_patches[i].brush);
		sg_wire_put_u32(p + 16, response_patches[i].brush_side);
		sg_wire_put_u32(p + 20, response_patches[i].source_surface);
		sg_wire_put_u32(p + 24, (uint32_t)response_patches[i].source_frame);
		sg_wire_put_u32(p + 28, response_patches[i].parent_facet.value);
		sg_wire_put_u32(p + 32, response_patches[i].target_cell.value);
		sg_wire_put_u32(p + 36,
			response_patches[i].boundary_incidences.first);
		sg_wire_put_u32(p + 40,
			response_patches[i].boundary_incidences.count);
		sg_wire_put_u64(p + 44, response_patches[i].static_partition_id);
		sg_wire_put_u32(p + 52, response_patches[i].configuration_region);
		sg_wire_put_u32(p + 56, response_patches[i].configuration_cell);
		for (axis = 0U; axis < 3U; axis++)
			sg_wire_put_u32(p + 60U + axis * 4U,
				response_patches[i].plane.normal_bits[axis]);
		sg_wire_put_u32(p + 72, response_patches[i].plane.distance_bits);
		sg_wire_put_u32(p + 76, response_patches[i].first_vertex);
		sg_wire_put_u32(p + 80, response_patches[i].vertex_count);
		sg_wire_put_bounds(p + 84, &response_patches[i].bounds);
		sg_wire_put_u32(p + 108, response_patches[i].bsp_leaf);
		sg_wire_put_u32(p + 112, response_patches[i].bsp_area);
		sg_wire_put_u32(p + 116, response_patches[i].bsp_cluster);
		sg_wire_put_u32(p + 120, response_patches[i].flags);
		p[124] = response_patches[i].valid_stances;
		memcpy(p + 125, response_patches[i].reserved, 3);
	}
	for (i = 0U; i < source->counts[
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_TARGET_VERTICES]; i++)
		sg_wire_put_vec3(SG_WIRE_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_TARGET_VERTICES, i),
			&response_target_vertices[i]);
	for (i = 0U; i < source->counts[
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SPLITS]; i++)
	{
		uint32_t axis;
		p = SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SPLITS, i);
		for (axis = 0U; axis < 3U; axis++)
			sg_wire_put_u32(p + axis * 4U,
				response_splits[i].plane.normal_bits[axis]);
		sg_wire_put_u32(p + 12, response_splits[i].plane.distance_bits);
		sg_wire_put_u32(p + 16, (uint32_t)response_splits[i].kind);
		sg_wire_put_u64(p + 20, response_splits[i].target_surface_id);
		sg_wire_put_u32(p + 28, response_splits[i].occluder);
		sg_wire_put_u32(p + 32, response_splits[i].edge);
		sg_wire_put_u32(p + 36, response_splits[i].brush_side);
	}
	for (i = 0U; i < source->counts[
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FACTS]; i++)
	{
		p = SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FACTS, i);
		sg_wire_put_u32(p, response_facts[i].source_fragment);
		sg_wire_put_u32(p + 4, response_facts[i].target_patch);
		sg_wire_put_u32(p + 8, response_facts[i].flags);
		sg_wire_put_u32(p + 12, (uint32_t)response_facts[i].visibility);
		sg_wire_put_u32(p + 16,
			(uint32_t)response_facts[i].visibility_reason);
		p[20] = response_facts[i].requires_exact_ray;
		p[21] = response_facts[i].requires_area_state;
		memcpy(p + 22, response_facts[i].reserved, 2);
		sg_wire_put_u32(p + 24, response_facts[i].certificate_split);
		sg_wire_put_vec3(p + 28, &response_facts[i].target_witness);
		sg_wire_put_u32(p + 40, response_facts[i].occluders.first);
		sg_wire_put_u32(p + 44, response_facts[i].occluders.count);
		sg_wire_put_trace(p + 48, &response_facts[i].trace);
	}
	for (i = 0U; i < source->counts[
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_CANDIDATE_GROUPS]; i++)
	{
		p = SG_WIRE_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_CANDIDATE_GROUPS, i);
		sg_wire_put_u32(p, response_candidates[i].source_group);
		sg_wire_put_u32(p + 4, response_candidates[i].target_group);
		sg_wire_put_u32(p + 8,
			(uint32_t)response_candidates[i].classification);
		sg_wire_put_u32(p + 12, (uint32_t)response_candidates[i].reason);
		p[16] = response_candidates[i].requires_exact_ray;
		p[17] = response_candidates[i].requires_area_state;
		memcpy(p + 18, response_candidates[i].reserved, 2);
		sg_wire_put_u32(p + 20, response_candidates[i].relation_flags);
	}
	for (i = 0U; i < source->counts[
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SOURCE_ENDPOINT_GROUPS]; i++)
	{
		p = SG_WIRE_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SOURCE_ENDPOINT_GROUPS, i);
		sg_wire_put_u32(p, response_source_groups[i].bsp_cluster);
		sg_wire_put_u32(p + 4, response_source_groups[i].bsp_area);
		sg_wire_put_u32(p + 8, response_source_groups[i].flags);
		sg_wire_put_u32(p + 12, response_source_groups[i].first_member);
		sg_wire_put_u32(p + 16, response_source_groups[i].member_count);
	}
	for (i = 0U; i < source->counts[
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SOURCE_ENDPOINT_MEMBERS]; i++)
		sg_wire_put_u32(SG_WIRE_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SOURCE_ENDPOINT_MEMBERS, i),
			response_source_members[i]);
	for (i = 0U; i < source->counts[
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_TARGET_ENDPOINT_GROUPS]; i++)
	{
		p = SG_WIRE_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_TARGET_ENDPOINT_GROUPS, i);
		sg_wire_put_u32(p, response_target_groups[i].bsp_cluster);
		sg_wire_put_u32(p + 4, response_target_groups[i].bsp_area);
		sg_wire_put_u32(p + 8, response_target_groups[i].flags);
		sg_wire_put_u32(p + 12, response_target_groups[i].first_member);
		sg_wire_put_u32(p + 16, response_target_groups[i].member_count);
	}
	for (i = 0U; i < source->counts[
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_TARGET_ENDPOINT_MEMBERS]; i++)
		sg_wire_put_u32(SG_WIRE_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_TARGET_ENDPOINT_MEMBERS, i),
			response_target_members[i]);
	p = SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SEAL, 0U);
	sg_wire_put_u16(p, response_seal->version);
	sg_wire_put_u16(p + 2, response_seal->reserved);
	sg_wire_put_u32(p + 4, response_seal->flags);
	sg_wire_put_u32(p + 8, response_seal->split_frontier_count);
	sg_wire_put_u32(p + 12, response_seal->source_fragment_count);
	sg_wire_put_u32(p + 16, response_seal->target_patch_count);
	sg_wire_put_u32(p + 20, response_seal->split_count);
	sg_wire_put_u32(p + 24, response_seal->response_pair_count);
	sg_wire_put_u32(p + 28, response_seal->certified_direct_pair_count);
	sg_wire_put_u32(p + 32,
		response_seal->certified_static_impact_pair_count);
	sg_wire_put_u32(p + 36, response_seal->unresolved_response_pair_count);
	sg_wire_put_u32(p + 40,
		response_seal->unresolved_candidate_group_count);
	sg_wire_put_u32(p + 44, response_seal->source_endpoint_group_count);
	sg_wire_put_u32(p + 48, response_seal->target_endpoint_group_count);
	sg_wire_put_u32(p + 52, response_seal->source_endpoint_member_count);
	sg_wire_put_u32(p + 56, response_seal->target_endpoint_member_count);
	sg_wire_put_u32(p + 60, response_seal->static_occluder_count);
	sg_wire_put_u32(p + 64, response_seal->compact_facet_count);
	sg_wire_put_u32(p + 68, response_seal->compact_cell_count);
	sg_wire_put_u32(p + 72, response_seal->compact_source_surface_count);
	sg_wire_put_u32(p + 76,
		response_seal->compact_source_surface_vertex_count);
	sg_wire_put_u64(p + 80, response_seal->source_surface_catalog_seal);
	p[88] = source->response_exact_live_prefire_trace_required;
	for (i = 0; i < source->counts[SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_PROFILES]; ++i)
	{
		p = SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_PROFILES, i);
		sg_wire_put_u32(p, weapon_profiles[i].source_profile);
		sg_wire_put_u32(p + 4, weapon_profiles[i].response_families);
		sg_wire_put_u16(p + 8, weapon_profiles[i].projectile_count_min);
		sg_wire_put_u16(p + 10, weapon_profiles[i].projectile_count_max);
		sg_wire_put_u16(p + 12, weapon_profiles[i].auxiliary_trace_count);
		p[14] = weapon_profiles[i].direct_response_count;
		p[15] = weapon_profiles[i].reserved;
	}
	for (i = 0; i < source->counts[SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_KERNELS]; ++i)
	{
		p = SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_KERNELS, i);
		sg_wire_put_u32(p, weapon_kernels[i].profile);
		sg_wire_put_u32(p + 4, (uint32_t)weapon_kernels[i].family);
		sg_wire_put_u32(p + 8, weapon_kernels[i].functions.first);
		sg_wire_put_u32(p + 12, weapon_kernels[i].functions.count);
		sg_wire_put_u32(p + 16,
			(uint32_t)weapon_kernels[i].event_law.kind);
		sg_wire_put_u32(p + 20,
			weapon_kernels[i].event_law.requirements);
	}
	for (i = 0; i < source->counts[
		SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_FUNCTION_REFS]; ++i)
	{
		p = SG_WIRE_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_FUNCTION_REFS, i);
		sg_wire_put_u32(p, weapon_function_refs[i].function.value);
		sg_wire_put_u32(p + 4, (uint32_t)weapon_function_refs[i].channel);
		sg_wire_put_u32(p + 8, weapon_function_refs[i].instance);
	}
	for (i = 0U; i < source->counts[
		SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_ATTACHMENTS]; i++) {
		p = SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_ATTACHMENTS, i);
		sg_wire_put_u32(p, weapon_attachments[i].cell.value);
		sg_wire_put_u32(p + 4, weapon_attachments[i].source_surface);
		sg_wire_put_u32(p + 8,
			(uint32_t)weapon_attachments[i].relation_class);
		sg_wire_put_u32(p + 12, weapon_attachments[i].reserved0);
		sg_wire_put_u32(p + 16, weapon_attachments[i].relations.first);
		sg_wire_put_u32(p + 20, weapon_attachments[i].relations.count);
		sg_wire_put_u32(p + 24, weapon_attachments[i].relation_span);
		sg_wire_put_u32(p + 28, weapon_attachments[i].reserved1);
	}
	for (i = 0U; i < source->counts[
		SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_RELATION_SPANS]; i++) {
		p = SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_RELATION_SPANS,
			i);
		sg_wire_put_u32(p, weapon_relation_spans[i].references.first);
		sg_wire_put_u32(p + 4, weapon_relation_spans[i].references.count);
	}
	for (i = 0U; i < source->counts[
		SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_RELATION_REFS]; i++) {
		p = SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_RELATION_REFS, i);
		sg_wire_put_u32(p, (uint32_t)weapon_relation_refs[i].kind);
		sg_wire_put_u32(p + 4, weapon_relation_refs[i].index);
	}
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
		sg_wire_put_u32(p + 4, mechanisms[i].entry_cell.value);
		sg_wire_put_u32(p + 8, mechanisms[i].exit_cell.value);
		sg_wire_put_u32(p + 12, mechanisms[i].activation_landmark.value);
		sg_wire_put_bounds(p + 16, &mechanisms[i].bounds);
		sg_wire_put_u32(p + 40, mechanisms[i].controllers.first);
		sg_wire_put_u32(p + 44, mechanisms[i].controllers.count);
		sg_wire_put_u32(p + 48, mechanisms[i].topology.first);
		sg_wire_put_u32(p + 52, mechanisms[i].topology.count);
		sg_wire_put_u32(p + 56, mechanisms[i].transitions.first);
		sg_wire_put_u32(p + 60, mechanisms[i].transitions.count);
		sg_wire_put_u32(p + 64, mechanisms[i].delay_ms);
		sg_wire_put_u32(p + 68, mechanisms[i].dwell_ms);
		sg_wire_put_u32(p + 72, mechanisms[i].travel_ms);
		sg_wire_put_u32(p + 76, mechanisms[i].wait_ms);
		sg_wire_put_u32(p + 80, mechanisms[i].reset_ms);
		sg_wire_put_u32(p + 84, mechanisms[i].activation_mask);
		sg_wire_put_u32(p + 88, (uint32_t)mechanisms[i].damage);
		sg_wire_put_u32(p + 92, (uint32_t)mechanisms[i].health);
		sg_wire_put_u32(p + 96, mechanisms[i].required_item);
		sg_wire_put_u32(p + 100,
			mechanisms[i].transition_destination.entity_ordinal);
		sg_wire_put_u32(p + 104, mechanisms[i].transition_fanout_ordinal);
		sg_wire_put_u32(p + 108, mechanisms[i].launch_velocity_bits[0]);
		sg_wire_put_u32(p + 112, mechanisms[i].launch_velocity_bits[1]);
		sg_wire_put_u32(p + 116, mechanisms[i].launch_velocity_bits[2]);
		sg_wire_put_u32(p + 120, mechanisms[i].gravity_bits);
		sg_wire_put_u32(p + 124, mechanisms[i].flight_ms);
		sg_wire_put_u32(p + 128, (uint32_t)mechanisms[i].kind);
		sg_wire_put_u32(p + 132, (uint32_t)mechanisms[i].initial_state);
		sg_wire_put_u32(p + 136, (uint32_t)mechanisms[i].activated_state);
		sg_wire_put_u32(p + 140, (uint32_t)mechanisms[i].reset_state);
		sg_wire_put_u32(p + 144, (uint32_t)mechanisms[i].recovery);
		p[148] = mechanisms[i].flags;
		memcpy(p + 149, mechanisms[i].reserved, 3);
	}
	for (i = 0; i < source->counts[
		SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_CONTROLLERS]; ++i)
	{
		p = SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_CONTROLLERS,
			i);
		sg_wire_put_u32(p, controllers[i].controller.entity_ordinal);
		sg_wire_put_u32(p + 4, controllers[i].topology_edge);
		p[8] = controllers[i].spatiality;
		memcpy(p + 9, controllers[i].reserved, 3);
		sg_wire_put_u32(p + 12, controllers[i].activation_cell.value);
		sg_wire_put_vec3(p + 16, &controllers[i].activation_witness);
		sg_wire_put_bounds(p + 28, &controllers[i].activation_bounds);
	}
	for (i = 0; i < source->counts[SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_EDGES]; ++i)
	{
		p = SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_EDGES, i);
		sg_wire_put_u32(p, edges[i].source.entity_ordinal);
		sg_wire_put_u32(p + 4, edges[i].destination.entity_ordinal);
		sg_wire_put_u32(p + 8, edges[i].fanout_ordinal);
		sg_wire_put_u32(p + 12, (uint32_t)edges[i].kind);
	}
	for (i = 0; i < source->counts[SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS]; ++i)
	{
		uint32_t axis;

		p = SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS, i);
		sg_wire_put_u32(p, transitions[i].mechanism.value);
		sg_wire_put_u32(p + 4, (uint32_t)transitions[i].kind);
		sg_wire_put_u32(p + 8, transitions[i].entry_cell.value);
		sg_wire_put_u32(p + 12, transitions[i].exit_cell.value);
		sg_wire_put_u32(p + 16, (uint32_t)transitions[i].source_state);
		sg_wire_put_u32(p + 20, (uint32_t)transitions[i].destination_state);
		sg_wire_put_u64(p + 24, transitions[i].elapsed_ms);
		switch (transitions[i].kind)
		{
		case SG_RUNE_COMPACT_STATIC_TRANSITION_PORTAL_STATE:
			sg_wire_put_u32(p + 32,
				transitions[i].value.portal_state.portal.value);
			sg_wire_put_u32(p + 36,
				transitions[i].value.portal_state.mover_model);
			sg_wire_put_u32(p + 40,
				transitions[i].value.portal_state.delay_ms);
			sg_wire_put_u32(p + 44,
				transitions[i].value.portal_state.dwell_ms);
			sg_wire_put_u32(p + 48,
				transitions[i].value.portal_state.pause_ms);
			sg_wire_put_u32(p + 52,
				transitions[i].value.portal_state.travel_ms);
			sg_wire_put_u32(p + 56,
				transitions[i].value.portal_state.recovery_ms);
			p[60] = transitions[i].value.portal_state.source_blocked;
			p[61] = transitions[i].value.portal_state.destination_blocked;
			p[62] = 0U;
			p[63] = 0U;
			break;
		case SG_RUNE_COMPACT_STATIC_TRANSITION_TELEPORT:
			sg_wire_put_u32(p + 32,
				transitions[i].value.teleport.destination.entity_ordinal);
			sg_wire_put_u32(p + 36,
				transitions[i].value.teleport.fanout_ordinal);
			sg_wire_put_vec3(p + 40,
				&transitions[i].value.teleport.approach_witness);
			sg_wire_put_vec3(p + 52,
				&transitions[i].value.teleport.entry_witness);
			sg_wire_put_vec3(p + 64,
				&transitions[i].value.teleport.exit_witness);
			for (axis = 0U; axis < 3U; axis++)
				sg_wire_put_u32(p + 76U + axis * 4U,
					transitions[i].value.teleport.arrival_velocity_bits[axis]);
			break;
		case SG_RUNE_COMPACT_STATIC_TRANSITION_PUSH:
			sg_wire_put_vec3(p + 32,
				&transitions[i].value.push.approach_witness);
			sg_wire_put_vec3(p + 44,
				&transitions[i].value.push.entry_witness);
			sg_wire_put_vec3(p + 56,
				&transitions[i].value.push.exit_witness);
			for (axis = 0U; axis < 3U; axis++)
				sg_wire_put_u32(p + 68U + axis * 4U,
					transitions[i].value.push.launch_velocity_bits[axis]);
			sg_wire_put_u32(p + 80,
				transitions[i].value.push.gravity_bits);
			sg_wire_put_u32(p + 84,
				transitions[i].value.push.flight_ms);
			break;
		case SG_RUNE_COMPACT_STATIC_TRANSITION_TRANSPORT:
		{
			uint32_t row;

			sg_wire_put_u32(p + 32,
				transitions[i].value.transport.mover_model);
			sg_wire_put_u32(p + 36,
				transitions[i].value.transport.source_surface_ordinal);
			sg_wire_put_vec3(p + 40,
				&transitions[i].value.transport.source_player_local);
			sg_wire_put_vec3(p + 52,
				&transitions[i].value.transport.destination_player_local);
			sg_wire_put_vec3(p + 64,
				&transitions[i].value.transport.source_support_local);
			sg_wire_put_vec3(p + 76,
				&transitions[i].value.transport.destination_support_local);
			for (axis = 0U; axis < 3U; axis++)
			{
				sg_wire_put_u32(p + 88U + axis * 4U,
					transitions[i].value.transport.source_player_world_bits[axis]);
				sg_wire_put_u32(p + 100U + axis * 4U,
					transitions[i].value.transport.destination_player_world_bits[axis]);
				sg_wire_put_u32(p + 112U + axis * 4U,
					transitions[i].value.transport.source_support_world_bits[axis]);
				sg_wire_put_u32(p + 124U + axis * 4U,
					transitions[i].value.transport.destination_support_world_bits[axis]);
				sg_wire_put_u32(p + 136U + axis * 4U,
					transitions[i].value.transport.source_mover_origin_bits[axis]);
				sg_wire_put_u32(p + 184U + axis * 4U,
					transitions[i].value.transport.destination_mover_origin_bits[axis]);
			}
			for (row = 0U; row < 3U; row++)
				for (axis = 0U; axis < 3U; axis++) {
					sg_wire_put_u32(p + 148U + (row * 3U + axis) * 4U,
						transitions[i].value.transport.source_mover_axis_bits[row][axis]);
					sg_wire_put_u32(p + 196U + (row * 3U + axis) * 4U,
						transitions[i].value.transport.destination_mover_axis_bits[row][axis]);
				}
			sg_wire_put_u32(p + 232,
				transitions[i].value.transport.source_endpoint.entity_ordinal);
			sg_wire_put_u32(p + 236,
				transitions[i].value.transport.destination_endpoint.entity_ordinal);
			sg_wire_put_u32(p + 240,
				transitions[i].value.transport.fanout_ordinal);
			p[244] = transitions[i].value.transport.swept_static_clear;
			p[245] = transitions[i].value.transport.start_supported;
			p[246] = transitions[i].value.transport.end_supported;
			p[247] = transitions[i].value.transport.stance;
			break;
		}
		case SG_RUNE_COMPACT_STATIC_TRANSITION_KIND_COUNT:
			break;
		}
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
		sg_wire_put_u32(p + 8, annotations[i].source_surface);
		sg_wire_put_u32(p + 12, (uint32_t)annotations[i].source_frame);
	}
	for (i = 0; i < source->counts[SG_RUNE_COMPACT_WIRE_SECTION_PORTAL_MECHANISMS]; ++i)
	{
		p = SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_PORTAL_MECHANISMS, i);
		sg_wire_put_u32(p, portal_mechanisms[i].portal.value);
		sg_wire_put_u32(p + 4, portal_mechanisms[i].mechanism.value);
		sg_wire_put_u32(p + 8, (uint32_t)portal_mechanisms[i].kind);
		memcpy(p + 12, portal_mechanisms[i].reserved, 3);
	}
	for (i = 0U; i < source->counts[
		SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITIES]; i++) {
		p = SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITIES,
			i);
		sg_wire_put_u32(p, mechanism_authorities[i].source.entity_ordinal);
		sg_wire_put_u32(p + 4, (uint32_t)mechanism_authorities[i].kind);
		sg_wire_put_u32(p + 8, mechanism_authorities[i].activation);
		sg_wire_put_u32(p + 12, mechanism_authorities[i].activation_cell.value);
		sg_wire_put_vec3(p + 16, &mechanism_authorities[i].activation_witness);
		sg_wire_put_bounds(p + 28, &mechanism_authorities[i].activation_bounds);
		sg_wire_put_u32(p + 52, mechanism_authorities[i].controllers.first);
		sg_wire_put_u32(p + 56, mechanism_authorities[i].controllers.count);
		sg_wire_put_u32(p + 60, mechanism_authorities[i].topology.first);
		sg_wire_put_u32(p + 64, mechanism_authorities[i].topology.count);
		sg_wire_put_u32(p + 68, mechanism_authorities[i].transitions.first);
		sg_wire_put_u32(p + 72, mechanism_authorities[i].transitions.count);
		sg_wire_put_u32(p + 76, mechanism_authorities[i].delay_ms);
		sg_wire_put_u32(p + 80, mechanism_authorities[i].dwell_ms);
		sg_wire_put_u32(p + 84, mechanism_authorities[i].pause_ms);
		sg_wire_put_u32(p + 88, mechanism_authorities[i].travel_ms);
		sg_wire_put_u32(p + 92, (uint32_t)mechanism_authorities[i].damage);
		sg_wire_put_u32(p + 96, (uint32_t)mechanism_authorities[i].health);
		sg_wire_put_u32(p + 100, mechanism_authorities[i].required_item);
		sg_wire_put_u32(p + 104,
			(uint32_t)mechanism_authorities[i].initial_state);
		sg_wire_put_u32(p + 108,
			(uint32_t)mechanism_authorities[i].activated_state);
		sg_wire_put_u32(p + 112,
			(uint32_t)mechanism_authorities[i].reset_state);
		sg_wire_put_u32(p + 116, mechanism_authorities[i].recovery_ms);
		sg_wire_put_u32(p + 120, mechanism_authorities[i].flags);
	}
	for (i = 0U; i < source->counts[
		SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_CONTROLLERS]; i++) {
		p = SG_WIRE_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_CONTROLLERS, i);
		sg_wire_put_u32(p, authority_controllers[i].mechanism);
		sg_wire_put_u32(p + 4,
			authority_controllers[i].controller.entity_ordinal);
		sg_wire_put_u32(p + 8, authority_controllers[i].topology_edge);
		sg_wire_put_u32(p + 12, authority_controllers[i].activation);
		sg_wire_put_u32(p + 16, (uint32_t)authority_controllers[i].damage);
		sg_wire_put_u32(p + 20, (uint32_t)authority_controllers[i].health);
		sg_wire_put_u32(p + 24, authority_controllers[i].required_item);
		sg_wire_put_u32(p + 28, authority_controllers[i].flags);
		p[32] = authority_controllers[i].spatiality;
		memcpy(p + 33, authority_controllers[i].reserved, 3);
		sg_wire_put_u32(p + 36,
			authority_controllers[i].activation_cell.value);
		sg_wire_put_vec3(p + 40,
			&authority_controllers[i].activation_witness);
		sg_wire_put_bounds(p + 52,
			&authority_controllers[i].activation_bounds);
	}
	for (i = 0U; i < source->counts[
		SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TOPOLOGY_EDGES]; i++) {
		p = SG_WIRE_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TOPOLOGY_EDGES, i);
		sg_wire_put_u32(p, authority_edges[i].source.entity_ordinal);
		sg_wire_put_u32(p + 4, authority_edges[i].destination.entity_ordinal);
		sg_wire_put_u32(p + 8, (uint32_t)authority_edges[i].kind);
		sg_wire_put_u32(p + 12, authority_edges[i].fanout_ordinal);
	}
	for (i = 0U; i < source->counts[
		SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TRANSITIONS]; i++)
		sg_wire_put_authority_transition(SG_WIRE_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TRANSITIONS, i),
			&authority_transitions[i]);
	for (i = 0U; i < source->counts[
		SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TRANSITION_STATIC_INDICES];
		i++)
		sg_wire_put_u32(SG_WIRE_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TRANSITION_STATIC_INDICES,
			i), authority_transition_static_indices[i]);
	for (i = 0U; i < source->counts[
		SG_RUNE_COMPACT_WIRE_SECTION_STATIC_TRANSITION_AUTHORITY_INDICES]; i++)
		sg_wire_put_u32(SG_WIRE_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_STATIC_TRANSITION_AUTHORITY_INDICES, i),
			static_transition_authority_indices[i]);
	for (i = 0; i < source->counts[
		SG_RUNE_COMPACT_WIRE_SECTION_STATIC_OCCLUDERS]; ++i)
	{
		p = SG_WIRE_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_STATIC_OCCLUDERS,
			i);
		sg_wire_put_u32(p, static_occluders[i].model);
		sg_wire_put_u32(p + 4, static_occluders[i].brush);
		sg_wire_put_u32(p + 8, static_occluders[i].contents);
		sg_wire_put_u32(p + 12, static_occluders[i].conditional);
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
	if (total > SG_RuneCompactWireImageLimit()) {
		sg_wire_set_error(error_out, SG_RUNE_COMPACT_WIRE_ERROR_LIMIT_EXCEEDED,
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

static int sg_wire_static_occluder_compare(const uint8_t *left,
	const uint8_t *right)
{
	uint32_t offset;

	for (offset = 0U; offset <= 12U; offset += 4U) {
		const uint32_t left_value = sg_wire_u32(left + offset);
		const uint32_t right_value = sg_wire_u32(right + offset);

		if (left_value < right_value)
			return -1;
		if (left_value > right_value)
			return 1;
	}
	return 0;
}

static int sg_wire_compare_u32(uint32_t left, uint32_t right)
{
	return left < right ? -1 : left > right ? 1 : 0;
}

static int sg_wire_compare_u64(uint64_t left, uint64_t right)
{
	return left < right ? -1 : left > right ? 1 : 0;
}

static int sg_wire_compare_q8_vec3(const uint8_t *left,
	const uint8_t *right)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; ++axis) {
		const int32_t left_value = sg_wire_i32(left + axis * 4U);
		const int32_t right_value = sg_wire_i32(right + axis * 4U);

		if (left_value != right_value)
			return left_value < right_value ? -1 : 1;
	}
	return 0;
}

static int sg_wire_compare_u32_array(const uint8_t *left,
	const uint8_t *right, uint32_t count)
{
	uint32_t index;

	for (index = 0U; index < count; ++index) {
		const int comparison = sg_wire_compare_u32(
			sg_wire_u32(left + index * 4U),
			sg_wire_u32(right + index * 4U));

		if (comparison != 0)
			return comparison;
	}
	return 0;
}

static int sg_wire_u32_array_equals(const uint8_t *wire,
	const uint32_t *values, uint32_t count)
{
	uint32_t index;

	for (index = 0U; index < count; ++index)
		if (sg_wire_u32(wire + index * 4U) != values[index])
			return 0;
	return 1;
}

/* This is the byte-level counterpart of
 * SG_RuneCompactStaticTransitionCompareCanonical.  It keeps wire inspection
 * independent from the decoded static model while preserving the producer's
 * one transition order. */
static int sg_wire_transition_compare_canonical(const uint8_t *left,
	const uint8_t *right)
{
	int comparison;
	uint32_t kind;

	comparison = sg_wire_compare_u32(sg_wire_u32(left), sg_wire_u32(right));
	if (comparison == 0)
		comparison = sg_wire_compare_u32(sg_wire_u32(left + 4U),
			sg_wire_u32(right + 4U));
	if (comparison != 0)
		return comparison;
	kind = sg_wire_u32(left + 4U);
	if (kind == (uint32_t)SG_RUNE_COMPACT_STATIC_TRANSITION_PORTAL_STATE) {
		comparison = sg_wire_compare_u32(sg_wire_u32(left + 32U),
			sg_wire_u32(right + 32U));
		if (comparison != 0)
			return comparison;
	}
	if (comparison == 0)
		comparison = sg_wire_compare_u32(sg_wire_u32(left + 8U),
			sg_wire_u32(right + 8U));
	if (comparison == 0)
		comparison = sg_wire_compare_u32(sg_wire_u32(left + 12U),
			sg_wire_u32(right + 12U));
	if (comparison == 0)
		comparison = sg_wire_compare_u32(sg_wire_u32(left + 16U),
			sg_wire_u32(right + 16U));
	if (comparison == 0)
		comparison = sg_wire_compare_u32(sg_wire_u32(left + 20U),
			sg_wire_u32(right + 20U));
	if (comparison == 0)
		comparison = sg_wire_compare_u64(sg_wire_u64(left + 24U),
			sg_wire_u64(right + 24U));
	if (comparison != 0)
		return comparison;
	switch ((sg_rune_compact_static_transition_kind_t)kind)
	{
	case SG_RUNE_COMPACT_STATIC_TRANSITION_PORTAL_STATE:
		for (kind = 36U; kind < 60U; kind += 4U) {
			comparison = sg_wire_compare_u32(sg_wire_u32(left + kind),
				sg_wire_u32(right + kind));
			if (comparison != 0)
				return comparison;
		}
		comparison = sg_wire_compare_u32(left[60U], right[60U]);
		return comparison == 0 ? sg_wire_compare_u32(left[61U],
			right[61U]) : comparison;
	case SG_RUNE_COMPACT_STATIC_TRANSITION_TELEPORT:
		comparison = sg_wire_compare_u32(sg_wire_u32(left + 32U),
			sg_wire_u32(right + 32U));
		if (comparison == 0)
			comparison = sg_wire_compare_u32(sg_wire_u32(left + 36U),
				sg_wire_u32(right + 36U));
		if (comparison == 0)
			comparison = sg_wire_compare_q8_vec3(left + 40U, right + 40U);
		if (comparison == 0)
			comparison = sg_wire_compare_q8_vec3(left + 52U, right + 52U);
		if (comparison == 0)
			comparison = sg_wire_compare_q8_vec3(left + 64U, right + 64U);
		return comparison == 0 ? sg_wire_compare_u32_array(left + 76U,
			right + 76U, 3U) : comparison;
	case SG_RUNE_COMPACT_STATIC_TRANSITION_PUSH:
		comparison = sg_wire_compare_q8_vec3(left + 32U, right + 32U);
		if (comparison == 0)
			comparison = sg_wire_compare_q8_vec3(left + 44U, right + 44U);
		if (comparison == 0)
			comparison = sg_wire_compare_q8_vec3(left + 56U, right + 56U);
		if (comparison == 0)
			comparison = sg_wire_compare_u32_array(left + 68U, right + 68U,
				3U);
		if (comparison == 0)
			comparison = sg_wire_compare_u32(sg_wire_u32(left + 80U),
				sg_wire_u32(right + 80U));
		return comparison == 0 ? sg_wire_compare_u32(sg_wire_u32(left + 84U),
			sg_wire_u32(right + 84U)) : comparison;
	case SG_RUNE_COMPACT_STATIC_TRANSITION_TRANSPORT:
		comparison = sg_wire_compare_u32(sg_wire_u32(left + 32U),
			sg_wire_u32(right + 32U));
		if (comparison == 0)
			comparison = sg_wire_compare_u32(sg_wire_u32(left + 36U),
				sg_wire_u32(right + 36U));
		if (comparison == 0)
			comparison = sg_wire_compare_q8_vec3(left + 40U, right + 40U);
		if (comparison == 0)
			comparison = sg_wire_compare_q8_vec3(left + 52U, right + 52U);
		if (comparison == 0)
			comparison = sg_wire_compare_q8_vec3(left + 64U, right + 64U);
		if (comparison == 0)
			comparison = sg_wire_compare_q8_vec3(left + 76U, right + 76U);
		if (comparison == 0)
			comparison = sg_wire_compare_u32_array(left + 88U, right + 88U,
				3U);
		if (comparison == 0)
			comparison = sg_wire_compare_u32_array(left + 100U,
				right + 100U, 3U);
		if (comparison == 0)
			comparison = sg_wire_compare_u32_array(left + 112U,
				right + 112U, 3U);
		if (comparison == 0)
			comparison = sg_wire_compare_u32_array(left + 124U,
				right + 124U, 3U);
		if (comparison == 0)
			comparison = sg_wire_compare_u32_array(left + 136U,
				right + 136U, 3U);
		if (comparison == 0)
			comparison = sg_wire_compare_u32_array(left + 148U,
				right + 148U, 9U);
		if (comparison == 0)
			comparison = sg_wire_compare_u32_array(left + 184U,
				right + 184U, 3U);
		if (comparison == 0)
			comparison = sg_wire_compare_u32_array(left + 196U,
				right + 196U, 9U);
		if (comparison == 0)
			comparison = sg_wire_compare_u32(sg_wire_u32(left + 232U),
				sg_wire_u32(right + 232U));
		if (comparison == 0)
			comparison = sg_wire_compare_u32(sg_wire_u32(left + 236U),
				sg_wire_u32(right + 236U));
		if (comparison == 0)
			comparison = sg_wire_compare_u32(sg_wire_u32(left + 240U),
				sg_wire_u32(right + 240U));
		if (comparison == 0)
			comparison = sg_wire_compare_u32(left[244U], right[244U]);
		if (comparison == 0)
			comparison = sg_wire_compare_u32(left[245U], right[245U]);
		if (comparison == 0)
			comparison = sg_wire_compare_u32(left[246U], right[246U]);
		return comparison == 0 ? sg_wire_compare_u32(left[247U],
			right[247U]) : comparison;
	case SG_RUNE_COMPACT_STATIC_TRANSITION_KIND_COUNT:
		break;
	}
	return 0;
}

/* Keep the artifact's local-field order identical to the model comparator.
 * PORTAL_STATE fanout is distinguished by transition before any spans, so
 * independent portal roots cannot be reordered by incidental analytic data. */
static int sg_wire_movement_field_compare(const uint8_t *left,
	const uint8_t *right)
{
	int comparison;

	comparison = sg_wire_compare_u32(sg_wire_u32(left), sg_wire_u32(right));
	if (comparison == 0)
		comparison = sg_wire_compare_u32(sg_wire_u32(left + 4U),
			sg_wire_u32(right + 4U));
	if (comparison == 0)
		comparison = sg_wire_compare_u32(sg_wire_u32(left + 8U),
			sg_wire_u32(right + 8U));
	if (comparison == 0)
		comparison = sg_wire_compare_u32(left[12U], right[12U]);
	if (comparison == 0)
		comparison = sg_wire_compare_u32(left[13U], right[13U]);
	if (comparison == 0)
		comparison = sg_wire_compare_u32(sg_wire_u32(left + 16U),
			sg_wire_u32(right + 16U));
	return comparison == 0 ? sg_wire_compare_u32(sg_wire_u32(left + 20U),
		sg_wire_u32(right + 20U)) : comparison;
}

static int sg_wire_movement_state_compare(const uint8_t *left,
	const uint8_t *right)
{
	int comparison = sg_wire_compare_u32(left[0], right[0]);
	uint32_t offset;

	for (offset = 4U; offset <= 20U && comparison == 0; offset += 4U)
		comparison = sg_wire_compare_u32(sg_wire_u32(left + offset),
			sg_wire_u32(right + offset));
	return comparison;
}

static int sg_wire_movement_angular_schedule_compare(const uint8_t *left,
	const uint8_t *right)
{
	int comparison = sg_wire_compare_u32(sg_wire_u32(left),
		sg_wire_u32(right));

	if (comparison == 0)
		comparison = sg_wire_compare_u32(sg_wire_u32(left + 72U),
			sg_wire_u32(right + 72U));
	return comparison;
}

static uint32_t sg_wire_movement_state_variables(uint32_t capability_kind,
	uint32_t transition_kind)
{
	uint32_t variables = SG_RUNE_MOVEMENT_STATE_POSITION |
		SG_RUNE_MOVEMENT_STATE_VELOCITY | SG_RUNE_MOVEMENT_STATE_STANCE |
		SG_RUNE_MOVEMENT_STATE_TIME;

	if (capability_kind <=
			(uint32_t)SG_RUNE_MOVEMENT_CAPABILITY_AIR_CONTROL &&
		capability_kind != (uint32_t)SG_RUNE_MOVEMENT_CAPABILITY_SWIM)
		variables |= SG_RUNE_MOVEMENT_STATE_SUPPORT;
	if (capability_kind == (uint32_t)SG_RUNE_MOVEMENT_CAPABILITY_SWIM)
		variables |= SG_RUNE_MOVEMENT_STATE_WATER |
			SG_RUNE_MOVEMENT_STATE_CURRENT;
	if (capability_kind >= (uint32_t)SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BOLT &&
		capability_kind <=
			(uint32_t)SG_RUNE_MOVEMENT_CAPABILITY_HOOK_RELAUNCH)
		variables |= SG_RUNE_MOVEMENT_STATE_HOOK;
	if (capability_kind ==
		(uint32_t)SG_RUNE_MOVEMENT_CAPABILITY_HOOK_RELEASE)
		variables |= SG_RUNE_MOVEMENT_STATE_SUPPORT;
	if (capability_kind == (uint32_t)SG_RUNE_MOVEMENT_CAPABILITY_MOVER &&
		transition_kind ==
			(uint32_t)SG_RUNE_COMPACT_MECHANISM_TRANSITION_TELEPORT)
		variables |= SG_RUNE_MOVEMENT_STATE_SUPPORT |
			SG_RUNE_MOVEMENT_STATE_WATER;
	else if (capability_kind ==
			(uint32_t)SG_RUNE_MOVEMENT_CAPABILITY_MOVER || capability_kind ==
			(uint32_t)SG_RUNE_MOVEMENT_CAPABILITY_CONTROLLER_ACTION)
		variables |= SG_RUNE_MOVEMENT_STATE_MOVER;
	if (capability_kind ==
		(uint32_t)SG_RUNE_MOVEMENT_CAPABILITY_EXTERNAL_FORCE)
		variables |= SG_RUNE_MOVEMENT_STATE_EXTERNAL_FORCE;
	return variables;
}

static int sg_wire_teleport_state_valid(const uint8_t *state)
{
	const uint32_t support = sg_wire_u32(state + 4);
	const uint32_t water = sg_wire_u32(state + 8);
	const uint32_t flags = sg_wire_u32(state + 16);

	return (support == SG_RUNE_MOVEMENT_SUPPORT_NONE ||
			support == SG_RUNE_MOVEMENT_SUPPORT_STATIC) &&
		(water == SG_RUNE_MOVEMENT_WATER_DRY ||
			water == SG_RUNE_MOVEMENT_WATER_SUBMERGED) &&
		(flags & ~(uint32_t)SG_RUNE_MOVEMENT_STATE_AIRBORNE) == 0U &&
		sg_wire_u32(state + 20) == SG_RUNE_COMPACT_INDEX_NONE &&
		((flags & SG_RUNE_MOVEMENT_STATE_AIRBORNE) != 0U) ==
			(support == SG_RUNE_MOVEMENT_SUPPORT_NONE);
}

static int sg_wire_controller_state_valid(const uint8_t *state)
{
	const uint32_t support = sg_wire_u32(state + 4U);
	const uint32_t water = sg_wire_u32(state + 8U);
	const uint32_t flags = sg_wire_u32(state + 16U);

	return (support == SG_RUNE_MOVEMENT_SUPPORT_NONE ||
			support == SG_RUNE_MOVEMENT_SUPPORT_STATIC) &&
		(water == SG_RUNE_MOVEMENT_WATER_DRY ||
			water == SG_RUNE_MOVEMENT_WATER_SUBMERGED) &&
		sg_wire_u32(state + 12U) == SG_HOST_HOOK_IDLE &&
		(flags & ~(uint32_t)SG_RUNE_MOVEMENT_STATE_AIRBORNE) == 0U &&
		sg_wire_u32(state + 20U) == SG_RUNE_COMPACT_INDEX_NONE &&
		((flags & SG_RUNE_MOVEMENT_STATE_AIRBORNE) != 0U) ==
			(support == SG_RUNE_MOVEMENT_SUPPORT_NONE);
}

#if defined(SG_RUNE_COMPACT_WIRE_TESTING)
int SG_RuneCompactWireTestTeleportStateValid(const uint8_t *state);
int SG_RuneCompactWireTestControllerStateValid(const uint8_t *state);

int SG_RuneCompactWireTestTeleportStateValid(const uint8_t *state)
{
	return state != NULL && sg_wire_teleport_state_valid(state);
}

int SG_RuneCompactWireTestControllerStateValid(const uint8_t *state)
{
	return state != NULL && sg_wire_controller_state_valid(state);
}
#endif

static int sg_wire_movement_fiber_kind_valid(uint32_t fiber_kind,
	uint32_t capability_kind)
{
	if (capability_kind >= (uint32_t)SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BOLT &&
		capability_kind <=
			(uint32_t)SG_RUNE_MOVEMENT_CAPABILITY_HOOK_RELAUNCH)
		return fiber_kind == (uint32_t)SG_RUNE_MOVEMENT_FIBER_HOOK;
	if (capability_kind == (uint32_t)SG_RUNE_MOVEMENT_CAPABILITY_MOVER)
		return fiber_kind ==
				(uint32_t)SG_RUNE_MOVEMENT_FIBER_MECHANISM_TRANSITION ||
			fiber_kind == (uint32_t)SG_RUNE_MOVEMENT_FIBER_ANGULAR_MOVER;
	if (capability_kind ==
			(uint32_t)SG_RUNE_MOVEMENT_CAPABILITY_EXTERNAL_FORCE ||
		capability_kind ==
			(uint32_t)SG_RUNE_MOVEMENT_CAPABILITY_CONTROLLER_ACTION)
		return fiber_kind ==
			(uint32_t)SG_RUNE_MOVEMENT_FIBER_MECHANISM_TRANSITION;
	return fiber_kind == (uint32_t)SG_RUNE_MOVEMENT_FIBER_PMOVE;
}

static int sg_wire_hook_target_compare(const uint8_t *left,
	const uint8_t *right)
{
	int comparison = sg_wire_compare_u32(sg_wire_u32(left + 4U),
		sg_wire_u32(right + 4U));

	if (comparison == 0)
		comparison = sg_wire_compare_u32(sg_wire_u32(left + 8U),
			sg_wire_u32(right + 8U));
	if (comparison == 0)
		comparison = sg_wire_compare_u32(sg_wire_u32(left + 12U),
			sg_wire_u32(right + 12U));
	if (comparison == 0)
		comparison = sg_wire_compare_u32(sg_wire_u32(left + 16U),
			sg_wire_u32(right + 16U));
	if (comparison == 0)
		comparison = sg_wire_compare_u32(left[24], right[24]);
	if (comparison == 0)
		comparison = sg_wire_compare_u32(left[25], right[25]);
	return comparison == 0 ? sg_wire_compare_u32(sg_wire_u32(left + 20U),
		sg_wire_u32(right + 20U)) : comparison;
}

static const uint8_t *sg_wire_record_at(const uint8_t *image,
	const sg_wire_desc_t *descs, sg_rune_compact_wire_section_t section,
	uint32_t ordinal)
{
	return image + descs[section].offset +
		(uint64_t)ordinal * sg_wire_specs[section].wire_size;
}

static uint32_t sg_wire_hook_static_target_kind(const uint8_t *image,
	const sg_wire_desc_t *descs, uint32_t response_kind,
	uint32_t response_index)
{
	uint32_t first;
	uint32_t count;
	uint32_t member;
	uint32_t kind = (uint32_t)SG_HOST_HOOK_TARGET_NONE;

	if (response_kind == SG_RUNE_COMPACT_RESPONSE_REF_CERTIFIED_FACT) {
		const uint8_t *fact;
		uint32_t patch;

		if (response_index >=
			descs[SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FACTS].count)
			return kind;
		fact = sg_wire_record_at(image, descs,
			SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FACTS, response_index);
		patch = sg_wire_u32(fact + 4);
		if (patch >= descs[
			SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_PATCHES].count)
			return kind;
		return sg_wire_u32(sg_wire_record_at(image, descs,
			SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_PATCHES, patch) + 24) ==
			SG_RUNE_COMPACT_SOURCE_SURFACE_WORLD ?
			(uint32_t)SG_HOST_HOOK_TARGET_WORLD :
			(uint32_t)SG_HOST_HOOK_TARGET_FUNC;
	}
	if (response_kind != SG_RUNE_COMPACT_RESPONSE_REF_CANDIDATE_GROUP ||
		response_index >= descs[
			SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_CANDIDATE_GROUPS].count)
		return kind;
	{
		const uint8_t *candidate = sg_wire_record_at(image, descs,
			SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_CANDIDATE_GROUPS,
			response_index);
		const uint32_t group_index = sg_wire_u32(candidate + 4);
		const uint8_t *group;

		if (group_index >= descs[
			SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_TARGET_ENDPOINT_GROUPS].count)
			return kind;
		group = sg_wire_record_at(image, descs,
			SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_TARGET_ENDPOINT_GROUPS,
			group_index);
		first = sg_wire_u32(group + 12);
		count = sg_wire_u32(group + 16);
	}
	if (first > descs[
			SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_TARGET_ENDPOINT_MEMBERS].count ||
		count > descs[
			SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_TARGET_ENDPOINT_MEMBERS].count -
			first)
		return kind;
	for (member = 0U; member < count; member++) {
		const uint32_t patch = sg_wire_u32(sg_wire_record_at(image, descs,
			SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_TARGET_ENDPOINT_MEMBERS,
			first + member));
		uint32_t member_kind;

		if (patch >= descs[
			SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_PATCHES].count)
			return (uint32_t)SG_HOST_HOOK_TARGET_NONE;
		member_kind = sg_wire_u32(sg_wire_record_at(image, descs,
			SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_PATCHES, patch) + 24) ==
			SG_RUNE_COMPACT_SOURCE_SURFACE_WORLD ?
			(uint32_t)SG_HOST_HOOK_TARGET_WORLD :
			(uint32_t)SG_HOST_HOOK_TARGET_FUNC;
		if (kind != (uint32_t)SG_HOST_HOOK_TARGET_NONE && kind != member_kind)
			return (uint32_t)SG_HOST_HOOK_TARGET_NONE;
		kind = member_kind;
	}
	return kind;
}

static int sg_wire_response_halfspace_compare(const uint8_t *left,
	const uint8_t *right)
{
	int comparison = sg_wire_compare_u32_array(left, right, 5U);

	if (comparison == 0)
		comparison = sg_wire_compare_u32(left[20U], right[20U]);
	return comparison;
}

static int sg_wire_response_fragment_compare(const uint8_t *image,
	const sg_wire_desc_t *descs, const uint8_t *left, const uint8_t *right)
{
	uint32_t axis;
	uint32_t halfspace;
	int comparison = sg_wire_compare_u32(sg_wire_u32(left), sg_wire_u32(right));

#define SG_WIRE_FRAGMENT_KEY(a, b) \
	do { if (comparison == 0) comparison = sg_wire_compare_u32((a), (b)); } while (0)
	if (comparison == 0)
		comparison = sg_wire_compare_u64(sg_wire_u64(left + 12U),
			sg_wire_u64(right + 12U));
	SG_WIRE_FRAGMENT_KEY(sg_wire_u32(left + 20U), sg_wire_u32(right + 20U));
	SG_WIRE_FRAGMENT_KEY(sg_wire_u32(left + 24U), sg_wire_u32(right + 24U));
	SG_WIRE_FRAGMENT_KEY(sg_wire_u32(left + 72U), sg_wire_u32(right + 72U));
	SG_WIRE_FRAGMENT_KEY(sg_wire_u32(left + 76U), sg_wire_u32(right + 76U));
	SG_WIRE_FRAGMENT_KEY(sg_wire_u32(left + 80U), sg_wire_u32(right + 80U));
	SG_WIRE_FRAGMENT_KEY(left[84U], right[84U]);
	for (axis = 0U; axis < 3U && comparison == 0; axis++) {
		const int32_t left_minimum = sg_wire_i32(left + 36U + axis * 4U);
		const int32_t right_minimum = sg_wire_i32(right + 36U + axis * 4U);
		const int32_t left_maximum = sg_wire_i32(left + 48U + axis * 4U);
		const int32_t right_maximum = sg_wire_i32(right + 48U + axis * 4U);

		comparison = left_minimum < right_minimum ? -1 :
			left_minimum > right_minimum ? 1 : 0;
		if (comparison == 0)
			comparison = left_maximum < right_maximum ? -1 :
				left_maximum > right_maximum ? 1 : 0;
	}
	SG_WIRE_FRAGMENT_KEY(sg_wire_u32(left + 32U), sg_wire_u32(right + 32U));
	for (halfspace = 0U; halfspace < sg_wire_u32(left + 32U) &&
		comparison == 0; halfspace++)
		comparison = sg_wire_response_halfspace_compare(sg_wire_record_at(image,
			descs, SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_HALFSPACES,
			sg_wire_u32(left + 28U) + halfspace), sg_wire_record_at(image,
			descs, SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_HALFSPACES,
			sg_wire_u32(right + 28U) + halfspace));
#undef SG_WIRE_FRAGMENT_KEY
	return comparison;
}

static int sg_wire_response_patch_compare(const uint8_t *image,
	const sg_wire_desc_t *descs, const uint8_t *left, const uint8_t *right)
{
	uint32_t vertex;
	int comparison = sg_wire_compare_u32(sg_wire_u32(left + 8U),
		sg_wire_u32(right + 8U));

#define SG_WIRE_PATCH_KEY(a, b) \
	do { if (comparison == 0) comparison = sg_wire_compare_u32((a), (b)); } while (0)
	SG_WIRE_PATCH_KEY(sg_wire_u32(left + 12U), sg_wire_u32(right + 12U));
	SG_WIRE_PATCH_KEY(sg_wire_u32(left + 16U), sg_wire_u32(right + 16U));
	SG_WIRE_PATCH_KEY(sg_wire_u32(left + 20U), sg_wire_u32(right + 20U));
	SG_WIRE_PATCH_KEY(sg_wire_u32(left + 24U), sg_wire_u32(right + 24U));
	SG_WIRE_PATCH_KEY(sg_wire_u32(left + 108U), sg_wire_u32(right + 108U));
	if (comparison == 0)
		comparison = sg_wire_compare_u64(sg_wire_u64(left), sg_wire_u64(right));
	SG_WIRE_PATCH_KEY(sg_wire_u32(left + 80U), sg_wire_u32(right + 80U));
	for (vertex = 0U; vertex < sg_wire_u32(left + 80U) && comparison == 0;
		vertex++)
		comparison = sg_wire_compare_q8_vec3(sg_wire_record_at(image, descs,
			SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_TARGET_VERTICES,
			sg_wire_u32(left + 76U) + vertex), sg_wire_record_at(image, descs,
			SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_TARGET_VERTICES,
			sg_wire_u32(right + 76U) + vertex));
#undef SG_WIRE_PATCH_KEY
	return comparison;
}

static int sg_wire_response_split_compare(const uint8_t *left,
	const uint8_t *right)
{
	int comparison = sg_wire_compare_u32_array(left, right, 4U);

	if (comparison == 0)
		comparison = sg_wire_compare_u32(sg_wire_u32(left + 16U),
			sg_wire_u32(right + 16U));
	if (comparison == 0)
		comparison = sg_wire_compare_u64(sg_wire_u64(left + 20U),
			sg_wire_u64(right + 20U));
	if (comparison == 0)
		comparison = sg_wire_compare_u32(sg_wire_u32(left + 28U),
			sg_wire_u32(right + 28U));
	if (comparison == 0)
		comparison = sg_wire_compare_u32(sg_wire_u32(left + 32U),
			sg_wire_u32(right + 32U));
	if (comparison == 0)
		comparison = sg_wire_compare_u32(sg_wire_u32(left + 36U),
			sg_wire_u32(right + 36U));
	return comparison;
}

static int sg_wire_response_split_target_surface_valid(const uint8_t *image,
	const sg_wire_desc_t *descs, const uint8_t *split, int require_edge)
{
	uint32_t patch;

	for (patch = 0U; patch < descs[
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_PATCHES].count; patch++) {
		const uint8_t *target = sg_wire_record_at(image, descs,
			SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_PATCHES, patch);

		if (sg_wire_u64(target) == sg_wire_u64(split + 20U) &&
			(!require_edge || sg_wire_u32(split + 32U) <
				sg_wire_u32(target + 80U)))
			return 1;
	}
	return 0;
}

static int sg_wire_kernel_law_valid(uint32_t source_profile,
	uint32_t family, const uint8_t *kernel)
{
	sg_rune_weapon_event_law_t expected;

	return family < (uint32_t)SG_RUNE_WEAPON_RESPONSE_FAMILY_COUNT &&
		SG_RuneCompactWeaponCanonicalEventLaw(source_profile,
			(sg_rune_weapon_response_family_t)family, &expected) &&
		expected.kind ==
			(sg_rune_weapon_event_law_kind_t)sg_wire_u32(kernel + 16) &&
			expected.requirements == sg_wire_u32(kernel + 20);
}

static int sg_wire_weapon_attachment_compare(const uint8_t *left,
	const uint8_t *right)
{
	int comparison = sg_wire_compare_u32(sg_wire_u32(left), sg_wire_u32(right));

	if (comparison == 0)
		comparison = sg_wire_compare_u32(sg_wire_u32(left + 4U),
			sg_wire_u32(right + 4U));
	if (comparison == 0)
		comparison = sg_wire_compare_u32(sg_wire_u32(left + 8U),
			sg_wire_u32(right + 8U));
	return comparison;
}

static int sg_wire_weapon_fact_supports_relation_class(uint32_t relation_class,
	uint32_t flags)
{
	const int direct = (flags & SG_RUNE_COMPACT_STATIC_RELATION_DIRECT) != 0U;
	const int penetrating = (flags &
		SG_RUNE_COMPACT_STATIC_RELATION_PENETRATING) != 0U;
	const int impact = (flags &
		SG_RUNE_COMPACT_STATIC_RELATION_OCCLUDED_IMPACT) != 0U;

	switch ((sg_rune_compact_weapon_relation_class_t)relation_class) {
	case SG_RUNE_COMPACT_WEAPON_RELATION_DIRECT:
		return direct;
	case SG_RUNE_COMPACT_WEAPON_RELATION_RAIL:
		return direct || penetrating;
	case SG_RUNE_COMPACT_WEAPON_RELATION_IMPACT:
		return direct || impact;
	case SG_RUNE_COMPACT_WEAPON_RELATION_CLASS_COUNT:
		return 0;
	}
	return 0;
}

static const uint8_t *sg_wire_find_weapon_attachment(const uint8_t *image,
	const sg_wire_desc_t *descs, uint32_t cell, uint32_t source_surface,
	uint32_t relation_class)
{
	uint32_t lower = 0U;
	uint32_t upper = descs[
		SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_ATTACHMENTS].count;

	while (lower < upper) {
		const uint32_t index = lower + (upper - lower) / 2U;
		const uint8_t *attachment = sg_wire_record_at(image, descs,
			SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_ATTACHMENTS, index);
		int comparison = sg_wire_compare_u32(cell, sg_wire_u32(attachment));

		if (comparison == 0)
			comparison = sg_wire_compare_u32(source_surface,
				sg_wire_u32(attachment + 4U));
		if (comparison == 0)
			comparison = sg_wire_compare_u32(relation_class,
				sg_wire_u32(attachment + 8U));
		if (comparison == 0)
			return attachment;
		if (comparison < 0)
			upper = index;
		else
			lower = index + 1U;
	}
	return NULL;
}

static int sg_wire_weapon_attachment_references_fact(const uint8_t *image,
	const sg_wire_desc_t *descs, const uint8_t *attachment,
	uint32_t fact_index)
{
	const uint32_t first = sg_wire_u32(attachment + 16U);
	uint32_t lower = 0U;
	uint32_t upper = sg_wire_u32(attachment + 20U);

	while (lower < upper) {
		const uint32_t offset = lower + (upper - lower) / 2U;
		const uint8_t *reference = sg_wire_record_at(image, descs,
			SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_RELATION_REFS,
			first + offset);
		const uint32_t referenced_fact = sg_wire_u32(reference + 4U);

		if (referenced_fact == fact_index)
			return 1;
		if (fact_index < referenced_fact)
			upper = offset;
		else
			lower = offset + 1U;
	}
	return 0;
}

static int sg_wire_authority_transition_states_valid(const uint8_t *authority,
	const uint8_t *transition)
{
	const uint32_t kind = sg_wire_u32(transition + 4U);
	const uint32_t source = sg_wire_u32(transition + 16U);
	const uint32_t destination = sg_wire_u32(transition + 20U);
	const uint32_t authority_kind = sg_wire_u32(authority + 4U);

	if (kind ==
		(uint32_t)SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE)
		return source == sg_wire_u32(authority + 104U) &&
			destination == sg_wire_u32(authority + 108U);
	if (kind == (uint32_t)SG_RUNE_COMPACT_MECHANISM_TRANSITION_TELEPORT ||
		kind == (uint32_t)SG_RUNE_COMPACT_MECHANISM_TRANSITION_PUSH)
		return source ==
				(uint32_t)SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE &&
			destination ==
				(uint32_t)SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	if (kind !=
		(uint32_t)SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT)
		return 0;
	if (authority_kind ==
			(uint32_t)SG_RUNE_COMPACT_MECHANISM_AUTHORITY_LIFT ||
		(authority_kind ==
			(uint32_t)SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TRAIN &&
		 (sg_wire_u32(authority + 8U) &
			SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_AUTO) == 0U))
		return source == sg_wire_u32(authority + 104U) &&
			destination == sg_wire_u32(authority + 108U);
	return authority_kind ==
			(uint32_t)SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TRAIN &&
		source == (uint32_t)SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE &&
		destination ==
			(uint32_t)SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
}

/* Weapon profiles are a fixed-width wire record.  Decode the complete sealed
 * shape before validating a kernel: the profile id and family mask alone are
 * not enough to establish the exact pellet/lane/direct response contract. */
static void sg_wire_get_weapon_profile(const uint8_t *p,
	sg_rune_weapon_profile_t *profile)
{
	profile->source_profile = sg_wire_u32(p);
	profile->response_families = sg_wire_u32(p + 4);
	profile->projectile_count_min = sg_wire_u16(p + 8);
	profile->projectile_count_max = sg_wire_u16(p + 10);
	profile->auxiliary_trace_count = sg_wire_u16(p + 12);
	profile->direct_response_count = p[14];
	profile->reserved = p[15];
}

static int sg_wire_validate_records(const uint8_t *image,
	const sg_wire_desc_t *descs, sg_rune_compact_wire_error_t *error)
{
	const uint8_t *p;
	const uint8_t *profile_record;
	const uint8_t *identity_record;
	const uint8_t *function_record;
	const uint8_t *relation_span_record;
	sg_rune_weapon_profile_t compact_profile;
	sg_rune_weapon_profile_t compact_profiles[
		SG_RUNE_COMPACT_CANONICAL_WEAPON_PROFILE_COUNT];
	uint64_t weapon_profile_catalog_id;
	uint32_t i;
	uint32_t profile_index;
	uint32_t expected_kernel_cursor = 0U;
	uint32_t weapon_relation_cursor = 0U;
	uint32_t form;
	uint32_t definition_limit;
	uint32_t mechanism_controller_cursor = 0U;
	uint32_t mechanism_edge_cursor = 0U;
	uint32_t transition_cursor = 0U;
	uint32_t authority_controller_cursor = 0U;
	uint32_t authority_topology_cursor = 0U;
	uint32_t authority_transition_cursor = 0U;
	uint32_t movement_fiber_cursor = 0U;
	uint32_t movement_target_cursor = 0U;
	uint32_t movement_function_cursor = 0U;
	uint32_t source_vertex_cursor = 0U;
	uint32_t response_halfspace_cursor = 0U;
	uint32_t response_target_vertex_cursor = 0U;
	uint32_t current_source_root = SG_RUNE_COMPACT_INDEX_NONE;
	uint32_t previous_source_root = SG_RUNE_COMPACT_INDEX_NONE;
	uint32_t previous_source_child = SG_RUNE_COMPACT_INDEX_NONE;
#define SG_COUNT(section_name) descs[section_name].count
#define SG_RECORD(section_name, ordinal) \
	(image + descs[section_name].offset + \
	 (uint64_t)(ordinal) * sg_wire_specs[section_name].wire_size)
#define SG_FAIL(code, section_name, ordinal) \
	return sg_wire_record_error(error, code, section_name, ordinal)
	identity_record = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_IDENTITY, 0U);
	for (i = 0; i < SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_CELLS); ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_CELLS, i);
		if (!sg_wire_span(sg_wire_u32(p + 44), sg_wire_u32(p + 48),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_CELL_INCIDENCES)) ||
			!sg_wire_span(sg_wire_u32(p + 52), sg_wire_u32(p + 56),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_CAPABILITIES)))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_SPAN,
				SG_RUNE_COMPACT_WIRE_SECTION_CELLS, i);
		if (!sg_wire_zero(p + 60, 8))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_NONZERO_RESERVED,
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
	for (i = 0U; i < SG_COUNT(
		SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACES); ++i)
	{
		uint32_t parent;
		uint32_t cell;
		uint32_t frame;
		uint32_t vertex_first;
		uint32_t vertex_count;
		uint32_t vertex;

		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACES, i);
		parent = sg_wire_u32(p + 24);
		cell = sg_wire_u32(p + 20);
		frame = sg_wire_u32(p + 16);
		vertex_first = sg_wire_u32(p + 48);
		vertex_count = sg_wire_u32(p + 52);
		if (sg_wire_u32(p) >= sg_wire_u32(identity_record + 136) ||
			sg_wire_u32(p + 4) >= sg_wire_u32(identity_record + 152) ||
			sg_wire_u32(p + 8) >= sg_wire_u32(identity_record + 156) ||
			sg_wire_u32(p + 12) >= sg_wire_u32(identity_record + 148) ||
			frame >= (uint32_t)SG_RUNE_COMPACT_SOURCE_SURFACE_FRAME_COUNT ||
			frame != (sg_wire_u32(p) == 0U ?
				(uint32_t)SG_RUNE_COMPACT_SOURCE_SURFACE_WORLD :
				(uint32_t)SG_RUNE_COMPACT_SOURCE_SURFACE_MODEL_LOCAL) ||
			vertex_first != source_vertex_cursor || vertex_count < 3U ||
			!sg_wire_span(vertex_first, vertex_count, SG_COUNT(
				SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACE_VERTICES)))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
				SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACES, i);
		if (parent == SG_RUNE_COMPACT_INDEX_NONE) {
			if (cell != SG_RUNE_COMPACT_INDEX_NONE || sg_wire_u32(p + 28) != 0U ||
				(previous_source_root != SG_RUNE_COMPACT_INDEX_NONE &&
				 sg_wire_source_surface_provenance_compare(
					SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACES,
						previous_source_root), p) >= 0))
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
					SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACES, i);
			previous_source_root = i;
			current_source_root = i;
			previous_source_child = SG_RUNE_COMPACT_INDEX_NONE;
		}
		else {
			const uint8_t *root;

			if (parent != current_source_root || cell >= SG_COUNT(
					SG_RUNE_COMPACT_WIRE_SECTION_CELLS) || sg_wire_u32(p) != 0U ||
				frame != (uint32_t)SG_RUNE_COMPACT_SOURCE_SURFACE_WORLD ||
				sg_wire_u32(p + 28) == 0U ||
				sg_wire_u32(SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_CELLS,
					cell)) != 0U)
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACES, i);
			root = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACES,
				current_source_root);
			if (memcmp(p, root, 16U) != 0 || memcmp(p + 32, root + 32,
				16U) != 0 ||
				(previous_source_child != SG_RUNE_COMPACT_INDEX_NONE &&
				 (sg_wire_u32(SG_RECORD(
					SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACES,
					previous_source_child) + 20) > cell ||
				  (sg_wire_u32(SG_RECORD(
					SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACES,
					previous_source_child) + 20) == cell &&
				   sg_wire_u32(SG_RECORD(
					SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACES,
					previous_source_child) + 28) >= sg_wire_u32(p + 28)))))
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
					SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACES, i);
			previous_source_child = i;
		}
		if (!sg_wire_published_plane_valid(p + 32U))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
				SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACES, i);
		for (vertex = 0U; vertex < vertex_count; vertex++)
			if (!sg_wire_q8_vertex_on_plane(p + 32U, SG_RECORD(
				SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACE_VERTICES,
				vertex_first + vertex)))
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
					SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACES, i);
		source_vertex_cursor += vertex_count;
	}
	if (source_vertex_cursor != SG_COUNT(
		SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACE_VERTICES))
		SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_SPAN,
			SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACES, source_vertex_cursor);
	for (i = 0U; i < SG_COUNT(
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FRAGMENTS); i++) {
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FRAGMENTS, i);
		if (!sg_wire_ref(sg_wire_u32(p),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_CELLS), 0) ||
			!sg_wire_span(sg_wire_u32(p + 4U), sg_wire_u32(p + 8U),
				SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_CELL_INCIDENCES)) ||
			sg_wire_u32(p + 8U) == 0U ||
			sg_wire_u32(p + 28U) != response_halfspace_cursor ||
			!sg_wire_span(sg_wire_u32(p + 28U), sg_wire_u32(p + 32U),
				SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_HALFSPACES)) ||
			!sg_wire_bounds_valid(p + 36U) ||
			!sg_wire_point_in_bounds(p + 60U, p + 36U) ||
			sg_wire_u32(p + 72U) >= sg_wire_u32(identity_record + 140U) ||
			sg_wire_u32(p + 76U) >= sg_wire_u32(identity_record + 144U) ||
			(p[84U] & (uint8_t)~SG_RUNE_STANCE_VALID_ALL) != 0U ||
			p[84U] == 0U || !sg_wire_zero(p + 85U, 3U) ||
			(i != 0U && sg_wire_response_fragment_compare(image, descs,
				SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FRAGMENTS,
					i - 1U), p) >= 0))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
				SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FRAGMENTS, i);
		response_halfspace_cursor += sg_wire_u32(p + 32U);
	}
	if (response_halfspace_cursor != SG_COUNT(
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_HALFSPACES))
		SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_SPAN,
			SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_HALFSPACES,
			response_halfspace_cursor);
	for (i = 0U; i < SG_COUNT(
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_HALFSPACES); i++) {
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_HALFSPACES, i);
		if (!sg_wire_published_plane_valid(p) ||
			!sg_wire_ref(sg_wire_u32(p + 16U),
				SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SPLITS), 1) ||
			p[20U] > 1U || !sg_wire_zero(p + 21U, 3U))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
				SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_HALFSPACES, i);
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
		if (sg_wire_u32(p + 56) >=
			(uint32_t)SG_RUNE_COMPACT_FACET_KIND_COUNT)
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
				SG_RUNE_COMPACT_WIRE_SECTION_FACETS, i);
		switch ((sg_rune_compact_facet_kind_t)sg_wire_u32(p + 56))
		{
		case SG_RUNE_COMPACT_FACET_POLYGON:
			if (sg_wire_u32(p + 40) < UINT32_C(3))
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
					SG_RUNE_COMPACT_WIRE_SECTION_FACETS, i);
			break;
		case SG_RUNE_COMPACT_FACET_CONSTRAINT_ONLY:
			if (sg_wire_u32(p + 40) != 0 ||
				sg_wire_u32(p + 48) != 1 ||
				sg_wire_u32(p + 52) != SG_RUNE_COMPACT_INDEX_NONE)
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
					SG_RUNE_COMPACT_WIRE_SECTION_FACETS, i);
			break;
		default:
			break;
		}
	}
	for (i = 0U; i < SG_COUNT(
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_PATCHES); ++i)
	{
		const uint8_t *source_surface;
		uint32_t vertex;
		const uint32_t parent_facet = sg_wire_u32(
			SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_PATCHES, i) +
			28U);
		const uint32_t flags = sg_wire_u32(
			SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_PATCHES, i) +
			120U);

		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_PATCHES, i);
		if (!sg_wire_ref(sg_wire_u32(p + 20U), SG_COUNT(
				SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACES), 0) ||
			!sg_wire_span(sg_wire_u32(p + 76U), sg_wire_u32(p + 80U),
				SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_TARGET_VERTICES)) ||
			sg_wire_u32(p + 80U) < 3U ||
			sg_wire_u32(p + 76U) != response_target_vertex_cursor ||
			!sg_wire_response_patch_bounds_valid(p, SG_RECORD(
				SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_TARGET_VERTICES,
				sg_wire_u32(p + 76U))) ||
			(flags & ~(uint32_t)(SG_RUNE_COMPACT_RESPONSE_PATCH_HOOKABLE |
				SG_RUNE_COMPACT_RESPONSE_PATCH_SKY |
				SG_RUNE_COMPACT_RESPONSE_PATCH_MOVING)) != 0U ||
			(p[124U] & (uint8_t)~SG_RUNE_STANCE_VALID_ALL) != 0U ||
			!sg_wire_zero(p + 125U, 3U) ||
			((flags & SG_RUNE_COMPACT_RESPONSE_PATCH_SKY) != 0U &&
			 (flags & SG_RUNE_COMPACT_RESPONSE_PATCH_HOOKABLE) != 0U) ||
			(parent_facet == SG_RUNE_COMPACT_INDEX_NONE &&
			 (sg_wire_u32(p + 36U) != 0U || sg_wire_u32(p + 40U) != 0U)))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
				SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_PATCHES, i);
		source_surface = SG_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACES, sg_wire_u32(p + 20U));
		if (sg_wire_u32(p + 8U) != sg_wire_u32(source_surface) ||
			sg_wire_u32(p + 12U) != sg_wire_u32(source_surface + 4U) ||
			sg_wire_u32(p + 16U) != sg_wire_u32(source_surface + 8U) ||
			sg_wire_u32(p + 24U) != sg_wire_u32(source_surface + 16U) ||
			memcmp(p + 60U, source_surface + 32U, 16U) != 0 ||
			(parent_facet != SG_RUNE_COMPACT_INDEX_NONE &&
			 (!sg_wire_ref(parent_facet,
				SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_FACETS), 0) ||
			  sg_wire_u32(SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_FACETS,
					parent_facet) + 56U) !=
					(uint32_t)SG_RUNE_COMPACT_FACET_POLYGON)))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_PATCHES, i);
		for (vertex = 0U; vertex < sg_wire_u32(p + 80U); vertex++)
			if (!sg_wire_q8_vertex_on_plane(p + 60U, SG_RECORD(
				SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_TARGET_VERTICES,
				sg_wire_u32(p + 76U) + vertex)))
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
					SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_PATCHES, i);
		if (i != 0U && sg_wire_response_patch_compare(image, descs,
			SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_PATCHES,
				i - 1U), p) >= 0)
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
				SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_PATCHES, i);
		response_target_vertex_cursor += sg_wire_u32(p + 80U);
		if ((flags & SG_RUNE_COMPACT_RESPONSE_PATCH_SKY) == 0U &&
			(!sg_wire_ref(sg_wire_u32(p + 32U),
				SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_CELLS), 0) ||
			 (parent_facet != SG_RUNE_COMPACT_INDEX_NONE &&
			  (!sg_wire_span(sg_wire_u32(p + 36U), sg_wire_u32(p + 40U),
				SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_INCIDENCES)) ||
			   sg_wire_u32(p + 40U) == 0U))))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_PATCHES, i);
	}
	if (response_target_vertex_cursor != SG_COUNT(
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_TARGET_VERTICES))
		SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_SPAN,
			SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_TARGET_VERTICES,
			response_target_vertex_cursor);
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
	for (i = 0; i < SG_COUNT(
		SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_CAPABILITIES); ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_CAPABILITIES, i);
		if (i != 0U && sg_wire_movement_field_compare(
				SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_CAPABILITIES,
					i - 1U), p) >= 0)
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
				SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_CAPABILITIES, i);
		if (!sg_wire_ref(sg_wire_u32(p),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_CELLS), 0) ||
			!sg_wire_ref(sg_wire_u32(p + 4),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_PORTALS), 1))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_CAPABILITIES, i);
		if (sg_wire_u32(p + 8) >=
			(uint32_t)SG_RUNE_MOVEMENT_CAPABILITY_KIND_COUNT)
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
				SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_CAPABILITIES, i);
		if (!sg_wire_zero(p + 14, 2))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_NONZERO_RESERVED,
				SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_CAPABILITIES, i);
		if ((p[12] & (uint8_t)~SG_RUNE_STANCE_VALID_ALL) != 0 || p[12] == 0U ||
			(p[13] & (uint8_t)~SG_RUNE_STANCE_VALID_ALL) != 0 || p[13] == 0U)
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
				SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_CAPABILITIES, i);
		if (!sg_wire_span(sg_wire_u32(p + 16), sg_wire_u32(p + 20),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBERS)) ||
			sg_wire_u32(p + 16) != movement_fiber_cursor ||
			sg_wire_u32(p + 20) == 0U)
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_SPAN,
				SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_CAPABILITIES, i);
		if (sg_wire_u32(p + 8) ==
			(uint32_t)SG_RUNE_MOVEMENT_CAPABILITY_HOOK_RELEASE) {
			uint32_t coverage = 0U;
			uint32_t offset;

			if (sg_wire_u32(p + 20) != 4U)
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_CAPABILITIES, i);
			for (offset = 0U; offset < 4U; offset++) {
				const uint8_t *fiber = SG_RECORD(
					SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBERS,
					sg_wire_u32(p + 16) + offset);
				const uint32_t source_index = sg_wire_u32(fiber + 12);
				const uint32_t destination_index = sg_wire_u32(fiber + 16);
				const uint8_t *source;
				const uint8_t *destination;
				uint32_t variant;

				if (!sg_wire_ref(source_index, SG_COUNT(
						SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_STATES), 0) ||
					!sg_wire_ref(destination_index, SG_COUNT(
						SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_STATES), 0))
					SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
						SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBERS,
						sg_wire_u32(p + 16) + offset);
				source = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_STATES,
					source_index);
				destination = SG_RECORD(
					SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_STATES,
					destination_index);
				if (sg_wire_u32(source + 12) == SG_HOST_HOOK_IN_FLIGHT)
					variant = 0U;
				else if (sg_wire_u32(source + 12) == SG_HOST_HOOK_ATTACHED)
					variant = 2U;
				else
					SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
						SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBERS,
						sg_wire_u32(p + 16) + offset);
				if (sg_wire_u32(source + 4) ==
					SG_RUNE_MOVEMENT_SUPPORT_STATIC)
					variant++;
				else if (sg_wire_u32(source + 4) !=
					SG_RUNE_MOVEMENT_SUPPORT_NONE)
					SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
						SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBERS,
						sg_wire_u32(p + 16) + offset);
				if (sg_wire_u32(destination + 12) != SG_HOST_HOOK_COAST ||
					sg_wire_u32(destination + 4) != sg_wire_u32(source + 4) ||
					(sg_wire_u32(source + 16) &
						SG_RUNE_MOVEMENT_STATE_MOVER_RELATIVE) != 0U ||
					(sg_wire_u32(destination + 16) &
						SG_RUNE_MOVEMENT_STATE_MOVER_RELATIVE) != 0U ||
					sg_wire_u32(source + 20) != SG_RUNE_COMPACT_INDEX_NONE ||
					sg_wire_u32(destination + 20) != SG_RUNE_COMPACT_INDEX_NONE ||
					(coverage & (UINT32_C(1) << variant)) != 0U)
					SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
						SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBERS,
						sg_wire_u32(p + 16) + offset);
				if (variant == 0U || variant == 2U) {
					if ((sg_wire_u32(source + 16) &
						SG_RUNE_MOVEMENT_STATE_AIRBORNE) == 0U ||
						(sg_wire_u32(destination + 16) &
						SG_RUNE_MOVEMENT_STATE_AIRBORNE) == 0U)
						SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
							SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBERS,
							sg_wire_u32(p + 16) + offset);
				} else if ((sg_wire_u32(source + 16) &
						SG_RUNE_MOVEMENT_STATE_AIRBORNE) != 0U ||
					(sg_wire_u32(destination + 16) &
						SG_RUNE_MOVEMENT_STATE_AIRBORNE) != 0U)
					SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
						SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBERS,
						sg_wire_u32(p + 16) + offset);
				if (sg_wire_u32(source + 12) == SG_HOST_HOOK_IN_FLIGHT &&
					sg_wire_u32(fiber + 32) != 0U)
					SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
						SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBERS,
						sg_wire_u32(p + 16) + offset);
				coverage |= UINT32_C(1) << variant;
			}
			if (coverage != 15U)
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_CAPABILITIES, i);
		}
		movement_fiber_cursor += sg_wire_u32(p + 20);
	}
	if (movement_fiber_cursor != SG_COUNT(
		SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBERS))
		SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_SPAN,
			SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_CAPABILITIES,
			movement_fiber_cursor);
	if (SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_RUNTIME) != 1U)
		SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
			SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_RUNTIME, 0U);
	for (i = 0U; i < SG_COUNT(
		SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_STATES); i++) {
		const int mover = sg_wire_u32(SG_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_STATES, i) + 4) ==
				(uint32_t)SG_RUNE_MOVEMENT_SUPPORT_MOVER ||
			(sg_wire_u32(SG_RECORD(
				SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_STATES, i) + 16) &
				SG_RUNE_MOVEMENT_STATE_MOVER_RELATIVE) != 0U;

		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_STATES, i);
		if ((i != 0U && sg_wire_movement_state_compare(SG_RECORD(
				SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_STATES, i - 1U), p) >= 0) ||
			(p[0] & (uint8_t)~SG_RUNE_STANCE_VALID_ALL) != 0U || p[0] == 0U ||
			(p[0] & (p[0] - UINT8_C(1))) != 0U || !sg_wire_zero(p + 1, 3) ||
			sg_wire_u32(p + 4) >= SG_RUNE_MOVEMENT_SUPPORT_KIND_COUNT ||
			sg_wire_u32(p + 8) >= SG_RUNE_MOVEMENT_WATER_KIND_COUNT ||
			sg_wire_u32(p + 12) > (uint32_t)SG_HOST_HOOK_COAST ||
			(sg_wire_u32(p + 16) &
				~(uint32_t)SG_RUNE_MOVEMENT_STATE_FLAGS_KNOWN) != 0U ||
			(mover ? !sg_wire_ref(sg_wire_u32(p + 20), SG_COUNT(
				SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITIES), 0) :
			 sg_wire_u32(p + 20) != SG_RUNE_COMPACT_INDEX_NONE))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
				SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_STATES, i);
	}
	for (i = 0U; i < SG_COUNT(
		SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBERS); i++) {
		const uint32_t capability = sg_wire_u32(SG_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBERS, i));
		const uint8_t *owner;
		uint32_t capability_kind;
		uint32_t fiber_kind;
		uint32_t transition_kind =
			(uint32_t)SG_RUNE_COMPACT_MECHANISM_TRANSITION_KIND_COUNT;

		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBERS, i);
		if (!sg_wire_ref(capability, SG_COUNT(
				SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_CAPABILITIES), 0) ||
			sg_wire_u32(p + 4) >= SG_RUNE_MOVEMENT_FIBER_KIND_COUNT ||
			(sg_wire_u32(p + 8) &
				~(uint32_t)SG_RUNE_MOVEMENT_STATE_VARIABLES_KNOWN) != 0U ||
			!sg_wire_ref(sg_wire_u32(p + 12), SG_COUNT(
				SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_STATES), 0) ||
			!sg_wire_ref(sg_wire_u32(p + 16), SG_COUNT(
				SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_STATES), 0) ||
			sg_wire_u32(p + 20) != movement_function_cursor ||
			!sg_wire_span(sg_wire_u32(p + 20), sg_wire_u32(p + 24), SG_COUNT(
				SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBER_FUNCTION_REFS)) ||
			sg_wire_u32(p + 28) != movement_target_cursor ||
			!sg_wire_span(sg_wire_u32(p + 28), sg_wire_u32(p + 32), SG_COUNT(
				SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_HOOK_TARGETS)) ||
			!sg_wire_ref(sg_wire_u32(p + 36), SG_COUNT(
				SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TRANSITIONS), 1) ||
			!sg_wire_ref(sg_wire_u32(p + 40), SG_COUNT(
				SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_ANGULAR_SCHEDULES), 1) ||
			!sg_wire_ref(sg_wire_u32(p + 44), SG_COUNT(
				SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_CONTROLLERS), 1) ||
			!sg_wire_ref(sg_wire_u32(p + 48), SG_COUNT(
				SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITIES), 1))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBERS, i);
		owner = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_CAPABILITIES,
			capability);
		capability_kind = sg_wire_u32(owner + 8);
		fiber_kind = sg_wire_u32(p + 4);
		if (fiber_kind ==
				(uint32_t)SG_RUNE_MOVEMENT_FIBER_MECHANISM_TRANSITION &&
			sg_wire_u32(p + 36) < SG_COUNT(
				SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TRANSITIONS))
			transition_kind = sg_wire_u32(SG_RECORD(
				SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TRANSITIONS,
				sg_wire_u32(p + 36)) + 4);
		if (i < sg_wire_u32(owner + 16) ||
			i - sg_wire_u32(owner + 16) >= sg_wire_u32(owner + 20) ||
			sg_wire_u32(p + 8) !=
				sg_wire_movement_state_variables(capability_kind,
					transition_kind) ||
			!sg_wire_movement_fiber_kind_valid(fiber_kind, capability_kind) ||
			(fiber_kind == (uint32_t)SG_RUNE_MOVEMENT_FIBER_HOOK ?
				(sg_wire_u32(p + 32) == 0U && capability_kind !=
					(uint32_t)SG_RUNE_MOVEMENT_CAPABILITY_HOOK_RELEASE &&
				 capability_kind !=
					(uint32_t)SG_RUNE_MOVEMENT_CAPABILITY_HOOK_COAST) :
				sg_wire_u32(p + 32) != 0U) ||
			(fiber_kind ==
				(uint32_t)SG_RUNE_MOVEMENT_FIBER_MECHANISM_TRANSITION ?
				sg_wire_u32(p + 36) == SG_RUNE_COMPACT_INDEX_NONE :
				sg_wire_u32(p + 36) != SG_RUNE_COMPACT_INDEX_NONE) ||
			(fiber_kind == (uint32_t)SG_RUNE_MOVEMENT_FIBER_ANGULAR_MOVER ?
				sg_wire_u32(p + 40) == SG_RUNE_COMPACT_INDEX_NONE :
				sg_wire_u32(p + 40) != SG_RUNE_COMPACT_INDEX_NONE) ||
			(capability_kind ==
				(uint32_t)SG_RUNE_MOVEMENT_CAPABILITY_CONTROLLER_ACTION ?
				(sg_wire_u32(p + 44) == SG_RUNE_COMPACT_INDEX_NONE ||
				 sg_wire_u32(p + 48) == SG_RUNE_COMPACT_INDEX_NONE) :
				(sg_wire_u32(p + 44) != SG_RUNE_COMPACT_INDEX_NONE ||
				 sg_wire_u32(p + 48) != SG_RUNE_COMPACT_INDEX_NONE)))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBERS, i);
		if (capability_kind ==
			(uint32_t)SG_RUNE_MOVEMENT_CAPABILITY_CONTROLLER_ACTION) {
			const uint8_t *controller = SG_RECORD(
				SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_CONTROLLERS,
				sg_wire_u32(p + 44));
			const uint8_t *transition = SG_RECORD(
				SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TRANSITIONS,
				sg_wire_u32(p + 36));

			if (controller[32] !=
					SG_RUNE_COMPACT_MECHANISM_CONTROLLER_PLAYER_SPATIAL ||
				sg_wire_u32(controller) != sg_wire_u32(p + 48) ||
				sg_wire_u32(transition) != sg_wire_u32(p + 48) ||
				sg_wire_u32(controller + 36) != sg_wire_u32(owner) ||
				sg_wire_u32(owner + 4) != SG_RUNE_COMPACT_INDEX_NONE ||
				sg_wire_u32(p + 12) != sg_wire_u32(p + 16) ||
				!sg_wire_controller_state_valid(SG_RECORD(
					SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_STATES,
					sg_wire_u32(p + 12))))
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBERS, i);
		}
		if (capability_kind >=
				(uint32_t)SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BOLT &&
			capability_kind <=
				(uint32_t)SG_RUNE_MOVEMENT_CAPABILITY_HOOK_RELAUNCH) {
			static const uint32_t source_phases[6] = {
				SG_HOST_HOOK_IDLE, SG_HOST_HOOK_IN_FLIGHT,
				SG_HOST_HOOK_ATTACHED, UINT32_MAX,
				SG_HOST_HOOK_COAST, SG_HOST_HOOK_COAST
			};
			static const uint32_t destination_phases[6] = {
				SG_HOST_HOOK_IN_FLIGHT, SG_HOST_HOOK_ATTACHED,
				SG_HOST_HOOK_ATTACHED, SG_HOST_HOOK_COAST,
				SG_HOST_HOOK_COAST, SG_HOST_HOOK_IN_FLIGHT
			};
			const uint32_t phase = capability_kind -
				(uint32_t)SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BOLT;
			const uint8_t *source_state = SG_RECORD(
				SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_STATES,
				sg_wire_u32(p + 12));
			const uint8_t *destination_state = SG_RECORD(
				SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_STATES,
				sg_wire_u32(p + 16));

			if ((capability_kind !=
					(uint32_t)SG_RUNE_MOVEMENT_CAPABILITY_HOOK_RELEASE &&
				 sg_wire_u32(source_state + 12) != source_phases[phase]) ||
				sg_wire_u32(destination_state + 12) !=
					destination_phases[phase])
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBERS, i);
		}
		if (capability_kind == (uint32_t)SG_RUNE_MOVEMENT_CAPABILITY_MOVER) {
			const uint8_t *source_state = SG_RECORD(
				SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_STATES,
				sg_wire_u32(p + 12));
			const uint8_t *destination_state = SG_RECORD(
				SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_STATES,
				sg_wire_u32(p + 16));
			uint32_t expected_authority;
			const int teleport = transition_kind ==
				(uint32_t)SG_RUNE_COMPACT_MECHANISM_TRANSITION_TELEPORT;

			if (teleport) {
				const uint8_t *transition = SG_RECORD(
					SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TRANSITIONS,
					sg_wire_u32(p + 36));

				if (sg_wire_u32(owner) != sg_wire_u32(transition + 8) ||
					sg_wire_u32(owner + 4) != SG_RUNE_COMPACT_INDEX_NONE ||
					!sg_wire_teleport_state_valid(source_state) ||
					!sg_wire_teleport_state_valid(destination_state))
					SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
						SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBERS, i);
			} else {
				if (fiber_kind ==
					(uint32_t)SG_RUNE_MOVEMENT_FIBER_MECHANISM_TRANSITION) {
					expected_authority = sg_wire_u32(SG_RECORD(
						SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TRANSITIONS,
						sg_wire_u32(p + 36)));
				} else {
					const uint8_t *schedule = SG_RECORD(
						SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_ANGULAR_SCHEDULES,
						sg_wire_u32(p + 40));
					const uint8_t *static_mechanism = SG_RECORD(
						SG_RUNE_COMPACT_WIRE_SECTION_MECHANISMS,
						sg_wire_u32(schedule));

					if (sg_wire_u32(owner) != sg_wire_u32(static_mechanism + 4) ||
						sg_wire_u32(owner + 4) != SG_RUNE_COMPACT_INDEX_NONE)
						SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
							SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBERS, i);
					expected_authority = sg_wire_u32(schedule + 72);
				}
				if (sg_wire_u32(source_state + 20) != expected_authority ||
					sg_wire_u32(destination_state + 20) != expected_authority ||
					sg_wire_u32(source_state + 4) !=
						SG_RUNE_MOVEMENT_SUPPORT_MOVER ||
					sg_wire_u32(destination_state + 4) !=
						SG_RUNE_MOVEMENT_SUPPORT_MOVER ||
					(sg_wire_u32(source_state + 16) &
						SG_RUNE_MOVEMENT_STATE_MOVER_RELATIVE) == 0U ||
					(sg_wire_u32(destination_state + 16) &
						SG_RUNE_MOVEMENT_STATE_MOVER_RELATIVE) == 0U)
					SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
						SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBERS, i);
			}
		}
		movement_function_cursor += sg_wire_u32(p + 24);
		movement_target_cursor += sg_wire_u32(p + 32);
	}
	if (movement_target_cursor != SG_COUNT(
		SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_HOOK_TARGETS))
		SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_SPAN,
			SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBERS,
			movement_target_cursor);
	for (i = 0U; i < SG_COUNT(
		SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_HOOK_TARGETS); i++) {
		uint32_t phase;
		const uint32_t target_kind = sg_wire_u32(SG_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_HOOK_TARGETS, i) + 4);
		const uint32_t provenance = sg_wire_u32(SG_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_HOOK_TARGETS, i) + 8);
		const uint32_t response_kind = sg_wire_u32(SG_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_HOOK_TARGETS, i) + 12);
		const uint32_t response_index = sg_wire_u32(SG_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_HOOK_TARGETS, i) + 16);

		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_HOOK_TARGETS, i);
		if (!sg_wire_ref(sg_wire_u32(p), SG_COUNT(
				SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBERS), 0) ||
			target_kind < (uint32_t)SG_HOST_HOOK_TARGET_WORLD ||
			target_kind > (uint32_t)SG_HOST_HOOK_TARGET_INFO_FLAG ||
			provenance >=
				(uint32_t)SG_RUNE_MOVEMENT_HOOK_TARGET_PROVENANCE_COUNT ||
			(provenance ==
				(uint32_t)SG_RUNE_MOVEMENT_HOOK_TARGET_PROVENANCE_GENERIC ?
			 (response_kind != SG_RUNE_COMPACT_RESPONSE_REF_KIND_COUNT ||
			  response_index != SG_RUNE_COMPACT_INDEX_NONE ||
			  sg_wire_u32(p + 20) !=
				(uint32_t)SG_RUNE_MOVEMENT_HOOK_TARGET_CONDITIONAL ||
			  (target_kind != (uint32_t)SG_HOST_HOOK_TARGET_PLAYER &&
			   target_kind != (uint32_t)SG_HOST_HOOK_TARGET_BODYQUE &&
			   target_kind != (uint32_t)SG_HOST_HOOK_TARGET_FUNC &&
			   target_kind != (uint32_t)SG_HOST_HOOK_TARGET_INFO_FLAG)) :
			 (response_kind >= SG_RUNE_COMPACT_RESPONSE_REF_KIND_COUNT ||
			  (target_kind != (uint32_t)SG_HOST_HOOK_TARGET_WORLD &&
			   target_kind != (uint32_t)SG_HOST_HOOK_TARGET_FUNC) ||
			  sg_wire_hook_static_target_kind(image, descs, response_kind,
				response_index) != target_kind ||
			  (response_kind == SG_RUNE_COMPACT_RESPONSE_REF_CANDIDATE_GROUP ?
			   !sg_wire_ref(response_index, SG_COUNT(
				SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_CANDIDATE_GROUPS), 0) ||
			   sg_wire_u32(p + 20) != sg_wire_u32(SG_RECORD(
				SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_CANDIDATE_GROUPS,
				response_index) + 8) :
			   !sg_wire_ref(response_index, SG_COUNT(
				SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FACTS), 0) ||
			   sg_wire_u32(p + 20) != sg_wire_u32(SG_RECORD(
				SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FACTS,
				response_index) + 12)))) ||
			sg_wire_u32(p + 20) >= SG_RUNE_MOVEMENT_HOOK_TARGET_CLASS_COUNT ||
			(p[24] & ~SG_RUNE_STANCE_VALID_ALL) != 0U || p[24] == 0U ||
			(p[25] & ~SG_RUNE_STANCE_VALID_ALL) != 0U || p[25] == 0U ||
			!sg_wire_zero(p + 26, 2))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
				SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_HOOK_TARGETS, i);
		{
			const uint8_t *owner = SG_RECORD(
				SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBERS, sg_wire_u32(p));

			if (i < sg_wire_u32(owner + 28) ||
				i - sg_wire_u32(owner + 28) >= sg_wire_u32(owner + 32) ||
				(i != sg_wire_u32(owner + 28) &&
				 sg_wire_hook_target_compare(SG_RECORD(
					SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_HOOK_TARGETS,
					i - 1U), p) >= 0))
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_HOOK_TARGETS, i);
		}
		for (phase = 0U; phase < 6U; phase++)
			if (sg_wire_u32(p + 28U + phase * 8U) !=
					movement_function_cursor ||
				!sg_wire_span(sg_wire_u32(p + 28U + phase * 8U),
					sg_wire_u32(p + 32U + phase * 8U), SG_COUNT(
					SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBER_FUNCTION_REFS)))
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_SPAN,
					SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_HOOK_TARGETS, i);
			else
				movement_function_cursor +=
					sg_wire_u32(p + 32U + phase * 8U);
	}
	if (movement_function_cursor != SG_COUNT(
		SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBER_FUNCTION_REFS))
		SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_SPAN,
			SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBER_FUNCTION_REFS,
			movement_function_cursor);
	for (i = 0U; i < SG_COUNT(
		SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_ANGULAR_SCHEDULES); i++) {
		uint32_t axis;

		p = SG_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_ANGULAR_SCHEDULES, i);
		if (!sg_wire_ref(sg_wire_u32(p),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_MECHANISMS), 0) ||
			!sg_wire_ref(sg_wire_u32(p + 72), SG_COUNT(
				SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITIES), 0) ||
			sg_wire_u32(p + 4) >= sg_wire_u32(
				SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_IDENTITY, 0) + 160) ||
			sg_wire_u32(p + 8) >= sg_wire_u32(
				SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_IDENTITY, 0) + 136) ||
			(sg_wire_u32(p + 12U) &
				~(uint32_t)(SG_BSP_ENTITY_ANGULAR_MOVER_START_ON |
					SG_BSP_ENTITY_ANGULAR_MOVER_REVERSE |
					SG_BSP_ENTITY_ANGULAR_MOVER_STOP_ON_BLOCK |
					SG_BSP_ENTITY_ANGULAR_MOVER_TOUCH_DAMAGE)) != 0U ||
			sg_wire_u32(p + 68U) == 0U ||
			sg_wire_u32(p + 68U) != sg_wire_u32(identity_record + 244U) ||
			!sg_wire_binary32_nonnegative(sg_wire_u32(p + 64U)) ||
			sg_wire_u32(p + 64U) == 0U ||
			(i != 0U && sg_wire_movement_angular_schedule_compare(
				SG_RECORD(
					SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_ANGULAR_SCHEDULES,
					i - 1U), p) >= 0))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_ANGULAR_SCHEDULES, i);
		for (axis = 0U; axis < 3U; axis++)
			if (!sg_wire_binary32_canonical_finite(
					sg_wire_u32(p + 16U + axis * 4U)) ||
				!sg_wire_binary32_canonical_finite(
					sg_wire_u32(p + 28U + axis * 4U)) ||
				!sg_wire_binary32_canonical_finite(
					sg_wire_u32(p + 40U + axis * 4U)) ||
				!sg_wire_binary32_canonical_finite(
					sg_wire_u32(p + 52U + axis * 4U)))
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
					SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_ANGULAR_SCHEDULES,
					i);
		{
			const uint8_t *static_mechanism = SG_RECORD(
				SG_RUNE_COMPACT_WIRE_SECTION_MECHANISMS, sg_wire_u32(p));
			const uint8_t *authority = SG_RECORD(
				SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITIES,
				sg_wire_u32(p + 72));

			if (sg_wire_u32(static_mechanism) != sg_wire_u32(p + 4) ||
				sg_wire_u32(authority) != sg_wire_u32(p + 4) ||
				sg_wire_u32(static_mechanism + 128) !=
					(uint32_t)SG_RUNE_COMPACT_MECHANISM_ROTATOR ||
				sg_wire_u32(authority + 4) !=
					(uint32_t)SG_RUNE_COMPACT_MECHANISM_AUTHORITY_ROTATOR ||
				(static_mechanism[148] &
					SG_RUNE_COMPACT_MECHANISM_FINITE_ANGULAR_DOOR) != 0U)
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_ANGULAR_SCHEDULES, i);
		}
	}
	if (SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SEAL) != 1U)
		SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
			SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SEAL, 0U);
	p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SEAL, 0U);
	if (sg_wire_u16(p) != SG_RUNE_COMPACT_RESPONSE_PARTITION_VERSION ||
		sg_wire_u16(p + 2) != 0U || p[88] != 1U || !sg_wire_zero(p + 89, 3) ||
		(sg_wire_u32(p + 4) & SG_RUNE_COMPACT_RESPONSE_SEAL_REQUIRED) !=
			SG_RUNE_COMPACT_RESPONSE_SEAL_REQUIRED ||
		(sg_wire_u32(p + 4) & ~
			(uint32_t)SG_RUNE_COMPACT_RESPONSE_SEAL_KNOWN) != 0U ||
		sg_wire_u32(p + 8) != 0U ||
		sg_wire_u32(p + 12) != SG_COUNT(
			SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FRAGMENTS) ||
		sg_wire_u32(p + 16) != SG_COUNT(
			SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_PATCHES) ||
		sg_wire_u32(p + 20) != SG_COUNT(
			SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SPLITS) ||
		sg_wire_u32(p + 24) != SG_COUNT(
			SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FACTS) ||
		sg_wire_u32(p + 36) != 0U ||
		sg_wire_u32(p + 40) != SG_COUNT(
			SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_CANDIDATE_GROUPS) ||
		sg_wire_u32(p + 44) != SG_COUNT(
			SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SOURCE_ENDPOINT_GROUPS) ||
		sg_wire_u32(p + 48) != SG_COUNT(
			SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_TARGET_ENDPOINT_GROUPS) ||
		sg_wire_u32(p + 52) != SG_COUNT(
			SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SOURCE_ENDPOINT_MEMBERS) ||
		sg_wire_u32(p + 56) != SG_COUNT(
			SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_TARGET_ENDPOINT_MEMBERS) ||
		sg_wire_u32(p + 60) != SG_COUNT(
			SG_RUNE_COMPACT_WIRE_SECTION_STATIC_OCCLUDERS) ||
		sg_wire_u32(p + 64) != SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_FACETS) ||
		sg_wire_u32(p + 68) != SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_CELLS) ||
		sg_wire_u32(p + 72) != SG_COUNT(
			SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACES) ||
		sg_wire_u32(p + 76) != SG_COUNT(
			SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACE_VERTICES) ||
		sg_wire_u64(p + 80) == 0U)
		SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
			SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SEAL, 0U);
	if (SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_PROFILES) !=
		SG_RUNE_COMPACT_CANONICAL_WEAPON_PROFILE_COUNT)
		SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
			SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_PROFILES, 0U);
	for (i = 0; i < SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_PROFILES); ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_PROFILES, i);
		sg_wire_get_weapon_profile(p, &compact_profile);
		if (sg_wire_u32(p) != i + 1U ||
			sg_wire_u32(p + 4) !=
				SG_RuneCompactWeaponCanonicalProfileMask(i + 1U) ||
			!SG_RuneCompactWeaponProfileShapeValid(&compact_profile))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
				SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_PROFILES, i);
		compact_profiles[i] = compact_profile;
	}
	if (!SG_RuneCompactWeaponProfileCatalogId(compact_profiles,
		SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_PROFILES),
		&weapon_profile_catalog_id) ||
		weapon_profile_catalog_id != sg_wire_u64(identity_record + 252))
		SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
			SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_PROFILES, 0U);
	/* The sealed profile mask owns the complete kernel sequence.  In
	 * particular, a GRENADE_FLIGHT row has zero function refs but must remain
	 * present for runtime law selection. */
	for (profile_index = 0U; profile_index < SG_COUNT(
		SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_PROFILES); ++profile_index) {
		const uint32_t mask = compact_profiles[profile_index].response_families;
		uint32_t family;

		for (family = 0U;
			family < (uint32_t)SG_RUNE_WEAPON_RESPONSE_FAMILY_COUNT;
			++family) {
			if ((mask & SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(family)) == 0U)
				continue;
			if (expected_kernel_cursor >= SG_COUNT(
				SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_KERNELS))
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
					SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_KERNELS,
					expected_kernel_cursor);
			p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_KERNELS,
				expected_kernel_cursor);
			if (sg_wire_u32(p) != profile_index ||
				sg_wire_u32(p + 4U) != family)
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
					SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_KERNELS,
					expected_kernel_cursor);
			expected_kernel_cursor++;
		}
	}
	if (expected_kernel_cursor != SG_COUNT(
		SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_KERNELS))
		SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
			SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_KERNELS,
			expected_kernel_cursor);
	for (i = 0; i < SG_COUNT(
		SG_RUNE_COMPACT_WIRE_SECTION_STATIC_OCCLUDERS); ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_STATIC_OCCLUDERS, i);
		if (sg_wire_u32(p) >= sg_wire_u32(identity_record + 136) ||
			sg_wire_u32(p + 4) >= sg_wire_u32(identity_record + 152) ||
			sg_wire_u32(p + 12) > 1U ||
			(i != 0U && sg_wire_static_occluder_compare(
				SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_STATIC_OCCLUDERS,
					i - 1U), p) >= 0))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
				SG_RUNE_COMPACT_WIRE_SECTION_STATIC_OCCLUDERS, i);
	}
	for (i = 0U; i < SG_COUNT(
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SPLITS); i++) {
		const uint8_t *split = SG_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SPLITS, i);
		const uint32_t kind = sg_wire_u32(split + 16U);

		if (!sg_wire_published_plane_valid(split) || kind >=
				(uint32_t)SG_RUNE_COMPACT_RESPONSE_SPLIT_KIND_COUNT ||
			(i != 0U && sg_wire_response_split_compare(SG_RECORD(
				SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SPLITS, i - 1U),
				split) >= 0))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
				SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SPLITS, i);
		switch ((sg_rune_compact_response_split_kind_t)kind)
		{
		case SG_RUNE_COMPACT_RESPONSE_SPLIT_TARGET_EDGE:
			if (sg_wire_u64(split + 20U) == UINT64_MAX ||
				sg_wire_u32(split + 28U) != SG_RUNE_COMPACT_INDEX_NONE ||
				sg_wire_u32(split + 36U) !=
					SG_HOST_COLLISION_BRUSH_NONE ||
				!sg_wire_response_split_target_surface_valid(image, descs,
					split, 1))
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
					SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SPLITS, i);
			break;
		case SG_RUNE_COMPACT_RESPONSE_SPLIT_OCCLUDER_PLANE:
			if (sg_wire_u64(split + 20U) != UINT64_MAX ||
				!sg_wire_ref(sg_wire_u32(split + 28U), SG_COUNT(
					SG_RUNE_COMPACT_WIRE_SECTION_STATIC_OCCLUDERS), 0) ||
				sg_wire_u32(split + 32U) == SG_RUNE_COMPACT_INDEX_NONE ||
				sg_wire_u32(split + 36U) ==
					SG_HOST_COLLISION_BRUSH_NONE ||
				sg_wire_u32(split + 36U) >=
					sg_wire_u32(identity_record + 156U))
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
					SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SPLITS, i);
			break;
		case SG_RUNE_COMPACT_RESPONSE_SPLIT_OCCLUDER_EDGE:
			if (sg_wire_u64(split + 20U) != UINT64_MAX ||
				!sg_wire_ref(sg_wire_u32(split + 28U), SG_COUNT(
					SG_RUNE_COMPACT_WIRE_SECTION_STATIC_OCCLUDERS), 0) ||
				sg_wire_u32(split + 32U) == SG_RUNE_COMPACT_INDEX_NONE ||
				sg_wire_u32(split + 36U) !=
					SG_HOST_COLLISION_BRUSH_NONE)
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
					SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SPLITS, i);
			break;
		case SG_RUNE_COMPACT_RESPONSE_SPLIT_FIRST_HIT_TIE:
			if (sg_wire_u64(split + 20U) == UINT64_MAX ||
				!sg_wire_ref(sg_wire_u32(split + 28U), SG_COUNT(
					SG_RUNE_COMPACT_WIRE_SECTION_STATIC_OCCLUDERS), 0) ||
				sg_wire_u32(split + 32U) == SG_RUNE_COMPACT_INDEX_NONE ||
				sg_wire_u32(split + 36U) !=
					SG_HOST_COLLISION_BRUSH_NONE ||
				!sg_wire_response_split_target_surface_valid(image, descs,
					split, 0))
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
					SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SPLITS, i);
			break;
		case SG_RUNE_COMPACT_RESPONSE_SPLIT_KIND_COUNT:
		default:
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
				SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SPLITS, i);
		}
	}
	for (i = 0U; i < SG_COUNT(
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FACTS); i++) {
		const uint8_t *fragment;
		const uint8_t *patch;
		const uint8_t *vertices;
		const uint8_t *trace;
		uint32_t certificate;

		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FACTS, i);
		if (!sg_wire_ref(sg_wire_u32(p), SG_COUNT(
				SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FRAGMENTS), 0) ||
			!sg_wire_ref(sg_wire_u32(p + 4U), SG_COUNT(
				SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_PATCHES), 0) ||
			!sg_wire_span(sg_wire_u32(p + 40U), sg_wire_u32(p + 44U),
				SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_STATIC_OCCLUDERS)) ||
			!sg_wire_zero(p + 22U, 2U))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FACTS, i);
		fragment = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FRAGMENTS,
			sg_wire_u32(p));
		patch = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_PATCHES,
			sg_wire_u32(p + 4U));
		vertices = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_TARGET_VERTICES,
			sg_wire_u32(patch + 76U));
		trace = p + 48U;
		certificate = sg_wire_u32(p + 8U) &
			(SG_RUNE_COMPACT_STATIC_RELATION_DIRECT |
			 SG_RUNE_COMPACT_STATIC_RELATION_OCCLUDED_IMPACT);
		if (!sg_wire_response_trace_finite_and_bound(p, fragment, patch,
			vertices))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
				SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FACTS, i);
		if (certificate == SG_RUNE_COMPACT_STATIC_RELATION_DIRECT) {
			if (sg_wire_u32(p + 44U) != 0U ||
				sg_wire_u32(p + 24U) != SG_RUNE_COMPACT_INDEX_NONE ||
				!sg_wire_response_trace_canonical_no_hit(trace) ||
				!sg_wire_response_trace_ends_at_target(trace, p + 28U))
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
					SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FACTS, i);
		} else if (certificate ==
			SG_RUNE_COMPACT_STATIC_RELATION_OCCLUDED_IMPACT) {
			const uint8_t *split;
			const uint8_t *occluder;
			double residual;
			uint32_t axis;

			if (sg_wire_u32(p + 44U) != 1U ||
				!sg_wire_ref(sg_wire_u32(p + 24U), SG_COUNT(
					SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SPLITS), 0))
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
					SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FACTS, i);
			split = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SPLITS,
				sg_wire_u32(p + 24U));
			if (sg_wire_u32(split + 16U) !=
					SG_RUNE_COMPACT_RESPONSE_SPLIT_OCCLUDER_PLANE ||
				!sg_wire_published_plane_valid(split) ||
				sg_wire_u32(split + 28U) != sg_wire_u32(p + 40U) ||
				!sg_wire_ref(sg_wire_u32(split + 28U), SG_COUNT(
					SG_RUNE_COMPACT_WIRE_SECTION_STATIC_OCCLUDERS), 0))
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
					SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FACTS, i);
			occluder = SG_RECORD(
				SG_RUNE_COMPACT_WIRE_SECTION_STATIC_OCCLUDERS,
				sg_wire_u32(split + 28U));
			if (sg_wire_u32(trace + 68U) != sg_wire_u32(occluder + 4U) ||
				sg_wire_u32(split + 36U) == SG_HOST_COLLISION_BRUSH_NONE ||
				sg_wire_u32(split + 36U) >=
					sg_wire_u32(identity_record + 156U) ||
				sg_wire_u32(trace + 72U) != sg_wire_u32(split + 36U) ||
				!sg_wire_response_trace_plane_matches_split(trace, split) ||
				(sg_wire_u32(trace + 44U) &
					(SG_HOST_CONTENTS_SOLID | SG_HOST_CONTENTS_WINDOW)) == 0U)
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
					SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FACTS, i);
			residual = -(double)sg_wire_float_from_bits(
				sg_wire_u32(split + 12U));
			for (axis = 0U; axis < 3U; axis++)
				residual += (double)sg_wire_float_from_bits(
					sg_wire_u32(split + axis * 4U)) *
					(double)sg_wire_float_from_bits(
						sg_wire_u32(trace + 12U + axis * 4U));
			if (sg_wire_abs(residual) > 0.001)
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
					SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FACTS, i);
		} else
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
				SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FACTS, i);
	}
	for (i = 0; i < SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_KERNELS); ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_KERNELS, i);
		if (!sg_wire_ref(sg_wire_u32(p),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_PROFILES), 0))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_KERNELS, i);
		if (sg_wire_u32(p + 4) >=
			(uint32_t)SG_RUNE_WEAPON_RESPONSE_FAMILY_COUNT)
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
				SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_KERNELS, i);
		profile_record = SG_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_PROFILES,
			sg_wire_u32(p));
		sg_wire_get_weapon_profile(profile_record, &compact_profile);
		if ((sg_wire_u32(profile_record + 4) &
			SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(sg_wire_u32(p + 4))) == 0U)
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_KERNELS, i);
		if (!sg_wire_span(sg_wire_u32(p + 8), sg_wire_u32(p + 12),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_FUNCTION_REFS)))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_SPAN,
				SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_KERNELS, i);
		if (!SG_RuneCompactWeaponKernelReferenceCount(&compact_profile,
				(sg_rune_weapon_response_family_t)sg_wire_u32(p + 4),
				&definition_limit) ||
			sg_wire_u32(p + 12) != definition_limit ||
			!sg_wire_kernel_law_valid(compact_profile.source_profile,
				sg_wire_u32(p + 4), p))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
				SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_KERNELS, i);
		for (form = 0U; form < sg_wire_u32(p + 12); form++) {
			const uint8_t *reference = SG_RECORD(
				SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_FUNCTION_REFS,
				sg_wire_u32(p + 8) + form);

			if (!sg_wire_ref(sg_wire_u32(reference),
				SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_FUNCTIONS),
				0) || sg_wire_u32(reference + 4) >=
				(uint32_t)SG_RUNE_WEAPON_EFFECT_CHANNEL_COUNT)
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_KERNELS, i);
			function_record = SG_RECORD(
				SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_FUNCTIONS,
				sg_wire_u32(reference));
			{
				sg_rune_weapon_effect_channel_t expected_channel;
				uint32_t expected_instance;
				sg_rune_analytic_output_meaning_t expected_output;

				if (!SG_RuneCompactWeaponFunctionRefExpected(&compact_profile,
						(sg_rune_weapon_response_family_t)sg_wire_u32(p + 4),
					form, &expected_channel, &expected_instance,
					&expected_output) || expected_channel !=
					(sg_rune_weapon_effect_channel_t)sg_wire_u32(reference + 4) ||
					expected_instance != sg_wire_u32(reference + 8) ||
					expected_output !=
					(sg_rune_analytic_output_meaning_t)
						sg_wire_u32(function_record + 12))
					SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
						SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_KERNELS, i);
			}
		}
	}
	for (i = 0;
		i < SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_FUNCTION_REFS);
		++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_FUNCTION_REFS, i);
		if (!sg_wire_ref(sg_wire_u32(p),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_FUNCTIONS), 0) ||
			sg_wire_u32(p + 4) >=
				(uint32_t)SG_RUNE_WEAPON_EFFECT_CHANNEL_COUNT)
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_FUNCTION_REFS, i);
	}
	for (i = 0; i < SG_COUNT(
		SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBER_FUNCTION_REFS); ++i)
		if (!sg_wire_ref(sg_wire_u32(SG_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBER_FUNCTION_REFS, i)),
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_FUNCTIONS), 0))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBER_FUNCTION_REFS, i);
	for (i = 0U; i < SG_COUNT(
		SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_RELATION_SPANS); i++) {
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_RELATION_SPANS, i);
		if (sg_wire_u32(p) != weapon_relation_cursor || sg_wire_u32(p + 4) == 0U ||
			!sg_wire_span(sg_wire_u32(p), sg_wire_u32(p + 4), SG_COUNT(
				SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_RELATION_REFS)))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_RELATION_SPANS, i);
		weapon_relation_cursor += sg_wire_u32(p + 4);
	}
	if (weapon_relation_cursor != SG_COUNT(
		SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_RELATION_REFS))
		SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_SPAN,
			SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_RELATION_REFS,
			weapon_relation_cursor);
	if (SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_ATTACHMENTS) !=
		SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_RELATION_SPANS))
		SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
			SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_ATTACHMENTS, 0U);
	for (i = 0U; i < SG_COUNT(
		SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_ATTACHMENTS); i++) {
		uint32_t relation_offset;
		uint32_t relation_class;
		uint32_t relation_first;
		uint32_t relation_count;
		const uint8_t *previous_attachment;

		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_ATTACHMENTS, i);
		relation_class = sg_wire_u32(p + 8U);
		relation_first = sg_wire_u32(p + 16U);
		relation_count = sg_wire_u32(p + 20U);
		previous_attachment = i == 0U ? NULL : SG_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_ATTACHMENTS, i - 1U);
		if (!sg_wire_ref(sg_wire_u32(p),
				SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_CELLS), 0) ||
			!sg_wire_ref(sg_wire_u32(p + 4), SG_COUNT(
				SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACES), 0) ||
			relation_class >=
				SG_RUNE_COMPACT_WEAPON_RELATION_CLASS_COUNT ||
			sg_wire_u32(p + 12) != 0U ||
			sg_wire_u32(p + 24) != i || relation_count == 0U ||
			sg_wire_u32(p + 28) != 0U ||
			!sg_wire_span(relation_first, relation_count, SG_COUNT(
				SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_RELATION_REFS)) ||
			(previous_attachment != NULL &&
				sg_wire_weapon_attachment_compare(previous_attachment, p) >= 0))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_ATTACHMENTS, i);
		relation_span_record = image + descs[
			SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_RELATION_SPANS].offset +
			(uint64_t)i *
			sg_wire_specs[SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_RELATION_SPANS].wire_size;
		if (relation_first != sg_wire_u32(relation_span_record) ||
			relation_count != sg_wire_u32(relation_span_record + 4))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_ATTACHMENTS, i);
		for (relation_offset = 0U; relation_offset < relation_count;
			relation_offset++) {
			const uint8_t *reference = SG_RECORD(
				SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_RELATION_REFS,
				relation_first + relation_offset);
			const uint8_t *fact;
			const uint8_t *fragment;
			const uint8_t *patch;

			if (sg_wire_u32(reference) !=
					SG_RUNE_COMPACT_RESPONSE_REF_CERTIFIED_FACT ||
				!sg_wire_ref(sg_wire_u32(reference + 4U), SG_COUNT(
					SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FACTS), 0) ||
				(relation_offset != 0U && sg_wire_u32(reference + 4U) <=
					sg_wire_u32(SG_RECORD(
						SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_RELATION_REFS,
						relation_first + relation_offset - 1U) + 4U)))
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_ATTACHMENTS, i);
			fact = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FACTS,
				sg_wire_u32(reference + 4U));
			fragment = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FRAGMENTS,
				sg_wire_u32(fact));
			patch = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_PATCHES,
				sg_wire_u32(fact + 4U));
			if (sg_wire_u32(fragment) != sg_wire_u32(p) ||
				sg_wire_u32(patch + 20U) != sg_wire_u32(p + 4U) ||
				!sg_wire_weapon_fact_supports_relation_class(relation_class,
					sg_wire_u32(fact + 8U)))
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_ATTACHMENTS, i);
		}
	}
	{
		uint64_t expected_relation_ref_count = 0U;

		/* The raw image must carry the complete sorted fact-to-class projection,
		 * not merely well-formed attachments for whichever facts it lists. */
		for (i = 0U; i < SG_COUNT(
			SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FACTS); i++) {
			const uint8_t *fact = SG_RECORD(
				SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FACTS, i);
			const uint8_t *fragment = SG_RECORD(
				SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FRAGMENTS,
				sg_wire_u32(fact));
			const uint8_t *patch = SG_RECORD(
				SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_PATCHES,
				sg_wire_u32(fact + 4U));
			uint32_t relation_class;

			for (relation_class =
				(uint32_t)SG_RUNE_COMPACT_WEAPON_RELATION_DIRECT;
				relation_class <
				(uint32_t)SG_RUNE_COMPACT_WEAPON_RELATION_CLASS_COUNT;
				relation_class++) {
				const uint8_t *attachment;

				if (!sg_wire_weapon_fact_supports_relation_class(
					relation_class, sg_wire_u32(fact + 8U)))
					continue;
				expected_relation_ref_count++;
				attachment = sg_wire_find_weapon_attachment(image, descs,
					sg_wire_u32(fragment), sg_wire_u32(patch + 20U),
					relation_class);
				if (attachment == NULL ||
					!sg_wire_weapon_attachment_references_fact(image, descs,
						attachment, i))
					SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
						SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_ATTACHMENTS, i);
			}
		}
		if (expected_relation_ref_count != SG_COUNT(
			SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_RELATION_REFS))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_ATTACHMENTS, 0U);
	}
	for (i = 0U; i < SG_COUNT(
		SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_RELATION_REFS); i++) {
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_RELATION_REFS, i);
		if (sg_wire_u32(p) != SG_RUNE_COMPACT_RESPONSE_REF_CERTIFIED_FACT ||
			!sg_wire_ref(sg_wire_u32(p + 4), SG_COUNT(
				SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FACTS), 0))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_RELATION_REFS, i);
	}
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
		uint32_t activation_mask;

		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_MECHANISMS, i);
		activation_mask = sg_wire_u32(p + 84);
		if (sg_wire_u32(p) >= sg_wire_u32(identity_record + 160) ||
			!sg_wire_ref(sg_wire_u32(p + 4),
				SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_CELLS), 0) ||
			!sg_wire_ref(sg_wire_u32(p + 8),
				SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_CELLS), 0) ||
			!sg_wire_ref(sg_wire_u32(p + 12),
				SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_LANDMARKS), 1))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_MECHANISMS, i);
		if (!sg_wire_span(sg_wire_u32(p + 40), sg_wire_u32(p + 44),
				SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_CONTROLLERS)) ||
			!sg_wire_span(sg_wire_u32(p + 48), sg_wire_u32(p + 52),
				SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_EDGES)) ||
			!sg_wire_span(sg_wire_u32(p + 56), sg_wire_u32(p + 60),
				SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS)))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_SPAN,
				SG_RUNE_COMPACT_WIRE_SECTION_MECHANISMS, i);
		if (sg_wire_u32(p + 40) != mechanism_controller_cursor ||
			sg_wire_u32(p + 48) != mechanism_edge_cursor ||
			sg_wire_u32(p + 56) != transition_cursor)
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
				SG_RUNE_COMPACT_WIRE_SECTION_MECHANISMS, i);
		mechanism_controller_cursor += sg_wire_u32(p + 44);
		mechanism_edge_cursor += sg_wire_u32(p + 52);
		transition_cursor += sg_wire_u32(p + 60);
		if (activation_mask == 0U ||
			(activation_mask & ~
				(uint32_t)SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_KNOWN) != 0U ||
			sg_wire_u32(p + 128) >=
				(uint32_t)SG_RUNE_COMPACT_MECHANISM_KIND_COUNT ||
			sg_wire_u32(p + 132) >=
				(uint32_t)SG_RUNE_COMPACT_MECHANISM_STATE_COUNT ||
			sg_wire_u32(p + 136) >=
				(uint32_t)SG_RUNE_COMPACT_MECHANISM_STATE_COUNT ||
			sg_wire_u32(p + 140) >=
				(uint32_t)SG_RUNE_COMPACT_MECHANISM_STATE_COUNT ||
			sg_wire_u32(p + 144) >=
				(uint32_t)SG_RUNE_COMPACT_MECHANISM_RECOVERY_COUNT ||
			(p[148] & (uint8_t)~SG_RUNE_COMPACT_MECHANISM_FLAGS_KNOWN) != 0U)
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
				SG_RUNE_COMPACT_WIRE_SECTION_MECHANISMS, i);
		if ((p[148] & SG_RUNE_COMPACT_MECHANISM_FINITE_ANGULAR_DOOR) != 0U &&
			(sg_wire_u32(p + 128) !=
				(uint32_t)SG_RUNE_COMPACT_MECHANISM_ROTATOR ||
			 (p[148] & SG_RUNE_COMPACT_MECHANISM_MOVER_RELATIVE) == 0U))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
				SG_RUNE_COMPACT_WIRE_SECTION_MECHANISMS, i);
		if (!sg_wire_zero(p + 149, 3))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_NONZERO_RESERVED,
				SG_RUNE_COMPACT_WIRE_SECTION_MECHANISMS, i);
		if (((activation_mask &
				(uint32_t)SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_INVENTORY) != 0U &&
				sg_wire_u32(p + 96) == SG_RUNE_COMPACT_INDEX_NONE) ||
			((activation_mask &
				(uint32_t)SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_INVENTORY) == 0U &&
				sg_wire_u32(p + 96) != SG_RUNE_COMPACT_INDEX_NONE) ||
			((activation_mask &
				(uint32_t)SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_DAMAGE) != 0U &&
				(int32_t)sg_wire_u32(p + 92) <= 0) ||
			((activation_mask &
				(uint32_t)SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_DAMAGE) == 0U &&
				(int32_t)sg_wire_u32(p + 92) != 0) ||
			((activation_mask & ~(uint32_t)
				SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_AUTO) != 0U &&
				sg_wire_u32(p + 12) == SG_RUNE_COMPACT_INDEX_NONE) ||
			((activation_mask & ~(uint32_t)
				SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_AUTO) == 0U &&
				sg_wire_u32(p + 12) != SG_RUNE_COMPACT_INDEX_NONE))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
				SG_RUNE_COMPACT_WIRE_SECTION_MECHANISMS, i);
	}
	if (mechanism_controller_cursor != SG_COUNT(
		SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_CONTROLLERS) ||
		mechanism_edge_cursor != SG_COUNT(
			SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_EDGES) ||
		transition_cursor != SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS))
		SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
			SG_RUNE_COMPACT_WIRE_SECTION_MECHANISMS, 0U);
	for (i = 0; i < SG_COUNT(
		SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_CONTROLLERS); ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_CONTROLLERS, i);
		if (sg_wire_u32(p) >= sg_wire_u32(identity_record + 160) ||
			!sg_wire_ref(sg_wire_u32(p + 4), SG_COUNT(
				SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_EDGES), 1) ||
			p[8] >= SG_RUNE_COMPACT_MECHANISM_CONTROLLER_SPATIALITY_COUNT ||
			!sg_wire_zero(p + 9, 3))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_CONTROLLERS, i);
		if (p[8] == SG_RUNE_COMPACT_MECHANISM_CONTROLLER_NONSPATIAL ?
			(sg_wire_u32(p + 12) != SG_RUNE_COMPACT_INDEX_NONE ||
			 !sg_wire_zero(p + 16, 36)) :
			(!sg_wire_ref(sg_wire_u32(p + 12), SG_COUNT(
				SG_RUNE_COMPACT_WIRE_SECTION_CELLS), 0) ||
			 !sg_wire_bounds_valid(p + 28) ||
			 !sg_wire_point_in_bounds(p + 16, p + 28) ||
			 !sg_wire_point_in_bounds(p + 16, SG_RECORD(
				SG_RUNE_COMPACT_WIRE_SECTION_CELLS, sg_wire_u32(p + 12)) +
				20)))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
				SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_CONTROLLERS, i);
	}
	for (i = 0; i < SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_EDGES); ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_EDGES, i);
		if (sg_wire_u32(p) >= sg_wire_u32(identity_record + 160) ||
			sg_wire_u32(p + 4) >= sg_wire_u32(identity_record + 160) ||
			sg_wire_u32(p + 12) >=
			(uint32_t)SG_RUNE_COMPACT_MECHANISM_EDGE_KIND_COUNT)
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
				SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_EDGES, i);
	}
	for (i = 0; i < SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS); ++i)
	{
		const uint8_t *mechanism;
		uint64_t elapsed_ms;
		uint32_t kind;
		uint32_t axis;

		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS, i);
		if (i != 0U && sg_wire_transition_compare_canonical(
				SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS, i - 1U),
				p) >= 0)
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
				SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS, i);
		kind = sg_wire_u32(p + 4);
		if (!sg_wire_ref(sg_wire_u32(p),
				SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_MECHANISMS), 0) ||
			!sg_wire_ref(sg_wire_u32(p + 8),
				SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_CELLS), 0) ||
			!sg_wire_ref(sg_wire_u32(p + 12),
				SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_CELLS), 0))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS, i);
		if (kind >= (uint32_t)SG_RUNE_COMPACT_STATIC_TRANSITION_KIND_COUNT ||
			sg_wire_u32(p + 16) >=
				(uint32_t)SG_RUNE_COMPACT_MECHANISM_STATE_COUNT ||
			sg_wire_u32(p + 20) >=
				(uint32_t)SG_RUNE_COMPACT_MECHANISM_STATE_COUNT)
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
				SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS, i);
		mechanism = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_MECHANISMS,
			sg_wire_u32(p));
		if (i < sg_wire_u32(mechanism + 56) ||
			i - sg_wire_u32(mechanism + 56) >=
				sg_wire_u32(mechanism + 60))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS, i);
		elapsed_ms = sg_wire_u64(p + 24);
	switch ((sg_rune_compact_static_transition_kind_t)kind)
		{
		case SG_RUNE_COMPACT_STATIC_TRANSITION_PORTAL_STATE:
			if (!sg_wire_ref(sg_wire_u32(p + 32),
					SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_PORTALS), 0) ||
				sg_wire_u32(p + 36) >= sg_wire_u32(identity_record + 136) ||
				elapsed_ms == 0U ||
				sg_wire_u32(p + 16) != sg_wire_u32(mechanism + 132) ||
				sg_wire_u32(p + 20) != sg_wire_u32(mechanism + 136) ||
				sg_wire_u32(p + 40) != sg_wire_u32(mechanism + 64) ||
				sg_wire_u32(p + 44) != sg_wire_u32(mechanism + 68) ||
				sg_wire_u32(p + 48) != sg_wire_u32(mechanism + 76) ||
				sg_wire_u32(p + 52) != sg_wire_u32(mechanism + 72) ||
				sg_wire_u32(p + 56) != sg_wire_u32(mechanism + 80) ||
				(sg_wire_u32(mechanism + 128) !=
					(uint32_t)SG_RUNE_COMPACT_MECHANISM_DOOR &&
				 sg_wire_u32(mechanism + 128) !=
					(uint32_t)SG_RUNE_COMPACT_MECHANISM_BUTTON &&
					 !(sg_wire_u32(mechanism + 128) ==
						(uint32_t)SG_RUNE_COMPACT_MECHANISM_ROTATOR &&
					   (mechanism[148] &
						SG_RUNE_COMPACT_MECHANISM_FINITE_ANGULAR_DOOR) != 0U)))
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS, i);
			if (p[60] > 1U || p[61] > 1U || p[60] == p[61])
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
					SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS, i);
			if (!sg_wire_zero(p + 62, 2U) || !sg_wire_zero(p + 64, 184U))
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_NONZERO_RESERVED,
					SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS, i);
			break;
		case SG_RUNE_COMPACT_STATIC_TRANSITION_TELEPORT:
			if (!sg_wire_ref(sg_wire_u32(p + 32),
					sg_wire_u32(identity_record + 160), 0) ||
				sg_wire_u32(p + 36) == UINT32_MAX || elapsed_ms != 0U ||
				sg_wire_u32(mechanism + 128) !=
					(uint32_t)SG_RUNE_COMPACT_MECHANISM_TELEPORT ||
				sg_wire_u32(p + 16) !=
					(uint32_t)SG_RUNE_COMPACT_MECHANISM_STATE_ACTIVE ||
				sg_wire_u32(p + 20) !=
					(uint32_t)SG_RUNE_COMPACT_MECHANISM_STATE_ACTIVE ||
				sg_wire_u32(p + 32) != sg_wire_u32(mechanism + 100) ||
				sg_wire_u32(p + 36) != sg_wire_u32(mechanism + 104) ||
				sg_wire_u32(p + 76) != 0U || sg_wire_u32(p + 80) != 0U ||
				sg_wire_u32(p + 84) != 0U)
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS, i);
			if (!sg_wire_zero(p + 88, 160))
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_NONZERO_RESERVED,
					SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS, i);
			break;
		case SG_RUNE_COMPACT_STATIC_TRANSITION_PUSH:
			if (sg_wire_u32(mechanism + 128) !=
					(uint32_t)SG_RUNE_COMPACT_MECHANISM_PUSH ||
				sg_wire_u32(p + 16) !=
					(uint32_t)SG_RUNE_COMPACT_MECHANISM_STATE_ACTIVE ||
				sg_wire_u32(p + 20) !=
					(uint32_t)SG_RUNE_COMPACT_MECHANISM_STATE_ACTIVE ||
				elapsed_ms == 0U || elapsed_ms != (uint64_t)sg_wire_u32(p + 84) ||
				sg_wire_u32(p + 80) != sg_wire_u32(mechanism + 120) ||
				sg_wire_u32(p + 84) != sg_wire_u32(mechanism + 124) ||
				!sg_wire_binary32_nonnegative(sg_wire_u32(p + 80)))
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
					SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS, i);
			for (axis = 0U; axis < 3U; axis++)
				if (!sg_wire_binary32_finite_allow_signed_zero(
					sg_wire_u32(p + 68U + axis * 4U)) ||
					sg_wire_u32(p + 68U + axis * 4U) !=
					sg_wire_u32(mechanism + 108U + axis * 4U))
					SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
						SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS, i);
			if (!sg_wire_zero(p + 88, 160))
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_NONZERO_RESERVED,
					SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS, i);
			break;
	case SG_RUNE_COMPACT_STATIC_TRANSITION_TRANSPORT:
		{
			uint32_t derived_world[3];
			const uint32_t mechanism_kind = sg_wire_u32(mechanism + 128);
			const int finite_rotator = mechanism_kind ==
				(uint32_t)SG_RUNE_COMPACT_MECHANISM_ROTATOR &&
				(mechanism[148] &
					SG_RUNE_COMPACT_MECHANISM_FINITE_ANGULAR_DOOR) != 0U;

			if (sg_wire_u32(p + 32) >= sg_wire_u32(identity_record + 136) ||
				sg_wire_u32(p + 32) == SG_RUNE_COMPACT_INDEX_NONE ||
				!sg_wire_ref(sg_wire_u32(p + 36),
					SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACES), 0) ||
				sg_wire_u32(SG_RECORD(
					SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACES,
					sg_wire_u32(p + 36)) + 16) !=
					(uint32_t)SG_RUNE_COMPACT_SOURCE_SURFACE_MODEL_LOCAL ||
				sg_wire_u32(SG_RECORD(
					SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACES,
					sg_wire_u32(p + 36)) + 20) !=
					SG_RUNE_COMPACT_INDEX_NONE ||
				sg_wire_u32(SG_RECORD(
					SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACES,
					sg_wire_u32(p + 36)) + 24) !=
					SG_RUNE_COMPACT_INDEX_NONE ||
				sg_wire_u32(SG_RECORD(
					SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACES,
					sg_wire_u32(p + 36)) + 28) != 0U ||
				sg_wire_u32(SG_RECORD(
					SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACES,
					sg_wire_u32(p + 36))) != sg_wire_u32(p + 32) ||
				!sg_wire_ref(sg_wire_u32(p + 232),
					sg_wire_u32(identity_record + 160), 1) ||
				!sg_wire_ref(sg_wire_u32(p + 236),
					sg_wire_u32(identity_record + 160), 1))
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS, i);
			if (elapsed_ms == 0U || p[244] != 1U || p[245] != 1U ||
				p[246] != 1U)
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
					SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS, i);
			for (axis = 0U; axis < 36U; axis++)
				if (!sg_wire_binary32_canonical_finite(
					sg_wire_u32(p + 88U + axis * 4U)))
					SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
						SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS, i);
			if (!sg_wire_transport_derive_world(p + 40U, p + 136U,
				p + 148U, derived_world) ||
				!sg_wire_u32_array_equals(p + 88U, derived_world, 3U) ||
				!sg_wire_transport_derive_world(p + 52U, p + 184U,
					p + 196U, derived_world) ||
				!sg_wire_u32_array_equals(p + 100U, derived_world, 3U) ||
				!sg_wire_transport_derive_world(p + 64U, p + 136U,
					p + 148U, derived_world) ||
				!sg_wire_u32_array_equals(p + 112U, derived_world, 3U) ||
				!sg_wire_transport_derive_world(p + 76U, p + 184U,
					p + 196U, derived_world) ||
				!sg_wire_u32_array_equals(p + 124U, derived_world, 3U))
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
					SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS, i);
			if (mechanism_kind ==
				(uint32_t)SG_RUNE_COMPACT_MECHANISM_LIFT ||
				mechanism_kind ==
					(uint32_t)SG_RUNE_COMPACT_MECHANISM_DOOR ||
				mechanism_kind ==
					(uint32_t)SG_RUNE_COMPACT_MECHANISM_BUTTON ||
				finite_rotator) {
				if (sg_wire_u32(p + 232) != SG_RUNE_COMPACT_INDEX_NONE ||
					sg_wire_u32(p + 236) != SG_RUNE_COMPACT_INDEX_NONE ||
					sg_wire_u32(p + 240) != UINT32_MAX ||
					sg_wire_u32(p + 16) != sg_wire_u32(mechanism + 132) ||
					sg_wire_u32(p + 20) != sg_wire_u32(mechanism + 136))
					SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
						SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS, i);
			} else if (mechanism_kind ==
				(uint32_t)SG_RUNE_COMPACT_MECHANISM_TRAIN) {
				if (sg_wire_u32(p + 232) == SG_RUNE_COMPACT_INDEX_NONE ||
					sg_wire_u32(p + 236) == SG_RUNE_COMPACT_INDEX_NONE ||
					sg_wire_u32(p + 232) == sg_wire_u32(p + 236) ||
					sg_wire_u32(p + 240) == UINT32_MAX ||
					((sg_wire_u32(mechanism + 84) &
						SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_AUTO) != 0U &&
					 (sg_wire_u32(p + 16) !=
						(uint32_t)SG_RUNE_COMPACT_MECHANISM_STATE_ACTIVE ||
					  sg_wire_u32(p + 20) !=
						(uint32_t)SG_RUNE_COMPACT_MECHANISM_STATE_ACTIVE)) ||
					((sg_wire_u32(mechanism + 84) &
						SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_AUTO) == 0U &&
					 (sg_wire_u32(p + 16) != sg_wire_u32(mechanism + 132) ||
					  sg_wire_u32(p + 20) != sg_wire_u32(mechanism + 136))))
					SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
						SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS, i);
			} else if (mechanism_kind ==
				(uint32_t)SG_RUNE_COMPACT_MECHANISM_ROTATOR) {
				if (sg_wire_u32(p + 232) != SG_RUNE_COMPACT_INDEX_NONE ||
					sg_wire_u32(p + 236) != SG_RUNE_COMPACT_INDEX_NONE ||
					sg_wire_u32(p + 240) != UINT32_MAX ||
					sg_wire_u32(p + 16) !=
						(uint32_t)SG_RUNE_COMPACT_MECHANISM_STATE_ACTIVE ||
					sg_wire_u32(p + 20) !=
						(uint32_t)SG_RUNE_COMPACT_MECHANISM_STATE_ACTIVE)
					SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
						SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS, i);
			} else {
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS, i);
			}
			if (p[247] >= SG_RUNE_STANCE_COUNT)
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
					SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS, i);
			break;
		}
		case SG_RUNE_COMPACT_STATIC_TRANSITION_KIND_COUNT:
			break;
		}
	}
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
		const uint8_t *facet;
		const uint32_t source_surface = sg_wire_u32(
			SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_FACET_ANNOTATIONS, i) + 8U);
		const uint32_t source_frame = sg_wire_u32(
			SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_FACET_ANNOTATIONS, i) + 12U);

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
		if (source_frame >=
			(uint32_t)SG_RUNE_COMPACT_SOURCE_SURFACE_FRAME_COUNT)
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_FACET_ANNOTATIONS, i);
		if (source_surface == SG_RUNE_COMPACT_INDEX_NONE) {
			if (source_frame !=
				(uint32_t)SG_RUNE_COMPACT_SOURCE_SURFACE_WORLD)
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_WIRE_SECTION_FACET_ANNOTATIONS, i);
			continue;
		}
		if (!sg_wire_ref(source_surface,
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACES), 0))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_FACET_ANNOTATIONS, i);
		facet = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_FACETS,
			sg_wire_u32(p));
		{
			const uint8_t *surface = SG_RECORD(
				SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACES, source_surface);

			if (source_frame != sg_wire_u32(surface + 16U) ||
				sg_wire_u32(surface + 24U) !=
					SG_RUNE_COMPACT_INDEX_NONE ||
				sg_wire_u32(surface) != 0U ||
				sg_wire_u32(facet + 56U) !=
					(uint32_t)SG_RUNE_COMPACT_FACET_POLYGON ||
				sg_wire_u32(facet) !=
					(uint32_t)SG_RUNE_COMPACT_SOURCE_EXPANDED_BRUSH_SIDE ||
				sg_wire_u32(facet + 4U) != sg_wire_u32(surface) ||
				sg_wire_u32(facet + 8U) != sg_wire_u32(surface + 4U) ||
				sg_wire_u32(facet + 12U) != sg_wire_u32(surface + 8U) ||
				sg_wire_u32(facet + 16U) != sg_wire_u32(surface + 12U))
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_WIRE_SECTION_FACET_ANNOTATIONS, i);
		}
	}
	for (i = 0; i < SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_PORTAL_MECHANISMS); ++i)
	{
		const uint8_t *mechanism;

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
		if (i != 0U) {
			const uint8_t *previous = SG_RECORD(
				SG_RUNE_COMPACT_WIRE_SECTION_PORTAL_MECHANISMS, i - 1U);
			const uint32_t previous_mechanism = sg_wire_u32(previous + 4U);
			const uint32_t mechanism_index = sg_wire_u32(p + 4U);

			if (previous_mechanism > mechanism_index ||
				(previous_mechanism == mechanism_index &&
				 sg_wire_u32(previous) >= sg_wire_u32(p)))
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
					SG_RUNE_COMPACT_WIRE_SECTION_PORTAL_MECHANISMS, i);
		}
		mechanism = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_MECHANISMS,
			sg_wire_u32(p + 4));
		if (sg_wire_u32(p + 8) !=
			(uint32_t)SG_RUNE_COMPACT_PORTAL_MECHANISM_BLOCKS)
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_PORTAL_MECHANISMS, i);
		if (sg_wire_u32(p + 8) ==
				(uint32_t)SG_RUNE_COMPACT_PORTAL_MECHANISM_BLOCKS &&
			sg_wire_u32(mechanism + 128) ==
				(uint32_t)SG_RUNE_COMPACT_MECHANISM_ROTATOR &&
			(mechanism[148] &
				SG_RUNE_COMPACT_MECHANISM_FINITE_ANGULAR_DOOR) == 0U)
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_PORTAL_MECHANISMS, i);
		if (!sg_wire_zero(p + 12, 4))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_NONZERO_RESERVED,
				SG_RUNE_COMPACT_WIRE_SECTION_PORTAL_MECHANISMS, i);
	}
	for (i = 0U; i < SG_COUNT(
		SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITIES); i++) {
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITIES, i);
		if ((i != 0U && sg_wire_u32(SG_RECORD(
				SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITIES,
				i - 1U)) >= sg_wire_u32(p)) ||
			sg_wire_u32(p) >= sg_wire_u32(identity_record + 160) ||
			sg_wire_u32(p + 4) >=
				(uint32_t)SG_RUNE_COMPACT_MECHANISM_AUTHORITY_KIND_COUNT ||
			sg_wire_u32(p + 8) == 0U ||
			(sg_wire_u32(p + 8) &
				~(uint32_t)SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_KNOWN) != 0U ||
			!sg_wire_ref(sg_wire_u32(p + 12), SG_COUNT(
				SG_RUNE_COMPACT_WIRE_SECTION_CELLS), 0) ||
			!sg_wire_bounds_valid(p + 28) ||
			!sg_wire_point_in_bounds(p + 16, p + 28) ||
			sg_wire_u32(p + 52) != authority_controller_cursor ||
			sg_wire_u32(p + 60) != authority_topology_cursor ||
			sg_wire_u32(p + 68) != authority_transition_cursor ||
			!sg_wire_span(sg_wire_u32(p + 52), sg_wire_u32(p + 56), SG_COUNT(
				SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_CONTROLLERS)) ||
			!sg_wire_span(sg_wire_u32(p + 60), sg_wire_u32(p + 64), SG_COUNT(
				SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TOPOLOGY_EDGES)) ||
			!sg_wire_span(sg_wire_u32(p + 68), sg_wire_u32(p + 72), SG_COUNT(
				SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TRANSITIONS)) ||
			sg_wire_u32(p + 104) >=
				SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_COUNT ||
			sg_wire_u32(p + 108) >=
				SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_COUNT ||
			sg_wire_u32(p + 112) >=
				SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_COUNT ||
			(sg_wire_u32(p + 120) &
				~(uint32_t)SG_RUNE_COMPACT_MECHANISM_AUTHORITY_FLAGS_KNOWN) != 0U)
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITIES, i);
		authority_controller_cursor += sg_wire_u32(p + 56);
		authority_topology_cursor += sg_wire_u32(p + 64);
		authority_transition_cursor += sg_wire_u32(p + 72);
	}
	if (authority_controller_cursor != SG_COUNT(
			SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_CONTROLLERS) ||
		authority_topology_cursor != SG_COUNT(
			SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TOPOLOGY_EDGES) ||
		authority_transition_cursor != SG_COUNT(
			SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TRANSITIONS) ||
		SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TRANSITIONS) !=
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS) ||
		SG_COUNT(
			SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TRANSITION_STATIC_INDICES) !=
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS) ||
		SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_STATIC_TRANSITION_AUTHORITY_INDICES) !=
			SG_COUNT(SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS))
		SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_SPAN,
			SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITIES, i);
	for (i = 0U; i < SG_COUNT(
		SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_CONTROLLERS); i++) {
		const uint32_t mechanism = sg_wire_u32(SG_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_CONTROLLERS, i));
		const uint8_t *authority;

		p = SG_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_CONTROLLERS, i);
		if (!sg_wire_ref(mechanism, SG_COUNT(
				SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITIES), 0) ||
			sg_wire_u32(p + 4) >= sg_wire_u32(identity_record + 160) ||
			sg_wire_u32(p + 12) == 0U ||
			(sg_wire_u32(p + 12) &
				~(uint32_t)SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_KNOWN) != 0U ||
			(sg_wire_u32(p + 28) &
				~(uint32_t)SG_RUNE_COMPACT_MECHANISM_CONTROLLER_FLAGS_KNOWN) != 0U ||
			p[32] >= SG_RUNE_COMPACT_MECHANISM_CONTROLLER_SPATIALITY_COUNT ||
			!sg_wire_zero(p + 33, 3) ||
			(p[32] == SG_RUNE_COMPACT_MECHANISM_CONTROLLER_NONSPATIAL ?
			 (sg_wire_u32(p + 36) != SG_RUNE_COMPACT_INDEX_NONE ||
			  !sg_wire_zero(p + 40, 36)) :
				 (!sg_wire_ref(sg_wire_u32(p + 36), SG_COUNT(
					SG_RUNE_COMPACT_WIRE_SECTION_CELLS), 0) ||
				  !sg_wire_bounds_valid(p + 52) ||
				  !sg_wire_point_in_bounds(p + 40, p + 52) ||
				  !sg_wire_point_in_bounds(p + 40, SG_RECORD(
					SG_RUNE_COMPACT_WIRE_SECTION_CELLS,
					sg_wire_u32(p + 36)) + 20))))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_CONTROLLERS, i);
		authority = SG_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITIES, mechanism);
		if (i < sg_wire_u32(authority + 52) ||
			i - sg_wire_u32(authority + 52) >= sg_wire_u32(authority + 56) ||
			sg_wire_u32(p + 8) < sg_wire_u32(authority + 60) ||
			sg_wire_u32(p + 8) - sg_wire_u32(authority + 60) >=
				sg_wire_u32(authority + 64))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_CONTROLLERS, i);
	}
	for (i = 0U; i < SG_COUNT(
		SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TOPOLOGY_EDGES); i++) {
		p = SG_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TOPOLOGY_EDGES, i);
		if (sg_wire_u32(p) >= sg_wire_u32(identity_record + 160) ||
			sg_wire_u32(p + 4) >= sg_wire_u32(identity_record + 160) ||
			sg_wire_u32(p + 8) < (uint32_t)SG_MECH_EDGE_TARGET ||
			sg_wire_u32(p + 8) > (uint32_t)SG_MECH_EDGE_ROUTE_TARGET)
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TOPOLOGY_EDGES,
				i);
	}
	for (i = 0U; i < SG_COUNT(
		SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TRANSITIONS); i++) {
		const uint32_t mechanism = sg_wire_u32(SG_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TRANSITIONS, i));
		const uint8_t *authority;
		const uint32_t kind = sg_wire_u32(SG_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TRANSITIONS, i) + 4);

		p = SG_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TRANSITIONS, i);
		if (!sg_wire_ref(mechanism, SG_COUNT(
				SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITIES), 0) ||
			kind >= SG_RUNE_COMPACT_MECHANISM_TRANSITION_KIND_COUNT ||
			!sg_wire_ref(sg_wire_u32(p + 8), SG_COUNT(
				SG_RUNE_COMPACT_WIRE_SECTION_CELLS), 0) ||
			!sg_wire_ref(sg_wire_u32(p + 12), SG_COUNT(
				SG_RUNE_COMPACT_WIRE_SECTION_CELLS), 0) ||
			sg_wire_u32(p + 16) >=
				SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_COUNT ||
			sg_wire_u32(p + 20) >=
				SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_COUNT)
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TRANSITIONS, i);
		authority = SG_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITIES, mechanism);
		if (i < sg_wire_u32(authority + 68) ||
			i - sg_wire_u32(authority + 68) >= sg_wire_u32(authority + 72) ||
			!sg_wire_authority_transition_states_valid(authority, p))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TRANSITIONS, i);
		switch ((sg_rune_compact_mechanism_transition_kind_t)kind) {
		case SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE:
			if (!sg_wire_ref(sg_wire_u32(p + 32), SG_COUNT(
					SG_RUNE_COMPACT_WIRE_SECTION_PORTALS), 0) ||
				sg_wire_u64(p + 24) == 0U ||
				p[60] > 1U || p[61] > 1U || p[60] == p[61] ||
				!sg_wire_zero(p + 62, 186U))
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
					SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TRANSITIONS,
					i);
			break;
		case SG_RUNE_COMPACT_MECHANISM_TRANSITION_TELEPORT:
			if (sg_wire_u64(p + 24) != 0U ||
				sg_wire_u32(p + 32) >= sg_wire_u32(identity_record + 160) ||
				!sg_wire_zero(p + 88, 160U))
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
					SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TRANSITIONS,
					i);
			break;
		case SG_RUNE_COMPACT_MECHANISM_TRANSITION_PUSH:
			if (sg_wire_u64(p + 24) == 0U ||
				!sg_wire_zero(p + 88, 160U))
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_NONZERO_RESERVED,
					SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TRANSITIONS,
					i);
			break;
		case SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT:
			if (sg_wire_u64(p + 24) == 0U ||
				sg_wire_u32(p + 32) >= sg_wire_u32(identity_record + 136) ||
				!sg_wire_ref(sg_wire_u32(p + 36), SG_COUNT(
					SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACES), 0) ||
				p[244] > 1U || p[245] > 1U || p[246] > 1U ||
				p[247] >= SG_RUNE_STANCE_COUNT)
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TRANSITIONS,
					i);
			break;
		case SG_RUNE_COMPACT_MECHANISM_TRANSITION_KIND_COUNT:
			break;
		}
		{
			const uint32_t static_index = sg_wire_u32(SG_RECORD(
				SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TRANSITION_STATIC_INDICES,
				i));
			const uint8_t *static_transition;
			const uint8_t *static_owner;
			const uint32_t static_mechanism_count = SG_COUNT(
				SG_RUNE_COMPACT_WIRE_SECTION_MECHANISMS);

			if (static_index >= SG_COUNT(
					SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS) ||
				sg_wire_u32(SG_RECORD(
					SG_RUNE_COMPACT_WIRE_SECTION_STATIC_TRANSITION_AUTHORITY_INDICES,
					static_index)) != i)
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TRANSITION_STATIC_INDICES,
					i);
			static_transition = SG_RECORD(
				SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS, static_index);
			if (sg_wire_u32(static_transition) >= static_mechanism_count)
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS, static_index);
			static_owner = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_MECHANISMS,
				sg_wire_u32(static_transition));
			if (sg_wire_u32(static_owner) != sg_wire_u32(authority) ||
				sg_wire_u32(static_owner + 128) != sg_wire_u32(authority + 4) ||
				memcmp(static_transition + 4, p + 4, 28U) != 0)
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TRANSITIONS,
					i);
			if (kind ==
				(uint32_t)SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE) {
				if (memcmp(static_transition + 32, p + 32, 8U) != 0 ||
					memcmp(static_transition + 60, p + 60, 2U) != 0 ||
					sg_wire_u32(p + 40) != sg_wire_u32(authority + 76) ||
					sg_wire_u32(p + 44) != sg_wire_u32(authority + 80) ||
					sg_wire_u32(p + 48) != sg_wire_u32(authority + 84) ||
					sg_wire_u32(p + 52) != sg_wire_u32(authority + 88) ||
					sg_wire_u32(p + 56) != sg_wire_u32(authority + 116) ||
					sg_wire_u32(static_transition + 40) !=
						sg_wire_u32(authority + 76) ||
					sg_wire_u32(static_transition + 44) !=
						sg_wire_u32(authority + 80) ||
					sg_wire_u32(static_transition + 48) !=
						sg_wire_u32(authority + 84) ||
					sg_wire_u32(static_transition + 52) !=
						sg_wire_u32(authority + 88) ||
					sg_wire_u32(static_transition + 56) !=
						sg_wire_u32(authority + 116) ||
					sg_wire_u64(p + 24) != (uint64_t)sg_wire_u32(p + 52))
					SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
						SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TRANSITIONS,
						i);
			} else if (memcmp(static_transition + 32, p + 32, 216U) != 0) {
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TRANSITIONS,
					i);
			}
		}
	}
	for (i = 0U; i < SG_COUNT(
		SG_RUNE_COMPACT_WIRE_SECTION_STATIC_TRANSITION_AUTHORITY_INDICES); i++) {
		const uint32_t authority_index = sg_wire_u32(SG_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_STATIC_TRANSITION_AUTHORITY_INDICES, i));

		if (authority_index >= SG_COUNT(
				SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TRANSITIONS) ||
			sg_wire_u32(SG_RECORD(
				SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TRANSITION_STATIC_INDICES,
				authority_index)) != i)
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_STATIC_TRANSITION_AUTHORITY_INDICES, i);
	}
	{
		uint32_t binding_cursor = 0U;
		uint32_t mechanism_index;

		/* Both projections are mechanism-major. Portal-state transitions sort
		 * by portal before their remaining payload, so this is a linear exact
		 * join over (mechanism, portal), with no portal-sized scratch table. */
		for (mechanism_index = 0U;
			mechanism_index < SG_COUNT(
				SG_RUNE_COMPACT_WIRE_SECTION_MECHANISMS);
			++mechanism_index)
		{
			const uint8_t *mechanism = SG_RECORD(
				SG_RUNE_COMPACT_WIRE_SECTION_MECHANISMS, mechanism_index);
			uint32_t transition = sg_wire_u32(mechanism + 56U);
			const uint32_t transition_end = transition +
				sg_wire_u32(mechanism + 60U);

			while (transition < transition_end &&
				sg_wire_u32(SG_RECORD(
					SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS,
					transition) + 4U) ==
					(uint32_t)SG_RUNE_COMPACT_STATIC_TRANSITION_PORTAL_STATE)
			{
				const uint8_t *transition_record = SG_RECORD(
					SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS, transition);

				if (binding_cursor >= SG_COUNT(
						SG_RUNE_COMPACT_WIRE_SECTION_PORTAL_MECHANISMS) ||
					sg_wire_u32(SG_RECORD(
						SG_RUNE_COMPACT_WIRE_SECTION_PORTAL_MECHANISMS,
						binding_cursor) + 4U) != mechanism_index ||
					sg_wire_u32(SG_RECORD(
						SG_RUNE_COMPACT_WIRE_SECTION_PORTAL_MECHANISMS,
						binding_cursor)) != sg_wire_u32(transition_record + 32U))
					SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
						SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS, transition);
				++binding_cursor;
				++transition;
			}
			while (binding_cursor < SG_COUNT(
					SG_RUNE_COMPACT_WIRE_SECTION_PORTAL_MECHANISMS) &&
				sg_wire_u32(SG_RECORD(
					SG_RUNE_COMPACT_WIRE_SECTION_PORTAL_MECHANISMS,
					binding_cursor) + 4U) == mechanism_index)
				SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
					SG_RUNE_COMPACT_WIRE_SECTION_PORTAL_MECHANISMS,
					binding_cursor);
		}
		if (binding_cursor != SG_COUNT(
				SG_RUNE_COMPACT_WIRE_SECTION_PORTAL_MECHANISMS))
			SG_FAIL(SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
				SG_RUNE_COMPACT_WIRE_SECTION_PORTAL_MECHANISMS,
				binding_cursor);
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
	if ((uint64_t)image_size > SG_RuneCompactWireImageLimit())
		return sg_wire_record_error(error,
			SG_RUNE_COMPACT_WIRE_ERROR_LIMIT_EXCEEDED,
			SG_RUNE_COMPACT_WIRE_SECTION_COUNT, UINT32_MAX);
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
	size_t bytes;

	if (count != 0U && size > SIZE_MAX / (size_t)count)
		return 0;
	bytes = (size_t)count * size;
	if (bytes > SIZE_MAX - (size_t)7)
		return 0;
	bytes = (bytes + (size_t)7) & ~(size_t)7;
	if (*total > SIZE_MAX - bytes)
		return 0;
	*total += bytes;
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

static void sg_wire_get_authority_transition(const uint8_t *p,
	sg_rune_compact_mechanism_transition_t *transition)
{
	uint32_t axis;

	transition->mechanism = sg_wire_u32(p);
	transition->kind =
		(sg_rune_compact_mechanism_transition_kind_t)sg_wire_u32(p + 4);
	transition->entry_cell.value = sg_wire_u32(p + 8);
	transition->exit_cell.value = sg_wire_u32(p + 12);
	transition->source_state =
		(sg_rune_compact_mechanism_authority_state_t)sg_wire_u32(p + 16);
	transition->destination_state =
		(sg_rune_compact_mechanism_authority_state_t)sg_wire_u32(p + 20);
	transition->elapsed_ms = sg_wire_u64(p + 24);
	switch (transition->kind)
	{
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE:
		transition->value.portal_state.portal.value = sg_wire_u32(p + 32);
		transition->value.portal_state.mover_model = sg_wire_u32(p + 36);
		transition->value.portal_state.delay_ms = sg_wire_u32(p + 40);
		transition->value.portal_state.dwell_ms = sg_wire_u32(p + 44);
		transition->value.portal_state.pause_ms = sg_wire_u32(p + 48);
		transition->value.portal_state.travel_ms = sg_wire_u32(p + 52);
		transition->value.portal_state.recovery_ms = sg_wire_u32(p + 56);
		transition->value.portal_state.source_blocked = p[60];
		transition->value.portal_state.destination_blocked = p[61];
		break;
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_TELEPORT:
		transition->value.teleport.destination.entity_ordinal = sg_wire_u32(p + 32);
		transition->value.teleport.fanout_ordinal = sg_wire_u32(p + 36);
		sg_wire_get_vec3(p + 40, &transition->value.teleport.approach_witness);
		sg_wire_get_vec3(p + 52, &transition->value.teleport.entry_witness);
		sg_wire_get_vec3(p + 64, &transition->value.teleport.exit_witness);
		for (axis = 0U; axis < 3U; axis++)
			transition->value.teleport.arrival_velocity_bits[axis] =
				sg_wire_u32(p + 76U + axis * 4U);
		break;
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_PUSH:
		sg_wire_get_vec3(p + 32, &transition->value.push.approach_witness);
		sg_wire_get_vec3(p + 44, &transition->value.push.entry_witness);
		sg_wire_get_vec3(p + 56, &transition->value.push.exit_witness);
		for (axis = 0U; axis < 3U; axis++)
			transition->value.push.launch_velocity_bits[axis] =
				sg_wire_u32(p + 68U + axis * 4U);
		transition->value.push.gravity_bits = sg_wire_u32(p + 80);
		transition->value.push.flight_ms = sg_wire_u32(p + 84);
		break;
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT:
	{
		uint32_t row;

		transition->value.transport.mover_model = sg_wire_u32(p + 32);
		transition->value.transport.source_surface_ordinal = sg_wire_u32(p + 36);
		sg_wire_get_vec3(p + 40,
			&transition->value.transport.source_player_local);
		sg_wire_get_vec3(p + 52,
			&transition->value.transport.destination_player_local);
		sg_wire_get_vec3(p + 64,
			&transition->value.transport.source_support_local);
		sg_wire_get_vec3(p + 76,
			&transition->value.transport.destination_support_local);
		for (axis = 0U; axis < 3U; axis++) {
			transition->value.transport.source_player_world_bits[axis] =
				sg_wire_u32(p + 88U + axis * 4U);
			transition->value.transport.destination_player_world_bits[axis] =
				sg_wire_u32(p + 100U + axis * 4U);
			transition->value.transport.source_support_world_bits[axis] =
				sg_wire_u32(p + 112U + axis * 4U);
			transition->value.transport.destination_support_world_bits[axis] =
				sg_wire_u32(p + 124U + axis * 4U);
			transition->value.transport.source_mover_origin_bits[axis] =
				sg_wire_u32(p + 136U + axis * 4U);
			transition->value.transport.destination_mover_origin_bits[axis] =
				sg_wire_u32(p + 184U + axis * 4U);
		}
		for (row = 0U; row < 3U; row++)
			for (axis = 0U; axis < 3U; axis++) {
				transition->value.transport.source_mover_axis_bits[row][axis] =
					sg_wire_u32(p + 148U + (row * 3U + axis) * 4U);
				transition->value.transport.destination_mover_axis_bits[row][axis] =
					sg_wire_u32(p + 196U + (row * 3U + axis) * 4U);
			}
		transition->value.transport.source_endpoint.entity_ordinal =
			sg_wire_u32(p + 232);
		transition->value.transport.destination_endpoint.entity_ordinal =
			sg_wire_u32(p + 236);
		transition->value.transport.fanout_ordinal = sg_wire_u32(p + 240);
		transition->value.transport.swept_static_clear = p[244];
		transition->value.transport.start_supported = p[245];
		transition->value.transport.end_supported = p[246];
		transition->value.transport.stance = p[247];
		break;
	}
	case SG_RUNE_COMPACT_MECHANISM_TRANSITION_KIND_COUNT:
		break;
	}
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
	sg_rune_compact_source_surface_t *source_surfaces;
	sg_rune_q8_vec3_t *source_surface_vertices;
	sg_rune_movement_capability_t *movement_capabilities;
	sg_rune_compact_movement_state_t *movement_states;
	sg_rune_compact_movement_fiber_t *movement_fibers;
	sg_rune_compact_movement_hook_target_t *movement_hook_targets;
	sg_rune_analytic_function_index_t *movement_fiber_function_refs;
	sg_rune_compact_movement_angular_schedule_t *movement_angular_schedules;
	sg_rune_compact_response_fragment_t *response_fragments;
	sg_rune_compact_response_halfspace_t *response_halfspaces;
	sg_rune_compact_response_patch_t *response_patches;
	sg_rune_q8_vec3_t *response_target_vertices;
	sg_rune_compact_response_split_t *response_splits;
	sg_rune_compact_response_fact_t *response_facts;
	sg_rune_compact_response_candidate_group_t *response_candidates;
	sg_rune_compact_response_endpoint_group_t *response_source_groups;
	uint32_t *response_source_members;
	sg_rune_compact_response_endpoint_group_t *response_target_groups;
	uint32_t *response_target_members;
	sg_rune_compact_response_seal_t *response_seal;
	sg_rune_weapon_profile_t *weapon_profiles;
	sg_rune_weapon_response_kernel_t *weapon_kernels;
	sg_rune_weapon_function_ref_t *weapon_function_refs;
	sg_rune_compact_weapon_field_attachment_t *weapon_attachments;
	sg_rune_compact_weapon_relation_span_t *weapon_relation_spans;
	sg_rune_compact_response_ref_t *weapon_relation_refs;
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
	sg_rune_compact_static_mechanism_controller_t *controllers;
	sg_rune_compact_mechanism_edge_t *edges;
	sg_rune_compact_static_transition_t *transitions;
	sg_rune_compact_landmark_t *landmarks;
	sg_rune_compact_cell_index_t *landmark_cells;
	sg_rune_compact_facet_annotation_t *annotations;
	sg_rune_compact_portal_mechanism_t *portal_mechanisms;
	sg_rune_compact_static_occluder_t *static_occluders;
	sg_rune_compact_mechanism_authority_t *mechanism_authorities;
	sg_rune_compact_mechanism_controller_t *authority_controllers;
	sg_rune_compact_mechanism_topology_edge_t *authority_edges;
	sg_rune_compact_mechanism_transition_t *authority_transitions;
	uint32_t *authority_transition_static_indices;
	uint32_t *static_transition_authority_indices;
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
	source_surfaces = SG_TAKE(SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACES,
		sg_rune_compact_source_surface_t);
	source_surface_vertices = SG_TAKE(
		SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACE_VERTICES,
		sg_rune_q8_vec3_t);
	movement_capabilities = SG_TAKE(
		SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_CAPABILITIES,
		sg_rune_movement_capability_t);
	movement_states = SG_TAKE(SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_STATES,
		sg_rune_compact_movement_state_t);
	movement_fibers = SG_TAKE(SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBERS,
		sg_rune_compact_movement_fiber_t);
	movement_hook_targets = SG_TAKE(
		SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_HOOK_TARGETS,
		sg_rune_compact_movement_hook_target_t);
	movement_fiber_function_refs = SG_TAKE(
		SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBER_FUNCTION_REFS,
		sg_rune_analytic_function_index_t);
	movement_angular_schedules = SG_TAKE(
		SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_ANGULAR_SCHEDULES,
		sg_rune_compact_movement_angular_schedule_t);
	response_fragments = SG_TAKE(
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FRAGMENTS,
		sg_rune_compact_response_fragment_t);
	response_halfspaces = SG_TAKE(
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_HALFSPACES,
		sg_rune_compact_response_halfspace_t);
	response_patches = SG_TAKE(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_PATCHES,
		sg_rune_compact_response_patch_t);
	response_target_vertices = SG_TAKE(
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_TARGET_VERTICES,
		sg_rune_q8_vec3_t);
	response_splits = SG_TAKE(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SPLITS,
		sg_rune_compact_response_split_t);
	response_facts = SG_TAKE(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FACTS,
		sg_rune_compact_response_fact_t);
	response_candidates = SG_TAKE(
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_CANDIDATE_GROUPS,
		sg_rune_compact_response_candidate_group_t);
	response_source_groups = SG_TAKE(
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SOURCE_ENDPOINT_GROUPS,
		sg_rune_compact_response_endpoint_group_t);
	response_source_members = SG_TAKE(
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SOURCE_ENDPOINT_MEMBERS,
		uint32_t);
	response_target_groups = SG_TAKE(
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_TARGET_ENDPOINT_GROUPS,
		sg_rune_compact_response_endpoint_group_t);
	response_target_members = SG_TAKE(
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_TARGET_ENDPOINT_MEMBERS,
		uint32_t);
	response_seal = SG_TAKE(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SEAL,
		sg_rune_compact_response_seal_t);
	weapon_profiles = SG_TAKE(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_PROFILES, sg_rune_weapon_profile_t);
	weapon_kernels = SG_TAKE(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_KERNELS, sg_rune_weapon_response_kernel_t);
	weapon_function_refs = SG_TAKE(
		SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_FUNCTION_REFS,
		sg_rune_weapon_function_ref_t);
	weapon_attachments = SG_TAKE(
		SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_ATTACHMENTS,
		sg_rune_compact_weapon_field_attachment_t);
	weapon_relation_spans = SG_TAKE(
		SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_RELATION_SPANS,
		sg_rune_compact_weapon_relation_span_t);
	weapon_relation_refs = SG_TAKE(
		SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_RELATION_REFS,
		sg_rune_compact_response_ref_t);
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
	controllers = SG_TAKE(SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_CONTROLLERS, sg_rune_compact_static_mechanism_controller_t);
	edges = SG_TAKE(SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_EDGES, sg_rune_compact_mechanism_edge_t);
	transitions = SG_TAKE(SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS, sg_rune_compact_static_transition_t);
	landmarks = SG_TAKE(SG_RUNE_COMPACT_WIRE_SECTION_LANDMARKS, sg_rune_compact_landmark_t);
	landmark_cells = SG_TAKE(SG_RUNE_COMPACT_WIRE_SECTION_LANDMARK_CELLS, sg_rune_compact_cell_index_t);
	annotations = SG_TAKE(SG_RUNE_COMPACT_WIRE_SECTION_FACET_ANNOTATIONS, sg_rune_compact_facet_annotation_t);
	portal_mechanisms = SG_TAKE(SG_RUNE_COMPACT_WIRE_SECTION_PORTAL_MECHANISMS, sg_rune_compact_portal_mechanism_t);
	static_occluders = SG_TAKE(SG_RUNE_COMPACT_WIRE_SECTION_STATIC_OCCLUDERS,
		sg_rune_compact_static_occluder_t);
	mechanism_authorities = SG_TAKE(
		SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITIES,
		sg_rune_compact_mechanism_authority_t);
	authority_controllers = SG_TAKE(
		SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_CONTROLLERS,
		sg_rune_compact_mechanism_controller_t);
	authority_edges = SG_TAKE(
		SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TOPOLOGY_EDGES,
		sg_rune_compact_mechanism_topology_edge_t);
	authority_transitions = SG_TAKE(
		SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TRANSITIONS,
		sg_rune_compact_mechanism_transition_t);
	authority_transition_static_indices = SG_TAKE(
		SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TRANSITION_STATIC_INDICES,
		uint32_t);
	static_transition_authority_indices = SG_TAKE(
		SG_RUNE_COMPACT_WIRE_SECTION_STATIC_TRANSITION_AUTHORITY_INDICES,
		uint32_t);

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
	model->source_surfaces = source_surfaces;
	model->source_surface_count = descs[
		SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACES].count;
	model->source_surface_vertices = source_surface_vertices;
	model->source_surface_vertex_count = descs[
		SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACE_VERTICES].count;
	model->movement_capabilities = movement_capabilities;
	model->movement_capability_count = descs[
		SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_CAPABILITIES].count;
	model->movement_states = movement_states;
	model->movement_state_count = descs[
		SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_STATES].count;
	model->movement_fibers = movement_fibers;
	model->movement_fiber_count = descs[
		SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBERS].count;
	model->movement_hook_targets = movement_hook_targets;
	model->movement_hook_target_count = descs[
		SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_HOOK_TARGETS].count;
	model->movement_fiber_function_refs = movement_fiber_function_refs;
	model->movement_fiber_function_ref_count = descs[
		SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBER_FUNCTION_REFS].count;
	model->movement_angular_schedules = movement_angular_schedules;
	model->movement_angular_schedule_count = descs[
		SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_ANGULAR_SCHEDULES].count;
	model->response.source_fragments = response_fragments;
	model->response.source_fragment_count = descs[
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FRAGMENTS].count;
	model->response.source_halfspaces = response_halfspaces;
	model->response.source_halfspace_count = descs[
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_HALFSPACES].count;
	model->response.target_patches = response_patches;
	model->response.target_patch_count = descs[
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_PATCHES].count;
	model->response.target_vertices = response_target_vertices;
	model->response.target_vertex_count = descs[
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_TARGET_VERTICES].count;
	model->response.splits = response_splits;
	model->response.split_count = descs[
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SPLITS].count;
	model->response.facts = response_facts;
	model->response.fact_count = descs[
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FACTS].count;
	model->response.candidate_groups = response_candidates;
	model->response.candidate_group_count = descs[
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_CANDIDATE_GROUPS].count;
	model->response.source_endpoint_groups = response_source_groups;
	model->response.source_endpoint_group_count = descs[
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SOURCE_ENDPOINT_GROUPS].count;
	model->response.source_endpoint_members = response_source_members;
	model->response.source_endpoint_member_count = descs[
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SOURCE_ENDPOINT_MEMBERS].count;
	model->response.target_endpoint_groups = response_target_groups;
	model->response.target_endpoint_group_count = descs[
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_TARGET_ENDPOINT_GROUPS].count;
	model->response.target_endpoint_members = response_target_members;
	model->response.target_endpoint_member_count = descs[
		SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_TARGET_ENDPOINT_MEMBERS].count;
	model->response.occluders = static_occluders;
	model->response.occluder_count = descs[
		SG_RUNE_COMPACT_WIRE_SECTION_STATIC_OCCLUDERS].count;
	model->weapon_profiles = weapon_profiles;
	model->weapon_profile_count = descs[SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_PROFILES].count;
	model->weapon_kernels = weapon_kernels;
	model->weapon_kernel_count = descs[SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_KERNELS].count;
	model->weapon_function_refs = weapon_function_refs;
	model->weapon_function_ref_count = descs[
		SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_FUNCTION_REFS].count;
	model->weapon_attachments = weapon_attachments;
	model->weapon_attachment_count = descs[
		SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_ATTACHMENTS].count;
	model->weapon_relation_spans = weapon_relation_spans;
	model->weapon_relation_span_count = descs[
		SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_RELATION_SPANS].count;
	model->weapon_relation_refs = weapon_relation_refs;
	model->weapon_relation_ref_count = descs[
		SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_RELATION_REFS].count;
	model->mechanism_authorities = mechanism_authorities;
	model->mechanism_authority_count = descs[
		SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITIES].count;
	model->mechanism_authority_controllers = authority_controllers;
	model->mechanism_authority_controller_count = descs[
		SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_CONTROLLERS].count;
	model->mechanism_authority_topology_edges = authority_edges;
	model->mechanism_authority_topology_edge_count = descs[
		SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TOPOLOGY_EDGES].count;
	model->mechanism_authority_transitions = authority_transitions;
	model->mechanism_authority_transition_count = descs[
		SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TRANSITIONS].count;
	model->mechanism_authority_transition_static_indices =
		authority_transition_static_indices;
	model->static_transition_authority_indices =
		static_transition_authority_indices;
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
	static_data->mechanism_controllers = controllers;
	static_data->mechanism_controller_count = descs[
		SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_CONTROLLERS].count;
	static_data->mechanism_edges = edges;
	static_data->mechanism_edge_count = descs[SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_EDGES].count;
	static_data->transitions = transitions;
	static_data->transition_count = descs[
		SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS].count;
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
		facets[i].kind = (sg_rune_compact_facet_kind_t)sg_wire_u32(p + 56);
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
	for (i = 0U; i < model->source_surface_count; ++i)
	{
		uint32_t axis;

		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACES, i);
		source_surfaces[i].source.model = sg_wire_u32(p);
		source_surfaces[i].source.brush = sg_wire_u32(p + 4);
		source_surfaces[i].source.brush_side = sg_wire_u32(p + 8);
		source_surfaces[i].source.plane = sg_wire_u32(p + 12);
		source_surfaces[i].frame =
			(sg_rune_compact_source_surface_frame_t)sg_wire_u32(p + 16);
		source_surfaces[i].cell.value = sg_wire_u32(p + 20);
		source_surfaces[i].parent_surface = sg_wire_u32(p + 24);
		source_surfaces[i].split_ordinal = sg_wire_u32(p + 28);
		for (axis = 0U; axis < 3U; axis++)
			source_surfaces[i].plane.normal_bits[axis] =
				sg_wire_u32(p + 32U + axis * 4U);
		source_surfaces[i].plane.distance_bits = sg_wire_u32(p + 44);
		source_surfaces[i].vertices.first = sg_wire_u32(p + 48);
		source_surfaces[i].vertices.count = sg_wire_u32(p + 52);
	}
	for (i = 0U; i < model->source_surface_vertex_count; ++i)
		sg_wire_get_vec3(SG_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACE_VERTICES, i),
			&source_surface_vertices[i]);
	for (i = 0U; i < model->movement_capability_count; i++) {
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_CAPABILITIES, i);
		movement_capabilities[i].cell.value = sg_wire_u32(p);
		movement_capabilities[i].boundary_portal.value = sg_wire_u32(p + 4);
		movement_capabilities[i].kind =
			(sg_rune_movement_capability_kind_t)sg_wire_u32(p + 8);
		movement_capabilities[i].source_stances = p[12];
		movement_capabilities[i].destination_stances = p[13];
		memcpy(movement_capabilities[i].reserved, p + 14, 2);
		movement_capabilities[i].fibers.first = sg_wire_u32(p + 16);
		movement_capabilities[i].fibers.count = sg_wire_u32(p + 20);
	}
	for (i = 0U; i < model->movement_state_count; i++) {
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_STATES, i);
		movement_states[i].stance = p[0];
		memcpy(movement_states[i].reserved, p + 1, 3);
		movement_states[i].support =
			(sg_rune_movement_support_kind_t)sg_wire_u32(p + 4);
		movement_states[i].water =
			(sg_rune_movement_water_kind_t)sg_wire_u32(p + 8);
		movement_states[i].hook_phase = (sg_host_hook_phase_t)sg_wire_u32(p + 12);
		movement_states[i].flags = sg_wire_u32(p + 16);
		movement_states[i].mover_mechanism = sg_wire_u32(p + 20);
	}
	for (i = 0U; i < model->movement_fiber_count; i++) {
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBERS, i);
		movement_fibers[i].capability.value = sg_wire_u32(p);
		movement_fibers[i].kind =
			(sg_rune_movement_fiber_kind_t)sg_wire_u32(p + 4);
		movement_fibers[i].state_variables = sg_wire_u32(p + 8);
		movement_fibers[i].source_state.value = sg_wire_u32(p + 12);
		movement_fibers[i].destination_state.value = sg_wire_u32(p + 16);
		movement_fibers[i].functions.first = sg_wire_u32(p + 20);
		movement_fibers[i].functions.count = sg_wire_u32(p + 24);
		movement_fibers[i].hook_targets.first = sg_wire_u32(p + 28);
		movement_fibers[i].hook_targets.count = sg_wire_u32(p + 32);
		movement_fibers[i].mechanism_transition.value = sg_wire_u32(p + 36);
		movement_fibers[i].angular_schedule = sg_wire_u32(p + 40);
		movement_fibers[i].controller_action_controller.value =
			sg_wire_u32(p + 44);
		movement_fibers[i].controller_action_target.value =
			sg_wire_u32(p + 48);
	}
	for (i = 0U; i < model->movement_hook_target_count; i++) {
		sg_rune_analytic_function_span_t *spans[6];
		uint32_t phase;

		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_HOOK_TARGETS, i);
		movement_hook_targets[i].fiber.value = sg_wire_u32(p);
		movement_hook_targets[i].target_kind =
			(sg_host_hook_target_kind_t)sg_wire_u32(p + 4);
		movement_hook_targets[i].provenance =
			(sg_rune_movement_hook_target_provenance_t)sg_wire_u32(p + 8);
		movement_hook_targets[i].response.kind =
			(sg_rune_compact_response_ref_kind_t)sg_wire_u32(p + 12);
		movement_hook_targets[i].response.index = sg_wire_u32(p + 16);
		movement_hook_targets[i].visibility_class =
			(sg_rune_movement_hook_target_class_t)sg_wire_u32(p + 20);
		movement_hook_targets[i].source_stances = p[24];
		movement_hook_targets[i].target_stances = p[25];
		memcpy(movement_hook_targets[i].reserved, p + 26, 2);
		spans[0] = &movement_hook_targets[i].functions.bolt;
		spans[1] = &movement_hook_targets[i].functions.body;
		spans[2] = &movement_hook_targets[i].functions.pull;
		spans[3] = &movement_hook_targets[i].functions.release;
		spans[4] = &movement_hook_targets[i].functions.coast;
		spans[5] = &movement_hook_targets[i].functions.relaunch;
		for (phase = 0U; phase < 6U; phase++) {
			spans[phase]->first = sg_wire_u32(p + 28U + phase * 8U);
			spans[phase]->count = sg_wire_u32(p + 32U + phase * 8U);
		}
	}
	for (i = 0U; i < model->movement_fiber_function_ref_count; i++)
		movement_fiber_function_refs[i].value = sg_wire_u32(SG_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBER_FUNCTION_REFS, i));
	for (i = 0U; i < model->movement_angular_schedule_count; i++) {
		uint32_t axis;

		p = SG_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_ANGULAR_SCHEDULES, i);
		movement_angular_schedules[i].static_mechanism.value = sg_wire_u32(p);
		movement_angular_schedules[i].source_entity = sg_wire_u32(p + 4);
		movement_angular_schedules[i].mover_model = sg_wire_u32(p + 8);
		movement_angular_schedules[i].flags = sg_wire_u32(p + 12);
		for (axis = 0U; axis < 3U; axis++) {
			movement_angular_schedules[i].initial_angles_bits[axis] =
				sg_wire_u32(p + 16U + axis * 4U);
			movement_angular_schedules[i].axis_bits[axis] =
				sg_wire_u32(p + 28U + axis * 4U);
			movement_angular_schedules[i].angular_velocity_bits[axis] =
				sg_wire_u32(p + 40U + axis * 4U);
			movement_angular_schedules[i].frame_angular_delta_bits[axis] =
				sg_wire_u32(p + 52U + axis * 4U);
		}
		movement_angular_schedules[i].speed_bits = sg_wire_u32(p + 64);
		movement_angular_schedules[i].frame_ms = sg_wire_u32(p + 68);
		movement_angular_schedules[i].authority_mechanism.value =
			sg_wire_u32(p + 72);
	}
	p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_RUNTIME, 0U);
	model->movement_pmove_abi.version = sg_wire_u32(p);
	model->movement_pmove_abi.game_api_version = sg_wire_u32(p + 4);
	model->movement_pmove_abi.import_size = sg_wire_u32(p + 8);
	model->movement_pmove_abi.pmove_offset = sg_wire_u32(p + 12);
	model->movement_pmove_abi.pmove_size = sg_wire_u32(p + 16);
	model->movement_pmove_abi.state_size = sg_wire_u32(p + 20);
	model->movement_pmove_abi.command_size = sg_wire_u32(p + 24);
	model->movement_pmove_abi.fraction_bits = sg_wire_u32(p + 28);
	model->movement_pmove_abi.substep_ms = sg_wire_u32(p + 32);
	model->movement_pmove_abi.identity = sg_wire_u64(p + 36);
	model->movement_pmove_behavior_fingerprint = sg_wire_u64(p + 44);
	model->movement_host_level_generation = sg_wire_u64(p + 52);
	model->movement_physics_abi_id = sg_wire_u64(p + 60);
	model->movement_collision_law_id = sg_wire_u64(p + 68);
	model->movement_pmove_law_id = sg_wire_u64(p + 76);
	model->movement_gravity_law_id = sg_wire_u64(p + 84);
	model->movement_hook_law_id = sg_wire_u64(p + 92);
	model->movement_mechanism_law_id = sg_wire_u64(p + 100);
	for (i = 0U; i < model->response.source_fragment_count; i++)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FRAGMENTS, i);
		response_fragments[i].parent_cell.value = sg_wire_u32(p);
		response_fragments[i].boundary_incidences.first = sg_wire_u32(p + 4);
		response_fragments[i].boundary_incidences.count = sg_wire_u32(p + 8);
		response_fragments[i].static_partition_id = sg_wire_u64(p + 12);
		response_fragments[i].configuration_region = sg_wire_u32(p + 20);
		response_fragments[i].configuration_cell = sg_wire_u32(p + 24);
		response_fragments[i].first_halfspace = sg_wire_u32(p + 28);
		response_fragments[i].halfspace_count = sg_wire_u32(p + 32);
		sg_wire_get_bounds(p + 36, &response_fragments[i].bounds);
		sg_wire_get_vec3(p + 60, &response_fragments[i].witness);
		response_fragments[i].bsp_leaf = sg_wire_u32(p + 72);
		response_fragments[i].bsp_area = sg_wire_u32(p + 76);
		response_fragments[i].bsp_cluster = sg_wire_u32(p + 80);
		response_fragments[i].valid_stances = p[84];
	}
	for (i = 0U; i < model->response.source_halfspace_count; i++)
	{
		uint32_t axis;
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_HALFSPACES, i);
		for (axis = 0U; axis < 3U; axis++)
			response_halfspaces[i].plane.normal_bits[axis] =
				sg_wire_u32(p + axis * 4U);
		response_halfspaces[i].plane.distance_bits = sg_wire_u32(p + 12);
		response_halfspaces[i].split = sg_wire_u32(p + 16);
		response_halfspaces[i].open = p[20];
	}
	for (i = 0U; i < model->response.target_patch_count; i++)
	{
		uint32_t axis;
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_PATCHES, i);
		response_patches[i].visibility_surface_id = sg_wire_u64(p);
		response_patches[i].model = sg_wire_u32(p + 8);
		response_patches[i].brush = sg_wire_u32(p + 12);
		response_patches[i].brush_side = sg_wire_u32(p + 16);
		response_patches[i].source_surface = sg_wire_u32(p + 20);
		response_patches[i].source_frame =
			(sg_rune_compact_source_surface_frame_t)sg_wire_u32(p + 24);
		response_patches[i].parent_facet.value = sg_wire_u32(p + 28);
		response_patches[i].target_cell.value = sg_wire_u32(p + 32);
		response_patches[i].boundary_incidences.first = sg_wire_u32(p + 36);
		response_patches[i].boundary_incidences.count = sg_wire_u32(p + 40);
		response_patches[i].static_partition_id = sg_wire_u64(p + 44);
		response_patches[i].configuration_region = sg_wire_u32(p + 52);
		response_patches[i].configuration_cell = sg_wire_u32(p + 56);
		for (axis = 0U; axis < 3U; axis++)
			response_patches[i].plane.normal_bits[axis] =
				sg_wire_u32(p + 60U + axis * 4U);
		response_patches[i].plane.distance_bits = sg_wire_u32(p + 72);
		response_patches[i].first_vertex = sg_wire_u32(p + 76);
		response_patches[i].vertex_count = sg_wire_u32(p + 80);
		sg_wire_get_bounds(p + 84, &response_patches[i].bounds);
		response_patches[i].bsp_leaf = sg_wire_u32(p + 108);
		response_patches[i].bsp_area = sg_wire_u32(p + 112);
		response_patches[i].bsp_cluster = sg_wire_u32(p + 116);
		response_patches[i].flags = sg_wire_u32(p + 120);
		response_patches[i].valid_stances = p[124];
	}
	for (i = 0U; i < model->response.target_vertex_count; i++)
		sg_wire_get_vec3(SG_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_TARGET_VERTICES, i),
			&response_target_vertices[i]);
	for (i = 0U; i < model->response.split_count; i++)
	{
		uint32_t axis;
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SPLITS, i);
		for (axis = 0U; axis < 3U; axis++)
			response_splits[i].plane.normal_bits[axis] =
				sg_wire_u32(p + axis * 4U);
		response_splits[i].plane.distance_bits = sg_wire_u32(p + 12);
		response_splits[i].kind =
			(sg_rune_compact_response_split_kind_t)sg_wire_u32(p + 16);
		response_splits[i].target_surface_id = sg_wire_u64(p + 20);
		response_splits[i].occluder = sg_wire_u32(p + 28);
		response_splits[i].edge = sg_wire_u32(p + 32);
		response_splits[i].brush_side = sg_wire_u32(p + 36);
	}
	for (i = 0U; i < model->response.fact_count; i++)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FACTS, i);
		response_facts[i].source_fragment = sg_wire_u32(p);
		response_facts[i].target_patch = sg_wire_u32(p + 4);
		response_facts[i].flags = sg_wire_u32(p + 8);
		response_facts[i].visibility =
			(sg_rune_compact_static_visibility_class_t)sg_wire_u32(p + 12);
		response_facts[i].visibility_reason =
			(sg_rune_compact_static_visibility_reason_t)sg_wire_u32(p + 16);
		response_facts[i].requires_exact_ray = p[20];
		response_facts[i].requires_area_state = p[21];
		response_facts[i].certificate_split = sg_wire_u32(p + 24);
		sg_wire_get_vec3(p + 28, &response_facts[i].target_witness);
		response_facts[i].occluders.first = sg_wire_u32(p + 40);
		response_facts[i].occluders.count = sg_wire_u32(p + 44);
		sg_wire_get_trace(p + 48, &response_facts[i].trace);
	}
	for (i = 0U; i < model->response.candidate_group_count; i++)
	{
		p = SG_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_CANDIDATE_GROUPS, i);
		response_candidates[i].source_group = sg_wire_u32(p);
		response_candidates[i].target_group = sg_wire_u32(p + 4);
		response_candidates[i].classification =
			(sg_rune_compact_static_visibility_class_t)sg_wire_u32(p + 8);
		response_candidates[i].reason =
			(sg_rune_compact_static_visibility_reason_t)sg_wire_u32(p + 12);
		response_candidates[i].requires_exact_ray = p[16];
		response_candidates[i].requires_area_state = p[17];
		response_candidates[i].relation_flags = sg_wire_u32(p + 20);
	}
	for (i = 0U; i < model->response.source_endpoint_group_count; i++)
	{
		p = SG_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SOURCE_ENDPOINT_GROUPS, i);
		response_source_groups[i].bsp_cluster = sg_wire_u32(p);
		response_source_groups[i].bsp_area = sg_wire_u32(p + 4);
		response_source_groups[i].flags = sg_wire_u32(p + 8);
		response_source_groups[i].first_member = sg_wire_u32(p + 12);
		response_source_groups[i].member_count = sg_wire_u32(p + 16);
	}
	for (i = 0U; i < model->response.source_endpoint_member_count; i++)
		response_source_members[i] = sg_wire_u32(SG_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SOURCE_ENDPOINT_MEMBERS, i));
	for (i = 0U; i < model->response.target_endpoint_group_count; i++)
	{
		p = SG_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_TARGET_ENDPOINT_GROUPS, i);
		response_target_groups[i].bsp_cluster = sg_wire_u32(p);
		response_target_groups[i].bsp_area = sg_wire_u32(p + 4);
		response_target_groups[i].flags = sg_wire_u32(p + 8);
		response_target_groups[i].first_member = sg_wire_u32(p + 12);
		response_target_groups[i].member_count = sg_wire_u32(p + 16);
	}
	for (i = 0U; i < model->response.target_endpoint_member_count; i++)
		response_target_members[i] = sg_wire_u32(SG_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_TARGET_ENDPOINT_MEMBERS, i));
	p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SEAL, 0U);
	response_seal->version = sg_wire_u16(p);
	response_seal->reserved = sg_wire_u16(p + 2);
	response_seal->flags = sg_wire_u32(p + 4);
	response_seal->split_frontier_count = sg_wire_u32(p + 8);
	response_seal->source_fragment_count = sg_wire_u32(p + 12);
	response_seal->target_patch_count = sg_wire_u32(p + 16);
	response_seal->split_count = sg_wire_u32(p + 20);
	response_seal->response_pair_count = sg_wire_u32(p + 24);
	response_seal->certified_direct_pair_count = sg_wire_u32(p + 28);
	response_seal->certified_static_impact_pair_count = sg_wire_u32(p + 32);
	response_seal->unresolved_response_pair_count = sg_wire_u32(p + 36);
	response_seal->unresolved_candidate_group_count = sg_wire_u32(p + 40);
	response_seal->source_endpoint_group_count = sg_wire_u32(p + 44);
	response_seal->target_endpoint_group_count = sg_wire_u32(p + 48);
	response_seal->source_endpoint_member_count = sg_wire_u32(p + 52);
	response_seal->target_endpoint_member_count = sg_wire_u32(p + 56);
	response_seal->static_occluder_count = sg_wire_u32(p + 60);
	response_seal->compact_facet_count = sg_wire_u32(p + 64);
	response_seal->compact_cell_count = sg_wire_u32(p + 68);
	response_seal->compact_source_surface_count = sg_wire_u32(p + 72);
	response_seal->compact_source_surface_vertex_count = sg_wire_u32(p + 76);
	response_seal->source_surface_catalog_seal = sg_wire_u64(p + 80);
	model->response.seal = *response_seal;
	model->response.exact_live_prefire_trace_required = p[88];
	for (i = 0; i < model->weapon_profile_count; ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_PROFILES, i);
		weapon_profiles[i].source_profile = sg_wire_u32(p);
		weapon_profiles[i].response_families = sg_wire_u32(p + 4);
		weapon_profiles[i].projectile_count_min = sg_wire_u16(p + 8);
		weapon_profiles[i].projectile_count_max = sg_wire_u16(p + 10);
		weapon_profiles[i].auxiliary_trace_count = sg_wire_u16(p + 12);
		weapon_profiles[i].direct_response_count = p[14];
		weapon_profiles[i].reserved = p[15];
	}
	for (i = 0; i < model->weapon_kernel_count; ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_KERNELS, i);
		weapon_kernels[i].profile = sg_wire_u32(p);
		weapon_kernels[i].family =
			(sg_rune_weapon_response_family_t)sg_wire_u32(p + 4);
		weapon_kernels[i].functions.first = sg_wire_u32(p + 8);
		weapon_kernels[i].functions.count = sg_wire_u32(p + 12);
		weapon_kernels[i].event_law.kind =
			(sg_rune_weapon_event_law_kind_t)sg_wire_u32(p + 16);
		weapon_kernels[i].event_law.requirements = sg_wire_u32(p + 20);
	}
	for (i = 0; i < model->weapon_function_ref_count; ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_FUNCTION_REFS,
			i);
		weapon_function_refs[i].function.value = sg_wire_u32(p);
		weapon_function_refs[i].channel =
			(sg_rune_weapon_effect_channel_t)sg_wire_u32(p + 4);
		weapon_function_refs[i].instance = sg_wire_u32(p + 8);
	}
	for (i = 0U; i < model->weapon_attachment_count; i++) {
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_ATTACHMENTS, i);
		weapon_attachments[i].cell.value = sg_wire_u32(p);
		weapon_attachments[i].source_surface = sg_wire_u32(p + 4);
		weapon_attachments[i].relation_class =
			(sg_rune_compact_weapon_relation_class_t)sg_wire_u32(p + 8);
		weapon_attachments[i].reserved0 = sg_wire_u32(p + 12);
		weapon_attachments[i].relations.first = sg_wire_u32(p + 16);
		weapon_attachments[i].relations.count = sg_wire_u32(p + 20);
		weapon_attachments[i].relation_span = sg_wire_u32(p + 24);
		weapon_attachments[i].reserved1 = sg_wire_u32(p + 28);
	}
	for (i = 0U; i < model->weapon_relation_span_count; i++) {
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_RELATION_SPANS, i);
		weapon_relation_spans[i].references.first = sg_wire_u32(p);
		weapon_relation_spans[i].references.count = sg_wire_u32(p + 4);
	}
	for (i = 0U; i < model->weapon_relation_ref_count; i++) {
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_RELATION_REFS, i);
		weapon_relation_refs[i].kind =
			(sg_rune_compact_response_ref_kind_t)sg_wire_u32(p);
		weapon_relation_refs[i].index = sg_wire_u32(p + 4);
	}
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
		mechanisms[i].entry_cell.value = sg_wire_u32(p + 4);
		mechanisms[i].exit_cell.value = sg_wire_u32(p + 8);
		mechanisms[i].activation_landmark.value = sg_wire_u32(p + 12);
		sg_wire_get_bounds(p + 16, &mechanisms[i].bounds);
		mechanisms[i].controllers.first = sg_wire_u32(p + 40);
		mechanisms[i].controllers.count = sg_wire_u32(p + 44);
		mechanisms[i].topology.first = sg_wire_u32(p + 48);
		mechanisms[i].topology.count = sg_wire_u32(p + 52);
		mechanisms[i].transitions.first = sg_wire_u32(p + 56);
		mechanisms[i].transitions.count = sg_wire_u32(p + 60);
		mechanisms[i].delay_ms = sg_wire_u32(p + 64);
		mechanisms[i].dwell_ms = sg_wire_u32(p + 68);
		mechanisms[i].travel_ms = sg_wire_u32(p + 72);
		mechanisms[i].wait_ms = sg_wire_u32(p + 76);
		mechanisms[i].reset_ms = sg_wire_u32(p + 80);
		mechanisms[i].activation_mask = sg_wire_u32(p + 84);
		mechanisms[i].damage = (int32_t)sg_wire_u32(p + 88);
		mechanisms[i].health = (int32_t)sg_wire_u32(p + 92);
		mechanisms[i].required_item = sg_wire_u32(p + 96);
		mechanisms[i].transition_destination.entity_ordinal = sg_wire_u32(p + 100);
		mechanisms[i].transition_fanout_ordinal = sg_wire_u32(p + 104);
		mechanisms[i].launch_velocity_bits[0] = sg_wire_u32(p + 108);
		mechanisms[i].launch_velocity_bits[1] = sg_wire_u32(p + 112);
		mechanisms[i].launch_velocity_bits[2] = sg_wire_u32(p + 116);
		mechanisms[i].gravity_bits = sg_wire_u32(p + 120);
		mechanisms[i].flight_ms = sg_wire_u32(p + 124);
		mechanisms[i].kind = (sg_rune_compact_mechanism_kind_t)sg_wire_u32(p + 128);
		mechanisms[i].initial_state = (sg_rune_compact_mechanism_state_t)sg_wire_u32(p + 132);
		mechanisms[i].activated_state = (sg_rune_compact_mechanism_state_t)sg_wire_u32(p + 136);
		mechanisms[i].reset_state = (sg_rune_compact_mechanism_state_t)sg_wire_u32(p + 140);
		mechanisms[i].recovery = (sg_rune_compact_mechanism_recovery_t)sg_wire_u32(p + 144);
		mechanisms[i].flags = p[148];
	}
	for (i = 0; i < static_data->mechanism_controller_count; ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_CONTROLLERS, i);
		controllers[i].controller.entity_ordinal = sg_wire_u32(p);
		controllers[i].topology_edge = sg_wire_u32(p + 4);
		controllers[i].spatiality = p[8];
		memcpy(controllers[i].reserved, p + 9, 3);
		controllers[i].activation_cell.value = sg_wire_u32(p + 12);
		sg_wire_get_vec3(p + 16, &controllers[i].activation_witness);
		sg_wire_get_bounds(p + 28, &controllers[i].activation_bounds);
	}
	for (i = 0; i < static_data->mechanism_edge_count; ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_EDGES, i);
		edges[i].source.entity_ordinal = sg_wire_u32(p);
		edges[i].destination.entity_ordinal = sg_wire_u32(p + 4);
		edges[i].fanout_ordinal = sg_wire_u32(p + 8);
		edges[i].kind = (sg_rune_compact_mechanism_edge_kind_t)sg_wire_u32(p + 12);
	}
	for (i = 0; i < static_data->transition_count; ++i)
	{
		uint32_t axis;

		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS, i);
		transitions[i].mechanism.value = sg_wire_u32(p);
		transitions[i].kind = (sg_rune_compact_static_transition_kind_t)
			sg_wire_u32(p + 4);
		transitions[i].entry_cell.value = sg_wire_u32(p + 8);
		transitions[i].exit_cell.value = sg_wire_u32(p + 12);
		transitions[i].source_state =
			(sg_rune_compact_mechanism_state_t)sg_wire_u32(p + 16);
		transitions[i].destination_state =
			(sg_rune_compact_mechanism_state_t)sg_wire_u32(p + 20);
		transitions[i].elapsed_ms = sg_wire_u64(p + 24);
		switch (transitions[i].kind)
		{
		case SG_RUNE_COMPACT_STATIC_TRANSITION_PORTAL_STATE:
			transitions[i].value.portal_state.portal.value = sg_wire_u32(p + 32);
			transitions[i].value.portal_state.mover_model = sg_wire_u32(p + 36);
			transitions[i].value.portal_state.delay_ms = sg_wire_u32(p + 40);
			transitions[i].value.portal_state.dwell_ms = sg_wire_u32(p + 44);
			transitions[i].value.portal_state.pause_ms = sg_wire_u32(p + 48);
			transitions[i].value.portal_state.travel_ms = sg_wire_u32(p + 52);
			transitions[i].value.portal_state.recovery_ms = sg_wire_u32(p + 56);
			transitions[i].value.portal_state.source_blocked = p[60];
			transitions[i].value.portal_state.destination_blocked = p[61];
			transitions[i].value.portal_state.reserved[0] = p[62];
			transitions[i].value.portal_state.reserved[1] = p[63];
			break;
		case SG_RUNE_COMPACT_STATIC_TRANSITION_TELEPORT:
			transitions[i].value.teleport.destination.entity_ordinal =
				sg_wire_u32(p + 32);
			transitions[i].value.teleport.fanout_ordinal = sg_wire_u32(p + 36);
			sg_wire_get_vec3(p + 40,
				&transitions[i].value.teleport.approach_witness);
			sg_wire_get_vec3(p + 52,
				&transitions[i].value.teleport.entry_witness);
			sg_wire_get_vec3(p + 64,
				&transitions[i].value.teleport.exit_witness);
			for (axis = 0U; axis < 3U; axis++)
				transitions[i].value.teleport.arrival_velocity_bits[axis] =
					sg_wire_u32(p + 76U + axis * 4U);
			break;
		case SG_RUNE_COMPACT_STATIC_TRANSITION_PUSH:
			sg_wire_get_vec3(p + 32,
				&transitions[i].value.push.approach_witness);
			sg_wire_get_vec3(p + 44,
				&transitions[i].value.push.entry_witness);
			sg_wire_get_vec3(p + 56,
				&transitions[i].value.push.exit_witness);
			for (axis = 0U; axis < 3U; axis++)
				transitions[i].value.push.launch_velocity_bits[axis] =
					sg_wire_u32(p + 68U + axis * 4U);
			transitions[i].value.push.gravity_bits = sg_wire_u32(p + 80);
			transitions[i].value.push.flight_ms = sg_wire_u32(p + 84);
			break;
		case SG_RUNE_COMPACT_STATIC_TRANSITION_TRANSPORT:
		{
			uint32_t row;

			transitions[i].value.transport.mover_model = sg_wire_u32(p + 32);
			transitions[i].value.transport.source_surface_ordinal =
				sg_wire_u32(p + 36);
			sg_wire_get_vec3(p + 40,
				&transitions[i].value.transport.source_player_local);
			sg_wire_get_vec3(p + 52,
				&transitions[i].value.transport.destination_player_local);
			sg_wire_get_vec3(p + 64,
				&transitions[i].value.transport.source_support_local);
			sg_wire_get_vec3(p + 76,
				&transitions[i].value.transport.destination_support_local);
			for (axis = 0U; axis < 3U; axis++)
			{
				transitions[i].value.transport.source_player_world_bits[axis] =
					sg_wire_u32(p + 88U + axis * 4U);
				transitions[i].value.transport.destination_player_world_bits[axis] =
					sg_wire_u32(p + 100U + axis * 4U);
				transitions[i].value.transport.source_support_world_bits[axis] =
					sg_wire_u32(p + 112U + axis * 4U);
				transitions[i].value.transport.destination_support_world_bits[axis] =
					sg_wire_u32(p + 124U + axis * 4U);
				transitions[i].value.transport.source_mover_origin_bits[axis] =
					sg_wire_u32(p + 136U + axis * 4U);
				transitions[i].value.transport.destination_mover_origin_bits[axis] =
					sg_wire_u32(p + 184U + axis * 4U);
			}
			for (row = 0U; row < 3U; row++)
				for (axis = 0U; axis < 3U; axis++) {
					transitions[i].value.transport.source_mover_axis_bits[row][axis] =
						sg_wire_u32(p + 148U + (row * 3U + axis) * 4U);
					transitions[i].value.transport.destination_mover_axis_bits[row][axis] =
						sg_wire_u32(p + 196U + (row * 3U + axis) * 4U);
				}
			transitions[i].value.transport.source_endpoint.entity_ordinal =
				sg_wire_u32(p + 232);
			transitions[i].value.transport.destination_endpoint.entity_ordinal =
				sg_wire_u32(p + 236);
			transitions[i].value.transport.fanout_ordinal = sg_wire_u32(p + 240);
			transitions[i].value.transport.swept_static_clear = p[244];
			transitions[i].value.transport.start_supported = p[245];
			transitions[i].value.transport.end_supported = p[246];
			transitions[i].value.transport.stance = p[247];
			break;
		}
		case SG_RUNE_COMPACT_STATIC_TRANSITION_KIND_COUNT:
			break;
		}
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
		annotations[i].source_surface = sg_wire_u32(p + 8);
		annotations[i].source_frame =
			(sg_rune_compact_source_surface_frame_t)sg_wire_u32(p + 12);
	}
	for (i = 0; i < static_data->portal_mechanism_count; ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_PORTAL_MECHANISMS, i);
		portal_mechanisms[i].portal.value = sg_wire_u32(p);
		portal_mechanisms[i].mechanism.value = sg_wire_u32(p + 4);
		portal_mechanisms[i].kind = (sg_rune_compact_portal_mechanism_kind_t)sg_wire_u32(p + 8);
	}
	for (i = 0U; i < model->mechanism_authority_count; i++) {
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITIES, i);
		mechanism_authorities[i].source.entity_ordinal = sg_wire_u32(p);
		mechanism_authorities[i].kind =
			(sg_rune_compact_mechanism_authority_kind_t)sg_wire_u32(p + 4);
		mechanism_authorities[i].activation = sg_wire_u32(p + 8);
		mechanism_authorities[i].activation_cell.value = sg_wire_u32(p + 12);
		sg_wire_get_vec3(p + 16, &mechanism_authorities[i].activation_witness);
		sg_wire_get_bounds(p + 28, &mechanism_authorities[i].activation_bounds);
		mechanism_authorities[i].controllers.first = sg_wire_u32(p + 52);
		mechanism_authorities[i].controllers.count = sg_wire_u32(p + 56);
		mechanism_authorities[i].topology.first = sg_wire_u32(p + 60);
		mechanism_authorities[i].topology.count = sg_wire_u32(p + 64);
		mechanism_authorities[i].transitions.first = sg_wire_u32(p + 68);
		mechanism_authorities[i].transitions.count = sg_wire_u32(p + 72);
		mechanism_authorities[i].delay_ms = sg_wire_u32(p + 76);
		mechanism_authorities[i].dwell_ms = sg_wire_u32(p + 80);
		mechanism_authorities[i].pause_ms = sg_wire_u32(p + 84);
		mechanism_authorities[i].travel_ms = sg_wire_u32(p + 88);
		mechanism_authorities[i].damage = (int32_t)sg_wire_u32(p + 92);
		mechanism_authorities[i].health = (int32_t)sg_wire_u32(p + 96);
		mechanism_authorities[i].required_item = sg_wire_u32(p + 100);
		mechanism_authorities[i].initial_state =
			(sg_rune_compact_mechanism_authority_state_t)sg_wire_u32(p + 104);
		mechanism_authorities[i].activated_state =
			(sg_rune_compact_mechanism_authority_state_t)sg_wire_u32(p + 108);
		mechanism_authorities[i].reset_state =
			(sg_rune_compact_mechanism_authority_state_t)sg_wire_u32(p + 112);
		mechanism_authorities[i].recovery_ms = sg_wire_u32(p + 116);
		mechanism_authorities[i].flags = sg_wire_u32(p + 120);
	}
	for (i = 0U; i < model->mechanism_authority_controller_count; i++) {
		p = SG_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_CONTROLLERS, i);
		authority_controllers[i].mechanism = sg_wire_u32(p);
		authority_controllers[i].controller.entity_ordinal = sg_wire_u32(p + 4);
		authority_controllers[i].topology_edge = sg_wire_u32(p + 8);
		authority_controllers[i].activation = sg_wire_u32(p + 12);
		authority_controllers[i].damage = (int32_t)sg_wire_u32(p + 16);
		authority_controllers[i].health = (int32_t)sg_wire_u32(p + 20);
		authority_controllers[i].required_item = sg_wire_u32(p + 24);
		authority_controllers[i].flags = sg_wire_u32(p + 28);
		authority_controllers[i].spatiality = p[32];
		memcpy(authority_controllers[i].reserved, p + 33, 3);
		authority_controllers[i].activation_cell.value = sg_wire_u32(p + 36);
		sg_wire_get_vec3(p + 40,
			&authority_controllers[i].activation_witness);
		sg_wire_get_bounds(p + 52, &authority_controllers[i].activation_bounds);
	}
	for (i = 0U; i < model->mechanism_authority_topology_edge_count; i++) {
		p = SG_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TOPOLOGY_EDGES, i);
		authority_edges[i].source.entity_ordinal = sg_wire_u32(p);
		authority_edges[i].destination.entity_ordinal = sg_wire_u32(p + 4);
		authority_edges[i].kind = (sg_mech_edge_kind_t)sg_wire_u32(p + 8);
		authority_edges[i].fanout_ordinal = sg_wire_u32(p + 12);
	}
	for (i = 0U; i < model->mechanism_authority_transition_count; i++)
		sg_wire_get_authority_transition(SG_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TRANSITIONS, i),
			&authority_transitions[i]);
	for (i = 0U; i < model->mechanism_authority_transition_count; i++)
		authority_transition_static_indices[i] = sg_wire_u32(SG_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TRANSITION_STATIC_INDICES,
			i));
	for (i = 0U; i < static_data->transition_count; i++)
		static_transition_authority_indices[i] = sg_wire_u32(SG_RECORD(
			SG_RUNE_COMPACT_WIRE_SECTION_STATIC_TRANSITION_AUTHORITY_INDICES, i));
	for (i = 0; i < model->response.occluder_count; ++i)
	{
		p = SG_RECORD(SG_RUNE_COMPACT_WIRE_SECTION_STATIC_OCCLUDERS, i);
		static_occluders[i].model = sg_wire_u32(p);
		static_occluders[i].brush = sg_wire_u32(p + 4);
		static_occluders[i].contents = sg_wire_u32(p + 8);
		static_occluders[i].conditional = sg_wire_u32(p + 12);
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
		sizeof(sg_rune_movement_capability_t),
		sizeof(sg_rune_compact_movement_state_t),
		sizeof(sg_rune_compact_movement_fiber_t),
		sizeof(sg_rune_compact_movement_hook_target_t),
		sizeof(sg_rune_analytic_function_index_t),
		sizeof(sg_rune_compact_movement_angular_schedule_t),
		0U,
		sizeof(sg_rune_compact_response_fragment_t),
		sizeof(sg_rune_compact_response_halfspace_t),
		sizeof(sg_rune_compact_response_patch_t),
		sizeof(sg_rune_q8_vec3_t),
		sizeof(sg_rune_compact_response_split_t),
		sizeof(sg_rune_compact_response_fact_t),
		sizeof(sg_rune_compact_response_candidate_group_t),
		sizeof(sg_rune_compact_response_endpoint_group_t),
		sizeof(uint32_t),
		sizeof(sg_rune_compact_response_endpoint_group_t),
		sizeof(uint32_t),
		sizeof(sg_rune_compact_response_seal_t),
		sizeof(sg_rune_compact_static_occluder_t),
		sizeof(sg_rune_weapon_profile_t),
		sizeof(sg_rune_weapon_response_kernel_t),
		sizeof(sg_rune_weapon_function_ref_t),
		sizeof(sg_rune_compact_weapon_field_attachment_t),
		sizeof(sg_rune_compact_weapon_relation_span_t),
		sizeof(sg_rune_compact_response_ref_t),
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
		sizeof(sg_rune_compact_static_mechanism_controller_t),
		sizeof(sg_rune_compact_mechanism_edge_t),
		sizeof(sg_rune_compact_static_transition_t),
		sizeof(sg_rune_compact_landmark_t),
		sizeof(sg_rune_compact_cell_index_t),
		sizeof(sg_rune_compact_facet_annotation_t),
		sizeof(sg_rune_compact_portal_mechanism_t),
		sizeof(sg_rune_compact_source_surface_t),
		sizeof(sg_rune_q8_vec3_t),
		sizeof(sg_rune_compact_mechanism_authority_t),
		sizeof(sg_rune_compact_mechanism_controller_t),
		sizeof(sg_rune_compact_mechanism_topology_edge_t),
		sizeof(sg_rune_compact_mechanism_transition_t),
		sizeof(uint32_t),
		sizeof(uint32_t)
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

const char *SG_RuneCompactWireSectionName(
	sg_rune_compact_wire_section_t section)
{
	if ((uint32_t)section <
		(uint32_t)SG_RUNE_COMPACT_WIRE_SECTION_COUNT)
		return sg_wire_section_names[(uint32_t)section];
	if (section == SG_RUNE_COMPACT_WIRE_SECTION_COUNT)
		return "none";
	return "unknown";
}
