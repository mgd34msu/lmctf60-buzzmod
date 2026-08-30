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
	CHECK(view != NULL && view->particle_count == 18U);
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
	CHECK(SG_BeliefRuntimeObserve(&observation) == SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	CHECK(SG_BeliefRuntimeViewForClient(1U, 3U) == NULL);
	observation = Observation(SG_PERCEPTION_SOURCE_SIGHT, 9U, 1100U, 31U);
	observation.data.sight.in_pvs = 1U;
	observation.data.sight.line_of_sight_proved = 1U;
	observation.data.sight.hypothesis = Hypothesis(0U, 0U,
		SG_PERCEPTION_LOCATION_EARNED_RUNTIME, 0.0f);
	CHECK(SG_BeliefRuntimeObserve(&observation) == SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	SG_BeliefRuntimeRetireClient(3U);
	CHECK(SG_BeliefRuntimeViewForClient(1U, 3U) == NULL);
	CHECK(SG_BeliefRuntimeProviderSet(NULL));
	CHECK(!SG_BeliefRuntimeProviderAvailable());
	CHECK(SG_BeliefRuntimeSnapshot() == NULL);
}

int main(void)
{
	TestRuntimeOwner();
	if (failures != 0)
	{
		fprintf(stderr, "sg_belief_runtime_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("sg_belief_runtime_test: ok");
	return 0;
}
