#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "slipgate/sg_belief_runtime.h"

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

typedef struct runtime_fixture_s
{
	sg_rune_plane_t planes[6];
	sg_rune_phase_basis_t model_phases[3];
	sg_rune_capability_kernel_t kernels[3];
	sg_rune_cell_t cells[2];
	sg_rune_model_t model;
	sg_phase_coordinate_t phases[3];
	sg_rune_runtime_snapshot_t snapshot;
} runtime_fixture_t;

static sg_belief_runtime_provider_t replacement_provider;
static sg_belief_runtime_provider_t restored_provider;

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
	kernel->parameters.speed = Interval(0.0f, 100.0f);
	kernel->parameters.acceleration = Interval(0.0f, 1000.0f);
	kernel->parameters.vertical_acceleration = Interval(0.0f, 1000.0f);
	kernel->parameters.gravity = 800.0f;
	kernel->flags = SG_RUNE_KERNEL_DIRECTIONAL | SG_RUNE_KERNEL_PHASE_AWARE |
		SG_RUNE_KERNEL_PROVEN;
}

static void FixtureInit(runtime_fixture_t *fixture)
{
	size_t index;

	memset(fixture, 0, sizeof(*fixture));
	fixture->model.version = SG_RUNE_MODEL_VERSION;
	fixture->model.schema_tag = SG_RUNE_MODEL_SCHEMA_TAG;
	fixture->model.flags = SG_RUNE_MODEL_IMMUTABLE |
		SG_RUNE_MODEL_EXACT_BOUND | SG_RUNE_MODEL_NO_RUNTIME_ACTORS;
	fixture->model.completeness.state = SG_RUNE_COMPLETENESS_COMPLETE;
	fixture->model.cell_count = 2U;
	fixture->model.phase_count = 3U;
	fixture->model.plane_count = 6U;
	fixture->model.planes = fixture->planes;
	fixture->model.cells = fixture->cells;
	fixture->model.phases = fixture->model_phases;
	fixture->model.kernel_count = 3U;
	fixture->model.kernels = fixture->kernels;
	fixture->model.identity.physics.gravity = 800.0f;
	fixture->model.identity.physics.ground_acceleration = 1000.0f;
	fixture->model.identity.physics.air_acceleration = 1000.0f;
	fixture->model.identity.physics.water_acceleration = 1000.0f;
	fixture->model.identity.physics.hook_acceleration = 1000.0f;
	fixture->model.identity.physics.external_acceleration = 1000.0f;
	fixture->model.identity.physics.max_velocity = 20000.0f;
	fixture->cells[0].id.value = StableId(10U);
	fixture->cells[1].id.value = StableId(11U);
	for (index = 0U; index < 6U; index++)
	{
		fixture->planes[index].normal.value[index / 2U] =
			(index & 1U) == 0U ? 1.0f : -1.0f;
		fixture->planes[index].distance = 1000.0f;
	}
	for (index = 0U; index < 2U; index++)
	{
		fixture->cells[index].bounds.mins =
			(sg_rune_vec3_t){ { -1000.0f, -1000.0f, -1000.0f } };
		fixture->cells[index].bounds.maxs =
			(sg_rune_vec3_t){ { 1000.0f, 1000.0f, 1000.0f } };
		fixture->cells[index].boundary_planes =
			(sg_rune_plane_span_t){ 0U, 6U };
	}
	for (index = 0U; index < 3U; index++)
	{
		fixture->model_phases[index].id.value = StableId(index + 1U);
		fixture->model_phases[index].velocity.x = Interval(-20000.0f, 20000.0f);
		fixture->model_phases[index].velocity.y = Interval(-20000.0f, 20000.0f);
		fixture->model_phases[index].velocity.z = Interval(-20000.0f, 20000.0f);
	}
	SetKernel(&fixture->kernels[0], &fixture->cells[0],
		&fixture->model_phases[0], &fixture->cells[0],
		&fixture->model_phases[1]);
	SetKernel(&fixture->kernels[1], &fixture->cells[0],
		&fixture->model_phases[0], &fixture->cells[1],
		&fixture->model_phases[2]);
	SetKernel(&fixture->kernels[2], &fixture->cells[0],
		&fixture->model_phases[1], &fixture->cells[1],
		&fixture->model_phases[2]);
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

static int Locate(void *context, const sg_rune_runtime_snapshot_t *snapshot,
	const float position[3], sg_phase_coordinate_t *phase_out)
{
	(void)context;
	if (!SG_RuneRuntimeSnapshotValid(snapshot) || !position || !phase_out)
		return 0;
	*phase_out = position[0] >= 500.0f ?
		(sg_phase_coordinate_t){ 2U, 1U } :
		(sg_phase_coordinate_t){ 0U, 0U };
	return 1;
}

static int LocateReplacingProvider(void *context,
	const sg_rune_runtime_snapshot_t *snapshot, const float position[3],
	sg_phase_coordinate_t *phase_out)
{
	int located = Locate(context, snapshot, position, phase_out);

	(void)SG_BeliefRuntimeProviderSet(&replacement_provider);
	return located;
}

static int LocateReplacingProviderTwice(void *context,
	const sg_rune_runtime_snapshot_t *snapshot, const float position[3],
	sg_phase_coordinate_t *phase_out)
{
	int located = Locate(context, snapshot, position, phase_out);

	(void)SG_BeliefRuntimeProviderSet(&replacement_provider);
	(void)SG_BeliefRuntimeProviderSet(&restored_provider);
	return located;
}

static sg_belief_runtime_provider_t Provider(runtime_fixture_t *fixture,
	uint64_t localization_generation, float diffusion_fraction)
{
	sg_belief_runtime_provider_t provider;

	memset(&provider, 0, sizeof(provider));
	provider.snapshot = &fixture->snapshot;
	provider.localization_generation = localization_generation;
	provider.policy.confidence_decay_ms = 1000U;
	provider.policy.diffusion_fraction = diffusion_fraction;
	provider.policy.spread_growth_per_ms = 0.01f;
	provider.locate = Locate;
	return provider;
}

static sg_belief_life_identity_t Life(uint32_t client_id,
	uint64_t spawn_generation)
{
	sg_belief_life_identity_t life;

	memset(&life, 0, sizeof(life));
	life.client_id = client_id;
	life.spawn_generation = spawn_generation;
	return life;
}

static sg_perception_hypothesis_t Hypothesis(uint32_t phase, uint32_t cell,
	sg_perception_location_basis_t basis, float spread)
{
	sg_perception_hypothesis_t hypothesis;

	memset(&hypothesis, 0, sizeof(hypothesis));
	hypothesis.phase = (sg_phase_coordinate_t){ phase, cell };
	hypothesis.location_basis = basis;
	hypothesis.movement_state = SG_BELIEF_MOTION_GROUND;
	hypothesis.position[0] = phase == 2U ? 500.0f : 0.0f;
	hypothesis.velocity[0] = 1.0f;
	hypothesis.spread_radius = spread;
	hypothesis.likelihood = 1.0f;
	return hypothesis;
}

static sg_perception_observation_t Observation(sg_perception_source_t source,
	uint64_t sequence, uint64_t at_ms, uint64_t target_generation)
{
	sg_perception_observation_t observation;

	memset(&observation, 0, sizeof(observation));
	observation.authentication.authenticated = 1U;
	observation.authentication.authority = source == SG_PERCEPTION_SOURCE_TEAMMATE ?
		SG_PERCEPTION_AUTHORITY_HOST_TEAMMATE_REPORT :
		SG_PERCEPTION_AUTHORITY_HOST_SENSOR;
	observation.authentication.issuer_team = 1U;
	observation.authentication.audience_team = 1U;
	observation.authentication.issuer_life = Life(4U, 40U);
	observation.authentication.event_id = sequence + 1000U;
	observation.authentication.evidence_sequence = sequence;
	observation.authentication.observed_at_ms = at_ms;
	observation.authentication.authenticated_at_ms = at_ms;
	observation.authentication.valid_until_ms = at_ms + 100U;
	observation.authentication.rune_identity = 99U;
	observation.authentication.topology_revision = 7U;
	observation.source = source;
	observation.evidence_kind = SG_BELIEF_EVIDENCE_POSITIVE;
	observation.target_team = 2U;
	observation.target_life = Life(3U, target_generation);
	observation.confidence = 0.8f;
	return observation;
}

static void SightObservation(sg_perception_observation_t *observation,
	uint64_t sequence, uint64_t at_ms, uint64_t target_generation)
{
	*observation = Observation(SG_PERCEPTION_SOURCE_SIGHT, sequence, at_ms,
		target_generation);
	observation->data.sight.in_pvs = 1U;
	observation->data.sight.line_of_sight_proved = 1U;
	observation->data.sight.hypothesis = Hypothesis(0U, 0U,
		SG_PERCEPTION_LOCATION_EARNED_RUNTIME, 0.0f);
}

static void SoundObservation(sg_perception_observation_t *observation,
	uint64_t sequence, uint64_t at_ms, uint64_t target_generation,
	const sg_perception_hypothesis_t *hypothesis)
{
	*observation = Observation(SG_PERCEPTION_SOURCE_SOUND, sequence, at_ms,
		target_generation);
	observation->data.sound.in_phs = 1U;
	observation->data.sound.positional = 1U;
	observation->data.sound.kind = SG_PERCEPTION_SOUND_WEAPON;
	observation->data.sound.sound_id = 9U;
	observation->data.sound.attenuation = 1.0f;
	observation->data.sound.audible_radius = 800.0f;
	observation->data.sound.hypotheses = hypothesis;
	observation->data.sound.hypothesis_count = 1U;
}

static void TestRuntimeHorizonScopeLifecycle(void)
{
	runtime_fixture_t fixture;
	sg_belief_runtime_provider_t provider;
	sg_perception_observation_t observation;
	const sg_belief_runtime_view_t *view;
	size_t allocations;
	uint64_t frame;

	FixtureInit(&fixture);
	provider = Provider(&fixture, 1U, 0.0f);
	CHECK(SG_BeliefRuntimeProviderSet(&provider));
	CHECK(SG_BeliefTestHorizonScopeLiveCount() == 0U);
	allocations = SG_BeliefTestHorizonScopeAllocationCount();
	SightObservation(&observation, 1U, 100U, 30U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	CHECK(SG_BeliefTestHorizonScopeLiveCount() == 1U);
	CHECK(SG_BeliefTestHorizonScopeAllocationCount() == allocations + 1U);
	view = SG_BeliefRuntimeViewForClient(1U, 3U);
	CHECK(view && view->updated_at_ms == 100U);
	SG_BeliefTestHorizonScopeFailNext(
		SG_BELIEF_HORIZON_ALLOCATION_FAILED);
	CHECK(SG_BeliefRuntimeFrame(2U, 200U) ==
		SG_BELIEF_RUNTIME_FRAME_CAPACITY);
	view = SG_BeliefRuntimeViewForClient(1U, 3U);
	CHECK(view && view->updated_at_ms == 100U);
	CHECK(SG_BeliefRuntimeFrame(2U, 200U) ==
		SG_BELIEF_RUNTIME_FRAME_APPLIED);
	view = SG_BeliefRuntimeViewForClient(1U, 3U);
	CHECK(view && view->updated_at_ms == 200U);
	SG_BeliefTestHorizonScopeFailNext(SG_BELIEF_HORIZON_OVERFLOW);
	CHECK(SG_BeliefRuntimeFrame(3U, 300U) ==
		SG_BELIEF_RUNTIME_FRAME_OVERFLOW);
	view = SG_BeliefRuntimeViewForClient(1U, 3U);
	CHECK(view && view->updated_at_ms == 200U);
	CHECK(SG_BeliefRuntimeFrame(3U, 300U) ==
		SG_BELIEF_RUNTIME_FRAME_APPLIED);
	for (frame = 4U; frame <= 1024U; frame++)
		CHECK(SG_BeliefRuntimeFrame(frame, frame * 100U) ==
			SG_BELIEF_RUNTIME_FRAME_APPLIED);
	CHECK(SG_BeliefTestHorizonScopeLiveCount() == 1U);
	CHECK(SG_BeliefTestHorizonScopeAllocationCount() == allocations + 1U);
	CHECK(SG_BeliefRuntimeProviderSet(NULL));
	CHECK(SG_BeliefTestHorizonScopeLiveCount() == 0U);
}

static void TestRuntimeReplacementIsAtomic(void)
{
	runtime_fixture_t fixture;
	sg_belief_runtime_provider_t provider;
	sg_perception_observation_t observation;
	sg_belief_life_identity_t old_life;
	sg_belief_life_identity_t new_life;
	const sg_belief_runtime_view_t *view;

	FixtureInit(&fixture);
	provider = Provider(&fixture, 1U, 0.5f);
	CHECK(SG_BeliefRuntimeProviderSet(&provider));
	SightObservation(&observation, 1U, 100U, 30U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	old_life = Life(3U, 30U);
	new_life = Life(3U, 31U);
	CHECK(SG_BeliefRuntimeView(1U, &old_life) != NULL);
	SightObservation(&observation, 2U, 150U, 31U);
	observation.authentication.authenticated_at_ms = 200U;
	observation.authentication.valid_until_ms = 300U;
	SG_BeliefTestHorizonScopeFailNext(
		SG_BELIEF_HORIZON_ALLOCATION_FAILED);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_CAPACITY);
	view = SG_BeliefRuntimeView(1U, &old_life);
	CHECK(view && view->updated_at_ms == 100U);
	CHECK(SG_BeliefRuntimeView(1U, &new_life) == NULL);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	CHECK(SG_BeliefRuntimeView(1U, &old_life) == NULL);
	CHECK(SG_BeliefRuntimeView(1U, &new_life) != NULL);
	CHECK(SG_BeliefRuntimeProviderSet(NULL));
}

static void TestRuntimeLifeFencePreventsResurrection(void)
{
	runtime_fixture_t fixture;
	sg_belief_runtime_provider_t provider;
	sg_perception_observation_t observation;
	sg_belief_life_identity_t life31;
	sg_belief_life_identity_t life32;
	sg_belief_life_identity_t life33;
	const sg_belief_runtime_view_t *view;

	FixtureInit(&fixture);
	provider = Provider(&fixture, 1U, 0.5f);
	CHECK(SG_BeliefRuntimeProviderSet(&provider));
	life31 = Life(3U, 31U);
	life32 = Life(3U, 32U);
	life33 = Life(3U, 33U);
	SightObservation(&observation, 1U, 200U, 31U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	/* Each audience owns its observation ordering.  This lower observed time
	 * is the first Team 2 track for the same authenticated life, not stale
	 * Team 1 evidence. */
	SightObservation(&observation, 2U, 100U, 31U);
	observation.authentication.issuer_team = 2U;
	observation.authentication.audience_team = 2U;
	observation.authentication.authenticated_at_ms = 300U;
	observation.authentication.valid_until_ms = 400U;
	observation.target_team = 1U;
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	view = SG_BeliefRuntimeViewForClient(2U, 3U);
	CHECK(view && SG_BeliefLifeIdentityEqual(&view->target_life, &life31));
	SightObservation(&observation, 2U, 150U, 30U);
	observation.authentication.authenticated_at_ms = 300U;
	observation.authentication.valid_until_ms = 400U;
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_REJECTED);
	view = SG_BeliefRuntimeViewForClient(1U, 3U);
	CHECK(view && SG_BeliefLifeIdentityEqual(&view->target_life, &life31));
	SightObservation(&observation, 3U, 250U, 32U);
	observation.authentication.authenticated_at_ms = 300U;
	observation.authentication.valid_until_ms = 400U;
	SG_BeliefTestHorizonScopeFailNext(
		SG_BELIEF_HORIZON_ALLOCATION_FAILED);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_CAPACITY);
	view = SG_BeliefRuntimeViewForClient(1U, 3U);
	CHECK(view && SG_BeliefLifeIdentityEqual(&view->target_life, &life31));
	view = SG_BeliefRuntimeViewForClient(2U, 3U);
	CHECK(view && SG_BeliefLifeIdentityEqual(&view->target_life, &life31));
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	view = SG_BeliefRuntimeViewForClient(1U, 3U);
	CHECK(view && SG_BeliefLifeIdentityEqual(&view->target_life, &life32));
	CHECK(SG_BeliefRuntimeViewForClient(2U, 3U) == NULL);
	CHECK(SG_BeliefRuntimeFrame(1U, 350U) ==
		SG_BELIEF_RUNTIME_FRAME_APPLIED);
	CHECK(SG_BeliefRuntimeViewForClient(2U, 3U) == NULL);
	SG_BeliefRuntimeRetireLife(&life32);
	CHECK(SG_BeliefRuntimeViewForClient(1U, 3U) == NULL);
	CHECK(SG_BeliefRuntimeFrame(2U, 600U) ==
		SG_BELIEF_RUNTIME_FRAME_APPLIED);
	CHECK(SG_BeliefRuntimeFrame(2U, 500U) ==
		SG_BELIEF_RUNTIME_FRAME_REJECTED);
	SightObservation(&observation, 4U, 700U, 32U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_REJECTED);
	SightObservation(&observation, 5U, 800U, 33U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	SG_BeliefRuntimeRetireLife(&life33);
	CHECK(SG_BeliefRuntimeViewForClient(1U, 3U) == NULL);
	CHECK(SG_BeliefRuntimeViewForClient(2U, 3U) == NULL);
	SightObservation(&observation, 6U, 900U, 33U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_REJECTED);
	SightObservation(&observation, 7U, 1000U, 34U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	CHECK(SG_BeliefRuntimeProviderSet(NULL));
}

static void TestRuntimeIssuerLifeFences(void)
{
	runtime_fixture_t fixture;
	sg_belief_runtime_provider_t provider;
	sg_perception_observation_t observation;
	sg_belief_life_identity_t issuer40;
	sg_belief_life_identity_t issuer41;
	sg_belief_life_identity_t target30;
	const sg_belief_runtime_view_t *view;

	issuer40 = Life(4U, 40U);
	issuer41 = Life(4U, 41U);
	target30 = Life(3U, 30U);
	/* Target and issuer can name the same exact life once.  A different
	 * generation in that same slot is rejected before it can build a track. */
	FixtureInit(&fixture);
	provider = Provider(&fixture, 1U, 0.5f);
	CHECK(SG_BeliefRuntimeProviderSet(&provider));
	SightObservation(&observation, 1U, 100U, 40U);
	observation.target_life = issuer40;
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	view = SG_BeliefRuntimeView(1U, &issuer40);
	CHECK(view && view->updated_at_ms == 100U);
	SightObservation(&observation, 2U, 200U, 40U);
	observation.target_life = issuer40;
	observation.authentication.issuer_life = issuer41;
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_REJECTED);
	view = SG_BeliefRuntimeView(1U, &issuer40);
	CHECK(view && view->updated_at_ms == 100U);
	CHECK(SG_BeliefRuntimeProviderSet(NULL));

	/* A life known only as an issuer still establishes an exact-life retirement
	 * boundary, so a later target claim cannot revive that life. */
	FixtureInit(&fixture);
	provider = Provider(&fixture, 1U, 0.5f);
	CHECK(SG_BeliefRuntimeProviderSet(&provider));
	SightObservation(&observation, 1U, 100U, 30U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	CHECK(SG_BeliefRuntimeView(1U, &target30) != NULL);
	SG_BeliefRuntimeRetireLife(&issuer40);
	CHECK(SG_BeliefRuntimeView(1U, &target30) == NULL);
	SightObservation(&observation, 2U, 200U, 40U);
	observation.target_life = issuer40;
	observation.authentication.issuer_life = Life(5U, 50U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_REJECTED);
	CHECK(SG_BeliefRuntimeView(1U, &issuer40) == NULL);
	SightObservation(&observation, 3U, 250U, 30U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_REJECTED);
	CHECK(SG_BeliefRuntimeProviderSet(NULL));

	/* A newer issuer life is not published until its observation succeeds.
	 * Once published, it invalidates an older target track for that issuer's
	 * client, and both stale and retired issuers are rejected thereafter. */
	FixtureInit(&fixture);
	provider = Provider(&fixture, 1U, 0.5f);
	CHECK(SG_BeliefRuntimeProviderSet(&provider));
	SightObservation(&observation, 1U, 100U, 40U);
	observation.target_life = issuer40;
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	CHECK(SG_BeliefRuntimeView(1U, &issuer40) != NULL);
	SightObservation(&observation, 2U, 110U, 30U);
	observation.authentication.issuer_life = issuer41;
	observation.authentication.authenticated_at_ms = 300U;
	observation.authentication.valid_until_ms = 400U;
	SG_BeliefTestHorizonScopeFailNext(
		SG_BELIEF_HORIZON_ALLOCATION_FAILED);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_CAPACITY);
	CHECK(SG_BeliefRuntimeView(1U, &issuer40) != NULL);
	CHECK(SG_BeliefRuntimeView(1U, &target30) == NULL);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	CHECK(SG_BeliefRuntimeView(1U, &issuer40) == NULL);
	view = SG_BeliefRuntimeView(1U, &target30);
	CHECK(view && view->updated_at_ms == 300U);
	SightObservation(&observation, 3U, 350U, 30U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_REJECTED);
	view = SG_BeliefRuntimeView(1U, &target30);
	CHECK(view && view->updated_at_ms == 300U);
	SG_BeliefRuntimeRetireLife(&issuer41);
	CHECK(SG_BeliefRuntimeView(1U, &target30) == NULL);
	SightObservation(&observation, 4U, 400U, 30U);
	observation.authentication.issuer_life = issuer41;
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_REJECTED);
	CHECK(SG_BeliefRuntimeProviderSet(NULL));
}

static void TestRuntimeIssuerGenerationInvalidatesTracks(void)
{
	runtime_fixture_t fixture;
	sg_belief_runtime_provider_t provider;
	sg_perception_observation_t observation;
	sg_belief_life_identity_t target30;
	const sg_belief_runtime_view_t *view;

	FixtureInit(&fixture);
	provider = Provider(&fixture, 1U, 0.5f);
	target30 = Life(3U, 30U);
	CHECK(SG_BeliefRuntimeProviderSet(&provider));
	SightObservation(&observation, 1U, 100U, 30U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	view = SG_BeliefRuntimeView(1U, &target30);
	CHECK(view && view->updated_at_ms == 100U);

	/* A rejected issuer generation cannot publish its fence or disturb the
	 * established target belief. */
	SightObservation(&observation, 2U, 200U, 31U);
	observation.target_life = Life(5U, 31U);
	observation.authentication.issuer_life = Life(4U, 41U);
	observation.authentication.authenticated_at_ms = 300U;
	observation.authentication.valid_until_ms = 400U;
	SG_BeliefTestHorizonScopeFailNext(
		SG_BELIEF_HORIZON_ALLOCATION_FAILED);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_CAPACITY);
	view = SG_BeliefRuntimeView(1U, &target30);
	CHECK(view && view->updated_at_ms == 100U);
	SightObservation(&observation, 3U, 160U, 30U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	CHECK(SG_BeliefRuntimeFrame(1U, 180U) ==
		SG_BELIEF_RUNTIME_FRAME_APPLIED);
	view = SG_BeliefRuntimeView(1U, &target30);
	CHECK(view && view->updated_at_ms == 180U);

	/* Publishing issuer 4/41 retires every track that recorded issuer 4/40.
	 * The later frame may advance the new track, never the stale one. */
	SightObservation(&observation, 4U, 220U, 31U);
	observation.target_life = Life(5U, 31U);
	observation.authentication.issuer_life = Life(4U, 41U);
	observation.authentication.authenticated_at_ms = 300U;
	observation.authentication.valid_until_ms = 400U;
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	CHECK(SG_BeliefRuntimeView(1U, &target30) == NULL);
	CHECK(SG_BeliefRuntimeFrame(2U, 300U) ==
		SG_BELIEF_RUNTIME_FRAME_APPLIED);
	CHECK(SG_BeliefRuntimeView(1U, &target30) == NULL);
	view = SG_BeliefRuntimeViewForClient(1U, 5U);
	CHECK(view && view->updated_at_ms == 300U);
	CHECK(SG_BeliefRuntimeProviderSet(NULL));
}

static void TestRuntimeIssuerGenerationReplacesSameTargetTrack(void)
{
	runtime_fixture_t fixture;
	sg_belief_runtime_provider_t provider;
	sg_perception_observation_t observation;
	sg_belief_life_identity_t target30;
	const sg_belief_runtime_view_t *view;

	FixtureInit(&fixture);
	provider = Provider(&fixture, 1U, 0.5f);
	target30 = Life(3U, 30U);
	CHECK(SG_BeliefRuntimeProviderSet(&provider));
	SightObservation(&observation, 1U, 100U, 30U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	view = SG_BeliefRuntimeView(1U, &target30);
	CHECK(view && view->updated_at_ms == 100U);

	SightObservation(&observation, 2U, 200U, 30U);
	observation.authentication.issuer_life = Life(4U, 41U);
	observation.authentication.authenticated_at_ms = 300U;
	observation.authentication.valid_until_ms = 400U;
	SG_BeliefTestHorizonScopeFailNext(
		SG_BELIEF_HORIZON_ALLOCATION_FAILED);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_CAPACITY);
	view = SG_BeliefRuntimeView(1U, &target30);
	CHECK(view && view->updated_at_ms == 100U);

	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	view = SG_BeliefRuntimeView(1U, &target30);
	CHECK(view && view->updated_at_ms == 300U);
	CHECK(SG_BeliefRuntimeFrame(1U, 350U) ==
		SG_BELIEF_RUNTIME_FRAME_APPLIED);
	view = SG_BeliefRuntimeView(1U, &target30);
	CHECK(view && view->updated_at_ms == 350U);
	CHECK(SG_BeliefRuntimeProviderSet(NULL));
}

static void TestRuntimeIssuerRolloverRejectsTimestampRollback(void)
{
	runtime_fixture_t fixture;
	sg_belief_runtime_provider_t provider;
	sg_perception_observation_t observation;
	sg_belief_life_identity_t target30;
	const sg_belief_runtime_view_t *view;

	FixtureInit(&fixture);
	provider = Provider(&fixture, 1U, 0.5f);
	target30 = Life(3U, 30U);
	CHECK(SG_BeliefRuntimeProviderSet(&provider));
	SightObservation(&observation, 1U, 500U, 30U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	SightObservation(&observation, 2U, 200U, 30U);
	observation.authentication.issuer_life = Life(4U, 41U);
	observation.authentication.authenticated_at_ms = 300U;
	observation.authentication.valid_until_ms = 400U;
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_REJECTED);
	view = SG_BeliefRuntimeView(1U, &target30);
	CHECK(view && view->updated_at_ms == 500U);
	CHECK(SG_BeliefRuntimeProviderSet(NULL));
}

static void TestRuntimeIssuerRolloverRejectsEvidenceSequenceRollback(void)
{
	runtime_fixture_t fixture;
	sg_belief_runtime_provider_t provider;
	sg_perception_observation_t observation;
	sg_belief_life_identity_t target30;
	const sg_belief_runtime_view_t *view;

	FixtureInit(&fixture);
	provider = Provider(&fixture, 1U, 0.5f);
	target30 = Life(3U, 30U);
	CHECK(SG_BeliefRuntimeProviderSet(&provider));
	SightObservation(&observation, 5U, 100U, 30U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	SightObservation(&observation, 4U, 200U, 30U);
	observation.authentication.issuer_life = Life(4U, 41U);
	observation.authentication.authenticated_at_ms = 300U;
	observation.authentication.valid_until_ms = 400U;
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_REJECTED);
	view = SG_BeliefRuntimeView(1U, &target30);
	CHECK(view && view->updated_at_ms == 100U);
	CHECK(SG_BeliefRuntimeProviderSet(NULL));
}

static void TestRuntimeFrameWatermarkRejectsIssuerRollover(void)
{
	runtime_fixture_t fixture;
	sg_belief_runtime_provider_t provider;
	sg_perception_observation_t observation;
	sg_belief_life_identity_t target30;
	const sg_belief_runtime_view_t *view;

	FixtureInit(&fixture);
	provider = Provider(&fixture, 1U, 0.5f);
	target30 = Life(3U, 30U);
	CHECK(SG_BeliefRuntimeProviderSet(&provider));
	SightObservation(&observation, 1U, 100U, 30U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	CHECK(SG_BeliefRuntimeFrame(1U, 500U) ==
		SG_BELIEF_RUNTIME_FRAME_APPLIED);
	view = SG_BeliefRuntimeView(1U, &target30);
	CHECK(view && view->updated_at_ms == 500U);
	SightObservation(&observation, 2U, 200U, 30U);
	observation.authentication.issuer_life = Life(4U, 41U);
	observation.authentication.authenticated_at_ms = 300U;
	observation.authentication.valid_until_ms = 400U;
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_REJECTED);
	view = SG_BeliefRuntimeView(1U, &target30);
	CHECK(view && view->updated_at_ms == 500U);
	CHECK(SG_BeliefRuntimeProviderSet(NULL));
}

static void TestRuntimeFrameWatermarkRejectsDelayedEvidence(void)
{
	runtime_fixture_t fixture;
	sg_belief_runtime_provider_t provider;
	sg_perception_observation_t observation;
	sg_belief_life_identity_t target30;
	const sg_belief_runtime_view_t *view;

	FixtureInit(&fixture);
	provider = Provider(&fixture, 1U, 0.5f);
	target30 = Life(3U, 30U);
	CHECK(SG_BeliefRuntimeProviderSet(&provider));
	SightObservation(&observation, 1U, 100U, 30U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	CHECK(SG_BeliefRuntimeFrame(1U, 500U) ==
		SG_BELIEF_RUNTIME_FRAME_APPLIED);
	view = SG_BeliefRuntimeView(1U, &target30);
	CHECK(view && view->updated_at_ms == 500U);
	SightObservation(&observation, 2U, 200U, 30U);
	observation.authentication.authenticated_at_ms = 300U;
	observation.authentication.valid_until_ms = 400U;
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_REJECTED);
	view = SG_BeliefRuntimeView(1U, &target30);
	CHECK(view && view->updated_at_ms == 500U);
	CHECK(SG_BeliefRuntimeProviderSet(NULL));
}

static void TestRuntimeFrameWatermarkRejectsTargetLifeRollover(void)
{
	runtime_fixture_t fixture;
	sg_belief_runtime_provider_t provider;
	sg_perception_observation_t observation;
	sg_belief_life_identity_t target30;
	sg_belief_life_identity_t target31;
	const sg_belief_runtime_view_t *view;

	FixtureInit(&fixture);
	provider = Provider(&fixture, 1U, 0.5f);
	target30 = Life(3U, 30U);
	target31 = Life(3U, 31U);
	CHECK(SG_BeliefRuntimeProviderSet(&provider));
	SightObservation(&observation, 1U, 100U, 30U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	CHECK(SG_BeliefRuntimeFrame(1U, 500U) ==
		SG_BELIEF_RUNTIME_FRAME_APPLIED);
	view = SG_BeliefRuntimeViewForClient(1U, 3U);
	CHECK(view && view->updated_at_ms == 500U);
	SightObservation(&observation, 2U, 200U, 31U);
	observation.authentication.authenticated_at_ms = 300U;
	observation.authentication.valid_until_ms = 400U;
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_REJECTED);
	view = SG_BeliefRuntimeViewForClient(1U, 3U);
	CHECK(view && SG_BeliefLifeIdentityEqual(&view->target_life, &target30));
	CHECK(view && view->updated_at_ms == 500U);
	SightObservation(&observation, 3U, 600U, 31U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	CHECK(SG_BeliefRuntimeView(1U, &target30) == NULL);
	view = SG_BeliefRuntimeView(1U, &target31);
	CHECK(view && view->updated_at_ms == 600U);
	CHECK(SG_BeliefRuntimeProviderSet(NULL));
}

static void TestRuntimeFrameTimestampRegressionResetsTracks(void)
{
	runtime_fixture_t fixture;
	sg_belief_runtime_provider_t provider;
	sg_perception_observation_t observation;
	sg_belief_life_identity_t target30;
	sg_belief_life_identity_t retired50;

	FixtureInit(&fixture);
	provider = Provider(&fixture, 1U, 0.5f);
	target30 = Life(3U, 30U);
	retired50 = Life(5U, 50U);
	CHECK(SG_BeliefRuntimeProviderSet(&provider));
	SightObservation(&observation, 1U, 500U, 30U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	CHECK(SG_BeliefRuntimeView(1U, &target30) != NULL);
	SG_BeliefRuntimeRetireLife(&retired50);
	CHECK(SG_BeliefRuntimeFrame(1U, 400U) ==
		SG_BELIEF_RUNTIME_FRAME_REJECTED);
	CHECK(SG_BeliefRuntimeView(1U, &target30) == NULL);
	CHECK(SG_BeliefRuntimeViewForClient(1U, 3U) == NULL);
	SightObservation(&observation, 2U, 600U, 50U);
	observation.target_life = retired50;
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_REJECTED);
	CHECK(SG_BeliefRuntimeProviderSet(NULL));
}

static void TestRuntimeAudienceClientWatermarkSurvivesRetirement(void)
{
	runtime_fixture_t fixture;
	sg_belief_runtime_provider_t provider;
	sg_perception_observation_t observation;
	sg_belief_life_identity_t target30;
	sg_belief_life_identity_t target31;
	const sg_belief_runtime_view_t *view;

	FixtureInit(&fixture);
	provider = Provider(&fixture, 1U, 0.5f);
	target30 = Life(3U, 30U);
	target31 = Life(3U, 31U);
	CHECK(SG_BeliefRuntimeProviderSet(&provider));
	SightObservation(&observation, 1U, 100U, 30U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	CHECK(SG_BeliefRuntimeFrame(1U, 500U) ==
		SG_BELIEF_RUNTIME_FRAME_APPLIED);
	SG_BeliefRuntimeRetireLife(&target30);
	CHECK(SG_BeliefRuntimeViewForClient(1U, 3U) == NULL);
	SightObservation(&observation, 2U, 200U, 31U);
	observation.authentication.authenticated_at_ms = 300U;
	observation.authentication.valid_until_ms = 400U;
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_REJECTED);
	CHECK(SG_BeliefRuntimeView(1U, &target31) == NULL);
	SightObservation(&observation, 3U, 600U, 31U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	view = SG_BeliefRuntimeView(1U, &target31);
	CHECK(view && view->updated_at_ms == 600U);
	CHECK(SG_BeliefRuntimeProviderSet(NULL));
}

static void TestRuntimeAudienceClientWatermarkSurvivesSupersession(void)
{
	runtime_fixture_t fixture;
	sg_belief_runtime_provider_t provider;
	sg_perception_observation_t observation;
	sg_belief_life_identity_t target31;
	const sg_belief_runtime_view_t *view;

	FixtureInit(&fixture);
	provider = Provider(&fixture, 1U, 0.5f);
	target31 = Life(3U, 31U);
	CHECK(SG_BeliefRuntimeProviderSet(&provider));
	SightObservation(&observation, 1U, 100U, 30U);
	observation.authentication.issuer_team = 2U;
	observation.authentication.audience_team = 2U;
	observation.target_team = 1U;
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	CHECK(SG_BeliefRuntimeFrame(1U, 500U) ==
		SG_BELIEF_RUNTIME_FRAME_APPLIED);
	view = SG_BeliefRuntimeViewForClient(2U, 3U);
	CHECK(view && view->updated_at_ms == 500U);
	SightObservation(&observation, 2U, 600U, 31U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	CHECK(SG_BeliefRuntimeViewForClient(2U, 3U) == NULL);
	SightObservation(&observation, 3U, 200U, 31U);
	observation.authentication.issuer_team = 2U;
	observation.authentication.audience_team = 2U;
	observation.authentication.authenticated_at_ms = 300U;
	observation.authentication.valid_until_ms = 400U;
	observation.target_team = 1U;
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_REJECTED);
	SightObservation(&observation, 4U, 600U, 31U);
	observation.authentication.issuer_team = 2U;
	observation.authentication.audience_team = 2U;
	observation.target_team = 1U;
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	view = SG_BeliefRuntimeView(2U, &target31);
	CHECK(view && view->updated_at_ms == 600U);
	CHECK(SG_BeliefRuntimeProviderSet(NULL));
}

static void TestRuntimeAudienceClientWatermarkAdvancesEmptyFrame(void)
{
	runtime_fixture_t fixture;
	sg_belief_runtime_provider_t provider;
	sg_perception_observation_t observation;
	sg_belief_life_identity_t target31;

	FixtureInit(&fixture);
	provider = Provider(&fixture, 1U, 0.5f);
	target31 = Life(3U, 31U);
	CHECK(SG_BeliefRuntimeProviderSet(&provider));
	SightObservation(&observation, 1U, 100U, 30U);
	observation.evidence_kind = SG_BELIEF_EVIDENCE_NEGATIVE;
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	CHECK(SG_BeliefRuntimeViewForClient(1U, 3U) == NULL);
	CHECK(SG_BeliefRuntimeFrame(1U, 500U) ==
		SG_BELIEF_RUNTIME_FRAME_APPLIED);
	CHECK(SG_BeliefRuntimeViewForClient(1U, 3U) == NULL);
	SightObservation(&observation, 2U, 200U, 31U);
	observation.authentication.authenticated_at_ms = 300U;
	observation.authentication.valid_until_ms = 400U;
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_REJECTED);
	CHECK(SG_BeliefRuntimeView(1U, &target31) == NULL);
	CHECK(SG_BeliefRuntimeProviderSet(NULL));
}

static void TestRuntimeAudienceClientWatermarkFailureDoesNotAdvance(void)
{
	runtime_fixture_t fixture;
	sg_belief_runtime_provider_t provider;
	sg_perception_observation_t observation;
	sg_belief_life_identity_t target30;
	sg_belief_life_identity_t target31;
	const sg_belief_runtime_view_t *view;

	FixtureInit(&fixture);
	provider = Provider(&fixture, 1U, 0.5f);
	target30 = Life(3U, 30U);
	target31 = Life(3U, 31U);
	CHECK(SG_BeliefRuntimeProviderSet(&provider));
	SightObservation(&observation, 1U, 100U, 30U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	CHECK(SG_BeliefRuntimeFrame(1U, 500U) ==
		SG_BELIEF_RUNTIME_FRAME_APPLIED);
	SG_BeliefRuntimeRetireLife(&target30);
	SightObservation(&observation, 2U, 550U, 31U);
	observation.authentication.authenticated_at_ms = 600U;
	observation.authentication.valid_until_ms = 700U;
	SG_BeliefTestHorizonScopeFailNext(
		SG_BELIEF_HORIZON_ALLOCATION_FAILED);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_CAPACITY);
	SightObservation(&observation, 3U, 550U, 31U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	view = SG_BeliefRuntimeView(1U, &target31);
	CHECK(view && view->updated_at_ms == 550U);
	CHECK(SG_BeliefRuntimeProviderSet(NULL));
}

static void TestRuntimeAudienceClientWatermarkSurvivesProviderReplacement(void)
{
	runtime_fixture_t fixture;
	sg_belief_runtime_provider_t provider;
	sg_perception_observation_t observation;
	sg_belief_life_identity_t target32;
	const sg_belief_runtime_view_t *view;

	FixtureInit(&fixture);
	provider = Provider(&fixture, 1U, 0.5f);
	target32 = Life(3U, 32U);
	CHECK(SG_BeliefRuntimeProviderSet(&provider));
	SightObservation(&observation, 1U, 100U, 30U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	CHECK(SG_BeliefRuntimeFrame(1U, 500U) ==
		SG_BELIEF_RUNTIME_FRAME_APPLIED);
	CHECK(SG_BeliefRuntimeProviderSet(&provider));
	SightObservation(&observation, 2U, 200U, 31U);
	observation.authentication.authenticated_at_ms = 300U;
	observation.authentication.valid_until_ms = 400U;
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_REJECTED);
	provider.policy.diffusion_fraction = 0.25f;
	CHECK(SG_BeliefRuntimeProviderSet(&provider));
	SightObservation(&observation, 3U, 200U, 32U);
	observation.authentication.authenticated_at_ms = 300U;
	observation.authentication.valid_until_ms = 400U;
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_REJECTED);
	SightObservation(&observation, 4U, 600U, 32U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	view = SG_BeliefRuntimeView(1U, &target32);
	CHECK(view && view->updated_at_ms == 600U);
	CHECK(SG_BeliefRuntimeProviderSet(NULL));
}

static void TestRuntimeAudienceClientWatermarkResetStartsUniverse(void)
{
	runtime_fixture_t fixture;
	sg_belief_runtime_provider_t provider;
	sg_perception_observation_t observation;
	sg_belief_life_identity_t target30;
	const sg_belief_runtime_view_t *view;

	FixtureInit(&fixture);
	provider = Provider(&fixture, 1U, 0.5f);
	target30 = Life(3U, 30U);
	CHECK(SG_BeliefRuntimeProviderSet(&provider));
	SightObservation(&observation, 1U, 100U, 30U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	CHECK(SG_BeliefRuntimeFrame(1U, 500U) ==
		SG_BELIEF_RUNTIME_FRAME_APPLIED);
	SG_BeliefRuntimeReset();
	SightObservation(&observation, 2U, 200U, 30U);
	observation.authentication.authenticated_at_ms = 300U;
	observation.authentication.valid_until_ms = 400U;
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	view = SG_BeliefRuntimeView(1U, &target30);
	CHECK(view && view->updated_at_ms == 300U);
	CHECK(SG_BeliefRuntimeProviderSet(NULL));
}

static void TestRuntimeAudienceTargetSequenceSurvivesIssuerRetirement(void)
{
	runtime_fixture_t fixture;
	sg_belief_runtime_provider_t provider;
	sg_perception_observation_t observation;
	sg_belief_life_identity_t issuer40;
	sg_belief_life_identity_t target30;

	FixtureInit(&fixture);
	provider = Provider(&fixture, 1U, 0.5f);
	issuer40 = Life(4U, 40U);
	target30 = Life(3U, 30U);
	CHECK(SG_BeliefRuntimeProviderSet(&provider));
	SightObservation(&observation, 5U, 100U, 30U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	SG_BeliefRuntimeRetireLife(&issuer40);
	CHECK(SG_BeliefRuntimeViewForClient(1U, 3U) == NULL);
	SightObservation(&observation, 4U, 200U, 30U);
	observation.authentication.issuer_life = Life(4U, 41U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_REJECTED);
	CHECK(SG_BeliefRuntimeView(1U, &target30) == NULL);
	CHECK(SG_BeliefRuntimeProviderSet(NULL));
}

static void TestRuntimeAudienceTargetSequenceSurvivesProviderReplacement(void)
{
	runtime_fixture_t fixture;
	sg_belief_runtime_provider_t provider;
	sg_perception_observation_t observation;
	sg_belief_life_identity_t target30;

	FixtureInit(&fixture);
	provider = Provider(&fixture, 1U, 0.5f);
	target30 = Life(3U, 30U);
	CHECK(SG_BeliefRuntimeProviderSet(&provider));
	SightObservation(&observation, 5U, 100U, 30U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	CHECK(SG_BeliefRuntimeProviderSet(&provider));
	CHECK(SG_BeliefRuntimeViewForClient(1U, 3U) == NULL);
	SightObservation(&observation, 4U, 200U, 30U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_REJECTED);
	CHECK(SG_BeliefRuntimeView(1U, &target30) == NULL);
	CHECK(SG_BeliefRuntimeProviderSet(NULL));
}

static void TestRuntimeAudienceTargetSequenceSurvivesFrameRegression(void)
{
	runtime_fixture_t fixture;
	sg_belief_runtime_provider_t provider;
	sg_perception_observation_t observation;
	sg_belief_life_identity_t target30;

	FixtureInit(&fixture);
	provider = Provider(&fixture, 1U, 0.5f);
	target30 = Life(3U, 30U);
	CHECK(SG_BeliefRuntimeProviderSet(&provider));
	SightObservation(&observation, 5U, 500U, 30U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	CHECK(SG_BeliefRuntimeFrame(1U, 400U) ==
		SG_BELIEF_RUNTIME_FRAME_REJECTED);
	CHECK(SG_BeliefRuntimeViewForClient(1U, 3U) == NULL);
	SightObservation(&observation, 4U, 600U, 30U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_REJECTED);
	CHECK(SG_BeliefRuntimeView(1U, &target30) == NULL);
	CHECK(SG_BeliefRuntimeProviderSet(NULL));
}

static void TestRuntimeAudienceTargetSequenceResetsForNewGeneration(void)
{
	runtime_fixture_t fixture;
	sg_belief_runtime_provider_t provider;
	sg_perception_observation_t observation;
	sg_belief_life_identity_t target31;
	const sg_belief_runtime_view_t *view;

	FixtureInit(&fixture);
	provider = Provider(&fixture, 1U, 0.5f);
	target31 = Life(3U, 31U);
	CHECK(SG_BeliefRuntimeProviderSet(&provider));
	SightObservation(&observation, 5U, 100U, 30U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	SightObservation(&observation, 4U, 200U, 31U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	view = SG_BeliefRuntimeView(1U, &target31);
	CHECK(view && view->updated_at_ms == 200U);
	CHECK(SG_BeliefRuntimeProviderSet(NULL));
}

static void TestRuntimeAudienceTargetSequenceStaysAudienceScoped(void)
{
	runtime_fixture_t fixture;
	sg_belief_runtime_provider_t provider;
	sg_perception_observation_t observation;
	sg_belief_life_identity_t target30;
	const sg_belief_runtime_view_t *view;

	FixtureInit(&fixture);
	provider = Provider(&fixture, 1U, 0.5f);
	target30 = Life(3U, 30U);
	CHECK(SG_BeliefRuntimeProviderSet(&provider));
	SightObservation(&observation, 5U, 100U, 30U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	SightObservation(&observation, 4U, 200U, 30U);
	observation.authentication.issuer_team = 2U;
	observation.authentication.audience_team = 2U;
	observation.target_team = 1U;
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	view = SG_BeliefRuntimeView(2U, &target30);
	CHECK(view && view->updated_at_ms == 200U);
	CHECK(SG_BeliefRuntimeProviderSet(NULL));
}

static void TestRuntimeAudienceTargetSequenceResetStartsUniverse(void)
{
	runtime_fixture_t fixture;
	sg_belief_runtime_provider_t provider;
	sg_perception_observation_t observation;
	sg_belief_life_identity_t target30;
	const sg_belief_runtime_view_t *view;

	FixtureInit(&fixture);
	provider = Provider(&fixture, 1U, 0.5f);
	target30 = Life(3U, 30U);
	CHECK(SG_BeliefRuntimeProviderSet(&provider));
	SightObservation(&observation, 5U, 100U, 30U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	SG_BeliefRuntimeReset();
	SightObservation(&observation, 4U, 200U, 30U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	view = SG_BeliefRuntimeView(1U, &target30);
	CHECK(view && view->updated_at_ms == 200U);
	CHECK(SG_BeliefRuntimeProviderSet(NULL));
}

static void TestRuntimeAudienceTargetSequenceRejectedObserveDoesNotAdvance(void)
{
	runtime_fixture_t fixture;
	sg_belief_runtime_provider_t provider;
	sg_perception_observation_t observation;
	sg_belief_life_identity_t target30;
	const sg_belief_runtime_view_t *view;

	FixtureInit(&fixture);
	provider = Provider(&fixture, 1U, 0.5f);
	target30 = Life(3U, 30U);
	CHECK(SG_BeliefRuntimeProviderSet(&provider));
	SightObservation(&observation, 3U, 100U, 30U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	SightObservation(&observation, 5U, 150U, 30U);
	observation.authentication.authenticated_at_ms = 200U;
	observation.authentication.valid_until_ms = 199U;
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_REJECTED);
	SightObservation(&observation, 4U, 200U, 30U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	view = SG_BeliefRuntimeView(1U, &target30);
	CHECK(view && view->updated_at_ms == 200U);
	CHECK(SG_BeliefRuntimeProviderSet(NULL));
}

static void TestRuntimeAudienceTargetSequenceFailureDoesNotAdvance(void)
{
	const sg_belief_horizon_accept_result_t failures_to_inject[] = {
		SG_BELIEF_HORIZON_ALLOCATION_FAILED,
		SG_BELIEF_HORIZON_OVERFLOW
	};
	size_t index;

	for (index = 0U; index < sizeof(failures_to_inject) /
		sizeof(failures_to_inject[0]); index++)
	{
		runtime_fixture_t fixture;
		sg_belief_runtime_provider_t provider;
		sg_perception_observation_t observation;
		sg_belief_life_identity_t target30;
		const sg_belief_runtime_view_t *view;

		FixtureInit(&fixture);
		provider = Provider(&fixture, 1U, 0.5f);
		target30 = Life(3U, 30U);
		CHECK(SG_BeliefRuntimeProviderSet(&provider));
		SightObservation(&observation, 3U, 100U, 30U);
		CHECK(SG_BeliefRuntimeObserve(&observation) ==
			SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
		SightObservation(&observation, 5U, 150U, 30U);
		observation.authentication.authenticated_at_ms = 200U;
		observation.authentication.valid_until_ms = 300U;
		SG_BeliefTestHorizonScopeFailNext(failures_to_inject[index]);
		CHECK(SG_BeliefRuntimeObserve(&observation) ==
			(failures_to_inject[index] ==
			 SG_BELIEF_HORIZON_ALLOCATION_FAILED ?
				SG_BELIEF_RUNTIME_OBSERVE_CAPACITY :
				SG_BELIEF_RUNTIME_OBSERVE_OVERFLOW));
		SightObservation(&observation, 4U, 200U, 30U);
		CHECK(SG_BeliefRuntimeObserve(&observation) ==
			SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
		view = SG_BeliefRuntimeView(1U, &target30);
		CHECK(view && view->updated_at_ms == 200U);
		CHECK(SG_BeliefRuntimeProviderSet(NULL));
	}
}

static void TestRuntimeFrameIsAtomic(void)
{
	const sg_belief_horizon_accept_result_t failures_to_inject[] = {
		SG_BELIEF_HORIZON_ALLOCATION_FAILED,
		SG_BELIEF_HORIZON_OVERFLOW
	};
	runtime_fixture_t fixture;
	sg_belief_runtime_provider_t provider;
	sg_perception_observation_t observation;
	const sg_belief_runtime_view_t *first;
	const sg_belief_runtime_view_t *second;
	size_t kind;
	size_t failure_index;

	for (kind = 0U; kind < sizeof(failures_to_inject) /
		sizeof(failures_to_inject[0]); kind++)
		for (failure_index = 0U; failure_index < 2U; failure_index++)
		{
			FixtureInit(&fixture);
			provider = Provider(&fixture, 1U, 0.5f);
			CHECK(SG_BeliefRuntimeProviderSet(&provider));
			SightObservation(&observation, 1U, 100U, 30U);
			CHECK(SG_BeliefRuntimeObserve(&observation) ==
				SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
			SightObservation(&observation, 2U, 100U, 30U);
			observation.target_life = Life(5U, 30U);
			CHECK(SG_BeliefRuntimeObserve(&observation) ==
				SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
			SG_BeliefTestHorizonScopeFailAfter(failure_index,
				failures_to_inject[kind]);
			CHECK(SG_BeliefRuntimeFrame(2U, 200U) ==
				(failures_to_inject[kind] ==
				 SG_BELIEF_HORIZON_ALLOCATION_FAILED ?
					SG_BELIEF_RUNTIME_FRAME_CAPACITY :
					SG_BELIEF_RUNTIME_FRAME_OVERFLOW));
			first = SG_BeliefRuntimeViewForClient(1U, 3U);
			second = SG_BeliefRuntimeViewForClient(1U, 5U);
			CHECK(first && first->updated_at_ms == 100U);
			CHECK(second && second->updated_at_ms == 100U);
			CHECK(SG_BeliefRuntimeFrame(2U, 200U) ==
				SG_BELIEF_RUNTIME_FRAME_APPLIED);
			first = SG_BeliefRuntimeViewForClient(1U, 3U);
			second = SG_BeliefRuntimeViewForClient(1U, 5U);
			CHECK(first && first->updated_at_ms == 200U);
			CHECK(second && second->updated_at_ms == 200U);
			CHECK(SG_BeliefRuntimeProviderSet(NULL));
		}
}

static void TestRuntimeRejectedObservationPreservesTrack(void)
{
	runtime_fixture_t fixture;
	sg_belief_runtime_provider_t provider;
	sg_perception_observation_t observation;
	sg_belief_life_identity_t rejected_issuer;
	const sg_belief_runtime_view_t *view;

	FixtureInit(&fixture);
	provider = Provider(&fixture, 1U, 0.5f);
	CHECK(SG_BeliefRuntimeProviderSet(&provider));
	SightObservation(&observation, 1U, 100U, 30U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	rejected_issuer = Life(5U, 50U);
	SightObservation(&observation, 2U, 150U, 30U);
	observation.authentication.authenticated_at_ms = 200U;
	observation.authentication.valid_until_ms = 300U;
	observation.authentication.issuer_life = rejected_issuer;
	SG_BeliefTestHorizonScopeFailNext(
		SG_BELIEF_HORIZON_ALLOCATION_FAILED);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_CAPACITY);
	view = SG_BeliefRuntimeViewForClient(1U, 3U);
	CHECK(view && view->updated_at_ms == 100U);
	SG_BeliefRuntimeRetireLife(&rejected_issuer);
	CHECK(SG_BeliefRuntimeViewForClient(1U, 3U) != NULL);
	SightObservation(&observation, 3U, 50U, 30U);
	observation.authentication.authenticated_at_ms = 300U;
	observation.authentication.valid_until_ms = 400U;
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_REJECTED);
	view = SG_BeliefRuntimeViewForClient(1U, 3U);
	CHECK(view && view->updated_at_ms == 100U);
	CHECK(SG_BeliefRuntimeProviderSet(NULL));
}

static void TestRuntimeLocatorProviderChangeRejected(void)
{
	runtime_fixture_t fixture;
	sg_belief_runtime_provider_t provider;
	sg_belief_runtime_pose_t pose;
	sg_perception_hypothesis_t hypothesis;
	sg_perception_hypothesis_t expected;

	FixtureInit(&fixture);
	provider = Provider(&fixture, 1U, 0.5f);
	replacement_provider = Provider(&fixture, 2U, 0.5f);
	provider.locate = LocateReplacingProvider;
	CHECK(SG_BeliefRuntimeProviderSet(&provider));
	memset(&pose, 0, sizeof(pose));
	pose.movement_state = SG_BELIEF_MOTION_GROUND;
	memset(&hypothesis, 0xa5, sizeof(hypothesis));
	expected = hypothesis;
	CHECK(!SG_BeliefRuntimeHypothesis(&pose, &hypothesis));
	CHECK(memcmp(&hypothesis, &expected, sizeof(hypothesis)) == 0);
	CHECK(SG_BeliefRuntimeProviderAvailable());
	CHECK(SG_BeliefRuntimeProviderSet(NULL));
}

static void TestRuntimeProviderReplacementIsTransactional(void)
{
	runtime_fixture_t fixture;
	sg_belief_runtime_provider_t provider;
	sg_belief_runtime_provider_t invalid;
	sg_belief_runtime_pose_t pose;
	sg_perception_hypothesis_t hypothesis;
	sg_perception_hypothesis_t expected;
	sg_perception_observation_t observation;

	FixtureInit(&fixture);
	provider = Provider(&fixture, 1U, 0.5f);
	CHECK(SG_BeliefRuntimeProviderSet(&provider));
	SightObservation(&observation, 1U, 100U, 30U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	SightObservation(&observation, 2U, 100U, 30U);
	observation.target_life = Life(5U, 30U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	invalid = provider;
	invalid.localization_generation = 0U;
	CHECK(!SG_BeliefRuntimeProviderSet(&invalid));
	CHECK(SG_BeliefRuntimeSnapshot() == &fixture.snapshot);
	CHECK(SG_BeliefRuntimeViewForClient(1U, 3U) != NULL);
	CHECK(SG_BeliefRuntimeViewForClient(1U, 5U) != NULL);
	CHECK(SG_BeliefRuntimeProviderSet(NULL));

	provider = Provider(&fixture, 1U, 0.5f);
	provider.locate = LocateReplacingProvider;
	replacement_provider = provider;
	CHECK(SG_BeliefRuntimeProviderSet(&provider));
	memset(&pose, 0, sizeof(pose));
	pose.movement_state = SG_BELIEF_MOTION_GROUND;
	memset(&hypothesis, 0xa5, sizeof(hypothesis));
	expected = hypothesis;
	CHECK(!SG_BeliefRuntimeHypothesis(&pose, &hypothesis));
	CHECK(memcmp(&hypothesis, &expected, sizeof(hypothesis)) == 0);
	CHECK(SG_BeliefRuntimeProviderAvailable());
	CHECK(SG_BeliefRuntimeProviderSet(NULL));

	provider = Provider(&fixture, 1U, 0.5f);
	provider.locate = LocateReplacingProviderTwice;
	replacement_provider = Provider(&fixture, 2U, 0.5f);
	restored_provider = provider;
	CHECK(SG_BeliefRuntimeProviderSet(&provider));
	memset(&hypothesis, 0xa5, sizeof(hypothesis));
	expected = hypothesis;
	CHECK(!SG_BeliefRuntimeHypothesis(&pose, &hypothesis));
	CHECK(memcmp(&hypothesis, &expected, sizeof(hypothesis)) == 0);
	CHECK(SG_BeliefRuntimeProviderAvailable());
	CHECK(SG_BeliefRuntimeProviderSet(NULL));

	provider = Provider(&fixture, 1U, 0.5f);
	provider.locate = LocateReplacingProvider;
	replacement_provider = provider;
	replacement_provider.policy.diffusion_fraction = 0.25f;
	CHECK(SG_BeliefRuntimeProviderSet(&provider));
	memset(&hypothesis, 0xa5, sizeof(hypothesis));
	expected = hypothesis;
	CHECK(!SG_BeliefRuntimeHypothesis(&pose, &hypothesis));
	CHECK(memcmp(&hypothesis, &expected, sizeof(hypothesis)) == 0);
	CHECK(SG_BeliefRuntimeProviderAvailable());
	CHECK(SG_BeliefRuntimeProviderSet(NULL));

	provider = Provider(&fixture, 1U, 0.5f);
	CHECK(SG_BeliefRuntimeProviderSet(&provider));
	SightObservation(&observation, 3U, 300U, 30U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	SG_BeliefTestRuntimeProviderEpochExhaust();
	invalid = provider;
	invalid.policy.diffusion_fraction = 0.25f;
	CHECK(!SG_BeliefRuntimeProviderSet(&invalid));
	CHECK(SG_BeliefRuntimeSnapshot() == &fixture.snapshot);
	CHECK(SG_BeliefRuntimeViewForClient(1U, 3U) != NULL);
	CHECK(SG_BeliefRuntimeProviderSet(NULL));
}

static void TestRuntimeProviderReplacementPreservesLifeFences(void)
{
	runtime_fixture_t fixture;
	sg_belief_runtime_provider_t provider;
	sg_perception_observation_t observation;
	sg_belief_life_identity_t target30;

	FixtureInit(&fixture);
	provider = Provider(&fixture, 1U, 0.5f);
	target30 = Life(3U, 30U);
	CHECK(SG_BeliefRuntimeProviderSet(&provider));
	SightObservation(&observation, 1U, 100U, 30U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	SG_BeliefRuntimeRetireLife(&target30);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_REJECTED);
	CHECK(SG_BeliefRuntimeProviderSet(&provider));
	CHECK(SG_BeliefRuntimeViewForClient(1U, 3U) == NULL);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_REJECTED);
	CHECK(SG_BeliefRuntimeViewForClient(1U, 3U) == NULL);
	CHECK(SG_BeliefRuntimeProviderSet(NULL));
}

static void TestRuntimeDelayedEvidenceConverges(void)
{
	runtime_fixture_t fixture;
	sg_belief_runtime_provider_t provider;
	sg_perception_observation_t observation;
	sg_perception_hypothesis_t hypothesis;
	sg_belief_particle_t stepped[64];
	sg_belief_particle_t delayed[64];
	const sg_belief_runtime_view_t *view;
	size_t stepped_count = 0U;
	size_t delayed_count = 0U;
	size_t index;
	int stepped_diffused = 0;

	FixtureInit(&fixture);
	provider = Provider(&fixture, 1U, 0.5f);
	SightObservation(&observation, 1U, 100U, 30U);
	/* Start a known life with no positive mass.  The later sound is therefore
	 * the only possible source of a non-origin mode at authentication time. */
	observation.evidence_kind = SG_BELIEF_EVIDENCE_NEGATIVE;
	hypothesis = Hypothesis(0U, 0U, SG_PERCEPTION_LOCATION_EARNED_RUNTIME,
		32.0f);
	CHECK(SG_BeliefRuntimeProviderSet(&provider));
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	CHECK(SG_BeliefRuntimeFrame(2U, 200U) ==
		SG_BELIEF_RUNTIME_FRAME_APPLIED);
	SoundObservation(&observation, 2U, 300U, 30U, &hypothesis);
	observation.authentication.observed_at_ms = 200U;
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	view = SG_BeliefRuntimeViewForClient(1U, 3U);
	CHECK(view && view->particle_count <= 64U);
	if (view && view->particle_count <= 64U)
	{
		stepped_count = view->particle_count;
		memcpy(stepped, view->particles,
			stepped_count * sizeof(*stepped));
	}
	for (index = 0U; index < stepped_count; index++)
		if (stepped[index].phase.phase_id != 0U)
			stepped_diffused = 1;
	CHECK(stepped_count > 1U);
	CHECK(stepped_diffused);
	CHECK(SG_BeliefRuntimeProviderSet(NULL));
	CHECK(SG_BeliefRuntimeProviderSet(&provider));
	SightObservation(&observation, 1U, 100U, 30U);
	observation.evidence_kind = SG_BELIEF_EVIDENCE_NEGATIVE;
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	SoundObservation(&observation, 2U, 300U, 30U, &hypothesis);
	observation.authentication.observed_at_ms = 200U;
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	view = SG_BeliefRuntimeViewForClient(1U, 3U);
	CHECK(view && view->particle_count <= 64U);
	if (view && view->particle_count <= 64U)
	{
		delayed_count = view->particle_count;
		memcpy(delayed, view->particles,
			delayed_count * sizeof(*delayed));
	}
	CHECK(stepped_count == delayed_count);
	for (index = 0U; index < stepped_count && index < delayed_count; index++)
	{
		CHECK(stepped[index].phase.phase_id == delayed[index].phase.phase_id);
		CHECK(stepped[index].phase.cell_id == delayed[index].phase.cell_id);
		CHECK(stepped[index].future_time_ms == delayed[index].future_time_ms);
		CHECK(fabsf(stepped[index].weight - delayed[index].weight) < 0.0001f);
	}
	CHECK(SG_BeliefRuntimeProviderSet(NULL));
}

static int RuntimeParticlesEqual(const sg_belief_particle_t *left,
	const sg_belief_particle_t *right)
{
	size_t axis;

	if (!left || !right || left->phase.phase_id != right->phase.phase_id ||
		left->phase.cell_id != right->phase.cell_id ||
		left->movement_state != right->movement_state ||
		left->weapon_state != right->weapon_state ||
		left->reserved != right->reserved ||
		left->source_mask != right->source_mask ||
		left->reserved2 != right->reserved2 ||
		left->future_time_ms != right->future_time_ms ||
		left->latest_evidence_id != right->latest_evidence_id ||
		left->latest_evidence_at_ms != right->latest_evidence_at_ms ||
		left->spread_radius != right->spread_radius ||
		left->weight != right->weight)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (left->position[axis] != right->position[axis] ||
			left->velocity[axis] != right->velocity[axis] ||
			left->acceleration[axis] != right->acceleration[axis] ||
			left->orientation[axis] != right->orientation[axis])
			return 0;
	return 1;
}

static void TestRuntimeFuturePredictionQuery(void)
{
	runtime_fixture_t fixture;
	sg_belief_runtime_provider_t provider;
	sg_perception_observation_t observation;
	sg_belief_life_identity_t target;
	sg_belief_particle_t scratch_first[128];
	sg_belief_particle_t scratch_second[128];
	sg_belief_particle_t predicted_first[128];
	sg_belief_particle_t predicted_second[128];
	sg_belief_particle_t predicted_small[128];
	sg_belief_particle_t predicted_small_before[128];
	sg_belief_particle_t before[128];
	sg_belief_prediction_t probe;
	sg_belief_prediction_t first;
	sg_belief_prediction_t second;
	sg_belief_prediction_t small;
	sg_belief_prediction_t rejected;
	sg_belief_prediction_t rejected_before;
	const sg_belief_runtime_view_t *view;
	size_t index;
	size_t required_particles;
	size_t small_capacity;
	float weight_sum;
	int diffused;

	FixtureInit(&fixture);
	provider = Provider(&fixture, 1U, 0.5f);
	target = Life(3U, 30U);
	memset(before, 0, sizeof(before));
	CHECK(SG_BeliefRuntimeProviderSet(&provider));
	SightObservation(&observation, 1U, 100U, 30U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	view = SG_BeliefRuntimeViewForClient(1U, 3U);
	CHECK(view && view->particle_count <= sizeof(before) / sizeof(before[0]));
	if (view && view->particle_count <= sizeof(before) / sizeof(before[0]))
		memcpy(before, view->particles,
			view->particle_count * sizeof(*before));

	/* Probe the accepted prediction contract before supplying destination
	 * storage.  The runtime must not silently truncate a sparse frontier. */
	memset(&probe, 0, sizeof(probe));
	CHECK(SG_BeliefRuntimePredict(1U, &target, 200U, scratch_first,
		scratch_second, sizeof(scratch_first) / sizeof(scratch_first[0]),
		NULL, 0U, &probe) == SG_BELIEF_RUNTIME_PREDICT_CAPACITY);
	required_particles = probe.required_particle_capacity;
	CHECK(required_particles > 0U);
	CHECK(probe.particle_count == 0U);
	CHECK(probe.at_time_ms == 200U);
	CHECK(probe.target_life.spawn_generation == 30U);
	CHECK(required_particles <= sizeof(predicted_first) /
		sizeof(predicted_first[0]));

	/* A short destination reports capacity and leaves its existing contents
	 * untouched, while scratch remains disposable as documented. */
	memset(predicted_small, 0xa5, sizeof(predicted_small));
	memcpy(predicted_small_before, predicted_small,
		sizeof(predicted_small_before));
	small_capacity = required_particles > 1U ? required_particles - 1U : 0U;
	memset(&small, 0, sizeof(small));
	CHECK(SG_BeliefRuntimePredict(1U, &target, 200U, scratch_first,
		scratch_second, sizeof(scratch_first) / sizeof(scratch_first[0]),
		small_capacity != 0U ? predicted_small : NULL, small_capacity,
		&small) == SG_BELIEF_RUNTIME_PREDICT_CAPACITY);
	CHECK(small.required_particle_capacity == required_particles);
	CHECK(memcmp(predicted_small, predicted_small_before,
		sizeof(predicted_small)) == 0);

	memset(&first, 0, sizeof(first));
	CHECK(SG_BeliefRuntimePredict(1U, &target, 200U, scratch_first,
		scratch_second, sizeof(scratch_first) / sizeof(scratch_first[0]),
		predicted_first, sizeof(predicted_first) / sizeof(predicted_first[0]),
		&first) == SG_BELIEF_RUNTIME_PREDICT_APPLIED);
	CHECK(first.particle_count == required_particles);
	CHECK(first.particle_count > 1U);
	CHECK(first.total_weight > 0.9999f && first.total_weight < 1.0001f);
	weight_sum = 0.0f;
	diffused = 0;
	for (index = 0U; index < first.particle_count; index++)
	{
		weight_sum += predicted_first[index].weight;
		CHECK(predicted_first[index].future_time_ms == 200U);
		if (predicted_first[index].phase.phase_id != 0U)
			diffused = 1;
	}
	CHECK(weight_sum > 0.9999f && weight_sum < 1.0001f);
	CHECK(diffused);
	CHECK(first.source.horizon_chain_identity.bytes[0] != 0U ||
		first.source.horizon_chain_identity.bytes[1] != 0U);

	/* Repeating the same query yields the same normalized sparse modes.  The
	 * issuance metadata may advance, but it cannot affect the distribution. */
	memset(&second, 0, sizeof(second));
	CHECK(SG_BeliefRuntimePredict(1U, &target, 200U, scratch_first,
		scratch_second, sizeof(scratch_first) / sizeof(scratch_first[0]),
		predicted_second,
		sizeof(predicted_second) / sizeof(predicted_second[0]), &second) ==
		SG_BELIEF_RUNTIME_PREDICT_APPLIED);
	CHECK(second.particle_count == first.particle_count);
	CHECK(second.confidence == first.confidence);
	CHECK(memcmp(&second.source.horizon_chain_identity,
		&first.source.horizon_chain_identity,
		sizeof(first.source.horizon_chain_identity)) == 0);
	for (index = 0U; index < first.particle_count; index++)
		CHECK(RuntimeParticlesEqual(&predicted_first[index],
			&predicted_second[index]));

	/* Querying never publishes its temporary horizon or prediction buffers. */
	view = SG_BeliefRuntimeViewForClient(1U, 3U);
	CHECK(view && view->updated_at_ms == 100U);
	if (view && view->particle_count <= sizeof(before) / sizeof(before[0]))
	{
		CHECK(view->particle_count == 1U);
		for (index = 0U; index < view->particle_count; index++)
			CHECK(RuntimeParticlesEqual(&view->particles[index], &before[index]));
	}

	memset(&rejected, 0xa5, sizeof(rejected));
	rejected.at_time_ms = UINT64_MAX;
	rejected_before = rejected;
	CHECK(SG_BeliefRuntimePredict(1U, &target, 99U, scratch_first,
		scratch_second, sizeof(scratch_first) / sizeof(scratch_first[0]),
		predicted_first, sizeof(predicted_first) / sizeof(predicted_first[0]),
		&rejected) == SG_BELIEF_RUNTIME_PREDICT_REJECTED);
	CHECK(memcmp(&rejected, &rejected_before, sizeof(rejected)) == 0);

	CHECK(SG_BeliefRuntimeFrame(1U, 300U) ==
		SG_BELIEF_RUNTIME_FRAME_APPLIED);
	CHECK(SG_BeliefRuntimePredict(1U, &target, 299U, scratch_first,
		scratch_second, sizeof(scratch_first) / sizeof(scratch_first[0]),
		predicted_first, sizeof(predicted_first) / sizeof(predicted_first[0]),
		&rejected) == SG_BELIEF_RUNTIME_PREDICT_REJECTED);
	fixture.snapshot.topology_revision = 8U;
	CHECK(SG_BeliefRuntimePredict(1U, &target, 350U, scratch_first,
		scratch_second, sizeof(scratch_first) / sizeof(scratch_first[0]),
		predicted_first, sizeof(predicted_first) / sizeof(predicted_first[0]),
		&rejected) == SG_BELIEF_RUNTIME_PREDICT_UNAVAILABLE);
	fixture.snapshot.topology_revision = 7U;
	CHECK(SG_BeliefRuntimePredict(1U, &target, 350U, scratch_first,
		scratch_second, sizeof(scratch_first) / sizeof(scratch_first[0]),
		predicted_first, sizeof(predicted_first) / sizeof(predicted_first[0]),
		&first) == SG_BELIEF_RUNTIME_PREDICT_APPLIED);
	CHECK(first.at_time_ms == 350U);
	CHECK(SG_BeliefRuntimeProviderSet(NULL));
}

static void TestRuntimeOwner(void)
{
	runtime_fixture_t fixture;
	sg_belief_runtime_provider_t provider;
	sg_belief_runtime_pose_t pose;
	sg_perception_observation_t observation;
	sg_perception_hypothesis_t hypothesis;
	sg_perception_hypothesis_t sound_hypotheses[17];
	sg_belief_life_identity_t old_target;
	sg_belief_life_identity_t current_target;
	sg_belief_life_identity_t issuer;
	const sg_belief_runtime_view_t *view;
	size_t expected_particle_count;
	uint64_t expected_updated_at;

	FixtureInit(&fixture);
	memset(&pose, 0, sizeof(pose));
	pose.movement_state = SG_BELIEF_MOTION_GROUND;
	CHECK(!SG_BeliefRuntimeHypothesis(&pose, &hypothesis));
	memset(&provider, 0, sizeof(provider));
	provider.snapshot = &fixture.snapshot;
	provider.policy.confidence_decay_ms = 1000U;
	provider.policy.diffusion_fraction = 0.5f;
	provider.policy.spread_growth_per_ms = 0.01f;
	provider.locate = Locate;
	CHECK(!SG_BeliefRuntimeProviderSet(&provider));
	provider.localization_generation = 1U;
	CHECK(SG_BeliefRuntimeProviderSet(&provider));
	CHECK(SG_BeliefRuntimeProviderAvailable());
	CHECK(SG_BeliefRuntimeSnapshot() == &fixture.snapshot);
	CHECK(SG_BeliefRuntimeHypothesis(&pose, &hypothesis));
	CHECK(hypothesis.phase.phase_id == 0U);

	observation = Observation(SG_PERCEPTION_SOURCE_SIGHT, 1U, 100U, 30U);
	observation.data.sight.in_pvs = 1U;
	observation.data.sight.line_of_sight_proved = 1U;
	observation.data.sight.hypothesis = Hypothesis(0U, 0U,
		SG_PERCEPTION_LOCATION_EARNED_RUNTIME, 0.0f);
	CHECK(SG_BeliefRuntimeObserve(&observation) == SG_BELIEF_RUNTIME_OBSERVE_APPLIED);

	observation = Observation(SG_PERCEPTION_SOURCE_SOUND, 2U, 200U, 30U);
	for (size_t index = 0U; index < 17U; index++)
	{
		sound_hypotheses[index] = Hypothesis(0U, 0U,
			SG_PERCEPTION_LOCATION_EARNED_RUNTIME, 0.0f);
		sound_hypotheses[index].position[0] = (float)index;
	}
	observation.data.sound.in_phs = 1U;
	observation.data.sound.positional = 1U;
	observation.data.sound.kind = SG_PERCEPTION_SOUND_WEAPON;
	observation.data.sound.sound_id = 9U;
	observation.data.sound.attenuation = 1.0f;
	observation.data.sound.audible_radius = 800.0f;
	observation.data.sound.hypotheses = sound_hypotheses;
	observation.data.sound.hypothesis_count = 17U;
	CHECK(SG_BeliefRuntimeObserve(&observation) == SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	view = SG_BeliefRuntimeViewForClient(1U, 3U);
	CHECK(view != NULL && view->particle_count >= 18U);
	CHECK(view && view->particles != NULL &&
		view->particles[0].position[0] != view->particles[16].position[0]);

	observation = Observation(SG_PERCEPTION_SOURCE_DAMAGE, 3U, 300U, 30U);
	hypothesis = Hypothesis(0U, 0U, SG_PERCEPTION_LOCATION_EARNED_RUNTIME,
		48.0f);
	observation.data.damage.landed = 1U;
	observation.data.damage.damage = 20U;
	observation.data.damage.means_of_death = 7U;
	observation.data.damage.incoming_direction[0] = 1.0f;
	observation.data.damage.hypotheses = &hypothesis;
	observation.data.damage.hypothesis_count = 1U;
	CHECK(SG_BeliefRuntimeObserve(&observation) == SG_BELIEF_RUNTIME_OBSERVE_APPLIED);

	observation = Observation(SG_PERCEPTION_SOURCE_ITEM, 4U, 400U, 30U);
	hypothesis = Hypothesis(2U, 1U, SG_PERCEPTION_LOCATION_RUNE_STATIC, 0.0f);
	observation.data.item.occurrence = SG_PERCEPTION_ITEM_TARGET_PICKUP;
	observation.data.item.destination.kind = SG_DESTINATION_POWERUP;
	observation.data.item.destination.value.item.item_id = 11U;
	observation.data.item.hypotheses = &hypothesis;
	observation.data.item.hypothesis_count = 1U;
	CHECK(SG_BeliefRuntimeObserve(&observation) == SG_BELIEF_RUNTIME_OBSERVE_APPLIED);

	observation = Observation(SG_PERCEPTION_SOURCE_FLAG, 5U, 500U, 30U);
	hypothesis = Hypothesis(2U, 1U, SG_PERCEPTION_LOCATION_RUNE_STATIC, 0.0f);
	observation.data.flag.occurrence = SG_PERCEPTION_FLAG_TARGET_PICKUP;
	observation.data.flag.destination.kind = SG_DESTINATION_FLAG;
	observation.data.flag.destination.value.flag.team = 1U;
	observation.data.flag.destination.value.flag.location = SG_DESTINATION_FLAG_HOME;
	observation.data.flag.hypotheses = &hypothesis;
	observation.data.flag.hypothesis_count = 1U;
	CHECK(SG_BeliefRuntimeObserve(&observation) == SG_BELIEF_RUNTIME_OBSERVE_APPLIED);

	observation = Observation(SG_PERCEPTION_SOURCE_TEAMMATE, 6U, 600U, 30U);
	observation.authentication.issuer_life = Life(5U, 50U);
	hypothesis = Hypothesis(0U, 0U, SG_PERCEPTION_LOCATION_EARNED_RUNTIME,
		32.0f);
	observation.data.teammate.reported_source = SG_PERCEPTION_SOURCE_SOUND;
	observation.data.teammate.report_kind = 1U;
	observation.data.teammate.hypotheses = &hypothesis;
	observation.data.teammate.hypothesis_count = 1U;
	CHECK(SG_BeliefRuntimeObserve(&observation) == SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	view = SG_BeliefRuntimeView(1U, &observation.target_life);
	CHECK(view != NULL);
	CHECK(view && view->latest_source == SG_BELIEF_SOURCE_TEAMMATE);
	CHECK(SG_BeliefRuntimeFrame(7U, 800U) ==
		SG_BELIEF_RUNTIME_FRAME_APPLIED);
	view = SG_BeliefRuntimeViewForClient(1U, 3U);
	CHECK(view != NULL);
	CHECK(view && view->updated_at_ms == 800U);
	CHECK(view && !view->exact_sight);

	observation = Observation(SG_PERCEPTION_SOURCE_SIGHT, 7U, 900U, 31U);
	observation.data.sight.in_pvs = 1U;
	observation.data.sight.line_of_sight_proved = 1U;
	observation.data.sight.hypothesis = Hypothesis(0U, 0U,
		SG_PERCEPTION_LOCATION_EARNED_RUNTIME, 0.0f);
	CHECK(SG_BeliefRuntimeObserve(&observation) == SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	old_target = Life(3U, 30U);
	CHECK(SG_BeliefRuntimeView(1U, &old_target) == NULL);
	view = SG_BeliefRuntimeView(1U, &observation.target_life);
	CHECK(view != NULL && view->target_life.spawn_generation == 31U);
	expected_particle_count = view ? view->particle_count : 0U;
	expected_updated_at = view ? view->updated_at_ms : 0U;
	SightObservation(&observation, 7U, 900U, 31U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_REJECTED);
	SightObservation(&observation, 8U, 850U, 31U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_REJECTED);
	view = SG_BeliefRuntimeViewForClient(1U, 3U);
	CHECK(view && view->updated_at_ms == expected_updated_at);
	CHECK(view && view->particle_count == expected_particle_count);
	SightObservation(&observation, 9U, 950U, 31U);
	observation.target_team = 1U;
	CHECK(SG_BeliefRuntimeObserve(&observation) == SG_BELIEF_RUNTIME_OBSERVE_REJECTED);
	current_target = Life(3U, 31U);
	CHECK(SG_BeliefRuntimeView(1U, &current_target) != NULL);

	issuer = Life(4U, 40U);
	SG_BeliefRuntimeRetireLife(&issuer);
	CHECK(SG_BeliefRuntimeViewForClient(1U, 3U) == NULL);
	observation = Observation(SG_PERCEPTION_SOURCE_SIGHT, 8U, 1000U, 31U);
	observation.evidence_kind = SG_BELIEF_EVIDENCE_NEGATIVE;
	observation.data.sight.in_pvs = 1U;
	observation.data.sight.line_of_sight_proved = 1U;
	observation.data.sight.hypothesis = Hypothesis(0U, 0U,
		SG_PERCEPTION_LOCATION_EARNED_RUNTIME, 0.0f);
	observation.authentication.issuer_life = Life(6U, 60U);
	CHECK(SG_BeliefRuntimeObserve(&observation) == SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	CHECK(SG_BeliefRuntimeViewForClient(1U, 3U) == NULL);
	observation = Observation(SG_PERCEPTION_SOURCE_SIGHT, 9U, 1100U, 31U);
	observation.data.sight.in_pvs = 1U;
	observation.data.sight.line_of_sight_proved = 1U;
	observation.data.sight.hypothesis = Hypothesis(0U, 0U,
		SG_PERCEPTION_LOCATION_EARNED_RUNTIME, 0.0f);
	observation.authentication.issuer_life = Life(6U, 60U);
	CHECK(SG_BeliefRuntimeObserve(&observation) == SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	CHECK(SG_BeliefRuntimeViewForClient(1U, 3U) != NULL);
	fixture.snapshot.topology_revision = 8U;
	CHECK(SG_BeliefRuntimeViewForClient(1U, 3U) == NULL);
	fixture.snapshot.topology_revision = 7U;
	CHECK(SG_BeliefRuntimeViewForClient(1U, 3U) != NULL);
	SG_BeliefRuntimeRetireLife(&current_target);
	CHECK(SG_BeliefRuntimeViewForClient(1U, 3U) == NULL);
	CHECK(SG_BeliefRuntimeProviderSet(NULL));
	CHECK(!SG_BeliefRuntimeProviderAvailable());
	CHECK(SG_BeliefRuntimeSnapshot() == NULL);
}

int main(void)
{
	TestRuntimeHorizonScopeLifecycle();
	TestRuntimeReplacementIsAtomic();
	TestRuntimeLifeFencePreventsResurrection();
	TestRuntimeIssuerLifeFences();
	TestRuntimeIssuerGenerationInvalidatesTracks();
	TestRuntimeIssuerGenerationReplacesSameTargetTrack();
	TestRuntimeIssuerRolloverRejectsTimestampRollback();
	TestRuntimeIssuerRolloverRejectsEvidenceSequenceRollback();
	TestRuntimeFrameWatermarkRejectsIssuerRollover();
	TestRuntimeFrameWatermarkRejectsDelayedEvidence();
	TestRuntimeFrameWatermarkRejectsTargetLifeRollover();
	TestRuntimeFrameTimestampRegressionResetsTracks();
	TestRuntimeAudienceClientWatermarkSurvivesRetirement();
	TestRuntimeAudienceClientWatermarkSurvivesSupersession();
	TestRuntimeAudienceClientWatermarkAdvancesEmptyFrame();
	TestRuntimeAudienceClientWatermarkFailureDoesNotAdvance();
	TestRuntimeAudienceClientWatermarkSurvivesProviderReplacement();
	TestRuntimeAudienceClientWatermarkResetStartsUniverse();
	TestRuntimeAudienceTargetSequenceSurvivesIssuerRetirement();
	TestRuntimeAudienceTargetSequenceSurvivesProviderReplacement();
	TestRuntimeAudienceTargetSequenceSurvivesFrameRegression();
	TestRuntimeAudienceTargetSequenceResetsForNewGeneration();
	TestRuntimeAudienceTargetSequenceStaysAudienceScoped();
	TestRuntimeAudienceTargetSequenceResetStartsUniverse();
	TestRuntimeAudienceTargetSequenceRejectedObserveDoesNotAdvance();
	TestRuntimeAudienceTargetSequenceFailureDoesNotAdvance();
	TestRuntimeFrameIsAtomic();
	TestRuntimeRejectedObservationPreservesTrack();
	TestRuntimeLocatorProviderChangeRejected();
	TestRuntimeDelayedEvidenceConverges();
	TestRuntimeFuturePredictionQuery();
	TestRuntimeOwner();
	TestRuntimeProviderReplacementPreservesLifeFences();
	TestRuntimeProviderReplacementIsTransactional();
	if (failures != 0)
	{
		fprintf(stderr, "sg_belief_runtime_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("sg_belief_runtime_test: ok");
	return 0;
}
