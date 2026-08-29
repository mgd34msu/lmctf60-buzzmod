/* Immutable audit-only join of accepted static-affordance authorities. */
#ifndef SG_STATIC_AFFORDANCE_CATALOG_H
#define SG_STATIC_AFFORDANCE_CATALOG_H

#include <stdint.h>

#include "sg_hook_visibility_catalog.h"
#include "sg_static_visibility_publication.h"
#include "sg_weapon_static_affordance.h"

#define SG_STATIC_AFFORDANCE_CATALOG_AUTHORITY_COUNT UINT32_C(3)

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

typedef struct sg_static_affordance_catalog_static_visibility_evidence_s
{
	sg_rune_model_identity_t identity;
	uint64_t revision;
	uint32_t partition_count;
	uint32_t area_count;
	uint32_t occluder_count;
	uint32_t surface_count;
} sg_static_affordance_catalog_static_visibility_evidence_t;

/* This is scalar evidence copied from the hook catalog. It deliberately
 * contains neither terminal domains nor surface geometry. The acceptance
 * report retains every hook complement outcome. */
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
	uint32_t terminal_count;
	uint32_t relation_count;
	sg_hook_visibility_feasibility_metrics_t metrics;
	sg_hook_visibility_feasibility_audit_report_t acceptance;
} sg_static_affordance_catalog_hook_evidence_t;

typedef struct sg_static_affordance_catalog_evidence_view_s
{
	sg_static_affordance_catalog_coverage_t coverage;
	uint32_t authority_count;
	sg_static_affordance_catalog_static_visibility_evidence_t
		static_visibility;
	sg_weapon_static_binding_t weapon_binding;
	sg_static_affordance_catalog_hook_evidence_t hook;
} sg_static_affordance_catalog_evidence_view_t;

/* The caller owns all inputs. Issue copies only scalar evidence, so every
 * predecessor may be destroyed immediately after a successful issue. */
typedef struct sg_static_affordance_catalog_input_s
{
	const sg_static_visibility_publication_t *static_visibility;
	const sg_weapon_static_context_t *weapon_context;
	const sg_hook_visibility_catalog_t *hook_catalog;
} sg_static_affordance_catalog_input_t;

typedef enum sg_static_affordance_catalog_error_code_e
{
	SG_STATIC_AFFORDANCE_CATALOG_ERROR_NONE = 0,
	SG_STATIC_AFFORDANCE_CATALOG_ERROR_INVALID_ARGUMENT,
	SG_STATIC_AFFORDANCE_CATALOG_ERROR_STATIC_VISIBILITY_REJECTED,
	SG_STATIC_AFFORDANCE_CATALOG_ERROR_WEAPON_CONTEXT_REJECTED,
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
	SG_STATIC_AFFORDANCE_CATALOG_AUDIT_COMPLEMENT_DISAGREEMENT,
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

/* Audit operates only on the catalog's owned scalar snapshot. In particular,
 * it never dereferences a predecessor and remains valid after every input has
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
int SG_StaticAffordanceCatalogHookOutcomeCount(
	const sg_static_affordance_catalog_t *catalog,
	sg_hook_visibility_catalog_outcome_t outcome, uint32_t *count_out);
void SG_StaticAffordanceCatalogDestroy(sg_static_affordance_catalog_t *catalog);
const char *SG_StaticAffordanceCatalogErrorString(
	sg_static_affordance_catalog_error_code_t code);
const char *SG_StaticAffordanceCatalogAuditCodeString(
	sg_static_affordance_catalog_audit_code_t code);

#endif /* SG_STATIC_AFFORDANCE_CATALOG_H */
