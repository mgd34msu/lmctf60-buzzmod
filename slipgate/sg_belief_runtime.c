#include "sg_belief_runtime.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct belief_runtime_track_s
{
	uint8_t active;
	uint8_t reserved[7];
	sg_belief_state_t state;
	sg_belief_particle_t *particles;
	sg_belief_particle_t *scratch_first;
	sg_belief_particle_t *scratch_second;
	sg_belief_particle_t *prediction_particles;
	size_t capacity;
	uint64_t localization_generation;
	sg_belief_horizon_scope_t *horizon_scope;
	sg_belief_life_identity_t *issuers;
	size_t issuer_count;
	size_t issuer_capacity;
	sg_belief_runtime_view_t view;
} belief_runtime_track_t;

typedef struct belief_runtime_life_fence_s
{
	uint64_t spawn_generation;
	uint8_t retired;
	uint8_t reserved[7];
} belief_runtime_life_fence_t;

typedef struct belief_runtime_sequence_fence_s
{
	uint64_t target_spawn_generation;
	uint64_t last_evidence_sequence;
} belief_runtime_sequence_fence_t;

typedef struct belief_runtime_life_publication_s
{
	sg_belief_life_identity_t lives[2];
	size_t count;
} belief_runtime_life_publication_t;

typedef struct belief_runtime_frame_candidate_s
{
	belief_runtime_track_t track;
	sg_belief_horizon_scope_prepared_t *horizon;
} belief_runtime_frame_candidate_t;

static sg_belief_runtime_provider_t belief_runtime_provider;
static belief_runtime_track_t belief_runtime_tracks[2][SG_BELIEF_MAX_CLIENTS];
static belief_runtime_life_fence_t
	belief_runtime_life_fences[SG_BELIEF_MAX_CLIENTS];
static uint64_t
	belief_runtime_audience_client_watermarks[2][SG_BELIEF_MAX_CLIENTS];
static belief_runtime_sequence_fence_t
	belief_runtime_audience_target_sequence_fences[2][SG_BELIEF_MAX_CLIENTS];
static uint64_t belief_runtime_reduction_sequence;
static uint64_t belief_runtime_frame_sequence;
static uint64_t belief_runtime_frame_time;
static uint64_t belief_runtime_provider_epoch;

typedef struct belief_runtime_provider_capture_s
{
	sg_belief_runtime_provider_t provider;
	uint64_t rune_identity;
	uint64_t topology_revision;
	uint64_t epoch;
} belief_runtime_provider_capture_t;

static int BeliefRuntimeProviderValid(const sg_belief_runtime_provider_t *provider)
{
	return provider && SG_RuneRuntimeSnapshotValid(provider->snapshot) &&
		provider->localization_generation != 0U && provider->locate &&
		provider->policy.confidence_decay_ms != 0U &&
		isfinite(provider->policy.diffusion_fraction) &&
		provider->policy.diffusion_fraction >= 0.0f &&
		provider->policy.diffusion_fraction <= 1.0f &&
		isfinite(provider->policy.spread_growth_per_ms) &&
		provider->policy.spread_growth_per_ms >= 0.0f;
}

static int BeliefRuntimeTeamIndex(uint8_t team, size_t *index_out)
{
	if (!index_out || !SG_BeliefTeamValid(team))
		return 0;
	*index_out = (size_t)(team - 1U);
	return 1;
}

static int BeliefRuntimeCurrent(void)
{
	return BeliefRuntimeProviderValid(&belief_runtime_provider);
}

static int BeliefRuntimeProviderCapture(
	belief_runtime_provider_capture_t *capture)
{
	if (!capture || !BeliefRuntimeCurrent())
		return 0;
	capture->provider = belief_runtime_provider;
	capture->rune_identity = capture->provider.snapshot->identity;
	capture->topology_revision =
		capture->provider.snapshot->topology_revision;
	capture->epoch = belief_runtime_provider_epoch;
	return 1;
}

static int BeliefRuntimeProviderCaptureCurrent(
	const belief_runtime_provider_capture_t *capture)
{
	return capture && BeliefRuntimeCurrent() &&
		belief_runtime_provider_epoch == capture->epoch &&
		capture->epoch != 0U &&
		belief_runtime_provider.snapshot == capture->provider.snapshot &&
		belief_runtime_provider.localization_generation ==
			capture->provider.localization_generation &&
		belief_runtime_provider.locate == capture->provider.locate &&
		belief_runtime_provider.context == capture->provider.context &&
		belief_runtime_provider.snapshot->identity == capture->rune_identity &&
		belief_runtime_provider.snapshot->topology_revision ==
			capture->topology_revision;
}

static void BeliefRuntimeClearTrack(belief_runtime_track_t *track)
{
	if (!track)
		return;
	free(track->particles);
	free(track->scratch_first);
	free(track->scratch_second);
	free(track->prediction_particles);
	free(track->issuers);
	SG_BeliefHorizonScopeDestroy(track->horizon_scope);
	memset(track, 0, sizeof(*track));
}

static void BeliefRuntimeTransientReset(void)
{
	size_t team;
	size_t client;

	for (team = 0U; team < 2U; team++)
		for (client = 0U; client < SG_BELIEF_MAX_CLIENTS; client++)
			BeliefRuntimeClearTrack(&belief_runtime_tracks[team][client]);
	belief_runtime_reduction_sequence = 0U;
	belief_runtime_frame_sequence = 0U;
	belief_runtime_frame_time = 0U;
}

void SG_BeliefRuntimeReset(void)
{
	BeliefRuntimeTransientReset();
	memset(belief_runtime_life_fences, 0,
		sizeof(belief_runtime_life_fences));
	memset(belief_runtime_audience_client_watermarks, 0,
		sizeof(belief_runtime_audience_client_watermarks));
	memset(belief_runtime_audience_target_sequence_fences, 0,
		sizeof(belief_runtime_audience_target_sequence_fences));
}

int SG_BeliefRuntimeProviderSet(const sg_belief_runtime_provider_t *provider)
{
	sg_belief_runtime_provider_t candidate;
	uint64_t next_epoch;

	if (!provider)
	{
		memset(&belief_runtime_provider, 0, sizeof(belief_runtime_provider));
		SG_BeliefRuntimeReset();
		return 1;
	}
	if (!BeliefRuntimeProviderValid(provider))
		return 0;
	if (belief_runtime_provider_epoch == UINT64_MAX)
		return 0;
	candidate = *provider;
	next_epoch = belief_runtime_provider_epoch + 1U;
	BeliefRuntimeTransientReset();
	belief_runtime_provider = candidate;
	belief_runtime_provider_epoch = next_epoch;
	return 1;
}

int SG_BeliefRuntimeProviderAvailable(void)
{
	return BeliefRuntimeCurrent();
}

const sg_rune_runtime_snapshot_t *SG_BeliefRuntimeSnapshot(void)
{
	return BeliefRuntimeCurrent() ? belief_runtime_provider.snapshot : NULL;
}

#if defined(SG_BELIEF_TESTING)
void SG_BeliefTestRuntimeProviderEpochExhaust(void)
{
	belief_runtime_provider_epoch = UINT64_MAX;
}
#endif

static int BeliefRuntimeFinite3(const float value[3])
{
	return value && isfinite(value[0]) && isfinite(value[1]) &&
		isfinite(value[2]);
}

int SG_BeliefRuntimeHypothesis(const sg_belief_runtime_pose_t *pose,
	sg_perception_hypothesis_t *out)
{
	belief_runtime_provider_capture_t capture;
	sg_phase_coordinate_t phase;

	if (!pose || !out || !BeliefRuntimeProviderCapture(&capture) ||
		pose->movement_state < SG_BELIEF_MOTION_UNKNOWN ||
		pose->movement_state >= SG_BELIEF_MOTION_COUNT ||
		pose->reserved[0] != 0U || pose->reserved[1] != 0U ||
		pose->reserved[2] != 0U || !BeliefRuntimeFinite3(pose->position) ||
		!BeliefRuntimeFinite3(pose->velocity) ||
		!BeliefRuntimeFinite3(pose->acceleration) ||
		!BeliefRuntimeFinite3(pose->orientation))
		return 0;
	memset(&phase, 0, sizeof(phase));
	if (!capture.provider.locate(capture.provider.context,
		capture.provider.snapshot, pose->position, &phase) ||
		!BeliefRuntimeProviderCaptureCurrent(&capture) ||
		!SG_PhaseCoordinateValid(capture.provider.snapshot, &phase))
		return 0;
	memset(out, 0, sizeof(*out));
	out->phase = phase;
	out->location_basis = SG_PERCEPTION_LOCATION_EARNED_RUNTIME;
	out->movement_state = pose->movement_state;
	out->weapon_state = pose->weapon_state;
	memcpy(out->position, pose->position, sizeof(out->position));
	memcpy(out->velocity, pose->velocity, sizeof(out->velocity));
	memcpy(out->acceleration, pose->acceleration, sizeof(out->acceleration));
	memcpy(out->orientation, pose->orientation, sizeof(out->orientation));
	out->spread_radius = 0.0f;
	out->likelihood = 1.0f;
	return 1;
}

static belief_runtime_track_t *BeliefRuntimeTrack(uint8_t audience_team,
	const sg_belief_life_identity_t *target_life)
{
	size_t team_index;

	if (!BeliefRuntimeTeamIndex(audience_team, &team_index) ||
		!SG_BeliefLifeIdentityValid(target_life))
		return NULL;
	return &belief_runtime_tracks[team_index][target_life->client_id];
}

static int BeliefRuntimeLifeFenceAdmits(
	const sg_belief_life_identity_t *life)
{
	const belief_runtime_life_fence_t *fence;

	if (!SG_BeliefLifeIdentityValid(life))
		return 0;
	fence = &belief_runtime_life_fences[life->client_id];
	if (fence->spawn_generation == 0U)
		return 1;
	if (life->spawn_generation < fence->spawn_generation)
		return 0;
	/* This fence publishes only global life identity.  Evidence timing and
	 * sequence are ordered by the audience-owned active track, so one team's
	 * observation cannot suppress another team's independent first track. */
	if (life->spawn_generation > fence->spawn_generation)
		return 1;
	if (life->spawn_generation == fence->spawn_generation && fence->retired)
		return 0;
	return 1;
}

static int BeliefRuntimeLifePublicationPrepare(
	const sg_belief_evidence_t *evidence,
	belief_runtime_life_publication_t *publication)
{
	const sg_belief_life_identity_t *issuer;

	if (!evidence || !publication ||
		!SG_BeliefLifeIdentityValid(&evidence->target_life) ||
		!SG_BeliefLifeIdentityValid(
			&evidence->provenance.issuer_life))
		return 0;
	publication->lives[0] = evidence->target_life;
	publication->count = 1U;
	issuer = &evidence->provenance.issuer_life;
	/* One client slot cannot name two exact lives in one observation.  Equal
	 * target/issuer identities share a single fence publication. */
	if (issuer->client_id == publication->lives[0].client_id)
	{
		if (!SG_BeliefLifeIdentityEqual(issuer, &publication->lives[0]))
			return 0;
	}
	else
	{
		publication->lives[publication->count] = *issuer;
		publication->count++;
	}
	return 1;
}

static int BeliefRuntimeLifePublicationAdmits(
	const belief_runtime_life_publication_t *publication)
{
	size_t index;

	if (!publication || publication->count == 0U ||
		publication->count > sizeof(publication->lives) /
			sizeof(publication->lives[0]))
		return 0;
	for (index = 0U; index < publication->count; index++)
		if (!BeliefRuntimeLifeFenceAdmits(&publication->lives[index]))
			return 0;
	return 1;
}

static void BeliefRuntimeLifeFenceCommit(
	const sg_belief_life_identity_t *life)
{
	belief_runtime_life_fence_t *fence;

	if (!SG_BeliefLifeIdentityValid(life))
		return;
	fence = &belief_runtime_life_fences[life->client_id];
	if (life->spawn_generation > fence->spawn_generation)
	{
		fence->spawn_generation = life->spawn_generation;
		fence->retired = 0U;
	}
}

static void BeliefRuntimeLifeFenceRetire(
	const sg_belief_life_identity_t *life)
{
	belief_runtime_life_fence_t *fence;

	if (!SG_BeliefLifeIdentityValid(life))
		return;
	fence = &belief_runtime_life_fences[life->client_id];
	if (life->spawn_generation < fence->spawn_generation)
		return;
	if (life->spawn_generation > fence->spawn_generation)
		fence->spawn_generation = life->spawn_generation;
	fence->retired = 1U;
}

static int BeliefRuntimeLifeFenceCurrent(
	const sg_belief_life_identity_t *life)
{
	const belief_runtime_life_fence_t *fence;

	if (!SG_BeliefLifeIdentityValid(life))
		return 0;
	fence = &belief_runtime_life_fences[life->client_id];
	return fence->spawn_generation == life->spawn_generation &&
		fence->retired == 0U;
}

static int BeliefRuntimeTrackRecordsSupersededLife(
	const belief_runtime_track_t *track,
	const sg_belief_life_identity_t *life)
{
	size_t issuer;

	if (!track || !SG_BeliefLifeIdentityValid(life))
		return 0;
	if (track->state.target_life.client_id == life->client_id &&
		track->state.target_life.spawn_generation < life->spawn_generation)
		return 1;
	if (track->issuer_count == 0U)
		return 0;
	if (!track->issuers || track->issuer_count > track->issuer_capacity)
		return 1;
	for (issuer = 0U; issuer < track->issuer_count; issuer++)
		if (track->issuers[issuer].client_id == life->client_id &&
			track->issuers[issuer].spawn_generation <
				life->spawn_generation)
			return 1;
	return 0;
}

static int BeliefRuntimeTrackSupersededByPublication(
	const belief_runtime_track_t *track,
	const belief_runtime_life_publication_t *publication)
{
	size_t index;

	if (!track || !publication || publication->count == 0U ||
		publication->count > sizeof(publication->lives) /
			sizeof(publication->lives[0]))
		return 0;
	for (index = 0U; index < publication->count; index++)
		if (BeliefRuntimeTrackRecordsSupersededLife(track,
			&publication->lives[index]))
			return 1;
	return 0;
}

static void BeliefRuntimeClearSupersededLifeTracks(
	const sg_belief_life_identity_t *life)
{
	size_t team;
	size_t client;

	if (!SG_BeliefLifeIdentityValid(life))
		return;
	for (team = 0U; team < 2U; team++)
		for (client = 0U; client < SG_BELIEF_MAX_CLIENTS; client++)
		{
			belief_runtime_track_t *track =
				&belief_runtime_tracks[team][client];

			if (track->active == 1U &&
				BeliefRuntimeTrackRecordsSupersededLife(track, life))
				BeliefRuntimeClearTrack(track);
		}
}

static int BeliefRuntimeTrackIssuersCurrent(
	const belief_runtime_track_t *track)
{
	size_t issuer;

	if (!track || track->issuer_count == 0U || !track->issuers ||
		track->issuer_count > track->issuer_capacity)
		return 0;
	for (issuer = 0U; issuer < track->issuer_count; issuer++)
		if (!BeliefRuntimeLifeFenceCurrent(&track->issuers[issuer]))
			return 0;
	return 1;
}

static void BeliefRuntimeLifePublicationCommit(
	const belief_runtime_life_publication_t *publication)
{
	size_t index;

	/* Both roles have already been authenticated and all fallible candidate
	 * work has completed.  Publish every generation before clearing stale
	 * tracks, including tracks derived from a retired issuer, so one observation
	 * cannot expose a partial life order. */
	for (index = 0U; index < publication->count; index++)
		BeliefRuntimeLifeFenceCommit(&publication->lives[index]);
	for (index = 0U; index < publication->count; index++)
		BeliefRuntimeClearSupersededLifeTracks(&publication->lives[index]);
}

static int BeliefRuntimeTrackMatches(const belief_runtime_track_t *track,
	uint8_t audience_team, const sg_belief_life_identity_t *target_life)
{
	return track && track->active == 1U &&
		SG_BeliefStateValid(&track->state) &&
		track->state.audience_team == audience_team &&
		SG_BeliefLifeIdentityEqual(&track->state.target_life, target_life) &&
		BeliefRuntimeLifeFenceCurrent(target_life) &&
		BeliefRuntimeTrackIssuersCurrent(track) &&
		track->localization_generation ==
			belief_runtime_provider.localization_generation &&
		track->state.rune_identity == belief_runtime_provider.snapshot->identity &&
		track->state.topology_revision ==
			belief_runtime_provider.snapshot->topology_revision;
}

static int BeliefRuntimeTrackOrderingIdentityMatches(
	const belief_runtime_track_t *track, uint8_t audience_team,
	const sg_belief_life_identity_t *target_life)
{
	return track && track->active == 1U && SG_BeliefStateValid(&track->state) &&
		track->state.audience_team == audience_team &&
		SG_BeliefLifeIdentityEqual(&track->state.target_life, target_life);
}

static void BeliefRuntimeAudienceClientWatermarkAdvance(size_t team,
	size_t client, uint64_t at_ms)
{
	if (team >= 2U || client >= SG_BELIEF_MAX_CLIENTS)
		return;
	if (at_ms > belief_runtime_audience_client_watermarks[team][client])
		belief_runtime_audience_client_watermarks[team][client] = at_ms;
}

static int BeliefRuntimeEvidenceFollowsAudienceClientWatermark(
	const sg_belief_evidence_t *evidence)
{
	size_t team;
	uint64_t watermark;

	if (!evidence || !BeliefRuntimeTeamIndex(
		evidence->provenance.audience_team, &team) ||
		!SG_BeliefLifeIdentityValid(&evidence->target_life))
		return 0;
	watermark = belief_runtime_audience_client_watermarks[team]
		[evidence->target_life.client_id];
	return evidence->provenance.authenticated_at_ms >= watermark &&
		evidence->observed_at_ms >= watermark &&
		evidence->valid_until_ms >=
			evidence->provenance.authenticated_at_ms;
}

static int BeliefRuntimeAudienceTargetSequenceAdmits(
	const sg_belief_evidence_t *evidence)
{
	const belief_runtime_sequence_fence_t *fence;
	size_t team;

	if (!evidence || !BeliefRuntimeTeamIndex(
		evidence->provenance.audience_team, &team) ||
		!SG_BeliefLifeIdentityValid(&evidence->target_life) ||
		evidence->provenance.evidence_sequence == 0U)
	{
		return 0;
	}
	fence = &belief_runtime_audience_target_sequence_fences[team]
		[evidence->target_life.client_id];
	if (fence->target_spawn_generation == 0U ||
		evidence->target_life.spawn_generation >
			fence->target_spawn_generation)
	{
		return 1;
	}
	return evidence->target_life.spawn_generation ==
		fence->target_spawn_generation &&
		evidence->provenance.evidence_sequence >
			fence->last_evidence_sequence;
}

static void BeliefRuntimeAudienceTargetSequenceCommit(size_t team,
	const sg_belief_evidence_t *evidence)
{
	belief_runtime_sequence_fence_t *fence;

	if (team >= 2U || !evidence ||
		!SG_BeliefLifeIdentityValid(&evidence->target_life) ||
		evidence->provenance.evidence_sequence == 0U)
	{
		return;
	}
	fence = &belief_runtime_audience_target_sequence_fences[team]
		[evidence->target_life.client_id];
	if (evidence->target_life.spawn_generation >
		fence->target_spawn_generation)
	{
		fence->target_spawn_generation =
			evidence->target_life.spawn_generation;
		fence->last_evidence_sequence =
			evidence->provenance.evidence_sequence;
	}
	else if (evidence->target_life.spawn_generation ==
		fence->target_spawn_generation &&
		evidence->provenance.evidence_sequence >
			fence->last_evidence_sequence)
	{
		fence->last_evidence_sequence =
			evidence->provenance.evidence_sequence;
	}
}

static int BeliefRuntimeGrowTrack(belief_runtime_track_t *track,
	size_t required_capacity)
{
	sg_belief_particle_t *particles;
	sg_belief_particle_t *scratch_first;
	sg_belief_particle_t *scratch_second;
	sg_belief_particle_t *prediction_particles;
	size_t view_particle_count;
	size_t bytes;
	int view_uses_prediction;

	if (!track || required_capacity == 0U ||
		required_capacity > SIZE_MAX / sizeof(*particles))
		return 0;
	if (track->capacity >= required_capacity && track->particles &&
		track->scratch_first && track->scratch_second &&
		track->prediction_particles)
		return 1;
	view_uses_prediction = track->active &&
		track->view.particles == track->prediction_particles &&
		track->view.particle_count <= track->capacity;
	view_particle_count = view_uses_prediction ?
		track->view.particle_count : 0U;
	bytes = required_capacity * sizeof(*particles);
	particles = malloc(bytes);
	scratch_first = malloc(bytes);
	scratch_second = malloc(bytes);
	prediction_particles = malloc(bytes);
	if (!particles || !scratch_first || !scratch_second ||
		!prediction_particles)
	{
		free(particles);
		free(scratch_first);
		free(scratch_second);
		free(prediction_particles);
		return 0;
	}
	if (track->active && track->state.particle_count != 0U)
		memcpy(particles, track->state.particles,
			track->state.particle_count * sizeof(*particles));
	if (view_particle_count != 0U)
		memcpy(prediction_particles, track->prediction_particles,
			view_particle_count * sizeof(*prediction_particles));
	free(track->particles);
	free(track->scratch_first);
	free(track->scratch_second);
	free(track->prediction_particles);
	track->particles = particles;
	track->scratch_first = scratch_first;
	track->scratch_second = scratch_second;
	track->prediction_particles = prediction_particles;
	track->capacity = required_capacity;
	if (view_uses_prediction)
		track->view.particles = prediction_particles;
	if (track->active)
	{
		track->state.particles = track->particles;
		track->state.particle_capacity = required_capacity;
	}
	return 1;
}

static int BeliefRuntimeBuildTrack(belief_runtime_track_t *track,
	const sg_belief_evidence_t *evidence, size_t required_capacity)
{
	sg_belief_state_config_t config;

	if (!track || !evidence || !BeliefRuntimeCurrent())
		return 0;
	if (!BeliefRuntimeGrowTrack(track, required_capacity))
		return 0;
	track->horizon_scope = SG_BeliefHorizonScopeCreate();
	if (!track->horizon_scope)
	{
		BeliefRuntimeClearTrack(track);
		return 0;
	}
	memset(&config, 0, sizeof(config));
	config.audience_team = evidence->provenance.audience_team;
	config.target_team = evidence->target_team;
	config.target_life = evidence->target_life;
	config.initialized_at_ms = evidence->observed_at_ms;
	config.policy = belief_runtime_provider.policy;
	if (!SG_BeliefStateInit(belief_runtime_provider.snapshot, &track->state,
		&config, track->particles, track->capacity))
	{
		BeliefRuntimeClearTrack(track);
		return 0;
	}
	track->active = 1U;
	track->localization_generation =
		belief_runtime_provider.localization_generation;
	return 1;
}

static int BeliefRuntimePrepareIssuer(const belief_runtime_track_t *track,
	const sg_belief_life_identity_t *issuer, int *append_out,
	sg_belief_life_identity_t **prepared_issuers_out,
	size_t *prepared_capacity_out)
{
	sg_belief_life_identity_t *issuers;
	size_t index;
	size_t bytes;

	if (!track || !append_out || !prepared_issuers_out ||
		!prepared_capacity_out || !SG_BeliefLifeIdentityValid(issuer))
		return 0;
	*append_out = 0;
	*prepared_issuers_out = NULL;
	*prepared_capacity_out = 0U;
	for (index = 0U; index < track->issuer_count; index++)
		if (SG_BeliefLifeIdentityEqual(&track->issuers[index], issuer))
			return 1;
	if (track->issuer_count == SIZE_MAX ||
		track->issuer_count + 1U > SIZE_MAX / sizeof(*issuers))
		return 0;
	if (track->issuer_capacity >= track->issuer_count + 1U)
	{
		*append_out = 1;
		return 1;
	}
	bytes = (track->issuer_count + 1U) * sizeof(*issuers);
	issuers = malloc(bytes);
	if (!issuers)
		return 0;
	if (track->issuer_count != 0U)
		memcpy(issuers, track->issuers,
			track->issuer_count * sizeof(*issuers));
	*prepared_issuers_out = issuers;
	*prepared_capacity_out = track->issuer_count + 1U;
	*append_out = 1;
	return 1;
}

static void BeliefRuntimeCommitIssuer(belief_runtime_track_t *track,
	const sg_belief_life_identity_t *issuer, int append,
	sg_belief_life_identity_t *prepared_issuers, size_t prepared_capacity)
{
	if (!track || !issuer || !append)
		return;
	if (prepared_issuers)
	{
		free(track->issuers);
		track->issuers = prepared_issuers;
		track->issuer_capacity = prepared_capacity;
	}
	if (track->issuer_count >= track->issuer_capacity)
		return;
	track->issuers[track->issuer_count] = *issuer;
	track->issuer_count++;
}

static int BeliefRuntimeIssuerRecorded(const belief_runtime_track_t *track,
	const sg_belief_life_identity_t *life)
{
	size_t index;

	if (!track || !SG_BeliefLifeIdentityValid(life))
		return 0;
	for (index = 0U; index < track->issuer_count; index++)
		if (SG_BeliefLifeIdentityEqual(&track->issuers[index], life))
			return 1;
	return 0;
}

static int BeliefRuntimeTentativeReductionSequence(uint64_t *sequence_out)
{
	if (!sequence_out || belief_runtime_reduction_sequence == UINT64_MAX)
		return 0;
	*sequence_out = belief_runtime_reduction_sequence + 1U;
	return 1;
}

static sg_belief_reduce_result_t BeliefRuntimeReduce(
	belief_runtime_track_t *track, sg_belief_frame_t *frame,
	sg_belief_reduction_t *reduction)
{
	sg_belief_reduce_result_t result;
	size_t required_capacity;

	if (!track || !frame || !reduction || !track->active ||
		track->capacity == 0U)
		return SG_BELIEF_REDUCE_REJECTED_INVALID;
	for (;;)
	{
		frame->scratch_first = track->scratch_first;
		frame->scratch_second = track->scratch_second;
		frame->scratch_capacity = track->capacity;
		result = SG_BeliefReduce(belief_runtime_provider.snapshot,
			&track->state, frame, reduction);
		if (result != SG_BELIEF_REDUCE_CAPACITY)
			return result;
		required_capacity = reduction->required_scratch_capacity;
		if (reduction->required_particle_capacity > required_capacity)
			required_capacity = reduction->required_particle_capacity;
		if (required_capacity <= track->capacity ||
			!BeliefRuntimeGrowTrack(track, required_capacity))
			return SG_BELIEF_REDUCE_CAPACITY;
	}
}

static sg_belief_runtime_frame_result_t BeliefRuntimeHorizonPrepare(
	belief_runtime_track_t *track, uint64_t at_time_ms,
	uint64_t evidence_observed_at_ms,
	const sg_belief_horizon_authority_t **authority_out,
	const sg_belief_horizon_kernel_t **kernels_out, size_t *kernel_count_out)
{
	sg_belief_horizon_accept_result_t result;

	if (!track || !authority_out || !kernels_out || !kernel_count_out ||
		!track->active || !SG_BeliefStateValid(&track->state) ||
		!track->horizon_scope || !BeliefRuntimeCurrent() ||
		at_time_ms < track->state.updated_at_ms)
		return SG_BELIEF_RUNTIME_FRAME_REJECTED;
	*authority_out = NULL;
	*kernels_out = NULL;
	*kernel_count_out = 0U;
	if (at_time_ms == track->state.updated_at_ms)
		return SG_BELIEF_RUNTIME_FRAME_APPLIED;
	result = SG_BeliefHorizonScopePrepare(track->horizon_scope,
		belief_runtime_provider.snapshot, &track->state, at_time_ms,
		evidence_observed_at_ms);
	if (result != SG_BELIEF_HORIZON_ACCEPTED)
		return result == SG_BELIEF_HORIZON_ALLOCATION_FAILED ?
			SG_BELIEF_RUNTIME_FRAME_CAPACITY :
			result == SG_BELIEF_HORIZON_OVERFLOW ?
				SG_BELIEF_RUNTIME_FRAME_OVERFLOW :
				SG_BELIEF_RUNTIME_FRAME_REJECTED;
	*authority_out = SG_BeliefHorizonScopeAuthority(track->horizon_scope);
	*kernels_out = SG_BeliefHorizonScopeKernels(track->horizon_scope,
		kernel_count_out);
	return *authority_out && *kernels_out && *kernel_count_out != 0U ?
		SG_BELIEF_RUNTIME_FRAME_APPLIED : SG_BELIEF_RUNTIME_FRAME_REJECTED;
}

static sg_belief_runtime_frame_result_t BeliefRuntimePredict(
	belief_runtime_track_t *track, uint64_t at_time_ms,
	const sg_belief_horizon_authority_t *authority,
	sg_belief_runtime_view_t *view_out)
{
	sg_belief_prediction_request_t request;
	sg_belief_prediction_t prediction;
	sg_belief_predict_result_t result;
	sg_belief_runtime_view_t view;
	size_t required_capacity;

	if (!track || !track->active || !SG_BeliefStateValid(&track->state) ||
		!BeliefRuntimeCurrent())
		return SG_BELIEF_RUNTIME_FRAME_REJECTED;
	for (;;)
	{
		memset(&request, 0, sizeof(request));
		request.at_time_ms = at_time_ms;
		request.horizon = authority;
		request.scratch_first = track->scratch_first;
		request.scratch_second = track->scratch_second;
		request.scratch_capacity = track->capacity;
		request.particles = track->prediction_particles;
		request.particle_capacity = track->capacity;
		memset(&prediction, 0, sizeof(prediction));
		result = SG_BeliefPredict(belief_runtime_provider.snapshot,
			&track->state, &request, &prediction);
		if (result != SG_BELIEF_PREDICT_CAPACITY)
			break;
		required_capacity = prediction.required_scratch_capacity;
		if (prediction.required_particle_capacity > required_capacity)
			required_capacity = prediction.required_particle_capacity;
		if (required_capacity <= track->capacity ||
			!BeliefRuntimeGrowTrack(track, required_capacity))
			return SG_BELIEF_RUNTIME_FRAME_CAPACITY;
	}
	if (result != SG_BELIEF_PREDICT_APPLIED)
		return result == SG_BELIEF_PREDICT_OVERFLOW ?
			SG_BELIEF_RUNTIME_FRAME_OVERFLOW :
			SG_BELIEF_RUNTIME_FRAME_REJECTED;
	if (!view_out)
		return SG_BELIEF_RUNTIME_FRAME_APPLIED;
	if (prediction.particle_count == 0U)
	{
		memset(view_out, 0, sizeof(*view_out));
		return SG_BELIEF_RUNTIME_FRAME_APPLIED;
	}
	memset(&view, 0, sizeof(view));
	view.audience_team = track->state.audience_team;
	view.target_team = track->state.target_team;
	view.exact_sight = track->state.latest_source == SG_BELIEF_SOURCE_SIGHT &&
		track->state.latest_observed_at_ms == prediction.at_time_ms;
	view.latest_source = (uint8_t)track->state.latest_source;
	view.target_life = track->state.target_life;
	view.observed_at_ms = track->state.latest_observed_at_ms;
	view.updated_at_ms = prediction.at_time_ms;
	view.confidence = prediction.confidence;
	view.particles = track->prediction_particles;
	view.particle_count = prediction.particle_count;
	*view_out = view;
	return SG_BELIEF_RUNTIME_FRAME_APPLIED;
}

static sg_belief_runtime_frame_result_t BeliefRuntimeRefreshView(
	belief_runtime_track_t *track)
{
	sg_belief_runtime_view_t view;
	sg_belief_runtime_frame_result_t result;

	if (!track || track->state.updated_at_ms == 0U)
		return SG_BELIEF_RUNTIME_FRAME_REJECTED;
	result = BeliefRuntimePredict(track, track->state.updated_at_ms, NULL,
		&view);
	if (result == SG_BELIEF_RUNTIME_FRAME_APPLIED)
		track->view = view;
	return result;
}

static void BeliefRuntimeFrameCandidateClear(
	belief_runtime_frame_candidate_t *candidate)
{
	if (!candidate)
		return;
	SG_BeliefHorizonScopePreparedDestroy(candidate->horizon);
	free(candidate->track.particles);
	free(candidate->track.scratch_first);
	free(candidate->track.scratch_second);
	free(candidate->track.prediction_particles);
	memset(candidate, 0, sizeof(*candidate));
}

static sg_belief_runtime_frame_result_t BeliefRuntimeFrameCandidatePrepare(
	const belief_runtime_track_t *track, uint64_t at_ms,
	belief_runtime_frame_candidate_t *candidate)
{
	const sg_belief_horizon_authority_t *authority = NULL;
	sg_belief_horizon_accept_result_t horizon_result;
	sg_belief_runtime_frame_result_t result;

	if (!track || !candidate || !track->active ||
		!SG_BeliefStateValid(&track->state) ||
		!track->horizon_scope || at_ms < track->state.updated_at_ms)
		return SG_BELIEF_RUNTIME_FRAME_REJECTED;
	memset(candidate, 0, sizeof(*candidate));
	candidate->track = *track;
	candidate->track.particles = NULL;
	candidate->track.scratch_first = NULL;
	candidate->track.scratch_second = NULL;
	candidate->track.prediction_particles = NULL;
	candidate->track.capacity = 0U;
	candidate->track.issuers = NULL;
	candidate->track.issuer_count = 0U;
	candidate->track.issuer_capacity = 0U;
	memset(&candidate->track.view, 0, sizeof(candidate->track.view));
	if (!BeliefRuntimeGrowTrack(&candidate->track, track->capacity))
		return SG_BELIEF_RUNTIME_FRAME_CAPACITY;
	if (at_ms > track->state.updated_at_ms)
	{
		horizon_result = SG_BeliefHorizonScopePrepareCandidate(
			track->horizon_scope, belief_runtime_provider.snapshot,
			&candidate->track.state, at_ms, 0U, &candidate->horizon);
		if (horizon_result != SG_BELIEF_HORIZON_ACCEPTED)
			return horizon_result == SG_BELIEF_HORIZON_ALLOCATION_FAILED ?
				SG_BELIEF_RUNTIME_FRAME_CAPACITY :
				horizon_result == SG_BELIEF_HORIZON_OVERFLOW ?
					SG_BELIEF_RUNTIME_FRAME_OVERFLOW :
					SG_BELIEF_RUNTIME_FRAME_REJECTED;
		authority = SG_BeliefHorizonScopePreparedAuthority(candidate->horizon);
		if (!authority)
			return SG_BELIEF_RUNTIME_FRAME_REJECTED;
	}
	result = BeliefRuntimePredict(&candidate->track, at_ms, authority,
		&candidate->track.view);
	return result;
}

static void BeliefRuntimeFrameCandidateCommit(belief_runtime_track_t *track,
	belief_runtime_frame_candidate_t *candidate)
{
	if (!track || !candidate)
		return;
	if (candidate->horizon)
	{
		SG_BeliefHorizonScopePreparedCommit(track->horizon_scope,
			candidate->horizon);
		candidate->horizon = NULL;
	}
	free(track->particles);
	free(track->scratch_first);
	free(track->scratch_second);
	free(track->prediction_particles);
	track->particles = candidate->track.particles;
	track->scratch_first = candidate->track.scratch_first;
	track->scratch_second = candidate->track.scratch_second;
	track->prediction_particles = candidate->track.prediction_particles;
	track->capacity = candidate->track.capacity;
	track->state = candidate->track.state;
	track->state.particles = track->particles;
	track->state.particle_capacity = track->capacity;
	track->view = candidate->track.view;
	candidate->track.particles = NULL;
	candidate->track.scratch_first = NULL;
	candidate->track.scratch_second = NULL;
	candidate->track.prediction_particles = NULL;
}

sg_belief_runtime_observe_result_t SG_BeliefRuntimeObserve(
	const sg_perception_observation_t *observation)
{
	sg_belief_evidence_support_t *supports = NULL;
	sg_perception_adaptation_t adaptation;
	sg_belief_frame_t frame;
	sg_belief_reduction_t reduction;
	belief_runtime_life_publication_t life_publication;
	belief_runtime_track_t candidate;
	belief_runtime_track_t *track;
	belief_runtime_track_t *working;
	const sg_belief_horizon_authority_t *authority;
	const sg_belief_horizon_kernel_t *kernels;
	sg_belief_runtime_frame_result_t horizon_result;
	size_t kernel_count;
	uint64_t sequence;
	sg_belief_life_identity_t *prepared_issuers = NULL;
	size_t prepared_issuer_capacity = 0U;
	size_t audience_index;
	int ordering_identity_replacement;
	int fresh_storage;
	int append_issuer;
	sg_belief_reduce_result_t result;

	if (!BeliefRuntimeCurrent())
		return SG_BELIEF_RUNTIME_OBSERVE_UNAVAILABLE;
	memset(&adaptation, 0, sizeof(adaptation));
	if (SG_PerceptionEvidenceAdapt(belief_runtime_provider.snapshot, observation,
		NULL, 0U, &adaptation) != SG_PERCEPTION_ADAPT_CAPACITY)
	{
		return adaptation.result == SG_PERCEPTION_ADAPT_OVERFLOW ?
				SG_BELIEF_RUNTIME_OBSERVE_OVERFLOW :
			SG_BELIEF_RUNTIME_OBSERVE_REJECTED;
	}
	if (adaptation.required_support_capacity == 0U ||
		adaptation.required_support_capacity >
			SIZE_MAX / sizeof(*supports))
		return SG_BELIEF_RUNTIME_OBSERVE_OVERFLOW;
	supports = malloc(adaptation.required_support_capacity * sizeof(*supports));
	if (!supports)
		return SG_BELIEF_RUNTIME_OBSERVE_CAPACITY;
	if (SG_PerceptionEvidenceAdapt(belief_runtime_provider.snapshot, observation,
		supports, adaptation.required_support_capacity, &adaptation) !=
		SG_PERCEPTION_ADAPT_APPLIED)
	{
		free(supports);
		return adaptation.result == SG_PERCEPTION_ADAPT_OVERFLOW ?
			SG_BELIEF_RUNTIME_OBSERVE_OVERFLOW :
			SG_BELIEF_RUNTIME_OBSERVE_REJECTED;
	}
	if (adaptation.evidence.target_team ==
		adaptation.evidence.provenance.audience_team)
	{
		free(supports);
		return SG_BELIEF_RUNTIME_OBSERVE_REJECTED;
	}
	if (!BeliefRuntimeLifePublicationPrepare(&adaptation.evidence,
		&life_publication) ||
		!BeliefRuntimeLifePublicationAdmits(&life_publication))
	{
		free(supports);
		return SG_BELIEF_RUNTIME_OBSERVE_REJECTED;
	}
	if (!BeliefRuntimeTeamIndex(adaptation.evidence.provenance.audience_team,
		&audience_index) ||
		!BeliefRuntimeEvidenceFollowsAudienceClientWatermark(
			&adaptation.evidence) ||
		!BeliefRuntimeAudienceTargetSequenceAdmits(&adaptation.evidence))
	{
		free(supports);
		return SG_BELIEF_RUNTIME_OBSERVE_REJECTED;
	}
	track = BeliefRuntimeTrack(adaptation.evidence.provenance.audience_team,
		&adaptation.evidence.target_life);
	if (!track)
	{
		free(supports);
		return SG_BELIEF_RUNTIME_OBSERVE_REJECTED;
	}
	ordering_identity_replacement = !BeliefRuntimeTrackOrderingIdentityMatches(
		track, adaptation.evidence.provenance.audience_team,
		&adaptation.evidence.target_life);
	fresh_storage = ordering_identity_replacement ||
		!BeliefRuntimeTrackMatches(track,
		adaptation.evidence.provenance.audience_team,
		&adaptation.evidence.target_life) ||
		track->state.target_team != adaptation.evidence.target_team ||
		BeliefRuntimeTrackSupersededByPublication(track, &life_publication);
	memset(&candidate, 0, sizeof(candidate));
	working = fresh_storage ? &candidate : track;
	if (fresh_storage && !BeliefRuntimeBuildTrack(working, &adaptation.evidence,
		adaptation.required_support_capacity))
	{
		free(supports);
		return SG_BELIEF_RUNTIME_OBSERVE_CAPACITY;
	}
	if (!BeliefRuntimePrepareIssuer(working,
		&adaptation.evidence.provenance.issuer_life, &append_issuer,
		&prepared_issuers, &prepared_issuer_capacity))
	{
		if (fresh_storage)
			BeliefRuntimeClearTrack(&candidate);
		free(supports);
		return SG_BELIEF_RUNTIME_OBSERVE_CAPACITY;
	}
	if (!BeliefRuntimeTentativeReductionSequence(&sequence))
	{
		if (fresh_storage)
			BeliefRuntimeClearTrack(&candidate);
		free(prepared_issuers);
		free(supports);
		return SG_BELIEF_RUNTIME_OBSERVE_OVERFLOW;
	}
	memset(&frame, 0, sizeof(frame));
	frame.sequence = sequence;
	frame.expected_revision = working->state.revision;
	frame.expected_generation = working->state.generation;
	frame.at_ms = adaptation.evidence.provenance.authenticated_at_ms;
	frame.evidence = &adaptation.evidence;
	frame.evidence_count = 1U;
	authority = NULL;
	kernels = NULL;
	kernel_count = 0U;
	horizon_result = BeliefRuntimeHorizonPrepare(working, frame.at_ms,
		adaptation.evidence.observed_at_ms, &authority, &kernels,
		&kernel_count);
	if (horizon_result != SG_BELIEF_RUNTIME_FRAME_APPLIED)
	{
		if (fresh_storage)
			BeliefRuntimeClearTrack(&candidate);
		free(prepared_issuers);
		free(supports);
		return horizon_result == SG_BELIEF_RUNTIME_FRAME_CAPACITY ?
			SG_BELIEF_RUNTIME_OBSERVE_CAPACITY :
			horizon_result == SG_BELIEF_RUNTIME_FRAME_OVERFLOW ?
				SG_BELIEF_RUNTIME_OBSERVE_OVERFLOW :
				SG_BELIEF_RUNTIME_OBSERVE_REJECTED;
	}
	(void)authority;
	frame.kernels = kernels;
	frame.kernel_count = kernel_count;
	memset(&reduction, 0, sizeof(reduction));
	result = BeliefRuntimeReduce(working, &frame, &reduction);
	free(supports);
	if (result != SG_BELIEF_REDUCE_APPLIED)
	{
		if (fresh_storage)
			BeliefRuntimeClearTrack(&candidate);
		free(prepared_issuers);
		return result == SG_BELIEF_REDUCE_CAPACITY ?
			SG_BELIEF_RUNTIME_OBSERVE_CAPACITY :
			result == SG_BELIEF_REDUCE_OVERFLOW ?
				SG_BELIEF_RUNTIME_OBSERVE_OVERFLOW :
				SG_BELIEF_RUNTIME_OBSERVE_REJECTED;
	}
	BeliefRuntimeCommitIssuer(working,
		&adaptation.evidence.provenance.issuer_life, append_issuer,
		prepared_issuers, prepared_issuer_capacity);
	if (fresh_storage)
	{
		BeliefRuntimeClearTrack(track);
		*track = candidate;
	}
	BeliefRuntimeLifePublicationCommit(&life_publication);
	belief_runtime_reduction_sequence = sequence;
	(void)BeliefRuntimeRefreshView(track);
	BeliefRuntimeAudienceClientWatermarkAdvance(audience_index,
		adaptation.evidence.target_life.client_id,
		adaptation.evidence.provenance.authenticated_at_ms);
	BeliefRuntimeAudienceTargetSequenceCommit(audience_index,
		&adaptation.evidence);
	return SG_BELIEF_RUNTIME_OBSERVE_APPLIED;
}

sg_belief_runtime_frame_result_t SG_BeliefRuntimeFrame(
	uint64_t frame_sequence, uint64_t at_ms)
{
	belief_runtime_frame_candidate_t candidates[2][SG_BELIEF_MAX_CLIENTS];
	size_t team;
	size_t client;
	sg_belief_runtime_frame_result_t result;

	if (!BeliefRuntimeCurrent())
		return SG_BELIEF_RUNTIME_FRAME_UNAVAILABLE;
	if (frame_sequence == 0U || at_ms == 0U ||
		(frame_sequence < belief_runtime_frame_sequence) ||
		(at_ms < belief_runtime_frame_time))
	{
		BeliefRuntimeTransientReset();
		return SG_BELIEF_RUNTIME_FRAME_REJECTED;
	}
	memset(candidates, 0, sizeof(candidates));
	for (team = 0U; team < 2U; team++)
		for (client = 0U; client < SG_BELIEF_MAX_CLIENTS; client++)
		{
			belief_runtime_track_t *track = &belief_runtime_tracks[team][client];

			if (!track->active)
				continue;
			if (!SG_BeliefStateValid(&track->state))
			{
				result = SG_BELIEF_RUNTIME_FRAME_REJECTED;
				goto failure;
			}
			if (track->state.updated_at_ms > at_ms)
				goto timestamp_regression;
			if (!BeliefRuntimeLifeFenceCurrent(&track->state.target_life) ||
				!BeliefRuntimeTrackIssuersCurrent(track))
				continue;
			result = BeliefRuntimeFrameCandidatePrepare(track, at_ms,
				&candidates[team][client]);
			if (result != SG_BELIEF_RUNTIME_FRAME_APPLIED)
				goto failure;
		}
	for (team = 0U; team < 2U; team++)
		for (client = 0U; client < SG_BELIEF_MAX_CLIENTS; client++)
			if (candidates[team][client].track.active)
			{
				BeliefRuntimeFrameCandidateCommit(
					&belief_runtime_tracks[team][client],
					&candidates[team][client]);
				BeliefRuntimeAudienceClientWatermarkAdvance(team, client,
					at_ms);
			}
	belief_runtime_frame_sequence = frame_sequence;
	belief_runtime_frame_time = at_ms;
	return SG_BELIEF_RUNTIME_FRAME_APPLIED;

timestamp_regression:
	for (team = 0U; team < 2U; team++)
		for (client = 0U; client < SG_BELIEF_MAX_CLIENTS; client++)
			BeliefRuntimeFrameCandidateClear(&candidates[team][client]);
	BeliefRuntimeTransientReset();
	return SG_BELIEF_RUNTIME_FRAME_REJECTED;

failure:
	for (team = 0U; team < 2U; team++)
		for (client = 0U; client < SG_BELIEF_MAX_CLIENTS; client++)
			BeliefRuntimeFrameCandidateClear(&candidates[team][client]);
	return result;
}

static sg_belief_runtime_predict_result_t BeliefRuntimePredictResult(
	sg_belief_predict_result_t result)
{
	switch (result)
	{
	case SG_BELIEF_PREDICT_APPLIED:
		return SG_BELIEF_RUNTIME_PREDICT_APPLIED;
	case SG_BELIEF_PREDICT_CAPACITY:
		return SG_BELIEF_RUNTIME_PREDICT_CAPACITY;
	case SG_BELIEF_PREDICT_OVERFLOW:
		return SG_BELIEF_RUNTIME_PREDICT_OVERFLOW;
	case SG_BELIEF_PREDICT_REJECTED_INVALID:
		break;
	}
	return SG_BELIEF_RUNTIME_PREDICT_REJECTED;
}

sg_belief_runtime_predict_result_t SG_BeliefRuntimePredict(
	uint8_t audience_team, const sg_belief_life_identity_t *target_life,
	uint64_t at_ms, sg_belief_particle_t *scratch_first,
	sg_belief_particle_t *scratch_second, size_t scratch_capacity,
	sg_belief_particle_t *particles, size_t particle_capacity,
	sg_belief_prediction_t *out)
{
	belief_runtime_track_t *track;
	sg_belief_horizon_scope_prepared_t *prepared = NULL;
	const sg_belief_horizon_authority_t *authority = NULL;
	sg_belief_prediction_request_t request;
	sg_belief_horizon_accept_result_t horizon_result;
	sg_belief_predict_result_t predict_result;
	size_t team_index;
	uint64_t minimum_time;

	if (!BeliefRuntimeCurrent())
		return SG_BELIEF_RUNTIME_PREDICT_UNAVAILABLE;
	if (!out || !BeliefRuntimeTeamIndex(audience_team, &team_index) ||
		!SG_BeliefLifeIdentityValid(target_life))
		return SG_BELIEF_RUNTIME_PREDICT_REJECTED;
	track = BeliefRuntimeTrack(audience_team, target_life);
	if (!BeliefRuntimeTrackMatches(track, audience_team, target_life))
		return SG_BELIEF_RUNTIME_PREDICT_UNAVAILABLE;
	minimum_time = track->state.updated_at_ms;
	if (belief_runtime_audience_client_watermarks[team_index]
		[target_life->client_id] > minimum_time)
		minimum_time = belief_runtime_audience_client_watermarks[team_index]
			[target_life->client_id];
	if (track->view.updated_at_ms > minimum_time)
		minimum_time = track->view.updated_at_ms;
	if (at_ms < minimum_time)
		return SG_BELIEF_RUNTIME_PREDICT_REJECTED;
	if (at_ms > track->state.updated_at_ms)
	{
		if (!track->horizon_scope)
			return SG_BELIEF_RUNTIME_PREDICT_REJECTED;
		horizon_result = SG_BeliefHorizonScopePrepareCandidate(
			track->horizon_scope, belief_runtime_provider.snapshot,
			&track->state, at_ms, 0U, &prepared);
		if (horizon_result != SG_BELIEF_HORIZON_ACCEPTED)
			return horizon_result == SG_BELIEF_HORIZON_ALLOCATION_FAILED ?
				SG_BELIEF_RUNTIME_PREDICT_CAPACITY :
				horizon_result == SG_BELIEF_HORIZON_OVERFLOW ?
					SG_BELIEF_RUNTIME_PREDICT_OVERFLOW :
					SG_BELIEF_RUNTIME_PREDICT_REJECTED;
		authority = SG_BeliefHorizonScopePreparedAuthority(prepared);
		if (!authority)
		{
			SG_BeliefHorizonScopePreparedDestroy(prepared);
			return SG_BELIEF_RUNTIME_PREDICT_REJECTED;
		}
	}
	memset(&request, 0, sizeof(request));
	request.at_time_ms = at_ms;
	request.horizon = authority;
	request.scratch_first = scratch_first;
	request.scratch_second = scratch_second;
	request.scratch_capacity = scratch_capacity;
	request.particles = particles;
	request.particle_capacity = particle_capacity;
	predict_result = SG_BeliefPredict(belief_runtime_provider.snapshot,
		&track->state, &request, out);
	SG_BeliefHorizonScopePreparedDestroy(prepared);
	return BeliefRuntimePredictResult(predict_result);
}

void SG_BeliefRuntimeRetireLife(const sg_belief_life_identity_t *life)
{
	size_t team;
	size_t client;

	if (!SG_BeliefLifeIdentityValid(life))
		return;
	BeliefRuntimeLifeFenceRetire(life);
	for (team = 0U; team < 2U; team++)
		for (client = 0U; client < SG_BELIEF_MAX_CLIENTS; client++)
		{
			belief_runtime_track_t *track = &belief_runtime_tracks[team][client];

			if (!track->active || !SG_BeliefStateValid(&track->state))
				continue;
			if (SG_BeliefLifeIdentityEqual(&track->state.target_life, life) ||
				BeliefRuntimeIssuerRecorded(track, life))
				BeliefRuntimeClearTrack(track);
		}
}

const sg_belief_runtime_view_t *SG_BeliefRuntimeView(uint8_t audience_team,
	const sg_belief_life_identity_t *target_life)
{
	belief_runtime_track_t *track;

	if (!BeliefRuntimeCurrent())
		return NULL;
	track = BeliefRuntimeTrack(audience_team, target_life);
	if (!BeliefRuntimeTrackMatches(track, audience_team, target_life) ||
		track->view.target_life.spawn_generation == 0U)
		return NULL;
	return &track->view;
}

const sg_belief_runtime_view_t *SG_BeliefRuntimeViewForClient(
	uint8_t audience_team, uint32_t client_id)
{
	size_t team_index;
	belief_runtime_track_t *track;

	if (!BeliefRuntimeCurrent() || client_id >= SG_BELIEF_MAX_CLIENTS ||
		!BeliefRuntimeTeamIndex(audience_team, &team_index))
		return NULL;
	track = &belief_runtime_tracks[team_index][client_id];
	if (!BeliefRuntimeTrackMatches(track, audience_team,
		&track->state.target_life) ||
		track->view.target_life.spawn_generation == 0U)
		return NULL;
	return &track->view;
}
