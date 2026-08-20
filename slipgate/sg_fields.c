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
#include "slipgate/sg_action.h"
#include "slipgate/sg_cvars.h"
#include "slipgate/sg_util.h"
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_snag_repair.h"

sg_fields_t sg_fields;

/*
 * Multi-source Dijkstra over reversed links: dist[] = cost to reach the
 * NEAREST source seed. Sources carry an initial cost so a projected position
 * can be seeded with its age.
 *
 * The flood runs BACKWARDS: a link is stored from -> to, and the field asks
 * what it costs to get from a seed TO the source, so settling a seed u relaxes
 * every link that ARRIVES at u and writes the cost of its .from.
 */


static int sg_futile[SG_MAX_SEEDS];
static int sg_futile_last_seed = -1;
static int sg_futile_streak;

void SG_TeachFutility(int seed)
{
	int amount;

	if (seed < 0 || seed >= SG_MAX_SEEDS)
		return;
	/*
	 * A wall that keeps winning earns a compounding lesson. The flat 3s
	 * (capped 20s) surcharge cannot break a corridor MONOPOLY: on lmctf01
	 * the valley is the only 26s route, the alternatives cost more than
	 * the cap, so the flood funneled Gate back into seed 327 between
	 * every escape -- 136 firings, one game, one pillar (iter 49). Repeat
	 * firings at one seed now scale the charge up to 5x with no ceiling
	 * but the decay itself, which heals a 45s lesson in under four
	 * minutes. A seed that stops biting resets the streak.
	 */
	if (seed == sg_futile_last_seed)
		sg_futile_streak = (sg_futile_streak < 5) ?
		                     sg_futile_streak + 1 : 5;
	else
		sg_futile_streak = 1;
	sg_futile_last_seed = seed;
	amount = 3000 * sg_futile_streak;
	sg_futile[seed] += amount;
	if (sg_futile[seed] > 60000)
		sg_futile[seed] = 60000;
}

/*
 * THE ROPE'S PRICE AS A DOSE (sg_ropecost). Three blind
 * judges named the same tell on every caught route sheet: off-graph
 * fraction -- humans spend 3-18% of samples >96u from any nav node
 * (grapple flight through open air), bots pin near zero. The +1000
 * ritual surcharge below is why: it prices legs over ropes nearly
 * everywhere. It was fitted from a carrier's observed flailing; the rest
 * of the roster inherited it. Default 1000 keeps today's flood
	* exactly; configuration may lower it for comparison. Read once per field
 * build (Fields_Setup), not per relaxation.
 */
static int sg_ropecost_ms = 1000;

void SG_RopecostRefresh(void)
{
	sg_ropecost_ms = (int)sg_cv.ropecost->value;
	if (sg_ropecost_ms < 0)
		sg_ropecost_ms = 0;
}

static int Link_EffCost(const rune_link_t *l)
{
	/* 1000, not 400: the rope's REAL ritual is a standing aim frame (the
	 * body halts, the aiming state owns the view), the fire, and a landing
	 * brake. Carrier traces show ~2-3s of wall clock per rope against
	 * cost_ms figures in the hundreds, and a carrier that chained ropes
	 * covered 4s of field in 70s of flailing while a run would have flown.
	 * Underpricing the ritual made the flood chain hooks where legs win.
	 * The registry preserves the other legacy doses: DROP +150 to align the
	 * lip line and reserved RJ +900 to raise, aim, and pay. */
	return l->cost_ms + SG_ActionFieldBiasMs(l->action, sg_ropecost_ms);
}

/*
 * Static fields may depend only on actions the loaded server configuration
 * can ever execute.  Transient readiness (inventory, hook phase, a per-bot
 * ban) belongs to live link selection, but a map loaded without the offhand
 * hook can never execute RL_HOOK at all.  Leaving those edges in the flood
 * builds gradients whose cheapest exit the body is permanently forbidden to
 * take.  A missing host cvar is not authority to publish hook reachability.
 */
static qboolean Field_HookCapability(void)
{
	return ctfflags &&
	       (((int)ctfflags->value & CTF_OFFHAND_HOOK) != 0);
}

static qboolean Field_LinkAdmitted(const rune_link_t *link)
{
	if (!link)
		return false;
	return Fields_ActionAdmitted(link->action);
}

qboolean Fields_ActionAdmitted(int action)
{
	return action != RL_HOOK || sg_fields.hook_admitted;
}

qboolean Fields_ActionTopologyCurrent(unsigned epoch)
{
	return epoch != 0 && epoch == sg_fields.action_topology_epoch;
}



#define SG_ENV_BUCKETS		4
#define SG_ENV_BUCKET_SPAN	(256 / SG_ENV_BUCKETS)  /* stored units per band */

/*
 * ms per byte-degree of body turn beyond the link's own slack. FITTED: the
 * tree measures no turn rate, because turning is not a pmove parameter -- it
 * is the body steering its velocity round with SG_Strafe while friction and
 * the accel cap fight it. 2ms/byte makes a full 180 (128 bytes) cost 256ms,
 * about the quarter second a run-out-and-back visibly takes.
 */
#define SG_TURN_MS_PER_BYTE	2

/*
 * The generator's own link ceiling, LINK_MAX at sg_rune.c:30. It is private to
 * the generator, so it is mirrored here rather than shared; the reverse index
 * below is sized by it and a bigger rune than the generator can write is
 * refused rather than trusted.
 */
#define SG_ENV_MAX_LINKS	262144

static int Env_Bucket(int stored_speed)
{
	int b = stored_speed / SG_ENV_BUCKET_SPAN;

	if (b < 0)
		b = 0;
	if (b >= SG_ENV_BUCKETS)
		b = SG_ENV_BUCKETS - 1;
	return b;
}

static int Env_BucketSpeed(int bucket)      /* representative, stored units */
{
	return bucket * SG_ENV_BUCKET_SPAN + SG_ENV_BUCKET_SPAN / 2 - 1;
}


static int Env_AccelCost(int have_stored, int need_stored)
{
	if (need_stored <= have_stored)
		return 0;
	return ((need_stored - have_stored) * 4) / 3;
}

/*
 * Time to swing the body from the heading it arrived on to the one the next
 * link's envelope wants. Inside the link's own slack it is free.
 */
static int Env_TurnCost(int from_head, int to_head, int slack)
{
	int d = (from_head - to_head) & 255;

	if (d > 128)
		d = 256 - d;
	if (d <= slack)
		return 0;
	return (d - slack) * SG_TURN_MS_PER_BYTE;
}

/*
 * Scratch. Static, never allocated: the fields themselves stay TAG_LEVEL and
 * are untouched by this, and nothing here may be cached across a level because
 * a TAG_LEVEL rune's address can be reused by the next one. Sized by the hard
 * caps (SG_MAX_SEEDS seeds, SG_ENV_MAX_LINKS links), ~3.1MB of BSS of which a
 * real map touches about a tenth.
 */
static int	env_dist[SG_MAX_SEEDS * SG_ENV_BUCKETS];
static byte	env_done[SG_MAX_SEEDS * SG_ENV_BUCKETS];
static byte	env_head[SG_MAX_SEEDS * SG_ENV_BUCKETS];    /* the plan's first
                                                         * link: its heading */
static byte	env_slack[SG_MAX_SEEDS * SG_ENV_BUCKETS];   /* ...and its slack */
static int	env_heap[SG_MAX_SEEDS * SG_ENV_BUCKETS];    /* state ids, min-heap */
static int	env_hpos[SG_MAX_SEEDS * SG_ENV_BUCKETS];    /* state -> heap slot */
static int	env_hn;
static int	env_rev_first[SG_MAX_SEEDS];                /* links arriving here */
static int	env_rev_next[SG_ENV_MAX_LINKS];

/*
 * An indexed binary heap, because the state space is four times what it was
 * and the old O(n^2) pick-the-minimum scan would have been sixteen times the
 * work. With the reverse index below, a flood is O(links + states log states)
 * -- on lmctf01 (3171 seeds, 249089 links) that is faster than the flat flood
 * it replaces, which rescanned all 249089 links for every seed it settled.
 */
static void Heap_Up(int i)
{
	int s = env_heap[i];

	while (i > 0)
	{
		int p = (i - 1) / 2;

		if (env_dist[env_heap[p]] <= env_dist[s])
			break;
		env_heap[i] = env_heap[p];
		env_hpos[env_heap[i]] = i;
		i = p;
	}
	env_heap[i] = s;
	env_hpos[s] = i;
}

static void Heap_Down(int i)
{
	int s = env_heap[i];

	for (;;)
	{
		int c = i * 2 + 1;

		if (c >= env_hn)
			break;
		if (c + 1 < env_hn && env_dist[env_heap[c + 1]] < env_dist[env_heap[c]])
			c++;
		if (env_dist[env_heap[c]] >= env_dist[s])
			break;
		env_heap[i] = env_heap[c];
		env_hpos[env_heap[i]] = i;
		i = c;
	}
	env_heap[i] = s;
	env_hpos[s] = i;
}

static void Heap_Push(int s)        /* insert, or decrease-key in place */
{
	if (env_hpos[s] >= 0)
	{
		Heap_Up(env_hpos[s]);
		return;
	}
	env_heap[env_hn] = s;
	env_hpos[s] = env_hn;
	env_hn++;
	Heap_Up(env_hn - 1);
}

static int Heap_Pop(void)
{
	int top = env_heap[0];

	env_hn--;
	env_hpos[top] = -1;
	if (env_hn > 0)
	{
		env_heap[0] = env_heap[env_hn];
		env_hpos[env_heap[0]] = 0;
		Heap_Down(0);
	}
	return top;
}

/*
 * The flood proper. nb == SG_ENV_BUCKETS is the envelope-aware run;
 * nb == 1 collapses the state space back to one state per seed and skips both
 * transition terms, which is exactly the old flat flood and is what the
 * self-check below compares against.
 */
/*
 * Per-LINK futility, the finer chisel. Seed-level lessons taught at the
 * moat transitions poisoned lmctf01's whole tunnel: a stall at the water's
 * edge repriced every route through that ground, the argmin fell back to
 * land links that grind, which taught more futility -- a loop that turned
 * the map's only corridor radioactive (observed chokes at 1470 then
 * 3168/1923, the east and west moat entries). A failed WALK is
 * evidence about one link, not about the ground it starts from; only a
 * dead DOOR still teaches the seed, because a door blocks the body no
 * matter which link approaches it.
 */
static int sg_link_futile[SG_ENV_MAX_LINKS];

void SG_TeachLinkFutility(int link)
{
	if (link < 0 || link >= SG_ENV_MAX_LINKS)
		return;
	sg_link_futile[link] += 6000;
	if (sg_link_futile[link] > 60000)
		sg_link_futile[link] = 60000;
}


static void Futility_Decay(void)
{
	int i;

	for (i = 0; i < SG_MAX_SEEDS; i++)
		if (sg_futile[i])
		{
			sg_futile[i] -= 200;
			if (sg_futile[i] < 0)
				sg_futile[i] = 0;
		}
	for (i = 0; i < SG_ENV_MAX_LINKS; i++)
		if (sg_link_futile[i])
		{
			sg_link_futile[i] -= 400;
			if (sg_link_futile[i] < 0)
				sg_link_futile[i] = 0;
		}
}

/* per-seed destination surcharge, applied only while the flag fields
 * flood (the low-road pricing behind sg_shelfcost); zero otherwise */
static int		sg_shelf_pen[SG_MAX_SEEDS];

static void Field_FloodRun(rune_t *r, int *dist,
                           const int *sources, const int *source_cost,
                           int num_sources, int nb)
{
	int ns = r->hdr.num_seeds;
	int nl = r->hdr.num_links;
	int i, k;

	if (ns > SG_MAX_SEEDS)
	{
		sg_host.dprint("slipgate: rune has %d seeds, over the %d cap -- "
		           "flooding the first %d only\n", ns, SG_MAX_SEEDS, SG_MAX_SEEDS);
		ns = SG_MAX_SEEDS;
	}
	if (nl > SG_ENV_MAX_LINKS)
	{
		sg_host.dprint("slipgate: rune has %d links, over the %d cap -- "
		           "flooding the first %d only\n", nl, SG_ENV_MAX_LINKS,
		           SG_ENV_MAX_LINKS);
		nl = SG_ENV_MAX_LINKS;
	}

	for (i = 0; i < ns; i++)
		dist[i] = SG_FIELD_INF;
	for (i = 0; i < ns * nb; i++)
	{
		env_dist[i] = SG_FIELD_INF;
		env_done[i] = 0;
		env_hpos[i] = -1;
		env_head[i] = 0;
		env_slack[i] = 255;
	}
	env_hn = 0;

	/*
	 * The links that ARRIVE at each seed. The rune's own chain is keyed by
	 * .from (sg_arach.c:178-182) and this flood walks backwards, so the chain
	 * by .to is built here -- rebuilt every call rather than cached, because a
	 * cached index would outlive the TAG_LEVEL rune it indexes and the next
	 * level can hand back the same address.
	 */
	for (i = 0; i < ns; i++)
		env_rev_first[i] = -1;
	for (i = nl - 1; i >= 0; i--)
	{
		int t = r->links[i].to, f = r->links[i].from;

		env_rev_next[i] = -1;
		if (t < 0 || t >= ns || f < 0 || f >= ns ||
		    !Field_LinkAdmitted(&r->links[i]))
			continue;
		env_rev_next[i] = env_rev_first[t];
		env_rev_first[t] = i;
	}

	/*
	 * A source is where the route ENDS, so its state has no next link to line
	 * up with: slack 255 covers the whole circle (a byte-difference is at most
	 * 128) and the first link relaxed off a source pays no turn.
	 */
	for (i = 0; i < num_sources; i++)
	{
		if (sources[i] < 0 || sources[i] >= ns)
			continue;
		for (k = 0; k < nb; k++)
		{
			int id = sources[i] * nb + k;

			if (source_cost[i] < env_dist[id])
			{
				env_dist[id] = source_cost[i];
				env_head[id] = 0;
				env_slack[id] = 255;
				Heap_Push(id);
			}
		}
	}

	while (env_hn > 0)
	{
		int id = Heap_Pop();
		int u = id / nb;
		int ku = id - u * nb;
		int li;

		if (env_done[id])
			continue;
		env_done[id] = 1;
		/* settled in increasing cost, so the first band settled at a seed is
		 * that seed's exported value: the min over the four */
		if (env_dist[id] < dist[u])
			dist[u] = env_dist[id];

		for (li = env_rev_first[u]; li >= 0; li = env_rev_next[li])
		{
			const rune_link_t *b = &r->links[li];
			int x = b->from, base, kk;

			/*
			 * Taking b lands the body at u with b's exit speed, so b only
			 * continues into the band that speed falls in.
			 */
			if (nb > 1 && Env_Bucket(b->exit_speed) != ku)
				continue;

			base = env_dist[id] + Link_EffCost(b) + sg_futile[x]
			     + sg_link_futile[li] + sg_shelf_pen[u]
			     + SG_SnagRepairSeedSurcharge(x)
			     + SG_SnagRepairLinkSurcharge(li);
			if (nb > 1)
				base += Env_TurnCost(b->heading, env_head[id], env_slack[id]);

			for (kk = 0; kk < nb; kk++)
			{
				int tid = x * nb + kk;
				int c = base;

				if (nb > 1)
					c += Env_AccelCost(Env_BucketSpeed(kk), b->min_speed);
				if (c < env_dist[tid])
				{
					env_dist[tid] = c;
					env_head[tid] = b->heading;
					env_slack[tid] = b->heading_slack;
					Heap_Push(tid);
				}
			}
		}
	}
}

/*
 * FLOODCHECK: one line per level, on sg_debug, off the first flood the level
 * runs (Fields_Setup arms it). The envelope flood only ever ADDS cost, so it
 * must never reach a seed the flat flood cannot -- if it does, the state
 * machinery is wrong rather than merely differently priced, and the line says
 * BROKEN. Costs an extra flat flood, once, on a debug server only.
 */
static qboolean	sg_floodcheck_armed;
static int		env_check_dist[SG_MAX_SEEDS];

void Field_Flood(rune_t *r, int *dist,
                 const int *sources, const int *source_cost, int num_sources)
{
	Field_FloodRun(r, dist, sources, source_cost, num_sources, SG_ENV_BUCKETS);

	if (sg_floodcheck_armed && sg_cv.debug->value)
	{
		int i, ns = r->hdr.num_seeds, flat = 0, env = 0;

		sg_floodcheck_armed = false;
		if (ns > SG_MAX_SEEDS)
			ns = SG_MAX_SEEDS;
		Field_FloodRun(r, env_check_dist, sources, source_cost, num_sources, 1);
		for (i = 0; i < ns; i++)
		{
			if (env_check_dist[i] < SG_FIELD_INF)
				flat++;
			if (dist[i] < SG_FIELD_INF)
				env++;
		}
		sg_host.dprint("FLOODCHECK seeds=%d reachable_flat=%d reachable_env=%d%s\n",
		           ns, flat, env, env > flat ? " BROKEN" : "");
	}
}

#ifdef SG_FIELDS_TEST
void Fields_TestFloodFlat(rune_t *r, int *dist,
	const int *sources, const int *source_cost, int num_sources)
{
	Field_FloodRun(r, dist, sources, source_cost, num_sources, 1);
}
#endif

static int *Field_Alloc(rune_t *r)
{
	return sg_host.level_alloc(sizeof(int) * r->hdr.num_seeds);
}

static void Field_FromOne(rune_t *r, int *dist, int seed)
{
	int cost = 0;

	Field_Flood(r, dist, &seed, &cost, 1);
}

/* Rebuild the two static stand fields and every value derived directly from
 * them.  All storage is allocated by Fields_Setup; this routine is also used
 * on an action-capability edge and must never allocate. */
static void FlagFields_Build(rune_t *r)
{
	int t;

	if (sg_cv.shelfcost->value > 0.0f)
	{
		for (t = 0; t < 2; t++)
		{
			int fseed = t ? sg_fields.blue_flag_seed
			              : sg_fields.red_flag_seed;
			float *fo = r->seeds[fseed].origin;
			int fi, ns = r->hdr.num_seeds;

			if (ns > SG_MAX_SEEDS)
				ns = SG_MAX_SEEDS;
			for (fi = 0; fi < ns; fi++)
			{
				float dx = r->seeds[fi].origin[0] - fo[0];
				float dy = r->seeds[fi].origin[1] - fo[1];

				sg_shelf_pen[fi] = 0;
				if (dx * dx + dy * dy <= 350.0f * 350.0f &&
				    r->seeds[fi].origin[2] <= fo[2] - 96.0f)
					sg_shelf_pen[fi] = (int)
					    (sg_cv.shelfcost->value * 12000.0f);
			}
			Field_FromOne(r, t ? sg_fields.to_blue_flag
			                   : sg_fields.to_red_flag, fseed);
		}
		memset(sg_shelf_pen, 0, sizeof(sg_shelf_pen));
	}
	else
	{
		Field_FromOne(r, sg_fields.to_red_flag, sg_fields.red_flag_seed);
		Field_FromOne(r, sg_fields.to_blue_flag, sg_fields.blue_flag_seed);
	}

	/* Recompute the sub-stand cliff from the rebuilt home fields. */
	for (t = 0; t < 2; t++)
	{
		int enemy_seed = t ? sg_fields.red_flag_seed
		                   : sg_fields.blue_flag_seed;
		int *sf = t ? sg_fields.to_red_flag : sg_fields.to_blue_flag;
		float *fo = r->seeds[enemy_seed].origin;
		int si, best_plat = 0x7fffffff;

		for (si = 0; si < r->hdr.num_seeds; si++)
			sg_fields.shelf_cliff[t][si] = 0;
		for (si = 0; si < r->hdr.num_seeds; si++)
		{
			float dx = r->seeds[si].origin[0] - fo[0];
			float dy = r->seeds[si].origin[1] - fo[1];

			if (dx * dx + dy * dy > 350.0f * 350.0f)
				continue;
			if (r->seeds[si].origin[2] > fo[2] - 48.0f &&
			    sf[si] < best_plat)
				best_plat = sf[si];
		}
		if (best_plat == 0x7fffffff)
		{
			if (sg_cv.debug->value)
				sg_host.dprint("SHELF t=%d: no platform-level seed in radius\n", t);
			continue;
		}
		for (si = 0; si < r->hdr.num_seeds; si++)
		{
			float dx = r->seeds[si].origin[0] - fo[0];
			float dy = r->seeds[si].origin[1] - fo[1];

			if (dx * dx + dy * dy > 350.0f * 350.0f ||
			    r->seeds[si].origin[2] > fo[2] - 96.0f)
				continue;
			if (sf[si] > best_plat)
				sg_fields.shelf_cliff[t][si] = sf[si] - best_plat;
		}
		if (sg_cv.debug->value)
		{
			int n = 0, mx = 0, sub = 0;

			for (si = 0; si < r->hdr.num_seeds; si++)
			{
				float dx = r->seeds[si].origin[0] - fo[0];
				float dy = r->seeds[si].origin[1] - fo[1];

				if (dx * dx + dy * dy > 350.0f * 350.0f ||
				    r->seeds[si].origin[2] > fo[2] - 96.0f)
					continue;
				sub++;
				if (sg_fields.shelf_cliff[t][si] > 0)
				{
					n++;
					if (sg_fields.shelf_cliff[t][si] > mx)
						mx = sg_fields.shelf_cliff[t][si];
				}
			}
			sg_host.dprint("SHELF t=%d: %d sub-stand seeds, %d cliffed, max %d, best_plat %d\n",
			           t, sub, n, mx, best_plat);
		}
	}
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

/*
 * THE MEGA PADS (sg_megaworth), one field each.
 *
 * Built off the back of the health class rather than on a clock of its own:
 * item_health_mega carries the "item_health" prefix, so a mega going up or
 * down already moves SG_FC_HEALTH's signature and already calls Class_Build
 * here. One hook, no second cadence, no second scan of the edict list to
 * decide whether a rebuild is due.
 *
 * Off by default, and "off" means no flood at all -- mega_count stays 0, the
 * buffers stay untouched, and every consumer's first test fails.
 */
qboolean SG_MegaOn(void)
{
	return (sg_cv.megaworth->value > 0.0f) ? true : false;
}

static void Mega_Build(rune_t *r)
{
	edict_t *e;
	int i;

	sg_fields.mega_count = 0;
	if (!SG_MegaOn())
		return;

	for (i = 0; i < globals.num_edicts &&
	     sg_fields.mega_count < SG_MAX_MEGA; i++)
	{
		int k = sg_fields.mega_count, seed;

		e = &g_edicts[i];
		if (!e->inuse || !e->classname || !Caco_ItemBelievedUp(e))
			continue;           /* a taken mega is SOLID_NOT for 20 s */
		if (strcmp(e->classname, "item_health_mega") != 0)
			continue;
		seed = Caco_ItemBeliefSeed(r, e);
		if (seed < 0 || !sg_fields.to_mega[k])
			continue;
		sg_fields.mega_seed[k] = seed;
		sg_fields.mega_ent[k] = i;
		Field_FromOne(r, sg_fields.to_mega[k], seed);
		sg_fields.mega_count++;
	}
}

/* the on/off edge, so flipping the cvar mid-level does not wait for the next
 * health item to change state before the fields exist (or stop existing) */
static qboolean sg_mega_was;
static unsigned sg_field_belief_sig;

static void Class_Build(rune_t *r, int cls)
{
	int sources[256] = { 0 }, costs[256] = { 0 }, ents[256] = { 0 }, n = 0;
	edict_t *e;
	int i;

	for (i = 0; i < globals.num_edicts && n < 256; i++)
	{
		e = &g_edicts[i];
		if (!e->inuse || !e->classname)
			continue;
		if (!Class_Match(&field_classes[cls], e->classname))
			continue;
		if (Class_PerItem(cls))
		{
			if (!Caco_ItemBelievedRouteable(e))
				continue;
		}
		else if (!Caco_ItemBelievedUp(e))
			continue;
		sources[n] = Caco_ItemBeliefSeed(r, e);
		costs[n] = 0;
		ents[n] = i;        /* identity: which entity this slot prices */
		if (sources[n] >= 0)
			n++;
	}
	Field_Flood(r, sg_fields.item[cls], sources, costs, n);
	sg_fields.item_sig[cls] = Class_Signature(&field_classes[cls]);

	/* the mega rides the health class's rebuild: same entities, same edge */
	if (cls == SG_FC_HEALTH)
	{
		Mega_Build(r);
		sg_mega_was = SG_MegaOn();
	}

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
		sg_fields.per_item_ent[cls][i] = ents[i];
		if (sg_fields.per_item[cls][i])
			Field_FromOne(r, sg_fields.per_item[cls][i], sources[i]);
	}
	for (i = n; i < SG_MAX_PER_ITEM; i++)
	{
		sg_fields.per_item_seed[cls][i] = -1;
		sg_fields.per_item_ent[cls][i] = -1;
	}
	sg_fields.per_item_count[cls] = n;
}

#ifdef SG_FIELDS_TEST
#define SG_FIELDS_PRIVATE
#else
#define SG_FIELDS_PRIVATE static
#endif

SG_FIELDS_PRIVATE int Fields_DefensiveRoot(const rune_t *r,
	const unsigned char *plane)
{
	int best = -1;
	int si;

	if (!plane)
		return -1;
	for (si = 0; si < r->hdr.num_seeds; si++)
	{
		/* Sidecar validation already requires tombstone cells to be zero.
		 * Keep the field boundary defensive as well: a future caller must not
		 * be able to turn retained dead geometry into a learned field root. */
		if ((r->seeds[si].flags & RSF_TOMBSTONE) ||
		    (r->linked_seed && !r->linked_seed[si]) || plane[si] == 0)
			continue;
		if (best < 0 || plane[si] > plane[best])
			best = si;
	}
	return best;
}

#undef SG_FIELDS_PRIVATE

static void DefensiveFields_Build(rune_t *r)
{
	int t;

	for (t = 0; t < 2; t++)
	{
		int *own = t == 0 ? sg_fields.to_red_flag
		                  : sg_fields.to_blue_flag;

		if (sg_fields.post_seed[t] >= 0)
			Field_FromOne(r, sg_fields.to_post[t], sg_fields.post_seed[t]);
		else
			memcpy(sg_fields.to_post[t], own,
			       sizeof(int) * r->hdr.num_seeds);
		if (sg_fields.icept_seed[t] >= 0)
			Field_FromOne(r, sg_fields.to_icept[t], sg_fields.icept_seed[t]);
		else
			memcpy(sg_fields.to_icept[t], own,
			       sizeof(int) * r->hdr.num_seeds);
	}
}

/* Re-select the lane against the rebuilt home field.  Merely re-flooding the
 * previous lane seed is insufficient: the 1.5--4 second approach band itself
 * changes when a permanent action enters or leaves the topology. */
static void LaneFields_Build(rune_t *r)
{
	int t, li, j;

	for (t = 0; t < 2; t++)
	{
		int *own = t == 0 ? sg_fields.to_red_flag
		                  : sg_fields.to_blue_flag;
		int flag_seed = t == 0 ? sg_fields.red_flag_seed
		                       : sg_fields.blue_flag_seed;
		int appr[48], na = 0;
		int best = -1;
		float bestscore = -1.0f;

		for (li = 0; li < r->hdr.num_seeds && na < 48; li += 7)
			if (own[li] < SG_FIELD_INF &&
			    own[li] >= 1500 && own[li] <= 4000)
				appr[na++] = li;

		for (li = 0; li < r->hdr.num_seeds; li++)
		{
			float score = 0.0f;
			vec3_t eye;

			if (own[li] >= SG_FIELD_INF || own[li] > 2000)
				continue;
			VectorCopy(r->seeds[li].origin, eye);
			eye[2] += 22.0f;
			for (j = 0; j < na; j++)
			{
				vec3_t thr;
				trace_t ltr;

				VectorCopy(r->seeds[appr[j]].origin, thr);
				thr[2] += 22.0f;
				ltr = sg_host.trace(eye, NULL, NULL, thr, NULL,
				                   MASK_SOLID);
				if (ltr.fraction >= 1.0f)
					score += 1.0f;
			}
			{
				vec3_t fthr;
				trace_t ftr;

				VectorCopy(r->seeds[flag_seed].origin, fthr);
				fthr[2] += 22.0f;
				ftr = sg_host.trace(eye, NULL, NULL, fthr, NULL,
				                   MASK_SOLID);
				if (ftr.fraction < 1.0f)
					continue;
			}
			if (score > bestscore)
			{
				bestscore = score;
				best = li;
			}
		}
		if (best >= 0 && na > 0 &&
		    bestscore / (float)na < 0.40f)
			best = -1;
		sg_fields.lane_seed[t] = best;
		if (best >= 0)
		{
			Field_FromOne(r, sg_fields.to_lane[t], best);
			sg_host.dprint("slipgate: rail lane team%d seed=%d covers %.0f/%d approach seeds\n",
			           t + 1, best, bestscore, na);
		}
		else
			memcpy(sg_fields.to_lane[t], own,
			       sizeof(int) * r->hdr.num_seeds);
	}
}

qboolean Fields_ActionTopologyRefresh(rune_t *r)
{
	qboolean hook_now = Field_HookCapability();
	int i;

	if (!r || hook_now == sg_fields.hook_admitted)
		return false;
	sg_fields.hook_admitted = hook_now;
	sg_fields.action_topology_epoch++;
	if (sg_fields.action_topology_epoch == 0)
		sg_fields.action_topology_epoch = 1;

	/* Static/cache roots cross the edge before any belief projection can use
	 * them.  Dynamic belief fields are rebuilt by the normal Fields_Refresh
	 * later in the same SG frame; pending prevents its cadence guard from
	 * returning early. */
	FlagFields_Build(r);
	DefensiveFields_Build(r);
	LaneFields_Build(r);
	for (i = 0; i < SG_FIELD_CLASSES; i++)
		Class_Build(r, i);
	sg_field_belief_sig = Caco_ItemBeliefSig();
	sg_fields.action_topology_pending = true;
	if (sg_cv.debug->value)
		sg_host.dprint("slipgate: field action topology epoch=%u hook=%d\n",
		           sg_fields.action_topology_epoch,
		           sg_fields.hook_admitted ? 1 : 0);
	return true;
}

qboolean Fields_Setup(rune_t *r, const sg_field_setup_inputs_t *inputs)
{
	edict_t *rf, *bf;
	cvar_t *gamedir;
	const char *game_directory;
	int i;

	/* the next flood is this level's first: it carries the self-check */
	sg_floodcheck_armed = true;
	SG_RopecostRefresh();
	sg_fields.hook_admitted = Field_HookCapability();
	sg_fields.action_topology_pending = false;
	sg_fields.action_topology_epoch++;
	if (sg_fields.action_topology_epoch == 0)
		 sg_fields.action_topology_epoch = 1;
	/* A published artifact requires one exact RUNE-bound snag declaration;
	 * zero repairs is explicit in that file, never inferred from ENOENT.  Unit
	 * fixtures that construct only an in-memory graph have no artifact identity
	 * and therefore do not perform level-file I/O. */
	if (r->artifact.identity.map_name[0])
	{
		gamedir = sg_host.cvar ? sg_host.cvar("gamedir", "", 0) : NULL;
		game_directory = gamedir && gamedir->string && gamedir->string[0]
			? gamedir->string : ".";
		if (!SG_SnagRepairLoadForLevel(r, game_directory))
		{
			if (sg_host.dprint)
				sg_host.dprint("slipgate: snag declaration missing or invalid "
				               "for map %s; fields rejected\n",
				               r->artifact.identity.map_name);
			return false;
		}
	}
	else
		SG_SnagRepairClear();

	rf = SG_FlagStand(CTF_TEAM_RED, true);
	bf = SG_FlagStand(CTF_TEAM_BLUE, true);
	if (!rf || !bf)
		return false;

	sg_fields.red_flag_seed = Rune_NearestSeed(r, rf->s.origin);
	sg_fields.blue_flag_seed = Rune_NearestSeed(r, bf->s.origin);
	if (sg_fields.red_flag_seed < 0 || sg_fields.blue_flag_seed < 0)
		return false;

	sg_fields.to_red_flag = Field_Alloc(r);
	sg_fields.to_blue_flag = Field_Alloc(r);


	if (sg_cv.shelfcost->value > 0.0f)
	{
		int t;

		for (t = 0; t < 2; t++)
		{
			int fseed = t ? sg_fields.blue_flag_seed
			              : sg_fields.red_flag_seed;
			float *fo = r->seeds[fseed].origin;
			int fi, ns = r->hdr.num_seeds;

			if (ns > SG_MAX_SEEDS)
				ns = SG_MAX_SEEDS;
			for (fi = 0; fi < ns; fi++)
			{
				float dx = r->seeds[fi].origin[0] - fo[0];
				float dy = r->seeds[fi].origin[1] - fo[1];

				sg_shelf_pen[fi] = 0;
				if (dx * dx + dy * dy <= 350.0f * 350.0f &&
				    r->seeds[fi].origin[2] <= fo[2] - 96.0f)
					sg_shelf_pen[fi] = (int)
					    (sg_cv.shelfcost->value * 12000.0f);
			}
			Field_FromOne(r, t ? sg_fields.to_blue_flag
			                   : sg_fields.to_red_flag, fseed);
		}
		memset(sg_shelf_pen, 0, sizeof(sg_shelf_pen));
	}
	else
	{
		Field_FromOne(r, sg_fields.to_red_flag, sg_fields.red_flag_seed);
		Field_FromOne(r, sg_fields.to_blue_flag, sg_fields.blue_flag_seed);
	}

	/*
	 * THE SUB-STAND SHELF (steal-genesis study, 2026-08-06). mactf06
	 * film: 101 of 280 close approaches terminated on the floor 141u
	 * BELOW the flag platform -- 91% died there in ~1.2s, zero steals
	 * ever -- because RL_DROP prices every drop at +150 while the climb
	 * back costs ~1275. Per team, for every seed within 350u horizontal
	 * of the ENEMY stand and 96u+ below it, store the measured cliff:
	 * this seed's static cost to that flag minus the best platform-level
	 * seed's inside the same radius. Flat stands store nothing -- lmctf22
	 * and lmctf44 are the built-in null arms. Consumed by the ATTACK
	 * pricing behind sg_shelfcost (default 0).
	 */
	{
		int t;

		for (t = 0; t < 2; t++)
		{
			int enemy_seed = t ? sg_fields.red_flag_seed
			                   : sg_fields.blue_flag_seed;
			int *sf = t ? sg_fields.to_red_flag : sg_fields.to_blue_flag;
			float *fo = r->seeds[enemy_seed].origin;
			int si, best_plat = 0x7fffffff;

			sg_fields.shelf_cliff[t] = Field_Alloc(r);
			for (si = 0; si < r->hdr.num_seeds; si++)
				sg_fields.shelf_cliff[t][si] = 0;

			for (si = 0; si < r->hdr.num_seeds; si++)
			{
				float dx = r->seeds[si].origin[0] - fo[0];
				float dy = r->seeds[si].origin[1] - fo[1];

				if (dx * dx + dy * dy > 350.0f * 350.0f)
					continue;
				if (r->seeds[si].origin[2] > fo[2] - 48.0f &&
				    sf[si] < best_plat)
					best_plat = sf[si];
			}
			if (best_plat == 0x7fffffff)
			{
				if (sg_cv.debug->value)
					sg_host.dprint("SHELF t=%d: no platform-level seed in radius\n", t);
				continue;
			}
			for (si = 0; si < r->hdr.num_seeds; si++)
			{
				float dx = r->seeds[si].origin[0] - fo[0];
				float dy = r->seeds[si].origin[1] - fo[1];

				if (dx * dx + dy * dy > 350.0f * 350.0f)
					continue;
				if (r->seeds[si].origin[2] > fo[2] - 96.0f)
					continue;
				if (sf[si] > best_plat)
					sg_fields.shelf_cliff[t][si] = sf[si] - best_plat;
			}
			{
				int n = 0, mx = 0, sub = 0;

				for (si = 0; si < r->hdr.num_seeds; si++)
				{
					float dx = r->seeds[si].origin[0] - fo[0];
					float dy = r->seeds[si].origin[1] - fo[1];

					if (dx * dx + dy * dy > 350.0f * 350.0f)
						continue;
					if (r->seeds[si].origin[2] <= fo[2] - 96.0f)
					{
						sub++;
						if (sg_fields.shelf_cliff[t][si] > 0)
						{
							n++;
							if (sg_fields.shelf_cliff[t][si] > mx)
								mx = sg_fields.shelf_cliff[t][si];
						}
					}
				}
				if (sg_cv.debug->value)
					sg_host.dprint("SHELF t=%d: %d sub-stand seeds, %d cliffed, max %d, best_plat %d\n",
					           t, sub, n, mx, best_plat);
			}
		}
	}

	{
		int si, nr = 0, nb = 0;

		for (si = 0; si < r->hdr.num_seeds; si++)
		{
			if (sg_fields.to_red_flag[si] < SG_FIELD_INF) nr++;
			if (sg_fields.to_blue_flag[si] < SG_FIELD_INF) nb++;
		}
		sg_host.dprint("slipgate: field coverage red %d/%d blue %d/%d (flag seeds %d, %d)\n",
		           nr, r->hdr.num_seeds, nb, r->hdr.num_seeds,
		           sg_fields.red_flag_seed, sg_fields.blue_flag_seed);
	}

	/*
	 * Learned defensive fields (.dpo planes): flood from the
	 * corpus's top post seed and top intercept seed per team. The candidate
	 * planes arrive explicitly in authenticated artifact order; this setup
	 * never reads or
	 * publishes the live sidecar globals. Missing plane -> the team's own flag
	 * field, i.e. exactly today's behavior.
	 */
	{
		int t;

		for (t = 0; t < 2; t++)
		{
			int *own = t == 0 ? sg_fields.to_red_flag
			                  : sg_fields.to_blue_flag;
			const unsigned char *pp = inputs ?
			    inputs->dpo[SG_DPO_POST_RED + t] : NULL;
			const unsigned char *ip = inputs ?
			    inputs->dpo[SG_DPO_INTERCEPT_RED + t] : NULL;
			int best = Fields_DefensiveRoot(r, pp);

			sg_fields.post_seed[t] = best;
			sg_fields.to_post[t] = Field_Alloc(r);
			if (best >= 0)
				Field_FromOne(r, sg_fields.to_post[t], best);
			else
				memcpy(sg_fields.to_post[t], own,
				       sizeof(int) * r->hdr.num_seeds);

			sg_fields.to_icept[t] = Field_Alloc(r);
			best = Fields_DefensiveRoot(r, ip);
			sg_fields.icept_seed[t] = best;
			if (best >= 0)
				Field_FromOne(r, sg_fields.to_icept[t], best);
			else
				memcpy(sg_fields.to_icept[t], own,
				       sizeof(int) * r->hdr.num_seeds);
		}
	}

	/*
	 * THE RAIL LANE (sg_raillane): "rails
	 * guard sight lines; I held them so well the enemy couldn't reach
	 * our base"). Per team, once per level: among candidate posts near
	 * the stand, pick the seed that SEES the most of the approach
	 * corridor -- the band of seeds an attacker must cross on its way
	 * in. Geometry, not corpus dwell: the .dpo posts failed because
	 * they pulled defenders off the stand; the lane is defined by
	 * covering the way TO the stand.
	 */
	{
		int t, li, j;

		for (t = 0; t < 2; t++)
		{
			int *own = t == 0 ? sg_fields.to_red_flag
			                  : sg_fields.to_blue_flag;
			int flag_seed = t == 0 ? sg_fields.red_flag_seed
			                       : sg_fields.blue_flag_seed;
			int appr[48], na = 0;
			int best = -1;
			float bestscore = -1.0f;

			/* the corridor: sample seeds 1.5-4s out on the home field */
			for (li = 0; li < r->hdr.num_seeds && na < 48; li += 7)
				if (own[li] < SG_FIELD_INF &&
				    own[li] >= 1500 && own[li] <= 4000)
					appr[na++] = li;

			/* candidates: seeds inside 2s of the stand */
			for (li = 0; li < r->hdr.num_seeds; li++)
			{
				float score = 0.0f;
				vec3_t eye;

				if (own[li] >= SG_FIELD_INF || own[li] > 2000)
					continue;
				VectorCopy(r->seeds[li].origin, eye);
				eye[2] += 22.0f;
				for (j = 0; j < na; j++)
				{
					vec3_t thr;
					trace_t ltr;

					VectorCopy(r->seeds[appr[j]].origin, thr);
					thr[2] += 22.0f;
					ltr = sg_host.trace(eye, NULL, NULL, thr, NULL,
					               MASK_SOLID);
					if (ltr.fraction >= 1.0f)
						score += 1.0f;
				}
				/* seeing the stand itself is required: a lane that
				 * cannot also watch the flag is a camp, not a guard */
				{
					vec3_t fthr;
					trace_t ftr;

					VectorCopy(r->seeds[flag_seed].origin, fthr);
					fthr[2] += 22.0f;
					ftr = sg_host.trace(eye, NULL, NULL, fthr, NULL,
					               MASK_SOLID);
					if (ftr.fraction < 1.0f)
						continue;
				}
				if (score > bestscore)
				{
					bestscore = score;
					best = li;
				}
			}
			sg_fields.to_lane[t] = Field_Alloc(r);
			/* A lane that cannot see 40% of the corridor is not a lane.
			 * Sixty-percent coverage was effective on mactf06, while 25%
			 * coverage on lmctf22 produced no useful effect. */
			if (best >= 0 && na > 0 &&
			    bestscore / (float)na < 0.40f)
				best = -1;
			sg_fields.lane_seed[t] = best;
			if (best >= 0)
			{
				Field_FromOne(r, sg_fields.to_lane[t], best);
				sg_host.dprint("slipgate: rail lane team%d seed=%d covers %.0f/%d approach seeds\n",
				           t + 1, best, bestscore, na);
			}
			else
				memcpy(sg_fields.to_lane[t], own,
				       sizeof(int) * r->hdr.num_seeds);
		}
	}

	/* Dynamic flag position is a belief, so each team owns a separate row. */
	for (i = 0; i < 2; i++)
	{
		sg_fields.to_flag_now[i][0] = Field_Alloc(r);
		sg_fields.to_flag_now[i][1] = Field_Alloc(r);
		memcpy(sg_fields.to_flag_now[i][0], sg_fields.to_red_flag,
		       sizeof(int) * r->hdr.num_seeds);
		memcpy(sg_fields.to_flag_now[i][1], sg_fields.to_blue_flag,
		       sizeof(int) * r->hdr.num_seeds);
	}

	/*
	 * Every pointer in sg_fields was TAG_LEVEL and is dangling by now, so all
	 * of them are (re)allocated here rather than lazily -- a "if (!ptr)" test
	 * would see the previous level's freed address and skip.
	 */
	/*
	 * The mega buffers are allocated whatever the cvar says, for the same
	 * reason every other pointer here is: they were TAG_LEVEL and are
	 * dangling by now, and a lazy "allocate when the cvar turns on" would
	 * read the previous level's freed address. Allocation is not behaviour
	 * -- with the cvar off nothing is ever flooded into them and mega_count
	 * stays 0, so the surface is the byte it was before this existed. Ahead
	 * of the class loop because Class_Build(SG_FC_HEALTH) floods them.
	 */
	for (i = 0; i < SG_MAX_MEGA; i++)
	{
		sg_fields.to_mega[i] = Field_Alloc(r);
		sg_fields.mega_seed[i] = -1;
		sg_fields.mega_ent[i] = -1;
	}
	sg_fields.mega_count = 0;

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

	memset(sg_futile, 0, sizeof(sg_futile));
	memset(sg_link_futile, 0, sizeof(sg_link_futile));
	sg_futile_last_seed = -1;
	sg_futile_streak = 0;
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
	qboolean topology_changed;
	qboolean cadence_due;
	int i;

	(void)Fields_ActionTopologyRefresh(r);
	topology_changed = sg_fields.action_topology_pending;
	cadence_due = (level.time >= sg_fields.next_refresh);
	if (!topology_changed && !cadence_due)
		return;
	if (cadence_due)
	{
		sg_fields.next_refresh = level.time + 1.0f;
		Futility_Decay();
	}

	{
		/* the entity walk sees an item going up or down, but a rune moves
		 * WHILE up every 30s (Rune_Think, g_runes.c:338) -- the belief
		 * signature is what notices that */
		unsigned bsig = Caco_ItemBeliefSig();

		for (i = 0; i < SG_FIELD_CLASSES; i++)
			if (Class_Signature(&field_classes[i]) != sg_fields.item_sig[i]
			    || (Class_PerItem(i) && bsig != sg_field_belief_sig))
				Class_Build(r, i);
		sg_field_belief_sig = bsig;
	}

	/* the cvar's own edge: turning sg_megaworth on mid-level must not wait
	 * for the next health item to change state before there is a field, and
	 * turning it off must drop the pull on the spot */
	if (SG_MegaOn() != sg_mega_was)
	{
		Mega_Build(r);
		sg_mega_was = SG_MegaOn();
	}

	/* flag positions per CACO: seed the "now" field wherever belief puts it */
	for (i = 0; i < 2; i++)
	{
		int fi;

		for (fi = 0; fi < 2; fi++)
		{
			sg_belief_flag_t *bf = &sg_caco_team_belief.flag[i][fi];
			int *fld = sg_fields.to_flag_now[i][fi];
			int home = fi ? sg_fields.blue_flag_seed : sg_fields.red_flag_seed;
			int seed, cost = 0;

			if (bf->state == SG_FLAG_HOME)
				seed = home;
			else if (bf->where_seed >= 0)
				seed = bf->where_seed;
			else
				/* Astray and unseen: sweep toward the thief's home stand. */
				seed = fi ? sg_fields.red_flag_seed : sg_fields.blue_flag_seed;
			Field_Flood(r, fld, &seed, &cost, 1);
		}
	}

	/* our-carrier support fields, one per team -- flooded from a point
	 * AHEAD of the carrier, not from its heels. The field refreshes once
	 * a second and a rope-speed carrier outruns its own past: escorts
	 * routed to the believed position screened nothing (0-11% of carry
	 * seconds with an escort inside 700). The flood seed
	 * now walks three hops down the carrier's homeward gradient first,
	 * so the escort converges on where the carrier is GOING and the
	 * screen forms on the path, not the wake. */
	for (i = 0; i < 2; i++)
	{
		sg_belief_carrier_t *c = &sg_caco_team_belief.carrier[i];
		int cost = 0;

		if (c->client >= 0 && c->seed >= 0 &&
		    c->seed < r->hdr.num_seeds)
		{
			int *home = i ? sg_fields.to_blue_flag
			              : sg_fields.to_red_flag;
			int seed = c->seed, hop;

			for (hop = 0; hop < 3; hop++)
			{
				int li2, best = -1, bv = home[seed];

				for (li2 = r->first_link[seed]; li2 >= 0;
				     li2 = r->next_link[li2])
				{
					rune_link_t *l2 = &r->links[li2];

					if (Field_LinkAdmitted(l2) &&
					    home[l2->to] < bv)
					{
						bv = home[l2->to];
						best = l2->to;
					}
				}
				if (best < 0)
					break;
				seed = best;
			}
			Field_Flood(r, sg_fields.our_carrier[i], &seed, &cost, 1);
			sg_fields.our_carrier_valid[i] = true;
		}
		else
		{
			int seed;

			for (seed = 0; seed < r->hdr.num_seeds; seed++)
				sg_fields.our_carrier[i][seed] = SG_FIELD_INF;
			sg_fields.our_carrier_valid[i] = false;
		}
	}
	sg_fields.action_topology_pending = false;
}
