#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../slipgate/sg_rune_compact_pmove_control_build_private.h"
#include "../slipgate/sg_rune_compact_pmove_control_wire.h"

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static sg_rune_pmove_control_identity_t Identity(void)
{
	sg_rune_pmove_control_identity_t identity;

	memset(&identity, 0, sizeof(identity));
	identity.version = SG_RUNE_PMOVE_CONTROL_VERSION;
	identity.compact_artifact_id = UINT64_C(0x1001);
	identity.bsp_content_id = UINT64_C(0x2002);
	memset(identity.bsp_identity, 0x5a, sizeof(identity.bsp_identity));
	identity.physics_abi_id = UINT64_C(0x3003);
	identity.collision_law_id = SG_RUNE_PMOVE_CONTROL_COLLISION_LAW_ID;
	identity.pmove_law_id = SG_RUNE_PMOVE_CONTROL_PMOVE_LAW_ID;
	identity.pmove_behavior_id = identity.physics_abi_id;
	identity.frame_ms = SG_RUNE_PMOVE_CONTROL_FRAME_MS;
	identity.substep_ms = SG_RUNE_PMOVE_CONTROL_SUBSTEP_MS;
	identity.substep_count = SG_RUNE_PMOVE_CONTROL_SUBSTEPS;
	identity.frame_cost_units = 1U;
	identity.source_reserve_units = SG_RUNE_PMOVE_CONTROL_SOURCE_RESERVE;
	return identity;
}

static int Build(sg_rune_pmove_control_storage_t *storage,
	sg_rune_pmove_control_model_t *model)
{
	sg_rune_pmove_control_build_input_t input;
	sg_rune_pmove_control_error_t error;

	memset(&input, 0, sizeof(input));
	input.identity = Identity();
	input.cell = 7U;
	input.portal = 3U;
	input.target_cell = 8U;
	input.corridor_min_q8[0] = -512 * 8;
	input.corridor_max_q8[0] = 272 * 8;
	input.corridor_min_q8[1] = -512 * 8;
	input.corridor_max_q8[1] = 512 * 8;
	input.portal_q8 = 256 * 8;
	input.support_z_q8 = 0;
	input.hull_half_width_q8 = 16 * 8;
	input.maximum_velocity_q8 = 2000 * 8;
	return SG_RunePmoveControlBuildAxisCorridorPrivate(&input, storage, model,
		&error);
}

static sg_rune_pmove_control_state_t State(int32_t x, int32_t y,
	int32_t vx, int32_t vy)
{
	sg_rune_pmove_control_state_t state;

	memset(&state, 0, sizeof(state));
	state.origin_q8[0] = x * 8;
	state.origin_q8[1] = y * 8;
	state.velocity_q8[0] = vx * 8;
	state.velocity_q8[1] = vy * 8;
	state.cell = 7U;
	state.standing = 1U;
	state.dry = 1U;
	state.supported = 1U;
	state.support_is_static_world = 1U;
	return state;
}

static void TestShapeAndWholeDomainPotential(void)
{
	sg_rune_pmove_control_storage_t storage;
	sg_rune_pmove_control_model_t model;
	sg_rune_pmove_control_error_t error;
	sg_rune_pmove_control_gradient_t gradient;
	sg_rune_pmove_control_state_t values[] = {
		State(0, 0, 0, 0), State(0, 0, 300, 0),
		State(0, 0, -300, 0), State(0, 0, 0, 300),
		State(0, 0, -300, 300), State(0, 0, 1999, 0),
		State(0, 0, -1999, 0), State(0, 0, 0, -2000),
		State(0, 0, 0, 2000)
	};
	sg_rune_pmove_control_state_t unsafe_positive_edge =
		State(0, 500, 0, 2000);
	sg_rune_pmove_control_state_t unsafe_negative_edge =
		State(0, -500, 0, -2000);
	uint64_t units[sizeof(values) / sizeof(values[0])];
	uint32_t index;

	CHECK(Build(&storage, &model));
	CHECK(SG_RunePmoveControlValidate(&model, &error));
	CHECK(storage.region.longitudinal_min_q8 <
		storage.region.longitudinal_max_q8);
	CHECK(storage.region.lateral_min_q8 < storage.region.lateral_max_q8);
	CHECK(storage.region.velocity_forward_min_q8 <
		storage.region.velocity_forward_max_q8);
	CHECK(storage.region.velocity_lateral_min_q8 <
		storage.region.velocity_lateral_max_q8);
	CHECK(storage.certificate.minimum_descent_units >=
		model.identity.frame_cost_units);
	for (index = 0U; index < sizeof(values) / sizeof(values[0]); index++)
		CHECK(SG_RunePmoveControlPotentialCeil(&model, 0U, &values[index],
			100U, &units[index], &error));
	CHECK(!SG_RunePmoveControlPotentialCeil(&model, 0U,
		&unsafe_positive_edge,
		100U, &units[0], &error));
	CHECK(error == SG_RUNE_PMOVE_CONTROL_ERROR_REGION_MISS);
	CHECK(!SG_RunePmoveControlPotentialCeil(&model, 0U,
		&unsafe_negative_edge,
		100U, &units[0], &error));
	CHECK(error == SG_RUNE_PMOVE_CONTROL_ERROR_REGION_MISS);
	CHECK(units[2] > units[0]);
	CHECK(units[3] > units[0]);
	CHECK(units[4] > units[2] && units[4] > units[3]);
	CHECK(units[6] > units[5]);
	CHECK(SG_RunePmoveControlGradient(&model, 0U, &values[4], &gradient,
		&error));
	CHECK(gradient.longitudinal < 0);
	CHECK(gradient.reversal_velocity < 0);
	CHECK(gradient.lateral_velocity > 0);
}

static void TestStrictDescentAndPortal(void)
{
	sg_rune_pmove_control_storage_t storage;
	sg_rune_pmove_control_model_t model;
	sg_rune_pmove_control_error_t error;
	sg_rune_pmove_control_state_t source = State(0, 0, 0, 0);
	sg_rune_pmove_control_state_t next = source;
	uint64_t source_units;
	uint64_t next_units;
	uint32_t frame;

	CHECK(Build(&storage, &model));
	for (frame = 0U; frame < 3U; frame++)
	{
		next.origin_q8[0] = source.origin_q8[0] + 16 * 8;
		CHECK(SG_RunePmoveControlCheckDescent(&model, 0U, &source, &next,
			0U, 100U, &source_units, &next_units, &error));
		CHECK(next_units + model.identity.frame_cost_units < source_units);
		source = next;
	}
	CHECK(!SG_RunePmoveControlCheckDescent(&model, 0U, &source, &source,
		0U, 100U, &source_units, &next_units, &error));
	CHECK(error == SG_RUNE_PMOVE_CONTROL_ERROR_NO_DESCENT);
	next = source;
	next.origin_q8[0] = storage.region.portal_q8;
	next.cell = storage.region.target_cell;
	CHECK(SG_RunePmoveControlCheckDescent(&model, 0U, &source, &next,
		1U, 100U, &source_units, &next_units, &error));
	CHECK(next_units == 100U);
	next.cell++;
	CHECK(!SG_RunePmoveControlCheckDescent(&model, 0U, &source, &next,
		1U, 100U, &source_units, &next_units, &error));
	CHECK(error == SG_RUNE_PMOVE_CONTROL_ERROR_PORTAL_MISMATCH);
	model.identity.source_reserve_units = 2U;
	CHECK(!SG_RunePmoveControlValidate(&model, &error));
	CHECK(error == SG_RUNE_PMOVE_CONTROL_ERROR_INVALID_IDENTITY);
	model.identity.source_reserve_units = 1U;
	CHECK(!SG_RunePmoveControlPotentialCeil(&model, 0U, &source,
		UINT64_MAX, &source_units, &error));
	CHECK(error == SG_RUNE_PMOVE_CONTROL_ERROR_OVERFLOW);
	source.origin_q8[2] = 1;
	CHECK(!SG_RunePmoveControlPotentialCeil(&model, 0U, &source, 100U,
		&source_units, &error));
	CHECK(error == SG_RUNE_PMOVE_CONTROL_ERROR_REGION_MISS);
	CHECK(Build(&storage, &model));
	storage.transitions[1] = storage.transitions[0];
	CHECK(!SG_RunePmoveControlValidate(&model, &error));
	CHECK(error == SG_RUNE_PMOVE_CONTROL_ERROR_INVALID_REFERENCE);
	CHECK(Build(&storage, &model));
	storage.region.lateral_min_q8 = INT32_MIN;
	storage.region.lateral_max_q8 = INT32_MAX;
	storage.region.lateral_center_q8 = INT32_MAX - 1;
	CHECK(!SG_RunePmoveControlValidate(&model, &error));
	CHECK(error == SG_RUNE_PMOVE_CONTROL_ERROR_INVALID_DOMAIN);
}

static void TestCanonicalSection(void)
{
	sg_rune_pmove_control_storage_t storage;
	sg_rune_pmove_control_storage_t decoded_storage;
	sg_rune_pmove_control_model_t model;
	sg_rune_pmove_control_model_t decoded;
	sg_rune_pmove_control_identity_t identity;
	sg_rune_pmove_control_error_t error;
	sg_rune_pmove_control_gradient_t first_gradient;
	sg_rune_pmove_control_gradient_t second_gradient;
	sg_rune_pmove_control_state_t first = State(0, -10, 0, -200);
	sg_rune_pmove_control_state_t second = State(0, 10, -300, 200);
	uint8_t *first_bytes;
	uint8_t *second_bytes;
	size_t size;

	CHECK(Build(&storage, &model));
	CHECK(SG_RunePmoveControlSectionMeasure(&model, &size, &error));
	error = SG_RUNE_PMOVE_CONTROL_ERROR_INVALID_CERTIFICATE;
	CHECK(!SG_RunePmoveControlSectionEncode(&model, NULL, size, &error));
	CHECK(error == SG_RUNE_PMOVE_CONTROL_ERROR_INVALID_ARGUMENT);
	first_bytes = malloc(size + 1U);
	second_bytes = malloc(size);
	CHECK(first_bytes != NULL && second_bytes != NULL);
	if (!first_bytes || !second_bytes)
	{
		free(first_bytes);
		free(second_bytes);
		return;
	}
	CHECK(SG_RunePmoveControlGradient(&model, 0U, &first, &first_gradient,
		&error));
	CHECK(SG_RunePmoveControlSectionEncode(&model, first_bytes, size, &error));
	CHECK(SG_RunePmoveControlGradient(&model, 0U, &second, &second_gradient,
		&error));
	CHECK(memcmp(&first_gradient, &second_gradient,
		sizeof(first_gradient)) != 0);
	CHECK(SG_RunePmoveControlSectionEncode(&model, second_bytes, size, &error));
	CHECK(memcmp(first_bytes, second_bytes, size) == 0);
	CHECK(SG_RunePmoveControlSectionInspect(first_bytes, size, &identity,
		&error));
	CHECK(identity.compact_artifact_id == model.identity.compact_artifact_id);
	CHECK(SG_RunePmoveControlSectionDecode(first_bytes, size, &decoded_storage,
		&decoded, &error));
	CHECK(decoded.transition_count == 2U);
	first_bytes[8] = UINT8_C(0x7f);
	first_bytes[9] = UINT8_C(0x01);
	CHECK(!SG_RunePmoveControlSectionInspect(first_bytes, size, &identity,
		&error));
	CHECK(error == SG_RUNE_PMOVE_CONTROL_ERROR_INVALID_DOMAIN);
	CHECK(!SG_RunePmoveControlSectionInspect(second_bytes, size + 1U,
		&identity, &error));
	free(first_bytes);
	free(second_bytes);
}

int main(void)
{
	sg_rune_pmove_control_model_t empty;
	sg_rune_pmove_control_error_t error;

	memset(&empty, 0, sizeof(empty));
	CHECK(!SG_RunePmoveControlValidate(&empty, &error));
	CHECK(error == SG_RUNE_PMOVE_CONTROL_ERROR_INVALID_IDENTITY);
	TestShapeAndWholeDomainPotential();
	TestStrictDescentAndPortal();
	TestCanonicalSection();
	if (failures)
		return 1;
	puts("v13 PMove control-region model/section tests passed");
	return 0;
}
