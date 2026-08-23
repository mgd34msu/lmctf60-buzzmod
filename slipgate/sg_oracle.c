

#include <float.h>
#include <limits.h>
#include <stdint.h>

#include "g_local.h"
#include "slipgate/sg_compound.h"
#include "slipgate/sg_compound_world.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_replay.h"
#include "slipgate/sg_rune_binding.h"
#include "slipgate/sg_rune_mechanism_catalog.h"
#include "slipgate/sg_rune_proof.h"
#include "slipgate/sg_rocketjump_live.h"
#include "slipgate/sg_rocketjump_impact.h"
#include "slipgate/sg_shoot_door_live.h"
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_util.h"
#include "slipgate/sg_door_approach.h"

void Touch_DoorTrigger(edict_t *self, edict_t *other, cplane_t *plane,
	csurface_t *surf);
void Touch_Multi(edict_t *self, edict_t *other, cplane_t *plane,
	csurface_t *surf);
void button_touch(edict_t *self, edict_t *other, cplane_t *plane,
	csurface_t *surf);
void button_use(edict_t *self, edict_t *other, edict_t *activator);
void button_killed(edict_t *self, edict_t *inflictor, edict_t *attacker,
	int damage, vec3_t point);
void door_secret_use(edict_t *self, edict_t *other, edict_t *activator);
void door_use(edict_t *self, edict_t *other, edict_t *activator);
void door_go_down(edict_t *self);
void door_hit_top(edict_t *self);
void door_hit_bottom(edict_t *self);
void door_blocked(edict_t *self, edict_t *other);
void Move_Begin(edict_t *self);
void Move_Final(edict_t *self);
void Move_Done(edict_t *self);
void AngleMove_Begin(edict_t *self);
void AngleMove_Final(edict_t *self);
void AngleMove_Done(edict_t *self);
void Think_CalcMoveSpeed(edict_t *self);
void Think_SpawnDoorTrigger(edict_t *self);
void hook_touch(edict_t *self, edict_t *other, cplane_t *plane,
	csurface_t *surf);
void hook_die(edict_t *self, edict_t *inflictor, edict_t *attacker,
	int damage, vec3_t point);
void trigger_relay_use(edict_t *self, edict_t *other, edict_t *activator);
void Use_Target_Speaker(edict_t *self, edict_t *other, edict_t *activator);
void trigger_push_touch(edict_t *self, edict_t *other, cplane_t *plane,
	csurface_t *surf);

#define SG_PUSH_PROOF_LIMIT_MS 10000

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
static edict_t *sg_oracle_declared_entry;
static edict_t *sg_oracle_declared_door;
/* The generator may synchronously pose one catalog-authenticated func_button
 * at its sealed TOP endpoint while proving the post-activation suffix.  This
 * scope is deliberately private: ordinary admission continues to require the
 * sealed BOTTOM state. */
static edict_t *sg_oracle_declared_button_top;
static const sg_rune_mechanism_binding_t *sg_oracle_bound_door;
static int sg_oracle_declared_action;
static qboolean sg_oracle_declared_touched;
/* Compound PREOPEN owns one exact trigger and one exact translating leaf.
 * Keep this narrower than ordinary RL_DOOR set equivalence so proof cannot
 * fan out to another activator or team member. */
static edict_t *sg_oracle_compound_trigger;
static edict_t *sg_oracle_compound_member;
static qboolean sg_oracle_compound_touched;
/* Rune loading may happen after humans have joined.  Its map-contract replay
 * must not depend on a transient client or that client's projectile occupying
 * the path at this instant; the normal offline/live proofs remain deliberately
 * conservative about those bodies. */
static qboolean sg_oracle_loader_replay;

static qboolean SG_OracleDeclaredActivatorSafe(edict_t *trigger);
static qboolean SG_OracleDeclaredActivatorSafeWithDelay(edict_t *trigger,
	qboolean require_positive_delay);
static qboolean SG_OracleDeclaredTriggerContains(edict_t *trigger,
	const vec3_t origin);
static qboolean SG_OracleDeclaredButtonDoorSafe(edict_t *button);
static qboolean SG_OracleDeclaredButtonTopSafe(edict_t *button);
static qboolean SG_OracleDeclaredDoorSourceSafe(edict_t *source);
static qboolean SG_OracleDeclaredSameDoorSet(edict_t *a, edict_t *b);
static qboolean SG_OracleBoundSameDoorSet(
	const sg_rune_mechanism_binding_t *binding, edict_t *trigger);
static qboolean SG_OracleTriggerOverlap(sg_phantom_t *ph);
static qboolean SG_OracleSolidOverlap(sg_phantom_t *ph);
static qboolean SG_OracleRotatingDoorBoxOverlapIsCoarse(edict_t *door,
	const sg_phantom_t *ph);
static int SG_OracleLiveEdictIndex(const edict_t *ent);
static qboolean SG_OracleMoverSweepIdentity(edict_t *member);
static int SG_DeclaredDoorMembersResolved(edict_t *source,
	edict_t **members, int capacity);

/* A synchronous loader replay deliberately poses authenticated movers without
 * running their callbacks.  During that scope the immutable sealed topology
 * remains authoritative, while every live controller path continues to
 * require the controller-owned execution-state law. */
static qboolean SG_OracleBoundDoorBindingCurrent(
	const sg_rune_mechanism_binding_t *binding)
{
	if (!binding || !binding->link ||
	    (binding->link->action != RL_DOOR &&
	     binding->link->action != RL_BUTTON_DOOR))
		return false;
	return sg_oracle_loader_replay
	    ? (SG_RuneMechanismBindingTopologyCurrent(binding) ? true : false)
	    : (SG_RuneMechanismBindingCurrent(binding) ? true : false);
}

static int SG_OracleBoundDoorMoverKeys(
	const sg_rune_mechanism_binding_t *binding,
	uint32_t keys[SG_RUNE_BINDING_MAX_MOVERS], size_t *count)
{
	return sg_oracle_loader_replay
	    ? SG_RuneMechanismBindingTopologyMoverKeys(binding, keys, count)
	    : SG_RuneMechanismBindingMoverKeys(binding, keys, count);
}

static edict_t *SG_OracleBoundDoorResolveNode(
	const sg_rune_mechanism_binding_t *binding, uint32_t key)
{
	return sg_oracle_loader_replay
	    ? SG_RuneMechanismBindingResolveTopologyNode(binding, key)
	    : SG_RuneMechanismBindingResolveNode(binding, key);
}

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

static qboolean SG_OracleRotatorEntitySweepBlocks(const edict_t *rotator,
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
	if (!rotator || !start || !end || !SG_OracleFinite3(start) ||
	    !SG_OracleFinite3(end) ||
	    (!!hull_mins != !!hull_maxs) ||
	    (hull_mins && (!SG_OracleFinite3(hull_mins) ||
	                   !SG_OracleFinite3(hull_maxs) ||
	                   hull_mins[0] > hull_maxs[0] ||
	                   hull_mins[1] > hull_maxs[1] ||
	                   hull_mins[2] > hull_maxs[2])))
		return true;
	if (rotator->solid != SOLID_BSP || !rotator->classname ||
	    strcmp(rotator->classname, "func_rotating"))
		return false;
	if (!SG_OracleFinite3(rotator->s.origin) ||
	    !SG_OracleFinite3(rotator->mins) ||
	    !SG_OracleFinite3(rotator->maxs) ||
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

		if (rotator->solid != SOLID_BSP || !rotator->classname ||
		    strcmp(rotator->classname, "func_rotating"))
			continue;
		if (SG_OracleRotatorEntitySweepBlocks(rotator, start, hull_mins,
		        hull_maxs, end, contentmask))
			return true;
	}
	return false;
}

static qboolean SG_OracleDeclaredTrigger(edict_t *trigger)
{
	if (!trigger || !sg_oracle_declared_expected)
		return false;
	if (sg_oracle_declared_entry)
		return trigger == sg_oracle_declared_entry;
	if (sg_oracle_declared_action == RL_TELEPORT)
		return trigger->owner == sg_oracle_declared_expected;
	if (sg_oracle_declared_action == RL_LIFT)
		return trigger->enemy == sg_oracle_declared_expected;
	if (sg_oracle_declared_action == RL_DOOR)
		return trigger == sg_oracle_declared_expected;
	if (sg_oracle_declared_action == RL_PUSH)
		return trigger == sg_oracle_declared_expected;
	return false;
}

qboolean SG_OracleDeclaredApproachTriggerAllowed(int action,
	edict_t *declared, edict_t *actual)
{
	if (!declared || !actual)
		return false;
	if (declared == actual)
		return true;
	if (action == RL_DOOR || action == RL_BUTTON_DOOR)
		return SG_OracleDeclaredSameDoorSet(declared, actual);
	return action == RL_LIFT &&
	       SG_DeclaredDoorSameSet(declared, actual);
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
static qboolean SG_OracleRotatingDoorIntervalBounds(edict_t *door,
	const vec3_t local_mins, const vec3_t local_maxs,
	float interval_first_degrees, float interval_second_degrees,
	float sample_first_degrees, float sample_second_degrees,
	float sample_third_degrees,
	vec3_t dmins, vec3_t dmaxs)
{
	const float rad = (float)(M_PI * 2.0 / 360.0);
	const float deg = (float)(360.0 / (M_PI * 2.0));
	int angle_axis = -1, changed = 0;
	float first_angle, second_angle, low, high;
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
	first_angle = interval_first_degrees * rad;
	second_angle = interval_second_degrees * rad;
	if (!isfinite(first_angle) || !isfinite(second_angle) ||
	    !isfinite(sample_first_degrees) ||
	    !isfinite(sample_second_degrees) ||
	    !isfinite(sample_third_degrees) ||
	    fabsf(second_angle - first_angle) > 8.0f * M_PI)
		return false;
	low = first_angle < second_angle ? first_angle : second_angle;
	high = first_angle > second_angle ? first_angle : second_angle;
	VectorSet(dmins, 1.0e30f, 1.0e30f, 1.0e30f);
	VectorSet(dmaxs, -1.0e30f, -1.0e30f, -1.0e30f);
	for (corner = 0; corner < 8; corner++)
	{
		vec3_t local, p0, p90, p180, point;

		local[0] = (corner & 1) ? local_maxs[0] : local_mins[0];
		local[1] = (corner & 2) ? local_maxs[1] : local_mins[1];
		local[2] = (corner & 4) ? local_maxs[2] : local_mins[2];
		SG_OracleRotatingPoint(door, local, angle_axis,
		                           sample_first_degrees, point);
		SG_OracleBoundsAdd(point, dmins, dmaxs);
		SG_OracleRotatingPoint(door, local, angle_axis,
		                           sample_second_degrees, point);
		SG_OracleBoundsAdd(point, dmins, dmaxs);
		/* Preserve every direct engine-visible endpoint/current transform:
		 * coefficient recovery follows a different float path and can otherwise
		 * miss a low bit at large world origins. */
		SG_OracleRotatingPoint(door, local, angle_axis,
		                           sample_third_degrees, point);
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
			float first_value, last_value;
			int first, last, k;

			if (fabsf(a) + fabsf(b) <= 0.0001f)
				continue;
			base = atan2f(b, a);
			first_value = ceilf((low - base) / (float)M_PI);
			last_value = floorf((high - base) / (float)M_PI);
			if (!isfinite(first_value) || !isfinite(last_value) ||
			    (double)first_value < (double)INT_MIN ||
			    (double)first_value > (double)INT_MAX ||
			    (double)last_value < (double)INT_MIN ||
			    (double)last_value > (double)INT_MAX)
				return false;
			first = (int)first_value;
			last = (int)last_value;
			for (k = first; k <= last; )
			{
				float at = base + (float)k * (float)M_PI;

				SG_OracleRotatingPoint(door, local, angle_axis,
				                           at * deg, point);
				SG_OracleBoundsAdd(point, dmins, dmaxs);
				if (k == last)
					break;
				k++;
			}
		}
	}
	return true;
}

static qboolean SG_OracleRotatingDoorBounds(edict_t *door,
	const vec3_t local_mins, const vec3_t local_maxs,
	vec3_t dmins, vec3_t dmaxs)
{
	float low, high;
	int angle_axis = -1, changed = 0;
	int axis;

	for (axis = 0; axis < 3; axis++)
	{
		if (fabsf(door->moveinfo.end_angles[axis] -
		          door->moveinfo.start_angles[axis]) <= 0.001f)
			continue;
		angle_axis = axis;
		changed++;
	}
	if (changed != 1)
		return false;
	low = door->moveinfo.start_angles[angle_axis] <
	          door->moveinfo.end_angles[angle_axis]
	    ? door->moveinfo.start_angles[angle_axis]
	    : door->moveinfo.end_angles[angle_axis];
	high = door->moveinfo.start_angles[angle_axis] >
	           door->moveinfo.end_angles[angle_axis]
	    ? door->moveinfo.start_angles[angle_axis]
	    : door->moveinfo.end_angles[angle_axis];
	/* AngleMove's final divide/multiply can leave the linked leaf a few float
	 * steps past the serialized endpoint.  Include that authenticated current
	 * pose and the complete sliver leading to it. */
	if (door->s.angles[angle_axis] < low)
		low = door->s.angles[angle_axis];
	if (door->s.angles[angle_axis] > high)
		high = door->s.angles[angle_axis];
	return SG_OracleRotatingDoorIntervalBounds(door, local_mins, local_maxs,
	                                           low, high,
	                                           door->moveinfo.start_angles[angle_axis],
	                                           door->moveinfo.end_angles[angle_axis],
	                                           door->s.angles[angle_axis],
	                                           dmins, dmaxs);
}

/* The partial-arc extrema above and the engine-visible corner transform take
 * different floating-point paths: the former recovers sinusoid coefficients,
 * while the latter evaluates AngleVectors directly.  A single nextafter is
 * not a sufficient enclosure when coefficient recovery loses low bits against
 * a large world origin.  Keep a full 1/8-unit server-physics lattice margin,
 * then add a 128-operation relative roundoff envelope over every magnitude
 * participating in the transform.  This remains narrow on normal doors while
 * making an inclusive guard conservative across both evaluation paths. */
static void SG_OracleRotatingBoundsOutward(edict_t *door,
	const vec3_t local_mins, const vec3_t local_maxs,
	vec3_t dmins, vec3_t dmaxs)
{
	double scale = 1.0;
	double pad;
	int axis;

	for (axis = 0; axis < 3; axis++)
	{
		scale += fabs((double)door->s.origin[axis]);
		scale += fabs((double)local_mins[axis]);
		scale += fabs((double)local_maxs[axis]);
	}
	pad = 0.125 + 128.0 * (double)FLT_EPSILON * scale;
	for (axis = 0; axis < 3; axis++)
	{
		double low, high;
		float rounded;

		if (!isfinite(dmins[axis]) || !isfinite(dmaxs[axis]) ||
		    !isfinite(pad) || pad >= (double)FLT_MAX)
		{
			dmins[axis] = -FLT_MAX;
			dmaxs[axis] = FLT_MAX;
			continue;
		}
		low = (double)dmins[axis] - pad;
		high = (double)dmaxs[axis] + pad;
		if (low <= -(double)FLT_MAX)
			dmins[axis] = -FLT_MAX;
		else
		{
			rounded = (float)low;
			dmins[axis] = rounded <= -FLT_MAX
			    ? -FLT_MAX : nextafterf(rounded, -INFINITY);
		}
		if (high >= (double)FLT_MAX)
			dmaxs[axis] = FLT_MAX;
		else
		{
			rounded = (float)high;
			dmaxs[axis] = rounded >= FLT_MAX
			    ? FLT_MAX : nextafterf(rounded, INFINITY);
		}
	}
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

		if (!SG_OracleRotatingDoorBounds(door, local_mins, local_maxs,
		                                       dmins, dmaxs))
		{
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
		}
		SG_OracleRotatingBoundsOutward(door, local_mins, local_maxs,
		                                     dmins, dmaxs);
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

typedef struct sg_oracle_door_bounds_state_s
{
	qboolean rotating;
	int use_kind;
	vec3_t origin;
	vec3_t angles;
	vec3_t mins;
	vec3_t maxs;
	vec3_t absmin;
	vec3_t absmax;
	vec3_t pos1;
	vec3_t pos2;
	vec3_t start_origin;
	vec3_t end_origin;
	vec3_t start_angles;
	vec3_t end_angles;
} sg_oracle_door_bounds_state_t;

typedef struct sg_oracle_door_bounds_cache_s
{
	sg_oracle_door_bounds_state_t state;
	qboolean has_hull;
	vec3_t hull_mins;
	vec3_t hull_maxs;
	vec3_t mins;
	vec3_t maxs;
	struct sg_oracle_door_bounds_cache_s *next;
} sg_oracle_door_bounds_cache_t;

static sg_oracle_door_bounds_cache_t
	*sg_oracle_door_bounds_cache[MAX_EDICTS];
static qboolean sg_oracle_door_bounds_cache_active;

static int SG_OracleDoorBoundsUseKind(const edict_t *door)
{
	if (door->use == door_use)
		return 1;
	if (door->use == door_secret_use)
		return 2;
	return 0;
}

static void SG_OracleDoorBoundsStateCapture(
	sg_oracle_door_bounds_state_t *state, const edict_t *door)
{
	memset(state, 0, sizeof(*state));
	state->rotating = !strcmp(door->classname, "func_door_rotating");
	state->use_kind = SG_OracleDoorBoundsUseKind(door);
	VectorCopy(door->mins, state->mins);
	VectorCopy(door->maxs, state->maxs);
	if (state->rotating)
	{
		VectorCopy(door->s.origin, state->origin);
		VectorCopy(door->s.angles, state->angles);
		VectorCopy(door->moveinfo.start_angles, state->start_angles);
		VectorCopy(door->moveinfo.end_angles, state->end_angles);
		return;
	}
	VectorCopy(door->absmin, state->absmin);
	VectorCopy(door->absmax, state->absmax);
	VectorCopy(door->moveinfo.start_origin, state->start_origin);
	VectorCopy(door->moveinfo.end_origin, state->end_origin);
	if (state->use_kind == 2)
	{
		VectorCopy(door->pos1, state->pos1);
		VectorCopy(door->pos2, state->pos2);
	}
}

static qboolean SG_OracleDoorBoundsVectorMatches(const vec3_t first,
	const vec3_t second)
{
	return first[0] == second[0] && first[1] == second[1] &&
	       first[2] == second[2];
}

static qboolean SG_OracleDoorBoundsStateMatches(
	const sg_oracle_door_bounds_state_t *state, const edict_t *door)
{
	sg_oracle_door_bounds_state_t current;

	if (!state || !door || !door->classname)
		return false;
	SG_OracleDoorBoundsStateCapture(&current, door);
	return memcmp(state, &current, sizeof(current)) == 0;
}

static void SG_OracleDoorBoundsCacheClear(void)
{
	int index;

	for (index = 0; index < MAX_EDICTS; index++)
	{
		sg_oracle_door_bounds_cache_t *entry =
			sg_oracle_door_bounds_cache[index];

		while (entry)
		{
			sg_oracle_door_bounds_cache_t *next = entry->next;

			sg_host.game_free(entry);
			entry = next;
		}
		sg_oracle_door_bounds_cache[index] = NULL;
	}
}

void SG_OracleDoorBoundsCacheBegin(void)
{
	SG_OracleDoorBoundsCacheClear();
	sg_oracle_door_bounds_cache_active = true;
}

void SG_OracleDoorBoundsCacheEnd(void)
{
	sg_oracle_door_bounds_cache_active = false;
	SG_OracleDoorBoundsCacheClear();
}

static void SG_OracleDoorBoundsScoped(int index, edict_t *door,
	const vec3_t hull_mins, const vec3_t hull_maxs,
	vec3_t dmins, vec3_t dmaxs)
{
	sg_oracle_door_bounds_cache_t *entry;
	qboolean has_hull = hull_mins && hull_maxs;

	if (!sg_oracle_door_bounds_cache_active || index <= 0 ||
	    index >= MAX_EDICTS || !door || door != &g_edicts[index] ||
	    (!has_hull && (hull_mins || hull_maxs)))
	{
		SG_OracleDoorBounds(door, hull_mins, hull_maxs, dmins, dmaxs);
		return;
	}
	entry = sg_oracle_door_bounds_cache[index];
	if (entry && !SG_OracleDoorBoundsStateMatches(&entry->state, door))
	{
		sg_oracle_door_bounds_cache_active = false;
		SG_OracleDoorBoundsCacheClear();
		SG_OracleDoorBounds(door, hull_mins, hull_maxs, dmins, dmaxs);
		return;
	}
	for (; entry; entry = entry->next)
		if (entry->has_hull == has_hull &&
		    (!has_hull ||
		     (SG_OracleDoorBoundsVectorMatches(entry->hull_mins, hull_mins) &&
		      SG_OracleDoorBoundsVectorMatches(entry->hull_maxs, hull_maxs))))
		{
			VectorCopy(entry->mins, dmins);
			VectorCopy(entry->maxs, dmaxs);
			return;
		}
	SG_OracleDoorBounds(door, hull_mins, hull_maxs, dmins, dmaxs);
	entry = sg_host.game_alloc(sizeof(*entry));
	if (!entry)
		return;
	memset(entry, 0, sizeof(*entry));
	SG_OracleDoorBoundsStateCapture(&entry->state, door);
	entry->has_hull = has_hull;
	if (has_hull)
	{
		VectorCopy(hull_mins, entry->hull_mins);
		VectorCopy(hull_maxs, entry->hull_maxs);
	}
	VectorCopy(dmins, entry->mins);
	VectorCopy(dmaxs, entry->maxs);
	entry->next = sg_oracle_door_bounds_cache[index];
	sg_oracle_door_bounds_cache[index] = entry;
}

static void *SG_OracleDoorBoundsCacheTestAllocationFailure(int size)
{
	(void)size;
	return NULL;
}

int SG_OracleTestDoorBoundsCacheCases(void)
{
	edict_t saved, *door;
	sg_phantom_t phantom;
	sg_oracle_door_bounds_cache_t *rotating_entry;
	vec3_t hull_mins = { -16.0f, -16.0f, -24.0f };
	vec3_t hull_maxs = { 16.0f, 16.0f, 32.0f };
	vec3_t other_maxs = { 8.0f, 8.0f, 16.0f };
	vec3_t expected_mins, expected_maxs, actual_mins, actual_maxs;
	void *(*allocate)(int) = sg_host.game_alloc;
	int failures = 0;
	int index;

	if (!g_edicts || globals.edicts != g_edicts || globals.num_edicts <= 1 ||
	    globals.num_edicts > MAX_EDICTS || !sg_host.game_alloc ||
	    !sg_host.game_free)
		return 1;
	index = globals.num_edicts - 1;
	door = &g_edicts[index];
	saved = *door;
	memset(door, 0, sizeof(*door));
	door->inuse = true;
	door->s.number = index;
	door->classname = "func_door";
	door->use = door_use;
	door->solid = SOLID_BSP;
	door->linkcount = 1;
	VectorSet(door->mins, -32.0f, -24.0f, -8.0f);
	VectorSet(door->maxs, 32.0f, 24.0f, 72.0f);
	VectorCopy(door->mins, door->absmin);
	VectorCopy(door->maxs, door->absmax);
	VectorSet(door->moveinfo.end_origin, 64.0f, 0.0f, 0.0f);
	SG_OracleDoorBounds(door, hull_mins, hull_maxs,
	                    expected_mins, expected_maxs);
	SG_OracleDoorBoundsCacheBegin();
	SG_OracleDoorBoundsScoped(index, door, hull_mins, hull_maxs,
	                          actual_mins, actual_maxs);
	if (!SG_OracleDoorBoundsVectorMatches(expected_mins, actual_mins) ||
	    !SG_OracleDoorBoundsVectorMatches(expected_maxs, actual_maxs) ||
	    !sg_oracle_door_bounds_cache[index])
		failures |= 2;
	SG_OracleDoorBoundsScoped(index, door, hull_mins, other_maxs,
	                          actual_mins, actual_maxs);
	if (!sg_oracle_door_bounds_cache[index] ||
	    !sg_oracle_door_bounds_cache[index]->next)
		failures |= 4;
	door->absmax[0] += 1.0f;
	SG_OracleDoorBounds(door, hull_mins, hull_maxs,
	                    expected_mins, expected_maxs);
	SG_OracleDoorBoundsScoped(index, door, hull_mins, hull_maxs,
	                          actual_mins, actual_maxs);
	if (sg_oracle_door_bounds_cache_active ||
	    !SG_OracleDoorBoundsVectorMatches(expected_mins, actual_mins) ||
	    !SG_OracleDoorBoundsVectorMatches(expected_maxs, actual_maxs))
		failures |= 8;
	SG_OracleDoorBoundsCacheEnd();

	memset(door, 0, sizeof(*door));
	door->inuse = true;
	door->s.number = index;
	door->classname = "func_door_rotating";
	door->use = door_use;
	door->solid = SOLID_BSP;
	VectorSet(door->s.origin, 128.0f, -64.0f, 16.0f);
	VectorSet(door->mins, -48.0f, -8.0f, -8.0f);
	VectorSet(door->maxs, 48.0f, 8.0f, 8.0f);
	VectorSet(door->moveinfo.start_angles, 0.0f, 0.0f, 0.0f);
	VectorSet(door->moveinfo.end_angles, 0.0f, 90.0f, 0.0f);
	SG_OracleDoorBounds(door, hull_mins, hull_maxs,
	                    expected_mins, expected_maxs);
	memset(&phantom, 0, sizeof(phantom));
	VectorCopy(door->s.origin, phantom.origin);
	if (SG_OracleRotatingDoorBoxOverlapIsCoarse(door, &phantom))
		failures |= 128;
	phantom.origin[0] = expected_maxs[0] + 1.0f;
	if (!SG_OracleRotatingDoorBoxOverlapIsCoarse(door, &phantom))
		failures |= 256;
	door->solid = SOLID_NOT;
	if (SG_OracleRotatingDoorBoxOverlapIsCoarse(door, &phantom))
		failures |= 512;
	door->solid = SOLID_BSP;
	SG_OracleDoorBoundsCacheBegin();
	SG_OracleDoorBoundsScoped(index, door, hull_mins, hull_maxs,
	                          actual_mins, actual_maxs);
	rotating_entry = sg_oracle_door_bounds_cache[index];
	SG_OracleDoorBoundsScoped(index, door, hull_mins, hull_maxs,
	                          actual_mins, actual_maxs);
	if (!rotating_entry || sg_oracle_door_bounds_cache[index] != rotating_entry ||
	    rotating_entry->next ||
	    !SG_OracleDoorBoundsVectorMatches(expected_mins, actual_mins) ||
	    !SG_OracleDoorBoundsVectorMatches(expected_maxs, actual_maxs))
		failures |= 32;
	door->moveinfo.end_angles[YAW] = 120.0f;
	SG_OracleDoorBounds(door, hull_mins, hull_maxs,
	                    expected_mins, expected_maxs);
	SG_OracleDoorBoundsScoped(index, door, hull_mins, hull_maxs,
	                          actual_mins, actual_maxs);
	if (sg_oracle_door_bounds_cache_active ||
	    !SG_OracleDoorBoundsVectorMatches(expected_mins, actual_mins) ||
	    !SG_OracleDoorBoundsVectorMatches(expected_maxs, actual_maxs))
		failures |= 64;
	SG_OracleDoorBoundsCacheEnd();

	sg_host.game_alloc = SG_OracleDoorBoundsCacheTestAllocationFailure;
	SG_OracleDoorBoundsCacheBegin();
	SG_OracleDoorBoundsScoped(index, door, hull_mins, hull_maxs,
	                          actual_mins, actual_maxs);
	if (sg_oracle_door_bounds_cache[index] ||
	    !SG_OracleDoorBoundsVectorMatches(expected_mins, actual_mins) ||
	    !SG_OracleDoorBoundsVectorMatches(expected_maxs, actual_maxs))
		failures |= 16;
	sg_host.game_alloc = allocate;
	SG_OracleDoorBoundsCacheEnd();
	*door = saved;
	return failures;
}

/* Match the staging and 1/8-unit clamp in SV_Physics_Pusher/SV_Push without
 * performing an undefined float-to-int conversion on malformed velocity. */
static qboolean SG_OracleProspectivePusherStep(const edict_t *door,
	vec3_t move, vec3_t amove)
{
	int axis;

	if (!door || !SG_OracleFinite3(door->velocity) ||
	    !SG_OracleFinite3(door->avelocity))
		return false;
	for (axis = 0; axis < 3; axis++)
	{
		float scaled;

		move[axis] = door->velocity[axis] * FRAMETIME;
		scaled = move[axis] * 8.0f;
		if (!isfinite(scaled))
			return false;
		if (scaled > 0.0f)
			scaled += 0.5f;
		else
			scaled -= 0.5f;
		if (!isfinite(scaled) || (double)scaled < (double)INT_MIN ||
		    (double)scaled > (double)INT_MAX)
			return false;
		move[axis] = 0.125f * (float)(int)scaled;
		amove[axis] = door->avelocity[axis] * FRAMETIME;
		if (!isfinite(move[axis]) || !isfinite(amove[axis]))
			return false;
	}
	return true;
}

/* A human touched by an earlier team leaf can synchronously run door_go_up on
 * a later DOWN/BOTTOM translating leaf.  Reproduce the velocity which stock
 * Move_Calc/Move_Begin (or its one-frame Move_Final branch) would stage, then
 * apply the same SV_Push clamp. */
static qboolean SG_OracleProspectiveReopenMove(edict_t *door, vec3_t move)
{
	vec3_t direction, velocity;
	float distance, one_frame;
	int axis;

	if (!door || !move || !isfinite(door->moveinfo.speed) ||
	    door->moveinfo.speed <= 0.0f)
		return false;
	VectorSubtract(door->moveinfo.end_origin, door->s.origin, direction);
	if (!SG_OracleFinite3(direction))
		return false;
	distance = VectorNormalize(direction);
	one_frame = door->moveinfo.speed * FRAMETIME;
	if (!isfinite(distance) || distance < 0.0f || !isfinite(one_frame))
		return false;
	if (distance == 0.0f)
		VectorClear(velocity);
	else if (one_frame >= distance)
		VectorScale(direction, distance / FRAMETIME, velocity);
	else
		VectorScale(direction, door->moveinfo.speed, velocity);
	if (!SG_OracleFinite3(velocity))
		return false;
	for (axis = 0; axis < 3; axis++)
	{
		float scaled;

		move[axis] = velocity[axis] * FRAMETIME;
		scaled = move[axis] * 8.0f;
		if (!isfinite(move[axis]) || !isfinite(scaled))
			return false;
		if (scaled > 0.0f)
			scaled += 0.5f;
		else
			scaled -= 0.5f;
		if (!isfinite(scaled) || (double)scaled < (double)INT_MIN ||
		    (double)scaled > (double)INT_MAX)
			return false;
		move[axis] = 0.125f * (float)(int)scaled;
		if (!isfinite(move[axis]))
			return false;
	}
	return true;
}

/* The interlock needs only the movement that can happen in this one pusher
 * dispatch.  A full endpoint sweep would deadlock the legitimate owner while
 * it traverses an open doorway. */
static qboolean SG_OracleProspectiveDoorBounds(edict_t *door,
	const vec3_t hull_mins, const vec3_t hull_maxs,
	vec3_t dmins, vec3_t dmaxs)
{
	vec3_t move, amove;
	int axis;

	if (!SG_OracleProspectivePusherStep(door, move, amove))
		return false;
	if (!strcmp(door->classname, "func_door_rotating"))
	{
		vec3_t alternate_mins, alternate_maxs, local_mins, local_maxs;
		float current, next;
		int angle_axis = -1, changed = 0;

		for (axis = 0; axis < 3; axis++)
		{
			float hmin = hull_mins ? hull_mins[axis] : 0.0f;
			float hmax = hull_maxs ? hull_maxs[axis] : 0.0f;

			local_mins[axis] = door->mins[axis] - hmax;
			local_maxs[axis] = door->maxs[axis] - hmin;
			if (fabsf(door->moveinfo.end_angles[axis] -
			          door->moveinfo.start_angles[axis]) > 0.001f)
			{
				angle_axis = axis;
				changed++;
			}
		}
		if (changed != 1)
			return false;
		for (axis = 0; axis < 3; axis++)
			if (axis != angle_axis && amove[axis] != 0.0f)
				return false;
		current = door->s.angles[angle_axis];
		next = current + amove[angle_axis];
		if (!isfinite(next) ||
		    !SG_OracleRotatingDoorIntervalBounds(door, local_mins,
		        local_maxs, current, next, current, next, current,
		        dmins, dmaxs))
			return false;
		/* SV_Push touches triggers before SV_Physics_Pusher advances the next
		 * team member.  A human pushed by an earlier leaf keeps stock authority
		 * to reopen a closing door, so a later rotating slave can reverse after
		 * this observation but before its own push.  Enclose that one stock
		 * AngleMove_Begin step as well as the currently staged angular step. */
		if (door->moveinfo.state == SG_PLAT_STATE_DOWN ||
		    door->moveinfo.state == SG_PLAT_STATE_BOTTOM)
		{
			float alternate_amove, alternate_next, alternate_velocity;
			float delta = door->moveinfo.end_angles[angle_axis] - current;
			float travel;

			if (!isfinite(delta) || !isfinite(door->moveinfo.speed) ||
			    door->moveinfo.speed <= 0.0f)
				return false;
			travel = fabsf(delta) / door->moveinfo.speed;
			if (!isfinite(travel))
				return false;
			if (delta == 0.0f)
				alternate_amove = 0.0f;
			else
			{
				alternate_velocity = travel < FRAMETIME
				    ? delta / FRAMETIME : delta / travel;
				alternate_amove = alternate_velocity * FRAMETIME;
			}
			alternate_next = current + alternate_amove;
			if (!isfinite(alternate_next) ||
			    !SG_OracleRotatingDoorIntervalBounds(door, local_mins,
			        local_maxs, current, alternate_next, current,
			        alternate_next, current, alternate_mins,
			        alternate_maxs))
				return false;
			for (axis = 0; axis < 3; axis++)
			{
				if (alternate_mins[axis] < dmins[axis])
					dmins[axis] = alternate_mins[axis];
				if (alternate_maxs[axis] > dmaxs[axis])
					dmaxs[axis] = alternate_maxs[axis];
			}
		}
		SG_OracleRotatingBoundsOutward(door, local_mins, local_maxs,
		                                     dmins, dmaxs);
		/* A malformed-but-finite translated rotator is still enclosed: every
		 * angular pose above may additionally occupy any point on the linear
		 * pusher chord. */
		for (axis = 0; axis < 3; axis++)
		{
			if (move[axis] < 0.0f)
				dmins[axis] = nextafterf(dmins[axis] + move[axis],
				                            -INFINITY);
			else if (move[axis] > 0.0f)
				dmaxs[axis] = nextafterf(dmaxs[axis] + move[axis],
				                            INFINITY);
		}
		return SG_OracleFinite3(dmins) && SG_OracleFinite3(dmaxs);
	}
	for (axis = 0; axis < 3; axis++)
		if (amove[axis] != 0.0f)
			return false;

	for (axis = 0; axis < 3; axis++)
	{
		double current_min = (double)door->s.origin[axis] +
		                     (double)door->mins[axis];
		double current_max = (double)door->s.origin[axis] +
		                     (double)door->maxs[axis];
		double moved_min = current_min + (double)move[axis];
		double moved_max = current_max + (double)move[axis];
		double hmin = hull_mins ? (double)hull_mins[axis] : 0.0;
		double hmax = hull_maxs ? (double)hull_maxs[axis] : 0.0;
		double low = (current_min < moved_min ? current_min : moved_min) -
		             hmax;
		double high = (current_max > moved_max ? current_max : moved_max) -
		              hmin;

		if (!isfinite(low) || !isfinite(high) ||
		    low <= -(double)FLT_MAX || high >= (double)FLT_MAX)
			return false;
		dmins[axis] = nextafterf((float)low, -INFINITY);
		dmaxs[axis] = nextafterf((float)high, INFINITY);
	}
	/* The same inter-member trigger ordering can run door_go_up on a later
	 * translating leaf.  Union the exact stock reopen step from the observed
	 * pose with the step staged when the fence began. */
	if (door->moveinfo.state == SG_PLAT_STATE_DOWN ||
	    door->moveinfo.state == SG_PLAT_STATE_BOTTOM)
	{
		vec3_t reopen_move;

		if (!SG_OracleProspectiveReopenMove(door, reopen_move))
			return false;
		for (axis = 0; axis < 3; axis++)
		{
			double current_min = (double)door->s.origin[axis] +
			                     (double)door->mins[axis];
			double current_max = (double)door->s.origin[axis] +
			                     (double)door->maxs[axis];
			double moved_min = current_min + (double)reopen_move[axis];
			double moved_max = current_max + (double)reopen_move[axis];
			double hmin = hull_mins ? (double)hull_mins[axis] : 0.0;
			double hmax = hull_maxs ? (double)hull_maxs[axis] : 0.0;
			double low = (current_min < moved_min
			              ? current_min : moved_min) - hmax;
			double high = (current_max > moved_max
			               ? current_max : moved_max) - hmin;

			if (!isfinite(low) || !isfinite(high) ||
			    low <= -(double)FLT_MAX || high >= (double)FLT_MAX)
				return false;
			if (low < (double)dmins[axis])
				dmins[axis] = nextafterf((float)low, -INFINITY);
			if (high > (double)dmaxs[axis])
				dmaxs[axis] = nextafterf((float)high, INFINITY);
		}
	}
	return true;
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

/* SV_LinkEdict publishes a rotating BSP through a coarse radius cube.  A
 * partial-arc door can therefore appear in AREA_SOLID even when the player is
 * outside every authenticated pose of the brush.  The complete analytic door
 * sweep remains collision authority for that broad-phase hit. */
static qboolean SG_OracleRotatingDoorBoxOverlapIsCoarse(edict_t *door,
	const sg_phantom_t *ph)
{
	vec3_t hull_mins = { -16.0f, -16.0f, -24.0f };
	vec3_t hull_maxs = { 16.0f, 16.0f, 32.0f };
	vec3_t mins, maxs;
	int index, axis;

	if (!door || !ph || door->solid != SOLID_BSP || !door->classname ||
	    strcmp(door->classname, "func_door_rotating") != 0)
		return false;
	index = SG_OracleLiveEdictIndex(door);
	if (index <= 0)
		return false;
	SG_OracleDoorBoundsScoped(index, door, hull_mins, hull_maxs, mins, maxs);
	if (!SG_OracleFinite3(mins) || !SG_OracleFinite3(maxs))
		return false;
	for (axis = 0; axis < 3; axis++)
		if (mins[axis] > maxs[axis])
			return false;
	return !SG_OracleSegmentBox(ph->origin, ph->origin, mins, maxs);
}

typedef struct sg_oracle_population_snapshot_s
{
	edict_t *entity;
	int index;
	solid_t solid;
	qboolean inuse;
	int number;
	gclient_t *client;
	edict_t *owner;
	link_t *area_prev;
	link_t *area_next;
} sg_oracle_population_snapshot_t;

/* The engine trace is the authority for AREA_SOLID order, independent BBOX
 * epsilon, ties, startsolid propagation, planes, surfaces, and ownership
 * exclusions.  Loader replay removes only joined clients and their live
 * owned BBOXes for the duration of one synchronous native trace.  Merely
 * changing solid leaves the area links and their order untouched; the host
 * skips SOLID_NOT exactly where it normally filters an AREA_SOLID result. */
static qboolean sg_oracle_population_trace_active;

static qboolean SG_OraclePopulationGlobalsValid(void)
{
	return g_edicts && globals.edicts == g_edicts &&
	       globals.edict_size == (int)sizeof(edict_t) &&
	       globals.num_edicts > 1 && globals.num_edicts <= MAX_EDICTS &&
	       game.maxentities > BODY_QUEUE_SIZE &&
	       game.maxentities <= MAX_EDICTS &&
	       globals.max_edicts == game.maxentities &&
	       globals.num_edicts <= game.maxentities && game.clients &&
	       game.maxclients > 0 &&
	       game.maxclients < game.maxentities - BODY_QUEUE_SIZE;
}

/* Return 1 for a real client slot, 0 for no client, and -1 for a malformed
 * non-NULL client identity.  This never performs relational pointer tests. */
static int SG_OraclePopulationClientIdentity(const edict_t *entity,
	int index)
{
	if (!entity || !entity->client)
		return 0;
	if (index <= 0 || index > game.maxclients || !game.clients)
		return -1;
	return entity->client == &game.clients[index - 1] ? 1 : -1;
}

/* Classify client provenance without dereferencing a stale owner pointer.
 * Client-owned bodies remain population noise even if their owner slot has
 * just gone inactive; the exact slot/client identity must still be intact. */
static int SG_OraclePopulationTransientIdentity(const edict_t *entity,
	int index)
{
	int client_identity;
	int owner_index;

	if (!entity || index <= 0 || index >= globals.num_edicts ||
	    entity != &g_edicts[index] || entity->s.number != index)
		return -1;
	client_identity = SG_OraclePopulationClientIdentity(entity, index);
	if (client_identity != 0)
		return client_identity;
	if (!entity->owner)
		return 0;
	owner_index = entity->owner == g_edicts
	    ? 0 : SG_OracleLiveEdictIndex(entity->owner);
	if (owner_index < 0)
		return -1;
	client_identity = SG_OraclePopulationClientIdentity(entity->owner,
	    owner_index);
	return client_identity < 0 ? -1 : client_identity;
}

static int SG_OraclePopulationTransientBBox(const edict_t *entity,
	int index)
{
	if (!entity || !entity->inuse || entity->solid != SOLID_BBOX ||
	    !entity->area.prev || !entity->area.next)
		return 0;
	return SG_OraclePopulationTransientIdentity(entity, index);
}

static qboolean SG_OraclePopulationTraceResultValid(const trace_t *trace,
	edict_t *passent, qboolean population_independent)
{
	int axis;
	int index;
	int transient;

	if (!trace || !isfinite(trace->fraction) || trace->fraction < 0.0f ||
	    trace->fraction > 1.0f)
		return false;
	for (axis = 0; axis < 3; axis++)
		if (!isfinite(trace->endpos[axis]))
			return false;
	if (!trace->ent)
		return trace->fraction == 1.0f && !trace->startsolid &&
		       !trace->allsolid;
	if (trace->ent == g_edicts)
		return true;
	index = SG_OracleLiveEdictIndex(trace->ent);
	if (index < 0 || !trace->ent->inuse || trace->ent == passent)
		return false;
	if (!population_independent)
		return true;
	transient = SG_OraclePopulationTransientIdentity(trace->ent, index);
	if (transient != 0)
		return false;
	if (trace->ent->solid != SOLID_BBOX)
		return true;
	return SG_ImmutableSupport(trace->ent) ? true : false;
}

static qboolean SG_OracleStablePopulationTraceMask(const vec3_t start,
	const vec3_t mins, const vec3_t maxs, const vec3_t end,
	edict_t *passent, int mask, qboolean population_independent,
	trace_t *trace_out)
{
	sg_oracle_population_snapshot_t snapshots[MAX_EDICTS];
	edict_t *saved_base;
	edict_t *saved_globals_edicts;
	gclient_t *saved_clients;
	int saved_edict_size;
	int saved_num_edicts;
	int saved_max_edicts;
	int saved_maxentities;
	int saved_maxclients;
	qboolean integrity = true;
	int snapshot_count = 0;
	int axis;
	int index;

	if (trace_out)
		memset(trace_out, 0, sizeof(*trace_out));
	if (!start || !end || !trace_out || !sg_host.trace ||
	    ((!mins) != (!maxs)))
		return false;
	for (axis = 0; axis < 3; axis++)
		if (!isfinite(start[axis]) || !isfinite(end[axis]) ||
		    (mins && (!isfinite(mins[axis]) || !isfinite(maxs[axis]) ||
		              mins[axis] > maxs[axis])))
			return false;
	if (sg_oracle_population_trace_active)
		return false;
	if (!population_independent)
	{
		*trace_out = sg_host.trace(start, mins, maxs, end, passent, mask);
		return SG_OraclePopulationTraceResultValid(trace_out, passent,
		    false);
	}
	if (!SG_OraclePopulationGlobalsValid())
		return false;
	saved_base = g_edicts;
	saved_globals_edicts = globals.edicts;
	saved_edict_size = globals.edict_size;
	saved_num_edicts = globals.num_edicts;
	saved_max_edicts = globals.max_edicts;
	saved_maxentities = game.maxentities;
	saved_maxclients = game.maxclients;
	saved_clients = game.clients;
	/* Finish all validation and snapshot collection before publishing even a
	 * temporary SOLID_NOT value, so a malformed owner/client identity leaves
	 * the world byte-for-byte untouched. */
	for (index = 1; index < globals.num_edicts; index++)
	{
		edict_t *entity = &g_edicts[index];
		int transient;

		if (!entity->inuse || entity->solid != SOLID_BBOX ||
		    !entity->area.prev || !entity->area.next)
			continue;
		if (entity->s.number != index)
			return false;
		transient = SG_OraclePopulationTransientBBox(entity, index);
		if (transient < 0)
			return false;
		if (!transient)
			continue;
		snapshots[snapshot_count].entity = entity;
		snapshots[snapshot_count].index = index;
		snapshots[snapshot_count].solid = entity->solid;
		snapshots[snapshot_count].inuse = entity->inuse;
		snapshots[snapshot_count].number = entity->s.number;
		snapshots[snapshot_count].client = entity->client;
		snapshots[snapshot_count].owner = entity->owner;
		snapshots[snapshot_count].area_prev = entity->area.prev;
		snapshots[snapshot_count].area_next = entity->area.next;
		snapshot_count++;
	}
	sg_oracle_population_trace_active = true;
	for (index = 0; index < snapshot_count; index++)
		snapshots[index].entity->solid = SOLID_NOT;
	*trace_out = sg_host.trace(start, mins, maxs, end, passent, mask);
	/* Restore first, then inspect or return the native result.  The engine
	 * trace has no game callbacks, but the identity checks make this boundary
	 * fail closed under a malformed host or an adversarial test double. */
	for (index = 0; index < snapshot_count; index++)
	{
		edict_t *entity = snapshots[index].entity;

		if (entity->s.number != snapshots[index].number ||
		    entity->inuse != snapshots[index].inuse ||
		    entity->client != snapshots[index].client ||
		    entity->owner != snapshots[index].owner ||
		    entity->area.prev != snapshots[index].area_prev ||
		    entity->area.next != snapshots[index].area_next ||
		    entity->solid != SOLID_NOT)
			integrity = false;
		entity->solid = snapshots[index].solid;
	}
	sg_oracle_population_trace_active = false;
	if (g_edicts != saved_base || globals.edicts != saved_globals_edicts ||
	    globals.edict_size != saved_edict_size ||
	    globals.num_edicts != saved_num_edicts ||
	    globals.max_edicts != saved_max_edicts ||
	    game.maxentities != saved_maxentities ||
	    game.maxclients != saved_maxclients || game.clients != saved_clients)
		integrity = false;
	return integrity &&
	       SG_OraclePopulationTraceResultValid(trace_out, passent, true);
}

qboolean SG_OracleStablePopulationTrace(const vec3_t start,
	const vec3_t mins, const vec3_t maxs, const vec3_t end,
	edict_t *passent, qboolean population_independent, trace_t *trace_out)
{
	if (!mins || !maxs)
		return false;
	return SG_OracleStablePopulationTraceMask(start, mins, maxs, end,
	                                         passent, MASK_PLAYERSOLID,
	                                         population_independent,
	                                         trace_out);
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

static qboolean SG_OracleDoorMember(edict_t *source, edict_t *ent)
{
	edict_t *members[SG_PHANTOM_ARMED_DOORS];
	edict_t *master, *member;
	int count, index;

	if (!source || !ent)
		return false;
	/* Resolve declared controllers through the same exact enumeration used to
	 * publish their proof pose.  This includes one authenticated synchronous
	 * relay hop as well as direct and generated door triggers. */
	count = SG_DeclaredDoorMembersResolved(source, members,
	    SG_PHANTOM_ARMED_DOORS);
	if (count >= 0)
	{
		for (index = 0; index < count; index++)
			if (members[index] == ent)
				return true;
		return false;
	}
	master = source;
	master = master->teammaster ? master->teammaster : master;
	for (member = master; member; member = member->teamchain)
		if (member == ent)
			return true;
	return false;
}

typedef struct sg_oracle_bound_member_scope_s
{
	const sg_rune_mechanism_binding_t *binding;
	edict_t *members[SG_RUNE_BINDING_MAX_MOVERS];
	int member_indices[SG_RUNE_BINDING_MAX_MOVERS];
	sg_oracle_door_bounds_state_t member_states[SG_RUNE_BINDING_MAX_MOVERS];
	size_t count;
	edict_t *saved_edicts;
	int saved_num_edicts;
	qboolean active;
} sg_oracle_bound_member_scope_t;

static sg_oracle_bound_member_scope_t sg_oracle_bound_member_scope;

typedef struct sg_oracle_door_egress_replay_key_s
{
	vec3_t source;
	vec3_t target;
	edict_t *trigger;
	edict_t *passent;
	edict_t *members[SG_RUNE_BINDING_MAX_MOVERS];
	size_t member_count;
	int controller_kind;
	int support_mode;
} sg_oracle_door_egress_replay_key_t;

typedef struct sg_oracle_door_egress_replay_entry_s
{
	sg_oracle_door_egress_replay_key_t key;
	int arrival_ms;
} sg_oracle_door_egress_replay_entry_t;

#define SG_ORACLE_DOOR_EGRESS_REPLAY_CACHE_MAX 1024U

typedef struct sg_oracle_door_egress_replay_cache_s
{
	sg_oracle_door_egress_replay_entry_t
		entries[SG_ORACLE_DOOR_EGRESS_REPLAY_CACHE_MAX];
	size_t count;
	qboolean active;
} sg_oracle_door_egress_replay_cache_t;

static sg_oracle_door_egress_replay_cache_t
	sg_oracle_door_egress_replay_cache;

qboolean SG_OracleDoorEgressReplayCacheBegin(void)
{
	if (sg_oracle_door_egress_replay_cache.active)
		return false;
	memset(&sg_oracle_door_egress_replay_cache, 0,
	    sizeof(sg_oracle_door_egress_replay_cache));
	sg_oracle_door_egress_replay_cache.active = true;
	return true;
}

void SG_OracleDoorEgressReplayCacheEnd(void)
{
	memset(&sg_oracle_door_egress_replay_cache, 0,
	    sizeof(sg_oracle_door_egress_replay_cache));
}

static qboolean SG_OracleDoorEgressReplayKeyCapture(
	sg_oracle_door_egress_replay_key_t *key, const vec3_t source,
	const vec3_t target, const sg_rune_mechanism_binding_t *binding,
	edict_t *passent, sg_button_support_mode_t support_mode)
{
	uint32_t keys[SG_RUNE_BINDING_MAX_MOVERS];
	size_t count;
	size_t index;

	if (!key || !source || !target || !binding || !binding->plan ||
	    !binding->entry_entity ||
	    !SG_OracleBoundDoorBindingCurrent(binding) ||
	    !SG_OracleBoundDoorMoverKeys(binding, keys, &count) || count == 0U ||
	    count > SG_RUNE_BINDING_MAX_MOVERS)
		return false;
	memset(key, 0, sizeof(*key));
	VectorCopy(source, key->source);
	VectorCopy(target, key->target);
	key->trigger = binding->entry_entity;
	key->passent = passent;
	key->member_count = count;
	key->controller_kind = binding->plan->controller_kind;
	key->support_mode = support_mode;
	for (index = 0U; index < count; index++)
	{
		key->members[index] =
		    SG_OracleBoundDoorResolveNode(binding, keys[index]);
		if (!key->members[index])
			return false;
	}
	return true;
}

static qboolean SG_OracleDoorEgressReplayCacheLookup(
	const sg_oracle_door_egress_replay_key_t *key, int *arrival_ms)
{
	size_t index;

	if (!key || !arrival_ms || !sg_oracle_door_egress_replay_cache.active)
		return false;
	for (index = 0U; index < sg_oracle_door_egress_replay_cache.count;
	     index++)
		if (memcmp(key,
		        &sg_oracle_door_egress_replay_cache.entries[index].key,
		        sizeof(*key)) == 0)
		{
			*arrival_ms =
			    sg_oracle_door_egress_replay_cache.entries[index].arrival_ms;
			return true;
		}
	return false;
}

static void SG_OracleDoorEgressReplayCacheStore(
	const sg_oracle_door_egress_replay_key_t *key, int arrival_ms)
{
	sg_oracle_door_egress_replay_entry_t *entry;

	if (!key || arrival_ms <= 0 ||
	    !sg_oracle_door_egress_replay_cache.active ||
	    sg_oracle_door_egress_replay_cache.count >=
	        SG_ORACLE_DOOR_EGRESS_REPLAY_CACHE_MAX)
		return;
	entry = &sg_oracle_door_egress_replay_cache.entries[
	    sg_oracle_door_egress_replay_cache.count++];
	entry->key = *key;
	entry->arrival_ms = arrival_ms;
}

int SG_OracleTestDoorEgressReplayCacheCases(void)
{
	sg_oracle_door_egress_replay_cache_t saved =
	    sg_oracle_door_egress_replay_cache;
	sg_oracle_door_egress_replay_key_t key, changed;
	edict_t entities[4];
	int arrival = 0;
	int failures = 0;

	memset(entities, 0, sizeof(entities));
	memset(&key, 0, sizeof(key));
	VectorSet(key.source, 1.0f, 2.0f, 3.0f);
	VectorSet(key.target, 4.0f, 5.0f, 6.0f);
	key.trigger = &entities[0];
	key.passent = &entities[1];
	key.members[0] = &entities[2];
	key.members[1] = &entities[3];
	key.member_count = 2U;
	key.controller_kind = SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR;
	key.support_mode = SG_BUTTON_SUPPORT_NONE;
	memset(&sg_oracle_door_egress_replay_cache, 0,
	    sizeof(sg_oracle_door_egress_replay_cache));
	if (!SG_OracleDoorEgressReplayCacheBegin() ||
	    SG_OracleDoorEgressReplayCacheBegin())
		failures |= 1;
	SG_OracleDoorEgressReplayCacheStore(&key, 725);
	if (!SG_OracleDoorEgressReplayCacheLookup(&key, &arrival) ||
	    arrival != 725)
		failures |= 2;
	changed = key;
	changed.source[0] += 0.125f;
	if (SG_OracleDoorEgressReplayCacheLookup(&changed, &arrival))
		failures |= 4;
	changed = key;
	changed.target[2] += 0.125f;
	if (SG_OracleDoorEgressReplayCacheLookup(&changed, &arrival))
		failures |= 8;
	changed = key;
	changed.trigger = &entities[1];
	if (SG_OracleDoorEgressReplayCacheLookup(&changed, &arrival))
		failures |= 16;
	changed = key;
	changed.passent = NULL;
	if (SG_OracleDoorEgressReplayCacheLookup(&changed, &arrival))
		failures |= 32;
	changed = key;
	changed.members[0] = key.members[1];
	changed.members[1] = key.members[0];
	if (SG_OracleDoorEgressReplayCacheLookup(&changed, &arrival))
		failures |= 64;
	changed = key;
	changed.member_count = 1U;
	if (SG_OracleDoorEgressReplayCacheLookup(&changed, &arrival))
		failures |= 128;
	changed = key;
	changed.controller_kind = SG_MECHANISM_CONTROLLER_BUTTON_DOOR;
	if (SG_OracleDoorEgressReplayCacheLookup(&changed, &arrival))
		failures |= 256;
	changed = key;
	changed.support_mode = SG_BUTTON_SUPPORT_STATIC;
	if (SG_OracleDoorEgressReplayCacheLookup(&changed, &arrival))
		failures |= 512;
	sg_oracle_door_egress_replay_cache.count =
	    SG_ORACLE_DOOR_EGRESS_REPLAY_CACHE_MAX;
	SG_OracleDoorEgressReplayCacheStore(&changed, 800);
	if (sg_oracle_door_egress_replay_cache.count !=
	    SG_ORACLE_DOOR_EGRESS_REPLAY_CACHE_MAX)
		failures |= 1024;
	SG_OracleDoorEgressReplayCacheEnd();
	if (SG_OracleDoorEgressReplayCacheLookup(&key, &arrival) ||
	    sg_oracle_door_egress_replay_cache.active ||
	    sg_oracle_door_egress_replay_cache.count != 0U)
		failures |= 2048;
	sg_oracle_door_egress_replay_cache = saved;
	return failures;
}

static qboolean SG_OracleBoundMemberScopeBegin(
	const sg_rune_mechanism_binding_t *binding)
{
	uint32_t keys[SG_RUNE_BINDING_MAX_MOVERS];
	size_t count;
	size_t index;
	size_t prior;

	if (sg_oracle_bound_member_scope.active || !binding ||
	    !sg_oracle_loader_replay ||
	    !SG_OracleBoundDoorBindingCurrent(binding) ||
	    !SG_OracleBoundDoorMoverKeys(binding, keys, &count) || count == 0U ||
	    count > SG_RUNE_BINDING_MAX_MOVERS)
		return false;
	memset(&sg_oracle_bound_member_scope, 0,
	    sizeof(sg_oracle_bound_member_scope));
	for (index = 0U; index < count; index++)
	{
		edict_t *member = SG_OracleBoundDoorResolveNode(binding, keys[index]);
		int member_index;

		member_index = SG_OracleLiveEdictIndex(member);
		if (!member || member_index <= 0 ||
		    !SG_OracleMoverSweepIdentity(member))
			goto fail;
		for (prior = 0U; prior < index; prior++)
			if (sg_oracle_bound_member_scope.members[prior] == member)
				goto fail;
		sg_oracle_bound_member_scope.members[index] = member;
		sg_oracle_bound_member_scope.member_indices[index] = member_index;
		SG_OracleDoorBoundsStateCapture(
		    &sg_oracle_bound_member_scope.member_states[index], member);
	}
	if (!g_edicts || globals.edicts != g_edicts ||
	    globals.num_edicts <= 0 || globals.num_edicts > MAX_EDICTS)
		goto fail;
	sg_oracle_bound_member_scope.saved_edicts = g_edicts;
	sg_oracle_bound_member_scope.saved_num_edicts = globals.num_edicts;
	sg_oracle_bound_member_scope.binding = binding;
	sg_oracle_bound_member_scope.count = count;
	sg_oracle_bound_member_scope.active = true;
	return true;

fail:
	memset(&sg_oracle_bound_member_scope, 0,
	    sizeof(sg_oracle_bound_member_scope));
	return false;
}

static qboolean SG_OracleBoundMemberScopeEnd(
	const sg_rune_mechanism_binding_t *binding)
{
	uint32_t keys[SG_RUNE_BINDING_MAX_MOVERS];
	size_t count = 0U;
	qboolean valid = sg_oracle_bound_member_scope.active &&
	    sg_oracle_bound_member_scope.binding == binding &&
	    SG_OracleBoundDoorBindingCurrent(binding) &&
	    SG_OracleBoundDoorMoverKeys(binding, keys, &count) &&
	    count == sg_oracle_bound_member_scope.count;
	size_t index;

	if (valid && (g_edicts != sg_oracle_bound_member_scope.saved_edicts ||
	              globals.edicts != g_edicts ||
	              globals.num_edicts !=
	                  sg_oracle_bound_member_scope.saved_num_edicts))
		valid = false;
	if (valid)
		for (index = 0U; index < count; index++)
		{
			edict_t *member = SG_OracleBoundDoorResolveNode(binding, keys[index]);
			sg_oracle_door_bounds_state_t state;

			if (member != sg_oracle_bound_member_scope.members[index] ||
			    SG_OracleLiveEdictIndex(member) !=
			        sg_oracle_bound_member_scope.member_indices[index] ||
			    !SG_OracleMoverSweepIdentity(member))
			{
				valid = false;
				break;
			}
			SG_OracleDoorBoundsStateCapture(&state, member);
			if (memcmp(&state,
			        &sg_oracle_bound_member_scope.member_states[index],
			        sizeof(state)) != 0)
			{
				valid = false;
				break;
			}
		}

	memset(&sg_oracle_bound_member_scope, 0,
	    sizeof(sg_oracle_bound_member_scope));
	return valid;
}

static qboolean SG_OracleDeclaredSetMember(edict_t *trigger, edict_t *ent)
{
	uint32_t keys[SG_RUNE_BINDING_MAX_MOVERS];
	size_t count;
	size_t index;

	if (sg_oracle_bound_door)
	{
		if (sg_oracle_bound_member_scope.active)
		{
			if (sg_oracle_bound_member_scope.binding !=
			    sg_oracle_bound_door || !ent)
				return false;
			for (index = 0U;
			     index < sg_oracle_bound_member_scope.count; index++)
				if (sg_oracle_bound_member_scope.members[index] == ent)
					return true;
			return false;
		}
		if (!ent || !SG_OracleBoundDoorMoverKeys(
		        sg_oracle_bound_door, keys, &count))
			return false;
		for (index = 0U; index < count; index++)
			if (SG_OracleBoundDoorResolveNode(sg_oracle_bound_door,
			        keys[index]) == ent)
				return SG_OracleBoundDoorBindingCurrent(
				    sg_oracle_bound_door);
		return false;
	}
	return SG_OracleDoorMember(trigger, ent);
}

/* Two player triggers are interchangeable during an already-open egress only
 * when their complete physical closure is identical.  LMCTF commonly places
 * a broad activator on each side of one rotating team; crossing the far-side
 * trigger is a harmless refresh, but sharing one member or one target string
 * is not enough to prove there are no extra movers. */
static int SG_OracleDeclaredDoorSet(edict_t *trigger, edict_t **set, int cap)
{
	edict_t *members[SG_PHANTOM_ARMED_DOORS];
	int member_count;
	int count = 0;
	int member_index;

	if (!SG_OracleDeclaredDoorSourceSafe(trigger) || !set || cap <= 0)
		return -1;
	member_count = SG_DeclaredDoorMembers(trigger, members,
	    SG_PHANTOM_ARMED_DOORS);
	if (member_count <= 0)
		return -1;
	for (member_index = 0; member_index < member_count; member_index++)
	{
		edict_t *master = members[member_index]->teammaster
		    ? members[member_index]->teammaster : members[member_index];
		int i;

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
	const vec3_t end, qboolean record_passage)
{
	int i, axis;

	for (i = 1; i < globals.num_edicts; i++)
	{
		edict_t *door = &g_edicts[i];
		vec3_t mins, maxs, open_mins, open_maxs;

		if (!door->inuse || !door->classname ||
		    strncmp(door->classname, "func_door", 9) != 0)
			continue;
		/* PREOPEN stages one contract-resolved physical leaf at each exact
		 * mover pose.  Pmove must see that live BSP, not this broad union. */
		if (door == sg_oracle_compound_member)
			continue;
		if (sg_oracle_declared_action == RL_LIFT &&
		    door == sg_oracle_declared_expected)
			continue;
		/* A declared door egress links this exact team at its real open pose.
		 * Pmove collides with those brushes normally; the broad synthetic sweep
		 * must not simultaneously pretend the same team is still closed. */
		if (sg_oracle_declared_door &&
		    SG_OracleDeclaredSetMember(sg_oracle_declared_door, door))
			continue;
		SG_OracleDoorBoundsScoped(i, door, hull_mins, hull_maxs, mins, maxs);
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
		if (ph && record_passage)
			ph->door_passed = true;
	}
	return false;
}

static qboolean SG_OracleDoorOverlap(sg_phantom_t *ph)
{
	vec3_t mins = { -16.0f, -16.0f, -24.0f };
	vec3_t maxs = { 16.0f, 16.0f, 32.0f };

	return ph && SG_OracleDoorTraceBlocked(ph, ph->origin, mins, maxs,
	                                      ph->origin, true);
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

	if (!source || depth > 4 || source->killtarget ||
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

/* Declared plans may preserve a positive-delay relay as an authenticated
 * terminal: the runtime consumes that bound source before DelayedUse is
 * allocated.  Negative-delay relays are malformed scheduling state and stay
 * outside this controller law.  Every omitted branch must still be provably
 * sound-only, including nested relays. */
static qboolean SG_OraclePlanSoundOnlyTargets(edict_t *source, int depth)
{
	edict_t *target = NULL;
	qboolean found = false;

	if (!source || !isfinite(source->delay) || source->delay < 0.0f ||
	    depth > 4 || source->killtarget ||
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
		    SG_OraclePlanSoundOnlyTargets(target, depth + 1))
			continue;
		return false;
	}
	return found;
}

static qboolean SG_OracleDoorEffectsSafe(edict_t *door)
{
	edict_t *target = NULL;

	if (!door || door->killtarget || door->message ||
	    !isfinite(door->delay) || door->delay < 0.0f)
		return false;
	if (!door->target)
		return true;
	if (door->delay != 0.0f)
		return false;
	if (!door->target[0])
		return false;
	while ((target = G_Find(target, FOFS(targetname), door->target)) != NULL)
	{
		if (!target->inuse || !target->classname)
			return false;
		/* G_UseTargets deliberately skips a door's func_areaportal; the door
		 * updates it through door_use_areaportals instead. */
		if (!Q_stricmp(target->classname, "func_areaportal"))
			continue;
		if (!Q_stricmp(target->classname, "target_speaker") &&
		    target->use == Use_Target_Speaker)
			continue;
		if (!Q_stricmp(target->classname, "trigger_relay") &&
		    target->use == trigger_relay_use &&
		    SG_OraclePlanSoundOnlyTargets(target, 1))
			continue;
		return false;
	}
	/* A nonempty mapper target with no live recipient is an exact
	 * G_UseTargets no-op. */
	return true;
}

/* Static declared mechanisms must remain the same edict incarnations for the
 * life of their lease.  The stock game frees mapper entities only through
 * G_UseTargets killtarget (repeatable declared triggers do not self-free), so
 * reject every trigger or door that any live map entity can kill.  This makes
 * integer mover/activator keys ABA-safe without pretending linkcount is an
 * incarnation token. */
static qboolean SG_OracleEntityKilltargetable(edict_t *entity)
{
	int index;

	if (!entity || !entity->targetname)
		return false;
	if (!g_edicts || globals.edicts != g_edicts ||
	    globals.edict_size != (int)sizeof(edict_t) ||
	    globals.num_edicts <= 1 || globals.num_edicts > MAX_EDICTS ||
	    game.maxentities <= 1 || game.maxentities > MAX_EDICTS ||
	    globals.max_edicts != game.maxentities ||
	    globals.num_edicts > game.maxentities)
		return true;
	for (index = 1; index < globals.num_edicts; index++)
	{
		edict_t *source = &g_edicts[index];

		if (source->inuse && source->killtarget &&
		    !Q_stricmp(source->killtarget, entity->targetname))
			return true;
	}
	return false;
}

/* G_FindTeams publishes one exact captain-first linked list.  A declared
 * controller must move the same set that stock door_use will traverse from
 * the callback target: accepting only a plausible teammaster pointer is not
 * enough, because a missing slave bit, foreign backlink, omitted member, or
 * cycle changes door_use from a team-wide command into a suffix/no-op. */
static qboolean SG_OracleDeclaredDoorTeamCanonical(edict_t *door,
	edict_t **master_out)
{
	edict_t *master, *previous = NULL;
	qboolean found = false;
	int index;

	if (master_out)
		*master_out = NULL;
	if (SG_OracleLiveEdictIndex(door) < 0)
		return false;
	master = door->teammaster ? door->teammaster : door;
	if (SG_OracleLiveEdictIndex(master) < 0 ||
	    (master->flags & FL_TEAMSLAVE) ||
	    master->teammaster != master)
		return false;
	/* SP_func_door makes an unteamed door a one-member team before
	 * G_FindTeams.  Nothing else can legitimately appear in its chain. */
	if (!master->team)
	{
		if (door != master || master->teamchain)
			return false;
		if (master_out)
			*master_out = master;
		return true;
	}
	/* Reconstruct G_FindTeams' exact ascending-edict chain.  This proves the
	 * selected captain is the first same-team entity and that no live peer was
	 * omitted, reordered, or attached to a different captain. */
	for (index = 1; index < globals.num_edicts; index++)
	{
		edict_t *member = &g_edicts[index];

		if (!member->inuse || !member->team ||
		    strcmp(member->team, master->team) != 0)
			continue;
		if (SG_OracleLiveEdictIndex(member) != index)
			return false;
		if (!previous)
		{
			if (member != master || (member->flags & FL_TEAMSLAVE) ||
			    member->teammaster != master)
				return false;
		}
		else if (previous->teamchain != member ||
		         !(member->flags & FL_TEAMSLAVE) ||
		         member->teammaster != master)
			return false;
		if (member == door)
			found = true;
		previous = member;
	}
	if (!found || !previous || previous->teamchain)
		return false;
	if (master_out)
		*master_out = master;
	return true;
}

/* The declared controller can reproduce a canonical rotating or translating
 * door, including CRUSHER/REVERSE geometry, because its body waits entirely
 * outside the complete sweep until STATE_TOP. Scripted/shot/toggle/start-open
 * mechanisms and physical side effects remain unrepresentable. */
static qboolean SG_OracleDeclaredDoorTeamSafe(edict_t *door)
{
	edict_t *master, *member;

	if (!door || !door->inuse || !door->classname ||
	    !SG_OracleDeclaredDoorTeamCanonical(door, &master) ||
	    SG_OracleEntityKilltargetable(door) ||
	    strncmp(door->classname, "func_door", 9) != 0)
		return false;
	if (!master->inuse || (master->flags & FL_TEAMSLAVE) ||
	    master->use != door_use)
		return false;
	for (member = master; member; member = member->teamchain)
	{
		float travel;

		if (!member->inuse || !member->classname ||
		    SG_OracleEntityKilltargetable(member) ||
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
		    !isfinite(member->moveinfo.wait) ||
		    !SG_OracleDoorEffectsSafe(member))
			return false;
	}
	return true;
}

/* G_UseTargets enumerates matching targetnames in edict order.  G_FindTeams
 * chooses the first team member as captain, so a mapper fanout may legally
 * reach that captain and then one or more same-name slaves.  door_use opens
 * the complete team at the captain and later slave calls are stock no-ops.
 * A slave without its captain earlier in this exact fanout remains unsafe. */
static qboolean SG_OracleDeclaredMasterReachedBefore(edict_t *trigger,
	edict_t *slave, edict_t *master)
{
	edict_t *target = NULL;

	if (!trigger || !trigger->target || !slave || !master || slave == master)
		return false;
	while ((target = G_Find(target, (int)offsetof(edict_t, targetname),
	                        trigger->target)) != NULL)
	{
		if (target == master)
			return true;
		if (target == slave)
			return false;
	}
	return false;
}

/* A stock relay dispatch is synchronous when delay is zero.  Admit one such
 * indirection only when its entire fanout is the same declared door closure
 * the entry trigger could have named directly.  The existing mechanism plan
 * records both target edges, so runtime replays this callback rather than
 * treating the relay as invisible. */
static qboolean SG_OracleDeclaredRelayDoorTargetsSafe(edict_t *relay)
{
	edict_t *target = NULL;
	edict_t *master;
	qboolean found = false;

	if (!relay || !relay->inuse || !relay->classname ||
	    Q_stricmp(relay->classname, "trigger_relay") ||
	    relay->use != trigger_relay_use || !isfinite(relay->delay) ||
	    relay->delay != 0.0f || relay->killtarget || relay->pathtarget ||
	    relay->message || !relay->target || !relay->target[0] ||
	    SG_OracleEntityKilltargetable(relay))
		return false;
	while ((target = G_Find(target, (int)offsetof(edict_t, targetname),
	                        relay->target)) != NULL)
	{
		if (!target->inuse || !target->classname)
			return false;
		if (strncmp(target->classname, "func_door", 9) == 0)
		{
			if (!SG_OracleDeclaredDoorTeamCanonical(target, &master) ||
			    !SG_OracleDeclaredDoorTeamSafe(target) ||
			    ((target->flags & FL_TEAMSLAVE) &&
			     !SG_OracleDeclaredMasterReachedBefore(relay, target, master)))
				return false;
			if (!(target->flags & FL_TEAMSLAVE))
				found = true;
			continue;
		}
		if (!Q_stricmp(target->classname, "target_speaker") &&
		    target->use == Use_Target_Speaker)
			continue;
		if (!Q_stricmp(target->classname, "trigger_relay") &&
		    target->use == trigger_relay_use &&
		    SG_OraclePlanSoundOnlyTargets(target, 1))
			continue;
		return false;
	}
	return found;
}

static qboolean SG_OracleDeclaredActivatorSafeWithDelay(edict_t *trigger,
	qboolean require_positive_delay)
{
	edict_t *target = NULL;
	edict_t *master;
	qboolean found = false;

	if (!trigger || !trigger->inuse || trigger->solid != SOLID_TRIGGER ||
	    SG_OracleEntityKilltargetable(trigger))
		return false;
	/* Canonical automatic door triggers have no mapper-controlled target
	 * closure: Think_SpawnDoorTrigger owns them directly from one safe master.
	 * Their fixed one-second debounce is accounted by the shared cost helper. */
	if (trigger->touch == Touch_DoorTrigger)
	{
		if (require_positive_delay)
			return false;
		if (!trigger->owner ||
		    !SG_OracleDeclaredDoorTeamCanonical(trigger->owner, &master) ||
		    trigger->owner != master)
			return false;
		return trigger->movetype == MOVETYPE_NONE &&
		       SG_OracleDeclaredDoorTeamSafe(master);
	}
	if (!trigger->classname ||
	    strcmp(trigger->classname, "trigger_multiple") != 0 ||
	    trigger->touch != Touch_Multi ||
	    (trigger->spawnflags & (2 | 4)) || !isfinite(trigger->wait) ||
	    trigger->wait <= 0.0f ||
	    !VectorCompare(trigger->movedir, vec3_origin) ||
	    !isfinite(trigger->delay) ||
	    (require_positive_delay ? trigger->delay <= 0.0f :
	        trigger->delay != 0.0f) ||
	    trigger->delay > (float)UINT32_MAX / 1000.0f || trigger->killtarget ||
	    !trigger->target || !trigger->target[0])
		return false;
	while ((target = G_Find(target, (int)offsetof(edict_t, targetname),
	                        trigger->target)) != NULL)
	{
		if (!target->inuse || !target->classname)
			return false;
		if (strncmp(target->classname, "func_door", 9) == 0)
		{
			if (!SG_OracleDeclaredDoorTeamCanonical(target, &master) ||
			    !SG_OracleDeclaredDoorTeamSafe(target) ||
			    ((target->flags & FL_TEAMSLAVE) &&
			     !SG_OracleDeclaredMasterReachedBefore(trigger, target, master)))
				return false;
			if (!(target->flags & FL_TEAMSLAVE))
				found = true;
			continue;
		}
		if (!Q_stricmp(target->classname, "target_speaker") &&
		    target->use == Use_Target_Speaker)
			continue;
		if (!Q_stricmp(target->classname, "trigger_relay") &&
		    target->use == trigger_relay_use)
		{
			if (SG_OraclePlanSoundOnlyTargets(target, 1))
				continue;
			if (!require_positive_delay &&
			    SG_OracleDeclaredRelayDoorTargetsSafe(target))
			{
				found = true;
				continue;
			}
		}
		return false;
	}
	return found;
}

static qboolean SG_OracleDeclaredActivatorSafe(edict_t *trigger)
{
	return SG_OracleDeclaredActivatorSafeWithDelay(trigger, false);
}

qboolean SG_DeclaredDoorDelayedActivatorSafe(edict_t *trigger,
	uint32_t *delay_ms_out)
{
	double milliseconds;

	if (delay_ms_out)
		*delay_ms_out = 0U;
	if (!delay_ms_out ||
	    !SG_OracleDeclaredActivatorSafeWithDelay(trigger, true))
		return false;
	milliseconds = (double)trigger->delay * 1000.0;
	if (!isfinite(milliseconds) || milliseconds <= 0.0 ||
	    milliseconds > (double)UINT32_MAX)
		return false;
	*delay_ms_out = (uint32_t)lround(milliseconds);
	if (*delay_ms_out == 0U)
		*delay_ms_out = 1U;
	return true;
}

qboolean SG_DeclaredDelayedDoorTouchMatches(edict_t *trigger,
	const vec3_t activator_origin)
{
	uint32_t delay_ms;

	return trigger && activator_origin &&
	       SG_DeclaredDoorDelayedActivatorSafe(trigger, &delay_ms) &&
	       SG_OracleDeclaredTriggerContains(trigger, activator_origin) &&
	       SG_DeclaredDelayedDoorOutsideSweep(trigger, activator_origin);
}

qboolean SG_DeclaredDelayedDoorSameSet(edict_t *first, edict_t *second)
{
	uint32_t first_delay;
	uint32_t second_delay;

	return first && second &&
	       SG_DeclaredDoorDelayedActivatorSafe(first, &first_delay) &&
	       SG_DeclaredDoorDelayedActivatorSafe(second, &second_delay) &&
	       first_delay == second_delay &&
	       SG_OracleDeclaredSameDoorSet(first, second);
}

/* The active BUTTON_DOOR controller intentionally admits only the stock
 * physical-touch button shape.  Remote, shootable, delayed, named, or
 * cross-team/effect-target buttons require different execution laws and
 * remain visible in the mechanism inventory without plan authority.  A
 * master followed by same-team slave no-ops is one canonical door team. */
static qboolean SG_OracleDeclaredButtonTargetsSafe(edict_t *button)
{
	edict_t *target = NULL;
	edict_t *master = NULL;
	int matches = 0;

	if (!button || !button->target || !button->target[0])
		return false;
	while ((target = G_Find(target, (int)offsetof(edict_t, targetname),
	                        button->target)) != NULL)
	{
		edict_t *target_master;

		matches++;
		if (!target->classname ||
		    strncmp(target->classname, "func_door", 9) != 0 ||
		    !SG_OracleDeclaredDoorTeamCanonical(target, &target_master) ||
		    !SG_OracleDeclaredDoorTeamSafe(target_master))
			return false;
		if (target == target_master)
		{
			/* Exactly one canonical team is admitted.  It must be reached at
			 * its master before G_UseTargets can encounter any same-name slave. */
			if (master)
				return false;
			master = target_master;
		}
		else if (!master || target_master != master ||
		         !(target->flags & FL_TEAMSLAVE))
			return false;
	}
	return matches > 0 && master != NULL;
}

static qboolean SG_OracleDeclaredButtonDoorSafe(edict_t *button)
{
	sg_mech_button_endpoints_t endpoints;
	int button_key = SG_OracleLiveEdictIndex(button);

	if (!button || button_key <= 0 || !button->inuse || !button->classname ||
	    strcmp(button->classname, "func_button") != 0 ||
	    button->movetype != MOVETYPE_STOP || button->solid != SOLID_BSP ||
	    button->touch != button_touch || button->use != button_use ||
	    button->think || button->blocked || button->targetname ||
	    button->killtarget || button->pathtarget || button->message ||
	    !button->target || !button->target[0] || button->spawnflags != 0 ||
	    button->health != 0 || button->max_health != 0 ||
	    button->takedamage != DAMAGE_NO || button->delay != 0.0f ||
	    button->nextthink != 0.0f ||
	    button->moveinfo.state != SG_PLAT_STATE_BOTTOM ||
	    !isfinite(button->moveinfo.distance) ||
	    !isfinite(button->moveinfo.speed) || button->moveinfo.speed <= 0.0f ||
	    !isfinite(button->moveinfo.accel) ||
	    !isfinite(button->moveinfo.decel) ||
	    fabsf(button->moveinfo.accel - button->moveinfo.speed) > 0.01f ||
	    fabsf(button->moveinfo.decel - button->moveinfo.speed) > 0.01f ||
	    !isfinite(button->moveinfo.wait) || button->moveinfo.wait <= 0.0f ||
	    SG_OracleEntityKilltargetable(button) ||
	    !SG_MechCatalogButtonBottomEndpoints((uint32_t)button_key, NULL,
	        button, &endpoints))
		return false;
	return SG_OracleDeclaredButtonTargetsSafe(button);
}

/* Validate only the synchronous proof pose installed by the generator.  The
 * catalog binds the immutable endpoint pair and the private pointer binds the
 * exact entity/scope; this must never become a general live admission path. */
static qboolean SG_OracleDeclaredButtonTopSafe(edict_t *button)
{
	sg_mech_button_endpoints_t endpoints;
	int button_key;
	int axis;

	button_key = SG_OracleLiveEdictIndex(button);
	if (!button || button != sg_oracle_declared_button_top || button_key <= 0 ||
	    !button->inuse || !button->classname ||
	    strcmp(button->classname, "func_button") != 0 ||
	    button->movetype != MOVETYPE_STOP || button->solid != SOLID_BSP ||
	    button->touch != button_touch || button->use != button_use ||
	    button->think || button->blocked || button->targetname ||
	    button->killtarget || button->pathtarget || button->message ||
	    !button->target || !button->target[0] || button->spawnflags != 0 ||
	    button->health != 0 || button->max_health != 0 ||
	    button->takedamage != DAMAGE_NO || button->delay != 0.0f ||
	    button->nextthink != 0.0f ||
	    button->moveinfo.state != SG_PLAT_STATE_TOP ||
	    !VectorCompare(button->velocity, vec3_origin) ||
	    !VectorCompare(button->avelocity, vec3_origin) ||
	    !SG_MechCatalogButtonEndpoints((uint32_t)button_key, NULL, button,
	        &endpoints))
		return false;
	for (axis = 0; axis < 3; axis++)
		if (button->s.origin[axis] !=
		        (float)endpoints.end_q8[axis] * 0.125f ||
		    button->s.old_origin[axis] != button->s.origin[axis])
			return false;
	return SG_OracleDeclaredButtonTargetsSafe(button);
}

static qboolean SG_OracleDeclaredDoorSourceSafe(edict_t *source)
{
	return SG_OracleDeclaredActivatorSafe(source) ||
	       SG_OracleDeclaredButtonDoorSafe(source) ||
	       SG_OracleDeclaredButtonTopSafe(source);
}

qboolean SG_DeclaredDoorActivatorSafe(edict_t *trigger)
{
	return SG_OracleDeclaredActivatorSafe(trigger);
}

qboolean SG_DeclaredDoorDirectActivatorSafe(edict_t *trigger)
{
	return SG_OracleDeclaredActivatorSafe(trigger) &&
	       trigger->touch == Touch_Multi;
}

qboolean SG_DeclaredButtonDoorSafe(edict_t *button)
{
	return SG_OracleDeclaredButtonDoorSafe(button);
}

qboolean SG_OracleButtonCarryClear(edict_t *button, const vec3_t from,
	const vec3_t to, qboolean population_independent)
{
	vec3_t mins = { -16.0f, -16.0f, -24.0f };
	vec3_t maxs = { 16.0f, 16.0f, 32.0f };
	trace_t trace;

	if (!button || !from || !to ||
	    (!SG_OracleDeclaredButtonDoorSafe(button) &&
	     !SG_OracleDeclaredButtonTopSafe(button)))
		return false;
	/* The authenticated entry pusher is the support carrying this hull, so it
	 * is the sole excluded entity. World and every foreign solid remain
	 * authoritative over the complete endpoint segment. */
	if (!SG_OracleStablePopulationTrace(from, mins, maxs, to, button,
	        population_independent, &trace))
		return false;
	if (trace.startsolid || trace.allsolid || trace.fraction != 1.0f)
		return false;
	return true;
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
	double milliseconds;
	float wait;

	if (!SG_OracleDeclaredDoorSourceSafe(trigger))
		return -1;
	wait = trigger->touch == Touch_DoorTrigger ? 1.0f :
	       SG_OracleDeclaredButtonDoorSafe(trigger)
	           ? trigger->moveinfo.wait : trigger->wait;
	/* Long trigger cooldowns are not intrinsically unsafe: the complete door
	 * contract applies its own bounded-duration policy below.  Reject here only
	 * when the millisecond conversion cannot fit the signed return type. */
	if (!isfinite(wait) || wait <= 0.0f)
		return -1;
	milliseconds = (double)wait * 1000.0;
	if (!isfinite(milliseconds) || milliseconds > (double)INT_MAX)
		return -1;
	/* Match the sealed catalog's canonical float-to-millisecond conversion.
	 * Using ceilf here made generation disagree with the wire node by one
	 * millisecond for values such as 312.0004 seconds. */
	return (int)lround(milliseconds);
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

/* Pointer subtraction and relational comparison are undefined for a stale
 * pointer that is not part of g_edicts.  The bounded identity scan is cheap
 * (MAX_EDICTS is 1024) and gives the guard a fail-closed exact-live test. */
static int SG_OracleLiveEdictIndex(const edict_t *ent)
{
	int i;

	if (!ent || !g_edicts || globals.edicts != g_edicts ||
	    globals.edict_size != (int)sizeof(edict_t) ||
	    globals.num_edicts <= 1 || globals.num_edicts > MAX_EDICTS ||
	    game.maxentities <= 1 || game.maxentities > MAX_EDICTS ||
	    globals.max_edicts != game.maxentities ||
	    globals.num_edicts > game.maxentities || !game.clients ||
	    game.maxclients <= 0 || game.maxentities <= BODY_QUEUE_SIZE ||
	    game.maxclients >= game.maxentities - BODY_QUEUE_SIZE)
		return -1;
	for (i = 1; i < globals.num_edicts; i++)
	{
		if (ent != &g_edicts[i])
			continue;
		return ent->s.number == i ? i : -1;
	}
	return -1;
}

/* SV_LinkEdict owns these fields as one transaction.  A positive linkcount
 * alone only proves that an entity was linked sometime in the past: unlinking
 * deliberately leaves it positive.  Recompute the engine-visible bounds so a
 * stale or partially mutated edict cannot be used to release a mover guard. */
static qboolean SG_OracleLinkedBoundsValid(edict_t *ent)
{
	float radius = 0.0f;
	qboolean rotated_bsp;
	int axis;

	if (!ent || !ent->area.prev || !ent->area.next ||
	    ent->area.prev == &ent->area || ent->area.next == &ent->area ||
	    !SG_OracleFinite3(ent->size))
		return false;
	rotated_bsp = ent->solid == SOLID_BSP &&
	              (ent->s.angles[0] != 0.0f || ent->s.angles[1] != 0.0f ||
	               ent->s.angles[2] != 0.0f);
	if (rotated_bsp)
	{
		for (axis = 0; axis < 3; axis++)
		{
			float lo = fabsf(ent->mins[axis]);
			float hi = fabsf(ent->maxs[axis]);

			if (lo > radius) radius = lo;
			if (hi > radius) radius = hi;
		}
	}
	for (axis = 0; axis < 3; axis++)
	{
		float size = ent->maxs[axis] - ent->mins[axis];
		float absmin = ent->s.origin[axis] +
		               (rotated_bsp ? -radius : ent->mins[axis]) - 1.0f;
		float absmax = ent->s.origin[axis] +
		               (rotated_bsp ? radius : ent->maxs[axis]) + 1.0f;

		if (ent->size[axis] != size || ent->absmin[axis] != absmin ||
		    ent->absmax[axis] != absmax)
			return false;
	}
	return true;
}

static qboolean SG_OracleRotatingPoseValid(edict_t *member)
{
	int axis, changed = 0;

	for (axis = 0; axis < 3; axis++)
	{
		float start = member->moveinfo.start_angles[axis];
		float end = member->moveinfo.end_angles[axis];
		float current = member->s.angles[axis];
		double low = start < end ? (double)start : (double)end;
		double high = start > end ? (double)start : (double)end;
		double delta = (double)end - (double)start;
		double scale;
		double envelope;

		if (start == end)
		{
			if (current != start)
				return false;
			continue;
		}
		changed++;
		/* AngleMove_Final forms (end-current)/FRAMETIME and the pusher then
		 * multiplies by FRAMETIME again.  Those two float roundings can put the
		 * stock terminal a few ULPs beyond the serialized endpoint.  Bound that
		 * exact operation chain conservatively without admitting a meaningful
		 * out-of-sweep pose. */
		scale = fmax(fabs((double)start), fabs((double)end));
		scale = fmax(scale, fabs(delta));
		scale = fmax(scale, 1.0);
		envelope = 64.0 * (double)FLT_EPSILON * scale;
		if (fabs(delta) > 1440.0 || (double)current < low - envelope ||
		    (double)current > high + envelope)
			return false;
	}
	return changed == 1;
}

static qboolean SG_OracleMoverSweepIdentity(edict_t *member)
{
	int axis, member_index;
	qboolean rotating, secret;

	member_index = SG_OracleLiveEdictIndex(member);
	if (member_index <= game.maxclients + BODY_QUEUE_SIZE || !member->inuse ||
	    member->linkcount <= 0 || member->solid != SOLID_BSP ||
	    member->movetype != MOVETYPE_PUSH || member->prethink ||
	    !member->classname)
		return false;
	rotating = strcmp(member->classname, "func_door_rotating") == 0;
	secret = strcmp(member->classname, "func_door") == 0 &&
	         member->use == door_secret_use;
	if ((!rotating && strcmp(member->classname, "func_door") != 0) ||
	    (rotating && member->use != door_use) ||
	    (!rotating && !secret && member->use != door_use) ||
	    !SG_OracleFinite3(member->s.origin) ||
	    !SG_OracleFinite3(member->mins) ||
	    !SG_OracleFinite3(member->maxs) ||
	    !SG_OracleFinite3(member->absmin) ||
	    !SG_OracleFinite3(member->absmax) ||
	    !SG_OracleFinite3(member->moveinfo.start_origin) ||
	    !SG_OracleFinite3(member->moveinfo.end_origin) ||
	    !SG_OracleFinite3(member->moveinfo.start_angles) ||
	    !SG_OracleFinite3(member->moveinfo.end_angles) ||
	    !SG_OracleFinite3(member->s.angles) ||
	    !SG_OracleFinite3(member->velocity) ||
	    !SG_OracleFinite3(member->avelocity) ||
	    (secret && (!SG_OracleFinite3(member->pos1) ||
	                !SG_OracleFinite3(member->pos2))))
		return false;
	for (axis = 0; axis < 3; axis++)
	{
		if (member->mins[axis] > member->maxs[axis] ||
		    member->absmin[axis] > member->absmax[axis])
			return false;
	}
	if (!SG_OracleLinkedBoundsValid(member))
		return false;
	if (rotating)
		return SG_OracleRotatingPoseValid(member);
	return member->s.angles[0] == 0.0f && member->s.angles[1] == 0.0f &&
	       member->s.angles[2] == 0.0f;
}

/* SV_Physics_Pusher calls a failed part's blocked callback after rollback and
 * calls every part's due think after a successful move.  Geometry alone is
 * therefore not an interlock: a drifted function pointer could mutate a
 * protected subject after a positive sweep result.  Admit only callback/state
 * combinations produced by the stock constant-speed door state machine. */
static qboolean SG_OracleMoverCallbackStateValid(edict_t *member)
{
	void (*think)(edict_t *);
	qboolean rotating, stopped, terminal;
	int axis;

	if (!member || member->use != door_use ||
	    member->blocked != door_blocked ||
	    (member->spawnflags & (SG_DOOR_START_OPEN | SG_DOOR_TOGGLE)) ||
	    !isfinite(member->nextthink) || member->nextthink < 0.0f ||
	    !isfinite(member->moveinfo.speed) ||
	    member->moveinfo.speed <= 0.0f ||
	    !isfinite(member->moveinfo.accel) ||
	    !isfinite(member->moveinfo.decel) ||
	    !isfinite(member->moveinfo.distance) ||
	    member->moveinfo.distance == 0.0f ||
	    !isfinite(member->moveinfo.wait) ||
	    !isfinite(member->moveinfo.remaining_distance) ||
	    !SG_OracleFinite3(member->moveinfo.dir))
		return false;
	rotating = strcmp(member->classname, "func_door_rotating") == 0;
	/* Think_AccelMove has a larger mutable numeric state than the prospective
	 * step can authenticate.  Canonical translating doors use the exact
	 * constant-speed branch in Move_Calc. */
	if (!rotating &&
	    (member->moveinfo.accel != member->moveinfo.speed ||
	     member->moveinfo.decel != member->moveinfo.speed))
		return false;
	if (member->moveinfo.endfunc != NULL &&
	    member->moveinfo.endfunc != door_hit_top &&
	    member->moveinfo.endfunc != door_hit_bottom)
		return false;
	switch (member->moveinfo.state)
	{
	case SG_PLAT_STATE_TOP:
		if (member->moveinfo.endfunc != door_hit_top)
			return false;
		break;
	case SG_PLAT_STATE_BOTTOM:
		if (member->moveinfo.endfunc != NULL &&
		    member->moveinfo.endfunc != door_hit_bottom)
			return false;
		break;
	case SG_PLAT_STATE_UP:
		if (member->moveinfo.endfunc != door_hit_top)
			return false;
		break;
	case SG_PLAT_STATE_DOWN:
		if (member->moveinfo.endfunc != door_hit_bottom)
			return false;
		break;
	default:
		return false;
	}
	stopped = true;
	for (axis = 0; axis < 3; axis++)
		if (member->velocity[axis] != 0.0f ||
		    member->avelocity[axis] != 0.0f)
			stopped = false;
	terminal = member->moveinfo.state == SG_PLAT_STATE_TOP ||
	           member->moveinfo.state == SG_PLAT_STATE_BOTTOM;
	think = member->think;
	if (!think)
		return member->nextthink == 0.0f && terminal && stopped;
	if (think == Think_CalcMoveSpeed || think == Think_SpawnDoorTrigger)
		return member->moveinfo.state == SG_PLAT_STATE_BOTTOM &&
		       member->moveinfo.endfunc == NULL && stopped;
	if (think == door_go_down)
		return member->nextthink > 0.0f && stopped &&
		       member->moveinfo.state == SG_PLAT_STATE_TOP;
	if (!rotating &&
	    (think == Move_Begin || think == Move_Final || think == Move_Done))
	{
		if (member->nextthink > 0.0f)
			return !terminal;
		/* SV_RunThink clears nextthink before the terminal callback.  The
		 * callback pointer then remains installed on an idle stock door. */
		return terminal && stopped;
	}
	if (rotating && (think == AngleMove_Begin ||
	                 think == AngleMove_Final ||
	                 think == AngleMove_Done))
	{
		if (member->nextthink > 0.0f)
			return !terminal;
		return terminal && stopped;
	}
	return false;
}

static qboolean SG_OracleMoverSubjectIdentity(edict_t *subject)
{
	edict_t *owner;
	int owner_index, subject_index;

	subject_index = SG_OracleLiveEdictIndex(subject);
	if (subject_index < 0 || !subject->inuse ||
	    subject->linkcount <= 0 || !subject->classname ||
	    !SG_OracleFinite3(subject->s.origin) ||
	    !SG_OracleFinite3(subject->mins) ||
	    !SG_OracleFinite3(subject->maxs) ||
	    subject->mins[0] > subject->maxs[0] ||
	    subject->mins[1] > subject->maxs[1] ||
	    subject->mins[2] > subject->maxs[2] ||
	    !SG_OracleLinkedBoundsValid(subject))
		return false;

	/* A live/dead client and its copied body are axis-aligned solid boxes.
	 * The grapple is a point box in flight and becomes a trigger on attach. */
	if (subject->client)
		return subject_index > 0 && subject_index <= game.maxclients &&
		       subject->client == &game.clients[subject_index - 1] &&
		       !strcmp(subject->classname, "player") &&
		       subject->solid == SOLID_BBOX &&
		       (subject->movetype == MOVETYPE_WALK ||
		        subject->movetype == MOVETYPE_TOSS);
	if (!strcmp(subject->classname, "bodyque"))
		return subject_index > game.maxclients &&
		       subject_index <= game.maxclients + BODY_QUEUE_SIZE &&
		       subject->solid == SOLID_BBOX &&
		       subject->movetype == MOVETYPE_TOSS;
	if (subject_index <= game.maxclients + BODY_QUEUE_SIZE ||
	    strcmp(subject->classname, "noclass") ||
	    (subject->solid != SOLID_BBOX && subject->solid != SOLID_TRIGGER) ||
	    subject->movetype != MOVETYPE_FLYMISSILE ||
	    subject->touch != hook_touch || subject->die != hook_die)
		return false;
	owner = subject->owner;
	owner_index = SG_OracleLiveEdictIndex(owner);
	return owner_index > 0 && owner_index <= game.maxclients && owner->inuse &&
	       owner->client == &game.clients[owner_index - 1] &&
	       owner->classname && !strcmp(owner->classname, "player") &&
	       owner->client->hook == subject;
}

qboolean SG_MoverSubjectOutsideSweep(edict_t *member, edict_t *subject)
{
	vec3_t mins, maxs;
	int axis;

	if (member == subject || !SG_OracleMoverSweepIdentity(member) ||
	    !SG_OracleMoverSubjectIdentity(subject))
		return false;
	SG_OracleDoorBounds(member, subject->mins, subject->maxs, mins, maxs);
	if (!SG_OracleFinite3(mins) || !SG_OracleFinite3(maxs))
		return false;
	for (axis = 0; axis < 3; axis++)
		if (mins[axis] > maxs[axis])
			return false;
	/* SegmentBox is inclusive: exact boundary contact remains occupied. */
	return !SG_OracleSegmentBox(subject->s.origin, subject->s.origin,
	                           mins, maxs);
}

qboolean SG_MoverProspectivePusherValid(edict_t *member)
{
	vec3_t mins, maxs;
	int axis;

	if (!SG_OracleMoverSweepIdentity(member) ||
	    !SG_OracleMoverCallbackStateValid(member) ||
	    !SG_OracleProspectiveDoorBounds(member, NULL, NULL, mins, maxs) ||
	    !SG_OracleFinite3(mins) || !SG_OracleFinite3(maxs))
		return false;
	for (axis = 0; axis < 3; axis++)
		if (mins[axis] > maxs[axis])
			return false;
	return true;
}

qboolean SG_MoverSubjectOutsideProspectivePush(edict_t *member,
	edict_t *subject)
{
	vec3_t mins, maxs;
	int axis;

	if (member == subject || !SG_OracleMoverSweepIdentity(member) ||
	    !SG_OracleMoverCallbackStateValid(member) ||
	    !SG_OracleMoverSubjectIdentity(subject) ||
	    subject->groundentity == member ||
	    !SG_OracleProspectiveDoorBounds(member, subject->mins, subject->maxs,
	        mins, maxs) || !SG_OracleFinite3(mins) ||
	    !SG_OracleFinite3(maxs))
		return false;
	for (axis = 0; axis < 3; axis++)
		if (mins[axis] > maxs[axis])
			return false;
	return !SG_OracleSegmentBox(subject->s.origin, subject->s.origin,
	                           mins, maxs);
}

static qboolean SG_DeclaredDoorOutsideSweepInternal(edict_t *trigger,
	const vec3_t origin, qboolean delayed)
{
	vec3_t hull_mins = { -16.0f, -16.0f, -24.0f };
	vec3_t hull_maxs = { 16.0f, 16.0f, 32.0f };
	edict_t *members[SG_PHANTOM_ARMED_DOORS];
	int count, i;

	if (!(delayed ? SG_OracleDeclaredActivatorSafeWithDelay(trigger, true) :
	        SG_OracleDeclaredDoorSourceSafe(trigger)) || !origin)
		return false;
	count = delayed ? SG_DeclaredDelayedDoorMembers(trigger, members,
	    SG_PHANTOM_ARMED_DOORS) : SG_DeclaredDoorMembers(trigger, members,
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

qboolean SG_DeclaredDoorOutsideSweep(edict_t *trigger, const vec3_t origin)
{
	return SG_DeclaredDoorOutsideSweepInternal(trigger, origin, false);
}

qboolean SG_DeclaredDelayedDoorOutsideSweep(edict_t *trigger,
	const vec3_t origin)
{
	return SG_DeclaredDoorOutsideSweepInternal(trigger, origin, true);
}

static qboolean SG_DeclaredDoorCrossesSweepInternal(edict_t *trigger,
	const vec3_t from, const vec3_t to, qboolean delayed)
{
	vec3_t hull_mins = { -16.0f, -16.0f, -24.0f };
	vec3_t hull_maxs = { 16.0f, 16.0f, 32.0f };
	edict_t *members[SG_PHANTOM_ARMED_DOORS];
	int count, i;

	if (!(delayed ? SG_OracleDeclaredActivatorSafeWithDelay(trigger, true) :
	        SG_OracleDeclaredDoorSourceSafe(trigger)) || !from || !to)
		return false;
	count = delayed ? SG_DeclaredDelayedDoorMembers(trigger, members,
	    SG_PHANTOM_ARMED_DOORS) : SG_DeclaredDoorMembers(trigger, members,
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

qboolean SG_DeclaredDoorCrossesSweep(edict_t *trigger, const vec3_t from,
	const vec3_t to)
{
	return SG_DeclaredDoorCrossesSweepInternal(trigger, from, to, false);
}

qboolean SG_DeclaredDelayedDoorCrossesSweep(edict_t *trigger,
	const vec3_t from, const vec3_t to)
{
	return SG_DeclaredDoorCrossesSweepInternal(trigger, from, to, true);
}

static int SG_BoundDoorMembers(
	const sg_rune_mechanism_binding_t *binding, edict_t **members,
	int capacity)
{
	uint32_t keys[SG_RUNE_BINDING_MAX_MOVERS];
	size_t count;
	size_t index;

	if (sg_oracle_bound_member_scope.active)
	{
		if (binding != sg_oracle_bound_member_scope.binding || !members ||
		    capacity < (int)sg_oracle_bound_member_scope.count)
			return -1;
		for (index = 0U; index < sg_oracle_bound_member_scope.count; index++)
			members[index] = sg_oracle_bound_member_scope.members[index];
		return (int)sg_oracle_bound_member_scope.count;
	}
	if (!binding || !members || capacity <= 0 ||
	    !SG_OracleBoundDoorBindingCurrent(binding) ||
	    !SG_OracleBoundDoorMoverKeys(binding, keys, &count) ||
	    count == 0U || count > (size_t)capacity)
		return -1;
	for (index = 0U; index < count; index++)
	{
		members[index] = SG_OracleBoundDoorResolveNode(binding,
		    keys[index]);
		if (!members[index] || !SG_OracleMoverSweepIdentity(members[index]))
			return -1;
	}
	return SG_OracleBoundDoorBindingCurrent(binding) ? (int)count : -1;
}

/* Loader replay authenticates one entry trigger, but an already-open egress
 * may cross an opposite-side trigger that refreshes the identical mover set.
 * Compare that live trigger's complete canonical members against the sealed
 * binding; a shared target name or partial overlap is not sufficient. */
static qboolean SG_OracleBoundSameDoorSet(
	const sg_rune_mechanism_binding_t *binding, edict_t *trigger)
{
	edict_t *bound[SG_RUNE_BINDING_MAX_MOVERS];
	edict_t *candidate[SG_RUNE_BINDING_MAX_MOVERS];
	int bound_count, candidate_count, candidate_index, bound_index;

	if (!binding || !binding->plan ||
	    binding->plan->controller_kind !=
	        SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR)
		return false;
	bound_count = SG_BoundDoorMembers(binding, bound,
	    SG_RUNE_BINDING_MAX_MOVERS);
	candidate_count = SG_DeclaredDoorMembers(trigger, candidate,
	    SG_RUNE_BINDING_MAX_MOVERS);
	if (bound_count <= 0 || candidate_count != bound_count)
		return false;
	for (candidate_index = 0; candidate_index < candidate_count;
	     candidate_index++)
	{
		for (bound_index = 0; bound_index < bound_count; bound_index++)
			if (candidate[candidate_index] == bound[bound_index])
				break;
		if (bound_index == bound_count)
			return false;
	}
	return SG_OracleBoundDoorBindingCurrent(binding);
}

qboolean SG_BoundDoorOutsideSweep(
	const sg_rune_mechanism_binding_t *binding, const vec3_t origin)
{
	vec3_t hull_mins = { -16.0f, -16.0f, -24.0f };
	vec3_t hull_maxs = { 16.0f, 16.0f, 32.0f };
	edict_t *members[SG_RUNE_BINDING_MAX_MOVERS];
	int count;
	int index;

	if (!origin || (count = SG_BoundDoorMembers(binding, members,
	        SG_RUNE_BINDING_MAX_MOVERS)) <= 0)
		return false;
	for (index = 0; index < count; index++)
	{
		vec3_t mins, maxs;

		if (sg_oracle_bound_member_scope.active &&
		    binding == sg_oracle_bound_member_scope.binding)
			SG_OracleDoorBoundsScoped(
			    sg_oracle_bound_member_scope.member_indices[index],
			    members[index], hull_mins, hull_maxs, mins, maxs);
		else
			SG_OracleDoorBounds(members[index], hull_mins, hull_maxs,
			    mins, maxs);
		if (SG_OracleSegmentBox(origin, origin, mins, maxs))
			return false;
	}
	return sg_oracle_bound_member_scope.active &&
	    binding == sg_oracle_bound_member_scope.binding
	    ? true : SG_OracleBoundDoorBindingCurrent(binding);
}

qboolean SG_BoundDoorCrossesSweep(
	const sg_rune_mechanism_binding_t *binding, const vec3_t from,
	const vec3_t to)
{
	vec3_t hull_mins = { -16.0f, -16.0f, -24.0f };
	vec3_t hull_maxs = { 16.0f, 16.0f, 32.0f };
	edict_t *members[SG_RUNE_BINDING_MAX_MOVERS];
	int count;
	int index;

	if (!from || !to || (count = SG_BoundDoorMembers(binding, members,
	        SG_RUNE_BINDING_MAX_MOVERS)) <= 0)
		return false;
	for (index = 0; index < count; index++)
	{
		vec3_t mins, maxs;

		SG_OracleDoorBounds(members[index], hull_mins, hull_maxs,
		    mins, maxs);
		if (SG_OracleSegmentBox(from, to, mins, maxs))
			return SG_OracleBoundDoorBindingCurrent(binding);
	}
	return false;
}

qboolean SG_BoundDoorTouchMatches(
	const sg_rune_mechanism_binding_t *binding,
	const vec3_t activator_origin)
{
	return binding && activator_origin &&
	       binding->plan->controller_kind !=
	           SG_MECHANISM_CONTROLLER_BUTTON_DOOR &&
	       SG_OracleBoundDoorBindingCurrent(binding) &&
	       SG_OracleDeclaredTriggerContains(binding->entry_entity,
	           activator_origin) &&
	       SG_BoundDoorOutsideSweep(binding, activator_origin) &&
	       SG_OracleBoundDoorBindingCurrent(binding);
}

qboolean SG_BoundDoorEntryContactMatches(
	const sg_rune_mechanism_binding_t *binding, const vec3_t origin)
{
	edict_t *entry;

	if (!binding || !origin ||
	    !SG_OracleBoundDoorBindingCurrent(binding) ||
	    !(entry = binding->entry_entity))
		return false;
	if (binding->plan->controller_kind ==
	    SG_MECHANISM_CONTROLLER_BUTTON_DOOR)
	{
		/* A solid BSP button fires on the first exact collision trace, not when
		 * the player merely enters its broad expanded AABB.  The latter can be
		 * tens of units early on a wide floor plate and made loader replay pause
		 * before the generated contact point. */
		return SG_DeclaredButtonDoorContactMatches(entry, origin) &&
		       SG_OracleBoundDoorBindingCurrent(binding);
	}
	return SG_OracleDeclaredTriggerContains(entry, origin) &&
	       SG_OracleBoundDoorBindingCurrent(binding);
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

static qboolean SG_DeclaredDelayedDoorForLinkMatches(edict_t *expected,
	const vec3_t anchor, const vec3_t source)
{
	edict_t *match = NULL;
	uint32_t expected_delay;
	int i;

	if (!expected || !anchor || !source ||
	    !SG_DeclaredDoorDelayedActivatorSafe(expected, &expected_delay) ||
	    !SG_DeclaredDelayedDoorTouchMatches(expected, anchor) ||
	    !SG_DeclaredDelayedDoorOutsideSweep(expected, source))
		return false;
	for (i = 1; i < globals.num_edicts; i++)
	{
		edict_t *trigger = &g_edicts[i];
		uint32_t delay_ms;

		if (!SG_DeclaredDoorDelayedActivatorSafe(trigger, &delay_ms) ||
		    !SG_DeclaredDelayedDoorTouchMatches(trigger, anchor) ||
		    !SG_DeclaredDelayedDoorOutsideSweep(trigger, source))
			continue;
		if (delay_ms != expected_delay ||
		    (match && !SG_DeclaredDelayedDoorSameSet(match, trigger)))
			return false;
		match = trigger;
	}
	return match && SG_DeclaredDelayedDoorSameSet(match, expected);
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

/* The serialized controller is one exact trigger entity.  Sharing a complete
 * mover set does not let a sibling consume its physical-touch transaction. */
qboolean SG_DeclaredDoorEquivalentTouch(edict_t *expected,
	edict_t *actual, const vec3_t activator_origin)
{
	return expected && actual && activator_origin &&
	       expected == actual &&
	       SG_OracleDeclaredActivatorSafe(expected) &&
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

qboolean SG_DeclaredDelayedDoorApproachSourceClear(edict_t *trigger,
	const vec3_t origin)
{
	uint32_t delay_ms;
	int i;

	if (!trigger || !origin ||
	    !SG_DeclaredDoorDelayedActivatorSafe(trigger, &delay_ms) ||
	    !SG_DeclaredDelayedDoorOutsideSweep(trigger, origin))
		return false;
	for (i = 1; i < globals.num_edicts; i++)
	{
		edict_t *other = &g_edicts[i];
		uint32_t other_delay_ms;

		if (!SG_DeclaredDoorDelayedActivatorSafe(other, &other_delay_ms) ||
		    !SG_DeclaredDelayedDoorSameSet(trigger, other))
			continue;
		if (SG_OracleDeclaredTriggerContains(other, origin))
			return false;
	}
	return true;
}

sg_button_contact_status_t SG_DeclaredButtonDoorContactStatus(
	edict_t *button, const vec3_t origin)
{
	vec3_t mins = { -16.0f, -16.0f, -24.0f };
	vec3_t maxs = { 16.0f, 16.0f, 32.0f };
	vec3_t center, direction, end;
	float distance;
	trace_t trace;
	int axis;

	if (!SG_OracleDeclaredButtonDoorSafe(button))
		return SG_BUTTON_CONTACT_UNSAFE;
	if (!origin || !SG_OracleFinite3(origin))
		return SG_BUTTON_CONTACT_BAD_ORIGIN;
	if (!SG_DeclaredDoorOutsideSweep(button, origin))
		return SG_BUTTON_CONTACT_SWEEP_OCCUPIED;
	for (axis = 0; axis < 3; axis++)
		center[axis] = 0.5f * (button->absmin[axis] + button->absmax[axis]);
	VectorSubtract(center, origin, direction);
	distance = VectorLength(direction);
	if (!isfinite(distance) || distance <= 0.01f)
		return SG_BUTTON_CONTACT_DEGENERATE;
	VectorScale(direction, 4.0f / distance, direction);
	VectorAdd(origin, direction, end);
	if (!SG_OracleStablePopulationTrace(origin, mins, maxs, end, NULL,
	        sg_oracle_loader_replay, &trace))
		return SG_BUTTON_CONTACT_WRONG_HIT;
	if (trace.startsolid)
		return SG_BUTTON_CONTACT_STARTSOLID;
	if (trace.allsolid)
		return SG_BUTTON_CONTACT_ALLSOLID;
	if (trace.fraction >= 1.0f)
		return SG_BUTTON_CONTACT_NO_HIT;
	if (trace.ent != button)
		return SG_BUTTON_CONTACT_WRONG_HIT;
	return SG_BUTTON_CONTACT_OK;
}

qboolean SG_DeclaredButtonDoorContactMatches(edict_t *button,
	const vec3_t origin)
{
	return SG_DeclaredButtonDoorContactStatus(button, origin) ==
	       SG_BUTTON_CONTACT_OK;
}

qboolean SG_DeclaredButtonDoorApproachSourceClear(edict_t *button,
	const vec3_t origin)
{
	return SG_OracleDeclaredButtonDoorSafe(button) &&
	       SG_DeclaredDoorOutsideSweep(button, origin) &&
	       SG_DeclaredButtonDoorContactStatus(button, origin) !=
	           SG_BUTTON_CONTACT_OK;
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
static qboolean SG_OracleDoorStepSafe(edict_t *ent, edict_t *trigger,
	const sg_rune_mechanism_binding_t *binding, const usercmd_t *cmd)
{
	edict_t *old_passent = sg_oracle_passent;
	edict_t *old_expected = sg_oracle_declared_expected;
	edict_t *old_door = sg_oracle_declared_door;
	const sg_rune_mechanism_binding_t *old_bound = sg_oracle_bound_door;
	qboolean old_world = sg_oracle_world_only;
	qboolean old_contaminated = sg_oracle_contaminated;
	qboolean old_touched = sg_oracle_declared_touched;
	int old_action = sg_oracle_declared_action;
	sg_phantom_t ph;
	usercmd_t step_cmd;
	qboolean safe = false;
	int controller_kind;
	int axis;

	/* Only a current serialized binding distinguishes a DIRECT controller.
	 * The legacy unbound wrapper has no authenticated controller identity and
	 * therefore retains the historical dry-only AUTO law. */
	controller_kind = binding && binding->plan
	    ? binding->plan->controller_kind
	    : SG_MECHANISM_CONTROLLER_AUTO_DOOR;
	if (!ent || !ent->inuse || !ent->client || !trigger || !cmd ||
	    !SG_OracleDoorEgressWaterSafe(controller_kind, ent->waterlevel,
	        ent->watertype) ||
	    (binding && (!SG_OracleBoundDoorBindingCurrent(binding) ||
	                 binding->entry_entity != trigger)) ||
	    !(binding ? SG_BoundDoorOutsideSweep(binding, ent->s.origin) :
	                 SG_DeclaredDoorOutsideSweep(trigger, ent->s.origin)))
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
	sg_oracle_bound_door = binding;
	sg_oracle_declared_action = binding && binding->plan->controller_kind ==
	    SG_MECHANISM_CONTROLLER_BUTTON_DOOR ? RL_BUTTON_DOOR : RL_DOOR;
	sg_oracle_declared_touched = false;
	if (!SG_OracleTriggerOverlap(&ph) && !SG_OracleSolidOverlap(&ph))
	{
		step_cmd = *cmd;
		SG_OracleRun(&ph, &step_cmd, 1);
		safe = !sg_oracle_contaminated && ph.groundentity &&
		       SG_OracleDoorEgressWaterSafe(controller_kind, ph.waterlevel,
		           ph.watertype) &&
		       (binding ? SG_BoundDoorOutsideSweep(binding, ph.origin) :
		                  SG_DeclaredDoorOutsideSweep(trigger, ph.origin));
	}

done:
	sg_oracle_passent = old_passent;
	sg_oracle_world_only = old_world;
	sg_oracle_contaminated = old_contaminated;
	sg_oracle_declared_expected = old_expected;
	sg_oracle_declared_door = old_door;
	sg_oracle_bound_door = old_bound;
	sg_oracle_declared_action = old_action;
	sg_oracle_declared_touched = old_touched;
	return safe;
}

qboolean SG_OracleDeclaredDoorStepSafe(edict_t *ent, edict_t *trigger,
	const usercmd_t *cmd)
{
	return SG_OracleDoorStepSafe(ent, trigger, NULL, cmd);
}

qboolean SG_OracleBoundDoorStepSafe(edict_t *ent,
	const sg_rune_mechanism_binding_t *binding, const usercmd_t *cmd)
{
	return binding ? SG_OracleDoorStepSafe(ent, binding->entry_entity,
	    binding, cmd) : false;
}

static void SG_OracleDoorApproachObservation(const sg_phantom_t *ph,
	edict_t *trigger, const sg_rune_mechanism_binding_t *binding,
	qboolean fall_sampled, float fall_delta,
	sg_door_approach_observation_t *observation);

qboolean SG_OracleBoundDoorApproachStep(edict_t *ent,
	const sg_rune_mechanism_binding_t *binding, const usercmd_t *cmd,
	const sg_door_approach_state_t *state,
	sg_door_approach_prediction_t *prediction,
	sg_door_approach_reason_t *reason_out)
{
	edict_t *old_passent = sg_oracle_passent;
	edict_t *old_expected = sg_oracle_declared_expected;
	edict_t *old_door = sg_oracle_declared_door;
	const sg_rune_mechanism_binding_t *old_bound = sg_oracle_bound_door;
	qboolean old_world = sg_oracle_world_only;
	qboolean old_contaminated = sg_oracle_contaminated;
	qboolean old_touched = sg_oracle_declared_touched;
	int old_action = sg_oracle_declared_action;
	sg_door_approach_observation_t observation;
	sg_door_approach_result_t result;
	sg_phantom_t ph;
	usercmd_t step_cmd;
	qboolean ok = false;
	qboolean fall_sampled;
	float fall_delta = 0.0f;
	int axis;

	if (prediction)
		memset(prediction, 0, sizeof(*prediction));
	if (reason_out)
		*reason_out = SG_DOOR_APPROACH_REASON_ARGUMENT;
	if (!ent || !ent->inuse || !ent->client || !binding || !binding->plan ||
	    !binding->entry_entity || !cmd || !state || !prediction ||
	    binding->plan->controller_kind !=
	        SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR ||
	    !SG_OracleBoundDoorBindingCurrent(binding) ||
	    !SG_BoundDoorOutsideSweep(binding, ent->s.origin))
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
	ph.groundentity_entity = ent->groundentity;
	ph.watertype = ent->watertype;
	ph.waterlevel = ent->waterlevel;

	sg_oracle_passent = ent;
	sg_oracle_world_only = true;
	sg_oracle_contaminated = false;
	sg_oracle_declared_expected = binding->entry_entity;
	sg_oracle_declared_door = NULL;
	sg_oracle_bound_door = binding;
	sg_oracle_declared_action = RL_DOOR;
	sg_oracle_declared_touched = false;
	(void)SG_OracleTriggerOverlap(&ph);
	(void)SG_OracleSolidOverlap(&ph);
	SG_OracleDoorApproachObservation(&ph, binding->entry_entity, binding,
	    false, 0.0f, &observation);
	result = SG_DoorApproachPreStep(state, &observation, cmd->msec);
	if (result.reason != SG_DOOR_APPROACH_REASON_NONE)
	{
		if (reason_out)
			*reason_out = result.reason;
		goto done;
	}
	step_cmd = *cmd;
	SG_OracleRun(&ph, &step_cmd, 1);
	fall_sampled = ((state->elapsed_ms + cmd->msec) %
	    SG_DOOR_APPROACH_FRAME_MS) == 0;
	if (fall_sampled)
		fall_delta = P_FallDelta(state->old_frame_z, ph.velocity[2],
		    ph.groundentity, ph.waterlevel);
	SG_OracleDoorApproachObservation(&ph, binding->entry_entity, binding,
	    fall_sampled, fall_delta, &observation);
	prediction->state = *state;
	result = SG_DoorApproachPostStep(&prediction->state, &observation,
	    cmd->msec);
	if (result.reason != SG_DOOR_APPROACH_REASON_NONE ||
	    (sg_oracle_declared_touched && !ph.groundentity) ||
	    !SG_RuneMechanismBindingCurrent(binding))
	{
		if (reason_out)
			*reason_out = result.reason != SG_DOOR_APPROACH_REASON_NONE
			    ? result.reason : SG_DOOR_APPROACH_REASON_SUPPORT;
		goto done;
	}
	prediction->pms = ph.pms;
	VectorCopy(ph.mins, prediction->mins);
	VectorCopy(ph.maxs, prediction->maxs);
	prediction->groundentity = ph.groundentity_entity;
	prediction->watertype = ph.watertype;
	prediction->waterlevel = ph.waterlevel;
	prediction->expected_touch = sg_oracle_declared_touched;
	if (reason_out)
		*reason_out = SG_DOOR_APPROACH_REASON_NONE;
	ok = true;

done:
	sg_oracle_passent = old_passent;
	sg_oracle_world_only = old_world;
	sg_oracle_contaminated = old_contaminated;
	sg_oracle_declared_expected = old_expected;
	sg_oracle_declared_door = old_door;
	sg_oracle_bound_door = old_bound;
	sg_oracle_declared_action = old_action;
	sg_oracle_declared_touched = old_touched;
	return ok;
}

/* Re-prove the remaining TOP-pose egress from the exact authoritative state
 * at a production 100 ms boundary. Unlike the nominal generator proof this
 * may begin inside the mover sweep: the validated trigger set is already
 * linked at TOP and remains physical, while its synthetic union is used only
 * as the required terminal escape condition. No mover/entity loop interleaves
 * the four ClientThink commands that consume this grant. */
static qboolean SG_OracleDoorContinue(edict_t *ent, const vec3_t target,
	edict_t *trigger, const sg_rune_mechanism_binding_t *binding,
	int *arrival_ms)
{
	edict_t *old_passent = sg_oracle_passent;
	edict_t *old_expected = sg_oracle_declared_expected;
	edict_t *old_door = sg_oracle_declared_door;
	const sg_rune_mechanism_binding_t *old_bound = sg_oracle_bound_door;
	qboolean old_world = sg_oracle_world_only;
	qboolean old_contaminated = sg_oracle_contaminated;
	qboolean old_touched = sg_oracle_declared_touched;
	int old_action = sg_oracle_declared_action;
	sg_phantom_t ph;
	usercmd_t cmd;
	qboolean ok = false;
	qboolean button_controller;
	int controller_kind;
	float old_frame_z;
	int elapsed, axis;

	controller_kind = binding && binding->plan
	    ? binding->plan->controller_kind : SG_MECHANISM_CONTROLLER_NONE;
	button_controller = controller_kind ==
	    SG_MECHANISM_CONTROLLER_BUTTON_DOOR;
	if (!ent || !ent->inuse || !ent->client || !target || !trigger ||
	    !arrival_ms || !sv_gravity || ent->health <= 0 || ent->deadflag ||
	    ent->movetype != MOVETYPE_WALK || ent->s.modelindex != 255 ||
	    ent->client->ps.pmove.pm_type != PM_NORMAL ||
	    (ent->client->ps.pmove.pm_flags & PMF_DUCKED) ||
	    (ent->client->ps.pmove.pm_flags &
	        (PMF_TIME_WATERJUMP | PMF_TIME_TELEPORT)) ||
	    (ent->client->ps.pmove.pm_time &&
	     !(ent->client->ps.pmove.pm_flags & PMF_TIME_LAND)) ||
	    ent->client->hookstate != 0 || ent->client->hook != NULL ||
	    !SG_OracleDoorEgressWaterSafe(controller_kind, ent->waterlevel,
	        ent->watertype) ||
	    (binding && (!SG_OracleBoundDoorBindingCurrent(binding) ||
	                 binding->entry_entity != trigger)) ||
	    !(binding ? SG_BoundDoorAtTop(binding) :
	                 SG_DeclaredDoorAtTop(trigger)))
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
	sg_oracle_bound_door = binding;
	sg_oracle_declared_action = button_controller ? RL_BUTTON_DOOR : RL_DOOR;
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
		if (sg_oracle_contaminated ||
		    !SG_OracleDoorEgressWaterSafe(controller_kind, ph.waterlevel,
		        ph.watertype))
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
			if ((binding ? SG_BoundDoorOutsideSweep(binding, ph.origin) :
			               SG_DeclaredDoorOutsideSweep(trigger, ph.origin)) &&
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
	sg_oracle_bound_door = old_bound;
	sg_oracle_declared_action = old_action;
	sg_oracle_declared_touched = old_touched;
	return ok;
}

qboolean SG_OracleDeclaredDoorContinue(edict_t *ent, const vec3_t target,
	edict_t *trigger, int *arrival_ms)
{
	return SG_OracleDoorContinue(ent, target, trigger, NULL, arrival_ms);
}

qboolean SG_OracleBoundDoorContinue(edict_t *ent, const vec3_t target,
	const sg_rune_mechanism_binding_t *binding, int *arrival_ms)
{
	return binding ? SG_OracleDoorContinue(ent, target,
	    binding->entry_entity, binding, arrival_ms) : false;
}

/* Runtime completion for a declared trigger set. door_hit_top publishes
 * STATE_TOP only after the mover reached its authoritative end pose, so this
 * predicate is also the handoff from the motion wait to the proved egress.
 * Re-resolve every direct target: stale/scripted map state fails closed. */
qboolean SG_DeclaredDoorAtTopFor(edict_t *trigger, int window_ms)
{
	edict_t *members[SG_PHANTOM_ARMED_DOORS];
	int count, i;
	float until;

	if (!SG_OracleDeclaredActivatorSafe(trigger) || window_ms < 0 ||
	    !isfinite(level.time))
		return false;
	until = level.time + (float)window_ms * 0.001f + FRAMETIME;
	if (!isfinite(until))
		return false;
	count = SG_DeclaredDoorMembers(trigger, members,
	    SG_PHANTOM_ARMED_DOORS);
	if (count <= 0)
		return false;
	for (i = 0; i < count; i++)
	{
		edict_t *member = members[i];

		if (!member->inuse || member->moveinfo.state != SG_PLAT_STATE_TOP ||
		    !SG_MoverCompletionMatches(member, SG_MOVER_COMPLETION_TOP))
			return false;
		if (member->moveinfo.wait >= 0.0f &&
		    (member->think != door_go_down ||
		     !isfinite(member->nextthink) || member->nextthink <= until))
			return false;
	}
	return true;
}

qboolean SG_DeclaredDoorAtTop(edict_t *trigger)
{
	return SG_DeclaredDoorAtTopFor(trigger, 0);
}

qboolean SG_BoundDoorAtTopFor(
	const sg_rune_mechanism_binding_t *binding, int window_ms)
{
	edict_t *members[SG_RUNE_BINDING_MAX_MOVERS];
	float until;
	int count;
	int index;

	if (window_ms < 0 || !isfinite(level.time) ||
	    (count = SG_BoundDoorMembers(binding, members,
	        SG_RUNE_BINDING_MAX_MOVERS)) <= 0)
		return false;
	until = level.time + (float)window_ms * 0.001f + FRAMETIME;
	if (!isfinite(until))
		return false;
	for (index = 0; index < count; index++)
	{
		edict_t *member = members[index];

		if (member->moveinfo.state != SG_PLAT_STATE_TOP ||
		    !SG_MoverCompletionMatches(member, SG_MOVER_COMPLETION_TOP))
			return false;
		if (member->moveinfo.wait >= 0.0f &&
		    (member->think != door_go_down || !isfinite(member->nextthink) ||
		     member->nextthink <= until))
			return false;
	}
	return SG_OracleBoundDoorBindingCurrent(binding);
}

qboolean SG_BoundDoorAtTop(
	const sg_rune_mechanism_binding_t *binding)
{
	return SG_BoundDoorAtTopFor(binding, 0);
}

/* A combat body can transiently block both proved exits after the bot has
 * entered a CRUSHER sweep. There is no physical controller that guarantees an
 * escape through an occupied corridor, so keep the exact validated set at TOP
 * in short leases until a fresh suffix or retreat proof succeeds. This changes
 * no pose and never opens a door; it only postpones an already scheduled close
 * while a declared client is inside its occupied volume. */
static qboolean SG_DeclaredDoorMembersExact(edict_t *const *members,
	int count)
{
	int i, j;

	if (!members || count <= 0 || count > SG_PHANTOM_ARMED_DOORS)
		return false;
	for (i = 0; i < count; i++)
	{
		edict_t *member = members[i];
		edict_t *master, *team_member;
		int team_count = 0;

		if (!member || !SG_OracleMoverSweepIdentity(member))
			return false;
		for (j = 0; j < i; j++)
			if (members[j] == member)
				return false;
		/* The captured-key fallback may outlive its activator.  Prove that the
		 * supplied union is still closed over every current teamchain before
		 * validating or renewing any member; otherwise a newly appended teammate
		 * would receive a split timer mutation. */
		master = member->teammaster ? member->teammaster : member;
		for (team_member = master; team_member && team_count <= count;
		     team_member = team_member->teamchain, team_count++)
		{
			/* Prove array membership and linked mover shape before reading this
			 * possibly drifted team pointer or its next link. */
			if (!SG_OracleMoverSweepIdentity(team_member))
				return false;
			for (j = 0; j < count; j++)
				if (members[j] == team_member)
					break;
			if (j == count)
				return false;
		}
		if (team_member || team_count <= 0 || team_count > count ||
		    !SG_OracleDeclaredDoorTeamSafe(member))
			return false;
	}
	return true;
}

qboolean SG_DeclaredDoorHoldMembers(edict_t *const *members, int count,
	int lease_ms)
{
	int i;
	float until;

	if (!SG_DeclaredDoorMembersExact(members, count) || lease_ms < 100 ||
	    lease_ms > 1000 || !isfinite(level.time))
		return false;
	until = level.time + lease_ms * 0.001f;
	if (!isfinite(until))
		return false;
	/* Validate the complete set before changing any timer.  A partial lease on
	 * a drifted multi-door mechanism is itself a world mutation and can split
	 * members that the declared record treats as one atomic set. */
	for (i = 0; i < count; i++)
	{
		edict_t *member = members[i];

		if (member->moveinfo.state != SG_PLAT_STATE_TOP ||
		    !SG_MoverCompletionMatches(member,
		                               SG_MOVER_COMPLETION_TOP))
			return false;
		if (member->moveinfo.wait >= 0.0f &&
		    (member->think != door_go_down ||
		     !isfinite(member->nextthink)))
			return false;
	}
	for (i = 0; i < count; i++)
		if (members[i]->moveinfo.wait >= 0.0f &&
		    members[i]->nextthink < until)
			members[i]->nextthink = until;
	return true;
}

/* A release fence outlives its logical owner.  Quantized translating doors can
 * accumulate an unbounded nominal-endpoint residual across repeated cycles;
 * mutable callback/state fields therefore cannot prove physical completion.
 * Require the out-of-edict snapshot published by the real stock callback.
 * An exact initial BOTTOM is the sole no-witness exception, and only before
 * any observed movement transition. */
qboolean SG_DeclaredDoorMembersTerminal(edict_t *const *members, int count)
{
	int i;

	if (!SG_DeclaredDoorMembersExact(members, count))
		return false;
	for (i = 0; i < count; i++)
	{
		edict_t *member = members[i];
		qboolean at_bottom;
		qboolean at_permanent_top;

		if (!VectorCompare(member->velocity, vec3_origin) ||
		    !VectorCompare(member->avelocity, vec3_origin) ||
		    member->nextthink != 0.0f)
			return false;
		at_bottom = member->moveinfo.state == SG_PLAT_STATE_BOTTOM &&
		    ((member->moveinfo.endfunc == door_hit_bottom &&
		      SG_MoverCompletionMatches(member,
		                                SG_MOVER_COMPLETION_BOTTOM)) ||
		     (!member->moveinfo.endfunc &&
		      SG_MoverCompletionUntouched(member) &&
		      VectorCompare(member->s.origin,
		          member->moveinfo.start_origin) &&
		      VectorCompare(member->s.angles,
		          member->moveinfo.start_angles)));
		at_permanent_top = member->moveinfo.state == SG_PLAT_STATE_TOP &&
		    member->moveinfo.wait < 0.0f &&
		    member->moveinfo.endfunc == door_hit_top &&
		    SG_MoverCompletionMatches(member, SG_MOVER_COMPLETION_TOP);
		if (!at_bottom && !at_permanent_top)
			return false;
	}
	return true;
}

qboolean SG_DeclaredDoorHoldOpen(edict_t *trigger, int lease_ms)
{
	edict_t *members[SG_PHANTOM_ARMED_DOORS];
	int count;

	if (!SG_OracleDeclaredActivatorSafe(trigger))
		return false;
	count = SG_DeclaredDoorMembers(trigger, members,
	    SG_PHANTOM_ARMED_DOORS);
	return SG_DeclaredDoorHoldMembers(members, count, lease_ms);
}

static int SG_DeclaredDoorMembersAddTeam(edict_t *target,
	edict_t **members, int capacity, int count)
{
	edict_t *master, *member;
	int i;

	if (!target || !members || capacity <= 0)
		return -1;
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
	return count;
}

/* Expand one declared activator to the exact unique member list used by the
 * generator's temporary TOP pose.  A trigger may name several independent
 * masters, while G_Find may also encounter more than one member of a team;
 * deduplicate the physical brushes rather than charging or relinking them
 * twice. */
static int SG_DeclaredDoorMembersResolved(edict_t *trigger,
	edict_t **members, int capacity)
{
	edict_t *target = NULL;
	int count = 0;

	if (!trigger || !members || capacity <= 0)
		return -1;
	if (trigger->touch == Touch_DoorTrigger)
	{
		if (!trigger->owner)
			return -1;
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
	if (!trigger->classname ||
	    (strcmp(trigger->classname, "trigger_multiple") != 0 &&
	     strcmp(trigger->classname, "func_button") != 0) ||
	    !trigger->target || !trigger->target[0])
		return -1;
	while ((target = G_Find(target, FOFS(targetname), trigger->target)) != NULL)
	{
		if (target->classname &&
		    !Q_stricmp(target->classname, "trigger_relay") &&
		    !strcmp(trigger->classname, "trigger_multiple") &&
		    SG_OracleDeclaredRelayDoorTargetsSafe(target))
		{
			edict_t *relay_target = NULL;

			while ((relay_target = G_Find(relay_target, FOFS(targetname),
			                        target->target)) != NULL)
			{
				if (!relay_target->classname ||
				    strncmp(relay_target->classname, "func_door", 9) != 0)
					continue;
				count = SG_DeclaredDoorMembersAddTeam(relay_target,
				    members, capacity, count);
				if (count < 0)
					return -1;
			}
			continue;
		}
		if (!target->classname ||
		    strncmp(target->classname, "func_door", 9) != 0)
			continue;
		count = SG_DeclaredDoorMembersAddTeam(target, members, capacity,
		    count);
		if (count < 0)
			return -1;
	}
	return count;
}

static int SG_DeclaredDoorMembersInternal(edict_t *trigger,
	edict_t **members, int capacity, qboolean delayed)
{
	if (!(delayed ? SG_OracleDeclaredActivatorSafeWithDelay(trigger, true) :
	        SG_OracleDeclaredDoorSourceSafe(trigger)))
		return -1;
	return SG_DeclaredDoorMembersResolved(trigger, members, capacity);
}

int SG_DeclaredDoorMembers(edict_t *trigger, edict_t **members,
	int capacity)
{
	return SG_DeclaredDoorMembersInternal(trigger, members, capacity, false);
}

int SG_DeclaredDelayedDoorMembers(edict_t *trigger, edict_t **members,
	int capacity)
{
	return SG_DeclaredDoorMembersInternal(trigger, members, capacity, true);
}

/* One authoritative timing contract shared by generation and loading.  The
 * latest handoff is bounded by both approach capture and the slowest member;
 * each member's TOP hold is then charged only for the skew after that member
 * can first arrive.  This matters for a single short-hold automatic door: its
 * wait starts at TOP, not when the player first touches its trigger. */
static int SG_DoorContractCost(edict_t *trigger,
	const sg_rune_mechanism_binding_t *binding, int approach_ms,
	int touch_ms, int egress_ms)
{
	edict_t *members[SG_PHANTOM_ARMED_DOORS];
	int64_t earliest[SG_PHANTOM_ARMED_DOORS];
	int64_t hold_floor[SG_PHANTOM_ARMED_DOORS];
	int count, i, cyclic = 0;
	int64_t longest = 0, longest_cycle = 0;
	int64_t post_touch_ms, handoff_ms;
	int64_t trigger_ms, cooldown_gap, total;
	int64_t button_press_ms = 0;
	qboolean button_controller;

	if (approach_ms <= 0 || touch_ms <= 0 || touch_ms > approach_ms ||
	    egress_ms <= 0 || !trigger)
		return -1;
	count = binding
	    ? SG_BoundDoorMembers(binding, members, SG_PHANTOM_ARMED_DOORS)
	    : SG_DeclaredDoorMembers(trigger, members, SG_PHANTOM_ARMED_DOORS);
	if (count <= 0)
		return -1;
	/* DIRECT_TRIGGER_DOOR saturates the redundant plan cooldown field to the
	 * 30-second wire bound.  The sealed entry node still carries the exact
	 * mapper wait, and live binding revalidates that node before this cost is
	 * trusted.  Use the exact value here so 63/304/312-second rearm gaps have
	 * identical generation and load-time arithmetic. */
	trigger_ms = binding && binding->plan->controller_kind ==
	        SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR
	    ? binding->entry_node->wait_ms
	    : binding ? (int)binding->plan->cooldown_ms
	              : SG_DeclaredDoorTriggerWaitMs(trigger);
	if (trigger_ms <= 0)
		return -1;
	button_controller = binding
	    ? binding->plan->controller_kind == SG_MECHANISM_CONTROLLER_BUTTON_DOOR
	    : SG_OracleDeclaredButtonDoorSafe(trigger);
	if (button_controller)
	{
		vec3_t travel;
		double nominal;

		/* SP_func_button stores its immutable endpoints but never initializes
		 * moveinfo.distance. Derive the stock translation from those endpoints;
		 * thin floor plates can legitimately travel only a few units. */
		VectorSubtract(trigger->moveinfo.end_origin,
		               trigger->moveinfo.start_origin, travel);
		nominal = (double)VectorLength(travel) /
		          (double)trigger->moveinfo.speed * 1000.0;

		if (!isfinite(nominal) || nominal <= 0.0 || nominal > 12500.0)
			return -1;
		button_press_ms = (int64_t)ceil(nominal) + 200;
	}
	for (i = 0; i < count; i++)
	{
		edict_t *member = members[i];
		int64_t travel, hold, cycle;
		double hold_ms, nominal;

		if (!isfinite(member->moveinfo.distance) ||
		    !isfinite(member->moveinfo.speed) ||
		    member->moveinfo.speed <= 0.0f ||
		    !isfinite(member->moveinfo.wait))
			return -1;
		nominal = fabs((double)member->moveinfo.distance) /
		          (double)member->moveinfo.speed * 1000.0;
		/* The contract already rejects a travel above 12.5 seconds.  Apply
		 * that bound before converting so an extreme finite map value can
		 * never reach an out-of-range integer cast. */
		if (!isfinite(nominal) || nominal <= 0.0 || nominal > 12500.0)
			return -1;
		earliest[i] = (int64_t)floor(nominal);
		travel = (int64_t)ceil(nominal) + 200;
		if (travel > 12500)
			return -1;
		if (travel > longest)
			longest = travel;
		hold_floor[i] = -1;
		if (member->moveinfo.wait < 0.0f)
			continue;
		hold_ms = (double)member->moveinfo.wait * 1000.0;
		if (!isfinite(hold_ms) || hold_ms < 0.0 ||
		    hold_ms >= (double)INT64_MAX)
			return -1;
		hold_floor[i] = (int64_t)floor(hold_ms);
		hold = (int64_t)ceil(hold_ms);
		if (hold > INT64_MAX - 2 * travel)
			return -1;
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
	post_touch_ms = (int64_t)approach_ms - (int64_t)touch_ms;
	handoff_ms = post_touch_ms > button_press_ms + longest
	    ? post_touch_ms : button_press_ms + longest;
	/* func_button fires its targets only after reaching TOP, then begins its
	 * own TOP hold.  The synthetic simultaneous TOP witness is live-reachable
	 * only when that entry brush remains at the sealed endpoint through the
	 * slowest target-door opening, complete egress, and one scheduling margin. */
	if (button_controller &&
	    button_press_ms + trigger_ms <
	        handoff_ms + (int64_t)egress_ms + 300)
		return -1;
	for (i = 0; i < count; i++)
		if (hold_floor[i] >= 0 &&
		    button_press_ms + earliest[i] + hold_floor[i] <
		        handoff_ms + (int64_t)egress_ms + 300)
			return -1;
	cooldown_gap = cyclic && trigger_ms > longest_cycle
	    ? trigger_ms - longest_cycle : 0;
	total = (int64_t)approach_ms + button_press_ms + 2 * longest +
	        cooldown_gap + (int64_t)egress_ms + 1000;
	if (total <= 0 || total > 30000)
		return -1;
	return (int)total;
}

int SG_DeclaredDoorContractCost(edict_t *trigger, int approach_ms,
	int touch_ms, int egress_ms)
{
	return SG_DoorContractCost(trigger, NULL, approach_ms, touch_ms,
	    egress_ms);
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

static qboolean SG_OracleRelayDoorTargets(edict_t *relay,
	float trigger_wait, edict_t **doors, int *num_doors)
{
	edict_t *target = NULL;
	qboolean found = false;

	if (!relay || !doors || !num_doors || !relay->inuse ||
	    !relay->classname || Q_stricmp(relay->classname, "trigger_relay") ||
	    relay->use != trigger_relay_use || !isfinite(relay->delay) ||
	    relay->delay != 0.0f || relay->killtarget || relay->pathtarget ||
	    relay->message || !relay->target || !relay->target[0])
		return false;
	while ((target = G_Find(target, FOFS(targetname), relay->target)) != NULL)
	{
		edict_t *master;
		int i;

		if (!target->inuse || !target->classname)
			return false;
		if (strncmp(target->classname, "func_door", 9) == 0)
		{
			master = target->teammaster ? target->teammaster : target;
			if ((target->flags & FL_TEAMSLAVE) ||
			    !SG_OracleDoorCooldownSafe(master, trigger_wait))
				return false;
			for (i = 0; i < *num_doors; i++)
				if (doors[i] == master)
					return false;
			if (*num_doors >= SG_PHANTOM_ARMED_DOORS)
				return false;
			doors[(*num_doors)++] = master;
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

/* A static proof may ignore a Touch_Multi only when a live player can fire it
 * on every visit and its complete target set is safe doors plus sound.  One
 * synchronous relay hop is admissible because both edges are encoded in the
 * rune link; all other scripted indirection fails closed. */
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
		    target->use == trigger_relay_use)
		{
			if (SG_OracleSoundOnlyTargets(target, 1))
				continue;
			if (SG_OracleRelayDoorTargets(target, trigger->wait, doors,
			        &num_doors))
				continue;
		}
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

	if (!sg_host.box_edicts ||
	    (!sg_oracle_world_only && !sg_oracle_compound_trigger))
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
		if (sg_oracle_compound_trigger)
		{
			if (hit != sg_oracle_compound_trigger)
			{
				if (sg_oracle_world_only)
					return true;
				continue;
			}
			sg_oracle_compound_touched = true;
			continue;
		}
		if (SG_OracleDeclaredTrigger(hit))
		{
			sg_oracle_declared_touched = true;
			continue;
		}
		if (sg_oracle_declared_door &&
		    (sg_oracle_bound_door
		         ? ((sg_oracle_declared_action == RL_DOOR ||
		             sg_oracle_declared_action == RL_BUTTON_DOOR) &&
		            SG_OracleBoundSameDoorSet(sg_oracle_bound_door, hit))
		         : SG_OracleDeclaredApproachTriggerAllowed(
		               sg_oracle_declared_action, sg_oracle_declared_door, hit)))
			continue;
		/* A declared door approach owns exactly one scripted touch. Do not let
		 * an unrelated auto-door or trigger_multiple become an unrecorded second
		 * mechanism merely because its volume overlaps this short rollout. */
		if (sg_oracle_declared_action == RL_DOOR ||
		    sg_oracle_declared_action == RL_BUTTON_DOOR)
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
	vec3_t hull_mins = { -16.0f, -16.0f, -24.0f };
	vec3_t hull_maxs = { 16.0f, 16.0f, 32.0f };
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
		if (SG_ImmutableSupport(hit) || hit == sg_oracle_compound_member ||
		    hit == sg_oracle_declared_expected ||
		    ((sg_oracle_declared_action == RL_BUTTON_DOOR ||
		      sg_oracle_declared_action == RL_TRAIN) &&
		     hit == sg_oracle_declared_door) ||
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
		/* SV_LinkEdict publishes a rotating BSP through its radius cube.  Use
		 * the authoritative full-motion sweep before treating that coarse box
		 * as occupied; malformed geometry still fails closed in the sweep. */
		if (hit->solid == SOLID_BSP && hit->classname &&
		    !strcmp(hit->classname, "func_rotating") &&
		    !SG_OracleRotatorEntitySweepBlocks(hit, ph->origin, hull_mins,
		        hull_maxs, ph->origin, MASK_PLAYERSOLID))
			continue;
		if (SG_OracleRotatingDoorBoxOverlapIsCoarse(hit, ph))
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
	trace_t tr;
	qboolean population_stable;

	population_stable = SG_OracleStablePopulationTrace(start, mins, maxs,
	    end, sg_oracle_passent, sg_oracle_loader_replay, &tr);
	if (sg_oracle_world_only && !population_stable)
		sg_oracle_contaminated = true;

	if (sg_oracle_world_only &&
	    (sg_oracle_declared_action == RL_BUTTON_DOOR ||
	     sg_oracle_declared_action == RL_TRAIN) &&
	    tr.ent == sg_oracle_declared_expected &&
	    (tr.startsolid || tr.allsolid || tr.fraction < 1.0f))
		sg_oracle_declared_touched = true;
	if (sg_oracle_world_only && !SG_ImmutableSupport(tr.ent) &&
	    tr.ent != sg_oracle_compound_member &&
	    tr.ent != sg_oracle_declared_expected &&
	    !((sg_oracle_declared_action == RL_BUTTON_DOOR ||
	       sg_oracle_declared_action == RL_TRAIN) &&
	      tr.ent == sg_oracle_declared_door) &&
	    !SG_OracleDeclaredSetMember(sg_oracle_declared_door, tr.ent) &&
	    (tr.startsolid || tr.allsolid || tr.fraction < 1.0f) &&
	    tr.ent && tr.ent != g_edicts)
		sg_oracle_contaminated = true;
	/* Pmove probes both the direct and step-up branches before selecting one.
	 * An unarmed synthetic door sweep is collision authority for either probe,
	 * not evidence that the body used that branch.  Return a fail-closed solid
	 * trace so Pmove can discard the blocked candidate without poisoning the
	 * accepted one.  Limit the synthetic query to the native trace endpoint: a
	 * door behind an earlier world collision was never reached. */
	if (sg_oracle_world_only && !tr.startsolid && !tr.allsolid &&
	    SG_OracleDoorTraceBlocked(sg_oracle_active_phantom,
	                              start, mins, maxs, tr.endpos, false))
	{
		tr.startsolid = true;
		tr.allsolid = true;
		tr.fraction = 0.0f;
		VectorCopy(start, tr.endpos);
		tr.ent = g_edicts;
		tr.contents = CONTENTS_SOLID;
	}
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
	if (SG_OracleDoorTraceBlocked(NULL, muzzle, bolt_mins, bolt_maxs, bite,
	                              false))
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
	if (!SG_OracleStablePopulationTraceMask(from, NULL, NULL, to, passent,
	        MASK_PLAYERSOLID, sg_oracle_loader_replay, &tr))
		return false;
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

/* The public traversal and compound PREOPEN replay consume the same literal
 * one-command SWIM adapter.  A cursor owns reducer state and decoded pose, but
 * deliberately owns no oracle globals: its caller establishes exactly one
 * collision/trigger scope around the complete transaction. */
typedef struct sg_oracle_swim_cursor_s
{
	sg_swim_replay_state_t state;
	sg_replay_pose_t pose;
	sg_replay_observation_t observation;
	edict_t *passent;
	qboolean suppress_arrival;
} sg_oracle_swim_cursor_t;

static sg_replay_status_t SG_OracleSwimCursorBegin(
	sg_oracle_swim_cursor_t *cursor, const sg_phantom_t *ph,
	const vec3_t destination, qboolean destination_water,
	float old_frame_z, edict_t *passent, qboolean suppress_arrival)
{
	sg_swim_replay_spec_t spec;

	if (!cursor)
		return SG_REPLAY_FAILED;
	memset(cursor, 0, sizeof(*cursor));
	cursor->passent = passent;
	cursor->suppress_arrival = suppress_arrival;
	memset(&spec, 0, sizeof(spec));
	VectorCopy(destination, spec.destination);
	spec.destination_water = destination_water;
	spec.expected_arrival_ms = SG_REPLAY_TIME_DISCOVER;
	SG_OracleReplayPose(ph, &cursor->pose);
	SG_OracleSwimObservation(ph, destination, destination_water,
		!cursor->suppress_arrival,
		passent, &cursor->observation);
	if (cursor->suppress_arrival)
		cursor->observation.contact_clear = false;
	return SG_SwimReplayBegin(&cursor->state, &spec, &cursor->pose,
		&cursor->observation, old_frame_z);
}

static sg_replay_status_t SG_OracleSwimCursorStep(
	sg_oracle_swim_cursor_t *cursor, sg_phantom_t *ph)
{
	usercmd_t command;
	sg_replay_status_t status;

	if (!cursor || !ph)
		return SG_REPLAY_FAILED;
	status = SG_SwimReplayPreStep(&cursor->state, &cursor->pose, &command);
	if (status != SG_REPLAY_RUNNING)
		return status;
	SG_OracleRun(ph, &command, 1);
	SG_OracleReplayPose(ph, &cursor->pose);
	SG_OracleSwimObservation(ph, cursor->state.spec.destination,
		cursor->state.spec.destination_water,
		!cursor->suppress_arrival &&
		cursor->state.progress.elapsed_ms + SG_REPLAY_STEP_MS <
		    SG_REPLAY_SWIM_LIMIT_MS &&
		((cursor->state.progress.elapsed_ms + SG_REPLAY_STEP_MS) %
		 SG_REPLAY_FRAME_MS) == 0,
		cursor->passent, &cursor->observation);
	if (cursor->suppress_arrival)
		cursor->observation.contact_clear = false;
	return SG_SwimReplayPostStep(&cursor->state, &cursor->pose,
		&cursor->observation);
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
	sg_oracle_swim_cursor_t cursor;
	sg_replay_status_t status;
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

	status = SG_OracleSwimCursorBegin(&cursor, ph, destination,
		destination_water, old_frame_z, passent, false);
	while (status == SG_REPLAY_RUNNING)
		status = SG_OracleSwimCursorStep(&cursor, ph);
	if (status == SG_REPLAY_ARRIVED ||
	    (status == SG_REPLAY_FAILED &&
	     cursor.state.progress.reason == SG_REPLAY_REASON_DOOR_PASSED &&
	     cursor.state.progress.arrival_ms != SG_REPLAY_TIME_DISCOVER))
	{
		proof->arrival_ms = cursor.state.progress.arrival_ms;
		proof->exit_speed = cursor.state.progress.exit_speed;
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

typedef struct sg_oracle_compound_scope_s
{
	edict_t *passent;
	sg_phantom_t *active_phantom;
	qboolean world_only;
	qboolean contaminated;
	edict_t *declared_expected;
	edict_t *declared_door;
	int declared_action;
	qboolean declared_touched;
	qboolean loader_replay;
	edict_t *compound_trigger;
	edict_t *compound_member;
	qboolean compound_touched;
} sg_oracle_compound_scope_t;

typedef struct sg_oracle_member_snapshot_s
{
	vec3_t origin;
	vec3_t old_origin;
	vec3_t absmin;
	vec3_t absmax;
	vec3_t size;
	vec3_t velocity;
	vec3_t avelocity;
	void (*endfunc)(edict_t *);
	void (*think)(edict_t *);
	float nextthink;
	int state;
	int number;
	solid_t solid;
	int linkcount;
} sg_oracle_member_snapshot_t;

static void SG_OracleCompoundScopeEnter(sg_oracle_compound_scope_t *scope,
	edict_t *trigger, edict_t *member, edict_t *passent,
	qboolean world_only, qboolean loader_replay)
{
	scope->passent = sg_oracle_passent;
	scope->active_phantom = sg_oracle_active_phantom;
	scope->world_only = sg_oracle_world_only;
	scope->contaminated = sg_oracle_contaminated;
	scope->declared_expected = sg_oracle_declared_expected;
	scope->declared_door = sg_oracle_declared_door;
	scope->declared_action = sg_oracle_declared_action;
	scope->declared_touched = sg_oracle_declared_touched;
	scope->loader_replay = sg_oracle_loader_replay;
	scope->compound_trigger = sg_oracle_compound_trigger;
	scope->compound_member = sg_oracle_compound_member;
	scope->compound_touched = sg_oracle_compound_touched;

	sg_oracle_passent = passent;
	sg_oracle_active_phantom = NULL;
	sg_oracle_world_only = world_only;
	sg_oracle_contaminated = false;
	sg_oracle_declared_expected = NULL;
	sg_oracle_declared_door = NULL;
	sg_oracle_declared_action = 0;
	sg_oracle_declared_touched = false;
	sg_oracle_loader_replay = loader_replay;
	sg_oracle_compound_trigger = trigger;
	sg_oracle_compound_member = member;
	sg_oracle_compound_touched = false;
}

static void SG_OracleCompoundScopeRestore(
	const sg_oracle_compound_scope_t *scope)
{
	sg_oracle_passent = scope->passent;
	sg_oracle_active_phantom = scope->active_phantom;
	sg_oracle_world_only = scope->world_only;
	sg_oracle_contaminated = scope->contaminated;
	sg_oracle_declared_expected = scope->declared_expected;
	sg_oracle_declared_door = scope->declared_door;
	sg_oracle_declared_action = scope->declared_action;
	sg_oracle_declared_touched = scope->declared_touched;
	sg_oracle_loader_replay = scope->loader_replay;
	sg_oracle_compound_trigger = scope->compound_trigger;
	sg_oracle_compound_member = scope->compound_member;
	sg_oracle_compound_touched = scope->compound_touched;
}

qboolean SG_OracleRunCompoundWorld(sg_phantom_t *ph, usercmd_t *cmd,
	int steps, edict_t *trigger, edict_t *member)
{
	sg_oracle_compound_scope_t scope;
	qboolean clean = false;

	if (!ph || !cmd || steps <= 0 || !trigger || !trigger->inuse ||
	    !member || !member->inuse || !sg_host.box_edicts)
		return false;
	SG_OracleCompoundScopeEnter(&scope, trigger, member, NULL, true, false);
	if (!SG_OracleTriggerOverlap(ph) && !SG_OracleSolidOverlap(ph))
	{
		SG_OracleRun(ph, cmd, steps);
		clean = !sg_oracle_contaminated &&
		        !SG_OracleTriggerOverlap(ph) &&
		        !SG_OracleSolidOverlap(ph) &&
		        !ph->door_passed && !ph->door_arm_overflow;
	}
	SG_OracleCompoundScopeRestore(&scope);
	return clean;
}

static void SG_OracleMemberSnapshot(edict_t *member,
	sg_oracle_member_snapshot_t *snapshot)
{
	VectorCopy(member->s.origin, snapshot->origin);
	VectorCopy(member->s.old_origin, snapshot->old_origin);
	VectorCopy(member->absmin, snapshot->absmin);
	VectorCopy(member->absmax, snapshot->absmax);
	VectorCopy(member->size, snapshot->size);
	VectorCopy(member->velocity, snapshot->velocity);
	VectorCopy(member->avelocity, snapshot->avelocity);
	snapshot->endfunc = member->moveinfo.endfunc;
	snapshot->think = member->think;
	snapshot->nextthink = member->nextthink;
	snapshot->state = member->moveinfo.state;
	snapshot->number = member->s.number;
	snapshot->solid = member->solid;
	snapshot->linkcount = member->linkcount;
}

static void SG_OracleMemberRestore(edict_t *member,
	const sg_oracle_member_snapshot_t *snapshot)
{
	VectorCopy(snapshot->origin, member->s.origin);
	VectorCopy(snapshot->old_origin, member->s.old_origin);
	VectorCopy(snapshot->velocity, member->velocity);
	VectorCopy(snapshot->avelocity, member->avelocity);
	member->moveinfo.endfunc = snapshot->endfunc;
	member->think = snapshot->think;
	member->nextthink = snapshot->nextthink;
	member->moveinfo.state = snapshot->state;
	member->s.number = snapshot->number;
	member->solid = snapshot->solid;
	sg_host.linkentity(member);
	/* The host increments linkcount and may round its cached bounds.  Restore
	 * the caller-visible snapshot after relinking at the original pose. */
	VectorCopy(snapshot->origin, member->s.origin);
	VectorCopy(snapshot->old_origin, member->s.old_origin);
	member->solid = snapshot->solid;
	member->linkcount = snapshot->linkcount;
	VectorCopy(snapshot->absmin, member->absmin);
	VectorCopy(snapshot->absmax, member->absmax);
	VectorCopy(snapshot->size, member->size);
}

static qboolean SG_OracleCompoundMemberAtBottom(edict_t *member,
	const sg_compound_world_preopen_t *resolved)
{
	return member && resolved && member->solid == SOLID_BSP &&
	       member->moveinfo.state == SG_PLAT_STATE_BOTTOM &&
	       member->nextthink == 0.0f &&
	       member->s.origin[0] == resolved->bottom_origin[0] &&
	       member->s.origin[1] == resolved->bottom_origin[1] &&
	       member->s.origin[2] == resolved->bottom_origin[2] &&
	       VectorCompare(member->velocity, vec3_origin) &&
	       VectorCompare(member->avelocity, vec3_origin);
}

static qboolean SG_OracleCompoundStageMember(edict_t *member,
	const sg_compound_world_preopen_t *resolved,
	const sg_compound_translate_step_t *step)
{
	edict_t *current = NULL;

	if (!step || !SG_CompoundWorldResolvedMember(resolved, &current) ||
	    current != member)
		return false;
	VectorCopy(member->s.origin, member->s.old_origin);
	VectorCopy(step->origin, member->s.origin);
	member->solid = SOLID_BSP;
	sg_host.linkentity(member);
	return true;
}

static qboolean SG_OracleCompoundStageMemberTop(edict_t *member,
	const sg_compound_world_preopen_t *resolved)
{
	if (!member || !resolved || !isfinite(level.time) ||
	    !isfinite(resolved->wait) || resolved->wait <= 0.0f)
		return false;
	VectorClear(member->velocity);
	VectorClear(member->avelocity);
	member->moveinfo.state = SG_PLAT_STATE_TOP;
	member->moveinfo.endfunc = door_hit_top;
	member->think = door_go_down;
	member->nextthink = level.time + resolved->wait;
	return isfinite(member->nextthink) &&
	       SG_CompoundWorldStagedAtTopFor(resolved, 0);
}

static qboolean SG_OracleCompoundPhantomClean(const sg_phantom_t *ph)
{
	return ph && !sg_oracle_contaminated && !ph->door_passed &&
	       !ph->door_arm_overflow;
}

static qboolean SG_OracleCompoundAnchorMatches(const sg_phantom_t *ph,
	const vec3_t anchor)
{
	int axis;

	if (!ph || !anchor)
		return false;
	for (axis = 0; axis < 3; axis++)
	{
		float scaled = anchor[axis] * 8.0f;

		if (!isfinite(scaled) || scaled < -32768.0f ||
		    scaled > 32767.0f || scaled != (float)(short)scaled ||
		    ph->pms.origin[axis] != (short)scaled ||
		    ph->origin[axis] != anchor[axis])
			return false;
	}
	return true;
}

static qboolean SG_OracleCompoundOutsideSegment(
	const sg_compound_world_preopen_t *resolved,
	const vec3_t from, const vec3_t to)
{
	return SG_CompoundWorldOutsideSweep(resolved, to) &&
	       !SG_CompoundWorldCrossesSweep(resolved, from, to);
}

static qboolean SG_OracleCompoundFrame(sg_phantom_t *ph,
	const sg_compound_world_preopen_t *resolved, const vec3_t anchor,
	const vec3_t view_angles, int commands, float *old_frame_z)
{
	sg_replay_pose_t pose;
	usercmd_t command;
	vec3_t before;
	int index;

	if (!ph || !resolved || (anchor && view_angles) ||
	    (!anchor && !view_angles) || !old_frame_z ||
	    commands <= 0 || commands > 4)
		return false;
	for (index = 0; index < commands; index++)
	{
		VectorCopy(ph->origin, before);
		SG_OracleReplayPose(ph, &pose);
		if ((view_angles && !SG_HookReplayFixedViewCommand(
		        &pose, view_angles, &command)) ||
		    (anchor && !SG_SwimReplayCommand(
		        &pose, anchor, SG_SWIM_REPLAY_HOLD, &command)))
			return false;
		SG_OracleRun(ph, &command, 1);
		if (!SG_OracleCompoundPhantomClean(ph) ||
		    !SG_OracleCompoundOutsideSegment(resolved, before, ph->origin))
			return false;
	}
	if (ph->waterlevel > 0 &&
	    (ph->watertype & (CONTENTS_LAVA | CONTENTS_SLIME)))
		return false;
	if (SG_ReplayFallDelta(*old_frame_z, ph->velocity[2],
	                       ph->groundentity, ph->waterlevel) >
	    SG_RUNE_PROOF_DAMAGING_FALL_DELTA)
		return false;
	*old_frame_z = ph->velocity[2];
	return true;
}

static void SG_OracleCompoundCaptureSuffix(const sg_phantom_t *ph,
	float old_frame_z, sg_compound_swim_proof_t *proof)
{
	proof->suffix_pms = ph->pms;
	proof->suffix_old_pms = ph->old_pms;
	VectorCopy(ph->origin, proof->suffix_origin);
	VectorCopy(ph->velocity, proof->suffix_velocity);
	proof->suffix_groundentity = ph->groundentity;
	proof->suffix_watertype = ph->watertype;
	proof->suffix_waterlevel = ph->waterlevel;
	proof->suffix_old_frame_z = old_frame_z;
}

static void SG_OracleCompoundDropCaptureSource(const sg_phantom_t *ph,
	float old_frame_z, sg_compound_drop_proof_t *proof)
{
	proof->source_pms = ph->pms;
	proof->source_old_pms = ph->old_pms;
	VectorCopy(ph->origin, proof->source_origin);
	VectorCopy(ph->velocity, proof->source_velocity);
	proof->source_groundentity = ph->groundentity;
	proof->source_watertype = ph->watertype;
	proof->source_waterlevel = ph->waterlevel;
	proof->source_old_frame_z = old_frame_z;
}

static void SG_OracleCompoundDropCaptureSuffix(const sg_phantom_t *ph,
	float old_frame_z, sg_compound_drop_proof_t *proof)
{
	proof->suffix_pms = ph->pms;
	proof->suffix_old_pms = ph->old_pms;
	VectorCopy(ph->origin, proof->suffix_origin);
	VectorCopy(ph->velocity, proof->suffix_velocity);
	proof->suffix_groundentity = ph->groundentity;
	proof->suffix_watertype = ph->watertype;
	proof->suffix_waterlevel = ph->waterlevel;
	proof->suffix_old_frame_z = old_frame_z;
}

static qboolean SG_OracleCompoundFixedVector(const vec3_t value)
{
	int axis;

	if (!value)
		return false;
	for (axis = 0; axis < 3; axis++)
	{
		float scaled = value[axis] * 8.0f;

		if (!isfinite(scaled) || scaled < -32768.0f ||
		    scaled > 32767.0f || scaled != (float)(short)scaled)
			return false;
	}
	return true;
}

static qboolean SG_OracleCompoundPreparedSourceValid(
	const sg_compound_swim_source_t *prepared)
{
	const sg_phantom_t *ph;
	int axis;

	if (!prepared || !isfinite(prepared->old_frame_z))
		return false;
	ph = &prepared->phantom;
	if (!SG_OracleCompoundAnchorMatches(ph, ph->origin) ||
	    (ph->groundentity != false && ph->groundentity != true) ||
	    ph->armed_door_count != 0 || ph->door_arm_overflow ||
	    ph->door_passed || ph->door_wait_ms != 0 ||
	    ph->door_open_ms != 0 ||
	    ph->waterlevel < 2 || ph->waterlevel > 3 ||
	    !(ph->watertype & CONTENTS_WATER) ||
	    (ph->watertype & (CONTENTS_LAVA | CONTENTS_SLIME)))
		return false;
	for (axis = 0; axis < 3; axis++)
		if (!isfinite(ph->velocity[axis]) ||
		    ph->velocity[axis] != ph->pms.velocity[axis] * 0.125f)
			return false;
	return true;
}

static rune_reject_reason_t SG_OracleCompoundDropFirstContact(
	const vec3_t source, const sg_compound_world_preopen_t *resolved,
	const vec3_t target, vec3_t contact_anchor, sg_phantom_t *contact_phantom,
	qboolean loader_replay)
{
	sg_oracle_compound_scope_t scope;
	sg_compound_world_preopen_t exact;
	sg_phantom_t ph;
	edict_t *member = NULL;
	rune_reject_reason_t reason = RLR_APPROACH_REPLAY_FAILED;
	qboolean scope_entered = false;
	usercmd_t command;
	int elapsed = 0;

	if (contact_anchor)
		VectorClear(contact_anchor);
	if (contact_phantom)
		memset(contact_phantom, 0, sizeof(*contact_phantom));
	if (!source || !resolved || !target || !contact_anchor ||
	    !SG_OracleCompoundFixedVector(source) ||
	    !SG_OracleCompoundFixedVector(target) ||
	    !sg_host.pmove || !sg_host.trace || !sg_host.pointcontents ||
	    !sg_host.box_edicts || sg_oracle_active_phantom ||
	    !SG_CompoundWorldResolvedMember(resolved, &member) ||
	    !resolved->trigger ||
	    !SG_OracleCompoundMemberAtBottom(member, resolved))
		return RLR_BAD_CONTROL_POLICY;

	SG_OraclePlace(&ph, (vec_t *)source);
	SG_OracleCompoundScopeEnter(&scope, resolved->trigger, member, NULL,
	                            true, loader_replay);
	scope_entered = true;
	memset(&command, 0, sizeof(command));
	command.msec = 0;
	SG_OracleRun(&ph, &command, 1);
	if (!SG_OracleCompoundPhantomClean(&ph) ||
	    !SG_OracleCompoundAnchorMatches(&ph, source) ||
	    !ph.groundentity || ph.waterlevel != 0 ||
	    !SG_CompoundWorldOutsideSweep(resolved, ph.origin) ||
	    SG_OracleTriggerOverlap(&ph) || sg_oracle_compound_touched ||
	    SG_OracleSolidOverlap(&ph))
		goto done;

	for (elapsed = 0; elapsed < SG_DOOR_APPROACH_LIMIT_MS;
	     elapsed += SG_REPLAY_STEP_MS)
	{
		vec3_t before, delta;
		float horizontal;

		memset(&command, 0, sizeof(command));
		command.msec = SG_REPLAY_STEP_MS;
		if (!SG_DeclaredCommand(ph.origin, (vec_t *)target, &ph.pms,
		        &command))
			goto done;
		VectorSubtract(target, ph.origin, delta);
		delta[2] = 0.0f;
		horizontal = VectorLength(delta);
		if (horizontal <= 64.0f && command.forwardmove > 64)
			command.forwardmove = 64;
		if (horizontal > 0.01f && command.forwardmove == 0)
			command.forwardmove = 40;
		VectorCopy(ph.origin, before);
		SG_OracleRun(&ph, &command, 1);
		if (!SG_OracleCompoundPhantomClean(&ph) || ph.waterlevel != 0 ||
		    !SG_OracleCompoundOutsideSegment(resolved, before, ph.origin))
			goto done;
		if (!sg_oracle_compound_touched)
			continue;
		VectorCopy(ph.origin, contact_anchor);
		if (SG_CompoundWorldResolvePreopen(contact_anchor, &exact) != RLR_OK ||
		    exact.trigger_key != resolved->trigger_key ||
		    exact.mover_key != resolved->mover_key)
		{
			VectorClear(contact_anchor);
			goto done;
		}
		if (contact_phantom)
			*contact_phantom = ph;
		reason = RLR_OK;
		goto done;
	}

done:
	if (scope_entered)
		SG_OracleCompoundScopeRestore(&scope);
	return reason;
}

rune_reject_reason_t SG_OracleCompoundDropDiscoverContact(
	const vec3_t source, const sg_compound_world_preopen_t *resolved,
	const vec3_t canonical_hint, vec3_t mechanism_anchor,
	qboolean loader_replay)
{
	vec3_t discovered, replayed;
	rune_reject_reason_t reason;

	if (!mechanism_anchor)
		return RLR_BAD_CONTROL_POLICY;
	VectorClear(mechanism_anchor);
	if (!source || !resolved || !canonical_hint ||
	    !SG_OracleCompoundFixedVector(canonical_hint) ||
	    !SG_CompoundWorldPreopenHintMatches(resolved, canonical_hint))
		return RLR_BAD_MECHANISM_ANCHOR;
	reason = SG_OracleCompoundDropFirstContact(source, resolved,
	    canonical_hint, discovered, NULL, loader_replay);
	if (reason != RLR_OK)
		return reason;
	reason = SG_OracleCompoundDropFirstContact(source, resolved, discovered,
	    replayed, NULL, loader_replay);
	if (reason != RLR_OK)
		return reason;
	if (memcmp(discovered, replayed, sizeof(discovered)) != 0)
		return RLR_APPROACH_REPLAY_FAILED;
	VectorCopy(discovered, mechanism_anchor);
	return RLR_OK;
}

typedef struct sg_oracle_drop_cursor_s
{
	sg_drop_replay_state_t state;
	sg_replay_pose_t pose;
	sg_replay_observation_t observation;
} sg_oracle_drop_cursor_t;

static qboolean SG_OracleDropHarmfulLiquid(const sg_phantom_t *ph)
{
	return ph && ph->waterlevel > 0 &&
	       (ph->watertype & (CONTENTS_LAVA | CONTENTS_SLIME));
}

static void SG_OracleDropObservation(const sg_phantom_t *ph,
	const vec3_t destination, qboolean destination_water,
	const sg_drop_replay_state_t *state, edict_t *passent,
	sg_replay_observation_t *observation)
{
	vec3_t delta;
	float horizontal2;
	qboolean airborne_after;

	memset(observation, 0, sizeof(*observation));
	if (!ph || !state)
		return;
	observation->ground_support_valid = true;
	observation->drop_recovery_admitted = !destination_water;
	observation->drop_landing_observed =
		ph->groundentity || ph->waterlevel >= 2;
	observation->door_passed = ph->door_passed;
	observation->contaminated = sg_oracle_contaminated;
	if (observation->contaminated || SG_OracleDropHarmfulLiquid(ph) ||
	    ((state->progress.elapsed_ms + SG_REPLAY_STEP_MS) %
	     SG_REPLAY_FRAME_MS) != 0)
		return;
	airborne_after = state->airborne ||
		(state->walkoff && !ph->groundentity);
	VectorSubtract(destination, ph->origin, delta);
	horizontal2 = delta[0] * delta[0] + delta[1] * delta[1];
	if (state->walkoff && airborne_after &&
	    horizontal2 < SG_REPLAY_ARRIVE_RADIUS * SG_REPLAY_ARRIVE_RADIUS &&
	    delta[2] > -SG_REPLAY_ARRIVE_Z && delta[2] < SG_REPLAY_ARRIVE_Z &&
	    (destination_water ? ph->waterlevel == 3 :
	     (ph->groundentity || ph->waterlevel >= 2)))
	{
		observation->contact_clear = SG_OracleReplayContactClear(
			ph->origin, destination, passent);
		observation->drop_arrival_contact_clear =
			observation->contact_clear;
		if (observation->contact_clear)
			return;
	}
	if (state->walkoff && airborne_after && !destination_water &&
	    ph->groundentity && ph->waterlevel == 0 &&
	    horizontal2 < SG_RUNE_PROOF_DROP_RECOVERY_RADIUS *
	                      SG_RUNE_PROOF_DROP_RECOVERY_RADIUS &&
	    delta[2] > -SG_RUNE_PROOF_DROP_RECOVERY_Z &&
	    delta[2] < SG_RUNE_PROOF_DROP_RECOVERY_Z)
		observation->drop_recovery_contact_clear =
			SG_OracleReplayContactClear(ph->origin, destination, passent);
}

typedef enum sg_oracle_compound_suffix_start_e
{
	SG_ORACLE_COMPOUND_SUFFIX_OUTSIDE = 0,
	SG_ORACLE_COMPOUND_SUFFIX_INSIDE
} sg_oracle_compound_suffix_start_t;

static rune_reject_reason_t SG_OracleCompoundDropSuffix(
	sg_phantom_t *ph, const sg_compound_world_preopen_t *resolved,
	edict_t *member, const vec3_t destination, const vec3_t lip,
	byte heading, qboolean destination_water, float old_frame_z,
	edict_t *passent, sg_oracle_compound_suffix_start_t start,
	qboolean require_live_top,
	sg_compound_drop_proof_t *proof)
{
	sg_oracle_drop_cursor_t cursor;
	sg_drop_replay_spec_t spec;
	sg_replay_status_t status;
	qboolean outside_before;
	int last_sweep_contact_ms = 0;

	memset(&cursor, 0, sizeof(cursor));
	memset(&spec, 0, sizeof(spec));
	VectorCopy(destination, spec.destination);
	VectorCopy(lip, spec.lip);
	spec.heading = heading;
	spec.destination_water = destination_water;
	spec.expected_arrival_ms = SG_REPLAY_TIME_DISCOVER;
	SG_OracleReplayPose(ph, &cursor.pose);
	SG_OracleDropObservation(ph, destination, destination_water,
	    &cursor.state, passent, &cursor.observation);
	status = SG_DropReplayBegin(&cursor.state, &spec, &cursor.pose,
	    &cursor.observation, old_frame_z);
	outside_before = SG_CompoundWorldOutsideSweep(resolved, ph->origin);
	if ((start == SG_ORACLE_COMPOUND_SUFFIX_OUTSIDE && !outside_before) ||
	    (start == SG_ORACLE_COMPOUND_SUFFIX_INSIDE && outside_before))
		return RLR_SUFFIX_REPLAY_FAILED;
	while (status == SG_REPLAY_RUNNING)
	{
		edict_t *current = NULL;
		usercmd_t command;
		vec3_t before;
		qboolean crossed, outside;

		if (!SG_CompoundWorldResolvedMember(resolved, &current) ||
		    current != member ||
		    (require_live_top && !SG_CompoundWorldAtTopFor(resolved, 0)))
			return RLR_MECHANISM_UNRESOLVED;
		status = SG_DropReplayPreStep(&cursor.state, &cursor.pose, &command);
		if (status != SG_REPLAY_RUNNING)
			break;
		VectorCopy(ph->origin, before);
		SG_OracleRun(ph, &command, 1);
		if (!SG_CompoundWorldResolvedMember(resolved, &current) ||
		    current != member ||
		    (require_live_top && !SG_CompoundWorldAtTopFor(resolved, 0)) ||
		    !SG_OracleCompoundPhantomClean(ph))
			return RLR_SUFFIX_REPLAY_FAILED;
		SG_OracleReplayPose(ph, &cursor.pose);
		SG_OracleDropObservation(ph, destination, destination_water,
		    &cursor.state, passent, &cursor.observation);
		status = SG_DropReplayPostStep(&cursor.state, &cursor.pose,
		    &cursor.observation);
		crossed = SG_CompoundWorldCrossesSweep(resolved, before, ph->origin);
		outside = SG_CompoundWorldOutsideSweep(resolved, ph->origin);
		if (proof->sweep_clear_ms)
		{
			if (crossed || !outside)
				return RLR_CLEAR_MISMATCH;
		}
		else
		{
			if (crossed || !outside_before || !outside)
				last_sweep_contact_ms = cursor.state.progress.elapsed_ms;
			if ((cursor.state.progress.elapsed_ms % SG_REPLAY_FRAME_MS) == 0 &&
			    last_sweep_contact_ms > 0 && outside)
				proof->sweep_clear_ms = cursor.state.progress.elapsed_ms;
		}
		outside_before = outside;
	}
	if (status != SG_REPLAY_ARRIVED)
		return RLR_SUFFIX_REPLAY_FAILED;
	if (!proof->sweep_clear_ms ||
	    proof->sweep_clear_ms > cursor.state.progress.arrival_ms)
		return RLR_CLEAR_MISMATCH;
	proof->arrival_ms = cursor.state.progress.arrival_ms;
	proof->exit_speed = cursor.state.progress.exit_speed;
	return RLR_OK;
}

rune_reject_reason_t SG_OracleCompoundDropPreopen(
	const vec3_t source, const sg_compound_world_preopen_t *resolved,
	const vec3_t mechanism_anchor, const vec3_t destination,
	const vec3_t lip, byte heading, qboolean destination_water,
	sg_compound_drop_proof_t *proof, qboolean loader_replay)
{
	sg_oracle_compound_scope_t scope;
	sg_oracle_member_snapshot_t member_snapshot;
	sg_compound_translate_t translate;
	sg_compound_translate_step_t mover_step;
	sg_compound_drop_proof_t candidate;
	sg_phantom_t ph;
	vec3_t contact;
	edict_t *member = NULL;
	rune_reject_reason_t reason;
	qboolean scope_entered = false;
	qboolean member_staged = false;
	int remainder_commands;
	float old_frame_z;

	if (!proof)
		return RLR_BAD_CONTROL_POLICY;
	memset(proof, 0, sizeof(*proof));
	memset(&candidate, 0, sizeof(candidate));
	if (!source || !resolved || !mechanism_anchor || !destination || !lip ||
	    !SG_OracleCompoundFixedVector(source) ||
	    !SG_OracleCompoundFixedVector(mechanism_anchor) ||
	    !SG_OracleCompoundFixedVector(destination) ||
	    !SG_OracleFinite3(lip) ||
	    !SG_CompoundWorldResolvedMember(resolved, &member) ||
	    !resolved->trigger ||
	    !SG_OracleCompoundMemberAtBottom(member, resolved))
		return RLR_BAD_CONTROL_POLICY;
	reason = SG_OracleCompoundDropFirstContact(source, resolved,
	    mechanism_anchor, contact, &ph, loader_replay);
	if (reason != RLR_OK)
		return reason;
	if (memcmp(contact, mechanism_anchor, sizeof(contact)) != 0)
		return RLR_APPROACH_REPLAY_FAILED;
	candidate.touch_ms = 0;
	/* Re-run once inside the single scope below to preserve the exact phantom
	 * and contact time through mover staging and the DROP suffix. */

	SG_OracleMemberSnapshot(member, &member_snapshot);
	SG_OraclePlace(&ph, (vec_t *)source);
	SG_OracleCompoundScopeEnter(&scope, resolved->trigger, member, NULL,
	                            true, loader_replay);
	scope_entered = true;
	{
		usercmd_t command;
		int elapsed;

		memset(&command, 0, sizeof(command));
		command.msec = 0;
		SG_OracleRun(&ph, &command, 1);
		if (!SG_OracleCompoundPhantomClean(&ph) || !ph.groundentity ||
		    ph.waterlevel != 0 || SG_OracleTriggerOverlap(&ph) ||
		    SG_OracleSolidOverlap(&ph))
		{
			reason = RLR_APPROACH_REPLAY_FAILED;
			goto done;
		}
		SG_OracleCompoundDropCaptureSource(&ph, 0.0f, &candidate);
		for (elapsed = 0; elapsed < SG_DOOR_APPROACH_LIMIT_MS;
		     elapsed += SG_REPLAY_STEP_MS)
		{
			vec3_t before, delta;
			float horizontal;

			memset(&command, 0, sizeof(command));
			command.msec = SG_REPLAY_STEP_MS;
			if (!SG_DeclaredCommand(ph.origin, (vec_t *)mechanism_anchor,
			        &ph.pms, &command))
			{
				reason = RLR_APPROACH_REPLAY_FAILED;
				goto done;
			}
			VectorSubtract(mechanism_anchor, ph.origin, delta);
			delta[2] = 0.0f;
			horizontal = VectorLength(delta);
			if (horizontal <= 64.0f && command.forwardmove > 64)
				command.forwardmove = 64;
			if (horizontal > 0.01f && command.forwardmove == 0)
				command.forwardmove = 40;
			VectorCopy(ph.origin, before);
			SG_OracleRun(&ph, &command, 1);
			if (!SG_OracleCompoundPhantomClean(&ph) || ph.waterlevel != 0 ||
			    !SG_OracleCompoundOutsideSegment(resolved, before, ph.origin))
			{
				reason = RLR_APPROACH_REPLAY_FAILED;
				goto done;
			}
			if (!sg_oracle_compound_touched)
				continue;
			candidate.touch_ms = elapsed + SG_REPLAY_STEP_MS;
			break;
		}
	}
	if (candidate.touch_ms <= 0 ||
	    !SG_OracleCompoundAnchorMatches(&ph, mechanism_anchor))
	{
		reason = RLR_APPROACH_REPLAY_FAILED;
		goto done;
	}
	old_frame_z = ph.velocity[2];
	remainder_commands =
		((SG_REPLAY_FRAME_MS - candidate.touch_ms % SG_REPLAY_FRAME_MS) %
		 SG_REPLAY_FRAME_MS) / SG_REPLAY_STEP_MS;
	candidate.touch_frame_end_ms = candidate.touch_ms +
		remainder_commands * SG_REPLAY_STEP_MS;
	if (remainder_commands > 0 &&
	    !SG_OracleCompoundFrame(&ph, resolved, mechanism_anchor, NULL,
	                            remainder_commands, &old_frame_z))
	{
		reason = RLR_APPROACH_REPLAY_FAILED;
		goto done;
	}
	if (!SG_CompoundTranslateBegin(&translate, resolved->bottom_origin,
	                               resolved->top_origin, resolved->speed))
	{
		reason = RLR_RIDE_REPLAY_FAILED;
		goto done;
	}
	for (;;)
	{
		if (!SG_CompoundTranslateFrame(&translate, &mover_step) ||
		    !SG_OracleCompoundStageMember(member, resolved, &mover_step))
		{
			reason = RLR_RIDE_REPLAY_FAILED;
			goto done;
		}
		member_staged = true;
		if (mover_step.at_top)
			break;
		if (!SG_OracleCompoundFrame(&ph, resolved, mechanism_anchor, NULL, 4,
		                            &old_frame_z))
		{
			reason = RLR_RIDE_REPLAY_FAILED;
			goto done;
		}
	}
	candidate.mover_top_ms = mover_step.elapsed_ms;
	candidate.suffix_start_ms = mover_step.elapsed_ms - SG_REPLAY_FRAME_MS;
	candidate.heading = heading;
	SG_OracleCompoundDropCaptureSuffix(&ph, old_frame_z, &candidate);
	reason = SG_OracleCompoundDropSuffix(&ph, resolved, member, destination,
	    lip, heading, destination_water, old_frame_z, NULL,
	    SG_ORACLE_COMPOUND_SUFFIX_OUTSIDE, false, &candidate);
	if (reason != RLR_OK)
		goto done;
	if (candidate.touch_frame_end_ms >
	        RUNE_MAX_COST_MS - candidate.suffix_start_ms ||
	    candidate.arrival_ms > RUNE_MAX_COST_MS -
	        candidate.touch_frame_end_ms - candidate.suffix_start_ms)
	{
		reason = RLR_COST_MISMATCH;
		goto done;
	}
	candidate.total_cost_ms = candidate.touch_frame_end_ms +
		candidate.suffix_start_ms + candidate.arrival_ms;
	*proof = candidate;
	reason = RLR_OK;

done:
	if (member_staged)
		SG_OracleMemberRestore(member, &member_snapshot);
	if (scope_entered)
		SG_OracleCompoundScopeRestore(&scope);
	return reason;
}

static qboolean SG_OracleCompoundLivePhantomValid(const sg_phantom_t *ph,
	edict_t *passent, float old_frame_z)
{
	pmove_state_t expected;
	double scaled;
	int axis;

	if (!ph || !passent || !passent->inuse || !passent->client ||
	    passent->health <= 0 || passent->deadflag ||
	    passent->movetype != MOVETYPE_WALK || passent->s.modelindex != 255 ||
	    passent->client->chase_target || passent->client->hookstate ||
	    passent->client->hook ||
	    passent->client->ps.pmove.pm_type != PM_NORMAL ||
	    !sv_gravity || !isfinite(sv_gravity->value) ||
	    sv_gravity->value < -32768.0f || sv_gravity->value > 32767.0f ||
	    !isfinite(old_frame_z) ||
	    old_frame_z != passent->client->oldvelocity[2] ||
	    ph->armed_door_count != 0 || ph->door_arm_overflow ||
	    ph->door_passed || ph->door_wait_ms != 0 || ph->door_open_ms != 0)
		return false;
	expected = passent->client->ps.pmove;
	expected.gravity = (short)sv_gravity->value;
	for (axis = 0; axis < 3; axis++)
	{
		scaled = (double)passent->s.origin[axis] * 8.0;
		if (!isfinite(scaled) || scaled < -32768.0 || scaled > 32767.0)
			return false;
		expected.origin[axis] = (short)scaled;
		if (passent->s.origin[axis] != expected.origin[axis] * 0.125f)
			return false;
		scaled = (double)passent->velocity[axis] * 8.0;
		if (!isfinite(scaled) || scaled < -32768.0 || scaled > 32767.0)
			return false;
		expected.velocity[axis] = (short)scaled;
		if (passent->velocity[axis] != expected.velocity[axis] * 0.125f)
			return false;
	}
	if (memcmp(&ph->pms, &expected, sizeof(expected)) != 0 ||
	    memcmp(&ph->old_pms, &passent->client->old_pmove,
	           sizeof(ph->old_pms)) != 0 ||
	    ph->groundentity != (passent->groundentity != NULL) ||
	    ph->watertype != passent->watertype ||
	    ph->waterlevel != passent->waterlevel)
		return false;
	for (axis = 0; axis < 3; axis++)
		if (ph->origin[axis] != ph->pms.origin[axis] * 0.125f ||
		    ph->velocity[axis] != ph->pms.velocity[axis] * 0.125f)
			return false;
	return true;
}

static rune_reject_reason_t SG_OracleCompoundDropLiveSuffix(sg_phantom_t *ph,
	const sg_compound_world_preopen_t *resolved,
	const vec3_t destination, const vec3_t lip, byte heading,
	qboolean destination_water, float old_frame_z,
	sg_compound_drop_proof_t *proof, edict_t *passent,
	sg_oracle_compound_suffix_start_t start)
{
	sg_oracle_compound_scope_t scope;
	sg_compound_drop_proof_t candidate;
	edict_t *member = NULL;
	rune_reject_reason_t reason = RLR_BAD_CONTROL_POLICY;
	qboolean scope_entered = false;

	if (!proof)
		return RLR_BAD_CONTROL_POLICY;
	memset(proof, 0, sizeof(*proof));
	memset(&candidate, 0, sizeof(candidate));
	if (!ph || !resolved || !destination || !lip ||
	    !SG_OracleFinite3(destination) || !SG_OracleFinite3(lip) ||
	    (destination_water != false && destination_water != true) ||
	    !sg_host.pmove || !sg_host.trace || !sg_host.pointcontents ||
	    !sg_host.box_edicts || sg_oracle_active_phantom ||
	    !SG_OracleCompoundLivePhantomValid(ph, passent, old_frame_z))
		return RLR_BAD_CONTROL_POLICY;
	if (!resolved->trigger ||
	    !SG_CompoundWorldResolvedMember(resolved, &member) ||
	    !SG_CompoundWorldAtTopFor(resolved, 0))
		return RLR_MECHANISM_UNRESOLVED;
	if ((start == SG_ORACLE_COMPOUND_SUFFIX_OUTSIDE &&
	     !SG_CompoundWorldOutsideSweep(resolved, ph->origin)) ||
	    (start == SG_ORACLE_COMPOUND_SUFFIX_INSIDE &&
	     SG_CompoundWorldOutsideSweep(resolved, ph->origin)))
		return RLR_SUFFIX_REPLAY_FAILED;

	SG_OracleCompoundScopeEnter(&scope, resolved->trigger, member, passent,
	                            true, false);
	scope_entered = true;
	if (SG_OracleTriggerOverlap(ph) || SG_OracleSolidOverlap(ph) ||
	    !SG_OracleCompoundPhantomClean(ph))
	{
		reason = RLR_SUFFIX_REPLAY_FAILED;
		goto done;
	}
	reason = SG_OracleCompoundDropSuffix(ph, resolved, member, destination,
	    lip, heading, destination_water, old_frame_z, passent, start, true,
	    &candidate);
	if (reason == RLR_OK)
		*proof = candidate;

done:
	if (scope_entered)
		SG_OracleCompoundScopeRestore(&scope);
	return reason;
}

rune_reject_reason_t SG_OracleCompoundDropRecover(sg_phantom_t *ph,
	const sg_compound_world_preopen_t *resolved,
	const vec3_t destination, const vec3_t lip, byte heading,
	qboolean destination_water, float old_frame_z,
	sg_compound_drop_proof_t *proof, edict_t *passent)
{
	return SG_OracleCompoundDropLiveSuffix(ph, resolved, destination, lip,
	    heading, destination_water, old_frame_z, proof, passent,
	    SG_ORACLE_COMPOUND_SUFFIX_INSIDE);
}

rune_reject_reason_t SG_OracleCompoundDropContinue(sg_phantom_t *ph,
	const sg_compound_world_preopen_t *resolved,
	const vec3_t destination, const vec3_t lip, byte heading,
	qboolean destination_water, float old_frame_z,
	sg_compound_drop_proof_t *proof, edict_t *passent)
{
	return SG_OracleCompoundDropLiveSuffix(ph, resolved, destination, lip,
	    heading, destination_water, old_frame_z, proof, passent,
	    SG_ORACLE_COMPOUND_SUFFIX_OUTSIDE);
}

static rune_reject_reason_t SG_OracleCompoundSwimSuffix(
	sg_phantom_t *ph, const sg_compound_world_preopen_t *resolved,
	edict_t *member, const vec3_t destination, qboolean destination_water,
	float old_frame_z, edict_t *passent,
	sg_oracle_compound_suffix_start_t start,
	qboolean require_live_top, sg_compound_swim_recovery_proof_t *proof,
	sg_replay_reason_t *replay_reason)
{
	sg_oracle_swim_cursor_t cursor;
	sg_compound_swim_recovery_proof_t candidate;
	sg_replay_status_t status;
	qboolean outside_before;
	int last_sweep_contact_ms = 0;

	if (!proof)
		return RLR_BAD_CONTROL_POLICY;
	memset(proof, 0, sizeof(*proof));
	if (replay_reason)
		*replay_reason = SG_REPLAY_REASON_NONE;
	memset(&candidate, 0, sizeof(candidate));
	outside_before = SG_CompoundWorldOutsideSweep(resolved, ph->origin);
	if ((start == SG_ORACLE_COMPOUND_SUFFIX_OUTSIDE && !outside_before) ||
	    (start == SG_ORACLE_COMPOUND_SUFFIX_INSIDE && outside_before))
		return RLR_SUFFIX_REPLAY_FAILED;
	status = SG_OracleSwimCursorBegin(&cursor, ph, destination,
	                                  destination_water, old_frame_z,
	                                  passent, false);
	while (status == SG_REPLAY_RUNNING)
	{
		edict_t *current = NULL;
		vec3_t before;
		qboolean crossed, outside;

		if (!SG_CompoundWorldResolvedMember(resolved, &current) ||
		    current != member ||
		    (require_live_top && !SG_CompoundWorldAtTopFor(resolved, 0)))
			return RLR_SUFFIX_REPLAY_FAILED;
		VectorCopy(ph->origin, before);
		status = SG_OracleSwimCursorStep(&cursor, ph);
		if (!SG_CompoundWorldResolvedMember(resolved, &current) ||
		    current != member ||
		    (require_live_top && !SG_CompoundWorldAtTopFor(resolved, 0)))
			return RLR_SUFFIX_REPLAY_FAILED;
		crossed = SG_CompoundWorldCrossesSweep(resolved, before, ph->origin);
		outside = SG_CompoundWorldOutsideSweep(resolved, ph->origin);
		if (candidate.sweep_clear_ms)
		{
			if (crossed || !outside)
				return RLR_CLEAR_MISMATCH;
		}
		else
		{
			/* Contact in any 25 ms segment conservatively lasts through
			 * that segment's endpoint.  A chord may start and end outside;
			 * the aligned endpoint can still be the first clear boundary. */
			if (crossed || !outside_before || !outside)
				last_sweep_contact_ms =
					cursor.state.progress.elapsed_ms;
			if ((cursor.state.progress.elapsed_ms % SG_REPLAY_FRAME_MS) == 0 &&
			    last_sweep_contact_ms > 0 && outside)
				candidate.sweep_clear_ms = cursor.state.progress.elapsed_ms;
		}
		outside_before = outside;
	}
	if (status != SG_REPLAY_ARRIVED)
	{
		if (replay_reason)
			*replay_reason = cursor.state.progress.reason;
		return RLR_SUFFIX_REPLAY_FAILED;
	}
	if (!candidate.sweep_clear_ms ||
	    candidate.sweep_clear_ms > cursor.state.progress.arrival_ms)
		return RLR_CLEAR_MISMATCH;
	candidate.arrival_ms = cursor.state.progress.arrival_ms;
	candidate.exit_speed = cursor.state.progress.exit_speed;
	*proof = candidate;
	return RLR_OK;
}

rune_reject_reason_t SG_OracleCompoundSwimPrepareSource(
	const vec3_t source, const sg_compound_world_preopen_t *resolved,
	float old_frame_z, sg_compound_swim_source_t *prepared,
	edict_t *passent, qboolean world_only, qboolean loader_replay)
{
	sg_oracle_compound_scope_t scope;
	sg_compound_swim_source_t candidate;
	usercmd_t command;
	edict_t *member = NULL;
	rune_reject_reason_t reason = RLR_APPROACH_REPLAY_FAILED;
	qboolean scope_entered = false;

	if (!prepared)
		return RLR_BAD_CONTROL_POLICY;
	memset(prepared, 0, sizeof(*prepared));
	memset(&candidate, 0, sizeof(candidate));
	if (!source || !resolved || !SG_OracleFinite3(source) ||
	    !SG_OracleCompoundFixedVector(source) || !isfinite(old_frame_z) ||
	    (world_only != false && world_only != true) ||
	    (loader_replay != false && loader_replay != true) ||
	    !sg_host.pmove || !sg_host.trace || !sg_host.pointcontents ||
	    !sg_host.box_edicts || sg_oracle_active_phantom)
		return source && SG_OracleFinite3(source) &&
		       !SG_OracleCompoundFixedVector(source)
		           ? RLR_BAD_MECHANISM_ANCHOR : RLR_BAD_CONTROL_POLICY;
	if (!SG_CompoundWorldResolvedMember(resolved, &member) ||
	    !resolved->trigger ||
	    !SG_OracleCompoundMemberAtBottom(member, resolved))
		return RLR_MECHANISM_UNRESOLVED;

	SG_OraclePlace(&candidate.phantom, (vec_t *)source);
	candidate.old_frame_z = old_frame_z;
	SG_OracleCompoundScopeEnter(&scope, resolved->trigger, member, passent,
	                            world_only, loader_replay);
	scope_entered = true;
	if (!SG_OracleCompoundAnchorMatches(&candidate.phantom, source) ||
	    !SG_CompoundWorldOutsideSweep(resolved,
	                                  candidate.phantom.origin) ||
	    SG_OracleTriggerOverlap(&candidate.phantom) ||
	    sg_oracle_compound_touched ||
	    SG_OracleSolidOverlap(&candidate.phantom) ||
	    !SG_OracleCompoundPhantomClean(&candidate.phantom))
		goto done;
	memset(&command, 0, sizeof(command));
	command.msec = 0;
	SG_OracleRun(&candidate.phantom, &command, 1);
	if (!SG_OracleCompoundAnchorMatches(&candidate.phantom, source) ||
	    !SG_CompoundWorldOutsideSweep(resolved,
	                                  candidate.phantom.origin) ||
	    SG_OracleTriggerOverlap(&candidate.phantom) ||
	    sg_oracle_compound_touched ||
	    SG_OracleSolidOverlap(&candidate.phantom) ||
	    !SG_OracleCompoundPhantomClean(&candidate.phantom) ||
	    candidate.phantom.waterlevel < 2 ||
	    !(candidate.phantom.watertype & CONTENTS_WATER) ||
	    (candidate.phantom.watertype &
	     (CONTENTS_LAVA | CONTENTS_SLIME)))
		goto done;
	*prepared = candidate;
	reason = RLR_OK;

done:
	if (scope_entered)
		SG_OracleCompoundScopeRestore(&scope);
	return reason;
}

static rune_reject_reason_t SG_OracleCompoundSwimFirstContact(
	const sg_compound_swim_source_t *prepared,
	const sg_compound_world_preopen_t *resolved, const vec3_t target,
	vec3_t contact_anchor, edict_t *passent, qboolean world_only,
	qboolean loader_replay)
{
	sg_oracle_compound_scope_t scope;
	sg_oracle_swim_cursor_t cursor;
	sg_compound_world_preopen_t exact;
	sg_phantom_t ph;
	sg_replay_status_t status;
	edict_t *member = NULL;
	rune_reject_reason_t reason = RLR_APPROACH_REPLAY_FAILED;
	qboolean scope_entered = false;
	int axis;

	memset(contact_anchor, 0, sizeof(vec3_t));
	if (!SG_CompoundWorldResolvedMember(resolved, &member) ||
	    !resolved->trigger ||
	    !SG_OracleCompoundMemberAtBottom(member, resolved))
		return RLR_MECHANISM_UNRESOLVED;
	ph = prepared->phantom;
	SG_OracleCompoundScopeEnter(&scope, resolved->trigger, member, passent,
	                            world_only, loader_replay);
	scope_entered = true;
	if (!SG_OracleCompoundPreparedSourceValid(prepared) ||
	    !SG_CompoundWorldOutsideSweep(resolved, ph.origin) ||
	    SG_OracleTriggerOverlap(&ph) || sg_oracle_compound_touched ||
	    SG_OracleSolidOverlap(&ph) || !SG_OracleCompoundPhantomClean(&ph))
		goto done;
	status = SG_OracleSwimCursorBegin(&cursor, &ph, target, true,
	                                  prepared->old_frame_z, passent, true);
	while (status == SG_REPLAY_RUNNING)
	{
		vec3_t before;

		VectorCopy(ph.origin, before);
		status = SG_OracleSwimCursorStep(&cursor, &ph);
		if (!SG_OracleCompoundOutsideSegment(resolved, before, ph.origin))
			goto done;
		if (sg_oracle_compound_touched)
			break;
	}
	if (!sg_oracle_compound_touched || status == SG_REPLAY_FAILED ||
	    !SG_OracleCompoundPhantomClean(&ph))
		goto done;
	for (axis = 0; axis < 3; axis++)
	{
		contact_anchor[axis] = ph.pms.origin[axis] * 0.125f;
		if (contact_anchor[axis] == 0.0f)
			contact_anchor[axis] = 0.0f;
	}
	if (!SG_OracleCompoundAnchorMatches(&ph, contact_anchor) ||
	    SG_CompoundWorldResolvePreopen(contact_anchor, &exact) != RLR_OK ||
	    exact.trigger_key != resolved->trigger_key ||
	    exact.mover_key != resolved->mover_key)
	{
		memset(contact_anchor, 0, sizeof(vec3_t));
		goto done;
	}
	reason = RLR_OK;

done:
	if (scope_entered)
		SG_OracleCompoundScopeRestore(&scope);
	return reason;
}

rune_reject_reason_t SG_OracleCompoundSwimDiscoverContact(
	const sg_compound_swim_source_t *prepared,
	const sg_compound_world_preopen_t *resolved,
	const vec3_t canonical_hint, vec3_t mechanism_anchor,
	edict_t *passent, qboolean world_only, qboolean loader_replay)
{
	vec3_t discovered, replayed;
	rune_reject_reason_t reason;

	if (!mechanism_anchor)
		return RLR_BAD_CONTROL_POLICY;
	memset(mechanism_anchor, 0, sizeof(vec3_t));
	if (!prepared || !resolved || !canonical_hint ||
	    !SG_OracleFinite3(canonical_hint) ||
	    !SG_OracleCompoundFixedVector(canonical_hint) ||
	    (world_only != false && world_only != true) ||
	    (loader_replay != false && loader_replay != true) ||
	    !sg_host.pmove || !sg_host.trace || !sg_host.pointcontents ||
	    !sg_host.box_edicts || sg_oracle_active_phantom)
		return RLR_BAD_CONTROL_POLICY;
	if (!SG_CompoundWorldPreopenHintMatches(resolved, canonical_hint))
		return RLR_BAD_MECHANISM_ANCHOR;
	reason = SG_OracleCompoundSwimFirstContact(prepared, resolved,
	                                          canonical_hint, discovered,
	                                          passent, world_only,
	                                          loader_replay);
	if (reason != RLR_OK)
		return reason;
	reason = SG_OracleCompoundSwimFirstContact(prepared, resolved,
	                                          discovered, replayed,
	                                          passent, world_only,
	                                          loader_replay);
	if (reason != RLR_OK)
		return reason;
	if (memcmp(discovered, replayed, sizeof(discovered)) != 0)
		return RLR_APPROACH_REPLAY_FAILED;
	memcpy(mechanism_anchor, discovered, sizeof(discovered));
	return RLR_OK;
}

/* Joint witness for PREOPEN D_SWIM.  It observes the resolved trigger, stages
 * but never activates the one physical member, and starts the suffix from the
 * same phantom only after TOP is linked. */
static rune_reject_reason_t SG_OracleCompoundSwimPreopenMode(
	sg_phantom_t *ph,
	const sg_compound_world_preopen_t *resolved,
	const vec3_t mechanism_anchor, const vec3_t destination,
	qboolean destination_water, float old_frame_z,
	sg_compound_swim_proof_t *proof, edict_t *passent,
	qboolean world_only, qboolean loader_replay,
	qboolean capture_contact, vec3_t contact_anchor,
	sg_replay_reason_t *replay_reason)
{
	sg_oracle_compound_scope_t scope;
	sg_oracle_member_snapshot_t member_snapshot;
	sg_compound_translate_t translate;
	sg_compound_translate_step_t mover_step;
	sg_oracle_swim_cursor_t cursor;
	sg_compound_swim_proof_t candidate;
	sg_compound_swim_recovery_proof_t suffix;
	vec3_t captured_contact;
	sg_replay_status_t status;
	rune_reject_reason_t reason = RLR_BAD_CONTROL_POLICY;
	edict_t *member = NULL;
	qboolean scope_entered = false;
	qboolean member_staged = false;
	int remainder_commands;

	if (!proof || (capture_contact && !contact_anchor))
		return RLR_BAD_CONTROL_POLICY;
	memset(proof, 0, sizeof(*proof));
	if (replay_reason)
		*replay_reason = SG_REPLAY_REASON_NONE;
	if (contact_anchor)
		VectorClear(contact_anchor);
	memset(&candidate, 0, sizeof(candidate));
	VectorClear(captured_contact);
	if (!ph || !resolved || !mechanism_anchor || !destination ||
	    !SG_OracleFinite3(mechanism_anchor) ||
	    !SG_OracleFinite3(destination) || !isfinite(old_frame_z) ||
	    (destination_water != false && destination_water != true) ||
	    (world_only != false && world_only != true) ||
	    (loader_replay != false && loader_replay != true) ||
	    (capture_contact != false && capture_contact != true) ||
	    !sg_host.pmove || !sg_host.trace || !sg_host.pointcontents ||
	    !sg_host.box_edicts || !sg_host.linkentity ||
	    sg_oracle_active_phantom)
		return RLR_BAD_CONTROL_POLICY;
	if (!SG_CompoundWorldResolvedMember(resolved, &member) ||
	    !resolved->trigger ||
	    !SG_OracleCompoundMemberAtBottom(member, resolved))
		return RLR_MECHANISM_UNRESOLVED;

	SG_OracleMemberSnapshot(member, &member_snapshot);
	SG_OracleCompoundScopeEnter(&scope, resolved->trigger, member, passent,
		world_only, loader_replay);
	scope_entered = true;
	if (!SG_CompoundWorldOutsideSweep(resolved, ph->origin) ||
	    SG_OracleTriggerOverlap(ph) || sg_oracle_compound_touched ||
	    SG_OracleSolidOverlap(ph) || !SG_OracleCompoundPhantomClean(ph))
	{
		reason = RLR_APPROACH_REPLAY_FAILED;
		goto done;
	}
	status = SG_OracleSwimCursorBegin(&cursor, ph, mechanism_anchor, true,
		old_frame_z, passent, true);
	while (status == SG_REPLAY_RUNNING)
	{
		vec3_t before;

		VectorCopy(ph->origin, before);
		status = SG_OracleSwimCursorStep(&cursor, ph);
		if (!SG_OracleCompoundOutsideSegment(resolved, before, ph->origin))
		{
			reason = RLR_APPROACH_REPLAY_FAILED;
			goto done;
		}
		if (sg_oracle_compound_touched)
			break;
	}
	if (!sg_oracle_compound_touched || status == SG_REPLAY_FAILED ||
	    !SG_OracleCompoundPhantomClean(ph) ||
	    (!capture_contact &&
	     !SG_OracleCompoundAnchorMatches(ph, mechanism_anchor)))
	{
		reason = RLR_APPROACH_REPLAY_FAILED;
		goto done;
	}
	if (capture_contact)
	{
		VectorCopy(ph->origin, captured_contact);
		if (!SG_OracleCompoundFixedVector(captured_contact))
		{
			reason = RLR_APPROACH_REPLAY_FAILED;
			goto done;
		}
	}
	candidate.touch_ms = cursor.state.progress.elapsed_ms;
	if (candidate.touch_ms <= 0 ||
	    (candidate.touch_ms % SG_REPLAY_STEP_MS) != 0)
	{
		reason = RLR_APPROACH_REPLAY_FAILED;
		goto done;
	}
	old_frame_z = cursor.state.progress.old_frame_z;
	remainder_commands =
		((SG_REPLAY_FRAME_MS -
		  (candidate.touch_ms % SG_REPLAY_FRAME_MS)) %
		 SG_REPLAY_FRAME_MS) / SG_REPLAY_STEP_MS;
	candidate.touch_frame_end_ms = candidate.touch_ms +
		remainder_commands * SG_REPLAY_STEP_MS;
	if ((candidate.touch_frame_end_ms % SG_REPLAY_FRAME_MS) != 0)
	{
		reason = RLR_APPROACH_REPLAY_FAILED;
		goto done;
	}
	if (remainder_commands > 0 &&
	    !SG_OracleCompoundFrame(ph, resolved, mechanism_anchor, NULL,
	                            remainder_commands, &old_frame_z))
	{
		reason = RLR_APPROACH_REPLAY_FAILED;
		goto done;
	}
	if (!SG_CompoundTranslateBegin(&translate, resolved->bottom_origin,
	                               resolved->top_origin, resolved->speed))
	{
		reason = RLR_RIDE_REPLAY_FAILED;
		goto done;
	}
	for (;;)
	{
		if (!SG_CompoundTranslateFrame(&translate, &mover_step) ||
		    mover_step.elapsed_ms > RUNE_MAX_COST_MS ||
		    !SG_OracleCompoundStageMember(member, resolved, &mover_step))
		{
			reason = RLR_RIDE_REPLAY_FAILED;
			goto done;
		}
		member_staged = true;
		if (mover_step.at_top)
			break;
		if (!SG_OracleCompoundFrame(ph, resolved, mechanism_anchor, NULL, 4,
		                            &old_frame_z))
		{
			reason = RLR_RIDE_REPLAY_FAILED;
			goto done;
		}
	}
	candidate.mover_top_ms = mover_step.elapsed_ms;
	candidate.suffix_start_ms = mover_step.elapsed_ms - SG_REPLAY_FRAME_MS;
	if (candidate.suffix_start_ms < 0)
	{
		reason = RLR_RIDE_REPLAY_FAILED;
		goto done;
	}
	SG_OracleCompoundCaptureSuffix(ph, old_frame_z, &candidate);
	reason = SG_OracleCompoundSwimSuffix(ph, resolved, member, destination,
		destination_water, old_frame_z, passent,
		SG_ORACLE_COMPOUND_SUFFIX_OUTSIDE, false, &suffix, replay_reason);
	if (reason != RLR_OK)
		goto done;
	candidate.arrival_ms = suffix.arrival_ms;
	candidate.sweep_clear_ms = suffix.sweep_clear_ms;
	if (candidate.touch_frame_end_ms >
	    RUNE_MAX_COST_MS - candidate.suffix_start_ms ||
	    candidate.arrival_ms >
	    RUNE_MAX_COST_MS - candidate.touch_frame_end_ms -
	    candidate.suffix_start_ms)
	{
		reason = RLR_COST_MISMATCH;
		goto done;
	}
	candidate.total_cost_ms = candidate.touch_frame_end_ms +
		candidate.suffix_start_ms + candidate.arrival_ms;
	candidate.exit_speed = suffix.exit_speed;
	if (capture_contact)
		VectorCopy(captured_contact, contact_anchor);
	*proof = candidate;
	reason = RLR_OK;

done:
	if (member_staged)
		SG_OracleMemberRestore(member, &member_snapshot);
	if (scope_entered)
		SG_OracleCompoundScopeRestore(&scope);
	return reason;
}

rune_reject_reason_t SG_OracleCompoundSwimPreopen(sg_phantom_t *ph,
	const sg_compound_world_preopen_t *resolved,
	const vec3_t mechanism_anchor, const vec3_t destination,
	qboolean destination_water, float old_frame_z,
	sg_compound_swim_proof_t *proof, sg_replay_reason_t *replay_reason,
	edict_t *passent,
	qboolean world_only, qboolean loader_replay)
{
	return SG_OracleCompoundSwimPreopenMode(ph, resolved, mechanism_anchor,
		destination, destination_water, old_frame_z, proof, passent,
		world_only, loader_replay, false, NULL, replay_reason);
}

rune_reject_reason_t SG_OracleCompoundSwimPlanLive(sg_phantom_t *ph,
	const sg_compound_world_preopen_t *resolved,
	const vec3_t canonical_hint, const vec3_t destination,
	qboolean destination_water, float old_frame_z,
	sg_compound_swim_proof_t *proof, vec3_t contact_anchor,
	edict_t *passent)
{
	return SG_OracleCompoundSwimPreopenMode(ph, resolved, canonical_hint,
		destination, destination_water, old_frame_z, proof, passent,
		true, false, true, contact_anchor, NULL);
}

static rune_reject_reason_t SG_OracleCompoundSwimLiveSuffix(sg_phantom_t *ph,
	const sg_compound_world_preopen_t *resolved, const vec3_t destination,
	qboolean destination_water, float old_frame_z,
	sg_compound_swim_recovery_proof_t *proof, edict_t *passent,
	sg_oracle_compound_suffix_start_t start)
{
	sg_oracle_compound_scope_t scope;
	sg_compound_swim_recovery_proof_t candidate;
	edict_t *member = NULL;
	rune_reject_reason_t reason = RLR_BAD_CONTROL_POLICY;
	qboolean scope_entered = false;

	if (!proof)
		return RLR_BAD_CONTROL_POLICY;
	memset(proof, 0, sizeof(*proof));
	memset(&candidate, 0, sizeof(candidate));
	if (!ph || !resolved || !destination || !SG_OracleFinite3(destination) ||
	    (destination_water != false && destination_water != true) ||
	    !sg_host.pmove || !sg_host.trace || !sg_host.pointcontents ||
	    !sg_host.box_edicts || sg_oracle_active_phantom ||
	    !SG_OracleCompoundLivePhantomValid(ph, passent, old_frame_z))
		return RLR_BAD_CONTROL_POLICY;
	if (!resolved->trigger ||
	    !SG_CompoundWorldResolvedMember(resolved, &member) ||
	    !SG_CompoundWorldAtTopFor(resolved, 0))
		return RLR_MECHANISM_UNRESOLVED;
	if ((start == SG_ORACLE_COMPOUND_SUFFIX_OUTSIDE &&
	     !SG_CompoundWorldOutsideSweep(resolved, ph->origin)) ||
	    (start == SG_ORACLE_COMPOUND_SUFFIX_INSIDE &&
	     SG_CompoundWorldOutsideSweep(resolved, ph->origin)))
		return RLR_SUFFIX_REPLAY_FAILED;

	SG_OracleCompoundScopeEnter(&scope, resolved->trigger, member, passent,
	                            true, false);
	scope_entered = true;
	if (SG_OracleTriggerOverlap(ph) || SG_OracleSolidOverlap(ph) ||
	    !SG_OracleCompoundPhantomClean(ph))
	{
		reason = RLR_SUFFIX_REPLAY_FAILED;
		goto done;
	}
	reason = SG_OracleCompoundSwimSuffix(ph, resolved, member, destination,
		destination_water, old_frame_z, passent,
		start, true, &candidate, NULL);
	if (reason == RLR_OK)
		*proof = candidate;

done:
	if (scope_entered)
		SG_OracleCompoundScopeRestore(&scope);
	return reason;
}

rune_reject_reason_t SG_OracleCompoundSwimRecover(sg_phantom_t *ph,
	const sg_compound_world_preopen_t *resolved, const vec3_t destination,
	qboolean destination_water, float old_frame_z,
	sg_compound_swim_recovery_proof_t *proof, edict_t *passent)
{
	return SG_OracleCompoundSwimLiveSuffix(ph, resolved, destination,
		destination_water, old_frame_z, proof, passent,
		SG_ORACLE_COMPOUND_SUFFIX_INSIDE);
}

rune_reject_reason_t SG_OracleCompoundSwimContinue(sg_phantom_t *ph,
	const sg_compound_world_preopen_t *resolved, const vec3_t destination,
	qboolean destination_water, float old_frame_z,
	sg_compound_swim_recovery_proof_t *proof, edict_t *passent)
{
	return SG_OracleCompoundSwimLiveSuffix(ph, resolved, destination,
		destination_water, old_frame_z, proof, passent,
		SG_ORACLE_COMPOUND_SUFFIX_OUTSIDE);
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

static qboolean SG_OracleLiftSwimSupported(const sg_phantom_t *ph,
	edict_t *platform)
{
	vec3_t mins = { -16.0f, -16.0f, -24.0f };
	vec3_t maxs = { 16.0f, 16.0f, 32.0f };
	vec3_t end;
	trace_t trace;

	if (!ph || !platform || !sg_host.trace)
		return false;
	VectorCopy(ph->origin, end);
	end[2] -= 4.0f;
	trace = sg_host.trace((vec_t *)ph->origin, mins, maxs, end,
	    sg_oracle_passent, MASK_PLAYERSOLID);
	return !trace.startsolid && !trace.allsolid && trace.fraction < 1.0f &&
	       trace.ent == platform && trace.plane.normal[2] >= 0.7f;
}

/* A water-entry lift owns only the exact synthesized center trigger and the
 * matched platform support.  The shared swim controller may reach the trigger
 * from deep water, but declaration begins only when that same observation is
 * also a valid rider state.  Existing dry lifts retain their planar oracle. */
qboolean SG_OracleLiftSwimApproach(sg_phantom_t *ph,
	const vec3_t anchor, edict_t *entry, edict_t *platform,
	float old_frame_z, sg_swim_proof_t *proof, edict_t *passent,
	qboolean world_only)
{
	usercmd_t cmd;
	edict_t *old_passent = sg_oracle_passent;
	edict_t *old_expected = sg_oracle_declared_expected;
	edict_t *old_entry = sg_oracle_declared_entry;
	qboolean old_world = sg_oracle_world_only;
	qboolean old_contaminated = sg_oracle_contaminated;
	qboolean old_touched = sg_oracle_declared_touched;
	int old_action = sg_oracle_declared_action;
	qboolean ok = false;
	int elapsed;

	if (!ph || !anchor || !entry || !entry->inuse || !platform ||
	    !platform->inuse || !proof || ph->waterlevel < 2 ||
	    !(ph->watertype & CONTENTS_WATER) ||
	    (ph->watertype & (CONTENTS_LAVA | CONTENTS_SLIME)))
		return false;
	memset(proof, 0, sizeof(*proof));
	sg_oracle_passent = passent;
	sg_oracle_world_only = world_only;
	sg_oracle_contaminated = false;
	sg_oracle_declared_expected = platform;
	sg_oracle_declared_entry = entry;
	sg_oracle_declared_action = RL_LIFT;
	sg_oracle_declared_touched = false;
	if (SG_OracleTriggerOverlap(ph) || sg_oracle_contaminated ||
	    SG_OracleSolidOverlap(ph) || SG_OracleDoorOverlap(ph))
		goto done;
	if (sg_oracle_declared_touched)
	{
		if (!SG_OracleLiftSwimSupported(ph, platform))
			goto done;
		proof->arrival_ms = 0;
		ok = true;
		goto done;
	}
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
			if (!SG_OracleLiftSwimSupported(ph, platform))
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
	sg_oracle_declared_entry = old_entry;
	sg_oracle_declared_action = old_action;
	sg_oracle_declared_touched = old_touched;
	return ok;
}


void SG_OracleRun(sg_phantom_t *ph, usercmd_t *cmd, int steps)
{
	pmove_t pm;
	int i;

	for (i = 0; i < steps; i++)
	{
		vec3_t before;

		VectorCopy(ph->origin, before);
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
		ph->groundentity_entity = pm.groundentity;
		ph->watertype = pm.watertype;
		ph->waterlevel = pm.waterlevel;
		VectorCopy(pm.mins, ph->mins);
		VectorCopy(pm.maxs, ph->maxs);

		/* decode the fixed-point state once per step so callers read floats */
		ph->origin[0] = pm.s.origin[0] * 0.125f;
		ph->origin[1] = pm.s.origin[1] * 0.125f;
		ph->origin[2] = pm.s.origin[2] * 0.125f;
		ph->velocity[0] = pm.s.velocity[0] * 0.125f;
		ph->velocity[1] = pm.s.velocity[1] * 0.125f;
		ph->velocity[2] = pm.s.velocity[2] * 0.125f;
		/* Armed-door passage is a property of the accepted body segment.  The
		 * trace adapter already blocks every unarmed candidate, but retain this
		 * fail-closed check across the decoded authoritative result. */
		if (sg_oracle_world_only &&
		    SG_OracleDoorTraceBlocked(ph, before, pm.mins, pm.maxs,
		                              ph->origin, true))
			sg_oracle_contaminated = true;
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
	qboolean initial_clear, clean;

	if (!sg_host.box_edicts)
		return false;
	sg_oracle_passent = NULL;
	sg_oracle_world_only = true;
	sg_oracle_contaminated = false;
	initial_clear = !SG_OracleTriggerOverlap(ph) && !SG_OracleSolidOverlap(ph);
	if (initial_clear)
		SG_OracleRun(ph, cmd, steps);
	clean = initial_clear && !sg_oracle_contaminated;
	sg_oracle_passent = previous_passent;
	sg_oracle_world_only = previous_world_only;
	sg_oracle_contaminated = previous_contaminated;
	return clean;
}

qboolean SG_OraclePushFlight(const vec3_t source, edict_t *trigger,
	const float push_velocity[3], vec3_t landing, int *arrival_ms)
{
	edict_t *old_passent = sg_oracle_passent;
	edict_t *old_expected = sg_oracle_declared_expected;
	qboolean old_world = sg_oracle_world_only;
	qboolean old_contaminated = sg_oracle_contaminated;
	qboolean old_touched = sg_oracle_declared_touched;
	int old_action = sg_oracle_declared_action;
	sg_phantom_t ph;
	usercmd_t command;
	vec3_t target;
	qboolean airborne = false;
	qboolean launched = false;
	qboolean ok = false;
	int elapsed = 0;
	int axis;
	float scale;

	if (!source || !trigger || !push_velocity || !landing || !arrival_ms ||
	    !trigger->inuse || !trigger->classname ||
	    strcmp(trigger->classname, "trigger_push") != 0 ||
	    trigger->touch != trigger_push_touch || trigger->solid != SOLID_TRIGGER ||
	    trigger->spawnflags != 0 || trigger->speed != 85.0f)
		return false;
	scale = trigger->speed * 10.0f;
	for (axis = 0; axis < 3; axis++)
	{
		float stock = trigger->movedir[axis] * scale;
		float fixed;

		if (!isfinite(push_velocity[axis]) ||
		    memcmp(&stock, &push_velocity[axis], sizeof(stock)) != 0)
			return false;
		fixed = push_velocity[axis] * 8.0f;
		if (!isfinite(fixed) || fixed < (float)SHRT_MIN ||
		    fixed > (float)SHRT_MAX)
			return false;
	}

	sg_oracle_passent = NULL;
	sg_oracle_world_only = true;
	sg_oracle_contaminated = false;
	sg_oracle_declared_expected = trigger;
	sg_oracle_declared_action = RL_PUSH;
	sg_oracle_declared_touched = false;
	SG_OraclePlace(&ph, (vec_t *)source);
	memset(&command, 0, sizeof(command));
	command.msec = 0;
	SG_OracleRun(&ph, &command, 1);
	if (sg_oracle_contaminated || SG_OracleDoorOverlap(&ph) ||
	    !ph.groundentity || ph.waterlevel != 0)
		goto done;
	launched = sg_oracle_declared_touched;
	VectorAdd(trigger->absmin, trigger->absmax, target);
	VectorScale(target, 0.5f, target);
	if (launched)
	{
		for (axis = 0; axis < 3; axis++)
		{
			ph.pms.velocity[axis] = (short)(push_velocity[axis] * 8.0f);
			ph.velocity[axis] = ph.pms.velocity[axis] * 0.125f;
		}
	}

	for (elapsed = 0; elapsed < SG_PUSH_PROOF_LIMIT_MS;
	     elapsed += SG_RUNE_PROOF_PMOVE_SUBSTEP_MS)
	{
		memset(&command, 0, sizeof(command));
		command.msec = SG_RUNE_PROOF_PMOVE_SUBSTEP_MS;
		if (!launched && !SG_DeclaredCommand(ph.origin, target, &ph.pms,
		        &command))
			goto done;
		sg_oracle_declared_touched = false;
		SG_OracleRun(&ph, &command, 1);
		if (sg_oracle_contaminated || SG_OracleDoorOverlap(&ph) ||
		    (ph.waterlevel > 0 &&
		     (ph.watertype & (CONTENTS_LAVA | CONTENTS_SLIME))))
			goto done;
		if (sg_oracle_declared_touched)
		{
			for (axis = 0; axis < 3; axis++)
			{
				ph.pms.velocity[axis] =
					(short)(push_velocity[axis] * 8.0f);
				ph.velocity[axis] = ph.pms.velocity[axis] * 0.125f;
			}
			launched = true;
		}
		if (!launched && (!ph.groundentity || ph.waterlevel != 0))
			goto done;
		if (launched && !ph.groundentity)
			airborne = true;
		if (!launched ||
		    ((elapsed + SG_RUNE_PROOF_PMOVE_SUBSTEP_MS) % 100) != 0)
			continue;
		if (airborne && ph.groundentity && ph.waterlevel == 0)
		{
			VectorCopy(ph.origin, landing);
			*arrival_ms = elapsed + SG_RUNE_PROOF_PMOVE_SUBSTEP_MS;
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

/* A collision trace stops DIST_EPSILON short of its floor.  That float is
 * not necessarily a legal pmove coordinate: truncating floor+1/32 to signed
 * eighth units can put the standing hull back on the floor boundary.  Let
 * Pmove's own initial-snap search select the nearest legal signed-q8 body,
 * then require that exact state to be clear and grounded.  Ongoing rest
 * stability remains Seed_SourceUnstable's separate action-source law.  This
 * scope intentionally permits initial snap before checking the resulting
 * body for overlap; SG_OracleRunWorld would reject the raw, pre-snap endpoint
 * before Pmove gets that opportunity. */
qboolean SG_OracleCanonicalGroundSource(const vec3_t floor_endpoint,
	vec3_t canonical_origin)
{
	static const vec3_t player_mins = { -16.0f, -16.0f, -24.0f };
	static const vec3_t player_maxs = { 16.0f, 16.0f, 32.0f };
	edict_t *old_passent = sg_oracle_passent;
	sg_phantom_t *old_active = sg_oracle_active_phantom;
	edict_t *old_declared_expected = sg_oracle_declared_expected;
	edict_t *old_declared_door = sg_oracle_declared_door;
	const sg_rune_mechanism_binding_t *old_bound_door =
		sg_oracle_bound_door;
	edict_t *old_compound_trigger = sg_oracle_compound_trigger;
	edict_t *old_compound_member = sg_oracle_compound_member;
	qboolean old_world_only = sg_oracle_world_only;
	qboolean old_contaminated = sg_oracle_contaminated;
	qboolean old_declared_touched = sg_oracle_declared_touched;
	qboolean old_compound_touched = sg_oracle_compound_touched;
	qboolean old_loader_replay = sg_oracle_loader_replay;
	int old_declared_action = sg_oracle_declared_action;
	sg_phantom_t phantom;
	usercmd_t command;
	trace_t clear;
	qboolean ok = false;
	int axis;

	if (!floor_endpoint || !canonical_origin || !sg_host.pmove ||
	    !sg_host.trace || !sg_host.pointcontents)
		return false;
	for (axis = 0; axis < 3; axis++)
	{
		float scaled;
		int base;

		if (!isfinite(floor_endpoint[axis]))
			return false;
		scaled = floor_endpoint[axis] *
			(float)SG_RUNE_PROOF_WORLD_FIXED_SCALE;
		if (scaled < (float)SG_RUNE_PROOF_WORLD_FIXED_MIN ||
		    scaled > (float)SG_RUNE_PROOF_WORLD_FIXED_MAX)
			return false;
		base = (int)scaled;
		/* PM_InitialSnapPosition may test base-1 and base+1 on every
		 * coordinate.  Keep those signed-short candidates representable. */
		if (base <= SG_RUNE_PROOF_WORLD_FIXED_MIN ||
		    base >= SG_RUNE_PROOF_WORLD_FIXED_MAX)
			return false;
	}

	sg_oracle_passent = NULL;
	sg_oracle_active_phantom = NULL;
	sg_oracle_world_only = false;
	sg_oracle_contaminated = false;
	sg_oracle_declared_expected = NULL;
	sg_oracle_declared_door = NULL;
	sg_oracle_bound_door = NULL;
	sg_oracle_declared_action = RL_RUN;
	sg_oracle_declared_touched = false;
	sg_oracle_compound_trigger = NULL;
	sg_oracle_compound_member = NULL;
	sg_oracle_compound_touched = false;
	sg_oracle_loader_replay = false;

	SG_OraclePlace(&phantom, (vec_t *)floor_endpoint);
	/* old_pms is used only to request PM_InitialSnapPosition on this first
	 * command.  pm.s remains the intended PM_NORMAL candidate. */
	phantom.old_pms.pm_type = PM_FREEZE;
	memset(&command, 0, sizeof(command));
	command.msec = 0;
	SG_OracleRun(&phantom, &command, 1);
	if (!phantom.groundentity || phantom.pms.pm_type != PM_NORMAL)
		goto done;
	for (axis = 0; axis < 3; axis++)
	{
		float scaled = phantom.origin[axis] *
			(float)SG_RUNE_PROOF_WORLD_FIXED_SCALE;

		if (scaled != (float)phantom.pms.origin[axis])
			goto done;
	}
	clear = sg_host.trace(phantom.origin, (vec_t *)player_mins,
		(vec_t *)player_maxs, phantom.origin, NULL, MASK_PLAYERSOLID);
	if (clear.startsolid || clear.allsolid)
		goto done;

	VectorCopy(phantom.origin, canonical_origin);
	ok = true;

done:
	sg_oracle_passent = old_passent;
	sg_oracle_active_phantom = old_active;
	sg_oracle_world_only = old_world_only;
	sg_oracle_contaminated = old_contaminated;
	sg_oracle_declared_expected = old_declared_expected;
	sg_oracle_declared_door = old_declared_door;
	sg_oracle_bound_door = old_bound_door;
	sg_oracle_declared_action = old_declared_action;
	sg_oracle_declared_touched = old_declared_touched;
	sg_oracle_compound_trigger = old_compound_trigger;
	sg_oracle_compound_member = old_compound_member;
	sg_oracle_compound_touched = old_compound_touched;
	sg_oracle_loader_replay = old_loader_replay;
	return ok;
}

/* Prove the exact planar controller from a static graph source until it first
 * overlaps the one trigger owned by the declared pad/platform. The expected
 * solid still participates in Pmove collision; it is merely deterministic,
 * while every other non-world solid/trigger remains contamination. */
static qboolean SG_OracleDeclaredApproachInternal(const vec3_t source,
	const vec3_t target, edict_t *entry, edict_t *support,
	edict_t *approach_door, edict_t *required_ground, int action,
	int *arrival_ms, vec3_t contact_out, sg_phantom_t *arrival_out)
{
	edict_t *old_passent = sg_oracle_passent;
	edict_t *old_expected = sg_oracle_declared_expected;
	edict_t *old_entry = sg_oracle_declared_entry;
	edict_t *old_door = sg_oracle_declared_door;
	qboolean old_world = sg_oracle_world_only;
	qboolean old_contaminated = sg_oracle_contaminated;
	qboolean old_touched = sg_oracle_declared_touched;
	int old_action = sg_oracle_declared_action;
	sg_phantom_t ph;
	usercmd_t cmd;
	qboolean ok = false;
	int elapsed = 0;

	if (!entry || !entry->inuse || !support || !support->inuse || !arrival_ms ||
	    (action != RL_LIFT && action != RL_TELEPORT && action != RL_TRAIN))
		return false;
	sg_oracle_passent = NULL;
	sg_oracle_world_only = true;
	sg_oracle_contaminated = false;
	sg_oracle_declared_expected = support;
	sg_oracle_declared_entry = entry;
	sg_oracle_declared_door = approach_door;
	sg_oracle_declared_action = action;
	sg_oracle_declared_touched = false;
	SG_OraclePlace(&ph, (vec_t *)source);
	memset(&cmd, 0, sizeof(cmd));
	cmd.msec = 0;
	SG_OracleRun(&ph, &cmd, 1);
	/* A staging seed already inside the mechanism trigger is not a separate,
	 * routable source phase. */
	if (sg_oracle_contaminated || sg_oracle_declared_touched ||
	    SG_OracleDoorOverlap(&ph) || !ph.groundentity || ph.waterlevel != 0)
	{
		goto done;
	}
	for (elapsed = 0; elapsed < 3000; elapsed += 25)
	{
		memset(&cmd, 0, sizeof(cmd));
		cmd.msec = 25;
		if (!SG_DeclaredCommand(ph.origin, target, &ph.pms, &cmd))
		{
			goto done;
		}
		SG_OracleRun(&ph, &cmd, 1);
		if (sg_oracle_contaminated || SG_OracleDoorOverlap(&ph) ||
		    !ph.groundentity || ph.waterlevel != 0)
		{
			goto done;
		}
		if (sg_oracle_declared_touched)
		{
			if (required_ground && ph.groundentity_entity != required_ground)
				goto done;
			*arrival_ms = elapsed + 25;
			if (contact_out)
				VectorCopy(ph.origin, contact_out);
			if (arrival_out)
				*arrival_out = ph;
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
	sg_oracle_declared_entry = old_entry;
	sg_oracle_declared_door = old_door;
	sg_oracle_declared_action = old_action;
	sg_oracle_declared_touched = old_touched;
	return ok;
}

qboolean SG_OracleDeclaredApproach(const vec3_t source, const vec3_t target,
	edict_t *entry, edict_t *support, int action, int *arrival_ms)
{
	return SG_OracleDeclaredApproachInternal(source, target, entry, support,
		NULL, NULL, action, arrival_ms, NULL, NULL);
}

qboolean SG_OracleTrainGateApproach(const vec3_t source,
	const vec3_t target, edict_t *button, int *arrival_ms,
	vec3_t contact_out)
{
	if (contact_out)
		VectorClear(contact_out);
	return button && contact_out &&
	       SG_OracleDeclaredApproachInternal(source, target, button, button,
	           NULL, NULL, RL_TRAIN, arrival_ms, contact_out, NULL);
}

qboolean SG_OracleTrainRideBoard(const vec3_t source,
	const vec3_t target, edict_t *button, edict_t *train,
	int *arrival_ms, vec3_t contact_out)
{
	edict_t *old_passent = sg_oracle_passent;
	edict_t *old_expected = sg_oracle_declared_expected;
	edict_t *old_entry = sg_oracle_declared_entry;
	edict_t *old_door = sg_oracle_declared_door;
	qboolean old_world = sg_oracle_world_only;
	qboolean old_contaminated = sg_oracle_contaminated;
	qboolean old_touched = sg_oracle_declared_touched;
	int old_action = sg_oracle_declared_action;
	sg_phantom_t ph;
	sg_phantom_t activated;
	usercmd_t cmd;
	vec3_t travel;
	vec3_t board_target;
	double dwell_ms;
	int activation_ms;
	int elapsed;
	int dwell_limit;
	int candidate;
	int jump_ms;
	qboolean ok = false;

	if (contact_out)
		VectorClear(contact_out);
	if (!button || !train || !contact_out || !arrival_ms || !button->inuse ||
	    !train->inuse || !button->classname || !train->classname ||
	    strcmp(button->classname, "func_button") != 0 ||
	    strcmp(train->classname, "func_train") != 0 ||
	    button->touch != button_touch || button->use != button_use ||
	    !train->use || !button->target || !train->targetname ||
	    strcmp(button->target, train->targetname) != 0 ||
	    !isfinite(button->moveinfo.speed) || button->moveinfo.speed <= 0.0f)
		return false;
	VectorSubtract(button->moveinfo.end_origin,
	    button->moveinfo.start_origin, travel);
	dwell_ms = (double)VectorLength(travel) /
	    (double)button->moveinfo.speed * 1000.0;
	/* button_wait dispatches the train only after this translation completes.
	 * The floor of distance/max-speed is a conservative real lower dwell: the
	 * stock acceleration profile cannot reach the endpoint sooner. */
	if (!isfinite(dwell_ms) || dwell_ms < 25.0 || dwell_ms > 3000.0)
		return false;
	if (!SG_OracleDeclaredApproachInternal(source, target, button, button,
	        train, NULL, RL_TRAIN, &activation_ms, NULL, &ph))
		return false;
	activated = ph;
	dwell_limit = (int)floor(dwell_ms);
	sg_oracle_passent = NULL;
	sg_oracle_world_only = true;
	sg_oracle_contaminated = false;
	sg_oracle_declared_expected = button;
	sg_oracle_declared_entry = button;
	sg_oracle_declared_door = train;
	sg_oracle_declared_action = RL_TRAIN;
	sg_oracle_declared_touched = false;
	for (candidate = 0; candidate < 6 && !ok; candidate++)
	{
		for (jump_ms = 0; jump_ms <= dwell_limit && !ok; jump_ms += 100)
		{
			int stable_ms = 0;
			qboolean trial_clean = true;

			ph = activated;
			sg_oracle_contaminated = false;
			for (elapsed = 25; elapsed <= dwell_limit; elapsed += 25)
			{
				float xlo;
				float xhi;
				float ylo;
				float yhi;

				xlo = train->absmin[0] + 16.125f;
				xhi = train->absmax[0] - 16.125f;
				ylo = train->absmin[1] + 16.125f;
				yhi = train->absmax[1] - 16.125f;
				if (xlo > xhi || ylo > yhi)
				{
					trial_clean = false;
					break;
				}
				if (candidate == 0)
				{
					board_target[0] = ph.origin[0] < xlo ? xlo :
					    ph.origin[0] > xhi ? xhi : ph.origin[0];
					board_target[1] = ph.origin[1] < ylo ? ylo :
					    ph.origin[1] > yhi ? yhi : ph.origin[1];
				}
				else if (candidate == 1)
				{
					board_target[0] = (xlo + xhi) * 0.5f;
					board_target[1] = (ylo + yhi) * 0.5f;
				}
				else
				{
					board_target[0] = (candidate & 1) ? xhi : xlo;
					board_target[1] = (candidate & 2) ? yhi : ylo;
				}
				board_target[2] = train->absmax[2] + 24.125f;
				memset(&cmd, 0, sizeof(cmd));
				cmd.msec = 25;
				if (stable_ms == 0 &&
				    !SG_DeclaredCommand(ph.origin, board_target, &ph.pms, &cmd))
				{
					trial_clean = false;
					break;
				}
				if (stable_ms == 0 && elapsed >= jump_ms &&
				    elapsed < jump_ms + 50)
					cmd.upmove = 400;
				SG_OracleRun(&ph, &cmd, 1);
				if (sg_oracle_contaminated || SG_OracleDoorOverlap(&ph) ||
				    ph.waterlevel != 0)
				{
					trial_clean = false;
					break;
				}
				if (ph.groundentity_entity != train)
				{
					stable_ms = 0;
					continue;
				}
				stable_ms += 25;
				if (stable_ms < 100)
					continue;
				*arrival_ms = activation_ms + elapsed;
				VectorCopy(ph.origin, contact_out);
				ok = true;
				break;
			}
			if (!trial_clean)
				continue;
		}
	}

	if (ph.door_passed)
		ok = false;
	sg_oracle_passent = old_passent;
	sg_oracle_world_only = old_world;
	sg_oracle_contaminated = old_contaminated;
	sg_oracle_declared_expected = old_expected;
	sg_oracle_declared_entry = old_entry;
	sg_oracle_declared_door = old_door;
	sg_oracle_declared_action = old_action;
	sg_oracle_declared_touched = old_touched;
	return ok;
}

qboolean SG_OracleTrainRideCarry(const vec3_t source,
	const vec3_t displacement, edict_t *train, vec3_t destination_out)
{
	vec3_t mins = { -16.0f, -16.0f, -24.0f };
	vec3_t maxs = { 16.0f, 16.0f, 32.0f };
	trace_t trace;
	int axis;

	if (destination_out)
		VectorClear(destination_out);
	if (!source || !displacement || !train || !train->inuse ||
	    !destination_out || !SG_OracleFinite3(source) ||
	    !SG_OracleFinite3(displacement) || fabsf(displacement[0]) > 0.125f ||
	    fabsf(displacement[1]) > 0.125f || displacement[2] < 8.0f)
		return false;
	for (axis = 0; axis < 3; axis++)
		destination_out[axis] = source[axis] + displacement[axis];
	if (!SG_OracleFinite3(destination_out))
		return false;
	trace = sg_host.trace((vec_t *)source, mins, maxs, destination_out,
	    train, MASK_PLAYERSOLID);
	if (trace.startsolid || trace.allsolid || trace.fraction < 1.0f)
	{
		VectorClear(destination_out);
		return false;
	}
	return true;
}

qboolean SG_OracleTrainGateShot(const vec3_t source, edict_t *button,
	vec3_t contact_out, int *flight_ms)
{
	vec3_t aim;
	vec3_t angles;
	vec3_t forward;
	vec3_t muzzle;
	vec3_t end;
	trace_t trace;

	if (contact_out)
		VectorClear(contact_out);
	if (flight_ms)
		*flight_ms = 0;
	if (!source || !button || !button->inuse || !contact_out || !flight_ms ||
	    !button->classname || strcmp(button->classname, "func_button") ||
	    !button->takedamage || button->health != 1 ||
	    button->max_health != 1 || button->die != button_killed)
		return false;
	aim[0] = (button->absmin[0] + button->absmax[0]) * 0.5f;
	aim[1] = (button->absmin[1] + button->absmax[1]) * 0.5f;
	aim[2] = (button->absmin[2] + button->absmax[2]) * 0.5f;
	if (!SG_BlasterAimAngles(source, 22.0f, RIGHT_HANDED, aim, angles,
	        muzzle))
		return false;
	AngleVectors(angles, forward, NULL, NULL);
	VectorMA(muzzle, 8192.0f, forward, end);
	trace = sg_host.trace(muzzle, NULL, NULL, end, NULL, MASK_SHOT);
	if (trace.ent != button || trace.startsolid || trace.allsolid ||
	    trace.fraction <= 0.0f || trace.fraction >= 1.0f)
		return false;
	VectorCopy(trace.endpos, contact_out);
	VectorSubtract(contact_out, muzzle, end);
	*flight_ms = (int)ceilf(VectorLength(end));
	return *flight_ms > 0 && *flight_ms <= RUNE_MAX_COST_MS;
}

qboolean SG_OracleDeclaredCompoundLiftApproach(const vec3_t source,
	const vec3_t target, edict_t *entry, edict_t *support,
	edict_t *approach_door, int *arrival_ms)
{
	uint32_t delay_ms;

	return approach_door &&
	       (SG_DeclaredDoorActivatorSafe(approach_door) ||
	        SG_DeclaredDoorDelayedActivatorSafe(approach_door, &delay_ms)) &&
	       SG_OracleDeclaredApproachInternal(source, target, entry, support,
	           approach_door, NULL, RL_LIFT, arrival_ms, NULL, NULL);
}

static qboolean SG_TrainGateHullOutside(const vec3_t origin,
	const vec3_t sweep_mins, const vec3_t sweep_maxs)
{
	return origin[0] + 16.0f <= sweep_mins[0] ||
	       origin[0] - 16.0f >= sweep_maxs[0] ||
	       origin[1] + 16.0f <= sweep_mins[1] ||
	       origin[1] - 16.0f >= sweep_maxs[1] ||
	       origin[2] + 32.0f <= sweep_mins[2] ||
	       origin[2] - 24.0f >= sweep_maxs[2];
}

static qboolean SG_TrainGateHullOutsideAxis(const vec3_t origin,
	const vec3_t sweep_mins, const vec3_t sweep_maxs,
	unsigned int passage_axis)
{
	static const float hull_mins[3] = { -16.0f, -16.0f, -24.0f };
	static const float hull_maxs[3] = { 16.0f, 16.0f, 32.0f };

	if (passage_axis >= 3U)
		return SG_TrainGateHullOutside(origin, sweep_mins, sweep_maxs);
	return origin[passage_axis] + hull_maxs[passage_axis] <=
	           sweep_mins[passage_axis] ||
	       origin[passage_axis] + hull_mins[passage_axis] >=
	           sweep_maxs[passage_axis];
}

static qboolean SG_OracleTrainGateMove(const vec3_t source,
	const vec3_t target, edict_t *button, edict_t *train,
	const vec3_t sweep_mins, const vec3_t sweep_maxs,
	unsigned int passage_axis, qboolean require_cross, int *arrival_ms,
	vec3_t observed_landing)
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
	qboolean entered_sweep = false;
	qboolean ok = false;
	int source_side = 0;
	int elapsed;

	if (!source || !target || !button || !button->inuse || !train ||
	    !train->inuse || !arrival_ms ||
	    (require_cross && (!sweep_mins || !sweep_maxs || passage_axis > 3U)))
		return false;
	*arrival_ms = 0;
	sg_oracle_passent = NULL;
	sg_oracle_world_only = true;
	sg_oracle_contaminated = false;
	sg_oracle_declared_expected = button;
	sg_oracle_declared_door = train;
	sg_oracle_declared_action = RL_TRAIN;
	sg_oracle_declared_touched = false;
	SG_OraclePlace(&ph, (vec_t *)source);
	memset(&cmd, 0, sizeof(cmd));
	SG_OracleRun(&ph, &cmd, 1);
	if (sg_oracle_contaminated || !ph.groundentity || ph.waterlevel != 0)
		goto done;
	if (require_cross)
	{
		entered_sweep = !SG_TrainGateHullOutsideAxis(ph.origin, sweep_mins,
		    sweep_maxs, passage_axis);
		if (observed_landing &&
		    ph.origin[passage_axis] + ph.maxs[passage_axis] <=
		    sweep_mins[passage_axis])
			source_side = -1;
		else if (observed_landing &&
		         ph.origin[passage_axis] + ph.mins[passage_axis] >=
		    sweep_maxs[passage_axis])
			source_side = 1;
	}
	for (elapsed = 0; elapsed < 5000; elapsed += 25)
	{
		memset(&cmd, 0, sizeof(cmd));
		cmd.msec = 25;
		if (!SG_DeclaredCommand(ph.origin, target, &ph.pms, &cmd))
			goto done;
		if (observed_landing)
		{
			float gap = 0.0f;

			if (ph.origin[passage_axis] + ph.maxs[passage_axis] <=
			    sweep_mins[passage_axis])
				gap = sweep_mins[passage_axis] -
				    (ph.origin[passage_axis] + ph.maxs[passage_axis]);
			else if (ph.origin[passage_axis] + ph.mins[passage_axis] >=
			    sweep_maxs[passage_axis])
				gap = ph.origin[passage_axis] + ph.mins[passage_axis] -
				    sweep_maxs[passage_axis];
			if (gap * 8.0f <= SG_SHOOT_DOOR_JUMP_APPROACH_Q8)
				cmd.upmove = 400;
		}
		SG_OracleRun(&ph, &cmd, 1);
		if (sg_oracle_contaminated ||
		    (ph.waterlevel > 0 &&
		     (ph.watertype & (CONTENTS_LAVA | CONTENTS_SLIME))))
			goto done;
		if (require_cross &&
		    !SG_TrainGateHullOutsideAxis(ph.origin, sweep_mins, sweep_maxs,
		        passage_axis))
			entered_sweep = true;
		if (((elapsed + 25) % 100) == 0 &&
		    (!require_cross ||
		     (entered_sweep &&
		      SG_TrainGateHullOutsideAxis(ph.origin, sweep_mins, sweep_maxs,
		          passage_axis))) &&
		    SG_SupportedArrived(ph.origin, target, ph.groundentity,
		        ph.watertype, ph.waterlevel, NULL))
		{
			*arrival_ms = elapsed + 25;
			if (observed_landing)
				VectorCopy(ph.origin, observed_landing);
			ok = true;
			goto done;
		}
	}
	if (observed_landing && require_cross && entered_sweep &&
	    source_side != 0 && !sg_oracle_contaminated &&
	    ph.groundentity && ph.waterlevel == 0 &&
	    ((source_side < 0 &&
	      ph.origin[passage_axis] + ph.mins[passage_axis] >=
	          sweep_maxs[passage_axis]) ||
	     (source_side > 0 &&
	      ph.origin[passage_axis] + ph.maxs[passage_axis] <=
	          sweep_mins[passage_axis])))
	{
		VectorCopy(ph.origin, observed_landing);
		*arrival_ms = 5000;
		ok = true;
	}

done:
	if (ph.door_passed)
		ok = false;
	sg_oracle_passent = old_passent;
	sg_oracle_world_only = old_world;
	sg_oracle_contaminated = old_contaminated;
	sg_oracle_declared_expected = old_expected;
	sg_oracle_declared_door = old_door;
	sg_oracle_declared_action = old_action;
	sg_oracle_declared_touched = old_touched;
	return ok;
}

qboolean SG_OracleTrainGateEntry(const vec3_t source,
	const vec3_t entry, edict_t *button, edict_t *train, int *arrival_ms)
{
	return SG_OracleTrainGateMove(source, entry, button, train, NULL, NULL,
	    0U, false, arrival_ms, NULL);
}

qboolean SG_OracleTrainGateCross(const vec3_t entry,
	const vec3_t target, edict_t *button, edict_t *train,
	const vec3_t sweep_mins, const vec3_t sweep_maxs,
	unsigned int passage_axis, int *arrival_ms)
{
	return SG_OracleTrainGateMove(entry, target, button, train, sweep_mins,
	    sweep_maxs, passage_axis, true, arrival_ms, NULL);
}

qboolean SG_OracleShootDoorCross(const vec3_t entry,
	const vec3_t target, edict_t *button, edict_t *train,
	const vec3_t sweep_mins, const vec3_t sweep_maxs,
	unsigned int passage_axis, int *arrival_ms, vec3_t landing)
{
	return SG_OracleTrainGateMove(entry, target, button, train, sweep_mins,
	    sweep_maxs, passage_axis, true, arrival_ms, landing);
}

qboolean SG_OracleTrainGateExit(const vec3_t cross,
	const vec3_t target, edict_t *button, edict_t *train, int *arrival_ms)
{
	return SG_OracleTrainGateMove(cross, target, button, train, NULL, NULL,
	    0U, false, arrival_ms, NULL);
}

/* Prove the lift's top-platform-to-static-graph handoff. The caller positions
 * the resolved platform at its authoritative top for this synchronous scope.
 * Its solid and center trigger are admitted, but success requires the player
 * hull to have left the platform footprint and reached the same supported
 * endpoint predicate used live. */
static qboolean SG_OracleDeclaredEgressInternal(const vec3_t source,
	const vec3_t target, edict_t *support, edict_t *egress_trigger,
	int action, int *arrival_ms)
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
	int elapsed = 0;

	if (!support || !support->inuse || !arrival_ms ||
	    (action != RL_LIFT && action != RL_TRAIN))
		return false;
	sg_oracle_passent = NULL;
	sg_oracle_world_only = true;
	sg_oracle_contaminated = false;
	sg_oracle_declared_expected = support;
	sg_oracle_declared_door = egress_trigger;
	sg_oracle_declared_action = action;
	sg_oracle_declared_touched = false;
	SG_OraclePlace(&ph, (vec_t *)source);
	memset(&cmd, 0, sizeof(cmd));
	cmd.msec = 0;
	SG_OracleRun(&ph, &cmd, 1);
	if (sg_oracle_contaminated || SG_OracleDoorOverlap(&ph) ||
	    !ph.groundentity || ph.waterlevel != 0)
	{
		goto done;
	}
	for (elapsed = 0; elapsed < 3000; elapsed += 25)
	{
		qboolean outside;

		memset(&cmd, 0, sizeof(cmd));
		cmd.msec = 25;
		if (!SG_DeclaredCommand(ph.origin, target, &ph.pms, &cmd))
		{
			goto done;
		}
		SG_OracleRun(&ph, &cmd, 1);
		if (sg_oracle_contaminated || SG_OracleDoorOverlap(&ph) ||
		    (ph.waterlevel > 0 &&
		     (ph.watertype & (CONTENTS_LAVA | CONTENTS_SLIME))))
		{
			goto done;
		}
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
	sg_oracle_declared_door = old_door;
	sg_oracle_declared_action = old_action;
	sg_oracle_declared_touched = old_touched;
	return ok;
}

qboolean SG_OracleDeclaredEgress(const vec3_t source, const vec3_t target,
	edict_t *support, int *arrival_ms)
{
	return SG_OracleDeclaredEgressInternal(source, target, support, NULL,
		RL_LIFT, arrival_ms);
}

qboolean SG_OracleTrainRideEgress(const vec3_t source,
	const vec3_t target, edict_t *train, int *arrival_ms)
{
	vec3_t mins = { -16.0f, -16.0f, -24.0f };
	vec3_t maxs = { 16.0f, 16.0f, 32.0f };
	vec3_t end;
	trace_t trace;

	if (!source || !target || !train || !train->inuse || !arrival_ms)
		return false;
	VectorCopy(source, end);
	end[2] -= 4.0f;
	trace = sg_host.trace((vec_t *)source, mins, maxs, end, NULL,
	    MASK_PLAYERSOLID);
	if (trace.startsolid || trace.allsolid || trace.fraction >= 1.0f ||
	    trace.ent != train || trace.plane.normal[2] < 0.7f)
		return false;
	return SG_OracleDeclaredEgressInternal(source, target, train, NULL,
		RL_TRAIN, arrival_ms);
}

qboolean SG_OracleDeclaredCompoundLiftEgress(const vec3_t source,
	const vec3_t target, edict_t *support, edict_t *egress_trigger,
	int *arrival_ms)
{
	uint32_t delay_ms;
	qboolean delayed;

	delayed = SG_DeclaredDoorDelayedActivatorSafe(egress_trigger, &delay_ms);
	return (delayed || SG_DeclaredDoorActivatorSafe(egress_trigger)) &&
	       (delayed
	            ? SG_DeclaredDelayedDoorOutsideSweep(egress_trigger, source) &&
	              SG_DeclaredDelayedDoorOutsideSweep(egress_trigger, target)
	            : SG_DeclaredDoorOutsideSweep(egress_trigger, source) &&
	              SG_DeclaredDoorOutsideSweep(egress_trigger, target)) &&
	       SG_OracleDeclaredEgressInternal(source, target, support,
	           egress_trigger, RL_LIFT, arrival_ms);
}

qboolean SG_OracleDoorApproachContactObserved(qboolean button_controller,
	qboolean physical_touch, qboolean bound_contact)
{
	/* The bound button contact predicate is a four-unit inward lookahead used
	 * to authenticate the serialized anchor.  It can become true one Pmove
	 * sample before the solid BSP callback.  Only the callback is the physical
	 * activation/support event proved by generation; ordinary declared trigger
	 * volumes retain their bound containment fallback. */
	return physical_touch || (!button_controller && bound_contact);
}

qboolean SG_OracleDoorShallowWadeSafe(int waterlevel, int watertype)
{
	return SG_DoorApproachWaterSafe(waterlevel, watertype) ? true : false;
}

qboolean SG_OracleDoorEgressWaterSafe(int controller_kind, int waterlevel,
	int watertype)
{
	if (controller_kind == SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR)
		return SG_OracleDoorShallowWadeSafe(waterlevel, watertype);
	return waterlevel == 0 &&
	    !(watertype & (CONTENTS_LAVA | CONTENTS_SLIME));
}

static qboolean SG_OracleDoorApproachStaticSupport(const sg_phantom_t *ph)
{
	return ph && ph->groundentity &&
	       (ph->groundentity_entity == g_edicts ||
	        SG_ImmutableSupport(ph->groundentity_entity));
}

static void SG_OracleDoorApproachObservation(const sg_phantom_t *ph,
	edict_t *trigger, const sg_rune_mechanism_binding_t *binding,
	qboolean fall_sampled, float fall_delta,
	sg_door_approach_observation_t *observation)
{
	memset(observation, 0, sizeof(*observation));
	if (!ph)
		return;
	observation->pms = ph->pms;
	observation->grounded = ph->groundentity ? 1 : 0;
	observation->static_support =
	    SG_OracleDoorApproachStaticSupport(ph) ? 1 : 0;
	observation->watertype = ph->watertype;
	observation->waterlevel = ph->waterlevel;
	observation->hazardous_liquid =
	    (ph->watertype & (CONTENTS_LAVA | CONTENTS_SLIME)) != 0;
	observation->population_stable = !sg_oracle_contaminated;
	observation->sweep_clear = binding
	    ? SG_BoundDoorOutsideSweep(binding, ph->origin)
	    : SG_DeclaredDoorOutsideSweep(trigger, ph->origin);
	observation->physical_touch = sg_oracle_declared_touched;
	observation->fall_sampled = fall_sampled;
	observation->fall_delta = fall_delta;
}

/* Phase one of RL_DOOR: walk from a connected dry graph seed to one exact
 * sweep-clear wait point inside the validated activator. The expected trigger
 * is the only admitted side effect; the door set itself remains in the
 * synthetic full-sweep audit, so this path is safe for every current pose.
 * Arrival is a realizable rest state within the same two-unit canonicalization
 * envelope used live. */
static qboolean SG_OracleDoorApproach(const vec3_t source,
	const vec3_t wait_point, edict_t *trigger,
	const sg_rune_mechanism_binding_t *binding, int *arrival_ms,
	int *touch_ms, sg_button_support_mode_t *support_mode)
{
	edict_t *old_passent = sg_oracle_passent;
	edict_t *old_expected = sg_oracle_declared_expected;
	edict_t *old_door = sg_oracle_declared_door;
	const sg_rune_mechanism_binding_t *old_bound = sg_oracle_bound_door;
	qboolean old_world = sg_oracle_world_only;
	qboolean old_contaminated = sg_oracle_contaminated;
	qboolean old_touched = sg_oracle_declared_touched;
	int old_action = sg_oracle_declared_action;
	sg_phantom_t ph, exact;
	usercmd_t cmd;
	qboolean ok = false;
	qboolean triggered = false;
	qboolean button_controller;
	qboolean direct_controller;
	qboolean delayed_controller;
	qboolean member_scope_active = false;
	qboolean bounds_cache_active = false;
	int controller_kind;
	sg_button_support_mode_t first_support = SG_BUTTON_SUPPORT_NONE;
	sg_door_approach_state_t direct_state;
	sg_door_approach_observation_t direct_observation;
	sg_door_approach_result_t direct_result;
	int elapsed, resume_ms = 0, first_touch_ms = 0;

	button_controller = binding
	    ? binding->plan && binding->plan->controller_kind ==
	          SG_MECHANISM_CONTROLLER_BUTTON_DOOR
	    : SG_OracleDeclaredButtonDoorSafe(trigger);
	direct_controller = binding
	    ? binding->plan && binding->plan->controller_kind ==
	          SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR
	    : !button_controller && SG_DeclaredDoorDirectActivatorSafe(trigger);
	{
		uint32_t delay_ms;

		delayed_controller = !binding && !button_controller &&
		    SG_DeclaredDoorDelayedActivatorSafe(trigger, &delay_ms);
	}
	controller_kind = button_controller
	    ? SG_MECHANISM_CONTROLLER_BUTTON_DOOR
	    : (direct_controller
	          ? SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR
	          : SG_MECHANISM_CONTROLLER_AUTO_DOOR);
	if (support_mode)
		*support_mode = SG_BUTTON_SUPPORT_NONE;
	if (!trigger || !arrival_ms || !touch_ms ||
	    (button_controller && !support_mode) ||
	    (binding ? (!SG_OracleBoundDoorBindingCurrent(binding) ||
	                binding->entry_entity != trigger ||
	                !SG_BoundDoorOutsideSweep(binding, source) ||
	                !SG_BoundDoorOutsideSweep(binding, wait_point) ||
	                !SG_BoundDoorEntryContactMatches(binding, wait_point)) :
	               (button_controller
	                    ? (!SG_DeclaredButtonDoorApproachSourceClear(trigger,
	                          source) ||
	                       !SG_DeclaredButtonDoorContactMatches(trigger,
	                          wait_point))
	                    : delayed_controller
	                        ? (!SG_DeclaredDelayedDoorApproachSourceClear(
	                              trigger, source) ||
	                           !SG_DeclaredDelayedDoorForLinkMatches(trigger,
	                              wait_point, source))
	                        : !SG_DeclaredDoorSameSet(
	                              SG_DeclaredDoorForLink(wait_point, source),
	                              trigger))))
		return false;
	if (binding && sg_oracle_loader_replay)
	{
		if (!SG_OracleBoundMemberScopeBegin(binding))
			return false;
		member_scope_active = true;
		SG_OracleDoorBoundsCacheBegin();
		bounds_cache_active = true;
	}
	sg_oracle_passent = NULL;
	sg_oracle_world_only = true;
	sg_oracle_contaminated = false;
	sg_oracle_declared_expected = trigger;
	sg_oracle_declared_door = NULL; /* keep the complete mover sweep physical */
	sg_oracle_bound_door = binding;
	sg_oracle_declared_action = button_controller
	    ? RL_BUTTON_DOOR : RL_DOOR;
	sg_oracle_declared_touched = false;
	SG_OraclePlace(&ph, (vec_t *)source);
	memset(&cmd, 0, sizeof(cmd));
	cmd.msec = 0;
	if (!SG_OracleRunWorld(&ph, &cmd, 1) || sg_oracle_declared_touched ||
	    (binding && SG_BoundDoorEntryContactMatches(binding, ph.origin)) ||
	    !ph.groundentity ||
	    !SG_OracleDoorEgressWaterSafe(controller_kind, ph.waterlevel,
	        ph.watertype))
		goto done;
	memset(&direct_state, 0, sizeof(direct_state));
	memset(&direct_result, 0, sizeof(direct_result));
	if (direct_controller)
	{
		short source_q8[3], anchor_q8[3];
		int axis;

		for (axis = 0; axis < 3; axis++)
		{
			source_q8[axis] = (short)(source[axis] * 8.0f);
			anchor_q8[axis] = (short)(wait_point[axis] * 8.0f);
		}
		SG_OracleDoorApproachObservation(&ph, trigger, binding, false, 0.0f,
		    &direct_observation);
		direct_result = SG_DoorApproachBegin(&direct_state, source_q8,
		    anchor_q8, &direct_observation);
		if (direct_result.reason != SG_DOOR_APPROACH_REASON_NONE)
			goto done;
	}
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
		if (direct_controller)
		{
			SG_OracleDoorApproachObservation(&ph, trigger, binding, false,
			    0.0f, &direct_observation);
			direct_result = SG_DoorApproachPreStep(&direct_state,
			    &direct_observation, cmd.msec);
			if (direct_result.reason != SG_DOOR_APPROACH_REASON_NONE)
				goto done;
			if (direct_result.drive &&
			    !SG_DeclaredCommand(ph.origin, wait_point, &ph.pms, &cmd))
				goto done;
		}
		else if (!triggered || elapsed >= resume_ms)
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
		if ((!direct_controller || direct_result.drive) &&
		    (!triggered || elapsed >= resume_ms) &&
		    ((!triggered && horiz > 0.01f) ||
		     (triggered && horiz > 2.0f)) && cmd.forwardmove == 0)
			cmd.forwardmove = 40;
		if (!SG_OracleRunWorld(&ph, &cmd, 1) ||
		    (!direct_controller && !ph.groundentity) ||
		    !SG_OracleDoorEgressWaterSafe(controller_kind, ph.waterlevel,
		        ph.watertype))
			goto done;
		if (direct_controller)
		{
			qboolean fall_sampled = ((elapsed + 25) % 100) == 0;
			float fall_delta = 0.0f;

			if (fall_sampled)
			{
				fall_delta = P_FallDelta(direct_state.old_frame_z, ph.velocity[2],
				    ph.groundentity, ph.waterlevel);
			}
			SG_OracleDoorApproachObservation(&ph, trigger, binding,
			    fall_sampled, fall_delta, &direct_observation);
			direct_result = SG_DoorApproachPostStep(&direct_state,
			    &direct_observation, cmd.msec);
			if (direct_result.reason != SG_DOOR_APPROACH_REASON_NONE)
				goto done;
			triggered = direct_state.touched ? true : false;
			first_touch_ms = direct_state.first_touch_ms;
			resume_ms = direct_state.resume_ms;
			if (direct_result.snap_required)
			{
				vec3_t mins = { -16.0f, -16.0f, -24.0f };
				vec3_t maxs = { 16.0f, 16.0f, 32.0f };
				trace_t snap;
				int axis;

				if (!SG_OracleStablePopulationTrace(ph.origin, mins, maxs,
				        wait_point, NULL, sg_oracle_loader_replay, &snap) ||
				    snap.startsolid || snap.allsolid || snap.fraction < 1.0f)
					goto done;
				direct_result = SG_DoorApproachSnapped(&direct_state,
				    &direct_observation);
				if (direct_result.reason != SG_DOOR_APPROACH_REASON_NONE)
					goto done;
				ph.pms = direct_state.expected_pms;
				ph.old_pms = ph.pms;
				for (axis = 0; axis < 3; axis++)
				{
					ph.origin[axis] = ph.pms.origin[axis] * 0.125f;
					ph.velocity[axis] = 0.0f;
				}
			}
			if (direct_state.phase == SG_DOOR_APPROACH_COMPLETE)
			{
				*arrival_ms = direct_state.elapsed_ms;
				*touch_ms = direct_state.first_touch_ms;
				ok = true;
				goto done;
			}
			continue;
		}
		if (!triggered && SG_OracleDoorApproachContactObserved(
		        button_controller, sg_oracle_declared_touched,
		        binding && SG_BoundDoorEntryContactMatches(binding, ph.origin)))
		{
			if (button_controller)
			{
				/* The support identity at the first physical Pmove contact is
				 * the carry law.  Bounds alone cannot distinguish a coplanar
				 * world floor from a button pusher. */
				if (!sg_oracle_declared_touched)
					goto done;
				if (ph.groundentity_entity == trigger)
					first_support = SG_BUTTON_SUPPORT_RIDER;
				else if (ph.groundentity_entity == g_edicts ||
				         SG_ImmutableSupport(ph.groundentity_entity))
					first_support = SG_BUTTON_SUPPORT_STATIC;
				else
					goto done;
			}
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
			trace_t snap;

			if (!SG_OracleStablePopulationTrace(ph.origin, mins, maxs,
			        wait_point, NULL, sg_oracle_loader_replay, &snap) ||
			    snap.startsolid || snap.allsolid || snap.fraction < 1.0f)
				goto done;
		}
		SG_OraclePlace(&exact, (vec_t *)wait_point);
		memset(&cmd, 0, sizeof(cmd));
		cmd.msec = 0;
		if (!SG_OracleRunWorld(&exact, &cmd, 1) || !exact.groundentity ||
		    !SG_OracleDoorEgressWaterSafe(controller_kind,
		        exact.waterlevel, exact.watertype) ||
		    !(binding ? SG_BoundDoorOutsideSweep(binding, exact.origin) :
		      delayed_controller
		          ? SG_DeclaredDelayedDoorOutsideSweep(trigger, exact.origin)
		          : SG_DeclaredDoorOutsideSweep(trigger, exact.origin)))
			goto done;
		if (button_controller)
		{
			sg_button_support_mode_t exact_support;

			if (exact.groundentity_entity == trigger)
				exact_support = SG_BUTTON_SUPPORT_RIDER;
			else if (exact.groundentity_entity == g_edicts ||
			         SG_ImmutableSupport(exact.groundentity_entity))
				exact_support = SG_BUTTON_SUPPORT_STATIC;
			else
				goto done;
			/* The serialized anchor authenticates the same carry law observed at
			 * first physical touch; an edge/coplanar support change is not the
			 * proved transaction. */
			if (exact_support != first_support)
				goto done;
		}
		*arrival_ms = elapsed + 25;
		*touch_ms = first_touch_ms;
		if (support_mode)
			*support_mode = first_support;
		ok = true;
		goto done;
	}

done:
	if (bounds_cache_active)
		SG_OracleDoorBoundsCacheEnd();
	if (member_scope_active && !SG_OracleBoundMemberScopeEnd(binding))
		ok = false;
	sg_oracle_passent = old_passent;
	sg_oracle_world_only = old_world;
	sg_oracle_contaminated = old_contaminated;
	sg_oracle_declared_expected = old_expected;
	sg_oracle_declared_door = old_door;
	sg_oracle_bound_door = old_bound;
	sg_oracle_declared_action = old_action;
	sg_oracle_declared_touched = old_touched;
	return ok;
}

qboolean SG_OracleDeclaredDoorApproach(const vec3_t source,
	const vec3_t wait_point, edict_t *trigger, int *arrival_ms,
	int *touch_ms)
{
	return SG_OracleDoorApproach(source, wait_point, trigger, NULL,
	    arrival_ms, touch_ms, NULL);
}

qboolean SG_OracleDeclaredButtonDoorApproach(const vec3_t source,
	const vec3_t wait_point, edict_t *button, int *arrival_ms,
	int *touch_ms, sg_button_support_mode_t *support_mode)
{
	return SG_OracleDoorApproach(source, wait_point, button, NULL,
	    arrival_ms, touch_ms, support_mode);
}

qboolean SG_OracleBoundDoorApproach(const vec3_t source,
	const vec3_t wait_point,
	const sg_rune_mechanism_binding_t *binding, int *arrival_ms,
	int *touch_ms)
{
	return binding ? SG_OracleDoorApproach(source, wait_point,
	    binding->entry_entity, binding, arrival_ms, touch_ms, NULL) : false;
}

qboolean SG_OracleBoundButtonDoorApproach(const vec3_t source,
	const vec3_t wait_point,
	const sg_rune_mechanism_binding_t *binding, int *arrival_ms,
	int *touch_ms, sg_button_support_mode_t *support_mode)
{
	return binding && binding->plan &&
	    binding->plan->controller_kind ==
	        SG_MECHANISM_CONTROLLER_BUTTON_DOOR
	    ? SG_OracleDoorApproach(source, wait_point, binding->entry_entity,
	          binding, arrival_ms, touch_ms, support_mode)
	    : false;
}

/* One complete RL_DOOR egress. The caller has synchronously linked every
 * member of `door` at its authoritative open pose. Unlike a generic proof,
 * that exact team remains physical collision while its own repeatable trigger
 * and solids are deterministic members of this declaration. The body begins
 * at the outside-sweep trigger seed where live execution waited motionless,
 * and success is a dry supported endpoint outside the full sweep. */
static qboolean SG_OracleDoorEgress(const vec3_t source,
	const vec3_t target, edict_t *trigger,
	const sg_rune_mechanism_binding_t *binding, edict_t *passent,
	int *arrival_ms, sg_button_support_mode_t support_mode)
{
	edict_t *old_passent = sg_oracle_passent;
	edict_t *old_expected = sg_oracle_declared_expected;
	edict_t *old_door = sg_oracle_declared_door;
	const sg_rune_mechanism_binding_t *old_bound = sg_oracle_bound_door;
	qboolean old_world = sg_oracle_world_only;
	qboolean old_contaminated = sg_oracle_contaminated;
	qboolean old_touched = sg_oracle_declared_touched;
	int old_action = sg_oracle_declared_action;
	sg_phantom_t ph;
	usercmd_t cmd;
	qboolean ok = false;
	qboolean button_controller;
	qboolean member_scope_active = false;
	int controller_kind;
	float old_frame_z;
	int elapsed;

	button_controller = binding
	    ? binding->plan && binding->plan->controller_kind ==
	          SG_MECHANISM_CONTROLLER_BUTTON_DOOR
	    : (SG_OracleDeclaredButtonDoorSafe(trigger) ||
	       SG_OracleDeclaredButtonTopSafe(trigger));
	controller_kind = binding && binding->plan
	    ? binding->plan->controller_kind
	    : (button_controller ? SG_MECHANISM_CONTROLLER_BUTTON_DOOR
	                         : (SG_DeclaredDoorDirectActivatorSafe(trigger)
	                               ? SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR
	                               : SG_MECHANISM_CONTROLLER_AUTO_DOOR));
	if (!trigger || !arrival_ms ||
	    (button_controller != (support_mode != SG_BUTTON_SUPPORT_NONE)) ||
	    (button_controller && support_mode != SG_BUTTON_SUPPORT_STATIC &&
	     support_mode != SG_BUTTON_SUPPORT_RIDER) ||
	    (binding && (!SG_OracleBoundDoorBindingCurrent(binding) ||
	                 binding->entry_entity != trigger)) ||
	    !(binding ? SG_BoundDoorOutsideSweep(binding, source) :
	                 SG_DeclaredDoorOutsideSweep(trigger, source)) ||
	    !(binding ? SG_BoundDoorOutsideSweep(binding, target) :
	                 SG_DeclaredDoorOutsideSweep(trigger, target)) ||
	    !(binding ? SG_BoundDoorCrossesSweep(binding, source, target) :
	                 SG_DeclaredDoorCrossesSweep(trigger, source, target)))
		return false;
	if (binding && sg_oracle_loader_replay)
	{
		if (!SG_OracleBoundMemberScopeBegin(binding))
			return false;
		member_scope_active = true;
	}
	sg_oracle_passent = passent;
	sg_oracle_world_only = true;
	sg_oracle_contaminated = false;
	sg_oracle_declared_expected = NULL;
	sg_oracle_declared_door = trigger;
	sg_oracle_bound_door = binding;
	sg_oracle_declared_action = button_controller
	    ? RL_BUTTON_DOOR : RL_DOOR;
	sg_oracle_declared_touched = false;
	SG_OraclePlace(&ph, (vec_t *)source);
	memset(&cmd, 0, sizeof(cmd));
	cmd.msec = 0;
	SG_OracleRun(&ph, &cmd, 1);
	if (sg_oracle_contaminated || !ph.groundentity ||
	    !SG_OracleDoorEgressWaterSafe(controller_kind, ph.waterlevel,
	        ph.watertype))
		goto done;
	if (button_controller)
	{
		qboolean support_matches = support_mode == SG_BUTTON_SUPPORT_RIDER
		    ? ph.groundentity_entity == trigger
		    : (ph.groundentity_entity == g_edicts ||
		       SG_ImmutableSupport(ph.groundentity_entity));

		if (!support_matches)
			goto done;
	}
	old_frame_z = ph.velocity[2];
	for (elapsed = 0; elapsed < 5000; elapsed += 25)
	{
		memset(&cmd, 0, sizeof(cmd));
		cmd.msec = 25;
		if (!SG_DeclaredCommand(ph.origin, target, &ph.pms, &cmd))
			goto done;
		SG_OracleRun(&ph, &cmd, 1);
		if (sg_oracle_contaminated ||
		    !SG_OracleDoorEgressWaterSafe(controller_kind, ph.waterlevel,
		        ph.watertype))
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
			if ((binding ? SG_BoundDoorOutsideSweep(binding, ph.origin) :
			               SG_DeclaredDoorOutsideSweep(trigger, ph.origin)) &&
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
	if (member_scope_active && !SG_OracleBoundMemberScopeEnd(binding))
		ok = false;
	sg_oracle_passent = old_passent;
	sg_oracle_world_only = old_world;
	sg_oracle_contaminated = old_contaminated;
	sg_oracle_declared_expected = old_expected;
	sg_oracle_declared_door = old_door;
	sg_oracle_bound_door = old_bound;
	sg_oracle_declared_action = old_action;
	sg_oracle_declared_touched = old_touched;
	return ok;
}

static qboolean SG_OracleBoundDoorEgressReplayCached(const vec3_t source,
	const vec3_t target, const sg_rune_mechanism_binding_t *binding,
	edict_t *passent, int *arrival_ms,
	sg_button_support_mode_t support_mode)
{
	sg_oracle_door_egress_replay_key_t key;
	qboolean cacheable;
	qboolean ok;

	if (!arrival_ms)
		return false;
	cacheable = sg_oracle_door_egress_replay_cache.active &&
	    SG_OracleDoorEgressReplayKeyCapture(&key, source, target, binding,
	        passent, support_mode);
	if (cacheable &&
	    SG_OracleDoorEgressReplayCacheLookup(&key, arrival_ms))
		return true;
	ok = SG_OracleDoorEgress(source, target,
	    binding ? binding->entry_entity : NULL, binding, passent, arrival_ms,
	    support_mode);
	if (ok && cacheable)
		SG_OracleDoorEgressReplayCacheStore(&key, *arrival_ms);
	return ok;
}

qboolean SG_OracleDeclaredDoorEgress(const vec3_t source,
	const vec3_t target, edict_t *trigger, edict_t *passent,
	int *arrival_ms)
{
	return SG_OracleDoorEgress(source, target, trigger, NULL, passent,
	    arrival_ms, SG_BUTTON_SUPPORT_NONE);
}

qboolean SG_OracleDeclaredButtonDoorTopEgress(const vec3_t source,
	const vec3_t target, edict_t *button, edict_t *passent,
	int *arrival_ms, sg_button_support_mode_t support_mode)
{
	edict_t *old_top = sg_oracle_declared_button_top;
	qboolean ok = false;

	if (button && (support_mode == SG_BUTTON_SUPPORT_STATIC ||
	               support_mode == SG_BUTTON_SUPPORT_RIDER))
	{
		sg_oracle_declared_button_top = button;
		if (SG_OracleDeclaredButtonTopSafe(button))
			ok = SG_OracleDoorEgress(source, target, button, NULL, passent,
			    arrival_ms, support_mode);
	}
	sg_oracle_declared_button_top = old_top;
	return ok;
}

qboolean SG_OracleBoundDoorEgress(const vec3_t source,
	const vec3_t target, const sg_rune_mechanism_binding_t *binding,
	edict_t *passent, int *arrival_ms)
{
	return binding ? SG_OracleDoorEgress(source, target,
	    binding->entry_entity, binding, passent, arrival_ms,
	    SG_BUTTON_SUPPORT_NONE) : false;
}

qboolean SG_OracleBoundButtonDoorEgress(const vec3_t source,
	const vec3_t target, const sg_rune_mechanism_binding_t *binding,
	edict_t *passent, int *arrival_ms,
	sg_button_support_mode_t support_mode)
{
	return binding && binding->plan &&
	    binding->plan->controller_kind ==
	        SG_MECHANISM_CONTROLLER_BUTTON_DOOR
	    ? SG_OracleDoorEgress(source, target, binding->entry_entity, binding,
	          passent, arrival_ms, support_mode)
	    : false;
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
static int SG_OracleDoorTopBegin(edict_t *trigger,
	const sg_rune_mechanism_binding_t *binding,
	sg_declared_door_pose_t *saved, int capacity)
{
	edict_t *members[SG_PHANTOM_ARMED_DOORS];
	int count, i;

	if (!saved || capacity <= 0)
		return -1;
	count = binding
	    ? SG_BoundDoorMembers(binding, members, SG_PHANTOM_ARMED_DOORS)
	    : SG_DeclaredDoorMembers(trigger, members, SG_PHANTOM_ARMED_DOORS);
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

static qboolean SG_OracleButtonTopBegin(edict_t *button,
	const sg_rune_mechanism_binding_t *binding,
	sg_declared_door_pose_t *saved, vec3_t displacement)
{
	const rune_mechanism_node_t *node = binding ? binding->entry_node : NULL;
	sg_mech_button_endpoints_t endpoints;
	int button_key = SG_OracleLiveEdictIndex(button);
	int axis;

	if (displacement)
		VectorClear(displacement);
	if (!button || !saved || !displacement || button_key <= 0 ||
	    !SG_MechCatalogButtonBottomEndpoints((uint32_t)button_key, node,
	        button, &endpoints))
		return false;
	for (axis = 0; axis < 3; axis++)
	{
		int delta = (int)endpoints.end_q8[axis] -
		    (int)endpoints.start_q8[axis];

		if (delta < INT16_MIN || delta > INT16_MAX)
			return false;
		displacement[axis] = (float)delta * 0.125f;
	}
	if (VectorCompare(displacement, vec3_origin))
		return false;
	memset(saved, 0, sizeof(*saved));
	saved->ent = button;
	VectorCopy(button->s.origin, saved->origin);
	VectorCopy(button->s.old_origin, saved->old_origin);
	VectorCopy(button->s.angles, saved->angles);
	VectorCopy(button->velocity, saved->velocity);
	VectorCopy(button->avelocity, saved->avelocity);
	saved->state = button->moveinfo.state;
	saved->solid = button->solid;
	saved->linkcount = button->linkcount;
	for (axis = 0; axis < 3; axis++)
	{
		button->s.origin[axis] = (float)endpoints.end_q8[axis] * 0.125f;
		button->s.old_origin[axis] = button->s.origin[axis];
	}
	VectorClear(button->velocity);
	VectorClear(button->avelocity);
	button->moveinfo.state = SG_PLAT_STATE_TOP;
	button->solid = SOLID_BSP;
	sg_host.linkentity(button);
	return true;
}

/* Loader-side replay of the complete serialized RL_DOOR witness.  Resolving
 * the mechanism and checking the sweep is not enough: a destination may be
 * outside the sweep yet unreachable at TOP, or its egress may outlast a team
 * member's open hold.  Re-run both controller phases against the same temporary
 * TOP collision pose used by generation, apply the shared exact timing formula,
 * then restore the live world before returning on every path. */
static qboolean SG_OracleValidateDoorLink(const vec3_t source,
	const vec3_t anchor, const vec3_t target, edict_t *trigger,
	const sg_rune_mechanism_binding_t *binding, int stored_cost_ms)
{
	sg_declared_door_pose_t saved[SG_PHANTOM_ARMED_DOORS];
	sg_declared_door_pose_t button_saved;
	edict_t *resolved;
	vec3_t effective_anchor, displacement;
	int pose_count = 0, approach_ms, touch_ms, egress_ms, contract_cost;
	int expected_mode = RLCM_NONE;
	qboolean button_controller;
	qboolean button_posed = false;
	qboolean egress_cache_active = false;
	qboolean old_loader_replay;
	qboolean valid = false;

	if (!source || !anchor || !target || !trigger || stored_cost_ms <= 0 ||
	    (binding && (!SG_OracleBoundDoorBindingCurrent(binding) ||
	                 binding->entry_entity != trigger)))
		return false;
	button_controller = binding && binding->plan &&
	    binding->plan->controller_kind ==
	        SG_MECHANISM_CONTROLLER_BUTTON_DOOR;
	if (!binding && SG_OracleDeclaredButtonDoorSafe(trigger))
		return false; /* the active button witness requires its authenticated tail */
	VectorCopy(anchor, effective_anchor);
	VectorClear(displacement);
	if (button_controller)
	{
		sg_mech_button_endpoints_t endpoints;
		int axis;

			if ((binding->link->mode != RLCM_PREOPEN &&
		     binding->link->mode != RLCM_RIDE) ||
		    binding->link->sweep_clear_ms == 0U ||
		    binding->link->sweep_clear_ms % 100U != 0U ||
		    !SG_MechCatalogButtonBottomEndpoints(binding->entry_node->key,
		        binding->entry_node, trigger, &endpoints))
			return false;
		for (axis = 0; axis < 3; axis++)
		{
			int delta = (int)endpoints.end_q8[axis] -
			    (int)endpoints.start_q8[axis];

			if (delta < INT16_MIN || delta > INT16_MAX ||
			    (float)delta * 0.125f !=
			        binding->link->mechanism_anchor[axis])
				return false;
			displacement[axis] = (float)delta * 0.125f;
			if (binding->link->mode == RLCM_RIDE)
				effective_anchor[axis] += displacement[axis];
		}
		if (VectorCompare(displacement, vec3_origin) ||
		    (binding->link->mode == RLCM_RIDE &&
		     (SG_BoundDoorCrossesSweep(binding, anchor, effective_anchor) ||
		      !SG_OracleButtonCarryClear(trigger, anchor,
		          effective_anchor, true))))
			return false;
		expected_mode = binding->link->mode;
	}
	resolved = binding ? trigger : SG_DeclaredDoorForLink(anchor, source);
	if ((binding ? resolved != trigger :
	     !SG_DeclaredDoorSameSet(resolved, trigger)) ||
	    !(binding ? SG_BoundDoorOutsideSweep(binding, target) :
	                 SG_DeclaredDoorOutsideSweep(trigger, target)) ||
	    !(binding ? SG_BoundDoorCrossesSweep(binding, effective_anchor, target) :
	                 SG_DeclaredDoorCrossesSweep(trigger, anchor, target)))
		return false;
	old_loader_replay = sg_oracle_loader_replay;
	sg_oracle_loader_replay = true;
	if (button_controller)
	{
		sg_button_support_mode_t observed = SG_BUTTON_SUPPORT_NONE;
		sg_button_support_mode_t expected = expected_mode == RLCM_RIDE
		    ? SG_BUTTON_SUPPORT_RIDER : SG_BUTTON_SUPPORT_STATIC;

		if (!SG_OracleBoundButtonDoorApproach(source, anchor, binding,
		        &approach_ms, &touch_ms, &observed) || observed != expected)
			goto restore;
	}
	else if (!(binding ? SG_OracleBoundDoorApproach(source, anchor, binding,
	              &approach_ms, &touch_ms) :
	            SG_OracleDeclaredDoorApproach(source, anchor, trigger,
	              &approach_ms, &touch_ms)))
		goto restore;
	pose_count = SG_OracleDoorTopBegin(trigger, binding, saved,
	    SG_PHANTOM_ARMED_DOORS);
	if (pose_count <= 0)
		goto restore;
	if (button_controller)
	{
		vec3_t posed_displacement;
		sg_button_support_mode_t expected = expected_mode == RLCM_RIDE
		    ? SG_BUTTON_SUPPORT_RIDER : SG_BUTTON_SUPPORT_STATIC;

		if (!SG_OracleButtonTopBegin(trigger, binding, &button_saved,
		        posed_displacement))
			goto restore;
		button_posed = true;
		if (!VectorCompare(posed_displacement, displacement))
			goto restore;
		SG_OracleDoorBoundsCacheBegin();
		egress_cache_active = true;
		if (!SG_OracleBoundDoorEgressReplayCached(effective_anchor, target,
		        binding, NULL, &egress_ms, expected))
			goto restore;
		if (((egress_ms + 99) / 100) * 100 !=
		    binding->link->sweep_clear_ms)
			goto restore;
	}
	else
	{
		SG_OracleDoorBoundsCacheBegin();
		egress_cache_active = true;
		if (!(binding ? SG_OracleBoundDoorEgressReplayCached(anchor, target,
		              binding, NULL, &egress_ms, SG_BUTTON_SUPPORT_NONE) :
		            SG_OracleDeclaredDoorEgress(anchor, target, trigger, NULL,
		              &egress_ms)))
			goto restore;
	}
	contract_cost = SG_DoorContractCost(trigger, binding, approach_ms,
	    touch_ms, egress_ms);
	valid = contract_cost > 0 && stored_cost_ms >= contract_cost;

restore:
	if (egress_cache_active)
		SG_OracleDoorBoundsCacheEnd();
	if (button_posed)
		SG_OracleDeclaredDoorTopEnd(&button_saved, 1);
	if (pose_count > 0)
		SG_OracleDeclaredDoorTopEnd(saved, pose_count);
	sg_oracle_loader_replay = old_loader_replay;
	return valid && (!binding || SG_RuneMechanismBindingCurrent(binding));
}

qboolean SG_OracleValidateDeclaredDoorLink(const vec3_t source,
	const vec3_t anchor, const vec3_t target, edict_t *trigger,
	int stored_cost_ms)
{
	return SG_OracleValidateDoorLink(source, anchor, target, trigger, NULL,
	    stored_cost_ms);
}

qboolean SG_OracleValidateBoundDoorLink(const vec3_t source,
	const vec3_t anchor, const vec3_t target,
	const sg_rune_mechanism_binding_t *binding, int stored_cost_ms)
{
	return binding ? SG_OracleValidateDoorLink(source, anchor, target,
	    binding->entry_entity, binding, stored_cost_ms) : false;
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

typedef qboolean (*sg_oracle_hook_monitor_fn)(void *context,
	const sg_phantom_t *ph, const vec3_t before, const vec3_t after,
	int elapsed_ms);

static qboolean SG_OracleHookTraverseMonitored(sg_phantom_t *ph,
	const vec3_t bite,
	const vec3_t destination, const vec3_t view_angles, int hand,
	int flight_ms, int settle_limit_ms, float old_frame_z,
	sg_hook_proof_t *proof, edict_t *passent, qboolean world_only,
	sg_oracle_hook_monitor_fn monitor, void *monitor_context)
{
	sg_hook_replay_spec_t spec;
	sg_hook_replay_state_t state;
	sg_replay_pose_t pose;
	sg_replay_observation_t observation;
	sg_replay_status_t status;
	usercmd_t cmd;
	qboolean result = false;
	qboolean fixed_point_valid = false;
	sg_phantom_t fixed_point_phantom;
	usercmd_t fixed_point_command;
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
	if (monitor && !monitor(monitor_context, ph, ph->origin, ph->origin, 0))
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
			if (monitor && !monitor(monitor_context, ph, ph->origin,
			                        ph->origin, state.progress.elapsed_ms))
				goto done;
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
			if (monitor && !monitor(monitor_context, ph, ph->origin,
			                        ph->origin, state.progress.elapsed_ms))
				goto done;
			SG_OracleHookStep(ph, bite, view_angles, hand);
			SG_OracleReplayPose(ph, &pose);
			if (monitor && !monitor(monitor_context, ph, ph->origin,
			                        ph->origin, state.progress.elapsed_ms))
				goto done;
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
		{
			vec3_t before;

			VectorCopy(ph->origin, before);
			if (monitor && !monitor(monitor_context, ph, before, before,
			                        state.progress.elapsed_ms))
				goto done;
			if (!(monitor == NULL && world_only && fixed_point_valid &&
			      memcmp(ph, &fixed_point_phantom, sizeof(*ph)) == 0 &&
			      memcmp(&cmd, &fixed_point_command, sizeof(cmd)) == 0))
			{
				sg_phantom_t step_start = *ph;

				SG_OracleRun(ph, &cmd, 1);
				if (monitor == NULL && world_only &&
				    !sg_oracle_contaminated &&
				    memcmp(ph, &step_start, sizeof(*ph)) == 0)
				{
					fixed_point_phantom = *ph;
					fixed_point_command = cmd;
					fixed_point_valid = true;
				}
			}
			if (monitor && !monitor(monitor_context, ph, before, ph->origin,
			                        state.progress.elapsed_ms +
			                            SG_REPLAY_STEP_MS))
				goto done;
		}
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

qboolean SG_OracleHookTraverse(sg_phantom_t *ph, const vec3_t bite,
	const vec3_t destination, const vec3_t view_angles, int hand,
	int flight_ms, int settle_limit_ms, float old_frame_z,
	sg_hook_proof_t *proof, edict_t *passent, qboolean world_only)
{
	return SG_OracleHookTraverseMonitored(ph, bite, destination,
	    view_angles, hand, flight_ms, settle_limit_ms, old_frame_z, proof,
	    passent, world_only, NULL, NULL);
}

typedef struct sg_oracle_compound_hook_monitor_s
{
	const sg_compound_world_preopen_t *resolved;
	edict_t *member;
	int outside_since_ms;
	int sweep_clear_ms;
	int failure_reason;
} sg_oracle_compound_hook_monitor_t;

static qboolean SG_OracleCompoundHookMonitor(void *opaque,
	const sg_phantom_t *ph, const vec3_t before, const vec3_t after,
	int elapsed_ms)
{
	sg_oracle_compound_hook_monitor_t *monitor = opaque;
	edict_t *current = NULL;
	qboolean crossed;
	qboolean outside_before;
	qboolean outside_after;

	if (!monitor || !ph || elapsed_ms < 0 ||
	    !SG_CompoundWorldResolvedMember(monitor->resolved, &current) ||
	    current != monitor->member ||
	    !SG_CompoundWorldStagedAtTopFor(monitor->resolved, 0))
	{
		if (monitor)
			monitor->failure_reason = RLR_MECHANISM_UNRESOLVED;
		return false;
	}
	if (!SG_OracleCompoundPhantomClean(ph))
	{
		monitor->failure_reason = RLR_SUFFIX_REPLAY_FAILED;
		return false;
	}
	crossed = SG_CompoundWorldCrossesSweep(monitor->resolved, before, after);
	outside_before = SG_CompoundWorldOutsideSweep(monitor->resolved, before);
	outside_after = SG_CompoundWorldOutsideSweep(monitor->resolved, after);
	if (monitor->sweep_clear_ms)
	{
		if (crossed || !outside_before || !outside_after)
		{
			monitor->failure_reason = RLR_CLEAR_MISMATCH;
			return false;
		}
		return true;
	}
	if (crossed || !outside_before || !outside_after)
		monitor->outside_since_ms = -1;
	else if (monitor->outside_since_ms < 0)
		monitor->outside_since_ms = elapsed_ms;
	if (elapsed_ms >= SG_REPLAY_FRAME_MS &&
	    elapsed_ms % SG_REPLAY_FRAME_MS == 0 &&
	    monitor->outside_since_ms >= 0 &&
	    elapsed_ms - monitor->outside_since_ms >= SG_REPLAY_FRAME_MS)
		monitor->sweep_clear_ms = elapsed_ms;
	return true;
}

static qboolean SG_OracleCompoundHookBoltOutside(
	const sg_compound_world_preopen_t *resolved,
	const vec3_t muzzle, const vec3_t bite)
{
	vec3_t delta, direction, before, after;
	float distance, travelled;

	VectorSubtract(bite, muzzle, delta);
	distance = VectorLength(delta);
	if (distance < 1.0f ||
	    !SG_CompoundWorldOutsideSweep(resolved, muzzle))
		return false;
	VectorScale(delta, 1.0f / distance, direction);
	VectorCopy(muzzle, before);
	for (travelled = RUNE_HOOK_FRAME_DISTANCE; ;
	     travelled += RUNE_HOOK_FRAME_DISTANCE)
	{
		float at = travelled < distance ? travelled : distance;

		after[0] = muzzle[0] + at * direction[0];
		after[1] = muzzle[1] + at * direction[1];
		after[2] = muzzle[2] + at * direction[2];
		if (!SG_OracleCompoundOutsideSegment(resolved, before, after))
			return false;
		if (travelled >= distance)
			break;
		VectorCopy(after, before);
	}
	return true;
}

static void SG_OracleCompoundHookCaptureSource(const sg_phantom_t *ph,
	float old_frame_z, sg_compound_hook_proof_t *proof)
{
	proof->source_pms = ph->pms;
	proof->source_old_pms = ph->old_pms;
	VectorCopy(ph->origin, proof->source_origin);
	VectorCopy(ph->velocity, proof->source_velocity);
	proof->source_groundentity = ph->groundentity;
	proof->source_watertype = ph->watertype;
	proof->source_waterlevel = ph->waterlevel;
	proof->source_old_frame_z = old_frame_z;
}

static void SG_OracleCompoundHookCaptureSuffix(const sg_phantom_t *ph,
	float old_frame_z, sg_compound_hook_proof_t *proof)
{
	proof->suffix_pms = ph->pms;
	proof->suffix_old_pms = ph->old_pms;
	VectorCopy(ph->origin, proof->suffix_origin);
	VectorCopy(ph->velocity, proof->suffix_velocity);
	proof->suffix_groundentity = ph->groundentity;
	proof->suffix_watertype = ph->watertype;
	proof->suffix_waterlevel = ph->waterlevel;
	proof->suffix_old_frame_z = old_frame_z;
}

static qboolean SG_OracleCompoundHookControl(const sg_phantom_t *ph,
	const sg_compound_world_preopen_t *resolved, const vec3_t control,
	edict_t *passent, vec3_t view_angles, vec3_t muzzle, vec3_t bite)
{
	vec3_t forward, right, shot_end, miss;
	trace_t muzzle_trace, shot_trace;
	float shot_length;

	if (!SG_HookControlDecode(ph->origin, 22.0f, RIGHT_HANDED, control,
	                         view_angles, muzzle, bite))
		return false;
	AngleVectors(view_angles, forward, right, NULL);
	if (!SG_OracleStablePopulationTraceMask(ph->origin, NULL, NULL, muzzle,
	        passent, MASK_SHOT, sg_oracle_loader_replay, &muzzle_trace))
		return false;
	if (muzzle_trace.startsolid || muzzle_trace.fraction < 1.0f)
		return false;
	VectorNormalize(forward);
	shot_length = control[ROLL] + 96.0f;
	if (shot_length > RUNE_HOOK_MAX_RAY)
		shot_length = RUNE_HOOK_MAX_RAY;
	VectorMA(muzzle, shot_length, forward, shot_end);
	if (!SG_OracleStablePopulationTraceMask(muzzle, NULL, NULL, shot_end,
	        passent, MASK_SHOT, sg_oracle_loader_replay, &shot_trace))
		return false;
	VectorSubtract(shot_trace.endpos, bite, miss);
	if (shot_trace.startsolid || shot_trace.fraction >= 1.0f ||
	    shot_trace.ent != g_edicts ||
	    (shot_trace.surface && (shot_trace.surface->flags & SURF_SKY)) ||
	    VectorLength(miss) > RUNE_HOOK_BITE_TOLERANCE ||
	    CTF_HookPullVelocity(muzzle, bite, miss) < 150 ||
	    !SG_OracleHookFlightClear(muzzle, bite) ||
	    !SG_OracleCompoundHookBoltOutside(resolved, muzzle, bite))
		return false;
	return true;
}

static qboolean SG_OracleCompoundHookDiscoverControl(
	const sg_phantom_t *ph, const vec3_t destination, int candidate_index,
	vec3_t control)
{
	static const float backs[4] = { 0.0f, 0.35f, 0.6f, 0.85f };
	vec3_t up, ceiling, aim, view, forward, right, muzzle, shot_end, delta;
	trace_t trace;
	float distance;

	if (candidate_index < 0 || candidate_index >= 4)
		return false;
	VectorCopy(destination, up);
	up[0] += (ph->origin[0] - up[0]) * backs[candidate_index];
	up[1] += (ph->origin[1] - up[1]) * backs[candidate_index];
	if (backs[candidate_index] > 0.0f &&
	    ph->origin[2] < destination[2])
		up[2] = destination[2];
	up[2] += 24.0f;
	VectorCopy(up, ceiling);
	ceiling[2] += 512.0f;
	trace = sg_host.trace(up, NULL, NULL, ceiling, NULL, MASK_PLAYERSOLID);
	if (trace.startsolid || trace.fraction >= 1.0f ||
	    (trace.surface && (trace.surface->flags & SURF_SKY)))
		return false;
	VectorCopy(trace.endpos, aim);
	aim[2] -= 4.0f;
	if (!SG_HookAimAngles(ph->origin, 22.0f, aim, view))
		return false;
	AngleVectors(view, forward, right, NULL);
	CTF_HookMuzzle(ph->origin, 22.0f, RIGHT_HANDED, forward, right, muzzle);
	VectorNormalize(forward);
	VectorMA(muzzle, RUNE_HOOK_MAX_RAY, forward, shot_end);
	trace = sg_host.trace(muzzle, NULL, NULL, shot_end, NULL, MASK_SHOT);
	if (trace.startsolid || trace.fraction >= 1.0f || trace.ent != g_edicts ||
	    (trace.surface && (trace.surface->flags & SURF_SKY)))
		return false;
	VectorSubtract(trace.endpos, muzzle, delta);
	distance = DotProduct(delta, forward);
	if (distance < 1.0f || distance > RUNE_HOOK_MAX_RAY)
		return false;
	control[PITCH] = view[PITCH];
	control[YAW] = view[YAW];
	control[ROLL] = distance;
	return true;
}

static rune_reject_reason_t SG_OracleCompoundHookSuffix(
	sg_phantom_t *ph, const sg_compound_world_preopen_t *resolved,
	edict_t *member, const vec3_t destination,
	const vec3_t expected_control, float old_frame_z,
	sg_compound_hook_proof_t *proof, edict_t *passent,
	qboolean world_only)
{
	sg_oracle_compound_hook_monitor_t monitor;
	sg_hook_proof_t hook;
	sg_phantom_t suffix_start;
	vec3_t control, view_angles, muzzle, bite;
	int first_candidate, candidate_count, candidate_index;

	suffix_start = *ph;
	first_candidate = expected_control ? -1 : 0;
	candidate_count = expected_control ? 1 : 4;
	for (candidate_index = 0; candidate_index < candidate_count;
	     candidate_index++)
	{
		*ph = suffix_start;
		memset(&hook, 0, sizeof(hook));
		if (expected_control)
			VectorCopy(expected_control, control);
		else if (!SG_OracleCompoundHookDiscoverControl(ph, destination,
		             first_candidate + candidate_index, control))
			continue;
		if (!SG_OracleCompoundHookControl(ph, resolved, control, passent,
		                                  view_angles, muzzle, bite))
			continue;
		memset(&monitor, 0, sizeof(monitor));
		monitor.resolved = resolved;
		monitor.member = member;
		monitor.outside_since_ms = 0;
		if (!SG_OracleHookTraverseMonitored(ph, bite, destination,
		        view_angles, RIGHT_HANDED,
		        (int)ceilf(control[ROLL] / RUNE_HOOK_FRAME_DISTANCE) *
		            SG_REPLAY_FRAME_MS,
		        RUNE_HOOK_WATER_SETTLE_MS, old_frame_z, &hook, passent,
		        world_only, SG_OracleCompoundHookMonitor, &monitor))
			continue;
		if (!monitor.sweep_clear_ms ||
		    monitor.failure_reason != RLR_OK)
			continue;
		VectorCopy(control, proof->control);
		VectorCopy(bite, proof->hook_spec.bite);
		VectorCopy(destination, proof->hook_spec.destination);
		VectorCopy(view_angles, proof->hook_spec.view_angles);
		proof->hook_spec.flight_ms =
			(int)ceilf(control[ROLL] / RUNE_HOOK_FRAME_DISTANCE) *
			SG_REPLAY_FRAME_MS;
		proof->hook_spec.settle_limit_ms = RUNE_HOOK_WATER_SETTLE_MS;
		proof->hook_spec.expected_release_ms = hook.release_ms;
		proof->hook_spec.expected_pull_ms = hook.pull_ms;
		proof->hook_spec.expected_settle_arrival_ms = hook.settle_arrival_ms;
		proof->hook_spec.expected_settle_ms = hook.settle_ms;
		proof->arrival_ms = proof->hook_spec.flight_ms + hook.pull_ms +
		                    hook.settle_ms;
		proof->sweep_clear_ms = monitor.sweep_clear_ms;
		proof->exit_speed = hook.exit_speed;
		return RLR_OK;
	}
	*ph = suffix_start;
	return RLR_SUFFIX_REPLAY_FAILED;
}

rune_reject_reason_t SG_OracleCompoundHookPreopen(
	sg_phantom_t *ph, const sg_compound_world_preopen_t *resolved,
	const vec3_t mechanism_anchor, const vec3_t destination,
	const vec3_t expected_control, float old_frame_z,
	sg_compound_hook_proof_t *proof, edict_t *passent,
	qboolean world_only, qboolean loader_replay)
{
	sg_oracle_compound_scope_t scope;
	sg_oracle_member_snapshot_t member_snapshot;
	sg_compound_translate_t translate;
	sg_compound_translate_step_t mover_step;
	sg_oracle_swim_cursor_t cursor;
	sg_compound_hook_proof_t candidate;
	sg_replay_status_t status;
	rune_reject_reason_t reason = RLR_BAD_CONTROL_POLICY;
	edict_t *member = NULL;
	vec3_t opening_view;
	qboolean scope_entered = false;
	qboolean member_staged = false;
	float suffix_checkpoint_old_frame_z;
	int remainder_commands;

	if (!proof)
		return RLR_BAD_CONTROL_POLICY;
	memset(proof, 0, sizeof(*proof));
	memset(&candidate, 0, sizeof(candidate));
	if (!ph || !resolved || !mechanism_anchor || !destination ||
	    !SG_OracleCompoundFixedVector(mechanism_anchor) ||
	    !SG_OracleCompoundFixedVector(destination) ||
	    (expected_control && !SG_OracleFinite3(expected_control)) ||
	    (loader_replay && !expected_control) ||
	    !isfinite(old_frame_z) ||
	    (world_only != false && world_only != true) ||
	    (loader_replay != false && loader_replay != true) ||
	    !sg_host.pmove || !sg_host.trace || !sg_host.pointcontents ||
	    !sg_host.box_edicts || !sg_host.linkentity ||
	    sg_oracle_active_phantom)
		return RLR_BAD_CONTROL_POLICY;
	if (!SG_CompoundWorldResolvedMember(resolved, &member) ||
	    !resolved->trigger ||
	    !SG_OracleCompoundMemberAtBottom(member, resolved))
		return RLR_MECHANISM_UNRESOLVED;

	SG_OracleMemberSnapshot(member, &member_snapshot);
	SG_OracleCompoundScopeEnter(&scope, resolved->trigger, member, passent,
	                            world_only, loader_replay);
	scope_entered = true;
	if (!SG_CompoundWorldOutsideSweep(resolved, ph->origin) ||
	    SG_OracleTriggerOverlap(ph) || sg_oracle_compound_touched ||
	    SG_OracleSolidOverlap(ph) || !SG_OracleCompoundPhantomClean(ph))
	{
		reason = RLR_APPROACH_REPLAY_FAILED;
		goto done;
	}
	SG_OracleCompoundHookCaptureSource(ph, old_frame_z, &candidate);
	status = SG_OracleSwimCursorBegin(&cursor, ph, mechanism_anchor, true,
	                                  old_frame_z, passent, true);
	while (status == SG_REPLAY_RUNNING)
	{
		vec3_t before;

		VectorCopy(ph->origin, before);
		status = SG_OracleSwimCursorStep(&cursor, ph);
		if (!SG_OracleCompoundOutsideSegment(resolved, before, ph->origin))
		{
			reason = RLR_APPROACH_REPLAY_FAILED;
			goto done;
		}
		if (sg_oracle_compound_touched)
			break;
	}
	if (!sg_oracle_compound_touched || status == SG_REPLAY_FAILED ||
	    !SG_OracleCompoundPhantomClean(ph) ||
	    !SG_OracleCompoundAnchorMatches(ph, mechanism_anchor))
	{
		reason = RLR_APPROACH_REPLAY_FAILED;
		goto done;
	}
	candidate.touch_ms = cursor.state.progress.elapsed_ms;
	if (candidate.touch_ms <= 0 ||
	    candidate.touch_ms % SG_REPLAY_STEP_MS != 0)
	{
		reason = RLR_APPROACH_REPLAY_FAILED;
		goto done;
	}
	old_frame_z = cursor.state.progress.old_frame_z;
	if (expected_control)
	{
		opening_view[PITCH] = expected_control[PITCH];
		opening_view[YAW] = expected_control[YAW];
		opening_view[ROLL] = 0.0f;
	}
	else
		VectorClear(opening_view);
	remainder_commands =
		((SG_REPLAY_FRAME_MS - candidate.touch_ms % SG_REPLAY_FRAME_MS) %
		 SG_REPLAY_FRAME_MS) / SG_REPLAY_STEP_MS;
	candidate.touch_frame_end_ms = candidate.touch_ms +
	                               remainder_commands * SG_REPLAY_STEP_MS;
	if (remainder_commands > 0 &&
	    !SG_OracleCompoundFrame(ph, resolved, NULL, opening_view,
	                            remainder_commands, &old_frame_z))
	{
		reason = RLR_APPROACH_REPLAY_FAILED;
		goto done;
	}
	if (!SG_CompoundTranslateBegin(&translate, resolved->bottom_origin,
	                               resolved->top_origin, resolved->speed))
	{
		reason = RLR_RIDE_REPLAY_FAILED;
		goto done;
	}
	for (;;)
	{
		if (!SG_CompoundTranslateFrame(&translate, &mover_step) ||
		    mover_step.elapsed_ms > RUNE_MAX_COST_MS ||
		    !SG_OracleCompoundStageMember(member, resolved, &mover_step))
		{
			reason = RLR_RIDE_REPLAY_FAILED;
			goto done;
		}
		member_staged = true;
		if (mover_step.at_top)
			break;
		if (!SG_OracleCompoundFrame(ph, resolved, NULL, opening_view, 4,
		                            &old_frame_z))
		{
			reason = RLR_RIDE_REPLAY_FAILED;
			goto done;
		}
	}
	candidate.mover_top_ms = mover_step.elapsed_ms;
	candidate.suffix_start_ms = mover_step.elapsed_ms - SG_REPLAY_FRAME_MS;
	if (!SG_OracleCompoundStageMemberTop(member, resolved))
	{
		reason = RLR_MECHANISM_UNRESOLVED;
		goto done;
	}
	suffix_checkpoint_old_frame_z = old_frame_z;
	if (!SG_OracleCompoundFrame(ph, resolved, NULL, opening_view, 4,
	                            &old_frame_z))
	{
		reason = RLR_RIDE_REPLAY_FAILED;
		goto done;
	}
	SG_OracleCompoundHookCaptureSuffix(ph, suffix_checkpoint_old_frame_z,
	                                  &candidate);
	reason = SG_OracleCompoundHookSuffix(ph, resolved, member, destination,
	    expected_control, old_frame_z, &candidate, passent, world_only);
	if (reason != RLR_OK)
		goto done;
	if (candidate.arrival_ms > RUNE_MAX_COST_MS - SG_REPLAY_FRAME_MS)
	{
		reason = RLR_COST_MISMATCH;
		goto done;
	}
	candidate.arrival_ms += SG_REPLAY_FRAME_MS;
	if (candidate.touch_frame_end_ms >
	        RUNE_MAX_COST_MS - candidate.suffix_start_ms ||
	    candidate.arrival_ms > RUNE_MAX_COST_MS -
	        candidate.touch_frame_end_ms - candidate.suffix_start_ms)
	{
		reason = RLR_COST_MISMATCH;
		goto done;
	}
	candidate.total_cost_ms = candidate.touch_frame_end_ms +
	                          candidate.suffix_start_ms +
	                          candidate.arrival_ms;
	*proof = candidate;
	reason = RLR_OK;

done:
	if (member_staged)
		SG_OracleMemberRestore(member, &member_snapshot);
	if (scope_entered)
		SG_OracleCompoundScopeRestore(&scope);
	return reason;
}


#define SG_RJ_RADIUS_DAMAGE \
	((float)SG_RUNE_PROOF_ROCKETJUMP_RADIUS_DAMAGE)
#define SG_RJ_DAMAGE_RADIUS \
	((float)SG_RUNE_PROOF_ROCKETJUMP_DAMAGE_RADIUS)
#define SG_RJ_ROCKET_SPEED \
	((float)SG_RUNE_PROOF_ROCKETJUMP_ROCKET_SPEED)
#define SG_RJ_PLAYER_MASS \
	((float)SG_RUNE_PROOF_ROCKETJUMP_PLAYER_MASS)
#define SG_RJ_BBOX_CENTRE \
	((float)SG_RUNE_PROOF_ROCKETJUMP_BBOX_CENTER) /* standing
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
qboolean SG_OracleRocketJumpAim(vec3_t origin, short pitch, short yaw,
                                vec3_t boom_out, float *flight_ms)
{
	vec3_t forward, start, end, d;
	trace_t tr;
	float travel;

	/* Decode the authenticated controls directly. Re-deriving angles from a
	 * near-vertical float forward vector can flip yaw by 180 degrees and mirror
	 * the right-handed muzzle offset across the player. */
	if (!SG_RocketJumpControlMuzzle(origin, pitch, yaw, start, forward))
		return false;

	VectorMA(start, 8192.0f, forward, end);
	tr = sg_host.trace(start, NULL, NULL, end, NULL, MASK_SHOT);
	if (!SG_RocketJumpWorldImpact(&tr))
		return false;

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
	/* Retired replay compatibility retains its fixed 800 law exactly. Active
	 * generation owns the short-lived proof scope, populated from the exact
	 * integral law captured before generation and reset on every exit. */
	ph->pms.gravity = SG_RuneProofGravity();
	ph->old_pms = ph->pms;
}
