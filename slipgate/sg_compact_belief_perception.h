/* Opaque host evidence into compact-native sparse runtime beliefs. */
#ifndef SG_COMPACT_BELIEF_PERCEPTION_H
#define SG_COMPACT_BELIEF_PERCEPTION_H

#include <stdint.h>

#include "sg_belief_runtime.h"

typedef enum sg_compact_belief_perception_result_e
{
	SG_COMPACT_BELIEF_PERCEPTION_APPLIED = 0,
	SG_COMPACT_BELIEF_PERCEPTION_UNAVAILABLE,
	SG_COMPACT_BELIEF_PERCEPTION_IDENTITY_MISMATCH,
	SG_COMPACT_BELIEF_PERCEPTION_REJECTED,
	SG_COMPACT_BELIEF_PERCEPTION_CAPACITY,
	SG_COMPACT_BELIEF_PERCEPTION_OVERFLOW
} sg_compact_belief_perception_result_t;

/* The host creates and owns this token. Its representation remains private so
 * gameplay code cannot manufacture sight, sound, damage, or teammate-report
 * authority by filling a public payload. */
typedef struct sg_compact_belief_perception_evidence_authority_s
	sg_compact_belief_perception_evidence_authority_t;

/* The trusted host owner has a private decoder registry in the implementation.
 * Gameplay code can retain this state only to submit an opaque token; it has
 * no public decoder callback, context pointer, or binding constructor. */
typedef struct sg_compact_belief_perception_binding_s
{
	const sg_rune_compact_model_t *model;
	const sg_rune_compact_identity_t *identity;
	uint64_t rune_identity;
	uint64_t topology_revision;
	uint64_t generation;
	uint32_t owner_slot;
	uint8_t bound;
	uint8_t reserved[3];
} sg_compact_belief_perception_binding_t;

void SG_CompactBeliefPerceptionUnbind(
	sg_compact_belief_perception_binding_t *binding);
int SG_CompactBeliefPerceptionBindingCurrent(
	const sg_compact_belief_perception_binding_t *binding);

sg_compact_belief_perception_result_t SG_CompactBeliefPerceptionObserve(
	const sg_compact_belief_perception_binding_t *binding,
	const sg_compact_belief_perception_evidence_authority_t *authority);
sg_compact_belief_perception_result_t SG_CompactBeliefPerceptionObserveSound(
	const sg_compact_belief_perception_binding_t *binding,
	const sg_compact_belief_perception_evidence_authority_t *authority);
sg_compact_belief_perception_result_t SG_CompactBeliefPerceptionObserveDamage(
	const sg_compact_belief_perception_binding_t *binding,
	const sg_compact_belief_perception_evidence_authority_t *authority);
sg_compact_belief_perception_result_t SG_CompactBeliefPerceptionFrame(
	const sg_compact_belief_perception_binding_t *binding,
	uint64_t frame_sequence, uint64_t at_ms);
const sg_belief_runtime_view_t *SG_CompactBeliefPerceptionView(
	const sg_compact_belief_perception_binding_t *binding,
	uint8_t audience_team, const sg_belief_runtime_life_t *target_life,
	uint64_t at_ms);
const sg_belief_runtime_view_t *SG_CompactBeliefPerceptionViewForClient(
	const sg_compact_belief_perception_binding_t *binding,
	uint8_t audience_team, uint32_t client_id, uint64_t at_ms);

#endif /* SG_COMPACT_BELIEF_PERCEPTION_H */
