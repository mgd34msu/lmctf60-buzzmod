#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef q_exported
#define q_exported
#endif
#include "../slipgate/sg_host_law_publication.h"
#include "../game.h"
#include "../slipgate/sg_host_law_owner.h"
#include "../slipgate/sg_hooks.h"

game_import_t gi;
sg_host_t sg_host;
cvar_t *sv_gravity;
cvar_t *sv_maxvelocity;
cvar_t *want_funky_gravity;
cvar_t *ctfflags;

static cvar_t gravity_cvar;
static cvar_t maxvelocity_cvar;
static cvar_t funky_gravity_cvar;
static cvar_t airaccelerate_cvar;
static cvar_t ctf_flags_cvar;
static int failures;

/* This is a real BSP-shaped world. Pmove reaches it through the production
 * host collision adapter; no test callback supplies movement authority. */
static sg_bsp_plane_t planes[1];
static sg_bsp_node_t nodes[1];
static sg_bsp_leaf_t leaves[2];
static sg_bsp_model_t models[1];
static sg_bsp_world_t test_world;

extern void Pmove(pmove_t *pmove);
int CTF_HookPullVelocity(const vec3_t start, const vec3_t bite,
	vec3_t velocity);
void Com_DPrintf(const char *format, ...);
void Com_Printf(char *format, ...);

void Com_DPrintf(const char *format, ...)
{
	(void)format;
}

void Com_Printf(char *format, ...)
{
	(void)format;
}

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static void SetVector(float value[3], float x, float y, float z)
{
	value[0] = x;
	value[1] = y;
	value[2] = z;
}

static void InitializeWorld(void)
{
	memset(&test_world, 0, sizeof(test_world));
	memset(planes, 0, sizeof(planes));
	memset(nodes, 0, sizeof(nodes));
	memset(leaves, 0, sizeof(leaves));
	memset(models, 0, sizeof(models));
	SetVector(planes[0].normal.value, 0.0f, 0.0f, 1.0f);
	planes[0].type = 2;
	nodes[0].plane = 0U;
	nodes[0].children[0] = -1;
	nodes[0].children[1] = -2;
	models[0].headnode = 0;
	test_world.planes = planes;
	test_world.plane_count = 1U;
	test_world.nodes = nodes;
	test_world.node_count = 1U;
	test_world.leaves = leaves;
	test_world.leaf_count = 2U;
	test_world.models = models;
	test_world.model_count = 1U;
}

static sg_rune_model_identity_t Identity(void)
{
	sg_rune_model_identity_t identity;

	memset(&identity, 0, sizeof(identity));
	identity.bsp_content_id = UINT64_C(0x101);
	identity.entity_semantics_id = UINT64_C(0x202);
	identity.physics_abi_id = UINT64_C(0x303);
	identity.source_set_identity = UINT64_C(0x404);
	identity.schema_id = UINT64_C(0x505);
	identity.producer_identity = UINT64_C(0x606);
	SetVector(identity.standing_hull.mins.value, -16.0f, -16.0f, -24.0f);
	SetVector(identity.standing_hull.maxs.value, 16.0f, 16.0f, 32.0f);
	SetVector(identity.crouching_hull.mins.value, -16.0f, -16.0f, -24.0f);
	SetVector(identity.crouching_hull.maxs.value, 16.0f, 16.0f, 4.0f);
	identity.physics.gravity = 800.0f;
	identity.physics.ground_acceleration = 10.0f;
	identity.physics.air_acceleration = 1.0f;
	identity.physics.water_acceleration = 10.0f;
	identity.physics.hook_acceleration = 800.0f;
	identity.physics.external_acceleration = 1.0f;
	identity.physics.water_drag = 1.0f;
	identity.physics.max_velocity = 2000.0f;
	identity.physics.frame_ms = SG_HOST_ENGINE_FRAME_MS;
	identity.physics.substep_ms = SG_HOST_ENGINE_PMOVE_SUBSTEP_MS;
	return identity;
}

static sg_host_collision_authority_t Authority(void)
{
	sg_host_collision_authority_t authority;
	sg_rune_model_identity_t identity = Identity();
	sg_host_collision_error_t error;

	memset(&authority, 0, sizeof(authority));
	CHECK(SG_HostCollisionInit(&authority, &test_world, &identity, &error));
	CHECK(error == SG_HOST_COLLISION_ERROR_NONE);
	return authority;
}

static cvar_t *TestCvar(char *name, char *value, int flags)
{
	(void)value;
	(void)flags;
	if (strcmp(name, "sv_airaccelerate") == 0)
		return &airaccelerate_cvar;
	return NULL;
}

static int HookSpeed(const vec3_t start, const vec3_t bite, vec3_t velocity)
{
	float distance;
	int speed;

	VectorSubtract(bite, start, velocity);
	distance = VectorLength(velocity);
	speed = (int)distance;
	VectorNormalize(velocity);
	if (speed > 120)
		VectorScale(velocity, SG_HOST_HOOK_PULL_SPEED, velocity);
	else if (speed > 100)
		VectorScale(velocity, (float)(speed * 5), velocity);
	else if (speed > 80)
		VectorScale(velocity, (float)(speed * 4), velocity);
	else if (speed > 40)
		VectorScale(velocity, (float)(speed * 3), velocity);
	else if (speed > 20)
		VectorScale(velocity, (float)(speed * 2), velocity);
	else if (speed > 10)
		VectorScale(velocity, (float)speed, velocity);
	return speed;
}

int CTF_HookPullVelocity(const vec3_t start, const vec3_t bite,
	vec3_t velocity)
{
	return HookSpeed(start, bite, velocity);
}

static void ForgedPmove(pmove_t *pmove)
{
	Pmove(pmove);
	pmove->maxs[2] += 1.0f;
}

static sg_host_law_publication_t *Issue(void)
{
	sg_host_collision_authority_t authority = Authority();
	sg_host_law_publication_t *publication = NULL;
	sg_host_law_result_t result;

	result = SG_HostLawPublicationIssue(&authority, &publication);
	CHECK(result.status == SG_HOST_LAW_OK);
	CHECK(publication != NULL);
	return publication;
}

static sg_host_law_view_t Read(const sg_host_law_publication_t *publication)
{
	sg_host_law_view_t view;
	sg_host_law_result_t result;

	memset(&view, 0xa5, sizeof(view));
	result = SG_HostLawPublicationRead(publication, &view);
	CHECK(result.status == SG_HOST_LAW_OK);
	return view;
}

static void TestEngineBindingAndParity(void)
{
	sg_host_engine_pmove_abi_t abi;
	sg_host_engine_parity_result_t parity;

	gi.Pmove = Pmove;
	CHECK(SG_HostEnginePmoveABI(&abi));
	CHECK(abi.version == SG_HOST_ENGINE_PMOVE_ABI_VERSION);
	CHECK(abi.game_api_version == GAME_API_VERSION);
	CHECK(abi.import_size == (uint32_t)sizeof(game_import_t));
	CHECK(abi.pmove_offset == (uint32_t)offsetof(game_import_t, Pmove));
	CHECK(abi.pmove_size == (uint32_t)sizeof(pmove_t));
	CHECK(abi.state_size == (uint32_t)sizeof(pmove_state_t));
	CHECK(abi.command_size == (uint32_t)sizeof(usercmd_t));
	CHECK(abi.fraction_bits == SG_HOST_ENGINE_PMOVE_FRACTION_BITS);
	CHECK(abi.substep_ms == SG_HOST_ENGINE_PMOVE_SUBSTEP_MS);
	CHECK(abi.identity == SG_HOST_ENGINE_PMOVE_ABI_ID);
	CHECK(SG_HostEnginePmoveParity(&parity));
	CHECK(parity.cases == SG_HOST_ENGINE_PARITY_ALL);
	CHECK(parity.fingerprint != 0U);
	CHECK(parity.engine_calls == 7U);
	CHECK(parity.trace_calls != 0U);
	CHECK(parity.contents_calls != 0U);
}

static void TestPublicationAndCallbackIsolation(void)
{
	sg_host_law_publication_t *publication = Issue();
	sg_host_law_view_t view = Read(publication);
	sg_host_law_result_t result;

	CHECK(view.version == SG_HOST_LAW_PUBLICATION_VERSION);
	CHECK(view.identity.physics.gravity == 800.0f);
	CHECK(view.pmove_behavior_fingerprint != 0U);
	CHECK(view.hook.identity == SG_HOST_HOOK_LAW_ID);
	CHECK(view.mechanism.identity == SG_HOST_MECHANISM_LAW_ID);
	/* A forged legacy callback has no authority over a publication. */
	sg_host.pmove = ForgedPmove;
	result = SG_HostLawPublicationRevalidateProduction(publication);
	CHECK(result.status == SG_HOST_LAW_OK);
	/* The actual engine slot is authoritative and behavior drift is caught. */
	gi.Pmove = ForgedPmove;
	result = SG_HostLawPublicationRevalidateProduction(publication);
	CHECK(result.status == SG_HOST_LAW_PRODUCTION_DRIFT);
	CHECK(result.field == SG_HOST_LAW_FIELD_PMOVE_BEHAVIOR);
	gi.Pmove = Pmove;
	sg_host.pmove = NULL;
	result = SG_HostLawPublicationRevalidateProduction(publication);
	CHECK(result.status == SG_HOST_LAW_OK);
	SG_HostLawPublicationDestroy(publication);
}

static void TestPmoveAndCollisionExecution(void)
{
	sg_host_law_publication_t *publication = Issue();
	sg_host_collision_trace_t trace;
	sg_host_pmove_request_t request;
	sg_host_pmove_result_t pmove_result;
	sg_host_pmove_error_t pmove_error;
	const float zero[3] = { 0.0f, 0.0f, 0.0f };
	const float end[3] = { 64.0f, 0.0f, 0.0f };
	sg_host_law_result_t result;

	memset(&trace, 0, sizeof(trace));
	result = SG_HostLawPublicationCollisionTrace(publication, NULL, zero, zero,
		zero, end, SG_HOST_MASK_PLAYER_SOLID, &trace);
	CHECK(result.status == SG_HOST_LAW_OK);
	CHECK(trace.fraction == 1.0f);
	memset(&request, 0, sizeof(request));
	request.state.pm_type = PM_NORMAL;
	request.state.origin[2] = 1600;
	request.previous_state = request.state;
	request.command.forwardmove = 300;
	result = SG_HostLawPublicationPmove(publication, NULL, &request,
		&pmove_result, &pmove_error);
	CHECK(result.status == SG_HOST_LAW_OK);
	CHECK(pmove_error == SG_HOST_PMOVE_ERROR_NONE);
	CHECK(pmove_result.evaluated_steps == 4U);
	CHECK(pmove_result.elapsed_ms == SG_HOST_ENGINE_FRAME_MS);
	CHECK(pmove_result.physics_abi_id == UINT64_C(0x303));
	SG_HostLawPublicationDestroy(publication);
}

static void TestHookChronology(void)
{
	sg_host_law_publication_t *publication = Issue();
	sg_host_hook_observation_t observation;
	sg_host_hook_step_t step;
	float origin[3] = { 10.0f, 20.0f, 30.0f };
	float forward[3] = { 1.0f, 0.0f, 0.0f };
	float right[3] = { 0.0f, 1.0f, 0.0f };
	float muzzle[3];
	sg_host_law_result_t result;

	result = SG_HostLawPublicationHookMuzzle(publication, origin, 22.0f,
		2, forward, right, muzzle);
	CHECK(result.status == SG_HOST_LAW_OK);
	CHECK(muzzle[0] == 18.0f && muzzle[1] == 20.0f && muzzle[2] == 44.0f);
	memset(&observation, 0, sizeof(observation));
	observation.event = SG_HOST_HOOK_FIRE;
	observation.phase = SG_HOST_HOOK_IDLE;
	observation.muzzle_clear = 1;
	observation.attack_held = 1;
	result = SG_HostLawPublicationHookStep(publication, &observation, &step);
	CHECK(result.status == SG_HOST_LAW_OK && step.accepted);
	CHECK(step.next_phase == SG_HOST_HOOK_IN_FLIGHT);
	observation.event = SG_HOST_HOOK_FLIGHT_HIT;
	observation.phase = SG_HOST_HOOK_IN_FLIGHT;
	observation.first_hit = 1;
	observation.target_kind = SG_HOST_HOOK_TARGET_PLAYER;
	observation.target_identity = UINT64_C(0x99);
	result = SG_HostLawPublicationHookStep(publication, &observation, &step);
	CHECK(result.status == SG_HOST_LAW_OK && step.attached);
	CHECK(step.first_hit && step.damage == SG_HOST_HOOK_INITIAL_DAMAGE);
	CHECK(step.target_identity == UINT64_C(0x99));
	{
		sg_host_hook_observation_t killed = observation;

		killed.target_died_after_damage = 1;
		result = SG_HostLawPublicationHookStep(publication, &killed, &step);
		CHECK(result.status == SG_HOST_LAW_OK && step.accepted &&
			step.first_hit && !step.attached && step.aborted &&
			step.damage == SG_HOST_HOOK_INITIAL_DAMAGE &&
			step.next_phase == SG_HOST_HOOK_IDLE);
	}
	observation.event = SG_HOST_HOOK_ATTACHED_TICK;
	observation.phase = SG_HOST_HOOK_ATTACHED;
	observation.attached_target_identity = UINT64_C(0x99);
	observation.frame = 7U;
	observation.last_damage_frame = 0U;
	observation.bite_distance = 200.0f;
	result = SG_HostLawPublicationHookStep(publication, &observation, &step);
	CHECK(result.status == SG_HOST_LAW_OK && step.accepted);
	CHECK(step.pull_after_pmove && !step.pull_before_pmove &&
		step.gravity_applied && !step.gravity_zeroed);
	CHECK(step.damage == SG_HOST_HOOK_ATTACHED_DAMAGE);
	observation.frame = 8U;
	observation.bite_distance = SG_HOST_HOOK_NEAR_BITE_DISTANCE;
	result = SG_HostLawPublicationHookStep(publication, &observation, &step);
	CHECK(result.status == SG_HOST_LAW_OK && step.accepted);
	CHECK(!step.gravity_applied && !step.gravity_zeroed && step.damage == 0U);
	observation.frame = 9U;
	observation.bite_distance =
		SG_HOST_HOOK_NEAR_BITE_GRAVITY_ZERO_DISTANCE - 1.0f;
	result = SG_HostLawPublicationHookStep(publication, &observation, &step);
	CHECK(result.status == SG_HOST_LAW_OK && step.accepted &&
		step.gravity_zeroed && !step.gravity_applied);
	observation.frame = 10U;
	observation.bite_distance = SG_HOST_HOOK_NEAR_BITE_GRAVITY_ZERO_DISTANCE;
	result = SG_HostLawPublicationHookStep(publication, &observation, &step);
	CHECK(result.status == SG_HOST_LAW_OK && step.accepted &&
		!step.gravity_zeroed && !step.gravity_applied);
	observation.target_identity = UINT64_C(0x100);
	result = SG_HostLawPublicationHookStep(publication, &observation, &step);
	CHECK(result.status == SG_HOST_LAW_OK && !step.accepted);
	CHECK(step.target_identity == UINT64_C(0x99) &&
		step.target_kind == SG_HOST_HOOK_TARGET_NONE);
	observation.target_identity = UINT64_C(0x99);
	observation.sky = 1;
	result = SG_HostLawPublicationHookStep(publication, &observation, &step);
	CHECK(result.status == SG_HOST_LAW_OK && !step.accepted);
	observation.event = SG_HOST_HOOK_RELEASE;
	observation.sky = 0;
	result = SG_HostLawPublicationHookStep(publication, &observation, &step);
	CHECK(result.status == SG_HOST_LAW_OK && step.released && step.coast_velocity);
	CHECK(step.next_phase == SG_HOST_HOOK_COAST);
	observation.event = SG_HOST_HOOK_REFIRE;
	observation.phase = SG_HOST_HOOK_COAST;
	observation.muzzle_clear = 1;
	observation.attack_held = 1;
	result = SG_HostLawPublicationHookStep(publication, &observation, &step);
	CHECK(result.status == SG_HOST_LAW_OK && step.accepted);
	CHECK(step.next_phase == SG_HOST_HOOK_IN_FLIGHT);
	SG_HostLawPublicationDestroy(publication);
}

static void TestHookDamagePolicy(void)
{
	sg_host_law_publication_t *publication;
	sg_host_hook_observation_t observation;
	sg_host_hook_step_t step;
	sg_host_law_result_t result;

	ctf_flags_cvar.value = (float)SG_HOST_HOOK_CTF_NO_GRAP_DAMAGE;
	publication = Issue();
	memset(&observation, 0, sizeof(observation));
	observation.event = SG_HOST_HOOK_FLIGHT_HIT;
	observation.phase = SG_HOST_HOOK_IN_FLIGHT;
	observation.first_hit = 1;
	observation.target_kind = SG_HOST_HOOK_TARGET_PLAYER;
	observation.target_identity = UINT64_C(0x42);
	result = SG_HostLawPublicationHookStep(publication, &observation, &step);
	CHECK(result.status == SG_HOST_LAW_OK && step.attached && step.damage == 0U);
	SG_HostLawPublicationDestroy(publication);
	ctf_flags_cvar.value = 0.0f;
}

static void TestMechanismEquations(void)
{
	sg_host_law_publication_t *publication = Issue();
	sg_host_mechanism_move_result_t move;
	sg_host_mechanism_transition_t transition;
	sg_host_law_result_t result;

	result = SG_HostLawPublicationMoveSchedule(publication, 25.0f, 100.0f,
		100.0f, 100.0f, 0, &move);
	CHECK(result.status == SG_HOST_LAW_OK && move.valid && !move.accelerated);
	CHECK(move.first_think_ms == 100U && move.full_speed_frames == 2U);
	CHECK(move.residual_distance == 5.0f && move.final_speed == 50.0f);
	CHECK(move.completion_ms == 400U);
	result = SG_HostLawPublicationMoveSchedule(publication, 25.0f, 100.0f,
		100.0f, 100.0f, 1, &move);
	CHECK(result.status == SG_HOST_LAW_OK && move.first_think_ms == 0U);
	CHECK(move.completion_ms == 300U);
	result = SG_HostLawPublicationMoveSchedule(publication, 20.0f, 100.0f,
		100.0f, 100.0f, 1, &move);
	CHECK(result.status == SG_HOST_LAW_OK && move.residual_distance == 0.0f &&
		move.completion_ms == 200U);
	result = SG_HostLawPublicationMoveSchedule(publication, 100.0f, 2.0f,
		0.5f, 0.5f, 1, &move);
	CHECK(result.status == SG_HOST_LAW_OK && move.valid && move.accelerated);
	CHECK(move.first_think_ms == 100U && move.completion_ms > 100U);
	result = SG_HostLawPublicationDoorStep(publication,
		SG_HOST_MECHANISM_DOOR_TOP, 0U, SG_HOST_MECHANISM_STATE_TOP, 0.0f,
		1000U, 0U, &transition);
	CHECK(result.status == SG_HOST_LAW_OK && transition.accepted);
	CHECK(transition.next_think_ms == 4000U);
	result = SG_HostLawPublicationDoorStep(publication,
		SG_HOST_MECHANISM_DOOR_TOP, SG_HOST_MECHANISM_DOOR_TOGGLE,
		SG_HOST_MECHANISM_STATE_TOP, 0.0f, 1000U, 0U, &transition);
	CHECK(result.status == SG_HOST_LAW_OK && transition.next_think_ms == 0U);
	result = SG_HostLawPublicationDoorStep(publication,
		SG_HOST_MECHANISM_DOOR_TRIGGER_TOUCH, 0U,
		SG_HOST_MECHANISM_STATE_BOTTOM, 100.0f, 1000U, 1500U, &transition);
	CHECK(result.status == SG_HOST_LAW_OK && !transition.accepted);
	result = SG_HostLawPublicationDoorStep(publication,
		SG_HOST_MECHANISM_DOOR_TRIGGER_TOUCH, 0U,
		SG_HOST_MECHANISM_STATE_BOTTOM, 100.0f, 1500U, 1500U, &transition);
	CHECK(result.status == SG_HOST_LAW_OK && transition.next_debounce_ms == 2500U);
	result = SG_HostLawPublicationDoorStep(publication,
		SG_HOST_MECHANISM_DOOR_BLOCKED, 0U, SG_HOST_MECHANISM_STATE_DOWN,
		1.0f, 0U, 0U, &transition);
	CHECK(result.status == SG_HOST_LAW_OK && transition.reversed &&
		transition.next_state == SG_HOST_MECHANISM_STATE_UP);
	result = SG_HostLawPublicationDoorStep(publication,
		SG_HOST_MECHANISM_DOOR_BLOCKED, SG_HOST_MECHANISM_DOOR_CRUSHER,
		SG_HOST_MECHANISM_STATE_DOWN, 1.0f, 0U, 0U, &transition);
	CHECK(result.status == SG_HOST_LAW_OK && !transition.reversed);
	result = SG_HostLawPublicationDoorStep(publication,
		SG_HOST_MECHANISM_DOOR_BLOCKED, 0U, SG_HOST_MECHANISM_STATE_TOP,
		1.0f, 0U, 0U, &transition);
	CHECK(result.status == SG_HOST_LAW_OK && !transition.reversed &&
		transition.next_state == SG_HOST_MECHANISM_STATE_TOP);
	result = SG_HostLawPublicationPlatformStep(publication,
		SG_HOST_MECHANISM_PLATFORM_TOP, SG_HOST_MECHANISM_STATE_TOP, 200U,
		0U, &transition);
	CHECK(result.status == SG_HOST_LAW_OK && transition.next_think_ms == 3200U);
	result = SG_HostLawPublicationPlatformStep(publication,
		SG_HOST_MECHANISM_PLATFORM_TRIGGER_TOUCH,
		SG_HOST_MECHANISM_STATE_TOP, 500U, 0U, &transition);
	CHECK(result.status == SG_HOST_LAW_OK && transition.next_think_ms == 1500U);
	result = SG_HostLawPublicationPlatformStep(publication,
		SG_HOST_MECHANISM_PLATFORM_TRIGGER_TOUCH,
		SG_HOST_MECHANISM_STATE_BOTTOM, 500U, 0U, &transition);
	CHECK(result.status == SG_HOST_LAW_OK &&
		transition.next_state == SG_HOST_MECHANISM_STATE_UP);
	result = SG_HostLawPublicationTriggerStep(publication, 0, 0.0f, 100U,
		&transition);
	CHECK(result.status == SG_HOST_LAW_OK && transition.next_think_ms == 300U);
	result = SG_HostLawPublicationTriggerStep(publication, 0, -1.0f, 100U,
		&transition);
	CHECK(result.status == SG_HOST_LAW_OK && transition.next_think_ms == 200U);
	result = SG_HostLawPublicationTrainStep(publication,
		SG_HOST_MECHANISM_TRAIN_BLOCKED, 0U, 0.0f,
		SG_HOST_MECHANISM_STATE_UP, 0, 0, 1, 1, 99U, 100U, &transition);
	CHECK(result.status == SG_HOST_LAW_OK && !transition.accepted);
	result = SG_HostLawPublicationTrainStep(publication,
		SG_HOST_MECHANISM_TRAIN_BLOCKED, 0U, 0.0f,
		SG_HOST_MECHANISM_STATE_UP, 0, 0, 1, 1, 100U, 100U, &transition);
	CHECK(result.status == SG_HOST_LAW_OK && transition.next_debounce_ms == 600U);
	result = SG_HostLawPublicationTrainStep(publication,
		SG_HOST_MECHANISM_TRAIN_WAIT, SG_HOST_MECHANISM_TRAIN_TOGGLE, -1.0f,
		SG_HOST_MECHANISM_STATE_UP, 0, 0, 0, 0, 0U, 0U, &transition);
	CHECK(result.status == SG_HOST_LAW_OK && transition.stopped);
	{
		sg_host_law_view_t view = Read(publication);
		view.hook.near_bite_gravity_zero_distance += 1.0f;
		result = SG_HostLawPublicationMatch(publication, &view);
		CHECK(result.status == SG_HOST_LAW_PRODUCTION_DRIFT &&
			result.field == SG_HOST_LAW_FIELD_HOOK_CHRONOLOGY);
	}
	{
		sg_host_law_view_t view = Read(publication);
		view.mechanism.door_default_wait_ms++;
		result = SG_HostLawPublicationMatch(publication, &view);
		CHECK(result.status == SG_HOST_LAW_PRODUCTION_DRIFT &&
			result.field == SG_HOST_LAW_FIELD_MECHANISM_EQUATIONS);
	}
	SG_HostLawPublicationDestroy(publication);
}

static void TestOwnerFailClosedAndDrift(void)
{
	sg_host_collision_authority_t authority = Authority();
	const sg_host_law_publication_t *publication;
	sg_host_law_result_t result;

	SG_HostLawProductionReset();
	result = SG_HostLawProductionRevalidate();
	CHECK(result.status == SG_HOST_LAW_HOST_UNAVAILABLE);
	result = SG_HostLawProductionInstall(&authority);
	CHECK(result.status == SG_HOST_LAW_OK);
	publication = SG_HostLawProductionPublication();
	CHECK(publication != NULL);
	result = SG_HostLawProductionRevalidate();
	CHECK(result.status == SG_HOST_LAW_OK);
	gravity_cvar.value = 799.0f;
	result = SG_HostLawProductionRevalidate();
	CHECK(result.status == SG_HOST_LAW_PRODUCTION_DRIFT);
	CHECK(SG_HostLawProductionPublication() == NULL);
	gravity_cvar.value = 800.0f;
}

int main(void)
{
	memset(&gi, 0, sizeof(gi));
	memset(&sg_host, 0, sizeof(sg_host));
	memset(&gravity_cvar, 0, sizeof(gravity_cvar));
	memset(&maxvelocity_cvar, 0, sizeof(maxvelocity_cvar));
	memset(&funky_gravity_cvar, 0, sizeof(funky_gravity_cvar));
	memset(&airaccelerate_cvar, 0, sizeof(airaccelerate_cvar));
	memset(&ctf_flags_cvar, 0, sizeof(ctf_flags_cvar));
	gravity_cvar.value = 800.0f;
	maxvelocity_cvar.value = 2000.0f;
	airaccelerate_cvar.value = 0.0f;
	sv_gravity = &gravity_cvar;
	sv_maxvelocity = &maxvelocity_cvar;
	want_funky_gravity = &funky_gravity_cvar;
	ctfflags = &ctf_flags_cvar;
	gi.Pmove = Pmove;
	gi.cvar = TestCvar;
	InitializeWorld();

	TestEngineBindingAndParity();
	TestPublicationAndCallbackIsolation();
	TestPmoveAndCollisionExecution();
	TestHookChronology();
	TestHookDamagePolicy();
	TestMechanismEquations();
	TestOwnerFailClosedAndDrift();
	SG_HostLawProductionReset();
	if (failures != 0)
		return 1;
	puts("host-law publication tests passed");
	return 0;
}
