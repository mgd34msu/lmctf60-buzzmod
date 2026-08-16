/* sg_compound_world.h -- compound live-world proof and bounded TOP lease. */
#ifndef SG_COMPOUND_WORLD_H
#define SG_COMPOUND_WORLD_H

#include "sg_action_contract.generated.h"

#define SG_COMPOUND_WORLD_MAX_INERT_DELAY_SECONDS 5.0f
#define SG_COMPOUND_WORLD_PREOPEN_HINT_MAX 6

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
	float member_mins[3];
	float member_maxs[3];
	float fixed_angles[3];
	float speed;
	float wait;
	float inert_effect_delay;
	int trigger_key;
	int mover_key;
	int axis;
} sg_compound_world_preopen_t;

/* Generator/loader discovery never reimplements trigger geometry.  Each
 * entry binds one exact safe BOTTOM member to its automatic trigger and
 * carries a bounded, deterministic set of player-centre targets.  Hints are
 * positive-zero eighth-unit coordinates which overlap that trigger while
 * remaining outside the member's complete swept volume. */
typedef struct sg_compound_world_candidate_s
{
	sg_compound_world_preopen_t resolved;
	float hints[SG_COMPOUND_WORLD_PREOPEN_HINT_MAX][3];
	int hint_count;
} sg_compound_world_candidate_t;

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

/* Enumerate every usable safe PREOPEN mechanism once, ordered by
 * (trigger_key, mover_key).  A NULL candidates/zero-capacity call is a count
 * query.  Insufficient capacity and duplicate physical ownership fail before
 * writing any candidate. */
rune_reject_reason_t SG_CompoundWorldEnumeratePreopen(
	sg_compound_world_candidate_t *candidates, int capacity,
	int *count_out);

/* Exact membership in the current candidate's canonical hint set. */
int SG_CompoundWorldPreopenHintMatches(
	const sg_compound_world_preopen_t *resolved, const float hint[3]);

/* Revalidate every copied identity field and return the exact physical leaf.
 * Offline replay uses this seam before temporarily staging that leaf; it must
 * not duplicate or weaken the resolver's pointer/key/static-world contract. */
int SG_CompoundWorldResolvedMember(
	const sg_compound_world_preopen_t *resolved,
	struct edict_s **member_out);

/* Compound-only observation against the exact resolved translating member.
 * These helpers deliberately do not inherit ordinary declared-door policy.
 * Every call revalidates the live pointer/key and copied static identity. */
int SG_CompoundWorldOutsideSweep(
	const sg_compound_world_preopen_t *resolved, const float origin[3]);
int SG_CompoundWorldCrossesSweep(
	const sg_compound_world_preopen_t *resolved, const float from[3],
	const float to[3]);
int SG_CompoundWorldAtTopFor(
	const sg_compound_world_preopen_t *resolved, int window_ms);

/* Runtime-only.  This invokes no entity callback and can only extend an
 * already scheduled canonical TOP close by the shared compound lease. */
int SG_CompoundWorldHoldOpen(
	const sg_compound_world_preopen_t *resolved, int lease_ms);

#endif /* SG_COMPOUND_WORLD_H */
