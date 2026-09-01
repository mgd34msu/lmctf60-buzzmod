/* Runtime ownership for destination fields over one immutable compact RUNE. */
#ifndef SG_RUNE_COMPACT_FIELD_SERVICE_H
#define SG_RUNE_COMPACT_FIELD_SERVICE_H

#include <stddef.h>
#include <stdint.h>

#include "sg_destination.h"
#include "sg_rune_compact_field.h"

typedef struct sg_rune_compact_field_service_s
	sg_rune_compact_field_service_t;

typedef enum sg_rune_compact_field_target_motion_e
{
	SG_RUNE_COMPACT_FIELD_TARGET_STATIC = 0,
	SG_RUNE_COMPACT_FIELD_TARGET_MOVING,
	SG_RUNE_COMPACT_FIELD_TARGET_MOTION_COUNT
} sg_rune_compact_field_target_motion_t;

/* A target is a runtime destination identity plus its compact destination.
 * The semantic destination is part of the cache key: strategy target IDs are
 * plan-local (for example, every plan may call its primary target "5"), so an
 * ID alone must never alias the other team's flag or another bot's goal. The
 * service never stores actors in the RUNE. A moving target must advance
 * target_generation whenever its destination changes. */
typedef struct sg_rune_compact_field_target_s
{
	uint64_t target_id;
	uint64_t target_generation;
	sg_rune_compact_field_target_motion_t motion;
	sg_destination_ref_t semantic_destination;
	sg_rune_compact_destination_t destination;
} sg_rune_compact_field_target_t;

/* Handles are capabilities, not indexes. Every field generation is unique
 * within a service and remains valid until its matching release. */
typedef struct sg_rune_compact_field_handle_s
{
	uint64_t service_identity;
	uint64_t service_generation;
	uint64_t rune_identity;
	uint64_t topology_revision;
	uint64_t target_id;
	uint64_t target_generation;
	uint64_t field_generation;
} sg_rune_compact_field_handle_t;

typedef struct sg_rune_compact_field_service_stats_s
{
	size_t cached_field_count;
	size_t lease_count;
	size_t coarse_region_count;
	uint64_t static_cache_hits;
	uint64_t moving_cache_hits;
	uint64_t clean_plan_builds;
	uint64_t moving_plan_rebuilds;
	uint64_t region_refreshes;
	uint64_t incremental_plan_refreshes;
	uint64_t incremental_affected_states;
	uint64_t incremental_affected_leaf_regions;
	uint64_t incremental_affected_coarse_regions;
	uint64_t incremental_examined_transitions;
} sg_rune_compact_field_service_stats_t;

typedef enum sg_rune_compact_field_service_status_e
{
	SG_RUNE_COMPACT_FIELD_SERVICE_OK = 0,
	SG_RUNE_COMPACT_FIELD_SERVICE_INVALID_ARGUMENT,
	SG_RUNE_COMPACT_FIELD_SERVICE_INVALID_SERVICE,
	SG_RUNE_COMPACT_FIELD_SERVICE_INVALID_TARGET,
	SG_RUNE_COMPACT_FIELD_SERVICE_IDENTITY_MISMATCH,
	SG_RUNE_COMPACT_FIELD_SERVICE_STALE,
	SG_RUNE_COMPACT_FIELD_SERVICE_FIELD_FAILED,
	SG_RUNE_COMPACT_FIELD_SERVICE_ALLOCATION_FAILED,
	SG_RUNE_COMPACT_FIELD_SERVICE_CAPACITY,
	SG_RUNE_COMPACT_FIELD_SERVICE_STATUS_COUNT
} sg_rune_compact_field_service_status_t;

/* The model and identity are borrowed. The owner must keep them immutable and
 * alive until service destruction. Neither construction nor the wire schema
 * is changed by this runtime owner. */
sg_rune_compact_field_service_status_t
SG_RuneCompactFieldServiceCreate(
	const sg_rune_compact_model_t *model,
	const sg_rune_compact_identity_t *expected_identity,
	uint64_t rune_identity, uint64_t topology_revision,
	sg_rune_compact_field_service_t **service_out,
	sg_rune_compact_error_t *model_error_out);

void SG_RuneCompactFieldServiceDestroy(
	sg_rune_compact_field_service_t *service);

/* Resolve leases an immutable destination plan. Static targets reuse the
 * exact plan. A moving target derives an exact affected-region refresh only
 * after its generation advances. */
sg_rune_compact_field_service_status_t
SG_RuneCompactFieldServiceResolve(
	sg_rune_compact_field_service_t *service,
	const sg_rune_compact_field_target_t *target,
	sg_rune_compact_field_handle_t *handle_out);

/* Refresh atomically acquires the new target plan before releasing previous.
 * The previous handle must belong to the same target and service. */
sg_rune_compact_field_service_status_t
SG_RuneCompactFieldServiceRefresh(
	sg_rune_compact_field_service_t *service,
	const sg_rune_compact_field_handle_t *previous,
	const sg_rune_compact_field_target_t *target,
	sg_rune_compact_field_handle_t *handle_out);

sg_rune_compact_field_service_status_t
SG_RuneCompactFieldServiceQuery(
	const sg_rune_compact_field_service_t *service,
	const sg_rune_compact_field_handle_t *handle,
	const sg_rune_compact_field_local_context_t *context,
	sg_rune_compact_field_result_t *result_out);

sg_rune_compact_field_service_status_t
SG_RuneCompactFieldServiceRelease(
	sg_rune_compact_field_service_t *service,
	const sg_rune_compact_field_handle_t *handle);

/* The handle is current only while its lease and cache entry are both live.
 * This check is intentionally stricter than Query: a retired moving-target
 * lease remains queryable for an in-flight consumer, but cannot authenticate
 * a newly resolved strategy binding. */
int SG_RuneCompactFieldServiceHandleCurrent(
	const sg_rune_compact_field_service_t *service,
	const sg_rune_compact_field_handle_t *handle,
	sg_rune_compact_field_target_t *target_out,
	uint32_t *coarse_region_out,
	uint64_t *coarse_region_epoch_out);

/* Invalidation retires only the selected destination version. Existing leases
 * remain queryable until release; later resolves build a fresh version. */
sg_rune_compact_field_service_status_t
SG_RuneCompactFieldServiceInvalidateTarget(
	sg_rune_compact_field_service_t *service, uint64_t target_id);

void SG_RuneCompactFieldServiceStats(
	const sg_rune_compact_field_service_t *service,
	sg_rune_compact_field_service_stats_t *stats_out);

const sg_rune_compact_model_t *SG_RuneCompactFieldServiceModel(
	const sg_rune_compact_field_service_t *service);
uint64_t SG_RuneCompactFieldServiceIdentity(
	const sg_rune_compact_field_service_t *service);
uint64_t SG_RuneCompactFieldServiceGeneration(
	const sg_rune_compact_field_service_t *service);

/* Borrow the field-owned portal-major BLOCKS layout for a live snapshot
 * producer.  The wire's mechanism-major static ordering never crosses this
 * runtime boundary. */
uint32_t SG_RuneCompactFieldServicePortalRootCount(
	const sg_rune_compact_field_service_t *service);
int SG_RuneCompactFieldServicePortalRootAt(
	const sg_rune_compact_field_service_t *service, uint32_t root_index,
	sg_rune_compact_portal_index_t *portal_out,
	sg_rune_compact_mechanism_index_t *mechanism_out);

/* Runtime pose supplied by the live owner for destinations that can move.
 * Positions are quantized to the compact Q8 lattice by the resolver; the
 * source remains responsible for using a monotonic generation whenever the
 * semantic target changes. */
typedef struct sg_rune_compact_field_service_live_pose_s
{
	uint8_t present;
	uint8_t reserved[7];
	uint64_t generation;
	float position[3];
	float velocity[3];
	uint64_t observed_at_ms;
} sg_rune_compact_field_service_live_pose_t;

/* Map a strategy destination to the compact representation without creating
 * seed/link edges. Static item, flag, and defensive landmarks resolve from
 * the accepted model. Current flags, carriers, escort/intercept targets,
 * learned points, and waypoints require the matching live pose. The output
 * handle is the reducer-facing semantic identity; the compact target is the
 * field-service identity. */
int SG_RuneCompactFieldServiceResolveSemanticTarget(
	const sg_rune_compact_field_service_t *service,
	uint64_t target_id,
	const sg_destination_ref_t *destination,
	const sg_rune_compact_field_service_live_pose_t *live_pose,
	sg_rune_compact_field_target_t *target_out,
	sg_destination_handle_t *handle_out);

/* Compact-native provider callbacks. The locator lends a stable token; only
 * the authority mints a lease, and release returns that exact token. These
 * callbacks intentionally do not translate into legacy seed/link fields. */
typedef struct sg_rune_compact_field_service_target_request_s
{
	uint64_t commitment_id;
	sg_rune_compact_field_target_t target;
} sg_rune_compact_field_service_target_request_t;

typedef struct sg_rune_compact_field_service_target_view_s
{
	const void *opaque;
} sg_rune_compact_field_service_target_view_t;

typedef struct sg_rune_compact_field_service_target_binding_s
{
	uint64_t commitment_id;
	sg_rune_compact_field_target_t target;
	sg_rune_compact_field_handle_t handle;
	const void *accepted_view;
	uint32_t coarse_region;
	uint64_t coarse_region_epoch;
} sg_rune_compact_field_service_target_binding_t;

typedef int (*sg_rune_compact_field_service_target_locator_fn)(void *context,
	const sg_rune_compact_field_service_target_request_t *request,
	sg_rune_compact_field_service_target_view_t *view_out);
typedef int (*sg_rune_compact_field_service_target_authority_fn)(void *context,
	const sg_rune_compact_field_service_target_request_t *request,
	const sg_rune_compact_field_service_target_view_t *view,
	sg_rune_compact_field_service_target_binding_t *binding_out);
typedef void (*sg_rune_compact_field_service_target_release_fn)(void *context,
	const void *accepted_view);

typedef struct sg_rune_compact_field_service_provider_s
{
	sg_rune_compact_field_service_target_locator_fn locator;
	sg_rune_compact_field_service_target_authority_fn authority;
	sg_rune_compact_field_service_target_release_fn release_view;
	void *context;
} sg_rune_compact_field_service_provider_t;

int SG_RuneCompactFieldServiceProvider(
	sg_rune_compact_field_service_t *service,
	sg_rune_compact_field_service_provider_t *provider_out);

int SG_RuneCompactFieldServiceTargetLocate(void *context,
	const sg_rune_compact_field_service_target_request_t *request,
	sg_rune_compact_field_service_target_view_t *view_out);
int SG_RuneCompactFieldServiceTargetAuthorize(void *context,
	const sg_rune_compact_field_service_target_request_t *request,
	const sg_rune_compact_field_service_target_view_t *view,
	sg_rune_compact_field_service_target_binding_t *binding_out);
void SG_RuneCompactFieldServiceTargetRelease(void *context,
	const void *accepted_view);

const char *SG_RuneCompactFieldServiceStatusString(
	sg_rune_compact_field_service_status_t status);

#endif /* SG_RUNE_COMPACT_FIELD_SERVICE_H */
