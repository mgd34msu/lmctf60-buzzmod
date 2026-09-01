#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "slipgate/sg_compact_belief_perception.h"

/* Trusted-host-only declarations intentionally absent from the public
 * adapter header.  Gameplay can submit opaque evidence, not install a raw
 * decoder or invoke the runtime admission path. */
typedef int (*sg_compact_belief_perception_observation_consume_fn)(
	void *context, const sg_belief_runtime_observation_t *observation);
typedef int (*sg_compact_belief_perception_evidence_decode_fn)(
	void *context, const sg_belief_runtime_provider_t *provider,
	const sg_compact_belief_perception_evidence_authority_t *authority,
	sg_compact_belief_perception_observation_consume_fn consume,
	void *consume_context);
extern sg_compact_belief_perception_result_t
	SG_CompactBeliefPerceptionBindTrustedOwner(
		sg_compact_belief_perception_binding_t *binding,
		const sg_belief_runtime_provider_t *provider,
		sg_compact_belief_perception_evidence_decode_fn decode_evidence,
		void *decode_context);

#define SG_CompactBeliefPerceptionBind \
	SG_CompactBeliefPerceptionBindTrustedOwner

static int failures;
static int provider_current = 1;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

struct sg_compact_belief_perception_evidence_authority_s
{
	uint8_t allowed;
	uint8_t reserved[7];
	sg_belief_runtime_observation_t observation;
	sg_belief_runtime_hypothesis_t hypotheses[2];
	sg_belief_runtime_coverage_t coverage[2];
};

static sg_rune_compact_model_t model;
static sg_rune_compact_cell_t cell;

static int Locate(void *context, const sg_rune_compact_model_t *input,
	const float position[3], sg_belief_runtime_cell_state_t *out)
{
	(void)context;
	if (input != &model || !position || !out)
		return 0;
	memset(out, 0, sizeof(*out));
	out->location.cell.value = 0U;
	out->location.valid_stances = SG_RUNE_STANCE_VALID_STANDING;
	return 1;
}

static int Current(void *context, const sg_rune_compact_model_t *input,
	const sg_rune_compact_identity_t *identity, uint64_t rune_identity,
	uint64_t topology_revision, uint64_t generation)
{
	(void)context;
	return provider_current && input == &model && identity == &model.identity &&
		rune_identity == 88U && topology_revision == 4U && generation == 3U;
}

static sg_belief_runtime_life_t Life(uint32_t client, uint64_t generation)
{
	sg_belief_runtime_life_t life;

	memset(&life, 0, sizeof(life));
	life.client_id = client;
	life.spawn_generation = generation;
	return life;
}

static sg_belief_runtime_provider_t Provider(void)
{
	sg_belief_runtime_provider_t provider;

	memset(&model, 0, sizeof(model));
	memset(&cell, 0, sizeof(cell));
	cell.valid_stances = SG_RUNE_STANCE_VALID_STANDING;
	model.version = SG_RUNE_COMPACT_MODEL_VERSION;
	model.schema_tag = SG_RUNE_COMPACT_MODEL_SCHEMA_TAG;
	model.cells = &cell;
	model.cell_count = 1U;
	memset(&provider, 0, sizeof(provider));
	provider.model = &model;
	provider.identity = &model.identity;
	provider.rune_identity = 88U;
	provider.topology_revision = 4U;
	provider.generation = 3U;
	provider.policy.confidence_decay_ms = 1000U;
	provider.policy.spread_growth_per_ms = 0.1f;
	provider.locate = Locate;
	provider.current = Current;
	provider_current = 1;
	return provider;
}

static void AuthorityInit(
	struct sg_compact_belief_perception_evidence_authority_s *authority,
	sg_belief_runtime_source_t source, uint64_t sequence)
{
	memset(authority, 0, sizeof(*authority));
	authority->allowed = 1U;
	authority->hypotheses[0].position[0] = 32.0f;
	authority->hypotheses[0].spread_radius =
		source == SG_BELIEF_RUNTIME_SOURCE_SIGHT ? 0.0f : 16.0f;
	authority->hypotheses[0].likelihood = 1.0f;
	authority->observation.authenticated = 1U;
	authority->observation.authority = SG_BELIEF_RUNTIME_AUTHORITY_HOST_SENSOR;
	authority->observation.audience_team = 1U;
	authority->observation.target_team = 2U;
	authority->observation.source = source;
	authority->observation.evidence_kind = SG_BELIEF_RUNTIME_EVIDENCE_POSITIVE;
	authority->observation.issuer_life = Life(3U, 33U);
	authority->observation.target_life = Life(7U, 77U);
	authority->observation.event_id = sequence;
	authority->observation.evidence_sequence = sequence;
	authority->observation.observed_at_ms = 100U + sequence;
	authority->observation.authenticated_at_ms = 100U + sequence;
	authority->observation.valid_until_ms = 1100U + sequence;
	authority->observation.rune_identity = 88U;
	authority->observation.topology_revision = 4U;
	authority->observation.confidence = 1.0f;
	authority->observation.hypotheses = authority->hypotheses;
	authority->observation.hypothesis_count = 1U;
}

static void AuthorityNegative(
	struct sg_compact_belief_perception_evidence_authority_s *authority,
	uint64_t sequence)
{
	AuthorityInit(authority, SG_BELIEF_RUNTIME_SOURCE_SIGHT, sequence);
	authority->observation.evidence_kind = SG_BELIEF_RUNTIME_EVIDENCE_NEGATIVE;
	authority->observation.hypotheses = NULL;
	authority->observation.hypothesis_count = 0U;
	authority->coverage[0].location.cell.value = 0U;
	authority->coverage[0].location.valid_stances =
		SG_RUNE_STANCE_VALID_STANDING;
	authority->observation.coverage = authority->coverage;
	authority->observation.coverage_count = 1U;
}

static int Decode(void *context, const sg_belief_runtime_provider_t *provider,
	const sg_compact_belief_perception_evidence_authority_t *opaque,
	sg_compact_belief_perception_observation_consume_fn consume,
	void *consume_context)
{
	const struct sg_compact_belief_perception_evidence_authority_s *authority =
		(const struct sg_compact_belief_perception_evidence_authority_s *)opaque;

	(void)context;
	if (!provider || !authority || authority->allowed != 1U || !consume ||
		authority->reserved[0] != 0U || authority->reserved[1] != 0U ||
		authority->reserved[2] != 0U || authority->reserved[3] != 0U ||
		authority->reserved[4] != 0U || authority->reserved[5] != 0U ||
		authority->reserved[6] != 0U ||
		authority->observation.rune_identity != provider->rune_identity ||
		authority->observation.topology_revision != provider->topology_revision)
		return 0;
	return consume(consume_context, &authority->observation);
}

static int DecodeTwice(void *context,
	const sg_belief_runtime_provider_t *provider,
	const sg_compact_belief_perception_evidence_authority_t *opaque,
	sg_compact_belief_perception_observation_consume_fn consume,
	void *consume_context)
{
	const struct sg_compact_belief_perception_evidence_authority_s *authority =
		(const struct sg_compact_belief_perception_evidence_authority_s *)opaque;

	(void)context;
	if (!provider || !authority || !consume)
		return 0;
	return consume(consume_context, &authority->observation) &&
		consume(consume_context, &authority->observation);
}

int main(void)
{
	sg_belief_runtime_provider_t provider = Provider();
	sg_compact_belief_perception_binding_t binding;
	sg_compact_belief_perception_binding_t forged_binding;
	struct sg_compact_belief_perception_evidence_authority_s authority;
	const sg_belief_runtime_view_t *view;

	memset(&binding, 0, sizeof(binding));
	CHECK(SG_BeliefRuntimeProviderSet(&provider));
	/* Copying the public identity fields cannot install a decoder: only the
	 * implementation-private owner registry makes a binding current. */
	memset(&forged_binding, 0, sizeof(forged_binding));
	forged_binding.model = provider.model;
	forged_binding.identity = provider.identity;
	forged_binding.rune_identity = provider.rune_identity;
	forged_binding.topology_revision = provider.topology_revision;
	forged_binding.generation = provider.generation;
	forged_binding.owner_slot = 1U;
	forged_binding.bound = 1U;
	AuthorityInit(&authority, SG_BELIEF_RUNTIME_SOURCE_SOUND, 1U);
	CHECK(SG_CompactBeliefPerceptionObserveSound(&forged_binding,
		(const sg_compact_belief_perception_evidence_authority_t *)&authority) ==
		SG_COMPACT_BELIEF_PERCEPTION_REJECTED);
	CHECK(SG_CompactBeliefPerceptionBind(&binding, &provider, Decode, NULL) ==
		SG_COMPACT_BELIEF_PERCEPTION_APPLIED);
	CHECK(SG_CompactBeliefPerceptionBindingCurrent(&binding));

	AuthorityInit(&authority, SG_BELIEF_RUNTIME_SOURCE_SIGHT, 1U);
	CHECK(SG_CompactBeliefPerceptionObserve(&binding,
		(const sg_compact_belief_perception_evidence_authority_t *)&authority) ==
		SG_COMPACT_BELIEF_PERCEPTION_APPLIED);
	view = SG_CompactBeliefPerceptionViewForClient(&binding, 1U, 7U, 101U);
	CHECK(view != NULL);
	if (view)
	{
		CHECK(view->exact_sight == 1U);
		CHECK(view->particle_count == 1U);
		CHECK(view->particles[0].cell.location.cell.value == 0U);
	}

	AuthorityInit(&authority, SG_BELIEF_RUNTIME_SOURCE_SIGHT, 2U);
	CHECK(SG_CompactBeliefPerceptionObserveSound(&binding,
		(const sg_compact_belief_perception_evidence_authority_t *)&authority) ==
		SG_COMPACT_BELIEF_PERCEPTION_REJECTED);
	AuthorityInit(&authority, SG_BELIEF_RUNTIME_SOURCE_SOUND, 2U);
	CHECK(SG_CompactBeliefPerceptionObserveSound(&binding,
		(const sg_compact_belief_perception_evidence_authority_t *)&authority) ==
		SG_COMPACT_BELIEF_PERCEPTION_APPLIED);
	view = SG_CompactBeliefPerceptionViewForClient(&binding, 1U, 7U, 102U);
	CHECK(view != NULL && view->exact_sight == 1U);

	/* Sound preserves separate sparse modes and CaptureObservation owns its
	 * copy.  Mutating the host token after consumption cannot alter the view. */
	AuthorityInit(&authority, SG_BELIEF_RUNTIME_SOURCE_SOUND, 1U);
	authority.observation.target_life = Life(8U, 88U);
	authority.hypotheses[0].position[0] = 10.0f;
	authority.hypotheses[0].spread_radius = 32.0f;
	authority.hypotheses[0].likelihood = 1.0f;
	authority.hypotheses[1].position[0] = 200.0f;
	authority.hypotheses[1].spread_radius = 32.0f;
	authority.hypotheses[1].likelihood = 3.0f;
	authority.observation.hypothesis_count = 2U;
	CHECK(SG_CompactBeliefPerceptionObserveSound(&binding,
		(const sg_compact_belief_perception_evidence_authority_t *)&authority) ==
		SG_COMPACT_BELIEF_PERCEPTION_APPLIED);
	authority.hypotheses[0].position[0] = 999.0f;
	view = SG_CompactBeliefPerceptionViewForClient(&binding, 1U, 8U, 101U);
	CHECK(view != NULL && view->particle_count == 2U &&
		view->exact_sight == 0U);
	if (view)
	{
		CHECK(view->particles[0].position[0] == 10.0f);
		CHECK(view->particles[1].weight > view->particles[0].weight);
	}
	CHECK(SG_CompactBeliefPerceptionViewForClient(&binding, 2U, 8U, 101U) ==
		NULL);

	AuthorityInit(&authority, SG_BELIEF_RUNTIME_SOURCE_DAMAGE, 3U);
	authority.observation.rune_identity = 99U;
	CHECK(SG_CompactBeliefPerceptionObserveDamage(&binding,
		(const sg_compact_belief_perception_evidence_authority_t *)&authority) ==
		SG_COMPACT_BELIEF_PERCEPTION_REJECTED);

	/* Team communication carries a distinct host authority and a report source.
	 * An opaque decoder rejects any token it did not authenticate. */
	AuthorityInit(&authority, SG_BELIEF_RUNTIME_SOURCE_TEAMMATE, 1U);
	authority.observation.target_life = Life(9U, 99U);
	authority.observation.authority =
		SG_BELIEF_RUNTIME_AUTHORITY_HOST_TEAMMATE_REPORT;
	authority.observation.reported_source = SG_BELIEF_RUNTIME_SOURCE_SOUND;
	authority.hypotheses[0].spread_radius = 32.0f;
	CHECK(SG_CompactBeliefPerceptionObserve(&binding,
		(const sg_compact_belief_perception_evidence_authority_t *)&authority) ==
		SG_COMPACT_BELIEF_PERCEPTION_APPLIED);
	view = SG_CompactBeliefPerceptionViewForClient(&binding, 1U, 9U, 101U);
	CHECK(view != NULL && view->latest_source ==
		SG_BELIEF_RUNTIME_SOURCE_TEAMMATE && view->exact_sight == 0U);
	authority.allowed = 0U;
	CHECK(SG_CompactBeliefPerceptionObserve(&binding,
		(const sg_compact_belief_perception_evidence_authority_t *)&authority) ==
		SG_COMPACT_BELIEF_PERCEPTION_REJECTED);

	/* The adapter accepts exactly one immutable decoded observation.  A decoder
	 * attempting to replay a token in one call is rejected before runtime use. */
	{
		sg_compact_belief_perception_binding_t replay_binding;

		memset(&replay_binding, 0, sizeof(replay_binding));
		CHECK(SG_CompactBeliefPerceptionBind(&replay_binding, &provider,
			DecodeTwice, NULL) == SG_COMPACT_BELIEF_PERCEPTION_APPLIED);
		AuthorityInit(&authority, SG_BELIEF_RUNTIME_SOURCE_SOUND, 1U);
		authority.observation.target_life = Life(10U, 100U);
		CHECK(SG_CompactBeliefPerceptionObserveSound(&replay_binding,
			(const sg_compact_belief_perception_evidence_authority_t *)&authority) ==
			SG_COMPACT_BELIEF_PERCEPTION_REJECTED);
		SG_CompactBeliefPerceptionUnbind(&replay_binding);
	}

	CHECK(SG_CompactBeliefPerceptionFrame(&binding, 1U, 500U) ==
		SG_COMPACT_BELIEF_PERCEPTION_APPLIED);
	AuthorityNegative(&authority, 3U);
	authority.observation.observed_at_ms = 600U;
	authority.observation.authenticated_at_ms = 600U;
	authority.observation.valid_until_ms = 1600U;
	CHECK(SG_CompactBeliefPerceptionObserve(&binding,
		(const sg_compact_belief_perception_evidence_authority_t *)&authority) ==
		SG_COMPACT_BELIEF_PERCEPTION_APPLIED);
	CHECK(SG_CompactBeliefPerceptionViewForClient(&binding, 1U, 7U, 600U) ==
		NULL);

	provider_current = 0;
	CHECK(!SG_CompactBeliefPerceptionBindingCurrent(&binding));
	SG_CompactBeliefPerceptionUnbind(&binding);
	CHECK(!SG_CompactBeliefPerceptionBindingCurrent(&binding));
	CHECK(SG_BeliefRuntimeProviderSet(NULL));
	return failures == 0 ? 0 : 1;
}
