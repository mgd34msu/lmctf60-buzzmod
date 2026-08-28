#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "slipgate/sg_destination_field.h"

#define TEST_SOURCE_SET UINT64_C(0x5352435345543031)
#define TEST_PHASE_COUNT 3U
#define TEST_CELL_COUNT 2U
#define TEST_TRANSITION_COUNT 2U
#define TEST_KERNEL_COUNT 2U

_Static_assert((SG_DESTINATION_FIELD_CAPABILITY_TRANSITION &
	((UINT32_C(1) << SG_RUNE_CAPABILITY_FAMILY_COUNT) - 1U)) == 0U,
	"transition capability does not collide with kernel families");

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

typedef struct field_fixture_s
{
	sg_rune_plane_t planes[11];
	sg_rune_vec3_t vertices[4];
	sg_rune_phase_basis_t phases[TEST_PHASE_COUNT];
	sg_rune_phase_transition_t transitions[TEST_TRANSITION_COUNT];
	sg_rune_cell_t cells[TEST_CELL_COUNT];
	sg_rune_portal_t portals[1];
	sg_rune_capability_kernel_t kernels[TEST_KERNEL_COUNT];
	sg_phase_coordinate_t coordinates[TEST_PHASE_COUNT];
	sg_rune_model_t model;
	sg_rune_validation_evidence_t evidence;
	sg_rune_runtime_snapshot_t snapshot;
} field_fixture_t;

typedef struct reference_edge_s
{
	uint32_t source;
	uint32_t destination;
	uint32_t cost;
} reference_edge_t;

static sg_rune_order_key_t Order(uint32_t domain, uint32_t ordinal)
{
	return (sg_rune_order_key_t){ TEST_SOURCE_SET, domain, 7U, ordinal, 0U };
}

#define DEFINE_ID_HELPER(name, type, domain) \
static type name(uint32_t ordinal) \
{ \
	type id = { SG_RuneModelStableIdFromOrderKey(& \
		(sg_rune_order_key_t){ TEST_SOURCE_SET, domain, 7U, ordinal, 0U }) }; \
	return id; \
}

DEFINE_ID_HELPER(CellId, sg_rune_cell_id_t, SG_RUNE_ORDER_CELL)
DEFINE_ID_HELPER(PortalId, sg_rune_portal_id_t, SG_RUNE_ORDER_PORTAL)
DEFINE_ID_HELPER(PlaneId, sg_rune_plane_id_t, SG_RUNE_ORDER_PLANE)
DEFINE_ID_HELPER(PhaseId, sg_rune_phase_id_t, SG_RUNE_ORDER_PHASE)
DEFINE_ID_HELPER(TransitionId, sg_rune_phase_transition_id_t,
	SG_RUNE_ORDER_PHASE_TRANSITION)
DEFINE_ID_HELPER(KernelId, sg_rune_kernel_id_t, SG_RUNE_ORDER_KERNEL)

static sg_rune_interval_t Interval(float minimum, float maximum)
{
	return (sg_rune_interval_t){ minimum, maximum };
}

static sg_rune_source_geometry_ref_t Geometry(uint32_t index,
	uint32_t ordinal)
{
	return (sg_rune_source_geometry_ref_t){ TEST_SOURCE_SET, index, ordinal };
}

static void SetPlane(sg_rune_plane_t *plane, uint32_t ordinal,
	float x, float y, float z, float distance)
{
	memset(plane, 0, sizeof(*plane));
	plane->id = PlaneId(ordinal);
	plane->order = Order(SG_RUNE_ORDER_PLANE, ordinal);
	plane->normal.value[0] = x;
	plane->normal.value[1] = y;
	plane->normal.value[2] = z;
	plane->distance = distance;
}

static sg_rune_phase_basis_t Phase(uint32_t ordinal, sg_rune_stance_t stance)
{
	sg_rune_phase_basis_t phase;

	memset(&phase, 0, sizeof(phase));
	phase.id = PhaseId(ordinal);
	phase.order = Order(SG_RUNE_ORDER_PHASE, ordinal);
	phase.stance = stance;
	phase.motion = SG_RUNE_MOTION_SUPPORTED;
	phase.support = SG_RUNE_SUPPORT_SUPPORTED;
	phase.medium = SG_RUNE_MEDIUM_DRY;
	phase.void_relation = SG_RUNE_VOID_CLEAR;
	phase.reference_frame = SG_RUNE_FRAME_WORLD;
	phase.mover = SG_RUNE_MECHANISM_REF_NONE;
	phase.velocity.x = Interval(-320.0f, 320.0f);
	phase.velocity.y = Interval(-320.0f, 320.0f);
	phase.velocity.z = Interval(0.0f, 0.0f);
	phase.elapsed_ms = Interval(0.0f, 1000.0f);
	phase.time_quantum_ms = 8U;
	phase.time_horizon_ms = 2000U;
	return phase;
}

static void SetKernelParameters(sg_rune_capability_kernel_t *kernel,
	float duration_ms, float x, float y, float z)
{
	kernel->family = SG_RUNE_CAPABILITY_CONTINUOUS_SUPPORT;
	kernel->cost_law = SG_RUNE_COST_CONSTANT_RATE;
	kernel->parameters.displacement.x = Interval(x, x);
	kernel->parameters.displacement.y = Interval(y, y);
	kernel->parameters.displacement.z = Interval(z, z);
	kernel->parameters.duration_ms = Interval(duration_ms, duration_ms);
	kernel->parameters.speed = Interval(0.0f, 320.0f);
	kernel->parameters.acceleration = Interval(0.0f, 10.0f);
	kernel->parameters.vertical_acceleration = Interval(0.0f, 10.0f);
	kernel->parameters.gravity = 100.0f;
	kernel->parameters.physics_abi_id = UINT64_C(0x303);
	kernel->flags = SG_RUNE_KERNEL_DIRECTIONAL | SG_RUNE_KERNEL_PHASE_AWARE |
		SG_RUNE_KERNEL_PROVEN;
}

static void SetEvidence(field_fixture_t *fixture)
{
	fixture->evidence = (sg_rune_validation_evidence_t){
		.version = SG_RUNE_VALIDATION_EVIDENCE_VERSION,
		.verifier_identity = UINT64_C(0x50524f4f46564552),
		.bsp_content_id = fixture->model.identity.bsp_content_id,
		.source_set_identity = fixture->model.identity.source_set_identity,
		.fixed_point_identity = UINT64_C(0x4650585441554449),
		.fixed_point_rounds = 3U,
		.proved_cells = fixture->model.cell_count,
		.proved_portals = fixture->model.portal_count
	};
}

static void InitFixture(field_fixture_t *fixture)
{
	sg_rune_capability_kernel_t *kernel;
	uint32_t index;

	memset(fixture, 0, sizeof(*fixture));
	SetPlane(&fixture->planes[0], 0U, 1.0f, 0.0f, 0.0f, 0.0f);
	SetPlane(&fixture->planes[1], 1U, 0.0f, 1.0f, 0.0f, 0.0f);
	SetPlane(&fixture->planes[2], 2U, 0.0f, 1.0f, 0.0f, 64.0f);
	SetPlane(&fixture->planes[3], 3U, 0.0f, 0.0f, 1.0f, 0.0f);
	SetPlane(&fixture->planes[4], 4U, 0.0f, 0.0f, 1.0f, 64.0f);
	SetPlane(&fixture->planes[5], 5U, 1.0f, 0.0f, 0.0f, 64.0f);
	SetPlane(&fixture->planes[6], 6U, 0.0f, 1.0f, 0.0f, 0.0f);
	SetPlane(&fixture->planes[7], 7U, 0.0f, 1.0f, 0.0f, 64.0f);
	SetPlane(&fixture->planes[8], 8U, 0.0f, 0.0f, 1.0f, 0.0f);
	SetPlane(&fixture->planes[9], 9U, 0.0f, 0.0f, 1.0f, 64.0f);
	SetPlane(&fixture->planes[10], 10U, 1.0f, 0.0f, 0.0f, 128.0f);
	fixture->vertices[0] = (sg_rune_vec3_t){ { 64.0f, 16.0f, 16.0f } };
	fixture->vertices[1] = (sg_rune_vec3_t){ { 64.0f, 48.0f, 16.0f } };
	fixture->vertices[2] = (sg_rune_vec3_t){ { 64.0f, 48.0f, 48.0f } };
	fixture->vertices[3] = (sg_rune_vec3_t){ { 64.0f, 16.0f, 48.0f } };

	fixture->phases[0] = Phase(0U, SG_RUNE_STANCE_STANDING);
	fixture->phases[1] = Phase(1U, SG_RUNE_STANCE_CROUCHING);
	fixture->phases[2] = Phase(2U, SG_RUNE_STANCE_STANDING);
	fixture->transitions[0] = (sg_rune_phase_transition_t){
		.id = TransitionId(0U),
		.order = Order(SG_RUNE_ORDER_PHASE_TRANSITION, 0U),
		.cell = CellId(0U),
		.source_phase = fixture->phases[0].id,
		.destination_phase = fixture->phases[1].id,
		.kind = SG_RUNE_PHASE_TRANSITION_STANCE,
		.duration_ms = { 250.0f, 250.0f }
	};
	fixture->transitions[1] = (sg_rune_phase_transition_t){
		.id = TransitionId(1U),
		.order = Order(SG_RUNE_ORDER_PHASE_TRANSITION, 1U),
		.cell = CellId(0U),
		.source_phase = fixture->phases[1].id,
		.destination_phase = fixture->phases[0].id,
		.kind = SG_RUNE_PHASE_TRANSITION_STANCE,
		.duration_ms = { 100.0f, 100.0f }
	};

	fixture->cells[0].id = CellId(0U);
	fixture->cells[0].order = Order(SG_RUNE_ORDER_CELL, 0U);
	fixture->cells[0].geometry = Geometry(0U, 0U);
	fixture->cells[0].bounds = (sg_rune_bounds_t){
		.mins = { { 0.0f, 0.0f, 0.0f } },
		.maxs = { { 64.0f, 64.0f, 64.0f } }
	};
	fixture->cells[0].boundary_planes = (sg_rune_plane_span_t){ 0U, 6U };
	fixture->cells[0].phases = (sg_rune_phase_span_t){ 0U, 2U };
	fixture->cells[0].kernels = (sg_rune_kernel_span_t){ 0U, 2U };
	fixture->cells[0].bsp_leaf.index = 0U;
	fixture->cells[0].bsp_area.index = 0U;
	fixture->cells[0].bsp_cluster.index = 0U;
	fixture->cells[1].id = CellId(1U);
	fixture->cells[1].order = Order(SG_RUNE_ORDER_CELL, 1U);
	fixture->cells[1].geometry = Geometry(0U, 1U);
	fixture->cells[1].bounds = (sg_rune_bounds_t){
		.mins = { { 64.0f, 0.0f, 0.0f } },
		.maxs = { { 128.0f, 64.0f, 64.0f } }
	};
	fixture->cells[1].boundary_planes = (sg_rune_plane_span_t){ 5U, 6U };
	fixture->cells[1].phases = (sg_rune_phase_span_t){ 2U, 1U };
	fixture->cells[1].kernels = (sg_rune_kernel_span_t){ 2U, 0U };
	fixture->cells[1].bsp_leaf.index = 1U;
	fixture->cells[1].bsp_area.index = 1U;
	fixture->cells[1].bsp_cluster.index = 1U;

	fixture->portals[0].id = PortalId(0U);
	fixture->portals[0].order = Order(SG_RUNE_ORDER_PORTAL, 0U);
	fixture->portals[0].geometry = Geometry(1U, 0U);
	fixture->portals[0].from_cell = fixture->cells[0].id;
	fixture->portals[0].to_cell = fixture->cells[1].id;
	fixture->portals[0].boundary_plane = fixture->planes[5].id;
	fixture->portals[0].boundary_vertices =
		(sg_rune_vertex_span_t){ 0U, 4U };
	fixture->portals[0].phases = (sg_rune_phase_span_t){ 0U, 2U };
	fixture->portals[0].direction = SG_RUNE_PORTAL_FROM_TO;
	fixture->portals[0].clearance = 32.0f;
	fixture->portals[0].flags = SG_RUNE_PORTAL_HULL_VALID;

	kernel = &fixture->kernels[0];
	kernel->id = KernelId(0U);
	kernel->order = Order(SG_RUNE_ORDER_KERNEL, 0U);
	kernel->source_cell = fixture->cells[0].id;
	kernel->destination_cell = fixture->cells[0].id;
	kernel->boundary = SG_RUNE_PORTAL_REF_NONE;
	kernel->affordance = SG_RUNE_AFFORDANCE_REF_NONE;
	kernel->mechanism = SG_RUNE_MECHANISM_REF_NONE;
	kernel->source_phase = fixture->phases[0].id;
	kernel->destination_phase = fixture->phases[1].id;
	kernel->transition = fixture->transitions[0].id;
	SetKernelParameters(kernel, 300.0f, 0.0f, 0.0f, 0.0f);
	kernel = &fixture->kernels[1];
	kernel->id = KernelId(1U);
	kernel->order = Order(SG_RUNE_ORDER_KERNEL, 1U);
	kernel->source_cell = fixture->cells[0].id;
	kernel->destination_cell = fixture->cells[1].id;
	kernel->boundary = fixture->portals[0].id;
	kernel->affordance = SG_RUNE_AFFORDANCE_REF_NONE;
	kernel->mechanism = SG_RUNE_MECHANISM_REF_NONE;
	kernel->source_phase = fixture->phases[0].id;
	kernel->destination_phase = fixture->phases[2].id;
	kernel->transition = SG_RUNE_PHASE_TRANSITION_REF_NONE;
	SetKernelParameters(kernel, 700.0f, 64.0f, 0.0f, 0.0f);

	fixture->model.version = SG_RUNE_MODEL_VERSION;
	fixture->model.schema_tag = SG_RUNE_MODEL_SCHEMA_TAG;
	fixture->model.flags = SG_RUNE_MODEL_IMMUTABLE | SG_RUNE_MODEL_EXACT_BOUND |
		SG_RUNE_MODEL_NO_RUNTIME_ACTORS;
	fixture->model.identity.bsp_content_id = UINT64_C(0x101);
	fixture->model.identity.entity_semantics_id = UINT64_C(0x202);
	fixture->model.identity.physics_abi_id = UINT64_C(0x303);
	fixture->model.identity.source_set_identity = TEST_SOURCE_SET;
	fixture->model.identity.schema_id = UINT64_C(0x404);
	fixture->model.identity.producer_identity = UINT64_C(0x50524f4455434552);
	fixture->model.identity.standing_hull = (sg_rune_hull_profile_t){
		.mins = { { -16.0f, -16.0f, -24.0f } },
		.maxs = { { 16.0f, 16.0f, 32.0f } }
	};
	fixture->model.identity.crouching_hull = (sg_rune_hull_profile_t){
		.mins = { { -16.0f, -16.0f, -24.0f } },
		.maxs = { { 16.0f, 16.0f, 16.0f } }
	};
	fixture->model.identity.physics = (sg_rune_physics_parameters_t){
		.gravity = 100.0f,
		.ground_acceleration = 10.0f,
		.air_acceleration = 1.0f,
		.water_acceleration = 4.0f,
		.hook_acceleration = 1000.0f,
		.external_acceleration = 1200.0f,
		.water_drag = 0.5f,
		.max_velocity = 800.0f,
		.frame_ms = 8U,
		.substep_ms = 1U
	};
	fixture->model.completeness = (sg_rune_completeness_t){
		.state = SG_RUNE_COMPLETENESS_COMPLETE,
		.reason = SG_RUNE_FAILURE_NONE,
		.expected_cells = TEST_CELL_COUNT,
		.expected_portals = 1U,
		.covered_cells = TEST_CELL_COUNT,
		.covered_portals = 1U,
		.failure_record = UINT32_MAX
	};
	fixture->model.planes = fixture->planes;
	fixture->model.plane_count = 11U;
	fixture->model.portal_vertices = fixture->vertices;
	fixture->model.portal_vertex_count = 4U;
	fixture->model.phases = fixture->phases;
	fixture->model.phase_count = TEST_PHASE_COUNT;
	fixture->model.phase_transitions = fixture->transitions;
	fixture->model.phase_transition_count = TEST_TRANSITION_COUNT;
	fixture->model.cells = fixture->cells;
	fixture->model.cell_count = TEST_CELL_COUNT;
	fixture->model.portals = fixture->portals;
	fixture->model.portal_count = 1U;
	fixture->model.kernels = fixture->kernels;
	fixture->model.kernel_count = TEST_KERNEL_COUNT;
	SetEvidence(fixture);
	for (index = 0U; index < TEST_PHASE_COUNT; index++)
		fixture->coordinates[index] = (sg_phase_coordinate_t){
			index, index < 2U ? 0U : 1U
		};
	fixture->snapshot = (sg_rune_runtime_snapshot_t){
		.identity = UINT64_C(0x909),
		.topology_revision = UINT64_C(7),
		.cell_count = TEST_CELL_COUNT,
		.phase_count = TEST_PHASE_COUNT,
		.region_count = TEST_CELL_COUNT,
		.model = &fixture->model,
		.phases = fixture->coordinates
	};
}

static sg_destination_handle_t Destination(uint64_t generation,
	uint32_t phase_id, uint32_t cell_id)
{
	sg_destination_handle_t destination;

	memset(&destination, 0, sizeof(destination));
	destination.id = UINT64_C(0x44);
	destination.generation = generation;
	destination.kind = SG_DESTINATION_WAYPOINT;
	destination.motion = SG_DESTINATION_STATIC;
	destination.valid = 1U;
	destination.pose.phase = (sg_phase_coordinate_t){ phase_id, cell_id };
	destination.pose.position[0] = 16.0f;
	destination.pose.position[1] = 16.0f;
	destination.pose.position[2] = 16.0f;
	destination.pose.region_id = cell_id;
	return destination;
}

static uint32_t ReferenceCost(uint32_t source, uint32_t destination)
{
	static const reference_edge_t edges[] = {
		{ 0U, 1U, 300U }, { 0U, 2U, 700U }, { 1U, 0U, 100U }
	};
	uint32_t distance[TEST_PHASE_COUNT];
	uint8_t settled[TEST_PHASE_COUNT] = { 0U };
	uint32_t index;

	for (index = 0U; index < TEST_PHASE_COUNT; index++)
		distance[index] = SG_DESTINATION_FIELD_INF;
	distance[source] = 0U;
	for (;;) {
		uint32_t current = SG_DESTINATION_FIELD_NO_PHASE;
		uint32_t best = SG_DESTINATION_FIELD_INF;

		for (index = 0U; index < TEST_PHASE_COUNT; index++)
			if (!settled[index] && distance[index] < best) {
				best = distance[index];
				current = index;
			}
		if (current == SG_DESTINATION_FIELD_NO_PHASE || current == destination)
			break;
		settled[current] = 1U;
		for (index = 0U; index < sizeof(edges) / sizeof(edges[0]); index++)
			if (edges[index].source == current &&
				distance[current] <= SG_DESTINATION_FIELD_INF - edges[index].cost &&
				distance[current] + edges[index].cost <
				 distance[edges[index].destination])
				distance[edges[index].destination] =
					distance[current] + edges[index].cost;
	}
	return distance[destination];
}

static void TestCanonicalModelAndTransitions(void)
{
	field_fixture_t fixture;
	sg_field_sample_t samples[TEST_PHASE_COUNT];
	sg_destination_field_t field;
	sg_destination_handle_t destination;
	uint32_t destination_phase;
	uint32_t phase;

	InitFixture(&fixture);
	CHECK(SG_RuneModelValidate(&fixture.model, &fixture.evidence) ==
		SG_RUNE_FAILURE_NONE);
	for (destination_phase = 0U; destination_phase < TEST_PHASE_COUNT;
		destination_phase++) {
		destination = Destination(1U, destination_phase,
			fixture.coordinates[destination_phase].cell_id);
		CHECK(SG_DestinationFieldSolve(&fixture.snapshot, &destination, 100U,
			samples, TEST_PHASE_COUNT, &field));
		CHECK(SG_DestinationFieldValid(&fixture.snapshot, &field));
		for (phase = 0U; phase < TEST_PHASE_COUNT; phase++)
			CHECK(samples[phase].cost_ms ==
				ReferenceCost(phase, destination_phase));
	}
	destination = Destination(1U, 0U, 0U);
	CHECK(SG_DestinationFieldSolve(&fixture.snapshot, &destination, 100U,
		samples, TEST_PHASE_COUNT, &field));
	CHECK(samples[1].cost_ms == 100U);
	CHECK(samples[1].capability_mask ==
		SG_DESTINATION_FIELD_CAPABILITY_TRANSITION);
	destination = Destination(1U, 1U, 0U);
	CHECK(SG_DestinationFieldSolve(&fixture.snapshot, &destination, 100U,
		samples, TEST_PHASE_COUNT, &field));
	CHECK(samples[0].cost_ms == 300U);
	CHECK(samples[0].capability_mask ==
		(UINT32_C(1) << SG_RUNE_CAPABILITY_CONTINUOUS_SUPPORT));
}

static void TestDeterminismAndImmutability(void)
{
	field_fixture_t fixture;
	field_fixture_t fixture_copy;
	sg_field_sample_t first[TEST_PHASE_COUNT];
	sg_field_sample_t second[TEST_PHASE_COUNT];
	sg_destination_field_t field;
	sg_destination_handle_t destination;

	InitFixture(&fixture);
	CHECK(SG_RuneModelValidate(&fixture.model, &fixture.evidence) ==
		SG_RUNE_FAILURE_NONE);
	fixture_copy = fixture;
	destination = Destination(1U, 2U, 1U);
	CHECK(SG_DestinationFieldSolve(&fixture.snapshot, &destination, 100U,
		first, TEST_PHASE_COUNT, &field));
	CHECK(SG_DestinationFieldSolve(&fixture.snapshot, &destination, 100U,
		second, TEST_PHASE_COUNT, &field));
	CHECK(memcmp(first, second, sizeof(first)) == 0);
	CHECK(memcmp(&fixture_copy, &fixture, sizeof(fixture)) == 0);
}

static void TestQueryAndUpdatePolicy(void)
{
	field_fixture_t fixture;
	sg_field_sample_t samples[TEST_PHASE_COUNT];
	sg_destination_field_t field;
	sg_destination_handle_t destination;
	sg_destination_handle_t changed;
	sg_destination_pose_t query;
	sg_field_sample_t result;
	sg_field_sample_t temporary;

	InitFixture(&fixture);
	CHECK(SG_RuneModelValidate(&fixture.model, &fixture.evidence) ==
		SG_RUNE_FAILURE_NONE);
	destination = Destination(1U, 0U, 0U);
	query = Destination(2U, 1U, 0U).pose;
	CHECK(SG_DestinationFieldSolve(&fixture.snapshot, &destination, 100U,
		samples, TEST_PHASE_COUNT, &field));
	CHECK(SG_FieldQuery(&fixture.snapshot, &field, &query, &result));
	CHECK(result.cost_ms == 100U);
	temporary = samples[0];
	samples[0] = samples[1];
	samples[1] = temporary;
	CHECK(!SG_FieldQuery(&fixture.snapshot, &field, &query, &result));
	temporary = samples[0];
	samples[0] = samples[1];
	samples[1] = temporary;
	CHECK(!SG_FieldNeedsUpdate(&fixture.snapshot, &field, &destination));
	CHECK(SG_FieldCanReuseStatic(&fixture.snapshot, &field, &destination));
	changed = destination;
	changed.generation++;
	CHECK(SG_FieldNeedsUpdate(&fixture.snapshot, &field, &changed));
	CHECK(SG_FieldCanReuseStatic(&fixture.snapshot, &field, &changed));
}

static void TestExactTerminalPose(void)
{
	field_fixture_t fixture;
	sg_field_sample_t first_samples[TEST_PHASE_COUNT];
	sg_field_sample_t second_samples[TEST_PHASE_COUNT];
	sg_destination_field_t first_field;
	sg_destination_field_t second_field;
	sg_destination_handle_t first_destination;
	sg_destination_handle_t second_destination;
	sg_destination_pose_t source;
	sg_field_sample_t first_result;
	sg_field_sample_t second_result;

	InitFixture(&fixture);
	CHECK(SG_RuneModelValidate(&fixture.model, &fixture.evidence) ==
		SG_RUNE_FAILURE_NONE);
	first_destination = Destination(1U, 1U, 0U);
	first_destination.pose.position[0] = 32.0f;
	first_destination.pose.position[1] = 0.0f;
	first_destination.pose.position[2] = 0.0f;
	first_destination.pose.velocity[0] = 20.0f;
	second_destination = Destination(2U, 1U, 0U);
	second_destination.pose.position[0] = 0.0f;
	second_destination.pose.position[1] = 32.0f;
	second_destination.pose.position[2] = 0.0f;
	second_destination.pose.velocity[1] = 5.0f;
	source = Destination(3U, 1U, 0U).pose;
	source.position[0] = 0.0f;
	source.position[1] = 0.0f;
	source.position[2] = 0.0f;
	CHECK(SG_DestinationFieldSolve(&fixture.snapshot, &first_destination, 100U,
		first_samples, TEST_PHASE_COUNT, &first_field));
	CHECK(SG_DestinationFieldSolve(&fixture.snapshot, &second_destination, 100U,
		second_samples, TEST_PHASE_COUNT, &second_field));
	CHECK(memcmp(first_samples, second_samples, sizeof(first_samples)) == 0);
	CHECK(SG_FieldQuery(&fixture.snapshot, &first_field, &source, &first_result));
	CHECK(SG_FieldQuery(&fixture.snapshot, &second_field, &source, &second_result));
	CHECK(first_result.cost_ms == 56U);
	CHECK(second_result.cost_ms == 48U);
	CHECK(first_result.direction[0] == 1.0f && first_result.direction[1] == 0.0f);
	CHECK(second_result.direction[0] == 0.0f && second_result.direction[1] == 1.0f);
	CHECK(first_result.velocity_direction[0] == 1.0f);
	CHECK(second_result.velocity_direction[1] == 1.0f);
}

int main(void)
{
	TestCanonicalModelAndTransitions();
	TestDeterminismAndImmutability();
	TestQueryAndUpdatePolicy();
	TestExactTerminalPose();
	if (failures != 0) {
		fprintf(stderr, "%d destination-field checks failed\n", failures);
		return 1;
	}
	puts("destination field tests passed");
	return 0;
}
