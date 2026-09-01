#include "sg_rune_compact_weapon_field.h"

#include "sg_rune_compact_weapon_catalog.h"
#include "sg_rune_compact_weapon_relations.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

struct sg_rune_compact_weapon_field_s
{
	sg_rune_compact_identity_t identity;
	sg_rune_weapon_response_kernel_t *kernels;
	uint32_t kernel_count;
	sg_rune_compact_weapon_field_attachment_t *attachments;
	uint32_t attachment_count;
	sg_rune_compact_weapon_relation_span_t *relation_spans;
	uint32_t relation_span_count;
	sg_rune_compact_response_ref_t *relation_refs;
	uint32_t relation_ref_count;
	const sg_rune_compact_weapon_relations_t *relations_owner;
	sg_rune_compact_weapon_relations_view_t relations;
	sg_rune_weapon_function_ref_t *weapon_function_refs;
	uint32_t weapon_function_ref_count;
	sg_rune_analytic_function_t *functions;
	sg_rune_analytic_input_dimension_t *input_dimensions;
	sg_rune_analytic_constant_t *constants;
	sg_rune_analytic_affine_t *affines;
	sg_rune_analytic_scalar_bits_t *affine_slopes;
	sg_rune_analytic_ballistic_t *ballistics;
	sg_rune_compact_analytic_t analytic;
};

typedef struct sg_rune_compact_weapon_family_law_s
{
	uint32_t channel_count;
	uint8_t requires_target_relation;
	uint8_t requires_channel_instances;
	uint8_t reserved[2];
} sg_rune_compact_weapon_family_law_t;

typedef struct sg_rune_compact_weapon_fact_group_record_s
{
	sg_rune_compact_cell_index_t cell;
	uint32_t source_surface;
	sg_rune_compact_weapon_relation_class_t relation_class;
	uint32_t fact_index;
} sg_rune_compact_weapon_fact_group_record_t;

typedef struct sg_rune_compact_weapon_attachment_plan_s
{
	sg_rune_compact_weapon_fact_group_record_t *records;
	uint32_t relation_count;
	uint32_t span_count;
} sg_rune_compact_weapon_attachment_plan_t;

#if defined(SG_RUNE_COMPACT_WEAPON_FIELD_TEST_WRAP_CALLOC)
static uint32_t test_attachment_limit =
	SG_RUNE_COMPACT_MAX_WEAPON_ATTACHMENTS;
static uint32_t test_span_limit = SG_RUNE_COMPACT_MAX_WEAPON_RELATION_SPANS;
static uint32_t test_reference_limit =
	SG_RUNE_COMPACT_MAX_WEAPON_RELATION_REFS;

void SG_RuneCompactWeaponFieldTestSetRelationLimits(uint32_t attachments,
	uint32_t spans, uint32_t references)
{
	test_attachment_limit = attachments;
	test_span_limit = spans;
	test_reference_limit = references;
}

void SG_RuneCompactWeaponFieldTestResetRelationLimits(void)
{
	test_attachment_limit = SG_RUNE_COMPACT_MAX_WEAPON_ATTACHMENTS;
	test_span_limit = SG_RUNE_COMPACT_MAX_WEAPON_RELATION_SPANS;
	test_reference_limit = SG_RUNE_COMPACT_MAX_WEAPON_RELATION_REFS;
}

#define WEAPON_ATTACHMENT_LIMIT test_attachment_limit
#define WEAPON_RELATION_SPAN_LIMIT test_span_limit
#define WEAPON_RELATION_REFERENCE_LIMIT test_reference_limit
#else
#define WEAPON_ATTACHMENT_LIMIT SG_RUNE_COMPACT_MAX_WEAPON_ATTACHMENTS
#define WEAPON_RELATION_SPAN_LIMIT SG_RUNE_COMPACT_MAX_WEAPON_RELATION_SPANS
#define WEAPON_RELATION_REFERENCE_LIMIT SG_RUNE_COMPACT_MAX_WEAPON_RELATION_REFS
#endif

static void SetError(sg_rune_compact_weapon_field_error_t *error,
	sg_rune_compact_weapon_field_status_t status, uint32_t record)
{
	if (error != NULL) {
		error->status = status;
		error->record = record;
	}
}

static int ArrayPresent(const void *array, uint32_t count)
{
	return count == 0U || array != NULL;
}

static int CompareU32(uint32_t left, uint32_t right)
{
	return left < right ? -1 : left > right ? 1 : 0;
}

static uint32_t FloatBits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static float FloatFromBits(uint32_t bits)
{
	float value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

static int WeaponLawValid(const sg_rune_source_weapon_law_t *law)
{
	return law != NULL &&
		law->weapon_balance_compiled == (uint8_t)SG_WEAPON_BALANCE_COMPILED &&
		law->weapon_balance_enabled <= 1U && law->rail_match_active <= 1U &&
		law->deathmatch_active <= 1U && law->fast_switch_enabled <= 1U &&
		law->reserved[0] == 0U && law->reserved[1] == 0U &&
		law->reserved[2] == 0U &&
		(law->weapon_balance_compiled != 0U ||
		 law->weapon_balance_enabled == 0U);
}

static int RelationProjectionEqual(
	const sg_rune_compact_response_projection_t *left,
	const sg_rune_compact_response_projection_t *right)
{
	return left != NULL && right != NULL &&
		left->source_fragments == right->source_fragments &&
		left->source_fragment_count == right->source_fragment_count &&
		left->source_halfspaces == right->source_halfspaces &&
		left->source_halfspace_count == right->source_halfspace_count &&
		left->target_patches == right->target_patches &&
		left->target_patch_count == right->target_patch_count &&
		left->target_vertices == right->target_vertices &&
		left->target_vertex_count == right->target_vertex_count &&
		left->splits == right->splits && left->split_count == right->split_count &&
		left->facts == right->facts && left->fact_count == right->fact_count &&
		left->candidate_groups == right->candidate_groups &&
		left->candidate_group_count == right->candidate_group_count &&
		left->source_endpoint_groups == right->source_endpoint_groups &&
		left->source_endpoint_group_count ==
			right->source_endpoint_group_count &&
		left->source_endpoint_members == right->source_endpoint_members &&
		left->source_endpoint_member_count ==
			right->source_endpoint_member_count &&
		left->target_endpoint_groups == right->target_endpoint_groups &&
		left->target_endpoint_group_count ==
			right->target_endpoint_group_count &&
		left->target_endpoint_members == right->target_endpoint_members &&
		left->target_endpoint_member_count ==
			right->target_endpoint_member_count &&
		left->occluders == right->occluders &&
		left->occluder_count == right->occluder_count &&
		memcmp(&left->seal, &right->seal, sizeof(left->seal)) == 0 &&
		left->exact_live_prefire_trace_required ==
			right->exact_live_prefire_trace_required &&
		left->reserved[0] == right->reserved[0] &&
		left->reserved[1] == right->reserved[1] &&
		left->reserved[2] == right->reserved[2];
}

static int RelationsValid(const sg_rune_compact_weapon_relations_view_t *view,
	const sg_rune_compact_identity_t *identity)
{
	const sg_rune_compact_response_projection_t *response;
	uint32_t index;

	if (view == NULL || identity == NULL ||
		!SG_RuneCompactIdentityMatches(&view->identity, identity) ||
		view->owner == NULL)
		return 0;
	response = &view->response;
	if (response->exact_live_prefire_trace_required != 1U ||
		!ArrayPresent(response->source_fragments,
			response->source_fragment_count) ||
		!ArrayPresent(response->target_patches, response->target_patch_count) ||
		!ArrayPresent(response->facts, response->fact_count) ||
		response->seal.compact_cell_count == 0U ||
		response->seal.compact_source_surface_count == 0U)
		return 0;
	for (index = 0U; index < response->fact_count; index++) {
		const sg_rune_compact_response_fact_t *fact = &response->facts[index];
		const sg_rune_compact_response_fragment_t *fragment;
		const sg_rune_compact_response_patch_t *patch;

		if (fact->source_fragment >= response->source_fragment_count ||
			fact->target_patch >= response->target_patch_count ||
			fact->requires_exact_ray > 1U || fact->requires_area_state > 1U ||
			(fact->flags & ~(sg_rune_compact_static_relation_flags_t)
				SG_RUNE_COMPACT_STATIC_RELATION_FLAGS_KNOWN) != 0U)
			return 0;
		fragment = &response->source_fragments[fact->source_fragment];
		patch = &response->target_patches[fact->target_patch];
		if (fragment->parent_cell.value >= response->seal.compact_cell_count ||
			patch->source_surface >=
				response->seal.compact_source_surface_count)
			return 0;
	}
	return 1;
}

static int ReadRelations(const sg_rune_compact_weapon_field_input_t *input,
	sg_rune_compact_weapon_relations_view_t *view_out)
{
	return input != NULL && input->relations_owner != NULL &&
		view_out != NULL &&
		SG_RuneCompactWeaponRelationsRead(input->relations_owner, view_out) &&
		RelationsValid(view_out, input->identity);
}

static sg_rune_weapon_response_family_mask_t ResponseMask(
	const sg_weapon_profile_t *profile)
{
	return profile == NULL ? 0U :
		SG_RuneCompactWeaponCanonicalProfileMask((uint32_t)profile->id);
}

static int ProfileMatchesFamilyLaw(const sg_weapon_profile_t *profile,
	sg_rune_weapon_response_family_t family,
	sg_rune_compact_weapon_family_law_t *law_out)
{
	const sg_weapon_effect_flag_t effects = profile->effects;
	sg_rune_compact_weapon_family_law_t law = { 0 };

	switch (family) {
	case SG_RUNE_WEAPON_RESPONSE_HITSCAN:
		if ((effects & SG_WEAPON_EFFECT_HITSCAN) == 0U)
			return 0;
		law.channel_count = 1U;
		break;
	case SG_RUNE_WEAPON_RESPONSE_RAIL:
		if (profile->id != SG_WEAPON_PROFILE_RAILGUN ||
			(effects & (SG_WEAPON_EFFECT_HITSCAN |
				SG_WEAPON_EFFECT_PENETRATION)) !=
				(SG_WEAPON_EFFECT_HITSCAN |
				 SG_WEAPON_EFFECT_PENETRATION))
			return 0;
		law.channel_count = 1U + (uint32_t)profile->auxiliary_trace_count;
		law.requires_channel_instances = profile->auxiliary_trace_count != 0U;
		break;
	case SG_RUNE_WEAPON_RESPONSE_AUTOMATIC_SPREAD:
		if ((profile->id != SG_WEAPON_PROFILE_MACHINEGUN &&
			 profile->id != SG_WEAPON_PROFILE_CHAINGUN) ||
			(effects & (SG_WEAPON_EFFECT_HITSCAN |
				SG_WEAPON_EFFECT_SPREAD)) !=
				(SG_WEAPON_EFFECT_HITSCAN | SG_WEAPON_EFFECT_SPREAD))
			return 0;
		law.channel_count = profile->projectile_count_max;
		law.requires_channel_instances = law.channel_count > 1U;
		break;
	case SG_RUNE_WEAPON_RESPONSE_SHOTGUN_CONE:
		if ((profile->id != SG_WEAPON_PROFILE_SHOTGUN &&
			 profile->id != SG_WEAPON_PROFILE_SUPER_SHOTGUN) ||
			(effects & (SG_WEAPON_EFFECT_HITSCAN |
				SG_WEAPON_EFFECT_SPREAD |
				SG_WEAPON_EFFECT_MULTI_PROJECTILE)) !=
				(SG_WEAPON_EFFECT_HITSCAN | SG_WEAPON_EFFECT_SPREAD |
				 SG_WEAPON_EFFECT_MULTI_PROJECTILE))
			return 0;
		law.channel_count = profile->projectile_count_max;
		law.requires_channel_instances = law.channel_count > 1U;
		break;
	case SG_RUNE_WEAPON_RESPONSE_STRAIGHT_BOLT:
		if ((effects & SG_WEAPON_EFFECT_PROJECTILE) == 0U)
			return 0;
		law.channel_count = profile->projectile_count_max;
		law.requires_channel_instances = law.channel_count > 1U;
		break;
	case SG_RUNE_WEAPON_RESPONSE_ROCKET_IMPACT:
		if ((effects & SG_WEAPON_EFFECT_PROJECTILE) == 0U)
			return 0;
		law.channel_count = 1U;
		break;
	case SG_RUNE_WEAPON_RESPONSE_ROCKET_SPLASH:
		if ((effects & (SG_WEAPON_EFFECT_PROJECTILE |
				SG_WEAPON_EFFECT_SPLASH)) !=
				(SG_WEAPON_EFFECT_PROJECTILE | SG_WEAPON_EFFECT_SPLASH))
			return 0;
		law.channel_count = 1U;
		break;
	case SG_RUNE_WEAPON_RESPONSE_GRENADE_FLIGHT:
		if ((profile->id != SG_WEAPON_PROFILE_GRENADE_LAUNCHER &&
			 profile->id != SG_WEAPON_PROFILE_HAND_GRENADE) ||
			(effects & (SG_WEAPON_EFFECT_PROJECTILE |
				SG_WEAPON_EFFECT_BOUNCE)) !=
				(SG_WEAPON_EFFECT_PROJECTILE | SG_WEAPON_EFFECT_BOUNCE) ||
			profile->gravity_scale <= 0.0f)
			return 0;
		law.channel_count = 1U;
		break;
	case SG_RUNE_WEAPON_RESPONSE_GRENADE_BOUNCE_FUSE:
		if ((profile->id != SG_WEAPON_PROFILE_GRENADE_LAUNCHER &&
			 profile->id != SG_WEAPON_PROFILE_HAND_GRENADE) ||
			(effects & (SG_WEAPON_EFFECT_PROJECTILE |
				SG_WEAPON_EFFECT_BOUNCE)) !=
				(SG_WEAPON_EFFECT_PROJECTILE | SG_WEAPON_EFFECT_BOUNCE) ||
			profile->fuse_ms == 0U)
			return 0;
		law.channel_count = 2U;
		law.requires_channel_instances = 1U;
		break;
	case SG_RUNE_WEAPON_RESPONSE_HYPERBLASTER:
		if (profile->id != SG_WEAPON_PROFILE_HYPERBLASTER ||
			(effects & SG_WEAPON_EFFECT_PROJECTILE) == 0U)
			return 0;
		law.channel_count = 1U;
		break;
	case SG_RUNE_WEAPON_RESPONSE_BFG:
		if (profile->id != SG_WEAPON_PROFILE_BFG ||
			(effects & (SG_WEAPON_EFFECT_PROJECTILE |
				SG_WEAPON_EFFECT_SPLASH |
				SG_WEAPON_EFFECT_SECONDARY_AREA |
				SG_WEAPON_EFFECT_PENETRATION |
				SG_WEAPON_EFFECT_PERIODIC_RAY |
				SG_WEAPON_EFFECT_SPECIAL)) !=
				(SG_WEAPON_EFFECT_PROJECTILE | SG_WEAPON_EFFECT_SPLASH |
				 SG_WEAPON_EFFECT_SECONDARY_AREA |
				 SG_WEAPON_EFFECT_PENETRATION |
				 SG_WEAPON_EFFECT_PERIODIC_RAY |
				 SG_WEAPON_EFFECT_SPECIAL))
			return 0;
		law.channel_count = 4U;
		law.requires_channel_instances = 1U;
		break;
	case SG_RUNE_WEAPON_RESPONSE_SPECIAL:
		if ((profile->id == SG_WEAPON_PROFILE_BFG ||
			 profile->id == SG_WEAPON_PROFILE_HOOK) &&
			(effects & SG_WEAPON_EFFECT_SPECIAL) == 0U)
			return 0;
		if (profile->id == SG_WEAPON_PROFILE_PLASMA_REFLECT &&
			(effects & (SG_WEAPON_EFFECT_PROJECTILE |
				SG_WEAPON_EFFECT_BOUNCE)) !=
				(SG_WEAPON_EFFECT_PROJECTILE | SG_WEAPON_EFFECT_BOUNCE))
			return 0;
		if (profile->id == SG_WEAPON_PROFILE_PLASMA_SPREAD &&
			(effects & (SG_WEAPON_EFFECT_PROJECTILE |
				SG_WEAPON_EFFECT_SPREAD |
				SG_WEAPON_EFFECT_MULTI_PROJECTILE)) !=
				(SG_WEAPON_EFFECT_PROJECTILE | SG_WEAPON_EFFECT_SPREAD |
				 SG_WEAPON_EFFECT_MULTI_PROJECTILE))
			return 0;
		if (profile->id != SG_WEAPON_PROFILE_BFG &&
			profile->id != SG_WEAPON_PROFILE_PLASMA_REFLECT &&
			profile->id != SG_WEAPON_PROFILE_PLASMA_SPREAD &&
			profile->id != SG_WEAPON_PROFILE_HOOK)
			return 0;
		law.channel_count = profile->id == SG_WEAPON_PROFILE_HOOK ? 2U : 1U;
		law.requires_channel_instances = law.channel_count > 1U;
		break;
	case SG_RUNE_WEAPON_RESPONSE_FAMILY_COUNT:
		return 0;
	}
	law.requires_target_relation = 1U;
	if (law.channel_count == 0U)
		return 0;
	*law_out = law;
	return 1;
}

static int ProfilesValid(const sg_rune_compact_weapon_field_input_t *input)
{
	sg_weapon_law_input_t law;
	sg_rune_weapon_response_family_mask_t aggregate = 0U;
	uint64_t catalog_id;
	uint64_t weapon_law_id;
	uint32_t profile_index;

	if (input->profile_count != (uint32_t)SG_WeaponProfileCount() ||
		input->profile_count == 0U ||
		input->profile_count > SG_RUNE_COMPACT_MAX_WEAPON_PROFILES ||
		input->physics_abi_id == 0U || input->weapon_law_id == 0U)
		return 0;
	memset(&law, 0, sizeof(law));
	law.build_identity = input->resolved_profiles[0].build_identity;
	law.physics_abi_id = input->physics_abi_id;
	law.weapon_balance_compiled = input->weapon_law->weapon_balance_compiled;
	law.weapon_balance_enabled = input->weapon_law->weapon_balance_enabled;
	law.rail_match_active = input->weapon_law->rail_match_active;
	law.deathmatch_active = input->weapon_law->deathmatch_active;
	law.fast_switch_enabled = input->weapon_law->fast_switch_enabled;
	if (law.build_identity == 0U)
		return 0;
	for (profile_index = 0U; profile_index < input->profile_count;
		profile_index++) {
		const sg_rune_weapon_profile_t *compact =
			&input->compact_profiles[profile_index];
		const sg_weapon_profile_t *resolved =
			&input->resolved_profiles[profile_index];
		sg_weapon_profile_t expected;
		sg_rune_weapon_response_family_mask_t families;
		uint32_t family;

		if (compact->source_profile != profile_index + 1U ||
			!SG_RuneCompactWeaponProfileShapeValid(compact) ||
			!SG_WeaponProfileValid(resolved) || resolved->resolved != 1U ||
			resolved->build_identity != law.build_identity ||
			resolved->physics_abi_id != input->physics_abi_id ||
			!SG_WeaponProfileResolve(
				(sg_weapon_profile_id_t)compact->source_profile,
				&law, &expected) ||
			memcmp(&expected, resolved, sizeof(expected)) != 0)
			return 0;
		families = ResponseMask(resolved);
		if (families == 0U || compact->response_families != families ||
			compact->projectile_count_min != resolved->projectile_count_min ||
			compact->projectile_count_max != resolved->projectile_count_max ||
			compact->auxiliary_trace_count != resolved->auxiliary_trace_count ||
			compact->direct_response_count !=
				(FloatBits(resolved->direct_damage) ==
				 FloatBits(resolved->direct_damage_max) ? 2U : 1U))
			return 0;
		for (family = 0U;
			family < (uint32_t)SG_RUNE_WEAPON_RESPONSE_FAMILY_COUNT;
			family++) {
			sg_rune_compact_weapon_family_law_t family_law;

			if ((families & SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(family)) == 0U)
				continue;
			if (!ProfileMatchesFamilyLaw(resolved,
				(sg_rune_weapon_response_family_t)family, &family_law))
				return 0;
		}
		aggregate |= families;
	}
	return aggregate == SG_RUNE_WEAPON_RESPONSE_FAMILIES_ALL &&
		SG_RuneCompactWeaponProfileCatalogId(input->compact_profiles,
			input->profile_count, &catalog_id) &&
		catalog_id == input->identity->weapon_profile_catalog_id &&
		SG_RuneCompactWeaponLawIdentity(input->weapon_law,
			input->resolved_profiles, input->profile_count, &weapon_law_id) &&
		weapon_law_id == input->weapon_law_id &&
		weapon_law_id == input->identity->weapon_law_id;
}

static int InputValid(const sg_rune_compact_weapon_field_input_t *input,
	sg_rune_compact_weapon_relations_view_t *relations_out,
	sg_rune_compact_weapon_field_error_t *error_out)
{
	if (input == NULL || input->identity == NULL ||
		!ArrayPresent(input->compact_profiles, input->profile_count) ||
		!ArrayPresent(input->resolved_profiles, input->profile_count) ||
		input->relations_owner == NULL) {
		SetError(error_out, SG_RUNE_COMPACT_WEAPON_FIELD_INVALID_ARGUMENT, 0U);
		return 0;
	}
	if (input->identity->physics_abi_id != input->physics_abi_id ||
		input->identity->weapon_law_id != input->weapon_law_id) {
		SetError(error_out, SG_RUNE_COMPACT_WEAPON_FIELD_INVALID_ARGUMENT, 0U);
		return 0;
	}
	if (!WeaponLawValid(input->weapon_law)) {
		SetError(error_out, SG_RUNE_COMPACT_WEAPON_FIELD_INVALID_WEAPON_LAW,
			0U);
		return 0;
	}
	if (!ProfilesValid(input)) {
		SetError(error_out, SG_RUNE_COMPACT_WEAPON_FIELD_INVALID_PROFILE, 0U);
		return 0;
	}
	if (!ReadRelations(input, relations_out)) {
		SetError(error_out, SG_RUNE_COMPACT_WEAPON_FIELD_INVALID_RELATIONS,
			0U);
		return 0;
	}
	return 1;
}

typedef struct sg_rune_compact_weapon_function_spec_s
{
	sg_rune_compact_analytic_form_t form;
	sg_rune_analytic_output_meaning_t output;
	sg_rune_analytic_input_dimension_t input;
	sg_rune_analytic_scalar_bits_t first;
	sg_rune_analytic_scalar_bits_t second;
	sg_rune_analytic_scalar_bits_t third;
	uint32_t original;
} sg_rune_compact_weapon_function_spec_t;

static int EventLawFor(const sg_weapon_profile_t *profile,
	sg_rune_weapon_response_family_t family,
	sg_rune_weapon_event_law_t *law_out)
{
	return profile != NULL && SG_RuneCompactWeaponCanonicalEventLaw(
		(uint32_t)profile->id, family, law_out);
}

typedef enum sg_rune_compact_weapon_build_result_e
{
	SG_RUNE_COMPACT_WEAPON_BUILD_OK = 0,
	SG_RUNE_COMPACT_WEAPON_BUILD_UNSUPPORTED,
	SG_RUNE_COMPACT_WEAPON_BUILD_LIMIT
} sg_rune_compact_weapon_build_result_t;

static int CheckedAdd(uint32_t left, uint32_t right, uint32_t *out)
{
	if (left > UINT32_MAX - right)
		return 0;
	*out = left + right;
	return 1;
}

static int ScalarFromFloat(float value, sg_rune_analytic_scalar_bits_t *out)
{
	if (!isfinite(value) || FloatBits(value) == UINT32_C(0x80000000))
		return 0;
	out->bits = FloatBits(value);
	return 1;
}

static int FunctionSpecCompare(const void *left_pointer,
	const void *right_pointer)
{
	const sg_rune_compact_weapon_function_spec_t *left = left_pointer;
	const sg_rune_compact_weapon_function_spec_t *right = right_pointer;
	int comparison = CompareU32((uint32_t)left->form,
		(uint32_t)right->form);

	if (comparison == 0)
		comparison = CompareU32((uint32_t)left->output,
			(uint32_t)right->output);
	if (comparison == 0)
		comparison = CompareU32((uint32_t)left->input,
			(uint32_t)right->input);
	if (comparison == 0)
		comparison = CompareU32(left->first.bits, right->first.bits);
	if (comparison == 0)
		comparison = CompareU32(left->second.bits, right->second.bits);
	if (comparison == 0)
		comparison = CompareU32(left->third.bits, right->third.bits);
	return comparison;
}

static int FunctionSpecEqual(
	const sg_rune_compact_weapon_function_spec_t *left,
	const sg_rune_compact_weapon_function_spec_t *right)
{
	return FunctionSpecCompare(left, right) == 0;
}

static sg_rune_compact_weapon_build_result_t AppendFunctionRef(
	sg_rune_compact_weapon_field_t *field,
	sg_rune_compact_weapon_function_spec_t *specs, uint32_t spec_capacity,
	uint32_t *spec_count, uint32_t *reference_cursor,
	sg_rune_weapon_effect_channel_t channel, uint32_t instance,
	const sg_rune_compact_weapon_function_spec_t *spec)
{
	sg_rune_compact_weapon_function_spec_t *stored;

	if (*spec_count >= spec_capacity ||
		*reference_cursor >= field->weapon_function_ref_count)
		return SG_RUNE_COMPACT_WEAPON_BUILD_LIMIT;
	stored = &specs[*spec_count];
	*stored = *spec;
	stored->original = *spec_count;
	field->weapon_function_refs[*reference_cursor].function.value =
		*spec_count;
	field->weapon_function_refs[*reference_cursor].channel = channel;
	field->weapon_function_refs[*reference_cursor].instance = instance;
	(*spec_count)++;
	(*reference_cursor)++;
	return SG_RUNE_COMPACT_WEAPON_BUILD_OK;
}

static sg_rune_compact_weapon_build_result_t AppendConstantRef(
	sg_rune_compact_weapon_field_t *field,
	sg_rune_compact_weapon_function_spec_t *specs, uint32_t spec_capacity,
	uint32_t *spec_count, uint32_t *reference_cursor,
	sg_rune_weapon_effect_channel_t channel, uint32_t instance,
	sg_rune_analytic_output_meaning_t output, float value)
{
	sg_rune_compact_weapon_function_spec_t spec;

	memset(&spec, 0, sizeof(spec));
	spec.form = SG_RUNE_COMPACT_ANALYTIC_CONSTANT;
	spec.output = output;
	spec.input = SG_RUNE_ANALYTIC_INPUT_DIMENSION_COUNT;
	if (!ScalarFromFloat(value, &spec.first))
		return SG_RUNE_COMPACT_WEAPON_BUILD_UNSUPPORTED;
	return AppendFunctionRef(field, specs, spec_capacity, spec_count,
		reference_cursor, channel, instance, &spec);
}

static sg_rune_compact_weapon_build_result_t AppendDistanceTimeRef(
	sg_rune_compact_weapon_field_t *field,
	sg_rune_compact_weapon_function_spec_t *specs, uint32_t spec_capacity,
	uint32_t *spec_count, uint32_t *reference_cursor,
	sg_rune_weapon_effect_channel_t channel, uint32_t instance, float speed)
{
	sg_rune_compact_weapon_function_spec_t spec;
	const float inverse_speed = 1.0f / speed;

	if (!isfinite(speed) || speed <= 0.0f || !isfinite(inverse_speed))
		return SG_RUNE_COMPACT_WEAPON_BUILD_UNSUPPORTED;
	memset(&spec, 0, sizeof(spec));
	spec.form = SG_RUNE_COMPACT_ANALYTIC_AFFINE;
	spec.output = SG_RUNE_ANALYTIC_OUTPUT_TRAVEL_TIME_SECONDS;
	spec.input = SG_RUNE_ANALYTIC_INPUT_DISTANCE;
	if (!ScalarFromFloat(0.0f, &spec.first) ||
		!ScalarFromFloat(inverse_speed, &spec.second))
		return SG_RUNE_COMPACT_WEAPON_BUILD_UNSUPPORTED;
	return AppendFunctionRef(field, specs, spec_capacity, spec_count,
		reference_cursor, channel, instance, &spec);
}

static sg_rune_compact_weapon_build_result_t AppendBallisticZRef(
	sg_rune_compact_weapon_field_t *field,
	sg_rune_compact_weapon_function_spec_t *specs, uint32_t spec_capacity,
	uint32_t *spec_count, uint32_t *reference_cursor,
	sg_rune_weapon_effect_channel_t channel, uint32_t instance,
	float launch_vertical_speed, float gravity)
{
	sg_rune_compact_weapon_function_spec_t spec;
	const float half_acceleration = -0.5f * gravity;

	if (!isfinite(launch_vertical_speed) || !isfinite(gravity) ||
		!isfinite(half_acceleration))
		return SG_RUNE_COMPACT_WEAPON_BUILD_UNSUPPORTED;
	memset(&spec, 0, sizeof(spec));
	spec.form = SG_RUNE_COMPACT_ANALYTIC_BALLISTIC;
	spec.output = SG_RUNE_ANALYTIC_OUTPUT_POSITION_Z;
	spec.input = SG_RUNE_ANALYTIC_INPUT_TIME_SECONDS;
	if (!ScalarFromFloat(0.0f, &spec.first) ||
		!ScalarFromFloat(launch_vertical_speed, &spec.second) ||
		!ScalarFromFloat(half_acceleration, &spec.third))
		return SG_RUNE_COMPACT_WEAPON_BUILD_UNSUPPORTED;
	return AppendFunctionRef(field, specs, spec_capacity, spec_count,
		reference_cursor, channel, instance, &spec);
}

static sg_rune_compact_weapon_build_result_t AppendFuseRemainingRef(
	sg_rune_compact_weapon_field_t *field,
	sg_rune_compact_weapon_function_spec_t *specs, uint32_t spec_capacity,
	uint32_t *spec_count, uint32_t *reference_cursor,
	sg_rune_weapon_effect_channel_t channel, uint32_t instance,
	uint32_t fuse_ms)
{
	sg_rune_compact_weapon_function_spec_t spec;
	const float fuse_seconds = (float)fuse_ms * 0.001f;

	memset(&spec, 0, sizeof(spec));
	spec.form = SG_RUNE_COMPACT_ANALYTIC_AFFINE;
	spec.output = SG_RUNE_ANALYTIC_OUTPUT_FUSE_REMAINING_SECONDS;
	spec.input = SG_RUNE_ANALYTIC_INPUT_TIME_SECONDS;
	if (!ScalarFromFloat(fuse_seconds, &spec.first) ||
		!ScalarFromFloat(-1.0f, &spec.second))
		return SG_RUNE_COMPACT_WEAPON_BUILD_UNSUPPORTED;
	return AppendFunctionRef(field, specs, spec_capacity, spec_count,
		reference_cursor, channel, instance, &spec);
}

static int StaticLawValue(const sg_weapon_profile_t *profile,
	sg_rune_weapon_static_law_slot_t slot, float *value_out)
{
	if (profile == NULL || value_out == NULL)
		return 0;
	switch (slot) {
	case SG_RUNE_WEAPON_STATIC_LAW_DIRECT_DAMAGE:
		*value_out = profile->direct_damage; break;
	case SG_RUNE_WEAPON_STATIC_LAW_DIRECT_DAMAGE_MAX:
		*value_out = profile->direct_damage_max; break;
	case SG_RUNE_WEAPON_STATIC_LAW_RAY_DISTANCE:
		*value_out = profile->ray_distance; break;
	case SG_RUNE_WEAPON_STATIC_LAW_PROJECTILE_SPEED:
		*value_out = profile->projectile_speed; break;
	case SG_RUNE_WEAPON_STATIC_LAW_PROJECTILE_SPEED_MAX:
		*value_out = profile->projectile_speed_max; break;
	case SG_RUNE_WEAPON_STATIC_LAW_PROJECTILE_RETIRE_DISTANCE:
		*value_out = profile->projectile_retire_distance; break;
	case SG_RUNE_WEAPON_STATIC_LAW_PROJECTILE_HALF_EXTENT:
		*value_out = profile->projectile_half_extent; break;
	case SG_RUNE_WEAPON_STATIC_LAW_PROJECTILE_LIFETIME_MS:
		*value_out = (float)profile->projectile_lifetime_ms; break;
	case SG_RUNE_WEAPON_STATIC_LAW_PROJECTILE_COUNT_MIN:
		*value_out = (float)profile->projectile_count_min; break;
	case SG_RUNE_WEAPON_STATIC_LAW_PROJECTILE_COUNT_MAX:
		*value_out = (float)profile->projectile_count_max; break;
	case SG_RUNE_WEAPON_STATIC_LAW_LAUNCH_VERTICAL_SPEED:
		*value_out = profile->launch_vertical_speed; break;
	case SG_RUNE_WEAPON_STATIC_LAW_LAUNCH_JITTER:
		*value_out = profile->launch_jitter; break;
	case SG_RUNE_WEAPON_STATIC_LAW_GRAVITY_SCALE:
		*value_out = profile->gravity_scale; break;
	case SG_RUNE_WEAPON_STATIC_LAW_FUSE_MS:
		*value_out = (float)profile->fuse_ms; break;
	case SG_RUNE_WEAPON_STATIC_LAW_COOK_MS:
		*value_out = (float)profile->cook_ms; break;
	case SG_RUNE_WEAPON_STATIC_LAW_HORIZONTAL_SPREAD:
		*value_out = profile->horizontal_spread; break;
	case SG_RUNE_WEAPON_STATIC_LAW_VERTICAL_SPREAD:
		*value_out = profile->vertical_spread; break;
	case SG_RUNE_WEAPON_STATIC_LAW_YAW_SPREAD_DEGREES:
		*value_out = profile->yaw_spread_degrees; break;
	case SG_RUNE_WEAPON_STATIC_LAW_AUXILIARY_TRACE_DAMAGE:
		*value_out = profile->auxiliary_trace_damage; break;
	case SG_RUNE_WEAPON_STATIC_LAW_AUXILIARY_HORIZONTAL_SPREAD:
		*value_out = profile->auxiliary_horizontal_spread; break;
	case SG_RUNE_WEAPON_STATIC_LAW_AUXILIARY_VERTICAL_SPREAD:
		*value_out = profile->auxiliary_vertical_spread; break;
	case SG_RUNE_WEAPON_STATIC_LAW_SPLASH_RADIUS:
		*value_out = profile->splash_radius; break;
	case SG_RUNE_WEAPON_STATIC_LAW_SPLASH_RADIUS_MAX:
		*value_out = profile->splash_radius_max; break;
	case SG_RUNE_WEAPON_STATIC_LAW_SPLASH_DAMAGE:
		*value_out = profile->splash_damage; break;
	case SG_RUNE_WEAPON_STATIC_LAW_SPLASH_DAMAGE_MAX:
		*value_out = profile->splash_damage_max; break;
	case SG_RUNE_WEAPON_STATIC_LAW_SELF_DAMAGE_SCALE:
		*value_out = profile->self_damage_scale; break;
	case SG_RUNE_WEAPON_STATIC_LAW_TEAMMATE_RISK_SCALE:
		*value_out = profile->teammate_risk_scale; break;
	case SG_RUNE_WEAPON_STATIC_LAW_SPLASH_KERNEL:
		*value_out = (float)profile->splash.kernel; break;
	case SG_RUNE_WEAPON_STATIC_LAW_SPLASH_OWNER:
		*value_out = (float)profile->splash.owner; break;
	case SG_RUNE_WEAPON_STATIC_LAW_SPLASH_OWNER_SCALE:
		*value_out = profile->splash.owner_scale; break;
	case SG_RUNE_WEAPON_STATIC_LAW_SECONDARY_SPLASH_RADIUS:
		*value_out = profile->secondary_splash_radius; break;
	case SG_RUNE_WEAPON_STATIC_LAW_SECONDARY_SPLASH_DAMAGE:
		*value_out = profile->secondary_splash_damage; break;
	case SG_RUNE_WEAPON_STATIC_LAW_SECONDARY_SPLASH_KERNEL:
		*value_out = (float)profile->secondary_splash.kernel; break;
	case SG_RUNE_WEAPON_STATIC_LAW_SECONDARY_SPLASH_OWNER:
		*value_out = (float)profile->secondary_splash.owner; break;
	case SG_RUNE_WEAPON_STATIC_LAW_SECONDARY_SPLASH_OWNER_SCALE:
		*value_out = profile->secondary_splash.owner_scale; break;
	case SG_RUNE_WEAPON_STATIC_LAW_PERIODIC_RAY_DAMAGE:
		*value_out = profile->periodic_ray_damage; break;
	case SG_RUNE_WEAPON_STATIC_LAW_PERIODIC_RAY_RADIUS:
		*value_out = profile->periodic_ray_radius; break;
	case SG_RUNE_WEAPON_STATIC_LAW_PERIODIC_RAY_DISTANCE:
		*value_out = profile->periodic_ray_distance; break;
	case SG_RUNE_WEAPON_STATIC_LAW_PERIODIC_RAY_INTERVAL_MS:
		*value_out = (float)profile->periodic_ray_interval_ms; break;
	case SG_RUNE_WEAPON_STATIC_LAW_WINDUP_MS:
		*value_out = (float)profile->windup_ms; break;
	case SG_RUNE_WEAPON_STATIC_LAW_CADENCE_MS:
		*value_out = (float)profile->cadence_ms; break;
	case SG_RUNE_WEAPON_STATIC_LAW_CADENCE_KIND:
		*value_out = (float)profile->cadence_kind; break;
	case SG_RUNE_WEAPON_STATIC_LAW_AMMO_READY_MINIMUM:
		*value_out = (float)profile->ammo.ready_minimum; break;
	case SG_RUNE_WEAPON_STATIC_LAW_AMMO_LIVE_FIRE_MINIMUM:
		*value_out = (float)profile->ammo.live_fire_minimum; break;
	case SG_RUNE_WEAPON_STATIC_LAW_AMMO_DEBIT:
		*value_out = (float)profile->ammo.debit; break;
	case SG_RUNE_WEAPON_STATIC_LAW_AMMO_DEBIT_MAXIMUM:
		*value_out = (float)profile->ammo.debit_maximum; break;
	case SG_RUNE_WEAPON_STATIC_LAW_AMMO_INFINITE_DEBIT:
		*value_out = (float)profile->ammo.infinite_ammo_debit; break;
	case SG_RUNE_WEAPON_STATIC_LAW_HOOK_INITIAL_DAMAGE:
		*value_out = (float)profile->hook_initial_damage; break;
	case SG_RUNE_WEAPON_STATIC_LAW_HOOK_ATTACHED_DAMAGE:
		*value_out = (float)profile->hook_attached_damage; break;
	case SG_RUNE_WEAPON_STATIC_LAW_HOOK_PULL_SPEED:
		*value_out = (float)profile->hook_pull_speed; break;
	case SG_RUNE_WEAPON_STATIC_LAW_HOOK_HEALTH:
		*value_out = (float)profile->hook_health; break;
	case SG_RUNE_WEAPON_STATIC_LAW_COUNT:
		return 0;
	}
	return isfinite(*value_out);
}

static sg_rune_compact_weapon_build_result_t AppendDirectResponse(
	sg_rune_compact_weapon_field_t *field,
	sg_rune_compact_weapon_function_spec_t *specs, uint32_t spec_capacity,
	uint32_t *spec_count, uint32_t *reference_cursor,
	sg_rune_weapon_effect_channel_t channel, uint32_t instance,
	const sg_weapon_profile_t *profile, int projectile)
{
	sg_rune_compact_weapon_build_result_t result;

	if (projectile != 0)
		result = AppendDistanceTimeRef(field, specs, spec_capacity,
			spec_count, reference_cursor, channel, instance,
			profile->projectile_speed);
	else
		result = AppendConstantRef(field, specs, spec_capacity, spec_count,
			reference_cursor, channel, instance,
			SG_RUNE_ANALYTIC_OUTPUT_TRAVEL_TIME_SECONDS, 0.0f);
	if (result != SG_RUNE_COMPACT_WEAPON_BUILD_OK)
		return result;
	if (FloatBits(profile->direct_damage) != FloatBits(profile->direct_damage_max))
		return SG_RUNE_COMPACT_WEAPON_BUILD_OK;
	return AppendConstantRef(field, specs, spec_capacity, spec_count,
		reference_cursor, channel, instance, SG_RUNE_ANALYTIC_OUTPUT_DAMAGE,
		profile->direct_damage);
}

static sg_rune_compact_weapon_build_result_t AppendEffectRadiusRef(
	sg_rune_compact_weapon_field_t *field,
	sg_rune_compact_weapon_function_spec_t *specs, uint32_t spec_capacity,
	uint32_t *spec_count, uint32_t *reference_cursor,
	sg_rune_weapon_effect_channel_t channel, uint32_t instance, float radius)
{
	if (!isfinite(radius) || radius < 0.0f)
		return SG_RUNE_COMPACT_WEAPON_BUILD_UNSUPPORTED;
	return AppendConstantRef(field, specs, spec_capacity, spec_count,
		reference_cursor, channel, instance,
		SG_RUNE_ANALYTIC_OUTPUT_EFFECT_RADIUS, radius);
}

static sg_rune_compact_weapon_build_result_t EmitKernel(
	sg_rune_compact_weapon_field_t *field,
	sg_rune_compact_weapon_function_spec_t *specs, uint32_t spec_capacity,
	uint32_t *spec_count, uint32_t *reference_cursor,
	const sg_weapon_profile_t *profile,
	sg_rune_weapon_response_family_t family)
{
	sg_rune_compact_weapon_build_result_t result;
	const float gravity = FloatFromBits(field->identity.physics.gravity_bits);
	uint32_t instance;
	uint32_t slot;

#define APPEND_DIRECT(channel, index, projectile) do { \
	result = AppendDirectResponse(field, specs, spec_capacity, spec_count, \
		reference_cursor, (channel), (index), profile, (projectile)); \
	if (result != SG_RUNE_COMPACT_WEAPON_BUILD_OK) return result; \
} while (0)
	switch (family) {
	case SG_RUNE_WEAPON_RESPONSE_HITSCAN:
		APPEND_DIRECT(SG_RUNE_WEAPON_EFFECT_CHANNEL_PRIMARY, 0U, 0);
		break;
	case SG_RUNE_WEAPON_RESPONSE_RAIL:
		APPEND_DIRECT(SG_RUNE_WEAPON_EFFECT_CHANNEL_PRIMARY, 0U, 0);
		for (instance = 0U; instance < profile->auxiliary_trace_count;
			instance++) {
			result = AppendConstantRef(field, specs, spec_capacity, spec_count,
				reference_cursor,
				SG_RUNE_WEAPON_EFFECT_CHANNEL_AUXILIARY_TRACE, instance,
				SG_RUNE_ANALYTIC_OUTPUT_DAMAGE,
				profile->auxiliary_trace_damage);
			if (result != SG_RUNE_COMPACT_WEAPON_BUILD_OK)
				return result;
		}
		break;
	case SG_RUNE_WEAPON_RESPONSE_AUTOMATIC_SPREAD:
		APPEND_DIRECT(SG_RUNE_WEAPON_EFFECT_CHANNEL_PRIMARY, 0U, 0);
		break;
	case SG_RUNE_WEAPON_RESPONSE_SHOTGUN_CONE:
		for (instance = 0U; instance < profile->projectile_count_max; instance++)
			APPEND_DIRECT(SG_RUNE_WEAPON_EFFECT_CHANNEL_PRIMARY, instance, 0);
		break;
	case SG_RUNE_WEAPON_RESPONSE_STRAIGHT_BOLT:
		for (instance = 0U; instance < profile->projectile_count_max;
			instance++)
			APPEND_DIRECT(SG_RUNE_WEAPON_EFFECT_CHANNEL_PRIMARY, instance, 1);
		break;
	case SG_RUNE_WEAPON_RESPONSE_ROCKET_IMPACT:
		APPEND_DIRECT(SG_RUNE_WEAPON_EFFECT_CHANNEL_PRIMARY, 0U, 1);
		break;
	case SG_RUNE_WEAPON_RESPONSE_ROCKET_SPLASH:
		result = AppendEffectRadiusRef(field, specs, spec_capacity, spec_count,
			reference_cursor, SG_RUNE_WEAPON_EFFECT_CHANNEL_SPLASH, 0U,
			profile->splash_radius_max);
		if (result != SG_RUNE_COMPACT_WEAPON_BUILD_OK)
			return result;
		break;
	case SG_RUNE_WEAPON_RESPONSE_GRENADE_FLIGHT:
		result = AppendBallisticZRef(field, specs, spec_capacity, spec_count,
			reference_cursor, SG_RUNE_WEAPON_EFFECT_CHANNEL_PRIMARY, 0U,
			profile->launch_vertical_speed,
			gravity * profile->gravity_scale);
		if (result != SG_RUNE_COMPACT_WEAPON_BUILD_OK)
			return result;
		result = AppendFuseRemainingRef(field, specs, spec_capacity,
			spec_count, reference_cursor,
			SG_RUNE_WEAPON_EFFECT_CHANNEL_PRIMARY, 0U, profile->fuse_ms);
		if (result != SG_RUNE_COMPACT_WEAPON_BUILD_OK)
			return result;
		break;
	case SG_RUNE_WEAPON_RESPONSE_GRENADE_BOUNCE_FUSE:
		result = AppendEffectRadiusRef(field, specs, spec_capacity, spec_count,
			reference_cursor, SG_RUNE_WEAPON_EFFECT_CHANNEL_SPLASH, 0U,
			profile->splash_radius_max);
		if (result != SG_RUNE_COMPACT_WEAPON_BUILD_OK)
			return result;
		result = AppendFuseRemainingRef(field, specs, spec_capacity,
			spec_count, reference_cursor,
			SG_RUNE_WEAPON_EFFECT_CHANNEL_PRIMARY, 0U, profile->fuse_ms);
		if (result != SG_RUNE_COMPACT_WEAPON_BUILD_OK)
			return result;
		break;
	case SG_RUNE_WEAPON_RESPONSE_HYPERBLASTER:
		APPEND_DIRECT(SG_RUNE_WEAPON_EFFECT_CHANNEL_PRIMARY, 0U, 1);
		break;
	case SG_RUNE_WEAPON_RESPONSE_BFG:
		APPEND_DIRECT(SG_RUNE_WEAPON_EFFECT_CHANNEL_PRIMARY, 0U, 1);
		result = AppendEffectRadiusRef(field, specs, spec_capacity, spec_count,
			reference_cursor, SG_RUNE_WEAPON_EFFECT_CHANNEL_SPLASH, 0U,
			profile->splash_radius_max);
		if (result != SG_RUNE_COMPACT_WEAPON_BUILD_OK)
			return result;
		result = AppendEffectRadiusRef(field, specs, spec_capacity, spec_count,
			reference_cursor, SG_RUNE_WEAPON_EFFECT_CHANNEL_SECONDARY_SPLASH,
			0U, profile->secondary_splash_radius);
		if (result != SG_RUNE_COMPACT_WEAPON_BUILD_OK)
			return result;
		result = AppendEffectRadiusRef(field, specs, spec_capacity, spec_count,
			reference_cursor, SG_RUNE_WEAPON_EFFECT_CHANNEL_PERIODIC_RAY, 0U,
			profile->periodic_ray_radius);
		if (result != SG_RUNE_COMPACT_WEAPON_BUILD_OK)
			return result;
		break;
	case SG_RUNE_WEAPON_RESPONSE_SPECIAL:
		if (profile->id == SG_WEAPON_PROFILE_HOOK) {
			APPEND_DIRECT(SG_RUNE_WEAPON_EFFECT_CHANNEL_PRIMARY, 0U, 1);
			result = AppendConstantRef(field, specs, spec_capacity, spec_count,
				reference_cursor,
				SG_RUNE_WEAPON_EFFECT_CHANNEL_ATTACHED_EFFECT, 0U,
				SG_RUNE_ANALYTIC_OUTPUT_DAMAGE,
				(float)profile->hook_attached_damage);
		} else if (profile->id == SG_WEAPON_PROFILE_BFG) {
			result = AppendConstantRef(field, specs, spec_capacity, spec_count,
				reference_cursor,
				SG_RUNE_WEAPON_EFFECT_CHANNEL_ATTACHED_EFFECT, 0U,
				SG_RUNE_ANALYTIC_OUTPUT_TRAVEL_TIME_SECONDS,
				(float)profile->windup_ms * 0.001f);
		} else {
			result = AppendEffectRadiusRef(field, specs, spec_capacity,
				spec_count, reference_cursor,
				SG_RUNE_WEAPON_EFFECT_CHANNEL_ATTACHED_EFFECT, 0U,
				profile->splash_radius_max);
		}
		if (result != SG_RUNE_COMPACT_WEAPON_BUILD_OK)
			return result;
		break;
	case SG_RUNE_WEAPON_RESPONSE_FAMILY_COUNT:
		return SG_RUNE_COMPACT_WEAPON_BUILD_UNSUPPORTED;
	}
#undef APPEND_DIRECT
	for (slot = 0U; slot < (uint32_t)SG_RUNE_WEAPON_STATIC_LAW_COUNT;
		slot++) {
		float value;

		if (!SG_RuneCompactWeaponStaticLawSlotRequired(
			(uint32_t)profile->id, family,
			(sg_rune_weapon_static_law_slot_t)slot))
			continue;
		if (!StaticLawValue(profile,
			(sg_rune_weapon_static_law_slot_t)slot, &value))
			return SG_RUNE_COMPACT_WEAPON_BUILD_UNSUPPORTED;
		result = AppendConstantRef(field, specs, spec_capacity, spec_count,
			reference_cursor, SG_RUNE_WEAPON_EFFECT_CHANNEL_STATIC_LAW,
			slot, SG_RUNE_ANALYTIC_OUTPUT_STATIC_WEAPON_LAW_VALUE, value);
		if (result != SG_RUNE_COMPACT_WEAPON_BUILD_OK)
			return result;
	}
	return SG_RUNE_COMPACT_WEAPON_BUILD_OK;
}

static int AllocateFieldAnalytic(sg_rune_compact_weapon_field_t *field,
	sg_rune_compact_weapon_function_spec_t *specs, uint32_t spec_count,
	sg_rune_compact_weapon_field_status_t *status_out)
{
	uint32_t *candidate_functions = NULL;
	uint32_t function_count = 0U;
	uint32_t constant_count = 0U;
	uint32_t affine_count = 0U;
	uint32_t ballistic_count = 0U;
	uint32_t input_count = 0U;
	uint32_t index;
	uint32_t function_index = 0U;
	uint32_t constant_cursor = 0U;
	uint32_t affine_cursor = 0U;
	uint32_t ballistic_cursor = 0U;
	uint32_t input_cursor = 0U;

	qsort(specs, spec_count, sizeof(*specs), FunctionSpecCompare);
	candidate_functions = calloc(spec_count, sizeof(*candidate_functions));
	if (candidate_functions == NULL) {
		*status_out = SG_RUNE_COMPACT_WEAPON_FIELD_ALLOCATION_FAILED;
		return 0;
	}
	for (index = 0U; index < spec_count; index++) {
		if (index == 0U || !FunctionSpecEqual(&specs[index - 1U],
			&specs[index])) {
			if (!CheckedAdd(function_count, 1U, &function_count)) {
				free(candidate_functions);
				*status_out = SG_RUNE_COMPACT_WEAPON_FIELD_LIMIT_EXCEEDED;
				return 0;
			}
			switch (specs[index].form) {
			case SG_RUNE_COMPACT_ANALYTIC_CONSTANT:
				constant_count++;
				break;
			case SG_RUNE_COMPACT_ANALYTIC_AFFINE:
				affine_count++;
				input_count++;
				break;
			case SG_RUNE_COMPACT_ANALYTIC_BALLISTIC:
				ballistic_count++;
				input_count++;
				break;
			case SG_RUNE_COMPACT_ANALYTIC_POLYNOMIAL:
			case SG_RUNE_COMPACT_ANALYTIC_PIECEWISE:
			case SG_RUNE_COMPACT_ANALYTIC_FORM_COUNT:
				free(candidate_functions);
				*status_out = SG_RUNE_COMPACT_WEAPON_FIELD_UNSUPPORTED_SCHEMA;
				return 0;
			}
		}
		candidate_functions[specs[index].original] = function_count - 1U;
	}
	if (function_count == 0U ||
		function_count > SG_RUNE_ANALYTIC_MAX_FUNCTIONS ||
		input_count > SG_RUNE_ANALYTIC_MAX_INPUT_DIMENSIONS) {
		free(candidate_functions);
		*status_out = SG_RUNE_COMPACT_WEAPON_FIELD_LIMIT_EXCEEDED;
		return 0;
	}
	field->functions = calloc(function_count, sizeof(*field->functions));
	field->constants = constant_count == 0U ? NULL :
		calloc(constant_count, sizeof(*field->constants));
	field->affines = affine_count == 0U ? NULL :
		calloc(affine_count, sizeof(*field->affines));
	field->affine_slopes = affine_count == 0U ? NULL :
		calloc(affine_count, sizeof(*field->affine_slopes));
	field->ballistics = ballistic_count == 0U ? NULL :
		calloc(ballistic_count, sizeof(*field->ballistics));
	field->input_dimensions = input_count == 0U ? NULL :
		calloc(input_count, sizeof(*field->input_dimensions));
	if (field->functions == NULL ||
		(constant_count != 0U && field->constants == NULL) ||
		(affine_count != 0U && field->affines == NULL) ||
		(affine_count != 0U && field->affine_slopes == NULL) ||
		(ballistic_count != 0U && field->ballistics == NULL) ||
		(input_count != 0U && field->input_dimensions == NULL)) {
		free(candidate_functions);
		*status_out = SG_RUNE_COMPACT_WEAPON_FIELD_ALLOCATION_FAILED;
		return 0;
	}
	for (index = 0U; index < spec_count; index++) {
		const sg_rune_compact_weapon_function_spec_t *spec = &specs[index];
		sg_rune_analytic_function_t *function;

		if (index != 0U && FunctionSpecEqual(&specs[index - 1U], spec))
			continue;
		function = &field->functions[function_index++];
		function->output = spec->output;
		function->form = spec->form;
		switch (spec->form) {
		case SG_RUNE_COMPACT_ANALYTIC_CONSTANT:
			function->definition = constant_cursor;
			field->constants[constant_cursor++].value = spec->first;
			break;
		case SG_RUNE_COMPACT_ANALYTIC_AFFINE:
			function->inputs.first = input_cursor;
			function->inputs.count = 1U;
			field->input_dimensions[input_cursor++] = spec->input;
			function->definition = affine_cursor;
			field->affines[affine_cursor].bias = spec->first;
			field->affines[affine_cursor].slopes.first = affine_cursor;
			field->affines[affine_cursor].slopes.count = 1U;
			field->affine_slopes[affine_cursor++] = spec->second;
			break;
		case SG_RUNE_COMPACT_ANALYTIC_BALLISTIC:
			function->inputs.first = input_cursor;
			function->inputs.count = 1U;
			field->input_dimensions[input_cursor++] = spec->input;
			function->definition = ballistic_cursor;
			field->ballistics[ballistic_cursor].initial = spec->first;
			field->ballistics[ballistic_cursor].first_derivative = spec->second;
			field->ballistics[ballistic_cursor++].half_second_derivative =
				spec->third;
			break;
		case SG_RUNE_COMPACT_ANALYTIC_POLYNOMIAL:
		case SG_RUNE_COMPACT_ANALYTIC_PIECEWISE:
		case SG_RUNE_COMPACT_ANALYTIC_FORM_COUNT:
			free(candidate_functions);
			*status_out = SG_RUNE_COMPACT_WEAPON_FIELD_UNSUPPORTED_SCHEMA;
			return 0;
		}
	}
	for (index = 0U; index < field->weapon_function_ref_count; index++)
		field->weapon_function_refs[index].function.value =
			candidate_functions[field->weapon_function_refs[index].function.value];
	free(candidate_functions);
	field->analytic.version = SG_RUNE_COMPACT_ANALYTIC_VERSION;
	field->analytic.functions = field->functions;
	field->analytic.function_count = function_count;
	field->analytic.input_dimensions = field->input_dimensions;
	field->analytic.input_dimension_count = input_count;
	field->analytic.constants = field->constants;
	field->analytic.constant_count = constant_count;
	field->analytic.affines = field->affines;
	field->analytic.affine_count = affine_count;
	field->analytic.affine_slopes = field->affine_slopes;
	field->analytic.affine_slope_count = affine_count;
	field->analytic.ballistics = field->ballistics;
	field->analytic.ballistic_count = ballistic_count;
	return 1;
}

static int FactGroupRecordCompare(const void *left_pointer,
	const void *right_pointer)
{
	const sg_rune_compact_weapon_fact_group_record_t *left = left_pointer;
	const sg_rune_compact_weapon_fact_group_record_t *right = right_pointer;
	int comparison = CompareU32(left->cell.value, right->cell.value);

	if (comparison == 0)
		comparison = CompareU32(left->source_surface, right->source_surface);
	if (comparison == 0)
		comparison = CompareU32((uint32_t)left->relation_class,
			(uint32_t)right->relation_class);
	if (comparison == 0)
		comparison = CompareU32(left->fact_index, right->fact_index);
	return comparison;
}

static int FactGroupKeyEqual(
	const sg_rune_compact_weapon_fact_group_record_t *left,
	const sg_rune_compact_weapon_fact_group_record_t *right)
{
	return left->cell.value == right->cell.value &&
		left->source_surface == right->source_surface &&
		left->relation_class == right->relation_class;
}

static uint32_t FactRelationClassCount(
	sg_rune_compact_static_relation_flags_t flags)
{
	if ((flags & SG_RUNE_COMPACT_STATIC_RELATION_DIRECT) != 0U)
		return 3U;
	return (uint32_t)((flags &
		SG_RUNE_COMPACT_STATIC_RELATION_PENETRATING) != 0U) +
		(uint32_t)((flags &
		SG_RUNE_COMPACT_STATIC_RELATION_OCCLUDED_IMPACT) != 0U);
}

static int FactRelationClassAt(sg_rune_compact_static_relation_flags_t flags,
	uint32_t ordinal, sg_rune_compact_weapon_relation_class_t *class_out)
{
	if (class_out == NULL)
		return 0;
	if ((flags & SG_RUNE_COMPACT_STATIC_RELATION_DIRECT) != 0U) {
		if (ordinal >= 3U)
			return 0;
		*class_out = (sg_rune_compact_weapon_relation_class_t)ordinal;
		return 1;
	}
	if ((flags & SG_RUNE_COMPACT_STATIC_RELATION_PENETRATING) != 0U) {
		if (ordinal == 0U) {
			*class_out = SG_RUNE_COMPACT_WEAPON_RELATION_RAIL;
			return 1;
		}
		ordinal--;
	}
	if ((flags & SG_RUNE_COMPACT_STATIC_RELATION_OCCLUDED_IMPACT) != 0U &&
		ordinal == 0U) {
		*class_out = SG_RUNE_COMPACT_WEAPON_RELATION_IMPACT;
		return 1;
	}
	return 0;
}

static void DestroyAttachmentPlan(sg_rune_compact_weapon_attachment_plan_t *plan)
{
	if (plan == NULL)
		return;
	free(plan->records);
	memset(plan, 0, sizeof(*plan));
}

/* Plan the bounded serialized representation before allocating any artifact
 * payload.  The temporary records are one per fact/class membership, never
 * one per weapon kernel, and their count is itself bounded by the final ref
 * limit. */
static sg_rune_compact_weapon_field_status_t BuildAttachmentPlan(
	const sg_rune_compact_weapon_relations_view_t *relations,
	sg_rune_compact_weapon_attachment_plan_t *plan)
{
	const sg_rune_compact_response_projection_t *response;
	sg_rune_compact_weapon_fact_group_record_t *records = NULL;
	uint32_t relation_count = 0U;
	uint32_t span_count = 0U;
	uint32_t fact_index;
	uint32_t cursor = 0U;

	if (relations == NULL || plan == NULL)
		return SG_RUNE_COMPACT_WEAPON_FIELD_INVALID_RELATIONS;
	memset(plan, 0, sizeof(*plan));
	response = &relations->response;
	for (fact_index = 0U; fact_index < response->fact_count; fact_index++) {
		const sg_rune_compact_response_fact_t *fact =
			&response->facts[fact_index];
		const uint32_t classes = FactRelationClassCount(fact->flags);

		if (fact->source_fragment >= response->source_fragment_count ||
			fact->target_patch >= response->target_patch_count || classes == 0U)
			return SG_RUNE_COMPACT_WEAPON_FIELD_INVALID_RELATIONS;
		if (!CheckedAdd(relation_count, classes, &relation_count))
			return SG_RUNE_COMPACT_WEAPON_FIELD_LIMIT_EXCEEDED;
	}
	if (relation_count == 0U)
		return SG_RUNE_COMPACT_WEAPON_FIELD_INVALID_RELATIONS;
	if (relation_count > WEAPON_RELATION_REFERENCE_LIMIT)
		return SG_RUNE_COMPACT_WEAPON_FIELD_LIMIT_EXCEEDED;
	/* Count distinct (cell, surface, class) keys without allocating the final
	 * field or the sortable work array.  This is intentionally quadratic in the
	 * bounded fact set: the model limit keeps it small and it makes all three
	 * final representation limits provable before allocation. */
	for (fact_index = 0U; fact_index < response->fact_count; fact_index++) {
		const sg_rune_compact_response_fact_t *fact =
			&response->facts[fact_index];
		const sg_rune_compact_response_fragment_t *fragment =
			&response->source_fragments[fact->source_fragment];
		const sg_rune_compact_response_patch_t *patch =
			&response->target_patches[fact->target_patch];
		const uint32_t class_count = FactRelationClassCount(fact->flags);
		uint32_t class_index;

		for (class_index = 0U; class_index < class_count; class_index++) {
			sg_rune_compact_weapon_relation_class_t relation_class;
			uint32_t earlier_fact;
			int seen = 0;

			if (!FactRelationClassAt(fact->flags, class_index,
				&relation_class))
				return SG_RUNE_COMPACT_WEAPON_FIELD_UNSUPPORTED_SCHEMA;
			for (earlier_fact = 0U; earlier_fact <= fact_index && !seen;
				earlier_fact++) {
				const sg_rune_compact_response_fact_t *earlier =
					&response->facts[earlier_fact];
				const uint32_t earlier_count =
					FactRelationClassCount(earlier->flags);
				uint32_t earlier_class_index;

				for (earlier_class_index = 0U;
					earlier_class_index < earlier_count; earlier_class_index++) {
					sg_rune_compact_weapon_relation_class_t earlier_class;

					if (earlier_fact == fact_index &&
						earlier_class_index >= class_index)
						break;
					if (!FactRelationClassAt(earlier->flags,
						earlier_class_index, &earlier_class))
						return SG_RUNE_COMPACT_WEAPON_FIELD_UNSUPPORTED_SCHEMA;
					if (earlier_class == relation_class &&
						response->source_fragments[
							earlier->source_fragment].parent_cell.value ==
							fragment->parent_cell.value &&
						response->target_patches[
							earlier->target_patch].source_surface ==
							patch->source_surface) {
						seen = 1;
						break;
					}
				}
			}
			if (!seen && !CheckedAdd(span_count, 1U, &span_count))
				return SG_RUNE_COMPACT_WEAPON_FIELD_LIMIT_EXCEEDED;
		}
	}
	if (span_count == 0U || span_count > WEAPON_RELATION_SPAN_LIMIT ||
		span_count > WEAPON_ATTACHMENT_LIMIT)
		return SG_RUNE_COMPACT_WEAPON_FIELD_LIMIT_EXCEEDED;
	records = calloc(relation_count, sizeof(*records));
	if (records == NULL)
		return SG_RUNE_COMPACT_WEAPON_FIELD_ALLOCATION_FAILED;
	for (fact_index = 0U; fact_index < response->fact_count; fact_index++) {
		const sg_rune_compact_response_fact_t *fact =
			&response->facts[fact_index];
		const sg_rune_compact_response_fragment_t *fragment =
			&response->source_fragments[fact->source_fragment];
		const sg_rune_compact_response_patch_t *patch =
			&response->target_patches[fact->target_patch];
		const sg_rune_compact_static_relation_flags_t flags = fact->flags;
		const uint32_t class_count = FactRelationClassCount(flags);
		uint32_t class_index;

		for (class_index = 0U; class_index < class_count; class_index++) {
			sg_rune_compact_weapon_fact_group_record_t *record;

			if (cursor >= relation_count) {
				free(records);
				return SG_RUNE_COMPACT_WEAPON_FIELD_UNSUPPORTED_SCHEMA;
			}
			record = &records[cursor++];
			record->cell = fragment->parent_cell;
			record->source_surface = patch->source_surface;
			if (!FactRelationClassAt(flags, class_index,
				&record->relation_class)) {
				free(records);
				return SG_RUNE_COMPACT_WEAPON_FIELD_UNSUPPORTED_SCHEMA;
			}
			record->fact_index = fact_index;
		}
	}
	if (cursor != relation_count) {
		free(records);
		return SG_RUNE_COMPACT_WEAPON_FIELD_UNSUPPORTED_SCHEMA;
	}
	qsort(records, relation_count, sizeof(*records), FactGroupRecordCompare);
	{
		uint32_t observed_span_count = 1U;

		for (cursor = 1U; cursor < relation_count; cursor++) {
			if (FactGroupRecordCompare(&records[cursor - 1U], &records[cursor]) ==
				0) {
				free(records);
				return SG_RUNE_COMPACT_WEAPON_FIELD_INVALID_RELATIONS;
			}
			if (!FactGroupKeyEqual(&records[cursor - 1U], &records[cursor]) &&
				!CheckedAdd(observed_span_count, 1U, &observed_span_count)) {
				free(records);
				return SG_RUNE_COMPACT_WEAPON_FIELD_LIMIT_EXCEEDED;
			}
		}
		if (observed_span_count != span_count) {
			free(records);
			return SG_RUNE_COMPACT_WEAPON_FIELD_UNSUPPORTED_SCHEMA;
		}
	}
	/* Each unique group becomes exactly one attachment.  The consumer derives
	 * a kernel's relation class during lookup, so this count never multiplies
	 * by profile-family kernels. */
	plan->records = records;
	plan->relation_count = relation_count;
	plan->span_count = span_count;
	return SG_RUNE_COMPACT_WEAPON_FIELD_OK;
}

static sg_rune_compact_weapon_field_status_t EmitAttachmentPlan(
	sg_rune_compact_weapon_field_t *field,
	const sg_rune_compact_weapon_attachment_plan_t *plan)
{
	uint32_t cursor = 0U;
	uint32_t span_cursor = 0U;
	uint32_t relation_cursor = 0U;

	if (field == NULL || plan == NULL || plan->records == NULL ||
		plan->relation_count == 0U || plan->span_count == 0U ||
		plan->relation_count > WEAPON_RELATION_REFERENCE_LIMIT ||
		plan->span_count > WEAPON_RELATION_SPAN_LIMIT ||
		plan->span_count > WEAPON_ATTACHMENT_LIMIT)
		return SG_RUNE_COMPACT_WEAPON_FIELD_INVALID_RELATIONS;
	field->attachments = calloc(plan->span_count, sizeof(*field->attachments));
	field->relation_spans = calloc(plan->span_count,
		sizeof(*field->relation_spans));
	field->relation_refs = calloc(plan->relation_count,
		sizeof(*field->relation_refs));
	if (field->attachments == NULL || field->relation_spans == NULL ||
		field->relation_refs == NULL) {
		return SG_RUNE_COMPACT_WEAPON_FIELD_ALLOCATION_FAILED;
	}
	while (cursor < plan->relation_count) {
		const uint32_t first = cursor;
		sg_rune_compact_weapon_relation_span_t *span;
		sg_rune_compact_weapon_field_attachment_t *attachment;
		uint32_t offset;

		while (cursor < plan->relation_count && FactGroupKeyEqual(
			&plan->records[first], &plan->records[cursor]))
			cursor++;
		span = &field->relation_spans[span_cursor];
		span->references.first = relation_cursor;
		span->references.count = cursor - first;
		for (offset = 0U; offset < span->references.count; offset++) {
			field->relation_refs[relation_cursor + offset].kind =
				SG_RUNE_COMPACT_RESPONSE_REF_CERTIFIED_FACT;
			field->relation_refs[relation_cursor + offset].index =
				plan->records[first + offset].fact_index;
		}
		relation_cursor += span->references.count;
		attachment = &field->attachments[span_cursor];
		attachment->cell = plan->records[first].cell;
		attachment->source_surface = plan->records[first].source_surface;
		attachment->relation_class = plan->records[first].relation_class;
		attachment->relations = span->references;
		attachment->relation_span = span_cursor;
		span_cursor++;
	}
	if (span_cursor != plan->span_count || relation_cursor != plan->relation_count)
		return SG_RUNE_COMPACT_WEAPON_FIELD_UNSUPPORTED_SCHEMA;
	field->attachment_count = plan->span_count;
	field->relation_span_count = plan->span_count;
	field->relation_ref_count = plan->relation_count;
	return SG_RUNE_COMPACT_WEAPON_FIELD_OK;
}

static int RelationsCurrent(const sg_rune_compact_weapon_field_t *field)
{
	sg_rune_compact_weapon_relations_view_t current;

	return field != NULL && field->relations_owner != NULL &&
		SG_RuneCompactWeaponRelationsRead(field->relations_owner, &current) &&
		RelationsValid(&current, &field->identity) &&
		current.owner == field->relations_owner &&
		current.version == field->relations.version &&
		current.reserved == field->relations.reserved &&
		SG_RuneCompactIdentityMatches(&current.identity, &field->identity) &&
		RelationProjectionEqual(&current.response, &field->relations.response);
}

sg_rune_compact_weapon_field_status_t SG_RuneCompactWeaponFieldBuild(
	const sg_rune_compact_weapon_field_input_t *input,
	sg_rune_compact_weapon_field_t **field_out,
	sg_rune_compact_weapon_field_error_t *error_out)
{
	sg_rune_compact_weapon_field_error_t validation_error = { 0 };
	sg_rune_compact_weapon_field_t *field = NULL;
	sg_rune_compact_weapon_function_spec_t *specs = NULL;
	sg_rune_compact_weapon_relations_view_t relations;
	sg_rune_compact_weapon_attachment_plan_t attachment_plan = { 0 };
	sg_rune_compact_weapon_field_status_t status;
	uint32_t kernel_count = 0U;
	uint32_t reference_count = 0U;
	uint32_t profile_index;
	uint32_t kernel_cursor = 0U;
	uint32_t reference_cursor = 0U;
	uint32_t spec_count = 0U;

	if (field_out == NULL) {
		SetError(error_out, SG_RUNE_COMPACT_WEAPON_FIELD_INVALID_ARGUMENT, 0U);
		return SG_RUNE_COMPACT_WEAPON_FIELD_INVALID_ARGUMENT;
	}
	*field_out = NULL;
	SetError(error_out, SG_RUNE_COMPACT_WEAPON_FIELD_OK, 0U);
	if (!InputValid(input, &relations, &validation_error)) {
		SetError(error_out, validation_error.status, validation_error.record);
		return validation_error.status;
	}
	/* Establish exact final response attachment/ref bounds before allocating
	 * kernels, functions, or field storage. */
	status = BuildAttachmentPlan(&relations, &attachment_plan);
	if (status != SG_RUNE_COMPACT_WEAPON_FIELD_OK) {
		SetError(error_out, status, 0U);
		return status;
	}
	for (profile_index = 0U; profile_index < input->profile_count;
		profile_index++) {
		const sg_rune_weapon_profile_t *compact_profile =
			&input->compact_profiles[profile_index];
		const sg_rune_weapon_response_family_mask_t families =
			input->compact_profiles[profile_index].response_families;
		uint32_t family;

		for (family = 0U;
			family < (uint32_t)SG_RUNE_WEAPON_RESPONSE_FAMILY_COUNT;
			family++) {
			uint32_t references;

			if ((families & SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(family)) == 0U)
				continue;
			if (!SG_RuneCompactWeaponKernelReferenceCount(compact_profile,
				(sg_rune_weapon_response_family_t)family, &references) ||
				!CheckedAdd(kernel_count, 1U, &kernel_count) ||
				!CheckedAdd(reference_count, references, &reference_count)) {
				DestroyAttachmentPlan(&attachment_plan);
				SetError(error_out,
					SG_RUNE_COMPACT_WEAPON_FIELD_LIMIT_EXCEEDED, profile_index);
				return SG_RUNE_COMPACT_WEAPON_FIELD_LIMIT_EXCEEDED;
			}
		}
	}
	if (kernel_count == 0U || reference_count == 0U ||
		kernel_count > SG_RUNE_COMPACT_MAX_WEAPON_KERNELS ||
		reference_count > SG_RUNE_COMPACT_MAX_WEAPON_FUNCTION_REFS) {
		DestroyAttachmentPlan(&attachment_plan);
		SetError(error_out, SG_RUNE_COMPACT_WEAPON_FIELD_LIMIT_EXCEEDED, 0U);
		return SG_RUNE_COMPACT_WEAPON_FIELD_LIMIT_EXCEEDED;
	}
	field = calloc(1U, sizeof(*field));
	if (field == NULL) {
		DestroyAttachmentPlan(&attachment_plan);
		SetError(error_out, SG_RUNE_COMPACT_WEAPON_FIELD_ALLOCATION_FAILED, 0U);
		return SG_RUNE_COMPACT_WEAPON_FIELD_ALLOCATION_FAILED;
	}
	field->identity = *input->identity;
	field->relations_owner = input->relations_owner;
	field->relations = relations;
	field->kernel_count = kernel_count;
	field->weapon_function_ref_count = reference_count;
	field->kernels = calloc(field->kernel_count, sizeof(*field->kernels));
	field->weapon_function_refs = calloc(field->weapon_function_ref_count,
		sizeof(*field->weapon_function_refs));
	specs = calloc(reference_count, sizeof(*specs));
	if (field->kernels == NULL || field->weapon_function_refs == NULL ||
		specs == NULL) {
		free(specs);
		DestroyAttachmentPlan(&attachment_plan);
		SG_RuneCompactWeaponFieldDestroy(field);
		SetError(error_out, SG_RUNE_COMPACT_WEAPON_FIELD_ALLOCATION_FAILED, 0U);
		return SG_RUNE_COMPACT_WEAPON_FIELD_ALLOCATION_FAILED;
	}
	for (profile_index = 0U; profile_index < input->profile_count;
		profile_index++) {
		const sg_weapon_profile_t *profile =
			&input->resolved_profiles[profile_index];
		const sg_rune_weapon_profile_t *compact_profile =
			&input->compact_profiles[profile_index];
		const sg_rune_weapon_response_family_mask_t families =
			compact_profile->response_families;
		uint32_t family;

		for (family = 0U;
			family < (uint32_t)SG_RUNE_WEAPON_RESPONSE_FAMILY_COUNT;
			family++) {
			sg_rune_weapon_response_kernel_t *kernel;
			uint32_t expected_references;
			sg_rune_compact_weapon_build_result_t emitted;

			if ((families & SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(family)) == 0U)
				continue;
			if (!SG_RuneCompactWeaponKernelReferenceCount(compact_profile,
				(sg_rune_weapon_response_family_t)family,
				&expected_references)) {
				status = SG_RUNE_COMPACT_WEAPON_FIELD_UNSUPPORTED_SCHEMA;
				goto fail;
			}
			kernel = &field->kernels[kernel_cursor++];
			kernel->profile = profile_index;
			kernel->family = (sg_rune_weapon_response_family_t)family;
			if (!EventLawFor(profile, kernel->family, &kernel->event_law)) {
				status = SG_RUNE_COMPACT_WEAPON_FIELD_UNSUPPORTED_SCHEMA;
				goto fail;
			}
			kernel->functions.first = reference_cursor;
			emitted = EmitKernel(field, specs, reference_count, &spec_count,
				&reference_cursor, profile,
				(sg_rune_weapon_response_family_t)family);
			if (emitted != SG_RUNE_COMPACT_WEAPON_BUILD_OK) {
				status = emitted == SG_RUNE_COMPACT_WEAPON_BUILD_LIMIT ?
					SG_RUNE_COMPACT_WEAPON_FIELD_LIMIT_EXCEEDED :
					SG_RUNE_COMPACT_WEAPON_FIELD_UNSUPPORTED_SCHEMA;
				goto fail;
			}
			if (reference_cursor - kernel->functions.first !=
				expected_references) {
				status = SG_RUNE_COMPACT_WEAPON_FIELD_UNSUPPORTED_SCHEMA;
				goto fail;
			}
			kernel->functions.count = expected_references;
		}
	}
	if (kernel_cursor != field->kernel_count ||
		reference_cursor != field->weapon_function_ref_count ||
		spec_count != reference_count) {
		status = SG_RUNE_COMPACT_WEAPON_FIELD_UNSUPPORTED_SCHEMA;
		goto fail;
	}
	if (!AllocateFieldAnalytic(field, specs, spec_count, &status))
		goto fail;
	status = EmitAttachmentPlan(field, &attachment_plan);
	if (status != SG_RUNE_COMPACT_WEAPON_FIELD_OK)
		goto fail;
	if (!RelationsCurrent(field)) {
		status = SG_RUNE_COMPACT_WEAPON_FIELD_INVALID_RELATIONS;
		goto fail;
	}
	free(specs);
	DestroyAttachmentPlan(&attachment_plan);
	SetError(error_out, SG_RUNE_COMPACT_WEAPON_FIELD_OK, 0U);
	*field_out = field;
	return SG_RUNE_COMPACT_WEAPON_FIELD_OK;

fail:
	free(specs);
	DestroyAttachmentPlan(&attachment_plan);
	SG_RuneCompactWeaponFieldDestroy(field);
	SetError(error_out, status, profile_index);
	return status;
}

void SG_RuneCompactWeaponFieldDestroy(sg_rune_compact_weapon_field_t *field)
{
	if (field == NULL)
		return;
	free(field->affine_slopes);
	free(field->affines);
	free(field->constants);
	free(field->input_dimensions);
	free(field->functions);
	free(field->ballistics);
	free(field->relation_refs);
	free(field->relation_spans);
	free(field->attachments);
	free(field->weapon_function_refs);
	free(field->kernels);
	free(field);
}

int SG_RuneCompactWeaponFieldReadBound(
	const sg_rune_compact_weapon_field_t *field,
	sg_rune_compact_weapon_field_view_t *view_out)
{
	if (field == NULL || view_out == NULL || !RelationsCurrent(field))
		return 0;
	memset(view_out, 0, sizeof(*view_out));
	view_out->identity = field->identity;
	view_out->kernels = field->kernels;
	view_out->kernel_count = field->kernel_count;
	view_out->attachments = field->attachments;
	view_out->attachment_count = field->attachment_count;
	view_out->relation_spans = field->relation_spans;
	view_out->relation_span_count = field->relation_span_count;
	view_out->relation_refs = field->relation_refs;
	view_out->relation_ref_count = field->relation_ref_count;
	view_out->response = &field->relations.response;
	view_out->weapon_function_refs = field->weapon_function_refs;
	view_out->weapon_function_ref_count = field->weapon_function_ref_count;
	view_out->functions = field->functions;
	view_out->input_dimensions = field->input_dimensions;
	view_out->constants = field->constants;
	view_out->affines = field->affines;
	view_out->affine_slopes = field->affine_slopes;
	view_out->ballistics = field->ballistics;
	view_out->analytic = field->analytic;
	return 1;
}

const char *SG_RuneCompactWeaponFieldStatusString(
	sg_rune_compact_weapon_field_status_t status)
{
	static const char *const reasons[] = {
		"ok",
		"invalid argument",
		"invalid host weapon law",
		"invalid sealed weapon profile",
		"invalid shared weapon relation projection",
		"weapon family law cannot be represented exactly",
		"representation limit exceeded",
		"out of memory"
	};

	if ((uint32_t)status >= (uint32_t)SG_RUNE_COMPACT_WEAPON_FIELD_STATUS_COUNT)
		return "unknown compact weapon field status";
	return reasons[status];
}
