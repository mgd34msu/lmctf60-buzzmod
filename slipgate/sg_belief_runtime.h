/* Runtime-owned sparse player beliefs over an accepted compact RUNE. */
#ifndef SG_BELIEF_RUNTIME_H
#define SG_BELIEF_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#include "sg_rune_compact_localize.h"

/* These bound live actor beliefs, not map cells. The runtime never allocates
 * storage proportional to the BSP or publishes a selected route. */
#define SG_BELIEF_RUNTIME_MAX_TRACKS 512U
#define SG_BELIEF_RUNTIME_MAX_PARTICLES 32U
#define SG_BELIEF_RUNTIME_MAX_COVERAGE 32U
/* Life and replay tables are runtime fences, not sparse probability mass.
 * Their finite capacity is explicit: admission fails rather than evicting a
 * fence that protects a delayed packet. */
#define SG_BELIEF_RUNTIME_MAX_LIFE_FENCES SG_BELIEF_RUNTIME_MAX_TRACKS
#define SG_BELIEF_RUNTIME_MAX_REPLAY_FENCES \
	(SG_BELIEF_RUNTIME_MAX_TRACKS * 2U)

typedef enum sg_belief_runtime_source_e
{
	SG_BELIEF_RUNTIME_SOURCE_SIGHT = 0,
	SG_BELIEF_RUNTIME_SOURCE_SOUND,
	SG_BELIEF_RUNTIME_SOURCE_DAMAGE,
	SG_BELIEF_RUNTIME_SOURCE_ITEM,
	SG_BELIEF_RUNTIME_SOURCE_FLAG,
	SG_BELIEF_RUNTIME_SOURCE_TEAMMATE,
	SG_BELIEF_RUNTIME_SOURCE_WEAPON_FIRE,
	SG_BELIEF_RUNTIME_SOURCE_HOOK,
	SG_BELIEF_RUNTIME_SOURCE_MECHANISM,
	SG_BELIEF_RUNTIME_SOURCE_WATER,
	SG_BELIEF_RUNTIME_SOURCE_COUNT
} sg_belief_runtime_source_t;

/* ITEM records the host-confirmed target pickup event.  Keep the old short
 * spelling for compact callers while making the evidence meaning explicit. */
#define SG_BELIEF_RUNTIME_SOURCE_ITEM_PICKUP SG_BELIEF_RUNTIME_SOURCE_ITEM

typedef enum sg_belief_runtime_evidence_kind_e
{
	SG_BELIEF_RUNTIME_EVIDENCE_POSITIVE = 0,
	SG_BELIEF_RUNTIME_EVIDENCE_NEGATIVE,
	SG_BELIEF_RUNTIME_EVIDENCE_KIND_COUNT
} sg_belief_runtime_evidence_kind_t;

typedef enum sg_belief_runtime_authority_e
{
	SG_BELIEF_RUNTIME_AUTHORITY_HOST_SENSOR = 0,
	SG_BELIEF_RUNTIME_AUTHORITY_HOST_TEAMMATE_REPORT,
	SG_BELIEF_RUNTIME_AUTHORITY_COUNT
} sg_belief_runtime_authority_t;

/* UNKNOWN is deliberately the zero value.  An observation may identify a
 * compact location without claiming an unearned weapon state. */
typedef enum sg_belief_runtime_weapon_state_e
{
	SG_BELIEF_RUNTIME_WEAPON_UNKNOWN = 0,
	SG_BELIEF_RUNTIME_WEAPON_READY,
	SG_BELIEF_RUNTIME_WEAPON_FIRING,
	SG_BELIEF_RUNTIME_WEAPON_RELOADING,
	SG_BELIEF_RUNTIME_WEAPON_SWITCHING,
	SG_BELIEF_RUNTIME_WEAPON_STATE_COUNT
} sg_belief_runtime_weapon_state_t;

typedef struct sg_belief_runtime_life_s
{
	uint32_t client_id;
	uint32_t reserved;
	uint64_t spawn_generation;
} sg_belief_runtime_life_t;

/* A compact cell is always explicit. State dimensions are optional because a
 * visible human has an earned position but no bot Pmove state to borrow. */
typedef struct sg_belief_runtime_cell_state_s
{
	sg_rune_compact_location_t location;
	uint32_t known_components;
	sg_rune_stance_t stance;
	sg_rune_motion_t motion;
	sg_rune_support_t support;
	sg_rune_medium_t medium;
	sg_rune_void_relation_t void_relation;
	sg_rune_reference_frame_t reference_frame;
} sg_belief_runtime_cell_state_t;

/* Negative evidence names only cells whose absence the host sensor actually
 * checked.  It cannot erase a belief from an inferred radius or a route. */
typedef struct sg_belief_runtime_coverage_s
{
	sg_rune_compact_location_t location;
} sg_belief_runtime_coverage_t;

enum
{
	SG_BELIEF_RUNTIME_CELL_STANCE = UINT32_C(1) << 0,
	SG_BELIEF_RUNTIME_CELL_MOTION = UINT32_C(1) << 1,
	SG_BELIEF_RUNTIME_CELL_SUPPORT = UINT32_C(1) << 2,
	SG_BELIEF_RUNTIME_CELL_MEDIUM = UINT32_C(1) << 3,
	SG_BELIEF_RUNTIME_CELL_VOID_RELATION = UINT32_C(1) << 4,
	SG_BELIEF_RUNTIME_CELL_REFERENCE_FRAME = UINT32_C(1) << 5
};

#define SG_BELIEF_RUNTIME_CELL_COMPONENTS_KNOWN \
	(SG_BELIEF_RUNTIME_CELL_STANCE | SG_BELIEF_RUNTIME_CELL_MOTION | \
	 SG_BELIEF_RUNTIME_CELL_SUPPORT | SG_BELIEF_RUNTIME_CELL_MEDIUM | \
	 SG_BELIEF_RUNTIME_CELL_VOID_RELATION | \
	 SG_BELIEF_RUNTIME_CELL_REFERENCE_FRAME)

typedef struct sg_belief_runtime_hypothesis_s
{
	float position[3];
	float velocity[3];
	float acceleration[3];
	float orientation[3];
	float spread_radius;
	float likelihood;
	sg_belief_runtime_weapon_state_t weapon_state;
} sg_belief_runtime_hypothesis_t;

typedef struct sg_belief_runtime_particle_s
{
	sg_belief_runtime_cell_state_t cell;
	float position[3];
	float velocity[3];
	float acceleration[3];
	float orientation[3];
	float spread_radius;
	float weight;
	uint64_t observed_at_ms;
	/* The time bucket represented by this state.  It advances only through the
	 * provider's movement transition, never by looking at an actor slot. */
	uint64_t future_at_ms;
	sg_belief_runtime_weapon_state_t weapon_state;
} sg_belief_runtime_particle_t;

/* A propagation provider emits every legal compact successor for one sparse
 * particle.  It may inspect static RUNE fields and authenticated frame-local
 * movement/mechanism state, but receives no player or enemy object.  On
 * success it reports the complete count in count_out.  A count greater than
 * capacity makes the runtime fail the frame with OVERFLOW; it must never
 * silently keep only the first modes. */
typedef struct sg_belief_runtime_propagation_s
{
	sg_belief_runtime_cell_state_t cell;
	/* NONE is legal only when the successor remains in the same compact cell.
	 * A cross-cell successor names its single RUNE portal so the runtime can
	 * verify both endpoints and direction in O(1). */
	sg_rune_compact_portal_index_t portal;
	float position[3];
	float velocity[3];
	float acceleration[3];
	float orientation[3];
	float likelihood;
} sg_belief_runtime_propagation_t;

typedef struct sg_belief_runtime_policy_s
{
	uint64_t confidence_decay_ms;
	float spread_growth_per_ms;
} sg_belief_runtime_policy_t;

/* The locator is the compact-cell authority. It receives an already-earned
 * runtime point and must return the exact compact cell containing it. It may
 * attach state components only when the host supplied them; it never inspects
 * player slots, controllers, or mutable RUNE state. */
typedef int (*sg_belief_runtime_locate_fn)(void *context,
	const sg_rune_compact_model_t *model, const float position[3],
	sg_belief_runtime_cell_state_t *cell_out);

typedef int (*sg_belief_runtime_propagate_fn)(void *context,
	const sg_rune_compact_model_t *model,
	const sg_belief_runtime_particle_t *particle, uint64_t from_ms,
	uint64_t to_ms, sg_belief_runtime_propagation_t *transitions,
	size_t capacity, size_t *count_out);

/* The provider owner supplies this generation fence. It must return false as
 * soon as the accepted artifact, locator, or host-law publication it borrows
 * is no longer current. */
typedef int (*sg_belief_runtime_current_fn)(void *context,
	const sg_rune_compact_model_t *model,
	const sg_rune_compact_identity_t *identity, uint64_t rune_identity,
	uint64_t topology_revision, uint64_t generation);

typedef struct sg_belief_runtime_provider_s
{
	const sg_rune_compact_model_t *model;
	const sg_rune_compact_identity_t *identity;
	uint64_t rune_identity;
	uint64_t topology_revision;
	uint64_t generation;
	sg_belief_runtime_policy_t policy;
	sg_belief_runtime_locate_fn locate;
	/* Optional while the production compact movement adapter is being
	 * migrated.  Without it a belief ages and diffuses in its current valid
	 * cell, but it is never allowed to invent a cross-cell transition. */
	sg_belief_runtime_propagate_fn propagate;
	sg_belief_runtime_current_fn current;
	void *context;
} sg_belief_runtime_provider_t;

/* This private payload shape is produced only through the opaque compact
 * perception authority.  Its caller-supplied authentication and identity
 * fields are evidence to validate, never admission authority: the raw
 * runtime entry point is intentionally not declared for gameplay code. */
typedef struct sg_belief_runtime_observation_s
{
	uint8_t authenticated;
	sg_belief_runtime_authority_t authority;
	uint8_t audience_team;
	uint8_t target_team;
	sg_belief_runtime_source_t source;
	/* TEAMMATE records preserve the source of the authenticated runtime report.
	 * Other sources must leave this as SOURCE_SIGHT (the zero value). */
	sg_belief_runtime_source_t reported_source;
	sg_belief_runtime_evidence_kind_t evidence_kind;
	uint8_t reserved[2];
	sg_belief_runtime_life_t issuer_life;
	sg_belief_runtime_life_t target_life;
	uint64_t event_id;
	uint64_t evidence_sequence;
	uint64_t observed_at_ms;
	uint64_t authenticated_at_ms;
	uint64_t valid_until_ms;
	uint64_t rune_identity;
	uint64_t topology_revision;
	float confidence;
	const sg_belief_runtime_hypothesis_t *hypotheses;
	size_t hypothesis_count;
	const sg_belief_runtime_coverage_t *coverage;
	size_t coverage_count;
} sg_belief_runtime_observation_t;

typedef struct sg_belief_runtime_view_s
{
	uint8_t audience_team;
	uint8_t target_team;
	uint8_t exact_sight;
	uint8_t latest_source;
	sg_belief_runtime_life_t target_life;
	uint64_t observed_at_ms;
	uint64_t updated_at_ms;
	uint64_t valid_until_ms;
	float confidence;
	const sg_belief_runtime_particle_t *particles;
	size_t particle_count;
} sg_belief_runtime_view_t;

typedef enum sg_belief_runtime_observe_result_e
{
	SG_BELIEF_RUNTIME_OBSERVE_APPLIED = 0,
	SG_BELIEF_RUNTIME_OBSERVE_UNAVAILABLE,
	SG_BELIEF_RUNTIME_OBSERVE_REJECTED,
	SG_BELIEF_RUNTIME_OBSERVE_CAPACITY,
	SG_BELIEF_RUNTIME_OBSERVE_OVERFLOW
} sg_belief_runtime_observe_result_t;

typedef enum sg_belief_runtime_frame_result_e
{
	SG_BELIEF_RUNTIME_FRAME_APPLIED = 0,
	SG_BELIEF_RUNTIME_FRAME_UNAVAILABLE,
	SG_BELIEF_RUNTIME_FRAME_REJECTED,
	SG_BELIEF_RUNTIME_FRAME_OVERFLOW
} sg_belief_runtime_frame_result_t;

/* Registration is a level-transition boundary. The compact model was already
 * accepted by the artifact owner; this stores its exact identity and clears
 * all process-local beliefs before the new borrowed model is used. */
int SG_BeliefRuntimeProviderSet(const sg_belief_runtime_provider_t *provider);
int SG_BeliefRuntimeProviderAvailable(void);
const sg_belief_runtime_provider_t *SG_BeliefRuntimeProvider(void);
void SG_BeliefRuntimeReset(void);

sg_belief_runtime_frame_result_t SG_BeliefRuntimeFrame(
	uint64_t frame_sequence, uint64_t at_ms);
int SG_BeliefRuntimeRetireLife(const sg_belief_runtime_life_t *life);
const sg_belief_runtime_view_t *SG_BeliefRuntimeView(uint8_t audience_team,
	const sg_belief_runtime_life_t *target_life, uint64_t at_ms);
const sg_belief_runtime_view_t *SG_BeliefRuntimeViewForClient(
	uint8_t audience_team, uint32_t client_id, uint64_t at_ms);

#endif /* SG_BELIEF_RUNTIME_H */
