/* Immutable weapon facts and the shared static-geometry query boundary. */
#ifndef SG_WEAPON_EFFECT_PROFILE_H
#define SG_WEAPON_EFFECT_PROFILE_H

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "sg_rune_model.h"
#include "sg_rune_v2_wire.h"

#ifdef WEAP_BALANCE_OK
#define SG_WEAPON_BALANCE_COMPILED 1U
#else
#define SG_WEAPON_BALANCE_COMPILED 0U
#endif

typedef enum sg_weapon_profile_id_e
{
	SG_WEAPON_PROFILE_BLASTER = 1,
	SG_WEAPON_PROFILE_SHOTGUN,
	SG_WEAPON_PROFILE_SUPER_SHOTGUN,
	SG_WEAPON_PROFILE_MACHINEGUN,
	SG_WEAPON_PROFILE_CHAINGUN,
	SG_WEAPON_PROFILE_GRENADE_LAUNCHER,
	SG_WEAPON_PROFILE_ROCKET_LAUNCHER,
	SG_WEAPON_PROFILE_HYPERBLASTER,
	SG_WEAPON_PROFILE_RAILGUN,
	SG_WEAPON_PROFILE_BFG,
	SG_WEAPON_PROFILE_PLASMA_REFLECT,
	SG_WEAPON_PROFILE_PLASMA_SPREAD,
	SG_WEAPON_PROFILE_HAND_GRENADE,
	SG_WEAPON_PROFILE_HOOK,
	SG_WEAPON_PROFILE_COUNT
} sg_weapon_profile_id_t;

typedef enum sg_weapon_family_e
{
	SG_WEAPON_FAMILY_HITSCAN = 0,
	SG_WEAPON_FAMILY_SPREAD,
	SG_WEAPON_FAMILY_STRAIGHT_PROJECTILE,
	SG_WEAPON_FAMILY_ROCKET_SPLASH,
	SG_WEAPON_FAMILY_GRENADE_BOUNCE,
	SG_WEAPON_FAMILY_HYPERBLASTER,
	SG_WEAPON_FAMILY_BFG,
	SG_WEAPON_FAMILY_PLASMA_REFLECT,
	SG_WEAPON_FAMILY_PLASMA_SPREAD,
	SG_WEAPON_FAMILY_SPECIAL,
	SG_WEAPON_FAMILY_COUNT
} sg_weapon_family_t;

typedef uint32_t sg_weapon_effect_flag_t;
enum
{
	SG_WEAPON_EFFECT_HITSCAN = UINT32_C(1) << 0,
	SG_WEAPON_EFFECT_SPREAD = UINT32_C(1) << 1,
	SG_WEAPON_EFFECT_PROJECTILE = UINT32_C(1) << 2,
	SG_WEAPON_EFFECT_SPLASH = UINT32_C(1) << 3,
	SG_WEAPON_EFFECT_BOUNCE = UINT32_C(1) << 4,
	SG_WEAPON_EFFECT_PENETRATION = UINT32_C(1) << 5,
	SG_WEAPON_EFFECT_MULTI_PROJECTILE = UINT32_C(1) << 7,
	SG_WEAPON_EFFECT_SECONDARY_AREA = UINT32_C(1) << 8,
	SG_WEAPON_EFFECT_SPECIAL = UINT32_C(1) << 9,
	SG_WEAPON_EFFECT_PERIODIC_RAY = UINT32_C(1) << 10
};

#define SG_WEAPON_EFFECT_MASK \
	(SG_WEAPON_EFFECT_HITSCAN | SG_WEAPON_EFFECT_SPREAD | \
	 SG_WEAPON_EFFECT_PROJECTILE | SG_WEAPON_EFFECT_SPLASH | \
	 SG_WEAPON_EFFECT_BOUNCE | SG_WEAPON_EFFECT_PENETRATION | \
	 SG_WEAPON_EFFECT_MULTI_PROJECTILE | \
	 SG_WEAPON_EFFECT_SECONDARY_AREA | SG_WEAPON_EFFECT_SPECIAL | \
	 SG_WEAPON_EFFECT_PERIODIC_RAY)

typedef enum sg_weapon_splash_kernel_e
{
	SG_WEAPON_SPLASH_NONE = 0,
	SG_WEAPON_SPLASH_LINEAR_HALF_DISTANCE,
	SG_WEAPON_SPLASH_NORMALIZED_LINEAR,
	SG_WEAPON_SPLASH_SQRT_NORMALIZED,
	SG_WEAPON_SPLASH_KERNEL_COUNT
} sg_weapon_splash_kernel_t;

typedef enum sg_weapon_splash_owner_e
{
	SG_WEAPON_SPLASH_OWNER_SELF_SCALE = 0,
	SG_WEAPON_SPLASH_OWNER_EXCLUDED,
	SG_WEAPON_SPLASH_OWNER_COUNT
} sg_weapon_splash_owner_t;

typedef enum sg_weapon_damage_dependency_e
{
	SG_WEAPON_DAMAGE_SHOT_BOUND = 0,
	SG_WEAPON_DAMAGE_IMPACT_GLOBAL_QUAD,
	SG_WEAPON_DAMAGE_DEPENDENCY_COUNT
} sg_weapon_damage_dependency_t;

typedef enum sg_weapon_cadence_kind_e
{
	SG_WEAPON_CADENCE_FIXED = 0,
	SG_WEAPON_CADENCE_HELD_LOOP,
	SG_WEAPON_CADENCE_COOKED_RELEASE,
	SG_WEAPON_CADENCE_STATE_DEPENDENT,
	SG_WEAPON_CADENCE_KIND_COUNT
} sg_weapon_cadence_kind_t;

typedef struct sg_weapon_splash_law_s
{
	sg_weapon_splash_kernel_t kernel;
	sg_weapon_splash_owner_t owner;
	float owner_scale;
} sg_weapon_splash_law_t;

typedef struct sg_weapon_ammo_law_s
{
	uint16_t ready_minimum;
	uint16_t live_fire_minimum;
	uint16_t debit;
	uint16_t debit_maximum;
	uint16_t infinite_ammo_debit;
} sg_weapon_ammo_law_t;

typedef struct sg_weapon_timing_law_s
{
	uint16_t activate_last_frame;
	uint16_t fire_last_frame;
	uint16_t idle_last_frame;
	uint16_t deactivate_first_frame;
	uint16_t deactivate_last_frame;
	uint16_t first_effect_frame;
	uint16_t last_effect_frame;
	uint32_t effect_interval_ms;
	uint32_t resolved_switch_in_ms;
	uint32_t resolved_switch_out_ms;
	uint8_t activation_frame_step;
	uint8_t fast_switch_bypasses_activation;
	uint8_t fast_switch_bypasses_deactivation;
} sg_weapon_timing_law_t;

typedef struct sg_weapon_profile_s
{
	sg_weapon_profile_id_t id;
	sg_weapon_family_t family;
	sg_weapon_effect_flag_t effects;
	float projectile_speed;
	float projectile_speed_max;
	float ray_distance;
	float projectile_retire_distance;
	float projectile_half_extent;
	float launch_vertical_speed;
	float launch_jitter;
	float gravity_scale;
	float horizontal_spread;
	float vertical_spread;
	float yaw_spread_degrees;
	float splash_radius;
	float splash_radius_max;
	float secondary_splash_radius;
	float direct_damage;
	float direct_damage_max;
	float splash_damage;
	float splash_damage_max;
	float secondary_splash_damage;
	float periodic_ray_damage;
	float periodic_ray_radius;
	float periodic_ray_distance;
	float self_damage_scale;
	float teammate_risk_scale;
	float auxiliary_trace_damage;
	float auxiliary_horizontal_spread;
	float auxiliary_vertical_spread;
	uint32_t windup_ms;
	uint32_t cook_ms;
	uint32_t fuse_ms;
	uint32_t projectile_lifetime_ms;
	uint32_t cadence_ms;
	uint32_t periodic_ray_interval_ms;
	uint16_t projectile_count_min;
	uint16_t projectile_count_max;
	uint16_t auxiliary_trace_count;
	uint16_t hook_initial_damage;
	uint16_t hook_attached_damage;
	uint16_t hook_health;
	uint16_t hook_pull_speed;
	sg_weapon_splash_law_t splash;
	sg_weapon_splash_law_t secondary_splash;
	sg_weapon_ammo_law_t ammo;
	sg_weapon_timing_law_t timing;
	sg_weapon_damage_dependency_t damage_dependency;
	sg_weapon_cadence_kind_t cadence_kind;
	uint8_t requires_live_trace;
	uint8_t supports_occluded_impact;
	uint8_t auxiliary_traces_penetrate;
	uint8_t resolved;
	uint8_t reserved;
	uint64_t build_identity;
	uint64_t physics_abi_id;
} sg_weapon_profile_t;

typedef struct sg_weapon_law_input_s
{
	uint64_t build_identity;
	uint64_t physics_abi_id;
	uint8_t weapon_balance_compiled;
	uint8_t weapon_balance_enabled;
	uint8_t quad_active;
	uint8_t rail_match_active;
	uint8_t deathmatch_active;
	uint8_t fast_switch_enabled;
} sg_weapon_law_input_t;

static inline int SG_WeaponFamilyValid(sg_weapon_family_t family)
{
	return family >= SG_WEAPON_FAMILY_HITSCAN &&
		family < SG_WEAPON_FAMILY_COUNT;
}

static inline int SG_WeaponFloatValid(float value)
{
	return isfinite(value) != 0;
}

static inline int SG_WeaponProfileValid(const sg_weapon_profile_t *profile)
{
	if (!profile || profile->id <= 0 || profile->id >= SG_WEAPON_PROFILE_COUNT ||
	    !SG_WeaponFamilyValid(profile->family) || profile->effects == 0U ||
	    (profile->effects & ~(uint32_t)SG_WEAPON_EFFECT_MASK) != 0U ||
	    profile->requires_live_trace != 1U ||
	    profile->supports_occluded_impact > 1U ||
	    profile->resolved > 1U || profile->reserved != 0U ||
	    ((profile->resolved != 0U) !=
	     (profile->build_identity != 0U && profile->physics_abi_id != 0U)) ||
	    profile->projectile_count_min == 0U ||
	    profile->projectile_count_max < profile->projectile_count_min ||
	    profile->ammo.debit_maximum < profile->ammo.debit ||
	    profile->ammo.live_fire_minimum > profile->ammo.ready_minimum ||
	    profile->splash.kernel < SG_WEAPON_SPLASH_NONE ||
	    profile->splash.kernel >= SG_WEAPON_SPLASH_KERNEL_COUNT ||
	    profile->secondary_splash.kernel < SG_WEAPON_SPLASH_NONE ||
	    profile->secondary_splash.kernel >= SG_WEAPON_SPLASH_KERNEL_COUNT ||
	    profile->splash.owner < SG_WEAPON_SPLASH_OWNER_SELF_SCALE ||
	    profile->splash.owner >= SG_WEAPON_SPLASH_OWNER_COUNT ||
	    profile->secondary_splash.owner < SG_WEAPON_SPLASH_OWNER_SELF_SCALE ||
	    profile->secondary_splash.owner >= SG_WEAPON_SPLASH_OWNER_COUNT ||
	    profile->damage_dependency < SG_WEAPON_DAMAGE_SHOT_BOUND ||
	    profile->damage_dependency >= SG_WEAPON_DAMAGE_DEPENDENCY_COUNT ||
	    profile->cadence_kind < SG_WEAPON_CADENCE_FIXED ||
	    profile->cadence_kind >= SG_WEAPON_CADENCE_KIND_COUNT ||
	    profile->timing.fast_switch_bypasses_activation > 1U ||
	    profile->timing.fast_switch_bypasses_deactivation > 1U ||
	    profile->auxiliary_traces_penetrate > 1U ||
	    profile->projectile_speed < 0.0f || profile->ray_distance < 0.0f ||
	    profile->projectile_speed_max < profile->projectile_speed ||
	    profile->projectile_retire_distance < 0.0f ||
	    profile->projectile_half_extent < 0.0f ||
	    profile->launch_vertical_speed < 0.0f || profile->launch_jitter < 0.0f ||
	    profile->gravity_scale < 0.0f || profile->horizontal_spread < 0.0f ||
	    profile->vertical_spread < 0.0f ||
	    profile->yaw_spread_degrees < 0.0f || profile->splash_radius < 0.0f ||
	    profile->splash_radius_max < profile->splash_radius ||
	    profile->secondary_splash_radius < 0.0f ||
	    profile->direct_damage < 0.0f ||
	    profile->direct_damage_max < profile->direct_damage ||
	    profile->splash_damage < 0.0f ||
	    profile->splash_damage_max < profile->splash_damage ||
	    profile->secondary_splash_damage < 0.0f ||
	    profile->periodic_ray_damage < 0.0f ||
	    profile->periodic_ray_radius < 0.0f ||
	    profile->periodic_ray_distance < 0.0f ||
	    profile->self_damage_scale < 0.0f ||
	    profile->teammate_risk_scale < 0.0f ||
	    profile->auxiliary_trace_damage < 0.0f ||
	    profile->auxiliary_horizontal_spread < 0.0f ||
	    profile->auxiliary_vertical_spread < 0.0f ||
	    profile->splash.owner_scale < 0.0f ||
	    profile->secondary_splash.owner_scale < 0.0f ||
	    !SG_WeaponFloatValid(profile->projectile_speed) ||
	    !SG_WeaponFloatValid(profile->projectile_speed_max) ||
	    !SG_WeaponFloatValid(profile->ray_distance) ||
	    !SG_WeaponFloatValid(profile->projectile_retire_distance) ||
	    !SG_WeaponFloatValid(profile->projectile_half_extent) ||
	    !SG_WeaponFloatValid(profile->launch_vertical_speed) ||
	    !SG_WeaponFloatValid(profile->launch_jitter) ||
	    !SG_WeaponFloatValid(profile->gravity_scale) ||
	    !SG_WeaponFloatValid(profile->horizontal_spread) ||
	    !SG_WeaponFloatValid(profile->vertical_spread) ||
	    !SG_WeaponFloatValid(profile->yaw_spread_degrees) ||
	    !SG_WeaponFloatValid(profile->splash_radius) ||
	    !SG_WeaponFloatValid(profile->splash_radius_max) ||
	    !SG_WeaponFloatValid(profile->secondary_splash_radius) ||
	    !SG_WeaponFloatValid(profile->direct_damage) ||
	    !SG_WeaponFloatValid(profile->direct_damage_max) ||
	    !SG_WeaponFloatValid(profile->splash_damage) ||
	    !SG_WeaponFloatValid(profile->splash_damage_max) ||
	    !SG_WeaponFloatValid(profile->secondary_splash_damage) ||
	    !SG_WeaponFloatValid(profile->periodic_ray_damage) ||
	    !SG_WeaponFloatValid(profile->periodic_ray_radius) ||
	    !SG_WeaponFloatValid(profile->periodic_ray_distance) ||
	    !SG_WeaponFloatValid(profile->self_damage_scale) ||
	    !SG_WeaponFloatValid(profile->teammate_risk_scale))
		return 0;
	if (profile->cadence_kind != SG_WEAPON_CADENCE_STATE_DEPENDENT &&
	    profile->cadence_ms == 0U)
		return 0;
	if (!SG_WeaponFloatValid(profile->auxiliary_trace_damage) ||
	    !SG_WeaponFloatValid(profile->auxiliary_horizontal_spread) ||
	    !SG_WeaponFloatValid(profile->auxiliary_vertical_spread) ||
	    !SG_WeaponFloatValid(profile->splash.owner_scale) ||
	    !SG_WeaponFloatValid(profile->secondary_splash.owner_scale))
		return 0;
	if ((profile->effects & SG_WEAPON_EFFECT_PROJECTILE) != 0U &&
	    profile->projectile_speed_max <= 0.0f)
		return 0;
	if ((profile->effects & SG_WEAPON_EFFECT_PROJECTILE) != 0U &&
	    profile->projectile_speed <= 0.0f)
		return 0;
	if ((profile->effects & SG_WEAPON_EFFECT_HITSCAN) != 0U &&
	    profile->ray_distance <= 0.0f)
		return 0;
	if ((profile->effects & SG_WEAPON_EFFECT_SPLASH) != 0U &&
	    (profile->splash_radius <= 0.0f || profile->splash_damage <= 0.0f ||
	     profile->splash.kernel == SG_WEAPON_SPLASH_NONE))
		return 0;
	if ((profile->effects & SG_WEAPON_EFFECT_SPLASH) == 0U &&
	    profile->splash.kernel != SG_WEAPON_SPLASH_NONE)
		return 0;
	if ((profile->effects & SG_WEAPON_EFFECT_SECONDARY_AREA) != 0U &&
	    (profile->secondary_splash_radius <= 0.0f ||
	     profile->secondary_splash_damage <= 0.0f ||
	     profile->secondary_splash.kernel == SG_WEAPON_SPLASH_NONE))
		return 0;
	if ((profile->effects & SG_WEAPON_EFFECT_SECONDARY_AREA) == 0U &&
	    profile->secondary_splash.kernel != SG_WEAPON_SPLASH_NONE)
		return 0;
	if (profile->splash.owner == SG_WEAPON_SPLASH_OWNER_EXCLUDED &&
	    profile->splash.owner_scale != 0.0f)
		return 0;
	if (profile->secondary_splash.owner == SG_WEAPON_SPLASH_OWNER_EXCLUDED &&
	    profile->secondary_splash.owner_scale != 0.0f)
		return 0;
	if (profile->auxiliary_trace_count == 0U &&
	    (profile->auxiliary_trace_damage != 0.0f ||
	     profile->auxiliary_horizontal_spread != 0.0f ||
	     profile->auxiliary_vertical_spread != 0.0f))
		return 0;
	if (profile->auxiliary_trace_count != 0U &&
	    (profile->auxiliary_trace_damage <= 0.0f ||
	     profile->auxiliary_horizontal_spread <= 0.0f ||
	     profile->auxiliary_vertical_spread <= 0.0f))
		return 0;
	if (profile->timing.activate_last_frame != 0U &&
	    (profile->timing.fire_last_frame <=
	      profile->timing.activate_last_frame ||
	     profile->timing.idle_last_frame <= profile->timing.fire_last_frame ||
	     profile->timing.deactivate_last_frame <=
	      profile->timing.idle_last_frame ||
	     profile->timing.deactivate_first_frame <=
	      profile->timing.idle_last_frame ||
	     profile->timing.deactivate_first_frame >
	      profile->timing.deactivate_last_frame ||
	     profile->timing.activation_frame_step == 0U ||
	     profile->timing.first_effect_frame <=
	      profile->timing.activate_last_frame ||
	     profile->timing.last_effect_frame <
	      profile->timing.first_effect_frame ||
	     profile->timing.last_effect_frame > profile->timing.fire_last_frame))
		return 0;
	if ((profile->effects & SG_WEAPON_EFFECT_PERIODIC_RAY) != 0U &&
	    (profile->periodic_ray_damage <= 0.0f ||
	     profile->periodic_ray_radius <= 0.0f ||
	     profile->periodic_ray_distance <= 0.0f ||
	     profile->periodic_ray_interval_ms == 0U))
		return 0;
	if ((profile->effects & SG_WEAPON_EFFECT_MULTI_PROJECTILE) != 0U &&
	    profile->projectile_count_max <= 1U)
		return 0;
	if (profile->family == SG_WEAPON_FAMILY_HITSCAN &&
	    (profile->effects & SG_WEAPON_EFFECT_HITSCAN) == 0U)
		return 0;
	if (profile->family == SG_WEAPON_FAMILY_SPREAD &&
	    (profile->effects & SG_WEAPON_EFFECT_SPREAD) == 0U)
		return 0;
	if ((profile->family == SG_WEAPON_FAMILY_STRAIGHT_PROJECTILE ||
	     profile->family == SG_WEAPON_FAMILY_HYPERBLASTER ||
	     profile->family == SG_WEAPON_FAMILY_PLASMA_REFLECT ||
	     profile->family == SG_WEAPON_FAMILY_PLASMA_SPREAD) &&
	    (profile->effects & SG_WEAPON_EFFECT_PROJECTILE) == 0U)
		return 0;
	if ((profile->family == SG_WEAPON_FAMILY_ROCKET_SPLASH ||
	     profile->family == SG_WEAPON_FAMILY_BFG) &&
	    ((profile->effects & SG_WEAPON_EFFECT_PROJECTILE) == 0U ||
	     (profile->effects & SG_WEAPON_EFFECT_SPLASH) == 0U))
		return 0;
	if ((profile->family == SG_WEAPON_FAMILY_GRENADE_BOUNCE ||
	     profile->family == SG_WEAPON_FAMILY_PLASMA_REFLECT) &&
	    (profile->effects & SG_WEAPON_EFFECT_BOUNCE) == 0U)
		return 0;
	if (profile->family == SG_WEAPON_FAMILY_PLASMA_SPREAD &&
	    (profile->effects & SG_WEAPON_EFFECT_MULTI_PROJECTILE) == 0U)
		return 0;
	if (profile->id == SG_WEAPON_PROFILE_HOOK)
	{
		if (profile->family != SG_WEAPON_FAMILY_SPECIAL ||
		    (profile->effects & SG_WEAPON_EFFECT_SPECIAL) == 0U ||
		    profile->hook_initial_damage == 0U ||
		    profile->hook_attached_damage == 0U ||
		    profile->hook_health == 0U || profile->hook_pull_speed == 0U)
			return 0;
	}
	else if (profile->hook_initial_damage != 0U ||
		 profile->hook_attached_damage != 0U || profile->hook_health != 0U ||
		 profile->hook_pull_speed != 0U)
		return 0;
	return 1;
}

typedef uint32_t sg_weapon_static_relation_t;
enum
{
	SG_WEAPON_STATIC_DIRECT_VISIBILITY = UINT32_C(1) << 0,
	SG_WEAPON_STATIC_PROJECTILE_CORRIDOR = UINT32_C(1) << 1,
	SG_WEAPON_STATIC_IMPACT_SURFACE = UINT32_C(1) << 2,
	SG_WEAPON_STATIC_BLAST_REACH = UINT32_C(1) << 3,
	SG_WEAPON_STATIC_BOUNCE_SURFACE = UINT32_C(1) << 4,
	SG_WEAPON_STATIC_SECONDARY_BLAST_REACH = UINT32_C(1) << 5,
	SG_WEAPON_STATIC_PERIODIC_PROJECTILE_RAY = UINT32_C(1) << 11
};

#define SG_WEAPON_STATIC_RELATION_MASK \
	(SG_WEAPON_STATIC_DIRECT_VISIBILITY | \
	 SG_WEAPON_STATIC_PROJECTILE_CORRIDOR | \
	 SG_WEAPON_STATIC_IMPACT_SURFACE | SG_WEAPON_STATIC_BLAST_REACH | \
	 SG_WEAPON_STATIC_BOUNCE_SURFACE | \
	 SG_WEAPON_STATIC_SECONDARY_BLAST_REACH | \
	 SG_WEAPON_STATIC_PERIODIC_PROJECTILE_RAY)

/* These identities are supplied by the accepted-artifact boundary. The
 * weapon layer compares and carries them; it does not derive new identities. */
typedef struct sg_weapon_static_binding_s
{
	sg_rune_v2_content_id_t artifact_identity;
	sg_rune_v2_content_id_t bsp_identity;
	sg_rune_v2_content_id_t schema_identity;
	uint64_t source_set_identity;
	uint64_t visibility_revision;
} sg_weapon_static_binding_t;

typedef struct sg_weapon_static_query_input_s
{
	sg_weapon_static_binding_t binding;
	sg_rune_cell_ref_t source_cell;
	sg_rune_cell_ref_t target_cell;
	sg_rune_phase_ref_t source_phase;
	sg_rune_phase_ref_t target_phase;
	sg_rune_vec3_t source_origin;
	sg_rune_vec3_t target_origin;
	sg_rune_bounds_t target_bounds;
	sg_weapon_static_relation_t requested_relations;
} sg_weapon_static_query_input_t;

typedef struct sg_weapon_static_query_s
{
	sg_weapon_static_binding_t binding;
	sg_rune_cell_ref_t source_cell;
	sg_rune_cell_ref_t target_cell;
	sg_rune_phase_ref_t source_phase;
	sg_rune_phase_ref_t target_phase;
	sg_rune_vec3_t source_origin;
	sg_rune_vec3_t target_origin;
	sg_rune_bounds_t target_bounds;
	sg_weapon_static_relation_t requested_relations;
	uint8_t exact_live_prefire_trace_required;
} sg_weapon_static_query_t;

size_t SG_WeaponProfileCount(void);
int SG_WeaponProfileIdValid(uint16_t profile_id);
int SG_WeaponProfileLookup(sg_weapon_profile_id_t id,
	const sg_weapon_profile_t **profile_out);
int SG_WeaponProfileResolve(sg_weapon_profile_id_t id,
	const sg_weapon_law_input_t *law, sg_weapon_profile_t *profile_out);
int SG_WeaponProfileCatalogValid(void);
int SG_WeaponStaticBindingValid(const sg_weapon_static_binding_t *binding);
int SG_WeaponStaticQueryPrepare(const sg_weapon_static_query_input_t *input,
	sg_weapon_static_query_t *query_out);

#endif /* SG_WEAPON_EFFECT_PROFILE_H */
