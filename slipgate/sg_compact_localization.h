/* Allocation-free runtime localization against one accepted compact RUNE. */
#ifndef SG_COMPACT_LOCALIZATION_H
#define SG_COMPACT_LOCALIZATION_H

#include "sg_localization_runtime.h"
#include "sg_rune_compact_localize.h"
#include "sg_rune_compact_spatial_index.h"

/* Numeric recovery is an explicit compact-Q8 contract.  The public bound is
 * four Q8 units (0.5 world units); the implementation derives its search
 * radius from each sample's advertised distance and visits every bounded
 * offset. */
#define SG_COMPACT_LOCALIZATION_MAX_RECOVERY_DISTANCE 0.5f
#define SG_COMPACT_LOCALIZATION_MAX_RECOVERY_Q8 UINT32_C(4)
typedef struct sg_compact_localization_scratch_s
{
	uint32_t *candidates;
	uint32_t candidate_capacity;
	uint32_t candidate_count;
} sg_compact_localization_scratch_t;

typedef struct sg_compact_localization_observation_s
	sg_compact_localization_observation_t;

/* This view is filled only by the observation owner after it validates the
 * opaque capability against the exact host publication supplied by the
 * locator. Callers cannot construct an observation by filling this view. */
typedef struct sg_compact_localization_observation_view_s
{
	sg_localization_observation_kind_t kind;
	sg_localization_subject_t subject;
	uint64_t host_authority_epoch;
	uint64_t frame_sequence;
	uint64_t observed_at_ms;
	sg_localization_model_stamp_t model_stamp;
	const sg_host_pmove_result_t *pmove_result;
	const sg_host_pmove_state_observation_t *state_observation;
	float maximum_recovery_distance;
	uint64_t maximum_temporary_absence_ms;
	sg_localization_subject_t previous_subject;
	uint64_t previous_frame_sequence;
	uint64_t previous_observed_at_ms;
} sg_compact_localization_observation_view_t;

typedef sg_localization_status_t
(*sg_compact_localization_observation_validate_fn)(void *context,
	const sg_host_law_runtime_authority_t *authority,
	const sg_compact_localization_observation_t *observation,
	sg_compact_localization_observation_view_t *view_out);

typedef struct sg_compact_localization_observation_owner_s
{
	void *context;
	sg_compact_localization_observation_validate_fn validate;
} sg_compact_localization_observation_owner_t;

/* The compact artifact is borrowed.  The artifact loader owns its immutable
 * model and the host-law owner owns the authority; both must outlive this
 * binding and every query made through it. */
typedef struct sg_compact_localization_binding_s
{
	const sg_rune_compact_model_t *model;
	const sg_rune_compact_spatial_index_t *spatial_index;
	sg_rune_compact_identity_t identity;
	sg_host_law_runtime_authority_t host_authority;
	sg_compact_localization_observation_owner_t observation_owner;
	uint64_t rune_identity;
	uint64_t topology_revision;
	uint8_t bound;
	uint8_t reserved[7];
} sg_compact_localization_binding_t;

/* The caller carries one opaque owner-issued capability and no asserted
 * identity, lifecycle, collision, Pmove, or reset facts. */
typedef struct sg_compact_localization_sample_s
{
	const sg_compact_localization_observation_t *observation;
} sg_compact_localization_sample_t;

/* This state is a compact-cell localization result, not a strategy or a
 * movement command.  A cell result never proves a transition to another
 * cell; the tactical/runtime owners decide how to execute a destination. */
typedef struct sg_compact_localized_state_s
{
	sg_localization_subject_t subject;
	sg_localization_model_stamp_t model_stamp;
	uint64_t rune_identity;
	uint64_t topology_revision;
	uint64_t frame_sequence;
	uint64_t localized_at_ms;
	uint64_t absence_started_at_ms;
	sg_rune_compact_location_t location;
	sg_rune_stance_t stance;
	sg_rune_motion_t motion;
	sg_rune_support_t support;
	sg_rune_medium_t medium;
	sg_rune_void_relation_t void_relation;
	sg_rune_reference_frame_t reference_frame;
	uint32_t support_model_index;
	uint64_t support_instance_id;
	uint8_t water_level;
	uint8_t valid;
	uint8_t reserved[2];
	sg_host_collision_contents_t water_type;
	float position[3];
	float velocity[3];
	pmove_state_t host_state;
	sg_localization_presence_t presence;
	sg_localization_recovery_t recovery;
} sg_compact_localized_state_t;

/* Binding is an off-line/level-transition operation.  The full compact model
 * validation remains the artifact loader's responsibility; this boundary
 * checks its version, identity, shape, and exact host-law publication before
 * retaining the borrowed pointers. */
sg_localization_status_t SG_CompactLocalizationBind(
	sg_compact_localization_binding_t *binding,
	const sg_rune_compact_model_t *model,
	const sg_rune_compact_identity_t *expected_identity,
	const sg_rune_compact_spatial_index_t *spatial_index,
	const sg_compact_localization_observation_owner_t *observation_owner,
	const sg_host_law_runtime_authority_t *host_authority,
	uint64_t rune_identity, uint64_t topology_revision);

void SG_CompactLocalizationUnbind(
	sg_compact_localization_binding_t *binding);

int SG_CompactLocalizationBindingCurrent(
	const sg_compact_localization_binding_t *binding);

int SG_CompactLocalizationStateCurrent(
	const sg_compact_localization_binding_t *binding,
	const sg_localization_subject_t *subject,
	const sg_compact_localized_state_t *state);

/* Runtime owners supply cell_count storage allocated at level install. Exact
 * bootstrap and teleport queries use the borrowed spatial index. Continuity
 * and recovery stream only the previous cell's incidence span. */
sg_localization_status_t SG_CompactLocalizationObserveWithScratch(
	const sg_compact_localization_binding_t *binding,
	const sg_compact_localization_sample_t *sample,
	const sg_compact_localized_state_t *previous,
	sg_compact_localization_scratch_t *scratch,
	sg_compact_localized_state_t *state_out);

#endif /* SG_COMPACT_LOCALIZATION_H */
