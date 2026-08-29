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

static void TestStaticPatch(void)
{
	sg_destination_terminal_t terminal = {
		.destination = Waypoint(),
		.generation = 2U,
		.kind = SG_DESTINATION_TERMINAL_STATIC_PATCH,
		.value.static_patch = {
			.domain = { 11U, 12U }
		}
	};

	CHECK(SG_DestinationTerminalValid(&terminal));
	terminal.value.static_patch.domain.domain_identity = 0U;
	CHECK(!SG_DestinationTerminalValid(&terminal));
	terminal.value.static_patch.domain.domain_identity = 12U;
	terminal.generation = 0U;
	CHECK(!SG_DestinationTerminalValid(&terminal));
}

static void TestMovingTube(void)
{
	sg_destination_tube_segment_t segments[2] = {
		{ 100U, 200U, { 11U, 12U } },
		{ 200U, 300U, { 11U, 13U } }
	};
	sg_destination_terminal_t terminal = {
		.destination = Waypoint(),
		.generation = 3U,
		.kind = SG_DESTINATION_TERMINAL_MOVING_TUBE,
		.value.moving_tube = { 21U, segments, 2U }
	};

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
	CHECK(SG_DestinationHandleValid(&handle));
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
