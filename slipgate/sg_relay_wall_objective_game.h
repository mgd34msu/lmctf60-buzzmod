#ifndef SG_RELAY_WALL_OBJECTIVE_GAME_H
#define SG_RELAY_WALL_OBJECTIVE_GAME_H

#include "slipgate/sg_relay_wall_objective.h"

typedef struct sg_relay_wall_objective_game_request_s
{
	const sg_mech_catalog_view_t *catalog;
	const rune_seed_t *seeds;
	uint32_t seed_count;
	const int *components;
	const uint8_t *objective_masks;
	const uint8_t *source_stable;
	const uint8_t *source_waterlevel;
	const uint8_t *source_watertype;
	rune_link_t *links;
	int *link_count;
	int link_capacity;
	sg_mechanism_plan_binding_t *bindings;
	uint32_t *binding_count;
	uint32_t binding_capacity;
	void *context;
	int (*has_incoming)(void *context, uint32_t seed);
	int (*has_outgoing)(void *context, uint32_t seed);
} sg_relay_wall_objective_game_request_t;

int SG_RelayWallObjectiveGameBridge(
	const sg_relay_wall_objective_game_request_t *request,
	sg_relay_wall_objective_report_t *report_out);

#endif /* SG_RELAY_WALL_OBJECTIVE_GAME_H */
