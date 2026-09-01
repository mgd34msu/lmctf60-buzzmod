/* Trusted host-owner entrance to compact belief perception. */
#ifndef SG_COMPACT_BELIEF_PERCEPTION_OWNER_H
#define SG_COMPACT_BELIEF_PERCEPTION_OWNER_H

#include "sg_compact_belief_perception.h"

typedef int (*sg_compact_belief_perception_observation_consume_fn)(
	void *context, const sg_belief_runtime_observation_t *observation);

typedef int (*sg_compact_belief_perception_evidence_decode_fn)(
	void *context, const sg_belief_runtime_provider_t *provider,
	const sg_compact_belief_perception_evidence_authority_t *authority,
	sg_compact_belief_perception_observation_consume_fn consume,
	void *consume_context);

/* This constructor belongs only to the trusted game-host owner.  Ordinary
 * gameplay consumers receive the opaque binding and evidence APIs from the
 * public header; they cannot register a decoder. */
sg_compact_belief_perception_result_t
SG_CompactBeliefPerceptionBindTrustedOwner(
	sg_compact_belief_perception_binding_t *binding,
	const sg_belief_runtime_provider_t *provider,
	sg_compact_belief_perception_evidence_decode_fn decode_evidence,
	void *decode_context);

#endif /* SG_COMPACT_BELIEF_PERCEPTION_OWNER_H */
