/*
 * sg_combat.h -- SLIPGATE combat: view, weapon and trigger, never movement.
 *
 * Per the constitution (slipgate/SLIPGATE.md): "Combat runs concurrently with
 * navigation. There is no state that suspends movement." SG_CombatFrame is
 * therefore a MODIFIER on an already-built usercmd_t: it may overwrite
 * cmd->angles and cmd->buttons, and it touches nothing else. forwardmove,
 * sidemove and upmove belong to the Body and are left exactly as found.
 *
 * The one other thing it does is ASK for a weapon, through the same entry a
 * player's "use <name>" runs: FindItem, then it->use (g_cmds.c:667-687,
 * WEAPONS.md 2.0). That sets client->newweapon (p_weapon.c:376); the engine
 * performs the change itself. No inventory is invented and no cvar is touched.
 *
 * ---------------------------------------------------------------- integration
 * Place SG_CombatFrame after the movement policy has filled cmd and after the
 * hook aim override, but BEFORE ClientThink(e, &cmd):
 *
 *     #include "slipgate/sg_combat.h"                 // with the other includes
 *     qboolean engaged = false;                       // near the cmd declaration
 *     if (bot->hook_phase != 1)                       // rope LAUNCH outranks combat
 *         SG_CombatFrame(e, &cmd, &engaged);          // view+trigger only
 *
 * The gate is hook_phase == 1 and nothing wider. On that one frame the cmd
 * angles ARE the anchor bearing (sg_arach.c:603-620) and Weapon_Hook_Fire
 * launches along v_angle, so combat must not steal the view. Once the rope is
 * out it is sustained by ClientEndServerFrame with no button and no view input
 * (p_view.c:988-990), so BUTTON_ATTACK is free and combat runs normally --
 * WEAPONS.md 2.4-D2 turns on exactly that fact. The rope only conflicts with
 * the trigger when the grapple is pers.weapon (g_cmds.c:1408-1411), which
 * SG_CombatFrame never allows to happen (WEAPONS.md rule S3).
 */

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

/*
 * The duel, as terms rather than as a mode.
 *
 * Dueling is not a behaviour this file enters; it is two numbers the value
 * surface can price a candidate seed with, plus the position they are about.
 * The constitution's rule holds unchanged -- nothing here suspends movement,
 * and nothing here writes a usercmd. The Body decides what to do with them.
 *
 * Returns true when the bot has a target it is holding this frame, or held
 * within the last two seconds (belief outlives line of sight; the enemy table
 * may hold a fresher fix than this bot's own last look, and is preferred when
 * it does). On true:
 *
 *   enemy_org    where the target is believed to be. NULL to skip.
 *   want_range   the distance the weapon in HAND wants against the weapon the
 *                target was last SEEN holding -- their weapon is on their
 *                player model, so it is an eye fact, not an inventory read.
 *                Derived from the 2.1 ladders already in this file: a weapon's
 *                preferred range is the rank-weighted mean of the centres of
 *                the bands whose ladders name it. NULL to skip.
 *   exposure_w   what being visible to that target costs right now, 0 to ~1.
 *                Healthy and in band is near zero -- a fight you are winning
 *                is not one to hide from. Hurt, or holding a weapon this range
 *                does not suit, drives it to one. NULL to skip.
 *
 * False leaves every out untouched at its zero; the caller's early-out.
 */
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

/*
 * This bot's effective skill, times 100 -- 0 to 400, for telemetry that wants
 * an int. It is the bot_skill cvar clamped to a 0..4 team level, minus a fixed
 * personal handicap of 0 to 1 levels derived from the client index, so two bots
 * on one server setting are not the same shooter: the sixteen names sg_arach.c
 * spawns split into five recognisable grades of aim, reaction, trigger cadence
 * and lead. The cvar names the team's best, not its average.
 *
 * Skill 4 is the ceiling and is the behaviour that shipped: the aim ramp, its
 * floor and the fire windows are all identical there to the constants used
 * before there was a skill model, and the lead error is exactly zero. Every
 * skill below 4 is worse, never better. The one thing skill 4 pays that the
 * old code did not is a 0.12 s reaction on a new target, which is a shade over
 * one server frame.
 *
 * Safe on any edict; a non-client returns the team level alone.
 */
int SG_CombatSkill(edict_t *self);

/* debug: the trigger-veto tally, printed every 5s on sg_debug */
void SG_CombatWhy(void);
