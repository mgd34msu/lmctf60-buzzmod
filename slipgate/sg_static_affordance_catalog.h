/* Immutable audit-only join of accepted static-affordance authorities. */
#ifndef SG_STATIC_AFFORDANCE_CATALOG_H
#define SG_STATIC_AFFORDANCE_CATALOG_H

#include <stdint.h>

#include "sg_hook_visibility_catalog.h"
#include "sg_static_visibility_publication.h"
#include "sg_weapon_static_affordance.h"

#define SG_STATIC_AFFORDANCE_CATALOG_AUTHORITY_COUNT UINT32_C(3)
#define SG_STATIC_AFFORDANCE_CATALOG_INDEX_NONE UINT32_MAX

typedef struct sg_static_affordance_catalog_s sg_static_affordance_catalog_t;

/* The fixed index order is part of the catalog's deterministic audit form. */
typedef enum sg_static_affordance_catalog_authority_e
{
	SG_STATIC_AFFORDANCE_CATALOG_STATIC_VISIBILITY = 0,
	SG_STATIC_AFFORDANCE_CATALOG_WEAPON_STATIC_AFFORDANCE,
	SG_STATIC_AFFORDANCE_CATALOG_HOOK_VISIBILITY,
	SG_STATIC_AFFORDANCE_CATALOG_AUTHORITY_COUNT_VALUE
} sg_static_affordance_catalog_authority_t;

typedef enum sg_static_affordance_catalog_coverage_e
{
	/* This snapshot neither authorizes nor executes a game action. */
	SG_STATIC_AFFORDANCE_CATALOG_AUDIT_ONLY = 0
} sg_static_affordance_catalog_coverage_t;

/* Region classifications are exhaustive, static-only query results. They are
 * indexed in source-partition-major order and never include a live scene or
 * actor. */
typedef struct sg_static_affordance_catalog_visibility_classification_s
{
	uint8_t classification;
	uint8_t reason;
	uint8_t requires_exact_ray;
	uint8_t requires_area_state;
} sg_static_affordance_catalog_visibility_classification_t;

typedef struct sg_static_affordance_catalog_static_visibility_evidence_s
{
	sg_rune_model_identity_t identity;
	uint64_t revision;
	const sg_static_visibility_partition_t *partitions;
	uint32_t partition_count;
	const uint32_t *area_components;
	uint32_t area_count;
	const sg_static_visibility_occluder_t *occluders;
	uint32_t occluder_count;
	const sg_static_visibility_surface_t *surfaces;
	uint32_t surface_count;
	const sg_static_affordance_catalog_visibility_classification_t
		*classifications;
	uint64_t classification_count;
} sg_static_affordance_catalog_static_visibility_evidence_t;

/* Weapon evidence is emitted only by the opaque weapon-owner publication.
 * The catalog never accepts a caller-supplied affordance result structure. */
typedef sg_weapon_static_result_evidence_t
	sg_static_affordance_catalog_weapon_evidence_t;

typedef struct sg_static_affordance_catalog_hook_terminal_s
{
	sg_hook_visibility_domain_term_t domain;
	sg_hook_visibility_catalog_outcome_t outcome;
	sg_hook_visibility_catalog_domain_flags_t flags;
	uint32_t surface_rule_index;
} sg_static_affordance_catalog_hook_terminal_t;

/* A reduced hook relation points into the catalog-owned flattened domain
 * array. Its surface fields are retained redundantly so audits can prove the
 * relation was not silently rebound to a different rule. */
typedef struct sg_static_affordance_catalog_hook_relation_s
{
	uint64_t surface_id;
	uint32_t model_index;
	uint32_t texinfo;
	uint32_t surface_rule_index;
	uint32_t first_domain;
	uint32_t domain_count;
} sg_static_affordance_catalog_hook_relation_t;

typedef struct sg_static_affordance_catalog_hook_evidence_s
{
	uint64_t source_digest;
	uint64_t verifier_source_digest;
	uint64_t producer_identity;
	uint64_t verifier_identity;
	sg_rune_model_identity_t collision_identity;
	uint32_t world_counts[8];
	sg_hook_visibility_q8_box_t origins;
	sg_rune_stance_t stance;
	sg_hook_visibility_fire_law_t fire_law;
	const sg_hook_visibility_control_root_t *controls;
	uint32_t control_count;
	const sg_hook_visibility_surface_rule_t *surface_rules;
	uint32_t surface_rule_count;
	const sg_static_affordance_catalog_hook_terminal_t *terminals;
	uint32_t terminal_count;
	const sg_static_affordance_catalog_hook_relation_t *relations;
	uint32_t relation_count;
	const sg_hook_visibility_domain_term_t *relation_domains;
	uint32_t relation_domain_count;
	sg_hook_visibility_feasibility_metrics_t metrics;
	sg_hook_visibility_feasibility_audit_report_t acceptance;
} sg_static_affordance_catalog_hook_evidence_t;

typedef struct sg_static_affordance_catalog_evidence_view_s
{
	sg_static_affordance_catalog_coverage_t coverage;
	uint32_t authority_count;
	uint64_t content_digest;
	sg_static_affordance_catalog_static_visibility_evidence_t
		static_visibility;
	/* Retained for callers that need only the accepted binding. */
	sg_weapon_static_binding_t weapon_binding;
	const sg_static_affordance_catalog_weapon_evidence_t *weapons;
	uint32_t weapon_count;
	sg_static_affordance_catalog_hook_evidence_t hook;
} sg_static_affordance_catalog_evidence_view_t;

/* The caller owns every input. Weapon results must be opaque accepted
 * publications from the weapon owner; Issue audits, canonicalizes, and
 * deep-copies their complete evidence, so every predecessor may be destroyed
 * immediately after a successful issue. */
typedef struct sg_static_affordance_catalog_input_s
{
	const sg_static_visibility_publication_t *static_visibility;
	const sg_weapon_static_context_t *weapon_context;
	const sg_weapon_static_result_publication_t *const *weapon_publications;
	uint32_t weapon_count;
	const sg_hook_visibility_catalog_t *hook_catalog;
} sg_static_affordance_catalog_input_t;

typedef enum sg_static_affordance_catalog_error_code_e
{
	SG_STATIC_AFFORDANCE_CATALOG_ERROR_NONE = 0,
	SG_STATIC_AFFORDANCE_CATALOG_ERROR_INVALID_ARGUMENT,
	SG_STATIC_AFFORDANCE_CATALOG_ERROR_STATIC_VISIBILITY_REJECTED,
	SG_STATIC_AFFORDANCE_CATALOG_ERROR_WEAPON_CONTEXT_REJECTED,
	SG_STATIC_AFFORDANCE_CATALOG_ERROR_WEAPON_EVIDENCE_REJECTED,
	SG_STATIC_AFFORDANCE_CATALOG_ERROR_SOURCE_MISMATCH,
	SG_STATIC_AFFORDANCE_CATALOG_ERROR_HOOK_CATALOG_REJECTED,
	SG_STATIC_AFFORDANCE_CATALOG_ERROR_COMPLEMENT_DISAGREEMENT,
	SG_STATIC_AFFORDANCE_CATALOG_ERROR_OVERFLOW,
	SG_STATIC_AFFORDANCE_CATALOG_ERROR_OUT_OF_MEMORY,
	SG_STATIC_AFFORDANCE_CATALOG_ERROR_COPY_DISAGREEMENT
} sg_static_affordance_catalog_error_code_t;

typedef struct sg_static_affordance_catalog_error_s
{
	sg_static_affordance_catalog_error_code_t code;
	sg_static_affordance_catalog_authority_t authority;
} sg_static_affordance_catalog_error_t;

typedef enum sg_static_affordance_catalog_audit_code_e
{
	SG_STATIC_AFFORDANCE_CATALOG_AUDIT_OK = 0,
	SG_STATIC_AFFORDANCE_CATALOG_AUDIT_INVALID_ARGUMENT,
	SG_STATIC_AFFORDANCE_CATALOG_AUDIT_STORAGE_DISAGREEMENT,
	SG_STATIC_AFFORDANCE_CATALOG_AUDIT_SOURCE_MISMATCH,
	SG_STATIC_AFFORDANCE_CATALOG_AUDIT_STATIC_VISIBILITY_DISAGREEMENT,
	SG_STATIC_AFFORDANCE_CATALOG_AUDIT_WEAPON_EVIDENCE_DISAGREEMENT,
	SG_STATIC_AFFORDANCE_CATALOG_AUDIT_HOOK_EVIDENCE_DISAGREEMENT,
	SG_STATIC_AFFORDANCE_CATALOG_AUDIT_DIGEST_DISAGREEMENT,
	SG_STATIC_AFFORDANCE_CATALOG_AUDIT_COVERAGE_DISAGREEMENT
} sg_static_affordance_catalog_audit_code_t;

typedef struct sg_static_affordance_catalog_audit_report_s
{
	sg_static_affordance_catalog_audit_code_t code;
	sg_static_affordance_catalog_authority_t authority;
} sg_static_affordance_catalog_audit_report_t;

int SG_StaticAffordanceCatalogIssue(
	const sg_static_affordance_catalog_input_t *input,
	sg_static_affordance_catalog_t **catalog_out,
	sg_static_affordance_catalog_error_t *error_out);

/* Audit operates only on the catalog's owned snapshot. In particular, it
 * never dereferences a predecessor and remains valid after every input has
 * reached the end of its lifetime. */
int SG_StaticAffordanceCatalogAudit(
	const sg_static_affordance_catalog_t *catalog,
	sg_static_affordance_catalog_audit_report_t *report_out);

int SG_StaticAffordanceCatalogEvidence(
	const sg_static_affordance_catalog_t *catalog,
	sg_static_affordance_catalog_evidence_view_t *evidence_out);
uint32_t SG_StaticAffordanceCatalogAuthorityCount(
	const sg_static_affordance_catalog_t *catalog);
int SG_StaticAffordanceCatalogAuthority(
	const sg_static_affordance_catalog_t *catalog, uint32_t index,
	sg_static_affordance_catalog_authority_t *authority_out);
uint64_t SG_StaticAffordanceCatalogVisibilityClassificationCount(
	const sg_static_affordance_catalog_t *catalog);
int SG_StaticAffordanceCatalogVisibilityClassification(
	const sg_static_affordance_catalog_t *catalog, uint64_t index,
	sg_static_affordance_catalog_visibility_classification_t
		*classification_out);
uint32_t SG_StaticAffordanceCatalogWeaponCount(
	const sg_static_affordance_catalog_t *catalog);
int SG_StaticAffordanceCatalogWeapon(
	const sg_static_affordance_catalog_t *catalog, uint32_t index,
	sg_static_affordance_catalog_weapon_evidence_t *weapon_out);
uint32_t SG_StaticAffordanceCatalogHookTerminalCount(
	const sg_static_affordance_catalog_t *catalog);
int SG_StaticAffordanceCatalogHookTerminal(
	const sg_static_affordance_catalog_t *catalog, uint32_t index,
	sg_static_affordance_catalog_hook_terminal_t *terminal_out);
uint32_t SG_StaticAffordanceCatalogHookRelationCount(
	const sg_static_affordance_catalog_t *catalog);
int SG_StaticAffordanceCatalogHookRelation(
	const sg_static_affordance_catalog_t *catalog, uint32_t index,
	sg_static_affordance_catalog_hook_relation_t *relation_out,
	const sg_hook_visibility_domain_term_t **domains_out);
int SG_StaticAffordanceCatalogHookOutcomeCount(
	const sg_static_affordance_catalog_t *catalog,
	sg_hook_visibility_catalog_outcome_t outcome, uint32_t *count_out);
void SG_StaticAffordanceCatalogDestroy(sg_static_affordance_catalog_t *catalog);
const char *SG_StaticAffordanceCatalogErrorString(
	sg_static_affordance_catalog_error_code_t code);
const char *SG_StaticAffordanceCatalogAuditCodeString(
	sg_static_affordance_catalog_audit_code_t code);

#endif /* SG_STATIC_AFFORDANCE_CATALOG_H */
