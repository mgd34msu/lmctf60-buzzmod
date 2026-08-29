#include "sg_static_affordance_catalog.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sg_configuration_audit.h"

#define SG_STATIC_AFFORDANCE_CATALOG_MAGIC UINT64_C(0x5341434154303031)

struct sg_static_affordance_catalog_s
{
	uint64_t magic;
	uint64_t magic_inverse;
	const sg_static_affordance_catalog_t *self;
	sg_static_affordance_catalog_evidence_view_t evidence;
};

static const sg_static_affordance_catalog_authority_t authority_order[] = {
	SG_STATIC_AFFORDANCE_CATALOG_STATIC_VISIBILITY,
	SG_STATIC_AFFORDANCE_CATALOG_WEAPON_STATIC_AFFORDANCE,
	SG_STATIC_AFFORDANCE_CATALOG_HOOK_VISIBILITY
};

_Static_assert(sizeof(authority_order) / sizeof(authority_order[0]) ==
	SG_STATIC_AFFORDANCE_CATALOG_AUTHORITY_COUNT,
	"static-affordance authority order must be complete");
_Static_assert((int)SG_STATIC_AFFORDANCE_CATALOG_AUTHORITY_COUNT_VALUE ==
	(int)SG_STATIC_AFFORDANCE_CATALOG_AUTHORITY_COUNT,
	"static-affordance authority count mismatch");
_Static_assert((int)SG_HOOK_VISIBILITY_CATALOG_HOOKABLE == 0,
	"hook outcome order mismatch");
_Static_assert((int)SG_HOOK_VISIBILITY_CATALOG_CLEARANCE_BLOCKED == 4,
	"hook outcome order mismatch");

static void SetError(sg_static_affordance_catalog_error_t *error_out,
	sg_static_affordance_catalog_error_code_t code,
	sg_static_affordance_catalog_authority_t authority)
{
	if (!error_out || error_out->code != SG_STATIC_AFFORDANCE_CATALOG_ERROR_NONE)
		return;
	error_out->code = code;
	error_out->authority = authority;
}

static int IdentityEqual(const sg_rune_model_identity_t *left,
	const sg_rune_model_identity_t *right)
{
	return left->bsp_content_id == right->bsp_content_id &&
		left->entity_semantics_id == right->entity_semantics_id &&
		left->physics_abi_id == right->physics_abi_id &&
		left->source_set_identity == right->source_set_identity &&
		left->schema_id == right->schema_id &&
		left->producer_identity == right->producer_identity &&
		memcmp(&left->standing_hull, &right->standing_hull,
			sizeof(left->standing_hull)) == 0 &&
		memcmp(&left->crouching_hull, &right->crouching_hull,
			sizeof(left->crouching_hull)) == 0 &&
		memcmp(&left->physics, &right->physics,
			sizeof(left->physics)) == 0;
}

static int PublicationSourcesAccepted(
	const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *configuration,
	const sg_configuration_semantics_t *semantics,
	const sg_static_visibility_t *visibility)
{
	sg_configuration_audit_result_t configuration_audit;
	sg_configuration_semantics_audit_result_t semantics_audit;
	sg_static_visibility_audit_result_t visibility_audit;

	return authority && configuration && semantics && visibility &&
		authority->identity.source_set_identity != 0U &&
		IdentityEqual(&authority->identity, &configuration->identity) &&
		IdentityEqual(&authority->identity, &semantics->identity) &&
		IdentityEqual(&authority->identity, &visibility->identity) &&
		SG_ConfigurationAudit(authority, configuration, &configuration_audit) &&
		SG_ConfigurationSemanticsAudit(authority, configuration, semantics,
			&semantics_audit) &&
		SG_StaticVisibilityAudit(authority, configuration, semantics, visibility,
			&visibility_audit);
}

static uint64_t HookOutcomeCount(
	const sg_hook_visibility_feasibility_audit_report_t *acceptance)
{
	return (uint64_t)acceptance->hookable_terms +
		(uint64_t)acceptance->sky_terms +
		(uint64_t)acceptance->nonhookable_terms +
		(uint64_t)acceptance->no_hit_terms +
		(uint64_t)acceptance->clearance_blocked_terms;
}

static int HookEvidenceValid(
	const sg_static_affordance_catalog_hook_evidence_t *hook)
{
	uint64_t outcomes;

	if (!hook || hook->acceptance.code !=
			SG_HOOK_VISIBILITY_FEASIBILITY_AUDIT_OK ||
		hook->producer_identity == 0U || hook->verifier_identity == 0U ||
		hook->producer_identity == hook->verifier_identity ||
		hook->acceptance.producer_identity != hook->producer_identity ||
		hook->acceptance.verifier_identity != hook->verifier_identity ||
		hook->metrics.relation_count != hook->relation_count ||
		hook->metrics.legal_action_tuples !=
			hook->acceptance.reconstructed_action_tuples ||
		hook->metrics.predicate_domains !=
			hook->acceptance.reconstructed_predicate_domains)
		return 0;
	outcomes = HookOutcomeCount(&hook->acceptance);
	if (outcomes > UINT32_MAX || (uint32_t)outcomes != hook->terminal_count ||
		hook->acceptance.clearance_blocked_terms > hook->terminal_count ||
		hook->acceptance.hookable_terms > hook->terminal_count ||
		hook->metrics.muzzle_clearance_traces != hook->terminal_count ||
		hook->metrics.first_hit_traces != hook->terminal_count -
			hook->acceptance.clearance_blocked_terms ||
		hook->metrics.complement_term_count != hook->terminal_count -
			hook->acceptance.hookable_terms)
		return 0;
	return 1;
}

static int CatalogHeaderValid(const sg_static_affordance_catalog_t *catalog)
{
	return catalog && catalog->magic == SG_STATIC_AFFORDANCE_CATALOG_MAGIC &&
		catalog->magic_inverse == ~SG_STATIC_AFFORDANCE_CATALOG_MAGIC &&
		catalog->self == catalog;
}

static int CopyHookEvidence(const sg_hook_visibility_catalog_t *catalog,
	sg_static_affordance_catalog_hook_evidence_t *hook_out)
{
	sg_hook_visibility_catalog_evidence_view_t evidence;

	if (!catalog || !hook_out || !SG_HookVisibilityCatalogEvidence(catalog,
			&evidence))
		return 0;
	memset(hook_out, 0, sizeof(*hook_out));
	hook_out->source_digest = evidence.source_digest;
	hook_out->verifier_source_digest = evidence.verifier_source_digest;
	hook_out->producer_identity = evidence.producer_identity;
	hook_out->verifier_identity = evidence.verifier_identity;
	hook_out->collision_identity = evidence.collision_identity;
	memcpy(hook_out->world_counts, evidence.world_counts,
		sizeof(hook_out->world_counts));
	hook_out->origins = evidence.origins;
	hook_out->stance = evidence.stance;
	hook_out->fire_law = evidence.fire_law;
	hook_out->terminal_count = SG_HookVisibilityCatalogTerminalCount(catalog);
	hook_out->relation_count = SG_HookVisibilityCatalogRelationCount(catalog);
	hook_out->metrics = evidence.metrics;
	hook_out->acceptance = evidence.acceptance;
	return HookEvidenceValid(hook_out);
}

int SG_StaticAffordanceCatalogIssue(
	const sg_static_affordance_catalog_input_t *input,
	sg_static_affordance_catalog_t **catalog_out,
	sg_static_affordance_catalog_error_t *error_out)
{
	sg_static_affordance_catalog_audit_report_t copied;
	sg_static_affordance_catalog_evidence_view_t evidence;
	sg_static_affordance_catalog_t *catalog;
	sg_weapon_static_binding_t weapon_binding;
	const sg_static_visibility_publication_t *weapon_publication;
	const sg_host_collision_authority_t *authority;
	const sg_configuration_space_t *configuration;
	const sg_configuration_semantics_t *semantics;
	const sg_static_visibility_t *visibility;
	uint64_t revision;

	if (error_out)
		memset(error_out, 0, sizeof(*error_out));
	if (!input || !catalog_out || *catalog_out ||
		!input->static_visibility || !input->weapon_context ||
		!input->hook_catalog)
	{
		SetError(error_out, SG_STATIC_AFFORDANCE_CATALOG_ERROR_INVALID_ARGUMENT,
			SG_STATIC_AFFORDANCE_CATALOG_STATIC_VISIBILITY);
		return 0;
	}
	if (!SG_StaticVisibilityPublicationRead(input->static_visibility,
			&authority, &configuration, &semantics, &visibility, &revision) ||
		revision == 0U || !PublicationSourcesAccepted(authority, configuration,
			semantics, visibility))
	{
		SetError(error_out,
			SG_STATIC_AFFORDANCE_CATALOG_ERROR_STATIC_VISIBILITY_REJECTED,
			SG_STATIC_AFFORDANCE_CATALOG_STATIC_VISIBILITY);
		return 0;
	}
	if (!SG_WeaponStaticContextSource(input->weapon_context, &weapon_binding,
			&weapon_publication) || !SG_WeaponStaticBindingValid(&weapon_binding))
	{
		SetError(error_out,
			SG_STATIC_AFFORDANCE_CATALOG_ERROR_WEAPON_CONTEXT_REJECTED,
			SG_STATIC_AFFORDANCE_CATALOG_WEAPON_STATIC_AFFORDANCE);
		return 0;
	}
	if (weapon_publication != input->static_visibility ||
		weapon_binding.source_set_identity !=
			authority->identity.source_set_identity ||
		weapon_binding.visibility_revision != revision)
	{
		SetError(error_out,
			SG_STATIC_AFFORDANCE_CATALOG_ERROR_SOURCE_MISMATCH,
			SG_STATIC_AFFORDANCE_CATALOG_WEAPON_STATIC_AFFORDANCE);
		return 0;
	}
	memset(&evidence, 0, sizeof(evidence));
	evidence.coverage = SG_STATIC_AFFORDANCE_CATALOG_AUDIT_ONLY;
	evidence.authority_count = SG_STATIC_AFFORDANCE_CATALOG_AUTHORITY_COUNT;
	evidence.static_visibility.identity = authority->identity;
	evidence.static_visibility.revision = revision;
	evidence.static_visibility.partition_count = visibility->partition_count;
	evidence.static_visibility.area_count = visibility->area_count;
	evidence.static_visibility.occluder_count = visibility->occluder_count;
	evidence.static_visibility.surface_count = visibility->surface_count;
	evidence.weapon_binding = weapon_binding;
	if (!CopyHookEvidence(input->hook_catalog, &evidence.hook))
	{
		SetError(error_out,
			SG_STATIC_AFFORDANCE_CATALOG_ERROR_HOOK_CATALOG_REJECTED,
			SG_STATIC_AFFORDANCE_CATALOG_HOOK_VISIBILITY);
		return 0;
	}
	if (!IdentityEqual(&authority->identity, &evidence.hook.collision_identity))
	{
		SetError(error_out,
			SG_STATIC_AFFORDANCE_CATALOG_ERROR_SOURCE_MISMATCH,
			SG_STATIC_AFFORDANCE_CATALOG_HOOK_VISIBILITY);
		return 0;
	}
	catalog = calloc(1U, sizeof(*catalog));
	if (!catalog)
	{
		SetError(error_out, SG_STATIC_AFFORDANCE_CATALOG_ERROR_OUT_OF_MEMORY,
			SG_STATIC_AFFORDANCE_CATALOG_STATIC_VISIBILITY);
		return 0;
	}
	catalog->magic = SG_STATIC_AFFORDANCE_CATALOG_MAGIC;
	catalog->magic_inverse = ~SG_STATIC_AFFORDANCE_CATALOG_MAGIC;
	catalog->self = catalog;
	catalog->evidence = evidence;
	if (!SG_StaticAffordanceCatalogAudit(catalog, &copied))
	{
		SetError(error_out,
			SG_STATIC_AFFORDANCE_CATALOG_ERROR_COPY_DISAGREEMENT,
			copied.authority);
		SG_StaticAffordanceCatalogDestroy(catalog);
		return 0;
	}
	*catalog_out = catalog;
	return 1;
}

int SG_StaticAffordanceCatalogAudit(
	const sg_static_affordance_catalog_t *catalog,
	sg_static_affordance_catalog_audit_report_t *report_out)
{
	sg_static_affordance_catalog_audit_report_t report;
	const sg_static_affordance_catalog_evidence_view_t *evidence;

	memset(&report, 0, sizeof(report));
	report.code = SG_STATIC_AFFORDANCE_CATALOG_AUDIT_INVALID_ARGUMENT;
	if (!catalog || !report_out)
	{
		if (report_out)
			*report_out = report;
		return 0;
	}
	if (!CatalogHeaderValid(catalog))
	{
		report.code = SG_STATIC_AFFORDANCE_CATALOG_AUDIT_STORAGE_DISAGREEMENT;
		*report_out = report;
		return 0;
	}
	evidence = &catalog->evidence;
	if (evidence->coverage != SG_STATIC_AFFORDANCE_CATALOG_AUDIT_ONLY ||
		evidence->authority_count !=
			SG_STATIC_AFFORDANCE_CATALOG_AUTHORITY_COUNT)
	{
		report.code = SG_STATIC_AFFORDANCE_CATALOG_AUDIT_COVERAGE_DISAGREEMENT;
		*report_out = report;
		return 0;
	}
	if (evidence->static_visibility.identity.source_set_identity == 0U ||
		evidence->static_visibility.revision == 0U ||
		!SG_WeaponStaticBindingValid(&evidence->weapon_binding) ||
		evidence->weapon_binding.source_set_identity !=
			evidence->static_visibility.identity.source_set_identity ||
		evidence->weapon_binding.visibility_revision !=
			evidence->static_visibility.revision ||
		!IdentityEqual(&evidence->static_visibility.identity,
			&evidence->hook.collision_identity))
	{
		report.code = SG_STATIC_AFFORDANCE_CATALOG_AUDIT_SOURCE_MISMATCH;
		report.authority = SG_STATIC_AFFORDANCE_CATALOG_WEAPON_STATIC_AFFORDANCE;
		*report_out = report;
		return 0;
	}
	if (!HookEvidenceValid(&evidence->hook))
	{
		report.code = SG_STATIC_AFFORDANCE_CATALOG_AUDIT_COMPLEMENT_DISAGREEMENT;
		report.authority = SG_STATIC_AFFORDANCE_CATALOG_HOOK_VISIBILITY;
		*report_out = report;
		return 0;
	}
	report.code = SG_STATIC_AFFORDANCE_CATALOG_AUDIT_OK;
	*report_out = report;
	return 1;
}

int SG_StaticAffordanceCatalogEvidence(
	const sg_static_affordance_catalog_t *catalog,
	sg_static_affordance_catalog_evidence_view_t *evidence_out)
{
	sg_static_affordance_catalog_audit_report_t audit;

	if (!evidence_out || !SG_StaticAffordanceCatalogAudit(catalog, &audit))
		return 0;
	*evidence_out = catalog->evidence;
	return 1;
}

uint32_t SG_StaticAffordanceCatalogAuthorityCount(
	const sg_static_affordance_catalog_t *catalog)
{
	return CatalogHeaderValid(catalog) ?
		SG_STATIC_AFFORDANCE_CATALOG_AUTHORITY_COUNT : 0U;
}

int SG_StaticAffordanceCatalogAuthority(
	const sg_static_affordance_catalog_t *catalog, uint32_t index,
	sg_static_affordance_catalog_authority_t *authority_out)
{
	if (!CatalogHeaderValid(catalog) || !authority_out ||
		index >= SG_STATIC_AFFORDANCE_CATALOG_AUTHORITY_COUNT)
		return 0;
	*authority_out = authority_order[index];
	return 1;
}

int SG_StaticAffordanceCatalogHookOutcomeCount(
	const sg_static_affordance_catalog_t *catalog,
	sg_hook_visibility_catalog_outcome_t outcome, uint32_t *count_out)
{
	const sg_hook_visibility_feasibility_audit_report_t *acceptance;

	if (!CatalogHeaderValid(catalog) || !count_out)
		return 0;
	acceptance = &catalog->evidence.hook.acceptance;
	switch (outcome)
	{
	case SG_HOOK_VISIBILITY_CATALOG_HOOKABLE:
		*count_out = acceptance->hookable_terms;
		return 1;
	case SG_HOOK_VISIBILITY_CATALOG_SKY:
		*count_out = acceptance->sky_terms;
		return 1;
	case SG_HOOK_VISIBILITY_CATALOG_NONHOOKABLE:
		*count_out = acceptance->nonhookable_terms;
		return 1;
	case SG_HOOK_VISIBILITY_CATALOG_NO_HIT:
		*count_out = acceptance->no_hit_terms;
		return 1;
	case SG_HOOK_VISIBILITY_CATALOG_CLEARANCE_BLOCKED:
		*count_out = acceptance->clearance_blocked_terms;
		return 1;
	default:
		return 0;
	}
}

void SG_StaticAffordanceCatalogDestroy(sg_static_affordance_catalog_t *catalog)
{
	if (!catalog)
		return;
	memset(catalog, 0, sizeof(*catalog));
	free(catalog);
}

const char *SG_StaticAffordanceCatalogErrorString(
	sg_static_affordance_catalog_error_code_t code)
{
	switch (code)
	{
	case SG_STATIC_AFFORDANCE_CATALOG_ERROR_NONE: return "none";
	case SG_STATIC_AFFORDANCE_CATALOG_ERROR_INVALID_ARGUMENT:
		return "invalid argument";
	case SG_STATIC_AFFORDANCE_CATALOG_ERROR_STATIC_VISIBILITY_REJECTED:
		return "static visibility source rejected";
	case SG_STATIC_AFFORDANCE_CATALOG_ERROR_WEAPON_CONTEXT_REJECTED:
		return "weapon context rejected";
	case SG_STATIC_AFFORDANCE_CATALOG_ERROR_SOURCE_MISMATCH:
		return "static-affordance source mismatch";
	case SG_STATIC_AFFORDANCE_CATALOG_ERROR_HOOK_CATALOG_REJECTED:
		return "hook visibility catalog rejected";
	case SG_STATIC_AFFORDANCE_CATALOG_ERROR_COMPLEMENT_DISAGREEMENT:
		return "hook complement outcomes disagreed";
	case SG_STATIC_AFFORDANCE_CATALOG_ERROR_OVERFLOW:
		return "representation overflow";
	case SG_STATIC_AFFORDANCE_CATALOG_ERROR_OUT_OF_MEMORY:
		return "out of memory";
	case SG_STATIC_AFFORDANCE_CATALOG_ERROR_COPY_DISAGREEMENT:
		return "owned audit copy disagreed";
	default: return "unknown static-affordance catalog error";
	}
}

const char *SG_StaticAffordanceCatalogAuditCodeString(
	sg_static_affordance_catalog_audit_code_t code)
{
	switch (code)
	{
	case SG_STATIC_AFFORDANCE_CATALOG_AUDIT_OK: return "ok";
	case SG_STATIC_AFFORDANCE_CATALOG_AUDIT_INVALID_ARGUMENT:
		return "invalid argument";
	case SG_STATIC_AFFORDANCE_CATALOG_AUDIT_STORAGE_DISAGREEMENT:
		return "storage disagreement";
	case SG_STATIC_AFFORDANCE_CATALOG_AUDIT_SOURCE_MISMATCH:
		return "source mismatch";
	case SG_STATIC_AFFORDANCE_CATALOG_AUDIT_COMPLEMENT_DISAGREEMENT:
		return "hook complement outcomes disagreed";
	case SG_STATIC_AFFORDANCE_CATALOG_AUDIT_COVERAGE_DISAGREEMENT:
		return "unsupported coverage claim";
	default: return "unknown static-affordance catalog audit error";
	}
}
