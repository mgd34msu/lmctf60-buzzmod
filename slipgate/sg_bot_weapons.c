#include "sg_bot_weapons.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "sg_rune_fire.h"
#include "sg_engine_facts.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* The engine spreads bullets by a random offset of up to N units at 8192
 * units out: as a half-angle, atan(N / 8192). */
#define SPREAD_DEGREES(units) ((float)(atan((double)(units) / 8192.0) * 180.0 / M_PI))

#define RAY_LIMIT SG_FACT_RAY_REACH
#define SHOT_LIMIT SG_FACT_RAY_REACH

#define RAY (SG_RUNE_FIRE_LINE)
#define STRAIGHT (SG_RUNE_FIRE_CORRIDOR)
#define ROCKET (SG_RUNE_FIRE_CORRIDOR | SG_RUNE_FIRE_BLAST)
#define GRENADE (SG_RUNE_FIRE_CORRIDOR | SG_RUNE_FIRE_BLAST | SG_RUNE_FIRE_LOB)

/* Cadences are the weapons' trigger frames at ten frames a second. */
static sg_bot_weapon_t sg_weapons[10];
static int sg_weapons_ready;

static void Build(void)
{
	sg_bot_weapon_t *w = sg_weapons;

	memset(sg_weapons, 0, sizeof(sg_weapons));
	w[0] = (sg_bot_weapon_t){ "Blaster", STRAIGHT, SG_FACT_BLASTER_SPEED, 0.0f, 0,
		SG_FACT_BLASTER_DAMAGE, 1, 0.0f, 0.0f, 0.0f, 0.0f, SG_FACT_BLASTER_SECONDS, 1, SHOT_LIMIT };
	w[1] = (sg_bot_weapon_t){ "Shotgun", RAY, 0.0f, 0.0f, 0, SG_FACT_SHOTGUN_DAMAGE,
		SG_FACT_SHOTGUN_PELLETS, SPREAD_DEGREES(SG_FACT_SHOTGUN_HSPREAD),
		0.0f, 0.0f, 0.0f, SG_FACT_SHOTGUN_SECONDS, 1, RAY_LIMIT };
	w[2] = (sg_bot_weapon_t){ "Super Shotgun", RAY, 0.0f, 0.0f, 0,
		SG_FACT_SUPER_SHOTGUN_DAMAGE, SG_FACT_SUPER_SHOTGUN_PELLETS,
		SPREAD_DEGREES(SG_FACT_SHOTGUN_HSPREAD) +
		SG_FACT_SUPER_SHOTGUN_YAW, 0.0f, 0.0f, 0.0f, SG_FACT_SUPER_SHOTGUN_SECONDS, 1, RAY_LIMIT };
	w[3] = (sg_bot_weapon_t){ "Machinegun", RAY, 0.0f, 0.0f, 0,
		SG_FACT_MACHINEGUN_DAMAGE, 1, SPREAD_DEGREES(SG_FACT_BULLET_HSPREAD),
		0.0f, 0.0f, 0.0f, SG_FACT_MACHINEGUN_SECONDS, 1, RAY_LIMIT };
	w[4] = (sg_bot_weapon_t){ "Chaingun", RAY, 0.0f, 0.0f, 0,
		SG_FACT_CHAINGUN_DAMAGE, 1, SPREAD_DEGREES(SG_FACT_BULLET_HSPREAD),
		0.0f, 0.0f, 0.0f, SG_FACT_CHAINGUN_SECONDS, SG_FACT_CHAINGUN_MAX_SHOTS, RAY_LIMIT };
	w[5] = (sg_bot_weapon_t){ "Grenade Launcher", GRENADE, SG_FACT_GRENADE_SPEED,
		SG_FACT_GRENADE_RISE, 1, SG_FACT_GRENADE_DAMAGE, 1, 0.0f,
		SG_FACT_GRENADE_DAMAGE,
		SG_FACT_GRENADE_RADIUS, 1.0f, SG_FACT_GRENADE_SECONDS, 1,
		SHOT_LIMIT };
	w[6] = (sg_bot_weapon_t){ "Rocket Launcher", ROCKET, SG_FACT_ROCKET_SPEED, 0.0f,
		0, SG_FACT_ROCKET_DAMAGE,
		1, 0.0f, SG_FACT_ROCKET_SPLASH_DAMAGE, SG_FACT_ROCKET_SPLASH_RADIUS,
		0.5f, SG_FACT_ROCKET_SECONDS, 1, SHOT_LIMIT };
	w[7] = (sg_bot_weapon_t){ "HyperBlaster", STRAIGHT, SG_FACT_BLASTER_SPEED,
		0.0f, 0, SG_FACT_HYPERBLASTER_DAMAGE, 1, 0.0f, 0.0f, 0.0f, 0.0f, SG_FACT_HYPERBLASTER_SECONDS, 1,
		SHOT_LIMIT };
	w[8] = (sg_bot_weapon_t){ "Railgun", RAY, 0.0f, 0.0f, 0, SG_FACT_RAILGUN_DAMAGE,
		1, 0.0f, 0.0f, 0.0f, 0.0f, SG_FACT_RAILGUN_SECONDS, 1, RAY_LIMIT };
	w[9] = (sg_bot_weapon_t){ "BFG10K", ROCKET, SG_FACT_BFG_SPEED, 0.0f, 0,
		SG_FACT_BFG_DAMAGE, 1, 0.0f, SG_FACT_BFG_DAMAGE,
		SG_FACT_BFG_CORE_RADIUS, 0.0f, SG_FACT_BFG_SECONDS, 1, SHOT_LIMIT };
	sg_weapons_ready = 1;
}

int SG_BotWeaponCount(void)
{
	return (int)(sizeof(sg_weapons) / sizeof(sg_weapons[0]));
}

const sg_bot_weapon_t *SG_BotWeapon(int index)
{
	if (!sg_weapons_ready)
		Build();
	if (index < 0 || index >= SG_BotWeaponCount())
		return NULL;
	return &sg_weapons[index];
}

int SG_BotWeaponIndex(const char *item)
{
	int i;

	if (!item)
		return -1;
	if (!sg_weapons_ready)
		Build();
	for (i = 0; i < SG_BotWeaponCount(); i++)
		if (!strcmp(sg_weapons[i].item, item))
			return i;
	return -1;
}
