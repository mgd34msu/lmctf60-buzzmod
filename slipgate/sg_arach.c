/*
 * sg_arach.c -- ARACHNOTRON: the brain with legs. First walking cut.
 *
 * This is deliberately the minimum that PLAYS: load the rune, flood one
 * cost field per flag, spawn a real client, and every ClientThink descend
 * the field toward the enemy flag -- run home when carrying. No combat, no
 * hook, no speed tricks yet. If a bot cannot get base to base on the rune,
 * nothing fancier deserves to exist; this is the walking skeleton the rest
 * grows on, and it is A/B-able against the legacy bots from the first
 * frame.
 *
 *   sv sg add        spawn a SLIPGATE bot (team by botctfteam, like legacy)
 *   sv sg remove     remove them all
 *
 * Movement per think: pick among the current seed's outgoing links (and
 * staying put) by field value at the destination, steer at the chosen
 * link's endpoint, run at full command. Jump links jump. Arrival needs no
 * test -- the next think re-reads position and field wherever we ended up,
 * which is the whole point of descending a field instead of chasing nodes.
 */

#include "g_local.h"
#include "g_ctffunc.h"
#include "g_tourney.h"              /* Match_Mode -- the clock read's one caveat */
#include "slipgate/sg_local.h"
#include "slipgate/sg_combat.h"
#include "slipgate/sg_chat.h"       /* human orders replace the role quota */
#include "slipgate/sg_persona.h"    /* the roster's names, wired to behaviour */

/*
 * The client lifecycle and the glue's edict helpers are declared where the
 * legacy bot spawner declares them -- locally, the way this tree does it
 * (see bl_spawn.c:26-28). Same functions, same signatures.
 */
void		ClientThink(edict_t *ent, usercmd_t *ucmd);
void		Cmd_Kill_f(edict_t *ent);
void		Cmd_Hook_f(edict_t *ent);
void		ClientDisconnect(edict_t *ent);
qboolean	ClientConnect(edict_t *ent, char *userinfo);
void		ClientBegin(edict_t *ent);
void		ClientUserinfoChanged(edict_t *ent, char *userinfo);
#include "slipgate/sg_net.h"
#include "slipgate/sg_cvars.h"
#include "slipgate/sg_util.h"
#include "slipgate/sg_hooks.h"

#define FIELD_INF       0x3fffffff
#include "slipgate/sg_bot.h"
#include "slipgate/sg_clock.h"
#include "slipgate/sg_danger.h"
#include "slipgate/sg_weights.h"
#include "slipgate/sg_tilt.h"
#include "slipgate/sg_lead.h"
#include "slipgate/sg_move.h"
#include "slipgate/sg_price.h"
#include "slipgate/sg_descend.h"
#include "slipgate/sg_goal.h"


float	sg_grab_time[2] = { -1000.0f, -1000.0f };  /* per team */
float	sg_push_until[2];   /* the conductor's window (sg_wavepush) */
static float	sg_role_skew_until[2];
static int	sg_role_skew[2];
static int	sg_role_escort_carrier[2] = { -1, -1 };
static qboolean sg_role_escort_on[2] = { true, true };
static rune_t	*sg_rune;
static qboolean sg_physics_warned;
static float sg_last_frame_time;

static void Role_LevelReset(void)
{
	sg_grab_time[0] = sg_grab_time[1] = -1000.0f;
	sg_push_until[0] = sg_push_until[1] = 0.0f;
	sg_role_skew_until[0] = sg_role_skew_until[1] = 0.0f;
	sg_role_skew[0] = sg_role_skew[1] = 0;
	sg_role_escort_carrier[0] = sg_role_escort_carrier[1] = -1;
	sg_role_escort_on[0] = sg_role_escort_on[1] = true;
}

rune_t *SG_Rune(void)
{
	return sg_rune;
}


static char		sg_rune_map[64];
/* A structurally valid rune can still be unusable for this level (most
 * notably when neither objective localizes).  Botfill retries joins on a
 * cadence; re-running the level-tagged setup on every retry leaked another
 * full graph/field allocation each time.  Latch that terminal setup failure
 * until the next level epoch instead. */
static qboolean	sg_setup_failed;

const char *SG_RuneMapName(void)
{
	return sg_rune_map;
}

/* one cost field per flag: cost_ms from every seed TO the flag seed */
static int		*sg_field_red;
static int		*sg_field_blue;

/* ------------------------------------------------------------------ rune */

unsigned char *sg_human_use; /* per-link human traffic tier (0-255)
                                     * from the demo corpus; NULL = none */
unsigned char *sg_def_post[2];  /* per-seed human defensive dwell
                                        * tier by team (.dpo plane 0/1):
                                        * where humans actually stand while
                                        * their flag is home -- 19% of it
                                        * within 250u of the stand, the
                                        * rest on the approaches */
unsigned char *sg_def_icept[2]; /* per-seed steal-response END
                                        * positions (.dpo plane 2/3): the
                                        * spots humans run to when the
                                        * flag leaves, aimed at the
                                        * carrier's future, not his now */
/* .dpo plane accessor for sg_fields (the arrays are file-static here) */
unsigned char *SG_DefPlane(int post, int team1)
{
	if (team1 < 0 || team1 > 1)
		return NULL;
	return post ? sg_def_post[team1] : sg_def_icept[team1];
}

unsigned char *sg_human_escape; /* the ESCAPEE's cut: only the flag
                                        * carrier's own entity trajectory in
                                        * the 20s after each steal (.hme) --
                                        * the roads humans actually flee on,
                                        * as opposed to .hml's hunter-heavy
                                        * POV-agnostic window */
unsigned char *sg_human_live; /* same, cut from the 20s windows
                                      * after a steal: how humans move
                                      * when a flag is OUT (.hml) */

/*
 * THE ESCAPE PRIORS (sg_escapeprior, enhancement 6, escapepriors.py).
 * Which WAY humans leave a stand they just robbed, per map and per stolen
 * flag, as an eight-bucket compass distribution: counts of the bearing
 * from the stand to where the human carrier actually was three seconds
 * after the grab. Mined from 268 client demos / 1549 usable steals.
 *
 * Held here as raw counts, one distribution for the CURRENT map, chosen
 * at load time by the same key order the mining tool writes:
 * "<map>:<stolen flag colour>" first, plain "<map>" as the fallback. A
 * CTF map is usually a mirror of itself, so the two stands' exits are
 * mirror bearings of one habit; pooling them is a real loss of signal
 * (measured over the corpus: the pooled entry's bucket entropy is
 * 0.3-0.8 bits higher than either colour's on most maps), and the
 * carrier always knows which stand he just robbed.
 */
int	sg_escape_count[2][SG_ESC_BUCKETS];  /* [0]=red flag stolen, [1]=blue */
int	sg_escape_total[2];                  /* 0 = no prior for that flag */


/*
 * A deliberately tiny reader for the one shape escapepriors.py writes:
 * find the quoted key, then read eight integers out of the bracket that
 * follows it. No JSON library, and no pretence of being one -- anything
 * that is not exactly the expected shape leaves the prior unset and the
 * pricing silent, which is the same outcome as a missing file.
 *
 * Matching the key WITH its quotes is what keeps "lmctf01" from matching
 * inside "lmctf01:red" or "lmctf01b", and the tool guarantees no map name
 * appears anywhere in the file outside the maps object.
 */
static qboolean Escape_Parse(const char *buf, const char *key, int *out)
{
	const char *p;
	char quoted[80];
	int i, got[SG_ESC_BUCKETS];

	Com_sprintf(quoted, sizeof(quoted), "\"%s\"", key);
	p = strstr(buf, quoted);
	if (!p)
		return false;
	p += strlen(quoted);
	while (*p == ' ' || *p == '\t' || *p == ':')
		p++;
	if (*p != '[')
		return false;
	p++;
	for (i = 0; i < SG_ESC_BUCKETS; i++)
	{
		while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' ||
		       *p == ',')
			p++;
		if (*p < '0' || *p > '9')
			return false;
		got[i] = atoi(p);
		while (*p >= '0' && *p <= '9')
			p++;
	}
	for (i = 0; i < SG_ESC_BUCKETS; i++)
		out[i] = got[i];
	return true;
}

static void Escape_Load(const char *mapname)
{
	char path[MAX_OSPATH];
	char lower[64], key[80];
	static char buf[32768];
	cvar_t *gamedir = sg_host.cvar("gamedir", "", 0);
	size_t n;
	FILE *f;
	int c, i, k;

	memset(sg_escape_count, 0, sizeof(sg_escape_count));
	sg_escape_total[0] = sg_escape_total[1] = 0;

	Com_sprintf(path, sizeof(path), "%s/escape-priors.json",
	            gamedir->string[0] ? gamedir->string : ".");
	f = fopen(path, "rb");
	if (!f)
		return;
	n = fread(buf, 1, sizeof(buf) - 1, f);
	fclose(f);
	buf[n] = 0;

	/* the file is keyed in lower case; a server that spelled the map
	 * LMCTF35 on the map command still means the same map */
	for (i = 0; mapname[i] && i < (int)sizeof(lower) - 1; i++)
		lower[i] = (char)tolower((unsigned char)mapname[i]);
	lower[i] = 0;

	for (k = 0; k < 2; k++)
	{
		Com_sprintf(key, sizeof(key), "%s:%s", lower,
		            k == 0 ? "red" : "blue");
		if (!Escape_Parse(buf, key, sg_escape_count[k]) &&
		    !Escape_Parse(buf, lower, sg_escape_count[k]))
			continue;
		for (c = 0, i = 0; i < SG_ESC_BUCKETS; i++)
			c += sg_escape_count[k][i];
		sg_escape_total[k] = c;
	}
	if (sg_escape_total[0] > 0 || sg_escape_total[1] > 0)
		sg_host.dprint("rune: escape bearings loaded (%s: red n=%d, blue n=%d)\n",
		           path, sg_escape_total[0], sg_escape_total[1]);
}

static rune_t *Rune_LoadReject(FILE *f, rune_t *r,
                               const char *path, const char *why)
{
	if (f)
		fclose(f);
	if (r)
	{
		if (r->linked_seed)
			sg_host.level_free(r->linked_seed);
		if (r->next_link)
			sg_host.level_free(r->next_link);
		if (r->first_link)
			sg_host.level_free(r->first_link);
		if (r->links)
			sg_host.level_free(r->links);
		if (r->seeds)
			sg_host.level_free(r->seeds);
		sg_host.level_free(r);
	}
	sg_host.dprint("rune: rejected %s (%s)\n", path, why);
	return NULL;
}

static int Rune_LinkKeyCompare(const void *left, const void *right)
{
	unsigned long long a = *(const unsigned long long *)left;
	unsigned long long b = *(const unsigned long long *)right;

	return (a > b) - (a < b);
}

/* The deployment linter rejects two graph shapes that the flat records alone
 * do not make safe: ambiguous duplicate controllers and live islands outside
 * either flag's reverse component. Keep the ordered link array untouched --
 * every human sidecar is indexed by that order -- and validate with temporary
 * indexes whose worst-case work remains bounded at the format maxima. */
static const char *Rune_ValidateGraphContract(rune_t *r)
{
	unsigned long long *keys = NULL;
	int *first_in = NULL, *next_in = NULL, *queue = NULL;
	byte *seen = NULL;
	edict_t *stands[2];
	int roots[2];
	int ns = r->hdr.num_seeds, nl = r->hdr.num_links;
	int i, which;
	const char *failure = NULL;

	/* Seed indices occupy 15 bits and every accepted action fits in four.
	 * Packing (from,to,action) into 34 bits makes adjacent equality after qsort
	 * exact, without reordering the serialized links themselves. */
	if (nl > 1)
	{
		keys = sg_host.level_alloc(sizeof(*keys) * (size_t)nl);
		if (!keys)
		{
			failure = "graph-contract allocation failure";
			goto done;
		}
		for (i = 0; i < nl; i++)
		{
			rune_link_t *l = &r->links[i];

			keys[i] = ((unsigned long long)(unsigned int)l->from << 19) |
			          ((unsigned long long)(unsigned int)l->to << 4) |
			          (unsigned long long)l->action;
		}
		qsort(keys, (size_t)nl, sizeof(*keys), Rune_LinkKeyCompare);
		for (i = 1; i < nl; i++)
			if (keys[i] == keys[i - 1])
			{
				failure = "duplicate (from,to,action) link triple";
				goto done;
			}
		sg_host.level_free(keys);
		keys = NULL;
	}

	/* The stand markers are stable even while a live flag is carried, and are
	 * the same objective positions Fields_Setup localizes immediately after the
	 * load. Rune_NearestSeed also enforces the tombstone/outgoing-owner rule. */
	stands[0] = SG_FlagStand(CTF_TEAM_RED, true);
	stands[1] = SG_FlagStand(CTF_TEAM_BLUE, true);
	if (!stands[0] || !stands[1])
	{
		failure = "flag objective stand unavailable";
		goto done;
	}
	for (which = 0; which < 2; which++)
	{
		roots[which] = Rune_NearestSeed(r, stands[which]->s.origin);
		if (roots[which] < 0)
		{
			failure = "flag objective root is not routable";
			goto done;
		}
	}

	first_in = sg_host.level_alloc(sizeof(*first_in) * (size_t)ns);
	next_in = sg_host.level_alloc(sizeof(*next_in) *
	                              (size_t)(nl ? nl : 1));
	queue = sg_host.level_alloc(sizeof(*queue) * (size_t)ns);
	seen = sg_host.level_alloc((size_t)ns);
	if (!first_in || !next_in || !queue || !seen)
	{
		failure = "graph-contract allocation failure";
		goto done;
	}
	for (i = 0; i < ns; i++)
		first_in[i] = -1;
	for (i = 0; i < nl; i++)
	{
		next_in[i] = first_in[r->links[i].to];
		first_in[r->links[i].to] = i;
	}

	for (which = 0; which < 2; which++)
	{
		int head = 0, tail = 0;

		memset(seen, 0, (size_t)ns);
		seen[roots[which]] = 1;
		queue[tail++] = roots[which];
		while (head < tail)
		{
			int at = queue[head++];
			int li;

			for (li = first_in[at]; li >= 0; li = next_in[li])
			{
				int from = r->links[li].from;

				if (seen[from])
					continue;
				seen[from] = 1;
				queue[tail++] = from;
			}
		}
		for (i = 0; i < ns; i++)
			if (!(r->seeds[i].flags & RSF_TOMBSTONE) && !seen[i])
			{
				failure = which == 0
				    ? "live seed outside red objective reverse component"
				    : "live seed outside blue objective reverse component";
				goto done;
			}
	}

done:
	if (keys)
		sg_host.level_free(keys);
	if (first_in)
		sg_host.level_free(first_in);
	if (next_in)
		sg_host.level_free(next_in);
	if (queue)
		sg_host.level_free(queue);
	if (seen)
		sg_host.level_free(seen);
	return failure;
}

/* Standard reflected IEEE CRC32. The rune file is already the canonical byte
 * representation: one ordered seed array followed by one ordered link array.
 * Hashing those still-unmodified arrays binds every sidecar to that graph. */
static unsigned int Sidecar_CRC32Update(unsigned int crc,
	const void *block, size_t size)
{
	static unsigned int table[256];
	static qboolean ready;
	const unsigned char *p = (const unsigned char *)block;
	unsigned int c;
	int i, j;

	if (!ready)
	{
		for (i = 0; i < 256; i++)
		{
			c = (unsigned int)i;
			for (j = 0; j < 8; j++)
				c = (c & 1U) ? (c >> 1) ^ 0xEDB88320U : c >> 1;
			table[i] = c;
		}
		ready = true;
	}
	while (size--)
		crc = table[(crc ^ (unsigned int)*p++) & 0xffU] ^ (crc >> 8);
	return crc;
}

static unsigned int Rune_PayloadCRC(const rune_t *r)
{
	unsigned int crc = 0xffffffffU;

	crc = Sidecar_CRC32Update(crc, r->seeds,
	        sizeof(rune_seed_t) * (size_t)r->hdr.num_seeds);
	crc = Sidecar_CRC32Update(crc, r->links,
	        sizeof(rune_link_t) * (size_t)r->hdr.num_links);
	return crc ^ 0xffffffffU;
}

/* Sidecars have no embedded map name: their maps/<mapname> suffix is the map
 * binding. The five-word header must otherwise match exactly, including the
 * graph CRC, and the file must contain exactly the indexed payload. Rewinding
 * to the payload here keeps every caller from maintaining a partial contract. */
static qboolean Sidecar_HeaderOK(FILE *f, const unsigned int header[5],
	unsigned int magic, int version, int count, int shape,
	unsigned int graph_crc, size_t payload_size)
{
	long file_size;
	size_t expected_size = sizeof(unsigned int) * 5 + payload_size;

	if (sizeof(unsigned int) != 4 || header[0] != magic ||
	    header[1] != (unsigned int)version ||
	    header[2] != (unsigned int)count ||
	    header[3] != (unsigned int)shape || header[4] != graph_crc)
		return false;
	if (fseek(f, 0, SEEK_END) != 0 || (file_size = ftell(f)) < 0 ||
	    (size_t)file_size != expected_size ||
	    fseek(f, (long)(sizeof(unsigned int) * 5), SEEK_SET) != 0)
		return false;
	return true;
}

rune_t *Rune_Load(const char *mapname)
{
	char path[MAX_OSPATH];
	FILE *f;
	rune_t *r;
	size_t expected_size;
	long file_size;
	unsigned int rune_crc;
	int i;
	cvar_t *gamedir = sg_host.cvar("gamedir", "", 0);

	Com_sprintf(path, sizeof(path), "%s/maps/%s.rune",
	            gamedir->string[0] ? gamedir->string : ".", mapname);
	f = fopen(path, "rb");
	if (!f)
		return NULL;

	r = sg_host.level_alloc(sizeof(rune_t));
	if (!r)
	{
		fclose(f);
		return NULL;
	}
	memset(r, 0, sizeof(*r));
	if (fread(&r->hdr, sizeof(r->hdr), 1, f) != 1 ||
	    r->hdr.magic != RUNE_MAGIC || r->hdr.version != RUNE_VERSION)
		return Rune_LoadReject(f, r, path, "bad header or version");
	if (r->hdr.num_seeds <= 0 || r->hdr.num_seeds > RUNE_MAX_SEEDS ||
	    r->hdr.num_links < 0 || r->hdr.num_links > RUNE_MAX_LINKS)
		return Rune_LoadReject(f, r, path, "count outside format limits");
	if (!memchr(r->hdr.mapname, '\0', sizeof(r->hdr.mapname)) ||
	    Q_stricmp(r->hdr.mapname, mapname))
		return Rune_LoadReject(f, r, path, "map identity mismatch");

	expected_size = sizeof(rune_header_t) +
	                sizeof(rune_seed_t) * (size_t)r->hdr.num_seeds +
	                sizeof(rune_link_t) * (size_t)r->hdr.num_links;
	if (fseek(f, 0, SEEK_END) != 0 || (file_size = ftell(f)) < 0 ||
	    (size_t)file_size != expected_size ||
	    fseek(f, (long)sizeof(rune_header_t), SEEK_SET) != 0)
		return Rune_LoadReject(f, r, path, "truncated or trailing data");

	r->seeds = sg_host.level_alloc(sizeof(rune_seed_t) * r->hdr.num_seeds);
	r->links = sg_host.level_alloc(sizeof(rune_link_t) *
	                              (r->hdr.num_links ? r->hdr.num_links : 1));
	if (!r->seeds || !r->links)
		return Rune_LoadReject(f, r, path, "allocation failure");
	if (fread(r->seeds, sizeof(rune_seed_t), r->hdr.num_seeds, f) != (size_t)r->hdr.num_seeds ||
	    fread(r->links, sizeof(rune_link_t), r->hdr.num_links, f) != (size_t)r->hdr.num_links)
		return Rune_LoadReject(f, r, path, "short payload read");
	fclose(f);
	f = NULL;

	for (i = 0; i < r->hdr.num_seeds; i++)
	{
		rune_seed_t *s = &r->seeds[i];

		if (!isfinite(s->origin[0]) || !isfinite(s->origin[1]) ||
		    !isfinite(s->origin[2]) ||
		    s->origin[0] < -4096.0f || s->origin[0] > 4095.875f ||
		    s->origin[1] < -4096.0f || s->origin[1] > 4095.875f ||
		    s->origin[2] < -4096.0f || s->origin[2] > 4095.875f ||
		    s->area_hint < 0 || s->area_hint > 255 ||
		    ((unsigned short)s->flags &
		     ~(unsigned short)(RSF_WATER | RSF_TOMBSTONE)))
			return Rune_LoadReject(NULL, r, path, "invalid seed geometry or flags");
	}
	for (i = 0; i < r->hdr.num_links; i++)
	{
		rune_link_t *l = &r->links[i];
		qboolean from_water, to_water;

		if (l->from < 0 || l->from >= r->hdr.num_seeds ||
		    l->to < 0 || l->to >= r->hdr.num_seeds || l->from == l->to ||
		    (r->seeds[l->from].flags & RSF_TOMBSTONE) ||
		    (r->seeds[l->to].flags & RSF_TOMBSTONE) ||
		    l->action > RL_DOOR || l->provenance > RL_DECLARED ||
		    l->cost_ms <= 0 ||
		    !isfinite(l->anchor[0]) || !isfinite(l->anchor[1]) ||
		    !isfinite(l->anchor[2]))
			return Rune_LoadReject(NULL, r, path, "invalid link record");
		from_water = (r->seeds[l->from].flags & RSF_WATER) != 0;
		to_water = (r->seeds[l->to].flags & RSF_WATER) != 0;
		{
			qboolean anchor_zero = l->anchor[0] == 0.0f &&
			                           l->anchor[1] == 0.0f &&
			                           l->anchor[2] == 0.0f;
			qboolean anchor_world =
			    l->anchor[0] >= -4096.0f && l->anchor[0] <= 4095.875f &&
			    l->anchor[1] >= -4096.0f && l->anchor[1] <= 4095.875f &&
			    l->anchor[2] >= -4096.0f && l->anchor[2] <= 4095.875f;

			vec3_t anchor_delta;
			float anchor_horiz;

			VectorSubtract(l->anchor, r->seeds[l->from].origin, anchor_delta);
			anchor_horiz = sqrtf(anchor_delta[0] * anchor_delta[0] +
			                     anchor_delta[1] * anchor_delta[1]);
			if ((l->action == RL_RUN && !anchor_zero && !anchor_world) ||
			    (l->action == RL_JUMP && !anchor_zero) ||
			    ((l->action == RL_LIFT || l->action == RL_TELEPORT ||
			      l->action == RL_DOOR) &&
			     (!anchor_world || l->provenance != RL_DECLARED ||
			      l->min_speed != 0 || l->heading != 0 ||
			      l->heading_slack != RUNE_DECLARED_CONTROL_MARKER ||
			      l->exit_speed != 0)) ||
			    (l->action == RL_TELEPORT &&
			     (anchor_horiz > RUNE_TELEPORT_SEED_REACH ||
			      fabsf(anchor_delta[2]) > RUNE_TELEPORT_SEED_REACH)))
				return Rune_LoadReject(NULL, r, path,
				                       "invalid action anchor/control");
		}
		/* V2 gives water movement one exact controller. Legacy generators
		 * proved a RUN/JUMP and changed its action byte later; accepting either
		 * form would reintroduce that unproved contract at load time. */
		if ((l->action == RL_RUN || l->action == RL_JUMP) &&
		    (from_water || to_water))
			return Rune_LoadReject(NULL, r, path,
			                       "water endpoint on dry controller");
		if (l->action == RL_ROCKETJUMP && (from_water || to_water))
			return Rune_LoadReject(NULL, r, path,
			                       "water endpoint on dry special controller");
		if (l->action == RL_SWIM &&
		    (!(from_water || to_water) || l->min_speed != 0 ||
		     l->heading != 0 || l->heading_slack != 0 ||
		     (l->provenance != RL_PROVEN && l->provenance != RL_ADJUSTED) ||
		     l->anchor[0] != 0.0f || l->anchor[1] != 0.0f ||
		     l->anchor[2] != 0.0f))
			return Rune_LoadReject(NULL, r, path,
			                       "invalid swim control");
		if (l->action == RL_ROCKETJUMP)
			return Rune_LoadReject(NULL, r, path,
			                       "rocket-jump action unsupported in rune v2");
		if (l->action == RL_DOOR)
		{
			edict_t *trigger;
			vec3_t approach_delta, egress_delta;
			float approach_h2, egress_h2;
			int axis;

			VectorSubtract(l->anchor, r->seeds[l->from].origin,
			               approach_delta);
			VectorSubtract(r->seeds[l->to].origin, l->anchor,
			               egress_delta);
			approach_h2 = approach_delta[0] * approach_delta[0] +
			              approach_delta[1] * approach_delta[1];
			egress_h2 = egress_delta[0] * egress_delta[0] +
			            egress_delta[1] * egress_delta[1];

			for (axis = 0; axis < 3; axis++)
				if (l->anchor[axis] !=
				    (float)(short)(l->anchor[axis] * 8.0f) * 0.125f)
					return Rune_LoadReject(NULL, r, path,
					                       "noncanonical door wait point");
			if (from_water || to_water ||
			    approach_h2 > 320.0f * 320.0f ||
			    fabsf(approach_delta[2]) > 48.0f ||
			    egress_h2 > 768.0f * 768.0f ||
			    fabsf(egress_delta[2]) > 96.0f ||
			    !(trigger = SG_DeclaredDoorForLink(l->anchor,
			        r->seeds[l->from].origin)) ||
			    !SG_OracleValidateDeclaredDoorLink(
			        r->seeds[l->from].origin, l->anchor,
			        r->seeds[l->to].origin, trigger, l->cost_ms))
				return Rune_LoadReject(NULL, r, path,
				                       "invalid declared door contract");
		}
		if (l->action == RL_HOOK &&
		    (l->provenance != RL_PROVEN || l->min_speed != 0 ||
		     (from_water && to_water) ||
		     l->heading_slack != (from_water
		         ? RUNE_WATER_HOOK_CONTROL_MARKER
		         : RUNE_HOOK_CONTROL_SLACK) ||
		     l->anchor[PITCH] < -180.0f || l->anchor[PITCH] >= 180.0f ||
		     l->anchor[YAW] < -180.0f || l->anchor[YAW] >= 180.0f ||
		     l->anchor[PITCH] != SHORT2ANGLE((short)ANGLE2SHORT(l->anchor[PITCH])) ||
		     l->anchor[YAW] != SHORT2ANGLE((short)ANGLE2SHORT(l->anchor[YAW])) ||
		     l->anchor[PITCH] < -89.0f || l->anchor[PITCH] > 89.0f ||
		     l->anchor[ROLL] < 1.0f ||
		     l->anchor[ROLL] > RUNE_HOOK_MAX_RAY))
			return Rune_LoadReject(NULL, r, path,
			                       "invalid hook control");
		if (l->action == RL_DROP)
		{
			vec3_t lip_delta;
			float lip_horiz, lip_yaw, stored_yaw, yaw_delta;

			VectorSubtract(l->anchor, r->seeds[l->from].origin,
			               lip_delta);
			lip_horiz = sqrtf(lip_delta[0] * lip_delta[0] +
			                  lip_delta[1] * lip_delta[1]);
			lip_yaw = atan2f(lip_delta[1], lip_delta[0]) *
			          180.0f / (float)M_PI;
			stored_yaw = l->heading * (360.0f / 256.0f);
			yaw_delta = lip_yaw - stored_yaw;
			while (yaw_delta > 180.0f) yaw_delta -= 360.0f;
			while (yaw_delta < -180.0f) yaw_delta += 360.0f;
			/* A v2 drop owns a real nearby lip and a recorded walk-off cone.
			 * Legacy generic fallthroughs stamped RL_DROP onto ordinary
			 * run/jump proofs, leaving anchor zero and slack 255; executing
			 * those records sends the bot toward world origin. */
			if ((r->seeds[l->from].flags & RSF_WATER) ||
			    l->min_speed != 0 ||
			    l->heading_slack != RUNE_DROP_CONTROL_MARKER ||
			    lip_horiz < 2.0f ||
			    lip_horiz > 256.0f || fabsf(lip_delta[2] - 8.0f) > 0.25f ||
			    fabsf(yaw_delta) > 360.0f / 256.0f)
				return Rune_LoadReject(NULL, r, path,
				                       "invalid drop control");
		}
		if (l->action == RL_JUMP && l->min_speed != 0)
			return Rune_LoadReject(NULL, r, path,
			                       "unsupported momentum-jump envelope");
	}
	rune_crc = Rune_PayloadCRC(r);

	/* per-seed link chains */
	r->first_link = sg_host.level_alloc(sizeof(int) * r->hdr.num_seeds);
	r->next_link = sg_host.level_alloc(sizeof(int) *
	                                  (r->hdr.num_links ? r->hdr.num_links : 1));
	r->linked_seed = sg_host.level_alloc((size_t)r->hdr.num_seeds);
	if (!r->first_link || !r->next_link || !r->linked_seed)
		return Rune_LoadReject(NULL, r, path, "index allocation failure");
	memset(r->linked_seed, 0, (size_t)r->hdr.num_seeds);
	for (i = 0; i < r->hdr.num_seeds; i++)
		r->first_link[i] = -1;
	for (i = r->hdr.num_links - 1; i >= 0; i--)
	{
		r->next_link[i] = r->first_link[r->links[i].from];
		r->first_link[r->links[i].from] = i;
		r->linked_seed[r->links[i].from] = 1;
	}
	for (i = 0; i < r->hdr.num_seeds; i++)
	{
		qboolean tombstone =
		    (r->seeds[i].flags & RSF_TOMBSTONE) != 0;

		/* V2 localization chooses the nearest visible geometry owner first.
		 * Every routable owner must therefore have an outgoing route, and every
		 * tombstone must remain link-free; malformed files cannot manufacture a
		 * search-past-the-barrier fallback. */
		if (tombstone == (r->linked_seed[i] != 0))
			return Rune_LoadReject(NULL, r, path,
			                       "invalid route-core seed ownership");
	}
	{
		const char *graph_failure = Rune_ValidateGraphContract(r);

		if (graph_failure)
			return Rune_LoadReject(NULL, r, path, graph_failure);
	}

	/*
	 * The human sidecar (<map>.hmn, humanbake.py): one traffic tier per
	 * link, log-scaled 0-255, cut from 59 hours of recorded human play.
	 * Optional -- a missing or mismatched sidecar leaves the array NULL
	 * and every consumer prices as before.
	 */
	sg_human_use = NULL;
	Com_sprintf(path, sizeof(path), "%s/maps/%s.hmn",
	            gamedir->string[0] ? gamedir->string : ".", mapname);
	f = fopen(path, "rb");
	if (f)
	{
		unsigned int hh[5];

		if (fread(hh, sizeof(unsigned int), 5, f) == 5 &&
		    Sidecar_HeaderOK(f, hh, 0x484D4E31, r->hdr.version,
		                     r->hdr.num_links, 0, rune_crc,
		                     (size_t)r->hdr.num_links))
		{
			sg_human_use = sg_host.level_alloc(r->hdr.num_links);
			if (fread(sg_human_use, 1, r->hdr.num_links, f) !=
			    (size_t)r->hdr.num_links)
				sg_human_use = NULL;
			else
				sg_host.dprint("rune: human prior loaded (%s)\n", path);
		}
		fclose(f);
	}
	sg_human_live = NULL;
	Com_sprintf(path, sizeof(path), "%s/maps/%s.hml",
	            gamedir->string[0] ? gamedir->string : ".", mapname);
	f = fopen(path, "rb");
	if (f)
	{
		unsigned int hh[5];

		if (fread(hh, sizeof(unsigned int), 5, f) == 5 &&
		    Sidecar_HeaderOK(f, hh, 0x484D4C31, r->hdr.version,
		                     r->hdr.num_links, 0, rune_crc,
		                     (size_t)r->hdr.num_links))
		{
			sg_human_live = sg_host.level_alloc(r->hdr.num_links);
			if (fread(sg_human_live, 1, r->hdr.num_links, f) !=
			    (size_t)r->hdr.num_links)
				sg_human_live = NULL;
			else
				sg_host.dprint("rune: flag-live prior loaded (%s)\n", path);
		}
		fclose(f);
	}
	sg_human_escape = NULL;
	Com_sprintf(path, sizeof(path), "%s/maps/%s.hme",
	            gamedir->string[0] ? gamedir->string : ".", mapname);
	f = fopen(path, "rb");
	if (f)
	{
		unsigned int hh[5];

		if (fread(hh, sizeof(unsigned int), 5, f) == 5 &&
		    Sidecar_HeaderOK(f, hh, 0x484D4531, r->hdr.version,
		                     r->hdr.num_links, 0, rune_crc,
		                     (size_t)r->hdr.num_links))
		{
			sg_human_escape = sg_host.level_alloc(r->hdr.num_links);
			if (fread(sg_human_escape, 1, r->hdr.num_links, f) !=
			    (size_t)r->hdr.num_links)
				sg_human_escape = NULL;
			else
				sg_host.dprint("rune: escape prior loaded (%s)\n", path);
		}
		fclose(f);
	}
	sg_def_post[0] = sg_def_post[1] = NULL;
	sg_def_icept[0] = sg_def_icept[1] = NULL;
	Com_sprintf(path, sizeof(path), "%s/maps/%s.dpo",
	            gamedir->string[0] ? gamedir->string : ".", mapname);
	f = fopen(path, "rb");
	if (f)
	{
		unsigned int hh[5];

		/* four per-seed planes: post tier red/blue, intercept tier
		 * red/blue; validates on seed count, not links */
		if (fread(hh, sizeof(unsigned int), 5, f) == 5 &&
		    Sidecar_HeaderOK(f, hh, 0x314F5044, r->hdr.version,
		                     r->hdr.num_seeds, 4, rune_crc,
		                     (size_t)r->hdr.num_seeds * 4))
		{
			int ns = r->hdr.num_seeds, k, ok = 1;
			unsigned char *planes[4];

			for (k = 0; k < 4; k++)
			{
				planes[k] = sg_host.level_alloc(ns);
				if (fread(planes[k], 1, ns, f) != (size_t)ns)
					ok = 0;
			}
			if (ok)
			{
				sg_def_post[0] = planes[0];
				sg_def_post[1] = planes[1];
				sg_def_icept[0] = planes[2];
				sg_def_icept[1] = planes[3];
				sg_host.dprint("rune: defense prior loaded (%s)\n", path);
			}
		}
		fclose(f);
	}
	Escape_Load(mapname);
	return r;
}

int Rune_NearestSeed(rune_t *r, vec3_t p)
{
	/* A seed is a local topology sample, not a global Voronoi label. Beyond two
	 * lattice steps the body may be in an intentionally omitted/unreachable
	 * region; snapping it to a distant visible component makes commands claim a
	 * route through geometry the graph never proved. Seedless recovery owns that
	 * fail-closed case. */
	const float max_horiz2 = 128.0f * 128.0f;
	int i, best = -1;
	float bestd = 1e30f;

	for (i = 0; i < r->hdr.num_seeds; i++)
	{
		vec3_t d;
		float dd;

		VectorSubtract(r->seeds[i].origin, p, d);
		if (d[2] > 96.0f || d[2] < -96.0f)
			continue;
		if (d[0] * d[0] + d[1] * d[1] > max_horiz2)
			continue;
		dd = d[0] * d[0] + d[1] * d[1] + d[2] * d[2] * 0.25f;
		if (dd < bestd)
		{
			vec3_t from, to;
			trace_t tr;

			/* Adjacent rooms and stacked walkways can have closer Euclidean
			 * seeds on the wrong side of solid architecture. Localizing there
			 * makes every perfectly good outgoing link point into the wall.
			 * Use a chest-height world line as the minimum topology test; a
			 * closed mover also correctly keeps the bot on its current side. */
			VectorCopy(p, from);
			VectorCopy(r->seeds[i].origin, to);
			from[2] += 16.0f;
			to[2] += 16.0f;
			tr = sg_host.trace(from, NULL, NULL, to, NULL, MASK_DEADSOLID);
			if (tr.startsolid || tr.fraction < 1.0f)
				continue;
			bestd = dd;
			best = i;
		}
	}
	if (best >= 0 &&
	    ((r->seeds[best].flags & RSF_TOMBSTONE) ||
	     (r->linked_seed && !r->linked_seed[best])))
		return -1;
	return best;
}

/* --------------------------------------------------------------- fields */

/*
 * THE WAY TO AIR (waves 415-419 forensics). The gurgle override kicked
 * straight up, and straight up is exactly wrong under the smap05 shelf
 * overhang: a drowning bot pinned itself to a ceiling at spd=0 with its
 * nose pointed at rock until "sank like a rock". Air is a GRAPH question
 * -- so answer it once per map: a multi-source relaxation from every dry
 * seed backward through swimmable links gives each water seed its next
 * hop toward breathable surface. The override then swims the actual way
 * out, overhangs and all. NULL on maps with no water; -1 for a water
 * seed with no path (a sealed pool -- then straight up remains the only
 * prayer and the old behavior stands).
 */
int	*sg_airnext;

static void Air_Build(void)
{
	int i, li, qhead = 0, qtail = 0;
	int n = sg_rune->hdr.num_seeds;
	int *dist, *incoming, *next_incoming, *queue;

	sg_airnext = sg_host.level_alloc(sizeof(int) * n);
	dist = sg_host.level_alloc(sizeof(int) * n);
	incoming = sg_host.level_alloc(sizeof(int) * n);
	next_incoming = sg_host.level_alloc(sizeof(int) *
	    (sg_rune->hdr.num_links > 0 ? sg_rune->hdr.num_links : 1));
	queue = sg_host.level_alloc(sizeof(int) * n);
	for (i = 0; i < n; i++)
	{
		sg_airnext[i] = -1;
		incoming[i] = -1;
		if ((sg_rune->seeds[i].flags & RSF_WATER) &&
		    !(sg_rune->seeds[i].flags & RSF_TOMBSTONE))
			dist[i] = 0x7fffff;
		else
		{
			dist[i] = 0;
			queue[qtail++] = i;
		}
	}
	/* Dry seeds are the only zero-distance air sources.  In particular, do
	 * not call a submerged water seed "air" merely because it owns a direct
	 * shoreline edge: that suppresses the relaxation below and leaves its
	 * next hop at -1, sending an emergency swimmer straight into an overhang.
	 * A proved water-to-dry SWIM is handled like every other edge and records
	 * the dry seed as the first real step toward breath. */
	/* Index every incoming water-origin SWIM once, then run a reverse
	 * multi-source BFS from all dry seeds. The old 64 whole-graph relaxation
	 * silently truncated valid long pools and depended on link order; this is
	 * O(seeds+links), converges for the full v2 bounds, and chooses a shortest
	 * number-of-strokes escape. */
	for (li = 0; li < sg_rune->hdr.num_links; li++)
	{
		rune_link_t *l = &sg_rune->links[li];

		next_incoming[li] = -1;
		if (l->action != RL_SWIM ||
		    !(sg_rune->seeds[l->from].flags & RSF_WATER))
			continue;
		next_incoming[li] = incoming[l->to];
		incoming[l->to] = li;
	}
	while (qhead < qtail)
	{
		int to = queue[qhead++];

		for (li = incoming[to]; li >= 0; li = next_incoming[li])
		{
			rune_link_t *l = &sg_rune->links[li];

			if (dist[l->from] != 0x7fffff)
				continue;
			dist[l->from] = dist[to] + 1;
			sg_airnext[l->from] = to;
			queue[qtail++] = l->from;
		}
	}
	sg_host.level_free(dist);
	sg_host.level_free(incoming);
	sg_host.level_free(next_incoming);
	sg_host.level_free(queue);
}

qboolean SG_LevelSetup(void)
{
	SG_HooksInit();     /* the host table, before any module reaches out */
	if (!SG_RunePhysicsCompatible())
	{
		sg_host.dprint("slipgate: disabled: rune v%d requires sv_gravity %d, "
		               "sv_airaccelerate 0, sv_maxvelocity >= 800, and want_funky_gravity 0\n",
		               RUNE_VERSION, RUNE_PROOF_GRAVITY);
		return false;
	}
	if (sg_setup_failed)
		return false;

	if (sg_rune && Q_stricmp(sg_rune_map, level.mapname) == 0)
		return true;

	/* once per map, ahead of the rune: a map with no rune still answers
	 * `sv sg weights`, and the admin editing the file between maps expects
	 * the next map to be running it */
	Weights_Load();

	sg_rune = Rune_Load(level.mapname);
	if (!sg_rune)
	{
		sg_host.dprint("slipgate: no rune for %s -- run 'sv rune' first\n",
		           level.mapname);
		return false;
	}
	/* Danger_Load names its file through SG_RuneMapName; publish the new map
	 * before asking it to read, never after. */
	Com_sprintf(sg_rune_map, sizeof(sg_rune_map), "%s", level.mapname);
	Air_Build();        /* every water seed learns its way to air */
	Danger_Load();      /* what past matches taught about this map */
	/* Com_sprintf is the tree's own bounded copy (q_shared.c) and always
	 * terminates; strncpy at sizeof-1 does not, which is what -Wall's
	 * stringop-truncation was reporting here. Same call Rune_Load uses. */
	if (!Fields_Setup(sg_rune))
	{
		sg_host.dprint("slipgate: field setup failed (no flags?); "
		               "disabled until the next level\n");
		sg_setup_failed = true;
		sg_rune = NULL;
		sg_human_use = NULL;
		sg_human_live = NULL;
		sg_human_escape = NULL;
		sg_def_post[0] = sg_def_post[1] = NULL;
		sg_def_icept[0] = sg_def_icept[1] = NULL;
		sg_airnext = NULL;
		memset(&sg_fields, 0, sizeof(sg_fields));
		return false;
	}
	Caco_Reset();

	sg_host.dprint("slipgate: rune %s, %d seeds, %d links, all fields up\n",
	           sg_rune->hdr.mapname, sg_rune->hdr.num_seeds,
	           sg_rune->hdr.num_links);
	return true;
}

/* ----------------------------------------------------------------- body */



/* the role whose surface is being evaluated this frame -- SLIPGATE runs
 * its bots strictly serially, so a file-static carries it into the
 * detour arithmetic without widening every signature on the path */




/*
 * (d) THE DETOUR BUDGET for the mega, Worth_Quad's own arithmetic applied to
 * the mega's own fields.
 *
 * The triangle is the one Detour_Value evaluates for the per-item classes and
 * it is exact here for the same reason: to_mega[k] was flooded FROM pad k, so
 * it reads as cost from anywhere TO that pad, and the far leg is the goal field
 * sampled at the pad's own seed.
 *
 *     detour = cost_to_pad + pad_to_goal - direct
 *     value  = worth / (1 + max(0, detour) / 1500)
 *
 * plus one thing the class arithmetic does not have: a HARD ceiling. The decay
 * alone never quite reaches zero, and a mega on the far side of the map would
 * still tug a little at every seed forever. Four seconds of extra road is the
 * bound -- past that the bot is not detouring for the mega, it is going to the
 * mega and calling the flag a detour, which is the failure mode this whole
 * feature has to not have.
 */


/*
 * Role assignment: the owner's quota, then the two situational roles.
 *
 * Two in five defend (nearest-rounded), carrier counts toward defence, a side
 * of one attacks. Rank is slot order among SLIPGATE bots of the team, so the
 * assignment is stable frame to frame.
 *
 * Precedence, in order:
 *
 *   CARRY     I have the flag. Nothing else applies.
 *   DEFEND    my rank falls inside the quota -- the post is kept whatever
 *             else is happening; the situational roles are drawn from the
 *             attacking share only, which is what "attackers convert" means.
 *   RECOVER   our own flag is astray. EVERY attacker converts: getting it
 *             back outranks escorting, so no escort is named this frame.
 *   ESCORT    we have a live carrier who is not me: exactly one attacker,
 *             the lowest-ranked one that is not the carrier itself.
 *   ATTACK    everyone else.
 */
static sg_role_t SG_Role(sg_bot_t *bot, qboolean carrying)
{
	int team = bot->ent->client->ctf.teamnum;
	int size = 0, defenders_wanted, my_rank = 0, i;
	int my_client = (int)(bot->ent - g_edicts) - 1;
	sg_belief_carrier_t *own = &sg_caco_team_belief.carrier[SG_TeamIdx(team)];

	if (carrying)
		return SG_ROLE_CARRY;

	/*
	 * A standing order from a HUMAN teammate replaces the quota: the
	 * player said "defend" and defending is what happens, for the order's
	 * lifetime (sg_chat.c owns expiry: 90s, disconnect, team change).
	 * Only the flag outranks a human -- a carrier carries.
	 */
	{
		int forced = SG_ChatOrderedRole(bot->ent);

		if (forced >= 0 &&
		    (forced != SG_ROLE_ESCORT || SG_ChatEscortTarget(bot->ent)))
		{
			/* A direct defend order names the stand watchman, independent of
			 * this slot's prior natural-role history. */
			if (forced == SG_ROLE_DEFEND)
				bot->def_stand = true;
			return (sg_role_t)forced;
		}
	}

	for (i = 0; i < SG_MAXBOTS; i++)
	{
		if (!sg_bots[i].active || !sg_bots[i].ent || !sg_bots[i].ent->inuse)
			continue;
		if (sg_bots[i].ent->client->ctf.teamnum != team)
			continue;
		if (&sg_bots[i] == bot)
			my_rank = size;
		/* where in the ranking our own carrier sits, if it is one of ours */
		size++;
	}

	/*
	 * STRATEGY BY GAME STATE, the way the demos play it. Four states from
	 * two common-knowledge bits (each flag home or astray -- the HUD tells
	 * everyone), and each state names its shape. The old static 2-in-5
	 * quota played every state identically; games are won by playing them
	 * differently.
	 *
	 *   both home        2 DEFEND, rest ATTACK. The base shape.
	 *   theirs astray    2 DEFEND (the return-kill wave is coming),
	 *   (ours home)      1 ESCORT walks the carrier in, rest ATTACK their
	 *                    base -- they cannot score while we hold theirs,
	 *                    and the next steal queues behind this capture.
	 *   ours astray      1 DEFEND holds the stand for the return; the
	 *   (theirs home)    rest RECOVER. An empty stand needs a watchman,
	 *                    not a garrison.
	 *   both astray      the decisive state. 1 DEFEND stand-watch,
	 *                    1 ESCORT keeps our carrier alive, rest RECOVER --
	 *                    the standoff breaks on exactly one event, their
	 *                    carrier's death, and ours must survive to convert
	 *                    it.
	 */
	{
		int belief_team = SG_TeamIdx(team);
		qboolean ours_astray =
		    (sg_caco_team_belief.flag[belief_team][SG_TeamIdx(team)].state ==
		     SG_FLAG_ASTRAY);
		qboolean theirs_astray =
		    (sg_caco_team_belief.flag[belief_team]
		         [SG_TeamIdx(SG_EnemyTeam(team))].state == SG_FLAG_ASTRAY);
		qboolean have_carrier = (own->client >= 0 && own->seed >= 0 &&
		                         sg_fields.our_carrier_valid[SG_TeamIdx(team)]);

		defenders_wanted = ours_astray ? 1 : 2;

		/*
		 * TEAM SKEW (sg_teamskew, rung-4 tell #2: team-mirror symmetry).
		 * All three set-#1 judges read "identical AI on both sides" off
		 * the sheets: balanced escort means, alternating presses,
		 * role-locked plateaus that never rotate. Real pub teams are
		 * lopsided and DRIFT -- one side runs attack-heavy for a few
		 * minutes, then reshuffles. Each team rolls a defender-count
		 * skew of -1, 0, or +1 that rerolls every ~3 minutes on
		 * independent per-team clocks, so the two sides' role mixes
		 * decorrelate and wander the way two unrelated rosters do.
		 * The existing states below still own the astray cases.
		 */
		if (sg_cv.teamskew->value > 0.0f && size >= 4)
		{
			int ts = SG_TeamIdx(team);

			if (SG_TimerReady(sg_role_skew_until[ts]))
			{
				sg_role_skew[ts] = (rand() % 3) - 1;
				SG_TimerArm(&sg_role_skew_until[ts], 150.0f +
				            (float)(rand() % 90));
			}
			defenders_wanted += sg_role_skew[ts];
			if (defenders_wanted < 0)
				defenders_wanted = 0;
			if (defenders_wanted > size - 1)
				defenders_wanted = size - 1;
		}

		if (size <= 1)
			defenders_wanted = 0;
		else if (size == 2)
		{
			/*
			 * DUEL ROLES (sg_duelroles, wave 285+). The hardcoded 1
			 * was a catch-22 the 268-283 census convicted: dw stuck
			 * at 1 in 131/138 transitions, zero duel caps in 16
			 * waves, while the statue defender sat p90=173u from
			 * its post -- 2v2 could never push together because
			 * dw=0 required already holding the flag. Under the
			 * flag, duel teams run the same state machine as
			 * everyone: theirs-astray pushes BOTH bots (they cannot
			 * score while we hold theirs), ours-astray keeps the
			 * watchman. Off, the old pin stands.
			 */
			if (sg_cv.duelroles->value)
				defenders_wanted = theirs_astray ? 0 : 1;
			else
				defenders_wanted = 1;
		}
		/* a live carrier on our side counts toward the defensive share */
		if (own->client >= 0 && !ours_astray)
			defenders_wanted--;
		if (defenders_wanted < 0)
			defenders_wanted = 0;

		/*
		 * CLOCKPLAY (sg_clockplay). The score and the clock, finally in
		 * the quota. It lands here, last, on the FINAL number: the flag
		 * states above decide the shape of the fight and the scoreline
		 * only leans it, so a late lead adds its body to whatever the
		 * state already asked for rather than replacing it.
		 *
		 * One body, never more -- a lean, not a formation change -- and
		 * the team keeps at least one attacker whatever the lead is,
		 * because a side with nobody in the enemy base cannot end the
		 * game, only survive it, and surviving runs out at zero.
		 */
		{
			int shift = Clock_DefendShift(team);

			if (shift)
			{
				defenders_wanted += shift;
				if (defenders_wanted < 0)
					defenders_wanted = 0;
				if (size > 1 && defenders_wanted > size - 1)
					defenders_wanted = size - 1;
				else if (size <= 1)
					defenders_wanted = 0;
			}
		}

		/* role-flap diagnostic: two bots alternated DEFEND/ATTACK every
		 * frame of it18 (600 flips/600 samples) -- print the decision
		 * inputs on each change so the oscillating input names itself */
		if (sg_cv.debug->value)
		{
			static int last_dw[SG_MAXBOTS], last_oc[SG_MAXBOTS];
			int me = (int)(bot - sg_bots);

			if (me >= 0 && me < SG_MAXBOTS &&
			    (last_dw[me] != defenders_wanted ||
			     last_oc[me] != own->client))
			{
				sg_host.dprint("ROLEIN %s dw=%d rank=%d own=%d astray=%d size=%d\n",
				           bot->ent->client->pers.netname,
				           defenders_wanted, my_rank, own->client,
				           (int)ours_astray, size);
				last_dw[me] = defenders_wanted;
				last_oc[me] = own->client;
			}
		}

		if (my_rank < defenders_wanted)
		{
			/* the FIRST defender is the statue on the stand; a second
			 * is the patrol -- it never pins, so the surface walks it
			 * around the base picking up armor and covering approaches */
			bot->def_stand = (my_rank == 0);
			return SG_ROLE_DEFEND;
		}

		/*
		 * One escort whenever we have a live carrier that is not me --
		 * and the escort is the NEAREST eligible body, not a rank slot.
		 * The rank-slot version handed the job to whoever sat at a fixed
		 * position in the scan order: dead, respawning, or across the
		 * map. The waves 71-72 census reads accordingly -- no escort at
		 * all for 30-100% of carry seconds, median distance 430-1860
		 * when one existed, and mactf06's entire 28-second carry walked
		 * naked. Every bot runs the same argmin over the same shared
		 * positions; the incumbent gets a 300-unit head start so the
		 * job does not flap between two equidistant mates.
		 */
		if (have_carrier && own->client != my_client)
		{
			edict_t *car_ent = g_edicts + own->client + 1;
			float bestd = 1e30f;
			int best_i = -1, rank_i = 0, k;

			for (k = 0; k < SG_MAXBOTS; k++)
			{
				vec3_t ed;
				float dd;

				if (!sg_bots[k].active || !sg_bots[k].ent ||
				    !sg_bots[k].ent->inuse)
					continue;
				if (sg_bots[k].ent->client->ctf.teamnum != team)
					continue;
				if (rank_i++ < defenders_wanted)
					continue;       /* defenders keep the base */
				if ((int)(sg_bots[k].ent - g_edicts) - 1 == own->client)
					continue;       /* the carrier escorts nobody */
				if (sg_bots[k].ent->deadflag)
					continue;
				/* The shared carrier flood can be finite globally but unreachable
				 * from this bot's directed component. Assign an escort only where
				 * the mission can actually be descended; another reachable body may
				 * be available. */
				if (sg_bots[k].seed < 0 ||
				    sg_bots[k].seed >= SG_Rune()->hdr.num_seeds ||
				    sg_fields.our_carrier[SG_TeamIdx(team)]
				        [sg_bots[k].seed] >= SG_FIELD_INF)
					continue;
				VectorSubtract(sg_bots[k].ent->s.origin,
				               car_ent->s.origin, ed);
				dd = VectorLength(ed);
				if (sg_bots[k].last_role == (int)SG_ROLE_ESCORT)
					dd -= 300.0f;
				if (dd < bestd)
				{
					bestd = dd;
					best_i = k;
				}
			}
			if (best_i >= 0 && &sg_bots[best_i] == bot)
			{
				/*
				 * ESCORT DOSE (sg_escortdose, rung-4 set #1 tell #1,
				 * named by all three judges on every bot sheet): the
				 * fleet escorts EVERY carry at 0.33-0.75 escort
				 * fraction while pub humans run flags alone (0.02-
				 * 0.32, "classic lone-wolf hero run"). The machinery
				 * out-organizes the population it imitates. The dose
				 * is the percent of carries that get an escort AT
				 * ALL; the roll happens once per carry (rerolled when
				 * the carrier changes) so a carry is escorted or
				 * abandoned for its whole life, like a pub decides.
				 */
				int et = SG_TeamIdx(team);
				int cc = own->client;

				if (sg_role_escort_carrier[et] != cc)
				{
					sg_role_escort_carrier[et] = cc;
					sg_role_escort_on[et] = ((rand() % 100) <
					    (int)sg_cv.escortdose->value);
				}
				if (sg_role_escort_on[et])
					return SG_ROLE_ESCORT;
			}
		}

		/* No carrier ends the carry epoch. A later steal by the same client
		 * gets a fresh pub-style escort coin instead of inheriting the first. */
		if (!have_carrier)
			sg_role_escort_carrier[SG_TeamIdx(team)] = -1;
		if (ours_astray)
			return SG_ROLE_RECOVER;
		(void)theirs_astray;    /* shape only differs via the states above */
		return SG_ROLE_ATTACK;
	}
}




/*
 * The chain's shape, all of it fitted rather than derived -- the ANGLE is
 * the engine's and is computed above; these are the preferences around it.
 *
 * SG_AS_PERIOD     seconds per full swing. A hop off 270 up under 800
 *                  gravity is airborne 0.675s, so one period is two hops:
 *                  one shoulder per flight, which is the cadence a player
 *                  swaps strafe keys on.
 * SG_AS_VIEWSHARE  how much of the angle the VIEW carries; the input
 *                  carries the rest, exactly as forward+strafe does.
 * SG_AS_VIEWMAX    and the ceiling on that, in degrees. At the period
 *                  above this peaks near 150 deg/s of view movement --
 *                  inside sg_turnrate's 600 and inside a human wrist.
 * SG_AS_CORR       heading error, in degrees, that saturates the bias on
 *                  the swing. The bias is what keeps the mean of the S on
 *                  the road instead of walking it off one shoulder.
 * SG_AS_ABORT      heading error that ends the chain outright: past this
 *                  the body needs to turn, not to harvest.
 * SG_AS_RUN        straight road a chain wants before it commits, units.
 * SG_AS_HOLD       and the fraction of that a chain already running is
 *                  held to, so the road test cannot chatter a chain to
 *                  death across its own bar.
 * SG_AS_FLOOR      2D speed under which there is nothing to chain.
 * SG_AS_FLAGKEEP   never this close to either stand: speed is for TRAVEL.
 * SG_AS_MINCHAIN   a chain shorter than this is not worth a log line.
 */


/*
 * One physics step of movement, decided from where the bot actually is.
 *
 * The caller has already put the plain command in place -- forward down the
 * view, no strafe -- so every early return here leaves honest, unaltered
 * running. Three things can be added to it:
 *
 *   ground strafe   accel 10, the strong half of this engine
 *   air strafe      accel 1, the same derivation, only A changes
 *   landing jump    Pmove runs PM_CheckJump before PM_Friction, and a jump
 *                   clears groundentity -- which is the condition PM_Friction
 *                   tests before applying any ground friction at all. A jump
 *                   issued on the step the bot touches down therefore pays no
 *                   friction; one issued a step late pays speed * 6 * ft.
 *                   That single step is the whole of bunny hopping here.
 */


/*
 * Is there a straight, clear road ahead worth committing a hop chain to?
 *
 * The same walk the pursuit point makes -- plain RUN links, no rounding
 * anchors, strictly down the field -- collected into a chain, and then two
 * questions asked of the point `want` units of ARC down it:
 *
 *   the chord      how far that point actually is in a straight line. The
 *                  seed centers are beads on a road and the polyline
 *                  through them zigzags even where the road does not (the
 *                  pursuit census: 40 deg/s of churn on geometrically
 *                  straight chain), so leg-by-leg bend angles measure the
 *                  beads, not the road. Chord over arc does not: a road
 *                  that goes somewhere gives back most of what was walked.
 *   the room       the fan's own player-box trace, run to that point. A
 *                  chain in the air cannot dodge, so the corridor has to
 *                  be there before the first hop, not discovered on the
 *                  third.
 *
 * SG_AS_CHORD is the fraction of the arc the chord has to keep, and the
 * chord also has to point within SG_AS_BEND of the heading the body is
 * actually steering on -- a road that doubles back scores well on chord
 * alone.
 */




/*
 * The duel, priced onto the same surface as everything else.
 *
 * Dueling is not a mode the body enters; it is two more terms in the sum, in
 * the same milliseconds every other term is denominated in (Surface_At). What
 * combat supplies is the target's believed position, the range the weapon in
 * hand wants, and what being seen costs right now (SG_CombatDuel, sg_combat.h).
 *
 * SG_DUEL_RANGE_MS   value per unit of range error. Half the carrier's own
 *                    threat repulsion above, which prices a step toward a
 *                    believed contact at 3.0 per unit: a carrier being caught
 *                    loses the match, a fighter standing a hundred units off
 *                    its best range loses some damage. The relation between
 *                    the two is the claim; the absolute is fitted.
 * SG_DUEL_COVER_MS   value for standing where the target can see you, scaled
 *                    by exposure. At exposure 1 it is 900 ms -- comparable to
 *                    the 1500 ms scale the detour arithmetic uses for an item
 *                    worth taking, so cover competes with a pickup and loses
 *                    to the objective. Fitted.
 */


/*
 * The lateral weave. Period per bot so a squad does not oscillate in phase and
 * present one wide target; the spread is 0.4 to 0.85 s, which is fast enough
 * that a 650 u/s rocket aimed where the bot was arrives where it is not, and
 * slow enough that ground friction is not eating the whole reversal. 300 is
 * pm_maxspeed's own wishspeed clamp (the strafe work above uses 400 pre-clamp
 * for direction only; here the magnitude is the point). All three fitted.
 */



/*
 * Has anything landed on this body since `since`? The damage ring
 * (sg_caco.c, four entries per client) already books every hit T_Damage
 * delivers, seen shooter or not, so the spawn beat needs no sense of its
 * own: the question "did the world just object" is exactly the one the ring
 * was built to answer, and it answers it for splash and falls and the rail
 * from a room away alike.
 */
qboolean Beat_HurtSince(edict_t *e, float since)
{
	int ci, k;

	if (!e || !e->client)
		return false;
	ci = (int)(e->client - game.clients);
	if (ci < 0 || ci >= SG_DMG_CLIENTS)
		return false;
	for (k = 0; k < SG_DMG_RING; k++)
		if (sg_caco_damage[ci][k].attacker >= 0 &&
		    sg_caco_damage[ci][k].time > since)
			return true;
	return false;
}


/*
 * A bot slot and its learned map facts outlive a body.  These do not: active
 * weapon/action phases, route commitments, local progress samples, holds and
 * carry-specific policy.  Clear them once on the death edge so a respawn can
 * never finish a hook, rocket jump, grenade cook or mission chosen by the
 * previous life.  Blacklists, dead-door lessons, danger, persona and tilt are
 * intentionally absent: those are knowledge the next life is meant to keep.
 */
static void Bot_ResetLifeActions(sg_bot_t *bot)
{
	int i;

	bot->hook_phase = 0;
	bot->hook_link = -1;
	bot->hook_bite_logged = false;
	bot->hook_attached_validated = false;
	bot->hook_landbrake = 0.0f;
	VectorClear(bot->hook_anchor);
	VectorClear(bot->hook_view);
	VectorClear(bot->hook_source);
	memset(&bot->hook_source_pms, 0, sizeof(bot->hook_source_pms));
	memset(&bot->hook_attach_pms, 0, sizeof(bot->hook_attach_pms));
	bot->hook_source_water = false;
	bot->hook_source_health = 0;
	bot->hook_attach_groundentity = false;
	bot->hook_attach_watertype = 0;
	bot->hook_attach_waterlevel = 0;
	VectorClear(bot->hook_dest);
	bot->hook_deadline = 0.0f;
	bot->hook_pull_ms = 0;
	bot->hook_settle_ms = 0;
	bot->hook_proved_pull_ms = 0;
	bot->hook_proved_release_ms = 0;
	bot->hook_proved_arrival_ms = 0;
	bot->hook_proved_settle_ms = 0;
	bot->flow_release = false;
	bot->speedhook = false;
	VectorClear(bot->hp_cur_dep);
	VectorClear(bot->hp_prev_dep);
	bot->hp_prev_land = 0.0f;

	bot->rj_phase = 0;
	VectorClear(bot->rj_aim);
	VectorClear(bot->rj_dest);
	bot->rj_deadline = 0.0f;
	bot->rj_fire_until = 0.0f;
	bot->rj_use_next = 0.0f;
	bot->nade_phase = 0;
	bot->nade_until = 0.0f;
	VectorClear(bot->nade_at);

	bot->watch_link = -1;
	bot->watch_since = 0.0f;
	VectorClear(bot->watch_org);
	bot->jump_link = -1;
	bot->jump_started = false;
	bot->drop_link = -1;
	bot->drop_started = false;
	bot->drop_walkoff = false;
	bot->drop_airborne = false;
	bot->drop_recover = false;
	bot->swim_validated = false;
	bot->swim_proved_ms = 0;
	bot->swim_elapsed_ms = 0;
	bot->swim_air_seed = -1;
	bot->declared_activated = false;
	bot->declared_started = false;
	bot->declared_start_frame = -1;
	bot->declared_touched = false;
	bot->declared_touch_frame = -1;
	bot->declared_triggered = false;
	bot->declared_trigger_frame = -1;
	bot->declared_egress_proof_frame = -1;
	bot->declared_door_retreat = false;
	bot->declared_door_suffix_ms = 0;
	bot->commit_link = -1;
	bot->commit_until = 0.0f;
	bot->sticky_link = -1;
	bot->latch_until = 0.0f;
	bot->rail_link = -1;
	bot->rail_stage = 0;
	bot->rail_until = 0.0f;
	bot->railhold_since = 0.0f;
	bot->railhold_patience = 0.0f;
	bot->railhold_enemy = -1;

	bot->stuck_time = 0.0f;
	VectorClear(bot->stuck_origin);
	bot->seedless_active = false;
	bot->seedless_since = 0.0f;
	bot->seedless_turn_until = 0.0f;
	bot->seedless_yaw = 0.0f;
	VectorClear(bot->stag_org);
	bot->stag_since = 0.0f;
	bot->stag_next = 0.0f;
	VectorClear(bot->wedge_org);
	bot->wedge_since = 0.0f;
	bot->nav_drove = false;
	bot->engaged_last = false;
	bot->fan_side = 0;
	bot->fan_side_until = 0.0f;
	bot->escape_until = 0.0f;
	bot->escape_yaw = 0.0f;
	bot->door_hold_ent = NULL;
	bot->door_hold_link = -1;
	bot->door_hold_deadline = 0.0f;
	bot->deaddoor_ahead = false;
	bot->door_held_last = false;
	bot->mate_block_last = false;

	bot->linger_since = 0.0f;
	bot->linger_hot = false;
	bot->rally_since = 0.0f;
	bot->strict_since = 0.0f;
	bot->rally_cover = -1;
	bot->tac_seed = -1;
	bot->tac_time = 0.0f;
	bot->tac_role = -1;
	bot->patrol_seed = -1;
	bot->patrol_until = 0.0f;
	bot->last_role = -1;
	bot->last_goalcost = -1;

	bot->was_carrying = false;
	bot->carry_start = 0.0f;
	bot->carry_startcost = -1;
	bot->carry_bestcost = -1;
	bot->carry_lost_at = 0.0f;
	bot->exitasym_n = 0;
	bot->exitasym_armed = false;
	bot->escprior_bucket = -1;
	bot->escprior_until = 0.0f;
	bot->escprior_dose = 0.0f;
	bot->runeconv_until = 0.0f;

	for (i = 0; i < SG_VISIT_RING; i++)
	{
		bot->visit_seed[i] = -1;
		bot->visit_goal[i] = 0;
		bot->visit_min[i] = 0;
		bot->visit_time[i] = 0.0f;
	}
	bot->visit_head = 0;
	bot->orbit_last_seed = -1;
	bot->inlinks_n = 0;
	bot->prev_seed = -1;
	bot->prev_seed_time = 0.0f;
	bot->ribbon_link = -1;
	bot->ribbon_next = 0.0f;
	bot->ribbon_off = 0.0f;
	bot->ribbon_goal = 0.0f;
	bot->nav_yaw_cur = 0.0f;
	bot->nav_yaw_t = 0.0f;
	bot->terminal = false;
	bot->sink_ban = false;
	bot->term_brake = 0.0f;
	bot->breather_until = 0.0f;
	bot->was_dead = 1;
}

/*
 * THE CORPSE FRAME (split from SG_BotThink, 2026-08-11 standards pass).
 * Everything a dead bot owes the world: teach the danger and tilt ledgers
 * once, drop every live claim, pulse the respawn button. Returns true when
 * this frame belonged to a corpse and the think ends with it.
 */
static qboolean Think_Dead(sg_bot_t *bot, edict_t *e, usercmd_t *cmd)
{
	if (!e->deadflag)
		return false;

	/* my own death, at my own seed: the most honest sighting there
	 * is, and the danger dimension's only teacher */
	if (!bot->death_taught)
	{
		if (bot->seed >= 0)
		{
			Danger_Learn(e->client->ctf.teamnum, bot->seed);
			Tilt_Note(e, bot);  /* the same death, remembered personally */
		}
			Bot_ResetLifeActions(bot);
			Combat_ResetClient(e);
			Caco_ResetClient(e);
			bot->death_taught = true;
	}
	bot->seed = -1;
	bot->view_on = false;   /* respawn snaps the view fresh */
	/* a chain that ended in a death ended; the frame that would have
	 * closed it never ran, and a stale start would date the next one */
	bot->as_since = 0.0f;
	bot->as_phase = 0.0f;
	/* a corpse is not standing anybody's pad: release the lease early
	 * rather than making the next claimant wait it out */
	Lead_Abort(bot, "died");
	/* no reading, so the respawn's jump to 100 is not a pickup; the
	 * back-off dies with the life that earned it */
	bot->mega_on = false;
	bot->mega_hp = 0;
	bot->mega_since = 0.0f;
	bot->mega_next = 0.0f;
	bot->beat_ready = true; /* dead HERE, on this level: the spawn beat
	                         * has something to be the far side of */
	/*
	 * PULSE the trigger, never hold it. Respawn keys off
	 * latched_buttons -- fresh presses only (p_client.c:3203) -- and
	 * a button held from the first dead frame latches exactly once,
	 * before respawn_time has elapsed, then never again: the corpse
	 * waits forever for a press that cannot re-arrive. Observed live
	 * the moment a human watched a body instead of a stat line.
	 * Toggling at 5Hz lands a fresh latch every other frame.
	 */
	cmd->buttons = (((int)(level.time * 10.0f)) & 2)
	              ? BUTTON_ATTACK : 0;
	ClientThink(e, cmd);
	return true;
}

/*
 * THE RESPAWN EDGE (same split, body verbatim): the first live frame
 * after a death, where the tilt clocks start -- a window started on the
 * corpse would spend a second and a half of itself lying on the floor.
 */
static void Think_RespawnEdge(sg_bot_t *bot, edict_t *e)
{
	if (bot->death_taught)
	{
		/* Progress clocks are sampled from the new body's actual spawn.  Zero
		 * is a real old timestamp once a map has run for 15 seconds; leaving
		 * wedge_since at zero could make a respawn near world origin look like
		 * a 15-second wedge and trigger the last-resort suicide immediately. */
		VectorCopy(e->s.origin, bot->stuck_origin);
		VectorCopy(e->s.origin, bot->watch_org);
		VectorCopy(e->s.origin, bot->stag_org);
		VectorCopy(e->s.origin, bot->wedge_org);
		SG_Mark(&bot->watch_since);
		SG_Mark(&bot->stag_since);
		SG_Mark(&bot->wedge_since);
		bot->dither_salt = (unsigned)(rand() & 0x7fffffff);
	}
	if (bot->death_taught && sg_cv.tilt->value > 0.0f)
	{
		/* the caution runs shorter for the better shooter: the same
		 * span the threat clock uses, and for the same reason -- the
		 * skill-4 bot gets his composure back first. Skill is read
		 * through combat's own accessor (0..400) so there is exactly
		 * one skill model in this tree. */
		float sk = (float)SG_CombatSkill(e) / 400.0f;   /* 0..1 */

		if (sk < 0.0f) sk = 0.0f;
		if (sk > 1.0f) sk = 1.0f;
		SG_TimerArm(&bot->tilt_until, bot->tilt_window);
		SG_TimerArm(&bot->tilt_caution_until, SG_TILT_CAUTION +
		    (SG_TILT_CAUTION4 - SG_TILT_CAUTION) * sk);
	}
}






/* An optional speedhook deliberately leaves the sampled surface, but its
 * phase-1/2 deadline and release live in Think_Emit.  Keep using only the
 * loader-validated departure owner while that bounded action is active; if
 * the seed itself is stale or non-routable, normal seedless recovery must win.
 */
static qboolean Think_SpeedhookOwnsSeed(const sg_bot_t *bot)
{
	return bot && sg_rune && bot->speedhook && bot->hook_phase != 0 &&
	       bot->seed >= 0 && bot->seed < sg_rune->hdr.num_seeds &&
	       !(sg_rune->seeds[bot->seed].flags & RSF_TOMBSTONE) &&
	       sg_rune->linked_seed && sg_rune->linked_seed[bot->seed];
}

/*
 * WHERE AM I ON THE RUNE (split from SG_BotThink, 2026-08-11 standards
 * pass; body verbatim): seed relocation on 48 units of travel, the
 * previous-seed memory the dither reads, and the pit trace.
 */
static void Think_TrackSeed(sg_bot_t *bot, edict_t *e, int team)
{
	vec3_t d;
	rune_link_t *commit = NULL;

	if (bot->commit_link >= 0 && sg_rune &&
	    bot->commit_link < sg_rune->hdr.num_links)
		commit = &sg_rune->links[bot->commit_link];
	if (Think_SpeedhookOwnsSeed(bot))
		return;
	/* A proved swim is a feedback traversal between two specific endpoints.
	 * Water-volume seed cells overlap vertically and localization can change
	 * several times along one stroke; treating each sample as a new route step
	 * lets combat/fields replace the command before its shared arrival predicate
	 * is reached. Keep the departure identity for the bounded commitment, just
	 * as an airborne ballistic keeps it through sparse vertical coverage. */
	if (commit && (commit->action == RL_SWIM ||
	               commit->action == RL_LIFT ||
	               commit->action == RL_TELEPORT ||
	               commit->action == RL_DOOR))
		return;
	/* Once a ballistic has submitted its first proved command, its departure
	 * identity belongs to that bounded action until CommitLink judges the first
	 * supported/water boundary.  Relocalizing on the landing can return -1 for
	 * a legitimate incoming-only destination and divert the frame through
	 * Think_Seedless before arrival or DROP recovery is evaluated. */
	if (commit &&
	    ((commit->action == RL_JUMP && bot->jump_started) ||
	     (commit->action == RL_DROP && bot->drop_started)))
		return;

	/* A jump/drop is a single proved action, not a new route decision at each
	 * airborne sample. Tall arcs routinely leave every seed's +/-96 z band;
	 * relocalizing there either returns -1 or snaps to an unrelated floor and
	 * chains a second action in midair. Keep the departure seed until the body
	 * lands, then localize the outcome and argue a fresh step. */
	if (!e->groundentity && e->waterlevel < 2)
	{
		if (bot->rj_phase == 3)
			return;
		if (commit &&
		    (commit->action == RL_JUMP || commit->action == RL_DROP))
			return;
	}

	/* where am I on the rune? */
	VectorSubtract(e->s.origin, bot->last_origin, d);
	if (bot->seed < 0 || VectorLength(d) > 48.0f)
	{
		int was = bot->seed;

		bot->seed = Rune_NearestSeed(sg_rune, e->s.origin);
		VectorCopy(e->s.origin, bot->last_origin);
		if (was >= 0 && bot->seed != was)
		{
			bot->prev_seed = was;
			SG_Mark(&bot->prev_seed_time);
			bot->dither_salt = (unsigned)(rand() & 0x7fffffff);

			/*
			 * PITTRACE (sg_debug): the moment a bot's seed enters the
			 * masked sub-stand region, say who, from where, in what role,
			 * chasing what tactical waypoint. Three flat nulls said the
			 * pit traffic rides neither the waypoint surface nor the
			 * descent steps nor the flag flood -- this line names the
			 * actual carrier of the traffic.
			 */
			if (sg_cv.debug->value && team >= 1 && team <= 2 &&
			    was < sg_rune->hdr.num_seeds && bot->seed >= 0 &&
			    bot->seed < sg_rune->hdr.num_seeds)
			{
				int pti = SG_TeamIdx(team);
				const char *role =
				    (bot->last_role >= 0 && bot->last_role < SG_ROLES)
				    ? sg_role_names[bot->last_role] : "-";

				if (sg_fields.shelf_cliff[pti] &&
				    sg_fields.shelf_cliff[pti][bot->seed] > 0 &&
				    !(sg_fields.shelf_cliff[pti][was] > 0))
					sg_host.dprint("PITTRACE %s role=%s seed %d->%d z=%.0f "
					           "tac_seed=%d tac_role=%d hook=%d\n",
					           e->client->pers.netname,
					           role,
					           was, bot->seed, e->s.origin[2],
					           bot->tac_seed, bot->tac_role,
					           bot->hook_phase);
			}
		}
	}
}

/*
 * Losing the rune is an exceptional body state, not a navigation mode. A
 * blind line to the closest Euclidean seed repeats the through-wall error
 * this path is meant to contain. Probe real player-box clearance around a
 * bias toward the last valid seed, hold that escape briefly, and respawn if
 * topology is not recovered. The timeout makes a sealed pocket finite.
 */
static void Think_Seedless(sg_bot_t *bot, edict_t *e, usercmd_t *cmd,
                           qboolean carrying)
{
	static const float fan_deg[8] = { 0, -45, 45, -90, 90, 180, -135, 135 };
	float preferred;
	int k;

	if (!bot->seedless_active)
	{
		bot->seedless_active = true;
		SG_Mark(&bot->seedless_since);
		bot->seedless_turn_until = 0.0f;
		preferred = e->client->v_angle[YAW];
		if (bot->prev_seed >= 0 && bot->prev_seed < sg_rune->hdr.num_seeds)
		{
			vec3_t back;

			VectorSubtract(sg_rune->seeds[bot->prev_seed].origin,
			               e->s.origin, back);
			if (back[0] * back[0] + back[1] * back[1] > 1.0f)
				preferred = atan2f(back[1], back[0]) * 180.0f /
				            (float)M_PI;
		}
		bot->seedless_yaw = preferred;
	}

	if (SG_AgeAtLeast(bot->seedless_since, carrying ? 12.0f : 6.0f))
	{
		/* A carrier gets twice the recovery budget and is never killed while
		 * secretly retaining the flag. But that must not turn into holding the
		 * match hostage forever in a sealed/off-graph pocket: release the flag
		 * through the normal CTF path, then respawn both objective and body. */
		if (carrying)
		{
			edict_t *flag = ClientHasFlag(e);

			if (flag && flag->item)
				ctf_playerdropflag(e, flag->item);
		}
		sg_host.dprint("SG: %s could not recover rune topology; %srespawning\n",
		               e->client->pers.netname,
		               carrying ? "released flag and " : "");
		Cmd_Kill_f(e);
		bot->seedless_active = false;
		return;
	}

	if (SG_TimerReady(bot->seedless_turn_until))
	{
		float best_score = -1e30f;
		float best_yaw = bot->seedless_yaw;

		preferred = bot->seedless_yaw;
		for (k = 0; k < 8; k++)
		{
			float yaw = preferred + fan_deg[k];
			float rad = yaw * (float)M_PI / 180.0f;
			vec3_t stepdir, end, probe, down;
			trace_t ahead, floor;
			float score;

			stepdir[0] = cosf(rad);
			stepdir[1] = sinf(rad);
			stepdir[2] = 0.0f;
			VectorMA(e->s.origin, 128.0f, stepdir, end);
			ahead = sg_host.trace(e->s.origin, e->mins, e->maxs,
			                      end, e, MASK_PLAYERSOLID);
			score = ahead.fraction - 0.025f * (float)k;
			if (e->groundentity && e->waterlevel < 2 && ahead.fraction > 0.35f)
			{
				VectorMA(e->s.origin, 96.0f * ahead.fraction,
				         stepdir, probe);
				VectorCopy(probe, down);
				down[2] -= 72.0f;
				floor = sg_host.trace(probe, e->mins, e->maxs,
				                      down, e, MASK_PLAYERSOLID);
				if (floor.fraction >= 1.0f)
					score -= 0.5f;
			}
			if (score > best_score)
			{
				best_score = score;
				best_yaw = yaw;
			}
		}
		bot->seedless_yaw = best_yaw;
		SG_TimerArm(&bot->seedless_turn_until, 0.5f);
	}

	cmd->angles[YAW] = ANGLE2SHORT(bot->seedless_yaw) -
	                   e->client->ps.pmove.delta_angles[YAW];
	cmd->angles[PITCH] = -e->client->ps.pmove.delta_angles[PITCH];
	cmd->angles[ROLL] = -e->client->ps.pmove.delta_angles[ROLL];
	cmd->forwardmove = 400;
	if (e->waterlevel >= 2)
		cmd->upmove = 300;
	ClientThink(e, cmd);
}
















void SG_BotThink(sg_bot_t *bot)
{
	edict_t *e = bot->ent;
	const int *goal_field;
	sg_role_t role;
	int team, bestlink = -1;
	qboolean carrying;

	/* the few frame terms still born outside the context: the seeds the
	 * stages take through it, and the one flow flag read here */
	qboolean	precision = false;          /* final approach: no tricks */
	qboolean	hold_post = false;          /* defender at its stand: guard */
	qboolean	rally_hold = false;         /* attacker waiting for a partner */
	qboolean	think_over;                 /* a stage ended the frame */
	float		post_yaw = 0.0f;            /* facing the likeliest approach */
	float		post_sight = -1.0f;         /* clear distance down that facing;
	                                         * WEAPONS.md 2.4-D3 picks the
	                                         * pre-held weapon from it */

	/* the duel terms, read once per frame and priced per candidate seed */
	qboolean	duel = false;               /* combat has a live or fresh target */
	vec3_t		duel_org;                   /* where it is believed to be */
	float		duel_want = 0.0f;           /* range the weapon in hand wants */
	float		duel_expo = 0.0f;           /* what being seen costs, 0 to ~1 */

	/* the think context: the container these frame locals are migrating
	 * into, loaded before each converted stage and read back after */
	sg_think_t	tc;

	/* Every stage receives this object, and Objective itself prices candidate
	 * seeds before PickLink.  Zeroing only cmd left its pointer fields as stack
	 * garbage and made tactics dereference an arbitrary "danger" field after
	 * map transitions.  A frame context is born wholly initialized. */
	memset(&tc, 0, sizeof(tc));
	tc.cmd.msec = 100;
	VectorClear(duel_org);

	if (Think_Dead(bot, e, &tc.cmd))
		return;
	if (!SG_RunePhysicsCompatible())
	{
		/* A runtime cvar change invalidates every stored ballistic witness.
		 * Leave the body in real physics, but submit no navigation and retire
		 * every action that could resume under a different law. */
		if (e->client->hookstate || e->client->hook)
			ctf_hook_abort(e);
		bot->hook_phase = 0;
		bot->hook_link = -1;
		bot->rj_phase = 0;
		bot->nade_phase = 0;
		bot->jump_link = -1;
		bot->jump_started = false;
		bot->drop_link = -1;
		bot->drop_started = false;
		bot->drop_walkoff = false;
		bot->drop_airborne = false;
		bot->drop_recover = false;
		bot->declared_activated = false;
		bot->declared_started = false;
		bot->declared_start_frame = -1;
		bot->declared_touched = false;
		bot->declared_touch_frame = -1;
		bot->declared_triggered = false;
		bot->declared_trigger_frame = -1;
		bot->declared_egress_proof_frame = -1;
		bot->declared_door_retreat = false;
		bot->declared_door_suffix_ms = 0;
		bot->commit_link = -1;
		ClientThink(e, &tc.cmd);
		return;
	}
	/* A rope not represented by the bot action state is stale host state, not
	 * permission to start another proved move. In particular, ClientThink sets
	 * gravity to zero for an attached rope shorter than 50 units; waiting until
	 * after Think_Emit to abort it lets all four JUMP/DROP commands run under a
	 * different law than their witness. Retire it in its own zero-input frame,
	 * then let route selection resume from the resulting authoritative state. */
	if (bot->hook_phase == 0 &&
	    (e->client->hookstate != 0 || e->client->hook != NULL))
	{
		ctf_hook_abort(e);
		bot->hook_link = -1;
		bot->speedhook = false;
		bot->flow_release = false;
		/* This zero-input cleanup frame is outside every serialized action
		 * witness.  If stale host rope state surfaced after a JUMP/DROP/RJ had
		 * already started, resuming that action on the next frame would splice an
		 * unproved 100 ms pause into its trajectory.  Retire the whole action
		 * atomically and let the field select it again from the resulting real
		 * state; the rope defect is not evidence that the graph link itself is
		 * bad, so do not shelf it. */
		bot->commit_link = -1;
		bot->commit_until = 0.0f;
		bot->jump_link = -1;
		bot->jump_started = false;
		bot->drop_link = -1;
		bot->drop_started = false;
		bot->drop_walkoff = false;
		bot->drop_airborne = false;
		bot->drop_recover = false;
		bot->declared_activated = false;
		bot->declared_started = false;
		bot->declared_start_frame = -1;
		bot->declared_touched = false;
		bot->declared_touch_frame = -1;
		bot->declared_triggered = false;
		bot->declared_trigger_frame = -1;
		bot->declared_egress_proof_frame = -1;
		bot->declared_door_retreat = false;
		bot->declared_door_suffix_ms = 0;
		bot->rj_phase = 0;
		bot->nade_phase = 0;
		ClientThink(e, &tc.cmd);
		return;
	}
	Think_RespawnEdge(bot, e);
	bot->death_taught = false;
	if (e->waterlevel == 0)
		bot->swim_air_seed = -1;

	/* my eyes feed the team belief before I decide from it */
	Caco_See(sg_rune, e);
	/* A proved rope owns the complete command before role/objective/approach
	 * stages can arm a grenade, hold, or other mission-side action. It also
	 * remains executable through airborne seed-coverage gaps. */
	if (bot->hook_link >= 0 && !bot->speedhook &&
	    (bot->hook_phase == 2 || bot->hook_phase == 3) &&
	    SG_HookActiveFrame(bot, e))
		return;

	team = e->client->ctf.teamnum;
	/* LMCTF has ONE flag item: "Enemy Flag" (g_items.c:2478). Carrying is
	 * the same inventory test ctf_flagtouch itself makes. */
	{
		static gitem_t *flagitem;
		if (!flagitem)
			flagitem = FindItem("Enemy Flag");
		carrying = flagitem &&
		           e->client->pers.inventory[ITEM_INDEX(flagitem)] > 0;
	}

	role = SG_Role(bot, carrying);

	Think_CarryBookends(bot, e, role, team, carrying);

	/* the context carries the stage contract from here down; the frame
	 * identity loads first, each stage adds what it resolves */
	tc.e = e;
	tc.role = role;
	tc.team = team;
	tc.carrying = carrying;

	Think_LiveWeights(bot, &tc);    /* fills tc.live */
	tc.w = &tc.live;

	tc.support = NULL;
	tc.intercept = NULL;
	Think_InterceptField(role, team, &tc.support, &tc.intercept);

	/* Objective's tactical waypoint search calls Surface_At, so every pricing
	 * input must exist before Objective—not be filled later by PickLink. */
	tc.health = e->health;
	tc.danger = Danger_Field(team);
	tc.push = (role == SG_ROLE_ATTACK &&
	           SG_TimerPending(sg_push_until[SG_TeamIdx(team)]));

	Think_Objective(bot, &tc);

	goal_field = tc.goal_field;


	rally_hold = Think_ApproachBand(bot, &tc);
	bot->term_brake = 1.0f;         /* terminal braking re-earned every frame */
	bot->terminal = false;

	Think_TrackSeed(bot, e, team);
	if ((bot->seed < 0 || goal_field[bot->seed] >= SG_FIELD_INF) &&
	    !Think_SpeedhookOwnsSeed(bot) &&
	    !(bot->seed >= 0 && bot->commit_link >= 0 &&
	      bot->commit_link < sg_rune->hdr.num_links &&
	      (sg_rune->links[bot->commit_link].action == RL_SWIM ||
	       sg_rune->links[bot->commit_link].action == RL_LIFT ||
	       sg_rune->links[bot->commit_link].action == RL_TELEPORT ||
	       sg_rune->links[bot->commit_link].action == RL_DOOR)) &&
	    !(bot->seed >= 0 && !e->groundentity && e->waterlevel < 2 &&
	      (bot->rj_phase == 3 ||
	       (bot->commit_link >= 0 &&
	        bot->commit_link < sg_rune->hdr.num_links &&
	         (sg_rune->links[bot->commit_link].action == RL_JUMP ||
	          sg_rune->links[bot->commit_link].action == RL_DROP)))) &&
	    !(bot->seed >= 0 && bot->commit_link >= 0 &&
	      bot->commit_link < sg_rune->hdr.num_links &&
	      ((sg_rune->links[bot->commit_link].action == RL_JUMP &&
	        bot->jump_started) ||
	       (sg_rune->links[bot->commit_link].action == RL_DROP &&
	        bot->drop_started))))
	{
		Think_Seedless(bot, e, &tc.cmd, carrying);
		return;
	}
	bot->seedless_active = false;

	/*
	 * The precision case: no tricks on the final approach.
	 *
	 * A flag is a thirty-unit box and a hop chain covers eight hundred units a
	 * second; the legacy bots arrived with a second of route left and sailed
	 * straight over the top, lap after lap. The legacy adapter suppressed the
	 * tricks within about 700 units of a must-touch goal (bl_main.c:451-464).
	 * The SLIPGATE field is denominated in real milliseconds of traversal, so
	 * the same idea is stated in time: inside a second and a half of the
	 * objective, run plainly and be able to stop. Speed serves the objective.
	 */
	precision = (goal_field[bot->seed] < 1500);

	/*
	 * Ask combat whether there is a fight on, ONCE, before the fan is walked.
	 * The answer is last frame's -- SG_CombatFrame runs after the movement is
	 * decided, which is the order the constitution requires (combat modifies a
	 * usercmd the body already built) -- and a tenth of a second of staleness
	 * on a believed position that is already up to two seconds old changes
	 * nothing. The carrier is excluded outright: 2.4-D2 is flee, not fight,
	 * and its own repulsion term below already prices contact.
	 */
	if (role != SG_ROLE_CARRY)
		duel = SG_CombatDuel(e, duel_org, &duel_want, &duel_expo);

	/* descend the surface: my seed vs every seed one proven link away.
	 * PickLink reads the think context; these locals are migrating into
	 * it stage by stage, so the context is loaded from them here and the
	 * results read back below until every stage speaks context natively. */
	tc.precision = precision;
	tc.duel = duel;
	VectorCopy(duel_org, tc.duel_org);
	tc.duel_want = duel_want;
	tc.duel_expo = duel_expo;
	tc.rally_hold = rally_hold;

	bestlink = Think_PickLink(bot, &tc);

	/* the context already holds PickLink's results; seed the in/out terms
	 * CommitLink owns and read every one back for the stages below */
	tc.think_over = false;
	tc.hold_post = hold_post;
	tc.post_yaw = post_yaw;
	tc.post_sight = post_sight;

	bestlink = Think_CommitLink(bot, &tc);

	think_over = tc.think_over;
	if (think_over)
		return;
	/*
	 * The surface has a gradient EVERYWHERE. Where the rune is proven, the
	 * gradient is the best outgoing link. Where it is not -- field infinite,
	 * no improving link, graph hole -- the gradient degrades to the local
	 * one: straight at the goal, deflected around whatever the feelers hit.
	 * A player with no knowledge of the map still runs toward the enemy
	 * base; a bot that stands still because its database has a hole is not
	 * descending a surface, it is worshipping a graph.
	 */
	/* the context already holds every movement input except the frame's
	 * view seed; bestlink re-loads because CommitLink may have overridden
	 * the picker's choice through its return value */
	tc.bestlink = bestlink;
	tc.view_yaw = 0.0f;
	tc.view_pitch = 0.0f;

	Think_Move(bot, &tc);
	Think_Emit(bot, &tc);
}



void SG_RunFrame(void)
{
	int i;

	/*
	 * Level changes are detected here rather than by a hook in the spawn
	 * code: the rune and fields were TAG_LEVEL so the engine already freed
	 * them, and level.time restarting is the tell. Same map or different,
	 * every pointer we held is stale the moment this trips.
	 */
	if (SG_TimerPending(sg_last_frame_time) ||
	    (sg_rune && Q_stricmp(sg_rune_map, level.mapname) != 0))
		SG_LevelChange();
	SG_Mark(&sg_last_frame_time);
	if (!SG_RunePhysicsCompatible())
	{
		if (!sg_physics_warned)
			sg_host.dprint("slipgate: movement held: rune v%d requires "
			               "sv_gravity %d, sv_airaccelerate 0, sv_maxvelocity >= 800, and "
			               "want_funky_gravity 0\n",
			               RUNE_VERSION, RUNE_PROOF_GRAVITY);
		sg_physics_warned = true;
	}
	else if (sg_physics_warned)
	{
		sg_host.dprint("slipgate: proof physics restored; movement resumed\n");
		sg_physics_warned = false;
	}
	SG_CombatWhy();
	Danger_Decay();

	if (sg_rune)
	{
		Caco_Frame(sg_rune);
		Fields_Refresh(sg_rune);
	}
	Botfill_Frame();
	/* the scoreline and the clock, before anybody decides a role from them */
	Clock_Frame();

	for (i = 0; i < SG_MAXBOTS; i++)
	{
		edict_t *ent;

		if (!sg_bots[i].active)
			continue;
		ent = sg_bots[i].ent;
		/* Kicks and other engine-owned disconnect paths do not call an SG
		 * removal verb.  Retire the bookkeeping even when the edict has
		 * already gone, otherwise each kick permanently consumes one of the
		 * sixteen SG ownership slots. */
		if (!ent || !ent->inuse || !ent->client ||
		    !(ent->flags & FL_BOT))
		{
			/* ClientDisconnect parks an externally kicked bot but intentionally
			 * knows nothing about engineless-client ownership. Finish that release
			 * before forgetting the SG slot, so FL_BOT/CTF state cannot reach the
			 * next human generation. A live occupant that already lost FL_BOT is a
			 * human replacement and must only be disowned, never cleared. */
			if (ent && ent->client && !ent->inuse &&
			    (ent->flags & FL_BOT))
				SG_FreeClientEdict(ent);
			SG_DisownBot(ent);
			continue;
		}
		if (ent->client->ctf.teamnum != CTF_TEAM_RED &&
		    ent->client->ctf.teamnum != CTF_TEAM_BLUE)
		{
			SG_RetireBotForClient(ent);
			continue;
		}
		SG_BotThink(&sg_bots[i]);
	}
}

/* ---------------------------------------------------------------- spawn */


void SG_LevelChange(void)
{
	int i;

	Danger_Save();      /* the map's lessons outlive the level */
	/* SpawnEntities calls this before TAG_LEVEL/edict teardown. Remove fake
	 * clients through the real disconnect path while their objective state is
	 * still valid; otherwise the next map inherits invisible client slots. */
	SG_RemoveBots();
	/* SpawnEntities resets level.time after this synchronous hook. Zero keeps
	 * the first new-map frame from interpreting that reset as an unhandled
	 * second transition and retiring a bot added by a startup/rcon command. */
	sg_last_frame_time = 0.0f;

	/* rune and fields were TAG_LEVEL -- the engine freed them */
	sg_rune = NULL;
	sg_setup_failed = false;
	sg_human_use = NULL;    /* TAG_LEVEL too: freed with its rune */
	sg_human_live = NULL;
	sg_human_escape = NULL;
	sg_def_post[0] = sg_def_post[1] = NULL;
	sg_def_icept[0] = sg_def_icept[1] = NULL;
	sg_airnext = NULL;
	memset(&sg_fields, 0, sizeof(sg_fields));
	sg_field_red = sg_field_blue = NULL;
	sg_rune_map[0] = 0;

	/*
	 * The clockplay state is stamped in level.time, which restarts at 0 on
	 * the new map -- left alone, the next-read and next-latch stamps from
	 * minute 19 of the old map would gag both for the first nineteen
	 * minutes of this one. The posture goes with them: last map's lead is
	 * not this map's.
	 */
	Role_LevelReset();
	Botfill_Reset();
	Combat_ResetLevel();
	Clock_LevelReset();
	Tilt_LevelReset();      /* TAG_LEVEL as well: the engine freed it */
	for (i = 0; i < SG_MAXBOTS; i++)
	{
		sg_bots[i].active = false;
		sg_bots[i].ent = NULL;
		/*
		 * A grudge is about a PLACE, and seed 137 on the next map is a
		 * different place. Tilt dies with the level -- what survives a
		 * map change is the danger dimension, which is saved above and
		 * is the only thing here that has earned it.
		 */
		sg_bots[i].tilt_lane_n = 0;
		sg_bots[i].tilt_seed = -1;
		sg_bots[i].tilt_killer_seed = -1;
		sg_bots[i].tilt_until = 0.0f;
		sg_bots[i].tilt_caution_until = 0.0f;
		sg_bots[i].tilt_death_time = -1000.0f;
		sg_bots[i].tilt_window = 0.0f;
	}
}
