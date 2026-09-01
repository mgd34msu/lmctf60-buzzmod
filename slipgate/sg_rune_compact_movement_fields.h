#ifndef SG_RUNE_COMPACT_MOVEMENT_FIELDS_H
#define SG_RUNE_COMPACT_MOVEMENT_FIELDS_H

#include <stdint.h>

#include "sg_configuration_semantics.h"
#include "sg_host_law_publication.h"
#include "sg_rune_compact_analytic.h"
#include "sg_rune_compact_geometry.h"
#include "sg_rune_compact_mechanisms.h"
#include "sg_rune_compact_model.h"
#include "sg_rune_compact_response_partition.h"
#include "sg_rune_compact_static.h"
#include "sg_rune_compact_static_materializer.h"
#include "sg_static_visibility.h"

#define SG_RUNE_COMPACT_MOVEMENT_FIELDS_VERSION UINT16_C(1)
#define SG_RUNE_COMPACT_MOVEMENT_MAX_FIELDS SG_RUNE_COMPACT_MAX_MOVEMENT_FIELDS
#define SG_RUNE_COMPACT_MOVEMENT_MAX_ANALYTIC_REFERENCES \
	SG_RUNE_COMPACT_MAX_MOVEMENT_FIBER_FUNCTION_REFS

/* The constructor reads the already-authenticated compact geometry and host
 * publications. It does not copy the BSP or invent a second cell graph. */
typedef struct sg_rune_compact_movement_fields_input_s
{
	const sg_rune_compact_builder_t *builder;
	const sg_host_law_construction_t *host_owner;
	const sg_rune_compact_geometry_t *geometry_owner;
	const sg_rune_compact_response_partition_t *response_owner;
	const sg_rune_compact_mechanisms_t *mechanisms_owner;
	const sg_rune_compact_static_materializer_t *static_owner;
	const sg_host_collision_scene_t *collision_scene;
} sg_rune_compact_movement_fields_input_t;

typedef struct sg_rune_compact_movement_fields_view_s
{
	sg_rune_compact_identity_t identity;
	const sg_rune_movement_capability_t *capabilities;
	uint32_t capability_count;
	const sg_rune_compact_movement_state_t *states;
	uint32_t state_count;
	const sg_rune_compact_movement_fiber_t *fibers;
	uint32_t fiber_count;
	const sg_rune_compact_movement_hook_target_t *hook_targets;
	uint32_t hook_target_count;
	const sg_rune_analytic_function_index_t *fiber_function_refs;
	uint32_t fiber_function_ref_count;
	sg_rune_compact_analytic_t analytic;
	const sg_rune_compact_movement_angular_schedule_t *angular_schedules;
	uint32_t angular_schedule_count;
	sg_host_engine_pmove_abi_t pmove_abi;
	uint64_t pmove_behavior_fingerprint;
	uint64_t host_level_generation;
	uint64_t physics_abi_id;
	uint64_t collision_law_id;
	uint64_t pmove_law_id;
	uint64_t gravity_law_id;
	uint64_t hook_law_id;
	uint64_t mechanism_law_id;
} sg_rune_compact_movement_fields_view_t;

typedef struct sg_rune_compact_movement_fields_s
	sg_rune_compact_movement_fields_t;

typedef enum sg_rune_compact_movement_fields_error_code_e
{
	SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_NONE = 0,
	SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_ARGUMENT,
	SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_LIMIT_EXCEEDED,
	SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_GEOMETRY,
	SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_CONFIGURATION,
	SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_HOST_LAW,
	SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_VISIBILITY,
	SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_STATIC_DATA,
	SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_OUT_OF_MEMORY,
	SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_ANALYTIC_CONTRACT,
	SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_CODE_COUNT
} sg_rune_compact_movement_fields_error_code_t;

typedef struct sg_rune_compact_movement_fields_error_s
{
	sg_rune_compact_movement_fields_error_code_t code;
	uint32_t record;
	uint64_t expected;
	uint64_t observed;
} sg_rune_compact_movement_fields_error_t;

int SG_RuneCompactMovementFieldsBuild(
	const sg_rune_compact_movement_fields_input_t *input,
	sg_rune_compact_movement_fields_t **fields_out,
	sg_rune_compact_movement_fields_error_t *error_out);

int SG_RuneCompactMovementFieldsRead(
	const sg_rune_compact_movement_fields_t *fields,
	sg_rune_compact_movement_fields_view_t *view_out);

/* Composer seam: return the identity captured from the authenticated geometry
 * view together with the borrowed movement/analytic arrays. */
int SG_RuneCompactMovementFieldsReadBound(
	const sg_rune_compact_movement_fields_t *fields,
	sg_rune_compact_identity_t *identity_out,
	sg_rune_compact_movement_fields_view_t *view_out);

/* Evaluate one exact stock pusher frame for a persisted continuous rotator.
 * The caller owns live activation and blockage state: an inactive or rolled
 * back frame leaves angles unchanged. */
int SG_RuneCompactMovementAngularInitial(
	const sg_rune_compact_movement_angular_schedule_t *schedule,
	float angles_out[3]);
int SG_RuneCompactMovementAngularFrame(
	const sg_rune_compact_movement_angular_schedule_t *schedule,
	const float current_angles[3], int active, int frame_succeeded,
	float angles_out[3]);

void SG_RuneCompactMovementFieldsDestroy(
	sg_rune_compact_movement_fields_t *fields);

const char *SG_RuneCompactMovementFieldsErrorString(
	sg_rune_compact_movement_fields_error_code_t code);

#if defined(SG_RUNE_COMPACT_MOVEMENT_FIELDS_TESTING)
uint64_t SG_RuneCompactMovementFieldsTestPortalMergeSteps(void);
#endif

#endif
