/* Immutable conservative brush index for compact RUNE construction. */
#ifndef SG_RUNE_COMPACT_SPATIAL_INDEX_H
#define SG_RUNE_COMPACT_SPATIAL_INDEX_H

#include <stddef.h>
#include <stdint.h>

#include "sg_bsp_world.h"
#include "sg_rune_model.h"

#define SG_RUNE_COMPACT_SPATIAL_EPSILON 0.0001f
#define SG_RUNE_COMPACT_SPATIAL_MAX_BRUSHES (UINT32_MAX / UINT32_C(2))

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

/* allocator may be NULL to use malloc and free. */
int SG_RuneCompactSpatialIndexBuild(const sg_bsp_world_t *world,
	const sg_rune_compact_spatial_allocator_t *allocator,
	sg_rune_compact_spatial_index_t **index_out,
	sg_rune_compact_spatial_error_t *error_out);

/* A NULL brushes_out and zero brush_capacity return the required count. */
int SG_RuneCompactSpatialIndexQuery(
	const sg_rune_compact_spatial_index_t *index,
	const sg_rune_compact_spatial_query_t *query, uint32_t *brushes_out,
	uint32_t brush_capacity, uint32_t *brush_count_out,
	sg_rune_compact_spatial_error_t *error_out);

void SG_RuneCompactSpatialIndexDestroy(
	sg_rune_compact_spatial_index_t *index);
const char *SG_RuneCompactSpatialIndexErrorString(
	sg_rune_compact_spatial_error_code_t code);

#endif
