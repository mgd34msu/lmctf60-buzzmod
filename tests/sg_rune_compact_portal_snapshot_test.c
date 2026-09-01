#include "../slipgate/sg_rune_compact_portal_snapshot.h"
#include "../slipgate/sg_rune_compact_mechanisms.h"
#include "../slipgate/sg_rune_mechanism_catalog.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

struct sg_rune_compact_field_service_s
{
	const sg_rune_compact_model_t *model;
	uint64_t identity;
	uint64_t generation;
	sg_rune_compact_field_portal_root_t roots[2];
};

typedef struct fixture_s
{
	sg_rune_compact_mechanism_t mechanisms[2];
	sg_rune_compact_static_transition_t transitions[2];
	sg_rune_compact_mechanism_authority_t authorities[2];
	sg_rune_compact_static_t static_data;
	sg_rune_compact_model_t model;
	sg_bsp_entity_semantic_t entities[2];
	sg_bsp_entity_semantics_t semantics;
	struct sg_rune_compact_field_service_s service;
} fixture_t;

static int failures;
static int resolution_present[2];
static sg_mech_catalog_source_resolution_t resolutions[2];
static uint32_t resolved_ordinals[8];
static uint32_t resolved_ordinal_count;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

const sg_rune_compact_model_t *SG_RuneCompactFieldServiceModel(
	const sg_rune_compact_field_service_t *service)
{
	return service != NULL ? service->model : NULL;
}

uint64_t SG_RuneCompactFieldServiceIdentity(
	const sg_rune_compact_field_service_t *service)
{
	return service != NULL ? service->identity : 0U;
}

uint64_t SG_RuneCompactFieldServiceGeneration(
	const sg_rune_compact_field_service_t *service)
{
	return service != NULL ? service->generation : 0U;
}

uint32_t SG_RuneCompactFieldServicePortalRootCount(
	const sg_rune_compact_field_service_t *service)
{
	return service != NULL ? 2U : 0U;
}

int SG_RuneCompactFieldServicePortalRootAt(
	const sg_rune_compact_field_service_t *service, uint32_t root_index,
	sg_rune_compact_portal_index_t *portal_out,
	sg_rune_compact_mechanism_index_t *mechanism_out)
{
	if (service == NULL || root_index >= 2U || portal_out == NULL ||
		mechanism_out == NULL)
		return 0;
	*portal_out = service->roots[root_index].portal;
	*mechanism_out = service->roots[root_index].mechanism;
	return 1;
}

int SG_MechCatalogResolveSourceOrdinal(uint32_t source_ordinal,
	sg_mech_catalog_source_resolution_t *resolution_out)
{
	uint32_t index;

	if (resolution_out != NULL)
		memset(resolution_out, 0, sizeof(*resolution_out));
	if (resolved_ordinal_count < 8U)
		resolved_ordinals[resolved_ordinal_count++] = source_ordinal;
	if (resolution_out == NULL)
		return 0;
	for (index = 0U; index < 2U; index++)
		if (resolutions[index].source_ordinal == source_ordinal) {
			if (!resolution_present[index])
				return 0;
			*resolution_out = resolutions[index];
			return 1;
		}
	return 0;
}

static void SetResolution(uint32_t index, uint32_t source_ordinal,
	sg_mech_motion_state_t motion, float phase)
{
	memset(&resolutions[index], 0, sizeof(resolutions[index]));
	resolutions[index].source_ordinal = source_ordinal;
	resolutions[index].key = index + 1U;
	resolutions[index].generation = 1U;
	resolutions[index].motion_state = motion;
	resolutions[index].phase = phase;
	resolutions[index].phase_known = 1U;
	resolution_present[index] = 1;
}

static void InitFixture(fixture_t *fixture)
{
	uint32_t index;

	memset(fixture, 0, sizeof(*fixture));
	memset(resolution_present, 0, sizeof(resolution_present));
	memset(resolutions, 0, sizeof(resolutions));
	memset(resolved_ordinals, 0, sizeof(resolved_ordinals));
	resolved_ordinal_count = 0U;
	fixture->model.identity.bsp_bytes = 1U;
	fixture->model.static_data = &fixture->static_data;
	fixture->model.mechanism_authorities = fixture->authorities;
	fixture->model.mechanism_authority_count = 2U;
	fixture->static_data.mechanisms = fixture->mechanisms;
	fixture->static_data.mechanism_count = 2U;
	fixture->static_data.transitions = fixture->transitions;
	fixture->static_data.transition_count = 2U;
	for (index = 0U; index < 2U; index++) {
		fixture->mechanisms[index].transitions.first = index;
		fixture->mechanisms[index].transitions.count = 1U;
		fixture->transitions[index].mechanism.value = index;
		fixture->transitions[index].kind =
			SG_RUNE_COMPACT_STATIC_TRANSITION_PORTAL_STATE;
		fixture->transitions[index].value.portal_state.portal.value = 0U;
		fixture->transitions[index].value.portal_state.source_blocked = 1U;
		fixture->transitions[index].value.portal_state.destination_blocked = 0U;
		fixture->entities[index].source_set_identity = UINT64_C(91);
		fixture->entities[index].canonical_ordinal = index;
		fixture->authorities[index].source.entity_ordinal = index;
		fixture->service.roots[index].portal.value = 0U;
		fixture->service.roots[index].mechanism.value = index;
	}
	/* Compact mechanisms use canonical semantic ordinals.  The live catalog
	 * uses the original declaration ordinals, which are deliberately neither
	 * canonical nor edict-like here. */
	fixture->mechanisms[0].source.entity_ordinal = 1U;
	fixture->mechanisms[1].source.entity_ordinal = 0U;
	fixture->entities[0].source_entity_ordinal = 41U;
	fixture->entities[1].source_entity_ordinal = 77U;
	fixture->semantics.source_set_identity = UINT64_C(91);
	fixture->semantics.world.source_set_identity = UINT64_C(91);
	fixture->semantics.entities = fixture->entities;
	fixture->semantics.entity_count = 2U;
	fixture->service.model = &fixture->model;
	fixture->service.identity = UINT64_C(7);
	fixture->service.generation = UINT64_C(9);
}

static void TestProvenanceAndEndpointStates(void)
{
	fixture_t fixture;
	sg_rune_compact_portal_snapshot_t *snapshot = NULL;
	sg_rune_compact_portal_snapshot_frame_t frame;

	InitFixture(&fixture);
	SetResolution(0U, 41U, SG_MECH_MOTION_AT_DESTINATION, 1.0f);
	SetResolution(1U, 77U, SG_MECH_MOTION_AT_ORIGIN, 0.0f);
	CHECK(SG_RuneCompactPortalSnapshotCreate(&fixture.model, &fixture.service,
		&fixture.semantics, &snapshot));
	CHECK(SG_RuneCompactPortalSnapshotPublish(snapshot, UINT64_C(12), &frame));
	CHECK(resolved_ordinal_count == 4U);
	CHECK(resolved_ordinals[0] == 41U && resolved_ordinals[1] == 77U &&
		resolved_ordinals[2] == 77U && resolved_ordinals[3] == 41U);
	CHECK(frame.mechanisms != NULL && frame.portal_roots != NULL);
	CHECK(frame.mechanisms->frame_sequence == UINT64_C(12) &&
		frame.portal_roots->frame_sequence == UINT64_C(12));
	CHECK(frame.mechanisms->phase_count == 2U);
	CHECK(frame.mechanisms->phases[0].mechanism.value == 0U &&
		frame.mechanisms->phases[0].phase == 1.0f);
	CHECK(frame.mechanisms->phases[1].mechanism.value == 1U &&
		frame.mechanisms->phases[1].phase == 0.0f);
	CHECK(frame.portal_roots->roots[0].state ==
		SG_RUNE_COMPACT_FIELD_PORTAL_ROOT_BLOCKED);
	CHECK(frame.portal_roots->roots[1].state ==
		SG_RUNE_COMPACT_FIELD_PORTAL_ROOT_UNBLOCKED);
	SG_RuneCompactPortalSnapshotDestroy(snapshot);
}

static void TestUnresolvedAndMoving(void)
{
	fixture_t fixture;
	sg_rune_compact_portal_snapshot_t *snapshot = NULL;
	sg_rune_compact_portal_snapshot_frame_t frame;

	InitFixture(&fixture);
	fixture.mechanisms[0].kind = SG_RUNE_COMPACT_MECHANISM_LIFT;
	fixture.mechanisms[1].kind = SG_RUNE_COMPACT_MECHANISM_TRAIN;
	SetResolution(0U, 41U, SG_MECH_MOTION_TO_ORIGIN, 0.75f);
	SetResolution(1U, 77U, SG_MECH_MOTION_TO_DESTINATION, 0.25f);
	CHECK(SG_RuneCompactPortalSnapshotCreate(&fixture.model, &fixture.service,
		&fixture.semantics, &snapshot));
	CHECK(SG_RuneCompactPortalSnapshotPublish(snapshot, UINT64_C(20), &frame));
	CHECK(frame.mechanisms->phase_count == 2U);
	CHECK(frame.mechanisms->phases[0].phase == 0.75f &&
		frame.mechanisms->phases[1].phase == 0.25f);
	CHECK(frame.portal_roots->roots[0].state ==
		SG_RUNE_COMPACT_FIELD_PORTAL_ROOT_UNKNOWN &&
		frame.portal_roots->roots[1].state ==
		SG_RUNE_COMPACT_FIELD_PORTAL_ROOT_UNKNOWN);
	resolution_present[1] = 0;
	CHECK(SG_RuneCompactPortalSnapshotPublish(snapshot, UINT64_C(21), &frame));
	CHECK(frame.mechanisms->phase_count == 1U);
	CHECK(frame.mechanisms->phases[0].mechanism.value == 0U);
	CHECK(frame.portal_roots->roots[0].state ==
		SG_RUNE_COMPACT_FIELD_PORTAL_ROOT_UNKNOWN);
	resolutions[0].phase_known = 0U;
	CHECK(SG_RuneCompactPortalSnapshotPublish(snapshot, UINT64_C(22), &frame));
	CHECK(frame.mechanisms->phase_count == 0U);
	CHECK(frame.portal_roots->roots[0].state ==
		SG_RUNE_COMPACT_FIELD_PORTAL_ROOT_UNKNOWN &&
		frame.portal_roots->roots[1].state ==
		SG_RUNE_COMPACT_FIELD_PORTAL_ROOT_UNKNOWN);
	SG_RuneCompactPortalSnapshotDestroy(snapshot);
}

static void TestDriftAndMalformedSemantics(void)
{
	fixture_t fixture;
	sg_rune_compact_portal_snapshot_t *snapshot = NULL;
	sg_rune_compact_portal_snapshot_frame_t frame;

	InitFixture(&fixture);
	CHECK(SG_RuneCompactPortalSnapshotCreate(&fixture.model, &fixture.service,
		&fixture.semantics, &snapshot));
	fixture.service.generation++;
	frame.mechanisms = (const void *)(uintptr_t)1U;
	frame.portal_roots = (const void *)(uintptr_t)1U;
	CHECK(!SG_RuneCompactPortalSnapshotPublish(snapshot, UINT64_C(30), &frame));
	CHECK(frame.mechanisms == NULL && frame.portal_roots == NULL);
	fixture.service.generation--;
	fixture.service.roots[1].mechanism.value = 0U;
	CHECK(!SG_RuneCompactPortalSnapshotPublish(snapshot, UINT64_C(31), &frame));
	SG_RuneCompactPortalSnapshotDestroy(snapshot);
	snapshot = NULL;

	InitFixture(&fixture);
	fixture.entities[1].canonical_ordinal = 2U;
	CHECK(!SG_RuneCompactPortalSnapshotCreate(&fixture.model, &fixture.service,
		&fixture.semantics, &snapshot));
	CHECK(snapshot == NULL);
}

int main(void)
{
	TestProvenanceAndEndpointStates();
	TestUnresolvedAndMoving();
	TestDriftAndMalformedSemantics();
	if (failures != 0) {
		fprintf(stderr, "%d compact portal snapshot tests failed\n", failures);
		return 1;
	}
	puts("sg_rune_compact_portal_snapshot_test: PASS");
	return 0;
}
