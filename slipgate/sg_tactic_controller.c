#include "sg_tactic_controller.h"

#include <math.h>
#include <string.h>

#include "sg_rune_law.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define EASE_DISTANCE 96.0f       /* a walk that must end at a point slows over this */
#define EASE_FLOOR 0.2f           /* and never below this fraction of full speed */

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
static float FlightReach(const sg_tactic_body_t *body, float vertical_velocity)
{
	float speed = body->law ? body->law->max_velocity : 0.0f;

	if (!(body->gravity > 0.0f) || !(vertical_velocity > 0.0f))
		return 0.0f;
	return speed * (2.0f * vertical_velocity / body->gravity);
}

/* A flight record was traced from the cell's middle at full run speed
 * toward its portal.  The body reproduces it by being behind the portal
 * along that launch direction, close to the line, with its own velocity
 * along it; otherwise it goes to the run-up point first.  launch_out is
 * the launch's horizontal unit direction when there is one. */
#define RUN_UP_EASE 64.0f
#define RUN_UP_CLOSE 16.0f
#define RUN_UP_BEHIND 8.0f
#define RUN_UP_LATERAL 12.0f
#define RUN_UP_LATERAL_PER_UNIT 0.2f
#define RUN_UP_VELOCITY_ALIGNED 0.85f
#define RUN_UP_VELOCITY_MATTERS 120.0f

/* The fraction of run speed the record's launch leaves the edge at. */
static float LaunchRun(const sg_rune_step_t *step, const sg_tactic_body_t *body)
{
	float speed = sqrtf(step->launch[0] * step->launch[0] +
		step->launch[1] * step->launch[1]);
	float max = body->law ? body->law->max_velocity : 0.0f;

	if (!step->launch_present || !(max > 0.0f) || !(speed > 0.0f))
		return 1.0f;
	return speed / max > 1.0f ? 1.0f : (speed / max < 0.2f ? 0.2f : speed / max);
}

static int LinedUp(const sg_rune_step_t *step, const sg_tactic_body_t *body,
	float launch_out[3])
{
	const float lx = step->launch[0], ly = step->launch[1];
	const float length = sqrtf(lx * lx + ly * ly);
	float bx, by, along, across, speed;

	if (!step->launch_present || !isfinite(length) || length < 1.0f)
		return 1;
	launch_out[0] = lx / length;
	launch_out[1] = ly / length;
	launch_out[2] = 0.0f;
	if (!step->run_up_present)
		return 1;
	/* At the run-up point itself, or with no room behind the portal. */
	bx = step->run_up[0] - step->target[0];
	by = step->run_up[1] - step->target[1];
	if (bx * bx + by * by < RUN_UP_BEHIND * RUN_UP_BEHIND)
		return 1;
	bx = body->origin[0] - step->run_up[0];
	by = body->origin[1] - step->run_up[1];
	if (bx * bx + by * by < RUN_UP_CLOSE * RUN_UP_CLOSE)
		return 1;
	bx = body->origin[0] - step->target[0];
	by = body->origin[1] - step->target[1];
	along = bx * launch_out[0] + by * launch_out[1];
	across = fabsf(-bx * launch_out[1] + by * launch_out[0]);
	if (along > -RUN_UP_BEHIND ||
		across > RUN_UP_LATERAL + RUN_UP_LATERAL_PER_UNIT * -along)
		return 0;
	speed = sqrtf(body->velocity[0] * body->velocity[0] +
		body->velocity[1] * body->velocity[1]);
	if (speed > RUN_UP_VELOCITY_MATTERS &&
		(body->velocity[0] * launch_out[0] + body->velocity[1] * launch_out[1]) <
			RUN_UP_VELOCITY_ALIGNED * speed)
		return 0;
	return 1;
}

/* Going to the run-up point: eased in over the last body length. */
static void ToRunUp(const sg_rune_step_t *step, const sg_tactic_body_t *body,
	sg_tactic_command_t *command)
{
	float direction[3], distance, rise;

	/* Eased over two body lengths: a frame at full speed covers a body
	 * length, and a point ten units off would be overshot every frame. */
	if (Steer(body, step->run_up, direction, &distance, &rise))
		Toward(command, direction, distance < RUN_UP_EASE ?
			(distance / RUN_UP_EASE > 0.1f ? distance / RUN_UP_EASE : 0.1f) : 1.0f);
	else
		command->status = SG_TACTIC_COMMAND_MOVE;
}

/* The speed of a walk that must end at the step's ease point: full until
 * the point is near, then down with the distance, never quite stopped. */
static float EasedSpeed(const sg_rune_step_t *step, const sg_tactic_body_t *body)
{
	float dx, dy, distance;

	if (!step->ease)
		return 1.0f;
	dx = step->ease_point[0] - body->origin[0];
	dy = step->ease_point[1] - body->origin[1];
	distance = sqrtf(dx * dx + dy * dy);
	if (!(distance < EASE_DISTANCE))
		return 1.0f;
	return distance / EASE_DISTANCE > EASE_FLOOR ? distance / EASE_DISTANCE : EASE_FLOOR;
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
 * the aim at the bite; the body keeps pushing toward the landing throughout;
 * the rope is let go once the ride has carried the body up to the landing's
 * height or over it, or when the bite is about to be reached. */
#define HOOK_STAND_CLOSE 24.0f
#define HOOK_FIRE_STILL 120.0f    /* the rope is fired only under this speed */
#define HOOK_RELEASE_BELOW 8.0f
#define HOOK_RELEASE_FLAT 40.0f
#define HOOK_RELEASE_AT_BITE 60.0f

static void HookControl(const sg_rune_step_t *step, const sg_tactic_body_t *body,
	int have_direction, const float direction[3], float distance,
	sg_tactic_command_t *command)
{
	const sg_tactic_hook_phase_t live = body->hook_phase;
	float eye[3];

	(void)distance;
	/* The body pushes toward the landing once the rope has it or once it
	 * is off the floor; with the bolt still flying it stands where it
	 * fired, or it runs off the edge before the rope bites. */
	if (have_direction && !(live == SG_TACTIC_HOOK_IN_FLIGHT && body->supported))
		Toward(command, direction, 1.0f);
	if (live == SG_TACTIC_HOOK_IN_FLIGHT && body->supported)
		command->status = SG_TACTIC_COMMAND_MOVE;
	if (live == SG_TACTIC_HOOK_IDLE || live == SG_TACTIC_HOOK_COAST)
	{
		if (step->hook_point_present == 0U || body->hook_ready == 0U)
		{
			command->status = SG_TACTIC_COMMAND_UNSUPPORTED;
			return;
		}
		/* The ride was traced from the cell's middle, from a body standing
		 * still: fire from there, once the run-up's speed has bled off, so
		 * the pull runs the line the record checked and the body is not
		 * sliding on when the bolt bites. */
		if (body->supported && step->run_up_present)
		{
			const float dx = step->run_up[0] - body->origin[0];
			const float dy = step->run_up[1] - body->origin[1];
			const float speed = sqrtf(body->velocity[0] * body->velocity[0] +
				body->velocity[1] * body->velocity[1]);

			if (dx * dx + dy * dy > HOOK_STAND_CLOSE * HOOK_STAND_CLOSE)
			{
				ToRunUp(step, body, command);
				return;
			}
			if (speed > HOOK_FIRE_STILL)
			{
				command->status = SG_TACTIC_COMMAND_MOVE;   /* stand: friction does the rest */
				return;
			}
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
	if (live == SG_TACTIC_HOOK_ATTACHED)
	{
		float dx = step->target[0] - body->origin[0];

		/* The ride was swept with the crouch hull: ride crouched. */
		command->up = -1.0f;
		float dy = step->target[1] - body->origin[1];
		float flat = sqrtf(dx * dx + dy * dy);
		float to_bite = INFINITY;

		float let_go = HOOK_RELEASE_AT_BITE;

		if (step->hook_point_present)
		{
			float bx = step->hook_point[0] - body->origin[0];
			float by = step->hook_point[1] - body->origin[1];
			float bz = step->hook_point[2] - (body->origin[2] + body->view_height);

			to_bite = sqrtf(bx * bx + by * by + bz * bz);
			if (step->hook_release_distance > 0.0f)
				let_go = step->hook_release_distance + HOOK_RELEASE_BELOW;
		}
		/* Let go where the record's flight was traced from (the eye that
		 * far from the bite), or once the ride has the body at the
		 * landing's height, or over the landing and nearly there. */
		if (!have_direction || to_bite <= let_go ||
			body->origin[2] >= step->target[2] - HOOK_RELEASE_BELOW ||
			(flat < HOOK_RELEASE_FLAT &&
			 body->origin[2] >= step->target[2] - 6.0f * HOOK_RELEASE_BELOW))
		{
			command->hook_release = 1U;
			command->status = SG_TACTIC_COMMAND_MOVE;
			return;
		}
	}
	command->status = SG_TACTIC_COMMAND_MOVE;
}

int SG_TacticControl(const sg_rune_step_t *step, const sg_tactic_body_t *body,
	sg_tactic_command_t *command_out)
{
	sg_tactic_command_t command;
	float direction[3] = { 0.0f, 0.0f, 0.0f };
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
	case SG_RUNE_MOVE_AIR_CONTROL:
	case SG_RUNE_MOVE_MOVER:
	case SG_RUNE_MOVE_EXTERNAL_FORCE:
		if (have_direction)
			Toward(&command, direction, EasedSpeed(step, body));
		break;
	case SG_RUNE_MOVE_DROP:
	{
		float launch[3];

		/* Off the edge along the record's own line, from behind it. */
		if (body->supported != 0U && !LinedUp(step, body, launch))
		{
			ToRunUp(step, body, &command);
			break;
		}
		if (body->supported != 0U && step->launch_present)
			Toward(&command, launch, LaunchRun(step, body));
		else if (have_direction)
			Toward(&command, direction, 1.0f);
		break;
	}
	case SG_RUNE_MOVE_CROUCH:
		if (have_direction)
			Toward(&command, direction, EasedSpeed(step, body));
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
	{
		float launch[3];

		/* Run at the crossing along the record's line, from behind it;
		 * press jump once it is within the jump's own reach, so the arc
		 * lands past the portal rather than short of it.  Off the ground
		 * the press is nothing and the run is air control. */
		if (body->supported != 0U && !LinedUp(step, body, launch))
		{
			ToRunUp(step, body, &command);
			break;
		}
		if (body->supported != 0U && step->launch_present)
			Toward(&command, launch, 1.0f);
		else if (have_direction)
			Toward(&command, direction, 1.0f);
		if (body->supported != 0U && (distance <=
			FlightReach(body, body->law ? body->law->jump_velocity : 0.0f) ||
			!have_direction))
		{
			command.up = 1.0f;
			command.status = SG_TACTIC_COMMAND_MOVE;
		}
		break;
	}
	case SG_RUNE_MOVE_ROCKET_JUMP:
	{
		sg_rune_rocket_jump_t launch;
		float line[3];

		/* The launcher must be in hand first; until it is, close in on the
		 * crossing along the record's line, from behind it.  Then, on the
		 * ground, within the launch's reach: face the crossing, aim straight
		 * down, fire and jump in one command. */
		command.want_launcher = 1U;
		if (body->supported != 0U && !LinedUp(step, body, line))
		{
			ToRunUp(step, body, &command);
			break;
		}
		if (body->supported != 0U && step->launch_present)
			Toward(&command, line, 1.0f);
		else if (have_direction)
			Toward(&command, direction, 1.0f);
		if (body->launcher_ready == 0U || body->supported == 0U || !body->law ||
			!SG_RuneLawRocketJump(body->law, &launch))
			break;
		if (have_direction && distance > FlightReach(body, launch.vertical_velocity))
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
