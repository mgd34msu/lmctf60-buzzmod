#ifndef SG_RUNE_COMPACT_STATIC_MATERIALIZER_H
#define SG_RUNE_COMPACT_STATIC_MATERIALIZER_H

/*
 * Materializes the static sections of a compact RUNE from already-authenticated
 * geometry and static source semantics.  The input is borrowed for the call;
 * the result owns every array exposed through its compact-static view.
 */

#include <stddef.h>
#include <stdint.h>

#include "sg_bsp_entity_semantics.h"
#include "sg_configuration_semantics.h"
#include "sg_rune_compact_geometry.h"
#include "sg_rune_compact_mechanisms.h"
#include "sg_rune_compact_static.h"
#include "sg_static_visibility.h"

typedef struct sg_rune_compact_static_geometry_view_s
{
	/* Copied from the owning compact geometry view.  The materializer never
	 * retains a caller-owned identity pointer. */
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
	/* Authenticated all-model BSP source roots.  A root has no compact cell;
	 * model-local roots are joined by exact source tuple and transformed by the
	 * mechanism/entity owner, never by this materializer. */
	const sg_rune_compact_source_surface_t *source_surfaces;
	uint32_t source_surface_count;
	const sg_rune_q8_vec3_t *source_surface_vertices;
	uint32_t source_surface_vertex_count;
	/* Optional explicit correspondence copied from the compact geometry
	 * materializer.  A configuration cell may cover several compact cells. */
	const sg_rune_compact_geometry_cell_span_t
		*compact_cells_for_configuration_cell;
	uint32_t compact_cells_for_configuration_cell_count;
	const sg_rune_compact_cell_index_t *configuration_cell_compact_cells;
	uint32_t configuration_cell_compact_cell_count;
} sg_rune_compact_static_geometry_view_t;

/* Entity references in the compact output use canonical_ordinal.  The source
 * declaration ordinal can contain gaps when host-inhibited entities are
 * omitted and is retained only by the input semantics. */
typedef struct sg_rune_compact_static_materializer_input_s
{
	sg_rune_compact_static_geometry_view_t geometry;
	const sg_bsp_entity_semantics_t *entities;
	const sg_configuration_semantics_t *configuration;
	const sg_static_visibility_t *visibility;
	/* Opaque identity-bound mechanism authority produced from the same builder
	 * and compact geometry.  A copied view is deliberately not accepted here:
	 * static materialization reads the authenticated owner at the boundary and
	 * never reconstructs controller, activation, or transition facts from raw
	 * entity guesses. */
	const sg_rune_compact_mechanisms_t *mechanisms;
} sg_rune_compact_static_materializer_input_t;

typedef enum sg_rune_compact_static_materializer_error_code_e
{
	SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_NONE = 0,
	SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_ARGUMENT,
	SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_IDENTITY_MISMATCH,
	SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SOURCE,
	SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_INVALID_SEMANTICS,
	SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_AMBIGUOUS_BINDING,
	SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_LIMIT_EXCEEDED,
	SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_OVERFLOW,
	SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_OUT_OF_MEMORY,
	SG_RUNE_COMPACT_STATIC_MATERIALIZER_ERROR_CODE_COUNT
} sg_rune_compact_static_materializer_error_code_t;

typedef enum sg_rune_compact_static_materializer_record_domain_e
{
	SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MODEL = 0,
	SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_ENTITY,
	SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_ENTITY_EDGE,
	SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_CONFIGURATION,
	SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_VISIBILITY,
	SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_MECHANISM,
	SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_LANDMARK,
	SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_FACET,
	SG_RUNE_COMPACT_STATIC_MATERIALIZER_RECORD_PORTAL
} sg_rune_compact_static_materializer_record_domain_t;

typedef struct sg_rune_compact_static_materializer_error_s
{
	sg_rune_compact_static_materializer_error_code_t code;
	sg_rune_compact_static_materializer_record_domain_t domain;
	uint32_t record;
} sg_rune_compact_static_materializer_error_t;

typedef struct sg_rune_compact_static_materializer_s
	sg_rune_compact_static_materializer_t;

/* Publishes only after every owned array and cross-reference has succeeded.
 * On failure, *materializer_out is left NULL. */
int SG_RuneCompactStaticMaterializerBuild(
	const sg_rune_compact_static_materializer_input_t *input,
	sg_rune_compact_static_materializer_t **materializer_out,
	sg_rune_compact_static_materializer_error_t *error_out);

int SG_RuneCompactStaticMaterializerRead(
	const sg_rune_compact_static_materializer_t *materializer,
	sg_rune_compact_static_t *static_out);

int SG_RuneCompactStaticMaterializerReadBound(
	const sg_rune_compact_static_materializer_t *materializer,
	sg_rune_compact_identity_t *identity_out,
	sg_rune_compact_static_t *static_out);

/* Return the canonical static-transition record that this materializer
 * projected from one authenticated mechanism-authority transition.  The
 * correspondence is construction-private provenance: it is never persisted
 * or accepted from a caller, because static mechanism numbering is allowed to
 * differ from authority numbering after canonical fanout and sorting. */
int SG_RuneCompactStaticMaterializerAuthorityTransitionStaticIndex(
	const sg_rune_compact_static_materializer_t *materializer,
	uint32_t authority_transition_index, uint32_t *static_transition_index_out);

/* Return the authenticated mechanism-authority record which produced one
 * canonical static mechanism.  Several canonical static mechanisms may map
 * to one authority when a mechanism has independently materialized fanout.
 * Like transition provenance, this construction-only relation is not wire
 * data and cannot be supplied by a caller. */
int SG_RuneCompactStaticMaterializerStaticMechanismAuthorityIndex(
	const sg_rune_compact_static_materializer_t *materializer,
	uint32_t static_mechanism_index, uint32_t *authority_mechanism_index_out);

void SG_RuneCompactStaticMaterializerDestroy(
	sg_rune_compact_static_materializer_t *materializer);

const char *SG_RuneCompactStaticMaterializerErrorString(
	sg_rune_compact_static_materializer_error_code_t code);

#if defined(SG_RUNE_COMPACT_STATIC_MATERIALIZER_TESTING)
/* Test-only allocation fault injection.  The counter is reset by setting the
 * failure ordinal to SIZE_MAX. */
void SG_RuneCompactStaticMaterializerTestFailAfter(size_t allocation);
size_t SG_RuneCompactStaticMaterializerTestAllocationCount(void);
#endif

#endif
