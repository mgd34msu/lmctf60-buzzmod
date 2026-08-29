#define SG_GROUND_CAPABILITY_TEST_NO_MAIN
#include "sg_ground_capability_test.c"

#include "../slipgate/sg_ground_capability_publication.h"
#include "../slipgate/sg_configuration_audit.h"

typedef struct accepted_source_fixture_s
{
	fixture_t world;
	sg_host_collision_authority_t authority;
	sg_configuration_space_t *configuration;
	sg_configuration_semantics_t *semantics;
	sg_ground_capability_set_t candidate;
	sg_ground_capability_publication_source_t source;
} accepted_source_fixture_t;

typedef union fake_phase_catalog_storage_u
{
	max_align_t alignment;
	unsigned char bytes[sizeof(max_align_t)];
} fake_phase_catalog_storage_t;

static fixture_t WalkFixture(void)
{
	fixture_t fixture;
	uint32_t side;

	memset(&fixture, 0, sizeof(fixture));
	fixture.planes = calloc(8U, sizeof(*fixture.planes));
	fixture.nodes = calloc(2U, sizeof(*fixture.nodes));
	fixture.leaves = calloc(3U, sizeof(*fixture.leaves));
	fixture.leaf_brushes = calloc(1U, sizeof(*fixture.leaf_brushes));
	fixture.models = calloc(1U, sizeof(*fixture.models));
	fixture.brushes = calloc(1U, sizeof(*fixture.brushes));
	fixture.brush_sides = calloc(6U, sizeof(*fixture.brush_sides));
	if (!fixture.planes || !fixture.nodes || !fixture.leaves ||
		!fixture.leaf_brushes || !fixture.models || !fixture.brushes ||
		!fixture.brush_sides)
	{
		fputs("walk fixture allocation failed\n", stderr);
		exit(2);
	}
	SetPlane(&fixture.planes[0], 0.0f, 0.0f, 1.0f, -24.0f);
	SetPlane(&fixture.planes[1], 1.0f, 0.0f, 0.0f, 0.0f);
	fixture.nodes[0].plane = 0U;
	fixture.nodes[0].children[0] = 1;
	fixture.nodes[0].children[1] = -3;
	fixture.nodes[1].plane = 1U;
	fixture.nodes[1].children[0] = -1;
	fixture.nodes[1].children[1] = -2;
	fixture.leaves[0].cluster = 0;
	fixture.leaves[0].area = 1U;
	fixture.leaves[1].cluster = 1;
	fixture.leaves[1].area = 2U;
	fixture.leaves[2].contents = SG_HOST_CONTENTS_SOLID;
	fixture.leaves[2].cluster = -1;
	fixture.leaves[2].area = 1U;
	fixture.leaves[2].first_leaf_brush = 0U;
	fixture.leaves[2].leaf_brush_count = 1U;
	fixture.leaf_brushes[0] = 0U;
	SetPlane(&fixture.planes[2], 1.0f, 0.0f, 0.0f, 4095.0f);
	SetPlane(&fixture.planes[3], -1.0f, 0.0f, 0.0f, 4096.0f);
	SetPlane(&fixture.planes[4], 0.0f, 1.0f, 0.0f, 4095.0f);
	SetPlane(&fixture.planes[5], 0.0f, -1.0f, 0.0f, 4096.0f);
	SetPlane(&fixture.planes[6], 0.0f, 0.0f, 1.0f, -24.125f);
	SetPlane(&fixture.planes[7], 0.0f, 0.0f, -1.0f, 4096.0f);
	fixture.brushes[0].first_side = 0U;
	fixture.brushes[0].side_count = 6U;
	fixture.brushes[0].contents = SG_HOST_CONTENTS_SOLID;
	for (side = 0U; side < 6U; side++)
	{
		fixture.brush_sides[side].plane = side + 2U;
		fixture.brush_sides[side].texinfo = -1;
	}
	fixture.models[0].headnode = 0;
	SetVector(fixture.models[0].mins.value,
		-4096.0f, -4096.0f, -4096.0f);
	SetVector(fixture.models[0].maxs.value,
		4095.0f, 4095.0f, 4095.0f);
	fixture.world.planes = fixture.planes;
	fixture.world.plane_count = 8U;
	fixture.world.nodes = fixture.nodes;
	fixture.world.node_count = 2U;
	fixture.world.leaves = fixture.leaves;
	fixture.world.leaf_count = 3U;
	fixture.world.leaf_brushes = fixture.leaf_brushes;
	fixture.world.leaf_brush_count = 1U;
	fixture.world.models = fixture.models;
	fixture.world.model_count = 1U;
	fixture.world.brushes = fixture.brushes;
	fixture.world.brush_count = 1U;
	fixture.world.brush_sides = fixture.brush_sides;
	fixture.world.brush_side_count = 6U;
	return fixture;
}

static void AcceptedSourceDestroy(accepted_source_fixture_t *fixture)
{
	SG_ConfigurationSemanticsDestroy(fixture->semantics);
	SG_ConfigurationDestroy(fixture->configuration);
	DestroyFixture(&fixture->world);
	memset(fixture, 0, sizeof(*fixture));
}

static int AcceptedSourceInit(accepted_source_fixture_t *fixture)
{
	sg_rune_model_identity_t identity = GroundIdentity();
	sg_host_collision_error_t host_error;
	sg_configuration_error_t configuration_error;
	sg_configuration_audit_result_t configuration_audit;
	sg_configuration_semantics_error_t semantics_error;
	sg_configuration_semantics_limits_t limits;

	memset(fixture, 0, sizeof(*fixture));
	fixture->world = WalkFixture();
	if (!SG_HostCollisionInit(&fixture->authority, &fixture->world.world,
			&identity, &host_error))
		goto fail;
	if (!SG_ConfigurationBuild(&fixture->authority, NULL,
			&fixture->configuration, &configuration_error) ||
		!SG_ConfigurationAudit(&fixture->authority, fixture->configuration,
			&configuration_audit))
		goto fail;
	SG_ConfigurationSemanticsDefaultLimits(&limits);
	if (!SG_ConfigurationSemanticsBuild(&fixture->authority,
			fixture->configuration, &limits, &fixture->semantics,
			&semantics_error))
		goto fail;
	fixture->candidate.identity = identity;
	fixture->source.authority = &fixture->authority;
	fixture->source.configuration = fixture->configuration;
	fixture->source.semantics = fixture->semantics;
	fixture->source.host_pmove = Pmove;
	fixture->source.host_law_identity = identity.physics_abi_id;
	return 1;

fail:
	AcceptedSourceDestroy(fixture);
	return 0;
}

static void TestAcceptedSourcesReachClosedPhaseSeam(void)
{
	accepted_source_fixture_t fixture;
	fake_phase_catalog_storage_t fake_catalog;
	sg_ground_capability_audit_result_t audit;
	sg_ground_capability_publication_t *publication = NULL;

	memset(&fake_catalog, 0, sizeof(fake_catalog));
	CHECK(AcceptedSourceInit(&fixture));
	if (!fixture.configuration || !fixture.semantics)
		return;
	CHECK(fixture.configuration->certificate_node_count > 0U);
	CHECK(fixture.semantics->boundary_count > 0U);
	CHECK(fixture.configuration->portal_count > 0U);
	CHECK(!SG_GroundCapabilityAudit(&fixture.source, &fixture.candidate,
		&audit));
	CHECK(audit.code == SG_GROUND_CAPABILITY_AUDIT_PHASE_CATALOG_REQUIRED);
	CHECK(audit.completeness ==
		SG_GROUND_CAPABILITY_COMPLETENESS_UNRESOLVED);
	fixture.source.phase_catalog =
		(const sg_phase_catalog_publication_t *)(const void *)&fake_catalog;
	CHECK(!SG_GroundCapabilityAudit(&fixture.source, &fixture.candidate,
		&audit));
	CHECK(audit.code ==
		SG_GROUND_CAPABILITY_AUDIT_PHASE_CATALOG_UNAVAILABLE);
	CHECK(!SG_GroundCapabilityPublicationIssue(&fixture.source,
		&fixture.candidate, &publication, &audit));
	CHECK(publication == NULL);
	CHECK(audit.code ==
		SG_GROUND_CAPABILITY_AUDIT_PHASE_CATALOG_UNAVAILABLE);
	AcceptedSourceDestroy(&fixture);
}

static void TestZeroCountsCannotBypassAcceptedAudits(void)
{
	accepted_source_fixture_t fixture;
	sg_ground_capability_audit_result_t audit;
	uint32_t certificate_count;
	uint32_t cell_count;
	uint32_t boundary_count;
	uint32_t region_cell;

	CHECK(AcceptedSourceInit(&fixture));
	if (!fixture.configuration || !fixture.semantics)
		return;
	certificate_count = fixture.configuration->certificate_node_count;
	cell_count = fixture.configuration->cell_count;
	boundary_count = fixture.semantics->boundary_count;
	CHECK(certificate_count > 0U && cell_count > 1U && boundary_count > 0U);
	fixture.configuration->certificate_node_count = 0U;
	fixture.configuration->cell_count = cell_count - 1U;
	fixture.semantics->boundary_count = 0U;
	CHECK(!SG_GroundCapabilityAudit(&fixture.source, &fixture.candidate,
		&audit));
	CHECK(audit.code == SG_GROUND_CAPABILITY_AUDIT_CONFIGURATION_REJECTED);
	CHECK(audit.completeness ==
		SG_GROUND_CAPABILITY_COMPLETENESS_UNRESOLVED);
	fixture.configuration->cell_count = cell_count;
	fixture.configuration->certificate_node_count = certificate_count;
	fixture.semantics->boundary_count = boundary_count;

	CHECK(boundary_count > 0U && fixture.semantics->region_count > 0U);
	region_cell = fixture.semantics->regions[0].cell;
	fixture.semantics->boundary_count = 0U;
	fixture.semantics->regions[0].cell = fixture.configuration->cell_count;
	CHECK(!SG_GroundCapabilityAudit(&fixture.source, &fixture.candidate,
		&audit));
	CHECK(audit.code == SG_GROUND_CAPABILITY_AUDIT_SEMANTICS_REJECTED);
	CHECK(audit.completeness ==
		SG_GROUND_CAPABILITY_COMPLETENESS_UNRESOLVED);
	fixture.semantics->regions[0].cell = region_cell;
	fixture.semantics->boundary_count = boundary_count;
	AcceptedSourceDestroy(&fixture);
}

static void TestAirborneOnlyCannotForgeProvenEmpty(void)
{
	const test_box_t floor = {
		{ -4096.0f, -4096.0f, -4096.0f },
		{ 4095.0f, 4095.0f, -24.1f }, SG_HOST_CONTENTS_SOLID
	};
	ground_fixture_t raw;
	sg_ground_phase_binding_t airborne[2];
	sg_ground_capability_set_t *candidate = NULL;
	sg_ground_capability_error_t error;
	accepted_source_fixture_t accepted;
	sg_ground_capability_audit_result_t audit;

	GroundFixtureInit(&raw, &floor, 1U, 800.0f,
		SG_RUNE_STANCE_STANDING, 0.0f, 0.0f);
	GroundFixtureRebind(&raw);
	airborne[0] = raw.bindings[1];
	airborne[1] = raw.bindings[3];
	raw.phases[1].velocity.z.min_value = 100.0f;
	raw.phases[1].velocity.z.max_value = 100.0f;
	raw.phases[3].velocity.z.min_value = 100.0f;
	raw.phases[3].velocity.z.max_value = 100.0f;
	CHECK(raw.configuration.portal_count == 1U);
	CHECK(SG_GroundCapabilityBuild(&raw.authority, &raw.configuration,
		&raw.semantics, raw.phases, 4U, airborne, 2U, Pmove,
		&candidate, &error));
	CHECK(candidate != NULL && candidate->capability_count == 0U);
	CHECK(AcceptedSourceInit(&accepted));
	if (candidate && accepted.configuration && accepted.semantics)
	{
		CHECK(accepted.configuration->portal_count > 0U);
		candidate->identity = accepted.authority.identity;
		CHECK(!SG_GroundCapabilityAudit(&accepted.source, candidate, &audit));
		CHECK(audit.code ==
			SG_GROUND_CAPABILITY_AUDIT_PHASE_CATALOG_REQUIRED);
		CHECK(audit.completeness ==
			SG_GROUND_CAPABILITY_COMPLETENESS_UNRESOLVED);
	}
	AcceptedSourceDestroy(&accepted);
	SG_GroundCapabilityDestroy(candidate);
	GroundFixtureDestroy(&raw);
}

static void TestExactFactsUseCanonicalFloatBits(void)
{
	sg_ground_capability_t left;
	sg_ground_capability_t right;

	memset(&left, 0, sizeof(left));
	right = left;
	CHECK(SG_GroundCapabilityFactBitsEqual(&left, &right));
	right.gravity = -0.0f;
	CHECK(!SG_GroundCapabilityFactBitsEqual(&left, &right));
	right = left;
	right.source_witness.value[1] = -0.0f;
	CHECK(!SG_GroundCapabilityFactBitsEqual(&left, &right));
	right = left;
	right.duration_ms.max_value = -0.0f;
	CHECK(!SG_GroundCapabilityFactBitsEqual(&left, &right));
	CHECK(!SG_GroundCapabilityFactBitsEqual(NULL, &right));
}

static void TestInvalidArgumentsStayFailClosed(void)
{
	sg_ground_capability_audit_result_t audit;
	sg_ground_capability_publication_t *publication = NULL;

	memset(&audit, 0xa5, sizeof(audit));
	CHECK(!SG_GroundCapabilityAudit(NULL, NULL, &audit));
	CHECK(audit.code == SG_GROUND_CAPABILITY_AUDIT_INVALID_ARGUMENT);
	CHECK(audit.completeness ==
		SG_GROUND_CAPABILITY_COMPLETENESS_UNRESOLVED);
	CHECK(!SG_GroundCapabilityPublicationIssue(NULL, NULL, NULL, &audit));
	CHECK(audit.code == SG_GROUND_CAPABILITY_AUDIT_INVALID_ARGUMENT);
	CHECK(!SG_GroundCapabilityPublicationIssue(NULL, NULL, &publication,
		&audit));
	CHECK(publication == NULL);
	CHECK(!SG_GroundCapabilityPublicationDescribe(NULL, NULL));
	CHECK(!SG_GroundCapabilityPublicationFact(NULL, 0U, NULL));
	SG_GroundCapabilityPublicationDestroy(NULL);
}

int main(void)
{
	if (RunGroundCapabilityTests() != 0)
		return 1;
	failures = 0;
	TestAcceptedSourcesReachClosedPhaseSeam();
	TestZeroCountsCannotBypassAcceptedAudits();
	TestAirborneOnlyCannotForgeProvenEmpty();
	TestExactFactsUseCanonicalFloatBits();
	TestInvalidArgumentsStayFailClosed();
	if (failures)
	{
		fprintf(stderr, "%d ground publication test failure(s)\n", failures);
		return 1;
	}
	puts("ground capability publication fail-closed checks passed");
	return 0;
}
