#include "../g_local.h"
#include "sg_local.h"
#include "sg_oracle_internal.h"

#include <float.h>
#include <math.h>
#include <string.h>

#define SG_ROTATOR_X_AXIS 4
#define SG_ROTATOR_Y_AXIS 8
#define SG_ROTATOR_TRACE_EPSILON (1.0 / 32.0)
#define SG_ROTATOR_FLOAT_ENVELOPE_ULPS 64.0

static qboolean OracleRotatorFinite3(const vec3_t value)
{
	return isfinite(value[0]) && isfinite(value[1]) && isfinite(value[2]);
}

static double SG_OraclePointSegmentDistance2(const vec3_t point,
	const vec3_t start, const vec3_t end)
{
	double dx = (double)end[0] - (double)start[0];
	double dy = (double)end[1] - (double)start[1];
	double dz = (double)end[2] - (double)start[2];
	double px = (double)point[0] - (double)start[0];
	double py = (double)point[1] - (double)start[1];
	double pz = (double)point[2] - (double)start[2];
	double length2 = dx * dx + dy * dy + dz * dz;
	double t, ex, ey, ez;

	if (!isfinite(length2))
		return -1.0;
	if (length2 == 0.0)
		return px * px + py * py + pz * pz;
	t = (px * dx + py * dy + pz * dz) / length2;
	if (!isfinite(t))
		return -1.0;
	if (t < 0.0)
		t = 0.0;
	else if (t > 1.0)
		t = 1.0;
	ex = (double)start[0] + t * dx;
	ey = (double)start[1] + t * dy;
	ez = (double)start[2] + t * dz;
	ex = (double)point[0] - ex;
	ey = (double)point[1] - ey;
	ez = (double)point[2] - ez;
	return ex * ex + ey * ey + ez * ez;
}

static double SG_OracleRotatorTracePad(const edict_t *rotator,
	const vec3_t start, const vec3_t end, const vec3_t hull_mins,
	const vec3_t hull_maxs)
{
	double brush_radius2 = 0.0, hull_radius2 = 0.0;
	double coordinate_scale = 0.0, scale, pad;
	int corner, axis;

	for (axis = 0; axis < 3; axis++)
	{
		double values[5];
		int value;

		values[0] = fabs((double)start[axis]);
		values[1] = fabs((double)end[axis]);
		values[2] = fabs((double)rotator->s.origin[axis]);
		values[3] = fabs((double)hull_mins[axis]);
		values[4] = fabs((double)hull_maxs[axis]);
		for (value = 0; value < 5; value++)
			if (values[value] > coordinate_scale)
				coordinate_scale = values[value];
	}
	for (corner = 0; corner < 8; corner++)
	{
		double length2 = 0.0;

		for (axis = 0; axis < 3; axis++)
		{
			double value = (corner & (1 << axis)) ?
				(double)rotator->maxs[axis] : (double)rotator->mins[axis];

			length2 += value * value;
		}
		if (length2 > brush_radius2)
			brush_radius2 = length2;
	}
	for (axis = 0; axis < 3; axis++)
	{
		double lo = fabs((double)hull_mins[axis]);
		double hi = fabs((double)hull_maxs[axis]);
		double value = lo > hi ? lo : hi;

		hull_radius2 += value * value;
	}
	if (!isfinite(coordinate_scale) || !isfinite(brush_radius2) ||
	    !isfinite(hull_radius2))
		return -1.0;
	/* Include the full brush radius, not only the inner radius: a float
	 * transformed corner can perturb the nearest annulus boundary by the
	 * magnitude of every local component. */
	scale = coordinate_scale + sqrt(brush_radius2) + sqrt(hull_radius2);
	if (!isfinite(scale))
		return -1.0;
	if (scale < 1.0)
		scale = 1.0;
	pad = SG_ROTATOR_TRACE_EPSILON +
	      SG_ROTATOR_FLOAT_ENVELOPE_ULPS * (double)FLT_EPSILON * scale;
	return isfinite(pad) ? nextafter(pad, INFINITY) : -1.0;
}

static qboolean SG_OracleRotatorSphereBlocks(const edict_t *rotator,
	const vec3_t start, const vec3_t end, const vec3_t hull_mins,
	const vec3_t hull_maxs, double trace_pad)
{
	double brush_radius2 = 0.0, hull_radius2 = 0.0, radius, distance2;
	int corner, axis;

	for (corner = 0; corner < 8; corner++)
	{
		double length2 = 0.0;

		for (axis = 0; axis < 3; axis++)
		{
			double value = (corner & (1 << axis)) ?
				(double)rotator->maxs[axis] : (double)rotator->mins[axis];

			length2 += value * value;
		}
		if (length2 > brush_radius2)
			brush_radius2 = length2;
	}
	for (axis = 0; axis < 3; axis++)
	{
		double lo = fabs((double)hull_mins[axis]);
		double hi = fabs((double)hull_maxs[axis]);
		double value = lo > hi ? lo : hi;

		hull_radius2 += value * value;
	}
	radius = sqrt(brush_radius2) + sqrt(hull_radius2) + trace_pad;
	distance2 = SG_OraclePointSegmentDistance2(rotator->s.origin, start, end);
	if (!isfinite(radius) || !isfinite(distance2) || distance2 < 0.0)
		return true;
	/* A tangent is occupied.  Move the sphere out by an ULP before squaring,
	 * then round that squared boundary outward too; float bounds otherwise
	 * make an exact brush corner spuriously clear on some libm builds. */
	radius = nextafter(radius, INFINITY);
	return distance2 <= nextafter(radius * radius, INFINITY);
}

static qboolean SG_OracleRotatorCanonicalAxis(const edict_t *rotator,
	int *axial_axis)
{
	qboolean x_axis = (rotator->spawnflags & SG_ROTATOR_X_AXIS) != 0;
	qboolean y_axis = (rotator->spawnflags & SG_ROTATOR_Y_AXIS) != 0;
	int dynamic_axis;

	if (x_axis && y_axis)
		return false;
	if (x_axis)
	{
		*axial_axis = 0;
		dynamic_axis = 2;              /* roll around X */
	}
	else if (y_axis)
	{
		*axial_axis = 1;
		dynamic_axis = 0;              /* pitch around Y */
	}
	else
	{
		*axial_axis = 2;
		dynamic_axis = 1;              /* yaw around Z */
	}
	return (dynamic_axis == 0 || rotator->s.angles[0] == 0.0f) &&
	       (dynamic_axis == 1 || rotator->s.angles[1] == 0.0f) &&
	       (dynamic_axis == 2 || rotator->s.angles[2] == 0.0f);
}

/* A full-turn canonical rotator occupies an axial slab and a perpendicular
 * radial annulus.  This intentionally ignores the dynamic Euler component:
 * topology and exposure must not change as the brush turns.  The fixed-axis
 * checks above keep the cheap annulus exact for the three engine mappings;
 * unusual geometry falls back to a conservative pivot sphere below. */
static qboolean SG_OracleRotatorAnnulusBlocks(const edict_t *rotator,
	const vec3_t start, const vec3_t end, const vec3_t hull_mins,
	const vec3_t hull_maxs, int axial_axis, double trace_pad)
{
	int u = (axial_axis + 1) % 3;
	int v = (axial_axis + 2) % 3;
	double delta_axis, low, high, lo = 0.0, hi = 1.0;
	double inner2 = 0.0, outer2 = 0.0, hull_radius;
	double q0, q1, qvertex, qmin, qmax, qdelta_u, qdelta_v;
	double radial_u, radial_v;
	int corner;

	low = (double)rotator->s.origin[axial_axis] +
	      (double)rotator->mins[axial_axis] - (double)hull_maxs[axial_axis] -
	      trace_pad;
	high = (double)rotator->s.origin[axial_axis] +
	       (double)rotator->maxs[axial_axis] - (double)hull_mins[axial_axis] +
	       trace_pad;
	delta_axis = (double)end[axial_axis] - (double)start[axial_axis];
	if (!isfinite(low) || !isfinite(high) || !isfinite(delta_axis))
		return true;
	if (delta_axis == 0.0)
	{
		if (start[axial_axis] < low || start[axial_axis] > high)
			return false;
	}
	else
	{
		double at_low = (low - (double)start[axial_axis]) / delta_axis;
		double at_high = (high - (double)start[axial_axis]) / delta_axis;
		double enter = at_low < at_high ? at_low : at_high;
		double leave = at_low > at_high ? at_low : at_high;

		if (enter > lo)
			lo = enter;
		if (leave < hi)
			hi = leave;
		if (!isfinite(enter) || !isfinite(leave))
			return true;
		if (lo > hi || hi < 0.0 || lo > 1.0)
			return false;
		if (lo < 0.0)
			lo = 0.0;
		if (hi > 1.0)
			hi = 1.0;
	}

	/* Distance from the origin to the unrotated perpendicular rectangle. */
	if (rotator->mins[u] > 0.0f)
		radial_u = (double)rotator->mins[u];
	else if (rotator->maxs[u] < 0.0f)
		radial_u = (double)rotator->maxs[u];
	else
		radial_u = 0.0;
	if (rotator->mins[v] > 0.0f)
		radial_v = (double)rotator->mins[v];
	else if (rotator->maxs[v] < 0.0f)
		radial_v = (double)rotator->maxs[v];
	else
		radial_v = 0.0;
	inner2 = radial_u * radial_u + radial_v * radial_v;
	for (corner = 0; corner < 4; corner++)
	{
		double du = (corner & 1) ? (double)rotator->maxs[u] :
			(double)rotator->mins[u];
		double dv = (corner & 2) ? (double)rotator->maxs[v] :
			(double)rotator->mins[v];
		double length2 = du * du + dv * dv;

		if (length2 > outer2)
			outer2 = length2;
	}
	hull_radius = hypot(
		fabs((double)hull_mins[u]) > fabs((double)hull_maxs[u]) ?
		fabs((double)hull_mins[u]) : fabs((double)hull_maxs[u]),
		fabs((double)hull_mins[v]) > fabs((double)hull_maxs[v]) ?
		fabs((double)hull_mins[v]) : fabs((double)hull_maxs[v]));
	{
		if (!isfinite(inner2) || !isfinite(outer2) ||
		    !isfinite(hull_radius))
			return true;
		double inner = sqrt(inner2) - hull_radius -
		               trace_pad;
		double outer = sqrt(outer2) + hull_radius +
		               trace_pad;

		if (!isfinite(inner) || !isfinite(outer))
			return true;
		/* Grow the occupied annulus at both radial boundaries.  An exact
		 * tangent blocks, and a one-ULP libm rounding error must never turn
		 * that into a phase-dependent clear trace. */
		if (inner > 0.0)
			inner = nextafter(inner, -INFINITY);
		else
			inner = 0.0;
		outer = nextafter(outer, INFINITY);
		inner2 = inner > 0.0 ? nextafter(inner * inner, -INFINITY) : 0.0;
		outer2 = nextafter(outer * outer, INFINITY);
	}

	qdelta_u = (double)end[u] - (double)start[u];
	qdelta_v = (double)end[v] - (double)start[v];
	/* q0/q1 are only endpoints of the slab-clipped segment below. */
	{
		double pu0 = (double)start[u] - (double)rotator->s.origin[u] +
			lo * qdelta_u;
		double pv0 = (double)start[v] - (double)rotator->s.origin[v] +
			lo * qdelta_v;
		double pu1 = (double)start[u] - (double)rotator->s.origin[u] +
			hi * qdelta_u;
		double pv1 = (double)start[v] - (double)rotator->s.origin[v] +
			hi * qdelta_v;
		double denom = qdelta_u * qdelta_u + qdelta_v * qdelta_v;

		q0 = pu0 * pu0 + pv0 * pv0;
		q1 = pu1 * pu1 + pv1 * pv1;
		qmin = q0 < q1 ? q0 : q1;
		qmax = q0 > q1 ? q0 : q1;
		if (!isfinite(q0) || !isfinite(q1) || !isfinite(denom))
			return true;
		if (denom > 0.0)
		{
			double t = -(((double)start[u] - (double)rotator->s.origin[u]) *
			             qdelta_u + ((double)start[v] -
			             (double)rotator->s.origin[v]) * qdelta_v) / denom;

			if (t < lo)
				t = lo;
			else if (t > hi)
				t = hi;
			if (!isfinite(t))
				return true;
			qvertex = (double)start[u] - (double)rotator->s.origin[u] +
			          t * qdelta_u;
			qvertex = qvertex * qvertex;
			{
				double pv = (double)start[v] - (double)rotator->s.origin[v] +
				            t * qdelta_v;
				qvertex += pv * pv;
			}
			if (!isfinite(qvertex))
				return true;
			if (qvertex < qmin)
				qmin = qvertex;
		}
	}
	if (!isfinite(qmin) || !isfinite(qmax) || !isfinite(inner2) ||
	    !isfinite(outer2))
		return true;
	return qmin <= outer2 && qmax >= inner2;
}

qboolean SG_OracleRotatorEntitySweepBlocks(const edict_t *rotator,
	const vec3_t start,
	const vec3_t hull_mins, const vec3_t hull_maxs, const vec3_t end,
	int contentmask)
{
	static const vec3_t zero = { 0.0f, 0.0f, 0.0f };
	const vec_t *active_hull_mins;
	const vec_t *active_hull_maxs;
	double trace_pad;
	int axial_axis;

	if (!(contentmask & CONTENTS_SOLID))
		return false;
	if (!rotator || !start || !end || !OracleRotatorFinite3(start) ||
	    !OracleRotatorFinite3(end) ||
	    (!!hull_mins != !!hull_maxs) ||
	    (hull_mins && (!OracleRotatorFinite3(hull_mins) ||
	                   !OracleRotatorFinite3(hull_maxs) ||
	                   hull_mins[0] > hull_maxs[0] ||
	                   hull_mins[1] > hull_maxs[1] ||
	                   hull_mins[2] > hull_maxs[2])))
		return true;
	if (rotator->solid != SOLID_BSP || !rotator->classname ||
	    strcmp(rotator->classname, "func_rotating"))
		return false;
	if (!OracleRotatorFinite3(rotator->s.origin) ||
	    !OracleRotatorFinite3(rotator->mins) ||
	    !OracleRotatorFinite3(rotator->maxs) ||
	    rotator->mins[0] > rotator->maxs[0] ||
	    rotator->mins[1] > rotator->maxs[1] ||
	    rotator->mins[2] > rotator->maxs[2])
		return true;
	active_hull_mins = hull_mins ? hull_mins : zero;
	active_hull_maxs = hull_maxs ? hull_maxs : zero;
	trace_pad = SG_OracleRotatorTracePad(rotator, start, end,
	                                      active_hull_mins, active_hull_maxs);
	if (trace_pad < 0.0)
		return true;
	if (SG_OracleRotatorCanonicalAxis(rotator, &axial_axis))
		return SG_OracleRotatorAnnulusBlocks(rotator, start, end,
		    active_hull_mins, active_hull_maxs, axial_axis, trace_pad);
	return SG_OracleRotatorSphereBlocks(rotator, start, end,
	    active_hull_mins, active_hull_maxs, trace_pad);
}

qboolean SG_OracleRotatorSweepBlocks(const vec3_t start,
	const vec3_t hull_mins, const vec3_t hull_maxs, const vec3_t end,
	int contentmask)
{
	int i;

	if (!(contentmask & CONTENTS_SOLID))
		return false;
	if (!start || !end || !OracleRotatorFinite3(start) || !OracleRotatorFinite3(end) ||
	    (!!hull_mins != !!hull_maxs) ||
	    (hull_mins && (!OracleRotatorFinite3(hull_mins) ||
	                   !OracleRotatorFinite3(hull_maxs) ||
	                   hull_mins[0] > hull_maxs[0] ||
	                   hull_mins[1] > hull_maxs[1] ||
	                   hull_mins[2] > hull_maxs[2])))
		return true;
	if (!g_edicts || globals.num_edicts < 0)
		return true;
	for (i = 0; i < globals.num_edicts; i++)
	{
		edict_t *rotator = &g_edicts[i];

		if (rotator->solid != SOLID_BSP || !rotator->classname ||
		    strcmp(rotator->classname, "func_rotating"))
			continue;
		if (SG_OracleRotatorEntitySweepBlocks(rotator, start, hull_mins,
		        hull_maxs, end, contentmask))
			return true;
	}
	return false;
}
