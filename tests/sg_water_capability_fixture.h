#ifndef SG_WATER_CAPABILITY_FIXTURE_H
#define SG_WATER_CAPABILITY_FIXTURE_H

#include "slipgate/sg_water_capability.h"

typedef struct water_fixture_s
{
	sg_bsp_world_t world;
	sg_bsp_plane_t planes[7];
	sg_bsp_node_t node;
	sg_bsp_leaf_t leaves[2];
	uint32_t leaf_brushes[2];
	sg_bsp_model_t model;
	sg_bsp_brush_t brush;
	sg_bsp_brush_side_t brush_sides[6];
	sg_host_collision_authority_t authority;
	sg_configuration_space_t configuration;
	sg_configuration_cell_t cells[2];
	sg_configuration_portal_t portal;
	sg_rune_vec3_t portal_vertices[4];
	sg_configuration_semantics_t semantics;
	sg_configuration_semantic_region_t regions[2];
	sg_configuration_semantic_face_t faces[12];
	sg_rune_phase_basis_t phases[2];
	sg_water_phase_binding_t bindings[2];
} water_fixture_t;

int WaterFixtureInit(water_fixture_t *fixture, uint32_t wet_contents,
	float gravity, int blocked, int portal);
int WaterFixtureBuild(water_fixture_t *fixture,
	const sg_water_capability_limits_t *limits,
	sg_water_capability_set_t **capabilities,
	sg_water_capability_error_t *error);
sg_rune_phase_basis_t WaterFixturePhase(const water_fixture_t *fixture,
	uint32_t index, uint32_t region_index);
void WaterFixturePmove(pmove_t *pmove);

#endif
