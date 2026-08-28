#include "sg_hook_visibility_feasibility_fixture.h"

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

static sg_rune_model_identity_t Identity(void)
{
	sg_rune_model_identity_t identity;

	memset(&identity, 0, sizeof(identity));
	identity.bsp_content_id = UINT64_C(0x4856460101);
	identity.entity_semantics_id = UINT64_C(0x4856460102);
	identity.physics_abi_id = UINT64_C(0x4856460103);
	identity.source_set_identity = UINT64_C(0x4856460104);
	identity.schema_id = UINT64_C(0x4856460105);
	identity.producer_identity = UINT64_C(0x4856460106);
	Set3(identity.standing_hull.mins.value, -1.0f, -1.0f, -1.0f);
	Set3(identity.standing_hull.maxs.value, 1.0f, 1.0f, 1.0f);
	Set3(identity.crouching_hull.mins.value, -1.0f, -1.0f, -1.0f);
	Set3(identity.crouching_hull.maxs.value, 1.0f, 1.0f, 1.0f);
	identity.physics.gravity = 100.0f;
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

static void AddBox(hook_visibility_fixture_t *fixture, uint32_t brush,
	float min_y, float max_y, float min_z, float max_z)
{
	uint32_t first_plane = 1U + brush * 6U;
	uint32_t side;

	SetPlane(&fixture->planes[first_plane], 1.0f, 0.0f, 0.0f, 1.0f);
	SetPlane(&fixture->planes[first_plane + 1U], -1.0f, 0.0f, 0.0f, 0.0f);
	SetPlane(&fixture->planes[first_plane + 2U], 0.0f, 1.0f, 0.0f, max_y);
	SetPlane(&fixture->planes[first_plane + 3U], 0.0f, -1.0f, 0.0f, -min_y);
	SetPlane(&fixture->planes[first_plane + 4U], 0.0f, 0.0f, 1.0f, max_z);
	SetPlane(&fixture->planes[first_plane + 5U], 0.0f, 0.0f, -1.0f, -min_z);
	fixture->brushes[brush].first_side = brush * 6U;
	fixture->brushes[brush].side_count = 6U;
	fixture->brushes[brush].contents = SG_HOST_CONTENTS_SOLID;
	for (side = 0U; side < 6U; side++)
	{
		fixture->brush_sides[brush * 6U + side].plane = first_plane + side;
		fixture->brush_sides[brush * 6U + side].texinfo = (int32_t)brush;
	}
}

int HookVisibilityFixtureInit(hook_visibility_fixture_t *fixture)
{
	sg_rune_model_identity_t identity = Identity();
	sg_host_collision_error_t error;
	uint32_t leaf, brush;

	if (!fixture)
		return 0;
	memset(fixture, 0, sizeof(*fixture));
	SetPlane(&fixture->planes[0], 1.0f, 0.0f, 0.0f, 0.0f);
	fixture->node.plane = 0U;
	fixture->node.children[0] = -1;
	fixture->node.children[1] = -2;
	for (leaf = 0U; leaf < 2U; leaf++)
	{
		fixture->leaves[leaf].contents = SG_HOST_CONTENTS_SOLID;
		fixture->leaves[leaf].first_leaf_brush = leaf * 4U;
		fixture->leaves[leaf].leaf_brush_count = 4U;
		for (brush = 0U; brush < 4U; brush++)
			fixture->leaf_brushes[leaf * 4U + brush] = brush;
	}
	AddBox(fixture, 0U, -64.0f, 0.0f, -64.0f, 14.0f);
	AddBox(fixture, 1U, 0.0f, 64.0f, -64.0f, 14.0f);
	AddBox(fixture, 2U, -64.0f, 0.0f, 14.0f, 64.0f);
	AddBox(fixture, 3U, 0.0f, 64.0f, 14.0f, 64.0f);
	fixture->texinfos[1].flags = SG_HOST_SURFACE_SKY;
	fixture->model.headnode = 0;
	Set3(fixture->model.mins.value, -1024.0f, -128.0f, -128.0f);
	Set3(fixture->model.maxs.value, 128.0f, 128.0f, 128.0f);
	fixture->world.planes = fixture->planes;
	fixture->world.plane_count = 25U;
	fixture->world.nodes = &fixture->node;
	fixture->world.node_count = 1U;
	fixture->world.leaves = fixture->leaves;
	fixture->world.leaf_count = 2U;
	fixture->world.leaf_brushes = fixture->leaf_brushes;
	fixture->world.leaf_brush_count = 8U;
	fixture->world.models = &fixture->model;
	fixture->world.model_count = 1U;
	fixture->world.brushes = fixture->brushes;
	fixture->world.brush_count = 4U;
	fixture->world.brush_sides = fixture->brush_sides;
	fixture->world.brush_side_count = 24U;
	fixture->world.texinfos = fixture->texinfos;
	fixture->world.texinfo_count = 4U;
	if (!SG_HostCollisionInit(&fixture->authority, &fixture->world, &identity,
		&error))
		return 0;
	fixture->controls[0].pitch_min = -1;
	fixture->controls[0].pitch_max = 1;
	fixture->controls[0].yaw_min = -1;
	fixture->controls[0].yaw_max = 1;
	fixture->controls[1].pitch_min = -1;
	fixture->controls[1].pitch_max = 1;
	fixture->controls[1].yaw_min = 32766;
	fixture->controls[1].yaw_max = 32767;
	fixture->rules[0].surface_id = UINT64_C(0x100);
	fixture->rules[0].brush_index = 0U;
	fixture->rules[0].texinfo = 0U;
	fixture->rules[0].classification = SG_HOOK_VISIBILITY_SURFACE_HOOKABLE;
	fixture->rules[1].surface_id = UINT64_C(0x300);
	fixture->rules[1].brush_index = 1U;
	fixture->rules[1].texinfo = 1U;
	fixture->rules[1].classification = SG_HOOK_VISIBILITY_SURFACE_SKY;
	fixture->rules[2].surface_id = UINT64_C(0x400);
	fixture->rules[2].brush_index = 2U;
	fixture->rules[2].texinfo = 2U;
	fixture->rules[2].classification =
		SG_HOOK_VISIBILITY_SURFACE_NONHOOKABLE;
	fixture->rules[3].surface_id = UINT64_C(0x200);
	fixture->rules[3].brush_index = 3U;
	fixture->rules[3].texinfo = 3U;
	fixture->rules[3].classification = SG_HOOK_VISIBILITY_SURFACE_HOOKABLE;
	fixture->sources.collision = &fixture->authority;
	fixture->sources.controls = fixture->controls;
	fixture->sources.control_count = 2U;
	fixture->sources.surface_rules = fixture->rules;
	fixture->sources.surface_rule_count = 4U;
	fixture->sources.origins.mins[0] = -640;
	fixture->sources.origins.maxs[0] = -72;
	fixture->sources.origins.mins[1] = -520;
	fixture->sources.origins.maxs[1] = 520;
	fixture->sources.origins.mins[2] = -520;
	fixture->sources.origins.maxs[2] = 408;
	fixture->sources.stance = SG_RUNE_STANCE_STANDING;
	fixture->sources.fire_law.identity = UINT64_C(0x4856461001);
	fixture->sources.fire_law.angle_authority_id =
		SG_HOOK_VISIBILITY_ANGLE_AUTHORITY_ID;
	fixture->sources.fire_law.standing_view_height = 22.0f;
	fixture->sources.fire_law.crouching_view_height = -2.0f;
	fixture->sources.fire_law.muzzle_forward = 8.0f;
	fixture->sources.fire_law.muzzle_lateral = 8.0f;
	fixture->sources.fire_law.maximum_range = 64.0f;
	fixture->sources.fire_law.trace_epsilon = 1.0f / 32.0f;
	fixture->sources.fire_law.shot_mask = SG_HOOK_VISIBILITY_MASK_SHOT;
	fixture->sources.producer_identity = UINT64_C(0x4856462001);
	fixture->sources.verifier_identity = UINT64_C(0x4856462002);
	return 1;
}
