/* Owned runtime cache and explicit coarse hierarchy for destination fields. */
#ifndef SG_DESTINATION_FIELD_CACHE_H
#define SG_DESTINATION_FIELD_CACHE_H

#include <stdint.h>

#include "sg_destination_field.h"

#define SG_FIELD_CACHE_NO_SLOT UINT32_MAX

typedef struct sg_field_region_level_s
{
	uint32_t region_count;
	/* One ancestor index for every leaf region in the runtime snapshot. */
	const uint32_t *leaf_to_region;
} sg_field_region_level_t;

typedef struct sg_field_region_hierarchy_s
{
	uint32_t leaf_region_count;
	uint32_t level_count;
	const sg_field_region_level_t *levels;
	/* One leaf-region owner for every phase in snapshot order. */
	const uint32_t *phase_to_leaf_region;
} sg_field_region_hierarchy_t;

typedef struct sg_field_region_ref_s
{
	uint32_t level;
	uint32_t region;
} sg_field_region_ref_t;

typedef enum sg_field_cache_scope_e
{
	SG_FIELD_CACHE_SCOPE_NONE = 0,
	SG_FIELD_CACHE_SCOPE_LOCAL,
	SG_FIELD_CACHE_SCOPE_ALL,
	SG_FIELD_CACHE_SCOPE_COUNT
} sg_field_cache_scope_t;

typedef enum sg_field_cache_disposition_e
{
	SG_FIELD_CACHE_MISS_SOLVED = 0,
	SG_FIELD_CACHE_HIT,
	SG_FIELD_CACHE_INCREMENTAL_REUSE,
	SG_FIELD_CACHE_CLEAN_REBUILD,
	SG_FIELD_CACHE_DISPOSITION_COUNT
} sg_field_cache_disposition_t;

/* A reference is O(1) to validate and query. It is invalidated by eviction,
 * explicit invalidation, or replacement of its slot. */
typedef struct sg_field_cache_ref_s
{
	uint64_t serial;
	uint32_t slot;
} sg_field_cache_ref_t;

typedef struct sg_field_cache_result_s
{
	sg_field_cache_ref_t ref;
	sg_field_cache_disposition_t disposition;
	sg_field_cache_scope_t scope;
	uint32_t affected_region_count;
	/* Borrowed from the cache until its next mutating operation. */
	const sg_field_region_ref_t *affected_regions;
} sg_field_cache_result_t;

typedef struct sg_destination_field_cache_stats_s
{
	uint64_t clean_solves;
	uint64_t static_hits;
	uint64_t incremental_reuses;
	uint64_t regional_updates;
	uint64_t evictions;
} sg_destination_field_cache_stats_t;

typedef struct sg_destination_field_cache_s sg_destination_field_cache_t;

/* Create copies the hierarchy and binds the cache to the exact snapshot and
 * immutable model identity. entry_capacity is a storage capacity, not a work
 * limit: every requested clean solve runs to convergence. */
int SG_DestinationFieldCacheCreate(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_field_region_hierarchy_t *hierarchy,
	uint32_t entry_capacity,
	sg_destination_field_cache_t **out);
void SG_DestinationFieldCacheDestroy(sg_destination_field_cache_t *cache);
void SG_DestinationFieldCacheInvalidate(sg_destination_field_cache_t *cache);

/* Static destinations reuse an exact semantic-key hit regardless of now_ms.
 * Moving destinations include now_ms in their key because it is part of the
 * terminal exactness domain. */
int SG_DestinationFieldCacheResolve(
	sg_destination_field_cache_t *cache,
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_destination_handle_t *destination,
	uint64_t now_ms,
	sg_field_cache_result_t *out);

/* Same-phase updates transfer immutable samples and affect only terminal
 * ancestors. A phase change recomputes the exact reverse dependency closure;
 * it reports ALL only when that closure owns every leaf region. */
int SG_DestinationFieldCacheUpdate(
	sg_destination_field_cache_t *cache,
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_destination_handle_t *before,
	const sg_destination_handle_t *after,
	uint64_t now_ms,
	sg_field_cache_result_t *out);

int SG_DestinationFieldCacheQuery(
	const sg_destination_field_cache_t *cache,
	const sg_rune_runtime_snapshot_t *snapshot,
	sg_field_cache_ref_t ref,
	const sg_destination_pose_t *source,
	sg_field_query_result_t *out);

/* The returned field is borrowed until the cache's next mutation. */
int SG_DestinationFieldCacheField(
	const sg_destination_field_cache_t *cache,
	const sg_rune_runtime_snapshot_t *snapshot,
	sg_field_cache_ref_t ref,
	const sg_destination_field_t **out);

void SG_DestinationFieldCacheStats(
	const sg_destination_field_cache_t *cache,
	sg_destination_field_cache_stats_t *out);

#endif /* SG_DESTINATION_FIELD_CACHE_H */
