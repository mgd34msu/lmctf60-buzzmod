/* sg_rune_topology.h -- flood-contact consistency for completed RUNE graphs. */
#pragma once

#include "sg_rune.h"

#include <stddef.h>
#include <stdint.h>

typedef enum sg_rune_contact_provenance_e
{
	SG_RUNE_CONTACT_FLOOD_CHILD = 1U,
	SG_RUNE_CONTACT_FLOOD_MEETING = 2U,
	SG_RUNE_CONTACT_BSP_OVERLAY = 4U
} sg_rune_contact_provenance_t;

/* Larger values are more restrictive when observations share a seed pair. */
typedef enum sg_rune_contact_kind_e
{
	SG_RUNE_CONTACT_STATIC_DRY,
	SG_RUNE_CONTACT_DYNAMIC_OR_UNKNOWN,
	SG_RUNE_CONTACT_WATER_BOUNDARY,
	SG_RUNE_CONTACT_DECLARED_MOVER
} sg_rune_contact_kind_t;

typedef struct sg_rune_collision_contact_s
{
	int low_seed;
	int high_seed;
	uint8_t provenance;
	uint8_t kind;
} sg_rune_collision_contact_t;

typedef struct sg_rune_contact_ledger_s
{
	sg_rune_collision_contact_t *contacts;
	uint32_t contact_count;
	uint32_t contact_capacity;
	uint32_t *slots;
	uint32_t slot_capacity;
	int overflow;
} sg_rune_contact_ledger_t;

typedef enum sg_rune_topology_status_e
{
	SG_RUNE_TOPOLOGY_OK,
	SG_RUNE_TOPOLOGY_INVALID,
	SG_RUNE_TOPOLOGY_CAPACITY,
	SG_RUNE_TOPOLOGY_NO_MEMORY,
	SG_RUNE_TOPOLOGY_OWNER_FATAL
} sg_rune_topology_status_t;

typedef enum sg_rune_topology_proof_status_e
{
	SG_RUNE_TOPOLOGY_ADDED,
	SG_RUNE_TOPOLOGY_PRESENT,
	SG_RUNE_TOPOLOGY_REJECTED,
	SG_RUNE_TOPOLOGY_DEFERRED,
	SG_RUNE_TOPOLOGY_FATAL
} sg_rune_topology_proof_status_t;

typedef struct sg_rune_topology_proof_result_s
{
	sg_rune_topology_proof_status_t status;
	uint16_t owner;
	uint16_t reason;
} sg_rune_topology_proof_result_t;

typedef struct sg_rune_topology_graph_s
{
	const rune_seed_t *seeds;
	uint32_t seed_count;
	rune_link_t *links;
	int *link_count;
	uint32_t link_capacity;
} sg_rune_topology_graph_t;

typedef struct sg_rune_topology_snapshot_s
{
	int *components;
	uint32_t component_capacity;
	uint32_t seed_count;
	uint32_t link_count;
	uint64_t graph_identity;
	uint32_t component_count;
} sg_rune_topology_snapshot_t;

typedef sg_rune_topology_proof_result_t (*sg_rune_topology_prove_fn)(
	void *context, const sg_rune_collision_contact_t *contact,
	int from, int to);

typedef struct sg_rune_topology_proof_ops_s
{
	sg_rune_topology_prove_fn prove;
	void *context;
} sg_rune_topology_proof_ops_t;

typedef struct sg_rune_topology_outcome_s
{
	int from;
	int to;
	int initial_from_scc;
	int initial_to_scc;
	uint32_t first_link;
	uint32_t links_added;
	uint16_t owner;
	uint16_t reason;
	uint8_t contact_kind;
	uint8_t provenance;
	uint8_t result;
	uint8_t final_same_scc;
} sg_rune_topology_outcome_t;

typedef struct sg_rune_topology_report_s
{
	sg_rune_topology_outcome_t *outcomes;
	sg_rune_topology_snapshot_t *final_snapshot;
	uint32_t outcome_capacity;
	uint32_t outcome_count;
	uint32_t contacts;
	uint32_t crossing_contacts;
	uint32_t crossing_directions;
	uint32_t proof_calls;
	uint32_t edges_added;
	uint32_t rejected;
	uint32_t deferred;
	uint32_t unresolved;
	uint32_t initial_sccs;
	uint32_t final_sccs;
	uint32_t scc_builds;
} sg_rune_topology_report_t;

typedef void *(*sg_rune_topology_allocate_fn)(size_t bytes);
typedef void (*sg_rune_topology_release_fn)(void *allocation);

sg_rune_topology_status_t SG_RuneTopologyGraphIdentity(
	const sg_rune_topology_graph_t *graph, uint64_t *identity_out);

sg_rune_topology_status_t SG_RuneTopologySnapshotBuild(
	const sg_rune_topology_graph_t *graph,
	sg_rune_topology_snapshot_t *snapshot,
	sg_rune_topology_allocate_fn allocate,
	sg_rune_topology_release_fn release);

int SG_RuneTopologySnapshotCurrent(const sg_rune_topology_graph_t *graph,
	const sg_rune_topology_snapshot_t *snapshot);

sg_rune_topology_status_t SG_RuneTopologyLedgerInit(
	sg_rune_contact_ledger_t *ledger,
	sg_rune_collision_contact_t *contacts, uint32_t contact_capacity,
	uint32_t *slots, uint32_t slot_capacity);

sg_rune_topology_status_t SG_RuneTopologyRecordContact(
	sg_rune_contact_ledger_t *ledger, int first, int second,
	sg_rune_contact_provenance_t provenance,
	sg_rune_contact_kind_t kind);

sg_rune_topology_status_t SG_RuneTopologyReconcile(
	const sg_rune_contact_ledger_t *ledger,
	const sg_rune_topology_graph_t *graph,
	const sg_rune_topology_proof_ops_t *proof_ops,
	sg_rune_topology_report_t *report,
	sg_rune_topology_allocate_fn allocate,
	sg_rune_topology_release_fn release);
