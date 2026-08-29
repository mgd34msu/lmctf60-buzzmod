/* Immutable publication of an accepted hook-visibility feasibility proof. */
#ifndef SG_HOOK_VISIBILITY_CATALOG_H
#define SG_HOOK_VISIBILITY_CATALOG_H

#include <stdint.h>

#include "sg_hook_visibility_feasibility.h"

#define SG_HOOK_VISIBILITY_CATALOG_INDEX_NONE UINT32_MAX

typedef struct sg_hook_visibility_catalog_s sg_hook_visibility_catalog_t;

typedef enum sg_hook_visibility_catalog_outcome_e
{
	SG_HOOK_VISIBILITY_CATALOG_HOOKABLE = 0,
	SG_HOOK_VISIBILITY_CATALOG_SKY,
	SG_HOOK_VISIBILITY_CATALOG_NONHOOKABLE,
	SG_HOOK_VISIBILITY_CATALOG_NO_HIT,
	SG_HOOK_VISIBILITY_CATALOG_CLEARANCE_BLOCKED
} sg_hook_visibility_catalog_outcome_t;

typedef uint32_t sg_hook_visibility_catalog_domain_flags_t;
enum
{
	SG_HOOK_VISIBILITY_CATALOG_LOWER_DIMENSIONAL = UINT32_C(1) << 0,
	SG_HOOK_VISIBILITY_CATALOG_EDGE = UINT32_C(1) << 1,
	SG_HOOK_VISIBILITY_CATALOG_VERTEX = UINT32_C(1) << 2,
	SG_HOOK_VISIBILITY_CATALOG_TIE = UINT32_C(1) << 3
};

typedef struct sg_hook_visibility_catalog_evidence_view_s
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
	sg_hook_visibility_feasibility_metrics_t metrics;
	sg_hook_visibility_feasibility_audit_report_t acceptance;
} sg_hook_visibility_catalog_evidence_view_t;

typedef struct sg_hook_visibility_catalog_relation_view_s
{
	uint32_t surface_rule_index;
	const sg_hook_visibility_surface_rule_t *surface_rule;
	const sg_hook_visibility_domain_term_t *domains;
	uint32_t domain_count;
} sg_hook_visibility_catalog_relation_view_t;

typedef struct sg_hook_visibility_catalog_terminal_view_s
{
	sg_hook_visibility_domain_term_t domain;
	sg_hook_visibility_catalog_outcome_t outcome;
	sg_hook_visibility_catalog_domain_flags_t flags;
	uint32_t surface_rule_index;
	const sg_hook_visibility_surface_rule_t *surface_rule;
} sg_hook_visibility_catalog_terminal_view_t;

typedef enum sg_hook_visibility_catalog_error_code_e
{
	SG_HOOK_VISIBILITY_CATALOG_ERROR_NONE = 0,
	SG_HOOK_VISIBILITY_CATALOG_ERROR_INVALID_ARGUMENT,
	SG_HOOK_VISIBILITY_CATALOG_ERROR_PROOF_REJECTED,
	SG_HOOK_VISIBILITY_CATALOG_ERROR_OVERFLOW,
	SG_HOOK_VISIBILITY_CATALOG_ERROR_OUT_OF_MEMORY,
	SG_HOOK_VISIBILITY_CATALOG_ERROR_COPY_DISAGREEMENT
} sg_hook_visibility_catalog_error_code_t;

typedef struct sg_hook_visibility_catalog_error_s
{
	sg_hook_visibility_catalog_error_code_t code;
	uint32_t record;
	sg_hook_visibility_feasibility_audit_code_t proof_code;
} sg_hook_visibility_catalog_error_t;

typedef enum sg_hook_visibility_catalog_audit_code_e
{
	SG_HOOK_VISIBILITY_CATALOG_AUDIT_OK = 0,
	SG_HOOK_VISIBILITY_CATALOG_AUDIT_INVALID_ARGUMENT,
	SG_HOOK_VISIBILITY_CATALOG_AUDIT_STORAGE_DISAGREEMENT,
	SG_HOOK_VISIBILITY_CATALOG_AUDIT_PROOF_REJECTED,
	SG_HOOK_VISIBILITY_CATALOG_AUDIT_ACCEPTANCE_DISAGREEMENT
} sg_hook_visibility_catalog_audit_code_t;

typedef struct sg_hook_visibility_catalog_audit_report_s
{
	sg_hook_visibility_catalog_audit_code_t code;
	uint32_t record;
	sg_hook_visibility_feasibility_audit_report_t proof;
} sg_hook_visibility_catalog_audit_report_t;

int SG_HookVisibilityCatalogBuild(
	const sg_hook_visibility_feasibility_sources_t *sources,
	const sg_hook_visibility_feasibility_catalog_t *accepted_proof,
	sg_hook_visibility_catalog_t **catalog_out,
	sg_hook_visibility_catalog_error_t *error_out);
int SG_HookVisibilityCatalogAudit(
	const sg_hook_visibility_feasibility_sources_t *sources,
	const sg_hook_visibility_catalog_t *catalog,
	sg_hook_visibility_catalog_audit_report_t *report_out);
int SG_HookVisibilityCatalogEvidence(
	const sg_hook_visibility_catalog_t *catalog,
	sg_hook_visibility_catalog_evidence_view_t *evidence_out);
uint32_t SG_HookVisibilityCatalogRelationCount(
	const sg_hook_visibility_catalog_t *catalog);
int SG_HookVisibilityCatalogRelation(
	const sg_hook_visibility_catalog_t *catalog, uint32_t index,
	sg_hook_visibility_catalog_relation_view_t *relation_out);
uint32_t SG_HookVisibilityCatalogTerminalCount(
	const sg_hook_visibility_catalog_t *catalog);
int SG_HookVisibilityCatalogTerminal(
	const sg_hook_visibility_catalog_t *catalog, uint32_t index,
	sg_hook_visibility_catalog_terminal_view_t *terminal_out);
void SG_HookVisibilityCatalogDestroy(sg_hook_visibility_catalog_t *catalog);
const char *SG_HookVisibilityCatalogErrorString(
	sg_hook_visibility_catalog_error_code_t code);
const char *SG_HookVisibilityCatalogAuditCodeString(
	sg_hook_visibility_catalog_audit_code_t code);

#endif
