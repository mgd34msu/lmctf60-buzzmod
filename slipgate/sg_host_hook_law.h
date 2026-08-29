/* Exact hook chronology shared by the publication and its tests. */
#ifndef SG_HOST_HOOK_LAW_H
#define SG_HOST_HOOK_LAW_H

#include <stdint.h>

#include "sg_host_pmove.h"
#include "sg_weapon_host_constants.h"

#define SG_HOST_HOOK_LAW_VERSION UINT32_C(1)
#define SG_HOST_HOOK_LAW_ID UINT64_C(0x484f4f4b4c573031)
#define SG_HOST_HOOK_ATTACHED_CADENCE UINT32_C(7)
#define SG_HOST_HOOK_NEAR_BITE_DISTANCE 120.0f
#define SG_HOST_HOOK_NEAR_BITE_GRAVITY_ZERO_DISTANCE 50.0f
#define SG_HOST_HOOK_CTF_NO_GRAP_DAMAGE UINT32_C(64)
#define SG_HOST_HOOK_MUZZLE_FORWARD_OFFSET 8U
#define SG_HOST_HOOK_MUZZLE_RIGHT_OFFSET 8U
#define SG_HOST_HOOK_MUZZLE_VIEW_OFFSET 8U
#define SG_HOST_HOOK_TRACE_EPSILON 10.0f

typedef enum sg_host_hook_phase_e
{
	SG_HOST_HOOK_IDLE = 0,
	SG_HOST_HOOK_IN_FLIGHT,
	SG_HOST_HOOK_ATTACHED,
	SG_HOST_HOOK_COAST
} sg_host_hook_phase_t;

typedef enum sg_host_hook_event_e
{
	SG_HOST_HOOK_FIRE = 1,
	SG_HOST_HOOK_FLIGHT_TICK,
	SG_HOST_HOOK_FLIGHT_HIT,
	SG_HOST_HOOK_ATTACHED_TICK,
	SG_HOST_HOOK_RELEASE,
	SG_HOST_HOOK_REFIRE
} sg_host_hook_event_t;

typedef enum sg_host_hook_target_kind_e
{
	SG_HOST_HOOK_TARGET_NONE = 0,
	SG_HOST_HOOK_TARGET_WORLD,
	SG_HOST_HOOK_TARGET_PLAYER,
	SG_HOST_HOOK_TARGET_BODYQUE,
	SG_HOST_HOOK_TARGET_FUNC,
	SG_HOST_HOOK_TARGET_INFO_FLAG,
	SG_HOST_HOOK_TARGET_OTHER
} sg_host_hook_target_kind_t;

typedef struct sg_host_hook_law_s
{
	uint32_t version;
	uint32_t trace_mask;
	uint32_t muzzle_forward_offset;
	uint32_t muzzle_right_offset;
	uint32_t muzzle_view_offset;
	uint32_t fire_speed;
	uint32_t pull_speed;
	uint32_t initial_damage;
	uint32_t attached_damage;
	uint32_t projectile_health;
	uint32_t attached_cadence_frames;
	/* LMCTF_FireHumanHook moves a blocked bolt back by this exact amount
	 * before invoking its touch callback. */
	float trace_epsilon;
	/* Runtime ctf_no_grapple_damage is part of the published damage law. */
	uint32_t no_grapple_damage;
	uint64_t identity;
	/* Weapon-side SV_AddGravity starts above 120 units.  The player Pmove
	 * path has a separate near-bite rule which zeroes pmove gravity below
	 * 50 units while the hook is attached. */
	float near_bite_distance;
	float near_bite_gravity_zero_distance;
} sg_host_hook_law_t;

typedef int (*sg_host_hook_live_capture_function_t)(sg_host_hook_law_t *law);

typedef struct sg_host_hook_observation_s
{
	sg_host_hook_event_t event;
	sg_host_hook_phase_t phase;
	sg_host_hook_target_kind_t target_kind;
	uint32_t frame;
	uint32_t last_damage_frame;
	uint64_t target_identity;
	float bite_distance;
	int muzzle_clear;
	/* FIRE accepts the spawn before this trace result is consumed.  When set,
	 * first_hit is the immediate touch from that same fire operation. */
	int immediate_hit;
	int trace_epsilon_applied;
	int first_hit;
	int sky;
	int owner_hit;
	int same_team;
	int target_dead;
	/* Set when the damage operation itself killed the target.  The live
	 * touch path aborts before publishing an attachment in this case. */
	int target_died_after_damage;
	int attack_held;
	int grounded;
	/* The identity returned by the first accepted hit.  An attached tick
	 * must carry this forward so a moving bite cannot silently retarget. */
	uint64_t attached_target_identity;
} sg_host_hook_observation_t;

/* A FIRE touch is accepted only after the publication's collision authority
 * has filled this record.  It is intentionally separate from the public
 * observation so caller-supplied target/sky booleans cannot manufacture an
 * attach. */
typedef struct sg_host_hook_collision_s
{
	int hit;
	int owner_hit;
	int sky;
	int same_team;
	int target_dead;
	int target_died_after_damage;
	int trace_epsilon_applied;
	sg_host_hook_target_kind_t target_kind;
	uint64_t target_identity;
} sg_host_hook_collision_t;

typedef struct sg_host_hook_fire_request_s
{
	/* The live bolt trace is from the owner origin to the spawned bolt. */
	float start[3];
	float end[3];
	sg_host_hook_phase_t phase;
	uint64_t owner_instance_id;
	int attack_held;
} sg_host_hook_fire_request_t;

typedef struct sg_host_hook_step_s
{
	int accepted;
	int first_hit;
	int attached;
	int released;
	int aborted;
	int coast_velocity;
	int pull_before_pmove;
	int pull_after_pmove;
	int gravity_applied;
	int gravity_zeroed;
	int zero_velocity_z;
	int zero_oldvelocity_z;
	int trace_epsilon_applied;
	uint32_t damage;
	uint32_t next_last_damage_frame;
	sg_host_hook_phase_t next_phase;
	sg_host_hook_target_kind_t target_kind;
	uint64_t target_identity;
	/* Owner-derived immediate trace record used by the live bot weapon to
	 * dispatch its existing touch callback without issuing a second trace. */
	int collision_hit;
	float collision_end[3];
	float collision_plane_normal[3];
	float collision_plane_distance;
	int32_t collision_plane_type;
	int32_t collision_surface_flags;
	uint64_t collision_instance_id;
} sg_host_hook_step_t;

void SG_HostHookLawDefault(sg_host_hook_law_t *law_out);
/* Implemented by the live weapon owner (p_weapon.c). */
int SG_HostHookLiveCapture(sg_host_hook_law_t *law_out);
int SG_HostHookLawValid(const sg_host_hook_law_t *law);
int SG_HostHookMuzzle(const float origin[3], float viewheight, int hand,
	const float forward[3], const float right[3], float start_out[3]);
int SG_HostHookStep(const sg_host_hook_law_t *law,
	const sg_host_hook_observation_t *observation,
	sg_host_hook_step_t *step_out);

/* Internal publication seam: collision must be derived by the owner before
 * FIRE chronology consumes it.  Non-FIRE events use the observation as-is. */
int SG_HostHookStepWithCollision(const sg_host_hook_law_t *law,
	const sg_host_hook_observation_t *observation,
	const sg_host_hook_collision_t *collision,
	sg_host_hook_step_t *step_out);

#endif /* SG_HOST_HOOK_LAW_H */
