/* Exercise the production publication barrier all the way into the frozen
 * RUNE model validator. */
int SG_RuneModelContractFixtureMain(void);
#define main SG_RuneModelContractFixtureMain
#include "sg_rune_model_contract_test.c"
#undef main

#include "../slipgate/sg_phase_catalog_owner.h"

typedef struct catalog_model_source_fixture_s
{
	sg_host_collision_authority_t authority;
	sg_configuration_space_t configuration;
	sg_configuration_cell_t cells[2];
	sg_configuration_portal_t portal;
	sg_configuration_semantics_t semantics;
	sg_configuration_semantic_region_t regions[2];
	sg_mechanism_capability_set_t capabilities;
	sg_phase_catalog_publication_t *publication;
} catalog_model_source_fixture_t;

/* This fixture has no mechanism rows, so it issues a private test-only empty
 * capability result.  The production issuer remains static inside the real
 * mechanism builder and is not reachable through a public stamp function. */
static uint64_t TestDigestBytes(uint64_t digest, const void *data, size_t size)
{
	const unsigned char *bytes = data;
	size_t index;

	for (index = 0U; index < size; index++)
		digest = (digest ^ (uint64_t)bytes[index]) * UINT64_C(1099511628211);
	return digest;
}

static void TestSealAccepted(sg_mechanism_capability_set_t *capabilities)
{
	uint64_t digest;
	const uint64_t key = UINT64_C(0x8f2c6a4d9137be25);

	digest = SG_MechanismCapabilitySetDigest(capabilities);
	capabilities->seal_magic = SG_MECHANISM_CAPABILITY_SEAL_MAGIC;
	capabilities->seal_magic_inverse = ~SG_MECHANISM_CAPABILITY_SEAL_MAGIC;
	capabilities->self = capabilities;
	digest = TestDigestBytes(digest, &capabilities->self,
		sizeof(capabilities->self));
	capabilities->seal_digest = TestDigestBytes(digest, &key, sizeof(key));
}

static void TestIssueAcceptedEmpty(sg_mechanism_capability_set_t *capabilities,
	const sg_rune_model_identity_t *identity)
{
	memset(capabilities, 0, sizeof(*capabilities));
	capabilities->identity = *identity;
	TestSealAccepted(capabilities);
}

static void SetCatalogCell(sg_configuration_cell_t *cell,
	const sg_rune_model_identity_t *identity, uint32_t index)
{
	memset(cell, 0, sizeof(*cell));
	cell->order.source_set_identity = identity->source_set_identity;
	cell->order.domain = SG_RUNE_ORDER_CELL;
	/* Match the model-contract fixture's canonical cell order keys.  The
	 * configuration array position remains the semantic cell index. */
	cell->order.source_index = 7U;
	cell->order.local_ordinal = index;
	cell->order.variant = 0U;
	cell->id.value = SG_RuneModelStableIdFromOrderKey(&cell->order);
	cell->stance = SG_RUNE_STANCE_STANDING;
}

static void SetCatalogRegion(sg_configuration_semantic_region_t *region,
	uint32_t index, uint32_t cell)
{
	memset(region, 0, sizeof(*region));
	region->id = ((uint64_t)cell << 32) | (uint64_t)index;
	region->cell = cell;
	region->flags = SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED;
	region->bounds.mins = (sg_rune_vec3_t){ { -16.0f, -16.0f, -24.0f } };
	region->bounds.maxs = (sg_rune_vec3_t){ { 16.0f, 16.0f, 32.0f } };
	region->interior_witness = (sg_rune_vec3_t){ { 0.0f, 0.0f, 0.0f } };
}

static void InitCatalogModelSource(catalog_model_source_fixture_t *fixture,
	const sg_rune_model_identity_t *identity)
{
	memset(fixture, 0, sizeof(*fixture));
	fixture->authority.identity = *identity;
	fixture->configuration.identity = *identity;
	fixture->configuration.cells = fixture->cells;
	fixture->configuration.cell_count = 2U;
	SetCatalogCell(&fixture->cells[0], identity, 0U);
	SetCatalogCell(&fixture->cells[1], identity, 1U);
	SetCatalogRegion(&fixture->regions[0], 0U, 0U);
	SetCatalogRegion(&fixture->regions[1], 1U, 1U);
	fixture->semantics.identity = *identity;
	fixture->semantics.regions = fixture->regions;
	fixture->semantics.region_count = 2U;
	fixture->portal.order.source_set_identity = identity->source_set_identity;
	fixture->portal.order.domain = SG_RUNE_ORDER_PORTAL;
	fixture->portal.order.source_index = 0U;
	fixture->portal.order.local_ordinal = 0U;
	fixture->portal.order.variant = 0U;
	fixture->portal.id.value = SG_RuneModelStableIdFromOrderKey(
		&fixture->portal.order);
	fixture->portal.from_cell = 0U;
	fixture->portal.to_cell = 1U;
	fixture->portal.stance = SG_RUNE_STANCE_STANDING;
	fixture->portal.plane.normal[0] = 1.0f;
	fixture->portal.clearance = 1.0f;
	fixture->configuration.portals = &fixture->portal;
	fixture->configuration.portal_count = 1U;
	TestIssueAcceptedEmpty(&fixture->capabilities, identity);
}

static void TestPublishedPortalTransitionsValidate(void)
{
	model_fixture_t model_fixture;
	catalog_model_source_fixture_t source_fixture;
	const sg_phase_catalog_view_t *view = NULL;
	sg_phase_catalog_error_t error;
	sg_phase_catalog_audit_result_t audit;
	uint32_t index;

	InitFixture(&model_fixture);
	InitCatalogModelSource(&source_fixture, &model_fixture.model.identity);
	CHECK(SG_PhaseCatalogPublicationBuild(&source_fixture.authority,
		&source_fixture.configuration, &source_fixture.semantics,
		&source_fixture.capabilities, &source_fixture.publication, &error,
		&audit));
	CHECK(source_fixture.publication != NULL);
	if (!source_fixture.publication)
		return;
	CHECK(SG_PhaseCatalogPublicationRead(source_fixture.publication, &view));
	CHECK(view != NULL && view->phase_count == 2U &&
		view->transition_count == 2U);
	if (!view)
	{
		SG_PhaseCatalogPublicationDestroy(source_fixture.publication);
		return;
	}
	for (index = 0U; index < view->transition_count; index++)
	{
		CHECK(view->transitions[index].kind == SG_RUNE_PHASE_TRANSITION_PORTAL);
		CHECK((view->transitions[index].flags &
			SG_RUNE_PHASE_TRANSITION_CROSS_CELL) != 0U);
		CHECK(view->transition_evidence[index].source_cell !=
			view->transition_evidence[index].destination_cell);
		CHECK(view->transitions[index].duration_ms.min_value == 0.0f);
		CHECK(view->transitions[index].duration_ms.max_value == 0.0f);
		CHECK(view->transition_evidence[index].portal_duration_ms == 0U);
	}

	/* Reuse the fully populated model fixture for geometry and completeness,
	 * replacing only the phase/transition arrays with the immutable view. */
	model_fixture.model.phases = view->phases;
	model_fixture.model.phase_count = view->phase_count;
	model_fixture.model.phase_transitions = view->transitions;
	model_fixture.model.phase_transition_count = view->transition_count;
	model_fixture.cells[0].phases = (sg_rune_phase_span_t){ 0U, 1U };
	model_fixture.cells[1].phases = (sg_rune_phase_span_t){ 1U, 1U };
	/* The generic fixture's hook kernel references a fifth phase; this
	 * integration is intentionally about the published transition array. */
	model_fixture.cells[0].kernels = (sg_rune_kernel_span_t){ 0U, 0U };
	model_fixture.model.kernels = NULL;
	model_fixture.model.kernel_count = 0U;
	model_fixture.portals[0].phases = (sg_rune_phase_span_t){ 0U, 2U };
	SetEvidence(&model_fixture.model, &model_fixture.evidence);
	CHECK(SG_RuneModelValidate(&model_fixture.model,
		&model_fixture.evidence) == SG_RUNE_FAILURE_NONE);
	SG_PhaseCatalogPublicationDestroy(source_fixture.publication);
}

typedef struct mechanism_heavy_source_fixture_s
{
	sg_host_collision_authority_t authority;
	sg_configuration_space_t configuration;
	sg_configuration_cell_t cells[2];
	sg_configuration_semantics_t semantics;
	sg_configuration_semantic_region_t regions[3];
	sg_mechanism_capability_set_t capabilities;
	sg_mechanism_capability_fact_t *facts;
	uint32_t *facts_by_trace;
} mechanism_heavy_source_fixture_t;

static void InitHeavySource(mechanism_heavy_source_fixture_t *fixture,
	const sg_rune_model_identity_t *identity, uint32_t fact_count)
{
	uint32_t index;

	memset(fixture, 0, sizeof(*fixture));
	fixture->authority.identity = *identity;
	fixture->configuration.identity = *identity;
	fixture->configuration.cells = fixture->cells;
	fixture->configuration.cell_count = 2U;
	SetCatalogCell(&fixture->cells[0], identity, 0U);
	SetCatalogCell(&fixture->cells[1], identity, 1U);
	fixture->semantics.identity = *identity;
	fixture->semantics.regions = fixture->regions;
	fixture->semantics.region_count = 3U;
	SetCatalogRegion(&fixture->regions[0], 0U, 0U);
	SetCatalogRegion(&fixture->regions[1], 1U, 0U);
	SetCatalogRegion(&fixture->regions[2], 2U, 1U);
	fixture->facts = calloc(fact_count == 0U ? 1U : (size_t)fact_count,
		sizeof(*fixture->facts));
	fixture->facts_by_trace = calloc(fact_count == 0U ? 1U :
		(size_t)fact_count, sizeof(*fixture->facts_by_trace));
	if (!fixture->facts || !fixture->facts_by_trace)
		return;
	fixture->capabilities.identity = *identity;
	fixture->capabilities.facts = fixture->facts;
	fixture->capabilities.fact_count = fact_count;
	fixture->capabilities.facts_by_trace = fixture->facts_by_trace;
	for (index = 0U; index < fact_count; index++)
	{
		sg_mechanism_capability_fact_t *fact = &fixture->facts[index];

		memset(fact, 0, sizeof(*fact));
		fact->order = index;
		fact->trace_identity = UINT64_C(0x80000000) + index;
		fact->mechanism_id = MechanismId(0U);
		fact->source_region = 0U;
		/* Both endpoint regions are in the heavy source cell.  The third
		 * region keeps the model's required second mechanism endpoint out
		 * of this per-cell boundary count. */
		fact->destination_region = 1U;
		fact->kind = SG_MECHANISM_CAPABILITY_BUTTON_ACTIVATION;
		fact->source_state = SG_MECHANISM_STATE_INACTIVE;
		fact->destination_state = SG_MECHANISM_STATE_ACTIVATING;
		fact->delay_ms = index + 1U;
		fixture->facts_by_trace[index] = index;
	}
	TestSealAccepted(&fixture->capabilities);
}

static void DestroyHeavySource(mechanism_heavy_source_fixture_t *fixture)
{
	free(fixture->facts);
	free(fixture->facts_by_trace);
	memset(fixture, 0, sizeof(*fixture));
}

static void ConfigureHeavyModel(model_fixture_t *model_fixture,
	const sg_phase_catalog_view_t *view)
{
	sg_rune_mechanism_t *mechanism = &model_fixture->mechanisms[0];

	model_fixture->model.phases = view->phases;
	model_fixture->model.phase_count = view->phase_count;
	model_fixture->model.phase_transitions = view->transitions;
	model_fixture->model.phase_transition_count = view->transition_count;
	model_fixture->cells[0].phases = (sg_rune_phase_span_t){ 0U,
		SG_RUNE_MODEL_MAX_CELL_PHASES };
	model_fixture->cells[1].phases = (sg_rune_phase_span_t){
		SG_RUNE_MODEL_MAX_CELL_PHASES, 1U };
	model_fixture->cells[0].surfaces = (sg_rune_surface_span_t){ 0U, 0U };
	model_fixture->cells[0].affordances =
		(sg_rune_affordance_span_t){ 0U, 0U };
	model_fixture->cells[0].kernels = (sg_rune_kernel_span_t){ 0U, 0U };
	model_fixture->cells[0].landmarks =
		(sg_rune_landmark_span_t){ 0U, 0U };
	model_fixture->cells[0].mechanisms =
		(sg_rune_mechanism_span_t){ 0U, 1U };
	model_fixture->cells[1].surfaces = (sg_rune_surface_span_t){ 0U, 0U };
	model_fixture->cells[1].affordances =
		(sg_rune_affordance_span_t){ 0U, 0U };
	model_fixture->cells[1].kernels = (sg_rune_kernel_span_t){ 0U, 0U };
	model_fixture->cells[1].landmarks =
		(sg_rune_landmark_span_t){ 0U, 0U };
	model_fixture->cells[1].mechanisms =
		(sg_rune_mechanism_span_t){ 0U, 0U };
	model_fixture->model.portals = NULL;
	model_fixture->model.portal_count = 0U;
	model_fixture->model.portal_vertices = NULL;
	model_fixture->model.portal_vertex_count = 0U;
	model_fixture->model.surfaces = NULL;
	model_fixture->model.surface_count = 0U;
	model_fixture->model.affordances = NULL;
	model_fixture->model.affordance_count = 0U;
	model_fixture->model.kernels = NULL;
	model_fixture->model.kernel_count = 0U;
	model_fixture->model.landmarks = NULL;
	model_fixture->model.landmark_count = 0U;
	model_fixture->model.mechanisms = model_fixture->mechanisms;
	model_fixture->model.mechanism_count = 1U;
	mechanism->id = MechanismId(0U);
	mechanism->order = Order(SG_RUNE_ORDER_MECHANISM, 0U);
	mechanism->kind = SG_RUNE_MECHANISM_DOOR;
	mechanism->entry_cell = model_fixture->cells[0].id;
	mechanism->exit_cell = model_fixture->cells[1].id;
	mechanism->activation_landmark = SG_RUNE_LANDMARK_REF_NONE;
	mechanism->entity = (sg_rune_entity_ref_t){ 1U, 1U };
	mechanism->dwell_ms = (sg_rune_interval_t){ 0.0f, 250.0f };
	mechanism->travel_ms = (sg_rune_interval_t){ 250.0f, 750.0f };
	model_fixture->model.completeness.expected_cells = 2U;
	model_fixture->model.completeness.covered_cells = 2U;
	model_fixture->model.completeness.expected_portals = 0U;
	model_fixture->model.completeness.covered_portals = 0U;
	SetEvidence(&model_fixture->model, &model_fixture->evidence);
}

static void TestMechanismHeavyCellPhaseLimit(void)
{
	model_fixture_t model_fixture;
	mechanism_heavy_source_fixture_t source_fixture;
	const sg_phase_catalog_view_t *view = NULL;
	sg_phase_catalog_publication_t *publication = NULL;
	sg_phase_catalog_error_t error;
	sg_phase_catalog_audit_result_t audit;
	uint32_t at_limit = SG_RUNE_MODEL_MAX_CELL_PHASES - 2U;
	uint32_t over_limit = at_limit + 1U;

	InitFixture(&model_fixture);
	/* The elapsed variants are deliberately all below one authoritative
	 * frame, so each mechanism timing remains a distinct phase. */
	model_fixture.model.identity.physics.frame_ms = 100U;
	model_fixture.model.identity.physics.substep_ms = 10U;
	InitHeavySource(&source_fixture, &model_fixture.model.identity, at_limit);
	CHECK(source_fixture.facts != NULL && source_fixture.facts_by_trace != NULL);
	if (!source_fixture.facts || !source_fixture.facts_by_trace)
	{
		DestroyHeavySource(&source_fixture);
		return;
	}
	CHECK(SG_PhaseCatalogPublicationBuild(&source_fixture.authority,
		&source_fixture.configuration, &source_fixture.semantics,
		&source_fixture.capabilities, &publication, &error, &audit));
	CHECK(publication != NULL);
	if (publication)
	{
		CHECK(SG_PhaseCatalogPublicationRead(publication, &view));
		CHECK(view != NULL && view->phase_count ==
			SG_RUNE_MODEL_MAX_CELL_PHASES + 1U);
		CHECK(view != NULL && view->transition_count == at_limit);
		if (view)
		{
			ConfigureHeavyModel(&model_fixture, view);
			CHECK(SG_RuneModelValidate(&model_fixture.model,
				&model_fixture.evidence) == SG_RUNE_FAILURE_NONE);
		}
		SG_PhaseCatalogPublicationDestroy(publication);
		publication = NULL;
	}
	DestroyHeavySource(&source_fixture);

	InitHeavySource(&source_fixture, &model_fixture.model.identity, over_limit);
	CHECK(source_fixture.facts != NULL && source_fixture.facts_by_trace != NULL);
	if (source_fixture.facts && source_fixture.facts_by_trace)
	{
		CHECK(!SG_PhaseCatalogPublicationBuild(&source_fixture.authority,
			&source_fixture.configuration, &source_fixture.semantics,
			&source_fixture.capabilities, &publication, &error, &audit));
		CHECK(publication == NULL);
		CHECK(error.code == SG_PHASE_CATALOG_ERROR_OVERFLOW);
	}
	DestroyHeavySource(&source_fixture);
}

int main(void)
{
	TestPublishedPortalTransitionsValidate();
	TestMechanismHeavyCellPhaseLimit();
	if (failures != 0)
		return 1;
	puts("sg_phase_catalog_model_integration_test: ok");
	return 0;
}
