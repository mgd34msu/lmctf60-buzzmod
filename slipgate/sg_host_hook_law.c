#include "sg_host_hook_law.h"

#include <math.h>
#include <string.h>

#define SG_HOST_HOOK_LEFT_HAND 1
#define SG_HOST_HOOK_CENTER_HAND 2

static int FiniteVector(const float value[3])
{
	return value && isfinite(value[0]) && isfinite(value[1]) &&
		isfinite(value[2]);
}

void SG_HostHookLawDefault(sg_host_hook_law_t *law_out)
{
	if (!law_out)
		return;
	memset(law_out, 0, sizeof(*law_out));
	law_out->version = SG_HOST_HOOK_LAW_VERSION;
	law_out->trace_mask = MASK_SHOT;
	law_out->muzzle_forward_offset = 8U;
	law_out->muzzle_right_offset = 8U;
	law_out->muzzle_view_offset = 8U;
	law_out->fire_speed = SG_HOST_HOOK_FIRE_SPEED;
	law_out->pull_speed = SG_HOST_HOOK_PULL_SPEED;
	law_out->initial_damage = SG_HOST_HOOK_INITIAL_DAMAGE;
	law_out->attached_damage = SG_HOST_HOOK_ATTACHED_DAMAGE;
	law_out->projectile_health = SG_HOST_HOOK_HEALTH;
	law_out->attached_cadence_frames = SG_HOST_HOOK_ATTACHED_CADENCE;
	law_out->no_grapple_damage = 0U;
	law_out->identity = SG_HOST_HOOK_LAW_ID;
	law_out->near_bite_distance = SG_HOST_HOOK_NEAR_BITE_DISTANCE;
	law_out->near_bite_gravity_zero_distance =
		SG_HOST_HOOK_NEAR_BITE_GRAVITY_ZERO_DISTANCE;
}

int SG_HostHookLawValid(const sg_host_hook_law_t *law)
{
	if (!law)
		return 0;
	return law->version == SG_HOST_HOOK_LAW_VERSION &&
		law->trace_mask == MASK_SHOT && law->muzzle_forward_offset == 8U &&
		law->muzzle_right_offset == 8U && law->muzzle_view_offset == 8U &&
		law->fire_speed == SG_HOST_HOOK_FIRE_SPEED &&
		law->pull_speed == SG_HOST_HOOK_PULL_SPEED &&
		law->initial_damage == SG_HOST_HOOK_INITIAL_DAMAGE &&
		law->attached_damage == SG_HOST_HOOK_ATTACHED_DAMAGE &&
		law->projectile_health == SG_HOST_HOOK_HEALTH &&
		law->attached_cadence_frames == SG_HOST_HOOK_ATTACHED_CADENCE &&
		law->no_grapple_damage <= 1U &&
		law->identity == SG_HOST_HOOK_LAW_ID &&
		memcmp(&law->near_bite_distance,
			&(const float){ SG_HOST_HOOK_NEAR_BITE_DISTANCE },
			sizeof(law->near_bite_distance)) == 0 &&
		memcmp(&law->near_bite_gravity_zero_distance,
			&(const float){ SG_HOST_HOOK_NEAR_BITE_GRAVITY_ZERO_DISTANCE },
			sizeof(law->near_bite_gravity_zero_distance)) == 0;
}

int SG_HostHookMuzzle(const float origin[3], float viewheight, int hand,
	const float forward[3], const float right[3], float start_out[3])
{
	float side;
	float forward_offset;
	float view_offset;

	if (!FiniteVector(origin) || !FiniteVector(forward) ||
		!FiniteVector(right) || !isfinite(viewheight) || !start_out)
		return 0;
	forward_offset = 8.0f;
	side = hand == SG_HOST_HOOK_LEFT_HAND ? -8.0f :
		hand == SG_HOST_HOOK_CENTER_HAND ? 0.0f : 8.0f;
	view_offset = viewheight - 8.0f;
	start_out[0] = origin[0] + forward[0] * forward_offset + right[0] * side;
	start_out[1] = origin[1] + forward[1] * forward_offset + right[1] * side;
	start_out[2] = origin[2] + forward[2] * forward_offset +
		right[2] * side + view_offset;
	return FiniteVector(start_out);
}

static int TargetAllowed(sg_host_hook_target_kind_t kind)
{
	return kind == SG_HOST_HOOK_TARGET_WORLD ||
		kind == SG_HOST_HOOK_TARGET_PLAYER ||
		kind == SG_HOST_HOOK_TARGET_BODYQUE ||
		kind == SG_HOST_HOOK_TARGET_FUNC ||
		kind == SG_HOST_HOOK_TARGET_INFO_FLAG;
}

static int DamageAllowed(const sg_host_hook_law_t *law,
	sg_host_hook_target_kind_t kind)
{
	return !law->no_grapple_damage || kind != SG_HOST_HOOK_TARGET_PLAYER;
}

int SG_HostHookStep(const sg_host_hook_law_t *law,
	const sg_host_hook_observation_t *observation,
	sg_host_hook_step_t *step_out)
{
	const sg_host_hook_observation_t *input = observation;

	if (!step_out)
		return 0;
	memset(step_out, 0, sizeof(*step_out));
	if (!SG_HostHookLawValid(law) || !input)
		return 0;
	step_out->next_phase = input->phase;
	step_out->target_kind = input->target_kind;
	step_out->target_identity = input->target_identity;
	if (input->event == SG_HOST_HOOK_FIRE ||
		input->event == SG_HOST_HOOK_REFIRE)
	{
		step_out->target_kind = SG_HOST_HOOK_TARGET_NONE;
		step_out->target_identity = 0U;
		if ((input->event == SG_HOST_HOOK_FIRE && input->phase != SG_HOST_HOOK_IDLE) ||
			(input->event == SG_HOST_HOOK_REFIRE && input->phase != SG_HOST_HOOK_COAST &&
				input->phase != SG_HOST_HOOK_IDLE) || !input->muzzle_clear ||
			!input->attack_held)
			return 1;
		step_out->accepted = 1;
		step_out->next_phase = SG_HOST_HOOK_IN_FLIGHT;
		return 1;
	}
	if (input->event == SG_HOST_HOOK_FLIGHT_TICK)
	{
		step_out->target_kind = SG_HOST_HOOK_TARGET_NONE;
		step_out->target_identity = 0U;
		step_out->accepted = input->phase == SG_HOST_HOOK_IN_FLIGHT;
		return 1;
	}
	if (input->event == SG_HOST_HOOK_FLIGHT_HIT)
	{
		if (input->phase != SG_HOST_HOOK_IN_FLIGHT || !input->first_hit)
			return 1;
		if (input->owner_hit)
			return 1;
		if (input->sky || input->same_team || input->target_dead ||
			!TargetAllowed(input->target_kind) || input->target_identity == 0U)
		{
			step_out->aborted = 1;
			step_out->next_phase = SG_HOST_HOOK_IDLE;
			step_out->target_kind = SG_HOST_HOOK_TARGET_NONE;
			step_out->target_identity = 0U;
			return 1;
		}
		step_out->accepted = 1;
		step_out->first_hit = 1;
		if (DamageAllowed(law, input->target_kind))
			step_out->damage = law->initial_damage;
		if (input->target_died_after_damage)
		{
			step_out->aborted = 1;
			step_out->next_phase = SG_HOST_HOOK_IDLE;
			step_out->target_kind = SG_HOST_HOOK_TARGET_NONE;
			step_out->target_identity = 0U;
			return 1;
		}
		step_out->attached = 1;
		step_out->next_phase = SG_HOST_HOOK_ATTACHED;
		return 1;
	}
	if (input->event == SG_HOST_HOOK_ATTACHED_TICK)
	{
		if (input->phase != SG_HOST_HOOK_ATTACHED)
			return 1;
		/* The real hook ignores an owner touch immediately, even if the
		 * observation cannot identify the attached target. */
		if (input->owner_hit)
			return 1;
		if (input->attached_target_identity == 0U)
		{
			step_out->aborted = 1;
			step_out->next_phase = SG_HOST_HOOK_IDLE;
			step_out->target_kind = SG_HOST_HOOK_TARGET_NONE;
			step_out->target_identity = 0U;
			return 1;
		}
		/* The real hook ignores a new touch once hook_target is set. */
		if (input->target_identity != input->attached_target_identity)
		{
			step_out->target_kind = SG_HOST_HOOK_TARGET_NONE;
			step_out->target_identity = input->attached_target_identity;
			return 1;
		}
		if (input->sky || input->same_team || input->target_dead ||
			!TargetAllowed(input->target_kind) || input->target_identity == 0U ||
			!isfinite(input->bite_distance) || input->bite_distance < 0.0f)
		{
			step_out->aborted = 1;
			step_out->next_phase = SG_HOST_HOOK_IDLE;
			step_out->target_kind = SG_HOST_HOOK_TARGET_NONE;
			step_out->target_identity = 0U;
			return 1;
		}
		step_out->accepted = 1;
		if (DamageAllowed(law, input->target_kind) &&
			(input->frame % law->attached_cadence_frames) == 0U &&
			input->frame != input->last_damage_frame)
		{
			step_out->damage = law->attached_damage;
			step_out->next_last_damage_frame = input->frame;
		}
		if (input->target_died_after_damage)
		{
			step_out->aborted = 1;
			step_out->next_phase = SG_HOST_HOOK_IDLE;
			step_out->target_kind = SG_HOST_HOOK_TARGET_NONE;
			step_out->target_identity = 0U;
			return 1;
		}
		/* The live player path runs Think_Weapon after gi.Pmove.  The
		 * resulting velocity is therefore the input to the next Pmove, not
		 * a pre-Pmove mutation of this frame. */
		step_out->pull_after_pmove = 1;
		step_out->gravity_applied = input->bite_distance >
			law->near_bite_distance;
		step_out->gravity_zeroed = input->bite_distance <
			law->near_bite_gravity_zero_distance;
		return 1;
	}
	if (input->event == SG_HOST_HOOK_RELEASE)
	{
		if (input->phase != SG_HOST_HOOK_IN_FLIGHT &&
			input->phase != SG_HOST_HOOK_ATTACHED)
			return 1;
		step_out->accepted = 1;
		step_out->released = 1;
		step_out->coast_velocity = 1;
		step_out->target_kind = SG_HOST_HOOK_TARGET_NONE;
		step_out->target_identity = 0U;
		step_out->next_phase = SG_HOST_HOOK_COAST;
		return 1;
	}
	return 0;
}
