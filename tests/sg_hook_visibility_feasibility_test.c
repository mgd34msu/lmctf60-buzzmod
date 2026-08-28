#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static void CheckAngleAuthority(void)
{
	uint32_t code;

	for (code = 0U; code <= UINT16_MAX; code++)
	{
		uint32_t produced_sine, produced_cosine;
		uint32_t host_sine, host_cosine;

		SG_HookVisibilityFeasibilityAngleBits((uint16_t)code,
			&produced_sine, &produced_cosine);
		HookVisibilityHostReferenceAngleBits((uint16_t)code, &host_sine,
			&host_cosine);
		if (produced_sine != host_sine || produced_cosine != host_cosine)
		{
			fprintf(stderr, "angle authority mismatch at code %u\n", code);
			failures++;
			return;
		}
	}
}

static void CheckRelationShape(
	const sg_hook_visibility_feasibility_catalog_t *catalog,
	const sg_hook_visibility_feasibility_metrics_t *metrics,
	const sg_hook_visibility_feasibility_audit_report_t *audit)
{
	uint32_t relation;
	uint32_t saw_left = 0U, saw_center = 0U, saw_right = 0U;

	CHECK(SG_HookVisibilityFeasibilityRelationCount(catalog) == 2U);
	for (relation = 0U; relation <
		SG_HookVisibilityFeasibilityRelationCount(catalog); relation++)
	{
		sg_hook_visibility_relation_view_t view;
		uint32_t term;

		CHECK(SG_HookVisibilityFeasibilityRelation(catalog, relation, &view));
		CHECK(view.surface_id == (relation == 0U ? UINT64_C(0x100) :
			UINT64_C(0x200)));
		for (term = 0U; term < view.term_count; term++)
		{
			saw_left |= view.terms[term].hand_mask &
				SG_HOOK_VISIBILITY_HAND_BIT(SG_HOOK_VISIBILITY_HAND_LEFT);
			saw_center |= view.terms[term].hand_mask &
				SG_HOOK_VISIBILITY_HAND_BIT(SG_HOOK_VISIBILITY_HAND_CENTER);
			saw_right |= view.terms[term].hand_mask &
				SG_HOOK_VISIBILITY_HAND_BIT(SG_HOOK_VISIBILITY_HAND_RIGHT);
			CHECK(view.terms[term].yaw_max < 32766);
		}
	}
	CHECK(saw_left && saw_center && saw_right);
	CHECK(metrics->relation_count == 2U);
	CHECK(metrics->relation_term_count < audit->hookable_terms);
}

static void CheckBoundaryEvidence(
	const sg_hook_visibility_feasibility_catalog_t *catalog)
{
	uint32_t terminal;
	int saw_range_endpoint = 0;
	int saw_range_rejection = 0;
	int saw_left_sky = 0;
	int saw_center_vertex = 0;
	int saw_right_hookable = 0;

	for (terminal = 0U; terminal < catalog->terminal_count; terminal++)
	{
		const sg_hook_visibility_terminal_t *record =
			&catalog->terminals[terminal];
		const sg_hook_visibility_domain_term_t *domain = &record->domain;

		if (domain->pitch_min != 0 || domain->pitch_max != 0 ||
			domain->yaw_min != 0 || domain->yaw_max != 0)
			continue;
		if (domain->origins.mins[0] == -576 &&
			domain->origins.maxs[0] == -576 &&
			record->outcome == SG_HOOK_VISIBILITY_TERMINAL_HOOKABLE)
			saw_range_endpoint = 1;
		if (domain->origins.maxs[0] < -576 &&
			record->outcome == SG_HOOK_VISIBILITY_TERMINAL_NO_HIT)
			saw_range_rejection = 1;
		if (domain->origins.mins[1] != 0 || domain->origins.maxs[1] != 0 ||
			domain->origins.mins[2] != 0 || domain->origins.maxs[2] != 0)
			continue;
		if (domain->hand_mask == SG_HOOK_VISIBILITY_HAND_BIT(
				SG_HOOK_VISIBILITY_HAND_LEFT) &&
			record->outcome == SG_HOOK_VISIBILITY_TERMINAL_SKY)
			saw_left_sky = 1;
		if (domain->hand_mask == SG_HOOK_VISIBILITY_HAND_BIT(
				SG_HOOK_VISIBILITY_HAND_CENTER) &&
			record->outcome == SG_HOOK_VISIBILITY_TERMINAL_HOOKABLE &&
			(record->flags & (SG_HOOK_VISIBILITY_TERMINAL_VERTEX |
			 SG_HOOK_VISIBILITY_TERMINAL_TIE)) ==
				(SG_HOOK_VISIBILITY_TERMINAL_VERTEX |
				 SG_HOOK_VISIBILITY_TERMINAL_TIE) && record->surface_rule == 0U)
			saw_center_vertex = 1;
		if (domain->hand_mask == SG_HOOK_VISIBILITY_HAND_BIT(
				SG_HOOK_VISIBILITY_HAND_RIGHT) &&
			record->outcome == SG_HOOK_VISIBILITY_TERMINAL_HOOKABLE)
			saw_right_hookable = 1;
	}
	CHECK(saw_range_endpoint);
	CHECK(saw_range_rejection);
	CHECK(saw_left_sky);
	CHECK(saw_center_vertex);
	CHECK(saw_right_hookable);
}

static void CheckTamperDetection(hook_visibility_fixture_t *fixture,
	sg_hook_visibility_feasibility_catalog_t *catalog)
{
	sg_hook_visibility_feasibility_audit_report_t report;
	sg_hook_visibility_feasibility_metrics_t saved_metrics;
	sg_hook_visibility_terminal_outcome_t saved_outcome;
	uint32_t saved_count;
	float saved_range, saved_plane_distance;

	saved_outcome = catalog->terminals[0].outcome;
	catalog->terminals[0].outcome = saved_outcome ==
		SG_HOOK_VISIBILITY_TERMINAL_NO_HIT ?
		SG_HOOK_VISIBILITY_TERMINAL_SKY : SG_HOOK_VISIBILITY_TERMINAL_NO_HIT;
	CHECK(!SG_HookVisibilityFeasibilityAudit(&fixture->sources, catalog,
		&report));
	CHECK(report.code ==
		SG_HOOK_VISIBILITY_FEASIBILITY_AUDIT_TERMINAL_DISAGREEMENT);
	catalog->terminals[0].outcome = saved_outcome;
	saved_range = fixture->sources.fire_law.maximum_range;
	fixture->sources.fire_law.maximum_range = saved_range + 8.0f;
	CHECK(!SG_HookVisibilityFeasibilityAudit(&fixture->sources, catalog,
		&report));
	CHECK(report.code == SG_HOOK_VISIBILITY_FEASIBILITY_AUDIT_SOURCE_MISMATCH);
	fixture->sources.fire_law.maximum_range = saved_range;
	CHECK(SG_HookVisibilityFeasibilityAudit(&fixture->sources, catalog,
		&report));
	saved_plane_distance = fixture->planes[1].distance;
	fixture->planes[1].distance = saved_plane_distance + 0.125f;
	CHECK(!SG_HookVisibilityFeasibilityAudit(&fixture->sources, catalog,
		&report));
	CHECK(report.code == SG_HOOK_VISIBILITY_FEASIBILITY_AUDIT_SOURCE_MISMATCH);
	fixture->planes[1].distance = saved_plane_distance;
	CHECK(SG_HookVisibilityFeasibilityAudit(&fixture->sources, catalog,
		&report));
	fixture->sources.control_count =
		SG_HOOK_VISIBILITY_FEASIBILITY_MAX_CONTROL_ROOTS + 1U;
	CHECK(!SG_HookVisibilityFeasibilityAudit(&fixture->sources, catalog,
		&report));
	CHECK(report.code == SG_HOOK_VISIBILITY_FEASIBILITY_AUDIT_SOURCE_MISMATCH);
	fixture->sources.control_count = 1U;
	saved_count = catalog->terminal_count;
	catalog->terminal_count--;
	CHECK(!SG_HookVisibilityFeasibilityAudit(&fixture->sources, catalog,
		&report));
	catalog->terminal_count = saved_count;
	saved_count = catalog->relations[0].term_count;
	catalog->relations[0].term_count--;
	CHECK(!SG_HookVisibilityFeasibilityAudit(&fixture->sources, catalog,
		&report));
	catalog->relations[0].term_count = saved_count;
	saved_count = catalog->relation_count;
	catalog->relation_count--;
	CHECK(!SG_HookVisibilityFeasibilityAudit(&fixture->sources, catalog,
		&report));
	catalog->relation_count = saved_count;
	saved_metrics = catalog->metrics;
	memset(&catalog->metrics, 0, sizeof(catalog->metrics));
	CHECK(!SG_HookVisibilityFeasibilityAudit(&fixture->sources, catalog,
		&report));
	catalog->metrics = saved_metrics;
	CHECK(SG_HookVisibilityFeasibilityAudit(&fixture->sources, catalog,
		&report));
}

static void CheckFailClosed(hook_visibility_fixture_t *fixture)
{
	sg_hook_visibility_feasibility_sources_t sources = fixture->sources;
	sg_hook_visibility_control_root_t controls[2];
	sg_hook_visibility_feasibility_catalog_t *catalog = NULL;
	sg_hook_visibility_feasibility_error_t error;
	sg_host_collision_instance_t instance;
	sg_host_collision_scene_t scene;

	sources.fire_law.moving_model_count = 1U;
	CHECK(!SG_HookVisibilityFeasibilityBuild(&sources, &catalog, &error));
	CHECK(error.code ==
		SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_MOVING_MODEL_AUTHORITY);
	sources = fixture->sources;
	sources.verifier_identity = sources.producer_identity;
	CHECK(!SG_HookVisibilityFeasibilityBuild(&sources, &catalog, &error));
	CHECK(error.code == SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_INVALID_SOURCE);
	sources = fixture->sources;
	memcpy(controls, fixture->controls, sizeof(controls));
	controls[0].pitch_min = -2;
	sources.controls = controls;
	CHECK(!SG_HookVisibilityFeasibilityBuild(&sources, &catalog, &error));
	CHECK(error.code == SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_UNSUPPORTED);
	sources = fixture->sources;
	memcpy(controls, fixture->controls, sizeof(controls));
	controls[0].pitch_min = 0;
	controls[0].pitch_max = 0;
	controls[0].yaw_min = 2;
	controls[0].yaw_max = 2;
	sources.controls = controls;
	CHECK(!SG_HookVisibilityFeasibilityBuild(&sources, &catalog, &error));
	CHECK(error.code == SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_UNSUPPORTED);
	CHECK(!SG_HookVisibilityFeasibilityAuditFamilyValid(&sources));
	sources = fixture->sources;
	memcpy(controls, fixture->controls, sizeof(controls));
	controls[0].pitch_min = -1;
	controls[0].pitch_max = 1;
	controls[0].yaw_min = -1;
	controls[0].yaw_max = 1;
	sources.controls = controls;
	CHECK(!SG_HookVisibilityFeasibilityBuild(&sources, &catalog, &error));
	CHECK(error.code == SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_UNSUPPORTED);
	sources = fixture->sources;
	sources.control_count =
		SG_HOOK_VISIBILITY_FEASIBILITY_MAX_CONTROL_ROOTS + 1U;
	CHECK(!SG_HookVisibilityFeasibilityBuild(&sources, &catalog, &error));
	CHECK(error.code == SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_OVERFLOW);
	sources = fixture->sources;
	sources.surface_rule_count =
		SG_HOOK_VISIBILITY_FEASIBILITY_MAX_SURFACE_RULES + 1U;
	CHECK(!SG_HookVisibilityFeasibilityBuild(&sources, &catalog, &error));
	CHECK(error.code == SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_OVERFLOW);
	sources = fixture->sources;
	memset(&instance, 0, sizeof(instance));
	instance.instance_id = 1U;
	instance.model_index = 0U;
	scene.instances = &instance;
	scene.instance_count = 1U;
	sources.scene = &scene;
	CHECK(!SG_HookVisibilityFeasibilityBuild(&sources, &catalog, &error));
	CHECK(error.code == SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_UNSUPPORTED);
	sources = fixture->sources;
	fixture->planes[31].normal.value[0] = 0.70710677f;
	fixture->planes[31].normal.value[1] = 0.70710677f;
	fixture->planes[31].normal.value[2] = 0.0f;
	fixture->planes[31].distance = -4095.0f;
	fixture->planes[31].type = 3;
	fixture->brush_sides[30].plane = 31U;
	fixture->brush_sides[30].texinfo = 4;
	fixture->brushes[4].side_count = 7U;
	fixture->world.plane_count = 32U;
	fixture->world.brush_side_count = 31U;
	CHECK(!SG_HookVisibilityFeasibilityBuild(&sources, &catalog, &error));
	CHECK(error.code == SG_HOOK_VISIBILITY_FEASIBILITY_ERROR_UNSUPPORTED);
	fixture->brushes[4].side_count = 6U;
	fixture->world.plane_count = 31U;
	fixture->world.brush_side_count = 30U;
}

int main(void)
{
	hook_visibility_fixture_t fixture;
	sg_hook_visibility_feasibility_catalog_t *first = NULL, *second = NULL;
	sg_hook_visibility_feasibility_error_t error;
	sg_hook_visibility_feasibility_audit_report_t audit;
	sg_hook_visibility_feasibility_metrics_t metrics;
	uint8_t *first_bytes = NULL, *second_bytes = NULL;
	size_t first_size = 0U, second_size = 0U;

	CheckAngleAuthority();
	CHECK(HookVisibilityFixtureInit(&fixture));
	CHECK(SG_HookVisibilityFeasibilityBuild(&fixture.sources, &first, &error));
	if (!first)
	{
		fprintf(stderr, "build failed: %s source=%u\n",
			SG_HookVisibilityFeasibilityErrorString(error.code),
			error.source_index);
		return 1;
	}
	CHECK(SG_HookVisibilityFeasibilityAudit(&fixture.sources, first, &audit));
	if (audit.code != SG_HOOK_VISIBILITY_FEASIBILITY_AUDIT_OK)
		fprintf(stderr, "audit failed: %s record=%u\n",
			SG_HookVisibilityFeasibilityAuditCodeString(audit.code), audit.record);
	CHECK(SG_HookVisibilityFeasibilityMetrics(first, &metrics));
	CHECK(metrics.angle_authority_entries == UINT64_C(65536));
	CHECK(metrics.legal_action_tuples >
		metrics.predicate_domains * UINT64_C(100000));
	CHECK(metrics.muzzle_clearance_traces == metrics.predicate_domains);
	CHECK(metrics.first_hit_traces == metrics.predicate_domains -
		audit.clearance_blocked_terms);
	CHECK(audit.hookable_terms > 0U);
	CHECK(audit.sky_terms > 0U);
	CHECK(audit.nonhookable_terms > 0U);
	CHECK(audit.no_hit_terms > 0U);
	CHECK(audit.clearance_blocked_terms > 0U);
	CHECK(audit.lower_dimensional_terms > 0U);
	CHECK(audit.edge_terms > 0U);
	CHECK(audit.vertex_terms > 0U);
	CHECK(audit.tie_terms > 0U);
	CHECK(audit.producer_identity != audit.verifier_identity);
	CHECK(audit.reconstructed_action_tuples == metrics.legal_action_tuples);
	CHECK(audit.reconstructed_predicate_domains == metrics.predicate_domains);
	CheckRelationShape(first, &metrics, &audit);
	CheckBoundaryEvidence(first);
	CHECK(SG_HookVisibilityFeasibilitySerialize(first, &first_bytes,
		&first_size));
	CHECK(first_size == 24U + metrics.relation_count * 20U +
		(size_t)metrics.relation_term_count * 24U);
	CHECK(first_size < (size_t)metrics.predicate_domains *
		sizeof(sg_hook_visibility_terminal_t));
	CHECK(SG_HookVisibilityFeasibilityBuild(&fixture.sources, &second, &error));
	CHECK(SG_HookVisibilityFeasibilitySerialize(second, &second_bytes,
		&second_size));
	CHECK(first_size == second_size);
	CHECK(first_size == second_size &&
		memcmp(first_bytes, second_bytes, first_size) == 0);
	CheckTamperDetection(&fixture, first);
	CheckFailClosed(&fixture);
	printf("hook visibility feasibility passed: tuples=%llu predicates=%llu "
		"relations=%u terms=%u complement=%u bytes=%zu\n",
		(unsigned long long)metrics.legal_action_tuples,
		(unsigned long long)metrics.predicate_domains,
		metrics.relation_count, metrics.relation_term_count,
		metrics.complement_term_count, first_size);
	free(second_bytes);
	free(first_bytes);
	SG_HookVisibilityFeasibilityDestroy(second);
	SG_HookVisibilityFeasibilityDestroy(first);
	if (failures)
	{
		fprintf(stderr, "%d hook visibility feasibility failure(s)\n", failures);
		return 1;
	}
	return 0;
}
