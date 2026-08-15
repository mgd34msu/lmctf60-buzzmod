/*
 * sg_oracle.c -- the physics, used as truth.
 *
 * Everything SLIPGATE knows about movement it knows because this file rolled
 * the engine's own Pmove forward and watched what happened. There is no model
 * of the physics anywhere in the system -- there is only the physics, called
 * on phantom state that belongs to no client and touches no entity.
 *
 * sg_host.pmove is the same function the server runs for every real player
 * (game.h:122, "player movement code common with client prediction"), with
 * the world queried through the trace and pointcontents callbacks we supply.
 * We pass the engine's own sg_host.trace against the live collision world.
 * Offline generation has no owner; a live reproof scopes its actual bot as
 * passent so the phantom cannot collide with the body whose state it cloned.
 *
 * The one thing Pmove does NOT simulate is the LMCTF grapple, which lives in
 * the game code (p_weapon.c). SG_OracleHookStep calls the same pure muzzle
 * transform and integer-truncated pull ladder as the production weapon; the
 * oracle no longer carries an almost-the-same body-origin/float-rope copy.
 */

#include <float.h>

#include "g_local.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_replay.h"
#include "slipgate/sg_rune_proof.h"
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_util.h"

void Touch_DoorTrigger(edict_t *self, edict_t *other, cplane_t *plane,
	csurface_t *surf);
void Touch_Multi(edict_t *self, edict_t *other, cplane_t *plane,
	csurface_t *surf);
void door_secret_use(edict_t *self, edict_t *other, edict_t *activator);
void door_use(edict_t *self, edict_t *other, edict_t *activator);
void door_go_down(edict_t *self);
void trigger_relay_use(edict_t *self, edict_t *other, edict_t *activator);
void Use_Target_Speaker(edict_t *self, edict_t *other, edict_t *activator);

/*
 * A phantom's trace must not pass through any entity: it models a player
 * moving through the world at generation time, when the only thing that is
 * reliably present is the world itself. Entities (doors especially) are
 * handled at the link level -- a link through a door area is tagged so the
 * runtime can re-validate it against the door's current state.
 */
static edict_t *sg_oracle_passent;
static sg_phantom_t *sg_oracle_active_phantom;
static qboolean sg_oracle_world_only;
static qboolean sg_oracle_contaminated;
static edict_t *sg_oracle_declared_expected;
static edict_t *sg_oracle_declared_door;
static int sg_oracle_declared_action;
static qboolean sg_oracle_declared_touched;
/* Rune loading may happen after humans have joined.  Its map-contract replay
 * must not depend on a transient client or that client's projectile occupying
 * the path at this instant; the normal offline/live proofs remain deliberately
 * conservative about those bodies. */
static qboolean sg_oracle_loader_replay;

static qboolean SG_OracleDeclaredActivatorSafe(edict_t *trigger);
static qboolean SG_OracleDeclaredSameDoorSet(edict_t *a, edict_t *b);
static qboolean SG_OracleTriggerOverlap(sg_phantom_t *ph);
static qboolean SG_OracleSolidOverlap(sg_phantom_t *ph);

/* func_rotating uses these private g_func.c spawnflag values. */
#define SG_ROTATOR_X_AXIS 4
#define SG_ROTATOR_Y_AXIS 8
/* The collision model backs a trace off by DIST_EPSILON (1/32).  This local
 * spelling keeps the phase-independent topology model conservative in the
 * same float/trace domain without importing engine-private headers. */
#define SG_ROTATOR_TRACE_EPSILON (1.0 / 32.0)
/* AngleVectors rounds sin/cos to float, then forms up to three products and
 * two sums per coordinate before CM consumes the result.  64 float epsilons
 * at the largest participating coordinate/radius covers those operations,
 * origin subtraction, and later radial dot products without pretending the
 * double oracle arithmetic is the engine's float collision domain. */
#define SG_ROTATOR_FLOAT_ENVELOPE_ULPS 64.0

/* door spawnflags from g_func.c; kept local so the movement oracle does not
 * depend on that implementation file's private macros. */
#define SG_DOOR_START_OPEN 1
#define SG_DOOR_CRUSHER 4
#define SG_DOOR_TOGGLE 32

static qboolean SG_OracleDeclaredActivatorSafe(edict_t *trigger);

static qboolean SG_OracleFinite3(const vec3_t value)
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

qboolean SG_OracleRotatorSweepBlocks(const vec3_t start,
	const vec3_t hull_mins, const vec3_t hull_maxs, const vec3_t end,
	int contentmask)
{
	static const vec3_t zero = { 0.0f, 0.0f, 0.0f };
	int i;

	if (!(contentmask & CONTENTS_SOLID))
		return false;
	if (!start || !end || !SG_OracleFinite3(start) || !SG_OracleFinite3(end) ||
	    (!!hull_mins != !!hull_maxs) ||
	    (hull_mins && (!SG_OracleFinite3(hull_mins) ||
	                   !SG_OracleFinite3(hull_maxs) ||
	                   hull_mins[0] > hull_maxs[0] ||
	                   hull_mins[1] > hull_maxs[1] ||
	                   hull_mins[2] > hull_maxs[2])))
		return true;
	if (!g_edicts || globals.num_edicts < 0)
		return true;
	for (i = 0; i < globals.num_edicts; i++)
	{
		edict_t *rotator = &g_edicts[i];
		const vec_t *active_hull_mins = hull_mins ? hull_mins : zero;
		const vec_t *active_hull_maxs = hull_maxs ? hull_maxs : zero;
		double trace_pad;
		int axial_axis;

		if (rotator->solid != SOLID_BSP || !rotator->classname ||
		    strcmp(rotator->classname, "func_rotating"))
			continue;
		if (!SG_OracleFinite3(rotator->s.origin) ||
		    !SG_OracleFinite3(rotator->mins) ||
		    !SG_OracleFinite3(rotator->maxs) ||
		    rotator->mins[0] > rotator->maxs[0] ||
		    rotator->mins[1] > rotator->maxs[1] ||
		    rotator->mins[2] > rotator->maxs[2])
			return true;
		trace_pad = SG_OracleRotatorTracePad(rotator, start, end,
		                                      active_hull_mins, active_hull_maxs);
		if (trace_pad < 0.0)
			return true;
		if (SG_OracleRotatorCanonicalAxis(rotator, &axial_axis))
		{
			if (SG_OracleRotatorAnnulusBlocks(rotator, start, end,
			                                active_hull_mins, active_hull_maxs,
			                                axial_axis, trace_pad))
				return true;
		}
		else if (SG_OracleRotatorSphereBlocks(rotator, start, end,
			                                active_hull_mins, active_hull_maxs,
			                                trace_pad))
			return true;
	}
	return false;
}

static qboolean SG_OracleDeclaredTrigger(edict_t *trigger)
{
	if (!trigger || !sg_oracle_declared_expected)
		return false;
	if (sg_oracle_declared_action == RL_TELEPORT)
		return trigger->owner == sg_oracle_declared_expected;
	if (sg_oracle_declared_action == RL_LIFT)
		return trigger->enemy == sg_oracle_declared_expected;
	if (sg_oracle_declared_action == RL_DOOR)
		return trigger == sg_oracle_declared_expected ||
		       SG_OracleDeclaredSameDoorSet(sg_oracle_declared_expected,
		                                    trigger);
	return false;
}

/* Generation makes doors nonsolid so a phantom can prove the body motion on
 * either side.  Executability still depends on when the body reaches a real
 * activator.  Use the union of every pose a door can occupy, then admit that
 * volume only after this particular phantom has touched a validated player
 * trigger.  This follows the rolled trajectory itself; an endpoint chord
 * cannot describe RUN waypoints, jumps, drops, swims, or hook arcs. */
static void SG_OracleRotatingPoint(edict_t *door, const vec3_t local,
	int angle_axis, float angle, vec3_t point)
{
	vec3_t angles, forward, right, up;

	VectorCopy(door->moveinfo.start_angles, angles);
	angles[angle_axis] = angle;
	AngleVectors(angles, forward, right, up);
	point[0] = door->s.origin[0] + local[0] * forward[0] -
	           local[1] * right[0] + local[2] * up[0];
	point[1] = door->s.origin[1] + local[0] * forward[1] -
	           local[1] * right[1] + local[2] * up[1];
	point[2] = door->s.origin[2] + local[0] * forward[2] -
	           local[1] * right[2] + local[2] * up[2];
}

static void SG_OracleBoundsAdd(const vec3_t point, vec3_t mins, vec3_t maxs)
{
	int axis;

	for (axis = 0; axis < 3; axis++)
	{
		if (point[axis] < mins[axis]) mins[axis] = point[axis];
		if (point[axis] > maxs[axis]) maxs[axis] = point[axis];
	}
}

/* A rotating door traverses one directed angular interval, not a complete
 * sphere.  The old radius cube made the intentionally safe side of a pair of
 * saloon doors look occupied even though both leaves rotate away from it.
 *
 * For one varying Euler component every transformed corner coordinate is
 * C + A*cos(theta) + B*sin(theta).  Evaluate both endpoints and every exact
 * derivative zero inside the interval.  Sweeping the local model AABB is
 * conservative for the brush while preserving which side of a partial arc is
 * genuinely clear.  Canonical func_door_rotating changes exactly one angle;
 * malformed multi-axis or excessive rotations fall back to the radius cube. */
static qboolean SG_OracleRotatingDoorBounds(edict_t *door,
	const vec3_t local_mins, const vec3_t local_maxs,
	vec3_t dmins, vec3_t dmaxs)
{
	const float rad = (float)(M_PI * 2.0 / 360.0);
	const float deg = (float)(360.0 / (M_PI * 2.0));
	int angle_axis = -1, changed = 0;
	float start, end, low, high;
	int corner, coord;

	for (coord = 0; coord < 3; coord++)
	{
		float delta = door->moveinfo.end_angles[coord] -
		              door->moveinfo.start_angles[coord];

		if (fabsf(delta) <= 0.001f)
			continue;
		angle_axis = coord;
		changed++;
	}
	if (changed != 1)
		return false;
	start = door->moveinfo.start_angles[angle_axis] * rad;
	end = door->moveinfo.end_angles[angle_axis] * rad;
	if (!isfinite(start) || !isfinite(end) || fabsf(end - start) > 8.0f * M_PI)
		return false;
	low = start < end ? start : end;
	high = start > end ? start : end;
	VectorSet(dmins, 1.0e30f, 1.0e30f, 1.0e30f);
	VectorSet(dmaxs, -1.0e30f, -1.0e30f, -1.0e30f);
	for (corner = 0; corner < 8; corner++)
	{
		vec3_t local, p0, p90, p180, point;

		local[0] = (corner & 1) ? local_maxs[0] : local_mins[0];
		local[1] = (corner & 2) ? local_maxs[1] : local_mins[1];
		local[2] = (corner & 4) ? local_maxs[2] : local_mins[2];
		SG_OracleRotatingPoint(door, local, angle_axis,
		                           door->moveinfo.start_angles[angle_axis], point);
		SG_OracleBoundsAdd(point, dmins, dmaxs);
		SG_OracleRotatingPoint(door, local, angle_axis,
		                           door->moveinfo.end_angles[angle_axis], point);
		SG_OracleBoundsAdd(point, dmins, dmaxs);

		/* Recover the sinusoid coefficients in the same AngleVectors transform
		 * the engine uses; this avoids hand-written pitch/roll sign conventions. */
		SG_OracleRotatingPoint(door, local, angle_axis, 0.0f, p0);
		SG_OracleRotatingPoint(door, local, angle_axis, 90.0f, p90);
		SG_OracleRotatingPoint(door, local, angle_axis, 180.0f, p180);
		for (coord = 0; coord < 3; coord++)
		{
			float c = 0.5f * (p0[coord] + p180[coord]);
			float a = p0[coord] - c;
			float b = p90[coord] - c;
			float base;
			int first, last, k;

			if (fabsf(a) + fabsf(b) <= 0.0001f)
				continue;
			base = atan2f(b, a);
			first = (int)ceilf((low - base) / (float)M_PI);
			last = (int)floorf((high - base) / (float)M_PI);
			for (k = first; k <= last; k++)
			{
				float at = base + (float)k * (float)M_PI;

				SG_OracleRotatingPoint(door, local, angle_axis,
				                           at * deg, point);
				SG_OracleBoundsAdd(point, dmins, dmaxs);
			}
		}
	}
	return true;
}

/* CM_TransformedBoxTrace rotates a trace origin into a rotating brush's local
 * frame but leaves the trace mins/maxs unchanged.  The occupied player-origin
 * volume is therefore the angular sweep of the local Minkowski box, not the
 * brush sweep expanded by a fixed world-axis hull after rotation. */
static void SG_OracleDoorBounds(edict_t *door,
	const vec3_t hull_mins, const vec3_t hull_maxs,
	vec3_t dmins, vec3_t dmaxs)
{
	int axis;

	if (!strcmp(door->classname, "func_door_rotating"))
	{
		vec3_t local_mins, local_maxs;
		float ext[3], radius;

		for (axis = 0; axis < 3; axis++)
		{
			float hmin = hull_mins ? hull_mins[axis] : 0.0f;
			float hmax = hull_maxs ? hull_maxs[axis] : 0.0f;

			local_mins[axis] = door->mins[axis] - hmax;
			local_maxs[axis] = door->maxs[axis] - hmin;
		}

		if (SG_OracleRotatingDoorBounds(door, local_mins, local_maxs,
		                                      dmins, dmaxs))
			return;
		for (axis = 0; axis < 3; axis++)
		{
			float lo = fabsf(local_mins[axis]);
			float hi = fabsf(local_maxs[axis]);

			ext[axis] = lo > hi ? lo : hi;
		}
		radius = sqrtf(ext[0] * ext[0] + ext[1] * ext[1] +
		               ext[2] * ext[2]);
		for (axis = 0; axis < 3; axis++)
		{
			dmins[axis] = door->s.origin[axis] - radius;
			dmaxs[axis] = door->s.origin[axis] + radius;
		}
		return;
	}

	VectorCopy(door->absmin, dmins);
	VectorCopy(door->absmax, dmaxs);
	for (axis = 0; axis < 3; axis++)
	{
		float smin = door->moveinfo.start_origin[axis] + door->mins[axis];
		float smax = door->moveinfo.start_origin[axis] + door->maxs[axis];
		float emin = door->moveinfo.end_origin[axis] + door->mins[axis];
		float emax = door->moveinfo.end_origin[axis] + door->maxs[axis];

		if (smin < dmins[axis]) dmins[axis] = smin;
		if (emin < dmins[axis]) dmins[axis] = emin;
		if (smax > dmaxs[axis]) dmaxs[axis] = smax;
		if (emax > dmaxs[axis]) dmaxs[axis] = emax;
	}
	if (door->use == door_secret_use)
	{
		for (axis = 0; axis < 3; axis++)
		{
			float p1min = door->pos1[axis] + door->mins[axis];
			float p1max = door->pos1[axis] + door->maxs[axis];
			float p2min = door->pos2[axis] + door->mins[axis];
			float p2max = door->pos2[axis] + door->maxs[axis];

			if (p1min < dmins[axis]) dmins[axis] = p1min;
			if (p2min < dmins[axis]) dmins[axis] = p2min;
			if (p1max > dmaxs[axis]) dmaxs[axis] = p1max;
			if (p2max > dmaxs[axis]) dmaxs[axis] = p2max;
		}
	}
	for (axis = 0; axis < 3; axis++)
	{
		float hmin = hull_mins ? hull_mins[axis] : 0.0f;
		float hmax = hull_maxs ? hull_maxs[axis] : 0.0f;

		dmins[axis] -= hmax;
		dmaxs[axis] -= hmin;
	}
}

static qboolean SG_OracleSegmentBox(const vec3_t start, const vec3_t end,
	const vec3_t mins, const vec3_t maxs)
{
	float low = 0.0f, high = 1.0f;
	int axis;

	for (axis = 0; axis < 3; axis++)
	{
		float delta = end[axis] - start[axis];

		if (fabsf(delta) < 0.001f)
		{
			if (start[axis] < mins[axis] || start[axis] > maxs[axis])
				return false;
		}
		else
		{
			float a = (mins[axis] - start[axis]) / delta;
			float b = (maxs[axis] - start[axis]) / delta;

			if (a > b) { float swap = a; a = b; b = swap; }
			if (a > low) low = a;
			if (b < high) high = b;
			if (low > high)
				return false;
		}
	}
	return true;
}

static qboolean SG_OracleDoorArmed(const sg_phantom_t *ph, edict_t *door)
{
	int i, index;

	if (!ph || !door)
		return false;
	index = (int)(door - g_edicts);
	for (i = 0; i < ph->armed_door_count; i++)
		if (ph->armed_door[i] == index)
			return true;
	return false;
}

static qboolean SG_OracleDoorMember(edict_t *master, edict_t *ent)
{
	edict_t *target = NULL, *member;

	if (!master || !ent)
		return false;
	/* RL_DOOR stores the unique trigger as its mechanism identity because one
	 * trigger may legitimately fire several unteamed leaves. */
	if (master->classname && !strcmp(master->classname, "trigger_multiple"))
	{
		while ((target = G_Find(target, FOFS(targetname), master->target)) != NULL)
		{
			edict_t *door_master;

			if (!target->classname ||
			    strncmp(target->classname, "func_door", 9) != 0)
				continue;
			door_master = target->teammaster ? target->teammaster : target;
			for (member = door_master; member; member = member->teamchain)
				if (member == ent)
					return true;
		}
		return false;
	}
	/* Think_SpawnDoorTrigger creates one anonymous trigger owned by the exact
	 * canonical door master.  It has no target string to resolve. */
	if (master->touch == Touch_DoorTrigger && master->owner)
	{
		edict_t *door_master = master->owner->teammaster
		    ? master->owner->teammaster : master->owner;

		for (member = door_master; member; member = member->teamchain)
			if (member == ent)
				return true;
		return false;
	}
	master = master->teammaster ? master->teammaster : master;
	for (member = master; member; member = member->teamchain)
		if (member == ent)
			return true;
	return false;
}

static qboolean SG_OracleDeclaredSetMember(edict_t *trigger, edict_t *ent)
{
	return SG_OracleDoorMember(trigger, ent);
}

/* Two player triggers are interchangeable during an already-open egress only
 * when their complete physical closure is identical.  LMCTF commonly places
 * a broad activator on each side of one rotating team; crossing the far-side
 * trigger is a harmless refresh, but sharing one member or one target string
 * is not enough to prove there are no extra movers. */
static int SG_OracleDeclaredDoorSet(edict_t *trigger, edict_t **set, int cap)
{
	edict_t *target = NULL;
	int count = 0;

	if (!SG_OracleDeclaredActivatorSafe(trigger) || !set || cap <= 0)
		return -1;
	if (trigger->touch == Touch_DoorTrigger)
	{
		set[0] = trigger->owner->teammaster
		    ? trigger->owner->teammaster : trigger->owner;
		return 1;
	}
	while ((target = G_Find(target, FOFS(targetname), trigger->target)) != NULL)
	{
		edict_t *master;
		int i;

		if (!target->classname ||
		    strncmp(target->classname, "func_door", 9) != 0)
			continue;
		master = target->teammaster ? target->teammaster : target;
		for (i = 0; i < count; i++)
			if (set[i] == master)
				break;
		if (i < count)
			continue;
		if (count >= cap)
			return -1;
		set[count++] = master;
	}
	return count;
}

static qboolean SG_OracleDeclaredSameDoorSet(edict_t *a, edict_t *b)
{
	edict_t *aset[SG_PHANTOM_ARMED_DOORS];
	edict_t *bset[SG_PHANTOM_ARMED_DOORS];
	int an, bn, i, j;

	if (a == b)
		return true;
	an = SG_OracleDeclaredDoorSet(a, aset, SG_PHANTOM_ARMED_DOORS);
	bn = SG_OracleDeclaredDoorSet(b, bset, SG_PHANTOM_ARMED_DOORS);
	if (an <= 0 || an != bn)
		return false;
	for (i = 0; i < an; i++)
	{
		for (j = 0; j < bn; j++)
			if (aset[i] == bset[j])
				break;
		if (j == bn)
			return false;
	}
	return true;
}

static qboolean SG_OracleDoorTraceBlocked(sg_phantom_t *ph,
	const vec3_t start, const vec3_t hull_mins, const vec3_t hull_maxs,
	const vec3_t end)
{
	int i, axis;

	for (i = 1; i < globals.num_edicts; i++)
	{
		edict_t *door = &g_edicts[i];
		vec3_t mins, maxs, open_mins, open_maxs;

		if (!door->inuse || !door->classname ||
		    strncmp(door->classname, "func_door", 9) != 0)
			continue;
		/* A declared door egress links this exact team at its real open pose.
		 * Pmove collides with those brushes normally; the broad synthetic sweep
		 * must not simultaneously pretend the same team is still closed. */
		if (SG_OracleDeclaredSetMember(sg_oracle_declared_door, door))
			continue;
		SG_OracleDoorBounds(door, hull_mins, hull_maxs, mins, maxs);
		/* A translating door remains a solid brush at its open destination.
		 * It is never safe to erase that pose merely because its trigger fired. */
		if (!strcmp(door->classname, "func_door") && door->use == door_use &&
		    !(door->spawnflags & SG_DOOR_START_OPEN))
		{
			for (axis = 0; axis < 3; axis++)
			{
				float hmin = hull_mins ? hull_mins[axis] : 0.0f;
				float hmax = hull_maxs ? hull_maxs[axis] : 0.0f;

				open_mins[axis] = door->moveinfo.end_origin[axis] +
				                  door->mins[axis] - hmax;
				open_maxs[axis] = door->moveinfo.end_origin[axis] +
				                  door->maxs[axis] - hmin;
			}
			if (SG_OracleSegmentBox(start, end, open_mins, open_maxs))
				return true;
		}
		if (!SG_OracleSegmentBox(start, end, mins, maxs))
			continue;
		if (!SG_OracleDoorArmed(ph, door))
			return true;
		if (ph)
			ph->door_passed = true;
	}
	return false;
}

static qboolean SG_OracleDoorOverlap(sg_phantom_t *ph)
{
	vec3_t mins = { -16.0f, -16.0f, -24.0f };
	vec3_t maxs = { 16.0f, 16.0f, 32.0f };

	return ph && SG_OracleDoorTraceBlocked(ph, ph->origin, mins, maxs,
	                                      ph->origin);
}

/* A target chain made only of speakers cannot change collision, movement, or
 * health.  It is therefore safe for a static movement rollout to ignore the
 * trigger's one-shot/wait bookkeeping.  Follow relays only when every branch
 * remains sound-only, reject killtargets, and bound recursion so malformed
 * cyclic maps fail closed. */
static qboolean SG_OracleSoundOnlyTargets(edict_t *source, int depth)
{
	edict_t *target = NULL;
	qboolean found = false;

	if (!source || depth > 4 ||
	    (source->killtarget && source->killtarget[0]) ||
	    !source->target || !source->target[0])
		return false;
	while ((target = G_Find(target, (int)offsetof(edict_t, targetname),
	                        source->target)) != NULL)
	{
		if (!target->inuse || !target->classname)
			return false;
		found = true;
		if (!Q_stricmp(target->classname, "target_speaker") &&
		    target->use == Use_Target_Speaker)
			continue;
		if (!Q_stricmp(target->classname, "trigger_relay") &&
		    target->use == trigger_relay_use &&
		    SG_OracleSoundOnlyTargets(target, depth + 1))
			continue;
		return false;
	}
	return found;
}

static qboolean SG_OracleDoorEffectsSafe(edict_t *door)
{
	edict_t *target = NULL;
	qboolean found = false;

	if (!door || (door->killtarget && door->killtarget[0]) ||
	    door->delay != 0.0f)
		return false;
	if (!door->target || !door->target[0])
		return true;
	while ((target = G_Find(target, FOFS(targetname), door->target)) != NULL)
	{
		if (!target->inuse || !target->classname)
			return false;
		found = true;
		/* G_UseTargets deliberately skips a door's func_areaportal; the door
		 * updates it through door_use_areaportals instead. */
		if (!Q_stricmp(target->classname, "func_areaportal"))
			continue;
		if (!Q_stricmp(target->classname, "target_speaker") &&
		    target->use == Use_Target_Speaker)
			continue;
		if (!Q_stricmp(target->classname, "trigger_relay") &&
		    target->use == trigger_relay_use &&
		    SG_OracleSoundOnlyTargets(target, 1))
			continue;
		return false;
	}
	return found;
}

/* The declared controller can reproduce a canonical rotating or translating
 * door, including CRUSHER/REVERSE geometry, because its body waits entirely
 * outside the complete sweep until STATE_TOP. Scripted/shot/toggle/start-open
 * mechanisms and physical side effects remain unrepresentable. */
static qboolean SG_OracleDeclaredDoorTeamSafe(edict_t *door)
{
	edict_t *master, *member;

	if (!door || !door->inuse || !door->classname ||
	    strncmp(door->classname, "func_door", 9) != 0)
		return false;
	master = door->teammaster ? door->teammaster : door;
	if (!master->inuse || (master->flags & FL_TEAMSLAVE) ||
	    master->use != door_use)
		return false;
	for (member = master; member; member = member->teamchain)
	{
		float travel;

		if (!member->inuse || !member->classname ||
		    (strcmp(member->classname, "func_door") != 0 &&
		     strcmp(member->classname, "func_door_rotating") != 0) ||
		    member->use != door_use || member->health > 0 ||
		    (member->spawnflags & (SG_DOOR_START_OPEN | SG_DOOR_TOGGLE)) ||
		    !isfinite(member->moveinfo.distance) ||
		    !isfinite(member->moveinfo.speed) || member->moveinfo.speed <= 0.0f ||
		    !isfinite(member->moveinfo.accel) ||
		    !isfinite(member->moveinfo.decel) ||
		    !isfinite(member->moveinfo.wait) ||
		    !SG_OracleDoorEffectsSafe(member))
			return false;
		if (!strcmp(member->classname, "func_door") &&
		    (fabsf(member->moveinfo.accel - member->moveinfo.speed) > 0.01f ||
		     fabsf(member->moveinfo.decel - member->moveinfo.speed) > 0.01f))
			return false; /* declared cost uses Move_Begin's constant-speed law */
		travel = fabsf(member->moveinfo.distance) / member->moveinfo.speed;
		if (!isfinite(travel) || travel <= 0.0f || travel > 12.0f)
			return false;
	}
	return true;
}

/* The runtime route can wait for a normal door, but it cannot reproduce an
 * arbitrary scripted mechanism. Require the canonical door use function, a
 * closed non-toggle start, finite motion, and sound/areaportal-only effects
 * for every member moved by the team master. */
static qboolean SG_OracleDoorTeamSafe(edict_t *door)
{
	edict_t *master, *member;

	if (!door || !door->inuse || !door->classname ||
	    strncmp(door->classname, "func_door", 9) != 0)
		return false;
	master = door->teammaster ? door->teammaster : door;
	if (!master->inuse || (master->flags & FL_TEAMSLAVE) ||
	    master->use != door_use ||
	    master->moveinfo.state != SG_PLAT_STATE_BOTTOM)
		return false;
	for (member = master; member; member = member->teamchain)
	{
		float dz = member->moveinfo.end_origin[2] -
		           member->moveinfo.start_origin[2];

		if (!member->inuse || !member->classname ||
		    strcmp(member->classname, "func_door") != 0 ||
		    member->use != door_use || member->health > 0 ||
		    (member->spawnflags & (SG_DOOR_START_OPEN | SG_DOOR_CRUSHER |
		                           SG_DOOR_TOGGLE)) ||
		    !isfinite(member->moveinfo.distance) ||
		    !isfinite(member->moveinfo.speed) || member->moveinfo.speed <= 0.0f ||
		    !isfinite(member->moveinfo.accel) ||
		    !isfinite(member->moveinfo.decel) ||
		    fabsf(member->moveinfo.accel - member->moveinfo.speed) > 0.01f ||
		    fabsf(member->moveinfo.decel - member->moveinfo.speed) > 0.01f ||
		    !isfinite(member->moveinfo.wait) || fabsf(dz) > 1.0f ||
		    !SG_OracleDoorEffectsSafe(member))
			return false;
	}
	return true;
}

static qboolean SG_OracleDeclaredActivatorSafe(edict_t *trigger)
{
	edict_t *target = NULL;
	edict_t *master;
	qboolean found = false;

	if (!trigger || !trigger->inuse || trigger->solid != SOLID_TRIGGER)
		return false;
	/* Canonical automatic door triggers have no mapper-controlled target
	 * closure: Think_SpawnDoorTrigger owns them directly from one safe master.
	 * Their fixed one-second debounce is accounted by the shared cost helper. */
	if (trigger->touch == Touch_DoorTrigger)
	{
		master = trigger->owner;
		if (!master || !master->inuse || (master->flags & FL_TEAMSLAVE))
			return false;
		master = master->teammaster ? master->teammaster : master;
		return trigger->movetype == MOVETYPE_NONE &&
		       SG_OracleDeclaredDoorTeamSafe(master);
	}
	if (!trigger->classname ||
	    strcmp(trigger->classname, "trigger_multiple") != 0 ||
	    trigger->touch != Touch_Multi ||
	    (trigger->spawnflags & (2 | 4)) || !isfinite(trigger->wait) ||
	    trigger->wait <= 0.0f ||
	    !VectorCompare(trigger->movedir, vec3_origin) ||
	    trigger->delay != 0.0f ||
	    (trigger->killtarget && trigger->killtarget[0]) ||
	    !trigger->target || !trigger->target[0])
		return false;
	while ((target = G_Find(target, (int)offsetof(edict_t, targetname),
	                        trigger->target)) != NULL)
	{
		if (!target->inuse || !target->classname)
			return false;
		if (strncmp(target->classname, "func_door", 9) == 0)
		{
			master = target->teammaster ? target->teammaster : target;
			if ((target->flags & FL_TEAMSLAVE) ||
			    !SG_OracleDeclaredDoorTeamSafe(master))
				return false;
			found = true;
			continue;
		}
		if (!Q_stricmp(target->classname, "target_speaker") &&
		    target->use == Use_Target_Speaker)
			continue;
		if (!Q_stricmp(target->classname, "trigger_relay") &&
		    target->use == trigger_relay_use &&
		    SG_OracleSoundOnlyTargets(target, 1))
			continue;
		return false;
	}
	return found;
}

qboolean SG_DeclaredDoorActivatorSafe(edict_t *trigger)
{
	return SG_OracleDeclaredActivatorSafe(trigger);
}

/* Classify the exact trigger selected by the real host's G_TouchTriggers.
 * This is observation only: it invokes no touch/use chain and performs no
 * trace.  Keep the exemptions identical to the ordinary world-only oracle so
 * live replay does not invent a stricter trigger policy. */
qboolean SG_OracleReplayTriggerEvents(edict_t *trigger,
	qboolean *contaminated, qboolean *door_passed)
{
	if (!trigger || !contaminated || !door_passed)
		return false;
	*contaminated = false;
	*door_passed = false;
	if (!trigger->inuse || !trigger->touch)
		return false;
	if (trigger->touch == Touch_Item)
		return true;
	if (SG_OracleDeclaredActivatorSafe(trigger))
		return true;
	if (trigger->touch == Touch_Multi &&
	    SG_OracleSoundOnlyTargets(trigger, 0))
		return true;
	*contaminated = true;
	return true;
}

/* Observe an actual live 25 ms body segment against the same complete door
 * sweeps used by the offline oracle.  This is analytic geometry only: it does
 * not trace, arm, move, touch, or use any entity.  A safe activator overlap is
 * clean until the body genuinely enters one of its door sweeps. */
qboolean SG_OracleReplayDoorPassage(const vec3_t from, const vec3_t to)
{
	int i;

	if (!from || !to)
		return false;
	for (i = 1; i < globals.num_edicts; i++)
	{
		edict_t *trigger = &g_edicts[i];

		if (SG_OracleDeclaredActivatorSafe(trigger) &&
		    SG_DeclaredDoorCrossesSweep(trigger, from, to))
			return true;
	}
	return false;
}

/* Snapshot the real linked source pose before DROP can submit its first
 * command.  This mirrors G_TouchTriggers and the world-only oracle's solid
 * overlap query, but invokes no touch, use, trace, or entity side effect. */
qboolean SG_OracleReplaySourceEvents(edict_t *ent,
	qboolean *contaminated, qboolean *door_passed)
{
	edict_t *touch[MAX_EDICTS];
	int i, num;

	if (!ent || !contaminated || !door_passed || !sg_host.box_edicts)
		return false;
	*contaminated = false;
	*door_passed = false;
	num = sg_host.box_edicts(ent->absmin, ent->absmax, touch,
	                          MAX_EDICTS, AREA_TRIGGERS);
	if (num < 0 || num > MAX_EDICTS)
		return false;
	for (i = 0; i < num; i++)
	{
		qboolean trigger_contaminated, trigger_door;
		edict_t *hit = touch[i];

		if (!hit || !hit->inuse || !hit->touch)
			continue;
		if (!SG_OracleReplayTriggerEvents(hit, &trigger_contaminated,
		        &trigger_door))
			return false;
		if (trigger_contaminated)
			*contaminated = true;
		if (trigger_door)
			*door_passed = true;
	}
	num = sg_host.box_edicts(ent->absmin, ent->absmax, touch,
	                          MAX_EDICTS, AREA_SOLID);
	if (num < 0 || num > MAX_EDICTS)
		return false;
	for (i = 0; i < num; i++)
	{
		edict_t *hit = touch[i];

		if (!hit || !hit->inuse || hit == ent || hit == g_edicts ||
		    SG_ImmutableSupport(hit))
			continue;
		if (hit->classname &&
		    strncmp(hit->classname, "func_door", 9) == 0)
			*door_passed = true;
		else
			*contaminated = true;
	}
	if (SG_OracleReplayDoorPassage(ent->s.origin, ent->s.origin))
		*door_passed = true;
	return true;
}

int SG_DeclaredDoorTriggerWaitMs(edict_t *trigger)
{
	float wait;

	if (!SG_OracleDeclaredActivatorSafe(trigger))
		return -1;
	wait = trigger->touch == Touch_DoorTrigger ? 1.0f : trigger->wait;
	/* Long trigger cooldowns are not intrinsically unsafe: the complete door
	 * contract applies its own bounded-duration policy below.  Reject here only
	 * when the millisecond conversion cannot fit the signed return type. */
	if (!isfinite(wait) || wait <= 0.0f || wait > 2147483.0f)
		return -1;
	return (int)ceilf(wait * 1000.0f);
}

static qboolean SG_OracleDeclaredTriggerContains(edict_t *trigger,
	const vec3_t origin)
{
	vec3_t mins, maxs;

	if (!trigger || !origin)
		return false;
	VectorSet(mins, origin[0] - 17.0f, origin[1] - 17.0f,
	          origin[2] - 25.0f);
	VectorSet(maxs, origin[0] + 17.0f, origin[1] + 17.0f,
	          origin[2] + 33.0f);
	return maxs[0] > trigger->absmin[0] && mins[0] < trigger->absmax[0] &&
	       maxs[1] > trigger->absmin[1] && mins[1] < trigger->absmax[1] &&
	       maxs[2] > trigger->absmin[2] && mins[2] < trigger->absmax[2];
}

qboolean SG_DeclaredDoorOutsideSweep(edict_t *trigger, const vec3_t origin)
{
	vec3_t hull_mins = { -16.0f, -16.0f, -24.0f };
	vec3_t hull_maxs = { 16.0f, 16.0f, 32.0f };
	edict_t *members[SG_PHANTOM_ARMED_DOORS];
	int count, i;

	if (!SG_OracleDeclaredActivatorSafe(trigger) || !origin)
		return false;
	count = SG_DeclaredDoorMembers(trigger, members,
	    SG_PHANTOM_ARMED_DOORS);
	if (count <= 0)
		return false;
	for (i = 0; i < count; i++)
	{
		vec3_t mins, maxs;

		SG_OracleDoorBounds(members[i], hull_mins, hull_maxs,
		                    mins, maxs);
		if (SG_OracleSegmentBox(origin, origin, mins, maxs))
			return false;
	}
	return true;
}

qboolean SG_DeclaredDoorCrossesSweep(edict_t *trigger, const vec3_t from,
	const vec3_t to)
{
	vec3_t hull_mins = { -16.0f, -16.0f, -24.0f };
	vec3_t hull_maxs = { 16.0f, 16.0f, 32.0f };
	edict_t *members[SG_PHANTOM_ARMED_DOORS];
	int count, i;

	if (!SG_OracleDeclaredActivatorSafe(trigger) || !from || !to)
		return false;
	count = SG_DeclaredDoorMembers(trigger, members,
	    SG_PHANTOM_ARMED_DOORS);
	if (count <= 0)
		return false;
	for (i = 0; i < count; i++)
	{
		vec3_t mins, maxs;

		SG_OracleDoorBounds(members[i], hull_mins, hull_maxs,
		                    mins, maxs);
		if (SG_OracleSegmentBox(from, to, mins, maxs))
			return true;
	}
	return false;
}

edict_t *SG_DeclaredDoorForLink(const vec3_t anchor, const vec3_t source)
{
	edict_t *match = NULL;
	int i;

	if (!anchor || !source)
		return NULL;
	for (i = 1; i < globals.num_edicts; i++)
	{
		edict_t *trigger = &g_edicts[i];
		if (!SG_OracleDeclaredActivatorSafe(trigger))
			continue;
		/* The record stores the exact wait point, not a brush origin. It names
		 * the activator by live player-hull overlap; ambiguous overlapping
		 * triggers are unrepresentable and therefore rejected below. */
		if (!SG_OracleDeclaredTriggerContains(trigger, anchor))
			continue;
		if (!SG_DeclaredDoorOutsideSweep(trigger, source) ||
		    !SG_DeclaredDoorOutsideSweep(trigger, anchor))
			continue;
		if (match)
		{
			/* Several maps place a broad trigger on one side and a thin trigger
			 * at the opposite face. A player hull at the seam legitimately touches
			 * both. They are one mechanism only when each independently satisfies
			 * the strict contract and their complete canonical mover sets match. */
			if (!SG_OracleDeclaredSameDoorSet(match, trigger))
				return NULL;
			continue;
		}
		match = trigger;
	}
	return match;
}

/* Touch_Multi calls this before its cooldown test.  It records only the
 * expected live player contact that the declared approach oracle admitted;
 * whether G_UseTargets subsequently fires is deliberately separate. */
qboolean SG_DeclaredDoorTouchMatches(edict_t *trigger,
	const vec3_t activator_origin)
{
	return trigger && activator_origin &&
	       SG_OracleDeclaredActivatorSafe(trigger) &&
	       SG_OracleDeclaredTriggerContains(trigger, activator_origin) &&
	       SG_DeclaredDoorOutsideSweep(trigger, activator_origin);
}

/* Runtime's door_use callback is evidence only when it is the exact direct
 * target set named by the record, fired by a body that still overlaps the
 * player trigger from a point safe for every mover pose.  Normalizing the
 * reported door makes the predicate insensitive to which team member G_Find
 * reached first without admitting a different team sharing the targetname. */
qboolean SG_DeclaredDoorActivationMatches(edict_t *trigger,
	edict_t *door_master, const vec3_t activator_origin)
{
	if (!trigger || !door_master || !activator_origin)
		return false;
	door_master = door_master->teammaster
	    ? door_master->teammaster : door_master;
	return SG_DeclaredDoorTouchMatches(trigger, activator_origin) &&
	       SG_OracleDeclaredSetMember(trigger, door_master);
}

/* The offline approach pauses on the first safe trigger that owns this exact
 * complete mover set.  Maps may place overlapping broad and narrow triggers
 * on opposite sides of the same leaves, so live callbacks must use the same
 * set identity rather than requiring pointer equality with the canonical
 * trigger selected by the loader. */
qboolean SG_DeclaredDoorEquivalentTouch(edict_t *expected,
	edict_t *actual, const vec3_t activator_origin)
{
	return expected && actual && activator_origin &&
	       SG_OracleDeclaredActivatorSafe(expected) &&
	       SG_OracleDeclaredActivatorSafe(actual) &&
	       SG_OracleDeclaredSameDoorSet(expected, actual) &&
	       SG_OracleDeclaredTriggerContains(actual, activator_origin) &&
	       SG_DeclaredDoorOutsideSweep(expected, activator_origin);
}

/* Runtime ownership is per complete canonical mover set, not per trigger
 * brush: opposite-side activators may be distinct edicts that operate the
 * same unteamed doors. */
qboolean SG_DeclaredDoorSameSet(edict_t *first, edict_t *second)
{
	return first && second &&
	       SG_OracleDeclaredActivatorSafe(first) &&
	       SG_OracleDeclaredActivatorSafe(second) &&
	       SG_OracleDeclaredSameDoorSet(first, second);
}

/* A declared approach must begin before its first mechanism touch.  Filter
 * source candidates by the same complete-set trigger identity the rollout
 * uses, so a dense row of already-overlapping seeds cannot crowd every clean
 * source out of the generator's bounded nearest fan. */
qboolean SG_DeclaredDoorApproachSourceClear(edict_t *trigger,
	const vec3_t origin)
{
	int i;

	if (!trigger || !origin || !SG_OracleDeclaredActivatorSafe(trigger) ||
	    !SG_DeclaredDoorOutsideSweep(trigger, origin))
		return false;
	for (i = 1; i < globals.num_edicts; i++)
	{
		edict_t *other = &g_edicts[i];

		if (!SG_OracleDeclaredActivatorSafe(other) ||
		    !SG_OracleDeclaredSameDoorSet(trigger, other))
			continue;
		if (SG_OracleDeclaredTriggerContains(other, origin))
			return false;
	}
	return true;
}

qboolean SG_DeclaredDoorEquivalentActivation(edict_t *expected,
	edict_t *actual, edict_t *door_master,
	const vec3_t activator_origin)
{
	if (!door_master ||
	    !SG_DeclaredDoorEquivalentTouch(expected, actual, activator_origin))
		return false;
	door_master = door_master->teammaster
	    ? door_master->teammaster : door_master;
	return SG_OracleDeclaredSetMember(actual, door_master);
}

/* Preflight one literal live door-approach command before ClientThink can run
 * item, flag, weapon, or arbitrary trigger side effects.  This clones the
 * authoritative fixed-point state and uses the same Pmove against all normal
 * live collision plus the synthetic full mover sweep.  Dynamic bodies are
 * conservatively rejected; only the expected/equivalent trigger contact is
 * admitted.  A false result therefore shelves the action without needing an
 * impossible transaction over game-side touch effects. */
qboolean SG_OracleDeclaredDoorStepSafe(edict_t *ent, edict_t *trigger,
	const usercmd_t *cmd)
{
	edict_t *old_passent = sg_oracle_passent;
	edict_t *old_expected = sg_oracle_declared_expected;
	edict_t *old_door = sg_oracle_declared_door;
	qboolean old_world = sg_oracle_world_only;
	qboolean old_contaminated = sg_oracle_contaminated;
	qboolean old_touched = sg_oracle_declared_touched;
	int old_action = sg_oracle_declared_action;
	sg_phantom_t ph;
	usercmd_t step_cmd;
	qboolean safe = false;
	int axis;

	if (!ent || !ent->inuse || !ent->client || !trigger || !cmd ||
	    !SG_DeclaredDoorOutsideSweep(trigger, ent->s.origin))
		return false;
	memset(&ph, 0, sizeof(ph));
	ph.pms = ent->client->ps.pmove;
	ph.old_pms = ent->client->old_pmove;
	for (axis = 0; axis < 3; axis++)
	{
		ph.pms.origin[axis] = (short)(ent->s.origin[axis] * 8.0f);
		ph.pms.velocity[axis] = (short)(ent->velocity[axis] * 8.0f);
		ph.origin[axis] = ph.pms.origin[axis] * 0.125f;
		ph.velocity[axis] = ph.pms.velocity[axis] * 0.125f;
	}
	if (!sv_gravity)
		goto done;
	ph.pms.gravity = (short)sv_gravity->value;
	ph.groundentity = ent->groundentity != NULL;
	ph.watertype = ent->watertype;
	ph.waterlevel = ent->waterlevel;

	sg_oracle_passent = ent;
	sg_oracle_world_only = true;
	sg_oracle_contaminated = false;
	sg_oracle_declared_expected = trigger;
	sg_oracle_declared_door = NULL; /* the complete mover sweep stays blocked */
	sg_oracle_declared_action = RL_DOOR;
	sg_oracle_declared_touched = false;
	if (!SG_OracleTriggerOverlap(&ph) && !SG_OracleSolidOverlap(&ph))
	{
		step_cmd = *cmd;
		SG_OracleRun(&ph, &step_cmd, 1);
		safe = !sg_oracle_contaminated && ph.groundentity &&
		       ph.waterlevel == 0 &&
		       SG_DeclaredDoorOutsideSweep(trigger, ph.origin);
	}

done:
	sg_oracle_passent = old_passent;
	sg_oracle_world_only = old_world;
	sg_oracle_contaminated = old_contaminated;
	sg_oracle_declared_expected = old_expected;
	sg_oracle_declared_door = old_door;
	sg_oracle_declared_action = old_action;
	sg_oracle_declared_touched = old_touched;
	return safe;
}

/* Re-prove the remaining TOP-pose egress from the exact authoritative state
 * at a production 100 ms boundary. Unlike the nominal generator proof this
 * may begin inside the mover sweep: the validated trigger set is already
 * linked at TOP and remains physical, while its synthetic union is used only
 * as the required terminal escape condition. No mover/entity loop interleaves
 * the four ClientThink commands that consume this grant. */
qboolean SG_OracleDeclaredDoorContinue(edict_t *ent, const vec3_t target,
	edict_t *trigger, int *arrival_ms)
{
	edict_t *old_passent = sg_oracle_passent;
	edict_t *old_expected = sg_oracle_declared_expected;
	edict_t *old_door = sg_oracle_declared_door;
	qboolean old_world = sg_oracle_world_only;
	qboolean old_contaminated = sg_oracle_contaminated;
	qboolean old_touched = sg_oracle_declared_touched;
	int old_action = sg_oracle_declared_action;
	sg_phantom_t ph;
	usercmd_t cmd;
	qboolean ok = false;
	float old_frame_z;
	int elapsed, axis;

	if (!ent || !ent->inuse || !ent->client || !target || !trigger ||
	    !arrival_ms || !sv_gravity || ent->health <= 0 || ent->deadflag ||
	    ent->movetype != MOVETYPE_WALK || ent->s.modelindex != 255 ||
	    ent->client->ps.pmove.pm_type != PM_NORMAL ||
	    (ent->client->ps.pmove.pm_flags & PMF_DUCKED) ||
	    (ent->client->ps.pmove.pm_flags &
	        (PMF_TIME_WATERJUMP | PMF_TIME_TELEPORT)) ||
	    (ent->client->ps.pmove.pm_time &&
	     !(ent->client->ps.pmove.pm_flags & PMF_TIME_LAND)) ||
	    ent->client->hookstate != 0 ||
	    ent->client->hook != NULL || ent->waterlevel != 0 ||
	    !SG_DeclaredDoorAtTop(trigger))
		return false;
	memset(&ph, 0, sizeof(ph));
	ph.pms = ent->client->ps.pmove;
	ph.old_pms = ent->client->old_pmove;
	for (axis = 0; axis < 3; axis++)
	{
		ph.pms.origin[axis] = (short)(ent->s.origin[axis] * 8.0f);
		ph.pms.velocity[axis] = (short)(ent->velocity[axis] * 8.0f);
		ph.origin[axis] = ph.pms.origin[axis] * 0.125f;
		ph.velocity[axis] = ph.pms.velocity[axis] * 0.125f;
	}
	ph.pms.gravity = (short)sv_gravity->value;
	ph.groundentity = ent->groundentity != NULL;
	ph.watertype = ent->watertype;
	ph.waterlevel = ent->waterlevel;
	old_frame_z = ent->client->oldvelocity[2];

	sg_oracle_passent = ent;
	sg_oracle_world_only = true;
	sg_oracle_contaminated = false;
	sg_oracle_declared_expected = NULL;
	sg_oracle_declared_door = trigger;
	sg_oracle_declared_action = RL_DOOR;
	sg_oracle_declared_touched = false;
	if (SG_OracleTriggerOverlap(&ph) || SG_OracleSolidOverlap(&ph))
		goto restore;
	for (elapsed = 0; elapsed < 5000; elapsed += 25)
	{
		memset(&cmd, 0, sizeof(cmd));
		cmd.msec = 25;
		if (!SG_DeclaredCommand(ph.origin, target, &ph.pms, &cmd))
			goto restore;
		SG_OracleRun(&ph, &cmd, 1);
		if (sg_oracle_contaminated || ph.waterlevel != 0)
			goto restore;
		if (((elapsed + 25) % 100) == 0)
		{
			/* ClientEndServerFrame applies falling damage after these same four
			 * literal commands.  This recovery proof deliberately permits that
			 * normal live consequence: rejecting a damaging but physically valid
			 * landing would freeze an airborne body inside the mover forever.
			 * A fatal landing cleanly hands ownership to the ordinary death path;
			 * it is safer than suspending gravity beneath a closing brush. */
			(void)P_FallDelta(old_frame_z, ph.velocity[2], ph.groundentity,
			                  ph.waterlevel);
			old_frame_z = ph.velocity[2];
			if (SG_DeclaredDoorOutsideSweep(trigger, ph.origin) &&
			    SG_SupportedArrived(ph.origin, target, ph.groundentity,
			                        ph.watertype, ph.waterlevel, ent))
			{
				*arrival_ms = elapsed + 25;
				ok = true;
				goto restore;
			}
		}
	}

restore:
	sg_oracle_passent = old_passent;
	sg_oracle_world_only = old_world;
	sg_oracle_contaminated = old_contaminated;
	sg_oracle_declared_expected = old_expected;
	sg_oracle_declared_door = old_door;
	sg_oracle_declared_action = old_action;
	sg_oracle_declared_touched = old_touched;
	return ok;
}

/* Runtime completion for a declared trigger set. door_hit_top publishes
 * STATE_TOP only after the mover reached its authoritative end pose, so this
 * predicate is also the handoff from the motion wait to the proved egress.
 * Re-resolve every direct target: stale/scripted map state fails closed. */
qboolean SG_DeclaredDoorAtTopFor(edict_t *trigger, int window_ms)
{
	edict_t *members[SG_PHANTOM_ARMED_DOORS];
	int count, i;

	if (!SG_OracleDeclaredActivatorSafe(trigger) || window_ms < 0)
		return false;
	count = SG_DeclaredDoorMembers(trigger, members,
	    SG_PHANTOM_ARMED_DOORS);
	if (count <= 0)
		return false;
	for (i = 0; i < count; i++)
	{
		edict_t *member = members[i];

		if (!member->inuse || member->moveinfo.state != SG_PLAT_STATE_TOP)
			return false;
		if (member->moveinfo.wait >= 0.0f &&
		    (member->think != door_go_down ||
		     member->nextthink <= level.time +
		         (float)window_ms * 0.001f + FRAMETIME))
			return false;
	}
	return true;
}

qboolean SG_DeclaredDoorAtTop(edict_t *trigger)
{
	return SG_DeclaredDoorAtTopFor(trigger, 0);
}

/* A combat body can transiently block both proved exits after the bot has
 * entered a CRUSHER sweep. There is no physical controller that guarantees an
 * escape through an occupied corridor, so keep the exact validated set at TOP
 * in short leases until a fresh suffix or retreat proof succeeds. This changes
 * no pose and never opens a door; it only postpones an already scheduled close
 * while a declared client is inside its occupied volume. */
qboolean SG_DeclaredDoorHoldOpen(edict_t *trigger, int lease_ms)
{
	edict_t *members[SG_PHANTOM_ARMED_DOORS];
	int count, i;
	float until;

	if (!SG_OracleDeclaredActivatorSafe(trigger) || lease_ms < 100 ||
	    lease_ms > 1000)
		return false;
	count = SG_DeclaredDoorMembers(trigger, members,
	    SG_PHANTOM_ARMED_DOORS);
	if (count <= 0)
		return false;
	until = level.time + lease_ms * 0.001f;
	for (i = 0; i < count; i++)
	{
		edict_t *member = members[i];

		if (!member->inuse || member->moveinfo.state != SG_PLAT_STATE_TOP)
			return false;
		if (member->moveinfo.wait >= 0.0f)
		{
			if (member->think != door_go_down)
				return false;
			if (member->nextthink < until)
				member->nextthink = until;
		}
	}
	return true;
}

/* Expand one declared activator to the exact unique member list used by the
 * generator's temporary TOP pose.  A trigger may name several independent
 * masters, while G_Find may also encounter more than one member of a team;
 * deduplicate the physical brushes rather than charging or relinking them
 * twice. */
int SG_DeclaredDoorMembers(edict_t *trigger, edict_t **members,
	int capacity)
{
	edict_t *target = NULL;
	int count = 0;

	if (!SG_OracleDeclaredActivatorSafe(trigger) || !members || capacity <= 0)
		return -1;
	if (trigger->touch == Touch_DoorTrigger)
	{
		edict_t *master = trigger->owner->teammaster
		    ? trigger->owner->teammaster : trigger->owner;
		edict_t *member;

		for (member = master; member; member = member->teamchain)
		{
			if (count >= capacity)
				return -1;
			members[count++] = member;
		}
		return count;
	}
	while ((target = G_Find(target, FOFS(targetname), trigger->target)) != NULL)
	{
		edict_t *master, *member;
		int i;

		if (!target->classname ||
		    strncmp(target->classname, "func_door", 9) != 0)
			continue;
		master = target->teammaster ? target->teammaster : target;
		for (member = master; member; member = member->teamchain)
		{
			for (i = 0; i < count; i++)
				if (members[i] == member)
					break;
			if (i < count)
				continue;
			if (count >= capacity)
				return -1;
			members[count++] = member;
		}
	}
	return count;
}

/* One authoritative timing contract shared by generation and loading.  The
 * latest handoff is bounded by both approach capture and the slowest member;
 * each member's TOP hold is then charged only for the skew after that member
 * can first arrive.  This matters for a single short-hold automatic door: its
 * wait starts at TOP, not when the player first touches its trigger. */
int SG_DeclaredDoorContractCost(edict_t *trigger, int approach_ms,
	int touch_ms, int egress_ms)
{
	edict_t *members[SG_PHANTOM_ARMED_DOORS];
	int earliest[SG_PHANTOM_ARMED_DOORS];
	int count, i, longest = 0, longest_cycle = 0, cyclic = 0;
	int trigger_ms, cooldown_gap, post_touch_ms, handoff_ms, total;

	if (approach_ms <= 0 || touch_ms <= 0 || touch_ms > approach_ms ||
	    egress_ms <= 0 || !trigger)
		return -1;
	count = SG_DeclaredDoorMembers(trigger, members,
	    SG_PHANTOM_ARMED_DOORS);
	if (count <= 0)
		return -1;
	trigger_ms = SG_DeclaredDoorTriggerWaitMs(trigger);
	if (trigger_ms <= 0)
		return -1;
	for (i = 0; i < count; i++)
	{
		edict_t *member = members[i];
		int travel, hold, cycle;
		float nominal;

		if (!isfinite(member->moveinfo.distance) ||
		    !isfinite(member->moveinfo.speed) ||
		    member->moveinfo.speed <= 0.0f ||
		    !isfinite(member->moveinfo.wait))
			return -1;
		nominal = fabsf(member->moveinfo.distance) /
		          member->moveinfo.speed * 1000.0f;
		if (!isfinite(nominal) || nominal <= 0.0f)
			return -1;
		earliest[i] = (int)floorf(nominal);
		travel = (int)ceilf(nominal) + 200;
		if (travel > longest)
			longest = travel;
		if (member->moveinfo.wait < 0.0f)
			continue;
		hold = (int)ceilf(member->moveinfo.wait * 1000.0f);
		cycle = 2 * travel + hold;
		if (cycle > longest_cycle)
			longest_cycle = cycle;
		cyclic++;
	}
	if (longest <= 0 || longest > 12500)
		return -1;
	/* A member's TOP hold begins after the accepted touch starts its opening,
	 * not when the source-to-trigger approach began.  Compare every member's
	 * relative TOP/close schedule against only the post-touch capture tail. */
	post_touch_ms = approach_ms - touch_ms;
	handoff_ms = post_touch_ms > longest ? post_touch_ms : longest;
	for (i = 0; i < count; i++)
		if (members[i]->moveinfo.wait >= 0.0f &&
		    earliest[i] + (int)floorf(members[i]->moveinfo.wait * 1000.0f) <
		        handoff_ms + egress_ms + 300)
			return -1;
	cooldown_gap = cyclic && trigger_ms > longest_cycle
	    ? trigger_ms - longest_cycle : 0;
	total = approach_ms + 2 * longest + cooldown_gap + egress_ms + 1000;
	if (total <= 0 || total > 30000)
		return -1;
	return total;
}

static qboolean SG_OracleArmDoor(sg_phantom_t *ph, edict_t *door)
{
	edict_t *master, *member;
	int i, index, wait_ms = 0, open_ms = 32767;

	if (!ph || !SG_OracleDoorTeamSafe(door))
		return false;
	master = door->teammaster ? door->teammaster : door;
	if (SG_OracleDoorArmed(ph, master))
		return true;
	for (member = master; member; member = member->teamchain)
	{
		int member_ms = (int)ceilf(fabsf(member->moveinfo.distance) /
		                              member->moveinfo.speed * 1000.0f) + 200;
		int member_open = member->moveinfo.wait < 0.0f ? 32767 :
		    (int)floorf(member->moveinfo.wait * 1000.0f);

		if (member_ms > wait_ms)
			wait_ms = member_ms;
		if (member_open < open_ms)
			open_ms = member_open;
		index = (int)(member - g_edicts);
		for (i = 0; i < ph->armed_door_count; i++)
			if (ph->armed_door[i] == index)
				break;
		if (i < ph->armed_door_count)
			continue;
		if (ph->armed_door_count >= SG_PHANTOM_ARMED_DOORS)
		{
			ph->door_arm_overflow = true;
			return false;
		}
		ph->armed_door[ph->armed_door_count++] = (short)index;
	}
	if (wait_ms <= 0 || open_ms <= 0 || ph->door_wait_ms > 30000 - wait_ms)
	{
		ph->door_arm_overflow = true;
		return false;
	}
	ph->door_wait_ms += wait_ms;
	if (ph->door_open_ms == 0 || open_ms < ph->door_open_ms)
		ph->door_open_ms = open_ms;
	return true;
}

static qboolean SG_OracleDoorCooldownSafe(edict_t *door, float trigger_wait)
{
	edict_t *master, *member;
	float closed_after = 1.0e30f;

	if (!SG_OracleDoorTeamSafe(door) || !isfinite(trigger_wait) ||
	    trigger_wait <= 0.0f)
		return false;
	master = door->teammaster ? door->teammaster : door;
	for (member = master; member; member = member->teamchain)
	{
		float cycle;

		if (member->moveinfo.wait < 0.0f)
			continue; /* this member never recloses */
		cycle = 2.0f * fabsf(member->moveinfo.distance) /
		        member->moveinfo.speed + member->moveinfo.wait + 0.1f;
		if (cycle < closed_after)
			closed_after = cycle;
	}
	return closed_after > 1.0e20f || trigger_wait <= closed_after;
}

/* A static proof may ignore a Touch_Multi only when a live player can fire it
 * on every visit and its complete direct target set is safe doors plus sound.
 * Disabled/NOT_PLAYER/one-shot/facing triggers and relays to doors all fail
 * closed: their state or missing interaction is not encoded in a rune link. */
static qboolean SG_OracleDoorActivator(edict_t *trigger, sg_phantom_t *ph)
{
	edict_t *target = NULL;
	edict_t *doors[SG_PHANTOM_ARMED_DOORS];
	int num_doors = 0, i;

	if (!trigger || !ph || !trigger->classname ||
	    strcmp(trigger->classname, "trigger_multiple") != 0 ||
	    trigger->touch != Touch_Multi || trigger->solid != SOLID_TRIGGER ||
	    (trigger->spawnflags & (2 | 4)) || trigger->wait <= 0.0f ||
	    !VectorCompare(trigger->movedir, vec3_origin) ||
	    trigger->delay != 0.0f || trigger->nextthink > level.time ||
	    (trigger->killtarget && trigger->killtarget[0]) ||
	    !trigger->target || !trigger->target[0])
		return false;
	while ((target = G_Find(target, FOFS(targetname), trigger->target)) != NULL)
	{
		if (!target->inuse || !target->classname)
			return false;
		if (strncmp(target->classname, "func_door", 9) == 0)
		{
			edict_t *master = target->teammaster ? target->teammaster : target;

			if (target->flags & FL_TEAMSLAVE ||
			    !SG_OracleDoorCooldownSafe(master, trigger->wait) ||
			    num_doors >= SG_PHANTOM_ARMED_DOORS)
				return false;
			doors[num_doors++] = master;
			continue;
		}
		if (!Q_stricmp(target->classname, "target_speaker") &&
		    target->use == Use_Target_Speaker)
			continue;
		if (!Q_stricmp(target->classname, "trigger_relay") &&
		    target->use == trigger_relay_use &&
		    SG_OracleSoundOnlyTargets(target, 1))
			continue;
		return false;
	}
	if (num_doors == 0)
		return false;
	for (i = 0; i < num_doors; i++)
		if (!SG_OracleArmDoor(ph, doors[i]))
			return false;
	return true;
}

static qboolean SG_OracleTriggerOverlap(sg_phantom_t *ph)
{
	edict_t *touch[MAX_EDICTS];
	vec3_t mins, maxs;
	int i, num;

	if (!sg_oracle_world_only || !sg_host.box_edicts)
		return false;
	/* linkentity expands absmin/absmax by one unit before BoxEdicts sees a
	 * live client. Match that conservative trigger-contact fringe. */
	VectorSet(mins, ph->origin[0] - 17.0f, ph->origin[1] - 17.0f,
	          ph->origin[2] - 25.0f);
	VectorSet(maxs, ph->origin[0] + 17.0f, ph->origin[1] + 17.0f,
	          ph->origin[2] + 33.0f);
	num = sg_host.box_edicts(mins, maxs, touch, MAX_EDICTS, AREA_TRIGGERS);
	for (i = 0; i < num; i++)
	{
		edict_t *hit = touch[i];

		if (!hit || !hit->inuse || !hit->touch || hit->touch == Touch_Item)
			continue;
		if (SG_OracleDeclaredTrigger(hit))
		{
			sg_oracle_declared_touched = true;
			continue;
		}
		if (sg_oracle_declared_door &&
		    (hit == sg_oracle_declared_door ||
		     (sg_oracle_declared_action == RL_DOOR &&
		      SG_OracleDeclaredSameDoorSet(sg_oracle_declared_door, hit))))
			continue;
		/* A declared door approach owns exactly one scripted touch. Do not let
		 * an unrelated auto-door or trigger_multiple become an unrecorded second
		 * mechanism merely because its volume overlaps this short rollout. */
		if (sg_oracle_declared_action == RL_DOOR)
		{
			if (hit->touch == Touch_Multi &&
			    SG_OracleSoundOnlyTargets(hit, 0))
				continue;
			return true;
		}
		/* An untargeted door's generated trigger is a deterministic part of
		 * the route, not transient contamination. Rune generation deliberately
		 * opens these movers while proving the crossing; runtime walks into this
		 * exact Touch_DoorTrigger volume to request the same opening. Push,
		 * teleport, hurt, gravity, and arbitrary scripted triggers still fail
		 * closed because Pmove alone cannot replay their touch effects. */
		if (hit->touch == Touch_DoorTrigger && hit->owner &&
		    SG_OracleArmDoor(ph, hit->owner))
			continue;
		if (hit->touch == Touch_Multi && SG_OracleDoorActivator(hit, ph))
			continue;
		/* Gong, thunder, and ambience pads are common around objective
		 * geometry.  Their complete target closure is network sound only, so
		 * touching them cannot invalidate an otherwise exact body rollout. */
		if (hit->touch == Touch_Multi &&
		    SG_OracleSoundOnlyTargets(hit, 0))
			continue;
		return true;
	}
	return false;
}

static qboolean SG_OracleSolidOverlap(sg_phantom_t *ph)
{
	edict_t *touch[MAX_EDICTS];
	vec3_t mins, maxs;
	int i, num;

	if (!sg_oracle_world_only || !sg_host.box_edicts)
		return false;
	VectorSet(mins, ph->origin[0] - 17.0f, ph->origin[1] - 17.0f,
	          ph->origin[2] - 25.0f);
	VectorSet(maxs, ph->origin[0] + 17.0f, ph->origin[1] + 17.0f,
	          ph->origin[2] + 33.0f);
	num = sg_host.box_edicts(mins, maxs, touch, MAX_EDICTS, AREA_SOLID);
	for (i = 0; i < num; i++)
	{
		edict_t *hit = touch[i];

		if (!hit || !hit->inuse || hit == sg_oracle_passent)
			continue;
		if (sg_oracle_loader_replay &&
		    (hit->client || (hit->owner && hit->owner->client)))
			continue;
		/* LMCTF implements each home flag stand by reusing the solid
		 * misc_teleporter_dest pedestal created by SP_info_flag_*. It is a
		 * deterministic objective surface, and the exact flag germ stands on
		 * its eight-unit top. Treating that support as transient contamination
		 * orphaned both flag seeds and made every objective field infinite.
		 * The separately spawned/touchable flag is a trigger; only these named
		 * solid pedestals are admitted here. */
		if (SG_ImmutableSupport(hit) || hit == sg_oracle_declared_expected ||
		    SG_OracleDeclaredSetMember(sg_oracle_declared_door, hit))
			continue;
		/* SV_LinkEdict represents an angled BSP mover with a coarse radius cube.
		 * That AreaEdicts box can cover empty space well outside the exact brush
		 * and its complete analytic sweep.  During a declared approach, ignore
		 * only this expected set's coarse overlap: Pmove still traces the real TOP
		 * brush, and SG_OracleDoorOverlap/TraceBlocked still enforce every pose. */
		if (sg_oracle_declared_action == RL_DOOR &&
		    SG_OracleDeclaredSetMember(sg_oracle_declared_expected, hit))
			continue;
		/* World collision is supplied by the BSP, not BoxEdicts. Any linked
		 * solid here is a client, mover, door, or other time-varying obstacle. */
		if (hit != g_edicts)
			return true;
	}
	return ph->door_arm_overflow || SG_OracleDoorOverlap(ph);
}

static trace_t SG_PhantomTrace(vec3_t start, vec3_t mins, vec3_t maxs, vec3_t end)
{
	int mask = sg_oracle_loader_replay
	    ? (MASK_PLAYERSOLID & ~CONTENTS_MONSTER) : MASK_PLAYERSOLID;
	trace_t tr = sg_host.trace(start, mins, maxs, end, sg_oracle_passent,
	                           mask);

	if (sg_oracle_world_only &&
	    SG_OracleDoorTraceBlocked(sg_oracle_active_phantom,
	                              start, mins, maxs, end))
		sg_oracle_contaminated = true;
	if (sg_oracle_world_only && !SG_ImmutableSupport(tr.ent) &&
	    tr.ent != sg_oracle_declared_expected &&
	    !SG_OracleDeclaredSetMember(sg_oracle_declared_door, tr.ent) &&
	    (tr.startsolid || tr.allsolid || tr.fraction < 1.0f) &&
	    tr.ent && tr.ent != g_edicts)
		sg_oracle_contaminated = true;
	return tr;
}

static int SG_PhantomContents(vec3_t point)
{
	return sg_host.pointcontents(point);
}

/* A hook bolt is a linked point entity. After each 80-unit server-frame move,
 * SV_PushEntity expands its bounds by one unit and calls G_TouchTriggers.
 * MASK_SHOT ray traces cannot see those volumes: trigger_push can redirect the
 * bolt and trigger_hurt can eventually kill it. Sample the exact flight-frame
 * endpoints and reject every active trigger except items (Touch_Item ignores a
 * non-client bolt), so a straight-ray witness cannot silently cross a second
 * movement system. Runtime repeats this against the live trigger snapshot. */
qboolean SG_OracleHookFlightClear(const vec3_t muzzle, const vec3_t bite)
{
	edict_t *touch[MAX_EDICTS];
	vec3_t delta, direction, point, mins, maxs;
	vec3_t bolt_mins = { -1.0f, -1.0f, -1.0f };
	vec3_t bolt_maxs = { 1.0f, 1.0f, 1.0f };
	float distance, travelled;
	int i, num;

	if (!sg_host.box_edicts)
		return false;
	VectorSubtract(bite, muzzle, delta);
	distance = VectorLength(delta);
	if (distance < 1.0f)
		return false;
	/* The bolt has no serialized door-activation phase. Generation holds doors
	 * nonsolid, so reject any ray through a door's complete motion envelope;
	 * live reproof then cannot depend on a lucky open-door snapshot. */
	if (SG_OracleDoorTraceBlocked(NULL, muzzle, bolt_mins, bolt_maxs, bite))
		return false;
	VectorScale(delta, 1.0f / distance, direction);
	for (travelled = 80.0f; ; travelled += 80.0f)
	{
		float at = travelled < distance ? travelled : distance;

		point[0] = muzzle[0] + at * direction[0];
		point[1] = muzzle[1] + at * direction[1];
		point[2] = muzzle[2] + at * direction[2];
		VectorSet(mins, point[0] - 1.0f, point[1] - 1.0f, point[2] - 1.0f);
		VectorSet(maxs, point[0] + 1.0f, point[1] + 1.0f, point[2] + 1.0f);
		num = sg_host.box_edicts(mins, maxs, touch, MAX_EDICTS,
		                          AREA_TRIGGERS);
		for (i = 0; i < num; i++)
		{
			edict_t *hit = touch[i];

			if (!hit || !hit->inuse || !hit->touch || hit->touch == Touch_Item)
				continue;
			return false;
		}
		if (travelled >= distance)
			break;
	}
	return true;
}

static void SG_OracleReplayPose(const sg_phantom_t *ph, sg_replay_pose_t *pose)
{
	if (!pose)
		return;
	memset(pose, 0, sizeof(*pose));
	if (!ph)
		return;
	pose->pms = ph->pms;
	VectorCopy(ph->origin, pose->origin);
	VectorCopy(ph->velocity, pose->velocity);
	pose->grounded = ph->groundentity;
	pose->watertype = ph->watertype;
	pose->waterlevel = ph->waterlevel;
}

static qboolean SG_OracleReplayContactClear(const vec3_t origin,
	const vec3_t destination, edict_t *passent)
{
	vec3_t from, to;
	trace_t tr;

	VectorCopy(origin, from);
	VectorCopy(destination, to);
	from[2] += 16.0f;
	to[2] += 16.0f;
	tr = sg_host.trace(from, NULL, NULL, to, passent, MASK_PLAYERSOLID);
	return !tr.startsolid && !tr.allsolid && tr.fraction >= 1.0f;
}

static qboolean SG_OracleSwimArrivalMayTrace(const sg_phantom_t *ph,
	const vec3_t destination, qboolean destination_water)
{
	vec3_t delta;

	if (!ph)
		return false;
	VectorSubtract(destination, ph->origin, delta);
	if (delta[0] * delta[0] + delta[1] * delta[1] >=
	        SG_REPLAY_ARRIVE_RADIUS * SG_REPLAY_ARRIVE_RADIUS ||
	    delta[2] <= -SG_REPLAY_ARRIVE_Z ||
	    delta[2] >= SG_REPLAY_ARRIVE_Z ||
	    (ph->waterlevel > 0 &&
	     (ph->watertype & (CONTENTS_LAVA | CONTENTS_SLIME))))
		return false;
	if (destination_water)
		return ph->waterlevel >= 2 && (ph->watertype & CONTENTS_WATER);
	return ph->groundentity && ph->waterlevel < 2;
}

static void SG_OracleSwimObservation(const sg_phantom_t *ph,
	const vec3_t destination, qboolean destination_water,
	qboolean check_arrival, edict_t *passent,
	sg_replay_observation_t *observation)
{
	if (!observation)
		return;
	memset(observation, 0, sizeof(*observation));
	if (!ph)
		return;
	/* Legacy SG_SwimArrived reaches its trace only at a production boundary
	 * and only after proximity, liquid, and support all pass.  Preserve that
	 * host-call cadence; contact is irrelevant at every other reducer step. */
	observation->contaminated = sg_oracle_contaminated;
	observation->door_passed = ph->door_passed;
	observation->contact_clear = true;
	if (!observation->contaminated && check_arrival &&
	    SG_OracleSwimArrivalMayTrace(ph, destination, destination_water))
		observation->contact_clear = SG_OracleReplayContactClear(ph->origin,
			destination, passent);
}

/* One exact SWIM witness from an already initialized fixed-point state.
 * Offline generation supplies the seed-at-rest state; live execution supplies
 * the authoritative post-world state that its first ClientThink will consume.
 * Both therefore share command construction, collision/trigger rejection,
 * falling-damage boundaries, arrival, and the three-second local-action cap. */
qboolean SG_OracleSwimTraverse(sg_phantom_t *ph, const vec3_t destination,
	qboolean destination_water, float old_frame_z, sg_swim_proof_t *proof,
	edict_t *passent, qboolean world_only)
{
	sg_swim_replay_spec_t spec;
	sg_swim_replay_state_t state;
	sg_replay_pose_t pose;
	sg_replay_observation_t observation;
	sg_replay_status_t status;
	usercmd_t cmd;
	edict_t *previous_passent;
	qboolean previous_world_only, previous_contaminated;
	qboolean result = false;

	if (!ph || !proof)
		return false;
	memset(proof, 0, sizeof(*proof));
	previous_passent = sg_oracle_passent;
	previous_world_only = sg_oracle_world_only;
	previous_contaminated = sg_oracle_contaminated;
	sg_oracle_passent = passent;
	sg_oracle_world_only = world_only;
	sg_oracle_contaminated = false;
	if (SG_OracleTriggerOverlap(ph) || SG_OracleSolidOverlap(ph))
		goto done;

	memset(&spec, 0, sizeof(spec));
	VectorCopy(destination, spec.destination);
	spec.destination_water = destination_water;
	spec.expected_arrival_ms = SG_REPLAY_TIME_DISCOVER;
	SG_OracleReplayPose(ph, &pose);
	SG_OracleSwimObservation(ph, destination, destination_water, true,
		passent, &observation);
	status = SG_SwimReplayBegin(&state, &spec, &pose, &observation,
		old_frame_z);
	while (status == SG_REPLAY_RUNNING)
	{
		status = SG_SwimReplayPreStep(&state, &pose, &cmd);
		if (status != SG_REPLAY_RUNNING)
			break;
		SG_OracleRun(ph, &cmd, 1);
		SG_OracleReplayPose(ph, &pose);
		SG_OracleSwimObservation(ph, destination, destination_water,
			state.progress.elapsed_ms + SG_REPLAY_STEP_MS <
			    SG_REPLAY_SWIM_LIMIT_MS &&
			((state.progress.elapsed_ms + SG_REPLAY_STEP_MS) %
			 SG_REPLAY_FRAME_MS) == 0,
			passent, &observation);
		status = SG_SwimReplayPostStep(&state, &pose, &observation);
	}
	if (status == SG_REPLAY_ARRIVED ||
	    (status == SG_REPLAY_FAILED &&
	     state.progress.reason == SG_REPLAY_REASON_DOOR_PASSED &&
	     state.progress.arrival_ms != SG_REPLAY_TIME_DISCOVER))
	{
		proof->arrival_ms = state.progress.arrival_ms;
		proof->exit_speed = state.progress.exit_speed;
		result = status == SG_REPLAY_ARRIVED;
	}
done:
	if (ph->door_passed)
		result = false;
	sg_oracle_passent = previous_passent;
	sg_oracle_world_only = previous_world_only;
	sg_oracle_contaminated = previous_contaminated;
	return result;
}

/* A submerged teleporter has no stable rest state and cannot use the planar
 * declared approach. Re-run the shared swim feedback controller until the
 * exact pad trigger is touched. The trigger side effect itself remains the
 * declaration; every command before it is ordinary Pmove and is re-proved
 * from the actual live fixed-point state at execution time. */
qboolean SG_OracleTeleportSwimApproach(sg_phantom_t *ph,
	const vec3_t anchor, edict_t *pad, float old_frame_z,
	sg_swim_proof_t *proof, edict_t *passent, qboolean world_only)
{
	usercmd_t cmd;
	edict_t *old_passent = sg_oracle_passent;
	edict_t *old_expected = sg_oracle_declared_expected;
	qboolean old_world = sg_oracle_world_only;
	qboolean old_contaminated = sg_oracle_contaminated;
	qboolean old_touched = sg_oracle_declared_touched;
	int old_action = sg_oracle_declared_action;
	qboolean ok = false;
	int elapsed;

	if (!ph || !pad || !proof)
		return false;
	memset(proof, 0, sizeof(*proof));
	sg_oracle_passent = passent;
	sg_oracle_world_only = world_only;
	sg_oracle_contaminated = false;
	sg_oracle_declared_expected = pad;
	sg_oracle_declared_action = RL_TELEPORT;
	sg_oracle_declared_touched = false;
	if (SG_OracleTriggerOverlap(ph) || sg_oracle_declared_touched ||
	    SG_OracleSolidOverlap(ph) ||
	    SG_OracleDoorOverlap(ph))
		goto done;
	for (elapsed = 0; elapsed < 3000; elapsed += SG_SWIM_STEP_MSEC)
	{
		if (ph->waterlevel > 0 &&
		    (ph->watertype & (CONTENTS_LAVA | CONTENTS_SLIME)))
			goto done;
		if (elapsed > 0 && (elapsed % 100) == 0)
		{
			if (P_FallDelta(old_frame_z, ph->velocity[2], ph->groundentity,
			                ph->waterlevel) > 30.0f)
				goto done;
			old_frame_z = ph->velocity[2];
		}
		memset(&cmd, 0, sizeof(cmd));
		cmd.msec = SG_SWIM_STEP_MSEC;
		if (!SG_SwimCommand(ph->origin, anchor, &ph->pms, &cmd))
			goto done;
		SG_OracleRun(ph, &cmd, 1);
		if (sg_oracle_contaminated || SG_OracleDoorOverlap(ph))
			goto done;
		if (sg_oracle_declared_touched)
		{
			/* teleporter_touch clears velocity and relocates to a dry supported
			 * destination before this server frame's P_FallingDamage. Reject an
			 * actual entry whose prior end-frame descent would make that stop
			 * damaging; nominal generation begins with old_frame_z == 0. */
			if (P_FallDelta(old_frame_z, 0.0f, true, 0) > 30.0f)
				goto done;
			proof->arrival_ms = elapsed + SG_SWIM_STEP_MSEC;
			ok = true;
			goto done;
		}
	}
done:
	if (ph->door_passed)
		ok = false;
	sg_oracle_passent = old_passent;
	sg_oracle_world_only = old_world;
	sg_oracle_contaminated = old_contaminated;
	sg_oracle_declared_expected = old_expected;
	sg_oracle_declared_action = old_action;
	sg_oracle_declared_touched = old_touched;
	return ok;
}

/*
 * Roll the real physics forward.
 *
 * in:  state (position, velocity, pm flags), a command, how many steps and
 *      how long each one is (msec must be what a real client could send --
 *      the caller owns making steps sum to honest time, principle 2).
 * out: the state Pmove left behind, plus what happened on the way.
 *
 * The command is applied unchanged every step. Callers who want per-step
 * decisions (the generator proving a strafe link, say) call this once per
 * step with steps=1 and change the command between calls -- the exact
 * structure a real client has, one usercmd per step of simulation.
 */
void SG_OracleRun(sg_phantom_t *ph, usercmd_t *cmd, int steps)
{
	pmove_t pm;
	int i;

	for (i = 0; i < steps; i++)
	{
		memset(&pm, 0, sizeof(pm));
		pm.s = ph->pms;
		pm.cmd = *cmd;
		pm.snapinitial = memcmp(&ph->old_pms, &pm.s, sizeof(pm.s)) != 0;
		pm.trace = SG_PhantomTrace;
		pm.pointcontents = SG_PhantomContents;

		sg_oracle_active_phantom = ph;
		sg_host.pmove(&pm);
		sg_oracle_active_phantom = NULL;

		ph->pms = pm.s;
		ph->old_pms = pm.s;
		ph->groundentity = pm.groundentity ? true : false;
		ph->watertype = pm.watertype;
		ph->waterlevel = pm.waterlevel;

		/* decode the fixed-point state once per step so callers read floats */
		ph->origin[0] = pm.s.origin[0] * 0.125f;
		ph->origin[1] = pm.s.origin[1] * 0.125f;
		ph->origin[2] = pm.s.origin[2] * 0.125f;
		ph->velocity[0] = pm.s.velocity[0] * 0.125f;
		ph->velocity[1] = pm.s.velocity[1] * 0.125f;
		ph->velocity[2] = pm.s.velocity[2] * 0.125f;
		if (SG_OracleTriggerOverlap(ph) || SG_OracleSolidOverlap(ph))
			sg_oracle_contaminated = true;
	}
}

/* One scoped static-world rollout for ordinary rune proofs. Pmove alone does
 * not call the game's trigger touches, and a generation-time player/mover is
 * not a durable map affordance. The overlap/trace latches conservatively
 * reject either kind of contamination, then every global is restored so this
 * helper cannot leak context into another proof. */
qboolean SG_OracleRunWorld(sg_phantom_t *ph, usercmd_t *cmd, int steps)
{
	edict_t *previous_passent = sg_oracle_passent;
	qboolean previous_world_only = sg_oracle_world_only;
	qboolean previous_contaminated = sg_oracle_contaminated;
	qboolean clean;

	if (!sg_host.box_edicts)
		return false;
	sg_oracle_passent = NULL;
	sg_oracle_world_only = true;
	sg_oracle_contaminated = false;
	if (!SG_OracleTriggerOverlap(ph) && !SG_OracleSolidOverlap(ph))
		SG_OracleRun(ph, cmd, steps);
	clean = !sg_oracle_contaminated &&
	        !SG_OracleTriggerOverlap(ph) && !SG_OracleSolidOverlap(ph);
	sg_oracle_passent = previous_passent;
	sg_oracle_world_only = previous_world_only;
	sg_oracle_contaminated = previous_contaminated;
	return clean;
}

/* Prove the exact planar controller from a static graph source until it first
 * overlaps the one trigger owned by the declared pad/platform. The expected
 * solid still participates in Pmove collision; it is merely deterministic,
 * while every other non-world solid/trigger remains contamination. */
qboolean SG_OracleDeclaredApproach(const vec3_t source, const vec3_t target,
	edict_t *expected, int action, int *arrival_ms)
{
	edict_t *old_passent = sg_oracle_passent;
	edict_t *old_expected = sg_oracle_declared_expected;
	qboolean old_world = sg_oracle_world_only;
	qboolean old_contaminated = sg_oracle_contaminated;
	qboolean old_touched = sg_oracle_declared_touched;
	int old_action = sg_oracle_declared_action;
	sg_phantom_t ph;
	usercmd_t cmd;
	qboolean ok = false;
	int elapsed;

	if (!expected || !expected->inuse || !arrival_ms ||
	    (action != RL_LIFT && action != RL_TELEPORT))
		return false;
	sg_oracle_passent = NULL;
	sg_oracle_world_only = true;
	sg_oracle_contaminated = false;
	sg_oracle_declared_expected = expected;
	sg_oracle_declared_action = action;
	sg_oracle_declared_touched = false;
	SG_OraclePlace(&ph, (vec_t *)source);
	memset(&cmd, 0, sizeof(cmd));
	cmd.msec = 0;
	SG_OracleRun(&ph, &cmd, 1);
	/* A staging seed already inside the mechanism trigger is not a separate,
	 * routable source phase. */
	if (sg_oracle_contaminated || sg_oracle_declared_touched ||
	    SG_OracleDoorOverlap(&ph) ||
	    !ph.groundentity || ph.waterlevel != 0)
		goto done;
	for (elapsed = 0; elapsed < 3000; elapsed += 25)
	{
		memset(&cmd, 0, sizeof(cmd));
		cmd.msec = 25;
		if (!SG_DeclaredCommand(ph.origin, target, &ph.pms, &cmd))
			goto done;
		SG_OracleRun(&ph, &cmd, 1);
		if (sg_oracle_contaminated || SG_OracleDoorOverlap(&ph) ||
		    !ph.groundentity || ph.waterlevel != 0)
			goto done;
		if (sg_oracle_declared_touched)
		{
			*arrival_ms = elapsed + 25;
			ok = true;
			goto done;
		}
	}

done:
	if (ph.door_passed)
		ok = false;
	sg_oracle_passent = old_passent;
	sg_oracle_world_only = old_world;
	sg_oracle_contaminated = old_contaminated;
	sg_oracle_declared_expected = old_expected;
	sg_oracle_declared_action = old_action;
	sg_oracle_declared_touched = old_touched;
	return ok;
}

/* Prove the lift's top-platform-to-static-graph handoff. The caller positions
 * the resolved platform at its authoritative top for this synchronous scope.
 * Its solid and center trigger are admitted, but success requires the player
 * hull to have left the platform footprint and reached the same supported
 * endpoint predicate used live. */
qboolean SG_OracleDeclaredEgress(const vec3_t source, const vec3_t target,
	edict_t *support, int *arrival_ms)
{
	edict_t *old_passent = sg_oracle_passent;
	edict_t *old_expected = sg_oracle_declared_expected;
	qboolean old_world = sg_oracle_world_only;
	qboolean old_contaminated = sg_oracle_contaminated;
	qboolean old_touched = sg_oracle_declared_touched;
	int old_action = sg_oracle_declared_action;
	sg_phantom_t ph;
	usercmd_t cmd;
	qboolean ok = false;
	int elapsed;

	if (!support || !support->inuse || !arrival_ms)
		return false;
	sg_oracle_passent = NULL;
	sg_oracle_world_only = true;
	sg_oracle_contaminated = false;
	sg_oracle_declared_expected = support;
	sg_oracle_declared_action = RL_LIFT;
	sg_oracle_declared_touched = false;
	SG_OraclePlace(&ph, (vec_t *)source);
	memset(&cmd, 0, sizeof(cmd));
	cmd.msec = 0;
	SG_OracleRun(&ph, &cmd, 1);
	if (sg_oracle_contaminated || SG_OracleDoorOverlap(&ph) ||
	    !ph.groundentity || ph.waterlevel != 0)
		goto done;
	for (elapsed = 0; elapsed < 3000; elapsed += 25)
	{
		qboolean outside;

		memset(&cmd, 0, sizeof(cmd));
		cmd.msec = 25;
		if (!SG_DeclaredCommand(ph.origin, target, &ph.pms, &cmd))
			goto done;
		SG_OracleRun(&ph, &cmd, 1);
		if (sg_oracle_contaminated || SG_OracleDoorOverlap(&ph) ||
		    (ph.waterlevel > 0 &&
		     (ph.watertype & (CONTENTS_LAVA | CONTENTS_SLIME))))
			goto done;
		/* Match SG_LiftRider exactly. Linked entity bounds carry a one-unit
		 * fringe on both the platform and player; cancelling those fringes
		 * makes the phantom's physical +/-16 hull leave at absmin/absmax,
		 * not at absmin+1/absmax-1. A one-unit disagreement here could prove
		 * an egress that the live controller still classified as riding. */
		outside = ph.origin[0] + 16.0f <= support->absmin[0] ||
		          ph.origin[0] - 16.0f >= support->absmax[0] ||
		          ph.origin[1] + 16.0f <= support->absmin[1] ||
		          ph.origin[1] - 16.0f >= support->absmax[1];
		if (((elapsed + 25) % 100) == 0 && outside &&
		    SG_SupportedArrived(ph.origin, target, ph.groundentity,
		                        ph.watertype, ph.waterlevel, NULL))
		{
			*arrival_ms = elapsed + 25;
			ok = true;
			goto done;
		}
	}

done:
	if (ph.door_passed)
		ok = false;
	sg_oracle_passent = old_passent;
	sg_oracle_world_only = old_world;
	sg_oracle_contaminated = old_contaminated;
	sg_oracle_declared_expected = old_expected;
	sg_oracle_declared_action = old_action;
	sg_oracle_declared_touched = old_touched;
	return ok;
}

/* Phase one of RL_DOOR: walk from a connected dry graph seed to one exact
 * sweep-clear wait point inside the validated activator. The expected trigger
 * is the only admitted side effect; the door set itself remains in the
 * synthetic full-sweep audit, so this path is safe for every current pose.
 * Arrival is a realizable rest state within the same two-unit canonicalization
 * envelope used live. */
qboolean SG_OracleDeclaredDoorApproach(const vec3_t source,
	const vec3_t wait_point, edict_t *trigger, int *arrival_ms,
	int *touch_ms)
{
	edict_t *old_passent = sg_oracle_passent;
	edict_t *old_expected = sg_oracle_declared_expected;
	edict_t *old_door = sg_oracle_declared_door;
	qboolean old_world = sg_oracle_world_only;
	qboolean old_contaminated = sg_oracle_contaminated;
	qboolean old_touched = sg_oracle_declared_touched;
	int old_action = sg_oracle_declared_action;
	sg_phantom_t ph, exact;
	usercmd_t cmd;
	qboolean ok = false;
	qboolean triggered = false;
	int elapsed, resume_ms = 0, first_touch_ms = 0;

	if (!trigger || !arrival_ms || !touch_ms ||
	    !SG_DeclaredDoorSameSet(
	        SG_DeclaredDoorForLink(wait_point, source), trigger))
		return false;
	sg_oracle_passent = NULL;
	sg_oracle_world_only = true;
	sg_oracle_contaminated = false;
	sg_oracle_declared_expected = trigger;
	sg_oracle_declared_door = NULL; /* keep the complete mover sweep physical */
	sg_oracle_declared_action = RL_DOOR;
	sg_oracle_declared_touched = false;
	SG_OraclePlace(&ph, (vec_t *)source);
	memset(&cmd, 0, sizeof(cmd));
	cmd.msec = 0;
	if (!SG_OracleRunWorld(&ph, &cmd, 1) || sg_oracle_declared_touched ||
	    !ph.groundentity || ph.waterlevel != 0)
		goto done;
	for (elapsed = 0; elapsed < 5000; elapsed += 25)
	{
		vec3_t delta;
		float horiz;

		memset(&cmd, 0, sizeof(cmd));
		cmd.msec = 25;
		/* The live door_use callback occurs inside ClientThink.  The next
		 * substep observes it and submits zero input for the rest of that
		 * production 100 ms frame; normal anchor capture resumes on the next
		 * outer frame.  Reproduce that one synchronous pause here. */
		if (!triggered || elapsed >= resume_ms)
			if (!SG_DeclaredCommand(ph.origin, wait_point, &ph.pms, &cmd))
				goto done;
		VectorSubtract(wait_point, ph.origin, delta);
		delta[2] = 0.0f;
		horiz = VectorLength(delta);
		/* Brake before a narrow boundary trigger rather than arriving with the
		 * ordinary 200/400 command's momentum.  The tested 64-unit runway is
		 * enough for fixed-point ground friction while preserving the five-second
		 * horizon of longer broad-trigger approaches.  A 64-unit wish speed then
		 * advances at most 1.6 units per 25 ms overlap sample. */
		if (!triggered && horiz <= 64.0f && cmd.forwardmove > 64)
			cmd.forwardmove = 64;
		/* A synthesized wait point can sit only one fixed-point unit inside a
		 * thin trigger's player-contact fringe.  The ordinary declared command
		 * stops at four units and canonical capture begins at two, so stopping
		 * the slow approach at two can leave the body forever just outside the
		 * trigger.  Before the first admitted touch, keep the small command
		 * alive all the way to the exact anchor; after touch, retain the normal
		 * two-unit clear-sweep capture envelope. */
		if ((!triggered || elapsed >= resume_ms) &&
		    ((!triggered && horiz > 0.01f) ||
		     (triggered && horiz > 2.0f)) && cmd.forwardmove == 0)
			cmd.forwardmove = 40;
		if (!SG_OracleRunWorld(&ph, &cmd, 1) ||
		    !ph.groundentity || ph.waterlevel != 0)
			goto done;
		if (!triggered && sg_oracle_declared_touched)
		{
			first_touch_ms = elapsed + 25;

			triggered = true;
			resume_ms = ((first_touch_ms + 99) / 100) * 100;
		}
		VectorSubtract(wait_point, ph.origin, delta);
		if (!triggered || fabsf(delta[2]) > 2.0f ||
		    delta[0] * delta[0] + delta[1] * delta[1] > 4.0f ||
		    ph.pms.velocity[0] != 0 || ph.pms.velocity[1] != 0 ||
		    ph.pms.velocity[2] != 0)
			continue;
		/* Prove the canonical fixed point that runtime's clear <=2u snap
		 * installs before it begins the motion wait.  SG_OraclePlace is an
		 * injected state, so first require the same clear player-hull sweep as
		 * Ballistic_CanonicalizeSource; otherwise a two-unit wall seam could be
		 * proved offline but rejected by the live controller. */
		{
			vec3_t mins = { -16.0f, -16.0f, -24.0f };
			vec3_t maxs = { 16.0f, 16.0f, 32.0f };
			trace_t snap = sg_host.trace(ph.origin, mins, maxs,
			                             (vec_t *)wait_point, NULL,
			                             MASK_PLAYERSOLID);

			if (snap.startsolid || snap.allsolid || snap.fraction < 1.0f)
				goto done;
		}
		SG_OraclePlace(&exact, (vec_t *)wait_point);
		memset(&cmd, 0, sizeof(cmd));
		cmd.msec = 0;
		if (!SG_OracleRunWorld(&exact, &cmd, 1) || !exact.groundentity ||
		    exact.waterlevel != 0 || !SG_DeclaredDoorOutsideSweep(trigger,
		        exact.origin))
			goto done;
		*arrival_ms = elapsed + 25;
		*touch_ms = first_touch_ms;
		ok = true;
		goto done;
	}

done:
	sg_oracle_passent = old_passent;
	sg_oracle_world_only = old_world;
	sg_oracle_contaminated = old_contaminated;
	sg_oracle_declared_expected = old_expected;
	sg_oracle_declared_door = old_door;
	sg_oracle_declared_action = old_action;
	sg_oracle_declared_touched = old_touched;
	return ok;
}

/* One complete RL_DOOR egress. The caller has synchronously linked every
 * member of `door` at its authoritative open pose. Unlike a generic proof,
 * that exact team remains physical collision while its own repeatable trigger
 * and solids are deterministic members of this declaration. The body begins
 * at the outside-sweep trigger seed where live execution waited motionless,
 * and success is a dry supported endpoint outside the full sweep. */
qboolean SG_OracleDeclaredDoorEgress(const vec3_t source,
	const vec3_t target, edict_t *trigger, edict_t *passent,
	int *arrival_ms)
{
	edict_t *old_passent = sg_oracle_passent;
	edict_t *old_expected = sg_oracle_declared_expected;
	edict_t *old_door = sg_oracle_declared_door;
	qboolean old_world = sg_oracle_world_only;
	qboolean old_contaminated = sg_oracle_contaminated;
	qboolean old_touched = sg_oracle_declared_touched;
	int old_action = sg_oracle_declared_action;
	sg_phantom_t ph;
	usercmd_t cmd;
	qboolean ok = false;
	float old_frame_z;
	int elapsed;

	if (!trigger || !arrival_ms ||
	    !SG_DeclaredDoorOutsideSweep(trigger, source) ||
	    !SG_DeclaredDoorOutsideSweep(trigger, target) ||
	    !SG_DeclaredDoorCrossesSweep(trigger, source, target))
		return false;
	sg_oracle_passent = passent;
	sg_oracle_world_only = true;
	sg_oracle_contaminated = false;
	sg_oracle_declared_expected = NULL;
	sg_oracle_declared_door = trigger;
	sg_oracle_declared_action = RL_DOOR;
	sg_oracle_declared_touched = false;
	SG_OraclePlace(&ph, (vec_t *)source);
	memset(&cmd, 0, sizeof(cmd));
	cmd.msec = 0;
	SG_OracleRun(&ph, &cmd, 1);
	if (sg_oracle_contaminated || !ph.groundentity || ph.waterlevel != 0)
		goto done;
	old_frame_z = ph.velocity[2];
	for (elapsed = 0; elapsed < 5000; elapsed += 25)
	{
		memset(&cmd, 0, sizeof(cmd));
		cmd.msec = 25;
		if (!SG_DeclaredCommand(ph.origin, target, &ph.pms, &cmd))
			goto done;
		SG_OracleRun(&ph, &cmd, 1);
		if (sg_oracle_contaminated || ph.waterlevel != 0)
			goto done;
		if (((elapsed + 25) % 100) == 0)
		{
			/* Production applies falling damage after the same four literal
			 * ClientThink commands.  A transient 25 ms loss of ground support is
			 * therefore valid, but the nominal serialized action must not depend
			 * on taking damaging fall impact at that server-frame boundary. */
			if (P_FallDelta(old_frame_z, ph.velocity[2], ph.groundentity,
			                ph.waterlevel) > 30.0f)
				goto done;
			old_frame_z = ph.velocity[2];
			if (SG_DeclaredDoorOutsideSweep(trigger, ph.origin) &&
			    SG_SupportedArrived(ph.origin, target, ph.groundentity,
			                        ph.watertype, ph.waterlevel, passent))
			{
				*arrival_ms = elapsed + 25;
				ok = true;
				goto done;
			}
		}
	}

done:
	sg_oracle_passent = old_passent;
	sg_oracle_world_only = old_world;
	sg_oracle_contaminated = old_contaminated;
	sg_oracle_declared_expected = old_expected;
	sg_oracle_declared_door = old_door;
	sg_oracle_declared_action = old_action;
	sg_oracle_declared_touched = old_touched;
	return ok;
}

typedef struct
{
	edict_t *ent;
	vec3_t origin, old_origin, angles, velocity, avelocity;
	int state, linkcount;
	solid_t solid;
} sg_declared_door_pose_t;

/* Temporarily publish the declaration's exact physical TOP pose.  This is a
 * synchronous collision snapshot only: it calls no use/think/touch target,
 * changes no areaportal, and the server cannot interleave an entity frame.
 * Snapshot every member before relinking the first one so every successful
 * begin has one complete restoration scope. */
static int SG_OracleDeclaredDoorTopBegin(edict_t *trigger,
	sg_declared_door_pose_t *saved, int capacity)
{
	edict_t *members[SG_PHANTOM_ARMED_DOORS];
	int count, i;

	if (!saved || capacity <= 0)
		return -1;
	count = SG_DeclaredDoorMembers(trigger, members,
	    SG_PHANTOM_ARMED_DOORS);
	if (count <= 0 || count > capacity)
		return -1;
	for (i = 0; i < count; i++)
	{
		edict_t *member = members[i];

		saved[i].ent = member;
		VectorCopy(member->s.origin, saved[i].origin);
		VectorCopy(member->s.old_origin, saved[i].old_origin);
		VectorCopy(member->s.angles, saved[i].angles);
		VectorCopy(member->velocity, saved[i].velocity);
		VectorCopy(member->avelocity, saved[i].avelocity);
		saved[i].state = member->moveinfo.state;
		saved[i].solid = member->solid;
		saved[i].linkcount = member->linkcount;
	}
	for (i = 0; i < count; i++)
	{
		edict_t *member = members[i];

		if (!strcmp(member->classname, "func_door_rotating"))
			VectorCopy(member->moveinfo.end_angles, member->s.angles);
		else
		{
			VectorCopy(member->moveinfo.end_origin, member->s.origin);
			VectorCopy(member->moveinfo.end_origin, member->s.old_origin);
			VectorCopy(member->moveinfo.end_angles, member->s.angles);
		}
		VectorClear(member->velocity);
		VectorClear(member->avelocity);
		member->moveinfo.state = SG_PLAT_STATE_TOP;
		member->solid = SOLID_BSP;
		sg_host.linkentity(member);
	}
	return count;
}

static void SG_OracleDeclaredDoorTopEnd(sg_declared_door_pose_t *saved,
	int count)
{
	int i;

	for (i = 0; i < count; i++)
	{
		edict_t *member = saved[i].ent;

		VectorCopy(saved[i].origin, member->s.origin);
		VectorCopy(saved[i].old_origin, member->s.old_origin);
		VectorCopy(saved[i].angles, member->s.angles);
		VectorCopy(saved[i].velocity, member->velocity);
		VectorCopy(saved[i].avelocity, member->avelocity);
		member->moveinfo.state = saved[i].state;
		member->solid = saved[i].solid;
		sg_host.linkentity(member);
		member->linkcount = saved[i].linkcount;
	}
}

/* Loader-side replay of the complete serialized RL_DOOR witness.  Resolving
 * the mechanism and checking the sweep is not enough: a destination may be
 * outside the sweep yet unreachable at TOP, or its egress may outlast a team
 * member's open hold.  Re-run both controller phases against the same temporary
 * TOP collision pose used by generation, apply the shared exact timing formula,
 * then restore the live world before returning on every path. */
qboolean SG_OracleValidateDeclaredDoorLink(const vec3_t source,
	const vec3_t anchor, const vec3_t target, edict_t *trigger,
	int stored_cost_ms)
{
	sg_declared_door_pose_t saved[SG_PHANTOM_ARMED_DOORS];
	edict_t *resolved;
	int pose_count, approach_ms, touch_ms, egress_ms, contract_cost;
	qboolean old_loader_replay;
	qboolean valid = false;

	if (!source || !anchor || !target || !trigger || stored_cost_ms <= 0)
		return false;
	resolved = SG_DeclaredDoorForLink(anchor, source);
	if (!SG_DeclaredDoorSameSet(resolved, trigger) ||
	    !SG_DeclaredDoorOutsideSweep(trigger, target) ||
	    !SG_DeclaredDoorCrossesSweep(trigger, anchor, target))
		return false;
	pose_count = SG_OracleDeclaredDoorTopBegin(trigger, saved,
	    SG_PHANTOM_ARMED_DOORS);
	if (pose_count <= 0)
		return false;
	old_loader_replay = sg_oracle_loader_replay;
	sg_oracle_loader_replay = true;
	if (!SG_OracleDeclaredDoorApproach(source, anchor, trigger, &approach_ms,
	        &touch_ms) ||
	    !SG_OracleDeclaredDoorEgress(anchor, target, trigger, NULL, &egress_ms))
		goto restore;
	contract_cost = SG_DeclaredDoorContractCost(trigger, approach_ms, touch_ms,
	    egress_ms);
	valid = contract_cost > 0 && stored_cost_ms >= contract_cost;

restore:
	sg_oracle_loader_replay = old_loader_replay;
	SG_OracleDeclaredDoorTopEnd(saved, pose_count);
	return valid;
}

/*
 * One production end-frame pull. The fixed view is part of a proved graph
 * hook's command profile, so the current phantom origin plus that view yields
 * the same handed muzzle start the live weapon will use. Return the integer
 * rope length so the prover can make release decisions in the same units.
 */
static int SG_OracleHookPullVelocity(const sg_phantom_t *ph,
	const vec3_t bite, const vec3_t view_angles, int hand, vec3_t velocity)
{
	vec3_t angles, forward, right, muzzle;

	VectorCopy(view_angles, angles);
	AngleVectors(angles, forward, right, NULL);
	CTF_HookMuzzle(ph->origin, 22.0f, hand, forward, right, muzzle);
	return CTF_HookPullVelocity(muzzle, bite, velocity);
}

int SG_OracleHookStep(sg_phantom_t *ph, const vec3_t bite,
	const vec3_t view_angles, int hand)
{
	int rope;

	rope = SG_OracleHookPullVelocity(ph, bite, view_angles, hand,
		ph->velocity);

	/* write back into the fixed-point state Pmove will read */
	ph->pms.velocity[0] = (short)(ph->velocity[0] * 8.0f);
	ph->pms.velocity[1] = (short)(ph->velocity[1] * 8.0f);
	ph->pms.velocity[2] = (short)(ph->velocity[2] * 8.0f);
	return rope;
}

static qboolean SG_OracleSupportedArrivalMayTrace(const sg_phantom_t *ph,
	const vec3_t destination)
{
	vec3_t delta;

	if (!ph)
		return false;
	VectorSubtract(destination, ph->origin, delta);
	return delta[0] * delta[0] + delta[1] * delta[1] <
	           SG_REPLAY_ARRIVE_RADIUS * SG_REPLAY_ARRIVE_RADIUS &&
	       delta[2] > -SG_REPLAY_ARRIVE_Z &&
	       delta[2] < SG_REPLAY_ARRIVE_Z &&
	       (ph->groundentity || ph->waterlevel >= 2) &&
	       !(ph->waterlevel > 0 &&
	         (ph->watertype & (CONTENTS_LAVA | CONTENTS_SLIME)));
}

static void SG_OracleHookObservation(const sg_phantom_t *ph,
	const vec3_t bite, const vec3_t destination, const vec3_t view_angles,
	int hand, qboolean check_rope, qboolean check_contact, edict_t *passent,
	sg_replay_observation_t *observation)
{
	vec3_t pull_velocity;

	if (!observation)
		return;
	memset(observation, 0, sizeof(*observation));
	if (!ph)
		return;
	observation->contaminated = sg_oracle_contaminated;
	observation->door_passed = ph->door_passed;
	observation->contact_clear = true;
	if (!observation->contaminated && check_contact &&
	    SG_OracleSupportedArrivalMayTrace(ph, destination))
		observation->contact_clear = SG_OracleReplayContactClear(ph->origin,
			destination, passent);
	observation->hook_rope_valid = check_rope;
	if (check_rope)
		observation->hook_rope_length = SG_OracleHookPullVelocity(ph, bite,
			view_angles, hand, pull_velocity);
}

/* Shared graph-hook witness. The caller supplies a fully initialized
 * fixed-point phantom. One production pull owns each 100 ms interval; four
 * literal 25 ms zero commands consume it, then release hands off to the same
 * direct-yaw settlement controller runtime executes. */
qboolean SG_OracleHookTraverse(sg_phantom_t *ph, const vec3_t bite,
	const vec3_t destination, const vec3_t view_angles, int hand,
	int flight_ms, int settle_limit_ms, float old_frame_z,
	sg_hook_proof_t *proof, edict_t *passent, qboolean world_only)
{
	sg_hook_replay_spec_t spec;
	sg_hook_replay_state_t state;
	sg_replay_pose_t pose;
	sg_replay_observation_t observation;
	sg_replay_status_t status;
	usercmd_t cmd;
	qboolean result = false;
	edict_t *previous_passent;
	qboolean previous_world_only, previous_contaminated;

	if (!ph || !proof || flight_ms < SG_REPLAY_FRAME_MS ||
	    flight_ms > SG_REPLAY_HOOK_FLIGHT_MAX_MS ||
	    (flight_ms % SG_REPLAY_FRAME_MS) != 0 ||
	    settle_limit_ms < RUNE_HOOK_DRY_SETTLE_MS ||
	    settle_limit_ms > RUNE_HOOK_WATER_SETTLE_MS)
		return false;
	memset(proof, 0, sizeof(*proof));
	/* The server is single-threaded, but keep this API scoped so every return
	 * restores the default offline context. */
	previous_passent = sg_oracle_passent;
	previous_world_only = sg_oracle_world_only;
	previous_contaminated = sg_oracle_contaminated;
	sg_oracle_passent = passent;
	sg_oracle_world_only = world_only;
	sg_oracle_contaminated = false;
	if (SG_OracleTriggerOverlap(ph) || SG_OracleSolidOverlap(ph))
		goto done;
	memset(&spec, 0, sizeof(spec));
	VectorCopy(bite, spec.bite);
	VectorCopy(destination, spec.destination);
	VectorCopy(view_angles, spec.view_angles);
	spec.flight_ms = flight_ms;
	spec.settle_limit_ms = settle_limit_ms;
	spec.expected_release_ms = SG_REPLAY_TIME_DISCOVER;
	spec.expected_pull_ms = SG_REPLAY_TIME_DISCOVER;
	spec.expected_settle_arrival_ms = SG_REPLAY_TIME_DISCOVER;
	spec.expected_settle_ms = SG_REPLAY_TIME_DISCOVER;
	SG_OracleReplayPose(ph, &pose);
	SG_OracleHookObservation(ph, bite, destination, view_angles, hand,
		false, false, passent, &observation);
	status = SG_HookReplayBegin(&state, &spec, &pose, &observation,
		old_frame_z);
	while (status == SG_REPLAY_RUNNING)
	{
		qboolean check_contact;

		if (state.phase == SG_HOOK_REPLAY_WAIT_ATTACH)
		{
			status = SG_HookReplayAttached(&state, &pose);
			if (status == SG_REPLAY_RUNNING)
			{
				proof->attach_pms = state.attach_pms;
				proof->attach_groundentity = state.attach_grounded;
				proof->attach_watertype = state.attach_watertype;
				proof->attach_waterlevel = state.attach_waterlevel;
			}
			continue;
		}
		if (state.phase == SG_HOOK_REPLAY_WAIT_PULL)
		{
			SG_OracleHookStep(ph, bite, view_angles, hand);
			SG_OracleReplayPose(ph, &pose);
			status = SG_HookReplayPullApplied(&state, &pose);
			continue;
		}
		/* Legacy settlement checks once at each frame start.  Reuse a
		 * post-command observation within the frame so a failed contact trace
		 * is not repeated before the next command. */
		if (state.phase == SG_HOOK_REPLAY_SETTLE &&
		    state.phase_step == 0 && !state.arrived_in_frame)
			SG_OracleHookObservation(ph, bite, destination, view_angles, hand,
				false, true, passent, &observation);
		status = SG_HookReplayPreStep(&state, &pose, &observation, &cmd);
		if (status != SG_REPLAY_RUNNING)
			break;
		SG_OracleRun(ph, &cmd, 1);
		SG_OracleReplayPose(ph, &pose);
		check_contact = state.phase == SG_HOOK_REPLAY_SETTLE &&
			(!state.arrived_in_frame ||
			 (state.phase_step + 1 ==
			      SG_REPLAY_FRAME_MS / SG_REPLAY_STEP_MS &&
			  SG_ReplayFallDelta(state.progress.old_frame_z,
			      pose.velocity[2], pose.grounded, pose.waterlevel) <=
			      SG_RUNE_PROOF_DAMAGING_FALL_DELTA));
		SG_OracleHookObservation(ph, bite, destination, view_angles, hand,
			!sg_oracle_contaminated &&
			    ((state.phase == SG_HOOK_REPLAY_ATTACH_FRAME &&
			      state.phase_step + 1 ==
			          SG_REPLAY_FRAME_MS / SG_REPLAY_STEP_MS) ||
			     (state.phase == SG_HOOK_REPLAY_PULL_FRAME &&
			      !state.release_requested)),
			check_contact, passent, &observation);
		/* If substep four first discovers settlement, legacy immediately
		 * performs a second same-pose trace for terminal persistence after its
		 * boundary hazard checks.  Preserve that call only when those checks
		 * can pass; an already-latched arrival used the single trace above as
		 * its persistence check. */
		if (!observation.contaminated &&
		    state.phase == SG_HOOK_REPLAY_SETTLE &&
		    !state.arrived_in_frame &&
		    state.phase_step + 1 ==
		        SG_REPLAY_FRAME_MS / SG_REPLAY_STEP_MS &&
		    SG_OracleSupportedArrivalMayTrace(ph, destination) &&
		    observation.contact_clear &&
		    SG_ReplayFallDelta(state.progress.old_frame_z,
		        pose.velocity[2], pose.grounded, pose.waterlevel) <=
		        SG_RUNE_PROOF_DAMAGING_FALL_DELTA)
			observation.contact_clear = SG_OracleReplayContactClear(
				ph->origin, destination, passent);
		status = SG_HookReplayPostStep(&state, &pose, &observation);
		if (status == SG_REPLAY_RUNNING && state.release_requested &&
		    !state.release_applied)
		{
			/* ctf_hook_abort clears vertical velocity and oldvelocity Z on
			 * support.  The pure reducer owns the history; this adapter owns
			 * the exact external phantom write. */
			if (ph->groundentity)
			{
				ph->velocity[2] = 0.0f;
				ph->pms.velocity[2] = 0;
			}
			SG_OracleReplayPose(ph, &pose);
			status = SG_HookReplayReleaseApplied(&state, &pose);
		}
	}
	if (status == SG_REPLAY_ARRIVED ||
	    (status == SG_REPLAY_FAILED &&
	     state.progress.reason == SG_REPLAY_REASON_DOOR_PASSED &&
	     state.progress.arrival_ms != SG_REPLAY_TIME_DISCOVER))
	{
		proof->pull_ms = state.pull_ms;
		proof->release_ms = state.release_ms;
		proof->settle_arrival_ms = state.settle_arrival_ms;
		proof->settle_ms = state.settle_ms;
		proof->exit_speed = state.progress.exit_speed;
		result = status == SG_REPLAY_ARRIVED;
	}
done:
	if (ph->door_passed)
		result = false;
	sg_oracle_passent = previous_passent;
	sg_oracle_world_only = previous_world_only;
	sg_oracle_contaminated = previous_contaminated;
	return result;
}

/*
 * ------------------------------------------------------------- rocket jump
 *
 * A rocket jump is not a movement rule. It is a DAMAGE event the mover
 * arranges to happen underneath himself: the splash of his own rocket pushes
 * him, and the push stacks on the jump pmove already gave him. Nothing here
 * is tuned; every number is read out of this tree.
 *
 *   the shot    Weapon_RocketLauncher_Fire calls fire_rocket with speed 650,
 *               damage_radius 120 and radius_damage 120 -- the live #else
 *               branch (p_weapon.c:864-866, :889). The CTF_WEAP_BALANCE
 *               numbers above them sit inside #ifdef WEAP_BALANCE_OK, which
 *               nothing in the build defines, so they are dead text.
 *   the muzzle  VectorSet(offset, 8, 8, ent->viewheight-8) then
 *               P_ProjectSource (p_weapon.c:880-881), which is
 *               G_ProjectSource (g_utils.c:7-12) for a right-handed client:
 *               origin + forward*8 + right*8 + (0,0,14), viewheight being 22
 *               (p_client.c:1923).
 *   the flight  MOVETYPE_FLYMISSILE with mins and maxs cleared
 *               (g_weapon.c:692-696): a POINT travelling a straight line at
 *               650 u/s, clipped by MASK_SHOT, no gravity. A point trace is
 *               therefore the projectile's own path, not an approximation of
 *               it. Sky ends the rocket without an explosion
 *               (rocket_touch, g_weapon.c:635-637).
 *   the owner   rocket_touch returns the instant it strikes its owner
 *               (g_weapon.c:631-632), so the jumper never eats the 100-119
 *               direct hit (p_weapon.c:851) and never takes its knockback,
 *               which is zero anyway (g_weapon.c:653). ALL of the push and
 *               ALL of the self-damage come from T_RadiusDamage.
 *   the splash  T_RadiusDamage (g_combat.c:698-756): the distance is measured
 *               from the explosion to the target's bbox CENTRE, not its
 *               origin (g_combat.c:716-718); points = damage - 0.5*dist
 *               (g_combat.c:742); halved because the jumper is his own
 *               attacker (g_combat.c:745-746); and findradius hard-culls
 *               anything past the radius (g_utils.c:60-83), so outside 120
 *               units nothing happens at all -- there is no taper to zero.
 *               (int)points is passed as BOTH the damage and the knockback
 *               (g_combat.c:752).
 *   the push    T_Damage normalises dir (g_combat.c:471) and, because the
 *               attacker IS the target, scales by 1600 instead of 500 -- the
 *               line id itself labelled "the rocket jump hack"
 *               (g_combat.c:491-494). Mass is 200 for a player
 *               (p_client.c:1926), floored at 50, so the kick is exactly
 *               8 units/s per point of knockback. A body still standing on
 *               the floor cannot be pushed down into it (g_combat.c:505-508).
 *   the health  take starts at (int)points and only ever comes DOWN from
 *               there -- power armour, the Resist rune, armour
 *               (g_combat.c:534-548). A link records the WORST case, which is
 *               also the honest one for a generator that cannot know what the
 *               jumper will be wearing: an unarmoured body pays all of it.
 *
 * Nothing above says anything about how high the jumper goes. That is
 * pmove's business and it is never assumed here: the caller applies this
 * force and then lets SG_OracleRun integrate, exactly as with the hook.
 */
#define SG_RJ_RADIUS_DAMAGE	120.0f		/* p_weapon.c:865 */
#define SG_RJ_DAMAGE_RADIUS	120.0f		/* p_weapon.c:866 */
#define SG_RJ_ROCKET_SPEED	650.0f		/* p_weapon.c:889 */
#define SG_RJ_PLAYER_MASS	200.0f		/* p_client.c:1926 */
#define SG_RJ_VIEWHEIGHT	22.0f		/* p_client.c:1923 */
#define SG_RJ_BBOX_CENTRE	4.0f		/* 0.5*(mins+maxs) z for a standing
                                         * player box, g_combat.c:716-717 */

/*
 * Where does the rocket a body fires from here, along this aim, actually go
 * off -- and how long does it take to get there? Both are geometry plus the
 * game's own muzzle offset and projectile speed, so both belong here rather
 * than in a prover.
 *
 * Returns false when the shot never detonates near the shooter: no surface
 * within the trace, or sky (which frees the rocket silently).
 */
qboolean SG_OracleRocketJumpAim(vec3_t origin, vec3_t aim,
                                vec3_t boom_out, float *flight_ms)
{
	vec3_t angles, forward, right, start, end, d;
	trace_t tr;
	float travel;

	vectoangles(aim, angles);
	AngleVectors(angles, forward, right, NULL);

	/* p_weapon.c:880-881 -> g_utils.c:7-12, right-handed */
	start[0] = origin[0] + forward[0] * 8.0f + right[0] * 8.0f;
	start[1] = origin[1] + forward[1] * 8.0f + right[1] * 8.0f;
	start[2] = origin[2] + forward[2] * 8.0f + right[2] * 8.0f
	           + (SG_RJ_VIEWHEIGHT - 8.0f);

	VectorMA(start, 8192.0f, forward, end);
	tr = sg_host.trace(start, NULL, NULL, end, NULL, MASK_SHOT);
	if (tr.startsolid || tr.allsolid || tr.fraction >= 1.0f)
		return false;
	if (tr.surface && (tr.surface->flags & SURF_SKY))
		return false;               /* g_weapon.c:635-637: no explosion */

	VectorCopy(tr.endpos, boom_out);
	VectorSubtract(boom_out, start, d);
	travel = VectorLength(d);
	*flight_ms = travel / SG_RJ_ROCKET_SPEED * 1000.0f;
	return true;
}

/*
 * The detonation itself, applied to a phantom exactly as T_RadiusDamage ->
 * T_Damage applies it to a real player, and in the same place in the frame:
 * the game code changes velocity, then Pmove integrates the result. A rollout
 * therefore rolls the flight time with SG_OracleRun, calls this once at the
 * moment the rocket arrives, and keeps rolling.
 *
 * Returns the health the jumper pays -- 0 when the burst is out of range or
 * on the wrong side of a wall, in which case nothing was applied.
 */
int SG_OracleRocketJumpStep(sg_phantom_t *ph, vec3_t boom)
{
	vec3_t centre, v, dir, kvel;
	float dist, points, len;
	int knockback;
	trace_t tr;

	/* distance to the bbox CENTRE, g_combat.c:716-718 */
	VectorCopy(ph->origin, centre);
	centre[2] += SG_RJ_BBOX_CENTRE;
	VectorSubtract(boom, centre, v);
	dist = VectorLength(v);

	/* findradius hard-culls past the radius, g_utils.c:60-83 */
	if (dist > SG_RJ_DAMAGE_RADIUS)
		return 0;

	/*
	 * CanDamage (g_combat.c:14-70, called at :749): a point trace from the
	 * explosion to the target ORIGIN against MASK_SOLID, and when that is
	 * blocked, four more to the corners at +/-15 in x and y. Same five traces
	 * here, with the phantom's null passent.
	 */
	{
		static const float corners[5][2] = {
			{ 0.0f, 0.0f }, { 15.0f, 15.0f }, { 15.0f, -15.0f },
			{ -15.0f, 15.0f }, { -15.0f, -15.0f },
		};
		int c;
		qboolean seen = false;

		for (c = 0; c < 5 && !seen; c++)
		{
			vec3_t dest;

			VectorCopy(ph->origin, dest);
			dest[0] += corners[c][0];
			dest[1] += corners[c][1];
			tr = sg_host.trace(boom, vec3_origin, vec3_origin, dest, NULL,
			              MASK_SOLID);
			if (tr.fraction == 1.0f)
				seen = true;
		}
		if (!seen)
			return 0;
	}

	/* points = damage - 0.5*dist (g_combat.c:742), halved for self
	 * (g_combat.c:745-746), and only a positive result does anything
	 * (g_combat.c:747) */
	points = (SG_RJ_RADIUS_DAMAGE - 0.5f * dist) * 0.5f;
	if (points <= 0.0f)
		return 0;
	knockback = (int)points;        /* g_combat.c:752 passes (int)points as
	                                 * the damage AND as the knockback */

	/* dir runs explosion -> target ORIGIN (g_combat.c:751) -- the origin, not
	 * the centre the distance was measured to -- and T_Damage normalises it
	 * (g_combat.c:471) */
	VectorSubtract(ph->origin, boom, dir);
	len = VectorLength(dir);
	if (len < 0.001f)
		return 0;
	VectorScale(dir, 1.0f / len, dir);

	/* "the rocket jump hack": 1600 for self-damage, 500 for everything else
	 * (g_combat.c:491-494), over a player's mass of 200 (p_client.c:1926) */
	VectorScale(dir, 1600.0f * (float)knockback / SG_RJ_PLAYER_MASS, kvel);

	/* standing on the floor cannot be pushed into it, g_combat.c:505-508 */
	if (ph->groundentity && kvel[2] < 0.0f)
		kvel[2] = 0.0f;

	/* VectorAdd(targ->velocity, kvel, targ->velocity), g_combat.c:510 --
	 * an ADD onto whatever pmove left there, which is what makes the jump
	 * and the blast stack */
	VectorAdd(ph->velocity, kvel, ph->velocity);

	/* write back into the fixed-point state Pmove will read, as the hook
	 * step does */
	ph->pms.velocity[0] = (short)(ph->velocity[0] * 8.0f);
	ph->pms.velocity[1] = (short)(ph->velocity[1] * 8.0f);
	ph->pms.velocity[2] = (short)(ph->velocity[2] * 8.0f);

	return knockback;               /* == the damage taken, worst case */
}

/*
 * The highest a rocket jump can possibly lift a body, from the arithmetic
 * above and nothing else. This is a CANDIDATE FILTER, not a claim: it says
 * which pairs are worth handing to the prover, and the prover still has to
 * roll the real physics for a link to exist.
 *
 * The strongest burst the game can produce under a standing player is one on
 * the floor directly beneath him: his box bottom is 24 below his origin
 * (p_client.c's player mins) and the centre the damage is measured to sits 4
 * above it, so the shortest distance is 28 and no arrangement of aim can beat
 * it. That gives points = (120 - 14)/2 = 53, a kick of 8*53 = 424 up.
 *
 * On top of that goes the jump pmove hands out. PM_CheckJump lives in the
 * engine, not in this tree, so its 270 is the one constant here that cannot
 * carry a line number from these sources -- which is exactly why it is
 * confined to this filter and kept out of every proof: SG_OracleRun gets the
 * real number from sg_host.pmove every time it steps.
 *
 * Ballistic rise for the stacked launch speed, under the server's own
 * gravity: v^2 / 2g. At sv_gravity 800 that is (424+270)^2/1600 = 301 units.
 * Any real jump comes in under it -- the rocket takes time to reach the
 * floor, and by the time it detonates the body has already left, which puts
 * the burst further away and costs it kick.
 */
float SG_OracleRocketJumpCeiling(void)
{
	float points = (SG_RJ_RADIUS_DAMAGE - 0.5f * (24.0f + SG_RJ_BBOX_CENTRE))
	               * 0.5f;
	float kick = 1600.0f * (float)(int)points / SG_RJ_PLAYER_MASS;
	float launch = kick + 270.0f;   /* PM_CheckJump, engine pmove */
	float g = (sv_gravity && sv_gravity->value > 1.0f)
	              ? sv_gravity->value : 800.0f;

	return launch * launch / (2.0f * g);
}

/*
 * Stand a phantom up at a position, at rest, feet on whatever is below.
 * The usual way a generator seed begins.
 */
void SG_OraclePlace(sg_phantom_t *ph, vec3_t origin)
{
	memset(ph, 0, sizeof(*ph));
	ph->pms.origin[0] = (short)(origin[0] * 8.0f);
	ph->pms.origin[1] = (short)(origin[1] * 8.0f);
	ph->pms.origin[2] = (short)(origin[2] * 8.0f);
	/* Pmove owns an eighth-unit fixed-point origin. Expose that decoded state
	 * immediately: hook/RJ helpers run before the first SG_OracleRun and must
	 * not see an unrepresentable float position that a live client cannot have. */
	ph->origin[0] = ph->pms.origin[0] * 0.125f;
	ph->origin[1] = ph->pms.origin[1] * 0.125f;
	ph->origin[2] = ph->pms.origin[2] * 0.125f;
	ph->pms.pm_type = PM_NORMAL;
	/*
	 * Weight. pmove applies pm->s.gravity, which the game sets per client
	 * from sv_gravity every frame (p_client.c:2798) -- and a memset phantom
	 * had zero. Every drop proof ever attempted stepped off its lip and
	 * LEVITATED at source height until the budget died; jumps rose 270 and
	 * never came back; only the hook proofs survived, because the rope
	 * overwrites velocity wholesale. One uninitialized field, four failed
	 * prover designs built on top of it.
	 */
	/* Existing v2 loader/runtime replays retain their fixed 800 law exactly.
	 * Only Rune_Generate owns the short-lived v3 proof scope, populated from
	 * the exact integral law captured before generation and reset on every
	 * exit.  This keeps a v2-compatible active value such as 800.5 on the old
	 * short/800 behavior while allowing an honest v3 proof at gravity 650. */
	ph->pms.gravity = SG_RuneProofGravity();
	ph->old_pms = ph->pms;
}
