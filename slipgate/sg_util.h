/*
 * sg_util.h -- the patterns every SLIPGATE file kept re-typing.
 *
 * Born in the 2026-08-11 standards pass: flag-entity resolution, the
 * stand-marker lookup, XY distance, and the eye-to-point sight trace
 * each existed as five to eleven hand-rolled copies.  One copy each,
 * here, and a call site reads as intent instead of plumbing.
 */
#ifndef SG_UTIL_H
#define SG_UTIL_H

/* Team/index conversions -- CTF_TEAM_RED and CTF_TEAM_BLUE (1 and 2,
 * g_ctffunc.h) are the wire values, but most per-team state is a
 * 2-element array (row 0 red, row 1 blue), and finding the OTHER
 * team came up just as often. Both directions were hand-derived at
 * well over a hundred sites -- `team - CTF_TEAM_RED` as an array
 * index, `(team == CTF_TEAM_RED) ? 0 : 1` written the other way
 * around, an enemy team spelled out as a ternary -- each a fresh
 * chance for the two forms to drift.
 *
 *   SG_TeamIdx(team)     team - CTF_TEAM_RED       (0 red, 1 blue)
 *   SG_TeamFromIdx(idx)  CTF_TEAM_RED + idx         (inverse of above)
 *   SG_EnemyTeam(team)   the other of RED/BLUE
 *
 * All three assume team is CTF_TEAM_RED or CTF_TEAM_BLUE; a caller
 * that has not already ruled out CTF_TEAM_UNDEFINED (or a spectator)
 * rules it out itself, the same discipline the sites they replace
 * already required of themselves. */
int	SG_TeamIdx(int team);
int	SG_TeamFromIdx(int idx);
int	SG_EnemyTeam(int team);

/* The live flag ITEMS (droptofloor-settled, the thing a touch scores
 * on), by the engine's own pointers.  NULL when absent or carried. */
edict_t	*SG_OwnFlag(int team);      /* the flag this team defends */
edict_t	*SG_EnemyFlag(int team);    /* the flag this team steals */

/* The info_flag_* spawn MARKER -- the stand's advertised position,
 * common knowledge under Rule 19 even when the item is elsewhere. */
edict_t	*SG_FlagStand(int team, qboolean own);

/* Horizontal distance -- the pattern behind most range gates. */
float	SG_DistXY(const vec3_t a, const vec3_t b);

/* Eye-to-point sight: can this entity's viewpoint see pt (lifted by
 * lift_z) through MASK_OPAQUE?  The standard perception trace. */
qboolean SG_CanSee(edict_t *e, const vec3_t pt, float lift_z);

/* Timer/cooldown primitives -- the two hand-rolled patterns against
 * level.time (a float, seconds, restarts at zero on changelevel) that
 * this tree kept re-typing at well over a hundred sites, each a fresh
 * read of the same idiom with occasional > vs >= drift between them.
 *
 * A DEADLINE field holds a future level.time value ("ready at X"):
 *   SG_TimerArm(&field, delay)    field = level.time + delay
 *   SG_TimerReady(field)          level.time >= field
 *   SG_TimerReadyStrict(field)    level.time >  field
 *   SG_TimerPending(field)        level.time <  field
 *   SG_TimerRemaining(field)      field - level.time
 *
 * A SINCE field holds a past level.time value ("started at X"):
 *   SG_Mark(&field)               field = level.time
 *   SG_Age(field)                 level.time - field
 *   SG_AgeOver(field, span)       (level.time - field) >  span
 *   SG_AgeAtLeast(field, span)    (level.time - field) >= span
 *   SG_AgeUnder(field, span)      (level.time - field) <  span
 *
 * A comparison written with the operands reversed at the call site
 * (e.g. "field > level.time" instead of "level.time < field") still
 * picks whichever of these matches the ORIGINAL operator -- reversing
 * operand order on a comparison changes nothing arithmetically, but
 * swapping > for >= does, and that edge is exactly what these keep:
 * SG_TimerReady and SG_TimerReadyStrict are not interchangeable, and
 * neither are SG_AgeOver, SG_AgeAtLeast, and SG_AgeUnder. */
void     SG_TimerArm(float *stamp, float delay);
qboolean SG_TimerReady(float stamp);
qboolean SG_TimerReadyStrict(float stamp);
qboolean SG_TimerPending(float stamp);
float    SG_TimerRemaining(float stamp);

void     SG_Mark(float *stamp);
float    SG_Age(float since);
qboolean SG_AgeOver(float since, float span);
qboolean SG_AgeAtLeast(float since, float span);
qboolean SG_AgeUnder(float since, float span);

#endif
