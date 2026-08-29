/* Exercise the production publication barrier all the way into the frozen
 * RUNE model validator. */
int SG_RuneModelContractFixtureMain(void);
#define main SG_RuneModelContractFixtureMain
#include "sg_rune_model_contract_test.c"
#undef main

#include "../slipgate/sg_mechanism_capability_internal.h"
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
	fixture->capabilities.identity = *identity;
	fixture->capabilities.candidate_verifier_identity = UINT64_C(0x1111);
	fixture->capabilities.trace_verifier_identity = UINT64_C(0x2222);
	SG_MechanismCapabilitySetStamp(&fixture->capabilities);
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

int main(void)
{
	TestPublishedPortalTransitionsValidate();
	if (failures != 0)
		return 1;
	puts("sg_phase_catalog_model_integration_test: ok");
	return 0;
}
