#include <float.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <math.h>
#include <stddef.h>

#define main sg_rune_compact_model_fixture_main
#define failures sg_rune_compact_model_fixture_failures
int sg_rune_compact_model_fixture_main(void);
#include "sg_rune_compact_model_test.c"
#undef failures
#undef main
#undef CHECK

#include "../slipgate/sg_rune_compact_static.h"
#include "../slipgate/sg_rune_compact_source_surface_catalog.h"
#include "../slipgate/sg_weapon_effect_profile.h"

#define SG_RUNE_COMPACT_LEARNING_OWNER_PRIVATE 1
#include "../slipgate/sg_rune_compact_learning_owner.h"
#undef SG_RUNE_COMPACT_LEARNING_OWNER_PRIVATE

static int failures;

#if defined(SG_RUNE_COMPACT_LEARNING_TEST_WRAP_ALLOC)
static int fail_calloc_after = -1;
static int fail_realloc_after = -1;

void *__real_calloc(size_t count, size_t size);
void *__real_realloc(void *pointer, size_t size);
void *__wrap_calloc(size_t count, size_t size);
void *__wrap_realloc(void *pointer, size_t size);

void *__wrap_calloc(size_t count, size_t size)
{
	if (fail_calloc_after == 0) {
		fail_calloc_after = -1;
		return NULL;
	}
	if (fail_calloc_after > 0)
		fail_calloc_after--;
	return __real_calloc(count, size);
}

void *__wrap_realloc(void *pointer, size_t size)
{
	if (fail_realloc_after == 0) {
		fail_realloc_after = -1;
		return NULL;
	}
	if (fail_realloc_after > 0)
		fail_realloc_after--;
	return __real_realloc(pointer, size);
}
#endif

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

struct sg_human_trace_v3_scope_acceptance_s
{
	uint32_t token;
};

struct sg_human_trace_v3_spool_ref_s
{
	uint32_t token;
};

typedef struct sg_human_trace_v3_spool_ref_s
	sg_human_trace_v3_spool_ref_t;

int SG_HumanTraceAcceptedV3ScopeView(
	const sg_human_trace_v3_scope_acceptance_t *scope,
	const sg_human_trace_v3_spool_ref_t **root_out,
	uint32_t *client_id_out, uint64_t *spawn_generation_out);

static struct sg_human_trace_v3_scope_acceptance_s test_human_scope = { 1U };
static struct sg_human_trace_v3_spool_ref_s test_human_root = { 1U };
static struct sg_human_trace_v3_scope_acceptance_s forged_human_scope = {
	0xdeadU
};

int SG_HumanTraceAcceptedV3ScopeView(
	const sg_human_trace_v3_scope_acceptance_t *scope,
	const sg_human_trace_v3_spool_ref_t **root_out,
	uint32_t *client_id_out, uint64_t *spawn_generation_out)
{
	if (root_out != NULL)
		*root_out = NULL;
	if (client_id_out != NULL)
		*client_id_out = 0U;
	if (spawn_generation_out != NULL)
		*spawn_generation_out = 0U;
	if (scope != &test_human_scope)
		return 0;
	if (root_out != NULL)
		*root_out = &test_human_root;
	if (client_id_out != NULL)
		*client_id_out = 1U;
	if (spawn_generation_out != NULL)
		*spawn_generation_out = UINT64_C(1);
	return 1;
}
static sg_rune_compact_learning_claim_t Claim(
	const compact_fixture_t *fixture, sg_rune_compact_learning_kind_t kind)
{
	sg_rune_compact_learning_claim_t claim;

	(void)fixture;
	memset(&claim, 0, sizeof(claim));
	claim.key.kind = kind;
	switch (kind) {
	case SG_RUNE_COMPACT_LEARNING_STABLE_CELL_CAPABILITY_COST:
		claim.key.value.cost.cell.value = 0U;
		claim.key.value.cost.capability.value = 0U;
		claim.key.value.cost.stance = SG_RUNE_STANCE_VALID_STANDING;
		claim.value = 12.5f;
		break;
	case SG_RUNE_COMPACT_LEARNING_LANDING_PREFERENCE:
		claim.key.value.landing.cell.value = 1U;
		claim.key.value.landing.stance = SG_RUNE_STANCE_VALID_STANDING;
		claim.value = 0.75f;
		break;
	case SG_RUNE_COMPACT_LEARNING_TACTICAL_PRIOR:
		claim.key.value.tactical.cell.value = 0U;
		claim.key.value.tactical.weapon_kernel = 0U;
		claim.value = 0.25f;
		break;
	case SG_RUNE_COMPACT_LEARNING_STRATEGY_OUTCOME:
		claim.key.value.strategy.landmark.value = 0U;
		claim.key.value.strategy.outcome =
			SG_RUNE_COMPACT_LEARNING_STRATEGY_SUCCEEDED;
		claim.value = 1.0f;
		break;
	case SG_RUNE_COMPACT_LEARNING_KIND_COUNT:
		break;
	}
	return claim;
}

static sg_rune_compact_learning_t *CreateLearning(
	const compact_fixture_t *fixture)
{
	sg_rune_compact_learning_t *learning = NULL;
	sg_rune_compact_error_t error;

	CHECK(SG_RuneCompactLearningCreate(&fixture->model,
		&fixture->model.identity, &learning, &error) ==
		SG_RUNE_COMPACT_LEARNING_OK);
	CHECK(learning != NULL);
	return learning;
}

static sg_rune_compact_learning_issuer_t *CreateIssuer(
	const compact_fixture_t *fixture, int bot)
{
	sg_rune_compact_learning_issuer_t *issuer = NULL;
	sg_rune_compact_error_t error;

	CHECK((bot ? SG_RuneCompactLearningIssuerAcquireBot(&fixture->model,
		&fixture->model.identity, &issuer, &error) :
		SG_RuneCompactLearningIssuerAcquireHuman(&fixture->model,
		&fixture->model.identity, &test_human_scope, &issuer, &error)) ==
		SG_RUNE_COMPACT_LEARNING_OK);
	CHECK(issuer != NULL);
	return issuer;
}

static sg_rune_compact_learning_observation_t *Issue(
	const sg_rune_compact_learning_issuer_t *issuer,
	const sg_rune_compact_learning_claim_t *claim)
{
	sg_rune_compact_learning_observation_t *observation = NULL;

	CHECK(SG_RuneCompactLearningIssuerIssue(issuer, claim, &observation) ==
		SG_RUNE_COMPACT_LEARNING_OK);
	CHECK(observation != NULL);
	return observation;
}

static sg_rune_compact_learning_status_t ApplyIssued(
	sg_rune_compact_learning_t *learning,
	sg_rune_compact_learning_observation_t *observation,
	sg_rune_compact_learning_prior_t *prior_out)
{
	sg_rune_compact_learning_status_t status = SG_RuneCompactLearningApply(
		learning, observation, prior_out);

	SG_RuneCompactLearningObservationDestroy(observation);
	return status;
}

static sg_rune_compact_learning_prior_t ReadPrior(
	const sg_rune_compact_learning_t *learning, uint32_t index)
{
	sg_rune_compact_learning_prior_t prior;

	memset(&prior, 0, sizeof(prior));
	CHECK(SG_RuneCompactLearningPriorRead(learning, index, &prior));
	return prior;
}

static int PriorEqual(const sg_rune_compact_learning_prior_t *left,
	const sg_rune_compact_learning_prior_t *right)
{
	return memcmp(&left->key, &right->key, sizeof(left->key)) == 0 &&
		left->value_total_q16 == right->value_total_q16 &&
		left->human_samples == right->human_samples &&
		left->bot_samples == right->bot_samples;
}

static void TestVerifiedUpdates(void)
{
	compact_fixture_t fixture;
	sg_rune_compact_learning_t *learning;
	sg_rune_compact_learning_issuer_t *human;
	sg_rune_compact_learning_issuer_t *bot;
	sg_rune_compact_learning_claim_t claim;
	sg_rune_compact_learning_prior_t prior;

	InitFixture(&fixture);
	learning = CreateLearning(&fixture);
	human = CreateIssuer(&fixture, 0);
	bot = CreateIssuer(&fixture, 1);
	claim = Claim(&fixture,
		SG_RUNE_COMPACT_LEARNING_STABLE_CELL_CAPABILITY_COST);
	CHECK(ApplyIssued(learning, Issue(human, &claim), &prior) ==
		SG_RUNE_COMPACT_LEARNING_OK);
	CHECK(prior.value_total_q16 == UINT64_C(819200));
	CHECK(prior.human_samples == 1U);
	CHECK(prior.bot_samples == 0U);

	claim = Claim(&fixture, SG_RUNE_COMPACT_LEARNING_LANDING_PREFERENCE);
	CHECK(ApplyIssued(learning, Issue(bot, &claim), &prior) ==
		SG_RUNE_COMPACT_LEARNING_OK);
	CHECK(prior.value_total_q16 == UINT64_C(49152));
	CHECK(prior.human_samples == 0U);
	CHECK(prior.bot_samples == 1U);

	claim = Claim(&fixture, SG_RUNE_COMPACT_LEARNING_TACTICAL_PRIOR);
	CHECK(ApplyIssued(learning, Issue(human, &claim), &prior) ==
		SG_RUNE_COMPACT_LEARNING_OK);
	CHECK(prior.value_total_q16 == UINT64_C(16384));

	claim = Claim(&fixture, SG_RUNE_COMPACT_LEARNING_STRATEGY_OUTCOME);
	CHECK(ApplyIssued(learning, Issue(human, &claim), &prior) ==
		SG_RUNE_COMPACT_LEARNING_OK);
	CHECK(prior.value_total_q16 == UINT64_C(65536));
	CHECK(SG_RuneCompactLearningPriorCount(learning) == 4U);
	CHECK(!SG_RuneCompactLearningPriorRead(learning, 4U, &prior));
	prior = ReadPrior(learning, 0U);
	CHECK(prior.key.kind ==
		SG_RUNE_COMPACT_LEARNING_STABLE_CELL_CAPABILITY_COST);
	prior = ReadPrior(learning, 3U);
	CHECK(prior.key.kind ==
		SG_RUNE_COMPACT_LEARNING_STRATEGY_OUTCOME);
	SG_RuneCompactLearningIssuerDestroy(bot);
	SG_RuneCompactLearningIssuerDestroy(human);
	SG_RuneCompactLearningDestroy(learning);
}

static void TestHumanIssuerRequiresAcceptedScope(void)
{
	compact_fixture_t fixture;
	sg_rune_compact_learning_issuer_t *unchanged_issuer =
		(sg_rune_compact_learning_issuer_t *)(void *)&fixture;
	sg_rune_compact_error_t error;

	InitFixture(&fixture);
	CHECK(SG_RuneCompactLearningIssuerAcquireHuman(&fixture.model,
		&fixture.model.identity, NULL, &unchanged_issuer, &error) ==
		SG_RUNE_COMPACT_LEARNING_UNAUTHENTICATED);
	CHECK(unchanged_issuer ==
		(sg_rune_compact_learning_issuer_t *)(void *)&fixture);
	CHECK(SG_RuneCompactLearningIssuerAcquireHuman(&fixture.model,
		&fixture.model.identity,
		(const sg_human_trace_v3_scope_acceptance_t *)(void *)&forged_human_scope,
		&unchanged_issuer, &error) ==
		SG_RUNE_COMPACT_LEARNING_UNAUTHENTICATED);
	CHECK(unchanged_issuer ==
		(sg_rune_compact_learning_issuer_t *)(void *)&fixture);
}

static void TestInventedGeometryRejected(void)
{
	compact_fixture_t fixture;
	sg_rune_compact_learning_t *learning;
	sg_rune_compact_learning_issuer_t *issuer;
	sg_rune_compact_learning_claim_t claim;
	sg_rune_compact_learning_observation_t *unchanged_observation =
		(sg_rune_compact_learning_observation_t *)(void *)&fixture;
	sg_rune_compact_learning_observation_t *observation = NULL;
	sg_rune_compact_learning_prior_t prior;
	sg_rune_compact_learning_prior_t prior_before;

	InitFixture(&fixture);
	learning = CreateLearning(&fixture);
	issuer = CreateIssuer(&fixture, 0);
	memset(&prior, 0xa5, sizeof(prior));
	prior_before = prior;
	claim = Claim(&fixture,
		SG_RUNE_COMPACT_LEARNING_STABLE_CELL_CAPABILITY_COST);
	claim.key.value.cost.cell.value = 1U;
	CHECK(SG_RuneCompactLearningIssuerIssue(issuer, &claim,
		&unchanged_observation) ==
		SG_RUNE_COMPACT_LEARNING_INVALID_REFERENCE);
	CHECK(unchanged_observation ==
		(sg_rune_compact_learning_observation_t *)(void *)&fixture);
	CHECK(SG_RuneCompactLearningPriorCount(learning) == 0U);
	CHECK(memcmp(&prior, &prior_before, sizeof(prior)) == 0);

	claim = Claim(&fixture, SG_RUNE_COMPACT_LEARNING_LANDING_PREFERENCE);
	claim.key.value.landing.cell.value = 2U;
	CHECK(SG_RuneCompactLearningIssuerIssue(issuer, &claim,
		&unchanged_observation) ==
		SG_RUNE_COMPACT_LEARNING_INVALID_REFERENCE);
	claim = Claim(&fixture, SG_RUNE_COMPACT_LEARNING_TACTICAL_PRIOR);
	claim.key.value.tactical.cell.value = 1U;
	/* A tactical prior is valid only when the target cell carries a verified
	 * compact response fragment. */
	CHECK(SG_RuneCompactLearningIssuerIssue(issuer, &claim,
		&observation) == SG_RUNE_COMPACT_LEARNING_OK);
	CHECK(observation != NULL);
	SG_RuneCompactLearningObservationDestroy(observation);
	observation = NULL;
	claim = Claim(&fixture, SG_RUNE_COMPACT_LEARNING_STRATEGY_OUTCOME);
	claim.key.value.strategy.landmark.value = 2U;
	CHECK(SG_RuneCompactLearningIssuerIssue(issuer, &claim,
		&unchanged_observation) ==
		SG_RUNE_COMPACT_LEARNING_INVALID_REFERENCE);
	CHECK(SG_RuneCompactLearningPriorCount(learning) == 0U);
	SG_RuneCompactLearningIssuerDestroy(issuer);
	SG_RuneCompactLearningDestroy(learning);
}

static void TestObservationIsConsumedOnce(void)
{
	compact_fixture_t fixture;
	sg_rune_compact_learning_t *learning;
	sg_rune_compact_learning_issuer_t *issuer;
	sg_rune_compact_learning_claim_t claim;
	sg_rune_compact_learning_observation_t *observation;
	sg_rune_compact_learning_prior_t prior;
	sg_rune_compact_learning_prior_t unchanged_prior;
	sg_rune_compact_learning_prior_t first_prior;
	sg_rune_compact_learning_prior_t stored_prior;

	InitFixture(&fixture);
	learning = CreateLearning(&fixture);
	issuer = CreateIssuer(&fixture, 0);
	claim = Claim(&fixture,
		SG_RUNE_COMPACT_LEARNING_STABLE_CELL_CAPABILITY_COST);
	observation = Issue(issuer, &claim);
	memset(&prior, 0x5a, sizeof(prior));
	CHECK(SG_RuneCompactLearningApply(learning, observation, &prior) ==
		SG_RUNE_COMPACT_LEARNING_OK);
	first_prior = prior;
	memset(&prior, 0xa6, sizeof(prior));
	unchanged_prior = prior;
	CHECK(SG_RuneCompactLearningApply(learning, observation, &prior) ==
		SG_RUNE_COMPACT_LEARNING_INVALID_OBSERVATION);
	CHECK(memcmp(&prior, &unchanged_prior, sizeof(prior)) == 0);
	CHECK(SG_RuneCompactLearningPriorCount(learning) == 1U);
	stored_prior = ReadPrior(learning, 0U);
	CHECK(PriorEqual(&stored_prior, &first_prior));
	SG_RuneCompactLearningObservationDestroy(observation);
	SG_RuneCompactLearningIssuerDestroy(issuer);
	SG_RuneCompactLearningDestroy(learning);
}

static void TestIdentityBoundIssuer(void)
{
	compact_fixture_t fixture;
	sg_rune_compact_learning_t *learning;
	sg_rune_compact_learning_t *unchanged = (sg_rune_compact_learning_t *)(void *)&fixture;
	sg_rune_compact_learning_issuer_t *issuer;
	sg_rune_compact_learning_issuer_t *unchanged_issuer =
		(sg_rune_compact_learning_issuer_t *)(void *)&fixture;
	sg_rune_compact_learning_observation_t *observation;
	sg_rune_compact_learning_observation_t *unchanged_observation =
		(sg_rune_compact_learning_observation_t *)(void *)&fixture;
	sg_rune_compact_learning_claim_t claim;
	sg_rune_compact_learning_prior_t prior;
	sg_rune_compact_learning_prior_t prior_before;
	sg_rune_compact_identity_t wrong_identity;
	sg_rune_compact_error_t error;

	InitFixture(&fixture);
	wrong_identity = fixture.model.identity;
	wrong_identity.hook_law_id ^= UINT64_C(1);
	CHECK(SG_RuneCompactLearningCreate(&fixture.model, &wrong_identity,
		&unchanged, &error) == SG_RUNE_COMPACT_LEARNING_IDENTITY_MISMATCH);
	CHECK(unchanged == (sg_rune_compact_learning_t *)(void *)&fixture);
	CHECK(SG_RuneCompactLearningIssuerAcquireHuman(&fixture.model,
		&wrong_identity, &test_human_scope, &unchanged_issuer, &error) ==
		SG_RUNE_COMPACT_LEARNING_IDENTITY_MISMATCH);
	CHECK(unchanged_issuer ==
		(sg_rune_compact_learning_issuer_t *)(void *)&fixture);

	learning = CreateLearning(&fixture);
	issuer = CreateIssuer(&fixture, 0);
	claim = Claim(&fixture,
		SG_RUNE_COMPACT_LEARNING_STABLE_CELL_CAPABILITY_COST);
	observation = Issue(issuer, &claim);
	memset(&prior, 0xa5, sizeof(prior));
	prior_before = prior;
	fixture.model.identity.hook_law_id ^= UINT64_C(1);
	CHECK(SG_RuneCompactLearningIssuerIssue(issuer, &claim,
		&unchanged_observation) == SG_RUNE_COMPACT_LEARNING_IDENTITY_MISMATCH);
	CHECK(unchanged_observation ==
		(sg_rune_compact_learning_observation_t *)(void *)&fixture);
	CHECK(SG_RuneCompactLearningApply(learning, observation, &prior) ==
		SG_RUNE_COMPACT_LEARNING_IDENTITY_MISMATCH);
	CHECK(memcmp(&prior, &prior_before, sizeof(prior)) == 0);
	SG_RuneCompactLearningObservationDestroy(observation);
	SG_RuneCompactLearningIssuerDestroy(issuer);
	SG_RuneCompactLearningDestroy(learning);
}

static void TestMalformedValuesRejected(void)
{
	compact_fixture_t fixture;
	sg_rune_compact_learning_t *learning;
	sg_rune_compact_learning_issuer_t *human;
	sg_rune_compact_learning_issuer_t *bot;
	sg_rune_compact_learning_claim_t claim;
	sg_rune_compact_learning_observation_t *observation;
	sg_rune_compact_learning_prior_t prior;

	InitFixture(&fixture);
	learning = CreateLearning(&fixture);
	human = CreateIssuer(&fixture, 0);
	bot = CreateIssuer(&fixture, 1);
	claim = Claim(&fixture, SG_RUNE_COMPACT_LEARNING_TACTICAL_PRIOR);
	claim.value = NAN;
	CHECK(SG_RuneCompactLearningIssuerIssue(human, &claim,
		&observation) ==
		SG_RUNE_COMPACT_LEARNING_INVALID_VALUE);
	claim.value = INFINITY;
	CHECK(SG_RuneCompactLearningIssuerIssue(human, &claim,
		&observation) ==
		SG_RUNE_COMPACT_LEARNING_INVALID_VALUE);
	claim.value = -0.25f;
	CHECK(SG_RuneCompactLearningIssuerIssue(human, &claim,
		&observation) ==
		SG_RUNE_COMPACT_LEARNING_INVALID_VALUE);
	claim.value = FLT_MAX;
	CHECK(SG_RuneCompactLearningIssuerIssue(human, &claim,
		&observation) ==
		SG_RUNE_COMPACT_LEARNING_INVALID_VALUE);
	claim.key.kind = SG_RUNE_COMPACT_LEARNING_KIND_COUNT;
	CHECK(SG_RuneCompactLearningIssuerIssue(human, &claim,
		&observation) ==
		SG_RUNE_COMPACT_LEARNING_INVALID_OBSERVATION);
	claim.key.kind = (sg_rune_compact_learning_kind_t)-1;
	CHECK(SG_RuneCompactLearningIssuerIssue(human, &claim,
		&observation) ==
		SG_RUNE_COMPACT_LEARNING_INVALID_OBSERVATION);
	claim = Claim(&fixture, SG_RUNE_COMPACT_LEARNING_TACTICAL_PRIOR);
	claim.value = 200000000000000.0f;
	CHECK(ApplyIssued(learning, Issue(human, &claim), &prior) ==
		SG_RUNE_COMPACT_LEARNING_OK);
	CHECK(ApplyIssued(learning, Issue(bot, &claim), &prior) ==
		SG_RUNE_COMPACT_LEARNING_OK);
	CHECK(prior.value_total_q16 == UINT64_MAX);
	CHECK(prior.human_samples == 1U);
	CHECK(prior.bot_samples == 1U);
	claim = Claim(&fixture,
		SG_RUNE_COMPACT_LEARNING_STABLE_CELL_CAPABILITY_COST);
	claim.value = 0x1p48f;
	observation = (sg_rune_compact_learning_observation_t *)(void *)&fixture;
	CHECK(SG_RuneCompactLearningIssuerIssue(human, &claim, &observation) ==
		SG_RUNE_COMPACT_LEARNING_INVALID_VALUE);
	CHECK(observation ==
		(sg_rune_compact_learning_observation_t *)(void *)&fixture);
	claim.value = 0x1.fffffep47f;
	CHECK(ApplyIssued(learning, Issue(human, &claim), &prior) ==
		SG_RUNE_COMPACT_LEARNING_OK);
	CHECK(prior.value_total_q16 != UINT64_MAX);
	SG_RuneCompactLearningIssuerDestroy(bot);
	SG_RuneCompactLearningIssuerDestroy(human);
	SG_RuneCompactLearningDestroy(learning);
}

static void TestOrderIndependentMerge(void)
{
	compact_fixture_t fixture;
	sg_rune_compact_learning_t *left;
	sg_rune_compact_learning_t *right;
	sg_rune_compact_learning_t *forward;
	sg_rune_compact_learning_t *reverse;
	sg_rune_compact_learning_issuer_t *human;
	sg_rune_compact_learning_issuer_t *bot;
	sg_rune_compact_learning_claim_t claims[4];
	uint32_t index;

	InitFixture(&fixture);
	claims[0] = Claim(&fixture,
		SG_RUNE_COMPACT_LEARNING_STABLE_CELL_CAPABILITY_COST);
	claims[0].value = 3.0f;
	claims[1] = claims[0];
	claims[1].value = 5.0f;
	claims[2] = Claim(&fixture, SG_RUNE_COMPACT_LEARNING_TACTICAL_PRIOR);
	claims[2].value = 0.5f;
	claims[3] = Claim(&fixture,
		SG_RUNE_COMPACT_LEARNING_STRATEGY_OUTCOME);
	claims[3].value = 0.25f;
	human = CreateIssuer(&fixture, 0);
	bot = CreateIssuer(&fixture, 1);
	left = CreateLearning(&fixture);
	right = CreateLearning(&fixture);
	forward = CreateLearning(&fixture);
	reverse = CreateLearning(&fixture);
	CHECK(ApplyIssued(left, Issue(human, &claims[0]),
		&(sg_rune_compact_learning_prior_t){ 0 }) == SG_RUNE_COMPACT_LEARNING_OK);
	CHECK(ApplyIssued(left, Issue(human, &claims[2]),
		&(sg_rune_compact_learning_prior_t){ 0 }) == SG_RUNE_COMPACT_LEARNING_OK);
	CHECK(ApplyIssued(right, Issue(bot, &claims[1]),
		&(sg_rune_compact_learning_prior_t){ 0 }) == SG_RUNE_COMPACT_LEARNING_OK);
	CHECK(ApplyIssued(right, Issue(bot, &claims[3]),
		&(sg_rune_compact_learning_prior_t){ 0 }) == SG_RUNE_COMPACT_LEARNING_OK);
	CHECK(SG_RuneCompactLearningMerge(forward, left) ==
		SG_RUNE_COMPACT_LEARNING_OK);
	CHECK(SG_RuneCompactLearningMerge(forward, right) ==
		SG_RUNE_COMPACT_LEARNING_OK);
	CHECK(SG_RuneCompactLearningMerge(reverse, right) ==
		SG_RUNE_COMPACT_LEARNING_OK);
	CHECK(SG_RuneCompactLearningMerge(reverse, left) ==
		SG_RUNE_COMPACT_LEARNING_OK);
	CHECK(SG_RuneCompactLearningPriorCount(forward) == 3U);
	CHECK(SG_RuneCompactLearningPriorCount(reverse) == 3U);
	for (index = 0U; index < 3U; index++) {
		const sg_rune_compact_learning_prior_t forward_prior =
			ReadPrior(forward, index);
		const sg_rune_compact_learning_prior_t reverse_prior =
			ReadPrior(reverse, index);

		CHECK(PriorEqual(&forward_prior, &reverse_prior));
	}
	{
		const sg_rune_compact_learning_prior_t prior = ReadPrior(forward, 0U);

		CHECK(prior.value_total_q16 ==
		UINT64_C(524288));
		CHECK(prior.human_samples == 1U);
		CHECK(prior.bot_samples == 1U);
	}
	SG_RuneCompactLearningDestroy(reverse);
	SG_RuneCompactLearningDestroy(forward);
	SG_RuneCompactLearningDestroy(right);
	SG_RuneCompactLearningDestroy(left);
	SG_RuneCompactLearningIssuerDestroy(bot);
	SG_RuneCompactLearningIssuerDestroy(human);
}

static void TestCanonicalKeysAndCopiedReads(void)
{
	compact_fixture_t fixture;
	sg_rune_compact_learning_t *clean_learning;
	sg_rune_compact_learning_t *dirty_learning;
	sg_rune_compact_learning_issuer_t *issuer;
	sg_rune_compact_learning_claim_t clean;
	sg_rune_compact_learning_claim_t dirty;
	sg_rune_compact_learning_prior_t clean_prior;
	sg_rune_compact_learning_prior_t dirty_prior;
	sg_rune_compact_learning_prior_t unchanged;
	sg_rune_compact_learning_prior_t unchanged_before;

	InitFixture(&fixture);
	issuer = CreateIssuer(&fixture, 0);
	clean = Claim(&fixture, SG_RUNE_COMPACT_LEARNING_TACTICAL_PRIOR);
	memset(&dirty, 0xa5, sizeof(dirty));
	dirty.key.kind = SG_RUNE_COMPACT_LEARNING_TACTICAL_PRIOR;
	dirty.key.value.tactical.cell.value = 0U;
	dirty.key.value.tactical.weapon_kernel = 0U;
	dirty.value = 0.25f;
	clean_learning = CreateLearning(&fixture);
	dirty_learning = CreateLearning(&fixture);
	CHECK(ApplyIssued(clean_learning, Issue(issuer, &clean), &clean_prior) ==
		SG_RUNE_COMPACT_LEARNING_OK);
	CHECK(ApplyIssued(dirty_learning, Issue(issuer, &dirty), &dirty_prior) ==
		SG_RUNE_COMPACT_LEARNING_OK);
	clean_prior = ReadPrior(clean_learning, 0U);
	dirty_prior = ReadPrior(dirty_learning, 0U);
	CHECK(memcmp(&clean_prior.key, &dirty_prior.key,
		sizeof(clean_prior.key)) == 0);
	CHECK(memcmp(&clean_prior, &dirty_prior, sizeof(clean_prior)) == 0);
	memset(&unchanged, 0x5a, sizeof(unchanged));
	unchanged_before = unchanged;
	CHECK(!SG_RuneCompactLearningPriorRead(clean_learning, 1U, &unchanged));
	CHECK(memcmp(&unchanged, &unchanged_before, sizeof(unchanged)) == 0);
	SG_RuneCompactLearningDestroy(dirty_learning);
	SG_RuneCompactLearningDestroy(clean_learning);
	CHECK(clean_prior.key.kind == SG_RUNE_COMPACT_LEARNING_TACTICAL_PRIOR);
	CHECK(clean_prior.value_total_q16 == UINT64_C(16384));
	SG_RuneCompactLearningIssuerDestroy(issuer);
}

#if defined(SG_RUNE_COMPACT_LEARNING_TEST_WRAP_ALLOC)
static void TestAllocationFailureTransaction(void)
{
	compact_fixture_t fixture;
	sg_rune_compact_learning_t *learning;
	sg_rune_compact_learning_t *unchanged = (sg_rune_compact_learning_t *)(void *)&fixture;
	sg_rune_compact_learning_issuer_t *issuer;
	sg_rune_compact_learning_observation_t *observation;
	sg_rune_compact_learning_claim_t claim;
	sg_rune_compact_learning_prior_t prior;
	sg_rune_compact_learning_prior_t prior_before;
	sg_rune_compact_error_t error;

	InitFixture(&fixture);
	/* Model validation allocates analytic-use and response-projection
	 * worklists before the learning state itself is created. */
	fail_calloc_after = 5;
	CHECK(SG_RuneCompactLearningCreate(&fixture.model,
		&fixture.model.identity, &unchanged, &error) ==
		SG_RUNE_COMPACT_LEARNING_ALLOCATION_FAILED);
	CHECK(unchanged == (sg_rune_compact_learning_t *)(void *)&fixture);
	fail_calloc_after = -1;
	{
		sg_rune_compact_learning_issuer_t *unchanged_issuer =
			(sg_rune_compact_learning_issuer_t *)(void *)&fixture;

		fail_calloc_after = 5;
		CHECK(SG_RuneCompactLearningIssuerAcquireBot(&fixture.model,
			&fixture.model.identity, &unchanged_issuer, &error) ==
			SG_RUNE_COMPACT_LEARNING_ALLOCATION_FAILED);
		CHECK(unchanged_issuer ==
			(sg_rune_compact_learning_issuer_t *)(void *)&fixture);
		fail_calloc_after = -1;
	}
	learning = CreateLearning(&fixture);
	issuer = CreateIssuer(&fixture, 0);
	claim = Claim(&fixture,
		SG_RUNE_COMPACT_LEARNING_STABLE_CELL_CAPABILITY_COST);
	observation = (sg_rune_compact_learning_observation_t *)(void *)&fixture;
	fail_calloc_after = 0;
	CHECK(SG_RuneCompactLearningIssuerIssue(issuer, &claim, &observation) ==
		SG_RUNE_COMPACT_LEARNING_ALLOCATION_FAILED);
	CHECK(observation ==
		(sg_rune_compact_learning_observation_t *)(void *)&fixture);
	fail_calloc_after = -1;
	observation = Issue(issuer, &claim);
	memset(&prior, 0x5a, sizeof(prior));
	prior_before = prior;
	fail_realloc_after = 0;
	CHECK(SG_RuneCompactLearningApply(learning, observation, &prior) ==
		SG_RUNE_COMPACT_LEARNING_ALLOCATION_FAILED);
	CHECK(SG_RuneCompactLearningPriorCount(learning) == 0U);
	CHECK(memcmp(&prior, &prior_before, sizeof(prior)) == 0);
	fail_realloc_after = -1;
	CHECK(SG_RuneCompactLearningApply(learning, observation, &prior) ==
		SG_RUNE_COMPACT_LEARNING_OK);
	CHECK(SG_RuneCompactLearningPriorCount(learning) == 1U);
	SG_RuneCompactLearningObservationDestroy(observation);
	SG_RuneCompactLearningIssuerDestroy(issuer);
	SG_RuneCompactLearningDestroy(learning);
}
#endif

int main(void)
{
	TestVerifiedUpdates();
	TestHumanIssuerRequiresAcceptedScope();
	TestInventedGeometryRejected();
	TestObservationIsConsumedOnce();
	TestIdentityBoundIssuer();
	TestMalformedValuesRejected();
	TestOrderIndependentMerge();
	TestCanonicalKeysAndCopiedReads();
#if defined(SG_RUNE_COMPACT_LEARNING_TEST_WRAP_ALLOC)
	TestAllocationFailureTransaction();
#endif
	if (failures != 0) {
		fprintf(stderr, "%d compact RUNE learning checks failed\n", failures);
		return 1;
	}
	puts("compact RUNE learning checks passed");
	return 0;
}
