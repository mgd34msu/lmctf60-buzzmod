/* V13 PMove control regions for the dry supported corridor walking slice. */
#ifndef SG_RUNE_COMPACT_PMOVE_CONTROL_H
#define SG_RUNE_COMPACT_PMOVE_CONTROL_H

#include <stddef.h>
#include <stdint.h>

#define SG_RUNE_PMOVE_CONTROL_VERSION UINT32_C(13)
#define SG_RUNE_PMOVE_CONTROL_NONE UINT32_MAX
#define SG_RUNE_PMOVE_CONTROL_FRAME_MS UINT32_C(100)
#define SG_RUNE_PMOVE_CONTROL_SUBSTEP_MS UINT32_C(25)
#define SG_RUNE_PMOVE_CONTROL_SUBSTEPS UINT32_C(4)
#define SG_RUNE_PMOVE_CONTROL_SOURCE_RESERVE UINT64_C(1)
#define SG_RUNE_PMOVE_CONTROL_MAXIMUM_VELOCITY_Q8 INT32_C(16000)
#define SG_RUNE_PMOVE_CONTROL_LATERAL_STOP_SECONDS_DENOMINATOR UINT32_C(4)
#define SG_RUNE_PMOVE_CONTROL_COLLISION_LAW_ID \
	UINT64_C(0x434f4c4c49534933)
#define SG_RUNE_PMOVE_CONTROL_PMOVE_LAW_ID UINT64_C(0x504d4f56454c5733)
#define SG_RUNE_PMOVE_CONTROL_BSP_IDENTITY_BYTES 32U

typedef enum sg_rune_pmove_control_error_e
{
	SG_RUNE_PMOVE_CONTROL_ERROR_NONE = 0,
	SG_RUNE_PMOVE_CONTROL_ERROR_INVALID_ARGUMENT,
	SG_RUNE_PMOVE_CONTROL_ERROR_INVALID_IDENTITY,
	SG_RUNE_PMOVE_CONTROL_ERROR_INVALID_DOMAIN,
	SG_RUNE_PMOVE_CONTROL_ERROR_INVALID_REFERENCE,
	SG_RUNE_PMOVE_CONTROL_ERROR_INVALID_CERTIFICATE,
	SG_RUNE_PMOVE_CONTROL_ERROR_REGION_MISS,
	SG_RUNE_PMOVE_CONTROL_ERROR_STALE_IDENTITY,
	SG_RUNE_PMOVE_CONTROL_ERROR_INCOMPLETE_REPLAY,
	SG_RUNE_PMOVE_CONTROL_ERROR_DYNAMIC_COLLISION,
	SG_RUNE_PMOVE_CONTROL_ERROR_PORTAL_MISMATCH,
	SG_RUNE_PMOVE_CONTROL_ERROR_OVERFLOW,
	SG_RUNE_PMOVE_CONTROL_ERROR_NO_DESCENT
} sg_rune_pmove_control_error_t;

typedef struct sg_rune_pmove_control_identity_s
{
	uint32_t version;
	uint32_t reserved;
	uint64_t compact_artifact_id;
	uint64_t bsp_content_id;
	uint8_t bsp_identity[SG_RUNE_PMOVE_CONTROL_BSP_IDENTITY_BYTES];
	uint64_t physics_abi_id;
	uint64_t collision_law_id;
	uint64_t pmove_law_id;
	uint64_t pmove_behavior_id;
	uint32_t frame_ms;
	uint32_t substep_ms;
	uint32_t substep_count;
	uint32_t reserved_2;
	uint64_t frame_cost_units;
	uint64_t source_reserve_units;
} sg_rune_pmove_control_identity_t;

typedef struct sg_rune_pmove_control_potential_s
{
	uint32_t id;
	uint32_t divisor;
	uint32_t distance_weight;
	uint32_t reversal_velocity_weight;
	uint32_t lateral_position_weight;
	uint32_t lateral_velocity_weight;
} sg_rune_pmove_control_potential_t;

/* The domain has positive intrinsic volume in (x,y,vx,vy).  Its lateral
 * position and velocity bounds are intersected with the stopping envelope
 * 4*abs(y-center)+abs(vy) < 4*half_width.  Dry supported walking is a support
 * stratum: z and vz are fixed by the authenticated host floor law. */
typedef struct sg_rune_pmove_control_region_s
{
	uint32_t id;
	uint32_t cell;
	uint32_t target_portal;
	uint32_t target_cell;
	uint32_t potential;
	uint32_t certificate;
	uint32_t first_transition;
	uint32_t transition_count;
	int32_t longitudinal_min_q8;
	int32_t longitudinal_max_q8;
	int32_t lateral_min_q8;
	int32_t lateral_max_q8;
	int32_t velocity_forward_min_q8;
	int32_t velocity_forward_max_q8;
	int32_t velocity_lateral_min_q8;
	int32_t velocity_lateral_max_q8;
	int32_t portal_q8;
	int32_t lateral_center_q8;
} sg_rune_pmove_control_region_t;

/* A small, checkable certificate for one branch-fixed host law.  It proves
 * the empty axis-aligned corridor has clearance for the standing hull and
 * that the potential decreases throughout the region's viability domain. */
typedef struct sg_rune_pmove_control_certificate_s
{
	uint32_t id;
	uint32_t region;
	int32_t hull_half_width_q8;
	int32_t wall_clearance_q8;
	int32_t static_support_z_q8;
	int32_t maximum_velocity_q8;
	uint32_t friction_keep_numerator;
	uint32_t friction_keep_denominator;
	int32_t acceleration_per_substep_q8;
	int32_t wish_speed_q8;
	uint64_t minimum_descent_units;
	uint32_t dry;
	uint32_t static_world_support;
} sg_rune_pmove_control_certificate_t;

typedef enum sg_rune_pmove_control_transition_kind_e
{
	SG_RUNE_PMOVE_CONTROL_TRANSITION_SAME_CELL = 1,
	SG_RUNE_PMOVE_CONTROL_TRANSITION_PORTAL = 2
} sg_rune_pmove_control_transition_kind_t;

typedef struct sg_rune_pmove_control_transition_s
{
	uint32_t source_region;
	uint32_t kind;
	uint32_t target_region;
	uint32_t target_cell;
	uint32_t portal;
	uint32_t certificate;
} sg_rune_pmove_control_transition_t;

typedef struct sg_rune_pmove_control_model_s
{
	sg_rune_pmove_control_identity_t identity;
	const sg_rune_pmove_control_region_t *regions;
	uint32_t region_count;
	const sg_rune_pmove_control_potential_t *potentials;
	uint32_t potential_count;
	const sg_rune_pmove_control_certificate_t *certificates;
	uint32_t certificate_count;
	const sg_rune_pmove_control_transition_t *transitions;
	uint32_t transition_count;
} sg_rune_pmove_control_model_t;

typedef struct sg_rune_pmove_control_state_s
{
	int32_t origin_q8[3];
	int32_t velocity_q8[3];
	uint32_t cell;
	uint32_t standing;
	uint32_t dry;
	uint32_t supported;
	uint32_t support_is_static_world;
} sg_rune_pmove_control_state_t;

typedef struct sg_rune_pmove_control_gradient_s
{
	int64_t longitudinal;
	int64_t lateral_position;
	int64_t reversal_velocity;
	int64_t lateral_velocity;
} sg_rune_pmove_control_gradient_t;

int SG_RunePmoveControlValidate(const sg_rune_pmove_control_model_t *model,
	sg_rune_pmove_control_error_t *error_out);
int SG_RunePmoveControlPotentialCeil(
	const sg_rune_pmove_control_model_t *model, uint32_t region,
	const sg_rune_pmove_control_state_t *state, uint64_t tail_units,
	uint64_t *units_out, sg_rune_pmove_control_error_t *error_out);
int SG_RunePmoveControlGradient(
	const sg_rune_pmove_control_model_t *model, uint32_t region,
	const sg_rune_pmove_control_state_t *state,
	sg_rune_pmove_control_gradient_t *gradient_out,
	sg_rune_pmove_control_error_t *error_out);
int SG_RunePmoveControlCheckDescent(
	const sg_rune_pmove_control_model_t *model, uint32_t region,
	const sg_rune_pmove_control_state_t *source,
	const sg_rune_pmove_control_state_t *target, uint32_t transition,
	uint64_t authenticated_tail_units, uint64_t *source_units_out,
	uint64_t *next_units_out,
	sg_rune_pmove_control_error_t *error_out);
const char *SG_RunePmoveControlErrorString(
	sg_rune_pmove_control_error_t error);

#endif /* SG_RUNE_COMPACT_PMOVE_CONTROL_H */
