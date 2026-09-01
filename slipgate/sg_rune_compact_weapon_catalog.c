#include "sg_rune_compact_model.h"
#include "sg_rune_compact_weapon_catalog.h"

#include <stddef.h>
#include <string.h>

static int WeaponSourceLawValid(
	const sg_rune_source_weapon_law_t *static_law)
{
	return static_law != NULL &&
		static_law->weapon_balance_compiled ==
			(uint8_t)SG_WEAPON_BALANCE_COMPILED &&
		static_law->weapon_balance_enabled <= 1U &&
		static_law->rail_match_active <= 1U &&
		static_law->deathmatch_active <= 1U &&
		static_law->fast_switch_enabled <= 1U &&
		static_law->reserved[0] == 0U &&
		static_law->reserved[1] == 0U &&
		static_law->reserved[2] == 0U &&
		(static_law->weapon_balance_compiled != 0U ||
		 static_law->weapon_balance_enabled == 0U);
}

int SG_RuneCompactWeaponProfilesResolve(
	const sg_rune_source_weapon_law_t *static_law,
	uint64_t physics_abi_id, sg_weapon_profile_t *profiles_out,
	uint32_t profile_count)
{
	sg_weapon_law_input_t law;
	uint32_t index;

	if (!WeaponSourceLawValid(static_law) || physics_abi_id == 0U ||
		profiles_out == NULL ||
		profile_count != SG_RUNE_COMPACT_CANONICAL_WEAPON_PROFILE_COUNT ||
		SG_WeaponProfileCount() != (size_t)profile_count ||
		!SG_WeaponProfileCatalogValid())
		return 0;
	memset(&law, 0, sizeof(law));
	law.build_identity = SG_RUNE_COMPACT_WEAPON_PRODUCER_ID;
	law.physics_abi_id = physics_abi_id;
	law.weapon_balance_compiled = static_law->weapon_balance_compiled;
	law.weapon_balance_enabled = static_law->weapon_balance_enabled;
	law.rail_match_active = static_law->rail_match_active;
	law.deathmatch_active = static_law->deathmatch_active;
	law.fast_switch_enabled = static_law->fast_switch_enabled;
	for (index = 0U; index < profile_count; index++) {
		const sg_weapon_profile_id_t profile =
			(sg_weapon_profile_id_t)(index + 1U);

		if (!SG_WeaponProfileResolve(profile, &law, &profiles_out[index]) ||
			profiles_out[index].id != profile ||
			profiles_out[index].build_identity !=
				SG_RUNE_COMPACT_WEAPON_PRODUCER_ID ||
			profiles_out[index].physics_abi_id != physics_abi_id)
			return 0;
	}
	return 1;
}

static uint64_t WeaponCatalogHashByte(uint64_t hash, uint8_t value)
{
	return (hash ^ (uint64_t)value) * UINT64_C(1099511628211);
}

static uint64_t WeaponCatalogHashU16(uint64_t hash, uint16_t value)
{
	hash = WeaponCatalogHashByte(hash, (uint8_t)value);
	return WeaponCatalogHashByte(hash, (uint8_t)(value >> 8));
}

static uint64_t WeaponCatalogHashU32(uint64_t hash, uint32_t value)
{
	hash = WeaponCatalogHashU16(hash, (uint16_t)value);
	return WeaponCatalogHashU16(hash, (uint16_t)(value >> 16));
}

static uint64_t WeaponLawHashFloat(uint64_t hash, float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return WeaponCatalogHashU32(hash, bits);
}

static uint64_t WeaponLawHashProfile(uint64_t hash,
	const sg_weapon_profile_t *profile)
{
#define HASH_WEAPON_U16(field) hash = WeaponCatalogHashU16(hash, profile->field)
#define HASH_WEAPON_U32(field) hash = WeaponCatalogHashU32(hash, (uint32_t)profile->field)
#define HASH_WEAPON_FLOAT(field) hash = WeaponLawHashFloat(hash, profile->field)
	HASH_WEAPON_U32(id);
	HASH_WEAPON_U32(family);
	HASH_WEAPON_U32(effects);
	HASH_WEAPON_FLOAT(projectile_speed);
	HASH_WEAPON_FLOAT(projectile_speed_max);
	HASH_WEAPON_FLOAT(ray_distance);
	HASH_WEAPON_FLOAT(projectile_retire_distance);
	HASH_WEAPON_FLOAT(projectile_half_extent);
	HASH_WEAPON_FLOAT(launch_vertical_speed);
	HASH_WEAPON_FLOAT(launch_jitter);
	HASH_WEAPON_FLOAT(gravity_scale);
	HASH_WEAPON_FLOAT(horizontal_spread);
	HASH_WEAPON_FLOAT(vertical_spread);
	HASH_WEAPON_FLOAT(yaw_spread_degrees);
	HASH_WEAPON_FLOAT(splash_radius);
	HASH_WEAPON_FLOAT(splash_radius_max);
	HASH_WEAPON_FLOAT(secondary_splash_radius);
	HASH_WEAPON_FLOAT(direct_damage);
	HASH_WEAPON_FLOAT(direct_damage_max);
	HASH_WEAPON_FLOAT(splash_damage);
	HASH_WEAPON_FLOAT(splash_damage_max);
	HASH_WEAPON_FLOAT(secondary_splash_damage);
	HASH_WEAPON_FLOAT(periodic_ray_damage);
	HASH_WEAPON_FLOAT(periodic_ray_radius);
	HASH_WEAPON_FLOAT(periodic_ray_distance);
	HASH_WEAPON_FLOAT(self_damage_scale);
	HASH_WEAPON_FLOAT(teammate_risk_scale);
	HASH_WEAPON_FLOAT(auxiliary_trace_damage);
	HASH_WEAPON_FLOAT(auxiliary_horizontal_spread);
	HASH_WEAPON_FLOAT(auxiliary_vertical_spread);
	HASH_WEAPON_U32(windup_ms);
	HASH_WEAPON_U32(cook_ms);
	HASH_WEAPON_U32(fuse_ms);
	HASH_WEAPON_U32(projectile_lifetime_ms);
	HASH_WEAPON_U32(cadence_ms);
	HASH_WEAPON_U32(periodic_ray_interval_ms);
	HASH_WEAPON_U16(projectile_count_min);
	HASH_WEAPON_U16(projectile_count_max);
	HASH_WEAPON_U16(auxiliary_trace_count);
	HASH_WEAPON_U16(hook_initial_damage);
	HASH_WEAPON_U16(hook_attached_damage);
	HASH_WEAPON_U16(hook_health);
	HASH_WEAPON_U16(hook_pull_speed);
	HASH_WEAPON_U32(splash.kernel);
	HASH_WEAPON_U32(splash.owner);
	HASH_WEAPON_FLOAT(splash.owner_scale);
	HASH_WEAPON_U32(secondary_splash.kernel);
	HASH_WEAPON_U32(secondary_splash.owner);
	HASH_WEAPON_FLOAT(secondary_splash.owner_scale);
	HASH_WEAPON_U16(ammo.ready_minimum);
	HASH_WEAPON_U16(ammo.live_fire_minimum);
	HASH_WEAPON_U16(ammo.debit);
	HASH_WEAPON_U16(ammo.debit_maximum);
	HASH_WEAPON_U16(ammo.infinite_ammo_debit);
	HASH_WEAPON_U16(timing.activate_last_frame);
	HASH_WEAPON_U16(timing.fire_last_frame);
	HASH_WEAPON_U16(timing.idle_last_frame);
	HASH_WEAPON_U16(timing.deactivate_first_frame);
	HASH_WEAPON_U16(timing.deactivate_last_frame);
	HASH_WEAPON_U16(timing.first_effect_frame);
	HASH_WEAPON_U16(timing.last_effect_frame);
	HASH_WEAPON_U32(timing.effect_interval_ms);
	HASH_WEAPON_U32(timing.resolved_switch_in_ms);
	HASH_WEAPON_U32(timing.resolved_switch_out_ms);
	HASH_WEAPON_U32(timing.activation_frame_step);
	HASH_WEAPON_U32(timing.fast_switch_bypasses_activation);
	HASH_WEAPON_U32(timing.fast_switch_bypasses_deactivation);
	HASH_WEAPON_U32(damage_dependency);
	HASH_WEAPON_U32(cadence_kind);
	HASH_WEAPON_U32(requires_live_trace);
	HASH_WEAPON_U32(supports_occluded_impact);
	HASH_WEAPON_U32(auxiliary_traces_penetrate);
#undef HASH_WEAPON_FLOAT
#undef HASH_WEAPON_U32
#undef HASH_WEAPON_U16
	return hash;
}

int SG_RuneCompactWeaponLawIdentity(
	const sg_rune_source_weapon_law_t *static_law,
	const sg_weapon_profile_t *profiles, uint32_t profile_count,
	uint64_t *identity_out)
{
	static const char domain[] = "lmctf.compact.weapon-law.v1";
	uint64_t hash = UINT64_C(14695981039346656037);
	size_t character;
	uint32_t index;

	if (static_law == NULL || profiles == NULL || identity_out == NULL ||
		profile_count == 0U)
		return 0;
	for (character = 0U; character < sizeof(domain) - 1U; character++)
		hash = WeaponCatalogHashByte(hash,
			(uint8_t)(unsigned char)domain[character]);
	hash = WeaponCatalogHashU32(hash, static_law->weapon_balance_compiled);
	hash = WeaponCatalogHashU32(hash, static_law->weapon_balance_enabled);
	hash = WeaponCatalogHashU32(hash, static_law->rail_match_active);
	hash = WeaponCatalogHashU32(hash, static_law->deathmatch_active);
	hash = WeaponCatalogHashU32(hash, static_law->fast_switch_enabled);
	hash = WeaponCatalogHashU32(hash, profile_count);
	for (index = 0U; index < profile_count; index++)
		hash = WeaponLawHashProfile(hash, &profiles[index]);
	if (hash == 0U)
		hash = UINT64_C(1);
	else if (hash == UINT64_MAX)
		hash = UINT64_MAX - UINT64_C(1);
	*identity_out = hash;
	return 1;
}

int SG_RuneCompactWeaponProfileCatalogId(
	const sg_rune_weapon_profile_t *profiles, uint32_t profile_count,
	uint64_t *catalog_id_out)
{
	static const char domain[] = "lmctf.compact.weapon-profile-catalog.v1";
	uint64_t hash = UINT64_C(14695981039346656037);
	size_t character;
	uint32_t index;

	if (profiles == NULL || catalog_id_out == NULL || profile_count !=
		SG_RUNE_COMPACT_CANONICAL_WEAPON_PROFILE_COUNT)
		return 0;
	for (character = 0U; character < sizeof(domain) - 1U; character++)
		hash = WeaponCatalogHashByte(hash,
			(uint8_t)(unsigned char)domain[character]);
	hash = WeaponCatalogHashU32(hash, profile_count);
	for (index = 0U; index < profile_count; index++) {
		const sg_rune_weapon_profile_t *profile = &profiles[index];

		hash = WeaponCatalogHashU32(hash, profile->source_profile);
		hash = WeaponCatalogHashU32(hash, profile->response_families);
		hash = WeaponCatalogHashU16(hash, profile->projectile_count_min);
		hash = WeaponCatalogHashU16(hash, profile->projectile_count_max);
		hash = WeaponCatalogHashU16(hash, profile->auxiliary_trace_count);
		hash = WeaponCatalogHashByte(hash, profile->direct_response_count);
		hash = WeaponCatalogHashByte(hash, profile->reserved);
	}
	if (hash == 0U)
		hash = UINT64_C(1);
	else if (hash == UINT64_MAX)
		hash = UINT64_MAX - UINT64_C(1);
	*catalog_id_out = hash;
	return 1;
}
