#include "../slipgate/sg_rune_compact_mechanisms_transitions.h"

#include "../slipgate/sg_rune_compact_builder_owner.h"
#include "../slipgate/sg_rune_compact_localize.h"

#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, \
			#expression); \
		return 0; \
	} \
} while (0)

struct sg_rune_compact_builder_s
{
	sg_rune_compact_builder_owner_view_t owner;
	int *pmove_calls;
	int pmove_mode;
	int *mover_calls;
	int mover_mode;
	int *owner_read_calls;
	int revoke_owner_on_read;
	int team_portal_fixture;
	int second_door_portal_fixture;
};

struct sg_rune_compact_geometry_s
{
	sg_rune_compact_geometry_view_t view;
};

typedef struct fixture_s
{
	struct sg_rune_compact_builder_s builder;
	struct sg_rune_compact_geometry_s geometry;
	sg_bsp_entity_semantics_t entities_view;
	sg_bsp_entity_semantic_t entities[11];
	sg_bsp_entity_semantic_edge_t edges[8];
	sg_rune_compact_cell_t cells[4];
	sg_rune_compact_facet_t facets[3];
	sg_rune_compact_incidence_t incidences[6];
	sg_rune_q8_vec3_t vertices[9];
	sg_rune_compact_portal_t portals[3];
	sg_rune_compact_source_surface_t source_surfaces[4];
	sg_rune_q8_vec3_t source_surface_vertices[12];
	sg_rune_compact_mechanism_authority_t mechanisms[6];
	sg_host_collision_authority_t collision;
	int pmove_calls;
	int mover_calls;
	int owner_read_calls;
	int revoke_owner_on_read;
	int placement_calls;
	int placement_ok;
} fixture_t;

static fixture_t *active_fixture;

static uint32_t FloatBits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

int SG_RuneCompactBuilderOwnerRead(const sg_rune_compact_builder_t *builder,
	sg_rune_compact_builder_owner_view_t *view_out)
{
	const struct sg_rune_compact_builder_s *source =
		(const struct sg_rune_compact_builder_s *)builder;

	if (source == NULL || view_out == NULL)
		return 0;
	*view_out = source->owner;
	if (source->owner_read_calls != NULL)
		(*source->owner_read_calls)++;
	if (source->revoke_owner_on_read != 0 && source->owner_read_calls != NULL &&
		*source->owner_read_calls >= source->revoke_owner_on_read)
		view_out->identity.physics_abi_id++;
	return 1;
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

int SG_RuneCompactIdentityMatches(const sg_rune_compact_identity_t *actual,
	const sg_rune_compact_identity_t *expected)
{
	return actual != NULL && expected != NULL &&
		actual->source_counts.model_count == expected->source_counts.model_count &&
		actual->source_counts.entity_count == expected->source_counts.entity_count &&
		actual->physics.gravity_bits == expected->physics.gravity_bits &&
		actual->physics.frame_ms == expected->physics.frame_ms &&
		actual->physics.substep_ms == expected->physics.substep_ms &&
		actual->physics_abi_id == expected->physics_abi_id;
}

sg_rune_compact_localize_status_t SG_RuneCompactLocalize(
	const sg_rune_compact_model_t *model, const sg_rune_q8_vec3_t *point,
	sg_rune_compact_location_t *location_out)
{
	uint32_t cell;

	if (model == NULL || point == NULL || location_out == NULL ||
		model->cell_count != 4U)
		return SG_RUNE_COMPACT_LOCALIZE_INVALID_ARGUMENT;
	if (point->value[0] >= 0 && point->value[0] < 80)
	{
		if (point->value[2] >= 0 && point->value[2] < 80)
			cell = 0U;
		else if (point->value[2] >= 80 && point->value[2] < 160)
			cell = 1U;
		else
			return SG_RUNE_COMPACT_LOCALIZE_NOT_FOUND;
	}
	else if (point->value[0] >= 240 && point->value[0] < 320)
		cell = 2U;
	else
		return SG_RUNE_COMPACT_LOCALIZE_NOT_FOUND;
	memset(location_out, 0, sizeof(*location_out));
	location_out->cell.value = cell;
	location_out->valid_stances = SG_RUNE_STANCE_VALID_STANDING;
	return SG_RUNE_COMPACT_LOCALIZE_OK;
}

sg_rune_compact_localize_status_t SG_RuneCompactLocalizeBinary32(
	const sg_rune_compact_model_t *model, const sg_rune_vec3_t *point,
	sg_rune_compact_location_t *location_out)
{
	sg_rune_q8_vec3_t q8;
	uint32_t axis;

	if (point == NULL)
		return SG_RUNE_COMPACT_LOCALIZE_INVALID_ARGUMENT;
	for (axis = 0U; axis < 3U; axis++)
		q8.value[axis] = (int32_t)(point->value[axis] * 8.0f);
	return SG_RuneCompactLocalize(model, &q8, location_out);
}

int SG_HostCollisionClassifyPose(const sg_host_collision_authority_t *authority,
	const sg_host_collision_scene_t *scene, const float origin[3],
	sg_rune_stance_t stance, sg_host_collision_pose_t *pose_out)
{
	fixture_t *fixture = active_fixture;

	(void)scene;
	if (fixture == NULL || authority != &fixture->collision || origin == NULL ||
		pose_out == NULL || stance != SG_RUNE_STANCE_STANDING)
		return 0;
	if ((fixture->placement_calls == 0 && (origin[0] != 4.0f ||
		origin[1] != 0.0f || origin[2] != 10.0f)) ||
		(fixture->placement_calls == 1 && (origin[0] != 4.0f ||
		origin[1] != 0.0f || origin[2] != 0.0f)))
		return 0;
	fixture->placement_calls++;
	memset(pose_out, 0, sizeof(*pose_out));
	pose_out->valid = fixture->placement_ok;
	return 1;
}

sg_host_law_result_t SG_RuneCompactBuilderOwnerReplayLocalQ8Pose(
	const sg_rune_compact_builder_t *builder, uint32_t mover_entity_ordinal,
	const sg_host_collision_world_transform_t *transform,
	const sg_rune_q8_vec3_t *local_pose, sg_rune_vec3_t *world_pose_out)
{
	sg_host_law_result_t result;
	float local[3];
	uint32_t local_axis;
	uint32_t world_axis;

	memset(&result, 0, sizeof(result));
	if (active_fixture == NULL || builder !=
		(const sg_rune_compact_builder_t *)&active_fixture->builder ||
		mover_entity_ordinal >= 3U || transform == NULL || local_pose == NULL ||
		world_pose_out == NULL)
	{
		result.status = SG_HOST_LAW_EVALUATION_FAILED;
		return result;
	}
	for (local_axis = 0U; local_axis < 3U; local_axis++)
		local[local_axis] = (float)local_pose->value[local_axis] * 0.125f;
	for (world_axis = 0U; world_axis < 3U; world_axis++)
		world_pose_out->value[world_axis] = local[0] * transform->axis[0][world_axis] +
			local[1] * transform->axis[1][world_axis] +
			local[2] * transform->axis[2][world_axis] +
			transform->origin[world_axis];
	result.status = SG_HOST_LAW_OK;
	return result;
}

/* The candidate transform is deliberately supplied by the builder-owner
 * boundary.  This fixture allows only its simple authenticated translation;
 * derivation never performs that transform itself. */
sg_host_law_result_t SG_RuneCompactBuilderOwnerTransformModelLocalQ8(
	const sg_rune_compact_builder_t *builder, uint32_t mover_entity_ordinal,
	const sg_rune_q8_vec3_t *local_vertices, uint32_t vertex_count,
	sg_rune_vec3_t *world_vertices_out, sg_rune_bounds_t *world_bounds_out)
{
	sg_host_law_result_t result;
	uint32_t vertex;

	memset(&result, 0, sizeof(result));
	if (active_fixture == NULL || builder !=
		(const sg_rune_compact_builder_t *)&active_fixture->builder ||
		local_vertices == NULL || vertex_count == 0U || world_vertices_out == NULL ||
		world_bounds_out == NULL || mover_entity_ordinal >= 3U)
	{
		result.status = SG_HOST_LAW_EVALUATION_FAILED;
		return result;
	}
	for (vertex = 0U; vertex < vertex_count; vertex++)
	{
		uint32_t axis;

		for (axis = 0U; axis < 3U; axis++)
		{
			const float world = (float)local_vertices[vertex].value[axis] *
				0.125f + active_fixture->entities[mover_entity_ordinal]
					.origin.value[axis];

			world_vertices_out[vertex].value[axis] = world;
			if (vertex == 0U)
				world_bounds_out->mins.value[axis] =
					world_bounds_out->maxs.value[axis] = world;
			else if (world < world_bounds_out->mins.value[axis])
				world_bounds_out->mins.value[axis] = world;
			else if (world > world_bounds_out->maxs.value[axis])
				world_bounds_out->maxs.value[axis] = world;
		}
	}
	result.status = SG_HOST_LAW_OK;
	return result;
}

/* The transport oracle is the sole authority for a mover state or carried
 * player result.  The source catalog merely supplies candidates. */
sg_host_law_result_t SG_RuneCompactBuilderOwnerMoverTransport(
	const sg_rune_compact_builder_t *builder,
	const sg_rune_compact_geometry_t *geometry,
	const sg_rune_compact_builder_mover_request_t *request,
	sg_rune_compact_builder_mover_result_t *result_out)
{
	sg_host_law_result_t result;
	fixture_t *fixture = active_fixture;
	uint32_t axis;

	memset(&result, 0, sizeof(result));
	if (fixture == NULL || builder !=
		(const sg_rune_compact_builder_t *)&fixture->builder || geometry !=
		(const sg_rune_compact_geometry_t *)&fixture->geometry || request == NULL ||
		result_out == NULL ||
		(request->mover_entity_ordinal >= 3U &&
			!(fixture->builder.team_portal_fixture != 0 &&
				request->mover_entity_ordinal == 10U)) ||
		!(((request->source_state ==
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE &&
			request->destination_state ==
				SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE) ||
			(request->source_state ==
				SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE &&
			request->destination_state ==
				SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE)) ||
			((request->mover_entity_ordinal == 2U ||
			  (request->mover_entity_ordinal == 0U &&
			   fixture->entities[0].angular_mover.kind ==
				SG_BSP_ENTITY_ANGULAR_MOVER_CONTINUOUS_ROTATOR)) &&
			request->source_state ==
				SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE &&
			request->destination_state ==
				SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE)))
	{
		result.status = SG_HOST_LAW_EVALUATION_FAILED;
		return result;
	}
	(*fixture->builder.mover_calls)++;
	const float mover_origin_x = request->mover_entity_ordinal == 1U ? 2.0f :
		4.0f;
	memset(result_out, 0, sizeof(*result_out));
	result_out->mode = request->mode;
	result_out->team_portal = request->team_portal;
	result_out->team_master_entity_ordinal =
		request->team_master_entity_ordinal;
	if (fixture->builder.team_portal_fixture != 0 &&
		fixture->builder.mover_mode == 4)
		result_out->team_master_entity_ordinal = 1U;
	result_out->source_state = request->source_state;
	result_out->destination_state = request->destination_state;
	result_out->stance = request->stance;
	result_out->mover_model = request->mover_entity_ordinal + 1U;
	result_out->source_surface_ordinal = request->source_surface_ordinal;
	result_out->source_vertex_count = fixture->source_surfaces[
		request->source_surface_ordinal].vertices.count;
	result_out->portal_ordinal = request->portal_ordinal;
	result_out->source_endpoint_entity_ordinal =
		request->source_endpoint_entity_ordinal;
	result_out->destination_endpoint_entity_ordinal =
		request->destination_endpoint_entity_ordinal;
	result_out->route_fanout_ordinal = request->route_fanout_ordinal;
	if (request->mode == SG_RUNE_COMPACT_BUILDER_MOVER_MODE_CARRIED_SUPPORT &&
		fixture->builder.mover_mode >= 14 && fixture->builder.mover_mode <= 16)
	{
		result_out->applicable = 1;
		result_out->failure = SG_RUNE_COMPACT_BUILDER_MOVER_FAILURE_NO_LANDING;
		if (fixture->builder.mover_mode == 14)
			result_out->mode = SG_RUNE_COMPACT_BUILDER_MOVER_MODE_PORTAL_STATE;
		else if (fixture->builder.mover_mode == 15)
			result_out->source_state =
				SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
		else
			result_out->stance = request->stance == SG_RUNE_STANCE_STANDING ?
				SG_RUNE_STANCE_CROUCHING : SG_RUNE_STANCE_STANDING;
		result.status = SG_HOST_LAW_OK;
		return result;
	}
	if (fixture->builder.mover_mode == 2 &&
		request->mover_entity_ordinal == 2U)
		result_out->route_fanout_ordinal ^= UINT32_C(1);
	if (fixture->builder.mover_mode == 1 && request->mode ==
		SG_RUNE_COMPACT_BUILDER_MOVER_MODE_CARRIED_SUPPORT)
	{
		result_out->applicable = 1;
		result_out->failure = SG_RUNE_COMPACT_BUILDER_MOVER_FAILURE_NO_LANDING;
		result.status = SG_HOST_LAW_OK;
		return result;
	}
	if (fixture->builder.mover_mode == 13 && request->mode ==
		SG_RUNE_COMPACT_BUILDER_MOVER_MODE_CARRIED_SUPPORT)
	{
		result_out->applicable = 0;
		result_out->failure = SG_RUNE_COMPACT_BUILDER_MOVER_FAILURE_NO_LANDING;
		result.status = SG_HOST_LAW_OK;
		return result;
	}
	if (request->mode == SG_RUNE_COMPACT_BUILDER_MOVER_MODE_PORTAL_STATE)
	{
		const int team_portal = fixture->builder.team_portal_fixture != 0;
		const int second_door_portal =
			fixture->builder.second_door_portal_fixture != 0;
		const uint32_t expected_mover = team_portal ? 10U : 0U;
		const uint32_t expected_surface = team_portal ? 3U :
			request->portal_ordinal;

		if (request->team_portal != team_portal ||
			(team_portal && request->team_master_entity_ordinal != 0U) ||
			request->mover_entity_ordinal != expected_mover ||
			request->source_surface_ordinal != expected_surface ||
			request->portal_ordinal > (second_door_portal ? 1U : 0U) ||
			request->entry_cell.value != request->portal_ordinal ||
			request->exit_cell.value != request->portal_ordinal + 1U ||
			request->route_fanout_ordinal != SG_RUNE_COMPACT_INDEX_NONE ||
			request->source_world_vertices_out == NULL ||
			request->destination_world_vertices_out == NULL ||
			request->world_vertex_capacity != 3U)
		{
			result_out->applicable = 0;
			result.status = SG_HOST_LAW_OK;
			return result;
		}
		result_out->applicable = 1;
		if (fixture->builder.team_portal_fixture != 0)
			result_out->mover_model = 4U;
		result_out->entry_cell.value = request->portal_ordinal;
		result_out->exit_cell.value = request->portal_ordinal + 1U;
		result_out->source_vertex_count = 3U;
		result_out->source_portal_blocked = request->source_state ==
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE ? 1 : 0;
		result_out->destination_portal_blocked = request->destination_state ==
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE ? 1 : 0;
		if (fixture->builder.mover_mode == 3)
			result_out->destination_portal_blocked = 1;
		/* Full host-owned inactive-to-active mover schedule, rather than a
		 * directional traversal or copied mechanism timing field. */
		result_out->elapsed_ms = 300U;
		result_out->failure = SG_RUNE_COMPACT_BUILDER_MOVER_FAILURE_NONE;
		result.status = SG_HOST_LAW_OK;
		return result;
	}
	if (request->stance == SG_RUNE_STANCE_CROUCHING &&
		fixture->builder.mover_mode != 10)
	{
		result_out->applicable = 0;
		result.status = SG_HOST_LAW_OK;
		return result;
	}
	/* The second portal fixture deliberately leaves the first door-owned
	 * model-local surface unsupported.  It is a legal candidate, but not an
	 * authenticated rider placement; the host answers non-applicable rather
	 * than turning that absence into a derivation failure. */
	if (fixture->builder.second_door_portal_fixture != 0 &&
		request->mover_entity_ordinal == 0U &&
		request->source_surface_ordinal == 0U)
	{
		if (fixture->builder.mover_mode == 12)
		{
			result_out->applicable = 1;
			result_out->failure =
				SG_RUNE_COMPACT_BUILDER_MOVER_FAILURE_NO_LANDING;
			result.status = SG_HOST_LAW_OK;
			return result;
		}
		result_out->applicable = 0;
		result.status = SG_HOST_LAW_OK;
		return result;
	}
	if (fixture->builder.team_portal_fixture != 0 &&
		(request->mover_entity_ordinal == 0U ||
		 request->mover_entity_ordinal == 10U))
	{
		result_out->applicable = 0;
		result.status = SG_HOST_LAW_OK;
		return result;
	}
	if (request->mode != SG_RUNE_COMPACT_BUILDER_MOVER_MODE_CARRIED_SUPPORT ||
		request->source_surface_ordinal != request->mover_entity_ordinal +
			(fixture->builder.second_door_portal_fixture != 0 ? 1U : 0U) ||
		request->portal_ordinal != SG_RUNE_COMPACT_INDEX_NONE ||
		request->entry_cell.value != SG_RUNE_COMPACT_INDEX_NONE ||
		request->exit_cell.value != SG_RUNE_COMPACT_INDEX_NONE ||
		request->support_pose_mode !=
			SG_RUNE_COMPACT_BUILDER_SUPPORT_POSE_CANONICAL ||
		request->stance >= SG_RUNE_STANCE_COUNT ||
		((request->mover_entity_ordinal == 0U ||
			request->mover_entity_ordinal == 1U) !=
			(request->source_endpoint_entity_ordinal == SG_RUNE_COMPACT_INDEX_NONE &&
			request->destination_endpoint_entity_ordinal == SG_RUNE_COMPACT_INDEX_NONE &&
			request->route_fanout_ordinal == SG_RUNE_COMPACT_INDEX_NONE)) ||
		((request->mover_entity_ordinal == 2U) !=
			((request->source_endpoint_entity_ordinal == 7U &&
				request->destination_endpoint_entity_ordinal == 8U &&
				(request->route_fanout_ordinal == 0U ||
				request->route_fanout_ordinal == 1U)) ||
				(request->source_endpoint_entity_ordinal == 8U &&
				request->destination_endpoint_entity_ordinal == 9U &&
				request->route_fanout_ordinal == 2U) ||
				(request->source_endpoint_entity_ordinal == 9U &&
				request->destination_endpoint_entity_ordinal == 7U &&
				request->route_fanout_ordinal == 3U))))
	{
		result.status = SG_HOST_LAW_EVALUATION_FAILED;
		return result;
	}
	result_out->applicable = 1;
	result_out->portal_ordinal = SG_RUNE_COMPACT_INDEX_NONE;
	result_out->entry_cell.value = 0U;
	result_out->exit_cell.value = 1U;
	result_out->source_vertex_count = 3U;
	result_out->source_mover_transform.origin[0] = mover_origin_x;
	result_out->destination_mover_transform.origin[0] = mover_origin_x;
	result_out->destination_mover_transform.origin[2] = 12.0f;
	result_out->source_support_local = fixture->source_surface_vertices[
		fixture->source_surfaces[request->source_surface_ordinal].vertices.first];
	result_out->source_player_local = result_out->source_support_local;
	result_out->source_player_local.value[2]++;
	result_out->destination_support_local = result_out->source_support_local;
	result_out->destination_player_local = result_out->source_player_local;
	for (axis = 0U; axis < 3U; axis++)
	{
		result_out->source_mover_transform.axis[axis][axis] = 1.0f;
		result_out->destination_mover_transform.axis[axis][axis] = 1.0f;
		result_out->source_player_world.value[axis] =
			(float)result_out->source_player_local.value[axis] * 0.125f +
			result_out->source_mover_transform.origin[axis];
		result_out->destination_player_world.value[axis] =
			(float)result_out->destination_player_local.value[axis] * 0.125f +
			result_out->destination_mover_transform.origin[axis];
		result_out->source_support_world.value[axis] =
			(float)result_out->source_support_local.value[axis] * 0.125f +
			result_out->source_mover_transform.origin[axis];
		result_out->destination_support_world.value[axis] =
			(float)result_out->destination_support_local.value[axis] * 0.125f +
			result_out->destination_mover_transform.origin[axis];
	}
	if (fixture->builder.mover_mode == 5)
	{
		/* A self-consistent transport callback cannot launder endpoints that
		 * its asserted transform cannot produce. */
		result_out->source_mover_transform.origin[0] = 16777216.0f;
		result_out->destination_mover_transform.origin[0] = 16777218.0f;
	}
	if (fixture->builder.mover_mode == 6)
	{
		/* Both destination local poses move together, so only the full
		 * source/destination rigid-pose comparison detects this drift. */
		result_out->destination_player_local.value[2] = 8;
		result_out->destination_support_local.value[2] = 8;
		result_out->destination_player_world.value[2] = 13.0f;
		result_out->destination_support_world.value[2] = 13.0f;
	}
	if (fixture->builder.mover_mode == 7)
	{
		/* This response replays exactly, but its rigid support pose is outside
		 * the cited model-local source root. */
		result_out->source_player_local.value[0] = 16;
		result_out->source_support_local.value[0] = 16;
		result_out->destination_player_local.value[0] = 16;
		result_out->destination_support_local.value[0] = 16;
		result_out->source_player_world.value[0] = mover_origin_x + 2.0f;
		result_out->source_support_world.value[0] = mover_origin_x + 2.0f;
		result_out->destination_player_world.value[0] = mover_origin_x + 2.0f;
		result_out->destination_support_world.value[0] = mover_origin_x + 2.0f;
	}
	if (fixture->builder.mover_mode == 9 &&
		request->mover_entity_ordinal == 2U &&
		request->source_endpoint_entity_ordinal == 7U)
	{
		/* The endpoint/fanout key is lower, but its authenticated common cells
		 * are higher.  Canonical transition order must choose common facts first. */
		result_out->entry_cell.value = 2U;
		result_out->exit_cell.value = 2U;
		result_out->source_mover_transform.origin[0] = 32.0f;
		result_out->source_mover_transform.origin[2] = 0.0f;
		result_out->destination_mover_transform.origin[0] = 32.0f;
		result_out->destination_mover_transform.origin[2] = 0.0f;
		for (axis = 0U; axis < 3U; axis++) {
			result_out->source_player_world.value[axis] =
				(float)result_out->source_player_local.value[axis] * 0.125f +
				result_out->source_mover_transform.origin[axis];
			result_out->destination_player_world.value[axis] =
				(float)result_out->destination_player_local.value[axis] * 0.125f +
				result_out->destination_mover_transform.origin[axis];
			result_out->source_support_world.value[axis] =
				(float)result_out->source_support_local.value[axis] * 0.125f +
				result_out->source_mover_transform.origin[axis];
			result_out->destination_support_world.value[axis] =
				(float)result_out->destination_support_local.value[axis] * 0.125f +
				result_out->destination_mover_transform.origin[axis];
		}
	}
	result_out->elapsed_ms = 100U;
	result_out->start_supported = 1;
	result_out->end_supported = 1;
	result_out->swept_static_clear = 1;
	result_out->failure = SG_RUNE_COMPACT_BUILDER_MOVER_FAILURE_NONE;
	result.status = SG_HOST_LAW_OK;
	return result;
}

sg_host_law_result_t SG_RuneCompactBuilderOwnerPmove(
	const sg_rune_compact_builder_t *builder, const sg_host_collision_scene_t *scene,
	const sg_host_pmove_request_t *request, sg_host_pmove_result_t *result_out,
	sg_host_pmove_error_t *error_out)
{
	const struct sg_rune_compact_builder_s *source =
		(const struct sg_rune_compact_builder_s *)builder;
	sg_host_law_result_t result;
	int call;

	(void)scene;
	memset(&result, 0, sizeof(result));
	if (source == NULL || request == NULL || result_out == NULL ||
		error_out == NULL || request->state.pm_type != PM_NORMAL ||
		request->state.gravity != 650 ||
		memcmp(&request->state, &request->previous_state,
			sizeof(request->state)) != 0)
	{
		result.status = SG_HOST_LAW_EVALUATION_FAILED;
		return result;
	}
	call = ++*source->pmove_calls;
	if (call > 2)
	{
		result.status = SG_HOST_LAW_EVALUATION_FAILED;
		return result;
	}
	memset(result_out, 0, sizeof(*result_out));
	result_out->state = request->state;
	if (source->pmove_mode == 2 && call == 1)
		result_out->state.pm_type = PM_SPECTATOR;
	else if (source->pmove_mode == 1 && call == 2)
		result_out->state.origin[0] = 24;
	else if (call == 1)
		result_out->state.origin[0] = 160;
	else
		result_out->state.origin[0] = 256;
	result_out->origin[0] = (float)result_out->state.origin[0] * 0.125f;
	result_out->origin[1] = (float)result_out->state.origin[1] * 0.125f;
	result_out->origin[2] = (float)result_out->state.origin[2] * 0.125f;
	result_out->grounded = source->pmove_mode == 0 && call == 2;
	result_out->elapsed_ms = 100U;
	result_out->evaluated_steps = 4U;
	result_out->physics_abi_id = UINT64_C(0x701);
	result_out->gravity = 650.0f;
	*error_out = SG_HOST_PMOVE_ERROR_NONE;
	result.status = SG_HOST_LAW_OK;
	return result;
}

static void Entity(sg_bsp_entity_semantic_t *entity, uint32_t ordinal,
	sg_rune_mechanism_kind_t kind, sg_mech_node_kind_t role)
{
	memset(entity, 0, sizeof(*entity));
	entity->source_set_identity = UINT64_C(0x888);
	entity->source_entity_ordinal = ordinal;
	entity->canonical_ordinal = ordinal;
	entity->required_item = SG_BSP_ENTITY_STRING_NONE;
	entity->bsp_model = SG_BSP_ENTITY_MODEL_NONE;
	entity->mechanism_kind = kind;
	entity->mechanism_role = role;
	entity->flags = SG_BSP_ENTITY_HAS_MECHANISM |
		SG_BSP_ENTITY_CANONICAL_MECHANISM_KIND |
		SG_BSP_ENTITY_TOUCH_ACTIVATED;
}

static void Mechanism(sg_rune_compact_mechanism_authority_t *mechanism,
	uint32_t source, sg_rune_compact_mechanism_authority_kind_t kind)
{
	memset(mechanism, 0, sizeof(*mechanism));
	mechanism->source.entity_ordinal = source;
	mechanism->kind = kind;
	mechanism->activation = SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_TOUCH;
	mechanism->activation_cell.value = 0U;
	mechanism->activation_witness.value[0] = 24;
	mechanism->initial_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE;
	mechanism->activated_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	mechanism->reset_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE;
}

static void PortalFacet(fixture_t *fixture, uint32_t index)
{
	sg_rune_compact_facet_t *facet = &fixture->facets[index];
	uint32_t vertex;

	/* World portal topology has no dynamic-model facet lineage.  Its mover
	 * relation is supplied only by the separate model-local source catalog. */
	facet->source.kind = SG_RUNE_COMPACT_SOURCE_BSP_PLANE;
	facet->source.value.bsp_plane.model = 0U;
	facet->source.value.bsp_plane.leaf = 0U;
	facet->source.value.bsp_plane.plane = index;
	facet->plane.normal_bits[2] = FloatBits(1.0f);
	facet->vertices.first = index * 3U;
	facet->vertices.count = 3U;
	facet->kind = SG_RUNE_COMPACT_FACET_POLYGON;
	for (vertex = 0U; vertex < 3U; vertex++)
	{
		uint32_t offset = index * 3U + vertex;
		fixture->vertices[offset].value[0] = (int32_t)(index * 16U) +
			(vertex == 1U ? 8 : 0);
		fixture->vertices[offset].value[1] = vertex == 2U ? 8 : 0;
	}
}

static void ModelLocalSurfaceForEntity(fixture_t *fixture,
	uint32_t surface_index, uint32_t entity_index, uint32_t world_vertex_first)
{
	sg_rune_compact_source_surface_t *surface =
		&fixture->source_surfaces[surface_index];
	const sg_bsp_entity_semantic_t *entity = &fixture->entities[entity_index];
	uint32_t vertex;

	memset(surface, 0, sizeof(*surface));
	surface->source.model = entity->bsp_model;
	surface->source.brush = surface_index;
	surface->source.brush_side = surface_index;
	surface->source.plane = surface_index;
	surface->frame = SG_RUNE_COMPACT_SOURCE_SURFACE_MODEL_LOCAL;
	surface->cell.value = SG_RUNE_COMPACT_INDEX_NONE;
	surface->parent_surface = SG_RUNE_COMPACT_INDEX_NONE;
	surface->plane.normal_bits[2] = FloatBits(1.0f);
	surface->vertices.first = surface_index * 3U;
	surface->vertices.count = 3U;
	/* Catalog vertices stay in the bmodel's local frame.  The host-bound
	 * transform must restore the world portal triangle using entity origin. */
	for (vertex = 0U; vertex < 3U; vertex++)
	{
		const uint32_t offset = surface_index * 3U + vertex;

		fixture->source_surface_vertices[offset] =
			fixture->vertices[world_vertex_first + vertex];
		fixture->source_surface_vertices[offset].value[0] -=
			(int32_t)(entity->origin.value[0] * 8.0f);
	}
}

static void ModelLocalSurface(fixture_t *fixture, uint32_t index)
{
	ModelLocalSurfaceForEntity(fixture, index, index, index * 3U);
}

static void Initialize(fixture_t *fixture)
{
	sg_rune_compact_identity_t identity;
	uint32_t index;

	memset(fixture, 0, sizeof(*fixture));
	active_fixture = fixture;
	memset(&identity, 0, sizeof(identity));
	identity.source_counts.model_count = 4U;
	identity.source_counts.entity_count = 10U;
	identity.source_counts.brush_count = 3U;
	identity.source_counts.brush_side_count = 3U;
	identity.source_counts.plane_count = 3U;
	identity.physics.gravity_bits = FloatBits(650.0f);
	identity.physics.frame_ms = 100U;
	identity.physics.substep_ms = 25U;
	identity.physics_abi_id = UINT64_C(0x701);
	fixture->builder.owner.identity = identity;
	fixture->geometry.view.identity = identity;
	fixture->builder.pmove_calls = &fixture->pmove_calls;
	fixture->builder.mover_calls = &fixture->mover_calls;
	fixture->builder.owner_read_calls = &fixture->owner_read_calls;
	fixture->placement_ok = 1;
	fixture->entities_view.source_set_identity = UINT64_C(0x888);
	fixture->entities_view.world.source_set_identity = UINT64_C(0x888);
	fixture->entities_view.entities = fixture->entities;
	fixture->entities_view.entity_count = 10U;
	fixture->entities_view.edges = fixture->edges;
	fixture->entities_view.edge_count = 7U;
	fixture->builder.owner.entity_semantics = &fixture->entities_view;
	fixture->builder.owner.collision = &fixture->collision;

	Entity(&fixture->entities[0], 0U, SG_RUNE_MECHANISM_DOOR,
		SG_MECH_NODE_DOOR_MASTER);
	Entity(&fixture->entities[1], 1U, SG_RUNE_MECHANISM_LIFT,
		SG_MECH_NODE_PLATFORM);
	Entity(&fixture->entities[2], 2U, SG_RUNE_MECHANISM_TRAIN,
		SG_MECH_NODE_TRAIN);
	for (index = 0U; index < 3U; index++)
	{
		fixture->entities[index].flags |= SG_BSP_ENTITY_HAS_BRUSH_MODEL;
		fixture->entities[index].bsp_model = index + 1U;
		fixture->entities[index].origin.value[0] = 4.0f;
	}
	/* This model-local root begins at the local origin.  The transport oracle
	 * may therefore use the exact catalog vertex as its canonical support. */
	fixture->entities[1].origin.value[0] = 2.0f;
	Entity(&fixture->entities[3], 3U, SG_RUNE_MECHANISM_TELEPORT,
		SG_MECH_NODE_TELEPORTER);
	Entity(&fixture->entities[4], 4U, SG_RUNE_MECHANISM_TELEPORT,
		SG_MECH_NODE_TELEPORT_DEST);
	fixture->entities[4].origin.value[0] = 4.0f;
	Entity(&fixture->entities[5], 5U, SG_RUNE_MECHANISM_TELEPORT,
		SG_MECH_NODE_TELEPORT_DEST);
	fixture->entities[5].origin.value[0] = 4.0f;
	fixture->entities[5].origin.value[2] = -10.0f;
	Entity(&fixture->entities[6], 6U, SG_RUNE_MECHANISM_PUSH,
		SG_MECH_NODE_PUSH);
	Entity(&fixture->entities[7], 7U, SG_RUNE_MECHANISM_TRAIN,
		SG_MECH_NODE_PATH_CORNER);
	Entity(&fixture->entities[8], 8U, SG_RUNE_MECHANISM_TRAIN,
		SG_MECH_NODE_PATH_CORNER);
	Entity(&fixture->entities[9], 9U, SG_RUNE_MECHANISM_TRAIN,
		SG_MECH_NODE_PATH_CORNER);
	fixture->entities[6].physics_kind = SG_BSP_ENTITY_PHYSICS_PUSH;
	fixture->entities[6].speed = 85.0f;
	fixture->entities[6].angles.value[1] = 17.0f;
	fixture->edges[0].source = 3U;
	fixture->edges[0].destination = 4U;
	fixture->edges[0].kind = SG_MECH_EDGE_TARGET;
	fixture->edges[1].source = 3U;
	fixture->edges[1].destination = 5U;
	fixture->edges[1].kind = SG_MECH_EDGE_TARGET;
	fixture->edges[1].fanout_ordinal = 1U;
	fixture->edges[2].source = 2U;
	fixture->edges[2].destination = 7U;
	fixture->edges[2].kind = SG_MECH_EDGE_TARGET;
	fixture->edges[3].source = 7U;
	fixture->edges[3].destination = 8U;
	fixture->edges[3].kind = SG_MECH_EDGE_TARGET;
	fixture->edges[4] = fixture->edges[3];
	fixture->edges[4].fanout_ordinal = 1U;
	fixture->edges[5].source = 8U;
	fixture->edges[5].destination = 9U;
	fixture->edges[5].kind = SG_MECH_EDGE_TARGET;
	fixture->edges[5].fanout_ordinal = 2U;
	fixture->edges[6].source = 9U;
	fixture->edges[6].destination = 7U;
	fixture->edges[6].kind = SG_MECH_EDGE_TARGET;
	fixture->edges[6].fanout_ordinal = 3U;

	fixture->geometry.view.cells = fixture->cells;
	fixture->geometry.view.cell_count = 4U;
	fixture->geometry.view.facets = fixture->facets;
	fixture->geometry.view.facet_count = 3U;
	fixture->geometry.view.incidences = fixture->incidences;
	fixture->geometry.view.incidence_count = 6U;
	fixture->geometry.view.vertices = fixture->vertices;
	fixture->geometry.view.vertex_count = 9U;
	fixture->geometry.view.portals = fixture->portals;
	fixture->geometry.view.portal_count = 3U;
	fixture->geometry.view.source_surfaces = fixture->source_surfaces;
	fixture->geometry.view.source_surface_count = 3U;
	fixture->geometry.view.source_surface_vertices =
		fixture->source_surface_vertices;
	fixture->geometry.view.source_surface_vertex_count = 9U;
	for (index = 0U; index < 4U; index++)
		fixture->cells[index].valid_stances = SG_RUNE_STANCE_VALID_STANDING;
	for (index = 0U; index < 3U; index++)
	{
		PortalFacet(fixture, index);
		ModelLocalSurface(fixture, index);
		fixture->incidences[index * 2U].cell.value = index;
		fixture->incidences[index * 2U + 1U].cell.value = index + 1U;
		fixture->portals[index].facet.value = index;
		fixture->portals[index].negative_incidence.value = index * 2U;
		fixture->portals[index].positive_incidence.value = index * 2U + 1U;
		fixture->portals[index].direction = SG_RUNE_PORTAL_CONTINUITY_BOTH;
	}
	Mechanism(&fixture->mechanisms[0], 0U,
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_DOOR);
	Mechanism(&fixture->mechanisms[1], 1U,
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_LIFT);
	Mechanism(&fixture->mechanisms[2], 2U,
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TRAIN);
	for (index = 0U; index < 3U; index++)
		fixture->mechanisms[index].flags =
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_MOVER_RELATIVE;
	Mechanism(&fixture->mechanisms[3], 3U,
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TELEPORT);
	Mechanism(&fixture->mechanisms[4], 6U,
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_PUSH);
}

static void AddTwoPanelTeamDoor(fixture_t *fixture)
{
	sg_rune_compact_identity_t identity = fixture->builder.owner.identity;

	identity.source_counts.model_count = 5U;
	identity.source_counts.entity_count = 11U;
	identity.source_counts.brush_count = 4U;
	identity.source_counts.brush_side_count = 4U;
	identity.source_counts.plane_count = 4U;
	fixture->builder.owner.identity = identity;
	fixture->geometry.view.identity = identity;
	fixture->entities_view.entity_count = 11U;
	fixture->entities_view.edge_count = 8U;
	Entity(&fixture->entities[10], 10U, SG_RUNE_MECHANISM_DOOR,
		SG_MECH_NODE_DOOR_MEMBER);
	fixture->entities[10].flags |= SG_BSP_ENTITY_HAS_BRUSH_MODEL;
	fixture->entities[10].bsp_model = 4U;
	fixture->entities[10].origin.value[0] = 4.0f;
	fixture->edges[7].source = 10U;
	fixture->edges[7].destination = 0U;
	fixture->edges[7].kind = SG_MECH_EDGE_TEAM;
	ModelLocalSurfaceForEntity(fixture, 3U, 10U, 0U);
	fixture->geometry.view.source_surface_count = 4U;
	fixture->geometry.view.source_surface_vertex_count = 12U;
	Mechanism(&fixture->mechanisms[5], 10U,
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_DOOR);
	fixture->mechanisms[5].flags =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_MOVER_RELATIVE;
	fixture->builder.team_portal_fixture = 1;
}

static void AddSecondDoorPortal(fixture_t *fixture)
{
	sg_rune_compact_identity_t identity = fixture->builder.owner.identity;

	identity.source_counts.brush_count = 4U;
	identity.source_counts.brush_side_count = 4U;
	identity.source_counts.plane_count = 4U;
	fixture->builder.owner.identity = identity;
	fixture->geometry.view.identity = identity;
	/* Canonical catalog order remains (model, brush, side, plane): the second
	 * door root comes before the lift/train roots it displaces. */
	ModelLocalSurfaceForEntity(fixture, 1U, 0U, 3U);
	ModelLocalSurfaceForEntity(fixture, 2U, 1U, 3U);
	ModelLocalSurfaceForEntity(fixture, 3U, 2U, 6U);
	fixture->geometry.view.source_surface_count = 4U;
	fixture->geometry.view.source_surface_vertex_count = 12U;
	fixture->builder.second_door_portal_fixture = 1;
}

static int TestExactTransitions(void)
{
	fixture_t fixture;
	sg_rune_compact_mechanism_transitions_result_t result;
	sg_rune_compact_mechanisms_error_t error;

	Initialize(&fixture);
	memset(&result, 0, sizeof(result));
	CHECK(SG_RuneCompactMechanismTransitionsDerive(
		(const sg_rune_compact_builder_t *)&fixture.builder,
		(const sg_rune_compact_geometry_t *)&fixture.geometry,
		fixture.mechanisms, 5U, &result, &error));
	CHECK(result.transition_count == 11U);
	CHECK(result.spans[0].count == 3U && result.spans[1].count == 1U &&
		result.spans[2].count == 4U && result.spans[3].count == 2U &&
		result.spans[4].count == 1U);
	CHECK(result.transitions[0].kind ==
		SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE &&
		result.transitions[0].value.portal_state.portal.value == 0U &&
		result.transitions[0].value.portal_state.mover_model == 1U &&
		result.transitions[0].value.portal_state.source_blocked == 1U &&
		result.transitions[0].value.portal_state.destination_blocked == 0U &&
		result.transitions[0].value.portal_state.reserved[0] == 0U &&
		result.transitions[0].value.portal_state.reserved[1] == 0U &&
		result.transitions[0].entry_cell.value == 0U &&
		result.transitions[0].exit_cell.value == 1U &&
		result.transitions[0].elapsed_ms == UINT64_C(300) &&
		result.transitions[0].value.portal_state.travel_ms == 300U &&
		result.transitions[0].value.portal_state.recovery_ms == 0U &&
		result.transitions[0].source_state ==
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE &&
		result.transitions[0].destination_state ==
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE);
	CHECK(result.transitions[1].kind ==
		SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE &&
		result.transitions[1].source_state ==
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE &&
		result.transitions[1].destination_state ==
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE &&
		result.transitions[1].elapsed_ms == UINT64_C(300) &&
		result.transitions[1].value.portal_state.travel_ms == 300U);
	CHECK(result.transitions[2].kind ==
		SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT &&
		result.transitions[2].value.transport.mover_model == 1U &&
		result.transitions[2].value.transport.source_surface_ordinal == 0U &&
		result.transitions[2].elapsed_ms == UINT64_C(100));
	CHECK(result.transitions[3].kind ==
		SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT &&
		result.transitions[3].value.transport.mover_model == 2U &&
		result.transitions[3].value.transport.source_surface_ordinal == 1U &&
		result.transitions[3].value.transport.source_endpoint.entity_ordinal ==
			SG_RUNE_COMPACT_INDEX_NONE &&
		result.transitions[3].value.transport.destination_endpoint.entity_ordinal ==
			SG_RUNE_COMPACT_INDEX_NONE &&
		result.transitions[3].value.transport.fanout_ordinal ==
			SG_RUNE_COMPACT_INDEX_NONE &&
		result.transitions[3].value.transport.swept_static_clear == 1U &&
		result.transitions[3].value.transport.start_supported == 1U &&
		result.transitions[3].value.transport.end_supported == 1U &&
		result.transitions[3].elapsed_ms == UINT64_C(100));
	CHECK(result.transitions[4].kind ==
		SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT &&
		result.transitions[4].value.transport.mover_model == 3U &&
		result.transitions[4].value.transport.source_surface_ordinal == 2U &&
		result.transitions[4].value.transport.source_endpoint.entity_ordinal == 7U &&
		result.transitions[4].value.transport.destination_endpoint.entity_ordinal == 8U &&
		result.transitions[4].value.transport.fanout_ordinal == 0U &&
		result.transitions[4].value.transport.source_player_world_bits[0] ==
			FloatBits(4.0f) &&
		result.transitions[4].value.transport.destination_player_world_bits[2] ==
			FloatBits(12.125f) &&
		result.transitions[4].value.transport.source_mover_origin_bits[0] ==
			FloatBits(4.0f) &&
		result.transitions[4].value.transport.destination_mover_origin_bits[0] ==
			FloatBits(4.0f) &&
		result.transitions[4].value.transport.source_mover_axis_bits[1][1] ==
			FloatBits(1.0f) &&
		result.transitions[4].value.transport.source_mover_axis_bits[1][0] ==
			FloatBits(0.0f) &&
		result.transitions[4].value.transport.destination_mover_axis_bits[2][2] ==
			FloatBits(1.0f) &&
		result.transitions[4].value.transport.destination_mover_origin_bits[2] ==
			FloatBits(12.0f));
	CHECK(result.transitions[5].value.transport.source_endpoint.entity_ordinal ==
			7U && result.transitions[5].value.transport.destination_endpoint.entity_ordinal ==
			8U && result.transitions[5].value.transport.fanout_ordinal == 1U &&
		result.transitions[6].value.transport.source_endpoint.entity_ordinal ==
			8U && result.transitions[6].value.transport.destination_endpoint.entity_ordinal ==
			9U && result.transitions[6].value.transport.fanout_ordinal == 2U &&
		result.transitions[7].value.transport.source_endpoint.entity_ordinal ==
			9U && result.transitions[7].value.transport.destination_endpoint.entity_ordinal ==
			7U && result.transitions[7].value.transport.fanout_ordinal == 3U);
	CHECK(result.transitions[8].exit_cell.value == 0U &&
		result.transitions[8].value.teleport.destination.entity_ordinal == 5U &&
		result.transitions[8].value.teleport.exit_witness.value[2] == 0 &&
		result.transitions[8].value.teleport.arrival_velocity_bits[0] == 0U &&
		result.transitions[8].value.teleport.arrival_velocity_bits[1] == 0U &&
		result.transitions[8].value.teleport.arrival_velocity_bits[2] == 0U &&
		result.transitions[9].exit_cell.value == 1U &&
		result.transitions[9].value.teleport.destination.entity_ordinal == 4U &&
		result.transitions[9].value.teleport.exit_witness.value[2] == 80 &&
		result.transitions[9].value.teleport.arrival_velocity_bits[0] == 0U &&
		result.transitions[9].value.teleport.arrival_velocity_bits[1] == 0U &&
		result.transitions[9].value.teleport.arrival_velocity_bits[2] == 0U &&
		fixture.placement_calls == 2);
	CHECK(result.transitions[10].value.push.launch_velocity_bits[0] ==
		UINT32_C(0x444b36fb) &&
		result.transitions[10].value.push.launch_velocity_bits[1] ==
		UINT32_C(0x43788414) &&
		result.transitions[10].value.push.launch_velocity_bits[2] ==
		UINT32_C(0x80000000) &&
		result.transitions[10].value.push.flight_ms == 200U &&
		result.transitions[10].exit_cell.value == 2U && fixture.pmove_calls == 2);
	for (uint32_t index = 0U; index < 5U; index++)
		fixture.mechanisms[index].transitions = result.spans[index];
	fixture.pmove_calls = 0;
	fixture.placement_calls = 0;
	CHECK(SG_RuneCompactMechanismTransitionsValidate(
		(const sg_rune_compact_builder_t *)&fixture.builder,
		(const sg_rune_compact_geometry_t *)&fixture.geometry,
		fixture.mechanisms, 5U, result.transitions, result.transition_count,
		&error));
	/* Occupancy is a semantic fact, not padding: a copied record cannot invert
	 * or erase the host-certified endpoint conjunction. */
	result.transitions[0].value.portal_state.source_blocked = 0U;
	fixture.pmove_calls = 0;
	fixture.placement_calls = 0;
	CHECK(!SG_RuneCompactMechanismTransitionsValidate(
		(const sg_rune_compact_builder_t *)&fixture.builder,
		(const sg_rune_compact_geometry_t *)&fixture.geometry,
		fixture.mechanisms, 5U, result.transitions, result.transition_count,
		&error));
	CHECK(error.code == SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE);
	result.transitions[0].value.portal_state.source_blocked = 1U;
	result.transitions[3].value.transport.destination_support_world_bits[2]++;
	fixture.pmove_calls = 0;
	fixture.placement_calls = 0;
	CHECK(!SG_RuneCompactMechanismTransitionsValidate(
		(const sg_rune_compact_builder_t *)&fixture.builder,
		(const sg_rune_compact_geometry_t *)&fixture.geometry,
		fixture.mechanisms, 5U, result.transitions, result.transition_count,
		&error));
	CHECK(error.code == SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE);
	result.transitions[3].value.transport.destination_support_world_bits[2]--;
	result.transitions[3].value.transport.source_mover_origin_bits[0]++;
	fixture.pmove_calls = 0;
	fixture.placement_calls = 0;
	CHECK(!SG_RuneCompactMechanismTransitionsValidate(
		(const sg_rune_compact_builder_t *)&fixture.builder,
		(const sg_rune_compact_geometry_t *)&fixture.geometry,
		fixture.mechanisms, 5U, result.transitions, result.transition_count,
		&error));
	CHECK(error.code == SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE);
	result.transitions[3].value.transport.source_mover_origin_bits[0]--;
	result.transitions[3].value.transport.source_mover_axis_bits[0][0]++;
	fixture.pmove_calls = 0;
	fixture.placement_calls = 0;
	CHECK(!SG_RuneCompactMechanismTransitionsValidate(
		(const sg_rune_compact_builder_t *)&fixture.builder,
		(const sg_rune_compact_geometry_t *)&fixture.geometry,
		fixture.mechanisms, 5U, result.transitions, result.transition_count,
		&error));
	CHECK(error.code == SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE);
	result.transitions[3].value.transport.source_mover_axis_bits[0][0]--;
	SG_RuneCompactMechanismTransitionsRelease(&result);
	return 1;
}

static int TestTransportTransformReplayRejectsFalseHostFact(void)
{
	fixture_t fixture;
	sg_rune_compact_mechanism_transitions_result_t result;
	sg_rune_compact_mechanisms_error_t error;

	Initialize(&fixture);
	/* The mover callback still marks the response applicable with all support
	 * evidence set.  Only the owner-bound transform replay may reject it. */
	fixture.builder.mover_mode = 5;
	memset(&result, 0, sizeof(result));
	CHECK(!SG_RuneCompactMechanismTransitionsDerive(
		(const sg_rune_compact_builder_t *)&fixture.builder,
		(const sg_rune_compact_geometry_t *)&fixture.geometry,
		fixture.mechanisms, 5U, &result, &error));
	CHECK(error.code == SG_RUNE_COMPACT_MECHANISMS_ERROR_HOST_EVALUATION &&
		result.transitions == NULL && result.transition_count == 0U);
	return 1;
}

static int TestTransportPoseRelationRejectsEndpointLocalDrift(void)
{
	fixture_t fixture;
	sg_rune_compact_mechanism_transitions_result_t result;
	sg_rune_compact_mechanisms_error_t error;

	Initialize(&fixture);
	fixture.builder.mover_mode = 6;
	memset(&result, 0, sizeof(result));
	CHECK(!SG_RuneCompactMechanismTransitionsDerive(
		(const sg_rune_compact_builder_t *)&fixture.builder,
		(const sg_rune_compact_geometry_t *)&fixture.geometry,
		fixture.mechanisms, 5U, &result, &error));
	CHECK(error.code == SG_RUNE_COMPACT_MECHANISMS_ERROR_HOST_EVALUATION &&
		result.transitions == NULL && result.transition_count == 0U);
	return 1;
}

static int TestTransportRejectsSupportOutsideSourceRoot(void)
{
	fixture_t fixture;
	sg_rune_compact_mechanism_transitions_result_t result;
	sg_rune_compact_mechanisms_error_t error;

	Initialize(&fixture);
	/* The host response is self-consistent under its supplied transform.  The
	 * selected source root is the remaining authority for support placement. */
	fixture.builder.mover_mode = 7;
	memset(&result, 0, sizeof(result));
	CHECK(!SG_RuneCompactMechanismTransitionsDerive(
		(const sg_rune_compact_builder_t *)&fixture.builder,
		(const sg_rune_compact_geometry_t *)&fixture.geometry,
		fixture.mechanisms, 5U, &result, &error));
	CHECK(error.code == SG_RUNE_COMPACT_MECHANISMS_ERROR_HOST_EVALUATION &&
		result.transitions == NULL && result.transition_count == 0U);
	return 1;
}

static int TestPortalStateCanonicalOrder(void)
{
	fixture_t fixture;
	sg_rune_compact_mechanism_transitions_result_t result;
	sg_rune_compact_mechanisms_error_t error;

	Initialize(&fixture);
	AddSecondDoorPortal(&fixture);
	memset(&result, 0, sizeof(result));
	CHECK(SG_RuneCompactMechanismTransitionsDerive(
		(const sg_rune_compact_builder_t *)&fixture.builder,
		(const sg_rune_compact_geometry_t *)&fixture.geometry,
		fixture.mechanisms, 5U, &result, &error));
	CHECK(result.spans[0].count == 5U &&
		result.transitions[0].mechanism == 0U &&
		result.transitions[1].mechanism == 0U &&
		result.transitions[2].mechanism == 0U &&
		result.transitions[3].mechanism == 0U &&
		result.transitions[0].kind ==
			SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE &&
		result.transitions[1].kind ==
			SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE &&
		result.transitions[0].value.portal_state.portal.value == 0U &&
		result.transitions[1].value.portal_state.portal.value == 0U &&
		result.transitions[2].value.portal_state.portal.value == 1U &&
		result.transitions[3].value.portal_state.portal.value == 1U &&
		result.transitions[0].entry_cell.value == 0U &&
		result.transitions[0].exit_cell.value == 1U &&
		result.transitions[2].entry_cell.value == 1U &&
		result.transitions[2].exit_cell.value == 2U);
	SG_RuneCompactMechanismTransitionsRelease(&result);
	return 1;
}

static int TestCommonKeyPrecedesTransportPayload(void)
{
	fixture_t fixture;
	sg_rune_compact_mechanism_transitions_result_t result;
	sg_rune_compact_mechanisms_error_t error;
	uint32_t first;

	Initialize(&fixture);
	fixture.builder.mover_mode = 9;
	memset(&result, 0, sizeof(result));
	CHECK(SG_RuneCompactMechanismTransitionsDerive(
		(const sg_rune_compact_builder_t *)&fixture.builder,
		(const sg_rune_compact_geometry_t *)&fixture.geometry,
		fixture.mechanisms, 5U, &result, &error));
	first = result.spans[2].first;
	CHECK(result.spans[2].count == 4U &&
		result.transitions[first].entry_cell.value == 0U &&
		result.transitions[first].exit_cell.value == 1U &&
		result.transitions[first].value.transport.source_endpoint.entity_ordinal ==
			8U &&
		result.transitions[first + 1U].entry_cell.value == 0U &&
		result.transitions[first + 1U].exit_cell.value == 1U &&
		result.transitions[first + 1U].value.transport.source_endpoint
			.entity_ordinal == 9U &&
		result.transitions[first + 2U].entry_cell.value == 2U &&
		result.transitions[first + 2U].exit_cell.value == 2U &&
		result.transitions[first + 2U].value.transport.source_endpoint
			.entity_ordinal == 7U &&
		result.transitions[first + 2U].value.transport.fanout_ordinal == 0U &&
		result.transitions[first + 3U].entry_cell.value == 2U &&
		result.transitions[first + 3U].exit_cell.value == 2U &&
		result.transitions[first + 3U].value.transport.source_endpoint
			.entity_ordinal == 7U &&
		result.transitions[first + 3U].value.transport.fanout_ordinal == 1U);
	SG_RuneCompactMechanismTransitionsRelease(&result);
	return 1;
}

static int TestTwoPanelTeamDoorPortalGroup(void)
{
	fixture_t fixture;
	sg_rune_compact_mechanism_transitions_result_t result;
	sg_rune_compact_mechanisms_error_t error;
	uint32_t index;

	Initialize(&fixture);
	AddTwoPanelTeamDoor(&fixture);
	memset(&result, 0, sizeof(result));
	CHECK(SG_RuneCompactMechanismTransitionsDerive(
		(const sg_rune_compact_builder_t *)&fixture.builder,
		(const sg_rune_compact_geometry_t *)&fixture.geometry,
		fixture.mechanisms, 6U, &result, &error));
	CHECK(result.transition_count == 10U);
	CHECK(result.spans[0].count == 2U && result.spans[5].count == 0U);
	CHECK(result.transitions[0].kind ==
		SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE &&
		result.transitions[0].mechanism == 0U &&
		result.transitions[0].value.portal_state.portal.value == 0U &&
		result.transitions[0].value.portal_state.mover_model == 4U &&
		result.transitions[0].elapsed_ms == UINT64_C(300));
	for (index = 0U; index < 6U; index++)
		fixture.mechanisms[index].transitions = result.spans[index];
	fixture.pmove_calls = 0;
	fixture.placement_calls = 0;
	CHECK(SG_RuneCompactMechanismTransitionsValidate(
		(const sg_rune_compact_builder_t *)&fixture.builder,
		(const sg_rune_compact_geometry_t *)&fixture.geometry,
		fixture.mechanisms, 6U, result.transitions, result.transition_count,
		&error));
	SG_RuneCompactMechanismTransitionsRelease(&result);

	Initialize(&fixture);
	AddTwoPanelTeamDoor(&fixture);
	fixture.builder.mover_mode = 4;
	memset(&result, 0, sizeof(result));
	CHECK(!SG_RuneCompactMechanismTransitionsDerive(
		(const sg_rune_compact_builder_t *)&fixture.builder,
		(const sg_rune_compact_geometry_t *)&fixture.geometry,
		fixture.mechanisms, 6U, &result, &error));
	CHECK(error.code == SG_RUNE_COMPACT_MECHANISMS_ERROR_HOST_EVALUATION);

	Initialize(&fixture);
	AddTwoPanelTeamDoor(&fixture);
	fixture.edges[7].destination = 10U;
	memset(&result, 0, sizeof(result));
	CHECK(!SG_RuneCompactMechanismTransitionsDerive(
		(const sg_rune_compact_builder_t *)&fixture.builder,
		(const sg_rune_compact_geometry_t *)&fixture.geometry,
		fixture.mechanisms, 6U, &result, &error));
	CHECK(error.code == SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE &&
		error.domain == SG_RUNE_COMPACT_MECHANISMS_RECORD_EDGE &&
		error.record == 7U);
	return 1;
}

static int TestAngularRotatorPortalAuthority(void)
{
	fixture_t fixture;
	sg_rune_compact_mechanism_transitions_result_t result;
	sg_rune_compact_mechanisms_error_t error;

	Initialize(&fixture);
	fixture.entities[0].mechanism_kind = SG_RUNE_MECHANISM_ROTATOR;
	fixture.entities[0].angular_mover.kind =
		SG_BSP_ENTITY_ANGULAR_MOVER_FINITE_DOOR;
	fixture.mechanisms[0].kind =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_ROTATOR;
	memset(&result, 0, sizeof(result));
	CHECK(SG_RuneCompactMechanismTransitionsDerive(
		(const sg_rune_compact_builder_t *)&fixture.builder,
		(const sg_rune_compact_geometry_t *)&fixture.geometry,
		fixture.mechanisms, 1U, &result, &error));
	CHECK(result.transition_count == 3U);
	CHECK(result.spans[0].count == 3U);
	CHECK(result.transitions[0].kind ==
		SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE);
	CHECK(result.transitions[0].value.portal_state.mover_model == 1U);
	CHECK(result.transitions[2].kind ==
		SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT &&
		result.transitions[2].value.transport.stance == SG_RUNE_STANCE_STANDING);
	fixture.mechanisms[0].transitions = result.spans[0];
	CHECK(SG_RuneCompactMechanismTransitionsValidate(
		(const sg_rune_compact_builder_t *)&fixture.builder,
		(const sg_rune_compact_geometry_t *)&fixture.geometry,
		fixture.mechanisms, 1U, result.transitions, result.transition_count,
		&error));
	SG_RuneCompactMechanismTransitionsRelease(&result);

	Initialize(&fixture);
	fixture.entities[0].mechanism_kind = SG_RUNE_MECHANISM_ROTATOR;
	fixture.entities[0].angular_mover.kind =
		SG_BSP_ENTITY_ANGULAR_MOVER_CONTINUOUS_ROTATOR;
	fixture.mechanisms[0].kind =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_ROTATOR;
	memset(&result, 0, sizeof(result));
	CHECK(SG_RuneCompactMechanismTransitionsDerive(
		(const sg_rune_compact_builder_t *)&fixture.builder,
		(const sg_rune_compact_geometry_t *)&fixture.geometry,
		fixture.mechanisms, 1U, &result, &error));
	CHECK(result.transition_count == 1U);
	CHECK(result.spans[0].count == 1U &&
		result.transitions[0].kind ==
			SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT &&
		result.transitions[0].source_state ==
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE &&
		result.transitions[0].destination_state ==
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE);
	SG_RuneCompactMechanismTransitionsRelease(&result);
	return 1;
}

static int TestToggleMoverTransitions(void)
{
	fixture_t fixture;
	sg_rune_compact_mechanism_transitions_result_t result;
	sg_rune_compact_mechanisms_error_t error;

	Initialize(&fixture);
	fixture.mechanisms[0].reset_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	memset(&result, 0, sizeof(result));
	CHECK(SG_RuneCompactMechanismTransitionsDerive(
		(const sg_rune_compact_builder_t *)&fixture.builder,
		(const sg_rune_compact_geometry_t *)&fixture.geometry,
		&fixture.mechanisms[0], 1U, &result, &error));
	CHECK(result.transition_count == 3U && result.spans[0].count == 3U);
	CHECK(result.transitions[0].source_state ==
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE &&
		result.transitions[0].destination_state ==
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE &&
		result.transitions[1].source_state ==
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE &&
		result.transitions[1].destination_state ==
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE &&
		result.transitions[0].value.portal_state.travel_ms == 300U &&
		result.transitions[1].value.portal_state.travel_ms == 300U);
	SG_RuneCompactMechanismTransitionsRelease(&result);

	Initialize(&fixture);
	fixture.entities[0].mechanism_kind = SG_RUNE_MECHANISM_ROTATOR;
	fixture.entities[0].angular_mover.kind =
		SG_BSP_ENTITY_ANGULAR_MOVER_FINITE_DOOR;
	fixture.entities[0].angular_mover.flags =
		SG_BSP_ENTITY_ANGULAR_MOVER_TOGGLE;
	fixture.mechanisms[0].kind =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_ROTATOR;
	fixture.mechanisms[0].flags =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_MOVER_RELATIVE;
	fixture.mechanisms[0].reset_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	memset(&result, 0, sizeof(result));
	CHECK(SG_RuneCompactMechanismTransitionsDerive(
		(const sg_rune_compact_builder_t *)&fixture.builder,
		(const sg_rune_compact_geometry_t *)&fixture.geometry,
		&fixture.mechanisms[0], 1U, &result, &error));
	CHECK(result.transition_count == 3U && result.spans[0].count == 3U);
	CHECK(result.transitions[0].source_state ==
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE &&
		result.transitions[1].source_state ==
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE &&
		result.transitions[1].destination_state ==
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE);
	SG_RuneCompactMechanismTransitionsRelease(&result);
	return 1;
}

static int TestPushFailures(void)
{
	fixture_t fixture;
	sg_rune_compact_mechanism_transitions_result_t result;
	sg_rune_compact_mechanisms_error_t error;

	Initialize(&fixture);
	fixture.builder.pmove_mode = 1;
	memset(&result, 0, sizeof(result));
	CHECK(!SG_RuneCompactMechanismTransitionsDerive(
		(const sg_rune_compact_builder_t *)&fixture.builder,
		(const sg_rune_compact_geometry_t *)&fixture.geometry,
		fixture.mechanisms, 5U, &result, &error));
	CHECK(error.code == SG_RUNE_COMPACT_MECHANISMS_ERROR_HOST_EVALUATION &&
		fixture.pmove_calls == 2);
	Initialize(&fixture);
	fixture.builder.pmove_mode = 2;
	CHECK(!SG_RuneCompactMechanismTransitionsDerive(
		(const sg_rune_compact_builder_t *)&fixture.builder,
		(const sg_rune_compact_geometry_t *)&fixture.geometry,
		fixture.mechanisms, 5U, &result, &error));
	CHECK(error.code == SG_RUNE_COMPACT_MECHANISMS_ERROR_HOST_EVALUATION &&
		fixture.pmove_calls == 1);
	return 1;
}

static int TestTrainGraphAndAutoStart(void)
{
	fixture_t fixture;
	sg_rune_compact_mechanism_transitions_result_t result;
	sg_rune_compact_mechanisms_error_t error;
	uint32_t index;

	Initialize(&fixture);
	fixture.mechanisms[2].initial_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture.mechanisms[2].activated_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	memset(&result, 0, sizeof(result));
	CHECK(SG_RuneCompactMechanismTransitionsDerive(
		(const sg_rune_compact_builder_t *)&fixture.builder,
		(const sg_rune_compact_geometry_t *)&fixture.geometry,
		fixture.mechanisms, 5U, &result, &error));
	CHECK(result.spans[2].count == 4U);
	for (index = result.spans[2].first;
		index < result.spans[2].first + result.spans[2].count; index++)
		CHECK(result.transitions[index].kind ==
			SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT &&
			result.transitions[index].source_state ==
				SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE &&
			result.transitions[index].destination_state ==
				SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE &&
			result.transitions[index].elapsed_ms != 0U &&
			result.transitions[index].value.transport.source_endpoint.entity_ordinal !=
				result.transitions[index].value.transport.destination_endpoint.entity_ordinal);
	SG_RuneCompactMechanismTransitionsRelease(&result);

	Initialize(&fixture);
	/* A reachable TARGET edge leaving the path-corner graph has no host route
	 * meaning.  The finite walk rejects it rather than omitting that branch. */
	fixture.edges[5].destination = 4U;
	CHECK(!SG_RuneCompactMechanismTransitionsDerive(
		(const sg_rune_compact_builder_t *)&fixture.builder,
		(const sg_rune_compact_geometry_t *)&fixture.geometry,
		fixture.mechanisms, 5U, &result, &error));
	CHECK(error.code == SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE &&
		error.domain == SG_RUNE_COMPACT_MECHANISMS_RECORD_EDGE &&
		error.record == 5U);
	return 1;
}

static int TestAlwaysActiveTeleportAndPush(void)
{
	fixture_t fixture;
	sg_rune_compact_mechanism_transitions_result_t result;
	sg_rune_compact_mechanisms_error_t error;

	Initialize(&fixture);
	fixture.mechanisms[3].initial_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture.mechanisms[3].activated_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture.mechanisms[4].initial_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture.mechanisms[4].activated_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	memset(&result, 0, sizeof(result));
	CHECK(SG_RuneCompactMechanismTransitionsDerive(
		(const sg_rune_compact_builder_t *)&fixture.builder,
		(const sg_rune_compact_geometry_t *)&fixture.geometry,
		fixture.mechanisms, 5U, &result, &error));
	CHECK(result.transitions[8].kind ==
		SG_RUNE_COMPACT_MECHANISM_TRANSITION_TELEPORT &&
		result.transitions[8].source_state ==
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE &&
		result.transitions[8].destination_state ==
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE &&
		result.transitions[10].kind ==
		SG_RUNE_COMPACT_MECHANISM_TRANSITION_PUSH &&
		result.transitions[10].source_state ==
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE &&
		result.transitions[10].destination_state ==
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE);
	SG_RuneCompactMechanismTransitionsRelease(&result);
	return 1;
}

static int TestActivationMaskAuthority(void)
{
	fixture_t fixture;
	sg_rune_compact_mechanism_transitions_result_t result;
	sg_rune_compact_mechanisms_error_t error;

	Initialize(&fixture);
	fixture.mechanisms[3].activation =
		SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_AUTO;
	memset(&result, 0, sizeof(result));
	CHECK(!SG_RuneCompactMechanismTransitionsDerive(
		(const sg_rune_compact_builder_t *)&fixture.builder,
		(const sg_rune_compact_geometry_t *)&fixture.geometry,
		fixture.mechanisms, 5U, &result, &error));
	CHECK(error.code == SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE &&
		error.domain == SG_RUNE_COMPACT_MECHANISMS_RECORD_ENTITY &&
		error.record == 3U && result.transitions == NULL &&
		result.transition_count == 0U);
	return 1;
}

static int TestDelayedDwellDoorTiming(void)
{
	fixture_t fixture;
	sg_rune_compact_mechanism_transitions_result_t result;
	sg_rune_compact_mechanisms_error_t error;
	const sg_rune_compact_mechanism_portal_state_t *state;

	Initialize(&fixture);
	fixture.mechanisms[0].delay_ms = 250U;
	fixture.mechanisms[0].dwell_ms = 1750U;
	fixture.mechanisms[0].pause_ms = 3000U;
	memset(&result, 0, sizeof(result));
	CHECK(SG_RuneCompactMechanismTransitionsDerive(
		(const sg_rune_compact_builder_t *)&fixture.builder,
		(const sg_rune_compact_geometry_t *)&fixture.geometry,
		&fixture.mechanisms[0], 1U, &result, &error));
	CHECK(result.transition_count == 3U && result.spans[0].count == 3U);
	state = &result.transitions[0].value.portal_state;
	CHECK(result.transitions[0].kind ==
		SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE &&
		result.transitions[0].elapsed_ms == UINT64_C(300) &&
		state->delay_ms == 250U && state->dwell_ms == 1750U &&
		state->pause_ms == 3000U && state->travel_ms == 300U &&
		state->recovery_ms == 0U);
	SG_RuneCompactMechanismTransitionsRelease(&result);
	return 1;
}

static int TestInconsistentTimingAggregateFailsClosed(void)
{
	fixture_t fixture;
	sg_rune_compact_mechanism_transitions_result_t result;
	sg_rune_compact_mechanisms_error_t error;

	Initialize(&fixture);
	fixture.mechanisms[0].travel_ms = 299U;
	memset(&result, 0, sizeof(result));
	CHECK(!SG_RuneCompactMechanismTransitionsDerive(
		(const sg_rune_compact_builder_t *)&fixture.builder,
		(const sg_rune_compact_geometry_t *)&fixture.geometry,
		&fixture.mechanisms[0], 1U, &result, &error));
	CHECK(error.code == SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE &&
		result.transitions == NULL && result.transition_count == 0U);

	Initialize(&fixture);
	fixture.mechanisms[0].recovery_ms = 299U;
	memset(&result, 0, sizeof(result));
	CHECK(!SG_RuneCompactMechanismTransitionsDerive(
		(const sg_rune_compact_builder_t *)&fixture.builder,
		(const sg_rune_compact_geometry_t *)&fixture.geometry,
		&fixture.mechanisms[0], 1U, &result, &error));
	CHECK(error.code == SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE &&
		result.transitions == NULL && result.transition_count == 0U);
	return 1;
}

static int TestAuthorityRevocationLeavesResultUntouched(void)
{
	fixture_t fixture;
	sg_rune_compact_mechanism_transitions_result_t result;
	sg_rune_compact_mechanism_transitions_result_t sentinel;
	sg_rune_compact_mechanisms_error_t error;

	Initialize(&fixture);
	fixture.builder.revoke_owner_on_read = 2;
	memset(&sentinel, 0x5a, sizeof(sentinel));
	result = sentinel;
	CHECK(!SG_RuneCompactMechanismTransitionsDerive(
		(const sg_rune_compact_builder_t *)&fixture.builder,
		(const sg_rune_compact_geometry_t *)&fixture.geometry,
		&fixture.mechanisms[3], 1U, &result, &error));
	CHECK(error.code == SG_RUNE_COMPACT_MECHANISMS_ERROR_IDENTITY_MISMATCH &&
		fixture.owner_read_calls == 2 &&
		memcmp(&result, &sentinel, sizeof(result)) == 0);
	return 1;
}

static int TestMoverOracleFailures(void)
{
	fixture_t fixture;
	sg_rune_compact_mechanism_transitions_result_t result;
	sg_rune_compact_mechanisms_error_t error;

	Initialize(&fixture);
	fixture.builder.mover_mode = 1;
	memset(&result, 0, sizeof(result));
	CHECK(SG_RuneCompactMechanismTransitionsDerive(
		(const sg_rune_compact_builder_t *)&fixture.builder,
		(const sg_rune_compact_geometry_t *)&fixture.geometry,
		fixture.mechanisms, 5U, &result, &error));
	CHECK(result.transition_count == 5U);
	for (uint32_t index = 0U; index < result.transition_count; index++)
		CHECK(result.transitions[index].kind !=
			SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT);
	SG_RuneCompactMechanismTransitionsRelease(&result);
	Initialize(&fixture);
	AddSecondDoorPortal(&fixture);
	fixture.builder.mover_mode = 12;
	memset(&result, 0, sizeof(result));
	CHECK(SG_RuneCompactMechanismTransitionsDerive(
		(const sg_rune_compact_builder_t *)&fixture.builder,
		(const sg_rune_compact_geometry_t *)&fixture.geometry,
		fixture.mechanisms, 5U, &result, &error));
	CHECK(result.spans[0].count == 5U);
	for (uint32_t index = result.spans[0].first;
		index < result.spans[0].first + result.spans[0].count; index++)
		if (result.transitions[index].kind ==
			SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT)
			CHECK(result.transitions[index].value.transport.source_surface_ordinal ==
				1U);
	SG_RuneCompactMechanismTransitionsRelease(&result);
	Initialize(&fixture);
	fixture.builder.mover_mode = 13;
	memset(&result, 0, sizeof(result));
	CHECK(!SG_RuneCompactMechanismTransitionsDerive(
		(const sg_rune_compact_builder_t *)&fixture.builder,
		(const sg_rune_compact_geometry_t *)&fixture.geometry,
		fixture.mechanisms, 5U, &result, &error));
	CHECK(error.code == SG_RUNE_COMPACT_MECHANISMS_ERROR_HOST_EVALUATION);
	for (uint32_t mode = 14U; mode <= 16U; mode++) {
		Initialize(&fixture);
		fixture.builder.mover_mode = (int)mode;
		memset(&result, 0, sizeof(result));
		CHECK(!SG_RuneCompactMechanismTransitionsDerive(
			(const sg_rune_compact_builder_t *)&fixture.builder,
			(const sg_rune_compact_geometry_t *)&fixture.geometry,
			fixture.mechanisms, 5U, &result, &error));
		CHECK(error.code == SG_RUNE_COMPACT_MECHANISMS_ERROR_HOST_EVALUATION);
	}
	Initialize(&fixture);
	fixture.builder.mover_mode = 2;
	CHECK(!SG_RuneCompactMechanismTransitionsDerive(
		(const sg_rune_compact_builder_t *)&fixture.builder,
		(const sg_rune_compact_geometry_t *)&fixture.geometry,
		fixture.mechanisms, 5U, &result, &error));
	CHECK(error.code == SG_RUNE_COMPACT_MECHANISMS_ERROR_HOST_EVALUATION);
	Initialize(&fixture);
	fixture.builder.mover_mode = 3;
	CHECK(!SG_RuneCompactMechanismTransitionsDerive(
		(const sg_rune_compact_builder_t *)&fixture.builder,
		(const sg_rune_compact_geometry_t *)&fixture.geometry,
		fixture.mechanisms, 5U, &result, &error));
	CHECK(error.code == SG_RUNE_COMPACT_MECHANISMS_ERROR_HOST_EVALUATION &&
		fixture.mover_calls == 1);
	return 1;
}

static int TestTeleportPlacementAndAuthorityFailures(void)
{
	fixture_t fixture;
	sg_rune_compact_mechanism_transitions_result_t result;
	sg_rune_compact_mechanism_transitions_result_t sentinel;
	sg_rune_compact_mechanisms_error_t error;

	Initialize(&fixture);
	fixture.placement_ok = 0;
	memset(&result, 0, sizeof(result));
	CHECK(!SG_RuneCompactMechanismTransitionsDerive(
		(const sg_rune_compact_builder_t *)&fixture.builder,
		(const sg_rune_compact_geometry_t *)&fixture.geometry,
		fixture.mechanisms, 5U, &result, &error));
	CHECK(error.code == SG_RUNE_COMPACT_MECHANISMS_ERROR_HOST_EVALUATION &&
		fixture.placement_calls == 1);
	Initialize(&fixture);
	fixture.mechanisms[3].source.entity_ordinal = 7U;
	memset(&sentinel, 0xa5, sizeof(sentinel));
	result = sentinel;
	CHECK(!SG_RuneCompactMechanismTransitionsDerive(
		(const sg_rune_compact_builder_t *)&fixture.builder,
		(const sg_rune_compact_geometry_t *)&fixture.geometry,
		fixture.mechanisms, 5U, &result, &error));
	CHECK(error.code == SG_RUNE_COMPACT_MECHANISMS_ERROR_INVALID_SOURCE &&
		memcmp(&result, &sentinel, sizeof(result)) == 0);
	return 1;
}

static int TestAllocationFailures(void)
{
	fixture_t fixture;
	sg_rune_compact_mechanism_transitions_result_t result;
	sg_rune_compact_mechanism_transitions_result_t sentinel;
	sg_rune_compact_mechanisms_error_t error;
	size_t allocations;
	size_t failure;

	Initialize(&fixture);
	SG_RuneCompactMechanismTransitionsTestFailAfter(SIZE_MAX);
	memset(&result, 0, sizeof(result));
	CHECK(SG_RuneCompactMechanismTransitionsDerive(
		(const sg_rune_compact_builder_t *)&fixture.builder,
		(const sg_rune_compact_geometry_t *)&fixture.geometry,
		fixture.mechanisms, 5U, &result, &error));
	allocations = SG_RuneCompactMechanismTransitionsTestAllocationCount();
	CHECK(allocations > 3U);
	SG_RuneCompactMechanismTransitionsRelease(&result);
	memset(&sentinel, 0x3c, sizeof(sentinel));
	for (failure = 0U; failure < allocations; failure++)
	{
		result = sentinel;
		fixture.pmove_calls = 0;
		fixture.mover_calls = 0;
		fixture.placement_calls = 0;
		SG_RuneCompactMechanismTransitionsTestFailAfter(failure);
		CHECK(!SG_RuneCompactMechanismTransitionsDerive(
			(const sg_rune_compact_builder_t *)&fixture.builder,
			(const sg_rune_compact_geometry_t *)&fixture.geometry,
			fixture.mechanisms, 5U, &result, &error));
		CHECK(error.code == SG_RUNE_COMPACT_MECHANISMS_ERROR_OUT_OF_MEMORY &&
			memcmp(&result, &sentinel, sizeof(result)) == 0);
	}
	SG_RuneCompactMechanismTransitionsTestFailAfter(SIZE_MAX);
	return 1;
}

int main(void)
{
	if (!TestExactTransitions() ||
		!TestTransportTransformReplayRejectsFalseHostFact() ||
		!TestTransportPoseRelationRejectsEndpointLocalDrift() ||
		!TestTransportRejectsSupportOutsideSourceRoot() ||
		!TestPortalStateCanonicalOrder() ||
		!TestCommonKeyPrecedesTransportPayload() ||
		!TestTwoPanelTeamDoorPortalGroup() ||
		!TestAngularRotatorPortalAuthority() ||
		!TestToggleMoverTransitions() ||
		!TestPushFailures() ||
		!TestTrainGraphAndAutoStart() ||
		!TestAlwaysActiveTeleportAndPush() ||
		!TestActivationMaskAuthority() ||
		!TestDelayedDwellDoorTiming() ||
		!TestInconsistentTimingAggregateFailsClosed() ||
		!TestAuthorityRevocationLeavesResultUntouched() ||
		!TestMoverOracleFailures() ||
		!TestTeleportPlacementAndAuthorityFailures() || !TestAllocationFailures())
		return 1;
	puts("compact mechanism transition derivation tests passed");
	return 0;
}
