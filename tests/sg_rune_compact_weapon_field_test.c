#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "slipgate/sg_rune_compact_weapon_field.h"
#include "slipgate/sg_rune_compact_weapon_catalog.h"
#include "slipgate/sg_rune_compact_weapon_relations.h"

static int failures;

#if defined(SG_RUNE_COMPACT_WEAPON_FIELD_TEST_WRAP_CALLOC)
static size_t calloc_fail_after = SIZE_MAX;
static size_t calloc_count;

void *__real_calloc(size_t count, size_t size);
void *__wrap_calloc(size_t count, size_t size);

void *__wrap_calloc(size_t count, size_t size)
{
	if (calloc_count++ == calloc_fail_after)
		return NULL;
	return __real_calloc(count, size);
}
#endif

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

enum { PROFILE_COUNT = SG_WEAPON_PROFILE_COUNT - 1 };

typedef struct weapon_fixture_s
{
	sg_rune_weapon_profile_t compact_profiles[PROFILE_COUNT];
	sg_weapon_profile_t resolved_profiles[PROFILE_COUNT];
	sg_rune_source_weapon_law_t weapon_law;
	sg_rune_compact_identity_t identity;
	sg_rune_compact_response_fragment_t fragments[3];
	sg_rune_compact_response_patch_t patches[2];
	sg_rune_compact_response_fact_t facts[3];
	sg_rune_compact_weapon_relations_view_t relations;
	int relations_available;
	sg_rune_compact_weapon_field_input_t input;
} weapon_fixture_t;

static weapon_fixture_t *active_fixture;

int SG_RuneCompactWeaponRelationsRead(
	const sg_rune_compact_weapon_relations_t *relations,
	sg_rune_compact_weapon_relations_view_t *view_out)
{
	if (active_fixture == NULL || view_out == NULL ||
		relations != (const sg_rune_compact_weapon_relations_t *)(uintptr_t)1U ||
		active_fixture->relations_available == 0)
		return 0;
	*view_out = active_fixture->relations;
	return 1;
}

static uint32_t Bits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static void ResolveProfiles(weapon_fixture_t *fixture)
{
	sg_weapon_law_input_t law = { 0 };
	uint32_t index;

	law.build_identity = UINT64_C(0x21);
	law.physics_abi_id = fixture->identity.physics_abi_id;
	law.weapon_balance_compiled = fixture->weapon_law.weapon_balance_compiled;
	law.weapon_balance_enabled = fixture->weapon_law.weapon_balance_enabled;
	law.rail_match_active = fixture->weapon_law.rail_match_active;
	law.deathmatch_active = fixture->weapon_law.deathmatch_active;
	law.fast_switch_enabled = fixture->weapon_law.fast_switch_enabled;
	for (index = 0U; index < PROFILE_COUNT; index++) {
		const sg_weapon_profile_id_t id =
			(sg_weapon_profile_id_t)(index + 1U);

		CHECK(SG_WeaponProfileResolve(id, &law,
			&fixture->resolved_profiles[index]));
		fixture->compact_profiles[index].source_profile = (uint32_t)id;
		fixture->compact_profiles[index].response_families =
			SG_RuneCompactWeaponCanonicalProfileMask((uint32_t)id);
		fixture->compact_profiles[index].projectile_count_min =
			fixture->resolved_profiles[index].projectile_count_min;
		fixture->compact_profiles[index].projectile_count_max =
			fixture->resolved_profiles[index].projectile_count_max;
		fixture->compact_profiles[index].auxiliary_trace_count =
			fixture->resolved_profiles[index].auxiliary_trace_count;
		fixture->compact_profiles[index].direct_response_count =
			Bits(fixture->resolved_profiles[index].direct_damage) ==
			Bits(fixture->resolved_profiles[index].direct_damage_max) ? 2U : 1U;
	}
	CHECK(SG_RuneCompactWeaponProfileCatalogId(fixture->compact_profiles,
		PROFILE_COUNT, &fixture->identity.weapon_profile_catalog_id));
	CHECK(SG_RuneCompactWeaponLawIdentity(&fixture->weapon_law,
		fixture->resolved_profiles, PROFILE_COUNT,
		&fixture->identity.weapon_law_id));
	fixture->input.weapon_law_id = fixture->identity.weapon_law_id;
}

static void InitFixture(weapon_fixture_t *fixture)
{
	memset(fixture, 0, sizeof(*fixture));
	fixture->identity.bsp_sha256[0] = 1U;
	fixture->identity.bsp_bytes = 1U;
	fixture->identity.entity_semantics_id = UINT64_C(0x11);
	fixture->identity.physics_abi_id = UINT64_C(0x12);
	fixture->identity.collision_law_id = UINT64_C(0x13);
	fixture->identity.pmove_law_id = UINT64_C(0x14);
	fixture->identity.gravity_law_id = UINT64_C(0x15);
	fixture->identity.hook_law_id = UINT64_C(0x16);
	fixture->identity.mechanism_law_id = UINT64_C(0x17);
	fixture->identity.weapon_law_id = UINT64_C(0x18);
	fixture->identity.construction_id = UINT64_C(0x19);
	fixture->identity.schema_id = UINT64_C(0x1a);
	fixture->identity.producer_identity = UINT64_C(0x1b);
	fixture->identity.source_counts.model_count = 1U;
	fixture->identity.source_counts.leaf_count = 1U;
	fixture->identity.source_counts.area_count = 1U;
	fixture->identity.source_counts.plane_count = 1U;
	fixture->identity.source_counts.brush_count = 1U;
	fixture->identity.source_counts.brush_side_count = 1U;
	fixture->identity.source_counts.entity_count = 1U;
	fixture->identity.physics.gravity_bits = Bits(100.0f);
	fixture->identity.physics.frame_ms = 100U;
	fixture->identity.physics.substep_ms = 10U;
	fixture->weapon_law.weapon_balance_compiled =
		(uint8_t)SG_WEAPON_BALANCE_COMPILED;
	fixture->weapon_law.deathmatch_active = 1U;
	fixture->fragments[0].parent_cell.value = 0U;
	fixture->fragments[1].parent_cell.value = 0U;
	fixture->fragments[2].parent_cell.value = 1U;
	fixture->patches[0].source_surface = 0U;
	fixture->patches[1].source_surface = 1U;
	fixture->facts[0].source_fragment = 0U;
	fixture->facts[0].target_patch = 0U;
	fixture->facts[0].flags = SG_RUNE_COMPACT_STATIC_RELATION_DIRECT;
	fixture->facts[1].source_fragment = 1U;
	fixture->facts[1].target_patch = 0U;
	fixture->facts[1].flags = SG_RUNE_COMPACT_STATIC_RELATION_PENETRATING;
	fixture->facts[2].source_fragment = 2U;
	fixture->facts[2].target_patch = 1U;
	fixture->facts[2].flags =
		SG_RUNE_COMPACT_STATIC_RELATION_OCCLUDED_IMPACT;
	fixture->relations.version = SG_RUNE_COMPACT_WEAPON_RELATIONS_VERSION;
	fixture->relations.owner =
		(const sg_rune_compact_weapon_relations_t *)(uintptr_t)1U;
	fixture->relations.response.source_fragments = fixture->fragments;
	fixture->relations.response.source_fragment_count = 3U;
	fixture->relations.response.target_patches = fixture->patches;
	fixture->relations.response.target_patch_count = 2U;
	fixture->relations.response.facts = fixture->facts;
	fixture->relations.response.fact_count = 3U;
	fixture->relations.response.seal.compact_cell_count = 2U;
	fixture->relations.response.seal.compact_source_surface_count = 2U;
	fixture->relations.response.exact_live_prefire_trace_required = 1U;
	fixture->relations_available = 1;
	ResolveProfiles(fixture);
	fixture->relations.identity = fixture->identity;
	fixture->input.identity = &fixture->identity;
	fixture->input.compact_profiles = fixture->compact_profiles;
	fixture->input.resolved_profiles = fixture->resolved_profiles;
	fixture->input.profile_count = PROFILE_COUNT;
	fixture->input.weapon_law = &fixture->weapon_law;
	fixture->input.physics_abi_id = fixture->identity.physics_abi_id;
	fixture->input.weapon_law_id = fixture->identity.weapon_law_id;
	fixture->input.relations_owner = fixture->relations.owner;
	active_fixture = fixture;
}

static const sg_rune_weapon_response_kernel_t *FindKernel(
	const sg_rune_compact_weapon_field_view_t *view,
	sg_weapon_profile_id_t profile,
	sg_rune_weapon_response_family_t family)
{
	uint32_t index;

	for (index = 0U; index < view->kernel_count; index++)
		if (view->kernels[index].profile == (uint32_t)profile - 1U &&
			view->kernels[index].family == family)
			return &view->kernels[index];
	return NULL;
}

static const sg_rune_analytic_function_t *FindKernelFunction(
	const sg_rune_compact_weapon_field_view_t *view,
	const sg_rune_weapon_response_kernel_t *kernel,
	sg_rune_weapon_effect_channel_t channel, uint32_t instance,
	sg_rune_analytic_output_meaning_t output)
{
	uint32_t index;

	if (view == NULL || kernel == NULL ||
		kernel->functions.first > view->weapon_function_ref_count ||
		kernel->functions.count > view->weapon_function_ref_count -
			kernel->functions.first)
		return NULL;
	for (index = 0U; index < kernel->functions.count; index++) {
		const sg_rune_weapon_function_ref_t *reference =
			&view->weapon_function_refs[kernel->functions.first + index];

		if (reference->channel == channel && reference->instance == instance &&
			reference->function.value < view->analytic.function_count &&
			view->analytic.functions[reference->function.value].output == output)
			return &view->analytic.functions[reference->function.value];
	}
	return NULL;
}

static int StaticLawBits(const sg_rune_compact_weapon_field_view_t *view,
	const sg_rune_weapon_response_kernel_t *kernel,
	sg_rune_weapon_static_law_slot_t slot, uint32_t *bits_out)
{
	const sg_rune_analytic_function_t *function = FindKernelFunction(view,
		kernel, SG_RUNE_WEAPON_EFFECT_CHANNEL_STATIC_LAW, (uint32_t)slot,
		SG_RUNE_ANALYTIC_OUTPUT_STATIC_WEAPON_LAW_VALUE);

	if (function == NULL || function->form != SG_RUNE_COMPACT_ANALYTIC_CONSTANT ||
		function->definition >= view->analytic.constant_count || bits_out == NULL)
		return 0;
	*bits_out = view->analytic.constants[function->definition].value.bits;
	return 1;
}

static const sg_rune_compact_weapon_field_attachment_t *FindAttachment(
	const sg_rune_compact_weapon_field_view_t *view, uint32_t cell,
	uint32_t source_surface, sg_weapon_profile_id_t profile,
	sg_rune_weapon_response_family_t family)
{
	sg_rune_compact_weapon_relation_class_t relation_class;
	uint32_t index;

	if (!SG_RuneCompactWeaponRelationClassForProfile((uint32_t)profile,
		family, &relation_class))
		return NULL;
	for (index = 0U; index < view->attachment_count; index++) {
		const sg_rune_compact_weapon_field_attachment_t *attachment =
			&view->attachments[index];

		if (attachment->cell.value == cell &&
			attachment->source_surface == source_surface &&
			attachment->relation_class == relation_class)
			return attachment;
	}
	return NULL;
}

static void CheckKernelLaw(const sg_rune_compact_weapon_field_view_t *view,
	sg_weapon_profile_id_t profile, sg_rune_weapon_response_family_t family,
	sg_rune_weapon_event_law_kind_t kind,
	sg_rune_weapon_runtime_requirement_mask_t requirements)
{
	const sg_rune_weapon_response_kernel_t *kernel = FindKernel(view, profile,
		family);
	sg_rune_weapon_event_law_t canonical;

	CHECK(kernel != NULL);
	if (kernel == NULL)
		return;
	CHECK(SG_RuneCompactWeaponCanonicalEventLaw((uint32_t)profile, family,
		&canonical));
	CHECK(kernel->event_law.kind == canonical.kind);
	CHECK(kernel->event_law.requirements == canonical.requirements);
	CHECK(kernel->event_law.kind == kind);
	CHECK(kernel->event_law.requirements == requirements);
	CHECK((kernel->event_law.requirements &
		SG_RUNE_WEAPON_RUNTIME_PREFIRE_TRACE) != 0U);
	CHECK((kernel->event_law.requirements &
		SG_RUNE_WEAPON_RUNTIME_DAMAGE_SCALE) != 0U);
}

static void TestFullCatalog(void)
{
	weapon_fixture_t fixture;
	sg_rune_compact_weapon_field_t *field = NULL;
	sg_rune_compact_weapon_field_error_t error = { 0 };
	sg_rune_compact_weapon_field_view_t view;
	uint32_t profile;
	uint32_t kernels = 0U;
	uint32_t references = 0U;

	InitFixture(&fixture);
	CHECK(SG_RuneCompactWeaponFieldBuild(&fixture.input, &field, &error) ==
		SG_RUNE_COMPACT_WEAPON_FIELD_OK);
	if (field == NULL)
		return;
	CHECK(SG_RuneCompactWeaponFieldReadBound(field, &view));
	CHECK(memcmp(&view.identity, &fixture.identity, sizeof(view.identity)) == 0);
	for (profile = 0U; profile < PROFILE_COUNT; profile++) {
		const sg_rune_weapon_response_family_mask_t families =
			fixture.compact_profiles[profile].response_families;
		uint32_t family;

		for (family = 0U;
			family < (uint32_t)SG_RUNE_WEAPON_RESPONSE_FAMILY_COUNT;
			family++) {
			const sg_rune_weapon_response_family_mask_t bit =
				SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(family);
			const sg_rune_weapon_response_kernel_t *kernel;
			uint32_t expected;

			if ((families & bit) == 0U)
				continue;
			CHECK(kernels < view.kernel_count);
			if (kernels >= view.kernel_count)
				continue;
			kernel = &view.kernels[kernels++];
			CHECK(kernel->profile == profile);
			CHECK((uint32_t)kernel->family == family);
			CHECK(SG_RuneCompactWeaponKernelReferenceCount(
				&fixture.compact_profiles[profile], kernel->family, &expected));
			CHECK(kernel->functions.first == references);
			CHECK(kernel->functions.count == expected);
			references += expected;
		}
	}
	CHECK(kernels == view.kernel_count);
	CHECK(references == view.weapon_function_ref_count);
	CHECK(view.kernels != NULL && view.weapon_function_refs != NULL &&
		view.functions != NULL);
	CHECK(view.attachment_count != 0U && view.attachments != NULL &&
		view.relation_ref_count != 0U && view.relation_refs != NULL &&
		view.response != NULL && view.response->facts == fixture.facts);
	SG_RuneCompactWeaponFieldDestroy(field);
}

static void TestSharedRelationAttachments(void)
{
	weapon_fixture_t fixture;
	sg_rune_compact_weapon_field_t *field = NULL;
	sg_rune_compact_weapon_field_error_t error = { 0 };
	sg_rune_compact_weapon_field_view_t view;
	const sg_rune_compact_weapon_field_attachment_t *blaster;
	const sg_rune_compact_weapon_field_attachment_t *hyperblaster;
	const sg_rune_compact_weapon_field_attachment_t *rail;
	const sg_rune_compact_weapon_field_attachment_t *rocket;
	const sg_rune_compact_weapon_field_attachment_t *bfg;
	uint32_t attachment_index;

	InitFixture(&fixture);
	CHECK(SG_RuneCompactWeaponFieldBuild(&fixture.input, &field, &error) ==
		SG_RUNE_COMPACT_WEAPON_FIELD_OK);
	if (field == NULL)
		return;
	CHECK(SG_RuneCompactWeaponFieldReadBound(field, &view));
	blaster = FindAttachment(&view, 0U, 0U, SG_WEAPON_PROFILE_BLASTER,
		SG_RUNE_WEAPON_RESPONSE_STRAIGHT_BOLT);
	CHECK(blaster != NULL);
	if (blaster != NULL) {
		CHECK(blaster->relation_class ==
			SG_RUNE_COMPACT_WEAPON_RELATION_DIRECT);
		CHECK(blaster->relation_span < view.relation_span_count);
		if (blaster->relation_span < view.relation_span_count) {
			CHECK(memcmp(&view.relation_spans[blaster->relation_span].references,
				&blaster->relations, sizeof(blaster->relations)) == 0);
		}
		CHECK(blaster->relations.count == 1U);
		CHECK(blaster->relations.first + blaster->relations.count <=
			view.relation_ref_count);
		if (blaster->relations.first + blaster->relations.count <=
			view.relation_ref_count) {
			CHECK(view.relation_refs[blaster->relations.first].kind ==
				SG_RUNE_COMPACT_RESPONSE_REF_CERTIFIED_FACT);
			CHECK(view.relation_refs[blaster->relations.first].index == 0U);
		}
	}
	hyperblaster = FindAttachment(&view, 0U, 0U,
		SG_WEAPON_PROFILE_HYPERBLASTER,
		SG_RUNE_WEAPON_RESPONSE_HYPERBLASTER);
	CHECK(hyperblaster != NULL);
	if (blaster != NULL && hyperblaster != NULL)
		CHECK(blaster->relation_span == hyperblaster->relation_span);
	rail = FindAttachment(&view, 0U, 0U, SG_WEAPON_PROFILE_RAILGUN,
		SG_RUNE_WEAPON_RESPONSE_RAIL);
	CHECK(rail != NULL);
	if (rail != NULL) {
		CHECK(rail->relations.count == 2U);
		CHECK(view.relation_refs[rail->relations.first].index == 0U);
		CHECK(view.relation_refs[rail->relations.first + 1U].index == 1U);
	}
	CHECK(FindAttachment(&view, 1U, 1U, SG_WEAPON_PROFILE_BLASTER,
		SG_RUNE_WEAPON_RESPONSE_STRAIGHT_BOLT) == NULL);
	rocket = FindAttachment(&view, 1U, 1U, SG_WEAPON_PROFILE_ROCKET_LAUNCHER,
		SG_RUNE_WEAPON_RESPONSE_ROCKET_IMPACT);
	CHECK(rocket != NULL);
	if (rocket != NULL) {
		CHECK(rocket->relations.count == 1U);
		CHECK(view.relation_refs[rocket->relations.first].index == 2U);
	}
	bfg = FindAttachment(&view, 1U, 1U, SG_WEAPON_PROFILE_BFG,
		SG_RUNE_WEAPON_RESPONSE_BFG);
	CHECK(bfg != NULL && bfg->relations.count == 1U);
	CHECK(view.relation_span_count != 0U && view.relation_spans != NULL);
	CHECK(view.relation_span_count == view.attachment_count);
	for (attachment_index = 0U; attachment_index < view.attachment_count;
		attachment_index++) {
		const sg_rune_compact_weapon_field_attachment_t *attachment =
			&view.attachments[attachment_index];
		uint32_t relation_index;

		if (attachment_index != 0U) {
			const sg_rune_compact_weapon_field_attachment_t *previous =
				&view.attachments[attachment_index - 1U];

			CHECK(previous->cell.value < attachment->cell.value ||
				(previous->cell.value == attachment->cell.value &&
				(previous->source_surface < attachment->source_surface ||
				(previous->source_surface == attachment->source_surface &&
				(uint32_t)previous->relation_class <
					(uint32_t)attachment->relation_class))));
		}
		for (relation_index = attachment->relations.first;
			relation_index < attachment->relations.first +
				attachment->relations.count; relation_index++) {
			const uint32_t fact_index = view.relation_refs[relation_index].index;
			const sg_rune_compact_response_fact_t *fact = &fixture.facts[fact_index];

			CHECK(view.relation_refs[relation_index].kind ==
				SG_RUNE_COMPACT_RESPONSE_REF_CERTIFIED_FACT);
			CHECK(fixture.fragments[fact->source_fragment].parent_cell.value ==
				attachment->cell.value);
			CHECK(fixture.patches[fact->target_patch].source_surface ==
				attachment->source_surface);
		}
	}
	SG_RuneCompactWeaponFieldDestroy(field);
}

static void TestRelationDriftFailsClosed(void)
{
	weapon_fixture_t fixture;
	sg_rune_compact_weapon_field_t *field = NULL;
	sg_rune_compact_weapon_field_error_t error = { 0 };
	sg_rune_compact_weapon_field_view_t view;
	sg_rune_compact_weapon_field_view_t preserved;

	InitFixture(&fixture);
	CHECK(SG_RuneCompactWeaponFieldBuild(&fixture.input, &field, &error) ==
		SG_RUNE_COMPACT_WEAPON_FIELD_OK);
	if (field == NULL)
		return;
	memset(&view, 0x4d, sizeof(view));
	preserved = view;
	fixture.relations_available = 0;
	CHECK(!SG_RuneCompactWeaponFieldReadBound(field, &view));
	CHECK(memcmp(&view, &preserved, sizeof(view)) == 0);
	fixture.relations_available = 1;
	fixture.relations.response.facts = NULL;
	CHECK(!SG_RuneCompactWeaponFieldReadBound(field, &view));
	CHECK(memcmp(&view, &preserved, sizeof(view)) == 0);
	SG_RuneCompactWeaponFieldDestroy(field);
}

static void TestStaticGrenadeFlight(void)
{
	weapon_fixture_t fixture;
	sg_rune_compact_weapon_field_t *field = NULL;
	sg_rune_compact_weapon_field_error_t error = { 0 };
	sg_rune_compact_weapon_field_view_t view;
	const sg_rune_weapon_response_kernel_t *kernel;
	const sg_rune_analytic_function_t *ballistic;
	const sg_rune_analytic_function_t *fuse;
	const sg_weapon_profile_t *profile;

	InitFixture(&fixture);
	CHECK(SG_RuneCompactWeaponFieldBuild(&fixture.input, &field, &error) ==
		SG_RUNE_COMPACT_WEAPON_FIELD_OK);
	if (field == NULL)
		return;
	CHECK(SG_RuneCompactWeaponFieldReadBound(field, &view));
	kernel = FindKernel(&view, SG_WEAPON_PROFILE_GRENADE_LAUNCHER,
		SG_RUNE_WEAPON_RESPONSE_GRENADE_FLIGHT);
	CHECK(kernel != NULL && kernel->functions.count > 2U);
	if (kernel != NULL) {
		CHECK(kernel->event_law.kind ==
			SG_RUNE_WEAPON_EVENT_GRENADE_BOUNCE_FUSE);
		profile = &fixture.resolved_profiles[
			(uint32_t)SG_WEAPON_PROFILE_GRENADE_LAUNCHER - 1U];
		ballistic = FindKernelFunction(&view, kernel,
			SG_RUNE_WEAPON_EFFECT_CHANNEL_PRIMARY, 0U,
			SG_RUNE_ANALYTIC_OUTPUT_POSITION_Z);
		fuse = FindKernelFunction(&view, kernel,
			SG_RUNE_WEAPON_EFFECT_CHANNEL_PRIMARY, 0U,
			SG_RUNE_ANALYTIC_OUTPUT_FUSE_REMAINING_SECONDS);
		CHECK(ballistic != NULL && fuse != NULL);
		if (ballistic != NULL) {
			CHECK(ballistic->form == SG_RUNE_COMPACT_ANALYTIC_BALLISTIC);
			CHECK(view.analytic.input_dimensions[ballistic->inputs.first] ==
				SG_RUNE_ANALYTIC_INPUT_TIME_SECONDS);
			CHECK(view.analytic.ballistics[ballistic->definition].initial.bits ==
				Bits(0.0f));
			CHECK(view.analytic.ballistics[ballistic->definition].first_derivative.bits ==
				Bits(profile->launch_vertical_speed));
			CHECK(view.analytic.ballistics[ballistic->definition].half_second_derivative.bits ==
				Bits(-0.5f * 100.0f * profile->gravity_scale));
		}
		if (fuse != NULL) {
			CHECK(fuse->form == SG_RUNE_COMPACT_ANALYTIC_AFFINE);
			CHECK(view.analytic.input_dimensions[fuse->inputs.first] ==
				SG_RUNE_ANALYTIC_INPUT_TIME_SECONDS);
			CHECK(view.analytic.affines[fuse->definition].bias.bits ==
				Bits((float)profile->fuse_ms * 0.001f));
			CHECK(view.analytic.affine_slopes[
				view.analytic.affines[fuse->definition].slopes.first].bits ==
				Bits(-1.0f));
		}
	}
	kernel = FindKernel(&view, SG_WEAPON_PROFILE_HAND_GRENADE,
		SG_RUNE_WEAPON_RESPONSE_GRENADE_FLIGHT);
	CHECK(kernel != NULL && kernel->functions.count > 2U);
	SG_RuneCompactWeaponFieldDestroy(field);
}

static void TestStaticHostLawValues(void)
{
	weapon_fixture_t fixture;
	sg_rune_compact_weapon_field_t *field = NULL;
	sg_rune_compact_weapon_field_error_t error = { 0 };
	sg_rune_compact_weapon_field_view_t view;
	const sg_rune_weapon_response_kernel_t *kernel;
	const sg_weapon_profile_t *profile;
	uint32_t bits;

	InitFixture(&fixture);
	CHECK(SG_RuneCompactWeaponFieldBuild(&fixture.input, &field, &error) ==
		SG_RUNE_COMPACT_WEAPON_FIELD_OK);
	if (field == NULL)
		return;
	CHECK(SG_RuneCompactWeaponFieldReadBound(field, &view));
	profile = &fixture.resolved_profiles[
		(uint32_t)SG_WEAPON_PROFILE_GRENADE_LAUNCHER - 1U];
	kernel = FindKernel(&view, SG_WEAPON_PROFILE_GRENADE_LAUNCHER,
		SG_RUNE_WEAPON_RESPONSE_GRENADE_FLIGHT);
	CHECK(StaticLawBits(&view, kernel,
		SG_RUNE_WEAPON_STATIC_LAW_PROJECTILE_HALF_EXTENT, &bits));
	CHECK(bits == Bits(profile->projectile_half_extent));
	CHECK(StaticLawBits(&view, kernel,
		SG_RUNE_WEAPON_STATIC_LAW_LAUNCH_JITTER, &bits));
	CHECK(bits == Bits(profile->launch_jitter));
	CHECK(StaticLawBits(&view, kernel,
		SG_RUNE_WEAPON_STATIC_LAW_GRAVITY_SCALE, &bits));
	CHECK(bits == Bits(profile->gravity_scale));
	CHECK(StaticLawBits(&view, kernel, SG_RUNE_WEAPON_STATIC_LAW_FUSE_MS,
		&bits));
	CHECK(bits == Bits((float)profile->fuse_ms));

	profile = &fixture.resolved_profiles[
		(uint32_t)SG_WEAPON_PROFILE_CHAINGUN - 1U];
	kernel = FindKernel(&view, SG_WEAPON_PROFILE_CHAINGUN,
		SG_RUNE_WEAPON_RESPONSE_AUTOMATIC_SPREAD);
	CHECK(StaticLawBits(&view, kernel,
		SG_RUNE_WEAPON_STATIC_LAW_HORIZONTAL_SPREAD, &bits));
	CHECK(bits == Bits(profile->horizontal_spread));
	CHECK(StaticLawBits(&view, kernel, SG_RUNE_WEAPON_STATIC_LAW_CADENCE_MS,
		&bits));
	CHECK(bits == Bits((float)profile->cadence_ms));
	CHECK(StaticLawBits(&view, kernel,
		SG_RUNE_WEAPON_STATIC_LAW_AMMO_LIVE_FIRE_MINIMUM, &bits));
	CHECK(bits == Bits((float)profile->ammo.live_fire_minimum));

	profile = &fixture.resolved_profiles[
		(uint32_t)SG_WEAPON_PROFILE_ROCKET_LAUNCHER - 1U];
	kernel = FindKernel(&view, SG_WEAPON_PROFILE_ROCKET_LAUNCHER,
		SG_RUNE_WEAPON_RESPONSE_ROCKET_SPLASH);
	CHECK(StaticLawBits(&view, kernel,
		SG_RUNE_WEAPON_STATIC_LAW_SELF_DAMAGE_SCALE, &bits));
	CHECK(bits == Bits(profile->self_damage_scale));
	CHECK(StaticLawBits(&view, kernel,
		SG_RUNE_WEAPON_STATIC_LAW_TEAMMATE_RISK_SCALE, &bits));
	CHECK(bits == Bits(profile->teammate_risk_scale));
	CHECK(StaticLawBits(&view, kernel, SG_RUNE_WEAPON_STATIC_LAW_SPLASH_OWNER,
		&bits));
	CHECK(bits == Bits((float)profile->splash.owner));

	profile = &fixture.resolved_profiles[(uint32_t)SG_WEAPON_PROFILE_BFG - 1U];
	kernel = FindKernel(&view, SG_WEAPON_PROFILE_BFG,
		SG_RUNE_WEAPON_RESPONSE_BFG);
	CHECK(StaticLawBits(&view, kernel,
		SG_RUNE_WEAPON_STATIC_LAW_SECONDARY_SPLASH_DAMAGE, &bits));
	CHECK(bits == Bits(profile->secondary_splash_damage));
	CHECK(StaticLawBits(&view, kernel,
		SG_RUNE_WEAPON_STATIC_LAW_PERIODIC_RAY_INTERVAL_MS, &bits));
	CHECK(bits == Bits((float)profile->periodic_ray_interval_ms));
	SG_RuneCompactWeaponFieldDestroy(field);
}

static void TestHostLawCoverageAndRuntimeBoundary(void)
{
	weapon_fixture_t fixture;
	sg_rune_compact_weapon_field_t *field = NULL;
	sg_rune_compact_weapon_field_error_t error = { 0 };
	sg_rune_compact_weapon_field_view_t view;
	uint32_t input;

	InitFixture(&fixture);
	CHECK(SG_RuneCompactWeaponFieldBuild(&fixture.input, &field, &error) ==
		SG_RUNE_COMPACT_WEAPON_FIELD_OK);
	if (field == NULL)
		return;
	CHECK(SG_RuneCompactWeaponFieldReadBound(field, &view));
	if (view.kernels == NULL) {
		SG_RuneCompactWeaponFieldDestroy(field);
		return;
	}
	CheckKernelLaw(&view, SG_WEAPON_PROFILE_SHOTGUN,
		SG_RUNE_WEAPON_RESPONSE_HITSCAN, SG_RUNE_WEAPON_EVENT_HITSCAN_RAY,
		SG_RUNE_WEAPON_RUNTIME_PREFIRE_TRACE |
		SG_RUNE_WEAPON_RUNTIME_DAMAGE_SCALE |
		SG_RUNE_WEAPON_RUNTIME_RANDOM_U15);
	CheckKernelLaw(&view, SG_WEAPON_PROFILE_RAILGUN,
		SG_RUNE_WEAPON_RESPONSE_RAIL, SG_RUNE_WEAPON_EVENT_RAIL_PENETRATION,
		SG_RUNE_WEAPON_RUNTIME_PREFIRE_TRACE |
		SG_RUNE_WEAPON_RUNTIME_DAMAGE_SCALE |
		SG_RUNE_WEAPON_RUNTIME_TARGET_RELATION);
	CheckKernelLaw(&view, SG_WEAPON_PROFILE_CHAINGUN,
		SG_RUNE_WEAPON_RESPONSE_AUTOMATIC_SPREAD,
		SG_RUNE_WEAPON_EVENT_SPREAD_RAYS,
		SG_RUNE_WEAPON_RUNTIME_PREFIRE_TRACE |
		SG_RUNE_WEAPON_RUNTIME_DAMAGE_SCALE |
		SG_RUNE_WEAPON_RUNTIME_RANDOM_U15 |
		SG_RUNE_WEAPON_RUNTIME_WEAPON_FRAME |
		SG_RUNE_WEAPON_RUNTIME_ATTACK_HELD |
		SG_RUNE_WEAPON_RUNTIME_AMMO_COUNT);
	CheckKernelLaw(&view, SG_WEAPON_PROFILE_SUPER_SHOTGUN,
		SG_RUNE_WEAPON_RESPONSE_SHOTGUN_CONE,
		SG_RUNE_WEAPON_EVENT_SPREAD_RAYS,
		SG_RUNE_WEAPON_RUNTIME_PREFIRE_TRACE |
		SG_RUNE_WEAPON_RUNTIME_DAMAGE_SCALE |
		SG_RUNE_WEAPON_RUNTIME_RANDOM_U15);
	CheckKernelLaw(&view, SG_WEAPON_PROFILE_BLASTER,
		SG_RUNE_WEAPON_RESPONSE_STRAIGHT_BOLT,
		SG_RUNE_WEAPON_EVENT_STRAIGHT_PROJECTILE,
		SG_RUNE_WEAPON_RUNTIME_PREFIRE_TRACE |
		SG_RUNE_WEAPON_RUNTIME_DAMAGE_SCALE |
		SG_RUNE_WEAPON_RUNTIME_COLLISION_EVENT |
		SG_RUNE_WEAPON_RUNTIME_PROJECTILE_ORIGIN |
		SG_RUNE_WEAPON_RUNTIME_TARGET_RELATION);
	CheckKernelLaw(&view, SG_WEAPON_PROFILE_ROCKET_LAUNCHER,
		SG_RUNE_WEAPON_RESPONSE_ROCKET_IMPACT,
		SG_RUNE_WEAPON_EVENT_PROJECTILE_IMPACT,
		SG_RUNE_WEAPON_RUNTIME_PREFIRE_TRACE |
		SG_RUNE_WEAPON_RUNTIME_DAMAGE_SCALE |
		SG_RUNE_WEAPON_RUNTIME_COLLISION_EVENT |
		SG_RUNE_WEAPON_RUNTIME_PROJECTILE_ORIGIN |
		SG_RUNE_WEAPON_RUNTIME_TARGET_RELATION |
		SG_RUNE_WEAPON_RUNTIME_RANDOM_U15);
	CheckKernelLaw(&view, SG_WEAPON_PROFILE_ROCKET_LAUNCHER,
		SG_RUNE_WEAPON_RESPONSE_ROCKET_SPLASH,
		SG_RUNE_WEAPON_EVENT_LINEAR_SPLASH,
		SG_RUNE_WEAPON_RUNTIME_PREFIRE_TRACE |
		SG_RUNE_WEAPON_RUNTIME_DAMAGE_SCALE |
		SG_RUNE_WEAPON_RUNTIME_COLLISION_EVENT |
		SG_RUNE_WEAPON_RUNTIME_TARGET_RELATION);
	CheckKernelLaw(&view, SG_WEAPON_PROFILE_GRENADE_LAUNCHER,
		SG_RUNE_WEAPON_RESPONSE_GRENADE_BOUNCE_FUSE,
		SG_RUNE_WEAPON_EVENT_GRENADE_BOUNCE_FUSE,
		SG_RUNE_WEAPON_RUNTIME_PREFIRE_TRACE |
		SG_RUNE_WEAPON_RUNTIME_DAMAGE_SCALE |
		SG_RUNE_WEAPON_RUNTIME_RANDOM_U15 |
		SG_RUNE_WEAPON_RUNTIME_FUSE_DEADLINE |
		SG_RUNE_WEAPON_RUNTIME_COLLISION_EVENT |
		SG_RUNE_WEAPON_RUNTIME_PROJECTILE_ORIGIN |
		SG_RUNE_WEAPON_RUNTIME_TARGET_RELATION |
		SG_RUNE_WEAPON_RUNTIME_EVENT_FRAME);
	CheckKernelLaw(&view, SG_WEAPON_PROFILE_HAND_GRENADE,
		SG_RUNE_WEAPON_RESPONSE_GRENADE_FLIGHT,
		SG_RUNE_WEAPON_EVENT_GRENADE_BOUNCE_FUSE,
		SG_RUNE_WEAPON_RUNTIME_PREFIRE_TRACE |
		SG_RUNE_WEAPON_RUNTIME_DAMAGE_SCALE |
		SG_RUNE_WEAPON_RUNTIME_RANDOM_U15 |
		SG_RUNE_WEAPON_RUNTIME_FUSE_DEADLINE |
		SG_RUNE_WEAPON_RUNTIME_COLLISION_EVENT |
		SG_RUNE_WEAPON_RUNTIME_PROJECTILE_ORIGIN |
		SG_RUNE_WEAPON_RUNTIME_TARGET_RELATION |
		SG_RUNE_WEAPON_RUNTIME_EVENT_FRAME |
		SG_RUNE_WEAPON_RUNTIME_WEAPON_FRAME |
		SG_RUNE_WEAPON_RUNTIME_ATTACK_HELD |
		SG_RUNE_WEAPON_RUNTIME_AMMO_COUNT);
	CheckKernelLaw(&view, SG_WEAPON_PROFILE_HYPERBLASTER,
		SG_RUNE_WEAPON_RESPONSE_HYPERBLASTER,
		SG_RUNE_WEAPON_EVENT_HYPERBLASTER,
		SG_RUNE_WEAPON_RUNTIME_PREFIRE_TRACE |
		SG_RUNE_WEAPON_RUNTIME_DAMAGE_SCALE |
		SG_RUNE_WEAPON_RUNTIME_COLLISION_EVENT |
		SG_RUNE_WEAPON_RUNTIME_PROJECTILE_ORIGIN |
		SG_RUNE_WEAPON_RUNTIME_TARGET_RELATION |
		SG_RUNE_WEAPON_RUNTIME_WEAPON_FRAME |
		SG_RUNE_WEAPON_RUNTIME_ATTACK_HELD |
		SG_RUNE_WEAPON_RUNTIME_AMMO_COUNT);
	CheckKernelLaw(&view, SG_WEAPON_PROFILE_BFG,
		SG_RUNE_WEAPON_RESPONSE_BFG, SG_RUNE_WEAPON_EVENT_BFG_COMPOSITE,
		SG_RUNE_WEAPON_RUNTIME_PREFIRE_TRACE |
		SG_RUNE_WEAPON_RUNTIME_DAMAGE_SCALE |
		SG_RUNE_WEAPON_RUNTIME_COLLISION_EVENT |
		SG_RUNE_WEAPON_RUNTIME_PROJECTILE_ORIGIN |
		SG_RUNE_WEAPON_RUNTIME_TARGET_RELATION |
		SG_RUNE_WEAPON_RUNTIME_IMPACT_STATE |
		SG_RUNE_WEAPON_RUNTIME_ENTITY_QUERY);
	CheckKernelLaw(&view, SG_WEAPON_PROFILE_BFG,
		SG_RUNE_WEAPON_RESPONSE_SPECIAL,
		SG_RUNE_WEAPON_EVENT_BFG_COMPOSITE,
		SG_RUNE_WEAPON_RUNTIME_PREFIRE_TRACE |
		SG_RUNE_WEAPON_RUNTIME_DAMAGE_SCALE |
		SG_RUNE_WEAPON_RUNTIME_WEAPON_FRAME |
		SG_RUNE_WEAPON_RUNTIME_AMMO_COUNT |
		SG_RUNE_WEAPON_RUNTIME_ENTITY_QUERY);
	CheckKernelLaw(&view, SG_WEAPON_PROFILE_HOOK,
		SG_RUNE_WEAPON_RESPONSE_SPECIAL, SG_RUNE_WEAPON_EVENT_HOOK_DAMAGE,
		SG_RUNE_WEAPON_RUNTIME_PREFIRE_TRACE |
		SG_RUNE_WEAPON_RUNTIME_DAMAGE_SCALE |
		SG_RUNE_WEAPON_RUNTIME_COLLISION_EVENT |
		SG_RUNE_WEAPON_RUNTIME_TARGET_RELATION |
		SG_RUNE_WEAPON_RUNTIME_EVENT_FRAME);
	{
		uint32_t time_inputs = 0U;

		for (input = 0U; input < view.analytic.input_dimension_count; input++) {
			CHECK(view.analytic.input_dimensions[input] ==
				SG_RUNE_ANALYTIC_INPUT_DISTANCE ||
				view.analytic.input_dimensions[input] ==
				SG_RUNE_ANALYTIC_INPUT_TIME_SECONDS);
			if (view.analytic.input_dimensions[input] ==
				SG_RUNE_ANALYTIC_INPUT_TIME_SECONDS)
				time_inputs++;
		}
		CHECK(time_inputs != 0U);
	}
	SG_RuneCompactWeaponFieldDestroy(field);
}

static void TestBalanceAndDeterminism(void)
{
	weapon_fixture_t fixture;
	sg_rune_compact_weapon_field_t *first = NULL;
	sg_rune_compact_weapon_field_t *second = NULL;
	sg_rune_compact_weapon_field_error_t error = { 0 };
	sg_rune_compact_weapon_field_view_t first_view;
	sg_rune_compact_weapon_field_view_t second_view;

	InitFixture(&fixture);
#if SG_WEAPON_BALANCE_COMPILED
	fixture.weapon_law.weapon_balance_enabled = 1U;
	ResolveProfiles(&fixture);
	/* ResolveProfiles changes the identity-bound catalog ID. Keep the borrowed
	 * response owner on that same identity rather than weakening the field's
	 * relation check for the balanced host law. */
	fixture.relations.identity = fixture.identity;
#endif
	CHECK(SG_RuneCompactWeaponFieldBuild(&fixture.input, &first, &error) ==
		SG_RUNE_COMPACT_WEAPON_FIELD_OK);
	CHECK(SG_RuneCompactWeaponFieldBuild(&fixture.input, &second, &error) ==
		SG_RUNE_COMPACT_WEAPON_FIELD_OK);
	if (first == NULL || second == NULL)
		goto done;
	CHECK(SG_RuneCompactWeaponFieldReadBound(first, &first_view));
	CHECK(SG_RuneCompactWeaponFieldReadBound(second, &second_view));
	CHECK(first_view.kernel_count == second_view.kernel_count);
	CHECK(first_view.weapon_function_ref_count ==
		second_view.weapon_function_ref_count);
	CHECK(memcmp(first_view.kernels, second_view.kernels,
		(size_t)first_view.kernel_count * sizeof(*first_view.kernels)) == 0);
	CHECK(memcmp(first_view.weapon_function_refs,
		second_view.weapon_function_refs,
		(size_t)first_view.weapon_function_ref_count *
			sizeof(*first_view.weapon_function_refs)) == 0);
done:
	SG_RuneCompactWeaponFieldDestroy(second);
	SG_RuneCompactWeaponFieldDestroy(first);
}

static void TestInputRejection(void)
{
	weapon_fixture_t fixture;
	sg_rune_compact_weapon_field_t *field = (void *)(uintptr_t)1U;
	sg_rune_compact_weapon_field_error_t error = { 0 };

	InitFixture(&fixture);
	fixture.input.relations_owner = NULL;
	CHECK(SG_RuneCompactWeaponFieldBuild(&fixture.input, &field, &error) ==
		SG_RUNE_COMPACT_WEAPON_FIELD_INVALID_ARGUMENT);
	CHECK(field == NULL);
	InitFixture(&fixture);
	fixture.input.identity = NULL;
	CHECK(SG_RuneCompactWeaponFieldBuild(&fixture.input, &field, &error) ==
		SG_RUNE_COMPACT_WEAPON_FIELD_INVALID_ARGUMENT);
	CHECK(field == NULL);
	InitFixture(&fixture);
	fixture.input.weapon_law_id++;
	field = (void *)(uintptr_t)1U;
	CHECK(SG_RuneCompactWeaponFieldBuild(&fixture.input, &field, &error) ==
		SG_RUNE_COMPACT_WEAPON_FIELD_INVALID_ARGUMENT);
	CHECK(field == NULL);
	InitFixture(&fixture);
	fixture.weapon_law.reserved[0] = 1U;
	field = (void *)(uintptr_t)1U;
	CHECK(SG_RuneCompactWeaponFieldBuild(&fixture.input, &field, &error) ==
		SG_RUNE_COMPACT_WEAPON_FIELD_INVALID_WEAPON_LAW);
	CHECK(field == NULL);
	InitFixture(&fixture);
	fixture.weapon_law.fast_switch_enabled = 1U;
	field = (void *)(uintptr_t)1U;
	CHECK(SG_RuneCompactWeaponFieldBuild(&fixture.input, &field, &error) ==
		SG_RUNE_COMPACT_WEAPON_FIELD_INVALID_PROFILE);
	CHECK(field == NULL);
	InitFixture(&fixture);
	{
		const uint64_t stale_weapon_law_id = fixture.identity.weapon_law_id;

		fixture.weapon_law.fast_switch_enabled = 1U;
		ResolveProfiles(&fixture);
		fixture.identity.weapon_law_id = stale_weapon_law_id;
		fixture.input.weapon_law_id = stale_weapon_law_id;
		fixture.relations.identity = fixture.identity;
		field = (void *)(uintptr_t)1U;
		CHECK(SG_RuneCompactWeaponFieldBuild(&fixture.input, &field, &error) ==
			SG_RUNE_COMPACT_WEAPON_FIELD_INVALID_PROFILE);
		CHECK(field == NULL);
	}
	InitFixture(&fixture);
	fixture.compact_profiles[0].source_profile = 2U;
	field = (void *)(uintptr_t)1U;
	CHECK(SG_RuneCompactWeaponFieldBuild(&fixture.input, &field, &error) ==
		SG_RUNE_COMPACT_WEAPON_FIELD_INVALID_PROFILE);
	CHECK(field == NULL);
	InitFixture(&fixture);
	fixture.identity.weapon_profile_catalog_id++;
	field = (void *)(uintptr_t)1U;
	CHECK(SG_RuneCompactWeaponFieldBuild(&fixture.input, &field, &error) ==
		SG_RUNE_COMPACT_WEAPON_FIELD_INVALID_PROFILE);
	CHECK(field == NULL);
	InitFixture(&fixture);
	fixture.facts[0].source_fragment = 3U;
	field = (void *)(uintptr_t)1U;
	CHECK(SG_RuneCompactWeaponFieldBuild(&fixture.input, &field, &error) ==
		SG_RUNE_COMPACT_WEAPON_FIELD_INVALID_RELATIONS);
	CHECK(field == NULL);
}

static void TestAllocationFailure(void)
{
#if defined(SG_RUNE_COMPACT_WEAPON_FIELD_TEST_WRAP_CALLOC)
	weapon_fixture_t fixture;
	sg_rune_compact_weapon_field_t *field = (void *)(uintptr_t)1U;
	sg_rune_compact_weapon_field_error_t error = { 0 };
	size_t total;
	size_t index;

	InitFixture(&fixture);
	calloc_count = 0U;
	calloc_fail_after = SIZE_MAX;
	CHECK(SG_RuneCompactWeaponFieldBuild(&fixture.input, &field, &error) ==
		SG_RUNE_COMPACT_WEAPON_FIELD_OK);
	total = calloc_count;
	SG_RuneCompactWeaponFieldDestroy(field);
	for (index = 0U; index < total; index++) {
		InitFixture(&fixture);
		field = (void *)(uintptr_t)1U;
		calloc_count = 0U;
		calloc_fail_after = index;
		CHECK(SG_RuneCompactWeaponFieldBuild(&fixture.input, &field, &error) ==
			SG_RUNE_COMPACT_WEAPON_FIELD_ALLOCATION_FAILED);
		CHECK(field == NULL);
	}
	calloc_fail_after = SIZE_MAX;
#endif
}

static void TestRelationRepresentationLimits(void)
{
#if defined(SG_RUNE_COMPACT_WEAPON_FIELD_TEST_WRAP_CALLOC)
	weapon_fixture_t fixture;
	sg_rune_compact_weapon_field_t *field = NULL;
	sg_rune_compact_weapon_field_error_t error = { 0 };

	/* The fixture has five fact refs in four class-indexed spans.  Exact model
	 * limits admit it; every one-less boundary must fail during planning. */
	InitFixture(&fixture);
	SG_RuneCompactWeaponFieldTestSetRelationLimits(4U, 4U, 5U);
	CHECK(SG_RuneCompactWeaponFieldBuild(&fixture.input, &field, &error) ==
		SG_RUNE_COMPACT_WEAPON_FIELD_OK);
	if (field != NULL)
		SG_RuneCompactWeaponFieldDestroy(field);

	InitFixture(&fixture);
	field = (void *)(uintptr_t)1U;
	SG_RuneCompactWeaponFieldTestSetRelationLimits(3U, 4U, 5U);
	CHECK(SG_RuneCompactWeaponFieldBuild(&fixture.input, &field, &error) ==
		SG_RUNE_COMPACT_WEAPON_FIELD_LIMIT_EXCEEDED);
	CHECK(field == NULL);

	InitFixture(&fixture);
	field = (void *)(uintptr_t)1U;
	SG_RuneCompactWeaponFieldTestSetRelationLimits(4U, 3U, 5U);
	CHECK(SG_RuneCompactWeaponFieldBuild(&fixture.input, &field, &error) ==
		SG_RUNE_COMPACT_WEAPON_FIELD_LIMIT_EXCEEDED);
	CHECK(field == NULL);

	InitFixture(&fixture);
	field = (void *)(uintptr_t)1U;
	SG_RuneCompactWeaponFieldTestSetRelationLimits(4U, 4U, 4U);
	CHECK(SG_RuneCompactWeaponFieldBuild(&fixture.input, &field, &error) ==
		SG_RUNE_COMPACT_WEAPON_FIELD_LIMIT_EXCEEDED);
	CHECK(field == NULL);
	SG_RuneCompactWeaponFieldTestResetRelationLimits();
#endif
}

int main(void)
{
	TestFullCatalog();
	TestSharedRelationAttachments();
	TestRelationDriftFailsClosed();
	TestStaticGrenadeFlight();
	TestStaticHostLawValues();
	TestHostLawCoverageAndRuntimeBoundary();
	TestBalanceAndDeterminism();
	TestInputRejection();
	TestRelationRepresentationLimits();
	TestAllocationFailure();
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
