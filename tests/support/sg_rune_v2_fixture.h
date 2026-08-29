/* Shared canonical RUNE v2 model and wire-image test fixture. */
#ifndef SG_RUNE_V2_FIXTURE_H
#define SG_RUNE_V2_FIXTURE_H

#include <stdint.h>
#include <string.h>

#include "slipgate/sg_rune_v2_codec.h"

#define TEST_IMAGE_CAPACITY 4096U
#define TEST_IMAGE_BYTES 3352U
#define SOURCE_SET_ID UINT64_C(0x5352435345543031)

typedef struct sg_rune_v2_test_model_fixture_s
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
	sg_rune_model_t model;
	sg_rune_validation_evidence_t evidence;
	sg_rune_v2_wire_binding_t binding;
} sg_rune_v2_test_model_fixture_t;

static sg_rune_order_key_t Order(uint32_t domain, uint32_t ordinal)
{
	sg_rune_order_key_t order = {
		SOURCE_SET_ID, domain, 7U, ordinal, ordinal + 11U
	};
	return order;
}

#define DEFINE_ID_HELPER(name, type, domain) \
static type name(uint32_t ordinal) \
{ \
	sg_rune_order_key_t order = Order((domain), ordinal); \
	type id = { SG_RuneModelStableIdFromOrderKey(&order) }; \
	return id; \
}

DEFINE_ID_HELPER(CellId, sg_rune_cell_id_t, SG_RUNE_ORDER_CELL)
DEFINE_ID_HELPER(PortalId, sg_rune_portal_id_t, SG_RUNE_ORDER_PORTAL)
DEFINE_ID_HELPER(PlaneId, sg_rune_plane_id_t, SG_RUNE_ORDER_PLANE)
DEFINE_ID_HELPER(PhaseId, sg_rune_phase_id_t, SG_RUNE_ORDER_PHASE)
DEFINE_ID_HELPER(TransitionId, sg_rune_phase_transition_id_t,
	SG_RUNE_ORDER_PHASE_TRANSITION)
DEFINE_ID_HELPER(SurfaceId, sg_rune_surface_id_t, SG_RUNE_ORDER_SURFACE)
DEFINE_ID_HELPER(AffordanceId, sg_rune_affordance_id_t,
	SG_RUNE_ORDER_AFFORDANCE)
DEFINE_ID_HELPER(KernelId, sg_rune_kernel_id_t, SG_RUNE_ORDER_KERNEL)
DEFINE_ID_HELPER(LandmarkId, sg_rune_landmark_id_t, SG_RUNE_ORDER_LANDMARK)
DEFINE_ID_HELPER(MechanismId, sg_rune_mechanism_id_t,
	SG_RUNE_ORDER_MECHANISM)

static sg_rune_interval_t Interval(float minimum, float maximum)
{
	sg_rune_interval_t interval = { minimum, maximum };
	return interval;
}

static sg_rune_source_geometry_ref_t Geometry(uint32_t index,
	uint32_t ordinal)
{
	sg_rune_source_geometry_ref_t geometry = {
		SOURCE_SET_ID, index, ordinal
	};
	return geometry;
}

static sg_rune_v2_content_id_t ContentId(uint8_t seed)
{
	sg_rune_v2_content_id_t id = { { 0 } };
	unsigned int index;

	for (index = 0U; index < SG_RUNE_V2_CONTENT_ID_BYTES; index++)
		id.bytes[index] = (uint8_t)(seed + index);
	return id;
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

static sg_rune_phase_basis_t Phase(uint32_t ordinal, sg_rune_stance_t stance,
	sg_rune_motion_t motion, sg_rune_support_t support)
{
	sg_rune_phase_basis_t phase;

	memset(&phase, 0, sizeof(phase));
	phase.id = PhaseId(ordinal);
	phase.order = Order(SG_RUNE_ORDER_PHASE, ordinal);
	phase.stance = stance;
	phase.motion = motion;
	phase.support = support;
	phase.medium = SG_RUNE_MEDIUM_DRY;
	phase.void_relation = SG_RUNE_VOID_CLEAR;
	phase.reference_frame = SG_RUNE_FRAME_WORLD;
	phase.mover = SG_RUNE_MECHANISM_REF_NONE;
	phase.velocity.x = Interval(-321.25f, 322.5f);
	phase.velocity.y = Interval(-123.75f, 124.25f);
	phase.velocity.z = Interval(-800.5f, 801.0f);
	phase.elapsed_ms = Interval(0.25f, 1000.75f);
	phase.time_quantum_ms = 8U;
	phase.time_horizon_ms = 2000U;
	return phase;
}

static void SG_RuneV2TestFixtureInit(sg_rune_v2_test_model_fixture_t *fixture)
{
	sg_rune_model_t *model;
	sg_rune_capability_kernel_t *kernel;

	memset(fixture, 0, sizeof(*fixture));
	SetPlane(&fixture->planes[0], 0U, 1.0f, 0.0f, 0.0f, 0.0f);
	SetPlane(&fixture->planes[1], 1U, 0.0f, 1.0f, 0.0f, 0.0f);
	SetPlane(&fixture->planes[2], 2U, 0.0f, 0.0f, 1.0f, 0.0f);
	SetPlane(&fixture->planes[3], 3U, -1.0f, 0.0f, 0.0f, -64.0f);
	SetPlane(&fixture->planes[4], 4U, 1.0f, 0.0f, 0.0f, 64.0f);
	SetPlane(&fixture->planes[5], 5U, 0.0f, 1.0f, 0.0f, 64.0f);
	SetPlane(&fixture->planes[6], 6U, 0.0f, 0.0f, 1.0f, 64.0f);
	SetPlane(&fixture->planes[7], 7U, -1.0f, 0.0f, 0.0f, -128.0f);
	fixture->portal_vertices[0] =
		(sg_rune_vec3_t){ { 64.25f, 8.5f, 8.75f } };
	fixture->portal_vertices[1] =
		(sg_rune_vec3_t){ { 64.25f, 56.5f, 8.75f } };
	fixture->portal_vertices[2] =
		(sg_rune_vec3_t){ { 64.25f, 32.5f, 56.75f } };
	fixture->phases[0] = Phase(0U, SG_RUNE_STANCE_STANDING,
		SG_RUNE_MOTION_SUPPORTED, SG_RUNE_SUPPORT_SUPPORTED);
	fixture->phases[1] = Phase(1U, SG_RUNE_STANCE_CROUCHING,
		SG_RUNE_MOTION_SUPPORTED, SG_RUNE_SUPPORT_SUPPORTED);
	fixture->phases[2] = Phase(2U, SG_RUNE_STANCE_STANDING,
		SG_RUNE_MOTION_AIRBORNE, SG_RUNE_SUPPORT_NONE);
	fixture->phase_transitions[0].id = TransitionId(0U);
	fixture->phase_transitions[0].order =
		Order(SG_RUNE_ORDER_PHASE_TRANSITION, 0U);
	fixture->phase_transitions[0].cell = CellId(0U);
	fixture->phase_transitions[0].destination_cell = CellId(0U);
	fixture->phase_transitions[0].source_phase = PhaseId(0U);
	fixture->phase_transitions[0].destination_phase = PhaseId(1U);
	fixture->phase_transitions[0].kind = SG_RUNE_PHASE_TRANSITION_STANCE;
	fixture->phase_transitions[0].duration_ms = Interval(8.5f, 250.75f);

	fixture->cells[0].id = CellId(0U);
	fixture->cells[0].order = Order(SG_RUNE_ORDER_CELL, 0U);
	fixture->cells[0].geometry = Geometry(0U, 4U);
	fixture->cells[0].bounds.mins =
		(sg_rune_vec3_t){ { 0.25f, 0.5f, 0.75f } };
	fixture->cells[0].bounds.maxs =
		(sg_rune_vec3_t){ { 64.25f, 64.5f, 64.75f } };
	fixture->cells[0].boundary_planes = (sg_rune_plane_span_t){ 0U, 4U };
	fixture->cells[0].phases = (sg_rune_phase_span_t){ 0U, 2U };
	fixture->cells[0].surfaces = (sg_rune_surface_span_t){ 0U, 1U };
	fixture->cells[0].affordances = (sg_rune_affordance_span_t){ 0U, 1U };
	fixture->cells[0].kernels = (sg_rune_kernel_span_t){ 0U, 1U };
	fixture->cells[0].landmarks = (sg_rune_landmark_span_t){ 0U, 1U };
	fixture->cells[0].mechanisms = (sg_rune_mechanism_span_t){ 0U, 1U };
	fixture->cells[0].bsp_leaf.index = 3U;
	fixture->cells[0].bsp_area.index = 5U;
	fixture->cells[0].bsp_cluster.index = 7U;
	fixture->cells[0].contents = SG_RUNE_CONTENTS_WINDOW;
	fixture->cells[0].semantics = SG_RUNE_CELL_SEMANTIC_HAZARD |
		SG_RUNE_CELL_SEMANTIC_VOID_BOUNDARY;
	fixture->cells[1].id = CellId(1U);
	fixture->cells[1].order = Order(SG_RUNE_ORDER_CELL, 1U);
	fixture->cells[1].geometry = Geometry(1U, 8U);
	fixture->cells[1].bounds.mins =
		(sg_rune_vec3_t){ { 64.25f, 0.5f, 0.75f } };
	fixture->cells[1].bounds.maxs =
		(sg_rune_vec3_t){ { 128.25f, 64.5f, 64.75f } };
	fixture->cells[1].boundary_planes = (sg_rune_plane_span_t){ 4U, 4U };
	fixture->cells[1].phases = (sg_rune_phase_span_t){ 2U, 1U };
	fixture->cells[1].bsp_leaf.index = 11U;
	fixture->cells[1].bsp_area.index = 13U;
	fixture->cells[1].bsp_cluster.index = 17U;
	fixture->cells[1].contents = SG_RUNE_CONTENTS_EMPTY;
	fixture->cells[1].semantics = SG_RUNE_CELL_SEMANTIC_SKY_BOUNDARY;

	fixture->portals[0].id = PortalId(0U);
	fixture->portals[0].order = Order(SG_RUNE_ORDER_PORTAL, 0U);
	fixture->portals[0].geometry = Geometry(2U, 3U);
	fixture->portals[0].from_cell = CellId(0U);
	fixture->portals[0].to_cell = CellId(1U);
	fixture->portals[0].boundary_plane = PlaneId(4U);
	fixture->portals[0].boundary_vertices =
		(sg_rune_vertex_span_t){ 0U, 3U };
	fixture->portals[0].phases = (sg_rune_phase_span_t){ 0U, 1U };
	fixture->portals[0].direction = SG_RUNE_PORTAL_FROM_TO;
	fixture->portals[0].clearance = 31.75f;
	fixture->portals[0].contents_from = SG_RUNE_CONTENTS_WINDOW;
	fixture->portals[0].contents_to = SG_RUNE_CONTENTS_EMPTY;
	fixture->portals[0].flags = SG_RUNE_PORTAL_HULL_VALID |
		SG_RUNE_PORTAL_CONTENTS_CHANGE | SG_RUNE_PORTAL_VOID_EDGE;

	fixture->surfaces[0].id = SurfaceId(0U);
	fixture->surfaces[0].order = Order(SG_RUNE_ORDER_SURFACE, 0U);
	fixture->surfaces[0].geometry = Geometry(3U, 9U);
	fixture->surfaces[0].owner_cell = CellId(0U);
	fixture->surfaces[0].plane = PlaneId(3U);
	fixture->surfaces[0].normal =
		(sg_rune_vec3_t){ { -1.0f, 0.25f, 0.5f } };
	fixture->surfaces[0].contents = SG_RUNE_CONTENTS_WINDOW;
	fixture->surfaces[0].semantics = SG_RUNE_SURFACE_SEMANTIC_HOOKABLE |
		SG_RUNE_SURFACE_SEMANTIC_COVER_BOUNDARY |
		SG_RUNE_SURFACE_SEMANTIC_BOUNCE;

	fixture->affordances[0].id = AffordanceId(0U);
	fixture->affordances[0].order = Order(SG_RUNE_ORDER_AFFORDANCE, 0U);
	fixture->affordances[0].owner_cell = CellId(0U);
	fixture->affordances[0].surfaces = (sg_rune_surface_span_t){ 0U, 1U };
	fixture->affordances[0].phases = (sg_rune_phase_span_t){ 0U, 1U };
	fixture->affordances[0].kind = SG_RUNE_AFFORDANCE_HOOKABLE_REGION;
	fixture->affordances[0].range = Interval(0.5f, 8192.75f);
	fixture->affordances[0].flags = UINT32_C(0x12345678);

	fixture->mechanisms[0].id = MechanismId(0U);
	fixture->mechanisms[0].order = Order(SG_RUNE_ORDER_MECHANISM, 0U);
	fixture->mechanisms[0].kind = SG_RUNE_MECHANISM_DOOR;
	fixture->mechanisms[0].entry_cell = CellId(0U);
	fixture->mechanisms[0].exit_cell = CellId(1U);
	fixture->mechanisms[0].activation_landmark = LandmarkId(0U);
	fixture->mechanisms[0].entity = (sg_rune_entity_ref_t){ 19U, 23U };
	fixture->mechanisms[0].dwell_ms = Interval(10.25f, 20.5f);
	fixture->mechanisms[0].travel_ms = Interval(30.75f, 40.125f);
	fixture->mechanisms[0].topology =
		(sg_rune_mechanism_span_t){ 0U, 1U };
	fixture->mechanisms[0].flags = UINT32_C(0xa5a55a5a);

	fixture->landmarks[0].id = LandmarkId(0U);
	fixture->landmarks[0].order = Order(SG_RUNE_ORDER_LANDMARK, 0U);
	fixture->landmarks[0].geometry = Geometry(4U, 12U);
	fixture->landmarks[0].cell = CellId(0U);
	fixture->landmarks[0].entity = (sg_rune_entity_ref_t){ 29U, 31U };
	fixture->landmarks[0].kind = SG_RUNE_LANDMARK_FLAG_STAND;
	fixture->landmarks[0].origin =
		(sg_rune_vec3_t){ { 16.25f, 16.5f, 16.75f } };
	fixture->landmarks[0].bounds.mins =
		(sg_rune_vec3_t){ { 8.25f, 8.5f, 8.75f } };
	fixture->landmarks[0].bounds.maxs =
		(sg_rune_vec3_t){ { 24.25f, 24.5f, 24.75f } };
	fixture->landmarks[0].mechanism = MechanismId(0U);
	fixture->landmarks[0].surface = SurfaceId(0U);
	fixture->landmarks[0].semantics = UINT32_C(0x89abcdef);

	kernel = &fixture->kernels[0];
	kernel->id = KernelId(0U);
	kernel->order = Order(SG_RUNE_ORDER_KERNEL, 0U);
	kernel->source_cell = CellId(0U);
	kernel->destination_cell = CellId(1U);
	kernel->boundary = PortalId(0U);
	kernel->affordance = AffordanceId(0U);
	kernel->mechanism = MechanismId(0U);
	kernel->source_phase = PhaseId(0U);
	kernel->destination_phase = PhaseId(2U);
	kernel->transition = SG_RUNE_PHASE_TRANSITION_REF_NONE;
	kernel->family = SG_RUNE_CAPABILITY_HOOK_TRAJECTORY;
	kernel->cost_law = SG_RUNE_COST_TETHERED;
	kernel->parameters.displacement.x = Interval(63.5f, 64.25f);
	kernel->parameters.displacement.y = Interval(-1.5f, 2.25f);
	kernel->parameters.displacement.z = Interval(0.75f, 64.5f);
	kernel->parameters.duration_ms = Interval(100.25f, 750.5f);
	kernel->parameters.speed = Interval(0.5f, 799.75f);
	kernel->parameters.acceleration = Interval(0.25f, 999.5f);
	kernel->parameters.vertical_acceleration = Interval(0.75f, 998.5f);
	kernel->parameters.gravity = 100.25f;
	kernel->parameters.drag = 0.0f;
	kernel->parameters.physics_abi_id = UINT64_C(0x3030405060708090);
	kernel->parameters.fixed_latency_ms = 37U;
	kernel->parameters.dwell_ms = 41U;
	kernel->flags = SG_RUNE_KERNEL_DIRECTIONAL |
		SG_RUNE_KERNEL_PHASE_AWARE | SG_RUNE_KERNEL_PROVEN;

	model = &fixture->model;
	model->version = SG_RUNE_MODEL_VERSION;
	model->schema_tag = SG_RUNE_MODEL_SCHEMA_TAG;
	model->flags = SG_RUNE_MODEL_IMMUTABLE | SG_RUNE_MODEL_EXACT_BOUND |
		SG_RUNE_MODEL_NO_RUNTIME_ACTORS;
	model->identity.bsp_content_id = UINT64_C(0x1011121314151617);
	model->identity.entity_semantics_id = UINT64_C(0x2021222324252627);
	model->identity.physics_abi_id = UINT64_C(0x3030405060708090);
	model->identity.source_set_identity = SOURCE_SET_ID;
	model->identity.schema_id = UINT64_C(0x4041424344454647);
	model->identity.producer_identity = UINT64_C(0x5051525354555657);
	model->identity.standing_hull.mins =
		(sg_rune_vec3_t){ { -16.25f, -16.5f, -24.75f } };
	model->identity.standing_hull.maxs =
		(sg_rune_vec3_t){ { 16.25f, 16.5f, 32.75f } };
	model->identity.crouching_hull.mins =
		(sg_rune_vec3_t){ { -15.25f, -15.5f, -23.75f } };
	model->identity.crouching_hull.maxs =
		(sg_rune_vec3_t){ { 15.25f, 15.5f, 16.75f } };
	model->identity.physics.gravity = 100.25f;
	model->identity.physics.ground_acceleration = 10.5f;
	model->identity.physics.air_acceleration = 1.25f;
	model->identity.physics.water_acceleration = 4.75f;
	model->identity.physics.hook_acceleration = 1000.0f;
	model->identity.physics.external_acceleration = 1200.5f;
	model->identity.physics.water_drag = 0.625f;
	model->identity.physics.max_velocity = 800.25f;
	model->identity.physics.frame_ms = 8U;
	model->identity.physics.substep_ms = 1U;
	model->completeness.state = SG_RUNE_COMPLETENESS_COMPLETE;
	model->completeness.reason = SG_RUNE_FAILURE_NONE;
	model->completeness.expected_cells = 2U;
	model->completeness.expected_portals = 1U;
	model->completeness.covered_cells = 2U;
	model->completeness.covered_portals = 1U;
	model->completeness.failure_record = UINT32_MAX;
	model->planes = fixture->planes;
	model->plane_count = 8U;
	model->portal_vertices = fixture->portal_vertices;
	model->portal_vertex_count = 3U;
	model->phases = fixture->phases;
	model->phase_count = 3U;
	model->phase_transitions = fixture->phase_transitions;
	model->phase_transition_count = 1U;
	model->cells = fixture->cells;
	model->cell_count = 2U;
	model->portals = fixture->portals;
	model->portal_count = 1U;
	model->surfaces = fixture->surfaces;
	model->surface_count = 1U;
	model->affordances = fixture->affordances;
	model->affordance_count = 1U;
	model->kernels = fixture->kernels;
	model->kernel_count = 1U;
	model->landmarks = fixture->landmarks;
	model->landmark_count = 1U;
	model->mechanisms = fixture->mechanisms;
	model->mechanism_count = 1U;
	fixture->evidence.version = SG_RUNE_VALIDATION_EVIDENCE_VERSION;
	fixture->evidence.verifier_identity = UINT64_C(0x9091929394959697);
	fixture->evidence.bsp_content_id = model->identity.bsp_content_id;
	fixture->evidence.source_set_identity = SOURCE_SET_ID;
	fixture->evidence.fixed_point_identity = UINT64_C(0xa0a1a2a3a4a5a6a7);
	fixture->evidence.fixed_point_rounds = 43U;
	fixture->evidence.proved_cells = 2U;
	fixture->evidence.proved_portals = 1U;
	fixture->binding.generation = UINT64_C(0xb0b1b2b3b4b5b6b7);
	fixture->binding.bsp_identity = ContentId(1U);
	fixture->binding.schema_identity = ContentId(65U);
}


static unsigned char *SG_RuneV2TestSectionEntry(unsigned char *encoded, uint32_t section)
{
	return encoded + SG_RUNE_V2_HEADER_BYTES +
		(size_t)section * SG_RUNE_V2_SECTION_ENTRY_BYTES;
}

static void SG_RuneV2TestFixChecksums(unsigned char *encoded, size_t encoded_size)
{
	uint32_t index;

	for (index = 0U; index < SG_RUNE_V2_REQUIRED_SECTION_COUNT; index++)
	{
		unsigned char *entry = SG_RuneV2TestSectionEntry(encoded, index);
		uint64_t offset = SG_RuneV2WireGetU64(entry +
			SG_RUNE_V2_SECTION_OFFSET_OFFSET);
		uint64_t bytes = SG_RuneV2WireGetU64(entry +
			SG_RUNE_V2_SECTION_BYTES_OFFSET);

		if (offset <= encoded_size && bytes <= encoded_size - (size_t)offset)
			SG_RuneV2WirePutU32(entry + SG_RUNE_V2_SECTION_CRC_OFFSET,
				SG_RuneV2WireCRC32(encoded + (size_t)offset, (size_t)bytes));
	}
	SG_RuneV2WirePutU32(encoded + SG_RUNE_V2_HEADER_PAYLOAD_CRC_OFFSET,
		SG_RuneV2WireCRC32(encoded + SG_RUNE_V2_HEADER_BYTES,
			encoded_size - SG_RUNE_V2_HEADER_BYTES));
	SG_RuneV2WirePutU32(encoded + SG_RUNE_V2_HEADER_CRC_OFFSET,
		SG_RuneV2WireHeaderCRC32(encoded));
}

#endif /* SG_RUNE_V2_FIXTURE_H */
