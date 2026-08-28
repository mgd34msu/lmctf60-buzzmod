#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sg_water_capability_fixture.h"

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static const sg_water_capability_fact_t *FindFact(
	const sg_water_capability_set_t *capabilities,
	sg_water_capability_kind_t kind, uint32_t source, uint32_t destination,
	sg_water_direction_t direction)
{
	uint32_t fact;

	for (fact = 0U; fact < capabilities->fact_count; fact++)
		if (capabilities->facts[fact].kind == kind &&
			capabilities->facts[fact].source_region == source &&
			capabilities->facts[fact].destination_region == destination &&
			capabilities->facts[fact].direction == direction)
			return &capabilities->facts[fact];
	return NULL;
}

static int PointInSemanticRegion(const water_fixture_t *fixture,
	uint32_t region_index, const float point[3])
{
	const sg_configuration_semantic_region_t *region =
		&fixture->regions[region_index];
	uint32_t face;

	for (face = region->first_face;
		face < region->first_face + region->face_count; face++)
	{
		const sg_configuration_semantic_face_t *plane = &fixture->faces[face];
		float distance = point[0] * plane->normal[0] +
			point[1] * plane->normal[1] + point[2] * plane->normal[2];

		if (distance > plane->distance)
			return 0;
	}
	return 1;
}

static void SetAllWet(water_fixture_t *fixture, uint32_t contents)
{
	uint32_t region;
	uint32_t sample;

	fixture->leaves[1].contents = (int32_t)contents;
	for (region = 0U; region < 2U; region++)
	{
		fixture->regions[region].origin_contents = contents;
		fixture->regions[region].origin_rune_contents =
			SG_HostCollisionRuneContents(contents);
		for (sample = 0U; sample < 3U; sample++)
			fixture->regions[region].sample_contents[sample] = contents;
		fixture->regions[region].water_type = contents;
		fixture->regions[region].water_level = 3U;
		fixture->regions[region].flags =
			SG_CONFIGURATION_SEMANTIC_REGION_WATER |
			SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE;
		fixture->phases[region] = WaterFixturePhase(fixture, region, region);
	}
}

static void TestVolumeMediumAndGravity(uint32_t contents,
	sg_rune_medium_t medium, float gravity)
{
	water_fixture_t fixture;
	sg_water_capability_set_t *first = NULL;
	sg_water_capability_set_t *second = NULL;
	sg_water_capability_error_t error;
	const sg_water_capability_fact_t *up;
	const sg_water_capability_fact_t *down;
	const sg_water_capability_fact_t *entry;
	const sg_water_capability_fact_t *exit;
	uint32_t fact;

	CHECK(WaterFixtureInit(&fixture, contents, gravity, 0, 0));
	CHECK(WaterFixtureBuild(&fixture, NULL, &first, &error));
	CHECK(error.code == SG_WATER_CAPABILITY_ERROR_NONE);
	CHECK(first != NULL && first->wet_region_count == 1U);
	if (!first)
		return;
	CHECK(first->boundary_count == 1U);
	up = FindFact(first, SG_WATER_CAPABILITY_DIRECTIONAL_SWIM, 1U, 1U,
		SG_WATER_DIRECTION_POSITIVE_Z);
	down = FindFact(first, SG_WATER_CAPABILITY_DIRECTIONAL_SWIM, 1U, 1U,
		SG_WATER_DIRECTION_NEGATIVE_Z);
	entry = FindFact(first, SG_WATER_CAPABILITY_ENTRY, 0U, 1U,
		SG_WATER_DIRECTION_BOUNDARY);
	exit = FindFact(first, SG_WATER_CAPABILITY_EXIT, 1U, 0U,
		SG_WATER_DIRECTION_BOUNDARY);
	CHECK(up != NULL && down != NULL && entry != NULL && exit != NULL);
	if (up)
	{
		const float deep_low[3] = { 32.0f, 20.0f, -63.0f };
		const float deep_high[3] = { 32.0f, -20.0f, 63.0f };

		CHECK(up->source_medium == medium);
		CHECK(up->parameters.gravity == gravity);
		CHECK(up->parameters.drag == 1.0f);
		CHECK(up->parameters.acceleration.max_value == 10.0f);
		CHECK(up->parameters.duration_ms.min_value == 100.0f);
		CHECK(up->observed_velocity.value[2] > 0.0f);
		CHECK(fixture.regions[1].bounds.mins.value[2] == -64.0f);
		CHECK(fixture.regions[1].bounds.maxs.value[2] == 64.0f);
		CHECK(up->parameters.displacement.z.min_value ==
			up->observed_displacement.value[2]);
		CHECK(up->parameters.displacement.z.max_value ==
			up->observed_displacement.value[2]);
		CHECK(-63.0f >= fixture.regions[up->source_region].bounds.mins.value[2]);
		CHECK(63.0f <= fixture.regions[up->source_region].bounds.maxs.value[2]);
		CHECK(PointInSemanticRegion(&fixture, up->source_region, deep_low));
		CHECK(PointInSemanticRegion(&fixture, up->source_region, deep_high));
	}
	if (down)
		CHECK(down->observed_velocity.value[2] < 0.0f);
	if (entry)
	{
		CHECK(entry->flags & SG_WATER_CAPABILITY_CHANGES_MEDIUM);
		CHECK(entry->boundary_witness.value[0] == 0.0f);
		CHECK(entry->source_witness.value[0] < 0.0f);
		CHECK(entry->destination_witness.value[0] > 0.0f);
		CHECK(entry->observed_displacement.value[0] > 0.0f);
	}
	CHECK(FindFact(first, SG_WATER_CAPABILITY_SINK, 1U, 1U,
		SG_WATER_DIRECTION_NEGATIVE_Z) != NULL);
	CHECK(FindFact(first, SG_WATER_CAPABILITY_SURFACE, 1U, 1U,
		SG_WATER_DIRECTION_POSITIVE_Z) != NULL);
	for (fact = 0U; fact < first->fact_count; fact++)
		CHECK(first->facts[fact].order == fact);
	CHECK(WaterFixtureBuild(&fixture, NULL, &second, &error));
	CHECK(second != NULL && second->fact_count == first->fact_count);
	if (second && second->fact_count == first->fact_count)
		CHECK(memcmp(second->facts, first->facts,
			(size_t)first->fact_count * sizeof(*first->facts)) == 0);
	SG_WaterCapabilityDestroy(first);
	SG_WaterCapabilityDestroy(second);
}

static void TestCurrents(void)
{
	static const sg_water_direction_t directions[6] = {
		SG_WATER_DIRECTION_POSITIVE_X, SG_WATER_DIRECTION_POSITIVE_Y,
		SG_WATER_DIRECTION_NEGATIVE_X, SG_WATER_DIRECTION_NEGATIVE_Y,
		SG_WATER_DIRECTION_POSITIVE_Z, SG_WATER_DIRECTION_NEGATIVE_Z
	};
	static const sg_rune_contents_mask_t current_bits[6] = {
		SG_RUNE_CONTENTS_CURRENT_0, SG_RUNE_CONTENTS_CURRENT_90,
		SG_RUNE_CONTENTS_CURRENT_180, SG_RUNE_CONTENTS_CURRENT_270,
		SG_RUNE_CONTENTS_CURRENT_UP, SG_RUNE_CONTENTS_CURRENT_DOWN
	};
	static const uint32_t host_bits[6] = {
		SG_HOST_CONTENTS_CURRENT_0, SG_HOST_CONTENTS_CURRENT_90,
		SG_HOST_CONTENTS_CURRENT_180, SG_HOST_CONTENTS_CURRENT_270,
		SG_HOST_CONTENTS_CURRENT_UP, SG_HOST_CONTENTS_CURRENT_DOWN
	};
	static const uint32_t axes[6] = { 0U, 1U, 0U, 1U, 2U, 2U };
	static const int signs[6] = { 1, 1, -1, -1, 1, -1 };
	uint32_t direction;

	for (direction = 0U; direction < 6U; direction++)
	{
		water_fixture_t fixture;
		sg_water_capability_set_t *capabilities = NULL;
		sg_water_capability_error_t error;
		const sg_water_capability_fact_t *current;
		uint32_t axis = axes[direction];
		uint32_t fact;
		uint32_t current_count = 0U;

		CHECK(WaterFixtureInit(&fixture,
			SG_HOST_CONTENTS_WATER | host_bits[direction], 800.0f, 0, 0));
		CHECK(WaterFixtureBuild(&fixture, NULL, &capabilities, &error));
		if (!capabilities)
			continue;
		for (fact = 0U; fact < capabilities->fact_count; fact++)
			if (capabilities->facts[fact].kind == SG_WATER_CAPABILITY_CURRENT)
				current_count++;
		CHECK(current_count == 1U);
		current = FindFact(capabilities, SG_WATER_CAPABILITY_CURRENT,
			1U, 1U, directions[direction]);
		CHECK(current != NULL);
		if (current)
		{
			CHECK(current->current == current_bits[direction]);
			CHECK(current->flags & SG_WATER_CAPABILITY_USES_CURRENT);
			CHECK(signs[direction] > 0 ?
				current->observed_velocity.value[axis] > 0.0f :
				current->observed_velocity.value[axis] < 0.0f);
		}
		SG_WaterCapabilityDestroy(capabilities);
	}
}

static void TestPortalAndSubmergedCorridor(void)
{
	water_fixture_t fixture;
	sg_water_capability_set_t *capabilities = NULL;
	sg_water_capability_error_t error;
	const sg_water_capability_fact_t *entry;

	CHECK(WaterFixtureInit(&fixture, SG_HOST_CONTENTS_WATER, 800.0f, 0, 1));
	CHECK(WaterFixtureBuild(&fixture, NULL, &capabilities, &error));
	if (!capabilities)
		return;
	entry = FindFact(capabilities, SG_WATER_CAPABILITY_ENTRY, 0U, 1U,
		SG_WATER_DIRECTION_BOUNDARY);
	CHECK(entry != NULL);
	if (entry)
	{
		CHECK(entry->portal == 0U);
		CHECK(entry->flags & SG_WATER_CAPABILITY_CROSSES_PORTAL);
	}
	SG_WaterCapabilityDestroy(capabilities);

	CHECK(WaterFixtureInit(&fixture, SG_HOST_CONTENTS_WATER, 800.0f, 0, 1));
	fixture.faces[0].normal[0] /= 16384.0f;
	fixture.faces[7].normal[0] /= 16384.0f;
	capabilities = NULL;
	CHECK(WaterFixtureBuild(&fixture, NULL, &capabilities, &error));
	if (capabilities)
	{
		entry = FindFact(capabilities, SG_WATER_CAPABILITY_ENTRY, 0U, 1U,
			SG_WATER_DIRECTION_BOUNDARY);
		CHECK(entry != NULL && entry->portal == 0U);
		SG_WaterCapabilityDestroy(capabilities);
	}

	CHECK(WaterFixtureInit(&fixture, SG_HOST_CONTENTS_WATER, 800.0f, 0, 1));
	fixture.portal_vertices[0].value[1] = 24.0f;
	fixture.portal_vertices[1].value[1] = 40.0f;
	fixture.portal_vertices[2].value[1] = 40.0f;
	fixture.portal_vertices[3].value[1] = 24.0f;
	capabilities = NULL;
	CHECK(WaterFixtureBuild(&fixture, NULL, &capabilities, &error));
	if (capabilities)
	{
		entry = FindFact(capabilities, SG_WATER_CAPABILITY_ENTRY, 0U, 1U,
			SG_WATER_DIRECTION_BOUNDARY);
		CHECK(entry != NULL);
		if (entry)
		{
			CHECK(entry->boundary_witness.value[1] > 24.0f);
			CHECK(entry->boundary_witness.value[1] < 40.0f);
		}
		SG_WaterCapabilityDestroy(capabilities);
	}

	CHECK(WaterFixtureInit(&fixture, SG_HOST_CONTENTS_WATER, 800.0f, 0, 1));
	SetAllWet(&fixture, SG_HOST_CONTENTS_WATER);
	capabilities = NULL;
	CHECK(WaterFixtureBuild(&fixture, NULL, &capabilities, &error));
	if (capabilities)
	{
		CHECK(FindFact(capabilities, SG_WATER_CAPABILITY_VOLUME_CROSSING,
			0U, 1U, SG_WATER_DIRECTION_BOUNDARY) != NULL);
		SG_WaterCapabilityDestroy(capabilities);
	}
}

static void TestPartialBoundaryOverlap(void)
{
	water_fixture_t fixture;
	sg_water_capability_set_t *capabilities = NULL;
	sg_water_capability_error_t error;
	const sg_water_capability_fact_t *entry;

	CHECK(WaterFixtureInit(&fixture, SG_HOST_CONTENTS_WATER, 800.0f, 0, 0));
	fixture.regions[0].bounds.maxs.value[1] = 16.0f;
	fixture.faces[2].distance = 16.0f;
	fixture.regions[1].bounds.mins.value[1] = -16.0f;
	fixture.faces[9].distance = 16.0f;
	CHECK(WaterFixtureBuild(&fixture, NULL, &capabilities, &error));
	if (!capabilities)
		return;
	entry = FindFact(capabilities, SG_WATER_CAPABILITY_ENTRY, 0U, 1U,
		SG_WATER_DIRECTION_BOUNDARY);
	CHECK(entry != NULL);
	if (entry)
	{
		CHECK(entry->boundary_witness.value[1] >= -16.0f);
		CHECK(entry->boundary_witness.value[1] <= 16.0f);
		CHECK(entry->source_witness.value[1] >= -16.0f);
		CHECK(entry->source_witness.value[1] <= 16.0f);
		CHECK(entry->destination_witness.value[1] >= -16.0f);
		CHECK(entry->destination_witness.value[1] <= 16.0f);
		CHECK(entry->source_witness.value[0] == -0.125f);
		CHECK(entry->destination_witness.value[0] == 0.125f);
	}
	SG_WaterCapabilityDestroy(capabilities);
}

static void TestHostileFaceMultiplicity(void)
{
	enum { STRIPES = 16, REGIONS = STRIPES * 2, FACES = REGIONS * 6 };
	water_fixture_t fixture;
	sg_configuration_semantic_region_t *regions;
	sg_configuration_semantic_face_t *faces;
	sg_rune_phase_basis_t *phases;
	sg_water_phase_binding_t *bindings;
	sg_water_capability_set_t *capabilities = NULL;
	sg_water_capability_error_t error;
	uint32_t region;

	CHECK(WaterFixtureInit(&fixture, SG_HOST_CONTENTS_WATER, 800.0f, 0, 0));
	regions = calloc(REGIONS, sizeof(*regions));
	faces = calloc(FACES, sizeof(*faces));
	phases = calloc(REGIONS, sizeof(*phases));
	bindings = calloc(REGIONS, sizeof(*bindings));
	CHECK(regions != NULL && faces != NULL && phases != NULL && bindings != NULL);
	if (!regions || !faces || !phases || !bindings)
		goto done;
	for (region = 0U; region < REGIONS; region++)
	{
		uint32_t side = region & 1U;
		uint32_t stripe = region / 2U;
		uint32_t face;
		float min_y = -64.0f + (float)stripe * 8.0f;
		float max_y = min_y + 8.0f;

		regions[region] = fixture.regions[side];
		regions[region].id = UINT64_C(1000) + region;
		regions[region].first_face = region * 6U;
		regions[region].bounds.mins.value[1] = min_y;
		regions[region].bounds.maxs.value[1] = max_y;
		regions[region].interior_witness.value[1] = (min_y + max_y) * 0.5f;
		for (face = 0U; face < 6U; face++)
			faces[region * 6U + face] = fixture.faces[side * 6U + face];
		faces[region * 6U + 2U].distance = max_y;
		faces[region * 6U + 3U].distance = -min_y;
		phases[region] = fixture.phases[side];
		phases[region].order.source_index = region;
		phases[region].order.local_ordinal = region;
		phases[region].order.variant = region;
		phases[region].id.value =
			SG_RuneModelStableIdFromOrderKey(&phases[region].order);
		bindings[region].semantic_region_id = regions[region].id;
		bindings[region].phase = region;
	}
	fixture.semantics.regions = regions;
	fixture.semantics.region_count = REGIONS;
	fixture.semantics.faces = faces;
	fixture.semantics.face_count = FACES;
	CHECK(SG_WaterCapabilityBuild(&fixture.authority, WaterFixturePmove,
		&fixture.configuration, &fixture.semantics, phases, REGIONS,
		bindings, REGIONS, NULL, &capabilities, &error));
	if (capabilities)
	{
		CHECK(capabilities->fact_count == STRIPES * 10U);
		CHECK(capabilities->wet_region_count == STRIPES);
		CHECK(capabilities->boundary_count == STRIPES);
		CHECK(capabilities->lattice_solve_calls < STRIPES * STRIPES);
		SG_WaterCapabilityDestroy(capabilities);
	}
done:
	free(regions);
	free(faces);
	free(phases);
	free(bindings);
}

static void TestBlockedExit(void)
{
	water_fixture_t fixture;
	sg_water_capability_set_t *capabilities = NULL;
	sg_water_capability_error_t error;

	CHECK(WaterFixtureInit(&fixture, SG_HOST_CONTENTS_WATER, 800.0f, 1, 0));
	CHECK(WaterFixtureBuild(&fixture, NULL, &capabilities, &error));
	if (capabilities)
	{
		CHECK(capabilities->boundary_count == 0U);
		CHECK(FindFact(capabilities, SG_WATER_CAPABILITY_EXIT, 1U, 0U,
			SG_WATER_DIRECTION_BOUNDARY) == NULL);
		SG_WaterCapabilityDestroy(capabilities);
	}
}

static void TestPhaseVelocityAndNarrowVolume(void)
{
	water_fixture_t fixture;
	sg_water_capability_set_t *capabilities = NULL;
	sg_water_capability_error_t error;
	const sg_water_capability_fact_t *positive_x;

	CHECK(WaterFixtureInit(&fixture, SG_HOST_CONTENTS_WATER, 800.0f, 0, 0));
	fixture.phases[1].velocity.x.min_value = 80.0f;
	fixture.phases[1].velocity.x.max_value = 100.0f;
	fixture.phases[1].velocity.y.min_value = 0.0f;
	fixture.phases[1].velocity.y.max_value = 0.0f;
	fixture.phases[1].velocity.z.min_value = 0.0f;
	fixture.phases[1].velocity.z.max_value = 0.0f;
	CHECK(WaterFixtureBuild(&fixture, NULL, &capabilities, &error));
	if (capabilities)
	{
		positive_x = FindFact(capabilities,
			SG_WATER_CAPABILITY_DIRECTIONAL_SWIM, 1U, 1U,
			SG_WATER_DIRECTION_POSITIVE_X);
		CHECK(positive_x != NULL);
		if (positive_x)
		{
			CHECK(positive_x->destination_phase == 1U);
			CHECK(positive_x->observed_velocity.value[0] > 80.0f);
			CHECK(positive_x->observed_velocity.value[0] <= 100.0f);
		}
		SG_WaterCapabilityDestroy(capabilities);
	}

	CHECK(WaterFixtureInit(&fixture, SG_HOST_CONTENTS_WATER, 800.0f, 0, 0));
	fixture.regions[1].bounds.maxs.value[0] = 0.5f;
	fixture.regions[1].interior_witness.value[0] = 0.25f;
	fixture.faces[6].distance = 0.5f;
	capabilities = NULL;
	CHECK(WaterFixtureBuild(&fixture, NULL, &capabilities, &error));
	if (capabilities)
	{
		CHECK(FindFact(capabilities, SG_WATER_CAPABILITY_DIRECTIONAL_SWIM,
			1U, 1U, SG_WATER_DIRECTION_POSITIVE_X) == NULL);
		CHECK(FindFact(capabilities, SG_WATER_CAPABILITY_DIRECTIONAL_SWIM,
			1U, 1U, SG_WATER_DIRECTION_POSITIVE_Z) != NULL);
		SG_WaterCapabilityDestroy(capabilities);
	}
}

static void TestDestinationPhaseSelection(void)
{
	water_fixture_t fixture;
	sg_rune_phase_basis_t phases[3];
	sg_water_phase_binding_t bindings[3];
	sg_water_capability_set_t *capabilities = NULL;
	sg_water_capability_error_t error;
	uint32_t fact;
	uint32_t entries = 0U;

	CHECK(WaterFixtureInit(&fixture, SG_HOST_CONTENTS_WATER, 800.0f, 0, 0));
	phases[0] = fixture.phases[0];
	phases[1] = fixture.phases[1];
	phases[1].velocity.x.min_value = -100.0f;
	phases[1].velocity.x.max_value = -80.0f;
	phases[2] = fixture.phases[1];
	phases[2].order.source_index = 2U;
	phases[2].order.local_ordinal = 2U;
	phases[2].order.variant = 2U;
	phases[2].id.value =
		SG_RuneModelStableIdFromOrderKey(&phases[2].order);
	phases[2].velocity.x.min_value = 0.0f;
	phases[2].velocity.x.max_value = 20.0f;
	bindings[0] = fixture.bindings[0];
	bindings[1] = fixture.bindings[1];
	bindings[2] = fixture.bindings[1];
	bindings[2].phase = 2U;
	CHECK(SG_WaterCapabilityBuild(&fixture.authority, WaterFixturePmove,
		&fixture.configuration, &fixture.semantics, phases, 3U,
		bindings, 3U, NULL, &capabilities, &error));
	if (!capabilities)
		return;
	for (fact = 0U; fact < capabilities->fact_count; fact++)
		if (capabilities->facts[fact].kind == SG_WATER_CAPABILITY_ENTRY)
		{
			entries++;
			CHECK(capabilities->facts[fact].source_phase == 0U);
			CHECK(capabilities->facts[fact].destination_phase == 2U);
			CHECK(capabilities->facts[fact].observed_velocity.value[0] >=
				phases[2].velocity.x.min_value);
			CHECK(capabilities->facts[fact].observed_velocity.value[0] <=
				phases[2].velocity.x.max_value);
		}
	CHECK(entries == 1U);
	SG_WaterCapabilityDestroy(capabilities);
}

static void TestSameRegionPhaseSelection(void)
{
	water_fixture_t fixture;
	sg_rune_phase_basis_t phases[3];
	sg_water_phase_binding_t bindings[3];
	sg_water_capability_set_t *capabilities = NULL;
	sg_water_capability_error_t error;
	uint32_t fact;
	uint32_t transitions = 0U;

	CHECK(WaterFixtureInit(&fixture, SG_HOST_CONTENTS_WATER, 800.0f, 0, 0));
	phases[0] = fixture.phases[0];
	phases[1] = fixture.phases[1];
	phases[1].velocity.x.min_value = 0.0f;
	phases[1].velocity.x.max_value = 0.0f;
	phases[2] = fixture.phases[1];
	phases[2].order.source_index = 2U;
	phases[2].order.local_ordinal = 2U;
	phases[2].order.variant = 2U;
	phases[2].id.value =
		SG_RuneModelStableIdFromOrderKey(&phases[2].order);
	phases[2].velocity.x.min_value = 12.0f;
	phases[2].velocity.x.max_value = 13.0f;
	bindings[0] = fixture.bindings[0];
	bindings[1] = fixture.bindings[1];
	bindings[2] = fixture.bindings[1];
	bindings[2].phase = 2U;
	CHECK(SG_WaterCapabilityBuild(&fixture.authority, WaterFixturePmove,
		&fixture.configuration, &fixture.semantics, phases, 3U,
		bindings, 3U, NULL, &capabilities, &error));
	if (!capabilities)
		return;
	for (fact = 0U; fact < capabilities->fact_count; fact++)
		if (capabilities->facts[fact].kind ==
				SG_WATER_CAPABILITY_DIRECTIONAL_SWIM &&
			capabilities->facts[fact].source_region == 1U &&
			capabilities->facts[fact].destination_region == 1U &&
			capabilities->facts[fact].direction ==
				SG_WATER_DIRECTION_POSITIVE_X &&
			capabilities->facts[fact].source_phase == 1U)
		{
			transitions++;
			CHECK(capabilities->facts[fact].destination_phase == 2U);
			CHECK(capabilities->facts[fact].observed_velocity.value[0] ==
				12.5f);
		}
	CHECK(transitions == 1U);
	SG_WaterCapabilityDestroy(capabilities);
}

static sg_water_capability_set_t *BuildNarrowPlane(float normal_x,
	float normal_y, float distance)
{
	water_fixture_t fixture;
	sg_water_capability_set_t *capabilities = NULL;
	sg_water_capability_error_t error;

	CHECK(WaterFixtureInit(&fixture, SG_HOST_CONTENTS_WATER, 800.0f, 0, 0));
	fixture.regions[1].bounds.maxs.value[0] = 0.5f;
	fixture.regions[1].interior_witness.value[0] = 0.25f;
	fixture.faces[6].normal[0] = normal_x;
	fixture.faces[6].normal[1] = normal_y;
	fixture.faces[6].distance = distance;
	CHECK(WaterFixtureBuild(&fixture, NULL, &capabilities, &error));
	return capabilities;
}

static void CheckEquivalentFacts(const sg_water_capability_set_t *first,
	const sg_water_capability_set_t *second)
{
	CHECK(first != NULL && second != NULL);
	if (!first || !second)
		return;
	CHECK(first->fact_count == second->fact_count);
	CHECK(first->boundary_count == second->boundary_count);
	CHECK(first->host_pmove_frames == second->host_pmove_frames);
	if (first->fact_count == second->fact_count)
		CHECK(memcmp(first->facts, second->facts,
			(size_t)first->fact_count * sizeof(*first->facts)) == 0);
}

static void TestPlaneScaleInvariantContainment(void)
{
	const float small = 1.0f / 16384.0f;
	sg_water_capability_set_t *axis = BuildNarrowPlane(1.0f, 0.0f, 0.5f);
	sg_water_capability_set_t *scaled_axis =
		BuildNarrowPlane(small, 0.0f, 0.5f * small);
	sg_water_capability_set_t *oblique = BuildNarrowPlane(1.0f, 1.0f, 0.5f);
	sg_water_capability_set_t *scaled_oblique =
		BuildNarrowPlane(small, small, 0.5f * small);

	CheckEquivalentFacts(axis, scaled_axis);
	CheckEquivalentFacts(oblique, scaled_oblique);
	if (axis)
		CHECK(FindFact(axis, SG_WATER_CAPABILITY_DIRECTIONAL_SWIM,
			1U, 1U, SG_WATER_DIRECTION_POSITIVE_X) == NULL);
	if (scaled_axis)
		CHECK(FindFact(scaled_axis, SG_WATER_CAPABILITY_DIRECTIONAL_SWIM,
			1U, 1U, SG_WATER_DIRECTION_POSITIVE_X) == NULL);
	if (oblique)
	{
		CHECK(FindFact(oblique, SG_WATER_CAPABILITY_DIRECTIONAL_SWIM,
			1U, 1U, SG_WATER_DIRECTION_POSITIVE_X) == NULL);
		CHECK(FindFact(oblique, SG_WATER_CAPABILITY_DIRECTIONAL_SWIM,
			1U, 1U, SG_WATER_DIRECTION_POSITIVE_Y) == NULL);
	}
	if (scaled_oblique)
	{
		CHECK(FindFact(scaled_oblique,
			SG_WATER_CAPABILITY_DIRECTIONAL_SWIM, 1U, 1U,
			SG_WATER_DIRECTION_POSITIVE_X) == NULL);
		CHECK(FindFact(scaled_oblique,
			SG_WATER_CAPABILITY_DIRECTIONAL_SWIM, 1U, 1U,
			SG_WATER_DIRECTION_POSITIVE_Y) == NULL);
	}
	SG_WaterCapabilityDestroy(axis);
	SG_WaterCapabilityDestroy(scaled_axis);
	SG_WaterCapabilityDestroy(oblique);
	SG_WaterCapabilityDestroy(scaled_oblique);
}

static void TestFailureTransactionAndImmutability(void)
{
	water_fixture_t fixture;
	water_fixture_t before;
	sg_water_capability_set_t *capabilities = NULL;
	sg_water_capability_limits_t limits;
	sg_water_capability_error_t error;

	CHECK(WaterFixtureInit(&fixture, SG_HOST_CONTENTS_WATER, 800.0f, 0, 0));
	memcpy(&before, &fixture, sizeof(before));
	SG_WaterCapabilityDefaultLimits(&limits);
	limits.max_facts = 1U;
	CHECK(!WaterFixtureBuild(&fixture, &limits, &capabilities, &error));
	CHECK(error.code == SG_WATER_CAPABILITY_ERROR_OVERFLOW);
	CHECK(capabilities == NULL);
	CHECK(memcmp(&before.configuration, &fixture.configuration,
		sizeof(fixture.configuration)) == 0);
	CHECK(memcmp(before.cells, fixture.cells, sizeof(fixture.cells)) == 0);
	CHECK(memcmp(&before.semantics, &fixture.semantics,
		sizeof(fixture.semantics)) == 0);
	CHECK(memcmp(before.regions, fixture.regions, sizeof(fixture.regions)) == 0);
	CHECK(memcmp(before.faces, fixture.faces, sizeof(fixture.faces)) == 0);
	CHECK(memcmp(before.phases, fixture.phases, sizeof(fixture.phases)) == 0);
	CHECK(memcmp(before.bindings, fixture.bindings,
		sizeof(fixture.bindings)) == 0);
	fixture.configuration.identity.physics.gravity = 100.0f;
	CHECK(!WaterFixtureBuild(&fixture, NULL, &capabilities, &error));
	CHECK(error.code == SG_WATER_CAPABILITY_ERROR_INVALID_SOURCE);
	CHECK(capabilities == NULL);
	fixture.configuration.identity = fixture.authority.identity;
	fixture.semantics.identity = fixture.authority.identity;
	fixture.authority.identity.physics.gravity = 32768.0f;
	fixture.configuration.identity.physics.gravity = 32768.0f;
	fixture.semantics.identity.physics.gravity = 32768.0f;
	CHECK(!WaterFixtureBuild(&fixture, NULL, &capabilities, &error));
	CHECK(error.code == SG_WATER_CAPABILITY_ERROR_INVALID_SOURCE);
	CHECK(capabilities == NULL);
	fixture.authority.identity.physics.gravity = 100.5f;
	fixture.configuration.identity.physics.gravity = 100.5f;
	fixture.semantics.identity.physics.gravity = 100.5f;
	CHECK(!WaterFixtureBuild(&fixture, NULL, &capabilities, &error));
	CHECK(error.code == SG_WATER_CAPABILITY_ERROR_INVALID_SOURCE);
	CHECK(capabilities == NULL);
}

static void TestMediaMismatch(void)
{
	water_fixture_t fixture;
	sg_water_capability_set_t *capabilities = NULL;
	sg_water_capability_error_t error;

	CHECK(WaterFixtureInit(&fixture, SG_HOST_CONTENTS_WATER, 800.0f, 0, 0));
	fixture.regions[1].water_type = SG_HOST_CONTENTS_LAVA;
	CHECK(!WaterFixtureBuild(&fixture, NULL, &capabilities, &error));
	CHECK(error.code == SG_WATER_CAPABILITY_ERROR_INVALID_SOURCE);
	CHECK(capabilities == NULL);
	CHECK(WaterFixtureInit(&fixture, SG_HOST_CONTENTS_WATER, 800.0f, 0, 0));
	fixture.phases[1].medium = SG_RUNE_MEDIUM_LAVA;
	CHECK(!WaterFixtureBuild(&fixture, NULL, &capabilities, &error));
	CHECK(error.code == SG_WATER_CAPABILITY_ERROR_INVALID_PHASE);
	CHECK(capabilities == NULL);
}

int main(void)
{
	sg_water_capability_limits_t limits;

	SG_WaterCapabilityDefaultLimits(&limits);
	CHECK(limits.max_facts == SG_RUNE_MODEL_MAX_KERNELS);
	TestVolumeMediumAndGravity(SG_HOST_CONTENTS_WATER,
		SG_RUNE_MEDIUM_WATER, 100.0f);
	TestVolumeMediumAndGravity(SG_HOST_CONTENTS_WATER,
		SG_RUNE_MEDIUM_WATER, 800.0f);
	TestVolumeMediumAndGravity(SG_HOST_CONTENTS_LAVA,
		SG_RUNE_MEDIUM_LAVA, 800.0f);
	TestVolumeMediumAndGravity(SG_HOST_CONTENTS_SLIME,
		SG_RUNE_MEDIUM_SLIME, 800.0f);
	TestCurrents();
	TestPortalAndSubmergedCorridor();
	TestPartialBoundaryOverlap();
	TestHostileFaceMultiplicity();
	TestBlockedExit();
	TestPhaseVelocityAndNarrowVolume();
	TestDestinationPhaseSelection();
	TestSameRegionPhaseSelection();
	TestPlaneScaleInvariantContainment();
	TestFailureTransactionAndImmutability();
	TestMediaMismatch();
	if (failures)
	{
		fprintf(stderr, "sg_water_capability_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("sg_water_capability_test: ok");
	return 0;
}
