#include "slipgate/sg_weapon_effect_profile.h"
#include "slipgate/sg_weapon_contract.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(condition) \
	do { \
		if (!(condition)) { \
			fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, \
				__LINE__, #condition); \
			failures++; \
		} \
	} while (0)

static const sg_weapon_profile_t *Profile(sg_weapon_profile_id_t id)
{
	const sg_weapon_profile_t *profile = NULL;

	CHECK(SG_WeaponProfileLookup(id, &profile));
	CHECK(profile != NULL);
	return profile;
}

static void TestCatalog(void)
{
	const sg_weapon_profile_t *sentinel =
		(const sg_weapon_profile_t *)(const void *)&failures;
	const sg_weapon_profile_t *profile = sentinel;
	sg_weapon_profile_t invalid;

	CHECK(SG_WeaponProfileCatalogValid());
	CHECK(SG_WeaponProfileCount() == 14U);
	CHECK(Profile(SG_WEAPON_PROFILE_SHOTGUN)->horizontal_spread == 500.0f);
	CHECK(Profile(SG_WEAPON_PROFILE_SHOTGUN)->ray_distance == 8192.0f);
	CHECK(Profile(SG_WEAPON_PROFILE_SUPER_SHOTGUN)->yaw_spread_degrees == 5.0f);
	CHECK(Profile(SG_WEAPON_PROFILE_MACHINEGUN)->family ==
		SG_WEAPON_FAMILY_HITSCAN);
	CHECK(Profile(SG_WEAPON_PROFILE_CHAINGUN)->projectile_count_max == 3U);
	CHECK(Profile(SG_WEAPON_PROFILE_CHAINGUN)->ammo.debit_maximum == 3U);
	CHECK(Profile(SG_WEAPON_PROFILE_ROCKET_LAUNCHER)->projectile_speed ==
		650.0f);
	CHECK(Profile(SG_WEAPON_PROFILE_ROCKET_LAUNCHER)->direct_damage_max ==
		119.0f);
	CHECK(Profile(SG_WEAPON_PROFILE_ROCKET_LAUNCHER)->
		projectile_retire_distance == 8000.0f);
	CHECK(Profile(SG_WEAPON_PROFILE_GRENADE_LAUNCHER)->fuse_ms == 2500U);
	CHECK(Profile(SG_WEAPON_PROFILE_GRENADE_LAUNCHER)->gravity_scale == 1.0f);
	CHECK(Profile(SG_WEAPON_PROFILE_HYPERBLASTER)->family ==
		SG_WEAPON_FAMILY_HYPERBLASTER);
	CHECK(Profile(SG_WEAPON_PROFILE_HYPERBLASTER)->projectile_lifetime_ms ==
		2000U);
	CHECK(Profile(SG_WEAPON_PROFILE_BFG)->windup_ms == 800U);
	CHECK(Profile(SG_WEAPON_PROFILE_BFG)->secondary_splash_radius == 1000.0f);
	CHECK(Profile(SG_WEAPON_PROFILE_BFG)->periodic_ray_damage == 5.0f);
	CHECK(Profile(SG_WEAPON_PROFILE_BFG)->periodic_ray_radius == 256.0f);
	CHECK(Profile(SG_WEAPON_PROFILE_BFG)->periodic_ray_distance == 2048.0f);
	CHECK(Profile(SG_WEAPON_PROFILE_BFG)->periodic_ray_interval_ms == 100U);
	CHECK(Profile(SG_WEAPON_PROFILE_PLASMA_REFLECT)->family ==
		SG_WEAPON_FAMILY_PLASMA_REFLECT);
	CHECK(Profile(SG_WEAPON_PROFILE_PLASMA_REFLECT)->splash_radius == 109.0f);
	CHECK(Profile(SG_WEAPON_PROFILE_PLASMA_REFLECT)->projectile_half_extent ==
		12.0f);
	CHECK(Profile(SG_WEAPON_PROFILE_PLASMA_SPREAD)->family ==
		SG_WEAPON_FAMILY_PLASMA_SPREAD);
	CHECK(Profile(SG_WEAPON_PROFILE_PLASMA_SPREAD)->projectile_count_min == 3U);
	CHECK(Profile(SG_WEAPON_PROFILE_PLASMA_SPREAD)->yaw_spread_degrees == 10.0f);

	CHECK(!SG_WeaponProfileLookup((sg_weapon_profile_id_t)0, &profile));
	CHECK(profile == sentinel);
	CHECK(!SG_WeaponProfileLookup(SG_WEAPON_PROFILE_COUNT, &profile));
	CHECK(profile == sentinel);
	CHECK(!SG_WeaponProfileLookup(SG_WEAPON_PROFILE_BLASTER, NULL));

	invalid = *Profile(SG_WEAPON_PROFILE_ROCKET_LAUNCHER);
	invalid.projectile_speed = NAN;
	CHECK(!SG_WeaponProfileValid(&invalid));
	invalid = *Profile(SG_WEAPON_PROFILE_ROCKET_LAUNCHER);
	invalid.direct_damage_max = invalid.direct_damage - 1.0f;
	CHECK(!SG_WeaponProfileValid(&invalid));
	invalid = *Profile(SG_WEAPON_PROFILE_PLASMA_SPREAD);
	invalid.effects &= ~(uint32_t)SG_WEAPON_EFFECT_MULTI_PROJECTILE;
	CHECK(!SG_WeaponProfileValid(&invalid));
	invalid = *Profile(SG_WEAPON_PROFILE_BFG);
	invalid.secondary_splash_radius = 0.0f;
	CHECK(!SG_WeaponProfileValid(&invalid));
	invalid = *Profile(SG_WEAPON_PROFILE_HOOK);
	invalid.hook_pull_speed = 0U;
	CHECK(!SG_WeaponProfileValid(&invalid));
	invalid = *Profile(SG_WEAPON_PROFILE_HOOK);
	invalid.hook_health = 0U;
	CHECK(!SG_WeaponProfileValid(&invalid));
	invalid = *Profile(SG_WEAPON_PROFILE_ROCKET_LAUNCHER);
	invalid.hook_pull_speed = 800U;
	CHECK(!SG_WeaponProfileValid(&invalid));
	invalid = *Profile(SG_WEAPON_PROFILE_ROCKET_LAUNCHER);
	invalid.effects |= UINT32_C(1) << 6;
	CHECK(!SG_WeaponProfileValid(&invalid));
	invalid = *Profile(SG_WEAPON_PROFILE_ROCKET_LAUNCHER);
	invalid.splash.kernel = (sg_weapon_splash_kernel_t)-1;
	CHECK(!SG_WeaponProfileValid(&invalid));
	invalid = *Profile(SG_WEAPON_PROFILE_ROCKET_LAUNCHER);
	invalid.splash.owner = (sg_weapon_splash_owner_t)-1;
	CHECK(!SG_WeaponProfileValid(&invalid));
	invalid = *Profile(SG_WEAPON_PROFILE_ROCKET_LAUNCHER);
	invalid.damage_dependency = (sg_weapon_damage_dependency_t)-1;
	CHECK(!SG_WeaponProfileValid(&invalid));
	invalid = *Profile(SG_WEAPON_PROFILE_ROCKET_LAUNCHER);
	invalid.cadence_kind = (sg_weapon_cadence_kind_t)-1;
	CHECK(!SG_WeaponProfileValid(&invalid));
}

static void CheckResolveRejected(sg_weapon_profile_id_t id,
	const sg_weapon_law_input_t *law)
{
	sg_weapon_profile_t before;
	sg_weapon_profile_t output;

	memset(&before, 0xa5, sizeof(before));
	output = before;
	CHECK(!SG_WeaponProfileResolve(id, law, &output));
	CHECK(memcmp(&output, &before, sizeof(output)) == 0);
}

static void TestRuntimeLawResolution(void)
{
	sg_weapon_law_input_t law = {
		.build_identity = UINT64_C(0x1111),
		.physics_abi_id = UINT64_C(0x2222),
		.weapon_balance_compiled = SG_WEAPON_BALANCE_COMPILED,
		.deathmatch_active = 1U
	};
	sg_weapon_profile_t resolved;

	CHECK(SG_WeaponProfileResolve(SG_WEAPON_PROFILE_ROCKET_LAUNCHER,
		&law, &resolved));
	CHECK(resolved.resolved == 1U &&
		resolved.build_identity == law.build_identity &&
		resolved.physics_abi_id == law.physics_abi_id);
	CHECK(resolved.projectile_speed == 650.0f &&
		resolved.direct_damage == 100.0f &&
		resolved.direct_damage_max == 119.0f &&
		resolved.splash_damage == 120.0f &&
		resolved.splash_radius == 120.0f);

	if (SG_WEAPON_BALANCE_COMPILED != 0U)
	{
		law.weapon_balance_enabled = 1U;
		CHECK(SG_WeaponProfileResolve(SG_WEAPON_PROFILE_ROCKET_LAUNCHER,
			&law, &resolved));
		CHECK(resolved.projectile_speed == 750.0f &&
			resolved.splash_damage == 75.0f &&
			resolved.splash_radius == 240.0f);
		CHECK(SG_WeaponProfileResolve(SG_WEAPON_PROFILE_GRENADE_LAUNCHER,
			&law, &resolved));
		CHECK(resolved.direct_damage == 110.0f &&
			resolved.splash_damage == 110.0f &&
			resolved.splash_radius == 240.0f);
		CHECK(SG_WeaponProfileResolve(SG_WEAPON_PROFILE_BLASTER, &law,
			&resolved));
		CHECK(resolved.projectile_lifetime_ms == 4000U);
		CHECK(resolved.projectile_half_extent == 8.0f);
	}
	else
	{
		law.weapon_balance_enabled = 1U;
		CheckResolveRejected(SG_WEAPON_PROFILE_ROCKET_LAUNCHER, &law);
		law.weapon_balance_enabled = 0U;
	}
	law.quad_active = 1U;
	CHECK(SG_WeaponProfileResolve(SG_WEAPON_PROFILE_ROCKET_LAUNCHER,
		&law, &resolved));
	CHECK(resolved.direct_damage == 400.0f &&
		resolved.direct_damage_max == 476.0f);
	if (SG_WEAPON_BALANCE_COMPILED != 0U)
	{
		CHECK(resolved.splash_damage == 300.0f &&
			resolved.splash_radius == 240.0f);
		CHECK(SG_WeaponProfileResolve(SG_WEAPON_PROFILE_GRENADE_LAUNCHER,
			&law, &resolved));
		CHECK(resolved.direct_damage == 470.0f &&
			resolved.splash_damage == 470.0f &&
			resolved.splash_radius == 240.0f);
		CHECK(SG_WeaponProfileResolve(SG_WEAPON_PROFILE_SHOTGUN, &law,
			&resolved));
		CHECK(resolved.direct_damage == 17.0f &&
			resolved.projectile_count_min == 14U);
		CHECK(SG_WeaponProfileResolve(SG_WEAPON_PROFILE_SUPER_SHOTGUN, &law,
			&resolved));
		CHECK(resolved.direct_damage == 21.0f &&
			resolved.projectile_count_min == 32U);
	}
	else
	{
		CHECK(resolved.splash_damage == 480.0f &&
			resolved.splash_radius == 120.0f);
	}

	CHECK(SG_WeaponProfileResolve(SG_WEAPON_PROFILE_PLASMA_REFLECT,
		&law, &resolved));
	CHECK(resolved.splash_damage == 39.0f &&
		resolved.splash_radius == 109.0f &&
		resolved.direct_damage_max == 156.0f);
	CHECK(SG_WeaponProfileResolve(SG_WEAPON_PROFILE_PLASMA_SPREAD,
		&law, &resolved));
	CHECK(resolved.splash_damage == 28.0f &&
		resolved.splash_radius == 98.0f &&
		resolved.direct_damage_max == 112.0f);

	CHECK(SG_WeaponProfileResolve(SG_WEAPON_PROFILE_BFG, &law, &resolved));
	CHECK(resolved.direct_damage == 200.0f && resolved.splash_damage == 200.0f &&
		resolved.splash_radius == 100.0f &&
		resolved.secondary_splash_damage ==
			(SG_WEAPON_BALANCE_COMPILED != 0U ? 720.0f : 800.0f));
	CHECK(resolved.projectile_speed ==
		(SG_WEAPON_BALANCE_COMPILED != 0U ? 180.0f : 400.0f));
	CHECK(resolved.secondary_splash_radius ==
		(SG_WEAPON_BALANCE_COMPILED != 0U ? 1200.0f : 1000.0f));
	CHECK(resolved.periodic_ray_damage ==
		(SG_WEAPON_BALANCE_COMPILED != 0U ? 3.0f : 5.0f));

	law.rail_match_active = 1U;
	CHECK(SG_WeaponProfileResolve(SG_WEAPON_PROFILE_RAILGUN, &law,
		&resolved));
	CHECK(resolved.direct_damage == 20000.0f);

	law.weapon_balance_compiled =
		(uint8_t)(SG_WEAPON_BALANCE_COMPILED == 0U ? 1U : 0U);
	CheckResolveRejected(SG_WEAPON_PROFILE_ROCKET_LAUNCHER, &law);
	law.weapon_balance_enabled = 0U;
	law.quad_active = 2U;
	CheckResolveRejected(SG_WEAPON_PROFILE_ROCKET_LAUNCHER, &law);
	CHECK(!SG_WeaponProfileResolve(SG_WEAPON_PROFILE_ROCKET_LAUNCHER,
		NULL, &resolved));
	CHECK(!SG_WeaponProfileResolve(SG_WEAPON_PROFILE_ROCKET_LAUNCHER,
		&law, NULL));
}

static void FillIdentity(sg_rune_v2_content_id_t *identity, uint8_t seed)
{
	uint32_t index;

	for (index = 0U; index < SG_RUNE_V2_CONTENT_ID_BYTES; index++)
		identity->bytes[index] = (uint8_t)(seed + (uint8_t)index);
}

static sg_rune_stable_id_t StableId(uint64_t source_set_identity,
	uint32_t domain, uint32_t source_index)
{
	const sg_rune_order_key_t key = {
		.source_set_identity = source_set_identity,
		.domain = domain,
		.source_index = source_index,
		.local_ordinal = source_index,
		.variant = 0U
	};

	return SG_RuneModelStableIdFromOrderKey(&key);
}

static sg_weapon_static_query_input_t StaticInput(void)
{
	const uint64_t source_set_identity = UINT64_C(0x1111222233334444);
	sg_weapon_static_query_input_t input;

	memset(&input, 0, sizeof(input));
	FillIdentity(&input.binding.artifact_identity, 1U);
	FillIdentity(&input.binding.bsp_identity, 33U);
	FillIdentity(&input.binding.schema_identity, 65U);
	input.binding.source_set_identity = source_set_identity;
	input.binding.visibility_revision = 7U;
	input.source_cell.value = StableId(source_set_identity,
		SG_RUNE_ORDER_CELL, 1U);
	input.target_cell.value = StableId(source_set_identity,
		SG_RUNE_ORDER_CELL, 2U);
	input.source_phase.value = StableId(source_set_identity,
		SG_RUNE_ORDER_PHASE, 3U);
	input.target_phase.value = StableId(source_set_identity,
		SG_RUNE_ORDER_PHASE, 4U);
	input.source_origin.value[0] = 10.0f;
	input.source_origin.value[1] = 20.0f;
	input.source_origin.value[2] = 30.0f;
	input.target_origin.value[0] = 100.0f;
	input.target_origin.value[1] = 20.0f;
	input.target_origin.value[2] = 30.0f;
	input.target_bounds.mins.value[0] = 84.0f;
	input.target_bounds.mins.value[1] = 4.0f;
	input.target_bounds.mins.value[2] = 6.0f;
	input.target_bounds.maxs.value[0] = 116.0f;
	input.target_bounds.maxs.value[1] = 36.0f;
	input.target_bounds.maxs.value[2] = 62.0f;
	input.requested_relations = SG_WEAPON_STATIC_DIRECT_VISIBILITY |
		SG_WEAPON_STATIC_PROJECTILE_CORRIDOR |
		SG_WEAPON_STATIC_IMPACT_SURFACE | SG_WEAPON_STATIC_BLAST_REACH |
		SG_WEAPON_STATIC_BOUNCE_SURFACE;
	return input;
}

static void CheckRejectedTransaction(const sg_weapon_static_query_input_t *input)
{
	sg_weapon_static_query_t before;
	sg_weapon_static_query_t output;

	memset(&before, 0xa5, sizeof(before));
	output = before;
	CHECK(!SG_WeaponStaticQueryPrepare(input, &output));
	CHECK(memcmp(&output, &before, sizeof(output)) == 0);
}

static void TestStaticQueryTransaction(void)
{
	sg_weapon_static_query_input_t input = StaticInput();
	sg_weapon_static_query_input_t invalid;
	sg_weapon_static_query_t output;

	memset(&output, 0, sizeof(output));
	CHECK(SG_WeaponStaticBindingValid(&input.binding));
	CHECK(SG_WeaponStaticQueryPrepare(&input, &output));
	CHECK(output.exact_live_prefire_trace_required == 1U);
	CHECK(output.requested_relations == input.requested_relations);
	CHECK(memcmp(&output.binding, &input.binding,
		sizeof(output.binding)) == 0);
	CHECK(SG_RuneModelStableIdEqual(&output.source_cell.value,
		&input.source_cell.value));

	/* The geometry transaction has no profile field: one BSP-derived answer
	 * can feed both of these distinct physical kernels. */
	CHECK(Profile(SG_WEAPON_PROFILE_RAILGUN)->family ==
		SG_WEAPON_FAMILY_HITSCAN);
	CHECK(Profile(SG_WEAPON_PROFILE_ROCKET_LAUNCHER)->family ==
		SG_WEAPON_FAMILY_ROCKET_SPLASH);

	invalid = input;
	memset(&invalid.binding.artifact_identity, 0,
		sizeof(invalid.binding.artifact_identity));
	CheckRejectedTransaction(&invalid);
	invalid = input;
	invalid.binding.source_set_identity++;
	CheckRejectedTransaction(&invalid);
	invalid = input;
	invalid.source_cell.value = input.source_phase.value;
	CheckRejectedTransaction(&invalid);
	invalid = input;
	invalid.target_phase.value = input.target_cell.value;
	CheckRejectedTransaction(&invalid);
	invalid = input;
	invalid.binding.visibility_revision = 0U;
	CheckRejectedTransaction(&invalid);
	invalid = input;
	invalid.requested_relations = 0U;
	CheckRejectedTransaction(&invalid);
	invalid = input;
	invalid.requested_relations |= UINT32_C(0x80000000);
	CheckRejectedTransaction(&invalid);
	invalid = input;
	invalid.source_origin.value[1] = INFINITY;
	CheckRejectedTransaction(&invalid);
	invalid = input;
	invalid.target_bounds.mins.value[2] =
		invalid.target_bounds.maxs.value[2] + 1.0f;
	CheckRejectedTransaction(&invalid);
	CHECK(!SG_WeaponStaticQueryPrepare(NULL, &output));
	CHECK(!SG_WeaponStaticQueryPrepare(&input, NULL));
}

static sg_weapon_prefire_request_t PrefireRequest(void)
{
	return (sg_weapon_prefire_request_t){
		.shot_id = 1U,
		.shot_revision = 2U,
		.rune_identity = 3U,
		.pose_revision = 4U,
		.fired_at_ms = 100U,
		.prediction_time_ms = 100U,
		.source_cell_id = 1U,
		.target_cell_id = 2U,
		.shooter_client = 1U,
		.target_client = 2U,
		.profile_id = SG_WEAPON_PROFILE_ROCKET_LAUNCHER,
		.shooter_team = 1U,
		.target_team = 2U,
		.audience_team = 1U,
		.exact_required = 1U,
		.muzzle_origin = { 1.0f, 2.0f, 3.0f },
		.aim_direction = { 1.0f, 0.0f, 0.0f },
		.intended_impact = { 101.0f, 2.0f, 3.0f }
	};
}

static sg_weapon_prefire_validation_t PrefireValidation(void)
{
	return (sg_weapon_prefire_validation_t){
		.shot_id = 1U,
		.shot_revision = 2U,
		.rune_identity = 3U,
		.pose_revision = 4U,
		.fired_at_ms = 100U,
		.prediction_time_ms = 100U,
		.source_cell_id = 1U,
		.target_cell_id = 2U,
		.shooter_client = 1U,
		.target_client = 2U,
		.profile_id = SG_WEAPON_PROFILE_ROCKET_LAUNCHER,
		.shooter_team = 1U,
		.target_team = 2U,
		.audience_team = 1U,
		.trace_status = SG_WEAPON_TRACE_ACCEPTED,
		.muzzle_clear = 1U,
		.host_agrees = 1U,
		.authenticated = 1U,
		.authorization_id = 5U,
		.muzzle_origin = { 1.0f, 2.0f, 3.0f },
		.aim_direction = { 1.0f, 0.0f, 0.0f },
		.intended_impact = { 101.0f, 2.0f, 3.0f }
	};
}

static void TestLivePrefireBoundary(void)
{
	sg_weapon_prefire_request_t request = PrefireRequest();
	sg_weapon_prefire_validation_t validation = PrefireValidation();

	CHECK(SG_WeaponPrefireAllowed(&request, &validation));
	validation.trace_status = SG_WEAPON_TRACE_NOT_RUN;
	CHECK(!SG_WeaponPrefireAllowed(&request, &validation));
	validation = PrefireValidation();
	validation.trace_status = SG_WEAPON_TRACE_REJECTED;
	CHECK(!SG_WeaponPrefireAllowed(&request, &validation));
	validation = PrefireValidation();
	validation.muzzle_clear = 0U;
	CHECK(!SG_WeaponPrefireAllowed(&request, &validation));
	validation = PrefireValidation();
	validation.pose_revision++;
	CHECK(!SG_WeaponPrefireAllowed(&request, &validation));
	validation = PrefireValidation();
	validation.authorization_id = 0U;
	CHECK(!SG_WeaponPrefireAllowed(&request, &validation));
	request = PrefireRequest();
	request.profile_id = UINT16_MAX;
	CHECK(!SG_WeaponPrefireRequestValid(&request));
	request = PrefireRequest();
	validation = PrefireValidation();
	validation.profile_id = UINT16_MAX;
	CHECK(!SG_WeaponPrefireShotMatches(&request, &validation));
}

int main(void)
{
	TestCatalog();
	TestRuntimeLawResolution();
	TestStaticQueryTransaction();
	TestLivePrefireBoundary();
	if (failures != 0)
	{
		fprintf(stderr, "sg_weapon_effect_profile_test: %d failure(s)\n",
			failures);
		return 1;
	}
	puts("sg_weapon_effect_profile_test: ok");
	return 0;
}
