#ifndef SG_COMPOUND_PUBLICATION_FIXTURE_H
#define SG_COMPOUND_PUBLICATION_FIXTURE_H

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "g_local.h"
#include "slipgate/sg_compound_action_publication.h"
#include "slipgate/sg_compound_publication.h"
#include "slipgate/sg_local.h"

typedef enum fixture_proof_mutation_e
{
	PROOF_VALID = 0,
	PROOF_TOUCH_ZERO,
	PROOF_TOUCH_UNALIGNED,
	PROOF_FRAME_WRONG,
	PROOF_TOP_SHORT,
	PROOF_TOP_UNALIGNED,
	PROOF_SUFFIX_WRONG,
	PROOF_ARRIVAL_UNALIGNED,
	PROOF_CLEAR_ZERO,
	PROOF_CLEAR_AFTER_ARRIVAL,
	PROOF_TOTAL_WRONG,
	PROOF_COST_MISMATCH,
	PROOF_EXIT_MISMATCH,
	PROOF_CLEAR_MISMATCH,
	PROOF_BAD_SOURCE_CHECKPOINT,
	PROOF_BAD_SOURCE_OLD_Z,
	PROOF_BAD_SOURCE_WATER,
	PROOF_BAD_SUFFIX_CHECKPOINT
} fixture_proof_mutation_t;

typedef enum fixture_hook_source_drift_e
{
	HOOK_SOURCE_STABLE = 0,
	HOOK_SOURCE_PMS,
	HOOK_SOURCE_OLD_PMS,
	HOOK_SOURCE_ORIGIN,
	HOOK_SOURCE_VELOCITY,
	HOOK_SOURCE_GROUNDED,
	HOOK_SOURCE_WATERTYPE,
	HOOK_SOURCE_WATERLEVEL,
	HOOK_SOURCE_OLD_FRAME_Z,
	HOOK_SOURCE_DRIFT_COUNT
} fixture_hook_source_drift_t;

typedef struct fixture_s
{
	int allocation_calls;
	int free_calls;
	int live_allocations;
	int fail_allocation_call;
	int resolve_calls;
	int enumerate_calls;
	int source_calls;
	int discover_calls;
	int replay_calls;
	int resolved_member_calls;
	int hint_match_calls;
	int fail_resolve;
	int fail_enumerate;
	int fail_source;
	int fail_discover;
	int fail_replay;
	int world_drift;
	int hint_drift;
	int inconsistent_second_mechanism;
	int hook_bite_drift;
	fixture_hook_source_drift_t hook_source_drift;
	float prepared_old_frame_z;
	fixture_proof_mutation_t proof_mutation;
} fixture_t;

extern fixture_t fixture;
extern int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		        __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

void Set3(float value[3], float x, float y, float z);
rune_link_t CompoundDropLink(int from, int to);
rune_link_t CompoundHookLink(int from, int to);
rune_t RuneFixture(rune_seed_t seeds[3], rune_link_t links[3]);
void ResetFixture(void);
sg_compound_publication_result_t Build(rune_t *rune);
void Destroy(rune_t *rune);
void SG_CompoundPublicationCoreCasesRun(void);
void SG_CompoundHookPublicationCasesRun(void);

#endif
