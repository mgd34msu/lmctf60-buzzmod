#include "g_local.h"

struct sg_bot_s;

int SG_TrainStationGameEmit(struct sg_bot_s *bot, int selected_link)
{
	(void)bot;
	(void)selected_link;
	return 0;
}

/* The focused execution binary links catalog and delayed-dispatch slices,
 * not the shoot-door game owner or g_func. These identities are sufficient
 * for paths outside that test's declared mechanism fixtures. */
void door_killed(edict_t *self, edict_t *inflictor, edict_t *attacker,
	int damage, vec3_t point)
{
	(void)self;
	(void)inflictor;
	(void)attacker;
	(void)damage;
	(void)point;
}

int SG_ShootDoorGameAuthorizeActivation(edict_t *source,
	edict_t *door_master, edict_t *activator)
{
	(void)source;
	(void)door_master;
	(void)activator;
	return -1;
}

void target_laser_use(edict_t *self, edict_t *other, edict_t *activator)
{
	(void)self;
	(void)other;
	(void)activator;
}

void target_laser_start(edict_t *self)
{
	(void)self;
}

void target_laser_think(edict_t *self)
{
	(void)self;
}

void func_wall_use(edict_t *self, edict_t *other, edict_t *activator)
{
	(void)other;
	(void)activator;
	if (self->solid == SOLID_NOT)
	{
		self->solid = SOLID_BSP;
		self->svflags &= ~SVF_NOCLIENT;
	}
	else
	{
		self->solid = SOLID_NOT;
		self->svflags |= SVF_NOCLIENT;
	}
	if (gi.linkentity)
		gi.linkentity(self);
}

void T_Damage(edict_t *target, edict_t *inflictor, edict_t *attacker,
	vec3_t direction, vec3_t point, vec3_t normal, int damage,
	int knockback, int damage_flags, int means_of_death)
{
	(void)target;
	(void)inflictor;
	(void)attacker;
	(void)direction;
	(void)point;
	(void)normal;
	(void)damage;
	(void)knockback;
	(void)damage_flags;
	(void)means_of_death;
}
