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
 * NEAREST source seed. Sources carry an initial cost so a projected position
 * can be seeded with its age.
 *
 * The flood runs BACKWARDS: a link is stored from -> to, and the field asks
 * what it costs to get from a seed TO the source, so settling a seed u relaxes
 * every link that ARRIVES at u and writes the cost of its .from.
 */
/*
 * What a link REALLY costs the body, not just what the traversal took the
 * phantom. The proofs fired hooks from the seed at rest and walked drops
 * onto their recorded lip line, so the body does too -- and that approach
 * time is part of the price. A flood that ignores it systematically
 * over-prefers hooks against runs and swims (smap05: every route funneled
 * onto ropes).
 *
 * These stay exactly as they were, and they are NOT the envelope: they price
 * the entry RITUAL -- walking into position, braking, raising a weapon and
 * paying health -- which the body pays whatever state it shows up in. The
 * velocity mismatch is priced separately, below.
 */
/*
 * FUTILITY -- the surface's answer to a wall the body cannot solve.
 *
 * A proven link is a claim the ORACLE made good on; the runtime body is a
 * worse navigator, and on lmctf01 one bare corner near each side's exit
 * corridor defeats the feeler fan forever: the bot grinds the wall, the
 * stagnation watchdog shelves the local fan -- and the FIELD, which never
 * heard, funnels the bot straight back in from every neighboring seed
 * (iter 44: Gate, 90 shelve events at seed 327, attack floors pinned at
 * 21.5s all game). Shelves are per-bot and local; the funnel is global.
 * So the watchdog now also teaches the seed itself: a decaying surcharge
 * on every route THROUGH it, charged in the flood, so within a second the
 * whole map reprices and the fleet approaches by another corridor. Decay
 * (200ms/s) retries the corridor eventually; re-sticking re-teaches.
 */
static int sg_futile[SG_MAX_SEEDS];

void SG_TeachFutility(int seed)
{
	static int last_seed = -1;
	static int streak;
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
	if (seed == last_seed)
		streak = (streak < 5) ? streak + 1 : 5;
	else
		streak = 1;
	last_seed = seed;
	amount = 3000 * streak;
	sg_futile[seed] += amount;
	if (sg_futile[seed] > 60000)
		sg_futile[seed] = 60000;
}

static int Link_EffCost(const rune_link_t *l)
{
	switch (l->action)
	{
	/*
	 * 1000, not 400: the rope's REAL ritual is a standing aim frame (the
	 * body halts, phase 1 owns the view), the fire, and a landing brake --
	 * wave 57's carrier trace shows ~2-3s of wall clock per rope against
	 * cost_ms figures in the hundreds, and a carrier that chained ropes
	 * covered 4s of field in 70s of flailing while a run would have flown.
	 * Underpricing the ritual made the flood chain hooks where legs win.
	 */
	case RL_HOOK:       return l->cost_ms + 1000;
	case RL_DROP:       return l->cost_ms + 150;    /* align the lip line */
	case RL_ROCKETJUMP: return l->cost_ms + 900;    /* raise RL + aim + pay */
	default:            return l->cost_ms;
	}
}

/* ------------------------------------------------------ envelope transitions
 *
 * A rune link is conditioned on the state it is entered in: min_speed (stored
 * as speed/4), heading (0-255 around the circle) and heading_slack, and it
 * reports the exit_speed the traversal ended with (sg_rune.h:85-95). A flat
 * flood throws all of that away and prices a route as if the body could enter
 * any link from any state for free, which it cannot: arriving at 40 u/s in
 * front of a link that needs 400 costs the time to build 360 u/s, and arriving
 * pointed the wrong way costs the time to swing the BODY round (the view
 * re-aims within a frame, the velocity does not).
 *
 * So the price of taking link B after link A is
 *
 *     Link_EffCost(B)                             the ritual, as before
 *   + Env_AccelCost(A.exit_speed, B.min_speed)    build the speed B needs
 *   + Env_TurnCost(A.heading, B.heading, B.heading_slack)
 *
 * which is a cost on the PAIR, not on the link -- the line graph, 250k nodes
 * and every in/out pair of a seed an edge. That does not fit in a field
 * rebuilt every second, so the predecessor is remembered only to the precision
 * that changes the answer: which of four speed bands it arrived in.
 *
 *   state       (seed, bucket)   bucket = exit_speed / 64, i.e. stored
 *               0-63 / 64-127 / 128-191 / 192-255 == 0-252 / 256-508 /
 *               512-764 / 768-1020 u/s. Four states per seed, and the
 *               representative speed of a bucket is its midpoint
 *               (31/95/159/223 stored = 124/380/636/892 u/s).
 *   value       cost from that seed to the source given the body arrives
 *               there in that speed band.
 *
 * The speed term is charged when the state is written (the arrival band is the
 * state's own key). The heading term needs BOTH headings, and the arriving
 * link's is not in the key -- so each settled state also remembers the heading
 * and slack of the link its plan leaves on, and the turn is charged when a
 * predecessor reads it, where both headings are concrete. That is exact for
 * the plan the state holds; the approximation is that the state chose that
 * plan without knowing which heading would arrive. Costs stay non-negative, so
 * Dijkstra is still Dijkstra and no seed becomes reachable that a flat flood
 * could not reach.
 *
 * The field a consumer reads is unchanged: one int per seed, the MIN over the
 * four buckets -- the best band to arrive in is the field's own business.
 */

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

/*
 * Time to build the speed a link's entry envelope demands.
 *
 * PM_Accelerate adds accelspeed = accel * frametime * wishspeed along wishdir;
 * on the ground accel is 10 and pm_maxspeed clamps wishspeed to 300, so the
 * body gains 10 * 300 = 3000 u/s of speed per second of running while it is
 * below the cap (the body's own derivation of this, read off the engine, is at
 * sg_arach.c:636-650 -- pmove.c itself is not in this tree). One u/s therefore
 * costs 1000/3000 ms, and one STORED unit is 4 u/s, so a stored unit costs
 * 4/3 ms.
 *
 * FITTED in one respect: the real gain tapers as addspeed closes on wishspeed,
 * and above 300 only the strafe keeps it climbing at a fraction of this. This
 * charges the full rate the whole way, so it UNDER-prices the top of the
 * range -- deliberately, because over-pricing it would push routes back onto
 * the ropes that the surcharges above exist to hold them off.
 */
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
 * the map's only corridor radioactive (waves 52-53, chokes at 1470 then
 * 3168/1923, the east and west moat entries in turn). A failed WALK is
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

static void Field_FloodRun(rune_t *r, int *dist,
                           const int *sources, const int *source_cost,
                           int num_sources, int nb)
{
	int ns = r->hdr.num_seeds;
	int nl = r->hdr.num_links;
	int i, k;

	if (ns > SG_MAX_SEEDS)
	{
		gi.dprintf("slipgate: rune has %d seeds, over the %d cap -- "
		           "flooding the first %d only\n", ns, SG_MAX_SEEDS, SG_MAX_SEEDS);
		ns = SG_MAX_SEEDS;
	}
	if (nl > SG_ENV_MAX_LINKS)
	{
		gi.dprintf("slipgate: rune has %d links, over the %d cap -- "
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
		if (t < 0 || t >= ns || f < 0 || f >= ns)
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
			     + sg_link_futile[li];
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

	if (sg_floodcheck_armed && gi.cvar("sg_debug", "0", 0)->value)
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
		gi.dprintf("FLOODCHECK seeds=%d reachable_flat=%d reachable_env=%d%s\n",
		           ns, flat, env, env > flat ? " BROKEN" : "");
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
	int sources[256] = { 0 }, costs[256] = { 0 }, ents[256] = { 0 }, n = 0;
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
		ents[n] = i;        /* identity: which entity this slot prices */
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

qboolean Fields_Setup(rune_t *r)
{
	edict_t *rf, *bf;
	int i;

	/* the next flood is this level's first: it carries the self-check */
	sg_floodcheck_armed = true;

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

	/*
	 * Learned defensive fields (.dpo planes, wave 307+): flood from the
	 * corpus's top post seed and top intercept seed per team. Missing
	 * plane -> the team's own flag field, i.e. exactly today's behavior.
	 */
	{
		int t, i;

		for (t = 0; t < 2; t++)
		{
			int *own = t == 0 ? sg_fields.to_red_flag
			                  : sg_fields.to_blue_flag;
			unsigned char *pp = SG_DefPlane(1, t);
			unsigned char *ip = SG_DefPlane(0, t);
			int best;

			sg_fields.to_post[t] = Field_Alloc(r);
			best = -1;
			if (pp)
				for (i = 0; i < r->hdr.num_seeds; i++)
					if (best < 0 || pp[i] > pp[best]) best = i;
			if (best >= 0 && pp[best] > 0)
				Field_FromOne(r, sg_fields.to_post[t], best);
			else
				memcpy(sg_fields.to_post[t], own,
				       sizeof(int) * r->hdr.num_seeds);

			sg_fields.to_icept[t] = Field_Alloc(r);
			best = -1;
			if (ip)
				for (i = 0; i < r->hdr.num_seeds; i++)
					if (best < 0 || ip[i] > ip[best]) best = i;
			if (best >= 0 && ip[best] > 0)
				Field_FromOne(r, sg_fields.to_icept[t], best);
			else
				memcpy(sg_fields.to_icept[t], own,
				       sizeof(int) * r->hdr.num_seeds);
		}
	}

	/*
	 * THE RAIL LANE (sg_raillane, wave 345 -- the owner's craft: "rails
	 * guard sight lines; I held them so well the enemy couldn't reach
	 * our base"). Per team, once per level: among candidate posts near
	 * the stand, pick the seed that SEES the most of the approach
	 * corridor -- the band of seeds an attacker must cross on its way
	 * in. Geometry, not corpus dwell: the .dpo posts failed because
	 * they pulled defenders off the stand; the lane is defined by
	 * covering the way TO the stand.
	 */
	{
		int t, i, j;

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
			for (i = 0; i < r->hdr.num_seeds && na < 48; i += 7)
				if (own[i] < SG_FIELD_INF &&
				    own[i] >= 1500 && own[i] <= 4000)
					appr[na++] = i;

			/* candidates: seeds inside 2s of the stand */
			for (i = 0; i < r->hdr.num_seeds; i++)
			{
				float score = 0.0f;
				vec3_t eye;

				if (own[i] >= SG_FIELD_INF || own[i] > 2000)
					continue;
				VectorCopy(r->seeds[i].origin, eye);
				eye[2] += 22.0f;
				for (j = 0; j < na; j++)
				{
					vec3_t thr;
					trace_t ltr;

					VectorCopy(r->seeds[appr[j]].origin, thr);
					thr[2] += 22.0f;
					ltr = gi.trace(eye, NULL, NULL, thr, NULL,
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
					ftr = gi.trace(eye, NULL, NULL, fthr, NULL,
					               MASK_SOLID);
					if (ftr.fraction < 1.0f)
						continue;
				}
				if (score > bestscore)
				{
					bestscore = score;
					best = i;
				}
			}
			sg_fields.to_lane[t] = Field_Alloc(r);
			if (best >= 0)
			{
				Field_FromOne(r, sg_fields.to_lane[t], best);
				gi.dprintf("slipgate: rail lane team%d seed=%d covers %.0f/%d approach seeds\n",
				           t + 1, best, bestscore, na);
			}
			else
				memcpy(sg_fields.to_lane[t], own,
				       sizeof(int) * r->hdr.num_seeds);
		}
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

	memset(sg_futile, 0, sizeof(sg_futile));
	memset(sg_link_futile, 0, sizeof(sg_link_futile));
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
	Futility_Decay();

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

		if (bf->state == SG_FLAG_HOME)
			seed = home;
		else if (bf->where_seed >= 0)
			seed = bf->where_seed;
		else
			/*
			 * Astray and never sighted. The old fallback said "home" --
			 * which sent every RECOVER bot to squat its own EMPTY stand
			 * while the thief ran the flag the other way (campaign 1,
			 * lmctf01 g1: role=3 parked at the vacant base all game;
			 * both teams did it; the standoff never broke and no game
			 * has ever seen a capture). The game broadcast WHO took it
			 * the moment it happened; a taken flag is travelling to the
			 * thief's stand, so that is where the hunt begins -- the
			 * route there sweeps the drop case on the way.
			 */
			seed = i ? sg_fields.red_flag_seed : sg_fields.blue_flag_seed;
		Field_Flood(r, fld, &seed, &cost, 1);
	}

	/* our-carrier support fields, one per team -- flooded from a point
	 * AHEAD of the carrier, not from its heels. The field refreshes once
	 * a second and a rope-speed carrier outruns its own past: escorts
	 * routed to the believed position screened nothing (waves 111-112:
	 * 0-11% of carry seconds with an escort inside 700). The flood seed
	 * now walks three hops down the carrier's homeward gradient first,
	 * so the escort converges on where the carrier is GOING and the
	 * screen forms on the path, not the wake. */
	for (i = 0; i < 2; i++)
	{
		sg_belief_carrier_t *c = &sg_caco_team_belief.carrier[i];
		int cost = 0;

		if (c->client >= 0 && c->seed >= 0)
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

					if (home[l2->to] < bv)
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
	}
}
