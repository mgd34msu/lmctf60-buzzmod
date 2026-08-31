#include "sg_compact_belief_perception.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct decoded_observation_s
{
	sg_perception_observation_t observation;
	sg_perception_hypothesis_t *hypotheses;
	size_t hypothesis_count;
	uint8_t consumed;
	uint8_t invalid;
} decoded_observation_t;

static int ObservationHypothesisSpan(
	const sg_perception_observation_t *observation,
	const sg_perception_hypothesis_t **hypotheses_out,
	size_t *count_out, int *external_out)
{
	if (!observation || !hypotheses_out || !count_out || !external_out)
		return 0;
	*hypotheses_out = NULL;
	*count_out = 0U;
	*external_out = 0;
	switch (observation->source)
	{
	case SG_PERCEPTION_SOURCE_SIGHT:
		return 1;
	case SG_PERCEPTION_SOURCE_SOUND:
		*hypotheses_out = observation->data.sound.hypotheses;
		*count_out = observation->data.sound.hypothesis_count;
		*external_out = 1;
		return 1;
	case SG_PERCEPTION_SOURCE_DAMAGE:
		*hypotheses_out = observation->data.damage.hypotheses;
		*count_out = observation->data.damage.hypothesis_count;
		*external_out = 1;
		return 1;
	case SG_PERCEPTION_SOURCE_ITEM:
		*hypotheses_out = observation->data.item.hypotheses;
		*count_out = observation->data.item.hypothesis_count;
		*external_out = 1;
		return 1;
	case SG_PERCEPTION_SOURCE_FLAG:
		*hypotheses_out = observation->data.flag.hypotheses;
		*count_out = observation->data.flag.hypothesis_count;
		*external_out = 1;
		return 1;
	case SG_PERCEPTION_SOURCE_TEAMMATE:
		*hypotheses_out = observation->data.teammate.hypotheses;
		*count_out = observation->data.teammate.hypothesis_count;
		*external_out = 1;
		return 1;
	case SG_PERCEPTION_SOURCE_COUNT:
		break;
	}
	return 0;
}

static void DecodedObservationSetHypothesisSpan(
	sg_perception_observation_t *observation,
	const sg_perception_hypothesis_t *hypotheses)
{
	switch (observation->source)
	{
	case SG_PERCEPTION_SOURCE_SOUND:
		observation->data.sound.hypotheses = hypotheses;
		break;
	case SG_PERCEPTION_SOURCE_DAMAGE:
		observation->data.damage.hypotheses = hypotheses;
		break;
	case SG_PERCEPTION_SOURCE_ITEM:
		observation->data.item.hypotheses = hypotheses;
		break;
	case SG_PERCEPTION_SOURCE_FLAG:
		observation->data.flag.hypotheses = hypotheses;
		break;
	case SG_PERCEPTION_SOURCE_TEAMMATE:
		observation->data.teammate.hypotheses = hypotheses;
		break;
	case SG_PERCEPTION_SOURCE_SIGHT:
	case SG_PERCEPTION_SOURCE_COUNT:
		break;
	}
}

static void DecodedObservationClear(decoded_observation_t *decoded)
{
	if (!decoded)
		return;
	free(decoded->hypotheses);
	memset(decoded, 0, sizeof(*decoded));
}

/* The host decoder owns the borrowed observation only for the duration of
 * consume.  Copy every external hypothesis span before returning so no
 * adapter path observes a callback-owned pointer after that boundary. */
static int CaptureObservation(void *context,
	const sg_perception_observation_t *observation)
{
	decoded_observation_t *decoded = (decoded_observation_t *)context;
	const sg_perception_hypothesis_t *hypotheses;
	int external;
	size_t count;

	if (!decoded || !observation || decoded->consumed == 1U ||
		decoded->invalid == 1U ||
		!ObservationHypothesisSpan(observation, &hypotheses, &count,
			&external))
	{
		if (decoded)
			decoded->invalid = 1U;
		return 0;
	}
	if (external && ((count != 0U && !hypotheses) ||
		count > SIZE_MAX / sizeof(*decoded->hypotheses)))
	{
		decoded->invalid = 1U;
		return 0;
	}
	decoded->observation = *observation;
	decoded->hypothesis_count = count;
	if (external && count != 0U)
	{
		decoded->hypotheses = malloc(count * sizeof(*decoded->hypotheses));
		if (!decoded->hypotheses)
		{
			decoded->invalid = 1U;
			return 0;
		}
		memcpy(decoded->hypotheses, hypotheses,
			count * sizeof(*decoded->hypotheses));
		DecodedObservationSetHypothesisSpan(&decoded->observation,
			decoded->hypotheses);
	}
	else if (external)
	{
		DecodedObservationSetHypothesisSpan(&decoded->observation, NULL);
	}
	decoded->consumed = 1U;
	return 1;
}

static int BindingSnapshotValid(
	const sg_compact_belief_perception_binding_t *binding)
{
	return binding && binding->bound == 1U && binding->decode_evidence &&
		SG_BeliefReservedZero(binding->reserved, sizeof(binding->reserved)) &&
		SG_RuneRuntimeSnapshotValid(binding->snapshot) &&
		binding->model == binding->snapshot->model &&
		binding->rune_identity == binding->snapshot->identity &&
		binding->topology_revision == binding->snapshot->topology_revision;
}

static sg_compact_belief_perception_result_t BindingStatus(
	const sg_compact_belief_perception_binding_t *binding)
{
	const sg_rune_runtime_snapshot_t *current;

	if (!binding || binding->bound != 1U)
		return SG_COMPACT_BELIEF_PERCEPTION_REJECTED;
	if (!BindingSnapshotValid(binding))
		return SG_COMPACT_BELIEF_PERCEPTION_IDENTITY_MISMATCH;
	current = SG_BeliefRuntimeSnapshot();
	if (!current)
		return SG_COMPACT_BELIEF_PERCEPTION_UNAVAILABLE;
	if (current != binding->snapshot || current->model != binding->model ||
		current->identity != binding->rune_identity ||
		current->topology_revision != binding->topology_revision)
		return SG_COMPACT_BELIEF_PERCEPTION_IDENTITY_MISMATCH;
	return SG_COMPACT_BELIEF_PERCEPTION_APPLIED;
}

static int AuthenticationIdentityMatches(
	const sg_compact_belief_perception_binding_t *binding,
	const sg_perception_authentication_t *authentication)
{
	return binding && authentication &&
		authentication->rune_identity == binding->rune_identity &&
		authentication->topology_revision == binding->topology_revision;
}

static int AuthenticationValidFor(
	const sg_compact_belief_perception_binding_t *binding,
	const sg_perception_observation_t *observation)
{
	const sg_perception_authentication_t *authentication;
	sg_perception_authority_t authority;

	if (!binding || !observation)
		return 0;
	authentication = &observation->authentication;
	if (observation->source < SG_PERCEPTION_SOURCE_SIGHT ||
		observation->source >= SG_PERCEPTION_SOURCE_COUNT)
		return 0;
	authority = observation->source == SG_PERCEPTION_SOURCE_TEAMMATE ?
		SG_PERCEPTION_AUTHORITY_HOST_TEAMMATE_REPORT :
		SG_PERCEPTION_AUTHORITY_HOST_SENSOR;
	return authentication->authenticated == 1U &&
		authentication->authority == authority &&
		authentication->authority >= SG_PERCEPTION_AUTHORITY_HOST_SENSOR &&
		authentication->authority < SG_PERCEPTION_AUTHORITY_COUNT &&
		SG_BeliefTeamValid(authentication->issuer_team) &&
		authentication->issuer_team == authentication->audience_team &&
		SG_BeliefReservedZero(authentication->reserved,
			sizeof(authentication->reserved)) &&
		SG_BeliefLifeIdentityValid(&authentication->issuer_life) &&
		authentication->event_id != 0U &&
		authentication->evidence_sequence != 0U &&
		authentication->observed_at_ms != 0U &&
		authentication->authenticated_at_ms != 0U &&
		authentication->observed_at_ms <=
			authentication->authenticated_at_ms &&
		authentication->valid_until_ms >=
			authentication->authenticated_at_ms &&
		AuthenticationIdentityMatches(binding, authentication) &&
		SG_BeliefTeamValid(observation->target_team) &&
		observation->target_team != authentication->audience_team &&
		SG_BeliefLifeIdentityValid(&observation->target_life) &&
		isfinite(observation->confidence) && observation->confidence > 0.0f &&
		observation->confidence <= 1.0f;
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
	case SG_BELIEF_RUNTIME_FRAME_CAPACITY:
		return SG_COMPACT_BELIEF_PERCEPTION_CAPACITY;
	case SG_BELIEF_RUNTIME_FRAME_OVERFLOW:
		return SG_COMPACT_BELIEF_PERCEPTION_OVERFLOW;
	case SG_BELIEF_RUNTIME_FRAME_REJECTED:
		break;
	}
	return SG_COMPACT_BELIEF_PERCEPTION_REJECTED;
}

static sg_compact_belief_perception_result_t PredictResult(
	sg_belief_runtime_predict_result_t result)
{
	switch (result)
	{
	case SG_BELIEF_RUNTIME_PREDICT_APPLIED:
		return SG_COMPACT_BELIEF_PERCEPTION_APPLIED;
	case SG_BELIEF_RUNTIME_PREDICT_UNAVAILABLE:
		return SG_COMPACT_BELIEF_PERCEPTION_UNAVAILABLE;
	case SG_BELIEF_RUNTIME_PREDICT_CAPACITY:
		return SG_COMPACT_BELIEF_PERCEPTION_CAPACITY;
	case SG_BELIEF_RUNTIME_PREDICT_OVERFLOW:
		return SG_COMPACT_BELIEF_PERCEPTION_OVERFLOW;
	case SG_BELIEF_RUNTIME_PREDICT_REJECTED:
		break;
	}
	return SG_COMPACT_BELIEF_PERCEPTION_REJECTED;
}

static sg_compact_belief_perception_result_t AdaptPreview(
	const sg_compact_belief_perception_binding_t *binding,
	const sg_perception_observation_t *observation, size_t *required)
{
	sg_perception_adaptation_t adaptation;
	sg_perception_adapt_result_t result;

	if (!binding || !observation || !required)
		return SG_COMPACT_BELIEF_PERCEPTION_REJECTED;
	memset(&adaptation, 0, sizeof(adaptation));
	result = SG_PerceptionEvidenceAdapt(binding->snapshot, observation, NULL,
		0U, &adaptation);
	if (result == SG_PERCEPTION_ADAPT_CAPACITY ||
		result == SG_PERCEPTION_ADAPT_APPLIED)
	{
		*required = adaptation.required_support_capacity;
		return *required != 0U ? SG_COMPACT_BELIEF_PERCEPTION_APPLIED :
			SG_COMPACT_BELIEF_PERCEPTION_REJECTED;
	}
	if (result == SG_PERCEPTION_ADAPT_OVERFLOW)
		return SG_COMPACT_BELIEF_PERCEPTION_OVERFLOW;
	return SG_COMPACT_BELIEF_PERCEPTION_REJECTED;
}

static sg_compact_belief_perception_result_t DecodeObservation(
	const sg_compact_belief_perception_binding_t *binding,
	const sg_compact_belief_perception_evidence_authority_t *authority,
	decoded_observation_t *decoded_out)
{
	sg_compact_belief_perception_result_t status;
	size_t required;

	if (!decoded_out || !authority)
		return SG_COMPACT_BELIEF_PERCEPTION_REJECTED;
	memset(decoded_out, 0, sizeof(*decoded_out));
	status = BindingStatus(binding);
	if (status != SG_COMPACT_BELIEF_PERCEPTION_APPLIED)
		return status;
	if (!binding->decode_evidence(binding->decode_context, binding->snapshot,
		authority, CaptureObservation, decoded_out) ||
		decoded_out->consumed != 1U || decoded_out->invalid == 1U)
	{
		DecodedObservationClear(decoded_out);
		return SG_COMPACT_BELIEF_PERCEPTION_REJECTED;
	}
	status = BindingStatus(binding);
	if (status != SG_COMPACT_BELIEF_PERCEPTION_APPLIED)
	{
		DecodedObservationClear(decoded_out);
		return status;
	}
	if (!AuthenticationIdentityMatches(binding,
		&decoded_out->observation.authentication))
	{
		DecodedObservationClear(decoded_out);
		return SG_COMPACT_BELIEF_PERCEPTION_IDENTITY_MISMATCH;
	}
	if (!AuthenticationValidFor(binding, &decoded_out->observation))
	{
		DecodedObservationClear(decoded_out);
		return SG_COMPACT_BELIEF_PERCEPTION_REJECTED;
	}
	status = AdaptPreview(binding, &decoded_out->observation, &required);
	if (status != SG_COMPACT_BELIEF_PERCEPTION_APPLIED)
	{
		DecodedObservationClear(decoded_out);
		return status;
	}
	(void)required;
	return SG_COMPACT_BELIEF_PERCEPTION_APPLIED;
}

static sg_compact_belief_perception_result_t RuntimeObserve(
	const sg_compact_belief_perception_binding_t *binding,
	const sg_perception_observation_t *observation)
{
	sg_compact_belief_perception_result_t status = BindingStatus(binding);

	if (status != SG_COMPACT_BELIEF_PERCEPTION_APPLIED)
		return status;
	return ObserveResult(SG_BeliefRuntimeObserve(observation));
}

static int VectorDistance(const float left[3], const float right[3],
	double *distance_out)
{
	double dx;
	double dy;
	double dz;

	if (!left || !right || !distance_out)
		return 0;
	dx = (double)left[0] - (double)right[0];
	dy = (double)left[1] - (double)right[1];
	dz = (double)left[2] - (double)right[2];
	if (!isfinite(dx) || !isfinite(dy) || !isfinite(dz))
		return 0;
	*distance_out = hypot(hypot(dx, dy), dz);
	return isfinite(*distance_out);
}

static int SoundKindSupportsRuntime(
	const sg_perception_sound_t *sound,
	const sg_perception_hypothesis_t *hypothesis)
{
	if (!sound || !hypothesis ||
		hypothesis->location_basis != SG_PERCEPTION_LOCATION_EARNED_RUNTIME)
		return 0;
	switch (sound->kind)
	{
	case SG_PERCEPTION_SOUND_FOOTSTEP:
	case SG_PERCEPTION_SOUND_WEAPON:
	case SG_PERCEPTION_SOUND_MOVEMENT:
	case SG_PERCEPTION_SOUND_OTHER_SPATIAL:
		return 1;
	case SG_PERCEPTION_SOUND_ITEM:
		/* Static item coordinates are supplied by the item-event source.  An
		 * item sound may refine a runtime target, but cannot smuggle a static
		 * RUNE location into the sound channel. */
		return 1;
	case SG_PERCEPTION_SOUND_KIND_COUNT:
		break;
	}
	return 0;
}

static int SoundSupportFactor(const sg_perception_sound_t *sound,
	const sg_perception_hypothesis_t *hypothesis, float *factor_out)
{
	double event_distance;
	double target_distance;
	double radial_distance;
	double event_dx;
	double event_dy;
	double event_dz;
	double target_dx;
	double target_dy;
	double target_dz;
	double event_length;
	double target_length;
	double bearing;
	double dot;
	double factor;
	double denominator;

	if (!sound || !hypothesis || !factor_out ||
		!SoundKindSupportsRuntime(sound, hypothesis) ||
		!VectorDistance(hypothesis->position, sound->heard_origin,
			&event_distance) ||
		!VectorDistance(hypothesis->position, sound->listener_position,
			&target_distance) || event_distance >
		(double)sound->audible_radius)
		return 0;
	radial_distance = event_distance / (double)sound->audible_radius;
	denominator = (double)sound->attenuation + radial_distance;
	if (!isfinite(radial_distance) || !isfinite(denominator) ||
		denominator <= 0.0)
		return 0;
	factor = (double)sound->attenuation / denominator;
	event_dx = (double)sound->heard_origin[0] -
		(double)sound->listener_position[0];
	event_dy = (double)sound->heard_origin[1] -
		(double)sound->listener_position[1];
	event_dz = (double)sound->heard_origin[2] -
		(double)sound->listener_position[2];
	target_dx = (double)hypothesis->position[0] -
		(double)sound->listener_position[0];
	target_dy = (double)hypothesis->position[1] -
		(double)sound->listener_position[1];
	target_dz = (double)hypothesis->position[2] -
		(double)sound->listener_position[2];
	event_length = hypot(hypot(event_dx, event_dy), event_dz);
	target_length = hypot(hypot(target_dx, target_dy), target_dz);
	if (!isfinite(event_length) || !isfinite(target_length))
		return 0;
	if (event_length > 0.0 && target_length > 0.0)
	{
		dot = event_dx * target_dx + event_dy * target_dy +
			event_dz * target_dz;
		bearing = dot / (event_length * target_length);
		if (!isfinite(bearing))
			return 0;
		if (bearing < -1.0)
			bearing = -1.0;
		if (bearing > 1.0)
			bearing = 1.0;
		factor *= (bearing + 1.0) * 0.5;
	}
	if (!isfinite(factor) || factor <= 0.0 || factor > (double)FLT_MAX)
		return 0;
	*factor_out = (float)factor;
	return isfinite(*factor_out) && *factor_out > 0.0f;
}

static sg_compact_belief_perception_result_t ObserveSoundDecoded(
	const sg_compact_belief_perception_binding_t *binding,
	const sg_perception_observation_t *observation)
{
	sg_perception_hypothesis_t *storage;
	sg_perception_observation_t candidate;
	sg_compact_belief_perception_result_t result;
	size_t required;
	size_t index;
	size_t write = 0U;

	result = AdaptPreview(binding, observation, &required);
	if (result != SG_COMPACT_BELIEF_PERCEPTION_APPLIED)
		return result;
	if (required > SIZE_MAX / sizeof(*storage))
		return SG_COMPACT_BELIEF_PERCEPTION_OVERFLOW;
	storage = malloc(required * sizeof(*storage));
	if (!storage)
		return SG_COMPACT_BELIEF_PERCEPTION_CAPACITY;
	for (index = 0U; index < observation->data.sound.hypothesis_count;
		index++)
	{
		const sg_perception_hypothesis_t *source =
			&observation->data.sound.hypotheses[index];
		float factor;
		double weighted;

		if (!SoundSupportFactor(&observation->data.sound, source, &factor))
			continue;
		weighted = (double)source->likelihood * (double)factor;
		if (!isfinite(weighted) || weighted <= 0.0 ||
			weighted > (double)FLT_MAX)
			continue;
		storage[write] = *source;
		storage[write].likelihood = (float)weighted;
		if (!isfinite(storage[write].likelihood) ||
			storage[write].likelihood <= 0.0f)
			continue;
		write++;
	}
	if (write == 0U)
	{
		free(storage);
		return SG_COMPACT_BELIEF_PERCEPTION_REJECTED;
	}
	candidate = *observation;
	candidate.data.sound.hypotheses = storage;
	candidate.data.sound.hypothesis_count = write;
	result = RuntimeObserve(binding, &candidate);
	free(storage);
	return result;
}

static int NegativeSightCellEnvelope(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_phase_coordinate_t *phase, float center_out[3],
	float *radius_out)
{
	const sg_rune_cell_t *cell;
	double radius_squared = 0.0;
	double radius;
	uint8_t axis;

	if (!snapshot || !snapshot->model || !phase || !center_out ||
		!radius_out || !SG_PhaseCoordinateValid(snapshot, phase) ||
		phase->cell_id >= snapshot->model->cell_count)
		return 0;
	cell = &snapshot->model->cells[phase->cell_id];
	for (axis = 0U; axis < 3U; axis++)
	{
		double minimum = (double)cell->bounds.mins.value[axis];
		double maximum = (double)cell->bounds.maxs.value[axis];
		double midpoint;
		double extent;

		if (!isfinite(minimum) || !isfinite(maximum) || minimum >= maximum)
			return 0;
		midpoint = minimum + (maximum - minimum) * 0.5;
		extent = (maximum - minimum) * 0.5;
		if (!isfinite(midpoint) || !isfinite(extent) ||
			!isfinite(radius_squared + extent * extent))
			return 0;
		center_out[axis] = (float)midpoint;
		if (!isfinite(center_out[axis]))
			return 0;
		radius_squared += extent * extent;
	}
	/* The reducer's spatial overlap treats points within its epsilon as a
	 * fully excluded region.  Scale the exact cell envelope to cover every
	 * point in that cell, instead of merely attenuating its interior mass. */
	radius = sqrt(radius_squared) /
		sqrt((double)SG_BELIEF_WEIGHT_EPSILON);
	if (!isfinite(radius) || radius <= 0.0 || radius > (double)FLT_MAX)
		return 0;
	*radius_out = nextafterf((float)radius, FLT_MAX);
	if (!isfinite(*radius_out) || *radius_out <= 0.0f ||
		*radius_out <= (float)radius ||
		!SG_BeliefFloatValid(*radius_out * *radius_out) ||
		!SG_BeliefPositionInsidePhaseCell(snapshot, phase, center_out))
		return 0;
	return 1;
}

static sg_compact_belief_perception_result_t ObserveNegativeSightDecoded(
	const sg_compact_belief_perception_binding_t *binding,
	const sg_perception_observation_t *observation)
{
	sg_perception_observation_t candidate;
	float center[3];
	float radius;

	if (observation->source != SG_PERCEPTION_SOURCE_SIGHT ||
		observation->evidence_kind != SG_BELIEF_EVIDENCE_NEGATIVE ||
		!NegativeSightCellEnvelope(binding->snapshot,
			&observation->data.sight.hypothesis.phase, center, &radius))
		return SG_COMPACT_BELIEF_PERCEPTION_REJECTED;
	candidate = *observation;
	memcpy(candidate.data.sight.hypothesis.position, center,
		sizeof(center));
	candidate.data.sight.hypothesis.spread_radius = radius;
	return RuntimeObserve(binding, &candidate);
}

static int DirectionalDamageSupports(const sg_perception_damage_t *damage,
	sg_perception_hypothesis_t *storage, size_t *count_out)
{
	double direction_squared = 0.0;
	double direction_length;
	int has_bearing = 0;
	int has_forward = 0;
	size_t index;
	size_t write = 0U;

	if (!damage || !storage || !count_out || !damage->hypotheses ||
		damage->hypothesis_count == 0U)
		return 0;
	for (index = 0U; index < 3U; index++)
		direction_squared += (double)damage->incoming_direction[index] *
			(double)damage->incoming_direction[index];
	if (!isfinite(direction_squared) || direction_squared <= 0.0)
		return 0;
	direction_length = sqrt(direction_squared);
	if (!isfinite(direction_length) || direction_length <= 0.0)
		return 0;
	for (index = 0U; index < damage->hypothesis_count; index++)
	{
		const sg_perception_hypothesis_t *source =
			&damage->hypotheses[index];
		double offset_squared = 0.0;
		double dot = 0.0;
		uint8_t axis;

		for (axis = 0U; axis < 3U; axis++)
		{
			double offset = (double)source->position[axis] -
				(double)damage->victim_position[axis];
			offset_squared += offset * offset;
			dot += offset * (double)damage->incoming_direction[axis];
		}
		if (!isfinite(offset_squared) || !isfinite(dot))
			return 0;
		if (offset_squared > 0.0)
		{
			double alignment = dot /
				(sqrt(offset_squared) * direction_length);

			has_bearing = 1;
			if (alignment > 0.0)
				has_forward = 1;
		}
	}
	if (!has_bearing)
	{
		memcpy(storage, damage->hypotheses,
			damage->hypothesis_count * sizeof(*storage));
		*count_out = damage->hypothesis_count;
		return 1;
	}
	if (!has_forward)
		return 0;
	for (index = 0U; index < damage->hypothesis_count; index++)
	{
		const sg_perception_hypothesis_t *source =
			&damage->hypotheses[index];
		double offset_squared = 0.0;
		double dot = 0.0;
		uint8_t axis;

		for (axis = 0U; axis < 3U; axis++)
		{
			double offset = (double)source->position[axis] -
				(double)damage->victim_position[axis];
			offset_squared += offset * offset;
			dot += offset * (double)damage->incoming_direction[axis];
		}
		if (offset_squared == 0.0)
		{
			storage[write++] = *source;
		}
		else
		{
			double alignment = dot /
				(sqrt(offset_squared) * direction_length);
			double weighted;

			if (alignment <= 0.0)
				continue;
			if (alignment > 1.0)
				alignment = 1.0;
			weighted = (double)source->likelihood * alignment;
			if (!isfinite(weighted) || weighted <= 0.0 ||
				weighted > (double)FLT_MAX)
				continue;
			storage[write] = *source;
			storage[write].likelihood = (float)weighted;
			if (isfinite(storage[write].likelihood) &&
				storage[write].likelihood > 0.0f)
				write++;
		}
	}
	if (write == 0U)
		return 0;
	*count_out = write;
	return 1;
}

static sg_compact_belief_perception_result_t ObserveDamageDecoded(
	const sg_compact_belief_perception_binding_t *binding,
	const sg_perception_observation_t *observation)
{
	sg_perception_hypothesis_t *storage;
	sg_perception_observation_t candidate;
	sg_compact_belief_perception_result_t result;
	size_t required;
	size_t directional_count;

	result = AdaptPreview(binding, observation, &required);
	if (result != SG_COMPACT_BELIEF_PERCEPTION_APPLIED)
		return result;
	if (required > SIZE_MAX / sizeof(*storage))
		return SG_COMPACT_BELIEF_PERCEPTION_OVERFLOW;
	storage = malloc(required * sizeof(*storage));
	if (!storage)
		return SG_COMPACT_BELIEF_PERCEPTION_CAPACITY;
	if (!DirectionalDamageSupports(&observation->data.damage, storage,
		&directional_count))
	{
		free(storage);
		return SG_COMPACT_BELIEF_PERCEPTION_REJECTED;
	}
	candidate = *observation;
	candidate.data.damage.hypotheses = storage;
	candidate.data.damage.hypothesis_count = directional_count;
	result = RuntimeObserve(binding, &candidate);
	free(storage);
	return result;
}

static sg_compact_belief_perception_result_t ObserveDecoded(
	const sg_compact_belief_perception_binding_t *binding,
	const sg_perception_observation_t *observation)
{
	if (observation->source == SG_PERCEPTION_SOURCE_SOUND)
		return ObserveSoundDecoded(binding, observation);
	if (observation->source == SG_PERCEPTION_SOURCE_DAMAGE)
		return ObserveDamageDecoded(binding, observation);
	if (observation->source == SG_PERCEPTION_SOURCE_SIGHT &&
		observation->evidence_kind == SG_BELIEF_EVIDENCE_NEGATIVE)
		return ObserveNegativeSightDecoded(binding, observation);
	return RuntimeObserve(binding, observation);
}

sg_compact_belief_perception_result_t
SG_CompactBeliefPerceptionBind(
	sg_compact_belief_perception_binding_t *binding,
	const sg_rune_runtime_snapshot_t *snapshot,
	sg_compact_belief_perception_evidence_decode_fn decode_evidence,
	void *decode_context)
{
	const sg_rune_runtime_snapshot_t *current;
	sg_compact_belief_perception_binding_t candidate;

	if (!binding || !SG_RuneRuntimeSnapshotValid(snapshot) ||
		!decode_evidence)
		return SG_COMPACT_BELIEF_PERCEPTION_REJECTED;
	current = SG_BeliefRuntimeSnapshot();
	if (!current)
		return SG_COMPACT_BELIEF_PERCEPTION_UNAVAILABLE;
	if (current != snapshot)
		return SG_COMPACT_BELIEF_PERCEPTION_IDENTITY_MISMATCH;
	memset(&candidate, 0, sizeof(candidate));
	candidate.snapshot = snapshot;
	candidate.model = snapshot->model;
	candidate.rune_identity = snapshot->identity;
	candidate.topology_revision = snapshot->topology_revision;
	candidate.decode_evidence = decode_evidence;
	candidate.decode_context = decode_context;
	candidate.bound = 1U;
	*binding = candidate;
	return SG_COMPACT_BELIEF_PERCEPTION_APPLIED;
}

void SG_CompactBeliefPerceptionUnbind(
	sg_compact_belief_perception_binding_t *binding)
{
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
	decoded_observation_t decoded;
	sg_compact_belief_perception_result_t result;

	result = DecodeObservation(binding, authority, &decoded);
	if (result != SG_COMPACT_BELIEF_PERCEPTION_APPLIED)
		return result;
	result = ObserveDecoded(binding, &decoded.observation);
	DecodedObservationClear(&decoded);
	return result;
}

sg_compact_belief_perception_result_t
SG_CompactBeliefPerceptionObserveSound(
	const sg_compact_belief_perception_binding_t *binding,
	const sg_compact_belief_perception_evidence_authority_t *authority)
{
	decoded_observation_t decoded;
	sg_compact_belief_perception_result_t result;

	result = DecodeObservation(binding, authority, &decoded);
	if (result != SG_COMPACT_BELIEF_PERCEPTION_APPLIED)
		return result;
	if (decoded.observation.source != SG_PERCEPTION_SOURCE_SOUND)
	{
		DecodedObservationClear(&decoded);
		return SG_COMPACT_BELIEF_PERCEPTION_REJECTED;
	}
	result = ObserveSoundDecoded(binding, &decoded.observation);
	DecodedObservationClear(&decoded);
	return result;
}

sg_compact_belief_perception_result_t
SG_CompactBeliefPerceptionObserveDamage(
	const sg_compact_belief_perception_binding_t *binding,
	const sg_compact_belief_perception_evidence_authority_t *authority)
{
	decoded_observation_t decoded;
	sg_compact_belief_perception_result_t result;

	result = DecodeObservation(binding, authority, &decoded);
	if (result != SG_COMPACT_BELIEF_PERCEPTION_APPLIED)
		return result;
	if (decoded.observation.source != SG_PERCEPTION_SOURCE_DAMAGE)
	{
		DecodedObservationClear(&decoded);
		return SG_COMPACT_BELIEF_PERCEPTION_REJECTED;
	}
	result = ObserveDamageDecoded(binding, &decoded.observation);
	DecodedObservationClear(&decoded);
	return result;
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

sg_compact_belief_perception_result_t SG_CompactBeliefPerceptionPredict(
	const sg_compact_belief_perception_binding_t *binding,
	uint8_t audience_team, const sg_belief_life_identity_t *target_life,
	uint64_t at_ms, sg_belief_particle_t *scratch_first,
	sg_belief_particle_t *scratch_second, size_t scratch_capacity,
	sg_belief_particle_t *particles, size_t particle_capacity,
	sg_belief_prediction_t *out)
{
	sg_compact_belief_perception_result_t status = BindingStatus(binding);

	if (status != SG_COMPACT_BELIEF_PERCEPTION_APPLIED)
		return status;
	if (!SG_BeliefTeamValid(audience_team) ||
		!SG_BeliefLifeIdentityValid(target_life) || !out)
		return SG_COMPACT_BELIEF_PERCEPTION_REJECTED;
	return PredictResult(SG_BeliefRuntimePredict(audience_team, target_life,
		at_ms, scratch_first, scratch_second, scratch_capacity, particles,
		particle_capacity, out));
}

const sg_belief_runtime_view_t *SG_CompactBeliefPerceptionView(
	const sg_compact_belief_perception_binding_t *binding,
	uint8_t audience_team, const sg_belief_life_identity_t *target_life)
{
	if (BindingStatus(binding) != SG_COMPACT_BELIEF_PERCEPTION_APPLIED)
		return NULL;
	return SG_BeliefRuntimeView(audience_team, target_life);
}

const sg_belief_runtime_view_t *SG_CompactBeliefPerceptionViewForClient(
	const sg_compact_belief_perception_binding_t *binding,
	uint8_t audience_team, uint32_t client_id)
{
	if (BindingStatus(binding) != SG_COMPACT_BELIEF_PERCEPTION_APPLIED)
		return NULL;
	return SG_BeliefRuntimeViewForClient(audience_team, client_id);
}
