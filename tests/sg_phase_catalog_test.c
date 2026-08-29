#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "slipgate/sg_phase_catalog_internal.h"

static int phase_failures;

#define CHECK_PHASE(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		phase_failures++; \
	} \
} while (0)

typedef struct phase_fixture_s
{
	sg_host_collision_authority_t authority;
	sg_configuration_space_t configuration;
	sg_configuration_cell_t *cells;
	sg_configuration_semantics_t semantics;
	sg_configuration_semantic_region_t *regions;
	sg_phase_catalog_non_authoritative_source_t derivation;
} phase_fixture_t;

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

static void SetCell(sg_configuration_cell_t *cell,
	const sg_rune_model_identity_t *identity, uint32_t index,
	sg_rune_stance_t stance)
{
	memset(cell, 0, sizeof(*cell));
	cell->order.source_set_identity = identity->source_set_identity;
	cell->order.domain = SG_RUNE_ORDER_CELL;
	cell->order.source_index = index;
	cell->order.local_ordinal = 0U;
	cell->order.variant = 0U;
	cell->id.value = SG_RuneModelStableIdFromOrderKey(&cell->order);
	cell->stance = stance;
}

static void SetRegion(sg_configuration_semantic_region_t *region,
	uint64_t id, uint32_t cell, uint32_t flags)
{
	memset(region, 0, sizeof(*region));
	region->id = id;
	region->cell = cell;
	region->flags = flags;
	region->water_level = 0U;
	region->water_type = 0U;
	region->origin_contents = 0U;
	Set3(region->bounds.mins.value, -16.0f, -16.0f, -24.0f);
	Set3(region->bounds.maxs.value, 16.0f, 16.0f, 32.0f);
	Set3(region->interior_witness.value, 0.0f, 0.0f, 0.0f);
}

static int DeriveCatalogNonAuthoritative(
	const sg_phase_catalog_non_authoritative_source_t *source,
	sg_phase_catalog_t **catalog_out, sg_phase_catalog_error_t *error_out)
{
	sg_phase_catalog_expected_t expected;
	sg_phase_catalog_t *catalog;

	if (!source || !source->authority || !catalog_out || *catalog_out)
		return 0;
	memset(&expected, 0, sizeof(expected));
	if (!SG_PhaseCatalogDeriveExpectedNonAuthoritative(source, &expected,
		error_out))
		return 0;
	catalog = calloc(1U, sizeof(*catalog));
	if (!catalog)
	{
		SG_PhaseCatalogExpectedDestroy(&expected);
		return 0;
	}
	catalog->magic = SG_PHASE_CATALOG_MAGIC;
	catalog->magic_inverse = ~SG_PHASE_CATALOG_MAGIC;
	catalog->self = catalog;
	catalog->identity = source->authority->identity;
	catalog->completion = expected.completion;
	catalog->transition_completion = expected.transition_completion;
	catalog->mover_support_verifier_identity =
		expected.mover_support_verifier_identity;
	catalog->phases = expected.phases;
	catalog->phase_count = expected.phase_count;
	catalog->phase_capacity = expected.phase_count;
	catalog->bindings = expected.bindings;
	catalog->binding_count = expected.binding_count;
	catalog->binding_capacity = expected.binding_count;
	catalog->transitions = expected.transitions;
	catalog->transition_evidence = expected.transition_evidence;
	catalog->transition_count = expected.transition_count;
	catalog->transition_capacity = expected.transition_count;
	free(expected.transition_pairs);
	free(expected.transition_pair_hash);
	free(expected.phase_hash);
	free(expected.phase_neutral_hash);
	free(expected.phase_region_by_phase);
	*catalog_out = catalog;
	return 1;
}

static int FixtureInit(phase_fixture_t *fixture, uint32_t cell_count,
	uint32_t region_count, int split_support)
{
	sg_rune_model_identity_t identity = Identity();
	uint32_t index;

	memset(fixture, 0, sizeof(*fixture));
	fixture->cells = calloc(cell_count == 0U ? 1U : (size_t)cell_count,
		sizeof(*fixture->cells));
	fixture->regions = calloc(region_count == 0U ? 1U : (size_t)region_count,
		sizeof(*fixture->regions));
	if (!fixture->cells || !fixture->regions)
	{
		free(fixture->cells);
		free(fixture->regions);
		memset(fixture, 0, sizeof(*fixture));
		return 0;
	}
	fixture->authority.identity = identity;
	fixture->configuration.identity = identity;
	fixture->configuration.cells = fixture->cells;
	fixture->configuration.cell_count = cell_count;
	fixture->semantics.identity = identity;
	fixture->semantics.regions = fixture->regions;
	fixture->semantics.region_count = region_count;
	fixture->semantics.faces = NULL;
	fixture->semantics.face_count = 0U;
	for (index = 0U; index < cell_count; index++)
		SetCell(&fixture->cells[index], &identity, index,
			index == 1U ? SG_RUNE_STANCE_CROUCHING : SG_RUNE_STANCE_STANDING);
	if (!split_support)
	{
		if (region_count >= 1U)
			SetRegion(&fixture->regions[0], 0U, 0U,
				SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED);
		if (region_count >= 2U)
			SetRegion(&fixture->regions[1], (UINT64_C(1) << 32) | 1U, 1U,
				SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED);
		for (index = 2U; index < region_count; index++)
			SetRegion(&fixture->regions[index], (UINT64_C(1) << 32) | index, 1U,
				SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE);
	}
	else
	{
		SetRegion(&fixture->regions[0], 0U, 0U,
			SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE);
		SetRegion(&fixture->regions[1], 1U, 0U,
			SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED);
	}
	fixture->derivation.authority = &fixture->authority;
	fixture->derivation.configuration = &fixture->configuration;
	fixture->derivation.semantics = &fixture->semantics;
	fixture->derivation.completion = SG_PHASE_CATALOG_PROVEN_EMPTY;
	fixture->derivation.verifier_identity = UINT64_C(0x4e4f4e4155544801);
	return 1;
}

static void FixtureDestroy(phase_fixture_t *fixture)
{
	if (!fixture)
		return;
	free(fixture->cells);
	free(fixture->regions);
	memset(fixture, 0, sizeof(*fixture));
}

typedef struct real_producer_fixture_s
{
	sg_bsp_world_t world;
	sg_bsp_plane_t planes[7];
	sg_bsp_node_t node;
	sg_bsp_leaf_t leaves[3];
	uint32_t leaf_brush;
	sg_bsp_model_t models[2];
	sg_bsp_brush_t brush;
	sg_bsp_brush_side_t brush_sides[6];
	sg_host_collision_authority_t authority;
	sg_configuration_space_t *configuration;
	sg_configuration_semantics_t *semantics;
} real_producer_fixture_t;

static void SetBspPlane(sg_bsp_plane_t *plane, float x, float y, float z,
	float distance)
{
	Set3(plane->normal.value, x, y, z);
	plane->distance = distance;
	plane->type = x == 1.0f ? 0 : (y == 1.0f ? 1 : (z == 1.0f ? 2 : 3));
}

static void RealProducerWorldInit(real_producer_fixture_t *fixture)
{
	uint32_t side;

	memset(fixture, 0, sizeof(*fixture));
	SetBspPlane(&fixture->planes[0], 1.0f, 0.0f, 0.0f, 0.0f);
	SetBspPlane(&fixture->planes[1], 1.0f, 0.0f, 0.0f, 2.0f);
	SetBspPlane(&fixture->planes[2], -1.0f, 0.0f, 0.0f, 2.0f);
	SetBspPlane(&fixture->planes[3], 0.0f, 1.0f, 0.0f, 64.0f);
	SetBspPlane(&fixture->planes[4], 0.0f, -1.0f, 0.0f, 64.0f);
	SetBspPlane(&fixture->planes[5], 0.0f, 0.0f, 1.0f, 64.0f);
	SetBspPlane(&fixture->planes[6], 0.0f, 0.0f, -1.0f, 64.0f);
	fixture->node.plane = 0U;
	fixture->node.children[0] = -1;
	fixture->node.children[1] = -2;
	fixture->leaves[0].cluster = 0;
	fixture->leaves[0].area = 1U;
	fixture->leaves[1].cluster = 1;
	fixture->leaves[1].area = 2U;
	fixture->leaves[2].contents = SG_HOST_CONTENTS_SOLID;
	fixture->leaves[2].cluster = -1;
	fixture->leaves[2].first_leaf_brush = 0U;
	fixture->leaves[2].leaf_brush_count = 1U;
	fixture->leaf_brush = 0U;
	fixture->brush.first_side = 0U;
	fixture->brush.side_count = 6U;
	fixture->brush.contents = SG_HOST_CONTENTS_SOLID;
	for (side = 0U; side < 6U; side++)
	{
		fixture->brush_sides[side].plane = side + 1U;
		fixture->brush_sides[side].texinfo = -1;
	}
	fixture->models[0].headnode = 0;
	Set3(fixture->models[0].mins.value, -4096.0f, -4096.0f, -4096.0f);
	Set3(fixture->models[0].maxs.value, 4095.875f, 4095.875f, 4095.875f);
	fixture->models[1].headnode = -3;
	Set3(fixture->models[1].mins.value, -2.0f, -64.0f, -64.0f);
	Set3(fixture->models[1].maxs.value, 2.0f, 64.0f, 64.0f);
	fixture->world.planes = fixture->planes;
	fixture->world.plane_count = 7U;
	fixture->world.nodes = &fixture->node;
	fixture->world.node_count = 1U;
	fixture->world.leaves = fixture->leaves;
	fixture->world.leaf_count = 3U;
	fixture->world.leaf_brushes = &fixture->leaf_brush;
	fixture->world.leaf_brush_count = 1U;
	fixture->world.models = fixture->models;
	fixture->world.model_count = 2U;
	fixture->world.brushes = &fixture->brush;
	fixture->world.brush_count = 1U;
	fixture->world.brush_sides = fixture->brush_sides;
	fixture->world.brush_side_count = 6U;
}

static void RealProducerFixtureDestroy(real_producer_fixture_t *fixture)
{
	SG_ConfigurationSemanticsDestroy(fixture->semantics);
	SG_ConfigurationDestroy(fixture->configuration);
	fixture->semantics = NULL;
	fixture->configuration = NULL;
}

static void TestRealConfigurationSemanticsProducer(void)
{
	real_producer_fixture_t fixture;
	sg_rune_model_identity_t identity = Identity();
	sg_host_collision_error_t host_error;
	sg_configuration_error_t configuration_error;
	sg_configuration_semantics_error_t semantics_error;
	sg_configuration_semantics_limits_t semantics_limits;
	sg_phase_catalog_non_authoritative_source_t source;
	sg_phase_catalog_t *catalog = NULL;
	sg_phase_catalog_error_t catalog_error = { 0 };
	const sg_phase_catalog_binding_t *bindings = NULL;
	uint32_t binding_count = 0U;

	RealProducerWorldInit(&fixture);
	CHECK_PHASE(SG_HostCollisionInit(&fixture.authority, &fixture.world,
		&identity, &host_error));
	CHECK_PHASE(SG_ConfigurationBuild(&fixture.authority, NULL,
		&fixture.configuration, &configuration_error));
	if (!fixture.configuration)
	{
		RealProducerFixtureDestroy(&fixture);
		return;
	}
	SG_ConfigurationSemanticsDefaultLimits(&semantics_limits);
	CHECK_PHASE(SG_ConfigurationSemanticsBuild(&fixture.authority,
		fixture.configuration, &semantics_limits, &fixture.semantics,
		&semantics_error));
	if (!fixture.semantics)
	{
		RealProducerFixtureDestroy(&fixture);
		return;
	}
	CHECK_PHASE(fixture.semantics->region_count != 0U);
	CHECK_PHASE(fixture.semantics->regions != NULL);
	if (!fixture.semantics->regions)
	{
		RealProducerFixtureDestroy(&fixture);
		return;
	}
	CHECK_PHASE(fixture.semantics->regions[0].id == 0U);
	memset(&source, 0, sizeof(source));
	source.authority = &fixture.authority;
	source.configuration = fixture.configuration;
	source.semantics = fixture.semantics;
	source.completion = SG_PHASE_CATALOG_PROVEN_EMPTY;
	source.verifier_identity = UINT64_C(0x4e4f4e4155544802);
	CHECK_PHASE(DeriveCatalogNonAuthoritative(&source, &catalog,
		&catalog_error));
	CHECK_PHASE(catalog != NULL);
	if (catalog)
	{
		CHECK_PHASE(SG_PhaseCatalogBindingsForRegion(catalog, 0U, &bindings,
			&binding_count));
		CHECK_PHASE(binding_count != 0U && bindings != NULL);
		SG_PhaseCatalogDestroy(catalog);
	}
	RealProducerFixtureDestroy(&fixture);
}

static void TestRegionZeroAndImmutableEmpty(void)
{
	phase_fixture_t fixture;
	sg_phase_catalog_t *catalog = NULL;
	sg_phase_catalog_error_t error = { 0 };
	const sg_phase_catalog_binding_t *bindings = NULL;
	uint32_t binding_count = 0U;

	if (!FixtureInit(&fixture, 2U, 2U, 0))
	{
		CHECK_PHASE(0);
		return;
	}
	CHECK_PHASE(fixture.regions[0].id == 0U &&
		fixture.regions[1].id == ((UINT64_C(1) << 32) | UINT64_C(1)));
	CHECK_PHASE(DeriveCatalogNonAuthoritative(&fixture.derivation, &catalog,
		&error));
	CHECK_PHASE(catalog != NULL);
	if (!catalog)
	{
		FixtureDestroy(&fixture);
		return;
	}
	CHECK_PHASE(catalog->completion == SG_PHASE_CATALOG_COMPLETE);
	CHECK_PHASE(catalog->phase_count == 2U);
	CHECK_PHASE(catalog->binding_count == 2U);
	CHECK_PHASE(catalog->transition_completion ==
		SG_PHASE_CATALOG_PROVEN_EMPTY);
	CHECK_PHASE(catalog->transition_count == 0U);
	CHECK_PHASE(SG_PhaseCatalogBindingsForRegion(catalog, 0U, &bindings,
		&binding_count));
	CHECK_PHASE(binding_count == 1U && bindings != NULL &&
		bindings[0].semantic_region_id == 0U);
	SG_PhaseCatalogDestroy(catalog);
	FixtureDestroy(&fixture);
}

static void TestRejectNonCanonicalRegionIds(void)
{
	phase_fixture_t fixture;
	sg_phase_catalog_t *catalog = NULL;
	sg_phase_catalog_error_t error = { 0 };

	if (!FixtureInit(&fixture, 2U, 2U, 0))
	{
		CHECK_PHASE(0);
		return;
	}
	/* These values are increasing and unique, but are not the producer's
	 * ((uint64_t)cell_index << 32) | region_index derivation. */
	fixture.regions[0].id = UINT64_C(100);
	fixture.regions[1].id = UINT64_C(101);
	CHECK_PHASE(!DeriveCatalogNonAuthoritative(&fixture.derivation, &catalog,
		&error));
	CHECK_PHASE(catalog == NULL);
	CHECK_PHASE(error.code == SG_PHASE_CATALOG_ERROR_INVALID_SOURCE);
	FixtureDestroy(&fixture);
}

static void TestSupportTransitionEvidence(void)
{
	phase_fixture_t fixture;
	sg_phase_catalog_t *catalog = NULL;
	sg_phase_catalog_error_t error = { 0 };

	CHECK_PHASE(FixtureInit(&fixture, 1U, 2U, 1));
	CHECK_PHASE(DeriveCatalogNonAuthoritative(&fixture.derivation, &catalog,
		&error));
	CHECK_PHASE(catalog != NULL);
	if (!catalog)
	{
		FixtureDestroy(&fixture);
		return;
	}
	CHECK_PHASE(catalog->phase_count == 2U);
	CHECK_PHASE(catalog->transition_count == 1U);
	CHECK_PHASE(catalog->transition_completion == SG_PHASE_CATALOG_COMPLETE);
	CHECK_PHASE(catalog->transition_evidence[0].origin ==
		SG_PHASE_CATALOG_TRANSITION_SUPPORT_CHANGE);
	CHECK_PHASE(catalog->transitions[0].kind == SG_RUNE_PHASE_TRANSITION_SUPPORT);
	catalog->transition_evidence[0].source_state_mask =
		SG_PHASE_MECHANISM_STATE_RETURNING;
	CHECK_PHASE(catalog->transition_evidence[0].source_state_mask ==
		SG_PHASE_MECHANISM_STATE_RETURNING);
	SG_PhaseCatalogDestroy(catalog);
	FixtureDestroy(&fixture);
}

static void TestStanceAndPortalTransitions(void)
{
	phase_fixture_t fixture;
	sg_configuration_stance_overlap_t overlap;
	sg_configuration_portal_t portal;
	sg_phase_catalog_t *catalog = NULL;
	sg_phase_catalog_error_t error = { 0 };
	uint32_t index;
	uint32_t stance_transitions = 0U;
	uint32_t portal_transitions = 0U;

	CHECK_PHASE(FixtureInit(&fixture, 2U, 2U, 0));
	memset(&overlap, 0, sizeof(overlap));
	overlap.standing_cell = 0U;
	overlap.crouching_cell = 1U;
	Set3(overlap.bounds.mins.value, -8.0f, -8.0f, -8.0f);
	Set3(overlap.bounds.maxs.value, 8.0f, 8.0f, 8.0f);
	Set3(overlap.interior_witness.value, 0.0f, 0.0f, 0.0f);
	fixture.configuration.stance_overlaps = &overlap;
	fixture.configuration.stance_overlap_count = 1U;
	CHECK_PHASE(DeriveCatalogNonAuthoritative(&fixture.derivation, &catalog,
		&error));
	CHECK_PHASE(catalog != NULL);
	if (catalog)
	{
		for (index = 0U; index < catalog->transition_count; index++)
			if (catalog->transition_evidence[index].origin ==
				SG_PHASE_CATALOG_TRANSITION_STANCE_OVERLAP)
				stance_transitions++;
		CHECK_PHASE(stance_transitions == 2U);
		SG_PhaseCatalogDestroy(catalog);
		catalog = NULL;
	}
	FixtureDestroy(&fixture);

	if (!FixtureInit(&fixture, 2U, 2U, 0))
	{
		CHECK_PHASE(0);
		return;
	}
	fixture.cells[1].stance = SG_RUNE_STANCE_STANDING;
	memset(&portal, 0, sizeof(portal));
	portal.order.source_set_identity = fixture.authority.identity.source_set_identity;
	portal.order.domain = SG_RUNE_ORDER_PORTAL;
	portal.order.source_index = 0U;
	portal.order.local_ordinal = 1U;
	portal.order.variant = 0U;
	portal.id.value = SG_RuneModelStableIdFromOrderKey(&portal.order);
	portal.from_cell = 0U;
	portal.to_cell = 1U;
	portal.stance = SG_RUNE_STANCE_STANDING;
	portal.plane.normal[0] = 1.0f;
	portal.clearance = 1.0f;
	fixture.configuration.portals = &portal;
	fixture.configuration.portal_count = 1U;
	CHECK_PHASE(DeriveCatalogNonAuthoritative(&fixture.derivation, &catalog,
		&error));
	CHECK_PHASE(catalog != NULL);
	if (catalog)
	{
		for (index = 0U; index < catalog->transition_count; index++)
			if (catalog->transition_evidence[index].origin ==
				SG_PHASE_CATALOG_TRANSITION_PORTAL)
				portal_transitions++;
		CHECK_PHASE(portal_transitions == 2U);
		SG_PhaseCatalogDestroy(catalog);
	}
	FixtureDestroy(&fixture);
}

static void TestCallerCannotIssueMechanismProvider(void)
{
	phase_fixture_t fixture;
	sg_mechanism_capability_owner_t *capability_owner = NULL;
	sg_phase_mover_support_provider_owner_t *provider_owner = NULL;
	const sg_mechanism_capability_set_t *forged =
		(const sg_mechanism_capability_set_t *)(uintptr_t)UINT32_C(1);
	sg_phase_mover_support_provider_t *provider = NULL;
	sg_phase_catalog_error_t error = { 0 };

	CHECK_PHASE(FixtureInit(&fixture, 1U, 1U, 0));
	CHECK_PHASE(SG_MechanismCapabilityOwnerCreate(&capability_owner));
	CHECK_PHASE(SG_PhaseMoverSupportProviderOwnerCreate(&provider_owner));
	CHECK_PHASE(!SG_PhaseMoverSupportProviderBuild(provider_owner,
		capability_owner, &fixture.semantics, forged, &provider, &error));
	CHECK_PHASE(provider == NULL);
	CHECK_PHASE(error.code == SG_PHASE_CATALOG_ERROR_INVALID_SOURCE);
	SG_PhaseMoverSupportProviderOwnerDestroy(provider_owner);
	SG_MechanismCapabilityOwnerDestroy(capability_owner);
	FixtureDestroy(&fixture);
}

static void TestLargePreDedupSource(void)
{
	phase_fixture_t fixture;
	sg_phase_catalog_t *catalog = NULL;
	sg_phase_catalog_error_t error = { 0 };
	uint32_t index;
	uint32_t count = SG_RUNE_MODEL_MAX_PHASES + 1U;

	CHECK_PHASE(FixtureInit(&fixture, 1U, count, 0));
	if (!fixture.regions)
		return;
	for (index = 0U; index < count; index++)
		SetRegion(&fixture.regions[index], index, 0U,
			SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED);
	CHECK_PHASE(DeriveCatalogNonAuthoritative(&fixture.derivation, &catalog,
		&error));
	CHECK_PHASE(catalog != NULL);
	if (catalog)
	{
		CHECK_PHASE(catalog->phase_count == 1U);
		CHECK_PHASE(catalog->binding_count == count);
	}
	SG_PhaseCatalogDestroy(catalog);
	FixtureDestroy(&fixture);
}

#ifdef SG_PHASE_CATALOG_TEST_TRANSITION_LIMIT
static void TestTransitionAppendBoundBeforeGrowth(void)
{
	phase_fixture_t fixture;
	sg_configuration_stance_overlap_t overlap;
	sg_configuration_portal_t portals[2];
	sg_phase_catalog_t *catalog = NULL;
	sg_phase_catalog_error_t error = { 0 };
	uint32_t index;

	CHECK_PHASE(SG_PHASE_CATALOG_TEST_TRANSITION_LIMIT == UINT32_C(4));
	CHECK_PHASE(FixtureInit(&fixture, 4U, 4U, 0));
	SetRegion(&fixture.regions[2], (UINT64_C(2) << 32) | UINT64_C(2), 2U,
		SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED);
	SetRegion(&fixture.regions[3], (UINT64_C(3) << 32) | UINT64_C(3), 3U,
		SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED);
	memset(&overlap, 0, sizeof(overlap));
	overlap.standing_cell = 0U;
	overlap.crouching_cell = 1U;
	Set3(overlap.bounds.mins.value, -8.0f, -8.0f, -8.0f);
	Set3(overlap.bounds.maxs.value, 8.0f, 8.0f, 8.0f);
	fixture.configuration.stance_overlaps = &overlap;
	fixture.configuration.stance_overlap_count = 1U;
	memset(portals, 0, sizeof(portals));
	for (index = 0U; index < 2U; index++)
	{
		portals[index].order.source_set_identity =
			fixture.authority.identity.source_set_identity;
		portals[index].order.domain = SG_RUNE_ORDER_PORTAL;
		portals[index].order.source_index = index;
		portals[index].id.value = SG_RuneModelStableIdFromOrderKey(
			&portals[index].order);
		portals[index].from_cell = 0U;
		portals[index].to_cell = 2U;
		portals[index].stance = SG_RUNE_STANCE_STANDING;
		portals[index].plane.normal[0] = 1.0f;
		portals[index].clearance = 1.0f;
	}
	fixture.configuration.portals = portals;
	fixture.configuration.portal_count = 2U;
	CHECK_PHASE(DeriveCatalogNonAuthoritative(&fixture.derivation, &catalog,
		&error));
	CHECK_PHASE(catalog != NULL && catalog->transition_count ==
		SG_PHASE_CATALOG_TEST_TRANSITION_LIMIT);
	SG_PhaseCatalogDestroy(catalog);
	catalog = NULL;
	portals[1].to_cell = 3U;
	CHECK_PHASE(!DeriveCatalogNonAuthoritative(&fixture.derivation, &catalog,
		&error));
	CHECK_PHASE(catalog == NULL);
	CHECK_PHASE(error.code == SG_PHASE_CATALOG_ERROR_OVERFLOW);
	CHECK_PHASE(error.source_index == SG_PHASE_CATALOG_TEST_TRANSITION_LIMIT);
	FixtureDestroy(&fixture);
}
#endif

int main(void)
{
#ifdef SG_PHASE_CATALOG_TEST_TRANSITION_LIMIT
	TestTransitionAppendBoundBeforeGrowth();
	(void)TestRealConfigurationSemanticsProducer;
	(void)TestRegionZeroAndImmutableEmpty;
	(void)TestRejectNonCanonicalRegionIds;
	(void)TestSupportTransitionEvidence;
	(void)TestStanceAndPortalTransitions;
	(void)TestCallerCannotIssueMechanismProvider;
	(void)TestLargePreDedupSource;
#else
	TestRealConfigurationSemanticsProducer();
	TestRegionZeroAndImmutableEmpty();
	TestRejectNonCanonicalRegionIds();
	TestSupportTransitionEvidence();
	TestStanceAndPortalTransitions();
	TestCallerCannotIssueMechanismProvider();
	TestLargePreDedupSource();
#endif
	if (phase_failures)
	{
		fprintf(stderr, "%d phase catalog checks failed\n", phase_failures);
		return 1;
	}
	puts("phase catalog checks passed");
	return 0;
}
