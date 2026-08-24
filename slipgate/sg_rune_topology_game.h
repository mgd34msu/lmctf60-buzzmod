/* sg_rune_topology_game.h -- generator adapter for flood-contact auditing. */
#pragma once

#include "sg_rune_topology.h"

typedef int (*sg_rune_topology_try_action_fn)(void *context, int from,
	int to, int action);

typedef struct sg_rune_topology_game_request_s
{
	sg_rune_topology_graph_t graph;
	const sg_rune_contact_ledger_t *ledger;
	sg_rune_topology_outcome_t *outcomes;
	uint32_t outcome_capacity;
	sg_rune_topology_snapshot_t *final_snapshot;
	sg_rune_topology_try_action_fn try_action;
	void *context;
	sg_rune_topology_allocate_fn allocate;
	sg_rune_topology_release_fn release;
	void (*print)(const char *format, ...);
} sg_rune_topology_game_request_t;

sg_rune_contact_kind_t SG_RuneTopologyGameContactKind(
	const rune_seed_t *seeds, int first, int second,
	const rune_mechanism_node_t *nodes, uint32_t node_count);

sg_rune_topology_status_t SG_RuneTopologyGameRepair(
	const sg_rune_topology_game_request_t *request,
	sg_rune_topology_report_t *report);

/* OWNER_FATAL leaves the batch unusable; the caller must discard its graph. */
