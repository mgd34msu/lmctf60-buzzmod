#ifndef SG_RUNE_COMPACT_WIRE_H
#define SG_RUNE_COMPACT_WIRE_H

#include <stddef.h>
#include <stdint.h>

#include "sg_rune_compact_static.h"

#define SG_RUNE_COMPACT_WIRE_VERSION UINT16_C(12)
#define SG_RUNE_COMPACT_WIRE_MAX_IMAGE_BYTES UINT64_C(4294967296)

typedef enum sg_rune_compact_wire_section_e
{
	SG_RUNE_COMPACT_WIRE_SECTION_IDENTITY = 0,
	SG_RUNE_COMPACT_WIRE_SECTION_CELLS,
	SG_RUNE_COMPACT_WIRE_SECTION_FACETS,
	SG_RUNE_COMPACT_WIRE_SECTION_INCIDENCES,
	SG_RUNE_COMPACT_WIRE_SECTION_CELL_INCIDENCES,
	SG_RUNE_COMPACT_WIRE_SECTION_VERTICES,
	SG_RUNE_COMPACT_WIRE_SECTION_PORTALS,
	SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_CAPABILITIES,
	SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_STATES,
	SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBERS,
	SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_HOOK_TARGETS,
	SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_FIBER_FUNCTION_REFS,
	SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_ANGULAR_SCHEDULES,
	SG_RUNE_COMPACT_WIRE_SECTION_MOVEMENT_RUNTIME,
	/* One canonical spatial response projection, shared by weapons and
	 * movement.  No profile-specific region table exists on the wire. */
	SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FRAGMENTS,
	SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_HALFSPACES,
	SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_PATCHES,
	SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_TARGET_VERTICES,
	SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SPLITS,
	SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_FACTS,
	SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_CANDIDATE_GROUPS,
	SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SOURCE_ENDPOINT_GROUPS,
	SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SOURCE_ENDPOINT_MEMBERS,
	SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_TARGET_ENDPOINT_GROUPS,
	SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_TARGET_ENDPOINT_MEMBERS,
	SG_RUNE_COMPACT_WIRE_SECTION_RESPONSE_SEAL,
	SG_RUNE_COMPACT_WIRE_SECTION_STATIC_OCCLUDERS,
	SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_PROFILES,
	SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_KERNELS,
	SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_FUNCTION_REFS,
	SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_ATTACHMENTS,
	SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_RELATION_SPANS,
	SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_RELATION_REFS,
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
	SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_CONTROLLERS,
	SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_EDGES,
	SG_RUNE_COMPACT_WIRE_SECTION_TRANSITIONS,
	SG_RUNE_COMPACT_WIRE_SECTION_LANDMARKS,
	SG_RUNE_COMPACT_WIRE_SECTION_LANDMARK_CELLS,
	SG_RUNE_COMPACT_WIRE_SECTION_FACET_ANNOTATIONS,
	SG_RUNE_COMPACT_WIRE_SECTION_PORTAL_MECHANISMS,
	/* All-model BSP surface provenance; separate local/world vertex pool. */
	SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACES,
	SG_RUNE_COMPACT_WIRE_SECTION_SOURCE_SURFACE_VERTICES,
	SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITIES,
	SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_CONTROLLERS,
	SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TOPOLOGY_EDGES,
	SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TRANSITIONS,
	SG_RUNE_COMPACT_WIRE_SECTION_MECHANISM_AUTHORITY_TRANSITION_STATIC_INDICES,
	SG_RUNE_COMPACT_WIRE_SECTION_STATIC_TRANSITION_AUTHORITY_INDICES,
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

/* Largest image admitted by this schema's record-count limits. */
uint64_t SG_RuneCompactWireImageLimit(void);

/* Measures the one canonical image for model. The model, analytic, and static
 * objects and every nonempty array must remain valid for the duration of the
 * call. This performs wire-boundary checks, not complete model validation. */
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
const char *SG_RuneCompactWireSectionName(
	sg_rune_compact_wire_section_t section);

#endif
