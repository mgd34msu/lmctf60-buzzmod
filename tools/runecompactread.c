/*
 * Independent compact-RUNE reader.
 *
 * This file does not include or link the production compact codec.  All
 * accesses go through explicit little-endian byte readers and the constants
 * below are the wire contract, rather than host structure layout.  The
 * inspection API is allocation-free.  The command-line front end allocates
 * buffers for its input files.
 */

#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#define _FILE_OFFSET_BITS 64
#endif

#include "runecompactread.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <sys/types.h>
#endif

#define RC_ALIGNMENT UINT64_C(8)
#define RC_MAX_IMAGE_BYTES UINT64_C(4294967296)
#define RC_CHECKSUM_OFFSET UINT32_C(24)
#define RC_HEADER_TOTAL_BYTES \
	((uint64_t)RUNE_COMPACT_READ_HEADER_FIXED_BYTES + \
	 (uint64_t)RUNE_COMPACT_READ_SECTION_COUNT * \
	 (uint64_t)RUNE_COMPACT_READ_DESCRIPTOR_BYTES)

#define RC_NONE UINT32_MAX
#define RC_FACET_POLYGON UINT32_C(0)
#define RC_FACET_CONSTRAINT_ONLY UINT32_C(1)
#define RC_FACET_KIND_COUNT UINT32_C(2)
#define RC_WEAPON_RESPONSE_FAMILY_COUNT UINT32_C(12)
#define RC_WEAPON_RESPONSE_FAMILIES_ALL \
	((UINT32_C(1) << RC_WEAPON_RESPONSE_FAMILY_COUNT) - UINT32_C(1))

_Static_assert(RUNE_COMPACT_READ_SECTION_COUNT == UINT32_C(28),
	"compact RUNE has 28 sections");
_Static_assert(RC_HEADER_TOTAL_BYTES ==
	(uint64_t)RUNE_COMPACT_READ_HEADER_BYTES,
	"compact RUNE header size must match its section count");

typedef struct rc_spec_s
{
	uint32_t record_bytes;
	uint32_t max_count;
	const char *name;
} rc_spec_t;

typedef struct rc_section_s
{
	uint32_t count;
	uint64_t offset;
	uint64_t bytes;
} rc_section_t;

typedef struct rc_context_s
{
	const unsigned char *image;
	size_t image_size;
	rc_section_t sections[RUNE_COMPACT_READ_SECTION_COUNT];
} rc_context_t;

static const unsigned char rc_magic[8] = {
	0x53U, 0x47U, 0x52U, 0x43U, 0x57U, 0x30U, 0x30U, 0x31U
};

static const rc_spec_t rc_specs[RUNE_COMPACT_READ_SECTION_COUNT] = {
	{ 252U, 1U, "identity" },
	{ 80U, 1048576U, "cells" },
	{ 60U, 4194304U, "facets" },
	{ 20U, 8388608U, "incidences" },
	{ 4U, 8388608U, "cell_incidences" },
	{ 12U, 16777216U, "vertices" },
	{ 44U, 2097152U, "portals" },
	{ 24U, 4194304U, "movement_fields" },
	{ 20U, 4194304U, "weapon_regions" },
	{ 8U, 256U, "weapon_profiles" },
	{ 20U, 8388608U, "weapon_kernels" },
	{ 4U, 33554432U, "analytic_function_refs" },
	{ 20U, 1048576U, "analytic_functions" },
	{ 4U, 16777216U, "analytic_input_dimensions" },
	{ 4U, 1048576U, "analytic_constants" },
	{ 12U, 1048576U, "analytic_affines" },
	{ 4U, 16777216U, "analytic_affine_slopes" },
	{ 12U, 1048576U, "analytic_polynomials" },
	{ 4U, 33554432U, "analytic_polynomial_coefficients" },
	{ 12U, 1048576U, "analytic_ballistics" },
	{ 16U, 1048576U, "analytic_piecewise" },
	{ 16U, 4194304U, "analytic_piecewise_clauses" },
	{ 100U, 1048576U, "mechanisms" },
	{ 16U, 4194304U, "mechanism_edges" },
	{ 60U, 4194304U, "landmarks" },
	{ 4U, 16777216U, "landmark_cells" },
	{ 8U, 4194304U, "facet_annotations" },
	{ 15U, 4194304U, "portal_mechanisms" }
};

_Static_assert(sizeof(rc_specs) / sizeof(rc_specs[0]) ==
	RUNE_COMPACT_READ_SECTION_COUNT,
	"compact RUNE section ledger must be complete");

static void rc_set_error(rune_compact_read_error_t *error,
	rune_compact_read_error_code_t code, uint32_t section, uint32_t record)
{
	if (error == NULL)
		return;
	error->code = code;
	error->section = section < RUNE_COMPACT_READ_SECTION_COUNT ?
		(rune_compact_read_section_t)section :
		RUNE_COMPACT_READ_SECTION_NONE;
	error->record = record;
}

static void rc_clear_error(rune_compact_read_error_t *error)
{
	rc_set_error(error, RUNE_COMPACT_READ_ERROR_NONE, RC_NONE, RC_NONE);
}

static int rc_fail(rune_compact_read_error_t *error,
	rune_compact_read_error_code_t code, uint32_t section, uint32_t record)
{
	rc_set_error(error, code, section, record);
	return 0;
}

static uint16_t rc_u16(const unsigned char *bytes)
{
	return (uint16_t)((uint16_t)bytes[0] |
		((uint16_t)bytes[1] << 8));
}

static uint32_t rc_u32(const unsigned char *bytes)
{
	return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
		((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint64_t rc_u64(const unsigned char *bytes)
{
	uint64_t value = 0U;
	uint32_t index;

	for (index = 0U; index < 8U; ++index)
		value |= (uint64_t)bytes[index] << (index * 8U);
	return value;
}

static int rc_add_u64(uint64_t left, uint64_t right, uint64_t *out)
{
	if (right > UINT64_MAX - left || out == NULL)
		return 0;
	*out = left + right;
	return 1;
}

static int rc_mul_u64(uint64_t left, uint64_t right, uint64_t *out)
{
	if (out == NULL || (left != 0U && right > UINT64_MAX / left))
		return 0;
	*out = left * right;
	return 1;
}

static int rc_align_u64(uint64_t value, uint64_t *out)
{
	uint64_t padded;

	if (!rc_add_u64(value, RC_ALIGNMENT - UINT64_C(1), &padded))
		return 0;
	*out = padded & ~(RC_ALIGNMENT - UINT64_C(1));
	return 1;
}

static int rc_range(uint64_t offset, uint64_t bytes, uint64_t total)
{
	return offset <= total && bytes <= total - offset;
}

static int rc_zero(const unsigned char *bytes, uint64_t count)
{
	uint64_t index;

	for (index = 0U; index < count; ++index)
		if (bytes[(size_t)index] != 0U)
			return 0;
	return 1;
}

static uint32_t rc_crc32(const unsigned char *bytes, size_t size)
{
	uint32_t crc = UINT32_MAX;
	size_t index;

	for (index = 0U; index < size; ++index)
	{
		uint32_t byte = bytes[index];
		uint32_t bit;

		if (index >= (size_t)RC_CHECKSUM_OFFSET &&
			index < (size_t)RC_CHECKSUM_OFFSET + sizeof(uint32_t))
			byte = 0U;
		crc ^= byte;
		for (bit = 0U; bit < 8U; ++bit)
			crc = (crc >> 1) ^
				((crc & 1U) != 0U ? UINT32_C(0xedb88320) : 0U);
	}
	return ~crc;
}

static int rc_span(const unsigned char *bytes, uint32_t total,
	uint32_t minimum, uint32_t maximum)
{
	uint32_t first = rc_u32(bytes);
	uint32_t count = rc_u32(bytes + 4U);

	return count >= minimum && count <= maximum && first <= total &&
		count <= total - first;
}

static int rc_ref(uint32_t value, uint32_t total, int allow_none)
{
	return value < total || (allow_none && value == RC_NONE);
}

static int rc_source(const unsigned char *bytes, uint32_t split_parent_limit)
{
	uint32_t kind = rc_u32(bytes);

	if (kind >= 4U)
		return 0;
	if (kind == 0U)
		return rc_zero(bytes + 12U, 8U);
	if (kind == 1U)
		return rc_zero(bytes + 16U, 4U);
	if (kind == 3U)
		return rc_ref(rc_u32(bytes + 4U), split_parent_limit, 0) &&
			rc_zero(bytes + 12U, 8U);
	return 1;
}

static const unsigned char *rc_record(const rc_context_t *context,
	uint32_t section, uint32_t record)
{
	const rc_section_t *description = &context->sections[section];

	return context->image + (size_t)description->offset +
		(size_t)record * (size_t)rc_specs[section].record_bytes;
}

static int rc_parse_header(rc_context_t *context,
	rune_compact_read_error_t *error)
{
	const unsigned char *image = context->image;
	uint64_t total;
	uint64_t cursor;
	uint32_t section;

	if ((uintmax_t)context->image_size > (uintmax_t)RC_MAX_IMAGE_BYTES)
		return rc_fail(error, RUNE_COMPACT_READ_ERROR_LIMIT_EXCEEDED,
			RC_NONE, RC_NONE);
	if (context->image_size < (size_t)RUNE_COMPACT_READ_HEADER_FIXED_BYTES)
		return rc_fail(error, RUNE_COMPACT_READ_ERROR_TRUNCATED,
			RC_NONE, RC_NONE);
	if (memcmp(image, rc_magic, sizeof(rc_magic)) != 0)
		return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_FORMAT,
			RC_NONE, RC_NONE);
	if (rc_u16(image + 8U) != RUNE_COMPACT_READ_WIRE_VERSION ||
		rc_u16(image + 32U) != RUNE_COMPACT_READ_MODEL_VERSION ||
		rc_u32(image + 36U) != RUNE_COMPACT_READ_SCHEMA_TAG ||
		rc_u16(image + 40U) != RUNE_COMPACT_READ_ANALYTIC_VERSION)
		return rc_fail(error, RUNE_COMPACT_READ_ERROR_UNSUPPORTED_VERSION,
			RC_NONE, RC_NONE);
	if (rc_u16(image + 10U) != (uint16_t)RC_HEADER_TOTAL_BYTES ||
		rc_u32(image + 12U) != RUNE_COMPACT_READ_SECTION_COUNT)
		return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_FORMAT,
			RC_NONE, RC_NONE);
	if (context->image_size < (size_t)RC_HEADER_TOTAL_BYTES)
		return rc_fail(error, RUNE_COMPACT_READ_ERROR_TRUNCATED,
			RC_NONE, RC_NONE);
	total = rc_u64(image + 16U);
	if (total > RC_MAX_IMAGE_BYTES)
		return rc_fail(error, RUNE_COMPACT_READ_ERROR_LIMIT_EXCEEDED,
			RC_NONE, RC_NONE);
	if (total != (uint64_t)context->image_size)
		return rc_fail(error,
			total > (uint64_t)context->image_size ?
				RUNE_COMPACT_READ_ERROR_TRUNCATED :
				RUNE_COMPACT_READ_ERROR_INVALID_FORMAT,
			RC_NONE, RC_NONE);
	if (rc_u32(image + 28U) != 0U || rc_u16(image + 34U) != 0U ||
		rc_u16(image + 42U) != 0U || rc_u32(image + 44U) != 0U)
		return rc_fail(error, RUNE_COMPACT_READ_ERROR_NONZERO_RESERVED,
			RC_NONE, RC_NONE);

	cursor = RC_HEADER_TOTAL_BYTES;
	for (section = 0U; section < RUNE_COMPACT_READ_SECTION_COUNT; ++section)
	{
		const unsigned char *descriptor = image +
			(size_t)RUNE_COMPACT_READ_HEADER_FIXED_BYTES +
			(size_t)section * (size_t)RUNE_COMPACT_READ_DESCRIPTOR_BYTES;
		rc_section_t *destination = &context->sections[section];
		uint64_t aligned;
		uint64_t payload_bytes;
		uint64_t end;

		destination->count = rc_u32(descriptor + 8U);
		destination->offset = rc_u64(descriptor + 16U);
		destination->bytes = 0U;
		if (rc_u32(descriptor) != section ||
			rc_u32(descriptor + 4U) != rc_specs[section].record_bytes)
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_SECTION,
				section, RC_NONE);
		if (rc_u32(descriptor + 12U) != 0U)
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_NONZERO_RESERVED,
				section, RC_NONE);
		if (destination->count > rc_specs[section].max_count ||
			(section == RUNE_COMPACT_READ_SECTION_IDENTITY &&
				destination->count != 1U))
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_LIMIT_EXCEEDED,
				section, RC_NONE);
		if (!rc_align_u64(cursor, &aligned))
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_OVERFLOW,
				section, RC_NONE);
		if (destination->offset != aligned || destination->offset > total)
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_SECTION,
				section, RC_NONE);
		if (!rc_range(cursor, aligned - cursor, total) ||
			!rc_zero(image + (size_t)cursor, aligned - cursor))
			return rc_fail(error,
				!rc_range(cursor, aligned - cursor, total) ?
					RUNE_COMPACT_READ_ERROR_INVALID_SECTION :
					RUNE_COMPACT_READ_ERROR_NONZERO_RESERVED,
				section, RC_NONE);
		if (!rc_mul_u64(destination->count,
			(uint64_t)rc_specs[section].record_bytes, &payload_bytes) ||
			!rc_add_u64(destination->offset, payload_bytes, &end) ||
			end > total)
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_OVERFLOW,
				section, RC_NONE);
		destination->bytes = payload_bytes;
		cursor = end;
	}
	{
		uint64_t aligned;

		if (!rc_align_u64(cursor, &aligned) || aligned != total ||
			!rc_zero(image + (size_t)cursor, total - cursor))
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_NONZERO_RESERVED,
				RC_NONE, RC_NONE);
	}
	if (rc_u32(image + RC_CHECKSUM_OFFSET) !=
		rc_crc32(image, context->image_size))
		return rc_fail(error, RUNE_COMPACT_READ_ERROR_CHECKSUM_MISMATCH,
			RC_NONE, RC_NONE);
	return 1;
}

static int rc_validate_cells(const rc_context_t *context,
	rune_compact_read_error_t *error)
{
	const rc_section_t *section = &context->sections[
		RUNE_COMPACT_READ_SECTION_CELLS];
	uint32_t index;

	for (index = 0U; index < section->count; ++index)
	{
		const unsigned char *record = rc_record(context,
			RUNE_COMPACT_READ_SECTION_CELLS, index);

		if (!rc_span(record + 44U, context->sections[
				RUNE_COMPACT_READ_SECTION_CELL_INCIDENCES].count,
				0U, UINT32_MAX) ||
			!rc_span(record + 52U, context->sections[
				RUNE_COMPACT_READ_SECTION_MOVEMENT_FIELDS].count,
				0U, UINT32_MAX) ||
			!rc_span(record + 60U, context->sections[
				RUNE_COMPACT_READ_SECTION_WEAPON_REGIONS].count,
				0U, UINT32_MAX))
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_SPAN,
				RUNE_COMPACT_READ_SECTION_CELLS, index);
		if (!rc_zero(record + 77U, 3U))
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_NONZERO_RESERVED,
				RUNE_COMPACT_READ_SECTION_CELLS, index);
		if ((rc_u32(record + 68U) & ~UINT32_C(0x1fff)) != 0U ||
			(rc_u32(record + 72U) & ~UINT32_C(0x000f)) != 0U ||
			(record[76] & (unsigned char)~UINT8_C(3)) != 0U)
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_VALUE,
				RUNE_COMPACT_READ_SECTION_CELLS, index);
	}
	return 1;
}

static int rc_validate_facets(const rc_context_t *context,
	rune_compact_read_error_t *error)
{
	const rc_section_t *section = &context->sections[
		RUNE_COMPACT_READ_SECTION_FACETS];
	uint32_t index;

	for (index = 0U; index < section->count; ++index)
	{
		const unsigned char *record = rc_record(context,
			RUNE_COMPACT_READ_SECTION_FACETS, index);
		uint32_t kind;
		uint32_t vertex_count;
		uint32_t incidence_count;
		uint32_t portal;

		if (!rc_source(record, section->count))
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_REFERENCE,
				RUNE_COMPACT_READ_SECTION_FACETS, index);
		if (!rc_span(record + 36U, context->sections[
				RUNE_COMPACT_READ_SECTION_VERTICES].count, 0U, UINT32_MAX) ||
			!rc_span(record + 44U, context->sections[
				RUNE_COMPACT_READ_SECTION_INCIDENCES].count, 0U, UINT32_MAX))
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_SPAN,
				RUNE_COMPACT_READ_SECTION_FACETS, index);
		if (!rc_ref(rc_u32(record + 52U), context->sections[
				RUNE_COMPACT_READ_SECTION_PORTALS].count, 1))
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_REFERENCE,
				RUNE_COMPACT_READ_SECTION_FACETS, index);
		kind = rc_u32(record + 56U);
		vertex_count = rc_u32(record + 40U);
		incidence_count = rc_u32(record + 48U);
		portal = rc_u32(record + 52U);
		if ((kind == RC_FACET_POLYGON && vertex_count < 3U) ||
			(kind == RC_FACET_CONSTRAINT_ONLY &&
				(vertex_count != 0U || incidence_count != 1U ||
				portal != RC_NONE)) || kind >= RC_FACET_KIND_COUNT)
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_FORMAT,
				RUNE_COMPACT_READ_SECTION_FACETS, index);
	}
	return 1;
}

static int rc_validate_incidences(const rc_context_t *context,
	rune_compact_read_error_t *error)
{
	const rc_section_t *section = &context->sections[
		RUNE_COMPACT_READ_SECTION_INCIDENCES];
	uint32_t index;

	for (index = 0U; index < section->count; ++index)
	{
		const unsigned char *record = rc_record(context,
			RUNE_COMPACT_READ_SECTION_INCIDENCES, index);

		if (!rc_ref(rc_u32(record), context->sections[
				RUNE_COMPACT_READ_SECTION_CELLS].count, 0) ||
			!rc_ref(rc_u32(record + 4U), context->sections[
				RUNE_COMPACT_READ_SECTION_FACETS].count, 0))
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_REFERENCE,
				RUNE_COMPACT_READ_SECTION_INCIDENCES, index);
		if (rc_u32(record + 12U) >= 2U || rc_u32(record + 16U) >= 2U)
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_VALUE,
				RUNE_COMPACT_READ_SECTION_INCIDENCES, index);
	}
	return 1;
}

static int rc_validate_cell_incidences(const rc_context_t *context,
	rune_compact_read_error_t *error)
{
	const rc_section_t *section = &context->sections[
		RUNE_COMPACT_READ_SECTION_CELL_INCIDENCES];
	uint32_t index;

	for (index = 0U; index < section->count; ++index)
		if (!rc_ref(rc_u32(rc_record(context,
				RUNE_COMPACT_READ_SECTION_CELL_INCIDENCES, index)),
				context->sections[RUNE_COMPACT_READ_SECTION_INCIDENCES].count, 0))
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_REFERENCE,
				RUNE_COMPACT_READ_SECTION_CELL_INCIDENCES, index);
	return 1;
}

static int rc_validate_portals(const rc_context_t *context,
	rune_compact_read_error_t *error)
{
	const rc_section_t *section = &context->sections[
		RUNE_COMPACT_READ_SECTION_PORTALS];
	uint32_t index;

	for (index = 0U; index < section->count; ++index)
	{
		const unsigned char *record = rc_record(context,
			RUNE_COMPACT_READ_SECTION_PORTALS, index);

		if (!rc_source(record, context->sections[
				RUNE_COMPACT_READ_SECTION_FACETS].count) ||
			!rc_ref(rc_u32(record + 20U), context->sections[
				RUNE_COMPACT_READ_SECTION_FACETS].count, 0) ||
			!rc_ref(rc_u32(record + 24U), context->sections[
				RUNE_COMPACT_READ_SECTION_INCIDENCES].count, 0) ||
			!rc_ref(rc_u32(record + 28U), context->sections[
				RUNE_COMPACT_READ_SECTION_INCIDENCES].count, 0))
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_REFERENCE,
				RUNE_COMPACT_READ_SECTION_PORTALS, index);
		if (rc_u32(record + 36U) >= 3U ||
			(record[40] & (unsigned char)~UINT8_C(3)) != 0U)
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_VALUE,
				RUNE_COMPACT_READ_SECTION_PORTALS, index);
		if (!rc_zero(record + 41U, 3U))
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_NONZERO_RESERVED,
				RUNE_COMPACT_READ_SECTION_PORTALS, index);
	}
	return 1;
}

static int rc_validate_movement_fields(const rc_context_t *context,
	rune_compact_read_error_t *error)
{
	const rc_section_t *section = &context->sections[
		RUNE_COMPACT_READ_SECTION_MOVEMENT_FIELDS];
	uint32_t index;

	for (index = 0U; index < section->count; ++index)
	{
		const unsigned char *record = rc_record(context,
			RUNE_COMPACT_READ_SECTION_MOVEMENT_FIELDS, index);

		if (!rc_ref(rc_u32(record), context->sections[
				RUNE_COMPACT_READ_SECTION_CELLS].count, 0) ||
			!rc_ref(rc_u32(record + 4U), context->sections[
				RUNE_COMPACT_READ_SECTION_PORTALS].count, 1))
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_REFERENCE,
				RUNE_COMPACT_READ_SECTION_MOVEMENT_FIELDS, index);
		if (rc_u32(record + 8U) >= 6U ||
			(record[12] & (unsigned char)~UINT8_C(3)) != 0U)
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_VALUE,
				RUNE_COMPACT_READ_SECTION_MOVEMENT_FIELDS, index);
		if (!rc_zero(record + 13U, 3U))
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_NONZERO_RESERVED,
				RUNE_COMPACT_READ_SECTION_MOVEMENT_FIELDS, index);
		if (!rc_span(record + 16U, context->sections[
				RUNE_COMPACT_READ_SECTION_ANALYTIC_FUNCTION_REFS].count,
				0U, UINT32_MAX))
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_SPAN,
				RUNE_COMPACT_READ_SECTION_MOVEMENT_FIELDS, index);
	}
	return 1;
}

static int rc_validate_weapon(const rc_context_t *context,
	rune_compact_read_error_t *error)
{
	const rc_section_t *regions = &context->sections[
		RUNE_COMPACT_READ_SECTION_WEAPON_REGIONS];
	const rc_section_t *profiles = &context->sections[
		RUNE_COMPACT_READ_SECTION_WEAPON_PROFILES];
	const rc_section_t *kernels = &context->sections[
		RUNE_COMPACT_READ_SECTION_WEAPON_KERNELS];
	uint32_t index;

	for (index = 0U; index < regions->count; ++index)
	{
		const unsigned char *record = rc_record(context,
			RUNE_COMPACT_READ_SECTION_WEAPON_REGIONS, index);

		if (!rc_ref(rc_u32(record), context->sections[
				RUNE_COMPACT_READ_SECTION_CELLS].count, 0))
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_REFERENCE,
				RUNE_COMPACT_READ_SECTION_WEAPON_REGIONS, index);
		if (!rc_span(record + 4U, context->sections[
				RUNE_COMPACT_READ_SECTION_CELL_INCIDENCES].count,
				0U, UINT32_MAX) ||
			!rc_span(record + 12U, kernels->count, 0U, UINT32_MAX))
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_SPAN,
				RUNE_COMPACT_READ_SECTION_WEAPON_REGIONS, index);
	}
	for (index = 0U; index < profiles->count; ++index)
	{
		const unsigned char *record = rc_record(context,
			RUNE_COMPACT_READ_SECTION_WEAPON_PROFILES, index);
		uint32_t response_families = rc_u32(record + 4U);

		if (rc_u32(record) == 0U || response_families == 0U ||
			(response_families & ~RC_WEAPON_RESPONSE_FAMILIES_ALL) != 0U)
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_VALUE,
				RUNE_COMPACT_READ_SECTION_WEAPON_PROFILES, index);
	}
	for (index = 0U; index < kernels->count; ++index)
	{
		const unsigned char *record = rc_record(context,
			RUNE_COMPACT_READ_SECTION_WEAPON_KERNELS, index);
		uint32_t profile = rc_u32(record + 4U);
		uint32_t family = rc_u32(record + 8U);
		const unsigned char *profile_record;

		if (!rc_ref(rc_u32(record), regions->count, 0) ||
			!rc_ref(profile, profiles->count, 0))
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_REFERENCE,
				RUNE_COMPACT_READ_SECTION_WEAPON_KERNELS, index);
		if (family >= RC_WEAPON_RESPONSE_FAMILY_COUNT)
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_VALUE,
				RUNE_COMPACT_READ_SECTION_WEAPON_KERNELS, index);
		profile_record = rc_record(context,
			RUNE_COMPACT_READ_SECTION_WEAPON_PROFILES, profile);
		if ((rc_u32(profile_record + 4U) & (UINT32_C(1) << family)) == 0U)
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_REFERENCE,
				RUNE_COMPACT_READ_SECTION_WEAPON_KERNELS, index);
		if (!rc_span(record + 12U, context->sections[
				RUNE_COMPACT_READ_SECTION_ANALYTIC_FUNCTION_REFS].count,
				0U, UINT32_MAX))
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_SPAN,
				RUNE_COMPACT_READ_SECTION_WEAPON_KERNELS, index);
	}
	return 1;
}

static int rc_validate_analytic(const rc_context_t *context,
	rune_compact_read_error_t *error)
{
	uint32_t index;

	for (index = 0U; index < context->sections[
		RUNE_COMPACT_READ_SECTION_ANALYTIC_FUNCTION_REFS].count; ++index)
		if (!rc_ref(rc_u32(rc_record(context,
				RUNE_COMPACT_READ_SECTION_ANALYTIC_FUNCTION_REFS, index)),
				context->sections[
					RUNE_COMPACT_READ_SECTION_ANALYTIC_FUNCTIONS].count, 0))
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_REFERENCE,
				RUNE_COMPACT_READ_SECTION_ANALYTIC_FUNCTION_REFS, index);

	for (index = 0U; index < context->sections[
		RUNE_COMPACT_READ_SECTION_ANALYTIC_FUNCTIONS].count; ++index)
	{
		const unsigned char *record = rc_record(context,
			RUNE_COMPACT_READ_SECTION_ANALYTIC_FUNCTIONS, index);
		uint32_t form = rc_u32(record + 16U);
		uint32_t definition_limit;

		if (!rc_span(record, context->sections[
				RUNE_COMPACT_READ_SECTION_ANALYTIC_INPUT_DIMENSIONS].count,
				0U, 16U))
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_SPAN,
				RUNE_COMPACT_READ_SECTION_ANALYTIC_FUNCTIONS, index);
		if (rc_u32(record + 12U) >= 20U || form >= 5U)
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_VALUE,
				RUNE_COMPACT_READ_SECTION_ANALYTIC_FUNCTIONS, index);
		switch (form)
		{
		case 0U:
			definition_limit = context->sections[
				RUNE_COMPACT_READ_SECTION_ANALYTIC_CONSTANTS].count;
			break;
		case 1U:
			definition_limit = context->sections[
				RUNE_COMPACT_READ_SECTION_ANALYTIC_AFFINES].count;
			break;
		case 2U:
			definition_limit = context->sections[
				RUNE_COMPACT_READ_SECTION_ANALYTIC_POLYNOMIALS].count;
			break;
		case 3U:
			definition_limit = context->sections[
				RUNE_COMPACT_READ_SECTION_ANALYTIC_BALLISTICS].count;
			break;
		default:
			definition_limit = context->sections[
				RUNE_COMPACT_READ_SECTION_ANALYTIC_PIECEWISE].count;
			break;
		}
		if (!rc_ref(rc_u32(record + 8U), definition_limit, 0))
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_REFERENCE,
				RUNE_COMPACT_READ_SECTION_ANALYTIC_FUNCTIONS, index);
	}

	for (index = 0U; index < context->sections[
		RUNE_COMPACT_READ_SECTION_ANALYTIC_INPUT_DIMENSIONS].count; ++index)
		if (rc_u32(rc_record(context,
				RUNE_COMPACT_READ_SECTION_ANALYTIC_INPUT_DIMENSIONS, index)) >= 16U)
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_VALUE,
				RUNE_COMPACT_READ_SECTION_ANALYTIC_INPUT_DIMENSIONS, index);

	for (index = 0U; index < context->sections[
		RUNE_COMPACT_READ_SECTION_ANALYTIC_AFFINES].count; ++index)
		if (!rc_span(rc_record(context,
				RUNE_COMPACT_READ_SECTION_ANALYTIC_AFFINES, index) + 4U,
				context->sections[
					RUNE_COMPACT_READ_SECTION_ANALYTIC_AFFINE_SLOPES].count,
				0U, UINT32_MAX))
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_SPAN,
				RUNE_COMPACT_READ_SECTION_ANALYTIC_AFFINES, index);

	for (index = 0U; index < context->sections[
		RUNE_COMPACT_READ_SECTION_ANALYTIC_POLYNOMIALS].count; ++index)
	{
		const unsigned char *record = rc_record(context,
			RUNE_COMPACT_READ_SECTION_ANALYTIC_POLYNOMIALS, index);

		if (!rc_span(record, context->sections[
				RUNE_COMPACT_READ_SECTION_ANALYTIC_POLYNOMIAL_COEFFICIENTS].count,
				0U, UINT32_MAX))
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_SPAN,
				RUNE_COMPACT_READ_SECTION_ANALYTIC_POLYNOMIALS, index);
		if (!rc_zero(record + 9U, 3U))
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_NONZERO_RESERVED,
				RUNE_COMPACT_READ_SECTION_ANALYTIC_POLYNOMIALS, index);
	}

	for (index = 0U; index < context->sections[
		RUNE_COMPACT_READ_SECTION_ANALYTIC_PIECEWISE].count; ++index)
	{
		const unsigned char *record = rc_record(context,
			RUNE_COMPACT_READ_SECTION_ANALYTIC_PIECEWISE, index);

		if (!rc_span(record, context->sections[
				RUNE_COMPACT_READ_SECTION_ANALYTIC_PIECEWISE_CLAUSES].count,
				0U, UINT32_MAX))
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_SPAN,
				RUNE_COMPACT_READ_SECTION_ANALYTIC_PIECEWISE, index);
		if (!rc_ref(rc_u32(record + 8U), context->sections[
				RUNE_COMPACT_READ_SECTION_ANALYTIC_FUNCTIONS].count, 0))
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_REFERENCE,
				RUNE_COMPACT_READ_SECTION_ANALYTIC_PIECEWISE, index);
	}

	for (index = 0U; index < context->sections[
		RUNE_COMPACT_READ_SECTION_ANALYTIC_PIECEWISE_CLAUSES].count; ++index)
	{
		const unsigned char *record = rc_record(context,
			RUNE_COMPACT_READ_SECTION_ANALYTIC_PIECEWISE_CLAUSES, index);

		if (!rc_ref(rc_u32(record + 8U), context->sections[
				RUNE_COMPACT_READ_SECTION_ANALYTIC_FUNCTIONS].count, 0))
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_REFERENCE,
				RUNE_COMPACT_READ_SECTION_ANALYTIC_PIECEWISE_CLAUSES, index);
		if (rc_u32(record + 12U) >= 4U)
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_VALUE,
				RUNE_COMPACT_READ_SECTION_ANALYTIC_PIECEWISE_CLAUSES, index);
	}
	return 1;
}

static int rc_validate_static(const rc_context_t *context,
	rune_compact_read_error_t *error)
{
	const rc_section_t *mechanisms = &context->sections[
		RUNE_COMPACT_READ_SECTION_MECHANISMS];
	const rc_section_t *edges = &context->sections[
		RUNE_COMPACT_READ_SECTION_MECHANISM_EDGES];
	const rc_section_t *landmarks = &context->sections[
		RUNE_COMPACT_READ_SECTION_LANDMARKS];
	const rc_section_t *landmark_cells = &context->sections[
		RUNE_COMPACT_READ_SECTION_LANDMARK_CELLS];
	uint32_t index;

	for (index = 0U; index < mechanisms->count; ++index)
	{
		const unsigned char *record = rc_record(context,
			RUNE_COMPACT_READ_SECTION_MECHANISMS, index);

		if (!rc_ref(rc_u32(record + 8U), context->sections[
				RUNE_COMPACT_READ_SECTION_CELLS].count, 0) ||
			!rc_ref(rc_u32(record + 12U), context->sections[
				RUNE_COMPACT_READ_SECTION_CELLS].count, 0) ||
			!rc_ref(rc_u32(record + 16U), landmarks->count, 1))
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_REFERENCE,
				RUNE_COMPACT_READ_SECTION_MECHANISMS, index);
		if (!rc_span(record + 44U, edges->count, 0U, UINT32_MAX))
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_SPAN,
				RUNE_COMPACT_READ_SECTION_MECHANISMS, index);
		if (rc_u32(record + 72U) >= 8U || rc_u32(record + 76U) >= 5U ||
			rc_u32(record + 80U) >= 5U || rc_u32(record + 84U) >= 5U ||
			rc_u32(record + 88U) >= 5U || rc_u32(record + 92U) >= 3U ||
			(record[96] & (unsigned char)~UINT8_C(3)) != 0U)
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_VALUE,
				RUNE_COMPACT_READ_SECTION_MECHANISMS, index);
		if (!rc_zero(record + 97U, 3U))
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_NONZERO_RESERVED,
				RUNE_COMPACT_READ_SECTION_MECHANISMS, index);
	}

	for (index = 0U; index < edges->count; ++index)
		if (rc_u32(rc_record(context,
				RUNE_COMPACT_READ_SECTION_MECHANISM_EDGES, index) + 12U) >= 5U)
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_VALUE,
				RUNE_COMPACT_READ_SECTION_MECHANISM_EDGES, index);

	for (index = 0U; index < landmarks->count; ++index)
	{
		const unsigned char *record = rc_record(context,
			RUNE_COMPACT_READ_SECTION_LANDMARKS, index);

		if (!rc_span(record + 4U, landmark_cells->count, 0U, UINT32_MAX))
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_SPAN,
				RUNE_COMPACT_READ_SECTION_LANDMARKS, index);
		if (!rc_ref(rc_u32(record + 12U), mechanisms->count, 1))
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_REFERENCE,
				RUNE_COMPACT_READ_SECTION_LANDMARKS, index);
		if (rc_u32(record + 52U) >= 13U)
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_VALUE,
				RUNE_COMPACT_READ_SECTION_LANDMARKS, index);
		if (rc_u16(record + 58U) != 0U)
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_NONZERO_RESERVED,
				RUNE_COMPACT_READ_SECTION_LANDMARKS, index);
	}

	for (index = 0U; index < landmark_cells->count; ++index)
		if (!rc_ref(rc_u32(rc_record(context,
				RUNE_COMPACT_READ_SECTION_LANDMARK_CELLS, index)),
				context->sections[RUNE_COMPACT_READ_SECTION_CELLS].count, 0))
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_REFERENCE,
				RUNE_COMPACT_READ_SECTION_LANDMARK_CELLS, index);

	for (index = 0U; index < context->sections[
		RUNE_COMPACT_READ_SECTION_FACET_ANNOTATIONS].count; ++index)
	{
		const unsigned char *record = rc_record(context,
			RUNE_COMPACT_READ_SECTION_FACET_ANNOTATIONS, index);

		if (!rc_ref(rc_u32(record), context->sections[
				RUNE_COMPACT_READ_SECTION_FACETS].count, 0))
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_REFERENCE,
				RUNE_COMPACT_READ_SECTION_FACET_ANNOTATIONS, index);
		if (record[7] != 0U)
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_NONZERO_RESERVED,
				RUNE_COMPACT_READ_SECTION_FACET_ANNOTATIONS, index);
		if ((rc_u16(record + 4U) & (uint16_t)~UINT16_C(0x00ff)) != 0U ||
			(record[6] & (unsigned char)~UINT8_C(3)) != 0U)
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_VALUE,
				RUNE_COMPACT_READ_SECTION_FACET_ANNOTATIONS, index);
	}

	for (index = 0U; index < context->sections[
		RUNE_COMPACT_READ_SECTION_PORTAL_MECHANISMS].count; ++index)
	{
		const unsigned char *record = rc_record(context,
			RUNE_COMPACT_READ_SECTION_PORTAL_MECHANISMS, index);

		if (!rc_ref(rc_u32(record), context->sections[
				RUNE_COMPACT_READ_SECTION_PORTALS].count, 0) ||
			!rc_ref(rc_u32(record + 4U), mechanisms->count, 0))
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_REFERENCE,
				RUNE_COMPACT_READ_SECTION_PORTAL_MECHANISMS, index);
		if (rc_u32(record + 8U) >= 4U)
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_INVALID_VALUE,
				RUNE_COMPACT_READ_SECTION_PORTAL_MECHANISMS, index);
		if (!rc_zero(record + 12U, 3U))
			return rc_fail(error, RUNE_COMPACT_READ_ERROR_NONZERO_RESERVED,
				RUNE_COMPACT_READ_SECTION_PORTAL_MECHANISMS, index);
	}
	return 1;
}

int RuneCompactReadInspectBound(const void *image, size_t image_size,
	const void *expected_identity, size_t expected_identity_size,
	rune_compact_read_info_t *info_out, rune_compact_read_error_t *error_out)
{
	rc_context_t context;

	rc_clear_error(error_out);
	if (image == NULL || (expected_identity_size != 0U &&
		(expected_identity == NULL || expected_identity_size !=
			(size_t)RUNE_COMPACT_READ_IDENTITY_BYTES)))
		return rc_fail(error_out, RUNE_COMPACT_READ_ERROR_INVALID_ARGUMENT,
			RC_NONE, RC_NONE);
	memset(&context, 0, sizeof(context));
	context.image = (const unsigned char *)image;
	context.image_size = image_size;
	if (!rc_parse_header(&context, error_out) ||
		!rc_validate_cells(&context, error_out) ||
		!rc_validate_facets(&context, error_out) ||
		!rc_validate_incidences(&context, error_out) ||
		!rc_validate_cell_incidences(&context, error_out) ||
		!rc_validate_portals(&context, error_out) ||
		!rc_validate_movement_fields(&context, error_out) ||
		!rc_validate_weapon(&context, error_out) ||
		!rc_validate_analytic(&context, error_out) ||
		!rc_validate_static(&context, error_out))
		return 0;
	if (expected_identity_size != 0U && memcmp(expected_identity,
		rc_record(&context, RUNE_COMPACT_READ_SECTION_IDENTITY, 0U),
		expected_identity_size) != 0)
		return rc_fail(error_out, RUNE_COMPACT_READ_ERROR_IDENTITY_MISMATCH,
			RUNE_COMPACT_READ_SECTION_IDENTITY, 0U);
	if (info_out != NULL)
	{
		uint32_t section;
		const unsigned char *identity = rc_record(&context,
			RUNE_COMPACT_READ_SECTION_IDENTITY, 0U);

		memset(info_out, 0, sizeof(*info_out));
		info_out->wire_version = rc_u16(context.image + 8U);
		info_out->model_version = rc_u16(context.image + 32U);
		info_out->analytic_version = rc_u16(context.image + 40U);
		info_out->schema_tag = rc_u32(context.image + 36U);
		info_out->image_bytes = rc_u64(context.image + 16U);
		info_out->checksum = rc_u32(context.image + RC_CHECKSUM_OFFSET);
		memcpy(info_out->bsp_sha256, identity,
			RUNE_COMPACT_READ_BSP_SHA256_BYTES);
		for (section = 0U; section < RUNE_COMPACT_READ_SECTION_COUNT; ++section)
			info_out->counts[section] = context.sections[section].count;
	}
	return 1;
}

int RuneCompactReadInspect(const void *image, size_t image_size,
	rune_compact_read_info_t *info_out, rune_compact_read_error_t *error_out)
{
	return RuneCompactReadInspectBound(image, image_size, NULL, 0U,
		info_out, error_out);
}

const char *RuneCompactReadSectionName(rune_compact_read_section_t section)
{
	return (uint32_t)section < RUNE_COMPACT_READ_SECTION_COUNT ?
		rc_specs[(uint32_t)section].name : "header";
}

const char *RuneCompactReadErrorString(rune_compact_read_error_code_t code)
{
	static const char *const names[RUNE_COMPACT_READ_ERROR_CODE_COUNT] = {
		"none", "invalid argument", "unsupported version", "truncated",
		"invalid format", "limit exceeded", "overflow", "checksum mismatch",
		"nonzero reserved field", "invalid section", "invalid span",
		"invalid reference", "invalid order", "invalid value",
		"identity mismatch"
	};

	return (uint32_t)code < RUNE_COMPACT_READ_ERROR_CODE_COUNT ?
		names[(uint32_t)code] : "unknown error";
}

#ifndef RUNE_COMPACT_READ_NO_MAIN

#ifndef _WIN32
typedef off_t rc_file_offset_t;
#else
typedef int64_t rc_file_offset_t;
#endif

static int rc_file_seek(FILE *file, rc_file_offset_t offset, int origin)
{
#ifndef _WIN32
	return fseeko(file, offset, origin);
#else
	return _fseeki64(file, offset, origin);
#endif
}

static rc_file_offset_t rc_file_tell(FILE *file)
{
#ifndef _WIN32
	return ftello(file);
#else
	return _ftelli64(file);
#endif
}

static int rc_read_file(const char *path, unsigned char **bytes_out,
	size_t *size_out)
{
	FILE *file;
	rc_file_offset_t length;
	uintmax_t unsigned_length;
	unsigned char *bytes;
	size_t size;

	file = fopen(path, "rb");
	if (file == NULL)
		return 0;
	if (rc_file_seek(file, (rc_file_offset_t)0, SEEK_END) != 0 ||
		(length = rc_file_tell(file)) < (rc_file_offset_t)0)
	{
		(void)fclose(file);
		return 0;
	}
	unsigned_length = (uintmax_t)length;
	if (unsigned_length > RC_MAX_IMAGE_BYTES || unsigned_length > SIZE_MAX ||
		rc_file_seek(file, (rc_file_offset_t)0, SEEK_SET) != 0)
	{
		(void)fclose(file);
		return 0;
	}
	size = (size_t)unsigned_length;
	bytes = size != 0U ? (unsigned char *)malloc(size) : NULL;
	if (size != 0U && bytes == NULL)
	{
		(void)fclose(file);
		return 0;
	}
	if (size != 0U && fread(bytes, 1U, size, file) != size)
	{
		(void)fclose(file);
		free(bytes);
		return 0;
	}
	if (fclose(file) != 0)
	{
		free(bytes);
		return 0;
	}
	*bytes_out = bytes;
	*size_out = size;
	return 1;
}

static int rc_hex_digit(int character, unsigned char *value)
{
	if (character >= '0' && character <= '9')
		*value = (unsigned char)(character - '0');
	else if (character >= 'a' && character <= 'f')
		*value = (unsigned char)(character - 'a' + 10);
	else if (character >= 'A' && character <= 'F')
		*value = (unsigned char)(character - 'A' + 10);
	else
		return 0;
	return 1;
}

static int rc_parse_sha256(const char *text,
	unsigned char output[RUNE_COMPACT_READ_BSP_SHA256_BYTES])
{
	uint32_t index;

	if (strlen(text) != RUNE_COMPACT_READ_BSP_SHA256_BYTES * 2U)
		return 0;
	for (index = 0U; index < RUNE_COMPACT_READ_BSP_SHA256_BYTES; ++index)
	{
		unsigned char high;
		unsigned char low;

		if (!rc_hex_digit(text[index * 2U], &high) ||
			!rc_hex_digit(text[index * 2U + 1U], &low))
			return 0;
		output[index] = (unsigned char)(high * 16U + low);
	}
	return 1;
}

static void rc_print_hex(const unsigned char *bytes, uint32_t count)
{
	uint32_t index;

	for (index = 0U; index < count; ++index)
		(void)printf("%02x", bytes[index]);
}

static void rc_print_summary(const rune_compact_read_info_t *info)
{
	const uint32_t *c = info->counts;

	(void)printf(
		"{\"analytic_affine_slopes\":%" PRIu32
		",\"analytic_affines\":%" PRIu32
		",\"analytic_ballistics\":%" PRIu32
		",\"analytic_constants\":%" PRIu32
		",\"analytic_functions\":%" PRIu32
		",\"analytic_input_dimensions\":%" PRIu32
		",\"analytic_piecewise\":%" PRIu32
		",\"analytic_piecewise_clauses\":%" PRIu32
		",\"analytic_polynomial_coefficients\":%" PRIu32
		",\"analytic_polynomials\":%" PRIu32
		",\"analytic_function_refs\":%" PRIu32
		",\"bsp_sha256\":\"",
		c[RUNE_COMPACT_READ_SECTION_ANALYTIC_AFFINE_SLOPES],
		c[RUNE_COMPACT_READ_SECTION_ANALYTIC_AFFINES],
		c[RUNE_COMPACT_READ_SECTION_ANALYTIC_BALLISTICS],
		c[RUNE_COMPACT_READ_SECTION_ANALYTIC_CONSTANTS],
		c[RUNE_COMPACT_READ_SECTION_ANALYTIC_FUNCTIONS],
		c[RUNE_COMPACT_READ_SECTION_ANALYTIC_INPUT_DIMENSIONS],
		c[RUNE_COMPACT_READ_SECTION_ANALYTIC_PIECEWISE],
		c[RUNE_COMPACT_READ_SECTION_ANALYTIC_PIECEWISE_CLAUSES],
		c[RUNE_COMPACT_READ_SECTION_ANALYTIC_POLYNOMIAL_COEFFICIENTS],
		c[RUNE_COMPACT_READ_SECTION_ANALYTIC_POLYNOMIALS],
		c[RUNE_COMPACT_READ_SECTION_ANALYTIC_FUNCTION_REFS]);
	rc_print_hex(info->bsp_sha256, RUNE_COMPACT_READ_BSP_SHA256_BYTES);
	(void)printf(
		"\",\"cell_incidences\":%" PRIu32
		",\"cells\":%" PRIu32
		",\"checksum\":%" PRIu32
		",\"facets\":%" PRIu32
		",\"facet_annotations\":%" PRIu32
		",\"image_bytes\":%" PRIu64
		",\"incidences\":%" PRIu32
		",\"landmark_cells\":%" PRIu32
		",\"landmarks\":%" PRIu32
		",\"mechanism_edges\":%" PRIu32
		",\"mechanisms\":%" PRIu32
		",\"model_version\":%" PRIu16
		",\"movement_fields\":%" PRIu32
		",\"portals\":%" PRIu32
		",\"portal_mechanisms\":%" PRIu32
		",\"schema_tag\":%" PRIu32
		",\"vertices\":%" PRIu32
		",\"weapon_kernels\":%" PRIu32
		",\"weapon_profiles\":%" PRIu32
		",\"weapon_regions\":%" PRIu32
		",\"wire_version\":%" PRIu16 "}\n",
		c[RUNE_COMPACT_READ_SECTION_CELL_INCIDENCES],
		c[RUNE_COMPACT_READ_SECTION_CELLS], info->checksum,
		c[RUNE_COMPACT_READ_SECTION_FACETS],
		c[RUNE_COMPACT_READ_SECTION_FACET_ANNOTATIONS], info->image_bytes,
		c[RUNE_COMPACT_READ_SECTION_INCIDENCES],
		c[RUNE_COMPACT_READ_SECTION_LANDMARK_CELLS],
		c[RUNE_COMPACT_READ_SECTION_LANDMARKS],
		c[RUNE_COMPACT_READ_SECTION_MECHANISM_EDGES],
		c[RUNE_COMPACT_READ_SECTION_MECHANISMS], info->model_version,
		c[RUNE_COMPACT_READ_SECTION_MOVEMENT_FIELDS],
		c[RUNE_COMPACT_READ_SECTION_PORTALS],
		c[RUNE_COMPACT_READ_SECTION_PORTAL_MECHANISMS], info->schema_tag,
		c[RUNE_COMPACT_READ_SECTION_VERTICES],
		c[RUNE_COMPACT_READ_SECTION_WEAPON_KERNELS],
		c[RUNE_COMPACT_READ_SECTION_WEAPON_PROFILES],
		c[RUNE_COMPACT_READ_SECTION_WEAPON_REGIONS], info->wire_version);
}

int main(int argc, char **argv)
{
	const char *path = NULL;
	const char *identity_path = NULL;
	unsigned char expected_bsp[RUNE_COMPACT_READ_BSP_SHA256_BYTES];
	unsigned char *expected_identity = NULL;
	unsigned char *image = NULL;
	size_t expected_identity_size = 0U;
	size_t image_size = 0U;
	rune_compact_read_info_t info;
	rune_compact_read_error_t error;
	int have_expected_bsp = 0;
	int argument;

	for (argument = 1; argument < argc; ++argument)
	{
		if (strcmp(argv[argument], "--expected-bsp-sha256") == 0 &&
			argument + 1 < argc)
		{
			if (!rc_parse_sha256(argv[++argument], expected_bsp))
			{
				(void)fprintf(stderr, "runecompactread: invalid BSP SHA-256\n");
				return 2;
			}
			have_expected_bsp = 1;
		}
		else if (strcmp(argv[argument], "--expected-identity-file") == 0 &&
			argument + 1 < argc)
			identity_path = argv[++argument];
		else if (argv[argument][0] == '-')
		{
			(void)fprintf(stderr,
				"usage: runecompactread [--expected-bsp-sha256 HEX] "
				"[--expected-identity-file FILE] FILE\n");
			return 2;
		}
		else if (path == NULL)
			path = argv[argument];
		else
		{
			(void)fprintf(stderr,
				"usage: runecompactread [--expected-bsp-sha256 HEX] "
				"[--expected-identity-file FILE] FILE\n");
			return 2;
		}
	}
	if (path == NULL || !rc_read_file(path, &image, &image_size))
	{
		(void)fprintf(stderr, "runecompactread: cannot read image\n");
		return 2;
	}
	if (identity_path != NULL)
	{
		if (!rc_read_file(identity_path, &expected_identity,
			&expected_identity_size) || expected_identity_size !=
			(size_t)RUNE_COMPACT_READ_IDENTITY_BYTES)
		{
			(void)fprintf(stderr,
				"runecompactread: expected identity must be 252 bytes\n");
			free(expected_identity);
			free(image);
			return 2;
		}
	}
	if (!RuneCompactReadInspectBound(image, image_size, expected_identity,
		expected_identity_size, &info, &error) ||
		(have_expected_bsp && memcmp(info.bsp_sha256, expected_bsp,
			RUNE_COMPACT_READ_BSP_SHA256_BYTES) != 0))
	{
		if (have_expected_bsp && error.code == RUNE_COMPACT_READ_ERROR_NONE)
			rc_set_error(&error, RUNE_COMPACT_READ_ERROR_IDENTITY_MISMATCH,
				RUNE_COMPACT_READ_SECTION_IDENTITY, 0U);
		(void)fprintf(stderr, "runecompactread: reject: %s section=%s record=%" PRIu32 "\n",
			RuneCompactReadErrorString(error.code),
			RuneCompactReadSectionName(error.section), error.record);
		free(expected_identity);
		free(image);
		return 1;
	}
	rc_print_summary(&info);
	free(expected_identity);
	free(image);
	return 0;
}

#endif
