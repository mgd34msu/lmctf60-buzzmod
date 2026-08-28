#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "slipgate/sg_perception_evidence.h"

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

typedef struct perception_fixture_s
{
	sg_rune_phase_basis_t model_phases[3];
	sg_rune_cell_t cells[2];
	sg_rune_model_t model;
	sg_phase_coordinate_t phases[3];
	sg_rune_runtime_snapshot_t snapshot;
} perception_fixture_t;

_Static_assert(sizeof(sg_perception_hypothesis_t) == 76U,
	"hypothesis range regression assumes the public 76-byte layout");
_Static_assert(sizeof(sg_belief_evidence_support_t) == 72U,
	"support range regression assumes the public 72-byte layout");

static void FixtureInit(perception_fixture_t *fixture)
{
	memset(fixture, 0, sizeof(*fixture));
	fixture->model.version = SG_RUNE_MODEL_VERSION;
	fixture->model.schema_tag = SG_RUNE_MODEL_SCHEMA_TAG;
	fixture->model.flags = SG_RUNE_MODEL_IMMUTABLE |
		SG_RUNE_MODEL_EXACT_BOUND | SG_RUNE_MODEL_NO_RUNTIME_ACTORS;
	fixture->model.completeness.state = SG_RUNE_COMPLETENESS_COMPLETE;
	fixture->model.cell_count = 2U;
	fixture->model.phase_count = 3U;
	fixture->model.cells = fixture->cells;
	fixture->model.phases = fixture->model_phases;
	fixture->phases[0] = (sg_phase_coordinate_t){ 0U, 0U };
	fixture->phases[1] = (sg_phase_coordinate_t){ 1U, 0U };
	fixture->phases[2] = (sg_phase_coordinate_t){ 2U, 1U };
	fixture->snapshot.identity = 99U;
	fixture->snapshot.topology_revision = 7U;
	fixture->snapshot.cell_count = 2U;
	fixture->snapshot.phase_count = 3U;
	fixture->snapshot.region_count = 1U;
	fixture->snapshot.model = &fixture->model;
	fixture->snapshot.phases = fixture->phases;
	CHECK(SG_RuneRuntimeSnapshotValid(&fixture->snapshot));
}

static sg_perception_authentication_t Authentication(
	sg_perception_authority_t authority, uint64_t observed_at_ms,
	uint64_t authenticated_at_ms)
{
	sg_perception_authentication_t authentication;

	memset(&authentication, 0, sizeof(authentication));
	authentication.authenticated = 1U;
	authentication.authority = authority;
	authentication.issuer_team = 1U;
	authentication.audience_team = 1U;
	authentication.issuer_client = 4U;
	authentication.event_id = 1001U;
	authentication.evidence_sequence = 1U;
	authentication.observed_at_ms = observed_at_ms;
	authentication.authenticated_at_ms = authenticated_at_ms;
	authentication.valid_until_ms = authenticated_at_ms + 200U;
	authentication.rune_identity = 99U;
	authentication.topology_revision = 7U;
	return authentication;
}

static sg_perception_hypothesis_t Hypothesis(uint32_t phase, uint32_t cell,
	sg_perception_location_basis_t basis, float spread, float likelihood)
{
	sg_perception_hypothesis_t hypothesis;

	memset(&hypothesis, 0, sizeof(hypothesis));
	hypothesis.phase = (sg_phase_coordinate_t){ phase, cell };
	hypothesis.location_basis = basis;
	hypothesis.movement_state = SG_BELIEF_MOTION_GROUND;
	hypothesis.position[0] = (float)phase * 10.0f;
	hypothesis.velocity[0] = 1.0f;
	hypothesis.spread_radius = spread;
	hypothesis.likelihood = likelihood;
	return hypothesis;
}

static sg_perception_observation_t Observation(sg_perception_source_t source)
{
	sg_perception_observation_t observation;

	memset(&observation, 0, sizeof(observation));
	observation.authentication = Authentication(
		source == SG_PERCEPTION_SOURCE_TEAMMATE ?
			SG_PERCEPTION_AUTHORITY_HOST_TEAMMATE_REPORT :
			SG_PERCEPTION_AUTHORITY_HOST_SENSOR,
		100U, source == SG_PERCEPTION_SOURCE_TEAMMATE ? 150U : 100U);
	observation.source = source;
	observation.evidence_kind = SG_BELIEF_EVIDENCE_POSITIVE;
	observation.target_team = 2U;
	observation.target_client = 3U;
	observation.confidence = 0.8f;
	return observation;
}

static void SoundPayload(sg_perception_observation_t *observation,
	const sg_perception_hypothesis_t *hypotheses, size_t count)
{
	observation->data.sound.in_phs = 1U;
	observation->data.sound.positional = 1U;
	observation->data.sound.kind = SG_PERCEPTION_SOUND_WEAPON;
	observation->data.sound.sound_id = 17U;
	observation->data.sound.listener_position[0] = -20.0f;
	observation->data.sound.heard_origin[0] = 5.0f;
	observation->data.sound.attenuation = 1.0f;
	observation->data.sound.audible_radius = 800.0f;
	observation->data.sound.hypotheses = hypotheses;
	observation->data.sound.hypothesis_count = count;
}

static void DamagePayload(sg_perception_observation_t *observation,
	const sg_perception_hypothesis_t *hypotheses, size_t count)
{
	observation->data.damage.landed = 1U;
	observation->data.damage.damage = 40U;
	observation->data.damage.means_of_death = 7U;
	observation->data.damage.incoming_direction[0] = 1.0f;
	observation->data.damage.hypotheses = hypotheses;
	observation->data.damage.hypothesis_count = count;
}

static void UncertainPayload(sg_perception_observation_t *observation,
	sg_perception_source_t source,
	const sg_perception_hypothesis_t *hypotheses, size_t count)
{
	*observation = Observation(source);
	if (source == SG_PERCEPTION_SOURCE_SOUND)
		SoundPayload(observation, hypotheses, count);
	else
		DamagePayload(observation, hypotheses, count);
}

static void TestSightAndBorrowedLifetime(void)
{
	perception_fixture_t fixture;
	sg_perception_observation_t observation =
		Observation(SG_PERCEPTION_SOURCE_SIGHT);
	sg_belief_evidence_support_t storage[2];
	sg_belief_evidence_support_t storage_before[2];
	sg_perception_adaptation_t adaptation;
	sg_perception_hypothesis_t *hypothesis = &observation.data.sight.hypothesis;

	FixtureInit(&fixture);
	observation.data.sight.in_pvs = 1U;
	observation.data.sight.line_of_sight_proved = 1U;
	*hypothesis = Hypothesis(0U, 0U,
		SG_PERCEPTION_LOCATION_EARNED_RUNTIME, 0.0f, 1.0f);
	CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot, &observation, storage,
		2U, &adaptation) == SG_PERCEPTION_ADAPT_APPLIED);
	CHECK(adaptation.required_support_capacity == 1U);
	CHECK(adaptation.evidence.source == SG_BELIEF_SOURCE_SIGHT);
	CHECK(adaptation.evidence.supports == storage);
	CHECK(adaptation.evidence.support_count == 1U);
	CHECK(storage[0].phase.phase_id == 0U);
	CHECK(storage[0].spread_radius == 0.0f);
	hypothesis->position[0] = 999.0f;
	observation.authentication.event_id = 999U;
	CHECK(storage[0].position[0] == 0.0f);
	CHECK(adaptation.evidence.provenance.evidence_id == 1001U);
	CHECK(adaptation.evidence.provenance.rune_identity == fixture.snapshot.identity);
	CHECK(adaptation.evidence.provenance.topology_revision ==
		fixture.snapshot.topology_revision);

	*hypothesis = Hypothesis(0U, 0U,
		SG_PERCEPTION_LOCATION_EARNED_RUNTIME, 4.0f, 1.0f);
	memset(storage, 0xa5, sizeof(storage));
	memcpy(storage_before, storage, sizeof(storage));
	CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot, &observation, storage,
		2U, &adaptation) == SG_PERCEPTION_ADAPT_REJECTED_INVALID);
	CHECK(memcmp(storage, storage_before, sizeof(storage)) == 0);
	observation.evidence_kind = SG_BELIEF_EVIDENCE_NEGATIVE;
	CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot, &observation, storage,
		2U, &adaptation) == SG_PERCEPTION_ADAPT_APPLIED);
}

static void TestSoundAndDamageShape(void)
{
	perception_fixture_t fixture;
	sg_perception_hypothesis_t hypotheses[2];
	sg_belief_evidence_support_t storage[4];
	sg_belief_evidence_support_t before[4];
	sg_perception_observation_t observation;
	sg_perception_adaptation_t adaptation;

	FixtureInit(&fixture);
	hypotheses[0] = Hypothesis(0U, 0U,
		SG_PERCEPTION_LOCATION_EARNED_RUNTIME, 0.0f, 0.4f);
	hypotheses[1] = Hypothesis(1U, 0U,
		SG_PERCEPTION_LOCATION_EARNED_RUNTIME, 0.0f, 0.6f);
	observation = Observation(SG_PERCEPTION_SOURCE_SOUND);
	SoundPayload(&observation, hypotheses, 2U);
	CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot, &observation, storage,
		4U, &adaptation) == SG_PERCEPTION_ADAPT_APPLIED);
	CHECK(adaptation.evidence.source == SG_BELIEF_SOURCE_SOUND);
	CHECK(adaptation.evidence.support_count == 2U);

	SoundPayload(&observation, hypotheses, 1U);
	memset(storage, 0x6b, sizeof(storage));
	memcpy(before, storage, sizeof(storage));
	CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot, &observation, storage,
		4U, &adaptation) == SG_PERCEPTION_ADAPT_REJECTED_INVALID);
	CHECK(memcmp(storage, before, sizeof(storage)) == 0);
	hypotheses[1] = hypotheses[0];
	SoundPayload(&observation, hypotheses, 2U);
	CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot, &observation, storage,
		4U, &adaptation) == SG_PERCEPTION_ADAPT_REJECTED_INVALID);
	hypotheses[1].orientation[0] = 90.0f;
	CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot, &observation, storage,
		4U, &adaptation) == SG_PERCEPTION_ADAPT_REJECTED_INVALID);
	hypotheses[0].spread_radius = 96.0f;
	SoundPayload(&observation, hypotheses, 1U);
	CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot, &observation, storage,
		4U, &adaptation) == SG_PERCEPTION_ADAPT_APPLIED);

	observation = Observation(SG_PERCEPTION_SOURCE_DAMAGE);
	observation.data.damage.landed = 1U;
	observation.data.damage.damage = 40U;
	observation.data.damage.means_of_death = 7U;
	observation.data.damage.incoming_direction[0] = 1.0f;
	observation.data.damage.hypotheses = hypotheses;
	observation.data.damage.hypothesis_count = 1U;
	CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot, &observation, storage,
		4U, &adaptation) == SG_PERCEPTION_ADAPT_APPLIED);
	CHECK(adaptation.evidence.source == SG_BELIEF_SOURCE_DAMAGE);
	CHECK(storage[0].spread_radius == 96.0f);
	observation.data.damage.incoming_direction[0] = 0.0f;
	CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot, &observation, storage,
		4U, &adaptation) == SG_PERCEPTION_ADAPT_REJECTED_INVALID);
}

static void TestSoundDamageShapePermutationInvariant(void)
{
	static const sg_perception_source_t sources[] = {
		SG_PERCEPTION_SOURCE_SOUND,
		SG_PERCEPTION_SOURCE_DAMAGE
	};
	perception_fixture_t fixture;
	sg_perception_hypothesis_t hypotheses[2];
	sg_perception_hypothesis_t swap;
	sg_belief_evidence_support_t storage[2];
	sg_perception_observation_t observation;
	sg_perception_adaptation_t adaptation;
	size_t source_index;
	uint8_t ordering;

	FixtureInit(&fixture);
	for (source_index = 0U; source_index < sizeof(sources) /
	    sizeof(sources[0]); source_index++)
	{
		hypotheses[0] = Hypothesis(0U, 0U,
			SG_PERCEPTION_LOCATION_EARNED_RUNTIME, 0.0f, 0.5f);
		hypotheses[1] = Hypothesis(0U, 0U,
			SG_PERCEPTION_LOCATION_EARNED_RUNTIME, 64.0f, 0.5f);
		for (ordering = 0U; ordering < 2U; ordering++)
		{
			UncertainPayload(&observation, sources[source_index],
				hypotheses, 2U);
			CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot,
				&observation, storage, 2U, &adaptation) ==
				SG_PERCEPTION_ADAPT_REJECTED_INVALID);
			swap = hypotheses[0];
			hypotheses[0] = hypotheses[1];
			hypotheses[1] = swap;
		}

		hypotheses[0] = Hypothesis(0U, 0U,
			SG_PERCEPTION_LOCATION_EARNED_RUNTIME, 0.0f, 0.5f);
		hypotheses[1] = Hypothesis(1U, 0U,
			SG_PERCEPTION_LOCATION_EARNED_RUNTIME, 0.0f, 0.5f);
		for (ordering = 0U; ordering < 2U; ordering++)
		{
			UncertainPayload(&observation, sources[source_index],
				hypotheses, 2U);
			CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot,
				&observation, storage, 2U, &adaptation) ==
				SG_PERCEPTION_ADAPT_APPLIED);
			swap = hypotheses[0];
			hypotheses[0] = hypotheses[1];
			hypotheses[1] = swap;
		}
	}
}

static void TestItemAndFlagStaticRuneLocations(void)
{
	static const struct
	{
		sg_perception_flag_occurrence_t occurrence;
		sg_destination_flag_location_t location;
		int accepted;
	} flag_cases[] = {
		{ SG_PERCEPTION_FLAG_TARGET_PICKUP,
			SG_DESTINATION_FLAG_HOME, 1 },
		{ SG_PERCEPTION_FLAG_TARGET_PICKUP,
			SG_DESTINATION_FLAG_CURRENT, 0 },
		{ SG_PERCEPTION_FLAG_TARGET_DROP,
			SG_DESTINATION_FLAG_CURRENT, 0 },
		{ SG_PERCEPTION_FLAG_TARGET_DROP,
			SG_DESTINATION_FLAG_HOME, 0 },
		{ SG_PERCEPTION_FLAG_TARGET_CARRY_SIGHTED,
			SG_DESTINATION_FLAG_CURRENT, 0 }
	};
	perception_fixture_t fixture;
	sg_perception_hypothesis_t static_location = Hypothesis(2U, 1U,
		SG_PERCEPTION_LOCATION_RUNE_STATIC, 0.0f, 1.0f);
	sg_belief_evidence_support_t storage[2];
	sg_belief_evidence_support_t storage_before[2];
	sg_perception_observation_t observation;
	sg_perception_adaptation_t adaptation;
	size_t index;

	FixtureInit(&fixture);
	observation = Observation(SG_PERCEPTION_SOURCE_ITEM);
	observation.data.item.occurrence = SG_PERCEPTION_ITEM_TARGET_PICKUP;
	observation.data.item.destination.kind = SG_DESTINATION_POWERUP;
	observation.data.item.destination.value.item.item_id = 11U;
	observation.data.item.hypotheses = &static_location;
	observation.data.item.hypothesis_count = 1U;
	CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot, &observation, storage,
		2U, &adaptation) == SG_PERCEPTION_ADAPT_APPLIED);
	CHECK(adaptation.evidence.source == SG_BELIEF_SOURCE_ITEM);
	CHECK(storage[0].phase.phase_id == 2U);
	observation.evidence_kind = SG_BELIEF_EVIDENCE_NEGATIVE;
	CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot, &observation, storage,
		2U, &adaptation) == SG_PERCEPTION_ADAPT_REJECTED_INVALID);

	observation = Observation(SG_PERCEPTION_SOURCE_FLAG);
	observation.data.flag.destination.kind = SG_DESTINATION_FLAG;
	observation.data.flag.destination.value.flag.team = 1U;
	observation.data.flag.hypotheses = &static_location;
	observation.data.flag.hypothesis_count = 1U;
	for (index = 0U; index < sizeof(flag_cases) / sizeof(flag_cases[0]);
	    index++)
	{
		observation.data.flag.occurrence = flag_cases[index].occurrence;
		observation.data.flag.destination.value.flag.location =
			(uint8_t)flag_cases[index].location;
		memset(storage, 0xb4, sizeof(storage));
		memcpy(storage_before, storage, sizeof(storage));
		CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot, &observation,
			storage, 2U, &adaptation) ==
			(flag_cases[index].accepted ? SG_PERCEPTION_ADAPT_APPLIED :
			 SG_PERCEPTION_ADAPT_REJECTED_INVALID));
		if (flag_cases[index].accepted)
			CHECK(adaptation.evidence.source == SG_BELIEF_SOURCE_FLAG);
		else
			CHECK(memcmp(storage, storage_before, sizeof(storage)) == 0);
	}
	static_location.location_basis = SG_PERCEPTION_LOCATION_EARNED_RUNTIME;
	observation.data.flag.occurrence =
		SG_PERCEPTION_FLAG_TARGET_CARRY_SIGHTED;
	observation.data.flag.destination.value.flag.location =
		SG_DESTINATION_FLAG_CURRENT;
	CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot, &observation, storage,
		2U, &adaptation) == SG_PERCEPTION_ADAPT_APPLIED);
}

static void TestDelayedTeammateFeedsReducer(void)
{
	perception_fixture_t fixture;
	sg_perception_hypothesis_t hypotheses[2];
	sg_belief_evidence_support_t support_storage[4];
	sg_perception_observation_t observation =
		Observation(SG_PERCEPTION_SOURCE_TEAMMATE);
	sg_perception_adaptation_t adaptation;
	sg_belief_particle_t particles[8];
	sg_belief_particle_t scratch_first[8];
	sg_belief_particle_t scratch_second[8];
	sg_belief_state_t state;
	sg_belief_state_config_t config;
	sg_belief_frame_t frame;
	sg_belief_reduction_t reduction;

	FixtureInit(&fixture);
	hypotheses[0] = Hypothesis(0U, 0U,
		SG_PERCEPTION_LOCATION_EARNED_RUNTIME, 48.0f, 0.5f);
	hypotheses[1] = Hypothesis(1U, 0U,
		SG_PERCEPTION_LOCATION_EARNED_RUNTIME, 48.0f, 0.5f);
	observation.authentication.valid_until_ms = 250U;
	observation.data.teammate.reported_source = SG_PERCEPTION_SOURCE_SOUND;
	observation.data.teammate.report_kind = 17U;
	observation.data.teammate.hypotheses = hypotheses;
	observation.data.teammate.hypothesis_count = 2U;
	CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot, &observation,
		support_storage, 4U, &adaptation) == SG_PERCEPTION_ADAPT_APPLIED);
	CHECK(adaptation.evidence.source == SG_BELIEF_SOURCE_TEAMMATE);
	CHECK(adaptation.evidence.observed_at_ms == 100U);
	CHECK(adaptation.evidence.provenance.authenticated_at_ms == 150U);
	CHECK(adaptation.evidence.provenance.issuer_kind ==
		SG_BELIEF_ISSUER_TEAMMATE);

	memset(&config, 0, sizeof(config));
	config.audience_team = 1U;
	config.target_team = 2U;
	config.target_client = 3U;
	config.initialized_at_ms = 100U;
	config.policy.confidence_decay_ms = 1000U;
	config.policy.diffusion_fraction = 0.5f;
	config.policy.spread_growth_per_ms = 0.01f;
	CHECK(SG_BeliefStateInit(&fixture.snapshot, &state, &config, particles, 8U));
	memset(&frame, 0, sizeof(frame));
	frame.sequence = 1U;
	frame.expected_revision = state.revision;
	frame.expected_generation = state.generation;
	frame.at_ms = 200U;
	frame.evidence = &adaptation.evidence;
	frame.evidence_count = 1U;
	frame.scratch_first = scratch_first;
	frame.scratch_second = scratch_second;
	frame.scratch_capacity = 8U;
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_APPLIED);
	CHECK(state.particle_count == 2U);
	CHECK(fabsf(state.confidence - 0.8f * expf(-0.1f)) < 0.0001f);
}

static void TestTeammateStaticLocationLaundering(void)
{
	static const sg_perception_source_t reported_sources[] = {
		SG_PERCEPTION_SOURCE_FLAG,
		SG_PERCEPTION_SOURCE_ITEM,
		SG_PERCEPTION_SOURCE_SIGHT,
		SG_PERCEPTION_SOURCE_SOUND
	};
	perception_fixture_t fixture;
	sg_perception_hypothesis_t hypothesis = Hypothesis(1U, 0U,
		SG_PERCEPTION_LOCATION_RUNE_STATIC, 48.0f, 1.0f);
	sg_belief_evidence_support_t storage[2];
	sg_belief_evidence_support_t before[2];
	sg_perception_observation_t observation;
	sg_perception_adaptation_t adaptation;
	size_t index;

	FixtureInit(&fixture);
	for (index = 0U; index < sizeof(reported_sources) /
	    sizeof(reported_sources[0]); index++)
	{
		observation = Observation(SG_PERCEPTION_SOURCE_TEAMMATE);
		observation.data.teammate.reported_source =
			reported_sources[index];
		observation.data.teammate.report_kind = 1U;
		observation.data.teammate.hypotheses = &hypothesis;
		observation.data.teammate.hypothesis_count = 1U;
		if (reported_sources[index] == SG_PERCEPTION_SOURCE_FLAG)
		{
			observation.data.teammate.reported_destination.kind =
				SG_DESTINATION_FLAG;
			observation.data.teammate.reported_destination.value.flag.team =
				1U;
			observation.data.teammate.reported_destination.value.flag.location =
				SG_DESTINATION_FLAG_CURRENT;
		}
		else if (reported_sources[index] == SG_PERCEPTION_SOURCE_ITEM)
		{
			observation.data.teammate.reported_destination.kind =
				SG_DESTINATION_ARMOR;
			observation.data.teammate.reported_destination.value.item.item_id =
				72U;
		}
		else if (reported_sources[index] == SG_PERCEPTION_SOURCE_SIGHT)
		{
			hypothesis.spread_radius = 0.0f;
		}
		else
		{
			hypothesis.spread_radius = 48.0f;
		}
		memset(storage, 0x92, sizeof(storage));
		memcpy(before, storage, sizeof(storage));
		CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot, &observation,
			storage, 2U, &adaptation) ==
			SG_PERCEPTION_ADAPT_REJECTED_INVALID);
		CHECK(memcmp(storage, before, sizeof(storage)) == 0);
		hypothesis.location_basis =
			SG_PERCEPTION_LOCATION_EARNED_RUNTIME;
		CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot, &observation,
			storage, 2U, &adaptation) == SG_PERCEPTION_ADAPT_APPLIED);
		hypothesis.location_basis = SG_PERCEPTION_LOCATION_RUNE_STATIC;
	}
}

static void TestAliasAndRangeRejection(void)
{
	perception_fixture_t fixture;
	sg_perception_observation_t observation =
		Observation(SG_PERCEPTION_SOURCE_SIGHT);
	sg_perception_observation_t observation_before;
	sg_perception_hypothesis_t hypothesis;
	sg_belief_evidence_support_t storage[2];
	sg_belief_evidence_support_t storage_before[2];
	sg_perception_adaptation_t adaptation;
	sg_perception_adaptation_t adaptation_before;
	sg_belief_evidence_support_t *overlapping_support;
	sg_perception_adaptation_t *overlapping_output;
	uintptr_t overflowing_address;

	FixtureInit(&fixture);
	observation.data.sight.in_pvs = 1U;
	observation.data.sight.line_of_sight_proved = 1U;
	observation.data.sight.hypothesis = Hypothesis(0U, 0U,
		SG_PERCEPTION_LOCATION_EARNED_RUNTIME, 0.0f, 1.0f);
	memset(storage, 0x37, sizeof(storage));
	memcpy(storage_before, storage, sizeof(storage));
	observation_before = observation;
	overlapping_output = (sg_perception_adaptation_t *)(void *)&observation;
	CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot, &observation, storage,
		2U, overlapping_output) == SG_PERCEPTION_ADAPT_REJECTED_INVALID);
	CHECK(memcmp(storage, storage_before, sizeof(storage)) == 0);
	CHECK(memcmp(&observation, &observation_before, sizeof(observation)) == 0);

	overlapping_output = (sg_perception_adaptation_t *)(void *)&observation.data;
	CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot, &observation, storage,
		2U, overlapping_output) == SG_PERCEPTION_ADAPT_REJECTED_INVALID);
	CHECK(memcmp(storage, storage_before, sizeof(storage)) == 0);
	CHECK(memcmp(&observation, &observation_before, sizeof(observation)) == 0);

	memset(&adaptation, 0x64, sizeof(adaptation));
	adaptation_before = adaptation;
	overlapping_support =
		(sg_belief_evidence_support_t *)(void *)&observation;
	CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot, &observation,
		overlapping_support, 1U, &adaptation) ==
		SG_PERCEPTION_ADAPT_REJECTED_INVALID);
	CHECK(memcmp(&observation, &observation_before, sizeof(observation)) == 0);
	CHECK(memcmp(&adaptation, &adaptation_before, sizeof(adaptation)) == 0);

	overlapping_support =
		(sg_belief_evidence_support_t *)(void *)&observation.data;
	CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot, &observation,
		overlapping_support, 1U, &adaptation) ==
		SG_PERCEPTION_ADAPT_REJECTED_INVALID);
	CHECK(memcmp(&observation, &observation_before, sizeof(observation)) == 0);
	CHECK(memcmp(&adaptation, &adaptation_before, sizeof(adaptation)) == 0);

	overlapping_support =
		(sg_belief_evidence_support_t *)(void *)&adaptation;
	CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot, &observation,
		overlapping_support, 1U, &adaptation) ==
		SG_PERCEPTION_ADAPT_REJECTED_INVALID);
	CHECK(memcmp(&adaptation, &adaptation_before, sizeof(adaptation)) == 0);

	overlapping_support =
		(sg_belief_evidence_support_t *)(void *)&adaptation.evidence;
	CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot, &observation,
		overlapping_support, 1U, &adaptation) ==
		SG_PERCEPTION_ADAPT_REJECTED_INVALID);
	CHECK(memcmp(&adaptation, &adaptation_before, sizeof(adaptation)) == 0);

	CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot,
		(const sg_perception_observation_t *)(void *)(UINTPTR_MAX -
		 sizeof(observation) + 2U), storage, 2U, &adaptation) ==
		SG_PERCEPTION_ADAPT_REJECTED_INVALID);
	CHECK(memcmp(storage, storage_before, sizeof(storage)) == 0);
	CHECK(memcmp(&adaptation, &adaptation_before, sizeof(adaptation)) == 0);

	CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot, &observation, storage,
		2U, (sg_perception_adaptation_t *)(void *)(UINTPTR_MAX -
		 sizeof(adaptation) + 2U)) ==
		SG_PERCEPTION_ADAPT_REJECTED_INVALID);
	CHECK(memcmp(storage, storage_before, sizeof(storage)) == 0);
	CHECK(memcmp(&adaptation, &adaptation_before, sizeof(adaptation)) == 0);

	overflowing_address = UINTPTR_MAX - sizeof(*overlapping_support) + 2U;
	overlapping_support =
		(sg_belief_evidence_support_t *)(void *)overflowing_address;
	CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot, &observation,
		overlapping_support, 1U, &adaptation) ==
		SG_PERCEPTION_ADAPT_REJECTED_INVALID);
	CHECK(memcmp(&adaptation, &adaptation_before, sizeof(adaptation)) == 0);

	observation = Observation(SG_PERCEPTION_SOURCE_SOUND);
	observation.data.sound.in_phs = 1U;
	observation.data.sound.positional = 1U;
	observation.data.sound.kind = SG_PERCEPTION_SOUND_WEAPON;
	observation.data.sound.sound_id = 1U;
	observation.data.sound.attenuation = 1.0f;
	observation.data.sound.audible_radius = 100.0f;
	hypothesis = Hypothesis(0U, 0U,
		SG_PERCEPTION_LOCATION_EARNED_RUNTIME, 32.0f, 1.0f);
	observation.data.sound.hypotheses = &hypothesis;
	observation.data.sound.hypothesis_count =
		SIZE_MAX / sizeof(*overlapping_support) + 1U;
	CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot, &observation, storage,
		2U, &adaptation) == SG_PERCEPTION_ADAPT_OVERFLOW);
	CHECK(memcmp(storage, storage_before, sizeof(storage)) == 0);
	CHECK(memcmp(&adaptation, &adaptation_before, sizeof(adaptation)) == 0);
}

static void TestNestedHypothesisSpanAliasing(void)
{
	perception_fixture_t fixture;
	sg_perception_hypothesis_t hypotheses[3];
	sg_perception_hypothesis_t hypotheses_before[3];
	sg_perception_observation_t observation =
		Observation(SG_PERCEPTION_SOURCE_SOUND);
	sg_perception_observation_t observation_before;
	sg_belief_evidence_support_t storage[3];
	sg_belief_evidence_support_t storage_before[3];
	sg_perception_adaptation_t adaptation;
	sg_perception_adaptation_t adaptation_before;
	sg_belief_evidence_support_t *overlapping_support;
	const sg_perception_hypothesis_t *overlapping_hypotheses;
	size_t index;

	FixtureInit(&fixture);
	for (index = 0U; index < 3U; index++)
		hypotheses[index] = Hypothesis((uint32_t)index,
			index == 2U ? 1U : 0U,
			SG_PERCEPTION_LOCATION_EARNED_RUNTIME, 32.0f, 1.0f);
	SoundPayload(&observation, hypotheses, 3U);
	memset(&adaptation, 0x2d, sizeof(adaptation));
	adaptation_before = adaptation;
	memcpy(hypotheses_before, hypotheses, sizeof(hypotheses));
	overlapping_support =
		(sg_belief_evidence_support_t *)(void *)hypotheses;
	CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot, &observation,
		overlapping_support, 3U, &adaptation) ==
		SG_PERCEPTION_ADAPT_REJECTED_INVALID);
	CHECK(memcmp(hypotheses, hypotheses_before, sizeof(hypotheses)) == 0);
	CHECK(memcmp(&adaptation, &adaptation_before, sizeof(adaptation)) == 0);

	overlapping_support =
		(sg_belief_evidence_support_t *)(void *)&hypotheses[1];
	CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot, &observation,
		overlapping_support, 2U, &adaptation) ==
		SG_PERCEPTION_ADAPT_REJECTED_INVALID);
	CHECK(memcmp(hypotheses, hypotheses_before, sizeof(hypotheses)) == 0);
	CHECK(memcmp(&adaptation, &adaptation_before, sizeof(adaptation)) == 0);

	memset(storage, 0x81, sizeof(storage));
	memcpy(storage_before, storage, sizeof(storage));
	SoundPayload(&observation,
		(const sg_perception_hypothesis_t *)(void *)&adaptation, 1U);
	CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot, &observation, storage,
		3U, &adaptation) == SG_PERCEPTION_ADAPT_REJECTED_INVALID);
	CHECK(memcmp(storage, storage_before, sizeof(storage)) == 0);
	CHECK(memcmp(&adaptation, &adaptation_before, sizeof(adaptation)) == 0);

	SoundPayload(&observation,
		(const sg_perception_hypothesis_t *)(void *)&adaptation.evidence, 1U);
	CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot, &observation, storage,
		3U, &adaptation) == SG_PERCEPTION_ADAPT_REJECTED_INVALID);
	CHECK(memcmp(storage, storage_before, sizeof(storage)) == 0);
	CHECK(memcmp(&adaptation, &adaptation_before, sizeof(adaptation)) == 0);

	SoundPayload(&observation,
		(const sg_perception_hypothesis_t *)(void *)&observation, 1U);
	observation_before = observation;
	CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot, &observation, storage,
		3U, &adaptation) == SG_PERCEPTION_ADAPT_REJECTED_INVALID);
	CHECK(memcmp(&observation, &observation_before, sizeof(observation)) == 0);
	CHECK(memcmp(storage, storage_before, sizeof(storage)) == 0);
	CHECK(memcmp(&adaptation, &adaptation_before, sizeof(adaptation)) == 0);

	SoundPayload(&observation,
		(const sg_perception_hypothesis_t *)(void *)&observation.data, 1U);
	observation_before = observation;
	CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot, &observation, storage,
		3U, &adaptation) == SG_PERCEPTION_ADAPT_REJECTED_INVALID);
	CHECK(memcmp(&observation, &observation_before, sizeof(observation)) == 0);
	CHECK(memcmp(storage, storage_before, sizeof(storage)) == 0);
	CHECK(memcmp(&adaptation, &adaptation_before, sizeof(adaptation)) == 0);

	overlapping_hypotheses =
		(const sg_perception_hypothesis_t *)(void *)(UINTPTR_MAX -
		 sizeof(*overlapping_hypotheses) + 2U);
	SoundPayload(&observation, overlapping_hypotheses, 1U);
	CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot, &observation, storage,
		3U, &adaptation) == SG_PERCEPTION_ADAPT_REJECTED_INVALID);
	CHECK(memcmp(storage, storage_before, sizeof(storage)) == 0);
	CHECK(memcmp(&adaptation, &adaptation_before, sizeof(adaptation)) == 0);

	SoundPayload(&observation, hypotheses,
		SIZE_MAX / sizeof(*hypotheses) + 1U);
	CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot, &observation, storage,
		3U, &adaptation) == SG_PERCEPTION_ADAPT_OVERFLOW);
	CHECK(memcmp(hypotheses, hypotheses_before, sizeof(hypotheses)) == 0);
	CHECK(memcmp(storage, storage_before, sizeof(storage)) == 0);
	CHECK(memcmp(&adaptation, &adaptation_before, sizeof(adaptation)) == 0);
}

static void TestAuthorityValidationAndTransactionalFinalElement(void)
{
	perception_fixture_t fixture;
	sg_perception_hypothesis_t hypotheses[3];
	sg_belief_evidence_support_t storage[4];
	sg_belief_evidence_support_t before[4];
	sg_perception_observation_t observation =
		Observation(SG_PERCEPTION_SOURCE_SOUND);
	sg_perception_adaptation_t adaptation;

	FixtureInit(&fixture);
	hypotheses[0] = Hypothesis(0U, 0U,
		SG_PERCEPTION_LOCATION_EARNED_RUNTIME, 32.0f, 1.0f);
	hypotheses[1] = Hypothesis(1U, 0U,
		SG_PERCEPTION_LOCATION_EARNED_RUNTIME, 32.0f, 1.0f);
	hypotheses[2] = Hypothesis(99U, 0U,
		SG_PERCEPTION_LOCATION_EARNED_RUNTIME, 32.0f, 1.0f);
	SoundPayload(&observation, hypotheses, 3U);
	memset(storage, 0x3c, sizeof(storage));
	memcpy(before, storage, sizeof(storage));
	CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot, &observation, storage,
		4U, &adaptation) == SG_PERCEPTION_ADAPT_REJECTED_INVALID);
	CHECK(memcmp(storage, before, sizeof(storage)) == 0);

	observation.authentication.authority =
		SG_PERCEPTION_AUTHORITY_HOST_TEAMMATE_REPORT;
	CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot, &observation, storage,
		4U, &adaptation) == SG_PERCEPTION_ADAPT_REJECTED_AUTHORITY);
	observation.authentication.authenticated = 0U;
	CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot, &observation, storage,
		4U, &adaptation) == SG_PERCEPTION_ADAPT_REJECTED_AUTHORITY);
	observation = Observation(SG_PERCEPTION_SOURCE_TEAMMATE);
	observation.authentication.authority =
		SG_PERCEPTION_AUTHORITY_HOST_SENSOR;
	observation.data.teammate.reported_source = SG_PERCEPTION_SOURCE_SOUND;
	observation.data.teammate.report_kind = 1U;
	observation.data.teammate.hypotheses = hypotheses;
	observation.data.teammate.hypothesis_count = 3U;
	CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot, &observation, storage,
		4U, &adaptation) == SG_PERCEPTION_ADAPT_REJECTED_AUTHORITY);
	observation = Observation(SG_PERCEPTION_SOURCE_SOUND);
	SoundPayload(&observation, hypotheses, 3U);
	observation.authentication = Authentication(
		SG_PERCEPTION_AUTHORITY_HOST_SENSOR, 151U, 150U);
	CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot, &observation, storage,
		4U, &adaptation) == SG_PERCEPTION_ADAPT_REJECTED_AUTHORITY);
	observation.authentication = Authentication(
		SG_PERCEPTION_AUTHORITY_HOST_SENSOR, 100U, 100U);
	observation.authentication.rune_identity = 98U;
	CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot, &observation, storage,
		4U, &adaptation) == SG_PERCEPTION_ADAPT_REJECTED_AUTHORITY);
	observation.authentication = Authentication(
		SG_PERCEPTION_AUTHORITY_HOST_SENSOR, 100U, 100U);
	observation.authentication.topology_revision = 8U;
	CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot, &observation, storage,
		4U, &adaptation) == SG_PERCEPTION_ADAPT_REJECTED_AUTHORITY);
	observation.authentication = Authentication(
		SG_PERCEPTION_AUTHORITY_HOST_SENSOR, 100U, 100U);
	hypotheses[0] = Hypothesis(0U, 0U,
		SG_PERCEPTION_LOCATION_RUNE_STATIC, 32.0f, 1.0f);
	hypotheses[1] = Hypothesis(1U, 0U,
		SG_PERCEPTION_LOCATION_RUNE_STATIC, 32.0f, 1.0f);
	SoundPayload(&observation, hypotheses, 2U);
	CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot, &observation, storage,
		4U, &adaptation) == SG_PERCEPTION_ADAPT_REJECTED_INVALID);
	fixture.model_phases[2].motion = SG_RUNE_MOTION_AIRBORNE;
	observation = Observation(SG_PERCEPTION_SOURCE_SOUND);
	hypotheses[0] = Hypothesis(2U, 1U,
		SG_PERCEPTION_LOCATION_EARNED_RUNTIME, 32.0f, 1.0f);
	SoundPayload(&observation, hypotheses, 1U);
	CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot, &observation, storage,
		4U, &adaptation) == SG_PERCEPTION_ADAPT_REJECTED_INVALID);
}

static void TestUnboundedCapacityRetry(void)
{
	const size_t count = 300U;
	perception_fixture_t fixture;
	sg_perception_hypothesis_t *hypotheses = calloc(count,
		sizeof(*hypotheses));
	sg_belief_evidence_support_t small[8];
	sg_belief_evidence_support_t small_before[8];
	sg_belief_evidence_support_t *large = calloc(count, sizeof(*large));
	sg_perception_observation_t observation =
		Observation(SG_PERCEPTION_SOURCE_SOUND);
	sg_perception_adaptation_t adaptation;
	size_t index;

	FixtureInit(&fixture);
	CHECK(hypotheses && large);
	if (!hypotheses || !large)
		goto cleanup;
	for (index = 0U; index < count; index++)
	{
		hypotheses[index] = Hypothesis((uint32_t)(index % 3U),
			index % 3U == 2U ? 1U : 0U,
			SG_PERCEPTION_LOCATION_EARNED_RUNTIME, 32.0f, 1.0f);
		hypotheses[index].position[1] = (float)index;
	}
	SoundPayload(&observation, hypotheses, count);
	memset(small, 0x4d, sizeof(small));
	memcpy(small_before, small, sizeof(small));
	CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot, &observation, small,
		8U, &adaptation) == SG_PERCEPTION_ADAPT_CAPACITY);
	CHECK(adaptation.required_support_capacity == count);
	CHECK(memcmp(small, small_before, sizeof(small)) == 0);
	CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot, &observation, large,
		count, &adaptation) == SG_PERCEPTION_ADAPT_APPLIED);
	CHECK(adaptation.evidence.support_count == count);
	CHECK(large[count - 1U].position[1] == (float)(count - 1U));

cleanup:
	free(large);
	free(hypotheses);
}

static void FillPoisonedSight(sg_perception_observation_t *observation,
	unsigned char poison)
{
	memset(observation, poison, sizeof(*observation));
	observation->authentication = Authentication(
		SG_PERCEPTION_AUTHORITY_HOST_SENSOR, 100U, 100U);
	observation->source = SG_PERCEPTION_SOURCE_SIGHT;
	observation->evidence_kind = SG_BELIEF_EVIDENCE_POSITIVE;
	observation->target_team = 2U;
	observation->target_client = 3U;
	observation->reserved = 0U;
	observation->confidence = 0.8f;
	observation->data.sight.in_pvs = 1U;
	observation->data.sight.line_of_sight_proved = 1U;
	observation->data.sight.reserved[0] = 0U;
	observation->data.sight.reserved[1] = 0U;
	observation->data.sight.hypothesis = Hypothesis(0U, 0U,
		SG_PERCEPTION_LOCATION_EARNED_RUNTIME, 0.0f, 1.0f);
}

static void TestCanonicalOutputIgnoresInactiveUnionBytes(void)
{
	perception_fixture_t fixture;
	sg_perception_observation_t first;
	sg_perception_observation_t second;
	sg_belief_evidence_support_t storage[1];
	sg_belief_evidence_support_t support_before;
	sg_perception_adaptation_t adaptation;
	sg_perception_adaptation_t adaptation_before;

	FixtureInit(&fixture);
	FillPoisonedSight(&first, 0xa5U);
	FillPoisonedSight(&second, 0x5aU);
	CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot, &first, storage, 1U,
		&adaptation) == SG_PERCEPTION_ADAPT_APPLIED);
	adaptation_before = adaptation;
	support_before = storage[0];
	CHECK(SG_PerceptionEvidenceAdapt(&fixture.snapshot, &second, storage, 1U,
		&adaptation) == SG_PERCEPTION_ADAPT_APPLIED);
	CHECK(memcmp(&adaptation, &adaptation_before, sizeof(adaptation)) == 0);
	CHECK(memcmp(&storage[0], &support_before, sizeof(storage[0])) == 0);
}

int main(void)
{
	TestSightAndBorrowedLifetime();
	TestSoundAndDamageShape();
	TestSoundDamageShapePermutationInvariant();
	TestItemAndFlagStaticRuneLocations();
	TestDelayedTeammateFeedsReducer();
	TestTeammateStaticLocationLaundering();
	TestAliasAndRangeRejection();
	TestNestedHypothesisSpanAliasing();
	TestAuthorityValidationAndTransactionalFinalElement();
	TestUnboundedCapacityRetry();
	TestCanonicalOutputIgnoresInactiveUnionBytes();
	if (failures != 0)
	{
		fprintf(stderr, "sg_perception_evidence_test: %d failure(s)\n",
			failures);
		return 1;
	}
	puts("sg_perception_evidence_test: ok");
	return 0;
}
