#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "slipgate/sg_belief_runtime.h"

/* These raw symbols are deliberately absent from the public header.  The
 * fixture exercises the compact-owner path, while RawObserve proves a forged
 * caller-filled payload has no admission authority. */
extern sg_belief_runtime_observe_result_t
	SG_BeliefRuntimeObserveFromCompactOwner(
		const sg_belief_runtime_observation_t *observation);
extern sg_belief_runtime_observe_result_t SG_BeliefRuntimeObserve(
	const sg_belief_runtime_observation_t *observation);
static sg_belief_runtime_observe_result_t (*const RawObserve)(
	const sg_belief_runtime_observation_t *observation) =
	SG_BeliefRuntimeObserve;

#define SG_BeliefRuntimeObserve SG_BeliefRuntimeObserveFromCompactOwner

static int failures;
static int provider_current = 1;
static int propagation_enabled;
static int propagation_overflow;
static int propagation_overflow_cell;
static int propagation_wrong_direction;
static int propagation_nonadjacent;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static sg_rune_compact_model_t model;
static sg_rune_compact_cell_t cells[3];
static sg_rune_compact_portal_t portals[1];
static sg_rune_compact_incidence_t incidences[2];

static void FixtureInit(void)
{
	memset(&model, 0, sizeof(model));
	memset(cells, 0, sizeof(cells));
	memset(portals, 0, sizeof(portals));
	memset(incidences, 0, sizeof(incidences));
	cells[0].valid_stances = SG_RUNE_STANCE_VALID_STANDING;
	cells[1].valid_stances = SG_RUNE_STANCE_VALID_STANDING;
	cells[2].valid_stances = SG_RUNE_STANCE_VALID_STANDING;
	model.version = SG_RUNE_COMPACT_MODEL_VERSION;
	model.schema_tag = SG_RUNE_COMPACT_MODEL_SCHEMA_TAG;
	model.cells = cells;
	model.cell_count = 3U;
	incidences[0].cell.value = 0U;
	incidences[1].cell.value = 1U;
	portals[0].negative_incidence.value = 0U;
	portals[0].positive_incidence.value = 1U;
	portals[0].direction = SG_RUNE_PORTAL_CONTINUITY_BOTH;
	portals[0].valid_stances = SG_RUNE_STANCE_VALID_STANDING;
	model.portals = portals;
	model.portal_count = 1U;
	model.incidences = incidences;
	model.incidence_count = 2U;
	provider_current = 1;
	propagation_enabled = 0;
	propagation_overflow = 0;
	propagation_overflow_cell = -1;
	propagation_wrong_direction = 0;
	propagation_nonadjacent = 0;
}

static int Locate(void *context, const sg_rune_compact_model_t *input,
	const float position[3], sg_belief_runtime_cell_state_t *cell_out)
{
	(void)context;
	if (input != &model || !position || !cell_out || !isfinite(position[0]))
		return 0;
	memset(cell_out, 0, sizeof(*cell_out));
	cell_out->location.cell.value = position[0] < 100.0f ? 0U :
		(position[0] < 250.0f ? 1U : 2U);
	cell_out->location.valid_stances = SG_RUNE_STANCE_VALID_STANDING;
	cell_out->known_components = SG_BELIEF_RUNTIME_CELL_MOTION;
	cell_out->motion = SG_RUNE_MOTION_SUPPORTED;
	return 1;
}

static int Current(void *context, const sg_rune_compact_model_t *input,
	const sg_rune_compact_identity_t *identity, uint64_t rune_identity,
	uint64_t topology_revision, uint64_t generation)
{
	(void)context;
	return provider_current && input == &model && identity == &model.identity &&
		rune_identity == 77U && topology_revision == 9U && generation == 1U;
}

static void TransitionSet(sg_belief_runtime_propagation_t *transition,
	const sg_belief_runtime_particle_t *particle, uint32_t cell, float x,
	uint32_t portal, float likelihood)
{
	memset(transition, 0, sizeof(*transition));
	transition->cell.location.cell.value = cell;
	transition->cell.location.valid_stances = SG_RUNE_STANCE_VALID_STANDING;
	transition->portal.value = portal;
	memcpy(transition->position, particle->position,
		sizeof(transition->position));
	memcpy(transition->velocity, particle->velocity,
		sizeof(transition->velocity));
	memcpy(transition->acceleration, particle->acceleration,
		sizeof(transition->acceleration));
	memcpy(transition->orientation, particle->orientation,
		sizeof(transition->orientation));
	transition->position[0] = x;
	transition->likelihood = likelihood;
}

/* The fixture's sole published portal is the only cross-cell movement this
 * callback may emit.  This makes the runtime test prove the sparse boundary
 * rather than merely moving a particle by coordinate convention. */
static int Propagate(void *context, const sg_rune_compact_model_t *input,
	const sg_belief_runtime_particle_t *particle, uint64_t from_ms,
	uint64_t to_ms, sg_belief_runtime_propagation_t *transitions,
	size_t capacity, size_t *count_out)
{
	const sg_rune_compact_portal_t *portal;

	(void)context;
	if (input != &model || !particle || !transitions || !count_out ||
		from_ms >= to_ms || capacity == 0U || input->portals != portals ||
		input->portal_count != 1U || input->incidences != incidences ||
		input->incidence_count != 2U)
		return 0;
	if (propagation_overflow != 0 && (propagation_overflow_cell < 0 ||
		particle->cell.location.cell.value == (uint32_t)propagation_overflow_cell))
	{
		*count_out = SG_BELIEF_RUNTIME_MAX_PARTICLES + 1U;
		return 1;
	}
	if (propagation_wrong_direction != 0 &&
		particle->cell.location.cell.value == 1U)
	{
		TransitionSet(&transitions[0], particle, 0U, 10.0f, 0U, 1.0f);
		*count_out = 1U;
		return 1;
	}
	if (propagation_nonadjacent != 0 &&
		particle->cell.location.cell.value == 0U)
	{
		TransitionSet(&transitions[0], particle, 2U, 300.0f, 0U, 1.0f);
		*count_out = 1U;
		return 1;
	}
	portal = &input->portals[0];
	if (propagation_enabled != 0 &&
		particle->cell.location.cell.value ==
			input->incidences[portal->negative_incidence.value].cell.value &&
		portal->direction == SG_RUNE_PORTAL_CONTINUITY_BOTH &&
		portal->valid_stances == SG_RUNE_STANCE_VALID_STANDING)
	{
		if (capacity < 2U || input->incidences[portal->positive_incidence.value]
			.cell.value != 1U)
			return 0;
		TransitionSet(&transitions[0], particle, 0U, 10.0f,
			SG_RUNE_COMPACT_INDEX_NONE, 1.0f);
		TransitionSet(&transitions[1], particle, 1U, 200.0f, 0U, 1.0f);
		*count_out = 2U;
		return 1;
	}
	TransitionSet(&transitions[0], particle,
		particle->cell.location.cell.value, particle->position[0],
		SG_RUNE_COMPACT_INDEX_NONE, 1.0f);
	*count_out = 1U;
	return 1;
}

static sg_belief_runtime_provider_t Provider(void)
{
	sg_belief_runtime_provider_t provider;

	memset(&provider, 0, sizeof(provider));
	provider.model = &model;
	provider.identity = &model.identity;
	provider.rune_identity = 77U;
	provider.topology_revision = 9U;
	provider.generation = 1U;
	provider.policy.confidence_decay_ms = 1000U;
	provider.policy.spread_growth_per_ms = 0.25f;
	provider.locate = Locate;
	provider.propagate = Propagate;
	provider.current = Current;
	return provider;
}

static sg_belief_runtime_life_t Life(uint32_t client, uint64_t generation)
{
	sg_belief_runtime_life_t life;

	memset(&life, 0, sizeof(life));
	life.client_id = client;
	life.spawn_generation = generation;
	return life;
}

static sg_belief_runtime_observation_t Observation(
	const sg_belief_runtime_hypothesis_t *hypotheses, size_t count,
	uint64_t sequence)
{
	sg_belief_runtime_observation_t observation;

	memset(&observation, 0, sizeof(observation));
	observation.authenticated = 1U;
	observation.authority = SG_BELIEF_RUNTIME_AUTHORITY_HOST_SENSOR;
	observation.audience_team = 1U;
	observation.target_team = 2U;
	observation.source = SG_BELIEF_RUNTIME_SOURCE_SOUND;
	observation.evidence_kind = SG_BELIEF_RUNTIME_EVIDENCE_POSITIVE;
	observation.issuer_life = Life(1U, 11U);
	observation.target_life = Life(2U, 22U);
	observation.event_id = sequence;
	observation.evidence_sequence = sequence;
	observation.observed_at_ms = 100U + sequence;
	observation.authenticated_at_ms = 100U + sequence;
	observation.valid_until_ms = 1200U + sequence;
	observation.rune_identity = 77U;
	observation.topology_revision = 9U;
	observation.confidence = 0.8f;
	observation.hypotheses = hypotheses;
	observation.hypothesis_count = count;
	return observation;
}

static sg_belief_runtime_hypothesis_t Hypothesis(float x, float spread,
	float likelihood)
{
	sg_belief_runtime_hypothesis_t hypothesis;

	memset(&hypothesis, 0, sizeof(hypothesis));
	hypothesis.position[0] = x;
	hypothesis.velocity[0] = 10.0f;
	hypothesis.spread_radius = spread;
	hypothesis.likelihood = likelihood;
	return hypothesis;
}

static void MakeNegative(sg_belief_runtime_observation_t *observation,
	const sg_belief_runtime_coverage_t *coverage, size_t coverage_count,
	uint64_t sequence)
{
	*observation = Observation(NULL, 0U, sequence);
	observation->source = SG_BELIEF_RUNTIME_SOURCE_SIGHT;
	observation->evidence_kind = SG_BELIEF_RUNTIME_EVIDENCE_NEGATIVE;
	observation->confidence = 1.0f;
	observation->coverage = coverage;
	observation->coverage_count = coverage_count;
}

int main(void)
{
	sg_belief_runtime_provider_t provider;
	sg_belief_runtime_hypothesis_t hypotheses[2];
	sg_belief_runtime_observation_t observation;
	sg_belief_runtime_coverage_t coverage[2];
	const sg_belief_runtime_view_t *view;
	float confidence_after_first_miss = 0.0f;
	uint64_t original_updated_at_ms = 0U;
	uint64_t original_valid_until_ms = 0U;

	FixtureInit();
	provider = Provider();
	CHECK(SG_BeliefRuntimeProviderSet(&provider));
	CHECK(SG_BeliefRuntimeProviderAvailable());
	CHECK(SG_BeliefRuntimeProvider() != NULL);

	hypotheses[0] = Hypothesis(10.0f, 4.0f, 1.0f);
	hypotheses[1] = Hypothesis(200.0f, 4.0f, 3.0f);
	observation = Observation(hypotheses, 2U, 1U);
	CHECK(RawObserve(&observation) == SG_BELIEF_RUNTIME_OBSERVE_REJECTED);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	view = SG_BeliefRuntimeViewForClient(1U, 2U, 101U);
	CHECK(view != NULL);
	if (view)
	{
		CHECK(view->particle_count == 2U);
		CHECK(view->particles[0].cell.location.cell.value == 0U);
		CHECK(view->particles[1].cell.location.cell.value == 1U);
		CHECK(fabsf(view->particles[0].weight + view->particles[1].weight -
			1.0f) < 0.0001f);
		CHECK(view->exact_sight == 0U);
	}

	CHECK(SG_BeliefRuntimeFrame(1U, 600U) ==
		SG_BELIEF_RUNTIME_FRAME_APPLIED);
	view = SG_BeliefRuntimeViewForClient(1U, 2U, 600U);
	CHECK(view != NULL);
	if (view)
	{
		CHECK(view->confidence > 0.0f && view->confidence < 0.8f);
		CHECK(view->particles[0].spread_radius > 4.0f);
	}

	/* A later coarse sound cannot erase a current exact sight. */
	hypotheses[0] = Hypothesis(10.0f, 0.0f, 1.0f);
	observation = Observation(hypotheses, 1U, 2U);
	observation.source = SG_BELIEF_RUNTIME_SOURCE_SIGHT;
	observation.confidence = 1.0f;
	observation.observed_at_ms = 601U;
	observation.authenticated_at_ms = 601U;
	observation.valid_until_ms = 2000U;
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	hypotheses[0] = Hypothesis(200.0f, 64.0f, 1.0f);
	observation = Observation(hypotheses, 1U, 3U);
	observation.observed_at_ms = 602U;
	observation.authenticated_at_ms = 602U;
	observation.valid_until_ms = 2000U;
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	view = SG_BeliefRuntimeViewForClient(1U, 2U, 602U);
	CHECK(view != NULL && view->exact_sight == 1U);
	if (view)
		CHECK(view->particles[0].position[0] == 10.0f);

	/* Sound and damage are coarse evidence; exact zero-spread input is refused. */
	hypotheses[0] = Hypothesis(10.0f, 0.0f, 1.0f);
	observation = Observation(hypotheses, 1U, 1U);
	observation.target_life = Life(3U, 33U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_REJECTED);

	/* Coverage removes only cells the host sensor actually inspected. */
	hypotheses[0] = Hypothesis(10.0f, 16.0f, 1.0f);
	hypotheses[1] = Hypothesis(200.0f, 16.0f, 1.0f);
	observation = Observation(hypotheses, 2U, 1U);
	observation.target_life = Life(4U, 44U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	memset(coverage, 0, sizeof(coverage));
	coverage[0].location.cell.value = 0U;
	coverage[0].location.valid_stances = SG_RUNE_STANCE_VALID_STANDING;
	MakeNegative(&observation, coverage, 1U, 2U);
	observation.target_life = Life(4U, 44U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	view = SG_BeliefRuntimeViewForClient(1U, 4U, 102U);
	CHECK(view != NULL && view->particle_count == 1U);
	if (view)
		CHECK(view->particles[0].cell.location.cell.value == 1U);
	coverage[0].location.cell.value = 1U;
	MakeNegative(&observation, coverage, 1U, 3U);
	observation.target_life = Life(4U, 44U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	CHECK(SG_BeliefRuntimeViewForClient(1U, 4U, 103U) == NULL);

	/* Repeated misses subtract only what was observed. They must not renew the
	 * surviving estimate or stop its existing decay. */
	hypotheses[0] = Hypothesis(10.0f, 16.0f, 1.0f);
	hypotheses[1] = Hypothesis(200.0f, 16.0f, 1.0f);
	observation = Observation(hypotheses, 2U, 1U);
	observation.target_life = Life(6U, 66U);
	observation.valid_until_ms = 1000U;
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	original_updated_at_ms = observation.authenticated_at_ms;
	original_valid_until_ms = observation.valid_until_ms;
	memset(coverage, 0, sizeof(coverage));
	coverage[0].location.cell.value = 0U;
	coverage[0].location.valid_stances = SG_RUNE_STANCE_VALID_STANDING;
	MakeNegative(&observation, coverage, 1U, 2U);
	observation.target_life = Life(6U, 66U);
	observation.observed_at_ms = 600U;
	observation.authenticated_at_ms = 600U;
	observation.valid_until_ms = 2000U;
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	view = SG_BeliefRuntimeViewForClient(1U, 6U, 600U);
	CHECK(view != NULL && view->particle_count == 1U);
	if (view)
	{
		confidence_after_first_miss = view->confidence;
		CHECK(view->updated_at_ms == original_updated_at_ms);
		CHECK(view->valid_until_ms == original_valid_until_ms);
	}
	MakeNegative(&observation, coverage, 1U, 3U);
	observation.target_life = Life(6U, 66U);
	observation.observed_at_ms = 900U;
	observation.authenticated_at_ms = 900U;
	observation.valid_until_ms = 3000U;
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	view = SG_BeliefRuntimeViewForClient(1U, 6U, 900U);
	CHECK(view != NULL && view->particle_count == 1U);
	if (view)
	{
		CHECK(view->confidence > 0.0f);
		CHECK(view->confidence < confidence_after_first_miss);
		CHECK(view->updated_at_ms == original_updated_at_ms);
		CHECK(view->valid_until_ms == original_valid_until_ms);
	}
	CHECK(SG_BeliefRuntimeViewForClient(1U, 6U,
		original_valid_until_ms) == NULL);

	/* Coarse observations fuse sparse compact cells instead of replacing the
	 * previous possibility set. */
	hypotheses[0] = Hypothesis(10.0f, 16.0f, 1.0f);
	observation = Observation(hypotheses, 1U, 1U);
	observation.target_life = Life(5U, 55U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	hypotheses[0] = Hypothesis(200.0f, 16.0f, 1.0f);
	observation = Observation(hypotheses, 1U, 2U);
	observation.target_life = Life(5U, 55U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	view = SG_BeliefRuntimeViewForClient(1U, 5U, 102U);
	CHECK(view != NULL && view->particle_count == 2U);

	/* A view must fail closed once its original validity window expires. */
	CHECK(SG_BeliefRuntimeViewForClient(1U, 2U, 2000U) == NULL);
	CHECK(SG_BeliefRuntimeFrame(2U, 2000U) ==
		SG_BELIEF_RUNTIME_FRAME_APPLIED);
	CHECK(SG_BeliefRuntimeViewForClient(1U, 2U, 2001U) == NULL);

	/* A propagated sight belief branches only over the callback's published
	 * compact portal.  It remains normalized, becomes predictive rather than
	 * exact aim, and carries an explicit future-time bucket. */
	propagation_enabled = 1;
	hypotheses[0] = Hypothesis(10.0f, 0.0f, 1.0f);
	observation = Observation(hypotheses, 1U, 1U);
	observation.source = SG_BELIEF_RUNTIME_SOURCE_SIGHT;
	observation.confidence = 1.0f;
	observation.target_life = Life(10U, 100U);
	observation.observed_at_ms = 2100U;
	observation.authenticated_at_ms = 2100U;
	observation.valid_until_ms = 4000U;
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	CHECK(SG_BeliefRuntimeFrame(3U, 2200U) ==
		SG_BELIEF_RUNTIME_FRAME_APPLIED);
	view = SG_BeliefRuntimeViewForClient(1U, 10U, 2200U);
	CHECK(view != NULL && view->particle_count == 2U);
	if (view)
	{
		CHECK(view->exact_sight == 0U);
		CHECK(view->particles[0].cell.location.cell.value == 0U);
		CHECK(view->particles[1].cell.location.cell.value == 1U);
		CHECK(view->particles[0].future_at_ms == 2200U);
		CHECK(view->particles[1].future_at_ms == 2200U);
		CHECK(fabsf(view->particles[0].weight + view->particles[1].weight -
			1.0f) < 0.0001f);
	}
	/* A provider that cannot represent every successor fails loudly and leaves
	 * the prior sparse distribution available for the next valid frame. */
	propagation_overflow = 1;
	CHECK(SG_BeliefRuntimeFrame(4U, 2300U) ==
		SG_BELIEF_RUNTIME_FRAME_OVERFLOW);
	view = SG_BeliefRuntimeViewForClient(1U, 10U, 2200U);
	CHECK(view != NULL && view->particle_count == 2U);
	propagation_overflow = 0;
	propagation_enabled = 0;

	/* Each nonvisual runtime source stays diffuse.  Known item and flag events
	 * can localize a cell, but neither is marked as visual exact aim. */
	{
		static const sg_belief_runtime_source_t diffuse_sources[] = {
			SG_BELIEF_RUNTIME_SOURCE_SOUND,
			SG_BELIEF_RUNTIME_SOURCE_DAMAGE,
			SG_BELIEF_RUNTIME_SOURCE_WEAPON_FIRE,
			SG_BELIEF_RUNTIME_SOURCE_HOOK,
			SG_BELIEF_RUNTIME_SOURCE_MECHANISM,
			SG_BELIEF_RUNTIME_SOURCE_WATER
		};
		size_t source_index;

		for (source_index = 0U; source_index <
			sizeof(diffuse_sources) / sizeof(diffuse_sources[0]); source_index++)
		{
			hypotheses[0] = Hypothesis(10.0f, 24.0f, 1.0f);
			observation = Observation(hypotheses, 1U, 1U);
			observation.source = diffuse_sources[source_index];
			observation.target_life = Life((uint32_t)(20U + source_index),
				(uint64_t)(200U + source_index));
			CHECK(SG_BeliefRuntimeObserve(&observation) ==
				SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
			view = SG_BeliefRuntimeViewForClient(1U,
				observation.target_life.client_id,
				observation.authenticated_at_ms);
			CHECK(view != NULL && view->exact_sight == 0U &&
				view->particles[0].spread_radius > 0.0f);
			hypotheses[0].spread_radius = 0.0f;
			observation = Observation(hypotheses, 1U, 2U);
			observation.source = diffuse_sources[source_index];
			observation.target_life = Life((uint32_t)(40U + source_index),
				(uint64_t)(400U + source_index));
			CHECK(SG_BeliefRuntimeObserve(&observation) ==
				SG_BELIEF_RUNTIME_OBSERVE_REJECTED);
		}
		hypotheses[0] = Hypothesis(10.0f, 0.0f, 1.0f);
		observation = Observation(hypotheses, 1U, 1U);
		observation.source = SG_BELIEF_RUNTIME_SOURCE_ITEM;
		observation.target_life = Life(30U, 300U);
		CHECK(SG_BeliefRuntimeObserve(&observation) ==
			SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
		view = SG_BeliefRuntimeViewForClient(1U, 30U,
			observation.authenticated_at_ms);
		CHECK(view != NULL && view->exact_sight == 0U);
		observation = Observation(hypotheses, 1U, 1U);
		observation.source = SG_BELIEF_RUNTIME_SOURCE_FLAG;
		observation.target_life = Life(31U, 310U);
		CHECK(SG_BeliefRuntimeObserve(&observation) ==
			SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
		hypotheses[0] = Hypothesis(10.0f, 24.0f, 1.0f);
		hypotheses[0].weapon_state = SG_BELIEF_RUNTIME_WEAPON_FIRING;
		observation = Observation(hypotheses, 1U, 1U);
		observation.source = SG_BELIEF_RUNTIME_SOURCE_WEAPON_FIRE;
		observation.target_life = Life(32U, 320U);
		CHECK(SG_BeliefRuntimeObserve(&observation) ==
			SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
		view = SG_BeliefRuntimeViewForClient(1U, 32U,
			observation.authenticated_at_ms);
		CHECK(view != NULL && view->particles[0].weapon_state ==
			SG_BELIEF_RUNTIME_WEAPON_FIRING);
	}

	/* Sound keeps every supplied region: no mean collapse means a sound-only
	 * observation cannot turn into a justified single-cell aim target. */
	hypotheses[0] = Hypothesis(10.0f, 32.0f, 1.0f);
	hypotheses[1] = Hypothesis(200.0f, 32.0f, 3.0f);
	observation = Observation(hypotheses, 2U, 1U);
	observation.target_life = Life(60U, 600U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	view = SG_BeliefRuntimeViewForClient(1U, 60U,
		observation.authenticated_at_ms);
	CHECK(view != NULL && view->exact_sight == 0U &&
		view->particle_count == 2U);
	if (view)
		CHECK(view->particles[1].weight > view->particles[0].weight);

	/* Reports need the host's separate teammate-report authority and retain the
	 * reported source's uncertainty rules.  Their audience key is independent
	 * from an otherwise identical belief held by the other team. */
	hypotheses[0] = Hypothesis(10.0f, 32.0f, 1.0f);
	observation = Observation(hypotheses, 1U, 1U);
	observation.source = SG_BELIEF_RUNTIME_SOURCE_TEAMMATE;
	observation.reported_source = SG_BELIEF_RUNTIME_SOURCE_SOUND;
	observation.authority = SG_BELIEF_RUNTIME_AUTHORITY_HOST_TEAMMATE_REPORT;
	observation.target_life = Life(70U, 700U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	CHECK(SG_BeliefRuntimeViewForClient(1U, 70U,
		observation.authenticated_at_ms) != NULL);
	CHECK(SG_BeliefRuntimeViewForClient(2U, 70U,
		observation.authenticated_at_ms) == NULL);
	observation.authenticated = 0U;
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_REJECTED);
	observation.authenticated = 1U;
	observation.authority = SG_BELIEF_RUNTIME_AUTHORITY_HOST_SENSOR;
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_REJECTED);
	observation.authority = SG_BELIEF_RUNTIME_AUTHORITY_HOST_TEAMMATE_REPORT;
	observation.reported_source = SG_BELIEF_RUNTIME_SOURCE_TEAMMATE;
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_REJECTED);

	observation = Observation(hypotheses, 1U, 1U);
	observation.audience_team = 2U;
	observation.target_team = 1U;
	observation.target_life = Life(70U, 700U);
	observation.issuer_life = Life(71U, 710U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	CHECK(SG_BeliefRuntimeViewForClient(2U, 70U,
		observation.authenticated_at_ms) != NULL);

	/* Life fences outlive sparse mass.  Retirement rejects the exact retired
	 * target, a newer generation clears the old target track, and late issuer
	 * generations cannot create fresh evidence. */
	hypotheses[0] = Hypothesis(10.0f, 16.0f, 1.0f);
	observation = Observation(hypotheses, 1U, 1U);
	observation.target_life = Life(80U, 800U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	CHECK(SG_BeliefRuntimeRetireLife(&observation.target_life));
	CHECK(SG_BeliefRuntimeViewForClient(1U, 80U,
		observation.authenticated_at_ms) == NULL);
	observation = Observation(hypotheses, 1U, 2U);
	observation.target_life = Life(80U, 800U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_REJECTED);

	observation = Observation(hypotheses, 1U, 1U);
	observation.target_life = Life(81U, 1U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	observation = Observation(hypotheses, 1U, 1U);
	observation.target_life = Life(81U, 2U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	CHECK(SG_BeliefRuntimeView(1U, &((sg_belief_runtime_life_t){
		.client_id = 81U, .spawn_generation = 1U}), 101U) == NULL);
	observation = Observation(hypotheses, 1U, 2U);
	observation.target_life = Life(81U, 1U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_REJECTED);

	observation = Observation(hypotheses, 1U, 1U);
	observation.issuer_life = Life(82U, 1U);
	observation.target_life = Life(83U, 1U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	observation = Observation(hypotheses, 1U, 1U);
	observation.issuer_life = Life(82U, 2U);
	observation.target_life = Life(84U, 1U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	observation = Observation(hypotheses, 1U, 1U);
	observation.issuer_life = Life(82U, 1U);
	observation.target_life = Life(85U, 1U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_REJECTED);

	/* Negative sight may clear every particle, never the replay fence. */
	observation = Observation(hypotheses, 1U, 1U);
	observation.target_life = Life(86U, 1U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	memset(coverage, 0, sizeof(coverage));
	coverage[0].location.cell.value = 0U;
	coverage[0].location.valid_stances = SG_RUNE_STANCE_VALID_STANDING;
	MakeNegative(&observation, coverage, 1U, 2U);
	observation.target_life = Life(86U, 1U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	CHECK(SG_BeliefRuntimeViewForClient(1U, 86U, 102U) == NULL);
	observation = Observation(hypotheses, 1U, 2U);
	observation.target_life = Life(86U, 1U);
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_REJECTED);

	/* The runtime rejects a successor against a one-way portal in its forbidden
	 * direction and a target cell that is not an endpoint of the named portal. */
	portals[0].direction = SG_RUNE_PORTAL_CONTINUITY_NEGATIVE_TO_POSITIVE;
	propagation_wrong_direction = 1;
	hypotheses[0] = Hypothesis(200.0f, 16.0f, 1.0f);
	observation = Observation(hypotheses, 1U, 1U);
	observation.target_life = Life(87U, 1U);
	observation.observed_at_ms = 3000U;
	observation.authenticated_at_ms = 3000U;
	observation.valid_until_ms = 5000U;
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	CHECK(SG_BeliefRuntimeFrame(5U, 3100U) ==
		SG_BELIEF_RUNTIME_FRAME_REJECTED);
	view = SG_BeliefRuntimeViewForClient(1U, 87U, 3000U);
	CHECK(view != NULL && view->particles[0].future_at_ms == 3000U);
	propagation_wrong_direction = 0;
	CHECK(SG_BeliefRuntimeFrame(5U, 3100U) ==
		SG_BELIEF_RUNTIME_FRAME_APPLIED);
	portals[0].direction = SG_RUNE_PORTAL_CONTINUITY_BOTH;
	propagation_nonadjacent = 1;
	hypotheses[0] = Hypothesis(10.0f, 16.0f, 1.0f);
	observation = Observation(hypotheses, 1U, 1U);
	observation.target_life = Life(88U, 1U);
	observation.observed_at_ms = 3200U;
	observation.authenticated_at_ms = 3200U;
	observation.valid_until_ms = 5000U;
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	CHECK(SG_BeliefRuntimeFrame(6U, 3300U) ==
		SG_BELIEF_RUNTIME_FRAME_REJECTED);
	propagation_nonadjacent = 0;
	CHECK(SG_BeliefRuntimeFrame(6U, 3300U) ==
		SG_BELIEF_RUNTIME_FRAME_APPLIED);

	/* Frame aging is all-or-nothing.  The first target stages successfully,
	 * the second overflows, and neither future bucket advances until retry. */
	CHECK(SG_BeliefRuntimeRetireLife(&((sg_belief_runtime_life_t){
		.client_id = 87U, .spawn_generation = 1U})));
	CHECK(SG_BeliefRuntimeRetireLife(&((sg_belief_runtime_life_t){
		.client_id = 88U, .spawn_generation = 1U})));
	hypotheses[0] = Hypothesis(200.0f, 16.0f, 1.0f);
	observation = Observation(hypotheses, 1U, 1U);
	observation.target_life = Life(90U, 1U);
	observation.observed_at_ms = 4000U;
	observation.authenticated_at_ms = 4000U;
	observation.valid_until_ms = 6000U;
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	hypotheses[0] = Hypothesis(10.0f, 16.0f, 1.0f);
	observation = Observation(hypotheses, 1U, 1U);
	observation.target_life = Life(91U, 1U);
	observation.observed_at_ms = 4000U;
	observation.authenticated_at_ms = 4000U;
	observation.valid_until_ms = 6000U;
	CHECK(SG_BeliefRuntimeObserve(&observation) ==
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED);
	propagation_overflow = 1;
	propagation_overflow_cell = 0;
	CHECK(SG_BeliefRuntimeFrame(7U, 4100U) ==
		SG_BELIEF_RUNTIME_FRAME_OVERFLOW);
	view = SG_BeliefRuntimeViewForClient(1U, 90U, 4000U);
	CHECK(view != NULL && view->particles[0].future_at_ms == 4000U);
	view = SG_BeliefRuntimeViewForClient(1U, 91U, 4000U);
	CHECK(view != NULL && view->particles[0].future_at_ms == 4000U);
	propagation_overflow = 0;
	propagation_overflow_cell = -1;
	CHECK(SG_BeliefRuntimeFrame(7U, 4100U) ==
		SG_BELIEF_RUNTIME_FRAME_APPLIED);
	view = SG_BeliefRuntimeViewForClient(1U, 90U, 4100U);
	CHECK(view != NULL && view->particles[0].future_at_ms == 4100U);
	view = SG_BeliefRuntimeViewForClient(1U, 91U, 4100U);
	CHECK(view != NULL && view->particles[0].future_at_ms == 4100U);

	provider_current = 0;
	CHECK(!SG_BeliefRuntimeProviderAvailable());
	CHECK(SG_BeliefRuntimeViewForClient(1U, 2U, 2001U) == NULL);
	CHECK(SG_BeliefRuntimeProviderSet(NULL));
	return failures == 0 ? 0 : 1;
}
