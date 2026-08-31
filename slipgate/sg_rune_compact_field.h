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
	float velocity[3];
	float direction[3];
	float time_seconds;
	float distance;
	float support_distance;
	float fluid_fraction;
	float hook_length;
	float target_radius;
	const struct sg_rune_compact_field_mechanism_snapshot_s *mechanisms;
} sg_rune_compact_field_local_context_t;

typedef struct sg_rune_compact_field_mechanism_phase_s
{
	sg_rune_compact_mechanism_index_t mechanism;
	float phase;
} sg_rune_compact_field_mechanism_phase_t;

typedef struct sg_rune_compact_field_mechanism_snapshot_s
{
	/* The identity is matched by value against the identity bound at creation. */
	const sg_rune_compact_identity_t *model_identity;
	/* Entries are finite, unique, and strictly ordered by mechanism index. */
	const sg_rune_compact_field_mechanism_phase_t *phases;
	uint32_t phase_count;
} sg_rune_compact_field_mechanism_snapshot_t;

typedef enum sg_rune_compact_field_transition_kind_e
{
	SG_RUNE_COMPACT_FIELD_TRANSITION_PORTAL = 0,
	SG_RUNE_COMPACT_FIELD_TRANSITION_STANCE,
	SG_RUNE_COMPACT_FIELD_TRANSITION_KIND_COUNT
} sg_rune_compact_field_transition_kind_t;

typedef struct sg_rune_compact_field_portal_step_s
{
	float local_cost;
	float travel_time_seconds;
	sg_rune_compact_cell_index_t next_cell;
	sg_rune_compact_portal_index_t next_portal;
	sg_rune_compact_mechanism_index_t mechanism;
	uint32_t movement_field;
} sg_rune_compact_field_portal_step_t;

typedef struct sg_rune_compact_field_step_s
{
	sg_rune_compact_field_transition_kind_t kind;
	uint32_t source_rank;
	uint32_t target_rank;
	sg_rune_compact_field_stance_t target_stance;
	union
	{
		sg_rune_compact_field_portal_step_t portal;
	} value;
} sg_rune_compact_field_step_t;

typedef enum sg_rune_compact_field_result_kind_e
{
	SG_RUNE_COMPACT_FIELD_DISCONNECTED = 0,
	SG_RUNE_COMPACT_FIELD_LOCAL_DESTINATION,
	SG_RUNE_COMPACT_FIELD_CELL_DESTINATION,
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
	SG_RUNE_COMPACT_FIELD_MECHANISM_PHASE_REQUIRED,
	SG_RUNE_COMPACT_FIELD_EVALUATION_FAILED,
	SG_RUNE_COMPACT_FIELD_INVALID_TRANSITION_VALUE,
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
 * current state. Portal-local values are not remaining cost. A function that
 * reads MOVER_PHASE requires one portal mechanism binding and one matching
 * finite snapshot phase. Zero or multiple bindings and a missing phase return
 * MECHANISM_PHASE_REQUIRED. Interior attachments (boundary_portal == NONE)
 * cannot consume MOVER_PHASE. The snapshot and entries need only outlive the
 * query call. */
sg_rune_compact_field_status_t SG_RuneCompactFieldQuery(
	const sg_rune_compact_destination_plan_t *plan,
	const sg_rune_compact_field_local_context_t *context,
	sg_rune_compact_field_result_t *result_out);

const char *SG_RuneCompactFieldStatusString(
	sg_rune_compact_field_status_t status);

#endif
