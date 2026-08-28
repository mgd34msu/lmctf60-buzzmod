/* Weapon-specific interpretation of shared, immutable static visibility. */
#ifndef SG_WEAPON_STATIC_AFFORDANCE_H
#define SG_WEAPON_STATIC_AFFORDANCE_H

#include <stdint.h>

#include "sg_static_visibility.h"
#include "sg_weapon_effect_profile.h"

#define SG_WEAPON_STATIC_RELATION_COUNT UINT32_C(5)

typedef enum sg_weapon_static_status_e
{
	SG_WEAPON_STATIC_NOT_REQUESTED = 0,
	SG_WEAPON_STATIC_REJECTED,
	SG_WEAPON_STATIC_CONDITIONAL,
	SG_WEAPON_STATIC_PROVEN
} sg_weapon_static_status_t;

typedef enum sg_weapon_static_reason_e
{
	SG_WEAPON_STATIC_REASON_NONE = 0,
	SG_WEAPON_STATIC_REASON_PROFILE_UNSUPPORTED,
	SG_WEAPON_STATIC_REASON_VISIBILITY,
	SG_WEAPON_STATIC_REASON_TARGET_NOT_SURFACE,
	SG_WEAPON_STATIC_REASON_OUTSIDE_SPLASH_REACH,
	SG_WEAPON_STATIC_REASON_PROJECTILE_CLEARANCE,
	SG_WEAPON_STATIC_REASON_UNPROVEN_SURFACE_COVERAGE
} sg_weapon_static_reason_t;

typedef struct sg_weapon_static_relation_result_s
{
	sg_weapon_static_relation_t relation;
	sg_weapon_static_status_t status;
	sg_weapon_static_reason_t reason;
	sg_static_visibility_result_t visibility;
	sg_rune_vec3_t witness_point;
	uint8_t has_witness_point;
	uint8_t reserved[3];
} sg_weapon_static_relation_result_t;

/* This value is static evidence only. It deliberately has no shot-authority
 * field. A caller must still obtain an exact, authenticated live pre-fire
 * trace through the host weapon boundary. */
typedef struct sg_weapon_static_affordance_s
{
	sg_weapon_static_binding_t binding;
	sg_weapon_profile_id_t profile_id;
	sg_weapon_family_t family;
	sg_weapon_static_relation_t requested_relations;
	sg_weapon_static_relation_t allowed_relations;
	sg_weapon_static_relation_t proven_relations;
	sg_weapon_static_relation_t rejected_relations;
	sg_weapon_static_relation_t conditional_relations;
	uint8_t exact_authenticated_live_prefire_trace_required;
	uint8_t reserved[3];
	sg_weapon_static_relation_result_t
		relations[SG_WEAPON_STATIC_RELATION_COUNT];
} sg_weapon_static_affordance_t;

typedef enum sg_weapon_static_affordance_error_code_e
{
	SG_WEAPON_STATIC_AFFORDANCE_ERROR_NONE = 0,
	SG_WEAPON_STATIC_AFFORDANCE_ERROR_INVALID_ARGUMENT,
	SG_WEAPON_STATIC_AFFORDANCE_ERROR_INVALID_QUERY,
	SG_WEAPON_STATIC_AFFORDANCE_ERROR_IDENTITY_MISMATCH,
	SG_WEAPON_STATIC_AFFORDANCE_ERROR_INVALID_PROFILE,
	SG_WEAPON_STATIC_AFFORDANCE_ERROR_INVALID_SOURCE,
	SG_WEAPON_STATIC_AFFORDANCE_ERROR_VISIBILITY
} sg_weapon_static_affordance_error_code_t;

typedef struct sg_weapon_static_affordance_error_s
{
	sg_weapon_static_affordance_error_code_t code;
	sg_static_visibility_error_t visibility;
} sg_weapon_static_affordance_error_t;

/* The accepted boundary records these counts after static visibility audit.
 * The model is accepted only after SG_RuneModelValidate succeeds; that
 * contract supplies the canonical order required by bounded lookup. */
typedef struct sg_weapon_static_source_audit_s
{
	sg_weapon_static_binding_t binding;
	sg_static_visibility_audit_result_t visibility;
	uint32_t configuration_cells;
	uint32_t semantic_regions;
	uint32_t semantic_surfaces;
	uint32_t semantic_surface_vertices;
	uint32_t model_cells;
	uint32_t model_phases;
} sg_weapon_static_source_audit_t;

/* The accepted-artifact boundary constructs this bundle. Its binding applies
 * to every borrowed object for the full duration of one resolver call. */
typedef struct sg_weapon_static_sources_s
{
	sg_weapon_static_binding_t binding;
	const sg_host_collision_authority_t *authority;
	const sg_configuration_space_t *configuration;
	const sg_configuration_semantics_t *semantics;
	const sg_static_visibility_t *visibility;
	const sg_rune_model_t *model;
	const sg_weapon_static_source_audit_t *audit;
} sg_weapon_static_sources_t;

int SG_WeaponStaticAffordanceResolve(
	const sg_weapon_static_sources_t *sources,
	const sg_host_collision_scene_t *scene,
	const sg_weapon_static_query_t *query,
	const sg_weapon_profile_t *profile,
	sg_weapon_static_affordance_t *affordance_out,
	sg_weapon_static_affordance_error_t *error_out);

const char *SG_WeaponStaticAffordanceErrorString(
	sg_weapon_static_affordance_error_code_t code);

#endif /* SG_WEAPON_STATIC_AFFORDANCE_H */
