/* The executor turns a selected capability and the live body into one body
 * command.  These checks pin what each capability asks of the body. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "slipgate/sg_tactic_controller.h"

static int failures;

#define CHECK(condition) \
	do { \
		if (!(condition)) { \
			failures++; \
			fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, \
				__LINE__, #condition); \
		} \
	} while (0)

static sg_tactic_body_t Body(void)
{
	sg_tactic_body_t body;

	memset(&body, 0, sizeof(body));
	body.supported = 1U;
	body.gravity = 800.0f;
	body.frame_ms = 100U;
	body.substep_ms = 25U;
	return body;
}

static sg_tactic_execution_t Step(sg_tactic_capability_t capability,
	float x, float y, float z)
{
	sg_tactic_execution_t execution;

	memset(&execution, 0, sizeof(execution));
	execution.kind = SG_TACTIC_EXECUTION_PORTAL_STEP;
	execution.target_point[0] = x;
	execution.target_point[1] = y;
	execution.target_point[2] = z;
	execution.target_point_present = 1U;
	execution.selection_present = 1U;
	execution.selection_status = SG_TACTIC_RESULT_PROGRESS;
	execution.capability = capability;
	return execution;
}

static int Near(float value, float expected)
{
	return fabsf(value - expected) <= 1e-4f;
}

static void CheckWalkSteersAtTheCrossing(void)
{
	sg_tactic_body_t body = Body();
	sg_tactic_execution_t execution = Step(SG_TACTIC_CAPABILITY_WALK,
		300.0f, 400.0f, 0.0f);
	sg_tactic_command_t command;

	CHECK(SG_TacticControl(NULL, &execution, &body, &command));
	CHECK(command.status == SG_TACTIC_COMMAND_MOVE);
	CHECK(Near(command.direction[0], 0.6f) && Near(command.direction[1], 0.8f));
	CHECK(Near(command.speed, 1.0f) && Near(command.up, 0.0f));
	CHECK(command.aim_owned == 0U && command.attack == 0U);
}

static void CheckCrouchDucks(void)
{
	sg_tactic_body_t body = Body();
	sg_tactic_execution_t execution = Step(SG_TACTIC_CAPABILITY_CROUCH,
		100.0f, 0.0f, 0.0f);
	sg_tactic_command_t command;

	CHECK(SG_TacticControl(NULL, &execution, &body, &command));
	CHECK(command.status == SG_TACTIC_COMMAND_MOVE);
	CHECK(Near(command.up, -1.0f) && Near(command.direction[0], 1.0f));
}

/* Far from the crossing the jump is a run; within the jump's reach it is a
 * run and a press; off the ground the press is withheld. */
static void CheckJumpPressesWithinReach(void)
{
	sg_tactic_body_t body = Body();
	sg_tactic_execution_t far = Step(SG_TACTIC_CAPABILITY_JUMP, 500.0f, 0.0f,
		30.0f);
	sg_tactic_execution_t near = Step(SG_TACTIC_CAPABILITY_JUMP, 100.0f, 0.0f,
		30.0f);
	sg_tactic_command_t command;

	CHECK(SG_TacticControl(NULL, &far, &body, &command));
	CHECK(command.status == SG_TACTIC_COMMAND_MOVE && Near(command.up, 0.0f));
	CHECK(SG_TacticControl(NULL, &near, &body, &command));
	CHECK(command.status == SG_TACTIC_COMMAND_MOVE && Near(command.up, 1.0f));
	body.supported = 0U;
	CHECK(SG_TacticControl(NULL, &near, &body, &command));
	CHECK(command.status == SG_TACTIC_COMMAND_MOVE && Near(command.up, 0.0f));
}

/* The rocket jump asks for the launcher first and keeps closing; with it in
 * hand, on the ground, in reach, it faces the crossing, aims down, and fires
 * and jumps in the same command.  Airborne it never fires. */
static void CheckRocketJumpFiresOnlyWhenArmedAndGrounded(void)
{
	sg_tactic_body_t body = Body();
	sg_tactic_execution_t execution = Step(SG_TACTIC_CAPABILITY_ROCKET_JUMP,
		0.0f, 100.0f, 150.0f);
	sg_tactic_command_t command;

	CHECK(SG_TacticControl(NULL, &execution, &body, &command));
	CHECK(command.want_launcher == 1U && command.attack == 0U);
	CHECK(command.status == SG_TACTIC_COMMAND_MOVE && Near(command.speed, 1.0f));

	body.launcher_ready = 1U;
	CHECK(SG_TacticControl(NULL, &execution, &body, &command));
	CHECK(command.attack == 1U && Near(command.up, 1.0f));
	CHECK(command.aim_owned == 1U && Near(command.pitch, 90.0f));
	CHECK(Near(command.yaw, 90.0f));

	body.supported = 0U;
	CHECK(SG_TacticControl(NULL, &execution, &body, &command));
	CHECK(command.attack == 0U && command.aim_owned == 0U);

	body.supported = 1U;
	execution.target_point[1] = 5000.0f;
	CHECK(SG_TacticControl(NULL, &execution, &body, &command));
	CHECK(command.attack == 0U && command.want_launcher == 1U);
}

static void CheckSwimClimbsTowardTheTarget(void)
{
	sg_tactic_body_t body = Body();
	sg_tactic_execution_t execution = Step(SG_TACTIC_CAPABILITY_SWIM,
		30.0f, 0.0f, 40.0f);
	sg_tactic_command_t command;

	body.supported = 0U;
	body.waterlevel = 3U;
	CHECK(SG_TacticControl(NULL, &execution, &body, &command));
	CHECK(command.status == SG_TACTIC_COMMAND_MOVE);
	CHECK(Near(command.up, 0.8f) && Near(command.direction[0], 1.0f));
}

static void CheckStanceStepAndHolds(void)
{
	sg_tactic_body_t body = Body();
	sg_tactic_execution_t execution;
	sg_tactic_command_t command;

	memset(&execution, 0, sizeof(execution));
	execution.kind = SG_TACTIC_EXECUTION_STANCE_STEP;
	execution.target_stance = SG_RUNE_COMPACT_FIELD_CROUCHING;
	CHECK(SG_TacticControl(NULL, &execution, &body, &command));
	CHECK(command.status == SG_TACTIC_COMMAND_MOVE && Near(command.up, -1.0f));
	CHECK(Near(command.speed, 0.0f));

	execution.kind = SG_TACTIC_EXECUTION_BLOCKED_NOW;
	CHECK(SG_TacticControl(NULL, &execution, &body, &command));
	CHECK(command.status == SG_TACTIC_COMMAND_HOLD);

	execution = Step(SG_TACTIC_CAPABILITY_WAIT, 100.0f, 0.0f, 0.0f);
	CHECK(SG_TacticControl(NULL, &execution, &body, &command));
	CHECK(command.status == SG_TACTIC_COMMAND_HOLD && Near(command.speed, 0.0f));
}

/* A terminal point is steered to whatever the selection says. */
static void CheckLocalPointIsPlainSteering(void)
{
	sg_tactic_body_t body = Body();
	sg_tactic_execution_t execution;
	sg_tactic_command_t command;

	memset(&execution, 0, sizeof(execution));
	execution.kind = SG_TACTIC_EXECUTION_LOCAL_DESTINATION;
	execution.destination.kind = SG_RUNE_COMPACT_DESTINATION_POINT;
	execution.destination.value.point.value[0] = -800;
	CHECK(SG_TacticControl(NULL, &execution, &body, &command));
	CHECK(command.status == SG_TACTIC_COMMAND_MOVE);
	CHECK(Near(command.direction[0], -1.0f));
}

static void CheckRejectsBadInputs(void)
{
	sg_tactic_body_t body = Body();
	sg_tactic_execution_t execution = Step(SG_TACTIC_CAPABILITY_WALK,
		100.0f, 0.0f, 0.0f);
	sg_tactic_command_t command;

	CHECK(!SG_TacticControl(NULL, NULL, &body, &command));
	CHECK(!SG_TacticControl(NULL, &execution, NULL, &command));
	CHECK(!SG_TacticControl(NULL, &execution, &body, NULL));
	body.origin[0] = NAN;
	CHECK(!SG_TacticControl(NULL, &execution, &body, &command));
	CHECK(command.status == SG_TACTIC_COMMAND_HOLD);
}

/* A model with two hook targets: one certified fact with a witness on the
 * wall, one candidate group whose two patches sit at different distances. */
static sg_rune_compact_model_t HookModel(void)
{
	static sg_rune_compact_response_fact_t facts[1];
	static sg_rune_compact_response_candidate_group_t groups[1];
	static sg_rune_compact_response_endpoint_group_t endpoints[1];
	static uint32_t members[2] = { 1U, 0U };
	static sg_rune_compact_response_patch_t patches[2];
	static sg_rune_compact_movement_hook_target_t targets[2];
	sg_rune_compact_model_t model;

	memset(&model, 0, sizeof(model));
	memset(facts, 0, sizeof(facts));
	facts[0].target_witness.value[0] = 800;   /* 100, 0, 100 */
	facts[0].target_witness.value[2] = 800;
	memset(groups, 0, sizeof(groups));
	groups[0].target_group = 0U;
	memset(endpoints, 0, sizeof(endpoints));
	endpoints[0].first_member = 0U;
	endpoints[0].member_count = 2U;
	memset(patches, 0, sizeof(patches));
	patches[0].bounds.mins.value[0] = 1600;   /* centre 250, 0, 50 */
	patches[0].bounds.maxs.value[0] = 2400;
	patches[0].bounds.maxs.value[2] = 800;
	patches[1].bounds.mins.value[0] = 6400;   /* centre 850, 0, 50 */
	patches[1].bounds.maxs.value[0] = 7200;
	patches[1].bounds.maxs.value[2] = 800;
	memset(targets, 0, sizeof(targets));
	targets[0].provenance =
		SG_RUNE_MOVEMENT_HOOK_TARGET_PROVENANCE_STATIC_RESPONSE;
	targets[0].response.kind = SG_RUNE_COMPACT_RESPONSE_REF_CERTIFIED_FACT;
	targets[0].response.index = 0U;
	targets[1].provenance =
		SG_RUNE_MOVEMENT_HOOK_TARGET_PROVENANCE_STATIC_RESPONSE;
	targets[1].response.kind = SG_RUNE_COMPACT_RESPONSE_REF_CANDIDATE_GROUP;
	targets[1].response.index = 0U;
	model.movement_hook_targets = targets;
	model.movement_hook_target_count = 2U;
	model.response.facts = facts;
	model.response.fact_count = 1U;
	model.response.candidate_groups = groups;
	model.response.candidate_group_count = 1U;
	model.response.target_endpoint_groups = endpoints;
	model.response.target_endpoint_group_count = 1U;
	model.response.target_endpoint_members = members;
	model.response.target_endpoint_member_count = 2U;
	model.response.target_patches = patches;
	model.response.target_patch_count = 2U;
	return model;
}

static void CheckHookAimResolves(void)
{
	sg_rune_compact_model_t model = HookModel();
	const float from[3] = { 0.0f, 0.0f, 0.0f };
	float aim[3];

	CHECK(SG_TacticHookTargetAim(&model, 0U, from, aim));
	CHECK(Near(aim[0], 100.0f) && Near(aim[1], 0.0f) && Near(aim[2], 100.0f));
	/* The nearer patch of the group wins. */
	CHECK(SG_TacticHookTargetAim(&model, 1U, from, aim));
	CHECK(Near(aim[0], 250.0f) && Near(aim[2], 50.0f));
	CHECK(!SG_TacticHookTargetAim(&model, 2U, from, aim));
	CHECK(!SG_TacticHookTargetAim(NULL, 0U, from, aim));
}

/* Idle with a bite ahead: aim from the eye and fire, still moving.  A hook
 * fired on the run or from the air pulls when it bites, so nothing here
 * waits for the ground.  Riding presses nothing; a coast successor lets go;
 * a coasting body fires again for the chained fling. */
static void CheckHookFiresRidesAndReleases(void)
{
	sg_rune_compact_model_t model = HookModel();
	sg_tactic_body_t body = Body();
	sg_tactic_execution_t execution = Step(SG_TACTIC_CAPABILITY_HOOK,
		100.0f, 0.0f, 100.0f);
	sg_tactic_command_t command;

	body.view_height = 22.0f;
	body.hook_ready = 1U;
	execution.hook_target = 0U;
	execution.hook_target_present = 1U;
	execution.target_hook_phase = SG_HOST_HOOK_IN_FLIGHT;
	body.hook_phase = SG_HOST_HOOK_IDLE;
	CHECK(SG_TacticControl(&model, &execution, &body, &command));
	CHECK(command.status == SG_TACTIC_COMMAND_MOVE && command.hook_fire == 1U);
	CHECK(command.aim_owned == 1U && Near(command.yaw, 0.0f));
	CHECK(Near(command.pitch, -atan2f(78.0f, 100.0f) * 180.0f / (float)M_PI));
	CHECK(Near(command.speed, 1.0f));

	body.supported = 0U;
	CHECK(SG_TacticControl(&model, &execution, &body, &command));
	CHECK(command.hook_fire == 1U);

	body.hook_ready = 0U;
	CHECK(SG_TacticControl(&model, &execution, &body, &command));
	CHECK(command.status == SG_TACTIC_COMMAND_UNSUPPORTED &&
		command.hook_fire == 0U);

	body.hook_ready = 1U;
	body.hook_phase = SG_HOST_HOOK_ATTACHED;
	execution.target_hook_phase = SG_HOST_HOOK_ATTACHED;
	CHECK(SG_TacticControl(&model, &execution, &body, &command));
	CHECK(command.hook_fire == 0U && command.hook_release == 0U);
	CHECK(command.status == SG_TACTIC_COMMAND_MOVE);

	execution.target_hook_phase = SG_HOST_HOOK_COAST;
	CHECK(SG_TacticControl(&model, &execution, &body, &command));
	CHECK(command.hook_release == 1U && command.aim_owned == 0U);

	body.hook_phase = SG_HOST_HOOK_COAST;
	execution.target_hook_phase = SG_HOST_HOOK_IN_FLIGHT;
	CHECK(SG_TacticControl(&model, &execution, &body, &command));
	CHECK(command.hook_fire == 1U && command.aim_owned == 1U);

	execution.hook_target_present = 0U;
	body.hook_phase = SG_HOST_HOOK_IDLE;
	CHECK(SG_TacticControl(&model, &execution, &body, &command));
	CHECK(command.status == SG_TACTIC_COMMAND_UNSUPPORTED);
}

int main(void)
{
	CheckWalkSteersAtTheCrossing();
	CheckCrouchDucks();
	CheckJumpPressesWithinReach();
	CheckRocketJumpFiresOnlyWhenArmedAndGrounded();
	CheckSwimClimbsTowardTheTarget();
	CheckStanceStepAndHolds();
	CheckLocalPointIsPlainSteering();
	CheckHookAimResolves();
	CheckHookFiresRidesAndReleases();
	CheckRejectsBadInputs();
	if (failures != 0)
		return 1;
	puts("sg_tactic_controller_test: ok");
	return 0;
}
