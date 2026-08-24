/* Bounded ranking for physically proved reverse objective boundaries. */
#pragma once

#include "sg_rune_topology.h"

#include <stdint.h>

#define SG_RUNE_REVERSE_BOUNDARY_CAP 32U
#define SG_RUNE_REVERSE_ACTION_COUNT 16U
#define SG_RUNE_REVERSE_PROVENANCE_COUNT 5U

typedef struct sg_rune_reverse_boundary_candidate_s
{
	uint32_t link_index;
	uint16_t from;
	uint16_t to;
	int from_component;
	int to_component;
	uint64_t component_gain;
	float distance_squared;
	uint8_t boundary_action;
} sg_rune_reverse_boundary_candidate_t;

typedef struct sg_rune_reverse_boundary_report_s
{
	uint32_t scanned;
	uint32_t crossing;
	uint32_t rejected_provenance;
	uint32_t rejected_distance;
	uint32_t rejected_endpoints;
	uint32_t invalid_action;
	uint32_t invalid_provenance;
	uint32_t action_counts[SG_RUNE_REVERSE_ACTION_COUNT];
	uint32_t provenance_counts[SG_RUNE_REVERSE_PROVENANCE_COUNT];
	uint32_t unique_ranked_pairs;
	uint32_t ranked;
} sg_rune_reverse_boundary_report_t;

uint32_t SG_RuneReverseBoundaryRank(
	const rune_seed_t *seeds, uint32_t seed_count,
	const rune_link_t *links, uint32_t link_count,
	const int *components, uint32_t component_count,
	uint32_t *component_sizes, uint32_t component_size_capacity,
	const uint8_t *red_reach, const uint8_t *blue_reach,
	float maximum_distance_squared,
	sg_rune_reverse_boundary_candidate_t *candidates,
	uint32_t candidate_capacity,
	sg_rune_reverse_boundary_report_t *report);

sg_rune_topology_status_t SG_RuneReverseBoundaryRankGraph(
	const sg_rune_topology_graph_t *graph,
	const uint8_t *red_reach, const uint8_t *blue_reach,
	float maximum_distance_squared,
	sg_rune_reverse_boundary_candidate_t *candidates,
	uint32_t candidate_capacity, uint32_t *candidate_count_out,
	sg_rune_reverse_boundary_report_t *report,
	sg_rune_topology_allocate_fn allocate,
	sg_rune_topology_release_fn release);
