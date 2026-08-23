/* sg_train_gate_game.h -- live adapter for sealed two-stop train gates. */
#ifndef SG_TRAIN_GATE_GAME_H
#define SG_TRAIN_GATE_GAME_H

#include <stdint.h>

#include "sg_train_gate_live.h"

struct edict_s;
struct sg_bot_s;

typedef struct sg_train_gate_game_state_s
{
	sg_train_gate_state_t live;
	uint8_t button_touch_count;
	uint8_t button_shot_count;
	uint8_t target_dispatch_count;
	uint8_t train_use_count;
	uint8_t guard_owned;
} sg_train_gate_game_state_t;

int SG_TrainGateGameEmit(struct sg_bot_s *bot, int selected_link);
int SG_TrainGateGameOwns(const struct sg_bot_s *bot);
void SG_TrainGateGameReset(struct sg_bot_s *bot);

/* Tri-state mutation seams: -1 is not a train event, zero denies, one admits. */
int SG_TrainGateGameAuthorizeButtonTouch(struct edict_s *button,
	struct edict_s *activator);
int SG_TrainGateGameAuthorizeButtonUse(struct edict_s *button,
	struct edict_s *activator);
int SG_TrainGateGameAuthorizeButtonShot(struct edict_s *button,
	struct edict_s *inflictor, struct edict_s *attacker, int damage);
int SG_TrainGateGameAuthorizeButtonTargets(struct edict_s *button,
	struct edict_s *activator);
int SG_TrainGateGameHandleTargets(struct edict_s *source,
	struct edict_s *activator);
int SG_TrainGateGameAuthorizeTrainUse(struct edict_s *train,
	struct edict_s *source, struct edict_s *activator);

#endif /* SG_TRAIN_GATE_GAME_H */
