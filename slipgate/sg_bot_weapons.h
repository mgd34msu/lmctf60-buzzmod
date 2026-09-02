/* sg_bot_weapons.h -- what each weapon is, in the RUNE's own terms.
 *
 * A weapon reaches a target through the same relations the RUNE records
 * between cells: a ray needs a line, a projectile needs a corridor, a
 * rocket can also work by its burst at the feet, a grenade by its lob.
 * So each record names its reach as fire-relation bits, and the fight
 * asks one question of the pair of cells it is in: does this weapon's
 * reach meet what the rune says is open.  The rest is what the shot is:
 * how it flies, what one hit does, what the burst does and how far, how
 * wide the spread is, how often it fires.  Every number is the engine's. */
#ifndef SG_BOT_WEAPONS_H
#define SG_BOT_WEAPONS_H

#include <stdint.h>

typedef struct sg_bot_weapon_s
{
	const char *item;         /* the game's pickup name */
	uint32_t reach;           /* sg_rune_fire_flag_t bits the weapon works through */
	float speed;              /* projectile speed; 0 for an instant ray */
	float rise;               /* upward launch speed added (grenades) */
	int falls;                /* the projectile falls under gravity */
	float hit;                /* damage of one direct hit, per pellet */
	int pellets;              /* pellets per trigger */
	float spread;             /* half-angle of the cone, degrees */
	float burst;              /* damage at the burst's centre, 0 for none */
	float radius;             /* the burst's reach */
	float self_burst;         /* fraction of the burst the shooter takes */
	float seconds;            /* between triggers */
	int shots;                /* triggers per cycle: the chaingun spun up */
	float limit;              /* how far the shot can reach at all */
} sg_bot_weapon_t;

int SG_BotWeaponCount(void);
const sg_bot_weapon_t *SG_BotWeapon(int index);

/* The index whose item name this is, or -1. */
int SG_BotWeaponIndex(const char *item);

#endif /* SG_BOT_WEAPONS_H */
