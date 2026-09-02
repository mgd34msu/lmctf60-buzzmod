/* sg_bot_combat.h -- the era-4 fight: perception, weapon choice, aim, fire.
 *
 * Perception is live: what the bot can see from its eyes now, and where it
 * last saw each enemy.  Weapon choice reads the host-parity weapon profiles
 * (family, range, projectile speed, splash) against the range to the
 * target, the ammo in hand, and splash safety.  Aim leads projectiles by
 * the profile's speed and arcs grenades under the map's gravity, with an
 * error that scales with the skill setting and the persona.  Fire is one
 * trace: the shot must reach the target, and a splash must not reach the
 * bot or a teammate.  The RUNE's per-cell weapon relations, when generated,
 * feed the choice with where each weapon works from. */
#ifndef SG_BOT_COMBAT_H
#define SG_BOT_COMBAT_H

/* Once per frame for a bot that owns its view this frame: sets the view
 * angles and the attack button on cmd, and says whether it is engaged. */
void SG_BotCombatFrame(edict_t *self, usercmd_t *cmd, qboolean *engaged_out);

/* Ask the fight to put a shot on a point this frame (a shootable door or
 * button) when no enemy has the view. */
void SG_BotCombatShootAt(edict_t *self, const vec3_t point);

/* The executor's questions. */
qboolean SG_BotLauncherReady(edict_t *self);   /* in hand, ready, loaded */
void SG_BotRequestLauncher(edict_t *self);
qboolean SG_BotHookReady(edict_t *self);       /* the offhand hook may fire */

/* A sound a player made (from the net bridge): bots within earshot note
 * where an enemy was heard. */
void SG_NoteSound(edict_t *emitter, vec3_t origin, int channel,
	int soundindex, float volume, float attenuation);

/* Level and client lifecycle. */
void SG_BotCombatReset(void);
void SG_BotCombatResetClient(edict_t *self);

#endif /* SG_BOT_COMBAT_H */
