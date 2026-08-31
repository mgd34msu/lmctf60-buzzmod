#include "slipgate/sg_rune_compact_wire.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_HEADER_FIXED UINT32_C(48)
#define TEST_DESCRIPTOR_SIZE UINT32_C(24)
#define TEST_CHECKSUM_OFFSET UINT32_C(24)

#define CHECK(condition) \
	do { \
		if (!(condition)) { \
			fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
				__FILE__, __LINE__, #condition); \
			return 0; \
		} \
	} while (0)

typedef struct test_fixture_s
{
	sg_rune_compact_model_t model;
	sg_rune_compact_analytic_t analytic;
	sg_rune_compact_static_t static_data;
	sg_rune_compact_cell_t cells[2];
	sg_rune_compact_facet_t facets[1];
	sg_rune_compact_incidence_t incidences[2];
	sg_rune_compact_incidence_index_t cell_incidences[2];
	sg_rune_q8_vec3_t vertices[4];
	sg_rune_compact_portal_t portals[1];
	sg_rune_movement_field_attachment_t movement_fields[2];
	sg_rune_weapon_response_region_t weapon_regions[2];
	sg_rune_weapon_profile_t weapon_profiles[12];
	sg_rune_weapon_response_kernel_t weapon_kernels[26];
	sg_rune_analytic_function_index_t analytic_refs[114];
	sg_rune_analytic_function_t functions[7];
	sg_rune_analytic_constant_t constants[7];
	sg_rune_compact_mechanism_t mechanisms[2];
	sg_rune_compact_mechanism_edge_t mechanism_edges[1];
	sg_rune_compact_landmark_t landmarks[2];
	sg_rune_compact_cell_index_t landmark_cells[2];
	sg_rune_compact_facet_annotation_t annotations[1];
	sg_rune_compact_portal_mechanism_t portal_mechanisms[1];
} test_fixture_t;

static uint32_t float_bits(float value)
{
	uint32_t bits;
	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static sg_rune_compact_source_t bsp_plane_source(uint32_t plane)
{
	sg_rune_compact_source_t source;
	memset(&source, 0, sizeof(source));
	source.kind = SG_RUNE_COMPACT_SOURCE_BSP_PLANE;
	source.value.bsp_plane.model = 0;
	source.value.bsp_plane.leaf = 1;
	source.value.bsp_plane.plane = plane;
	return source;
}

static sg_rune_compact_source_t split_source(uint32_t parent, uint32_t ordinal)
{
	sg_rune_compact_source_t source;
	memset(&source, 0, sizeof(source));
	source.kind = SG_RUNE_COMPACT_SOURCE_SPLIT;
	source.value.split.parent_facet.value = parent;
	source.value.split.ordinal = ordinal;
	return source;
}

static void put_u32(unsigned char *p, uint32_t value)
{
	p[0] = (unsigned char)value;
	p[1] = (unsigned char)(value >> 8);
	p[2] = (unsigned char)(value >> 16);
	p[3] = (unsigned char)(value >> 24);
}

static uint32_t get_u32(const unsigned char *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
		((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t get_u64(const unsigned char *p)
{
	return (uint64_t)get_u32(p) | ((uint64_t)get_u32(p + 4) << 32);
}

static uint32_t test_checksum(const unsigned char *data, size_t size)
{
	uint32_t crc = UINT32_MAX;
	size_t i;
	for (i = 0; i < size; ++i)
	{
		uint32_t byte = data[i];
		uint32_t bit;
		if (i >= TEST_CHECKSUM_OFFSET && i < TEST_CHECKSUM_OFFSET + 4)
			byte = 0;
		crc ^= byte;
		for (bit = 0; bit < 8; ++bit)
			crc = (crc >> 1) ^
				(UINT32_C(0xedb88320) & (uint32_t)-(int32_t)(crc & 1));
	}
	return ~crc;
}

static void refresh_checksum(unsigned char *image, size_t size)
{
	put_u32(image + TEST_CHECKSUM_OFFSET, test_checksum(image, size));
}

static uint64_t section_offset(const unsigned char *image, uint32_t section)
{
	return get_u64(image + TEST_HEADER_FIXED +
		section * TEST_DESCRIPTOR_SIZE + 16);
}

static void init_fixture(test_fixture_t *fixture)
{
	sg_rune_compact_model_t *model;
	uint32_t profile;
	uint32_t region;
	uint32_t reference_cursor = 6;
	uint32_t kernel_cursor = 0;
	uint32_t i;
	memset(fixture, 0, sizeof(*fixture));

	fixture->vertices[0] = (sg_rune_q8_vec3_t){ { 64, 0, 0 } };
	fixture->vertices[1] = (sg_rune_q8_vec3_t){ { 64, 64, 0 } };
	fixture->vertices[2] = (sg_rune_q8_vec3_t){ { 64, 64, 64 } };
	fixture->vertices[3] = (sg_rune_q8_vec3_t){ { 64, 0, 64 } };

	fixture->cells[0].source = (sg_rune_compact_cell_source_t){ 0, 1, 2, 3, 0 };
	fixture->cells[0].bounds.maxs =
		(sg_rune_q8_vec3_t){ { 64, 64, 64 } };
	fixture->cells[0].incidences =
		(sg_rune_compact_cell_incidence_span_t){ 0, 1 };
	fixture->cells[0].movement_fields =
		(sg_rune_movement_field_span_t){ 0, 1 };
	fixture->cells[0].weapon_regions =
		(sg_rune_weapon_response_region_span_t){ 0, 1 };
	fixture->cells[0].valid_stances = SG_RUNE_STANCE_VALID_ALL;
	fixture->cells[1] = fixture->cells[0];
	fixture->cells[1].source.leaf = 2;
	fixture->cells[1].bounds.mins.value[0] = 64;
	fixture->cells[1].bounds.maxs.value[0] = 128;
	fixture->cells[1].incidences.first = 1;
	fixture->cells[1].movement_fields.first = 1;
	fixture->cells[1].weapon_regions.first = 1;

	fixture->facets[0].source = bsp_plane_source(4);
	fixture->facets[0].plane.normal_bits[0] = float_bits(1.0f);
	fixture->facets[0].plane.distance_bits = float_bits(8.0f);
	fixture->facets[0].vertices =
		(sg_rune_compact_vertex_span_t){ 0, 4 };
	fixture->facets[0].incidences =
		(sg_rune_compact_incidence_span_t){ 0, 2 };
	fixture->incidences[0].cell.value = 0;
	fixture->incidences[0].facet.value = 0;
	fixture->incidences[0].side = SG_RUNE_FACET_NEGATIVE_SIDE;
	fixture->incidences[0].boundary = SG_RUNE_BOUNDARY_CLOSED;
	fixture->incidences[1] = fixture->incidences[0];
	fixture->incidences[1].cell.value = 1;
	fixture->incidences[1].side = SG_RUNE_FACET_POSITIVE_SIDE;
	fixture->incidences[1].boundary = SG_RUNE_BOUNDARY_OPEN;
	fixture->cell_incidences[0].value = 0;
	fixture->cell_incidences[1].value = 1;

	fixture->portals[0].source = split_source(0, 0);
	fixture->portals[0].facet.value = 0;
	fixture->portals[0].negative_incidence.value = 0;
	fixture->portals[0].positive_incidence.value = 1;
	fixture->portals[0].clearance_q8 = 32;
	fixture->portals[0].direction = SG_RUNE_PORTAL_CONTINUITY_BOTH;
	fixture->portals[0].valid_stances = SG_RUNE_STANCE_VALID_ALL;

	fixture->movement_fields[0].cell.value = 0;
	fixture->movement_fields[0].boundary_portal.value = 0;
	fixture->movement_fields[0].family = SG_RUNE_MOVEMENT_FIELD_HOOK;
	fixture->movement_fields[0].valid_stances = SG_RUNE_STANCE_VALID_ALL;
	fixture->movement_fields[0].functions =
		(sg_rune_analytic_function_span_t){ 0, 3 };
	fixture->movement_fields[1] = fixture->movement_fields[0];
	fixture->movement_fields[1].cell.value = 1;
	fixture->movement_fields[1].boundary_portal.value = SG_RUNE_COMPACT_INDEX_NONE;
	fixture->movement_fields[1].functions.first = 3;
	fixture->analytic_refs[0].value = 0;
	fixture->analytic_refs[1].value = 1;
	fixture->analytic_refs[2].value = 5;
	fixture->analytic_refs[3].value = 0;
	fixture->analytic_refs[4].value = 1;
	fixture->analytic_refs[5].value = 5;

	fixture->weapon_regions[0].cell.value = 0;
	fixture->weapon_regions[0].boundary_incidences =
		(sg_rune_compact_cell_incidence_span_t){ 0, 1 };
	fixture->weapon_regions[0].kernels =
		(sg_rune_weapon_response_kernel_span_t){ 0, 13 };
	fixture->weapon_regions[1] = fixture->weapon_regions[0];
	fixture->weapon_regions[1].cell.value = 1;
	fixture->weapon_regions[1].boundary_incidences.first = 1;
	fixture->weapon_regions[1].kernels.first = 13;
	for (profile = 0; profile < 12; ++profile)
	{
		fixture->weapon_profiles[profile].source_profile = profile + 1;
		fixture->weapon_profiles[profile].response_families =
			SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(profile);
	}
	fixture->weapon_profiles[SG_RUNE_WEAPON_RESPONSE_ROCKET_IMPACT].
		response_families |= SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(
			SG_RUNE_WEAPON_RESPONSE_ROCKET_SPLASH);
	for (region = 0; region < 2; ++region)
	{
		for (profile = 0; profile < 12; ++profile)
		{
			uint32_t family;

			for (family = 0;
				family < (uint32_t)SG_RUNE_WEAPON_RESPONSE_FAMILY_COUNT;
				++family)
			{
				uint32_t bit = SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(family);
				int grenade =
					family == SG_RUNE_WEAPON_RESPONSE_GRENADE_FLIGHT ||
					family == SG_RUNE_WEAPON_RESPONSE_GRENADE_BOUNCE_FUSE;

				if ((fixture->weapon_profiles[profile].response_families & bit) == 0)
					continue;
				fixture->weapon_kernels[kernel_cursor].region.value = region;
				fixture->weapon_kernels[kernel_cursor].profile = profile;
				fixture->weapon_kernels[kernel_cursor].family =
					(sg_rune_weapon_response_family_t)family;
				fixture->weapon_kernels[kernel_cursor].functions.first =
					reference_cursor;
				fixture->weapon_kernels[kernel_cursor].functions.count =
					grenade ? 5 : 4;
				fixture->analytic_refs[reference_cursor++].value = 1;
				fixture->analytic_refs[reference_cursor++].value = 2;
				fixture->analytic_refs[reference_cursor++].value = 3;
				fixture->analytic_refs[reference_cursor++].value = 4;
				if (grenade)
					fixture->analytic_refs[reference_cursor++].value = 6;
				kernel_cursor++;
			}
		}
	}
	for (i = 0; i < 7; ++i)
	{
		fixture->functions[i].definition = i;
		fixture->functions[i].form = SG_RUNE_COMPACT_ANALYTIC_CONSTANT;
		fixture->constants[i].value.bits = float_bits((float)i + 1.0f);
	}
	fixture->functions[0].output = SG_RUNE_ANALYTIC_OUTPUT_COST;
	fixture->functions[1].output = SG_RUNE_ANALYTIC_OUTPUT_TRAVEL_TIME_SECONDS;
	fixture->functions[2].output = SG_RUNE_ANALYTIC_OUTPUT_DAMAGE;
	fixture->functions[3].output = SG_RUNE_ANALYTIC_OUTPUT_HIT_PROBABILITY;
	fixture->functions[4].output = SG_RUNE_ANALYTIC_OUTPUT_VISIBILITY_FRACTION;
	fixture->functions[5].output = SG_RUNE_ANALYTIC_OUTPUT_REACHABILITY_MARGIN;
	fixture->functions[6].output = SG_RUNE_ANALYTIC_OUTPUT_FUSE_REMAINING_SECONDS;
	fixture->analytic.version = SG_RUNE_COMPACT_ANALYTIC_VERSION;
	fixture->analytic.functions = fixture->functions;
	fixture->analytic.function_count = 7;
	fixture->analytic.constants = fixture->constants;
	fixture->analytic.constant_count = 7;

	fixture->mechanisms[0].source.entity_ordinal = 7;
	fixture->mechanisms[0].controller.entity_ordinal = 7;
	fixture->mechanisms[0].entry_cell.value = 0;
	fixture->mechanisms[0].exit_cell.value = 1;
	fixture->mechanisms[0].activation_landmark.value = 0;
	fixture->mechanisms[0].bounds.mins =
		(sg_rune_q8_vec3_t){ { 16, 16, 0 } };
	fixture->mechanisms[0].bounds.maxs =
		(sg_rune_q8_vec3_t){ { 32, 32, 48 } };
	fixture->mechanisms[0].topology =
		(sg_rune_compact_mechanism_edge_span_t){ 0, 1 };
	fixture->mechanisms[0].dwell_ms = 250;
	fixture->mechanisms[0].travel_ms = 1000;
	fixture->mechanisms[0].reset_ms = 2000;
	fixture->mechanisms[0].kind = SG_RUNE_COMPACT_MECHANISM_DOOR;
	fixture->mechanisms[0].activation = SG_RUNE_COMPACT_MECHANISM_ACTIVATION_DWELL;
	fixture->mechanisms[0].initial_state = SG_RUNE_COMPACT_MECHANISM_STATE_INACTIVE;
	fixture->mechanisms[0].activated_state = SG_RUNE_COMPACT_MECHANISM_STATE_ACTIVE;
	fixture->mechanisms[0].reset_state = SG_RUNE_COMPACT_MECHANISM_STATE_INACTIVE;
	fixture->mechanisms[0].recovery =
		SG_RUNE_COMPACT_MECHANISM_RECOVERY_WAIT_FOR_RESET;
	fixture->mechanisms[1] = fixture->mechanisms[0];
	fixture->mechanisms[1].source.entity_ordinal = 11;
	fixture->mechanisms[1].controller.entity_ordinal = 11;
	fixture->mechanisms[1].entry_cell.value = 0;
	fixture->mechanisms[1].exit_cell.value = 0;
	fixture->mechanisms[1].topology =
		(sg_rune_compact_mechanism_edge_span_t){ 1, 0 };
	fixture->mechanisms[1].dwell_ms = 0;
	fixture->mechanisms[1].travel_ms = 0;
	fixture->mechanisms[1].reset_ms = 0;
	fixture->mechanisms[1].kind = SG_RUNE_COMPACT_MECHANISM_BUTTON;
	fixture->mechanisms[1].activation = SG_RUNE_COMPACT_MECHANISM_ACTIVATION_USE;
	fixture->mechanisms[1].recovery = SG_RUNE_COMPACT_MECHANISM_RECOVERY_NONE;
	fixture->mechanism_edges[0].source.entity_ordinal = 11;
	fixture->mechanism_edges[0].destination.entity_ordinal = 7;
	fixture->mechanism_edges[0].kind = SG_RUNE_COMPACT_MECHANISM_EDGE_ACTIVATES;

	fixture->landmarks[0].source.entity_ordinal = 11;
	fixture->landmarks[0].cells =
		(sg_rune_compact_landmark_cell_span_t){ 0, 1 };
	fixture->landmarks[0].mechanism.value = 1;
	fixture->landmarks[0].origin = (sg_rune_q8_vec3_t){ { 24, 24, 8 } };
	fixture->landmarks[0].bounds.mins =
		(sg_rune_q8_vec3_t){ { 16, 16, 0 } };
	fixture->landmarks[0].bounds.maxs =
		(sg_rune_q8_vec3_t){ { 32, 32, 16 } };
	fixture->landmarks[0].kind = SG_RUNE_COMPACT_LANDMARK_BUTTON;
	fixture->landmarks[1] = fixture->landmarks[0];
	fixture->landmarks[1].source.entity_ordinal = 19;
	fixture->landmarks[1].cells.first = 1;
	fixture->landmarks[1].mechanism.value = SG_RUNE_COMPACT_INDEX_NONE;
	fixture->landmarks[1].origin = (sg_rune_q8_vec3_t){ { 96, 24, 8 } };
	fixture->landmarks[1].bounds.mins =
		(sg_rune_q8_vec3_t){ { 88, 16, 0 } };
	fixture->landmarks[1].bounds.maxs =
		(sg_rune_q8_vec3_t){ { 104, 32, 16 } };
	fixture->landmarks[1].kind = SG_RUNE_COMPACT_LANDMARK_FLAG;
	fixture->landmarks[1].variant = 1;
	fixture->landmark_cells[0].value = 0;
	fixture->landmark_cells[1].value = 1;
	fixture->annotations[0].facet.value = 0;
	fixture->annotations[0].attributes = SG_RUNE_COMPACT_FACET_HOOKABLE;
	fixture->annotations[0].hookable_stances = SG_RUNE_STANCE_VALID_ALL;
	fixture->portal_mechanisms[0].portal.value = 0;
	fixture->portal_mechanisms[0].mechanism.value = 0;
	fixture->portal_mechanisms[0].kind =
		SG_RUNE_COMPACT_PORTAL_MECHANISM_BLOCKS;

	fixture->static_data.mechanisms = fixture->mechanisms;
	fixture->static_data.mechanism_count = 2;
	fixture->static_data.mechanism_edges = fixture->mechanism_edges;
	fixture->static_data.mechanism_edge_count = 1;
	fixture->static_data.landmarks = fixture->landmarks;
	fixture->static_data.landmark_count = 2;
	fixture->static_data.landmark_cells = fixture->landmark_cells;
	fixture->static_data.landmark_cell_count = 2;
	fixture->static_data.facet_annotations = fixture->annotations;
	fixture->static_data.facet_annotation_count = 1;
	fixture->static_data.portal_mechanisms = fixture->portal_mechanisms;
	fixture->static_data.portal_mechanism_count = 1;

	model = &fixture->model;
	model->version = SG_RUNE_COMPACT_MODEL_VERSION;
	model->schema_tag = SG_RUNE_COMPACT_MODEL_SCHEMA_TAG;
	model->identity.bsp_sha256[0] = UINT8_C(0x5a);
	model->identity.bsp_bytes = 1024;
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
	model->identity.source_counts =
		(sg_rune_compact_source_counts_t){ 1, 3, 4, 5, 1, 1, 32 };
	model->identity.standing_hull.mins =
		(sg_rune_q8_vec3_t){ { -128, -128, -192 } };
	model->identity.standing_hull.maxs =
		(sg_rune_q8_vec3_t){ { 128, 128, 256 } };
	model->identity.crouching_hull.mins =
		(sg_rune_q8_vec3_t){ { -128, -128, -192 } };
	model->identity.crouching_hull.maxs =
		(sg_rune_q8_vec3_t){ { 128, 128, 128 } };
	model->identity.physics.gravity_bits = float_bits(100.0f);
	model->identity.physics.ground_acceleration_bits = float_bits(10.0f);
	model->identity.physics.air_acceleration_bits = float_bits(1.0f);
	model->identity.physics.water_acceleration_bits = float_bits(4.0f);
	model->identity.physics.hook_acceleration_bits = float_bits(1000.0f);
	model->identity.physics.external_acceleration_bits = float_bits(1200.0f);
	model->identity.physics.water_drag_bits = float_bits(0.5f);
	model->identity.physics.max_velocity_bits = float_bits(800.0f);
	model->identity.physics.frame_ms = 8;
	model->identity.physics.substep_ms = 1;
	model->cells = fixture->cells;
	model->cell_count = 2;
	model->facets = fixture->facets;
	model->facet_count = 1;
	model->incidences = fixture->incidences;
	model->incidence_count = 2;
	model->cell_incidences = fixture->cell_incidences;
	model->cell_incidence_count = 2;
	model->vertices = fixture->vertices;
	model->vertex_count = 4;
	model->portals = fixture->portals;
	model->portal_count = 1;
	model->movement_fields = fixture->movement_fields;
	model->movement_field_count = 2;
	model->weapon_regions = fixture->weapon_regions;
	model->weapon_region_count = 2;
	model->weapon_profiles = fixture->weapon_profiles;
	model->weapon_profile_count = 12;
	model->weapon_kernels = fixture->weapon_kernels;
	model->weapon_kernel_count = 26;
	model->analytic_function_refs = fixture->analytic_refs;
	model->analytic_function_ref_count = reference_cursor;
	model->analytic = &fixture->analytic;
	model->static_data = &fixture->static_data;
}

static int expect_error(const unsigned char *image, size_t size,
	sg_rune_compact_wire_error_code_t expected)
{
	sg_rune_compact_wire_error_t error;
	CHECK(!SG_RuneCompactWireInspect(image, size, NULL, &error));
	CHECK(error.code == expected);
	return 1;
}

static int expect_decode_model_error(const unsigned char *image, size_t size,
	const sg_rune_compact_identity_t *identity,
	sg_rune_compact_error_code_t expected_code,
	sg_rune_compact_record_domain_t expected_domain,
	uint32_t expected_record)
{
	sg_rune_compact_wire_error_t error;
	sg_rune_compact_wire_decoded_t *decoded = NULL;

	CHECK(SG_RuneCompactWireInspect(image, size, NULL, &error));
	CHECK(!SG_RuneCompactWireDecode(image, size, identity, &decoded,
		&error));
	CHECK(decoded == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_WIRE_ERROR_INVALID_MODEL);
	CHECK(error.model_error.code == expected_code);
	CHECK(error.model_error.domain == expected_domain);
	CHECK(error.model_error.record == expected_record);
	return 1;
}

static int test_round_trip(void)
{
	test_fixture_t fixture;
	sg_rune_compact_wire_info_t info;
	sg_rune_compact_wire_error_t error;
	sg_rune_compact_wire_decoded_t *decoded = NULL;
	const sg_rune_compact_model_t *model;
	unsigned char *first;
	unsigned char *second;
	size_t size;
	size_t written;
	sg_rune_compact_error_t model_error;
	init_fixture(&fixture);
	CHECK(SG_RuneCompactModelValidateBound(&fixture.model,
		&fixture.model.identity, &model_error));
	CHECK(SG_RuneCompactWireMeasure(&fixture.model, &size, &error));
	CHECK(size > 696);
	first = malloc(size);
	second = malloc(size);
	CHECK(first != NULL && second != NULL);
	CHECK(SG_RuneCompactWireEncode(&fixture.model, first, size, &written, &error));
	CHECK(written == size);
	CHECK(SG_RuneCompactWireInspect(first, size, &info, &error));
	CHECK(info.image_bytes == size);
	CHECK(info.checksum == test_checksum(first, size));
	CHECK(memcmp(&info.identity, &fixture.model.identity,
		sizeof(info.identity)) == 0);
	CHECK(info.counts[SG_RUNE_COMPACT_WIRE_SECTION_CELLS] == 2);
	CHECK(info.counts[SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_PROFILES] == 12);
	CHECK(info.counts[SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_KERNELS] == 26);
	CHECK(info.counts[SG_RUNE_COMPACT_WIRE_SECTION_ANALYTIC_FUNCTION_REFS] == 114);
	CHECK(info.counts[SG_RUNE_COMPACT_WIRE_SECTION_MECHANISMS] == 2);
	CHECK(SG_RuneCompactWireDecode(first, size, &fixture.model.identity,
		&decoded, &error));
	model = SG_RuneCompactWireModel(decoded);
	CHECK(model != NULL && model != &fixture.model);
	CHECK(model->analytic != fixture.model.analytic);
	CHECK(model->static_data != fixture.model.static_data);
	CHECK(model->identity.producer_identity ==
		fixture.model.identity.producer_identity);
	CHECK(model->facets[0].source.value.bsp_plane.plane == 4);
	CHECK(model->movement_fields[0].valid_stances == SG_RUNE_STANCE_VALID_ALL);
	CHECK(model->weapon_profiles[11].source_profile == 12);
	CHECK(model->weapon_kernels[25].profile == 11);
	CHECK(model->static_data->mechanisms[0].reset_ms == 2000);
	CHECK(SG_RuneCompactModelValidateBound(model, &fixture.model.identity,
		&model_error));
	CHECK(SG_RuneCompactWireEncode(model, second, size, &written, &error));
	CHECK(written == size);
	CHECK(memcmp(first, second, size) == 0);
	SG_RuneCompactWireDestroy(decoded);
	free(second);
	free(first);
	return 1;
}

static int test_semantic_and_identity_rejection(void)
{
	test_fixture_t fixture;
	sg_rune_compact_identity_t expected;
	sg_rune_compact_wire_error_t error;
	sg_rune_compact_wire_decoded_t *decoded = NULL;
	unsigned char *image;
	unsigned char *copy;
	size_t size;
	size_t written;
	uint64_t offset;

	init_fixture(&fixture);
	CHECK(SG_RuneCompactWireMeasure(&fixture.model, &size, &error));
	image = malloc(size);
	copy = malloc(size);
	CHECK(image != NULL && copy != NULL);
	CHECK(SG_RuneCompactWireEncode(&fixture.model, image, size, &written, &error));

	memcpy(copy, image, size);
	offset = section_offset(copy, SG_RUNE_COMPACT_WIRE_SECTION_CELLS);
	put_u32(copy + offset,
		fixture.model.identity.source_counts.model_count);
	refresh_checksum(copy, size);
	CHECK(SG_RuneCompactWireInspect(copy, size, NULL, &error));
	CHECK(!SG_RuneCompactWireDecode(copy, size, &fixture.model.identity,
		&decoded, &error));
	CHECK(decoded == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_WIRE_ERROR_INVALID_MODEL);
	CHECK(error.model_error.code == SG_RUNE_COMPACT_ERROR_INVALID_PROVENANCE);
	CHECK(error.model_error.domain == SG_RUNE_COMPACT_RECORD_CELL);
	CHECK(error.model_error.record == 0);

	expected = fixture.model.identity;
	expected.gravity_law_id ^= 1;
	CHECK(!SG_RuneCompactWireDecode(image, size, &expected, &decoded, &error));
	CHECK(decoded == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_WIRE_ERROR_IDENTITY_MISMATCH);
	CHECK(error.model_error.code == SG_RUNE_COMPACT_ERROR_IDENTITY_MISMATCH);

	free(copy);
	free(image);
	return 1;
}

static int test_hostile_images(void)
{
	test_fixture_t fixture;
	sg_rune_compact_wire_error_t error;
	unsigned char *image;
	unsigned char *copy;
	size_t size;
	size_t written;
	uint64_t offset;
	init_fixture(&fixture);
	CHECK(SG_RuneCompactWireMeasure(&fixture.model, &size, &error));
	image = malloc(size);
	copy = malloc(size);
	CHECK(image != NULL && copy != NULL);
	CHECK(SG_RuneCompactWireEncode(&fixture.model, image, size, &written, &error));
	CHECK(expect_error(image, size - 1,
		SG_RUNE_COMPACT_WIRE_ERROR_TRUNCATED));

	memcpy(copy, image, size);
	copy[section_offset(copy, SG_RUNE_COMPACT_WIRE_SECTION_VERTICES)] ^= 1;
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_CHECKSUM_MISMATCH));

	memcpy(copy, image, size);
	copy[8] = 2;
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_UNSUPPORTED_VERSION));

	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_PROFILES);
	put_u32(copy + offset + 4,
		SG_RUNE_WEAPON_RESPONSE_FAMILIES_ALL |
		SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(
			SG_RUNE_WEAPON_RESPONSE_FAMILY_COUNT));
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_FORMAT));

	memcpy(copy, image, size);
	put_u32(copy + TEST_HEADER_FIXED +
		SG_RUNE_COMPACT_WIRE_SECTION_CELLS * TEST_DESCRIPTOR_SIZE + 8,
		SG_RUNE_COMPACT_MAX_CELLS + 1);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_LIMIT_EXCEEDED));

	memcpy(copy, image, size);
	offset = section_offset(copy, SG_RUNE_COMPACT_WIRE_SECTION_FACETS);
	put_u32(copy + TEST_HEADER_FIXED +
		SG_RUNE_COMPACT_WIRE_SECTION_FACETS * TEST_DESCRIPTOR_SIZE + 16,
		(uint32_t)(offset + 8));
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_SECTION));

	memcpy(copy, image, size);
	offset = section_offset(copy, SG_RUNE_COMPACT_WIRE_SECTION_CELLS);
	put_u32(copy + offset + 44, UINT32_MAX);
	put_u32(copy + offset + 48, 2);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_SPAN));

	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_CELL_INCIDENCES);
	put_u32(copy + offset, fixture.model.incidence_count);
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE));

	memcpy(copy, image, size);
	offset = section_offset(copy, SG_RUNE_COMPACT_WIRE_SECTION_CELLS);
	copy[offset + 77] = 1;
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_NONZERO_RESERVED));

	memcpy(copy, image, size);
	copy[size - 1] = 1;
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_NONZERO_RESERVED));

	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_REGIONS);
	put_u32(copy + offset + 16, 12);
	refresh_checksum(copy, size);
	CHECK(expect_decode_model_error(copy, size, &fixture.model.identity,
		SG_RUNE_COMPACT_ERROR_INVALID_REFERENCE,
		SG_RUNE_COMPACT_RECORD_WEAPON_REGION, 0));

	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_PROFILES);
	put_u32(copy + offset + 6 * 8 + 4,
		SG_RUNE_WEAPON_RESPONSE_FAMILY_BIT(
			SG_RUNE_WEAPON_RESPONSE_ROCKET_IMPACT));
	refresh_checksum(copy, size);
	CHECK(expect_error(copy, size,
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_REFERENCE));

	memcpy(copy, image, size);
	offset = section_offset(copy,
		SG_RUNE_COMPACT_WIRE_SECTION_WEAPON_KERNELS);
	put_u32(copy + offset + 7 * 20 + 4,
		SG_RUNE_WEAPON_RESPONSE_ROCKET_IMPACT);
	put_u32(copy + offset + 7 * 20 + 8,
		SG_RUNE_WEAPON_RESPONSE_ROCKET_IMPACT);
	refresh_checksum(copy, size);
	CHECK(expect_decode_model_error(copy, size, &fixture.model.identity,
		SG_RUNE_COMPACT_ERROR_NONCANONICAL_ORDER,
		SG_RUNE_COMPACT_RECORD_WEAPON_KERNEL, 7));

	free(copy);
	free(image);
	return 1;
}

static int test_api_errors(void)
{
	test_fixture_t fixture;
	sg_rune_compact_wire_error_t error;
	sg_rune_compact_wire_decoded_t *decoded =
		(sg_rune_compact_wire_decoded_t *)(uintptr_t)1;
	size_t size = 99;
	size_t written = 99;
	init_fixture(&fixture);
	CHECK(!SG_RuneCompactWireMeasure(NULL, &size, &error));
	CHECK(size == 0);
	CHECK(error.code == SG_RUNE_COMPACT_WIRE_ERROR_INVALID_ARGUMENT);
	CHECK(!SG_RuneCompactWireEncode(&fixture.model, NULL, 0, &written, &error));
	CHECK(written == 0);
	CHECK(!SG_RuneCompactWireDecode(NULL, 0, &fixture.model.identity, NULL,
		&error));
	CHECK(!SG_RuneCompactWireDecode(NULL, 0, NULL, &decoded, &error));
	CHECK(decoded == NULL);
	CHECK(error.code == SG_RUNE_COMPACT_WIRE_ERROR_INVALID_ARGUMENT);
	CHECK(strcmp(SG_RuneCompactWireErrorString(
		SG_RUNE_COMPACT_WIRE_ERROR_CHECKSUM_MISMATCH),
		"checksum mismatch") == 0);
	CHECK(strcmp(SG_RuneCompactWireErrorString(
		SG_RUNE_COMPACT_WIRE_ERROR_INVALID_MODEL),
		"invalid compact model") == 0);
	CHECK(strcmp(SG_RuneCompactWireErrorString(
		SG_RUNE_COMPACT_WIRE_ERROR_IDENTITY_MISMATCH),
		"compact model identity mismatch") == 0);
	return 1;
}

int main(void)
{
	CHECK(test_round_trip());
	CHECK(test_hostile_images());
	CHECK(test_semantic_and_identity_rejection());
	CHECK(test_api_errors());
	puts("sg_rune_compact_wire_test: ok");
	return 0;
}
