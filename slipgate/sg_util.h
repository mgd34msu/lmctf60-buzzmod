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

#endif
