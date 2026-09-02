/* Owned configuration-space to compact-geometry materialization. */
#ifndef SG_RUNE_COMPACT_GEOMETRY_H
#define SG_RUNE_COMPACT_GEOMETRY_H

#include <stddef.h>
#include <stdint.h>

#include "sg_rune_compact_builder.h"

/* Configuration construction is protocol-lattice based.  The checked-nearest
 * policy rounds finite construction output to the nearest Q8 coordinate and
 * then checks plane membership with the compact model's quantization bound. */
#define SG_RUNE_COMPACT_GEOMETRY_Q8_RESIDUE_LIMIT 0.5

typedef struct sg_rune_compact_geometry_s sg_rune_compact_geometry_t;

typedef void *(*sg_rune_compact_geometry_allocate_fn)(void *context,
	size_t bytes);
typedef void (*sg_rune_compact_geometry_release_fn)(void *context,
	void *allocation);

typedef struct sg_rune_compact_geometry_allocator_s
{
	void *context;
	sg_rune_compact_geometry_allocate_fn allocate;
	sg_rune_compact_geometry_release_fn release;
} sg_rune_compact_geometry_allocator_t;

typedef enum sg_rune_compact_geometry_error_code_e
{
	SG_RUNE_COMPACT_GEOMETRY_ERROR_NONE = 0,
	SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_ARGUMENT,
	SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_CONFIGURATION,
	SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_WORLD,
	SG_RUNE_COMPACT_GEOMETRY_ERROR_IDENTITY_MISMATCH,
	SG_RUNE_COMPACT_GEOMETRY_ERROR_NONFINITE_GEOMETRY,
	SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_GEOMETRY,
	SG_RUNE_COMPACT_GEOMETRY_ERROR_INVALID_REFERENCE,
	SG_RUNE_COMPACT_GEOMETRY_ERROR_UNSUPPORTED_TOPOLOGY,
	SG_RUNE_COMPACT_GEOMETRY_ERROR_Q8_CONVERSION,
	SG_RUNE_COMPACT_GEOMETRY_ERROR_OVERFLOW,
	SG_RUNE_COMPACT_GEOMETRY_ERROR_OUT_OF_MEMORY,
	SG_RUNE_COMPACT_GEOMETRY_ERROR_CODE_COUNT
} sg_rune_compact_geometry_error_code_t;

typedef enum sg_rune_compact_geometry_record_domain_e
{
	SG_RUNE_COMPACT_GEOMETRY_RECORD_RESULT = 0,
	SG_RUNE_COMPACT_GEOMETRY_RECORD_IDENTITY,
	SG_RUNE_COMPACT_GEOMETRY_RECORD_WORLD,
	SG_RUNE_COMPACT_GEOMETRY_RECORD_CELL,
	SG_RUNE_COMPACT_GEOMETRY_RECORD_FACE,
	SG_RUNE_COMPACT_GEOMETRY_RECORD_PORTAL,
	SG_RUNE_COMPACT_GEOMETRY_RECORD_SOURCE_SURFACE
} sg_rune_compact_geometry_record_domain_t;

typedef struct sg_rune_compact_geometry_error_s
{
	sg_rune_compact_geometry_error_code_t code;
	sg_rune_compact_geometry_record_domain_t domain;
	uint32_t record;
} sg_rune_compact_geometry_error_t;

typedef struct sg_rune_compact_geometry_cell_span_s
{
	uint32_t first;
	uint32_t count;
} sg_rune_compact_geometry_cell_span_t;

/* Every pointer in this view is borrowed from geometry and remains valid until
 * SG_RuneCompactGeometryDestroy.  The view intentionally contains only the
 * geometry portions of the eventual compact model; later construction stages
 * own every non-geometry section. */
typedef struct sg_rune_compact_geometry_view_s
{
	sg_rune_compact_identity_t identity;
	const sg_rune_compact_cell_t *cells;
	uint32_t cell_count;
	const sg_rune_compact_facet_t *facets;
	uint32_t facet_count;
	const sg_rune_compact_incidence_t *incidences;
	uint32_t incidence_count;
	const sg_rune_compact_incidence_index_t *cell_incidences;
	uint32_t cell_incidence_count;
	const sg_rune_q8_vec3_t *vertices;
	uint32_t vertex_count;
	const sg_rune_compact_portal_t *portals;
	uint32_t portal_count;
	/* Canonical all-model source inventory.  Its vertex array is separate from
	 * compact facet vertices because model-local surfaces are not static cell
	 * boundaries. */
	const sg_rune_compact_source_surface_t *source_surfaces;
	uint32_t source_surface_count;
	const sg_rune_q8_vec3_t *source_surface_vertices;
	uint32_t source_surface_vertex_count;
	/* A configuration cell can cover several compact cells after the standing /
	 * crouching overlay.  Spans index the flat owned reference array. */
	const sg_rune_compact_geometry_cell_span_t
		*compact_cells_for_configuration_cell;
	uint32_t compact_cells_for_configuration_cell_count;
	const sg_rune_compact_cell_index_t *configuration_cell_compact_cells;
	uint32_t configuration_cell_compact_cell_count;
} sg_rune_compact_geometry_view_t;

/* On success geometry_out receives one owner-private immutable result.  On
 * every failure geometry_out is untouched, which permits sentinel testing and
 * transactional caller code. */
int SG_RuneCompactGeometryMaterialize(
	const sg_rune_compact_builder_t *builder,
	const sg_rune_compact_geometry_allocator_t *allocator,
	sg_rune_compact_geometry_t **geometry_out,
	sg_rune_compact_geometry_error_t *error_out);

/* Geometry straight from a configuration space and its regions, without the
 * builder: what the builder path calls, and what the map driver calls. */
struct sg_bsp_world_s;
struct sg_configuration_space_s;
struct sg_configuration_semantics_s;
int SG_RuneCompactGeometryFromSpace(const struct sg_bsp_world_s *world,
	const struct sg_configuration_space_s *configuration,
	const struct sg_configuration_semantics_s *semantics,
	const sg_rune_compact_identity_t *identity,
	const sg_rune_compact_geometry_allocator_t *allocator,
	sg_rune_compact_geometry_t **geometry_out,
	sg_rune_compact_geometry_error_t *error_out);

int SG_RuneCompactGeometryRead(const sg_rune_compact_geometry_t *geometry,
	sg_rune_compact_geometry_view_t *view_out);
void SG_RuneCompactGeometryDestroy(sg_rune_compact_geometry_t *geometry);
const char *SG_RuneCompactGeometryErrorString(
	sg_rune_compact_geometry_error_code_t code);

#endif
