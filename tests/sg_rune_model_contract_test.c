#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../slipgate/sg_rune_model.h"

_Static_assert(sizeof(float) == 4, "RUNE model requires binary32 values");
_Static_assert(sizeof(sg_rune_vec3_t) == sizeof(float) * 3,
	"vectors have three scalar components");
_Static_assert(sizeof(sg_rune_stable_id_t) == sizeof(uint64_t) * 3,
	"stable IDs preserve the source-set identity and order key");
_Static_assert(offsetof(sg_rune_capability_kernel_t, transition) >
	offsetof(sg_rune_capability_kernel_t, destination_phase),
	"same-cell transitions follow the phase pair");

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

typedef struct model_fixture_s
{
	sg_rune_plane_t planes[11];
	sg_rune_vec3_t vertices[4];
	sg_rune_phase_basis_t phases[5];
	sg_rune_phase_transition_t transitions[1];
	sg_rune_cell_t cells[2];
	sg_rune_portal_t portals[1];
	sg_rune_surface_t surfaces[1];
	sg_rune_affordance_t affordances[1];
	sg_rune_capability_kernel_t kernels[1];
	sg_rune_landmark_t landmarks[1];
	sg_rune_mechanism_t mechanisms[1];
	sg_rune_model_t model;
	sg_rune_validation_evidence_t evidence;
} model_fixture_t;

static sg_rune_order_key_t Order(uint32_t domain, uint32_t ordinal)
{
	sg_rune_order_key_t key = {
		UINT64_C(0x5352435345543031), domain, 7, ordinal, 0
	};
	return key;
}

#define DEFINE_ID_HELPER(name, type, domain) \
static type name(uint32_t ordinal) \
{ \
	sg_rune_order_key_t order = Order(domain, ordinal); \
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

static sg_rune_interval_t Interval(float min_value, float max_value)
{
	sg_rune_interval_t interval = { min_value, max_value };
	return interval;
}

static sg_rune_phase_basis_t MakePhase(uint32_t ordinal,
	sg_rune_stance_t stance, sg_rune_motion_t motion,
	sg_rune_support_t support, sg_rune_medium_t medium,
	sg_rune_void_relation_t void_relation,
	sg_rune_reference_frame_t reference_frame)
{
	sg_rune_phase_basis_t phase = { 0 };

	phase.order = Order(SG_RUNE_ORDER_PHASE, ordinal);
	phase.id = PhaseId(ordinal);
	phase.stance = stance;
	phase.motion = motion;
	phase.support = support;
	phase.medium = medium;
	phase.void_relation = void_relation;
	phase.reference_frame = reference_frame;
	phase.mover = SG_RUNE_MECHANISM_REF_NONE;
	if (reference_frame == SG_RUNE_FRAME_MOVER_RELATIVE)
		phase.mover.value = MechanismId(0).value;
	phase.velocity.x = Interval(-320.0f, 320.0f);
	phase.velocity.y = Interval(-320.0f, 320.0f);
	phase.velocity.z = Interval(-800.0f, 800.0f);
	phase.elapsed_ms = Interval(0.0f, 1000.0f);
	phase.time_quantum_ms = 8;
	phase.time_horizon_ms = 2000;
	return phase;
}

static void SetPlane(sg_rune_plane_t *plane, uint32_t ordinal,
	float x, float y, float z, float distance)
{
	memset(plane, 0, sizeof(*plane));
	plane->order = Order(SG_RUNE_ORDER_PLANE, ordinal);
	plane->id = PlaneId(ordinal);
	plane->normal.value[0] = x;
	plane->normal.value[1] = y;
	plane->normal.value[2] = z;
	plane->distance = distance;
}

static void SetEvidence(const sg_rune_model_t *model,
	sg_rune_validation_evidence_t *evidence)
{
	memset(evidence, 0, sizeof(*evidence));
	evidence->version = SG_RUNE_VALIDATION_EVIDENCE_VERSION;
	evidence->verifier_identity = UINT64_C(0x50524f4f46564552);
	evidence->bsp_content_id = model->identity.bsp_content_id;
	evidence->source_set_identity = model->identity.source_set_identity;
	evidence->fixed_point_identity = UINT64_C(0x4650585441554449);
	evidence->fixed_point_rounds = 3;
	evidence->proved_cells = model->cell_count;
	evidence->proved_portals = model->portal_count;
}

static sg_rune_source_geometry_ref_t Geometry(uint32_t index,
	uint32_t ordinal)
{
	sg_rune_source_geometry_ref_t reference = {
		UINT64_C(0x5352435345543031), index, ordinal
	};
	return reference;
}

static void InitFixture(model_fixture_t *fixture)
{
	sg_rune_model_t *model;
	sg_rune_capability_kernel_t *kernel;
	sg_rune_order_key_t order;

	memset(fixture, 0, sizeof(*fixture));
	SetPlane(&fixture->planes[0], 0, 1.0f, 0.0f, 0.0f, 0.0f);
	SetPlane(&fixture->planes[1], 1, 0.0f, 1.0f, 0.0f, 0.0f);
	SetPlane(&fixture->planes[2], 2, 0.0f, 1.0f, 0.0f, 64.0f);
	SetPlane(&fixture->planes[3], 3, 0.0f, 0.0f, 1.0f, 0.0f);
	SetPlane(&fixture->planes[4], 4, 0.0f, 0.0f, 1.0f, 64.0f);
	SetPlane(&fixture->planes[5], 5, 1.0f, 0.0f, 0.0f, 64.0f);
	SetPlane(&fixture->planes[6], 6, 0.0f, 1.0f, 0.0f, 0.0f);
	SetPlane(&fixture->planes[7], 7, 0.0f, 1.0f, 0.0f, 64.0f);
	SetPlane(&fixture->planes[8], 8, 0.0f, 0.0f, 1.0f, 0.0f);
	SetPlane(&fixture->planes[9], 9, 0.0f, 0.0f, 1.0f, 64.0f);
	SetPlane(&fixture->planes[10], 10, 1.0f, 0.0f, 0.0f, 128.0f);

	fixture->vertices[0] = (sg_rune_vec3_t){ { 64.0f, 16.0f, 16.0f } };
	fixture->vertices[1] = (sg_rune_vec3_t){ { 64.0f, 48.0f, 16.0f } };
	fixture->vertices[2] = (sg_rune_vec3_t){ { 64.0f, 48.0f, 48.0f } };
	fixture->vertices[3] = (sg_rune_vec3_t){ { 64.0f, 16.0f, 48.0f } };

	fixture->phases[0] = MakePhase(0, SG_RUNE_STANCE_STANDING,
		SG_RUNE_MOTION_SUPPORTED, SG_RUNE_SUPPORT_SUPPORTED,
		SG_RUNE_MEDIUM_DRY, SG_RUNE_VOID_CLEAR, SG_RUNE_FRAME_WORLD);
	fixture->phases[1] = MakePhase(1, SG_RUNE_STANCE_CROUCHING,
		SG_RUNE_MOTION_SUPPORTED, SG_RUNE_SUPPORT_SUPPORTED,
		SG_RUNE_MEDIUM_DRY, SG_RUNE_VOID_CLEAR, SG_RUNE_FRAME_WORLD);
	fixture->phases[2] = MakePhase(2, SG_RUNE_STANCE_STANDING,
		SG_RUNE_MOTION_SWIMMING, SG_RUNE_SUPPORT_NONE,
		SG_RUNE_MEDIUM_WATER, SG_RUNE_VOID_CLEAR, SG_RUNE_FRAME_WORLD);
	fixture->phases[3] = MakePhase(3, SG_RUNE_STANCE_CROUCHING,
		SG_RUNE_MOTION_AIRBORNE, SG_RUNE_SUPPORT_NONE,
		SG_RUNE_MEDIUM_DRY, SG_RUNE_VOID_ADJACENT, SG_RUNE_FRAME_WORLD);
	fixture->phases[4] = MakePhase(4, SG_RUNE_STANCE_STANDING,
		SG_RUNE_MOTION_SUPPORTED, SG_RUNE_SUPPORT_MOVER,
		SG_RUNE_MEDIUM_DRY, SG_RUNE_VOID_CLEAR,
		SG_RUNE_FRAME_MOVER_RELATIVE);

	order = Order(SG_RUNE_ORDER_PHASE_TRANSITION, 0);
	fixture->transitions[0].id = TransitionId(0);
	fixture->transitions[0].order = order;
	fixture->transitions[0].cell = CellId(0);
	fixture->transitions[0].source_phase = fixture->phases[0].id;
	fixture->transitions[0].destination_phase = fixture->phases[1].id;
	fixture->transitions[0].kind = SG_RUNE_PHASE_TRANSITION_STANCE;
	fixture->transitions[0].duration_ms = Interval(8.0f, 250.0f);

	fixture->cells[0].id = CellId(0);
	fixture->cells[0].order = Order(SG_RUNE_ORDER_CELL, 0);
	fixture->cells[0].geometry = Geometry(0, 0);
	fixture->cells[0].bounds.mins =
		(sg_rune_vec3_t){ { 0.0f, 0.0f, 0.0f } };
	fixture->cells[0].bounds.maxs =
		(sg_rune_vec3_t){ { 64.0f, 64.0f, 64.0f } };
	fixture->cells[0].boundary_planes = (sg_rune_plane_span_t){ 0, 6 };
	fixture->cells[0].phases = (sg_rune_phase_span_t){ 0, 3 };
	fixture->cells[0].surfaces = (sg_rune_surface_span_t){ 0, 1 };
	fixture->cells[0].affordances = (sg_rune_affordance_span_t){ 0, 1 };
	fixture->cells[0].kernels = (sg_rune_kernel_span_t){ 0, 1 };
	fixture->cells[0].landmarks = (sg_rune_landmark_span_t){ 0, 1 };
	fixture->cells[0].mechanisms = (sg_rune_mechanism_span_t){ 0, 1 };
	fixture->cells[0].bsp_leaf.index = 0;
	fixture->cells[0].bsp_area.index = 0;
	fixture->cells[0].bsp_cluster.index = 0;

	fixture->cells[1].id = CellId(1);
	fixture->cells[1].order = Order(SG_RUNE_ORDER_CELL, 1);
	fixture->cells[1].geometry = Geometry(0, 1);
	fixture->cells[1].bounds.mins =
		(sg_rune_vec3_t){ { 64.0f, 0.0f, 0.0f } };
	fixture->cells[1].bounds.maxs =
		(sg_rune_vec3_t){ { 128.0f, 64.0f, 64.0f } };
	fixture->cells[1].boundary_planes = (sg_rune_plane_span_t){ 5, 6 };
	fixture->cells[1].phases = (sg_rune_phase_span_t){ 3, 2 };
	fixture->cells[1].bsp_leaf.index = 1;
	fixture->cells[1].bsp_area.index = 1;
	fixture->cells[1].bsp_cluster.index = 1;

	fixture->portals[0].id = PortalId(0);
	fixture->portals[0].order = Order(SG_RUNE_ORDER_PORTAL, 0);
	fixture->portals[0].geometry = Geometry(1, 0);
	fixture->portals[0].from_cell = fixture->cells[0].id;
	fixture->portals[0].to_cell = fixture->cells[1].id;
	fixture->portals[0].boundary_plane = fixture->planes[5].id;
	fixture->portals[0].boundary_vertices =
		(sg_rune_vertex_span_t){ 0, 4 };
	fixture->portals[0].phases = (sg_rune_phase_span_t){ 0, 3 };
	fixture->portals[0].direction = SG_RUNE_PORTAL_BIDIRECTIONAL;
	fixture->portals[0].clearance = 32.0f;
	fixture->portals[0].flags = SG_RUNE_PORTAL_HULL_VALID;

	fixture->surfaces[0].id = SurfaceId(0);
	fixture->surfaces[0].order = Order(SG_RUNE_ORDER_SURFACE, 0);
	fixture->surfaces[0].geometry = Geometry(2, 0);
	fixture->surfaces[0].owner_cell = fixture->cells[0].id;
	fixture->surfaces[0].plane = fixture->planes[5].id;
	fixture->surfaces[0].normal =
		(sg_rune_vec3_t){ { 1.0f, 0.0f, 0.0f } };
	fixture->surfaces[0].semantics = SG_RUNE_SURFACE_SEMANTIC_HOOKABLE |
		SG_RUNE_SURFACE_SEMANTIC_BOUNCE;

	fixture->affordances[0].id = AffordanceId(0);
	fixture->affordances[0].order = Order(SG_RUNE_ORDER_AFFORDANCE, 0);
	fixture->affordances[0].owner_cell = fixture->cells[0].id;
	fixture->affordances[0].surfaces = (sg_rune_surface_span_t){ 0, 1 };
	fixture->affordances[0].phases = (sg_rune_phase_span_t){ 0, 1 };
	fixture->affordances[0].kind = SG_RUNE_AFFORDANCE_HOOKABLE_REGION;
	fixture->affordances[0].range = Interval(0.0f, 8192.0f);

	fixture->mechanisms[0].id = MechanismId(0);
	fixture->mechanisms[0].order = Order(SG_RUNE_ORDER_MECHANISM, 0);
	fixture->mechanisms[0].kind = SG_RUNE_MECHANISM_DOOR;
	fixture->mechanisms[0].entry_cell = fixture->cells[0].id;
	fixture->mechanisms[0].exit_cell = fixture->cells[1].id;
	fixture->mechanisms[0].activation_landmark = SG_RUNE_LANDMARK_REF_NONE;
	fixture->mechanisms[0].entity = (sg_rune_entity_ref_t){ 1, 1 };
	fixture->mechanisms[0].dwell_ms = Interval(0.0f, 250.0f);
	fixture->mechanisms[0].travel_ms = Interval(250.0f, 750.0f);

	fixture->landmarks[0].id = LandmarkId(0);
	fixture->landmarks[0].order = Order(SG_RUNE_ORDER_LANDMARK, 0);
	fixture->landmarks[0].geometry = Geometry(3, 0);
	fixture->landmarks[0].cell = fixture->cells[0].id;
	fixture->landmarks[0].entity = (sg_rune_entity_ref_t){ 2, 1 };
	fixture->landmarks[0].kind = SG_RUNE_LANDMARK_FLAG_STAND;
	fixture->landmarks[0].origin =
		(sg_rune_vec3_t){ { 16.0f, 16.0f, 16.0f } };
	fixture->landmarks[0].bounds.mins =
		(sg_rune_vec3_t){ { 8.0f, 8.0f, 8.0f } };
	fixture->landmarks[0].bounds.maxs =
		(sg_rune_vec3_t){ { 24.0f, 24.0f, 24.0f } };
	fixture->landmarks[0].mechanism = SG_RUNE_MECHANISM_REF_NONE;
	fixture->landmarks[0].surface = fixture->surfaces[0].id;

	kernel = &fixture->kernels[0];
	kernel->id = KernelId(0);
	kernel->order = Order(SG_RUNE_ORDER_KERNEL, 0);
	kernel->source_cell = fixture->cells[0].id;
	kernel->destination_cell = fixture->cells[1].id;
	kernel->boundary = fixture->portals[0].id;
	kernel->affordance = fixture->affordances[0].id;
	kernel->mechanism = SG_RUNE_MECHANISM_REF_NONE;
	kernel->source_phase = fixture->phases[0].id;
	kernel->destination_phase = fixture->phases[3].id;
	kernel->transition = SG_RUNE_PHASE_TRANSITION_REF_NONE;
	kernel->family = SG_RUNE_CAPABILITY_HOOK_TRAJECTORY;
	kernel->cost_law = SG_RUNE_COST_TETHERED;
	kernel->parameters.displacement.x = Interval(64.0f, 64.0f);
	kernel->parameters.displacement.y = Interval(0.0f, 0.0f);
	kernel->parameters.displacement.z = Interval(0.0f, 64.0f);
	kernel->parameters.duration_ms = Interval(100.0f, 750.0f);
	kernel->parameters.speed = Interval(0.0f, 800.0f);
	kernel->parameters.acceleration = Interval(0.0f, 1000.0f);
	kernel->parameters.vertical_acceleration = Interval(0.0f, 1000.0f);
	kernel->parameters.gravity = 100.0f;
	kernel->parameters.physics_abi_id = UINT64_C(0x303);
	kernel->flags = SG_RUNE_KERNEL_DIRECTIONAL |
		SG_RUNE_KERNEL_PHASE_AWARE | SG_RUNE_KERNEL_PROVEN;

	model = &fixture->model;
	model->version = SG_RUNE_MODEL_VERSION;
	model->schema_tag = SG_RUNE_MODEL_SCHEMA_TAG;
	model->flags = SG_RUNE_MODEL_IMMUTABLE | SG_RUNE_MODEL_EXACT_BOUND |
		SG_RUNE_MODEL_NO_RUNTIME_ACTORS;
	model->identity.bsp_content_id = UINT64_C(0x101);
	model->identity.entity_semantics_id = UINT64_C(0x202);
	model->identity.physics_abi_id = UINT64_C(0x303);
	model->identity.source_set_identity = UINT64_C(0x5352435345543031);
	model->identity.schema_id = UINT64_C(0x404);
	model->identity.producer_identity = UINT64_C(0x50524f4455434552);
	model->identity.standing_hull.mins =
		(sg_rune_vec3_t){ { -16.0f, -16.0f, -24.0f } };
	model->identity.standing_hull.maxs =
		(sg_rune_vec3_t){ { 16.0f, 16.0f, 32.0f } };
	model->identity.crouching_hull.mins =
		(sg_rune_vec3_t){ { -16.0f, -16.0f, -24.0f } };
	model->identity.crouching_hull.maxs =
		(sg_rune_vec3_t){ { 16.0f, 16.0f, 16.0f } };
	model->identity.physics.gravity = 100.0f;
	model->identity.physics.ground_acceleration = 10.0f;
	model->identity.physics.air_acceleration = 1.0f;
	model->identity.physics.water_acceleration = 4.0f;
	model->identity.physics.hook_acceleration = 1000.0f;
	model->identity.physics.external_acceleration = 1200.0f;
	model->identity.physics.water_drag = 0.5f;
	model->identity.physics.max_velocity = 800.0f;
	model->identity.physics.frame_ms = 8;
	model->identity.physics.substep_ms = 1;
	model->completeness.state = SG_RUNE_COMPLETENESS_COMPLETE;
	model->completeness.reason = SG_RUNE_FAILURE_NONE;
	model->completeness.expected_cells = 2;
	model->completeness.expected_portals = 1;
	model->completeness.covered_cells = 2;
	model->completeness.covered_portals = 1;
	model->completeness.failure_record = UINT32_MAX;
	model->planes = fixture->planes;
	model->plane_count = 11;
	model->portal_vertices = fixture->vertices;
	model->portal_vertex_count = 4;
	model->phases = fixture->phases;
	model->phase_count = 5;
	model->phase_transitions = fixture->transitions;
	model->phase_transition_count = 1;
	model->cells = fixture->cells;
	model->cell_count = 2;
	model->portals = fixture->portals;
	model->portal_count = 1;
	model->surfaces = fixture->surfaces;
	model->surface_count = 1;
	model->affordances = fixture->affordances;
	model->affordance_count = 1;
	model->kernels = fixture->kernels;
	model->kernel_count = 1;
	model->landmarks = fixture->landmarks;
	model->landmark_count = 1;
	model->mechanisms = fixture->mechanisms;
	model->mechanism_count = 1;
	SetEvidence(model, &fixture->evidence);
}

static void TestStableOrdering(void)
{
	sg_rune_order_key_t first = Order(SG_RUNE_ORDER_CELL, 4);
	sg_rune_order_key_t second = Order(SG_RUNE_ORDER_CELL, 5);
	sg_rune_order_key_t decoded = { 0 };
	sg_rune_order_key_t derived = { 0 };
	sg_rune_stable_id_t first_id;
	sg_rune_stable_id_t derived_id;
	sg_rune_stable_id_t malformed;
	sg_rune_canonical_order_input_t input = {
		SG_RUNE_ORDER_CELL, 7, 4, 0, UINT64_C(0x5352435345543031), 8, 1
	};

	CHECK(SG_RuneModelOrderKeyCompare(&first, &second) < 0);
	first_id = SG_RuneModelStableIdFromOrderKey(&first);
	CHECK(SG_RuneModelStableIdValid(&first_id));
	CHECK(SG_RuneModelStableIdToOrderKey(&first_id, &decoded));
	CHECK(SG_RuneModelOrderKeyCompare(&first, &decoded) == 0);
	CHECK(SG_RuneModelOrderKeyDerive(&input, &derived) ==
		SG_RUNE_ORDER_DERIVATION_OK);
	CHECK(SG_RuneModelOrderKeyCompare(&first, &derived) == 0);
	input.source_set_identity++;
	CHECK(SG_RuneModelOrderKeyDerive(&input, &derived) ==
		SG_RUNE_ORDER_DERIVATION_OK);
	CHECK(SG_RuneModelOrderKeyCompare(&first, &derived) != 0);
	derived_id = SG_RuneModelStableIdFromOrderKey(&derived);
	CHECK(!SG_RuneModelStableIdEqual(&first_id, &derived_id));
	input.source_set_identity--;
	input.source_set_complete = 0;
	CHECK(SG_RuneModelOrderKeyDerive(&input, &derived) ==
		SG_RUNE_ORDER_DERIVATION_INCOMPLETE);
	input.source_set_complete = 1;
	input.canonical_ordinal = input.source_set_count;
	CHECK(SG_RuneModelOrderKeyDerive(&input, &derived) ==
		SG_RUNE_ORDER_DERIVATION_INCOMPLETE);
	malformed = first_id;
	malformed.high &= UINT64_C(0xffffffff);
	CHECK(!SG_RuneModelStableIdValid(&malformed));
	CHECK(!SG_RuneModelStableIdToOrderKey(&malformed, &decoded));
	CHECK(!SG_RuneModelStableIdValid(&SG_RUNE_STABLE_ID_NONE));
}

static void TestLiquidsAndCurrents(void)
{
	model_fixture_t fixture;
	sg_rune_phase_basis_t phase;
	const sg_rune_contents_mask_t currents[] = {
		SG_RUNE_CONTENTS_CURRENT_0, SG_RUNE_CONTENTS_CURRENT_90,
		SG_RUNE_CONTENTS_CURRENT_180, SG_RUNE_CONTENTS_CURRENT_270,
		SG_RUNE_CONTENTS_CURRENT_UP, SG_RUNE_CONTENTS_CURRENT_DOWN
	};
	size_t first;
	size_t second;

	phase = MakePhase(20, SG_RUNE_STANCE_STANDING,
		SG_RUNE_MOTION_SWIMMING, SG_RUNE_SUPPORT_NONE,
		SG_RUNE_MEDIUM_WATER, SG_RUNE_VOID_CLEAR, SG_RUNE_FRAME_WORLD);
	CHECK(SG_RuneModelPhaseValid(&phase));
	phase.medium = SG_RUNE_MEDIUM_SLIME;
	CHECK(SG_RuneModelPhaseValid(&phase));
	phase.medium = SG_RUNE_MEDIUM_LAVA;
	CHECK(SG_RuneModelPhaseValid(&phase));
	phase.medium = SG_RUNE_MEDIUM_DRY;
	CHECK(!SG_RuneModelPhaseValid(&phase));
	for (first = 0; first < SG_RUNE_CURRENT_DIRECTION_COUNT; first++) {
		CHECK((SG_RUNE_CONTENTS_CURRENT_MASK & currents[first]) != 0);
		for (second = first + 1; second < SG_RUNE_CURRENT_DIRECTION_COUNT;
		     second++)
			CHECK(currents[first] != currents[second]);
	}
	InitFixture(&fixture);
	fixture.cells[0].contents = SG_RUNE_CONTENTS_WATER |
		SG_RUNE_CONTENTS_CURRENT_UP | SG_RUNE_CONTENTS_CURRENT_180;
	SetEvidence(&fixture.model, &fixture.evidence);
	CHECK(SG_RuneModelValidate(&fixture.model, &fixture.evidence) == SG_RUNE_FAILURE_NONE);
	fixture.cells[0].contents |= UINT32_C(1) << 31;
	SetEvidence(&fixture.model, &fixture.evidence);
	CHECK(SG_RuneModelValidate(&fixture.model, &fixture.evidence) ==
		SG_RUNE_FAILURE_INVALID_SEMANTICS);
}

static void TestNoPvsClusterCell(void)
{
	model_fixture_t fixture;

	InitFixture(&fixture);
	fixture.cells[0].bsp_cluster = SG_RUNE_BSP_CLUSTER_REF_NONE;
	SetEvidence(&fixture.model, &fixture.evidence);
	CHECK(SG_RuneModelValidate(&fixture.model, &fixture.evidence) ==
		SG_RUNE_FAILURE_NONE);
	fixture.cells[0].bsp_leaf = SG_RUNE_BSP_LEAF_REF_NONE;
	CHECK(SG_RuneModelValidate(&fixture.model, &fixture.evidence) ==
		SG_RUNE_FAILURE_INVALID_SEMANTICS);
	fixture.cells[0].bsp_leaf.index = 0U;
	fixture.cells[0].bsp_area = SG_RUNE_BSP_AREA_REF_NONE;
	CHECK(SG_RuneModelValidate(&fixture.model, &fixture.evidence) ==
		SG_RUNE_FAILURE_INVALID_SEMANTICS);
}

static void TestSameCellTransition(void)
{
	model_fixture_t fixture;
	sg_rune_capability_kernel_t *kernel;

	InitFixture(&fixture);
	kernel = &fixture.kernels[0];
	kernel->source_cell = fixture.cells[0].id;
	kernel->destination_cell = fixture.cells[0].id;
	kernel->boundary = SG_RUNE_PORTAL_REF_NONE;
	kernel->affordance = SG_RUNE_AFFORDANCE_REF_NONE;
	kernel->source_phase = fixture.phases[0].id;
	kernel->destination_phase = fixture.phases[1].id;
	kernel->transition = fixture.transitions[0].id;
	kernel->family = SG_RUNE_CAPABILITY_CONTINUOUS_SUPPORT;
	kernel->cost_law = SG_RUNE_COST_CONSTANT_RATE;
	kernel->parameters.acceleration = Interval(0.0f, 10.0f);
	kernel->parameters.vertical_acceleration = Interval(0.0f, 10.0f);
	SetEvidence(&fixture.model, &fixture.evidence);
	CHECK(SG_RuneModelValidate(&fixture.model, &fixture.evidence) == SG_RUNE_FAILURE_NONE);
	fixture.phases[1].time_quantum_ms++;
	CHECK(SG_RuneModelValidate(&fixture.model, &fixture.evidence) ==
		SG_RUNE_FAILURE_INVALID_PHASE);
	fixture.phases[1].time_quantum_ms--;
	kernel->transition = SG_RUNE_PHASE_TRANSITION_REF_NONE;
	SetEvidence(&fixture.model, &fixture.evidence);
	CHECK(SG_RuneModelValidate(&fixture.model, &fixture.evidence) ==
		SG_RUNE_FAILURE_INVALID_REFERENCE);
}

static void TestGeometryBinding(void)
{
	model_fixture_t fixture;

	InitFixture(&fixture);
	fixture.cells[0].geometry.source_set_identity++;
	CHECK(SG_RuneModelValidate(&fixture.model, &fixture.evidence) ==
		SG_RUNE_FAILURE_INVALID_REFERENCE);

	InitFixture(&fixture);
	fixture.portals[0].geometry.source_set_identity = 0;
	CHECK(SG_RuneModelValidate(&fixture.model, &fixture.evidence) ==
		SG_RUNE_FAILURE_INVALID_REFERENCE);

	InitFixture(&fixture);
	fixture.surfaces[0].geometry.source_ordinal = UINT32_MAX;
	CHECK(SG_RuneModelValidate(&fixture.model, &fixture.evidence) ==
		SG_RUNE_FAILURE_INVALID_REFERENCE);

	InitFixture(&fixture);
	fixture.landmarks[0].geometry.source_set_identity++;
	CHECK(SG_RuneModelValidate(&fixture.model, &fixture.evidence) ==
		SG_RUNE_FAILURE_INVALID_REFERENCE);
}

static void TestCompletenessBinding(void)
{
	model_fixture_t fixture;
	sg_rune_model_t shortened;

	InitFixture(&fixture);
	CHECK(SG_RuneModelValidate(&fixture.model, &fixture.evidence) == SG_RUNE_FAILURE_NONE);
	CHECK(SG_RuneModelValidate(&fixture.model, NULL) ==
		SG_RUNE_FAILURE_INCOMPLETE);
	fixture.evidence.pending_work = 1;
	CHECK(SG_RuneModelValidate(&fixture.model, &fixture.evidence) ==
		SG_RUNE_FAILURE_INCOMPLETE);

	InitFixture(&fixture);
	shortened = fixture.model;
	shortened.cell_count = 1;
	shortened.portal_count = 0;
	shortened.completeness.expected_cells = 1;
	shortened.completeness.covered_cells = 1;
	shortened.completeness.expected_portals = 0;
	shortened.completeness.covered_portals = 0;
	CHECK(SG_RuneModelCompletenessValid(&shortened.completeness));
	CHECK(SG_RuneModelValidate(&shortened, &fixture.evidence) == SG_RUNE_FAILURE_INCOMPLETE);

	InitFixture(&fixture);
	fixture.evidence.source_set_identity++;
	CHECK(SG_RuneModelValidate(&fixture.model, &fixture.evidence) == SG_RUNE_FAILURE_INCOMPLETE);

	InitFixture(&fixture);
	fixture.evidence.verifier_identity =
		fixture.model.identity.producer_identity;
	CHECK(SG_RuneModelValidate(&fixture.model, &fixture.evidence) == SG_RUNE_FAILURE_INCOMPLETE);
}

static void TestPhysicsIdentity(void)
{
	model_fixture_t fixture;

	InitFixture(&fixture);
	fixture.kernels[0].parameters.gravity = 800.0f;
	SetEvidence(&fixture.model, &fixture.evidence);
	CHECK(SG_RuneModelValidate(&fixture.model, &fixture.evidence) ==
		SG_RUNE_FAILURE_UNSUPPORTED_PHYSICS);

	InitFixture(&fixture);
	fixture.kernels[0].parameters.physics_abi_id++;
	SetEvidence(&fixture.model, &fixture.evidence);
	CHECK(SG_RuneModelValidate(&fixture.model, &fixture.evidence) ==
		SG_RUNE_FAILURE_IDENTITY_MISMATCH);

	InitFixture(&fixture);
	fixture.model.identity.physics.gravity = 800.0f;
	fixture.kernels[0].parameters.gravity = 800.0f;
	SetEvidence(&fixture.model, &fixture.evidence);
	CHECK(SG_RuneModelValidate(&fixture.model, &fixture.evidence) == SG_RUNE_FAILURE_NONE);
}

static void TestMoverReferences(void)
{
	model_fixture_t fixture;
	sg_rune_phase_basis_t phase;

	InitFixture(&fixture);
	phase = fixture.phases[0];
	phase.mover.value = fixture.mechanisms[0].id.value;
	CHECK(!SG_RuneModelPhaseValid(&phase));
	phase = fixture.phases[4];
	phase.mover = SG_RUNE_MECHANISM_REF_NONE;
	CHECK(!SG_RuneModelPhaseValid(&phase));
	phase.mover.value = CellId(0).value;
	CHECK(!SG_RuneModelPhaseValid(&phase));

	fixture.phases[4].mover.value = MechanismId(99).value;
	SetEvidence(&fixture.model, &fixture.evidence);
	CHECK(SG_RuneModelValidate(&fixture.model, &fixture.evidence) ==
		SG_RUNE_FAILURE_INVALID_REFERENCE);

	InitFixture(&fixture);
	fixture.cells[1].phases.count = 1;
	SetEvidence(&fixture.model, &fixture.evidence);
	CHECK(SG_RuneModelValidate(&fixture.model, &fixture.evidence) ==
		SG_RUNE_FAILURE_INVALID_REFERENCE);
}

static void TestDirectionalAndMediumKernels(void)
{
	model_fixture_t fixture;
	sg_rune_capability_kernel_t *kernel;

	InitFixture(&fixture);
	kernel = &fixture.kernels[0];
	fixture.portals[0].direction = SG_RUNE_PORTAL_FROM_TO;
	kernel->source_cell = fixture.cells[1].id;
	kernel->destination_cell = fixture.cells[0].id;
	kernel->source_phase = fixture.phases[3].id;
	kernel->destination_phase = fixture.phases[0].id;
	kernel->affordance = SG_RUNE_AFFORDANCE_REF_NONE;
	fixture.cells[0].kernels.count = 0;
	fixture.cells[1].kernels = (sg_rune_kernel_span_t){ 0, 1 };
	CHECK(SG_RuneModelValidate(&fixture.model, &fixture.evidence) ==
		SG_RUNE_FAILURE_INVALID_REFERENCE);
	fixture.portals[0].direction = SG_RUNE_PORTAL_TO_FROM;
	CHECK(SG_RuneModelValidate(&fixture.model, &fixture.evidence) ==
		SG_RUNE_FAILURE_NONE);

	InitFixture(&fixture);
	kernel = &fixture.kernels[0];
	fixture.portals[0].direction = SG_RUNE_PORTAL_TO_FROM;
	fixture.portals[0].flags |= SG_RUNE_PORTAL_CONTENTS_CHANGE;
	fixture.portals[0].contents_from = SG_RUNE_CONTENTS_WATER;
	fixture.portals[0].contents_to = SG_RUNE_CONTENTS_EMPTY;
	kernel->source_cell = fixture.cells[1].id;
	kernel->destination_cell = fixture.cells[0].id;
	kernel->source_phase = fixture.phases[3].id;
	kernel->destination_phase = fixture.phases[2].id;
	kernel->affordance = SG_RUNE_AFFORDANCE_REF_NONE;
	kernel->parameters.drag = fixture.model.identity.physics.water_drag;
	fixture.cells[0].kernels.count = 0;
	fixture.cells[1].kernels = (sg_rune_kernel_span_t){ 0, 1 };
	CHECK(SG_RuneModelValidate(&fixture.model, &fixture.evidence) ==
		SG_RUNE_FAILURE_INVALID_PHASE);
	kernel->flags |= SG_RUNE_KERNEL_CHANGES_MEDIUM;
	CHECK(SG_RuneModelValidate(&fixture.model, &fixture.evidence) ==
		SG_RUNE_FAILURE_NONE);
}

static void TestAuthoritativeAcceleration(void)
{
	model_fixture_t fixture;

	InitFixture(&fixture);
	fixture.model.identity.physics.hook_acceleration = 999.0f;
	CHECK(SG_RuneModelValidate(&fixture.model, &fixture.evidence) ==
		SG_RUNE_FAILURE_UNSUPPORTED_PHYSICS);
	fixture.model.identity.physics.hook_acceleration = 1000.0f;
	CHECK(SG_RuneModelValidate(&fixture.model, &fixture.evidence) ==
		SG_RUNE_FAILURE_NONE);
}

static void TestCellOwnershipSpans(void)
{
	model_fixture_t fixture;

	InitFixture(&fixture);
	fixture.surfaces[0].owner_cell = fixture.cells[1].id;
	fixture.landmarks[0].surface = SG_RUNE_SURFACE_REF_NONE;
	CHECK(SG_RuneModelValidate(&fixture.model, &fixture.evidence) ==
		SG_RUNE_FAILURE_INVALID_REFERENCE);
	InitFixture(&fixture);
	fixture.cells[0].surfaces.count = 0;
	CHECK(SG_RuneModelValidate(&fixture.model, &fixture.evidence) ==
		SG_RUNE_FAILURE_INVALID_REFERENCE);

	InitFixture(&fixture);
	fixture.kernels[0].source_cell = fixture.cells[1].id;
	fixture.kernels[0].destination_cell = fixture.cells[0].id;
	fixture.kernels[0].source_phase = fixture.phases[3].id;
	fixture.kernels[0].destination_phase = fixture.phases[0].id;
	fixture.kernels[0].affordance = SG_RUNE_AFFORDANCE_REF_NONE;
	CHECK(SG_RuneModelValidate(&fixture.model, &fixture.evidence) ==
		SG_RUNE_FAILURE_INVALID_REFERENCE);
}

static void TestNonquadraticLookups(void)
{
	enum { CELL_COUNT = 4096 };
	model_fixture_t fixture;
	sg_rune_cell_t *cells;
	sg_rune_model_t model;
	sg_rune_portal_t portal;
	uint32_t index;

	InitFixture(&fixture);
	cells = calloc(CELL_COUNT, sizeof(*cells));
	CHECK(cells != NULL);
	if (!cells)
		return;
	for (index = 0; index < CELL_COUNT; index++) {
		cells[index].id = CellId(index);
		cells[index].order = Order(SG_RUNE_ORDER_CELL, index);
		cells[index].geometry = Geometry(0, index);
		cells[index].bounds = fixture.cells[0].bounds;
		cells[index].boundary_planes = (sg_rune_plane_span_t){ 0, 6 };
		cells[index].phases = (sg_rune_phase_span_t){ 0, 1 };
		cells[index].bsp_leaf.index = index;
		cells[index].bsp_area.index = index;
		cells[index].bsp_cluster.index = index;
	}
	portal = fixture.portals[0];
	portal.from_cell = cells[0].id;
	portal.to_cell = cells[CELL_COUNT - 1].id;
	portal.phases = (sg_rune_phase_span_t){ 0, 1 };
	model = fixture.model;
	model.planes = fixture.planes;
	model.plane_count = 6;
	model.phases = fixture.phases;
	model.phase_count = 1;
	model.phase_transitions = NULL;
	model.phase_transition_count = 0;
	model.cells = cells;
	model.cell_count = CELL_COUNT;
	model.portals = &portal;
	model.portal_count = 1;
	model.surfaces = NULL;
	model.surface_count = 0;
	model.affordances = NULL;
	model.affordance_count = 0;
	model.kernels = NULL;
	model.kernel_count = 0;
	model.landmarks = NULL;
	model.landmark_count = 0;
	model.mechanisms = NULL;
	model.mechanism_count = 0;
	model.completeness.expected_cells = CELL_COUNT;
	model.completeness.covered_cells = CELL_COUNT;
	model.completeness.expected_portals = 1;
	model.completeness.covered_portals = 1;
	SetEvidence(&model, &fixture.evidence);
	CHECK(SG_RuneModelValidate(&model, &fixture.evidence) == SG_RUNE_FAILURE_NONE);
	CHECK(SG_RuneModelLastLookupComparisons() < 128);
	free(cells);
}

static void TestFailureState(void)
{
	model_fixture_t fixture;
	sg_rune_model_t invalid;

	InitFixture(&fixture);
	invalid = fixture.model;
	invalid.completeness.state = SG_RUNE_COMPLETENESS_FAILED;
	invalid.completeness.reason = SG_RUNE_FAILURE_MISSING_CONFIGURATION;
	invalid.completeness.failure_record = 1;
	CHECK(SG_RuneModelValidate(&invalid, NULL) ==
		SG_RUNE_FAILURE_MISSING_CONFIGURATION);
}

int main(void)
{
	TestStableOrdering();
	TestLiquidsAndCurrents();
	TestNoPvsClusterCell();
	TestSameCellTransition();
	TestGeometryBinding();
	TestCompletenessBinding();
	TestPhysicsIdentity();
	TestMoverReferences();
	TestDirectionalAndMediumKernels();
	TestAuthoritativeAcceleration();
	TestCellOwnershipSpans();
	TestNonquadraticLookups();
	TestFailureState();
	if (failures != 0)
		return 1;
	puts("sg_rune_model_contract_test: ok");
	return 0;
}
