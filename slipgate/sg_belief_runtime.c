#include "sg_belief_runtime.h"

#include <float.h>
#include <math.h>
#include <string.h>

/* Private cross-module declarations.  Neither raw-admission symbol is part
 * of sg_belief_runtime.h. */
sg_belief_runtime_observe_result_t SG_BeliefRuntimeObserveFromCompactOwner(
	const sg_belief_runtime_observation_t *observation);
sg_belief_runtime_observe_result_t SG_BeliefRuntimeObserve(
	const sg_belief_runtime_observation_t *observation);

typedef struct sg_belief_runtime_track_s
{
	uint8_t active;
	uint8_t exact_sight;
	uint8_t latest_source;
	uint8_t reserved;
	uint8_t audience_team;
	uint8_t target_team;
	uint8_t reserved2[6];
	sg_belief_runtime_life_t target_life;
	uint64_t observed_at_ms;
	uint64_t updated_at_ms;
	uint64_t decayed_at_ms;
	uint64_t valid_until_ms;
	uint64_t last_frame_sequence;
	float confidence;
	sg_belief_runtime_particle_t particles[SG_BELIEF_RUNTIME_MAX_PARTICLES];
	size_t particle_count;
	sg_belief_runtime_view_t view;
} sg_belief_runtime_track_t;

typedef struct sg_belief_runtime_life_fence_s
{
	uint8_t active;
	uint8_t retired;
	uint8_t reserved[6];
	uint32_t client_id;
	uint32_t reserved2;
	uint64_t latest_generation;
} sg_belief_runtime_life_fence_t;

typedef struct sg_belief_runtime_replay_fence_s
{
	uint8_t active;
	uint8_t audience_team;
	uint8_t target_team;
	uint8_t reserved[5];
	sg_belief_runtime_life_t target_life;
	uint64_t last_event_id;
	uint64_t last_evidence_sequence;
	uint64_t last_observed_at_ms;
} sg_belief_runtime_replay_fence_t;

static sg_belief_runtime_provider_t sg_belief_runtime_provider;
static sg_belief_runtime_track_t
	sg_belief_runtime_tracks[SG_BELIEF_RUNTIME_MAX_TRACKS];
static sg_belief_runtime_track_t
	sg_belief_runtime_frame_stage[SG_BELIEF_RUNTIME_MAX_TRACKS];
static sg_belief_runtime_life_fence_t
	sg_belief_runtime_life_fences[SG_BELIEF_RUNTIME_MAX_LIFE_FENCES];
static sg_belief_runtime_replay_fence_t
	sg_belief_runtime_replay_fences[SG_BELIEF_RUNTIME_MAX_REPLAY_FENCES];
static uint8_t sg_belief_runtime_provider_available;

typedef enum sg_belief_runtime_age_result_e
{
	SG_BELIEF_RUNTIME_AGE_APPLIED = 0,
	SG_BELIEF_RUNTIME_AGE_REJECTED,
	SG_BELIEF_RUNTIME_AGE_OVERFLOW
} sg_belief_runtime_age_result_t;

static int TeamValid(uint8_t team)
{
	return team == 1U || team == 2U;
}

static int LifeValid(const sg_belief_runtime_life_t *life)
{
	return life && life->reserved == 0U && life->client_id != UINT32_MAX &&
		life->spawn_generation != 0U;
}

static int LifeEqual(const sg_belief_runtime_life_t *left,
	const sg_belief_runtime_life_t *right)
{
	return LifeValid(left) && LifeValid(right) &&
		left->client_id == right->client_id &&
		left->spawn_generation == right->spawn_generation;
}

static int Float3Valid(const float value[3])
{
	return value && isfinite(value[0]) && isfinite(value[1]) &&
		isfinite(value[2]);
}

static int PolicyValid(const sg_belief_runtime_policy_t *policy)
{
	return policy && policy->confidence_decay_ms != 0U &&
		isfinite(policy->spread_growth_per_ms) &&
		policy->spread_growth_per_ms >= 0.0f;
}

static int ProviderValid(const sg_belief_runtime_provider_t *provider)
{
	const sg_rune_compact_model_t *model;

	if (!provider || !provider->model || !provider->locate || !provider->current ||
		provider->rune_identity == 0U || provider->topology_revision == 0U ||
		provider->generation == 0U || !PolicyValid(&provider->policy))
		return 0;
	model = provider->model;
	return provider->identity == &model->identity &&
		model->version == SG_RUNE_COMPACT_MODEL_VERSION &&
		model->schema_tag == SG_RUNE_COMPACT_MODEL_SCHEMA_TAG &&
		model->cells && model->cell_count != 0U;
}

static int ProviderCurrent(const sg_belief_runtime_provider_t *provider)
{
	return ProviderValid(provider) && provider->current(provider->context,
		provider->model, provider->identity, provider->rune_identity,
		provider->topology_revision, provider->generation) == 1;
}

static int CellStateValid(const sg_belief_runtime_cell_state_t *cell)
{
	if (!cell || !sg_belief_runtime_provider_available ||
		cell->location.cell.value == SG_RUNE_COMPACT_INDEX_NONE ||
		cell->location.cell.value >= sg_belief_runtime_provider.model->cell_count ||
		cell->location.valid_stances == 0U ||
		cell->location.reserved[0] != 0U || cell->location.reserved[1] != 0U ||
		cell->location.reserved[2] != 0U ||
		(cell->location.valid_stances & ~SG_RUNE_STANCE_VALID_ALL) != 0U ||
		(cell->known_components &
			~(uint32_t)SG_BELIEF_RUNTIME_CELL_COMPONENTS_KNOWN) != 0U)
		return 0;
	if (cell->location.valid_stances != sg_belief_runtime_provider.model->cells[
		cell->location.cell.value].valid_stances)
		return 0;
	if ((cell->known_components & SG_BELIEF_RUNTIME_CELL_STANCE) != 0U &&
		cell->stance >= SG_RUNE_STANCE_COUNT)
		return 0;
	if ((cell->known_components & SG_BELIEF_RUNTIME_CELL_MOTION) != 0U &&
		cell->motion >= SG_RUNE_MOTION_COUNT)
		return 0;
	if ((cell->known_components & SG_BELIEF_RUNTIME_CELL_SUPPORT) != 0U &&
		cell->support >= SG_RUNE_SUPPORT_COUNT)
		return 0;
	if ((cell->known_components & SG_BELIEF_RUNTIME_CELL_MEDIUM) != 0U &&
		cell->medium >= SG_RUNE_MEDIUM_COUNT)
		return 0;
	if ((cell->known_components & SG_BELIEF_RUNTIME_CELL_VOID_RELATION) != 0U &&
		cell->void_relation >= SG_RUNE_VOID_RELATION_COUNT)
		return 0;
	return (cell->known_components & SG_BELIEF_RUNTIME_CELL_REFERENCE_FRAME) == 0U ||
		cell->reference_frame < SG_RUNE_FRAME_COUNT;
}

static int SourceRequiresSpread(sg_belief_runtime_source_t source)
{
	return source == SG_BELIEF_RUNTIME_SOURCE_SOUND ||
		source == SG_BELIEF_RUNTIME_SOURCE_DAMAGE ||
		source == SG_BELIEF_RUNTIME_SOURCE_WEAPON_FIRE ||
		source == SG_BELIEF_RUNTIME_SOURCE_HOOK ||
		source == SG_BELIEF_RUNTIME_SOURCE_MECHANISM ||
		source == SG_BELIEF_RUNTIME_SOURCE_WATER;
}

static int PropagationValid(const sg_belief_runtime_particle_t *particle,
	const sg_belief_runtime_propagation_t *transition)
{
	sg_belief_runtime_cell_state_t located;
	const sg_rune_compact_portal_t *portal;
	uint32_t negative_cell;
	uint32_t positive_cell;
	uint32_t source_cell;
	uint32_t destination_cell;
	int legal_direction;

	if (!particle || !transition || !CellStateValid(&transition->cell) ||
		!Float3Valid(transition->position) || !Float3Valid(transition->velocity) ||
		!Float3Valid(transition->acceleration) ||
		!Float3Valid(transition->orientation) ||
		!isfinite(transition->likelihood) || transition->likelihood <= 0.0f)
		return 0;
	memset(&located, 0, sizeof(located));
	located.location.cell.value = SG_RUNE_COMPACT_INDEX_NONE;
	if (!sg_belief_runtime_provider.locate(sg_belief_runtime_provider.context,
		sg_belief_runtime_provider.model, transition->position, &located) ||
		!CellStateValid(&located) || located.location.cell.value !=
			transition->cell.location.cell.value ||
		located.location.valid_stances != transition->cell.location.valid_stances)
		return 0;
	source_cell = particle->cell.location.cell.value;
	destination_cell = transition->cell.location.cell.value;
	if (source_cell == destination_cell)
		return transition->portal.value == SG_RUNE_COMPACT_INDEX_NONE;
	if (transition->portal.value == SG_RUNE_COMPACT_INDEX_NONE ||
		transition->portal.value >= sg_belief_runtime_provider.model->portal_count ||
		!sg_belief_runtime_provider.model->portals ||
		!sg_belief_runtime_provider.model->incidences)
		return 0;
	portal = &sg_belief_runtime_provider.model->portals[transition->portal.value];
	if (portal->negative_incidence.value >=
		sg_belief_runtime_provider.model->incidence_count ||
		portal->positive_incidence.value >=
		sg_belief_runtime_provider.model->incidence_count ||
		portal->direction >= SG_RUNE_PORTAL_CONTINUITY_COUNT ||
		(portal->valid_stances & particle->cell.location.valid_stances &
			transition->cell.location.valid_stances) == 0U)
		return 0;
	negative_cell = sg_belief_runtime_provider.model->incidences[
		portal->negative_incidence.value].cell.value;
	positive_cell = sg_belief_runtime_provider.model->incidences[
		portal->positive_incidence.value].cell.value;
	legal_direction = (portal->direction == SG_RUNE_PORTAL_CONTINUITY_BOTH &&
		((source_cell == negative_cell && destination_cell == positive_cell) ||
		(source_cell == positive_cell && destination_cell == negative_cell))) ||
		(portal->direction == SG_RUNE_PORTAL_CONTINUITY_NEGATIVE_TO_POSITIVE &&
			source_cell == negative_cell && destination_cell == positive_cell) ||
		(portal->direction == SG_RUNE_PORTAL_CONTINUITY_POSITIVE_TO_NEGATIVE &&
			source_cell == positive_cell && destination_cell == negative_cell);
	return legal_direction;
}

static void TrackRefreshView(sg_belief_runtime_track_t *track)
{
	if (!track)
		return;
	memset(&track->view, 0, sizeof(track->view));
	if (track->active != 1U)
		return;
	track->view.audience_team = track->audience_team;
	track->view.target_team = track->target_team;
	track->view.exact_sight = track->exact_sight;
	track->view.latest_source = track->latest_source;
	track->view.target_life = track->target_life;
	track->view.observed_at_ms = track->observed_at_ms;
	track->view.updated_at_ms = track->updated_at_ms;
	track->view.valid_until_ms = track->valid_until_ms;
	track->view.confidence = track->confidence;
	track->view.particles = track->particles;
	track->view.particle_count = track->particle_count;
}

static void TrackClear(sg_belief_runtime_track_t *track)
{
	if (track)
		memset(track, 0, sizeof(*track));
}

static sg_belief_runtime_life_fence_t *FindLifeFence(uint32_t client_id)
{
	size_t index;

	for (index = 0U; index < SG_BELIEF_RUNTIME_MAX_LIFE_FENCES; index++)
		if (sg_belief_runtime_life_fences[index].active == 1U &&
			sg_belief_runtime_life_fences[index].client_id == client_id)
			return &sg_belief_runtime_life_fences[index];
	return NULL;
}

static sg_belief_runtime_life_fence_t *FindVacantLifeFence(void)
{
	size_t index;

	for (index = 0U; index < SG_BELIEF_RUNTIME_MAX_LIFE_FENCES; index++)
		if (sg_belief_runtime_life_fences[index].active != 1U)
			return &sg_belief_runtime_life_fences[index];
	return NULL;
}

static void ClearOlderLifeTracks(uint32_t client_id, uint64_t generation,
	int include_generation)
{
	size_t index;

	for (index = 0U; index < SG_BELIEF_RUNTIME_MAX_TRACKS; index++)
	{
		sg_belief_runtime_track_t *track = &sg_belief_runtime_tracks[index];

		if (track->active == 1U && track->target_life.client_id == client_id &&
			(track->target_life.spawn_generation < generation ||
			(include_generation != 0 &&
				track->target_life.spawn_generation == generation)))
			TrackClear(track);
	}
}

/* This only validates or reserves no state.  An observation cannot advance a
 * generation fence until its complete sparse evidence has been accepted. */
static sg_belief_runtime_observe_result_t LifeMayCommit(
	const sg_belief_runtime_life_t *life)
{
	sg_belief_runtime_life_fence_t *fence;

	if (!LifeValid(life))
		return SG_BELIEF_RUNTIME_OBSERVE_REJECTED;
	fence = FindLifeFence(life->client_id);
	if (!fence)
		return FindVacantLifeFence() ? SG_BELIEF_RUNTIME_OBSERVE_APPLIED :
			SG_BELIEF_RUNTIME_OBSERVE_CAPACITY;
	if (life->spawn_generation < fence->latest_generation ||
		(life->spawn_generation == fence->latest_generation &&
			fence->retired == 1U))
		return SG_BELIEF_RUNTIME_OBSERVE_REJECTED;
	return SG_BELIEF_RUNTIME_OBSERVE_APPLIED;
}

static sg_belief_runtime_observe_result_t LivesMayCommit(
	const sg_belief_runtime_life_t *issuer,
	const sg_belief_runtime_life_t *target)
{
	sg_belief_runtime_observe_result_t result = LifeMayCommit(issuer);
	int needed = 0;
	size_t index;

	if (result != SG_BELIEF_RUNTIME_OBSERVE_APPLIED)
		return result;
	result = LifeMayCommit(target);
	if (result != SG_BELIEF_RUNTIME_OBSERVE_APPLIED)
		return result;
	if (!FindLifeFence(issuer->client_id))
		needed++;
	if (target->client_id != issuer->client_id &&
		!FindLifeFence(target->client_id))
		needed++;
	for (index = 0U; index < SG_BELIEF_RUNTIME_MAX_LIFE_FENCES; index++)
		if (sg_belief_runtime_life_fences[index].active != 1U)
			needed--;
	return needed > 0 ? SG_BELIEF_RUNTIME_OBSERVE_CAPACITY :
		SG_BELIEF_RUNTIME_OBSERVE_APPLIED;
}

static void CommitLife(const sg_belief_runtime_life_t *life)
{
	sg_belief_runtime_life_fence_t *fence = FindLifeFence(life->client_id);

	if (!fence)
		fence = FindVacantLifeFence();
	if (!fence)
		return; /* LifeMayCommit guaranteed capacity before this point. */
	if (fence->active != 1U)
	{
		memset(fence, 0, sizeof(*fence));
		fence->active = 1U;
		fence->client_id = life->client_id;
		fence->latest_generation = life->spawn_generation;
		return;
	}
	if (life->spawn_generation > fence->latest_generation)
	{
		ClearOlderLifeTracks(life->client_id, life->spawn_generation, 0);
		fence->latest_generation = life->spawn_generation;
		fence->retired = 0U;
	}
}

static sg_belief_runtime_replay_fence_t *FindReplayFence(uint8_t audience_team,
	const sg_belief_runtime_life_t *target_life)
{
	size_t index;

	for (index = 0U; index < SG_BELIEF_RUNTIME_MAX_REPLAY_FENCES; index++)
	{
		sg_belief_runtime_replay_fence_t *fence =
			&sg_belief_runtime_replay_fences[index];

		if (fence->active == 1U && fence->audience_team == audience_team &&
			LifeEqual(&fence->target_life, target_life))
			return fence;
	}
	return NULL;
}

static sg_belief_runtime_replay_fence_t *FindVacantReplayFence(void)
{
	size_t index;

	for (index = 0U; index < SG_BELIEF_RUNTIME_MAX_REPLAY_FENCES; index++)
		if (sg_belief_runtime_replay_fences[index].active != 1U)
			return &sg_belief_runtime_replay_fences[index];
	return NULL;
}

static sg_belief_runtime_observe_result_t ReplayMayCommit(
	const sg_belief_runtime_observation_t *observation)
{
	sg_belief_runtime_replay_fence_t *fence = FindReplayFence(
		observation->audience_team, &observation->target_life);

	if (!fence)
		return FindVacantReplayFence() ? SG_BELIEF_RUNTIME_OBSERVE_APPLIED :
			SG_BELIEF_RUNTIME_OBSERVE_CAPACITY;
	if (fence->target_team != observation->target_team ||
		observation->event_id <= fence->last_event_id ||
		observation->evidence_sequence <= fence->last_evidence_sequence ||
		observation->observed_at_ms < fence->last_observed_at_ms)
		return SG_BELIEF_RUNTIME_OBSERVE_REJECTED;
	return SG_BELIEF_RUNTIME_OBSERVE_APPLIED;
}

static void CommitReplay(const sg_belief_runtime_observation_t *observation)
{
	sg_belief_runtime_replay_fence_t *fence = FindReplayFence(
		observation->audience_team, &observation->target_life);

	if (!fence)
		fence = FindVacantReplayFence();
	if (!fence)
		return; /* ReplayMayCommit guaranteed capacity before this point. */
	if (fence->active != 1U)
	{
		memset(fence, 0, sizeof(*fence));
		fence->active = 1U;
		fence->audience_team = observation->audience_team;
		fence->target_team = observation->target_team;
		fence->target_life = observation->target_life;
	}
	fence->last_event_id = observation->event_id;
	fence->last_evidence_sequence = observation->evidence_sequence;
	fence->last_observed_at_ms = observation->observed_at_ms;
}

static void CommitObservation(const sg_belief_runtime_observation_t *observation)
{
	CommitLife(&observation->issuer_life);
	CommitLife(&observation->target_life);
	CommitReplay(observation);
}

static sg_belief_runtime_track_t *FindTrack(uint8_t audience_team,
	const sg_belief_runtime_life_t *target_life)
{
	size_t index;

	for (index = 0U; index < SG_BELIEF_RUNTIME_MAX_TRACKS; index++)
	{
		sg_belief_runtime_track_t *track = &sg_belief_runtime_tracks[index];

		if (track->active == 1U && track->audience_team == audience_team &&
			LifeEqual(&track->target_life, target_life))
			return track;
	}
	return NULL;
}

static sg_belief_runtime_track_t *FindVacantTrack(void)
{
	size_t index;

	for (index = 0U; index < SG_BELIEF_RUNTIME_MAX_TRACKS; index++)
		if (sg_belief_runtime_tracks[index].active != 1U)
			return &sg_belief_runtime_tracks[index];
	return NULL;
}

/* Positive evidence owns updated_at_ms.  This private timestamp records how
 * far the current particles have already aged without making a miss look
 * freshly observed. */
static sg_belief_runtime_age_result_t TrackDecayTo(
	sg_belief_runtime_track_t *track, uint64_t at_ms, int hold_exact_sight)
{
	sg_belief_runtime_particle_t propagated[SG_BELIEF_RUNTIME_MAX_PARTICLES];
	uint64_t elapsed;
	double decay;
	double confidence;
	double total_weight = 0.0;
	size_t propagated_count = 0U;
	size_t index;

	if (!track || track->active != 1U || at_ms < track->decayed_at_ms)
		return SG_BELIEF_RUNTIME_AGE_REJECTED;
	elapsed = at_ms - track->decayed_at_ms;
	if (elapsed == 0U)
		return SG_BELIEF_RUNTIME_AGE_APPLIED;
	/* A second source in the same perception pass does not prove that visual
	 * contact was lost.  The frame boundary or negative sight does. */
	if (hold_exact_sight && track->exact_sight == 1U)
		return SG_BELIEF_RUNTIME_AGE_APPLIED;
	if (sg_belief_runtime_provider.propagate)
	{
		for (index = 0U; index < track->particle_count; index++)
		{
			sg_belief_runtime_propagation_t
				transitions[SG_BELIEF_RUNTIME_MAX_PARTICLES];
			size_t transition_count = 0U;
			size_t transition_index;

			memset(transitions, 0, sizeof(transitions));
			if (!sg_belief_runtime_provider.propagate(
				sg_belief_runtime_provider.context,
				sg_belief_runtime_provider.model, &track->particles[index],
				track->decayed_at_ms, at_ms, transitions,
				SG_BELIEF_RUNTIME_MAX_PARTICLES, &transition_count) ||
				transition_count == 0U)
				return SG_BELIEF_RUNTIME_AGE_REJECTED;
			if (transition_count > SG_BELIEF_RUNTIME_MAX_PARTICLES ||
				transition_count > SG_BELIEF_RUNTIME_MAX_PARTICLES -
				propagated_count)
				return SG_BELIEF_RUNTIME_AGE_OVERFLOW;
			for (transition_index = 0U; transition_index < transition_count;
				transition_index++)
			{
				const sg_belief_runtime_propagation_t *transition =
					&transitions[transition_index];
				sg_belief_runtime_particle_t *particle =
					&propagated[propagated_count];
				double weight;

				if (!PropagationValid(&track->particles[index], transition))
					return SG_BELIEF_RUNTIME_AGE_REJECTED;
				weight = (double)track->particles[index].weight *
					(double)transition->likelihood;
				if (!isfinite(weight) || weight <= 0.0 ||
					weight > (double)FLT_MAX ||
					!isfinite(total_weight + weight))
					return SG_BELIEF_RUNTIME_AGE_OVERFLOW;
				*particle = track->particles[index];
				particle->cell = transition->cell;
				memcpy(particle->position, transition->position,
					sizeof(particle->position));
				memcpy(particle->velocity, transition->velocity,
					sizeof(particle->velocity));
				memcpy(particle->acceleration, transition->acceleration,
					sizeof(particle->acceleration));
				memcpy(particle->orientation, transition->orientation,
					sizeof(particle->orientation));
				particle->weight = (float)weight;
				particle->future_at_ms = at_ms;
				total_weight += weight;
				propagated_count++;
			}
		}
		if (!isfinite(total_weight) || total_weight <= 0.0)
			return SG_BELIEF_RUNTIME_AGE_OVERFLOW;
		for (index = 0U; index < propagated_count; index++)
			propagated[index].weight = (float)(
				(double)propagated[index].weight / total_weight);
		memcpy(track->particles, propagated,
			propagated_count * sizeof(propagated[0]));
		track->particle_count = propagated_count;
	}
	else
	{
		for (index = 0U; index < track->particle_count; index++)
			track->particles[index].future_at_ms = at_ms;
	}
	decay = exp(-(double)elapsed /
		(double)sg_belief_runtime_provider.policy.confidence_decay_ms);
	if (!isfinite(decay) || decay < 0.0 || decay > 1.0)
		return SG_BELIEF_RUNTIME_AGE_REJECTED;
	confidence = (double)track->confidence * decay;
	if (!isfinite(confidence) || confidence < 0.0 || confidence >
		(double)FLT_MAX)
		return SG_BELIEF_RUNTIME_AGE_REJECTED;
	if (confidence <= (double)FLT_MIN)
	{
		TrackClear(track);
		return SG_BELIEF_RUNTIME_AGE_APPLIED;
	}
	for (index = 0U; index < track->particle_count; index++)
	{
		double spread = (double)track->particles[index].spread_radius +
			(double)elapsed *
			(double)sg_belief_runtime_provider.policy.spread_growth_per_ms;

		if (!isfinite(spread) || spread > (double)FLT_MAX)
			return SG_BELIEF_RUNTIME_AGE_OVERFLOW;
		track->particles[index].spread_radius = (float)spread;
	}
	track->confidence = (float)confidence;
	track->decayed_at_ms = at_ms;
	/* Once time advances beyond the direct visual sample, even a single
	 * successor is a prediction rather than exact aim. */
	track->exact_sight = 0U;
	return SG_BELIEF_RUNTIME_AGE_APPLIED;
}

static int ObservationValid(const sg_belief_runtime_observation_t *observation)
{
	sg_belief_runtime_source_t evidence_source;
	size_t index;

	if (!observation || observation->authenticated != 1U ||
		observation->authority >= SG_BELIEF_RUNTIME_AUTHORITY_COUNT ||
		!TeamValid(observation->audience_team) ||
		!TeamValid(observation->target_team) ||
		observation->audience_team == observation->target_team ||
		observation->source >= SG_BELIEF_RUNTIME_SOURCE_COUNT ||
		observation->reported_source >= SG_BELIEF_RUNTIME_SOURCE_COUNT ||
		observation->evidence_kind >= SG_BELIEF_RUNTIME_EVIDENCE_KIND_COUNT ||
		observation->reserved[0] != 0U || observation->reserved[1] != 0U ||
		!LifeValid(&observation->issuer_life) ||
		!LifeValid(&observation->target_life) || observation->event_id == 0U ||
		observation->evidence_sequence == 0U ||
		observation->observed_at_ms == 0U ||
		observation->authenticated_at_ms < observation->observed_at_ms ||
		observation->valid_until_ms <= observation->authenticated_at_ms ||
		observation->rune_identity != sg_belief_runtime_provider.rune_identity ||
		observation->topology_revision !=
			sg_belief_runtime_provider.topology_revision ||
		!isfinite(observation->confidence) || observation->confidence <= 0.0f ||
		observation->confidence > 1.0f)
		return 0;
	if (observation->issuer_life.client_id ==
		observation->target_life.client_id)
		return 0;
	if (observation->source == SG_BELIEF_RUNTIME_SOURCE_TEAMMATE)
	{
		if (observation->authority !=
			SG_BELIEF_RUNTIME_AUTHORITY_HOST_TEAMMATE_REPORT ||
			observation->reported_source == SG_BELIEF_RUNTIME_SOURCE_TEAMMATE ||
			LifeEqual(&observation->issuer_life, &observation->target_life))
			return 0;
		evidence_source = observation->reported_source;
	}
	else
	{
		if (observation->authority != SG_BELIEF_RUNTIME_AUTHORITY_HOST_SENSOR ||
			observation->reported_source != SG_BELIEF_RUNTIME_SOURCE_SIGHT)
			return 0;
		evidence_source = observation->source;
	}
	if (observation->evidence_kind == SG_BELIEF_RUNTIME_EVIDENCE_POSITIVE)
	{
		if (!observation->hypotheses || observation->hypothesis_count == 0U ||
			observation->hypothesis_count > SG_BELIEF_RUNTIME_MAX_PARTICLES ||
			observation->coverage || observation->coverage_count != 0U)
			return 0;
		for (index = 0U; index < observation->hypothesis_count; index++)
			if (SourceRequiresSpread(evidence_source) &&
				observation->hypotheses[index].spread_radius <= 0.0f)
				return 0;
	}
	else
	{
		if (observation->source != SG_BELIEF_RUNTIME_SOURCE_SIGHT ||
			observation->hypotheses || observation->hypothesis_count != 0U ||
			!observation->coverage || observation->coverage_count == 0U ||
			observation->coverage_count > SG_BELIEF_RUNTIME_MAX_COVERAGE)
			return 0;
		for (index = 0U; index < observation->coverage_count; index++)
		{
			const sg_rune_compact_location_t *location =
				&observation->coverage[index].location;

			if (location->cell.value == SG_RUNE_COMPACT_INDEX_NONE ||
				location->cell.value >= sg_belief_runtime_provider.model->cell_count ||
				location->valid_stances == 0U ||
				location->reserved[0] != 0U || location->reserved[1] != 0U ||
				location->reserved[2] != 0U ||
				location->valid_stances !=
					sg_belief_runtime_provider.model->cells[
						location->cell.value].valid_stances)
				return 0;
		}
	}
	return 1;
}

int SG_BeliefRuntimeProviderSet(const sg_belief_runtime_provider_t *provider)
{
	if (!provider)
	{
		SG_BeliefRuntimeReset();
		return 1;
	}
	if (!ProviderCurrent(provider))
		return 0;
	SG_BeliefRuntimeReset();
	sg_belief_runtime_provider = *provider;
	sg_belief_runtime_provider_available = 1U;
	return 1;
}

int SG_BeliefRuntimeProviderAvailable(void)
{
	return sg_belief_runtime_provider_available == 1U &&
		ProviderCurrent(&sg_belief_runtime_provider);
}

const sg_belief_runtime_provider_t *SG_BeliefRuntimeProvider(void)
{
	return SG_BeliefRuntimeProviderAvailable() ? &sg_belief_runtime_provider : NULL;
}

void SG_BeliefRuntimeReset(void)
{
	memset(&sg_belief_runtime_provider, 0, sizeof(sg_belief_runtime_provider));
	memset(sg_belief_runtime_tracks, 0, sizeof(sg_belief_runtime_tracks));
	memset(sg_belief_runtime_frame_stage, 0,
		sizeof(sg_belief_runtime_frame_stage));
	memset(sg_belief_runtime_life_fences, 0,
		sizeof(sg_belief_runtime_life_fences));
	memset(sg_belief_runtime_replay_fences, 0,
		sizeof(sg_belief_runtime_replay_fences));
	sg_belief_runtime_provider_available = 0U;
}

/* Not declared in the public runtime header: only the compact perception
 * adapter reaches this after it has consumed one owner-issued opaque token. */
sg_belief_runtime_observe_result_t SG_BeliefRuntimeObserveFromCompactOwner(
	const sg_belief_runtime_observation_t *observation)
{
	sg_belief_runtime_particle_t particles[SG_BELIEF_RUNTIME_MAX_PARTICLES];
	sg_belief_runtime_track_t incoming;
	sg_belief_runtime_track_t candidate;
	sg_belief_runtime_track_t *track;
	sg_belief_runtime_age_result_t age_result;
	size_t index;
	double total = 0.0;

	if (!SG_BeliefRuntimeProviderAvailable())
		return SG_BELIEF_RUNTIME_OBSERVE_UNAVAILABLE;
	if (!ObservationValid(observation))
		return SG_BELIEF_RUNTIME_OBSERVE_REJECTED;
	{
		sg_belief_runtime_observe_result_t fence_result = LivesMayCommit(
			&observation->issuer_life, &observation->target_life);

		if (fence_result != SG_BELIEF_RUNTIME_OBSERVE_APPLIED)
			return fence_result;
		fence_result = ReplayMayCommit(observation);
		if (fence_result != SG_BELIEF_RUNTIME_OBSERVE_APPLIED)
			return fence_result;
	}
	track = FindTrack(observation->audience_team, &observation->target_life);
	if (track && observation->authenticated_at_ms >= track->valid_until_ms)
	{
		if (observation->evidence_kind == SG_BELIEF_RUNTIME_EVIDENCE_NEGATIVE)
		{
			TrackClear(track);
			CommitObservation(observation);
			return SG_BELIEF_RUNTIME_OBSERVE_APPLIED;
		}
		TrackClear(track);
		track = NULL; /* The incoming positive evidence reuses a vacant slot. */
	}
	if (track && (track->target_team != observation->target_team ||
		observation->authenticated_at_ms < track->decayed_at_ms))
		return SG_BELIEF_RUNTIME_OBSERVE_REJECTED;
	if (observation->evidence_kind == SG_BELIEF_RUNTIME_EVIDENCE_NEGATIVE)
	{
		size_t kept = 0U;
		double kept_weight = 0.0;

		if (!track)
		{
			CommitObservation(observation);
			return SG_BELIEF_RUNTIME_OBSERVE_APPLIED;
		}
		candidate = *track;
		age_result = TrackDecayTo(&candidate, observation->authenticated_at_ms,
			0);
		if (age_result == SG_BELIEF_RUNTIME_AGE_OVERFLOW)
			return SG_BELIEF_RUNTIME_OBSERVE_OVERFLOW;
		if (age_result != SG_BELIEF_RUNTIME_AGE_APPLIED)
			return SG_BELIEF_RUNTIME_OBSERVE_REJECTED;
		if (candidate.active != 1U)
		{
			*track = candidate;
			CommitObservation(observation);
			return SG_BELIEF_RUNTIME_OBSERVE_APPLIED;
		}

		for (index = 0U; index < candidate.particle_count; index++)
		{
			size_t covered;

			for (covered = 0U; covered < observation->coverage_count; covered++)
				if (candidate.particles[index].cell.location.cell.value ==
					observation->coverage[covered].location.cell.value &&
					candidate.particles[index].cell.location.valid_stances ==
					observation->coverage[covered].location.valid_stances)
					break;
			if (covered == observation->coverage_count)
			{
				candidate.particles[kept++] = candidate.particles[index];
				kept_weight += (double)candidate.particles[kept - 1U].weight;
			}
		}
		if (kept == 0U)
		{
			TrackClear(&candidate);
			*track = candidate;
			CommitObservation(observation);
			return SG_BELIEF_RUNTIME_OBSERVE_APPLIED;
		}
		if (!isfinite(kept_weight) || kept_weight <= 0.0)
			return SG_BELIEF_RUNTIME_OBSERVE_OVERFLOW;
		candidate.confidence = (float)((double)candidate.confidence * kept_weight);
		if (!isfinite(candidate.confidence) || candidate.confidence <= FLT_MIN)
		{
			TrackClear(&candidate);
			*track = candidate;
			CommitObservation(observation);
			return SG_BELIEF_RUNTIME_OBSERVE_APPLIED;
		}
		for (index = 0U; index < kept; index++)
			candidate.particles[index].weight = (float)(
				(double)candidate.particles[index].weight / kept_weight);
		candidate.particle_count = kept;
		candidate.exact_sight = 0U;
		*track = candidate;
		TrackRefreshView(track);
		CommitObservation(observation);
		return SG_BELIEF_RUNTIME_OBSERVE_APPLIED;
	}
	for (index = 0U; index < observation->hypothesis_count; index++)
	{
		const sg_belief_runtime_hypothesis_t *hypothesis =
			&observation->hypotheses[index];
		sg_belief_runtime_cell_state_t cell;
		double weighted;

		if (!Float3Valid(hypothesis->position) || !Float3Valid(hypothesis->velocity) ||
			!Float3Valid(hypothesis->acceleration) ||
			!Float3Valid(hypothesis->orientation) ||
			!isfinite(hypothesis->spread_radius) || hypothesis->spread_radius < 0.0f ||
			!isfinite(hypothesis->likelihood) || hypothesis->likelihood <= 0.0f ||
			hypothesis->weapon_state >= SG_BELIEF_RUNTIME_WEAPON_STATE_COUNT)
			return SG_BELIEF_RUNTIME_OBSERVE_REJECTED;
		memset(&cell, 0, sizeof(cell));
		cell.location.cell.value = SG_RUNE_COMPACT_INDEX_NONE;
		if (!sg_belief_runtime_provider.locate(
			sg_belief_runtime_provider.context, sg_belief_runtime_provider.model,
			hypothesis->position, &cell) || !CellStateValid(&cell))
			return SG_BELIEF_RUNTIME_OBSERVE_REJECTED;
		weighted = (double)hypothesis->likelihood *
			(double)observation->confidence;
		if (!isfinite(weighted) || weighted <= 0.0 ||
			weighted > (double)FLT_MAX || !isfinite(total + weighted))
			return SG_BELIEF_RUNTIME_OBSERVE_OVERFLOW;
		memset(&particles[index], 0, sizeof(particles[index]));
		particles[index].cell = cell;
		memcpy(particles[index].position, hypothesis->position,
			sizeof(particles[index].position));
		memcpy(particles[index].velocity, hypothesis->velocity,
			sizeof(particles[index].velocity));
		memcpy(particles[index].acceleration, hypothesis->acceleration,
			sizeof(particles[index].acceleration));
		memcpy(particles[index].orientation, hypothesis->orientation,
			sizeof(particles[index].orientation));
		particles[index].spread_radius = hypothesis->spread_radius;
		particles[index].weight = (float)weighted;
		particles[index].observed_at_ms = observation->observed_at_ms;
		particles[index].future_at_ms = observation->observed_at_ms;
		particles[index].weapon_state = hypothesis->weapon_state;
		total += weighted;
	}
	if (!isfinite(total) || total <= 0.0)
		return SG_BELIEF_RUNTIME_OBSERVE_OVERFLOW;
	for (index = 0U; index < observation->hypothesis_count; index++)
		particles[index].weight = (float)((double)particles[index].weight / total);
	memset(&incoming, 0, sizeof(incoming));
	incoming.active = 1U;
	incoming.audience_team = observation->audience_team;
	incoming.target_team = observation->target_team;
	incoming.target_life = observation->target_life;
	incoming.confidence = observation->confidence;
	incoming.observed_at_ms = observation->observed_at_ms;
	incoming.updated_at_ms = observation->authenticated_at_ms;
	incoming.decayed_at_ms = observation->observed_at_ms;
	incoming.valid_until_ms = observation->valid_until_ms;
	incoming.latest_source = (uint8_t)observation->source;
	incoming.exact_sight = observation->source ==
		SG_BELIEF_RUNTIME_SOURCE_SIGHT && observation->hypothesis_count == 1U &&
		particles[0].spread_radius == 0.0f ? 1U : 0U;
	memcpy(incoming.particles, particles,
		observation->hypothesis_count * sizeof(particles[0]));
	incoming.particle_count = observation->hypothesis_count;
	age_result = TrackDecayTo(&incoming, observation->authenticated_at_ms, 0);
	if (age_result == SG_BELIEF_RUNTIME_AGE_OVERFLOW)
		return SG_BELIEF_RUNTIME_OBSERVE_OVERFLOW;
	if (age_result != SG_BELIEF_RUNTIME_AGE_APPLIED || incoming.active != 1U)
		return SG_BELIEF_RUNTIME_OBSERVE_REJECTED;
	if (track && observation->source != SG_BELIEF_RUNTIME_SOURCE_SIGHT)
	{
		candidate = *track;
		age_result = TrackDecayTo(&candidate, observation->authenticated_at_ms,
			candidate.exact_sight == 1U);
		if (age_result == SG_BELIEF_RUNTIME_AGE_OVERFLOW)
			return SG_BELIEF_RUNTIME_OBSERVE_OVERFLOW;
		if (age_result != SG_BELIEF_RUNTIME_AGE_APPLIED)
			return SG_BELIEF_RUNTIME_OBSERVE_REJECTED;
		if (candidate.active != 1U)
			track = NULL;
	}
	else if (track)
		candidate = *track;
	if (!track)
	{
		track = FindVacantTrack();
		if (!track)
			return SG_BELIEF_RUNTIME_OBSERVE_CAPACITY;
		candidate = incoming;
	}
	else if (observation->source == SG_BELIEF_RUNTIME_SOURCE_SIGHT)
	{
		candidate.particle_count = incoming.particle_count;
		memcpy(candidate.particles, incoming.particles,
			incoming.particle_count * sizeof(incoming.particles[0]));
		candidate.confidence = incoming.confidence;
		candidate.exact_sight = incoming.exact_sight;
	}
	else
	{
		sg_belief_runtime_particle_t fused[SG_BELIEF_RUNTIME_MAX_PARTICLES];
		size_t fused_count = 0U;

		/* Direct sight remains the live aiming authority for this perception
		 * pass.  Keep the coarse event's replay fence, but do not turn a known
		 * pose into an estimate until a frame or negative visual check retires
		 * that contact. */
		if (candidate.exact_sight == 1U)
		{
			*track = candidate;
			TrackRefreshView(track);
			CommitObservation(observation);
			return SG_BELIEF_RUNTIME_OBSERVE_APPLIED;
		}
		if (candidate.particle_count > SG_BELIEF_RUNTIME_MAX_PARTICLES -
			incoming.particle_count)
			return SG_BELIEF_RUNTIME_OBSERVE_OVERFLOW;
		for (index = 0U; index < candidate.particle_count; index++)
		{
			fused[fused_count] = candidate.particles[index];
			fused[fused_count].weight *= candidate.confidence;
			fused_count++;
		}
		for (index = 0U; index < incoming.particle_count; index++)
		{
			fused[fused_count] = incoming.particles[index];
			fused[fused_count].weight *= incoming.confidence;
			fused_count++;
		}
		total = 0.0;
		for (index = 0U; index < fused_count; index++)
		{
			if (!isfinite(fused[index].weight) || fused[index].weight <= 0.0f ||
				!isfinite(total + (double)fused[index].weight))
				return SG_BELIEF_RUNTIME_OBSERVE_OVERFLOW;
			total += (double)fused[index].weight;
		}
		if (!isfinite(total) || total <= 0.0)
			return SG_BELIEF_RUNTIME_OBSERVE_OVERFLOW;
		for (index = 0U; index < fused_count; index++)
			fused[index].weight = (float)((double)fused[index].weight / total);
		memcpy(candidate.particles, fused, fused_count * sizeof(fused[0]));
		candidate.particle_count = fused_count;
		candidate.confidence = 1.0f -
			(1.0f - candidate.confidence) * (1.0f - incoming.confidence);
		candidate.exact_sight = 0U;
	}
	candidate.observed_at_ms = observation->observed_at_ms;
	candidate.updated_at_ms = observation->authenticated_at_ms;
	candidate.decayed_at_ms = observation->authenticated_at_ms;
	if (candidate.valid_until_ms < observation->valid_until_ms)
		candidate.valid_until_ms = observation->valid_until_ms;
	candidate.latest_source = (uint8_t)observation->source;
	*track = candidate;
	TrackRefreshView(track);
	CommitObservation(observation);
	return SG_BELIEF_RUNTIME_OBSERVE_APPLIED;
}

/* A legacy direct caller can still link this symbol while production callers
 * migrate, but caller-filled flags and identity numbers grant no admission. */
sg_belief_runtime_observe_result_t SG_BeliefRuntimeObserve(
	const sg_belief_runtime_observation_t *observation)
{
	(void)observation;
	return SG_BELIEF_RUNTIME_OBSERVE_REJECTED;
}

sg_belief_runtime_frame_result_t SG_BeliefRuntimeFrame(uint64_t frame_sequence,
	uint64_t at_ms)
{
	size_t index;

	if (!SG_BeliefRuntimeProviderAvailable())
		return SG_BELIEF_RUNTIME_FRAME_UNAVAILABLE;
	if (frame_sequence == 0U || at_ms == 0U)
		return SG_BELIEF_RUNTIME_FRAME_REJECTED;
	/* Stage every track before committing any mass, time bucket, confidence, or
	 * view.  A bad movement successor for one target cannot partially age the
	 * rest of the team's beliefs. */
	memcpy(sg_belief_runtime_frame_stage, sg_belief_runtime_tracks,
		sizeof(sg_belief_runtime_frame_stage));
	for (index = 0U; index < SG_BELIEF_RUNTIME_MAX_TRACKS; index++)
	{
		sg_belief_runtime_track_t *track =
			&sg_belief_runtime_frame_stage[index];
		sg_belief_runtime_age_result_t age_result;

		if (track->active != 1U)
			continue;
		if (at_ms >= track->valid_until_ms)
		{
			TrackClear(track);
			continue;
		}
		if (frame_sequence <= track->last_frame_sequence ||
			at_ms < track->decayed_at_ms)
			return SG_BELIEF_RUNTIME_FRAME_REJECTED;
		age_result = TrackDecayTo(track, at_ms, 0);
		switch (age_result)
		{
		case SG_BELIEF_RUNTIME_AGE_APPLIED:
			break;
		case SG_BELIEF_RUNTIME_AGE_OVERFLOW:
			return SG_BELIEF_RUNTIME_FRAME_OVERFLOW;
		case SG_BELIEF_RUNTIME_AGE_REJECTED:
			return SG_BELIEF_RUNTIME_FRAME_REJECTED;
		}
		if (track->active != 1U)
			continue;
		if (track->particle_count == 0U)
			return SG_BELIEF_RUNTIME_FRAME_REJECTED;
		track->last_frame_sequence = frame_sequence;
	}
	memcpy(sg_belief_runtime_tracks, sg_belief_runtime_frame_stage,
		sizeof(sg_belief_runtime_tracks));
	for (index = 0U; index < SG_BELIEF_RUNTIME_MAX_TRACKS; index++)
		if (sg_belief_runtime_tracks[index].active == 1U)
			TrackRefreshView(&sg_belief_runtime_tracks[index]);
	return SG_BELIEF_RUNTIME_FRAME_APPLIED;
}

int SG_BeliefRuntimeRetireLife(const sg_belief_runtime_life_t *life)
{
	sg_belief_runtime_life_fence_t *fence;

	if (!LifeValid(life))
		return 0;
	fence = FindLifeFence(life->client_id);
	if (!fence)
		fence = FindVacantLifeFence();
	if (!fence)
		return 0;
	if (fence->active != 1U)
	{
		memset(fence, 0, sizeof(*fence));
		fence->active = 1U;
		fence->client_id = life->client_id;
		fence->latest_generation = life->spawn_generation;
		fence->retired = 1U;
		ClearOlderLifeTracks(life->client_id, life->spawn_generation, 1);
		return 1;
	}
	if (life->spawn_generation < fence->latest_generation)
		return 1; /* The old life was already superseded. */
	if (life->spawn_generation > fence->latest_generation)
		fence->latest_generation = life->spawn_generation;
	fence->retired = 1U;
	ClearOlderLifeTracks(life->client_id, life->spawn_generation, 1);
	return 1;
}

const sg_belief_runtime_view_t *SG_BeliefRuntimeView(uint8_t audience_team,
	const sg_belief_runtime_life_t *target_life, uint64_t at_ms)
{
	sg_belief_runtime_track_t *track;

	if (!SG_BeliefRuntimeProviderAvailable() || !TeamValid(audience_team) ||
		!LifeValid(target_life) || at_ms == 0U)
		return NULL;
	track = FindTrack(audience_team, target_life);
	return track && track->particle_count != 0U && at_ms < track->valid_until_ms ?
		&track->view : NULL;
}

const sg_belief_runtime_view_t *SG_BeliefRuntimeViewForClient(
	uint8_t audience_team, uint32_t client_id, uint64_t at_ms)
{
	size_t index;

	if (!SG_BeliefRuntimeProviderAvailable() || !TeamValid(audience_team) ||
		client_id == UINT32_MAX || at_ms == 0U)
		return NULL;
	for (index = 0U; index < SG_BELIEF_RUNTIME_MAX_TRACKS; index++)
	{
		sg_belief_runtime_track_t *track = &sg_belief_runtime_tracks[index];

		if (track->active == 1U && track->audience_team == audience_team &&
			track->target_life.client_id == client_id &&
			track->particle_count != 0U && at_ms < track->valid_until_ms)
			return &track->view;
	}
	return NULL;
}
