#include <math.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../slipgate/sg_cell_phase_localization.h"

#define MAX_CELLS 64U
#define MAX_REGIONS 64U
#define MAX_PHASES 70U
#define MAX_PORTALS 64U
#define MAX_PHASE_TRANSITIONS 4U
#define MAX_RUNTIME_PORTALS 4U
#define MAX_KERNELS 4U
#define MAX_FACES (MAX_REGIONS * 6U)

static int failures;

#ifdef SG_LOCALIZATION_REAL_PMOVE_TEST
extern void Pmove(pmove_t *pmove);
void Com_DPrintf(const char *format, ...);
void Com_DPrintf(const char *format, ...)
{
	(void)format;
}
void Com_Printf(char *format, ...);
void Com_Printf(char *format, ...)
{
	(void)format;
}
#endif

static void LocalizationPmove(pmove_t *pmove)
{
	vec3_t origin, destination, down;
	trace_t trace;
	uint32_t axis;
	int ducked = (pmove->s.pm_flags & PMF_DUCKED) != 0;

	if (pmove->cmd.upmove < 0)
		pmove->s.pm_flags |= PMF_DUCKED;
	else if (pmove->cmd.upmove > 0)
		pmove->s.pm_flags &= (byte)~PMF_DUCKED;
	ducked = (pmove->s.pm_flags & PMF_DUCKED) != 0;
	pmove->mins[0] = -16.0f;
	pmove->mins[1] = -16.0f;
	pmove->mins[2] = -24.0f;
	pmove->maxs[0] = 16.0f;
	pmove->maxs[1] = 16.0f;
	pmove->maxs[2] = ducked ? 4.0f : 32.0f;
	for (axis = 0U; axis < 3U; axis++)
		origin[axis] = pmove->s.origin[axis] * 0.125f;
	pmove->s.velocity[0] = pmove->cmd.forwardmove;
	pmove->s.velocity[1] = pmove->cmd.sidemove;
	pmove->s.velocity[2] = 0;
	for (axis = 0U; axis < 3U; axis++)
	{
		short packed = (short)(pmove->s.origin[axis] +
			(short)((pmove->s.velocity[axis] * pmove->cmd.msec) / 1000));

		destination[axis] = packed * 0.125f;
	}
	trace = pmove->trace(origin, pmove->mins, pmove->maxs, destination);
	for (axis = 0U; axis < 3U; axis++)
		pmove->s.origin[axis] = (short)(trace.endpos[axis] * 8.0f);
	VectorCopy(trace.endpos, down);
	down[2] -= 0.25f;
	trace = pmove->trace(trace.endpos, pmove->mins, pmove->maxs, down);
	pmove->groundentity = trace.ent;
	pmove->watertype = pmove->pointcontents(trace.endpos);
	pmove->waterlevel = (pmove->watertype & MASK_WATER) ? 1 : 0;
}

static void TeleportPmove(pmove_t *pmove)
{
	vec3_t origin;
	trace_t trace;
	uint32_t axis;

	pmove->mins[0] = pmove->mins[1] = -16.0f;
	pmove->mins[2] = -24.0f;
	pmove->maxs[0] = pmove->maxs[1] = 16.0f;
	pmove->maxs[2] = 32.0f;
	for (axis = 0U; axis < 3U; axis++)
		origin[axis] = pmove->s.origin[axis] * 0.125f;
	trace = pmove->trace(origin, pmove->mins, pmove->maxs, origin);
	(void)trace;
	pmove->s.origin[0] = 240;
}

static void PointTraceTeleportPmove(pmove_t *pmove)
{
	vec3_t origin, destination, point_hull = { 0.0f, 0.0f, 0.0f };
	trace_t trace;
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
	{
		origin[axis] = pmove->s.origin[axis] * 0.125f;
		destination[axis] = origin[axis];
	}
	destination[0] = 30.0f;
	trace = pmove->trace(origin, point_hull, point_hull, destination);
	pmove->mins[0] = pmove->mins[1] = -16.0f;
	pmove->mins[2] = -24.0f;
	pmove->maxs[0] = pmove->maxs[1] = 16.0f;
	pmove->maxs[2] = 32.0f;
	for (axis = 0U; axis < 3U; axis++)
		pmove->s.origin[axis] = (short)(trace.endpos[axis] * 8.0f);
}

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

typedef struct world_fixture_s
{
	sg_bsp_world_t world;
	sg_bsp_plane_t planes[12];
	sg_bsp_node_t nodes[12];
	sg_bsp_leaf_t leaves[14];
	uint32_t leaf_brushes[2];
	sg_bsp_model_t models[2];
	sg_bsp_brush_t brushes[2];
	sg_bsp_brush_side_t sides[12];
	sg_bsp_texinfo_t texinfos[2];
} world_fixture_t;

typedef struct locator_fixture_s
{
	sg_rune_model_identity_t identity;
	sg_host_collision_authority_t authority;
	sg_configuration_space_t configuration;
	sg_configuration_cell_t configuration_cells[MAX_CELLS];
	sg_configuration_face_t configuration_faces[MAX_CELLS * 6U];
	sg_configuration_portal_t portals[MAX_PORTALS];
	sg_rune_vec3_t portal_vertices[MAX_PORTALS * 4U];
	sg_configuration_stance_overlap_t stance_overlaps[4];
	sg_configuration_certificate_node_t certificates[8];
	sg_configuration_semantics_t semantics;
	sg_configuration_semantic_region_t regions[MAX_REGIONS];
	sg_configuration_semantic_face_t semantic_faces[MAX_FACES];
	sg_rune_model_t model;
	sg_rune_cell_t runtime_cells[MAX_REGIONS];
	sg_rune_phase_basis_t phases[MAX_PHASES];
	sg_rune_phase_transition_t phase_transitions[MAX_PHASE_TRANSITIONS];
	sg_rune_portal_t runtime_portals[MAX_RUNTIME_PORTALS];
	sg_rune_capability_kernel_t kernels[MAX_KERNELS];
	sg_rune_mechanism_t mechanisms[1];
	sg_phase_coordinate_t coordinates[MAX_PHASES];
	sg_rune_runtime_snapshot_t snapshot;
	sg_localization_region_binding_t bindings[MAX_REGIONS];
	uint32_t cell_region_offsets[MAX_CELLS + 1U];
	uint32_t region_indices[MAX_REGIONS];
	uint32_t region_runtime_cells[MAX_REGIONS];
	uint32_t region_runtime_regions[MAX_REGIONS];
	uint32_t cell_portal_offsets[MAX_CELLS + 1U];
	uint32_t cell_portal_cursors[MAX_CELLS];
	uint32_t portal_indices[MAX_PORTALS * 2U];
	uint32_t stance_overlap_offsets[MAX_CELLS + 1U];
	uint32_t stance_overlap_cursors[MAX_CELLS];
	uint32_t stance_overlap_indices[8];
	uint32_t phase_transition_offsets[MAX_PHASES + 1U];
	uint32_t phase_transition_cursors[MAX_PHASES];
	uint32_t phase_transition_indices[MAX_PHASE_TRANSITIONS];
	uint32_t phase_kernel_offsets[MAX_PHASES + 1U];
	uint32_t phase_kernel_cursors[MAX_PHASES];
	uint32_t phase_kernel_indices[MAX_KERNELS];
	sg_localization_workspace_t workspace;
	sg_cell_phase_locator_t locator;
	sg_cell_phase_runtime_t runtime;
	sg_host_pmove_substep_t replay_substeps[128];
	sg_host_pmove_trace_t replay_traces[4096];
	uint32_t phase_count;
} locator_fixture_t;

static void Set3(float value[3], float x, float y, float z)
{
	value[0] = x;
	value[1] = y;
	value[2] = z;
}

static void SetRune3(sg_rune_vec3_t *value, float x, float y, float z)
{
	Set3(value->value, x, y, z);
}

static void SetBsp3(sg_bsp_vec3_t *value, float x, float y, float z)
{
	Set3(value->value, x, y, z);
}

static void SetBspPlane(sg_bsp_plane_t *plane, float x, float y, float z,
	float distance)
{
	SetBsp3(&plane->normal, x, y, z);
	plane->distance = distance;
	plane->type = x == 1.0f ? 0 : (y == 1.0f ? 1 : 2);
}

static void BindWorldArrays(world_fixture_t *fixture)
{
	fixture->world.planes = fixture->planes;
	fixture->world.nodes = fixture->nodes;
	fixture->world.leaves = fixture->leaves;
	fixture->world.leaf_brushes = fixture->leaf_brushes;
	fixture->world.models = fixture->models;
	fixture->world.brushes = fixture->brushes;
	fixture->world.brush_sides = fixture->sides;
	fixture->world.texinfos = fixture->texinfos;
}

static world_fixture_t EmptyWorld(void)
{
	world_fixture_t fixture;

	memset(&fixture, 0, sizeof(fixture));
	SetBspPlane(&fixture.planes[0], 1.0f, 0.0f, 0.0f, 0.0f);
	fixture.nodes[0].plane = 0U;
	fixture.nodes[0].children[0] = -1;
	fixture.nodes[0].children[1] = -2;
	fixture.leaves[0].cluster = 0;
	fixture.leaves[0].area = 1;
	fixture.leaves[1].cluster = 1;
	fixture.leaves[1].area = 1;
	fixture.models[0].headnode = 0;
	SetBsp3(&fixture.models[0].mins, -4096.0f, -4096.0f, -4096.0f);
	SetBsp3(&fixture.models[0].maxs, 4095.0f, 4095.0f, 4095.0f);
	fixture.world.plane_count = 1U;
	fixture.world.node_count = 1U;
	fixture.world.leaf_count = 2U;
	fixture.world.model_count = 1U;
	BindWorldArrays(&fixture);
	return fixture;
}

static void AddBox(world_fixture_t *fixture, uint32_t model,
	uint32_t first_plane, uint32_t first_node, uint32_t first_leaf,
	uint32_t brush, const float mins[3], const float maxs[3])
{
	uint32_t side;
	uint32_t inside_leaf = first_leaf + 6U;

	SetBspPlane(&fixture->planes[first_plane], 1, 0, 0, maxs[0]);
	SetBspPlane(&fixture->planes[first_plane + 1U], -1, 0, 0, -mins[0]);
	SetBspPlane(&fixture->planes[first_plane + 2U], 0, 1, 0, maxs[1]);
	SetBspPlane(&fixture->planes[first_plane + 3U], 0, -1, 0, -mins[1]);
	SetBspPlane(&fixture->planes[first_plane + 4U], 0, 0, 1, maxs[2]);
	SetBspPlane(&fixture->planes[first_plane + 5U], 0, 0, -1, -mins[2]);
	for (side = 0U; side < 6U; side++)
	{
		sg_bsp_node_t *node = &fixture->nodes[first_node + side];

		node->plane = first_plane + side;
		node->children[0] = -1 - (int32_t)(first_leaf + side);
		node->children[1] = side == 5U ? -1 - (int32_t)inside_leaf :
			(int32_t)(first_node + side + 1U);
		fixture->sides[brush * 6U + side].plane = first_plane + side;
		fixture->sides[brush * 6U + side].texinfo = (int32_t)brush;
	}
	fixture->leaves[inside_leaf].contents = SG_HOST_CONTENTS_SOLID;
	fixture->leaves[inside_leaf].first_leaf_brush = brush;
	fixture->leaves[inside_leaf].leaf_brush_count = 1U;
	fixture->leaf_brushes[brush] = brush;
	fixture->brushes[brush].first_side = brush * 6U;
	fixture->brushes[brush].side_count = 6U;
	fixture->brushes[brush].contents = SG_HOST_CONTENTS_SOLID;
	fixture->models[model].headnode = (int32_t)first_node;
	SetBsp3(&fixture->models[model].mins,
		mins[0] - 1.0f, mins[1] - 1.0f, mins[2] - 1.0f);
	SetBsp3(&fixture->models[model].maxs,
		maxs[0] + 1.0f, maxs[1] + 1.0f, maxs[2] + 1.0f);
}

static world_fixture_t BoxWorld(const float mins[3], const float maxs[3])
{
	world_fixture_t fixture;

	memset(&fixture, 0, sizeof(fixture));
	AddBox(&fixture, 0U, 0U, 0U, 0U, 0U, mins, maxs);
	fixture.world.plane_count = 6U;
	fixture.world.node_count = 6U;
	fixture.world.leaf_count = 7U;
	fixture.world.leaf_brush_count = 1U;
	fixture.world.model_count = 1U;
	fixture.world.brush_count = 1U;
	fixture.world.brush_side_count = 6U;
	fixture.world.texinfo_count = 1U;
	BindWorldArrays(&fixture);
	return fixture;
}

static world_fixture_t ShallowWaterFloorWorld(void)
{
	const float floor_mins[3] = { -100.0f, -100.0f, -100.0f };
	const float floor_maxs[3] = { 100.0f, 100.0f, 0.0f };
	world_fixture_t fixture = BoxWorld(floor_mins, floor_maxs);

	SetBspPlane(&fixture.planes[6], 0.0f, 0.0f, 1.0f, 10.0f);
	fixture.nodes[4].children[0] = 6;
	fixture.nodes[6].plane = 6U;
	fixture.nodes[6].children[0] = -8;
	fixture.nodes[6].children[1] = -5;
	fixture.leaves[4].contents = SG_HOST_CONTENTS_WATER;
	fixture.world.plane_count = 7U;
	fixture.world.node_count = 7U;
	fixture.world.leaf_count = 8U;
	BindWorldArrays(&fixture);
	return fixture;
}

#ifdef SG_LOCALIZATION_REAL_PMOVE_TEST
static world_fixture_t StepWorld(void)
{
	const float floor_mins[3] = { -100.0f, -100.0f, -100.0f };
	const float floor_maxs[3] = { 100.0f, 100.0f, 0.0f };
	const float step_mins[3] = { 0.0f, -100.0f, 0.0f };
	const float step_maxs[3] = { 100.0f, 100.0f, 16.0f };
	world_fixture_t fixture;
	uint32_t leaf;

	memset(&fixture, 0, sizeof(fixture));
	AddBox(&fixture, 0U, 0U, 0U, 0U, 0U, floor_mins, floor_maxs);
	AddBox(&fixture, 1U, 6U, 6U, 7U, 1U, step_mins, step_maxs);
	fixture.nodes[0].children[0] = -1;
	fixture.nodes[0].children[1] = -2;
	fixture.leaf_brushes[0] = 0U;
	fixture.leaf_brushes[1] = 1U;
	for (leaf = 0U; leaf < 2U; leaf++)
	{
		fixture.leaves[leaf].contents = SG_HOST_CONTENTS_SOLID;
		fixture.leaves[leaf].first_leaf_brush = 0U;
		fixture.leaves[leaf].leaf_brush_count = 2U;
	}
	fixture.models[0].headnode = 0;
	SetBsp3(&fixture.models[0].mins, -101.0f, -101.0f, -101.0f);
	SetBsp3(&fixture.models[0].maxs, 101.0f, 101.0f, 17.0f);
	fixture.world.plane_count = 12U;
	fixture.world.node_count = 1U;
	fixture.world.leaf_count = 2U;
	fixture.world.leaf_brush_count = 2U;
	fixture.world.model_count = 1U;
	fixture.world.brush_count = 2U;
	fixture.world.brush_side_count = 12U;
	fixture.world.texinfo_count = 2U;
	BindWorldArrays(&fixture);
	return fixture;
}
#endif

static world_fixture_t WaterWorld(void)
{
	world_fixture_t fixture = EmptyWorld();

	SetBspPlane(&fixture.planes[0], 0.0f, 0.0f, 1.0f, 0.0f);
	fixture.leaves[1].contents = SG_HOST_CONTENTS_WATER |
		SG_HOST_CONTENTS_CURRENT_UP;
	return fixture;
}

static world_fixture_t MoverWorld(void)
{
	const float remote_mins[3] = { 1000.0f, 1000.0f, 1000.0f };
	const float remote_maxs[3] = { 1010.0f, 1010.0f, 1010.0f };
	const float mover_mins[3] = { -32.0f, -32.0f, -8.0f };
	const float mover_maxs[3] = { 32.0f, 32.0f, 0.0f };
	world_fixture_t fixture;

	memset(&fixture, 0, sizeof(fixture));
	AddBox(&fixture, 0U, 0U, 0U, 0U, 0U, remote_mins, remote_maxs);
	AddBox(&fixture, 1U, 6U, 6U, 7U, 1U, mover_mins, mover_maxs);
	fixture.world.plane_count = 12U;
	fixture.world.node_count = 12U;
	fixture.world.leaf_count = 14U;
	fixture.world.leaf_brush_count = 2U;
	fixture.world.model_count = 2U;
	fixture.world.brush_count = 2U;
	fixture.world.brush_side_count = 12U;
	fixture.world.texinfo_count = 2U;
	BindWorldArrays(&fixture);
	return fixture;
}

static sg_rune_model_identity_t Identity(void)
{
	sg_rune_model_identity_t identity;

	memset(&identity, 0, sizeof(identity));
	identity.bsp_content_id = UINT64_C(0x101);
	identity.entity_semantics_id = UINT64_C(0x102);
	identity.physics_abi_id = UINT64_C(0x103);
	identity.source_set_identity = UINT64_C(0x104);
	identity.schema_id = UINT64_C(0x105);
	identity.producer_identity = UINT64_C(0x106);
	SetRune3(&identity.standing_hull.mins, -16.0f, -16.0f, -24.0f);
	SetRune3(&identity.standing_hull.maxs, 16.0f, 16.0f, 32.0f);
	SetRune3(&identity.crouching_hull.mins, -16.0f, -16.0f, -24.0f);
	SetRune3(&identity.crouching_hull.maxs, 16.0f, 16.0f, 4.0f);
	identity.physics.gravity = 800.0f;
	identity.physics.ground_acceleration = 10.0f;
	identity.physics.air_acceleration = 1.0f;
	identity.physics.water_acceleration = 10.0f;
	identity.physics.hook_acceleration = 800.0f;
	identity.physics.external_acceleration = 1.0f;
	identity.physics.water_drag = 1.0f;
	identity.physics.max_velocity = 2000.0f;
	identity.physics.frame_ms = 1U;
	identity.physics.substep_ms = 1U;
	return identity;
}

static sg_rune_order_key_t Order(uint32_t domain, uint32_t ordinal)
{
	return (sg_rune_order_key_t){ UINT64_C(0x104), domain, 1U, ordinal, 0U };
}

static void SetConfigurationBox(locator_fixture_t *fixture, uint32_t cell,
	sg_rune_stance_t stance, float minimum_x, float maximum_x)
{
	static const float normals[6][3] = {
		{ -1, 0, 0 }, { 1, 0, 0 }, { 0, -1, 0 },
		{ 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 }
	};
	const float distances[6] = {
		-minimum_x, maximum_x, 128.0f, 128.0f, 128.0f, 128.0f
	};
	uint32_t face;
	sg_configuration_cell_t *record = &fixture->configuration_cells[cell];

	memset(record, 0, sizeof(*record));
	record->stance = stance;
	record->first_face = cell * 6U;
	record->face_count = 6U;
	SetRune3(&record->bounds.mins, minimum_x, -128.0f, -128.0f);
	SetRune3(&record->bounds.maxs, maximum_x, 128.0f, 128.0f);
	SetRune3(&record->interior_witness,
		(minimum_x + maximum_x) * 0.5f, 0.0f, 0.0f);
	for (face = 0U; face < 6U; face++)
	{
		sg_configuration_face_t *output =
			&fixture->configuration_faces[cell * 6U + face];

		Set3(output->plane.normal, normals[face][0], normals[face][1],
			normals[face][2]);
		output->plane.distance = distances[face];
		output->plane.source_kind = SG_CONFIGURATION_PLANE_BSP;
	}
}

static void SetSemanticBox(locator_fixture_t *fixture, uint32_t region,
	uint32_t cell, float minimum_x, float maximum_x,
	sg_configuration_semantic_region_flags_t flags, uint8_t water_level,
	sg_host_collision_contents_t water_type)
{
	static const float normals[6][3] = {
		{ -1, 0, 0 }, { 1, 0, 0 }, { 0, -1, 0 },
		{ 0, 1, 0 }, { 0, 0, -1 }, { 0, 0, 1 }
	};
	const float distances[6] = {
		-minimum_x, maximum_x, 128.0f, 128.0f, 128.0f, 128.0f
	};
	uint32_t face;
	sg_configuration_semantic_region_t *record = &fixture->regions[region];

	memset(record, 0, sizeof(*record));
	record->id = (uint64_t)region + 1U;
	record->cell = cell;
	record->first_face = region * 6U;
	record->face_count = 6U;
	SetRune3(&record->bounds.mins, minimum_x, -128.0f, -128.0f);
	SetRune3(&record->bounds.maxs, maximum_x, 128.0f, 128.0f);
	record->flags = flags;
	record->water_level = water_level;
	record->water_type = water_type;
	for (face = 0U; face < 6U; face++)
	{
		sg_configuration_semantic_face_t *output =
			&fixture->semantic_faces[region * 6U + face];

		Set3(output->normal, normals[face][0], normals[face][1],
			normals[face][2]);
		output->distance = distances[face];
	}
}

static void SetRuntimeCell(locator_fixture_t *fixture, uint32_t cell,
	uint32_t first_phase, uint32_t phase_count)
{
	sg_rune_cell_t *record = &fixture->runtime_cells[cell];

	memset(record, 0, sizeof(*record));
	record->order = Order(SG_RUNE_ORDER_CELL, cell);
	record->id.value = SG_RuneModelStableIdFromOrderKey(&record->order);
	record->phases.first = first_phase;
	record->phases.count = phase_count;
	fixture->bindings[cell].semantic_region_id = fixture->regions[cell].id;
	fixture->bindings[cell].rune_cell = record->id;
	fixture->bindings[cell].runtime_region = cell;
}

static void SetPhase(locator_fixture_t *fixture, uint32_t phase,
	uint32_t runtime_cell, sg_rune_stance_t stance, sg_rune_motion_t motion,
	sg_rune_support_t support, sg_rune_medium_t medium,
	sg_rune_reference_frame_t reference_frame,
	sg_rune_mechanism_ref_t mover, float elapsed_min, float elapsed_max)
{
	sg_rune_phase_basis_t *record = &fixture->phases[phase];

	memset(record, 0, sizeof(*record));
	record->order = Order(SG_RUNE_ORDER_PHASE, phase);
	record->id.value = SG_RuneModelStableIdFromOrderKey(&record->order);
	record->stance = stance;
	record->motion = motion;
	record->support = support;
	record->medium = medium;
	record->void_relation = SG_RUNE_VOID_CLEAR;
	record->reference_frame = reference_frame;
	record->mover = mover;
	record->velocity.x = (sg_rune_interval_t){ -1000.0f, 1000.0f };
	record->velocity.y = (sg_rune_interval_t){ -1000.0f, 1000.0f };
	record->velocity.z = (sg_rune_interval_t){ -1000.0f, 1000.0f };
	record->elapsed_ms = (sg_rune_interval_t){ elapsed_min, elapsed_max };
	record->time_quantum_ms = 10U;
	record->time_horizon_ms = 2000U;
	fixture->coordinates[phase] = (sg_phase_coordinate_t){ phase, runtime_cell };
	if (fixture->phase_count <= phase)
		fixture->phase_count = phase + 1U;
}

static void InitStandardFixture(locator_fixture_t *fixture,
	world_fixture_t *world, sg_configuration_semantic_region_flags_t flags,
	uint8_t water_level, sg_host_collision_contents_t water_type,
	sg_rune_motion_t motion, sg_rune_support_t support,
	sg_rune_medium_t medium)
{
	sg_host_collision_error_t host_error;

	memset(fixture, 0, sizeof(*fixture));
	fixture->identity = Identity();
	BindWorldArrays(world);
	CHECK(SG_HostCollisionInit(&fixture->authority, &world->world,
		&fixture->identity, &host_error));
	fixture->configuration.identity = fixture->identity;
	fixture->configuration.cells = fixture->configuration_cells;
	fixture->configuration.cell_count = 2U;
	fixture->configuration.faces = fixture->configuration_faces;
	fixture->configuration.face_count = 12U;
	fixture->configuration.certificate_nodes = fixture->certificates;
	fixture->configuration.certificate_node_count = 2U;
	fixture->configuration.certificate_roots[SG_RUNE_STANCE_STANDING] = 0U;
	fixture->configuration.certificate_roots[SG_RUNE_STANCE_CROUCHING] = 1U;
	SetConfigurationBox(fixture, 0U, SG_RUNE_STANCE_STANDING, -128, 128);
	SetConfigurationBox(fixture, 1U, SG_RUNE_STANCE_CROUCHING, -128, 128);
	fixture->certificates[0].kind = SG_CONFIGURATION_CERTIFICATE_VALID;
	fixture->certificates[0].cell = 0U;
	fixture->certificates[0].stance = SG_RUNE_STANCE_STANDING;
	fixture->certificates[1].kind = SG_CONFIGURATION_CERTIFICATE_VALID;
	fixture->certificates[1].cell = 1U;
	fixture->certificates[1].stance = SG_RUNE_STANCE_CROUCHING;
	fixture->semantics.identity = fixture->identity;
	fixture->semantics.regions = fixture->regions;
	fixture->semantics.region_count = 2U;
	fixture->semantics.faces = fixture->semantic_faces;
	fixture->semantics.face_count = 12U;
	SetSemanticBox(fixture, 0U, 0U, -128, 128, flags,
		water_level, water_type);
	SetSemanticBox(fixture, 1U, 1U, -128, 128, flags,
		water_level, water_type);
	SetRuntimeCell(fixture, 0U, 0U, 1U);
	SetRuntimeCell(fixture, 1U, 1U, 1U);
	SetPhase(fixture, 0U, 0U, SG_RUNE_STANCE_STANDING, motion, support,
		medium, SG_RUNE_FRAME_WORLD, SG_RUNE_MECHANISM_REF_NONE, 0, 1000);
	SetPhase(fixture, 1U, 1U, SG_RUNE_STANCE_CROUCHING, motion, support,
		medium, SG_RUNE_FRAME_WORLD, SG_RUNE_MECHANISM_REF_NONE, 0, 1000);
}

static void SetFixtureTiming(locator_fixture_t *fixture, uint32_t frame_ms,
	uint32_t substep_ms)
{
	fixture->identity.physics.frame_ms = frame_ms;
	fixture->identity.physics.substep_ms = substep_ms;
	fixture->authority.identity = fixture->identity;
	fixture->configuration.identity = fixture->identity;
	fixture->semantics.identity = fixture->identity;
}

static void FinalizeFixture(locator_fixture_t *fixture)
{
	sg_localization_status_t status;

	fixture->model.version = SG_RUNE_MODEL_VERSION;
	fixture->model.schema_tag = SG_RUNE_MODEL_SCHEMA_TAG;
	fixture->model.flags = SG_RUNE_MODEL_IMMUTABLE | SG_RUNE_MODEL_EXACT_BOUND |
		SG_RUNE_MODEL_NO_RUNTIME_ACTORS;
	fixture->model.identity = fixture->identity;
	fixture->model.completeness.state = SG_RUNE_COMPLETENESS_COMPLETE;
	fixture->model.completeness.expected_cells =
		fixture->semantics.region_count;
	fixture->model.completeness.covered_cells =
		fixture->semantics.region_count;
	fixture->model.completeness.failure_record = UINT32_MAX;
	fixture->model.cells = fixture->runtime_cells;
	fixture->model.cell_count = fixture->semantics.region_count;
	fixture->model.phases = fixture->phases;
	fixture->model.phase_count = fixture->phase_count;
	fixture->snapshot.identity = UINT64_C(0x201);
	fixture->snapshot.topology_revision = UINT64_C(0x202);
	fixture->snapshot.cell_count = fixture->model.cell_count;
	fixture->snapshot.phase_count = fixture->model.phase_count;
	fixture->snapshot.region_count = fixture->semantics.region_count;
	fixture->snapshot.model = &fixture->model;
	fixture->snapshot.phases = fixture->coordinates;
	fixture->workspace.cell_region_offsets = fixture->cell_region_offsets;
	fixture->workspace.cell_region_offset_capacity = MAX_CELLS + 1U;
	fixture->workspace.region_indices = fixture->region_indices;
	fixture->workspace.region_index_capacity = MAX_REGIONS;
	fixture->workspace.region_runtime_cells = fixture->region_runtime_cells;
	fixture->workspace.region_runtime_cell_capacity = MAX_REGIONS;
	fixture->workspace.region_runtime_regions = fixture->region_runtime_regions;
	fixture->workspace.region_runtime_region_capacity = MAX_REGIONS;
	fixture->workspace.cell_portal_offsets = fixture->cell_portal_offsets;
	fixture->workspace.cell_portal_offset_capacity = MAX_CELLS + 1U;
	fixture->workspace.cell_portal_cursors = fixture->cell_portal_cursors;
	fixture->workspace.cell_portal_cursor_capacity = MAX_CELLS;
	fixture->workspace.portal_indices = fixture->portal_indices;
	fixture->workspace.portal_index_capacity = MAX_PORTALS * 2U;
	fixture->workspace.stance_overlap_offsets =
		fixture->stance_overlap_offsets;
	fixture->workspace.stance_overlap_offset_capacity = MAX_CELLS + 1U;
	fixture->workspace.stance_overlap_cursors =
		fixture->stance_overlap_cursors;
	fixture->workspace.stance_overlap_cursor_capacity = MAX_CELLS;
	fixture->workspace.stance_overlap_indices =
		fixture->stance_overlap_indices;
	fixture->workspace.stance_overlap_index_capacity = 8U;
	fixture->workspace.phase_transition_offsets =
		fixture->phase_transition_offsets;
	fixture->workspace.phase_transition_offset_capacity = MAX_PHASES + 1U;
	fixture->workspace.phase_transition_cursors =
		fixture->phase_transition_cursors;
	fixture->workspace.phase_transition_cursor_capacity = MAX_PHASES;
	fixture->workspace.phase_transition_indices =
		fixture->phase_transition_indices;
	fixture->workspace.phase_transition_index_capacity =
		MAX_PHASE_TRANSITIONS;
	fixture->workspace.phase_kernel_offsets = fixture->phase_kernel_offsets;
	fixture->workspace.phase_kernel_offset_capacity = MAX_PHASES + 1U;
	fixture->workspace.phase_kernel_cursors = fixture->phase_kernel_cursors;
	fixture->workspace.phase_kernel_cursor_capacity = MAX_PHASES;
	fixture->workspace.phase_kernel_indices = fixture->phase_kernel_indices;
	fixture->workspace.phase_kernel_index_capacity = MAX_KERNELS;
	CHECK(SG_CellPhaseLocatorPrepare(&fixture->authority,
		&fixture->configuration, &fixture->semantics, &fixture->snapshot,
		fixture->bindings, fixture->semantics.region_count,
		&fixture->workspace, &fixture->locator, &status));
	CHECK(status == SG_LOCALIZATION_OK);
	CHECK(SG_CellPhaseRuntimePrepare(&fixture->locator, LocalizationPmove,
		&fixture->runtime, &status));
	CHECK(status == SG_LOCALIZATION_OK);
}

static sg_localization_request_t Request(void)
{
	sg_localization_request_t request;

	memset(&request, 0, sizeof(request));
	request.expected_subject.client_id = 7U;
	request.expected_subject.spawn_generation = 3U;
	request.now_ms = 100U;
	request.minimum_frame_sequence = 9U;
	request.max_observation_age_ms = 0U;
	return request;
}

static sg_localization_observation_t Observation(
	const locator_fixture_t *fixture, sg_rune_stance_t stance,
	float x, float y, float z)
{
	sg_localization_observation_t observation;

	memset(&observation, 0, sizeof(observation));
	observation.authenticated = 1U;
	observation.stance = stance;
	observation.subject.client_id = 7U;
	observation.subject.spawn_generation = 3U;
	observation.rune_identity = fixture->snapshot.identity;
	observation.topology_revision = fixture->snapshot.topology_revision;
	observation.frame_sequence = 9U;
	observation.observed_at_ms = 100U;
	observation.authenticated_at_ms = 100U;
	Set3(observation.position, x, y, z);
	observation.host_state.pm_type = PM_NORMAL;
	observation.host_state.gravity =
		(short)fixture->identity.physics.gravity;
	observation.host_state.origin[0] = (short)(x * 8.0f);
	observation.host_state.origin[1] = (short)(y * 8.0f);
	observation.host_state.origin[2] = (short)(z * 8.0f);
	if (stance == SG_RUNE_STANCE_CROUCHING)
		observation.host_state.pm_flags |= PMF_DUCKED;
	return observation;
}

static sg_localization_environment_t Environment(void)
{
	sg_localization_environment_t environment;

	memset(&environment, 0, sizeof(environment));
	environment.authenticated = 1U;
	environment.rune_identity = UINT64_C(0x201);
	environment.topology_revision = UINT64_C(0x202);
	environment.frame_sequence = 9U;
	environment.sampled_at_ms = 100U;
	environment.authenticated_at_ms = 100U;
	return environment;
}

static void SyncHostState(const locator_fixture_t *fixture,
	sg_localization_observation_t *observation)
{
	uint32_t axis;

	observation->host_state.pm_type = PM_NORMAL;
	observation->host_state.gravity =
		(short)fixture->identity.physics.gravity;
	if (observation->stance == SG_RUNE_STANCE_CROUCHING)
		observation->host_state.pm_flags |= PMF_DUCKED;
	else
		observation->host_state.pm_flags &= (byte)~PMF_DUCKED;
	for (axis = 0U; axis < 3U; axis++)
	{
		float origin = observation->position[axis] * 8.0f;
		float velocity = observation->velocity[axis] * 8.0f;

		observation->host_state.origin[axis] =
			isfinite(origin) && origin >= (float)SHRT_MIN &&
			origin <= (float)SHRT_MAX ? (short)origin : 0;
		observation->host_state.velocity[axis] =
			isfinite(velocity) && velocity >= (float)SHRT_MIN &&
			velocity <= (float)SHRT_MAX ? (short)velocity : 0;
	}
}

static int Localize(locator_fixture_t *fixture,
	sg_localization_observation_t *observation,
	sg_localization_environment_t *environment,
	sg_localized_player_state_t *state, sg_localization_status_t *status)
{
	sg_localization_request_t request = Request();
	if (observation->kind != SG_LOCALIZATION_OBSERVATION_TEMPORARILY_ABSENT &&
		observation->kind != SG_LOCALIZATION_OBSERVATION_DEAD)
		SyncHostState(fixture, observation);

	environment->rune_identity = observation->rune_identity;
	environment->topology_revision = observation->topology_revision;
	environment->frame_sequence = observation->frame_sequence;
	environment->sampled_at_ms = observation->observed_at_ms;
	environment->authenticated_at_ms = observation->authenticated_at_ms;

	return SG_CellPhaseLocalize(&fixture->runtime, &request, observation,
		environment, state, status);
}

static int LocalizeRequest(locator_fixture_t *fixture,
	sg_localization_request_t *request,
	sg_localization_observation_t *observation,
	sg_localization_environment_t *environment,
	sg_localized_player_state_t *state, sg_localization_status_t *status)
{
	sg_host_pmove_request_t pmove_request;
	const sg_host_pmove_request_t *saved_request = environment->pmove_request;
	sg_host_pmove_substep_t *saved_substeps = environment->replay_substeps;
	size_t saved_capacity = environment->replay_substep_capacity;
	sg_host_pmove_trace_t *saved_traces = environment->replay_traces;
	size_t saved_trace_capacity = environment->replay_trace_capacity;
	int result;
	if (observation->kind != SG_LOCALIZATION_OBSERVATION_TEMPORARILY_ABSENT &&
		observation->kind != SG_LOCALIZATION_OBSERVATION_DEAD)
		SyncHostState(fixture, observation);

	environment->rune_identity = observation->rune_identity;
	environment->topology_revision = observation->topology_revision;
	environment->frame_sequence = observation->frame_sequence;
	environment->sampled_at_ms = observation->observed_at_ms;
	environment->authenticated_at_ms = observation->authenticated_at_ms;
	if (request->previous &&
		observation->kind == SG_LOCALIZATION_OBSERVATION_PRESENT &&
		!environment->pmove_request)
	{
		memset(&pmove_request, 0, sizeof(pmove_request));
		pmove_request.state = request->previous->host_state;
		pmove_request.previous_state = request->previous->host_state;
		pmove_request.command.msec =
			(byte)fixture->identity.physics.frame_ms;
		pmove_request.command.forwardmove =
			(short)(observation->velocity[0] * 8.0f);
		pmove_request.command.sidemove =
			(short)(observation->velocity[1] * 8.0f);
		if (observation->stance != request->previous->stance)
			pmove_request.command.upmove =
				observation->stance == SG_RUNE_STANCE_CROUCHING ? -1 : 1;
		environment->pmove_request = &pmove_request;
		environment->replay_substeps = fixture->replay_substeps;
		environment->replay_substep_capacity =
			sizeof(fixture->replay_substeps) /
			sizeof(fixture->replay_substeps[0]);
		environment->replay_traces = fixture->replay_traces;
		environment->replay_trace_capacity =
			sizeof(fixture->replay_traces) /
			sizeof(fixture->replay_traces[0]);
	}
	result = SG_CellPhaseLocalize(&fixture->runtime, request, observation,
		environment, state, status);
	environment->pmove_request = saved_request;
	environment->replay_substeps = saved_substeps;
	environment->replay_substep_capacity = saved_capacity;
	environment->replay_traces = saved_traces;
	environment->replay_trace_capacity = saved_trace_capacity;
	return result;
}

static void TestStandingCrouchingLowCeilingAndHalfWall(void)
{
	const float ceiling_mins[3] = { -100, -100, 28 };
	const float ceiling_maxs[3] = { 100, 100, 100 };
	const float wall_mins[3] = { -2, -40, -40 };
	const float wall_maxs[3] = { 2, 40, 10 };
	world_fixture_t ceiling = BoxWorld(ceiling_mins, ceiling_maxs);
	world_fixture_t wall = BoxWorld(wall_mins, wall_maxs);
	locator_fixture_t fixture;
	sg_localization_environment_t environment = Environment();
	sg_localization_observation_t observation;
	sg_localized_player_state_t state;
	sg_localization_status_t status;

	InitStandardFixture(&fixture, &ceiling,
		SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE, 0U, 0U,
		SG_RUNE_MOTION_AIRBORNE, SG_RUNE_SUPPORT_NONE, SG_RUNE_MEDIUM_DRY);
	FinalizeFixture(&fixture);
	observation = Observation(&fixture, SG_RUNE_STANCE_STANDING, 0, 0, 0);
	CHECK(!Localize(&fixture, &observation, &environment, &state, &status));
	CHECK(status == SG_LOCALIZATION_SOLID);
	observation.stance = SG_RUNE_STANCE_CROUCHING;
	CHECK(Localize(&fixture, &observation, &environment, &state, &status));
	CHECK(state.stance == SG_RUNE_STANCE_CROUCHING);

	InitStandardFixture(&fixture, &wall,
		SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE, 0U, 0U,
		SG_RUNE_MOTION_AIRBORNE, SG_RUNE_SUPPORT_NONE, SG_RUNE_MEDIUM_DRY);
	FinalizeFixture(&fixture);
	observation = Observation(&fixture, SG_RUNE_STANCE_STANDING, 0, 0, 30);
	CHECK(!Localize(&fixture, &observation, &environment, &state, &status));
	CHECK(status == SG_LOCALIZATION_SOLID);
	observation.position[2] = 40.0f;
	CHECK(Localize(&fixture, &observation, &environment, &state, &status));
}

static void TestSupportWaterAirAndTime(void)
{
	const float floor_mins[3] = { -100, -100, -100 };
	const float floor_maxs[3] = { 100, 100, 0 };
	world_fixture_t floor = BoxWorld(floor_mins, floor_maxs);
	world_fixture_t water = WaterWorld();
	world_fixture_t empty = EmptyWorld();
	locator_fixture_t fixture;
	sg_localization_environment_t environment = Environment();
	sg_localization_observation_t observation;
	sg_localized_player_state_t state;
	sg_localization_status_t status;

	InitStandardFixture(&fixture, &floor,
		SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED, 0U, 0U,
		SG_RUNE_MOTION_SUPPORTED, SG_RUNE_SUPPORT_SUPPORTED,
		SG_RUNE_MEDIUM_DRY);
	FinalizeFixture(&fixture);
	observation = Observation(&fixture, SG_RUNE_STANCE_STANDING,
		0, 0, 24.125f);
	if (!Localize(&fixture, &observation, &environment, &state, &status))
		fprintf(stderr, "support localization: %s\n",
			SG_LocalizationStatusString(status));
	CHECK(status == SG_LOCALIZATION_OK);
	CHECK(state.support == SG_RUNE_SUPPORT_SUPPORTED);
	CHECK(state.phase_elapsed_ms == 0U && state.time_quantum_index == 0U);
	CHECK(state.support_model_index == SG_HOST_COLLISION_MODEL_WORLD);
	CHECK(SG_DestinationPoseValid(&state.field_pose));
	CHECK(SG_PhaseCoordinateValid(&fixture.snapshot,
		&state.field_pose.phase));

	InitStandardFixture(&fixture, &water,
		SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE |
		SG_CONFIGURATION_SEMANTIC_REGION_WATER,
		3U, SG_HOST_CONTENTS_WATER,
		SG_RUNE_MOTION_SWIMMING, SG_RUNE_SUPPORT_NONE,
		SG_RUNE_MEDIUM_WATER);
	FinalizeFixture(&fixture);
	observation = Observation(&fixture, SG_RUNE_STANCE_STANDING, 0, 0, -30);
	CHECK(Localize(&fixture, &observation, &environment, &state, &status));
	CHECK(state.motion == SG_RUNE_MOTION_SWIMMING);
	CHECK(state.medium == SG_RUNE_MEDIUM_WATER && state.water_level == 3U);

	InitStandardFixture(&fixture, &empty,
		SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE, 0U, 0U,
		SG_RUNE_MOTION_AIRBORNE, SG_RUNE_SUPPORT_NONE, SG_RUNE_MEDIUM_DRY);
	FinalizeFixture(&fixture);
	observation = Observation(&fixture, SG_RUNE_STANCE_STANDING,
		0, 0, 24.125f);
	observation.velocity[2] = 120.0f;
	CHECK(Localize(&fixture, &observation, &environment, &state, &status));
	CHECK(state.motion == SG_RUNE_MOTION_AIRBORNE);
	CHECK(state.phase_velocity[2] == 120.0f);
}

static void TestBoundaryAndOverlapDeterminism(void)
{
	world_fixture_t empty = EmptyWorld();
	locator_fixture_t fixture;
	sg_host_collision_error_t host_error;
	sg_localization_environment_t environment = Environment();
	sg_localization_observation_t observation;
	sg_localized_player_state_t state;
	sg_localization_status_t status;
	uint32_t iteration;

	memset(&fixture, 0, sizeof(fixture));
	fixture.identity = Identity();
	BindWorldArrays(&empty);
	CHECK(SG_HostCollisionInit(&fixture.authority, &empty.world,
		&fixture.identity, &host_error));
	fixture.configuration.identity = fixture.identity;
	fixture.configuration.cells = fixture.configuration_cells;
	fixture.configuration.cell_count = 3U;
	fixture.configuration.faces = fixture.configuration_faces;
	fixture.configuration.face_count = 18U;
	fixture.configuration.certificate_nodes = fixture.certificates;
	fixture.configuration.certificate_node_count = 4U;
	fixture.configuration.certificate_roots[SG_RUNE_STANCE_STANDING] = 0U;
	fixture.configuration.certificate_roots[SG_RUNE_STANCE_CROUCHING] = 3U;
	SetConfigurationBox(&fixture, 0U, SG_RUNE_STANCE_STANDING, 0, 128);
	SetConfigurationBox(&fixture, 1U, SG_RUNE_STANCE_STANDING, -128, 0);
	SetConfigurationBox(&fixture, 2U, SG_RUNE_STANCE_CROUCHING, -128, 128);
	fixture.certificates[0].kind = SG_CONFIGURATION_CERTIFICATE_SPLIT;
	fixture.certificates[0].stance = SG_RUNE_STANCE_STANDING;
	fixture.certificates[0].front = 1U;
	fixture.certificates[0].back = 2U;
	fixture.certificates[0].plane.source_kind = SG_CONFIGURATION_PLANE_BSP;
	fixture.certificates[0].plane.normal[0] = 1.0f;
	fixture.certificates[1].kind = SG_CONFIGURATION_CERTIFICATE_VALID;
	fixture.certificates[1].stance = SG_RUNE_STANCE_STANDING;
	fixture.certificates[1].cell = 0U;
	fixture.certificates[2].kind = SG_CONFIGURATION_CERTIFICATE_VALID;
	fixture.certificates[2].stance = SG_RUNE_STANCE_STANDING;
	fixture.certificates[2].cell = 1U;
	fixture.certificates[3].kind = SG_CONFIGURATION_CERTIFICATE_VALID;
	fixture.certificates[3].stance = SG_RUNE_STANCE_CROUCHING;
	fixture.certificates[3].cell = 2U;
	fixture.semantics.identity = fixture.identity;
	fixture.semantics.regions = fixture.regions;
	fixture.semantics.region_count = 4U;
	fixture.semantics.faces = fixture.semantic_faces;
	fixture.semantics.face_count = 24U;
	SetSemanticBox(&fixture, 0U, 0U, 0, 128,
		SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE, 0, 0);
	SetSemanticBox(&fixture, 1U, 0U, 0, 128,
		SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE, 0, 0);
	SetSemanticBox(&fixture, 2U, 1U, -128, 0,
		SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE, 0, 0);
	SetSemanticBox(&fixture, 3U, 2U, -128, 128,
		SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE, 0, 0);
	SetRuntimeCell(&fixture, 0U, 0U, 2U);
	SetRuntimeCell(&fixture, 1U, 2U, 1U);
	SetRuntimeCell(&fixture, 2U, 3U, 1U);
	SetRuntimeCell(&fixture, 3U, 4U, 1U);
	SetPhase(&fixture, 0U, 0U, SG_RUNE_STANCE_STANDING,
		SG_RUNE_MOTION_AIRBORNE, SG_RUNE_SUPPORT_NONE, SG_RUNE_MEDIUM_DRY,
		SG_RUNE_FRAME_WORLD, SG_RUNE_MECHANISM_REF_NONE, 0, 1000);
	SetPhase(&fixture, 1U, 0U, SG_RUNE_STANCE_STANDING,
		SG_RUNE_MOTION_AIRBORNE, SG_RUNE_SUPPORT_NONE, SG_RUNE_MEDIUM_DRY,
		SG_RUNE_FRAME_WORLD, SG_RUNE_MECHANISM_REF_NONE, 0, 1000);
	SetPhase(&fixture, 2U, 1U, SG_RUNE_STANCE_STANDING,
		SG_RUNE_MOTION_AIRBORNE, SG_RUNE_SUPPORT_NONE, SG_RUNE_MEDIUM_DRY,
		SG_RUNE_FRAME_WORLD, SG_RUNE_MECHANISM_REF_NONE, 0, 1000);
	SetPhase(&fixture, 3U, 2U, SG_RUNE_STANCE_STANDING,
		SG_RUNE_MOTION_AIRBORNE, SG_RUNE_SUPPORT_NONE, SG_RUNE_MEDIUM_DRY,
		SG_RUNE_FRAME_WORLD, SG_RUNE_MECHANISM_REF_NONE, 0, 1000);
	SetPhase(&fixture, 4U, 3U, SG_RUNE_STANCE_CROUCHING,
		SG_RUNE_MOTION_AIRBORNE, SG_RUNE_SUPPORT_NONE, SG_RUNE_MEDIUM_DRY,
		SG_RUNE_FRAME_WORLD, SG_RUNE_MECHANISM_REF_NONE, 0, 1000);
	FinalizeFixture(&fixture);
	observation = Observation(&fixture, SG_RUNE_STANCE_STANDING, 0, 0, 24);
	for (iteration = 0U; iteration < 8U; iteration++)
	{
		CHECK(Localize(&fixture, &observation, &environment, &state, &status));
		CHECK(state.configuration_cell == 0U);
		CHECK(state.semantic_region == 0U);
		CHECK(state.field_pose.phase.phase_id == 0U);
	}
}

static void TestAuthenticationFreshnessAndHostAgreement(void)
{
	world_fixture_t empty = EmptyWorld();
	locator_fixture_t fixture;
	sg_localization_environment_t environment = Environment();
	sg_localization_observation_t observation;
	sg_localized_player_state_t state;
	sg_localization_status_t status;
	sg_localization_request_t request;

	InitStandardFixture(&fixture, &empty,
		SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE, 0U, 0U,
		SG_RUNE_MOTION_AIRBORNE, SG_RUNE_SUPPORT_NONE, SG_RUNE_MEDIUM_DRY);
	FinalizeFixture(&fixture);
	observation = Observation(&fixture, SG_RUNE_STANCE_STANDING, 0, 0, 24);
	observation.rune_identity++;
	CHECK(!Localize(&fixture, &observation, &environment, &state, &status));
	CHECK(status == SG_LOCALIZATION_IDENTITY_MISMATCH);
	observation = Observation(&fixture, SG_RUNE_STANCE_STANDING, 0, 0, 24);
	observation.subject.client_id++;
	CHECK(!Localize(&fixture, &observation, &environment, &state, &status));
	CHECK(status == SG_LOCALIZATION_IDENTITY_MISMATCH);
	observation = Observation(&fixture, SG_RUNE_STANCE_STANDING, 0, 0, 24);
	observation.authenticated = 0U;
	CHECK(!Localize(&fixture, &observation, &environment, &state, &status));
	CHECK(status == SG_LOCALIZATION_UNAUTHENTICATED);
	observation = Observation(&fixture, SG_RUNE_STANCE_STANDING, 0, 0, 24);
	request = Request();
	request.now_ms++;
	CHECK(!SG_CellPhaseLocalize(&fixture.runtime, &request, &observation,
		&environment, &state, &status));
	CHECK(status == SG_LOCALIZATION_STALE);
	observation.position[0] = NAN;
	CHECK(!Localize(&fixture, &observation, &environment, &state, &status));
	CHECK(status == SG_LOCALIZATION_NONFINITE);
	InitStandardFixture(&fixture, &empty,
		SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED, 0U, 0U,
		SG_RUNE_MOTION_AIRBORNE, SG_RUNE_SUPPORT_NONE, SG_RUNE_MEDIUM_DRY);
	FinalizeFixture(&fixture);
	observation = Observation(&fixture, SG_RUNE_STANCE_STANDING, 0, 0, 24);
	CHECK(!Localize(&fixture, &observation, &environment, &state, &status));
	CHECK(status == SG_LOCALIZATION_NO_SEMANTIC_REGION);
}

static void TestBoundedRecoveryAndReset(void)
{
	world_fixture_t empty = EmptyWorld();
	locator_fixture_t fixture;
	sg_localization_environment_t environment = Environment();
	sg_localization_observation_t observation;
	sg_localized_player_state_t previous;
	sg_localized_player_state_t bad_previous;
	sg_localized_player_state_t state;
	sg_localization_status_t status;
	sg_localization_request_t request;
	uint32_t iteration;

	InitStandardFixture(&fixture, &empty,
		SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE, 0U, 0U,
		SG_RUNE_MOTION_AIRBORNE, SG_RUNE_SUPPORT_NONE, SG_RUNE_MEDIUM_DRY);
	SetSemanticBox(&fixture, 0U, 0U, -128.0f, 10.0f,
		SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE, 0U, 0U);
	FinalizeFixture(&fixture);
	observation = Observation(&fixture, SG_RUNE_STANCE_STANDING,
		9.875f, 0.0f, 24.0f);
	observation.velocity[0] = 250.0f;
	CHECK(Localize(&fixture, &observation, &environment, &previous, &status));
	CHECK(previous.recovery == SG_LOCALIZATION_RECOVERY_NONE);

	observation.frame_sequence = 10U;
	observation.observed_at_ms = 101U;
	observation.authenticated_at_ms = 101U;
	observation.position[0] = 10.125f;
	observation.velocity[0] = 250.0f;
	environment.sampled_at_ms = 101U;
	request = Request();
	request.now_ms = 101U;
	request.minimum_frame_sequence = 10U;
	request.previous = &previous;
	request.maximum_recovery_distance = 0.25f;
	for (iteration = 0U; iteration < 8U; iteration++)
	{
		CHECK(LocalizeRequest(&fixture, &request, &observation,
			&environment, &state, &status));
		CHECK(state.semantic_region == previous.semantic_region);
		CHECK(state.recovery == SG_LOCALIZATION_RECOVERY_NUMERIC_DRIFT);
	}
	request.maximum_recovery_distance = 0.0f;
	CHECK(!LocalizeRequest(&fixture, &request, &observation,
		&environment, &state, &status));
	CHECK(status == SG_LOCALIZATION_RECOVERY_REJECTED);
	request.maximum_recovery_distance = NAN;
	CHECK(!LocalizeRequest(&fixture, &request, &observation,
		&environment, &state, &status));
	CHECK(status == SG_LOCALIZATION_RECOVERY_PARAMETER);
	bad_previous = previous;
	bad_previous.rune_identity++;
	request.previous = &bad_previous;
	request.maximum_recovery_distance = 0.25f;
	CHECK(!LocalizeRequest(&fixture, &request, &observation,
		&environment, &state, &status));
	CHECK(status == SG_LOCALIZATION_IDENTITY_MISMATCH);
	bad_previous = previous;
	bad_previous.frame_sequence = observation.frame_sequence;
	request.previous = &bad_previous;
	CHECK(!LocalizeRequest(&fixture, &request, &observation,
		&environment, &state, &status));
	CHECK(status == SG_LOCALIZATION_STALE);
	request.previous = &previous;

	observation.kind = SG_LOCALIZATION_OBSERVATION_TEMPORARILY_ABSENT;
	observation.observed_at_ms = 105U;
	observation.authenticated_at_ms = 105U;
	observation.frame_sequence = 11U;
	observation.position[0] = NAN;
	observation.velocity[1] = NAN;
	environment.sampled_at_ms = 105U;
	request.now_ms = 105U;
	request.minimum_frame_sequence = 11U;
	request.maximum_recovery_distance = 0.0f;
	request.maximum_temporary_absence_ms = 10U;
	CHECK(!LocalizeRequest(&fixture, &request, &observation,
		&environment, &state, &status));
	CHECK(status == SG_LOCALIZATION_RECOVERY_REJECTED);
	request.maximum_temporary_absence_ms = 0U;
	CHECK(!LocalizeRequest(&fixture, &request, &observation,
		&environment, &state, &status));
	CHECK(status == SG_LOCALIZATION_RECOVERY_PARAMETER);
	request.maximum_temporary_absence_ms = 10U;

	observation.observed_at_ms = 111U;
	observation.authenticated_at_ms = 111U;
	observation.frame_sequence = 12U;
	environment.sampled_at_ms = 111U;
	request.now_ms = 111U;
	request.minimum_frame_sequence = 12U;
	CHECK(!LocalizeRequest(&fixture, &request, &observation,
		&environment, &state, &status));
	CHECK(status == SG_LOCALIZATION_STALE);

	observation = Observation(&fixture, SG_RUNE_STANCE_STANDING,
		0.0f, 0.0f, 24.0f);
	observation.kind = SG_LOCALIZATION_OBSERVATION_TELEPORTED;
	observation.frame_sequence = 13U;
	environment = Environment();
	request = Request();
	request.minimum_frame_sequence = 13U;
	request.previous = &previous;
	CHECK(LocalizeRequest(&fixture, &request, &observation,
		&environment, &state, &status));
	CHECK(state.recovery == SG_LOCALIZATION_RECOVERY_NONE);

	observation.kind = SG_LOCALIZATION_OBSERVATION_NEW_SPAWN;
	observation.subject.spawn_generation = 4U;
	request.expected_subject.spawn_generation = 4U;
	CHECK(LocalizeRequest(&fixture, &request, &observation,
		&environment, &state, &status));
	CHECK(state.subject.spawn_generation == 4U);
	CHECK(state.recovery == SG_LOCALIZATION_RECOVERY_NONE);

	observation.kind = SG_LOCALIZATION_OBSERVATION_DEAD;
	CHECK(!LocalizeRequest(&fixture, &request, &observation,
		&environment, &state, &status));
	CHECK(status == SG_LOCALIZATION_RESET_REQUIRED);
}

static void TestRecoveryOutsideCertificateBoundary(void)
{
	world_fixture_t empty = EmptyWorld();
	locator_fixture_t fixture;
	sg_localization_environment_t environment = Environment();
	sg_localization_observation_t observation;
	sg_localized_player_state_t previous;
	sg_localized_player_state_t state;
	sg_localization_status_t status;
	sg_localization_request_t request;

	InitStandardFixture(&fixture, &empty,
		SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE, 0U, 0U,
		SG_RUNE_MOTION_AIRBORNE, SG_RUNE_SUPPORT_NONE, SG_RUNE_MEDIUM_DRY);
	FinalizeFixture(&fixture);
	observation = Observation(&fixture, SG_RUNE_STANCE_STANDING,
		127.875f, 0.0f, 24.0f);
	observation.velocity[0] = 250.0f;
	CHECK(Localize(&fixture, &observation, &environment, &previous, &status));
	observation.position[0] = 128.125f;
	observation.velocity[0] = 250.0f;
	observation.frame_sequence = 10U;
	observation.observed_at_ms = 101U;
	observation.authenticated_at_ms = 101U;
	request = Request();
	request.now_ms = 101U;
	request.minimum_frame_sequence = 10U;
	request.previous = &previous;
	request.maximum_recovery_distance = 0.5f;
	CHECK(LocalizeRequest(&fixture, &request, &observation,
		&environment, &state, &status));
	CHECK(state.configuration_cell == previous.configuration_cell);
	CHECK(state.semantic_region == previous.semantic_region);
	CHECK(state.recovery == SG_LOCALIZATION_RECOVERY_NUMERIC_DRIFT);
}

static void TestTemporaryAbsencePresentReentry(void)
{
	const float floor_mins[3] = { -100.0f, -100.0f, -100.0f };
	const float floor_maxs[3] = { 100.0f, 100.0f, 0.0f };
	world_fixture_t floor = BoxWorld(floor_mins, floor_maxs);
	locator_fixture_t fixture;
	sg_localization_environment_t environment = Environment();
	sg_localization_observation_t observation;
	sg_localized_player_state_t initial;
	sg_localized_player_state_t absence;
	sg_localized_player_state_t forged;
	sg_localized_player_state_t state;
	sg_localization_status_t status;
	sg_localization_request_t request;

	InitStandardFixture(&fixture, &floor,
		SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED, 0U, 0U,
		SG_RUNE_MOTION_SUPPORTED, SG_RUNE_SUPPORT_SUPPORTED,
		SG_RUNE_MEDIUM_DRY);
	FinalizeFixture(&fixture);
	observation = Observation(&fixture, SG_RUNE_STANCE_STANDING,
		0.0f, 0.0f, 24.125f);
	observation.host_state.pm_flags |= PMF_ON_GROUND;
	CHECK(Localize(&fixture, &observation, &environment, &initial, &status));

	observation.kind = SG_LOCALIZATION_OBSERVATION_TEMPORARILY_ABSENT;
	observation.frame_sequence = 10U;
	observation.observed_at_ms = 105U;
	observation.authenticated_at_ms = 105U;
	request = Request();
	request.now_ms = 105U;
	request.minimum_frame_sequence = 10U;
	request.previous = &initial;
	request.maximum_temporary_absence_ms = 10U;
	CHECK(LocalizeRequest(&fixture, &request, &observation,
		&environment, &absence, &status));
	CHECK(absence.phase_started_at_ms == initial.phase_started_at_ms);
	CHECK(absence.field_pose.sample_time_ms == 105U);
	CHECK(absence.phase_elapsed_ms == 5U);

	observation = Observation(&fixture, SG_RUNE_STANCE_STANDING,
		0.0f, 0.0f, 24.125f);
	observation.host_state.pm_flags |= PMF_ON_GROUND;
	observation.frame_sequence = 11U;
	observation.observed_at_ms = 106U;
	observation.authenticated_at_ms = 106U;
	request = Request();
	request.now_ms = 106U;
	request.minimum_frame_sequence = 11U;
	request.previous = &absence;
	CHECK(LocalizeRequest(&fixture, &request, &observation,
		&environment, &state, &status));
	CHECK(state.phase_started_at_ms == initial.phase_started_at_ms);
	CHECK(state.phase_elapsed_ms == 6U);

	observation.observed_at_ms = 107U;
	observation.authenticated_at_ms = 107U;
	observation.frame_sequence = 12U;
	request.now_ms = 107U;
	request.minimum_frame_sequence = 12U;
	CHECK(!LocalizeRequest(&fixture, &request, &observation,
		&environment, &state, &status));
	CHECK(status == SG_LOCALIZATION_RECOVERY_REJECTED);

	observation.observed_at_ms = 106U;
	observation.authenticated_at_ms = 106U;
	observation.frame_sequence = 11U;
	request.now_ms = 106U;
	request.minimum_frame_sequence = 11U;
	forged = absence;
	forged.phase_started_at_ms++;
	request.previous = &forged;
	CHECK(!LocalizeRequest(&fixture, &request, &observation,
		&environment, &state, &status));
	CHECK(status == SG_LOCALIZATION_RECOVERY_REJECTED);
	forged = absence;
	forged.host_state.origin[0]++;
	request.previous = &forged;
	CHECK(!LocalizeRequest(&fixture, &request, &observation,
		&environment, &state, &status));
	CHECK(status == SG_LOCALIZATION_RECOVERY_REJECTED);

	request.previous = &absence;
	request.now_ms = 107U;
	CHECK(!LocalizeRequest(&fixture, &request, &observation,
		&environment, &state, &status));
	CHECK(status == SG_LOCALIZATION_STALE);
}

static void SetTimeTransition(locator_fixture_t *fixture, uint32_t index,
	uint32_t source_phase, uint32_t destination_phase, float duration_ms)
{
	sg_rune_phase_transition_t *transition =
		&fixture->phase_transitions[index];

	memset(transition, 0, sizeof(*transition));
	transition->order = Order(SG_RUNE_ORDER_PHASE_TRANSITION, index);
	transition->id.value = SG_RuneModelStableIdFromOrderKey(&transition->order);
	transition->cell = fixture->runtime_cells[0].id;
	transition->source_phase = fixture->phases[source_phase].id;
	transition->destination_phase = fixture->phases[destination_phase].id;
	transition->kind = SG_RUNE_PHASE_TRANSITION_TIME;
	transition->duration_ms =
		(sg_rune_interval_t){ duration_ms, duration_ms };
	fixture->model.phase_transitions = fixture->phase_transitions;
	fixture->model.phase_transition_count = index + 1U;
}

static void InitAbsenceTimeBinFixture(locator_fixture_t *fixture,
	world_fixture_t *floor)
{
	InitStandardFixture(fixture, floor,
		SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED, 0U, 0U,
		SG_RUNE_MOTION_SUPPORTED, SG_RUNE_SUPPORT_SUPPORTED,
		SG_RUNE_MEDIUM_DRY);
	SetPhase(fixture, 3U, 1U, SG_RUNE_STANCE_CROUCHING,
		SG_RUNE_MOTION_SUPPORTED, SG_RUNE_SUPPORT_SUPPORTED,
		SG_RUNE_MEDIUM_DRY, SG_RUNE_FRAME_WORLD,
		SG_RUNE_MECHANISM_REF_NONE, 0.0f, 1000.0f);
	SetPhase(fixture, 1U, 0U, SG_RUNE_STANCE_STANDING,
		SG_RUNE_MOTION_SUPPORTED, SG_RUNE_SUPPORT_SUPPORTED,
		SG_RUNE_MEDIUM_DRY, SG_RUNE_FRAME_WORLD,
		SG_RUNE_MECHANISM_REF_NONE, 100.0f, 199.0f);
	SetPhase(fixture, 2U, 0U, SG_RUNE_STANCE_STANDING,
		SG_RUNE_MOTION_SUPPORTED, SG_RUNE_SUPPORT_SUPPORTED,
		SG_RUNE_MEDIUM_DRY, SG_RUNE_FRAME_WORLD,
		SG_RUNE_MECHANISM_REF_NONE, 200.0f, 1000.0f);
	fixture->phases[0].elapsed_ms.max_value = 99.0f;
	fixture->runtime_cells[0].phases.count = 3U;
	fixture->runtime_cells[1].phases.first = 3U;
	SetTimeTransition(fixture, 0U, 0U, 1U, 100.0f);
	SetTimeTransition(fixture, 1U, 1U, 2U, 200.0f);
	SetFixtureTiming(fixture, 50U, 50U);
	FinalizeFixture(fixture);
}

static void TestTemporaryAbsenceCrossesAuthenticatedTimeBins(void)
{
	const float floor_mins[3] = { -100.0f, -100.0f, -100.0f };
	const float floor_maxs[3] = { 100.0f, 100.0f, 0.0f };
	world_fixture_t floor = BoxWorld(floor_mins, floor_maxs);
	locator_fixture_t fixture;
	sg_localization_environment_t environment = Environment();
	sg_localization_observation_t observation;
	sg_localized_player_state_t initial;
	sg_localized_player_state_t aged;
	sg_localized_player_state_t absence;
	sg_localized_player_state_t state;
	sg_localization_status_t status;
	sg_localization_request_t request;

	InitAbsenceTimeBinFixture(&fixture, &floor);
	observation = Observation(&fixture, SG_RUNE_STANCE_STANDING,
		0.0f, 0.0f, 24.125f);
	observation.host_state.pm_flags |= PMF_ON_GROUND;
	CHECK(Localize(&fixture, &observation, &environment, &initial, &status));
	CHECK(initial.field_pose.phase.phase_id == 0U);
	observation.frame_sequence = 10U;
	observation.observed_at_ms = 150U;
	observation.authenticated_at_ms = 150U;
	request = Request();
	request.now_ms = 150U;
	request.minimum_frame_sequence = 10U;
	request.previous = &initial;
	CHECK(LocalizeRequest(&fixture, &request, &observation,
		&environment, &aged, &status));
	CHECK(aged.field_pose.phase.phase_id == 0U);
	CHECK(aged.phase_elapsed_ms == 50U);

	observation.kind = SG_LOCALIZATION_OBSERVATION_TEMPORARILY_ABSENT;
	observation.frame_sequence = 11U;
	observation.observed_at_ms = 250U;
	observation.authenticated_at_ms = 250U;
	request.now_ms = 250U;
	request.minimum_frame_sequence = 11U;
	request.previous = &aged;
	request.maximum_temporary_absence_ms = 300U;
	CHECK(LocalizeRequest(&fixture, &request, &observation,
		&environment, &absence, &status));
	CHECK(absence.field_pose.phase.phase_id == 1U);
	CHECK(absence.phase_started_at_ms == initial.phase_started_at_ms);
	CHECK(absence.phase_elapsed_ms == 150U);
	CHECK(absence.time_quantum_index == 15U);

	observation.frame_sequence = 12U;
	observation.observed_at_ms = 350U;
	observation.authenticated_at_ms = 350U;
	request.now_ms = 350U;
	request.minimum_frame_sequence = 12U;
	request.previous = &aged;
	CHECK(LocalizeRequest(&fixture, &request, &observation,
		&environment, &absence, &status));
	CHECK(absence.field_pose.phase.phase_id == 2U);
	CHECK(absence.phase_started_at_ms == initial.phase_started_at_ms);
	CHECK(absence.phase_elapsed_ms == 250U);
	CHECK(absence.time_quantum_index == 25U);
	observation = Observation(&fixture, SG_RUNE_STANCE_STANDING,
		0.0f, 0.0f, 24.125f);
	observation.host_state.pm_flags |= PMF_ON_GROUND;
	observation.frame_sequence = 13U;
	observation.observed_at_ms = 400U;
	observation.authenticated_at_ms = 400U;
	request.now_ms = 400U;
	request.minimum_frame_sequence = 13U;
	request.previous = &absence;
	request.maximum_temporary_absence_ms = 0U;
	CHECK(LocalizeRequest(&fixture, &request, &observation,
		&environment, &state, &status));
	CHECK(state.field_pose.phase.phase_id == 2U);
	CHECK(state.phase_started_at_ms == initial.phase_started_at_ms);
	CHECK(state.phase_elapsed_ms == 300U);

	fixture.model.phase_transition_count = 1U;
	CHECK(SG_CellPhaseLocatorPrepare(&fixture.authority,
		&fixture.configuration, &fixture.semantics, &fixture.snapshot,
		fixture.bindings, fixture.semantics.region_count,
		&fixture.workspace, &fixture.locator, &status));
	observation.kind = SG_LOCALIZATION_OBSERVATION_TEMPORARILY_ABSENT;
	observation.frame_sequence = 12U;
	observation.observed_at_ms = 350U;
	observation.authenticated_at_ms = 350U;
	request.now_ms = 350U;
	request.minimum_frame_sequence = 12U;
	request.previous = &aged;
	request.maximum_temporary_absence_ms = 300U;
	CHECK(!LocalizeRequest(&fixture, &request, &observation,
		&environment, &state, &status));
	CHECK(status == SG_LOCALIZATION_RECOVERY_REJECTED);
}

static void TestTemporaryAbsenceRejectsShallowWaterHold(void)
{
	world_fixture_t floor = ShallowWaterFloorWorld();
	locator_fixture_t fixture;
	sg_localization_environment_t environment = Environment();
	sg_localization_observation_t observation;
	sg_localized_player_state_t previous;
	sg_localized_player_state_t state;
	sg_localization_status_t status;
	sg_localization_request_t request;

	InitStandardFixture(&fixture, &floor,
		SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED |
			SG_CONFIGURATION_SEMANTIC_REGION_WATER,
		1U, SG_HOST_CONTENTS_WATER,
		SG_RUNE_MOTION_SUPPORTED, SG_RUNE_SUPPORT_SUPPORTED,
		SG_RUNE_MEDIUM_WATER);
	FinalizeFixture(&fixture);
	observation = Observation(&fixture, SG_RUNE_STANCE_STANDING,
		0.0f, 0.0f, 24.125f);
	observation.host_state.pm_flags |= PMF_ON_GROUND;
	CHECK(Localize(&fixture, &observation, &environment, &previous, &status));
	CHECK(previous.water_level == 1U);
	CHECK(previous.medium == SG_RUNE_MEDIUM_WATER);
	observation.kind = SG_LOCALIZATION_OBSERVATION_TEMPORARILY_ABSENT;
	observation.frame_sequence = 10U;
	observation.observed_at_ms = 105U;
	observation.authenticated_at_ms = 105U;
	request = Request();
	request.now_ms = 105U;
	request.minimum_frame_sequence = 10U;
	request.previous = &previous;
	request.maximum_temporary_absence_ms = 10U;
	CHECK(!LocalizeRequest(&fixture, &request, &observation,
		&environment, &state, &status));
	CHECK(status == SG_LOCALIZATION_RECOVERY_REJECTED);
}

static void TestTemporaryAbsenceRejectsMovingAndTimedDryHold(void)
{
	const float floor_mins[3] = { -100.0f, -100.0f, -100.0f };
	const float floor_maxs[3] = { 100.0f, 100.0f, 0.0f };
	world_fixture_t floor = BoxWorld(floor_mins, floor_maxs);
	locator_fixture_t fixture;
	sg_localization_environment_t environment = Environment();
	sg_localization_observation_t observation;
	sg_localized_player_state_t previous;
	sg_localized_player_state_t state;
	sg_localization_status_t status;
	sg_localization_request_t request;

	InitStandardFixture(&fixture, &floor,
		SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED, 0U, 0U,
		SG_RUNE_MOTION_SUPPORTED, SG_RUNE_SUPPORT_SUPPORTED,
		SG_RUNE_MEDIUM_DRY);
	FinalizeFixture(&fixture);
	observation = Observation(&fixture, SG_RUNE_STANCE_STANDING,
		0.0f, 0.0f, 24.125f);
	observation.velocity[0] = 300.0f;
	observation.host_state.pm_flags |= PMF_ON_GROUND;
	CHECK(Localize(&fixture, &observation, &environment, &previous, &status));
	observation.kind = SG_LOCALIZATION_OBSERVATION_TEMPORARILY_ABSENT;
	observation.frame_sequence = 10U;
	observation.observed_at_ms = 105U;
	observation.authenticated_at_ms = 105U;
	request = Request();
	request.now_ms = 105U;
	request.minimum_frame_sequence = 10U;
	request.previous = &previous;
	request.maximum_temporary_absence_ms = 10U;
	CHECK(!LocalizeRequest(&fixture, &request, &observation,
		&environment, &state, &status));
	CHECK(status == SG_LOCALIZATION_RECOVERY_REJECTED);

	observation = Observation(&fixture, SG_RUNE_STANCE_STANDING,
		0.0f, 0.0f, 24.125f);
	observation.host_state.pm_flags |= PMF_ON_GROUND;
	observation.host_state.pm_time = 1U;
	CHECK(Localize(&fixture, &observation, &environment, &previous, &status));
	observation.kind = SG_LOCALIZATION_OBSERVATION_TEMPORARILY_ABSENT;
	observation.frame_sequence = 10U;
	observation.observed_at_ms = 105U;
	observation.authenticated_at_ms = 105U;
	request = Request();
	request.now_ms = 105U;
	request.minimum_frame_sequence = 10U;
	request.previous = &previous;
	request.maximum_temporary_absence_ms = 10U;
	CHECK(!LocalizeRequest(&fixture, &request, &observation,
		&environment, &state, &status));
	CHECK(status == SG_LOCALIZATION_RECOVERY_REJECTED);
}

#ifdef SG_LOCALIZATION_REAL_PMOVE_TEST
static void CheckRealPmoveMotion(world_fixture_t *world,
	sg_configuration_semantic_region_flags_t flags, uint8_t water_level,
	sg_host_collision_contents_t water_type, sg_rune_motion_t motion,
	sg_rune_support_t support, sg_rune_medium_t medium,
	sg_rune_stance_t stance, float z, float initial_x, short forwardmove,
	short upmove, float expected_x, float expected_z)
{
	locator_fixture_t fixture;
	sg_localization_environment_t environment = Environment();
	sg_localization_observation_t observation;
	sg_localized_player_state_t previous;
	sg_localized_player_state_t state;
	sg_localization_status_t status;
	sg_localization_request_t request;
	sg_host_pmove_request_t pmove_request;
	sg_host_pmove_result_t result;
	sg_host_pmove_error_t error;

	InitStandardFixture(&fixture, world, flags, water_level, water_type,
		motion, support, medium);
	FinalizeFixture(&fixture);
	CHECK(SG_CellPhaseRuntimePrepare(&fixture.locator, Pmove,
		&fixture.runtime, &status));
	observation = Observation(&fixture, stance, 0.0f, 0.0f, z);
	observation.velocity[0] = initial_x;
	if (motion == SG_RUNE_MOTION_SUPPORTED)
		observation.host_state.pm_flags |= PMF_ON_GROUND;
	CHECK(Localize(&fixture, &observation, &environment, &previous, &status));
	memset(&pmove_request, 0, sizeof(pmove_request));
	pmove_request.state = previous.host_state;
	pmove_request.previous_state = previous.host_state;
	pmove_request.command.msec = 1U;
	pmove_request.command.forwardmove = forwardmove;
	pmove_request.command.upmove = upmove;
	CHECK(SG_HostPmoveEvaluateFrame(&fixture.authority, NULL, Pmove,
		&pmove_request, &result, &error));
	CHECK(error == SG_HOST_PMOVE_ERROR_NONE);
	CHECK(result.velocity[0] == expected_x);
	CHECK(result.velocity[2] == expected_z);
	observation.frame_sequence = 10U;
	observation.observed_at_ms = 101U;
	observation.authenticated_at_ms = 101U;
	memcpy(observation.position, result.origin, sizeof(observation.position));
	memcpy(observation.velocity, result.velocity, sizeof(observation.velocity));
	observation.host_state = result.state;
	environment.pmove_request = &pmove_request;
	environment.replay_substeps = fixture.replay_substeps;
	environment.replay_substep_capacity = 128U;
	environment.replay_traces = fixture.replay_traces;
	environment.replay_trace_capacity = 4096U;
	request = Request();
	request.now_ms = 101U;
	request.minimum_frame_sequence = 10U;
	request.previous = &previous;
	CHECK(LocalizeRequest(&fixture, &request, &observation,
		&environment, &state, &status));
	CHECK(status == SG_LOCALIZATION_OK);
	CHECK(state.field_pose.velocity[0] == expected_x);
	CHECK(state.field_pose.velocity[2] == expected_z);
}

static void TestRealPmoveAccelerationLaws(void)
{
	const float floor_mins[3] = { -100.0f, -100.0f, -100.0f };
	const float floor_maxs[3] = { 100.0f, 100.0f, 0.0f };
	world_fixture_t standing_floor = BoxWorld(floor_mins, floor_maxs);
	world_fixture_t crouching_floor = BoxWorld(floor_mins, floor_maxs);
	world_fixture_t air = EmptyWorld();
	world_fixture_t water = WaterWorld();

	CheckRealPmoveMotion(&standing_floor,
		SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED, 0U, 0U,
		SG_RUNE_MOTION_SUPPORTED, SG_RUNE_SUPPORT_SUPPORTED,
		SG_RUNE_MEDIUM_DRY, SG_RUNE_STANCE_STANDING, 24.125f,
		0.0f, 300, 0, 3.0f, 0.0f);
	CheckRealPmoveMotion(&crouching_floor,
		SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED, 0U, 0U,
		SG_RUNE_MOTION_SUPPORTED, SG_RUNE_SUPPORT_SUPPORTED,
		SG_RUNE_MEDIUM_DRY, SG_RUNE_STANCE_CROUCHING, 24.125f,
		0.0f, 300, -1, 1.0f, 0.0f);
	CheckRealPmoveMotion(&air,
		SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE, 0U, 0U,
		SG_RUNE_MOTION_AIRBORNE, SG_RUNE_SUPPORT_NONE,
		SG_RUNE_MEDIUM_DRY, SG_RUNE_STANCE_STANDING, 24.0f,
		0.0f, 300, 0, 0.25f, -0.75f);
	CheckRealPmoveMotion(&water,
		SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE |
			SG_CONFIGURATION_SEMANTIC_REGION_WATER,
		3U, SG_HOST_CONTENTS_WATER, SG_RUNE_MOTION_SWIMMING,
		SG_RUNE_SUPPORT_NONE, SG_RUNE_MEDIUM_WATER,
		SG_RUNE_STANCE_STANDING, -30.0f,
		0.0f, 300, 0, 0.875f, 1.125f);
	CheckRealPmoveMotion(&standing_floor,
		SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED, 0U, 0U,
		SG_RUNE_MOTION_SUPPORTED, SG_RUNE_SUPPORT_SUPPORTED,
		SG_RUNE_MEDIUM_DRY, SG_RUNE_STANCE_STANDING, 24.125f,
		300.0f, 0, 0, 298.125f, 0.0f);
	CheckRealPmoveMotion(&water,
		SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE |
			SG_CONFIGURATION_SEMANTIC_REGION_WATER,
		3U, SG_HOST_CONTENTS_WATER | SG_HOST_CONTENTS_CURRENT_UP,
		SG_RUNE_MOTION_SWIMMING, SG_RUNE_SUPPORT_NONE,
		SG_RUNE_MEDIUM_WATER, SG_RUNE_STANCE_STANDING, -30.0f,
		0.0f, 0, 0, 0.0f, 1.5f);
}

static void AddStanceOverlap(locator_fixture_t *fixture);
static void AddStanceKernel(locator_fixture_t *fixture);

static void CheckRealPmoveDuckChronology(world_fixture_t *world,
	sg_configuration_semantic_region_flags_t flags,
	sg_rune_motion_t motion, sg_rune_support_t support, float z,
	int grounded, int expect_probe, int expect_blocked)
{
	locator_fixture_t fixture;
	sg_localization_environment_t environment = Environment();
	sg_localization_observation_t observation;
	sg_localized_player_state_t previous;
	sg_localized_player_state_t state;
	sg_localization_status_t status;
	sg_localization_request_t request;
	sg_host_pmove_request_t pmove_request;
	sg_host_pmove_replay_workspace_t workspace;
	sg_host_pmove_replay_t replay;
	sg_host_pmove_error_t error;
	size_t standing_traces = 0U;
	size_t index;

	InitStandardFixture(&fixture, world, flags, 0U, 0U, motion, support,
		SG_RUNE_MEDIUM_DRY);
	if (expect_probe && !expect_blocked)
	{
		AddStanceOverlap(&fixture);
		AddStanceKernel(&fixture);
		fixture.kernels[0].source_cell = fixture.runtime_cells[1].id;
		fixture.kernels[0].destination_cell = fixture.runtime_cells[0].id;
		fixture.kernels[0].source_phase = fixture.phases[1].id;
		fixture.kernels[0].destination_phase = fixture.phases[0].id;
		fixture.kernels[0].parameters.duration_ms =
			(sg_rune_interval_t){ 1.0f, 1.0f };
		fixture.kernels[0].parameters.displacement.z =
			(sg_rune_interval_t){ -0.125f, -0.125f };
		fixture.kernels[0].parameters.speed =
			(sg_rune_interval_t){ 125.0f, 125.0f };
	}
	FinalizeFixture(&fixture);
	CHECK(SG_CellPhaseRuntimePrepare(&fixture.locator, Pmove,
		&fixture.runtime, &status));
	observation = Observation(&fixture, SG_RUNE_STANCE_CROUCHING,
		0.0f, 0.0f, z);
	if (grounded)
		observation.host_state.pm_flags |= PMF_ON_GROUND;
	CHECK(Localize(&fixture, &observation, &environment, &previous, &status));
	memset(&pmove_request, 0, sizeof(pmove_request));
	pmove_request.state = previous.host_state;
	pmove_request.previous_state = previous.host_state;
	pmove_request.command.msec = 1U;
	pmove_request.command.upmove = -1;
	workspace.substeps = fixture.replay_substeps;
	workspace.substep_capacity = 128U;
	workspace.traces = fixture.replay_traces;
	workspace.trace_capacity = 4096U;
	CHECK(SG_HostPmoveReplayFrame(&fixture.authority, NULL, Pmove,
		&pmove_request, &workspace, &replay, &error));
	CHECK(error == SG_HOST_PMOVE_ERROR_NONE);
	for (index = 0U; index < replay.trace_count; index++)
		if (replay.traces[index].maxs[2] == 32.0f)
			standing_traces++;
	CHECK((standing_traces != 0U) == expect_probe);
	if (expect_probe)
	{
		CHECK((replay.traces[0].state.pm_flags & PMF_DUCKED) != 0);
		CHECK((replay.traces[0].state.pm_flags & PMF_ON_GROUND) == 0);
		CHECK(replay.traces[0].maxs[2] == 32.0f);
		CHECK(replay.traces[0].result.allsolid == expect_blocked);
		CHECK(memcmp(replay.traces[0].start, replay.traces[0].end,
			sizeof(replay.traces[0].start)) == 0);
	}
	CHECK(((replay.result.state.pm_flags & PMF_DUCKED) != 0) ==
		(expect_blocked || !expect_probe));
	if (expect_probe && !expect_blocked)
		CHECK(replay.result.origin[2] - previous.field_pose.position[2] ==
			-0.125f);
	observation.frame_sequence = 10U;
	observation.observed_at_ms = 101U;
	observation.authenticated_at_ms = 101U;
	observation.stance = (replay.result.state.pm_flags & PMF_DUCKED) ?
		SG_RUNE_STANCE_CROUCHING : SG_RUNE_STANCE_STANDING;
	memcpy(observation.position, replay.result.origin,
		sizeof(observation.position));
	memcpy(observation.velocity, replay.result.velocity,
		sizeof(observation.velocity));
	observation.host_state = replay.result.state;
	environment.pmove_request = &pmove_request;
	environment.replay_substeps = fixture.replay_substeps;
	environment.replay_substep_capacity = 128U;
	environment.replay_traces = fixture.replay_traces;
	environment.replay_trace_capacity = 4096U;
	request = Request();
	request.now_ms = 101U;
	request.minimum_frame_sequence = 10U;
	request.previous = &previous;
	CHECK(LocalizeRequest(&fixture, &request, &observation,
		&environment, &state, &status));
	CHECK(status == SG_LOCALIZATION_OK);
	CHECK(state.stance == observation.stance);
}

static void TestRealPmoveUnduckChronology(void)
{
	const float ceiling_mins[3] = { -100.0f, -100.0f, 5.0f };
	const float ceiling_maxs[3] = { 100.0f, 100.0f, 100.0f };
	const float floor_mins[3] = { -100.0f, -100.0f, -100.0f };
	const float floor_maxs[3] = { 100.0f, 100.0f, 0.0f };
	world_fixture_t ceiling = BoxWorld(ceiling_mins, ceiling_maxs);
	world_fixture_t air = EmptyWorld();
	world_fixture_t floor = BoxWorld(floor_mins, floor_maxs);

	CheckRealPmoveDuckChronology(&ceiling,
		SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE,
		SG_RUNE_MOTION_AIRBORNE, SG_RUNE_SUPPORT_NONE,
		0.0f, 0, 1, 1);
	CheckRealPmoveDuckChronology(&air,
		SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE,
		SG_RUNE_MOTION_AIRBORNE, SG_RUNE_SUPPORT_NONE,
		24.0f, 0, 1, 0);
	CheckRealPmoveDuckChronology(&floor,
		SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED,
		SG_RUNE_MOTION_SUPPORTED, SG_RUNE_SUPPORT_SUPPORTED,
		24.125f, 1, 0, 0);
}

static void TestRealPmoveStepSlide(void)
{
	world_fixture_t step_world = StepWorld();
	locator_fixture_t fixture;
	sg_localization_environment_t environment = Environment();
	sg_localization_observation_t observation;
	sg_localized_player_state_t previous;
	sg_localized_player_state_t state;
	sg_localization_status_t status;
	sg_localization_request_t request;
	sg_host_pmove_request_t pmove_request;
	sg_host_pmove_result_t result;
	sg_host_pmove_error_t error;

	InitStandardFixture(&fixture, &step_world,
		SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED, 0U, 0U,
		SG_RUNE_MOTION_SUPPORTED, SG_RUNE_SUPPORT_SUPPORTED,
		SG_RUNE_MEDIUM_DRY);
	FinalizeFixture(&fixture);
	CHECK(SG_CellPhaseRuntimePrepare(&fixture.locator, Pmove,
		&fixture.runtime, &status));
	observation = Observation(&fixture, SG_RUNE_STANCE_STANDING,
		-16.125f, 0.0f, 24.125f);
	observation.velocity[0] = 300.0f;
	observation.host_state.pm_flags |= PMF_ON_GROUND;
	CHECK(Localize(&fixture, &observation, &environment, &previous, &status));
	memset(&pmove_request, 0, sizeof(pmove_request));
	pmove_request.state = previous.host_state;
	pmove_request.previous_state = previous.host_state;
	pmove_request.command.msec = 1U;
	pmove_request.command.forwardmove = 300;
	CHECK(SG_HostPmoveEvaluateFrame(&fixture.authority, NULL, Pmove,
		&pmove_request, &result, &error));
	CHECK(error == SG_HOST_PMOVE_ERROR_NONE);
	CHECK(result.origin[2] - previous.field_pose.position[2] == 16.0f ||
		result.origin[2] - previous.field_pose.position[2] == 18.0f);
	observation.frame_sequence = 10U;
	observation.observed_at_ms = 101U;
	observation.authenticated_at_ms = 101U;
	memcpy(observation.position, result.origin, sizeof(observation.position));
	memcpy(observation.velocity, result.velocity, sizeof(observation.velocity));
	observation.host_state = result.state;
	environment.pmove_request = &pmove_request;
	environment.replay_substeps = fixture.replay_substeps;
	environment.replay_substep_capacity = 128U;
	environment.replay_traces = fixture.replay_traces;
	environment.replay_trace_capacity = 4096U;
	request = Request();
	request.now_ms = 101U;
	request.minimum_frame_sequence = 10U;
	request.previous = &previous;
	CHECK(LocalizeRequest(&fixture, &request, &observation,
		&environment, &state, &status));
	CHECK(status == SG_LOCALIZATION_OK);
	CHECK(state.field_pose.position[2] == result.origin[2]);
}
#endif

static void SetAccelerationTransition(locator_fixture_t *fixture,
	uint32_t source_phase, uint32_t destination_phase)
{
	sg_rune_phase_transition_t *transition = &fixture->phase_transitions[0];

	memset(transition, 0, sizeof(*transition));
	transition->order = Order(SG_RUNE_ORDER_PHASE_TRANSITION, 0U);
	transition->id.value = SG_RuneModelStableIdFromOrderKey(&transition->order);
	transition->cell = fixture->runtime_cells[0].id;
	transition->source_phase = fixture->phases[source_phase].id;
	transition->destination_phase = fixture->phases[destination_phase].id;
	transition->kind = SG_RUNE_PHASE_TRANSITION_ACCELERATION;
	transition->duration_ms = (sg_rune_interval_t){ 0.0f, 100.0f };
	fixture->model.phase_transitions = fixture->phase_transitions;
	fixture->model.phase_transition_count = 1U;
}

static void SetRuntimePortalKernel(locator_fixture_t *fixture,
	uint32_t source_phase, uint32_t destination_phase)
{
	sg_rune_portal_t *portal = &fixture->runtime_portals[0];
	sg_rune_capability_kernel_t *kernel = &fixture->kernels[0];
	sg_rune_interval_t zero = { 0.0f, 0.0f };

	memset(portal, 0, sizeof(*portal));
	portal->order = Order(SG_RUNE_ORDER_PORTAL, 0U);
	portal->id.value = SG_RuneModelStableIdFromOrderKey(&portal->order);
	portal->from_cell = fixture->runtime_cells[0].id;
	portal->to_cell = fixture->runtime_cells[1].id;
	portal->phases = (sg_rune_phase_span_t){ source_phase,
		destination_phase - source_phase + 1U };
	portal->direction = SG_RUNE_PORTAL_FROM_TO;
	portal->clearance = 64.0f;
	portal->flags = SG_RUNE_PORTAL_HULL_VALID;
	memset(kernel, 0, sizeof(*kernel));
	kernel->order = Order(SG_RUNE_ORDER_KERNEL, 0U);
	kernel->id.value = SG_RuneModelStableIdFromOrderKey(&kernel->order);
	kernel->source_cell = fixture->runtime_cells[0].id;
	kernel->destination_cell = fixture->runtime_cells[1].id;
	kernel->boundary = portal->id;
	kernel->affordance = SG_RUNE_AFFORDANCE_REF_NONE;
	kernel->mechanism = SG_RUNE_MECHANISM_REF_NONE;
	kernel->source_phase = fixture->phases[source_phase].id;
	kernel->destination_phase = fixture->phases[destination_phase].id;
	kernel->transition = SG_RUNE_PHASE_TRANSITION_REF_NONE;
	kernel->family = SG_RUNE_CAPABILITY_AIRBORNE_CONTROL;
	kernel->cost_law = SG_RUNE_COST_CONSTANT_RATE;
	kernel->parameters.displacement.x = (sg_rune_interval_t){ 0.25f, 0.25f };
	kernel->parameters.displacement.y = zero;
	kernel->parameters.displacement.z = zero;
	kernel->parameters.duration_ms = (sg_rune_interval_t){ 1.0f, 1.0f };
	kernel->parameters.speed = (sg_rune_interval_t){ 250.0f, 250.0f };
	kernel->parameters.acceleration = (sg_rune_interval_t){ 0.0f, 1.0f };
	kernel->parameters.vertical_acceleration =
		(sg_rune_interval_t){ 0.0f, 1.0f };
	kernel->parameters.gravity = fixture->identity.physics.gravity;
	kernel->parameters.physics_abi_id = fixture->identity.physics_abi_id;
	kernel->flags = SG_RUNE_KERNEL_DIRECTIONAL |
		SG_RUNE_KERNEL_PHASE_AWARE | SG_RUNE_KERNEL_PROVEN;
	fixture->model.portals = fixture->runtime_portals;
	fixture->model.portal_count = 1U;
	fixture->model.kernels = fixture->kernels;
	fixture->model.kernel_count = 1U;
}

static void AddStanceOverlap(locator_fixture_t *fixture)
{
	sg_configuration_stance_overlap_t *overlap =
		&fixture->stance_overlaps[0];

	fixture->configuration.stance_overlaps = fixture->stance_overlaps;
	fixture->configuration.stance_overlap_count = 1U;
	memset(overlap, 0, sizeof(*overlap));
	overlap->standing_cell = 0U;
	overlap->crouching_cell = 1U;
	overlap->first_face = fixture->configuration_cells[0].first_face;
	overlap->face_count = fixture->configuration_cells[0].face_count;
	overlap->bounds = fixture->configuration_cells[0].bounds;
	overlap->interior_witness =
		fixture->configuration_cells[0].interior_witness;
}

static void AddStanceKernel(locator_fixture_t *fixture)
{
	sg_rune_capability_kernel_t *kernel = &fixture->kernels[0];

	memset(kernel, 0, sizeof(*kernel));
	kernel->order = Order(SG_RUNE_ORDER_KERNEL, 0U);
	kernel->id.value = SG_RuneModelStableIdFromOrderKey(&kernel->order);
	kernel->source_cell = fixture->runtime_cells[0].id;
	kernel->destination_cell = fixture->runtime_cells[1].id;
	kernel->boundary = SG_RUNE_PORTAL_REF_NONE;
	kernel->affordance = SG_RUNE_AFFORDANCE_REF_NONE;
	kernel->mechanism = SG_RUNE_MECHANISM_REF_NONE;
	kernel->source_phase = fixture->phases[0].id;
	kernel->destination_phase = fixture->phases[1].id;
	kernel->transition = SG_RUNE_PHASE_TRANSITION_REF_NONE;
	kernel->family = SG_RUNE_CAPABILITY_CONTINUOUS_SUPPORT;
	kernel->cost_law = SG_RUNE_COST_CONSTANT_RATE;
	kernel->parameters.duration_ms = (sg_rune_interval_t){ 100.0f, 100.0f };
	kernel->parameters.speed = (sg_rune_interval_t){ 0.0f, 0.0f };
	kernel->parameters.physics_abi_id = fixture->identity.physics_abi_id;
	fixture->model.kernels = fixture->kernels;
	fixture->model.kernel_count = 1U;
}

static void InitAdjacentStandingFixture(locator_fixture_t *fixture,
	world_fixture_t *world);
static void AddDirectPortal(locator_fixture_t *fixture);

static void TestPhaseContinuityProof(void)
{
	world_fixture_t empty = EmptyWorld();
	locator_fixture_t fixture;
	sg_localization_environment_t environment = Environment();
	sg_localization_observation_t observation;
	sg_localized_player_state_t previous;
	sg_localized_player_state_t state;
	sg_localization_status_t status;
	sg_localization_request_t request;

	InitStandardFixture(&fixture, &empty,
		SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE, 0U, 0U,
		SG_RUNE_MOTION_AIRBORNE, SG_RUNE_SUPPORT_NONE, SG_RUNE_MEDIUM_DRY);
	SetPhase(&fixture, 2U, 1U, SG_RUNE_STANCE_CROUCHING,
		SG_RUNE_MOTION_AIRBORNE, SG_RUNE_SUPPORT_NONE, SG_RUNE_MEDIUM_DRY,
		SG_RUNE_FRAME_WORLD, SG_RUNE_MECHANISM_REF_NONE, 0, 1000);
	SetPhase(&fixture, 1U, 0U, SG_RUNE_STANCE_STANDING,
		SG_RUNE_MOTION_AIRBORNE, SG_RUNE_SUPPORT_NONE, SG_RUNE_MEDIUM_DRY,
		SG_RUNE_FRAME_WORLD, SG_RUNE_MECHANISM_REF_NONE, 0, 1000);
	fixture.runtime_cells[0].phases.count = 2U;
	fixture.runtime_cells[1].phases.first = 2U;
	fixture.phases[0].velocity.x.max_value = 0.0f;
	fixture.phases[1].velocity.x.min_value = 0.125f;
	FinalizeFixture(&fixture);
	observation = Observation(&fixture, SG_RUNE_STANCE_STANDING,
		0.0f, 0.0f, 24.0f);
	observation.velocity[0] = -0.125f;
	CHECK(Localize(&fixture, &observation, &environment, &previous, &status));
	CHECK(previous.field_pose.phase.phase_id == 0U);
	observation.velocity[0] = 0.125f;
	observation.frame_sequence = 10U;
	observation.observed_at_ms = 101U;
	observation.authenticated_at_ms = 101U;
	request = Request();
	request.now_ms = 101U;
	request.minimum_frame_sequence = 10U;
	request.previous = &previous;
	request.maximum_recovery_distance = 0.5f;
	CHECK(!LocalizeRequest(&fixture, &request, &observation,
		&environment, &state, &status));
	CHECK(status == SG_LOCALIZATION_RECOVERY_REJECTED);

	SetAccelerationTransition(&fixture, 0U, 1U);
	CHECK(SG_CellPhaseLocatorPrepare(&fixture.authority,
		&fixture.configuration, &fixture.semantics, &fixture.snapshot,
		fixture.bindings, fixture.semantics.region_count,
		&fixture.workspace, &fixture.locator, &status));
	CHECK(fixture.locator.prepare_phase_transition_steps == 1U);
	CHECK(LocalizeRequest(&fixture, &request, &observation,
		&environment, &state, &status));
	CHECK(state.field_pose.phase.phase_id == 1U);
	CHECK(state.phase_transition_candidates_examined == 1U);
	fixture.phase_transitions[0].kind = SG_RUNE_PHASE_TRANSITION_TIME;
	CHECK(!SG_CellPhaseLocatorPrepare(&fixture.authority,
		&fixture.configuration, &fixture.semantics, &fixture.snapshot,
		fixture.bindings, fixture.semantics.region_count,
		&fixture.workspace, &fixture.locator, &status));
	CHECK(status == SG_LOCALIZATION_INVALID_BINDING);
}

static void TestAuthenticatedStanceOverlapContinuity(void)
{
	world_fixture_t empty = EmptyWorld();
	locator_fixture_t fixture;
	sg_localization_environment_t environment = Environment();
	sg_localization_observation_t observation;
	sg_localized_player_state_t previous;
	sg_localized_player_state_t state;
	sg_localization_status_t status;
	sg_localization_request_t request;

	InitStandardFixture(&fixture, &empty,
		SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE, 0U, 0U,
		SG_RUNE_MOTION_AIRBORNE, SG_RUNE_SUPPORT_NONE, SG_RUNE_MEDIUM_DRY);
	AddStanceOverlap(&fixture);
	AddStanceKernel(&fixture);
	SetFixtureTiming(&fixture, 100U, 100U);
	FinalizeFixture(&fixture);
	observation = Observation(&fixture, SG_RUNE_STANCE_STANDING,
		0.0f, 0.0f, 24.0f);
	CHECK(Localize(&fixture, &observation, &environment, &previous, &status));
	observation.stance = SG_RUNE_STANCE_CROUCHING;
	observation.frame_sequence = 10U;
	observation.observed_at_ms = 200U;
	observation.authenticated_at_ms = 200U;
	request = Request();
	request.now_ms = 200U;
	request.minimum_frame_sequence = 10U;
	request.previous = &previous;
	request.maximum_recovery_distance = 0.5f;
	CHECK(LocalizeRequest(&fixture, &request, &observation,
		&environment, &state, &status));
	CHECK(state.stance == SG_RUNE_STANCE_CROUCHING);
	CHECK(state.configuration_cell == 1U);
	CHECK(state.field_pose.phase.phase_id == 1U);
	CHECK(state.phase_started_at_ms == previous.phase_started_at_ms);
	CHECK(state.phase_elapsed_ms == 100U);

	fixture.configuration.stance_overlap_count = 0U;
	CHECK(SG_CellPhaseLocatorPrepare(&fixture.authority,
		&fixture.configuration, &fixture.semantics, &fixture.snapshot,
		fixture.bindings, fixture.semantics.region_count,
		&fixture.workspace, &fixture.locator, &status));
	CHECK(!LocalizeRequest(&fixture, &request, &observation,
		&environment, &state, &status));
	CHECK(status == SG_LOCALIZATION_RECOVERY_REJECTED);
}

static void TestSamePhaseClockCannotRewind(void)
{
	world_fixture_t empty = EmptyWorld();
	locator_fixture_t fixture;
	sg_localization_environment_t environment = Environment();
	sg_localization_observation_t observation;
	sg_localized_player_state_t previous;
	sg_localized_player_state_t state;
	sg_localization_status_t status;
	sg_localization_request_t request;

	InitStandardFixture(&fixture, &empty,
		SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE, 0U, 0U,
		SG_RUNE_MOTION_AIRBORNE, SG_RUNE_SUPPORT_NONE, SG_RUNE_MEDIUM_DRY);
	FinalizeFixture(&fixture);
	observation = Observation(&fixture, SG_RUNE_STANCE_STANDING,
		0.0f, 0.0f, 24.0f);
	CHECK(Localize(&fixture, &observation, &environment, &previous, &status));
	observation.frame_sequence = 10U;
	observation.observed_at_ms = 101U;
	observation.authenticated_at_ms = 101U;
	environment.sampled_at_ms = 101U;
	request = Request();
	request.now_ms = 101U;
	request.minimum_frame_sequence = 10U;
	request.previous = &previous;
	request.maximum_recovery_distance = 0.5f;
	CHECK(LocalizeRequest(&fixture, &request, &observation,
		&environment, &state, &status));
	CHECK(state.phase_started_at_ms == previous.phase_started_at_ms);
	CHECK(state.phase_elapsed_ms == previous.phase_elapsed_ms + 1U);
}

static void TestPhaseTransitionClockOrigins(void)
{
	world_fixture_t empty = EmptyWorld();
	locator_fixture_t fixture;
	sg_localization_environment_t environment = Environment();
	sg_localization_observation_t observation;
	sg_localized_player_state_t previous;
	sg_localized_player_state_t state;
	sg_localization_status_t status;
	sg_localization_request_t request;

	InitStandardFixture(&fixture, &empty,
		SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE, 0U, 0U,
		SG_RUNE_MOTION_AIRBORNE, SG_RUNE_SUPPORT_NONE, SG_RUNE_MEDIUM_DRY);
	SetPhase(&fixture, 2U, 1U, SG_RUNE_STANCE_CROUCHING,
		SG_RUNE_MOTION_AIRBORNE, SG_RUNE_SUPPORT_NONE, SG_RUNE_MEDIUM_DRY,
		SG_RUNE_FRAME_WORLD, SG_RUNE_MECHANISM_REF_NONE, 0, 1000);
	SetPhase(&fixture, 1U, 0U, SG_RUNE_STANCE_STANDING,
		SG_RUNE_MOTION_AIRBORNE, SG_RUNE_SUPPORT_NONE, SG_RUNE_MEDIUM_DRY,
		SG_RUNE_FRAME_WORLD, SG_RUNE_MECHANISM_REF_NONE, 0, 1000);
	fixture.runtime_cells[0].phases.count = 2U;
	fixture.runtime_cells[1].phases.first = 2U;
	fixture.phases[0].velocity.x.max_value = 0.0f;
	fixture.phases[1].velocity.x.min_value = 0.125f;
	SetAccelerationTransition(&fixture, 0U, 1U);
	SetFixtureTiming(&fixture, 100U, 100U);
	FinalizeFixture(&fixture);
	observation = Observation(&fixture, SG_RUNE_STANCE_STANDING,
		0.0f, 0.0f, 24.0f);
	observation.velocity[0] = -0.125f;
	CHECK(Localize(&fixture, &observation, &environment, &previous, &status));
	observation.observed_at_ms = 200U;
	observation.authenticated_at_ms = 200U;
	observation.frame_sequence = 10U;
	observation.velocity[0] = 0.125f;
	environment.sampled_at_ms = 200U;
	request = Request();
	request.now_ms = 200U;
	request.minimum_frame_sequence = 10U;
	request.previous = &previous;
	request.maximum_recovery_distance = 0.5f;
	CHECK(LocalizeRequest(&fixture, &request, &observation,
		&environment, &state, &status));
	CHECK(state.phase_started_at_ms == previous.phase_started_at_ms);
	CHECK(state.phase_elapsed_ms == previous.phase_elapsed_ms + 100U);
}

static void TestAgedTimeBinTransitionPreservesEpoch(void)
{
	world_fixture_t empty = EmptyWorld();
	locator_fixture_t fixture;
	sg_localization_environment_t environment = Environment();
	sg_localization_observation_t observation;
	sg_localized_player_state_t previous;
	sg_localized_player_state_t state;
	sg_localization_status_t status;
	sg_localization_request_t request;
	sg_rune_phase_transition_t *transition;

	InitStandardFixture(&fixture, &empty,
		SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE, 0U, 0U,
		SG_RUNE_MOTION_AIRBORNE, SG_RUNE_SUPPORT_NONE, SG_RUNE_MEDIUM_DRY);
	SetPhase(&fixture, 2U, 1U, SG_RUNE_STANCE_CROUCHING,
		SG_RUNE_MOTION_AIRBORNE, SG_RUNE_SUPPORT_NONE, SG_RUNE_MEDIUM_DRY,
		SG_RUNE_FRAME_WORLD, SG_RUNE_MECHANISM_REF_NONE, 0.0f, 1000.0f);
	SetPhase(&fixture, 1U, 0U, SG_RUNE_STANCE_STANDING,
		SG_RUNE_MOTION_AIRBORNE, SG_RUNE_SUPPORT_NONE, SG_RUNE_MEDIUM_DRY,
		SG_RUNE_FRAME_WORLD, SG_RUNE_MECHANISM_REF_NONE, 100.0f, 1000.0f);
	fixture.phases[0].elapsed_ms.max_value = 99.0f;
	fixture.runtime_cells[0].phases.count = 2U;
	fixture.runtime_cells[1].phases.first = 2U;
	transition = &fixture.phase_transitions[0];
	memset(transition, 0, sizeof(*transition));
	transition->order = Order(SG_RUNE_ORDER_PHASE_TRANSITION, 0U);
	transition->id.value = SG_RuneModelStableIdFromOrderKey(&transition->order);
	transition->cell = fixture.runtime_cells[0].id;
	transition->source_phase = fixture.phases[0].id;
	transition->destination_phase = fixture.phases[1].id;
	transition->kind = SG_RUNE_PHASE_TRANSITION_TIME;
	transition->duration_ms = (sg_rune_interval_t){ 100.0f, 100.0f };
	fixture.model.phase_transitions = fixture.phase_transitions;
	fixture.model.phase_transition_count = 1U;
	SetFixtureTiming(&fixture, 100U, 100U);
	FinalizeFixture(&fixture);
	observation = Observation(&fixture, SG_RUNE_STANCE_STANDING,
		0.0f, 0.0f, 24.0f);
	CHECK(Localize(&fixture, &observation, &environment, &previous, &status));
	CHECK(previous.field_pose.phase.phase_id == 0U);
	observation.frame_sequence = 10U;
	observation.observed_at_ms = 200U;
	observation.authenticated_at_ms = 200U;
	request = Request();
	request.now_ms = 200U;
	request.minimum_frame_sequence = 10U;
	request.previous = &previous;
	request.maximum_recovery_distance = 0.0f;
	CHECK(LocalizeRequest(&fixture, &request, &observation,
		&environment, &state, &status));
	CHECK(status == SG_LOCALIZATION_OK);
	CHECK(state.field_pose.phase.phase_id == 1U);
	CHECK(state.phase_started_at_ms == previous.phase_started_at_ms);
	CHECK(state.phase_elapsed_ms == 100U);
	CHECK(state.time_quantum_index == 10U);
}

static void TestRuntimePortalPhaseContinuity(void)
{
	world_fixture_t empty = EmptyWorld();
	locator_fixture_t fixture;
	sg_localization_environment_t environment = Environment();
	sg_localization_observation_t observation;
	sg_localized_player_state_t previous;
	sg_localized_player_state_t state;
	sg_localization_status_t status;
	sg_localization_request_t request;

	InitAdjacentStandingFixture(&fixture, &empty);
	fixture.bindings[1].rune_cell = fixture.runtime_cells[1].id;
	AddDirectPortal(&fixture);
	FinalizeFixture(&fixture);
	observation = Observation(&fixture, SG_RUNE_STANCE_STANDING,
		-0.125f, 0.0f, 24.0f);
	observation.velocity[0] = 250.0f;
	CHECK(Localize(&fixture, &observation, &environment, &previous, &status));
	CHECK(previous.field_pose.phase.cell_id == 0U);
	observation.position[0] = 0.125f;
	observation.velocity[0] = 250.0f;
	observation.observed_at_ms = 101U;
	observation.authenticated_at_ms = 101U;
	observation.frame_sequence = 10U;
	environment.sampled_at_ms = 101U;
	request = Request();
	request.now_ms = 101U;
	request.minimum_frame_sequence = 10U;
	request.previous = &previous;
	request.maximum_recovery_distance = 0.5f;
	CHECK(!LocalizeRequest(&fixture, &request, &observation,
		&environment, &state, &status));
	CHECK(status == SG_LOCALIZATION_RECOVERY_REJECTED);
	SetRuntimePortalKernel(&fixture, 0U, 1U);
	CHECK(SG_CellPhaseLocatorPrepare(&fixture.authority,
		&fixture.configuration, &fixture.semantics, &fixture.snapshot,
		fixture.bindings, fixture.semantics.region_count,
		&fixture.workspace, &fixture.locator, &status));
	CHECK(LocalizeRequest(&fixture, &request, &observation,
		&environment, &state, &status));
	CHECK(state.field_pose.phase.cell_id == 1U);
	CHECK(state.field_pose.phase.phase_id == 1U);
	CHECK(state.kernel_candidates_examined == 1U);
	CHECK(state.phase_transition_candidates_examined == 0U);
	CHECK(fixture.locator.prepare_kernel_steps == 1U);
	CHECK(fixture.locator.prepare_kernel_lookup_comparisons < 32U);
	fixture.kernels[0].parameters.displacement.x =
		(sg_rune_interval_t){ 0.25f, 0.25f };
	fixture.kernels[0].parameters.duration_ms =
		(sg_rune_interval_t){ 1.0f, 1.0f };
	fixture.portals[0].order = Order(SG_RUNE_ORDER_PORTAL, 1U);
	fixture.portals[0].id.value = SG_RuneModelStableIdFromOrderKey(
		&fixture.portals[0].order);
	CHECK(SG_CellPhaseLocatorPrepare(&fixture.authority,
		&fixture.configuration, &fixture.semantics, &fixture.snapshot,
		fixture.bindings, fixture.semantics.region_count,
		&fixture.workspace, &fixture.locator, &status));
	CHECK(!LocalizeRequest(&fixture, &request, &observation,
		&environment, &state, &status));
	CHECK(status == SG_LOCALIZATION_RECOVERY_REJECTED);
}

static void TestMechanismCrossingPreparation(void)
{
	world_fixture_t empty = EmptyWorld();
	locator_fixture_t fixture;
	sg_localization_status_t status;

	InitAdjacentStandingFixture(&fixture, &empty);
	fixture.bindings[1].rune_cell = fixture.runtime_cells[1].id;
	fixture.identity.physics.ground_acceleration = 1.0f;
	fixture.identity.physics.external_acceleration = 10.0f;
	fixture.authority.identity = fixture.identity;
	fixture.configuration.identity = fixture.identity;
	fixture.semantics.identity = fixture.identity;
	memset(&fixture.mechanisms[0], 0, sizeof(fixture.mechanisms[0]));
	fixture.mechanisms[0].order = Order(SG_RUNE_ORDER_MECHANISM, 0U);
	fixture.mechanisms[0].id.value = SG_RuneModelStableIdFromOrderKey(
		&fixture.mechanisms[0].order);
	fixture.model.mechanisms = fixture.mechanisms;
	fixture.model.mechanism_count = 1U;
	AddDirectPortal(&fixture);
	SetRuntimePortalKernel(&fixture, 0U, 1U);
	fixture.kernels[0].family = SG_RUNE_CAPABILITY_MECHANISM_CROSSING;
	fixture.kernels[0].mechanism = fixture.mechanisms[0].id;
	fixture.kernels[0].parameters.acceleration =
		(sg_rune_interval_t){ 10.0f, 10.0f };
	fixture.kernels[0].parameters.vertical_acceleration =
		(sg_rune_interval_t){ 10.0f, 10.0f };
	FinalizeFixture(&fixture);
	CHECK(SG_CellPhaseLocatorPrepare(&fixture.authority,
		&fixture.configuration, &fixture.semantics, &fixture.snapshot,
		fixture.bindings, fixture.semantics.region_count,
		&fixture.workspace, &fixture.locator, &status));
}

static void TestPreviousStateAuthentication(void)
{
	world_fixture_t empty = EmptyWorld();
	locator_fixture_t fixture;
	sg_localization_environment_t environment = Environment();
	sg_localization_observation_t observation;
	sg_localized_player_state_t previous;
	sg_localized_player_state_t altered;
	sg_localized_player_state_t state;
	sg_localization_status_t status;
	sg_localization_request_t request;

	InitStandardFixture(&fixture, &empty,
		SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE, 0U, 0U,
		SG_RUNE_MOTION_AIRBORNE, SG_RUNE_SUPPORT_NONE, SG_RUNE_MEDIUM_DRY);
	FinalizeFixture(&fixture);
	observation = Observation(&fixture, SG_RUNE_STANCE_STANDING,
		0.0f, 0.0f, 24.0f);
	CHECK(Localize(&fixture, &observation, &environment, &previous, &status));
	observation.kind = SG_LOCALIZATION_OBSERVATION_TEMPORARILY_ABSENT;
	observation.frame_sequence = 10U;
	observation.observed_at_ms = 101U;
	observation.authenticated_at_ms = 101U;
	environment.sampled_at_ms = 101U;
	request = Request();
	request.now_ms = 101U;
	request.minimum_frame_sequence = 10U;
	request.maximum_temporary_absence_ms = 10U;

	altered = previous;
	altered.field_pose.position[0] = 200.0f;
	request.previous = &altered;
	CHECK(!LocalizeRequest(&fixture, &request, &observation,
		&environment, &state, &status));
	CHECK(status == SG_LOCALIZATION_RECOVERY_REJECTED);
	altered = previous;
	altered.motion = SG_RUNE_MOTION_SUPPORTED;
	request.previous = &altered;
	CHECK(!LocalizeRequest(&fixture, &request, &observation,
		&environment, &state, &status));
	CHECK(status == SG_LOCALIZATION_RECOVERY_REJECTED);
	altered = previous;
	altered.phase_velocity[0] = 10.0f;
	request.previous = &altered;
	CHECK(!LocalizeRequest(&fixture, &request, &observation,
		&environment, &state, &status));
	CHECK(status == SG_LOCALIZATION_RECOVERY_REJECTED);
	altered = previous;
	altered.phase_started_at_ms++;
	request.previous = &altered;
	CHECK(!LocalizeRequest(&fixture, &request, &observation,
		&environment, &state, &status));
	CHECK(status == SG_LOCALIZATION_RECOVERY_REJECTED);
	altered = previous;
	altered.mover.value = fixture.runtime_cells[0].id.value;
	request.previous = &altered;
	CHECK(!LocalizeRequest(&fixture, &request, &observation,
		&environment, &state, &status));
	CHECK(status == SG_LOCALIZATION_RECOVERY_REJECTED);
}

static void InitAdjacentStandingFixture(locator_fixture_t *fixture,
	world_fixture_t *world)
{
	InitStandardFixture(fixture, world,
		SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE, 0U, 0U,
		SG_RUNE_MOTION_AIRBORNE, SG_RUNE_SUPPORT_NONE, SG_RUNE_MEDIUM_DRY);
	SetConfigurationBox(fixture, 0U, SG_RUNE_STANCE_STANDING, -128, 0);
	SetConfigurationBox(fixture, 1U, SG_RUNE_STANCE_STANDING, 0, 128);
	fixture->configuration.certificate_node_count = 4U;
	fixture->configuration.certificate_roots[SG_RUNE_STANCE_STANDING] = 0U;
	fixture->configuration.certificate_roots[SG_RUNE_STANCE_CROUCHING] = 3U;
	memset(fixture->certificates, 0, sizeof(fixture->certificates));
	fixture->certificates[0].kind = SG_CONFIGURATION_CERTIFICATE_SPLIT;
	fixture->certificates[0].stance = SG_RUNE_STANCE_STANDING;
	fixture->certificates[0].front = 2U;
	fixture->certificates[0].back = 1U;
	fixture->certificates[0].plane.normal[0] = 1.0f;
	fixture->certificates[0].plane.source_kind = SG_CONFIGURATION_PLANE_BSP;
	fixture->certificates[1].kind = SG_CONFIGURATION_CERTIFICATE_VALID;
	fixture->certificates[1].stance = SG_RUNE_STANCE_STANDING;
	fixture->certificates[1].cell = 0U;
	fixture->certificates[2].kind = SG_CONFIGURATION_CERTIFICATE_VALID;
	fixture->certificates[2].stance = SG_RUNE_STANCE_STANDING;
	fixture->certificates[2].cell = 1U;
	fixture->certificates[3].kind = SG_CONFIGURATION_CERTIFICATE_VALID;
	fixture->certificates[3].stance = SG_RUNE_STANCE_CROUCHING;
	fixture->certificates[3].cell = 0U;
	SetSemanticBox(fixture, 0U, 0U, -128, 0,
		SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE, 0U, 0U);
	SetSemanticBox(fixture, 1U, 1U, 0, 128,
		SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE, 0U, 0U);
	fixture->phases[1].stance = SG_RUNE_STANCE_STANDING;
	fixture->bindings[1].rune_cell = fixture->runtime_cells[0].id;
}

static void AddDirectPortal(locator_fixture_t *fixture)
{
	fixture->configuration.portals = fixture->portals;
	fixture->configuration.portal_count = 1U;
	fixture->configuration.vertices = fixture->portal_vertices;
	fixture->configuration.vertex_count = 4U;
	fixture->portals[0].order = Order(SG_RUNE_ORDER_PORTAL, 0U);
	fixture->portals[0].id.value = SG_RuneModelStableIdFromOrderKey(
		&fixture->portals[0].order);
	fixture->portals[0].from_cell = 0U;
	fixture->portals[0].to_cell = 1U;
	fixture->portals[0].stance = SG_RUNE_STANCE_STANDING;
	fixture->portals[0].plane.normal[0] = 1.0f;
	fixture->portals[0].first_vertex = 0U;
	fixture->portals[0].vertex_count = 4U;
	fixture->portals[0].clearance = 200.0f;
	SetRune3(&fixture->portal_vertices[0], 0, -100, -100);
	SetRune3(&fixture->portal_vertices[1], 0, 100, -100);
	SetRune3(&fixture->portal_vertices[2], 0, 100, 100);
	SetRune3(&fixture->portal_vertices[3], 0, -100, 100);
}

static void TestRecoveryConnectivityProof(void)
{
	const float wall_mins[3] = { -2, -40, -40 };
	const float wall_maxs[3] = { 2, 40, 10 };
	world_fixture_t empty = EmptyWorld();
	world_fixture_t wall = BoxWorld(wall_mins, wall_maxs);
	locator_fixture_t fixture;
	sg_localization_environment_t environment = Environment();
	sg_localization_observation_t observation;
	sg_localized_player_state_t previous;
	sg_localized_player_state_t state;
	sg_localization_status_t status;
	sg_localization_request_t request;

	InitAdjacentStandingFixture(&fixture, &empty);
	SetFixtureTiming(&fixture, 20U, 10U);
	FinalizeFixture(&fixture);
	observation = Observation(&fixture, SG_RUNE_STANCE_STANDING,
		-10.0f, 0.0f, 24.0f);
	observation.velocity[0] = 1000.0f;
	CHECK(Localize(&fixture, &observation, &environment, &previous, &status));
	observation.position[0] = 10.0f;
	observation.velocity[0] = 1000.0f;
	observation.frame_sequence = 10U;
	observation.observed_at_ms = 120U;
	observation.authenticated_at_ms = 120U;
	request = Request();
	request.now_ms = 120U;
	request.minimum_frame_sequence = 10U;
	request.previous = &previous;
	request.maximum_recovery_distance = 32.0f;
	CHECK(!LocalizeRequest(&fixture, &request, &observation,
		&environment, &state, &status));
	CHECK(status == SG_LOCALIZATION_RECOVERY_REJECTED);

	AddDirectPortal(&fixture);
	CHECK(SG_CellPhaseLocatorPrepare(&fixture.authority,
		&fixture.configuration, &fixture.semantics, &fixture.snapshot,
		fixture.bindings, fixture.semantics.region_count,
		&fixture.workspace, &fixture.locator, &status));
	CHECK(LocalizeRequest(&fixture, &request, &observation,
		&environment, &state, &status));
	CHECK(state.configuration_cell == 1U);
	CHECK(state.recovery == SG_LOCALIZATION_RECOVERY_EXACT_CONTINUITY);

	InitAdjacentStandingFixture(&fixture, &wall);
	SetFixtureTiming(&fixture, 60U, 10U);
	AddDirectPortal(&fixture);
	FinalizeFixture(&fixture);
	observation = Observation(&fixture, SG_RUNE_STANCE_STANDING,
		-30.0f, 0.0f, 24.0f);
	observation.velocity[0] = 1000.0f;
	CHECK(Localize(&fixture, &observation, &environment, &previous, &status));
	observation.position[0] = 30.0f;
	observation.velocity[0] = 1000.0f;
	observation.frame_sequence = 10U;
	observation.observed_at_ms = 160U;
	observation.authenticated_at_ms = 160U;
	request.now_ms = 160U;
	request.previous = &previous;
	request.maximum_recovery_distance = 80.0f;
	CHECK(!LocalizeRequest(&fixture, &request, &observation,
		&environment, &state, &status));
	CHECK(status == SG_LOCALIZATION_RECOVERY_REJECTED);
}

static void TestHostReplayRejectsFabricatedPolyline(void)
{
	world_fixture_t empty = EmptyWorld();
	locator_fixture_t fixture;
	sg_localization_environment_t environment = Environment();
	sg_localization_observation_t observation;
	sg_localized_player_state_t previous;
	sg_localized_player_state_t state;
	sg_localization_status_t status;
	sg_localization_request_t request;
	sg_host_pmove_request_t pmove_request;

	InitStandardFixture(&fixture, &empty,
		SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE, 0U, 0U,
		SG_RUNE_MOTION_AIRBORNE, SG_RUNE_SUPPORT_NONE, SG_RUNE_MEDIUM_DRY);
	SetFixtureTiming(&fixture, 3U, 1U);
	FinalizeFixture(&fixture);
	observation = Observation(&fixture, SG_RUNE_STANCE_STANDING,
		0.0f, 0.0f, 24.0f);
	CHECK(Localize(&fixture, &observation, &environment, &previous, &status));
	observation.position[0] = 40.0f;
	observation.frame_sequence = 10U;
	observation.observed_at_ms = 103U;
	observation.authenticated_at_ms = 103U;
	request = Request();
	request.now_ms = 103U;
	request.minimum_frame_sequence = 10U;
	request.previous = &previous;
	request.maximum_recovery_distance = 0.0f;
	memset(&pmove_request, 0, sizeof(pmove_request));
	pmove_request.state = previous.host_state;
	pmove_request.previous_state = previous.host_state;
	pmove_request.command.msec = 3U;
	memset(fixture.replay_substeps, 0, sizeof(fixture.replay_substeps));
	fixture.replay_substeps[0].origin[0] = 40.0f;
	fixture.replay_substeps[1].origin[0] = 60.0f;
	fixture.replay_substeps[2].origin[0] = 40.0f;
	environment.pmove_request = &pmove_request;
	environment.replay_substeps = fixture.replay_substeps;
	environment.replay_substep_capacity = 3U;
	environment.replay_traces = fixture.replay_traces;
	environment.replay_trace_capacity = 4096U;
	CHECK(!LocalizeRequest(&fixture, &request, &observation,
		&environment, &state, &status));
	CHECK(status == SG_LOCALIZATION_RECOVERY_REJECTED);
}

static void TestHostReplayRejectsStationaryTraceTeleport(void)
{
	const float wall_mins[3] = { -2.0f, -40.0f, -40.0f };
	const float wall_maxs[3] = { 2.0f, 40.0f, 40.0f };
	world_fixture_t wall = BoxWorld(wall_mins, wall_maxs);
	locator_fixture_t fixture;
	sg_localization_environment_t environment = Environment();
	sg_localization_observation_t observation;
	sg_localized_player_state_t previous;
	sg_localized_player_state_t state;
	sg_localization_status_t status;
	sg_localization_request_t request;

	InitAdjacentStandingFixture(&fixture, &wall);
	AddDirectPortal(&fixture);
	SetFixtureTiming(&fixture, 60U, 60U);
	FinalizeFixture(&fixture);
	CHECK(SG_CellPhaseRuntimePrepare(&fixture.locator, TeleportPmove,
		&fixture.runtime, &status));
	observation = Observation(&fixture, SG_RUNE_STANCE_STANDING,
		-30.0f, 0.0f, 24.0f);
	CHECK(Localize(&fixture, &observation, &environment, &previous, &status));
	observation.position[0] = 30.0f;
	observation.frame_sequence = 10U;
	observation.observed_at_ms = 160U;
	observation.authenticated_at_ms = 160U;
	request = Request();
	request.now_ms = 160U;
	request.minimum_frame_sequence = 10U;
	request.previous = &previous;
	request.maximum_recovery_distance = 80.0f;
	CHECK(!LocalizeRequest(&fixture, &request, &observation,
		&environment, &state, &status));
	CHECK(status == SG_LOCALIZATION_RECOVERY_REJECTED);
}

static void TestHostReplayRejectsSubstitutePointTrace(void)
{
	const float wall_mins[3] = { -2.0f, -40.0f, -40.0f };
	const float wall_maxs[3] = { 2.0f, 40.0f, 10.0f };
	world_fixture_t wall = BoxWorld(wall_mins, wall_maxs);
	locator_fixture_t fixture;
	sg_localization_environment_t environment = Environment();
	sg_localization_observation_t observation;
	sg_localized_player_state_t previous;
	sg_localized_player_state_t state;
	sg_localization_status_t status;
	sg_localization_request_t request;

	InitAdjacentStandingFixture(&fixture, &wall);
	AddDirectPortal(&fixture);
	SetFixtureTiming(&fixture, 60U, 60U);
	FinalizeFixture(&fixture);
	CHECK(SG_CellPhaseRuntimePrepare(&fixture.locator,
		PointTraceTeleportPmove, &fixture.runtime, &status));
	observation = Observation(&fixture, SG_RUNE_STANCE_STANDING,
		-30.0f, 0.0f, 24.0f);
	CHECK(Localize(&fixture, &observation, &environment, &previous, &status));
	observation.position[0] = 30.0f;
	observation.frame_sequence = 10U;
	observation.observed_at_ms = 160U;
	observation.authenticated_at_ms = 160U;
	request = Request();
	request.now_ms = 160U;
	request.minimum_frame_sequence = 10U;
	request.previous = &previous;
	request.maximum_recovery_distance = 80.0f;
	CHECK(!LocalizeRequest(&fixture, &request, &observation,
		&environment, &state, &status));
	CHECK(status == SG_LOCALIZATION_RECOVERY_REJECTED);
}

static void TestMultiplePortalCrossingsUseHostReplay(void)
{
	world_fixture_t empty = EmptyWorld();
	locator_fixture_t fixture;
	sg_localization_environment_t environment = Environment();
	sg_localization_observation_t observation;
	sg_localized_player_state_t previous;
	sg_localized_player_state_t state;
	sg_localization_status_t status;
	sg_localization_request_t request;

	InitAdjacentStandingFixture(&fixture, &empty);
	AddDirectPortal(&fixture);
	SetFixtureTiming(&fixture, 20U, 10U);
	fixture.phases[0].velocity.x.max_value = 2000.0f;
	FinalizeFixture(&fixture);
	observation = Observation(&fixture, SG_RUNE_STANCE_STANDING,
		-20.0f, 0.0f, 24.0f);
	observation.velocity[0] = 2000.0f;
	CHECK(Localize(&fixture, &observation, &environment, &previous, &status));
	observation.position[0] = 20.0f;
	observation.velocity[0] = 2000.0f;
	observation.frame_sequence = 10U;
	observation.observed_at_ms = 120U;
	observation.authenticated_at_ms = 120U;
	request = Request();
	request.now_ms = 120U;
	request.minimum_frame_sequence = 10U;
	request.previous = &previous;
	request.maximum_recovery_distance = 0.0f;
	CHECK(LocalizeRequest(&fixture, &request, &observation,
		&environment, &state, &status));
	CHECK(state.configuration_cell == 1U);
	CHECK(state.phase_started_at_ms == previous.phase_started_at_ms);
}

static void TestPreparationScalesByRecords(void)
{
	world_fixture_t empty = EmptyWorld();
	locator_fixture_t fixture;
	sg_host_collision_error_t host_error;
	sg_localization_status_t status;
	sg_cell_phase_locator_t locator;
	sg_localization_region_binding_t swap;
	sg_localization_environment_t environment = Environment();
	sg_localization_observation_t observation;
	sg_localized_player_state_t previous;
	sg_localized_player_state_t state;
	sg_localization_request_t request;
	uint32_t index;

	memset(&fixture, 0, sizeof(fixture));
	fixture.identity = Identity();
	fixture.identity.physics.frame_ms = 20U;
	fixture.identity.physics.substep_ms = 10U;
	BindWorldArrays(&empty);
	CHECK(SG_HostCollisionInit(&fixture.authority, &empty.world,
		&fixture.identity, &host_error));
	fixture.configuration.identity = fixture.identity;
	fixture.configuration.cells = fixture.configuration_cells;
	fixture.configuration.cell_count = MAX_CELLS;
	fixture.configuration.faces = fixture.configuration_faces;
	fixture.configuration.face_count = MAX_CELLS * 6U;
	fixture.configuration.certificate_nodes = fixture.certificates;
	fixture.configuration.certificate_node_count = 4U;
	fixture.configuration.certificate_roots[SG_RUNE_STANCE_STANDING] = 0U;
	fixture.configuration.certificate_roots[SG_RUNE_STANCE_CROUCHING] = 3U;
	fixture.certificates[0].kind = SG_CONFIGURATION_CERTIFICATE_SPLIT;
	fixture.certificates[0].stance = SG_RUNE_STANCE_STANDING;
	fixture.certificates[0].front = 2U;
	fixture.certificates[0].back = 1U;
	fixture.certificates[0].plane.normal[0] = 1.0f;
	fixture.certificates[0].plane.source_kind = SG_CONFIGURATION_PLANE_BSP;
	fixture.certificates[1].kind = SG_CONFIGURATION_CERTIFICATE_VALID;
	fixture.certificates[1].cell = 0U;
	fixture.certificates[1].stance = SG_RUNE_STANCE_STANDING;
	fixture.certificates[2].kind = SG_CONFIGURATION_CERTIFICATE_VALID;
	fixture.certificates[2].cell = 1U;
	fixture.certificates[2].stance = SG_RUNE_STANCE_STANDING;
	fixture.certificates[3].kind = SG_CONFIGURATION_CERTIFICATE_VALID;
	fixture.certificates[3].cell = 0U;
	fixture.certificates[3].stance = SG_RUNE_STANCE_CROUCHING;
	fixture.semantics.identity = fixture.identity;
	fixture.semantics.regions = fixture.regions;
	fixture.semantics.region_count = MAX_REGIONS;
	fixture.semantics.faces = fixture.semantic_faces;
	fixture.semantics.face_count = MAX_FACES;
	for (index = 0U; index < MAX_REGIONS; index++)
	{
		float minimum_x = index == 1U ? 0.0f : -128.0f;
		float maximum_x = index == 0U ? 0.0f : 128.0f;

		SetConfigurationBox(&fixture, index, SG_RUNE_STANCE_STANDING,
			minimum_x, maximum_x);
		SetSemanticBox(&fixture, index, index, minimum_x, maximum_x,
			SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE, 0U, 0U);
		SetRuntimeCell(&fixture, index, index, 1U);
		SetPhase(&fixture, index, index, SG_RUNE_STANCE_STANDING,
			SG_RUNE_MOTION_AIRBORNE, SG_RUNE_SUPPORT_NONE,
			SG_RUNE_MEDIUM_DRY, SG_RUNE_FRAME_WORLD,
			SG_RUNE_MECHANISM_REF_NONE, 0, 1000);
	}
	fixture.bindings[1].rune_cell = fixture.runtime_cells[0].id;
	fixture.configuration.portals = fixture.portals;
	fixture.configuration.portal_count = MAX_PORTALS;
	fixture.configuration.vertices = fixture.portal_vertices;
	fixture.configuration.vertex_count = MAX_PORTALS * 4U;
	for (index = 0U; index < MAX_PORTALS; index++)
	{
		uint32_t vertex = index * 4U;
		sg_configuration_portal_t *portal = &fixture.portals[index];

		portal->order = Order(SG_RUNE_ORDER_PORTAL, index);
		portal->id.value = SG_RuneModelStableIdFromOrderKey(&portal->order);
		portal->from_cell = index == 0U ? 0U : 2U + (index - 1U) % 62U;
		portal->to_cell = index == 0U ? 1U : 2U + index % 62U;
		portal->stance = SG_RUNE_STANCE_STANDING;
		portal->plane.normal[0] = 1.0f;
		portal->first_vertex = vertex;
		portal->vertex_count = 4U;
		portal->clearance = 200.0f;
		SetRune3(&fixture.portal_vertices[vertex], 0, -100, -100);
		SetRune3(&fixture.portal_vertices[vertex + 1U], 0, 100, -100);
		SetRune3(&fixture.portal_vertices[vertex + 2U], 0, 100, 100);
		SetRune3(&fixture.portal_vertices[vertex + 3U], 0, -100, 100);
	}
	FinalizeFixture(&fixture);
	CHECK(fixture.locator.prepare_cell_steps == MAX_CELLS);
	CHECK(fixture.locator.prepare_region_steps == MAX_REGIONS);
	CHECK(fixture.locator.prepare_binding_checks == MAX_REGIONS);
	CHECK(fixture.locator.prepare_runtime_cell_comparisons <=
		(uint64_t)MAX_REGIONS * 7U);
	CHECK(fixture.locator.prepare_portal_steps == MAX_PORTALS);
	CHECK(fixture.locator.prepare_portal_adjacency_steps == MAX_PORTALS * 2U);
	observation = Observation(&fixture, SG_RUNE_STANCE_STANDING,
		-10.0f, 0.0f, 24.0f);
	observation.velocity[0] = 1000.0f;
	CHECK(Localize(&fixture, &observation, &environment, &previous, &status));
	observation.position[0] = 10.0f;
	observation.velocity[0] = 1000.0f;
	observation.frame_sequence = 10U;
	observation.observed_at_ms = 120U;
	observation.authenticated_at_ms = 120U;
	request = Request();
	request.now_ms = 120U;
	request.minimum_frame_sequence = 10U;
	request.previous = &previous;
	request.maximum_recovery_distance = 32.0f;
	CHECK(LocalizeRequest(&fixture, &request, &observation,
		&environment, &state, &status));
	CHECK(state.portal_candidates_examined == 1U);

	swap = fixture.bindings[1];
	fixture.bindings[1] = fixture.bindings[2];
	fixture.bindings[2] = swap;
	CHECK(!SG_CellPhaseLocatorPrepare(&fixture.authority,
		&fixture.configuration, &fixture.semantics, &fixture.snapshot,
		fixture.bindings, fixture.semantics.region_count,
		&fixture.workspace, &locator, &status));
	CHECK(status == SG_LOCALIZATION_INVALID_BINDING);
	swap = fixture.bindings[1];
	fixture.bindings[1] = fixture.bindings[2];
	fixture.bindings[2] = swap;
	fixture.regions[32].cell = 30U;
	CHECK(!SG_CellPhaseLocatorPrepare(&fixture.authority,
		&fixture.configuration, &fixture.semantics, &fixture.snapshot,
		fixture.bindings, fixture.semantics.region_count,
		&fixture.workspace, &locator, &status));
	CHECK(status == SG_LOCALIZATION_INVALID_ARGUMENT);
}

static void TestMoverCarryFailsClosedWithoutProductionAuthority(void)
{
	world_fixture_t world = MoverWorld();
	locator_fixture_t fixture;
	sg_localization_environment_t environment = Environment();
	sg_localization_observation_t observation;
	sg_localized_player_state_t state;
	sg_localization_status_t status;
	sg_host_collision_instance_t instance;
	sg_host_collision_scene_t scene;
	sg_rune_mechanism_ref_t mechanism;
	sg_rune_phase_transition_t *transition;
	sg_localization_request_t request;

	InitStandardFixture(&fixture, &world,
		SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE, 0U, 0U,
		SG_RUNE_MOTION_SUPPORTED, SG_RUNE_SUPPORT_MOVER, SG_RUNE_MEDIUM_DRY);
	memset(&fixture.mechanisms[0], 0, sizeof(fixture.mechanisms[0]));
	fixture.mechanisms[0].order = Order(SG_RUNE_ORDER_MECHANISM, 0U);
	fixture.mechanisms[0].id.value = SG_RuneModelStableIdFromOrderKey(
		&fixture.mechanisms[0].order);
	fixture.model.mechanisms = fixture.mechanisms;
	fixture.model.mechanism_count = 1U;
	mechanism = fixture.mechanisms[0].id;
	fixture.phases[0].reference_frame = SG_RUNE_FRAME_MOVER_RELATIVE;
	fixture.phases[0].mover = mechanism;
	fixture.phases[0].elapsed_ms.max_value = 99.0f;
	SetPhase(&fixture, 1U, 0U, SG_RUNE_STANCE_STANDING,
		SG_RUNE_MOTION_SUPPORTED, SG_RUNE_SUPPORT_MOVER, SG_RUNE_MEDIUM_DRY,
		SG_RUNE_FRAME_MOVER_RELATIVE, mechanism, 100.0f, 1000.0f);
	SetPhase(&fixture, 2U, 1U, SG_RUNE_STANCE_CROUCHING,
		SG_RUNE_MOTION_SUPPORTED, SG_RUNE_SUPPORT_MOVER, SG_RUNE_MEDIUM_DRY,
		SG_RUNE_FRAME_MOVER_RELATIVE, mechanism, 0.0f, 1000.0f);
	fixture.runtime_cells[0].phases.count = 2U;
	fixture.runtime_cells[1].phases.first = 2U;
	transition = &fixture.phase_transitions[0];
	memset(transition, 0, sizeof(*transition));
	transition->order = Order(SG_RUNE_ORDER_PHASE_TRANSITION, 0U);
	transition->id.value = SG_RuneModelStableIdFromOrderKey(&transition->order);
	transition->cell = fixture.runtime_cells[0].id;
	transition->source_phase = fixture.phases[0].id;
	transition->destination_phase = fixture.phases[1].id;
	transition->kind = SG_RUNE_PHASE_TRANSITION_MOVER_DWELL;
	transition->duration_ms = (sg_rune_interval_t){ 100.0f, 100.0f };
	fixture.model.phase_transitions = fixture.phase_transitions;
	fixture.model.phase_transition_count = 1U;
	FinalizeFixture(&fixture);
	CHECK(fixture.locator.prepare_phase_transition_steps == 1U);
	memset(&instance, 0, sizeof(instance));
	instance.instance_id = UINT64_C(0x301);
	instance.model_index = 1U;
	scene.instances = &instance;
	scene.instance_count = 1U;
	environment.scene = &scene;
	observation = Observation(&fixture, SG_RUNE_STANCE_STANDING,
		0.0f, 0.0f, 24.125f);
	CHECK(!Localize(&fixture, &observation, &environment, &state, &status));
	CHECK(status == SG_LOCALIZATION_MOVER_UNBOUND);
	instance.transform.origin[0] = 64.0f;
	observation.position[0] = 64.0f;
	observation.frame_sequence = 10U;
	observation.observed_at_ms = 101U;
	observation.authenticated_at_ms = 101U;
	request = Request();
	request.now_ms = 101U;
	request.minimum_frame_sequence = 10U;
	CHECK(!LocalizeRequest(&fixture, &request, &observation, &environment,
		&state, &status));
	CHECK(status == SG_LOCALIZATION_MOVER_UNBOUND);

	SetFixtureTiming(&fixture, 100U, 100U);
	FinalizeFixture(&fixture);
	instance.transform.origin[0] = 0.0f;
	observation = Observation(&fixture, SG_RUNE_STANCE_STANDING,
		0.0f, 0.0f, 24.125f);
	CHECK(!Localize(&fixture, &observation, &environment, &state, &status));
	CHECK(status == SG_LOCALIZATION_MOVER_UNBOUND);
	instance.transform.origin[0] = 64.0f;
	observation.position[0] = 64.0f;
	observation.frame_sequence = 10U;
	observation.observed_at_ms = 200U;
	observation.authenticated_at_ms = 200U;
	request = Request();
	request.now_ms = 200U;
	request.minimum_frame_sequence = 10U;
	CHECK(!LocalizeRequest(&fixture, &request, &observation, &environment,
		&state, &status));
	CHECK(status == SG_LOCALIZATION_MOVER_UNBOUND);
}

int main(void)
{
	TestStandingCrouchingLowCeilingAndHalfWall();
	TestSupportWaterAirAndTime();
	TestBoundaryAndOverlapDeterminism();
	TestAuthenticationFreshnessAndHostAgreement();
	TestBoundedRecoveryAndReset();
	TestRecoveryOutsideCertificateBoundary();
	TestTemporaryAbsencePresentReentry();
	TestTemporaryAbsenceCrossesAuthenticatedTimeBins();
	TestTemporaryAbsenceRejectsShallowWaterHold();
	TestTemporaryAbsenceRejectsMovingAndTimedDryHold();
	TestPhaseContinuityProof();
	TestAuthenticatedStanceOverlapContinuity();
	TestSamePhaseClockCannotRewind();
	TestPhaseTransitionClockOrigins();
	TestAgedTimeBinTransitionPreservesEpoch();
	TestRuntimePortalPhaseContinuity();
	TestMechanismCrossingPreparation();
	TestPreviousStateAuthentication();
	TestRecoveryConnectivityProof();
	TestHostReplayRejectsFabricatedPolyline();
	TestHostReplayRejectsStationaryTraceTeleport();
	TestHostReplayRejectsSubstitutePointTrace();
	TestMultiplePortalCrossingsUseHostReplay();
	TestPreparationScalesByRecords();
	TestMoverCarryFailsClosedWithoutProductionAuthority();
#ifdef SG_LOCALIZATION_REAL_PMOVE_TEST
	TestRealPmoveAccelerationLaws();
	TestRealPmoveUnduckChronology();
	TestRealPmoveStepSlide();
#endif
	if (failures != 0)
	{
		fprintf(stderr, "%d localization checks failed\n", failures);
		return 1;
	}
	puts("cell/phase localization tests passed");
	return 0;
}
