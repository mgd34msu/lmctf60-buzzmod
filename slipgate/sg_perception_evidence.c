#include "sg_perception_evidence.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

typedef struct perception_hypothesis_span_s
{
	const sg_perception_hypothesis_t *values;
	size_t count;
	sg_perception_source_t shape_source;
	uint8_t external;
} perception_hypothesis_span_t;

typedef struct perception_byte_range_s
{
	uintptr_t begin;
	uintptr_t end;
} perception_byte_range_t;

static int PerceptionByteCount(size_t count, size_t element_size,
	size_t *bytes)
{
	if (!bytes || (element_size != 0U && count > SIZE_MAX / element_size))
		return 0;
	*bytes = count * element_size;
	return 1;
}

static int PerceptionByteRange(const void *pointer, size_t bytes,
	perception_byte_range_t *range)
{
	uintptr_t begin;

	if (!pointer || !range || bytes == 0U)
		return 0;
	begin = (uintptr_t)pointer;
	if (bytes > UINTPTR_MAX - begin)
		return 0;
	range->begin = begin;
	range->end = begin + bytes;
	return 1;
}

static int PerceptionRangesOverlap(const perception_byte_range_t *left,
	const perception_byte_range_t *right)
{
	return left->begin < right->end && right->begin < left->end;
}

static int PerceptionVectorValid(const float vector[3])
{
	return vector && isfinite(vector[0]) && isfinite(vector[1]) &&
		isfinite(vector[2]);
}

static sg_perception_adapt_result_t PerceptionAuthenticationValid(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_perception_observation_t *observation)
{
	const sg_perception_authentication_t *authentication =
		&observation->authentication;
	sg_perception_authority_t required_authority =
		observation->source == SG_PERCEPTION_SOURCE_TEAMMATE ?
			SG_PERCEPTION_AUTHORITY_HOST_TEAMMATE_REPORT :
			SG_PERCEPTION_AUTHORITY_HOST_SENSOR;

	if (authentication->authenticated != 1U ||
	    authentication->authority != required_authority ||
	    authentication->authority < SG_PERCEPTION_AUTHORITY_HOST_SENSOR ||
	    authentication->authority >= SG_PERCEPTION_AUTHORITY_COUNT ||
	    !SG_BeliefTeamValid(authentication->issuer_team) ||
	    authentication->issuer_team != authentication->audience_team ||
	    !SG_BeliefReservedZero(authentication->reserved,
		sizeof(authentication->reserved)) ||
	    !SG_BeliefLifeIdentityValid(&authentication->issuer_life) ||
	    authentication->event_id == 0U ||
	    authentication->evidence_sequence == 0U ||
	    authentication->observed_at_ms == 0U ||
	    authentication->observed_at_ms >
		authentication->authenticated_at_ms ||
	    authentication->authenticated_at_ms == 0U ||
	    authentication->valid_until_ms <
		authentication->authenticated_at_ms ||
	    authentication->rune_identity != snapshot->identity ||
	    authentication->topology_revision != snapshot->topology_revision)
		return SG_PERCEPTION_ADAPT_REJECTED_AUTHORITY;
	return SG_PERCEPTION_ADAPT_APPLIED;
}

static int PerceptionHypothesisValid(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_perception_hypothesis_t *hypothesis,
	int static_location_allowed,
	sg_belief_evidence_kind_t evidence_kind)
{
	if (!hypothesis || !SG_PhaseCoordinateValid(snapshot, &hypothesis->phase) ||
	    !SG_BeliefPositionInsidePhaseCell(snapshot, &hypothesis->phase,
		hypothesis->position) ||
	    !SG_BeliefKinematicsCompatible(snapshot, &hypothesis->phase,
		hypothesis->movement_state, hypothesis->velocity,
		hypothesis->acceleration, hypothesis->orientation) ||
	    hypothesis->location_basis < SG_PERCEPTION_LOCATION_EARNED_RUNTIME ||
	    hypothesis->location_basis >= SG_PERCEPTION_LOCATION_BASIS_COUNT ||
	    (hypothesis->location_basis == SG_PERCEPTION_LOCATION_RUNE_STATIC &&
	     !static_location_allowed) ||
	    hypothesis->movement_state < SG_BELIEF_MOTION_UNKNOWN ||
	    hypothesis->movement_state >= SG_BELIEF_MOTION_COUNT ||
	    hypothesis->reserved[0] != 0U || hypothesis->reserved[1] != 0U ||
	    hypothesis->reserved[2] != 0U ||
	    !PerceptionVectorValid(hypothesis->position) ||
	    !PerceptionVectorValid(hypothesis->velocity) ||
	    !PerceptionVectorValid(hypothesis->acceleration) ||
	    !PerceptionVectorValid(hypothesis->orientation) ||
	    !isfinite(hypothesis->spread_radius) ||
	    hypothesis->spread_radius < 0.0f ||
	    !isfinite(hypothesis->likelihood) ||
	    hypothesis->likelihood <= 0.0f ||
	    (evidence_kind == SG_BELIEF_EVIDENCE_NEGATIVE &&
	     hypothesis->likelihood > 1.0f))
		return 0;
	return 1;
}

static int PerceptionLocationsDiffer(
	const sg_perception_hypothesis_t *left,
	const sg_perception_hypothesis_t *right)
{
	uint8_t axis;

	if (left->phase.phase_id != right->phase.phase_id ||
	    left->phase.cell_id != right->phase.cell_id)
		return 1;
	for (axis = 0U; axis < 3U; axis++)
		if (left->position[axis] != right->position[axis])
			return 1;
	return 0;
}

static int PerceptionHasDistinctModes(
	const sg_perception_hypothesis_t *hypotheses, size_t count)
{
	size_t index;

	for (index = 1U; index < count; index++)
		if (PerceptionLocationsDiffer(&hypotheses[0], &hypotheses[index]))
			return 1;
	return 0;
}

static int PerceptionSetDiffuseOrMultimodal(
	const sg_perception_hypothesis_t *hypotheses, size_t count)
{
	size_t index;
	int all_diffuse = 1;

	for (index = 0U; index < count; index++)
		if (hypotheses[index].spread_radius == 0.0f)
			all_diffuse = 0;
	return all_diffuse || PerceptionHasDistinctModes(hypotheses, count);
}

static int PerceptionShapeValid(sg_perception_source_t source,
	sg_belief_evidence_kind_t evidence_kind,
	const sg_perception_hypothesis_t *hypotheses, size_t count)
{
	if (!hypotheses || count == 0U)
		return 0;
	switch (source)
	{
	case SG_PERCEPTION_SOURCE_SIGHT:
		return evidence_kind == SG_BELIEF_EVIDENCE_NEGATIVE ||
			(count == 1U && hypotheses[0].spread_radius == 0.0f);
	case SG_PERCEPTION_SOURCE_SOUND:
	case SG_PERCEPTION_SOURCE_DAMAGE:
		return evidence_kind == SG_BELIEF_EVIDENCE_POSITIVE &&
			PerceptionSetDiffuseOrMultimodal(hypotheses, count);
	case SG_PERCEPTION_SOURCE_ITEM:
	case SG_PERCEPTION_SOURCE_FLAG:
		return evidence_kind == SG_BELIEF_EVIDENCE_POSITIVE;
	case SG_PERCEPTION_SOURCE_TEAMMATE:
	case SG_PERCEPTION_SOURCE_COUNT:
		return 0;
	}
	return 0;
}

static int PerceptionSoundMetadataValid(const sg_perception_sound_t *sound)
{
	return sound && sound->in_phs == 1U && sound->positional == 1U &&
		sound->reserved == 0U &&
		sound->kind >= SG_PERCEPTION_SOUND_FOOTSTEP &&
		sound->kind < SG_PERCEPTION_SOUND_KIND_COUNT && sound->sound_id != 0U &&
		PerceptionVectorValid(sound->listener_position) &&
		PerceptionVectorValid(sound->heard_origin) &&
		isfinite(sound->attenuation) && sound->attenuation > 0.0f &&
		isfinite(sound->audible_radius) && sound->audible_radius > 0.0f;
}

static int PerceptionDamageMetadataValid(
	const sg_perception_damage_t *damage)
{
	double direction_squared;

	if (!damage || damage->landed != 1U || damage->reserved[0] != 0U ||
	    damage->reserved[1] != 0U || damage->reserved[2] != 0U ||
	    damage->damage == 0U || !PerceptionVectorValid(damage->victim_position) ||
	    !PerceptionVectorValid(damage->incoming_direction))
		return 0;
	direction_squared =
		(double)damage->incoming_direction[0] *
			(double)damage->incoming_direction[0] +
		(double)damage->incoming_direction[1] *
			(double)damage->incoming_direction[1] +
		(double)damage->incoming_direction[2] *
			(double)damage->incoming_direction[2];
	return isfinite(direction_squared) && direction_squared > 0.0;
}

static int PerceptionItemDestinationValid(const sg_destination_ref_t *ref)
{
	return SG_DestinationRefValid(ref) &&
		(ref->kind == SG_DESTINATION_ITEM ||
		 ref->kind == SG_DESTINATION_WEAPON ||
		 ref->kind == SG_DESTINATION_ARMOR ||
		 ref->kind == SG_DESTINATION_POWERUP);
}

static int PerceptionPayloadSpan(const sg_perception_observation_t *observation,
	perception_hypothesis_span_t *span)
{
	memset(span, 0, sizeof(*span));
	span->shape_source = observation->source;
	switch (observation->source)
	{
	case SG_PERCEPTION_SOURCE_SIGHT:
		if (observation->data.sight.in_pvs != 1U ||
		    observation->data.sight.line_of_sight_proved != 1U ||
		    observation->data.sight.reserved[0] != 0U ||
		    observation->data.sight.reserved[1] != 0U)
			return 0;
		span->values = &observation->data.sight.hypothesis;
		span->count = 1U;
		return 1;
	case SG_PERCEPTION_SOURCE_SOUND:
		if (!PerceptionSoundMetadataValid(&observation->data.sound))
			return 0;
		span->values = observation->data.sound.hypotheses;
		span->count = observation->data.sound.hypothesis_count;
		span->external = 1U;
		return 1;
	case SG_PERCEPTION_SOURCE_DAMAGE:
		if (!PerceptionDamageMetadataValid(&observation->data.damage))
			return 0;
		span->values = observation->data.damage.hypotheses;
		span->count = observation->data.damage.hypothesis_count;
		span->external = 1U;
		return 1;
	case SG_PERCEPTION_SOURCE_ITEM:
		if (observation->data.item.occurrence <
		    SG_PERCEPTION_ITEM_TARGET_PICKUP ||
		    observation->data.item.occurrence >=
		    SG_PERCEPTION_ITEM_OCCURRENCE_COUNT ||
		    !PerceptionItemDestinationValid(
			&observation->data.item.destination))
			return 0;
		span->values = observation->data.item.hypotheses;
		span->count = observation->data.item.hypothesis_count;
		span->external = 1U;
		return 1;
	case SG_PERCEPTION_SOURCE_FLAG:
		if (observation->data.flag.occurrence <
		    SG_PERCEPTION_FLAG_TARGET_PICKUP ||
		    observation->data.flag.occurrence >=
		    SG_PERCEPTION_FLAG_OCCURRENCE_COUNT ||
		    !SG_DestinationRefValid(&observation->data.flag.destination) ||
		    observation->data.flag.destination.kind != SG_DESTINATION_FLAG)
			return 0;
		span->values = observation->data.flag.hypotheses;
		span->count = observation->data.flag.hypothesis_count;
		span->external = 1U;
		return 1;
	case SG_PERCEPTION_SOURCE_TEAMMATE:
		if (observation->data.teammate.reported_source <
		    SG_PERCEPTION_SOURCE_SIGHT ||
		    observation->data.teammate.reported_source >=
		    SG_PERCEPTION_SOURCE_TEAMMATE ||
		    observation->data.teammate.report_kind == 0U ||
		    (observation->data.teammate.reported_source ==
		     SG_PERCEPTION_SOURCE_ITEM &&
		     !PerceptionItemDestinationValid(
			&observation->data.teammate.reported_destination)) ||
		    (observation->data.teammate.reported_source ==
		     SG_PERCEPTION_SOURCE_FLAG &&
		     (!SG_DestinationRefValid(
			&observation->data.teammate.reported_destination) ||
		      observation->data.teammate.reported_destination.kind !=
			SG_DESTINATION_FLAG)))
			return 0;
		span->shape_source = observation->data.teammate.reported_source;
		span->values = observation->data.teammate.hypotheses;
		span->count = observation->data.teammate.hypothesis_count;
		span->external = 1U;
		return 1;
	case SG_PERCEPTION_SOURCE_COUNT:
		return 0;
	}
	return 0;
}

static int PerceptionOccurrenceLocationValid(
	const sg_perception_observation_t *observation,
	const perception_hypothesis_span_t *span)
{
	size_t index;

	if (observation->source == SG_PERCEPTION_SOURCE_TEAMMATE)
	{
		for (index = 0U; index < span->count; index++)
			if (span->values[index].location_basis !=
			    SG_PERCEPTION_LOCATION_EARNED_RUNTIME)
				return 0;
		return 1;
	}
	if (observation->source != SG_PERCEPTION_SOURCE_FLAG ||
	    observation->data.flag.occurrence !=
		SG_PERCEPTION_FLAG_TARGET_CARRY_SIGHTED)
		return 1;
	for (index = 0U; index < span->count; index++)
		if (span->values[index].location_basis !=
		    SG_PERCEPTION_LOCATION_EARNED_RUNTIME)
			return 0;
	return 1;
}

static int PerceptionStaticLocationAllowed(
	const sg_perception_observation_t *observation)
{
	if (observation->source == SG_PERCEPTION_SOURCE_ITEM)
		return 1;
	return observation->source == SG_PERCEPTION_SOURCE_FLAG &&
		observation->data.flag.occurrence ==
			SG_PERCEPTION_FLAG_TARGET_PICKUP &&
		observation->data.flag.destination.value.flag.location ==
			SG_DESTINATION_FLAG_HOME;
}

static sg_belief_evidence_source_t PerceptionBeliefSource(
	sg_perception_source_t source)
{
	switch (source)
	{
	case SG_PERCEPTION_SOURCE_SIGHT:
		return SG_BELIEF_SOURCE_SIGHT;
	case SG_PERCEPTION_SOURCE_SOUND:
		return SG_BELIEF_SOURCE_SOUND;
	case SG_PERCEPTION_SOURCE_DAMAGE:
		return SG_BELIEF_SOURCE_DAMAGE;
	case SG_PERCEPTION_SOURCE_ITEM:
		return SG_BELIEF_SOURCE_ITEM;
	case SG_PERCEPTION_SOURCE_FLAG:
		return SG_BELIEF_SOURCE_FLAG;
	case SG_PERCEPTION_SOURCE_TEAMMATE:
		return SG_BELIEF_SOURCE_TEAMMATE;
	case SG_PERCEPTION_SOURCE_COUNT:
		break;
	}
	return SG_BELIEF_SOURCE_COUNT;
}

static void PerceptionCopySupport(sg_belief_evidence_support_t *out,
	const sg_perception_hypothesis_t *hypothesis)
{
	uint8_t axis;

	memset(out, 0, sizeof(*out));
	out->phase = hypothesis->phase;
	out->movement_state = hypothesis->movement_state;
	out->weapon_state = hypothesis->weapon_state;
	for (axis = 0U; axis < 3U; axis++)
	{
		out->position[axis] = hypothesis->position[axis];
		out->velocity[axis] = hypothesis->velocity[axis];
		out->acceleration[axis] = hypothesis->acceleration[axis];
		out->orientation[axis] = hypothesis->orientation[axis];
	}
	out->spread_radius = hypothesis->spread_radius;
	out->likelihood = hypothesis->likelihood;
}

static void PerceptionBuildEvidence(sg_belief_evidence_t *evidence,
	const sg_perception_observation_t *observation,
	sg_belief_evidence_support_t *support_storage, size_t support_count)
{
	const sg_perception_authentication_t *authentication =
		&observation->authentication;

	memset(evidence, 0, sizeof(*evidence));
	evidence->provenance.authenticated = authentication->authenticated;
	evidence->provenance.issuer_kind =
		observation->source == SG_PERCEPTION_SOURCE_TEAMMATE ?
			SG_BELIEF_ISSUER_TEAMMATE : SG_BELIEF_ISSUER_LOCAL_SENSOR;
	evidence->provenance.issuer_team = authentication->issuer_team;
	evidence->provenance.audience_team = authentication->audience_team;
	evidence->provenance.issuer_life = authentication->issuer_life;
	evidence->provenance.evidence_id = authentication->event_id;
	evidence->provenance.evidence_sequence =
		authentication->evidence_sequence;
	evidence->provenance.authenticated_at_ms =
		authentication->authenticated_at_ms;
	evidence->provenance.rune_identity = authentication->rune_identity;
	evidence->provenance.topology_revision =
		authentication->topology_revision;
	evidence->source = PerceptionBeliefSource(observation->source);
	evidence->kind = observation->evidence_kind;
	evidence->target_team = observation->target_team;
	evidence->target_life = observation->target_life;
	evidence->observed_at_ms = authentication->observed_at_ms;
	evidence->valid_until_ms = authentication->valid_until_ms;
	evidence->confidence = observation->confidence;
	evidence->supports = support_storage;
	evidence->support_count = support_count;
}

sg_perception_adapt_result_t SG_PerceptionEvidenceAdapt(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_perception_observation_t *observation,
	sg_belief_evidence_support_t *support_storage, size_t support_capacity,
	sg_perception_adaptation_t *out)
{
	sg_perception_adaptation_t adaptation;
	perception_hypothesis_span_t span;
	perception_byte_range_t observation_range;
	perception_byte_range_t output_range;
	perception_byte_range_t support_range;
	perception_byte_range_t hypothesis_range;
	sg_perception_adapt_result_t authentication_result;
	size_t support_bytes;
	size_t support_span_count;
	size_t hypothesis_bytes;
	int static_location_allowed;
	size_t index;

	if (!out)
		return SG_PERCEPTION_ADAPT_REJECTED_INVALID;
	if (!PerceptionByteRange(observation, sizeof(*observation),
	    &observation_range) ||
	    !PerceptionByteRange(out, sizeof(*out), &output_range) ||
	    PerceptionRangesOverlap(&observation_range, &output_range))
		return SG_PERCEPTION_ADAPT_REJECTED_INVALID;
	if (!SG_RuneRuntimeSnapshotValid(snapshot) ||
	    !SG_BeliefMutableRangeDisjointFromRune(snapshot, out, sizeof(*out)))
		return SG_PERCEPTION_ADAPT_REJECTED_INVALID;
	memset(&adaptation, 0, sizeof(adaptation));
	adaptation.result = SG_PERCEPTION_ADAPT_REJECTED_INVALID;
	if (!observation ||
	    observation->source < SG_PERCEPTION_SOURCE_SIGHT ||
	    observation->source >= SG_PERCEPTION_SOURCE_COUNT ||
	    observation->evidence_kind < SG_BELIEF_EVIDENCE_POSITIVE ||
	    observation->evidence_kind >= SG_BELIEF_EVIDENCE_KIND_COUNT ||
	    !SG_BeliefTeamValid(observation->target_team) ||
	    !SG_BeliefReservedZero(observation->reserved,
		sizeof(observation->reserved)) ||
	    !SG_BeliefLifeIdentityValid(&observation->target_life) ||
	    !isfinite(observation->confidence) ||
	    observation->confidence <= 0.0f || observation->confidence > 1.0f)
	{
		*out = adaptation;
		return adaptation.result;
	}
	if (!PerceptionPayloadSpan(observation, &span) || !span.values ||
	    span.count == 0U)
	{
		*out = adaptation;
		return adaptation.result;
	}
	if (span.external && !PerceptionByteCount(span.count,
	    sizeof(*span.values), &hypothesis_bytes))
		return SG_PERCEPTION_ADAPT_OVERFLOW;
	if (span.external &&
	    (!PerceptionByteRange(span.values, hypothesis_bytes,
		&hypothesis_range) ||
	     PerceptionRangesOverlap(&hypothesis_range, &observation_range) ||
	     PerceptionRangesOverlap(&hypothesis_range, &output_range)))
		return SG_PERCEPTION_ADAPT_REJECTED_INVALID;
	if (!PerceptionByteCount(span.count, sizeof(*support_storage),
	    &support_bytes))
		return SG_PERCEPTION_ADAPT_OVERFLOW;
	support_span_count = support_capacity < span.count ? support_capacity :
		span.count;
	if (support_storage && support_span_count != 0U &&
	    (!PerceptionByteCount(support_span_count, sizeof(*support_storage),
		&support_bytes) ||
	     !PerceptionByteRange(support_storage, support_bytes, &support_range) ||
	     !SG_BeliefMutableRangeDisjointFromRune(snapshot, support_storage,
		support_bytes) ||
	     PerceptionRangesOverlap(&support_range, &observation_range) ||
	     PerceptionRangesOverlap(&support_range, &output_range)))
		return SG_PERCEPTION_ADAPT_REJECTED_INVALID;
	if (span.external && support_storage && support_span_count != 0U &&
	    PerceptionRangesOverlap(&hypothesis_range, &support_range))
		return SG_PERCEPTION_ADAPT_REJECTED_INVALID;
	authentication_result = PerceptionAuthenticationValid(snapshot,
		observation);
	if (authentication_result != SG_PERCEPTION_ADAPT_APPLIED)
	{
		adaptation.result = authentication_result;
		*out = adaptation;
		return adaptation.result;
	}
	if (!PerceptionShapeValid(span.shape_source, observation->evidence_kind,
	    span.values, span.count) ||
	    !PerceptionOccurrenceLocationValid(observation, &span))
	{
		*out = adaptation;
		return adaptation.result;
	}
	static_location_allowed = PerceptionStaticLocationAllowed(observation);
	for (index = 0U; index < span.count; index++)
		if (!PerceptionHypothesisValid(snapshot, &span.values[index],
		    static_location_allowed, observation->evidence_kind))
		{
			*out = adaptation;
			return adaptation.result;
		}
	adaptation.required_support_capacity = span.count;
	if (!support_storage || support_capacity < span.count)
	{
		adaptation.result = SG_PERCEPTION_ADAPT_CAPACITY;
		*out = adaptation;
		return adaptation.result;
	}
	for (index = 0U; index < span.count; index++)
		PerceptionCopySupport(&support_storage[index], &span.values[index]);
	PerceptionBuildEvidence(&adaptation.evidence, observation, support_storage,
		span.count);
	adaptation.result = SG_PERCEPTION_ADAPT_APPLIED;
	*out = adaptation;
	return adaptation.result;
}
