#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../slipgate/sg_static_affordance_catalog.h"
#include "sg_hook_visibility_feasibility_fixture.h"
#include "sg_weapon_static_affordance_fixture.h"

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

static int BuildHookCatalog(hook_visibility_fixture_t *fixture,
	const sg_rune_model_identity_t *identity,
	sg_hook_visibility_catalog_t **catalog_out)
{
	sg_hook_visibility_feasibility_catalog_t *proof = NULL;
	sg_hook_visibility_feasibility_error_t proof_error;
	sg_hook_visibility_catalog_error_t catalog_error;
	sg_host_collision_error_t collision_error;

	if (!fixture || !catalog_out || *catalog_out ||
		!HookVisibilityFixtureInit(fixture))
		return 0;
	if (identity && !SG_HostCollisionInit(&fixture->authority, &fixture->world,
			identity, &collision_error))
		return 0;
	if (!SG_HookVisibilityFeasibilityBuild(&fixture->sources, &proof,
			&proof_error))
		return 0;
	if (!SG_HookVisibilityCatalogBuild(&fixture->sources, proof, catalog_out,
			&catalog_error))
	{
		SG_HookVisibilityFeasibilityDestroy(proof);
		return 0;
	}
	SG_HookVisibilityFeasibilityDestroy(proof);
	return 1;
}

static uint32_t HookOutcomeCount(const sg_hook_visibility_catalog_t *catalog,
	sg_hook_visibility_catalog_outcome_t outcome)
{
	uint32_t count = 0U;
	uint32_t index;

	for (index = 0U; index < SG_HookVisibilityCatalogTerminalCount(catalog);
		index++)
	{
		sg_hook_visibility_catalog_terminal_view_t terminal;

		if (SG_HookVisibilityCatalogTerminal(catalog, index, &terminal) &&
			terminal.outcome == outcome)
			count++;
	}
	return count;
}

static void CheckEvidence(const built_fixture_t *weapon,
	const sg_hook_visibility_catalog_t *hook,
	const sg_static_affordance_catalog_t *catalog)
{
	sg_static_affordance_catalog_evidence_view_t evidence;
	sg_static_affordance_catalog_authority_t authority;
	uint32_t index;

	CHECK(SG_StaticAffordanceCatalogAudit(catalog,
		&(sg_static_affordance_catalog_audit_report_t){0}));
	CHECK(SG_StaticAffordanceCatalogEvidence(catalog, &evidence));
	CHECK(evidence.coverage == SG_STATIC_AFFORDANCE_CATALOG_AUDIT_ONLY);
	CHECK(evidence.authority_count ==
		SG_STATIC_AFFORDANCE_CATALOG_AUTHORITY_COUNT);
	CHECK(IdentityEqual(&evidence.static_visibility.identity,
		&weapon->fixture.authority.identity));
	CHECK(evidence.static_visibility.revision == weapon->binding.visibility_revision);
	CHECK(evidence.static_visibility.partition_count ==
		weapon->visibility->partition_count);
	CHECK(evidence.static_visibility.area_count == weapon->visibility->area_count);
	CHECK(evidence.static_visibility.occluder_count ==
		weapon->visibility->occluder_count);
	CHECK(evidence.static_visibility.surface_count ==
		weapon->visibility->surface_count);
	CHECK(SG_RuneV2ContentIdEqual(&evidence.weapon_binding.artifact_identity,
		&weapon->binding.artifact_identity));
	CHECK(SG_RuneV2ContentIdEqual(&evidence.weapon_binding.bsp_identity,
		&weapon->binding.bsp_identity));
	CHECK(SG_RuneV2ContentIdEqual(&evidence.weapon_binding.schema_identity,
		&weapon->binding.schema_identity));
	CHECK(evidence.weapon_binding.source_set_identity ==
		weapon->binding.source_set_identity);
	CHECK(evidence.weapon_binding.visibility_revision ==
		weapon->binding.visibility_revision);
	CHECK(evidence.hook.terminal_count ==
		SG_HookVisibilityCatalogTerminalCount(hook));
	CHECK(evidence.hook.relation_count ==
		SG_HookVisibilityCatalogRelationCount(hook));
	CHECK(evidence.hook.metrics.complement_term_count ==
		evidence.hook.terminal_count - evidence.hook.acceptance.hookable_terms);
	for (index = 0U;
		index < SG_STATIC_AFFORDANCE_CATALOG_AUTHORITY_COUNT; index++)
	{
		CHECK(SG_StaticAffordanceCatalogAuthority(catalog, index,
			&authority));
		CHECK((uint32_t)authority == index);
	}
	CHECK(!SG_StaticAffordanceCatalogAuthority(catalog,
		SG_STATIC_AFFORDANCE_CATALOG_AUTHORITY_COUNT, &authority));
	for (index = (uint32_t)SG_HOOK_VISIBILITY_CATALOG_HOOKABLE;
		index <= (uint32_t)SG_HOOK_VISIBILITY_CATALOG_CLEARANCE_BLOCKED;
		index++)
	{
		uint32_t published_count = UINT32_MAX;

		CHECK(SG_StaticAffordanceCatalogHookOutcomeCount(catalog,
			(sg_hook_visibility_catalog_outcome_t)index, &published_count));
		CHECK(published_count == HookOutcomeCount(hook,
			(sg_hook_visibility_catalog_outcome_t)index));
		CHECK(published_count > 0U);
	}
}

static void CheckOwnedSnapshotSurvivesPredecessors(void)
{
	built_fixture_t weapon;
	hook_visibility_fixture_t hook_fixture;
	sg_hook_visibility_catalog_t *hook = NULL;
	sg_static_affordance_catalog_t *catalog = NULL;
	sg_static_affordance_catalog_input_t input;
	sg_static_affordance_catalog_error_t error;
	sg_static_affordance_catalog_audit_report_t audit;
	sg_static_affordance_catalog_evidence_view_t before, after;
	sg_weapon_static_binding_t source_binding;
	const sg_static_visibility_publication_t *source_publication;

	CHECK(BuildFixture(&weapon, 1, 0, 0, 1, 0, 0.0f));
	if (!weapon.context)
	{
		DestroyFixture(&weapon);
		return;
	}
	CHECK(SG_WeaponStaticContextSource(weapon.context, &source_binding,
		&source_publication));
	CHECK(source_publication == weapon.visibility_publication);
	CHECK(source_binding.visibility_revision == weapon.binding.visibility_revision);
	CHECK(BuildHookCatalog(&hook_fixture, &weapon.fixture.authority.identity,
		&hook));
	if (!hook)
	{
		DestroyFixture(&weapon);
		return;
	}
	memset(&input, 0, sizeof(input));
	input.static_visibility = weapon.visibility_publication;
	input.weapon_context = weapon.context;
	input.hook_catalog = hook;
	CHECK(SG_StaticAffordanceCatalogIssue(&input, &catalog, &error));
	if (!catalog)
	{
		SG_HookVisibilityCatalogDestroy(hook);
		DestroyFixture(&weapon);
		return;
	}
	CheckEvidence(&weapon, hook, catalog);
	CHECK(SG_StaticAffordanceCatalogEvidence(catalog, &before));
	SG_HookVisibilityCatalogDestroy(hook);
	DestroyFixture(&weapon);
	CHECK(SG_StaticAffordanceCatalogAudit(catalog, &audit));
	CHECK(audit.code == SG_STATIC_AFFORDANCE_CATALOG_AUDIT_OK);
	CHECK(SG_StaticAffordanceCatalogEvidence(catalog, &after));
	CHECK(after.coverage == before.coverage);
	CHECK(after.authority_count == before.authority_count);
	CHECK(IdentityEqual(&after.static_visibility.identity,
		&before.static_visibility.identity));
	CHECK(after.static_visibility.revision == before.static_visibility.revision);
	CHECK(after.hook.terminal_count == before.hook.terminal_count);
	CHECK(after.hook.relation_count == before.hook.relation_count);
	CHECK(after.hook.acceptance.no_hit_terms == before.hook.acceptance.no_hit_terms);
	CHECK(after.hook.acceptance.clearance_blocked_terms ==
		before.hook.acceptance.clearance_blocked_terms);
	SG_StaticAffordanceCatalogDestroy(catalog);
}

static void CheckSourceBindingRejections(void)
{
	built_fixture_t weapon;
	hook_visibility_fixture_t matching_fixture, foreign_fixture;
	sg_hook_visibility_catalog_t *matching_hook = NULL;
	sg_hook_visibility_catalog_t *foreign_hook = NULL;
	sg_static_visibility_publication_t *other_publication = NULL;
	sg_static_affordance_catalog_t *catalog = NULL;
	sg_static_affordance_catalog_input_t input;
	sg_static_affordance_catalog_error_t error;

	CHECK(BuildFixture(&weapon, 1, 0, 0, 1, 0, 0.0f));
	if (!weapon.context)
	{
		DestroyFixture(&weapon);
		return;
	}
	CHECK(BuildHookCatalog(&matching_fixture, &weapon.fixture.authority.identity,
		&matching_hook));
	CHECK(BuildHookCatalog(&foreign_fixture, NULL, &foreign_hook));
	if (!matching_hook || !foreign_hook)
	{
		SG_HookVisibilityCatalogDestroy(matching_hook);
		SG_HookVisibilityCatalogDestroy(foreign_hook);
		DestroyFixture(&weapon);
		return;
	}
	memset(&input, 0, sizeof(input));
	input.static_visibility = weapon.visibility_publication;
	input.weapon_context = weapon.context;
	input.hook_catalog = foreign_hook;
	CHECK(!SG_StaticAffordanceCatalogIssue(&input, &catalog, &error));
	CHECK(catalog == NULL);
	CHECK(error.code == SG_STATIC_AFFORDANCE_CATALOG_ERROR_SOURCE_MISMATCH);
	CHECK(error.authority == SG_STATIC_AFFORDANCE_CATALOG_HOOK_VISIBILITY);
	CHECK(SG_StaticVisibilityPublicationIssue(&weapon.fixture.authority,
		weapon.configuration, weapon.semantics, weapon.visibility,
		weapon.binding.visibility_revision, &other_publication));
	input.static_visibility = other_publication;
	input.hook_catalog = matching_hook;
	CHECK(!SG_StaticAffordanceCatalogIssue(&input, &catalog, &error));
	CHECK(catalog == NULL);
	CHECK(error.code == SG_STATIC_AFFORDANCE_CATALOG_ERROR_SOURCE_MISMATCH);
	CHECK(error.authority ==
		SG_STATIC_AFFORDANCE_CATALOG_WEAPON_STATIC_AFFORDANCE);
	SG_StaticVisibilityPublicationDestroy(other_publication);
	SG_HookVisibilityCatalogDestroy(matching_hook);
	SG_HookVisibilityCatalogDestroy(foreign_hook);
	DestroyFixture(&weapon);
}

static void CheckArgumentAndStringContracts(void)
{
	sg_static_affordance_catalog_t *catalog = NULL;
	sg_static_affordance_catalog_error_t error;
	sg_static_affordance_catalog_audit_report_t audit;
	uint32_t code;

	CHECK(!SG_StaticAffordanceCatalogIssue(NULL, &catalog, &error));
	CHECK(error.code == SG_STATIC_AFFORDANCE_CATALOG_ERROR_INVALID_ARGUMENT);
	CHECK(!SG_StaticAffordanceCatalogAudit(NULL, &audit));
	CHECK(audit.code == SG_STATIC_AFFORDANCE_CATALOG_AUDIT_INVALID_ARGUMENT);
	for (code = (uint32_t)SG_STATIC_AFFORDANCE_CATALOG_ERROR_NONE;
		code <= (uint32_t)SG_STATIC_AFFORDANCE_CATALOG_ERROR_COPY_DISAGREEMENT;
		code++)
	{
		CHECK(strcmp(SG_StaticAffordanceCatalogErrorString(
			(sg_static_affordance_catalog_error_code_t)code), "") != 0);
	}
	for (code = (uint32_t)SG_STATIC_AFFORDANCE_CATALOG_AUDIT_OK;
		code <= (uint32_t)SG_STATIC_AFFORDANCE_CATALOG_AUDIT_COVERAGE_DISAGREEMENT;
		code++)
	{
		CHECK(strcmp(SG_StaticAffordanceCatalogAuditCodeString(
			(sg_static_affordance_catalog_audit_code_t)code), "") != 0);
	}
}

int main(void)
{
	CheckOwnedSnapshotSurvivesPredecessors();
	CheckSourceBindingRejections();
	CheckArgumentAndStringContracts();
	if (failures)
		return 1;
	puts("static affordance catalog preserved accepted audit evidence");
	return 0;
}
