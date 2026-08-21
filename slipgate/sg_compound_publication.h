/* Atomic exact replay state for authenticated compound actions. */
#ifndef SG_COMPOUND_PUBLICATION_H
#define SG_COMPOUND_PUBLICATION_H

#include <stddef.h>
#include <stdint.h>

#include "sg_compound_world.h"
#include "sg_replay.h"
#include "sg_rune.h"

#define SG_COMPOUND_PUBLICATION_INDEX_NONE UINT32_MAX

typedef struct sg_compound_publication_checkpoint_s
{
	pmove_state_t pms;
	pmove_state_t old_pms;
	qboolean grounded;
	int watertype;
	int waterlevel;
	float old_frame_z;
} sg_compound_publication_checkpoint_t;

/* Client view-angle offsets are allowed to differ from the offline replay by
 * one modular signed-short bias.  The same bias must exist in pms and the
 * snapinitial comparison state, and must still hold at the suffix checkpoint. */
typedef struct sg_compound_publication_angle_bias_s
{
	short axis[3];
} sg_compound_publication_angle_bias_t;

typedef struct sg_compound_hook_publication_proof_s
{
	sg_hook_replay_spec_t spec;
} sg_compound_hook_publication_proof_t;

typedef struct sg_compound_publication_binding_s
{
	uint32_t link_index;
	rune_link_t link;
	rune_seed_t source_seed;
	rune_seed_t destination_seed;
	uint32_t mechanism_index;
	float canonical_hint[3];
	sg_compound_publication_checkpoint_t source;
	sg_compound_publication_checkpoint_t suffix;
	int touch_ms;
	int touch_frame_end_ms;
	int mover_top_ms;
	int suffix_start_ms;
	int arrival_ms;
	int sweep_clear_ms;
	int total_cost_ms;
	sg_compound_hook_publication_proof_t hook_proof;
} sg_compound_publication_binding_t;

typedef enum sg_compound_publication_status_e
{
	SG_COMPOUND_PUBLICATION_OK = 0,
	SG_COMPOUND_PUBLICATION_INVALID,
	SG_COMPOUND_PUBLICATION_ALLOCATION,
	SG_COMPOUND_PUBLICATION_MECHANISM,
	SG_COMPOUND_PUBLICATION_SOURCE,
	SG_COMPOUND_PUBLICATION_REPLAY,
	SG_COMPOUND_PUBLICATION_MISMATCH,
	SG_COMPOUND_PUBLICATION_WORLD_DRIFT
} sg_compound_publication_status_t;

typedef struct sg_compound_publication_result_s
{
	sg_compound_publication_status_t status;
	rune_reject_reason_t reason;
	uint32_t link_index;
} sg_compound_publication_result_t;

typedef void *(*sg_compound_publication_alloc_fn)(int size);
typedef void (*sg_compound_publication_free_fn)(void *block);

typedef struct sg_compound_publication_s sg_compound_publication_t;

sg_compound_publication_result_t SG_CompoundPublicationBuild(
	const rune_t *rune, sg_compound_publication_alloc_fn allocate,
	sg_compound_publication_free_fn deallocate,
	sg_compound_publication_t **publication_out);
void SG_CompoundPublicationDestroy(sg_compound_publication_t *publication);

/* Re-resolve every exact contact against the current world.  Call this at the
 * final publication boundary, after all fallible candidate construction. */
sg_compound_publication_result_t SG_CompoundPublicationRevalidate(
	const rune_t *rune);

size_t SG_CompoundPublicationCount(const rune_t *rune);
const sg_compound_publication_binding_t *SG_CompoundPublicationBinding(
	const rune_t *rune, uint32_t link_index);
const sg_compound_world_preopen_t *SG_CompoundPublicationMechanism(
	const rune_t *rune,
	const sg_compound_publication_binding_t *binding);

int SG_CompoundPublicationCaptureAngleBias(
	const sg_compound_publication_checkpoint_t *expected,
	const sg_compound_publication_checkpoint_t *live,
	sg_compound_publication_angle_bias_t *bias_out);
int SG_CompoundPublicationCheckpointMatches(
	const sg_compound_publication_checkpoint_t *expected,
	const sg_compound_publication_checkpoint_t *live,
	const sg_compound_publication_angle_bias_t *bias);

const char *SG_CompoundPublicationStatusName(
	sg_compound_publication_status_t status);

#endif /* SG_COMPOUND_PUBLICATION_H */
