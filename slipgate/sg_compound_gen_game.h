#ifndef SG_COMPOUND_GEN_GAME_H
#define SG_COMPOUND_GEN_GAME_H

#include "sg_compound_gen.h"
#include "sg_replay.h"

typedef void *(*sg_compound_gen_game_alloc_fn)(int size);
typedef void (*sg_compound_gen_game_free_fn)(void *block);

typedef struct sg_compound_gen_game_topology_s
{
	int *component;
	uint8_t *objective_mask;
} sg_compound_gen_game_topology_t;

void SG_CompoundGenGameTopologyFree(
	sg_compound_gen_game_topology_t *topology,
	sg_compound_gen_game_free_fn deallocate);

typedef struct sg_compound_gen_game_request_s
{
	const rune_seed_t *seeds;
	size_t seed_count;
	rune_link_t *links;
	size_t *link_count;
	size_t link_capacity;
	const int *components;
	const uint8_t *objective_masks;
	sg_compound_gen_game_alloc_fn allocate;
	sg_compound_gen_game_free_fn deallocate;
} sg_compound_gen_game_request_t;

typedef struct sg_compound_gen_game_result_s
{
	sg_compound_gen_status_t status;
	rune_reject_reason_t reason;
	size_t candidates;
	size_t selected;
	size_t proof_calls;
	size_t emitted;
	rune_reject_reason_t proof_rejection;
	size_t proof_rejections;
	sg_replay_reason_t replay_rejection;
	size_t replay_rejections;
} sg_compound_gen_game_result_t;

sg_compound_gen_game_result_t SG_CompoundGenGameBuild(
	const sg_compound_gen_game_request_t *request);
int SG_CompoundGenGameGenerate(const rune_seed_t *seeds, size_t seed_count,
	rune_link_t *links, int *link_count, size_t link_capacity,
	const sg_compound_gen_game_topology_t *topology,
	sg_compound_gen_game_alloc_fn allocate,
	sg_compound_gen_game_free_fn deallocate);
#endif
