#ifndef SG_WATER_FOREST_H
#define SG_WATER_FOREST_H

#include <stddef.h>
#include <stdint.h>

typedef struct sg_water_proof_s
{
	int cost_ms;
	uint8_t exit_speed;
} sg_water_proof_t;

typedef struct sg_water_edge_s
{
	int from;
	int to;
	sg_water_proof_t proof;
} sg_water_edge_t;

typedef int (*sg_water_prove_fn)(void *context, int from, int to,
	sg_water_proof_t *proof);

typedef enum sg_water_connect_result_e
{
	SG_WATER_CONNECT_INVALID = 0,
	SG_WATER_CONNECT_NO_ROUTE,
	SG_WATER_CONNECT_RECORDED,
	SG_WATER_CONNECT_ALREADY,
	SG_WATER_CONNECT_OVERFLOW
} sg_water_connect_result_t;

typedef struct sg_water_forest_s
{
	int *parents;
	uint8_t *ranks;
	sg_water_edge_t *edges;
	int *edge_slots;
	size_t seed_capacity;
	size_t edge_capacity;
	size_t edge_slot_capacity;
	size_t edge_count;
	int overflow;
} sg_water_forest_t;

int SG_WaterForestInit(sg_water_forest_t *forest, int *parents,
	uint8_t *ranks, size_t seed_capacity, sg_water_edge_t *edges,
	size_t edge_capacity, int *edge_slots, size_t edge_slot_capacity);
sg_water_connect_result_t SG_WaterForestConnect(sg_water_forest_t *forest,
	int from, int to, sg_water_prove_fn prove, void *context);

#endif
