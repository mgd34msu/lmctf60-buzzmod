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
