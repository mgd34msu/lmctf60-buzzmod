

#pragma once

/* sibling-relative, the way sg_local.h includes sg_rune.h: the .c files are
 * symlinked into the build root, so a "slipgate/..." path inside a header that
 * already lives in slipgate/ would not resolve */
#include "sg_local.h"

/*
 * Explicit lifecycle boundaries for process-resident combat state.  A level
 * reset clears every client's beliefs, clocks and diagnostic counters without
 * invalidating the one-time item cache; a client reset protects a recycled
 * engine client index.  Public combat calls also initialize lazily, so call
 * ordering during startup is safe.
 */
void Combat_ResetLevel(void);
void Combat_ResetClient(edict_t *self);

/*
 * Scan for a visible enemy; hold the right weapon for the range band; if a
 * target is held, aim at its firing solution and pull the trigger at a
 * human-ish cadence. *out_engaged is set true when a target is held this frame
 * (may be NULL). Navigation continues regardless -- engaged is information for
 * the caller, not permission to stop moving.
 */
void SG_CombatFrame(edict_t *self, usercmd_t *cmd, qboolean *out_engaged);

/* A fact-only candidate snapshot shared by the execution scan and the
 * current-frame preview. `visibility_known` lets the live scan reject every
 * cheap candidate before paying the PVS/trace, while the preview always sets
 * it and therefore includes the full sight gate. */
typedef struct sg_combat_preview_candidate_s
{
	qboolean self_team_valid;
	qboolean target_team_valid;
	qboolean same_team;
	qboolean same_entity;
	qboolean target_inuse;
	qboolean target_client;
	qboolean target_dead;
	int target_health;
	qboolean target_noclip;
	float distance;
	float range_limit;
	float forward_dot;
	qboolean visibility_known;
	qboolean visible;
} sg_combat_preview_candidate_t;

/* The candidate law used by combat's target scan. It is intentionally pure:
 * callers supply the PVS/trace verdict instead of mutating game or combat
 * state to discover it. */
static inline qboolean SG_CombatPreviewCandidateEligible(
	const sg_combat_preview_candidate_t *candidate)
{
	if (!candidate || candidate->same_entity || !candidate->target_inuse ||
	    !candidate->target_client || candidate->target_dead ||
	    candidate->target_health <= 0 || candidate->target_noclip)
		return false;
	if (!candidate->self_team_valid || !candidate->target_team_valid ||
	    candidate->same_team)
		return false;
	if (candidate->distance < 1.0f ||
	    candidate->distance >= candidate->range_limit ||
	    candidate->forward_dot < 0.5f)
		return false;
	return !candidate->visibility_known || candidate->visible;
}

/* Read-only current-frame forward-visible engagement preview. It performs the
 * same team, liveness, noclip, persona/tilt range, cone, PVS and trace tests
 * as the normal target scan, but never touches combat state, diagnostics, a
 * command, weapon, view, or trigger. The recent-hit rear cone intentionally
 * remains execution-only: this preview closes the confirmed forward-visible
 * gap without adding a second combat reaction path. */
qboolean SG_CombatWouldEngage(edict_t *self);

/*
 * Item-need weights, WEAPONS.md 2.3. `role` is the static row for the bot's
 * current role, read as a bias; `out` receives that row with the item[] terms
 * scaled by the bot's own state (health, armour deficit, weapon tier, ammo,
 * quad clock, rune) and clamped to [0, 2.0]. Every other member is copied
 * through. The worths behind the scaling are recomputed once a second, the
 * same cadence Fields_Refresh runs on (sg_fields.c:261).
 */
void SG_CombatWeights(edict_t *self, const sg_weights_t *role, sg_weights_t *out);

/* Convert the already role/threat-scaled rune-class worth to the worth of one
 * exact believed rune entity.  Zero means the entity is not a collectible
 * rune for this client. */
float SG_RuneRouteWorth(edict_t *self, edict_t *rune, float class_worth);

/* Authoritative weapon state read from the live inventory and ammo pools.
 * `available_tier` uses the same Combat_Avail predicate as Use_Weapon;
 * `stocked_tier` additionally applies the three-second floor used by the
 * combat ladders.  The two booleans deliberately distinguish a usable gun
 * from a gun worth making a supply trip for. */
typedef struct sg_combat_weapon_state_s
{
	int		available_tier;
	int		stocked_tier;
	int		held_weapon;
	qboolean	nonblaster_available;
	qboolean	nonblaster_stocked;
} sg_combat_weapon_state_t;

qboolean SG_CombatWeaponState(edict_t *self,
	                         sg_combat_weapon_state_t *out);

/* Ammo tag consumed by the weapon currently in hand, or -1 when that weapon
 * has no ammo pool.  This is the identity paired with Worth_Ammo. */
int SG_CombatHeldAmmoTag(edict_t *self);

/* Acquisition tier of this exact weapon entity under the same ladder used by
 * SG_CombatWeaponState, or 0 when the item is not in the ordinary upgrade
 * ladder.  Physical pickup admission remains the game item's authority. */
int SG_CombatWeaponPickupTier(const edict_t *item);

/* Read-only production selector used by posted defenders.  The return value
 * is the combat weapon index (0 is the blaster, negative is invalid input),
 * and is the exact selector consumed by the live combat tick. */
int SG_CombatBestPostWeapon(edict_t *self, float sightline);

/*
 * The overheal worth of the mega for this bot, 0 to 2.0 (sg_megaworth). Not a
 * class weight: SG_FC_HEALTH prices "a health box, given my state" and collapses
 * at full health, while the mega is +100 OVER max and is worth taking at 100/100.
 * Zero whenever the cvar is off, whenever no mega is believed up by this bot's
 * TEAM, and whenever the bot is already carrying the overheal. The role gate and
 * the detour budget live with the router in sg_arach.c; this is the state half.
 */
float SG_WorthMega(edict_t *self);

/*
 * Tell combat this bot is standing a post and how far it can see down the
 * approach, in units (WEAPONS.md 2.4-D3: the pre-held weapon is a function of
 * the sightline length, because the spread saturation distances are hard
 * numbers). Pass a negative sightline when the bot is not posted.  The
 * stocked-post fallback is reserved for a defender holding its stand; other
 * post-like holds retain the ordinary selector.
 */
void SG_CombatPost(edict_t *self, float sightline,
                   qboolean defender_stand);
/* belief-driven expectation: pre-select for a contact at roughly this
 * range before any line of sight exists; decays in seconds */
void SG_CombatAlert(edict_t *self, float expect_range);
void SG_CombatAlertFromBeliefs(edict_t *self, const int *goal_field);


qboolean SG_CombatDuel(edict_t *self, vec3_t enemy_org, float *want_range,
                       float *exposure_w);

/*
 * Whether this bot may hold a corner on a target it just lost sight of.
 * The role decides: a carrier and its escort have somewhere to be, and a
 * defender only holds ground it is already standing on. Said every frame --
 * false clears any hold in progress, so a role change ends the camp on the
 * frame it happens.
 */
void SG_CombatPursuit(edict_t *self, qboolean allowed);

/* the live enemy entity, or NULL (grenade landing-lead consumer) */
edict_t *SG_CombatLiveEnemy(edict_t *self);


int SG_CombatSkill(edict_t *self);

/* debug: the trigger-veto tally, printed every 5s on sg_debug */
void SG_CombatWhy(void);
