#include "sg_water_capability_fixture.h"

#include <limits.h>
#include <string.h>

static void Set3(float value[3], float x, float y, float z)
{
	value[0] = x;
	value[1] = y;
	value[2] = z;
}

static void SetPlane(sg_bsp_plane_t *plane, float x, float y, float z,
	float distance)
{
	Set3(plane->normal.value, x, y, z);
	plane->distance = distance;
	if (x == 1.0f) plane->type = 0;
	else if (y == 1.0f) plane->type = 1;
	else if (z == 1.0f) plane->type = 2;
	else if (x == -1.0f) plane->type = 3;
	else if (y == -1.0f) plane->type = 4;
	else plane->type = 5;
}

static sg_rune_model_identity_t Identity(float gravity)
{
	sg_rune_model_identity_t identity;

	memset(&identity, 0, sizeof(identity));
	identity.bsp_content_id = UINT64_C(0x1001);
	identity.entity_semantics_id = UINT64_C(0x1002);
	identity.physics_abi_id = UINT64_C(0x1003);
	identity.source_set_identity = UINT64_C(0x1004);
	identity.schema_id = UINT64_C(0x1005);
	identity.producer_identity = UINT64_C(0x1006);
	Set3(identity.standing_hull.mins.value, -1.0f, -1.0f, -1.0f);
	Set3(identity.standing_hull.maxs.value, 1.0f, 1.0f, 1.0f);
	Set3(identity.crouching_hull.mins.value, -1.0f, -1.0f, -1.0f);
	Set3(identity.crouching_hull.maxs.value, 1.0f, 1.0f, 1.0f);
	identity.physics.gravity = gravity;
	identity.physics.ground_acceleration = 10.0f;
	identity.physics.air_acceleration = 1.0f;
	identity.physics.water_acceleration = 10.0f;
	identity.physics.hook_acceleration = 800.0f;
	identity.physics.external_acceleration = 1.0f;
	identity.physics.water_drag = 1.0f;
	identity.physics.max_velocity = 2000.0f;
	identity.physics.frame_ms = 100U;
	identity.physics.substep_ms = 10U;
	return identity;
}

static sg_rune_medium_t MediumFromContents(uint32_t contents)
{
	if (contents & SG_HOST_CONTENTS_WATER) return SG_RUNE_MEDIUM_WATER;
	if (contents & SG_HOST_CONTENTS_LAVA) return SG_RUNE_MEDIUM_LAVA;
	if (contents & SG_HOST_CONTENTS_SLIME) return SG_RUNE_MEDIUM_SLIME;
	return SG_RUNE_MEDIUM_DRY;
}

static uint32_t FlagsFromContents(uint32_t contents)
{
	if (contents & SG_HOST_CONTENTS_WATER)
		return SG_CONFIGURATION_SEMANTIC_REGION_WATER;
	if (contents & SG_HOST_CONTENTS_LAVA)
		return SG_CONFIGURATION_SEMANTIC_REGION_LAVA |
			SG_CONFIGURATION_SEMANTIC_REGION_HAZARD;
	if (contents & SG_HOST_CONTENTS_SLIME)
		return SG_CONFIGURATION_SEMANTIC_REGION_SLIME |
			SG_CONFIGURATION_SEMANTIC_REGION_HAZARD;
	return 0U;
}

static void SetSemanticFace(sg_configuration_semantic_face_t *face,
	float x, float y, float z, float distance, uint32_t source_kind,
	uint32_t source_index, uint8_t reversed)
{
	memset(face, 0, sizeof(*face));
	Set3(face->normal, x, y, z);
	face->distance = distance;
	face->source_kind = source_kind;
	face->source_index = source_index;
	face->reversed = reversed;
}

static void SetRegionBox(water_fixture_t *fixture, uint32_t region_index,
	uint32_t cell, float min_x, float max_x, uint32_t contents,
	uint32_t shared_source_kind, uint32_t shared_source_index,
	uint8_t shared_reversed)
{
	sg_configuration_semantic_region_t *region =
		&fixture->regions[region_index];
	uint32_t first = region_index * 6U;

	memset(region, 0, sizeof(*region));
	region->id = UINT64_C(100) + region_index;
	region->cell = cell;
	region->first_face = first;
	region->face_count = 6U;
	Set3(region->bounds.mins.value, min_x, -64.0f, -64.0f);
	Set3(region->bounds.maxs.value, max_x, 64.0f, 64.0f);
	Set3(region->interior_witness.value,
		(min_x + max_x) * 0.5f, 0.0f, 0.0f);
	region->origin_contents = contents;
	region->origin_rune_contents = SG_HostCollisionRuneContents(contents);
	region->sample_contents[0] = contents;
	region->sample_contents[1] = contents;
	region->sample_contents[2] = contents;
	region->water_type = contents;
	region->water_level = (uint8_t)(contents & SG_HOST_MASK_WATER ? 3U : 0U);
	region->flags = FlagsFromContents(contents) |
		SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE;
	SetSemanticFace(&fixture->faces[first], 1.0f, 0.0f, 0.0f, max_x,
		max_x == 0.0f ? shared_source_kind :
			SG_CONFIGURATION_SEMANTIC_PLANE_SUPPORT_CLIP,
		max_x == 0.0f ? shared_source_index : first, shared_reversed);
	SetSemanticFace(&fixture->faces[first + 1U], -1.0f, 0.0f, 0.0f, -min_x,
		min_x == 0.0f ? shared_source_kind :
			SG_CONFIGURATION_SEMANTIC_PLANE_SUPPORT_CLIP,
		min_x == 0.0f ? shared_source_index : first + 1U,
		shared_reversed);
	SetSemanticFace(&fixture->faces[first + 2U], 0.0f, 1.0f, 0.0f, 64.0f,
		SG_CONFIGURATION_SEMANTIC_PLANE_SUPPORT_CLIP, first + 2U, 0U);
	SetSemanticFace(&fixture->faces[first + 3U], 0.0f, -1.0f, 0.0f, 64.0f,
		SG_CONFIGURATION_SEMANTIC_PLANE_SUPPORT_CLIP, first + 3U, 0U);
	SetSemanticFace(&fixture->faces[first + 4U], 0.0f, 0.0f, 1.0f, 64.0f,
		SG_CONFIGURATION_SEMANTIC_PLANE_SUPPORT_CLIP, first + 4U, 0U);
	SetSemanticFace(&fixture->faces[first + 5U], 0.0f, 0.0f, -1.0f, 64.0f,
		SG_CONFIGURATION_SEMANTIC_PLANE_SUPPORT_CLIP, first + 5U, 0U);
}

sg_rune_phase_basis_t WaterFixturePhase(const water_fixture_t *fixture,
	uint32_t index, uint32_t region_index)
{
	const sg_configuration_semantic_region_t *region =
		&fixture->regions[region_index];
	sg_rune_phase_basis_t phase;

	memset(&phase, 0, sizeof(phase));
	phase.order.source_set_identity =
		fixture->authority.identity.source_set_identity;
	phase.order.domain = SG_RUNE_ORDER_PHASE;
	phase.order.source_index = index;
	phase.order.local_ordinal = region_index;
	phase.order.variant = index;
	phase.id.value = SG_RuneModelStableIdFromOrderKey(&phase.order);
	phase.stance = fixture->cells[region->cell].stance;
	phase.motion = region->water_level >= 2U ? SG_RUNE_MOTION_SWIMMING :
		SG_RUNE_MOTION_AIRBORNE;
	phase.support = SG_RUNE_SUPPORT_NONE;
	phase.medium = MediumFromContents(region->water_type);
	phase.void_relation = SG_RUNE_VOID_CLEAR;
	phase.reference_frame = SG_RUNE_FRAME_WORLD;
	phase.mover = SG_RUNE_MECHANISM_REF_NONE;
	phase.velocity.x.min_value = -2000.0f;
	phase.velocity.x.max_value = 2000.0f;
	phase.velocity.y = phase.velocity.x;
	phase.velocity.z = phase.velocity.x;
	phase.elapsed_ms.min_value = 0.0f;
	phase.elapsed_ms.max_value = 10000.0f;
	phase.time_quantum_ms = 10U;
	phase.time_horizon_ms = 10000U;
	return phase;
}

void WaterFixturePmove(pmove_t *pmove)
{
	float origin[3];
	int contents;
	int commands[3];
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		origin[axis] = pmove->s.origin[axis] * 0.125f;
	contents = pmove->pointcontents(origin);
	pmove->watertype = contents;
	pmove->waterlevel = (contents & MASK_WATER) ? 3 : 0;
	Set3(pmove->mins, -1.0f, -1.0f, -1.0f);
	Set3(pmove->maxs, 1.0f, 1.0f, 1.0f);
	pmove->viewheight = 0.0f;
	commands[0] = pmove->cmd.forwardmove;
	commands[1] = -pmove->cmd.sidemove;
	commands[2] = pmove->cmd.upmove;
	if (pmove->waterlevel >= 2 && commands[0] == 0 && commands[1] == 0 &&
		commands[2] == 0)
		commands[2] = -60;
	if (contents & SG_HOST_CONTENTS_CURRENT_0) commands[0] += 400;
	if (contents & SG_HOST_CONTENTS_CURRENT_90) commands[1] += 400;
	if (contents & SG_HOST_CONTENTS_CURRENT_180) commands[0] -= 400;
	if (contents & SG_HOST_CONTENTS_CURRENT_270) commands[1] -= 400;
	if (contents & SG_HOST_CONTENTS_CURRENT_UP) commands[2] += 400;
	if (contents & SG_HOST_CONTENTS_CURRENT_DOWN) commands[2] -= 400;
	for (axis = 0U; axis < 3U; axis++)
	{
		int velocity = (int)pmove->s.velocity[axis] + commands[axis] / 40;

		if (velocity > SHRT_MAX) velocity = SHRT_MAX;
		if (velocity < SHRT_MIN) velocity = SHRT_MIN;
		pmove->s.velocity[axis] = (short)velocity;
		if (velocity > 0) pmove->s.origin[axis]++;
		if (velocity < 0) pmove->s.origin[axis]--;
	}
}

static void InitWorld(water_fixture_t *fixture, uint32_t wet_contents,
	int blocked)
{
	SetPlane(&fixture->planes[0], 1.0f, 0.0f, 0.0f, 0.0f);
	fixture->node.plane = 0U;
	fixture->node.children[0] = -1;
	fixture->node.children[1] = -2;
	fixture->leaves[0].contents = (int32_t)wet_contents;
	fixture->leaves[0].cluster = 0;
	fixture->leaves[0].area = 1U;
	fixture->leaves[1].contents = blocked ? SG_HOST_CONTENTS_SOLID : 0;
	fixture->leaves[1].cluster = -1;
	fixture->leaves[1].area = 2U;
	fixture->model.headnode = 0;
	Set3(fixture->model.mins.value, -4096.0f, -4096.0f, -4096.0f);
	Set3(fixture->model.maxs.value, 4095.875f, 4095.875f, 4095.875f);
	fixture->world.planes = fixture->planes;
	fixture->world.plane_count = blocked ? 7U : 1U;
	fixture->world.nodes = &fixture->node;
	fixture->world.node_count = 1U;
	fixture->world.leaves = fixture->leaves;
	fixture->world.leaf_count = 2U;
	fixture->world.models = &fixture->model;
	fixture->world.model_count = 1U;
	if (blocked)
	{
		uint32_t side;

		SetPlane(&fixture->planes[1], 1.0f, 0.0f, 0.0f, 2.0f);
		SetPlane(&fixture->planes[2], -1.0f, 0.0f, 0.0f, 2.0f);
		SetPlane(&fixture->planes[3], 0.0f, 1.0f, 0.0f, 128.0f);
		SetPlane(&fixture->planes[4], 0.0f, -1.0f, 0.0f, 128.0f);
		SetPlane(&fixture->planes[5], 0.0f, 0.0f, 1.0f, 128.0f);
		SetPlane(&fixture->planes[6], 0.0f, 0.0f, -1.0f, 128.0f);
		fixture->brush.first_side = 0U;
		fixture->brush.side_count = 6U;
		fixture->brush.contents = SG_HOST_CONTENTS_SOLID;
		for (side = 0U; side < 6U; side++)
		{
			fixture->brush_sides[side].plane = side + 1U;
			fixture->brush_sides[side].texinfo = -1;
		}
		fixture->leaf_brushes[0] = 0U;
		fixture->leaf_brushes[1] = 0U;
		fixture->leaves[0].first_leaf_brush = 0U;
		fixture->leaves[0].leaf_brush_count = 1U;
		fixture->leaves[1].first_leaf_brush = 1U;
		fixture->leaves[1].leaf_brush_count = 1U;
		fixture->world.leaf_brushes = fixture->leaf_brushes;
		fixture->world.leaf_brush_count = 2U;
		fixture->world.brushes = &fixture->brush;
		fixture->world.brush_count = 1U;
		fixture->world.brush_sides = fixture->brush_sides;
		fixture->world.brush_side_count = 6U;
	}
}

int WaterFixtureInit(water_fixture_t *fixture, uint32_t wet_contents,
	float gravity, int blocked, int portal)
{
	sg_rune_model_identity_t identity = Identity(gravity);
	sg_host_collision_error_t host_error;

	if (!fixture)
		return 0;
	memset(fixture, 0, sizeof(*fixture));
	InitWorld(fixture, wet_contents, blocked);
	if (!SG_HostCollisionInit(&fixture->authority, &fixture->world,
		&identity, &host_error))
		return 0;
	fixture->configuration.identity = identity;
	fixture->configuration.cells = fixture->cells;
	fixture->configuration.cell_count = portal ? 2U : 1U;
	fixture->cells[0].stance = SG_RUNE_STANCE_STANDING;
	fixture->cells[0].bsp_cluster = SG_RUNE_BSP_CLUSTER_REF_NONE;
	Set3(fixture->cells[0].bounds.mins.value, -64.0f, -64.0f, -64.0f);
	Set3(fixture->cells[0].bounds.maxs.value, portal ? 0.0f : 64.0f,
		64.0f, 64.0f);
	if (portal)
	{
		fixture->cells[1].stance = SG_RUNE_STANCE_STANDING;
		fixture->cells[1].bsp_cluster = SG_RUNE_BSP_CLUSTER_REF_NONE;
		Set3(fixture->cells[1].bounds.mins.value, 0.0f, -64.0f, -64.0f);
		Set3(fixture->cells[1].bounds.maxs.value, 64.0f, 64.0f, 64.0f);
		fixture->configuration.portals = &fixture->portal;
		fixture->configuration.portal_count = 1U;
		fixture->configuration.vertices = fixture->portal_vertices;
		fixture->configuration.vertex_count = 4U;
		fixture->portal.from_cell = 0U;
		fixture->portal.to_cell = 1U;
		fixture->portal.stance = SG_RUNE_STANCE_STANDING;
		Set3(fixture->portal.plane.normal, 1.0f, 0.0f, 0.0f);
		fixture->portal.plane.distance = 0.0f;
		fixture->portal.first_vertex = 0U;
		fixture->portal.vertex_count = 4U;
		fixture->portal.clearance = 128.0f;
		Set3(fixture->portal_vertices[0].value, 0.0f, -64.0f, -64.0f);
		Set3(fixture->portal_vertices[1].value, 0.0f, 64.0f, -64.0f);
		Set3(fixture->portal_vertices[2].value, 0.0f, 64.0f, 64.0f);
		Set3(fixture->portal_vertices[3].value, 0.0f, -64.0f, 64.0f);
	}
	fixture->semantics.identity = identity;
	fixture->semantics.regions = fixture->regions;
	fixture->semantics.region_count = 2U;
	fixture->semantics.faces = fixture->faces;
	fixture->semantics.face_count = 12U;
	SetRegionBox(fixture, 0U, 0U, -64.0f, 0.0f, 0U,
		portal ? SG_CONFIGURATION_SEMANTIC_PLANE_CELL :
			SG_CONFIGURATION_SEMANTIC_PLANE_CONTENTS_SAMPLE,
		portal ? 0U : 55U, 0U);
	SetRegionBox(fixture, 1U, portal ? 1U : 0U, 0.0f, 64.0f,
		wet_contents,
		portal ? SG_CONFIGURATION_SEMANTIC_PLANE_CELL :
			SG_CONFIGURATION_SEMANTIC_PLANE_CONTENTS_SAMPLE,
		portal ? 1U : 55U, 1U);
	fixture->phases[0] = WaterFixturePhase(fixture, 0U, 0U);
	fixture->phases[1] = WaterFixturePhase(fixture, 1U, 1U);
	fixture->bindings[0].semantic_region_id = fixture->regions[0].id;
	fixture->bindings[0].phase = 0U;
	fixture->bindings[1].semantic_region_id = fixture->regions[1].id;
	fixture->bindings[1].phase = 1U;
	return 1;
}

int WaterFixtureBuild(water_fixture_t *fixture,
	const sg_water_capability_limits_t *limits,
	sg_water_capability_set_t **capabilities,
	sg_water_capability_error_t *error)
{
	return SG_WaterCapabilityBuild(&fixture->authority, WaterFixturePmove,
		&fixture->configuration, &fixture->semantics, fixture->phases, 2U,
		fixture->bindings, 2U, limits, capabilities, error);
}
