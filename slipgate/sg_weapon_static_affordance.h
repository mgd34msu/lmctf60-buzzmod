/* Weapon-specific interpretation of shared, immutable static visibility. */
#ifndef SG_WEAPON_STATIC_AFFORDANCE_H
#define SG_WEAPON_STATIC_AFFORDANCE_H

#include <stdint.h>

#include "sg_configuration_audit.h"
#include "sg_rune_v2_artifact_loader.h"
#include "sg_static_visibility.h"
#include "sg_static_visibility_publication.h"
#include "sg_weapon_effect_profile.h"

#define SG_WEAPON_STATIC_RELATION_COUNT UINT32_C(7)

typedef enum sg_weapon_static_status_e
{
	SG_WEAPON_STATIC_NOT_REQUESTED = 0,
	SG_WEAPON_STATIC_REJECTED,
	SG_WEAPON_STATIC_CONDITIONAL,
	SG_WEAPON_STATIC_PROVEN
} sg_weapon_static_status_t;

typedef enum sg_weapon_static_reason_e
{
	SG_WEAPON_STATIC_REASON_NONE = 0,
	SG_WEAPON_STATIC_REASON_PROFILE_UNSUPPORTED,
	SG_WEAPON_STATIC_REASON_VISIBILITY,
	SG_WEAPON_STATIC_REASON_TARGET_NOT_SURFACE,
	SG_WEAPON_STATIC_REASON_OUTSIDE_SPLASH_REACH,
	SG_WEAPON_STATIC_REASON_PROJECTILE_CLEARANCE,
	SG_WEAPON_STATIC_REASON_UNPROVEN_SURFACE_COVERAGE,
	SG_WEAPON_STATIC_REASON_RUNTIME_PROJECTILE_ORIGIN,
	SG_WEAPON_STATIC_REASON_OWNER_DAMAGE_VISIBILITY
} sg_weapon_static_reason_t;

typedef struct sg_weapon_static_relation_result_s
{
	sg_weapon_static_relation_t relation;
	sg_weapon_static_status_t status;
	sg_weapon_static_reason_t reason;
	sg_static_visibility_result_t visibility;
	sg_rune_vec3_t witness_point;
	uint8_t has_witness_point;
	uint8_t reserved[3];
} sg_weapon_static_relation_result_t;

/* This value is static evidence only. It deliberately has no shot-authority
 * field. A caller must still obtain an exact, authenticated live pre-fire
 * trace through the host weapon boundary. */
typedef struct sg_weapon_static_affordance_s
{
	sg_weapon_static_binding_t binding;
	sg_weapon_profile_id_t profile_id;
	sg_weapon_family_t family;
	sg_weapon_static_relation_t requested_relations;
	sg_weapon_static_relation_t allowed_relations;
	sg_weapon_static_relation_t proven_relations;
	sg_weapon_static_relation_t rejected_relations;
	sg_weapon_static_relation_t conditional_relations;
	uint8_t exact_authenticated_live_prefire_trace_required;
	uint8_t reserved[3];
	uint32_t spatial_nodes_visited;
	uint32_t candidate_surfaces_visited;
	uint32_t candidate_points_queried;
	uint32_t pose_partition_nodes_visited;
	uint32_t pose_partition_bounds_overlaps;
	uint32_t pose_partition_faces_tested;
	sg_weapon_static_relation_result_t
		relations[SG_WEAPON_STATIC_RELATION_COUNT];
} sg_weapon_static_affordance_t;

/* Contexts remain source-bound while a result publication is audited. The
 * opaque publication copies the accepted result, but never grants shot
 * authority or retains a live scene. */
typedef struct sg_weapon_static_context_s sg_weapon_static_context_t;
typedef struct sg_weapon_static_result_publication_s
	sg_weapon_static_result_publication_t;

/* This is the complete immutable evidence emitted by the weapon owner. The
 * static-scene marker is deliberately explicit: a publication has no actor,
 * mover, or other runtime-scene input. */
typedef struct sg_weapon_static_result_evidence_s
{
	sg_rune_model_identity_t static_identity;
	sg_weapon_static_binding_t binding;
	sg_weapon_static_query_t query;
	sg_weapon_law_input_t law;
	sg_weapon_profile_t profile;
	sg_weapon_static_affordance_t affordance;
	uint64_t context_binding_comparisons;
	uint64_t context_partition_preparation_work;
	uint64_t context_surface_preparation_work;
	uint8_t static_scene_only;
	uint8_t reserved[7];
} sg_weapon_static_result_evidence_t;

/* candidate is untrusted public output. Issue re-resolves the exact query in
 * the weapon owner against a strictly empty static scene and accepts it only
 * if every relation, witness, status, rejection, and work field agrees. */
typedef struct sg_weapon_static_result_publication_input_s
{
	const sg_weapon_static_context_t *context;
	const sg_host_collision_scene_t *scene;
	const sg_weapon_static_query_t *query;
	const sg_weapon_law_input_t *law;
	sg_weapon_profile_id_t profile_id;
	const sg_weapon_static_affordance_t *candidate;
} sg_weapon_static_result_publication_input_t;

typedef enum sg_weapon_static_affordance_error_code_e
{
	SG_WEAPON_STATIC_AFFORDANCE_ERROR_NONE = 0,
	SG_WEAPON_STATIC_AFFORDANCE_ERROR_INVALID_ARGUMENT,
	SG_WEAPON_STATIC_AFFORDANCE_ERROR_INVALID_QUERY,
	SG_WEAPON_STATIC_AFFORDANCE_ERROR_IDENTITY_MISMATCH,
	SG_WEAPON_STATIC_AFFORDANCE_ERROR_INVALID_PROFILE,
	SG_WEAPON_STATIC_AFFORDANCE_ERROR_INVALID_SOURCE,
	SG_WEAPON_STATIC_AFFORDANCE_ERROR_VISIBILITY
} sg_weapon_static_affordance_error_code_t;

typedef struct sg_weapon_static_affordance_error_s
{
	sg_weapon_static_affordance_error_code_t code;
	sg_static_visibility_error_t visibility;
} sg_weapon_static_affordance_error_t;

typedef enum sg_weapon_static_result_publication_error_code_e
{
	SG_WEAPON_STATIC_RESULT_PUBLICATION_ERROR_NONE = 0,
	SG_WEAPON_STATIC_RESULT_PUBLICATION_ERROR_INVALID_ARGUMENT,
	SG_WEAPON_STATIC_RESULT_PUBLICATION_ERROR_STATIC_SCENE_REJECTED,
	SG_WEAPON_STATIC_RESULT_PUBLICATION_ERROR_RESOLUTION_REJECTED,
	SG_WEAPON_STATIC_RESULT_PUBLICATION_ERROR_CANDIDATE_REJECTED,
	SG_WEAPON_STATIC_RESULT_PUBLICATION_ERROR_SOURCE_MISMATCH,
	SG_WEAPON_STATIC_RESULT_PUBLICATION_ERROR_OUT_OF_MEMORY,
	SG_WEAPON_STATIC_RESULT_PUBLICATION_ERROR_COPY_DISAGREEMENT
} sg_weapon_static_result_publication_error_code_t;

typedef struct sg_weapon_static_result_publication_error_s
{
	sg_weapon_static_result_publication_error_code_t code;
	sg_weapon_static_affordance_error_t resolution;
} sg_weapon_static_result_publication_error_t;

typedef enum sg_weapon_static_result_publication_audit_code_e
{
	SG_WEAPON_STATIC_RESULT_PUBLICATION_AUDIT_OK = 0,
	SG_WEAPON_STATIC_RESULT_PUBLICATION_AUDIT_INVALID_ARGUMENT,
	SG_WEAPON_STATIC_RESULT_PUBLICATION_AUDIT_STORAGE_DISAGREEMENT,
	SG_WEAPON_STATIC_RESULT_PUBLICATION_AUDIT_SOURCE_MISMATCH,
	SG_WEAPON_STATIC_RESULT_PUBLICATION_AUDIT_EVIDENCE_DISAGREEMENT,
	SG_WEAPON_STATIC_RESULT_PUBLICATION_AUDIT_DIGEST_DISAGREEMENT
} sg_weapon_static_result_publication_audit_code_t;

typedef struct sg_weapon_static_result_publication_audit_report_s
{
	sg_weapon_static_result_publication_audit_code_t code;
	sg_weapon_static_affordance_error_t resolution;
} sg_weapon_static_result_publication_audit_report_t;

typedef enum sg_weapon_static_prepare_error_code_e
{
	SG_WEAPON_STATIC_PREPARE_ERROR_NONE = 0,
	SG_WEAPON_STATIC_PREPARE_ERROR_INVALID_ARGUMENT,
	SG_WEAPON_STATIC_PREPARE_ERROR_BINDING,
	SG_WEAPON_STATIC_PREPARE_ERROR_CONFIGURATION_AUDIT,
	SG_WEAPON_STATIC_PREPARE_ERROR_SEMANTICS_AUDIT,
	SG_WEAPON_STATIC_PREPARE_ERROR_VISIBILITY_AUDIT,
	SG_WEAPON_STATIC_PREPARE_ERROR_MODEL_VALIDATION,
	SG_WEAPON_STATIC_PREPARE_ERROR_SOURCE_MISMATCH,
	SG_WEAPON_STATIC_PREPARE_ERROR_OVERFLOW,
	SG_WEAPON_STATIC_PREPARE_ERROR_OUT_OF_MEMORY
} sg_weapon_static_prepare_error_code_t;

typedef struct sg_weapon_static_prepare_error_s
{
	sg_weapon_static_prepare_error_code_t code;
	sg_configuration_audit_code_t configuration;
	sg_configuration_semantics_audit_code_t semantics;
	sg_static_visibility_audit_code_t visibility;
	sg_rune_failure_reason_t model;
	uint32_t record;
} sg_weapon_static_prepare_error_t;

/* Provisional exact-byte loader bridge. It authenticates ownership and
 * lifetime of the immutable decoded snapshot, but does not establish the
 * future semantic-acceptance publication. Preparation and query-time reload
 * checks consume this pair through the isolated bridge reader. */
typedef struct sg_weapon_static_artifact_loader_bridge_s
{
	const sg_rune_v2_artifact_loader_t *loader;
	const sg_rune_v2_artifact_snapshot_t *snapshot;
} sg_weapon_static_artifact_loader_bridge_t;

typedef struct sg_weapon_static_prepare_input_s
{
	sg_weapon_static_artifact_loader_bridge_t artifact;
	const sg_static_visibility_publication_t *visibility_publication;
} sg_weapon_static_prepare_input_t;

/* Preparation validates every borrowed source and builds the owned lookup
 * indices. The borrowed sources must remain immutable and live until destroy. */

int SG_WeaponStaticContextPrepare(
	const sg_weapon_static_prepare_input_t *input,
	sg_weapon_static_context_t **context_out,
	sg_weapon_static_prepare_error_t *error_out);

/* This is an acceptance identity only. The publication pointer is borrowed
 * and is useful only while the context remains live; callers must not retain
 * it after destroying the context. No geometry or resolver state is exposed. */
int SG_WeaponStaticContextSource(const sg_weapon_static_context_t *context,
	sg_weapon_static_binding_t *binding_out,
	const sg_static_visibility_publication_t **visibility_publication_out);

void SG_WeaponStaticContextDestroy(sg_weapon_static_context_t *context);

/* Preparation work evidence. This counts stable-ID comparisons used to build
 * the accepted configuration/model binding; resolver queries do not change it. */
uint64_t SG_WeaponStaticContextBindingComparisons(
	const sg_weapon_static_context_t *context);
uint64_t SG_WeaponStaticContextPartitionPreparationWork(
	const sg_weapon_static_context_t *context);
uint64_t SG_WeaponStaticContextSurfacePreparationWork(
	const sg_weapon_static_context_t *context);

int SG_WeaponStaticAffordanceResolve(
	const sg_weapon_static_context_t *context,
	const sg_host_collision_scene_t *scene,
	const sg_weapon_static_query_t *query,
	const sg_weapon_law_input_t *law,
	sg_weapon_profile_id_t profile_id,
	sg_weapon_static_affordance_t *affordance_out,
	sg_weapon_static_affordance_error_t *error_out);

int SG_WeaponStaticResultPublicationIssue(
	const sg_weapon_static_result_publication_input_t *input,
	sg_weapon_static_result_publication_t **publication_out,
	sg_weapon_static_result_publication_error_t *error_out);
/* Audit replays the owner resolver while the source context remains live. */
int SG_WeaponStaticResultPublicationAudit(
	const sg_weapon_static_result_publication_t *publication,
	sg_weapon_static_result_publication_audit_report_t *report_out);
int SG_WeaponStaticResultPublicationEvidence(
	const sg_weapon_static_result_publication_t *publication,
	sg_weapon_static_result_evidence_t *evidence_out);
/* This preserves the exact accepted context identity without exposing the
 * context pointer from the opaque publication.  Catalog callers pair this
 * with Evidence(), which performs the full replay audit. */
int SG_WeaponStaticResultPublicationContextMatches(
	const sg_weapon_static_result_publication_t *publication,
	const sg_weapon_static_context_t *context);
void SG_WeaponStaticResultPublicationDestroy(
	sg_weapon_static_result_publication_t *publication);
const char *SG_WeaponStaticResultPublicationErrorString(
	sg_weapon_static_result_publication_error_code_t code);
const char *SG_WeaponStaticResultPublicationAuditCodeString(
	sg_weapon_static_result_publication_audit_code_t code);

const char *SG_WeaponStaticAffordanceErrorString(
	sg_weapon_static_affordance_error_code_t code);

#endif /* SG_WEAPON_STATIC_AFFORDANCE_H */
