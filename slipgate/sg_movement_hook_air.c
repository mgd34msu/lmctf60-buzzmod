#include "sg_movement_hook_air.h"

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
	return left && right && left->bsp_content_id == right->bsp_content_id &&
		left->entity_semantics_id == right->entity_semantics_id &&
		left->physics_abi_id == right->physics_abi_id &&
		left->source_set_identity == right->source_set_identity &&
		left->schema_id == right->schema_id &&
		left->producer_identity == right->producer_identity &&
		HullEqual(&left->standing_hull, &right->standing_hull) &&
		HullEqual(&left->crouching_hull, &right->crouching_hull) &&
		PhysicsEqual(&left->physics, &right->physics);
}

static void ErrorInitialize(sg_movement_hook_air_error_t *error)
{
	memset(error, 0, sizeof(*error));
	error->source_index = SG_MOVEMENT_HOOK_AIR_INDEX_NONE;
	error->bsp_code = SG_BSP_COMPLETENESS_OK;
	error->semantics_code = SG_CONFIGURATION_SEMANTICS_AUDIT_OK;
}

static int SourcesPresent(const sg_movement_hook_air_sources_t *sources)
{
	return sources && sources->collision && sources->collision->world &&
		sources->configuration && sources->semantics;
}

static int SourceIdentitiesMatch(const sg_movement_hook_air_sources_t *sources)
{
	return IdentityEqual(&sources->collision->identity,
			&sources->configuration->identity) &&
		IdentityEqual(&sources->collision->identity,
			&sources->semantics->identity);
}

static int AuditPrerequisites(const sg_movement_hook_air_sources_t *sources,
	sg_movement_hook_air_error_t *error)
{
	sg_bsp_completeness_result_t bsp;
	sg_configuration_semantics_audit_result_t semantics;

	memset(&bsp, 0, sizeof(bsp));
	if (!SG_BspCompletenessProve(sources->collision, sources->configuration,
			&bsp) || bsp.code != SG_BSP_COMPLETENESS_OK)
	{
		error->code = SG_MOVEMENT_HOOK_AIR_ERROR_INCOMPLETE_BSP_PROOF;
		error->source_index = bsp.record;
		error->bsp_code = bsp.code;
		return 0;
	}
	memset(&semantics, 0, sizeof(semantics));
	if (!SG_ConfigurationSemanticsAudit(sources->collision,
			sources->configuration, sources->semantics, &semantics) ||
		semantics.code != SG_CONFIGURATION_SEMANTICS_AUDIT_OK)
	{
		error->code = SG_MOVEMENT_HOOK_AIR_ERROR_INCOMPLETE_SEMANTICS_PROOF;
		error->source_index = semantics.record;
		error->semantics_code = semantics.code;
		return 0;
	}
	return 1;
}

int SG_MovementHookAirBuild(const sg_movement_hook_air_sources_t *sources,
	sg_movement_hook_air_set_t **set_out,
	sg_movement_hook_air_error_t *error_out)
{
	sg_movement_hook_air_error_t error;

	ErrorInitialize(&error);
	if (!set_out || *set_out)
	{
		error.code = SG_MOVEMENT_HOOK_AIR_ERROR_INVALID_ARGUMENT;
		goto done;
	}
	if (!SourcesPresent(sources))
	{
		error.code = SG_MOVEMENT_HOOK_AIR_ERROR_INVALID_ARGUMENT;
		goto done;
	}
	if (!SourceIdentitiesMatch(sources))
	{
		error.code = SG_MOVEMENT_HOOK_AIR_ERROR_IDENTITY_MISMATCH;
		goto done;
	}
	if (!AuditPrerequisites(sources, &error))
		goto done;

	/* The dependency graph has no production visibility publication yet. An
	 * opaque non-null pointer is not evidence, so this consumer cannot inspect
	 * or accept it until the provider's audit API lands. */
	if (!sources->visibility)
		error.code =
			SG_MOVEMENT_HOOK_AIR_ERROR_VISIBILITY_PREREQUISITE_UNAVAILABLE;
	else if (!sources->host_laws)
		error.code =
			SG_MOVEMENT_HOOK_AIR_ERROR_HOST_LAW_PREREQUISITE_UNAVAILABLE;
	else
		error.code =
			SG_MOVEMENT_HOOK_AIR_ERROR_VISIBILITY_PREREQUISITE_UNAVAILABLE;

done:
	if (error_out)
		*error_out = error;
	return 0;
}

void SG_MovementHookAirDestroy(sg_movement_hook_air_set_t *set)
{
	if (!set)
		return;
	free(set->facts);
	free(set);
}

int SG_MovementHookAirCommandForDirection(
	sg_movement_air_direction_t direction, usercmd_t *command_out,
	sg_rune_vec3_t *world_vector_out)
{
	if (!command_out || !world_vector_out ||
		direction <= SG_MOVEMENT_AIR_DIRECTION_NONE ||
		direction >= SG_MOVEMENT_AIR_DIRECTION_COUNT)
		return 0;
	memset(command_out, 0, sizeof(*command_out));
	memset(world_vector_out, 0, sizeof(*world_vector_out));
	switch (direction)
	{
	case SG_MOVEMENT_AIR_DIRECTION_POSITIVE_X:
		command_out->forwardmove = SG_MOVEMENT_HOOK_AIR_COMMAND_MAGNITUDE;
		world_vector_out->value[0] =
			(float)SG_MOVEMENT_HOOK_AIR_COMMAND_MAGNITUDE;
		break;
	case SG_MOVEMENT_AIR_DIRECTION_NEGATIVE_X:
		command_out->forwardmove = -SG_MOVEMENT_HOOK_AIR_COMMAND_MAGNITUDE;
		world_vector_out->value[0] =
			-(float)SG_MOVEMENT_HOOK_AIR_COMMAND_MAGNITUDE;
		break;
	case SG_MOVEMENT_AIR_DIRECTION_POSITIVE_Y:
		command_out->sidemove = -SG_MOVEMENT_HOOK_AIR_COMMAND_MAGNITUDE;
		world_vector_out->value[1] =
			(float)SG_MOVEMENT_HOOK_AIR_COMMAND_MAGNITUDE;
		break;
	case SG_MOVEMENT_AIR_DIRECTION_NEGATIVE_Y:
		command_out->sidemove = SG_MOVEMENT_HOOK_AIR_COMMAND_MAGNITUDE;
		world_vector_out->value[1] =
			-(float)SG_MOVEMENT_HOOK_AIR_COMMAND_MAGNITUDE;
		break;
	case SG_MOVEMENT_AIR_DIRECTION_NONE:
	case SG_MOVEMENT_AIR_DIRECTION_COUNT:
		return 0;
	}
	return 1;
}

int SG_MovementHookAirFlightFrameCount(float distance, float bolt_speed,
	uint32_t frame_ms, uint32_t *frames_out)
{
	double frame_distance;
	double total_frames;

	if (!frames_out || !isfinite(distance) || distance <= 0.0f ||
		!isfinite(bolt_speed) || bolt_speed <= 0.0f || frame_ms == 0U)
		return 0;
	frame_distance = (double)bolt_speed * (double)frame_ms / 1000.0;
	total_frames = ceil((double)distance / frame_distance);
	if (!isfinite(total_frames) || total_frames < 1.0 ||
		total_frames > (double)UINT32_MAX + 1.0)
		return 0;
	*frames_out = (uint32_t)(total_frames - 1.0);
	return 1;
}

const char *SG_MovementHookAirErrorString(
	sg_movement_hook_air_error_code_t code)
{
	switch (code)
	{
	case SG_MOVEMENT_HOOK_AIR_ERROR_NONE: return "none";
	case SG_MOVEMENT_HOOK_AIR_ERROR_INVALID_ARGUMENT:
		return "invalid argument";
	case SG_MOVEMENT_HOOK_AIR_ERROR_INVALID_SOURCE: return "invalid source";
	case SG_MOVEMENT_HOOK_AIR_ERROR_IDENTITY_MISMATCH:
		return "identity mismatch";
	case SG_MOVEMENT_HOOK_AIR_ERROR_INCOMPLETE_BSP_PROOF:
		return "BSP completeness proof rejected";
	case SG_MOVEMENT_HOOK_AIR_ERROR_INCOMPLETE_SEMANTICS_PROOF:
		return "configuration semantics proof rejected";
	case SG_MOVEMENT_HOOK_AIR_ERROR_VISIBILITY_PREREQUISITE_UNAVAILABLE:
		return "production hook visibility unavailable";
	case SG_MOVEMENT_HOOK_AIR_ERROR_HOST_LAW_PREREQUISITE_UNAVAILABLE:
		return "production host laws unavailable";
	case SG_MOVEMENT_HOOK_AIR_ERROR_INVALID_PHASE: return "invalid phase";
	case SG_MOVEMENT_HOOK_AIR_ERROR_HOST_DISAGREEMENT:
		return "host disagreement";
	case SG_MOVEMENT_HOOK_AIR_ERROR_OVERFLOW:
		return "representation overflow";
	case SG_MOVEMENT_HOOK_AIR_ERROR_OUT_OF_MEMORY: return "out of memory";
	default: return "unknown hook-air capability error";
	}
}
