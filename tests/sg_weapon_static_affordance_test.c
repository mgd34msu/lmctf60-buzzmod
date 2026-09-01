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

static sg_weapon_law_input_t WeaponLaw(const built_fixture_t *built)
{
	sg_weapon_law_input_t law = {
		.build_identity = built->fixture.identity.producer_identity,
		.physics_abi_id = built->fixture.identity.physics_abi_id,
		.weapon_balance_compiled = SG_WEAPON_BALANCE_COMPILED,
		.deathmatch_active = 1U
	};

	return law;
}

static sg_rune_phase_ref_t PhaseAtPartition(const built_fixture_t *built,
	uint32_t partition)
{
	sg_rune_phase_ref_t phase = SG_RUNE_PHASE_REF_NONE;
	const sg_static_visibility_partition_t *visibility_partition;
	const sg_configuration_semantic_region_t *region;
	const sg_configuration_cell_t *configuration_cell;
	uint32_t model_index;

	if (partition >= built->visibility->partition_count)
		return phase;
	visibility_partition = &built->visibility->partitions[partition];
	region = &built->semantics->regions[
		visibility_partition->configuration_region];
	configuration_cell = &built->configuration->cells[
		visibility_partition->configuration_cell];
	for (model_index = 0U; model_index < built->model.cell_count; model_index++)
	{
		const sg_rune_cell_t *model_cell = &built->model_cells[model_index];
		uint32_t local;

		if (!SG_RuneModelStableIdEqual(&model_cell->id.value,
				&configuration_cell->id.value))
			continue;
		for (local = 0U; local < model_cell->phases.count; local++)
		{
			const sg_rune_phase_basis_t *candidate =
				&built->model_phases[model_cell->phases.first + local];

			if (FixturePhaseMatchesRegion(candidate, configuration_cell, region))
				return candidate->id;
		}
		break;
	}
	return phase;
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
	*source_phase = PhaseAtPartition(built, visibility.source_partition);
	*target_phase = PhaseAtPartition(built, visibility.destination_partition);
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

static sg_weapon_static_query_t QueryForState(const built_fixture_t *built,
	const float source[3], const float target[3],
	const sg_rune_bounds_t *target_bounds,
	const sg_rune_cell_ref_t *source_cell,
	const sg_rune_phase_ref_t *source_phase,
	const sg_rune_cell_ref_t *target_cell,
	const sg_rune_phase_ref_t *target_phase,
	sg_weapon_static_relation_t requested)
{
	sg_weapon_static_query_input_t input;
	sg_weapon_static_query_t query;
	uint32_t axis;

	memset(&input, 0, sizeof(input));
	input.binding = built->binding;
	input.source_cell = *source_cell;
	input.source_phase = *source_phase;
	input.target_cell = *target_cell;
	input.target_phase = *target_phase;
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

	if ((profile->effects & SG_WEAPON_EFFECT_HITSCAN) != 0U)
		allowed |= SG_WEAPON_STATIC_DIRECT_VISIBILITY;
	if ((profile->effects & SG_WEAPON_EFFECT_PROJECTILE) != 0U)
		allowed |= SG_WEAPON_STATIC_PROJECTILE_CORRIDOR;
	if (profile->supports_occluded_impact != 0U)
		allowed |= SG_WEAPON_STATIC_IMPACT_SURFACE;
	if ((profile->effects & SG_WEAPON_EFFECT_SPLASH) != 0U)
		allowed |= SG_WEAPON_STATIC_BLAST_REACH;
	if ((profile->effects & SG_WEAPON_EFFECT_SECONDARY_AREA) != 0U)
		allowed |= SG_WEAPON_STATIC_SECONDARY_BLAST_REACH;
	if ((profile->effects & SG_WEAPON_EFFECT_BOUNCE) != 0U)
		allowed |= SG_WEAPON_STATIC_BOUNCE_SURFACE;
	if ((profile->effects & SG_WEAPON_EFFECT_PERIODIC_RAY) != 0U)
		allowed |= SG_WEAPON_STATIC_PERIODIC_PROJECTILE_RAY;
	return allowed;
}

static int ResolveAffordance(const built_fixture_t *built,
	const sg_host_collision_scene_t *scene,
	const sg_weapon_static_query_t *query,
	const sg_weapon_profile_t *profile,
	sg_weapon_static_affordance_t *affordance,
	sg_weapon_static_affordance_error_t *error)
{
	sg_weapon_law_input_t law = WeaponLaw(built);

	return SG_WeaponStaticAffordanceResolve(built->context, scene, query, &law,
		profile->id, affordance, error);
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

static fixture_t BfgCornerFixture(void)
{
	fixture_t fixture = PartialMoverFixture();

	fixture.planes[3].distance = 1800.0f;
	fixture.planes[4].distance = 1800.0f;
	fixture.sides[8].plane = 3U;
	SetPlane(&fixture.planes[8], 0.0f, 0.0f, 1.0f, 0.5f);
	fixture.sides[10].plane = 8U;
	fixture.world.plane_count = 9U;
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
	CHECK((affordance.rejected_relations & SG_WEAPON_STATIC_IMPACT_SURFACE) != 0U);
	CHECK(affordance.relations[2].reason ==
		SG_WEAPON_STATIC_REASON_PROFILE_UNSUPPORTED);
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
	CHECK((affordance.rejected_relations & SG_WEAPON_STATIC_IMPACT_SURFACE) != 0U);
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

static void TestBfgOwnerVisibilityAndProjectileOrigin(void)
{
	built_fixture_t built;
	const float source[3] = { -100.0f, 0.0f, 0.0f };
	const float actor[3] = { -60.0f, 0.0f, 0.0f };
	sg_rune_bounds_t bounds = BoundsAt(actor, 8.0f);
	sg_weapon_static_query_t query;
	sg_weapon_static_affordance_t affordance;
	sg_weapon_static_affordance_error_t error;
	sg_weapon_profile_t bfg;
	sg_host_collision_instance_t instance;
	sg_host_collision_scene_t scene;

	CHECK(BuildPreparedFixture(&built, BfgCornerFixture()));
	if (!built.context)
	{
		DestroyFixture(&built);
		return;
	}
	bfg = ResolveProfile(&built, SG_WEAPON_PROFILE_BFG);
	query = Query(&built, source, actor, &bounds,
		SG_WEAPON_STATIC_DIRECT_VISIBILITY |
		SG_WEAPON_STATIC_SECONDARY_BLAST_REACH |
		SG_WEAPON_STATIC_PERIODIC_PROJECTILE_RAY);
	CHECK(ResolveAffordance(&built, &empty_scene, &query, &bfg,
		&affordance, &error));
	CHECK((affordance.proven_relations &
		SG_WEAPON_STATIC_SECONDARY_BLAST_REACH) != 0U);
	CHECK((affordance.rejected_relations &
		SG_WEAPON_STATIC_DIRECT_VISIBILITY) != 0U);
	CHECK((affordance.conditional_relations &
		SG_WEAPON_STATIC_PERIODIC_PROJECTILE_RAY) != 0U);
	CHECK(affordance.relations[6].reason ==
		SG_WEAPON_STATIC_REASON_RUNTIME_PROJECTILE_ORIGIN);
	memset(&instance, 0, sizeof(instance));
	instance.instance_id = 29U;
	instance.model_index = 1U;
	instance.transform.origin[0] = -80.0f;
	scene.instances = &instance;
	scene.instance_count = 1U;
	CHECK(ResolveAffordance(&built, &scene, &query, &bfg, &affordance,
		&error));
	CHECK((affordance.proven_relations &
		SG_WEAPON_STATIC_SECONDARY_BLAST_REACH) == 0U);
	CHECK((affordance.conditional_relations &
		SG_WEAPON_STATIC_SECONDARY_BLAST_REACH) != 0U);
	CHECK(affordance.relations[5].reason ==
		SG_WEAPON_STATIC_REASON_OWNER_DAMAGE_VISIBILITY);
	DestroyFixture(&built);
}

static void TestPreparedBoundaryRejectsUnvalidatedSources(void)
{
	built_fixture_t built;
	sg_weapon_static_prepare_input_t input;
	sg_weapon_static_prepare_error_t error;
	sg_weapon_static_context_t *context = NULL;
	sg_rune_v2_artifact_loader_t second_loader =
		SG_RUNE_V2_ARTIFACT_LOADER_INITIALIZER;
	const sg_rune_v2_artifact_snapshot_t *second_snapshot = NULL;
	sg_rune_v2_artifact_snapshot_t forged_snapshot;
	sg_rune_model_t subset_model;
	sg_rune_validation_evidence_t subset_evidence;
	sg_configuration_semantics_t hostile_semantics;
	sg_configuration_space_t hostile_configuration;
	sg_configuration_cell_t *hostile_cells = NULL;
	sg_static_visibility_publication_t *publication = NULL;
	const sg_host_collision_authority_t *read_authority = NULL;
	const sg_configuration_space_t *read_configuration = NULL;
	const sg_configuration_semantics_t *read_semantics = NULL;
	const sg_static_visibility_t *read_visibility = NULL;
	uint64_t read_revision = 0U;

	CHECK(BuildFixture(&built, 1, 0, 0, 1, 0, 0.0f));
	if (!built.context)
	{
		DestroyFixture(&built);
		return;
	}
	memset(&input, 0, sizeof(input));
	input.artifact.loader = &built.artifact_loader;
	input.artifact.snapshot = built.artifact_snapshot;
	input.visibility_publication = built.visibility_publication;
	forged_snapshot = *built.artifact_snapshot;
	input.artifact.snapshot = &forged_snapshot;
	CHECK(!SG_WeaponStaticContextPrepare(&input, &context, &error));
	CHECK(context == NULL);
	CHECK(error.code == SG_WEAPON_STATIC_PREPARE_ERROR_SOURCE_MISMATCH);
	CHECK(LoadFixtureModel(&built, &built.model, &built.model_evidence,
		&second_loader, &second_snapshot));
	input.artifact.loader = &built.artifact_loader;
	input.artifact.snapshot = second_snapshot;
	CHECK(!SG_WeaponStaticContextPrepare(&input, &context, &error));
	CHECK(context == NULL);
	CHECK(error.code == SG_WEAPON_STATIC_PREPARE_ERROR_SOURCE_MISMATCH);
	input.artifact.loader = &second_loader;
	input.artifact.snapshot = second_snapshot;
	CHECK(SG_WeaponStaticContextPrepare(&input, &context, &error));
	SG_WeaponStaticContextDestroy(context);
	context = NULL;
	SG_RuneV2ArtifactLoaderDestroy(&second_loader);
	second_snapshot = NULL;
	hostile_semantics = *built.semantics;
	hostile_semantics.hook_surface_count = UINT32_MAX;
	CHECK(!SG_StaticVisibilityPublicationIssue(&built.fixture.authority,
		built.configuration, &hostile_semantics, built.visibility,
		built.binding.visibility_revision, &publication));
	CHECK(publication == NULL);
	CHECK(!SG_StaticVisibilityPublicationIssue(&built.fixture.authority,
		built.configuration, built.semantics, built.visibility, 0U,
		&publication));
	CHECK(publication == NULL);
	CHECK(SG_StaticVisibilityPublicationIssue(&built.fixture.authority,
		built.configuration, built.semantics, built.visibility,
		built.binding.visibility_revision + 1U, &publication));
	input.artifact.loader = &built.artifact_loader;
	input.artifact.snapshot = built.artifact_snapshot;
	input.visibility_publication = publication;
	CHECK(!SG_WeaponStaticContextPrepare(&input, &context, &error));
	CHECK(context == NULL);
	CHECK(error.code == SG_WEAPON_STATIC_PREPARE_ERROR_SOURCE_MISMATCH);
	SG_StaticVisibilityPublicationDestroy(publication);
	publication = NULL;
	publication = built.visibility_publication;
	CHECK(!SG_StaticVisibilityPublicationIssue(&built.fixture.authority,
		built.configuration, built.semantics, built.visibility, 0U,
		&publication));
	CHECK(publication == built.visibility_publication);
	CHECK(!SG_StaticVisibilityPublicationRead(built.visibility_publication,
		&read_authority, NULL, &read_semantics, &read_visibility,
		&read_revision));
	CHECK(read_authority == NULL);
	CHECK(read_configuration == NULL);
	CHECK(read_semantics == NULL);
	CHECK(read_visibility == NULL);
	CHECK(read_revision == 0U);
	publication = NULL;
	hostile_configuration = *built.configuration;
	hostile_cells = malloc((size_t)hostile_configuration.cell_count *
		sizeof(*hostile_cells));
	CHECK(hostile_cells != NULL);
	if (hostile_cells && hostile_configuration.cell_count > 1U)
	{
		sg_configuration_cell_t swap;

		memcpy(hostile_cells, hostile_configuration.cells,
			(size_t)hostile_configuration.cell_count *
				sizeof(*hostile_cells));
		hostile_configuration.cells = hostile_cells;
		hostile_cells[1].id = hostile_cells[0].id;
		CHECK(SG_StaticVisibilityPublicationIssue(&built.fixture.authority,
			&hostile_configuration, built.semantics, built.visibility,
			built.binding.visibility_revision, &publication));
		input.artifact.loader = &built.artifact_loader;
		input.artifact.snapshot = built.artifact_snapshot;
		input.visibility_publication = publication;
		CHECK(!SG_WeaponStaticContextPrepare(&input, &context, &error));
		CHECK(context == NULL);
		CHECK(error.code == SG_WEAPON_STATIC_PREPARE_ERROR_SOURCE_MISMATCH);
		SG_StaticVisibilityPublicationDestroy(publication);
		publication = NULL;
		memcpy(hostile_cells, built.configuration->cells,
			(size_t)hostile_configuration.cell_count *
				sizeof(*hostile_cells));
		swap = hostile_cells[0];
		hostile_cells[0] = hostile_cells[1];
		hostile_cells[1] = swap;
		CHECK(!SG_StaticVisibilityPublicationIssue(&built.fixture.authority,
			&hostile_configuration, built.semantics, built.visibility,
			built.binding.visibility_revision, &publication));
		CHECK(publication == NULL);
	}
	free(hostile_cells);
	input.visibility_publication = built.visibility_publication;
	if (built.model.cell_count > 1U)
	{
		subset_model = built.model;
		subset_evidence = built.model_evidence;
		subset_model.cell_count--;
		subset_model.completeness.expected_cells = subset_model.cell_count;
		subset_model.completeness.covered_cells = subset_model.cell_count;
		subset_evidence.proved_cells = subset_model.cell_count;
		CHECK(SG_RuneModelValidate(&subset_model, &subset_evidence) ==
			SG_RUNE_FAILURE_NONE);
		CHECK(LoadFixtureModel(&built, &subset_model, &subset_evidence,
			&second_loader, &second_snapshot));
		input.artifact.loader = &second_loader;
		input.artifact.snapshot = second_snapshot;
		CHECK(!SG_WeaponStaticContextPrepare(&input, &context, &error));
		CHECK(context == NULL);
		CHECK(error.code ==
			SG_WEAPON_STATIC_PREPARE_ERROR_SOURCE_MISMATCH);
		SG_RuneV2ArtifactLoaderDestroy(&second_loader);
	}
	DestroyFixture(&built);
}

static uint32_t CeilLog2(uint32_t value)
{
	uint32_t result = 0U;
	uint32_t power = 1U;

	while (power < value)
	{
		power *= 2U;
		result++;
	}
	return result;
}

typedef struct rejected_surface_ref_s
{
	uint32_t surface;
	sg_rune_bounds_t bounds;
} rejected_surface_ref_t;

static int RejectedSurfaceLess(const rejected_surface_ref_t *left,
	const rejected_surface_ref_t *right, uint32_t axis, uint64_t *work)
{
	double left_center = (double)left->bounds.mins.value[axis] +
		left->bounds.maxs.value[axis];
	double right_center = (double)right->bounds.mins.value[axis] +
		right->bounds.maxs.value[axis];

	(*work)++;
	if (left_center != right_center)
		return left_center < right_center;
	return left->surface < right->surface;
}

static void RejectedSurfaceSift(rejected_surface_ref_t *items,
	uint32_t count, uint32_t root, uint32_t axis, uint64_t *work)
{
	for (;;)
	{
		uint32_t child = root * 2U + 1U;
		uint32_t selected = root;
		rejected_surface_ref_t swap;

		if (child < count && RejectedSurfaceLess(&items[selected],
			&items[child], axis, work))
			selected = child;
		if (child + 1U < count && RejectedSurfaceLess(&items[selected],
			&items[child + 1U], axis, work))
			selected = child + 1U;
		if (selected == root)
			return;
		swap = items[root];
		items[root] = items[selected];
		items[selected] = swap;
		root = selected;
	}
}

static void RejectedSurfaceSort(rejected_surface_ref_t *items,
	uint32_t count, uint32_t axis, uint64_t *work)
{
	uint32_t index;

	for (index = count / 2U; index > 0U; index--)
		RejectedSurfaceSift(items, count, index - 1U, axis, work);
	for (index = count; index > 1U; index--)
	{
		rejected_surface_ref_t swap = items[0];

		items[0] = items[index - 1U];
		items[index - 1U] = swap;
		RejectedSurfaceSift(items, index - 1U, 0U, axis, work);
	}
}

static void TestBoundsInclude(sg_rune_bounds_t *bounds,
	const sg_rune_bounds_t *other)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
	{
		if (other->mins.value[axis] < bounds->mins.value[axis])
			bounds->mins.value[axis] = other->mins.value[axis];
		if (other->maxs.value[axis] > bounds->maxs.value[axis])
			bounds->maxs.value[axis] = other->maxs.value[axis];
	}
}

static void CountRejectedRecursiveSurfaceBuild(rejected_surface_ref_t *refs,
	uint32_t first, uint32_t count, uint64_t *work)
{
	sg_rune_bounds_t bounds = refs[first].bounds;
	uint32_t index, axis = 0U;
	float longest;

	*work += count;
	for (index = 1U; index < count; index++)
		TestBoundsInclude(&bounds, &refs[first + index].bounds);
	if (count == 1U)
		return;
	longest = bounds.maxs.value[0] - bounds.mins.value[0];
	for (index = 1U; index < 3U; index++)
	{
		float extent = bounds.maxs.value[index] - bounds.mins.value[index];

		if (extent > longest)
		{
			longest = extent;
			axis = index;
		}
	}
	RejectedSurfaceSort(&refs[first], count, axis, work);
	index = count / 2U;
	CountRejectedRecursiveSurfaceBuild(refs, first, index, work);
	CountRejectedRecursiveSurfaceBuild(refs, first + index, count - index,
		work);
}

static uint64_t RejectedRecursiveSurfacePreparationWork(
	const built_fixture_t *built)
{
	rejected_surface_ref_t *refs;
	uint64_t work;
	uint32_t index;

	refs = malloc((size_t)built->visibility->surface_count * sizeof(*refs));
	CHECK(refs != NULL);
	if (!refs)
		return 0U;
	work = built->visibility->surface_count;
	for (index = 0U; index < built->visibility->surface_count; index++)
	{
		uint32_t semantic_surface = built->visibility->surfaces[
			index].semantic_surface;

		refs[index].surface = index;
		refs[index].bounds = built->semantics->hook_surfaces[
			semantic_surface].bounds;
	}
	CountRejectedRecursiveSurfaceBuild(refs, 0U,
		built->visibility->surface_count, &work);
	free(refs);
	return work;
}

static void TestNoPvsAndSolidCellCoverage(void)
{
	built_fixture_t built;
	uint32_t configuration_cell, no_cluster = 0U, solid = 0U;

	CHECK(BuildFixture(&built, 1, 0, 0, 1, 0, 0.0f));
	if (!built.context)
	{
		DestroyFixture(&built);
		return;
	}
	CHECK(built.model.cell_count == built.configuration->cell_count);
	for (configuration_cell = 0U;
		configuration_cell < built.configuration->cell_count;
		configuration_cell++)
	{
		const sg_configuration_cell_t *configuration =
			&built.configuration->cells[configuration_cell];
		uint32_t model_cell;

		if (configuration->bsp_cluster.index == UINT32_MAX)
			no_cluster++;
		if ((configuration->contents & SG_RUNE_CONTENTS_SOLID) != 0U)
			solid++;
		for (model_cell = 0U; model_cell < built.model.cell_count;
			model_cell++)
			if (SG_RuneModelStableIdEqual(&configuration->id.value,
				&built.model.cells[model_cell].id.value))
				break;
		CHECK(model_cell < built.model.cell_count);
		if (model_cell < built.model.cell_count)
		{
			CHECK(built.model.cells[model_cell].bsp_cluster.index ==
				configuration->bsp_cluster.index);
			CHECK(built.model.cells[model_cell].contents ==
				configuration->contents);
		}
	}
	CHECK(no_cluster > 0U);
	CHECK(solid > 0U);
	DestroyFixture(&built);
}

static void TestCellBindingScalingAndFullCoverage(void)
{
	built_fixture_t built;
	uint32_t small_configuration_cells, small_model_cells;
	uint64_t small_comparisons, large_comparisons, large_bound;
	uint64_t rejected_linear_scan_comparisons;
	uint32_t large_configuration_cells, large_model_cells, logarithm;

	CHECK(BuildPreparedFixture(&built, CellScalingFixture(4U)));
	if (!built.context)
	{
		DestroyFixture(&built);
		return;
	}
	small_configuration_cells = built.configuration->cell_count;
	small_model_cells = built.model.cell_count;
	small_comparisons =
		SG_WeaponStaticContextBindingComparisons(built.context);
	CHECK(small_comparisons > 0U);
	DestroyFixture(&built);

	CHECK(BuildPreparedFixture(&built,
		CellScalingFixture(SG_WEAPON_FIXTURE_CELL_SPLITS)));
	if (!built.context)
	{
		DestroyFixture(&built);
		return;
	}
	large_configuration_cells = built.configuration->cell_count;
	large_model_cells = built.model.cell_count;
	large_comparisons =
		SG_WeaponStaticContextBindingComparisons(built.context);
	CHECK(large_configuration_cells > small_configuration_cells * 4U);
	CHECK(large_model_cells > small_model_cells * 4U);
	CHECK(large_comparisons > small_comparisons);
	logarithm = CeilLog2(large_configuration_cells);
	large_bound = (uint64_t)large_configuration_cells *
		(4U * logarithm + 4U);
	/* The rejected nested scan visits 1 + ... + n entries when both audited
	 * sources have their canonical stable-ID order. */
	rejected_linear_scan_comparisons =
		(uint64_t)large_configuration_cells *
		(large_configuration_cells + 1U) / 2U;
	CHECK(rejected_linear_scan_comparisons > large_bound);
	CHECK(large_comparisons <= large_bound);
	CHECK(large_comparisons * small_configuration_cells *
		small_configuration_cells < small_comparisons *
		large_configuration_cells * large_configuration_cells);
	DestroyFixture(&built);
}

static void TestIndexedScaling(void)
{
	built_fixture_t built;
	const float source[3] = { -100.0f, 0.0f, 0.0f };
	const float actor[3] = { -80.0f, 0.0f, 0.0f };
	sg_rune_bounds_t bounds = BoundsAt(actor, 8.0f);
	sg_weapon_static_query_t query;
	sg_weapon_static_affordance_t affordance;
	sg_weapon_static_affordance_error_t error;
	sg_weapon_profile_t rocket;
	uint32_t small_surfaces, small_nodes, small_candidates, small_points;
	uint32_t small_faces;

	CHECK(BuildPreparedFixture(&built, ScalingFixture(4U)));
	if (!built.context)
	{
		DestroyFixture(&built);
		return;
	}
	rocket = ResolveProfile(&built, SG_WEAPON_PROFILE_ROCKET_LAUNCHER);
	query = Query(&built, source, actor, &bounds,
		SG_WEAPON_STATIC_IMPACT_SURFACE | SG_WEAPON_STATIC_BLAST_REACH);
	CHECK(ResolveAffordance(&built, &empty_scene, &query, &rocket,
		&affordance, &error));
	small_surfaces = built.visibility->surface_count;
	small_nodes = affordance.spatial_nodes_visited;
	small_candidates = affordance.candidate_surfaces_visited;
	small_points = affordance.candidate_points_queried;
	small_faces = affordance.pose_partition_faces_tested;
	DestroyFixture(&built);

	CHECK(BuildPreparedFixture(&built,
		ScalingFixture(SG_WEAPON_FIXTURE_EXTRA_MODELS)));
	if (!built.context)
	{
		DestroyFixture(&built);
		return;
	}
	rocket = ResolveProfile(&built, SG_WEAPON_PROFILE_ROCKET_LAUNCHER);
	query = Query(&built, source, actor, &bounds,
		SG_WEAPON_STATIC_IMPACT_SURFACE | SG_WEAPON_STATIC_BLAST_REACH);
	CHECK(ResolveAffordance(&built, &empty_scene, &query, &rocket,
		&affordance, &error));
	CHECK(built.visibility->surface_count > small_surfaces * 4U);
	CHECK(affordance.candidate_surfaces_visited == small_candidates);
	CHECK(affordance.candidate_points_queried == small_points);
	CHECK(affordance.pose_partition_faces_tested == small_faces);
	CHECK(affordance.spatial_nodes_visited < built.visibility->surface_count);
	CHECK(affordance.spatial_nodes_visited < small_nodes + 32U);
	DestroyFixture(&built);
}

static void TestSurfacePreparationScaling(void)
{
	built_fixture_t built;
	uint32_t surface_count, logarithm;
	uint64_t measured_work, accepted_bound, rejected_work;

	CHECK(BuildPreparedFixture(&built,
		ScalingFixture(SG_WEAPON_FIXTURE_EXTRA_MODELS)));
	if (!built.context)
	{
		DestroyFixture(&built);
		return;
	}
	surface_count = built.visibility->surface_count;
	measured_work = SG_WeaponStaticContextSurfacePreparationWork(
		built.context);
	logarithm = CeilLog2(surface_count);
	accepted_bound = (uint64_t)surface_count *
		(5U * logarithm + 8U);
	rejected_work = RejectedRecursiveSurfacePreparationWork(&built);
	CHECK(surface_count >= 384U);
	CHECK(measured_work > surface_count);
	CHECK(measured_work <= accepted_bound);
	/* This is the measured work of the rejected per-node heapsort over the
	 * exact same audited surface bounds, not an asymptotic estimate. */
	CHECK(rejected_work > accepted_bound);
	CHECK(rejected_work > measured_work);
	DestroyFixture(&built);
}

static int TestPointInsideBounds(const sg_rune_bounds_t *bounds,
	const float point[3])
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		if (point[axis] < bounds->mins.value[axis] - 0.125f ||
			point[axis] > bounds->maxs.value[axis] + 0.125f)
			return 0;
	return 1;
}

static void TestPartitionPointIndexScaling(void)
{
	built_fixture_t built;
	sg_static_visibility_result_t visibility;
	sg_static_visibility_error_t visibility_error;
	sg_weapon_static_query_t query;
	sg_weapon_static_affordance_t affordance;
	sg_weapon_static_affordance_error_t error;
	sg_weapon_profile_t machinegun;
	sg_rune_bounds_t target_bounds;
	float point[3], last_point[3], overlap_point[3];
	uint32_t last = SG_STATIC_VISIBILITY_INDEX_NONE;
	uint32_t partition, cell, selected_cell = 0U, cell_partitions = 0U;
	uint32_t ordinal = 0U, last_ordinal = 0U;
	uint32_t leaf_bounds_overlaps = 0U, candidate_faces = 0U;
	uint32_t all_faces = 0U;
	uint64_t preparation_bound;

	CHECK(BuildPreparedFixture(&built,
		PartitionScalingFixture(SG_WEAPON_FIXTURE_PARTITIONS)));
	if (!built.context)
	{
		DestroyFixture(&built);
		return;
	}
	CHECK(built.visibility->partition_count >= 8U);
	for (cell = 0U; cell < built.configuration->cell_count; cell++)
	{
		uint32_t count = 0U;

		for (partition = 0U;
			partition < built.visibility->partition_count; partition++)
			if (built.visibility->partitions[
				partition].configuration_cell == cell)
				count++;
		if (count > cell_partitions)
		{
			cell_partitions = count;
			selected_cell = cell;
		}
	}
	CHECK(cell_partitions >= 8U);
	for (partition = 0U; partition < built.visibility->partition_count;
		partition++)
	{
		const sg_configuration_semantic_region_t *region;
		uint32_t overlaps = 0U, overlap_faces = 0U, other;

		if (built.visibility->partitions[partition].configuration_cell !=
			selected_cell)
			continue;
		ordinal++;
		region = &built.semantics->regions[built.visibility->partitions[
			partition].configuration_region];
		all_faces += region->face_count;
		memcpy(point, region->interior_witness.value, sizeof(point));
		if (SG_StaticVisibilityQueryPoints(&built.fixture.authority,
				&empty_scene, built.configuration, built.semantics,
				built.visibility, point, point, &visibility,
				&visibility_error) &&
			visibility.source_partition == partition &&
			visibility.destination_partition == partition)
		{
			last = partition;
			last_ordinal = ordinal;
			memcpy(last_point, point, sizeof(last_point));
			for (other = 0U; other < built.visibility->partition_count;
				other++)
			{
				const sg_configuration_semantic_region_t *other_region;

				if (built.visibility->partitions[
					other].configuration_cell != selected_cell)
					continue;
				other_region = &built.semantics->regions[
					built.visibility->partitions[
						other].configuration_region];
				if (TestPointInsideBounds(&other_region->bounds, point))
				{
					overlaps++;
					overlap_faces += other_region->face_count;
				}
			}
			if (overlaps > leaf_bounds_overlaps)
			{
				leaf_bounds_overlaps = overlaps;
				candidate_faces = overlap_faces;
				memcpy(overlap_point, point, sizeof(overlap_point));
			}
		}
	}
	CHECK(last != SG_STATIC_VISIBILITY_INDEX_NONE);
	if (last == SG_STATIC_VISIBILITY_INDEX_NONE)
	{
		DestroyFixture(&built);
		return;
	}
	CHECK(leaf_bounds_overlaps > 1U);
	CHECK(leaf_bounds_overlaps < cell_partitions);
	CHECK(last_ordinal == cell_partitions);
	if (leaf_bounds_overlaps <= 1U)
	{
		DestroyFixture(&built);
		return;
	}
	machinegun = ResolveProfile(&built, SG_WEAPON_PROFILE_MACHINEGUN);
	target_bounds = BoundsAt(last_point, 1.0f);
	query = Query(&built, last_point, last_point, &target_bounds,
		SG_WEAPON_STATIC_DIRECT_VISIBILITY);
	CHECK(ResolveAffordance(&built, &empty_scene, &query, &machinegun,
		&affordance, &error));
	CHECK(affordance.pose_partition_faces_tested < 2U * all_faces);
	target_bounds = BoundsAt(overlap_point, 1.0f);
	query = Query(&built, overlap_point, overlap_point, &target_bounds,
		SG_WEAPON_STATIC_DIRECT_VISIBILITY);
	CHECK(ResolveAffordance(&built, &empty_scene, &query, &machinegun,
		&affordance, &error));
	/* Two poses are located. Every overlapping internal node can expose at
	 * most two visited children; leaf face work is paid only for overlapping
	 * candidate bounds. */
	CHECK(affordance.pose_partition_nodes_visited <=
		2U * affordance.pose_partition_bounds_overlaps + 2U);
	CHECK(affordance.pose_partition_faces_tested <= 2U * candidate_faces);
	preparation_bound = (uint64_t)built.visibility->partition_count *
		(5U * CeilLog2(built.visibility->partition_count) + 8U);
	CHECK(SG_WeaponStaticContextPartitionPreparationWork(built.context) <=
		preparation_bound);
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

static uint32_t ModelPhaseIndex(const sg_rune_model_t *model,
	const sg_rune_phase_ref_t *reference)
{
	uint32_t index;

	for (index = 0U; index < model->phase_count; index++)
		if (SG_RuneModelStableIdEqual(&model->phases[index].id.value,
				&reference->value))
			return index;
	return UINT32_MAX;
}

static void SetTestMechanism(sg_rune_mechanism_t *mechanism,
	const built_fixture_t *built, const sg_rune_cell_ref_t *entry_cell)
{
	sg_rune_order_key_t order;
	uint32_t cell;

	memset(mechanism, 0, sizeof(*mechanism));
	memset(&order, 0, sizeof(order));
	order.source_set_identity = built->model.identity.source_set_identity;
	order.domain = SG_RUNE_ORDER_MECHANISM;
	mechanism->order = order;
	mechanism->id.value = SG_RuneModelStableIdFromOrderKey(&order);
	mechanism->kind = SG_RUNE_MECHANISM_DOOR;
	mechanism->entry_cell = *entry_cell;
	mechanism->exit_cell = SG_RUNE_CELL_REF_NONE;
	for (cell = 0U; cell < built->model.cell_count; cell++)
		if (!SG_RuneModelStableIdEqual(&built->model.cells[cell].id.value,
				&entry_cell->value))
		{
			mechanism->exit_cell = built->model.cells[cell].id;
			break;
		}
	mechanism->activation_landmark = SG_RUNE_LANDMARK_REF_NONE;
	mechanism->entity.index = 0U;
	mechanism->entity.spawn_ordinal = 0U;
}

static void CheckAlteredPhaseRejected(const built_fixture_t *built,
	const sg_weapon_static_query_t *query, sg_rune_model_t *model)
{
	sg_rune_v2_artifact_loader_t loader =
		SG_RUNE_V2_ARTIFACT_LOADER_INITIALIZER;
	const sg_rune_v2_artifact_snapshot_t *snapshot = NULL;
	sg_weapon_static_prepare_input_t prepare;
	sg_weapon_static_prepare_error_t prepare_error;
	sg_weapon_static_context_t *context = NULL;
	sg_weapon_static_affordance_t before, output;
	sg_weapon_static_affordance_error_t error;
	sg_weapon_law_input_t law = WeaponLaw(built);

	CHECK(SG_RuneModelValidate(model, &built->model_evidence) ==
		SG_RUNE_FAILURE_NONE);
	CHECK(LoadFixtureModel(built, model, &built->model_evidence,
		&loader, &snapshot));
	if (!snapshot)
	{
		SG_RuneV2ArtifactLoaderDestroy(&loader);
		return;
	}
	memset(&prepare, 0, sizeof(prepare));
	prepare.artifact.loader = &loader;
	prepare.artifact.snapshot = snapshot;
	prepare.visibility_publication = built->visibility_publication;
	CHECK(SG_WeaponStaticContextPrepare(&prepare, &context, &prepare_error));
	if (context)
	{
		memset(&before, 0xa5, sizeof(before));
		output = before;
		CHECK(!SG_WeaponStaticAffordanceResolve(context, &empty_scene, query,
			&law, SG_WEAPON_PROFILE_MACHINEGUN, &output, &error));
		CHECK(error.code ==
			SG_WEAPON_STATIC_AFFORDANCE_ERROR_INVALID_SOURCE);
		CHECK(memcmp(&output, &before, sizeof(output)) == 0);
	}
	SG_WeaponStaticContextDestroy(context);
	SG_RuneV2ArtifactLoaderDestroy(&loader);
}

static void TestPosePhaseSemanticBinding(void)
{
	built_fixture_t built;
	float point[3];
	sg_rune_bounds_t bounds;
	sg_weapon_static_query_t query;
	sg_weapon_static_affordance_t affordance;
	sg_weapon_static_affordance_error_t error;
	sg_weapon_profile_t machinegun;
	sg_rune_phase_basis_t *phases = NULL;
	sg_rune_model_t model;
	sg_rune_mechanism_t mechanism;
	uint32_t partition, phase_index, mismatch;
	int found = 0;

	CHECK(BuildFixture(&built, 0, 0, 0, 1, 0, 0.0f));
	if (!built.context)
	{
		DestroyFixture(&built);
		return;
	}
	for (partition = 0U; partition < built.visibility->partition_count;
		partition++)
	{
		const sg_static_visibility_partition_t *candidate =
			&built.visibility->partitions[partition];

		if (built.configuration->cells[
				candidate->configuration_cell].stance !=
			SG_RUNE_STANCE_CROUCHING)
			continue;
		memcpy(point, built.semantics->regions[
			candidate->configuration_region].interior_witness.value,
			sizeof(point));
		found = 1;
		break;
	}
	CHECK(found);
	if (!found)
	{
		DestroyFixture(&built);
		return;
	}
	bounds = BoundsAt(point, 1.0f);
	query = Query(&built, point, point, &bounds,
		SG_WEAPON_STATIC_DIRECT_VISIBILITY);
	machinegun = ResolveProfile(&built, SG_WEAPON_PROFILE_MACHINEGUN);
	CHECK(ResolveAffordance(&built, &empty_scene, &query, &machinegun,
		&affordance, &error));
	CHECK((affordance.proven_relations &
		SG_WEAPON_STATIC_DIRECT_VISIBILITY) != 0U);
	phase_index = ModelPhaseIndex(&built.model, &query.source_phase);
	CHECK(phase_index != UINT32_MAX);
	phases = malloc((size_t)built.model.phase_count * sizeof(*phases));
	CHECK(phases != NULL);
	if (!phases || phase_index == UINT32_MAX)
	{
		free(phases);
		DestroyFixture(&built);
		return;
	}
	for (mismatch = 0U; mismatch < 5U; mismatch++)
	{
		sg_rune_phase_basis_t *phase;

		memcpy(phases, built.model.phases,
			(size_t)built.model.phase_count * sizeof(*phases));
		model = built.model;
		model.phases = phases;
		phase = &phases[phase_index];
		switch (mismatch)
		{
		case 0U:
			phase->stance = phase->stance == SG_RUNE_STANCE_STANDING ?
				SG_RUNE_STANCE_CROUCHING : SG_RUNE_STANCE_STANDING;
			break;
		case 1U:
			/* Motion and support are structurally coupled. */
			if (phase->motion == SG_RUNE_MOTION_SUPPORTED)
			{
				phase->motion = SG_RUNE_MOTION_AIRBORNE;
				phase->support = SG_RUNE_SUPPORT_NONE;
			}
			else
			{
				phase->motion = SG_RUNE_MOTION_SUPPORTED;
				phase->support = SG_RUNE_SUPPORT_SUPPORTED;
			}
			break;
		case 2U:
			phase->medium = phase->medium == SG_RUNE_MEDIUM_WATER ?
				SG_RUNE_MEDIUM_LAVA : SG_RUNE_MEDIUM_WATER;
			break;
		case 3U:
			phase->void_relation =
				phase->void_relation == SG_RUNE_VOID_CLEAR ?
				SG_RUNE_VOID_ADJACENT : SG_RUNE_VOID_CLEAR;
			break;
		case 4U:
			/* Mover support, frame, and ownership are contract-coupled. */
			SetTestMechanism(&mechanism, &built, &query.source_cell);
			model.mechanisms = &mechanism;
			model.mechanism_count = 1U;
			phase->motion = SG_RUNE_MOTION_SUPPORTED;
			phase->support = SG_RUNE_SUPPORT_MOVER;
			phase->reference_frame = SG_RUNE_FRAME_MOVER_RELATIVE;
			phase->mover.value = mechanism.id.value;
			break;
		default:
			break;
		}
		CheckAlteredPhaseRejected(&built, &query, &model);
	}
	free(phases);
	DestroyFixture(&built);
}

static void TestSemanticPartitionTieBreakIsUnique(void)
{
	built_fixture_t built;
	uint32_t partition, maximum_compatible = 0U;

	CHECK(BuildPreparedFixture(&built,
		PartitionScalingFixture(SG_WEAPON_FIXTURE_PARTITIONS)));
	if (!built.context)
	{
		DestroyFixture(&built);
		return;
	}
	for (partition = 0U;
		partition < built.visibility->partition_count; partition++)
	{
		const sg_static_visibility_partition_t *owner =
			&built.visibility->partitions[partition];
		const sg_configuration_semantic_region_t *owner_region =
			&built.semantics->regions[owner->configuration_region];
		sg_rune_phase_ref_t owner_phase = PhaseAtPartition(&built, partition);
		uint32_t owner_phase_index = ModelPhaseIndex(&built.model, &owner_phase);
		uint32_t face_local;

		if (owner_phase_index == UINT32_MAX)
			continue;

		for (face_local = 0U;
			face_local < owner_region->face_count; face_local++)
		{
			const sg_configuration_semantic_face_t *face =
				&built.semantics->faces[
					owner_region->first_face + face_local];
			uint32_t vertex_local;

			for (vertex_local = 0U;
				vertex_local < face->vertex_count; vertex_local++)
			{
				const sg_rune_vec3_t *vertex = &built.semantics->vertices[
					face->first_vertex + vertex_local];
				uint32_t candidate;

				uint32_t compatible = 0U;
				for (candidate = 0U;
					candidate < built.visibility->partition_count; candidate++)
				{
					const sg_static_visibility_partition_t *other =
						&built.visibility->partitions[candidate];
					const sg_configuration_semantic_region_t *other_region;
					uint32_t tested = 0U;

					if (other->configuration_cell !=
							owner->configuration_cell)
						continue;
					other_region = &built.semantics->regions[
						other->configuration_region];
					if (!FixturePhaseMatchesRegion(
							&built.model.phases[owner_phase_index],
							&built.configuration->cells[
								owner->configuration_cell], other_region))
						continue;
					if (SG_StaticVisibilityPointInPartition(built.semantics,
							built.visibility, candidate, vertex->value, &tested))
						compatible++;
				}
				if (compatible > maximum_compatible)
					maximum_compatible = compatible;
			}
		}
	}
	/* Host BSP ties choose one strict leaf, so boundary samples stay unique. */
	CHECK(maximum_compatible == 1U);
	DestroyFixture(&built);
}

static void TestWaterLevelMotionBinding(void)
{
	built_fixture_t built;
	uint32_t shallow = UINT32_MAX, submerged = UINT32_MAX;
	uint32_t supported_shallow = UINT32_MAX, dry_airborne = UINT32_MAX;
	uint32_t partition;
	uint32_t cases[4];
	uint32_t case_index;

	CHECK(BuildPreparedFixture(&built, WaterLevelFixture()));
	if (!built.context)
	{
		DestroyFixture(&built);
		return;
	}
	for (partition = 0U; partition < built.visibility->partition_count;
		partition++)
	{
		const sg_configuration_semantic_region_t *region =
			&built.semantics->regions[built.visibility->partitions[
				partition].configuration_region];

		if ((region->flags & SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE) != 0U &&
			(region->flags & SG_CONFIGURATION_SEMANTIC_REGION_WATER) != 0U &&
			region->water_level == 1U && shallow == UINT32_MAX)
			shallow = partition;
		if ((region->flags & SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE) != 0U &&
			(region->flags & SG_CONFIGURATION_SEMANTIC_REGION_WATER) != 0U &&
			region->water_level >= 2U && submerged == UINT32_MAX)
			submerged = partition;
		if ((region->flags & SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED) != 0U &&
			(region->flags & SG_CONFIGURATION_SEMANTIC_REGION_WATER) != 0U &&
			region->water_level == 1U && supported_shallow == UINT32_MAX)
			supported_shallow = partition;
		if ((region->flags & SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE) != 0U &&
			(region->flags & (SG_CONFIGURATION_SEMANTIC_REGION_WATER |
				SG_CONFIGURATION_SEMANTIC_REGION_LAVA |
				SG_CONFIGURATION_SEMANTIC_REGION_SLIME)) == 0U &&
			region->water_level == 0U && dry_airborne == UINT32_MAX)
			dry_airborne = partition;
	}
	CHECK(shallow != UINT32_MAX);
	CHECK(submerged != UINT32_MAX);
	CHECK(supported_shallow != UINT32_MAX);
	CHECK(dry_airborne != UINT32_MAX);
	if (shallow == UINT32_MAX || submerged == UINT32_MAX ||
		supported_shallow == UINT32_MAX || dry_airborne == UINT32_MAX)
	{
		DestroyFixture(&built);
		return;
	}
	cases[0] = shallow;
	cases[1] = submerged;
	cases[2] = supported_shallow;
	cases[3] = dry_airborne;
	for (case_index = 0U; case_index < 4U; case_index++)
	{
		const sg_static_visibility_partition_t *visibility_partition =
			&built.visibility->partitions[cases[case_index]];
		const sg_configuration_semantic_region_t *region =
			&built.semantics->regions[
				visibility_partition->configuration_region];
		const sg_rune_cell_ref_t cell = built.configuration->cells[
			visibility_partition->configuration_cell].id;
		const sg_rune_phase_ref_t phase =
			PhaseAtPartition(&built, cases[case_index]);
		const uint32_t phase_index = ModelPhaseIndex(&built.model, &phase);
		const sg_rune_motion_t expected = case_index == 1U ?
			SG_RUNE_MOTION_SWIMMING : (case_index == 2U ?
			 SG_RUNE_MOTION_SUPPORTED : SG_RUNE_MOTION_AIRBORNE);
		const sg_rune_support_t expected_support = case_index == 2U ?
			SG_RUNE_SUPPORT_SUPPORTED : SG_RUNE_SUPPORT_NONE;
		const sg_rune_medium_t expected_medium = case_index == 3U ?
			SG_RUNE_MEDIUM_DRY : SG_RUNE_MEDIUM_WATER;
		float point[3];
		sg_rune_bounds_t bounds;
		sg_weapon_static_query_t query;
		sg_weapon_static_affordance_t affordance;
		sg_weapon_static_affordance_error_t error;
		sg_weapon_profile_t machinegun;
		sg_rune_phase_basis_t *phases;
		sg_rune_model_t model;

		CHECK(phase_index != UINT32_MAX);
		if (phase_index == UINT32_MAX)
			continue;
		CHECK(built.model.phases[phase_index].motion == expected);
		CHECK(built.model.phases[phase_index].support == expected_support);
		CHECK(built.model.phases[phase_index].medium == expected_medium);
		memcpy(point, region->interior_witness.value, sizeof(point));
		bounds = BoundsAt(point, 1.0f);
		query = QueryForState(&built, point, point, &bounds, &cell, &phase,
			&cell, &phase, SG_WEAPON_STATIC_DIRECT_VISIBILITY);
		machinegun = ResolveProfile(&built, SG_WEAPON_PROFILE_MACHINEGUN);
		CHECK(ResolveAffordance(&built, &empty_scene, &query, &machinegun,
			&affordance, &error));
		phases = malloc((size_t)built.model.phase_count * sizeof(*phases));
		CHECK(phases != NULL);
		if (!phases)
			continue;
		memcpy(phases, built.model.phases,
			(size_t)built.model.phase_count * sizeof(*phases));
		model = built.model;
		model.phases = phases;
		if (case_index == 0U)
			phases[phase_index].motion = SG_RUNE_MOTION_SWIMMING;
		else if (case_index == 1U)
			phases[phase_index].motion = SG_RUNE_MOTION_AIRBORNE;
		else if (case_index == 2U)
		{
			phases[phase_index].motion = SG_RUNE_MOTION_AIRBORNE;
			phases[phase_index].support = SG_RUNE_SUPPORT_NONE;
		}
		else
		{
			phases[phase_index].motion = SG_RUNE_MOTION_SUPPORTED;
			phases[phase_index].support = SG_RUNE_SUPPORT_SUPPORTED;
		}
		CheckAlteredPhaseRejected(&built, &query, &model);
		free(phases);
	}
	DestroyFixture(&built);
}

static void CheckLawRejectedTransaction(const built_fixture_t *built,
	const sg_weapon_static_query_t *query, const sg_weapon_law_input_t *law,
	sg_weapon_profile_id_t profile_id,
	sg_weapon_static_affordance_error_code_t expected)
{
	sg_weapon_static_affordance_t before, output;
	sg_weapon_static_affordance_error_t error;

	memset(&before, 0xa5, sizeof(before));
	output = before;
	CHECK(!SG_WeaponStaticAffordanceResolve(built->context, &empty_scene,
		query, law, profile_id, &output, &error));
	CHECK(error.code == expected);
	CHECK(memcmp(&output, &before, sizeof(output)) == 0);
}

static void CheckBindingRejected(const built_fixture_t *built,
	const sg_weapon_static_query_t *query, const sg_weapon_profile_t *profile,
	const sg_weapon_static_binding_t *binding)
{
	sg_weapon_static_query_t drifted = *query;
	sg_weapon_static_affordance_t before, output;
	sg_weapon_static_affordance_error_t error;

	drifted.binding = *binding;
	memset(&before, 0xa5, sizeof(before));
	output = before;
	{
		sg_weapon_law_input_t law = WeaponLaw(built);

		CHECK(!SG_WeaponStaticAffordanceResolve(built->context, &empty_scene,
			&drifted, &law, profile->id, &output, &error));
	}
	CHECK(error.code == SG_WEAPON_STATIC_AFFORDANCE_ERROR_IDENTITY_MISMATCH);
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
	sg_weapon_profile_t profile, profile_before;
	sg_weapon_law_input_t invalid_law;
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
	sg_rune_validation_evidence_t model_evidence_before;
	source_snapshot_t snapshots[26];
	size_t snapshot_count = sizeof(snapshots) / sizeof(snapshots[0]);

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
	model_evidence_before = built.model_evidence;
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
	snapshots[18] = (source_snapshot_t){ built.model.planes,
		(size_t)built.model.plane_count * sizeof(*built.model.planes), NULL };
	snapshots[19] = (source_snapshot_t){ &built.model_evidence,
		sizeof(built.model_evidence), NULL };
	snapshots[20] = (source_snapshot_t){ &empty_scene,
		sizeof(empty_scene), NULL };
	snapshots[21] = (source_snapshot_t){ built.artifact_snapshot,
		sizeof(*built.artifact_snapshot), NULL };
	snapshots[22] = (source_snapshot_t){ built.artifact_snapshot->model.cells,
		(size_t)built.artifact_snapshot->model.cell_count *
			sizeof(*built.artifact_snapshot->model.cells), NULL };
	snapshots[23] = (source_snapshot_t){ built.artifact_snapshot->model.phases,
		(size_t)built.artifact_snapshot->model.phase_count *
			sizeof(*built.artifact_snapshot->model.phases), NULL };
	snapshots[24] = (source_snapshot_t){ built.artifact_snapshot->model.planes,
		(size_t)built.artifact_snapshot->model.plane_count *
			sizeof(*built.artifact_snapshot->model.planes), NULL };
	snapshots[25] = (source_snapshot_t){ &built.artifact_snapshot->evidence,
		sizeof(built.artifact_snapshot->evidence), NULL };
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
	CHECK(memcmp(&built.model_evidence, &model_evidence_before,
		sizeof(model_evidence_before)) == 0);
	CheckAndReleaseSources(snapshots, snapshot_count);

	invalid_law = WeaponLaw(&built);
	invalid_law.physics_abi_id++;
	CheckLawRejectedTransaction(&built, &query, &invalid_law, profile.id,
		SG_WEAPON_STATIC_AFFORDANCE_ERROR_IDENTITY_MISMATCH);
	invalid_law = WeaponLaw(&built);
	invalid_law.build_identity++;
	CheckLawRejectedTransaction(&built, &query, &invalid_law, profile.id,
		SG_WEAPON_STATIC_AFFORDANCE_ERROR_IDENTITY_MISMATCH);
	invalid_law = WeaponLaw(&built);
	CheckLawRejectedTransaction(&built, &query, &invalid_law,
		SG_WEAPON_PROFILE_COUNT,
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

	{
		sg_host_collision_instance_t hostile_instance;
		sg_host_collision_scene_t hostile_scene;
		sg_weapon_law_input_t law = WeaponLaw(&built);
		sg_weapon_static_affordance_t before, output;

		memset(&hostile_instance, 0, sizeof(hostile_instance));
		hostile_instance.instance_id = 41U;
		hostile_instance.model_index = UINT32_MAX;
		hostile_scene.instances = &hostile_instance;
		hostile_scene.instance_count = 1U;
		memset(&before, 0xa5, sizeof(before));
		output = before;
		CHECK(!SG_WeaponStaticAffordanceResolve(built.context, &hostile_scene,
			&query, &law, profile.id, &output, &error));
		CHECK(error.code ==
			SG_WEAPON_STATIC_AFFORDANCE_ERROR_VISIBILITY);
		CHECK(memcmp(&output, &before, sizeof(output)) == 0);
	}

	invalid_law = WeaponLaw(&built);
	CHECK(!SG_WeaponStaticAffordanceResolve(NULL, &empty_scene, &query,
		&invalid_law, profile.id, &affordance, &error));
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
	TestWaterLevelMotionBinding();
	TestSurfacePreparationScaling();
	TestPartitionPointIndexScaling();
	TestPosePhaseSemanticBinding();
	TestSemanticPartitionTieBreakIsUnique();
	TestEveryProfileFamilyAndEffect();
	TestOcclusionImpactSplashBounceAndSky();
	TestConditionalMoverAndAreaPortal();
	TestConditionalSurfaceAndSplashRange();
	TestNearbyFloorImpactUsesPolygonPoint();
	TestAlternateVisibleSurfaceCandidate();
	TestBfgOwnerVisibilityAndProjectileOrigin();
	TestPreparedBoundaryRejectsUnvalidatedSources();
	TestNoPvsAndSolidCellCoverage();
	TestCellBindingScalingAndFullCoverage();
	TestIndexedScaling();
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
