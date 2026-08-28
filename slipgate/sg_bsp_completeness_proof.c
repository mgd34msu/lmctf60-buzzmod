#include "sg_bsp_completeness_internal.h"

#include <math.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static int VectorEqual(const sg_rune_vec3_t *left,
	const sg_rune_vec3_t *right)
{
	return left->value[0] == right->value[0] &&
		left->value[1] == right->value[1] &&
		left->value[2] == right->value[2];
}

static int HullEqual(const sg_rune_hull_profile_t *left,
	const sg_rune_hull_profile_t *right)
{
	return VectorEqual(&left->mins, &right->mins) &&
		VectorEqual(&left->maxs, &right->maxs);
}

static int PhysicsEqual(const sg_rune_physics_parameters_t *left,
	const sg_rune_physics_parameters_t *right)
{
	return left->gravity == right->gravity &&
		left->ground_acceleration == right->ground_acceleration &&
		left->air_acceleration == right->air_acceleration &&
		left->water_acceleration == right->water_acceleration &&
		left->hook_acceleration == right->hook_acceleration &&
		left->external_acceleration == right->external_acceleration &&
		left->water_drag == right->water_drag &&
		left->max_velocity == right->max_velocity &&
		left->frame_ms == right->frame_ms &&
		left->substep_ms == right->substep_ms;
}

static int IdentityEqual(const sg_rune_model_identity_t *left,
	const sg_rune_model_identity_t *right)
{
	return left->bsp_content_id == right->bsp_content_id &&
		left->entity_semantics_id == right->entity_semantics_id &&
		left->physics_abi_id == right->physics_abi_id &&
		left->source_set_identity == right->source_set_identity &&
		left->schema_id == right->schema_id &&
		left->producer_identity == right->producer_identity &&
		HullEqual(&left->standing_hull, &right->standing_hull) &&
		HullEqual(&left->crouching_hull, &right->crouching_hull) &&
		PhysicsEqual(&left->physics, &right->physics);
}

static int BoundsAreCanonical(const sg_rune_bounds_t *bounds)
{
	uint32_t axis;

	for (axis = 0; axis < 3U; axis++)
		if (!isfinite(bounds->mins.value[axis]) ||
			!isfinite(bounds->maxs.value[axis]) ||
			bounds->mins.value[axis] != SG_CONFIGURATION_PMOVE_ORIGIN_MIN ||
			bounds->maxs.value[axis] != SG_CONFIGURATION_PMOVE_ORIGIN_MAX)
			return 0;
	return 1;
}

static int WorldPointersPresent(const sg_bsp_world_t *world)
{
	return world && world->model_count != 0U && world->models &&
		(world->plane_count == 0U || world->planes) &&
		(world->node_count == 0U || world->nodes) &&
		(world->leaf_count == 0U || world->leaves) &&
		(world->leaf_brush_count == 0U || world->leaf_brushes) &&
		(world->brush_count == 0U || world->brushes) &&
		(world->brush_side_count == 0U || world->brush_sides);
}

int SG_BspCompletenessProve(const sg_host_collision_authority_t *authority,
	const sg_configuration_space_t *space,
	sg_bsp_completeness_result_t *result_out)
{
	sg_bsp_proof_context_t proof;
	int success = 0;

	memset(&proof, 0, sizeof(proof));
	proof.result.record = SG_CONFIGURATION_INDEX_NONE;
	if (!result_out || !authority || !space ||
		!WorldPointersPresent(authority->world) ||
		!IdentityEqual(&authority->identity, &space->identity) ||
		!BoundsAreCanonical(&space->domain) ||
		(space->cell_count != 0U && !space->cells) ||
		(space->face_count != 0U && !space->faces) ||
		(space->vertex_count != 0U && !space->vertices) ||
		(space->portal_count != 0U && !space->portals))
	{
		if (result_out)
		{
			memset(result_out, 0, sizeof(*result_out));
			result_out->code = SG_BSP_COMPLETENESS_INVALID_ARGUMENT;
			result_out->record = SG_CONFIGURATION_INDEX_NONE;
		}
		return 0;
	}
	proof.authority = authority;
	proof.space = space;
	proof.result.represented_cells = space->cell_count;
	proof.result.represented_portals = space->portal_count;
	if (!SG_BspProofBuildExpected(&proof))
	{
		if (proof.result.code == SG_BSP_COMPLETENESS_OK)
			SG_BspProofFail(&proof, SG_BSP_COMPLETENESS_OUT_OF_MEMORY, 0);
		goto done;
	}
	if (!SG_BspProofAuditStates(&proof))
	{
		if (proof.result.code == SG_BSP_COMPLETENESS_OK)
			SG_BspProofFail(&proof, SG_BSP_COMPLETENESS_OUT_OF_MEMORY, 0);
		goto done;
	}
	if (!SG_BspProofAuditCoverage(&proof))
	{
		if (proof.result.code == SG_BSP_COMPLETENESS_OK)
			SG_BspProofFail(&proof, SG_BSP_COMPLETENESS_OUT_OF_MEMORY, 0);
		goto done;
	}
	if (!SG_BspProofAuditPortals(&proof))
	{
		if (proof.result.code == SG_BSP_COMPLETENESS_OK)
			SG_BspProofFail(&proof, SG_BSP_COMPLETENESS_OUT_OF_MEMORY, 0);
		goto done;
	}
	success = 1;

done:
	if (proof.blockers)
	{
		uint32_t bucket;

		for (bucket = 0; bucket < proof.blocker_bucket_count; bucket++)
			SG_BspProofFreeRegions(&proof.blockers[bucket]);
		free(proof.blockers);
	}
	SG_BspProofFreeRegions(&proof.expected);
	*result_out = proof.result;
	return success;
}

const char *SG_BspCompletenessCodeString(sg_bsp_completeness_code_t code)
{
	switch (code)
	{
	case SG_BSP_COMPLETENESS_OK: return "ok";
	case SG_BSP_COMPLETENESS_INVALID_ARGUMENT: return "invalid argument";
	case SG_BSP_COMPLETENESS_INVALID_WORLD: return "invalid BSP world";
	case SG_BSP_COMPLETENESS_INVALID_CELL: return "invalid represented cell";
	case SG_BSP_COMPLETENESS_INVALID_PORTAL:
		return "invalid represented portal";
	case SG_BSP_COMPLETENESS_OMITTED_CELL: return "omitted valid cell volume";
	case SG_BSP_COMPLETENESS_INVENTED_CELL:
		return "invented player-origin volume";
	case SG_BSP_COMPLETENESS_OMITTED_PORTAL:
		return "omitted valid portal";
	case SG_BSP_COMPLETENESS_INVENTED_PORTAL: return "invented portal";
	case SG_BSP_COMPLETENESS_HOST_DISAGREEMENT:
		return "host collision disagreement";
	case SG_BSP_COMPLETENESS_OVERFLOW: return "proof counter overflow";
	case SG_BSP_COMPLETENESS_OUT_OF_MEMORY: return "out of memory";
	default: return "unknown BSP completeness result";
	}
}
