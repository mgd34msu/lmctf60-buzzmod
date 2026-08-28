#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "slipgate/sg_belief_contract.h"
#include "slipgate/sg_destination_field.h"
#include "slipgate/sg_learning_contract.h"
#include "slipgate/sg_tactic_contract.h"
#include "slipgate/sg_weapon_contract.h"

_Static_assert(SG_RUNTIME_CONTRACT_VERSION == UINT16_C(3),
	"runtime field ABI is version 3");

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static sg_destination_handle_t Destination(sg_destination_kind_t kind,
	uint64_t id, uint32_t phase_id, uint32_t cell_id)
{
	return (sg_destination_handle_t){
		.id = id,
		.generation = 1U,
		.kind = kind,
		.motion = SG_DESTINATION_STATIC,
		.valid = 1U,
		.pose = {
			.phase = { phase_id, cell_id },
			.position = { 10.0f, 20.0f, 30.0f }
		}
	};
}

static void TestPhaseSpaceFieldContract(void)
{
	sg_rune_cell_t cells[2] = { 0 };
	sg_rune_phase_basis_t model_phases[3] = { 0 };
	sg_phase_coordinate_t phases[3] = {
		{ 0U, 0U }, { 1U, 0U }, { 2U, 1U }
	};
	sg_rune_model_t model = {
		.version = SG_RUNE_MODEL_VERSION,
		.schema_tag = SG_RUNE_MODEL_SCHEMA_TAG,
		.flags = SG_RUNE_MODEL_IMMUTABLE | SG_RUNE_MODEL_EXACT_BOUND |
			SG_RUNE_MODEL_NO_RUNTIME_ACTORS,
		.completeness = {
			.state = SG_RUNE_COMPLETENESS_COMPLETE,
			.expected_cells = 2U,
			.covered_cells = 2U
		},
		.phases = model_phases,
		.phase_count = 3U,
		.cells = cells,
		.cell_count = 2U
	};
	sg_rune_runtime_snapshot_t snapshot = {
		.identity = 99U,
		.topology_revision = 7U,
		.cell_count = 2U,
		.phase_count = 3U,
		.model = &model,
		.phases = phases
	};
	sg_field_sample_t samples[3] = {
		{
			.phase = { 0U, 0U },
			.next_phase = { 1U, 0U },
			.cost_ms = 200U,
			.capability_families = { 1U },
			.phase_transition_kind = SG_RUNE_PHASE_TRANSITION_NONE,
			.direction = { 1.0f, 0.0f, 0.0f },
			.velocity_direction = { 0.0f, 1.0f, 0.0f },
			.finite = 1U
		},
		{
			.phase = { 1U, 0U },
			.next_phase = { 2U, 1U },
			.cost_ms = 100U,
			.capability_families = { 0U },
			.phase_transition_kind = SG_RUNE_PHASE_TRANSITION_STANCE,
			.direction = { 1.0f, 0.0f, 0.0f },
			.velocity_direction = { 0.0f, 0.5f, 0.0f },
			.finite = 1U
		},
		{
			.phase = { 2U, 1U },
			.next_phase = { 2U, 1U },
			.cost_ms = 0U,
			.capability_families = { 0U },
			.phase_transition_kind = SG_RUNE_PHASE_TRANSITION_NONE,
			.finite = 1U
		}
	};
	sg_destination_field_t field = {
		.rune_identity = 99U,
		.topology_revision = 7U,
		.generation = 1U,
		.computed_at_ms = 100U,
		.destination = Destination(SG_DESTINATION_WAYPOINT, 1U, 2U, 1U),
		.samples = samples,
		.sample_count = 3U,
		.complete = 1U
	};

	CHECK(SG_DestinationFieldValid(&snapshot, &field));
	samples[0].capability_families.bits =
		SG_FIELD_CAPABILITY_FAMILY_MASK + 1U;
	CHECK(!SG_DestinationFieldValid(&snapshot, &field));
	samples[0].capability_families = SG_FIELD_CAPABILITY_FAMILY_BIT(
		SG_RUNE_CAPABILITY_CONTINUOUS_SUPPORT);
	samples[0].phase_transition_kind = SG_RUNE_PHASE_TRANSITION_KIND_COUNT;
	CHECK(!SG_DestinationFieldValid(&snapshot, &field));
	samples[0].phase_transition_kind = SG_RUNE_PHASE_TRANSITION_NONE;
	model.flags &= ~(sg_rune_model_flags_t)SG_RUNE_MODEL_EXACT_BOUND;
	CHECK(!SG_RuneRuntimeSnapshotValid(&snapshot));
	model.flags |= SG_RUNE_MODEL_EXACT_BOUND;
	CHECK(samples[0].phase.cell_id == samples[1].phase.cell_id);
	field.sample_count = snapshot.cell_count;
	CHECK(!SG_DestinationFieldValid(&snapshot, &field));
	field.sample_count = snapshot.phase_count;
	samples[1].phase.phase_id = samples[0].phase.phase_id;
	CHECK(!SG_DestinationFieldValid(&snapshot, &field));
	samples[1].phase.phase_id = 1U;
	samples[0].phase = phases[1];
	samples[1].phase = phases[0];
	CHECK(!SG_DestinationFieldValid(&snapshot, &field));
	samples[0].phase = phases[0];
	samples[1].phase = phases[1];
	field.destination.pose.phase.phase_id = 3U;
	CHECK(!SG_DestinationFieldValid(&snapshot, &field));
	field.destination.pose.phase = (sg_phase_coordinate_t){ 1U, 1U };
	CHECK(!SG_DestinationFieldValid(&snapshot, &field));
}


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
	result.expected_cost_ms = SG_DESTINATION_FIELD_INF;
	result.progress = 0.0f;
	CHECK(SG_TacticResultValid(&result));
	result.failure = SG_TACTIC_FAILURE_NONE;
	CHECK(!SG_TacticResultValid(&result));
	result.failure = SG_TACTIC_FAILURE_LIVE_STATE;
	result.expected_cost_ms = 10U;
	CHECK(!SG_TacticResultValid(&result));
}

static void TestFieldTacticDomainCompatibility(void)
{
	sg_tactic_request_t request = TacticRequest();
	sg_field_query_result_t query = {
		.sample = {
			.phase = { 1U, 0U },
			.next_phase = { 2U, 1U },
			.cost_ms = 50U,
			.capability_families = {
				SG_FIELD_CAPABILITY_FAMILY_BIT(
					SG_RUNE_CAPABILITY_CONTINUOUS_SUPPORT).bits
			},
			.phase_transition_kind = SG_RUNE_PHASE_TRANSITION_NONE,
			.direction = { 1.0f, 0.0f, 0.0f },
			.velocity_direction = { 0.0f, 1.0f, 0.0f },
			.finite = 1U
		},
		.terminal_residual = {
			.status = SG_FIELD_TERMINAL_RESIDUAL_UNKNOWN,
			.upper_ms = SG_DESTINATION_FIELD_INF
		}
	};
	sg_rune_capability_family_t family;
	sg_rune_phase_transition_kind_t transition;

	CHECK(SG_TacticGradientMatchesFieldQuery(&request.gradient, &query));
	for (family = SG_RUNE_CAPABILITY_CONTINUOUS_SUPPORT;
		family < SG_RUNE_CAPABILITY_FAMILY_COUNT;
		family = (sg_rune_capability_family_t)(family + 1)) {
		query.sample.capability_families =
			SG_FIELD_CAPABILITY_FAMILY_BIT(family);
		query.sample.phase_transition_kind = SG_RUNE_PHASE_TRANSITION_NONE;
		request.gradient.field_capability_families =
			query.sample.capability_families;
		request.gradient.field_transition_kind =
			query.sample.phase_transition_kind;
		CHECK(SG_TacticGradientMatchesFieldQuery(&request.gradient, &query));
	}
	query.sample.capability_families.bits = 0U;
	request.gradient.field_capability_families.bits = 0U;
	for (transition = SG_RUNE_PHASE_TRANSITION_STANCE;
		transition < SG_RUNE_PHASE_TRANSITION_KIND_COUNT;
		transition = (sg_rune_phase_transition_kind_t)(transition + 1)) {
		query.sample.phase_transition_kind = transition;
		request.gradient.field_transition_kind = transition;
		CHECK(SG_TacticGradientMatchesFieldQuery(&request.gradient, &query));
	}
	request.gradient.field_transition_kind = SG_RUNE_PHASE_TRANSITION_NONE;
	CHECK(!SG_TacticGradientMatchesFieldQuery(&request.gradient, &query));
	query.sample.next_phase = query.sample.phase;
	query.sample.cost_ms = 0U;
	query.sample.capability_families.bits = 0U;
	query.sample.phase_transition_kind = SG_RUNE_PHASE_TRANSITION_NONE;
	memset(query.sample.direction, 0, sizeof(query.sample.direction));
	memset(query.sample.velocity_direction, 0,
		sizeof(query.sample.velocity_direction));
	query.terminal_residual.status = SG_FIELD_TERMINAL_RESIDUAL_EXACT;
	query.terminal_residual.upper_ms = 0U;
	request.gradient.next_phase_coordinate = query.sample.next_phase;
	request.gradient.cost_ms = query.sample.cost_ms;
	request.gradient.field_capability_families =
		query.sample.capability_families;
	request.gradient.field_transition_kind = query.sample.phase_transition_kind;
	memset(request.gradient.direction, 0, sizeof(request.gradient.direction));
	memset(request.gradient.velocity_direction, 0,
		sizeof(request.gradient.velocity_direction));
	CHECK(SG_TacticGradientMatchesFieldQuery(&request.gradient, &query));
	CHECK(!SG_TacticGradientValid(&request.gradient, &request.live));
}

static sg_weapon_effect_query_t WeaponQuery(sg_belief_state_t *target)
{
	static const sg_weapon_profile_t profile = {
		.id = 1U,
		.family = SG_WEAPON_FAMILY_HITSCAN,
		.effects = SG_WEAPON_EFFECT_HITSCAN,
		.max_range = 1000.0f,
		.direct_damage = 100.0f,
		.ammo_cost = 1U,
		.requires_live_trace = 1U
	};
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
		.profile = &profile,
		.affordance = &affordance,
		.target_belief = target,
		.ammo_available = 1U,
		.shooter_client = 1U,
		.target_client = 3U,
		.audience_team = 1U,
		.shooter_team = 1U,
		.target_team = 2U,
		.shooter_cell_id = 1U,
		.target_cell_id = 3U,
		.rune_identity = 99U,
		.now_ms = 500U,
		.prediction_time_ms = 700U,
		.teammate_snapshot_revision = 1U,
		.teammate_evidence_complete = 1U,
		.shooter_health = 100.0f
	};
}

static void TestWeaponObservationAndClientBindings(void)
{
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
		.target_client = 3U,
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
	sg_weapon_effect_query_t query = WeaponQuery(&target);
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

	CHECK(SG_WeaponEffectQueryValid(&query));
	target.updated_at_ms = 501U;
	particle.future_time_ms = 501U;
	CHECK(SG_BeliefStateValid(&target));
	CHECK(!SG_WeaponEffectQueryValid(&query));
	target.updated_at_ms = 500U;
	particle.future_time_ms = 500U;
	target.target_client = query.shooter_client;
	query.target_client = query.shooter_client;
	CHECK(!SG_WeaponEffectQueryValid(&query));
	CHECK(SG_WeaponPrefireShotMatches(&request, &validation));
	validation.pose_revision++;
	CHECK(!SG_WeaponPrefireShotMatches(&request, &validation));
	request.target_client = request.shooter_client;
	CHECK(!SG_WeaponPrefireRequestValid(&request));
}

int main(void)
{
	TestPhaseSpaceFieldContract();
	TestLearningTransactionIdentity();
	TestTacticBindingsAndResults();
	TestFieldTacticDomainCompatibility();
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
