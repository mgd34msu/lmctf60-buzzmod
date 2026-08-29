#define SG_GROUND_CAPABILITY_TEST_NO_MAIN
#include "sg_ground_capability_test.c"

#include "../slipgate/sg_ground_capability_publication.h"

static sg_ground_capability_publication_source_t PublicationSource(
	ground_fixture_t *fixture, sg_host_pmove_function_t host_pmove,
	const sg_ground_phase_binding_t *bindings, size_t binding_count)
{
	sg_ground_capability_publication_source_t source;

	memset(&source, 0, sizeof(source));
	source.authority = &fixture->authority;
	source.configuration = &fixture->configuration;
	source.semantics = &fixture->semantics;
	source.phases = fixture->phases;
	source.phase_count = 4U;
	source.bindings = bindings;
	source.binding_count = binding_count;
	source.host_pmove = host_pmove;
	source.host_law_identity = fixture->authority.identity.physics_abi_id;
	return source;
}

static void TestExactAuditAndOwnedPublication(void)
{
	const test_box_t floor = {
		{ -4096.0f, -4096.0f, -4096.0f },
		{ 4095.0f, 4095.0f, -24.1f }, SG_HOST_CONTENTS_SOLID
	};
	ground_fixture_t fixture;
	sg_ground_capability_publication_source_t source;
	sg_ground_capability_set_t *set = NULL;
	sg_ground_capability_publication_t *publication = NULL;
	sg_ground_capability_publication_description_t description;
	sg_ground_capability_publication_fact_t fact;
	sg_ground_capability_audit_result_t audit;
	sg_ground_capability_error_t error;
	float published_gravity;

	GroundFixtureInit(&fixture, &floor, 1U, 800.0f,
		SG_RUNE_STANCE_STANDING, 0.0f, 0.0f);
	GroundFixtureRebind(&fixture);
	source = PublicationSource(&fixture, Pmove, fixture.bindings, 4U);
	CHECK(GroundBuild(&fixture, &set, &error));
	CHECK(SG_GroundCapabilityAudit(&source, set, &audit));
	CHECK(audit.code == SG_GROUND_CAPABILITY_AUDIT_OK);
	CHECK(audit.completeness == SG_GROUND_CAPABILITY_COMPLETENESS_COMPLETE);
	CHECK(audit.expected_facts == set->capability_count);
	CHECK(audit.matched_facts == set->capability_count);
	CHECK(audit.proved_portals == set->proved_portals);
	CHECK(audit.proven_empty_portals == set->rejected_crossings);
	CHECK(audit.proved_directions == set->proved_directions);
	CHECK(audit.proven_empty_directions == set->rejected_directions);
	CHECK(audit.host_pmove_frames > 0U);
	CHECK(SG_GroundCapabilityPublicationIssue(&source, set, &publication,
		&audit));
	CHECK(SG_GroundCapabilityPublicationDescribe(publication, &description));
	CHECK(description.host_law_identity ==
		fixture.authority.identity.physics_abi_id);
	CHECK(description.cell_count == fixture.configuration.cell_count);
	CHECK(description.portal_count == fixture.configuration.portal_count);
	CHECK(description.semantic_region_count == fixture.semantics.region_count);
	CHECK(description.phase_count == 4U);
	CHECK(description.binding_count == 4U);
	CHECK(description.fact_count == set->capability_count);
	CHECK(description.fact_count_by_kind[SG_GROUND_CAPABILITY_WALK] > 0U);
	CHECK(description.fact_count_by_kind[
		SG_GROUND_CAPABILITY_JUMP_TAKEOFF] > 0U);
	CHECK(SG_GroundCapabilityPublicationFact(publication, 0U, &fact));
	CHECK(SG_RuneModelStableIdEqual(&fact.source_cell.value,
		&fixture.configuration.cells[set->capabilities[0].source_cell].id.value));
	CHECK(SG_RuneModelStableIdEqual(&fact.source_phase.value,
		&fixture.phases[set->capabilities[0].source_phase].id.value));
	published_gravity = fact.gravity;
	set->capabilities[0].gravity = 100.0f;
	fixture.phases[set->capabilities[0].source_phase].velocity.x.max_value = 1.0f;
	GroundFixtureDestroy(&fixture);
	SG_GroundCapabilityDestroy(set);
	CHECK(SG_GroundCapabilityPublicationDescribe(publication, &description));
	CHECK(SG_GroundCapabilityPublicationFact(publication, 0U, &fact));
	CHECK(fact.gravity == published_gravity);
	CHECK(!SG_GroundCapabilityPublicationFact(publication,
		description.fact_count, &fact));
	SG_GroundCapabilityPublicationDestroy(publication);
}

static void CheckPublishedKind(ground_fixture_t *fixture,
	sg_host_pmove_function_t host_pmove, sg_ground_capability_kind_t kind)
{
	sg_ground_capability_publication_source_t source =
		PublicationSource(fixture, host_pmove, fixture->bindings, 4U);
	sg_ground_capability_set_t *set = NULL;
	sg_ground_capability_publication_t *publication = NULL;
	sg_ground_capability_publication_description_t description;
	sg_ground_capability_audit_result_t audit;
	sg_ground_capability_error_t error;

	CHECK(SG_GroundCapabilityBuild(&fixture->authority, &fixture->configuration,
		&fixture->semantics, fixture->phases, 4U, fixture->bindings, 4U,
		host_pmove, &set, &error));
	CHECK(set != NULL && HasKind(set, kind));
	CHECK(SG_GroundCapabilityPublicationIssue(&source, set, &publication,
		&audit));
	CHECK(SG_GroundCapabilityPublicationDescribe(publication, &description));
	CHECK(description.fact_count_by_kind[kind] > 0U);
	SG_GroundCapabilityPublicationDestroy(publication);
	SG_GroundCapabilityDestroy(set);
}

static void TestGeneralizedKindsPublish(void)
{
	const float diagonal = 0.70710677f;
	const test_box_t low_clearance[2] = {
		{ { -4096.0f, -4096.0f, -4096.0f },
			{ 4095.0f, 4095.0f, -24.1f }, SG_HOST_CONTENTS_SOLID },
		{ { -4096.0f, -4096.0f, 16.0f },
			{ 4095.0f, 4095.0f, 4095.0f }, SG_HOST_CONTENTS_SOLID }
	};
	const test_box_t ramp = {
		{ -50.0f, -50.0f, -100.0f },
		{ 50.0f, 50.0f, 100.0f }, SG_HOST_CONTENTS_SOLID
	};
	const test_box_t ledge = {
		{ -4096.0f, -4096.0f, -4096.0f },
		{ -16.1f, 4095.0f, -24.1f }, SG_HOST_CONTENTS_SOLID
	};
	ground_fixture_t fixture;

	GroundFixtureInit(&fixture, low_clearance, 2U, 800.0f,
		SG_RUNE_STANCE_CROUCHING, 0.0f, 0.0f);
	GroundFixtureRebind(&fixture);
	CheckPublishedKind(&fixture, Pmove, SG_GROUND_CAPABILITY_CROUCH);
	GroundFixtureDestroy(&fixture);

	GroundFixtureInit(&fixture, &ramp, 1U, 100.0f,
		SG_RUNE_STANCE_STANDING, 39.0f, 41.0f);
	SetPlane(&fixture.world.planes[5], -diagonal, 0.0f, diagonal, 0.0f);
	fixture.semantics.faces = fixture.semantic_faces;
	fixture.semantics.face_count = 2U;
	SetVector(fixture.semantic_faces[0].normal, -1.0f, 0.0f, 1.0f);
	fixture.semantic_faces[0].distance = 40.25f;
	SetVector(fixture.semantic_faces[1].normal, 1.0f, 0.0f, -1.0f);
	fixture.semantic_faces[1].distance = -39.75f;
	fixture.regions[0].first_face = 0U;
	fixture.regions[0].face_count = 2U;
	fixture.regions[2].first_face = 0U;
	fixture.regions[2].face_count = 2U;
	SetRune3(&fixture.regions[0].bounds.mins, -64.0f, -64.0f, 20.0f);
	SetRune3(&fixture.regions[0].bounds.maxs, 0.0f, 64.0f, 40.25f);
	SetRune3(&fixture.regions[2].bounds.mins, 0.0f, -64.0f, 39.75f);
	SetRune3(&fixture.regions[2].bounds.maxs, 64.0f, 64.0f, 64.0f);
	SetRune3(&fixture.vertices[0], 0.0f, -32.0f, 32.0f);
	SetRune3(&fixture.vertices[1], 0.0f, 32.0f, 32.0f);
	SetRune3(&fixture.vertices[2], 0.0f, 32.0f, 48.0f);
	SetRune3(&fixture.vertices[3], 0.0f, -32.0f, 48.0f);
	GroundFixtureRebind(&fixture);
	CheckPublishedKind(&fixture, Pmove, SG_GROUND_CAPABILITY_RAMP);
	GroundFixtureDestroy(&fixture);

	GroundFixtureInit(&fixture, &ledge, 1U, 800.0f,
		SG_RUNE_STANCE_STANDING, 0.0f, -24.0f);
	GroundFixtureRebind(&fixture);
	SetRune3(&fixture.regions[3].bounds.mins, 0.0f, -64.0f, -64.0f);
	SetRune3(&fixture.regions[3].bounds.maxs, 64.0f, 64.0f, 64.0f);
	SetRune3(&fixture.regions[3].interior_witness, 1.0f, 0.0f, -4.0f);
	CheckPublishedKind(&fixture, Pmove, SG_GROUND_CAPABILITY_DROP);
	GroundFixtureDestroy(&fixture);
}

static void TestAuditRejectsEveryCandidateClass(void)
{
	const test_box_t floor = {
		{ -4096.0f, -4096.0f, -4096.0f },
		{ 4095.0f, 4095.0f, -24.1f }, SG_HOST_CONTENTS_SOLID
	};
	ground_fixture_t fixture;
	sg_ground_capability_publication_source_t source;
	sg_ground_capability_set_t *set = NULL;
	sg_ground_capability_audit_result_t audit;
	sg_ground_capability_error_t error;
	sg_ground_capability_t saved;
	uint32_t saved_count;
	uint32_t saved_counter;

	GroundFixtureInit(&fixture, &floor, 1U, 800.0f,
		SG_RUNE_STANCE_STANDING, 0.0f, 0.0f);
	GroundFixtureRebind(&fixture);
	source = PublicationSource(&fixture, Pmove, fixture.bindings, 4U);
	CHECK(GroundBuild(&fixture, &set, &error));
	if (!set || set->capability_count < 2U)
		goto done;
	saved = set->capabilities[0];
	set->capabilities[0].gravity = 100.0f;
	CHECK(!SG_GroundCapabilityAudit(&source, set, &audit));
	CHECK(audit.code == SG_GROUND_CAPABILITY_AUDIT_FACT_DISAGREEMENT);
	CHECK(audit.record == 0U);
	set->capabilities[0] = saved;
	saved_count = set->capability_count;
	set->capability_count--;
	CHECK(!SG_GroundCapabilityAudit(&source, set, &audit));
	CHECK(audit.code == SG_GROUND_CAPABILITY_AUDIT_OMITTED_FACT);
	set->capability_count = saved_count + 1U;
	CHECK(!SG_GroundCapabilityAudit(&source, set, &audit));
	CHECK(audit.code == SG_GROUND_CAPABILITY_AUDIT_INVENTED_FACT);
	set->capability_count = saved_count;
	saved = set->capabilities[0];
	set->capabilities[0] = set->capabilities[1];
	set->capabilities[1] = saved;
	CHECK(!SG_GroundCapabilityAudit(&source, set, &audit));
	CHECK(audit.code == SG_GROUND_CAPABILITY_AUDIT_FACT_DISAGREEMENT);
	set->capabilities[1] = set->capabilities[0];
	set->capabilities[0] = saved;
	saved_counter = set->proved_directions;
	set->proved_directions++;
	CHECK(!SG_GroundCapabilityAudit(&source, set, &audit));
	CHECK(audit.code ==
		SG_GROUND_CAPABILITY_AUDIT_COMPLETENESS_DISAGREEMENT);
	set->proved_directions = saved_counter;
	set->identity.physics_abi_id++;
	CHECK(!SG_GroundCapabilityAudit(&source, set, &audit));
	CHECK(audit.code == SG_GROUND_CAPABILITY_AUDIT_IDENTITY_MISMATCH);
	set->identity.physics_abi_id--;
	source.host_law_identity++;
	CHECK(!SG_GroundCapabilityAudit(&source, set, &audit));
	CHECK(audit.code == SG_GROUND_CAPABILITY_AUDIT_HOST_LAW_MISMATCH);
	source.host_law_identity--;
	source.host_pmove = EmptyPmove;
	CHECK(!SG_GroundCapabilityAudit(&source, set, &audit));
	CHECK(audit.code == SG_GROUND_CAPABILITY_AUDIT_HOST_LAW_MISMATCH);
	source.host_pmove = Pmove;
	fixture.phases[0].time_quantum_ms = 0U;
	CHECK(!SG_GroundCapabilityAudit(&source, set, &audit));
	CHECK(audit.code == SG_GROUND_CAPABILITY_AUDIT_RECONSTRUCTION_REJECTED);

done:
	SG_GroundCapabilityDestroy(set);
	GroundFixtureDestroy(&fixture);
}

static void TestProvenEmptyPublication(void)
{
	const test_box_t floor = {
		{ -4096.0f, -4096.0f, -4096.0f },
		{ 4095.0f, 4095.0f, -24.1f }, SG_HOST_CONTENTS_SOLID
	};
	ground_fixture_t fixture;
	sg_ground_phase_binding_t airborne[2];
	sg_ground_capability_publication_source_t source;
	sg_ground_capability_set_t *set = NULL;
	sg_ground_capability_publication_t *publication = NULL;
	sg_ground_capability_publication_description_t description;
	sg_ground_capability_audit_result_t audit;
	sg_ground_capability_error_t error;

	GroundFixtureInit(&fixture, &floor, 1U, 800.0f,
		SG_RUNE_STANCE_STANDING, 0.0f, 0.0f);
	GroundFixtureRebind(&fixture);
	fixture.configuration.portal_count = 0U;
	airborne[0] = fixture.bindings[1];
	airborne[1] = fixture.bindings[3];
	fixture.phases[1].velocity.z.min_value = 100.0f;
	fixture.phases[1].velocity.z.max_value = 100.0f;
	fixture.phases[3].velocity.z.min_value = 100.0f;
	fixture.phases[3].velocity.z.max_value = 100.0f;
	source = PublicationSource(&fixture, Pmove, airborne, 2U);
	CHECK(SG_GroundCapabilityBuild(&fixture.authority, &fixture.configuration,
		&fixture.semantics, fixture.phases, 4U, airborne, 2U, Pmove,
		&set, &error));
	CHECK(set != NULL && set->capability_count == 0U);
	CHECK(SG_GroundCapabilityPublicationIssue(&source, set, &publication,
		&audit));
	CHECK(audit.completeness ==
		SG_GROUND_CAPABILITY_COMPLETENESS_PROVEN_EMPTY);
	CHECK(SG_GroundCapabilityPublicationDescribe(publication, &description));
	CHECK(description.completeness ==
		SG_GROUND_CAPABILITY_COMPLETENESS_PROVEN_EMPTY);
	CHECK(description.fact_count == 0U);
	CHECK(description.portal_count == 0U);
	SG_GroundCapabilityPublicationDestroy(publication);
	SG_GroundCapabilityDestroy(set);
	GroundFixtureDestroy(&fixture);
}

static void TestInvalidPublicationArgumentsAreDeterministic(void)
{
	sg_ground_capability_audit_result_t audit;
	sg_ground_capability_publication_t *publication = NULL;

	memset(&audit, 0xa5, sizeof(audit));
	CHECK(!SG_GroundCapabilityPublicationIssue(NULL, NULL, NULL, &audit));
	CHECK(audit.code == SG_GROUND_CAPABILITY_AUDIT_INVALID_ARGUMENT);
	CHECK(audit.completeness ==
		SG_GROUND_CAPABILITY_COMPLETENESS_UNRESOLVED);
	CHECK(audit.record == SG_GROUND_CAPABILITY_INDEX_NONE);
	CHECK(!SG_GroundCapabilityPublicationIssue(NULL, NULL, &publication,
		&audit));
	CHECK(audit.code == SG_GROUND_CAPABILITY_AUDIT_INVALID_ARGUMENT);
	CHECK(!SG_GroundCapabilityPublicationDescribe(NULL, NULL));
	CHECK(!SG_GroundCapabilityPublicationFact(NULL, 0U, NULL));
	SG_GroundCapabilityPublicationDestroy(NULL);
}

int main(void)
{
	if (RunGroundCapabilityTests() != 0)
		return 1;
	failures = 0;
	TestExactAuditAndOwnedPublication();
	TestGeneralizedKindsPublish();
	TestAuditRejectsEveryCandidateClass();
	TestProvenEmptyPublication();
	TestInvalidPublicationArgumentsAreDeterministic();
	if (failures)
	{
		fprintf(stderr, "%d ground publication test failure(s)\n", failures);
		return 1;
	}
	puts("ground capability publication checks passed");
	return 0;
}
