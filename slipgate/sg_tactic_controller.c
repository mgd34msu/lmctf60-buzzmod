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

int SG_TacticControl(const sg_tactic_execution_t *execution,
	const sg_tactic_body_t *body, sg_tactic_command_t *command_out)
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
		/* The hook's aim comes from the RUNE's hook target; until the
		 * executor reads it, the crossing is approached on foot. */
		if (have_direction)
			Toward(&command, direction, 1.0f);
		command.status = SG_TACTIC_COMMAND_UNSUPPORTED;
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
