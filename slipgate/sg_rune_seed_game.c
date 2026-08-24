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
	static const float lifts[] = { 8, 24, 40, 56 };
	int lift;

	for (lift = 0; lift < 4; lift++)
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
	if (lift == 4 || trace.fraction == 1.0f || trace.plane.normal[2] < 0.7f)
		return false;
	return SG_OracleCanonicalGroundSource(trace.endpos, out);
}

int SG_RuneSeedSourceWaterlevel(vec3_t origin, int *watertype)
{
	sg_phantom_t phantom;
	usercmd_t command;

	SG_OraclePlace(&phantom, origin);
	memset(&command, 0, sizeof(command));
	command.msec = 0;
	SG_OracleRun(&phantom, &command, 1);
	if (watertype)
		*watertype = phantom.watertype;
	return phantom.waterlevel;
}

qboolean SG_RuneSeedSourceUnstable(vec3_t origin)
{
	vec3_t mins = { -16, -16, -24 }, maxs = { 16, 16, 32 };
	vec3_t start, end;
	sg_phantom_t phantom;
	usercmd_t command;
	short fixed[3];
	trace_t trace;
	int axis, step;

	VectorCopy(origin, start);
	VectorCopy(origin, end);
	start[2] += 1.0f;
	end[2] -= 4.0f;
	trace = sg_host.trace(start, mins, maxs, end, NULL, MASK_PLAYERSOLID);
	if (trace.fraction >= 1.0f || !trace.surface ||
	    (trace.surface->flags & SURF_SLICK) ||
	    (trace.contents & MASK_CURRENT))
		return true;
	SG_OraclePlace(&phantom, origin);
	for (axis = 0; axis < 3; axis++)
		fixed[axis] = phantom.pms.origin[axis];
	memset(&command, 0, sizeof(command));
	command.msec = SEED_STEP_MSEC;
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
