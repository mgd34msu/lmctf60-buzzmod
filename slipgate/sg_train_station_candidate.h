#ifndef SG_TRAIN_STATION_CANDIDATE_H
#define SG_TRAIN_STATION_CANDIDATE_H

#include "sg_rune_mechanism_plan.h"
#include "sg_train_station_plan.h"

#include <stdint.h>

#define SG_TRAIN_STATION_DIRECTIONS 2U
#define SG_TRAIN_STATION_DWELL_MS 3000U

typedef struct sg_train_station_direction_s
{
	uint32_t source_station_key;
	uint32_t destination_station_key;
	uint32_t ride_train_key;
	uint32_t source_dwell_ms;
	uint32_t destination_dwell_ms;
} sg_train_station_direction_t;

typedef struct sg_train_station_candidate_s
{
	sg_mechanism_plan_binding_t binding;
	sg_train_station_plan_witness_t witness;
	sg_train_station_direction_t directions[SG_TRAIN_STATION_DIRECTIONS];
} sg_train_station_candidate_t;

uint32_t SG_TrainStationCandidatesDiscover(
	const sg_mech_catalog_view_t *catalog,
	sg_train_station_candidate_t *candidates, uint32_t capacity);

#endif
