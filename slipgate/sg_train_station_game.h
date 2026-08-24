/* Passive live adapter for authenticated continuous station trains. */
#ifndef SG_TRAIN_STATION_GAME_H
#define SG_TRAIN_STATION_GAME_H

#include <stdint.h>

#include "sg_train_station_board_path.h"
#include "sg_train_station_transaction.h"

struct rune_s;
struct sg_bot_s;

typedef struct sg_train_station_game_state_s
{
	sg_train_station_state_t transaction;
	const struct rune_s *rune;
	float source[3];
	float approach[3];
	float boarding[3];
	float destination[3];
	uint32_t route_keys[SG_TRAIN_STATION_ROUTE_CORNERS];
	uint32_t link_index;
	uint32_t master_key;
	uint32_t member_key;
	uint32_t ride_key;
	uint32_t master_generation;
	uint32_t member_generation;
	sg_train_station_board_path_t approach_path;
	sg_train_station_board_path_t boarding_path;
	uint8_t active;
	uint8_t approach_reached;
	uint8_t boarding_path_ready;
} sg_train_station_game_state_t;

int SG_TrainStationGameEmit(struct sg_bot_s *bot, int selected_link);
int SG_TrainStationGameOwns(const struct sg_bot_s *bot);
void SG_TrainStationGameReset(struct sg_bot_s *bot);

#endif /* SG_TRAIN_STATION_GAME_H */
