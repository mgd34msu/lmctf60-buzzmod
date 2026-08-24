#ifndef SG_TRAIN_STATION_BOARD_PATH_H
#define SG_TRAIN_STATION_BOARD_PATH_H

#define SG_TRAIN_STATION_BOARD_SIDES 4
#define SG_TRAIN_STATION_BOARD_PATH_POINTS 5

typedef struct sg_train_station_board_path_s
{
	float points[SG_TRAIN_STATION_BOARD_PATH_POINTS][3];
	unsigned count;
	unsigned next;
} sg_train_station_board_path_t;

int SG_TrainStationApproachPathBuild(const float source[3],
	const float anchor[3], sg_train_station_board_path_t *path);

int SG_TrainStationBoardPathBuild(const float source[3],
	const float train_absmin[3], const float train_absmax[3],
	const float interior[3], int side, sg_train_station_board_path_t *path);

int SG_TrainStationBoardPathBuildCanonical(const float source[3],
	const float train_absmin[3], const float train_absmax[3],
	const float interior[3], sg_train_station_board_path_t *path);

int SG_TrainStationBoardPathSideOrder(const float source[3],
	const float train_absmin[3], const float train_absmax[3],
	const float interior[3], int sides[SG_TRAIN_STATION_BOARD_SIDES]);

void SG_TrainStationBoardPathNextTarget(const float current[3],
	sg_train_station_board_path_t *path, float target_out[3]);

#endif
