#ifndef SG_HOOK_VISIBILITY_FEASIBILITY_FIXTURE_H
#define SG_HOOK_VISIBILITY_FEASIBILITY_FIXTURE_H

#include "slipgate/sg_hook_visibility_feasibility.h"

typedef struct hook_visibility_fixture_s
{
	sg_bsp_world_t world;
	sg_bsp_plane_t planes[32];
	sg_bsp_node_t node;
	sg_bsp_leaf_t leaves[2];
	uint32_t leaf_brushes[10];
	sg_bsp_model_t model;
	sg_bsp_brush_t brushes[5];
	sg_bsp_brush_side_t brush_sides[31];
	sg_bsp_texinfo_t texinfos[5];
	sg_host_collision_authority_t authority;
	sg_hook_visibility_control_root_t controls[2];
	sg_hook_visibility_surface_rule_t rules[5];
	sg_hook_visibility_feasibility_sources_t sources;
} hook_visibility_fixture_t;

int HookVisibilityFixtureInit(hook_visibility_fixture_t *fixture);
void HookVisibilityHostReferenceAngleBits(uint16_t code,
	uint32_t *sine_bits_out, uint32_t *cosine_bits_out);
void HookVisibilityProductionDirection(int16_t pitch, int16_t yaw,
	float forward[3], float right[3]);

#endif
