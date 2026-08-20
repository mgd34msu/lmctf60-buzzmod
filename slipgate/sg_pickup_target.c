#include "g_local.h"
#include "g_ctffunc.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_defense_supply.h"
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_pickup_target.h"

qboolean SG_LocalPickupTarget(edict_t *self, edict_t *item, vec3_t target)
{
	vec3_t delta;
	trace_t trace;

	if (!self || !self->inuse || !self->client || !item || !item->inuse ||
	    !target || !sg_host.trace)
		return false;
	VectorSubtract(item->s.origin, self->s.origin, delta);
	if (delta[0] * delta[0] + delta[1] * delta[1] >= 160.0f * 160.0f ||
	    fabsf(delta[2]) > 64.0f)
		return false;
	trace = sg_host.trace(self->s.origin, self->mins, self->maxs,
	    item->s.origin, self, MASK_PLAYERSOLID);
	if (trace.startsolid || trace.allsolid ||
	    (trace.fraction < 1.0f && trace.ent != item))
		return false;
	VectorCopy(item->s.origin, target);
	return true;
}

qboolean SG_WeaponPickupRouteEligible(const edict_t *item,
	const edict_t *taker)
{
	if (!item || !item->inuse || !item->classname ||
	    strncmp(item->classname, "weapon_", 7) != 0)
		return false;
	if (strcmp(item->classname, "weapon_hook") == 0 ||
	    strcmp(item->classname, "weapon_blaster") == 0)
		return false;
	return item->solid != SOLID_NOT && Caco_ItemBelievedUp((edict_t *)item) &&
	       G_WeaponPickupEligible((edict_t *)item, (edict_t *)taker);
}

static qboolean SG_WeaponTargetWitnessValid(const sg_bot_t *bot, int item_ent,
	int target_seed, const vec3_t target_origin)
{
	edict_t *item;
	vec3_t delta;
	int seed;

	if (!bot || !bot->ent || !SG_Rune() || item_ent <= 0 ||
	    item_ent >= globals.num_edicts)
		return false;
	item = &g_edicts[item_ent];
	if (!SG_WeaponPickupRouteEligible(item, bot->ent))
		return false;
	seed = Rune_NearestSeed(SG_Rune(), item->s.origin);
	if (seed < 0 || seed != target_seed)
		return false;
	VectorSubtract(item->s.origin, target_origin, delta);
	return DotProduct(delta, delta) <= 1.0f;
}

qboolean SG_DefenseSupplyTargetValid(const sg_bot_t *bot)
{
	return bot && SG_WeaponTargetWitnessValid(bot, bot->def_supply_ent,
	    bot->def_supply_target_seed, bot->def_supply_target_org);
}

qboolean SG_StrikeWeaponTargetValid(const sg_bot_t *bot)
{
	return bot && SG_WeaponTargetWitnessValid(bot,
	    bot->strike_weapon_target_ent, bot->strike_weapon_target_seed,
	    bot->strike_weapon_target_org);
}

qboolean SG_WeaponPickupTarget(const sg_bot_t *bot, qboolean strike_pursuit,
	vec3_t target)
{
	edict_t *item = NULL;

	if (!bot || !target)
		return false;
	if (bot->def_supply_armed &&
	    bot->def_supply_phase == SG_DEFENSE_SUPPLY_PHASE_OUTBOUND &&
	    bot->def_supply_instance == bot->instance_token &&
	    SG_DefenseSupplyTargetValid(bot))
		item = &g_edicts[bot->def_supply_ent];
	else if (strike_pursuit && SG_StrikeWeaponTargetValid(bot))
		item = &g_edicts[bot->strike_weapon_target_ent];
	return item && SG_LocalPickupTarget(bot->ent, item, target);
}

qboolean SG_MegaPickupTarget(const sg_think_t *tc, vec3_t target)
{
	edict_t *item;

	if (!tc || !tc->e || !target || tc->route_pure ||
	    !isfinite(tc->mega) || tc->mega <= 0.0f ||
	    (tc->team != CTF_TEAM_RED && tc->team != CTF_TEAM_BLUE) ||
	    tc->mega_target_ent <= 0 ||
	    tc->mega_target_ent >= globals.num_edicts)
		return false;
	item = &g_edicts[tc->mega_target_ent];
	if (!item->inuse || !item->classname ||
	    strcmp(item->classname, "item_health_mega") != 0 ||
	    item->solid == SOLID_NOT ||
	    !Caco_ItemBelievedUpFor(tc->team, item) ||
	    !G_HealthPickupEligible(item, tc->e))
		return false;
	return SG_LocalPickupTarget(tc->e, item, target);
}
