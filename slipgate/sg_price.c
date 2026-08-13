/*
 * sg_price.c -- the composed surface: what a seed is WORTH.  The role
 * factor, detour value, the mega detour, and Surface_At, with the
 * frame-scoped pricing state the think loop arms before descending.
 * Moved verbatim from sg_arach.c in the 2026-08-12 standards pass.
 */
#include "g_local.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_cvars.h"
#include "g_ctffunc.h"
#include "slipgate/sg_bot.h"        /* sg_think_t -- the pricing context */
#include "slipgate/sg_price.h"
#include "slipgate/sg_util.h"

#define SG_MEGA_MAXDETOUR	4000    /* ms of extra road, hard refusal above */

/*
 * Detour worth: an item matters by how little it takes you off your road.
 *     value = worth / (1 + (cost_to_item + item_to_goal - direct) / scale)
 * All three terms are field lookups. An item dead on the route costs
 * nothing extra and pays full worth; one far off the road decays away.
 */
/*
 * Different runes are worth different things to different ROLES: a
 * defender wants staying power at the post (Resist, Regen), an attacker
 * and a carrier want the map to shrink (Haste), a recoverer wants the
 * re-kill (Damage). Class-level worth (Worth_Rune, sg_combat.c) prices
 * "a rune, given my state"; this table prices "THIS rune, given my job".
 * Values orbit 1.0 -- they are the fitted component, like every weight.
 */
float Rune_RoleFactor(int role, int entnum)
{
	static const struct { const char *cls; float w[SG_ROLES]; } tab[] = {
		/*                  ATTACK DEFEND CARRY  RECOVER ESCORT */
		{ "damage_rune",  { 1.15f, 1.15f, 0.70f, 1.25f, 1.10f } },
		{ "haste_rune",   { 1.30f, 0.80f, 1.30f, 1.15f, 1.00f } },
		/* courier-starvation fix (10,098 candidates, 40 tosses: the
		 * bots that hold defensive runes are DEFEND, anchored home,
		 * while the toss needs a holder NEAR THE CARRIER; under
		 * sg_runetoss>=2 the escort out-prices the defender for
		 * resist/regen so handoffs can actually form) */
		{ "resist_rune",  { 0.90f, 1.25f, 1.20f, 1.00f, 1.15f } },
		{ "regen_rune",   { 0.90f, 1.15f, 1.05f, 0.90f, 1.00f } },
		{ "vampire_rune", { 1.05f, 1.00f, 0.80f, 1.05f, 1.00f } },
	};
	edict_t *e;
	int i;

	if (entnum <= 0 || entnum >= globals.num_edicts)
		return 1.0f;
	e = g_edicts + entnum;
	if (!e->classname)
		return 1.0f;
	for (i = 0; i < (int)(sizeof(tab) / sizeof(tab[0])); i++)
		if (strcmp(e->classname, tab[i].cls) == 0)
		{
			float w2 = tab[i].w[role];

			if (role == SG_ROLE_ESCORT && i >= 2 && i <= 3 &&
			    sg_cv.runetoss->value >= 2)
				w2 = 1.40f;
			return w2;
		}
	return 1.0f;
}

qboolean sg_route_pure_now;  /* tactics priced at selection: the
                                     * per-frame walk stays pure */

float Detour_Value(sg_think_t *tc, int here, int cls, const int *goal_field,
                          float worth)
{
	int *ifld = sg_fields.item[cls];
	int to_item = ifld[here];
	int direct = goal_field[here];

	if (direct >= SG_FIELD_INF)
		return 0.0f;

	/*
	 * Where per-item fields exist (powerups, runes), the triangle is exact.
	 * per_item[cls][k] was flooded FROM item k, so it reads as cost from
	 * anywhere TO that item; the far leg is the goal field sampled at the
	 * item's own seed. The item that costs the least extra road wins -- not
	 * the nearest one, which is what a class field would have answered.
	 *
	 *     detour = cost_to_item + item_to_goal - direct
	 *     value  = worth / (1 + max(0, detour) / scale)
	 */
	if (sg_fields.per_item_count[cls] > 0)
	{
		float best = 0.0f;
		int k;

		for (k = 0; k < sg_fields.per_item_count[cls]; k++)
		{
			const int *kfld = sg_fields.per_item[cls][k];
			int kseed = sg_fields.per_item_seed[cls][k];
			int cost_to, item_to_goal, detour;
			float v;

			if (!kfld || kseed < 0)
				continue;
			cost_to = kfld[here];
			item_to_goal = goal_field[kseed];
			if (cost_to >= SG_FIELD_INF || item_to_goal >= SG_FIELD_INF)
				continue;

			detour = cost_to + item_to_goal - direct;
			if (detour < 0)
				detour = 0;      /* an item on the road is free, never a bonus */
			v = worth / (1.0f + (float)detour / 1500.0f);
			if (cls == SG_FC_RUNE)
				v *= Rune_RoleFactor(tc->role,
				                     sg_fields.per_item_ent[cls][k]);
			if (v > best)
				best = v;
		}
		return best;
	}

	/*
	 * The other classes are flooded per class only: item_to_goal is
	 * unknowable once many interchangeable items share one field, which gives
	 * cost to the NEAREST of them, so the triangle is approximated by to_item
	 * alone against scale. Honest limitation, recorded -- it holds for the
	 * classes whose members are interchangeable (a health box is a health
	 * box), which is why identity-bearing classes got per-item fields.
	 */
	if (to_item >= SG_FIELD_INF)
		return 0.0f;
	return worth / (1.0f + (float)to_item / 1500.0f);
}

float Mega_Detour(sg_think_t *tc, int here, const int *goal_field, int *out_ent)
{
	float	best = 0.0f;
	int		direct = goal_field[here];
	int		k;

	if (out_ent)
		*out_ent = -1;
	if (direct >= SG_FIELD_INF)
		return 0.0f;

	for (k = 0; k < sg_fields.mega_count; k++)
	{
		const int	*kfld = sg_fields.to_mega[k];
		int			kseed = sg_fields.mega_seed[k];
		int			cost_to, pad_to_goal, detour;
		float		v;

		if (!kfld || kseed < 0)
			continue;
		cost_to = kfld[here];
		pad_to_goal = goal_field[kseed];
		if (cost_to >= SG_FIELD_INF || pad_to_goal >= SG_FIELD_INF)
			continue;

		detour = cost_to + pad_to_goal - direct;
		if (detour < 0)
			detour = 0;         /* a pad on the road is free, never a bonus */
		if (detour > SG_MEGA_MAXDETOUR)
			continue;
		v = tc->mega / (1.0f + (float)detour / 1500.0f);
		if (v > best)
		{
			best = v;
			if (out_ent)
				*out_ent = sg_fields.mega_ent[k];
		}
	}
	return best;
}

/*
 * The surface at one seed, for one bot: weighted composition of every
 * basis field. LOWER is better (fields are costs); items and support
 * subtract because they add value. This is V(x | bot) from the design,
 * evaluated at the handful of seeds one step away.
 */
float Surface_At(sg_think_t *tc, int seed, const sg_weights_t *w,
                        const int *goal_field, const int *support,
                        const int *intercept)
{
	float v;
	int c;

	if (goal_field[seed] >= SG_FIELD_INF)
		return 1e30f;

	/*
	 * sg_atkobj (stage-2 volume lever 3): a per-server multiplier on the
	 * ATTACK role's objective pull, because the shared-gamedir weights
	 * file cannot A/B across a pair. 1.0 (default) is byte-identical;
	 * >1 makes attackers price the flag harder relative to items/cover,
	 * the hypothesis being that human-level steal VOLUME comes from
	 * commitment, not routes. Trialed like everything else.
	 */
	v = w->objective * (float)goal_field[seed];
	if (tc->role == SG_ROLE_ATTACK)
	{
		float ao = sg_cv.atkobj->value / 100.0f;

		if (ao != 1.0f)
			v = w->objective * ao * (float)goal_field[seed];
	}

	/*
	 * THE SHELF PAYS ITS CLIFF (sg_shelfcost, steal-genesis study): an
	 * attacker pricing a seed under the enemy stand pays the measured
	 * climb-back asymmetry RL_DROP never charged. 101 attempts on that
	 * floor, 91% dead in 1.2s, zero steals -- removing a zero-yield room
	 * cannot cost caps. Zero everywhere on flat-stand maps.
	 */
	if (tc->role == SG_ROLE_ATTACK &&
	    sg_cv.shelfcost->value > 0.0f)
	{
		int ti9 = SG_TeamIdx(tc->team);

		if (sg_fields.shelf_cliff[ti9] &&
		    sg_fields.shelf_cliff[ti9][seed] > 0)
			v += sg_cv.shelfcost->value *
			     (float)sg_fields.shelf_cliff[ti9][seed];
	}

	/* tactics mode: the waypoint was scored with the full surface at
	 * commitment; between commitments the descent runs on its flood
	 * and nothing else -- one field, no ties, no scribble */
	if (sg_route_pure_now)
		return v;

	/*
	 * THE NAKED CARRY (sg_nakedcarry, A/B wave 166+). Every navigation
	 * link from map file to field flood verified sound tonight, yet
	 * parity carriers drift away from a correct destination -- and one
	 * price term is self-reinforcing: the learned danger field records
	 * every carrier death along the road home and taxes that road for
	 * the next carrier. At parity the deaths are dense, so the tax
	 * compounds until home costs more than wandering. Naked mode prices
	 * a carrier's step by time-to-home and NOTHING else: no danger, no
	 * item detours, no cover terms. If naked carriers close on their
	 * stands, the wall was the pricing; if they still wander, the
	 * descent itself goes under the microscope.
	 */
	if (tc->role == SG_ROLE_CARRY &&
	    sg_cv.nakedcarry->value)
		return v;

	/* the danger dimension: learned, decayed, team-indexed (set by the
	 * caller in the context); zero where nothing has died */
	if (tc->danger && seed < SG_MAX_SEEDS)
		v += (float)tc->danger[seed];

	for (c = 0; c < SG_FIELD_CLASSES; c++)
		if (w->item[c] > 0.0f)
			/* legcarrier dose 3: a healthy carrier does not shop --
			 * humans never detour mid-carry (corpus: 310 u/s flat).
			 * Hurt carriers keep the detour; health is worth a stop. */
			v -= (tc->push ||
			      (tc->role == SG_ROLE_CARRY && tc->health > 60 &&
			       sg_cv.legcarrier->value >= 3.0f))
			     ? 0.0f :
			     1500.0f * Detour_Value(tc, seed, c, goal_field, w->item[c]);

	/*
	 * THE MEGA (sg_megaworth). A separate term rather than a bend in the
	 * health class, because the two say opposite things at 100 hp: the class
	 * is worth 0.05 there and the mega is worth its whole budget. The mega worth
	 * is already zero unless the role, the belief, the headroom and the fight
	 * all permit it (Mega_Worth), so this line is an OFFER and never a
	 * requirement -- the objective term is untouched and a bot that finds
	 * nothing cheap enough simply walks its road.
	 *
	 * Below the naked-carry and route-pure early returns above on purpose: a
	 * carrier does not shop, and a committed tactical waypoint was scored
	 * with this term already in it.
	 */
	if (tc->mega > 0.0f && sg_fields.mega_count > 0)
		v -= 1500.0f * Mega_Detour(tc, seed, goal_field, NULL);

	if (support && w->carrier_support > 0.0f && support[seed] < SG_FIELD_INF)
	{
		float csup = w->carrier_support;

		/*
		 * LONE WOLF (sg_lonewolf, rung-4 set #1 tell #1). All three
		 * judges read the same thing off every bot sheet: our carries
		 * run escorted 0.33-0.75 of their length while pub humans run
		 * flags alone at 0.02-0.32. sg_escortdose gated the ESCORT
		 * ROLE and moved nothing, because escort_fraction measures
		 * teammate PROXIMITY and the proximity is incidental -- every
		 * non-escort role also carries a carrier_support pull (defend
		 * 0.40, attack 0.10), so the whole team drifts down the
		 * carrier's lane whatever its assignment.
		 *
		 * This is the honest cut: REMOVE the artificial cohesion
		 * rather than add artificial dispersion. A pub is not a team
		 * fleeing its own carrier -- it is players whose objectives
		 * simply diverge. The dose scales the support pull for every
		 * role except the assigned escort, whose whole job it is.
		 * 1.0 == today's behavior exactly.
		 */
		if (tc->role != SG_ROLE_ESCORT)
		{
			cvar_t *lw = sg_cv.lonewolf;

			if (lw && lw->value >= 0.0f)
				csup *= lw->value;
		}
		v += csup * (float)support[seed];
	}

	if (intercept && w->intercept > 0.0f && intercept[seed] < SG_FIELD_INF)
		v += w->intercept * (float)intercept[seed];

	return v;
}
