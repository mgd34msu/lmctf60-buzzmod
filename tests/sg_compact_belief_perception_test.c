#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "slipgate/sg_compact_belief_perception.h"

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

typedef struct compact_belief_fixture_s
{
	sg_rune_plane_t planes[6];
	sg_rune_phase_basis_t model_phases[3];
	sg_rune_capability_kernel_t kernels[3];
	sg_rune_cell_t cells[2];
	sg_rune_model_t model;
	sg_phase_coordinate_t phases[3];
	sg_rune_runtime_snapshot_t snapshot;
} compact_belief_fixture_t;

typedef struct test_authority_s
{
	uint64_t nonce;
	uint8_t enabled;
	uint8_t sealed;
	uint8_t reserved[6];
	sg_perception_observation_t observation;
	sg_perception_observation_t sealed_observation;
	sg_perception_hypothesis_t sealed_hypotheses[8];
	size_t sealed_hypothesis_count;
} test_authority_t;

typedef struct decoder_context_s
{
	const compact_belief_fixture_t *fixture;
	const test_authority_t *expected;
	uint8_t mutate_after_consume;
} decoder_context_t;

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

static void FixtureInit(compact_belief_fixture_t *fixture)
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
		fixture->model_phases[index].velocity.x =
			Interval(-20000.0f, 20000.0f);
		fixture->model_phases[index].velocity.y =
			Interval(-20000.0f, 20000.0f);
		fixture->model_phases[index].velocity.z =
			Interval(-20000.0f, 20000.0f);
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

static sg_belief_runtime_provider_t Provider(
	compact_belief_fixture_t *fixture)
{
	sg_belief_runtime_provider_t provider;

	memset(&provider, 0, sizeof(provider));
	provider.snapshot = &fixture->snapshot;
	provider.localization_generation = 1U;
	provider.policy.confidence_decay_ms = 1000U;
	provider.policy.diffusion_fraction = 0.0f;
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

static sg_perception_authentication_t Authentication(
	sg_perception_source_t source, uint64_t sequence, uint64_t at_ms)
{
	sg_perception_authentication_t authentication;

	memset(&authentication, 0, sizeof(authentication));
	authentication.authenticated = 1U;
	authentication.authority = source == SG_PERCEPTION_SOURCE_TEAMMATE ?
		SG_PERCEPTION_AUTHORITY_HOST_TEAMMATE_REPORT :
		SG_PERCEPTION_AUTHORITY_HOST_SENSOR;
	authentication.issuer_team = 1U;
	authentication.audience_team = 1U;
	authentication.issuer_life = Life(4U, 40U);
	authentication.event_id = 1000U + sequence;
	authentication.evidence_sequence = sequence;
	authentication.observed_at_ms = at_ms;
	authentication.authenticated_at_ms = at_ms;
	authentication.valid_until_ms = at_ms + 1000U;
	authentication.rune_identity = 99U;
	authentication.topology_revision = 7U;
	return authentication;
}

static sg_perception_hypothesis_t Hypothesis(uint32_t phase, uint32_t cell,
	sg_perception_location_basis_t basis, float position, float spread)
{
	sg_perception_hypothesis_t hypothesis;

	memset(&hypothesis, 0, sizeof(hypothesis));
	hypothesis.phase = (sg_phase_coordinate_t){ phase, cell };
	hypothesis.location_basis = basis;
	hypothesis.movement_state = SG_BELIEF_MOTION_GROUND;
	hypothesis.position[0] = position;
	hypothesis.velocity[0] = 1.0f;
	hypothesis.spread_radius = spread;
	hypothesis.likelihood = 1.0f;
	return hypothesis;
}

static const sg_compact_belief_perception_evidence_authority_t *Authority(
	const test_authority_t *authority)
{
	return (const sg_compact_belief_perception_evidence_authority_t *)
		(const void *)authority;
}

static int ObservationHypothesisSpan(
	const sg_perception_observation_t *observation,
	const sg_perception_hypothesis_t **hypotheses_out,
	size_t *count_out, int *external_out)
{
	if (!observation || !hypotheses_out || !count_out || !external_out)
		return 0;
	*hypotheses_out = NULL;
	*count_out = 0U;
	*external_out = 0;
	switch (observation->source)
	{
	case SG_PERCEPTION_SOURCE_SIGHT:
		return 1;
	case SG_PERCEPTION_SOURCE_SOUND:
		*hypotheses_out = observation->data.sound.hypotheses;
		*count_out = observation->data.sound.hypothesis_count;
		*external_out = 1;
		return 1;
	case SG_PERCEPTION_SOURCE_DAMAGE:
		*hypotheses_out = observation->data.damage.hypotheses;
		*count_out = observation->data.damage.hypothesis_count;
		*external_out = 1;
		return 1;
	case SG_PERCEPTION_SOURCE_ITEM:
		*hypotheses_out = observation->data.item.hypotheses;
		*count_out = observation->data.item.hypothesis_count;
		*external_out = 1;
		return 1;
	case SG_PERCEPTION_SOURCE_FLAG:
		*hypotheses_out = observation->data.flag.hypotheses;
		*count_out = observation->data.flag.hypothesis_count;
		*external_out = 1;
		return 1;
	case SG_PERCEPTION_SOURCE_TEAMMATE:
		*hypotheses_out = observation->data.teammate.hypotheses;
		*count_out = observation->data.teammate.hypothesis_count;
		*external_out = 1;
		return 1;
	case SG_PERCEPTION_SOURCE_COUNT:
		break;
	}
	return 0;
}

static void ObservationSetHypothesisSpan(
	sg_perception_observation_t *observation,
	const sg_perception_hypothesis_t *hypotheses)
{
	switch (observation->source)
	{
	case SG_PERCEPTION_SOURCE_SOUND:
		observation->data.sound.hypotheses = hypotheses;
		break;
	case SG_PERCEPTION_SOURCE_DAMAGE:
		observation->data.damage.hypotheses = hypotheses;
		break;
	case SG_PERCEPTION_SOURCE_ITEM:
		observation->data.item.hypotheses = hypotheses;
		break;
	case SG_PERCEPTION_SOURCE_FLAG:
		observation->data.flag.hypotheses = hypotheses;
		break;
	case SG_PERCEPTION_SOURCE_TEAMMATE:
		observation->data.teammate.hypotheses = hypotheses;
		break;
	case SG_PERCEPTION_SOURCE_SIGHT:
	case SG_PERCEPTION_SOURCE_COUNT:
		break;
	}
}

static int ObservationEqual(const sg_perception_observation_t *left,
	const sg_perception_observation_t *right)
{
	const sg_perception_hypothesis_t *left_hypotheses;
	const sg_perception_hypothesis_t *right_hypotheses;
	sg_perception_observation_t left_copy;
	sg_perception_observation_t right_copy;
	size_t left_count;
	size_t right_count;
	int left_external;
	int right_external;

	if (!ObservationHypothesisSpan(left, &left_hypotheses, &left_count,
		&left_external) ||
		!ObservationHypothesisSpan(right, &right_hypotheses, &right_count,
			&right_external) || left_external != right_external ||
		left_count != right_count ||
		(left_external && left_count != 0U &&
			(!left_hypotheses || !right_hypotheses)))
		return 0;
	left_copy = *left;
	right_copy = *right;
	if (left_external)
	{
		ObservationSetHypothesisSpan(&left_copy, NULL);
		ObservationSetHypothesisSpan(&right_copy, NULL);
	}
	if (memcmp(&left_copy, &right_copy, sizeof(left_copy)) != 0)
		return 0;
	return !left_external || left_count == 0U ||
		memcmp(left_hypotheses, right_hypotheses,
			left_count * sizeof(*left_hypotheses)) == 0;
}

static int SealAuthority(test_authority_t *authority)
{
	const sg_perception_hypothesis_t *hypotheses;
	int external;
	size_t count;

	if (!authority ||
		!ObservationHypothesisSpan(&authority->observation, &hypotheses,
			&count, &external) || count >
		sizeof(authority->sealed_hypotheses) /
		sizeof(authority->sealed_hypotheses[0]) ||
		(external && count != 0U && !hypotheses))
	{
		if (authority)
			authority->sealed = 0U;
		return 0;
	}
	authority->sealed_observation = authority->observation;
	authority->sealed_hypothesis_count = count;
	if (external && count != 0U)
	{
		memcpy(authority->sealed_hypotheses, hypotheses,
			count * sizeof(authority->sealed_hypotheses[0]));
		ObservationSetHypothesisSpan(&authority->sealed_observation,
			authority->sealed_hypotheses);
	}
	authority->sealed = 1U;
	return 1;
}

static int Decode(void *context, const sg_rune_runtime_snapshot_t *snapshot,
	const sg_compact_belief_perception_evidence_authority_t *authority,
	sg_compact_belief_perception_observation_consume_fn consume,
	void *consume_context)
{
	decoder_context_t *decoder = (decoder_context_t *)context;
	const test_authority_t *record =
		(const test_authority_t *)(const void *)authority;
	int consumed;

	if (!decoder || !decoder->fixture || !decoder->expected ||
		snapshot != &decoder->fixture->snapshot || record != decoder->expected ||
		record->nonce != UINT64_C(0x534745564944) || record->enabled != 1U ||
		record->sealed != 1U || !consume ||
		!ObservationEqual(&record->observation, &record->sealed_observation))
		return 0;
	consumed = consume(consume_context, &record->sealed_observation);
	if (consumed && decoder->mutate_after_consume == 1U &&
		record->sealed_hypothesis_count != 0U)
	{
		test_authority_t *mutable_record = (test_authority_t *)(void *)record;
		mutable_record->sealed_hypotheses[0].position[0] += 777.0f;
		decoder->mutate_after_consume = 0U;
	}
	return consumed;
}

static sg_perception_observation_t *Issue(test_authority_t *authority,
	sg_perception_source_t source, sg_belief_evidence_kind_t evidence_kind,
	uint64_t sequence, uint64_t at_ms, uint8_t target_team,
	sg_belief_life_identity_t target_life, float confidence)
{
	memset(&authority->observation, 0, sizeof(authority->observation));
	authority->sealed = 0U;
	authority->observation.authentication = Authentication(source, sequence,
		at_ms);
	authority->observation.source = source;
	authority->observation.evidence_kind = evidence_kind;
	authority->observation.target_team = target_team;
	authority->observation.target_life = target_life;
	authority->observation.confidence = confidence;
	return &authority->observation;
}

static void Setup(compact_belief_fixture_t *fixture,
	sg_compact_belief_perception_binding_t *binding,
	decoder_context_t *decoder, test_authority_t *authority)
{
	sg_belief_runtime_provider_t provider;

	FixtureInit(fixture);
	memset(authority, 0, sizeof(*authority));
	authority->nonce = UINT64_C(0x534745564944);
	authority->enabled = 1U;
	decoder->fixture = fixture;
	decoder->expected = authority;
	provider = Provider(fixture);
	CHECK(SG_BeliefRuntimeProviderSet(NULL));
	CHECK(SG_BeliefRuntimeProviderSet(&provider));
	CHECK(SG_CompactBeliefPerceptionBind(binding, &fixture->snapshot,
		Decode, decoder) == SG_COMPACT_BELIEF_PERCEPTION_APPLIED);
}

static void Teardown(void)
{
	CHECK(SG_BeliefRuntimeProviderSet(NULL));
}

static void TestBindingAndOpaqueAuthority(void)
{
	compact_belief_fixture_t fixture;
	decoder_context_t decoder;
	test_authority_t authority;
	test_authority_t wrong_authority;
	sg_compact_belief_perception_binding_t binding;
	sg_belief_life_identity_t target = Life(3U, 30U);

	memset(&binding, 0, sizeof(binding));
	Setup(&fixture, &binding, &decoder, &authority);
	CHECK(SG_CompactBeliefPerceptionBindingCurrent(&binding));
	Issue(&authority, SG_PERCEPTION_SOURCE_SIGHT, SG_BELIEF_EVIDENCE_POSITIVE,
		1U, 100U, 2U, target, 0.8f)->data.sight.in_pvs = 1U;
	authority.observation.data.sight.line_of_sight_proved = 1U;
	authority.observation.data.sight.hypothesis = Hypothesis(0U, 0U,
		SG_PERCEPTION_LOCATION_EARNED_RUNTIME, 100.0f, 0.0f);
	CHECK(SealAuthority(&authority));
	CHECK(SG_CompactBeliefPerceptionObserve(&binding, Authority(&authority)) ==
		SG_COMPACT_BELIEF_PERCEPTION_APPLIED);
	/* The decoder's sealed copy must reject mutations to every authority-bound
	 * dimension, including target life, audience, sequence, and payload. */
	authority.observation.target_life = Life(30U, 300U);
	CHECK(SG_CompactBeliefPerceptionObserve(&binding, Authority(&authority)) ==
		SG_COMPACT_BELIEF_PERCEPTION_REJECTED);
	authority.observation.target_life = target;
	authority.observation.authentication.audience_team = 2U;
	CHECK(SG_CompactBeliefPerceptionObserve(&binding, Authority(&authority)) ==
		SG_COMPACT_BELIEF_PERCEPTION_REJECTED);
	authority.observation.authentication.audience_team = 1U;
	authority.observation.authentication.evidence_sequence++;
	CHECK(SG_CompactBeliefPerceptionObserve(&binding, Authority(&authority)) ==
		SG_COMPACT_BELIEF_PERCEPTION_REJECTED);
	authority.observation.authentication.evidence_sequence--;
	authority.observation.data.sight.hypothesis.position[0] += 1.0f;
	CHECK(SG_CompactBeliefPerceptionObserve(&binding, Authority(&authority)) ==
		SG_COMPACT_BELIEF_PERCEPTION_REJECTED);
	authority.observation.data.sight.hypothesis.position[0] -= 1.0f;
	memcpy(&wrong_authority, &authority, sizeof(wrong_authority));
	wrong_authority.nonce++;
	CHECK(SG_CompactBeliefPerceptionObserve(&binding,
		Authority(&wrong_authority)) == SG_COMPACT_BELIEF_PERCEPTION_REJECTED);
	fixture.snapshot.topology_revision = 8U;
	CHECK(!SG_CompactBeliefPerceptionBindingCurrent(&binding));
	CHECK(SG_CompactBeliefPerceptionObserve(&binding, Authority(&authority)) ==
		SG_COMPACT_BELIEF_PERCEPTION_IDENTITY_MISMATCH);
	fixture.snapshot.topology_revision = 7U;
	CHECK(SG_CompactBeliefPerceptionBindingCurrent(&binding));
	SG_CompactBeliefPerceptionUnbind(&binding);
	CHECK(!SG_CompactBeliefPerceptionBindingCurrent(&binding));
	Teardown();
}

static void TestSightAndNegativeCellExclusion(void)
{
	compact_belief_fixture_t fixture;
	decoder_context_t decoder;
	test_authority_t authority;
	sg_compact_belief_perception_binding_t binding;
	sg_belief_life_identity_t target = Life(3U, 30U);
	const sg_belief_runtime_view_t *view;

	Setup(&fixture, &binding, &decoder, &authority);
	Issue(&authority, SG_PERCEPTION_SOURCE_SIGHT, SG_BELIEF_EVIDENCE_POSITIVE,
		1U, 100U, 2U, target, 0.9f);
	authority.observation.data.sight.in_pvs = 1U;
	authority.observation.data.sight.line_of_sight_proved = 1U;
	authority.observation.data.sight.hypothesis = Hypothesis(0U, 0U,
		SG_PERCEPTION_LOCATION_EARNED_RUNTIME, 100.0f, 0.0f);
	CHECK(SealAuthority(&authority));
	CHECK(SG_CompactBeliefPerceptionObserve(&binding, Authority(&authority)) ==
		SG_COMPACT_BELIEF_PERCEPTION_APPLIED);
	Issue(&authority, SG_PERCEPTION_SOURCE_SIGHT, SG_BELIEF_EVIDENCE_POSITIVE,
		2U, 105U, 2U, target, 0.8f);
	authority.observation.data.sight.in_pvs = 1U;
	authority.observation.data.sight.line_of_sight_proved = 1U;
	authority.observation.data.sight.hypothesis = Hypothesis(0U, 0U,
		SG_PERCEPTION_LOCATION_EARNED_RUNTIME, 999.0f, 0.0f);
	CHECK(SealAuthority(&authority));
	CHECK(SG_CompactBeliefPerceptionObserve(&binding, Authority(&authority)) ==
		SG_COMPACT_BELIEF_PERCEPTION_APPLIED);
	Issue(&authority, SG_PERCEPTION_SOURCE_SIGHT, SG_BELIEF_EVIDENCE_POSITIVE,
		3U, 110U, 2U, target, 0.8f);
	authority.observation.data.sight.in_pvs = 1U;
	authority.observation.data.sight.line_of_sight_proved = 1U;
	authority.observation.data.sight.hypothesis = Hypothesis(1U, 0U,
		SG_PERCEPTION_LOCATION_EARNED_RUNTIME, 200.0f, 0.0f);
	CHECK(SealAuthority(&authority));
	CHECK(SG_CompactBeliefPerceptionObserve(&binding, Authority(&authority)) ==
		SG_COMPACT_BELIEF_PERCEPTION_APPLIED);
	Issue(&authority, SG_PERCEPTION_SOURCE_SIGHT, SG_BELIEF_EVIDENCE_POSITIVE,
		4U, 115U, 2U, target, 0.8f);
	authority.observation.data.sight.in_pvs = 1U;
	authority.observation.data.sight.line_of_sight_proved = 1U;
	authority.observation.data.sight.hypothesis = Hypothesis(2U, 1U,
		SG_PERCEPTION_LOCATION_EARNED_RUNTIME, 500.0f, 0.0f);
	CHECK(SealAuthority(&authority));
	CHECK(SG_CompactBeliefPerceptionObserve(&binding, Authority(&authority)) ==
		SG_COMPACT_BELIEF_PERCEPTION_APPLIED);
	Issue(&authority, SG_PERCEPTION_SOURCE_SIGHT, SG_BELIEF_EVIDENCE_NEGATIVE,
		5U, 120U, 2U, target, 1.0f);
	authority.observation.data.sight.in_pvs = 1U;
	authority.observation.data.sight.line_of_sight_proved = 1U;
	authority.observation.data.sight.hypothesis = Hypothesis(0U, 0U,
		SG_PERCEPTION_LOCATION_EARNED_RUNTIME, 100.0f, 0.0f);
	CHECK(SealAuthority(&authority));
	CHECK(SG_CompactBeliefPerceptionObserve(&binding, Authority(&authority)) ==
		SG_COMPACT_BELIEF_PERCEPTION_APPLIED);
	view = SG_CompactBeliefPerceptionViewForClient(&binding, 1U, 3U);
	CHECK(view != NULL && view->particle_count == 1U);
	CHECK(view != NULL && view->particles[0].phase.phase_id == 2U);
	Teardown();
}

static void TestProofsSoundAndDamage(void)
{
	compact_belief_fixture_t fixture;
	decoder_context_t decoder;
	test_authority_t authority;
	sg_compact_belief_perception_binding_t binding;
	sg_perception_hypothesis_t sound_hypotheses[3];
	sg_perception_hypothesis_t damage_hypotheses[2];
	sg_perception_sound_t sound;
	sg_perception_damage_t damage;
	sg_belief_life_identity_t sound_target = Life(5U, 50U);
	sg_belief_life_identity_t damage_target = Life(6U, 60U);
	const sg_belief_runtime_view_t *view;

	Setup(&fixture, &binding, &decoder, &authority);
	Issue(&authority, SG_PERCEPTION_SOURCE_SIGHT, SG_BELIEF_EVIDENCE_POSITIVE,
		1U, 100U, 2U, Life(4U, 44U), 0.9f);
	authority.observation.data.sight.in_pvs = 0U;
	authority.observation.data.sight.line_of_sight_proved = 0U;
	authority.observation.data.sight.hypothesis = Hypothesis(0U, 0U,
		SG_PERCEPTION_LOCATION_EARNED_RUNTIME, 100.0f, 0.0f);
	CHECK(SealAuthority(&authority));
	CHECK(SG_CompactBeliefPerceptionObserve(&binding, Authority(&authority)) ==
		SG_COMPACT_BELIEF_PERCEPTION_REJECTED);

	sound_hypotheses[0] = Hypothesis(0U, 0U,
		SG_PERCEPTION_LOCATION_EARNED_RUNTIME, 100.0f, 32.0f);
	sound_hypotheses[1] = Hypothesis(2U, 1U,
		SG_PERCEPTION_LOCATION_EARNED_RUNTIME, 500.0f, 32.0f);
	sound_hypotheses[2] = Hypothesis(2U, 1U,
		SG_PERCEPTION_LOCATION_EARNED_RUNTIME, 900.0f, 32.0f);
	memset(&sound, 0, sizeof(sound));
	sound.in_phs = 1U;
	sound.positional = 1U;
	sound.kind = SG_PERCEPTION_SOUND_WEAPON;
	sound.sound_id = 17U;
	sound.listener_position[0] = -20.0f;
	sound.heard_origin[0] = 0.0f;
	sound.attenuation = 0.5f;
	sound.audible_radius = 800.0f;
	sound.hypotheses = sound_hypotheses;
	sound.hypothesis_count = 3U;
	Issue(&authority, SG_PERCEPTION_SOURCE_SOUND,
		SG_BELIEF_EVIDENCE_POSITIVE, 1U, 200U, 2U, sound_target, 0.7f)->
		data.sound = sound;
	CHECK(SealAuthority(&authority));
	decoder.mutate_after_consume = 1U;
	CHECK(SG_CompactBeliefPerceptionObserveSound(&binding,
		Authority(&authority)) == SG_COMPACT_BELIEF_PERCEPTION_APPLIED);
	view = SG_CompactBeliefPerceptionView(&binding, 1U, &sound_target);
	CHECK(view != NULL && view->particle_count == 2U);
	CHECK(view != NULL && view->particles[0].position[0] == 100.0f);
	CHECK(view != NULL && view->particles[0].weight >
		view->particles[1].weight);
	sound.in_phs = 0U;
	Issue(&authority, SG_PERCEPTION_SOURCE_SOUND,
		SG_BELIEF_EVIDENCE_POSITIVE, 2U, 210U, 2U, sound_target, 0.7f)->
		data.sound = sound;
	CHECK(SealAuthority(&authority));
	CHECK(SG_CompactBeliefPerceptionObserveSound(&binding,
		Authority(&authority)) == SG_COMPACT_BELIEF_PERCEPTION_REJECTED);

	damage_hypotheses[0] = Hypothesis(0U, 0U,
		SG_PERCEPTION_LOCATION_EARNED_RUNTIME, 100.0f, 32.0f);
	damage_hypotheses[1] = Hypothesis(0U, 0U,
		SG_PERCEPTION_LOCATION_EARNED_RUNTIME, -100.0f, 32.0f);
	memset(&damage, 0, sizeof(damage));
	damage.landed = 1U;
	damage.damage = 30U;
	damage.means_of_death = 7U;
	damage.incoming_direction[0] = 1.0f;
	damage.hypotheses = damage_hypotheses;
	damage.hypothesis_count = 2U;
	Issue(&authority, SG_PERCEPTION_SOURCE_DAMAGE,
		SG_BELIEF_EVIDENCE_POSITIVE, 1U, 300U, 2U, damage_target, 0.8f)->
		data.damage = damage;
	CHECK(SealAuthority(&authority));
	CHECK(SG_CompactBeliefPerceptionObserveDamage(&binding,
		Authority(&authority)) == SG_COMPACT_BELIEF_PERCEPTION_APPLIED);
	view = SG_CompactBeliefPerceptionView(&binding, 1U, &damage_target);
	CHECK(view != NULL && view->particle_count == 1U);
	CHECK(view != NULL && view->particles[0].position[0] == 100.0f);

	damage_hypotheses[0].spread_radius = 0.0f;
	damage_hypotheses[1].spread_radius = 0.0f;
	Issue(&authority, SG_PERCEPTION_SOURCE_DAMAGE,
		SG_BELIEF_EVIDENCE_POSITIVE, 2U, 310U, 2U, Life(7U, 70U), 0.8f)->
		data.damage = damage;
	CHECK(SealAuthority(&authority));
	CHECK(SG_CompactBeliefPerceptionObserveDamage(&binding,
		Authority(&authority)) == SG_COMPACT_BELIEF_PERCEPTION_REJECTED);
	Teardown();
}

static void TestItemsFlagsReportsAndPrediction(void)
{
	compact_belief_fixture_t fixture;
	decoder_context_t decoder;
	test_authority_t authority;
	sg_compact_belief_perception_binding_t binding;
	sg_perception_hypothesis_t hypothesis;
	sg_perception_item_t item;
	sg_perception_flag_t flag;
	sg_perception_teammate_t teammate;
	sg_belief_life_identity_t item_target = Life(7U, 70U);
	sg_belief_life_identity_t flag_target = Life(8U, 80U);
	sg_belief_life_identity_t teammate_target = Life(9U, 90U);
	sg_belief_life_identity_t predicted_target = Life(10U, 100U);
	sg_belief_particle_t scratch_first[32];
	sg_belief_particle_t scratch_second[32];
	sg_belief_particle_t predicted_particles[32];
	sg_belief_prediction_t prediction;
	const sg_belief_runtime_view_t *view;

	Setup(&fixture, &binding, &decoder, &authority);
	hypothesis = Hypothesis(2U, 1U, SG_PERCEPTION_LOCATION_RUNE_STATIC,
		500.0f, 0.0f);
	memset(&item, 0, sizeof(item));
	item.occurrence = SG_PERCEPTION_ITEM_TARGET_PICKUP;
	item.destination.kind = SG_DESTINATION_POWERUP;
	item.destination.value.item.item_id = 11U;
	item.hypotheses = &hypothesis;
	item.hypothesis_count = 1U;
	Issue(&authority, SG_PERCEPTION_SOURCE_ITEM,
		SG_BELIEF_EVIDENCE_POSITIVE, 1U, 400U, 2U, item_target, 0.8f)->
		data.item = item;
	CHECK(SealAuthority(&authority));
	CHECK(SG_CompactBeliefPerceptionObserve(&binding, Authority(&authority)) ==
		SG_COMPACT_BELIEF_PERCEPTION_APPLIED);
	view = SG_CompactBeliefPerceptionView(&binding, 1U, &item_target);
	CHECK(view != NULL && view->latest_source == SG_BELIEF_SOURCE_ITEM);

	memset(&flag, 0, sizeof(flag));
	flag.occurrence = SG_PERCEPTION_FLAG_TARGET_PICKUP;
	flag.destination.kind = SG_DESTINATION_FLAG;
	flag.destination.value.flag.team = 1U;
	flag.destination.value.flag.location = SG_DESTINATION_FLAG_HOME;
	flag.hypotheses = &hypothesis;
	flag.hypothesis_count = 1U;
	Issue(&authority, SG_PERCEPTION_SOURCE_FLAG,
		SG_BELIEF_EVIDENCE_POSITIVE, 2U, 500U, 2U, flag_target, 0.8f)->
		data.flag = flag;
	CHECK(SealAuthority(&authority));
	CHECK(SG_CompactBeliefPerceptionObserve(&binding, Authority(&authority)) ==
		SG_COMPACT_BELIEF_PERCEPTION_APPLIED);
	view = SG_CompactBeliefPerceptionView(&binding, 1U, &flag_target);
	CHECK(view != NULL && view->latest_source == SG_BELIEF_SOURCE_FLAG);

	hypothesis = Hypothesis(0U, 0U, SG_PERCEPTION_LOCATION_EARNED_RUNTIME,
		0.0f, 32.0f);
	memset(&teammate, 0, sizeof(teammate));
	teammate.reported_source = SG_PERCEPTION_SOURCE_SOUND;
	teammate.report_kind = 1U;
	teammate.hypotheses = &hypothesis;
	teammate.hypothesis_count = 1U;
	Issue(&authority, SG_PERCEPTION_SOURCE_TEAMMATE,
		SG_BELIEF_EVIDENCE_POSITIVE, 3U, 600U, 2U, teammate_target, 0.6f)->
		data.teammate = teammate;
	CHECK(SealAuthority(&authority));
	CHECK(SG_CompactBeliefPerceptionObserve(&binding, Authority(&authority)) ==
		SG_COMPACT_BELIEF_PERCEPTION_APPLIED);
	view = SG_CompactBeliefPerceptionView(&binding, 1U, &teammate_target);
	CHECK(view != NULL && view->latest_source == SG_BELIEF_SOURCE_TEAMMATE);
	authority.observation.authentication.authenticated = 0U;
	CHECK(SG_CompactBeliefPerceptionObserve(&binding, Authority(&authority)) ==
		SG_COMPACT_BELIEF_PERCEPTION_REJECTED);

	hypothesis = Hypothesis(0U, 0U, SG_PERCEPTION_LOCATION_EARNED_RUNTIME,
		0.0f, 0.0f);
	Issue(&authority, SG_PERCEPTION_SOURCE_SIGHT,
		SG_BELIEF_EVIDENCE_POSITIVE, 4U, 700U, 2U, predicted_target, 0.8f);
	authority.observation.data.sight.in_pvs = 1U;
	authority.observation.data.sight.line_of_sight_proved = 1U;
	authority.observation.data.sight.hypothesis = hypothesis;
	CHECK(SealAuthority(&authority));
	CHECK(SG_CompactBeliefPerceptionObserve(&binding, Authority(&authority)) ==
		SG_COMPACT_BELIEF_PERCEPTION_APPLIED);
	memset(&prediction, 0, sizeof(prediction));
	CHECK(SG_CompactBeliefPerceptionPredict(&binding, 1U, &predicted_target,
		750U, scratch_first, scratch_second,
		sizeof(scratch_first) / sizeof(scratch_first[0]), predicted_particles,
		sizeof(predicted_particles) / sizeof(predicted_particles[0]),
		&prediction) == SG_COMPACT_BELIEF_PERCEPTION_APPLIED);
	CHECK(prediction.at_time_ms == 750U && prediction.particle_count > 0U);
	CHECK(prediction.source.rune_identity == fixture.snapshot.identity);
	CHECK(SG_CompactBeliefPerceptionFrame(&binding, 1U, 800U) ==
		SG_COMPACT_BELIEF_PERCEPTION_APPLIED);
	view = SG_CompactBeliefPerceptionView(&binding, 1U, &predicted_target);
	CHECK(view != NULL && view->updated_at_ms == 800U);
	Teardown();
}

int main(void)
{
	TestBindingAndOpaqueAuthority();
	TestSightAndNegativeCellExclusion();
	TestProofsSoundAndDamage();
	TestItemsFlagsReportsAndPrediction();
	return failures == 0 ? 0 : 1;
}
