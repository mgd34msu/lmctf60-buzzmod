/*
 * Non-enumerative synthetic gate for stationary, q8, six-plane box worlds.
 * It is not the production hook or air-capability constructor.
 */
#ifndef SG_HOOK_VISIBILITY_FEASIBILITY_H
#define SG_HOOK_VISIBILITY_FEASIBILITY_H

#include <stddef.h>
#include <stdint.h>

#include "sg_host_collision.h"

#define SG_HOOK_VISIBILITY_ANGLE_AUTHORITY_ID UINT64_C(0x506e721d6328c459)
#define SG_HOOK_VISIBILITY_MASK_SHOT \
	(SG_HOST_CONTENTS_SOLID | SG_HOST_CONTENTS_WINDOW | \
	 SG_HOST_CONTENTS_MONSTER | UINT32_C(0x04000000))

/* Exact limits of this bounded synthetic proof family, not work budgets. */
#define SG_HOOK_VISIBILITY_FEASIBILITY_MAX_CONTROL_ROOTS UINT32_C(15)
#define SG_HOOK_VISIBILITY_FEASIBILITY_MAX_SURFACE_RULES \
	(UINT32_MAX / UINT32_C(10))

typedef struct sg_hook_visibility_feasibility_catalog_s
	sg_hook_visibility_feasibility_catalog_t;

typedef enum sg_hook_visibility_hand_e
{
	SG_HOOK_VISIBILITY_HAND_LEFT = 0,
	SG_HOOK_VISIBILITY_HAND_CENTER,
	SG_HOOK_VISIBILITY_HAND_RIGHT,
	SG_HOOK_VISIBILITY_HAND_COUNT
} sg_hook_visibility_hand_t;

#define SG_HOOK_VISIBILITY_HAND_BIT(hand) (UINT32_C(1) << (hand))
#define SG_HOOK_VISIBILITY_ALL_HANDS \
	((UINT32_C(1) << SG_HOOK_VISIBILITY_HAND_COUNT) - UINT32_C(1))

typedef enum sg_hook_visibility_surface_class_e
{
	SG_HOOK_VISIBILITY_SURFACE_HOOKABLE = 0,
	SG_HOOK_VISIBILITY_SURFACE_NONHOOKABLE,
	SG_HOOK_VISIBILITY_SURFACE_SKY
} sg_hook_visibility_surface_class_t;

typedef struct sg_hook_visibility_q8_box_s
{
	int16_t mins[3];
	int16_t maxs[3];
} sg_hook_visibility_q8_box_t;

typedef struct sg_hook_visibility_control_root_s
{
	int16_t pitch_min;
	int16_t pitch_max;
	int16_t yaw_min;
	int16_t yaw_max;
} sg_hook_visibility_control_root_t;

typedef struct sg_hook_visibility_surface_rule_s
{
	uint64_t surface_id;
	uint32_t model_index;
	uint32_t brush_index;
	uint32_t texinfo;
	sg_hook_visibility_surface_class_t classification;
} sg_hook_visibility_surface_rule_t;

typedef struct sg_hook_visibility_fire_law_s
{
	uint64_t identity;
	uint64_t angle_authority_id;
	uint64_t mover_domain_identity;
	float standing_view_height;
	float crouching_view_height;
	float muzzle_forward;
	float muzzle_lateral;
	float maximum_range;
	float trace_epsilon;
	uint32_t shot_mask;
	uint32_t moving_model_count;
} sg_hook_visibility_fire_law_t;

typedef struct sg_hook_visibility_feasibility_sources_s
{
	const sg_host_collision_authority_t *collision;
	const sg_host_collision_scene_t *scene;
	const sg_hook_visibility_control_root_t *controls;
	uint32_t control_count;
	const sg_hook_visibility_surface_rule_t *surface_rules;
	uint32_t surface_rule_count;
	sg_hook_visibility_q8_box_t origins;
	sg_rune_stance_t stance;
	sg_hook_visibility_fire_law_t fire_law;
	uint64_t producer_identity;
	uint64_t verifier_identity;
} sg_hook_visibility_feasibility_sources_t;

typedef struct sg_hook_visibility_domain_term_s
{
	sg_hook_visibility_q8_box_t origins;
	int16_t pitch_min;
	int16_t pitch_max;
	int16_t yaw_min;
	int16_t yaw_max;
	uint32_t hand_mask;
} sg_hook_visibility_domain_term_t;

typedef struct sg_hook_visibility_relation_view_s
{
	uint64_t surface_id;
	uint32_t model_index;
	uint32_t texinfo;
	const sg_hook_visibility_domain_term_t *terms;
	uint32_t term_count;
} sg_hook_visibility_relation_view_t;

typedef struct sg_hook_visibility_feasibility_metrics_s
{
	uint64_t legal_action_tuples;
	uint64_t angle_authority_entries;
	uint64_t predicate_domains;
	uint64_t muzzle_clearance_traces;
	uint64_t first_hit_traces;
	uint32_t relation_count;
	uint32_t relation_term_count;
	uint32_t complement_term_count;
} sg_hook_visibility_feasibility_metrics_t;

typedef enum sg_hook_visibility_feasibility_error_code_e
{
	SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_NONE = 0,
	SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_INVALID_ARGUMENT,
	SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_INVALID_SOURCE,
	SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_SOURCE_MISMATCH,
	SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_ANGLE_AUTHORITY,
	SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_MOVING_MODEL_AUTHORITY,
	SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_UNSUPPORTED,
	SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_HOST_DISAGREEMENT,
	SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_OVERFLOW,
	SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_OUT_OF_MEMORY
} sg_hook_visibility_feasibility_error_code_t;

typedef struct sg_hook_visibility_feasibility_error_s
{
	sg_hook_visibility_feasibility_error_code_t code;
	uint32_t source_index;
} sg_hook_visibility_feasibility_error_t;

typedef enum sg_hook_visibility_feasibility_audit_code_e
{
	SG_HOOK_VISIBILITY_FEASIBILITY_AUDIT_OK = 0,
	SG_HOOK_VISIBILITY_FEASIBILITY_AUDIT_INVALID_ARGUMENT,
	SG_HOOK_VISIBILITY_FEASIBILITY_AUDIT_SOURCE_MISMATCH,
	SG_HOOK_VISIBILITY_FEASIBILITY_AUDIT_PRODUCER_VERIFIER_ALIAS,
	SG_HOOK_VISIBILITY_FEASIBILITY_AUDIT_ROOT_DISAGREEMENT,
	SG_HOOK_VISIBILITY_FEASIBILITY_AUDIT_TERMINAL_DISAGREEMENT,
	SG_HOOK_VISIBILITY_FEASIBILITY_AUDIT_RELATION_DISAGREEMENT,
	SG_HOOK_VISIBILITY_FEASIBILITY_AUDIT_HOST_DISAGREEMENT,
	SG_HOOK_VISIBILITY_FEASIBILITY_AUDIT_OUT_OF_MEMORY
} sg_hook_visibility_feasibility_audit_code_t;

typedef struct sg_hook_visibility_feasibility_audit_report_s
{
	sg_hook_visibility_feasibility_audit_code_t code;
	uint32_t record;
	uint64_t producer_identity;
	uint64_t verifier_identity;
	uint64_t reconstructed_action_tuples;
	uint64_t reconstructed_predicate_domains;
	uint32_t hookable_terms;
	uint32_t sky_terms;
	uint32_t nonhookable_terms;
	uint32_t no_hit_terms;
	uint32_t clearance_blocked_terms;
	uint32_t lower_dimensional_terms;
	uint32_t edge_terms;
	uint32_t vertex_terms;
	uint32_t tie_terms;
} sg_hook_visibility_feasibility_audit_report_t;

int SG_HookVisibilityFeasibilityBuild(
	const sg_hook_visibility_feasibility_sources_t *sources,
	sg_hook_visibility_feasibility_catalog_t **catalog_out,
	sg_hook_visibility_feasibility_error_t *error_out);
int SG_HookVisibilityFeasibilityAudit(
	const sg_hook_visibility_feasibility_sources_t *sources,
	const sg_hook_visibility_feasibility_catalog_t *catalog,
	sg_hook_visibility_feasibility_audit_report_t *report_out);
uint32_t SG_HookVisibilityFeasibilityRelationCount(
	const sg_hook_visibility_feasibility_catalog_t *catalog);
int SG_HookVisibilityFeasibilityRelation(
	const sg_hook_visibility_feasibility_catalog_t *catalog, uint32_t index,
	sg_hook_visibility_relation_view_t *relation_out);
int SG_HookVisibilityFeasibilityMetrics(
	const sg_hook_visibility_feasibility_catalog_t *catalog,
	sg_hook_visibility_feasibility_metrics_t *metrics_out);
int SG_HookVisibilityFeasibilitySerialize(
	const sg_hook_visibility_feasibility_catalog_t *catalog,
	uint8_t **bytes_out, size_t *size_out);
void SG_HookVisibilityFeasibilityDestroy(
	sg_hook_visibility_feasibility_catalog_t *catalog);
const char *SG_HookVisibilityFeasibilityErrorString(
	sg_hook_visibility_feasibility_error_code_t code);
const char *SG_HookVisibilityFeasibilityAuditCodeString(
	sg_hook_visibility_feasibility_audit_code_t code);

#endif
