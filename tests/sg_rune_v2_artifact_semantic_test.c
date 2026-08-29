#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "slipgate/sg_rune_v2_artifact_semantic.h"
#include "tests/support/sg_rune_v2_fixture.h"

static int failures;

struct sg_rune_v2_semantic_catalog_s
{
	const sg_rune_v2_semantic_catalog_view_t *view;
};

struct sg_rune_v2_complete_model_proof_output_s
{
	const sg_rune_v2_semantic_catalog_t *catalog;
};

static const sg_rune_v2_complete_model_proof_output_t
	*issued_complete_model_proof_output;
static const sg_rune_v2_semantic_catalog_t *issued_complete_model_catalog;
static const void *issued_complete_model_catalog_storage;
static size_t issued_complete_model_catalog_storage_size;
static size_t provider_storage_overlap_calls;

static int TestRangesOverlap(const void *left, size_t left_size,
	const void *right, size_t right_size)
{
	uintptr_t left_begin;
	uintptr_t right_begin;

	if (!left || !right || left_size == 0U || right_size == 0U)
		return 0;
	left_begin = (uintptr_t)left;
	right_begin = (uintptr_t)right;
	if ((sizeof(size_t) > sizeof(uintptr_t) &&
		left_size > (size_t)UINTPTR_MAX) ||
		(sizeof(size_t) > sizeof(uintptr_t) &&
		right_size > (size_t)UINTPTR_MAX))
		return 1;
	if (left_size > (size_t)(UINTPTR_MAX - left_begin) ||
		right_size > (size_t)(UINTPTR_MAX - right_begin))
		return 1;
	return left_begin < right_begin + (uintptr_t)right_size &&
		right_begin < left_begin + (uintptr_t)left_size;
}

/* Test-only stand-in for the audited complete-model proof provider. Inputs to
 * CompleteModelCatalogInit represent already established independent facts.
 * The registry refuses candidate copies and other caller-created storage. */
const sg_rune_v2_semantic_catalog_t *SG_RuneV2CompleteModelProofSemanticCatalog(
	const sg_rune_v2_complete_model_proof_output_t *complete_model_proof_output)
{
	if (!complete_model_proof_output ||
		complete_model_proof_output != issued_complete_model_proof_output ||
		complete_model_proof_output->catalog != issued_complete_model_catalog)
		return NULL;
	return complete_model_proof_output->catalog;
}

int SG_RuneV2CompleteModelProofSemanticCatalogRead(
	const sg_rune_v2_semantic_catalog_t *catalog,
	const sg_rune_v2_semantic_catalog_view_t **view_out)
{
	if (!catalog || !view_out || catalog != issued_complete_model_catalog ||
		!catalog->view)
		return 0;
	*view_out = catalog->view;
	return 1;
}

int SG_RuneV2CompleteModelProofSemanticCatalogStorageOverlaps(
	const sg_rune_v2_semantic_catalog_t *catalog,
	const void *range, size_t range_size)
{
	provider_storage_overlap_calls++;
	return catalog && catalog == issued_complete_model_catalog &&
		TestRangesOverlap(range, range_size,
			issued_complete_model_catalog_storage,
			issued_complete_model_catalog_storage_size);
}

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

#define CHECK_DIAGNOSTIC(expected, expression) do { \
	sg_rune_v2_semantic_diagnostic_t actual_ = (expression); \
	if (actual_ != (expected)) { \
		fprintf(stderr, "%s:%d: expected %d, got %d: %s\n", \
			__FILE__, __LINE__, (int)(expected), (int)actual_, #expression); \
		failures++; \
	} \
} while (0)

typedef struct decoded_fixture_s
{
	sg_rune_plane_t planes[8];
	sg_rune_vec3_t portal_vertices[3];
	sg_rune_phase_basis_t phases[3];
	sg_rune_phase_transition_t phase_transitions[1];
	sg_rune_cell_t cells[2];
	sg_rune_portal_t portals[1];
	sg_rune_surface_t surfaces[1];
	sg_rune_affordance_t affordances[1];
	sg_rune_capability_kernel_t kernels[1];
	sg_rune_landmark_t landmarks[1];
	sg_rune_mechanism_t mechanisms[1];
	sg_rune_v2_codec_storage_t storage;
	sg_rune_v2_wire_binding_t binding;
	sg_rune_model_t model;
	sg_rune_validation_evidence_t evidence;
} decoded_fixture_t;

typedef struct complete_model_catalog_fixture_s
{
	sg_rune_v2_expected_plane_t planes[8];
	sg_rune_v2_expected_vertex_t portal_vertices[3];
	sg_rune_v2_expected_phase_t phases[3];
	sg_rune_v2_expected_cell_t cells[2];
	sg_rune_v2_expected_portal_t portals[1];
	sg_rune_v2_expected_transition_t phase_transitions[1];
	sg_rune_v2_expected_surface_t surfaces[1];
	sg_rune_v2_expected_affordance_t affordances[1];
	sg_rune_v2_expected_kernel_t kernels[1];
	sg_rune_v2_expected_landmark_t landmarks[1];
	sg_rune_v2_expected_mechanism_t mechanisms[1];
	sg_rune_v2_semantic_catalog_view_t view;
	sg_rune_v2_semantic_catalog_t catalog;
	sg_rune_v2_complete_model_proof_output_t complete_model_proof_output;
	sg_rune_v2_codec_storage_t provider_owned_storage;
	sg_rune_cell_t provider_owned_cell;
	sg_rune_landmark_t provider_owned_landmark;
	sg_rune_mechanism_t provider_owned_mechanism;
	sg_rune_v2_wire_binding_t provider_owned_binding;
	sg_rune_model_t provider_owned_model;
	sg_rune_validation_evidence_t provider_owned_evidence;
	sg_rune_v2_semantic_report_t provider_owned_report;
} complete_model_catalog_fixture_t;

static const sg_rune_v2_semantic_catalog_t *CompleteModelCatalog(
	const complete_model_catalog_fixture_t *fixture)
{
	return SG_RuneV2CompleteModelProofSemanticCatalog(
		&fixture->complete_model_proof_output);
}

static void DecodedInit(decoded_fixture_t *fixture)
{
	memset(fixture, 0, sizeof(*fixture));
	fixture->storage.planes = fixture->planes;
	fixture->storage.plane_capacity = 8U;
	fixture->storage.portal_vertices = fixture->portal_vertices;
	fixture->storage.portal_vertex_capacity = 3U;
	fixture->storage.phases = fixture->phases;
	fixture->storage.phase_capacity = 3U;
	fixture->storage.phase_transitions = fixture->phase_transitions;
	fixture->storage.phase_transition_capacity = 1U;
	fixture->storage.cells = fixture->cells;
	fixture->storage.cell_capacity = 2U;
	fixture->storage.portals = fixture->portals;
	fixture->storage.portal_capacity = 1U;
	fixture->storage.surfaces = fixture->surfaces;
	fixture->storage.surface_capacity = 1U;
	fixture->storage.affordances = fixture->affordances;
	fixture->storage.affordance_capacity = 1U;
	fixture->storage.kernels = fixture->kernels;
	fixture->storage.kernel_capacity = 1U;
	fixture->storage.landmarks = fixture->landmarks;
	fixture->storage.landmark_capacity = 1U;
	fixture->storage.mechanisms = fixture->mechanisms;
	fixture->storage.mechanism_capacity = 1U;
}

static void ExpectedCell(sg_rune_v2_expected_cell_t *expected,
	const sg_rune_cell_t *cell)
{
	memset(expected, 0, sizeof(*expected));
	expected->id = cell->id;
	expected->geometry = cell->geometry;
	expected->order = cell->order;
	expected->bounds = cell->bounds;
	expected->boundary_planes = cell->boundary_planes;
	expected->phases = cell->phases;
	expected->surfaces = cell->surfaces;
	expected->affordances = cell->affordances;
	expected->kernels = cell->kernels;
	expected->landmarks = cell->landmarks;
	expected->mechanisms = cell->mechanisms;
	expected->bsp_leaf = cell->bsp_leaf;
	expected->bsp_area = cell->bsp_area;
	expected->bsp_cluster = cell->bsp_cluster;
	expected->contents = cell->contents;
	expected->semantics = cell->semantics;
}

static void ExpectedPlane(sg_rune_v2_expected_plane_t *expected,
	const sg_rune_plane_t *plane)
{
	memset(expected, 0, sizeof(*expected));
	expected->id = plane->id;
	expected->normal = plane->normal;
	expected->order = plane->order;
	expected->distance = plane->distance;
}

static void ExpectedVertex(sg_rune_v2_expected_vertex_t *expected,
	const sg_rune_vec3_t *vertex)
{
	expected->x = vertex->value[0];
	expected->y = vertex->value[1];
	expected->z = vertex->value[2];
}

static void ExpectedPhase(sg_rune_v2_expected_phase_t *expected,
	const sg_rune_phase_basis_t *phase)
{
	memset(expected, 0, sizeof(*expected));
	expected->id = phase->id;
	expected->mover = phase->mover;
	expected->order = phase->order;
	expected->stance = phase->stance;
	expected->motion = phase->motion;
	expected->support = phase->support;
	expected->medium = phase->medium;
	expected->void_relation = phase->void_relation;
	expected->reference_frame = phase->reference_frame;
	expected->velocity = phase->velocity;
	expected->elapsed_ms = phase->elapsed_ms;
	expected->time_quantum_ms = phase->time_quantum_ms;
	expected->time_horizon_ms = phase->time_horizon_ms;
}

static void ExpectedPortal(sg_rune_v2_expected_portal_t *expected,
	const sg_rune_portal_t *portal)
{
	memset(expected, 0, sizeof(*expected));
	expected->id = portal->id;
	expected->geometry = portal->geometry;
	expected->order = portal->order;
	expected->from_cell = portal->from_cell;
	expected->to_cell = portal->to_cell;
	expected->boundary_plane = portal->boundary_plane;
	expected->boundary_vertices = portal->boundary_vertices;
	expected->phases = portal->phases;
	expected->direction = portal->direction;
	expected->clearance = portal->clearance;
	expected->contents_from = portal->contents_from;
	expected->contents_to = portal->contents_to;
	expected->flags = portal->flags;
}

static void ExpectedTransition(sg_rune_v2_expected_transition_t *expected,
	const sg_rune_phase_transition_t *transition)
{
	memset(expected, 0, sizeof(*expected));
	expected->id = transition->id;
	expected->cell = transition->cell;
	expected->destination_cell = transition->destination_cell;
	expected->order = transition->order;
	expected->source_phase = transition->source_phase;
	expected->destination_phase = transition->destination_phase;
	expected->kind = transition->kind;
	expected->duration_ms = transition->duration_ms;
	expected->flags = transition->flags;
}

static void ExpectedKernel(sg_rune_v2_expected_kernel_t *expected,
	const sg_rune_capability_kernel_t *kernel)
{
	memset(expected, 0, sizeof(*expected));
	expected->id = kernel->id;
	expected->source_cell = kernel->source_cell;
	expected->order = kernel->order;
	expected->destination_cell = kernel->destination_cell;
	expected->boundary = kernel->boundary;
	expected->affordance = kernel->affordance;
	expected->mechanism = kernel->mechanism;
	expected->source_phase = kernel->source_phase;
	expected->destination_phase = kernel->destination_phase;
	expected->transition = kernel->transition;
	expected->family = kernel->family;
	expected->cost_law = kernel->cost_law;
	expected->parameters = kernel->parameters;
	expected->flags = kernel->flags;
}

static void ExpectedSurface(sg_rune_v2_expected_surface_t *expected,
	const sg_rune_surface_t *surface)
{
	memset(expected, 0, sizeof(*expected));
	expected->id = surface->id;
	expected->geometry = surface->geometry;
	expected->order = surface->order;
	expected->owner_cell = surface->owner_cell;
	expected->plane = surface->plane;
	expected->normal = surface->normal;
	expected->contents = surface->contents;
	expected->semantics = surface->semantics;
}

static void ExpectedAffordance(sg_rune_v2_expected_affordance_t *expected,
	const sg_rune_affordance_t *affordance)
{
	memset(expected, 0, sizeof(*expected));
	expected->id = affordance->id;
	expected->owner_cell = affordance->owner_cell;
	expected->order = affordance->order;
	expected->surfaces = affordance->surfaces;
	expected->phases = affordance->phases;
	expected->kind = affordance->kind;
	expected->range = affordance->range;
	expected->flags = affordance->flags;
}

static void ExpectedLandmark(sg_rune_v2_expected_landmark_t *expected,
	const sg_rune_landmark_t *landmark)
{
	memset(expected, 0, sizeof(*expected));
	expected->id = landmark->id;
	expected->geometry = landmark->geometry;
	expected->order = landmark->order;
	expected->cell = landmark->cell;
	expected->entity = landmark->entity;
	expected->kind = landmark->kind;
	expected->origin = landmark->origin;
	expected->bounds = landmark->bounds;
	expected->mechanism = landmark->mechanism;
	expected->surface = landmark->surface;
	expected->semantics = landmark->semantics;
}

static void ExpectedMechanism(sg_rune_v2_expected_mechanism_t *expected,
	const sg_rune_mechanism_t *mechanism)
{
	memset(expected, 0, sizeof(*expected));
	expected->id = mechanism->id;
	expected->entry_cell = mechanism->entry_cell;
	expected->order = mechanism->order;
	expected->kind = mechanism->kind;
	expected->exit_cell = mechanism->exit_cell;
	expected->activation_landmark = mechanism->activation_landmark;
	expected->entity = mechanism->entity;
	expected->dwell_ms = mechanism->dwell_ms;
	expected->travel_ms = mechanism->travel_ms;
	expected->topology = mechanism->topology;
	expected->flags = mechanism->flags;
}

static void CompleteModelCatalogFactsInit(
	complete_model_catalog_fixture_t *destination,
	const sg_rune_v2_test_model_fixture_t *independent_facts)
{
	const sg_rune_model_t *model = &independent_facts->model;
	const sg_rune_validation_evidence_t *evidence =
		&independent_facts->evidence;
	uint32_t index;

	memset(destination, 0, sizeof(*destination));
	destination->view.version = SG_RUNE_V2_SEMANTIC_CATALOG_VERSION;
	destination->view.binding = independent_facts->binding;
	destination->view.identity = model->identity;
	destination->view.counts = (sg_rune_v2_expected_counts_t){
		.planes = model->plane_count,
		.portal_vertices = model->portal_vertex_count,
		.phases = model->phase_count,
		.phase_transitions = model->phase_transition_count,
		.cells = model->cell_count,
		.portals = model->portal_count,
		.surfaces = model->surface_count,
		.affordances = model->affordance_count,
		.kernels = model->kernel_count,
		.landmarks = model->landmark_count,
		.mechanisms = model->mechanism_count
	};
	destination->view.complete_model_proof.version = evidence->version;
	destination->view.complete_model_proof.verifier_identity = evidence->verifier_identity;
	destination->view.complete_model_proof.bsp_content_id = evidence->bsp_content_id;
	destination->view.complete_model_proof.source_set_identity =
		evidence->source_set_identity;
	destination->view.complete_model_proof.fixed_point_identity =
		evidence->fixed_point_identity;
	destination->view.complete_model_proof.fixed_point_rounds =
		evidence->fixed_point_rounds;
	destination->view.complete_model_proof.expected_cells =
		model->completeness.expected_cells;
	destination->view.complete_model_proof.represented_cells = evidence->proved_cells;
	destination->view.complete_model_proof.expected_portals =
		model->completeness.expected_portals;
	destination->view.complete_model_proof.represented_portals = evidence->proved_portals;
	destination->view.complete_model_proof.omitted_cells = evidence->omitted_cells;
	destination->view.complete_model_proof.omitted_portals = evidence->omitted_portals;
	destination->view.complete_model_proof.invented_portals = evidence->invented_portals;
	destination->view.complete_model_proof.pending_work = evidence->pending_work;
	for (index = 0U; index < model->plane_count; index++)
		ExpectedPlane(&destination->planes[index], &model->planes[index]);
	for (index = 0U; index < model->portal_vertex_count; index++)
		ExpectedVertex(&destination->portal_vertices[index],
			&model->portal_vertices[index]);
	for (index = 0U; index < model->phase_count; index++)
		ExpectedPhase(&destination->phases[index], &model->phases[index]);
	for (index = 0U; index < model->cell_count; index++)
		ExpectedCell(&destination->cells[index], &model->cells[index]);
	for (index = 0U; index < model->portal_count; index++)
		ExpectedPortal(&destination->portals[index], &model->portals[index]);
	for (index = 0U; index < model->phase_transition_count; index++)
		ExpectedTransition(&destination->phase_transitions[index],
			&model->phase_transitions[index]);
	for (index = 0U; index < model->surface_count; index++)
		ExpectedSurface(&destination->surfaces[index], &model->surfaces[index]);
	for (index = 0U; index < model->affordance_count; index++)
		ExpectedAffordance(&destination->affordances[index],
			&model->affordances[index]);
	for (index = 0U; index < model->kernel_count; index++)
		ExpectedKernel(&destination->kernels[index], &model->kernels[index]);
	for (index = 0U; index < model->landmark_count; index++)
		ExpectedLandmark(&destination->landmarks[index],
			&model->landmarks[index]);
	for (index = 0U; index < model->mechanism_count; index++)
		ExpectedMechanism(&destination->mechanisms[index],
			&model->mechanisms[index]);
	destination->view.planes = destination->planes;
	destination->view.portal_vertices = destination->portal_vertices;
	destination->view.phases = destination->phases;
	destination->view.cells = destination->cells;
	destination->view.portals = destination->portals;
	destination->view.phase_transitions = destination->phase_transitions;
	destination->view.surfaces = destination->surfaces;
	destination->view.affordances = destination->affordances;
	destination->view.kernels = destination->kernels;
	destination->view.landmarks = destination->landmarks;
	destination->view.mechanisms = destination->mechanisms;
}

static void CompleteModelCatalogInit(
	complete_model_catalog_fixture_t *destination,
	const sg_rune_v2_test_model_fixture_t *independent_facts)
{
	CompleteModelCatalogFactsInit(destination, independent_facts);
	destination->catalog.view = &destination->view;
	destination->complete_model_proof_output.catalog = &destination->catalog;
	issued_complete_model_proof_output =
		&destination->complete_model_proof_output;
	issued_complete_model_catalog = &destination->catalog;
	issued_complete_model_catalog_storage = destination;
	issued_complete_model_catalog_storage_size = sizeof(*destination);
}

static void MakeDisconnected(sg_rune_v2_test_model_fixture_t *fixture)
{
	SG_RuneV2TestFixtureInit(fixture);
	fixture->model.portal_vertex_count = 0U;
	fixture->model.portal_count = 0U;
	fixture->model.kernel_count = 0U;
	fixture->model.landmark_count = 0U;
	fixture->model.mechanism_count = 0U;
	fixture->cells[0].kernels = (sg_rune_kernel_span_t){ 0U, 0U };
	fixture->cells[0].landmarks = (sg_rune_landmark_span_t){ 0U, 0U };
	fixture->cells[0].mechanisms = (sg_rune_mechanism_span_t){ 0U, 0U };
	fixture->model.completeness.expected_portals = 0U;
	fixture->model.completeness.covered_portals = 0U;
	fixture->evidence.proved_portals = 0U;
}

static void CheckProviderAliasRejected(
	sg_rune_v2_semantic_diagnostic_t expected,
	const unsigned char *encoded, size_t encoded_size,
	complete_model_catalog_fixture_t *authority,
	decoded_fixture_t *scratch, decoded_fixture_t *published,
	const sg_rune_v2_codec_storage_t *scratch_storage,
	const sg_rune_v2_codec_storage_t *published_storage,
	sg_rune_v2_wire_binding_t *binding_out, sg_rune_model_t *model_out,
	sg_rune_validation_evidence_t *evidence_out,
	sg_rune_v2_semantic_report_t *report_out,
	const unsigned char *ordinary_encoded,
	sg_rune_v2_semantic_report_t *ordinary_report,
	int expect_no_provider_query)
{
	complete_model_catalog_fixture_t authority_before = *authority;
	decoded_fixture_t scratch_before = *scratch;
	decoded_fixture_t published_before = *published;
	sg_rune_v2_semantic_report_t report_before = *ordinary_report;
	unsigned char encoded_before[TEST_IMAGE_CAPACITY];

	memcpy(encoded_before, ordinary_encoded, sizeof(encoded_before));
	provider_storage_overlap_calls = 0U;
	CHECK_DIAGNOSTIC(expected,
		SG_RuneV2ArtifactAccept(encoded, encoded_size,
			CompleteModelCatalog(authority), scratch_storage, published_storage,
			binding_out, model_out, evidence_out, report_out));
	CHECK(memcmp(authority, &authority_before, sizeof(*authority)) == 0);
	CHECK(memcmp(scratch, &scratch_before, sizeof(*scratch)) == 0);
	CHECK(memcmp(published, &published_before, sizeof(*published)) == 0);
	CHECK(memcmp(ordinary_encoded, encoded_before,
		sizeof(encoded_before)) == 0);
	CHECK(memcmp(ordinary_report, &report_before,
		sizeof(*ordinary_report)) == 0);
	if (expect_no_provider_query)
		CHECK(provider_storage_overlap_calls == 0U);
	*authority = authority_before;
}

static void TestCompleteDisconnectedModel(void)
{
	sg_rune_v2_test_model_fixture_t expected;
	sg_rune_v2_test_model_fixture_t candidate;
	complete_model_catalog_fixture_t authority;
	unsigned char encoded[TEST_IMAGE_CAPACITY];
	unsigned char before[TEST_IMAGE_CAPACITY];
	size_t encoded_size = 0U;
	sg_rune_v2_semantic_report_t report;

	MakeDisconnected(&expected);
	CompleteModelCatalogInit(&authority, &expected);
	MakeDisconnected(&candidate);
	CHECK(SG_RuneModelValidate(&candidate.model, &candidate.evidence) ==
		SG_RUNE_FAILURE_NONE);
	CHECK(SG_RuneV2CodecEncode(&candidate.binding, &candidate.model,
		&candidate.evidence, encoded, sizeof(encoded), &encoded_size) ==
		SG_RUNE_V2_WIRE_OK);
	memcpy(before, encoded, encoded_size);
	CHECK_DIAGNOSTIC(SG_RUNE_V2_SEMANTIC_OK,
		SG_RuneV2ArtifactLint(encoded, encoded_size,
			CompleteModelCatalog(&authority), &report));
	CHECK(memcmp(before, encoded, encoded_size) == 0);
	CHECK_DIAGNOSTIC(SG_RUNE_V2_SEMANTIC_OK,
		SG_RuneV2ArtifactSemanticCompareForTesting(&candidate.model, &candidate.evidence,
			CompleteModelCatalog(&authority), &report));
	CHECK(candidate.model.landmark_count == 0U);
	CHECK(candidate.model.portal_count == 0U);
}

static void TestCodecToSemanticAgreement(void)
{
	sg_rune_v2_test_model_fixture_t expected;
	sg_rune_v2_test_model_fixture_t candidate;
	complete_model_catalog_fixture_t authority;
	decoded_fixture_t scratch;
	decoded_fixture_t published;
	unsigned char encoded[TEST_IMAGE_CAPACITY];
	size_t encoded_size = 0U;
	sg_rune_v2_semantic_report_t report;

	SG_RuneV2TestFixtureInit(&expected);
	CompleteModelCatalogInit(&authority, &expected);
	SG_RuneV2TestFixtureInit(&candidate);
	DecodedInit(&scratch);
	DecodedInit(&published);
	CHECK(SG_RuneV2CodecEncode(&candidate.binding, &candidate.model,
		&candidate.evidence, encoded, sizeof(encoded), &encoded_size) ==
		SG_RUNE_V2_WIRE_OK);
	CHECK_DIAGNOSTIC(SG_RUNE_V2_SEMANTIC_OK,
		SG_RuneV2ArtifactAccept(encoded, encoded_size, CompleteModelCatalog(&authority),
			&scratch.storage, &published.storage, &published.binding,
			&published.model, &published.evidence, &report));
}

static void TestCopiedCandidateFactsCannotIssueCompleteModelAuthority(void)
{
	sg_rune_v2_test_model_fixture_t expected;
	sg_rune_v2_test_model_fixture_t candidate;
	complete_model_catalog_fixture_t authority;
	complete_model_catalog_fixture_t copied;
	decoded_fixture_t scratch;
	decoded_fixture_t published;
	unsigned char encoded[TEST_IMAGE_CAPACITY];
	size_t encoded_size = 0U;
	sg_rune_v2_semantic_report_t report;
	const sg_rune_v2_semantic_catalog_t *forged;

	SG_RuneV2TestFixtureInit(&expected);
	CompleteModelCatalogInit(&authority, &expected);
	SG_RuneV2TestFixtureInit(&candidate);
	CompleteModelCatalogFactsInit(&copied, &candidate);
	forged = (const sg_rune_v2_semantic_catalog_t *)&copied.view;
	copied.complete_model_proof_output.catalog = forged;
	CHECK(SG_RuneV2CompleteModelProofSemanticCatalog(
		&copied.complete_model_proof_output) == NULL);
	CHECK(SG_RuneV2CodecEncode(&candidate.binding, &candidate.model,
		&candidate.evidence, encoded, sizeof(encoded), &encoded_size) ==
		SG_RUNE_V2_WIRE_OK);
	DecodedInit(&scratch);
	DecodedInit(&published);
	CHECK_DIAGNOSTIC(SG_RUNE_V2_SEMANTIC_CATALOG_REJECTED,
		SG_RuneV2ArtifactAccept(encoded, encoded_size, forged,
			&scratch.storage, &published.storage, &published.binding,
			&published.model, &published.evidence, &report));
	CHECK(CompleteModelCatalog(&authority) == &authority.catalog);
}

static void TestIntegratedAcceptanceRejectsSubstitution(void)
{
	sg_rune_v2_test_model_fixture_t model_a;
	sg_rune_v2_test_model_fixture_t model_b;
	complete_model_catalog_fixture_t authority_b;
	decoded_fixture_t scratch;
	decoded_fixture_t published;
	decoded_fixture_t published_before;
	unsigned char encoded_a[TEST_IMAGE_CAPACITY];
	unsigned char encoded_b[TEST_IMAGE_CAPACITY];
	size_t encoded_a_size = 0U;
	size_t encoded_b_size = 0U;
	sg_rune_v2_semantic_report_t report;

	SG_RuneV2TestFixtureInit(&model_a);
	SG_RuneV2TestFixtureInit(&model_b);
	model_b.portals[0].id = PortalId(9U);
	model_b.portals[0].order = Order(SG_RUNE_ORDER_PORTAL, 9U);
	model_b.kernels[0].boundary = model_b.portals[0].id;
	CHECK(SG_RuneModelValidate(&model_b.model, &model_b.evidence) ==
		SG_RUNE_FAILURE_NONE);
	CompleteModelCatalogInit(&authority_b, &model_b);
	CHECK(SG_RuneV2CodecEncode(&model_a.binding, &model_a.model,
		&model_a.evidence, encoded_a, sizeof(encoded_a), &encoded_a_size) ==
		SG_RUNE_V2_WIRE_OK);
	CHECK(SG_RuneV2CodecEncode(&model_b.binding, &model_b.model,
		&model_b.evidence, encoded_b, sizeof(encoded_b), &encoded_b_size) ==
		SG_RUNE_V2_WIRE_OK);
	CHECK_DIAGNOSTIC(SG_RUNE_V2_SEMANTIC_OK,
		SG_RuneV2ArtifactLint(encoded_a, encoded_a_size,
			CompleteModelCatalog(&authority_b), &report));
	DecodedInit(&scratch);
	DecodedInit(&published);
	CHECK_DIAGNOSTIC(SG_RUNE_V2_SEMANTIC_OK,
		SG_RuneV2ArtifactAccept(encoded_b, encoded_b_size,
			CompleteModelCatalog(&authority_b), &scratch.storage, &published.storage,
			&published.binding, &published.model, &published.evidence, &report));
	CHECK(published.model.planes == published.planes);
	CHECK(published.model.portal_vertices == published.portal_vertices);
	CHECK(published.model.phases == published.phases);
	CHECK(published.model.phase_transitions == published.phase_transitions);
	CHECK(published.model.cells == published.cells);
	CHECK(published.model.portals == published.portals);
	CHECK(published.model.surfaces == published.surfaces);
	CHECK(published.model.affordances == published.affordances);
	CHECK(published.model.kernels == published.kernels);
	CHECK(published.model.landmarks == published.landmarks);
	CHECK(published.model.mechanisms == published.mechanisms);
	published_before = published;
	CHECK_DIAGNOSTIC(SG_RUNE_V2_SEMANTIC_RECORD_MISMATCH,
		SG_RuneV2ArtifactAccept(encoded_a, encoded_a_size,
			CompleteModelCatalog(&authority_b), &scratch.storage, &published.storage,
			&published.binding, &published.model, &published.evidence, &report));
	CHECK(report.section == SG_RUNE_V2_SECTION_PORTALS);
	CHECK(memcmp(&published, &published_before, sizeof(published)) == 0);
}

static void TestAliasedReportCannotMutateAcceptedArtifact(void)
{
	sg_rune_v2_test_model_fixture_t model;
	complete_model_catalog_fixture_t authority;
	decoded_fixture_t scratch;
	decoded_fixture_t published;
	decoded_fixture_t published_before;
	union
	{
		sg_rune_v2_semantic_report_t alignment;
		unsigned char bytes[TEST_IMAGE_CAPACITY];
	} encoded;
	unsigned char encoded_before[TEST_IMAGE_CAPACITY];
	sg_rune_v2_semantic_report_t report;
	sg_rune_v2_semantic_report_t *report_aliases[5];
	size_t encoded_size = 0U;
	size_t alias_index;

	SG_RuneV2TestFixtureInit(&model);
	CompleteModelCatalogInit(&authority, &model);
	DecodedInit(&scratch);
	DecodedInit(&published);
	memset(encoded.bytes, 0xa5, sizeof(encoded.bytes));
	CHECK(SG_RuneV2CodecEncode(&model.binding, &model.model,
		&model.evidence, encoded.bytes, sizeof(encoded.bytes), &encoded_size) ==
		SG_RUNE_V2_WIRE_OK);
	CHECK_DIAGNOSTIC(SG_RUNE_V2_SEMANTIC_OK,
		SG_RuneV2ArtifactAccept(encoded.bytes, encoded_size,
			CompleteModelCatalog(&authority), &scratch.storage, &published.storage,
			&published.binding, &published.model, &published.evidence, &report));
	published_before = published;
	memcpy(encoded_before, encoded.bytes, sizeof(encoded_before));
	report_aliases[0] = &encoded.alignment;
	report_aliases[1] = (sg_rune_v2_semantic_report_t *)(void *)&published.binding;
	report_aliases[2] = (sg_rune_v2_semantic_report_t *)(void *)&published.model;
	report_aliases[3] =
		(sg_rune_v2_semantic_report_t *)(void *)&published.evidence;
	report_aliases[4] = (sg_rune_v2_semantic_report_t *)(void *)&published.cells[0];
	for (alias_index = 0U;
		alias_index < sizeof(report_aliases) / sizeof(report_aliases[0]);
		alias_index++)
	{
		published = published_before;
		memcpy(encoded.bytes, encoded_before, sizeof(encoded.bytes));
		CHECK_DIAGNOSTIC(SG_RUNE_V2_SEMANTIC_INVALID_ARGUMENT,
			SG_RuneV2ArtifactAccept(encoded.bytes, encoded_size,
				CompleteModelCatalog(&authority), &scratch.storage, &published.storage,
				&published.binding, &published.model, &published.evidence,
				report_aliases[alias_index]));
		CHECK(memcmp(&published, &published_before, sizeof(published)) == 0);
		CHECK(memcmp(encoded.bytes, encoded_before, sizeof(encoded.bytes)) == 0);
	}
}

static void TestProviderStorageCannotAliasAcceptanceInputs(void)
{
	sg_rune_v2_test_model_fixture_t model;
	complete_model_catalog_fixture_t authority;
	decoded_fixture_t scratch;
	decoded_fixture_t published;
	unsigned char encoded[TEST_IMAGE_CAPACITY];
	size_t encoded_size = 0U;
	sg_rune_v2_semantic_report_t report;

	MakeDisconnected(&model);
	CompleteModelCatalogInit(&authority, &model);
	memset(encoded, 0xa5, sizeof(encoded));
	CHECK(SG_RuneV2CodecEncode(&model.binding, &model.model,
		&model.evidence, encoded, sizeof(encoded), &encoded_size) ==
		SG_RUNE_V2_WIRE_OK);

	DecodedInit(&scratch);
	DecodedInit(&published);
	memset(&report, 0x5a, sizeof(report));
	CheckProviderAliasRejected(SG_RUNE_V2_SEMANTIC_CIRCULAR_AUTHORITY,
		(const unsigned char *)(const void *)&authority.provider_owned_cell,
		sizeof(authority.provider_owned_cell), &authority, &scratch, &published,
		&scratch.storage, &published.storage, &published.binding,
		&published.model, &published.evidence, &report, encoded, &report, 0);

	DecodedInit(&scratch);
	DecodedInit(&published);
	memset(&report, 0x5a, sizeof(report));
	scratch.storage.landmarks = &authority.provider_owned_landmark;
	scratch.storage.landmark_capacity = 1U;
	CheckProviderAliasRejected(SG_RUNE_V2_SEMANTIC_CIRCULAR_AUTHORITY,
		encoded, encoded_size, &authority, &scratch, &published,
		&scratch.storage, &published.storage, &published.binding,
		&published.model, &published.evidence, &report, encoded, &report, 0);

	DecodedInit(&scratch);
	DecodedInit(&published);
	memset(&report, 0x5a, sizeof(report));
	published.storage.mechanisms = &authority.provider_owned_mechanism;
	published.storage.mechanism_capacity = 1U;
	CheckProviderAliasRejected(SG_RUNE_V2_SEMANTIC_CIRCULAR_AUTHORITY,
		encoded, encoded_size, &authority, &scratch, &published,
		&scratch.storage, &published.storage, &published.binding,
		&published.model, &published.evidence, &report, encoded, &report, 0);

	DecodedInit(&scratch);
	DecodedInit(&published);
	memset(&report, 0x5a, sizeof(report));
	CheckProviderAliasRejected(SG_RUNE_V2_SEMANTIC_CIRCULAR_AUTHORITY,
		encoded, encoded_size, &authority, &scratch, &published,
		&authority.provider_owned_storage, &published.storage,
		&published.binding, &published.model, &published.evidence, &report,
		encoded, &report, 0);

	DecodedInit(&scratch);
	DecodedInit(&published);
	memset(&report, 0x5a, sizeof(report));
	CheckProviderAliasRejected(SG_RUNE_V2_SEMANTIC_CIRCULAR_AUTHORITY,
		encoded, encoded_size, &authority, &scratch, &published,
		&scratch.storage, &authority.provider_owned_storage,
		&published.binding, &published.model, &published.evidence, &report,
		encoded, &report, 0);

	DecodedInit(&scratch);
	DecodedInit(&published);
	memset(&report, 0x5a, sizeof(report));
	CheckProviderAliasRejected(SG_RUNE_V2_SEMANTIC_CIRCULAR_AUTHORITY,
		encoded, encoded_size, &authority, &scratch, &published,
		&scratch.storage, &published.storage,
		&authority.provider_owned_binding, &published.model,
		&published.evidence, &report, encoded, &report, 0);

	DecodedInit(&scratch);
	DecodedInit(&published);
	memset(&report, 0x5a, sizeof(report));
	CheckProviderAliasRejected(SG_RUNE_V2_SEMANTIC_CIRCULAR_AUTHORITY,
		encoded, encoded_size, &authority, &scratch, &published,
		&scratch.storage, &published.storage, &published.binding,
		&authority.provider_owned_model, &published.evidence, &report,
		encoded, &report, 0);

	DecodedInit(&scratch);
	DecodedInit(&published);
	memset(&report, 0x5a, sizeof(report));
	CheckProviderAliasRejected(SG_RUNE_V2_SEMANTIC_CIRCULAR_AUTHORITY,
		encoded, encoded_size, &authority, &scratch, &published,
		&scratch.storage, &published.storage, &published.binding,
		&published.model, &authority.provider_owned_evidence, &report,
		encoded, &report, 0);

	DecodedInit(&scratch);
	DecodedInit(&published);
	memset(&report, 0x5a, sizeof(report));
	CheckProviderAliasRejected(SG_RUNE_V2_SEMANTIC_CIRCULAR_AUTHORITY,
		encoded, encoded_size, &authority, &scratch, &published,
		&scratch.storage, &published.storage, &published.binding,
		&published.model, &published.evidence,
		&authority.provider_owned_report, encoded, &report, 0);

	DecodedInit(&scratch);
	DecodedInit(&published);
	memset(&report, 0x5a, sizeof(report));
	scratch.storage.cells = &authority.provider_owned_cell;
	scratch.storage.cell_capacity = SIZE_MAX;
	CheckProviderAliasRejected(SG_RUNE_V2_SEMANTIC_INVALID_ARGUMENT,
		encoded, encoded_size, &authority, &scratch, &published,
		&scratch.storage, &published.storage, &published.binding,
		&published.model, &published.evidence, &report, encoded, &report, 1);
}

static void TestUnknownCatalogCannotReceiveReportWrite(void)
{
	decoded_fixture_t scratch;
	decoded_fixture_t published;
	sg_rune_v2_test_model_fixture_t candidate;
	struct
	{
		sg_rune_v2_semantic_catalog_t catalog;
		unsigned char padding[64];
	} unknown;
	unsigned char before[sizeof(unknown)];
	unsigned char encoded = 0U;
	sg_rune_v2_semantic_report_t *report =
		(sg_rune_v2_semantic_report_t *)(void *)&unknown.padding[8];

	DecodedInit(&scratch);
	DecodedInit(&published);
	SG_RuneV2TestFixtureInit(&candidate);
	memset(&unknown, 0xa5, sizeof(unknown));
	memcpy(before, &unknown, sizeof(before));
	provider_storage_overlap_calls = 0U;
	CHECK_DIAGNOSTIC(SG_RUNE_V2_SEMANTIC_CATALOG_REJECTED,
		SG_RuneV2ArtifactLint(NULL, 0U, &unknown.catalog, report));
	CHECK(memcmp(&unknown, before, sizeof(unknown)) == 0);
	CHECK(provider_storage_overlap_calls == 0U);
	provider_storage_overlap_calls = 0U;
	CHECK_DIAGNOSTIC(SG_RUNE_V2_SEMANTIC_CATALOG_REJECTED,
		SG_RuneV2ArtifactAccept(&encoded, 1U, &unknown.catalog,
			&scratch.storage, &published.storage, &published.binding,
			&published.model, &published.evidence, report));
	CHECK(memcmp(&unknown, before, sizeof(unknown)) == 0);
	CHECK(provider_storage_overlap_calls == 0U);
	provider_storage_overlap_calls = 0U;
	CHECK_DIAGNOSTIC(SG_RUNE_V2_SEMANTIC_CATALOG_REJECTED,
		SG_RuneV2ArtifactSemanticCompareForTesting(&candidate.model,
			&candidate.evidence, &unknown.catalog, report));
	CHECK(memcmp(&unknown, before, sizeof(unknown)) == 0);
	CHECK(provider_storage_overlap_calls == 0U);
}

static void TestOverflowingCapacityCannotBypassReportGuard(void)
{
	sg_rune_v2_test_model_fixture_t model;
	complete_model_catalog_fixture_t authority;
	decoded_fixture_t scratch;
	decoded_fixture_t published;
	decoded_fixture_t published_before;
	unsigned char encoded[TEST_IMAGE_CAPACITY];
	unsigned char encoded_before[TEST_IMAGE_CAPACITY];
	size_t encoded_size = 0U;

	SG_RuneV2TestFixtureInit(&model);
	CompleteModelCatalogInit(&authority, &model);
	DecodedInit(&scratch);
	DecodedInit(&published);
	memset(encoded, 0xa5, sizeof(encoded));
	CHECK(SG_RuneV2CodecEncode(&model.binding, &model.model,
		&model.evidence, encoded, sizeof(encoded), &encoded_size) ==
		SG_RUNE_V2_WIRE_OK);
	CHECK_DIAGNOSTIC(SG_RUNE_V2_SEMANTIC_OK,
		SG_RuneV2ArtifactAccept(encoded, encoded_size, CompleteModelCatalog(&authority),
			&scratch.storage, &published.storage, &published.binding,
			&published.model, &published.evidence, NULL));
	published.storage.cell_capacity = SIZE_MAX;
	published_before = published;
	memcpy(encoded_before, encoded, sizeof(encoded_before));
	CHECK_DIAGNOSTIC(SG_RUNE_V2_SEMANTIC_INVALID_ARGUMENT,
		SG_RuneV2ArtifactAccept(encoded, encoded_size, CompleteModelCatalog(&authority),
			&scratch.storage, &published.storage, &published.binding,
			&published.model, &published.evidence,
			(sg_rune_v2_semantic_report_t *)(void *)&published.cells[0]));
	CHECK(memcmp(&published, &published_before, sizeof(published)) == 0);
	CHECK(memcmp(encoded, encoded_before, sizeof(encoded)) == 0);
}

static void TestLintReportCannotAliasReadOnlyInputs(void)
{
	sg_rune_v2_test_model_fixture_t model;
	complete_model_catalog_fixture_t authority;
	complete_model_catalog_fixture_t authority_before;
	union
	{
		sg_rune_v2_semantic_report_t alignment;
		unsigned char bytes[TEST_IMAGE_CAPACITY];
	} encoded;
	unsigned char encoded_before[TEST_IMAGE_CAPACITY];
	sg_rune_v2_semantic_report_t report;
	sg_rune_v2_semantic_report_t report_before;
	size_t encoded_size = 0U;

	SG_RuneV2TestFixtureInit(&model);
	CompleteModelCatalogInit(&authority, &model);
	memset(encoded.bytes, 0xa5, sizeof(encoded.bytes));
	CHECK(SG_RuneV2CodecEncode(&model.binding, &model.model,
		&model.evidence, encoded.bytes, sizeof(encoded.bytes), &encoded_size) ==
		SG_RUNE_V2_WIRE_OK);
	authority_before = authority;
	memcpy(encoded_before, encoded.bytes, sizeof(encoded_before));
	CHECK_DIAGNOSTIC(SG_RUNE_V2_SEMANTIC_OK,
		SG_RuneV2ArtifactLint(encoded.bytes, encoded_size,
			CompleteModelCatalog(&authority), &report));
	CHECK_DIAGNOSTIC(SG_RUNE_V2_SEMANTIC_OK,
		SG_RuneV2ArtifactLint(encoded.bytes, encoded_size,
			CompleteModelCatalog(&authority), NULL));
	CHECK(memcmp(encoded.bytes, encoded_before, sizeof(encoded.bytes)) == 0);
	CHECK(memcmp(&authority, &authority_before, sizeof(authority)) == 0);
	memset(&report, 0x5a, sizeof(report));
	report_before = report;
	CHECK_DIAGNOSTIC(SG_RUNE_V2_SEMANTIC_CIRCULAR_AUTHORITY,
		SG_RuneV2ArtifactLint(
			(const unsigned char *)(const void *)&authority.provider_owned_cell,
			sizeof(authority.provider_owned_cell),
			CompleteModelCatalog(&authority), &report));
	CHECK(memcmp(&authority, &authority_before, sizeof(authority)) == 0);
	CHECK(memcmp(&report, &report_before, sizeof(report)) == 0);
	CHECK_DIAGNOSTIC(SG_RUNE_V2_SEMANTIC_INVALID_ARGUMENT,
		SG_RuneV2ArtifactLint(encoded.bytes, encoded_size,
			CompleteModelCatalog(&authority), &encoded.alignment));
	CHECK(memcmp(encoded.bytes, encoded_before, sizeof(encoded.bytes)) == 0);
	memcpy(encoded.bytes, encoded_before, sizeof(encoded.bytes));
	CHECK_DIAGNOSTIC(SG_RUNE_V2_SEMANTIC_CIRCULAR_AUTHORITY,
		SG_RuneV2ArtifactLint(encoded.bytes, encoded_size,
			CompleteModelCatalog(&authority),
			(sg_rune_v2_semantic_report_t *)(void *)&authority.catalog));
	CHECK(memcmp(encoded.bytes, encoded_before, sizeof(encoded.bytes)) == 0);
	CHECK(memcmp(&authority, &authority_before, sizeof(authority)) == 0);
	authority = authority_before;
	CHECK_DIAGNOSTIC(SG_RUNE_V2_SEMANTIC_CIRCULAR_AUTHORITY,
		SG_RuneV2ArtifactLint(encoded.bytes, encoded_size,
			CompleteModelCatalog(&authority),
			(sg_rune_v2_semantic_report_t *)(void *)&authority.cells[0]));
	CHECK(memcmp(encoded.bytes, encoded_before, sizeof(encoded.bytes)) == 0);
	CHECK(memcmp(&authority, &authority_before, sizeof(authority)) == 0);
}

static void TestMissingAndInventedConfiguration(void)
{
	sg_rune_v2_test_model_fixture_t expected;
	sg_rune_v2_test_model_fixture_t candidate;
	complete_model_catalog_fixture_t authority;
	decoded_fixture_t scratch;
	decoded_fixture_t published;
	sg_rune_v2_semantic_report_t report;
	unsigned char encoded[TEST_IMAGE_CAPACITY];
	size_t encoded_size = 0U;

	MakeDisconnected(&expected);
	CompleteModelCatalogInit(&authority, &expected);
	MakeDisconnected(&candidate);
	candidate.model.cell_count = 1U;
	candidate.model.completeness.expected_cells = 1U;
	candidate.model.completeness.covered_cells = 1U;
	candidate.evidence.proved_cells = 1U;
	CHECK(SG_RuneModelValidate(&candidate.model, &candidate.evidence) ==
		SG_RUNE_FAILURE_NONE);
	CHECK(SG_RuneV2ArtifactSemanticCompareForTesting(&candidate.model,
		&candidate.evidence, CompleteModelCatalog(&authority), &report) !=
		SG_RUNE_V2_SEMANTIC_OK);
	CHECK(SG_RuneV2CodecEncode(&candidate.binding, &candidate.model,
		&candidate.evidence, encoded, sizeof(encoded), &encoded_size) ==
		SG_RUNE_V2_WIRE_OK);
	CHECK_DIAGNOSTIC(SG_RUNE_V2_SEMANTIC_SECTION_COUNT_MISMATCH,
		SG_RuneV2ArtifactLint(encoded, encoded_size,
			CompleteModelCatalog(&authority), &report));
	CHECK(report.section == SG_RUNE_V2_SECTION_CELLS);

	MakeDisconnected(&candidate);
	candidate.cells[1].id = CellId(9U);
	candidate.cells[1].order = Order(SG_RUNE_ORDER_CELL, 9U);
	CHECK(SG_RuneModelValidate(&candidate.model, &candidate.evidence) ==
		SG_RUNE_FAILURE_NONE);
	CHECK(SG_RuneV2CodecEncode(&candidate.binding, &candidate.model,
		&candidate.evidence, encoded, sizeof(encoded), &encoded_size) ==
		SG_RUNE_V2_WIRE_OK);
	CHECK_DIAGNOSTIC(SG_RUNE_V2_SEMANTIC_OK,
		SG_RuneV2ArtifactLint(encoded, encoded_size,
			CompleteModelCatalog(&authority), &report));
	DecodedInit(&scratch);
	DecodedInit(&published);
	CHECK_DIAGNOSTIC(SG_RUNE_V2_SEMANTIC_RECORD_MISMATCH,
		SG_RuneV2ArtifactAccept(encoded, encoded_size, CompleteModelCatalog(&authority),
			&scratch.storage, &published.storage, &published.binding,
			&published.model, &published.evidence, &report));
	CHECK(report.section == SG_RUNE_V2_SECTION_CELLS);

	SG_RuneV2TestFixtureInit(&expected);
	CompleteModelCatalogInit(&authority, &expected);
	MakeDisconnected(&candidate);
	CHECK(SG_RuneModelValidate(&candidate.model, &candidate.evidence) ==
		SG_RUNE_FAILURE_NONE);
	CHECK(SG_RuneV2ArtifactSemanticCompareForTesting(&candidate.model,
		&candidate.evidence, CompleteModelCatalog(&authority), &report) !=
		SG_RUNE_V2_SEMANTIC_OK);
	CHECK(SG_RuneV2CodecEncode(&candidate.binding, &candidate.model,
		&candidate.evidence, encoded, sizeof(encoded), &encoded_size) ==
		SG_RUNE_V2_WIRE_OK);
	CHECK_DIAGNOSTIC(SG_RUNE_V2_SEMANTIC_SECTION_COUNT_MISMATCH,
		SG_RuneV2ArtifactLint(encoded, encoded_size,
			CompleteModelCatalog(&authority), &report));
	CHECK(report.section == SG_RUNE_V2_SECTION_PORTAL_VERTICES ||
		report.section == SG_RUNE_V2_SECTION_PORTALS);

	SG_RuneV2TestFixtureInit(&candidate);
	candidate.portals[0].id = PortalId(9U);
	candidate.portals[0].order = Order(SG_RUNE_ORDER_PORTAL, 9U);
	candidate.kernels[0].boundary = candidate.portals[0].id;
	CHECK(SG_RuneModelValidate(&candidate.model, &candidate.evidence) ==
		SG_RUNE_FAILURE_NONE);
	CHECK_DIAGNOSTIC(SG_RUNE_V2_SEMANTIC_RECORD_MISMATCH,
		SG_RuneV2ArtifactSemanticCompareForTesting(&candidate.model,
			&candidate.evidence, CompleteModelCatalog(&authority), &report));
	CHECK(report.section == SG_RUNE_V2_SECTION_PORTALS);
}

static void TestCapabilityReferencesAndCostDomain(void)
{
	sg_rune_v2_test_model_fixture_t expected;
	sg_rune_v2_test_model_fixture_t candidate;
	complete_model_catalog_fixture_t authority;
	sg_rune_v2_semantic_report_t report;

	SG_RuneV2TestFixtureInit(&expected);
	CompleteModelCatalogInit(&authority, &expected);
	SG_RuneV2TestFixtureInit(&candidate);
	candidate.kernels[0].destination_cell = SG_RUNE_CELL_REF_NONE;
	CHECK_DIAGNOSTIC(SG_RUNE_V2_SEMANTIC_MODEL_REJECTED,
		SG_RuneV2ArtifactSemanticCompareForTesting(&candidate.model,
			&candidate.evidence, CompleteModelCatalog(&authority), &report));
	CHECK(report.model_failure == SG_RUNE_FAILURE_INVALID_REFERENCE);

	SG_RuneV2TestFixtureInit(&candidate);
	candidate.kernels[0].parameters.speed.max_value =
		candidate.model.identity.physics.max_velocity + 1.0f;
	CHECK_DIAGNOSTIC(SG_RUNE_V2_SEMANTIC_MODEL_REJECTED,
		SG_RuneV2ArtifactSemanticCompareForTesting(&candidate.model,
			&candidate.evidence, CompleteModelCatalog(&authority), &report));
	CHECK(report.model_failure == SG_RUNE_FAILURE_UNSUPPORTED_PHYSICS);

	SG_RuneV2TestFixtureInit(&candidate);
	candidate.kernels[0].parameters.fixed_latency_ms++;
	CHECK_DIAGNOSTIC(SG_RUNE_V2_SEMANTIC_RECORD_MISMATCH,
		SG_RuneV2ArtifactSemanticCompareForTesting(&candidate.model,
			&candidate.evidence, CompleteModelCatalog(&authority), &report));
	CHECK(report.section == SG_RUNE_V2_SECTION_KERNELS);
}

static void TestSpanOrderAndIdentityFailures(void)
{
	sg_rune_v2_test_model_fixture_t expected;
	sg_rune_v2_test_model_fixture_t candidate;
	complete_model_catalog_fixture_t authority;
	sg_rune_v2_semantic_report_t report;

	SG_RuneV2TestFixtureInit(&expected);
	CompleteModelCatalogInit(&authority, &expected);
	SG_RuneV2TestFixtureInit(&candidate);
	candidate.cells[0].boundary_planes.first = candidate.model.plane_count;
	CHECK_DIAGNOSTIC(SG_RUNE_V2_SEMANTIC_MODEL_REJECTED,
		SG_RuneV2ArtifactSemanticCompareForTesting(&candidate.model,
			&candidate.evidence, CompleteModelCatalog(&authority), &report));
	CHECK(report.model_failure == SG_RUNE_FAILURE_INVALID_REFERENCE);

	SG_RuneV2TestFixtureInit(&candidate);
	candidate.cells[1].order = candidate.cells[0].order;
	candidate.cells[1].id = candidate.cells[0].id;
	CHECK_DIAGNOSTIC(SG_RUNE_V2_SEMANTIC_MODEL_REJECTED,
		SG_RuneV2ArtifactSemanticCompareForTesting(&candidate.model,
			&candidate.evidence, CompleteModelCatalog(&authority), &report));
	CHECK(report.model_failure == SG_RUNE_FAILURE_DUPLICATE_ID);

	SG_RuneV2TestFixtureInit(&candidate);
	candidate.cells[0].id = CellId(99U);
	CHECK_DIAGNOSTIC(SG_RUNE_V2_SEMANTIC_MODEL_REJECTED,
		SG_RuneV2ArtifactSemanticCompareForTesting(&candidate.model,
			&candidate.evidence, CompleteModelCatalog(&authority), &report));
	CHECK(report.model_failure == SG_RUNE_FAILURE_INVALID_REFERENCE);
}

static void TestHostileWireAndUnknownSection(void)
{
	sg_rune_v2_test_model_fixture_t fixture;
	complete_model_catalog_fixture_t authority;
	unsigned char encoded[TEST_IMAGE_CAPACITY];
	unsigned char malformed[TEST_IMAGE_CAPACITY];
	size_t encoded_size = 0U;
	sg_rune_v2_semantic_report_t report;
	unsigned char *entry;

	SG_RuneV2TestFixtureInit(&fixture);
	CompleteModelCatalogInit(&authority, &fixture);
	CHECK(SG_RuneV2CodecEncode(&fixture.binding, &fixture.model,
		&fixture.evidence, encoded, sizeof(encoded), &encoded_size) ==
		SG_RUNE_V2_WIRE_OK);

	memcpy(malformed, encoded, encoded_size);
	entry = SG_RuneV2TestSectionEntry(malformed,
		SG_RUNE_V2_SECTION_CELLS - 1U);
	SG_RuneV2WirePutU32(entry + SG_RUNE_V2_SECTION_COUNT_OFFSET,
		SG_RUNE_MODEL_MAX_CELLS + 1U);
	SG_RuneV2TestFixChecksums(malformed, encoded_size);
	CHECK_DIAGNOSTIC(SG_RUNE_V2_SEMANTIC_WIRE_REJECTED,
		SG_RuneV2ArtifactLint(malformed, encoded_size,
			CompleteModelCatalog(&authority), &report));
	CHECK(report.wire_diagnostic == SG_RUNE_V2_WIRE_HOSTILE_COUNT);

	memcpy(malformed, encoded, encoded_size);
	entry = SG_RuneV2TestSectionEntry(malformed,
		SG_RUNE_V2_SECTION_PORTALS - 1U);
	SG_RuneV2WirePutU16(entry + SG_RUNE_V2_SECTION_TYPE_OFFSET, UINT16_C(99));
	SG_RuneV2TestFixChecksums(malformed, encoded_size);
	CHECK_DIAGNOSTIC(SG_RUNE_V2_SEMANTIC_WIRE_REJECTED,
		SG_RuneV2ArtifactLint(malformed, encoded_size,
			CompleteModelCatalog(&authority), &report));
	CHECK(report.wire_diagnostic != SG_RUNE_V2_WIRE_OK);

	CHECK_DIAGNOSTIC(SG_RUNE_V2_SEMANTIC_WIRE_REJECTED,
		SG_RuneV2ArtifactLint(encoded, encoded_size - 1U,
			CompleteModelCatalog(&authority), &report));
}

static void TestBindingAndEvidenceFailures(void)
{
	sg_rune_v2_test_model_fixture_t expected;
	sg_rune_v2_test_model_fixture_t candidate;
	complete_model_catalog_fixture_t authority;
	unsigned char encoded[TEST_IMAGE_CAPACITY];
	size_t encoded_size = 0U;
	sg_rune_v2_semantic_report_t report;

	SG_RuneV2TestFixtureInit(&expected);
	CompleteModelCatalogInit(&authority, &expected);
	CHECK(SG_RuneV2CodecEncode(&expected.binding, &expected.model,
		&expected.evidence, encoded, sizeof(encoded), &encoded_size) ==
		SG_RUNE_V2_WIRE_OK);
	authority.view.binding.generation++;
	CHECK_DIAGNOSTIC(SG_RUNE_V2_SEMANTIC_BINDING_MISMATCH,
		SG_RuneV2ArtifactLint(encoded, encoded_size,
			CompleteModelCatalog(&authority), &report));
	authority.view.binding.generation--;
	authority.view.complete_model_proof.verifier_identity =
		authority.view.identity.producer_identity;
	CHECK_DIAGNOSTIC(SG_RUNE_V2_SEMANTIC_CATALOG_REJECTED,
		SG_RuneV2ArtifactLint(encoded, encoded_size,
			CompleteModelCatalog(&authority), &report));
	authority.view.complete_model_proof.verifier_identity =
		expected.evidence.verifier_identity;
	authority.view.complete_model_proof.invented_portals = 1U;
	CHECK_DIAGNOSTIC(SG_RUNE_V2_SEMANTIC_CATALOG_REJECTED,
		SG_RuneV2ArtifactLint(encoded, encoded_size,
			CompleteModelCatalog(&authority), &report));
	authority.view.complete_model_proof.invented_portals = 0U;

	SG_RuneV2TestFixtureInit(&candidate);
	candidate.evidence.fixed_point_identity++;
	CHECK_DIAGNOSTIC(SG_RUNE_V2_SEMANTIC_EVIDENCE_MISMATCH,
		SG_RuneV2ArtifactSemanticCompareForTesting(&candidate.model,
			&candidate.evidence, CompleteModelCatalog(&authority), &report));
}

static void TestCircularAuthorityRejected(void)
{
	sg_rune_v2_test_model_fixture_t expected;
	sg_rune_v2_test_model_fixture_t candidate;
	complete_model_catalog_fixture_t authority;
	sg_rune_v2_semantic_report_t report;

	SG_RuneV2TestFixtureInit(&expected);
	CompleteModelCatalogInit(&authority, &expected);
	SG_RuneV2TestFixtureInit(&candidate);
	authority.view.cells = (const sg_rune_v2_expected_cell_t *)
		candidate.model.cells;
	CHECK_DIAGNOSTIC(SG_RUNE_V2_SEMANTIC_CIRCULAR_AUTHORITY,
		SG_RuneV2ArtifactSemanticCompareForTesting(&candidate.model,
			&candidate.evidence, CompleteModelCatalog(&authority), &report));
	authority.view.cells = authority.cells;
	CHECK_DIAGNOSTIC(SG_RUNE_V2_SEMANTIC_CIRCULAR_AUTHORITY,
		SG_RuneV2ArtifactSemanticCompareForTesting(&candidate.model,
			(const sg_rune_validation_evidence_t *)&authority.view.complete_model_proof,
			CompleteModelCatalog(&authority), &report));
}

static void TestReauthenticatedSemanticMutations(void)
{
	sg_rune_v2_test_model_fixture_t expected;
	sg_rune_v2_test_model_fixture_t fixture;
	complete_model_catalog_fixture_t authority;
	decoded_fixture_t scratch;
	decoded_fixture_t published;
	unsigned char encoded[TEST_IMAGE_CAPACITY];
	unsigned char malformed[TEST_IMAGE_CAPACITY];
	size_t encoded_size = 0U;
	static const uint16_t checked_sections[] = {
		SG_RUNE_V2_SECTION_MODEL,
		SG_RUNE_V2_SECTION_PLANES,
		SG_RUNE_V2_SECTION_PORTAL_VERTICES,
		SG_RUNE_V2_SECTION_PHASES,
		SG_RUNE_V2_SECTION_PHASE_TRANSITIONS,
		SG_RUNE_V2_SECTION_CELLS,
		SG_RUNE_V2_SECTION_PORTALS,
		SG_RUNE_V2_SECTION_SURFACES,
		SG_RUNE_V2_SECTION_AFFORDANCES,
		SG_RUNE_V2_SECTION_KERNELS,
		SG_RUNE_V2_SECTION_LANDMARKS,
		SG_RUNE_V2_SECTION_MECHANISMS,
		SG_RUNE_V2_SECTION_BINDING
	};
	size_t section_index;
	size_t byte_index;
	unsigned int bit;

	SG_RuneV2TestFixtureInit(&expected);
	CompleteModelCatalogInit(&authority, &expected);
	SG_RuneV2TestFixtureInit(&fixture);
	CHECK(SG_RuneV2CodecEncode(&fixture.binding, &fixture.model,
		&fixture.evidence, encoded, sizeof(encoded), &encoded_size) ==
		SG_RUNE_V2_WIRE_OK);
	for (section_index = 0U;
		section_index < sizeof(checked_sections) / sizeof(checked_sections[0]);
		section_index++)
	{
		unsigned char *entry = SG_RuneV2TestSectionEntry(encoded,
			(uint32_t)checked_sections[section_index] - 1U);
		size_t first = (size_t)SG_RuneV2WireGetU64(entry +
			SG_RUNE_V2_SECTION_OFFSET_OFFSET);
		size_t bytes = (size_t)SG_RuneV2WireGetU64(entry +
			SG_RUNE_V2_SECTION_BYTES_OFFSET);

		for (byte_index = first; byte_index < first + bytes; byte_index++)
			for (bit = 0U; bit < 8U; bit++)
		{
			sg_rune_v2_semantic_diagnostic_t lint;
			sg_rune_v2_wire_diagnostic_t decode;
			sg_rune_v2_semantic_diagnostic_t semantic =
				SG_RUNE_V2_SEMANTIC_MODEL_REJECTED;

			memcpy(malformed, encoded, encoded_size);
			malformed[byte_index] ^= (unsigned char)(1U << bit);
			SG_RuneV2TestFixChecksums(malformed, encoded_size);
			lint = SG_RuneV2ArtifactLint(malformed, encoded_size,
				CompleteModelCatalog(&authority), NULL);
			DecodedInit(&scratch);
			DecodedInit(&published);
			decode = SG_RuneV2CodecDecode(malformed, encoded_size,
				&scratch.storage, &published.storage, &published.binding,
				&published.model, &published.evidence);
			if (decode == SG_RUNE_V2_WIRE_OK)
				semantic = SG_RuneV2ArtifactSemanticCompareForTesting(&published.model,
					&published.evidence, CompleteModelCatalog(&authority), NULL);
			if (lint == SG_RUNE_V2_SEMANTIC_OK &&
				decode == SG_RUNE_V2_WIRE_OK &&
				semantic == SG_RUNE_V2_SEMANTIC_OK)
			{
				fprintf(stderr,
					"%s:%d: repaired byte %lu bit %u escaped validation\n",
					__FILE__, __LINE__, (unsigned long)byte_index, bit);
				failures++;
			}
		}
	}
}

int main(void)
{
	TestCompleteDisconnectedModel();
	TestCodecToSemanticAgreement();
	TestCopiedCandidateFactsCannotIssueCompleteModelAuthority();
	TestIntegratedAcceptanceRejectsSubstitution();
	TestAliasedReportCannotMutateAcceptedArtifact();
	TestProviderStorageCannotAliasAcceptanceInputs();
	TestUnknownCatalogCannotReceiveReportWrite();
	TestOverflowingCapacityCannotBypassReportGuard();
	TestLintReportCannotAliasReadOnlyInputs();
	TestMissingAndInventedConfiguration();
	TestCapabilityReferencesAndCostDomain();
	TestSpanOrderAndIdentityFailures();
	TestHostileWireAndUnknownSection();
	TestBindingAndEvidenceFailures();
	TestCircularAuthorityRejected();
	TestReauthenticatedSemanticMutations();
	if (failures != 0)
	{
		fprintf(stderr, "sg_rune_v2_artifact_semantic_test: %d failure(s)\n",
			failures);
		return 1;
	}
	puts("sg_rune_v2_artifact_semantic_test: ok");
	return 0;
}
