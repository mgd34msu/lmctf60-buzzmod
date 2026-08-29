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

static int BuildHookCatalogWithSurfaceOffset(hook_visibility_fixture_t *fixture,
	const sg_rune_model_identity_t *identity, uint64_t surface_offset,
	sg_hook_visibility_catalog_t **catalog_out)
{
	sg_hook_visibility_feasibility_catalog_t *proof = NULL;
	sg_hook_visibility_feasibility_error_t proof_error;
	sg_hook_visibility_catalog_error_t catalog_error;
	sg_host_collision_error_t collision_error;

	if (!fixture || !catalog_out || *catalog_out ||
		!HookVisibilityFixtureInit(fixture))
		return 0;
	fixture->rules[0].surface_id += surface_offset;
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

static int BuildHookCatalog(hook_visibility_fixture_t *fixture,
	const sg_rune_model_identity_t *identity,
	sg_hook_visibility_catalog_t **catalog_out)
{
	return BuildHookCatalogWithSurfaceOffset(fixture, identity, 0U,
		catalog_out);
}

static sg_rune_phase_ref_t PhaseAtPartition(const built_fixture_t *weapon,
	uint32_t partition)
{
	sg_rune_phase_ref_t phase = SG_RUNE_PHASE_REF_NONE;
	const sg_static_visibility_partition_t *visibility_partition;
	const sg_configuration_semantic_region_t *region;
	const sg_configuration_cell_t *configuration_cell;
	uint32_t model_index;

	if (!weapon || partition >= weapon->visibility->partition_count)
		return phase;
	visibility_partition = &weapon->visibility->partitions[partition];
	region = &weapon->semantics->regions[
		visibility_partition->configuration_region];
	configuration_cell = &weapon->configuration->cells[
		visibility_partition->configuration_cell];
	for (model_index = 0U; model_index < weapon->model.cell_count;
		model_index++)
	{
		const sg_rune_cell_t *model_cell = &weapon->model_cells[model_index];
		uint32_t local;

		if (!SG_RuneModelStableIdEqual(&model_cell->id.value,
				&configuration_cell->id.value))
			continue;
		for (local = 0U; local < model_cell->phases.count; local++)
		{
			const sg_rune_phase_basis_t *candidate = &weapon->model_phases[
				model_cell->phases.first + local];

			if (FixturePhaseMatchesRegion(candidate, configuration_cell, region))
				return candidate->id;
		}
	}
	return phase;
}

static int CellsAtPoints(const built_fixture_t *weapon, const float source[3],
	const float target[3], sg_rune_cell_ref_t *source_cell,
	sg_rune_cell_ref_t *target_cell, sg_rune_phase_ref_t *source_phase,
	sg_rune_phase_ref_t *target_phase)
{
	sg_static_visibility_result_t visibility;
	sg_static_visibility_error_t error;

	if (!weapon || !source || !target || !source_cell || !target_cell ||
		!source_phase || !target_phase ||
		!SG_StaticVisibilityQueryPoints(&weapon->fixture.authority,
			&empty_scene, weapon->configuration, weapon->semantics,
			weapon->visibility, source, target, &visibility, &error) ||
		visibility.source_partition >= weapon->visibility->partition_count ||
		visibility.destination_partition >= weapon->visibility->partition_count)
		return 0;
	*source_cell = weapon->configuration->cells[
		weapon->visibility->partitions[visibility.source_partition].
			configuration_cell].id;
	*target_cell = weapon->configuration->cells[
		weapon->visibility->partitions[visibility.destination_partition].
			configuration_cell].id;
	*source_phase = PhaseAtPartition(weapon, visibility.source_partition);
	*target_phase = PhaseAtPartition(weapon, visibility.destination_partition);
	return source_phase->value.source_set_identity != 0U &&
		target_phase->value.source_set_identity != 0U;
}

static sg_rune_bounds_t BoundsAt(const float point[3], float half_extent)
{
	sg_rune_bounds_t bounds;
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
	{
		bounds.mins.value[axis] = point[axis] - half_extent;
		bounds.maxs.value[axis] = point[axis] + half_extent;
	}
	return bounds;
}

static int BuildWeaponCandidate(const built_fixture_t *weapon,
	uint8_t rail_match_active, sg_weapon_static_relation_t requested_relations,
	sg_weapon_static_query_t *query_out, sg_weapon_law_input_t *law_out,
	sg_weapon_profile_t *profile_out,
	sg_weapon_static_affordance_t *candidate_out)
{
	const float source[3] = {-100.0f, 0.0f, 0.0f};
	const float target[3] = {100.0f, 0.0f, 0.0f};
	sg_weapon_static_query_input_t query_input;
	sg_weapon_static_query_t query;
	sg_weapon_law_input_t law;
	sg_weapon_profile_t profile;
	sg_weapon_static_affordance_t candidate;
	sg_rune_cell_ref_t source_cell, target_cell;
	sg_rune_phase_ref_t source_phase, target_phase;

	if (!weapon || !query_out || !law_out || !profile_out || !candidate_out ||
		!CellsAtPoints(weapon, source, target,
		&source_cell, &target_cell, &source_phase, &target_phase))
		return 0;
	memset(&law, 0, sizeof(law));
	law.build_identity = weapon->fixture.identity.producer_identity;
	law.physics_abi_id = weapon->fixture.identity.physics_abi_id;
	law.weapon_balance_compiled = SG_WEAPON_BALANCE_COMPILED;
	law.deathmatch_active = 1U;
	law.rail_match_active = rail_match_active;
	if (!SG_WeaponProfileResolve(SG_WEAPON_PROFILE_RAILGUN,
		&law, &profile))
		return 0;
	memset(&query_input, 0, sizeof(query_input));
	query_input.binding = weapon->binding;
	query_input.source_cell = source_cell;
	query_input.target_cell = target_cell;
	query_input.source_phase = source_phase;
	query_input.target_phase = target_phase;
	memcpy(query_input.source_origin.value, source,
		sizeof(query_input.source_origin.value));
	memcpy(query_input.target_origin.value, target,
		sizeof(query_input.target_origin.value));
	query_input.target_bounds = BoundsAt(target, 16.0f);
	query_input.requested_relations = requested_relations;
	if (!SG_WeaponStaticQueryPrepare(&query_input, &query))
		return 0;
	if (!SG_WeaponStaticAffordanceResolve(weapon->context, &empty_scene,
		&query, &law, profile.id, &candidate,
		&(sg_weapon_static_affordance_error_t){0}))
		return 0;
	*query_out = query;
	*law_out = law;
	*profile_out = profile;
	*candidate_out = candidate;
	return 1;
}

static int BuildWeaponPublication(const built_fixture_t *weapon,
	uint8_t rail_match_active, sg_weapon_static_relation_t requested_relations,
	sg_weapon_static_result_publication_t **publication_out)
{
	sg_weapon_static_result_publication_input_t input;
	sg_weapon_static_query_t query;
	sg_weapon_law_input_t law;
	sg_weapon_profile_t profile;
	sg_weapon_static_affordance_t candidate;
	sg_weapon_static_result_publication_error_t error;

	if (!publication_out || *publication_out ||
		!BuildWeaponCandidate(weapon, rail_match_active, requested_relations,
			&query, &law, &profile, &candidate))
		return 0;
	memset(&input, 0, sizeof(input));
	input.context = weapon->context;
	input.scene = &empty_scene;
	input.query = &query;
	input.law = &law;
	input.profile_id = profile.id;
	input.candidate = &candidate;
	return SG_WeaponStaticResultPublicationIssue(&input, publication_out,
		&error);
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
	const sg_static_affordance_catalog_weapon_evidence_t *weapon_evidence,
	const sg_static_affordance_catalog_t *catalog)
{
	sg_static_affordance_catalog_evidence_view_t evidence;
	sg_static_affordance_catalog_authority_t authority;
	sg_hook_visibility_catalog_evidence_view_t hook_evidence;
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
	CHECK(evidence.static_visibility.partitions != weapon->visibility->partitions);
	CHECK(evidence.static_visibility.area_components !=
		weapon->visibility->area_components);
	CHECK(evidence.static_visibility.occluders != weapon->visibility->occluders);
	CHECK(evidence.static_visibility.surfaces != weapon->visibility->surfaces);
	CHECK(memcmp(evidence.static_visibility.partitions,
		weapon->visibility->partitions,
		(size_t)evidence.static_visibility.partition_count *
			sizeof(*evidence.static_visibility.partitions)) == 0);
	CHECK(memcmp(evidence.static_visibility.area_components,
		weapon->visibility->area_components,
		(size_t)evidence.static_visibility.area_count *
			sizeof(*evidence.static_visibility.area_components)) == 0);
	CHECK(memcmp(evidence.static_visibility.occluders,
		weapon->visibility->occluders,
		(size_t)evidence.static_visibility.occluder_count *
			sizeof(*evidence.static_visibility.occluders)) == 0);
	CHECK(memcmp(evidence.static_visibility.surfaces,
		weapon->visibility->surfaces,
		(size_t)evidence.static_visibility.surface_count *
			sizeof(*evidence.static_visibility.surfaces)) == 0);
	CHECK(evidence.static_visibility.classification_count ==
		(uint64_t)weapon->visibility->partition_count *
			(uint64_t)weapon->visibility->partition_count);
	if (evidence.static_visibility.classification_count != 0U)
	{
		const uint64_t samples[2] = {0U,
			evidence.static_visibility.classification_count - 1U};
		uint32_t sample;

		for (sample = 0U; sample < 2U; sample++)
		{
			sg_static_affordance_catalog_visibility_classification_t published;
			const sg_static_affordance_catalog_visibility_classification_t
				*result = &evidence.static_visibility.classifications[
					samples[sample]];

			CHECK(SG_StaticAffordanceCatalogVisibilityClassification(catalog,
				samples[sample], &published));
			CHECK(memcmp(&published, result, sizeof(published)) == 0);
			CHECK(result->classification <= SG_STATIC_VISIBILITY_CONDITIONAL);
			CHECK(result->reason <= SG_STATIC_VISIBILITY_REASON_SKY);
			CHECK(result->requires_exact_ray <= 1U);
			CHECK(result->requires_area_state <= 1U);
		}
	}
	if (weapon->visibility->partition_count != 0U)
	{
		const uint64_t samples[2] = {0U,
			evidence.static_visibility.classification_count - 1U};
		uint32_t sample;

		for (sample = 0U; sample < 2U; sample++)
		{
			const uint64_t sample_index = samples[sample];
			const uint32_t source = (uint32_t)(sample_index /
				weapon->visibility->partition_count);
			const uint32_t destination = (uint32_t)(sample_index %
				weapon->visibility->partition_count);
			sg_static_visibility_result_t source_result;
			sg_static_visibility_error_t source_error;
			const sg_static_affordance_catalog_visibility_classification_t
				*published = &evidence.static_visibility.classifications[
					sample_index];

			CHECK(SG_StaticVisibilityQueryRegions(&weapon->fixture.authority,
				weapon->configuration, weapon->semantics, weapon->visibility,
				source, destination, &source_result, &source_error));
			CHECK(published->classification == source_result.classification);
			CHECK(published->reason == source_result.reason);
			CHECK(published->requires_exact_ray ==
				source_result.requires_exact_ray);
			CHECK(published->requires_area_state ==
				source_result.requires_area_state);
		}
	}
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
	CHECK(evidence.weapon_count == 1U);
	CHECK(evidence.weapons != weapon_evidence);
	CHECK(memcmp(&evidence.weapons[0], weapon_evidence,
		sizeof(*weapon_evidence)) == 0);
	CHECK(evidence.weapons[0].profile.resolved == 1U);
	CHECK(evidence.weapons[0].affordance.relations[0].relation ==
		SG_WEAPON_STATIC_DIRECT_VISIBILITY);
	CHECK(evidence.weapons[0].affordance.relations[6].relation ==
		SG_WEAPON_STATIC_PERIODIC_PROJECTILE_RAY);
	CHECK(evidence.hook.terminal_count ==
		SG_HookVisibilityCatalogTerminalCount(hook));
	CHECK(evidence.hook.relation_count ==
		SG_HookVisibilityCatalogRelationCount(hook));
	CHECK(evidence.hook.controls != NULL);
	CHECK(evidence.hook.surface_rules != NULL);
	CHECK(evidence.hook.terminals != NULL);
	CHECK(evidence.hook.relations != NULL);
	CHECK(evidence.hook.relation_domains != NULL);
	CHECK(SG_HookVisibilityCatalogEvidence(hook, &hook_evidence));
	CHECK(evidence.hook.controls != hook_evidence.controls);
	CHECK(evidence.hook.surface_rules != hook_evidence.surface_rules);
	if (evidence.hook.control_count != 0U && evidence.hook.controls &&
		hook_evidence.controls)
	{
		CHECK(memcmp(evidence.hook.controls, hook_evidence.controls,
			(size_t)evidence.hook.control_count *
				sizeof(*evidence.hook.controls)) == 0);
	}
	else
		CHECK(evidence.hook.control_count == 0U);
	if (evidence.hook.surface_rule_count != 0U && evidence.hook.surface_rules &&
		hook_evidence.surface_rules)
	{
		CHECK(memcmp(evidence.hook.surface_rules, hook_evidence.surface_rules,
			(size_t)evidence.hook.surface_rule_count *
				sizeof(*evidence.hook.surface_rules)) == 0);
	}
	else
		CHECK(evidence.hook.surface_rule_count == 0U);
	for (index = 0U; index < evidence.hook.terminal_count; index++)
	{
		sg_hook_visibility_catalog_terminal_view_t source;
		sg_static_affordance_catalog_hook_terminal_t published;

		CHECK(SG_HookVisibilityCatalogTerminal(hook, index, &source));
		CHECK(SG_StaticAffordanceCatalogHookTerminal(catalog, index,
			&published));
		CHECK(memcmp(&published.domain, &source.domain,
			sizeof(published.domain)) == 0);
		CHECK(published.outcome == source.outcome);
		CHECK(published.flags == source.flags);
		CHECK(published.surface_rule_index == source.surface_rule_index);
	}
	for (index = 0U; index < evidence.hook.relation_count; index++)
	{
		sg_hook_visibility_catalog_relation_view_t source;
		sg_static_affordance_catalog_hook_relation_t published;
		const sg_hook_visibility_domain_term_t *domains = NULL;

		CHECK(SG_HookVisibilityCatalogRelation(hook, index, &source));
		CHECK(SG_StaticAffordanceCatalogHookRelation(catalog, index,
			&published, &domains));
		CHECK(published.surface_id == source.surface_rule->surface_id);
		CHECK(published.model_index == source.surface_rule->model_index);
		CHECK(published.texinfo == source.surface_rule->texinfo);
		CHECK(published.domain_count == source.domain_count);
		CHECK(memcmp(domains, source.domains,
			(size_t)published.domain_count * sizeof(*domains)) == 0);
	}
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
	sg_static_affordance_catalog_weapon_evidence_t weapon_evidence;
	sg_weapon_static_result_publication_t *weapon_publication = NULL;
	const sg_weapon_static_result_publication_t *weapon_publications[1];
	sg_weapon_static_binding_t source_binding;
	const sg_static_visibility_publication_t *source_publication;

	CHECK(BuildFixture(&weapon, 1, 0, 0, 1, 0, 0.0f));
	if (!weapon.context)
	{
		DestroyFixture(&weapon);
		return;
	}
	CHECK(BuildWeaponPublication(&weapon, 0U,
		SG_WEAPON_STATIC_RELATION_MASK, &weapon_publication));
	CHECK(SG_WeaponStaticResultPublicationEvidence(weapon_publication,
		&weapon_evidence));
	CHECK(SG_WeaponStaticContextSource(weapon.context, &source_binding,
		&source_publication));
	CHECK(source_publication == weapon.visibility_publication);
	CHECK(source_binding.visibility_revision == weapon.binding.visibility_revision);
	CHECK(BuildHookCatalog(&hook_fixture, &weapon.fixture.authority.identity,
		&hook));
	if (!hook)
	{
		SG_WeaponStaticResultPublicationDestroy(weapon_publication);
		DestroyFixture(&weapon);
		return;
	}
	memset(&input, 0, sizeof(input));
	input.static_visibility = weapon.visibility_publication;
	input.weapon_context = weapon.context;
	weapon_publications[0] = weapon_publication;
	input.weapon_publications = weapon_publications;
	input.weapon_count = 1U;
	input.hook_catalog = hook;
	CHECK(SG_StaticAffordanceCatalogIssue(&input, &catalog, &error));
	if (!catalog)
	{
		SG_WeaponStaticResultPublicationDestroy(weapon_publication);
		SG_HookVisibilityCatalogDestroy(hook);
		DestroyFixture(&weapon);
		return;
	}
	CheckEvidence(&weapon, hook, &weapon_evidence, catalog);
	CHECK(SG_StaticAffordanceCatalogEvidence(catalog, &before));
	SG_WeaponStaticResultPublicationDestroy(weapon_publication);
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
	CHECK(after.content_digest == before.content_digest);
	CHECK(after.static_visibility.classification_count ==
		before.static_visibility.classification_count);
	CHECK(memcmp(after.static_visibility.partitions,
		before.static_visibility.partitions,
		(size_t)after.static_visibility.partition_count *
			sizeof(*after.static_visibility.partitions)) == 0);
	CHECK(memcmp(after.static_visibility.area_components,
		before.static_visibility.area_components,
		(size_t)after.static_visibility.area_count *
			sizeof(*after.static_visibility.area_components)) == 0);
	CHECK(memcmp(after.static_visibility.occluders,
		before.static_visibility.occluders,
		(size_t)after.static_visibility.occluder_count *
			sizeof(*after.static_visibility.occluders)) == 0);
	CHECK(memcmp(after.static_visibility.surfaces,
		before.static_visibility.surfaces,
		(size_t)after.static_visibility.surface_count *
			sizeof(*after.static_visibility.surfaces)) == 0);
	CHECK(memcmp(after.static_visibility.classifications,
		before.static_visibility.classifications,
		(size_t)after.static_visibility.classification_count *
			sizeof(*after.static_visibility.classifications)) == 0);
	CHECK(after.weapon_count == before.weapon_count);
	CHECK(memcmp(after.weapons, before.weapons,
		(size_t)after.weapon_count * sizeof(*after.weapons)) == 0);
	CHECK(after.hook.terminal_count == before.hook.terminal_count);
	CHECK(after.hook.relation_count == before.hook.relation_count);
	CHECK(after.hook.relation_domain_count == before.hook.relation_domain_count);
	CHECK(memcmp(after.hook.controls, before.hook.controls,
		(size_t)after.hook.control_count * sizeof(*after.hook.controls)) == 0);
	CHECK(memcmp(after.hook.surface_rules, before.hook.surface_rules,
		(size_t)after.hook.surface_rule_count *
			sizeof(*after.hook.surface_rules)) == 0);
	CHECK(memcmp(after.hook.terminals, before.hook.terminals,
		(size_t)after.hook.terminal_count * sizeof(*after.hook.terminals)) == 0);
	CHECK(memcmp(after.hook.relations, before.hook.relations,
		(size_t)after.hook.relation_count * sizeof(*after.hook.relations)) == 0);
	CHECK(memcmp(after.hook.relation_domains, before.hook.relation_domains,
		(size_t)after.hook.relation_domain_count *
			sizeof(*after.hook.relation_domains)) == 0);
	CHECK(after.hook.acceptance.no_hit_terms == before.hook.acceptance.no_hit_terms);
	CHECK(after.hook.acceptance.clearance_blocked_terms ==
		before.hook.acceptance.clearance_blocked_terms);
	SG_StaticAffordanceCatalogDestroy(catalog);
}

static void CheckEqualCountsKeepDifferentAssignments(void)
{
	built_fixture_t first_weapon, second_weapon;
	hook_visibility_fixture_t first_hook_fixture, second_hook_fixture;
	sg_hook_visibility_catalog_t *first_hook = NULL, *second_hook = NULL;
	sg_static_affordance_catalog_t *first_catalog = NULL, *second_catalog = NULL;
	sg_static_affordance_catalog_input_t first_input, second_input;
	sg_weapon_static_result_publication_t *first_weapon_publication = NULL;
	sg_weapon_static_result_publication_t *second_weapon_publication = NULL;
	const sg_weapon_static_result_publication_t *first_weapon_publications[1];
	const sg_weapon_static_result_publication_t *second_weapon_publications[1];
	sg_static_affordance_catalog_evidence_view_t first_before, second_before;
	sg_static_affordance_catalog_evidence_view_t first_after, second_after;
	sg_static_affordance_catalog_error_t error;
	uint64_t classification;
	int saw_different_classification = 0;

	CHECK(BuildFixture(&first_weapon, 1, 0, 0, 1, 0, 0.0f));
	CHECK(BuildFixture(&second_weapon, 1, 0, 0, 0, 0, 0.0f));
	if (!first_weapon.context || !second_weapon.context)
	{
		DestroyFixture(&first_weapon);
		DestroyFixture(&second_weapon);
		return;
	}
	CHECK(BuildWeaponPublication(&first_weapon, 0U,
		SG_WEAPON_STATIC_RELATION_MASK, &first_weapon_publication));
	CHECK(BuildWeaponPublication(&second_weapon, 1U,
		SG_WEAPON_STATIC_RELATION_MASK, &second_weapon_publication));
	CHECK(BuildHookCatalogWithSurfaceOffset(&first_hook_fixture,
		&first_weapon.fixture.authority.identity, 0U, &first_hook));
	CHECK(BuildHookCatalogWithSurfaceOffset(&second_hook_fixture,
		&second_weapon.fixture.authority.identity, UINT64_C(0x10000),
		&second_hook));
	if (!first_hook || !second_hook)
	{
		SG_WeaponStaticResultPublicationDestroy(first_weapon_publication);
		SG_WeaponStaticResultPublicationDestroy(second_weapon_publication);
		SG_HookVisibilityCatalogDestroy(first_hook);
		SG_HookVisibilityCatalogDestroy(second_hook);
		DestroyFixture(&first_weapon);
		DestroyFixture(&second_weapon);
		return;
	}
	memset(&first_input, 0, sizeof(first_input));
	first_input.static_visibility = first_weapon.visibility_publication;
	first_input.weapon_context = first_weapon.context;
	first_weapon_publications[0] = first_weapon_publication;
	first_input.weapon_publications = first_weapon_publications;
	first_input.weapon_count = 1U;
	first_input.hook_catalog = first_hook;
	memset(&second_input, 0, sizeof(second_input));
	second_input.static_visibility = second_weapon.visibility_publication;
	second_input.weapon_context = second_weapon.context;
	second_weapon_publications[0] = second_weapon_publication;
	second_input.weapon_publications = second_weapon_publications;
	second_input.weapon_count = 1U;
	second_input.hook_catalog = second_hook;
	CHECK(SG_StaticAffordanceCatalogIssue(&first_input, &first_catalog, &error));
	CHECK(SG_StaticAffordanceCatalogIssue(&second_input, &second_catalog, &error));
	if (!first_catalog || !second_catalog)
	{
		SG_StaticAffordanceCatalogDestroy(first_catalog);
		SG_StaticAffordanceCatalogDestroy(second_catalog);
		SG_HookVisibilityCatalogDestroy(first_hook);
		SG_HookVisibilityCatalogDestroy(second_hook);
		SG_WeaponStaticResultPublicationDestroy(first_weapon_publication);
		SG_WeaponStaticResultPublicationDestroy(second_weapon_publication);
		DestroyFixture(&first_weapon);
		DestroyFixture(&second_weapon);
		return;
	}
	CHECK(SG_StaticAffordanceCatalogEvidence(first_catalog, &first_before));
	CHECK(SG_StaticAffordanceCatalogEvidence(second_catalog, &second_before));
	CHECK(first_before.static_visibility.partition_count ==
		second_before.static_visibility.partition_count);
	CHECK(first_before.static_visibility.area_count ==
		second_before.static_visibility.area_count);
	CHECK(first_before.static_visibility.occluder_count ==
		second_before.static_visibility.occluder_count);
	CHECK(first_before.static_visibility.surface_count ==
		second_before.static_visibility.surface_count);
	CHECK(first_before.static_visibility.classification_count ==
		second_before.static_visibility.classification_count);
	CHECK(first_before.weapon_count == second_before.weapon_count);
	CHECK(first_before.hook.terminal_count == second_before.hook.terminal_count);
	CHECK(first_before.hook.relation_count == second_before.hook.relation_count);
	CHECK(first_before.hook.relation_domain_count ==
		second_before.hook.relation_domain_count);
	CHECK(first_before.weapons[0].law.rail_match_active !=
		second_before.weapons[0].law.rail_match_active);
	CHECK(first_before.weapons[0].profile.direct_damage !=
		second_before.weapons[0].profile.direct_damage);
	CHECK(first_before.hook.surface_rules[0].surface_id !=
		second_before.hook.surface_rules[0].surface_id);
	for (classification = 0U;
		classification < first_before.static_visibility.classification_count;
		classification++)
		if (memcmp(&first_before.static_visibility.classifications[
			classification], &second_before.static_visibility.classifications[
			classification], sizeof(*first_before.static_visibility.classifications)) != 0)
			saw_different_classification = 1;
	CHECK(saw_different_classification);
	CHECK(first_before.content_digest != second_before.content_digest);
	SG_WeaponStaticResultPublicationDestroy(first_weapon_publication);
	SG_WeaponStaticResultPublicationDestroy(second_weapon_publication);
	SG_HookVisibilityCatalogDestroy(first_hook);
	SG_HookVisibilityCatalogDestroy(second_hook);
	DestroyFixture(&first_weapon);
	DestroyFixture(&second_weapon);
	CHECK(SG_StaticAffordanceCatalogAudit(first_catalog,
		&(sg_static_affordance_catalog_audit_report_t){0}));
	CHECK(SG_StaticAffordanceCatalogAudit(second_catalog,
		&(sg_static_affordance_catalog_audit_report_t){0}));
	CHECK(SG_StaticAffordanceCatalogEvidence(first_catalog, &first_after));
	CHECK(SG_StaticAffordanceCatalogEvidence(second_catalog, &second_after));
	CHECK(first_after.content_digest == first_before.content_digest);
	CHECK(second_after.content_digest == second_before.content_digest);
	CHECK(first_after.weapons[0].profile.direct_damage !=
		second_after.weapons[0].profile.direct_damage);
	CHECK(first_after.hook.surface_rules[0].surface_id !=
		second_after.hook.surface_rules[0].surface_id);
	SG_StaticAffordanceCatalogDestroy(first_catalog);
	SG_StaticAffordanceCatalogDestroy(second_catalog);
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
	sg_weapon_static_result_publication_t *weapon_publication = NULL;
	const sg_weapon_static_result_publication_t *weapon_publications[1];

	CHECK(BuildFixture(&weapon, 1, 0, 0, 1, 0, 0.0f));
	if (!weapon.context)
	{
		DestroyFixture(&weapon);
		return;
	}
	CHECK(BuildWeaponPublication(&weapon, 0U,
		SG_WEAPON_STATIC_RELATION_MASK, &weapon_publication));
	CHECK(BuildHookCatalog(&matching_fixture, &weapon.fixture.authority.identity,
		&matching_hook));
	CHECK(BuildHookCatalog(&foreign_fixture, NULL, &foreign_hook));
	if (!matching_hook || !foreign_hook)
	{
		SG_WeaponStaticResultPublicationDestroy(weapon_publication);
		SG_HookVisibilityCatalogDestroy(matching_hook);
		SG_HookVisibilityCatalogDestroy(foreign_hook);
		DestroyFixture(&weapon);
		return;
	}
	memset(&input, 0, sizeof(input));
	input.static_visibility = weapon.visibility_publication;
	input.weapon_context = weapon.context;
	weapon_publications[0] = weapon_publication;
	input.weapon_publications = weapon_publications;
	input.weapon_count = 1U;
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
	SG_WeaponStaticResultPublicationDestroy(weapon_publication);
	SG_HookVisibilityCatalogDestroy(matching_hook);
	SG_HookVisibilityCatalogDestroy(foreign_hook);
	DestroyFixture(&weapon);
}

static void CheckForgedWeaponPublicationRejected(void)
{
	built_fixture_t weapon;
	sg_weapon_static_result_publication_t *publication = NULL;
	sg_weapon_static_result_publication_input_t input;
	sg_weapon_static_result_publication_error_t error;
	sg_weapon_static_query_t query;
	sg_weapon_law_input_t law;
	sg_weapon_profile_t profile;
	sg_weapon_static_affordance_t candidate;
	sg_host_collision_instance_t instance;
	sg_host_collision_scene_t nonstatic_scene;
	uint32_t relation;
	int forged = 0;

	CHECK(BuildFixture(&weapon, 0, 0, 0, 1, 0, 0.0f));
	if (!weapon.context)
	{
		DestroyFixture(&weapon);
		return;
	}
	memset(&query, 0, sizeof(query));
	memset(&law, 0, sizeof(law));
	memset(&profile, 0, sizeof(profile));
	memset(&candidate, 0, sizeof(candidate));
	CHECK(BuildWeaponCandidate(&weapon, 0U,
		SG_WEAPON_STATIC_RELATION_MASK, &query, &law, &profile, &candidate));
	memset(&instance, 0, sizeof(instance));
	instance.instance_id = 1U;
	memset(&nonstatic_scene, 0, sizeof(nonstatic_scene));
	nonstatic_scene.instances = &instance;
	nonstatic_scene.instance_count = 1U;
	memset(&input, 0, sizeof(input));
	input.context = weapon.context;
	input.scene = &nonstatic_scene;
	input.query = &query;
	input.law = &law;
	input.profile_id = profile.id;
	input.candidate = &candidate;
	CHECK(!SG_WeaponStaticResultPublicationIssue(&input, &publication,
		&error));
	CHECK(publication == NULL);
	CHECK(error.code ==
		SG_WEAPON_STATIC_RESULT_PUBLICATION_ERROR_STATIC_SCENE_REJECTED);
	SG_WeaponStaticResultPublicationDestroy(publication);
	publication = NULL;
	for (relation = 0U; relation < SG_WEAPON_STATIC_RELATION_COUNT;
		relation++)
		if (candidate.relations[relation].status == SG_WEAPON_STATIC_PROVEN)
		{
			const sg_weapon_static_relation_t bit =
				candidate.relations[relation].relation;

			candidate.relations[relation].status = SG_WEAPON_STATIC_REJECTED;
			candidate.proven_relations &= ~bit;
			candidate.rejected_relations |= bit;
			forged = 1;
			break;
		}
	CHECK(forged);
	input.scene = &empty_scene;
	CHECK(!SG_WeaponStaticResultPublicationIssue(&input, &publication,
		&error));
	CHECK(publication == NULL);
	CHECK(error.code ==
		SG_WEAPON_STATIC_RESULT_PUBLICATION_ERROR_CANDIDATE_REJECTED);
	SG_WeaponStaticResultPublicationDestroy(publication);
	DestroyFixture(&weapon);
}

static void CheckPartialWeaponPublicationRejectedByCatalog(void)
{
	built_fixture_t weapon;
	hook_visibility_fixture_t hook_fixture;
	sg_hook_visibility_catalog_t *hook = NULL;
	sg_weapon_static_result_publication_t *publication = NULL;
	const sg_weapon_static_result_publication_t *publications[1];
	sg_static_affordance_catalog_t *catalog = NULL;
	sg_static_affordance_catalog_input_t input;
	sg_static_affordance_catalog_error_t error;

	CHECK(BuildFixture(&weapon, 1, 0, 0, 1, 0, 0.0f));
	if (!weapon.context)
	{
		DestroyFixture(&weapon);
		return;
	}
	CHECK(BuildWeaponPublication(&weapon, 0U,
		SG_WEAPON_STATIC_DIRECT_VISIBILITY, &publication));
	CHECK(BuildHookCatalog(&hook_fixture, &weapon.fixture.authority.identity,
		&hook));
	if (!publication || !hook)
	{
		SG_WeaponStaticResultPublicationDestroy(publication);
		SG_HookVisibilityCatalogDestroy(hook);
		DestroyFixture(&weapon);
		return;
	}
	memset(&input, 0, sizeof(input));
	publications[0] = publication;
	input.static_visibility = weapon.visibility_publication;
	input.weapon_context = weapon.context;
	input.weapon_publications = publications;
	input.weapon_count = 1U;
	input.hook_catalog = hook;
	CHECK(!SG_StaticAffordanceCatalogIssue(&input, &catalog, &error));
	CHECK(catalog == NULL);
	CHECK(error.code == SG_STATIC_AFFORDANCE_CATALOG_ERROR_WEAPON_EVIDENCE_REJECTED);
	CHECK(error.authority == SG_STATIC_AFFORDANCE_CATALOG_WEAPON_STATIC_AFFORDANCE);
	SG_WeaponStaticResultPublicationDestroy(publication);
	SG_HookVisibilityCatalogDestroy(hook);
	DestroyFixture(&weapon);
}

static void CheckWeaponCanonicalOrderAndDuplicateRejection(void)
{
	built_fixture_t weapon;
	hook_visibility_fixture_t hook_fixture;
	sg_hook_visibility_catalog_t *hook = NULL;
	sg_weapon_static_result_publication_t *first = NULL;
	sg_weapon_static_result_publication_t *second = NULL;
	sg_weapon_static_result_publication_t *equivalent_first = NULL;
	const sg_weapon_static_result_publication_t *forward[2];
	const sg_weapon_static_result_publication_t *reverse[2];
	const sg_weapon_static_result_publication_t *duplicates[2];
	sg_static_affordance_catalog_t *forward_catalog = NULL;
	sg_static_affordance_catalog_t *reverse_catalog = NULL;
	sg_static_affordance_catalog_t *duplicate_catalog = NULL;
	sg_static_affordance_catalog_input_t input;
	sg_static_affordance_catalog_evidence_view_t forward_evidence;
	sg_static_affordance_catalog_evidence_view_t reverse_evidence;
	sg_static_affordance_catalog_error_t error;

	CHECK(BuildFixture(&weapon, 1, 0, 0, 1, 0, 0.0f));
	if (!weapon.context)
	{
		DestroyFixture(&weapon);
		return;
	}
	CHECK(BuildWeaponPublication(&weapon, 0U,
		SG_WEAPON_STATIC_RELATION_MASK, &first));
	CHECK(BuildWeaponPublication(&weapon, 1U,
		SG_WEAPON_STATIC_RELATION_MASK, &second));
	CHECK(BuildWeaponPublication(&weapon, 0U,
		SG_WEAPON_STATIC_RELATION_MASK, &equivalent_first));
	CHECK(BuildHookCatalog(&hook_fixture, &weapon.fixture.authority.identity,
		&hook));
	if (!first || !second || !equivalent_first || !hook)
	{
		SG_WeaponStaticResultPublicationDestroy(first);
		SG_WeaponStaticResultPublicationDestroy(second);
		SG_WeaponStaticResultPublicationDestroy(equivalent_first);
		SG_HookVisibilityCatalogDestroy(hook);
		DestroyFixture(&weapon);
		return;
	}
	memset(&input, 0, sizeof(input));
	input.static_visibility = weapon.visibility_publication;
	input.weapon_context = weapon.context;
	input.weapon_count = 2U;
	input.hook_catalog = hook;
	forward[0] = first;
	forward[1] = second;
	input.weapon_publications = forward;
	CHECK(SG_StaticAffordanceCatalogIssue(&input, &forward_catalog, &error));
	reverse[0] = second;
	reverse[1] = first;
	input.weapon_publications = reverse;
	CHECK(SG_StaticAffordanceCatalogIssue(&input, &reverse_catalog, &error));
	if (!forward_catalog || !reverse_catalog)
	{
		SG_StaticAffordanceCatalogDestroy(forward_catalog);
		SG_StaticAffordanceCatalogDestroy(reverse_catalog);
		SG_WeaponStaticResultPublicationDestroy(first);
		SG_WeaponStaticResultPublicationDestroy(second);
		SG_WeaponStaticResultPublicationDestroy(equivalent_first);
		SG_HookVisibilityCatalogDestroy(hook);
		DestroyFixture(&weapon);
		return;
	}
	CHECK(SG_StaticAffordanceCatalogEvidence(forward_catalog,
		&forward_evidence));
	CHECK(SG_StaticAffordanceCatalogEvidence(reverse_catalog,
		&reverse_evidence));
	CHECK(forward_evidence.weapon_count == 2U);
	CHECK(reverse_evidence.weapon_count == 2U);
	CHECK(forward_evidence.content_digest == reverse_evidence.content_digest);
	CHECK(memcmp(forward_evidence.weapons, reverse_evidence.weapons,
		(size_t)forward_evidence.weapon_count *
			sizeof(*forward_evidence.weapons)) == 0);
	duplicates[0] = first;
	duplicates[1] = equivalent_first;
	input.weapon_publications = duplicates;
	CHECK(!SG_StaticAffordanceCatalogIssue(&input, &duplicate_catalog, &error));
	CHECK(duplicate_catalog == NULL);
	CHECK(error.code == SG_STATIC_AFFORDANCE_CATALOG_ERROR_WEAPON_EVIDENCE_REJECTED);
	CHECK(error.authority == SG_STATIC_AFFORDANCE_CATALOG_WEAPON_STATIC_AFFORDANCE);
	SG_StaticAffordanceCatalogDestroy(duplicate_catalog);
	SG_WeaponStaticResultPublicationDestroy(first);
	SG_WeaponStaticResultPublicationDestroy(second);
	SG_WeaponStaticResultPublicationDestroy(equivalent_first);
	SG_HookVisibilityCatalogDestroy(hook);
	DestroyFixture(&weapon);
	CHECK(SG_StaticAffordanceCatalogAudit(forward_catalog,
		&(sg_static_affordance_catalog_audit_report_t){0}));
	CHECK(SG_StaticAffordanceCatalogAudit(reverse_catalog,
		&(sg_static_affordance_catalog_audit_report_t){0}));
	SG_StaticAffordanceCatalogDestroy(forward_catalog);
	SG_StaticAffordanceCatalogDestroy(reverse_catalog);
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

/* The catalog must accept only weapon-owner publications, never a public
 * sg_weapon_static_affordance_t supplied by a caller. */
static void CheckWeaponPublicationBoundary(void)
{
	sg_weapon_static_result_publication_t *publication = NULL;

	CHECK(!SG_WeaponStaticResultPublicationIssue(NULL, &publication, NULL));
	CHECK(publication == NULL);
}

int main(void)
{
	CheckOwnedSnapshotSurvivesPredecessors();
	CheckEqualCountsKeepDifferentAssignments();
	CheckSourceBindingRejections();
	CheckForgedWeaponPublicationRejected();
	CheckPartialWeaponPublicationRejectedByCatalog();
	CheckWeaponCanonicalOrderAndDuplicateRejection();
	CheckArgumentAndStringContracts();
	CheckWeaponPublicationBoundary();
	if (failures)
		return 1;
	puts("static affordance catalog preserved accepted audit evidence");
	return 0;
}
