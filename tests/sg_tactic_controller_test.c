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

	CHECK(SG_TacticControl(&execution, &body, &command));
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

	CHECK(SG_TacticControl(&execution, &body, &command));
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

	CHECK(SG_TacticControl(&far, &body, &command));
	CHECK(command.status == SG_TACTIC_COMMAND_MOVE && Near(command.up, 0.0f));
	CHECK(SG_TacticControl(&near, &body, &command));
	CHECK(command.status == SG_TACTIC_COMMAND_MOVE && Near(command.up, 1.0f));
	body.supported = 0U;
	CHECK(SG_TacticControl(&near, &body, &command));
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

	CHECK(SG_TacticControl(&execution, &body, &command));
	CHECK(command.want_launcher == 1U && command.attack == 0U);
	CHECK(command.status == SG_TACTIC_COMMAND_MOVE && Near(command.speed, 1.0f));

	body.launcher_ready = 1U;
	CHECK(SG_TacticControl(&execution, &body, &command));
	CHECK(command.attack == 1U && Near(command.up, 1.0f));
	CHECK(command.aim_owned == 1U && Near(command.pitch, 90.0f));
	CHECK(Near(command.yaw, 90.0f));

	body.supported = 0U;
	CHECK(SG_TacticControl(&execution, &body, &command));
	CHECK(command.attack == 0U && command.aim_owned == 0U);

	body.supported = 1U;
	execution.target_point[1] = 5000.0f;
	CHECK(SG_TacticControl(&execution, &body, &command));
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
	CHECK(SG_TacticControl(&execution, &body, &command));
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
	CHECK(SG_TacticControl(&execution, &body, &command));
	CHECK(command.status == SG_TACTIC_COMMAND_MOVE && Near(command.up, -1.0f));
	CHECK(Near(command.speed, 0.0f));

	execution.kind = SG_TACTIC_EXECUTION_BLOCKED_NOW;
	CHECK(SG_TacticControl(&execution, &body, &command));
	CHECK(command.status == SG_TACTIC_COMMAND_HOLD);

	execution = Step(SG_TACTIC_CAPABILITY_WAIT, 100.0f, 0.0f, 0.0f);
	CHECK(SG_TacticControl(&execution, &body, &command));
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
	CHECK(SG_TacticControl(&execution, &body, &command));
	CHECK(command.status == SG_TACTIC_COMMAND_MOVE);
	CHECK(Near(command.direction[0], -1.0f));
}

static void CheckHookIsUnsupportedButApproached(void)
{
	sg_tactic_body_t body = Body();
	sg_tactic_execution_t execution = Step(SG_TACTIC_CAPABILITY_HOOK,
		100.0f, 0.0f, 200.0f);
	sg_tactic_command_t command;

	CHECK(SG_TacticControl(&execution, &body, &command));
	CHECK(command.status == SG_TACTIC_COMMAND_UNSUPPORTED);
	CHECK(Near(command.speed, 1.0f) && command.hook_fire == 0U);
}

static void CheckRejectsBadInputs(void)
{
	sg_tactic_body_t body = Body();
	sg_tactic_execution_t execution = Step(SG_TACTIC_CAPABILITY_WALK,
		100.0f, 0.0f, 0.0f);
	sg_tactic_command_t command;

	CHECK(!SG_TacticControl(NULL, &body, &command));
	CHECK(!SG_TacticControl(&execution, NULL, &command));
	CHECK(!SG_TacticControl(&execution, &body, NULL));
	body.origin[0] = NAN;
	CHECK(!SG_TacticControl(&execution, &body, &command));
	CHECK(command.status == SG_TACTIC_COMMAND_HOLD);
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
	CheckHookIsUnsupportedButApproached();
	CheckRejectsBadInputs();
	if (failures != 0)
		return 1;
	puts("sg_tactic_controller_test: ok");
	return 0;
}
