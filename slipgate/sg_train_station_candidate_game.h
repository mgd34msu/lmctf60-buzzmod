#ifndef SG_TRAIN_STATION_CANDIDATE_GAME_H
#define SG_TRAIN_STATION_CANDIDATE_GAME_H

#include "sg_train_station_candidate.h"

typedef int (*sg_train_station_connected_fn)(void *context, int seed);
typedef rune_link_t *(*sg_train_station_append_fn)(void *context, int from,
	int to, int cost_ms);

typedef struct sg_train_station_candidate_game_diagnostics_s
{
	uint32_t candidates;
	uint32_t directions;
	uint32_t source_candidates;
	uint32_t board_attempts;
	uint32_t board_successes;
	uint32_t carry_attempts;
	uint32_t carry_successes;
	uint32_t egress_attempts;
	uint32_t egress_successes;
	uint32_t appended;
} sg_train_station_candidate_game_diagnostics_t;

typedef struct sg_train_station_candidate_game_request_s
{
	const sg_mech_catalog_view_t *catalog;
	const rune_seed_t *seeds;
	int num_seeds;
	const rune_link_t *links;
	int num_links;
	const byte *source_stable;
	const byte *source_waterlevel;
	sg_train_station_connected_fn has_incoming;
	sg_train_station_connected_fn has_outgoing;
	sg_train_station_append_fn append;
	sg_mechanism_plan_binding_t *bindings;
	uint32_t *num_bindings;
	uint32_t binding_capacity;
	void *context;
} sg_train_station_candidate_game_request_t;

int SG_TrainStationCandidateGameGenerate(
	const sg_train_station_candidate_game_request_t *request);

const sg_train_station_candidate_game_diagnostics_t *
SG_TrainStationCandidateGameLastDiagnostics(void);

#endif
