#ifndef RUNE_COMPACT_READ_H
#define RUNE_COMPACT_READ_H

/*
 * Standalone reader for the compact RUNE wire image.
 *
 * This interface has no dependency on the SLIPGATE codec or model headers.
 * A caller may use Inspect for structural/canonical checks, or pass the exact
 * identity record to InspectBound at the load boundary.  The reader does not
 * derive the BSP SHA-256.  It compares the identity record's supplied bytes.
 */

#include <stddef.h>
#include <stdint.h>

#define RUNE_COMPACT_READ_WIRE_VERSION UINT16_C(1)
#define RUNE_COMPACT_READ_MODEL_VERSION UINT16_C(1)
#define RUNE_COMPACT_READ_ANALYTIC_VERSION UINT16_C(1)
#define RUNE_COMPACT_READ_SCHEMA_TAG UINT32_C(0x4d434e52)

#define RUNE_COMPACT_READ_SECTION_COUNT UINT32_C(28)
#define RUNE_COMPACT_READ_IDENTITY_BYTES UINT32_C(252)
#define RUNE_COMPACT_READ_HEADER_FIXED_BYTES UINT32_C(48)
#define RUNE_COMPACT_READ_DESCRIPTOR_BYTES UINT32_C(24)
#define RUNE_COMPACT_READ_HEADER_BYTES UINT32_C(720)
#define RUNE_COMPACT_READ_BSP_SHA256_BYTES UINT32_C(32)

typedef enum rune_compact_read_section_e
{
	RUNE_COMPACT_READ_SECTION_IDENTITY = 0,
	RUNE_COMPACT_READ_SECTION_CELLS,
	RUNE_COMPACT_READ_SECTION_FACETS,
	RUNE_COMPACT_READ_SECTION_INCIDENCES,
	RUNE_COMPACT_READ_SECTION_CELL_INCIDENCES,
	RUNE_COMPACT_READ_SECTION_VERTICES,
	RUNE_COMPACT_READ_SECTION_PORTALS,
	RUNE_COMPACT_READ_SECTION_MOVEMENT_FIELDS,
	RUNE_COMPACT_READ_SECTION_WEAPON_REGIONS,
	RUNE_COMPACT_READ_SECTION_WEAPON_PROFILES,
	RUNE_COMPACT_READ_SECTION_WEAPON_KERNELS,
	RUNE_COMPACT_READ_SECTION_ANALYTIC_FUNCTION_REFS,
	RUNE_COMPACT_READ_SECTION_ANALYTIC_FUNCTIONS,
	RUNE_COMPACT_READ_SECTION_ANALYTIC_INPUT_DIMENSIONS,
	RUNE_COMPACT_READ_SECTION_ANALYTIC_CONSTANTS,
	RUNE_COMPACT_READ_SECTION_ANALYTIC_AFFINES,
	RUNE_COMPACT_READ_SECTION_ANALYTIC_AFFINE_SLOPES,
	RUNE_COMPACT_READ_SECTION_ANALYTIC_POLYNOMIALS,
	RUNE_COMPACT_READ_SECTION_ANALYTIC_POLYNOMIAL_COEFFICIENTS,
	RUNE_COMPACT_READ_SECTION_ANALYTIC_BALLISTICS,
	RUNE_COMPACT_READ_SECTION_ANALYTIC_PIECEWISE,
	RUNE_COMPACT_READ_SECTION_ANALYTIC_PIECEWISE_CLAUSES,
	RUNE_COMPACT_READ_SECTION_MECHANISMS,
	RUNE_COMPACT_READ_SECTION_MECHANISM_EDGES,
	RUNE_COMPACT_READ_SECTION_LANDMARKS,
	RUNE_COMPACT_READ_SECTION_LANDMARK_CELLS,
	RUNE_COMPACT_READ_SECTION_FACET_ANNOTATIONS,
	RUNE_COMPACT_READ_SECTION_PORTAL_MECHANISMS,
	RUNE_COMPACT_READ_SECTION_NONE = RUNE_COMPACT_READ_SECTION_COUNT
} rune_compact_read_section_t;

typedef enum rune_compact_read_error_code_e
{
	RUNE_COMPACT_READ_ERROR_NONE = 0,
	RUNE_COMPACT_READ_ERROR_INVALID_ARGUMENT,
	RUNE_COMPACT_READ_ERROR_UNSUPPORTED_VERSION,
	RUNE_COMPACT_READ_ERROR_TRUNCATED,
	RUNE_COMPACT_READ_ERROR_INVALID_FORMAT,
	RUNE_COMPACT_READ_ERROR_LIMIT_EXCEEDED,
	RUNE_COMPACT_READ_ERROR_OVERFLOW,
	RUNE_COMPACT_READ_ERROR_CHECKSUM_MISMATCH,
	RUNE_COMPACT_READ_ERROR_NONZERO_RESERVED,
	RUNE_COMPACT_READ_ERROR_INVALID_SECTION,
	RUNE_COMPACT_READ_ERROR_INVALID_SPAN,
	RUNE_COMPACT_READ_ERROR_INVALID_REFERENCE,
	RUNE_COMPACT_READ_ERROR_INVALID_ORDER,
	RUNE_COMPACT_READ_ERROR_INVALID_VALUE,
	RUNE_COMPACT_READ_ERROR_IDENTITY_MISMATCH,
	RUNE_COMPACT_READ_ERROR_CODE_COUNT
} rune_compact_read_error_code_t;

typedef struct rune_compact_read_error_s
{
	rune_compact_read_error_code_t code;
	rune_compact_read_section_t section;
	uint32_t record;
} rune_compact_read_error_t;

typedef struct rune_compact_read_info_s
{
	uint16_t wire_version;
	uint16_t model_version;
	uint16_t analytic_version;
	uint32_t schema_tag;
	uint64_t image_bytes;
	uint32_t checksum;
	uint8_t bsp_sha256[RUNE_COMPACT_READ_BSP_SHA256_BYTES];
	uint32_t counts[RUNE_COMPACT_READ_SECTION_COUNT];
} rune_compact_read_info_t;

/* Performs independent structural, canonical-layout and record checks. */
int RuneCompactReadInspect(const void *image, size_t image_size,
	rune_compact_read_info_t *info_out, rune_compact_read_error_t *error_out);

/* Performs the same checks and compares the complete canonical identity
 * record.  expected_identity may be NULL only when expected_identity_size is
 * zero.  A nonzero expected_identity_size must equal
 * RUNE_COMPACT_READ_IDENTITY_BYTES. */
int RuneCompactReadInspectBound(const void *image, size_t image_size,
	const void *expected_identity, size_t expected_identity_size,
	rune_compact_read_info_t *info_out, rune_compact_read_error_t *error_out);

const char *RuneCompactReadSectionName(rune_compact_read_section_t section);
const char *RuneCompactReadErrorString(rune_compact_read_error_code_t code);

#endif
