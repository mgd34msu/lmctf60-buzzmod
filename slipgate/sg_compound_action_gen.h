/* Exhaustive topology planner for compound door links. */
#ifndef SG_COMPOUND_ACTION_GEN_H
#define SG_COMPOUND_ACTION_GEN_H

#include <stddef.h>
#include <stdint.h>

#ifndef GAME_INCLUDE
#include "../q_shared.h"
#endif
#include "sg_rune.h"

#define SG_COMPOUND_ACTION_GEN_OBJECTIVE_MASK 0x03U

typedef struct sg_compound_action_gen_seed_s
{
	int component;
	uint8_t objective_mask;
	uint8_t water;
	uint8_t has_incoming;
	uint8_t has_outgoing;
} sg_compound_action_gen_seed_t;

typedef struct sg_compound_action_gen_candidate_s
{
	int source;
	int destination;
	int trigger_key;
	int mover_key;
	float mechanism_anchor[3];
	uint32_t local_rank;
	uint8_t mode;
} sg_compound_action_gen_candidate_t;

/* The exact action oracle supplies all suffix controls. The planner owns only
 * finite topology selection and canonical native-link construction. */
typedef struct sg_compound_action_gen_proof_s
{
	float suffix_anchor[3];
	int touch_ms;
	int touch_frame_end_ms;
	int mover_top_ms;
	int suffix_start_ms;
	int total_cost_ms;
	int arrival_ms;
	int sweep_clear_ms;
	uint8_t heading;
	uint8_t heading_slack;
	uint8_t exit_speed;
} sg_compound_action_gen_proof_t;

typedef rune_reject_reason_t (*sg_compound_action_gen_prove_fn)(void *context,
	int action, const sg_compound_action_gen_candidate_t *candidate,
	sg_compound_action_gen_proof_t *proof);

typedef enum sg_compound_action_gen_status_e
{
	SG_COMPOUND_ACTION_GEN_OK = 0,
	SG_COMPOUND_ACTION_GEN_DISABLED,
	SG_COMPOUND_ACTION_GEN_INVALID,
	SG_COMPOUND_ACTION_GEN_DUPLICATE,
	SG_COMPOUND_ACTION_GEN_NO_IMPROVEMENT,
	SG_COMPOUND_ACTION_GEN_NO_PROOF,
	SG_COMPOUND_ACTION_GEN_BAD_PROOF,
	SG_COMPOUND_ACTION_GEN_CAPACITY
} sg_compound_action_gen_status_t;

typedef struct sg_compound_action_gen_request_s
{
	int action;
	const sg_compound_action_gen_seed_t *seeds;
	size_t seed_count;
	const sg_compound_action_gen_candidate_t *candidates;
	size_t candidate_count;
	rune_link_t *output;
	size_t output_capacity;
	sg_compound_action_gen_prove_fn prove;
	void *context;
	/* Generation remains inert until its action-specific oracle and live
	 * controller are both present. This flag does not change runtime admission. */
	int production_enabled;
} sg_compound_action_gen_request_t;

typedef struct sg_compound_action_gen_result_s
{
	sg_compound_action_gen_status_t status;
	size_t selected;
	size_t proof_calls;
	size_t emitted;
} sg_compound_action_gen_result_t;

sg_compound_action_gen_result_t SG_CompoundActionGenPlan(
	const sg_compound_action_gen_request_t *request);
const char *SG_CompoundActionGenStatusName(
	sg_compound_action_gen_status_t status);

#endif /* SG_COMPOUND_ACTION_GEN_H */
