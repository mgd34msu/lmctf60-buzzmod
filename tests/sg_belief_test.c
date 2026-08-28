#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "slipgate/sg_belief_contract.h"

static int failures;

_Static_assert(SG_BELIEF_SOURCE_COUNT == 6,
	"only sight, sound, damage, item, flag, and teammate evidence are admitted");

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

typedef struct belief_fixture_s
{
	sg_rune_phase_basis_t model_phases[3];
	sg_rune_phase_transition_t model_transitions[1];
	sg_rune_capability_kernel_t model_kernels[3];
	sg_rune_cell_t cells[2];
	sg_rune_model_t model;
	sg_phase_coordinate_t phases[3];
	sg_rune_runtime_snapshot_t snapshot;
} belief_fixture_t;

#define TEST_LARGE_PHASE_COUNT 70U

typedef struct large_belief_fixture_s
{
	sg_rune_phase_basis_t model_phases[TEST_LARGE_PHASE_COUNT];
	sg_rune_capability_kernel_t model_kernels[1];
	sg_rune_cell_t cells[2];
	sg_rune_model_t model;
	sg_phase_coordinate_t phases[TEST_LARGE_PHASE_COUNT];
	sg_rune_runtime_snapshot_t snapshot;
} large_belief_fixture_t;

static sg_rune_stable_id_t StableId(uint64_t low)
{
	return (sg_rune_stable_id_t){ 99U, 0U, low };
}

static sg_rune_interval_t Interval(float min_value, float max_value)
{
	return (sg_rune_interval_t){ min_value, max_value };
}

static void SetKernel(sg_rune_capability_kernel_t *kernel,
	const sg_rune_cell_t *from_cell, const sg_rune_phase_basis_t *from,
	const sg_rune_cell_t *to_cell, const sg_rune_phase_basis_t *to)
{
	memset(kernel, 0, sizeof(*kernel));
	kernel->source_cell.value = from_cell->id.value;
	kernel->destination_cell.value = to_cell->id.value;
	kernel->source_phase.value = from->id.value;
	kernel->destination_phase.value = to->id.value;
	kernel->family = SG_RUNE_CAPABILITY_CONTINUOUS_SUPPORT;
	kernel->parameters.displacement.x = Interval(0.0f, 300.0f);
	kernel->parameters.displacement.y = Interval(0.0f, 0.0f);
	kernel->parameters.displacement.z = Interval(0.0f, 0.0f);
	kernel->parameters.duration_ms = Interval(50.0f, 100.0f);
	kernel->flags = SG_RUNE_KERNEL_DIRECTIONAL | SG_RUNE_KERNEL_PHASE_AWARE |
		SG_RUNE_KERNEL_PROVEN;
}

static void BeliefFixtureInit(belief_fixture_t *fixture)
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
	fixture->model.kernel_count = 3U;
	fixture->model.kernels = fixture->model_kernels;
	fixture->cells[0].id.value = StableId(10U);
	fixture->cells[1].id.value = StableId(11U);
	fixture->model_phases[0].id.value = StableId(1U);
	fixture->model_phases[1].id.value = StableId(2U);
	fixture->model_phases[2].id.value = StableId(3U);
	SetKernel(&fixture->model_kernels[0], &fixture->cells[0],
		&fixture->model_phases[0], &fixture->cells[0],
		&fixture->model_phases[1]);
	SetKernel(&fixture->model_kernels[1], &fixture->cells[0],
		&fixture->model_phases[0], &fixture->cells[1],
		&fixture->model_phases[2]);
	SetKernel(&fixture->model_kernels[2], &fixture->cells[0],
		&fixture->model_phases[1], &fixture->cells[1],
		&fixture->model_phases[2]);
	fixture->phases[0] = (sg_phase_coordinate_t){ 0U, 0U };
	fixture->phases[1] = (sg_phase_coordinate_t){ 1U, 0U };
	fixture->phases[2] = (sg_phase_coordinate_t){ 2U, 1U };
	memset(&fixture->snapshot, 0, sizeof(fixture->snapshot));
	fixture->snapshot.identity = 99U;
	fixture->snapshot.topology_revision = 7U;
	fixture->snapshot.cell_count = 2U;
	fixture->snapshot.phase_count = 3U;
	fixture->snapshot.region_count = 1U;
	fixture->snapshot.model = &fixture->model;
	fixture->snapshot.phases = fixture->phases;
	CHECK(SG_RuneRuntimeSnapshotValid(&fixture->snapshot));
}

static void LargeBeliefFixtureInit(large_belief_fixture_t *fixture)
{
	size_t index;

	memset(fixture, 0, sizeof(*fixture));
	fixture->model.version = SG_RUNE_MODEL_VERSION;
	fixture->model.schema_tag = SG_RUNE_MODEL_SCHEMA_TAG;
	fixture->model.flags = SG_RUNE_MODEL_IMMUTABLE |
		SG_RUNE_MODEL_EXACT_BOUND | SG_RUNE_MODEL_NO_RUNTIME_ACTORS;
	fixture->model.completeness.state = SG_RUNE_COMPLETENESS_COMPLETE;
	fixture->model.cell_count = 2U;
	fixture->model.phase_count = TEST_LARGE_PHASE_COUNT;
	fixture->model.cells = fixture->cells;
	fixture->model.phases = fixture->model_phases;
	fixture->model.kernel_count = 1U;
	fixture->model.kernels = fixture->model_kernels;
	fixture->cells[0].id.value = StableId(10U);
	fixture->cells[1].id.value = StableId(11U);
	for (index = 0U; index < TEST_LARGE_PHASE_COUNT; index++)
	{
		fixture->model_phases[index].id.value = StableId(index + 1U);
		fixture->phases[index] = (sg_phase_coordinate_t){
			(uint32_t)index, (uint32_t)(index % 2U)
		};
	}
	SetKernel(&fixture->model_kernels[0], &fixture->cells[0],
		&fixture->model_phases[0], &fixture->cells[0],
		&fixture->model_phases[1]);
	fixture->snapshot.identity = 99U;
	fixture->snapshot.topology_revision = 7U;
	fixture->snapshot.cell_count = 2U;
	fixture->snapshot.phase_count = TEST_LARGE_PHASE_COUNT;
	fixture->snapshot.region_count = 1U;
	fixture->snapshot.model = &fixture->model;
	fixture->snapshot.phases = fixture->phases;
	CHECK(SG_RuneRuntimeSnapshotValid(&fixture->snapshot));
}

static sg_belief_state_config_t Config(uint8_t audience, uint8_t target,
	uint16_t client)
{
	sg_belief_state_config_t config;

	memset(&config, 0, sizeof(config));
	config.audience_team = audience;
	config.target_team = target;
	config.target_client = client;
	config.initialized_at_ms = 100U;
	config.policy.confidence_decay_ms = 1000U;
	config.policy.diffusion_fraction = 0.5f;
	config.policy.spread_growth_per_ms = 0.01f;
	return config;
}

static sg_belief_evidence_support_t Support(uint32_t phase, uint32_t cell,
	float likelihood)
{
	sg_belief_evidence_support_t support;

	memset(&support, 0, sizeof(support));
	support.phase = (sg_phase_coordinate_t){ phase, cell };
	support.movement_state = SG_BELIEF_MOTION_GROUND;
	support.position[0] = (float)phase * 10.0f;
	support.velocity[0] = 1.0f;
	support.likelihood = likelihood;
	return support;
}

static sg_belief_evidence_t Evidence(sg_belief_evidence_source_t source,
	uint64_t sequence, uint64_t at_ms,
	const sg_belief_evidence_support_t *supports, size_t support_count)
{
	sg_belief_evidence_t evidence;

	memset(&evidence, 0, sizeof(evidence));
	evidence.provenance.authenticated = 1U;
	evidence.provenance.issuer_kind = SG_BELIEF_ISSUER_LOCAL_SENSOR;
	evidence.provenance.issuer_team = 1U;
	evidence.provenance.audience_team = 1U;
	evidence.provenance.issuer_client = 1U;
	evidence.provenance.evidence_id = sequence + 100U;
	evidence.provenance.evidence_sequence = sequence;
	evidence.provenance.authenticated_at_ms = at_ms;
	evidence.provenance.rune_identity = 99U;
	evidence.provenance.topology_revision = 7U;
	evidence.source = source;
	evidence.kind = SG_BELIEF_EVIDENCE_POSITIVE;
	evidence.target_team = 2U;
	evidence.target_client = 3U;
	evidence.observed_at_ms = at_ms;
	evidence.valid_until_ms = at_ms + 100U;
	evidence.confidence = 1.0f;
	evidence.supports = supports;
	evidence.support_count = support_count;
	return evidence;
}

static int Near(float left, float right, float tolerance)
{
	float difference = left - right;
	if (difference < 0.0f)
		difference = -difference;
	return difference <= tolerance;
}

static sg_belief_frame_t Frame(uint64_t sequence, uint64_t revision,
	uint64_t at_ms, sg_belief_particle_t *first,
	sg_belief_particle_t *second, size_t capacity)
{
	sg_belief_frame_t frame;

	memset(&frame, 0, sizeof(frame));
	frame.sequence = sequence;
	frame.expected_revision = revision;
	frame.expected_generation = revision;
	frame.at_ms = at_ms;
	frame.scratch_first = first;
	frame.scratch_second = second;
	frame.scratch_capacity = capacity;
	return frame;
}

static void InitState(const belief_fixture_t *fixture, sg_belief_state_t *state,
	sg_belief_particle_t *storage, size_t capacity)
{
	sg_belief_state_config_t config = Config(1U, 2U, 3U);
	CHECK(SG_BeliefStateInit(&fixture->snapshot, state, &config, storage,
		capacity));
}

static void TestStateInitIsTransactional(void)
{
	belief_fixture_t fixture;
	sg_belief_state_t state;
	sg_belief_state_t before;
	sg_belief_particle_t storage[4];
	sg_belief_particle_t storage_before[4];
	sg_belief_state_config_t config = Config(1U, 2U, 3U);

	BeliefFixtureInit(&fixture);
	memset(&state, 0xa5, sizeof(state));
	memset(storage, 0x5a, sizeof(storage));
	before = state;
	memcpy(storage_before, storage, sizeof(storage));
	config.policy.confidence_decay_ms = 0U;
	CHECK(!SG_BeliefStateInit(&fixture.snapshot, &state, &config, storage, 4U));
	CHECK(memcmp(&state, &before, sizeof(state)) == 0);
	CHECK(memcmp(storage, storage_before, sizeof(storage)) == 0);
	config.policy = Config(1U, 2U, 3U).policy;
	CHECK(!SG_BeliefStateInit(&fixture.snapshot, &state, &config, storage,
		SIZE_MAX));
	CHECK(memcmp(&state, &before, sizeof(state)) == 0);
	CHECK(memcmp(storage, storage_before, sizeof(storage)) == 0);
}

static void TestStorageAliasingAndCountOverflow(void)
{
	belief_fixture_t fixture;
	sg_belief_particle_t storage[8];
	sg_belief_particle_t storage_before[8];
	sg_belief_particle_t scratch[8];
	sg_belief_state_t state;
	sg_belief_state_t before;
	sg_belief_evidence_support_t support = Support(0U, 0U, 1.0f);
	sg_belief_evidence_t evidence = Evidence(SG_BELIEF_SOURCE_SIGHT, 1U,
		100U, &support, 1U);
	sg_belief_frame_t frame;
	sg_belief_reduction_t reduction;
	sg_belief_prediction_t prediction;

	BeliefFixtureInit(&fixture);
	InitState(&fixture, &state, storage, 8U);
	frame = Frame(1U, state.revision, 100U, &storage[1], scratch, 7U);
	frame.evidence = &evidence;
	frame.evidence_count = 1U;
	before = state;
	memcpy(storage_before, storage, sizeof(storage));
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_REJECTED_INVALID);
	CHECK(memcmp(&state, &before, sizeof(state)) == 0);
	CHECK(memcmp(storage, storage_before, sizeof(storage)) == 0);
	frame = Frame(1U, state.revision, 100U, scratch, &scratch[4], 4U);
	frame.evidence = &evidence;
	frame.evidence_count = SIZE_MAX;
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_OVERFLOW);
	state.generation = UINT64_MAX;
	before = state;
	frame.evidence_count = 0U;
	frame.expected_generation = state.generation;
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_OVERFLOW);
	CHECK(memcmp(&state, &before, sizeof(state)) == 0);
	memset(&prediction, 0, sizeof(prediction));
	CHECK(!SG_BeliefPredict(&fixture.snapshot, &state, 100U, &storage[1], 7U,
		&prediction));
	CHECK(memcmp(&state, &before, sizeof(state)) == 0);
	CHECK(memcmp(storage, storage_before, sizeof(storage)) == 0);
}

static void TestSightConcentrates(void)
{
	belief_fixture_t fixture;
	sg_belief_particle_t storage[8];
	sg_belief_particle_t scratch_first[8];
	sg_belief_particle_t scratch_second[8];
	sg_belief_state_t state;
	sg_belief_state_config_t config = Config(1U, 2U, 3U);
	sg_belief_evidence_support_t support = Support(0U, 0U, 1.0f);
	sg_belief_evidence_t evidence = Evidence(SG_BELIEF_SOURCE_SIGHT, 1U,
		100U, &support, 1U);
	sg_belief_frame_t frame;
	sg_belief_reduction_t reduction;

	BeliefFixtureInit(&fixture);
	CHECK(SG_BeliefStateInit(&fixture.snapshot, &state, &config, storage,
		8U));
	memset(&frame, 0, sizeof(frame));
	frame.sequence = 1U;
	frame.expected_revision = state.revision;
	frame.expected_generation = state.generation;
	frame.at_ms = 100U;
	frame.evidence = &evidence;
	frame.evidence_count = 1U;
	frame.scratch_first = scratch_first;
	frame.scratch_second = scratch_second;
	frame.scratch_capacity = 8U;
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_APPLIED);
	CHECK(state.particle_count == 1U);
	CHECK(state.particles[0].phase.phase_id == 0U);
	CHECK(state.particles[0].weight == 1.0f);
	CHECK(state.confidence == 1.0f);
	CHECK(state.latest_source == SG_BELIEF_SOURCE_SIGHT);
	CHECK(state.latest_provenance.evidence_id == evidence.provenance.evidence_id);
	support.position[0] = 999.0f;
	evidence.provenance.evidence_id = 999U;
	CHECK(state.particles[0].position[0] == 0.0f);
	CHECK(state.latest_provenance.evidence_id == 101U);
}

static void TestCanonicalModeMerge(void)
{
	belief_fixture_t fixture;
	sg_belief_particle_t storage[8];
	sg_belief_particle_t scratch_first[8];
	sg_belief_particle_t scratch_second[8];
	sg_belief_state_t state;
	sg_belief_evidence_support_t support = Support(0U, 0U, 1.0f);
	sg_belief_evidence_t evidence[2];
	sg_belief_frame_t frame;
	sg_belief_reduction_t reduction;

	BeliefFixtureInit(&fixture);
	InitState(&fixture, &state, storage, 8U);
	evidence[0] = Evidence(SG_BELIEF_SOURCE_ITEM, 1U, 100U, &support, 1U);
	evidence[0].confidence = 0.5f;
	evidence[1] = Evidence(SG_BELIEF_SOURCE_ITEM, 2U, 100U, &support, 1U);
	evidence[1].confidence = 0.5f;
	frame = Frame(1U, state.revision, 100U, scratch_first, scratch_second, 8U);
	frame.evidence = evidence;
	frame.evidence_count = 2U;
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_APPLIED);
	CHECK(state.particle_count == 1U);
	CHECK(state.particles[0].weight == 1.0f);
	CHECK(state.particles[0].latest_evidence_id == 102U);
}

static void TestAllEarnedSourcesAndTeamAuthority(void)
{
	belief_fixture_t fixture;
	sg_belief_particle_t storage[32];
	sg_belief_particle_t scratch_first[32];
	sg_belief_particle_t scratch_second[32];
	sg_belief_state_t state;
	sg_belief_evidence_support_t supports[2];
	sg_belief_evidence_t evidence;
	sg_belief_frame_t frame;
	sg_belief_reduction_t reduction;
	uint64_t sequence;

	BeliefFixtureInit(&fixture);
	InitState(&fixture, &state, storage, 32U);
	supports[0] = Support(0U, 0U, 0.5f);
	supports[1] = Support(1U, 0U, 0.5f);
	for (sequence = 1U; sequence <= (uint64_t)SG_BELIEF_SOURCE_COUNT;
	     sequence++)
	{
		sg_belief_evidence_source_t source =
			(sg_belief_evidence_source_t)(sequence - 1U);
		size_t count = (source == SG_BELIEF_SOURCE_SOUND ||
			source == SG_BELIEF_SOURCE_DAMAGE) ? 2U : 1U;
		evidence = Evidence(source, sequence, 99U + sequence, supports, count);
		evidence.confidence = 0.25f;
		if (source == SG_BELIEF_SOURCE_TEAMMATE)
		{
			evidence.provenance.issuer_kind = SG_BELIEF_ISSUER_TEAMMATE;
			evidence.provenance.issuer_client = 7U;
		}
		frame = Frame(sequence, state.revision, 99U + sequence,
			scratch_first, scratch_second, 32U);
		frame.evidence = &evidence;
		frame.evidence_count = 1U;
		CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame,
			&reduction) == SG_BELIEF_REDUCE_APPLIED);
	}
	CHECK(state.last_evidence_sequence == 6U);
	CHECK(state.latest_source == SG_BELIEF_SOURCE_TEAMMATE);
	CHECK(state.latest_provenance.issuer_kind == SG_BELIEF_ISSUER_TEAMMATE);
	CHECK(state.particle_count == 8U);

	evidence = Evidence(SG_BELIEF_SOURCE_TEAMMATE, 7U, 106U, supports, 1U);
	frame = Frame(7U, state.revision, 106U, scratch_first, scratch_second, 32U);
	frame.evidence = &evidence;
	frame.evidence_count = 1U;
	{
		sg_belief_state_t before = state;
		sg_belief_particle_t particles_before[32];
		memcpy(particles_before, storage, sizeof(storage));
		CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame,
			&reduction) == SG_BELIEF_REDUCE_REJECTED_AUTHORITY);
		CHECK(memcmp(&state, &before, sizeof(state)) == 0);
		CHECK(memcmp(storage, particles_before, sizeof(storage)) == 0);
	}
}

static void TestSoundDiffusionAndNegativeSight(void)
{
	belief_fixture_t fixture;
	sg_belief_particle_t storage[8];
	sg_belief_particle_t scratch_first[8];
	sg_belief_particle_t scratch_second[8];
	sg_belief_state_t state;
	sg_belief_evidence_support_t supports[2];
	sg_belief_evidence_t evidence;
	sg_belief_frame_t frame;
	sg_belief_reduction_t reduction;

	BeliefFixtureInit(&fixture);
	InitState(&fixture, &state, storage, 8U);
	supports[0] = Support(0U, 0U, 1.0f);
	supports[1] = Support(1U, 0U, 3.0f);
	evidence = Evidence(SG_BELIEF_SOURCE_SOUND, 1U, 100U, supports, 2U);
	frame = Frame(1U, state.revision, 100U, scratch_first, scratch_second, 8U);
	frame.evidence = &evidence;
	frame.evidence_count = 1U;
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_APPLIED);
	CHECK(state.particle_count == 2U);
	CHECK(Near(state.particles[0].weight, 0.25f, 0.00001f));
	CHECK(Near(state.particles[1].weight, 0.75f, 0.00001f));

	supports[0] = Support(0U, 0U, 1.0f);
	supports[0].position[0] = 0.01f;
	evidence = Evidence(SG_BELIEF_SOURCE_SIGHT, 2U, 110U, supports, 1U);
	evidence.kind = SG_BELIEF_EVIDENCE_NEGATIVE;
	frame = Frame(2U, state.revision, 110U, scratch_first, scratch_second, 8U);
	frame.evidence = &evidence;
	frame.evidence_count = 1U;
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_APPLIED);
	CHECK(state.particle_count == 1U);
	CHECK(state.particles[0].phase.phase_id == 1U);
	CHECK(state.particles[0].weight == 1.0f);

	supports[0] = Support(1U, 0U, 1.0f);
	supports[0].position[0] = 10.02f;
	evidence = Evidence(SG_BELIEF_SOURCE_SIGHT, 3U, 120U, supports, 1U);
	evidence.kind = SG_BELIEF_EVIDENCE_NEGATIVE;
	frame = Frame(3U, state.revision, 120U, scratch_first, scratch_second, 8U);
	frame.evidence = &evidence;
	frame.evidence_count = 1U;
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_APPLIED);
	CHECK(state.particle_count == 0U);
	CHECK(state.confidence == 0.0f);
	CHECK(state.total_weight == 0.0f);
}

static void TestNegativeSightUsesSpatialOverlap(void)
{
	belief_fixture_t fixture;
	sg_belief_particle_t storage[8];
	sg_belief_particle_t scratch_first[8];
	sg_belief_particle_t scratch_second[8];
	sg_belief_state_t state;
	sg_belief_evidence_support_t supports[2];
	sg_belief_evidence_t evidence;
	sg_belief_frame_t frame;
	sg_belief_reduction_t reduction;

	BeliefFixtureInit(&fixture);
	InitState(&fixture, &state, storage, 8U);
	supports[0] = Support(0U, 0U, 1.0f);
	supports[1] = Support(0U, 0U, 1.0f);
	supports[1].position[0] = 10.0f;
	evidence = Evidence(SG_BELIEF_SOURCE_ITEM, 1U, 100U, supports, 2U);
	frame = Frame(1U, state.revision, 100U, scratch_first, scratch_second, 8U);
	frame.evidence = &evidence;
	frame.evidence_count = 1U;
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_APPLIED);
	CHECK(state.particle_count == 2U);

	supports[0] = Support(0U, 0U, 1.0f);
	supports[0].spread_radius = 1.0f;
	evidence = Evidence(SG_BELIEF_SOURCE_SIGHT, 2U, 100U, supports, 1U);
	evidence.kind = SG_BELIEF_EVIDENCE_NEGATIVE;
	evidence.confidence = 0.5f;
	frame = Frame(2U, state.revision, 100U, scratch_first, scratch_second, 8U);
	frame.evidence = &evidence;
	frame.evidence_count = 1U;
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_APPLIED);
	CHECK(state.particle_count == 2U);
	CHECK(Near(state.particles[0].weight, 1.0f / 3.0f, 0.00001f));
	CHECK(Near(state.particles[1].weight, 2.0f / 3.0f, 0.00001f));

	evidence.provenance.evidence_sequence = 3U;
	evidence.provenance.evidence_id = 103U;
	evidence.confidence = 1.0f;
	frame = Frame(3U, state.revision, 100U, scratch_first, scratch_second, 8U);
	frame.evidence = &evidence;
	frame.evidence_count = 1U;
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_APPLIED);
	CHECK(state.particle_count == 1U);
	CHECK(state.particles[0].phase.phase_id == 0U);
	CHECK(state.particles[0].position[0] == 10.0f);
}

static sg_belief_horizon_entry_t HorizonEntry(uint32_t from_phase,
	uint32_t from_cell, uint32_t to_phase, uint32_t to_cell,
	float displacement, float likelihood)
{
	sg_belief_horizon_entry_t entry;

	memset(&entry, 0, sizeof(entry));
	entry.from = (sg_phase_coordinate_t){ from_phase, from_cell };
	entry.to = (sg_phase_coordinate_t){ to_phase, to_cell };
	entry.displacement[0] = displacement;
	entry.likelihood = likelihood;
	return entry;
}

static sg_belief_horizon_step_t HorizonCapabilityStep(uint32_t from_phase,
	uint32_t from_cell, uint32_t to_phase, uint32_t to_cell,
	uint32_t record_index)
{
	sg_belief_horizon_step_t step;

	memset(&step, 0, sizeof(step));
	step.from = (sg_phase_coordinate_t){ from_phase, from_cell };
	step.to = (sg_phase_coordinate_t){ to_phase, to_cell };
	step.kind = SG_BELIEF_HORIZON_CAPABILITY_KERNEL;
	step.record_index = record_index;
	return step;
}

static sg_belief_horizon_kernel_t HorizonKernel(uint64_t from_time_ms,
	uint64_t to_time_ms, const sg_belief_horizon_entry_t *entries,
	size_t entry_count, sg_belief_horizon_span_t *spans, size_t phase_count,
	const sg_belief_horizon_step_t *steps, size_t step_count)
{
	sg_belief_horizon_kernel_t kernel;
	size_t phase;
	size_t cursor = 0U;

	memset(&kernel, 0, sizeof(kernel));
	for (phase = 0U; phase < phase_count; phase++)
	{
		spans[phase].first_entry = cursor;
		while (cursor < entry_count &&
		       entries[cursor].from.phase_id == phase)
			cursor++;
		spans[phase].entry_count = cursor - spans[phase].first_entry;
	}
	kernel.rune_identity = 99U;
	kernel.topology_revision = 7U;
	kernel.from_time_ms = from_time_ms;
	kernel.to_time_ms = to_time_ms;
	kernel.host_complete = 1U;
	kernel.origin_spans = spans;
	kernel.origin_span_count = phase_count;
	kernel.entries = entries;
	kernel.entry_count = entry_count;
	kernel.steps = steps;
	kernel.step_count = step_count;
	return kernel;
}

static void TestSourceShapeGuards(void)
{
	belief_fixture_t fixture;
	sg_belief_particle_t storage[8];
	sg_belief_particle_t storage_before[8];
	sg_belief_particle_t scratch_first[8];
	sg_belief_particle_t scratch_second[8];
	sg_belief_state_t state;
	sg_belief_state_t before;
	sg_belief_evidence_support_t support = Support(0U, 0U, 1.0f);
	sg_belief_evidence_t evidence;
	sg_belief_frame_t frame;
	sg_belief_reduction_t reduction;

	BeliefFixtureInit(&fixture);
	InitState(&fixture, &state, storage, 8U);
	before = state;
	memcpy(storage_before, storage, sizeof(storage));
	evidence = Evidence(SG_BELIEF_SOURCE_SOUND, 1U, 100U, &support, 1U);
	frame = Frame(1U, state.revision, 100U, scratch_first, scratch_second, 8U);
	frame.evidence = &evidence;
	frame.evidence_count = 1U;
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_REJECTED_INVALID);
	evidence = Evidence(SG_BELIEF_SOURCE_DAMAGE, 1U, 100U, &support, 1U);
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_REJECTED_INVALID);
	CHECK(memcmp(&state, &before, sizeof(state)) == 0);
	CHECK(memcmp(storage, storage_before, sizeof(storage)) == 0);
	support.spread_radius = 64.0f;
	evidence = Evidence(SG_BELIEF_SOURCE_SOUND, 1U, 100U, &support, 1U);
	frame.evidence = &evidence;
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_APPLIED);
	CHECK(state.particle_count == 1U);
	CHECK(state.particles[0].spread_radius == 64.0f);
}

static void TestDirectUncertainEvidenceCannotCollapseToExactAim(void)
{
	belief_fixture_t fixture;
	sg_belief_particle_t storage[8];
	sg_belief_particle_t scratch_first[8];
	sg_belief_particle_t scratch_second[8];
	sg_belief_state_t state;
	sg_belief_state_t before;
	sg_belief_evidence_support_t supports[2];
	sg_belief_evidence_t evidence;
	sg_belief_frame_t frame;
	sg_belief_reduction_t reduction;

	BeliefFixtureInit(&fixture);
	InitState(&fixture, &state, storage, 8U);
	supports[0] = Support(0U, 0U, 0.5f);
	supports[1] = supports[0];
	evidence = Evidence(SG_BELIEF_SOURCE_SOUND, 1U, 100U, supports, 2U);
	frame = Frame(1U, state.revision, 100U, scratch_first, scratch_second, 8U);
	frame.evidence = &evidence;
	frame.evidence_count = 1U;
	before = state;
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_REJECTED_INVALID);
	CHECK(memcmp(&state, &before, sizeof(state)) == 0);
}

static void TestNegativeVisibilityUsesRegionUnion(void)
{
	belief_fixture_t fixture;
	sg_belief_particle_t storage[8];
	sg_belief_particle_t scratch_first[8];
	sg_belief_particle_t scratch_second[8];
	sg_belief_state_t state;
	sg_belief_evidence_support_t positive[2];
	sg_belief_evidence_support_t excluded[2];
	sg_belief_evidence_t evidence;
	sg_belief_frame_t frame;
	sg_belief_reduction_t reduction;

	BeliefFixtureInit(&fixture);
	InitState(&fixture, &state, storage, 8U);
	positive[0] = Support(0U, 0U, 1.0f);
	positive[1] = Support(0U, 0U, 1.0f);
	positive[1].position[0] = 10.0f;
	evidence = Evidence(SG_BELIEF_SOURCE_ITEM, 1U, 100U, positive, 2U);
	frame = Frame(1U, state.revision, 100U, scratch_first, scratch_second, 8U);
	frame.evidence = &evidence;
	frame.evidence_count = 1U;
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_APPLIED);
	excluded[0] = Support(0U, 0U, 1.0f);
	excluded[0].spread_radius = 1.0f;
	excluded[1] = excluded[0];
	evidence = Evidence(SG_BELIEF_SOURCE_SIGHT, 2U, 100U, excluded, 2U);
	evidence.kind = SG_BELIEF_EVIDENCE_NEGATIVE;
	evidence.confidence = 0.5f;
	frame = Frame(2U, state.revision, 100U, scratch_first, scratch_second, 8U);
	frame.evidence = &evidence;
	frame.evidence_count = 1U;
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_APPLIED);
	CHECK(Near(state.particles[0].weight, 1.0f / 3.0f, 0.00001f));
	CHECK(Near(state.particles[1].weight, 2.0f / 3.0f, 0.00001f));
}

static void TestIdentityGenerationAndMotionFailClosed(void)
{
	belief_fixture_t fixture;
	sg_belief_particle_t storage[8];
	sg_belief_particle_t scratch_first[8];
	sg_belief_particle_t scratch_second[8];
	sg_belief_state_t state;
	sg_belief_state_t before;
	sg_belief_evidence_support_t support = Support(0U, 0U, 1.0f);
	sg_belief_evidence_t evidence;
	sg_belief_frame_t frame;
	sg_belief_reduction_t reduction;

	BeliefFixtureInit(&fixture);
	InitState(&fixture, &state, storage, 8U);
	evidence = Evidence(SG_BELIEF_SOURCE_SIGHT, 1U, 100U, &support, 1U);
	evidence.provenance.rune_identity++;
	frame = Frame(1U, state.revision, 100U, scratch_first, scratch_second, 8U);
	frame.evidence = &evidence;
	frame.evidence_count = 1U;
	before = state;
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_REJECTED_AUTHORITY);
	CHECK(memcmp(&state, &before, sizeof(state)) == 0);
	evidence.provenance.rune_identity = fixture.snapshot.identity;
	evidence.provenance.topology_revision++;
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_REJECTED_AUTHORITY);
	evidence.provenance.topology_revision = fixture.snapshot.topology_revision;
	evidence.provenance.issuer_team = 2U;
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_REJECTED_AUTHORITY);
	evidence.provenance.issuer_team = 1U;
	evidence.provenance.audience_team = 2U;
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_REJECTED_AUTHORITY);
	evidence.provenance.audience_team = 1U;
	evidence.target_team = 1U;
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_REJECTED_INVALID);
	evidence.target_team = 2U;
	frame.evidence_count = 0U;
	frame.expected_generation++;
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_REJECTED_INVALID);
	frame.expected_generation = state.generation;
	fixture.model_phases[2].motion = SG_RUNE_MOTION_AIRBORNE;
	support = Support(2U, 1U, 1.0f);
	evidence = Evidence(SG_BELIEF_SOURCE_SIGHT, 1U, 100U, &support, 1U);
	frame.evidence = &evidence;
	frame.evidence_count = 1U;
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_REJECTED_INVALID);
}

static void TestHorizonCannotInventConnectivity(void)
{
	belief_fixture_t fixture;
	sg_belief_particle_t storage[8];
	sg_belief_particle_t scratch_first[8];
	sg_belief_particle_t scratch_second[8];
	sg_belief_state_t state;
	sg_belief_horizon_entry_t entries[3];
	sg_belief_horizon_span_t spans[3];
	sg_belief_horizon_kernel_t kernel;
	sg_belief_frame_t frame;
	sg_belief_reduction_t reduction;

	BeliefFixtureInit(&fixture);
	InitState(&fixture, &state, storage, 8U);
	entries[0] = HorizonEntry(0U, 0U, 0U, 0U, 0.0f, 1.0f);
	entries[1] = HorizonEntry(1U, 0U, 1U, 0U, 0.0f, 1.0f);
	entries[2] = HorizonEntry(2U, 1U, 0U, 0U, 0.0f, 1.0f);
	kernel = HorizonKernel(100U, 200U, entries, 3U, spans, 3U, NULL, 0U);
	frame = Frame(1U, state.revision, 200U, scratch_first, scratch_second, 8U);
	frame.kernels = &kernel;
	frame.kernel_count = 1U;
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_REJECTED_INVALID);
}

static void TestHorizonWitnessBoundsAndCells(void)
{
	belief_fixture_t fixture;
	sg_belief_particle_t storage[8];
	sg_belief_particle_t storage_before[8];
	sg_belief_particle_t scratch_first[8];
	sg_belief_particle_t scratch_second[8];
	sg_belief_state_t state;
	sg_belief_state_t before;
	sg_belief_horizon_entry_t entries[3];
	sg_belief_horizon_span_t spans[3];
	sg_belief_horizon_step_t step;
	sg_belief_horizon_kernel_t kernel;
	sg_belief_frame_t frame;
	sg_belief_reduction_t reduction;

	BeliefFixtureInit(&fixture);
	InitState(&fixture, &state, storage, 8U);
	entries[0] = HorizonEntry(0U, 0U, 1U, 0U, 10000.0f, 1.0f);
	entries[0].step_count = 1U;
	entries[1] = HorizonEntry(1U, 0U, 1U, 0U, 0.0f, 1.0f);
	entries[2] = HorizonEntry(2U, 1U, 2U, 1U, 0.0f, 1.0f);
	step = HorizonCapabilityStep(0U, 0U, 1U, 0U, 0U);
	kernel = HorizonKernel(100U, 200U, entries, 3U, spans, 3U, &step, 1U);
	frame = Frame(1U, state.revision, 200U, scratch_first, scratch_second, 8U);
	frame.kernels = &kernel;
	frame.kernel_count = 1U;
	before = state;
	memcpy(storage_before, storage, sizeof(storage));
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_REJECTED_INVALID);
	CHECK(memcmp(&state, &before, sizeof(state)) == 0);
	CHECK(memcmp(storage, storage_before, sizeof(storage)) == 0);

	entries[0].displacement[0] = 10.0f;
	kernel.to_time_ms = 300U;
	frame.at_ms = 300U;
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_REJECTED_INVALID);
	CHECK(memcmp(&state, &before, sizeof(state)) == 0);
	CHECK(memcmp(storage, storage_before, sizeof(storage)) == 0);

	kernel.to_time_ms = 200U;
	frame.at_ms = 200U;
	fixture.model_kernels[0].source_cell.value = fixture.cells[1].id.value;
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_REJECTED_INVALID);
	CHECK(memcmp(&state, &before, sizeof(state)) == 0);
	CHECK(memcmp(storage, storage_before, sizeof(storage)) == 0);

	fixture.model.phase_transition_count = 1U;
	fixture.model.phase_transitions = fixture.model_transitions;
	fixture.model_transitions[0].cell.value = fixture.cells[0].id.value;
	fixture.model_transitions[0].source_phase.value =
		fixture.model_phases[0].id.value;
	fixture.model_transitions[0].destination_phase.value =
		fixture.model_phases[1].id.value;
	fixture.model_transitions[0].kind = SG_RUNE_PHASE_TRANSITION_STANCE;
	fixture.model_transitions[0].duration_ms = Interval(100.0f, 100.0f);
	step.kind = SG_BELIEF_HORIZON_PHASE_TRANSITION;
	entries[0].displacement[0] = 1.0f;
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_REJECTED_INVALID);
	CHECK(memcmp(&state, &before, sizeof(state)) == 0);
	CHECK(memcmp(storage, storage_before, sizeof(storage)) == 0);

	entries[0].displacement[0] = 0.0f;
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_APPLIED);
}

static void TestHorizonMultiStepMinkowskiBounds(void)
{
	belief_fixture_t fixture;
	sg_belief_particle_t storage[8];
	sg_belief_particle_t storage_before[8];
	sg_belief_particle_t scratch_first[8];
	sg_belief_particle_t scratch_second[8];
	sg_belief_state_t state;
	sg_belief_state_t before;
	sg_belief_horizon_entry_t entries[3];
	sg_belief_horizon_span_t spans[3];
	sg_belief_horizon_step_t steps[2];
	sg_belief_horizon_kernel_t kernel;
	sg_belief_frame_t frame;
	sg_belief_reduction_t reduction;

	BeliefFixtureInit(&fixture);
	fixture.model_kernels[0].parameters.displacement.x = Interval(9.0f, 11.0f);
	fixture.model_kernels[0].parameters.duration_ms = Interval(40.0f, 60.0f);
	fixture.model_kernels[2].parameters.displacement.x = Interval(19.0f, 21.0f);
	fixture.model_kernels[2].parameters.duration_ms = Interval(40.0f, 60.0f);
	InitState(&fixture, &state, storage, 8U);
	entries[0] = HorizonEntry(0U, 0U, 2U, 1U, 33.0f, 1.0f);
	entries[0].step_count = 2U;
	entries[1] = HorizonEntry(1U, 0U, 1U, 0U, 0.0f, 1.0f);
	entries[2] = HorizonEntry(2U, 1U, 2U, 1U, 0.0f, 1.0f);
	steps[0] = HorizonCapabilityStep(0U, 0U, 1U, 0U, 0U);
	steps[1] = HorizonCapabilityStep(1U, 0U, 2U, 1U, 2U);
	kernel = HorizonKernel(100U, 200U, entries, 3U, spans, 3U, steps, 2U);
	frame = Frame(1U, state.revision, 200U, scratch_first, scratch_second, 8U);
	frame.kernels = &kernel;
	frame.kernel_count = 1U;
	before = state;
	memcpy(storage_before, storage, sizeof(storage));
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_REJECTED_INVALID);
	CHECK(memcmp(&state, &before, sizeof(state)) == 0);
	CHECK(memcmp(storage, storage_before, sizeof(storage)) == 0);

	fixture.model_kernels[0].parameters.displacement.x =
		Interval(9.0f, FLT_MAX);
	fixture.model_kernels[2].parameters.displacement.x =
		Interval(19.0f, FLT_MAX);
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_OVERFLOW);
	CHECK(memcmp(&state, &before, sizeof(state)) == 0);
	CHECK(memcmp(storage, storage_before, sizeof(storage)) == 0);

	fixture.model_kernels[0].parameters.displacement.x = Interval(9.0f, 11.0f);
	fixture.model_kernels[2].parameters.displacement.x = Interval(19.0f, 21.0f);
	entries[0].displacement[0] = 30.0f;
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_APPLIED);
}

static void TestCompleteHorizonMultiHopAndTimeBound(void)
{
	belief_fixture_t fixture;
	sg_belief_particle_t short_storage[8];
	sg_belief_particle_t long_storage[8];
	sg_belief_particle_t scratch_first[8];
	sg_belief_particle_t scratch_second[8];
	sg_belief_state_t short_state;
	sg_belief_state_t long_state;
	sg_belief_evidence_support_t support = Support(0U, 0U, 1.0f);
	sg_belief_evidence_t evidence = Evidence(SG_BELIEF_SOURCE_SIGHT, 1U,
		100U, &support, 1U);
	sg_belief_horizon_entry_t short_entries[3];
	sg_belief_horizon_entry_t long_entries[3];
	sg_belief_horizon_span_t short_spans[3];
	sg_belief_horizon_span_t long_spans[3];
	sg_belief_horizon_step_t short_steps[1];
	sg_belief_horizon_step_t long_steps[2];
	sg_belief_horizon_kernel_t kernel;
	sg_belief_frame_t frame;
	sg_belief_reduction_t reduction;
	sg_belief_state_t before;
	sg_belief_particle_t particles_before[8];
	size_t index;

	BeliefFixtureInit(&fixture);
	InitState(&fixture, &short_state, short_storage, 8U);
	InitState(&fixture, &long_state, long_storage, 8U);
	frame = Frame(1U, short_state.revision, 100U, scratch_first,
		scratch_second, 8U);
	frame.evidence = &evidence;
	frame.evidence_count = 1U;
	CHECK(SG_BeliefReduce(&fixture.snapshot, &short_state, &frame,
		&reduction) == SG_BELIEF_REDUCE_APPLIED);
	frame.expected_revision = long_state.revision;
	frame.expected_generation = long_state.generation;
	CHECK(SG_BeliefReduce(&fixture.snapshot, &long_state, &frame,
		&reduction) == SG_BELIEF_REDUCE_APPLIED);

	short_entries[0] = HorizonEntry(0U, 0U, 1U, 0U, 10.0f, 1.0f);
	short_entries[0].step_count = 1U;
	short_entries[1] = HorizonEntry(1U, 0U, 1U, 0U, 0.0f, 1.0f);
	short_entries[2] = HorizonEntry(2U, 1U, 2U, 1U, 0.0f, 1.0f);
	short_steps[0] = HorizonCapabilityStep(0U, 0U, 1U, 0U, 0U);
	kernel = HorizonKernel(100U, 150U, short_entries, 3U, short_spans, 3U,
		short_steps, 1U);
	frame = Frame(2U, short_state.revision, 150U, scratch_first,
		scratch_second, 8U);
	frame.kernels = &kernel;
	frame.kernel_count = 1U;
	CHECK(SG_BeliefReduce(&fixture.snapshot, &short_state, &frame,
		&reduction) == SG_BELIEF_REDUCE_APPLIED);
	CHECK(short_state.particle_count == 2U);
	for (index = 0U; index < short_state.particle_count; index++)
		CHECK(short_state.particles[index].phase.phase_id != 2U);

	long_entries[0] = HorizonEntry(0U, 0U, 2U, 1U, 20.0f, 1.0f);
	long_entries[0].step_count = 1U;
	long_entries[1] = HorizonEntry(1U, 0U, 2U, 1U, 10.0f, 1.0f);
	long_entries[1].first_step = 1U;
	long_entries[1].step_count = 1U;
	long_entries[2] = HorizonEntry(2U, 1U, 2U, 1U, 0.0f, 1.0f);
	long_steps[0] = HorizonCapabilityStep(0U, 0U, 2U, 1U, 1U);
	long_steps[1] = HorizonCapabilityStep(1U, 0U, 2U, 1U, 2U);
	kernel = HorizonKernel(100U, 200U, long_entries, 3U, long_spans, 3U,
		long_steps, 2U);
	frame = Frame(2U, long_state.revision, 200U, scratch_first,
		scratch_second, 8U);
	frame.kernels = &kernel;
	frame.kernel_count = 1U;
	CHECK(SG_BeliefReduce(&fixture.snapshot, &long_state, &frame,
		&reduction) == SG_BELIEF_REDUCE_APPLIED);
	CHECK(long_state.particle_count == 2U);
	CHECK(long_state.particles[1].phase.phase_id == 2U);

	kernel = HorizonKernel(200U, 210U, long_entries, 2U, long_spans, 3U,
		long_steps, 2U);
	frame = Frame(3U, long_state.revision, 210U, scratch_first,
		scratch_second, 8U);
	frame.kernels = &kernel;
	frame.kernel_count = 1U;
	before = long_state;
	memcpy(particles_before, long_storage, sizeof(long_storage));
	CHECK(SG_BeliefReduce(&fixture.snapshot, &long_state, &frame,
		&reduction) == SG_BELIEF_REDUCE_REJECTED_INVALID);
	CHECK(memcmp(&long_state, &before, sizeof(long_state)) == 0);
	CHECK(memcmp(long_storage, particles_before, sizeof(long_storage)) == 0);
}

static void TestCsrKernelScalingCounters(void)
{
	const size_t phase_count = 4096U;
	const size_t entry_count = phase_count + 1U;
	sg_rune_phase_basis_t *model_phases = calloc(phase_count,
		sizeof(*model_phases));
	sg_phase_coordinate_t *phases = calloc(phase_count, sizeof(*phases));
	sg_belief_horizon_span_t *spans = calloc(phase_count, sizeof(*spans));
	sg_belief_horizon_entry_t *entries = calloc(entry_count,
		sizeof(*entries));
	sg_rune_cell_t cell;
	sg_rune_model_t model;
	sg_rune_capability_kernel_t model_kernel;
	sg_rune_runtime_snapshot_t snapshot;
	sg_belief_particle_t storage[4];
	sg_belief_particle_t scratch_first[4];
	sg_belief_particle_t scratch_second[4];
	sg_belief_state_t state;
	sg_belief_state_config_t config = Config(1U, 2U, 3U);
	sg_belief_evidence_support_t support = Support(0U, 0U, 1.0f);
	sg_belief_evidence_t evidence = Evidence(SG_BELIEF_SOURCE_SIGHT, 1U,
		100U, &support, 1U);
	sg_belief_horizon_kernel_t kernel;
	sg_belief_horizon_step_t step;
	sg_belief_frame_t frame;
	sg_belief_reduction_t reduction;
	size_t phase;
	size_t cursor = 0U;

	CHECK(model_phases && phases && spans && entries);
	if (!model_phases || !phases || !spans || !entries)
		goto cleanup;
	memset(&cell, 0, sizeof(cell));
	memset(&model, 0, sizeof(model));
	memset(&snapshot, 0, sizeof(snapshot));
	model.version = SG_RUNE_MODEL_VERSION;
	model.schema_tag = SG_RUNE_MODEL_SCHEMA_TAG;
	model.flags = SG_RUNE_MODEL_IMMUTABLE | SG_RUNE_MODEL_EXACT_BOUND |
		SG_RUNE_MODEL_NO_RUNTIME_ACTORS;
	model.completeness.state = SG_RUNE_COMPLETENESS_COMPLETE;
	model.cell_count = 1U;
	model.phase_count = (uint32_t)phase_count;
	model.cells = &cell;
	model.phases = model_phases;
	for (phase = 0U; phase < phase_count; phase++)
	{
		model_phases[phase].id.value = StableId(phase + 1U);
		phases[phase] = (sg_phase_coordinate_t){ (uint32_t)phase, 0U };
	}
	SetKernel(&model_kernel, &cell, &model_phases[0], &cell,
		&model_phases[1]);
	model.kernel_count = 1U;
	model.kernels = &model_kernel;
	snapshot.identity = 99U;
	snapshot.topology_revision = 7U;
	snapshot.cell_count = 1U;
	snapshot.phase_count = (uint32_t)phase_count;
	snapshot.region_count = 1U;
	snapshot.model = &model;
	snapshot.phases = phases;
	CHECK(SG_RuneRuntimeSnapshotValid(&snapshot));
	CHECK(SG_BeliefStateInit(&snapshot, &state, &config, storage, 4U));
	frame = Frame(1U, state.revision, 100U, scratch_first, scratch_second, 4U);
	frame.evidence = &evidence;
	frame.evidence_count = 1U;
	CHECK(SG_BeliefReduce(&snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_APPLIED);
	entries[cursor++] = HorizonEntry(0U, 0U, 0U, 0U, 0.0f, 0.5f);
	entries[cursor++] = HorizonEntry(0U, 0U, 1U, 0U, 2.0f, 0.5f);
	entries[1].step_count = 1U;
	for (phase = 1U; phase < phase_count; phase++)
		entries[cursor++] = HorizonEntry((uint32_t)phase, 0U,
			(uint32_t)phase, 0U, 0.0f, 1.0f);
	CHECK(cursor == entry_count);
	step = HorizonCapabilityStep(0U, 0U, 1U, 0U, 0U);
	kernel = HorizonKernel(100U, 200U, entries, entry_count, spans,
		phase_count, &step, 1U);
	frame = Frame(2U, state.revision, 200U, scratch_first, scratch_second, 4U);
	frame.kernels = &kernel;
	frame.kernel_count = 1U;
	CHECK(SG_BeliefReduce(&snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_APPLIED);
	CHECK(reduction.validated_phase_spans == phase_count);
	CHECK(reduction.validated_horizon_entries == entry_count);
	CHECK(reduction.validated_horizon_steps == 1U);
	CHECK(reduction.evaluated_outcomes == 2U);
	CHECK(state.particle_count == 2U);

cleanup:
	free(entries);
	free(spans);
	free(phases);
	free(model_phases);
}

static void TestPropagationDecayPredictionAndRuneImmutability(void)
{
	belief_fixture_t fixture;
	belief_fixture_t fixture_before;
	sg_belief_particle_t storage[8];
	sg_belief_particle_t particles_before[8];
	sg_belief_particle_t scratch_first[8];
	sg_belief_particle_t scratch_second[8];
	sg_belief_particle_t predicted[8];
	sg_belief_particle_t too_small[1];
	sg_belief_particle_t too_small_before[1];
	sg_belief_state_t state;
	sg_belief_state_t state_before;
	sg_belief_evidence_support_t support = Support(0U, 0U, 1.0f);
	sg_belief_evidence_t evidence = Evidence(SG_BELIEF_SOURCE_SIGHT, 1U,
		100U, &support, 1U);
	sg_belief_horizon_entry_t entries[3];
	sg_belief_horizon_span_t spans[3];
	sg_belief_horizon_step_t steps[2];
	sg_belief_horizon_kernel_t kernel;
	sg_belief_frame_t frame;
	sg_belief_reduction_t reduction;
	sg_belief_prediction_t prediction;

	BeliefFixtureInit(&fixture);
	fixture_before = fixture;
	InitState(&fixture, &state, storage, 8U);
	frame = Frame(1U, state.revision, 100U, scratch_first, scratch_second, 8U);
	frame.evidence = &evidence;
	frame.evidence_count = 1U;
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_APPLIED);
	entries[0] = HorizonEntry(0U, 0U, 2U, 1U, 20.0f, 1.0f);
	entries[0].step_count = 1U;
	entries[1] = HorizonEntry(1U, 0U, 2U, 1U, 10.0f, 1.0f);
	entries[1].first_step = 1U;
	entries[1].step_count = 1U;
	entries[2] = HorizonEntry(2U, 1U, 2U, 1U, 0.0f, 1.0f);
	steps[0] = HorizonCapabilityStep(0U, 0U, 2U, 1U, 1U);
	steps[1] = HorizonCapabilityStep(1U, 0U, 2U, 1U, 2U);
	kernel = HorizonKernel(100U, 200U, entries, 3U, spans, 3U, steps, 2U);
	frame = Frame(2U, state.revision, 200U, scratch_first, scratch_second, 8U);
	frame.kernels = &kernel;
	frame.kernel_count = 1U;
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_APPLIED);
	CHECK(state.particle_count == 2U);
	CHECK(Near(state.particles[0].weight, 0.5f, 0.00001f));
	CHECK(Near(state.particles[1].weight, 0.5f, 0.00001f));
	CHECK(state.particles[0].phase.phase_id == 0U);
	CHECK(state.particles[1].phase.phase_id == 2U);
	CHECK(Near(state.particles[0].position[0], 0.1f, 0.0001f));
	CHECK(Near(state.particles[1].position[0], 20.1f, 0.0001f));
	CHECK(Near(state.particles[0].spread_radius, 1.0f, 0.0001f));
	CHECK(Near(state.confidence, expf(-0.1f), 0.0001f));
	CHECK(memcmp(&fixture, &fixture_before, sizeof(fixture)) == 0);

	state_before = state;
	memcpy(particles_before, storage, sizeof(storage));
	memset(&prediction, 0, sizeof(prediction));
	CHECK(SG_BeliefPredict(&fixture.snapshot, &state, 300U, predicted, 8U,
		&prediction));
	CHECK(prediction.particle_count == 2U);
	CHECK(prediction.at_time_ms == 300U);
	CHECK(Near(predicted[0].position[0], 0.2f, 0.0001f));
	CHECK(memcmp(&state, &state_before, sizeof(state)) == 0);
	CHECK(memcmp(storage, particles_before, sizeof(storage)) == 0);

	memset(&prediction, 0, sizeof(prediction));
	memset(too_small, 0xa5, sizeof(too_small));
	memcpy(too_small_before, too_small, sizeof(too_small));
	CHECK(!SG_BeliefPredict(&fixture.snapshot, &state, 300U, too_small, 1U,
		&prediction));
	CHECK(prediction.required_particle_capacity == 2U);
	CHECK(memcmp(too_small, too_small_before, sizeof(too_small)) == 0);

	kernel.host_complete = 0U;
	frame = Frame(3U, state.revision, 210U, scratch_first, scratch_second, 8U);
	frame.kernels = &kernel;
	frame.kernel_count = 1U;
	state_before = state;
	memcpy(particles_before, storage, sizeof(storage));
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_REJECTED_INVALID);
	CHECK(memcmp(&state, &state_before, sizeof(state)) == 0);
	CHECK(memcmp(storage, particles_before, sizeof(storage)) == 0);
}

static void TestTransactionalRejectDuplicateAndStale(void)
{
	belief_fixture_t fixture;
	sg_belief_particle_t storage[8];
	sg_belief_particle_t particles_before[8];
	sg_belief_particle_t scratch_first[8];
	sg_belief_particle_t scratch_second[8];
	sg_belief_state_t state;
	sg_belief_state_t before;
	sg_belief_evidence_support_t supports[2];
	sg_belief_evidence_t evidence[2];
	sg_belief_frame_t frame;
	sg_belief_reduction_t reduction;

	BeliefFixtureInit(&fixture);
	InitState(&fixture, &state, storage, 8U);
	supports[0] = Support(0U, 0U, 1.0f);
	evidence[0] = Evidence(SG_BELIEF_SOURCE_SIGHT, 1U, 100U, supports, 1U);
	frame = Frame(2U, state.revision, 100U, scratch_first, scratch_second, 8U);
	frame.evidence = evidence;
	frame.evidence_count = 1U;
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_APPLIED);

	before = state;
	memcpy(particles_before, storage, sizeof(storage));
	memset(&frame, 0, sizeof(frame));
	frame.sequence = 2U;
	frame.expected_revision = state.revision - 1U;
	frame.expected_generation = state.generation - 1U;
	frame.at_ms = state.updated_at_ms;
	frame.evidence_count = SIZE_MAX;
	frame.evidence = (const sg_belief_evidence_t *)(uintptr_t)1U;
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_OVERFLOW);
	CHECK(memcmp(&state, &before, sizeof(state)) == 0);
	CHECK(memcmp(storage, particles_before, sizeof(storage)) == 0);

	frame = Frame(1U, state.revision, 101U, scratch_first, scratch_second, 8U);
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_REJECTED_STALE);
	CHECK(memcmp(&state, &before, sizeof(state)) == 0);

	supports[0] = Support(1U, 0U, 1.0f);
	supports[1] = Support(99U, 0U, 1.0f);
	evidence[0] = Evidence(SG_BELIEF_SOURCE_SIGHT, 2U, 110U, &supports[0], 1U);
	evidence[1] = Evidence(SG_BELIEF_SOURCE_SIGHT, 3U, 110U, &supports[1], 1U);
	frame = Frame(3U, state.revision, 110U, scratch_first, scratch_second, 8U);
	frame.evidence = evidence;
	frame.evidence_count = 2U;
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_REJECTED_INVALID);
	CHECK(memcmp(&state, &before, sizeof(state)) == 0);
	CHECK(memcmp(storage, particles_before, sizeof(storage)) == 0);

	evidence[0] = Evidence(SG_BELIEF_SOURCE_SIGHT, 1U, 110U, supports, 1U);
	frame = Frame(3U, state.revision, 110U, scratch_first, scratch_second, 8U);
	frame.evidence = evidence;
	frame.evidence_count = 1U;
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_REJECTED_STALE);
	CHECK(memcmp(&state, &before, sizeof(state)) == 0);
}

static void TestDelayedTeammateEvidenceIsAgedAndPropagated(void)
{
	belief_fixture_t fixture;
	sg_belief_particle_t storage[8];
	sg_belief_particle_t storage_before[8];
	sg_belief_particle_t scratch_first[8];
	sg_belief_particle_t scratch_second[8];
	sg_belief_state_t state;
	sg_belief_state_t before;
	sg_belief_evidence_support_t support = Support(0U, 0U, 1.0f);
	sg_belief_evidence_t evidence = Evidence(SG_BELIEF_SOURCE_TEAMMATE, 1U,
		100U, &support, 1U);
	sg_belief_horizon_entry_t entries[3];
	sg_belief_horizon_span_t spans[3];
	sg_belief_horizon_step_t step;
	sg_belief_horizon_kernel_t kernel;
	sg_belief_frame_t frame;
	sg_belief_reduction_t reduction;

	BeliefFixtureInit(&fixture);
	InitState(&fixture, &state, storage, 8U);
	evidence.provenance.issuer_kind = SG_BELIEF_ISSUER_TEAMMATE;
	evidence.provenance.issuer_client = 7U;
	evidence.provenance.authenticated_at_ms = 150U;
	evidence.valid_until_ms = 250U;
	entries[0] = HorizonEntry(0U, 0U, 1U, 0U, 10.0f, 1.0f);
	entries[0].step_count = 1U;
	entries[1] = HorizonEntry(1U, 0U, 1U, 0U, 0.0f, 1.0f);
	entries[2] = HorizonEntry(2U, 1U, 2U, 1U, 0.0f, 1.0f);
	step = HorizonCapabilityStep(0U, 0U, 1U, 0U, 0U);
	kernel = HorizonKernel(100U, 200U, entries, 3U, spans, 3U, &step, 1U);
	frame = Frame(1U, state.revision, 200U, scratch_first, scratch_second, 8U);
	frame.evidence = &evidence;
	frame.evidence_count = 1U;
	frame.kernels = &kernel;
	frame.kernel_count = 1U;
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_APPLIED);
	CHECK(state.particle_count == 2U);
	CHECK(state.particles[0].future_time_ms == 200U);
	CHECK(state.particles[1].future_time_ms == 200U);
	CHECK(Near(state.particles[0].position[0], 0.1f, 0.0001f));
	CHECK(Near(state.particles[1].position[0], 10.1f, 0.0001f));
	CHECK(Near(state.confidence, expf(-0.1f), 0.0001f));
	CHECK(state.latest_provenance.authenticated_at_ms == 150U);
	CHECK(state.latest_observed_at_ms == 100U);
	CHECK(state.latest_valid_until_ms == 250U);
	CHECK(state.latest_evidence_confidence == 1.0f);

	evidence.provenance.evidence_sequence = 2U;
	evidence.provenance.evidence_id = 102U;
	evidence.valid_until_ms = 199U;
	frame = Frame(2U, state.revision, 200U, scratch_first, scratch_second, 8U);
	frame.evidence = &evidence;
	frame.evidence_count = 1U;
	before = state;
	memcpy(storage_before, state.particles, sizeof(storage_before));
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_REJECTED_STALE);
	CHECK(memcmp(&state, &before, sizeof(state)) == 0);
	CHECK(memcmp(state.particles, storage_before, sizeof(storage_before)) == 0);
}

static void TestCapacityRetryBeyondOldThresholds(void)
{
	large_belief_fixture_t large;
	belief_fixture_t fixture;
	sg_belief_particle_t small_storage[8];
	sg_belief_particle_t small_before[8];
	sg_belief_particle_t large_storage[70];
	sg_belief_particle_t scratch_first[300];
	sg_belief_particle_t scratch_second[300];
	sg_belief_particle_t transition_storage[300];
	sg_belief_state_t state;
	sg_belief_state_t before;
	sg_belief_state_config_t config = Config(1U, 2U, 3U);
	sg_belief_evidence_support_t supports[65];
	sg_belief_evidence_t evidence;
	sg_belief_frame_t frame;
	sg_belief_reduction_t reduction;
	sg_belief_evidence_t evidence_many[17];
	sg_belief_evidence_support_t support_many[17];
	sg_belief_horizon_entry_t horizon_entries[260];
	sg_belief_horizon_span_t horizon_spans[3];
	sg_belief_horizon_kernel_t kernel;
	sg_belief_horizon_step_t step;
	sg_belief_particle_t evidence_storage[32];
	sg_belief_state_t evidence_state;
	sg_belief_particle_t transition_scratch_first[300];
	sg_belief_particle_t transition_scratch_second[300];
	sg_belief_state_t transition_state;
	sg_belief_evidence_support_t seed_support;
	sg_belief_evidence_t seed;
	size_t index;

	LargeBeliefFixtureInit(&large);
	CHECK(SG_BeliefStateInit(&large.snapshot, &state, &config, small_storage,
		8U));
	for (index = 0U; index < 65U; index++)
		supports[index] = Support((uint32_t)index,
			(uint32_t)(index % 2U), 1.0f);
	evidence = Evidence(SG_BELIEF_SOURCE_ITEM, 1U, 100U, supports, 65U);
	frame = Frame(1U, state.revision, 100U, scratch_first, scratch_second, 300U);
	frame.evidence = &evidence;
	frame.evidence_count = 1U;
	frame.scratch_capacity = 64U;
	before = state;
	memcpy(small_before, small_storage, sizeof(small_storage));
	CHECK(SG_BeliefReduce(&large.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_CAPACITY);
	CHECK(reduction.required_scratch_capacity == 65U);
	CHECK(memcmp(&state, &before, sizeof(state)) == 0);
	CHECK(memcmp(small_storage, small_before, sizeof(small_storage)) == 0);
	frame.scratch_capacity = 300U;
	CHECK(SG_BeliefReduce(&large.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_CAPACITY);
	CHECK(reduction.required_particle_capacity == 65U);
	CHECK(memcmp(&state, &before, sizeof(state)) == 0);
	CHECK(memcmp(small_storage, small_before, sizeof(small_storage)) == 0);
	frame.commit_storage = large_storage;
	frame.commit_capacity = 70U;
	CHECK(SG_BeliefReduce(&large.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_APPLIED);
	CHECK(state.particles == large_storage);
	CHECK(state.particle_count == 65U);
	CHECK(Near(state.total_weight, 1.0f, 0.00001f));

	BeliefFixtureInit(&fixture);
	InitState(&fixture, &evidence_state, evidence_storage, 32U);
	for (index = 0U; index < 17U; index++)
	{
		support_many[index] = Support(0U, 0U, 1.0f);
		support_many[index].position[0] = (float)index;
		evidence_many[index] = Evidence(SG_BELIEF_SOURCE_SIGHT,
			(uint64_t)index + 1U, 100U, &support_many[index], 1U);
		evidence_many[index].confidence = 0.1f;
	}
	frame = Frame(1U, evidence_state.revision, 100U, scratch_first,
		scratch_second, 300U);
	frame.evidence = evidence_many;
	frame.evidence_count = 17U;
	CHECK(SG_BeliefReduce(&fixture.snapshot, &evidence_state, &frame,
		&reduction) == SG_BELIEF_REDUCE_APPLIED);
	CHECK(evidence_state.particle_count == 17U);
	CHECK(evidence_state.last_evidence_sequence == 17U);

	InitState(&fixture, &transition_state, transition_storage, 300U);
	seed_support = Support(0U, 0U, 1.0f);
	seed = Evidence(SG_BELIEF_SOURCE_SIGHT, 1U, 100U, &seed_support, 1U);
	frame = Frame(1U, transition_state.revision, 100U,
		transition_scratch_first, transition_scratch_second, 300U);
	frame.evidence = &seed;
	frame.evidence_count = 1U;
	CHECK(SG_BeliefReduce(&fixture.snapshot, &transition_state, &frame,
		&reduction) == SG_BELIEF_REDUCE_APPLIED);
	for (index = 0U; index < 258U; index++)
	{
		horizon_entries[index] = HorizonEntry(0U, 0U, 1U, 0U,
			(float)index, 1.0f / 258.0f);
		horizon_entries[index].step_count = 1U;
	}
	horizon_entries[258] = HorizonEntry(1U, 0U, 1U, 0U, 0.0f, 1.0f);
	horizon_entries[259] = HorizonEntry(2U, 1U, 2U, 1U, 0.0f, 1.0f);
	step = HorizonCapabilityStep(0U, 0U, 1U, 0U, 0U);
	kernel = HorizonKernel(100U, 200U, horizon_entries, 260U,
		horizon_spans, 3U, &step, 1U);
	frame = Frame(2U, transition_state.revision, 200U,
		transition_scratch_first, transition_scratch_second, 300U);
	frame.kernels = &kernel;
	frame.kernel_count = 1U;
	CHECK(SG_BeliefReduce(&fixture.snapshot, &transition_state, &frame,
		&reduction) == SG_BELIEF_REDUCE_APPLIED);
	CHECK(transition_state.particle_count == 259U);
	CHECK(Near(transition_state.total_weight, 1.0f, 0.00001f));
	CHECK(SG_BeliefStateValid(&transition_state));
}

static void ReviewTestWitnessCannotHideUnboundedKinematics(void)
{
	belief_fixture_t fixture;
	sg_belief_particle_t storage[8];
	sg_belief_particle_t scratch_first[8];
	sg_belief_particle_t scratch_second[8];
	sg_belief_state_t state;
	sg_belief_state_config_t config = Config(1U, 2U, 3U);
	sg_belief_evidence_support_t support = Support(0U, 0U, 1.0f);
	sg_belief_evidence_t evidence;
	sg_belief_horizon_entry_t entries[3];
	sg_belief_horizon_span_t spans[3];
	sg_belief_horizon_step_t step;
	sg_belief_horizon_kernel_t kernel;
	sg_belief_frame_t frame;
	sg_belief_reduction_t reduction;

	BeliefFixtureInit(&fixture);
	config.policy.diffusion_fraction = 1.0f;
	CHECK(SG_BeliefStateInit(&fixture.snapshot, &state, &config, storage, 8U));
	support.velocity[0] = 10000.0f;
	evidence = Evidence(SG_BELIEF_SOURCE_SIGHT, 1U, 100U, &support, 1U);
	frame = Frame(1U, state.revision, 100U, scratch_first, scratch_second, 8U);
	frame.evidence = &evidence;
	frame.evidence_count = 1U;
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_APPLIED);
	entries[0] = HorizonEntry(0U, 0U, 1U, 0U, 10.0f, 1.0f);
	entries[0].step_count = 1U;
	entries[1] = HorizonEntry(1U, 0U, 1U, 0U, 0.0f, 1.0f);
	entries[2] = HorizonEntry(2U, 1U, 2U, 1U, 0U, 1.0f);
	step = HorizonCapabilityStep(0U, 0U, 1U, 0U, 0U);
	kernel = HorizonKernel(100U, 200U, entries, 3U, spans, 3U, &step, 1U);
	frame = Frame(2U, state.revision, 200U, scratch_first, scratch_second, 8U);
	frame.kernels = &kernel;
	frame.kernel_count = 1U;
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_REJECTED_INVALID);
}

static void ReviewTestSoundCannotCollapseNumerically(void)
{
	belief_fixture_t fixture;
	sg_belief_particle_t storage[8];
	sg_belief_particle_t scratch_first[8];
	sg_belief_particle_t scratch_second[8];
	sg_belief_state_t state;
	sg_belief_evidence_support_t supports[2];
	sg_belief_evidence_t evidence;
	sg_belief_frame_t frame;
	sg_belief_reduction_t reduction;

	BeliefFixtureInit(&fixture);
	InitState(&fixture, &state, storage, 8U);
	supports[0] = Support(0U, 0U, FLT_MAX);
	supports[1] = Support(0U, 0U, FLT_TRUE_MIN);
	supports[1].position[0] = 10.0f;
	evidence = Evidence(SG_BELIEF_SOURCE_SOUND, 1U, 100U, supports, 2U);
	frame = Frame(1U, state.revision, 100U, scratch_first, scratch_second, 8U);
	frame.evidence = &evidence;
	frame.evidence_count = 1U;
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_APPLIED);
	CHECK(state.particle_count == 2U);
}

static void ReviewTestKernelCsrRejectsOrphanStep(void)
{
	belief_fixture_t fixture;
	sg_belief_particle_t storage[8];
	sg_belief_particle_t scratch_first[8];
	sg_belief_particle_t scratch_second[8];
	sg_belief_state_t state;
	sg_belief_horizon_entry_t entries[3];
	sg_belief_horizon_span_t spans[3];
	sg_belief_horizon_step_t steps[2];
	sg_belief_horizon_kernel_t kernel;
	sg_belief_frame_t frame;
	sg_belief_reduction_t reduction;

	BeliefFixtureInit(&fixture);
	InitState(&fixture, &state, storage, 8U);
	entries[0] = HorizonEntry(0U, 0U, 1U, 0U, 10.0f, 1.0f);
	entries[0].step_count = 1U;
	entries[1] = HorizonEntry(1U, 0U, 1U, 0U, 0.0f, 1.0f);
	entries[2] = HorizonEntry(2U, 1U, 2U, 1U, 0.0f, 1.0f);
	steps[0] = HorizonCapabilityStep(0U, 0U, 1U, 0U, 0U);
	steps[1] = steps[0];
	kernel = HorizonKernel(100U, 200U, entries, 3U, spans, 3U, steps, 2U);
	frame = Frame(1U, state.revision, 200U, scratch_first, scratch_second, 8U);
	frame.kernels = &kernel;
	frame.kernel_count = 1U;
	CHECK(SG_BeliefReduce(&fixture.snapshot, &state, &frame, &reduction) ==
		SG_BELIEF_REDUCE_REJECTED_INVALID);
}

int main(void)
{
	ReviewTestWitnessCannotHideUnboundedKinematics();
	ReviewTestSoundCannotCollapseNumerically();
	ReviewTestKernelCsrRejectsOrphanStep();
	TestStateInitIsTransactional();
	TestStorageAliasingAndCountOverflow();
	TestSightConcentrates();
	TestCanonicalModeMerge();
	TestAllEarnedSourcesAndTeamAuthority();
	TestSoundDiffusionAndNegativeSight();
	TestNegativeSightUsesSpatialOverlap();
	TestSourceShapeGuards();
	TestDirectUncertainEvidenceCannotCollapseToExactAim();
	TestNegativeVisibilityUsesRegionUnion();
	TestIdentityGenerationAndMotionFailClosed();
	TestHorizonCannotInventConnectivity();
	TestHorizonWitnessBoundsAndCells();
	TestHorizonMultiStepMinkowskiBounds();
	TestCompleteHorizonMultiHopAndTimeBound();
	TestCsrKernelScalingCounters();
	TestPropagationDecayPredictionAndRuneImmutability();
	TestTransactionalRejectDuplicateAndStale();
	TestDelayedTeammateEvidenceIsAgedAndPropagated();
	TestCapacityRetryBeyondOldThresholds();
	if (failures != 0)
	{
		fprintf(stderr, "sg_belief_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("sg_belief_test: ok");
	return 0;
}
