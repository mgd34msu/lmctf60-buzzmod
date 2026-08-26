/* sg_compound_gen.h -- pure finite planner for contracted compound links. */
#ifndef SG_COMPOUND_GEN_H
#define SG_COMPOUND_GEN_H

#include <stddef.h>
#include <stdint.h>

#include "sg_rune.h"

/* The caller owns mechanism discovery and supplies only replay-exact
 * candidates. The planner examines every finite supplied candidate. */
#define SG_COMPOUND_GEN_OBJECTIVE_MASK 0x03U

typedef struct sg_compound_gen_seed_s
{
	int component;
	uint8_t objective_mask;
	uint8_t water;
	uint8_t has_incoming;
	uint8_t has_outgoing;
} sg_compound_gen_seed_t;

/* One candidate binds an already-discovered exact PREOPEN contact to one
 * possible suffix destination.  local_rank is an integer, caller-defined
 * proximity rank; lower wins and destination index breaks ties. */
typedef struct sg_compound_gen_candidate_s
{
	int source;
	int destination;
	int trigger_key;
	int mover_key;
	float mechanism_anchor[3];
	uint32_t local_rank;
} sg_compound_gen_candidate_t;

typedef struct sg_compound_gen_proof_s
{
	int touch_ms;
	int touch_frame_end_ms;
	int mover_top_ms;
	int suffix_start_ms;
	int total_cost_ms;
	int arrival_ms;
	int sweep_clear_ms;
	uint8_t exit_speed;
} sg_compound_gen_proof_t;

typedef rune_reject_reason_t (*sg_compound_gen_prove_fn)(void *context,
	const sg_compound_gen_candidate_t *candidate,
	sg_compound_gen_proof_t *proof);

typedef enum sg_compound_gen_status_e
{
	SG_COMPOUND_GEN_OK = 0,
	SG_COMPOUND_GEN_DISABLED,
	SG_COMPOUND_GEN_INVALID,
	SG_COMPOUND_GEN_DUPLICATE,
	SG_COMPOUND_GEN_NO_IMPROVEMENT,
	SG_COMPOUND_GEN_NO_PROOF,
	SG_COMPOUND_GEN_BAD_PROOF,
	SG_COMPOUND_GEN_CAPACITY
} sg_compound_gen_status_t;

typedef struct sg_compound_gen_request_s
{
	const sg_compound_gen_seed_t *seeds;
	size_t seed_count;
	const sg_compound_gen_candidate_t *candidates;
	size_t candidate_count;
	rune_link_t *output;
	size_t output_capacity;
	sg_compound_gen_prove_fn prove;
	void *context;
	int production_enabled;
} sg_compound_gen_request_t;

typedef struct sg_compound_gen_result_s
{
	sg_compound_gen_status_t status;
	size_t selected;
	size_t proof_calls;
	size_t emitted;
} sg_compound_gen_result_t;

sg_compound_gen_result_t SG_CompoundGenPlan(
	const sg_compound_gen_request_t *request);
const char *SG_CompoundGenStatusName(sg_compound_gen_status_t status);

#endif /* SG_COMPOUND_GEN_H */
