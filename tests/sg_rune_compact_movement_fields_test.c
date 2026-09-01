#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "slipgate/sg_rune_compact_eval.h"
#include "slipgate/sg_rune_compact_movement_fields.h"
#include "slipgate/sg_rune_compact_builder_owner.h"

/* The focused movement runner does not link the production static
 * materializer.  This owner-backed fixture mirrors its read seam so tests
 * cannot accidentally exercise the rejected detached-static path. */
struct sg_rune_compact_static_materializer_s
{
	sg_rune_compact_identity_t identity;
	const sg_rune_compact_static_t *static_data;
	const uint32_t *authority_transition_static;
	uint32_t authority_transition_count;
	const uint32_t *static_mechanism_authority;
	uint32_t static_mechanism_count;
};

/* The focused fixture uses owner objects with the same read-only contract as
 * the production handles.  Their backing views are deliberately borrowed
 * through one fixture, so every mutation test updates the owner source and
 * the movement constructor rereads it at its boundary. */
struct sg_rune_compact_builder_s
{
	const sg_rune_compact_builder_owner_view_t *view;
	uint32_t *pmove_calls;
	int replay_enabled;
	float replay_origin[3];
	float replay_velocity[3];
	float replay_support_normal_z;
	int replay_grounded;
	int replay_water_level;
	int replay_water_type;
};

struct sg_rune_compact_geometry_s
{
	const sg_rune_compact_geometry_view_t *view;
};

struct sg_rune_compact_response_partition_s
{
	const sg_rune_compact_response_partition_view_t *view;
};

struct sg_rune_compact_mechanisms_s
{
	const sg_rune_compact_mechanisms_view_t *view;
};

struct sg_host_law_construction_s
{
	const sg_host_law_construction_view_t *view;
	const sg_host_law_view_t *host;
};

const sg_bsp_entity_angular_mover_t *SG_BspEntitySemanticsAngularMover(
	const sg_bsp_entity_semantics_t *semantics, uint32_t canonical_ordinal)
{
	const sg_bsp_entity_semantic_t *entity;

	if (semantics == NULL || semantics->entities == NULL ||
		canonical_ordinal >= semantics->entity_count)
		return NULL;
	entity = &semantics->entities[canonical_ordinal];
	if (entity->canonical_ordinal != canonical_ordinal ||
		entity->angular_mover.kind == SG_BSP_ENTITY_ANGULAR_MOVER_NONE)
		return NULL;
	return &entity->angular_mover;
}

int SG_RuneCompactResponsePartitionSealValid(
	const sg_rune_compact_response_partition_view_t *view)
{
	return view != NULL;
}

static sg_host_law_result_t FixtureLawResult(sg_host_law_status_t status)
{
	sg_host_law_result_t result;

	memset(&result, 0, sizeof(result));
	result.status = status;
	result.field = SG_HOST_LAW_FIELD_NONE;
	result.element = SG_HOST_LAW_ELEMENT_NONE;
	return result;
}

static uint32_t Bits(float value);

int SG_RuneCompactBuilderOwnerRead(
	const sg_rune_compact_builder_t *builder,
	sg_rune_compact_builder_owner_view_t *view_out)
{
	const struct sg_rune_compact_builder_s *owner =
		(const struct sg_rune_compact_builder_s *)builder;

	if (owner == NULL || owner->view == NULL || view_out == NULL)
		return 0;
	*view_out = *owner->view;
	return 1;
}

sg_host_law_result_t SG_RuneCompactBuilderOwnerPmove(
	const sg_rune_compact_builder_t *builder,
	const sg_host_collision_scene_t *scene,
	const sg_host_pmove_request_t *request,
	sg_host_pmove_result_t *result_out,
	sg_host_pmove_error_t *error_out)
{
	const struct sg_rune_compact_builder_s *owner =
		(const struct sg_rune_compact_builder_s *)builder;
	const sg_host_law_view_t *host;

	(void)scene;
	if (owner == NULL || owner->view == NULL || owner->view->host_law == NULL ||
		request == NULL || result_out == NULL || error_out == NULL)
		return FixtureLawResult(SG_HOST_LAW_INVALID_ARGUMENT);
	host = owner->view->host_law;
	memset(result_out, 0, sizeof(*result_out));
	result_out->state = request->state;
	result_out->origin[0] = (float)request->state.origin[0] * 0.125f;
	result_out->origin[1] = (float)request->state.origin[1] * 0.125f;
	result_out->origin[2] = (float)request->state.origin[2] * 0.125f;
	result_out->velocity[0] = (float)request->state.velocity[0] * 0.125f;
	result_out->velocity[1] = (float)request->state.velocity[1] * 0.125f;
	result_out->velocity[2] = (float)request->state.velocity[2] * 0.125f;
	if (owner->replay_enabled != 0) {
		uint32_t axis;

		for (axis = 0U; axis < 3U; axis++) {
			result_out->origin[axis] = owner->replay_origin[axis];
			result_out->velocity[axis] = owner->replay_velocity[axis];
			result_out->state.origin[axis] =
				(short)lrintf(owner->replay_origin[axis] * 8.0f);
			result_out->state.velocity[axis] =
				(short)lrintf(owner->replay_velocity[axis] * 8.0f);
		}
		result_out->grounded = owner->replay_grounded;
		result_out->water_level = owner->replay_water_level;
		result_out->water_type = owner->replay_water_type;
		result_out->trace_count = 1U;
		result_out->collision_trace_count =
			owner->replay_support_normal_z < 1.0f ? 1U : 0U;
	}
	result_out->elapsed_ms = host->static_identity.physics.frame_ms;
	result_out->evaluated_steps = host->static_identity.physics.frame_ms /
		host->static_identity.physics.substep_ms;
	result_out->physics_abi_id = host->pmove_abi.identity;
	result_out->gravity_law_id = host->gravity_law_id;
	result_out->gravity = host->static_identity.physics.gravity;
	*error_out = SG_HOST_PMOVE_ERROR_NONE;
	if (owner->pmove_calls != NULL)
		(*owner->pmove_calls)++;
	return FixtureLawResult(SG_HOST_LAW_OK);
}

sg_host_law_result_t SG_RuneCompactBuilderOwnerReplayFrame(
	const sg_rune_compact_builder_t *builder,
	const sg_host_collision_scene_t *scene,
	const sg_host_pmove_request_t *request,
	const sg_host_pmove_replay_workspace_t *workspace,
	sg_host_pmove_replay_t *replay_out, sg_host_pmove_error_t *error_out)
{
	sg_host_pmove_result_t result;
	sg_host_law_result_t law_result;
	size_t index;

	if (workspace == NULL || replay_out == NULL ||
		workspace->substep_capacity < 4U)
		return FixtureLawResult(SG_HOST_LAW_INVALID_ARGUMENT);
	law_result = SG_RuneCompactBuilderOwnerPmove(builder, scene, request,
		&result, error_out);
	if (law_result.status != SG_HOST_LAW_OK)
		return law_result;
	memset(replay_out, 0, sizeof(*replay_out));
	replay_out->request = *request;
	replay_out->result = result;
	replay_out->substeps = workspace->substeps;
	replay_out->substep_count = 4U;
	replay_out->traces = workspace->traces;
	replay_out->trace_count = result.trace_count;
	for (index = 0U; index < replay_out->substep_count; index++) {
		sg_host_pmove_substep_t *step = &workspace->substeps[index];

		memset(step, 0, sizeof(*step));
		step->state = result.state;
		step->before_state = request->state;
		memcpy(step->before_origin, result.origin, sizeof(step->before_origin));
		memcpy(step->before_velocity, result.velocity,
			sizeof(step->before_velocity));
		memcpy(step->origin, result.origin, sizeof(step->origin));
		memcpy(step->velocity, result.velocity, sizeof(step->velocity));
		step->stance = (result.state.pm_flags & PMF_DUCKED) != 0U ?
			SG_RUNE_STANCE_CROUCHING : SG_RUNE_STANCE_STANDING;
		step->grounded = result.grounded;
		step->water_level = result.water_level;
		step->water_type = result.water_type;
		step->step = (uint32_t)index;
		step->elapsed_ms = (uint32_t)(index + 1U) * 25U;
	}
	if (result.trace_count != 0U) {
		memset(&workspace->traces[0], 0, sizeof(workspace->traces[0]));
		workspace->traces[0].ordinal = 1U;
		workspace->traces[0].result.fraction =
			((const struct sg_rune_compact_builder_s *)builder)->
				replay_support_normal_z < 1.0f ? 0.5f : 1.0f;
		workspace->traces[0].result.plane.normal[2] =
			((const struct sg_rune_compact_builder_s *)builder)->
				replay_support_normal_z;
	}
	replay_out->bsp_identity =
		((const struct sg_rune_compact_builder_s *)builder)->view->host_law->
		bsp_identity;
	replay_out->bsp_content_id = UINT64_C(1);
	replay_out->physics_abi_id = result.physics_abi_id;
	replay_out->frame_ms = 100U;
	replay_out->substep_ms = 25U;
	return law_result;
}

sg_host_law_result_t SG_RuneCompactBuilderOwnerReplayLocalQ8Pose(
	const sg_rune_compact_builder_t *builder, uint32_t mover_entity_ordinal,
	const sg_host_collision_world_transform_t *transform,
	const sg_rune_q8_vec3_t *local_pose, sg_rune_vec3_t *world_pose_out)
{
	const struct sg_rune_compact_builder_s *owner =
		(const struct sg_rune_compact_builder_s *)builder;
	const sg_bsp_entity_semantic_t *entity;
	float local[3];
	uint32_t local_axis;
	uint32_t world_axis;

	if (owner == NULL || owner->view == NULL ||
		owner->view->entity_semantics == NULL || transform == NULL ||
		local_pose == NULL || world_pose_out == NULL ||
		mover_entity_ordinal >= owner->view->entity_semantics->entity_count)
		return FixtureLawResult(SG_HOST_LAW_INVALID_ARGUMENT);
	entity = &owner->view->entity_semantics->entities[mover_entity_ordinal];
	if (entity->canonical_ordinal != mover_entity_ordinal ||
		entity->bsp_model == SG_HOST_COLLISION_MODEL_WORLD)
		return FixtureLawResult(SG_HOST_LAW_EVALUATION_FAILED);
	for (local_axis = 0U; local_axis < 3U; local_axis++)
		local[local_axis] = (float)local_pose->value[local_axis] * 0.125f;
	for (world_axis = 0U; world_axis < 3U; world_axis++) {
		if (!isfinite(transform->origin[world_axis]) ||
			(transform->origin[world_axis] == 0.0f &&
				Bits(transform->origin[world_axis]) != 0U))
			return FixtureLawResult(SG_HOST_LAW_EVALUATION_FAILED);
		for (local_axis = 0U; local_axis < 3U; local_axis++)
			if (!isfinite(transform->axis[local_axis][world_axis]) ||
				(transform->axis[local_axis][world_axis] == 0.0f &&
					Bits(transform->axis[local_axis][world_axis]) != 0U))
				return FixtureLawResult(SG_HOST_LAW_EVALUATION_FAILED);
		world_pose_out->value[world_axis] =
			local[0] * transform->axis[0][world_axis] +
			local[1] * transform->axis[1][world_axis] +
			local[2] * transform->axis[2][world_axis] +
			transform->origin[world_axis];
		if (!isfinite(world_pose_out->value[world_axis]))
			return FixtureLawResult(SG_HOST_LAW_EVALUATION_FAILED);
		if (world_pose_out->value[world_axis] == 0.0f)
			world_pose_out->value[world_axis] = 0.0f;
	}
	return FixtureLawResult(SG_HOST_LAW_OK);
}

int SG_RuneCompactGeometryRead(const sg_rune_compact_geometry_t *geometry,
	sg_rune_compact_geometry_view_t *view_out)
{
	const struct sg_rune_compact_geometry_s *owner =
		(const struct sg_rune_compact_geometry_s *)geometry;

	if (owner == NULL || owner->view == NULL || view_out == NULL)
		return 0;
	*view_out = *owner->view;
	return 1;
}

int SG_RuneCompactResponsePartitionRead(
	const sg_rune_compact_response_partition_t *partition,
	sg_rune_compact_response_partition_view_t *view_out)
{
	const struct sg_rune_compact_response_partition_s *owner =
		(const struct sg_rune_compact_response_partition_s *)partition;

	if (owner == NULL || owner->view == NULL || view_out == NULL)
		return 0;
	*view_out = *owner->view;
	return 1;
}

int SG_RuneCompactMechanismsRead(const sg_rune_compact_mechanisms_t *mechanisms,
	sg_rune_compact_mechanisms_view_t *view_out)
{
	const struct sg_rune_compact_mechanisms_s *owner =
		(const struct sg_rune_compact_mechanisms_s *)mechanisms;

	if (owner == NULL || owner->view == NULL || view_out == NULL)
		return 0;
	*view_out = *owner->view;
	return 1;
}

sg_host_law_result_t SG_HostLawConstructionRead(
	const sg_host_law_construction_t *construction,
	sg_host_law_construction_view_t *view_out)
{
	const struct sg_host_law_construction_s *owner =
		(const struct sg_host_law_construction_s *)construction;

	if (owner == NULL || owner->view == NULL || view_out == NULL)
		return FixtureLawResult(SG_HOST_LAW_INVALID_ARGUMENT);
	*view_out = *owner->view;
	/* The construction owner is the live authority.  Keep the publication
	 * metadata from the captured view, but reread mutable host laws at the
	 * boundary so detached snapshots cannot bless a changed Pmove contract. */
	if (owner->host != NULL) {
		view_out->host_static_identity = owner->host->static_identity;
		view_out->laws = *owner->host;
	}
	return FixtureLawResult(SG_HOST_LAW_OK);
}

int SG_RuneCompactStaticMaterializerReadBound(
	const sg_rune_compact_static_materializer_t *materializer,
	sg_rune_compact_identity_t *identity_out,
	sg_rune_compact_static_t *static_out)
{
	const struct sg_rune_compact_static_materializer_s *owner =
		(const struct sg_rune_compact_static_materializer_s *)materializer;

	if (owner == NULL || identity_out == NULL || static_out == NULL)
		return 0;
	*identity_out = owner->identity;
	if (owner->static_data == NULL)
		return 0;
	*static_out = *owner->static_data;
	return 1;
}

int SG_RuneCompactStaticMaterializerAuthorityTransitionStaticIndex(
	const sg_rune_compact_static_materializer_t *materializer,
	uint32_t authority_transition_index, uint32_t *static_transition_index_out)
{
	const struct sg_rune_compact_static_materializer_s *owner =
		(const struct sg_rune_compact_static_materializer_s *)materializer;
	uint32_t static_transition_index;

	if (owner == NULL || owner->static_data == NULL ||
		static_transition_index_out == NULL ||
		owner->authority_transition_static == NULL ||
		authority_transition_index >= owner->authority_transition_count)
		return 0;
	static_transition_index =
		owner->authority_transition_static[authority_transition_index];
	if (static_transition_index >= owner->static_data->transition_count)
		return 0;
	*static_transition_index_out = static_transition_index;
	return 1;
}

int SG_RuneCompactStaticMaterializerStaticMechanismAuthorityIndex(
	const sg_rune_compact_static_materializer_t *materializer,
	uint32_t static_mechanism_index, uint32_t *authority_mechanism_index_out)
{
	const struct sg_rune_compact_static_materializer_s *owner =
		(const struct sg_rune_compact_static_materializer_s *)materializer;
	uint32_t authority_mechanism_index;

	if (owner == NULL || owner->static_data == NULL ||
		authority_mechanism_index_out == NULL ||
		owner->static_mechanism_authority == NULL ||
		static_mechanism_index >= owner->static_mechanism_count)
		return 0;
	authority_mechanism_index =
		owner->static_mechanism_authority[static_mechanism_index];
	if (authority_mechanism_index >= owner->static_data->mechanism_count)
		return 0;
	*authority_mechanism_index_out = authority_mechanism_index;
	return 1;
}

enum
{
	CELL_COUNT = 3,
	FACET_COUNT = 3,
	INCIDENCE_COUNT = 4,
	PORTAL_COUNT = 1,
	PARTITION_COUNT = 3
};

/* This is the exact number of integer intervals emitted for the stock
 * CTF_HookPullVelocity ladder.  It is a law partition, not a constructor
 * work limit. */
#define EXPECTED_HOOK_LADDER_CLAUSE_COUNT 113U

typedef struct movement_fixture_s
{
	sg_rune_compact_cell_t cells[CELL_COUNT];
	sg_rune_compact_facet_t facets[FACET_COUNT];
	sg_rune_compact_incidence_t incidences[INCIDENCE_COUNT];
	sg_rune_compact_portal_t portals[PORTAL_COUNT];
	sg_configuration_semantic_region_t regions[CELL_COUNT];
	sg_configuration_semantics_t configuration;
	sg_static_visibility_partition_t partitions[PARTITION_COUNT];
	uint32_t area_components[PARTITION_COUNT];
	sg_static_visibility_occluder_t occluders[1];
	sg_static_visibility_surface_t visibility_surfaces[1];
	sg_static_visibility_t visibility;
	sg_configuration_hook_surface_t hook_surfaces[1];
	sg_rune_compact_facet_annotation_t annotations[1];
	sg_rune_q8_vec3_t geometry_vertices[3];
	sg_rune_compact_incidence_index_t cell_incidences[1];
	sg_rune_compact_source_surface_t source_surfaces[1];
	sg_rune_compact_geometry_view_t geometry;
	sg_rune_compact_mechanism_t mechanisms[2];
	sg_rune_compact_static_mechanism_controller_t static_controllers[2];
	sg_rune_compact_mechanism_edge_t static_edges[2];
	sg_rune_compact_portal_mechanism_t portal_mechanisms[2];
	sg_rune_compact_static_t static_data;
	struct sg_rune_compact_static_materializer_s static_owner;
	sg_rune_compact_mechanism_authority_t authorities[2];
	sg_rune_compact_mechanism_controller_t controllers[2];
	sg_rune_compact_mechanism_topology_edge_t topology_edges[2];
	sg_rune_compact_mechanism_transition_t transitions[2];
	sg_rune_compact_static_transition_t static_transitions[2];
	uint32_t authority_transition_static[2];
	uint32_t static_mechanism_authority[2];
	sg_rune_compact_mechanisms_view_t mechanisms_view;
	sg_rune_compact_response_fragment_t response_fragments[1];
	sg_rune_compact_response_halfspace_t response_halfspaces[1];
	sg_rune_compact_response_patch_t response_patches[1];
	sg_rune_q8_vec3_t response_vertices[3];
	sg_rune_compact_response_pair_t response_pairs[1];
	sg_rune_compact_response_candidate_group_t response_candidates[1];
	sg_rune_compact_response_endpoint_group_t source_endpoint_groups[1];
	sg_rune_compact_response_endpoint_group_t target_endpoint_groups[1];
	uint32_t source_endpoint_members[1];
	uint32_t target_endpoint_members[1];
	sg_rune_compact_response_partition_view_t response_partition;
	sg_host_law_view_t host;
	sg_bsp_entity_semantics_t entity_semantics;
	sg_bsp_entity_semantic_t entity_semantic[2];
	sg_rune_compact_builder_owner_view_t builder_owner_view;
	struct sg_rune_compact_builder_s builder_owner;
	struct sg_rune_compact_geometry_s geometry_owner;
	struct sg_rune_compact_response_partition_s response_owner;
	struct sg_rune_compact_mechanisms_s mechanisms_owner;
	struct sg_host_law_construction_s host_owner;
	sg_host_law_construction_view_t host_construction_view;
	uint32_t pmove_calls;
	sg_rune_compact_movement_fields_input_t input;
} movement_fixture_t;

static int failures;

#if defined(SG_RUNE_COMPACT_MOVEMENT_FIELDS_TEST_WRAP_CALLOC)
static int fail_calloc_after = -1;
static uint64_t calloc_calls;

void *__real_calloc(size_t count, size_t size);
void *__wrap_calloc(size_t count, size_t size);

void *__wrap_calloc(size_t count, size_t size)
{
	calloc_calls++;
	if (fail_calloc_after == 0) {
		fail_calloc_after = -1;
		return NULL;
	}
	if (fail_calloc_after > 0)
		fail_calloc_after--;
	return __real_calloc(count, size);
}
#endif

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static void SetFloatVector(float value[3], float x, float y, float z)
{
	value[0] = x;
	value[1] = y;
	value[2] = z;
}

static uint32_t Bits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static float ScalarValue(sg_rune_analytic_scalar_bits_t scalar)
{
	float value;

	memcpy(&value, &scalar.bits, sizeof(value));
	return value;
}

static int SameFloat(float left, float right)
{
	return Bits(left) == Bits(right);
}

static uint64_t IdentityHashByte(uint64_t hash, uint8_t value)
{
	return (hash ^ (uint64_t)value) * UINT64_C(1099511628211);
}

static uint64_t CompactBspContentId(const uint8_t digest[32])
{
	static const char domain[] = "lmctf.compact.bsp-content.v1";
	uint64_t hash = UINT64_C(14695981039346656037);
	uint32_t index;

	for (index = 0U; index < (uint32_t)(sizeof(domain) - 1U); index++)
		hash = IdentityHashByte(hash, (uint8_t)domain[index]);
	for (index = 0U; index < 32U; index++)
		hash = IdentityHashByte(hash, digest[index]);
	return hash == 0U ? UINT64_C(1) : hash == UINT64_MAX ?
		UINT64_MAX - UINT64_C(1) : hash;
}

static void SyncOwnerViews(movement_fixture_t *fixture)
{
	fixture->builder_owner_view.identity = fixture->geometry.identity;
	fixture->builder_owner_view.world = NULL;
	fixture->builder_owner_view.collision =
		(const sg_host_collision_authority_t *)(const void *)fixture;
	fixture->builder_owner_view.host_law = &fixture->host;
	fixture->builder_owner_view.weapon_law = NULL;
	fixture->builder_owner_view.configuration = NULL;
	fixture->builder_owner_view.semantics = &fixture->configuration;
	fixture->builder_owner_view.entity_semantics = &fixture->entity_semantics;
	fixture->builder_owner_view.visibility = &fixture->visibility;
	fixture->builder_owner.view = &fixture->builder_owner_view;
	fixture->builder_owner.pmove_calls = &fixture->pmove_calls;
	fixture->geometry_owner.view = &fixture->geometry;
	fixture->response_owner.view = &fixture->response_partition;
	fixture->mechanisms_owner.view = &fixture->mechanisms_view;
	fixture->host_construction_view.version =
		SG_HOST_LAW_PUBLICATION_VERSION;
	fixture->host_construction_view.current = 1U;
	fixture->host_construction_view.level_generation = UINT64_C(1);
	fixture->host_construction_view.host_static_identity =
		fixture->host.static_identity;
	fixture->host_construction_view.laws = fixture->host;
	fixture->host_owner.view = &fixture->host_construction_view;
	fixture->host_owner.host = &fixture->host;
	fixture->input.builder =
		(const sg_rune_compact_builder_t *)&fixture->builder_owner;
	fixture->input.host_owner =
		(const sg_host_law_construction_t *)&fixture->host_owner;
	fixture->input.geometry_owner =
		(const sg_rune_compact_geometry_t *)&fixture->geometry_owner;
	fixture->input.response_owner =
		(const sg_rune_compact_response_partition_t *)&fixture->response_owner;
	fixture->input.mechanisms_owner =
		(const sg_rune_compact_mechanisms_t *)&fixture->mechanisms_owner;
	fixture->input.collision_scene =
		(const sg_host_collision_scene_t *)(const void *)fixture;
}

static void InitHost(sg_host_law_view_t *host)
{
	memset(host, 0, sizeof(*host));
	host->version = SG_HOST_LAW_PUBLICATION_VERSION;
	host->collision_law_id = UINT64_C(1);
	host->pmove_law_id = UINT64_C(2);
	host->gravity_law_id = UINT64_C(3);
	host->static_identity.physics_abi_id = UINT64_C(4);
	host->static_identity.physics.gravity = 800.0f;
	host->static_identity.physics.ground_acceleration = 10.0f;
	host->static_identity.physics.air_acceleration = 1.0f;
	host->static_identity.physics.water_acceleration = 10.0f;
	host->static_identity.physics.hook_acceleration = 800.0f;
	host->static_identity.physics.external_acceleration = 1.0f;
	host->static_identity.physics.water_drag = 1.0f;
	host->static_identity.physics.max_velocity = 800.0f;
	host->static_identity.physics.frame_ms = 100U;
	host->static_identity.physics.substep_ms = 25U;
	host->static_identity.host_physics_epoch = UINT32_C(1);
	host->pmove_abi.version = SG_HOST_ENGINE_PMOVE_ABI_VERSION;
	host->pmove_abi.game_api_version = 1U;
	host->pmove_abi.import_size = 1U;
	host->pmove_abi.pmove_offset = 1U;
	host->pmove_abi.pmove_size = (uint32_t)sizeof(pmove_t);
	host->pmove_abi.state_size = (uint32_t)sizeof(pmove_state_t);
	host->pmove_abi.command_size = (uint32_t)sizeof(usercmd_t);
	host->pmove_abi.fraction_bits = SG_HOST_ENGINE_PMOVE_FRACTION_BITS;
	host->pmove_abi.substep_ms = SG_HOST_ENGINE_PMOVE_SUBSTEP_MS;
	host->pmove_abi.identity = SG_HOST_ENGINE_PMOVE_ABI_ID;
	host->pmove_behavior_fingerprint = host->pmove_abi.identity;
	SetFloatVector(host->static_identity.standing_hull.mins.value,
		-16.0f, -16.0f, -24.0f);
	SetFloatVector(host->static_identity.standing_hull.maxs.value,
		16.0f, 16.0f, 32.0f);
	SetFloatVector(host->static_identity.crouching_hull.mins.value,
		-16.0f, -16.0f, -24.0f);
	SetFloatVector(host->static_identity.crouching_hull.maxs.value,
		16.0f, 16.0f, 4.0f);
	host->airaccelerate = 0.0f;
	host->maxvelocity = 800.0f;
	SG_HostHookLawDefault(&host->hook);
	host->hook_law_id = host->hook.identity;
	host->hook_fire_speed = host->hook.fire_speed;
	host->hook_pull_speed = host->hook.pull_speed;
	host->hook_initial_damage = host->hook.initial_damage;
	host->hook_attached_damage = host->hook.attached_damage;
	host->hook_health = host->hook.projectile_health;
	SG_HostMechanismLawDefault(&host->mechanism);
	host->mechanism_law_id = host->mechanism.identity;
}

static void SyncFixtureIdentity(movement_fixture_t *fixture)
{
	sg_rune_compact_identity_t *identity = &fixture->geometry.identity;
	uint32_t index;

	identity->physics.gravity_bits = Bits(
		fixture->host.static_identity.physics.gravity);
	identity->physics.ground_acceleration_bits = Bits(
		fixture->host.static_identity.physics.ground_acceleration);
	identity->physics.air_acceleration_bits = Bits(
		fixture->host.static_identity.physics.air_acceleration);
	identity->physics.water_acceleration_bits = Bits(
		fixture->host.static_identity.physics.water_acceleration);
	identity->physics.hook_acceleration_bits = Bits(
		fixture->host.static_identity.physics.hook_acceleration);
	identity->physics.external_acceleration_bits = Bits(
		fixture->host.static_identity.physics.external_acceleration);
	identity->physics.water_drag_bits = Bits(
		fixture->host.static_identity.physics.water_drag);
	identity->physics.max_velocity_bits = Bits(
		fixture->host.static_identity.physics.max_velocity);
	identity->physics.frame_ms = fixture->host.static_identity.physics.frame_ms;
	identity->physics.substep_ms = fixture->host.static_identity.physics.substep_ms;
	for (index = 0U; index < 3U; index++) {
		identity->standing_hull.mins.value[index] = (int32_t)lrintf(
			fixture->host.static_identity.standing_hull.mins.value[index] * 8.0f);
		identity->standing_hull.maxs.value[index] = (int32_t)lrintf(
			fixture->host.static_identity.standing_hull.maxs.value[index] * 8.0f);
		identity->crouching_hull.mins.value[index] = (int32_t)lrintf(
			fixture->host.static_identity.crouching_hull.mins.value[index] * 8.0f);
		identity->crouching_hull.maxs.value[index] = (int32_t)lrintf(
			fixture->host.static_identity.crouching_hull.maxs.value[index] * 8.0f);
	}
	fixture->configuration.identity.bsp_content_id =
		CompactBspContentId(identity->bsp_sha256);
	fixture->configuration.identity.entity_semantics_id =
		identity->entity_semantics_id;
	fixture->configuration.identity.physics_abi_id = identity->physics_abi_id;
	fixture->configuration.identity.source_set_identity = UINT64_C(10);
	fixture->configuration.identity.schema_id = identity->schema_id;
	fixture->configuration.identity.producer_identity =
		identity->producer_identity;
	fixture->configuration.identity.standing_hull =
		fixture->host.static_identity.standing_hull;
	fixture->configuration.identity.crouching_hull =
		fixture->host.static_identity.crouching_hull;
	fixture->configuration.identity.physics =
		fixture->host.static_identity.physics;
	fixture->visibility.identity = fixture->configuration.identity;
	fixture->host.bsp_identity = fixture->host.static_identity.bsp_identity;
	fixture->host.bsp_bytes = fixture->host.static_identity.bsp_bytes;
	fixture->static_owner.identity = *identity;
	fixture->mechanisms_view.identity = *identity;
	fixture->response_partition.identity = *identity;
	SyncOwnerViews(fixture);
}

static void InitFixture(movement_fixture_t *fixture)
{
	uint32_t index;

	memset(fixture, 0, sizeof(*fixture));
	for (index = 0U; index < CELL_COUNT; index++) {
		sg_rune_compact_cell_t *cell = &fixture->cells[index];
		sg_configuration_semantic_region_t *region = &fixture->regions[index];

		cell->bounds.mins.value[0] = (int32_t)(index * 128U);
		cell->bounds.mins.value[1] = 0;
		cell->bounds.mins.value[2] = 0;
		cell->bounds.maxs.value[0] = (int32_t)((index + 1U) * 128U);
		cell->bounds.maxs.value[1] = 128;
		cell->bounds.maxs.value[2] = 128;
		cell->valid_stances = SG_RUNE_STANCE_VALID_ALL;
		region->cell = index;
		region->bounds.mins.value[0] = (float)(index * 128U);
		region->bounds.mins.value[1] = 0.0f;
		region->bounds.mins.value[2] = 0.0f;
		region->bounds.maxs.value[0] = (float)((index + 1U) * 128U);
		region->bounds.maxs.value[1] = 128.0f;
		region->bounds.maxs.value[2] = 128.0f;
		SetFloatVector(region->interior_witness.value,
			(float)(index * 128U) + 32.0f, 32.0f, 32.0f);
		region->flags = SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED;
	}
	fixture->cells[1].contents = SG_RUNE_COMPACT_CONTENTS_WATER;
	fixture->regions[1].flags |= SG_CONFIGURATION_SEMANTIC_REGION_WATER;
	for (index = 0U; index < FACET_COUNT; index++) {
		fixture->facets[index].kind = SG_RUNE_COMPACT_FACET_POLYGON;
		fixture->facets[index].portal.value = SG_RUNE_COMPACT_INDEX_NONE;
	}
	fixture->facets[1].kind = SG_RUNE_COMPACT_FACET_CONSTRAINT_ONLY;
	fixture->facets[2].kind = SG_RUNE_COMPACT_FACET_CONSTRAINT_ONLY;
	fixture->facets[0].plane.normal_bits[0] = Bits(1.0f);
	fixture->facets[0].plane.normal_bits[1] = Bits(0.0f);
	fixture->facets[0].plane.normal_bits[2] = Bits(0.0f);
	fixture->facets[0].plane.distance_bits = Bits(100.0f);
	fixture->facets[0].portal.value = 0U;
	fixture->facets[0].vertices.first = 0U;
	fixture->facets[0].vertices.count = 3U;
	fixture->incidences[0].cell.value = 0U;
	fixture->incidences[0].facet.value = 0U;
	fixture->incidences[0].side = SG_RUNE_FACET_NEGATIVE_SIDE;
	fixture->incidences[0].boundary = SG_RUNE_BOUNDARY_OPEN;
	fixture->incidences[1].cell.value = 1U;
	fixture->incidences[1].facet.value = 0U;
	fixture->incidences[1].side = SG_RUNE_FACET_POSITIVE_SIDE;
	fixture->incidences[1].boundary = SG_RUNE_BOUNDARY_OPEN;
	fixture->incidences[2].cell.value = 1U;
	fixture->incidences[2].facet.value = 1U;
	fixture->incidences[2].side = SG_RUNE_FACET_NEGATIVE_SIDE;
	fixture->incidences[2].boundary = SG_RUNE_BOUNDARY_CLOSED;
	fixture->incidences[3].cell.value = 2U;
	fixture->incidences[3].facet.value = 2U;
	fixture->incidences[3].side = SG_RUNE_FACET_NEGATIVE_SIDE;
	fixture->incidences[3].boundary = SG_RUNE_BOUNDARY_CLOSED;
	fixture->portals[0].negative_incidence.value = 0U;
	fixture->portals[0].positive_incidence.value = 1U;
	fixture->portals[0].facet.value = 0U;
	fixture->portals[0].clearance_q8 = 64U;
	fixture->portals[0].direction = SG_RUNE_PORTAL_CONTINUITY_BOTH;
	fixture->portals[0].valid_stances = SG_RUNE_STANCE_VALID_ALL;
	fixture->configuration.regions = fixture->regions;
	fixture->configuration.region_count = CELL_COUNT;
	fixture->visibility.partitions = fixture->partitions;
	fixture->visibility.partition_count = PARTITION_COUNT;
	fixture->visibility.area_components = fixture->area_components;
	fixture->visibility.area_count = PARTITION_COUNT;
	for (index = 0U; index < PARTITION_COUNT; index++) {
		fixture->partitions[index].id = (uint64_t)index + UINT64_C(1);
		fixture->partitions[index].configuration_region = index;
		fixture->partitions[index].configuration_cell = index;
		fixture->partitions[index].bsp_area = index;
		fixture->partitions[index].bsp_cluster = index;
		fixture->area_components[index] = index;
	}
	fixture->hook_surfaces[0].id = UINT64_C(77);
	fixture->hook_surfaces[0].model = SG_HOST_COLLISION_MODEL_WORLD;
	fixture->hook_surfaces[0].brush = 1U;
	fixture->hook_surfaces[0].normal[0] = 1.0f;
	fixture->hook_surfaces[0].normal[1] = 0.0f;
	fixture->hook_surfaces[0].normal[2] = 0.0f;
	fixture->hook_surfaces[0].distance = 100.0f;
	fixture->hook_surfaces[0].bounds.mins.value[0] = 100.0f;
	fixture->hook_surfaces[0].bounds.mins.value[1] = 16.0f;
	fixture->hook_surfaces[0].bounds.mins.value[2] = 16.0f;
	fixture->hook_surfaces[0].bounds.maxs.value[0] = 101.0f;
	fixture->hook_surfaces[0].bounds.maxs.value[1] = 112.0f;
	fixture->hook_surfaces[0].bounds.maxs.value[2] = 112.0f;
	fixture->hook_surfaces[0].flags =
		SG_CONFIGURATION_HOOK_SURFACE_HOOKABLE;
	fixture->configuration.hook_surfaces = fixture->hook_surfaces;
	fixture->configuration.hook_surface_count = 1U;
	fixture->visibility.surfaces = fixture->visibility_surfaces;
	fixture->visibility.surface_count = 1U;
	fixture->visibility_surfaces[0].id = fixture->hook_surfaces[0].id;
	fixture->visibility_surfaces[0].semantic_surface = 0U;
	fixture->visibility_surfaces[0].flags =
		SG_CONFIGURATION_HOOK_SURFACE_HOOKABLE;
	fixture->static_data.facet_annotations = fixture->annotations;
	fixture->static_data.facet_annotation_count = 1U;
	fixture->annotations[0].facet.value = 0U;
	fixture->annotations[0].attributes = SG_RUNE_COMPACT_FACET_HOOKABLE;
	fixture->annotations[0].hookable_stances = SG_RUNE_STANCE_VALID_ALL;
	fixture->static_data.mechanisms = fixture->mechanisms;
	fixture->static_data.mechanism_count = 1U;
	fixture->mechanisms[0].entry_cell.value = 0U;
	fixture->mechanisms[0].exit_cell.value = 1U;
	fixture->mechanisms[0].source.entity_ordinal = 0U;
	fixture->mechanisms[0].bounds = fixture->cells[0].bounds;
	fixture->mechanisms[0].kind = SG_RUNE_COMPACT_MECHANISM_LIFT;
	fixture->mechanisms[0].wait_ms = 1500U;
	fixture->mechanisms[0].activation_mask =
		SG_RUNE_COMPACT_STATIC_ACTIVATION_MASK_AUTO;
	fixture->mechanisms[0].required_item = SG_BSP_ENTITY_STRING_NONE;
	fixture->mechanisms[0].activation_landmark.value =
		SG_RUNE_COMPACT_INDEX_NONE;
	fixture->mechanisms[0].initial_state = SG_HOST_MECHANISM_STATE_BOTTOM;
	fixture->mechanisms[0].activated_state = SG_HOST_MECHANISM_STATE_TOP;
	fixture->mechanisms[0].reset_state = SG_HOST_MECHANISM_STATE_BOTTOM;
	fixture->static_data.portal_mechanisms = fixture->portal_mechanisms;
	fixture->static_data.portal_mechanism_count = 0U;
	fixture->portal_mechanisms[0].portal.value = 0U;
	fixture->portal_mechanisms[0].mechanism.value = 0U;
	fixture->portal_mechanisms[0].kind = SG_RUNE_COMPACT_PORTAL_MECHANISM_MOVES;
	InitHost(&fixture->host);
	fixture->host.static_identity.bsp_bytes = UINT64_C(4096);
	fixture->host.static_identity.engine_checksum = UINT32_C(7);
	fixture->host.static_identity.entity_crc32 = UINT32_C(8);
	for (index = 0U; index < 32U; index++)
		fixture->host.static_identity.bsp_identity.bytes[index] = 0xa5U;
	fixture->geometry.identity.bsp_bytes =
		fixture->host.static_identity.bsp_bytes;
	fixture->geometry.identity.bsp_checksum =
		fixture->host.static_identity.engine_checksum;
	fixture->geometry.identity.entity_crc32 =
		fixture->host.static_identity.entity_crc32;
	memcpy(fixture->geometry.identity.bsp_sha256,
		fixture->host.static_identity.bsp_identity.bytes, 32U);
	fixture->geometry.identity.entity_semantics_id = UINT64_C(9);
	fixture->geometry.identity.physics_abi_id = UINT64_C(4);
	fixture->geometry.identity.collision_law_id = UINT64_C(1);
	fixture->geometry.identity.pmove_law_id = UINT64_C(2);
	fixture->geometry.identity.gravity_law_id = UINT64_C(3);
	fixture->geometry.identity.hook_law_id = fixture->host.hook.identity;
	fixture->geometry.identity.mechanism_law_id =
		fixture->host.mechanism.identity;
	fixture->geometry.identity.weapon_law_id = UINT64_C(5);
	fixture->geometry.identity.construction_id = UINT64_C(6);
	fixture->geometry.identity.schema_id = UINT64_C(7);
	fixture->geometry.identity.producer_identity = UINT64_C(8);
	fixture->geometry.identity.source_counts.model_count = 2U;
	fixture->geometry.identity.source_counts.leaf_count = CELL_COUNT;
	fixture->geometry.identity.source_counts.area_count = CELL_COUNT;
	fixture->geometry.identity.source_counts.plane_count = FACET_COUNT;
	fixture->geometry.identity.source_counts.brush_count = 2U;
	fixture->geometry.identity.source_counts.brush_side_count = 2U;
	fixture->geometry.identity.source_counts.entity_count = 1U;
	fixture->entity_semantics.source_set_identity = UINT64_C(1);
	fixture->entity_semantics.entities = fixture->entity_semantic;
	fixture->entity_semantics.entity_count = 1U;
	fixture->entity_semantic[0].source_set_identity = UINT64_C(1);
	fixture->entity_semantic[0].canonical_ordinal = 0U;
	fixture->entity_semantic[0].mechanism_kind = SG_RUNE_MECHANISM_LIFT;
	fixture->entity_semantic[0].bsp_model = 1U;
	fixture->geometry_vertices[0].value[0] = 800;
	fixture->geometry_vertices[0].value[1] = 128;
	fixture->geometry_vertices[0].value[2] = 128;
	fixture->geometry_vertices[1].value[0] = 800;
	fixture->geometry_vertices[1].value[1] = 896;
	fixture->geometry_vertices[1].value[2] = 128;
	fixture->geometry_vertices[2].value[0] = 800;
	fixture->geometry_vertices[2].value[1] = 128;
	fixture->geometry_vertices[2].value[2] = 896;
	fixture->cell_incidences[0].value = 0U;
	fixture->geometry.cells = fixture->cells;
	fixture->geometry.cell_count = CELL_COUNT;
	fixture->geometry.facets = fixture->facets;
	fixture->geometry.facet_count = FACET_COUNT;
	fixture->geometry.incidences = fixture->incidences;
	fixture->geometry.incidence_count = INCIDENCE_COUNT;
	fixture->geometry.cell_incidences = fixture->cell_incidences;
	fixture->geometry.cell_incidence_count = 1U;
	fixture->geometry.vertices = fixture->geometry_vertices;
	fixture->geometry.vertex_count = 3U;
	fixture->geometry.portals = fixture->portals;
	fixture->geometry.portal_count = PORTAL_COUNT;
	fixture->geometry.source_surfaces = fixture->source_surfaces;
	fixture->geometry.source_surface_count = 1U;
	fixture->geometry.source_surface_vertices = fixture->geometry_vertices;
	fixture->geometry.source_surface_vertex_count = 3U;
	fixture->source_surfaces[0].source.model =
		SG_HOST_COLLISION_MODEL_WORLD;
	fixture->source_surfaces[0].source.brush = fixture->hook_surfaces[0].brush;
	fixture->source_surfaces[0].source.brush_side =
		fixture->hook_surfaces[0].brush_side;
	fixture->source_surfaces[0].frame = SG_RUNE_COMPACT_SOURCE_SURFACE_WORLD;
	fixture->source_surfaces[0].cell.value = 0U;
	fixture->source_surfaces[0].parent_surface = SG_RUNE_COMPACT_INDEX_NONE;
	fixture->source_surfaces[0].plane = fixture->facets[0].plane;
	fixture->source_surfaces[0].vertices.first = 0U;
	fixture->source_surfaces[0].vertices.count = 3U;
	SyncFixtureIdentity(fixture);
	fixture->visibility_surfaces[0].model = fixture->hook_surfaces[0].model;
	fixture->visibility_surfaces[0].brush = fixture->hook_surfaces[0].brush;
	fixture->visibility_surfaces[0].brush_side =
		fixture->hook_surfaces[0].brush_side;
	fixture->static_owner.identity = fixture->geometry.identity;
	fixture->static_owner.static_data = &fixture->static_data;
	fixture->static_owner.authority_transition_static =
		fixture->authority_transition_static;
	fixture->static_owner.authority_transition_count = 0U;
	fixture->static_owner.static_mechanism_authority =
		fixture->static_mechanism_authority;
	fixture->static_owner.static_mechanism_count = 1U;
	fixture->static_mechanism_authority[0] = 0U;
	memset(&fixture->mechanisms_view, 0, sizeof(fixture->mechanisms_view));
	fixture->mechanisms_view.identity = fixture->geometry.identity;
	fixture->mechanisms_view.mechanisms = fixture->authorities;
	fixture->mechanisms_view.mechanism_count = 1U;
	fixture->authorities[0].source.entity_ordinal = 0U;
	fixture->authorities[0].kind = SG_RUNE_COMPACT_MECHANISM_AUTHORITY_LIFT;
	fixture->authorities[0].activation =
		SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_AUTO;
	fixture->authorities[0].activation_cell.value = 0U;
	fixture->authorities[0].activation_bounds = fixture->cells[0].bounds;
	fixture->authorities[0].activation_witness.value[0] = 32;
	fixture->authorities[0].activation_witness.value[1] = 32;
	fixture->authorities[0].activation_witness.value[2] = 32;
	fixture->authorities[0].controllers.first = 0U;
	fixture->authorities[0].controllers.count = 0U;
	fixture->authorities[0].topology.first = 0U;
	fixture->authorities[0].topology.count = 0U;
	fixture->authorities[0].transitions.first = 0U;
	fixture->authorities[0].transitions.count = 0U;
	fixture->authorities[0].initial_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture->authorities[0].activated_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture->authorities[0].reset_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE;
	fixture->mechanisms_view.controllers = NULL;
	fixture->mechanisms_view.controller_count = 0U;
	fixture->mechanisms_view.topology_edges = NULL;
	fixture->mechanisms_view.topology_edge_count = 0U;
	fixture->mechanisms_view.transitions = NULL;
	fixture->mechanisms_view.transition_count = 0U;
	memset(&fixture->response_partition, 0,
		sizeof(fixture->response_partition));
	fixture->response_partition.identity = fixture->geometry.identity;
	fixture->response_partition.source_fragments = fixture->response_fragments;
	fixture->response_partition.source_fragment_count = 1U;
	fixture->response_partition.source_halfspaces = fixture->response_halfspaces;
	fixture->response_partition.source_halfspace_count = 1U;
	fixture->response_fragments[0].parent_cell.value = 0U;
	fixture->response_fragments[0].static_partition_id =
		fixture->partitions[0].id;
	fixture->response_fragments[0].configuration_region = 0U;
	fixture->response_fragments[0].configuration_cell = 0U;
	fixture->response_fragments[0].first_halfspace = 0U;
	fixture->response_fragments[0].halfspace_count = 1U;
	fixture->response_fragments[0].bounds = fixture->cells[0].bounds;
	fixture->response_fragments[0].witness =
		fixture->authorities[0].activation_witness;
	fixture->response_fragments[0].bsp_leaf = 0U;
	fixture->response_fragments[0].bsp_area = 0U;
	fixture->response_fragments[0].bsp_cluster = 0;
	fixture->response_fragments[0].valid_stances =
		SG_RUNE_STANCE_VALID_ALL;
	fixture->response_partition.target_patches = fixture->response_patches;
	fixture->response_partition.target_patch_count = 1U;
	fixture->response_partition.target_vertices = fixture->response_vertices;
	fixture->response_partition.target_vertex_count = 3U;
	fixture->source_endpoint_groups[0].bsp_cluster = 0U;
	fixture->source_endpoint_groups[0].bsp_area = 0U;
	fixture->source_endpoint_groups[0].first_member = 0U;
	fixture->source_endpoint_groups[0].member_count = 1U;
	fixture->source_endpoint_members[0] = 0U;
	fixture->target_endpoint_groups[0].bsp_cluster = 0U;
	fixture->target_endpoint_groups[0].bsp_area = 0U;
	fixture->target_endpoint_groups[0].first_member = 0U;
	fixture->target_endpoint_groups[0].member_count = 1U;
	fixture->target_endpoint_members[0] = 0U;
	fixture->response_partition.source_endpoint_groups =
		fixture->source_endpoint_groups;
	fixture->response_partition.source_endpoint_group_count = 1U;
	fixture->response_partition.source_endpoint_members =
		fixture->source_endpoint_members;
	fixture->response_partition.source_endpoint_member_count = 1U;
	fixture->response_partition.target_endpoint_groups =
		fixture->target_endpoint_groups;
	fixture->response_partition.target_endpoint_group_count = 1U;
	fixture->response_partition.target_endpoint_members =
		fixture->target_endpoint_members;
	fixture->response_partition.target_endpoint_member_count = 1U;
	fixture->response_patches[0].visibility_surface_id =
		fixture->visibility_surfaces[0].id;
	fixture->response_patches[0].model =
		fixture->visibility_surfaces[0].model;
	fixture->response_patches[0].brush =
		fixture->visibility_surfaces[0].brush;
	fixture->response_patches[0].brush_side =
		fixture->visibility_surfaces[0].brush_side;
	fixture->response_patches[0].source_surface = 0U;
	fixture->response_patches[0].source_frame =
		SG_RUNE_COMPACT_SOURCE_SURFACE_WORLD;
	fixture->response_patches[0].plane = fixture->source_surfaces[0].plane;
	fixture->response_patches[0].parent_facet.value = 0U;
	fixture->response_patches[0].boundary_incidences =
		fixture->facets[0].incidences;
	fixture->response_patches[0].target_cell.value = 0U;
	fixture->response_patches[0].static_partition_id =
		fixture->partitions[0].id;
	fixture->response_patches[0].configuration_region = 0U;
	fixture->response_patches[0].configuration_cell = 0U;
	fixture->response_patches[0].first_vertex = 0U;
	fixture->response_patches[0].vertex_count = 3U;
	fixture->response_patches[0].bounds = fixture->cells[0].bounds;
	fixture->response_patches[0].bsp_leaf = 0U;
	fixture->response_patches[0].bsp_area = 0U;
	fixture->response_patches[0].bsp_cluster = 0;
	fixture->response_patches[0].flags =
		SG_RUNE_COMPACT_RESPONSE_PATCH_HOOKABLE;
	fixture->response_patches[0].valid_stances = SG_RUNE_STANCE_VALID_ALL;
	fixture->response_vertices[0].value[0] = 8;
	fixture->response_vertices[0].value[1] = 8;
	fixture->response_vertices[0].value[2] = 8;
	fixture->response_vertices[1].value[0] = 16;
	fixture->response_vertices[1].value[1] = 8;
	fixture->response_vertices[1].value[2] = 8;
	fixture->response_vertices[2].value[0] = 8;
	fixture->response_vertices[2].value[1] = 16;
	fixture->response_vertices[2].value[2] = 8;
	fixture->response_partition.response_pairs = fixture->response_pairs;
	fixture->response_partition.response_pair_count = 1U;
	fixture->response_partition.candidate_groups = fixture->response_candidates;
	fixture->response_partition.candidate_group_count = 1U;
	fixture->response_candidates[0].source_group = 0U;
	fixture->response_candidates[0].target_group = 0U;
	fixture->response_candidates[0].classification =
		SG_RUNE_COMPACT_STATIC_VISIBILITY_CONDITIONAL;
	fixture->response_candidates[0].reason =
		SG_RUNE_COMPACT_STATIC_VISIBILITY_REASON_EXACT_RAY_REQUIRED;
	fixture->response_candidates[0].requires_exact_ray = 1U;
	fixture->response_pairs[0].source_fragment = 0U;
	fixture->response_pairs[0].target_patch = 0U;
	fixture->response_pairs[0].classification = SG_STATIC_VISIBILITY_CONDITIONAL;
	fixture->response_pairs[0].reason =
		SG_STATIC_VISIBILITY_REASON_EXACT_RAY_REQUIRED;
	fixture->response_pairs[0].first_hit_occluder =
		SG_STATIC_VISIBILITY_INDEX_NONE;
	fixture->response_pairs[0].requires_exact_ray = 1U;
	fixture->response_pairs[0].source_valid_stances =
		SG_RUNE_STANCE_VALID_ALL;
	fixture->response_pairs[0].target_valid_stances =
		SG_RUNE_STANCE_VALID_ALL;
	fixture->response_pairs[0].relation_flags =
		SG_RUNE_COMPACT_STATIC_RELATION_DIRECT;
	fixture->response_pairs[0].trace.fraction = 1.0f;
	fixture->response_pairs[0].trace.end[0] = 1.0f;
	fixture->response_pairs[0].trace.end[1] = 1.0f;
	fixture->response_pairs[0].trace.end[2] = 1.0f;
	fixture->response_partition.seal.version =
		SG_RUNE_COMPACT_RESPONSE_PARTITION_VERSION;
	fixture->response_partition.seal.flags =
		SG_RUNE_COMPACT_RESPONSE_SEAL_REQUIRED;
	fixture->response_partition.seal.source_fragment_count = 1U;
	fixture->response_partition.seal.target_patch_count = 1U;
	fixture->response_partition.seal.split_count = 0U;
	fixture->response_partition.seal.response_pair_count = 1U;
	fixture->response_partition.seal.certified_direct_pair_count = 1U;
	fixture->response_partition.seal.unresolved_candidate_group_count = 1U;
	fixture->response_partition.seal.source_endpoint_group_count = 1U;
	fixture->response_partition.seal.target_endpoint_group_count = 1U;
	fixture->response_partition.seal.source_endpoint_member_count = 1U;
	fixture->response_partition.seal.target_endpoint_member_count = 1U;
	fixture->input.static_owner =
		(const sg_rune_compact_static_materializer_t *)&fixture->static_owner;
	SyncOwnerViews(fixture);
}

static const sg_rune_movement_capability_t *FindField(
	const sg_rune_compact_movement_fields_view_t *view, uint32_t cell,
	uint32_t boundary, sg_rune_movement_capability_kind_t kind)
{
	uint32_t index;

	for (index = 0U; index < view->capability_count; index++) {
		const sg_rune_movement_capability_t *field =
			&view->capabilities[index];

		if (field->cell.value == cell &&
			field->boundary_portal.value == boundary && field->kind == kind)
			return field;
	}
	return NULL;
}

static const sg_rune_movement_capability_t *FindFieldForStance(
	const sg_rune_compact_movement_fields_view_t *view, uint32_t cell,
	uint32_t boundary, sg_rune_movement_capability_kind_t kind,
	sg_rune_stance_validity_t stance)
{
	uint32_t index;

	for (index = 0U; index < view->capability_count; index++) {
		const sg_rune_movement_capability_t *field =
			&view->capabilities[index];

		if (field->cell.value == cell &&
			field->boundary_portal.value == boundary &&
			field->kind == kind && field->source_stances == stance)
			return field;
	}
	return NULL;
}

static const sg_rune_analytic_function_t *FindOutput(
	const sg_rune_compact_movement_fields_view_t *view,
	const sg_rune_movement_capability_t *field,
	sg_rune_analytic_output_meaning_t output)
{
	uint32_t offset;

	const sg_rune_compact_movement_fiber_t *fiber =
		field->fibers.count == 0U ? NULL :
			&view->fibers[field->fibers.first];

	if (fiber == NULL)
		return NULL;
	for (offset = 0U; offset < fiber->functions.count; offset++) {
		const uint32_t reference = fiber->functions.first + offset;
		const uint32_t function = view->fiber_function_refs[reference].value;

		if (view->analytic.functions[function].output == output)
			return &view->analytic.functions[function];
	}
	return NULL;
}

static const sg_rune_compact_movement_hook_target_t *FindHookTargetByResponse(
	const sg_rune_compact_movement_fields_view_t *view,
	sg_rune_movement_capability_kind_t kind,
	sg_rune_stance_validity_t stance,
	sg_rune_compact_response_ref_kind_t response_kind, uint32_t response_index)
{
	uint32_t capability_index;

	for (capability_index = 0U;
		capability_index < view->capability_count; capability_index++) {
		const sg_rune_movement_capability_t *capability =
			&view->capabilities[capability_index];
		uint32_t fiber_offset;

		if (capability->kind != kind || capability->source_stances != stance)
			continue;
		for (fiber_offset = 0U; fiber_offset < capability->fibers.count;
			fiber_offset++) {
			const sg_rune_compact_movement_fiber_t *fiber =
				&view->fibers[capability->fibers.first + fiber_offset];
			uint32_t target_offset;

			for (target_offset = 0U; target_offset < fiber->hook_targets.count;
				target_offset++) {
				const sg_rune_compact_movement_hook_target_t *target =
					&view->hook_targets[fiber->hook_targets.first + target_offset];

				if (target->response.kind == response_kind &&
					target->response.index == response_index)
					return target;
			}
		}
	}
	return NULL;
}

static uint32_t CountHookTargets(
	const sg_rune_compact_movement_fields_view_t *view,
	sg_rune_movement_capability_kind_t kind,
	sg_rune_stance_validity_t stance,
	sg_rune_movement_hook_target_class_t visibility_class,
	sg_rune_compact_response_ref_kind_t response_kind)
{
	uint32_t count = 0U;
	uint32_t capability_index;

	for (capability_index = 0U;
		capability_index < view->capability_count; capability_index++) {
		const sg_rune_movement_capability_t *capability =
			&view->capabilities[capability_index];
		const sg_rune_compact_movement_fiber_t *fiber;
		uint32_t target_offset;

		if (capability->kind != kind || capability->source_stances != stance ||
			capability->fibers.count != 1U)
			continue;
		fiber = &view->fibers[capability->fibers.first];
		for (target_offset = 0U; target_offset < fiber->hook_targets.count;
			target_offset++) {
			const sg_rune_compact_movement_hook_target_t *target =
				&view->hook_targets[fiber->hook_targets.first + target_offset];

			if (target->visibility_class == visibility_class &&
				target->source_stances == stance &&
				target->target_stances == stance &&
				target->response.kind == response_kind)
				count++;
		}
	}
	return count;
}

static int FieldHasInput(const sg_rune_compact_analytic_t *analytic,
	const sg_rune_analytic_function_t *function,
	sg_rune_analytic_input_dimension_t dimension)
{
	uint32_t offset;

	for (offset = 0U; offset < function->inputs.count; offset++)
		if (analytic->input_dimensions[function->inputs.first + offset] ==
			dimension)
			return 1;
	return 0;
}

static uint32_t FunctionIndex(const sg_rune_compact_analytic_t *analytic,
	const sg_rune_analytic_function_t *function)
{
	uint32_t index;

	if (analytic == NULL || function == NULL)
		return UINT32_MAX;
	for (index = 0U; index < analytic->function_count; index++)
		if (&analytic->functions[index] == function)
			return index;
	return UINT32_MAX;
}

static uint32_t CountFamily(const sg_rune_compact_movement_fields_view_t *view,
	sg_rune_movement_capability_kind_t kind)
{
	uint32_t count = 0U;
	uint32_t index;

	for (index = 0U; index < view->capability_count; index++)
		if (view->capabilities[index].kind == kind)
			count++;
	return count;
}

static int TestSpanWithin(uint32_t first, uint32_t count, uint32_t limit);

static void SetEvaluationInputs(
	sg_rune_compact_eval_input_t inputs[SG_RUNE_ANALYTIC_INPUT_DIMENSION_COUNT])
{
	uint32_t index;

	for (index = 0U;
		index < (uint32_t)SG_RUNE_ANALYTIC_INPUT_DIMENSION_COUNT; index++) {
		inputs[index].dimension = (sg_rune_analytic_input_dimension_t)index;
		inputs[index].value = 0.0f;
	}
}

static void CheckBasicOutput(const movement_fixture_t *fixture,
	const sg_rune_compact_movement_fields_view_t *view)
{
	const sg_rune_movement_capability_t *hook = FindField(view, 0U,
		SG_RUNE_COMPACT_INDEX_NONE, SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BODY);
	const sg_rune_movement_capability_t *blocked = FindField(view, 2U,
		SG_RUNE_COMPACT_INDEX_NONE, SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BODY);
	const sg_rune_analytic_function_t *hook_position;
	const sg_rune_analytic_function_t *hook_velocity;
	const sg_rune_analytic_function_t *hook_length_time;
	uint32_t function_cursor = 0U;
	uint32_t fiber_index;
	uint32_t target_index;

	CHECK(hook != NULL);
	CHECK(blocked != NULL);
	/* One portal between two level supported cells: each direction gets a
	 * standing WALK and a crouching CROUCH.  Nothing here is stepped, sunk,
	 * airborne, or water on both sides, so no other ground family applies.
	 * RAMP is not emitted until the support plane's slope is available. */
	CHECK(CountFamily(view, SG_RUNE_MOVEMENT_CAPABILITY_WALK) == 2U);
	CHECK(CountFamily(view, SG_RUNE_MOVEMENT_CAPABILITY_CROUCH) == 2U);
	CHECK(CountFamily(view, SG_RUNE_MOVEMENT_CAPABILITY_RAMP) == 0U);
	CHECK(CountFamily(view, SG_RUNE_MOVEMENT_CAPABILITY_JUMP) == 0U);
	CHECK(CountFamily(view, SG_RUNE_MOVEMENT_CAPABILITY_DROP) == 0U);
	CHECK(CountFamily(view, SG_RUNE_MOVEMENT_CAPABILITY_SWIM) == 0U);
	CHECK(CountFamily(view, SG_RUNE_MOVEMENT_CAPABILITY_AIR_CONTROL) == 0U);
	{
		const sg_rune_movement_capability_t *walk = FindFieldForStance(view,
			0U, 0U, SG_RUNE_MOVEMENT_CAPABILITY_WALK,
			SG_RUNE_STANCE_VALID_STANDING);
		const sg_rune_movement_capability_t *crouch = FindFieldForStance(view,
			1U, 0U, SG_RUNE_MOVEMENT_CAPABILITY_CROUCH,
			SG_RUNE_STANCE_VALID_CROUCHING);

		/* A crossing keeps its stance when the far side allows it. */
		CHECK(walk != NULL && walk->destination_stances ==
			SG_RUNE_STANCE_VALID_STANDING && walk->fibers.count != 0U);
		CHECK(crouch != NULL && crouch->destination_stances ==
			SG_RUNE_STANCE_VALID_CROUCHING && crouch->fibers.count != 0U);
	}
	CHECK(CountFamily(view, SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BOLT) != 0U);
	CHECK(CountFamily(view, SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BODY) != 0U);
	CHECK(CountFamily(view, SG_RUNE_MOVEMENT_CAPABILITY_HOOK_PULL) != 0U);
	CHECK(CountFamily(view, SG_RUNE_MOVEMENT_CAPABILITY_HOOK_RELEASE) != 0U);
	CHECK(CountFamily(view, SG_RUNE_MOVEMENT_CAPABILITY_HOOK_COAST) != 0U);
	CHECK(CountFamily(view, SG_RUNE_MOVEMENT_CAPABILITY_HOOK_RELAUNCH) != 0U);
	CHECK(view->analytic.function_count != 0U &&
		view->analytic.function_count <= SG_RUNE_ANALYTIC_MAX_FUNCTIONS);
	hook_position = hook == NULL ? NULL : FindOutput(view, hook,
		SG_RUNE_ANALYTIC_OUTPUT_POSITION_X);
	hook_velocity = hook == NULL ? NULL : FindOutput(view, hook,
		SG_RUNE_ANALYTIC_OUTPUT_VELOCITY_X);
	hook_length_time = hook == NULL ? NULL : FindOutput(view, hook,
		SG_RUNE_ANALYTIC_OUTPUT_TRAVEL_TIME_SECONDS);
	CHECK(hook_position != NULL && hook_position->form ==
		SG_RUNE_COMPACT_ANALYTIC_PIECEWISE && hook_position->inputs.count == 5U &&
		FieldHasInput(&view->analytic, hook_position,
			SG_RUNE_ANALYTIC_INPUT_WORLD_X) &&
		FieldHasInput(&view->analytic, hook_position,
			SG_RUNE_ANALYTIC_INPUT_VELOCITY_X) &&
		FieldHasInput(&view->analytic, hook_position,
			SG_RUNE_ANALYTIC_INPUT_DIRECTION_X) &&
		FieldHasInput(&view->analytic, hook_position,
			SG_RUNE_ANALYTIC_INPUT_TIME_SECONDS) &&
		FieldHasInput(&view->analytic, hook_position,
			SG_RUNE_ANALYTIC_INPUT_HOOK_LENGTH));
	CHECK(hook_position != NULL && hook_position->definition <
		view->analytic.piecewise_count &&
		view->analytic.piecewise[hook_position->definition].selector_input == 4U &&
		view->analytic.piecewise[hook_position->definition].clauses.count ==
		EXPECTED_HOOK_LADDER_CLAUSE_COUNT);
	CHECK(hook_velocity != NULL && hook_velocity->form ==
		SG_RUNE_COMPACT_ANALYTIC_PIECEWISE && hook_velocity->inputs.count == 3U &&
		FieldHasInput(&view->analytic, hook_velocity,
			SG_RUNE_ANALYTIC_INPUT_VELOCITY_X) &&
		FieldHasInput(&view->analytic, hook_velocity,
			SG_RUNE_ANALYTIC_INPUT_DIRECTION_X));
	CHECK(hook_velocity != NULL && hook_velocity->definition <
		view->analytic.piecewise_count &&
		view->analytic.piecewise[hook_velocity->definition].selector_input == 2U);
	CHECK(hook_length_time != NULL && hook_length_time->form ==
		SG_RUNE_COMPACT_ANALYTIC_PIECEWISE && hook_length_time->inputs.count == 2U &&
		FieldHasInput(&view->analytic, hook_length_time,
			SG_RUNE_ANALYTIC_INPUT_HOOK_LENGTH));
	CHECK(hook_length_time != NULL && hook_length_time->definition <
		view->analytic.piecewise_count &&
		view->analytic.piecewise[hook_length_time->definition].selector_input == 1U);
	CHECK(view->state_count != 0U);
	CHECK(view->pmove_abi.identity == fixture->host.pmove_abi.identity &&
		view->pmove_behavior_fingerprint ==
			fixture->host.pmove_behavior_fingerprint);
	CHECK(hook != NULL && hook->fibers.count == 1U &&
		view->fibers[hook->fibers.first].kind == SG_RUNE_MOVEMENT_FIBER_HOOK &&
		view->fibers[hook->fibers.first].hook_targets.count != 0U);
	for (fiber_index = 0U; fiber_index < view->fiber_count; fiber_index++) {
		const sg_rune_analytic_function_span_t span =
			view->fibers[fiber_index].functions;

		CHECK(span.first == function_cursor);
		function_cursor += span.count;
	}
	for (target_index = 0U; target_index < view->hook_target_count;
		target_index++) {
		const sg_rune_compact_movement_hook_target_t *target =
			&view->hook_targets[target_index];
		const sg_rune_analytic_function_span_t spans[6] = {
			target->functions.bolt, target->functions.body,
			target->functions.pull, target->functions.release,
			target->functions.coast, target->functions.relaunch
		};
		uint32_t phase;

		CHECK(target->fiber.value < view->fiber_count);
		for (phase = 0U; phase < 6U; phase++) {
			CHECK(spans[phase].count != 0U &&
				TestSpanWithin(spans[phase].first, spans[phase].count,
					view->fiber_function_ref_count));
			CHECK(spans[phase].first == function_cursor);
			function_cursor += spans[phase].count;
		}
	}
	CHECK(function_cursor == view->fiber_function_ref_count);
}

static void CheckGenericHookTargetKinds(
	const sg_rune_compact_movement_fields_view_t *view)
{
	uint32_t generic_kinds = 0U;
	uint32_t static_world = 0U;
	uint32_t target_index;

	for (target_index = 0U; target_index < view->hook_target_count;
		target_index++) {
		const sg_rune_compact_movement_hook_target_t *target =
			&view->hook_targets[target_index];
		const sg_rune_compact_movement_fiber_t *fiber =
			&view->fibers[target->fiber.value];
		const sg_rune_movement_capability_t *capability =
			&view->capabilities[fiber->capability.value];

		if (capability->kind != SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BODY ||
			capability->source_stances != SG_RUNE_STANCE_VALID_STANDING)
			continue;
		if (target->provenance ==
			SG_RUNE_MOVEMENT_HOOK_TARGET_PROVENANCE_GENERIC) {
			CHECK(target->visibility_class ==
				SG_RUNE_MOVEMENT_HOOK_TARGET_CONDITIONAL);
			CHECK(target->response.kind ==
				SG_RUNE_COMPACT_RESPONSE_REF_KIND_COUNT);
			CHECK(target->response.index == SG_RUNE_COMPACT_INDEX_NONE);
			if (target->target_kind >= SG_HOST_HOOK_TARGET_PLAYER &&
				target->target_kind <= SG_HOST_HOOK_TARGET_INFO_FLAG)
				generic_kinds |= UINT32_C(1) <<
					((uint32_t)target->target_kind -
					 (uint32_t)SG_HOST_HOOK_TARGET_PLAYER);
		} else if (target->provenance ==
			SG_RUNE_MOVEMENT_HOOK_TARGET_PROVENANCE_STATIC_RESPONSE &&
			target->target_kind == SG_HOST_HOOK_TARGET_WORLD) {
			static_world++;
		}
	}
	CHECK(generic_kinds == UINT32_C(15));
	CHECK(static_world != 0U);
}

static int TestSpanWithin(uint32_t first, uint32_t count, uint32_t limit)
{
	return first <= limit && count <= limit - first;
}

/* Keep the movement owner directly consumable by the accepted compact model:
 * the model borrows the owner arrays and carries the same bound identity. */
static void CheckAcceptedModelShape(const movement_fixture_t *fixture,
	const sg_rune_compact_movement_fields_view_t *view)
{
	sg_rune_compact_model_t model;
	uint32_t capability_index;

	memset(&model, 0, sizeof(model));
	model.version = SG_RUNE_COMPACT_MODEL_VERSION;
	model.schema_tag = SG_RUNE_COMPACT_MODEL_SCHEMA_TAG;
	model.identity = view->identity;
	model.cells = fixture->geometry.cells;
	model.cell_count = fixture->geometry.cell_count;
	model.facets = fixture->geometry.facets;
	model.facet_count = fixture->geometry.facet_count;
	model.incidences = fixture->geometry.incidences;
	model.incidence_count = fixture->geometry.incidence_count;
	model.cell_incidences = fixture->geometry.cell_incidences;
	model.cell_incidence_count = fixture->geometry.cell_incidence_count;
	model.vertices = fixture->geometry.vertices;
	model.vertex_count = fixture->geometry.vertex_count;
	model.portals = fixture->geometry.portals;
	model.portal_count = fixture->geometry.portal_count;
	model.movement_capabilities = view->capabilities;
	model.movement_capability_count = view->capability_count;
	model.movement_states = view->states;
	model.movement_state_count = view->state_count;
	model.movement_fibers = view->fibers;
	model.movement_fiber_count = view->fiber_count;
	model.movement_hook_targets = view->hook_targets;
	model.movement_hook_target_count = view->hook_target_count;
	model.movement_fiber_function_refs = view->fiber_function_refs;
	model.movement_fiber_function_ref_count = view->fiber_function_ref_count;
	model.movement_angular_schedules = view->angular_schedules;
	model.movement_angular_schedule_count = view->angular_schedule_count;
	model.movement_pmove_abi = view->pmove_abi;
	model.movement_pmove_behavior_fingerprint =
		view->pmove_behavior_fingerprint;
	model.movement_host_level_generation = view->host_level_generation;
	model.movement_physics_abi_id = view->physics_abi_id;
	model.movement_collision_law_id = view->collision_law_id;
	model.movement_pmove_law_id = view->pmove_law_id;
	model.movement_gravity_law_id = view->gravity_law_id;
	model.movement_hook_law_id = view->hook_law_id;
	model.movement_mechanism_law_id = view->mechanism_law_id;
	model.analytic = &view->analytic;
	model.static_data = &fixture->static_data;

	CHECK(model.movement_capabilities == view->capabilities);
	CHECK(model.movement_capability_count == view->capability_count);
	CHECK(model.movement_states == view->states);
	CHECK(model.movement_fibers == view->fibers);
	CHECK(model.movement_hook_targets == view->hook_targets);
	CHECK(model.movement_fiber_function_refs == view->fiber_function_refs);
	CHECK(model.analytic == &view->analytic);
	CHECK(memcmp(&model.identity, &view->identity,
		sizeof(model.identity)) == 0);
	for (capability_index = 0U;
		capability_index < model.movement_capability_count; capability_index++) {
		const sg_rune_movement_capability_t *capability =
			&model.movement_capabilities[capability_index];
		uint32_t fiber_offset;

		CHECK(capability->cell.value < model.cell_count);
		CHECK(capability->boundary_portal.value == SG_RUNE_COMPACT_INDEX_NONE ||
			capability->boundary_portal.value < model.portal_count);
		CHECK((uint32_t)capability->kind <
			(uint32_t)SG_RUNE_MOVEMENT_CAPABILITY_KIND_COUNT);
		CHECK(capability->fibers.count != 0U && TestSpanWithin(
			capability->fibers.first, capability->fibers.count,
			model.movement_fiber_count));
		for (fiber_offset = 0U; fiber_offset < capability->fibers.count;
			fiber_offset++) {
			const sg_rune_compact_movement_fiber_t *fiber =
				&model.movement_fibers[capability->fibers.first + fiber_offset];

			CHECK(fiber->capability.value == capability_index);
			CHECK(fiber->source_state.value < model.movement_state_count);
			CHECK(fiber->destination_state.value < model.movement_state_count);
			CHECK(fiber->functions.count != 0U && TestSpanWithin(
				fiber->functions.first, fiber->functions.count,
				model.movement_fiber_function_ref_count));
			if (capability->kind ==
				SG_RUNE_MOVEMENT_CAPABILITY_CONTROLLER_ACTION) {
				CHECK(fiber->controller_action_controller.value !=
					SG_RUNE_COMPACT_INDEX_NONE);
				CHECK(fiber->controller_action_target.value !=
					SG_RUNE_COMPACT_INDEX_NONE);
			} else {
				CHECK(fiber->controller_action_controller.value ==
					SG_RUNE_COMPACT_INDEX_NONE);
				CHECK(fiber->controller_action_target.value ==
					SG_RUNE_COMPACT_INDEX_NONE);
			}
		}
	}
}

static void CheckGravity(movement_fixture_t *fixture)
{
	sg_rune_compact_movement_fields_t *fields = NULL;
	sg_rune_compact_movement_fields_view_t view;
	sg_rune_compact_movement_fields_error_t error;
	uint32_t first_gravity_bits;

	CHECK(SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	if (fields == NULL) {
		fixture->host.static_identity.physics.gravity = 800.0f;
		SyncFixtureIdentity(fixture);
		return;
	}
	if (!SG_RuneCompactMovementFieldsRead(fields, &view)) {
		CHECK(0);
		SG_RuneCompactMovementFieldsDestroy(fields);
		fixture->host.static_identity.physics.gravity = 800.0f;
		SyncFixtureIdentity(fixture);
		return;
	}
	first_gravity_bits = view.identity.physics.gravity_bits;
	SG_RuneCompactMovementFieldsDestroy(fields);
	fixture->host.static_identity.physics.gravity = 100.0f;
	SyncFixtureIdentity(fixture);
	fields = NULL;
	CHECK(SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	if (fields == NULL) {
		fixture->host.static_identity.physics.gravity = 800.0f;
		SyncFixtureIdentity(fixture);
		return;
	}
	if (!SG_RuneCompactMovementFieldsRead(fields, &view)) {
		CHECK(0);
		SG_RuneCompactMovementFieldsDestroy(fields);
		fixture->host.static_identity.physics.gravity = 800.0f;
		SyncFixtureIdentity(fixture);
		return;
	}
	CHECK(view.identity.physics.gravity_bits != first_gravity_bits &&
		view.identity.physics.gravity_bits == Bits(100.0f));
	SG_RuneCompactMovementFieldsDestroy(fields);
	fixture->host.static_identity.physics.gravity = 800.0f;
	SyncFixtureIdentity(fixture);
}

static void CheckEvaluation(const movement_fixture_t *fixture,
	const sg_rune_compact_movement_fields_view_t *view)
{
	const sg_rune_movement_capability_t *hook = FindField(view, 0U,
		SG_RUNE_COMPACT_INDEX_NONE, SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BODY);
	const sg_rune_analytic_function_t *hook_position = hook == NULL ? NULL :
		FindOutput(view, hook, SG_RUNE_ANALYTIC_OUTPUT_POSITION_X);
	sg_rune_compact_eval_input_t inputs[SG_RUNE_ANALYTIC_INPUT_DIMENSION_COUNT];
	sg_rune_compact_eval_query_t query;
	sg_rune_compact_eval_result_t result;

	SetEvaluationInputs(inputs);
	query.inputs = inputs;
	query.input_count = (uint32_t)SG_RUNE_ANALYTIC_INPUT_DIMENSION_COUNT;
	inputs[SG_RUNE_ANALYTIC_INPUT_WORLD_X].value = 10.0f;
	inputs[SG_RUNE_ANALYTIC_INPUT_VELOCITY_X].value = 2.0f;
	inputs[SG_RUNE_ANALYTIC_INPUT_DIRECTION_X].value = 0.5f;
	inputs[SG_RUNE_ANALYTIC_INPUT_TIME_SECONDS].value = 0.25f;
	query.function.value = FunctionIndex(&view->analytic, hook_position);
	CHECK(query.function.value != UINT32_MAX &&
		SG_RuneCompactEval(&view->analytic, &query, &result) ==
		SG_RUNE_COMPACT_EVAL_OK && SameFloat(result.value, 10.125f));
	CHECK(fixture->host.static_identity.physics.gravity > 0.0f);
}

static void CheckDirectionalFields(movement_fixture_t *fixture)
{
	sg_rune_compact_movement_fields_t *fields = NULL;
	sg_rune_compact_movement_fields_view_t view;
	sg_rune_compact_movement_fields_error_t error;
	sg_rune_compact_contents_mask_t saved_contents = fixture->cells[1].contents;

	fixture->cells[1].contents |= SG_RUNE_COMPACT_CONTENTS_CURRENT_90;
	CHECK(SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	if (fields == NULL) {
		fixture->cells[1].contents = saved_contents;
		return;
	}
	CHECK(SG_RuneCompactMovementFieldsRead(fields, &view));
	CHECK(CountFamily(&view, SG_RUNE_MOVEMENT_CAPABILITY_SWIM) == 0U);
	CHECK(CountFamily(&view, SG_RUNE_MOVEMENT_CAPABILITY_WALK) == 2U);
	SG_RuneCompactMovementFieldsDestroy(fields);
	fixture->cells[1].contents = saved_contents;
}

static void ConfigureReplayPortalFixture(movement_fixture_t *fixture)
{
	uint32_t index;

	fixture->facets[0].plane.distance_bits = Bits(16.0f);
	fixture->source_surfaces[0].plane = fixture->facets[0].plane;
	fixture->response_patches[0].plane = fixture->facets[0].plane;
	fixture->hook_surfaces[0].distance = 16.0f;
	fixture->hook_surfaces[0].bounds.mins.value[0] = 16.0f;
	fixture->hook_surfaces[0].bounds.maxs.value[0] = 17.0f;
	fixture->hook_surfaces[0].bounds.mins.value[1] = 2.0f;
	fixture->hook_surfaces[0].bounds.maxs.value[1] = 14.0f;
	fixture->hook_surfaces[0].bounds.mins.value[2] = 2.0f;
	fixture->hook_surfaces[0].bounds.maxs.value[2] = 14.0f;
	fixture->geometry_vertices[0].value[0] = 128;
	fixture->geometry_vertices[0].value[1] = 16;
	fixture->geometry_vertices[0].value[2] = 16;
	fixture->geometry_vertices[1].value[0] = 128;
	fixture->geometry_vertices[1].value[1] = 112;
	fixture->geometry_vertices[1].value[2] = 16;
	fixture->geometry_vertices[2].value[0] = 128;
	fixture->geometry_vertices[2].value[1] = 16;
	fixture->geometry_vertices[2].value[2] = 112;
	for (index = 0U; index < CELL_COUNT; index++) {
		fixture->regions[index].bounds.mins.value[0] = (float)(index * 16U);
		fixture->regions[index].bounds.mins.value[1] = 0.0f;
		fixture->regions[index].bounds.mins.value[2] = 0.0f;
		fixture->regions[index].bounds.maxs.value[0] =
			(float)((index + 1U) * 16U);
		fixture->regions[index].bounds.maxs.value[1] = 16.0f;
		fixture->regions[index].bounds.maxs.value[2] = 16.0f;
		SetFloatVector(fixture->regions[index].interior_witness.value,
			(float)(index * 16U) + 4.0f, 4.0f, 4.0f);
	}
	fixture->cells[0].valid_stances = SG_RUNE_STANCE_VALID_STANDING;
	fixture->cells[1].valid_stances = SG_RUNE_STANCE_VALID_STANDING;
	fixture->portals[0].valid_stances = SG_RUNE_STANCE_VALID_STANDING;
	fixture->response_fragments[0].valid_stances =
		SG_RUNE_STANCE_VALID_STANDING;
	fixture->response_patches[0].valid_stances =
		SG_RUNE_STANCE_VALID_STANDING;
	fixture->response_pairs[0].source_valid_stances =
		SG_RUNE_STANCE_VALID_STANDING;
	fixture->response_pairs[0].target_valid_stances =
		SG_RUNE_STANCE_VALID_STANDING;
	fixture->annotations[0].hookable_stances =
		SG_RUNE_STANCE_VALID_STANDING;
	fixture->builder_owner.replay_enabled = 1;
	SetFloatVector(fixture->builder_owner.replay_origin, 20.0f, 4.0f, 6.0f);
	SetFloatVector(fixture->builder_owner.replay_velocity, 120.0f, 0.0f, 0.0f);
	fixture->builder_owner.replay_support_normal_z = 1.0f;
	fixture->builder_owner.replay_grounded = 1;
	fixture->builder_owner.replay_water_level = 0;
	fixture->builder_owner.replay_water_type = 0;
}

/* Some places are reachable only crouched.  A portal that admits just the
 * crouching hull must still be crossed, as CROUCH, in both directions, and
 * must never be published as a walk. */
static void CheckCrouchOnlyPassage(void)
{
	movement_fixture_t fixture;
	sg_rune_compact_movement_fields_t *fields = NULL;
	sg_rune_compact_movement_fields_view_t view;
	sg_rune_compact_movement_fields_error_t error;

	InitFixture(&fixture);
	fixture.portals[0].valid_stances = SG_RUNE_STANCE_VALID_CROUCHING;
	CHECK(SG_RuneCompactMovementFieldsBuild(&fixture.input, &fields, &error));
	if (fields != NULL && SG_RuneCompactMovementFieldsRead(fields, &view)) {
		const sg_rune_movement_capability_t *forward = FindFieldForStance(
			&view, 0U, 0U, SG_RUNE_MOVEMENT_CAPABILITY_CROUCH,
			SG_RUNE_STANCE_VALID_CROUCHING);
		const sg_rune_movement_capability_t *back = FindFieldForStance(
			&view, 1U, 0U, SG_RUNE_MOVEMENT_CAPABILITY_CROUCH,
			SG_RUNE_STANCE_VALID_CROUCHING);

		CHECK(CountFamily(&view, SG_RUNE_MOVEMENT_CAPABILITY_WALK) == 0U);
		CHECK(CountFamily(&view, SG_RUNE_MOVEMENT_CAPABILITY_CROUCH) == 2U);
		CHECK(forward != NULL && forward->destination_stances ==
			SG_RUNE_STANCE_VALID_CROUCHING);
		CHECK(back != NULL && back->destination_stances ==
			SG_RUNE_STANCE_VALID_CROUCHING);
	}
	SG_RuneCompactMovementFieldsDestroy(fields);
}

/* Ordinary player movement is published across every portal the hull can
 * cross.  With one vertical portal between level supported cells the only
 * admissible families are walking and crouching, in both directions. */
static void CheckPmovePublicationEnabled(void)
{
	movement_fixture_t fixture;
	sg_rune_compact_movement_fields_t *fields = NULL;
	sg_rune_compact_movement_fields_view_t view;
	sg_rune_compact_movement_fields_error_t error;

	InitFixture(&fixture);
	ConfigureReplayPortalFixture(&fixture);
	CHECK(SG_RuneCompactMovementFieldsBuild(&fixture.input, &fields, &error));
	if (fields != NULL && SG_RuneCompactMovementFieldsRead(fields, &view)) {
		/* The replay fixture admits only the standing hull on both cells and
		 * the portal, so the crossing walks and never crouches. */
		CHECK(CountFamily(&view, SG_RUNE_MOVEMENT_CAPABILITY_WALK) == 2U);
		CHECK(CountFamily(&view, SG_RUNE_MOVEMENT_CAPABILITY_CROUCH) == 0U);
		CHECK(CountFamily(&view, SG_RUNE_MOVEMENT_CAPABILITY_RAMP) == 0U);
		CHECK(CountFamily(&view, SG_RUNE_MOVEMENT_CAPABILITY_JUMP) == 0U);
		CHECK(CountFamily(&view, SG_RUNE_MOVEMENT_CAPABILITY_DROP) == 0U);
		CHECK(CountFamily(&view, SG_RUNE_MOVEMENT_CAPABILITY_SWIM) == 0U);
		CHECK(CountFamily(&view, SG_RUNE_MOVEMENT_CAPABILITY_AIR_CONTROL) == 0U);
	}
	SG_RuneCompactMovementFieldsDestroy(fields);
}
static void CheckHookLadder(movement_fixture_t *fixture)
{
	static const float lengths[] = { 0.0f, 0.5f, 1.0f, 10.0f, 10.99f,
		11.0f, 20.99f, 21.0f, 40.99f, 41.0f, 80.99f, 81.0f, 100.99f,
		101.0f, 120.99f, 121.0f, 130.0f };
	static const float expected[] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f,
		11.0f, 20.0f, 42.0f, 80.0f, 123.0f, 240.0f, 324.0f, 400.0f,
		505.0f, 600.0f, 800.0f, 800.0f };
	sg_rune_compact_movement_fields_t *fields = NULL;
	sg_rune_compact_movement_fields_view_t view;
	sg_rune_compact_movement_fields_error_t error;
	const sg_rune_movement_capability_t *hook;
	const sg_rune_analytic_function_t *velocity;
	sg_rune_compact_eval_input_t inputs[
		SG_RUNE_ANALYTIC_INPUT_DIMENSION_COUNT];
	sg_rune_compact_eval_query_t query;
	sg_rune_compact_eval_result_t result;
	uint32_t index;

	CHECK(SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	if (fields == NULL)
		return;
	CHECK(SG_RuneCompactMovementFieldsRead(fields, &view));
	hook = FindField(&view, 0U, SG_RUNE_COMPACT_INDEX_NONE,
		SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BODY);
	velocity = hook == NULL ? NULL : FindOutput(&view, hook,
		SG_RUNE_ANALYTIC_OUTPUT_VELOCITY_X);
	SetEvaluationInputs(inputs);
	inputs[SG_RUNE_ANALYTIC_INPUT_VELOCITY_X].value = 37.0f;
	inputs[SG_RUNE_ANALYTIC_INPUT_DIRECTION_X].value = 1.0f;
	query.inputs = inputs;
	query.input_count = (uint32_t)SG_RUNE_ANALYTIC_INPUT_DIMENSION_COUNT;
	for (index = 0U; index < (uint32_t)(sizeof(lengths) / sizeof(lengths[0]));
		index++) {
		inputs[SG_RUNE_ANALYTIC_INPUT_HOOK_LENGTH].value = lengths[index];
		query.function.value = FunctionIndex(&view.analytic, velocity);
		CHECK(query.function.value != UINT32_MAX &&
			SG_RuneCompactEval(&view.analytic, &query, &result) ==
			SG_RUNE_COMPACT_EVAL_OK && SameFloat(result.value, expected[index]));
	}
	SG_RuneCompactMovementFieldsDestroy(fields);
}

static void CheckIdentityBinding(movement_fixture_t *fixture)
{
	sg_rune_compact_movement_fields_t *fields = NULL;
	sg_rune_compact_movement_fields_view_t view;
	sg_rune_compact_identity_t bound_identity;
	sg_rune_compact_movement_fields_error_t error;
	sg_rune_compact_identity_t saved_identity = fixture->geometry.identity;
	uint8_t saved_digest_byte =
		fixture->host.static_identity.bsp_identity.bytes[0];
	uint32_t saved_partition_leaf = fixture->partitions[0].bsp_leaf;
	const sg_rune_compact_static_materializer_t *saved_owner =
		fixture->input.static_owner;
	sg_rune_compact_identity_t saved_owner_identity =
		fixture->static_owner.identity;
	sg_rune_compact_identity_t saved_response_identity =
		fixture->response_partition.identity;
	sg_rune_compact_identity_t saved_mechanisms_identity =
		fixture->mechanisms_view.identity;
	uint64_t saved_fragment_partition_id =
		fixture->response_fragments[0].static_partition_id;
	uint64_t saved_patch_partition_id =
		fixture->response_patches[0].static_partition_id;

	CHECK(SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	CHECK(fields != NULL && SG_RuneCompactMovementFieldsReadBound(fields,
		&bound_identity, &view));
	if (fields != NULL) {
		CHECK(memcmp(&bound_identity, &fixture->geometry.identity,
			sizeof(bound_identity)) == 0);
		CHECK(memcmp(&view.identity, &fixture->geometry.identity,
			sizeof(view.identity)) == 0);
		SG_RuneCompactMovementFieldsDestroy(fields);
	}
	memset(&fixture->geometry.identity, 0, sizeof(fixture->geometry.identity));
	fields = (sg_rune_compact_movement_fields_t *)(uintptr_t)0x2468U;
	CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	CHECK(fields == (sg_rune_compact_movement_fields_t *)(uintptr_t)0x2468U);
	fixture->geometry.identity = saved_identity;
	fixture->host.static_identity.bsp_identity.bytes[0] ^= 1U;
	fields = (sg_rune_compact_movement_fields_t *)(uintptr_t)0x9876U;
	CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	CHECK(fields == (sg_rune_compact_movement_fields_t *)(uintptr_t)0x9876U);
	fixture->host.static_identity.bsp_identity.bytes[0] = saved_digest_byte;
	fixture->partitions[0].bsp_leaf =
		fixture->geometry.identity.source_counts.leaf_count;
	fields = (sg_rune_compact_movement_fields_t *)(uintptr_t)0x4321U;
	CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	CHECK(fields == (sg_rune_compact_movement_fields_t *)(uintptr_t)0x4321U &&
		error.code == SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_VISIBILITY);
	fixture->partitions[0].bsp_leaf = saved_partition_leaf;
	fixture->response_fragments[0].static_partition_id ^= UINT64_C(1);
	fields = (sg_rune_compact_movement_fields_t *)(uintptr_t)0x4343U;
	CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	CHECK(fields == (sg_rune_compact_movement_fields_t *)(uintptr_t)0x4343U &&
		error.code == SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_VISIBILITY);
	fixture->response_fragments[0].static_partition_id =
		saved_fragment_partition_id;
	fixture->response_patches[0].static_partition_id ^= UINT64_C(1);
	fields = (sg_rune_compact_movement_fields_t *)(uintptr_t)0x4344U;
	CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	CHECK(fields == (sg_rune_compact_movement_fields_t *)(uintptr_t)0x4344U &&
		error.code == SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_VISIBILITY);
	fixture->response_patches[0].static_partition_id = saved_patch_partition_id;
	fixture->input.static_owner = NULL;
	fields = (sg_rune_compact_movement_fields_t *)(uintptr_t)0x1111U;
	CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	CHECK(fields == (sg_rune_compact_movement_fields_t *)(uintptr_t)0x1111U);
	fixture->input.static_owner = saved_owner;
	fixture->static_owner.identity.construction_id ^= UINT64_C(1);
	fields = (sg_rune_compact_movement_fields_t *)(uintptr_t)0x2222U;
	CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	CHECK(fields == (sg_rune_compact_movement_fields_t *)(uintptr_t)0x2222U);
	fixture->static_owner.identity = saved_owner_identity;
	fixture->response_partition.identity.construction_id ^= UINT64_C(1);
	fields = (sg_rune_compact_movement_fields_t *)(uintptr_t)0x2a2aU;
	CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	CHECK(fields == (sg_rune_compact_movement_fields_t *)(uintptr_t)0x2a2aU);
	fixture->response_partition.identity = saved_response_identity;
	fixture->mechanisms_view.identity.construction_id ^= UINT64_C(1);
	fields = (sg_rune_compact_movement_fields_t *)(uintptr_t)0x2b2bU;
	CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	CHECK(fields == (sg_rune_compact_movement_fields_t *)(uintptr_t)0x2b2bU);
	fixture->mechanisms_view.identity = saved_mechanisms_identity;
}

_Static_assert(sizeof(sg_rune_compact_static_transition_t) ==
	sizeof(sg_rune_compact_mechanism_transition_t),
	"static and authority transition records must share the wire shape");

static void SyncFixtureTransition(movement_fixture_t *fixture)
{
	memcpy(&fixture->static_transitions[0], &fixture->transitions[0],
		sizeof(fixture->static_transitions[0]));
	fixture->static_data.transitions = fixture->static_transitions;
	fixture->static_data.transition_count = 1U;
	fixture->authority_transition_static[0] = 0U;
	fixture->static_owner.authority_transition_count = 1U;
	fixture->mechanisms_view.transitions = fixture->transitions;
	fixture->mechanisms_view.transition_count = 1U;
	fixture->authorities[0].transitions.first = 0U;
	fixture->authorities[0].transitions.count = 1U;
	fixture->mechanisms[0].transitions.first = 0U;
	fixture->mechanisms[0].transitions.count = 1U;
	fixture->mechanisms[0].kind =
		(sg_rune_compact_mechanism_kind_t)fixture->authorities[0].kind;
	fixture->mechanisms[0].delay_ms = fixture->authorities[0].delay_ms;
	fixture->mechanisms[0].dwell_ms = fixture->authorities[0].dwell_ms;
	fixture->mechanisms[0].travel_ms = fixture->authorities[0].travel_ms;
	fixture->mechanisms[0].wait_ms = fixture->authorities[0].pause_ms;
	fixture->mechanisms[0].reset_ms = fixture->authorities[0].recovery_ms;
	fixture->mechanisms[0].initial_state =
		(sg_rune_compact_mechanism_state_t)fixture->authorities[0].initial_state;
	fixture->mechanisms[0].activated_state =
		(sg_rune_compact_mechanism_state_t)fixture->authorities[0].activated_state;
	fixture->mechanisms[0].reset_state =
		(sg_rune_compact_mechanism_state_t)fixture->authorities[0].reset_state;
	fixture->mechanisms[0].recovery = fixture->authorities[0].recovery_ms == 0U ?
		SG_RUNE_COMPACT_MECHANISM_RECOVERY_NONE :
		SG_RUNE_COMPACT_MECHANISM_RECOVERY_WAIT_FOR_RESET;
}

static void ConfigureController(movement_fixture_t *fixture,
	uint32_t activation_cell)
{
	const sg_rune_q8_bounds_t bounds = fixture->cells[activation_cell].bounds;
	sg_rune_q8_vec3_t witness;

	witness.value[0] = bounds.mins.value[0] + 32;
	witness.value[1] = bounds.mins.value[1] + 32;
	witness.value[2] = bounds.mins.value[2] + 32;
	memset(&fixture->controllers[0], 0, sizeof(fixture->controllers[0]));
	fixture->controllers[0].mechanism = 0U;
	fixture->controllers[0].controller.entity_ordinal = 0U;
	fixture->controllers[0].topology_edge = 0U;
	fixture->controllers[0].activation =
		SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_TOUCH;
	fixture->controllers[0].required_item = SG_BSP_ENTITY_STRING_NONE;
	fixture->controllers[0].spatiality =
		SG_RUNE_COMPACT_MECHANISM_CONTROLLER_PLAYER_SPATIAL;
	fixture->controllers[0].activation_cell.value = activation_cell;
	fixture->controllers[0].activation_witness = witness;
	fixture->controllers[0].activation_bounds = bounds;
	memset(&fixture->topology_edges[0], 0,
		sizeof(fixture->topology_edges[0]));
	fixture->topology_edges[0].source.entity_ordinal = 0U;
	fixture->topology_edges[0].destination.entity_ordinal = 0U;
	fixture->topology_edges[0].kind = SG_MECH_EDGE_TARGET;
	fixture->mechanisms_view.controllers = fixture->controllers;
	fixture->mechanisms_view.controller_count = 1U;
	fixture->mechanisms_view.topology_edges = fixture->topology_edges;
	fixture->mechanisms_view.topology_edge_count = 1U;
	fixture->authorities[0].controllers.first = 0U;
	fixture->authorities[0].controllers.count = 1U;
	fixture->authorities[0].topology.first = 0U;
	fixture->authorities[0].topology.count = 1U;
	memset(&fixture->static_controllers[0], 0,
		sizeof(fixture->static_controllers[0]));
	fixture->static_controllers[0].controller.entity_ordinal = 0U;
	fixture->static_controllers[0].topology_edge = 0U;
	fixture->static_controllers[0].spatiality =
		SG_RUNE_COMPACT_MECHANISM_CONTROLLER_PLAYER_SPATIAL;
	fixture->static_controllers[0].activation_cell.value = activation_cell;
	fixture->static_controllers[0].activation_witness = witness;
	fixture->static_controllers[0].activation_bounds = bounds;
	fixture->static_data.mechanism_controllers = fixture->static_controllers;
	fixture->static_data.mechanism_controller_count = 1U;
	fixture->mechanisms[0].controllers.first = 0U;
	fixture->mechanisms[0].controllers.count = 1U;
	memset(&fixture->static_edges[0], 0, sizeof(fixture->static_edges[0]));
	fixture->static_edges[0].source.entity_ordinal = 0U;
	fixture->static_edges[0].destination.entity_ordinal = 0U;
	fixture->static_edges[0].kind = SG_RUNE_COMPACT_MECHANISM_EDGE_TARGET;
	fixture->static_data.mechanism_edges = fixture->static_edges;
	fixture->static_data.mechanism_edge_count = 1U;
	fixture->mechanisms[0].topology.first = 0U;
	fixture->mechanisms[0].topology.count = 1U;
}

static void DisableControllers(movement_fixture_t *fixture)
{
	fixture->mechanisms_view.controllers = NULL;
	fixture->mechanisms_view.controller_count = 0U;
	fixture->mechanisms_view.topology_edges = NULL;
	fixture->mechanisms_view.topology_edge_count = 0U;
	fixture->authorities[0].controllers.first = 0U;
	fixture->authorities[0].controllers.count = 0U;
	fixture->authorities[0].topology.first = 0U;
	fixture->authorities[0].topology.count = 0U;
	fixture->static_data.mechanism_controllers = NULL;
	fixture->static_data.mechanism_controller_count = 0U;
	fixture->static_data.mechanism_edges = NULL;
	fixture->static_data.mechanism_edge_count = 0U;
	fixture->mechanisms[0].controllers.first = 0U;
	fixture->mechanisms[0].controllers.count = 0U;
	fixture->mechanisms[0].topology.first = 0U;
	fixture->mechanisms[0].topology.count = 0U;
}

static void ConfigureAngularPortalFixture(movement_fixture_t *fixture,
	sg_bsp_entity_angular_mover_kind_t angular_kind)
{
	fixture->entity_semantic[0].mechanism_kind = SG_RUNE_MECHANISM_ROTATOR;
	fixture->entity_semantic[0].angular_mover.kind = angular_kind;
	fixture->authorities[0].kind = SG_RUNE_COMPACT_MECHANISM_AUTHORITY_ROTATOR;
	fixture->authorities[0].initial_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE;
	fixture->authorities[0].activated_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture->authorities[0].reset_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE;
	fixture->authorities[0].travel_ms = 1500U;
	fixture->mechanisms[0].kind = SG_RUNE_COMPACT_MECHANISM_ROTATOR;
	fixture->mechanisms[0].flags =
		SG_RUNE_COMPACT_MECHANISM_MOVER_RELATIVE |
		SG_RUNE_COMPACT_MECHANISM_FINITE_ANGULAR_DOOR;
	fixture->mechanisms[0].initial_state =
		SG_RUNE_COMPACT_MECHANISM_STATE_INACTIVE;
	fixture->mechanisms[0].activated_state =
		SG_RUNE_COMPACT_MECHANISM_STATE_ACTIVE;
	fixture->mechanisms[0].reset_state =
		SG_RUNE_COMPACT_MECHANISM_STATE_INACTIVE;
	fixture->mechanisms[0].travel_ms = 1500U;
	fixture->mechanisms[0].transitions =
		(sg_rune_compact_mechanism_transition_span_t){ 0U, 1U };
	fixture->portal_mechanisms[0].kind =
		SG_RUNE_COMPACT_PORTAL_MECHANISM_BLOCKS;
	fixture->static_data.portal_mechanism_count = 1U;
	memset(&fixture->transitions[0], 0, sizeof(fixture->transitions[0]));
	fixture->transitions[0].mechanism = 0U;
	fixture->transitions[0].kind =
		SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE;
	fixture->transitions[0].entry_cell.value = 0U;
	fixture->transitions[0].exit_cell.value = 1U;
	fixture->transitions[0].source_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE;
	fixture->transitions[0].destination_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture->transitions[0].elapsed_ms = 1500U;
	fixture->transitions[0].value.portal_state.portal.value = 0U;
	fixture->transitions[0].value.portal_state.mover_model = 1U;
	fixture->transitions[0].value.portal_state.travel_ms = 1500U;
	fixture->transitions[0].value.portal_state.source_blocked = 1U;
	fixture->transitions[0].value.portal_state.destination_blocked = 0U;
	SyncFixtureTransition(fixture);
}

static void CheckAngularPortalAuthority(void)
{
	movement_fixture_t fixture;
	sg_rune_compact_movement_fields_t *fields = NULL;
	sg_rune_compact_movement_fields_error_t error;

	InitFixture(&fixture);
	ConfigureAngularPortalFixture(&fixture,
		SG_BSP_ENTITY_ANGULAR_MOVER_FINITE_DOOR);
	CHECK(SG_RuneCompactMovementFieldsBuild(&fixture.input, &fields, &error));
	SG_RuneCompactMovementFieldsDestroy(fields);

	InitFixture(&fixture);
	ConfigureAngularPortalFixture(&fixture,
		SG_BSP_ENTITY_ANGULAR_MOVER_CONTINUOUS_ROTATOR);
	fields = (sg_rune_compact_movement_fields_t *)(uintptr_t)0x4040U;
	CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture.input, &fields, &error));
	CHECK(fields == (sg_rune_compact_movement_fields_t *)(uintptr_t)0x4040U);
	CHECK(error.code == SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_STATIC_DATA);
}

static void CheckContinuousRotatorField(void)
{
	movement_fixture_t fixture;
	sg_rune_compact_movement_fields_t *fields = NULL;
	sg_rune_compact_movement_fields_view_t view;
	sg_rune_compact_movement_fields_error_t error;
	const sg_rune_movement_capability_t *field;
	sg_bsp_entity_continuous_angular_schedule_t *schedule;

	InitFixture(&fixture);
	fixture.entity_semantic[0].mechanism_kind = SG_RUNE_MECHANISM_ROTATOR;
	fixture.entity_semantic[0].angular_mover.kind =
		SG_BSP_ENTITY_ANGULAR_MOVER_CONTINUOUS_ROTATOR;
	schedule = &fixture.entity_semantic[0].angular_mover.schedule.continuous_rotator;
	schedule->axis.value[2] = 1.0f;
	schedule->angular_velocity.value[2] = 90.0f;
	schedule->frame_angular_delta.value[2] = 9.0f;
	schedule->speed = 90.0f;
	schedule->frame_ms = fixture.host.mechanism.frame_ms;
	fixture.authorities[0].kind = SG_RUNE_COMPACT_MECHANISM_AUTHORITY_ROTATOR;
	fixture.mechanisms[0].kind = SG_RUNE_COMPACT_MECHANISM_ROTATOR;
	fixture.mechanisms[0].flags = SG_RUNE_COMPACT_MECHANISM_MOVER_RELATIVE;

	CHECK(SG_RuneCompactMovementFieldsBuild(&fixture.input, &fields, &error));
	if (fields != NULL && SG_RuneCompactMovementFieldsRead(fields, &view)) {
		field = FindField(&view, 0U, SG_RUNE_COMPACT_INDEX_NONE,
			SG_RUNE_MOVEMENT_CAPABILITY_MOVER);
		/* Static angular schedule metadata is retained, but it cannot create a
		 * rider capability without an exact carried-support transition. */
		CHECK(field == NULL && view.angular_schedule_count == 1U);
	}
	SG_RuneCompactMovementFieldsDestroy(fields);
}

static void CheckButtonPortalAuthority(void)
{
	movement_fixture_t fixture;
	sg_rune_compact_movement_fields_t *fields = NULL;
	sg_rune_compact_movement_fields_view_t view;
	sg_rune_compact_movement_fields_error_t error;
	const sg_rune_movement_capability_t *field;

	InitFixture(&fixture);
	fixture.entity_semantic[0].mechanism_kind = SG_RUNE_MECHANISM_BUTTON;
	fixture.authorities[0].kind =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_BUTTON;
	fixture.authorities[0].initial_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE;
	fixture.authorities[0].activated_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture.authorities[0].reset_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE;
	fixture.authorities[0].travel_ms = 100U;
	fixture.mechanisms[0].kind = SG_RUNE_COMPACT_MECHANISM_BUTTON;
	fixture.mechanisms[0].flags = SG_RUNE_COMPACT_MECHANISM_MOVER_RELATIVE;
	fixture.mechanisms[0].initial_state =
		SG_RUNE_COMPACT_MECHANISM_STATE_INACTIVE;
	fixture.mechanisms[0].activated_state =
		SG_RUNE_COMPACT_MECHANISM_STATE_ACTIVE;
	fixture.mechanisms[0].reset_state =
		SG_RUNE_COMPACT_MECHANISM_STATE_INACTIVE;
	fixture.mechanisms[0].travel_ms = 100U;
	fixture.mechanisms[0].transitions =
		(sg_rune_compact_mechanism_transition_span_t){ 0U, 1U };
	fixture.portal_mechanisms[0].kind =
		SG_RUNE_COMPACT_PORTAL_MECHANISM_BLOCKS;
	fixture.static_data.portal_mechanism_count = 1U;
	memset(&fixture.transitions[0], 0, sizeof(fixture.transitions[0]));
	fixture.transitions[0].mechanism = 0U;
	fixture.transitions[0].kind =
		SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE;
	fixture.transitions[0].entry_cell.value = 0U;
	fixture.transitions[0].exit_cell.value = 1U;
	fixture.transitions[0].source_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE;
	fixture.transitions[0].destination_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture.transitions[0].elapsed_ms = 100U;
	fixture.transitions[0].value.portal_state.portal.value = 0U;
	fixture.transitions[0].value.portal_state.mover_model = 1U;
	fixture.transitions[0].value.portal_state.travel_ms = 100U;
	fixture.transitions[0].value.portal_state.source_blocked = 1U;
	fixture.transitions[0].value.portal_state.destination_blocked = 0U;
	SyncFixtureTransition(&fixture);
	ConfigureController(&fixture, 1U);
	CHECK(SG_RuneCompactMovementFieldsBuild(&fixture.input, &fields, &error));
	if (fields != NULL) {
		CHECK(SG_RuneCompactMovementFieldsRead(fields, &view));
		field = FindField(&view, 1U, SG_RUNE_COMPACT_INDEX_NONE,
			SG_RUNE_MOVEMENT_CAPABILITY_CONTROLLER_ACTION);
		CHECK(field != NULL && field->fibers.count == 1U &&
			view.fibers[field->fibers.first].mechanism_transition.value == 0U &&
			view.fibers[field->fibers.first].controller_action_controller.value ==
				0U &&
			view.fibers[field->fibers.first].controller_action_target.value == 0U);
		CHECK(FindField(&view, 0U, 0U,
			SG_RUNE_MOVEMENT_CAPABILITY_CONTROLLER_ACTION) == NULL);
	}
	SG_RuneCompactMovementFieldsDestroy(fields);
	fields = NULL;
	fixture.controllers[0].spatiality =
		SG_RUNE_COMPACT_MECHANISM_CONTROLLER_NONSPATIAL;
	fixture.controllers[0].activation_cell.value = SG_RUNE_COMPACT_INDEX_NONE;
	memset(&fixture.controllers[0].activation_witness, 0,
		sizeof(fixture.controllers[0].activation_witness));
	memset(&fixture.controllers[0].activation_bounds, 0,
		sizeof(fixture.controllers[0].activation_bounds));
	fixture.static_controllers[0].spatiality =
		SG_RUNE_COMPACT_MECHANISM_CONTROLLER_NONSPATIAL;
	fixture.static_controllers[0].activation_cell.value =
		SG_RUNE_COMPACT_INDEX_NONE;
	memset(&fixture.static_controllers[0].activation_witness, 0,
		sizeof(fixture.static_controllers[0].activation_witness));
	memset(&fixture.static_controllers[0].activation_bounds, 0,
		sizeof(fixture.static_controllers[0].activation_bounds));
	CHECK(SG_RuneCompactMovementFieldsBuild(&fixture.input, &fields, &error));
	if (fields != NULL) {
		CHECK(SG_RuneCompactMovementFieldsRead(fields, &view));
		CHECK(FindField(&view, 1U, SG_RUNE_COMPACT_INDEX_NONE,
			SG_RUNE_MOVEMENT_CAPABILITY_CONTROLLER_ACTION) == NULL);
	}
	SG_RuneCompactMovementFieldsDestroy(fields);

	fixture.portal_mechanisms[0].kind =
		SG_RUNE_COMPACT_PORTAL_MECHANISM_MOVES;
	fields = NULL;
	CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture.input, &fields, &error));
	CHECK(error.code == SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_STATIC_DATA);
}

static void CheckMultiRootPortalAuthority(void)
{
	movement_fixture_t fixture;
	sg_rune_compact_movement_fields_t *fields = NULL;
	sg_rune_compact_movement_fields_view_t view;
	sg_rune_compact_movement_fields_error_t error;
	uint32_t matching_fields;
	uint32_t index;

	InitFixture(&fixture);
	fixture.geometry.identity.source_counts.entity_count = 2U;
	fixture.entity_semantics.entity_count = 2U;
	fixture.entity_semantic[0].mechanism_kind = SG_RUNE_MECHANISM_DOOR;
	fixture.entity_semantic[1] = fixture.entity_semantic[0];
	fixture.entity_semantic[1].canonical_ordinal = 1U;
	fixture.authorities[0].kind = SG_RUNE_COMPACT_MECHANISM_AUTHORITY_DOOR;
	fixture.authorities[0].initial_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE;
	fixture.authorities[0].activated_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture.authorities[0].reset_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE;
	fixture.authorities[0].travel_ms = 100U;
	fixture.authorities[0].transitions =
		(sg_rune_compact_mechanism_span_t){ 0U, 1U };
	fixture.authorities[1] = fixture.authorities[0];
	fixture.authorities[1].source.entity_ordinal = 1U;
	fixture.authorities[1].transitions =
		(sg_rune_compact_mechanism_span_t){ 1U, 1U };
	fixture.mechanisms_view.mechanism_count = 2U;
	fixture.mechanisms_view.transitions = fixture.transitions;
	fixture.mechanisms_view.transition_count = 2U;
	fixture.mechanisms[0].kind = SG_RUNE_COMPACT_MECHANISM_DOOR;
	fixture.mechanisms[0].flags = SG_RUNE_COMPACT_MECHANISM_MOVER_RELATIVE;
	fixture.mechanisms[0].initial_state = SG_RUNE_COMPACT_MECHANISM_STATE_INACTIVE;
	fixture.mechanisms[0].activated_state = SG_RUNE_COMPACT_MECHANISM_STATE_ACTIVE;
	fixture.mechanisms[0].reset_state = SG_RUNE_COMPACT_MECHANISM_STATE_INACTIVE;
	fixture.mechanisms[0].travel_ms = 100U;
	fixture.mechanisms[0].transitions =
		(sg_rune_compact_mechanism_transition_span_t){ 0U, 1U };
	fixture.mechanisms[1] = fixture.mechanisms[0];
	fixture.mechanisms[1].source.entity_ordinal = 1U;
	fixture.mechanisms[1].transitions =
		(sg_rune_compact_mechanism_transition_span_t){ 1U, 1U };
	fixture.static_data.mechanism_count = 2U;
	memset(&fixture.transitions[0], 0, sizeof(fixture.transitions));
	fixture.transitions[0].kind =
		SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE;
	fixture.transitions[0].entry_cell.value = 0U;
	fixture.transitions[0].exit_cell.value = 1U;
	fixture.transitions[0].source_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE;
	fixture.transitions[0].destination_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture.transitions[0].elapsed_ms = 100U;
	fixture.transitions[0].value.portal_state.portal.value = 0U;
	fixture.transitions[0].value.portal_state.mover_model = 1U;
	fixture.transitions[0].value.portal_state.travel_ms = 100U;
	fixture.transitions[0].value.portal_state.source_blocked = 1U;
	fixture.transitions[0].value.portal_state.destination_blocked = 0U;
	fixture.transitions[1] = fixture.transitions[0];
	fixture.transitions[1].mechanism = 1U;
	memcpy(fixture.static_transitions, fixture.transitions,
		sizeof(fixture.static_transitions));
	fixture.static_data.transitions = fixture.static_transitions;
	fixture.static_data.transition_count = 2U;
	fixture.authority_transition_static[0] = 0U;
	fixture.authority_transition_static[1] = 1U;
	fixture.static_owner.authority_transition_count = 2U;
	fixture.portal_mechanisms[0].portal.value = 0U;
	fixture.portal_mechanisms[0].mechanism.value = 0U;
	fixture.portal_mechanisms[0].kind =
		SG_RUNE_COMPACT_PORTAL_MECHANISM_BLOCKS;
	fixture.static_data.portal_mechanism_count = 1U;
	fixture.portal_mechanisms[1] = fixture.portal_mechanisms[0];
	fixture.portal_mechanisms[1].mechanism.value = 1U;
	fixture.static_data.portal_mechanism_count = 2U;
	SyncFixtureIdentity(&fixture);
	CHECK(SG_RuneCompactMovementFieldsBuild(&fixture.input, &fields, &error));
	matching_fields = 0U;
	if (fields != NULL && SG_RuneCompactMovementFieldsRead(fields, &view))
		for (index = 0U; index < view.capability_count; index++)
			if (view.capabilities[index].cell.value == 0U &&
				view.capabilities[index].boundary_portal.value == 0U &&
				view.capabilities[index].kind ==
					SG_RUNE_MOVEMENT_CAPABILITY_CONTROLLER_ACTION &&
				view.fibers[view.capabilities[index].fibers.first].
					mechanism_transition.value != SG_RUNE_COMPACT_INDEX_NONE)
				matching_fields++;
	CHECK(fields != NULL && matching_fields == 0U &&
		SG_RuneCompactMovementFieldsTestPortalMergeSteps() == 2U);
	SG_RuneCompactMovementFieldsDestroy(fields);
	fields = NULL;
	fixture.static_data.portal_mechanism_count = 1U;
	CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture.input, &fields, &error));
	CHECK(error.code == SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_STATIC_DATA);
	fixture.static_data.portal_mechanism_count = 2U;
	fixture.portal_mechanisms[0].mechanism.value = 1U;
	fixture.portal_mechanisms[1].mechanism.value = 0U;
	CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture.input, &fields, &error));
	CHECK(error.code == SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_STATIC_DATA);
}

static void CheckCanonicalStaticTransitionOrder(void)
{
	movement_fixture_t fixture;
	sg_rune_compact_movement_fields_t *fields = NULL;
	sg_rune_compact_movement_fields_view_t view;
	sg_rune_compact_movement_fields_error_t error;
	uint32_t index;
	uint32_t transition_fields = 0U;

	InitFixture(&fixture);
	fixture.geometry.identity.source_counts.entity_count = 3U;
	fixture.authorities[0].kind =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TELEPORT;
	fixture.authorities[0].transitions =
		(sg_rune_compact_mechanism_span_t){ 0U, 2U };
	fixture.mechanisms_view.transitions = fixture.transitions;
	fixture.mechanisms_view.transition_count = 2U;
	fixture.mechanisms[0].kind = SG_RUNE_COMPACT_MECHANISM_TELEPORT;
	fixture.mechanisms[0].initial_state = SG_RUNE_COMPACT_MECHANISM_STATE_ACTIVE;
	fixture.mechanisms[0].activated_state =
		SG_RUNE_COMPACT_MECHANISM_STATE_ACTIVE;
	fixture.mechanisms[0].transitions =
		(sg_rune_compact_mechanism_transition_span_t){ 0U, 2U };
	memset(fixture.transitions, 0, sizeof(fixture.transitions));
	for (index = 0U; index < 2U; index++) {
		sg_rune_compact_mechanism_transition_t *transition =
			&fixture.transitions[index];

		transition->kind = SG_RUNE_COMPACT_MECHANISM_TRANSITION_TELEPORT;
		transition->entry_cell.value = 0U;
		transition->exit_cell.value = 1U;
		transition->source_state =
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
		transition->destination_state =
			SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
		transition->value.teleport.approach_witness.value[0] = 32;
		transition->value.teleport.approach_witness.value[1] = 32;
		transition->value.teleport.approach_witness.value[2] = 32;
		transition->value.teleport.entry_witness =
			transition->value.teleport.approach_witness;
		transition->value.teleport.exit_witness.value[0] = 160;
		transition->value.teleport.exit_witness.value[1] = 32;
		transition->value.teleport.exit_witness.value[2] = 32;
	}
	/* Authority deliberately reports the fanout in reverse canonical order. */
	fixture.transitions[0].value.teleport.destination.entity_ordinal = 2U;
	fixture.transitions[0].value.teleport.fanout_ordinal = 1U;
	fixture.transitions[0].value.teleport.exit_witness.value[0] = 168;
	fixture.transitions[1].value.teleport.destination.entity_ordinal = 1U;
	fixture.transitions[1].value.teleport.fanout_ordinal = 0U;
	memcpy(&fixture.static_transitions[0], &fixture.transitions[1],
		sizeof(fixture.static_transitions[0]));
	memcpy(&fixture.static_transitions[1], &fixture.transitions[0],
		sizeof(fixture.static_transitions[1]));
	/* Canonical static fanout/reordering has deliberately assigned both
	 * transitions to a different static mechanism.  State still belongs to
	 * the one authority mechanism that produced both transitions. */
	fixture.static_data.mechanism_count = 2U;
	fixture.mechanisms[1] = fixture.mechanisms[0];
	fixture.mechanisms[0].transitions =
		(sg_rune_compact_mechanism_transition_span_t){ 0U, 0U };
	fixture.mechanisms[1].transitions =
		(sg_rune_compact_mechanism_transition_span_t){ 0U, 2U };
	fixture.static_transitions[0].mechanism.value = 1U;
	fixture.static_transitions[1].mechanism.value = 1U;
	fixture.static_data.transitions = fixture.static_transitions;
	fixture.static_data.transition_count = 2U;
	fixture.authority_transition_static[0] = 1U;
	fixture.authority_transition_static[1] = 0U;
	fixture.static_owner.authority_transition_count = 2U;
	fixture.static_mechanism_authority[0] = 0U;
	fixture.static_mechanism_authority[1] = 0U;
	fixture.static_owner.static_mechanism_count = 2U;
	SyncFixtureIdentity(&fixture);

	CHECK(SG_RuneCompactMovementFieldsBuild(&fixture.input, &fields, &error));
	if (fields != NULL && SG_RuneCompactMovementFieldsRead(fields, &view)) {
		for (index = 0U; index < view.capability_count; index++)
			if (view.capabilities[index].cell.value == 0U &&
				view.capabilities[index].boundary_portal.value ==
					SG_RUNE_COMPACT_INDEX_NONE &&
				view.capabilities[index].kind ==
					SG_RUNE_MOVEMENT_CAPABILITY_MOVER &&
				view.fibers[view.capabilities[index].fibers.first].
					mechanism_transition.value !=
					SG_RUNE_COMPACT_INDEX_NONE) {
				const sg_rune_compact_movement_fiber_t *fiber =
					&view.fibers[view.capabilities[index].fibers.first];
				const sg_rune_analytic_function_t *position = FindOutput(&view,
					&view.capabilities[index],
					SG_RUNE_ANALYTIC_OUTPUT_POSITION_X);
				const uint32_t expected_transition = transition_fields % 2U;

				CHECK(fiber->mechanism_transition.value == expected_transition);
				CHECK(view.states[fiber->source_state.value].mover_mechanism ==
					SG_RUNE_COMPACT_INDEX_NONE);
				CHECK(view.states[fiber->destination_state.value].mover_mechanism ==
					SG_RUNE_COMPACT_INDEX_NONE);
				CHECK((view.states[fiber->source_state.value].flags &
					SG_RUNE_MOVEMENT_STATE_MOVER_RELATIVE) == 0U);
				CHECK((view.states[fiber->destination_state.value].flags &
					SG_RUNE_MOVEMENT_STATE_MOVER_RELATIVE) == 0U);
				CHECK(position != NULL && position->form ==
					SG_RUNE_COMPACT_ANALYTIC_BALLISTIC &&
					SameFloat(ScalarValue(view.analytic.ballistics[
						position->definition].initial),
						expected_transition == 0U ? 21.0f : 20.0f));
				transition_fields++;
			}
		CHECK(transition_fields == 4U);
	}
	SG_RuneCompactMovementFieldsDestroy(fields);
	fields = NULL;
	/* The authority-to-static join is owner-issued provenance, not a raw byte
	 * search.  A bad owner mapping is rejected even when both independently
	 * valid transition records remain in range. */
	fixture.authority_transition_static[0] =
		fixture.static_data.transition_count;
	CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture.input, &fields, &error));
	CHECK(fields == NULL && error.code ==
		SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_STATIC_DATA);
}

static void CheckMechanismDiscontinuities(movement_fixture_t *fixture)
{
	sg_rune_compact_mechanism_authority_t saved_authority =
		fixture->authorities[0];
	sg_rune_compact_mechanism_t saved_static_mechanism =
		fixture->mechanisms[0];
	sg_rune_compact_portal_mechanism_t saved_portal_mechanism =
		fixture->portal_mechanisms[0];
	sg_rune_compact_source_surface_t saved_source_surface =
		fixture->source_surfaces[0];
	sg_rune_compact_response_patch_t saved_response_patch =
		fixture->response_patches[0];
	sg_rune_compact_mechanisms_view_t saved_view = fixture->mechanisms_view;
	const sg_rune_compact_static_transition_t *saved_static_transitions =
		fixture->static_data.transitions;
	uint32_t saved_static_transition_count = fixture->static_data.transition_count;
	uint32_t saved_portal_mechanism_count =
		fixture->static_data.portal_mechanism_count;
	uint32_t saved_entity_count =
		fixture->geometry.identity.source_counts.entity_count;
	uint32_t saved_hook_model = fixture->hook_surfaces[0].model;
	uint32_t saved_hook_flags = fixture->hook_surfaces[0].flags;
	uint32_t saved_visibility_model = fixture->visibility_surfaces[0].model;
	uint32_t saved_visibility_flags = fixture->visibility_surfaces[0].flags;
	sg_rune_compact_movement_fields_t *fields = NULL;
	sg_rune_compact_movement_fields_view_t view;
	sg_rune_compact_movement_fields_error_t error;
	const sg_rune_movement_capability_t *field;
	const sg_rune_analytic_function_t *cost;
	const sg_rune_analytic_function_t *time;
	uint32_t target_index;
	int func_static_target;

	fixture->geometry.identity.source_counts.entity_count = 3U;
	SyncFixtureIdentity(fixture);
	fixture->authorities[0].kind = SG_RUNE_COMPACT_MECHANISM_AUTHORITY_DOOR;
	fixture->authorities[0].initial_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE;
	fixture->authorities[0].activated_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture->authorities[0].delay_ms = 100U;
	fixture->authorities[0].dwell_ms = 200U;
	fixture->authorities[0].pause_ms = 300U;
	fixture->authorities[0].travel_ms = 1500U;
	fixture->authorities[0].recovery_ms = 400U;
	fixture->mechanisms[0].kind = SG_RUNE_COMPACT_MECHANISM_DOOR;
	fixture->portal_mechanisms[0].kind =
		SG_RUNE_COMPACT_PORTAL_MECHANISM_BLOCKS;
	fixture->static_data.portal_mechanism_count = 1U;
	memset(&fixture->transitions[0], 0, sizeof(fixture->transitions[0]));
	fixture->transitions[0].mechanism = 0U;
	fixture->transitions[0].kind =
		SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE;
	fixture->transitions[0].entry_cell.value = 0U;
	fixture->transitions[0].exit_cell.value = 1U;
	fixture->transitions[0].source_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE;
	fixture->transitions[0].destination_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture->transitions[0].elapsed_ms = 1500U;
	fixture->transitions[0].value.portal_state.portal.value = 0U;
	fixture->transitions[0].value.portal_state.mover_model = 1U;
	fixture->transitions[0].value.portal_state.delay_ms = 100U;
	fixture->transitions[0].value.portal_state.dwell_ms = 200U;
	fixture->transitions[0].value.portal_state.pause_ms = 300U;
	fixture->transitions[0].value.portal_state.travel_ms = 1500U;
	fixture->transitions[0].value.portal_state.recovery_ms = 400U;
	fixture->transitions[0].value.portal_state.source_blocked = 1U;
	fixture->transitions[0].value.portal_state.destination_blocked = 0U;
	SyncFixtureTransition(fixture);
	ConfigureController(fixture, 2U);
	CHECK(SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	if (fields != NULL) {
		CHECK(SG_RuneCompactMovementFieldsRead(fields, &view));
		field = FindField(&view, 2U, SG_RUNE_COMPACT_INDEX_NONE,
			SG_RUNE_MOVEMENT_CAPABILITY_CONTROLLER_ACTION);
		cost = field == NULL ? NULL : FindOutput(&view, field,
			SG_RUNE_ANALYTIC_OUTPUT_COST);
		time = field == NULL ? NULL : FindOutput(&view, field,
			SG_RUNE_ANALYTIC_OUTPUT_TRAVEL_TIME_SECONDS);
		CHECK(field != NULL && field->fibers.count == 1U &&
			view.fibers[field->fibers.first].mechanism_transition.value == 0U &&
			view.fibers[field->fibers.first].controller_action_controller.value ==
				0U &&
			view.fibers[field->fibers.first].controller_action_target.value == 0U &&
			cost != NULL &&
			time != NULL &&
			cost->form == SG_RUNE_COMPACT_ANALYTIC_AFFINE &&
			time->form == SG_RUNE_COMPACT_ANALYTIC_AFFINE &&
			!FieldHasInput(&view.analytic, time,
				SG_RUNE_ANALYTIC_INPUT_MOVER_PHASE) &&
			SameFloat(ScalarValue(view.analytic.affines[cost->definition].bias),
				2.5f) &&
			SameFloat(ScalarValue(view.analytic.affines[time->definition].bias),
				2.5f));
		SG_RuneCompactMovementFieldsDestroy(fields);
		fields = NULL;
	}
	DisableControllers(fixture);
	fixture->transitions[0].value.portal_state.source_blocked = 2U;
	SyncFixtureTransition(fixture);
	CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	fixture->transitions[0].value.portal_state.source_blocked = 1U;
	fixture->transitions[0].value.portal_state.destination_blocked = 1U;
	SyncFixtureTransition(fixture);
	CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	fixture->transitions[0].value.portal_state.destination_blocked = 0U;
	fixture->transitions[0].value.portal_state.reserved[0] = 1U;
	SyncFixtureTransition(fixture);
	CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	fixture->transitions[0].value.portal_state.reserved[0] = 0U;
	fixture->static_data.portal_mechanism_count = 0U;
	fixture->transitions[0].source_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture->transitions[0].destination_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE;
	SyncFixtureTransition(fixture);
	CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	fixture->authorities[0].delay_ms = 0U;
	fixture->authorities[0].dwell_ms = 0U;
	fixture->authorities[0].pause_ms = 0U;
	fixture->authorities[0].travel_ms = 0U;
	fixture->authorities[0].recovery_ms = 0U;
	fixture->authorities[0].kind =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TELEPORT;
	fixture->authorities[0].initial_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture->authorities[0].activated_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	memset(&fixture->transitions[0], 0, sizeof(fixture->transitions[0]));
	fixture->transitions[0].mechanism = 0U;
	fixture->transitions[0].kind =
		SG_RUNE_COMPACT_MECHANISM_TRANSITION_TELEPORT;
	fixture->transitions[0].entry_cell.value = 0U;
	fixture->transitions[0].exit_cell.value = 1U;
	fixture->transitions[0].source_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture->transitions[0].destination_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture->transitions[0].value.teleport.destination.entity_ordinal = 1U;
	fixture->transitions[0].value.teleport.fanout_ordinal = 0U;
	fixture->transitions[0].value.teleport.approach_witness.value[0] = 32;
	fixture->transitions[0].value.teleport.approach_witness.value[1] = 32;
	fixture->transitions[0].value.teleport.approach_witness.value[2] = 32;
	fixture->transitions[0].value.teleport.entry_witness =
		fixture->transitions[0].value.teleport.approach_witness;
	fixture->transitions[0].value.teleport.exit_witness.value[0] = 160;
	fixture->transitions[0].value.teleport.exit_witness.value[1] = 32;
	fixture->transitions[0].value.teleport.exit_witness.value[2] = 32;
	SyncFixtureTransition(fixture);
	CHECK(SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	if (fields != NULL) {
		CHECK(SG_RuneCompactMovementFieldsRead(fields, &view));
		field = FindField(&view, 0U, SG_RUNE_COMPACT_INDEX_NONE,
			SG_RUNE_MOVEMENT_CAPABILITY_MOVER);
		if (field != NULL && field->fibers.count == 1U) {
			const sg_rune_compact_movement_fiber_t *fiber =
				&view.fibers[field->fibers.first];
			const sg_rune_compact_movement_state_t *source =
				&view.states[fiber->source_state.value];
			const sg_rune_compact_movement_state_t *destination =
				&view.states[fiber->destination_state.value];
			const sg_rune_movement_state_variables_t expected_variables =
				SG_RUNE_MOVEMENT_STATE_POSITION |
				SG_RUNE_MOVEMENT_STATE_VELOCITY |
				SG_RUNE_MOVEMENT_STATE_STANCE |
				SG_RUNE_MOVEMENT_STATE_TIME |
				SG_RUNE_MOVEMENT_STATE_SUPPORT |
				SG_RUNE_MOVEMENT_STATE_WATER;

			CHECK(fiber->mechanism_transition.value == 0U &&
				fiber->state_variables == expected_variables &&
				source->support == SG_RUNE_MOVEMENT_SUPPORT_STATIC &&
				source->water == SG_RUNE_MOVEMENT_WATER_DRY &&
				source->mover_mechanism == SG_RUNE_COMPACT_INDEX_NONE &&
				(source->flags &
					SG_RUNE_MOVEMENT_STATE_MOVER_RELATIVE) == 0U &&
				destination->support == SG_RUNE_MOVEMENT_SUPPORT_STATIC &&
				destination->water == SG_RUNE_MOVEMENT_WATER_SUBMERGED &&
				destination->mover_mechanism ==
					SG_RUNE_COMPACT_INDEX_NONE &&
				(destination->flags &
					SG_RUNE_MOVEMENT_STATE_MOVER_RELATIVE) == 0U);
		} else {
			CHECK(0);
		}
		SG_RuneCompactMovementFieldsDestroy(fields);
		fields = NULL;
	}
	fixture->authorities[0].delay_ms = 1U;
	CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	fixture->authorities[0].delay_ms = 0U;
	fixture->transitions[0].source_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE;
	SyncFixtureTransition(fixture);
	CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));

	fixture->authorities[0].kind = SG_RUNE_COMPACT_MECHANISM_AUTHORITY_PUSH;
	fixture->transitions[0].kind = SG_RUNE_COMPACT_MECHANISM_TRANSITION_PUSH;
	fixture->transitions[0].source_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture->transitions[0].destination_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture->transitions[0].elapsed_ms = 250U;
	fixture->transitions[0].value.push.approach_witness.value[0] = 32;
	fixture->transitions[0].value.push.approach_witness.value[1] = 32;
	fixture->transitions[0].value.push.approach_witness.value[2] = 32;
	fixture->transitions[0].value.push.entry_witness =
		fixture->transitions[0].value.push.approach_witness;
	fixture->transitions[0].value.push.exit_witness.value[0] = 160;
	fixture->transitions[0].value.push.exit_witness.value[1] = 32;
	fixture->transitions[0].value.push.exit_witness.value[2] = 32;
	fixture->transitions[0].value.push.launch_velocity_bits[0] = Bits(120.0f);
	fixture->transitions[0].value.push.launch_velocity_bits[1] = Bits(-30.0f);
	fixture->transitions[0].value.push.launch_velocity_bits[2] = Bits(40.0f);
	fixture->transitions[0].value.push.gravity_bits = Bits(100.0f);
	fixture->transitions[0].value.push.flight_ms = 250U;
	SyncFixtureTransition(fixture);
	CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	fixture->transitions[0].value.push.gravity_bits =
		fixture->geometry.identity.physics.gravity_bits;
	SyncFixtureTransition(fixture);
	CHECK(SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	if (fields != NULL && SG_RuneCompactMovementFieldsRead(fields, &view)) {
		CHECK(FindFieldForStance(&view, 0U,
			SG_RUNE_COMPACT_INDEX_NONE,
			SG_RUNE_MOVEMENT_CAPABILITY_EXTERNAL_FORCE,
			SG_RUNE_STANCE_VALID_STANDING) != NULL);
		CHECK(FindFieldForStance(&view, 0U,
			SG_RUNE_COMPACT_INDEX_NONE,
			SG_RUNE_MOVEMENT_CAPABILITY_EXTERNAL_FORCE,
			SG_RUNE_STANCE_VALID_CROUCHING) != NULL);
	}
	SG_RuneCompactMovementFieldsDestroy(fields);
	fields = NULL;
	fixture->transitions[0].elapsed_ms = 249U;
	SyncFixtureTransition(fixture);
	CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));

	fixture->authorities[0].kind = SG_RUNE_COMPACT_MECHANISM_AUTHORITY_TRAIN;
	fixture->authorities[0].activation =
		SG_RUNE_COMPACT_MECHANISM_ACTIVATION_MASK_AUTO;
	fixture->source_surfaces[0].source.model = 1U;
	fixture->source_surfaces[0].frame =
		SG_RUNE_COMPACT_SOURCE_SURFACE_MODEL_LOCAL;
	fixture->source_surfaces[0].cell.value = SG_RUNE_COMPACT_INDEX_NONE;
	fixture->source_surfaces[0].parent_surface = SG_RUNE_COMPACT_INDEX_NONE;
	fixture->source_surfaces[0].split_ordinal = 0U;
	fixture->hook_surfaces[0].model = 1U;
	fixture->hook_surfaces[0].flags = SG_CONFIGURATION_HOOK_SURFACE_HOOKABLE |
		SG_CONFIGURATION_HOOK_SURFACE_MOVING_MODEL;
	fixture->visibility_surfaces[0].model = 1U;
	fixture->visibility_surfaces[0].flags =
		SG_CONFIGURATION_HOOK_SURFACE_HOOKABLE |
		SG_CONFIGURATION_HOOK_SURFACE_MOVING_MODEL;
	fixture->response_patches[0].model = 1U;
	fixture->response_patches[0].source_frame =
		SG_RUNE_COMPACT_SOURCE_SURFACE_MODEL_LOCAL;
	fixture->response_patches[0].parent_facet.value =
		SG_RUNE_COMPACT_RESPONSE_INDEX_NONE;
	fixture->response_patches[0].boundary_incidences.first = 0U;
	fixture->response_patches[0].boundary_incidences.count = 0U;
	fixture->response_patches[0].flags =
		SG_RUNE_COMPACT_RESPONSE_PATCH_HOOKABLE |
		SG_RUNE_COMPACT_RESPONSE_PATCH_MOVING;
	memset(&fixture->transitions[0], 0, sizeof(fixture->transitions[0]));
	fixture->transitions[0].mechanism = 0U;
	fixture->transitions[0].kind =
		SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT;
	fixture->transitions[0].entry_cell.value = 0U;
	fixture->transitions[0].exit_cell.value = 1U;
	fixture->transitions[0].source_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture->transitions[0].destination_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture->transitions[0].elapsed_ms = 5000U;
	fixture->transitions[0].value.transport.mover_model = 1U;
	fixture->transitions[0].value.transport.source_surface_ordinal = 0U;
	fixture->transitions[0].value.transport.source_player_local.value[0] = 8;
	fixture->transitions[0].value.transport.destination_player_local.value[0] = 8;
	fixture->transitions[0].value.transport.source_support_local.value[1] = 8;
	fixture->transitions[0].value.transport.destination_support_local.value[1] = 8;
	fixture->transitions[0].value.transport.source_mover_origin_bits[0] =
		Bits(10.0f);
	fixture->transitions[0].value.transport.source_mover_origin_bits[1] =
		Bits(20.0f);
	fixture->transitions[0].value.transport.source_mover_origin_bits[2] =
		Bits(30.0f);
	fixture->transitions[0].value.transport.source_mover_axis_bits[0][1] =
		Bits(1.0f);
	fixture->transitions[0].value.transport.source_mover_axis_bits[1][0] =
		Bits(-1.0f);
	fixture->transitions[0].value.transport.source_mover_axis_bits[2][2] =
		Bits(1.0f);
	fixture->transitions[0].value.transport.destination_mover_origin_bits[0] =
		Bits(20.0f);
	fixture->transitions[0].value.transport.destination_mover_origin_bits[1] =
		Bits(30.0f);
	fixture->transitions[0].value.transport.destination_mover_origin_bits[2] =
		Bits(40.0f);
	fixture->transitions[0].value.transport.destination_mover_axis_bits[0][0] =
		Bits(1.0f);
	fixture->transitions[0].value.transport.destination_mover_axis_bits[1][1] =
		Bits(1.0f);
	fixture->transitions[0].value.transport.destination_mover_axis_bits[2][2] =
		Bits(1.0f);
	fixture->transitions[0].value.transport.source_player_world_bits[0] =
		Bits(10.0f);
	fixture->transitions[0].value.transport.source_player_world_bits[1] =
		Bits(21.0f);
	fixture->transitions[0].value.transport.source_player_world_bits[2] =
		Bits(30.0f);
	fixture->transitions[0].value.transport.source_support_world_bits[0] =
		Bits(9.0f);
	fixture->transitions[0].value.transport.source_support_world_bits[1] =
		Bits(20.0f);
	fixture->transitions[0].value.transport.source_support_world_bits[2] =
		Bits(30.0f);
	fixture->transitions[0].value.transport.destination_player_world_bits[0] =
		Bits(21.0f);
	fixture->transitions[0].value.transport.destination_player_world_bits[1] =
		Bits(30.0f);
	fixture->transitions[0].value.transport.destination_player_world_bits[2] =
		Bits(40.0f);
	fixture->transitions[0].value.transport.destination_support_world_bits[0] =
		Bits(20.0f);
	fixture->transitions[0].value.transport.destination_support_world_bits[1] =
		Bits(31.0f);
	fixture->transitions[0].value.transport.destination_support_world_bits[2] =
		Bits(40.0f);
	fixture->transitions[0].value.transport.source_endpoint.entity_ordinal = 1U;
	fixture->transitions[0].value.transport.destination_endpoint.entity_ordinal = 2U;
	fixture->transitions[0].value.transport.fanout_ordinal = 0U;
	fixture->transitions[0].value.transport.swept_static_clear = 1U;
	fixture->transitions[0].value.transport.start_supported = 1U;
	fixture->transitions[0].value.transport.end_supported = 1U;
	SyncFixtureTransition(fixture);
	CHECK(SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	if (fields != NULL) {
		CHECK(SG_RuneCompactMovementFieldsRead(fields, &view));
		field = FindField(&view, 0U, SG_RUNE_COMPACT_INDEX_NONE,
			SG_RUNE_MOVEMENT_CAPABILITY_MOVER);
		time = field == NULL ? NULL : FindOutput(&view, field,
			SG_RUNE_ANALYTIC_OUTPUT_TRAVEL_TIME_SECONDS);
		CHECK(time != NULL && time->form == SG_RUNE_COMPACT_ANALYTIC_AFFINE &&
			SameFloat(ScalarValue(view.analytic.affines[time->definition].bias),
				5.0f));
		func_static_target = 0;
		for (target_index = 0U; target_index < view.hook_target_count;
			target_index++)
			if (view.hook_targets[target_index].target_kind ==
					SG_HOST_HOOK_TARGET_FUNC &&
				view.hook_targets[target_index].provenance ==
					SG_RUNE_MOVEMENT_HOOK_TARGET_PROVENANCE_STATIC_RESPONSE)
				func_static_target = 1;
		CHECK(func_static_target);
		SG_RuneCompactMovementFieldsDestroy(fields);
	}
	fields = NULL;
	fixture->source_surfaces[0].cell.value = 0U;
	CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	fixture->source_surfaces[0].cell.value = SG_RUNE_COMPACT_INDEX_NONE;
	fixture->source_surfaces[0].parent_surface = 0U;
	CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	fixture->source_surfaces[0].parent_surface = SG_RUNE_COMPACT_INDEX_NONE;
	fixture->source_surfaces[0].split_ordinal = 1U;
	CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	fixture->source_surfaces[0].split_ordinal = 0U;
	fixture->transitions[0].value.transport.source_player_world_bits[0] =
		Bits(-0.0f);
	SyncFixtureTransition(fixture);
	CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	fixture->transitions[0].value.transport.source_player_world_bits[0] =
		Bits(10.0f);
	fixture->transitions[0].value.transport.destination_mover_origin_bits[0] =
		Bits(22.0f);
	SyncFixtureTransition(fixture);
	CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	fixture->transitions[0].value.transport.destination_mover_origin_bits[0] =
		Bits(20.0f);
	fixture->transitions[0].value.transport.source_mover_axis_bits[0][0] =
		Bits(-0.0f);
	SyncFixtureTransition(fixture);
	CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	fixture->transitions[0].value.transport.source_mover_axis_bits[0][0] = 0U;
	fixture->authorities[0].kind =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_LIFT;
	fixture->authorities[0].initial_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE;
	fixture->authorities[0].activated_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture->transitions[0].source_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_INACTIVE;
	fixture->transitions[0].destination_state =
		SG_RUNE_COMPACT_MECHANISM_AUTHORITY_STATE_ACTIVE;
	fixture->transitions[0].value.transport.source_endpoint.entity_ordinal =
		SG_RUNE_COMPACT_INDEX_NONE;
	fixture->transitions[0].value.transport.destination_endpoint.entity_ordinal =
		SG_RUNE_COMPACT_INDEX_NONE;
	fixture->transitions[0].value.transport.fanout_ordinal =
		SG_RUNE_COMPACT_INDEX_NONE;
	SyncFixtureTransition(fixture);
	CHECK(SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	SG_RuneCompactMovementFieldsDestroy(fields);
	fields = NULL;
	fixture->transitions[0].value.transport.source_endpoint.entity_ordinal = 1U;
	SyncFixtureTransition(fixture);
	CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	fixture->transitions[0].value.transport.source_endpoint.entity_ordinal =
		SG_RUNE_COMPACT_INDEX_NONE;
	fixture->transitions[0].value.transport.destination_endpoint.entity_ordinal =
		2U;
	SyncFixtureTransition(fixture);
	CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	fixture->transitions[0].value.transport.destination_endpoint.entity_ordinal =
		SG_RUNE_COMPACT_INDEX_NONE;
	fixture->transitions[0].value.transport.fanout_ordinal = 0U;
	SyncFixtureTransition(fixture);
	CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));

	fixture->authorities[0] = saved_authority;
	fixture->mechanisms[0] = saved_static_mechanism;
	fixture->portal_mechanisms[0] = saved_portal_mechanism;
	fixture->source_surfaces[0] = saved_source_surface;
	fixture->response_patches[0] = saved_response_patch;
	fixture->hook_surfaces[0].model = saved_hook_model;
	fixture->hook_surfaces[0].flags = saved_hook_flags;
	fixture->visibility_surfaces[0].model = saved_visibility_model;
	fixture->visibility_surfaces[0].flags = saved_visibility_flags;
	fixture->mechanisms_view = saved_view;
	fixture->static_data.transitions = saved_static_transitions;
	fixture->static_data.transition_count = saved_static_transition_count;
	fixture->static_data.portal_mechanism_count = saved_portal_mechanism_count;
	fixture->geometry.identity.source_counts.entity_count = saved_entity_count;
	SyncFixtureIdentity(fixture);
}

static void CheckHookChronology(const movement_fixture_t *fixture)
{
	sg_host_hook_observation_t observation;
	sg_host_hook_step_t step;
	int result;

	memset(&observation, 0, sizeof(observation));
	observation.event = SG_HOST_HOOK_FIRE;
	observation.phase = SG_HOST_HOOK_IDLE;
	observation.attack_held = 1;
	result = SG_HostHookStep(&fixture->host.hook, &observation, &step);
	CHECK(result && step.accepted &&
		step.next_phase == SG_HOST_HOOK_IN_FLIGHT);
	memset(&observation, 0, sizeof(observation));
	observation.event = SG_HOST_HOOK_FLIGHT_HIT;
	observation.phase = SG_HOST_HOOK_IN_FLIGHT;
	observation.first_hit = 1;
	observation.target_kind = SG_HOST_HOOK_TARGET_WORLD;
	observation.target_identity = UINT64_C(77);
	result = SG_HostHookStep(&fixture->host.hook, &observation, &step);
	CHECK(result && step.attached && step.next_phase == SG_HOST_HOOK_ATTACHED &&
		step.target_identity == UINT64_C(77));
	memset(&observation, 0, sizeof(observation));
	observation.event = SG_HOST_HOOK_RELEASE;
	observation.phase = SG_HOST_HOOK_ATTACHED;
	result = SG_HostHookStep(&fixture->host.hook, &observation, &step);
	CHECK(result && step.released && step.coast_velocity &&
		step.next_phase == SG_HOST_HOOK_COAST);
	memset(&observation, 0, sizeof(observation));
	observation.event = SG_HOST_HOOK_REFIRE;
	observation.phase = SG_HOST_HOOK_COAST;
	observation.attack_held = 1;
	result = SG_HostHookStep(&fixture->host.hook, &observation, &step);
	CHECK(result && step.accepted && step.next_phase == SG_HOST_HOOK_IN_FLIGHT);
}

static void CheckProfilePruning(movement_fixture_t *fixture,
	uint32_t full_function_count)
{
	sg_rune_compact_movement_fields_t *fields = NULL;
	sg_rune_compact_movement_fields_view_t view;
	sg_rune_compact_movement_fields_error_t error;
	uint32_t saved_region_flags = fixture->regions[1].flags;
	sg_rune_compact_contents_mask_t saved_contents = fixture->cells[1].contents;
	uint32_t saved_binding_count = fixture->static_data.portal_mechanism_count;
	float saved_external_acceleration =
		fixture->host.static_identity.physics.external_acceleration;

	fixture->regions[1].flags = SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED;
	fixture->cells[1].contents = 0U;
	fixture->static_data.portal_mechanism_count = 0U;
	fixture->host.static_identity.physics.external_acceleration = 0.0f;
	SyncFixtureIdentity(fixture);
	CHECK(SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	if (fields == NULL) {
		fixture->regions[1].flags = saved_region_flags;
		fixture->cells[1].contents = saved_contents;
		fixture->static_data.portal_mechanism_count = saved_binding_count;
		fixture->host.static_identity.physics.external_acceleration =
			saved_external_acceleration;
		SyncFixtureIdentity(fixture);
		return;
	}
	if (!SG_RuneCompactMovementFieldsRead(fields, &view)) {
		CHECK(0);
		SG_RuneCompactMovementFieldsDestroy(fields);
		fixture->regions[1].flags = saved_region_flags;
		fixture->cells[1].contents = saved_contents;
		fixture->static_data.portal_mechanism_count = saved_binding_count;
		fixture->host.static_identity.physics.external_acceleration =
			saved_external_acceleration;
		SyncFixtureIdentity(fixture);
		return;
	}
	CHECK(SG_RuneCompactAnalyticValidate(&view.analytic, NULL));
	CHECK(view.analytic.function_count <= full_function_count);
	CHECK(CountFamily(&view, SG_RUNE_MOVEMENT_CAPABILITY_SWIM) == 0U);
	CHECK(CountFamily(&view, SG_RUNE_MOVEMENT_CAPABILITY_MOVER) == 0U);
	CHECK(CountFamily(&view, SG_RUNE_MOVEMENT_CAPABILITY_EXTERNAL_FORCE) == 0U);
	SG_RuneCompactMovementFieldsDestroy(fields);
	fixture->regions[1].flags = saved_region_flags;
	fixture->cells[1].contents = saved_contents;
	fixture->static_data.portal_mechanism_count = saved_binding_count;
	fixture->host.static_identity.physics.external_acceleration =
		saved_external_acceleration;
	SyncFixtureIdentity(fixture);
}

static int AnalyticEqual(const sg_rune_compact_analytic_t *left,
	const sg_rune_compact_analytic_t *right)
{
	if (left->version != right->version || left->reserved != right->reserved ||
		left->function_count != right->function_count ||
		left->input_dimension_count != right->input_dimension_count ||
		left->affine_count != right->affine_count ||
		left->affine_slope_count != right->affine_slope_count ||
		left->polynomial_count != right->polynomial_count ||
		left->polynomial_coefficient_count != right->polynomial_coefficient_count ||
		left->ballistic_count != right->ballistic_count ||
		left->piecewise_count != right->piecewise_count ||
		left->piecewise_clause_count != right->piecewise_clause_count)
		return 0;
	if (left->function_count != 0U && memcmp(left->functions, right->functions,
		(size_t)left->function_count * sizeof(*left->functions)) != 0)
		return 0;
	if (left->input_dimension_count != 0U && memcmp(left->input_dimensions,
		right->input_dimensions,
		(size_t)left->input_dimension_count * sizeof(*left->input_dimensions)) != 0)
		return 0;
	if (left->affine_count != 0U && memcmp(left->affines, right->affines,
		(size_t)left->affine_count * sizeof(*left->affines)) != 0)
		return 0;
	if (left->affine_slope_count != 0U && memcmp(left->affine_slopes,
		right->affine_slopes,
		(size_t)left->affine_slope_count * sizeof(*left->affine_slopes)) != 0)
		return 0;
	if (left->polynomial_count != 0U && memcmp(left->polynomials,
		right->polynomials,
		(size_t)left->polynomial_count * sizeof(*left->polynomials)) != 0)
		return 0;
	if (left->polynomial_coefficient_count != 0U &&
		memcmp(left->polynomial_coefficients, right->polynomial_coefficients,
		(size_t)left->polynomial_coefficient_count *
			sizeof(*left->polynomial_coefficients)) != 0)
		return 0;
	if (left->ballistic_count != 0U && memcmp(left->ballistics,
		right->ballistics,
		(size_t)left->ballistic_count * sizeof(*left->ballistics)) != 0)
		return 0;
	if (left->piecewise_count != 0U && memcmp(left->piecewise,
		right->piecewise,
		(size_t)left->piecewise_count * sizeof(*left->piecewise)) != 0)
		return 0;
	if (left->piecewise_clause_count != 0U && memcmp(
		left->piecewise_clauses, right->piecewise_clauses,
		(size_t)left->piecewise_clause_count *
			sizeof(*left->piecewise_clauses)) != 0)
		return 0;
	return 1;
}

static void CheckDeterminism(movement_fixture_t *fixture)
{
	sg_rune_compact_movement_fields_t *left_fields = NULL;
	sg_rune_compact_movement_fields_t *right_fields = NULL;
	sg_rune_compact_movement_fields_view_t left;
	sg_rune_compact_movement_fields_view_t right;
	sg_rune_compact_movement_fields_error_t error;

	CHECK(SG_RuneCompactMovementFieldsBuild(&fixture->input, &left_fields,
		&error));
	CHECK(SG_RuneCompactMovementFieldsBuild(&fixture->input, &right_fields,
		&error));
	if (left_fields == NULL || right_fields == NULL) {
		SG_RuneCompactMovementFieldsDestroy(left_fields);
		SG_RuneCompactMovementFieldsDestroy(right_fields);
		return;
	}
	if (!SG_RuneCompactMovementFieldsRead(left_fields, &left) ||
		!SG_RuneCompactMovementFieldsRead(right_fields, &right)) {
		CHECK(0);
		SG_RuneCompactMovementFieldsDestroy(left_fields);
		SG_RuneCompactMovementFieldsDestroy(right_fields);
		return;
	}
	CHECK(left.capability_count == right.capability_count);
	CHECK(left.state_count == right.state_count);
	CHECK(left.fiber_count == right.fiber_count);
	CHECK(left.hook_target_count == right.hook_target_count);
	CHECK(left.fiber_function_ref_count == right.fiber_function_ref_count);
	CHECK(left.angular_schedule_count == right.angular_schedule_count);
	CHECK(memcmp(left.capabilities, right.capabilities,
		(size_t)left.capability_count * sizeof(*left.capabilities)) == 0);
	CHECK(memcmp(left.states, right.states,
		(size_t)left.state_count * sizeof(*left.states)) == 0);
	CHECK(memcmp(left.fibers, right.fibers,
		(size_t)left.fiber_count * sizeof(*left.fibers)) == 0);
	CHECK(memcmp(left.hook_targets, right.hook_targets,
		(size_t)left.hook_target_count * sizeof(*left.hook_targets)) == 0);
	CHECK(memcmp(left.fiber_function_refs, right.fiber_function_refs,
		(size_t)left.fiber_function_ref_count *
			sizeof(*left.fiber_function_refs)) == 0);
	if (left.angular_schedule_count == right.angular_schedule_count &&
		left.angular_schedule_count != 0U)
		CHECK(memcmp(left.angular_schedules, right.angular_schedules,
			(size_t)left.angular_schedule_count *
				sizeof(*left.angular_schedules)) == 0);
	CHECK(memcmp(&left.identity, &right.identity, sizeof(left.identity)) == 0);
	CHECK(memcmp(&left.pmove_abi, &right.pmove_abi,
		sizeof(left.pmove_abi)) == 0);
	CHECK(left.pmove_behavior_fingerprint == right.pmove_behavior_fingerprint);
	CHECK(left.host_level_generation == right.host_level_generation);
	CHECK(left.physics_abi_id == right.physics_abi_id);
	CHECK(left.collision_law_id == right.collision_law_id);
	CHECK(left.pmove_law_id == right.pmove_law_id);
	CHECK(left.gravity_law_id == right.gravity_law_id);
	CHECK(left.hook_law_id == right.hook_law_id);
	CHECK(left.mechanism_law_id == right.mechanism_law_id);
	CHECK(AnalyticEqual(&left.analytic, &right.analytic));
	SG_RuneCompactMovementFieldsDestroy(left_fields);
	SG_RuneCompactMovementFieldsDestroy(right_fields);
}

static void CheckConditionalAndSky(movement_fixture_t *fixture)
{
	sg_rune_compact_movement_fields_t *fields = NULL;
	sg_rune_compact_movement_fields_view_t view;
	sg_rune_compact_movement_fields_error_t error;
	const sg_rune_movement_capability_t *hook;
	const sg_rune_analytic_function_t *reachability;
	sg_static_visibility_class_t saved_classification =
		fixture->response_pairs[0].classification;
	sg_rune_compact_response_patch_flags_t saved_patch_flags =
		fixture->response_patches[0].flags;
	sg_configuration_hook_surface_t *saved_hook_surfaces =
		fixture->configuration.hook_surfaces;
	uint32_t saved_hook_surface_count = fixture->configuration.hook_surface_count;
	sg_static_visibility_surface_t *saved_visibility_surfaces =
		fixture->visibility.surfaces;
	uint32_t saved_visibility_surface_count = fixture->visibility.surface_count;
	const sg_rune_compact_response_patch_t *saved_target_patches =
		fixture->response_partition.target_patches;
	uint32_t saved_target_patch_count =
		fixture->response_partition.target_patch_count;
	const sg_rune_compact_response_pair_t *saved_response_pairs =
		fixture->response_partition.response_pairs;
	uint32_t saved_response_pair_count = fixture->response_partition.response_pair_count;
	const sg_rune_compact_response_candidate_group_t *saved_candidate_groups =
		fixture->response_partition.candidate_groups;
	uint32_t saved_candidate_group_count =
		fixture->response_partition.candidate_group_count;
	const sg_rune_compact_response_endpoint_group_t *saved_target_endpoint_groups =
		fixture->response_partition.target_endpoint_groups;
	uint32_t saved_target_endpoint_group_count =
		fixture->response_partition.target_endpoint_group_count;
	const uint32_t *saved_target_endpoint_members =
		fixture->response_partition.target_endpoint_members;
	uint32_t saved_target_endpoint_member_count =
		fixture->response_partition.target_endpoint_member_count;
	sg_configuration_hook_surface_flags_t saved_configuration_surface_flags =
		fixture->hook_surfaces[0].flags;
	sg_configuration_hook_surface_flags_t saved_visibility_surface_flags =
		fixture->visibility_surfaces[0].flags;
	sg_rune_compact_facet_index_t saved_parent_facet =
		fixture->response_patches[0].parent_facet;
	sg_rune_compact_response_pair_t saved_response_pair =
		fixture->response_pairs[0];
	uint32_t saved_seal_target_patch_count =
		fixture->response_partition.seal.target_patch_count;
	uint32_t saved_seal_response_pair_count =
		fixture->response_partition.seal.response_pair_count;
	uint32_t saved_seal_target_endpoint_group_count =
		fixture->response_partition.seal.target_endpoint_group_count;
	uint32_t saved_seal_target_endpoint_member_count =
		fixture->response_partition.seal.target_endpoint_member_count;
	uint32_t saved_seal_certified_direct_pair_count =
		fixture->response_partition.seal.certified_direct_pair_count;
	uint32_t saved_seal_unresolved_candidate_group_count =
		fixture->response_partition.seal.unresolved_candidate_group_count;

	/* Conditional status comes from this certified source/target response;
	 * retained hook annotations do not override its sky configuration. */
	fixture->response_pairs[0].classification = SG_STATIC_VISIBILITY_CONDITIONAL;
	fixture->response_pairs[0].certificate =
		SG_RUNE_COMPACT_RESPONSE_UNRESOLVED_EXACT_RAY;
	fixture->response_pairs[0].requires_exact_ray = 1U;
	fixture->response_pairs[0].relation_flags = 0U;
	CHECK(SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	if (fields != NULL) {
		CHECK(SG_RuneCompactMovementFieldsRead(fields, &view));
		hook = FindField(&view, 0U, SG_RUNE_COMPACT_INDEX_NONE,
			SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BODY);
		reachability = hook == NULL ? NULL : FindOutput(&view, hook,
			SG_RUNE_ANALYTIC_OUTPUT_REACHABILITY_MARGIN);
		CHECK(reachability != NULL && SameFloat(ScalarValue(
			view.analytic.affines[reachability->definition].bias), -1.0f));
	}
	SG_RuneCompactMovementFieldsDestroy(fields);
	fixture->response_pairs[0].classification = saved_classification;
	/* Removing static responses retains only generic dynamic target laws. */
	fixture->configuration.hook_surfaces = NULL;
	fixture->configuration.hook_surface_count = 0U;
	fixture->visibility.surfaces = NULL;
	fixture->visibility.surface_count = 0U;
	fixture->response_partition.target_patches = NULL;
	fixture->response_partition.target_patch_count = 0U;
	fixture->response_partition.response_pairs = NULL;
	fixture->response_partition.response_pair_count = 0U;
	fixture->response_partition.candidate_groups = NULL;
	fixture->response_partition.candidate_group_count = 0U;
	fixture->response_partition.target_endpoint_groups = NULL;
	fixture->response_partition.target_endpoint_group_count = 0U;
	fixture->response_partition.target_endpoint_members = NULL;
	fixture->response_partition.target_endpoint_member_count = 0U;
	fixture->response_partition.seal.target_patch_count = 0U;
	fixture->response_partition.seal.response_pair_count = 0U;
	fixture->response_partition.seal.target_endpoint_group_count = 0U;
	fixture->response_partition.seal.target_endpoint_member_count = 0U;
	fixture->response_partition.seal.certified_direct_pair_count = 0U;
	fixture->response_partition.seal.unresolved_candidate_group_count = 0U;
	fields = NULL;
	CHECK(SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	SG_RuneCompactMovementFieldsDestroy(fields);
	fields = NULL;
	/* A sky patch is rejected even when the stale annotation/configuration
	 * still calls the same facet hookable.  This is a certificate failure,
	 * never a blocked-but-usable hook relation. */
	fixture->configuration.hook_surfaces = saved_hook_surfaces;
	fixture->configuration.hook_surface_count = saved_hook_surface_count;
	fixture->visibility.surfaces = saved_visibility_surfaces;
	fixture->visibility.surface_count = saved_visibility_surface_count;
	fixture->response_partition.target_patches = saved_target_patches;
	fixture->response_partition.target_patch_count = saved_target_patch_count;
	fixture->response_partition.response_pairs = saved_response_pairs;
	fixture->response_partition.response_pair_count = saved_response_pair_count;
	fixture->response_partition.candidate_groups = saved_candidate_groups;
	fixture->response_partition.candidate_group_count = saved_candidate_group_count;
	fixture->response_partition.target_endpoint_groups = saved_target_endpoint_groups;
	fixture->response_partition.target_endpoint_group_count =
		saved_target_endpoint_group_count;
	fixture->response_partition.target_endpoint_members = saved_target_endpoint_members;
	fixture->response_partition.target_endpoint_member_count =
		saved_target_endpoint_member_count;
	fixture->response_partition.seal.target_patch_count =
		saved_seal_target_patch_count;
	fixture->response_partition.seal.response_pair_count =
		saved_seal_response_pair_count;
	fixture->response_partition.seal.target_endpoint_group_count =
		saved_seal_target_endpoint_group_count;
	fixture->response_partition.seal.target_endpoint_member_count =
		saved_seal_target_endpoint_member_count;
	fixture->response_partition.seal.certified_direct_pair_count =
		saved_seal_certified_direct_pair_count;
	fixture->response_partition.seal.unresolved_candidate_group_count =
		saved_seal_unresolved_candidate_group_count;
	fixture->response_pairs[0] = saved_response_pair;
	/* A sky patch remains in the retained response corpus but is not an
	 * endpoint.  Its surface authority is explicitly sky and no certified
	 * response pair/group points at it. */
	fixture->configuration.hook_surfaces = fixture->hook_surfaces;
	fixture->configuration.hook_surface_count = 1U;
	fixture->hook_surfaces[0].flags = SG_CONFIGURATION_HOOK_SURFACE_SKY;
	fixture->visibility.surfaces = fixture->visibility_surfaces;
	fixture->visibility.surface_count = 1U;
	fixture->visibility_surfaces[0].flags = SG_CONFIGURATION_HOOK_SURFACE_SKY;
	fixture->response_patches[0].flags = SG_RUNE_COMPACT_RESPONSE_PATCH_SKY;
	fixture->response_patches[0].parent_facet.value =
		SG_RUNE_COMPACT_RESPONSE_INDEX_NONE;
	fixture->response_patches[0].boundary_incidences.first = 0U;
	fixture->response_patches[0].boundary_incidences.count = 0U;
	fixture->response_partition.target_endpoint_groups = NULL;
	fixture->response_partition.target_endpoint_group_count = 0U;
	fixture->response_partition.target_endpoint_members = NULL;
	fixture->response_partition.target_endpoint_member_count = 0U;
	fixture->response_partition.response_pairs = NULL;
	fixture->response_partition.response_pair_count = 0U;
	fixture->response_partition.candidate_groups = NULL;
	fixture->response_partition.candidate_group_count = 0U;
	fixture->response_partition.seal.response_pair_count = 0U;
	fixture->response_partition.seal.certified_direct_pair_count = 0U;
	fixture->response_partition.seal.unresolved_candidate_group_count = 0U;
	fixture->response_partition.seal.target_endpoint_group_count = 0U;
	fixture->response_partition.seal.target_endpoint_member_count = 0U;
	fields = NULL;
	CHECK(SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	SG_RuneCompactMovementFieldsDestroy(fields);
	fields = NULL;
	fixture->configuration.hook_surfaces = saved_hook_surfaces;
	fixture->configuration.hook_surface_count = saved_hook_surface_count;
	fixture->hook_surfaces[0].flags = saved_configuration_surface_flags;
	fixture->visibility.surfaces = saved_visibility_surfaces;
	fixture->visibility.surface_count = saved_visibility_surface_count;
	fixture->visibility_surfaces[0].flags = saved_visibility_surface_flags;
	fixture->response_patches[0].flags = saved_patch_flags;
	fixture->response_patches[0].parent_facet = saved_parent_facet;
	fixture->response_partition.target_endpoint_groups = saved_target_endpoint_groups;
	fixture->response_partition.target_endpoint_group_count =
		saved_target_endpoint_group_count;
	fixture->response_partition.target_endpoint_members = saved_target_endpoint_members;
	fixture->response_partition.target_endpoint_member_count =
		saved_target_endpoint_member_count;
	fixture->response_partition.response_pairs = saved_response_pairs;
	fixture->response_partition.response_pair_count = saved_response_pair_count;
	fixture->response_partition.candidate_groups = saved_candidate_groups;
	fixture->response_partition.candidate_group_count = saved_candidate_group_count;
	fixture->response_partition.seal.response_pair_count =
		saved_seal_response_pair_count;
	fixture->response_partition.seal.certified_direct_pair_count =
		saved_seal_certified_direct_pair_count;
	fixture->response_partition.seal.unresolved_candidate_group_count =
		saved_seal_unresolved_candidate_group_count;
	fixture->response_partition.seal.target_endpoint_group_count =
		saved_seal_target_endpoint_group_count;
	fixture->response_partition.seal.target_endpoint_member_count =
		saved_seal_target_endpoint_member_count;
	fixture->response_patches[0].flags =
		(sg_rune_compact_response_patch_flags_t)(saved_patch_flags |
			SG_RUNE_COMPACT_RESPONSE_PATCH_SKY);
	fields = (sg_rune_compact_movement_fields_t *)(uintptr_t)0x3333U;
	CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	CHECK(fields == (sg_rune_compact_movement_fields_t *)(uintptr_t)0x3333U &&
		error.code == SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_VISIBILITY);
	fixture->response_patches[0].flags = saved_patch_flags;
}

static void CheckConstraintFacet(movement_fixture_t *fixture)
{
	sg_rune_compact_movement_fields_t *fields = NULL;
	sg_rune_compact_movement_fields_error_t error;
	uint32_t saved_geometry_portal_count = fixture->geometry.portal_count;
	sg_rune_compact_vertex_span_t saved_vertices = fixture->facets[0].vertices;
	sg_rune_compact_facet_kind_t saved_kind = fixture->facets[0].kind;
	sg_rune_compact_portal_index_t saved_facet_portal =
		fixture->facets[0].portal;
	uint32_t saved_annotation_count =
		fixture->static_data.facet_annotation_count;
	uint32_t saved_portal_mechanism_count =
		fixture->static_data.portal_mechanism_count;
	uint32_t saved_hook_surface_count = fixture->configuration.hook_surface_count;
	sg_configuration_hook_surface_t *saved_hook_surfaces =
		fixture->configuration.hook_surfaces;
	uint32_t saved_visibility_surface_count = fixture->visibility.surface_count;
	sg_static_visibility_surface_t *saved_visibility_surfaces =
		fixture->visibility.surfaces;
	const sg_rune_compact_response_patch_t *saved_target_patches =
		fixture->response_partition.target_patches;
	uint32_t saved_target_patch_count =
		fixture->response_partition.target_patch_count;
	const sg_rune_q8_vec3_t *saved_target_vertices =
		fixture->response_partition.target_vertices;
	uint32_t saved_target_vertex_count =
		fixture->response_partition.target_vertex_count;
	const sg_rune_compact_response_pair_t *saved_response_pairs =
		fixture->response_partition.response_pairs;
	uint32_t saved_response_pair_count =
		fixture->response_partition.response_pair_count;
	const sg_rune_compact_response_candidate_group_t *saved_candidate_groups =
		fixture->response_partition.candidate_groups;
	uint32_t saved_candidate_group_count =
		fixture->response_partition.candidate_group_count;
	const sg_rune_compact_response_endpoint_group_t *saved_target_endpoint_groups =
		fixture->response_partition.target_endpoint_groups;
	uint32_t saved_target_endpoint_group_count =
		fixture->response_partition.target_endpoint_group_count;
	const uint32_t *saved_target_endpoint_members =
		fixture->response_partition.target_endpoint_members;
	uint32_t saved_target_endpoint_member_count =
		fixture->response_partition.target_endpoint_member_count;
	uint32_t saved_seal_target_patch_count =
		fixture->response_partition.seal.target_patch_count;
	uint32_t saved_seal_response_pair_count =
		fixture->response_partition.seal.response_pair_count;
	uint32_t saved_seal_target_endpoint_group_count =
		fixture->response_partition.seal.target_endpoint_group_count;
	uint32_t saved_seal_target_endpoint_member_count =
		fixture->response_partition.seal.target_endpoint_member_count;
	uint32_t saved_seal_unresolved_candidate_group_count =
		fixture->response_partition.seal.unresolved_candidate_group_count;

	fixture->facets[0].kind = SG_RUNE_COMPACT_FACET_CONSTRAINT_ONLY;
	fixture->facets[0].portal.value = SG_RUNE_COMPACT_INDEX_NONE;
	fixture->facets[0].vertices.count = 0U;
	fixture->geometry.portal_count = 0U;
	fixture->static_data.facet_annotation_count = 0U;
	fixture->static_data.portal_mechanism_count = 0U;
	fixture->configuration.hook_surface_count = 0U;
	fixture->configuration.hook_surfaces = NULL;
	fixture->visibility.surface_count = 0U;
	fixture->visibility.surfaces = NULL;
	fixture->response_partition.target_patches = NULL;
	fixture->response_partition.target_patch_count = 0U;
	fixture->response_partition.target_vertices = NULL;
	fixture->response_partition.target_vertex_count = 0U;
	fixture->response_partition.response_pairs = NULL;
	fixture->response_partition.response_pair_count = 0U;
	fixture->response_partition.candidate_groups = NULL;
	fixture->response_partition.candidate_group_count = 0U;
	fixture->response_partition.target_endpoint_groups = NULL;
	fixture->response_partition.target_endpoint_group_count = 0U;
	fixture->response_partition.target_endpoint_members = NULL;
	fixture->response_partition.target_endpoint_member_count = 0U;
	fixture->response_partition.seal.target_patch_count = 0U;
	fixture->response_partition.seal.response_pair_count = 0U;
	fixture->response_partition.seal.target_endpoint_group_count = 0U;
	fixture->response_partition.seal.target_endpoint_member_count = 0U;
	fixture->response_partition.seal.unresolved_candidate_group_count = 0U;
	CHECK(SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	SG_RuneCompactMovementFieldsDestroy(fields);
	fields = NULL;
	fixture->geometry.portal_count = saved_geometry_portal_count;
	fixture->static_data.facet_annotation_count = saved_annotation_count;
	fixture->static_data.portal_mechanism_count = saved_portal_mechanism_count;
	fixture->configuration.hook_surface_count = saved_hook_surface_count;
	fixture->configuration.hook_surfaces = saved_hook_surfaces;
	fixture->visibility.surface_count = saved_visibility_surface_count;
	fixture->visibility.surfaces = saved_visibility_surfaces;
	fixture->response_partition.target_patches = saved_target_patches;
	fixture->response_partition.target_patch_count = saved_target_patch_count;
	fixture->response_partition.target_vertices = saved_target_vertices;
	fixture->response_partition.target_vertex_count = saved_target_vertex_count;
	fixture->response_partition.response_pairs = saved_response_pairs;
	fixture->response_partition.response_pair_count = saved_response_pair_count;
	fixture->response_partition.candidate_groups = saved_candidate_groups;
	fixture->response_partition.candidate_group_count = saved_candidate_group_count;
	fixture->response_partition.target_endpoint_groups = saved_target_endpoint_groups;
	fixture->response_partition.target_endpoint_group_count =
		saved_target_endpoint_group_count;
	fixture->response_partition.target_endpoint_members = saved_target_endpoint_members;
	fixture->response_partition.target_endpoint_member_count =
		saved_target_endpoint_member_count;
	fixture->response_partition.seal.target_patch_count =
		saved_seal_target_patch_count;
	fixture->response_partition.seal.response_pair_count =
		saved_seal_response_pair_count;
	fixture->response_partition.seal.target_endpoint_group_count =
		saved_seal_target_endpoint_group_count;
	fixture->response_partition.seal.target_endpoint_member_count =
		saved_seal_target_endpoint_member_count;
	fixture->response_partition.seal.unresolved_candidate_group_count =
		saved_seal_unresolved_candidate_group_count;
	fixture->facets[0].kind = saved_kind;
	fixture->facets[0].portal = saved_facet_portal;
	fixture->facets[0].vertices = saved_vertices;
}

static void CheckCompactResponseGroups(movement_fixture_t *fixture)
{
	sg_rune_compact_response_fragment_t fragments[2];
	sg_rune_compact_response_halfspace_t halfspaces[2];
	sg_rune_compact_response_patch_t patches[2];
	sg_rune_compact_response_endpoint_group_t source_group;
	sg_rune_compact_response_endpoint_group_t target_group;
	sg_rune_compact_response_candidate_group_t candidate;
	uint32_t source_members[2] = { 0U, 1U };
	uint32_t target_members[2] = { 0U, 1U };
	sg_rune_compact_response_partition_view_t saved =
		fixture->response_partition;
	sg_rune_compact_movement_fields_t *fields = NULL;
	sg_rune_compact_movement_fields_view_t view;
	sg_rune_compact_movement_fields_error_t error;
	const sg_rune_compact_movement_hook_target_t *target;

	fragments[0] = fixture->response_fragments[0];
	fragments[1] = fragments[0];
	halfspaces[0] = fixture->response_halfspaces[0];
	halfspaces[1] = halfspaces[0];
	fragments[0].first_halfspace = 0U;
	fragments[1].first_halfspace = 1U;
	patches[0] = fixture->response_patches[0];
	patches[1] = patches[0];
	patches[0].parent_facet.value = SG_RUNE_COMPACT_RESPONSE_INDEX_NONE;
	patches[1].parent_facet.value = SG_RUNE_COMPACT_RESPONSE_INDEX_NONE;
	patches[0].valid_stances = SG_RUNE_STANCE_VALID_ALL;
	patches[1].valid_stances = SG_RUNE_STANCE_VALID_ALL;
	memset(&source_group, 0, sizeof(source_group));
	source_group.member_count = 2U;
	memset(&target_group, 0, sizeof(target_group));
	target_group.member_count = 2U;
	memset(&candidate, 0, sizeof(candidate));
	candidate.classification =
		SG_RUNE_COMPACT_STATIC_VISIBILITY_CONDITIONAL;
	candidate.reason =
		SG_RUNE_COMPACT_STATIC_VISIBILITY_REASON_EXACT_RAY_REQUIRED;
	candidate.requires_exact_ray = 1U;
	fixture->response_partition.source_fragments = fragments;
	fixture->response_partition.source_fragment_count = 2U;
	fixture->response_partition.source_halfspaces = halfspaces;
	fixture->response_partition.source_halfspace_count = 2U;
	fixture->response_partition.target_patches = patches;
	fixture->response_partition.target_patch_count = 2U;
	fixture->response_partition.response_pairs = NULL;
	fixture->response_partition.response_pair_count = 0U;
	fixture->response_partition.candidate_groups = &candidate;
	fixture->response_partition.candidate_group_count = 1U;
	fixture->response_partition.source_endpoint_groups = &source_group;
	fixture->response_partition.source_endpoint_group_count = 1U;
	fixture->response_partition.source_endpoint_members = source_members;
	fixture->response_partition.source_endpoint_member_count = 2U;
	fixture->response_partition.target_endpoint_groups = &target_group;
	fixture->response_partition.target_endpoint_group_count = 1U;
	fixture->response_partition.target_endpoint_members = target_members;
	fixture->response_partition.target_endpoint_member_count = 2U;
	fixture->response_partition.seal.source_fragment_count = 2U;
	fixture->response_partition.seal.target_patch_count = 2U;
	fixture->response_partition.seal.response_pair_count = 0U;
	fixture->response_partition.seal.certified_direct_pair_count = 0U;
	fixture->response_partition.seal.unresolved_candidate_group_count = 1U;
	fixture->response_partition.seal.source_endpoint_member_count = 2U;
	fixture->response_partition.seal.target_endpoint_member_count = 2U;
	CHECK(SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	if (fields != NULL) {
		CHECK(SG_RuneCompactMovementFieldsRead(fields, &view));
		target = FindHookTargetByResponse(&view,
			SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BODY,
			SG_RUNE_STANCE_VALID_STANDING,
			SG_RUNE_COMPACT_RESPONSE_REF_CANDIDATE_GROUP, 0U);
		CHECK(target != NULL && target->source_stances ==
			SG_RUNE_STANCE_VALID_STANDING && target->target_stances ==
			SG_RUNE_STANCE_VALID_STANDING && target->visibility_class ==
			SG_RUNE_MOVEMENT_HOOK_TARGET_CONDITIONAL && target->response.kind ==
			SG_RUNE_COMPACT_RESPONSE_REF_CANDIDATE_GROUP &&
			target->response.index == 0U);
		target = FindHookTargetByResponse(&view,
			SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BODY,
			SG_RUNE_STANCE_VALID_CROUCHING,
			SG_RUNE_COMPACT_RESPONSE_REF_CANDIDATE_GROUP, 0U);
		CHECK(target != NULL && target->source_stances ==
			SG_RUNE_STANCE_VALID_CROUCHING && target->target_stances ==
			SG_RUNE_STANCE_VALID_CROUCHING && target->visibility_class ==
			SG_RUNE_MOVEMENT_HOOK_TARGET_CONDITIONAL && target->response.kind ==
			SG_RUNE_COMPACT_RESPONSE_REF_CANDIDATE_GROUP &&
			target->response.index == 0U);
		SG_RuneCompactMovementFieldsDestroy(fields);
	}
	fixture->response_partition = saved;
}

static void CheckResponseCandidateDecorations(movement_fixture_t *fixture)
{
	sg_rune_compact_response_partition_view_t saved_partition =
		fixture->response_partition;
	sg_rune_compact_response_pair_t saved_pair = fixture->response_pairs[0];
	sg_rune_compact_response_candidate_group_t saved_candidate =
		fixture->response_candidates[0];
	sg_static_visibility_occluder_t *saved_occluders =
		fixture->visibility.occluders;
	uint32_t saved_occluder_count = fixture->visibility.occluder_count;
	sg_rune_compact_movement_fields_t *fields = NULL;
	sg_rune_compact_movement_fields_view_t view;
	sg_rune_compact_movement_fields_error_t error;
	const sg_rune_compact_movement_hook_target_t *target;

	/* A certified direct fact decorates, but does not replace, the exact-ray
	 * candidate's conditional visibility semantics. */
	CHECK(SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	if (fields != NULL && SG_RuneCompactMovementFieldsRead(fields, &view)) {
		target = FindHookTargetByResponse(&view,
			SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BODY,
			SG_RUNE_STANCE_VALID_STANDING,
			SG_RUNE_COMPACT_RESPONSE_REF_CERTIFIED_FACT, 0U);
		CHECK(target != NULL && target->visibility_class ==
			SG_RUNE_MOVEMENT_HOOK_TARGET_CONDITIONAL);
	}
	SG_RuneCompactMovementFieldsDestroy(fields);
	fields = NULL;

	fixture->response_pairs[0].classification = SG_STATIC_VISIBILITY_VISIBLE;
	CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	CHECK(error.code == SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_VISIBILITY);
	fixture->response_pairs[0] = saved_pair;
	fixture->response_pairs[0].relation_flags |=
		SG_RUNE_COMPACT_STATIC_RELATION_PORTAL_CROSSING;
	CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	CHECK(error.code == SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_VISIBILITY);
	fixture->response_pairs[0] = saved_pair;
	fixture->response_candidates[0].reason =
		SG_RUNE_COMPACT_STATIC_VISIBILITY_REASON_MOVING_SUBMODEL;
	CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	CHECK(error.code == SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_VISIBILITY);
	fixture->response_candidates[0] = saved_candidate;

	/* Static impact is the other legal certificate decoration of the same
	 * conditional candidate. */
	fixture->visibility.occluders = fixture->occluders;
	fixture->visibility.occluder_count = 1U;
	fixture->response_partition.static_occluder_count = 1U;
	fixture->response_partition.seal.static_occluder_count = 1U;
	fixture->response_partition.seal.certified_direct_pair_count = 0U;
	fixture->response_partition.seal.certified_static_impact_pair_count = 1U;
	fixture->response_pairs[0].first_hit_occluder = 0U;
	fixture->response_pairs[0].certificate =
		SG_RUNE_COMPACT_RESPONSE_CERTIFIED_STATIC_IMPACT;
	fixture->response_pairs[0].relation_flags =
		SG_RUNE_COMPACT_STATIC_RELATION_OCCLUDED_IMPACT;
	CHECK(SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	if (fields != NULL && SG_RuneCompactMovementFieldsRead(fields, &view)) {
		target = FindHookTargetByResponse(&view,
			SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BODY,
			SG_RUNE_STANCE_VALID_STANDING,
			SG_RUNE_COMPACT_RESPONSE_REF_CERTIFIED_FACT, 0U);
		CHECK(target != NULL && target->visibility_class ==
			SG_RUNE_MOVEMENT_HOOK_TARGET_CONDITIONAL);
	}
	SG_RuneCompactMovementFieldsDestroy(fields);
	fields = NULL;

	fixture->response_pairs[0].classification = SG_STATIC_VISIBILITY_OCCLUDED;
	CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	CHECK(error.code == SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_VISIBILITY);
	fixture->response_pairs[0].classification = SG_STATIC_VISIBILITY_CONDITIONAL;
	fixture->response_pairs[0].relation_flags =
		SG_RUNE_COMPACT_STATIC_RELATION_DIRECT;
	CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	CHECK(error.code == SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_VISIBILITY);
	fixture->response_pairs[0].relation_flags =
		SG_RUNE_COMPACT_STATIC_RELATION_OCCLUDED_IMPACT;
	fixture->response_pairs[0].requires_exact_ray = 0U;
	CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	CHECK(error.code == SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_VISIBILITY);

	fixture->response_pairs[0] = saved_pair;
	fixture->response_candidates[0] = saved_candidate;
	fixture->response_partition = saved_partition;
	fixture->visibility.occluders = saved_occluders;
	fixture->visibility.occluder_count = saved_occluder_count;
}

static void CheckHookResponseIsolation(movement_fixture_t *fixture)
{
	sg_rune_compact_response_candidate_group_t candidate;
	sg_rune_compact_response_pair_t mixed_pairs[2];
	sg_rune_compact_response_partition_view_t saved =
		fixture->response_partition;
	sg_rune_compact_response_pair_t saved_pair = fixture->response_pairs[0];
	sg_static_visibility_occluder_t *saved_occluders =
		fixture->visibility.occluders;
	uint32_t saved_occluder_count = fixture->visibility.occluder_count;
	sg_rune_compact_movement_fields_t *fields = NULL;
	sg_rune_compact_movement_fields_view_t view;
	sg_rune_compact_movement_fields_error_t error;
	const sg_rune_movement_capability_t *standing;
	const sg_rune_movement_capability_t *crouching;
	const sg_rune_compact_movement_hook_target_t *target;
	uint32_t grounded_release_count;
	uint32_t airborne_release_count;
	uint32_t in_flight_release_count;
	uint32_t attached_release_count;
	uint32_t capability_index;

	memset(&candidate, 0, sizeof(candidate));
	candidate.source_group = 0U;
	candidate.target_group = 0U;
	candidate.classification =
		SG_RUNE_COMPACT_STATIC_VISIBILITY_CONDITIONAL;
	candidate.reason =
		SG_RUNE_COMPACT_STATIC_VISIBILITY_REASON_EXACT_RAY_REQUIRED;
	candidate.requires_exact_ray = 1U;
	fixture->response_partition.candidate_groups = &candidate;
	fixture->response_partition.candidate_group_count = 1U;
	fixture->response_partition.seal.unresolved_candidate_group_count = 1U;
	mixed_pairs[0] = saved_pair;
	mixed_pairs[1] = saved_pair;
	mixed_pairs[1].first_hit_occluder = 0U;
	mixed_pairs[1].certificate =
		SG_RUNE_COMPACT_RESPONSE_CERTIFIED_STATIC_IMPACT;
	mixed_pairs[1].relation_flags =
		SG_RUNE_COMPACT_STATIC_RELATION_OCCLUDED_IMPACT;
	fixture->response_partition.response_pairs = mixed_pairs;
	fixture->response_partition.response_pair_count = 2U;
	fixture->response_partition.seal.response_pair_count = 2U;
	fixture->response_partition.seal.certified_direct_pair_count = 1U;
	fixture->response_partition.seal.certified_static_impact_pair_count = 1U;
	fixture->response_partition.static_occluder_count = 1U;
	fixture->response_partition.seal.static_occluder_count = 1U;
	fixture->visibility.occluders = fixture->occluders;
	fixture->visibility.occluder_count = 1U;
	memset(&fixture->occluders[0], 0, sizeof(fixture->occluders[0]));
	fixture->occluders[0].model = SG_HOST_COLLISION_MODEL_WORLD;
	fixture->occluders[0].brush = 2U;
	fixture->occluders[0].contents = SG_HOST_CONTENTS_SOLID;
	CHECK(SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	if (fields != NULL && SG_RuneCompactMovementFieldsRead(fields, &view)) {
		standing = FindFieldForStance(&view, 0U,
			SG_RUNE_COMPACT_INDEX_NONE, SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BODY,
			SG_RUNE_STANCE_VALID_STANDING);
		crouching = FindFieldForStance(&view, 0U,
			SG_RUNE_COMPACT_INDEX_NONE, SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BODY,
			SG_RUNE_STANCE_VALID_CROUCHING);
		CHECK(standing != NULL && crouching != NULL);
		CHECK(CountHookTargets(&view,
			SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BODY,
			SG_RUNE_STANCE_VALID_STANDING,
			SG_RUNE_MOVEMENT_HOOK_TARGET_CONDITIONAL,
			SG_RUNE_COMPACT_RESPONSE_REF_CERTIFIED_FACT) == 2U);
		CHECK(CountHookTargets(&view,
			SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BODY,
			SG_RUNE_STANCE_VALID_STANDING,
			SG_RUNE_MOVEMENT_HOOK_TARGET_CONDITIONAL,
			SG_RUNE_COMPACT_RESPONSE_REF_CANDIDATE_GROUP) == 1U);
		CHECK(CountHookTargets(&view,
			SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BODY,
			SG_RUNE_STANCE_VALID_CROUCHING,
			SG_RUNE_MOVEMENT_HOOK_TARGET_CONDITIONAL,
			SG_RUNE_COMPACT_RESPONSE_REF_CERTIFIED_FACT) == 2U);
		CHECK(CountHookTargets(&view,
			SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BODY,
			SG_RUNE_STANCE_VALID_CROUCHING,
			SG_RUNE_MOVEMENT_HOOK_TARGET_CONDITIONAL,
			SG_RUNE_COMPACT_RESPONSE_REF_CANDIDATE_GROUP) == 1U);
		grounded_release_count = 0U;
		airborne_release_count = 0U;
		in_flight_release_count = 0U;
		attached_release_count = 0U;
		for (capability_index = 0U;
			capability_index < view.capability_count; capability_index++) {
			const sg_rune_movement_capability_t *capability =
				&view.capabilities[capability_index];
			uint32_t fiber_offset;

			if (capability->kind !=
					SG_RUNE_MOVEMENT_CAPABILITY_HOOK_RELEASE ||
				capability->source_stances !=
					SG_RUNE_STANCE_VALID_STANDING)
				continue;
			CHECK(capability->fibers.count == 4U);
			for (fiber_offset = 0U;
				fiber_offset < capability->fibers.count; fiber_offset++) {
				const sg_rune_compact_movement_fiber_t *fiber =
					&view.fibers[capability->fibers.first + fiber_offset];
				const sg_rune_compact_movement_state_t *source =
					&view.states[fiber->source_state.value];

				CHECK((fiber->state_variables &
					SG_RUNE_MOVEMENT_STATE_SUPPORT) != 0U);
				if (source->support == SG_RUNE_MOVEMENT_SUPPORT_STATIC)
					grounded_release_count++;
				else if (source->support == SG_RUNE_MOVEMENT_SUPPORT_NONE &&
					(source->flags & SG_RUNE_MOVEMENT_STATE_AIRBORNE) != 0U)
					airborne_release_count++;
				if (source->hook_phase == SG_HOST_HOOK_IN_FLIGHT) {
					in_flight_release_count++;
					CHECK(fiber->hook_targets.count == 0U);
				} else if (source->hook_phase == SG_HOST_HOOK_ATTACHED) {
					attached_release_count++;
					CHECK(fiber->hook_targets.count != 0U);
				}
			}
		}
		CHECK(grounded_release_count == airborne_release_count);
		CHECK(in_flight_release_count == attached_release_count);
		CHECK(grounded_release_count != 0U && in_flight_release_count != 0U);
	}
	SG_RuneCompactMovementFieldsDestroy(fields);
	fields = NULL;

	fixture->response_partition = saved;
	fixture->response_pairs[0].source_valid_stances =
		SG_RUNE_STANCE_VALID_STANDING;
	fixture->response_pairs[0].target_valid_stances =
		SG_RUNE_STANCE_VALID_STANDING;
	CHECK(SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	if (fields != NULL && SG_RuneCompactMovementFieldsRead(fields, &view)) {
		crouching = FindFieldForStance(&view, 0U,
			SG_RUNE_COMPACT_INDEX_NONE, SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BODY,
			SG_RUNE_STANCE_VALID_CROUCHING);
		target = FindHookTargetByResponse(&view,
			SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BODY,
			SG_RUNE_STANCE_VALID_STANDING,
			SG_RUNE_COMPACT_RESPONSE_REF_CERTIFIED_FACT, 0U);
		CHECK(target != NULL && target->visibility_class ==
			SG_RUNE_MOVEMENT_HOOK_TARGET_CONDITIONAL && target->response.kind ==
			SG_RUNE_COMPACT_RESPONSE_REF_CERTIFIED_FACT &&
			target->source_stances == SG_RUNE_STANCE_VALID_STANDING);
		CHECK(crouching != NULL);
		CHECK(CountHookTargets(&view,
			SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BODY,
			SG_RUNE_STANCE_VALID_CROUCHING,
			SG_RUNE_MOVEMENT_HOOK_TARGET_CONDITIONAL,
			SG_RUNE_COMPACT_RESPONSE_REF_CERTIFIED_FACT) == 0U);
	}
	SG_RuneCompactMovementFieldsDestroy(fields);
	fixture->response_pairs[0] = saved_pair;
	fixture->response_partition = saved;
	fixture->visibility.occluders = saved_occluders;
	fixture->visibility.occluder_count = saved_occluder_count;
}

static void CheckPatchSourceAuthority(movement_fixture_t *fixture)
{
	sg_rune_compact_movement_fields_t *fields = NULL;
	sg_rune_compact_movement_fields_error_t error;
	sg_rune_compact_response_patch_t saved_patch = fixture->response_patches[0];
	sg_rune_compact_source_surface_t saved_source = fixture->source_surfaces[0];
	uint32_t saved_hook_model = fixture->hook_surfaces[0].model;
	uint32_t saved_hook_flags = fixture->hook_surfaces[0].flags;
	uint32_t saved_visibility_model = fixture->visibility_surfaces[0].model;
	uint32_t saved_visibility_flags = fixture->visibility_surfaces[0].flags;

	fixture->response_patches[0].parent_facet.value =
		SG_RUNE_COMPACT_RESPONSE_INDEX_NONE;
	fixture->response_patches[0].boundary_incidences.first = 0U;
	fixture->response_patches[0].boundary_incidences.count = 0U;
	CHECK(SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	SG_RuneCompactMovementFieldsDestroy(fields);
	fields = NULL;
	fixture->response_patches[0].boundary_incidences.count = 1U;
	CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	fixture->response_patches[0].boundary_incidences.count = 0U;
	fixture->hook_surfaces[0].model = 1U;
	fixture->hook_surfaces[0].flags = SG_CONFIGURATION_HOOK_SURFACE_HOOKABLE |
		SG_CONFIGURATION_HOOK_SURFACE_MOVING_MODEL;
	fixture->visibility_surfaces[0].model = 1U;
	fixture->visibility_surfaces[0].flags =
		SG_CONFIGURATION_HOOK_SURFACE_HOOKABLE |
		SG_CONFIGURATION_HOOK_SURFACE_MOVING_MODEL;
	fixture->source_surfaces[0].source.model = 1U;
	fixture->source_surfaces[0].frame =
		SG_RUNE_COMPACT_SOURCE_SURFACE_MODEL_LOCAL;
	fixture->response_patches[0].model = 1U;
	fixture->response_patches[0].source_frame =
		SG_RUNE_COMPACT_SOURCE_SURFACE_MODEL_LOCAL;
	fixture->response_patches[0].flags =
		SG_RUNE_COMPACT_RESPONSE_PATCH_HOOKABLE |
		SG_RUNE_COMPACT_RESPONSE_PATCH_MOVING;
	CHECK(SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	SG_RuneCompactMovementFieldsDestroy(fields);
	fixture->response_patches[0] = saved_patch;
	fixture->source_surfaces[0] = saved_source;
	fixture->hook_surfaces[0].model = saved_hook_model;
	fixture->hook_surfaces[0].flags = saved_hook_flags;
	fixture->visibility_surfaces[0].model = saved_visibility_model;
	fixture->visibility_surfaces[0].flags = saved_visibility_flags;
}

static void CheckInvalidInputs(movement_fixture_t *fixture)
{
	sg_rune_compact_movement_fields_t *fields = NULL;
	sg_rune_compact_movement_fields_t *sentinel =
		(sg_rune_compact_movement_fields_t *)(uintptr_t)0x1234U;
	sg_rune_compact_movement_fields_error_t error;
	uint32_t saved_cell_count = fixture->geometry.cell_count;
	uint32_t saved_facet_count = fixture->geometry.facet_count;
	uint32_t saved_portal_count = fixture->geometry.portal_count;
	uint32_t saved_incidence = fixture->portals[0].negative_incidence.value;
	uint32_t saved_region_cell = fixture->regions[1].cell;
	float saved_airaccelerate = fixture->host.airaccelerate;
	float saved_air_acceleration =
		fixture->host.static_identity.physics.air_acceleration;
	float saved_standing_max_x =
		fixture->host.static_identity.standing_hull.maxs.value[0];

	fixture->geometry.cell_count = SG_RUNE_COMPACT_MAX_CELLS + 1U;
	fields = sentinel;
	CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	CHECK(error.code == SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_ARGUMENT);
	CHECK(fields == sentinel);
	fixture->geometry.cell_count = saved_cell_count;
	fixture->geometry.facet_count = SG_RUNE_COMPACT_MAX_FACETS + 1U;
	fields = sentinel;
	CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	CHECK(error.code == SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_ARGUMENT);
	CHECK(fields == sentinel);
	fixture->geometry.facet_count = saved_facet_count;
	fixture->geometry.portal_count = SG_RUNE_COMPACT_MAX_PORTALS + 1U;
	fields = sentinel;
	CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	CHECK(error.code == SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_ARGUMENT);
	CHECK(fields == sentinel);
	fixture->geometry.portal_count = saved_portal_count;
	fixture->portals[0].negative_incidence.value = INCIDENCE_COUNT;
	fields = sentinel;
	CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	CHECK(error.code == SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_GEOMETRY);
	CHECK(fields == sentinel);
	fixture->portals[0].negative_incidence.value = saved_incidence;
	fixture->regions[1].cell = 0U;
	fields = sentinel;
	CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	CHECK(error.code == SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_VISIBILITY);
	CHECK(fields == sentinel);
	fixture->regions[1].cell = saved_region_cell;
	fixture->host.airaccelerate = 1.5f;
	fixture->host.static_identity.physics.air_acceleration =
		SG_HOST_ENGINE_GROUND_ACCELERATION;
	SyncFixtureIdentity(fixture);
	fields = sentinel;
	CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	CHECK(error.code == SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_HOST_LAW);
	CHECK(fields == sentinel);
	fixture->host.airaccelerate = 1.0f;
	fields = NULL;
	CHECK(SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	SG_RuneCompactMovementFieldsDestroy(fields);
	fields = sentinel;
	fixture->host.static_identity.physics.air_acceleration = 1.0f;
	SyncFixtureIdentity(fixture);
	CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	CHECK(error.code == SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_HOST_LAW);
	CHECK(fields == sentinel);
	fixture->host.airaccelerate = saved_airaccelerate;
	fixture->host.static_identity.physics.air_acceleration =
		saved_air_acceleration;
	SyncFixtureIdentity(fixture);
	fixture->host.static_identity.standing_hull.maxs.value[0] =
		fixture->host.static_identity.standing_hull.mins.value[0];
	fields = sentinel;
	CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	CHECK(error.code == SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_INVALID_ARGUMENT);
	CHECK(fields == sentinel);
	fixture->host.static_identity.standing_hull.maxs.value[0] =
		saved_standing_max_x;
}

#if defined(SG_RUNE_COMPACT_MOVEMENT_FIELDS_TEST_WRAP_CALLOC)
static void CheckAllocationFailures(movement_fixture_t *fixture)
{
	sg_rune_compact_movement_fields_t *fields = NULL;
	sg_rune_compact_movement_fields_error_t error;
	uint64_t allocation_count;
	uint64_t index;

	fail_calloc_after = -1;
	calloc_calls = 0U;
	CHECK(SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields, &error));
	allocation_count = calloc_calls;
	SG_RuneCompactMovementFieldsDestroy(fields);
	if (allocation_count == 0U)
		return;
	CHECK(allocation_count != 0U);
	for (index = 0U; index < allocation_count; index++) {
		fields = (sg_rune_compact_movement_fields_t *)(uintptr_t)0x5678U;
		fail_calloc_after = (int)index;
		CHECK(!SG_RuneCompactMovementFieldsBuild(&fixture->input, &fields,
			&error));
		CHECK(fields == (sg_rune_compact_movement_fields_t *)(uintptr_t)0x5678U);
		CHECK(error.code ==
			SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_OUT_OF_MEMORY);
	}
	fail_calloc_after = -1;
}
#endif

int main(void)
{
	movement_fixture_t fixture;
	sg_rune_compact_movement_fields_t *fields = NULL;
	sg_rune_compact_movement_fields_view_t view;
	sg_rune_compact_movement_fields_error_t error;

	InitFixture(&fixture);
	CHECK(SG_RuneCompactMovementFieldsBuild(&fixture.input, &fields, &error));
	CHECK(error.code == SG_RUNE_COMPACT_MOVEMENT_FIELDS_ERROR_NONE);
	if (fields != NULL) {
		CHECK(SG_RuneCompactMovementFieldsRead(fields, &view));
		CHECK(SG_RuneCompactAnalyticValidate(&view.analytic, NULL));
		CheckBasicOutput(&fixture, &view);
		CheckGenericHookTargetKinds(&view);
		CheckAcceptedModelShape(&fixture, &view);
		CheckEvaluation(&fixture, &view);
		CheckDirectionalFields(&fixture);
		CheckHookLadder(&fixture);
		CheckHookChronology(&fixture);
		CheckIdentityBinding(&fixture);
		CheckMechanismDiscontinuities(&fixture);
		CheckProfilePruning(&fixture, view.analytic.function_count);
	}
	SG_RuneCompactMovementFieldsDestroy(fields);
	CheckGravity(&fixture);
	CheckDeterminism(&fixture);
	CheckPmovePublicationEnabled();
	CheckCrouchOnlyPassage();
	CheckConditionalAndSky(&fixture);
	CheckCompactResponseGroups(&fixture);
	CheckResponseCandidateDecorations(&fixture);
	CheckHookResponseIsolation(&fixture);
	CheckPatchSourceAuthority(&fixture);
	CheckConstraintFacet(&fixture);
	CheckInvalidInputs(&fixture);
	CheckAngularPortalAuthority();
	CheckContinuousRotatorField();
	CheckButtonPortalAuthority();
	CheckMultiRootPortalAuthority();
	CheckCanonicalStaticTransitionOrder();
#if defined(SG_RUNE_COMPACT_MOVEMENT_FIELDS_TEST_WRAP_CALLOC)
	CheckAllocationFailures(&fixture);
#endif
	if (failures != 0)
		return EXIT_FAILURE;
	puts("sg_rune_compact_movement_fields: ok");
	return EXIT_SUCCESS;
}
