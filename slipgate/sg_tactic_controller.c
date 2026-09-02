#include "sg_tactic_controller.h"

#include <math.h>
#include <string.h>

#include "sg_host_engine_pmove.h"
#include "sg_host_rocket_jump_law.h"

static int Finite3(const float value[3])
{
	return isfinite(value[0]) && isfinite(value[1]) && isfinite(value[2]);
}

/* The point this frame moves toward: a portal or cell centre for a step, the
 * point itself for a terminal destination. */
static int ExecutionTarget(const sg_tactic_execution_t *execution,
	float target_out[3])
{
	uint32_t axis;

	switch (execution->kind)
	{
	case SG_TACTIC_EXECUTION_PORTAL_STEP:
	case SG_TACTIC_EXECUTION_DIRECT_STEP:
		if (execution->target_point_present == 0U ||
			!Finite3(execution->target_point))
			return 0;
		memcpy(target_out, execution->target_point, sizeof(float) * 3U);
		return 1;
	case SG_TACTIC_EXECUTION_LOCAL_DESTINATION:
		if (execution->destination.kind != SG_RUNE_COMPACT_DESTINATION_POINT)
			return 0;
		for (axis = 0U; axis < 3U; axis++)
			target_out[axis] = (float)
				execution->destination.value.point.value[axis] / 8.0f;
		return 1;
	case SG_TACTIC_EXECUTION_MECHANISMS_REQUIRED:
	case SG_TACTIC_EXECUTION_STANCE_STEP:
	case SG_TACTIC_EXECUTION_CELL_DESTINATION:
	case SG_TACTIC_EXECUTION_BLOCKED_NOW:
	case SG_TACTIC_EXECUTION_DISCONNECTED:
	case SG_TACTIC_EXECUTION_KIND_COUNT:
	default:
		return 0;
	}
}

/* Horizontal unit direction and distance from the body to the target, and the
 * signed rise.  A target under the body's own feet has no direction. */
static int Steer(const sg_tactic_body_t *body, const float target[3],
	float direction_out[3], float *distance_out, float *rise_out)
{
	const float dx = target[0] - body->origin[0];
	const float dy = target[1] - body->origin[1];
	const float distance = sqrtf(dx * dx + dy * dy);

	*rise_out = target[2] - body->origin[2];
	*distance_out = distance;
	if (!isfinite(distance) || distance <= 1.0f)
		return 0;
	direction_out[0] = dx / distance;
	direction_out[1] = dy / distance;
	direction_out[2] = 0.0f;
	return 1;
}

static void Toward(sg_tactic_command_t *command, const float direction[3],
	float speed)
{
	memcpy(command->direction, direction, sizeof(float) * 3U);
	command->speed = speed;
	command->status = SG_TACTIC_COMMAND_MOVE;
}

/* How far the body carries horizontally at full speed during a launch's
 * flight: the range within which pressing jump now reaches the target. */
static float FlightReach(float vertical_velocity, float gravity)
{
	if (!(gravity > 0.0f) || !(vertical_velocity > 0.0f))
		return 0.0f;
	return SG_HOST_ENGINE_MAX_SPEED * (2.0f * vertical_velocity / gravity);
}

static void Q8Point(const sg_rune_q8_vec3_t *point, float out[3])
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		out[axis] = (float)point->value[axis] / 8.0f;
}

static void Q8BoundsCentre(const sg_rune_q8_bounds_t *bounds, float out[3])
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		out[axis] = (float)((double)bounds->mins.value[axis] +
			(double)bounds->maxs.value[axis]) / 16.0f;
}

static float DistanceSquared(const float a[3], const float b[3])
{
	const float dx = a[0] - b[0];
	const float dy = a[1] - b[1];
	const float dz = a[2] - b[2];

	return dx * dx + dy * dy + dz * dz;
}

int SG_TacticHookTargetAim(const sg_rune_compact_model_t *model,
	uint32_t hook_target, const float from[3], float aim_out[3])
{
	const sg_rune_compact_movement_hook_target_t *target;
	const sg_rune_compact_response_projection_t *response;
	const sg_rune_compact_response_endpoint_group_t *group;
	uint32_t offset;
	int found = 0;
	float best = 0.0f;

	if (model == NULL || from == NULL || aim_out == NULL ||
		model->movement_hook_targets == NULL ||
		hook_target >= model->movement_hook_target_count)
		return 0;
	target = &model->movement_hook_targets[hook_target];
	response = &model->response;
	if (target->provenance !=
		SG_RUNE_MOVEMENT_HOOK_TARGET_PROVENANCE_STATIC_RESPONSE)
		return 0;
	switch (target->response.kind)
	{
	case SG_RUNE_COMPACT_RESPONSE_REF_CERTIFIED_FACT:
		if (response->facts == NULL ||
			target->response.index >= response->fact_count)
			return 0;
		Q8Point(&response->facts[target->response.index].target_witness,
			aim_out);
		return 1;
	case SG_RUNE_COMPACT_RESPONSE_REF_CANDIDATE_GROUP:
		if (response->candidate_groups == NULL ||
			target->response.index >= response->candidate_group_count)
			return 0;
		if (response->target_endpoint_groups == NULL ||
			response->candidate_groups[target->response.index].target_group >=
				response->target_endpoint_group_count)
			return 0;
		group = &response->target_endpoint_groups[
			response->candidate_groups[target->response.index].target_group];
		if (response->target_endpoint_members == NULL ||
			response->target_patches == NULL ||
			group->first_member > response->target_endpoint_member_count ||
			group->member_count >
				response->target_endpoint_member_count - group->first_member)
			return 0;
		for (offset = 0U; offset < group->member_count; offset++)
		{
			const uint32_t patch = response->target_endpoint_members[
				group->first_member + offset];
			float centre[3];
			float distance;

			if (patch >= response->target_patch_count)
				return 0;
			Q8BoundsCentre(&response->target_patches[patch].bounds, centre);
			distance = DistanceSquared(centre, from);
			if (!found || distance < best)
			{
				memcpy(aim_out, centre, sizeof(float) * 3U);
				best = distance;
				found = 1;
			}
		}
		return found;
	case SG_RUNE_COMPACT_RESPONSE_REF_KIND_COUNT:
	default:
		return 0;
	}
}

/* View angles that put the eye's forward ray through the point: yaw in the
 * plane, pitch positive downward as the engine has it. */
static int AimAngles(const float eye[3], const float point[3], float *yaw_out,
	float *pitch_out)
{
	const float dx = point[0] - eye[0];
	const float dy = point[1] - eye[1];
	const float dz = point[2] - eye[2];
	const float horizontal = sqrtf(dx * dx + dy * dy);

	if (!isfinite(horizontal) || (horizontal <= 0.0f && fabsf(dz) <= 0.0f))
		return 0;
	*yaw_out = atan2f(dy, dx) * 180.0f / (float)M_PI;
	*pitch_out = -atan2f(dz, horizontal) * 180.0f / (float)M_PI;
	return 1;
}

/* The hook is a rope the body fires, rides, and lets go of; the selected
 * capability says which of those happens now through the successor phase.
 * Firing needs the aim; the body keeps moving throughout, because a hook
 * fired on the run or in the air still pulls when it bites. */
static void HookControl(const sg_rune_compact_model_t *model,
	const sg_tactic_execution_t *execution, const sg_tactic_body_t *body,
	int have_direction, const float direction[3], sg_tactic_command_t *command)
{
	const sg_host_hook_phase_t live = body->hook_phase;
	const sg_host_hook_phase_t next = execution->target_hook_phase;
	float eye[3];
	float aim[3];

	if (have_direction)
		Toward(command, direction, 1.0f);
	if ((live == SG_HOST_HOOK_IDLE || live == SG_HOST_HOOK_COAST) &&
		next == SG_HOST_HOOK_IN_FLIGHT)
	{
		if (execution->hook_target_present == 0U || body->hook_ready == 0U)
		{
			command->status = SG_TACTIC_COMMAND_UNSUPPORTED;
			return;
		}
		memcpy(eye, body->origin, sizeof(eye));
		eye[2] += body->view_height;
		if (!SG_TacticHookTargetAim(model, execution->hook_target, eye, aim) ||
			!AimAngles(eye, aim, &command->yaw, &command->pitch))
		{
			command->status = SG_TACTIC_COMMAND_UNSUPPORTED;
			return;
		}
		command->aim_owned = 1U;
		command->hook_fire = 1U;
		command->status = SG_TACTIC_COMMAND_MOVE;
		return;
	}
	if ((live == SG_HOST_HOOK_ATTACHED || live == SG_HOST_HOOK_IN_FLIGHT) &&
		(next == SG_HOST_HOOK_COAST || next == SG_HOST_HOOK_IDLE))
	{
		command->hook_release = 1U;
		command->status = SG_TACTIC_COMMAND_MOVE;
		return;
	}
	/* In flight waiting for the bite, or riding the pull: nothing to press. */
	command->status = SG_TACTIC_COMMAND_MOVE;
}

int SG_TacticControl(const sg_rune_compact_model_t *model,
	const sg_tactic_execution_t *execution, const sg_tactic_body_t *body,
	sg_tactic_command_t *command_out)
{
	sg_tactic_command_t command;
	float target[3];
	float direction[3];
	float distance = 0.0f;
	float rise = 0.0f;
	int have_direction;

	if (command_out == NULL)
		return 0;
	memset(command_out, 0, sizeof(*command_out));
	command_out->status = SG_TACTIC_COMMAND_HOLD;
	if (execution == NULL || body == NULL || !Finite3(body->origin) ||
		!Finite3(body->velocity) || !isfinite(body->gravity))
		return 0;
	memset(&command, 0, sizeof(command));
	command.status = SG_TACTIC_COMMAND_HOLD;

	/* Terminal and blocked states hold; a stance step changes stance in
	 * place; everything else needs a point to move toward. */
	if (execution->kind == SG_TACTIC_EXECUTION_STANCE_STEP)
	{
		command.up = execution->target_stance ==
			SG_RUNE_COMPACT_FIELD_CROUCHING ? -1.0f : 0.0f;
		command.status = SG_TACTIC_COMMAND_MOVE;
		*command_out = command;
		return 1;
	}
	if (!ExecutionTarget(execution, target))
	{
		*command_out = command;
		return 1;
	}
	have_direction = Steer(body, target, direction, &distance, &rise);

	/* A terminal point, or a step whose selection is not a progressing
	 * capability, is plain steering. */
	if (execution->selection_present == 0U ||
		execution->selection_status != SG_TACTIC_RESULT_PROGRESS)
	{
		if (have_direction)
			Toward(&command, direction, 1.0f);
		*command_out = command;
		return 1;
	}

	switch (execution->capability)
	{
	case SG_TACTIC_CAPABILITY_WALK:
	case SG_TACTIC_CAPABILITY_STRAFE:
	case SG_TACTIC_CAPABILITY_DROP:
	case SG_TACTIC_CAPABILITY_AIR_CONTROL:
	case SG_TACTIC_CAPABILITY_MECHANISM:
	case SG_TACTIC_CAPABILITY_TELEPORT:
	case SG_TACTIC_CAPABILITY_PUSH:
	case SG_TACTIC_CAPABILITY_MOVER:
		if (have_direction)
			Toward(&command, direction, 1.0f);
		break;
	case SG_TACTIC_CAPABILITY_CROUCH:
		if (have_direction)
			Toward(&command, direction, 1.0f);
		command.up = -1.0f;
		command.status = SG_TACTIC_COMMAND_MOVE;
		break;
	case SG_TACTIC_CAPABILITY_SWIM:
		if (have_direction)
		{
			const float length = sqrtf(distance * distance + rise * rise);

			Toward(&command, direction, 1.0f);
			command.up = length > 0.0f ? rise / length : 0.0f;
		}
		else if (isfinite(rise) && fabsf(rise) > 1.0f)
		{
			command.up = rise > 0.0f ? 1.0f : -1.0f;
			command.status = SG_TACTIC_COMMAND_MOVE;
		}
		break;
	case SG_TACTIC_CAPABILITY_JUMP:
		/* Run at the crossing; press jump once it is within the jump's own
		 * reach, so the arc lands past the portal rather than short of it.
		 * Off the ground the press is nothing and the run is air control. */
		if (have_direction)
			Toward(&command, direction, 1.0f);
		if (body->supported != 0U && (distance <=
			FlightReach(SG_HOST_ENGINE_JUMP_VELOCITY, body->gravity) ||
			!have_direction))
		{
			command.up = 1.0f;
			command.status = SG_TACTIC_COMMAND_MOVE;
		}
		break;
	case SG_TACTIC_CAPABILITY_ROCKET_JUMP:
	{
		sg_host_rocket_jump_launch_t launch;

		/* The launcher must be in hand first; until it is, close in on the
		 * crossing.  Then, on the ground, within the launch's reach: face
		 * the crossing, aim straight down, fire and jump in one command. */
		command.want_launcher = 1U;
		if (have_direction)
			Toward(&command, direction, 1.0f);
		if (body->launcher_ready == 0U || body->supported == 0U ||
			!SG_HostRocketJumpLaunch(body->gravity, body->frame_ms,
				body->substep_ms, 0, &launch))
			break;
		if (have_direction && distance >
			FlightReach(launch.vertical_velocity, body->gravity))
			break;
		command.aim_owned = 1U;
		command.yaw = have_direction ? atan2f(direction[1], direction[0]) *
			180.0f / (float)M_PI : 0.0f;
		command.pitch = 90.0f;
		command.attack = 1U;
		command.up = 1.0f;
		command.status = SG_TACTIC_COMMAND_MOVE;
		break;
	}
	case SG_TACTIC_CAPABILITY_WAIT:
		break;
	case SG_TACTIC_CAPABILITY_HOOK:
		HookControl(model, execution, body, have_direction, direction,
			&command);
		break;
	case SG_TACTIC_CAPABILITY_COUNT:
	default:
		*command_out = command;
		return 0;
	}
	*command_out = command;
	return 1;
}

const char *SG_TacticCommandStatusString(sg_tactic_command_status_t status)
{
	static const char *const names[SG_TACTIC_COMMAND_STATUS_COUNT] = {
		"move", "hold", "unsupported"
	};

	return (uint32_t)status < (uint32_t)SG_TACTIC_COMMAND_STATUS_COUNT ?
		names[status] : "unknown tactic command status";
}
