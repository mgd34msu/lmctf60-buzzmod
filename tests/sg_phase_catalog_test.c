#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "slipgate/sg_phase_catalog.h"

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

typedef struct fixture_s
{
	sg_host_collision_authority_t authority;
	sg_configuration_space_t configuration;
	sg_configuration_cell_t cells[2];
	sg_configuration_semantics_t semantics;
	sg_configuration_semantic_region_t regions[4];
	sg_configuration_semantic_face_t faces[4];
	sg_phase_mover_support_t movers[2];
	sg_phase_catalog_source_t source;
} fixture_t;

static void Set3(float value[3], float x, float y, float z)
{
	value[0] = x;
	value[1] = y;
	value[2] = z;
}

static sg_rune_model_identity_t Identity(void)
{
	sg_rune_model_identity_t identity;

	memset(&identity, 0, sizeof(identity));
	identity.bsp_content_id = UINT64_C(0x1001);
	identity.entity_semantics_id = UINT64_C(0x1002);
	identity.physics_abi_id = UINT64_C(0x1003);
	identity.source_set_identity = UINT64_C(0x1004);
	identity.schema_id = UINT64_C(0x1005);
	identity.producer_identity = UINT64_C(0x1006);
	Set3(identity.standing_hull.mins.value, -16.0f, -16.0f, -24.0f);
	Set3(identity.standing_hull.maxs.value, 16.0f, 16.0f, 32.0f);
	Set3(identity.crouching_hull.mins.value, -16.0f, -16.0f, -24.0f);
	Set3(identity.crouching_hull.maxs.value, 16.0f, 16.0f, 4.0f);
	identity.physics.gravity = 800.0f;
	identity.physics.ground_acceleration = 10.0f;
	identity.physics.air_acceleration = 1.0f;
	identity.physics.water_acceleration = 10.0f;
	identity.physics.hook_acceleration = 800.0f;
	identity.physics.external_acceleration = 1000.0f;
	identity.physics.water_drag = 1.0f;
	identity.physics.max_velocity = 2000.0f;
	identity.physics.frame_ms = 100U;
	identity.physics.substep_ms = 10U;
	return identity;
}

static sg_rune_mechanism_ref_t Mechanism(uint32_t source_index)
{
	sg_rune_order_key_t order = {
		UINT64_C(0x1004), SG_RUNE_ORDER_MECHANISM,
		source_index, 0U, 0U
	};
	sg_rune_mechanism_ref_t result;

	result.value = SG_RuneModelStableIdFromOrderKey(&order);
	return result;
}

static void Region(fixture_t *fixture, uint32_t index, uint32_t cell,
	uint32_t flags, uint8_t water_level, uint32_t water_type)
{
	sg_configuration_semantic_region_t *region = &fixture->regions[index];

	memset(region, 0, sizeof(*region));
	region->id = UINT64_C(100) + index;
	region->cell = cell;
	region->first_face = index;
	region->face_count = 1U;
	region->flags = flags;
	region->water_level = water_level;
	region->water_type = water_type;
	region->origin_contents = water_type;
	region->sample_contents[0] = water_type;
	region->sample_contents[1] = water_type;
	region->sample_contents[2] = water_type;
	Set3(region->bounds.mins.value, -16.0f, -16.0f, (float)index);
	Set3(region->bounds.maxs.value, 16.0f, 16.0f, (float)index + 1.0f);
	Set3(region->interior_witness.value, 0.0f, 0.0f,
		(float)index + 0.5f);
}

static void FixtureInit(fixture_t *fixture)
{
	sg_rune_model_identity_t identity = Identity();

	memset(fixture, 0, sizeof(*fixture));
	fixture->authority.identity = identity;
	fixture->configuration.identity = identity;
	fixture->configuration.cells = fixture->cells;
	fixture->configuration.cell_count = 2U;
	fixture->cells[0].stance = SG_RUNE_STANCE_STANDING;
	fixture->cells[1].stance = SG_RUNE_STANCE_CROUCHING;
	fixture->semantics.identity = identity;
	fixture->semantics.regions = fixture->regions;
	fixture->semantics.region_count = 4U;
	fixture->semantics.faces = fixture->faces;
	fixture->semantics.face_count = 4U;
	Region(fixture, 0U, 0U,
		SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED, 0U, 0U);
	Region(fixture, 1U, 0U,
		SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED, 0U, 0U);
	Region(fixture, 2U, 0U,
		SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE |
		SG_CONFIGURATION_SEMANTIC_REGION_VOID_ADJACENT, 0U, 0U);
	Region(fixture, 3U, 1U,
		SG_CONFIGURATION_SEMANTIC_REGION_WATER |
		SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE, 3U,
		SG_HOST_CONTENTS_WATER);
	fixture->movers[0].semantic_region_id = fixture->regions[2].id;
	fixture->movers[0].mechanism = Mechanism(4U);
	fixture->movers[0].mechanism_state_mask =
		SG_PHASE_MECHANISM_STATE_INACTIVE |
		SG_PHASE_MECHANISM_STATE_ACTIVE;
	fixture->movers[1].semantic_region_id = fixture->regions[2].id;
	fixture->movers[1].mechanism = Mechanism(5U);
	fixture->movers[1].mechanism_state_mask =
		SG_PHASE_MECHANISM_STATE_DWELLING;
	fixture->source.authority = &fixture->authority;
	fixture->source.configuration = &fixture->configuration;
	fixture->source.semantics = &fixture->semantics;
	fixture->source.mover_supports = fixture->movers;
	fixture->source.mover_support_count = 2U;
	fixture->source.mover_support_completion = SG_PHASE_CATALOG_COMPLETE;
	fixture->source.mover_support_verifier_identity = UINT64_C(0x2001);
}

static void TestCompleteCatalogAndStableMembership(void)
{
	fixture_t fixture;
	sg_phase_catalog_t *catalog = NULL;
	sg_phase_catalog_error_t error;
	sg_phase_catalog_audit_result_t audit;
	const sg_phase_catalog_binding_t *bindings;
	uint32_t binding_count;

	FixtureInit(&fixture);
	CHECK(SG_PhaseCatalogBuild(&fixture.source, &catalog, &error));
	CHECK(catalog != NULL);
	if (!catalog)
		return;
	CHECK(catalog->completion == SG_PHASE_CATALOG_COMPLETE);
	CHECK(catalog->phase_count == 5U);
	CHECK(catalog->binding_count == 6U);
	CHECK(catalog->transition_completion == SG_PHASE_CATALOG_COMPLETE);
	CHECK(SG_PhaseCatalogAudit(&fixture.source, catalog, &audit));
	CHECK(audit.code == SG_PHASE_CATALOG_AUDIT_OK_COMPLETE);
	CHECK(audit.proved_phases == 5U);
	CHECK(SG_PhaseCatalogBindingsForRegion(catalog,
		fixture.regions[0].id, &bindings, &binding_count));
	CHECK(binding_count == 1U);
	CHECK(bindings != NULL && bindings[0].configuration_cell == 0U);
	CHECK(SG_PhaseCatalogBindingsForRegion(catalog,
		fixture.regions[2].id, &bindings, &binding_count));
	CHECK(binding_count == 3U);
	SG_PhaseCatalogDestroy(catalog);
}

static void TestProvenEmpty(void)
{
	fixture_t fixture;
	sg_phase_catalog_t *catalog = NULL;
	sg_phase_catalog_error_t error;
	sg_phase_catalog_audit_result_t audit;

	FixtureInit(&fixture);
	fixture.configuration.cell_count = 0U;
	fixture.configuration.cells = NULL;
	fixture.semantics.region_count = 0U;
	fixture.semantics.regions = NULL;
	fixture.semantics.face_count = 0U;
	fixture.semantics.faces = NULL;
	fixture.source.mover_supports = NULL;
	fixture.source.mover_support_count = 0U;
	fixture.source.mover_support_completion = SG_PHASE_CATALOG_PROVEN_EMPTY;
	CHECK(SG_PhaseCatalogBuild(&fixture.source, &catalog, &error));
	CHECK(catalog != NULL);
	if (!catalog)
		return;
	CHECK(catalog->completion == SG_PHASE_CATALOG_PROVEN_EMPTY);
	CHECK(catalog->phase_count == 0U);
	CHECK(catalog->transition_completion == SG_PHASE_CATALOG_PROVEN_EMPTY);
	CHECK(SG_PhaseCatalogAudit(&fixture.source, catalog, &audit));
	CHECK(audit.code == SG_PHASE_CATALOG_AUDIT_OK_PROVEN_EMPTY);
	SG_PhaseCatalogDestroy(catalog);
}

static void TestAuditRejectsEveryRecordClass(void)
{
	fixture_t fixture;
	sg_phase_catalog_t *catalog = NULL;
	sg_phase_catalog_error_t error;
	sg_phase_catalog_audit_result_t audit;
	uint32_t saved_count;
	sg_rune_phase_basis_t saved_phase;
	sg_phase_catalog_binding_t saved_binding;

	FixtureInit(&fixture);
	CHECK(SG_PhaseCatalogBuild(&fixture.source, &catalog, &error));
	if (!catalog)
		return;
	saved_count = catalog->phase_count;
	catalog->phase_count--;
	CHECK(!SG_PhaseCatalogAudit(&fixture.source, catalog, &audit));
	CHECK(audit.code == SG_PHASE_CATALOG_AUDIT_OMITTED_PHASE);
	catalog->phase_count = saved_count;
	saved_phase = catalog->phases[1];
	catalog->phases[1] = catalog->phases[0];
	CHECK(!SG_PhaseCatalogAudit(&fixture.source, catalog, &audit));
	CHECK(audit.code == SG_PHASE_CATALOG_AUDIT_DUPLICATE_PHASE);
	catalog->phases[1] = saved_phase;
	saved_binding = catalog->bindings[0];
	catalog->bindings[0].phase = SG_RUNE_PHASE_REF_NONE;
	CHECK(!SG_PhaseCatalogAudit(&fixture.source, catalog, &audit));
	CHECK(audit.code == SG_PHASE_CATALOG_AUDIT_UNRESOLVED_BINDING);
	catalog->bindings[0] = saved_binding;
	fixture.source.mover_supports[0].mechanism_state_mask = 0U;
	CHECK(!SG_PhaseCatalogAudit(&fixture.source, catalog, &audit));
	CHECK(audit.code == SG_PHASE_CATALOG_AUDIT_INVALID_SOURCE);
	SG_PhaseCatalogDestroy(catalog);
}

static void TestPublicationOwnsAuditedRecords(void)
{
	fixture_t fixture;
	sg_phase_catalog_t *catalog = NULL;
	sg_phase_catalog_publication_t *publication = NULL;
	const sg_phase_catalog_view_t *view = NULL;
	sg_phase_catalog_error_t error;
	sg_phase_catalog_audit_result_t audit;
	sg_rune_phase_basis_t first;

	FixtureInit(&fixture);
	CHECK(SG_PhaseCatalogBuild(&fixture.source, &catalog, &error));
	CHECK(SG_PhaseCatalogPublicationIssue(&fixture.source, catalog,
		&publication, &audit));
	CHECK(publication != NULL);
	CHECK(SG_PhaseCatalogPublicationRead(publication, &view));
	CHECK(view != NULL && view->phase_count == 5U);
	if (!view)
		return;
	first = view->phases[0];
	catalog->phases[0].velocity.x.max_value = 1.0f;
	fixture.regions[0].flags = SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE;
	CHECK(SG_PhaseCatalogPublicationRead(publication, &view));
	CHECK(!memcmp(&first, &view->phases[0], sizeof(first)));
	CHECK(!SG_PhaseCatalogPublicationStorageOverlaps(publication,
		catalog->phases,
		(size_t)catalog->phase_count * sizeof(*catalog->phases)));
	CHECK(SG_PhaseCatalogPublicationStorageOverlaps(publication,
		view->phases, (size_t)view->phase_count * sizeof(*view->phases)));
	SG_PhaseCatalogDestroy(catalog);
	SG_PhaseCatalogPublicationDestroy(publication);
}

static void TestIdentityAndMoverCoverageFailClosed(void)
{
	fixture_t fixture;
	sg_phase_catalog_t *catalog = NULL;
	sg_phase_catalog_error_t error;

	FixtureInit(&fixture);
	fixture.semantics.identity.physics_abi_id++;
	CHECK(!SG_PhaseCatalogBuild(&fixture.source, &catalog, &error));
	CHECK(error.code == SG_PHASE_CATALOG_ERROR_IDENTITY_MISMATCH);
	FixtureInit(&fixture);
	fixture.source.mover_support_completion = SG_PHASE_CATALOG_PROVEN_EMPTY;
	CHECK(!SG_PhaseCatalogBuild(&fixture.source, &catalog, &error));
	CHECK(error.code == SG_PHASE_CATALOG_ERROR_INVALID_SOURCE);
	FixtureInit(&fixture);
	fixture.source.mover_support_count = 0U;
	fixture.source.mover_supports = NULL;
	fixture.source.mover_support_completion = SG_PHASE_CATALOG_COMPLETE;
	CHECK(!SG_PhaseCatalogBuild(&fixture.source, &catalog, &error));
	CHECK(error.code == SG_PHASE_CATALOG_ERROR_INCOMPLETE_SOURCE);
}

int main(void)
{
	TestCompleteCatalogAndStableMembership();
	TestProvenEmpty();
	TestAuditRejectsEveryRecordClass();
	TestPublicationOwnsAuditedRecords();
	TestIdentityAndMoverCoverageFailClosed();
	if (failures)
	{
		fprintf(stderr, "%d phase catalog checks failed\n", failures);
		return 1;
	}
	puts("phase catalog checks passed");
	return 0;
}
