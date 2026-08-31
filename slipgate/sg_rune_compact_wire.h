#ifndef SG_RUNE_COMPACT_WIRE_H
#define SG_RUNE_COMPACT_WIRE_H

#include <stddef.h>
#include <stdint.h>

#include "sg_rune_compact_static.h"

#define SG_RUNE_COMPACT_WIRE_VERSION UINT16_C(1)

typedef enum sg_rune_compact_wire_section_e
{
	SG_RUNE_COMPACT_WIRE_SECTION_IDENTITY = 0,
	SG_RUNE_COMPACT_WIRE_SECTION_CELLS,
	SG_RUNE_COMPACT_WIRE_SECTION_FACETS,
	SG_RUNE_COMPACT_WIRE_SECTION_INCIDENCES,
	SG_RUNE_COMPACT_WIRE_SECTION_CELL_INCIDENCES,
	SG_RUNE_COMPACT_WIRE_SECTION_VERTICES,
	SG_RUNE_COMPACT_WIRE_SECTION_PORTALS,
	SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIELDS,
	SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_REGIONS,
	SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_PROFILES,
	SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_KERNELS,
	SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_FUNCTION_REFS,
	SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_FUNCTIONS,
	SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_INPUT_DIMENSIONS,
	SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_CONSTANTS,
	SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_AFFINES,
	SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_AFFINE_SLOPES,
	SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_POLYNOMIALS,
	SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_POLYNOMIAL_COEFFICIENTS,
	SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_BALLISTICS,
	SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_PIECEWISE,
	SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_PIECEWISE_CLAUSES,
	SG_RUNE_COMPACT_WIRE_SECTION_MECHANISMS,
	SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_EDGES,
	SG_RUNE_COMPACT_WIRE_SECTION_LANDMARKS,
	SG_RUNE_COMPACT_WIRE_SECTION_LANDMARK_CELLS,
	SG_RUNE_COMPACT_WIRE_SECTION_FACET_ANNOTATIONS,
	SG_RUNE_COMPACT_WIRE_SECTION_PORTAL_MECHANISMS,
	SG_RUNE_COMPACT_WIRE_SECTION_COUNT
} sg_rune_compact_wire_section_t;

typedef enum sg_rune_compact_wire_error_code_e
{
	SG_RUNE_COMPACT_WIRE_ERROR_NONE = 0,
	SG_RUNE_COMPACT_WIRE_ERROR_INVALID_ARGUMENT,
	SG_RUNE_COMPACT_WIRE_ERROR_UNSUPPORTED_VERSION,
	SG_RUNE_COMPACT_WIRE_ERROR_TRUNCATED,
	SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT,
	SG_RUNE_COMPACT_WIRE_ERROR_LIMIT_EXCEEDED,
	SG_RUNE_COMPACT_WIRE_ERROR_OVERFLOW,
	SG_RUNE_COMPACT_WIRE_ERROR_CHECKSUM_MISMATCH,
	SG_RUNE_COMPACT_WIRE_ERROR_NONZERO_RESERVED,
	SG_RUNE_COMPACT_WIRE_ERROR_INVALID_SECTION,
	SG_RUNE_COMPACT_WIRE_ERROR_INVALID_SPAN,
	SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE,
	SG_RUNE_COMPACT_WIRE_ERROR_OUT_OF_MEMORY,
	SG_RUNE_COMPACT_WIRE_ERROR_INVALID_MODEL,
	SG_RUNE_COMPACT_WIRE_ERROR_IDENTITY_MISMATCH,
	SG_RUNE_COMPACT_WIRE_ERROR_CODE_COUNT
} sg_rune_compact_wire_error_code_t;

typedef struct sg_rune_compact_wire_error_s
{
	sg_rune_compact_wire_error_code_t code;
	sg_rune_compact_wire_section_t section;
	uint32_t record;
	sg_rune_compact_error_t model_error;
} sg_rune_compact_wire_error_t;

typedef struct sg_rune_compact_wire_info_s
{
	uint16_t wire_version;
	uint16_t model_version;
	uint16_t analytic_version;
	uint32_t schema_tag;
	uint64_t image_bytes;
	uint32_t checksum;
	sg_rune_compact_identity_t identity;
	uint32_t counts[SG_RUNE_COMPACT_WIRE_SECTION_COUNT];
} sg_rune_compact_wire_info_t;

typedef struct sg_rune_compact_wire_decoded_s sg_rune_compact_wire_decoded_t;

/* Measures the one canonical image for model. The model, analytic, and static
 * objects and every nonempty array must remain valid for the duration of the
 * call. This performs wire-boundary checks, not semantic model proof. */
int SG_RuneCompactWireMeasure(const sg_rune_compact_model_t *model,
	size_t *size_out, sg_rune_compact_wire_error_t *error_out);

/* Writes a deterministic little-endian image. On failure, written_out is zero.
 * dest may be larger than the measured image; bytes beyond written_out are not
 * touched. */
int SG_RuneCompactWireEncode(const sg_rune_compact_model_t *model,
	void *dest, size_t dest_size, size_t *written_out,
	sg_rune_compact_wire_error_t *error_out);

/* Validates the complete image without allocation. It rejects trailing bytes,
 * noncanonical section placement, nonzero padding or reserved bytes, invalid
 * counts, spans and references, and checksum mismatch. */
int SG_RuneCompactWireInspect(const void *image, size_t image_size,
	sg_rune_compact_wire_info_t *info_out,
	sg_rune_compact_wire_error_t *error_out);

/* Decodes into one bounded owned allocation, validates the complete model, and
 * binds every identity field to expected_identity before ownership escapes.
 * Access is immutable through SG_RuneCompactWireModel. */
int SG_RuneCompactWireDecode(const void *image, size_t image_size,
	const sg_rune_compact_identity_t *expected_identity,
	sg_rune_compact_wire_decoded_t **decoded_out,
	sg_rune_compact_wire_error_t *error_out);
const sg_rune_compact_model_t *SG_RuneCompactWireModel(
	const sg_rune_compact_wire_decoded_t *decoded);
void SG_RuneCompactWireDestroy(sg_rune_compact_wire_decoded_t *decoded);

const char *SG_RuneCompactWireErrorString(
	sg_rune_compact_wire_error_code_t code);

#endif
