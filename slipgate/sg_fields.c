/*
 * sg_fields.c -- the basis: every potential the surface composes.
 *
 * A field is cost-in-milliseconds from every seed to somewhere that matters,
 * flooded over the rune's proven links. Static fields (flags, bases, item
 * classes) build at level setup and rebuild when their goal state changes;
 * dynamic fields (our carrier, their carrier's projected position) rebuild on
 * a short cadence from CACO's belief, never from raw entity state.
 *
 * All fields live in one registry so the surface can name them by index and
 * the debug dump can walk them.
 */

#include "g_local.h"
#include "g_ctffunc.h"
#include "slipgate/sg_local.h"

sg_fields_t sg_fields;

/*
 * Multi-source Dijkstra over reversed links: dist[] = cost to reach the
 * NEAREST source seed. O(n^2) at our sizes is milliseconds. Sources carry
 * an initial cost so a projected position can be seeded with its age.
 */
void Field_Flood(rune_t *r, int *dist,
                 const int *sources, const int *source_cost, int num_sources)
{
	byte done[SG_MAX_SEEDS];
	int i, j;

	for (i = 0; i < r->hdr.num_seeds; i++)
	{
		dist[i] = SG_FIELD_INF;
		done[i] = 0;
	}
	for (i = 0; i < num_sources; i++)
		if (sources[i] >= 0 && source_cost[i] < dist[sources[i]])
			dist[sources[i]] = source_cost[i];

	for (i = 0; i < r->hdr.num_seeds; i++)
	{
		int u = -1, ud = SG_FIELD_INF;

		for (j = 0; j < r->hdr.num_seeds; j++)
			if (!done[j] && dist[j] < ud)
			{
				ud = dist[j];
				u = j;
			}
		if (u < 0)
			break;
		done[u] = 1;

		for (j = 0; j < r->hdr.num_links; j++)
		{
			if (r->links[j].to != u)
				continue;
			if (dist[u] + r->links[j].cost_ms < dist[r->links[j].from])
				dist[r->links[j].from] = dist[u] + r->links[j].cost_ms;
		}
	}
}

static int *Field_Alloc(rune_t *r)
{
	return gi.TagMalloc(sizeof(int) * r->hdr.num_seeds, TAG_LEVEL);
}

static void Field_FromOne(rune_t *r, int *dist, int seed)
{
	int cost = 0;

	Field_Flood(r, dist, &seed, &cost, 1);
}

/*
 * Item-class fields seed from every live entity of the class. An item that
 * is taken (respawn pending) is excluded; the field rebuilds when the class
 * signature changes, checked once a second by Fields_Refresh.
 */
typedef struct
{
	const char	*prefixes[6];   /* classname prefixes in the class */
} fieldclass_t;

static const fieldclass_t field_classes[SG_FIELD_CLASSES] = {
	/* SG_FC_WEAPON  */ { { "weapon_", NULL } },
	/* SG_FC_ARMOR   */ { { "item_armor", NULL } },
	/* SG_FC_AMMO    */ { { "ammo_", NULL } },
	/* SG_FC_HEALTH  */ { { "item_health", NULL } },
	/* SG_FC_RUNE    */ { { "damage_rune", "resist_rune", "haste_rune",
	                        "regen_rune", "vampire_rune", NULL } },
	/* SG_FC_POWERUP */ { { "item_quad", "item_invulnerability", NULL } },
};

static qboolean Class_Match(const fieldclass_t *fc, const char *classname)
{
	int i;

	for (i = 0; i < 6 && fc->prefixes[i]; i++)
		if (strncmp(classname, fc->prefixes[i], strlen(fc->prefixes[i])) == 0)
			return true;
	return false;
}

static unsigned Class_Signature(const fieldclass_t *fc)
{
	unsigned sig = 2166136261u;
	edict_t *e;
	int i;

	for (i = 0; i < globals.num_edicts; i++)
	{
		e = &g_edicts[i];
		if (!e->inuse || !e->classname || !Caco_ItemBelievedUp(e))
			continue;               /* taken items are SOLID_NOT while waiting */
		if (!Class_Match(fc, e->classname))
			continue;
		sig = (sig ^ (unsigned)i) * 16777619u;
	}
	return sig;
}

/*
 * Which classes get a field per item as well as a field per class. Powerups
 * and runes: a quad is not an invulnerability and a haste rune is not a
 * vampire rune, so the surface has to be able to price a named one, and there
 * are only a handful of each on any map.
 */
static qboolean Class_PerItem(int cls)
{
	return (cls == SG_FC_POWERUP || cls == SG_FC_RUNE);
}

static void Class_Build(rune_t *r, int cls)
{
	int sources[256] = { 0 }, costs[256] = { 0 }, n = 0;
	edict_t *e;
	int i;

	for (i = 0; i < globals.num_edicts && n < 256; i++)
	{
		e = &g_edicts[i];
		if (!e->inuse || !e->classname || !Caco_ItemBelievedUp(e))
			continue;
		if (!Class_Match(&field_classes[cls], e->classname))
			continue;
		sources[n] = Caco_ItemBeliefSeed(r, e);
		costs[n] = 0;
		if (sources[n] >= 0)
			n++;
	}
	Field_Flood(r, sg_fields.item[cls], sources, costs, n);
	sg_fields.item_sig[cls] = Class_Signature(&field_classes[cls]);

	/*
	 * The same live entities, one field each, flooded FROM the item -- so the
	 * field reads as cost from anywhere TO that item, and the far leg of the
	 * detour triangle is a lookup of the goal field at the item's own seed.
	 * The buffers are allocated once at setup; this only refloods them.
	 */
	if (!Class_PerItem(cls))
	{
		sg_fields.per_item_count[cls] = 0;
		return;
	}
	if (n > SG_MAX_PER_ITEM)
		n = SG_MAX_PER_ITEM;
	for (i = 0; i < n; i++)
	{
		sg_fields.per_item_seed[cls][i] = sources[i];
		if (sg_fields.per_item[cls][i])
			Field_FromOne(r, sg_fields.per_item[cls][i], sources[i]);
	}
	for (i = n; i < SG_MAX_PER_ITEM; i++)
		sg_fields.per_item_seed[cls][i] = -1;
	sg_fields.per_item_count[cls] = n;
}

qboolean Fields_Setup(rune_t *r)
{
	edict_t *rf, *bf;
	int i;

	rf = G_Find(NULL, FOFS(classname), "info_flag_red");
	bf = G_Find(NULL, FOFS(classname), "info_flag_blue");
	if (!rf || !bf)
		return false;

	sg_fields.red_flag_seed = Rune_NearestSeed(r, rf->s.origin);
	sg_fields.blue_flag_seed = Rune_NearestSeed(r, bf->s.origin);
	if (sg_fields.red_flag_seed < 0 || sg_fields.blue_flag_seed < 0)
		return false;

	sg_fields.to_red_flag = Field_Alloc(r);
	sg_fields.to_blue_flag = Field_Alloc(r);
	Field_FromOne(r, sg_fields.to_red_flag, sg_fields.red_flag_seed);
	Field_FromOne(r, sg_fields.to_blue_flag, sg_fields.blue_flag_seed);

	{
		int i, nr = 0, nb = 0;

		for (i = 0; i < r->hdr.num_seeds; i++)
		{
			if (sg_fields.to_red_flag[i] < SG_FIELD_INF) nr++;
			if (sg_fields.to_blue_flag[i] < SG_FIELD_INF) nb++;
		}
		gi.dprintf("slipgate: field coverage red %d/%d blue %d/%d (flag seeds %d, %d)\n",
		           nr, r->hdr.num_seeds, nb, r->hdr.num_seeds,
		           sg_fields.red_flag_seed, sg_fields.blue_flag_seed);
	}

	/* dropped-flag fields start as copies of the home fields */
	sg_fields.to_red_flag_now = Field_Alloc(r);
	sg_fields.to_blue_flag_now = Field_Alloc(r);
	memcpy(sg_fields.to_red_flag_now, sg_fields.to_red_flag,
	       sizeof(int) * r->hdr.num_seeds);
	memcpy(sg_fields.to_blue_flag_now, sg_fields.to_blue_flag,
	       sizeof(int) * r->hdr.num_seeds);

	/*
	 * Every pointer in sg_fields was TAG_LEVEL and is dangling by now, so all
	 * of them are (re)allocated here rather than lazily -- a "if (!ptr)" test
	 * would see the previous level's freed address and skip.
	 */
	for (i = 0; i < SG_FIELD_CLASSES; i++)
	{
		int k;

		sg_fields.item[i] = Field_Alloc(r);
		sg_fields.per_item_count[i] = 0;
		for (k = 0; k < SG_MAX_PER_ITEM; k++)
		{
			sg_fields.per_item[i][k] = Class_PerItem(i) ? Field_Alloc(r) : NULL;
			sg_fields.per_item_seed[i][k] = -1;
		}
		Class_Build(r, i);
	}

	sg_fields.our_carrier[0] = Field_Alloc(r);   /* index by team-1 */
	sg_fields.our_carrier[1] = Field_Alloc(r);
	sg_fields.our_carrier_valid[0] = sg_fields.our_carrier_valid[1] = false;
	for (i = 0; i < r->hdr.num_seeds; i++)
		sg_fields.our_carrier[0][i] = sg_fields.our_carrier[1][i] = SG_FIELD_INF;

	sg_fields.next_refresh = 0.0f;
	return true;
}

/*
 * Once a second: rebuild item fields whose live-entity signature changed,
 * and re-seed the flag-now fields from CACO's belief about where each flag
 * actually is (home, dropped somewhere seen, or carried by someone seen).
 */
void Fields_Refresh(rune_t *r)
{
	int i;

	if (level.time < sg_fields.next_refresh)
		return;
	sg_fields.next_refresh = level.time + 1.0f;

	{
		/* the entity walk sees an item going up or down, but a rune moves
		 * WHILE up every 30s (Rune_Think, g_runes.c:338) -- the belief
		 * signature is what notices that */
		static unsigned belief_sig;
		unsigned bsig = Caco_ItemBeliefSig();

		for (i = 0; i < SG_FIELD_CLASSES; i++)
			if (Class_Signature(&field_classes[i]) != sg_fields.item_sig[i]
			    || (Class_PerItem(i) && bsig != belief_sig))
				Class_Build(r, i);
		belief_sig = bsig;
	}

	/* flag positions per CACO: seed the "now" field wherever belief puts it */
	for (i = 0; i < 2; i++)
	{
		sg_belief_flag_t *bf = &sg_caco_team_belief.flag[i];
		int *fld = i ? sg_fields.to_blue_flag_now : sg_fields.to_red_flag_now;
		int home = i ? sg_fields.blue_flag_seed : sg_fields.red_flag_seed;
		int seed, cost = 0;

		if (bf->state == SG_FLAG_HOME || bf->where_seed < 0)
			seed = home;
		else
			seed = bf->where_seed;
		Field_Flood(r, fld, &seed, &cost, 1);
	}

	/* our-carrier support fields, one per team, from believed position */
	for (i = 0; i < 2; i++)
	{
		sg_belief_carrier_t *c = &sg_caco_team_belief.carrier[i];
		int cost = 0;

		if (c->client >= 0 && c->seed >= 0)
		{
			Field_Flood(r, sg_fields.our_carrier[i], &c->seed, &cost, 1);
			sg_fields.our_carrier_valid[i] = true;
		}
	}
}
