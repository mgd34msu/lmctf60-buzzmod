/* The executor under each step kind: which direction, which presses, when. */
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "../slipgate/sg_rune_law.h"
#include "../slipgate/sg_tactic_controller.h"

static int failures;

#define CHECK(condition) \
	do { if (!(condition)) { failures++; \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (0)

static int Near(float a, float b)
{
	return fabsf(a - b) < 1e-3f;
}

static sg_tactic_body_t Body(void)
{
	static sg_rune_law_t law;
	sg_tactic_body_t body;

	memset(&body, 0, sizeof(body));
	SG_RuneLawEngine(&law, 800.0f);
	body.law = &law;
	body.supported = 1U;
	body.view_height = 22.0f;
	body.gravity = 800.0f;
	body.frame_ms = 100U;
	body.substep_ms = 25U;
	body.launcher_ready = 1U;
	body.hook_ready = 1U;
	return body;
}

static sg_rune_step_t Cross(sg_rune_move_kind_t kind, float x, float y,
	float z)
{
	sg_rune_step_t step;

	memset(&step, 0, sizeof(step));
	step.kind = SG_RUNE_STEP_CROSS;
	step.move_kind = (uint8_t)kind;
	step.target[0] = x;
	step.target[1] = y;
	step.target[2] = z;
	return step;
}

int main(void)
{
	sg_tactic_body_t body = Body();
	sg_tactic_command_t command;
	sg_rune_step_t step;

	/* Walk: full speed along the horizontal direction to the target. */
	step = Cross(SG_RUNE_MOVE_WALK, 60.0f, 80.0f, 10.0f);
	CHECK(SG_TacticControl(&step, &body, &command));
	CHECK(command.status == SG_TACTIC_COMMAND_MOVE);
	CHECK(Near(command.direction[0], 0.6f) && Near(command.direction[1], 0.8f));
	CHECK(Near(command.speed, 1.0f) && Near(command.up, 0.0f));
	CHECK(command.aim_owned == 0U && command.attack == 0U);

	/* Crouch: ducks while moving; a standing body asked to arrive crouched
	 * ducks too. */
	step = Cross(SG_RUNE_MOVE_CROUCH, 100.0f, 0.0f, 0.0f);
	CHECK(SG_TacticControl(&step, &body, &command));
	CHECK(Near(command.up, -1.0f) && Near(command.direction[0], 1.0f));
	step = Cross(SG_RUNE_MOVE_WALK, 100.0f, 0.0f, 0.0f);
	step.crouching_next = 1U;
	CHECK(SG_TacticControl(&step, &body, &command));
	CHECK(Near(command.up, -1.0f));

	/* Jump: run while far, press on the frame that reaches the portal at
	 * speed, never from a standing start, never in the air. */
	step = Cross(SG_RUNE_MOVE_JUMP, 500.0f, 0.0f, 0.0f);
	CHECK(SG_TacticControl(&step, &body, &command));
	CHECK(command.status == SG_TACTIC_COMMAND_MOVE && Near(command.up, 0.0f));
	step = Cross(SG_RUNE_MOVE_JUMP, 100.0f, 0.0f, 0.0f);
	CHECK(SG_TacticControl(&step, &body, &command));
	CHECK(command.status == SG_TACTIC_COMMAND_MOVE && Near(command.up, 0.0f));
	step = Cross(SG_RUNE_MOVE_JUMP, 30.0f, 0.0f, 0.0f);
	body.velocity[0] = 300.0f;
	CHECK(SG_TacticControl(&step, &body, &command));
	CHECK(command.status == SG_TACTIC_COMMAND_MOVE && Near(command.up, 1.0f));
	body.velocity[0] = 0.0f;
	body.supported = 0U;
	CHECK(SG_TacticControl(&step, &body, &command));
	CHECK(command.status == SG_TACTIC_COMMAND_MOVE && Near(command.up, 0.0f));
	body.supported = 1U;

	/* Rocket jump: ask for the launcher and close in; then, within reach on
	 * the ground, own the aim straight down and fire with the jump. */
	step = Cross(SG_RUNE_MOVE_ROCKET_JUMP, 0.0f, 900.0f, 0.0f);
	body.launcher_ready = 0U;
	CHECK(SG_TacticControl(&step, &body, &command));
	CHECK(command.want_launcher == 1U && command.attack == 0U);
	CHECK(command.status == SG_TACTIC_COMMAND_MOVE && Near(command.speed, 1.0f));
	body.launcher_ready = 1U;
	step = Cross(SG_RUNE_MOVE_ROCKET_JUMP, 0.0f, 200.0f, 0.0f);
	CHECK(SG_TacticControl(&step, &body, &command));
	CHECK(command.attack == 1U && Near(command.up, 1.0f));
	CHECK(command.aim_owned == 1U && Near(command.pitch, 90.0f));
	CHECK(Near(command.yaw, 90.0f));
	body.supported = 0U;
	CHECK(SG_TacticControl(&step, &body, &command));
	CHECK(command.attack == 0U && command.aim_owned == 0U);
	body.supported = 1U;

	/* Swim: the up component follows the rise. */
	step = Cross(SG_RUNE_MOVE_SWIM, 60.0f, 0.0f, 80.0f);
	CHECK(SG_TacticControl(&step, &body, &command));
	CHECK(command.status == SG_TACTIC_COMMAND_MOVE);
	CHECK(Near(command.up, 0.8f) && Near(command.direction[0], 1.0f));

	/* Hook: idle with a bite point fires with the aim owned; attached and
	 * at the crossing releases. */
	step = Cross(SG_RUNE_MOVE_HOOK, 300.0f, 0.0f, 0.0f);
	step.hook_point_present = 1U;
	step.hook_point[0] = 300.0f;
	step.hook_point[2] = 200.0f;
	CHECK(SG_TacticControl(&step, &body, &command));
	CHECK(command.hook_fire == 1U && command.aim_owned == 1U);
	CHECK(command.pitch < 0.0f);
	body.hook_phase = SG_TACTIC_HOOK_ATTACHED;
	body.origin[0] = 290.0f;
	/* Not carried yet: the body still has its running speed alone. */
	CHECK(SG_TacticControl(&step, &body, &command));
	CHECK(command.hook_release == 0U);
	/* Carried by the pull: released. */
	body.velocity[2] = 800.0f;
	CHECK(SG_TacticControl(&step, &body, &command));
	CHECK(command.hook_release == 1U);
	body.velocity[2] = 0.0f;
	body.hook_phase = SG_TACTIC_HOOK_IDLE;
	body.origin[0] = 0.0f;

	/* Arrived eases in; hold and unreachable hold. */
	memset(&step, 0, sizeof(step));
	step.kind = SG_RUNE_STEP_ARRIVED;
	step.target[0] = 16.0f;   /* inside the stand radius: no hunting */
	CHECK(SG_TacticControl(&step, &body, &command));
	CHECK(command.status == SG_TACTIC_COMMAND_HOLD);
	step.target[0] = 48.0f;   /* half the ease distance: half speed */
	CHECK(SG_TacticControl(&step, &body, &command));
	CHECK(command.status == SG_TACTIC_COMMAND_MOVE && Near(command.speed, 0.5f));
	step.kind = SG_RUNE_STEP_UNREACHABLE;
	CHECK(SG_TacticControl(&step, &body, &command));
	CHECK(command.status == SG_TACTIC_COMMAND_HOLD);
	step.kind = SG_RUNE_STEP_HOLD;
	CHECK(SG_TacticControl(&step, &body, &command));
	CHECK(command.status == SG_TACTIC_COMMAND_HOLD && Near(command.speed, 0.0f));

	CHECK(!SG_TacticControl(NULL, &body, &command));
	CHECK(!SG_TacticControl(&step, NULL, &command));
	if (failures)
	{
		fprintf(stderr, "sg_tactic_controller_test: %d failures\n", failures);
		return 1;
	}
	printf("sg_tactic_controller_test: ok\n");
	return 0;
}
