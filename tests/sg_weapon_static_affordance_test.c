#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../slipgate/sg_weapon_static_affordance.h"
#include "sg_weapon_static_affordance_fixture.h"

static sg_rune_phase_ref_t PhaseRef(uint64_t source_set_identity,
	uint32_t source_index)
{
	const sg_rune_order_key_t key = {
		.source_set_identity = source_set_identity,
		.domain = SG_RUNE_ORDER_PHASE,
		.source_index = source_index,
		.local_ordinal = source_index,
		.variant = 0U
	};
	sg_rune_phase_ref_t phase;

	phase.value = SG_RuneModelStableIdFromOrderKey(&key);
	return phase;
}

static sg_weapon_profile_t ResolveProfile(const built_fixture_t *built,
	sg_weapon_profile_id_t id)
{
	const sg_weapon_law_input_t law = {
		.build_identity = built->fixture.identity.producer_identity,
		.physics_abi_id = built->fixture.identity.physics_abi_id,
		.weapon_balance_compiled = SG_WEAPON_BALANCE_COMPILED,
		.deathmatch_active = 1U
	};
	sg_weapon_profile_t profile;

	memset(&profile, 0, sizeof(profile));
	CHECK(SG_WeaponProfileResolve(id, &law, &profile));
	return profile;
}

static void CellsAtPoints(const built_fixture_t *built,
	const float source[3], const float target[3],
	sg_rune_cell_ref_t *source_cell, sg_rune_cell_ref_t *target_cell,
	sg_rune_phase_ref_t *source_phase, sg_rune_phase_ref_t *target_phase)
{
	sg_static_visibility_result_t visibility;
	sg_static_visibility_error_t error;
	uint32_t source_index, target_index;

	CHECK(SG_StaticVisibilityQueryPoints(&built->fixture.authority,
		&empty_scene, built->configuration, built->semantics,
		built->visibility, source, target, &visibility, &error));
	if (visibility.source_partition >= built->visibility->partition_count ||
		visibility.destination_partition >=
			built->visibility->partition_count)
	{
		memset(source_cell, 0, sizeof(*source_cell));
		memset(target_cell, 0, sizeof(*target_cell));
		memset(source_phase, 0, sizeof(*source_phase));
		memset(target_phase, 0, sizeof(*target_phase));
		return;
	}
	source_index = built->visibility->partitions[
		visibility.source_partition].configuration_cell;
	target_index = built->visibility->partitions[
		visibility.destination_partition].configuration_cell;
	*source_cell = built->configuration->cells[source_index].id;
	*target_cell = built->configuration->cells[target_index].id;
	*source_phase = built->model_phases[source_index].id;
	*target_phase = built->model_phases[target_index].id;
}

static sg_weapon_static_query_t Query(const built_fixture_t *built,
	const float source[3], const float target[3],
	const sg_rune_bounds_t *target_bounds,
	sg_weapon_static_relation_t requested)
{
	sg_weapon_static_query_input_t input;
	sg_weapon_static_query_t query;
	sg_rune_cell_ref_t source_cell, target_cell;
	sg_rune_phase_ref_t source_phase, target_phase;
	uint32_t axis;

	memset(&input, 0, sizeof(input));
	input.binding = built->binding;
	CellsAtPoints(built, source, target, &source_cell, &target_cell,
		&source_phase, &target_phase);
	input.source_cell = source_cell;
	input.target_cell = target_cell;
	input.source_phase = source_phase;
	input.target_phase = target_phase;
	for (axis = 0U; axis < 3U; axis++)
	{
		input.source_origin.value[axis] = source[axis];
		input.target_origin.value[axis] = target[axis];
	}
	input.target_bounds = *target_bounds;
	input.requested_relations = requested;
	memset(&query, 0, sizeof(query));
	CHECK(SG_WeaponStaticQueryPrepare(&input, &query));
	return query;
}

static sg_rune_bounds_t BoundsAt(const float point[3], float half_extent)
{
	sg_rune_bounds_t bounds;
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
	{
		bounds.mins.value[axis] = point[axis] - half_extent;
		bounds.maxs.value[axis] = point[axis] + half_extent;
	}
	return bounds;
}

static sg_weapon_static_relation_t ExpectedAllowed(
	const sg_weapon_profile_t *profile)
{
	sg_weapon_static_relation_t allowed = 0U;

	if ((profile->effects & (SG_WEAPON_EFFECT_HITSCAN |
			SG_WEAPON_EFFECT_PERIODIC_RAY)) != 0U)
		allowed |= SG_WEAPON_STATIC_DIRECT_VISIBILITY;
	if ((profile->effects & SG_WEAPON_EFFECT_PROJECTILE) != 0U)
		allowed |= SG_WEAPON_STATIC_PROJECTILE_CORRIDOR;
	if (profile->supports_occluded_impact != 0U ||
		(profile->effects & SG_WEAPON_EFFECT_SPECIAL) != 0U)
		allowed |= SG_WEAPON_STATIC_IMPACT_SURFACE;
	if ((profile->effects & (SG_WEAPON_EFFECT_SPLASH |
			SG_WEAPON_EFFECT_SECONDARY_AREA)) != 0U)
		allowed |= SG_WEAPON_STATIC_BLAST_REACH;
	if ((profile->effects & SG_WEAPON_EFFECT_BOUNCE) != 0U)
		allowed |= SG_WEAPON_STATIC_BOUNCE_SURFACE;
	return allowed;
}

static int ResolveAffordance(const built_fixture_t *built,
	const sg_host_collision_scene_t *scene,
	const sg_weapon_static_query_t *query,
	const sg_weapon_profile_t *profile,
	sg_weapon_static_affordance_t *affordance,
	sg_weapon_static_affordance_error_t *error)
{
	const sg_weapon_static_sources_t sources = {
		.binding = query->binding,
		.authority = &built->fixture.authority,
		.configuration = built->configuration,
		.semantics = built->semantics,
		.visibility = built->visibility,
		.model = &built->model,
		.audit = &built->source_audit
	};

	return SG_WeaponStaticAffordanceResolve(&sources, scene, query, profile,
		affordance, error);
}

static int FindWorldWallSurface(const built_fixture_t *built,
	float target[3], uint32_t *surface_out)
{
	uint32_t surface;

	for (surface = 0U; surface < built->semantics->hook_surface_count;
		surface++)
	{
		const sg_configuration_hook_surface_t *record =
			&built->semantics->hook_surfaces[surface];
		uint32_t vertex, axis;

		if (record->model != 0U || record->normal[0] > -0.9f)
			continue;
		memset(target, 0, sizeof(float) * 3U);
		for (vertex = 0U; vertex < record->vertex_count; vertex++)
			for (axis = 0U; axis < 3U; axis++)
				target[axis] += built->semantics->hook_vertices[
					record->first_vertex + vertex].value[axis];
		for (axis = 0U; axis < 3U; axis++)
			target[axis] /= (float)record->vertex_count;
		*surface_out = surface;
		return 1;
	}
	return 0;
}

static fixture_t FloorFixture(void)
{
	fixture_t fixture = Fixture(1, 0, 0, 1, 0, 0.0f);
	uint32_t plane;

	for (plane = 0U; plane < fixture.world.plane_count; plane++)
	{
		float x = fixture.planes[plane].normal.value[0];

		fixture.planes[plane].normal.value[0] =
			fixture.planes[plane].normal.value[2];
		fixture.planes[plane].normal.value[2] = x;
	}
	return fixture;
}

static fixture_t PartialMoverFixture(void)
{
	fixture_t fixture = Fixture(1, 0, 0, 1, 0, 0.0f);

	SetPlane(&fixture.planes[7], 0.0f, 1.0f, 0.0f, 0.5f);
	fixture.sides[8].plane = 7U;
	fixture.world.plane_count = 8U;
	return fixture;
}

static void CheckWitnessOnSelectedSurface(const built_fixture_t *built,
	const sg_weapon_static_relation_result_t *relation)
{
	const sg_configuration_hook_surface_t *surface;
	float distance;

	CHECK(relation->has_witness_point == 1U);
	CHECK(relation->visibility.surface < built->semantics->hook_surface_count);
	if (relation->visibility.surface >= built->semantics->hook_surface_count)
		return;
	surface = &built->semantics->hook_surfaces[relation->visibility.surface];
	distance = relation->witness_point.value[0] * surface->normal[0] +
		relation->witness_point.value[1] * surface->normal[1] +
		relation->witness_point.value[2] * surface->normal[2] -
		surface->distance;
	CHECK(fabsf(distance) <= 0.03125f);
}

static void TestEveryProfileFamilyAndEffect(void)
{
	built_fixture_t built;
	float source[3], target[3];
	sg_rune_bounds_t bounds;
	sg_weapon_static_query_t query;
	uint32_t id;

	CHECK(BuildFixture(&built, 0, 0, 0, 1, 0, 0.0f));
	if (!built.visibility)
	{
		DestroyFixture(&built);
		return;
	}
	SidePoints(0, 0.0f, source, target);
	bounds = BoundsAt(target, 16.0f);
	query = Query(&built, source, target, &bounds,
		SG_WEAPON_STATIC_RELATION_MASK);
	for (id = 1U; id < (uint32_t)SG_WEAPON_PROFILE_COUNT; id++)
	{
		sg_weapon_profile_t profile = ResolveProfile(&built,
			(sg_weapon_profile_id_t)id);
		sg_weapon_static_affordance_t affordance;
		sg_weapon_static_affordance_error_t error;
		sg_weapon_static_relation_t point_relations;

		CHECK(ResolveAffordance(&built, &empty_scene, &query, &profile,
			&affordance, &error));
		CHECK(error.code == SG_WEAPON_STATIC_AFFORDANCE_ERROR_NONE);
		CHECK(affordance.profile_id == profile.id);
		CHECK(affordance.family == profile.family);
		CHECK(affordance.allowed_relations == ExpectedAllowed(&profile));
		CHECK(affordance.exact_authenticated_live_prefire_trace_required == 1U);
		CHECK(memcmp(&affordance.binding, &query.binding,
			sizeof(affordance.binding)) == 0);
		CHECK((affordance.proven_relations |
			affordance.rejected_relations |
			affordance.conditional_relations) == query.requested_relations);
		point_relations = affordance.allowed_relations &
			(SG_WEAPON_STATIC_DIRECT_VISIBILITY |
			 SG_WEAPON_STATIC_PROJECTILE_CORRIDOR);
		CHECK((affordance.proven_relations & point_relations) ==
			point_relations);
		CHECK((affordance.conditional_relations & point_relations) == 0U);
	}
	DestroyFixture(&built);
}

static void TestOcclusionImpactSplashBounceAndSky(void)
{
	built_fixture_t built;
	float source[3], cell_target[3], impact[3] = { 0.0f, 0.0f, 0.0f };
	float victim[3] = { -100.0f, 0.0f, 0.0f };
	sg_rune_bounds_t bounds = BoundsAt(victim, 8.0f);
	sg_weapon_static_query_t query;
	sg_weapon_static_affordance_t affordance;
	sg_weapon_static_affordance_error_t error;
	sg_weapon_profile_t rocket, grenade, hook;
	uint32_t surface = 0U, side;

	CHECK(BuildFixture(&built, 1, 0, 0, 1, 0, 0.0f));
	if (!built.visibility)
	{
		DestroyFixture(&built);
		return;
	}
	SidePoints(0, 0.0f, source, cell_target);
	CHECK(FindWorldWallSurface(&built, impact, &surface));
	rocket = ResolveProfile(&built, SG_WEAPON_PROFILE_ROCKET_LAUNCHER);
	bounds = BoundsAt(cell_target, 8.0f);
	query = Query(&built, source, cell_target, &bounds,
		SG_WEAPON_STATIC_PROJECTILE_CORRIDOR);
	CHECK(ResolveAffordance(&built, &empty_scene, &query, &rocket,
		&affordance, &error));
	CHECK((affordance.rejected_relations &
		SG_WEAPON_STATIC_PROJECTILE_CORRIDOR) != 0U);
	CHECK(affordance.relations[1].visibility.reason ==
		SG_STATIC_VISIBILITY_REASON_STATIC_WORLD);
	bounds = BoundsAt(victim, 8.0f);
	query = Query(&built, source, victim, &bounds,
		SG_WEAPON_STATIC_IMPACT_SURFACE | SG_WEAPON_STATIC_BLAST_REACH |
		SG_WEAPON_STATIC_BOUNCE_SURFACE);
	CHECK(ResolveAffordance(&built, &empty_scene, &query, &rocket,
		&affordance, &error));
	CHECK((affordance.proven_relations & SG_WEAPON_STATIC_IMPACT_SURFACE) != 0U);
	CHECK((affordance.proven_relations & SG_WEAPON_STATIC_BLAST_REACH) != 0U);
	CHECK(affordance.relations[2].visibility.surface == surface);
	CHECK(affordance.relations[2].has_witness_point == 1U);
	CHECK(memcmp(affordance.relations[2].witness_point.value, victim,
		sizeof(victim)) != 0);

	grenade = ResolveProfile(&built, SG_WEAPON_PROFILE_GRENADE_LAUNCHER);
	CHECK(ResolveAffordance(&built, &empty_scene, &query, &grenade,
		&affordance, &error));
	CHECK((affordance.proven_relations & SG_WEAPON_STATIC_BOUNCE_SURFACE) != 0U);

	hook = ResolveProfile(&built, SG_WEAPON_PROFILE_HOOK);
	CHECK(ResolveAffordance(&built, &empty_scene, &query, &hook,
		&affordance, &error));
	CHECK((affordance.proven_relations & SG_WEAPON_STATIC_IMPACT_SURFACE) != 0U);
	CHECK((affordance.rejected_relations & SG_WEAPON_STATIC_BLAST_REACH) != 0U);

	DestroyFixture(&built);
	{
		fixture_t sky = Fixture(1, 0, 0, 1, 0, 0.0f);

		for (side = 0U; side < sky.world.brush_side_count; side++)
			sky.sides[side].texinfo = 1U;
		CHECK(BuildPreparedFixture(&built, sky));
	}
	if (!built.visibility)
	{
		DestroyFixture(&built);
		return;
	}
	SidePoints(0, 0.0f, source, cell_target);
	bounds = BoundsAt(victim, 8.0f);
	query = Query(&built, source, victim, &bounds,
		SG_WEAPON_STATIC_IMPACT_SURFACE | SG_WEAPON_STATIC_BLAST_REACH |
		SG_WEAPON_STATIC_BOUNCE_SURFACE);
	grenade = ResolveProfile(&built, SG_WEAPON_PROFILE_GRENADE_LAUNCHER);
	CHECK(ResolveAffordance(&built, &empty_scene, &query, &grenade,
		&affordance, &error));
	CHECK((affordance.rejected_relations & SG_WEAPON_STATIC_IMPACT_SURFACE) != 0U);
	CHECK((affordance.rejected_relations & SG_WEAPON_STATIC_BLAST_REACH) != 0U);
	CHECK((affordance.rejected_relations & SG_WEAPON_STATIC_BOUNCE_SURFACE) != 0U);
	CHECK(affordance.relations[2].visibility.reason ==
		SG_STATIC_VISIBILITY_REASON_SKY);
	DestroyFixture(&built);
}

static void TestConditionalMoverAndAreaPortal(void)
{
	built_fixture_t built;
	float source[3], target[3];
	sg_rune_bounds_t bounds;
	sg_weapon_static_query_t query;
	sg_weapon_static_affordance_t affordance;
	sg_weapon_static_affordance_error_t error;
	sg_weapon_profile_t rail, blaster;
	sg_host_collision_instance_t instance;
	sg_host_collision_scene_t scene;

	CHECK(BuildFixture(&built, 0, 0, 0, 1, 0, 0.0f));
	if (!built.visibility)
	{
		DestroyFixture(&built);
		return;
	}
	SidePoints(0, 0.0f, source, target);
	bounds = BoundsAt(target, 16.0f);
	query = Query(&built, source, target, &bounds,
		SG_WEAPON_STATIC_DIRECT_VISIBILITY |
		SG_WEAPON_STATIC_PROJECTILE_CORRIDOR);
	rail = ResolveProfile(&built, SG_WEAPON_PROFILE_RAILGUN);
	blaster = ResolveProfile(&built, SG_WEAPON_PROFILE_BLASTER);
	memset(&instance, 0, sizeof(instance));
	instance.instance_id = 17U;
	instance.model_index = 1U;
	instance.transform.origin[0] = -50.0f;
	scene.instances = &instance;
	scene.instance_count = 1U;
	CHECK(ResolveAffordance(&built, &scene, &query, &rail, &affordance,
		&error));
	CHECK((affordance.conditional_relations &
		SG_WEAPON_STATIC_DIRECT_VISIBILITY) != 0U);
	CHECK(affordance.relations[0].visibility.reason ==
		SG_STATIC_VISIBILITY_REASON_MOVING_SUBMODEL);
	CHECK(ResolveAffordance(&built, &scene, &query, &blaster, &affordance,
		&error));
	CHECK((affordance.conditional_relations &
		SG_WEAPON_STATIC_PROJECTILE_CORRIDOR) != 0U);
	DestroyFixture(&built);

	CHECK(BuildFixture(&built, 0, 1, 1, 1, 0, 0.0f));
	if (!built.visibility)
	{
		DestroyFixture(&built);
		return;
	}
	SidePoints(0, 0.0f, source, target);
	bounds = BoundsAt(target, 16.0f);
	query = Query(&built, source, target, &bounds,
		SG_WEAPON_STATIC_DIRECT_VISIBILITY);
	rail = ResolveProfile(&built, SG_WEAPON_PROFILE_RAILGUN);
	CHECK(ResolveAffordance(&built, &empty_scene, &query, &rail,
		&affordance, &error));
	CHECK((affordance.conditional_relations &
		SG_WEAPON_STATIC_DIRECT_VISIBILITY) != 0U);
	CHECK(affordance.relations[0].visibility.reason ==
		SG_STATIC_VISIBILITY_REASON_AREA_PORTAL_STATE);
	DestroyFixture(&built);
}

static void TestConditionalSurfaceAndSplashRange(void)
{
	built_fixture_t built;
	float source[3], cell_target[3], impact[3] = { 0.0f, 0.0f, 0.0f };
	float near_victim[3] = { -100.0f, 0.0f, 0.0f };
	float occluded_victim[3] = { 100.0f, 0.0f, 0.0f };
	float far_victim[3] = { 1000.0f, 0.0f, 0.0f };
	sg_rune_bounds_t bounds;
	sg_weapon_static_query_t query;
	sg_weapon_static_affordance_t affordance;
	sg_weapon_static_affordance_error_t error;
	sg_weapon_profile_t rocket;
	sg_host_collision_instance_t instance;
	sg_host_collision_scene_t scene;
	uint32_t surface = 0U;

	CHECK(BuildFixture(&built, 1, 0, 0, 1, 0, 0.0f));
	if (!built.visibility)
	{
		DestroyFixture(&built);
		return;
	}
	SidePoints(0, 0.0f, source, cell_target);
	CHECK(FindWorldWallSurface(&built, impact, &surface));
	bounds = BoundsAt(near_victim, 8.0f);
	query = Query(&built, source, near_victim, &bounds,
		SG_WEAPON_STATIC_IMPACT_SURFACE | SG_WEAPON_STATIC_BLAST_REACH);
	rocket = ResolveProfile(&built, SG_WEAPON_PROFILE_ROCKET_LAUNCHER);
	memset(&instance, 0, sizeof(instance));
	instance.instance_id = 19U;
	instance.model_index = 1U;
	instance.transform.origin[0] = -50.0f;
	scene.instances = &instance;
	scene.instance_count = 1U;
	CHECK(ResolveAffordance(&built, &scene, &query, &rocket, &affordance,
		&error));
	CHECK((affordance.conditional_relations &
		SG_WEAPON_STATIC_IMPACT_SURFACE) != 0U);
	CHECK((affordance.conditional_relations &
		SG_WEAPON_STATIC_BLAST_REACH) != 0U);
	CHECK(affordance.relations[2].visibility.reason ==
		SG_STATIC_VISIBILITY_REASON_MOVING_SUBMODEL);

	bounds = BoundsAt(occluded_victim, 8.0f);
	query = Query(&built, source, occluded_victim, &bounds,
		SG_WEAPON_STATIC_IMPACT_SURFACE | SG_WEAPON_STATIC_BLAST_REACH);
	CHECK(ResolveAffordance(&built, &empty_scene, &query, &rocket,
		&affordance, &error));
	CHECK((affordance.proven_relations & SG_WEAPON_STATIC_IMPACT_SURFACE) != 0U);
	CHECK((affordance.conditional_relations &
		SG_WEAPON_STATIC_BLAST_REACH) != 0U);
	CHECK(affordance.relations[3].visibility.reason ==
		SG_STATIC_VISIBILITY_REASON_MOVING_SUBMODEL);

	bounds = BoundsAt(far_victim, 8.0f);
	query = Query(&built, source, far_victim, &bounds,
		SG_WEAPON_STATIC_IMPACT_SURFACE | SG_WEAPON_STATIC_BLAST_REACH);
	CHECK(ResolveAffordance(&built, &empty_scene, &query, &rocket,
		&affordance, &error));
	CHECK((affordance.proven_relations & SG_WEAPON_STATIC_IMPACT_SURFACE) != 0U);
	CHECK((affordance.rejected_relations & SG_WEAPON_STATIC_BLAST_REACH) != 0U);
	CHECK(affordance.relations[3].reason ==
		SG_WEAPON_STATIC_REASON_OUTSIDE_SPLASH_REACH);
	DestroyFixture(&built);
}

static void TestNearbyFloorImpactUsesPolygonPoint(void)
{
	built_fixture_t built;
	const float source[3] = { 0.0f, 0.0f, 100.0f };
	const float actor[3] = { 0.0f, 0.0f, 80.0f };
	sg_rune_bounds_t bounds = BoundsAt(actor, 8.0f);
	sg_weapon_static_query_t query;
	sg_weapon_static_affordance_t affordance;
	sg_weapon_static_affordance_error_t error;
	sg_weapon_profile_t rocket;

	CHECK(BuildPreparedFixture(&built, FloorFixture()));
	if (!built.visibility)
	{
		DestroyFixture(&built);
		return;
	}
	rocket = ResolveProfile(&built, SG_WEAPON_PROFILE_ROCKET_LAUNCHER);
	query = Query(&built, source, actor, &bounds,
		SG_WEAPON_STATIC_IMPACT_SURFACE | SG_WEAPON_STATIC_BLAST_REACH);
	CHECK(ResolveAffordance(&built, &empty_scene, &query, &rocket,
		&affordance, &error));
	CHECK((affordance.proven_relations & SG_WEAPON_STATIC_IMPACT_SURFACE) != 0U);
	CHECK((affordance.proven_relations & SG_WEAPON_STATIC_BLAST_REACH) != 0U);
	CheckWitnessOnSelectedSurface(&built, &affordance.relations[2]);
	CheckWitnessOnSelectedSurface(&built, &affordance.relations[3]);
	CHECK(memcmp(affordance.relations[2].witness_point.value, actor,
		sizeof(actor)) != 0);
	DestroyFixture(&built);
}

static void TestAlternateVisibleSurfaceCandidate(void)
{
	built_fixture_t built;
	const float source[3] = { -100.0f, 0.0f, 0.0f };
	const float actor[3] = { -80.0f, 0.0f, 0.0f };
	sg_rune_bounds_t bounds = BoundsAt(actor, 8.0f);
	sg_weapon_static_query_t query;
	sg_weapon_static_affordance_t affordance;
	sg_weapon_static_affordance_error_t error;
	sg_weapon_profile_t rocket;
	sg_host_collision_instance_t instance;
	sg_host_collision_scene_t scene;

	CHECK(BuildPreparedFixture(&built, PartialMoverFixture()));
	if (!built.visibility)
	{
		DestroyFixture(&built);
		return;
	}
	rocket = ResolveProfile(&built, SG_WEAPON_PROFILE_ROCKET_LAUNCHER);
	query = Query(&built, source, actor, &bounds,
		SG_WEAPON_STATIC_IMPACT_SURFACE | SG_WEAPON_STATIC_BLAST_REACH);
	memset(&instance, 0, sizeof(instance));
	instance.instance_id = 23U;
	instance.model_index = 1U;
	instance.transform.origin[0] = -50.0f;
	scene.instances = &instance;
	scene.instance_count = 1U;
	CHECK(ResolveAffordance(&built, &scene, &query, &rocket, &affordance,
		&error));
	CHECK((affordance.proven_relations & SG_WEAPON_STATIC_IMPACT_SURFACE) != 0U);
	CHECK((affordance.proven_relations & SG_WEAPON_STATIC_BLAST_REACH) != 0U);
	CheckWitnessOnSelectedSurface(&built, &affordance.relations[3]);
	CHECK(fabsf(affordance.relations[3].witness_point.value[1]) > 0.5f);
	DestroyFixture(&built);
}

static void CheckRejectedTransaction(const built_fixture_t *built,
	const sg_weapon_static_query_t *query, const sg_weapon_profile_t *profile,
	sg_weapon_static_affordance_error_code_t expected)
{
	sg_weapon_static_affordance_t before, output;
	sg_weapon_static_affordance_error_t error;

	memset(&before, 0xa5, sizeof(before));
	output = before;
	CHECK(!ResolveAffordance(built, &empty_scene, query, profile, &output,
		&error));
	if (error.code != expected)
		fprintf(stderr, "rejection error: got %u expected %u\n",
			(unsigned int)error.code, (unsigned int)expected);
	CHECK(error.code == expected);
	CHECK(memcmp(&output, &before, sizeof(output)) == 0);
}

static void CheckBindingRejected(const built_fixture_t *built,
	const sg_weapon_static_query_t *query, const sg_weapon_profile_t *profile,
	const sg_weapon_static_binding_t *binding)
{
	const sg_weapon_static_sources_t sources = {
		.binding = *binding,
		.authority = &built->fixture.authority,
		.configuration = built->configuration,
		.semantics = built->semantics,
		.visibility = built->visibility,
		.model = &built->model,
		.audit = &built->source_audit
	};
	sg_weapon_static_affordance_t before, output;
	sg_weapon_static_affordance_error_t error;

	memset(&before, 0xa5, sizeof(before));
	output = before;
	CHECK(!SG_WeaponStaticAffordanceResolve(&sources, &empty_scene, query,
		profile, &output, &error));
	CHECK(error.code == SG_WEAPON_STATIC_AFFORDANCE_ERROR_IDENTITY_MISMATCH);
	CHECK(memcmp(&output, &before, sizeof(output)) == 0);
}

static void CheckSourcesRejected(const sg_weapon_static_sources_t *sources,
	const sg_weapon_static_query_t *query, const sg_weapon_profile_t *profile,
	sg_weapon_static_affordance_error_code_t expected)
{
	sg_weapon_static_affordance_t before, output;
	sg_weapon_static_affordance_error_t error;

	memset(&before, 0xa5, sizeof(before));
	output = before;
	CHECK(!SG_WeaponStaticAffordanceResolve(sources, &empty_scene, query,
		profile, &output, &error));
	CHECK(error.code == expected);
	CHECK(memcmp(&output, &before, sizeof(output)) == 0);
}

typedef struct source_snapshot_s
{
	const void *source;
	size_t bytes;
	void *copy;
} source_snapshot_t;

static int CaptureSources(source_snapshot_t *snapshots, size_t count)
{
	size_t index;

	for (index = 0U; index < count; index++)
	{
		if (snapshots[index].bytes == 0U)
			continue;
		snapshots[index].copy = malloc(snapshots[index].bytes);
		if (!snapshots[index].copy)
		{
			while (index > 0U)
			{
				index--;
				free(snapshots[index].copy);
				snapshots[index].copy = NULL;
			}
			return 0;
		}
		memcpy(snapshots[index].copy, snapshots[index].source,
			snapshots[index].bytes);
	}
	return 1;
}

static void CheckAndReleaseSources(source_snapshot_t *snapshots, size_t count)
{
	size_t index;

	for (index = 0U; index < count; index++)
	{
		if (snapshots[index].bytes != 0U)
			CHECK(memcmp(snapshots[index].source, snapshots[index].copy,
				snapshots[index].bytes) == 0);
		free(snapshots[index].copy);
	}
}

static void TestIdentityDriftHostileInputsAndImmutability(void)
{
	built_fixture_t built;
	float source[3], target[3];
	sg_rune_bounds_t bounds;
	sg_weapon_static_query_t query, invalid_query, query_before;
	sg_weapon_profile_t profile, invalid_profile, profile_before;
	sg_weapon_static_binding_t invalid_binding;
	sg_weapon_static_affordance_t affordance;
	sg_weapon_static_affordance_error_t error;
	sg_rune_model_identity_t authority_identity_before;
	sg_rune_model_identity_t configuration_identity_before;
	sg_rune_model_identity_t semantics_identity_before;
	sg_rune_model_identity_t visibility_identity_before;
	sg_rune_model_identity_t model_identity_before;
	fixture_t fixture_before;
	sg_configuration_space_t configuration_before;
	sg_configuration_semantics_t semantics_before;
	sg_static_visibility_t visibility_before;
	sg_rune_model_t model_before;
	sg_weapon_static_source_audit_t source_audit_before;
	source_snapshot_t snapshots[20];
	size_t snapshot_count = sizeof(snapshots) / sizeof(snapshots[0]);
	uint32_t saved_surface_count;
	uint32_t model_cell_index;

	CHECK(BuildFixture(&built, 0, 0, 0, 1, 0, 0.0f));
	if (!built.visibility)
	{
		DestroyFixture(&built);
		return;
	}
	SidePoints(0, 0.0f, source, target);
	bounds = BoundsAt(target, 16.0f);
	query = Query(&built, source, target, &bounds,
		SG_WEAPON_STATIC_RELATION_MASK);
	profile = ResolveProfile(&built, SG_WEAPON_PROFILE_PLASMA_REFLECT);
	query_before = query;
	profile_before = profile;
	authority_identity_before = built.fixture.authority.identity;
	configuration_identity_before = built.configuration->identity;
	semantics_identity_before = built.semantics->identity;
	visibility_identity_before = built.visibility->identity;
	model_identity_before = built.model.identity;
	fixture_before = built.fixture;
	configuration_before = *built.configuration;
	semantics_before = *built.semantics;
	visibility_before = *built.visibility;
	model_before = built.model;
	source_audit_before = built.source_audit;
	memset(snapshots, 0, sizeof(snapshots));
	snapshots[0] = (source_snapshot_t){ built.configuration->cells,
		(size_t)built.configuration->cell_count *
			sizeof(*built.configuration->cells), NULL };
	snapshots[1] = (source_snapshot_t){ built.configuration->faces,
		(size_t)built.configuration->face_count *
			sizeof(*built.configuration->faces), NULL };
	snapshots[2] = (source_snapshot_t){ built.configuration->vertices,
		(size_t)built.configuration->vertex_count *
			sizeof(*built.configuration->vertices), NULL };
	snapshots[3] = (source_snapshot_t){ built.configuration->portals,
		(size_t)built.configuration->portal_count *
			sizeof(*built.configuration->portals), NULL };
	snapshots[4] = (source_snapshot_t){ built.configuration->stance_overlaps,
		(size_t)built.configuration->stance_overlap_count *
			sizeof(*built.configuration->stance_overlaps), NULL };
	snapshots[5] = (source_snapshot_t){ built.configuration->certificate_nodes,
		(size_t)built.configuration->certificate_node_count *
			sizeof(*built.configuration->certificate_nodes), NULL };
	snapshots[6] = (source_snapshot_t){ built.semantics->regions,
		(size_t)built.semantics->region_count *
			sizeof(*built.semantics->regions), NULL };
	snapshots[7] = (source_snapshot_t){ built.semantics->faces,
		(size_t)built.semantics->face_count *
			sizeof(*built.semantics->faces), NULL };
	snapshots[8] = (source_snapshot_t){ built.semantics->vertices,
		(size_t)built.semantics->vertex_count *
			sizeof(*built.semantics->vertices), NULL };
	snapshots[9] = (source_snapshot_t){ built.semantics->boundaries,
		(size_t)built.semantics->boundary_count *
			sizeof(*built.semantics->boundaries), NULL };
	snapshots[10] = (source_snapshot_t){ built.semantics->hook_surfaces,
		(size_t)built.semantics->hook_surface_count *
			sizeof(*built.semantics->hook_surfaces), NULL };
	snapshots[11] = (source_snapshot_t){ built.semantics->hook_vertices,
		(size_t)built.semantics->hook_vertex_count *
			sizeof(*built.semantics->hook_vertices), NULL };
	snapshots[12] = (source_snapshot_t){ built.visibility->partitions,
		(size_t)built.visibility->partition_count *
			sizeof(*built.visibility->partitions), NULL };
	snapshots[13] = (source_snapshot_t){ built.visibility->area_components,
		(size_t)built.visibility->area_count *
			sizeof(*built.visibility->area_components), NULL };
	snapshots[14] = (source_snapshot_t){ built.visibility->occluders,
		(size_t)built.visibility->occluder_count *
			sizeof(*built.visibility->occluders), NULL };
	snapshots[15] = (source_snapshot_t){ built.visibility->surfaces,
		(size_t)built.visibility->surface_count *
			sizeof(*built.visibility->surfaces), NULL };
	snapshots[16] = (source_snapshot_t){ built.model.cells,
		(size_t)built.model.cell_count * sizeof(*built.model.cells), NULL };
	snapshots[17] = (source_snapshot_t){ built.model.phases,
		(size_t)built.model.phase_count * sizeof(*built.model.phases), NULL };
	snapshots[18] = (source_snapshot_t){ &built.source_audit,
		sizeof(built.source_audit), NULL };
	snapshots[19] = (source_snapshot_t){ &empty_scene,
		sizeof(empty_scene), NULL };
	if (!CaptureSources(snapshots, snapshot_count))
	{
		fprintf(stderr, "%s:%d: source snapshot allocation failed\n",
			__FILE__, __LINE__);
		failures++;
		DestroyFixture(&built);
		return;
	}
#ifdef SG_STATIC_VISIBILITY_WRAP_ALLOC
	wrapped_query_allocations = 0U;
	count_wrapped_query_allocations = 1;
#endif
	CHECK(ResolveAffordance(&built, &empty_scene, &query, &profile,
		&affordance, &error));
#ifdef SG_STATIC_VISIBILITY_WRAP_ALLOC
	count_wrapped_query_allocations = 0;
	CHECK(wrapped_query_allocations == 0U);
#endif
	CHECK(memcmp(&query, &query_before, sizeof(query)) == 0);
	CHECK(memcmp(&profile, &profile_before, sizeof(profile)) == 0);
	CHECK(memcmp(&built.fixture.authority.identity,
		&authority_identity_before, sizeof(authority_identity_before)) == 0);
	CHECK(memcmp(&built.configuration->identity,
		&configuration_identity_before,
		sizeof(configuration_identity_before)) == 0);
	CHECK(memcmp(&built.semantics->identity, &semantics_identity_before,
		sizeof(semantics_identity_before)) == 0);
	CHECK(memcmp(&built.visibility->identity, &visibility_identity_before,
		sizeof(visibility_identity_before)) == 0);
	CHECK(memcmp(&built.model.identity, &model_identity_before,
		sizeof(model_identity_before)) == 0);
	CHECK(memcmp(&built.fixture, &fixture_before, sizeof(fixture_before)) == 0);
	CHECK(memcmp(built.configuration, &configuration_before,
		sizeof(configuration_before)) == 0);
	CHECK(memcmp(built.semantics, &semantics_before,
		sizeof(semantics_before)) == 0);
	CHECK(memcmp(built.visibility, &visibility_before,
		sizeof(visibility_before)) == 0);
	CHECK(memcmp(&built.model, &model_before, sizeof(model_before)) == 0);
	CHECK(memcmp(&built.source_audit, &source_audit_before,
		sizeof(source_audit_before)) == 0);
	CheckAndReleaseSources(snapshots, snapshot_count);

	invalid_profile = profile;
	invalid_profile.physics_abi_id++;
	CheckRejectedTransaction(&built, &query, &invalid_profile,
		SG_WEAPON_STATIC_AFFORDANCE_ERROR_IDENTITY_MISMATCH);
	invalid_profile = profile;
	invalid_profile.build_identity++;
	CheckRejectedTransaction(&built, &query, &invalid_profile,
		SG_WEAPON_STATIC_AFFORDANCE_ERROR_IDENTITY_MISMATCH);
	invalid_profile = profile;
	invalid_profile.family = SG_WEAPON_FAMILY_COUNT;
	CheckRejectedTransaction(&built, &query, &invalid_profile,
		SG_WEAPON_STATIC_AFFORDANCE_ERROR_INVALID_PROFILE);

	invalid_query = query;
	invalid_query.requested_relations |= UINT32_C(0x80000000);
	CheckRejectedTransaction(&built, &invalid_query, &profile,
		SG_WEAPON_STATIC_AFFORDANCE_ERROR_INVALID_QUERY);
	invalid_query = query;
	invalid_query.exact_live_prefire_trace_required = 0U;
	CheckRejectedTransaction(&built, &invalid_query, &profile,
		SG_WEAPON_STATIC_AFFORDANCE_ERROR_INVALID_QUERY);
	invalid_query = query;
	invalid_query.source_cell = query.target_cell;
	invalid_query.source_cell.value.low++;
	CheckRejectedTransaction(&built, &invalid_query, &profile,
		SG_WEAPON_STATIC_AFFORDANCE_ERROR_INVALID_SOURCE);
	invalid_query = query;
	invalid_query.source_cell = query.target_cell;
	invalid_query.target_cell = query.source_cell;
	CheckRejectedTransaction(&built, &invalid_query, &profile,
		SG_WEAPON_STATIC_AFFORDANCE_ERROR_INVALID_SOURCE);
	invalid_query = query;
	invalid_query.source_phase = query.target_phase;
	invalid_query.target_phase = query.source_phase;
	CheckRejectedTransaction(&built, &invalid_query, &profile,
		SG_WEAPON_STATIC_AFFORDANCE_ERROR_INVALID_SOURCE);
	invalid_query = query;
	invalid_query.source_phase = PhaseRef(query.binding.source_set_identity,
		built.model.phase_count + 7U);
	CheckRejectedTransaction(&built, &invalid_query, &profile,
		SG_WEAPON_STATIC_AFFORDANCE_ERROR_INVALID_SOURCE);
	for (model_cell_index = 0U; model_cell_index < built.model.cell_count;
		model_cell_index++)
		if (SG_RuneModelStableIdEqual(
				&built.model_cells[model_cell_index].id.value,
				&query.source_cell.value))
		{
			sg_rune_phase_span_t saved_span =
				built.model_cells[model_cell_index].phases;

			built.model_cells[model_cell_index].phases.first = UINT32_MAX;
			built.model_cells[model_cell_index].phases.count = UINT32_MAX;
			CheckRejectedTransaction(&built, &query, &profile,
				SG_WEAPON_STATIC_AFFORDANCE_ERROR_INVALID_SOURCE);
			built.model_cells[model_cell_index].phases = saved_span;
			break;
		}
	CHECK(model_cell_index < built.model.cell_count);
	invalid_binding = query.binding;
	invalid_binding.artifact_identity.bytes[0]++;
	CheckBindingRejected(&built, &query, &profile, &invalid_binding);
	invalid_binding = query.binding;
	invalid_binding.bsp_identity.bytes[0]++;
	CheckBindingRejected(&built, &query, &profile, &invalid_binding);
	invalid_binding = query.binding;
	invalid_binding.schema_identity.bytes[0]++;
	CheckBindingRejected(&built, &query, &profile, &invalid_binding);
	invalid_binding = query.binding;
	invalid_binding.source_set_identity++;
	CheckBindingRejected(&built, &query, &profile, &invalid_binding);
	invalid_binding = query.binding;
	invalid_binding.visibility_revision++;
	CheckBindingRejected(&built, &query, &profile, &invalid_binding);

	saved_surface_count = built.visibility->surface_count;
	built.visibility->surface_count = UINT32_MAX;
	CheckRejectedTransaction(&built, &query, &profile,
		SG_WEAPON_STATIC_AFFORDANCE_ERROR_IDENTITY_MISMATCH);
	built.visibility->surface_count = saved_surface_count;
	{
		sg_configuration_semantics_t hostile_semantics = *built.semantics;
		sg_static_visibility_t hostile_visibility = *built.visibility;
		sg_weapon_static_source_audit_t hostile_audit = built.source_audit;
		const sg_weapon_static_sources_t hostile_sources = {
			.binding = query.binding,
			.authority = &built.fixture.authority,
			.configuration = built.configuration,
			.semantics = &hostile_semantics,
			.visibility = &hostile_visibility,
			.model = &built.model,
			.audit = &hostile_audit
		};

		hostile_semantics.hook_surface_count = UINT32_MAX;
		hostile_visibility.surface_count = UINT32_MAX;
		hostile_audit.semantic_surfaces = UINT32_MAX;
		hostile_audit.visibility.reconstructed_surfaces = UINT32_MAX;
		CheckSourcesRejected(&hostile_sources, &query, &profile,
			SG_WEAPON_STATIC_AFFORDANCE_ERROR_IDENTITY_MISMATCH);
	}

	built.semantics->identity.schema_id++;
	CheckRejectedTransaction(&built, &query, &profile,
		SG_WEAPON_STATIC_AFFORDANCE_ERROR_IDENTITY_MISMATCH);
	built.semantics->identity.schema_id--;
	CHECK(!SG_WeaponStaticAffordanceResolve(NULL, &empty_scene, &query,
		&profile, &affordance, &error));
	CHECK(error.code == SG_WEAPON_STATIC_AFFORDANCE_ERROR_INVALID_ARGUMENT);
	DestroyFixture(&built);
}

static void TestErrorStrings(void)
{
	uint32_t code;

	for (code = (uint32_t)SG_WEAPON_STATIC_AFFORDANCE_ERROR_NONE;
		code <= (uint32_t)SG_WEAPON_STATIC_AFFORDANCE_ERROR_VISIBILITY;
		code++)
		CHECK(strcmp(SG_WeaponStaticAffordanceErrorString(
			(sg_weapon_static_affordance_error_code_t)code), "") != 0);
	CHECK(strcmp(SG_WeaponStaticAffordanceErrorString(
		(sg_weapon_static_affordance_error_code_t)UINT32_MAX),
		"unknown static weapon-affordance error") == 0);
}

int main(void)
{
	TestEveryProfileFamilyAndEffect();
	TestOcclusionImpactSplashBounceAndSky();
	TestConditionalMoverAndAreaPortal();
	TestConditionalSurfaceAndSplashRange();
	TestNearbyFloorImpactUsesPolygonPoint();
	TestAlternateVisibleSurfaceCandidate();
	TestIdentityDriftHostileInputsAndImmutability();
	TestErrorStrings();
	if (failures)
	{
		fprintf(stderr, "%d weapon static affordance checks failed\n",
			failures);
		return EXIT_FAILURE;
	}
	puts("weapon static affordance checks passed");
	return EXIT_SUCCESS;
}
