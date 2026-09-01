// g_weapon.c

#include "g_local.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_compound_guard_game.h"
#include "slipgate/sg_compound_hook_game_events.h"
#include "slipgate/sg_host_law_owner.h"
#include "slipgate/sg_host_law_owner_internal.h"
#include "slipgate/sg_human_trace.h"
#ifdef world
#define SG_P_WEAPON_RESTORE_WORLD
#undef world
#endif
#include "slipgate/sg_host_hook_law.h"
#ifdef SG_P_WEAPON_RESTORE_WORLD
#define world (&g_edicts[0])
#undef SG_P_WEAPON_RESTORE_WORLD
#endif
#include "m_player.h"
#include "g_tourney.h"

// SKWiD MOD
#include "plasma.h"
extern void fire_plasma (edict_t *ent, vec3_t start, vec3_t dir, int mode);
extern void Weapon_PLASMA_Generic (edict_t *,int,int,int,int,int *,int *,void(*fire)(edict_t *));
// END
#include "g_ctffunc.h"
#include "slipgate/sg_cvars.h"
#include "slipgate/sg_action_contract.generated.h"

void SG_NoteRailShot(edict_t *shooter);

#define GRAPPLE_FIRE_HOOK_SPEED        SG_HOST_HOOK_FIRE_SPEED
#define GRAPPLE_PULL_SPEED             SG_HOST_HOOK_PULL_SPEED
#define GRAPPLE_PULL_BALANCED_SPEED    SG_HOST_HOOK_PULL_SPEED

/* Capture the hook constants from the same translation unit that executes the
 * weapon.  Tests and default helpers cannot replace this callback. */
int SG_HostHookLiveCapture(sg_host_hook_law_t *law_out)
{
	if (!law_out)
		return 0;
	memset(law_out, 0, sizeof(*law_out));
	law_out->version = SG_HOST_HOOK_LAW_VERSION;
	law_out->trace_mask = MASK_SHOT;
	law_out->muzzle_forward_offset = SG_HOST_HOOK_MUZZLE_FORWARD_OFFSET;
	law_out->muzzle_right_offset = SG_HOST_HOOK_MUZZLE_RIGHT_OFFSET;
	law_out->muzzle_view_offset = SG_HOST_HOOK_MUZZLE_VIEW_OFFSET;
	law_out->fire_speed = GRAPPLE_FIRE_HOOK_SPEED;
	law_out->pull_speed = GRAPPLE_PULL_SPEED;
	law_out->initial_damage = SG_HOST_HOOK_INITIAL_DAMAGE;
	law_out->attached_damage = SG_HOST_HOOK_ATTACHED_DAMAGE;
	law_out->projectile_health = SG_HOST_HOOK_HEALTH;
	law_out->attached_cadence_frames = SG_HOST_HOOK_ATTACHED_CADENCE;
	law_out->trace_epsilon = SG_HOST_HOOK_TRACE_EPSILON;
	if (ctfflags && isfinite(ctfflags->value) && ctfflags->value >= 0.0f &&
		ctfflags->value <= (float)INT_MAX &&
		truncf(ctfflags->value) == ctfflags->value)
		law_out->no_grapple_damage =
			((uint32_t)ctfflags->value & SG_HOST_HOOK_CTF_NO_GRAP_DAMAGE) != 0U;
	law_out->identity = SG_HOST_HOOK_LAW_ID;
	law_out->near_bite_distance = SG_HOST_HOOK_NEAR_BITE_DISTANCE;
	law_out->near_bite_gravity_zero_distance =
		SG_HOST_HOOK_NEAR_BITE_GRAVITY_ZERO_DISTANCE;
	return 1;
}

static qboolean	is_quad;
static byte		is_silenced;

qboolean CheckTeamDamage (edict_t *targ, edict_t *attacker); // CTF CODE -- LM_JORM

void weapon_grenade_fire (edict_t *ent, qboolean held);


static void P_ProjectSource (gclient_t *client, vec3_t point, vec3_t distance, vec3_t forward, vec3_t right, vec3_t result)
{
	vec3_t	_distance;

	VectorCopy (distance, _distance);
	if (client->pers.hand == LEFT_HANDED)
		_distance[1] *= -1;
	else if (client->pers.hand == CENTER_HANDED)
		_distance[1] = 0;
	G_ProjectSource (point, _distance, forward, right, result);
}


/*
===============
PlayerNoise

Each player can have two noise objects associated with it:
a personal noise (jumping, pain, weapon firing), and a weapon
target noise (bullet wall impacts)

Monsters that don't directly see the player can move
to a noise in hopes of seeing the player from there.
===============
*/
void PlayerNoise(edict_t *who, vec3_t where, int type)
{
	edict_t		*noise;

	if (type == PNOISE_WEAPON)
	{
		if (who->client->silencer_shots)
		{
			who->client->silencer_shots--;
			return;
		}
	}

	if (deathmatch->value)
		return;

	if (who->flags & FL_NOTARGET)
		return;


	if (!who->mynoise)
	{
		noise = G_Spawn();
		noise->classname = "player_noise";
		VectorSet (noise->mins, -8, -8, -8);
		VectorSet (noise->maxs, 8, 8, 8);
		noise->owner = who;
		noise->svflags = SVF_NOCLIENT;
		who->mynoise = noise;

		noise = G_Spawn();
		noise->classname = "player_noise";
		VectorSet (noise->mins, -8, -8, -8);
		VectorSet (noise->maxs, 8, 8, 8);
		noise->owner = who;
		noise->svflags = SVF_NOCLIENT;
		who->mynoise2 = noise;
	}

	if (type == PNOISE_SELF || type == PNOISE_WEAPON)
	{
		noise = who->mynoise;
		level.sound_entity = noise;
		level.sound_entity_framenum = level.framenum;
	}
	else // type == PNOISE_IMPACT
	{
		noise = who->mynoise2;
		level.sound2_entity = noise;
		level.sound2_entity_framenum = level.framenum;
	}

	VectorCopy (where, noise->s.origin);
	VectorSubtract (where, noise->maxs, noise->absmin);
	VectorAdd (where, noise->maxs, noise->absmax);
	noise->teleport_time = level.time;
	gi.linkentity (noise);
}


qboolean G_WeaponPickupEligible(edict_t *ent, edict_t *other)
{
	int index;

	/* Keep route selection and the physical pickup on one law.  In
	 * particular, a world weapon already owned under WEAPONS_STAY is visible
	 * and solid but cannot be collected by this client. */
	if (!ent || !ent->item || !other || !other->client ||
	    matchstate == MATCH_RAILGUN_INPLAY)
		return false;
	index = ITEM_INDEX(ent->item);
	if (((((int)dmflags->value & DF_WEAPONS_STAY) || coop->value) &&
	     other->client->pers.inventory[index]) &&
	    !(ent->spawnflags & (DROPPED_ITEM | DROPPED_PLAYER_ITEM)))
		return false;
	return true;
}

qboolean Pickup_Weapon (edict_t *ent, edict_t *other)
{
	int			index;
	gitem_t		*ammo;

	if (!G_WeaponPickupEligible(ent, other))
		return(false);

	index = ITEM_INDEX(ent->item);

	other->client->pers.inventory[index]++;

	if (!(ent->spawnflags & DROPPED_ITEM) )
	{
		// give them some ammo with it
		ammo = FindItem (ent->item->ammo);
		if ( (int)dmflags->value & DF_INFINITE_AMMO )
			Add_Ammo (other, ammo, 1000);
		else
			Add_Ammo (other, ammo, ammo->quantity);

		if (! (ent->spawnflags & DROPPED_PLAYER_ITEM) )
		{
			if (deathmatch->value)
			{
				if ((int)(dmflags->value) & DF_WEAPONS_STAY)
					ent->flags |= FL_RESPAWN;
				else
					SetRespawn (ent, 30);
			}
			if (coop->value)
				ent->flags |= FL_RESPAWN;
		}
	}

	if (other->client->pers.weapon != ent->item && 
		(other->client->pers.inventory[index] == 1) &&
		( !deathmatch->value || other->client->pers.weapon == FindItem("blaster") ) )
		other->client->newweapon = ent->item;

	return true;
}


/*
===============
ChangeWeapon

The old weapon has been dropped all the way, so make the new one
current
===============
*/
void ChangeWeapon (edict_t *ent)
{
	int i;

	if(matchstate == MATCH_RAILGUN_INPLAY && ent->health > 0)
	{
		ent->client->newweapon = FindItem ("railgun");
		return;
	}



	if (ent->client->grenade_time)
	{
		ent->client->grenade_time = level.time;
		ent->client->weapon_sound = 0;
		weapon_grenade_fire (ent, false);
		ent->client->grenade_time = 0;
	}

	ent->client->pers.lastweapon = ent->client->pers.weapon;
	ent->client->pers.weapon = ent->client->newweapon;
	ent->client->newweapon = NULL;
	ent->client->machinegun_shots = 0;

	// set visible model
	if (ent->s.modelindex == 255) {
		if (ent->client->pers.weapon)
			i = ((ent->client->pers.weapon->weapmodel & 0xff) << 8);
		else
			i = 0;
		ent->s.skinnum = (ent - g_edicts - 1) | i;
	}

	if (ent->client->pers.weapon && ent->client->pers.weapon->ammo)
		ent->client->ammo_index = ITEM_INDEX(FindItem(ent->client->pers.weapon->ammo));
	else
		ent->client->ammo_index = 0;

	if (!ent->client->pers.weapon)
	{	// dead
		ent->client->ps.gunindex = 0;
		return;
	}

	ent->client->weaponstate = WEAPON_ACTIVATING;
	ent->client->ps.gunframe = 0;
	ent->client->ps.gunindex = gi.modelindex(ent->client->pers.weapon->view_model);

	ent->client->anim_priority = ANIM_PAIN;
	if(ent->client->ps.pmove.pm_flags & PMF_DUCKED)
	{
			ent->s.frame = FRAME_crpain1;
			ent->client->anim_end = FRAME_crpain4;
	}
	else
	{
			ent->s.frame = FRAME_pain301;
			ent->client->anim_end = FRAME_pain304;
			
	}
}

/*
=================
NoAmmoWeaponChange
=================
*/
void NoAmmoWeaponChange (edict_t *ent)
{
	if ( ent->client->pers.inventory[ITEM_INDEX(FindItem("slugs"))]
		&&  ent->client->pers.inventory[ITEM_INDEX(FindItem("railgun"))] )
	{
		ent->client->newweapon = FindItem ("railgun");
		return;
	}
	if ( ent->client->pers.inventory[ITEM_INDEX(FindItem("cells"))]
		&&  ent->client->pers.inventory[ITEM_INDEX(FindItem("hyperblaster"))] )
	{
		ent->client->newweapon = FindItem ("hyperblaster");
		return;
	}
	if ( ent->client->pers.inventory[ITEM_INDEX(FindItem("bullets"))]
		&&  ent->client->pers.inventory[ITEM_INDEX(FindItem("chaingun"))] )
	{
		ent->client->newweapon = FindItem ("chaingun");
		return;
	}
	if ( ent->client->pers.inventory[ITEM_INDEX(FindItem("bullets"))]
		&&  ent->client->pers.inventory[ITEM_INDEX(FindItem("machinegun"))] )
	{
		ent->client->newweapon = FindItem ("machinegun");
		return;
	}
	if ( ent->client->pers.inventory[ITEM_INDEX(FindItem("shells"))] > 1
		&&  ent->client->pers.inventory[ITEM_INDEX(FindItem("super shotgun"))] )
	{
		ent->client->newweapon = FindItem ("super shotgun");
		return;
	}
	if ( ent->client->pers.inventory[ITEM_INDEX(FindItem("shells"))]
		&&  ent->client->pers.inventory[ITEM_INDEX(FindItem("shotgun"))] )
	{
		ent->client->newweapon = FindItem ("shotgun");
		return;
	}
	ent->client->newweapon = FindItem ("blaster");
}

/*
=================
Think_Weapon

Called by ClientBeginServerFrame and ClientThink
=================
*/
void Think_Weapon (edict_t *ent)
{
	is_quad = (ent->client->quad_framenum > level.framenum);

	// if just died, put the weapon away
	if (ent->health < 1)
	{
		ent->client->newweapon = NULL;
		ChangeWeapon (ent);
	}

	// call active weapon think routine
	if (ent->client->pers.weapon && ent->client->pers.weapon->weaponthink)
	{
		if (ent->client->silencer_shots)
			is_silenced = MZ_SILENCED;
		else
			is_silenced = 0;
		ent->client->pers.weapon->weaponthink (ent);
		RuneWeaponThinkHook (ent);
		
		// LM_JORM -- Switch to next weapon if out of ammo

		/*
		if (ent->client->ammo_index && 
			ent->client->pers.inventory[ent->client->ammo_index] < ent->client->pers.weapon->quantity)
		{
			if (level.time >= ent->pain_debounce_time)
			{
				gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/noammo.wav"), 1, ATTN_NORM, 0);
				ent->pain_debounce_time = level.time + 1;
			}
			//NoAmmoWeaponChange (ent);
			Cmd_WeapNext_f(ent);
		}
		*/

		// LM_JORM -- Speed up all weapons!

		/*
		if (!(level.framenum % 4))
		{
			if (ent->client->ps.gunframe)
				ent->client->pers.weapon->weaponthink (ent);
		}
		*/
		// END -- LM_JORM
	}
}


/*
================
Use_Weapon

Make the weapon ready if there is ammo
================
*/
void Use_Weapon (edict_t *ent, gitem_t *item)
{
	int			ammo_index;
	gitem_t		*ammo_item;
	char message[MAX_INFO_STRING];

	// see if we're already using it
	if (item == ent->client->pers.weapon)
		return;

	if (item->ammo && !g_select_empty->value && !(item->flags & IT_AMMO))
	{
		ammo_item = FindItem(item->ammo);
		ammo_index = ITEM_INDEX(ammo_item);

		if (!ent->client->pers.inventory[ammo_index])
		{
			sprintf(message, "No %s for %s.\n", ammo_item->pickup_name, item->pickup_name);
			ctf_SafePrint(ent, PRINT_HIGH, message);
			return;
		}

		if (ent->client->pers.inventory[ammo_index] < item->quantity)
		{
			sprintf(message, "Not enough %s for %s.\n", ammo_item->pickup_name, item->pickup_name);
			ctf_SafePrint(ent, PRINT_HIGH, message);
			return;
		}
	}

	// change to this weapon when down
	ent->client->newweapon = item;
}



/*
================
Drop_Weapon
================
*/
void Drop_Weapon (edict_t *ent, gitem_t *item)
{
	int		index;

	if ((int)(dmflags->value) & DF_WEAPONS_STAY)
		return;

	index = ITEM_INDEX(item);
	// see if we're already using it
	if ( ((item == ent->client->pers.weapon) || (item == ent->client->newweapon))&& (ent->client->pers.inventory[index] == 1) )
	{
		ctf_SafePrint(ent, PRINT_HIGH, "Can't drop current weapon\n");
		return;
	}

	Drop_Item (ent, item);
	ent->client->pers.inventory[index]--;
}


/*
================
Weapon_Generic

A generic function to handle the basics of weapon thinking
================
*/
#define FRAME_FIRE_FIRST		(FRAME_ACTIVATE_LAST + 1)
#define FRAME_IDLE_FIRST		(FRAME_FIRE_LAST + 1)
#define FRAME_DEACTIVATE_FIRST	(FRAME_IDLE_LAST + 1)

void Weapon_Generic (edict_t *ent, int FRAME_ACTIVATE_LAST, int FRAME_FIRE_LAST, int FRAME_IDLE_LAST, int FRAME_DEACTIVATE_LAST, int *pause_frames, int *fire_frames, void (*fire)(edict_t *ent))
{
	int		n;
	ent->client->isfiring = 0; // By default, we aren't firing;

	if(ent->deadflag || ent->s.modelindex != 255) // VWep animations screw up corpses
	{
		return;
	}

	if (ent->client->weaponstate == WEAPON_DROPPING)
	{
		if (ent->client->ps.gunframe == FRAME_DEACTIVATE_LAST)
		{
			ChangeWeapon (ent);
			return;
		}
		else if ((FRAME_DEACTIVATE_LAST - ent->client->ps.gunframe) == 4)
		{
			ent->client->anim_priority = ANIM_REVERSE;
			if(ent->client->ps.pmove.pm_flags & PMF_DUCKED)
			{
				ent->s.frame = FRAME_crpain4+1;
				ent->client->anim_end = FRAME_crpain1;
			}
			else
			{
				ent->s.frame = FRAME_pain304+1;
				ent->client->anim_end = FRAME_pain301;
				
			}
		}

		ent->client->ps.gunframe++;
		return;
	}

	if (ent->client->weaponstate == WEAPON_ACTIVATING)
	{
		if (fastswitch->value) {
			ent->client->ps.gunframe = FRAME_ACTIVATE_LAST;
		}

		if (ent->client->ps.gunframe == FRAME_ACTIVATE_LAST)
		{
			ent->client->weaponstate = WEAPON_READY;
			ent->client->ps.gunframe = FRAME_IDLE_FIRST;
			return;
		}

		ent->client->ps.gunframe++;
		return;
	}

	if ((ent->client->newweapon) && (ent->client->weaponstate != WEAPON_FIRING))
	{
		ent->client->weaponstate = WEAPON_DROPPING;
		if (fastswitch->value) {
			ChangeWeapon(ent);
			return;
		} else {
			ent->client->ps.gunframe = FRAME_DEACTIVATE_FIRST;
		}

		if ((FRAME_DEACTIVATE_LAST - FRAME_DEACTIVATE_FIRST) < 4)
		{
			ent->client->anim_priority = ANIM_REVERSE;
			if(ent->client->ps.pmove.pm_flags & PMF_DUCKED)
			{
				ent->s.frame = FRAME_crpain4+1;
				ent->client->anim_end = FRAME_crpain1;
			}
			else
			{
				ent->s.frame = FRAME_pain304+1;
				ent->client->anim_end = FRAME_pain301;
				
			}
		}
		return;
	}

	if (ent->client->weaponstate == WEAPON_READY)
	{
		if ( ((ent->client->latched_buttons|ent->client->buttons) & BUTTON_ATTACK) )
		{
			ent->client->latched_buttons &= ~BUTTON_ATTACK;
			if ((!ent->client->ammo_index) || 
				( ent->client->pers.inventory[ent->client->ammo_index] >= ent->client->pers.weapon->quantity))
			{
				ent->client->ps.gunframe = FRAME_FIRE_FIRST;
				ent->client->weaponstate = WEAPON_FIRING;

				// start the animation
				ent->client->anim_priority = ANIM_ATTACK;
				if (ent->client->ps.pmove.pm_flags & PMF_DUCKED)
				{
					ent->s.frame = FRAME_crattak1-1;
					ent->client->anim_end = FRAME_crattak9;
				}
				else
				{
					ent->s.frame = FRAME_attack1-1;
					ent->client->anim_end = FRAME_attack8;
				}
			}
			else
			{
				if (level.time >= ent->pain_debounce_time)
				{
					gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/noammo.wav"), 1, ATTN_NORM, 0);
					ent->pain_debounce_time = level.time + 1;
				}
				NoAmmoWeaponChange (ent);
			}
		}
		else
		{
			if (ent->client->ps.gunframe == FRAME_IDLE_LAST)
			{
				ent->client->ps.gunframe = FRAME_IDLE_FIRST;
				return;
			}

			if (pause_frames)
			{
				for (n = 0; pause_frames[n]; n++)
				{
					if (ent->client->ps.gunframe == pause_frames[n])
					{
						if (rand()&15)
							return;
					}
				}
			}

			ent->client->ps.gunframe++;
			return;
		}
	}

	if (ent->client->weaponstate == WEAPON_FIRING)
	{
		for (n = 0; fire_frames[n]; n++)
		{
			if (ent->client->ps.gunframe == fire_frames[n])
			{
				if (ent->client->quad_framenum > level.framenum)
					gi.sound(ent, CHAN_ITEM, gi.soundindex("items/damage3.wav"), 1, ATTN_NORM, 0);

				ent->client->isfiring = 1; // We are firing this frame
				fire (ent);
				break;
			}
		}

		if (!fire_frames[n]) {
			ent->client->ps.gunframe++;
			if (ent->client->newweapon && fastswitch->value) {
				ent->client->weapon_sound = 0;
				ChangeWeapon(ent);
				return;
			}
		} else {
			if (ent->client->newweapon && fastswitch->value) {
				ent->client->weapon_sound = 0;
				ChangeWeapon(ent);
				return;
			}
		}

		if (ent->client->ps.gunframe == FRAME_IDLE_FIRST+1)
			ent->client->weaponstate = WEAPON_READY;

		//bat - Change the weapon right away!
		if ((ent->client->ammo_index) && 
			( ent->client->pers.inventory[ent->client->ammo_index] < ent->client->pers.weapon->quantity))
		{
			if (level.time >= ent->pain_debounce_time)
			{
				gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/noammo.wav"), 1, ATTN_NORM, 0);
				ent->pain_debounce_time = level.time + 1;
			}
			NoAmmoWeaponChange (ent);
		}

	}
}


/*
======================================================================

GRENADE

======================================================================
*/

#define GRENADE_TIMER		(SG_HOST_HAND_GRENADE_COOK_MS / 1000.0f)
#define GRENADE_MINSPEED	((float)SG_HOST_HAND_GRENADE_MIN_SPEED)
#define GRENADE_MAXSPEED	((float)SG_HOST_HAND_GRENADE_MAX_SPEED)

void weapon_grenade_fire (edict_t *ent, qboolean held)
{
	vec3_t	offset;
	vec3_t	forward, right;
	vec3_t	start;
	int		damage = SG_HOST_HAND_GRENADE_DAMAGE;
	float	timer;
	int		speed;
	float	radius;

	radius = damage+40;
	if (is_quad)
		damage *= SG_HOST_DAMAGE_QUAD_SCALE;

	VectorSet(offset, 8, 8, ent->viewheight-8);
	AngleVectors (ent->client->v_angle, forward, right, NULL);
	P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, start);

	timer = ent->client->grenade_time - level.time;
	speed = GRENADE_MINSPEED + (GRENADE_TIMER - timer) * ((GRENADE_MAXSPEED - GRENADE_MINSPEED) / GRENADE_TIMER);
	fire_grenade2 (ent, start, forward, damage, speed, timer, radius, held);

	if (! ( (int)dmflags->value & DF_INFINITE_AMMO ) )
		ent->client->pers.inventory[ent->client->ammo_index]--;

	ent->client->grenade_time = level.time + 1.0;

	if(ent->deadflag || ent->s.modelindex != 255) // VWep animations screw up corpses
	{
		return;
	}

	if (ent->health <= 0)
		return;

	if (ent->client->ps.pmove.pm_flags & PMF_DUCKED)
	{
		ent->client->anim_priority = ANIM_ATTACK;
		ent->s.frame = FRAME_crattak1-1;
		ent->client->anim_end = FRAME_crattak3;
	}
	else
	{
		ent->client->anim_priority = ANIM_REVERSE;
		ent->s.frame = FRAME_wave08;
		ent->client->anim_end = FRAME_wave01;
	}
}

void Weapon_Grenade (edict_t *ent)
{
	if ((ent->client->newweapon) && (ent->client->weaponstate == WEAPON_READY))
	{
		ChangeWeapon (ent);
		return;
	}

	if (ent->client->weaponstate == WEAPON_ACTIVATING)
	{
		ent->client->weaponstate = WEAPON_READY;
		ent->client->ps.gunframe = 16;
		return;
	}

	if (ent->client->weaponstate == WEAPON_READY)
	{
		if ( ((ent->client->latched_buttons|ent->client->buttons) & BUTTON_ATTACK) )
		{
			ent->client->latched_buttons &= ~BUTTON_ATTACK;
			if (ent->client->pers.inventory[ent->client->ammo_index])
			{
				ent->client->ps.gunframe = 1;
				ent->client->weaponstate = WEAPON_FIRING;
				ent->client->grenade_time = 0;
			}
			else
			{
				if (level.time >= ent->pain_debounce_time)
				{
					gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/noammo.wav"), 1, ATTN_NORM, 0);
					ent->pain_debounce_time = level.time + 1;
				}
				NoAmmoWeaponChange (ent);
			}
			return;
		}

		if ((ent->client->ps.gunframe == 29) || (ent->client->ps.gunframe == 34) || (ent->client->ps.gunframe == 39) || (ent->client->ps.gunframe == 48))
		{
			if (rand()&15)
				return;
		}

		if (++ent->client->ps.gunframe > 48)
			ent->client->ps.gunframe = 16;
		return;
	}

	if (ent->client->weaponstate == WEAPON_FIRING)
	{
		if (ent->client->ps.gunframe == 5)
			gi.sound(ent, CHAN_WEAPON, gi.soundindex("weapons/hgrena1b.wav"), 1, ATTN_NORM, 0);

		if (ent->client->ps.gunframe == 11)
		{
			if (!ent->client->grenade_time)
			{
				ent->client->grenade_time = level.time + GRENADE_TIMER + 0.2;
				ent->client->weapon_sound = gi.soundindex("weapons/hgrenc1b.wav");
			}

			// they waited too long, detonate it in their hand
			if (!ent->client->grenade_blew_up && level.time >= ent->client->grenade_time)
			{
				ent->client->weapon_sound = 0;
				weapon_grenade_fire (ent, true);
				ent->client->grenade_blew_up = true;
			}

			if (ent->client->buttons & BUTTON_ATTACK)
				return;

			if (ent->client->grenade_blew_up)
			{
				if (level.time >= ent->client->grenade_time)
				{
					ent->client->ps.gunframe = 15;
					ent->client->grenade_blew_up = false;
				}
				else
				{
					return;
				}
			}
		}

		if (ent->client->ps.gunframe == 12)
		{
			ent->client->weapon_sound = 0;
			weapon_grenade_fire (ent, false);
			ent->client->grenade_time = 0;
		}

		if ((ent->client->ps.gunframe == 15) && (level.time < ent->client->grenade_time))
			return;

		ent->client->ps.gunframe++;

		if (ent->client->ps.gunframe == 16)
		{
			ent->client->grenade_time = 0;
			ent->client->weaponstate = WEAPON_READY;
		}
	}
}

/*
======================================================================

GRENADE LAUNCHER

======================================================================
*/

void weapon_grenadelauncher_fire (edict_t *ent)
{
	vec3_t	offset;
	vec3_t	forward, right;
	vec3_t	start;
	int		damage = SG_HOST_GRENADE_DAMAGE;
	float	radius;

	radius = damage + SG_HOST_GRENADE_RADIUS_BONUS;
	if (is_quad)
		damage *= SG_HOST_DAMAGE_QUAD_SCALE;

#ifdef WEAP_BALANCE_OK	

	if ((int)ctfflags->value & CTF_WEAP_BALANCE)
	{
		radius *= SG_HOST_GRENADE_BALANCED_RADIUS_SCALE;
		damage -= SG_HOST_GRENADE_BALANCED_DAMAGE_DELTA;
	}
#endif


	VectorSet(offset, 8, 8, ent->viewheight-8);
	AngleVectors (ent->client->v_angle, forward, right, NULL);
	P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, start);

	VectorScale (forward, -2, ent->client->kick_origin);
	ent->client->kick_angles[0] = -1;

	fire_grenade (ent, start, forward, damage, SG_HOST_GRENADE_SPEED,
		SG_HOST_GRENADE_FUSE_SECONDS, radius);

	gi.WriteByte (svc_muzzleflash);
	gi.WriteShort (ent-g_edicts);
	gi.WriteByte (MZ_GRENADE | is_silenced);
	gi.multicast (ent->s.origin, MULTICAST_PVS);

	ent->client->ps.gunframe++;

	PlayerNoise(ent, start, PNOISE_WEAPON);

	if (! ( (int)dmflags->value & DF_INFINITE_AMMO ) )
		ent->client->pers.inventory[ent->client->ammo_index]--;
}

void Weapon_GrenadeLauncher (edict_t *ent)
{
	static int	pause_frames[]	= {34, 51, 59, 0};
	static int	fire_frames[]	= {6, 0};

	Weapon_Generic (ent, 5, 16, 59, 64, pause_frames, fire_frames, weapon_grenadelauncher_fire);
}

/*
======================================================================

ROCKET

======================================================================
*/

void Weapon_RocketLauncher_Fire (edict_t *ent)
{
	vec3_t	offset, start;
	vec3_t	forward, right;
	int		damage;
	float	damage_radius;
	int		radius_damage;

	damage = SG_HOST_ROCKET_DAMAGE_BASE +
		(int)(random() * SG_HOST_ROCKET_DAMAGE_RANDOM_SPAN);

#ifdef WEAP_BALANCE_OK
	if ((int)ctfflags->value & CTF_WEAP_BALANCE)
	{
		radius_damage = SG_HOST_ROCKET_BALANCED_SPLASH_DAMAGE;
		damage_radius = SG_HOST_ROCKET_BALANCED_SPLASH_RADIUS;
	}
	else
	{
		radius_damage = SG_RUNE_PROOF_ROCKETJUMP_RADIUS_DAMAGE;
		damage_radius = SG_RUNE_PROOF_ROCKETJUMP_DAMAGE_RADIUS;
	}
#else
	radius_damage = SG_RUNE_PROOF_ROCKETJUMP_RADIUS_DAMAGE;
	damage_radius = SG_RUNE_PROOF_ROCKETJUMP_DAMAGE_RADIUS;
#endif	
	
	if (is_quad)
	{
		damage *= SG_HOST_DAMAGE_QUAD_SCALE;
		radius_damage *= SG_HOST_DAMAGE_QUAD_SCALE;
	}

	AngleVectors (ent->client->v_angle, forward, right, NULL);

	VectorScale (forward, -2, ent->client->kick_origin);
	ent->client->kick_angles[0] = -1;

	VectorSet(offset, 8, 8, ent->viewheight-8);
	P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, start);

#ifdef WEAP_BALANCE_OK	
	if ((int)ctfflags->value & CTF_WEAP_BALANCE)
		fire_rocket (ent, start, forward, damage,
			SG_HOST_ROCKET_BALANCED_SPEED, damage_radius, radius_damage);
	else
		fire_rocket (ent, start, forward, damage,
			SG_RUNE_PROOF_ROCKETJUMP_ROCKET_SPEED, damage_radius,
			radius_damage); //SURT
#else
	fire_rocket (ent, start, forward, damage,
		SG_RUNE_PROOF_ROCKETJUMP_ROCKET_SPEED, damage_radius,
		radius_damage); //SURT
#endif

	// send muzzle flash
	gi.WriteByte (svc_muzzleflash);
	gi.WriteShort (ent-g_edicts);
	gi.WriteByte (MZ_ROCKET | is_silenced);
	gi.multicast (ent->s.origin, MULTICAST_PVS);

	ent->client->ps.gunframe++;

	PlayerNoise(ent, start, PNOISE_WEAPON);

	if (! ( (int)dmflags->value & DF_INFINITE_AMMO ) )
		ent->client->pers.inventory[ent->client->ammo_index]--;
}

void Weapon_RocketLauncher (edict_t *ent)
{
	static int	pause_frames[]	= {25, 33, 42, 50, 0};
	static int	fire_frames[]	= {5, 0};

	Weapon_Generic (ent, 4, 12, 50, 54, pause_frames, fire_frames, Weapon_RocketLauncher_Fire);
}


/*
======================================================================

BLASTER / HYPERBLASTER

======================================================================
*/

void Blaster_Fire (edict_t *ent, vec3_t g_offset, int damage, qboolean hyper, int effect)
{
	vec3_t	forward, right;
	vec3_t	start;
	vec3_t	offset;

	if (is_quad)
		damage *= SG_HOST_DAMAGE_QUAD_SCALE;
	AngleVectors (ent->client->v_angle, forward, right, NULL);
	VectorSet(offset, 24, 8, ent->viewheight-8);
	VectorAdd (offset, g_offset, offset);
	P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, start);

	VectorScale (forward, -2, ent->client->kick_origin);
	ent->client->kick_angles[0] = -1;

	fire_blaster (ent, start, forward, damage, SG_HOST_BLASTER_SPEED,
		effect, hyper);

	// send muzzle flash
	gi.WriteByte (svc_muzzleflash);
	gi.WriteShort (ent-g_edicts);
	if (hyper)
		gi.WriteByte (MZ_HYPERBLASTER | is_silenced);
	else
		gi.WriteByte (MZ_BLASTER | is_silenced);
	gi.multicast (ent->s.origin, MULTICAST_PVS);

	PlayerNoise(ent, start, PNOISE_WEAPON);
}


void Weapon_Blaster_Fire (edict_t *ent)
{
	int		damage;

	if (deathmatch->value)
		damage = SG_HOST_BLASTER_DM_DAMAGE;
	else
		damage = SG_HOST_BLASTER_NON_DM_DAMAGE;
	Blaster_Fire (ent, vec3_origin, damage, false, EF_BLASTER);

#ifdef WEAP_BALANCE_OK	
	if ((int)ctfflags->value & CTF_WEAP_BALANCE)
	{
		if (ent->client->ps.gunframe == 5) // First frame
			ent->client->ps.gunframe+=2;
		else
		{
			ent->client->ps.gunframe++;
		}
	}
	else
	{
		ent->client->ps.gunframe++;
	}
#else
	ent->client->ps.gunframe++;
#endif

}

void Weapon_Blaster (edict_t *ent)
{
	static int	pause_frames[]	= {19, 32, 0};
	static int	fire_frames[]	= {5, 0};

	Weapon_Generic (ent, 4, 8, 52, 55, pause_frames, fire_frames, Weapon_Blaster_Fire);
}


void Weapon_HyperBlaster_Fire (edict_t *ent)
{
	float	rotation;
	vec3_t	offset;
	int		effect;
	int		damage;

	ent->client->weapon_sound = gi.soundindex("weapons/hyprbl1a.wav");

	if (!(ent->client->buttons & BUTTON_ATTACK))
	{
		ent->client->ps.gunframe++;
	}
	else
	{
		if (! ent->client->pers.inventory[ent->client->ammo_index] )
		{
			if (level.time >= ent->pain_debounce_time)
			{
				gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/noammo.wav"), 1, ATTN_NORM, 0);
				ent->pain_debounce_time = level.time + 1;
			}
			NoAmmoWeaponChange (ent);
		}
		else
		{
			rotation = (ent->client->ps.gunframe - 5.0) * 2.0 * M_PI / 6.0;
			offset[0] = -4 * sin(rotation);
			offset[1] = 0;
			offset[2] = 4 * cos(rotation);

			if ((ent->client->ps.gunframe == 6) || (ent->client->ps.gunframe == 9))
				effect = EF_HYPERBLASTER;
			else
				effect = 0;
			if (deathmatch->value)
				damage = SG_HOST_HYPERBLASTER_DM_DAMAGE;
			else
				damage = SG_HOST_HYPERBLASTER_NON_DM_DAMAGE;

#ifdef WEAP_BALANCE_OK	
			if ((int)ctfflags->value & CTF_WEAP_BALANCE) //surt, a little less damage
				damage = SG_HOST_HYPERBLASTER_BALANCED_DAMAGE;
#endif

			Blaster_Fire (ent, offset, damage, true, effect);
			if (! ( (int)dmflags->value & DF_INFINITE_AMMO ) )
				ent->client->pers.inventory[ent->client->ammo_index]--;

			ent->client->anim_priority = ANIM_ATTACK;
			if (ent->client->ps.pmove.pm_flags & PMF_DUCKED)
			{
				ent->s.frame = FRAME_crattak1 - 1;
				ent->client->anim_end = FRAME_crattak9;
			}
			else
			{
				ent->s.frame = FRAME_attack1 - 1;
				ent->client->anim_end = FRAME_attack8;
			}
		}

		ent->client->ps.gunframe++;
		if (ent->client->ps.gunframe == 12 && ent->client->pers.inventory[ent->client->ammo_index])
			ent->client->ps.gunframe = 6;
	}

	if (ent->client->ps.gunframe == 12)
	{
		gi.sound(ent, CHAN_AUTO, gi.soundindex("weapons/hyprbd1a.wav"), 1, ATTN_NORM, 0);
		ent->client->weapon_sound = 0;
	}

}

void Weapon_HyperBlaster (edict_t *ent)
{
	static int	pause_frames[]	= {0};
	static int	fire_frames[]	= {6, 7, 8, 9, 10, 11, 0};

	Weapon_Generic (ent, 5, 20, 49, 53, pause_frames, fire_frames, Weapon_HyperBlaster_Fire);
}

/*
======================================================================

MACHINEGUN / CHAINGUN

======================================================================
*/

void Machinegun_Fire (edict_t *ent)
{
	int	i;
	vec3_t		start;
	vec3_t		forward, right;
	vec3_t		angles;
	int			damage = SG_HOST_MACHINEGUN_DAMAGE;
	int			kick = 2;
	vec3_t		offset;

#ifdef WEAP_BALANCE_OK	
	if ((int)ctfflags->value & CTF_WEAP_BALANCE)
	{
		damage = SG_HOST_MACHINEGUN_BALANCED_DAMAGE;
		//we'll lose accuracy for our mod however (extra spread)
	}
#endif

	if (!(ent->client->buttons & BUTTON_ATTACK))
	{
		ent->client->machinegun_shots = 0;
		ent->client->ps.gunframe++;
		return;
	}

	if (ent->client->ps.gunframe == 5)
		ent->client->ps.gunframe = 4;
	else
		ent->client->ps.gunframe = 5;

	if (ent->client->pers.inventory[ent->client->ammo_index] < 1)
	{
		ent->client->ps.gunframe = 6;
		if (level.time >= ent->pain_debounce_time)
		{
			gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/noammo.wav"), 1, ATTN_NORM, 0);
			ent->pain_debounce_time = level.time + 1;
		}
		NoAmmoWeaponChange (ent);
		return;
	}

	if (is_quad)
	{
		damage *= SG_HOST_DAMAGE_QUAD_SCALE;
		kick *= 4;
	}

	for (i=1 ; i<3 ; i++)
	{
		ent->client->kick_origin[i] = crandom() * 0.35;
		ent->client->kick_angles[i] = crandom() * 0.7;
	}
	ent->client->kick_origin[0] = crandom() * 0.35;
	ent->client->kick_angles[0] = ent->client->machinegun_shots * -1.5;

	// raise the gun as it is firing
	if (!deathmatch->value)
	{
		ent->client->machinegun_shots++;
		if (ent->client->machinegun_shots > 9)
			ent->client->machinegun_shots = 9;
	}

	// get start / end positions
	VectorAdd (ent->client->v_angle, ent->client->kick_angles, angles);
	AngleVectors (angles, forward, right, NULL);
	VectorSet(offset, 0, 8, ent->viewheight-8);
	P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, start);

#ifdef WEAP_BALANCE_OK	
	if ((int)ctfflags->value & CTF_WEAP_BALANCE) //loss of accuracy
		fire_bullet (ent, start, forward, damage, kick,
			DEFAULT_BULLET_HSPREAD + SG_HOST_MACHINEGUN_BALANCED_SPREAD_DELTA,
			DEFAULT_BULLET_VSPREAD + SG_HOST_MACHINEGUN_BALANCED_SPREAD_DELTA,
			MOD_MACHINEGUN);
	else //id original code
		fire_bullet (ent, start, forward, damage, kick, DEFAULT_BULLET_HSPREAD, DEFAULT_BULLET_VSPREAD, MOD_MACHINEGUN);
#else
	fire_bullet (ent, start, forward, damage, kick, DEFAULT_BULLET_HSPREAD, DEFAULT_BULLET_VSPREAD, MOD_MACHINEGUN);
#endif

	gi.WriteByte (svc_muzzleflash);
	gi.WriteShort (ent-g_edicts);
	gi.WriteByte (MZ_MACHINEGUN | is_silenced);
	gi.multicast (ent->s.origin, MULTICAST_PVS);

	PlayerNoise(ent, start, PNOISE_WEAPON);

	if (! ( (int)dmflags->value & DF_INFINITE_AMMO ) )
		ent->client->pers.inventory[ent->client->ammo_index]--;

	ent->client->anim_priority = ANIM_ATTACK;
	if (ent->client->ps.pmove.pm_flags & PMF_DUCKED)
	{
		ent->s.frame = FRAME_crattak1 - (int) (random()+0.25);
		ent->client->anim_end = FRAME_crattak9;
	}
	else
	{
		ent->s.frame = FRAME_attack1 - (int) (random()+0.25);
		ent->client->anim_end = FRAME_attack8;
	}
}

void Weapon_Machinegun (edict_t *ent)
{
	static int	pause_frames[]	= {23, 45, 0};
	static int	fire_frames[]	= {4, 5, 0};

	Weapon_Generic (ent, 3, 5, 45, 49, pause_frames, fire_frames, Machinegun_Fire);
}

void Chaingun_Fire (edict_t *ent)
{
	int			i;
	int			shots;
	vec3_t		start;
	vec3_t		forward, right, up;
	float		r, u;
	vec3_t		offset;
	int			damage;
	int			kick = 2;

	if (deathmatch->value)
		damage = SG_HOST_CHAINGUN_DM_DAMAGE;
	else
		damage = SG_HOST_CHAINGUN_NON_DM_DAMAGE;

	if (ent->client->ps.gunframe == 5)
		gi.sound(ent, CHAN_AUTO, gi.soundindex("weapons/chngnu1a.wav"), 1, ATTN_IDLE, 0);

	if ((ent->client->ps.gunframe == 14) && !(ent->client->buttons & BUTTON_ATTACK))
	{
		ent->client->ps.gunframe = 32;
		ent->client->weapon_sound = 0;
		return;
	}
	else if ((ent->client->ps.gunframe == 21) && (ent->client->buttons & BUTTON_ATTACK)
		&& ent->client->pers.inventory[ent->client->ammo_index])
	{
		ent->client->ps.gunframe = 15;
	}
	else
	{
		ent->client->ps.gunframe++;
	}

	if (ent->client->ps.gunframe == 22)
	{
		ent->client->weapon_sound = 0;
		gi.sound(ent, CHAN_AUTO, gi.soundindex("weapons/chngnd1a.wav"), 1, ATTN_IDLE, 0);
	}
	else
	{
		ent->client->weapon_sound = gi.soundindex("weapons/chngnl1a.wav");
	}

	ent->client->anim_priority = ANIM_ATTACK;
	if (ent->client->ps.pmove.pm_flags & PMF_DUCKED)
	{
		ent->s.frame = FRAME_crattak1 - (ent->client->ps.gunframe & 1);
		ent->client->anim_end = FRAME_crattak9;
	}
	else
	{
		ent->s.frame = FRAME_attack1 - (ent->client->ps.gunframe & 1);
		ent->client->anim_end = FRAME_attack8;
	}

	if (ent->client->ps.gunframe <= 9)
		shots = 1;
	else if (ent->client->ps.gunframe <= 14)
	{
		if (ent->client->buttons & BUTTON_ATTACK)
			shots = 2;
		else
			shots = 1;
	}
	else
		shots = 3;

	if (ent->client->pers.inventory[ent->client->ammo_index] < shots)
		shots = ent->client->pers.inventory[ent->client->ammo_index];

	if (!shots)
	{
		if (level.time >= ent->pain_debounce_time)
		{
			gi.sound(ent, CHAN_VOICE, gi.soundindex("weapons/noammo.wav"), 1, ATTN_NORM, 0);
			ent->pain_debounce_time = level.time + 1;
		}
		NoAmmoWeaponChange (ent);
		return;
	}

	if (is_quad)
	{
		damage *= SG_HOST_DAMAGE_QUAD_SCALE;
		kick *= 4;
	}

	for (i=0 ; i<3 ; i++)
	{
		ent->client->kick_origin[i] = crandom() * 0.35;
		ent->client->kick_angles[i] = crandom() * 0.7;
	}

	for (i=0 ; i<shots ; i++)
	{
		// get start / end positions
		AngleVectors (ent->client->v_angle, forward, right, up);
		r = 7 + crandom()*4;
		u = crandom()*4;
		VectorSet(offset, 0, r, u + ent->viewheight-8);
		P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, start);

		fire_bullet (ent, start, forward, damage, kick, DEFAULT_BULLET_HSPREAD, DEFAULT_BULLET_VSPREAD, MOD_CHAINGUN);
	}

	// send muzzle flash
	gi.WriteByte (svc_muzzleflash);
	gi.WriteShort (ent-g_edicts);
	gi.WriteByte ((MZ_CHAINGUN1 + shots - 1) | is_silenced);
	gi.multicast (ent->s.origin, MULTICAST_PVS);

	PlayerNoise(ent, start, PNOISE_WEAPON);

	if (! ( (int)dmflags->value & DF_INFINITE_AMMO ) )
		ent->client->pers.inventory[ent->client->ammo_index] -= shots;
}


void Weapon_Chaingun (edict_t *ent)
{
	static int	pause_frames[]	= {38, 43, 51, 61, 0};
	static int	fire_frames[]	= {5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 0};

	Weapon_Generic (ent, 4, 31, 61, 64, pause_frames, fire_frames, Chaingun_Fire);
}


/*
======================================================================

SHOTGUN / SUPERSHOTGUN

======================================================================
*/

void weapon_shotgun_fire (edict_t *ent)
{
	vec3_t		start;
	vec3_t		forward, right;
	vec3_t		offset;
	int			damage = SG_HOST_SHOTGUN_DAMAGE;
	int			kick = 8;
	int			count = 0; //surt weapon balance

	if (ent->client->ps.gunframe == 9)
	{
		ent->client->ps.gunframe++;
		return;
	}

	AngleVectors (ent->client->v_angle, forward, right, NULL);

	VectorScale (forward, -2, ent->client->kick_origin);
	ent->client->kick_angles[0] = -2;

	VectorSet(offset, 0, 8,  ent->viewheight-8);
	P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, start);

	if (is_quad)
	{
		damage *= SG_HOST_DAMAGE_QUAD_SCALE;
		kick *= 4;
	}

#ifdef WEAP_BALANCE_OK	
	if ((int)ctfflags->value & CTF_WEAP_BALANCE) //surt
	{
		damage += SG_HOST_SHOTGUN_BALANCED_DAMAGE_DELTA;
		count = SG_HOST_SHOTGUN_BALANCED_PELLET_DELTA;
	}
#endif

	if (deathmatch->value)
		fire_shotgun (ent, start, forward, damage, kick,
			SG_HOST_SHOTGUN_LIVE_HORIZONTAL_SPREAD,
			SG_HOST_SHOTGUN_LIVE_VERTICAL_SPREAD,
			DEFAULT_DEATHMATCH_SHOTGUN_COUNT + count, MOD_SHOTGUN);
	else
		fire_shotgun (ent, start, forward, damage, kick,
			SG_HOST_SHOTGUN_LIVE_HORIZONTAL_SPREAD,
			SG_HOST_SHOTGUN_LIVE_VERTICAL_SPREAD,
			DEFAULT_SHOTGUN_COUNT + count, MOD_SHOTGUN);

	// send muzzle flash
	gi.WriteByte (svc_muzzleflash);
	gi.WriteShort (ent-g_edicts);
	gi.WriteByte (MZ_SHOTGUN | is_silenced);
	gi.multicast (ent->s.origin, MULTICAST_PVS);

	ent->client->ps.gunframe++;
	PlayerNoise(ent, start, PNOISE_WEAPON);

	if (! ( (int)dmflags->value & DF_INFINITE_AMMO ) )
		ent->client->pers.inventory[ent->client->ammo_index]--;
}

void fire_fieldgun (edict_t *self, vec3_t start, vec3_t aimdir, int damage, int kick, int hspread, int vspread, int count)
{
	stats_add(self, STATS_SHOTS, count);	// BUZZKILL - one per pellet

	int		i;
	trace_t	tr;
	vec3_t	end, from;
	vec3_t	mins, maxs;
	float		*v;
	edict_t	*target;

	if (!self)
		return;
	
	v = tv(-32, -32, -32);
	VectorCopy (v, mins);
	v = tv(32,32,32);
	VectorCopy (v, maxs);

	if ((level.framenum % 3) && self->client->hooklength)
	{
		return;
	}

	//for (i = 0; i < count; i++)
	//	fire_lead (self, start, aimdir, damage, kick, TE_SHOTGUN, hspread, vspread);

	//fire_shotgun (self, start, aimdir, damage, kick, hspread, vspread, count);
	// send gun puff / flash
	if (self->client->hooklength < 640)
	{
		if (self->client->hooklength < 96)
			self->client->hooklength = 96;
		else
			self->client->hooklength += 32;
	}

	VectorMA (start, self->client->hooklength, aimdir, end);
	VectorCopy (start, from);

	target = self;
	while (target)
	{
		tr = gi.trace (from, mins, maxs, end, self, MASK_SHOT);

		if ((tr.ent->svflags & SVF_MONSTER) || (tr.ent->client))
			target = tr.ent;
		else
			target = NULL;

		if (target && (target != self) && (target->takedamage))
			T_Damage (target, self, self, aimdir, tr.endpos, tr.plane.normal, 2, 5, 0, MOD_SHOTGUN);

		VectorCopy (tr.endpos, from);
	}

	for (i=96; i < self->client->hooklength; i+=32)
	{
		VectorMA (start, i, aimdir, end);
		
		tr = gi.trace (start, NULL, NULL, end, self, MASK_SHOT);
		//T_RadiusDamage(self, self, 10, self, 32);

		gi.WriteByte (svc_temp_entity);
		gi.WriteByte (TE_ROCKET_EXPLOSION);
		gi.WritePosition (tr.endpos);
		gi.multicast (self->s.origin, MULTICAST_PHS);
	}
}


void weapon_fieldgun_fire (edict_t *ent)
{
	vec3_t		start;
	vec3_t		forward, right;
	vec3_t		offset;
	int			damage = 4;
	int			kick = 8;

	AngleVectors (ent->client->v_angle, forward, right, NULL);

	VectorScale (forward, -2, ent->client->kick_origin);
	ent->client->kick_angles[0] = -2;

	VectorSet(offset, 0, 8,  ent->viewheight-8);
	P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, start);

	if (is_quad)
	{
		damage *= SG_HOST_DAMAGE_QUAD_SCALE;
		kick *= 4;
	}

#ifdef WEAP_BALANCE_OK	
	if ((int)ctfflags->value & CTF_WEAP_BALANCE) //surt
		damage+=1; //slightly more damage, except with quad
#endif

	if (ent->client->buttons & BUTTON_ATTACK)
	{
		if (deathmatch->value)
			fire_fieldgun (ent, start, forward, damage, kick, 500, 500, DEFAULT_DEATHMATCH_SHOTGUN_COUNT);
		else
			fire_fieldgun (ent, start, forward, damage, kick, 500, 500, DEFAULT_SHOTGUN_COUNT);

		// send muzzle flash
		gi.WriteByte (svc_muzzleflash);
		gi.WriteShort (ent-g_edicts);
		gi.WriteByte (MZ_SHOTGUN | is_silenced);
		gi.multicast (ent->s.origin, MULTICAST_PVS);

		//ent->client->ps.gunframe++;
		//PlayerNoise(ent, start, PNOISE_WEAPON);

		//ent->client->pers.inventory[ent->client->ammo_index] -= ent->client->pers.weapon->quantity;
	}
	else
	{
		ent->client->ps.gunframe++;
		ent->client->hooklength = 0;
	}
}


void Weapon_Shotgun (edict_t *ent)
{
	static int	pause_frames[]	= {22, 28, 34, 0};
	static int	fire_frames[]	= {8, 9, 0};

	Weapon_Generic (ent, 7, 18, 36, 39, pause_frames, fire_frames, weapon_shotgun_fire);
}


void weapon_supershotgun_fire (edict_t *ent)
{
	vec3_t		start;
	vec3_t		forward, right;
	vec3_t		offset;
	vec3_t		v;
	int			damage = SG_HOST_SUPER_SHOTGUN_DAMAGE;
	int			kick = 12;
	int			count = 0; //for damage balance

	AngleVectors (ent->client->v_angle, forward, right, NULL);

	VectorScale (forward, -2, ent->client->kick_origin);
	ent->client->kick_angles[0] = -2;

	VectorSet(offset, 0, 8,  ent->viewheight-8);
	P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, start);

	if (is_quad)
	{
		damage *= SG_HOST_DAMAGE_QUAD_SCALE;
		kick *= 4;
	}

#ifdef WEAP_BALANCE_OK	
	if ((int)ctfflags->value & CTF_WEAP_BALANCE) //surt
	{
		count = SG_HOST_SUPER_SHOTGUN_BALANCED_PELLET_DELTA;
		damage -= SG_HOST_SUPER_SHOTGUN_BALANCED_DAMAGE_DELTA;
	}
#endif

	v[PITCH] = ent->client->v_angle[PITCH];
	v[YAW] = ent->client->v_angle[YAW] -
		SG_HOST_SUPER_SHOTGUN_YAW_DEGREES;
	v[ROLL]  = ent->client->v_angle[ROLL];
	AngleVectors (v, forward, NULL, NULL);
	fire_shotgun (ent, start, forward, damage, kick, DEFAULT_SHOTGUN_HSPREAD, DEFAULT_SHOTGUN_VSPREAD, DEFAULT_SSHOTGUN_COUNT/2 + count/2, MOD_SSHOTGUN); //surt, balance
	v[YAW] = ent->client->v_angle[YAW] +
		SG_HOST_SUPER_SHOTGUN_YAW_DEGREES;
	AngleVectors (v, forward, NULL, NULL);
	fire_shotgun (ent, start, forward, damage, kick, DEFAULT_SHOTGUN_HSPREAD, DEFAULT_SHOTGUN_VSPREAD, DEFAULT_SSHOTGUN_COUNT/2 + count/2, MOD_SSHOTGUN); //surt balance

	// send muzzle flash
	gi.WriteByte (svc_muzzleflash);
	gi.WriteShort (ent-g_edicts);
	gi.WriteByte (MZ_SSHOTGUN | is_silenced);
	gi.multicast (ent->s.origin, MULTICAST_PVS);

	ent->client->ps.gunframe++;
	PlayerNoise(ent, start, PNOISE_WEAPON);

	if (! ( (int)dmflags->value & DF_INFINITE_AMMO ) )
		ent->client->pers.inventory[ent->client->ammo_index] -= 2;
}

void Weapon_SuperShotgun (edict_t *ent)
{
	static int	pause_frames[]	= {29, 42, 57, 0};
	static int	fire_frames[]	= {7, 0};

	Weapon_Generic (ent, 6, 17, 57, 61, pause_frames, fire_frames, weapon_supershotgun_fire);
}



/*
======================================================================

RAILGUN

======================================================================
*/

void weapon_railgun_fire (edict_t *ent)
{
	vec3_t		start;
	vec3_t		forward, right;
	vec3_t		offset;
	int			damage;
	int			kick;

	if(matchstate == MATCH_RAILGUN_INPLAY)
	{
		damage = SG_HOST_RAILGUN_MATCH_DAMAGE;
	 	kick = 5000;
	}
	else if (deathmatch->value)
	{	
#ifdef WEAP_BALANCE_OK	
		// normal damage is too extreme in dm
		if ((int)ctfflags->value & CTF_WEAP_BALANCE)
		{
			damage = SG_HOST_RAILGUN_BALANCED_DAMAGE;
			kick = 125; //make it a little less irritating to use
		}
		else
		{
			damage = SG_HOST_RAILGUN_DM_DAMAGE;
			kick = 200;
		}
#else
		damage = 100;
		kick = 200;
#endif

	}
	else
	{
		damage = SG_HOST_RAILGUN_NON_DM_DAMAGE;
		kick = 250;
	}

	if (is_quad)
	{
		damage *= SG_HOST_DAMAGE_QUAD_SCALE;
		kick *= 4;
	}

	AngleVectors (ent->client->v_angle, forward, right, NULL);

	VectorScale (forward, -3, ent->client->kick_origin);
	ent->client->kick_angles[0] = -3;

	VectorSet(offset, 0, 7,  ent->viewheight-8);
	P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, start);
	fire_rail (ent, start, forward, damage, kick);

	// send muzzle flash
	gi.WriteByte (svc_muzzleflash);
	gi.WriteShort (ent-g_edicts);
	gi.WriteByte (MZ_RAILGUN | is_silenced);
	gi.multicast (ent->s.origin, MULTICAST_PVS);

	// BUZZKILL - SLIPGATE: the rail rhythm. The slug is away and the flash
	// and the trail are on the wire, which is the whole of what a player in
	// the room perceives; every test about who could have perceived it, and
	// what a bot is allowed to do with it, is on the slipgate side
	SG_NoteRailShot(ent);

	ent->client->ps.gunframe++;
	PlayerNoise(ent, start, PNOISE_WEAPON);

	if (! ( (int)dmflags->value & DF_INFINITE_AMMO ) )
		ent->client->pers.inventory[ent->client->ammo_index]--;
}


void Weapon_Railgun (edict_t *ent)
{
	static int	pause_frames[]	= {56, 0};
	static int	fire_frames[]	= {4, 0};

	Weapon_Generic (ent, 3, 18, 56, 61, pause_frames, fire_frames, weapon_railgun_fire);
}


/*
======================================================================

BFG10K

======================================================================
*/

void weapon_bfg_fire (edict_t *ent)
{
	vec3_t	offset, start;
	vec3_t	forward, right;
	int		damage;
	float	damage_radius = SG_HOST_BFG_EFFECT_RADIUS;
	
#ifdef WEAP_BALANCE_OK	
	if ((int)ctfflags->value & CTF_WEAP_BALANCE)
		damage_radius = SG_HOST_BFG_BALANCED_EFFECT_RADIUS;
#endif

	if (deathmatch->value)
		damage = SG_HOST_BFG_DAMAGE;
	else
		damage = SG_HOST_BFG_NON_DM_DAMAGE;

#ifdef WEAP_BALANCE_OK	
	if ((int)ctfflags->value & CTF_WEAP_BALANCE)
		damage = SG_HOST_BFG_BALANCED_DAMAGE;
#endif

	if (ent->client->ps.gunframe == 9)
	{
		// send muzzle flash
		gi.WriteByte (svc_muzzleflash);
		gi.WriteShort (ent-g_edicts);
		gi.WriteByte (MZ_BFG | is_silenced);
		gi.multicast (ent->s.origin, MULTICAST_PVS);

		ent->client->ps.gunframe++;

		PlayerNoise(ent, start, PNOISE_WEAPON);
		return;
	}

	// cells can go down during windup (from power armor hits), so
	// check again and abort firing if we don't have enough now
	if (ent->client->pers.inventory[ent->client->ammo_index] <
	    SG_HOST_BFG_AMMO_COST)
	{
		ent->client->ps.gunframe++;
		return;
	}

	if (is_quad)
		damage *= SG_HOST_DAMAGE_QUAD_SCALE;

	AngleVectors (ent->client->v_angle, forward, right, NULL);

	VectorScale (forward, -2, ent->client->kick_origin);

	// make a big pitch kick with an inverse fall
	ent->client->v_dmg_pitch = -40;
	ent->client->v_dmg_roll = crandom()*8;
	ent->client->v_dmg_time = level.time + DAMAGE_TIME;

	VectorSet(offset, 8, 8, ent->viewheight-8);
	P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, start);

#ifdef WEAP_BALANCE_OK	
	if ((int)ctfflags->value & CTF_WEAP_BALANCE)
		fire_bfg (ent, start, forward, damage,
			SG_HOST_BFG_BALANCED_SPEED, damage_radius);
	else
		fire_bfg (ent, start, forward, damage, SG_HOST_BFG_SPEED,
			damage_radius);
#else
	fire_bfg (ent, start, forward, damage, SG_HOST_BFG_SPEED,
		damage_radius);
#endif

	ent->client->ps.gunframe++;

	PlayerNoise(ent, start, PNOISE_WEAPON);

	if (! ( (int)dmflags->value & DF_INFINITE_AMMO ) )
		ent->client->pers.inventory[ent->client->ammo_index] -=
			SG_HOST_BFG_AMMO_COST;
}

void Weapon_BFG (edict_t *ent)
{
	static int	pause_frames[]	= {39, 45, 50, 55, 0};
	static int	fire_frames[]	= {
		SG_HOST_BFG_FLASH_FRAME, SG_HOST_BFG_FIRE_FRAME, 0
	};

	Weapon_Generic (ent, 8, 32, 55, 58, pause_frames, fire_frames, weapon_bfg_fire);
}


//======================================================================

// CTF CODE -- LM_JORM



/*
=================
hook_touch

Touch function for the grappling hook
=================
*/
static void SG_BotHookTouch(edict_t *self, edict_t *other,
	cplane_t *plane, csurface_t *surf)
{
	vec3_t dest;
	sg_host_hook_step_t touch_step;
	sg_host_law_result_t touch_result;
	uint32_t target_index;

	if (!other)
		return;

	if (other == self->owner)
		return; //we hit ourselves, ignore us

	if (self->hook_target && self->hook_target != other) // Already have a target... ignore this new target
		return;

	if (other->s.number < 0 || other->s.number >= globals.num_edicts ||
		&g_edicts[other->s.number] != other)
	{
		ctf_hook_abort(self->owner);
		return;
	}
	target_index = (uint32_t)other->s.number;
	touch_result = SG_HostLawProductionHookTouch(
		(uint32_t)self->owner->s.number, (uint32_t)self->s.number,
		target_index, surf ? surf->flags : 0,
		&touch_step);
	if (touch_result.status != SG_HOST_LAW_OK || touch_step.aborted ||
		!touch_step.accepted)
	{
		if (self->owner->client && sg_cv.debug->value)
			gi.dprintf("HOOKABORT %s entity=%s\n",
				self->owner->client->pers.netname,
				other->classname ? other->classname : "?");
		ctf_hook_abort(self->owner);
		return;
	}
	if (SG_OwnsBot(self->owner) &&
	    SG_CompoundHookGameAttachWillApply(self, other, surf) ==
	        SG_COMPOUND_HOOK_GAME_EVENT_DENIED)
	{
		if (self->owner && self->owner->client &&
		    !self->owner->client->hook)
			self->owner->client->hook = self;
		ctf_hook_abort(self->owner);
		return;
	}

	VectorClear (self->velocity);

	if (self->owner->client)
	{
		//PlayerNoise(self->owner, self->s.origin, PNOISE_IMPACT);
		if (self->owner->client->hookstate == 1) // Have we just hit?
		{
//			gi.sound(self, CHAN_AUTO, gi.soundindex("weapons/grapple/ghit.wav"), 1, ATTN_NORM, 0);
			//gi.sound(self->owner, CHAN_AUTO, gi.soundindex("weapons/grapple/grpull.wav"), 1, ATTN_NORM, 0);
		}
		self->owner->client->hookstate = 2;
	}

	if (touch_step.damage != 0U)
	{
		if (self->hook_target == other) 
		{
			if (touch_step.target_kind == SG_HOST_HOOK_TARGET_PLAYER)
				gi.sound(self, CHAN_AUTO, gi.soundindex("weapons/grapple/gkilling.wav"), 1, ATTN_NORM, 0);
			T_Damage (other, self, self->owner, self->velocity, self->s.origin, plane->normal, (int)touch_step.damage, (int)touch_step.damage, DAMAGE_ENERGY, MOD_CTF_GRAPPLE);
			self->hook_lastframe = (int)touch_step.next_last_damage_frame;
		}
		else 
		{
			if (touch_step.target_kind == SG_HOST_HOOK_TARGET_PLAYER)
				gi.sound(self, CHAN_AUTO, gi.soundindex("weapons/grapple/ghit.wav"), 1, ATTN_NORM, 0);
			else
				gi.sound(self, CHAN_AUTO, gi.soundindex("weapons/grapple/ghitwall.wav"), 0.8f, ATTN_NORM, 0);
			
			// Bonus damage for first hit
			T_Damage (other, self, self->owner, self->velocity, self->s.origin, plane->normal, (int)touch_step.damage, (int)touch_step.damage, DAMAGE_ENERGY, MOD_CTF_GRAPPLE);
		}
	}
	
	if (other->deadflag)
	{
		ctf_hook_abort(self->owner);
		return;
	}
		
	if (!self->hook_target)
	{
		if (!touch_step.attached)
		{
			ctf_hook_abort(self->owner);
			return;
		}
		self->hook_target = other;
		VectorSubtract(self->s.origin, self->hook_target->absmin, dest);
		VectorCopy(dest, self->hook_offset);
		self->solid = SOLID_TRIGGER;
		gi.linkentity(self);
		if (SG_OwnsBot(self->owner))
		{
			sg_compound_hook_live_result_t compound_result =
			    SG_CompoundHookGameAttached(self);

			if (compound_result.outcome == SG_COMPOUND_HOOK_LIVE_RECOVERING ||
			    compound_result.outcome == SG_COMPOUND_HOOK_LIVE_REJECTED)
			{
				if (self->owner->client && !self->owner->client->hook)
					self->owner->client->hook = self;
				ctf_hook_abort(self->owner);
				return;
			}
		}
	}

	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_BLASTER);
	gi.WritePosition (self->s.origin);
	if (!plane)
		gi.WriteDir (vec3_origin);
	else
		gi.WriteDir (plane->normal);
	gi.multicast (self->s.origin, MULTICAST_PVS);	
}

/* Unmodified LMCTF hook collision path. Bot bolts use SG_BotHookTouch and
 * never enter this function. */
void hook_touch (edict_t *self, edict_t *other, cplane_t *plane, csurface_t *surf)
{
	vec3_t dest;

	if (!other)
		return;

	if (other == self->owner)
		return;

	if (self->hook_target && self->hook_target != other)
		return;

	if (other &&
		(strcmp(other->classname, "bodyque") != 0) &&
		(!ctf_validateplayer(other, CTF_TEAM_ANYTEAM)) &&
		(strcmp(other->classname, "worldspawn") != 0) &&
		(strncmp(other->classname, "func", 4) != 0) &&
		(strncmp(other->classname, "info_flag", 9) != 0)
		)
	{
		ctf_hook_abort(self->owner);
		return;
	}

	if ((surf && (surf->flags & SURF_SKY)) ||
		((other->client) && (self->owner->client->ctf.teamnum == other->client->ctf.teamnum)) ||
		other->deadflag)
	{
		ctf_hook_abort(self->owner);
		return;
	}

	VectorClear (self->velocity);

	if (self->owner->client)
	{
		self->owner->client->hookstate = 2;
	}

	if (! ((int)ctfflags->value & CTF_NO_GRAP_DAMAGE) || (!other->client))
	{
		if (self->hook_target == other)
		{
			if ( (level.framenum % 7) == 0 && (level.framenum != self->hook_lastframe) )
			{
				if (ctf_validateplayer(other,CTF_TEAM_ANYTEAM))
					gi.sound(self, CHAN_AUTO, gi.soundindex("weapons/grapple/gkilling.wav"), 1, ATTN_NORM, 0);
				T_Damage (other, self, self->owner, self->velocity, self->s.origin, plane->normal, SG_HOST_HOOK_ATTACHED_DAMAGE, SG_HOST_HOOK_ATTACHED_DAMAGE, DAMAGE_ENERGY, MOD_CTF_GRAPPLE);
				self->hook_lastframe = level.framenum;
			}
		}
		else
		{
			if (ctf_validateplayer(other,CTF_TEAM_ANYTEAM))
				gi.sound(self, CHAN_AUTO, gi.soundindex("weapons/grapple/ghit.wav"), 1, ATTN_NORM, 0);
			else
				gi.sound(self, CHAN_AUTO, gi.soundindex("weapons/grapple/ghitwall.wav"), 0.8f, ATTN_NORM, 0);
			T_Damage (other, self, self->owner, self->velocity, self->s.origin, plane->normal, SG_HOST_HOOK_INITIAL_DAMAGE, SG_HOST_HOOK_INITIAL_DAMAGE, DAMAGE_ENERGY, MOD_CTF_GRAPPLE);
		}
	}

	if (other->deadflag)
	{
		ctf_hook_abort(self->owner);
		return;
	}

	if (!self->hook_target)
	{
		self->hook_target = other;
		VectorSubtract(self->s.origin, self->hook_target->absmin, dest);
		VectorCopy(dest, self->hook_offset);
		self->solid = SOLID_TRIGGER;
		gi.linkentity(self);
		SG_HumanTraceHookAttach(self->owner, self, other);
	}

	gi.WriteByte (svc_temp_entity);
	gi.WriteByte (TE_BLASTER);
	gi.WritePosition (self->s.origin);
	if (!plane)
		gi.WriteDir (vec3_origin);
	else
		gi.WriteDir (plane->normal);
	gi.multicast (self->s.origin, MULTICAST_PVS);
}

void Grapple_Bolt_Think(edict_t *self)
{
	if (!self->hook_target && self->owner->client->hooklength > 126)
	{
		//in flight sound
		gi.sound(self, CHAN_AUTO, gi.soundindex("weapons/grapple/gflyair.wav"), 1, ATTN_NORM, 0);
		self->nextthink = level.time + 0.4;
		self->think = Grapple_Bolt_Think;
	}
	else if (self->owner->client->hooklength > 126)
	{
		//retracting sound
		gi.sound(self, CHAN_AUTO, gi.soundindex("weapons/grapple/gpulling.wav"), 1, ATTN_NORM, 0);
		self->nextthink = level.time + 0.8;
		self->think = Grapple_Bolt_Think;
	}
	else
	{
		//no sound
		self->nextthink = 0;
		self->think = NULL;
	}
	
}

void
hook_die(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point)
{
	ctf_hook_abort(self->owner);
}

static edict_t *LMCTF_FireHumanHook(edict_t *self, vec3_t start,
	vec3_t dir, int speed)
{
	edict_t *bolt;
	trace_t tr;

	VectorNormalize(dir);
	bolt = G_Spawn();
	VectorCopy(start, bolt->s.origin);
	VectorCopy(start, bolt->s.old_origin);
	vectoangles(dir, bolt->s.angles);
	bolt->s.angles[PITCH] += 90;
	VectorScale(dir, speed, bolt->velocity);
	bolt->movetype = MOVETYPE_FLYMISSILE;
	bolt->clipmask = MASK_SHOT;
	bolt->solid = SOLID_BBOX;
	VectorClear(bolt->mins);
	VectorClear(bolt->maxs);
	bolt->s.modelindex = gi.modelindex("models/objects/ghook/tris.md2");
	G_ProjectileOwnerSet(bolt, self);
	bolt->touch = hook_touch;
	bolt->die = hook_die;
	bolt->nextthink = level.time + 1;
	bolt->think = Grapple_Bolt_Think;
	bolt->dmg = 2;
	bolt->takedamage = DAMAGE_YES;
	bolt->health = SG_HOST_HOOK_HEALTH;
	gi.linkentity(bolt);
	SG_HumanTraceHookFire(self, bolt);
	gi.sound(self, CHAN_AUTO, gi.soundindex("weapons/grapple/grfire.wav"),
	    0.8f, ATTN_NORM, 0);
	tr = gi.trace(self->s.origin, NULL, NULL, bolt->s.origin, self,
	    MASK_SHOT);
	if (tr.fraction < 1.0)
	{
		VectorMA(bolt->s.origin, -10, dir, bolt->s.origin);
		/* Base LMCTF passed NULL here, but hook_touch dereferences the plane
		 * when this launch trace hits a damageable target. Supplying this
		 * trace's plane is limited to the immediate-obstruction branch;
		 * ordinary missile flight still uses the engine's touch callback. */
		bolt->touch(bolt, tr.ent, &tr.plane, NULL);
	}
	return bolt;
}

edict_t *fire_hook (edict_t *self, vec3_t start, vec3_t dir, int speed)
{
	edict_t	*bolt;
	sg_host_hook_step_t fire_step;
	sg_host_law_result_t fire_result;

	VectorNormalize (dir);

	bolt = G_Spawn();
	VectorCopy (start, bolt->s.origin);
	VectorCopy (start, bolt->s.old_origin);
	vectoangles (dir, bolt->s.angles);
	bolt->s.angles[PITCH] += 90;
	VectorScale (dir, speed, bolt->velocity);
	bolt->movetype = MOVETYPE_FLYMISSILE;
	bolt->clipmask = MASK_SHOT;
	bolt->solid = SOLID_BBOX;
	//bolt->s.effects |= effect;
	VectorClear (bolt->mins);
	VectorClear (bolt->maxs);
	bolt->s.modelindex = gi.modelindex ("models/objects/ghook/tris.md2");
//	bolt->s.sound = gi.soundindex ("weapons/grapple/grfire.wav");
	G_ProjectileOwnerSet(bolt, self);
	bolt->touch = SG_BotHookTouch;
	bolt->die = hook_die;
	bolt->nextthink = level.time + 1;
	bolt->think = Grapple_Bolt_Think;
	bolt->dmg = 2;
	bolt->takedamage = DAMAGE_YES;
	bolt->health = SG_HOST_HOOK_HEALTH;	 // after 59 damage, hook destoyed
	gi.linkentity (bolt);
	if (SG_OwnsBot(self))
	{
		sg_compound_hook_live_result_t compound_result;
		sg_compound_guard_result_t guard_result;
		sg_mover_subject_t subject;

		guard_result = SG_CompoundGuardGameHookLinked(self, bolt, &subject);
		if (guard_result != SG_COMPOUND_GUARD_OK)
		{
			G_FreeEdict(bolt);
			return NULL;
		}
		compound_result = SG_CompoundHookGameLinked(self, bolt, &subject);
		if (compound_result.outcome != SG_COMPOUND_HOOK_LIVE_IDLE &&
		    compound_result.outcome != SG_COMPOUND_HOOK_LIVE_RUNNING)
		{
			self->client->hook = bolt;
			ctf_hook_abort(self);
			return NULL;
		}
	}
	fire_result = SG_HostLawProductionHookFire((uint32_t)self->s.number,
		(uint32_t)bolt->s.number, &fire_step);
	if (fire_result.status != SG_HOST_LAW_OK || !fire_step.accepted ||
		fire_step.aborted)
	{
		self->client->hook = bolt;
		ctf_hook_abort(self);
		return NULL;
	}


	//surt the muzzle flash code also causes a shotgun noise!!!!

		// send muzzle flash
//	gi.WriteByte (svc_muzzleflash);
//	gi.WriteShort (self-g_edicts);
//	gi.WriteByte (MZ_SHOTGUN | is_silenced);
//	gi.multicast (self->s.origin, MULTICAST_PVS);

	//surt can that be avoided?

	gi.sound(self, CHAN_AUTO, gi.soundindex("weapons/grapple/grfire.wav"), 0.8f, ATTN_NORM, 0);
//	gi.dprintf("Played grapple sound.\n");

//	bolt->s.sound = gi.soundindex ("weapons/grapple/grfire.wav");
//surt the above works, but sounds bad
	/*
	if (self->client)
		check_dodge (self, bolt->s.origin, dir, speed);
	*/

	if (fire_step.collision_hit)
	{
		cplane_t plane;
		csurface_t surface;
		edict_t *target;

		if (fire_step.collision_instance_id >= (uint64_t)globals.num_edicts)
		{
			self->client->hook = bolt;
			ctf_hook_abort(self);
			return NULL;
		}
		target = &g_edicts[fire_step.collision_instance_id];
		memset(&plane, 0, sizeof(plane));
		VectorCopy(fire_step.collision_plane_normal, plane.normal);
		plane.dist = fire_step.collision_plane_distance;
		plane.type = (byte)fire_step.collision_plane_type;
		memset(&surface, 0, sizeof(surface));
		surface.flags = fire_step.collision_surface_flags;
		VectorMA (bolt->s.origin, -10, dir, bolt->s.origin);
		self->client->hook = bolt;
		bolt->touch(bolt, target, &plane, &surface);
		if (self->client->hook != bolt)
			return NULL;
	}
	return bolt;
}	


// Ent is the owner
void Draw_Hook (edict_t *ent, vec3_t start, vec3_t end)
{
	vec3_t	dir, mins, maxs;
	vec3_t	offset;
	float		*v;

	v = tv(-15,-15,-15);
	_VectorCopy (v, mins);
	v = tv(15,15,15);
	_VectorCopy (v, maxs);

	VectorSubtract(end, start, dir);
	VectorSet(offset, 0, 0, 0);

	// Only display the grapple line if it isn't short
	if (VectorLength(dir) > 64)
	{
		gi.WriteByte (svc_temp_entity);
		gi.WriteByte (TE_GRAPPLE_CABLE);
		gi.WriteShort (ent - g_edicts);
		gi.WritePosition (start);
		gi.WritePosition (end);
		gi.WritePosition (offset);
		gi.multicast (ent->s.origin, MULTICAST_PVS);
	}	
}


/* The hook's actual shot/pull origin.  Keep this public and pure enough for
 * the SLIPGATE oracle: a proof and the live weapon must not acquire separate
 * versions of the handed {8,8,viewheight-8} muzzle transform. */
void CTF_HookMuzzle (const vec3_t origin, float viewheight, int hand,
	const vec3_t forward, const vec3_t right, vec3_t start)
{
	vec3_t offset;

	VectorSet (offset, 8, 8, viewheight - 8);
	if (hand == LEFT_HANDED)
		offset[1] *= -1;
	else if (hand == CENTER_HANDED)
		offset[1] = 0;
	start[0] = origin[0] + forward[0] * offset[0] + right[0] * offset[1];
	start[1] = origin[1] + forward[1] * offset[0] + right[1] * offset[1];
	start[2] = origin[2] + forward[2] * offset[0] + right[2] * offset[1]
	         + offset[2];
}

/*
 * The one grapple-pull law used by both the weapon and the rune oracle.
 * Return the same integer-truncated rope length the production weapon uses
 * and emit the exact velocity ladder.  SV_AddGravity in the historical
 * weapon path ran immediately before this velocity replaced ent->velocity,
 * so it had no observable effect and is intentionally absent here.
 */
int CTF_HookPullVelocity (const vec3_t start, const vec3_t bite,
	vec3_t velocity)
{
	int speed;

	VectorSubtract (bite, start, velocity);
	speed = (int)VectorLength (velocity);
	VectorNormalize (velocity);

	if (speed > 120)
	{
#ifdef WEAP_BALANCE_OK
		if ((int)ctfflags->value & CTF_WEAP_BALANCE)
			VectorScale (velocity, GRAPPLE_PULL_BALANCED_SPEED, velocity);
		else
			VectorScale (velocity, GRAPPLE_PULL_SPEED, velocity);
#else
		VectorScale (velocity, GRAPPLE_PULL_SPEED, velocity);
#endif
	}
	else if (speed > 100)
		VectorScale (velocity, speed * 5, velocity);
	else if (speed > 80)
		VectorScale (velocity, speed * 4, velocity);
	else if (speed > 40)
		VectorScale (velocity, speed * 3, velocity);
	else if (speed > 20)
		VectorScale (velocity, speed * 2, velocity);
	else if (speed > 10)
		VectorScale (velocity, speed, velocity);

	return speed;
}

/* Apply one production grapple update. Graph bots use the offhand path and
 * reach this once through ClientEndServerFrame; weapon-held human grapples
 * retain their historical Weapon_Generic calls as well. Bot-private pre-pmove
 * pulls execute a different chronology and are deliberately forbidden. */
void CTF_HookPullStep (edict_t *ent, qboolean draw_cable)
{
	vec3_t start, forward, right, velocity, dest;
	int speed;

	if (!ent || !ent->client || ent->client->hookstate != 2 ||
	    !ent->client->hook)
		return;

	AngleVectors (ent->client->v_angle, forward, right, NULL);
	CTF_HookMuzzle (ent->s.origin, ent->viewheight, ent->client->pers.hand,
	                forward, right, start);

	if (ent->client->hook->hook_target)
	{
		VectorAdd (ent->client->hook->hook_target->absmin,
		           ent->client->hook->hook_offset, dest);
		VectorCopy (dest, ent->client->hook->s.origin);
	}

	if (draw_cable)
		Draw_Hook (ent, start, ent->client->hook->s.origin);

	if (SG_OwnsBot(ent))
	{
		sg_host_law_result_t pull_result =
			SG_HostLawProductionHookPullVelocity((uint32_t)ent->s.number,
				(uint32_t)ent->client->hook->s.number, velocity, &speed);

		if (pull_result.status != SG_HOST_LAW_OK)
		{
			ctf_hook_abort(ent);
			return;
		}
	}
	else
		speed = CTF_HookPullVelocity (start, ent->client->hook->s.origin,
		                              velocity);

	if (!ent->client->hooklength)
		ent->client->hooklength = speed;
	ent->client->hooklength = speed;

	VectorCopy (velocity, ent->velocity);
	VectorCopy (ent->velocity, ent->client->oldvelocity);
	if (SG_OwnsBot(ent))
	{
		sg_compound_hook_live_result_t compound_result =
		    SG_CompoundHookGamePullApplied(ent, ent->client->hook);

		if (compound_result.outcome == SG_COMPOUND_HOOK_LIVE_RECOVERING ||
		    compound_result.outcome == SG_COMPOUND_HOOK_LIVE_REJECTED)
			ctf_hook_abort(ent);
	}
}

static void LMCTF_HumanHookFire(edict_t *ent)
{
	vec3_t offset, start, forward, right, dir;
	int speed;
	vec3_t dest;

	ent->client->isfiring = 0;
	if (ent->client->hookstate == 0)
		VectorCopy(ent->client->v_angle, ent->client->hookangle);
	AngleVectors(ent->client->v_angle, forward, right, NULL);
	VectorSet(offset, 8, 8, ent->viewheight - 8);
	P_ProjectSource(ent->client, ent->s.origin, offset, forward, right, start);

	switch (ent->client->hookstate)
	{
	case 0:
		VectorScale(forward, -2, ent->client->kick_origin);
		ent->client->kick_angles[0] = -1;
		ent->client->hookstate++;
		ent->client->isfiring = 1;
		ent->client->hook = LMCTF_FireHumanHook(ent, start, forward,
		    GRAPPLE_FIRE_HOOK_SPEED);
		Draw_Hook(ent, start, ent->client->hook->s.origin);
		/* fall through */
	case 1:
		if (ent->client->hook)
			Draw_Hook(ent, start, ent->client->hook->s.origin);
		break;
	case 2:
		if (!ent->client->hook)
		{
			ent->client->hookstate = 0;
			break;
		}
		if (ent->client->hook->hook_target)
		{
			VectorAdd(ent->client->hook->hook_target->absmin,
			    ent->client->hook->hook_offset, dest);
			VectorCopy(dest, ent->client->hook->s.origin);
		}
		Draw_Hook(ent, start, ent->client->hook->s.origin);
		VectorSubtract(ent->client->hook->s.origin, start, dir);
		speed = VectorLength(dir);
		if (!ent->client->hooklength)
			ent->client->hooklength = speed;
		ent->client->hooklength = speed;
		VectorNormalize(dir);
		if (speed > 120)
		{
#ifdef WEAP_BALANCE_OK
			if ((int)ctfflags->value & CTF_WEAP_BALANCE)
				VectorScale(dir, GRAPPLE_PULL_BALANCED_SPEED, dir);
			else
				VectorScale(dir, GRAPPLE_PULL_SPEED, dir);
#else
			VectorScale(dir, GRAPPLE_PULL_SPEED, dir);
#endif
			SV_AddGravity(ent);
		}
		else if (speed > 100)
			VectorScale(dir, speed * 5, dir);
		else if (speed > 80)
			VectorScale(dir, speed * 4, dir);
		else if (speed > 40)
			VectorScale(dir, speed * 3, dir);
		else if (speed > 20)
			VectorScale(dir, speed * 2, dir);
		else if (speed > 10)
			VectorScale(dir, speed, dir);
		VectorCopy(dir, ent->velocity);
		VectorCopy(ent->velocity, ent->client->oldvelocity);
		break;
	default:
		ctf_hook_abort(ent);
		break;
	}
}


void Weapon_Hook_Fire (edict_t *ent)
{
	vec3_t	mins, maxs, start, forward, right;
	float		*v;

	if (!SG_OwnsBot(ent))
	{
		LMCTF_HumanHookFire(ent);
		return;
	}

	v = tv(-15,-15,-15);
	_VectorCopy (v, mins);
	v = tv(15,15,15);
	_VectorCopy (v, maxs);

	ent->client->isfiring = 0; // We are only "firing" when we start

	if (ent->client->hookstate == 0)
	{
		VectorCopy(ent->client->v_angle, ent->client->hookangle);
	}

	// Set out ending point to our starting point
	AngleVectors (ent->client->v_angle, forward, right, NULL);
	CTF_HookMuzzle (ent->s.origin, ent->viewheight, ent->client->pers.hand,
	                forward, right, start);
	
	switch (ent->client->hookstate)
	{
	case 0: // Starting
		VectorScale (forward, -2, ent->client->kick_origin);
		ent->client->kick_angles[0] = -1;
		ent->client->hookstate++;
		ent->client->isfiring = 1;

		ent->client->hook = fire_hook (ent, start, forward, GRAPPLE_FIRE_HOOK_SPEED);
		if (SG_OwnsBot(ent) && !ent->client->hook)
		{
			ent->client->hookstate = 0;
			break;
		}

		//-bat.  Let's see this right away!
		Draw_Hook(ent, start, ent->client->hook->s.origin);

	case 1: // Moving out
		if (ent->client->hook)
			Draw_Hook(ent, start, ent->client->hook->s.origin);
		break;
	case 2: // Pulling us to the hook
		if (ent->client->hook)
			CTF_HookPullStep (ent, true);
		else
			ent->client->hookstate = 0;
		break;
	default: //bug
		ctf_hook_abort(ent);
		break;
	}
}

void Weapon_Hook (edict_t *ent)
{
	static int	pause_frames[]	= {14, 18, 26, 30, 0};
	static int	fire_frames[]	= {8, 9, 10, 11,0};

	//if (ent->client->weaponstate == WEAPON_READY)
	//		ent->client->hookstate = 0;
	
	if (ent->client->weaponstate == WEAPON_ACTIVATING)
	{
		// Speed up activation
		ent->client->ps.gunframe+=1;
	}

	if (ent->client->newweapon && ent->client->weaponstate != WEAPON_DROPPING)
	{
		ent->client->weaponstate = WEAPON_DROPPING;
		ent->client->ps.gunframe = 36; //FRAME_DEACTIVATE_FIRST;
		return;
	}
	
	if ( !((ent->client->latched_buttons|ent->client->buttons) & BUTTON_ATTACK))
	{
		SG_HumanTraceHookRelease(ent);
		ctf_hook_abort(ent);
	}
	
	Weapon_Generic (ent, 9, 13, 34, 38, pause_frames, fire_frames, Weapon_Hook_Fire);

}
// END CTF CODE

/*	SKWiD MOD
======================================================================

Plasma Rifle

======================================================================
*/

void weapon_plasma_fire (edict_t *ent)
{
	vec3_t	offset, start;
	vec3_t	forward, right;

	// if outa ammo, don't fire
	if (ent->client->pers.inventory[ent->client->ammo_index] < 1) {
		ent->client->ps.gunframe++;

		if (level.time >= ent->pain_debounce_time)
		{
			gi.sound(ent, CHAN_VOICE, gi.soundindex(PLASMA_SOUND_EMPTY), 1, ATTN_NORM, 0);
			ent->pain_debounce_time = level.time + 1;
		}
		
		NoAmmoWeaponChange (ent);
		return;
	}

	if (ent->client->ps.gunframe == 4) {
		AngleVectors (ent->client->v_angle, forward, right, NULL);
		VectorScale (forward, -2, ent->client->kick_origin);
		
		// fire weapon
		VectorSet(offset, 8, 8, ent->viewheight-8);
		P_ProjectSource (ent->client, ent->s.origin, offset, forward, right, start);

		if( ent->client->plasma_mode ) {
		gi.sound( ent,CHAN_WEAPON, gi.soundindex(PLASMA_SOUND_FIRE1), 1,
		          ATTN_NORM,0 );
		fire_plasma (ent, start, forward, 1);
		} else {
		gi.sound( ent,CHAN_WEAPON, gi.soundindex(PLASMA_SOUND_FIRE2), 1,
		          ATTN_NORM,0 );
		fire_plasma (ent, start, forward, 0);
		}

		if (! ( (int)dmflags->value & DF_INFINITE_AMMO ) )
			ent->client->pers.inventory[ent->client->ammo_index] -= 1;

		// make a big pitch kick with an inverse fall
		ent->client->v_dmg_pitch = -2;
		ent->client->v_dmg_roll = crandom()*2;
		ent->client->v_dmg_time = level.time + DAMAGE_TIME;

	}


	//-bat Silence??
	// send muzzle flash
	//gi.WriteByte (svc_muzzleflash);
	//gi.WriteShort (ent-g_edicts);
	//gi.WriteByte (MZ_ROCKET | is_silenced);
	//gi.multicast (ent->s.origin, MULTICAST_PVS);


	ent->client->ps.gunframe++;

	PlayerNoise(ent, start, PNOISE_WEAPON);
}

void Weapon_Plasma (edict_t *ent)
{
	static int	pause_frames[]	= {16, 46, 0};
	static int	fire_frames[]	= {4, 5, 0};
	//Weapon_PLASMA_Generic (ent, 3, 5, 46, 51, pause_frames, fire_frames, weapon_plasma_fire);
	//-bat make the time to fire next shot longer.
	//Weapon_PLASMA_Generic (ent, 3, 8, 46, 51, pause_frames, fire_frames, weapon_plasma_fire);
	Weapon_PLASMA_Generic (ent, 3, 11, 46, 51, pause_frames, fire_frames, weapon_plasma_fire);
}

// END
