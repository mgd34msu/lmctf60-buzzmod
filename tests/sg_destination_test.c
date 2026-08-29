#include <stdio.h>

#include "slipgate/sg_destination.h"

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static sg_destination_ref_t Waypoint(void)
{
	sg_destination_ref_t destination = { 0 };

	destination.kind = SG_DESTINATION_WAYPOINT;
	destination.value.point.point_id = 17U;
	return destination;
}

static sg_rune_stable_id_t Stable(uint32_t domain, uint32_t ordinal)
{
	return (sg_rune_stable_id_t){
		.source_set_identity = 1U,
		.high = (uint64_t)domain << 32,
		.low = (uint64_t)ordinal << 32
	};
}

static void TestStaticPatch(void)
{
	sg_destination_terminal_t terminal = {
		.destination = Waypoint(),
		.generation = 2U,
		.kind = SG_DESTINATION_TERMINAL_STATIC_PATCH,
		.value.static_patch = {
			.domain = {
				.chart = { { 0 } },
				.domain = { { 0 } }
			}
		}
	};

	terminal.value.static_patch.domain.chart.value =
		Stable(SG_RUNE_ORDER_STATE_CHART, 11U);
	terminal.value.static_patch.domain.domain.value =
		Stable(SG_RUNE_ORDER_STATE_DOMAIN, 12U);
	CHECK(SG_DestinationTerminalValid(&terminal));
	terminal.value.static_patch.domain.domain.value.source_set_identity = 0U;
	CHECK(!SG_DestinationTerminalValid(&terminal));
	terminal.value.static_patch.domain.domain.value =
		Stable(SG_RUNE_ORDER_STATE_DOMAIN, 12U);
	terminal.value.static_patch.domain.domain.value.source_set_identity = 2U;
	CHECK(!SG_DestinationTerminalValid(&terminal));
	terminal.value.static_patch.domain.domain.value.source_set_identity = 1U;
	terminal.value.static_patch.domain.domain.value.high =
		(uint64_t)SG_RUNE_ORDER_STATE_CHART << 32;
	CHECK(!SG_DestinationTerminalValid(&terminal));
	terminal.value.static_patch.domain.domain.value =
		Stable(SG_RUNE_ORDER_STATE_DOMAIN, 12U);
	terminal.generation = 0U;
	CHECK(!SG_DestinationTerminalValid(&terminal));
}

static void TestMovingTube(void)
{
	sg_destination_tube_segment_t segments[2] = { 0 };
	sg_destination_terminal_t terminal = {
		.destination = Waypoint(),
		.generation = 3U,
		.kind = SG_DESTINATION_TERMINAL_MOVING_TUBE,
		.value.moving_tube = { 21U, segments, 2U }
	};

	segments[0].valid_from_ms = 100U;
	segments[0].valid_until_ms = 200U;
	segments[0].domain.chart.value =
		Stable(SG_RUNE_ORDER_STATE_CHART, 11U);
	segments[0].domain.domain.value =
		Stable(SG_RUNE_ORDER_STATE_DOMAIN, 12U);
	segments[1].valid_from_ms = 200U;
	segments[1].valid_until_ms = 300U;
	segments[1].domain.chart.value =
		Stable(SG_RUNE_ORDER_STATE_CHART, 11U);
	segments[1].domain.domain.value =
		Stable(SG_RUNE_ORDER_STATE_DOMAIN, 13U);
	CHECK(SG_DestinationTerminalValid(&terminal));
	segments[1].valid_from_ms = 199U;
	CHECK(!SG_DestinationTerminalValid(&terminal));
	segments[1].valid_from_ms = 200U;
	segments[1].valid_until_ms = 200U;
	CHECK(!SG_DestinationTerminalValid(&terminal));
	segments[1].valid_until_ms = 300U;
	terminal.value.moving_tube.trajectory_identity = 0U;
	CHECK(!SG_DestinationTerminalValid(&terminal));
}

static void TestResolvedDestinationBoundaries(void)
{
	sg_destination_handle_t handle = {
		.id = 1U,
		.generation = 2U,
		.kind = SG_DESTINATION_WAYPOINT,
		.motion = SG_DESTINATION_STATIC,
		.valid = 1U,
		.pose = {
			.phase = { 3U, 4U },
			.region_id = 5U
		}
	};

	CHECK(SG_DestinationHandleValid(&handle));
	handle.reserved[2] = 1U;
	CHECK(!SG_DestinationHandleValid(&handle));
	handle.reserved[2] = 0U;
	handle.pose.region_id = SG_DESTINATION_NO_REGION;
	CHECK(!SG_DestinationHandleValid(&handle));
}

int main(void)
{
	TestStaticPatch();
	TestMovingTube();
	TestResolvedDestinationBoundaries();
	if (failures != 0)
	{
		fprintf(stderr, "sg_destination_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("sg_destination_test: ok");
	return 0;
}
