#include "sg_tactic_controller.h"

#include <math.h>
#include <string.h>

#include "sg_host_engine_pmove.h"
#include "sg_host_rocket_jump_law.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int Finite3(const float v[3])
{
	return isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]);
}

/* Horizontal unit direction and distance from the body to the target, and
 * the signed rise.  A target under the body's own feet has no direction. */
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

static int AimAngles(const float from[3], const float to[3], float *yaw_out,
	float *pitch_out)
{
	const float dx = to[0] - from[0], dy = to[1] - from[1], dz = to[2] - from[2];
	const float flat = sqrtf(dx * dx + dy * dy);

	if (!isfinite(flat) || (flat < 1e-3f && fabsf(dz) < 1e-3f))
		return 0;
	*yaw_out = atan2f(dy, dx) * 180.0f / (float)M_PI;
	*pitch_out = -atan2f(dz, flat) * 180.0f / (float)M_PI;
	return 1;
}

/* The hook is a rope the body fires, rides, and lets go of.  Firing needs
 * the aim; the body keeps moving throughout; the rope is released once the
 * body is past the crossing it was pulled toward. */
static void HookControl(const sg_rune_step_t *step, const sg_tactic_body_t *body,
	int have_direction, const float direction[3], float distance,
	sg_tactic_command_t *command)
{
	const sg_host_hook_phase_t live = body->hook_phase;
	float eye[3];

	if (have_direction)
		Toward(command, direction, 1.0f);
	if (live == SG_HOST_HOOK_IDLE || live == SG_HOST_HOOK_COAST)
	{
		if (step->hook_point_present == 0U || body->hook_ready == 0U)
		{
			command->status = SG_TACTIC_COMMAND_UNSUPPORTED;
			return;
		}
		memcpy(eye, body->origin, sizeof(eye));
		eye[2] += body->view_height;
		if (!AimAngles(eye, step->hook_point, &command->yaw, &command->pitch))
		{
			command->status = SG_TACTIC_COMMAND_UNSUPPORTED;
			return;
		}
		command->aim_owned = 1U;
		command->hook_fire = 1U;
		command->status = SG_TACTIC_COMMAND_MOVE;
		return;
	}
	if (live == SG_HOST_HOOK_ATTACHED && (!have_direction || distance < 24.0f))
	{
		command->hook_release = 1U;
		command->status = SG_TACTIC_COMMAND_MOVE;
		return;
	}
	command->status = SG_TACTIC_COMMAND_MOVE;
}

int SG_TacticControl(const sg_rune_step_t *step, const sg_tactic_body_t *body,
	sg_tactic_command_t *command_out)
{
	sg_tactic_command_t command;
	float direction[3];
	float distance = 0.0f;
	float rise = 0.0f;
	int have_direction;

	if (command_out == NULL)
		return 0;
	memset(command_out, 0, sizeof(*command_out));
	command_out->status = SG_TACTIC_COMMAND_HOLD;
	if (step == NULL || body == NULL || !Finite3(body->origin) ||
		!Finite3(body->velocity) || !isfinite(body->gravity))
		return 0;
	memset(&command, 0, sizeof(command));
	command.status = SG_TACTIC_COMMAND_HOLD;
	if (step->kind == SG_RUNE_STEP_HOLD ||
		step->kind == SG_RUNE_STEP_UNREACHABLE || !Finite3(step->target))
	{
		*command_out = command;
		return 1;
	}
	have_direction = Steer(body, step->target, direction, &distance, &rise);
	/* Arrived: close on the point, easing in over the last body length. */
	if (step->kind == SG_RUNE_STEP_ARRIVED)
	{
		if (have_direction)
			Toward(&command, direction, distance < 32.0f ? distance / 32.0f :
				1.0f);
		*command_out = command;
		return 1;
	}
	/* A stance the crossing needs is taken while moving. */
	if (step->crouching_next && body->crouched == 0U)
		command.up = -1.0f;
	switch (step->move_kind)
	{
	case SG_RUNE_MOVE_WALK:
	case SG_RUNE_MOVE_RAMP:
	case SG_RUNE_MOVE_DROP:
	case SG_RUNE_MOVE_AIR_CONTROL:
	case SG_RUNE_MOVE_MOVER:
	case SG_RUNE_MOVE_EXTERNAL_FORCE:
		if (have_direction)
			Toward(&command, direction, 1.0f);
		break;
	case SG_RUNE_MOVE_CROUCH:
		if (have_direction)
			Toward(&command, direction, 1.0f);
		command.up = -1.0f;
		command.status = SG_TACTIC_COMMAND_MOVE;
		break;
	case SG_RUNE_MOVE_SWIM:
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
	case SG_RUNE_MOVE_JUMP:
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
	case SG_RUNE_MOVE_ROCKET_JUMP:
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
	case SG_RUNE_MOVE_HOOK:
		HookControl(step, body, have_direction, direction, distance, &command);
		break;
	case SG_RUNE_MOVE_CONTROLLER_ACTION:
		break;
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
