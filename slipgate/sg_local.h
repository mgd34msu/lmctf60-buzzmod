/*
 * sg_local.h -- SLIPGATE internals.
 *
 *     SLIPGATE = RUNE + ARACHNOTRON + CACO
 *
 * RUNE        what the map affords: a link graph over phase space, every
 *             link proven by rolling the real physics before it was written
 * ARACHNOTRON the brain with legs: fields over the rune, a value surface
 *             per bot per moment, a body descending it at physics rate
 * CACO        the eye: belief instead of omniscience, other agents as
 *             advected phase mass, learning into the rune and the weights
 *
 * See slipgate/SLIPGATE.md for the constitution. Principles that bind every
 * file here: physics is read or simulated, never assumed; simulated time
 * sums to real time; facts are measured, only preferences are fitted; every
 * claim is A/B-able against the legacy bots in the same harness.
 */

#pragma once

/* ------------------------------------------------------------------ oracle */

/*
 * A phantom: player-shaped movement state that belongs to no client. The
 * oracle rolls these through gi.Pmove against the live world. pms is
 * authoritative (it is what Pmove reads and writes); origin/velocity are the
 * float decode of it, refreshed after every step.
 */
typedef struct sg_phantom_s
{
	pmove_state_t	pms;
	vec3_t			origin;
	vec3_t			velocity;
	qboolean		groundentity;
	int				waterlevel;
} sg_phantom_t;

void SG_OraclePlace(sg_phantom_t *ph, vec3_t origin);
void SG_OracleRun(sg_phantom_t *ph, usercmd_t *cmd, int steps);
void SG_OracleHookStep(sg_phantom_t *ph, vec3_t anchor);
