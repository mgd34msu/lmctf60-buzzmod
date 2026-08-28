#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sg_water_capability_fixture.h"

static int failures;
static uint32_t host_probe_calls;
static uint32_t sink_zero_command_calls;
static int sink_commands_exact;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static max_align_t shallow_ground;

static void CountingPmove(pmove_t *pmove)
{
	host_probe_calls++;
	WaterFixturePmove(pmove);
}

static void SinkRecordingPmove(pmove_t *pmove)
{
	host_probe_calls++;
	if (pmove->cmd.forwardmove == 0 && pmove->cmd.sidemove == 0 &&
		pmove->cmd.upmove == 0)
	{
		uint32_t axis;

		sink_zero_command_calls++;
		if (pmove->cmd.msec != 10U || pmove->cmd.buttons != 0U ||
			pmove->cmd.impulse != 0U || pmove->cmd.lightlevel != 0U)
			sink_commands_exact = 0;
		for (axis = 0U; axis < 3U; axis++)
			if (pmove->cmd.angles[axis] != 0)
				sink_commands_exact = 0;
	}
	WaterFixturePmove(pmove);
}

static void ShallowGroundedPmove(pmove_t *pmove)
{
	short before[3];
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		before[axis] = pmove->s.velocity[axis];
	WaterFixturePmove(pmove);
	for (axis = 0U; axis < 3U; axis++)
		pmove->s.velocity[axis] = (short)(before[axis] +
			(pmove->s.velocity[axis] - before[axis]) / 2);
	pmove->waterlevel = 1;
	pmove->groundentity = (struct edict_s *)(void *)&shallow_ground;
}

static void StandingPmove(pmove_t *pmove)
{
	WaterFixturePmove(pmove);
	pmove->s.pm_flags &= (byte)~PMF_DUCKED;
	pmove->mins[0] = pmove->mins[1] = pmove->mins[2] = -1.0f;
	pmove->maxs[0] = pmove->maxs[1] = pmove->maxs[2] = 1.0f;
}

static void WrongMediumPmove(pmove_t *pmove)
{
	WaterFixturePmove(pmove);
	pmove->watertype = SG_HOST_CONTENTS_LAVA;
	pmove->waterlevel = 3;
}

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

static void SetRegionWaterState(water_fixture_t *fixture, uint32_t region,
	uint32_t contents, uint8_t water_level,
	sg_configuration_semantic_region_flags_t flags)
{
	uint32_t sample;

	fixture->regions[region].origin_contents = contents;
	fixture->regions[region].origin_rune_contents =
		SG_HostCollisionRuneContents(contents);
	for (sample = 0U; sample < 3U; sample++)
		fixture->regions[region].sample_contents[sample] = contents;
	fixture->regions[region].water_type = water_level ? contents : 0U;
	fixture->regions[region].water_level = water_level;
	fixture->regions[region].flags = flags;
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
	CHECK(WaterFixtureBuild(&fixture, &first, &error));
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
		CHECK(entry->flags & SG_WATER_CAPABILITY_STRADDLES_FRAME_LAW);
		CHECK(entry->parameters.acceleration.max_value == 1.0f);
		CHECK(entry->parameters.drag == 0.0f);
		CHECK(entry->boundary_witness.value[0] == 0.0f);
		CHECK(entry->source_witness.value[0] < 0.0f);
		CHECK(entry->destination_witness.value[0] > 0.0f);
		CHECK(entry->observed_displacement.value[0] > 0.0f);
	}
	if (exit)
	{
		CHECK(exit->flags & SG_WATER_CAPABILITY_CHANGES_MEDIUM);
		CHECK(exit->flags & SG_WATER_CAPABILITY_STRADDLES_FRAME_LAW);
		CHECK(exit->parameters.acceleration.max_value == 10.0f);
		CHECK(exit->parameters.drag == 1.0f);
	}
	CHECK(FindFact(first, SG_WATER_CAPABILITY_SINK, 1U, 1U,
		SG_WATER_DIRECTION_NEGATIVE_Z) != NULL);
	CHECK(FindFact(first, SG_WATER_CAPABILITY_SURFACE, 1U, 1U,
		SG_WATER_DIRECTION_POSITIVE_Z) != NULL);
	for (fact = 0U; fact < first->fact_count; fact++)
		CHECK(first->facts[fact].order == fact);
	CHECK(WaterFixtureBuild(&fixture, &second, &error));
	CHECK(second != NULL && second->fact_count == first->fact_count);
	if (second && second->fact_count == first->fact_count)
		CHECK(memcmp(second->facts, first->facts,
			(size_t)first->fact_count * sizeof(*first->facts)) == 0);
	SG_WaterCapabilityDestroy(first);
	SG_WaterCapabilityDestroy(second);
}

static void TestSinkReplayCommand(void)
{
	water_fixture_t fixture;
	sg_water_capability_set_t *capabilities = NULL;
	sg_water_capability_error_t error;
	const sg_water_capability_fact_t *sink;

	CHECK(WaterFixtureInit(&fixture, SG_HOST_CONTENTS_WATER, 800.0f, 0, 0));
	host_probe_calls = 0U;
	sink_zero_command_calls = 0U;
	sink_commands_exact = 1;
	CHECK(SG_WaterCapabilityBuild(&fixture.authority, SinkRecordingPmove,
		&fixture.configuration, &fixture.semantics, fixture.phases, 2U,
		fixture.bindings, 2U, &capabilities, &error));
	if (!capabilities)
		return;
	sink = FindFact(capabilities, SG_WATER_CAPABILITY_SINK, 1U, 1U,
		SG_WATER_DIRECTION_NEGATIVE_Z);
	CHECK(sink != NULL);
	if (sink)
	{
		CHECK(sink->command_vector.value[0] == 0.0f);
		CHECK(sink->command_vector.value[1] == 0.0f);
		CHECK(sink->command_vector.value[2] == 0.0f);
	}
	CHECK(sink_zero_command_calls == 10U);
	CHECK(sink_commands_exact);
	CHECK(host_probe_calls == capabilities->host_pmove_frames * 10U);
	SG_WaterCapabilityDestroy(capabilities);
}

static void TestCurrents(void)
{
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
	float deep_speed = 0.0f;
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
		CHECK(WaterFixtureBuild(&fixture, &capabilities, &error));
		if (!capabilities)
			continue;
		for (fact = 0U; fact < capabilities->fact_count; fact++)
			if (capabilities->facts[fact].kind == SG_WATER_CAPABILITY_CURRENT)
				current_count++;
		CHECK(current_count == 1U);
		current = FindFact(capabilities, SG_WATER_CAPABILITY_CURRENT,
			1U, 1U, SG_WATER_DIRECTION_COMBINED);
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
	{
		water_fixture_t fixture;
		sg_water_capability_set_t *capabilities = NULL;
		sg_water_capability_error_t error;
		const sg_water_capability_fact_t *current;

		CHECK(WaterFixtureInit(&fixture, SG_HOST_CONTENTS_WATER |
			SG_HOST_CONTENTS_CURRENT_0 | SG_HOST_CONTENTS_CURRENT_90,
			800.0f, 0, 0));
		CHECK(WaterFixtureBuild(&fixture, &capabilities, &error));
		if (capabilities)
		{
			current = FindFact(capabilities, SG_WATER_CAPABILITY_CURRENT,
				1U, 1U, SG_WATER_DIRECTION_COMBINED);
			CHECK(current != NULL);
			if (current)
			{
				CHECK(current->current == (SG_RUNE_CONTENTS_CURRENT_0 |
					SG_RUNE_CONTENTS_CURRENT_90));
				CHECK(current->direction_vector.value[0] == 1.0f);
				CHECK(current->direction_vector.value[1] == 1.0f);
				CHECK(current->command_vector.value[0] == 0.0f);
				CHECK(current->command_vector.value[1] == 0.0f);
				CHECK(current->observed_velocity.value[0] > 0.0f);
				CHECK(current->observed_velocity.value[1] > 0.0f);
			}
			SG_WaterCapabilityDestroy(capabilities);
		}

		CHECK(WaterFixtureInit(&fixture, SG_HOST_CONTENTS_WATER |
			SG_HOST_CONTENTS_CURRENT_0 | SG_HOST_CONTENTS_CURRENT_180,
			800.0f, 0, 0));
		capabilities = NULL;
		CHECK(WaterFixtureBuild(&fixture, &capabilities, &error));
		if (capabilities)
		{
			current = FindFact(capabilities, SG_WATER_CAPABILITY_CURRENT,
				1U, 1U, SG_WATER_DIRECTION_COMBINED);
			CHECK(current != NULL);
			if (current)
			{
				CHECK(current->direction_vector.value[0] == 0.0f);
				CHECK(current->observed_velocity.value[0] == 0.0f);
			}
			SG_WaterCapabilityDestroy(capabilities);
		}

		CHECK(WaterFixtureInit(&fixture, SG_HOST_CONTENTS_WATER |
			SG_HOST_CONTENTS_CURRENT_0, 800.0f, 0, 0));
		capabilities = NULL;
		CHECK(WaterFixtureBuild(&fixture, &capabilities, &error));
		if (capabilities)
		{
			current = FindFact(capabilities, SG_WATER_CAPABILITY_CURRENT,
				1U, 1U, SG_WATER_DIRECTION_COMBINED);
			if (current)
				deep_speed = current->observed_velocity.value[0];
			SG_WaterCapabilityDestroy(capabilities);
		}

		fixture.regions[1].water_level = 1U;
		fixture.regions[1].flags = SG_CONFIGURATION_SEMANTIC_REGION_WATER |
			SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE;
		fixture.phases[1] = WaterFixturePhase(&fixture, 1U, 1U);
		capabilities = NULL;
		CHECK(SG_WaterCapabilityBuild(&fixture.authority,
			ShallowGroundedPmove, &fixture.configuration, &fixture.semantics,
			fixture.phases, 2U, fixture.bindings, 2U, &capabilities, &error));
		if (capabilities)
		{
			CHECK(FindFact(capabilities, SG_WATER_CAPABILITY_CURRENT,
				1U, 1U, SG_WATER_DIRECTION_COMBINED) == NULL);
			SG_WaterCapabilityDestroy(capabilities);
		}

		fixture.regions[1].flags = SG_CONFIGURATION_SEMANTIC_REGION_WATER |
			SG_CONFIGURATION_SEMANTIC_REGION_SUPPORTED;
		fixture.authority.identity.physics.ground_acceleration = 7.0f;
		fixture.configuration.identity.physics.ground_acceleration = 7.0f;
		fixture.semantics.identity.physics.ground_acceleration = 7.0f;
		fixture.phases[1] = WaterFixturePhase(&fixture, 1U, 1U);
		capabilities = NULL;
		CHECK(SG_WaterCapabilityBuild(&fixture.authority,
			ShallowGroundedPmove, &fixture.configuration, &fixture.semantics,
			fixture.phases, 2U, fixture.bindings, 2U, &capabilities, &error));
		if (capabilities)
		{
			current = FindFact(capabilities, SG_WATER_CAPABILITY_CURRENT,
				1U, 1U, SG_WATER_DIRECTION_COMBINED);
			CHECK(current != NULL);
			CHECK(capabilities->wet_region_count == 1U);
			CHECK(FindFact(capabilities,
				SG_WATER_CAPABILITY_DIRECTIONAL_SWIM, 1U, 1U,
				SG_WATER_DIRECTION_POSITIVE_X) == NULL);
			if (current)
			{
				CHECK(current->observed_velocity.value[0] == deep_speed * 0.5f);
				CHECK(current->result_grounded == 1U);
				CHECK(current->result_water_level == 1U);
				CHECK(current->parameters.acceleration.max_value == 7.0f);
				CHECK(current->parameters.drag == 1.0f);
			}
			SG_WaterCapabilityDestroy(capabilities);
		}
	}
}

static void TestPortalAndSubmergedCorridor(void)
{
	water_fixture_t fixture;
	sg_water_capability_set_t *capabilities = NULL;
	sg_water_capability_error_t error;
	const sg_water_capability_fact_t *entry;

	CHECK(WaterFixtureInit(&fixture, SG_HOST_CONTENTS_WATER, 800.0f, 0, 1));
	CHECK(WaterFixtureBuild(&fixture, &capabilities, &error));
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
	CHECK(WaterFixtureBuild(&fixture, &capabilities, &error));
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
	CHECK(WaterFixtureBuild(&fixture, &capabilities, &error));
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
	CHECK(WaterFixtureBuild(&fixture, &capabilities, &error));
	if (capabilities)
	{
		CHECK(FindFact(capabilities, SG_WATER_CAPABILITY_VOLUME_CROSSING,
			0U, 1U, SG_WATER_DIRECTION_BOUNDARY) != NULL);
		SG_WaterCapabilityDestroy(capabilities);
	}
}

static void TestResultStanceAndMedium(void)
{
	water_fixture_t fixture;
	sg_water_capability_set_t *capabilities = NULL;
	sg_water_capability_error_t error;
	const sg_water_capability_fact_t *exit;

	CHECK(WaterFixtureInit(&fixture, SG_HOST_CONTENTS_WATER, 800.0f, 0, 1));
	fixture.leaves[0].contents = 0;
	fixture.leaves[1].contents = SG_HOST_CONTENTS_WATER;
	SetRegionWaterState(&fixture, 0U, SG_HOST_CONTENTS_WATER, 3U,
		SG_CONFIGURATION_SEMANTIC_REGION_WATER |
		SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE);
	SetRegionWaterState(&fixture, 1U, 0U, 0U,
		SG_CONFIGURATION_SEMANTIC_REGION_AIRBORNE);
	fixture.cells[0].stance = SG_RUNE_STANCE_CROUCHING;
	fixture.authority.identity.crouching_hull.maxs.value[2] = 0.0f;
	fixture.configuration.identity.crouching_hull.maxs.value[2] = 0.0f;
	fixture.semantics.identity.crouching_hull.maxs.value[2] = 0.0f;
	fixture.phases[0] = WaterFixturePhase(&fixture, 0U, 0U);
	fixture.phases[1] = WaterFixturePhase(&fixture, 1U, 1U);
	CHECK(SG_WaterCapabilityBuild(&fixture.authority, StandingPmove,
		&fixture.configuration, &fixture.semantics, fixture.phases, 2U,
		fixture.bindings, 2U, &capabilities, &error));
	if (capabilities)
	{
		exit = FindFact(capabilities, SG_WATER_CAPABILITY_EXIT, 0U, 1U,
			SG_WATER_DIRECTION_BOUNDARY);
		CHECK(exit != NULL);
		if (exit)
		{
			CHECK(fixture.phases[exit->source_phase].stance ==
				SG_RUNE_STANCE_CROUCHING);
			CHECK(fixture.phases[exit->destination_phase].stance ==
				SG_RUNE_STANCE_STANDING);
			CHECK((exit->result_pm_flags & PMF_DUCKED) == 0U);
			CHECK(exit->result_water_level == 0U);
			CHECK(exit->result_water_type == 0U);
			CHECK(exit->source_medium == SG_RUNE_MEDIUM_WATER);
			CHECK(exit->destination_medium == SG_RUNE_MEDIUM_DRY);
		}
		SG_WaterCapabilityDestroy(capabilities);
	}

	CHECK(WaterFixtureInit(&fixture, SG_HOST_CONTENTS_WATER, 800.0f, 0, 0));
	capabilities = NULL;
	CHECK(SG_WaterCapabilityBuild(&fixture.authority, WrongMediumPmove,
		&fixture.configuration, &fixture.semantics, fixture.phases, 2U,
		fixture.bindings, 2U, &capabilities, &error));
	if (capabilities)
	{
		CHECK(FindFact(capabilities, SG_WATER_CAPABILITY_ENTRY, 0U, 1U,
			SG_WATER_DIRECTION_BOUNDARY) == NULL);
		CHECK(FindFact(capabilities,
			SG_WATER_CAPABILITY_DIRECTIONAL_SWIM, 1U, 1U,
			SG_WATER_DIRECTION_POSITIVE_X) == NULL);
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
	CHECK(WaterFixtureBuild(&fixture, &capabilities, &error));
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
		bindings, REGIONS, &capabilities, &error));
	if (capabilities)
	{
		uint64_t rejected_all_pairs = (uint64_t)STRIPES * STRIPES;

		CHECK(capabilities->fact_count == STRIPES * 10U);
		CHECK(capabilities->wet_region_count == STRIPES);
		CHECK(capabilities->boundary_count == STRIPES);
		CHECK(capabilities->same_cell_candidate_pairs == STRIPES);
		CHECK(capabilities->same_cell_candidate_pairs <= STRIPES * 3U);
		CHECK(rejected_all_pairs > capabilities->same_cell_candidate_pairs);
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
	CHECK(WaterFixtureBuild(&fixture, &capabilities, &error));
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
	const sg_water_capability_fact_t *positive_z;

	CHECK(WaterFixtureInit(&fixture, SG_HOST_CONTENTS_WATER, 800.0f, 0, 0));
	fixture.phases[1].velocity.x.min_value = 80.0f;
	fixture.phases[1].velocity.x.max_value = 100.0f;
	fixture.phases[1].velocity.y.min_value = 0.0f;
	fixture.phases[1].velocity.y.max_value = 0.0f;
	fixture.phases[1].velocity.z.min_value = 0.0f;
	fixture.phases[1].velocity.z.max_value = 0.0f;
	CHECK(WaterFixtureBuild(&fixture, &capabilities, &error));
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
	CHECK(WaterFixtureBuild(&fixture, &capabilities, &error));
	if (capabilities)
	{
		CHECK(FindFact(capabilities, SG_WATER_CAPABILITY_DIRECTIONAL_SWIM,
			1U, 1U, SG_WATER_DIRECTION_POSITIVE_X) == NULL);
		positive_z = FindFact(capabilities,
			SG_WATER_CAPABILITY_DIRECTIONAL_SWIM, 1U, 1U,
			SG_WATER_DIRECTION_POSITIVE_Z);
		CHECK(positive_z != NULL);
		if (positive_z)
		{
			CHECK(positive_z->source_witness.value[0] == 0.25f);
			CHECK(positive_z->observed_displacement.value[0] == 0.0f);
		}
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
		bindings, 3U, &capabilities, &error));
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
		bindings, 3U, &capabilities, &error));
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
			CHECK(capabilities->facts[fact].source_velocity.value[0] == 0.0f);
			CHECK(capabilities->facts[fact].command_vector.value[0] == 1.0f);
		}
	CHECK(transitions == 1U);
	SG_WaterCapabilityDestroy(capabilities);
}

static sg_rune_phase_basis_t DuplicatePhase(
	const sg_rune_phase_basis_t *source, uint32_t order)
{
	sg_rune_phase_basis_t phase = *source;

	phase.order.source_index = order;
	phase.order.local_ordinal = order;
	phase.order.variant = order;
	phase.id.value = SG_RuneModelStableIdFromOrderKey(&phase.order);
	return phase;
}

static void TestAmbiguousLocalDestinationPhase(void)
{
	water_fixture_t fixture;
	sg_rune_phase_basis_t phases[3];
	sg_water_phase_binding_t bindings[3];
	sg_water_capability_set_t *capabilities = NULL;
	sg_water_capability_error_t error;

	CHECK(WaterFixtureInit(&fixture, SG_HOST_CONTENTS_WATER, 800.0f, 0, 0));
	phases[0] = fixture.phases[0];
	phases[1] = fixture.phases[1];
	phases[2] = DuplicatePhase(&fixture.phases[1], 2U);
	bindings[0] = fixture.bindings[0];
	bindings[1] = fixture.bindings[1];
	bindings[2] = fixture.bindings[1];
	bindings[2].phase = 2U;
	CHECK(!SG_WaterCapabilityBuild(&fixture.authority, WaterFixturePmove,
		&fixture.configuration, &fixture.semantics, phases, 3U,
		bindings, 3U, &capabilities, &error));
	CHECK(error.code == SG_WATER_CAPABILITY_ERROR_INVALID_PHASE);
	CHECK(capabilities == NULL);
}

static void TestAmbiguousBoundaryDestinationPhase(void)
{
	water_fixture_t fixture;
	sg_rune_phase_basis_t phases[3];
	sg_water_phase_binding_t bindings[3];
	sg_water_capability_set_t *capabilities = NULL;
	sg_water_capability_error_t error;

	CHECK(WaterFixtureInit(&fixture, SG_HOST_CONTENTS_WATER, 800.0f, 0, 0));
	phases[0] = fixture.phases[0];
	phases[1] = fixture.phases[1];
	phases[2] = DuplicatePhase(&fixture.phases[0], 2U);
	bindings[0] = fixture.bindings[0];
	bindings[1] = fixture.bindings[0];
	bindings[1].phase = 2U;
	bindings[2] = fixture.bindings[1];
	CHECK(!SG_WaterCapabilityBuild(&fixture.authority, WaterFixturePmove,
		&fixture.configuration, &fixture.semantics, phases, 3U,
		bindings, 3U, &capabilities, &error));
	CHECK(error.code == SG_WATER_CAPABILITY_ERROR_INVALID_PHASE);
	CHECK(capabilities == NULL);
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
	CHECK(WaterFixtureBuild(&fixture, &capabilities, &error));
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
	sg_water_capability_error_t error;

	CHECK(WaterFixtureInit(&fixture, SG_HOST_CONTENTS_WATER, 800.0f, 0, 0));
	memcpy(&before, &fixture, sizeof(before));
	CHECK(WaterFixtureBuild(&fixture, &capabilities, &error));
	CHECK(capabilities != NULL);
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
	SG_WaterCapabilityDestroy(capabilities);
	capabilities = NULL;
	fixture.configuration.identity.physics.gravity = 100.0f;
	CHECK(!WaterFixtureBuild(&fixture, &capabilities, &error));
	CHECK(error.code == SG_WATER_CAPABILITY_ERROR_INVALID_SOURCE);
	CHECK(capabilities == NULL);
	fixture.configuration.identity = fixture.authority.identity;
	fixture.semantics.identity = fixture.authority.identity;
	fixture.authority.identity.physics.gravity = 32768.0f;
	fixture.configuration.identity.physics.gravity = 32768.0f;
	fixture.semantics.identity.physics.gravity = 32768.0f;
	CHECK(!WaterFixtureBuild(&fixture, &capabilities, &error));
	CHECK(error.code == SG_WATER_CAPABILITY_ERROR_INVALID_SOURCE);
	CHECK(capabilities == NULL);
	fixture.authority.identity.physics.gravity = 100.5f;
	fixture.configuration.identity.physics.gravity = 100.5f;
	fixture.semantics.identity.physics.gravity = 100.5f;
	CHECK(!WaterFixtureBuild(&fixture, &capabilities, &error));
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
	CHECK(!WaterFixtureBuild(&fixture, &capabilities, &error));
	CHECK(error.code == SG_WATER_CAPABILITY_ERROR_INVALID_SOURCE);
	CHECK(capabilities == NULL);
	CHECK(WaterFixtureInit(&fixture, SG_HOST_CONTENTS_WATER, 800.0f, 0, 0));
	fixture.phases[1].medium = SG_RUNE_MEDIUM_LAVA;
	CHECK(!WaterFixtureBuild(&fixture, &capabilities, &error));
	CHECK(error.code == SG_WATER_CAPABILITY_ERROR_INVALID_PHASE);
	CHECK(capabilities == NULL);
}

static void SetPhysicsField(sg_rune_physics_parameters_t *physics,
	uint32_t field, float value)
{
	switch (field)
	{
	case 0U: physics->gravity = value; break;
	case 1U: physics->ground_acceleration = value; break;
	case 2U: physics->air_acceleration = value; break;
	case 3U: physics->water_acceleration = value; break;
	case 4U: physics->hook_acceleration = value; break;
	case 5U: physics->external_acceleration = value; break;
	case 6U: physics->water_drag = value; break;
	case 7U: physics->max_velocity = value; break;
	default: CHECK(0); break;
	}
}

static void TestPhysicsIdentityValidation(void)
{
	uint32_t field;
	uint32_t invalid;
	static const float invalid_values[2] = { -1.0f, NAN };

	for (field = 0U; field < 8U; field++)
		for (invalid = 0U; invalid < 2U; invalid++)
		{
			water_fixture_t fixture;
			sg_water_capability_set_t *capabilities = NULL;
			sg_water_capability_error_t error;

			CHECK(WaterFixtureInit(&fixture, SG_HOST_CONTENTS_WATER,
				800.0f, 0, 0));
			SetPhysicsField(&fixture.authority.identity.physics, field,
				invalid_values[invalid]);
			SetPhysicsField(&fixture.configuration.identity.physics, field,
				invalid_values[invalid]);
			SetPhysicsField(&fixture.semantics.identity.physics, field,
				invalid_values[invalid]);
			host_probe_calls = 0U;
			CHECK(!SG_WaterCapabilityBuild(&fixture.authority, CountingPmove,
				&fixture.configuration, &fixture.semantics, fixture.phases, 2U,
				fixture.bindings, 2U, &capabilities, &error));
			CHECK(error.code == SG_WATER_CAPABILITY_ERROR_INVALID_SOURCE);
			CHECK(capabilities == NULL);
			CHECK(host_probe_calls == 0U);
		}
}

static void TestSubstepByteBound(void)
{
	water_fixture_t fixture;
	sg_water_capability_set_t *capabilities = NULL;
	sg_water_capability_error_t error;

	CHECK(WaterFixtureInit(&fixture, SG_HOST_CONTENTS_WATER, 800.0f, 0, 0));
	fixture.authority.identity.physics.frame_ms = 512U;
	fixture.authority.identity.physics.substep_ms = 256U;
	fixture.configuration.identity.physics.frame_ms = 512U;
	fixture.configuration.identity.physics.substep_ms = 256U;
	fixture.semantics.identity.physics.frame_ms = 512U;
	fixture.semantics.identity.physics.substep_ms = 256U;
	CHECK(!WaterFixtureBuild(&fixture, &capabilities, &error));
	CHECK(error.code == SG_WATER_CAPABILITY_ERROR_INVALID_SOURCE);
	CHECK(capabilities == NULL);

	CHECK(WaterFixtureInit(&fixture, 0U, 800.0f, 0, 0));
	fixture.authority.identity.physics.frame_ms = 512U;
	fixture.authority.identity.physics.substep_ms = 256U;
	fixture.configuration.identity.physics.frame_ms = 512U;
	fixture.configuration.identity.physics.substep_ms = 256U;
	fixture.semantics.identity.physics.frame_ms = 512U;
	fixture.semantics.identity.physics.substep_ms = 256U;
	CHECK(!WaterFixtureBuild(&fixture, &capabilities, &error));
	CHECK(error.code == SG_WATER_CAPABILITY_ERROR_INVALID_SOURCE);
	CHECK(capabilities == NULL);
}

int main(void)
{
	TestVolumeMediumAndGravity(SG_HOST_CONTENTS_WATER,
		SG_RUNE_MEDIUM_WATER, 100.0f);
	TestVolumeMediumAndGravity(SG_HOST_CONTENTS_WATER,
		SG_RUNE_MEDIUM_WATER, 800.0f);
	TestVolumeMediumAndGravity(SG_HOST_CONTENTS_LAVA,
		SG_RUNE_MEDIUM_LAVA, 800.0f);
	TestVolumeMediumAndGravity(SG_HOST_CONTENTS_SLIME,
		SG_RUNE_MEDIUM_SLIME, 800.0f);
	TestSinkReplayCommand();
	TestCurrents();
	TestPortalAndSubmergedCorridor();
	TestResultStanceAndMedium();
	TestPartialBoundaryOverlap();
	TestHostileFaceMultiplicity();
	TestBlockedExit();
	TestPhaseVelocityAndNarrowVolume();
	TestDestinationPhaseSelection();
	TestSameRegionPhaseSelection();
	TestAmbiguousLocalDestinationPhase();
	TestAmbiguousBoundaryDestinationPhase();
	TestPlaneScaleInvariantContainment();
	TestFailureTransactionAndImmutability();
	TestMediaMismatch();
	TestPhysicsIdentityValidation();
	TestSubstepByteBound();
	if (failures)
	{
		fprintf(stderr, "sg_water_capability_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("sg_water_capability_test: ok");
	return 0;
}
