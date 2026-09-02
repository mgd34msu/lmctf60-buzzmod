/* The executor: one step and one body in, one command out.
 *
 * The field says which crossing to take and how (the step's capability
 * kind and launch); this turns that into what the body presses this frame
 * under the host's movement law.  Stateless per frame. */
#ifndef SG_TACTIC_CONTROLLER_H
#define SG_TACTIC_CONTROLLER_H

#include <stdint.h>

#include "sg_host_hook_law.h"
#include "sg_rune_field.h"

typedef enum sg_tactic_command_status_e
{
	SG_TACTIC_COMMAND_MOVE = 0,     /* a body command was produced */
	SG_TACTIC_COMMAND_HOLD,         /* nothing to do this frame */
	SG_TACTIC_COMMAND_UNSUPPORTED,  /* this executor has no law for it yet */
	SG_TACTIC_COMMAND_STATUS_COUNT
} sg_tactic_command_status_t;

typedef struct sg_tactic_body_s
{
	float origin[3];
	float velocity[3];
	float view_height;        /* eye above origin; the hook leaves from here */
	uint8_t supported;
	uint8_t waterlevel;
	uint8_t crouched;
	sg_host_hook_phase_t hook_phase;
	uint8_t launcher_ready;   /* rocket launcher in hand, ready, loaded */
	uint8_t hook_ready;       /* the offhand hook may be fired now */
	float gravity;
	uint32_t frame_ms;
	uint32_t substep_ms;
} sg_tactic_body_t;

typedef struct sg_tactic_command_s
{
	sg_tactic_command_status_t status;
	float direction[3];       /* unit world direction to move, when speed > 0 */
	float speed;              /* 0..1 of the engine's command range */
	float up;                 /* -1 duck, 0 neutral, 1 jump or swim up */
	float yaw;                /* degrees, valid when aim_owned */
	float pitch;              /* degrees, positive down, valid when aim_owned */
	uint8_t aim_owned;        /* the capability needs the view this frame */
	uint8_t attack;
	uint8_t hook_fire;
	uint8_t hook_release;
	uint8_t want_launcher;    /* ask for the rocket launcher in hand */
	uint8_t reserved[3];
} sg_tactic_command_t;

int SG_TacticControl(const sg_rune_step_t *step, const sg_tactic_body_t *body,
	sg_tactic_command_t *command_out);

const char *SG_TacticCommandStatusString(sg_tactic_command_status_t status);

#endif /* SG_TACTIC_CONTROLLER_H */
