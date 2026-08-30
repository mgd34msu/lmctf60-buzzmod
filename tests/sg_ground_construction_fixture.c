#include "sg_ground_construction_fixture.h"

#include <stdlib.h>
#include <string.h>

struct sg_host_law_construction_s
{
	const sg_host_collision_authority_t *authority;
	sg_host_pmove_function_t pmove;
};

static sg_host_law_result_t FixtureResult(sg_host_law_status_t status,
	sg_host_law_field_t field)
{
	sg_host_law_result_t result;

	memset(&result, 0, sizeof(result));
	result.status = status;
	result.field = field;
	result.element = SG_HOST_LAW_ELEMENT_NONE;
	return result;
}

sg_host_law_construction_t *SG_TestGroundConstructionCreate(
	const sg_host_collision_authority_t *authority,
	sg_host_pmove_function_t pmove)
{
	sg_host_law_construction_t *construction;

	if (!authority || !authority->world || !pmove)
		return NULL;
	construction = calloc(1U, sizeof(*construction));
	if (!construction)
		return NULL;
	construction->authority = authority;
	construction->pmove = pmove;
	return construction;
}

void SG_TestGroundConstructionDestroy(
	sg_host_law_construction_t *construction)
{
	free(construction);
}

sg_host_law_result_t SG_HostLawConstructionCurrent(
	const sg_host_law_construction_t *construction)
{
	return FixtureResult(construction && construction->authority &&
		construction->authority->world && construction->pmove ? SG_HOST_LAW_OK :
		SG_HOST_LAW_CORRUPT_PUBLICATION, SG_HOST_LAW_FIELD_NONE);
}

sg_host_law_result_t SG_HostLawConstructionRead(
	const sg_host_law_construction_t *construction,
	sg_host_law_construction_view_t *view_out)
{
	const sg_rune_model_identity_t *identity;

	if (!view_out)
		return FixtureResult(SG_HOST_LAW_INVALID_ARGUMENT,
			SG_HOST_LAW_FIELD_NONE);
	memset(view_out, 0, sizeof(*view_out));
	if (SG_HostLawConstructionCurrent(construction).status != SG_HOST_LAW_OK)
		return FixtureResult(SG_HOST_LAW_CORRUPT_PUBLICATION,
			SG_HOST_LAW_FIELD_NONE);
	identity = &construction->authority->identity;
	view_out->version = SG_HOST_LAW_PUBLICATION_VERSION;
	view_out->current = 1U;
	view_out->level_generation = 1U;
	view_out->host_static_identity.bsp_identity =
		construction->authority->content_identity;
	view_out->host_static_identity.bsp_bytes =
		(uint64_t)construction->authority->world->source_size;
	view_out->host_static_identity.engine_checksum =
		construction->authority->world->engine_checksum;
	view_out->host_static_identity.entity_crc32 = 1U;
	view_out->host_static_identity.host_physics_epoch = SG_HOST_PHYSICS_EPOCH;
	view_out->host_static_identity.physics_abi_id = identity->physics_abi_id;
	view_out->host_static_identity.standing_hull = identity->standing_hull;
	view_out->host_static_identity.crouching_hull = identity->crouching_hull;
	view_out->host_static_identity.physics = identity->physics;
	view_out->laws.version = SG_HOST_LAW_PUBLICATION_VERSION;
	view_out->laws.collision_law_id = UINT64_C(1);
	view_out->laws.pmove_law_id = UINT64_C(2);
	view_out->laws.gravity_law_id = UINT64_C(3);
	view_out->laws.pmove_abi.version = SG_HOST_ENGINE_PMOVE_ABI_VERSION;
	view_out->laws.pmove_abi.identity = identity->physics_abi_id;
	view_out->laws.pmove_abi.substep_ms = identity->physics.substep_ms;
	view_out->laws.pmove_behavior_fingerprint = identity->physics_abi_id;
	return FixtureResult(SG_HOST_LAW_OK, SG_HOST_LAW_FIELD_NONE);
}

sg_host_law_result_t SG_HostLawConstructionClassifyPose(
	const sg_host_law_construction_t *construction,
	const sg_host_collision_scene_t *scene, const float origin[3],
	sg_rune_stance_t stance, sg_host_collision_pose_t *pose_out)
{
	if (SG_HostLawConstructionCurrent(construction).status != SG_HOST_LAW_OK ||
		!SG_HostCollisionClassifyPose(construction->authority, scene, origin,
			stance, pose_out))
		return FixtureResult(SG_HOST_LAW_EVALUATION_FAILED,
			SG_HOST_LAW_FIELD_COLLISION_LAW);
	return FixtureResult(SG_HOST_LAW_OK, SG_HOST_LAW_FIELD_NONE);
}

sg_host_law_result_t SG_HostLawConstructionPmove(
	const sg_host_law_construction_t *construction,
	const sg_host_collision_scene_t *scene,
	const sg_host_pmove_request_t *request,
	sg_host_pmove_result_t *result_out, sg_host_pmove_error_t *error_out)
{
	if (SG_HostLawConstructionCurrent(construction).status != SG_HOST_LAW_OK ||
		!SG_HostPmoveEvaluateFrame(construction->authority, scene,
			construction->pmove, request, result_out, error_out))
		return FixtureResult(SG_HOST_LAW_EVALUATION_FAILED,
			SG_HOST_LAW_FIELD_PMOVE_LAW);
	return FixtureResult(SG_HOST_LAW_OK, SG_HOST_LAW_FIELD_NONE);
}

sg_host_law_result_t SG_HostLawConstructionReplayFrame(
	const sg_host_law_construction_t *construction,
	const sg_host_collision_scene_t *scene,
	const sg_host_pmove_request_t *request,
	const sg_host_pmove_replay_workspace_t *workspace,
	sg_host_pmove_replay_t *replay_out, sg_host_pmove_error_t *error_out)
{
	if (SG_HostLawConstructionCurrent(construction).status != SG_HOST_LAW_OK ||
		!SG_HostPmoveReplayFrame(construction->authority, scene,
			construction->pmove, request, workspace, replay_out, error_out))
		return FixtureResult(SG_HOST_LAW_EVALUATION_FAILED,
			SG_HOST_LAW_FIELD_PMOVE_LAW);
	return FixtureResult(SG_HOST_LAW_OK, SG_HOST_LAW_FIELD_NONE);
}
