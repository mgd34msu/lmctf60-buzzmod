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
	sg_belief_life_identity_t *issuers;
	size_t issuer_count;
	sg_belief_runtime_view_t view;
} belief_runtime_track_t;

static sg_belief_runtime_provider_t belief_runtime_provider;
static belief_runtime_track_t belief_runtime_tracks[2][SG_BELIEF_MAX_CLIENTS];
static uint64_t belief_runtime_reduction_sequence;
static uint64_t belief_runtime_frame_sequence;
static uint64_t belief_runtime_frame_time;

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

static void BeliefRuntimeClearTrack(belief_runtime_track_t *track)
{
	if (!track)
		return;
	free(track->particles);
	free(track->scratch_first);
	free(track->scratch_second);
	free(track->prediction_particles);
	free(track->issuers);
	memset(track, 0, sizeof(*track));
}

void SG_BeliefRuntimeReset(void)
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

int SG_BeliefRuntimeProviderSet(const sg_belief_runtime_provider_t *provider)
{
	memset(&belief_runtime_provider, 0, sizeof(belief_runtime_provider));
	SG_BeliefRuntimeReset();
	if (!provider)
		return 1;
	if (!BeliefRuntimeProviderValid(provider))
		return 0;
	belief_runtime_provider = *provider;
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

static int BeliefRuntimeFinite3(const float value[3])
{
	return value && isfinite(value[0]) && isfinite(value[1]) &&
		isfinite(value[2]);
}

int SG_BeliefRuntimeHypothesis(const sg_belief_runtime_pose_t *pose,
	sg_perception_hypothesis_t *out)
{
	sg_phase_coordinate_t phase;

	if (!pose || !out || !BeliefRuntimeCurrent() ||
		pose->movement_state < SG_BELIEF_MOTION_UNKNOWN ||
		pose->movement_state >= SG_BELIEF_MOTION_COUNT ||
		pose->reserved[0] != 0U || pose->reserved[1] != 0U ||
		pose->reserved[2] != 0U || !BeliefRuntimeFinite3(pose->position) ||
		!BeliefRuntimeFinite3(pose->velocity) ||
		!BeliefRuntimeFinite3(pose->acceleration) ||
		!BeliefRuntimeFinite3(pose->orientation))
		return 0;
	memset(&phase, 0, sizeof(phase));
	if (!belief_runtime_provider.locate(belief_runtime_provider.context,
		belief_runtime_provider.snapshot, pose->position, &phase) ||
		!SG_PhaseCoordinateValid(belief_runtime_provider.snapshot, &phase))
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

static int BeliefRuntimeTrackMatches(const belief_runtime_track_t *track,
	uint8_t audience_team, const sg_belief_life_identity_t *target_life)
{
	return track && track->active == 1U &&
		SG_BeliefStateValid(&track->state) &&
		track->state.audience_team == audience_team &&
		SG_BeliefLifeIdentityEqual(&track->state.target_life, target_life) &&
		track->localization_generation ==
			belief_runtime_provider.localization_generation &&
		track->state.rune_identity == belief_runtime_provider.snapshot->identity &&
		track->state.topology_revision ==
			belief_runtime_provider.snapshot->topology_revision;
}

static int BeliefRuntimeGrowTrack(belief_runtime_track_t *track,
	size_t required_capacity)
{
	sg_belief_particle_t *particles;
	sg_belief_particle_t *scratch_first;
	sg_belief_particle_t *scratch_second;
	sg_belief_particle_t *prediction_particles;
	size_t bytes;

	if (!track || required_capacity == 0U ||
		required_capacity > SIZE_MAX / sizeof(*particles))
		return 0;
	if (track->capacity >= required_capacity && track->particles &&
		track->scratch_first && track->scratch_second &&
		track->prediction_particles)
		return 1;
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
	free(track->particles);
	free(track->scratch_first);
	free(track->scratch_second);
	free(track->prediction_particles);
	track->particles = particles;
	track->scratch_first = scratch_first;
	track->scratch_second = scratch_second;
	track->prediction_particles = prediction_particles;
	track->capacity = required_capacity;
	if (track->active)
	{
		track->state.particles = track->particles;
		track->state.particle_capacity = required_capacity;
	}
	return 1;
}

static int BeliefRuntimeEnsureTrack(belief_runtime_track_t *track,
	const sg_belief_evidence_t *evidence, size_t required_capacity)
{
	sg_belief_state_config_t config;

	if (!track || !evidence || !BeliefRuntimeCurrent())
		return 0;
	if (BeliefRuntimeTrackMatches(track, evidence->provenance.audience_team,
		&evidence->target_life) && track->state.target_team == evidence->target_team)
		return 1;
	BeliefRuntimeClearTrack(track);
	if (!BeliefRuntimeGrowTrack(track, required_capacity))
		return 0;
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

static int BeliefRuntimeRecordIssuer(belief_runtime_track_t *track,
	const sg_belief_life_identity_t *issuer)
{
	sg_belief_life_identity_t *issuers;
	size_t index;
	size_t bytes;

	if (!track || !SG_BeliefLifeIdentityValid(issuer))
		return 0;
	for (index = 0U; index < track->issuer_count; index++)
		if (SG_BeliefLifeIdentityEqual(&track->issuers[index], issuer))
			return 1;
	if (track->issuer_count == SIZE_MAX ||
		track->issuer_count + 1U > SIZE_MAX / sizeof(*issuers))
		return 0;
	bytes = (track->issuer_count + 1U) * sizeof(*issuers);
	issuers = realloc(track->issuers, bytes);
	if (!issuers)
		return 0;
	issuers[track->issuer_count] = *issuer;
	track->issuers = issuers;
	track->issuer_count++;
	return 1;
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

static int BeliefRuntimeNextReductionSequence(uint64_t *sequence_out)
{
	if (!sequence_out || belief_runtime_reduction_sequence == UINT64_MAX)
		return 0;
	belief_runtime_reduction_sequence++;
	*sequence_out = belief_runtime_reduction_sequence;
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

static sg_belief_runtime_frame_result_t BeliefRuntimeRefreshView(
	belief_runtime_track_t *track,
	uint64_t at_time_ms)
{
	sg_belief_horizon_source_t *source = NULL;
	sg_belief_horizon_authority_t *authority = NULL;
	const sg_belief_horizon_kernel_t *kernels = NULL;
	sg_rune_v2_content_id_t content_identity;
	sg_belief_prediction_request_t request;
	sg_belief_prediction_t prediction;
	sg_belief_predict_result_t result;
	sg_belief_runtime_view_t view;
	size_t kernel_count;

	if (!track || !track->active || !SG_BeliefStateValid(&track->state) ||
		!BeliefRuntimeCurrent() || at_time_ms < track->state.updated_at_ms)
		return SG_BELIEF_RUNTIME_FRAME_REJECTED;
	if (at_time_ms > track->state.updated_at_ms)
	{
		if (SG_BeliefHorizonSourceIssue(belief_runtime_provider.snapshot,
			&track->state, at_time_ms, &source) != SG_BELIEF_HORIZON_ACCEPTED ||
			!SG_BeliefHorizonSourceView(belief_runtime_provider.snapshot,
				&track->state, source, &kernels, &kernel_count,
				&content_identity) ||
			SG_BeliefHorizonAuthorityAccept(belief_runtime_provider.snapshot,
				&track->state, source, kernels, kernel_count, &authority) !=
				SG_BELIEF_HORIZON_ACCEPTED)
		{
			SG_BeliefHorizonAuthorityDestroy(authority);
			SG_BeliefHorizonSourceDestroy(source);
			return SG_BELIEF_RUNTIME_FRAME_REJECTED;
		}
	}
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
		result = SG_BeliefPredict(belief_runtime_provider.snapshot, &track->state,
			&request, &prediction);
		if (result != SG_BELIEF_PREDICT_CAPACITY)
			break;
		kernel_count = prediction.required_scratch_capacity;
		if (prediction.required_particle_capacity > kernel_count)
			kernel_count = prediction.required_particle_capacity;
		if (kernel_count <= track->capacity ||
			!BeliefRuntimeGrowTrack(track, kernel_count))
		{
			SG_BeliefHorizonAuthorityDestroy(authority);
			SG_BeliefHorizonSourceDestroy(source);
			return SG_BELIEF_RUNTIME_FRAME_CAPACITY;
		}
	}
	SG_BeliefHorizonAuthorityDestroy(authority);
	SG_BeliefHorizonSourceDestroy(source);
	if (result != SG_BELIEF_PREDICT_APPLIED || prediction.particle_count == 0U)
	{
		memset(&track->view, 0, sizeof(track->view));
		return result == SG_BELIEF_PREDICT_OVERFLOW ?
			SG_BELIEF_RUNTIME_FRAME_OVERFLOW :
			SG_BELIEF_RUNTIME_FRAME_REJECTED;
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
	track->view = view;
	return SG_BELIEF_RUNTIME_FRAME_APPLIED;
}

sg_belief_runtime_observe_result_t SG_BeliefRuntimeObserve(
	const sg_perception_observation_t *observation)
{
	sg_belief_evidence_support_t *supports = NULL;
	sg_perception_adaptation_t adaptation;
	sg_belief_frame_t frame;
	sg_belief_reduction_t reduction;
	belief_runtime_track_t *track;
	uint64_t sequence;
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
	track = BeliefRuntimeTrack(adaptation.evidence.provenance.audience_team,
		&adaptation.evidence.target_life);
	if (!track)
	{
		free(supports);
		return SG_BELIEF_RUNTIME_OBSERVE_REJECTED;
	}
	if (!BeliefRuntimeEnsureTrack(track, &adaptation.evidence,
		adaptation.required_support_capacity))
	{
		free(supports);
		return SG_BELIEF_RUNTIME_OBSERVE_CAPACITY;
	}
	if (!BeliefRuntimeRecordIssuer(track,
		&adaptation.evidence.provenance.issuer_life))
	{
		free(supports);
		return SG_BELIEF_RUNTIME_OBSERVE_CAPACITY;
	}
	if (!BeliefRuntimeNextReductionSequence(&sequence))
	{
		free(supports);
		return SG_BELIEF_RUNTIME_OBSERVE_OVERFLOW;
	}
	memset(&frame, 0, sizeof(frame));
	frame.sequence = sequence;
	frame.expected_revision = track->state.revision;
	frame.expected_generation = track->state.generation;
	frame.at_ms = adaptation.evidence.provenance.authenticated_at_ms;
	frame.evidence = &adaptation.evidence;
	frame.evidence_count = 1U;
	memset(&reduction, 0, sizeof(reduction));
	result = BeliefRuntimeReduce(track, &frame, &reduction);
	free(supports);
	if (result != SG_BELIEF_REDUCE_APPLIED)
	{
		if (result != SG_BELIEF_REDUCE_CAPACITY &&
			result != SG_BELIEF_REDUCE_OVERFLOW)
			BeliefRuntimeClearTrack(track);
		return result == SG_BELIEF_REDUCE_CAPACITY ?
			SG_BELIEF_RUNTIME_OBSERVE_CAPACITY :
			result == SG_BELIEF_REDUCE_OVERFLOW ?
				SG_BELIEF_RUNTIME_OBSERVE_OVERFLOW :
				SG_BELIEF_RUNTIME_OBSERVE_REJECTED;
	}
	(void)BeliefRuntimeRefreshView(track, track->state.updated_at_ms);
	return SG_BELIEF_RUNTIME_OBSERVE_APPLIED;
}

sg_belief_runtime_frame_result_t SG_BeliefRuntimeFrame(
	uint64_t frame_sequence, uint64_t at_ms)
{
	size_t team;
	size_t client;

	if (!BeliefRuntimeCurrent())
		return SG_BELIEF_RUNTIME_FRAME_UNAVAILABLE;
	if (frame_sequence == 0U || at_ms == 0U ||
		(frame_sequence < belief_runtime_frame_sequence) ||
		(at_ms < belief_runtime_frame_time))
	{
		SG_BeliefRuntimeReset();
		return SG_BELIEF_RUNTIME_FRAME_REJECTED;
	}
	belief_runtime_frame_sequence = frame_sequence;
	belief_runtime_frame_time = at_ms;
	for (team = 0U; team < 2U; team++)
		for (client = 0U; client < SG_BELIEF_MAX_CLIENTS; client++)
		{
			belief_runtime_track_t *track = &belief_runtime_tracks[team][client];
			sg_belief_frame_t frame;
			sg_belief_reduction_t reduction;
			sg_belief_runtime_frame_result_t refresh;
			sg_belief_reduce_result_t reduced;
			uint64_t sequence;

			if (!track->active)
				continue;
			if (!SG_BeliefStateValid(&track->state) ||
				track->state.updated_at_ms > at_ms)
			{
				BeliefRuntimeClearTrack(track);
				continue;
			}
			if (track->state.updated_at_ms == at_ms)
				continue;
			refresh = BeliefRuntimeRefreshView(track, at_ms);
			if (refresh == SG_BELIEF_RUNTIME_FRAME_CAPACITY ||
				refresh == SG_BELIEF_RUNTIME_FRAME_OVERFLOW)
				return refresh;
			if (refresh != SG_BELIEF_RUNTIME_FRAME_APPLIED)
			{
				BeliefRuntimeClearTrack(track);
				continue;
			}
			if (!BeliefRuntimeNextReductionSequence(&sequence))
				return SG_BELIEF_RUNTIME_FRAME_OVERFLOW;
			memset(&frame, 0, sizeof(frame));
			frame.sequence = sequence;
			frame.expected_revision = track->state.revision;
			frame.expected_generation = track->state.generation;
			frame.at_ms = at_ms;
			memset(&reduction, 0, sizeof(reduction));
			reduced = BeliefRuntimeReduce(track, &frame, &reduction);
			if (reduced == SG_BELIEF_REDUCE_CAPACITY)
				return SG_BELIEF_RUNTIME_FRAME_CAPACITY;
			if (reduced == SG_BELIEF_REDUCE_OVERFLOW)
				return SG_BELIEF_RUNTIME_FRAME_OVERFLOW;
			if (reduced != SG_BELIEF_REDUCE_APPLIED)
			{
				BeliefRuntimeClearTrack(track);
				continue;
			}
		}
	return SG_BELIEF_RUNTIME_FRAME_APPLIED;
}

void SG_BeliefRuntimeRetireLife(const sg_belief_life_identity_t *life)
{
	size_t team;
	size_t client;

	if (!SG_BeliefLifeIdentityValid(life))
		return;
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

void SG_BeliefRuntimeRetireClient(uint32_t client_id)
{
	size_t team;
	size_t client;

	if (client_id >= SG_BELIEF_MAX_CLIENTS)
		return;
	for (team = 0U; team < 2U; team++)
		for (client = 0U; client < SG_BELIEF_MAX_CLIENTS; client++)
		{
			belief_runtime_track_t *track = &belief_runtime_tracks[team][client];

			if (!track->active || !SG_BeliefStateValid(&track->state))
				continue;
			if (track->state.target_life.client_id == client_id)
				BeliefRuntimeClearTrack(track);
			else
			{
				size_t issuer;

				for (issuer = 0U; issuer < track->issuer_count; issuer++)
					if (track->issuers[issuer].client_id == client_id)
					{
						BeliefRuntimeClearTrack(track);
						break;
					}
			}
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
	if (!track->active || !SG_BeliefStateValid(&track->state) ||
		track->view.target_life.spawn_generation == 0U)
		return NULL;
	return &track->view;
}
