/* Strict, source-bound human route nominations for later exact proof. */
#ifndef SG_RUNE_LEARNING_H
#define SG_RUNE_LEARNING_H

#include <stdint.h>

#include "sg_rune.h"

#define SG_RUNE_LEARNING_MAX_CANDIDATES 4096U
#define SG_RUNE_LEARNING_MAX_HOOK_CANDIDATES 512U
#define SG_RUNE_LEARNING_MAX_HOOKS_PER_PAIR 8U

typedef enum sg_rune_learning_hint_e
{
	SG_RUNE_LEARNING_DRY_RUN_WAYPOINT = 1
} sg_rune_learning_hint_t;

typedef struct sg_rune_learning_candidate_s
{
	uint32_t source_from;
	uint32_t source_to;
	int32_t from_origin_q8[3];
	int32_t to_origin_q8[3];
	int32_t waypoint_q8[3];
	uint64_t first_sequence;
	uint64_t last_sequence;
	uint8_t has_waypoint;
	uint8_t hint;
} sg_rune_learning_candidate_t;

typedef struct sg_rune_learning_hook_candidate_s
{
	uint32_t source_from;
	uint32_t source_to;
	int32_t from_origin_q8[3];
	int32_t to_origin_q8[3];
	int16_t aim_short[2][2];
	int32_t bite_q8[2][3];
	uint8_t rope_count;
} sg_rune_learning_hook_candidate_t;

typedef struct sg_rune_learning_storage_s
{
	sg_rune_learning_candidate_t *runs;
	uint32_t run_capacity;
	sg_rune_learning_hook_candidate_t *hooks;
	uint32_t hook_capacity;
} sg_rune_learning_storage_t;

typedef struct sg_rune_learning_evidence_s
{
	char source_rune_sha256[65];
	char trace_sha256[65];
	char replay_sha256[65];
	uint32_t candidate_count;
	sg_rune_learning_candidate_t *candidates;
	uint32_t hook_candidate_count;
	sg_rune_learning_hook_candidate_t *hook_candidates;
} sg_rune_learning_evidence_t;

typedef enum sg_rune_learning_load_status_e
{
	SG_RUNE_LEARNING_READY,
	SG_RUNE_LEARNING_MISSING,
	SG_RUNE_LEARNING_REJECTED
} sg_rune_learning_load_status_t;

sg_rune_learning_load_status_t SG_RuneLearningLoadFile(
	const rune_t *source, const char *path,
	const sg_rune_learning_storage_t *storage,
	sg_rune_learning_evidence_t *out);

#endif /* SG_RUNE_LEARNING_H */
