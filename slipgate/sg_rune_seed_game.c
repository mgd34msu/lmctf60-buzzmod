/* sg_rune_seed_game.c -- exact host-backed RUNE seed canonicalization. */

#include "../g_local.h"
#include "sg_local.h"
#include "sg_hooks.h"
#include "sg_rune_proof.h"
#include "sg_rune_seed_game.h"

#define SEED_STEP_MSEC 25

_Static_assert(SEED_STEP_MSEC == SG_RUNE_PROOF_PMOVE_SUBSTEP_MS,
	"seed stability cadence drift");

qboolean SG_RuneSeedGround(vec3_t candidate, vec3_t out)
{
	vec3_t mins = { -16, -16, -24 }, maxs = { 16, 16, 32 };
	vec3_t start, down;
	trace_t trace;
	static const float lifts[] = { 0, 8, 24, 40, 56 };
	int lift;

	for (lift = 0; lift < 5; lift++)
	{
		VectorCopy(candidate, start);
		start[2] += lifts[lift];
		VectorCopy(start, down);
		down[2] -= 128.0f + lifts[lift];
		trace = sg_host.trace(start, mins, maxs, down, NULL,
			MASK_PLAYERSOLID);
		if (!trace.startsolid && !trace.allsolid &&
		    !(trace.ent && trace.ent->solid == SOLID_BSP &&
		      trace.ent->classname &&
		      !strcmp(trace.ent->classname, "func_rotating")) &&
		    !SG_OracleRotatorSweepBlocks(start, mins, maxs, trace.endpos,
		        MASK_PLAYERSOLID))
			break;
	}
	if (lift == 5 || trace.fraction == 1.0f || trace.plane.normal[2] < 0.7f)
		return false;
	return SG_OracleCanonicalGroundSource(trace.endpos, out);
}

static qboolean RuneSeed_SurfaceAt(const vec3_t sample, qboolean crouched,
	vec3_t out)
{
	static const vec3_t mins = { -16.0f, -16.0f, -24.0f };
	vec3_t maxs = { 16.0f, 16.0f, 32.0f };
	vec3_t down;
	trace_t trace;

	if (crouched)
		maxs[2] = 4.0f;
	VectorCopy(sample, down);
	down[2] -= 32.0f;
	trace = sg_host.trace((vec_t *)sample, (vec_t *)mins, (vec_t *)maxs,
		down, NULL, MASK_PLAYERSOLID);
	if (trace.startsolid || trace.allsolid || trace.fraction >= 1.0f ||
		trace.plane.normal[2] < 0.7f ||
		(trace.ent && trace.ent->solid == SOLID_BSP &&
		 trace.ent->classname &&
		 !strcmp(trace.ent->classname, "func_rotating")) ||
		SG_OracleRotatorSweepBlocks(sample, mins, maxs, trace.endpos,
			MASK_PLAYERSOLID))
		return false;
	return SG_OracleCanonicalGroundSourcePose(trace.endpos, crouched, out);
}

qboolean SG_RuneSeedScanWorldSurfaces(sg_rune_seed_surface_emit_fn emit,
	void *context, uint32_t *scan_count)
{
	const int minimum = -4064;
	const int maximum = 4064;
	const int spacing = 32;
	uint32_t scanned = 0U;
	vec3_t sample, surface;
	int x, y, z;

	if (scan_count)
		*scan_count = 0U;
	if (!emit || !sg_host.trace || !sg_host.pointcontents)
		return false;
	for (z = minimum; z <= maximum; z += spacing)
		for (y = minimum; y <= maximum; y += spacing)
			for (x = minimum; x <= maximum; x += spacing)
			{
				qboolean crouched = false;

				VectorSet(sample, (float)x, (float)y, (float)z);
				scanned++;
				if (sg_host.pointcontents(sample) & CONTENTS_SOLID)
					continue;
				if (!RuneSeed_SurfaceAt(sample, false, surface))
				{
					if (!RuneSeed_SurfaceAt(sample, true, surface))
						continue;
					crouched = true;
				}
				if (!emit(context, surface, crouched))
				{
					if (scan_count)
						*scan_count = scanned;
					return false;
				}
			}
	if (scan_count)
		*scan_count = scanned;
	return true;
}

qboolean SG_RuneSeedLocalContact(const vec3_t first, qboolean first_crouched,
	const vec3_t second, qboolean second_crouched)
{
	static const vec3_t mins = { -16.0f, -16.0f, -24.0f };
	vec3_t maxs = { 16.0f, 16.0f, 32.0f };
	trace_t trace;

	if (!first || !second || !sg_host.trace)
		return false;
	if (first[0] == second[0] && first[1] == second[1] &&
	    first[2] == second[2])
		return true;
	if (first_crouched != second_crouched)
		return false;
	if (first_crouched)
		maxs[2] = 4.0f;
	trace = sg_host.trace((vec_t *)first, (vec_t *)mins, maxs,
	    (vec_t *)second, NULL, MASK_PLAYERSOLID);
	return !trace.startsolid && !trace.allsolid && trace.fraction >= 1.0f &&
	    !SG_OracleRotatorSweepBlocks(first, mins, maxs, second,
	        MASK_PLAYERSOLID);
}

static void RuneSeed_PlacePose(sg_phantom_t *phantom, vec3_t origin,
	qboolean crouched)
{
	usercmd_t command;

	SG_OraclePlace(phantom, origin);
	if (crouched)
		phantom->pms.pm_flags |= PMF_DUCKED;
	memset(&command, 0, sizeof(command));
	command.msec = 0;
	command.upmove = crouched ? -400 : 0;
	SG_OracleRun(phantom, &command, 1);
}

int SG_RuneSeedSourceWaterlevelPose(vec3_t origin, qboolean crouched,
	int *watertype)
{
	sg_phantom_t phantom;

	RuneSeed_PlacePose(&phantom, origin, crouched);
	if (watertype)
		*watertype = phantom.watertype;
	return phantom.waterlevel;
}

int SG_RuneSeedSourceWaterlevel(vec3_t origin, int *watertype)
{
	return SG_RuneSeedSourceWaterlevelPose(origin, false, watertype);
}

qboolean SG_RuneSeedTriggerSafePose(vec3_t origin, qboolean crouched)
{
	sg_phantom_t phantom;

	RuneSeed_PlacePose(&phantom, origin, crouched);
	return SG_OracleWorldTriggerClear(&phantom);
}

qboolean SG_RuneSeedTriggerSafe(vec3_t origin)
{
	return SG_RuneSeedTriggerSafePose(origin, false);
}

qboolean SG_RuneSeedSourceUnstablePose(vec3_t origin, qboolean crouched)
{
	vec3_t mins = { -16, -16, -24 }, maxs = { 16, 16, 32 };
	vec3_t start, end;
	sg_phantom_t phantom;
	usercmd_t command;
	short fixed[3];
	trace_t trace;
	int axis, step;

	if (crouched)
		maxs[2] = 4.0f;
	VectorCopy(origin, start);
	VectorCopy(origin, end);
	start[2] += 1.0f;
	end[2] -= 4.0f;
	trace = sg_host.trace(start, mins, maxs, end, NULL, MASK_PLAYERSOLID);
	if (trace.fraction >= 1.0f || !trace.surface ||
	    (trace.surface->flags & SURF_SLICK) ||
	    (trace.contents & MASK_CURRENT))
		return true;
	RuneSeed_PlacePose(&phantom, origin, crouched);
	for (axis = 0; axis < 3; axis++)
		fixed[axis] = phantom.pms.origin[axis];
	memset(&command, 0, sizeof(command));
	command.msec = SEED_STEP_MSEC;
	command.upmove = crouched ? -400 : 0;
	for (step = 0; step < 4; step++)
		if (!SG_OracleRunWorld(&phantom, &command, 1))
			return true;
	if (!phantom.groundentity || phantom.waterlevel >= 2 ||
	    (phantom.watertype &
	     (MASK_CURRENT | CONTENTS_LAVA | CONTENTS_SLIME)))
		return true;
	for (axis = 0; axis < 3; axis++)
		if (phantom.pms.origin[axis] != fixed[axis] ||
		    phantom.pms.velocity[axis] != 0)
			return true;
	return false;
}

qboolean SG_RuneSeedSourceUnstable(vec3_t origin)
{
	return SG_RuneSeedSourceUnstablePose(origin, false);
}
