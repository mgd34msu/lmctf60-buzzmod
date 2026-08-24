#include "../q_shared.h"
#include "sg_train_station_board_path.h"

#include <math.h>

#define BOARD_CLEARANCE 16.125f
#define BOARD_STAGE_RADIUS 8.0f

static int Finite3(const vec3_t value)
{
	return value && isfinite(value[0]) && isfinite(value[1]) &&
		isfinite(value[2]);
}

static int SourceSide(const float source[3], const float bounds[4])
{
	float gaps[SG_TRAIN_STATION_BOARD_SIDES] = {
		bounds[0] - source[0], source[0] - bounds[1],
		bounds[2] - source[1], source[1] - bounds[3]
	};
	int side = -1;
	int i;

	for (i = 0; i < SG_TRAIN_STATION_BOARD_SIDES; i++)
		if (gaps[i] >= 0.0f && (side < 0 || gaps[i] < gaps[side]))
			side = i;
	return side;
}

static void AppendAxisApproach(const float source[3], int source_side,
	const float target[3], sg_train_station_board_path_t *path)
{
	float corner[3];

	VectorCopy(target, corner);
	if (source_side < 2)
		corner[0] = source[0];
	else
		corner[1] = source[1];
	if ((corner[0] != source[0] || corner[1] != source[1]) &&
	    (corner[0] != target[0] || corner[1] != target[1]))
	{
		VectorCopy(corner, path->points[path->count]);
		path->count++;
	}
	VectorCopy(target, path->points[path->count]);
	path->count++;
}

static void SideEntry(int side, const float bounds[4],
	const float interior[3], float entry[3])
{
	VectorCopy(interior, entry);
	if (side == 0) entry[0] = bounds[0];
	else if (side == 1) entry[0] = bounds[1];
	else if (side == 2) entry[1] = bounds[2];
	else entry[1] = bounds[3];
}

static void SharedCorner(int first, int second, const float bounds[4],
	float z, float corner[3])
{
	corner[0] = first == 0 || second == 0 ? bounds[0] : bounds[1];
	corner[1] = first == 2 || second == 2 ? bounds[2] : bounds[3];
	corner[2] = z;
}

static float SegmentLength(const float first[3], const float second[3])
{
	float x = second[0] - first[0];
	float y = second[1] - first[1];

	return sqrtf(x * x + y * y);
}

int SG_TrainStationApproachPathBuild(const float source[3],
	const float anchor[3], sg_train_station_board_path_t *path)
{
	vec3_t corner;
	float dx;
	float dy;

	if (path)
		memset(path, 0, sizeof(*path));
	if (!Finite3(source) || !Finite3(anchor) || !path)
		return false;
	dx = fabsf(anchor[0] - source[0]);
	dy = fabsf(anchor[1] - source[1]);
	if (dx == 0.0f && dy == 0.0f && anchor[2] == source[2])
		return false;
	VectorCopy(anchor, corner);
	if (dx != 0.0f && dy != 0.0f)
	{
		if (dy < dx)
			corner[0] = source[0];
		else
			corner[1] = source[1];
		corner[2] = source[2];
		VectorCopy(corner, path->points[path->count]);
		path->count++;
	}
	VectorCopy(anchor, path->points[path->count]);
	path->count++;
	return true;
}

int SG_TrainStationBoardPathBuild(const float source[3],
	const float train_absmin[3], const float train_absmax[3],
	const float interior[3], int side, sg_train_station_board_path_t *path)
{
	float bounds[4];
	float entry[3];
	int source_side;

	if (path)
		memset(path, 0, sizeof(*path));
	if (!Finite3(source) || !Finite3(train_absmin) ||
	    !Finite3(train_absmax) || !Finite3(interior) || !path ||
	    side < 0 || side >= SG_TRAIN_STATION_BOARD_SIDES ||
	    train_absmin[0] >= train_absmax[0] ||
	    train_absmin[1] >= train_absmax[1] ||
	    train_absmin[2] >= train_absmax[2])
		return false;
	bounds[0] = train_absmin[0] - BOARD_CLEARANCE;
	bounds[1] = train_absmax[0] + BOARD_CLEARANCE;
	bounds[2] = train_absmin[1] - BOARD_CLEARANCE;
	bounds[3] = train_absmax[1] + BOARD_CLEARANCE;
	source_side = SourceSide(source, bounds);
	if (source_side < 0)
		return false;
	SideEntry(side, bounds, interior, entry);
	if (source_side == side)
	{
		AppendAxisApproach(source, source_side, entry, path);
	}
	else if ((source_side < 2) != (side < 2))
	{
		float shared[3];

		SharedCorner(source_side, side, bounds, source[2], shared);
		AppendAxisApproach(source, source_side, shared, path);
		VectorCopy(entry, path->points[path->count]);
		path->count++;
	}
	else
	{
		int via_a = source_side < 2 ? 2 : 0;
		int via_b = source_side < 2 ? 3 : 1;
		float a0[3], a1[3], b0[3], b1[3];
		float length_a, length_b;
		int via;

		SharedCorner(source_side, via_a, bounds, source[2], a0);
		SharedCorner(side, via_a, bounds, source[2], a1);
		SharedCorner(source_side, via_b, bounds, source[2], b0);
		SharedCorner(side, via_b, bounds, source[2], b1);
		length_a = SegmentLength(source, a0) + SegmentLength(a0, a1) +
			SegmentLength(a1, entry);
		length_b = SegmentLength(source, b0) + SegmentLength(b0, b1) +
			SegmentLength(b1, entry);
		via = length_a <= length_b ? via_a : via_b;
		SharedCorner(source_side, via, bounds, source[2], a0);
		AppendAxisApproach(source, source_side, a0, path);
		SharedCorner(side, via, bounds, source[2],
			path->points[path->count++]);
		VectorCopy(entry, path->points[path->count]);
		path->count++;
	}
	VectorCopy(interior, path->points[path->count]);
	path->count++;
	return true;
}

int SG_TrainStationBoardPathBuildCanonical(const float source[3],
	const float train_absmin[3], const float train_absmax[3],
	const float interior[3], sg_train_station_board_path_t *path)
{
	float bounds[4];
	int side;

	if (!Finite3(source) || !Finite3(train_absmin) ||
	    !Finite3(train_absmax) || !Finite3(interior) || !path)
		return false;
	bounds[0] = train_absmin[0] - BOARD_CLEARANCE;
	bounds[1] = train_absmax[0] + BOARD_CLEARANCE;
	bounds[2] = train_absmin[1] - BOARD_CLEARANCE;
	bounds[3] = train_absmax[1] + BOARD_CLEARANCE;
	side = SourceSide(source, bounds);
	return side >= 0 && SG_TrainStationBoardPathBuild(source, train_absmin,
		train_absmax, interior, side, path);
}

int SG_TrainStationBoardPathSideOrder(const float source[3],
	const float train_absmin[3], const float train_absmax[3],
	const float interior[3], int sides[SG_TRAIN_STATION_BOARD_SIDES])
{
	float lengths[SG_TRAIN_STATION_BOARD_SIDES];
	int side;

	if (!sides)
		return false;
	for (side = 0; side < SG_TRAIN_STATION_BOARD_SIDES; side++)
	{
		sg_train_station_board_path_t path;
		const float *previous = source;
		unsigned point;

		if (!SG_TrainStationBoardPathBuild(source, train_absmin,
		        train_absmax, interior, side, &path))
			return false;
		sides[side] = side;
		lengths[side] = 0.0f;
		for (point = 0U; point < path.count; point++)
		{
			lengths[side] += SegmentLength(previous, path.points[point]);
			previous = path.points[point];
		}
	}
	for (side = 1; side < SG_TRAIN_STATION_BOARD_SIDES; side++)
	{
		int value = sides[side];
		int at = side;

		while (at > 0 && (lengths[value] < lengths[sides[at - 1]] ||
		       (lengths[value] == lengths[sides[at - 1]] &&
		        value < sides[at - 1])))
		{
			sides[at] = sides[at - 1];
			at--;
		}
		sides[at] = value;
	}
	return true;
}

void SG_TrainStationBoardPathNextTarget(const float current[3],
	sg_train_station_board_path_t *path, float target_out[3])
{
	vec3_t delta;

	if (!target_out)
		return;
	VectorClear(target_out);
	if (!Finite3(current) || !path || path->count == 0U ||
	    path->count > SG_TRAIN_STATION_BOARD_PATH_POINTS ||
	    path->next >= path->count)
		return;
	while (path->next + 1U < path->count)
	{
		VectorSubtract(path->points[path->next], current, delta);
		delta[2] = 0.0f;
		if (DotProduct(delta, delta) > BOARD_STAGE_RADIUS * BOARD_STAGE_RADIUS)
			break;
		path->next++;
	}
	VectorCopy(path->points[path->next], target_out);
}
