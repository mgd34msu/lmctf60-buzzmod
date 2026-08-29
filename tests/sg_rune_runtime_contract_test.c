#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "slipgate/sg_belief_contract.h"
#include "slipgate/sg_destination.h"
#include "slipgate/sg_learning_contract.h"
#include "slipgate/sg_tactic_contract.h"
#include "slipgate/sg_weapon_contract.h"

_Static_assert(SG_RUNTIME_CONTRACT_VERSION == UINT16_C(4),
	"runtime contract ABI is version 4");

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static sg_learning_parameters_t LearningParameters(uint64_t generation)
{
	return (sg_learning_parameters_t){
		.rune_identity = 99U,
		.topology_revision = 7U,
		.bsp_identity = 700U,
		.physics_identity = 800U,
		.generation = generation
	};
}

static sg_learning_update_t LearningUpdate(void)
{
	return (sg_learning_update_t){
		.evidence = {
			.evidence_id = 100U,
			.rune_identity = 99U,
			.topology_revision = 7U,
			.bsp_identity = 700U,
			.physics_identity = 800U,
			.trace_identity = 900U,
			.captured_at_ms = 1000U,
			.authenticated_at_ms = 1001U,
			.authenticated = 1U,
			.exact_bound = 1U,
			.host_verified = 1U,
			.post_match = 1U
		},
		.kind = SG_LEARNING_UPDATE_COST,
		.value.cost = { 1U, 20 }
	};
}

static void TestLearningTransactionIdentity(void)
{
	sg_learning_parameters_t parameters = LearningParameters(5U);
	sg_learning_parameters_t foreign;
	sg_learning_transaction_t transaction = {
		.transaction_id = 1U,
		.expected_generation = 4U,
		.applied_generation = 5U,
		.evidence_id = 100U,
		.state = SG_LEARNING_TRANSACTION_APPLIED,
		.before = LearningParameters(4U),
		.authorized_update = LearningUpdate()
	};

	CHECK(SG_LearningTransactionMayCommit(&parameters, &transaction));
	CHECK(SG_LearningTransactionMayRollback(&parameters, &transaction));
	foreign = parameters;
	foreign.rune_identity++;
	CHECK(!SG_LearningTransactionMayCommit(&foreign, &transaction));
	foreign = parameters;
	foreign.topology_revision++;
	CHECK(!SG_LearningTransactionMayRollback(&foreign, &transaction));
	foreign = parameters;
	foreign.bsp_identity++;
	CHECK(!SG_LearningTransactionMayCommit(&foreign, &transaction));
	foreign = parameters;
	foreign.physics_identity++;
	CHECK(!SG_LearningTransactionMayRollback(&foreign, &transaction));
}

static sg_tactic_request_t TacticRequest(void)
{
	return (sg_tactic_request_t){
		.live = {
			.rune_identity = 99U,
			.pose_revision = 4U,
			.now_ms = 500U,
			.phase_coordinate = { 1U, 0U },
			.phase = SG_TACTIC_PHASE_GROUND,
			.supported = 1U
		},
		.gradient = {
			.rune_identity = 99U,
			.field_generation = 3U,
			.pose_revision = 4U,
			.sampled_at_ms = 500U,
			.phase_coordinate = { 1U, 0U },
			.next_phase_coordinate = { 2U, 1U },
			.phase = SG_TACTIC_PHASE_GROUND,
			.cost_ms = 50U,
			.field_capability_families = {
				SG_FIELD_CAPABILITY_FAMILY_BIT(
					SG_RUNE_CAPABILITY_CONTINUOUS_SUPPORT).bits
			},
			.field_transition_kind = SG_RUNE_PHASE_TRANSITION_NONE,
			.capability_mask = SG_TACTIC_CAPABILITY_BIT(
				SG_TACTIC_CAPABILITY_WALK),
			.direction = { 1.0f, 0.0f, 0.0f },
			.velocity_direction = { 0.0f, 1.0f, 0.0f },
			.finite = 1U
		},
		.legal_capability_mask = SG_TACTIC_CAPABILITY_BIT(
			SG_TACTIC_CAPABILITY_WALK)
	};
}

static void TestTacticBindingsAndResults(void)
{
	sg_tactic_request_t request = TacticRequest();
	sg_tactic_modifier_t modifiers[17];
	sg_tactic_result_t result = {
		.status = SG_TACTIC_RESULT_PROGRESS,
		.failure = SG_TACTIC_FAILURE_NONE,
		.capability = SG_TACTIC_CAPABILITY_WALK,
		.target_phase = { 2U, 1U },
		.expected_cost_ms = 60U,
		.progress = 0.5f
	};
	size_t modifier;

	CHECK(SG_TacticRequestValid(&request));
	memset(modifiers, 0, sizeof(modifiers));
	for (modifier = 0U; modifier < 17U; modifier++)
	{
		modifiers[modifier].kind = (sg_tactic_modifier_kind_t)
			(modifier % (size_t)SG_TACTIC_MODIFIER_KIND_COUNT);
		modifiers[modifier].source_id = (uint32_t)modifier + 1U;
		modifiers[modifier].expires_at_ms = 501U;
		modifiers[modifier].active = 1U;
	}
	request.modifiers = modifiers;
	request.modifier_count = 17U;
	CHECK(SG_TacticRequestValid(&request));
	modifiers[16] = modifiers[0];
	CHECK(!SG_TacticRequestValid(&request));
	request.gradient.pose_revision++;
	CHECK(!SG_TacticRequestValid(&request));
	request = TacticRequest();
	request.gradient.sampled_at_ms++;
	CHECK(!SG_TacticRequestValid(&request));
	request = TacticRequest();
	request.gradient.phase = SG_TACTIC_PHASE_AIR;
	CHECK(!SG_TacticRequestValid(&request));
	request = TacticRequest();
	request.gradient.phase_coordinate.phase_id++;
	CHECK(!SG_TacticRequestValid(&request));
	request = TacticRequest();
	request.gradient.field_capability_families.bits =
		SG_FIELD_CAPABILITY_FAMILY_MASK + 1U;
	CHECK(!SG_TacticRequestValid(&request));
	CHECK(SG_TacticResultValid(&result));
	result.failure = SG_TACTIC_FAILURE_LIVE_STATE;
	CHECK(!SG_TacticResultValid(&result));
	result.status = SG_TACTIC_RESULT_RETRY;
	result.expected_cost_ms = SG_DESTINATION_COST_INFINITE;
	result.progress = 0.0f;
	CHECK(SG_TacticResultValid(&result));
	result.failure = SG_TACTIC_FAILURE_NONE;
	CHECK(!SG_TacticResultValid(&result));
	result.failure = SG_TACTIC_FAILURE_LIVE_STATE;
	result.expected_cost_ms = 10U;
	CHECK(!SG_TacticResultValid(&result));
}

static sg_belief_life_identity_t Life(uint32_t client_id,
	uint64_t spawn_generation)
{
	return (sg_belief_life_identity_t){
		.client_id = client_id,
		.spawn_generation = spawn_generation
	};
}

static sg_weapon_effect_query_t WeaponQuery(sg_belief_state_t *target,
	const sg_weapon_profile_t *profile)
{
	static const sg_weapon_affordance_t affordance = {
		.rune_identity = 99U,
		.visibility_revision = 2U,
		.source_cell_id = 1U,
		.target_cell_id = 3U,
		.allowed_effects = SG_WEAPON_EFFECT_HITSCAN,
		.visibility_probability = 1.0f,
		.exact_live_trace_required = 1U
	};

	return (sg_weapon_effect_query_t){
		.profile = profile,
		.affordance = &affordance,
		.target_belief = target,
		.ammo_available = 1U,
		.shooter_client = 1U,
		.target_client = 3U,
		.audience_team = 1U,
		.shooter_team = 1U,
		.target_team = 2U,
		.target_life = { .client_id = 3U, .spawn_generation = 30U },
		.shooter_cell_id = 1U,
		.target_cell_id = 3U,
		.rune_identity = 99U,
		.now_ms = 500U,
		.prediction_time_ms = 700U,
		.teammate_snapshot_revision = 1U,
		.build_identity = profile->build_identity,
		.physics_abi_id = profile->physics_abi_id,
		.teammate_evidence_complete = 1U,
		.shooter_health = 100.0f
	};
}

static void TestWeaponObservationAndClientBindings(void)
{
	sg_belief_particle_t teammate_particles[17];
	sg_belief_state_t teammate_beliefs[17];
	sg_belief_particle_t particle = {
		.phase = { 2U, 1U },
		.movement_state = SG_BELIEF_MOTION_GROUND,
		.source_mask = UINT16_C(1),
		.future_time_ms = 500U,
		.latest_evidence_id = 1U,
		.latest_evidence_at_ms = 500U,
		.position = { 100.0f, 0.0f, 0.0f },
		.weight = 1.0f
	};
	sg_belief_state_t target = {
		.audience_team = 1U,
		.target_team = 2U,
		.target_life = { .client_id = 3U, .spawn_generation = 30U },
		.particle_count = 1U,
		.particle_capacity = 1U,
		.generation = 1U,
		.revision = 1U,
		.rune_identity = 99U,
		.topology_revision = 7U,
		.updated_at_ms = 500U,
		.confidence = 1.0f,
		.total_weight = 1.0f,
		.policy = { 1000U, 0.5f, 0.01f },
		.particles = &particle
	};
	const sg_weapon_law_input_t law = {
		.build_identity = 11U,
		.physics_abi_id = 22U,
		.weapon_balance_compiled = SG_WEAPON_BALANCE_COMPILED,
		.deathmatch_active = 1U
	};
	sg_weapon_profile_t profile;
	sg_weapon_profile_t plasma_profile;
	const sg_weapon_profile_t *base_profile = NULL;
	sg_weapon_affordance_t plasma_affordance;
	sg_weapon_affordance_t invalid_affordance;
	sg_weapon_effect_query_t query;
	sg_weapon_prefire_request_t request = {
		.shot_id = 7U,
		.shot_revision = 8U,
		.rune_identity = 99U,
		.pose_revision = 4U,
		.fired_at_ms = 500U,
		.prediction_time_ms = 700U,
		.source_cell_id = 1U,
		.target_cell_id = 3U,
		.shooter_client = 1U,
		.target_client = 3U,
		.profile_id = 1U,
		.shooter_team = 1U,
		.target_team = 2U,
		.audience_team = 1U,
		.exact_required = 1U,
		.muzzle_origin = { 10.0f, 20.0f, 30.0f },
		.aim_direction = { 1.0f, 0.0f, 0.0f },
		.intended_impact = { 100.0f, 20.0f, 30.0f }
	};
	sg_weapon_prefire_validation_t validation = {
		.shot_id = 7U,
		.shot_revision = 8U,
		.rune_identity = 99U,
		.pose_revision = 4U,
		.fired_at_ms = 500U,
		.prediction_time_ms = 700U,
		.source_cell_id = 1U,
		.target_cell_id = 3U,
		.shooter_client = 1U,
		.target_client = 3U,
		.profile_id = 1U,
		.shooter_team = 1U,
		.target_team = 2U,
		.audience_team = 1U,
		.trace_status = SG_WEAPON_TRACE_ACCEPTED,
		.muzzle_clear = 1U,
		.host_agrees = 1U,
		.authenticated = 1U,
		.authorization_id = 1U,
		.muzzle_origin = { 10.0f, 20.0f, 30.0f },
		.aim_direction = { 1.0f, 0.0f, 0.0f },
		.intended_impact = { 100.0f, 20.0f, 30.0f }
	};

	CHECK(SG_WeaponProfileResolve(SG_WEAPON_PROFILE_MACHINEGUN, &law,
		&profile));
	query = WeaponQuery(&target, &profile);
	CHECK(SG_WeaponEffectQueryValid(&query));
	query.target_life.spawn_generation++;
	CHECK(!SG_WeaponEffectQueryValid(&query));
	query.target_life.spawn_generation--;
	invalid_affordance = *query.affordance;
	invalid_affordance.allowed_effects |= UINT32_C(1) << 6;
	CHECK(!SG_WeaponAffordanceValid(&invalid_affordance));
	query.build_identity++;
	CHECK(!SG_WeaponEffectQueryValid(&query));
	query.build_identity = profile.build_identity;
	query.physics_abi_id++;
	CHECK(!SG_WeaponEffectQueryValid(&query));
	query.physics_abi_id = profile.physics_abi_id;
	CHECK(SG_WeaponProfileLookup(SG_WEAPON_PROFILE_MACHINEGUN,
		&base_profile));
	query.profile = base_profile;
	CHECK(!SG_WeaponEffectQueryValid(&query));
	query.profile = &profile;
	for (uint32_t index = 0U; index < 17U; index++)
	{
		teammate_particles[index] = particle;
		teammate_beliefs[index] = target;
		teammate_beliefs[index].audience_team = query.audience_team;
		teammate_beliefs[index].target_team = query.shooter_team;
		teammate_beliefs[index].target_life = Life(4U + index,
			40U + index);
		teammate_beliefs[index].particles = &teammate_particles[index];
	}
	query.teammate_beliefs = teammate_beliefs;
	query.teammate_belief_count = 17U;
	CHECK(SG_WeaponEffectQueryValid(&query));
	teammate_beliefs[16].target_life =
		teammate_beliefs[15].target_life;
	CHECK(!SG_WeaponEffectQueryValid(&query));
	teammate_beliefs[16].target_life = Life(20U, 56U);
	query.teammate_belief_count = SG_BELIEF_MAX_CLIENTS + 1U;
	CHECK(!SG_WeaponEffectQueryValid(&query));
	query.teammate_beliefs = NULL;
	query.teammate_belief_count = 0U;
	CHECK(SG_WeaponProfileResolve(SG_WEAPON_PROFILE_PLASMA_REFLECT, &law,
		&plasma_profile));
	plasma_affordance = *query.affordance;
	plasma_affordance.allowed_effects = plasma_profile.effects;
	query.profile = &plasma_profile;
	query.affordance = &plasma_affordance;
	query.ammo_available = 1U;
	CHECK(SG_WeaponEffectQueryValid(&query));
	query.ammo_available = 0U;
	CHECK(!SG_WeaponEffectQueryValid(&query));
	query.profile = &profile;
	query.affordance = WeaponQuery(&target, &profile).affordance;
	query.ammo_available = 1U;
	target.updated_at_ms = 501U;
	particle.future_time_ms = 501U;
	CHECK(SG_BeliefStateValid(&target));
	CHECK(!SG_WeaponEffectQueryValid(&query));
	target.updated_at_ms = 500U;
	particle.future_time_ms = 500U;
	target.target_life.client_id = query.shooter_client;
	query.target_client = query.shooter_client;
	CHECK(!SG_WeaponEffectQueryValid(&query));
	CHECK(SG_WeaponPrefireShotMatches(&request, &validation));
	validation.pose_revision++;
	CHECK(!SG_WeaponPrefireShotMatches(&request, &validation));
	request.profile_id = UINT16_MAX;
	CHECK(!SG_WeaponPrefireRequestValid(&request));
	request.target_client = request.shooter_client;
	CHECK(!SG_WeaponPrefireRequestValid(&request));
}

int main(void)
{
	TestLearningTransactionIdentity();
	TestTacticBindingsAndResults();
	TestWeaponObservationAndClientBindings();
	if (failures != 0)
	{
		fprintf(stderr, "sg_rune_runtime_contract_test: %d failure(s)\n",
			failures);
		return 1;
	}
	puts("sg_rune_runtime_contract_test: ok");
	return 0;
}
