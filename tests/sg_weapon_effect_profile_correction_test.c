#include "slipgate/sg_weapon_effect_profile.h"
#include "slipgate/sg_weapon_host_constants.h"

#include <stdio.h>

static int failures;

#define CHECK(condition) \
	do { \
		if (!(condition)) { \
			fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, \
				__LINE__, #condition); \
			failures++; \
		} \
	} while (0)

static sg_weapon_profile_t Resolve(sg_weapon_profile_id_t id,
	uint8_t balance, uint8_t quad, uint8_t deathmatch, uint8_t rail_match)
{
	const sg_weapon_law_input_t law = {
		.build_identity = 11U,
		.physics_abi_id = 22U,
		.weapon_balance_compiled = SG_WEAPON_BALANCE_COMPILED,
		.weapon_balance_enabled = balance,
		.quad_active = quad,
		.rail_match_active = rail_match,
		.deathmatch_active = deathmatch
	};
	sg_weapon_profile_t profile = {0};

	CHECK(SG_WeaponProfileResolve(id, &law, &profile));
	return profile;
}

static int HostRocketDamageAtRandomEndpoint(float unit_random)
{
	return SG_HOST_ROCKET_DAMAGE_BASE + (int)(unit_random *
		SG_HOST_ROCKET_DAMAGE_RANDOM_SPAN);
}

static int HostHandGrenadeSpeedAtTimer(float timer_seconds)
{
	return (int)((float)SG_HOST_HAND_GRENADE_MIN_SPEED +
		((float)SG_HOST_HAND_GRENADE_COOK_MS * 0.001f - timer_seconds) *
		(((float)SG_HOST_HAND_GRENADE_MAX_SPEED -
		  (float)SG_HOST_HAND_GRENADE_MIN_SPEED) /
		 ((float)SG_HOST_HAND_GRENADE_COOK_MS * 0.001f)));
}

static void TestRailAuxiliaryTraces(void)
{
	sg_weapon_profile_t rail = Resolve(SG_WEAPON_PROFILE_RAILGUN,
		SG_WEAPON_BALANCE_COMPILED, 0U, 1U, 0U);

	CHECK(rail.auxiliary_trace_count ==
		(SG_WEAPON_BALANCE_COMPILED != 0U ? 4U : 0U));
	CHECK(rail.auxiliary_trace_damage ==
		(SG_WEAPON_BALANCE_COMPILED != 0U ? 4.0f : 0.0f));
	CHECK(rail.auxiliary_horizontal_spread ==
		(SG_WEAPON_BALANCE_COMPILED != 0U ? 512.0f : 0.0f));
	CHECK(rail.auxiliary_vertical_spread ==
		(SG_WEAPON_BALANCE_COMPILED != 0U ? 512.0f : 0.0f));
	CHECK(rail.auxiliary_traces_penetrate == 0U);
	rail = Resolve(SG_WEAPON_PROFILE_RAILGUN,
		SG_WEAPON_BALANCE_COMPILED, 0U, 1U, 1U);
	CHECK(rail.direct_damage == 5000.0f);
	CHECK(rail.auxiliary_trace_count ==
		(SG_WEAPON_BALANCE_COMPILED != 0U ? 4U : 0U));
}

static void TestSplashLaws(void)
{
	sg_weapon_profile_t rocket = Resolve(SG_WEAPON_PROFILE_ROCKET_LAUNCHER,
		0U, 0U, 1U, 0U);
	sg_weapon_profile_t bfg = Resolve(SG_WEAPON_PROFILE_BFG,
		0U, 0U, 1U, 0U);

	CHECK(rocket.splash.kernel == SG_WEAPON_SPLASH_LINEAR_HALF_DISTANCE);
	CHECK(rocket.splash.owner == SG_WEAPON_SPLASH_OWNER_SELF_SCALE);
	CHECK(rocket.splash.owner_scale == 0.5f);
	CHECK(bfg.splash.kernel == SG_WEAPON_SPLASH_LINEAR_HALF_DISTANCE);
	CHECK(bfg.secondary_splash.kernel == SG_WEAPON_SPLASH_SQRT_NORMALIZED);
	CHECK(bfg.secondary_splash.owner == SG_WEAPON_SPLASH_OWNER_EXCLUDED);
	CHECK(bfg.splash.owner_scale == 0.5f);
	if (SG_WEAPON_BALANCE_COMPILED != 0U)
	{
		rocket = Resolve(SG_WEAPON_PROFILE_ROCKET_LAUNCHER,
			1U, 0U, 1U, 0U);
		CHECK(rocket.splash.kernel == SG_WEAPON_SPLASH_NORMALIZED_LINEAR);
	}
}

static void TestMissingFamiliesAndRuntimeInputs(void)
{
	sg_weapon_profile_t grenade = Resolve(SG_WEAPON_PROFILE_HAND_GRENADE,
		0U, 0U, 1U, 0U);
	sg_weapon_profile_t hook = Resolve(SG_WEAPON_PROFILE_HOOK,
		0U, 0U, 1U, 0U);
	sg_weapon_profile_t quad_hook = Resolve(SG_WEAPON_PROFILE_HOOK,
		0U, 1U, 1U, 0U);
	sg_weapon_profile_t plasma = Resolve(SG_WEAPON_PROFILE_PLASMA_REFLECT,
		0U, 1U, 1U, 0U);
	sg_weapon_profile_t rail_non_dm = Resolve(SG_WEAPON_PROFILE_RAILGUN,
		0U, 0U, 0U, 0U);

	CHECK(grenade.projectile_speed == 386.0f);
	CHECK(grenade.projectile_speed_max == 800.0f);
	CHECK(grenade.cook_ms == 3000U);
	CHECK(grenade.fuse_ms == 3100U);
	CHECK(hook.family == SG_WEAPON_FAMILY_SPECIAL);
	CHECK(hook.projectile_speed == 800.0f);
	CHECK(hook.hook_pull_speed == 800U);
	CHECK(quad_hook.direct_damage == 8.0f);
	CHECK(hook.ammo.ready_minimum == 0U);
	CHECK(hook.ammo.debit == 0U);
	CHECK(rail_non_dm.direct_damage == 150.0f);
	CHECK(plasma.ammo.ready_minimum == 10U);
	CHECK(plasma.ammo.live_fire_minimum == 1U);
	CHECK(plasma.ammo.debit == 10U);
	CHECK(plasma.ammo.infinite_ammo_debit == 9U);
	CHECK(plasma.damage_dependency ==
		SG_WEAPON_DAMAGE_IMPACT_GLOBAL_QUAD);
	CHECK(plasma.direct_damage == 39.0f);
	CHECK(plasma.direct_damage_max == 156.0f);
	CHECK((SG_WEAPON_EFFECT_MASK & (UINT32_C(1) << 6)) == 0U);
}

static void TestHostEquationBoundaries(void)
{
	const float first_release_timer =
		((float)(SG_HOST_HAND_GRENADE_HELD_FUSE_MS -
			 SG_HOST_SERVER_FRAME_MS)) * 0.001f;
	sg_weapon_profile_t rocket = Resolve(SG_WEAPON_PROFILE_ROCKET_LAUNCHER,
		0U, 0U, 1U, 0U);
	sg_weapon_profile_t quad_rocket = Resolve(
		SG_WEAPON_PROFILE_ROCKET_LAUNCHER, 0U, 1U, 1U, 0U);
	sg_weapon_profile_t grenade = Resolve(SG_WEAPON_PROFILE_HAND_GRENADE,
		0U, 0U, 1U, 0U);

	CHECK(HostRocketDamageAtRandomEndpoint(0.0f) == 100);
	CHECK(HostRocketDamageAtRandomEndpoint(1.0f) == 120);
	CHECK(rocket.direct_damage ==
		(float)HostRocketDamageAtRandomEndpoint(0.0f));
	CHECK(rocket.direct_damage_max ==
		(float)HostRocketDamageAtRandomEndpoint(1.0f));
	CHECK(quad_rocket.direct_damage ==
		(float)(HostRocketDamageAtRandomEndpoint(0.0f) *
		SG_HOST_DAMAGE_QUAD_SCALE));
	CHECK(quad_rocket.direct_damage_max ==
		(float)(HostRocketDamageAtRandomEndpoint(1.0f) *
		SG_HOST_DAMAGE_QUAD_SCALE));
	CHECK(HostHandGrenadeSpeedAtTimer(first_release_timer) == 386);
	CHECK(HostHandGrenadeSpeedAtTimer(0.0f) == 800);
	CHECK(grenade.projectile_speed ==
		(float)HostHandGrenadeSpeedAtTimer(first_release_timer));
	CHECK(grenade.projectile_speed_max ==
		(float)HostHandGrenadeSpeedAtTimer(0.0f));
	CHECK(grenade.fuse_ms == (uint32_t)(first_release_timer * 1000.0f));
}

static void TestTimingAndIds(void)
{
	sg_weapon_profile_t shotgun = Resolve(SG_WEAPON_PROFILE_SHOTGUN,
		0U, 0U, 1U, 0U);
	const sg_weapon_profile_t *profile = NULL;

	CHECK(shotgun.timing.activate_last_frame == 7U);
	CHECK(shotgun.timing.fire_last_frame == 18U);
	CHECK(shotgun.timing.idle_last_frame == 36U);
	CHECK(shotgun.timing.deactivate_last_frame == 39U);
	CHECK(shotgun.timing.first_effect_frame == 8U);
	CHECK(shotgun.timing.last_effect_frame == 8U);
	CHECK(SG_WeaponProfileIdValid((uint16_t)SG_WEAPON_PROFILE_HOOK));
	CHECK(!SG_WeaponProfileIdValid(UINT16_MAX));
	CHECK(!SG_WeaponProfileLookup((sg_weapon_profile_id_t)UINT16_MAX,
		&profile));
	{
		sg_weapon_law_input_t law = {
			.build_identity = 11U,
			.physics_abi_id = 22U,
			.weapon_balance_compiled = SG_WEAPON_BALANCE_COMPILED,
			.deathmatch_active = 1U
		};
		sg_weapon_profile_t rocket;
		sg_weapon_profile_t plasma;
		sg_weapon_profile_t hook;

		CHECK(SG_WeaponProfileResolve(SG_WEAPON_PROFILE_ROCKET_LAUNCHER,
			&law, &rocket));
		CHECK(rocket.timing.resolved_switch_in_ms == 500U);
		CHECK(rocket.timing.resolved_switch_out_ms == 400U);
		CHECK(SG_WeaponProfileResolve(SG_WEAPON_PROFILE_HOOK, &law, &hook));
		CHECK(hook.timing.resolved_switch_in_ms == 500U);
		CHECK(hook.timing.resolved_switch_out_ms == 300U);
		law.fast_switch_enabled = 1U;
		CHECK(SG_WeaponProfileResolve(SG_WEAPON_PROFILE_ROCKET_LAUNCHER,
			&law, &rocket));
		CHECK(rocket.timing.resolved_switch_in_ms == 0U);
		CHECK(rocket.timing.resolved_switch_out_ms == 0U);
		CHECK(SG_WeaponProfileResolve(SG_WEAPON_PROFILE_PLASMA_REFLECT,
			&law, &plasma));
		CHECK(plasma.timing.resolved_switch_in_ms == 400U);
		CHECK(plasma.timing.resolved_switch_out_ms == 500U);
	}
}

int main(void)
{
	TestRailAuxiliaryTraces();
	TestSplashLaws();
	TestMissingFamiliesAndRuntimeInputs();
	TestHostEquationBoundaries();
	TestTimingAndIds();
	if (failures != 0)
		return 1;
	puts("sg_weapon_effect_profile_correction_test: ok");
	return 0;
}
