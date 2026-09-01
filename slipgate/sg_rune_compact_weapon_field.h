/* Sealed profile-family weapon response kernels for compact RUNEs. */
#ifndef SG_RUNE_COMPACT_WEAPON_FIELD_H
#define SG_RUNE_COMPACT_WEAPON_FIELD_H

#include <stdint.h>

#include "sg_rune_compact_model.h"
#include "sg_rune_source_authority.h"
#include "sg_weapon_effect_profile.h"

typedef struct sg_rune_compact_weapon_relations_s
	sg_rune_compact_weapon_relations_t;

/* One attachment owns every kernel-independent fact for one static relation
 * class at one compact cell/source-surface. Kernel lookup derives this class
 * from its canonical profile-family pair; facts are never copied per kernel. */
typedef struct sg_rune_compact_weapon_field_attachment_s
{
	sg_rune_compact_cell_index_t cell;
	uint32_t source_surface;
	sg_rune_compact_weapon_relation_class_t relation_class;
	uint32_t reserved0;
	sg_rune_compact_response_ref_span_t relations;
	uint32_t relation_span;
	uint32_t reserved1;
} sg_rune_compact_weapon_field_attachment_t;

typedef struct sg_rune_compact_weapon_field_input_s
{
	const sg_rune_compact_identity_t *identity;
	const sg_rune_weapon_profile_t *compact_profiles;
	const sg_weapon_profile_t *resolved_profiles;
	uint32_t profile_count;
	const sg_rune_source_weapon_law_t *weapon_law;
	uint64_t physics_abi_id;
	uint64_t weapon_law_id;
	/* The field borrows this immutable owner so its sparse relation references
	 * always resolve through the same response projection as movement. */
	const sg_rune_compact_weapon_relations_t *relations_owner;
} sg_rune_compact_weapon_field_input_t;

typedef struct sg_rune_compact_weapon_field_s sg_rune_compact_weapon_field_t;

/* An identity-bound immutable borrow. The owner alone allocates and frees its
 * backing arrays, so callers cannot combine an identity from one field with
 * payload from another. */
typedef struct sg_rune_compact_weapon_field_view_s
{
	sg_rune_compact_identity_t identity;
	const sg_rune_weapon_response_kernel_t *kernels;
	uint32_t kernel_count;
	const sg_rune_compact_weapon_field_attachment_t *attachments;
	uint32_t attachment_count;
	const sg_rune_compact_weapon_relation_span_t *relation_spans;
	uint32_t relation_span_count;
	const sg_rune_compact_response_ref_t *relation_refs;
	uint32_t relation_ref_count;
	/* This is a shallow borrowed projection from relations_owner.  The
	 * attachment spans index relation_refs, whose certified-fact indexes resolve
	 * through this projection. */
	const sg_rune_compact_response_projection_t *response;
	const sg_rune_weapon_function_ref_t *weapon_function_refs;
	uint32_t weapon_function_ref_count;
	const sg_rune_analytic_function_t *functions;
	const sg_rune_analytic_input_dimension_t *input_dimensions;
	const sg_rune_analytic_constant_t *constants;
	const sg_rune_analytic_affine_t *affines;
	const sg_rune_analytic_scalar_bits_t *affine_slopes;
	const sg_rune_analytic_ballistic_t *ballistics;
	sg_rune_compact_analytic_t analytic;
} sg_rune_compact_weapon_field_view_t;

typedef enum sg_rune_compact_weapon_field_status_e
{
	SG_RUNE_COMPACT_WEAPON_FIELD_OK = 0,
	SG_RUNE_COMPACT_WEAPON_FIELD_INVALID_ARGUMENT,
	SG_RUNE_COMPACT_WEAPON_FIELD_INVALID_WEAPON_LAW,
	SG_RUNE_COMPACT_WEAPON_FIELD_INVALID_PROFILE,
	SG_RUNE_COMPACT_WEAPON_FIELD_INVALID_RELATIONS,
	SG_RUNE_COMPACT_WEAPON_FIELD_UNSUPPORTED_SCHEMA,
	SG_RUNE_COMPACT_WEAPON_FIELD_LIMIT_EXCEEDED,
	SG_RUNE_COMPACT_WEAPON_FIELD_ALLOCATION_FAILED,
	SG_RUNE_COMPACT_WEAPON_FIELD_STATUS_COUNT
} sg_rune_compact_weapon_field_status_t;

typedef struct sg_rune_compact_weapon_field_error_s
{
	sg_rune_compact_weapon_field_status_t status;
	uint32_t record;
} sg_rune_compact_weapon_field_error_t;

/* Builds one typed response kernel per sealed profile-family pair and sparse
 * cell/surface attachments over the shared certified response projection. A
 * live pre-fire trace remains a runtime boundary. */
sg_rune_compact_weapon_field_status_t SG_RuneCompactWeaponFieldBuild(
	const sg_rune_compact_weapon_field_input_t *input,
	sg_rune_compact_weapon_field_t **field_out,
	sg_rune_compact_weapon_field_error_t *error_out);

void SG_RuneCompactWeaponFieldDestroy(sg_rune_compact_weapon_field_t *field);

int SG_RuneCompactWeaponFieldReadBound(
	const sg_rune_compact_weapon_field_t *field,
	sg_rune_compact_weapon_field_view_t *view_out);

const char *SG_RuneCompactWeaponFieldStatusString(
	sg_rune_compact_weapon_field_status_t status);

#if defined(SG_RUNE_COMPACT_WEAPON_FIELD_TEST_WRAP_CALLOC)
/* Test-only representation boundary override; production always uses the
 * immutable v12 model maxima. */
void SG_RuneCompactWeaponFieldTestSetRelationLimits(uint32_t attachments,
	uint32_t spans, uint32_t references);
void SG_RuneCompactWeaponFieldTestResetRelationLimits(void);
#endif

#endif
