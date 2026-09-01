/* Immutable exact brush and cell-complex indexes for compact RUNE construction. */
#ifndef SG_RUNE_COMPACT_SPATIAL_INDEX_H
#define SG_RUNE_COMPACT_SPATIAL_INDEX_H

#include <stddef.h>
#include <stdint.h>

#include "sg_bsp_world.h"
#include "sg_rune_compact_model.h"
#include "sg_rune_model.h"

#define SG_RUNE_COMPACT_SPATIAL_MAX_BRUSHES (UINT32_MAX / UINT32_C(2))
#define SG_RUNE_COMPACT_SPATIAL_INDEX_NONE UINT32_MAX

typedef struct sg_rune_compact_spatial_index_s
	sg_rune_compact_spatial_index_t;

typedef void *(*sg_rune_compact_spatial_allocate_fn)(void *context,
	size_t bytes);
typedef void (*sg_rune_compact_spatial_release_fn)(void *context,
	void *allocation);

typedef struct sg_rune_compact_spatial_allocator_s
{
	void *context;
	sg_rune_compact_spatial_allocate_fn allocate;
	sg_rune_compact_spatial_release_fn release;
} sg_rune_compact_spatial_allocator_t;

typedef enum sg_rune_compact_spatial_error_code_e
{
	SG_RUNE_COMPACT_SPATIAL_ERROR_NONE = 0,
	SG_RUNE_COMPACT_SPATIAL_ERROR_INVALID_ARGUMENT,
	SG_RUNE_COMPACT_SPATIAL_ERROR_INVALID_WORLD,
	SG_RUNE_COMPACT_SPATIAL_ERROR_NONFINITE_BOUNDS,
	SG_RUNE_COMPACT_SPATIAL_ERROR_OVERFLOW,
	SG_RUNE_COMPACT_SPATIAL_ERROR_OUT_OF_MEMORY,
	SG_RUNE_COMPACT_SPATIAL_ERROR_INSUFFICIENT_CAPACITY,
	SG_RUNE_COMPACT_SPATIAL_ERROR_INVALID_TOPOLOGY,
	SG_RUNE_COMPACT_SPATIAL_ERROR_NOT_FOUND,
	SG_RUNE_COMPACT_SPATIAL_ERROR_CODE_COUNT
} sg_rune_compact_spatial_error_code_t;

typedef struct sg_rune_compact_spatial_error_s
{
	sg_rune_compact_spatial_error_code_t code;
	uint32_t record;
	uint32_t required_capacity;
} sg_rune_compact_spatial_error_t;

typedef struct sg_rune_compact_spatial_query_s
{
	sg_rune_bounds_t origin_bounds;
	sg_rune_hull_profile_t hull;
} sg_rune_compact_spatial_query_t;

typedef struct sg_rune_compact_spatial_query_statistics_s
{
	uint32_t visited_nodes;
	uint32_t tested_entries;
} sg_rune_compact_spatial_query_statistics_t;

/* A topology cell owns one contiguous source-face span.  The span is copied
 * into the immutable index; no later face-pair reconstruction is performed. */
typedef struct sg_rune_compact_spatial_cell_input_s
{
	sg_rune_bounds_t bounds;
	uint32_t first_face;
	uint32_t face_count;
} sg_rune_compact_spatial_cell_input_t;

/* normal * point <= distance describes the cell interior.  OPEN assigns the
 * plane to its opposite CLOSED neighbour, making every shared face half-open
 * without a tolerance band. */
typedef struct sg_rune_compact_spatial_face_input_s
{
	sg_rune_bounds_t bounds;
	float normal[3];
	float distance;
	uint32_t source_boundary;
	sg_rune_boundary_ownership_t ownership;
} sg_rune_compact_spatial_face_input_t;

typedef struct sg_rune_compact_spatial_portal_input_s
{
	uint32_t source_boundary;
	uint32_t negative_cell;
	uint32_t positive_cell;
} sg_rune_compact_spatial_portal_input_t;

/* A split maps every portal incident to parent_cell to one or more child
 * portals.  An interior_portal is created directly between its two children.
 * This is the required adjacency evidence; consumers never infer portals from
 * co-planar or nearby faces. */
typedef struct sg_rune_compact_spatial_carried_portal_s
{
	uint32_t parent_portal;
	uint32_t child_portal;
} sg_rune_compact_spatial_carried_portal_t;

typedef struct sg_rune_compact_spatial_split_input_s
{
	uint32_t parent_cell;
	uint32_t negative_cell;
	uint32_t positive_cell;
	uint32_t source_boundary;
	uint32_t interior_portal;
	uint32_t first_carried_portal;
	uint32_t carried_portal_count;
} sg_rune_compact_spatial_split_input_t;

typedef struct sg_rune_compact_spatial_topology_input_s
{
	const sg_rune_compact_spatial_cell_input_t *cells;
	uint32_t cell_count;
	const sg_rune_compact_spatial_face_input_t *faces;
	uint32_t face_count;
	const sg_rune_compact_spatial_portal_input_t *portals;
	uint32_t portal_count;
	const sg_rune_compact_spatial_split_input_t *splits;
	uint32_t split_count;
	const sg_rune_compact_spatial_carried_portal_t *carried_portals;
	uint32_t carried_portal_count;
} sg_rune_compact_spatial_topology_input_t;

typedef struct sg_rune_compact_spatial_span_s
{
	uint32_t first;
	uint32_t count;
} sg_rune_compact_spatial_span_t;

typedef struct sg_rune_compact_spatial_counts_s
{
	uint32_t brush_count;
	uint32_t cell_count;
	uint32_t face_count;
	uint32_t portal_count;
	uint32_t source_boundary_count;
} sg_rune_compact_spatial_counts_t;

/* allocator may be NULL to use malloc and free. */
int SG_RuneCompactSpatialIndexBuild(const sg_bsp_world_t *bsp_world,
	const sg_rune_compact_spatial_allocator_t *allocator,
	sg_rune_compact_spatial_index_t **index_out,
	sg_rune_compact_spatial_error_t *error_out);

/* A NULL brushes_out and zero brush_capacity return the required count. */
int SG_RuneCompactSpatialIndexQuery(
	const sg_rune_compact_spatial_index_t *index,
	const sg_rune_compact_spatial_query_t *query, uint32_t *brushes_out,
	uint32_t brush_capacity, uint32_t *brush_count_out,
	sg_rune_compact_spatial_error_t *error_out);

/* As Query, with traversal counters for focused no-whole-world-scan tests. */
int SG_RuneCompactSpatialIndexQueryWithStatistics(
	const sg_rune_compact_spatial_index_t *index,
	const sg_rune_compact_spatial_query_t *query, uint32_t *brushes_out,
	uint32_t brush_capacity, uint32_t *brush_count_out,
	sg_rune_compact_spatial_query_statistics_t *statistics_out,
	sg_rune_compact_spatial_error_t *error_out);

/* Builds an immutable final cell complex from exact face and portal spans.
 * The input split sequence must already be canonical and topologically
 * ordered.  Cells and portals which were replaced by a split are absent from
 * the published index. */
int SG_RuneCompactSpatialIndexBuildTopology(
	const sg_rune_compact_spatial_topology_input_t *topology,
	const sg_rune_compact_spatial_allocator_t *allocator,
	sg_rune_compact_spatial_index_t **index_out,
	sg_rune_compact_spatial_error_t *error_out);

/* A NULL cells_out and zero cell_capacity return the required exact count. */
int SG_RuneCompactSpatialIndexQueryCells(
	const sg_rune_compact_spatial_index_t *index, const sg_rune_vec3_t *point,
	uint32_t *cells_out, uint32_t cell_capacity, uint32_t *cell_count_out,
	sg_rune_compact_spatial_error_t *error_out);

/* Reads exact source-boundary spans without scanning faces or portals.  The
 * returned pointers are borrowed from index and valid until destroy. */
int SG_RuneCompactSpatialIndexBoundaryRead(
	const sg_rune_compact_spatial_index_t *index, uint32_t source_boundary,
	const uint32_t **faces_out, sg_rune_compact_spatial_span_t *faces_span_out,
	const uint32_t **portals_out,
	sg_rune_compact_spatial_span_t *portals_span_out,
	sg_rune_compact_spatial_error_t *error_out);

int SG_RuneCompactSpatialIndexCounts(const sg_rune_compact_spatial_index_t *index,
	sg_rune_compact_spatial_counts_t *counts_out,
	sg_rune_compact_spatial_error_t *error_out);

void SG_RuneCompactSpatialIndexDestroy(
	sg_rune_compact_spatial_index_t *index);
const char *SG_RuneCompactSpatialIndexErrorString(
	sg_rune_compact_spatial_error_code_t code);

#endif
