#include "q_shared.h"
#include "slipgate/sg_rocketjump_live.h"

#include <string.h>

static qboolean RocketJumpBoolean(qboolean value)
{
	return value == false || value == true;
}

static qboolean RocketJumpProjectileValid(
	sg_rocketjump_projectile_key_t projectile)
{
	return projectile.key != 0U && projectile.generation != 0U;
}

static qboolean RocketJumpProjectileEqual(
	sg_rocketjump_projectile_key_t first,
	sg_rocketjump_projectile_key_t second)
{
	return first.key == second.key && first.generation == second.generation;
}

static qboolean RocketJumpObservationValid(
	const sg_rocketjump_observation_t *observation)
{
	return observation && RocketJumpBoolean(observation->alive) &&
	    RocketJumpBoolean(observation->grounded) &&
	    RocketJumpBoolean(observation->dry) &&
	    RocketJumpBoolean(observation->immutable_support) &&
	    RocketJumpBoolean(observation->normal_move) &&
	    RocketJumpBoolean(observation->standing) &&
	    RocketJumpBoolean(observation->jump_released) &&
	    RocketJumpBoolean(observation->right_handed) &&
	    RocketJumpBoolean(observation->quad_active) &&
	    RocketJumpBoolean(observation->standard_weapon_law) &&
	    RocketJumpBoolean(observation->launcher_owned) &&
	    RocketJumpBoolean(observation->launcher_selected) &&
	    RocketJumpBoolean(observation->weapon_ready);
}

static qboolean RocketJumpSourceExact(
	const sg_rocketjump_witness_t *witness,
	const sg_rocketjump_observation_t *observation)
{
	int axis;

	for (axis = 0; axis < 3; axis++)
		if (witness->source_q8[axis] != observation->origin_q8[axis])
			return false;
	return true;
}

static qboolean RocketJumpLaunchState(
	const sg_rocketjump_witness_t *witness,
	const sg_rocketjump_observation_t *observation)
{
	int axis;

	for (axis = 0; axis < 3; axis++)
		if (observation->velocity_q8[axis] != 0)
			return false;
	return observation->alive && observation->grounded && observation->dry &&
	    observation->immutable_support && observation->normal_move &&
	    observation->standing && observation->jump_released &&
	    observation->right_handed &&
	    !observation->quad_active && observation->standard_weapon_law &&
	    observation->launcher_owned && observation->rockets > 0 &&
	    observation->health >
	        (int)witness->health_price + SG_ROCKETJUMP_HEALTH_MARGIN;
}

static qboolean RocketJumpFail(sg_rocketjump_live_state_t *state,
	sg_rocketjump_failure_t failure)
{
	if (state)
	{
		state->phase = SG_ROCKETJUMP_FAILED;
		state->failure = failure;
	}
	return false;
}

void SG_RocketJumpLiveReset(sg_rocketjump_live_state_t *state)
{
	if (state)
		memset(state, 0, sizeof(*state));
}

qboolean SG_RocketJumpArrivalEnvelope(const short origin_q8[3],
	const short destination_q8[3])
{
	int64_t dx, dy, dz;

	if (!origin_q8 || !destination_q8)
		return false;
	dx = (int64_t)destination_q8[0] - origin_q8[0];
	dy = (int64_t)destination_q8[1] - origin_q8[1];
	dz = (int64_t)destination_q8[2] - origin_q8[2];
	return dx * dx + dy * dy <
	           (int64_t)SG_ROCKETJUMP_ARRIVAL_RADIUS_Q8 *
	               SG_ROCKETJUMP_ARRIVAL_RADIUS_Q8 &&
	       dz >= -SG_ROCKETJUMP_ARRIVAL_Z_Q8 &&
	       dz <= SG_ROCKETJUMP_ARRIVAL_Z_Q8;
}

qboolean SG_RocketJumpControlMuzzle(const vec3_t origin, short pitch,
	short yaw, vec3_t muzzle, vec3_t forward)
{
	const float short_to_radians =
	    2.0f * 3.14159265358979323846f / 65536.0f;
	float pitch_radians, yaw_radians;
	float cp, sp, cy, sy;
	vec3_t right;

	if (!origin || !muzzle || !forward || !isfinite(origin[0]) ||
	    !isfinite(origin[1]) || !isfinite(origin[2]))
		return false;
	pitch_radians = (float)(unsigned short)pitch * short_to_radians;
	yaw_radians = (float)(unsigned short)yaw * short_to_radians;
	cp = cosf(pitch_radians);
	sp = sinf(pitch_radians);
	cy = cosf(yaw_radians);
	sy = sinf(yaw_radians);
	forward[0] = cp * cy;
	forward[1] = cp * sy;
	forward[2] = -sp;
	right[0] = sy;
	right[1] = -cy;
	right[2] = 0.0f;
	muzzle[0] = origin[0] + forward[0] * 8.0f + right[0] * 8.0f;
	muzzle[1] = origin[1] + forward[1] * 8.0f + right[1] * 8.0f;
	muzzle[2] = origin[2] + forward[2] * 8.0f + right[2] * 8.0f +
	            (SG_RUNE_PROOF_ROCKETJUMP_VIEWHEIGHT - 8.0f);
	return true;
}

qboolean SG_RocketJumpLiveBegin(sg_rocketjump_live_state_t *state,
	const sg_rocketjump_witness_t *witness,
	const sg_rocketjump_observation_t *observation)
{
	int axis;
	qboolean different = false;

	if (!state)
		return false;
	SG_RocketJumpLiveReset(state);
	if (!witness || !RocketJumpObservationValid(observation))
		return RocketJumpFail(state, SG_ROCKETJUMP_FAILURE_ARGUMENT);
	for (axis = 0; axis < 3; axis++)
		if (witness->source_q8[axis] != witness->destination_q8[axis])
			different = true;
	if (witness->link_index < 0 || !different || witness->cost_ms == 0 ||
	    witness->cost_ms > SG_ROCKETJUMP_MAX_ACTION_MS ||
	    witness->health_price < SG_RUNE_PROOF_ROCKETJUMP_HEALTH_MIN ||
	    witness->health_price > SG_RUNE_PROOF_ROCKETJUMP_HEALTH_MAX)
		return RocketJumpFail(state, SG_ROCKETJUMP_FAILURE_WITNESS);
	state->witness = *witness;
	if (!RocketJumpSourceExact(witness, observation))
		return RocketJumpFail(state, SG_ROCKETJUMP_FAILURE_SOURCE);
	if (!RocketJumpLaunchState(witness, observation))
		return RocketJumpFail(state, SG_ROCKETJUMP_FAILURE_LAUNCH_STATE);
	state->phase = SG_ROCKETJUMP_EQUIP;
	return true;
}

sg_rocketjump_command_t SG_RocketJumpLiveCommand(
	sg_rocketjump_live_state_t *state,
	const sg_rocketjump_observation_t *observation)
{
	if (!state || !RocketJumpObservationValid(observation))
	{
		RocketJumpFail(state, SG_ROCKETJUMP_FAILURE_ARGUMENT);
		return SG_ROCKETJUMP_COMMAND_ZERO;
	}
	if (state->phase == SG_ROCKETJUMP_FLIGHT)
		return SG_ROCKETJUMP_COMMAND_FLIGHT;
	if (state->phase != SG_ROCKETJUMP_EQUIP &&
	    state->phase != SG_ROCKETJUMP_ARMED)
		return SG_ROCKETJUMP_COMMAND_ZERO;
	if (!RocketJumpSourceExact(&state->witness, observation))
	{
		RocketJumpFail(state, SG_ROCKETJUMP_FAILURE_SOURCE);
		return SG_ROCKETJUMP_COMMAND_ZERO;
	}
	if (!RocketJumpLaunchState(&state->witness, observation))
	{
		RocketJumpFail(state, SG_ROCKETJUMP_FAILURE_LAUNCH_STATE);
		return SG_ROCKETJUMP_COMMAND_ZERO;
	}
	if (!observation->launcher_selected || !observation->weapon_ready)
	{
		state->phase = SG_ROCKETJUMP_EQUIP;
		return SG_ROCKETJUMP_COMMAND_EQUIP;
	}
	state->phase = SG_ROCKETJUMP_ARMED;
	return SG_ROCKETJUMP_COMMAND_FIRE;
}

qboolean SG_RocketJumpLiveFired(sg_rocketjump_live_state_t *state,
	sg_rocketjump_projectile_key_t projectile,
	const short expected_impact_q8[3])
{
	if (!state || state->phase != SG_ROCKETJUMP_ARMED ||
	    !RocketJumpProjectileValid(projectile) || !expected_impact_q8)
		return RocketJumpFail(state, SG_ROCKETJUMP_FAILURE_PROJECTILE);
	state->projectile = projectile;
	memcpy(state->expected_impact_q8, expected_impact_q8,
	       sizeof(state->expected_impact_q8));
	state->phase = SG_ROCKETJUMP_FLIGHT;
	state->elapsed_ms = 0;
	return true;
}

qboolean SG_RocketJumpLiveImpactBegin(sg_rocketjump_live_state_t *state,
	sg_rocketjump_projectile_key_t projectile, qboolean world_hit,
	qboolean sky, qboolean expected_surface, int health_before,
	const short velocity_before_q8[3])
{
	if (!state || state->phase != SG_ROCKETJUMP_FLIGHT ||
	    !velocity_before_q8)
		return RocketJumpFail(state, SG_ROCKETJUMP_FAILURE_IMPACT);
	if (!RocketJumpProjectileValid(projectile) ||
	    !RocketJumpProjectileEqual(projectile, state->projectile))
		return RocketJumpFail(state, SG_ROCKETJUMP_FAILURE_PROJECTILE);
	if (!RocketJumpBoolean(world_hit) || !RocketJumpBoolean(sky) ||
	    !RocketJumpBoolean(expected_surface) || !world_hit || sky ||
	    !expected_surface || state->impact_pending ||
	    state->impact_confirmed)
		return RocketJumpFail(state, SG_ROCKETJUMP_FAILURE_IMPACT);
	state->impact_health_before = health_before;
	memcpy(state->impact_velocity_before_q8, velocity_before_q8,
	       sizeof(state->impact_velocity_before_q8));
	state->impact_pending = true;
	return true;
}

qboolean SG_RocketJumpLiveImpactEnd(sg_rocketjump_live_state_t *state,
	sg_rocketjump_projectile_key_t projectile, int health_after,
	const short velocity_after_q8[3])
{
	int damage;
	int vertical_kick;

	if (!state || state->phase != SG_ROCKETJUMP_FLIGHT ||
	    !velocity_after_q8 || !state->impact_pending)
		return RocketJumpFail(state, SG_ROCKETJUMP_FAILURE_IMPACT);
	if (!RocketJumpProjectileValid(projectile) ||
	    !RocketJumpProjectileEqual(projectile, state->projectile))
		return RocketJumpFail(state, SG_ROCKETJUMP_FAILURE_PROJECTILE);
	damage = state->impact_health_before - health_after;
	vertical_kick = (int)velocity_after_q8[2] -
	                (int)state->impact_velocity_before_q8[2];
	/* Armor may absorb some or all of the proved health price.  It cannot make
	 * the shot cost more than the serialized upper bound, and it cannot stand
	 * in for the upward impulse that makes this action a rocket jump. */
	if (damage < 0 || damage > (int)state->witness.health_price ||
	    vertical_kick <= 0)
		return RocketJumpFail(state, SG_ROCKETJUMP_FAILURE_DAMAGE);
	state->impact_pending = false;
	state->impact_confirmed = true;
	return true;
}

qboolean SG_RocketJumpLiveStep(sg_rocketjump_live_state_t *state,
	int step_ms)
{
	int deadline;

	if (!state || step_ms <= 0)
		return RocketJumpFail(state, SG_ROCKETJUMP_FAILURE_ARGUMENT);
	if (state->phase != SG_ROCKETJUMP_EQUIP &&
	    state->phase != SG_ROCKETJUMP_ARMED &&
	    state->phase != SG_ROCKETJUMP_FLIGHT)
		return false;
	if (state->elapsed_ms > SG_ROCKETJUMP_MAX_ACTION_MS - step_ms)
		return RocketJumpFail(state, SG_ROCKETJUMP_FAILURE_TIMEOUT);
	state->elapsed_ms += step_ms;
	if (state->phase == SG_ROCKETJUMP_EQUIP ||
	    state->phase == SG_ROCKETJUMP_ARMED)
	{
		if (state->elapsed_ms >
		    SG_RUNE_PROOF_ROCKETJUMP_EQUIP_TIMEOUT_MS)
			return RocketJumpFail(state, SG_ROCKETJUMP_FAILURE_TIMEOUT);
		return true;
	}
	deadline = (int)state->witness.cost_ms +
	           SG_RUNE_PROOF_ROCKETJUMP_ARRIVAL_GRACE_MS;
	if (state->elapsed_ms > deadline)
		return RocketJumpFail(state, SG_ROCKETJUMP_FAILURE_TIMEOUT);
	return true;
}

qboolean SG_RocketJumpLiveBoundary(sg_rocketjump_live_state_t *state,
	qboolean arrived, qboolean grounded)
{
	if (!state || !RocketJumpBoolean(arrived) ||
	    !RocketJumpBoolean(grounded) ||
	    state->phase != SG_ROCKETJUMP_FLIGHT)
		return RocketJumpFail(state, SG_ROCKETJUMP_FAILURE_ARGUMENT);
	if (arrived)
	{
		if (!grounded || !state->impact_confirmed)
			return RocketJumpFail(state, SG_ROCKETJUMP_FAILURE_LANDING);
		state->phase = SG_ROCKETJUMP_COMPLETE;
		return true;
	}
	if (grounded && state->impact_confirmed)
		return RocketJumpFail(state, SG_ROCKETJUMP_FAILURE_LANDING);
	return true;
}

qboolean SG_RocketJumpLiveOwns(const sg_rocketjump_live_state_t *state)
{
	return state && (state->phase == SG_ROCKETJUMP_EQUIP ||
	                 state->phase == SG_ROCKETJUMP_ARMED ||
	                 state->phase == SG_ROCKETJUMP_FLIGHT);
}

const char *SG_RocketJumpLiveFailureName(sg_rocketjump_failure_t failure)
{
	switch (failure)
	{
	case SG_ROCKETJUMP_FAILURE_NONE: return "none";
	case SG_ROCKETJUMP_FAILURE_ARGUMENT: return "argument";
	case SG_ROCKETJUMP_FAILURE_WITNESS: return "witness";
	case SG_ROCKETJUMP_FAILURE_SOURCE: return "source";
	case SG_ROCKETJUMP_FAILURE_LAUNCH_STATE: return "launch-state";
	case SG_ROCKETJUMP_FAILURE_TIMEOUT: return "timeout";
	case SG_ROCKETJUMP_FAILURE_PROJECTILE: return "projectile";
	case SG_ROCKETJUMP_FAILURE_IMPACT: return "impact";
	case SG_ROCKETJUMP_FAILURE_DAMAGE: return "damage";
	case SG_ROCKETJUMP_FAILURE_LANDING: return "landing";
	default: return "unknown";
	}
}
