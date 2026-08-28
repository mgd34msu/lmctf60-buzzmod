#include "sg_weapon_effect_profile.h"
#include "sg_weapon_contract.h"
#include "sg_action_contract.generated.h"
#include "sg_weapon_host_constants.h"
#include "../plasma.h"

#include <math.h>

#define EFFECT_PROJECTILE SG_WEAPON_EFFECT_PROJECTILE
#define EFFECT_SPLASH SG_WEAPON_EFFECT_SPLASH
#define EFFECT_SPREAD SG_WEAPON_EFFECT_SPREAD
#define EFFECT_BOUNCE SG_WEAPON_EFFECT_BOUNCE
#define EFFECT_MULTI SG_WEAPON_EFFECT_MULTI_PROJECTILE
#define AMMO_LAW(ready, live, debit_value, debit_max, infinite_debit) \
	.ammo = { \
		.ready_minimum = (ready), \
		.live_fire_minimum = (live), \
		.debit = (debit_value), \
		.debit_maximum = (debit_max), \
		.infinite_ammo_debit = (infinite_debit) \
	}
#define TIMING_LAW(activate, fire, idle, deactivate, first, last, interval) \
	.timing = { \
		.activate_last_frame = (activate), \
		.fire_last_frame = (fire), \
		.idle_last_frame = (idle), \
		.deactivate_first_frame = (idle) + 1U, \
		.deactivate_last_frame = (deactivate), \
		.first_effect_frame = (first), \
		.last_effect_frame = (last), \
		.effect_interval_ms = (interval), \
		.activation_frame_step = 1U, \
		.fast_switch_bypasses_activation = 1U, \
		.fast_switch_bypasses_deactivation = 1U \
	}
#define TIMING_LAW_NO_FAST(activate, fire, idle, deactivate, first, last, interval) \
	.timing = { \
		.activate_last_frame = (activate), \
		.fire_last_frame = (fire), \
		.idle_last_frame = (idle), \
		.deactivate_first_frame = (idle) + 1U, \
		.deactivate_last_frame = (deactivate), \
		.first_effect_frame = (first), \
		.last_effect_frame = (last), \
		.effect_interval_ms = (interval), \
		.activation_frame_step = 1U \
	}
#define HOOK_TIMING_LAW \
	.timing = { \
		.activate_last_frame = 9U, \
		.fire_last_frame = 13U, \
		.idle_last_frame = 34U, \
		.deactivate_first_frame = 36U, \
		.deactivate_last_frame = 38U, \
		.first_effect_frame = 10U, \
		.last_effect_frame = 11U, \
		.effect_interval_ms = SG_HOST_SERVER_FRAME_MS, \
		.activation_frame_step = 2U, \
		.fast_switch_bypasses_activation = 1U, \
		.fast_switch_bypasses_deactivation = 1U \
	}
#define ORDINARY_SPLASH \
	.splash = { \
		.kernel = SG_WEAPON_SPLASH_LINEAR_HALF_DISTANCE, \
		.owner = SG_WEAPON_SPLASH_OWNER_SELF_SCALE, \
		.owner_scale = SG_HOST_RADIUS_SELF_SCALE \
	}

static const sg_weapon_profile_t weapon_profiles[] = {
	{
		.id = SG_WEAPON_PROFILE_BLASTER,
		.family = SG_WEAPON_FAMILY_STRAIGHT_PROJECTILE,
		.effects = EFFECT_PROJECTILE,
		.projectile_speed = SG_HOST_BLASTER_SPEED,
		.projectile_speed_max = SG_HOST_BLASTER_SPEED,
		.projectile_lifetime_ms =
			(uint32_t)(SG_HOST_BLASTER_LIFETIME_SECONDS * 1000.0f),
		.direct_damage = SG_HOST_BLASTER_DM_DAMAGE,
		.direct_damage_max = SG_HOST_BLASTER_DM_DAMAGE,
		.cadence_ms = 500U,
		.teammate_risk_scale = 1.0f,
		.projectile_count_min = 1U,
		.projectile_count_max = 1U,
		AMMO_LAW(0U, 0U, 0U, 0U, 0U),
		TIMING_LAW(4U, 8U, 52U, 55U, 5U, 5U, 0U),
		.requires_live_trace = 1U
	},
	{
		.id = SG_WEAPON_PROFILE_SHOTGUN,
		.family = SG_WEAPON_FAMILY_SPREAD,
		.effects = SG_WEAPON_EFFECT_HITSCAN | EFFECT_SPREAD | EFFECT_MULTI,
		.ray_distance = SG_HOST_WEAPON_RAY_DISTANCE,
		.horizontal_spread = SG_HOST_SHOTGUN_LIVE_HORIZONTAL_SPREAD,
		.vertical_spread = SG_HOST_SHOTGUN_LIVE_VERTICAL_SPREAD,
		.direct_damage = SG_HOST_SHOTGUN_DAMAGE,
		.direct_damage_max = SG_HOST_SHOTGUN_DAMAGE,
		.cadence_ms = 1200U,
		.teammate_risk_scale = 1.0f,
		.projectile_count_min = SG_HOST_SHOTGUN_PELLETS,
		.projectile_count_max = SG_HOST_SHOTGUN_PELLETS,
		AMMO_LAW(1U, 1U, 1U, 1U, 0U),
		TIMING_LAW(7U, 18U, 36U, 39U, 8U, 8U, 0U),
		.requires_live_trace = 1U
	},
	{
		.id = SG_WEAPON_PROFILE_SUPER_SHOTGUN,
		.family = SG_WEAPON_FAMILY_SPREAD,
		.effects = SG_WEAPON_EFFECT_HITSCAN | EFFECT_SPREAD | EFFECT_MULTI,
		.ray_distance = SG_HOST_WEAPON_RAY_DISTANCE,
		.horizontal_spread = SG_HOST_SHOTGUN_HORIZONTAL_SPREAD,
		.vertical_spread = SG_HOST_SHOTGUN_VERTICAL_SPREAD,
		.yaw_spread_degrees = SG_HOST_SUPER_SHOTGUN_YAW_DEGREES,
		.direct_damage = SG_HOST_SUPER_SHOTGUN_DAMAGE,
		.direct_damage_max = SG_HOST_SUPER_SHOTGUN_DAMAGE,
		.cadence_ms = 1200U,
		.teammate_risk_scale = 1.0f,
		.projectile_count_min = SG_HOST_SUPER_SHOTGUN_PELLETS,
		.projectile_count_max = SG_HOST_SUPER_SHOTGUN_PELLETS,
		AMMO_LAW(2U, 2U, 2U, 2U, 0U),
		TIMING_LAW(6U, 17U, 57U, 61U, 7U, 7U, 0U),
		.requires_live_trace = 1U
	},
	{
		.id = SG_WEAPON_PROFILE_MACHINEGUN,
		.family = SG_WEAPON_FAMILY_HITSCAN,
		.effects = SG_WEAPON_EFFECT_HITSCAN | EFFECT_SPREAD,
		.ray_distance = SG_HOST_WEAPON_RAY_DISTANCE,
		.horizontal_spread = SG_HOST_BULLET_HORIZONTAL_SPREAD,
		.vertical_spread = SG_HOST_BULLET_VERTICAL_SPREAD,
		.direct_damage = SG_HOST_MACHINEGUN_DAMAGE,
		.direct_damage_max = SG_HOST_MACHINEGUN_DAMAGE,
		.teammate_risk_scale = 1.0f,
		.cadence_ms = SG_HOST_SERVER_FRAME_MS,
		.cadence_kind = SG_WEAPON_CADENCE_HELD_LOOP,
		.projectile_count_min = 1U,
		.projectile_count_max = 1U,
		AMMO_LAW(1U, 1U, 1U, 1U, 0U),
		TIMING_LAW(3U, 5U, 45U, 49U, 4U, 5U,
			SG_HOST_SERVER_FRAME_MS),
		.requires_live_trace = 1U
	},
	{
		.id = SG_WEAPON_PROFILE_CHAINGUN,
		.family = SG_WEAPON_FAMILY_HITSCAN,
		.effects = SG_WEAPON_EFFECT_HITSCAN | EFFECT_SPREAD | EFFECT_MULTI,
		.ray_distance = SG_HOST_WEAPON_RAY_DISTANCE,
		.horizontal_spread = SG_HOST_BULLET_HORIZONTAL_SPREAD,
		.vertical_spread = SG_HOST_BULLET_VERTICAL_SPREAD,
		.direct_damage = SG_HOST_CHAINGUN_DM_DAMAGE,
		.direct_damage_max = SG_HOST_CHAINGUN_DM_DAMAGE,
		.teammate_risk_scale = 1.0f,
		.cadence_ms = SG_HOST_SERVER_FRAME_MS,
		.cadence_kind = SG_WEAPON_CADENCE_HELD_LOOP,
		.projectile_count_min = 1U,
		.projectile_count_max = 3U,
		AMMO_LAW(1U, 1U, 1U, 3U, 0U),
		TIMING_LAW(4U, 31U, 61U, 64U, 5U, 21U,
			SG_HOST_SERVER_FRAME_MS),
		.requires_live_trace = 1U
	},
	{
		.id = SG_WEAPON_PROFILE_GRENADE_LAUNCHER,
		.family = SG_WEAPON_FAMILY_GRENADE_BOUNCE,
		.effects = EFFECT_PROJECTILE | EFFECT_SPLASH | EFFECT_BOUNCE,
		.projectile_speed = SG_HOST_GRENADE_SPEED,
		.projectile_speed_max = SG_HOST_GRENADE_SPEED,
		.launch_vertical_speed = SG_HOST_GRENADE_VERTICAL_SPEED,
		.launch_jitter = SG_HOST_GRENADE_JITTER,
		.gravity_scale = 1.0f,
		.splash_radius = SG_HOST_GRENADE_DAMAGE + SG_HOST_GRENADE_RADIUS_BONUS,
		.splash_radius_max = SG_HOST_GRENADE_DAMAGE + SG_HOST_GRENADE_RADIUS_BONUS,
		.direct_damage = SG_HOST_GRENADE_DAMAGE,
		.direct_damage_max = SG_HOST_GRENADE_DAMAGE,
		.splash_damage = SG_HOST_GRENADE_DAMAGE,
		.splash_damage_max = SG_HOST_GRENADE_DAMAGE,
		.self_damage_scale = SG_HOST_RADIUS_SELF_SCALE,
		.teammate_risk_scale = 1.0f,
		ORDINARY_SPLASH,
		.fuse_ms = SG_HOST_GRENADE_FUSE_MS,
		.cadence_ms = 1200U,
		.projectile_count_min = 1U,
		.projectile_count_max = 1U,
		AMMO_LAW(1U, 1U, 1U, 1U, 0U),
		TIMING_LAW(5U, 16U, 59U, 64U, 6U, 6U, 0U),
		.requires_live_trace = 1U,
		.supports_occluded_impact = 1U
	},
	{
		.id = SG_WEAPON_PROFILE_ROCKET_LAUNCHER,
		.family = SG_WEAPON_FAMILY_ROCKET_SPLASH,
		.effects = EFFECT_PROJECTILE | EFFECT_SPLASH,
		.projectile_speed = SG_RUNE_PROOF_ROCKETJUMP_ROCKET_SPEED,
		.projectile_speed_max = SG_RUNE_PROOF_ROCKETJUMP_ROCKET_SPEED,
		.projectile_retire_distance = SG_HOST_PROJECTILE_RETIRE_DISTANCE,
		.splash_radius = SG_RUNE_PROOF_ROCKETJUMP_DAMAGE_RADIUS,
		.splash_radius_max = SG_RUNE_PROOF_ROCKETJUMP_DAMAGE_RADIUS,
		.direct_damage = SG_HOST_ROCKET_DAMAGE_BASE,
		.direct_damage_max = SG_HOST_ROCKET_DAMAGE_BASE +
			SG_HOST_ROCKET_DAMAGE_RANDOM_SPAN - 1,
		.splash_damage = SG_RUNE_PROOF_ROCKETJUMP_RADIUS_DAMAGE,
		.splash_damage_max = SG_RUNE_PROOF_ROCKETJUMP_RADIUS_DAMAGE,
		.self_damage_scale = SG_HOST_RADIUS_SELF_SCALE,
		.teammate_risk_scale = 1.0f,
		.cadence_ms = 900U,
		ORDINARY_SPLASH,
		.projectile_count_min = 1U,
		.projectile_count_max = 1U,
		AMMO_LAW(1U, 1U, 1U, 1U, 0U),
		TIMING_LAW(4U, 12U, 50U, 54U, 5U, 5U, 0U),
		.requires_live_trace = 1U,
		.supports_occluded_impact = 1U
	},
	{
		.id = SG_WEAPON_PROFILE_HYPERBLASTER,
		.family = SG_WEAPON_FAMILY_HYPERBLASTER,
		.effects = EFFECT_PROJECTILE,
		.projectile_speed = SG_HOST_HYPERBLASTER_SPEED,
		.projectile_speed_max = SG_HOST_HYPERBLASTER_SPEED,
		.projectile_lifetime_ms =
			(uint32_t)(SG_HOST_BLASTER_LIFETIME_SECONDS * 1000.0f),
		.direct_damage = SG_HOST_HYPERBLASTER_DM_DAMAGE,
		.direct_damage_max = SG_HOST_HYPERBLASTER_DM_DAMAGE,
		.teammate_risk_scale = 1.0f,
		.cadence_ms = SG_HOST_SERVER_FRAME_MS,
		.cadence_kind = SG_WEAPON_CADENCE_HELD_LOOP,
		.projectile_count_min = 1U,
		.projectile_count_max = 1U,
		AMMO_LAW(1U, 1U, 1U, 1U, 0U),
		TIMING_LAW(5U, 20U, 49U, 53U, 6U, 11U,
			SG_HOST_SERVER_FRAME_MS),
		.requires_live_trace = 1U
	},
	{
		.id = SG_WEAPON_PROFILE_RAILGUN,
		.family = SG_WEAPON_FAMILY_HITSCAN,
		.effects = SG_WEAPON_EFFECT_HITSCAN | SG_WEAPON_EFFECT_PENETRATION,
		.ray_distance = SG_HOST_WEAPON_RAY_DISTANCE,
		.direct_damage = SG_HOST_RAILGUN_DM_DAMAGE,
		.direct_damage_max = SG_HOST_RAILGUN_DM_DAMAGE,
		.teammate_risk_scale = 1.0f,
		.cadence_ms = 1600U,
		.projectile_count_min = 1U,
		.projectile_count_max = 1U,
		AMMO_LAW(1U, 1U, 1U, 1U, 0U),
		TIMING_LAW(3U, 18U, 56U, 61U, 4U, 4U, 0U),
		.requires_live_trace = 1U
	},
	{
		.id = SG_WEAPON_PROFILE_BFG,
		.family = SG_WEAPON_FAMILY_BFG,
		.effects = EFFECT_PROJECTILE | EFFECT_SPLASH |
			SG_WEAPON_EFFECT_SECONDARY_AREA | SG_WEAPON_EFFECT_PENETRATION |
			SG_WEAPON_EFFECT_PERIODIC_RAY | SG_WEAPON_EFFECT_SPECIAL,
		.projectile_speed = SG_HOST_BFG_SPEED,
		.projectile_speed_max = SG_HOST_BFG_SPEED,
		.splash_radius = SG_HOST_BFG_CORE_RADIUS,
		.splash_radius_max = SG_HOST_BFG_CORE_RADIUS,
		.secondary_splash_radius = SG_HOST_BFG_EFFECT_RADIUS,
		.direct_damage = SG_HOST_BFG_DAMAGE,
		.direct_damage_max = SG_HOST_BFG_DAMAGE,
		.splash_damage = SG_HOST_BFG_DAMAGE,
		.splash_damage_max = SG_HOST_BFG_DAMAGE,
		.secondary_splash_damage = SG_HOST_BFG_DAMAGE,
		.periodic_ray_damage = SG_HOST_BFG_PERIODIC_RAY_DAMAGE,
		.periodic_ray_radius = SG_HOST_BFG_PERIODIC_RAY_RADIUS,
		.periodic_ray_distance = SG_HOST_BFG_PERIODIC_RAY_DISTANCE,
		.self_damage_scale = SG_HOST_RADIUS_SELF_SCALE,
		.teammate_risk_scale = 1.0f,
		ORDINARY_SPLASH,
		.secondary_splash = {
			.kernel = SG_WEAPON_SPLASH_SQRT_NORMALIZED,
			.owner = SG_WEAPON_SPLASH_OWNER_EXCLUDED
		},
		.windup_ms = SG_HOST_BFG_WINDUP_MS,
		.cadence_ms = 1700U,
		.periodic_ray_interval_ms = SG_HOST_SERVER_FRAME_MS,
		.projectile_count_min = 1U,
		.projectile_count_max = 1U,
		AMMO_LAW(SG_HOST_BFG_AMMO_COST, SG_HOST_BFG_AMMO_COST,
			SG_HOST_BFG_AMMO_COST, SG_HOST_BFG_AMMO_COST, 0U),
		TIMING_LAW(8U, 32U, 55U, 58U, SG_HOST_BFG_FIRE_FRAME,
			SG_HOST_BFG_FIRE_FRAME, 0U),
		.requires_live_trace = 1U,
		.supports_occluded_impact = 1U
	},
	{
		.id = SG_WEAPON_PROFILE_PLASMA_REFLECT,
		.family = SG_WEAPON_FAMILY_PLASMA_REFLECT,
		.effects = EFFECT_PROJECTILE | EFFECT_SPLASH | EFFECT_BOUNCE,
		.projectile_speed = PLASMA_REFLECT_SPEED,
		.projectile_speed_max = PLASMA_REFLECT_SPEED,
		.projectile_half_extent = PLASMA_REFLECT_HALF_EXTENT,
		.splash_radius = PLASMA_BOUNCE_DAMAGE + PLASMA_SPLASH_RADIUS,
		.splash_radius_max = PLASMA_BOUNCE_DAMAGE *
			SG_HOST_DAMAGE_QUAD_SCALE + PLASMA_SPLASH_RADIUS,
		.direct_damage = PLASMA_BOUNCE_DAMAGE,
		.direct_damage_max = PLASMA_BOUNCE_DAMAGE * SG_HOST_DAMAGE_QUAD_SCALE,
		.splash_damage = PLASMA_BOUNCE_DAMAGE,
		.splash_damage_max = PLASMA_BOUNCE_DAMAGE *
			SG_HOST_DAMAGE_QUAD_SCALE,
		.self_damage_scale = SG_HOST_RADIUS_SELF_SCALE,
		.teammate_risk_scale = 1.0f,
		ORDINARY_SPLASH,
		.damage_dependency = SG_WEAPON_DAMAGE_IMPACT_GLOBAL_QUAD,
		.projectile_lifetime_ms =
			(uint32_t)(PLASMA_REFLECT_LIFETIME_SECONDS * 1000.0f),
		.cadence_ms = 900U,
		.projectile_count_min = 1U,
		.projectile_count_max = 1U,
		AMMO_LAW(PLASMA_CELLS_PER_SHOT, 1U, PLASMA_CELLS_PER_SHOT,
			PLASMA_CELLS_PER_SHOT, PLASMA_CELLS_PER_SHOT - 1U),
		TIMING_LAW_NO_FAST(3U, 11U, 46U, 51U, 4U, 4U, 0U),
		.requires_live_trace = 1U,
		.supports_occluded_impact = 1U
	},
	{
		.id = SG_WEAPON_PROFILE_PLASMA_SPREAD,
		.family = SG_WEAPON_FAMILY_PLASMA_SPREAD,
		.effects = EFFECT_PROJECTILE | EFFECT_SPLASH | EFFECT_SPREAD |
			EFFECT_MULTI,
		.projectile_speed = PLASMA_SPREAD_SPEED,
		.projectile_speed_max = PLASMA_SPREAD_SPEED,
		.yaw_spread_degrees = PLASMA_SPREAD_YAW_DEGREES,
		.splash_radius = PLASMA_SPREAD_DAMAGE + PLASMA_SPLASH_RADIUS,
		.splash_radius_max = PLASMA_SPREAD_DAMAGE *
			SG_HOST_DAMAGE_QUAD_SCALE + PLASMA_SPLASH_RADIUS,
		.direct_damage = PLASMA_SPREAD_DAMAGE,
		.direct_damage_max = PLASMA_SPREAD_DAMAGE * SG_HOST_DAMAGE_QUAD_SCALE,
		.splash_damage = PLASMA_SPREAD_DAMAGE,
		.splash_damage_max = PLASMA_SPREAD_DAMAGE *
			SG_HOST_DAMAGE_QUAD_SCALE,
		.self_damage_scale = SG_HOST_RADIUS_SELF_SCALE,
		.teammate_risk_scale = 1.0f,
		ORDINARY_SPLASH,
		.damage_dependency = SG_WEAPON_DAMAGE_IMPACT_GLOBAL_QUAD,
		.projectile_lifetime_ms =
			(uint32_t)(PLASMA_SPREAD_LIFETIME_SECONDS * 1000.0f),
		.cadence_ms = 900U,
		.projectile_count_min = 3U,
		.projectile_count_max = 3U,
		AMMO_LAW(PLASMA_CELLS_PER_SHOT, 1U, PLASMA_CELLS_PER_SHOT,
			PLASMA_CELLS_PER_SHOT, PLASMA_CELLS_PER_SHOT - 1U),
		TIMING_LAW_NO_FAST(3U, 11U, 46U, 51U, 4U, 4U, 0U),
		.requires_live_trace = 1U,
		.supports_occluded_impact = 1U
	},
	{
		.id = SG_WEAPON_PROFILE_HAND_GRENADE,
		.family = SG_WEAPON_FAMILY_GRENADE_BOUNCE,
		.effects = EFFECT_PROJECTILE | EFFECT_SPLASH | EFFECT_BOUNCE,
		.projectile_speed = SG_HOST_HAND_GRENADE_MIN_SPEED,
		.projectile_speed_max = SG_HOST_HAND_GRENADE_MAX_SPEED,
		.launch_vertical_speed = SG_HOST_GRENADE_VERTICAL_SPEED,
		.launch_jitter = SG_HOST_GRENADE_JITTER,
		.gravity_scale = 1.0f,
		.splash_radius = SG_HOST_HAND_GRENADE_DAMAGE +
			SG_HOST_GRENADE_RADIUS_BONUS,
		.splash_radius_max = SG_HOST_HAND_GRENADE_DAMAGE +
			SG_HOST_GRENADE_RADIUS_BONUS,
		.direct_damage = SG_HOST_HAND_GRENADE_DAMAGE,
		.direct_damage_max = SG_HOST_HAND_GRENADE_DAMAGE,
		.splash_damage = SG_HOST_HAND_GRENADE_DAMAGE,
		.splash_damage_max = SG_HOST_HAND_GRENADE_DAMAGE,
		.self_damage_scale = SG_HOST_RADIUS_SELF_SCALE,
		.teammate_risk_scale = 1.0f,
		ORDINARY_SPLASH,
		.cook_ms = SG_HOST_HAND_GRENADE_COOK_MS,
		.fuse_ms = SG_HOST_HAND_GRENADE_HELD_FUSE_MS,
		.cadence_ms = 1600U,
		.cadence_kind = SG_WEAPON_CADENCE_COOKED_RELEASE,
		.projectile_count_min = 1U,
		.projectile_count_max = 1U,
		AMMO_LAW(1U, 1U, 1U, 1U, 0U),
		.requires_live_trace = 1U,
		.supports_occluded_impact = 1U
	},
	{
		.id = SG_WEAPON_PROFILE_HOOK,
		.family = SG_WEAPON_FAMILY_SPECIAL,
		.effects = EFFECT_PROJECTILE | SG_WEAPON_EFFECT_SPECIAL,
		.projectile_speed = SG_HOST_HOOK_FIRE_SPEED,
		.projectile_speed_max = SG_HOST_HOOK_FIRE_SPEED,
		.direct_damage = SG_HOST_HOOK_INITIAL_DAMAGE,
		.direct_damage_max = SG_HOST_HOOK_INITIAL_DAMAGE,
		.teammate_risk_scale = 1.0f,
		.projectile_count_min = 1U,
		.projectile_count_max = 1U,
		.hook_initial_damage = SG_HOST_HOOK_INITIAL_DAMAGE,
		.hook_attached_damage = SG_HOST_HOOK_ATTACHED_DAMAGE,
		.hook_health = SG_HOST_HOOK_HEALTH,
		.hook_pull_speed = SG_HOST_HOOK_PULL_SPEED,
		.cadence_kind = SG_WEAPON_CADENCE_STATE_DEPENDENT,
		AMMO_LAW(0U, 0U, 0U, 0U, 0U),
		HOOK_TIMING_LAW,
		.requires_live_trace = 1U
	}
};

static int WeaponPointValid(const sg_rune_vec3_t *point)
{
	uint32_t axis;

	if (!point)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (!isfinite(point->value[axis]))
			return 0;
	return 1;
}

static int WeaponBoundsValid(const sg_rune_bounds_t *bounds)
{
	uint32_t axis;

	if (!bounds || !WeaponPointValid(&bounds->mins) ||
	    !WeaponPointValid(&bounds->maxs))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (bounds->mins.value[axis] > bounds->maxs.value[axis])
			return 0;
	return 1;
}

static int WeaponStableRefBound(const sg_rune_stable_id_t *id,
	uint64_t source_set_identity, uint32_t expected_domain)
{
	sg_rune_order_key_t key;

	return SG_RuneModelStableIdToOrderKey(id, &key) &&
		key.source_set_identity == source_set_identity &&
		key.domain == expected_domain;
}

size_t SG_WeaponProfileCount(void)
{
	return sizeof(weapon_profiles) / sizeof(weapon_profiles[0]);
}

int SG_WeaponProfileLookup(sg_weapon_profile_id_t id,
	const sg_weapon_profile_t **profile_out)
{
	size_t index;

	if (!profile_out)
		return 0;
	for (index = 0U; index < SG_WeaponProfileCount(); index++)
		if (weapon_profiles[index].id == id)
		{
			*profile_out = &weapon_profiles[index];
			return 1;
		}
	return 0;
}

int SG_WeaponProfileIdValid(uint16_t profile_id)
{
	const sg_weapon_profile_t *profile;

	if (profile_id == 0U || profile_id >= (uint16_t)SG_WEAPON_PROFILE_COUNT)
		return 0;
	return SG_WeaponProfileLookup((sg_weapon_profile_id_t)profile_id, &profile);
}

static int WeaponLawValid(const sg_weapon_law_input_t *law)
{
	return law && law->build_identity != 0U && law->physics_abi_id != 0U &&
		law->weapon_balance_compiled <= 1U &&
		law->weapon_balance_compiled == SG_WEAPON_BALANCE_COMPILED &&
		law->weapon_balance_enabled <= 1U && law->quad_active <= 1U &&
		law->rail_match_active <= 1U && law->deathmatch_active <= 1U &&
		law->fast_switch_enabled <= 1U &&
		(law->weapon_balance_enabled == 0U ||
		 law->weapon_balance_compiled == 1U);
}

static void WeaponApplyBalancedLaw(sg_weapon_profile_t *profile)
{
	switch (profile->id)
	{
	case SG_WEAPON_PROFILE_SHOTGUN:
		profile->projectile_count_min +=
			SG_HOST_SHOTGUN_BALANCED_PELLET_DELTA;
		profile->projectile_count_max +=
			SG_HOST_SHOTGUN_BALANCED_PELLET_DELTA;
		break;
	case SG_WEAPON_PROFILE_SUPER_SHOTGUN:
		profile->projectile_count_min +=
			SG_HOST_SUPER_SHOTGUN_BALANCED_PELLET_DELTA;
		profile->projectile_count_max +=
			SG_HOST_SUPER_SHOTGUN_BALANCED_PELLET_DELTA;
		break;
	case SG_WEAPON_PROFILE_MACHINEGUN:
		profile->direct_damage = SG_HOST_MACHINEGUN_BALANCED_DAMAGE;
		profile->direct_damage_max = SG_HOST_MACHINEGUN_BALANCED_DAMAGE;
		profile->horizontal_spread +=
			SG_HOST_MACHINEGUN_BALANCED_SPREAD_DELTA;
		profile->vertical_spread +=
			SG_HOST_MACHINEGUN_BALANCED_SPREAD_DELTA;
		break;
	case SG_WEAPON_PROFILE_GRENADE_LAUNCHER:
		profile->splash_radius =
			(SG_HOST_GRENADE_DAMAGE + SG_HOST_GRENADE_RADIUS_BONUS) *
			SG_HOST_GRENADE_BALANCED_RADIUS_SCALE;
		profile->splash_radius_max = profile->splash_radius;
		break;
	case SG_WEAPON_PROFILE_HAND_GRENADE:
		break;
	case SG_WEAPON_PROFILE_ROCKET_LAUNCHER:
		profile->projectile_speed = SG_HOST_ROCKET_BALANCED_SPEED;
		profile->projectile_speed_max = SG_HOST_ROCKET_BALANCED_SPEED;
		profile->splash_damage = SG_HOST_ROCKET_BALANCED_SPLASH_DAMAGE;
		profile->splash_damage_max = SG_HOST_ROCKET_BALANCED_SPLASH_DAMAGE;
		profile->splash_radius = SG_HOST_ROCKET_BALANCED_SPLASH_RADIUS;
		profile->splash_radius_max = SG_HOST_ROCKET_BALANCED_SPLASH_RADIUS;
		break;
	case SG_WEAPON_PROFILE_HYPERBLASTER:
		profile->direct_damage = SG_HOST_HYPERBLASTER_BALANCED_DAMAGE;
		profile->direct_damage_max = SG_HOST_HYPERBLASTER_BALANCED_DAMAGE;
		break;
	case SG_WEAPON_PROFILE_RAILGUN:
		profile->direct_damage = SG_HOST_RAILGUN_BALANCED_DAMAGE;
		profile->direct_damage_max = SG_HOST_RAILGUN_BALANCED_DAMAGE;
		profile->auxiliary_trace_count =
			SG_HOST_RAILGUN_AUXILIARY_TRACE_COUNT;
		profile->auxiliary_trace_damage =
			SG_HOST_RAILGUN_AUXILIARY_DAMAGE;
		profile->auxiliary_horizontal_spread =
			SG_HOST_RAILGUN_AUXILIARY_SPREAD;
		profile->auxiliary_vertical_spread =
			SG_HOST_RAILGUN_AUXILIARY_SPREAD;
		break;
	case SG_WEAPON_PROFILE_BFG:
		profile->projectile_speed = SG_HOST_BFG_BALANCED_SPEED;
		profile->projectile_speed_max = SG_HOST_BFG_BALANCED_SPEED;
		profile->secondary_splash_damage = SG_HOST_BFG_BALANCED_DAMAGE;
		profile->secondary_splash_radius =
			SG_HOST_BFG_BALANCED_EFFECT_RADIUS;
		profile->periodic_ray_damage =
			SG_HOST_BFG_BALANCED_PERIODIC_RAY_DAMAGE;
		break;
	case SG_WEAPON_PROFILE_BLASTER:
	case SG_WEAPON_PROFILE_CHAINGUN:
	case SG_WEAPON_PROFILE_PLASMA_REFLECT:
	case SG_WEAPON_PROFILE_PLASMA_SPREAD:
	case SG_WEAPON_PROFILE_HOOK:
	case SG_WEAPON_PROFILE_COUNT:
		break;
	}
}

static void WeaponApplyQuadLaw(sg_weapon_profile_t *profile)
{
	if (profile->damage_dependency == SG_WEAPON_DAMAGE_IMPACT_GLOBAL_QUAD)
		return;
	if (profile->id == SG_WEAPON_PROFILE_HOOK)
		return;
	if (profile->id == SG_WEAPON_PROFILE_BFG)
	{
		profile->secondary_splash_damage *= SG_HOST_DAMAGE_QUAD_SCALE;
		return;
	}
	profile->direct_damage *= SG_HOST_DAMAGE_QUAD_SCALE;
	profile->direct_damage_max *= SG_HOST_DAMAGE_QUAD_SCALE;
	profile->splash_damage *= SG_HOST_DAMAGE_QUAD_SCALE;
	profile->splash_damage_max = profile->splash_damage;
}

static void WeaponApplyDeathmatchLaw(sg_weapon_profile_t *profile,
	uint8_t deathmatch_active)
{
	if (deathmatch_active != 0U)
		return;
	switch (profile->id)
	{
	case SG_WEAPON_PROFILE_BLASTER:
		profile->direct_damage = SG_HOST_BLASTER_NON_DM_DAMAGE;
		profile->direct_damage_max = SG_HOST_BLASTER_NON_DM_DAMAGE;
		break;
	case SG_WEAPON_PROFILE_HYPERBLASTER:
		profile->direct_damage = SG_HOST_HYPERBLASTER_NON_DM_DAMAGE;
		profile->direct_damage_max = SG_HOST_HYPERBLASTER_NON_DM_DAMAGE;
		break;
	case SG_WEAPON_PROFILE_CHAINGUN:
		profile->direct_damage = SG_HOST_CHAINGUN_NON_DM_DAMAGE;
		profile->direct_damage_max = SG_HOST_CHAINGUN_NON_DM_DAMAGE;
		break;
	case SG_WEAPON_PROFILE_RAILGUN:
		profile->direct_damage = SG_HOST_RAILGUN_NON_DM_DAMAGE;
		profile->direct_damage_max = SG_HOST_RAILGUN_NON_DM_DAMAGE;
		break;
	case SG_WEAPON_PROFILE_BFG:
		profile->secondary_splash_damage = SG_HOST_BFG_NON_DM_DAMAGE;
		profile->periodic_ray_damage =
			SG_HOST_BFG_NON_DM_PERIODIC_RAY_DAMAGE;
		break;
	case SG_WEAPON_PROFILE_SHOTGUN:
	case SG_WEAPON_PROFILE_SUPER_SHOTGUN:
	case SG_WEAPON_PROFILE_MACHINEGUN:
	case SG_WEAPON_PROFILE_GRENADE_LAUNCHER:
	case SG_WEAPON_PROFILE_ROCKET_LAUNCHER:
	case SG_WEAPON_PROFILE_PLASMA_REFLECT:
	case SG_WEAPON_PROFILE_PLASMA_SPREAD:
	case SG_WEAPON_PROFILE_HAND_GRENADE:
	case SG_WEAPON_PROFILE_HOOK:
	case SG_WEAPON_PROFILE_COUNT:
		break;
	}
}

int SG_WeaponProfileResolve(sg_weapon_profile_id_t id,
	const sg_weapon_law_input_t *law, sg_weapon_profile_t *profile_out)
{
	const sg_weapon_profile_t *base;
	sg_weapon_profile_t resolved;

	if (!profile_out || !WeaponLawValid(law) ||
	    !SG_WeaponProfileLookup(id, &base))
		return 0;
	resolved = *base;
	WeaponApplyDeathmatchLaw(&resolved, law->deathmatch_active);
	if (law->weapon_balance_enabled != 0U)
	{
		WeaponApplyBalancedLaw(&resolved);
		if ((resolved.effects & SG_WEAPON_EFFECT_SPLASH) != 0U)
			resolved.splash.kernel = SG_WEAPON_SPLASH_NORMALIZED_LINEAR;
	}
	if (law->weapon_balance_enabled != 0U &&
	    id == SG_WEAPON_PROFILE_BLASTER)
	{
		resolved.projectile_lifetime_ms = (uint32_t)
			(SG_HOST_BLASTER_BALANCED_LIFETIME_SECONDS * 1000.0f);
		resolved.projectile_half_extent =
			SG_HOST_BLASTER_BALANCED_HALF_EXTENT;
		resolved.cadence_ms = 400U;
	}
	if (law->rail_match_active != 0U && id == SG_WEAPON_PROFILE_RAILGUN)
	{
		resolved.direct_damage = SG_HOST_RAILGUN_MATCH_DAMAGE;
		resolved.direct_damage_max = SG_HOST_RAILGUN_MATCH_DAMAGE;
	}
	if (law->quad_active != 0U)
		WeaponApplyQuadLaw(&resolved);
	if (law->weapon_balance_enabled != 0U &&
	    id == SG_WEAPON_PROFILE_GRENADE_LAUNCHER)
	{
		resolved.direct_damage -= SG_HOST_GRENADE_BALANCED_DAMAGE_DELTA;
		resolved.direct_damage_max = resolved.direct_damage;
		resolved.splash_damage = resolved.direct_damage;
		resolved.splash_damage_max = resolved.splash_damage;
	}
	if (law->weapon_balance_enabled != 0U &&
	    id == SG_WEAPON_PROFILE_SHOTGUN)
	{
		resolved.direct_damage += SG_HOST_SHOTGUN_BALANCED_DAMAGE_DELTA;
		resolved.direct_damage_max = resolved.direct_damage;
	}
	if (law->weapon_balance_enabled != 0U &&
	    id == SG_WEAPON_PROFILE_SUPER_SHOTGUN)
	{
		resolved.direct_damage -=
			SG_HOST_SUPER_SHOTGUN_BALANCED_DAMAGE_DELTA;
		resolved.direct_damage_max = resolved.direct_damage;
	}
	if (id == SG_WEAPON_PROFILE_PLASMA_REFLECT ||
	    id == SG_WEAPON_PROFILE_PLASMA_SPREAD)
	{
		resolved.splash_radius = resolved.splash_damage + PLASMA_SPLASH_RADIUS;
		resolved.splash_radius_max = resolved.splash_damage_max +
			PLASMA_SPLASH_RADIUS;
		resolved.direct_damage_max = resolved.direct_damage *
			SG_HOST_DAMAGE_QUAD_SCALE;
	}
	if (resolved.timing.activate_last_frame != 0U)
	{
		resolved.timing.resolved_switch_in_ms =
			((uint32_t)resolved.timing.activate_last_frame + 1U +
			 (uint32_t)resolved.timing.activation_frame_step - 1U) /
			(uint32_t)resolved.timing.activation_frame_step *
			SG_HOST_SERVER_FRAME_MS;
		resolved.timing.resolved_switch_out_ms =
			((uint32_t)resolved.timing.deactivate_last_frame -
			 (uint32_t)resolved.timing.deactivate_first_frame + 1U) *
			SG_HOST_SERVER_FRAME_MS;
		if (law->fast_switch_enabled != 0U &&
		    resolved.timing.fast_switch_bypasses_activation != 0U)
			resolved.timing.resolved_switch_in_ms = 0U;
		if (law->fast_switch_enabled != 0U &&
		    resolved.timing.fast_switch_bypasses_deactivation != 0U)
			resolved.timing.resolved_switch_out_ms = 0U;
	}
	resolved.resolved = 1U;
	resolved.build_identity = law->build_identity;
	resolved.physics_abi_id = law->physics_abi_id;
	if (!SG_WeaponProfileValid(&resolved))
		return 0;
	*profile_out = resolved;
	return 1;
}

int SG_WeaponProfileCatalogValid(void)
{
	size_t index;

	if (SG_WeaponProfileCount() != (size_t)SG_WEAPON_PROFILE_COUNT - 1U)
		return 0;
	for (index = 0U; index < SG_WeaponProfileCount(); index++)
	{
		if (!SG_WeaponProfileValid(&weapon_profiles[index]) ||
		    (size_t)weapon_profiles[index].id != index + 1U ||
		    weapon_profiles[index].requires_live_trace != 1U)
			return 0;
	}
	return 1;
}

int SG_WeaponStaticBindingValid(const sg_weapon_static_binding_t *binding)
{
	return binding && binding->source_set_identity != 0U &&
		binding->visibility_revision != 0U &&
		SG_RuneV2ContentIdValid(&binding->artifact_identity) &&
		SG_RuneV2ContentIdValid(&binding->bsp_identity) &&
		SG_RuneV2ContentIdValid(&binding->schema_identity);
}

int SG_WeaponStaticQueryPrepare(const sg_weapon_static_query_input_t *input,
	sg_weapon_static_query_t *query_out)
{
	sg_weapon_static_query_t prepared;

	if (!input || !query_out || !SG_WeaponStaticBindingValid(&input->binding) ||
	    input->requested_relations == 0U ||
	    (input->requested_relations &
	     ~(uint32_t)SG_WEAPON_STATIC_RELATION_MASK) != 0U ||
	    !WeaponStableRefBound(&input->source_cell.value,
		input->binding.source_set_identity, SG_RUNE_ORDER_CELL) ||
	    !WeaponStableRefBound(&input->target_cell.value,
		input->binding.source_set_identity, SG_RUNE_ORDER_CELL) ||
	    !WeaponStableRefBound(&input->source_phase.value,
		input->binding.source_set_identity, SG_RUNE_ORDER_PHASE) ||
	    !WeaponStableRefBound(&input->target_phase.value,
		input->binding.source_set_identity, SG_RUNE_ORDER_PHASE) ||
	    !WeaponPointValid(&input->source_origin) ||
	    !WeaponPointValid(&input->target_origin) ||
	    !WeaponBoundsValid(&input->target_bounds))
		return 0;

	prepared.binding = input->binding;
	prepared.source_cell = input->source_cell;
	prepared.target_cell = input->target_cell;
	prepared.source_phase = input->source_phase;
	prepared.target_phase = input->target_phase;
	prepared.source_origin = input->source_origin;
	prepared.target_origin = input->target_origin;
	prepared.target_bounds = input->target_bounds;
	prepared.requested_relations = input->requested_relations;
	prepared.exact_live_prefire_trace_required = 1U;
	*query_out = prepared;
	return 1;
}

int SG_WeaponPrefireAllowed(const sg_weapon_prefire_request_t *request,
	const sg_weapon_prefire_validation_t *validation)
{
	return SG_WeaponPrefireShotMatches(request, validation) &&
		validation->trace_status == SG_WEAPON_TRACE_ACCEPTED &&
		validation->muzzle_clear == 1U && validation->host_agrees == 1U &&
		validation->authenticated == 1U && validation->authorization_id != 0U;
}
