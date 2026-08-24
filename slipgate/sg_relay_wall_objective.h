#ifndef SG_RELAY_WALL_OBJECTIVE_H
#define SG_RELAY_WALL_OBJECTIVE_H

#include "slipgate/sg_rune_mechanism_plan.h"

typedef struct sg_relay_wall_objective_proof_s
{
	float anchor[3];
	uint32_t cost_ms;
	uint32_t egress_ms;
	uint16_t sweep_clear_ms;
} sg_relay_wall_objective_proof_t;

typedef struct sg_relay_wall_objective_request_s
{
	const sg_mech_catalog_view_t *catalog;
	const rune_seed_t *seeds;
	uint32_t seed_count;
	const int *components;
	const uint8_t *objective_masks;
	void *context;
	int (*eligible)(void *context, uint32_t seed, int source);
	int (*discover)(void *context, const sg_mech_catalog_view_t *catalog,
		uint32_t entry_key, sg_relay_wall_plan_witness_t *witness_out);
	int (*prove)(void *context,
		const sg_relay_wall_plan_witness_t *witness,
		uint32_t source, uint32_t destination,
		sg_relay_wall_objective_proof_t *proof_out);
	int (*publish)(void *context,
		const sg_relay_wall_plan_witness_t *witness,
		uint32_t source, uint32_t destination,
		const sg_relay_wall_objective_proof_t *proof);
} sg_relay_wall_objective_request_t;

typedef struct sg_relay_wall_objective_report_s
{
	uint32_t mechanisms;
	uint32_t candidate_pairs;
	uint32_t proof_attempts;
	uint32_t published;
} sg_relay_wall_objective_report_t;

int SG_RelayWallNodeBounds(const rune_mechanism_node_t *node,
	float mins_out[3], float maxs_out[3]);
double SG_RelayWallNodeDistance2(const rune_seed_t *seed,
	const rune_mechanism_node_t *node);
int SG_RelayWallSourceContactElevation(
	const rune_mechanism_node_t *entry, float origin_z);

/* Returns -1 on malformed input/fatal callback failure, zero when no exact
 * relay-wall bridge was proved, and one after publishing exactly one bridge.
 * The caller rebuilds graph components before asking for another bridge. */
int SG_RelayWallObjectiveBridge(
	const sg_relay_wall_objective_request_t *request,
	sg_relay_wall_objective_report_t *report_out);

#endif /* SG_RELAY_WALL_OBJECTIVE_H */
