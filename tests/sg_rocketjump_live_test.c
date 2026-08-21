#include <stdio.h>
#include <string.h>

#include "q_shared.h"
#include "slipgate/sg_rocketjump_live.h"

static int failures;
static const short expected_impact_q8[3] = { 11, 22, 33 };

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		        __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static sg_rocketjump_witness_t Witness(void)
{
	sg_rocketjump_witness_t witness;

	memset(&witness, 0, sizeof(witness));
	witness.link_index = 17;
	witness.source_q8[0] = 100;
	witness.source_q8[1] = -200;
	witness.source_q8[2] = 300;
	witness.destination_q8[0] = 200;
	witness.destination_q8[1] = -100;
	witness.destination_q8[2] = 900;
	witness.pitch = 16384;
	witness.yaw = -8192;
	witness.cost_ms = 1800;
	witness.health_price = 46;
	return witness;
}

static sg_rocketjump_observation_t Observation(
	const sg_rocketjump_witness_t *witness)
{
	sg_rocketjump_observation_t observation;

	memset(&observation, 0, sizeof(observation));
	memcpy(observation.origin_q8, witness->source_q8,
	       sizeof(observation.origin_q8));
	observation.alive = true;
	observation.grounded = true;
	observation.dry = true;
	observation.immutable_support = true;
	observation.normal_move = true;
	observation.standing = true;
	observation.jump_released = true;
	observation.right_handed = true;
	observation.standard_weapon_law = true;
	observation.launcher_owned = true;
	observation.health = 100;
	observation.rockets = 3;
	return observation;
}

static void Arm(sg_rocketjump_live_state_t *state,
	sg_rocketjump_witness_t *witness,
	sg_rocketjump_observation_t *observation)
{
	*witness = Witness();
	*observation = Observation(witness);
	SG_RocketJumpLiveReset(state);
	CHECK(SG_RocketJumpLiveBegin(state, witness, observation));
	CHECK(state->phase == SG_ROCKETJUMP_EQUIP);
}

static void TestWeaponReadyIsRequired(void)
{
	sg_rocketjump_live_state_t state;
	sg_rocketjump_witness_t witness;
	sg_rocketjump_observation_t observation;

	Arm(&state, &witness, &observation);
	observation.launcher_selected = true;
	observation.weapon_ready = false;
	CHECK(SG_RocketJumpLiveCommand(&state, &observation) ==
	      SG_ROCKETJUMP_COMMAND_EQUIP);
	CHECK(state.phase == SG_ROCKETJUMP_EQUIP);
	observation.weapon_ready = true;
	CHECK(SG_RocketJumpLiveCommand(&state, &observation) ==
	      SG_ROCKETJUMP_COMMAND_FIRE);
	CHECK(state.phase == SG_ROCKETJUMP_ARMED);
}

static void TestProjectileConfirmationOwnsFlight(void)
{
	sg_rocketjump_live_state_t state;
	sg_rocketjump_witness_t witness;
	sg_rocketjump_observation_t observation;

	Arm(&state, &witness, &observation);
	observation.launcher_selected = true;
	observation.weapon_ready = true;
	CHECK(SG_RocketJumpLiveCommand(&state, &observation) ==
	      SG_ROCKETJUMP_COMMAND_FIRE);
	CHECK(SG_RocketJumpLiveStep(&state, SG_ROCKETJUMP_STEP_MS));
	CHECK(state.phase == SG_ROCKETJUMP_ARMED);
	CHECK(!SG_RocketJumpLiveFired(&state,
	      (sg_rocketjump_projectile_key_t){ 0U, 0U }, expected_impact_q8));
	CHECK(state.phase == SG_ROCKETJUMP_FAILED);

	Arm(&state, &witness, &observation);
	observation.launcher_selected = true;
	observation.weapon_ready = true;
	CHECK(SG_RocketJumpLiveCommand(&state, &observation) ==
	      SG_ROCKETJUMP_COMMAND_FIRE);
	CHECK(SG_RocketJumpLiveFired(&state,
	      (sg_rocketjump_projectile_key_t){ 12U, 34U },
	      expected_impact_q8));
	CHECK(state.phase == SG_ROCKETJUMP_FLIGHT);
	CHECK(state.projectile.key == 12U && state.projectile.generation == 34U);
}

static qboolean Impact(sg_rocketjump_live_state_t *state,
	sg_rocketjump_projectile_key_t projectile, qboolean expected_surface,
	int health_after, short velocity_after_z)
{
	short before[3] = { 0, 0, 100 };
	short after[3] = { 0, 0, velocity_after_z };

	if (!SG_RocketJumpLiveImpactBegin(state, projectile, true, false,
	                                 expected_surface, 100, before))
		return false;
	return SG_RocketJumpLiveImpactEnd(state, projectile, health_after, after);
}

static void BeginFlight(sg_rocketjump_live_state_t *state,
	sg_rocketjump_witness_t *witness,
	sg_rocketjump_observation_t *observation)
{
	Arm(state, witness, observation);
	observation->launcher_selected = true;
	observation->weapon_ready = true;
	CHECK(SG_RocketJumpLiveCommand(state, observation) ==
	      SG_ROCKETJUMP_COMMAND_FIRE);
	CHECK(SG_RocketJumpLiveFired(state,
	      (sg_rocketjump_projectile_key_t){ 12U, 34U },
	      expected_impact_q8));
}

static void TestImpactAuthentication(void)
{
	sg_rocketjump_live_state_t state;
	sg_rocketjump_witness_t witness;
	sg_rocketjump_observation_t observation;
	sg_rocketjump_projectile_key_t projectile = { 12U, 34U };

	BeginFlight(&state, &witness, &observation);
	projectile.generation++;
	CHECK(!Impact(&state, projectile, true, 54, 900));
	CHECK(state.phase == SG_ROCKETJUMP_FAILED &&
	      state.failure == SG_ROCKETJUMP_FAILURE_PROJECTILE);

	BeginFlight(&state, &witness, &observation);
	projectile.generation--;
	CHECK(!Impact(&state, projectile, false, 54, 900));
	CHECK(state.failure == SG_ROCKETJUMP_FAILURE_IMPACT);

	BeginFlight(&state, &witness, &observation);
	CHECK(!Impact(&state, projectile, true, 53, 900));
	CHECK(state.failure == SG_ROCKETJUMP_FAILURE_DAMAGE);

	BeginFlight(&state, &witness, &observation);
	CHECK(!Impact(&state, projectile, true, 54, 100));
	CHECK(state.failure == SG_ROCKETJUMP_FAILURE_DAMAGE);

	BeginFlight(&state, &witness, &observation);
	CHECK(Impact(&state, projectile, true, 54, 900));
	CHECK(state.phase == SG_ROCKETJUMP_FLIGHT && state.impact_confirmed);
}

static void TestArrivalRequiresBlast(void)
{
	sg_rocketjump_live_state_t state;
	sg_rocketjump_witness_t witness;
	sg_rocketjump_observation_t observation;
	sg_rocketjump_projectile_key_t projectile = { 12U, 34U };

	BeginFlight(&state, &witness, &observation);
	CHECK(!SG_RocketJumpLiveBoundary(&state, true, true));
	CHECK(state.phase == SG_ROCKETJUMP_FAILED &&
	      state.failure == SG_ROCKETJUMP_FAILURE_LANDING);

	BeginFlight(&state, &witness, &observation);
	CHECK(Impact(&state, projectile, true, 54, 900));
	CHECK(SG_RocketJumpLiveBoundary(&state, true, true));
	CHECK(state.phase == SG_ROCKETJUMP_COMPLETE);
	CHECK(!SG_RocketJumpLiveOwns(&state));
}

static void TestLaunchBoundaryAndTimeouts(void)
{
	sg_rocketjump_live_state_t state;
	sg_rocketjump_witness_t witness = Witness();
	sg_rocketjump_observation_t observation = Observation(&witness);

	observation.origin_q8[0]++;
	CHECK(!SG_RocketJumpLiveBegin(&state, &witness, &observation));
	CHECK(state.failure == SG_ROCKETJUMP_FAILURE_SOURCE);

	observation = Observation(&witness);
	observation.quad_active = true;
	CHECK(!SG_RocketJumpLiveBegin(&state, &witness, &observation));
	CHECK(state.failure == SG_ROCKETJUMP_FAILURE_LAUNCH_STATE);

	observation = Observation(&witness);
	observation.jump_released = false;
	CHECK(!SG_RocketJumpLiveBegin(&state, &witness, &observation));
	CHECK(state.failure == SG_ROCKETJUMP_FAILURE_LAUNCH_STATE);

	Arm(&state, &witness, &observation);
	CHECK(SG_RocketJumpLiveStep(&state, 3025));
	CHECK(SG_RocketJumpLiveStep(&state, 975));
	CHECK(!SG_RocketJumpLiveStep(&state, 1));
	CHECK(state.phase == SG_ROCKETJUMP_FAILED &&
	      state.failure == SG_ROCKETJUMP_FAILURE_TIMEOUT);

	BeginFlight(&state, &witness, &observation);
	state.witness.cost_ms = SG_ROCKETJUMP_MAX_ACTION_MS;
	CHECK(SG_RocketJumpLiveStep(&state, 3025));
	CHECK(SG_RocketJumpLiveStep(&state, 475));
	CHECK(!SG_RocketJumpLiveStep(&state, 1));
	CHECK(state.failure == SG_ROCKETJUMP_FAILURE_TIMEOUT);
}

static void TestPhysicalPhaseMembership(void)
{
	CHECK(!SG_RocketJumpPhasePhysical(SG_ROCKETJUMP_IDLE));
	CHECK(!SG_RocketJumpPhasePhysical(SG_ROCKETJUMP_EQUIP));
	CHECK(SG_RocketJumpPhasePhysical(SG_ROCKETJUMP_ARMED));
	CHECK(SG_RocketJumpPhasePhysical(SG_ROCKETJUMP_FLIGHT));
	CHECK(!SG_RocketJumpPhasePhysical(SG_ROCKETJUMP_COMPLETE));
	CHECK(!SG_RocketJumpPhasePhysical(SG_ROCKETJUMP_FAILED));
}

static void TestSharedArrivalEnvelopeBoundaries(void)
{
	short destination[3] = { 1000, -2000, 3000 };
	short origin[3] = { 1000, -2000, 3000 };

	origin[2] = destination[2] - SG_ROCKETJUMP_ARRIVAL_Z_Q8;
	CHECK(SG_RocketJumpArrivalEnvelope(origin, destination));
	origin[2]--;
	CHECK(!SG_RocketJumpArrivalEnvelope(origin, destination));
	origin[2] = destination[2] + SG_ROCKETJUMP_ARRIVAL_Z_Q8;
	CHECK(SG_RocketJumpArrivalEnvelope(origin, destination));
	origin[2]++;
	CHECK(!SG_RocketJumpArrivalEnvelope(origin, destination));

	origin[2] = destination[2];
	origin[0] = destination[0] - SG_ROCKETJUMP_ARRIVAL_RADIUS_Q8 + 1;
	CHECK(SG_RocketJumpArrivalEnvelope(origin, destination));
	origin[0]--;
	CHECK(!SG_RocketJumpArrivalEnvelope(origin, destination));
}

static void TestVerticalControlPreservesSerializedYaw(void)
{
	vec3_t origin = { 576.0f, -1120.0f, -322.0f };
	vec3_t muzzle, forward;

	CHECK(SG_RocketJumpControlMuzzle(origin, 16384, 0, muzzle, forward));
	CHECK(fabsf(muzzle[0] - 576.0f) < 0.001f);
	CHECK(fabsf(muzzle[1] + 1128.0f) < 0.001f);
	CHECK(fabsf(muzzle[2] + 316.0f) < 0.001f);
	CHECK(fabsf(forward[0]) < 0.001f);
	CHECK(fabsf(forward[1]) < 0.001f);
	CHECK(fabsf(forward[2] + 1.0f) < 0.001f);
}

int main(void)
{
	TestWeaponReadyIsRequired();
	TestProjectileConfirmationOwnsFlight();
	TestImpactAuthentication();
	TestArrivalRequiresBlast();
	TestLaunchBoundaryAndTimeouts();
	TestPhysicalPhaseMembership();
	TestSharedArrivalEnvelopeBoundaries();
	TestVerticalControlPreservesSerializedYaw();
	if (failures)
	{
		fprintf(stderr, "sg_rocketjump_live_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("sg_rocketjump_live_test: ok");
	return 0;
}
