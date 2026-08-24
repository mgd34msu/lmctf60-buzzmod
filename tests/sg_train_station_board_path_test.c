#include "q_shared.h"
#include "slipgate/sg_train_station_board_path.h"

#include <math.h>
#include <stdio.h>

static int failures;

#define CHECK(condition_) do { \
	if (!(condition_)) { \
		fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, \
			#condition_); \
		failures++; \
	} \
} while (0)

static int Near(const vec3_t actual, float x, float y, float z)
{
	return fabsf(actual[0] - x) < 0.001f &&
		fabsf(actual[1] - y) < 0.001f &&
		fabsf(actual[2] - z) < 0.001f;
}

static void TestPerimeterRoutesEnumerateNearestSideFirst(void)
{
	vec3_t source = { 68.0f, -160.0f, -132.0f };
	vec3_t absmin = { -64.0f, -64.0f, -204.0f };
	vec3_t absmax = { 200.0f, 192.0f, 96.0f };
	vec3_t interior = { 68.0f, 64.0f, -132.0f };
	int sides[SG_TRAIN_STATION_BOARD_SIDES];
	sg_train_station_board_path_t path;
	vec3_t target;

	CHECK(SG_TrainStationBoardPathSideOrder(source, absmin, absmax,
		interior, sides));
	CHECK(sides[0] == 2);
	CHECK(SG_TrainStationBoardPathBuild(source, absmin, absmax,
		interior, sides[0], &path));
	CHECK(path.count == 2U);
	CHECK(Near(path.points[0], 68.0f, -80.125f, -132.0f));
	CHECK(Near(path.points[1], 68.0f, 64.0f, -132.0f));
	SG_TrainStationBoardPathNextTarget(source, &path, target);
	CHECK(Near(target, 68.0f, -80.125f, -132.0f));
	SG_TrainStationBoardPathNextTarget(path.points[0], &path, target);
	CHECK(Near(target, 68.0f, 64.0f, -132.0f));
}

static void TestAlternateSideRoutesAroundSolidCarriageSide(void)
{
	vec3_t source = { 68.0f, -160.0f, -132.0f };
	vec3_t absmin = { -64.0f, -64.0f, -204.0f };
	vec3_t absmax = { 200.0f, 192.0f, 96.0f };
	vec3_t interior = { 68.0f, 64.0f, -132.0f };
	sg_train_station_board_path_t path;
	vec3_t target;

	CHECK(SG_TrainStationBoardPathBuild(source, absmin, absmax,
		interior, 0, &path));
	CHECK(path.count == 4U);
	CHECK(Near(path.points[0], -80.125f, -160.0f, -132.0f));
	CHECK(Near(path.points[1], -80.125f, -80.125f, -132.0f));
	CHECK(Near(path.points[2], -80.125f, 64.0f, -132.0f));
	CHECK(Near(path.points[3], 68.0f, 64.0f, -132.0f));
	SG_TrainStationBoardPathNextTarget(path.points[0], &path, target);
	CHECK(Near(target, -80.125f, -80.125f, -132.0f));
	SG_TrainStationBoardPathNextTarget(source, &path, target);
	CHECK(Near(target, -80.125f, -80.125f, -132.0f));
	SG_TrainStationBoardPathNextTarget(path.points[1], &path, target);
	CHECK(Near(target, -80.125f, 64.0f, -132.0f));
	CHECK(SG_TrainStationBoardPathBuild(source, absmin, absmax,
		interior, 1, &path));
	CHECK(path.count == 4U);
	CHECK(Near(path.points[0], 216.125f, -160.0f, -132.0f));
	CHECK(SG_TrainStationBoardPathBuild(source, absmin, absmax,
		interior, 3, &path));
	CHECK(path.count == 5U);
	CHECK(Near(path.points[0], -80.125f, -160.0f, -132.0f));
	CHECK(Near(path.points[1], -80.125f, -80.125f, -132.0f));
	CHECK(!SG_TrainStationBoardPathBuild(source, absmin, absmax,
		interior, SG_TRAIN_STATION_BOARD_SIDES, &path));
	CHECK(path.count == 0U);
	absmax[0] = absmin[0];
	CHECK(!SG_TrainStationBoardPathBuild(source, absmin, absmax,
		interior, 0, &path));
	CHECK(path.count == 0U);
}

static void TestLmctf25LCornerUsesAxisAlignedWestApproach(void)
{
	vec3_t source = { -160.0f, -320.0f, 120.125f };
	vec3_t absmin = { -65.0f, -65.0f, -205.0f };
	vec3_t absmax = { 203.0f, 195.0f, 99.0f };
	vec3_t contact = { -48.875f, -48.875f, 120.125f };
	sg_train_station_board_path_t path;

	CHECK(SG_TrainStationBoardPathBuildCanonical(source, absmin, absmax,
		contact, &path));
	CHECK(path.count == 3U);
	CHECK(Near(path.points[0], -160.0f, -48.875f, 120.125f));
	CHECK(Near(path.points[1], -81.125f, -48.875f, 120.125f));
	CHECK(Near(path.points[2], -48.875f, -48.875f, 120.125f));
}

static void TestLmctf25DryEndpointStagesSouthThenWest(void)
{
	vec3_t source = { 352.0f, -192.0f, 120.125f };
	vec3_t anchor = { -160.0f, -320.0f, 120.125f };
	sg_train_station_board_path_t path;

	CHECK(SG_TrainStationApproachPathBuild(source, anchor, &path));
	CHECK(path.count == 2U);
	CHECK(Near(path.points[0], 352.0f, -320.0f, 120.125f));
	CHECK(Near(path.points[1], -160.0f, -320.0f, 120.125f));
}

int main(void)
{
	TestPerimeterRoutesEnumerateNearestSideFirst();
	TestAlternateSideRoutesAroundSolidCarriageSide();
	TestLmctf25LCornerUsesAxisAlignedWestApproach();
	TestLmctf25DryEndpointStagesSouthThenWest();
	if (failures)
		return 1;
	puts("sg_train_station_board_path_test: PASS");
	return 0;
}
