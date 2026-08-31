/* Private deterministic convex partition operations for compact construction. */
#ifndef SG_RUNE_COMPACT_GEOMETRY_PARTITION_H
#define SG_RUNE_COMPACT_GEOMETRY_PARTITION_H

#include <stddef.h>
#include <stdint.h>

#include "sg_configuration_space.h"

/* All predicates use normalized double-precision planes. This relative
 * tolerance is scaled by the largest coordinate involved in the predicate. */
#define SG_RUNE_COMPACT_PARTITION_RELATIVE_EPSILON 1.0e-7

typedef void *(*sg_rune_compact_partition_allocate_fn)(void *context,
	size_t bytes);
typedef void (*sg_rune_compact_partition_release_fn)(void *context,
	void *allocation);

typedef struct sg_rune_compact_partition_allocator_s
{
	void *context;
	sg_rune_compact_partition_allocate_fn allocate;
	sg_rune_compact_partition_release_fn release;
} sg_rune_compact_partition_allocator_t;

/* contributor is caller-defined. The partitioner copies but never interprets
 * it. Open controls ownership only; it does not remove the boundary from any
 * geometric operation. */
typedef struct sg_rune_compact_partition_halfspace_s
{
	sg_configuration_plane_t plane;
	uint32_t source_plane_index;
	uint32_t contributor;
	uint8_t open;
	uint8_t reserved[3];
} sg_rune_compact_partition_halfspace_t;

typedef struct sg_rune_compact_partition_polygon_s
{
	sg_configuration_plane_t plane;
	uint32_t source_plane_index;
	uint32_t contributor;
	uint8_t open;
	uint8_t reserved[3];
	sg_rune_vec3_t *vertices;
	uint32_t vertex_count;
} sg_rune_compact_partition_polygon_t;

typedef struct sg_rune_compact_partition_polyhedron_s
{
	sg_rune_compact_partition_polygon_t *faces;
	uint32_t face_count;
	sg_rune_bounds_t bounds;
	int empty;
} sg_rune_compact_partition_polyhedron_t;

typedef struct sg_rune_compact_partition_cell_s
{
	const sg_rune_compact_partition_halfspace_t *halfspaces;
	uint32_t halfspace_count;
	sg_rune_bounds_t bounds;
} sg_rune_compact_partition_cell_t;

typedef struct sg_rune_compact_partition_subtraction_s
{
	sg_rune_compact_partition_polygon_t *remainders;
	uint32_t remainder_count;
	sg_rune_compact_partition_polygon_t consumed;
} sg_rune_compact_partition_subtraction_t;

typedef enum sg_rune_compact_partition_error_code_e
{
	SG_RUNE_COMPACT_PARTITION_ERROR_NONE = 0,
	SG_RUNE_COMPACT_PARTITION_ERROR_INVALID_ARGUMENT,
	SG_RUNE_COMPACT_PARTITION_ERROR_NONFINITE,
	SG_RUNE_COMPACT_PARTITION_ERROR_DEGENERATE,
	SG_RUNE_COMPACT_PARTITION_ERROR_OVERFLOW,
	SG_RUNE_COMPACT_PARTITION_ERROR_OUT_OF_MEMORY,
	SG_RUNE_COMPACT_PARTITION_ERROR_CODE_COUNT
} sg_rune_compact_partition_error_code_t;

typedef enum sg_rune_compact_partition_operation_e
{
	SG_RUNE_COMPACT_PARTITION_OPERATION_DERIVE_FACES = 0,
	SG_RUNE_COMPACT_PARTITION_OPERATION_CLIP_POLYGON,
	SG_RUNE_COMPACT_PARTITION_OPERATION_SUBTRACT_POLYGON,
	SG_RUNE_COMPACT_PARTITION_OPERATION_INTERSECT_CELLS
} sg_rune_compact_partition_operation_t;

typedef struct sg_rune_compact_partition_error_s
{
	sg_rune_compact_partition_error_code_t code;
	sg_rune_compact_partition_operation_t operation;
	uint32_t record;
} sg_rune_compact_partition_error_t;

/* allocator may be NULL to use malloc and free. Each function publishes its
 * output only on success and leaves the caller's output sentinel untouched on
 * failure. The halfspace convention is dot(normal, point) <= distance. */
int SG_RuneCompactPartitionDeriveFaces(
	const sg_rune_compact_partition_halfspace_t *halfspaces,
	uint32_t halfspace_count, const sg_rune_bounds_t *bounds,
	const sg_rune_compact_partition_allocator_t *allocator,
	sg_rune_compact_partition_polyhedron_t *polyhedron_out,
	sg_rune_compact_partition_error_t *error_out);

int SG_RuneCompactPartitionClipPolygon(
	const sg_rune_compact_partition_polygon_t *polygon,
	const sg_rune_compact_partition_halfspace_t *clip,
	const sg_rune_compact_partition_allocator_t *allocator,
	sg_rune_compact_partition_polygon_t *polygon_out,
	sg_rune_compact_partition_error_t *error_out);

int SG_RuneCompactPartitionSubtractPolygon(
	const sg_rune_compact_partition_polygon_t *face,
	const sg_rune_compact_partition_polygon_t *portal,
	const sg_rune_compact_partition_allocator_t *allocator,
	sg_rune_compact_partition_subtraction_t *subtraction_out,
	sg_rune_compact_partition_error_t *error_out);

int SG_RuneCompactPartitionIntersectCells(
	const sg_rune_compact_partition_cell_t *left,
	const sg_rune_compact_partition_cell_t *right,
	const sg_rune_compact_partition_allocator_t *allocator,
	sg_rune_compact_partition_polyhedron_t *intersection_out,
	sg_rune_compact_partition_error_t *error_out);

void SG_RuneCompactPartitionPolygonDestroy(
	sg_rune_compact_partition_polygon_t *polygon,
	const sg_rune_compact_partition_allocator_t *allocator);
void SG_RuneCompactPartitionPolyhedronDestroy(
	sg_rune_compact_partition_polyhedron_t *polyhedron,
	const sg_rune_compact_partition_allocator_t *allocator);
void SG_RuneCompactPartitionSubtractionDestroy(
	sg_rune_compact_partition_subtraction_t *subtraction,
	const sg_rune_compact_partition_allocator_t *allocator);

const char *SG_RuneCompactPartitionErrorString(
	sg_rune_compact_partition_error_code_t code);

#endif
