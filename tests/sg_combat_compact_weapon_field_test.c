#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "g_local.h"
#include "slipgate/sg_combat.h"
#include "slipgate/sg_rune_compact_weapon_field.h"

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

typedef struct compact_weapon_fixture_s
{
	sg_rune_compact_model_t model;
	sg_rune_compact_cell_t cells[3];
	sg_rune_compact_source_surface_t source_surfaces[2];
	sg_rune_weapon_profile_t profiles[SG_WEAPON_PROFILE_COUNT - 1U];
	sg_rune_weapon_response_kernel_t kernels[3];
	sg_rune_compact_weapon_field_attachment_t attachments[2];
	sg_rune_compact_weapon_relation_span_t relation_spans[2];
	sg_rune_compact_response_ref_t relation_refs[2];
	sg_rune_compact_response_fragment_t fragments[2];
	sg_rune_compact_response_patch_t patches[2];
	sg_rune_compact_response_fact_t facts[2];
} compact_weapon_fixture_t;

static void InitFixture(compact_weapon_fixture_t *fixture)
{
	memset(fixture, 0, sizeof(*fixture));
	fixture->model.version = SG_RUNE_COMPACT_MODEL_VERSION;
	fixture->model.schema_tag = SG_RUNE_COMPACT_MODEL_SCHEMA_TAG;
	fixture->model.identity.weapon_law_id = UINT64_C(1);
	fixture->model.cells = fixture->cells;
	fixture->model.cell_count = 3U;
	fixture->model.source_surfaces = fixture->source_surfaces;
	fixture->model.source_surface_count = 2U;
	fixture->model.weapon_profiles = fixture->profiles;
	fixture->model.weapon_profile_count = SG_WEAPON_PROFILE_COUNT - 1U;
	fixture->model.weapon_kernels = fixture->kernels;
	fixture->model.weapon_kernel_count = 3U;
	fixture->model.weapon_attachments = fixture->attachments;
	fixture->model.weapon_attachment_count = 2U;
	fixture->model.weapon_relation_spans = fixture->relation_spans;
	fixture->model.weapon_relation_span_count = 2U;
	fixture->model.weapon_relation_refs = fixture->relation_refs;
	fixture->model.weapon_relation_ref_count = 2U;
	fixture->model.response.source_fragments = fixture->fragments;
	fixture->model.response.source_fragment_count = 2U;
	fixture->model.response.target_patches = fixture->patches;
	fixture->model.response.target_patch_count = 2U;
	fixture->model.response.facts = fixture->facts;
	fixture->model.response.fact_count = 2U;
	fixture->model.response.exact_live_prefire_trace_required = 1U;
	fixture->profiles[SG_WEAPON_PROFILE_ROCKET_LAUNCHER - 1U].source_profile =
		SG_WEAPON_PROFILE_ROCKET_LAUNCHER;
	fixture->profiles[SG_WEAPON_PROFILE_RAILGUN - 1U].source_profile =
		SG_WEAPON_PROFILE_RAILGUN;
	fixture->profiles[SG_WEAPON_PROFILE_BFG - 1U].source_profile =
		SG_WEAPON_PROFILE_BFG;
	fixture->kernels[0].profile = SG_WEAPON_PROFILE_ROCKET_LAUNCHER - 1U;
	fixture->kernels[0].family = SG_RUNE_WEAPON_RESPONSE_ROCKET_SPLASH;
	fixture->kernels[1].profile = SG_WEAPON_PROFILE_BFG - 1U;
	fixture->kernels[1].family = SG_RUNE_WEAPON_RESPONSE_BFG;
	fixture->kernels[2].profile = SG_WEAPON_PROFILE_RAILGUN - 1U;
	fixture->kernels[2].family = SG_RUNE_WEAPON_RESPONSE_RAIL;
	fixture->attachments[0].cell.value = 0U;
	fixture->attachments[0].source_surface = 0U;
	fixture->attachments[0].relation_class =
		SG_RUNE_COMPACT_WEAPON_RELATION_IMPACT;
	fixture->attachments[0].relations.first = 0U;
	fixture->attachments[0].relations.count = 1U;
	fixture->attachments[1].cell.value = 0U;
	fixture->attachments[1].source_surface = 1U;
	fixture->attachments[1].relation_class =
		SG_RUNE_COMPACT_WEAPON_RELATION_RAIL;
	fixture->attachments[1].relations.first = 1U;
	fixture->attachments[1].relations.count = 1U;
	fixture->relation_spans[0].references = fixture->attachments[0].relations;
	fixture->relation_spans[1].references = fixture->attachments[1].relations;
	fixture->attachments[0].relation_span = 0U;
	fixture->attachments[1].relation_span = 1U;
	fixture->relation_refs[0].kind =
		SG_RUNE_COMPACT_RESPONSE_REF_CERTIFIED_FACT;
	fixture->relation_refs[0].index = 0U;
	fixture->relation_refs[1].kind =
		SG_RUNE_COMPACT_RESPONSE_REF_CERTIFIED_FACT;
	fixture->relation_refs[1].index = 1U;
	fixture->fragments[0].parent_cell.value = 0U;
	fixture->fragments[1].parent_cell.value = 0U;
	fixture->patches[0].target_cell.value = 1U;
	fixture->patches[0].source_surface = 0U;
	fixture->patches[1].target_cell.value = 2U;
	fixture->patches[1].source_surface = 1U;
	fixture->facts[0].source_fragment = 0U;
	fixture->facts[0].target_patch = 0U;
	fixture->facts[0].flags = SG_RUNE_COMPACT_STATIC_RELATION_DIRECT;
	fixture->facts[1].source_fragment = 1U;
	fixture->facts[1].target_patch = 1U;
	fixture->facts[1].flags = SG_RUNE_COMPACT_STATIC_RELATION_DIRECT;
}

static qboolean Supports(const compact_weapon_fixture_t *fixture)
{
	return SG_CombatCompactWeaponFieldSupports(&fixture->model, 0U,
		1U,
		SG_WEAPON_PROFILE_ROCKET_LAUNCHER,
		SG_RUNE_WEAPON_RESPONSE_ROCKET_SPLASH);
}

static qboolean SupportsWeapon(const compact_weapon_fixture_t *fixture,
	uint32_t target_cell, sg_weapon_profile_id_t profile,
	sg_rune_weapon_response_family_t family)
{
	return SG_CombatCompactWeaponFieldSupports(&fixture->model, 0U,
		target_cell, (uint32_t)profile, family);
}

static void TestCertifiedSourceAttachment(void)
{
	compact_weapon_fixture_t fixture;
	compact_weapon_fixture_t preserved;

	InitFixture(&fixture);
	preserved = fixture;
	CHECK(Supports(&fixture));
	CHECK(memcmp(&fixture, &preserved, sizeof(fixture)) == 0);
	CHECK(!SG_CombatCompactWeaponFieldSupports(&fixture.model, 1U,
		1U,
		SG_WEAPON_PROFILE_ROCKET_LAUNCHER,
		SG_RUNE_WEAPON_RESPONSE_ROCKET_SPLASH));
	CHECK(!SG_CombatCompactWeaponFieldSupports(&fixture.model, 0U, 0U,
		SG_WEAPON_PROFILE_ROCKET_LAUNCHER,
		SG_RUNE_WEAPON_RESPONSE_ROCKET_SPLASH));
	CHECK(!SG_CombatCompactWeaponFieldSupports(&fixture.model, 0U,
		1U,
		SG_WEAPON_PROFILE_BLASTER,
		SG_RUNE_WEAPON_RESPONSE_STRAIGHT_BOLT));
	CHECK(SupportsWeapon(&fixture, 1U, SG_WEAPON_PROFILE_BFG,
		SG_RUNE_WEAPON_RESPONSE_BFG));
	CHECK(!SupportsWeapon(&fixture, 1U, SG_WEAPON_PROFILE_RAILGUN,
		SG_RUNE_WEAPON_RESPONSE_RAIL));
}

static void TestClassIndexedRecordsDoNotDuplicateKernels(void)
{
	compact_weapon_fixture_t fixture;

	InitFixture(&fixture);
	CHECK(fixture.model.weapon_attachment_count == 2U);
	CHECK(SupportsWeapon(&fixture, 1U, SG_WEAPON_PROFILE_ROCKET_LAUNCHER,
		SG_RUNE_WEAPON_RESPONSE_ROCKET_SPLASH));
	CHECK(SupportsWeapon(&fixture, 1U, SG_WEAPON_PROFILE_BFG,
		SG_RUNE_WEAPON_RESPONSE_BFG));
	CHECK(!SupportsWeapon(&fixture, 2U, SG_WEAPON_PROFILE_ROCKET_LAUNCHER,
		SG_RUNE_WEAPON_RESPONSE_ROCKET_SPLASH));
	CHECK(SupportsWeapon(&fixture, 2U, SG_WEAPON_PROFILE_RAILGUN,
		SG_RUNE_WEAPON_RESPONSE_RAIL));
}

static void TestMalformedSpansAndLiveGateAreNotCandidates(void)
{
	compact_weapon_fixture_t fixture;

	InitFixture(&fixture);
	fixture.model.response.exact_live_prefire_trace_required = 0U;
	CHECK(!Supports(&fixture));
	InitFixture(&fixture);
	fixture.model.version--;
	CHECK(!Supports(&fixture));
	InitFixture(&fixture);
	fixture.relation_refs[0].kind =
		SG_RUNE_COMPACT_RESPONSE_REF_CANDIDATE_GROUP;
	CHECK(!Supports(&fixture));
	InitFixture(&fixture);
	fixture.relation_refs[0].index = 2U;
	CHECK(!Supports(&fixture));
	InitFixture(&fixture);
	fixture.attachments[0].relations.count = 2U;
	CHECK(!Supports(&fixture));
	InitFixture(&fixture);
	fixture.relation_spans[0].references.first = 1U;
	CHECK(!Supports(&fixture));
	InitFixture(&fixture);
	fixture.attachments[0].relation_class =
		SG_RUNE_COMPACT_WEAPON_RELATION_CLASS_COUNT;
	CHECK(!Supports(&fixture));
	InitFixture(&fixture);
	fixture.kernels[0].family = SG_RUNE_WEAPON_RESPONSE_ROCKET_IMPACT;
	CHECK(!Supports(&fixture));
	InitFixture(&fixture);
	fixture.facts[0].source_fragment = 2U;
	CHECK(!Supports(&fixture));
}

static void TestStaleLawIdentityIsNotCandidate(void)
{
	compact_weapon_fixture_t fixture;

	InitFixture(&fixture);
	fixture.model.identity.weapon_law_id = 0U;
	CHECK(!Supports(&fixture));
}

static void TestWeightedLifeMassPrefersTheLikelyTarget(void)
{
	compact_weapon_fixture_t fixture;
	const uint32_t target_cells[] = { 1U, 2U };
	const float weights[] = { 0.01f, 0.99f };
	float rocket;
	float rail;

	InitFixture(&fixture);
	rocket = SG_CombatCompactWeaponFieldMass(&fixture.model, 0U,
		SG_WEAPON_PROFILE_ROCKET_LAUNCHER,
		SG_RUNE_WEAPON_RESPONSE_ROCKET_SPLASH, target_cells, weights,
		sizeof(weights) / sizeof(weights[0]));
	rail = SG_CombatCompactWeaponFieldMass(&fixture.model, 0U,
		SG_WEAPON_PROFILE_RAILGUN, SG_RUNE_WEAPON_RESPONSE_RAIL,
		target_cells, weights, sizeof(weights) / sizeof(weights[0]));
	CHECK(fabsf(rocket - 0.01f) < 0.0001f);
	CHECK(fabsf(rail - 0.99f) < 0.0001f);
	CHECK(rail > rocket);
}

static void TestSeparateEnemyLivesCannotDonateSupport(void)
{
	compact_weapon_fixture_t fixture;
	const uint32_t current_cells[] = { 1U, 2U };
	const float current_weights[] = { 0.99f, 0.01f };
	const uint32_t other_cells[] = { 1U, 2U };
	const float other_weights[] = { 0.01f, 0.99f };
	float current_rocket;
	float current_rail;
	float other_rail;

	InitFixture(&fixture);
	current_rocket = SG_CombatCompactWeaponFieldMass(&fixture.model, 0U,
		SG_WEAPON_PROFILE_ROCKET_LAUNCHER,
		SG_RUNE_WEAPON_RESPONSE_ROCKET_SPLASH, current_cells,
		current_weights, sizeof(current_weights) / sizeof(current_weights[0]));
	current_rail = SG_CombatCompactWeaponFieldMass(&fixture.model, 0U,
		SG_WEAPON_PROFILE_RAILGUN, SG_RUNE_WEAPON_RESPONSE_RAIL,
		current_cells, current_weights,
		sizeof(current_weights) / sizeof(current_weights[0]));
	other_rail = SG_CombatCompactWeaponFieldMass(&fixture.model, 0U,
		SG_WEAPON_PROFILE_RAILGUN, SG_RUNE_WEAPON_RESPONSE_RAIL,
		other_cells, other_weights,
		sizeof(other_weights) / sizeof(other_weights[0]));
	CHECK(fabsf(current_rocket - 0.99f) < 0.0001f);
	CHECK(fabsf(current_rail - 0.01f) < 0.0001f);
	CHECK(fabsf(other_rail - 0.99f) < 0.0001f);
	CHECK(current_rocket > current_rail);
}

int main(void)
{
	TestCertifiedSourceAttachment();
	TestClassIndexedRecordsDoNotDuplicateKernels();
	TestMalformedSpansAndLiveGateAreNotCandidates();
	TestStaleLawIdentityIsNotCandidate();
	TestWeightedLifeMassPrefersTheLikelyTarget();
	TestSeparateEnemyLivesCannotDonateSupport();
	return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
