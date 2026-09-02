#include "sg_rune_law.h"

#include <math.h>
#include <string.h>

#include "sg_engine_facts.h"
#include "sg_rune_crc.h"

void SG_RuneLawEngine(sg_rune_law_t *law, float gravity)
{
	if (!law)
		return;
	memset(law, 0, sizeof(*law));
	law->standing_mins[0] = law->standing_mins[1] = -SG_FACT_BODY_HALF_WIDTH;
	law->standing_mins[2] = SG_FACT_BODY_MINS_Z;
	law->standing_maxs[0] = law->standing_maxs[1] = SG_FACT_BODY_HALF_WIDTH;
	law->standing_maxs[2] = SG_FACT_BODY_MAXS_Z;
	memcpy(law->crouching_mins, law->standing_mins, sizeof(law->crouching_mins));
	memcpy(law->crouching_maxs, law->standing_maxs, sizeof(law->crouching_maxs));
	law->crouching_maxs[2] = SG_FACT_BODY_DUCK_MAXS_Z;
	law->standing_view = SG_FACT_BODY_VIEW;
	law->crouching_view = SG_FACT_BODY_DUCK_VIEW;
	law->gravity = gravity;
	law->ground_acceleration = SG_FACT_ACCELERATE;
	law->air_acceleration = SG_FACT_AIR_ACCELERATE;
	law->water_acceleration = SG_FACT_WATER_ACCELERATE;
	law->hook_acceleration = SG_FACT_HOOK_PULL_SPEED;
	law->water_drag = SG_FACT_WATER_FRICTION;
	law->max_velocity = SG_FACT_MAX_SPEED;
	law->water_velocity = SG_FACT_WATER_SPEED;
	law->duck_velocity = SG_FACT_DUCK_SPEED;
	law->jump_velocity = SG_FACT_JUMP_VELOCITY;
	law->step_size = SG_FACT_STEP_SIZE;
	law->hook_fire_speed = SG_FACT_HOOK_FIRE_SPEED;
	law->hook_pull_speed = SG_FACT_HOOK_PULL_SPEED;
	law->hook_near_bite = SG_FACT_HOOK_NEAR_BITE;
	law->hook_hold = SG_FACT_HOOK_HOLD;
	law->frame_ms = SG_FACT_FRAME_MS;
	law->substep_ms = SG_FACT_PMOVE_SUBSTEP_MS;
}

static int Finite3(const float v[3])
{
	return isfinite(v[0]) && isfinite(v[1]) && isfinite(v[2]);
}

int SG_RuneLawValid(const sg_rune_law_t *law)
{
	if (!law)
		return 0;
	return Finite3(law->standing_mins) && Finite3(law->standing_maxs) &&
		Finite3(law->crouching_mins) && Finite3(law->crouching_maxs) &&
		law->standing_maxs[2] > law->standing_mins[2] &&
		law->crouching_maxs[2] > law->crouching_mins[2] &&
		isfinite(law->gravity) && law->gravity > 0.0f &&
		isfinite(law->max_velocity) && law->max_velocity > 0.0f &&
		isfinite(law->water_velocity) && law->water_velocity > 0.0f &&
		isfinite(law->jump_velocity) && law->jump_velocity > 0.0f &&
		isfinite(law->step_size) && law->step_size >= 0.0f &&
		isfinite(law->hook_fire_speed) && isfinite(law->hook_pull_speed) &&
		law->frame_ms > 0U && law->substep_ms > 0U &&
		law->substep_ms <= law->frame_ms;
}

int SG_RuneLawSame(const sg_rune_law_t *a, const sg_rune_law_t *b)
{
	return a && b && memcmp(a, b, sizeof(*a)) == 0;
}

uint32_t SG_RuneLawCrc(const sg_rune_law_t *law)
{
	return law ? SG_RuneCrc32((const uint8_t *)law, sizeof(*law)) : 0U;
}

void SG_RuneLawHull(const sg_rune_law_t *law, int crouching,
	const float **mins_out, const float **maxs_out, float *view_out)
{
	if (mins_out)
		*mins_out = crouching ? law->crouching_mins : law->standing_mins;
	if (maxs_out)
		*maxs_out = crouching ? law->crouching_maxs : law->standing_maxs;
	if (view_out)
		*view_out = crouching ? law->crouching_view : law->standing_view;
}

float SG_RuneLawHookPullSpeed(const sg_rune_law_t *law, float distance)
{
	if (!law || !(distance > SG_FACT_HOOK_BAND_1))
		return 0.0f;
	if (distance > law->hook_near_bite)
		return law->hook_pull_speed;
	if (distance > SG_FACT_HOOK_BAND_5)
		return distance * 5.0f;
	if (distance > SG_FACT_HOOK_BAND_4)
		return distance * 4.0f;
	if (distance > SG_FACT_HOOK_BAND_3)
		return distance * 3.0f;
	if (distance > SG_FACT_HOOK_BAND_2)
		return distance * 2.0f;
	return distance;
}

/* The rocket jump, step by step as the game runs it:
 *
 *   1. The command carries attack and jump.  Its pmove applies the jump
 *      first: the body leaves the floor at the jump velocity and rises
 *      through the frame's substeps, gravity taken before each move.
 *   2. The same frame's weapon think fires the rocket from the muzzle
 *      (forward 8, right 8, 8 under the eye; aiming down, forward is
 *      down), so the rocket starts just over the origin and flies down at
 *      its speed; the floor is a body's depth below, reached within the
 *      frame, and the blast happens at the floor beside the feet.
 *   3. The blast's damage to its own shooter is the splash damage less
 *      half the distance from the blast to the body's origin, then halved
 *      for being one's own; the knockback is that many points, scaled by
 *      1600 over the body's mass, along the line from the blast to the
 *      body.  That kick adds to the jump's velocity where the body is by
 *      then. */
int SG_RuneLawRocketJump(const sg_rune_law_t *law, sg_rune_rocket_jump_t *out)
{
	float substep, height, velocity, muzzle_over_origin, floor_below_muzzle;
	float blast_dx, blast_dz, blast_distance, points, kick, mass;
	uint32_t substeps, i;

	if (!out)
		return 0;
	memset(out, 0, sizeof(*out));
	if (!law || !(law->gravity > 0.0f) || law->substep_ms == 0U)
		return 0;
	substep = (float)law->substep_ms / 1000.0f;
	substeps = law->frame_ms / law->substep_ms;
	if (substeps == 0U)
		substeps = 1U;
	/* 1. One frame of the jump. */
	height = 0.0f;
	velocity = law->jump_velocity;
	for (i = 0U; i < substeps; i++)
	{
		velocity -= law->gravity * substep;
		height += velocity * substep;
	}
	/* 2. The rocket's fall to the floor: from the muzzle, which sits
	 *    (view - 8) - 8 over the origin when the aim is straight down. */
	muzzle_over_origin = (law->standing_view - SG_FACT_MUZZLE_BELOW_VIEW) -
		SG_FACT_MUZZLE_FORWARD;
	floor_below_muzzle = muzzle_over_origin - law->standing_mins[2];
	out->lead_seconds = (float)law->frame_ms / 1000.0f;
	if (floor_below_muzzle / SG_FACT_ROCKET_SPEED > out->lead_seconds)
		out->lead_seconds += floor_below_muzzle / SG_FACT_ROCKET_SPEED;
	out->pre_blast_rise = height;
	/* 3. The blast at the floor, the muzzle's offset to the right, against
	 *    the body one frame into its jump. */
	blast_dx = SG_FACT_MUZZLE_RIGHT;
	blast_dz = height - law->standing_mins[2];   /* floor to origin */
	blast_distance = sqrtf(blast_dx * blast_dx + blast_dz * blast_dz);
	points = SG_FACT_ROCKET_SPLASH_DAMAGE - SG_FACT_RADIUS_FALLOFF * blast_distance;
	if (points <= 0.0f)
		return 0;
	points *= SG_FACT_RADIUS_SELF_SCALE;
	out->self_damage = points;
	mass = SG_FACT_BODY_MASS < SG_FACT_KNOCKBACK_MIN_MASS ? SG_FACT_KNOCKBACK_MIN_MASS :
		SG_FACT_BODY_MASS;
	kick = SG_FACT_SELF_KNOCKBACK_SCALE * points / mass;
	out->vertical_velocity = velocity + kick * (blast_dz / blast_distance);
	out->lateral_velocity = kick * (blast_dx / blast_distance);
	if (!(out->vertical_velocity > 0.0f))
		return 0;
	out->rise = height + (out->vertical_velocity * out->vertical_velocity) /
		(2.0f * law->gravity);
	return 1;
}
