#ifndef SG_RUNE_COMPACT_FIELD_H
#define SG_RUNE_COMPACT_FIELD_H

#include <stdint.h>

#include "sg_rune_compact_eval.h"
#include "sg_rune_compact_localize.h"
#include "sg_rune_compact_static.h"

typedef struct sg_rune_compact_field_s sg_rune_compact_field_t;
typedef struct sg_rune_compact_destination_plan_s
	sg_rune_compact_destination_plan_t;

typedef enum sg_rune_compact_field_stance_e
{
	SG_RUNE_COMPACT_FIELD_STANDING = 0,
	SG_RUNE_COMPACT_FIELD_CROUCHING,
	SG_RUNE_COMPACT_FIELD_STANCE_COUNT
} sg_rune_compact_field_stance_t;

typedef enum sg_rune_compact_destination_kind_e
{
	SG_RUNE_COMPACT_DESTINATION_POINT = 0,
	SG_RUNE_COMPACT_DESTINATION_CELL,
	SG_RUNE_COMPACT_DESTINATION_SURFACE,
	SG_RUNE_COMPACT_DESTINATION_ITEM,
	SG_RUNE_COMPACT_DESTINATION_KIND_COUNT
} sg_rune_compact_destination_kind_t;

typedef struct sg_rune_compact_destination_s
{
	sg_rune_compact_destination_kind_t kind;
	union
	{
		sg_rune_q8_vec3_t point;
		sg_rune_compact_cell_index_t cell;
		sg_rune_compact_incidence_index_t surface;
		sg_rune_compact_landmark_index_t item;
	} value;
} sg_rune_compact_destination_t;

typedef struct sg_rune_compact_field_local_context_s
{
	sg_rune_q8_vec3_t origin;
	sg_rune_compact_field_stance_t stance;
	/* Exact owner-observed source state for v12 fiber selection.  The mover
	 * index is an authority index iff support/flags are mover-relative; it is
	 * SG_RUNE_COMPACT_INDEX_NONE otherwise. */
	sg_rune_movement_support_kind_t support;
	sg_rune_movement_water_kind_t water;
	sg_host_hook_phase_t hook_phase;
	sg_rune_movement_state_flags_t state_flags;
	uint32_t mover_mechanism;
	float velocity[3];
	float direction[3];
	float time_seconds;
	float distance;
	float support_distance;
	float fluid_fraction;
	float hook_length;
	float target_radius;
	/* The frame that owns every live runtime observation below. */
	uint64_t frame_sequence;
	const struct sg_rune_compact_field_mechanism_snapshot_s *mechanisms;
	const struct sg_rune_compact_field_portal_root_snapshot_s *portal_roots;
} sg_rune_compact_field_local_context_t;

typedef struct sg_rune_compact_field_mechanism_phase_s
{
	/* Live phase is consumed by authority transitions and mover-relative
	 * movement states.  Static BLOCKS roots use their separate static-domain
	 * snapshot below. */
	sg_rune_authority_mechanism_index_t mechanism;
	float phase;
} sg_rune_compact_field_mechanism_phase_t;

typedef struct sg_rune_compact_field_mechanism_snapshot_s
{
	/* The identity is matched by value against the identity bound at creation. */
	const sg_rune_compact_identity_t *model_identity;
	/* Must match local_context.frame_sequence and portal_roots when present. */
	uint64_t frame_sequence;
	/* Entries are finite, unique, and strictly ordered by mechanism index. */
	const sg_rune_compact_field_mechanism_phase_t *phases;
	uint32_t phase_count;
} sg_rune_compact_field_mechanism_snapshot_t;

/* A portal root is one authenticated static BLOCKS relation.  The field's
 * snapshot layout is portal-major, even though compact wire records remain
 * canonical mechanism-major. */
typedef enum sg_rune_compact_field_portal_root_state_e
{
	SG_RUNE_COMPACT_FIELD_PORTAL_ROOT_BLOCKED = 0,
	SG_RUNE_COMPACT_FIELD_PORTAL_ROOT_UNBLOCKED,
	SG_RUNE_COMPACT_FIELD_PORTAL_ROOT_UNKNOWN,
	SG_RUNE_COMPACT_FIELD_PORTAL_ROOT_STATE_COUNT
} sg_rune_compact_field_portal_root_state_t;

typedef struct sg_rune_compact_field_portal_root_s
{
	sg_rune_compact_portal_index_t portal;
	sg_rune_compact_mechanism_index_t mechanism;
	sg_rune_compact_field_portal_root_state_t state;
} sg_rune_compact_field_portal_root_t;

typedef struct sg_rune_compact_field_portal_root_snapshot_s
{
	/* Matched by value against the identity bound at field creation. */
	const sg_rune_compact_identity_t *model_identity;
	/* Must exactly equal local_context.frame_sequence. */
	uint64_t frame_sequence;
	/* Complete, unique, field-owned portal-major root coverage. */
	const sg_rune_compact_field_portal_root_t *roots;
	uint32_t root_count;
} sg_rune_compact_field_portal_root_snapshot_t;

typedef enum sg_rune_compact_field_mechanism_requirement_state_e
{
	SG_RUNE_COMPACT_FIELD_MECHANISM_REQUIREMENTS_BLOCKED = 0,
	SG_RUNE_COMPACT_FIELD_MECHANISM_REQUIREMENTS_UNKNOWN,
	SG_RUNE_COMPACT_FIELD_MECHANISM_REQUIREMENTS_STATE_COUNT
} sg_rune_compact_field_mechanism_requirement_state_t;

/* The mechanism list is owned by the immutable field and remains stable until
 * that field is destroyed.  It contains every independent root for portal. */
typedef struct sg_rune_compact_field_mechanism_requirements_s
{
	sg_rune_compact_portal_index_t portal;
	const sg_rune_compact_mechanism_index_t *mechanisms;
	uint32_t mechanism_count;
	sg_rune_compact_field_mechanism_requirement_state_t state;
} sg_rune_compact_field_mechanism_requirements_t;

typedef enum sg_rune_compact_field_transition_kind_e
{
	SG_RUNE_COMPACT_FIELD_TRANSITION_PORTAL = 0,
	SG_RUNE_COMPACT_FIELD_TRANSITION_DIRECT,
	SG_RUNE_COMPACT_FIELD_TRANSITION_STANCE,
	SG_RUNE_COMPACT_FIELD_TRANSITION_KIND_COUNT
} sg_rune_compact_field_transition_kind_t;

/* Unsigned Q52.12 analytic cost. One cost unit is exactly 1/4096 of the
 * compact analytic COST output; UINT64_MAX is reserved for unavailable cost.
 * Integer comparison preserves destination-cost ordering exactly. */
#define SG_RUNE_COMPACT_FIELD_COST_FRACTION_BITS 12U
#define SG_RUNE_COMPACT_FIELD_COST_SCALE UINT64_C(4096)
#define SG_RUNE_COMPACT_FIELD_COST_UNAVAILABLE UINT64_MAX

typedef struct sg_rune_compact_field_cost_s
{
	uint64_t units;
} sg_rune_compact_field_cost_t;

typedef struct sg_rune_compact_field_portal_step_s
{
	float local_cost;
	sg_rune_compact_cell_index_t next_cell;
	sg_rune_compact_portal_index_t next_portal;
} sg_rune_compact_field_portal_step_t;

/* A direct step is a capability whose fiber names its destination without a
 * topology portal.  Hook targets and mover/external-force transitions use
 * this shape.  Movement-family and fiber provenance remain private. */
typedef struct sg_rune_compact_field_direct_step_s
{
	float local_cost;
	sg_rune_compact_cell_index_t next_cell;
} sg_rune_compact_field_direct_step_t;

typedef struct sg_rune_compact_field_step_s
{
	sg_rune_compact_field_transition_kind_t kind;
	/* Immutable destination-field costs at the authenticated source and the
	 * selected successor.  The step's local_cost is evaluated separately from
	 * live context and is not their implied difference. */
	sg_rune_compact_field_cost_t cost_to_go;
	sg_rune_compact_field_cost_t next_cost_to_go;
	sg_rune_compact_field_stance_t target_stance;
	union
	{
		sg_rune_compact_field_portal_step_t portal;
		sg_rune_compact_field_direct_step_t direct;
	} value;
} sg_rune_compact_field_step_t;

typedef enum sg_rune_compact_field_result_kind_e
{
	SG_RUNE_COMPACT_FIELD_DISCONNECTED = 0,
	SG_RUNE_COMPACT_FIELD_LOCAL_DESTINATION,
	SG_RUNE_COMPACT_FIELD_CELL_DESTINATION,
	SG_RUNE_COMPACT_FIELD_MECHANISMS_REQUIRED,
	SG_RUNE_COMPACT_FIELD_BLOCKED_NOW,
	SG_RUNE_COMPACT_FIELD_STEP,
	SG_RUNE_COMPACT_FIELD_RESULT_KIND_COUNT
} sg_rune_compact_field_result_kind_t;

typedef struct sg_rune_compact_field_result_s
{
	sg_rune_compact_field_result_kind_t kind;
	sg_rune_compact_cell_index_t current_cell;
	union
	{
		sg_rune_compact_destination_t destination;
		sg_rune_compact_field_step_t step;
		sg_rune_compact_field_mechanism_requirements_t requirements;
	} value;
} sg_rune_compact_field_result_t;

typedef enum sg_rune_compact_field_status_e
{
	SG_RUNE_COMPACT_FIELD_OK = 0,
	SG_RUNE_COMPACT_FIELD_INVALID_ARGUMENT,
	SG_RUNE_COMPACT_FIELD_INVALID_MODEL,
	SG_RUNE_COMPACT_FIELD_INVALID_DESTINATION,
	SG_RUNE_COMPACT_FIELD_INVALID_CONTEXT,
	SG_RUNE_COMPACT_FIELD_LOCALIZATION_FAILED,
	SG_RUNE_COMPACT_FIELD_INVALID_MECHANISM_SNAPSHOT,
	SG_RUNE_COMPACT_FIELD_INVALID_PORTAL_ROOT_SNAPSHOT,
	SG_RUNE_COMPACT_FIELD_MECHANISM_PHASE_REQUIRED,
	SG_RUNE_COMPACT_FIELD_EVALUATION_FAILED,
	SG_RUNE_COMPACT_FIELD_INVALID_TRANSITION_VALUE,
	SG_RUNE_COMPACT_FIELD_COST_OVERFLOW,
	SG_RUNE_COMPACT_FIELD_ALLOCATION_FAILED,
	SG_RUNE_COMPACT_FIELD_STATUS_COUNT
} sg_rune_compact_field_status_t;

/* The service borrows an immutable model that must outlive it and all plans.
 * Creation binds the complete model to expected_identity before retaining it. */
sg_rune_compact_field_status_t SG_RuneCompactFieldCreate(
	const sg_rune_compact_model_t *model,
	const sg_rune_compact_identity_t *expected_identity,
	sg_rune_compact_field_t **field_out,
	sg_rune_compact_error_t *model_error_out);

void SG_RuneCompactFieldDestroy(sg_rune_compact_field_t *field);

/* The immutable plan borrows field and must be destroyed before field. */
sg_rune_compact_field_status_t SG_RuneCompactFieldPlanCreate(
	const sg_rune_compact_field_t *field,
	const sg_rune_compact_destination_t *destination,
	sg_rune_compact_destination_plan_t **plan_out);

void SG_RuneCompactFieldPlanDestroy(
	sg_rune_compact_destination_plan_t *plan);

/* Query is allocation-free and evaluates only transitions from the localized
 * current state against the destination plan's converged directional cost
 * field. The returned step owns geometry and cost only; it never identifies
 * a capability or fiber. Every portal crossing requires all of its static
 * BLOCKS roots to be UNBLOCKED in the exact frame-scoped portal_roots
 * snapshot; absent roots are UNKNOWN and fail closed with
 * MECHANISMS_REQUIRED. MOVER_PHASE is analytic-only: a function that reads it
 * uses its fiber's exact mechanism-authority transition or angular schedule
 * and one matching finite phase. Snapshots and their entries need only
 * outlive the query call. */
sg_rune_compact_field_status_t SG_RuneCompactFieldQuery(
	const sg_rune_compact_destination_plan_t *plan,
	const sg_rune_compact_field_local_context_t *context,
	sg_rune_compact_field_result_t *result_out);

const char *SG_RuneCompactFieldStatusString(
	sg_rune_compact_field_status_t status);

/* Portal-major runtime layout for snapshot producers.  These values are
 * derived once at field creation from static BLOCKS relations; callers cannot
 * use the compact mechanism-major wire order as a live lookup table. */
uint32_t SG_RuneCompactFieldPortalRootCount(
	const sg_rune_compact_field_t *field);
int SG_RuneCompactFieldPortalRootAt(const sg_rune_compact_field_t *field,
	uint32_t root_index, sg_rune_compact_portal_index_t *portal_out,
	sg_rune_compact_mechanism_index_t *mechanism_out);

#endif
