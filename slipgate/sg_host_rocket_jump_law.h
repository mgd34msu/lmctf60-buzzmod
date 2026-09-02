/* Rocket jump: the host's damage, knockback, and movement laws composed into
 * one launch.  Nothing here is fitted or tuned; every term is a host constant
 * or the live gravity.
 *
 * The execution this describes: attack and jump pressed in the same command
 * while standing, aiming straight down.  That command's Pmove applies the
 * jump.  The same server frame's weapon think launches the rocket from the
 * now-airborne body; the rocket reaches the floor within the frame's entity
 * run and detonates while the body is one frame into the jump.  The splash
 * then adds the self-knockback to the airborne velocity.
 *
 * Firing first and jumping on a later command loses the jump: a grounded
 * body whose upward velocity exceeds the ground threshold is not on the
 * ground when the next Pmove looks for one.  So this is the launch, not one
 * of several. */
#ifndef SG_HOST_ROCKET_JUMP_LAW_H
#define SG_HOST_ROCKET_JUMP_LAW_H

#include <math.h>
#include <stdint.h>

#include "sg_host_engine_pmove.h"
#include "sg_weapon_host_constants.h"

typedef struct sg_host_rocket_jump_launch_s
{
	uint32_t lead_frames;      /* server frames from the command to the blast */
	float pre_blast_rise;      /* height above the floor at the blast */
	float pre_blast_velocity;  /* vertical velocity at the blast */
	float blast_distance;      /* blast to body centre */
	float kick_vertical;       /* self-knockback, vertical */
	float kick_lateral;        /* self-knockback, horizontal, to the left */
	float vertical_velocity;   /* pre_blast_velocity + kick_vertical */
	float rise;                /* peak height above the floor */
	int self_damage;           /* health taken before armor */
} sg_host_rocket_jump_launch_t;

/* One server frame of free flight under the host's substep Euler: gravity is
 * applied before each substep's move. */
static inline void SG_HostRocketJumpAdvanceFrame(float gravity,
	uint32_t substeps, float substep_seconds, float *height, float *velocity)
{
	uint32_t index;

	for (index = 0U; index < substeps; index++) {
		*velocity -= gravity * substep_seconds;
		*height += *velocity * substep_seconds;
	}
}

static inline int SG_HostRocketJumpLaunch(float gravity, uint32_t frame_ms,
	uint32_t substep_ms, int balanced, sg_host_rocket_jump_launch_t *out)
{
	const float frame_seconds = (float)frame_ms / 1000.0f;
	const float substep_seconds = (float)substep_ms / 1000.0f;
	const float rocket_frame_travel =
		(balanced ? (float)SG_HOST_ROCKET_BALANCED_SPEED :
			(float)SG_HOST_ROCKET_SPEED) * frame_seconds;
	/* Straight down, the forward offset points down and the right offset
	 * stays horizontal. */
	const float muzzle_above_origin = SG_HOST_ENGINE_PLAYER_VIEWHEIGHT -
		(float)SG_HOST_PROJECTILE_SOURCE_BELOW_VIEW -
		(float)SG_HOST_PROJECTILE_SOURCE_FORWARD;
	const float lateral = (float)SG_HOST_PROJECTILE_SOURCE_RIGHT;
	const float mass = SG_HOST_ENGINE_PLAYER_MASS <
		(float)SG_HOST_KNOCKBACK_MIN_MASS ? (float)SG_HOST_KNOCKBACK_MIN_MASS :
		SG_HOST_ENGINE_PLAYER_MASS;
	float height = 0.0f;
	float velocity = SG_HOST_ENGINE_JUMP_VELOCITY;
	float points;
	float origin_dz;
	float centre_dz;
	float direction_length;
	float scale;
	uint32_t substeps;
	uint32_t frame;
	int knockback;

	if (out == NULL || !(gravity > 0.0f) || !isfinite(gravity) ||
		frame_ms == 0U || substep_ms == 0U || frame_ms % substep_ms != 0U ||
		!(rocket_frame_travel > 0.0f))
		return 0;
	substeps = frame_ms / substep_ms;
	/* The jump's Pmove runs first; then the rocket needs to cover the muzzle
	 * height plus the body's rise to the floor before the frame ends.  If
	 * it cannot, the body rises another frame before the blast. */
	for (frame = 1U; ; frame++) {
		SG_HostRocketJumpAdvanceFrame(gravity, substeps, substep_seconds,
			&height, &velocity);
		if (height + muzzle_above_origin - SG_HOST_ENGINE_PLAYER_MINS_Z <=
			rocket_frame_travel)
			break;
		if (frame > substeps || velocity <= 0.0f)
			return 0;
	}
	origin_dz = height - SG_HOST_ENGINE_PLAYER_MINS_Z;
	centre_dz = height + 0.5f * (SG_HOST_ENGINE_PLAYER_MAXS_Z -
		SG_HOST_ENGINE_PLAYER_MINS_Z);
	out->blast_distance = sqrtf(lateral * lateral + centre_dz * centre_dz);
	if (balanced)
		points = (float)SG_HOST_ROCKET_BALANCED_SPLASH_DAMAGE *
			(1.0f - out->blast_distance /
				(float)SG_HOST_ROCKET_BALANCED_SPLASH_RADIUS);
	else
		points = (float)SG_HOST_ROCKET_SPLASH_DAMAGE -
			(float)SG_HOST_SPLASH_DISTANCE_FALLOFF * out->blast_distance;
	points *= (float)SG_HOST_RADIUS_SELF_SCALE;
	if (!(points > 0.0f))
		return 0;
	knockback = (int)points;
	direction_length = sqrtf(lateral * lateral + origin_dz * origin_dz);
	scale = (float)(balanced ? SG_HOST_BALANCED_SELF_KNOCKBACK_SCALE :
		SG_HOST_SELF_KNOCKBACK_SCALE) * (float)knockback / mass;
	out->lead_frames = frame;
	out->pre_blast_rise = height;
	out->pre_blast_velocity = velocity;
	out->kick_vertical = scale * origin_dz / direction_length;
	out->kick_lateral = scale * lateral / direction_length;
	out->vertical_velocity = velocity + out->kick_vertical;
	out->rise = height + (out->vertical_velocity * out->vertical_velocity) /
		(2.0f * gravity);
	out->self_damage = knockback;
	return isfinite(out->rise) && out->vertical_velocity > 0.0f;
}

/* Health actually lost: CheckArmor saves ceil(protection * damage), capped
 * by what the armor has left. */
static inline int SG_HostRocketJumpHealthCost(int self_damage,
	float armor_protection, int armor_count)
{
	int save = 0;

	if (self_damage <= 0)
		return 0;
	if (armor_count > 0 && armor_protection > 0.0f) {
		save = (int)ceilf(armor_protection * (float)self_damage);
		if (save > armor_count)
			save = armor_count;
	}
	return self_damage - save;
}

#endif /* SG_HOST_ROCKET_JUMP_LAW_H */
