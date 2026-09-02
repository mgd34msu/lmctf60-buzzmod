/* Owned configuration-space to compact-geometry materialization. */
#ifndef SG_RUNE_CX_BUILD_H
#define SG_RUNE_CX_BUILD_H

#include <stddef.h>
#include <stdint.h>

#include "sg_rune_cx.h"

/* Configuration construction is protocol-lattice based.  The checked-nearest
 * policy rounds finite construction output to the nearest Q8 coordinate and
 * then checks plane membership with the compact model's quantization bound. */
#define SG_RUNE_CX_Q8_RESIDUE_LIMIT 0.5

typedef struct sg_rune_cx_s sg_rune_cx_t;

typedef void *(*sg_rune_cx_allocate_fn)(void *context,
	size_t bytes);
typedef void (*sg_rune_cx_release_fn)(void *context,
	void *allocation);

typedef struct sg_rune_cx_allocator_s
{
	void *context;
	sg_rune_cx_allocate_fn allocate;
	sg_rune_cx_release_fn release;
} sg_rune_cx_allocator_t;

typedef enum sg_rune_cx_error_code_e
{
	SG_RUNE_CX_ERROR_NONE = 0,
	SG_RUNE_CX_ERROR_INVALID_ARGUMENT,
	SG_RUNE_CX_ERROR_INVALID_CONFIGURATION,
	SG_RUNE_CX_ERROR_INVALID_WORLD,
	SG_RUNE_CX_ERROR_IDENTITY_MISMATCH,
	SG_RUNE_CX_ERROR_NONFINITE_GEOMETRY,
	SG_RUNE_CX_ERROR_INVALID_GEOMETRY,
	SG_RUNE_CX_ERROR_INVALID_REFERENCE,
	SG_RUNE_CX_ERROR_UNSUPPORTED_TOPOLOGY,
	SG_RUNE_CX_ERROR_Q8_CONVERSION,
	SG_RUNE_CX_ERROR_OVERFLOW,
	SG_RUNE_CX_ERROR_OUT_OF_MEMORY,
	SG_RUNE_CX_ERROR_CODE_COUNT
} sg_rune_cx_error_code_t;

typedef enum sg_rune_cx_record_domain_e
{
	SG_RUNE_CX_RECORD_RESULT = 0,
	SG_RUNE_CX_RECORD_IDENTITY,
	SG_RUNE_CX_RECORD_WORLD,
	SG_RUNE_CX_RECORD_CELL,
	SG_RUNE_CX_RECORD_FACE,
	SG_RUNE_CX_RECORD_PORTAL,
	SG_RUNE_CX_RECORD_SOURCE_SURFACE
} sg_rune_cx_record_domain_t;

typedef struct sg_rune_cx_error_s
{
	sg_rune_cx_error_code_t code;
	sg_rune_cx_record_domain_t domain;
	uint32_t record;
} sg_rune_cx_error_t;

/* Every pointer in this view is borrowed from geometry and remains valid until
 * SG_RuneCxDestroy.  The view intentionally contains only the
 * geometry portions of the eventual compact model; later construction stages
 * own every non-geometry section. */
/* On success geometry_out receives one owner-private immutable result.  On
 * every failure geometry_out is untouched, which permits sentinel testing and
 * transactional caller code. */
/* Geometry straight from a configuration space and its regions, without the
 * builder: what the builder path calls, and what the map driver calls. */
struct sg_bsp_world_s;
struct sg_configuration_space_s;
struct sg_configuration_semantics_s;
int SG_RuneCxFromSpace(const struct sg_bsp_world_s *world,
	const struct sg_configuration_space_s *configuration,
	const struct sg_configuration_semantics_s *semantics,
	const sg_rune_cx_allocator_t *allocator,
	sg_rune_cx_t **geometry_out,
	sg_rune_cx_error_t *error_out);

int SG_RuneCxRead(const sg_rune_cx_t *geometry,
	sg_rune_cx_view_t *view_out);
void SG_RuneCxDestroy(sg_rune_cx_t *geometry);
const char *SG_RuneCxErrorString(
	sg_rune_cx_error_code_t code);

#endif
