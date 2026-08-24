#ifndef SG_TRAIN_STATION_PLAN_H
#define SG_TRAIN_STATION_PLAN_H

#include "sg_rune.h"
#include "sg_rune_mechanism_catalog.h"

#include <stdint.h>

#define SG_TRAIN_STATION_ROUTE_CORNERS 14U
#define SG_TRAIN_STATION_PLAN_MAX_EDGES 32U

typedef struct sg_train_station_plan_witness_s
{
	uint32_t edge_indices[SG_TRAIN_STATION_PLAN_MAX_EDGES];
	uint32_t edge_count;
	uint32_t route_keys[SG_TRAIN_STATION_ROUTE_CORNERS];
	uint32_t route_count;
	uint32_t station_keys[2];
} sg_train_station_plan_witness_t;

int SG_TrainStationPlanAuthenticate(const sg_mech_catalog_view_t *catalog,
	uint32_t entry_key, uint32_t mover_key, uint32_t destination_key,
	uint32_t companion_key, sg_train_station_plan_witness_t *witness_out);
int SG_TrainStationPlanDiscover(const sg_mech_catalog_view_t *catalog,
	uint32_t entry_key, uint32_t mover_key, uint32_t *destination_key_out,
	uint32_t *companion_key_out,
	sg_train_station_plan_witness_t *witness_out);

#endif
