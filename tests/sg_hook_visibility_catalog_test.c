#include <stdint.h>
#include <stdio.h>

#include "../slipgate/sg_hook_visibility_catalog.h"
#include "../slipgate/sg_hook_visibility_feasibility_internal.h"
#include "sg_hook_visibility_feasibility_fixture.h"

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static void SetPlane(sg_bsp_plane_t *plane, float x, float y, float z,
	float distance)
{
	plane->normal.value[0] = x;
	plane->normal.value[1] = y;
	plane->normal.value[2] = z;
	plane->distance = distance;
	if (x == 1.0f) plane->type = 0;
	else if (y == 1.0f) plane->type = 1;
	else if (z == 1.0f) plane->type = 2;
	else if (x == -1.0f) plane->type = 3;
	else if (y == -1.0f) plane->type = 4;
	else plane->type = 5;
}

static void SetBox(hook_visibility_fixture_t *fixture, uint32_t brush,
	int16_t min_x, int16_t max_x, int16_t min_y, int16_t max_y,
	int16_t min_z, int16_t max_z)
{
	uint32_t first = 1U + brush * 6U;

	SetPlane(&fixture->planes[first], 1.0f, 0.0f, 0.0f,
		(float)max_x * 0.125f);
	SetPlane(&fixture->planes[first + 1U], -1.0f, 0.0f, 0.0f,
		(float)-min_x * 0.125f);
	SetPlane(&fixture->planes[first + 2U], 0.0f, 1.0f, 0.0f,
		(float)max_y * 0.125f);
	SetPlane(&fixture->planes[first + 3U], 0.0f, -1.0f, 0.0f,
		(float)-min_y * 0.125f);
	SetPlane(&fixture->planes[first + 4U], 0.0f, 0.0f, 1.0f,
		(float)max_z * 0.125f);
	SetPlane(&fixture->planes[first + 5U], 0.0f, 0.0f, -1.0f,
		(float)-min_z * 0.125f);
}

static int DomainContains(const sg_hook_visibility_domain_term_t *domain,
	const int16_t origin[3])
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		if (origin[axis] < domain->origins.mins[axis] ||
			origin[axis] > domain->origins.maxs[axis])
			return 0;
	return domain->pitch_min == 0 && domain->pitch_max == 0 &&
		domain->yaw_min == -1 && domain->yaw_max == -1 &&
		domain->hand_mask == SG_HOOK_VISIBILITY_HAND_BIT(
			SG_HOOK_VISIBILITY_HAND_LEFT);
}

static int CatalogOutcomeAt(const sg_hook_visibility_catalog_t *catalog,
	const int16_t origin[3], sg_hook_visibility_catalog_outcome_t outcome)
{
	uint32_t index;

	for (index = 0U; index <
		SG_HookVisibilityCatalogTerminalCount(catalog); index++)
	{
		sg_hook_visibility_catalog_terminal_view_t terminal;

		if (!SG_HookVisibilityCatalogTerminal(catalog, index, &terminal))
			return 0;
		if (DomainContains(&terminal.domain, origin))
			return terminal.outcome == outcome;
	}
	return 0;
}

static void CheckPublishedEvidence(hook_visibility_fixture_t *fixture,
	const sg_hook_visibility_catalog_t *catalog)
{
	sg_hook_visibility_catalog_evidence_view_t evidence;
	sg_hook_visibility_catalog_audit_report_t audit;
	uint32_t outcomes[5] = {0U, 0U, 0U, 0U, 0U};
	uint32_t flags = 0U;
	uint32_t terminal, relation, relation_domains = 0U;
	int saw_three_dimensional_origin = 0;
	uint32_t sky_surfaces = 0U, nonhookable_surfaces = 0U;

	CHECK(SG_HookVisibilityCatalogEvidence(catalog, &evidence));
	CHECK(evidence.source_digest != 0U);
	CHECK(evidence.verifier_source_digest != 0U);
	CHECK(evidence.producer_identity == fixture->sources.producer_identity);
	CHECK(evidence.verifier_identity == fixture->sources.verifier_identity);
	CHECK(evidence.collision_identity.bsp_content_id ==
		fixture->authority.identity.bsp_content_id);
	CHECK(evidence.collision_identity.physics.gravity == 100.0f);
	CHECK(evidence.stance == SG_RUNE_STANCE_STANDING);
	CHECK(evidence.origins.mins[2] == -80);
	CHECK(evidence.origins.maxs[2] == 80);
	CHECK(evidence.fire_law.moving_model_count == 0U);
	CHECK(evidence.fire_law.mover_domain_identity == 0U);
	CHECK(evidence.control_count == fixture->sources.control_count);
	CHECK(evidence.surface_rule_count == fixture->sources.surface_rule_count);
	for (terminal = 0U; terminal < evidence.surface_rule_count; terminal++)
	{
		sg_hook_visibility_surface_class_t classification =
			evidence.surface_rules[terminal].classification;

		sky_surfaces += classification == SG_HOOK_VISIBILITY_SURFACE_SKY;
		nonhookable_surfaces +=
			classification == SG_HOOK_VISIBILITY_SURFACE_NONHOOKABLE;
	}
	CHECK(sky_surfaces > 0U);
	CHECK(nonhookable_surfaces > 0U);
	for (terminal = 0U; terminal <
		SG_HookVisibilityCatalogTerminalCount(catalog); terminal++)
	{
		sg_hook_visibility_catalog_terminal_view_t view;

		CHECK(SG_HookVisibilityCatalogTerminal(catalog, terminal, &view));
		if ((uint32_t)view.outcome < 5U)
			outcomes[(uint32_t)view.outcome]++;
		flags |= view.flags;
		if (view.domain.origins.mins[2] < view.domain.origins.maxs[2])
			saw_three_dimensional_origin = 1;
		if (view.outcome == SG_HOOK_VISIBILITY_CATALOG_NO_HIT ||
			view.outcome == SG_HOOK_VISIBILITY_CATALOG_CLEARANCE_BLOCKED)
			CHECK(view.surface_rule == NULL);
		else
			CHECK(view.surface_rule != NULL);
	}
	CHECK(outcomes[SG_HOOK_VISIBILITY_CATALOG_HOOKABLE] ==
		evidence.acceptance.hookable_terms);
	CHECK(outcomes[SG_HOOK_VISIBILITY_CATALOG_SKY] ==
		evidence.acceptance.sky_terms);
	CHECK(outcomes[SG_HOOK_VISIBILITY_CATALOG_NONHOOKABLE] ==
		evidence.acceptance.nonhookable_terms);
	CHECK(outcomes[SG_HOOK_VISIBILITY_CATALOG_NO_HIT] ==
		evidence.acceptance.no_hit_terms);
	CHECK(outcomes[SG_HOOK_VISIBILITY_CATALOG_CLEARANCE_BLOCKED] ==
		evidence.acceptance.clearance_blocked_terms);
	for (terminal = 0U; terminal < 5U; terminal++)
		CHECK(outcomes[terminal] > 0U);
	CHECK((flags & SG_HOOK_VISIBILITY_CATALOG_LOWER_DIMENSIONAL) != 0U);
	CHECK((flags & SG_HOOK_VISIBILITY_CATALOG_EDGE) != 0U);
	CHECK((flags & SG_HOOK_VISIBILITY_CATALOG_VERTEX) != 0U);
	CHECK((flags & SG_HOOK_VISIBILITY_CATALOG_TIE) != 0U);
	CHECK(saw_three_dimensional_origin);
	CHECK(evidence.metrics.legal_action_tuples >
		(uint64_t)evidence.metrics.predicate_domains * UINT64_C(100000));
	for (relation = 0U; relation <
		SG_HookVisibilityCatalogRelationCount(catalog); relation++)
	{
		sg_hook_visibility_catalog_relation_view_t view;

		CHECK(SG_HookVisibilityCatalogRelation(catalog, relation, &view));
		CHECK(view.surface_rule != NULL);
		if (view.surface_rule)
			CHECK(view.surface_rule->classification ==
				SG_HOOK_VISIBILITY_SURFACE_HOOKABLE);
		CHECK(view.domain_count > 0U);
		relation_domains += view.domain_count;
	}
	CHECK(relation_domains == evidence.metrics.relation_term_count);
	CHECK(SG_HookVisibilityCatalogAudit(&fixture->sources, catalog, &audit));
	CHECK(audit.code == SG_HOOK_VISIBILITY_CATALOG_AUDIT_OK);
}

static void CheckProofAndSourceRejection(hook_visibility_fixture_t *fixture,
	sg_hook_visibility_feasibility_catalog_t *proof)
{
	sg_hook_visibility_catalog_t *catalog = NULL;
	sg_hook_visibility_catalog_error_t error;
	sg_hook_visibility_terminal_outcome_t saved_outcome;
	uint32_t saved_moving_models;
	uint64_t saved_mover_identity;

	saved_outcome = proof->terminals[0].outcome;
	proof->terminals[0].outcome = saved_outcome ==
		SG_HOOK_VISIBILITY_TERMINAL_NO_HIT ?
		SG_HOOK_VISIBILITY_TERMINAL_SKY : SG_HOOK_VISIBILITY_TERMINAL_NO_HIT;
	CHECK(!SG_HookVisibilityCatalogBuild(&fixture->sources, proof, &catalog,
		&error));
	CHECK(error.code == SG_HOOK_VISIBILITY_CATALOG_ERROR_PROOF_REJECTED);
	CHECK(catalog == NULL);
	proof->terminals[0].outcome = saved_outcome;
	saved_moving_models = fixture->sources.fire_law.moving_model_count;
	saved_mover_identity = fixture->sources.fire_law.mover_domain_identity;
	fixture->sources.fire_law.moving_model_count = 1U;
	fixture->sources.fire_law.mover_domain_identity = UINT64_C(0x484d4f564552);
	CHECK(!SG_HookVisibilityCatalogBuild(&fixture->sources, proof, &catalog,
		&error));
	CHECK(error.code == SG_HOOK_VISIBILITY_CATALOG_ERROR_PROOF_REJECTED);
	CHECK(catalog == NULL);
	fixture->sources.fire_law.moving_model_count = saved_moving_models;
	fixture->sources.fire_law.mover_domain_identity = saved_mover_identity;
}

static void CheckOwnedLifetimeAndSourceAudit(void)
{
	hook_visibility_fixture_t fixture;
	sg_hook_visibility_feasibility_catalog_t *proof = NULL;
	sg_hook_visibility_catalog_t *catalog = NULL;
	sg_hook_visibility_feasibility_error_t proof_error;
	sg_hook_visibility_catalog_error_t error;
	sg_hook_visibility_catalog_evidence_view_t evidence;
	sg_hook_visibility_catalog_audit_report_t audit;
	float saved_gravity;
	uint64_t saved_surface;
	int16_t saved_pitch;

	CHECK(HookVisibilityFixtureInit(&fixture));
	CHECK(!SG_HookVisibilityCatalogBuild(&fixture.sources, NULL, &catalog,
		&error));
	CHECK(error.code == SG_HOOK_VISIBILITY_CATALOG_ERROR_INVALID_ARGUMENT);
	CHECK(SG_HookVisibilityFeasibilityBuild(&fixture.sources, &proof,
		&proof_error));
	if (!proof)
		return;
	CheckProofAndSourceRejection(&fixture, proof);
	CHECK(SG_HookVisibilityCatalogBuild(&fixture.sources, proof, &catalog,
		&error));
	if (!catalog)
	{
		SG_HookVisibilityFeasibilityDestroy(proof);
		return;
	}
	SG_HookVisibilityFeasibilityDestroy(proof);
	CheckPublishedEvidence(&fixture, catalog);
	CHECK(SG_HookVisibilityCatalogEvidence(catalog, &evidence));
	saved_surface = fixture.rules[0].surface_id;
	saved_pitch = fixture.controls[0].pitch_min;
	fixture.rules[0].surface_id++;
	fixture.controls[0].pitch_min++;
	CHECK(evidence.surface_rules[0].surface_id == saved_surface);
	CHECK(evidence.controls[0].pitch_min == saved_pitch);
	fixture.rules[0].surface_id = saved_surface;
	fixture.controls[0].pitch_min = saved_pitch;
	saved_gravity = fixture.authority.identity.physics.gravity;
	fixture.authority.identity.physics.gravity = 80.0f;
	CHECK(!SG_HookVisibilityCatalogAudit(&fixture.sources, catalog, &audit));
	CHECK(audit.code == SG_HOOK_VISIBILITY_CATALOG_AUDIT_PROOF_REJECTED);
	CHECK(audit.proof.code ==
		SG_HOOK_VISIBILITY_FEASIBILITY_AUDIT_SOURCE_MISMATCH);
	fixture.authority.identity.physics.gravity = saved_gravity;
	CHECK(SG_HookVisibilityCatalogAudit(&fixture.sources, catalog, &audit));
	SG_HookVisibilityCatalogDestroy(catalog);
}

static void CheckClearanceFaces(void)
{
	const int16_t origins[2][3] = {{-16, -11, 2}, {-16, 2, -11}};
	hook_visibility_fixture_t fixture;
	sg_hook_visibility_feasibility_catalog_t *proof = NULL;
	sg_hook_visibility_catalog_t *catalog = NULL;
	sg_hook_visibility_feasibility_error_t proof_error;
	sg_hook_visibility_catalog_error_t error;
	sg_host_collision_error_t collision_error;
	sg_rune_model_identity_t identity;
	uint32_t rule;

	CHECK(HookVisibilityFixtureInit(&fixture));
	SetBox(&fixture, 0U, 71, 73, -128, 3, -128, 112);
	SetBox(&fixture, 1U, 71, 73, 3, 128, -128, 105);
	SetBox(&fixture, 2U, 71, 73, -128, 3, 105, 128);
	SetBox(&fixture, 3U, 71, 73, 3, 128, 105, 128);
	SetBox(&fixture, 4U, -32, -9, -8, 8, -8, 8);
	fixture.texinfos[1].flags = 0U;
	for (rule = 0U; rule < 4U; rule++)
		fixture.rules[rule].classification =
			SG_HOOK_VISIBILITY_SURFACE_HOOKABLE;
	fixture.sources.origins.mins[0] = -16;
	fixture.sources.origins.maxs[0] = -1;
	fixture.sources.origins.mins[1] = -12;
	fixture.sources.origins.maxs[1] = 12;
	fixture.sources.origins.mins[2] = -12;
	fixture.sources.origins.maxs[2] = 12;
	fixture.controls[0].pitch_min = 0;
	fixture.controls[0].pitch_max = 0;
	fixture.controls[0].yaw_min = -1;
	fixture.controls[0].yaw_max = -1;
	fixture.sources.fire_law.maximum_range = 8.0f;
	identity = fixture.authority.identity;
	CHECK(SG_HostCollisionInit(&fixture.authority, &fixture.world, &identity,
		&collision_error));
	CHECK(SG_HookVisibilityFeasibilityBuild(&fixture.sources, &proof,
		&proof_error));
	if (!proof)
		return;
	CHECK(SG_HookVisibilityCatalogBuild(&fixture.sources, proof, &catalog,
		&error));
	SG_HookVisibilityFeasibilityDestroy(proof);
	if (!catalog)
		return;
	CHECK(CatalogOutcomeAt(catalog, origins[0],
		SG_HOOK_VISIBILITY_CATALOG_CLEARANCE_BLOCKED));
	CHECK(CatalogOutcomeAt(catalog, origins[1],
		SG_HOOK_VISIBILITY_CATALOG_CLEARANCE_BLOCKED));
	SG_HookVisibilityCatalogDestroy(catalog);
}

int main(void)
{
	CheckOwnedLifetimeAndSourceAudit();
	CheckClearanceFaces();
	if (failures)
		return 1;
	puts("hook visibility catalog preserved accepted exact domains");
	return 0;
}
