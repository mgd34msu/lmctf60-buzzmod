#include <float.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <math.h>
#include <stddef.h>

#define SG_RUNE_COMPACT_LEARNING_OWNER_PRIVATE 1
#include "../slipgate/sg_rune_compact_learning_owner.h"
#undef SG_RUNE_COMPACT_LEARNING_OWNER_PRIVATE

static int failures;

#if defined(SG_RUNE_COMPACT_LEARNING_TEST_WRAP_ALLOC)
static int fail_calloc_after = -1;
static int fail_realloc_after = -1;

void *__real_calloc(size_t count, size_t size);
void *__real_realloc(void *pointer, size_t size);
void *__wrap_calloc(size_t count, size_t size);
void *__wrap_realloc(void *pointer, size_t size);

void *__wrap_calloc(size_t count, size_t size)
{
	if (fail_calloc_after == 0) {
		fail_calloc_after = -1;
		return NULL;
	}
	if (fail_calloc_after > 0)
		fail_calloc_after--;
	return __real_calloc(count, size);
}

void *__wrap_realloc(void *pointer, size_t size)
{
	if (fail_realloc_after == 0) {
		fail_realloc_after = -1;
		return NULL;
	}
	if (fail_realloc_after > 0)
		fail_realloc_after--;
	return __real_realloc(pointer, size);
}
#endif

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

typedef struct compact_fixture_s
{
	sg_rune_compact_cell_t cells[2];
	sg_rune_compact_facet_t facets[1];
	sg_rune_compact_incidence_t incidences[2];
	sg_rune_compact_incidence_index_t cell_incidences[2];
	sg_rune_q8_vec3_t vertices[5];
	sg_rune_compact_portal_t portals[1];
	sg_rune_movement_field_attachment_t movement_fields[2];
	sg_rune_weapon_response_region_t weapon_regions[2];
	sg_rune_weapon_profile_t weapon_profiles[12];
	sg_rune_weapon_response_kernel_t weapon_kernels[26];
	sg_rune_analytic_function_index_t analytic_function_refs[114];
	sg_rune_analytic_function_t analytic_functions[8];
	sg_rune_analytic_constant_t analytic_constants[8];
	sg_rune_compact_analytic_t analytic;
	sg_rune_compact_landmark_t landmarks[2];
	sg_rune_compact_cell_index_t landmark_cells[2];
	sg_rune_compact_facet_annotation_t facet_annotations[1];
	sg_rune_compact_static_t static_data;
	sg_rune_compact_model_t model;
} compact_fixture_t;

static sg_rune_compact_source_t BspPlaneSource(uint32_t plane)
{
	sg_rune_compact_source_t source;

	memset(&source, 0, sizeof(source));
	source.kind = SG_RUNE_COMPACT_SOURCE_BSP_PLANE;
	source.value.bsp_plane.model = 0U;
	source.value.bsp_plane.leaf = 1U;
	source.value.bsp_plane.plane = plane;
	return source;
}

static sg_rune_compact_source_t SplitSource(uint32_t parent,
	uint32_t ordinal)
{
	sg_rune_compact_source_t source;

	memset(&source, 0, sizeof(source));
	source.kind = SG_RUNE_COMPACT_SOURCE_SPLIT;
	source.value.split.parent_facet.value = parent;
	source.value.split.ordinal = ordinal;
	return source;
}

static uint32_t Bits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static void InitFixture(compact_fixture_t *fixture)
{
	sg_rune_compact_model_t *model;

	memset(fixture, 0, sizeof(*fixture));
	fixture->vertices[0] = (sg_rune_q8_vec3_t){ { 64, 0, 0 } };
	fixture->vertices[1] = (sg_rune_q8_vec3_t){ { 64, 64, 0 } };
	fixture->vertices[2] = (sg_rune_q8_vec3_t){ { 64, 64, 64 } };
	fixture->vertices[3] = (sg_rune_q8_vec3_t){ { 64, 0, 64 } };

	fixture->cells[0].source = (sg_rune_compact_cell_source_t){
		0U, 1U, 2U, 3, 0U
	};
	fixture->cells[0].bounds.mins =
		(sg_rune_q8_vec3_t){ { 0, 0, 0 } };
	fixture->cells[0].bounds.maxs =
		(sg_rune_q8_vec3_t){ { 64, 64, 64 } };
	fixture->cells[0].valid_stances = SG_RUNE_STANCE_VALID_STANDING |
		SG_RUNE_STANCE_VALID_CROUCHING;
	fixture->cells[0].incidences =
		(sg_rune_compact_cell_incidence_span_t){ 0U, 1U };
	fixture->cells[0].movement_fields =
		(sg_rune_movement_field_span_t){ 0U, 1U };
	fixture->cells[0].weapon_regions =
		(sg_rune_weapon_response_region_span_t){ 0U, 1U };

	fixture->cells[1] = fixture->cells[0];
	fixture->cells[1].source.leaf = 2U;
	fixture->cells[1].bounds.mins.value[0] = 64;
	fixture->cells[1].bounds.maxs.value[0] = 128;
	fixture->cells[1].incidences.first = 1U;
	fixture->cells[1].movement_fields.first = 1U;
	fixture->cells[1].weapon_regions =
		(sg_rune_weapon_response_region_span_t){ 1U, 1U };

	fixture->facets[0].source = BspPlaneSource(4U);
	fixture->facets[0].plane.normal_bits[0] = Bits(1.0f);
	fixture->facets[0].plane.normal_bits[1] = Bits(0.0f);
	fixture->facets[0].plane.normal_bits[2] = Bits(0.0f);
	fixture->facets[0].plane.distance_bits = Bits(8.0f);
	fixture->facets[0].vertices =
		(sg_rune_compact_vertex_span_t){ 0U, 4U };
	fixture->facets[0].incidences =
		(sg_rune_compact_incidence_span_t){ 0U, 2U };

	fixture->incidences[0].cell.value = 0U;
	fixture->incidences[0].facet.value = 0U;
	fixture->incidences[0].side = SG_RUNE_FACET_NEGATIVE_SIDE;
	fixture->incidences[0].boundary = SG_RUNE_BOUNDARY_CLOSED;
	fixture->incidences[1] = fixture->incidences[0];
	fixture->incidences[1].cell.value = 1U;
	fixture->incidences[1].side = SG_RUNE_FACET_POSITIVE_SIDE;
	fixture->incidences[1].boundary = SG_RUNE_BOUNDARY_OPEN;
	fixture->cell_incidences[0].value = 0U;
	fixture->cell_incidences[1].value = 1U;

	fixture->portals[0].source = SplitSource(0U, 0U);
	fixture->portals[0].facet.value = 0U;
	fixture->portals[0].negative_incidence.value = 0U;
	fixture->portals[0].positive_incidence.value = 1U;
	fixture->portals[0].valid_stances = SG_RUNE_STANCE_VALID_ALL;
	fixture->portals[0].direction = SG_RUNE_PORTAL_CONTINUITY_BOTH;
	fixture->portals[0].clearance_q8 = 32U;

	fixture->movement_fields[0].cell.value = 0U;
	fixture->movement_fields[0].boundary_portal.value = 0U;
	fixture->movement_fields[0].family = SG_RUNE_MOVEMENT_FIELD_HOOK;
	fixture->movement_fields[0].valid_stances = SG_RUNE_STANCE_VALID_ALL;
	fixture->movement_fields[0].functions =
		(sg_rune_analytic_function_span_t){ 0U, 3U };
	fixture->movement_fields[1] = fixture->movement_fields[0];
	fixture->movement_fields[1].cell.value = 1U;
	fixture->movement_fields[1].boundary_portal.value =
		SG_RUNE_COMPACT_INDEX_NONE;
	fixture->movement_fields[1].functions =
		(sg_rune_analytic_function_span_t){ 3U, 3U };
	fixture->analytic_function_refs[0].value = 0U;
	fixture->analytic_function_refs[1].value = 1U;
	fixture->analytic_function_refs[2].value = 5U;
	fixture->analytic_function_refs[3].value = 0U;
	fixture->analytic_function_refs[4].value = 1U;
	fixture->analytic_function_refs[5].value = 5U;

	fixture->weapon_regions[0].cell.value = 0U;
	fixture->weapon_regions[0].boundary_incidences =
		(sg_rune_compact_cell_incidence_span_t){ 0U, 1U };
	fixture->weapon_regions[0].kernels =
		(sg_rune_weapon_response_kernel_span_t){ 0U, 13U };
	fixture->weapon_regions[1] = fixture->weapon_regions[0];
	fixture->weapon_regions[1].cell.value = 1U;
	fixture->weapon_regions[1].boundary_incidences =
		(sg_rune_compact_cell_incidence_span_t){ 1U, 1U };
	fixture->weapon_regions[1].kernels =
		(sg_rune_weapon_response_kernel_span_t){ 13U, 13U };
	{
		uint32_t reference_cursor = 6U;
		uint32_t kernel_cursor = 0U;
		uint32_t region;
		uint32_t profile;

		for (profile = 0U; profile < 12U; profile++) {
			fixture->weapon_profiles[profile].source_profile = profile + 1U;
			fixture->weapon_profiles[profile].response_families =
				SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(profile);
		}
		fixture->weapon_profiles[SG_RUNE_WEAPON_RESPONSE_ROCKET_IMPACT].
			response_families |= SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(
				SG_RUNE_WEAPON_RESPONSE_ROCKET_SPLASH);

		for (region = 0U; region < 2U; region++) {
			uint32_t profile_index;

			for (profile_index = 0U; profile_index < 12U; profile_index++) {
				uint32_t family;

				for (family = 0U;
					family < (uint32_t)SG_RUNE_WEAPON_RESPONSE_FAMILY_COUNT;
					family++) {
					const int grenade =
						family == SG_RUNE_WEAPON_RESPONSE_GRENADE_FLIGHT ||
						family == SG_RUNE_WEAPON_RESPONSE_GRENADE_BOUNCE_FUSE;
					const uint32_t bit =
						SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(family);

					if ((fixture->weapon_profiles[profile_index].response_families &
						bit) == 0U)
						continue;
					fixture->weapon_kernels[kernel_cursor].region.value = region;
					fixture->weapon_kernels[kernel_cursor].profile = profile_index;
					fixture->weapon_kernels[kernel_cursor].family =
						(sg_rune_weapon_response_family_t)family;
					fixture->weapon_kernels[kernel_cursor].functions.first =
						reference_cursor;
					fixture->weapon_kernels[kernel_cursor].functions.count =
						grenade ? 5U : 4U;
					fixture->analytic_function_refs[reference_cursor++].value = 1U;
					fixture->analytic_function_refs[reference_cursor++].value = 2U;
					fixture->analytic_function_refs[reference_cursor++].value = 3U;
					fixture->analytic_function_refs[reference_cursor++].value = 4U;
					if (grenade)
						fixture->analytic_function_refs[reference_cursor++].value = 6U;
					kernel_cursor++;
				}
			}
		}
	}

	for (uint32_t index = 0U; index < 7U; index++) {
		fixture->analytic_functions[index].definition = index;
		fixture->analytic_functions[index].form =
			SG_RUNE_COMPACT_ANALYTIC_CONSTANT;
		fixture->analytic_constants[index].value.bits = Bits((float)index + 1.0f);
	}
	fixture->analytic_functions[0].output = SG_RUNE_ANALYTIC_OUTPUT_COST;
	fixture->analytic_functions[1].output =
		SG_RUNE_ANALYTIC_OUTPUT_TRAVEL_TIME_SECONDS;
	fixture->analytic_functions[2].output = SG_RUNE_ANALYTIC_OUTPUT_DAMAGE;
	fixture->analytic_functions[3].output =
		SG_RUNE_ANALYTIC_OUTPUT_HIT_PROBABILITY;
	fixture->analytic_functions[4].output =
		SG_RUNE_ANALYTIC_OUTPUT_VISIBILITY_FRACTION;
	fixture->analytic_functions[5].output =
		SG_RUNE_ANALYTIC_OUTPUT_REACHABILITY_MARGIN;
	fixture->analytic_functions[6].output =
		SG_RUNE_ANALYTIC_OUTPUT_FUSE_REMAINING_SECONDS;
	fixture->analytic.version = SG_RUNE_COMPACT_ANALYTIC_VERSION;
	fixture->analytic.functions = fixture->analytic_functions;
	fixture->analytic.function_count = 7U;
	fixture->analytic.constants = fixture->analytic_constants;
	fixture->analytic.constant_count = 7U;

	fixture->landmarks[0].source.entity_ordinal = 20U;
	fixture->landmarks[0].cells =
		(sg_rune_compact_landmark_cell_span_t){ 0U, 1U };
	fixture->landmarks[0].mechanism.value = SG_RUNE_COMPACT_INDEX_NONE;
	fixture->landmarks[0].origin = (sg_rune_q8_vec3_t){ { 16, 16, 8 } };
	fixture->landmarks[0].bounds.mins =
		(sg_rune_q8_vec3_t){ { 8, 8, 0 } };
	fixture->landmarks[0].bounds.maxs =
		(sg_rune_q8_vec3_t){ { 24, 24, 16 } };
	fixture->landmarks[0].kind = SG_RUNE_COMPACT_LANDMARK_FLAG;
	fixture->landmarks[1] = fixture->landmarks[0];
	fixture->landmarks[1].source.entity_ordinal = 21U;
	fixture->landmarks[1].cells =
		(sg_rune_compact_landmark_cell_span_t){ 1U, 1U };
	fixture->landmarks[1].origin.value[0] = 80;
	fixture->landmarks[1].bounds.mins.value[0] = 72;
	fixture->landmarks[1].bounds.maxs.value[0] = 88;
	fixture->landmark_cells[0].value = 0U;
	fixture->landmark_cells[1].value = 1U;
	fixture->facet_annotations[0].facet.value = 0U;
	fixture->facet_annotations[0].attributes = SG_RUNE_COMPACT_FACET_HOOKABLE;
	fixture->facet_annotations[0].hookable_stances =
		SG_RUNE_STANCE_VALID_STANDING | SG_RUNE_STANCE_VALID_CROUCHING;
	fixture->static_data.landmarks = fixture->landmarks;
	fixture->static_data.landmark_count = 2U;
	fixture->static_data.landmark_cells = fixture->landmark_cells;
	fixture->static_data.landmark_cell_count = 2U;
	fixture->static_data.facet_annotations = fixture->facet_annotations;
	fixture->static_data.facet_annotation_count = 1U;

	model = &fixture->model;
	model->version = SG_RUNE_COMPACT_MODEL_VERSION;
	model->schema_tag = SG_RUNE_COMPACT_MODEL_SCHEMA_TAG;
	model->identity.bsp_sha256[0] = UINT8_C(0x5a);
	model->identity.bsp_bytes = UINT64_C(1024);
	model->identity.bsp_checksum = UINT32_C(0x101);
	model->identity.entity_crc32 = UINT32_C(0x102);
	model->identity.entity_semantics_id = UINT64_C(0x202);
	model->identity.physics_abi_id = UINT64_C(0x303);
	model->identity.collision_law_id = UINT64_C(0x3031);
	model->identity.pmove_law_id = UINT64_C(0x3032);
	model->identity.gravity_law_id = UINT64_C(0x3033);
	model->identity.hook_law_id = UINT64_C(0x3034);
	model->identity.mechanism_law_id = UINT64_C(0x304);
	model->identity.weapon_law_id = UINT64_C(0x305);
	model->identity.construction_id = UINT64_C(0x306);
	model->identity.schema_id = UINT64_C(0x404);
	model->identity.producer_identity = UINT64_C(0x50524f4455434552);
	model->identity.source_counts = (sg_rune_compact_source_counts_t){
		1U, 3U, 4U, 5U, 1U, 1U, 32U
	};
	model->identity.standing_hull.mins =
		(sg_rune_q8_vec3_t){ { -128, -128, -192 } };
	model->identity.standing_hull.maxs =
		(sg_rune_q8_vec3_t){ { 128, 128, 256 } };
	model->identity.crouching_hull.mins =
		(sg_rune_q8_vec3_t){ { -128, -128, -192 } };
	model->identity.crouching_hull.maxs =
		(sg_rune_q8_vec3_t){ { 128, 128, 128 } };
	model->identity.physics.gravity_bits = Bits(100.0f);
	model->identity.physics.ground_acceleration_bits = Bits(10.0f);
	model->identity.physics.air_acceleration_bits = Bits(1.0f);
	model->identity.physics.water_acceleration_bits = Bits(4.0f);
	model->identity.physics.hook_acceleration_bits = Bits(1000.0f);
	model->identity.physics.external_acceleration_bits = Bits(1200.0f);
	model->identity.physics.water_drag_bits = Bits(0.5f);
	model->identity.physics.max_velocity_bits = Bits(800.0f);
	model->identity.physics.frame_ms = 8U;
	model->identity.physics.substep_ms = 1U;
	model->cells = fixture->cells;
	model->cell_count = 2U;
	model->facets = fixture->facets;
	model->facet_count = 1U;
	model->incidences = fixture->incidences;
	model->incidence_count = 2U;
	model->cell_incidences = fixture->cell_incidences;
	model->cell_incidence_count = 2U;
	model->vertices = fixture->vertices;
	model->vertex_count = 4U;
	model->portals = fixture->portals;
	model->portal_count = 1U;
	model->movement_fields = fixture->movement_fields;
	model->movement_field_count = 2U;
	model->weapon_regions = fixture->weapon_regions;
	model->weapon_region_count = 2U;
	model->weapon_profiles = fixture->weapon_profiles;
	model->weapon_profile_count = 12U;
	model->weapon_kernels = fixture->weapon_kernels;
	model->weapon_kernel_count = 26U;
	model->analytic_function_refs = fixture->analytic_function_refs;
	model->analytic_function_ref_count = 114U;
	model->analytic = &fixture->analytic;
	model->static_data = &fixture->static_data;
}

static sg_rune_compact_learning_claim_t Claim(
	const compact_fixture_t *fixture, sg_rune_compact_learning_kind_t kind)
{
	sg_rune_compact_learning_claim_t claim;

	(void)fixture;
	memset(&claim, 0, sizeof(claim));
	claim.key.kind = kind;
	switch (kind) {
	case SG_RUNE_COMPACT_LEARNING_LOCAL_TRAVERSAL:
		claim.key.value.traversal.source_cell.value = 0U;
		claim.key.value.traversal.target_cell.value = 1U;
		claim.key.value.traversal.portal.value = 0U;
		claim.key.value.traversal.movement_field = 0U;
		claim.key.value.traversal.stance = SG_RUNE_STANCE_VALID_STANDING;
		claim.value = 12.5f;
		break;
	case SG_RUNE_COMPACT_LEARNING_LANDING:
		claim.key.value.landing.source_cell.value = 0U;
		claim.key.value.landing.target_cell.value = 1U;
		claim.key.value.landing.portal.value = 0U;
		claim.key.value.landing.movement_field = 0U;
		claim.key.value.landing.stance = SG_RUNE_STANCE_VALID_STANDING;
		claim.value = 0.75f;
		break;
	case SG_RUNE_COMPACT_LEARNING_TACTIC:
		claim.key.value.tactic.cell.value = 0U;
		claim.key.value.tactic.weapon_kernel = 0U;
		claim.value = 0.25f;
		break;
	case SG_RUNE_COMPACT_LEARNING_STRATEGY:
		claim.key.value.strategy.cell.value = 0U;
		claim.key.value.strategy.landmark.value = 0U;
		claim.value = 1.0f;
		break;
	case SG_RUNE_COMPACT_LEARNING_KIND_COUNT:
		break;
	}
	return claim;
}

static sg_rune_compact_learning_t *CreateLearning(
	const compact_fixture_t *fixture)
{
	sg_rune_compact_learning_t *learning = NULL;
	sg_rune_compact_error_t error;

	CHECK(SG_RuneCompactLearningCreate(&fixture->model,
		&fixture->model.identity, &learning, &error) ==
		SG_RUNE_COMPACT_LEARNING_OK);
	CHECK(learning != NULL);
	return learning;
}

static sg_rune_compact_learning_issuer_t *CreateIssuer(
	const compact_fixture_t *fixture, int bot)
{
	sg_rune_compact_learning_issuer_t *issuer = NULL;
	sg_rune_compact_error_t error;

	CHECK((bot ? SG_RuneCompactLearningIssuerAcquireBot(&fixture->model,
		&fixture->model.identity, &issuer, &error) :
		SG_RuneCompactLearningIssuerAcquireHuman(&fixture->model,
		&fixture->model.identity, &issuer, &error)) ==
		SG_RUNE_COMPACT_LEARNING_OK);
	CHECK(issuer != NULL);
	return issuer;
}

static sg_rune_compact_learning_observation_t *Issue(
	const sg_rune_compact_learning_issuer_t *issuer,
	const sg_rune_compact_learning_claim_t *claim)
{
	sg_rune_compact_learning_observation_t *observation = NULL;

	CHECK(SG_RuneCompactLearningIssuerIssue(issuer, claim, &observation) ==
		SG_RUNE_COMPACT_LEARNING_OK);
	CHECK(observation != NULL);
	return observation;
}

static sg_rune_compact_learning_status_t ApplyIssued(
	sg_rune_compact_learning_t *learning,
	sg_rune_compact_learning_observation_t *observation,
	sg_rune_compact_learning_prior_t *prior_out)
{
	sg_rune_compact_learning_status_t status = SG_RuneCompactLearningApply(
		learning, observation, prior_out);

	SG_RuneCompactLearningObservationDestroy(observation);
	return status;
}

static sg_rune_compact_learning_prior_t ReadPrior(
	const sg_rune_compact_learning_t *learning, uint32_t index)
{
	sg_rune_compact_learning_prior_t prior;

	memset(&prior, 0, sizeof(prior));
	CHECK(SG_RuneCompactLearningPriorRead(learning, index, &prior));
	return prior;
}

static int PriorEqual(const sg_rune_compact_learning_prior_t *left,
	const sg_rune_compact_learning_prior_t *right)
{
	return memcmp(&left->key, &right->key, sizeof(left->key)) == 0 &&
		left->value_total_q16 == right->value_total_q16 &&
		left->human_samples == right->human_samples &&
		left->bot_samples == right->bot_samples;
}

static void TestVerifiedUpdates(void)
{
	compact_fixture_t fixture;
	sg_rune_compact_learning_t *learning;
	sg_rune_compact_learning_issuer_t *human;
	sg_rune_compact_learning_issuer_t *bot;
	sg_rune_compact_learning_claim_t claim;
	sg_rune_compact_learning_prior_t prior;

	InitFixture(&fixture);
	learning = CreateLearning(&fixture);
	human = CreateIssuer(&fixture, 0);
	bot = CreateIssuer(&fixture, 1);
	claim = Claim(&fixture, SG_RUNE_COMPACT_LEARNING_LOCAL_TRAVERSAL);
	CHECK(ApplyIssued(learning, Issue(human, &claim), &prior) ==
		SG_RUNE_COMPACT_LEARNING_OK);
	CHECK(prior.value_total_q16 == UINT64_C(819200));
	CHECK(prior.human_samples == 1U);
	CHECK(prior.bot_samples == 0U);

	claim = Claim(&fixture, SG_RUNE_COMPACT_LEARNING_LANDING);
	CHECK(ApplyIssued(learning, Issue(bot, &claim), &prior) ==
		SG_RUNE_COMPACT_LEARNING_OK);
	CHECK(prior.value_total_q16 == UINT64_C(49152));
	CHECK(prior.human_samples == 0U);
	CHECK(prior.bot_samples == 1U);

	claim = Claim(&fixture, SG_RUNE_COMPACT_LEARNING_TACTIC);
	CHECK(ApplyIssued(learning, Issue(human, &claim), &prior) ==
		SG_RUNE_COMPACT_LEARNING_OK);
	CHECK(prior.value_total_q16 == UINT64_C(16384));

	claim = Claim(&fixture, SG_RUNE_COMPACT_LEARNING_STRATEGY);
	CHECK(ApplyIssued(learning, Issue(human, &claim), &prior) ==
		SG_RUNE_COMPACT_LEARNING_OK);
	CHECK(prior.value_total_q16 == UINT64_C(65536));
	CHECK(SG_RuneCompactLearningPriorCount(learning) == 4U);
	CHECK(!SG_RuneCompactLearningPriorRead(learning, 4U, &prior));
	prior = ReadPrior(learning, 0U);
	CHECK(prior.key.kind ==
		SG_RUNE_COMPACT_LEARNING_LOCAL_TRAVERSAL);
	prior = ReadPrior(learning, 3U);
	CHECK(prior.key.kind ==
		SG_RUNE_COMPACT_LEARNING_STRATEGY);
	SG_RuneCompactLearningIssuerDestroy(bot);
	SG_RuneCompactLearningIssuerDestroy(human);
	SG_RuneCompactLearningDestroy(learning);
}

static void TestInventedGeometryRejected(void)
{
	compact_fixture_t fixture;
	sg_rune_compact_learning_t *learning;
	sg_rune_compact_learning_issuer_t *issuer;
	sg_rune_compact_learning_claim_t claim;
	sg_rune_compact_learning_observation_t *unchanged_observation =
		(sg_rune_compact_learning_observation_t *)(void *)&fixture;
	sg_rune_compact_learning_prior_t prior;
	sg_rune_compact_learning_prior_t prior_before;

	InitFixture(&fixture);
	learning = CreateLearning(&fixture);
	issuer = CreateIssuer(&fixture, 0);
	memset(&prior, 0xa5, sizeof(prior));
	prior_before = prior;
	claim = Claim(&fixture, SG_RUNE_COMPACT_LEARNING_LOCAL_TRAVERSAL);
	claim.key.value.traversal.target_cell.value = 0U;
	CHECK(SG_RuneCompactLearningIssuerIssue(issuer, &claim,
		&unchanged_observation) ==
		SG_RUNE_COMPACT_LEARNING_INVALID_REFERENCE);
	CHECK(unchanged_observation ==
		(sg_rune_compact_learning_observation_t *)(void *)&fixture);
	CHECK(SG_RuneCompactLearningPriorCount(learning) == 0U);
	CHECK(memcmp(&prior, &prior_before, sizeof(prior)) == 0);

	claim = Claim(&fixture, SG_RUNE_COMPACT_LEARNING_LANDING);
	claim.key.value.landing.movement_field = 1U;
	CHECK(SG_RuneCompactLearningIssuerIssue(issuer, &claim,
		&unchanged_observation) ==
		SG_RUNE_COMPACT_LEARNING_INVALID_REFERENCE);
	claim = Claim(&fixture, SG_RUNE_COMPACT_LEARNING_TACTIC);
	claim.key.value.tactic.cell.value = 1U;
	CHECK(SG_RuneCompactLearningIssuerIssue(issuer, &claim,
		&unchanged_observation) ==
		SG_RUNE_COMPACT_LEARNING_INVALID_REFERENCE);
	claim = Claim(&fixture, SG_RUNE_COMPACT_LEARNING_STRATEGY);
	claim.key.value.strategy.cell.value = 1U;
	CHECK(SG_RuneCompactLearningIssuerIssue(issuer, &claim,
		&unchanged_observation) ==
		SG_RUNE_COMPACT_LEARNING_INVALID_REFERENCE);
	CHECK(SG_RuneCompactLearningPriorCount(learning) == 0U);
	SG_RuneCompactLearningIssuerDestroy(issuer);
	SG_RuneCompactLearningDestroy(learning);
}

static void TestIdentityBoundIssuer(void)
{
	compact_fixture_t fixture;
	sg_rune_compact_learning_t *learning;
	sg_rune_compact_learning_t *unchanged = (sg_rune_compact_learning_t *)(void *)&fixture;
	sg_rune_compact_learning_issuer_t *issuer;
	sg_rune_compact_learning_issuer_t *unchanged_issuer =
		(sg_rune_compact_learning_issuer_t *)(void *)&fixture;
	sg_rune_compact_learning_observation_t *observation;
	sg_rune_compact_learning_observation_t *unchanged_observation =
		(sg_rune_compact_learning_observation_t *)(void *)&fixture;
	sg_rune_compact_learning_claim_t claim;
	sg_rune_compact_learning_prior_t prior;
	sg_rune_compact_learning_prior_t prior_before;
	sg_rune_compact_identity_t wrong_identity;
	sg_rune_compact_error_t error;

	InitFixture(&fixture);
	wrong_identity = fixture.model.identity;
	wrong_identity.hook_law_id ^= UINT64_C(1);
	CHECK(SG_RuneCompactLearningCreate(&fixture.model, &wrong_identity,
		&unchanged, &error) == SG_RUNE_COMPACT_LEARNING_IDENTITY_MISMATCH);
	CHECK(unchanged == (sg_rune_compact_learning_t *)(void *)&fixture);
	CHECK(SG_RuneCompactLearningIssuerAcquireHuman(&fixture.model,
		&wrong_identity, &unchanged_issuer, &error) ==
		SG_RUNE_COMPACT_LEARNING_IDENTITY_MISMATCH);
	CHECK(unchanged_issuer ==
		(sg_rune_compact_learning_issuer_t *)(void *)&fixture);

	learning = CreateLearning(&fixture);
	issuer = CreateIssuer(&fixture, 0);
	claim = Claim(&fixture, SG_RUNE_COMPACT_LEARNING_LOCAL_TRAVERSAL);
	observation = Issue(issuer, &claim);
	memset(&prior, 0xa5, sizeof(prior));
	prior_before = prior;
	fixture.model.identity.hook_law_id ^= UINT64_C(1);
	CHECK(SG_RuneCompactLearningIssuerIssue(issuer, &claim,
		&unchanged_observation) == SG_RUNE_COMPACT_LEARNING_IDENTITY_MISMATCH);
	CHECK(unchanged_observation ==
		(sg_rune_compact_learning_observation_t *)(void *)&fixture);
	CHECK(SG_RuneCompactLearningApply(learning, observation, &prior) ==
		SG_RUNE_COMPACT_LEARNING_IDENTITY_MISMATCH);
	CHECK(memcmp(&prior, &prior_before, sizeof(prior)) == 0);
	SG_RuneCompactLearningObservationDestroy(observation);
	SG_RuneCompactLearningIssuerDestroy(issuer);
	SG_RuneCompactLearningDestroy(learning);
}

static void TestMalformedValuesRejected(void)
{
	compact_fixture_t fixture;
	sg_rune_compact_learning_t *learning;
	sg_rune_compact_learning_issuer_t *human;
	sg_rune_compact_learning_issuer_t *bot;
	sg_rune_compact_learning_claim_t claim;
	sg_rune_compact_learning_observation_t *observation;
	sg_rune_compact_learning_prior_t prior;

	InitFixture(&fixture);
	learning = CreateLearning(&fixture);
	human = CreateIssuer(&fixture, 0);
	bot = CreateIssuer(&fixture, 1);
	claim = Claim(&fixture, SG_RUNE_COMPACT_LEARNING_TACTIC);
	claim.value = NAN;
	CHECK(SG_RuneCompactLearningIssuerIssue(human, &claim,
		&observation) ==
		SG_RUNE_COMPACT_LEARNING_INVALID_VALUE);
	claim.value = INFINITY;
	CHECK(SG_RuneCompactLearningIssuerIssue(human, &claim,
		&observation) ==
		SG_RUNE_COMPACT_LEARNING_INVALID_VALUE);
	claim.value = -0.25f;
	CHECK(SG_RuneCompactLearningIssuerIssue(human, &claim,
		&observation) ==
		SG_RUNE_COMPACT_LEARNING_INVALID_VALUE);
	claim.value = FLT_MAX;
	CHECK(SG_RuneCompactLearningIssuerIssue(human, &claim,
		&observation) ==
		SG_RUNE_COMPACT_LEARNING_INVALID_VALUE);
	claim.key.kind = SG_RUNE_COMPACT_LEARNING_KIND_COUNT;
	CHECK(SG_RuneCompactLearningIssuerIssue(human, &claim,
		&observation) ==
		SG_RUNE_COMPACT_LEARNING_INVALID_OBSERVATION);
	claim.key.kind = (sg_rune_compact_learning_kind_t)-1;
	CHECK(SG_RuneCompactLearningIssuerIssue(human, &claim,
		&observation) ==
		SG_RUNE_COMPACT_LEARNING_INVALID_OBSERVATION);
	claim = Claim(&fixture, SG_RUNE_COMPACT_LEARNING_TACTIC);
	claim.value = 200000000000000.0f;
	CHECK(ApplyIssued(learning, Issue(human, &claim), &prior) ==
		SG_RUNE_COMPACT_LEARNING_OK);
	CHECK(ApplyIssued(learning, Issue(bot, &claim), &prior) ==
		SG_RUNE_COMPACT_LEARNING_OK);
	CHECK(prior.value_total_q16 == UINT64_MAX);
	CHECK(prior.human_samples == 1U);
	CHECK(prior.bot_samples == 1U);
	claim = Claim(&fixture, SG_RUNE_COMPACT_LEARNING_LOCAL_TRAVERSAL);
	claim.value = 0x1p48f;
	observation = (sg_rune_compact_learning_observation_t *)(void *)&fixture;
	CHECK(SG_RuneCompactLearningIssuerIssue(human, &claim, &observation) ==
		SG_RUNE_COMPACT_LEARNING_INVALID_VALUE);
	CHECK(observation ==
		(sg_rune_compact_learning_observation_t *)(void *)&fixture);
	claim.value = 0x1.fffffep47f;
	CHECK(ApplyIssued(learning, Issue(human, &claim), &prior) ==
		SG_RUNE_COMPACT_LEARNING_OK);
	CHECK(prior.value_total_q16 != UINT64_MAX);
	SG_RuneCompactLearningIssuerDestroy(bot);
	SG_RuneCompactLearningIssuerDestroy(human);
	SG_RuneCompactLearningDestroy(learning);
}

static void TestOrderIndependentMerge(void)
{
	compact_fixture_t fixture;
	sg_rune_compact_learning_t *left;
	sg_rune_compact_learning_t *right;
	sg_rune_compact_learning_t *forward;
	sg_rune_compact_learning_t *reverse;
	sg_rune_compact_learning_issuer_t *human;
	sg_rune_compact_learning_issuer_t *bot;
	sg_rune_compact_learning_claim_t claims[4];
	uint32_t index;

	InitFixture(&fixture);
	claims[0] = Claim(&fixture,
		SG_RUNE_COMPACT_LEARNING_LOCAL_TRAVERSAL);
	claims[0].value = 3.0f;
	claims[1] = claims[0];
	claims[1].value = 5.0f;
	claims[2] = Claim(&fixture, SG_RUNE_COMPACT_LEARNING_TACTIC);
	claims[2].value = 0.5f;
	claims[3] = Claim(&fixture, SG_RUNE_COMPACT_LEARNING_STRATEGY);
	claims[3].value = 0.25f;
	human = CreateIssuer(&fixture, 0);
	bot = CreateIssuer(&fixture, 1);
	left = CreateLearning(&fixture);
	right = CreateLearning(&fixture);
	forward = CreateLearning(&fixture);
	reverse = CreateLearning(&fixture);
	CHECK(ApplyIssued(left, Issue(human, &claims[0]),
		&(sg_rune_compact_learning_prior_t){ 0 }) == SG_RUNE_COMPACT_LEARNING_OK);
	CHECK(ApplyIssued(left, Issue(human, &claims[2]),
		&(sg_rune_compact_learning_prior_t){ 0 }) == SG_RUNE_COMPACT_LEARNING_OK);
	CHECK(ApplyIssued(right, Issue(bot, &claims[1]),
		&(sg_rune_compact_learning_prior_t){ 0 }) == SG_RUNE_COMPACT_LEARNING_OK);
	CHECK(ApplyIssued(right, Issue(bot, &claims[3]),
		&(sg_rune_compact_learning_prior_t){ 0 }) == SG_RUNE_COMPACT_LEARNING_OK);
	CHECK(SG_RuneCompactLearningMerge(forward, left) ==
		SG_RUNE_COMPACT_LEARNING_OK);
	CHECK(SG_RuneCompactLearningMerge(forward, right) ==
		SG_RUNE_COMPACT_LEARNING_OK);
	CHECK(SG_RuneCompactLearningMerge(reverse, right) ==
		SG_RUNE_COMPACT_LEARNING_OK);
	CHECK(SG_RuneCompactLearningMerge(reverse, left) ==
		SG_RUNE_COMPACT_LEARNING_OK);
	CHECK(SG_RuneCompactLearningPriorCount(forward) == 3U);
	CHECK(SG_RuneCompactLearningPriorCount(reverse) == 3U);
	for (index = 0U; index < 3U; index++) {
		const sg_rune_compact_learning_prior_t forward_prior =
			ReadPrior(forward, index);
		const sg_rune_compact_learning_prior_t reverse_prior =
			ReadPrior(reverse, index);

		CHECK(PriorEqual(&forward_prior, &reverse_prior));
	}
	{
		const sg_rune_compact_learning_prior_t prior = ReadPrior(forward, 0U);

		CHECK(prior.value_total_q16 ==
		UINT64_C(524288));
		CHECK(prior.human_samples == 1U);
		CHECK(prior.bot_samples == 1U);
	}
	SG_RuneCompactLearningDestroy(reverse);
	SG_RuneCompactLearningDestroy(forward);
	SG_RuneCompactLearningDestroy(right);
	SG_RuneCompactLearningDestroy(left);
	SG_RuneCompactLearningIssuerDestroy(bot);
	SG_RuneCompactLearningIssuerDestroy(human);
}

static void TestCanonicalKeysAndCopiedReads(void)
{
	compact_fixture_t fixture;
	sg_rune_compact_learning_t *clean_learning;
	sg_rune_compact_learning_t *dirty_learning;
	sg_rune_compact_learning_issuer_t *issuer;
	sg_rune_compact_learning_claim_t clean;
	sg_rune_compact_learning_claim_t dirty;
	sg_rune_compact_learning_prior_t clean_prior;
	sg_rune_compact_learning_prior_t dirty_prior;
	sg_rune_compact_learning_prior_t unchanged;
	sg_rune_compact_learning_prior_t unchanged_before;

	InitFixture(&fixture);
	issuer = CreateIssuer(&fixture, 0);
	clean = Claim(&fixture, SG_RUNE_COMPACT_LEARNING_TACTIC);
	memset(&dirty, 0xa5, sizeof(dirty));
	dirty.key.kind = SG_RUNE_COMPACT_LEARNING_TACTIC;
	dirty.key.value.tactic.cell.value = 0U;
	dirty.key.value.tactic.weapon_kernel = 0U;
	dirty.value = 0.25f;
	clean_learning = CreateLearning(&fixture);
	dirty_learning = CreateLearning(&fixture);
	CHECK(ApplyIssued(clean_learning, Issue(issuer, &clean), &clean_prior) ==
		SG_RUNE_COMPACT_LEARNING_OK);
	CHECK(ApplyIssued(dirty_learning, Issue(issuer, &dirty), &dirty_prior) ==
		SG_RUNE_COMPACT_LEARNING_OK);
	clean_prior = ReadPrior(clean_learning, 0U);
	dirty_prior = ReadPrior(dirty_learning, 0U);
	CHECK(memcmp(&clean_prior.key, &dirty_prior.key,
		sizeof(clean_prior.key)) == 0);
	CHECK(memcmp(&clean_prior, &dirty_prior, sizeof(clean_prior)) == 0);
	memset(&unchanged, 0x5a, sizeof(unchanged));
	unchanged_before = unchanged;
	CHECK(!SG_RuneCompactLearningPriorRead(clean_learning, 1U, &unchanged));
	CHECK(memcmp(&unchanged, &unchanged_before, sizeof(unchanged)) == 0);
	SG_RuneCompactLearningDestroy(dirty_learning);
	SG_RuneCompactLearningDestroy(clean_learning);
	CHECK(clean_prior.key.kind == SG_RUNE_COMPACT_LEARNING_TACTIC);
	CHECK(clean_prior.value_total_q16 == UINT64_C(16384));
	SG_RuneCompactLearningIssuerDestroy(issuer);
}

#if defined(SG_RUNE_COMPACT_LEARNING_TEST_WRAP_ALLOC)
static void TestAllocationFailureTransaction(void)
{
	compact_fixture_t fixture;
	sg_rune_compact_learning_t *learning;
	sg_rune_compact_learning_t *unchanged = (sg_rune_compact_learning_t *)(void *)&fixture;
	sg_rune_compact_learning_issuer_t *issuer;
	sg_rune_compact_learning_observation_t *observation;
	sg_rune_compact_learning_claim_t claim;
	sg_rune_compact_learning_prior_t prior;
	sg_rune_compact_learning_prior_t prior_before;
	sg_rune_compact_error_t error;

	InitFixture(&fixture);
	fail_calloc_after = 1;
	CHECK(SG_RuneCompactLearningCreate(&fixture.model,
		&fixture.model.identity, &unchanged, &error) ==
		SG_RUNE_COMPACT_LEARNING_ALLOCATION_FAILED);
	CHECK(unchanged == (sg_rune_compact_learning_t *)(void *)&fixture);
	fail_calloc_after = -1;
	learning = CreateLearning(&fixture);
	issuer = CreateIssuer(&fixture, 0);
	claim = Claim(&fixture, SG_RUNE_COMPACT_LEARNING_LOCAL_TRAVERSAL);
	observation = (sg_rune_compact_learning_observation_t *)(void *)&fixture;
	fail_calloc_after = 0;
	CHECK(SG_RuneCompactLearningIssuerIssue(issuer, &claim, &observation) ==
		SG_RUNE_COMPACT_LEARNING_ALLOCATION_FAILED);
	CHECK(observation ==
		(sg_rune_compact_learning_observation_t *)(void *)&fixture);
	fail_calloc_after = -1;
	observation = Issue(issuer, &claim);
	memset(&prior, 0x5a, sizeof(prior));
	prior_before = prior;
	fail_realloc_after = 0;
	CHECK(SG_RuneCompactLearningApply(learning, observation, &prior) ==
		SG_RUNE_COMPACT_LEARNING_ALLOCATION_FAILED);
	CHECK(SG_RuneCompactLearningPriorCount(learning) == 0U);
	CHECK(memcmp(&prior, &prior_before, sizeof(prior)) == 0);
	fail_realloc_after = -1;
	SG_RuneCompactLearningObservationDestroy(observation);
	SG_RuneCompactLearningIssuerDestroy(issuer);
	SG_RuneCompactLearningDestroy(learning);
}
#endif

int main(void)
{
	TestVerifiedUpdates();
	TestInventedGeometryRejected();
	TestIdentityBoundIssuer();
	TestMalformedValuesRejected();
	TestOrderIndependentMerge();
	TestCanonicalKeysAndCopiedReads();
#if defined(SG_RUNE_COMPACT_LEARNING_TEST_WRAP_ALLOC)
	TestAllocationFailureTransaction();
#endif
	if (failures != 0) {
		fprintf(stderr, "%d compact RUNE learning checks failed\n", failures);
		return 1;
	}
	puts("compact RUNE learning checks passed");
	return 0;
}
