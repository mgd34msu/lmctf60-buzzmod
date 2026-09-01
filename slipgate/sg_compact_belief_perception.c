#include "sg_compact_belief_perception.h"

#include <string.h>

/* Deliberately absent from sg_belief_runtime.h.  This module is the only
 * adapter that may submit a decoded raw payload after one opaque-token
 * consumption. */
extern sg_belief_runtime_observe_result_t
	SG_BeliefRuntimeObserveFromCompactOwner(
		const sg_belief_runtime_observation_t *observation);

#define SG_COMPACT_BELIEF_PERCEPTION_MAX_OWNER_BINDINGS 8U

typedef int (*sg_compact_belief_perception_observation_consume_fn)(
	void *context, const sg_belief_runtime_observation_t *observation);

typedef int (*sg_compact_belief_perception_evidence_decode_fn)(
	void *context, const sg_belief_runtime_provider_t *provider,
	const sg_compact_belief_perception_evidence_authority_t *authority,
	sg_compact_belief_perception_observation_consume_fn consume,
	void *consume_context);

sg_compact_belief_perception_result_t
SG_CompactBeliefPerceptionBindTrustedOwner(
	sg_compact_belief_perception_binding_t *binding,
	const sg_belief_runtime_provider_t *provider,
	sg_compact_belief_perception_evidence_decode_fn decode_evidence,
	void *decode_context);

typedef struct sg_compact_belief_perception_owner_binding_s
{
	sg_compact_belief_perception_binding_t *binding;
	sg_compact_belief_perception_evidence_decode_fn decode_evidence;
	void *decode_context;
	uint8_t active;
	uint8_t reserved[7];
} sg_compact_belief_perception_owner_binding_t;

static sg_compact_belief_perception_owner_binding_t
	sg_compact_belief_perception_owner_bindings[
		SG_COMPACT_BELIEF_PERCEPTION_MAX_OWNER_BINDINGS];

typedef struct sg_compact_belief_decoded_observation_s
{
	sg_belief_runtime_observation_t observation;
	sg_belief_runtime_hypothesis_t
		hypotheses[SG_BELIEF_RUNTIME_MAX_PARTICLES];
	sg_belief_runtime_coverage_t
		coverage[SG_BELIEF_RUNTIME_MAX_COVERAGE];
	size_t hypothesis_count;
	size_t coverage_count;
	uint8_t consumed;
	uint8_t invalid;
} sg_compact_belief_decoded_observation_t;

static sg_compact_belief_perception_owner_binding_t *BindingOwner(
	const sg_compact_belief_perception_binding_t *binding)
{
	uint32_t index;
	sg_compact_belief_perception_owner_binding_t *owner;

	if (!binding || binding->owner_slot == 0U || binding->owner_slot >
		SG_COMPACT_BELIEF_PERCEPTION_MAX_OWNER_BINDINGS)
		return NULL;
	index = binding->owner_slot - 1U;
	owner = &sg_compact_belief_perception_owner_bindings[index];
	return owner->active == 1U && owner->binding == binding &&
		owner->decode_evidence ? owner : NULL;
}

static sg_compact_belief_perception_owner_binding_t *FindVacantOwner(void)
{
	size_t index;

	for (index = 0U; index <
		SG_COMPACT_BELIEF_PERCEPTION_MAX_OWNER_BINDINGS; index++)
		if (sg_compact_belief_perception_owner_bindings[index].active != 1U)
			return &sg_compact_belief_perception_owner_bindings[index];
	return NULL;
}

static const sg_belief_runtime_provider_t *BindingProvider(
	const sg_compact_belief_perception_binding_t *binding)
{
	const sg_belief_runtime_provider_t *provider = SG_BeliefRuntimeProvider();
	sg_compact_belief_perception_owner_binding_t *owner = BindingOwner(binding);

	if (!binding || binding->bound != 1U || !owner ||
		binding->reserved[0] != 0U || binding->reserved[1] != 0U ||
		binding->reserved[2] != 0U || !provider ||
		provider->model != binding->model ||
		provider->rune_identity != binding->rune_identity ||
		provider->topology_revision != binding->topology_revision ||
		provider->generation != binding->generation ||
		provider->identity != binding->identity)
		return NULL;
	return provider;
}

static sg_compact_belief_perception_result_t BindingStatus(
	const sg_compact_belief_perception_binding_t *binding)
{
	if (!binding || binding->bound != 1U)
		return SG_COMPACT_BELIEF_PERCEPTION_REJECTED;
	if (!BindingOwner(binding))
		return SG_COMPACT_BELIEF_PERCEPTION_REJECTED;
	if (!SG_BeliefRuntimeProviderAvailable())
		return SG_COMPACT_BELIEF_PERCEPTION_UNAVAILABLE;
	return BindingProvider(binding) ? SG_COMPACT_BELIEF_PERCEPTION_APPLIED :
		SG_COMPACT_BELIEF_PERCEPTION_IDENTITY_MISMATCH;
}

static int CaptureObservation(void *context,
	const sg_belief_runtime_observation_t *observation)
{
	sg_compact_belief_decoded_observation_t *decoded =
		(sg_compact_belief_decoded_observation_t *)context;

	if (!decoded || !observation || decoded->consumed == 1U ||
		decoded->invalid == 1U)
	{
		if (decoded)
			decoded->invalid = 1U;
		return 0;
	}
	if ((observation->evidence_kind == SG_BELIEF_RUNTIME_EVIDENCE_POSITIVE &&
		(!observation->hypotheses || observation->hypothesis_count == 0U ||
		observation->hypothesis_count > SG_BELIEF_RUNTIME_MAX_PARTICLES ||
		observation->coverage || observation->coverage_count != 0U)) ||
		(observation->evidence_kind == SG_BELIEF_RUNTIME_EVIDENCE_NEGATIVE &&
		(observation->hypotheses || observation->hypothesis_count != 0U ||
			!observation->coverage || observation->coverage_count == 0U ||
			observation->coverage_count > SG_BELIEF_RUNTIME_MAX_COVERAGE)))
	{
		decoded->invalid = 1U;
		return 0;
	}
	decoded->observation = *observation;
	decoded->hypothesis_count = observation->hypothesis_count;
	decoded->coverage_count = observation->coverage_count;
	if (decoded->hypothesis_count != 0U)
	{
		memcpy(decoded->hypotheses, observation->hypotheses,
			decoded->hypothesis_count * sizeof(decoded->hypotheses[0]));
		decoded->observation.hypotheses = decoded->hypotheses;
	}
	if (decoded->coverage_count != 0U)
	{
		memcpy(decoded->coverage, observation->coverage,
			decoded->coverage_count * sizeof(decoded->coverage[0]));
		decoded->observation.coverage = decoded->coverage;
	}
	decoded->consumed = 1U;
	return 1;
}

static sg_compact_belief_perception_result_t ObserveResult(
	sg_belief_runtime_observe_result_t result)
{
	switch (result)
	{
	case SG_BELIEF_RUNTIME_OBSERVE_APPLIED:
		return SG_COMPACT_BELIEF_PERCEPTION_APPLIED;
	case SG_BELIEF_RUNTIME_OBSERVE_UNAVAILABLE:
		return SG_COMPACT_BELIEF_PERCEPTION_UNAVAILABLE;
	case SG_BELIEF_RUNTIME_OBSERVE_CAPACITY:
		return SG_COMPACT_BELIEF_PERCEPTION_CAPACITY;
	case SG_BELIEF_RUNTIME_OBSERVE_OVERFLOW:
		return SG_COMPACT_BELIEF_PERCEPTION_OVERFLOW;
	case SG_BELIEF_RUNTIME_OBSERVE_REJECTED:
		break;
	}
	return SG_COMPACT_BELIEF_PERCEPTION_REJECTED;
}

static sg_compact_belief_perception_result_t FrameResult(
	sg_belief_runtime_frame_result_t result)
{
	switch (result)
	{
	case SG_BELIEF_RUNTIME_FRAME_APPLIED:
		return SG_COMPACT_BELIEF_PERCEPTION_APPLIED;
	case SG_BELIEF_RUNTIME_FRAME_UNAVAILABLE:
		return SG_COMPACT_BELIEF_PERCEPTION_UNAVAILABLE;
	case SG_BELIEF_RUNTIME_FRAME_OVERFLOW:
		return SG_COMPACT_BELIEF_PERCEPTION_OVERFLOW;
	case SG_BELIEF_RUNTIME_FRAME_REJECTED:
		break;
	}
	return SG_COMPACT_BELIEF_PERCEPTION_REJECTED;
}

static sg_compact_belief_perception_result_t DecodeObservation(
	const sg_compact_belief_perception_binding_t *binding,
	const sg_compact_belief_perception_evidence_authority_t *authority,
	sg_compact_belief_decoded_observation_t *decoded_out)
{
	const sg_belief_runtime_provider_t *provider;
	sg_compact_belief_perception_result_t status;

	if (!authority || !decoded_out)
		return SG_COMPACT_BELIEF_PERCEPTION_REJECTED;
	memset(decoded_out, 0, sizeof(*decoded_out));
	status = BindingStatus(binding);
	if (status != SG_COMPACT_BELIEF_PERCEPTION_APPLIED)
		return status;
	provider = BindingProvider(binding);
	if (!provider || !BindingOwner(binding)->decode_evidence(
		BindingOwner(binding)->decode_context, provider,
		authority, CaptureObservation, decoded_out) ||
		decoded_out->consumed != 1U || decoded_out->invalid == 1U)
		return SG_COMPACT_BELIEF_PERCEPTION_REJECTED;
	status = BindingStatus(binding);
	if (status != SG_COMPACT_BELIEF_PERCEPTION_APPLIED)
		return status;
	if (decoded_out->observation.rune_identity != binding->rune_identity ||
		decoded_out->observation.topology_revision != binding->topology_revision)
		return SG_COMPACT_BELIEF_PERCEPTION_IDENTITY_MISMATCH;
	return SG_COMPACT_BELIEF_PERCEPTION_APPLIED;
}

/* Trusted host-only constructor.  It is intentionally absent from the public
 * header, so ordinary gameplay cannot install a decoder for caller-filled
 * raw observations. */
sg_compact_belief_perception_result_t
SG_CompactBeliefPerceptionBindTrustedOwner(
	sg_compact_belief_perception_binding_t *binding,
	const sg_belief_runtime_provider_t *provider,
	sg_compact_belief_perception_evidence_decode_fn decode_evidence,
	void *decode_context)
{
	const sg_belief_runtime_provider_t *current;
	sg_compact_belief_perception_binding_t candidate;
	sg_compact_belief_perception_owner_binding_t *owner;

	if (!binding || !provider || !decode_evidence)
		return SG_COMPACT_BELIEF_PERCEPTION_REJECTED;
	current = SG_BeliefRuntimeProvider();
	if (!current)
		return SG_COMPACT_BELIEF_PERCEPTION_UNAVAILABLE;
	if (provider->model != current->model ||
		provider->rune_identity != current->rune_identity ||
		provider->topology_revision != current->topology_revision ||
		provider->generation != current->generation ||
		provider->identity != current->identity)
		return SG_COMPACT_BELIEF_PERCEPTION_IDENTITY_MISMATCH;
	if (BindingOwner(binding))
		return SG_COMPACT_BELIEF_PERCEPTION_REJECTED;
	owner = FindVacantOwner();
	if (!owner)
		return SG_COMPACT_BELIEF_PERCEPTION_CAPACITY;
	memset(&candidate, 0, sizeof(candidate));
	candidate.model = current->model;
	candidate.identity = current->identity;
	candidate.rune_identity = current->rune_identity;
	candidate.topology_revision = current->topology_revision;
	candidate.generation = current->generation;
	candidate.owner_slot = (uint32_t)(owner -
		sg_compact_belief_perception_owner_bindings) + 1U;
	candidate.bound = 1U;
	*binding = candidate;
	memset(owner, 0, sizeof(*owner));
	owner->binding = binding;
	owner->decode_evidence = decode_evidence;
	owner->decode_context = decode_context;
	owner->active = 1U;
	return SG_COMPACT_BELIEF_PERCEPTION_APPLIED;
}

void SG_CompactBeliefPerceptionUnbind(
	sg_compact_belief_perception_binding_t *binding)
{
	sg_compact_belief_perception_owner_binding_t *owner = BindingOwner(binding);

	if (owner)
		memset(owner, 0, sizeof(*owner));
	if (binding)
		memset(binding, 0, sizeof(*binding));
}

int SG_CompactBeliefPerceptionBindingCurrent(
	const sg_compact_belief_perception_binding_t *binding)
{
	return BindingStatus(binding) == SG_COMPACT_BELIEF_PERCEPTION_APPLIED;
}

sg_compact_belief_perception_result_t SG_CompactBeliefPerceptionObserve(
	const sg_compact_belief_perception_binding_t *binding,
	const sg_compact_belief_perception_evidence_authority_t *authority)
{
	sg_compact_belief_decoded_observation_t decoded;
	sg_compact_belief_perception_result_t status = DecodeObservation(binding,
		authority, &decoded);

	if (status != SG_COMPACT_BELIEF_PERCEPTION_APPLIED)
		return status;
	return ObserveResult(SG_BeliefRuntimeObserveFromCompactOwner(
		&decoded.observation));
}

sg_compact_belief_perception_result_t SG_CompactBeliefPerceptionObserveSound(
	const sg_compact_belief_perception_binding_t *binding,
	const sg_compact_belief_perception_evidence_authority_t *authority)
{
	sg_compact_belief_decoded_observation_t decoded;
	sg_compact_belief_perception_result_t status = DecodeObservation(binding,
		authority, &decoded);

	if (status != SG_COMPACT_BELIEF_PERCEPTION_APPLIED)
		return status;
	if (decoded.observation.source != SG_BELIEF_RUNTIME_SOURCE_SOUND)
		return SG_COMPACT_BELIEF_PERCEPTION_REJECTED;
	return ObserveResult(SG_BeliefRuntimeObserveFromCompactOwner(
		&decoded.observation));
}

sg_compact_belief_perception_result_t SG_CompactBeliefPerceptionObserveDamage(
	const sg_compact_belief_perception_binding_t *binding,
	const sg_compact_belief_perception_evidence_authority_t *authority)
{
	sg_compact_belief_decoded_observation_t decoded;
	sg_compact_belief_perception_result_t status = DecodeObservation(binding,
		authority, &decoded);

	if (status != SG_COMPACT_BELIEF_PERCEPTION_APPLIED)
		return status;
	if (decoded.observation.source != SG_BELIEF_RUNTIME_SOURCE_DAMAGE)
		return SG_COMPACT_BELIEF_PERCEPTION_REJECTED;
	return ObserveResult(SG_BeliefRuntimeObserveFromCompactOwner(
		&decoded.observation));
}

sg_compact_belief_perception_result_t SG_CompactBeliefPerceptionFrame(
	const sg_compact_belief_perception_binding_t *binding,
	uint64_t frame_sequence, uint64_t at_ms)
{
	sg_compact_belief_perception_result_t status = BindingStatus(binding);

	if (status != SG_COMPACT_BELIEF_PERCEPTION_APPLIED)
		return status;
	return FrameResult(SG_BeliefRuntimeFrame(frame_sequence, at_ms));
}

const sg_belief_runtime_view_t *SG_CompactBeliefPerceptionView(
	const sg_compact_belief_perception_binding_t *binding,
	uint8_t audience_team, const sg_belief_runtime_life_t *target_life,
	uint64_t at_ms)
{
	return BindingStatus(binding) == SG_COMPACT_BELIEF_PERCEPTION_APPLIED ?
		SG_BeliefRuntimeView(audience_team, target_life, at_ms) : NULL;
}

const sg_belief_runtime_view_t *SG_CompactBeliefPerceptionViewForClient(
	const sg_compact_belief_perception_binding_t *binding,
	uint8_t audience_team, uint32_t client_id, uint64_t at_ms)
{
	return BindingStatus(binding) == SG_COMPACT_BELIEF_PERCEPTION_APPLIED ?
		SG_BeliefRuntimeViewForClient(audience_team, client_id, at_ms) : NULL;
}
