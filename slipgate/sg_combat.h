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
 * Scan for a visible enemy; hold the right weapon for the range band; if a
 * target is held, aim at its firing solution and pull the trigger at a
 * human-ish cadence. *out_engaged is set true when a target is held this frame
 * (may be NULL). Navigation continues regardless -- engaged is information for
 * the caller, not permission to stop moving.
 */
void SG_CombatFrame(edict_t *self, usercmd_t *cmd, qboolean *out_engaged);

/*
 * Item-need weights, WEAPONS.md 2.3. `role` is the static row for the bot's
 * current role, read as a bias; `out` receives that row with the item[] terms
 * scaled by the bot's own state (health, armour deficit, weapon tier, ammo,
 * quad clock, rune) and clamped to [0, 2.0]. Every other member is copied
 * through. The worths behind the scaling are recomputed once a second, the
 * same cadence Fields_Refresh runs on (sg_fields.c:261).
 */
void SG_CombatWeights(edict_t *self, const sg_weights_t *role, sg_weights_t *out);

/*
 * Tell combat this bot is standing a post and how far it can see down the
 * approach, in units (WEAPONS.md 2.4-D3: the pre-held weapon is a function of
 * the sightline length, because the spread saturation distances are hard
 * numbers). Pass a negative sightline when the bot is not posted.
 */
void SG_CombatPost(edict_t *self, float sightline);
/* belief-driven expectation: pre-select for a contact at roughly this
 * range before any line of sight exists; decays in seconds */
void SG_CombatAlert(edict_t *self, float expect_range);
