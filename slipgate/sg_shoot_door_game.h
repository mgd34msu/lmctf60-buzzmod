/* sg_shoot_door_game.h -- live adapter for authenticated shootable doors. */
#ifndef SG_SHOOT_DOOR_GAME_H
#define SG_SHOOT_DOOR_GAME_H

#include <stdint.h>

#include "sg_shoot_door_live.h"

#define SG_SHOOT_DOOR_GAME_MAX_MEMBERS 16U

struct edict_s;
struct sg_bot_s;

typedef struct sg_shoot_door_game_state_s
{
	sg_shoot_door_state_t live;
	uint16_t mover_keys[SG_SHOOT_DOOR_GAME_MAX_MEMBERS];
	int16_t destination_q8[3];
	uint8_t mover_count;
	uint8_t shot_count;
	uint8_t guard_owned;
} sg_shoot_door_game_state_t;

int SG_ShootDoorGameEmit(struct sg_bot_s *bot, int selected_link);
int SG_ShootDoorGameOwns(const struct sg_bot_s *bot);
void SG_ShootDoorGameReset(struct sg_bot_s *bot);

/* Tri-state mutation seam: -1 is not a shoot-door event. */
int SG_ShootDoorGameAuthorizeActivation(struct edict_s *source,
	struct edict_s *door_master, struct edict_s *activator);

#endif /* SG_SHOOT_DOOR_GAME_H */
