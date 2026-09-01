#include "../slipgate/sg_rune_compact_mechanisms.h"
#include "../slipgate/sg_rune_compact_mechanisms_build.h"
#include "../slipgate/sg_rune_compact_builder_owner.h"
#include "../slipgate/sg_rune_compact_mechanisms_entities.h"
#include "../slipgate/sg_rune_compact_mechanisms_transitions.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, \
			#expression); \
		return 0; \
	} \
} while (0)

#define ENTITY_COUNT 6U
#define EDGE_COUNT 3U
#define CELL_COUNT 3U
#define FACET_COUNT 17U
#define INCIDENCE_COUNT 18U

struct sg_rune_compact_builder_s
{
	sg_rune_compact_builder_view_t view;
	sg_rune_compact_builder_owner_view_t owner;
	sg_bsp_entity_semantics_t semantics;
	sg_bsp_entity_semantic_t entities[ENTITY_COUNT];
	sg_bsp_entity_semantic_edge_t edges[EDGE_COUNT];
	char strings[4];
	sg_host_collision_authority_t collision;
	int *pmove_calls_out;
	int pmove_calls;
};

struct sg_rune_compact_geometry_s
{
	sg_rune_compact_geometry_view_t view;
	sg_rune_compact_cell_t cells[CELL_COUNT];
	sg_rune_compact_facet_t facets[FACET_COUNT];
	sg_rune_compact_incidence_t incidences[INCIDENCE_COUNT];
	sg_rune_compact_incidence_index_t cell_incidences[INCIDENCE_COUNT];
	sg_rune_q8_vec3_t vertices[3];
	sg_rune_compact_portal_t portals[1];
	sg_rune_compact_source_surface_t source_surfaces[1];
	sg_rune_q8_vec3_t source_surface_vertices[3];
};

static uint32_t FloatBits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static void InitializeIdentity(sg_rune_compact_identity_t *identity)
{
	memset(identity, 0, sizeof(*identity));
	identity->bsp_sha256[0] = 1U;
	identity->bsp_bytes = 1U;
	identity->source_counts.model_count = 2U;
	identity->source_counts.leaf_count = 1U;
	identity->source_counts.plane_count = 1U;
	identity->source_counts.brush_count = 1U;
	identity->source_counts.brush_side_count = 1U;
	identity->source_counts.entity_count = ENTITY_COUNT;
	identity->physics.gravity_bits = FloatBits(650.0f);
	identity->physics.frame_ms = 100U;
	identity->physics.substep_ms = 25U;
	identity->physics_abi_id = UINT64_C(0x701);
}

static void InitializeEntity(sg_bsp_entity_semantic_t *entity,
	uint32_t ordinal, sg_rune_mechanism_kind_t kind,
	sg_mech_node_kind_t role, int authority)
{
	memset(entity, 0, sizeof(*entity));
	entity->source_set_identity = UINT64_C(0x1020304050607080);
	entity->source_entity_ordinal = ordinal;
	entity->canonical_ordinal = ordinal;
	entity->classname = SG_BSP_ENTITY_STRING_NONE;
	entity->targetname = SG_BSP_ENTITY_STRING_NONE;
	entity->required_item = SG_BSP_ENTITY_STRING_NONE;
	entity->spawned_classname = SG_BSP_ENTITY_STRING_NONE;
	entity->destination_map = SG_BSP_ENTITY_STRING_NONE;
	entity->bsp_model = SG_BSP_ENTITY_MODEL_NONE;
	entity->flags = SG_BSP_ENTITY_HAS_MECHANISM;
	if (authority)
		entity->flags |= SG_BSP_ENTITY_CANONICAL_MECHANISM_KIND |
			SG_BSP_ENTITY_TOUCH_ACTIVATED;
	entity->mechanism_kind = kind;
	entity->mechanism_role = role;
}

static void InitializeEdge(sg_bsp_entity_semantic_edge_t *edge,
	uint32_t source, uint32_t destination, uint32_t fanout)
{
	memset(edge, 0, sizeof(*edge));
	edge->source = source;
	edge->destination = destination;
	edge->kind = SG_MECH_EDGE_TARGET;
	edge->name = SG_BSP_ENTITY_STRING_NONE;
	edge->fanout_ordinal = fanout;
}

static sg_rune_compact_builder_t *CreateBuilder(int reverse_edges)
{
	struct sg_rune_compact_builder_s *builder = calloc(1U, sizeof(*builder));
	const uint64_t source_identity = UINT64_C(0x1020304050607080);

	if (builder == NULL)
		return NULL;
	InitializeIdentity(&builder->view.identity);
	builder->owner.identity = builder->view.identity;
	builder->owner.entity_semantics = &builder->semantics;
	builder->owner.collision = &builder->collision;
	builder->pmove_calls_out = &builder->pmove_calls;
	builder->semantics.source_set_identity = source_identity;
	builder->semantics.world.source_set_identity = source_identity;
	builder->semantics.entities = builder->entities;
	builder->semantics.entity_count = ENTITY_COUNT;
	builder->semantics.edges = builder->edges;
	builder->semantics.edge_count = EDGE_COUNT;
	memcpy(builder->strings, "key", sizeof(builder->strings));
	builder->semantics.strings = builder->strings;
	builder->semantics.string_bytes = (uint32_t)sizeof(builder->strings);

	InitializeEntity(&builder->entities[0], 0U, SG_RUNE_MECHANISM_DOOR,
		SG_MECH_NODE_DOOR_MASTER, 1);
	builder->entities[0].origin.value[0] = 12.0f;
	builder->entities[0].flags |= SG_BSP_ENTITY_HAS_BRUSH_MODEL |
		SG_BSP_ENTITY_INVENTORY_GATED;
	builder->entities[0].bsp_model = 1U;
	builder->entities[0].required_item = 0U;
	InitializeEntity(&builder->entities[1], 1U, SG_RUNE_MECHANISM_TELEPORT,
		SG_MECH_NODE_TELEPORTER, 1);
	builder->entities[1].origin.value[0] = 4.0f;
	InitializeEntity(&builder->entities[2], 2U, SG_RUNE_MECHANISM_TELEPORT,
		SG_MECH_NODE_TELEPORT_DEST, 0);
	builder->entities[2].origin.value[0] = 6.0f;
	InitializeEntity(&builder->entities[3], 3U, SG_RUNE_MECHANISM_TELEPORT,
		SG_MECH_NODE_TELEPORT_DEST, 0);
	builder->entities[3].origin.value[0] = 32.0f;
	InitializeEntity(&builder->entities[4], 4U, SG_RUNE_MECHANISM_PUSH,
		SG_MECH_NODE_PUSH, 1);
	builder->entities[4].origin.value[0] = 3.0f;
	builder->entities[4].physics_kind = SG_BSP_ENTITY_PHYSICS_PUSH;
	builder->entities[4].speed = 85.0f;
	builder->entities[4].angles.value[YAW] = 360.0f;
	InitializeEntity(&builder->entities[5], 5U, SG_RUNE_MECHANISM_TRIGGER,
		SG_MECH_NODE_TRIGGER, 0);
	builder->entities[5].origin.value[0] = 14.0f;
	builder->entities[5].flags |= SG_BSP_ENTITY_TOUCH_ACTIVATED;

	if (reverse_edges)
	{
		InitializeEdge(&builder->edges[0], 5U, 0U, 0U);
		InitializeEdge(&builder->edges[1], 1U, 3U, 1U);
		InitializeEdge(&builder->edges[2], 1U, 2U, 0U);
	}
	else
	{
		InitializeEdge(&builder->edges[0], 1U, 2U, 0U);
		InitializeEdge(&builder->edges[1], 1U, 3U, 1U);
		InitializeEdge(&builder->edges[2], 5U, 0U, 0U);
	}
	return (sg_rune_compact_builder_t *)builder;
}

static void InitializeFacet(sg_rune_compact_facet_t *facet, uint32_t axis,
	float distance)
{
	memset(facet, 0, sizeof(*facet));
	facet->plane.normal_bits[axis] = FloatBits(1.0f);
	facet->plane.distance_bits = FloatBits(distance);
	facet->portal.value = SG_RUNE_COMPACT_INDEX_NONE;
}

static void InitializeCell(struct sg_rune_compact_geometry_s *geometry,
	uint32_t cell_index, uint32_t model, int32_t minimum_x, int32_t maximum_x,
	const uint32_t facet_indexes[6])
{
	static const sg_rune_facet_side_t sides[6] = {
		SG_RUNE_FACET_POSITIVE_SIDE, SG_RUNE_FACET_NEGATIVE_SIDE,
		SG_RUNE_FACET_POSITIVE_SIDE, SG_RUNE_FACET_NEGATIVE_SIDE,
		SG_RUNE_FACET_POSITIVE_SIDE, SG_RUNE_FACET_NEGATIVE_SIDE
	};
	const uint32_t first = cell_index * 6U;
	uint32_t local;

	geometry->cells[cell_index].source.model = model;
	geometry->cells[cell_index].bounds.mins.value[0] = minimum_x;
	geometry->cells[cell_index].bounds.maxs.value[0] = maximum_x;
	geometry->cells[cell_index].bounds.mins.value[1] = -80;
	geometry->cells[cell_index].bounds.maxs.value[1] = 80;
	geometry->cells[cell_index].bounds.mins.value[2] = -80;
	geometry->cells[cell_index].bounds.maxs.value[2] = 80;
	geometry->cells[cell_index].incidences.first = first;
	geometry->cells[cell_index].incidences.count = 6U;
	geometry->cells[cell_index].valid_stances = SG_RUNE_STANCE_VALID_ALL;
	for (local = 0U; local < 6U; local++)
	{
		const uint32_t incidence_index = first + local;

		geometry->incidences[incidence_index].cell.value = cell_index;
		geometry->incidences[incidence_index].facet.value = facet_indexes[local];
		geometry->incidences[incidence_index].cell_ordinal = local;
		geometry->incidences[incidence_index].side = sides[local];
		geometry->incidences[incidence_index].boundary = SG_RUNE_BOUNDARY_CLOSED;
		geometry->cell_incidences[incidence_index].value = incidence_index;
	}
}

static sg_rune_compact_geometry_t *CreateGeometry(
	const sg_rune_compact_identity_t *identity)
{
	static const uint32_t cell0_facets[6] = { 0U, 1U, 2U, 3U, 4U, 5U };
	static const uint32_t cell1_facets[6] = { 1U, 6U, 7U, 8U, 9U, 10U };
	static const uint32_t cell2_facets[6] = { 11U, 12U, 13U, 14U, 15U, 16U };
	struct sg_rune_compact_geometry_s *geometry = calloc(1U, sizeof(*geometry));

	if (geometry == NULL)
		return NULL;
	geometry->view.identity = *identity;
	geometry->view.cells = geometry->cells;
	geometry->view.cell_count = CELL_COUNT;
	geometry->view.facets = geometry->facets;
	geometry->view.facet_count = FACET_COUNT;
	geometry->view.incidences = geometry->incidences;
	geometry->view.incidence_count = INCIDENCE_COUNT;
	geometry->view.cell_incidences = geometry->cell_incidences;
	geometry->view.cell_incidence_count = INCIDENCE_COUNT;
	geometry->view.vertices = geometry->vertices;
	geometry->view.vertex_count = 3U;
	geometry->view.portals = geometry->portals;
	geometry->view.portal_count = 1U;
	geometry->view.source_surfaces = geometry->source_surfaces;
	geometry->view.source_surface_count = 1U;
	geometry->view.source_surface_vertices = geometry->source_surface_vertices;
	geometry->view.source_surface_vertex_count = 3U;
	InitializeFacet(&geometry->facets[0], 0U, 0.0f);
	InitializeFacet(&geometry->facets[1], 0U, 10.0f);
	InitializeFacet(&geometry->facets[2], 1U, -10.0f);
	InitializeFacet(&geometry->facets[3], 1U, 10.0f);
	InitializeFacet(&geometry->facets[4], 2U, -10.0f);
	InitializeFacet(&geometry->facets[5], 2U, 10.0f);
	InitializeFacet(&geometry->facets[6], 0U, 20.0f);
	InitializeFacet(&geometry->facets[7], 1U, -10.0f);
	InitializeFacet(&geometry->facets[8], 1U, 10.0f);
	InitializeFacet(&geometry->facets[9], 2U, -10.0f);
	InitializeFacet(&geometry->facets[10], 2U, 10.0f);
	InitializeFacet(&geometry->facets[11], 0U, 30.0f);
	InitializeFacet(&geometry->facets[12], 0U, 40.0f);
	InitializeFacet(&geometry->facets[13], 1U, -10.0f);
	InitializeFacet(&geometry->facets[14], 1U, 10.0f);
	InitializeFacet(&geometry->facets[15], 2U, -10.0f);
	InitializeFacet(&geometry->facets[16], 2U, 10.0f);
	/* Region cells are world-space. The moving bmodel is authenticated by the
	 * portal facet provenance below, not by cell.source.model. */
	InitializeCell(geometry, 0U, 0U, 0, 80, cell0_facets);
	InitializeCell(geometry, 1U, 0U, 80, 160, cell1_facets);
	InitializeCell(geometry, 2U, 0U, 240, 320, cell2_facets);
	geometry->vertices[0].value[0] = 80;
	geometry->vertices[1].value[0] = 80;
	geometry->vertices[1].value[1] = 8;
	geometry->vertices[2].value[0] = 80;
	geometry->vertices[2].value[2] = 8;
	geometry->facets[1].source.kind = SG_RUNE_COMPACT_SOURCE_BSP_PLANE;
	geometry->facets[1].source.value.bsp_plane.model = 0U;
	geometry->facets[1].source.value.bsp_plane.leaf = 0U;
	geometry->facets[1].source.value.bsp_plane.plane = 0U;
	geometry->facets[1].vertices.count = 3U;
	geometry->facets[1].portal.value = 0U;
	geometry->source_surfaces[0].source.model = 1U;
	geometry->source_surfaces[0].source.brush = 0U;
	geometry->source_surfaces[0].source.brush_side = 0U;
	geometry->source_surfaces[0].source.plane = 0U;
	geometry->source_surfaces[0].frame =
		SG_RUNE_COMPACT_SOURCE_SURFACE_MODEL_LOCAL;
	geometry->source_surfaces[0].cell.value = SG_RUNE_COMPACT_INDEX_NONE;
	geometry->source_surfaces[0].parent_surface = SG_RUNE_COMPACT_INDEX_NONE;
	geometry->source_surfaces[0].plane.normal_bits[0] = FloatBits(1.0f);
	geometry->source_surfaces[0].plane.distance_bits = FloatBits(-2.0f);
	geometry->source_surfaces[0].vertices.count = 3U;
	geometry->source_surface_vertices[0].value[0] = -16;
	geometry->source_surface_vertices[1].value[0] = -16;
	geometry->source_surface_vertices[1].value[1] = 8;
	geometry->source_surface_vertices[2].value[0] = -16;
	geometry->source_surface_vertices[2].value[2] = 8;
	geometry->portals[0].facet.value = 1U;
	geometry->portals[0].negative_incidence.value = 1U;
	geometry->portals[0].positive_incidence.value = 6U;
	geometry->portals[0].direction = SG_RUNE_PORTAL_CONTINUITY_BOTH;
	geometry->portals[0].valid_stances = SG_RUNE_STANCE_VALID_ALL;
	return (sg_rune_compact_geometry_t *)geometry;
}

int SG_RuneCompactBuilderRead(const sg_rune_compact_builder_t *builder,
	sg_rune_compact_builder_view_t *view_out)
{
	const struct sg_rune_compact_builder_s *source =
		(const struct sg_rune_compact_builder_s *)builder;

	if (source == NULL || view_out == NULL)
		return 0;
	*view_out = source->view;
	return 1;
}

int SG_RuneCompactBuilderOwnerRead(const sg_rune_compact_builder_t *builder,
	sg_rune_compact_builder_owner_view_t *view_out)
{
	const struct sg_rune_compact_builder_s *source =
		(const struct sg_rune_compact_builder_s *)builder;

	if (source == NULL || view_out == NULL)
		return 0;
	*view_out = source->owner;
	return 1;
}

sg_host_law_result_t SG_RuneCompactBuilderOwnerReplayLocalQ8Pose(
	const sg_rune_compact_builder_t *builder, uint32_t mover_entity_ordinal,
	const sg_host_collision_world_transform_t *transform,
	const sg_rune_q8_vec3_t *local_pose, sg_rune_vec3_t *world_pose_out)
{
	sg_host_law_result_t result;

	(void)builder;
	(void)mover_entity_ordinal;
	(void)transform;
	(void)local_pose;
	(void)world_pose_out;
	memset(&result, 0, sizeof(result));
	result.status = SG_HOST_LAW_EVALUATION_FAILED;
	return result;
}

sg_host_law_result_t SG_RuneCompactBuilderOwnerPmove(
	const sg_rune_compact_builder_t *builder,
	const sg_host_collision_scene_t *scene,
	const sg_host_pmove_request_t *request,
	sg_host_pmove_result_t *result_out, sg_host_pmove_error_t *error_out)
{
	const struct sg_rune_compact_builder_s *source =
		(const struct sg_rune_compact_builder_s *)builder;
	sg_host_law_result_t result;

	(void)scene;
	memset(&result, 0, sizeof(result));
	if (source == NULL || request == NULL || result_out == NULL ||
		error_out == NULL || request->state.pm_type != PM_NORMAL ||
		request->state.gravity != 650 ||
		(request->state.origin[0] != 24 && request->state.origin[0] != 160) ||
		source->pmove_calls_out == NULL)
	{
		result.status = SG_HOST_LAW_EVALUATION_FAILED;
		return result;
	}
	(*source->pmove_calls_out)++;
	memset(result_out, 0, sizeof(*result_out));
	result_out->state = request->state;
	if (request->state.origin[0] == 24)
		result_out->state.origin[0] = 160;
	else
	{
		result_out->state.origin[0] = 256;
		result_out->grounded = 1;
	}
	result_out->origin[0] =
		(float)result_out->state.origin[0] * 0.125f;
	result_out->elapsed_ms = 100U;
	result_out->evaluated_steps = 4U;
	result_out->physics_abi_id = UINT64_C(0x701);
	result_out->gravity = 650.0f;
	*error_out = SG_HOST_PMOVE_ERROR_NONE;
	result.status = SG_HOST_LAW_OK;
	return result;
}

sg_host_law_result_t SG_RuneCompactBuilderOwnerMoverTransport(
	const sg_rune_compact_builder_t *builder,
	const sg_rune_compact_geometry_t *geometry,
	const sg_rune_compact_builder_mover_request_t *request,
	sg_rune_compact_builder_mover_result_t *result_out)
{
	const struct sg_rune_compact_builder_s *source =
		(const struct sg_rune_compact_builder_s *)builder;
	sg_host_law_result_t result;

	memset(&result, 0, sizeof(result));
	if (source == NULL || geometry == NULL || request == NULL ||
		result_out == NULL)
	{
		result.status = SG_HOST_LAW_EVALUATION_FAILED;
		return result;
	}
	/* This build fixture authenticates only movable portal geometry; it has no
	 * collision/replay oracle for a rider.  A carried-support query is thus a
	 * well-formed non-applicable candidate, never a host-law failure. */
	if (request->mode == SG_RUNE_COMPACT_BUILDER_MOVER_MODE_CARRIED_SUPPORT)
	{
		result.status = SG_HOST_LAW_OK;
		return result;
	}
	if (request->mode !=
			SG_RUNE_COMPACT_BUILDER_MOVER_MODE_PORTAL_STATE ||
		request->team_portal != 0 || request->team_master_entity_ordinal !=
			SG_RUNE_COMPACT_INDEX_NONE ||
		request->mover_entity_ordinal != 0U ||
		request->source_surface_ordinal != 0U || request->portal_ordinal != 0U ||
		request->entry_cell.value != 0U || request->exit_cell.value != 1U ||
		request->route_fanout_ordinal != SG_RUNE_COMPACT_INDEX_NONE ||
		request->source_world_vertices_out == NULL ||
		request->destination_world_vertices_out == NULL ||
		request->world_vertex_capacity != 3U ||
		!((request->source_state ==
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE &&
			request->destination_state ==
				SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE) ||
			(request->source_state ==
				SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE &&
			request->destination_state ==
				SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE)))
	{
		result.status = SG_HOST_LAW_EVALUATION_FAILED;
		return result;
	}
	memset(result_out, 0, sizeof(*result_out));
	result_out->mode = request->mode;
	result_out->team_portal = request->team_portal;
	result_out->team_master_entity_ordinal =
		request->team_master_entity_ordinal;
	result_out->applicable = 1;
	result_out->source_state = request->source_state;
	result_out->destination_state = request->destination_state;
	result_out->stance = request->stance;
	result_out->mover_model = 1U;
	result_out->source_surface_ordinal = 0U;
	result_out->portal_ordinal = 0U;
	result_out->source_endpoint_entity_ordinal =
		request->source_endpoint_entity_ordinal;
	result_out->destination_endpoint_entity_ordinal =
		request->destination_endpoint_entity_ordinal;
	result_out->route_fanout_ordinal = request->route_fanout_ordinal;
	result_out->entry_cell = request->entry_cell;
	result_out->exit_cell = request->exit_cell;
	result_out->source_vertex_count = 3U;
	result_out->source_portal_blocked = request->source_state ==
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE ? 1 : 0;
	result_out->destination_portal_blocked = request->destination_state ==
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE ? 1 : 0;
	/* The portal fact carries the exact host state-transition schedule. */
	result_out->elapsed_ms = 300U;
	result_out->failure = SG_RUNE_COMPACT_BUILDER_MOVER_FAILURE_NONE;
	result.status = SG_HOST_LAW_OK;
	return result;
}

void SG_RuneCompactBuilderDestroy(sg_rune_compact_builder_t *builder)
{
	free(builder);
}

int SG_RuneCompactGeometryRead(const sg_rune_compact_geometry_t *geometry,
	sg_rune_compact_geometry_view_t *view_out)
{
	const struct sg_rune_compact_geometry_s *source =
		(const struct sg_rune_compact_geometry_s *)geometry;

	if (source == NULL || view_out == NULL)
		return 0;
	*view_out = source->view;
	return 1;
}

void SG_RuneCompactGeometryDestroy(sg_rune_compact_geometry_t *geometry)
{
	free(geometry);
}

int SG_RuneCompactIdentityMatches(const sg_rune_compact_identity_t *actual,
	const sg_rune_compact_identity_t *expected)
{
	return actual != NULL && expected != NULL &&
		memcmp(actual, expected, sizeof(*actual)) == 0;
}

int SG_HostCollisionClassifyPose(
	const sg_host_collision_authority_t *authority,
	const sg_host_collision_scene_t *scene, const float origin[3],
	sg_rune_stance_t stance, sg_host_collision_pose_t *pose_out)
{
	(void)scene;
	if (authority == NULL || origin == NULL || pose_out == NULL ||
		stance != SG_RUNE_STANCE_STANDING)
		return 0;
	memset(pose_out, 0, sizeof(*pose_out));
	pose_out->valid = 1;
	return 1;
}

static void DisableAllocationFailures(void)
{
	SG_RuneCompactMechanismsTestFailAfter(SIZE_MAX);
	SG_RuneCompactMechanismsBuildTestFailAfter(SIZE_MAX);
	SG_RuneCompactMechanismEntitiesTestFailAfter(SIZE_MAX);
	SG_RuneCompactMechanismTransitionsTestFailAfter(SIZE_MAX);
}

static int CreateInputs(int reverse_edges, sg_rune_compact_builder_t **builder_out,
	sg_rune_compact_geometry_t **geometry_out)
{
	sg_rune_compact_builder_view_t view;

	*builder_out = CreateBuilder(reverse_edges);
	*geometry_out = NULL;
	if (*builder_out == NULL || !SG_RuneCompactBuilderRead(*builder_out, &view))
		return 0;
	*geometry_out = CreateGeometry(&view.identity);
	return *geometry_out != NULL;
}

static void ConfigureAngularRotator(sg_rune_compact_builder_t *builder,
	sg_bsp_entity_angular_mover_kind_t angular_kind)
{
	struct sg_rune_compact_builder_s *mutable_builder =
		(struct sg_rune_compact_builder_s *)builder;

	mutable_builder->entities[0].mechanism_kind = SG_RUNE_MECHANISM_ROTATOR;
	mutable_builder->entities[0].angular_mover.kind = angular_kind;
}

static int Materialize(int reverse_edges,
	sg_rune_compact_mechanisms_t **owner_out,
	sg_rune_compact_mechanisms_error_t *error_out)
{
	sg_rune_compact_builder_t *builder;
	sg_rune_compact_geometry_t *geometry;
	int result;

	if (!CreateInputs(reverse_edges, &builder, &geometry))
	{
		SG_RuneCompactBuilderDestroy(builder);
		return 0;
	}
	result = SG_RuneCompactMechanismsMaterialize(builder, geometry, owner_out,
		error_out);
	SG_RuneCompactBuilderDestroy(builder);
	SG_RuneCompactGeometryDestroy(geometry);
	return result;
}

static int ViewsEqual(const sg_rune_compact_mechanisms_view_t *left,
	const sg_rune_compact_mechanisms_view_t *right)
{
	return memcmp(&left->identity, &right->identity, sizeof(left->identity)) == 0 &&
		left->mechanism_count == right->mechanism_count &&
		left->controller_count == right->controller_count &&
		left->topology_edge_count == right->topology_edge_count &&
		left->transition_count == right->transition_count &&
		memcmp(left->mechanisms, right->mechanisms,
			(size_t)left->mechanism_count * sizeof(*left->mechanisms)) == 0 &&
		memcmp(left->controllers, right->controllers,
			(size_t)left->controller_count * sizeof(*left->controllers)) == 0 &&
		memcmp(left->topology_edges, right->topology_edges,
			(size_t)left->topology_edge_count * sizeof(*left->topology_edges)) == 0 &&
		memcmp(left->transitions, right->transitions,
			(size_t)left->transition_count * sizeof(*left->transitions)) == 0;
}

static int TestOwnerLifetimeAndOrder(void)
{
	sg_rune_compact_mechanisms_t *first = NULL;
	sg_rune_compact_mechanisms_t *second = NULL;
	sg_rune_compact_mechanisms_view_t first_view;
	sg_rune_compact_mechanisms_view_t second_view;
	sg_rune_compact_mechanisms_error_t error;

	DisableAllocationFailures();
	memset(&error, 0, sizeof(error));
	if (!Materialize(0, &first, &error))
	{
		fprintf(stderr, "materialize failed: code=%u domain=%u record=%u\n",
			(unsigned int)error.code, (unsigned int)error.domain,
			(unsigned int)error.record);
		return 0;
	}
	CHECK(error.code == SG_RUNE_COMPACT_MECHANISMS_ERROR_NONE);
	CHECK(Materialize(1, &second, &error));
	CHECK(SG_RuneCompactMechanismsRead(first, &first_view));
	CHECK(SG_RuneCompactMechanismsRead(second, &second_view));
	CHECK(ViewsEqual(&first_view, &second_view));
	CHECK(first_view.mechanism_count == 3U);
	CHECK(first_view.controller_count == 1U);
	CHECK(first_view.topology_edge_count == 3U);
	CHECK(first_view.transition_count == 5U);
	CHECK(first_view.mechanisms[0].required_item == 0U);
	CHECK(first_view.mechanisms[0].controllers.count == 1U);
	CHECK(first_view.controllers[0].controller.entity_ordinal == 5U);
	CHECK(first_view.controllers[0].activation ==
		SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_TOUCH);
	CHECK(first_view.controllers[0].activation_cell.value == 1U);
	CHECK(first_view.mechanisms[1].required_item == SG_BSP_ENTITY_STRING_NONE);
	CHECK(first_view.mechanisms[0].transitions.first == 0U &&
		first_view.mechanisms[0].transitions.count == 2U);
	CHECK(first_view.mechanisms[1].transitions.first == 2U &&
		first_view.mechanisms[1].transitions.count == 2U);
	CHECK(first_view.mechanisms[2].transitions.first == 4U &&
		first_view.mechanisms[2].transitions.count == 1U);
	CHECK(first_view.transitions[0].kind ==
		SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE);
	CHECK(first_view.transitions[0].entry_cell.value == 0U &&
		first_view.transitions[0].exit_cell.value == 1U &&
		first_view.transitions[0].elapsed_ms == UINT64_C(300) &&
		first_view.transitions[0].value.portal_state.travel_ms == 300U &&
		first_view.transitions[0].value.portal_state.recovery_ms == 300U);
	CHECK(first_view.transitions[1].kind ==
		SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE &&
		first_view.transitions[1].source_state ==
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE &&
		first_view.transitions[1].destination_state ==
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE &&
		first_view.transitions[1].value.portal_state.travel_ms == 300U &&
		first_view.transitions[1].value.portal_state.recovery_ms == 300U);
	CHECK(first_view.transitions[2].kind ==
		SG_RUNE_COMPACT_MECHANISM_TRANSITION_TELEPORT);
	CHECK(first_view.transitions[2].value.teleport.destination.entity_ordinal ==
		2U && first_view.transitions[2].entry_cell.value == 0U &&
		first_view.transitions[2].exit_cell.value == 0U);
	CHECK(first_view.transitions[3].value.teleport.destination.entity_ordinal ==
		3U && first_view.transitions[3].exit_cell.value == 2U);
	CHECK(first_view.transitions[4].kind ==
		SG_RUNE_COMPACT_MECHANISM_TRANSITION_PUSH);
	CHECK(first_view.transitions[4].exit_cell.value == 2U);
	SG_RuneCompactMechanismsDestroy(second);
	SG_RuneCompactMechanismsDestroy(first);
	return 1;
}

static int TestAngularRotatorPortalAuthority(void)
{
	sg_rune_compact_builder_t *builder = NULL;
	sg_rune_compact_geometry_t *geometry = NULL;
	sg_rune_compact_mechanisms_t *owner = NULL;
	sg_rune_compact_mechanisms_view_t view;
	sg_rune_compact_mechanisms_error_t error;

	DisableAllocationFailures();
	CHECK(CreateInputs(0, &builder, &geometry));
	ConfigureAngularRotator(builder, SG_BSP_ENTITY_ANGULAR_MOVER_FINITE_DOOR);
	CHECK(SG_RuneCompactMechanismsMaterialize(builder, geometry, &owner,
		&error));
	CHECK(SG_RuneCompactMechanismsRead(owner, &view));
	CHECK(view.mechanisms[0].kind ==
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_ROTATOR);
	CHECK(view.mechanisms[0].transitions.count == 2U);
	CHECK(view.transitions[0].kind ==
		SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE);
	SG_RuneCompactMechanismsDestroy(owner);
	SG_RuneCompactBuilderDestroy(builder);
	SG_RuneCompactGeometryDestroy(geometry);

	builder = NULL;
	geometry = NULL;
	owner = NULL;
	CHECK(CreateInputs(0, &builder, &geometry));
	ConfigureAngularRotator(builder,
		SG_BSP_ENTITY_ANGULAR_MOVER_CONTINUOUS_ROTATOR);
	CHECK(SG_RuneCompactMechanismsMaterialize(builder, geometry, &owner,
		&error));
	CHECK(SG_RuneCompactMechanismsRead(owner, &view));
	CHECK(view.mechanisms[0].kind ==
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_ROTATOR);
	CHECK(view.mechanisms[0].transitions.count == 0U);
	SG_RuneCompactMechanismsDestroy(owner);
	SG_RuneCompactBuilderDestroy(builder);
	SG_RuneCompactGeometryDestroy(geometry);
	return 1;
}

static int TestIdentityMismatchLeavesCandidateUntouched(void)
{
	sg_rune_compact_builder_t *builder;
	sg_rune_compact_geometry_t *geometry;
	struct sg_rune_compact_geometry_s *mutable_geometry;
	sg_rune_compact_mechanisms_candidate_t candidate;
	sg_rune_compact_mechanisms_candidate_t sentinel;
	sg_rune_compact_mechanisms_error_t error;

	DisableAllocationFailures();
	CHECK(CreateInputs(0, &builder, &geometry));
	mutable_geometry = (struct sg_rune_compact_geometry_s *)geometry;
	mutable_geometry->view.identity.bsp_sha256[0] ^= UINT8_C(1);
	memset(&sentinel, 0x5a, sizeof(sentinel));
	candidate = sentinel;
	CHECK(!SG_RuneCompactMechanismsBuildCandidate(builder, geometry, &candidate,
		&error));
	CHECK(memcmp(&candidate, &sentinel, sizeof(candidate)) == 0);
	CHECK(error.code == SG_RUNE_COMPACT_MECHANISMS_ERROR_IDENTITY_MISMATCH);
	CHECK(error.domain == SG_RUNE_COMPACT_MECHANISMS_RECORD_BUILDER);
	SG_RuneCompactBuilderDestroy(builder);
	SG_RuneCompactGeometryDestroy(geometry);
	return 1;
}

static int TestOverflowLeavesCandidateUntouched(void)
{
	sg_rune_compact_builder_t *builder;
	sg_rune_compact_geometry_t *geometry;
	struct sg_rune_compact_geometry_s *mutable_geometry;
	sg_rune_compact_mechanisms_candidate_t candidate;
	sg_rune_compact_mechanisms_candidate_t sentinel;
	sg_rune_compact_mechanisms_error_t error;

	DisableAllocationFailures();
	CHECK(CreateInputs(0, &builder, &geometry));
	mutable_geometry = (struct sg_rune_compact_geometry_s *)geometry;
	mutable_geometry->cells[0].incidences.count = UINT32_MAX;
	memset(&sentinel, 0x6b, sizeof(sentinel));
	candidate = sentinel;
	CHECK(!SG_RuneCompactMechanismsBuildCandidate(builder, geometry, &candidate,
		&error));
	CHECK(memcmp(&candidate, &sentinel, sizeof(candidate)) == 0);
	CHECK(error.code == SG_RUNE_COMPACT_MECHANISMS_ERROR_OVERFLOW);
	CHECK(error.domain == SG_RUNE_COMPACT_MECHANISMS_RECORD_CELL);
	SG_RuneCompactBuilderDestroy(builder);
	SG_RuneCompactGeometryDestroy(geometry);
	return 1;
}

typedef enum allocation_owner_e
{
	ALLOCATION_OWNER_ENTITY = 0,
	ALLOCATION_OWNER_BUILD,
	ALLOCATION_OWNER_TRANSITION,
	ALLOCATION_OWNER_PUBLIC
} allocation_owner_t;

static void SelectAllocationFailure(allocation_owner_t owner, size_t ordinal)
{
	DisableAllocationFailures();
	if (owner == ALLOCATION_OWNER_ENTITY)
		SG_RuneCompactMechanismEntitiesTestFailAfter(ordinal);
	else if (owner == ALLOCATION_OWNER_BUILD)
		SG_RuneCompactMechanismsBuildTestFailAfter(ordinal);
	else if (owner == ALLOCATION_OWNER_TRANSITION)
		SG_RuneCompactMechanismTransitionsTestFailAfter(ordinal);
	else
		SG_RuneCompactMechanismsTestFailAfter(ordinal);
}

static size_t AllocationCount(allocation_owner_t owner)
{
	if (owner == ALLOCATION_OWNER_ENTITY)
		return SG_RuneCompactMechanismEntitiesTestAllocationCount();
	if (owner == ALLOCATION_OWNER_BUILD)
		return SG_RuneCompactMechanismsBuildTestAllocationCount();
	if (owner == ALLOCATION_OWNER_TRANSITION)
		return SG_RuneCompactMechanismTransitionsTestAllocationCount();
	return SG_RuneCompactMechanismsTestAllocationCount();
}

static int TestCandidateAllocationFailures(allocation_owner_t owner)
{
	sg_rune_compact_builder_t *builder;
	sg_rune_compact_geometry_t *geometry;
	sg_rune_compact_mechanisms_candidate_t candidate;
	sg_rune_compact_mechanisms_candidate_t sentinel;
	sg_rune_compact_mechanisms_error_t error;
	size_t allocations;
	size_t ordinal;

	SelectAllocationFailure(owner, SIZE_MAX);
	CHECK(CreateInputs(0, &builder, &geometry));
	memset(&candidate, 0, sizeof(candidate));
	CHECK(SG_RuneCompactMechanismsBuildCandidate(builder, geometry, &candidate,
		&error));
	allocations = AllocationCount(owner);
	CHECK(allocations != 0U);
	SG_RuneCompactMechanismsReleaseCandidate(&candidate);
	SG_RuneCompactBuilderDestroy(builder);
	SG_RuneCompactGeometryDestroy(geometry);
	memset(&sentinel, 0xa5, sizeof(sentinel));
	for (ordinal = 0U; ordinal < allocations; ordinal++)
	{
		CHECK(CreateInputs(0, &builder, &geometry));
		candidate = sentinel;
		SelectAllocationFailure(owner, ordinal);
		CHECK(!SG_RuneCompactMechanismsBuildCandidate(builder, geometry,
			&candidate, &error));
		CHECK(memcmp(&candidate, &sentinel, sizeof(candidate)) == 0);
		CHECK(error.code == SG_RUNE_COMPACT_MECHANISMS_ERROR_OUT_OF_MEMORY);
		SG_RuneCompactBuilderDestroy(builder);
		SG_RuneCompactGeometryDestroy(geometry);
	}
	return 1;
}

static int TestPublicAllocationFailures(void)
{
	sg_rune_compact_mechanisms_t *owner = NULL;
	sg_rune_compact_mechanisms_error_t error;
	size_t allocations;
	size_t ordinal;

	SelectAllocationFailure(ALLOCATION_OWNER_PUBLIC, SIZE_MAX);
	CHECK(Materialize(0, &owner, &error));
	allocations = AllocationCount(ALLOCATION_OWNER_PUBLIC);
	CHECK(allocations != 0U);
	SG_RuneCompactMechanismsDestroy(owner);
	for (ordinal = 0U; ordinal < allocations; ordinal++)
	{
		sg_rune_compact_mechanisms_t *const sentinel =
			(sg_rune_compact_mechanisms_t *)(uintptr_t)0x1234U;
		sg_rune_compact_mechanisms_t *output = sentinel;

		SelectAllocationFailure(ALLOCATION_OWNER_PUBLIC, ordinal);
		CHECK(!Materialize(0, &output, &error));
		CHECK(output == sentinel);
		CHECK(error.code == SG_RUNE_COMPACT_MECHANISMS_ERROR_OUT_OF_MEMORY);
	}
	return 1;
}

int main(void)
{
	if (!TestOwnerLifetimeAndOrder() ||
		!TestAngularRotatorPortalAuthority() ||
		!TestIdentityMismatchLeavesCandidateUntouched() ||
		!TestOverflowLeavesCandidateUntouched() ||
		!TestCandidateAllocationFailures(ALLOCATION_OWNER_ENTITY) ||
		!TestCandidateAllocationFailures(ALLOCATION_OWNER_BUILD) ||
		!TestCandidateAllocationFailures(ALLOCATION_OWNER_TRANSITION) ||
		!TestPublicAllocationFailures())
		return 1;
	puts("compact mechanism candidate builder tests passed");
	return 0;
}
