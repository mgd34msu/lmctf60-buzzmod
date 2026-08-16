/* sg_compound_world.h -- read-only live-world resolution for compound RUNE. */
#ifndef SG_COMPOUND_WORLD_H
#define SG_COMPOUND_WORLD_H

#include "sg_action_contract.generated.h"

#define SG_COMPOUND_WORLD_MAX_INERT_DELAY_SECONDS 5.0f

struct edict_s;

/* A resolved PREOPEN mechanism is deliberately one physical leaf.  The
 * scheduler consumes bottom_origin, top_origin and speed without retaining a
 * live edict dependency; trigger/member and mover_key bind later world proof
 * and lease ownership to the exact resolved objects. */
typedef struct sg_compound_world_preopen_s
{
	struct edict_s *trigger;
	struct edict_s *member;
	float bottom_origin[3];
	float top_origin[3];
	float speed;
	float inert_effect_delay;
	int mover_key;
	int axis;
} sg_compound_world_preopen_t;

/* This policy is intentionally distinct from ordinary declared-door policy.
 * With no delay it admits the same sound/areaportal-only effects.  A positive
 * delay is compound-safe only when its allocated DelayedUse has no observable
 * target/message/killtarget effect. */
int SG_CompoundWorldDoorEffectsSafe(const struct edict_s *door);

/* Resolve the exact automatic door trigger touched by a player hull centred
 * at mechanism_anchor.  This is observation only: no trigger, use, target,
 * think, trace, or link callback is invoked. */
rune_reject_reason_t SG_CompoundWorldResolvePreopen(
	const float mechanism_anchor[3],
	sg_compound_world_preopen_t *resolved);

#endif /* SG_COMPOUND_WORLD_H */
