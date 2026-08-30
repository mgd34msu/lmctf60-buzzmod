/* Runtime owner for sparse player beliefs.  The static RUNE is borrowed only
 * through an authenticated snapshot; all tracks, evidence, and views are
 * process-local player state. */
#ifndef SG_BELIEF_RUNTIME_H
#define SG_BELIEF_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include "sg_perception_evidence.h"

typedef int (*sg_belief_runtime_locate_fn)(void *context,
	const sg_rune_runtime_snapshot_t *snapshot, const float position[3],
	sg_phase_coordinate_t *phase_out);

/* A level owner supplies one immutable, accepted snapshot and the policy that
 * governs its live tracks.  locate turns an already-earned runtime point into
 * a phase coordinate.  It cannot inspect actors, controllers, or mutable RUNE
 * state.  Clear this registration before either borrowed source is retired. */
typedef struct sg_belief_runtime_provider_s
{
	const sg_rune_runtime_snapshot_t *snapshot;
	uint64_t localization_generation;
	sg_belief_policy_t policy;
	sg_belief_runtime_locate_fn locate;
	void *context;
} sg_belief_runtime_provider_t;

typedef struct sg_belief_runtime_pose_s
{
	sg_belief_motion_state_t movement_state;
	uint8_t weapon_state;
	uint8_t reserved[3];
	float position[3];
	float velocity[3];
	float acceleration[3];
	float orientation[3];
} sg_belief_runtime_pose_t;

/* A consumer view is a read-only, complete particle distribution.  It is
 * never a belief owner and callers must not retain its address past the
 * current frame.  No selected cell or position is published: a multimodal
 * belief must remain multimodal at every consumer boundary. */
typedef struct sg_belief_runtime_view_s
{
	uint8_t audience_team;
	uint8_t target_team;
	uint8_t exact_sight;
	uint8_t latest_source;
	sg_belief_life_identity_t target_life;
	uint64_t observed_at_ms;
	uint64_t updated_at_ms;
	float confidence;
	const sg_belief_particle_t *particles;
	size_t particle_count;
} sg_belief_runtime_view_t;

typedef enum sg_belief_runtime_observe_result_e
{
	SG_BELIEF_RUNTIME_OBSERVE_APPLIED = 0,
	SG_BELIEF_RUNTIME_OBSERVE_UNAVAILABLE,
	SG_BELIEF_RUNTIME_OBSERVE_REJECTED,
	/* The adapted/reduced state is unchanged.  The owner could not acquire the
	 * exact contract-reported storage needed for a retry. */
	SG_BELIEF_RUNTIME_OBSERVE_CAPACITY,
	SG_BELIEF_RUNTIME_OBSERVE_OVERFLOW
} sg_belief_runtime_observe_result_t;

typedef enum sg_belief_runtime_frame_result_e
{
	SG_BELIEF_RUNTIME_FRAME_APPLIED = 0,
	SG_BELIEF_RUNTIME_FRAME_UNAVAILABLE,
	SG_BELIEF_RUNTIME_FRAME_REJECTED,
	SG_BELIEF_RUNTIME_FRAME_CAPACITY,
	SG_BELIEF_RUNTIME_FRAME_OVERFLOW
} sg_belief_runtime_frame_result_t;

/* Registration is a lifecycle boundary.  A non-NULL replacement is validated
 * completely before it can retire tracks or the current provider.  Every
 * accepted registration receives a monotonic, non-wrapping owner identity,
 * including an equal-valued or policy-only replacement.  Clearing retires
 * every track before any borrowed snapshot or locator can go stale. */
int SG_BeliefRuntimeProviderSet(const sg_belief_runtime_provider_t *provider);
int SG_BeliefRuntimeProviderAvailable(void);
void SG_BeliefRuntimeReset(void);

/* The registered snapshot is borrowed and immutable.  It is exposed only so
 * producers can bind their typed authentication to the same exact identity;
 * no caller receives a route, player, or mutable RUNE handle. */
const sg_rune_runtime_snapshot_t *SG_BeliefRuntimeSnapshot(void);

/* Build a typed hypothesis from an earned point.  A failed provider leaves
 * out untouched.  The caller selects source-specific spread and likelihood. */
int SG_BeliefRuntimeHypothesis(const sg_belief_runtime_pose_t *pose,
	sg_perception_hypothesis_t *out);

/* Adapt and reduce one authenticated observation.  The runtime owns monotonic
 * reduction sequencing, so several team observations at one sample time are
 * retained instead of being mistaken for duplicates. */
sg_belief_runtime_observe_result_t SG_BeliefRuntimeObserve(
	const sg_perception_observation_t *observation);

/* Age every current track and refresh its predictor-backed view atomically.
 * A sequence or timestamp regression retires transient tracks and fails
 * closed without clearing permanent retired-life tombstones.
 * Capacity is never truncated: a staging capacity or overflow result leaves
 * every track and the global frame bookkeeping unchanged. */
sg_belief_runtime_frame_result_t SG_BeliefRuntimeFrame(
	uint64_t frame_sequence, uint64_t at_ms);

/* Retire data named by an exact player life.  Any track fused from that issuer
 * is removed, so recycled slots cannot inherit its authority. */
void SG_BeliefRuntimeRetireLife(const sg_belief_life_identity_t *life);

/* A host client-slot teardown is a stronger lifecycle boundary than an
 * observation: it retires every generation in that slot if the host can no
 * longer present the departing exact life. */
void SG_BeliefRuntimeRetireClient(uint32_t client_id);

/* Return NULL unless this exact audience/target life has a current sparse
 * projection.  The returned pointer remains owned by the runtime. */
const sg_belief_runtime_view_t *SG_BeliefRuntimeView(uint8_t audience_team,
	const sg_belief_life_identity_t *target_life);

/* Client lookup is intentionally derived entirely from the runtime-owned
 * track index.  It never consults a live player slot, which keeps consumers
 * from turning a belief lookup back into an enemy-state oracle. */
const sg_belief_runtime_view_t *SG_BeliefRuntimeViewForClient(
	uint8_t audience_team, uint32_t client_id);

#if defined(SG_BELIEF_TESTING)
void SG_BeliefTestRuntimeProviderEpochExhaust(void);
#endif

#endif /* SG_BELIEF_RUNTIME_H */
