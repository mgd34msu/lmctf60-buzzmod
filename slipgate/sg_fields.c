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
		if (!e->inuse || !e->classname || e->solid == SOLID_NOT)
			continue;               /* taken items are SOLID_NOT while waiting */
		if (!Class_Match(fc, e->classname))
			continue;
		sig = (sig ^ (unsigned)i) * 16777619u;
	}
	return sig;
}

static void Class_Build(rune_t *r, int cls)
{
	int sources[256], costs[256], n = 0;
	edict_t *e;
	int i;

	for (i = 0; i < globals.num_edicts && n < 256; i++)
	{
		e = &g_edicts[i];
		if (!e->inuse || !e->classname || e->solid == SOLID_NOT)
			continue;
		if (!Class_Match(&field_classes[cls], e->classname))
			continue;
		sources[n] = Rune_NearestSeed(r, e->s.origin);
		costs[n] = 0;
		if (sources[n] >= 0)
			n++;
	}
	Field_Flood(r, sg_fields.item[cls], sources, costs, n);
	sg_fields.item_sig[cls] = Class_Signature(&field_classes[cls]);
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

	for (i = 0; i < SG_FIELD_CLASSES; i++)
	{
		sg_fields.item[i] = Field_Alloc(r);
		Class_Build(r, i);
	}

	sg_fields.our_carrier[0] = Field_Alloc(r);   /* index by team-1 */
	sg_fields.our_carrier[1] = Field_Alloc(r);
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

	for (i = 0; i < SG_FIELD_CLASSES; i++)
		if (Class_Signature(&field_classes[i]) != sg_fields.item_sig[i])
			Class_Build(r, i);

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
			Field_Flood(r, sg_fields.our_carrier[i], &c->seed, &cost, 1);
	}
}
