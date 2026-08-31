#ifndef SG_COMPACT_BELIEF_PERCEPTION_H
#define SG_COMPACT_BELIEF_PERCEPTION_H

#include <stddef.h>
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

/* The host creates and owns this token.  Its representation is deliberately
 * private: a caller cannot manufacture visibility, sound, or team-report
 * authority by filling in a public payload structure. */
typedef struct sg_compact_belief_perception_evidence_authority_s
	sg_compact_belief_perception_evidence_authority_t;

/* The adapter supplies a consumer so the decoder can keep borrowed spans
 * inside the authority boundary.  The decoder must call consume at most once
 * and must not return until consume has returned. */
typedef int (*sg_compact_belief_perception_observation_consume_fn)(
	void *context, const sg_perception_observation_t *observation);

/* Decode one host-issued token and drive one observation into consume.  The
 * callback is the authority boundary.  It must reject a token unless it binds
 * the exact snapshot, target life, audience, payload, and monotonic evidence
 * sequence.  For sight and sound it must derive PVS/LOS, PHS, attenuation,
 * event kind, direction, occlusion, and known-item constraints from host
 * state; this adapter never sets those proofs.  Borrowed hypothesis spans
 * need only remain valid during the consume call. */
typedef int (*sg_compact_belief_perception_evidence_decode_fn)(
	void *context, const sg_rune_runtime_snapshot_t *snapshot,
	const sg_compact_belief_perception_evidence_authority_t *authority,
	sg_compact_belief_perception_observation_consume_fn consume,
	void *consume_context);

/* The binding is a borrowed, exact identity for one accepted runtime
 * provider.  It owns no RUNE, actor, or player storage. */
typedef struct sg_compact_belief_perception_binding_s
{
	const sg_rune_runtime_snapshot_t *snapshot;
	const sg_rune_model_t *model;
	uint64_t rune_identity;
	uint64_t topology_revision;
	sg_compact_belief_perception_evidence_decode_fn decode_evidence;
	void *decode_context;
	uint8_t bound;
	uint8_t reserved[7];
} sg_compact_belief_perception_binding_t;

sg_compact_belief_perception_result_t
SG_CompactBeliefPerceptionBind(
	sg_compact_belief_perception_binding_t *binding,
	const sg_rune_runtime_snapshot_t *snapshot,
	sg_compact_belief_perception_evidence_decode_fn decode_evidence,
	void *decode_context);
void SG_CompactBeliefPerceptionUnbind(
	sg_compact_belief_perception_binding_t *binding);
int SG_CompactBeliefPerceptionBindingCurrent(
	const sg_compact_belief_perception_binding_t *binding);

/* All observation entry points consume an opaque, host-authenticated token.
 * No entry point accepts caller-built earned sight/sound/damage booleans or
 * hypothesis spans. */
sg_compact_belief_perception_result_t SG_CompactBeliefPerceptionObserve(
	const sg_compact_belief_perception_binding_t *binding,
	const sg_compact_belief_perception_evidence_authority_t *authority);
sg_compact_belief_perception_result_t
SG_CompactBeliefPerceptionObserveSound(
	const sg_compact_belief_perception_binding_t *binding,
	const sg_compact_belief_perception_evidence_authority_t *authority);
sg_compact_belief_perception_result_t
SG_CompactBeliefPerceptionObserveDamage(
	const sg_compact_belief_perception_binding_t *binding,
	const sg_compact_belief_perception_evidence_authority_t *authority);

sg_compact_belief_perception_result_t SG_CompactBeliefPerceptionFrame(
	const sg_compact_belief_perception_binding_t *binding,
	uint64_t frame_sequence, uint64_t at_ms);
sg_compact_belief_perception_result_t SG_CompactBeliefPerceptionPredict(
	const sg_compact_belief_perception_binding_t *binding,
	uint8_t audience_team, const sg_belief_life_identity_t *target_life,
	uint64_t at_ms, sg_belief_particle_t *scratch_first,
	sg_belief_particle_t *scratch_second, size_t scratch_capacity,
	sg_belief_particle_t *particles, size_t particle_capacity,
	sg_belief_prediction_t *out);
const sg_belief_runtime_view_t *SG_CompactBeliefPerceptionView(
	const sg_compact_belief_perception_binding_t *binding,
	uint8_t audience_team, const sg_belief_life_identity_t *target_life);
const sg_belief_runtime_view_t *SG_CompactBeliefPerceptionViewForClient(
	const sg_compact_belief_perception_binding_t *binding,
	uint8_t audience_team, uint32_t client_id);

#endif /* SG_COMPACT_BELIEF_PERCEPTION_H */
