/*
 * sg_combat.h -- SLIPGATE combat: view and trigger, never movement.
 *
 * Per the constitution (slipgate/SLIPGATE.md): "Combat runs concurrently with
 * navigation. There is no state that suspends movement." SG_CombatFrame is
 * therefore a MODIFIER on an already-built usercmd_t: it may overwrite
 * cmd->angles and cmd->buttons, and it touches nothing else. forwardmove,
 * sidemove and upmove belong to the Body and are left exactly as found.
 *
 * ---------------------------------------------------------------- integration
 * For ARACHNOTRON to adopt this (NOT applied here -- sg_arach.c is owned by
 * another agent right now). Place it after the movement policy has filled cmd
 * and after the hook aim override, but BEFORE ClientThink(e, &cmd):
 *
 *     #include "slipgate/sg_combat.h"                 // with the other includes
 *     qboolean engaged = false;                       // near the cmd declaration
 *     if (bot->hook_phase == 0)                       // rope aim outranks combat
 *         SG_CombatFrame(e, &cmd, &engaged);          // view+trigger only
 *
 * The hook_phase guard is the one ordering rule that matters: when
 * bot->hook_phase == 1 the cmd angles ARE the anchor bearing (sg_arach.c:603-620)
 * and Weapon_Hook_Fire launches along v_angle, so combat must not steal the view
 * on that frame. Every other frame combat is free to aim, and movement is
 * unaffected either way.
 */

#pragma once

/*
 * Scan for a visible enemy; if one is held, aim at a lead point and pull the
 * trigger at a human-ish cadence. *out_engaged is set true when a target is
 * held this frame (may be NULL). Navigation continues regardless -- engaged is
 * information for the caller, not permission to stop moving.
 */
void SG_CombatFrame(edict_t *self, usercmd_t *cmd, qboolean *out_engaged);
