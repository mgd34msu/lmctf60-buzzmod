/* Immutable BSP-derived visibility over audited configuration regions. */
#ifndef SG_STATIC_VISIBILITY_H
#define SG_STATIC_VISIBILITY_H

#include <stdint.h>

#include "sg_configuration_semantics.h"

#define SG_STATIC_VISIBILITY_INDEX_NONE UINT32_MAX

typedef enum sg_static_visibility_error_code_e
{
	SG_STATIC_VISIBILITY_ERROR_NONE = 0,
	SG_STATIC_VISIBILITY_ERROR_INVALID_ARGUMENT,
	SG_STATIC_VISIBILITY_ERROR_INVALID_SOURCE,
	SG_STATIC_VISIBILITY_ERROR_SOURCE_MISMATCH,
	SG_STATIC_VISIBILITY_ERROR_NONFINITE_GEOMETRY,
	SG_STATIC_VISIBILITY_ERROR_OVERFLOW,
	SG_STATIC_VISIBILITY_ERROR_OUT_OF_MEMORY
} sg_static_visibility_error_code_t;

typedef struct sg_static_visibility_error_s
{
	sg_static_visibility_error_code_t code;
	uint32_t source_index;
} sg_static_visibility_error_t;

/* These are representation limits. They never stop work early. */
typedef struct sg_static_visibility_limits_s
{
	uint32_t max_partitions;
	uint32_t max_areas;
	uint32_t max_occluders;
	uint32_t max_surfaces;
} sg_static_visibility_limits_t;

typedef struct sg_static_visibility_partition_s
{
	uint64_t id;
	uint32_t configuration_region;
	uint32_t configuration_cell;
	uint32_t bsp_leaf;
	uint32_t bsp_area;
	uint32_t bsp_cluster;
} sg_static_visibility_partition_t;

typedef struct sg_static_visibility_occluder_s
{
	uint32_t model;
	uint32_t brush;
	uint32_t contents;
	uint32_t conditional;
} sg_static_visibility_occluder_t;

typedef struct sg_static_visibility_surface_s
{
	uint64_t id;
	uint32_t semantic_surface;
	uint32_t model;
	uint32_t brush;
	uint32_t brush_side;
	uint32_t flags;
} sg_static_visibility_surface_t;

typedef struct sg_static_visibility_s
{
	sg_rune_model_identity_t identity;
	sg_static_visibility_partition_t *partitions;
	uint32_t partition_count;
	/* Canonical minimum area index in each all-portals-open component. */
	uint32_t *area_components;
	uint32_t area_count;
	sg_static_visibility_occluder_t *occluders;
	uint32_t occluder_count;
	sg_static_visibility_surface_t *surfaces;
	uint32_t surface_count;
} sg_static_visibility_t;

typedef enum sg_static_visibility_class_e
{
	SG_STATIC_VISIBILITY_OCCLUDED = 0,
	SG_STATIC_VISIBILITY_VISIBLE,
	SG_STATIC_VISIBILITY_CONDITIONAL
} sg_static_visibility_class_t;

typedef enum sg_static_visibility_reason_e
{
	SG_STATIC_VISIBILITY_REASON_NONE = 0,
	SG_STATIC_VISIBILITY_REASON_PVS,
	SG_STATIC_VISIBILITY_REASON_AREA_GRAPH,
	SG_STATIC_VISIBILITY_REASON_STATIC_WORLD,
	SG_STATIC_VISIBILITY_REASON_AREA_PORTAL_STATE,
	SG_STATIC_VISIBILITY_REASON_EXACT_RAY_REQUIRED,
	SG_STATIC_VISIBILITY_REASON_MOVING_SUBMODEL,
	SG_STATIC_VISIBILITY_REASON_SKY
} sg_static_visibility_reason_t;

typedef struct sg_static_visibility_result_s
{
	sg_static_visibility_class_t classification;
	sg_static_visibility_reason_t reason;
	uint32_t source_partition;
	uint32_t destination_partition;
	uint32_t surface;
	uint32_t requires_exact_ray;
	uint32_t requires_area_state;
	sg_host_collision_trace_t trace;
} sg_static_visibility_result_t;

typedef enum sg_static_visibility_audit_code_e
{
	SG_STATIC_VISIBILITY_AUDIT_OK = 0,
	SG_STATIC_VISIBILITY_AUDIT_INVALID_ARGUMENT,
	SG_STATIC_VISIBILITY_AUDIT_OUT_OF_MEMORY,
	SG_STATIC_VISIBILITY_AUDIT_SOURCE_MISMATCH,
	SG_STATIC_VISIBILITY_AUDIT_OUTPUT_MUTATED,
	SG_STATIC_VISIBILITY_AUDIT_PARTITION_DISAGREEMENT,
	SG_STATIC_VISIBILITY_AUDIT_OCCLUDER_DISAGREEMENT,
	SG_STATIC_VISIBILITY_AUDIT_SURFACE_DISAGREEMENT
} sg_static_visibility_audit_code_t;

typedef struct sg_static_visibility_audit_result_s
{
	sg_static_visibility_audit_code_t code;
	uint32_t record;
	uint32_t reconstructed_partitions;
	uint32_t reconstructed_areas;
	uint32_t reconstructed_occluders;
	uint32_t reconstructed_surfaces;
} sg_static_visibility_audit_result_t;

void SG_StaticVisibilityDefaultLimits(
	sg_static_visibility_limits_t *limits_out);
int SG_StaticVisibilityBuild(const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	const sg_static_visibility_limits_t *limits,
	sg_static_visibility_t **visibility_out,
	sg_static_visibility_error_t *error_out);
int SG_StaticVisibilityAudit(const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	const sg_static_visibility_t *visibility,
	sg_static_visibility_audit_result_t *result_out);
int SG_StaticVisibilityQueryRegions(
	const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	const sg_static_visibility_t *visibility, uint32_t source_partition,
	uint32_t destination_partition, sg_static_visibility_result_t *result_out,
	sg_static_visibility_error_t *error_out);
int SG_StaticVisibilityQueryPoints(
	const sg_host_collision_authority_t *authority,
	const sg_host_collision_scene_t *scene,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	const sg_static_visibility_t *visibility, const float source[3],
	const float destination[3], sg_static_visibility_result_t *result_out,
	sg_static_visibility_error_t *error_out);
int SG_StaticVisibilityPointInPartition(
	const sg_configuration_semantics_t *semantics,
	const sg_static_visibility_t *visibility, uint32_t partition_index,
	const float point[3], uint32_t *face_tests_out);
int SG_StaticVisibilityQueryBoundPoints(
	const sg_host_collision_authority_t *authority,
	const sg_host_collision_scene_t *scene,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	const sg_static_visibility_t *visibility, uint32_t source_partition,
	uint32_t destination_partition, const float source[3],
	const float destination[3], sg_static_visibility_result_t *result_out,
	sg_static_visibility_error_t *error_out);
int SG_StaticVisibilityQuerySurface(
	const sg_host_collision_authority_t *authority,
	const sg_host_collision_scene_t *scene,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	const sg_static_visibility_t *visibility, const float source[3],
	uint32_t surface, const float target[3],
	sg_static_visibility_result_t *result_out,
	sg_static_visibility_error_t *error_out);
int SG_StaticVisibilityQueryBoundSurface(
	const sg_host_collision_authority_t *authority,
	const sg_host_collision_scene_t *scene,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	const sg_static_visibility_t *visibility, uint32_t source_partition,
	const float source[3], uint32_t surface_index, const float target[3],
	sg_static_visibility_result_t *result_out,
	sg_static_visibility_error_t *error_out);
void SG_StaticVisibilityDestroy(sg_static_visibility_t *visibility);
const char *SG_StaticVisibilityErrorString(
	sg_static_visibility_error_code_t code);
const char *SG_StaticVisibilityAuditCodeString(
	sg_static_visibility_audit_code_t code);

#endif
