/* Field-owned immutable plan derivation used by the runtime cache service. */
#ifndef SG_RUNE_COMPACT_FIELD_PLAN_PRIVATE_H
#define SG_RUNE_COMPACT_FIELD_PLAN_PRIVATE_H

#include <stdint.h>

#include "sg_rune_compact_field.h"
#include "sg_rune_compact_mechanisms.h"

typedef struct sg_rune_compact_field_refresh_report_s
{
	uint32_t affected_state_count;
	uint32_t invalidated_state_count;
	uint32_t decreased_state_count;
	uint32_t affected_leaf_region_count;
	uint32_t affected_coarse_region_count;
	uint64_t examined_transition_count;
} sg_rune_compact_field_refresh_report_t;

typedef enum sg_rune_compact_field_probe_provenance_kind_e
{
	SG_RUNE_COMPACT_FIELD_PROBE_INTRINSIC_STANCE = 0,
	SG_RUNE_COMPACT_FIELD_PROBE_PMOVE,
	SG_RUNE_COMPACT_FIELD_PROBE_HOOK,
	SG_RUNE_COMPACT_FIELD_PROBE_MECHANISM_TRANSITION,
	SG_RUNE_COMPACT_FIELD_PROBE_ANGULAR_MOVER,
	SG_RUNE_COMPACT_FIELD_PROBE_PROVENANCE_KIND_COUNT
} sg_rune_compact_field_probe_provenance_kind_t;

/* Common owner-private identity and state facts for one exact model-backed
 * arc.  These indexes never enter the public field result or the RUNE wire. */
typedef struct sg_rune_compact_field_movement_probe_s
{
	uint32_t field_arc;
	sg_rune_movement_capability_index_t capability;
	sg_rune_movement_fiber_index_t fiber;
	sg_rune_movement_capability_kind_t movement_kind;
	sg_rune_compact_movement_state_t source_state;
	sg_rune_compact_movement_state_t destination_state;
} sg_rune_compact_field_movement_probe_t;

typedef struct sg_rune_compact_field_pmove_probe_s
{
	sg_rune_compact_field_movement_probe_t movement;
} sg_rune_compact_field_pmove_probe_t;

typedef struct sg_rune_compact_field_hook_probe_s
{
	sg_rune_compact_field_movement_probe_t movement;
	uint32_t hook_target;
} sg_rune_compact_field_hook_probe_t;

typedef struct sg_rune_compact_field_mechanism_transition_probe_s
{
	sg_rune_compact_field_movement_probe_t movement;
	sg_rune_mechanism_transition_index_t mechanism_transition;
	sg_rune_mechanism_controller_index_t controller;
	sg_rune_authority_mechanism_index_t controller_target;
	sg_rune_compact_mechanism_transition_kind_t mechanism_kind;
} sg_rune_compact_field_mechanism_transition_probe_t;

typedef struct sg_rune_compact_field_angular_mover_probe_s
{
	sg_rune_compact_field_movement_probe_t movement;
	uint32_t angular_schedule;
} sg_rune_compact_field_angular_mover_probe_t;

/* STANCE is an intrinsic same-cell field edge, not a movement fiber.  This
 * tuple is its complete field-owned synthetic identity. */
typedef struct sg_rune_compact_field_intrinsic_stance_probe_s
{
	sg_rune_compact_cell_index_t cell;
	sg_rune_compact_field_stance_t source_stance;
	sg_rune_compact_field_stance_t destination_stance;
	uint32_t frame_ms;
} sg_rune_compact_field_intrinsic_stance_probe_t;

typedef struct sg_rune_compact_field_probe_provenance_s
{
	sg_rune_compact_field_probe_provenance_kind_t kind;
	union
	{
		sg_rune_compact_field_intrinsic_stance_probe_t intrinsic_stance;
		sg_rune_compact_field_pmove_probe_t pmove;
		sg_rune_compact_field_hook_probe_t hook;
		sg_rune_compact_field_mechanism_transition_probe_t mechanism;
		sg_rune_compact_field_angular_mover_probe_t angular_mover;
	} value;
} sg_rune_compact_field_probe_provenance_t;

/* Owner-private projection of one exact producer of the authenticated public
 * STEP.  The selector, not this evidence seam, decides strict descent. */
typedef struct sg_rune_compact_field_exact_probe_s
{
	sg_rune_compact_field_transition_kind_t transition_kind;
	sg_rune_compact_cell_index_t successor_cell;
	sg_rune_compact_portal_index_t portal;
	sg_rune_compact_field_stance_t successor_stance;
	sg_rune_compact_field_cost_t local_cost;
	float travel_time_seconds;
	sg_rune_compact_field_probe_provenance_t provenance;
} sg_rune_compact_field_exact_probe_t;

typedef int (*sg_rune_compact_field_exact_probe_visit_fn)(void *context,
	const sg_rune_compact_field_exact_probe_t *probe);

/* Derive a complete immutable plan from previous.  The old plan remains
 * readable.  destination is resolved by the same field-owned rules as clean
 * creation; no partial result is published on failure. */
sg_rune_compact_field_status_t SG_RuneCompactFieldPlanDerive(
	const sg_rune_compact_destination_plan_t *previous,
	const sg_rune_compact_destination_t *destination,
	sg_rune_compact_destination_plan_t **plan_out,
	sg_rune_compact_field_refresh_report_t *report_out);

/* Private verification and service accounting accessors. */
int SG_RuneCompactFieldPlanCostAt(
	const sg_rune_compact_destination_plan_t *plan,
	sg_rune_compact_field_stance_t stance, uint32_t cell,
	sg_rune_compact_field_cost_t *cost_out);
sg_rune_compact_field_status_t SG_RuneCompactFieldPlanVisitExactStepProbes(
	const sg_rune_compact_destination_plan_t *plan,
	const sg_rune_compact_field_local_context_t *context,
	const sg_rune_compact_field_result_t *expected_result,
	sg_rune_compact_field_exact_probe_visit_fn visit, void *visit_context,
	uint32_t *probe_count_out);
uint32_t SG_RuneCompactFieldRegionCount(const sg_rune_compact_field_t *field);
uint32_t SG_RuneCompactFieldCellRegion(
	const sg_rune_compact_field_t *field, uint32_t cell);
uint32_t SG_RuneCompactFieldDestinationRegion(
	const sg_rune_compact_field_t *field,
	const sg_rune_compact_destination_t *destination);

#endif /* SG_RUNE_COMPACT_FIELD_PLAN_PRIVATE_H */
