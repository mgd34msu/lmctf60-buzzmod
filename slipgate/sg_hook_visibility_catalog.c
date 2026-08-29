#include "sg_hook_visibility_catalog.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sg_hook_visibility_feasibility_internal.h"

#define HOOK_CATALOG_MAGIC UINT64_C(0x4856434154303031)

typedef struct catalog_layout_s
{
	size_t controls;
	size_t surface_rules;
	size_t terminals;
	size_t relations;
	size_t relation_terms;
	size_t size;
	uint32_t relation_term_count;
} catalog_layout_t;

struct sg_hook_visibility_catalog_s
{
	uint64_t magic;
	uint64_t magic_inverse;
	size_t allocation_size;
	sg_hook_visibility_feasibility_catalog_t proof;
	sg_hook_visibility_feasibility_audit_report_t acceptance;
};

_Static_assert((int)SG_HOOK_VISIBILITY_CATALOG_HOOKABLE ==
	(int)SG_HOOK_VISIBILITY_TERMINAL_HOOKABLE, "hookable outcome mismatch");
_Static_assert((int)SG_HOOK_VISIBILITY_CATALOG_SKY ==
	(int)SG_HOOK_VISIBILITY_TERMINAL_SKY, "sky outcome mismatch");
_Static_assert((int)SG_HOOK_VISIBILITY_CATALOG_NONHOOKABLE ==
	(int)SG_HOOK_VISIBILITY_TERMINAL_NONHOOKABLE,
	"nonhookable outcome mismatch");
_Static_assert((int)SG_HOOK_VISIBILITY_CATALOG_NO_HIT ==
	(int)SG_HOOK_VISIBILITY_TERMINAL_NO_HIT, "no-hit outcome mismatch");
_Static_assert((int)SG_HOOK_VISIBILITY_CATALOG_CLEARANCE_BLOCKED ==
	(int)SG_HOOK_VISIBILITY_TERMINAL_CLEARANCE_BLOCKED,
	"clearance outcome mismatch");
_Static_assert((int)SG_HOOK_VISIBILITY_CATALOG_LOWER_DIMENSIONAL ==
	(int)SG_HOOK_VISIBILITY_TERMINAL_LOWER_DIMENSIONAL,
	"lower-dimensional flag mismatch");
_Static_assert((int)SG_HOOK_VISIBILITY_CATALOG_EDGE ==
	(int)SG_HOOK_VISIBILITY_TERMINAL_EDGE, "edge flag mismatch");
_Static_assert((int)SG_HOOK_VISIBILITY_CATALOG_VERTEX ==
	(int)SG_HOOK_VISIBILITY_TERMINAL_VERTEX, "vertex flag mismatch");
_Static_assert((int)SG_HOOK_VISIBILITY_CATALOG_TIE ==
	(int)SG_HOOK_VISIBILITY_TERMINAL_TIE, "tie flag mismatch");

static void SetError(sg_hook_visibility_catalog_error_t *error,
	sg_hook_visibility_catalog_error_code_t code, uint32_t record,
	sg_hook_visibility_feasibility_audit_code_t proof_code)
{
	if (!error || error->code != SG_HOOK_VISIBILITY_CATALOG_ERROR_NONE)
		return;
	error->code = code;
	error->record = record;
	error->proof_code = proof_code;
}

static int AddArray(size_t *size, size_t count, size_t element_size,
	size_t alignment, size_t *offset_out)
{
	size_t aligned;

	if (!alignment || (alignment & (alignment - 1U)) != 0U ||
		*size > SIZE_MAX - (alignment - 1U))
		return 0;
	aligned = (*size + alignment - 1U) & ~(alignment - 1U);
	if (element_size && count > (SIZE_MAX - aligned) / element_size)
		return 0;
	*offset_out = aligned;
	*size = aligned + count * element_size;
	return 1;
}

static int CountRelationTerms(
	const sg_hook_visibility_feasibility_catalog_t *proof,
	uint32_t *count_out)
{
	uint32_t relation;
	uint32_t count = 0U;

	for (relation = 0U; relation < proof->relation_count; relation++)
	{
		if (count > UINT32_MAX - proof->relations[relation].term_count)
			return 0;
		count += proof->relations[relation].term_count;
	}
	*count_out = count;
	return 1;
}

static int BuildLayout(const sg_hook_visibility_feasibility_catalog_t *proof,
	catalog_layout_t *layout)
{
	size_t size = sizeof(sg_hook_visibility_catalog_t);

	memset(layout, 0, sizeof(*layout));
	if (!CountRelationTerms(proof, &layout->relation_term_count) ||
		!AddArray(&size, proof->control_count, sizeof(*proof->controls),
			_Alignof(sg_hook_visibility_control_root_t), &layout->controls) ||
		!AddArray(&size, proof->surface_rule_count,
			sizeof(*proof->surface_rules),
			_Alignof(sg_hook_visibility_surface_rule_t),
			&layout->surface_rules) ||
		!AddArray(&size, proof->terminal_count, sizeof(*proof->terminals),
			_Alignof(sg_hook_visibility_terminal_t), &layout->terminals) ||
		!AddArray(&size, proof->relation_count, sizeof(*proof->relations),
			_Alignof(sg_hook_visibility_relation_t), &layout->relations) ||
		!AddArray(&size, layout->relation_term_count,
			sizeof(sg_hook_visibility_domain_term_t),
			_Alignof(sg_hook_visibility_domain_term_t),
			&layout->relation_terms))
		return 0;
	layout->size = size;
	return 1;
}

static int CatalogHeaderValid(const sg_hook_visibility_catalog_t *catalog)
{
	return catalog && catalog->magic == HOOK_CATALOG_MAGIC &&
		catalog->magic_inverse == ~HOOK_CATALOG_MAGIC &&
		catalog->proof.magic == SG_HOOK_VISIBILITY_CATALOG_MAGIC;
}

static void *CatalogAt(sg_hook_visibility_catalog_t *catalog, size_t offset)
{
	return (void *)((unsigned char *)catalog + offset);
}

static const void *CatalogAtConst(
	const sg_hook_visibility_catalog_t *catalog, size_t offset)
{
	return (const void *)((const unsigned char *)catalog + offset);
}

static int CatalogStorageValid(const sg_hook_visibility_catalog_t *catalog)
{
	catalog_layout_t layout;
	const sg_hook_visibility_domain_term_t *next_term;
	uint32_t relation, seen_terms = 0U;

	if (!CatalogHeaderValid(catalog) || !catalog->proof.controls ||
		!catalog->proof.surface_rules || !catalog->proof.terminals ||
		!catalog->proof.relations || !BuildLayout(&catalog->proof, &layout) ||
		layout.size != catalog->allocation_size)
		return 0;
	if ((const void *)catalog->proof.controls !=
			CatalogAtConst(catalog, layout.controls) ||
		(const void *)catalog->proof.surface_rules !=
			CatalogAtConst(catalog, layout.surface_rules) ||
		(const void *)catalog->proof.terminals !=
			CatalogAtConst(catalog, layout.terminals) ||
		(const void *)catalog->proof.relations !=
			CatalogAtConst(catalog, layout.relations) ||
		catalog->proof.terminal_capacity != catalog->proof.terminal_count)
		return 0;
	next_term = CatalogAtConst(catalog, layout.relation_terms);
	for (relation = 0U; relation < catalog->proof.relation_count; relation++)
	{
		const sg_hook_visibility_relation_t *record =
			&catalog->proof.relations[relation];

		if (record->term_capacity != record->term_count ||
			record->terms != next_term)
			return 0;
		seen_terms += record->term_count;
		next_term += record->term_count;
	}
	return seen_terms == layout.relation_term_count;
}

static int AuditReportsEqual(
	const sg_hook_visibility_feasibility_audit_report_t *left,
	const sg_hook_visibility_feasibility_audit_report_t *right)
{
	return left->code == right->code && left->record == right->record &&
		left->producer_identity == right->producer_identity &&
		left->verifier_identity == right->verifier_identity &&
		left->reconstructed_action_tuples == right->reconstructed_action_tuples &&
		left->reconstructed_predicate_domains ==
			right->reconstructed_predicate_domains &&
		left->hookable_terms == right->hookable_terms &&
		left->sky_terms == right->sky_terms &&
		left->nonhookable_terms == right->nonhookable_terms &&
		left->no_hit_terms == right->no_hit_terms &&
		left->clearance_blocked_terms == right->clearance_blocked_terms &&
		left->lower_dimensional_terms == right->lower_dimensional_terms &&
		left->edge_terms == right->edge_terms &&
		left->vertex_terms == right->vertex_terms &&
		left->tie_terms == right->tie_terms;
}

static void CopyProofScalars(sg_hook_visibility_feasibility_catalog_t *target,
	const sg_hook_visibility_feasibility_catalog_t *source)
{
	target->magic = source->magic;
	target->source_digest = source->source_digest;
	target->verifier_source_digest = source->verifier_source_digest;
	target->producer_identity = source->producer_identity;
	target->verifier_identity = source->verifier_identity;
	target->collision_identity = source->collision_identity;
	memcpy(target->world_counts, source->world_counts,
		sizeof(target->world_counts));
	target->origins = source->origins;
	target->stance = source->stance;
	target->fire_law = source->fire_law;
	target->metrics = source->metrics;
}

static void CopyProofArrays(sg_hook_visibility_catalog_t *catalog,
	const sg_hook_visibility_feasibility_catalog_t *source,
	const catalog_layout_t *layout)
{
	sg_hook_visibility_feasibility_catalog_t *target = &catalog->proof;
	sg_hook_visibility_domain_term_t *next_term;
	uint32_t relation;

	target->controls = CatalogAt(catalog, layout->controls);
	target->control_count = source->control_count;
	memcpy(target->controls, source->controls,
		(size_t)target->control_count * sizeof(*target->controls));
	target->surface_rules = CatalogAt(catalog, layout->surface_rules);
	target->surface_rule_count = source->surface_rule_count;
	memcpy(target->surface_rules, source->surface_rules,
		(size_t)target->surface_rule_count * sizeof(*target->surface_rules));
	target->terminals = CatalogAt(catalog, layout->terminals);
	target->terminal_count = source->terminal_count;
	target->terminal_capacity = source->terminal_count;
	memcpy(target->terminals, source->terminals,
		(size_t)target->terminal_count * sizeof(*target->terminals));
	target->relations = CatalogAt(catalog, layout->relations);
	target->relation_count = source->relation_count;
	next_term = CatalogAt(catalog, layout->relation_terms);
	for (relation = 0U; relation < target->relation_count; relation++)
	{
		const sg_hook_visibility_relation_t *from = &source->relations[relation];
		sg_hook_visibility_relation_t *to = &target->relations[relation];

		to->surface_id = from->surface_id;
		to->model_index = from->model_index;
		to->texinfo = from->texinfo;
		to->terms = next_term;
		to->term_count = from->term_count;
		to->term_capacity = from->term_count;
		memcpy(to->terms, from->terms,
			(size_t)to->term_count * sizeof(*to->terms));
		next_term += to->term_count;
	}
}

int SG_HookVisibilityCatalogBuild(
	const sg_hook_visibility_feasibility_sources_t *sources,
	const sg_hook_visibility_feasibility_catalog_t *accepted_proof,
	sg_hook_visibility_catalog_t **catalog_out,
	sg_hook_visibility_catalog_error_t *error_out)
{
	sg_hook_visibility_feasibility_audit_report_t accepted;
	sg_hook_visibility_catalog_audit_report_t copied;
	sg_hook_visibility_catalog_t *catalog;
	catalog_layout_t layout;

	if (error_out)
		memset(error_out, 0, sizeof(*error_out));
	if (!sources || !accepted_proof || !catalog_out || *catalog_out)
	{
		SetError(error_out, SG_HOOK_VISIBILITY_CATALOG_ERROR_INVALID_ARGUMENT,
			0U, SG_HOOK_VISIBILITY_FEASIBILITY_AUDIT_INVALID_ARGUMENT);
		return 0;
	}
	if (!SG_HookVisibilityFeasibilityAudit(sources, accepted_proof, &accepted))
	{
		SetError(error_out, SG_HOOK_VISIBILITY_CATALOG_ERROR_PROOF_REJECTED,
			accepted.record, accepted.code);
		return 0;
	}
	if (!BuildLayout(accepted_proof, &layout))
	{
		SetError(error_out, SG_HOOK_VISIBILITY_CATALOG_ERROR_OVERFLOW, 0U,
			SG_HOOK_VISIBILITY_FEASIBILITY_AUDIT_OK);
		return 0;
	}
	catalog = calloc(1U, layout.size);
	if (!catalog)
	{
		SetError(error_out, SG_HOOK_VISIBILITY_CATALOG_ERROR_OUT_OF_MEMORY, 0U,
			SG_HOOK_VISIBILITY_FEASIBILITY_AUDIT_OK);
		return 0;
	}
	catalog->magic = HOOK_CATALOG_MAGIC;
	catalog->magic_inverse = ~HOOK_CATALOG_MAGIC;
	catalog->allocation_size = layout.size;
	CopyProofScalars(&catalog->proof, accepted_proof);
	CopyProofArrays(catalog, accepted_proof, &layout);
	catalog->acceptance = accepted;
	if (!SG_HookVisibilityCatalogAudit(sources, catalog, &copied))
	{
		SetError(error_out, SG_HOOK_VISIBILITY_CATALOG_ERROR_COPY_DISAGREEMENT,
			copied.record, copied.proof.code);
		SG_HookVisibilityCatalogDestroy(catalog);
		return 0;
	}
	*catalog_out = catalog;
	return 1;
}

int SG_HookVisibilityCatalogAudit(
	const sg_hook_visibility_feasibility_sources_t *sources,
	const sg_hook_visibility_catalog_t *catalog,
	sg_hook_visibility_catalog_audit_report_t *report_out)
{
	sg_hook_visibility_catalog_audit_report_t report;

	memset(&report, 0, sizeof(report));
	report.code = SG_HOOK_VISIBILITY_CATALOG_AUDIT_INVALID_ARGUMENT;
	if (!sources || !catalog || !report_out)
	{
		if (report_out)
			*report_out = report;
		return 0;
	}
	if (!CatalogStorageValid(catalog))
	{
		report.code = SG_HOOK_VISIBILITY_CATALOG_AUDIT_STORAGE_DISAGREEMENT;
		*report_out = report;
		return 0;
	}
	if (!SG_HookVisibilityFeasibilityAudit(sources, &catalog->proof,
			&report.proof))
	{
		report.code = SG_HOOK_VISIBILITY_CATALOG_AUDIT_PROOF_REJECTED;
		report.record = report.proof.record;
		*report_out = report;
		return 0;
	}
	if (!AuditReportsEqual(&catalog->acceptance, &report.proof))
	{
		report.code = SG_HOOK_VISIBILITY_CATALOG_AUDIT_ACCEPTANCE_DISAGREEMENT;
		report.record = report.proof.record;
		*report_out = report;
		return 0;
	}
	report.code = SG_HOOK_VISIBILITY_CATALOG_AUDIT_OK;
	*report_out = report;
	return 1;
}

int SG_HookVisibilityCatalogEvidence(
	const sg_hook_visibility_catalog_t *catalog,
	sg_hook_visibility_catalog_evidence_view_t *evidence_out)
{
	const sg_hook_visibility_feasibility_catalog_t *proof;

	if (!CatalogHeaderValid(catalog) || !evidence_out)
		return 0;
	proof = &catalog->proof;
	memset(evidence_out, 0, sizeof(*evidence_out));
	evidence_out->source_digest = proof->source_digest;
	evidence_out->verifier_source_digest = proof->verifier_source_digest;
	evidence_out->producer_identity = proof->producer_identity;
	evidence_out->verifier_identity = proof->verifier_identity;
	evidence_out->collision_identity = proof->collision_identity;
	memcpy(evidence_out->world_counts, proof->world_counts,
		sizeof(evidence_out->world_counts));
	evidence_out->origins = proof->origins;
	evidence_out->stance = proof->stance;
	evidence_out->fire_law = proof->fire_law;
	evidence_out->controls = proof->controls;
	evidence_out->control_count = proof->control_count;
	evidence_out->surface_rules = proof->surface_rules;
	evidence_out->surface_rule_count = proof->surface_rule_count;
	evidence_out->metrics = proof->metrics;
	evidence_out->acceptance = catalog->acceptance;
	return 1;
}

static uint32_t FindSurfaceRule(
	const sg_hook_visibility_feasibility_catalog_t *proof, uint64_t surface_id)
{
	uint32_t rule;

	for (rule = 0U; rule < proof->surface_rule_count; rule++)
		if (proof->surface_rules[rule].surface_id == surface_id)
			return rule;
	return SG_HOOK_VISIBILITY_CATALOG_INDEX_NONE;
}

uint32_t SG_HookVisibilityCatalogRelationCount(
	const sg_hook_visibility_catalog_t *catalog)
{
	return CatalogHeaderValid(catalog) ? catalog->proof.relation_count : 0U;
}

int SG_HookVisibilityCatalogRelation(
	const sg_hook_visibility_catalog_t *catalog, uint32_t index,
	sg_hook_visibility_catalog_relation_view_t *relation_out)
{
	const sg_hook_visibility_relation_t *relation;
	uint32_t surface_rule;

	if (!CatalogHeaderValid(catalog) || !relation_out ||
		index >= catalog->proof.relation_count)
		return 0;
	relation = &catalog->proof.relations[index];
	surface_rule = FindSurfaceRule(&catalog->proof, relation->surface_id);
	if (surface_rule == SG_HOOK_VISIBILITY_CATALOG_INDEX_NONE)
		return 0;
	relation_out->surface_rule_index = surface_rule;
	relation_out->surface_rule = &catalog->proof.surface_rules[surface_rule];
	relation_out->domains = relation->terms;
	relation_out->domain_count = relation->term_count;
	return 1;
}

uint32_t SG_HookVisibilityCatalogTerminalCount(
	const sg_hook_visibility_catalog_t *catalog)
{
	return CatalogHeaderValid(catalog) ? catalog->proof.terminal_count : 0U;
}

int SG_HookVisibilityCatalogTerminal(
	const sg_hook_visibility_catalog_t *catalog, uint32_t index,
	sg_hook_visibility_catalog_terminal_view_t *terminal_out)
{
	const sg_hook_visibility_terminal_t *terminal;

	if (!CatalogHeaderValid(catalog) || !terminal_out ||
		index >= catalog->proof.terminal_count)
		return 0;
	terminal = &catalog->proof.terminals[index];
	terminal_out->domain = terminal->domain;
	terminal_out->outcome =
		(sg_hook_visibility_catalog_outcome_t)terminal->outcome;
	terminal_out->flags = terminal->flags;
	terminal_out->surface_rule_index = terminal->surface_rule;
	terminal_out->surface_rule = terminal->surface_rule ==
		SG_HOOK_VISIBILITY_CATALOG_INDEX_NONE ? NULL :
		&catalog->proof.surface_rules[terminal->surface_rule];
	return 1;
}

void SG_HookVisibilityCatalogDestroy(sg_hook_visibility_catalog_t *catalog)
{
	if (!catalog)
		return;
	memset(catalog, 0, catalog->allocation_size);
	free(catalog);
}

const char *SG_HookVisibilityCatalogErrorString(
	sg_hook_visibility_catalog_error_code_t code)
{
	switch (code)
	{
	case SG_HOOK_VISIBILITY_CATALOG_ERROR_NONE: return "none";
	case SG_HOOK_VISIBILITY_CATALOG_ERROR_INVALID_ARGUMENT:
		return "invalid argument";
	case SG_HOOK_VISIBILITY_CATALOG_ERROR_PROOF_REJECTED:
		return "feasibility proof rejected";
	case SG_HOOK_VISIBILITY_CATALOG_ERROR_OVERFLOW:
		return "representation overflow";
	case SG_HOOK_VISIBILITY_CATALOG_ERROR_OUT_OF_MEMORY:
		return "out of memory";
	case SG_HOOK_VISIBILITY_CATALOG_ERROR_COPY_DISAGREEMENT:
		return "owned proof copy disagreed";
	default: return "unknown hook visibility catalog error";
	}
}

const char *SG_HookVisibilityCatalogAuditCodeString(
	sg_hook_visibility_catalog_audit_code_t code)
{
	switch (code)
	{
	case SG_HOOK_VISIBILITY_CATALOG_AUDIT_OK: return "ok";
	case SG_HOOK_VISIBILITY_CATALOG_AUDIT_INVALID_ARGUMENT:
		return "invalid argument";
	case SG_HOOK_VISIBILITY_CATALOG_AUDIT_STORAGE_DISAGREEMENT:
		return "storage disagreement";
	case SG_HOOK_VISIBILITY_CATALOG_AUDIT_PROOF_REJECTED:
		return "owned feasibility proof rejected";
	case SG_HOOK_VISIBILITY_CATALOG_AUDIT_ACCEPTANCE_DISAGREEMENT:
		return "acceptance report disagreement";
	default: return "unknown hook visibility catalog audit error";
	}
}
