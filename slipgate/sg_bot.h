/* sg_bot.h -- one bot's process-side state.
 *
 * The bot's knowledge of the map is the RUNE; its per-frame reasoning is
 * the era-4 runtime (locate, field, step, executor).  What lives here is
 * the little that persists between frames: which edict, which cell, which
 * destination, the step in progress, the role, and the hook. */
#ifndef SG_BOT_H
#define SG_BOT_H

#include <stdint.h>

#include "sg_rune_field.h"

#define SG_MAXBOTS 16

#define SG_BOT_AVOID 16

typedef struct sg_bot_s
{
	edict_t *ent;
	qboolean active;
	unsigned long long instance_token;

	/* Where the body is in the RUNE. */
	uint32_t cell;              /* SG_RUNE_CX_INDEX_NONE when unknown */
	uint8_t crouching;
	uint8_t airborne;
	uint8_t reserved[2];

	/* Where it is going and what it is doing about it. */
	uint32_t destination_cell;
	float destination[3];
	sg_rune_step_t step;
	uint32_t flight_capability; /* the flight in progress, or INDEX_NONE */
	float flight_landing[3];
	float stuck_since;
	float stuck_origin[3];
	uint32_t task_mechanism;    /* the mechanism being worked, or INDEX_NONE */
	float task_since;
	float shoot_point[3];       /* something to shoot (a door, a button) */
	uint8_t shoot_point_present;
	uint8_t reserved2[3];

	/* Role. */
	int role;
	int last_role;
	qboolean was_carrying;
	qboolean def_stand;
	qboolean death_taught;
	float carry_start;

	/* Crossings that failed on this body lately: not taken again for a
	 * while, the route goes around them. */
	uint32_t avoid[SG_BOT_AVOID];
	float avoid_until[SG_BOT_AVOID];
	float ride_since;           /* level.time the rope attached */
	float ride_nearest;         /* the closest the body has come to the bite this ride */
	float hang_since;           /* level.time the rope first held the body at the bite */
	float rope_fired_at;        /* level.time the rope was fired, for the rope log */
	float rope_bit_at;
	uint8_t rope_state_logged;  /* the hookstate the rope log last saw */
	uint8_t release_held_logged; /* a held release was logged for this ride */
	float stall_logged_at;      /* the last stalled-rope line, for rate limiting */
	float dodge_until;          /* when the strafe across the enemy's line reverses */
	int8_t dodge_sign;          /* which way it goes now: +1 or -1 */
	float dodge_hop_at;         /* the last hop with a reversal */
	uint8_t footwork;           /* this frame's movement is idle footwork: the view need not follow */
	uint8_t logged_status;      /* the last command, for the log line */
	float logged_direction[3];
	float logged_speed;
	float logged_up;
	uint32_t fired_capability;  /* the ride the rope was last fired for */
	uint8_t fired_bit;          /* that rope bit */
	uint32_t flight_from;       /* the floor cell the current flight left */
	uint8_t rescue;             /* the rope is out to save a fall into harm */
	uint8_t rescue_spent;       /* ropes fired on this fall, up to RESCUE_TRIES */
	float rescue_anchor[3];
	float rescue_failed[3];     /* the last bite a rescue rope stalled on */
	const struct edict_s *detour_logged; /* the last item detour written to the log */
	float patrol_point[3];      /* a posted defender's pickup, while it stands */
	float patrol_until;         /* level.time the patrol is given up */
	uint8_t patrolling;
	uint8_t logged_kind, logged_move; /* the last decision written to the log */
	uint32_t logged_capability;
	float logged_target[3];
	float ride_origin[3];       /* where the body was then */

	/* A defender's post: its cell (the destination is that cell, not a
	 * point re-resolved), and where to look from it when nothing is in sight. */
	uint32_t post_cell;
	float post_facing[3];
	qboolean post_facing_valid;

	/* Hook and combat hand-off. */
	int hook_phase;             /* 0 none, 2 rope out, 3 released */
	edict_t *hook_entity;
	qboolean engaged_last;
	int lives;
	qboolean was_dead;
} sg_bot_t;

extern sg_bot_t sg_bots[SG_MAXBOTS];

/* The bot driver (sg_bot_frame.c). */
void SG_BotThink(sg_bot_t *bot);
void SG_BotSlotInit(sg_bot_t *bot);

#endif /* SG_BOT_H */
