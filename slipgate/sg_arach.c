/*
 * sg_arach.c -- ARACHNOTRON: the brain with legs. First walking cut.
 *
 * This is deliberately the minimum that PLAYS: load the rune, flood one
 * cost field per flag, spawn a real client, and every ClientThink descend
 * the field toward the enemy flag -- run home when carrying. No combat, no
 * hook, no speed tricks yet. If a bot cannot get base to base on the rune,
 * nothing fancier deserves to exist; this is the walking skeleton the rest
 * grows on, and it is can be compared against the legacy bots from the first
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
#include "slipgate/sg_action.h"
#include "slipgate/sg_combat.h"
#include "slipgate/sg_chat.h"       /* human orders replace the role quota */
#include "slipgate/sg_identity.h"
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
#include "slipgate/sg_rune_install.h"
	#include "slipgate/sg_rune_file.h"
#include "slipgate/sg_rune_mechanism_catalog.h"
#include "slipgate/sg_rune_binding.h"
#include "slipgate/sg_rune_proof.h"
#include "slipgate/sg_compound_publication.h"
#include "slipgate/sg_danger_lease.h"
#include "slipgate/sg_danger_policy.h"
#include "slipgate/sg_sidecar_loader.h"
#include "slipgate/sg_sidecar_store.h"

#define FIELD_INF       0x3fffffff
#include "slipgate/sg_bot.h"
#include "slipgate/sg_hook_live.h"
#include "slipgate/sg_compound_guard_game.h"
#include "slipgate/sg_declared_door_guard.h"
#include "slipgate/sg_drop_live.h"
#include "slipgate/sg_swim_live.h"
#include "slipgate/sg_clock.h"
#include "slipgate/sg_danger.h"
#include "slipgate/sg_defense_shift.h"
#include "slipgate/sg_weights.h"
#include "slipgate/sg_tilt.h"
#include "slipgate/sg_lead.h"
#include "slipgate/sg_move.h"
#include "slipgate/sg_price.h"
#include "slipgate/sg_role_policy.h"
#include "slipgate/sg_route_dither.h"
#include "slipgate/sg_escort_dose.h"
#include "slipgate/sg_role_skew_random.h"
#include "slipgate/sg_descend.h"
#include "slipgate/sg_goal.h"
#include "slipgate/sg_strike_adapter.h"

#include <errno.h>
#include <math.h>


float	sg_grab_time[2] = { -1000.0f, -1000.0f };  /* per team */
float	sg_push_until[2];   /* the conductor's window (sg_wavepush) */
static float	sg_role_skew_until[2];
static int	sg_role_skew[2];
static uint32_t sg_role_skew_random[2];
static int	sg_role_escort_carrier[2] = { -1, -1 };
static qboolean sg_role_escort_on[2] = { true, true };
static uint32_t sg_role_escort_epoch[2];
static sg_strike_adapter_t sg_strike_adapter;
static sg_role_t sg_strike_role_cache[SG_MAXBOTS];
static qboolean sg_strike_role_valid[SG_MAXBOTS];
/* Effective enemy-pressure membership is published with the role/strike
 * snapshot, before serial bot think.  Route coordination must not infer a
 * teammate's live duty from last frame's organic role. */
static qboolean sg_strike_enemy_pressure_cache[SG_MAXBOTS];
static int sg_strike_enemy_pressure_goal_cache[SG_MAXBOTS];
static qboolean sg_strike_frame_ready;
static qboolean sg_strike_telemetry_valid[2];
static uint32_t sg_strike_telemetry_epoch[2];
static sg_strike_phase_t sg_strike_telemetry_phase[2];
static rune_t	*sg_rune;
static qboolean sg_physics_warned;
static float sg_last_frame_time;
static sg_danger_lease_t sg_danger_lease = SG_DANGER_LEASE_INITIALIZER;
static uint16_t sg_danger_selected_port;
static char sg_danger_game_directory[MAX_OSPATH];

static void Role_LevelReset(void)
{
	sg_grab_time[0] = sg_grab_time[1] = -1000.0f;
	sg_push_until[0] = sg_push_until[1] = 0.0f;
	sg_role_skew_until[0] = sg_role_skew_until[1] = 0.0f;
	sg_role_skew[0] = sg_role_skew[1] = 0;
	sg_role_skew_random[0] = SG_RoleSkewRandomInitial(0);
	sg_role_skew_random[1] = SG_RoleSkewRandomInitial(1);
	sg_role_escort_carrier[0] = sg_role_escort_carrier[1] = -1;
	sg_role_escort_on[0] = sg_role_escort_on[1] = true;
	sg_role_escort_epoch[0] = sg_role_escort_epoch[1] = 0;
	SG_StrikeAdapterReset(&sg_strike_adapter);
	memset(sg_strike_role_valid, 0, sizeof(sg_strike_role_valid));
	memset(sg_strike_enemy_pressure_cache, 0,
	       sizeof(sg_strike_enemy_pressure_cache));
	memset(sg_strike_enemy_pressure_goal_cache, 0xff,
	       sizeof(sg_strike_enemy_pressure_goal_cache));
	sg_strike_frame_ready = false;
	memset(sg_strike_telemetry_valid, 0,
	       sizeof(sg_strike_telemetry_valid));
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
/* The four public DPO views share this one TAG_LEVEL allocation. */
static unsigned char *sg_defense_payload;
static unsigned int sg_sidecar_log_mask;

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

typedef enum rune_load_attempt_e
{
	RUNE_LOAD_MISSING = 0,
	RUNE_LOAD_REJECTED,
	RUNE_LOAD_READY
} rune_load_attempt_t;

static rune_load_attempt_t sg_last_rune_load;

void Rune_Free(rune_t *r)
{
	if (!r)
		return;
	SG_CompoundPublicationDestroy(r->compound_publication);
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
	if (r->mechanism_strings)
		sg_host.level_free(r->mechanism_strings);
	if (r->mechanism_plans)
		sg_host.level_free(r->mechanism_plans);
	if (r->mechanism_edges)
		sg_host.level_free(r->mechanism_edges);
	if (r->mechanism_nodes)
		sg_host.level_free(r->mechanism_nodes);
	sg_host.level_free(r);
}

/* Wire validation owns duplicates and route ownership.  This final component
 * is intentionally world-dependent: both live flag stands must localize, and
 * every non-tombstone seed must reach each objective in the preserved graph. */
static const char *Rune_ValidateObjectiveCore(rune_t *r)
{
	int *first_in = NULL, *next_in = NULL, *queue = NULL;
	byte *seen = NULL;
	edict_t *stands[2];
	int roots[2];
	int ns = r->hdr.num_seeds, nl = r->hdr.num_links;
	int i, which;
	const char *failure = NULL;

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

static const char *Rune_BuildOutboundIndexes(rune_t *r)
{
	int i;

	if (!r || r->hdr.num_seeds <= 0 || r->hdr.num_links < 0)
		return "invalid native graph counts";
	r->first_link = sg_host.level_alloc(sizeof(*r->first_link) *
	    (size_t)r->hdr.num_seeds);
	r->next_link = sg_host.level_alloc(sizeof(*r->next_link) *
	    (size_t)(r->hdr.num_links ? r->hdr.num_links : 1));
	r->linked_seed = sg_host.level_alloc((size_t)r->hdr.num_seeds);
	if (!r->first_link || !r->next_link || !r->linked_seed)
		return "outbound-index allocation failure";
	memset(r->linked_seed, 0, (size_t)r->hdr.num_seeds);
	for (i = 0; i < r->hdr.num_seeds; i++)
		r->first_link[i] = -1;
	/* Reverse construction makes each source chain enumerate the original
	 * wire order, preserving equal-cost controller tie behavior exactly. */
	for (i = r->hdr.num_links - 1; i >= 0; i--)
	{
		int source = r->links[i].from;

		r->next_link[i] = r->first_link[source];
		r->first_link[source] = i;
		r->linked_seed[source] = 1;
	}
	for (i = 0; i < r->hdr.num_seeds; i++)
	{
		qboolean tombstone =
		    (r->seeds[i].flags & RSF_TOMBSTONE) != 0;

		if (tombstone == (r->linked_seed[i] != 0))
			return "invalid route-core seed ownership";
	}
	return NULL;
}

static const char *Rune_ReplayDoorPlans(rune_t *r, uint32_t *index_out)
{
	int i;

	for (i = 0; i < r->hdr.num_links; i++)
	{
		rune_link_t *link = &r->links[i];
		sg_rune_mechanism_binding_t binding;
		const char *check = NULL;

		if (link->action != RL_DOOR && link->action != RL_BUTTON_DOOR)
			continue;
		if (!SG_RuneMechanismBindingCapture(r, (uint32_t)i, &binding))
			check = "binding-capture";
		else if (binding.link != link)
			check = "link-identity";
		else if (!SG_OracleValidateBoundDoorLink(
		    r->seeds[link->from].origin, link->anchor,
		    r->seeds[link->to].origin, &binding, link->cost_ms))
			check = "rollout";
		else if (!SG_RuneMechanismBindingCurrent(&binding))
			check = "binding-current";
		if (check)
		{
			sg_host.dprint("rune: door replay reject index=%d check=%s\n",
				i, check);
			if (index_out)
				*index_out = (uint32_t)i;
			return "invalid live declared-door replay";
		}
	}
	return NULL;
}

rune_t *Rune_Load(const char *mapname)
{
	char path[MAX_OSPATH];
	rune_t *rune = NULL;
	sg_rune_authority_t captured;
	sg_rune_authority_t active;
	sg_rune_file_load_result_t load_result;
	sg_compound_publication_result_t compound_result;
	const char *failure = NULL;
	const char *failure_stage = "authority";
	uint32_t failure_index = UINT32_MAX;
	qboolean proof_scope_active = false;
	qboolean accepted = false;
	cvar_t *gamedir;
	const char *game_directory;

	memset(&captured, 0, sizeof(captured));
	memset(&active, 0, sizeof(active));
	memset(&load_result, 0, sizeof(load_result));
	path[0] = '\0';
	sg_last_rune_load = RUNE_LOAD_MISSING;
	SG_HooksInit();
	gamedir = sg_host.cvar("gamedir", "", 0);
	game_directory = gamedir && gamedir->string && gamedir->string[0]
		? gamedir->string : ".";

	if (!SG_RuneAuthorityCapture(mapname, &captured))
	{
		failure_stage = captured.identity_status == SG_IDENTITY_OK
			? "proof-law" : "identity";
		failure = "rune authority unavailable";
		goto cleanup;
	}
	if (!SG_RuneInstallDestinationPath(path, sizeof(path), game_directory,
	    captured.level.mapname))
	{
		failure_stage = "path";
		failure = "path exceeds MAX_OSPATH or has invalid map identity";
		goto cleanup;
	}
	load_result = SG_RuneFileLoad(path, &captured.identity,
		sg_host.level_alloc, sg_host.level_free, &rune);
	if (load_result.status == SG_RUNE_FILE_LOAD_MISSING)
	{
		sg_last_rune_load = RUNE_LOAD_MISSING;
		return NULL;
	}
	if (load_result.status != SG_RUNE_FILE_LOAD_READY || !rune)
	{
		failure_stage = load_result.stage ? load_result.stage : "decode";
		failure = load_result.reason ? load_result.reason
			: "rune artifact rejected";
		failure_index = load_result.index;
		goto cleanup;
	}
	if (!SG_MechCatalogMatches(rune->mechanism_nodes,
	    rune->artifact.num_mechanism_nodes, rune->mechanism_edges,
	    rune->artifact.num_inventory_edges, rune->mechanism_strings,
	    rune->artifact.string_bytes))
	{
		failure_stage = "live-catalog-rebind";
		failure = "decoded mechanism inventory differs from sealed world";
		goto cleanup;
	}
	failure_stage = "outbound-index";
	failure = Rune_BuildOutboundIndexes(rune);
	if (failure)
		goto cleanup;
	failure_stage = "mechanism-rebind";
	if (!SG_RuneMechanismBindingsReady(rune, &failure_index))
	{
		failure = "live mechanism binding rejected";
		goto cleanup;
	}
	if (!SG_RuneProofScopeBegin(rune->artifact.identity.gravity))
	{
		failure_stage = "proof-scope";
		failure = "proof scope busy or invalid";
		goto cleanup;
	}
	proof_scope_active = true;
	failure_stage = "compound-replay";
	compound_result = SG_CompoundPublicationBuild(rune,
		sg_host.level_alloc, sg_host.level_free,
		&rune->compound_publication);
	if (compound_result.status != SG_COMPOUND_PUBLICATION_OK)
	{
		failure = SG_CompoundPublicationStatusName(compound_result.status);
		failure_index = compound_result.link_index;
		goto cleanup;
	}
	failure_stage = "door-replay";
	failure = Rune_ReplayDoorPlans(rune, &failure_index);
	SG_RuneProofScopeEnd();
	proof_scope_active = false;
	if (failure)
		goto cleanup;
	failure_stage = "objective-core";
	failure = Rune_ValidateObjectiveCore(rune);
	if (failure)
		goto cleanup;
	if (!SG_RuneAuthorityCapture(rune->artifact.identity.map_name, &active) ||
	    !SG_RuneAuthorityMatchesArtifact(&active, &rune->artifact))
	{
		failure_stage = "authority-recheck";
		failure = "active identity or proof law drifted during load";
		goto cleanup;
	}
	accepted = true;

cleanup:
	if (proof_scope_active)
		SG_RuneProofScopeEnd();
	if (!accepted)
	{
		sg_last_rune_load = RUNE_LOAD_REJECTED;
		Rune_Free(rune);
		if (failure_index == UINT32_MAX)
			sg_host.dprint("rune: rejected %s stage=%s reason=%s\n",
				path[0] ? path : "<unresolved>", failure_stage,
				failure ? failure : "unknown");
		else
			sg_host.dprint("rune: rejected %s stage=%s reason=%s "
				"index=%u\n", path[0] ? path : "<unresolved>",
				failure_stage, failure ? failure : "unknown",
				(unsigned int)failure_index);
		return NULL;
	}
	sg_last_rune_load = RUNE_LOAD_READY;
	return rune;
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
 * THE WAY TO AIR (observations analysis). The gurgle override kicked
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

typedef struct sg_sidecar_candidates_s
{
	unsigned char *human;
	unsigned char *flag_live;
	unsigned char *escape;
	unsigned char *defense;
	unsigned char *danger;
	int *danger_red;
	int *danger_blue;
	size_t human_size;
	size_t flag_live_size;
	size_t escape_size;
	size_t defense_size;
	size_t danger_size;
	qboolean danger_loaded;
	qboolean danger_persistence;
} sg_sidecar_candidates_t;

static void *Sidecar_LevelAllocate(void *context, size_t size)
{
	(void)context;
	if (size == 0 || size > (size_t)INT_MAX)
		return NULL;
	return sg_host.level_alloc((int)size);
}

static void Sidecar_LevelDeallocate(void *context, void *allocation)
{
	(void)context;
	if (allocation)
		sg_host.level_free(allocation);
}

static void Sidecar_CandidatesRelease(sg_sidecar_candidates_t *candidates)
{
	if (!candidates)
		return;
	Sidecar_LevelDeallocate(NULL, candidates->human);
	Sidecar_LevelDeallocate(NULL, candidates->flag_live);
	Sidecar_LevelDeallocate(NULL, candidates->escape);
	Sidecar_LevelDeallocate(NULL, candidates->defense);
	Sidecar_LevelDeallocate(NULL, candidates->danger);
	Sidecar_LevelDeallocate(NULL, candidates->danger_red);
	Sidecar_LevelDeallocate(NULL, candidates->danger_blue);
	memset(candidates, 0, sizeof(*candidates));
}

static void Sidecar_LogLoad(sg_sidecar_kind_t kind, const char *path,
	const sg_sidecar_load_result_t *result)
{
	unsigned int bit;

	if (!result || kind < 0 || kind >= SG_SIDECAR_KIND_COUNT ||
	    result->diagnostic == SCD_ABSENT)
		return;
	bit = 1U << (unsigned int)kind;
	if (sg_sidecar_log_mask & bit)
		return;
	if (result->diagnostic == SCD_OK)
		return;
	sg_sidecar_log_mask |= bit;
	if (result->plane != SG_SIDECAR_INDEX_NONE ||
	    result->index != SG_SIDECAR_INDEX_NONE)
	{
		sg_host.dprint("slipgate: sidecar %s ignored path=%s stage=%s "
			"diagnostic=%s plane=%u index=%u os=%d\n",
			SG_SidecarKindName(kind), path && path[0] ? path : "<invalid>",
			SG_SidecarStageName(result->stage),
			SG_SidecarDiagnosticName(result->diagnostic),
			(unsigned int)result->plane, (unsigned int)result->index,
			result->os_error);
	}
	else
	{
		sg_host.dprint("slipgate: sidecar %s ignored path=%s stage=%s "
			"diagnostic=%s os=%d\n", SG_SidecarKindName(kind),
			path && path[0] ? path : "<invalid>",
			SG_SidecarStageName(result->stage),
			SG_SidecarDiagnosticName(result->diagnostic),
			result->os_error);
	}
}

static void Sidecar_LogPublished(const char *game_directory,
	sg_sidecar_kind_t kind, const rune_t *r, size_t payload_size)
{
	char path[MAX_OSPATH];
	unsigned int bit;

	if (!r || kind < 0 || kind >= SG_SIDECAR_KIND_COUNT)
		return;
	bit = 1U << (unsigned int)kind;
	if (sg_sidecar_log_mask & bit)
		return;
	path[0] = '\0';
	(void)SG_SidecarPath(path, sizeof(path), game_directory, kind,
		&r->artifact);
	sg_sidecar_log_mask |= bit;
	sg_host.dprint("slipgate: sidecar %s loaded path=%s bytes=%u\n",
		SG_SidecarKindName(kind), path[0] ? path : "<invalid>",
		(unsigned int)payload_size);
}

static sg_sidecar_load_result_t Sidecar_LoadCandidate(
	const char *game_directory,
	sg_sidecar_kind_t kind, const rune_t *r, unsigned char **payload_out,
	size_t *payload_size_out)
{
	sg_sidecar_load_ops_t ops;
	sg_sidecar_load_result_t result;
	char path[MAX_OSPATH];

	memset(&result, 0, sizeof(result));
	result.diagnostic = SCD_INVALID_ARGUMENT;
	result.stage = SCS_ARGUMENT;
	result.plane = SG_SIDECAR_INDEX_NONE;
	result.index = SG_SIDECAR_INDEX_NONE;
	if (!r || !payload_out || !payload_size_out)
		return result;
	*payload_out = NULL;
	*payload_size_out = 0;
	path[0] = '\0';
	(void)SG_SidecarPath(path, sizeof(path), game_directory, kind,
		&r->artifact);
	SG_SidecarDefaultLoadOps(&ops);
	ops.allocate = Sidecar_LevelAllocate;
	ops.deallocate = Sidecar_LevelDeallocate;
	result = SG_SidecarLoadFile(game_directory, kind, &r->artifact,
		r->linked_seed, (size_t)r->hdr.num_seeds, payload_out,
		payload_size_out, &ops);
	Sidecar_LogLoad(kind, path, &result);
	return result;
}

static const char *Danger_GameDirectory(void)
{
	cvar_t *game_cvar = sg_host.cvar("gamedir", "", 0);

	return game_cvar && game_cvar->string && game_cvar->string[0]
		? game_cvar->string : ".";
}

static sg_danger_port_value_t Danger_PortValue(const char *name)
{
	cvar_t *value = sg_host.cvar(name, "0", 0);
	sg_danger_port_value_t result;

	result.string = value ? value->string : NULL;
	result.flags = value ? value->flags : 0;
	return result;
}

static sg_danger_policy_status_t Danger_CurrentPolicy(
	uint16_t *selected_port_out)
{
	sg_danger_port_value_t port;
	sg_danger_port_value_t ip_hostport;
	sg_danger_port_value_t hostport;
	const char *selector;
	sg_danger_policy_status_t selector_status;

	SG_CvarsInit();
	selector = sg_cv.dangerpersistport ? sg_cv.dangerpersistport->string : NULL;
	/* Parse the opt-in before looking up engine port cvars.  The default-off
	 * path must be observationally inert: asking the engine for an absent cvar
	 * creates it, which is needless state mutation when persistence is disabled
	 * (and can hide a misspelled engine configuration). */
	selector_status = SG_DangerPolicySelect(selector, NULL, NULL, NULL,
		selected_port_out);
	if (selector_status == SG_DANGER_POLICY_DISABLED ||
	    selector_status == SG_DANGER_POLICY_BAD_SELECTOR)
		return selector_status;
	port = Danger_PortValue("port");
	ip_hostport = Danger_PortValue("ip_hostport");
	hostport = Danger_PortValue("hostport");
	return SG_DangerPolicySelect(selector, &port, &ip_hostport, &hostport,
		selected_port_out);
}

static void Danger_PersistenceRelease(void)
{
	sg_danger_lease_result_t result;

	result = SG_DangerLeaseRelease(&sg_danger_lease);
	if (result.status != SG_DANGER_LEASE_OK)
	{
		sg_host.dprint("slipgate: danger lease release failed status=%s "
			"os=%d cleanup=%d\n", SG_DangerLeaseReason(result.status),
			result.os_error, result.cleanup_error);
	}
	sg_danger_selected_port = 0;
	sg_danger_game_directory[0] = '\0';
}

static qboolean Danger_PersistenceAcquire(const char *game_directory,
	const rune_t *r)
{
	sg_danger_policy_status_t policy;
	sg_danger_lease_result_t lease;
	uint16_t selected_port = 0;
	char danger_path[MAX_OSPATH];
	char lock_path[MAX_OSPATH];
	char leased_directory[MAX_OSPATH];
	int written;

	if (!game_directory || !r || SG_DangerLeaseHeld(&sg_danger_lease))
		return false;
	policy = Danger_CurrentPolicy(&selected_port);
	if (policy == SG_DANGER_POLICY_DISABLED)
		return false;
	if (policy != SG_DANGER_POLICY_OK)
	{
		sg_host.dprint("slipgate: danger persistence disabled: %s\n",
			SG_DangerPolicyReason(policy));
		return false;
	}
	written = snprintf(leased_directory, sizeof(leased_directory), "%s",
		game_directory);
	if (written < 0 || (size_t)written >= sizeof(leased_directory))
	{
		sg_host.dprint("slipgate: danger persistence disabled: game "
			"directory is too long\n");
		return false;
	}
	if (SG_SidecarPath(danger_path, sizeof(danger_path), game_directory,
		SG_SIDECAR_DANGER, &r->artifact) != SCD_OK)
	{
		sg_host.dprint("slipgate: danger persistence disabled: invalid "
			"authenticated sidecar path\n");
		return false;
	}
	lock_path[0] = '\0';
	lease = SG_DangerLeaseAcquire(&sg_danger_lease, danger_path, lock_path,
		sizeof(lock_path), NULL);
	if (lease.status != SG_DANGER_LEASE_OK)
	{
		sg_host.dprint("slipgate: danger persistence disabled: lease=%s "
			"path=%s os=%d cleanup=%d\n",
			SG_DangerLeaseReason(lease.status),
			lock_path[0] ? lock_path : "<invalid>", lease.os_error,
			lease.cleanup_error);
		return false;
	}
	sg_danger_selected_port = selected_port;
	memcpy(sg_danger_game_directory, leased_directory,
		(size_t)written + 1U);
	sg_host.dprint("slipgate: danger persistence selected port=%u "
		"lock=%s\n", (unsigned int)selected_port, lock_path);
	return true;
}

typedef struct danger_checkpoint_context_s
{
	const rune_t *rune;
	uint64_t revision;
	uint16_t selected_port;
	char game_directory[MAX_OSPATH];
} danger_checkpoint_context_t;

static sg_sidecar_revalidate_t Danger_InstalledRuneMatches(
	const danger_checkpoint_context_t *context, int *os_error_out)
{
	sg_rune_authority_t authority;
	rune_artifact_t installed_artifact;
	sg_rune_file_inspect_status_t status;
	char path[MAX_OSPATH];

	if (os_error_out)
		*os_error_out = 0;
	if (!context || !context->rune ||
	    !SG_RuneAuthorityCapture(context->rune->artifact.identity.map_name,
	        &authority) ||
	    !SG_RuneAuthorityMatchesArtifact(&authority,
	        &context->rune->artifact) ||
	    !SG_RuneInstallDestinationPath(path, sizeof(path),
	        context->game_directory,
	        context->rune->artifact.identity.map_name))
		return SG_SIDECAR_REVALIDATE_DRIFT;
	status = SG_RuneFileInspect(path, &authority.identity,
		&installed_artifact, os_error_out);
	if (status == SG_RUNE_FILE_INSPECT_ERROR)
		return SG_SIDECAR_REVALIDATE_ERROR;
	if (status != SG_RUNE_FILE_INSPECT_MATCH)
		return SG_SIDECAR_REVALIDATE_DRIFT;
	return SG_RuneArtifactsEqual(&installed_artifact,
		&context->rune->artifact)
		? SG_SIDECAR_REVALIDATE_MATCH : SG_SIDECAR_REVALIDATE_DRIFT;
}
static sg_sidecar_revalidate_t Danger_CheckpointRevalidate(void *opaque,
	const rune_artifact_t *artifact, int *os_error_out)
{
	danger_checkpoint_context_t *context = opaque;
	uint16_t selected_port = 0;
	const char *game_directory;

	if (os_error_out)
		*os_error_out = 0;
	if (!context || !context->rune || !artifact ||
	    context->rune != SG_Rune() ||
	    !SG_RuneArtifactsEqual(&context->rune->artifact, artifact) ||
	    !SG_DangerLeaseHeld(&sg_danger_lease) ||
	    sg_danger_game_directory[0] == '\0' ||
	    strcmp(context->game_directory, sg_danger_game_directory) != 0 ||
	    context->selected_port == 0 ||
	    context->selected_port != sg_danger_selected_port ||
	    Danger_Revision() != context->revision ||
	    !Danger_CheckpointPending() ||
	    !SG_RunePhysicsCompatible(context->rune) ||
	    Danger_CurrentPolicy(&selected_port) != SG_DANGER_POLICY_OK ||
	    selected_port != context->selected_port)
		return SG_SIDECAR_REVALIDATE_DRIFT;
	game_directory = Danger_GameDirectory();
	if (strcmp(game_directory, context->game_directory) != 0)
		return SG_SIDECAR_REVALIDATE_DRIFT;
	return Danger_InstalledRuneMatches(context, os_error_out);
}

void SG_DangerCheckpoint(const char *event)
{
	danger_checkpoint_context_t context;
	sg_sidecar_store_result_t result;
	unsigned char *payload = NULL;
	unsigned char *encoded = NULL;
	const rune_t *r = SG_Rune();
	const char *current_game_directory;
	size_t payload_capacity;
	size_t payload_size = 0;
	size_t encoded_capacity = 0;
	size_t encoded_size = 0;
	uint64_t revision = 0;
	int written;

	SG_HooksInit();
	if (!r || !Danger_IsActive() || !Danger_PersistenceEnabled() ||
		!Danger_IsDirty() || !SG_DangerLeaseHeld(&sg_danger_lease))
		return;
	if (!Danger_CheckpointPending())
	{
		/* Dirty learned state is intentionally not serialized under a drifted
		 * identity or movement law.  Say that it was retained only in memory so
		 * an operator never mistakes a quiet shutdown for a durable checkpoint. */
		sg_host.dprint("slipgate: danger checkpoint retained event=%s "
			"revision=%llu reason=identity-or-physics-drift\n",
			event ? event : "unknown",
			(unsigned long long)Danger_Revision());
		return;
	}
	current_game_directory = Danger_GameDirectory();
	if (sg_danger_game_directory[0] == '\0' ||
	    strcmp(current_game_directory, sg_danger_game_directory) != 0)
	{
		sg_host.dprint("slipgate: danger checkpoint retained event=%s "
			"revision=%llu reason=game-directory-drift\n",
			event ? event : "unknown",
			(unsigned long long)Danger_Revision());
		return;
	}
	payload_capacity = Danger_PayloadBytes(r);
	if (!payload_capacity || payload_capacity > (size_t)INT_MAX ||
		SG_SidecarFileSize(SG_SIDECAR_DANGER, &r->artifact,
			&encoded_capacity) != SCD_OK ||
		encoded_capacity > (size_t)INT_MAX)
	{
		sg_host.dprint("slipgate: danger checkpoint skipped event=%s "
			"reason=invalid-bound-size\n", event ? event : "unknown");
		return;
	}
	payload = sg_host.game_alloc((int)payload_capacity);
	encoded = sg_host.game_alloc((int)encoded_capacity);
	if (!payload || !encoded)
	{
		sg_host.dprint("slipgate: danger checkpoint failed event=%s "
			"stage=allocation\n", event ? event : "unknown");
		goto cleanup;
	}
	if (!Danger_CapturePayload(payload, payload_capacity, &payload_size,
		&revision) || payload_size != payload_capacity ||
		SG_SidecarEncode(SG_SIDECAR_DANGER, &r->artifact,
			r->linked_seed, (size_t)r->hdr.num_seeds, payload,
			payload_size, encoded, encoded_capacity, &encoded_size) != SCD_OK ||
		encoded_size != encoded_capacity)
	{
		sg_host.dprint("slipgate: danger checkpoint failed event=%s "
			"stage=encode\n", event ? event : "unknown");
		goto cleanup;
	}
	memset(&context, 0, sizeof(context));
	context.rune = r;
	context.revision = revision;
	context.selected_port = sg_danger_selected_port;
	written = snprintf(context.game_directory,
		sizeof(context.game_directory), "%s", sg_danger_game_directory);
	if (written < 0 || (size_t)written >= sizeof(context.game_directory))
	{
		sg_host.dprint("slipgate: danger checkpoint failed event=%s "
			"stage=path\n", event ? event : "unknown");
		goto cleanup;
	}
	result = SG_SidecarStoreFile(context.game_directory,
		SG_SIDECAR_DANGER, &r->artifact, r->linked_seed,
		(size_t)r->hdr.num_seeds, encoded, encoded_size,
		Danger_CheckpointRevalidate, &context, NULL);
	if (result.diagnostic == SCD_OK && result.stage == SCS_DONE &&
		result.replacement_complete && result.durability_complete &&
		Danger_MarkCommitted(revision))
	{
		sg_host.dprint("slipgate: danger checkpoint committed event=%s "
			"revision=%llu bytes=%u\n", event ? event : "unknown",
			(unsigned long long)revision, (unsigned int)encoded_size);
	}
	else
	{
		sg_host.dprint("slipgate: danger checkpoint retained event=%s "
			"revision=%llu stage=%s diagnostic=%s os=%d close=%d "
			"cleanup=%d replaced=%d durable=%d\n",
			event ? event : "unknown", (unsigned long long)revision,
			SG_SidecarStageName(result.stage),
			SG_SidecarDiagnosticName(result.diagnostic), result.os_error,
			result.close_error, result.cleanup_error,
			result.replacement_complete, result.durability_complete);
	}

cleanup:
	if (encoded)
		sg_host.game_free(encoded);
	if (payload)
		sg_host.game_free(payload);
}

void SG_DangerPersistenceReset(void)
{
	Danger_PersistenceRelease();
	Danger_ResetLevel();
}

static int *Air_Build(const rune_t *r)
{
	int i, li, qhead = 0, qtail = 0;
	int n;
	int *airnext = NULL, *dist = NULL, *incoming = NULL;
	int *next_incoming = NULL, *queue = NULL;
	qboolean complete;

	if (!r || r->hdr.num_seeds <= 0)
		return NULL;
	n = r->hdr.num_seeds;
	airnext = sg_host.level_alloc(sizeof(int) * (size_t)n);
	dist = sg_host.level_alloc(sizeof(int) * n);
	incoming = sg_host.level_alloc(sizeof(int) * n);
	next_incoming = sg_host.level_alloc(sizeof(int) *
	    (r->hdr.num_links > 0 ? r->hdr.num_links : 1));
	queue = sg_host.level_alloc(sizeof(int) * n);
	if (!airnext || !dist || !incoming || !next_incoming || !queue)
		goto cleanup;
	for (i = 0; i < n; i++)
	{
		airnext[i] = -1;
		incoming[i] = -1;
		if ((r->seeds[i].flags & RSF_WATER) &&
		    !(r->seeds[i].flags & RSF_TOMBSTONE))
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
	 * O(seeds+links), converges for the full format bounds, and chooses a shortest
	 * number-of-strokes escape. */
	for (li = 0; li < r->hdr.num_links; li++)
	{
		const rune_link_t *l = &r->links[li];

		next_incoming[li] = -1;
		if (l->action != RL_SWIM ||
		    !(r->seeds[l->from].flags & RSF_WATER))
			continue;
		next_incoming[li] = incoming[l->to];
		incoming[l->to] = li;
	}
	while (qhead < qtail)
	{
		int to = queue[qhead++];

		for (li = incoming[to]; li >= 0; li = next_incoming[li])
		{
			const rune_link_t *l = &r->links[li];

			if (dist[l->from] != 0x7fffff)
				continue;
			dist[l->from] = dist[to] + 1;
			airnext[l->from] = to;
			queue[qtail++] = l->from;
		}
	}

cleanup:
	complete = airnext && dist && incoming && next_incoming && queue;
	if (dist)
		sg_host.level_free(dist);
	if (incoming)
		sg_host.level_free(incoming);
	if (next_incoming)
		sg_host.level_free(next_incoming);
	if (queue)
		sg_host.level_free(queue);
	if (!complete)
	{
		if (airnext)
			sg_host.level_free(airnext);
		return NULL;
	}
	return airnext;
}

qboolean SG_LevelSetup(void)
{
	rune_t *candidate = NULL;
	int *candidate_air = NULL;
	sg_sidecar_candidates_t sidecars;
	sg_field_setup_inputs_t field_inputs;
	sg_sidecar_load_result_t danger_load;
	sg_rune_authority_t active;
	cvar_t *game_cvar;
	const char *game_directory;
	uint32_t mechanism_failure_index = UINT32_MAX;
	qboolean fields_ready = false;

	memset(&sidecars, 0, sizeof(sidecars));
	memset(&field_inputs, 0, sizeof(field_inputs));
	memset(&danger_load, 0, sizeof(danger_load));
	danger_load.diagnostic = SCD_ABSENT;
	SG_HooksInit();     /* the host table, before any module reaches out */
	if (sg_setup_failed)
		return false;

	if (sg_rune)
	{
		if (strcmp(sg_rune_map, level.mapname) == 0)
		{
			if (SG_RunePhysicsCompatible(sg_rune))
				return true;
			sg_host.dprint("slipgate: setup held: active identity or "
			               "physics law differs from loaded artifact\n");
			return false;
		}
		sg_host.dprint("slipgate: disabled: loaded rune identity differs "
		               "from active map case\n");
		return false;
	}
	SG_StrikeAdapterReset(&sg_strike_adapter);
	memset(sg_strike_role_valid, 0, sizeof(sg_strike_role_valid));
	memset(sg_strike_enemy_pressure_cache, 0,
	       sizeof(sg_strike_enemy_pressure_cache));
	memset(sg_strike_enemy_pressure_goal_cache, 0xff,
	       sizeof(sg_strike_enemy_pressure_goal_cache));
	sg_strike_frame_ready = false;
	memset(sg_strike_telemetry_valid, 0,
	       sizeof(sg_strike_telemetry_valid));

	/* once per map, ahead of the rune: a map with no rune still answers
	 * `sv sg weights`, and the admin editing the file between maps expects
	 * the next map to be running it */
	Weights_Load();
	SG_DangerPersistenceReset();
	/* Clear the prior level's published views before building this level's
	 * authenticated sidecar candidates. */
	sg_human_use = NULL;
	sg_human_live = NULL;
	sg_human_escape = NULL;
	sg_def_post[0] = sg_def_post[1] = NULL;
	sg_def_icept[0] = sg_def_icept[1] = NULL;
	sg_defense_payload = NULL;
	sg_airnext = NULL;
	memset(&sg_fields, 0, sizeof(sg_fields));

	candidate = Rune_Load(level.mapname);
	if (!candidate)
	{
		if (sg_last_rune_load == RUNE_LOAD_MISSING)
			sg_host.dprint("slipgate: no rune for %s -- run 'sv rune' first\n",
			               level.mapname);
		else
			sg_host.dprint("slipgate: disabled: rejected rune for %s; "
			               "see rune diagnostic above\n", level.mapname);
		return false;
	}
	game_cvar = sg_host.cvar("gamedir", "", 0);
	game_directory = game_cvar && game_cvar->string && game_cvar->string[0]
	    ? game_cvar->string : ".";
	Sidecar_LoadCandidate(game_directory, SG_SIDECAR_HUMAN, candidate,
		&sidecars.human, &sidecars.human_size);
	Sidecar_LoadCandidate(game_directory, SG_SIDECAR_FLAG_LIVE, candidate,
		&sidecars.flag_live, &sidecars.flag_live_size);
	Sidecar_LoadCandidate(game_directory, SG_SIDECAR_ESCAPE, candidate,
		&sidecars.escape, &sidecars.escape_size);
	Sidecar_LoadCandidate(game_directory, SG_SIDECAR_DEFENSE, candidate,
		&sidecars.defense, &sidecars.defense_size);
	sidecars.danger_persistence = Danger_PersistenceAcquire(game_directory,
		candidate);
	if (sidecars.danger_persistence)
	{
		danger_load = Sidecar_LoadCandidate(game_directory,
			SG_SIDECAR_DANGER, candidate, &sidecars.danger,
			&sidecars.danger_size);
		if (danger_load.diagnostic == SCD_OK)
		{
			size_t plane_bytes = (size_t)candidate->hdr.num_seeds *
				sizeof(*sidecars.danger_red);

			sidecars.danger_red = Sidecar_LevelAllocate(NULL, plane_bytes);
			sidecars.danger_blue = Sidecar_LevelAllocate(NULL, plane_bytes);
			if (!sidecars.danger_red || !sidecars.danger_blue ||
				!Danger_DecodeCandidate(candidate, sidecars.danger,
					sidecars.danger_size, sidecars.danger_red,
					sidecars.danger_blue,
					(size_t)candidate->hdr.num_seeds))
			{
				sg_host.dprint("slipgate: sidecar DNG ignored "
					"stage=integration diagnostic=SCD_INTERNAL_ERROR; "
					"persistence disabled for this level\n");
				Danger_PersistenceRelease();
				sidecars.danger_persistence = false;
			}
			else
				sidecars.danger_loaded = true;
		}
		else if (danger_load.diagnostic != SCD_ABSENT)
		{
			/* A failed read is not permission to replace the existing file with
			 * a fresh model later in the same level.  Keep gameplay ephemeral. */
			Danger_PersistenceRelease();
			sidecars.danger_persistence = false;
		}
	}
	if (sidecars.defense)
	{
		size_t plane_size = (size_t)candidate->hdr.num_seeds;

		/* Decode already proves the exact DPO shape. Keep this boundary
		 * defensive so no future loader can hand field construction a partial
		 * or transposed candidate. */
		if (sidecars.defense_size != plane_size * SG_DPO_PLANE_COUNT)
		{
			sg_host.dprint("slipgate: sidecar DPO ignored stage=integration "
			               "diagnostic=SCD_INTERNAL_ERROR\n");
			Sidecar_LevelDeallocate(NULL, sidecars.defense);
			sidecars.defense = NULL;
			sidecars.defense_size = 0;
		}
		else
		{
			field_inputs.dpo[SG_DPO_POST_RED] = sidecars.defense;
			field_inputs.dpo[SG_DPO_POST_BLUE] =
				sidecars.defense + plane_size;
			field_inputs.dpo[SG_DPO_INTERCEPT_RED] =
				sidecars.defense + plane_size * 2U;
			field_inputs.dpo[SG_DPO_INTERCEPT_BLUE] =
				sidecars.defense + plane_size * 3U;
		}
	}
	candidate_air = Air_Build(candidate);
	if (!candidate_air)
	{
		sg_host.dprint("slipgate: air-index setup failed; disabled until "
		               "the next level\n");
		sg_setup_failed = true;
		goto fail;
	}
	/* Fields_Setup writes sg_fields while consuming only the local candidate.
	 * Those fields are not usable until sg_rune is published below; every
	 * failure path zeros the structure before releasing the candidate. */
	if (!Fields_Setup(candidate, &field_inputs))
	{
		sg_host.dprint("slipgate: field setup failed (no flags?); "
		               "disabled until the next level\n");
		sg_setup_failed = true;
		goto fail;
	}
	fields_ready = true;
	if (!SG_RuneAuthorityCapture(candidate->artifact.identity.map_name,
	    &active) ||
	    !SG_RuneAuthorityMatchesArtifact(&active, &candidate->artifact))
	{
		sg_host.dprint("slipgate: field setup discarded: active identity or "
		               "proof law drifted before publication\n");
		sg_setup_failed = true;
		goto fail;
	}
	{
		sg_compound_publication_result_t compound_result =
			SG_CompoundPublicationRevalidate(candidate);

		if (compound_result.status != SG_COMPOUND_PUBLICATION_OK)
		{
			sg_host.dprint("slipgate: compound publication discarded: "
			               "status=%s index=%u\n",
			               SG_CompoundPublicationStatusName(
			                   compound_result.status),
			               (unsigned int)compound_result.link_index);
			sg_setup_failed = true;
			goto fail;
		}
	}
	/* Rebind at the publication boundary as one transaction.  A failed
	 * incarnation or topology check leaves sg_rune unpublished and follows the
	 * ordinary candidate cleanup path. */
	if (!SG_RuneMechanismBindingsReady(candidate,
	        &mechanism_failure_index))
	{
		sg_host.dprint("slipgate: mechanism publication discarded: index=%u\n",
		    (unsigned int)mechanism_failure_index);
		sg_setup_failed = true;
		goto fail;
	}

	/* This is the sole synchronous publication transaction.  Danger_Publish
	 * requires SG_Rune() identity, so expose the candidate only within this
	 * call and roll it back before any frame can observe a failure. */
	sg_rune = candidate;
	candidate = NULL;
	if (!Danger_Publish(sg_rune,
		sidecars.danger_loaded ? sidecars.danger_red : NULL,
		sidecars.danger_loaded ? sidecars.danger_blue : NULL,
		sidecars.danger_loaded ? (size_t)sg_rune->hdr.num_seeds : 0,
		sidecars.danger_persistence))
	{
		sg_host.dprint("slipgate: danger publication failed; disabled until "
			"the next level\n");
		candidate = sg_rune;
		sg_rune = NULL;
		sg_setup_failed = true;
		goto fail;
	}
	sg_airnext = candidate_air;
	candidate_air = NULL;
	sg_human_use = sidecars.human;
	sidecars.human = NULL;
	sg_human_live = sidecars.flag_live;
	sidecars.flag_live = NULL;
	sg_human_escape = sidecars.escape;
	sidecars.escape = NULL;
	sg_defense_payload = sidecars.defense;
	sidecars.defense = NULL;
	if (sg_defense_payload)
	{
		size_t plane_size = (size_t)sg_rune->hdr.num_seeds;

		sg_def_post[0] = sg_defense_payload;
		sg_def_post[1] = sg_defense_payload + plane_size;
		sg_def_icept[0] = sg_defense_payload + plane_size * 2U;
		sg_def_icept[1] = sg_defense_payload + plane_size * 3U;
	}
	memcpy(sg_rune_map, sg_rune->artifact.identity.map_name,
	    sizeof(sg_rune_map));
	if (sg_human_use)
		Sidecar_LogPublished(game_directory, SG_SIDECAR_HUMAN, sg_rune,
			sidecars.human_size);
	if (sg_human_live)
		Sidecar_LogPublished(game_directory, SG_SIDECAR_FLAG_LIVE, sg_rune,
			sidecars.flag_live_size);
	if (sg_human_escape)
		Sidecar_LogPublished(game_directory, SG_SIDECAR_ESCAPE, sg_rune,
			sidecars.escape_size);
	if (sg_defense_payload)
		Sidecar_LogPublished(game_directory, SG_SIDECAR_DEFENSE, sg_rune,
			sidecars.defense_size);
	if (sidecars.danger_loaded)
		Sidecar_LogPublished(game_directory, SG_SIDECAR_DANGER, sg_rune,
			sidecars.danger_size);
	Sidecar_LevelDeallocate(NULL, sidecars.danger);
	Sidecar_LevelDeallocate(NULL, sidecars.danger_red);
	Sidecar_LevelDeallocate(NULL, sidecars.danger_blue);
	sidecars.danger = NULL;
	sidecars.danger_red = NULL;
	sidecars.danger_blue = NULL;
	Escape_Load(sg_rune_map); /* map-keyed configuration, not a graph sidecar */
	Caco_Reset();

	sg_host.dprint("slipgate: rune ready %s, %d seeds, %d links, "
	               "%u mechanism nodes, %u plans, gravity %.0f, all fields "
	               "up\n", sg_rune->artifact.identity.map_name,
	               sg_rune->hdr.num_seeds, sg_rune->hdr.num_links,
	               (unsigned int)sg_rune->artifact.num_mechanism_nodes,
	               (unsigned int)sg_rune->artifact.num_mechanism_plans,
	               sg_rune->artifact.identity.gravity);
	return true;

fail:
	Danger_PersistenceRelease();
	Danger_ResetLevel();
	Sidecar_CandidatesRelease(&sidecars);
	if (candidate_air)
		sg_host.level_free(candidate_air);
	Rune_Free(candidate);
	/* Fields allocations are TAG_LEVEL and remain owned by the level allocator;
	 * clearing every published pointer makes the failed attempt unusable and the
	 * setup-failure latch prevents repeated allocation until teardown. */
	if (fields_ready || sg_setup_failed)
		memset(&sg_fields, 0, sizeof(sg_fields));
	sg_airnext = NULL;
	return false;
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
static qboolean SG_BotCarrying(edict_t *e)
{
	static gitem_t *flagitem;

	if (!e || !e->client)
		return false;
	if (!flagitem)
		flagitem = FindItem("Enemy Flag");
	return flagitem &&
	       e->client->pers.inventory[ITEM_INDEX(flagitem)] > 0;
}

static sg_role_t SG_Role(sg_bot_t *bot, qboolean carrying)
{
	int team = bot->ent->client->ctf.teamnum;
	int size = 0, defenders_wanted, my_rank = 0, i;
	int my_client = (int)(bot->ent - g_edicts) - 1;
	int my_slot = (int)(bot - sg_bots);
	unsigned char live_team[SG_MAXBOTS];
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

	memset(live_team, 0, sizeof(live_team));
	for (i = 0; i < SG_MAXBOTS; i++)
	{
		if (!sg_bots[i].active || !sg_bots[i].ent || !sg_bots[i].ent->inuse)
			continue;
		if (sg_bots[i].ent->client->ctf.teamnum != team)
			continue;
		/* A corpse cannot occupy a defender quota.  Live teammates compress the
		 * stable slot order until it respawns; the next frame then admits it at
		 * its ordinary rank again. */
		if (sg_bots[i].ent->deadflag == DEAD_NO &&
		    sg_bots[i].ent->health > 0)
			live_team[i] = 1;
	}
	my_rank = SG_RoleLiveRank(live_team, SG_MAXBOTS, my_slot, &size);

	/*
	 * STRATEGY BY GAME STATE, the way the demos play it. Four states from
	 * two common-knowledge bits (each flag home or astray -- the HUD tells
	 * everyone), and each state names its shape. The old static 2-in-5
	 * quota played every state identically; games are won by playing them
	 * differently.
	 *
	 *   both home        2 DEFEND, rest ATTACK. The base shape.
	 *   theirs astray    2 DEFEND (return pressure is coming),
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
		qboolean have_carrier = own->client >= 0;

		defenders_wanted = ours_astray ? 1 : 2;

		/*
		 * TEAM SKEW (sg_teamskew) breaks team-mirror symmetry.
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
				sg_role_skew_random[ts] =
				    SG_RoleSkewRandomNext(sg_role_skew_random[ts]);
				sg_role_skew[ts] =
				    SG_RoleSkewRandomValue(sg_role_skew_random[ts]);
				sg_role_skew_random[ts] =
				    SG_RoleSkewRandomNext(sg_role_skew_random[ts]);
				SG_TimerArm(&sg_role_skew_until[ts],
				    SG_RoleSkewRandomInterval(sg_role_skew_random[ts]));
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
			 * DUEL ROLES (sg_duelroles). The hardcoded 1
			 * was a catch-22 the 268-283 census convicted: dw stuck
			 * at 1 in 131/138 transitions, zero duel caps in the
			 * observed matches, while the statue defender sat p90=173u from
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
		 * map. The observations census reads accordingly -- no escort at
		 * all for 30-100% of carry seconds, median distance 430-1860
		 * when one existed, and mactf06's entire 28-second carry walked
		 * naked. Every bot runs the same argmin over the same shared
		 * positions; the incumbent gets a 300-unit head start so the
		 * job does not flap between two equidistant mates.
		 */
		if (have_carrier && own->client != my_client)
		{
			const int ti = SG_TeamIdx(team);
			const int *home = team == CTF_TEAM_RED ? sg_fields.to_red_flag
			                                          : sg_fields.to_blue_flag;
			int best_score = 0;
			int best_i = -1, k;

			for (k = 0; k < SG_MAXBOTS; k++)
			{
				int route_cost, score;

				if (!sg_bots[k].active || !sg_bots[k].ent ||
				    !sg_bots[k].ent->inuse)
					continue;
				if (sg_bots[k].ent->client->ctf.teamnum != team)
					continue;
				/* Use only the team-belief carrier flood while it is current.  An
				 * unknown teammate position falls back to the public capture stand;
				 * the exact carrier edict origin never enters assignment. */
				if (sg_bots[k].seed < 0 ||
				    sg_bots[k].seed >= SG_Rune()->hdr.num_seeds ||
				    !home)
					continue;
				route_cost = SG_EscortRouteCost(
				    sg_fields.our_carrier_valid[ti],
				    sg_fields.our_carrier[ti]
				        ? sg_fields.our_carrier[ti][sg_bots[k].seed]
				        : SG_FIELD_INF,
				    home[sg_bots[k].seed]);
				if (!SG_AutonomousEscortCandidate(live_team[k],
				        SG_RoleOutsideDefenderQuota(live_team, SG_MAXBOTS,
				            k, defenders_wanted),
				        (int)(sg_bots[k].ent - g_edicts) - 1 == own->client,
				        SG_ChatOrderedRole(sg_bots[k].ent), route_cost))
					continue;
				score = SG_EscortAssignmentScore(route_cost,
				    sg_bots[k].last_role == (int)SG_ROLE_ESCORT);
				if (score >= 0 &&
				    (best_i < 0 || score < best_score ||
				     (score == best_score && k < best_i)))
				{
					best_score = score;
					best_i = k;
				}
			}
			if (best_i >= 0 && &sg_bots[best_i] == bot)
			{
				/*
				 * ESCORT DOSE (sg_escortdose,
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
					sg_role_escort_epoch[et]++;
					sg_role_escort_on[et] = SG_EscortDoseEnabled(et, cc,
					    sg_role_escort_epoch[et],
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




static const char *StrikePhaseName(sg_strike_phase_t phase)
{
	switch (phase)
	{
	case SG_STRIKE_IDLE:
		return "IDLE";
	case SG_STRIKE_ARM:
		return "ARM";
	case SG_STRIKE_FORM:
		return "FORM";
	case SG_STRIKE_GO:
		return "GO";
	case SG_STRIKE_EGRESS:
		return "EGRESS";
	default:
		return "UNKNOWN";
	}
}

/* Edge-only production evidence for the reducer's synchronized phase.  The
 * state cache is updated even with sg_debug off, so enabling diagnostics does
 * not replay a stale transition; equal epoch/phase frames never print. */
static void StrikeTelemetryEdge(int team_index)
{
	const sg_strike_team_t *team;
	qboolean edge;

	if (team_index < 0 || team_index >= 2)
		return;
	team = SG_StrikeAdapterTeam(&sg_strike_adapter, team_index);
	if (!team)
		return;
	edge = !sg_strike_telemetry_valid[team_index] ||
	    team->epoch != sg_strike_telemetry_epoch[team_index] ||
	    team->phase != sg_strike_telemetry_phase[team_index];
	if (edge && sg_cv.debug && sg_cv.debug->value)
		sg_host.dprint("STRIKE_EDGE team=%d epoch=%u phase=%s "
		               "members=0x%08x hold=0x%08x rush=0x%08x "
		               "carrier=%d\n",
		               SG_TeamFromIdx(team_index), (unsigned)team->epoch,
		               StrikePhaseName(team->phase),
		               (unsigned)team->member_mask,
		               (unsigned)team->hold_mask,
		               (unsigned)team->rush_mask,
		               team->carrier_slot);
	sg_strike_telemetry_valid[team_index] = true;
	sg_strike_telemetry_epoch[team_index] = team->epoch;
	sg_strike_telemetry_phase[team_index] = team->phase;
}

static void StrikeFrameInit(sg_strike_frame_t *frame)
{
	int slot;

	memset(frame, 0, sizeof(*frame));
	frame->now = level.time;
	frame->own_flag_home = 0;
	frame->enemy_flag_home = 0;
	frame->carrier_slot = -1;
	for (slot = 0; slot < SG_STRIKE_MAX_SLOTS; slot++)
	{
		frame->slot[slot].weapon_tier = 0;
		frame->slot[slot].enemy_flag_goal_ms = -1;
		frame->slot[slot].recover_goal_ms = -1;
		frame->slot[slot].carrier_goal_ms = -1;
	}
}

static int StrikeFieldCost(const int *field, int seed)
{
	if (!field || !SG_Rune() || seed < 0 ||
	    seed >= SG_Rune()->hdr.num_seeds || field[seed] >= SG_FIELD_INF)
		return -1;
	return field[seed];
}

static qboolean StrikeAttackEligible(sg_role_t role, qboolean carrying,
	int ordered_role)
{
	/* Physical possession outranks every standing order.  Otherwise a human
	 * order is the complete role authority promised by sg_chat.h; admitting
	 * that bot to the autonomous strike roster would overwrite its route a few
	 * stages later. */
	if (carrying)
		return true;
	if (ordered_role >= 0)
		return false;
	return role != SG_ROLE_DEFEND;
}

static const int *StrikeEnemyField(int team)
{
	int ti = SG_TeamIdx(team);
	int enemy = SG_TeamIdx(SG_EnemyTeam(team));
	const int *fixed = team == CTF_TEAM_RED ? sg_fields.to_blue_flag
	                                      : sg_fields.to_red_flag;

	/* PRESS/BREACH/CLEAR remain enemy-base duties after our pickup.  Carrier
	 * support has its own ESCORT field; using the moving flag belief here
	 * silently turns every strike member into another escort, and its unknown
	 * position fallback can point the entire strike home. */
	if (SG_AttackObjectiveUsesFixedStand(
	        sg_caco_team_belief.carrier[ti].client))
		return fixed;
	if (sg_fields.to_flag_now[ti][enemy])
		return sg_fields.to_flag_now[ti][enemy];
	return fixed;
}

static const int *StrikeOwnField(int team)
{
	int ti = SG_TeamIdx(team);

	if (sg_fields.to_flag_now[ti][ti])
		return sg_fields.to_flag_now[ti][ti];
	return team == CTF_TEAM_RED ? sg_fields.to_red_flag
	                            : sg_fields.to_blue_flag;
}

/* A carrier always returns to the physical capture stand.  The dynamic own
 * flag field above belongs to RECOVER: during a standoff it leads toward the
 * enemy thief, which is exactly the wrong route for our carrier. */
static const int *StrikeHomeField(int team)
{
	return team == CTF_TEAM_RED ? sg_fields.to_red_flag
	                            : sg_fields.to_blue_flag;
}

static const int *StrikeCarrierField(int team)
{
	int ti = SG_TeamIdx(team);

	if (sg_fields.our_carrier[ti] && sg_fields.our_carrier_valid[ti])
		return sg_fields.our_carrier[ti];
	/* Carrier belief is refreshed after the pickup frame.  Until its
	 * ahead-of-carrier flood is ready, send the escort toward the capture
	 * stand rather than toward a stolen own flag and the enemy thief. */
	return StrikeHomeField(team);
}

static const int *StrikeDutyField(sg_strike_duty_t duty, int team)
{
	switch (duty)
	{
	case SG_STRIKE_DUTY_CARRY:
		return StrikeHomeField(team);
	case SG_STRIKE_DUTY_RECOVER:
		return StrikeOwnField(team);
	case SG_STRIKE_DUTY_ESCORT:
		return StrikeCarrierField(team);
	case SG_STRIKE_DUTY_BREACH:
	case SG_STRIKE_DUTY_CLEAR:
	case SG_STRIKE_DUTY_PRESS:
		return StrikeEnemyField(team);
	case SG_STRIKE_DUTY_NONE:
	default:
		return NULL;
	}
}

static qboolean StrikeApplyDutyRoute(sg_think_t *tc,
	sg_strike_duty_t duty, int team)
{
	const int *route;

	if (!tc)
		return false;
	/* Objective has already resolved the effective ESCORT mission.  When its
	 * live carrier/threat inputs produced a moving formation station, that
	 * exact field is the coordinator's escort route too.  Replacing it here
	 * with the generic carrier flood silently disabled interposition for every
	 * attacker promoted to ESCORT by strike egress. */
	if (duty == SG_STRIKE_DUTY_ESCORT && tc->escort_mission &&
	    tc->escort_formation && tc->goal_field)
	{
		tc->route_field = tc->goal_field;
		tc->route_pure = true;
		tc->scoop_mission = false;
		return true;
	}
	route = StrikeDutyField(duty, team);
	if (!route && tc->strike_rush)
		route = StrikeEnemyField(team);
	if (!route)
		return false;
	tc->goal_field = route;
	tc->route_field = route;
	tc->route_pure = true;
	/* The coordinator replaced the organic objective.  In particular an
	 * organic escort assigned RECOVER must not retain a stale relay pickup. */
	tc->scoop_mission = false;
	return true;
}

static void StrikeRetireGenericRail(sg_bot_t *bot, const sg_think_t *tc)
{
	if (!bot || !tc || !tc->strike_active)
		return;
	bot->rail_link = -1;
	bot->rail_stage = 0;
	bot->rail_until = 0.0f;
}

/* Return true when strike policy consumed the rally decision. */
static qboolean StrikeApplyRallyPolicy(sg_bot_t *bot, const sg_think_t *tc,
	qboolean *rally_hold)
{
	if (!bot || !tc || !rally_hold || !tc->strike_active)
		return false;
	bot->rally_since = 0.0f;
	*rally_hold = tc->strike_hold && !tc->strike_rush;
	return true;
}

static int StrikeCarrierSlot(int team, edict_t *flag)
{
	int slot;

	if (!flag || !flag->owner || !flag->owner->inuse ||
	    !flag->owner->client || flag->owner->client->ctf.teamnum != team ||
	    ClientHasFlag(flag->owner) != flag)
		return -1;
	for (slot = 0; slot < SG_MAXBOTS; slot++)
		if (sg_bots[slot].active && sg_bots[slot].ent == flag->owner)
			return slot;
	return -1;
}

static qboolean StrikeEnemyRoomDeath(int team)
{
	edict_t *stand = SG_FlagStand(team, false);

	return stand && SG_EnemyRoomDeathKnown(team, stand->s.origin,
	    6.0f, 1200.0f);
}

/* This is the only producer of strike input in the game.  It runs after the
 * level-wide field/belief refresh and before any SG_BotThink call, so all
 * costs, roles, life identities, and flag edges belong to one immutable
 * pre-serial observation. */
static void StrikePrepareFrame(void)
{
	sg_strike_frame_t frames[2];
	int i, team_index;

	memset(sg_strike_role_valid, 0, sizeof(sg_strike_role_valid));
	memset(sg_strike_enemy_pressure_cache, 0,
	       sizeof(sg_strike_enemy_pressure_cache));
	memset(sg_strike_enemy_pressure_goal_cache, 0xff,
	       sizeof(sg_strike_enemy_pressure_goal_cache));
	for (i = 0; i < SG_MAXBOTS; i++)
	{
		edict_t *ent = sg_bots[i].ent;
		qboolean carrying;

		if (!sg_bots[i].active || !ent || !ent->inuse || !ent->client ||
		    (ent->client->ctf.teamnum != CTF_TEAM_RED &&
		     ent->client->ctf.teamnum != CTF_TEAM_BLUE))
			continue;
		carrying = SG_BotCarrying(ent);
		sg_strike_role_cache[i] = sg_rune
			? SG_Role(&sg_bots[i], carrying)
			: (carrying ? SG_ROLE_CARRY : SG_ROLE_ATTACK);
		sg_strike_role_valid[i] = true;
		sg_strike_enemy_pressure_cache[i] =
		    SG_StrikeEnemyPressureActive(
		        sg_strike_role_cache[i] == SG_ROLE_ATTACK, 0,
		        SG_STRIKE_DUTY_NONE);
	}

	for (team_index = 0; team_index < 2; team_index++)
	{
		int team = SG_TeamFromIdx(team_index);
		edict_t *own_flag = ctf_getteamflag(team, 0);
		edict_t *enemy_flag = ctf_getteamflag(team, CTF_TEAM_OPPOSING);

		StrikeFrameInit(&frames[team_index]);
		frames[team_index].own_flag_home = own_flag && own_flag->inuse &&
		    !own_flag->owner && ctf_flagathome(own_flag);
		frames[team_index].enemy_flag_home = enemy_flag && enemy_flag->inuse &&
		    !enemy_flag->owner && ctf_flagathome(enemy_flag);
		frames[team_index].enemy_flag_dropped = enemy_flag && enemy_flag->inuse &&
		    !enemy_flag->owner && !frames[team_index].enemy_flag_home;
		frames[team_index].enemy_flag_carried = enemy_flag && enemy_flag->inuse &&
		    enemy_flag->owner && enemy_flag->owner->inuse &&
		    enemy_flag->owner->client &&
		    enemy_flag->owner->client->ctf.teamnum == team &&
		    ClientHasFlag(enemy_flag->owner) == enemy_flag;
		frames[team_index].carrier_slot =
		    StrikeCarrierSlot(team, enemy_flag);
		frames[team_index].recent_enemy_room_death =
		    StrikeEnemyRoomDeath(team);
	}

	for (i = 0; i < SG_MAXBOTS; i++)
	{
		edict_t *ent = sg_bots[i].ent;
		int team, bot_team_index, seed;
		const int *enemy_field, *own_field, *carrier_field;
		sg_combat_weapon_state_t weapon;

		if (!sg_strike_role_valid[i] || !ent || !ent->client)
			continue;
		team = ent->client->ctf.teamnum;
		bot_team_index = SG_TeamIdx(team);
		seed = sg_bots[i].seed;
		enemy_field = StrikeEnemyField(team);
		own_field = StrikeOwnField(team);
		carrier_field = StrikeCarrierField(team);
		frames[bot_team_index].slot[i].present = 1;
		frames[bot_team_index].slot[i].alive =
		    ent->inuse && ent->deadflag == DEAD_NO && ent->health > 0;
		/* DEFEND remains the authoritative reserved role.  CARRY is admitted
		 * so the core can own its egress duty; RECOVER/ESCORT remain eligible
		 * pressure bodies rather than globally rewriting the roster law. */
		frames[bot_team_index].slot[i].carrying = SG_BotCarrying(ent);
		frames[bot_team_index].slot[i].attack_eligible =
		    StrikeAttackEligible(sg_strike_role_cache[i],
		        frames[bot_team_index].slot[i].carrying,
		        SG_ChatOrderedRole(ent));
		frames[bot_team_index].slot[i].life_id =
		    (uint32_t)ent->client->ctf.ctfid;
		if (!SG_CombatWeaponState(ent, &weapon))
			memset(&weapon, 0, sizeof(weapon));
		frames[bot_team_index].slot[i].weapon_tier = weapon.available_tier;
		frames[bot_team_index].slot[i].enemy_flag_goal_ms =
		    StrikeFieldCost(enemy_field, seed);
		frames[bot_team_index].slot[i].recover_goal_ms =
		    StrikeFieldCost(own_field, seed);
		frames[bot_team_index].slot[i].carrier_goal_ms =
		    StrikeFieldCost(carrier_field, seed);
		frames[bot_team_index].slot[i].direct_flag_touch =
		    frames[bot_team_index].slot[i].alive &&
		    frames[bot_team_index].slot[i].attack_eligible &&
		    SG_AttackFlagDirectTouchAuthority(ent, team, NULL);
	}
	sg_strike_frame_ready = SG_StrikeAdapterBeginFrame(
		&sg_strike_adapter, frames) ? true : false;
	if (sg_strike_frame_ready)
	{
		/* Freeze effective pressure for every teammate now.  Serial movement
		 * may read this table, but may not rewrite it from a partially advanced
		 * team.  Duty, not the transient rush mask, keeps RECOVER/ESCORT out and
		 * CLEAR/PRESS active through egress. */
		for (i = 0; i < SG_MAXBOTS; i++)
		{
			edict_t *ent = sg_bots[i].ent;
			const sg_strike_team_t *team;
			int team_index;
			qboolean participant = false;
			sg_strike_duty_t duty = SG_STRIKE_DUTY_NONE;

			if (!sg_strike_role_valid[i] || !ent || !ent->client)
				continue;
			team_index = SG_TeamIdx(ent->client->ctf.teamnum);
			team = SG_StrikeAdapterTeam(&sg_strike_adapter, team_index);
			if (team && SG_StrikeParticipant(team, i))
			{
				participant = true;
				duty = team->duty[i];
			}
			sg_strike_enemy_pressure_cache[i] =
			    SG_StrikeEnemyPressureActive(
			        sg_strike_role_cache[i] == SG_ROLE_ATTACK,
			        participant, duty);
			sg_strike_enemy_pressure_goal_cache[i] =
			    sg_strike_enemy_pressure_cache[i]
			        ? frames[team_index].slot[i].enemy_flag_goal_ms : -1;
		}
		for (team_index = 0; team_index < 2; team_index++)
			StrikeTelemetryEdge(team_index);
	}
}

void SG_StrikeSlotReset(int slot)
{
	SG_StrikeAdapterForgetSlot(&sg_strike_adapter, slot);
	if (slot >= 0 && slot < SG_MAXBOTS)
	{
		sg_strike_role_valid[slot] = false;
		sg_strike_enemy_pressure_cache[slot] = false;
		sg_strike_enemy_pressure_goal_cache[slot] = -1;
	}
}

qboolean SG_StrikeEnemyPressureSnapshot(const sg_bot_t *bot)
{
	int slot = bot ? (int)(bot - sg_bots) : -1;

	if (slot >= 0 && slot < SG_MAXBOTS && sg_strike_frame_ready &&
	    sg_strike_role_valid[slot])
		return sg_strike_enemy_pressure_cache[slot];
	return bot && bot->last_role == (int)SG_ROLE_ATTACK;
}

int SG_StrikeEnemyPressureGoalSnapshot(const sg_bot_t *bot)
{
	int slot = bot ? (int)(bot - sg_bots) : -1;

	if (slot >= 0 && slot < SG_MAXBOTS && sg_strike_frame_ready &&
	    sg_strike_role_valid[slot])
		return sg_strike_enemy_pressure_goal_cache[slot];
	return bot && bot->last_role == (int)SG_ROLE_ATTACK
	    ? bot->last_goalcost : -1;
}

static sg_role_t StrikeRoleForBot(const sg_bot_t *bot, qboolean carrying)
{
	int slot = bot ? (int)(bot - sg_bots) : -1;

	if (slot >= 0 && slot < SG_MAXBOTS && sg_strike_frame_ready &&
	    sg_strike_role_valid[slot])
		return sg_strike_role_cache[slot];
	return SG_Role((sg_bot_t *)bot, carrying);
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

	(void)SG_HookDiagnosticsFinish(&bot->hook_diagnostics, "death", "lifecycle");
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
	SG_HookLiveReset(&bot->hook_replay, &bot->hook_replay_active,
	    &bot->hook_replay_link, &bot->hook_final_guard);
	bot->hook_entity = NULL;
	bot->hook_legacy_settle = false;
	bot->hook_legacy_arrived = false;
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
	SG_NadeTargetClear(bot);
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
	SG_DropLiveReset(&bot->drop_replay, &bot->drop_replay_active,
	    &bot->drop_replay_link, &bot->drop_live_events);
	SG_SwimLiveReset(&bot->swim_replay, &bot->swim_replay_active,
	    &bot->swim_replay_link, &bot->swim_validated,
	    &bot->swim_proved_ms, &bot->swim_elapsed_ms);
	bot->swim_air_seed = -1;
	bot->declared_activated = false;
	bot->declared_started = false;
	bot->declared_start_frame = -1;
	bot->declared_touched = false;
	bot->declared_touch_frame = -1;
	SG_ButtonExecutionActionReset(bot);
	bot->declared_triggered = false;
	bot->declared_trigger_frame = -1;
	bot->declared_egress_proof_frame = -1;
	bot->declared_door_retreat = false;
	bot->declared_door_suffix_ms = 0;
	bot->declared_guard_paused = false;
	bot->declared_guard_pause_started = 0.0f;
	bot->declared_door_recovery_since = 0.0f;
	bot->commit_link = -1;
	bot->commit_until = 0.0f;
	bot->strike_weapon_link = -1;
	bot->strike_weapon_until = 0.0f;
	bot->strike_weapon_draining = false;
	SG_StrikeWeaponTargetClear(bot);
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
	bot->patrol_link = -1;
	bot->patrol_until = 0.0f;
	bot->def_shift_seed = -1;
	bot->def_shift_link = -1;
	bot->def_shift_from = -1;
	bot->def_shift_until = 0.0f;
	bot->def_shift_next = 0.0f;
	SG_DefenseSupplyReset(bot);
	SG_DefenseCombatLeaseReset(bot);
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
 * THE CORPSE FRAME.
 * Everything a dead bot owes the world: teach the danger and tilt ledgers
 * once, drop every live claim, pulse the respawn button. Returns true when
 * this frame belonged to a corpse and the think ends with it.
 */
static qboolean Think_Dead(sg_bot_t *bot, edict_t *e, usercmd_t *cmd,
	qboolean allow_command)
{
	if (!e->deadflag)
		return false;

	/* my own death, at my own seed: the most honest sighting there
	 * is, and the danger dimension's only teacher */
	if (!bot->death_taught)
	{
		/* Intermission is scoreboard time, not active play.  A corpse that is
		 * first observed there must not teach persisted danger from time in
		 * which navigation and combat are frozen. */
		if (!level.intermissiontime && bot->seed >= 0)
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
	SG_HumanSpeedTimerReset(&bot->as_landing);
	bot->as_landing_command = false;
	bot->as_landing_pending = false;
	bot->as_landing_flags_before = 0;
	memset(&bot->as_landing_before, 0, sizeof(bot->as_landing_before));
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
	if (allow_command)
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
		bot->dither_salt = SG_RouteDitherInitial(bot->instance_token,
		    (unsigned)(e - g_edicts - 1));
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
 * WHERE AM I ON THE RUNE: seed relocation on 48 units of travel, the
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
	if (commit && SG_ActionRuntimeHasTrait(
	        commit->action, SG_ACTF_SUPPRESS_LOCALIZATION))
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
			bot->dither_salt = SG_RouteDitherNext(bot->dither_salt,
			    was, bot->seed);

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





/* An ordinary declared door owns more than the movement fields below: its
 * singleton mover record may still protect this client body.  The local pause
 * latch also covers a failed transition attempt, so restoration cannot mistake
 * uncertain guard state for permission to resume generic navigation. */
static qboolean Bot_DeclaredDoorGuardAction(sg_bot_t *bot)
{
	sg_mover_lease_record_t record;
	qboolean local_door;

	if (!bot)
		return false;
	local_door = bot->declared_started && sg_rune && sg_rune->links &&
	    bot->commit_link >= 0 &&
	    bot->commit_link < sg_rune->hdr.num_links &&
	    (sg_rune->links[bot->commit_link].action == RL_DOOR ||
	     sg_rune->links[bot->commit_link].action == RL_BUTTON_DOOR);
	if (bot->declared_guard_paused)
		return true;
	/* The process claim is the durable authority identity.  The loaded rune
	 * and commit link can disappear on the very authority-loss edge this
	 * helper guards, so inspect the existing record before consulting either.
	 * Validate clears record on NO_LEASE; on identity/host/quarantine errors it
	 * still returns the located record, which must remain held fail-closed. */
	(void)SG_CompoundGuardValidate(&bot->compound_guard, &record);
	if (record.law == SG_MOVER_LAW_DECLARED_DOOR)
	{
		if (record.state == SG_MOVER_LEASE_ACTIVE ||
		    record.state == SG_MOVER_LEASE_PAUSED)
			return true;
		/* ORPHAN belongs solely to the corpse/frame lifecycle after death and
		 * must not immobilize the respawned occupant.  QUARANTINED remains a
		 * local fail-closed hold only while this same action is still retained. */
		if (record.state == SG_MOVER_LEASE_QUARANTINED &&
		    local_door)
			return true;
	}
	return local_door;
}

static qboolean Bot_DeclaredDoorGuardRecord(sg_bot_t *bot,
	sg_mover_lease_record_t *record)
{
	sg_compound_guard_result_t result;

	if (!bot || !record)
		return false;
	result = SG_CompoundGuardValidate(&bot->compound_guard, record);
	(void)result;
	/* Validate can return an identity/host error after copying the located
	 * ACTIVE record and quarantining it.  That durable tuple still owns this
	 * frame fail-closed; an observation error cannot erase command ownership. */
	return record->law == SG_MOVER_LAW_DECLARED_DOOR &&
	       record->mechanism_index != 0U && record->link_index >= 0 &&
	       (record->state == SG_MOVER_LEASE_ACTIVE ||
	        record->state == SG_MOVER_LEASE_PAUSED);
}

static void Bot_DeclaredDoorGuardClearAction(sg_bot_t *bot)
{
	if (bot->strike_weapon_link >= 0 &&
	    bot->sticky_link == bot->strike_weapon_link)
	{
		bot->sticky_link = -1;
		bot->latch_until = 0.0f;
	}
	bot->declared_activated = false;
	bot->declared_started = false;
	bot->declared_start_frame = -1;
	bot->declared_touched = false;
	bot->declared_touch_frame = -1;
	SG_ButtonExecutionActionReset(bot);
	bot->declared_triggered = false;
	bot->declared_trigger_frame = -1;
	bot->declared_egress_proof_frame = -1;
	bot->declared_door_retreat = false;
	bot->declared_door_suffix_ms = 0;
	bot->declared_guard_paused = false;
	bot->declared_guard_pause_started = 0.0f;
	bot->declared_door_recovery_since = 0.0f;
	bot->commit_link = -1;
	bot->commit_until = 0.0f;
	bot->strike_weapon_link = -1;
	bot->strike_weapon_until = 0.0f;
	bot->strike_weapon_draining = false;
	SG_StrikeWeaponTargetClear(bot);
}

/* Return true while the action must remain held.  Only the guard's positive
 * current-subject clearance proof returns false and permits the caller to
 * retire ordinary movement state. */
static qboolean Bot_DeclaredDoorGuardRetainOrRelease(sg_bot_t *bot)
{
	sg_mover_lease_record_t record;
	sg_compound_guard_result_t result;

	result = SG_DeclaredDoorGuardReleaseProvedClear(bot);
	if (result == SG_COMPOUND_GUARD_OK)
	{
		bot->declared_guard_paused = false;
		bot->declared_guard_pause_started = 0.0f;
		bot->declared_door_recovery_since = 0.0f;
		return false;
	}
	if (!bot->declared_guard_paused)
	{
		bot->declared_guard_paused = true;
		bot->declared_guard_pause_started = level.time;
	}
	/* Pause only an exact ACTIVE record.  A failed resolution/transition keeps
	 * the local hold but does not pretend the registry is PAUSED; restoration
	 * re-inspects the durable state and can safely re-authorize ACTIVE later. */
	if (SG_DeclaredDoorGuardHoldOpen(bot, 500) !=
	    SG_COMPOUND_GUARD_OK)
	{
		SG_DeclaredDoorTerminalDeath(bot);
		return true;
	}
	if (result == SG_COMPOUND_GUARD_NOT_CLEAR)
	{
		/* Entity/pusher frames continue while proof-law authority is absent.
		 * Renew the exact already-TOP set every held frame so it cannot close
		 * onto the deliberately frozen body.  PAUSED grants only this bounded
		 * protective timer extension. */
		if (Bot_DeclaredDoorGuardRecord(bot, &record) &&
		    record.state == SG_MOVER_LEASE_ACTIVE)
			(void)SG_DeclaredDoorGuardPause(bot);
	}
	return true;
}

static qboolean Bot_DeclaredDoorRecoveryBudget(sg_bot_t *bot)
{
	if (bot->declared_door_recovery_since == 0.0f)
	{
		SG_Mark(&bot->declared_door_recovery_since);
		return true;
	}
	if (!SG_AgeAtLeast(bot->declared_door_recovery_since, 5.0f))
		return true;
	SG_DeclaredDoorTerminalDeath(bot);
	return false;
}

/* Restore only an exact PAUSED claim.  Clearance retires the action instead;
 * a held client shifts the absolute deadline by precisely the authority gap
 * and discards every outer-frame proof grant before movement can run. */
static qboolean Bot_DeclaredDoorGuardRestore(sg_bot_t *bot)
{
	float paused_for, shifted_deadline;
	sg_mover_lease_record_t record;
	sg_compound_guard_result_t result;

	result = SG_DeclaredDoorGuardReleaseProvedClear(bot);
	if (result == SG_COMPOUND_GUARD_OK)
	{
		Bot_DeclaredDoorGuardClearAction(bot);
		bot->seed = -1;
		return true;
	}
	if (result != SG_COMPOUND_GUARD_NOT_CLEAR)
	{
		if (SG_DeclaredDoorGuardHoldOpen(bot, 500) !=
		    SG_COMPOUND_GUARD_OK)
			SG_DeclaredDoorTerminalDeath(bot);
		else
			(void)Bot_DeclaredDoorRecoveryBudget(bot);
		return false;
	}
	/* Compatibility restoration does not end the physical maintenance duty.
	 * Renew before current-link authorization: the bound activator may have
	 * vanished while the durable captured mover set remains safely holdable. */
	if (SG_DeclaredDoorGuardHoldOpen(bot, 500) !=
	    SG_COMPOUND_GUARD_OK)
	{
		SG_DeclaredDoorTerminalDeath(bot);
		return false;
	}
	if (!Bot_DeclaredDoorRecoveryBudget(bot))
		return false;
	if (!Bot_DeclaredDoorGuardRecord(bot, &record) ||
	    bot->commit_link != record.link_index)
		return false;
	/* A weapon-deadline retirement is one-way.  This paused lease exists only
	 * because current subjects were not yet proved clear; renew the protective
	 * TOP hold until release or the existing five-second terminal budget, but
	 * never resume forward trigger/egress authority for the ended errand. */
	if (bot->strike_weapon_draining)
	{
		if (record.state == SG_MOVER_LEASE_ACTIVE)
			(void)SG_DeclaredDoorGuardPause(bot);
		return false;
	}
	paused_for = level.time - bot->declared_guard_pause_started;
	if (!isfinite(level.time) ||
	    !isfinite(bot->declared_guard_pause_started) ||
	    !isfinite(bot->commit_until) || !isfinite(paused_for) ||
	    paused_for < 0.0f)
		return false;
	shifted_deadline = bot->commit_until;
	if (shifted_deadline != 0.0f)
	{
		shifted_deadline += paused_for;
		if (!isfinite(shifted_deadline))
			return false;
	}
	if (record.state == SG_MOVER_LEASE_PAUSED)
		result = SG_DeclaredDoorGuardResume(bot, record.link_index);
	else
		result = SG_DeclaredDoorGuardAuthorize(bot, record.link_index);
	if (result != SG_COMPOUND_GUARD_OK)
		return false;
	bot->commit_until = shifted_deadline;
	bot->declared_guard_paused = false;
	bot->declared_guard_pause_started = 0.0f;
	bot->declared_start_frame = -1;
	bot->declared_touch_frame = -1;
	bot->declared_trigger_frame = -1;
	bot->declared_egress_proof_frame = -1;
	bot->declared_door_suffix_ms = 0;
	return true;
}

void SG_BotThink(sg_bot_t *bot)
{
	edict_t *e = bot->ent;
	const int *goal_field;
	sg_role_t role;
	int team, bestlink = -1;
	qboolean carrying;
	qboolean rune_compatible;
	qboolean declared_door_guarded;
	const sg_strike_team_t *strike_team = NULL;
	const sg_strike_frame_t *strike_frame = NULL;
	sg_strike_duty_t strike_duty = SG_STRIKE_DUTY_NONE;
	int strike_slot = -1;

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

	rune_compatible = SG_RunePhysicsCompatible(sg_rune);
	declared_door_guarded = Bot_DeclaredDoorGuardAction(bot);
	/* Motion produced while the active law is not the loaded proof law cannot
	 * preserve graph localization. Clear it before the corpse path can teach a
	 * stale danger/tilt sample, including when this hold's ClientThink kills an
	 * initially live bot and authority is restored before the next bot frame.
	 * A surviving body localizes afresh after exact restoration. */
	if (!rune_compatible && !declared_door_guarded)
		bot->seed = -1;
	{
		sg_compound_guard_run_t run_state;

		run_state = SG_DeclaredDoorGuardRunState(bot);
		if (run_state == SG_COMPOUND_GUARD_RUN_TERMINAL)
		{
			(void)SG_HookDiagnosticsFinish(&bot->hook_diagnostics,
			    "death", "declared-door");
			SG_DeclaredDoorTerminalDeath(bot);
			return;
		}
		if (e->deadflag)
		{
			(void)Think_Dead(bot, e, &tc.cmd,
			    run_state == SG_COMPOUND_GUARD_RUN_READY);
			return;
		}
		if (run_state != SG_COMPOUND_GUARD_RUN_READY)
		{
			/* A later bot slot must not enter a newly claimed/released set.  Its
			 * already-linked hook is independent entity physics, so retire that
			 * projectile before suppressing the body command. */
			(void)SG_HookDiagnosticsFinish(&bot->hook_diagnostics,
			    "declared-door-interrupt", "guard-hold");
			if (e->client->hookstate || e->client->hook)
				ctf_hook_abort(e);
			bot->hook_phase = 0;
			bot->hook_link = -1;
			bot->hook_entity = NULL;
			bot->speedhook = false;
			bot->nade_phase = 0;
			SG_NadeTargetClear(bot);
			return;
		}
	}
	if (Think_Dead(bot, e, &tc.cmd, true))
		return;
	if (!rune_compatible)
	{
		/* A runtime cvar change invalidates every stored ballistic witness.
		 * Leave the body in real physics, but submit no navigation and retire
		 * every action that could resume under a different law. */
		(void)SG_HookDiagnosticsFinish(&bot->hook_diagnostics,
		    "physics-incompatible", "cvar-hold");
		if (e->client->hookstate || e->client->hook)
			ctf_hook_abort(e);
		bot->hook_phase = 0;
		bot->hook_link = -1;
		bot->hook_bite_logged = false;
		bot->hook_attached_validated = false;
		bot->hook_landbrake = 0.0f;
		SG_HookLiveReset(&bot->hook_replay, &bot->hook_replay_active,
		    &bot->hook_replay_link, &bot->hook_final_guard);
		bot->hook_entity = NULL;
		bot->hook_legacy_settle = false;
		bot->hook_legacy_arrived = false;
		bot->speedhook = false;
		bot->flow_release = false;
		if (declared_door_guarded)
			bot->declared_door_recovery_since = 0.0f;
		if (declared_door_guarded &&
		    Bot_DeclaredDoorGuardRetainOrRelease(bot))
			return;
		if (declared_door_guarded)
			bot->seed = -1;
		bot->rj_phase = 0;
		bot->rj_deadline = 0.0f;
		bot->rj_fire_until = 0.0f;
		bot->rj_use_next = 0.0f;
		bot->nade_phase = 0;
		SG_NadeTargetClear(bot);
		bot->nade_until = 0.0f;
		/* Progress and retry clocks are action state too. Letting their wall
		 * time accrue during an authority hold can force or shelf a link the
		 * instant the old law returns, even though no proved controller ran. */
		bot->watch_link = -1;
		bot->watch_since = 0.0f;
		VectorClear(bot->watch_org);
		bot->rail_link = -1;
		bot->rail_stage = 0;
		bot->rail_until = 0.0f;
		bot->railhold_since = 0.0f;
		bot->railhold_next = 0.0f;
		bot->railhold_patience = 0.0f;
		bot->railhold_enemy = -1;
		bot->seedless_active = false;
		bot->seedless_since = 0.0f;
		bot->seedless_turn_until = 0.0f;
		bot->seedless_yaw = 0.0f;
		VectorCopy(e->s.origin, bot->stag_org);
		SG_Mark(&bot->stag_since);
		bot->stag_next = 0.0f;
		VectorCopy(e->s.origin, bot->wedge_org);
		SG_Mark(&bot->wedge_since);
		bot->nav_drove = false;
		bot->stuck_time = 0.0f;
		VectorCopy(e->s.origin, bot->stuck_origin);
		bot->fan_side = 0;
		bot->fan_side_until = 0.0f;
		bot->escape_until = 0.0f;
		bot->escape_yaw = 0.0f;
		bot->deaddoor_ahead = false;
		VectorClear(bot->deaddoor_spot);
		{
			int visit;

			for (visit = 0; visit < SG_VISIT_RING; visit++)
			{
				bot->visit_seed[visit] = -1;
				bot->visit_goal[visit] = 0;
				bot->visit_min[visit] = 0;
				bot->visit_time[visit] = 0.0f;
			}
			bot->visit_head = 0;
			bot->orbit_last_seed = -1;
		}
		bot->jump_link = -1;
		bot->jump_started = false;
		bot->drop_link = -1;
		bot->drop_started = false;
		bot->drop_walkoff = false;
		bot->drop_airborne = false;
		bot->drop_recover = false;
		SG_DropLiveReset(&bot->drop_replay, &bot->drop_replay_active,
		    &bot->drop_replay_link, &bot->drop_live_events);
		SG_SwimLiveReset(&bot->swim_replay, &bot->swim_replay_active,
		    &bot->swim_replay_link, &bot->swim_validated,
		    &bot->swim_proved_ms, &bot->swim_elapsed_ms);
		bot->swim_air_seed = -1;
		bot->declared_activated = false;
		bot->declared_started = false;
		bot->declared_start_frame = -1;
		bot->declared_touched = false;
		bot->declared_touch_frame = -1;
		SG_ButtonExecutionActionReset(bot);
		bot->declared_triggered = false;
		bot->declared_trigger_frame = -1;
		bot->declared_egress_proof_frame = -1;
		bot->declared_door_retreat = false;
		bot->declared_door_suffix_ms = 0;
		bot->declared_guard_paused = false;
		bot->declared_guard_pause_started = 0.0f;
		bot->declared_door_recovery_since = 0.0f;
		bot->commit_link = -1;
		bot->commit_until = 0.0f;
		bot->sticky_link = -1;
		bot->latch_until = 0.0f;
		bot->door_hold_ent = NULL;
		bot->door_hold_link = -1;
		bot->door_hold_deadline = 0.0f;
		if (SG_DeclaredDoorGuardRunState(bot) !=
		    SG_COMPOUND_GUARD_RUN_READY)
			return;
		ClientThink(e, &tc.cmd);
		return;
	}
	if (bot->declared_guard_paused)
	{
		if (!Bot_DeclaredDoorGuardRestore(bot) ||
		    SG_DeclaredDoorGuardRunState(bot) !=
		        SG_COMPOUND_GUARD_RUN_READY)
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
		(void)SG_HookDiagnosticsFinish(&bot->hook_diagnostics,
		    "stale-host-rope", "cleanup");
		ctf_hook_abort(e);
		bot->hook_link = -1;
		bot->speedhook = false;
		bot->flow_release = false;
		declared_door_guarded = Bot_DeclaredDoorGuardAction(bot);
		if (declared_door_guarded &&
		    Bot_DeclaredDoorGuardRetainOrRelease(bot))
			return;
		if (declared_door_guarded)
			bot->seed = -1;
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
		SG_DropLiveReset(&bot->drop_replay, &bot->drop_replay_active,
		    &bot->drop_replay_link, &bot->drop_live_events);
		SG_SwimLiveReset(&bot->swim_replay, &bot->swim_replay_active,
		    &bot->swim_replay_link, &bot->swim_validated,
		    &bot->swim_proved_ms, &bot->swim_elapsed_ms);
		bot->declared_activated = false;
		bot->declared_started = false;
		bot->declared_start_frame = -1;
		bot->declared_touched = false;
		bot->declared_touch_frame = -1;
		SG_ButtonExecutionActionReset(bot);
		bot->declared_triggered = false;
		bot->declared_trigger_frame = -1;
		bot->declared_egress_proof_frame = -1;
		bot->declared_door_retreat = false;
		bot->declared_door_suffix_ms = 0;
		bot->declared_guard_paused = false;
		bot->declared_guard_pause_started = 0.0f;
		bot->declared_door_recovery_since = 0.0f;
		bot->rj_phase = 0;
		bot->nade_phase = 0;
		SG_NadeTargetClear(bot);
		if (SG_DeclaredDoorGuardRunState(bot) !=
		    SG_COMPOUND_GUARD_RUN_READY)
			return;
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
	/* LMCTF has ONE flag item: "Enemy Flag" (g_items.c:2478). */
	carrying = SG_BotCarrying(e);

	role = StrikeRoleForBot(bot, carrying);
	if (!SG_RoleOwnsDefenseState(role))
	{
		/* A patrol is a role-local leg, not a mission that may sleep through
		 * ATTACK/RECOVER/ESCORT and resume from stale topology later. */
		(void)SG_DefensePatrolRetireIfInactive(0, &bot->patrol_link,
		    &bot->patrol_seed, &bot->commit_link);
		if (bot->commit_link < 0)
			bot->commit_until = 0.0f;
		bot->patrol_until = 0.0f;
		bot->def_stand = false;
	}

	Think_CarryBookends(bot, e, role, team, carrying);

	/* the context carries the stage contract from here down; the frame
	 * identity loads first, each stage adds what it resolves */
	tc.e = e;
	tc.role = role;
	tc.team = team;
	tc.carrying = carrying;
	tc.strike_pressure = SG_StrikeEnemyPressureActive(
	    role == SG_ROLE_ATTACK, 0, SG_STRIKE_DUTY_NONE);
	tc.combat_pursuit = SG_StrikeCombatPursuitActive(
	    role == SG_ROLE_ATTACK || role == SG_ROLE_RECOVER,
	    0, SG_STRIKE_DUTY_NONE);
	tc.rearguard = SG_StrikeRearguardActive(
	    role == SG_ROLE_ATTACK || role == SG_ROLE_ESCORT,
	    0, SG_STRIKE_DUTY_NONE);
	tc.escort_mission = SG_StrikeEscortActive(
	    role == SG_ROLE_ESCORT, 0, SG_STRIKE_DUTY_NONE);

	Think_LiveWeights(bot, &tc);    /* fills tc.live */
	tc.w = &tc.live;
	/* The shared weapon field contains every visible pad, including pads this
	 * client already owns under WEAPONS_STAY.  Price only the exact live set
	 * accepted by Pickup_Weapon for this bot. */
	tc.collectible_weapon_field =
	    (tc.live.item[SG_FC_WEAPON] > 0.0f) ?
	    SG_CollectibleWeaponField(bot) : NULL;

	tc.support = NULL;
	tc.intercept = NULL;
	Think_InterceptField(role, team, &tc.support, &tc.intercept);

	/* Objective's tactical waypoint search calls Surface_At, so every pricing
	 * input must exist before Objective—not be filled later by PickLink. */
	tc.health = e->health;
	tc.danger = Danger_Field(team);
	tc.push = (role == SG_ROLE_ATTACK &&
	           SG_TimerPending(sg_push_until[SG_TeamIdx(team)]));

	/* Resolve coordinator ownership before Objective is allowed to claim an
	 * optional item, age a mega offer, or commit a tactical waypoint.  The
	 * later route overlay still owns the exact duty field; this early slice
	 * supplies only the already-frozen membership/duty policy needed to keep
	 * superseded preparation from being created and discarded every frame. */
	strike_slot = (int)(bot - sg_bots);
	if (strike_slot >= 0 && strike_slot < SG_MAXBOTS &&
	    sg_strike_frame_ready &&
	    team >= CTF_TEAM_RED && team <= CTF_TEAM_BLUE)
	{
		const int ti = SG_TeamIdx(team);

		strike_team = SG_StrikeAdapterTeam(&sg_strike_adapter, ti);
		strike_frame = SG_StrikeAdapterFrame(&sg_strike_adapter, ti);
		if (strike_team && strike_frame &&
		    SG_StrikeParticipant(strike_team, strike_slot))
		{
			strike_duty = strike_team->duty[strike_slot];
			tc.strike_active = true;
			tc.strike_hold = SG_StrikeMemberShouldHold(
				strike_team, strike_slot);
			tc.strike_rush = SG_StrikeMemberRushes(
				strike_team, strike_slot);
			tc.strike_pressure = SG_StrikeEnemyPressureActive(
			    role == SG_ROLE_ATTACK, 1, strike_duty);
			tc.combat_pursuit = SG_StrikeCombatPursuitActive(
			    role == SG_ROLE_ATTACK || role == SG_ROLE_RECOVER,
			    1, strike_duty);
			tc.rearguard = SG_StrikeRearguardActive(
			    role == SG_ROLE_ATTACK || role == SG_ROLE_ESCORT,
			    1, strike_duty);
			tc.escort_mission = SG_StrikeEscortActive(
			    role == SG_ROLE_ESCORT, 1, strike_duty);
			tc.strike_blocks_optional =
			    SG_StrikeDutyRetiresOptionalErrand(strike_duty);
			if (tc.strike_blocks_optional)
				Lead_Abort(bot, "strike duty");
		}
	}

	Think_Objective(bot, &tc);

	/* Strike is a coordinator overlay, not a second role allocator.  It may
	 * select the route owned by a duty (home, carrier, or enemy flag) while
	 * the role row, defender reservation, and CARRY bookends remain intact. */
	if (strike_team && strike_frame && tc.strike_active)
	{
		if (SG_StrikeParticipant(strike_team, strike_slot))
		{
			int direct_flag_touch;
			int weapon_goal_ms = -1;
			int weapon_remaining_ms;
			int needs_weapon;
			int combat_would_engage;
			const int *weapon_target_field = NULL;

			/* Egress and attack duties use the existing directed fields. */
			(void)StrikeApplyDutyRoute(&tc, strike_duty, team);
			if (tc.strike_rush)
			{
				/* A prior approach transaction cannot detour a same-frame
				 * rush.  Its identity is retired before movement can emit. */
				bot->nade_phase = 0;
				SG_NadeTargetClear(bot);
				bot->nade_until = 0.0f;
			}

			/* A below-tier member owns one exact collectible weapon-pad field
			 * only while the live authority says it is not fighting or already
			 * able to touch the flag. The core's immutable per-life deadline ends
			 * this branch. */
			direct_flag_touch =
				strike_frame->slot[strike_slot].direct_flag_touch ||
				SG_AttackFlagDirectTouchAuthority(e, team, NULL);
			combat_would_engage = SG_CombatWouldEngage(e);
			needs_weapon = SG_StrikeMemberNeedsWeapon(
				strike_team, strike_slot, level.time);
			if (needs_weapon && !tc.strike_rush && !carrying &&
			    !combat_would_engage && !direct_flag_touch)
				weapon_target_field = SG_StrikeWeaponTargetField(
					bot, &weapon_goal_ms);
			else
				SG_StrikeWeaponTargetClear(bot);
			weapon_remaining_ms = (int)((
				strike_team->weapon_deadline[strike_slot] - level.time) *
				1000.0f);
			if (SG_StrikeWeaponDetourAllowed(
				    needs_weapon,
				    tc.strike_rush, carrying, combat_would_engage,
				    direct_flag_touch,
				    strike_frame->slot[strike_slot].enemy_flag_goal_ms,
				    weapon_goal_ms, weapon_remaining_ms))
			{
				tc.goal_field = weapon_target_field;
				tc.route_field = weapon_target_field;
				tc.route_pure = true;
				tc.strike_weapon_pursuit = true;
				tc.strike_weapon_deadline =
					strike_team->weapon_deadline[strike_slot];
			}

			/* Generic proof-line retry has no strike phase/route identity.  Retire
			 * it for every active strike participant before Pick/Commit: otherwise
			 * an old lane can move a FORM leader, override GO, or outlive the
			 * five-second weapon errand.  The ordinary shelf/escape path remains
			 * the bounded stagnation recovery for coordinated offense. */
			StrikeRetireGenericRail(bot, &tc);
		}
	}

	goal_field = tc.goal_field;
	/* Objective published the prior route cost before the strike overlay may
	 * replace its route.  Publish the same live directed cost for downstream
	 * approach/terminal policy so a duty switch cannot retain stale pricing. */
	bot->last_goalcost = (bot->seed >= 0 &&
	                      goal_field[bot->seed] < SG_FIELD_INF)
	                     ? goal_field[bot->seed] : -1;

	/* The legacy approach band owns a 15-second/periodic rally clock.  A
	 * strike member is governed by the same-frame HOLD/RUSH verdict instead;
	 * non-members retain the existing approach behavior unchanged. */
	if (!StrikeApplyRallyPolicy(bot, &tc, &rally_hold))
		rally_hold = Think_ApproachBand(bot, &tc);
	else
		/* The coordinator owns HOLD/RUSH, while the approach function still
		 * owns independent pressure actions such as a live-enemy flying cook. */
		(void)Think_ApproachBand(bot, &tc);
	bot->term_brake = 1.0f;         /* terminal braking re-earned every frame */
	bot->terminal = false;

	Think_TrackSeed(bot, e, team);
	if ((bot->seed < 0 || goal_field[bot->seed] >= SG_FIELD_INF) &&
	    !Think_SpeedhookOwnsSeed(bot) &&
	    !(bot->seed >= 0 && bot->commit_link >= 0 &&
	      bot->commit_link < sg_rune->hdr.num_links &&
	      SG_ActionRuntimeHasTrait(
	          sg_rune->links[bot->commit_link].action,
	          SG_ACTF_SUPPRESS_LOCALIZATION)) &&
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

	/* Combat execution remains after movement. Only the optional posted-defense
	 * shift needs this read-only forward-visible preview before CommitLink so a
	 * newly seen enemy cannot buy one lateral RUN on the stale engaged_last.
	 * PickLink keeps its ordinary retained-fight input; this only governs the
	 * one short shift transaction. */
	if (isfinite(sg_cv.defshift->value) && sg_cv.defshift->value > 0.0f &&
	    role == SG_ROLE_DEFEND && bot->def_stand &&
	    SG_CombatWouldEngage(e))
		duel = true;
	tc.duel = duel;

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
	{
		sg_mover_lease_record_t record;

		/* CommitLink contains legacy rail/patrol/watchdog overrides after it
		 * restores a retained commitment.  A process-wide mover claim is the
		 * command owner, so normalize the candidate before Think_Move can write
		 * any body or usercmd state. */
		if (Bot_DeclaredDoorGuardRecord(bot, &record))
		{
			if (!sg_rune || !sg_rune->links || record.link_index < 0 ||
			    record.link_index >= sg_rune->hdr.num_links ||
			    (sg_rune->links[record.link_index].action != RL_DOOR &&
			     sg_rune->links[record.link_index].action != RL_BUTTON_DOOR) ||
			    bot->commit_link != record.link_index)
			{
				SG_DeclaredDoorTerminalDeath(bot);
				return;
			}
			bestlink = record.link_index;
		}
	}
	/* CommitLink may have positively released a declared claim in this same
	 * think.  Re-sample the physical retirement fence before Think_Move can
	 * write a body or Think_Emit can submit any command. */
	if (SG_DeclaredDoorGuardRunState(bot) !=
	    SG_COMPOUND_GUARD_RUN_READY)
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
	    (sg_rune && strcmp(sg_rune_map, level.mapname) != 0))
		SG_LevelChange();
	SG_CompoundGuardGameFrame();
	SG_Mark(&sg_last_frame_time);
	if (sg_rune && !SG_RunePhysicsCompatible(sg_rune))
	{
		if (!sg_physics_warned)
			sg_host.dprint("slipgate: movement held: active level identity or "
			               "physics law differs from loaded artifact\n");
		sg_physics_warned = true;
	}
	else if (sg_rune && sg_physics_warned)
	{
		sg_host.dprint("slipgate: rune identity and proof law restored; "
		               "movement resumed\n");
		sg_physics_warned = false;
	}
	SG_CombatWhy();
	/* Persisted danger measures active play.  Intermission can last well past
	 * the normal scoreboard delay (or indefinitely on an unattended server),
	 * so aging here would erase learned evidence while every client is frozen. */
	if (!level.intermissiontime)
		Danger_Decay();

	if (sg_rune)
	{
		/* A permanent action capability must cross the cached field boundary
		 * before CACO advects hidden carriers along that topology.  The normal
		 * refresh below completes dynamic belief fields after CACO updates and
		 * before any bot consumes them. */
		(void)Fields_ActionTopologyRefresh(sg_rune);
		Caco_Frame(sg_rune);
		Fields_Refresh(sg_rune);
	}
	Botfill_Frame();
	/* the scoreline and the clock, before anybody decides a role from them */
	Clock_Frame();
	/* Publish both immutable strike snapshots before the serial bot-think
	 * loop.  SG_StrikeStep therefore runs exactly once per team per server
	 * frame, never once per bot or from partially updated roster state. */
	StrikePrepareFrame();
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
		/* One map-local pulse per server second proves the diagnostic stream's
		 * complete residence coverage even while a bot is dead and therefore
		 * cannot reach Think_Emit's route-state report. */
		if (sg_cv.debug->value && level.framenum > 0 &&
		    level.framenum % 10 == 0)
			sg_host.dprint("SGCENSUS %s: frm=%d alive=%d\n",
			    ent->client->pers.netname, level.framenum,
			    ent->deadflag == DEAD_NO && ent->health > 0);
		SG_BotThink(&sg_bots[i]);
	}
}

/* ---------------------------------------------------------------- spawn */


void SG_LevelChange(void)
{
	int i;

	/* Map teardown is a terminal owner in its own right. Finish before the
	 * roster removal so the original map snapshot remains attached; slot reset
	 * then sees a closed state and is intentionally idempotent. */
	for (i = 0; i < SG_MAXBOTS; i++)
		(void)SG_HookDiagnosticsFinish(&sg_bots[i].hook_diagnostics,
		    "map-transition", "level-change");
	SG_ButtonExecutionLevelReset();
	(void)SG_CompoundGuardGameLevelReset();
	/* The fallback transition path must be as fail-closed as SpawnEntities. */
	SG_DangerPersistenceReset();
	SG_LevelIdentityReset();
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
	sg_physics_warned = false;
	sg_human_use = NULL;    /* TAG_LEVEL too: freed with its rune */
	sg_human_live = NULL;
	sg_human_escape = NULL;
	sg_def_post[0] = sg_def_post[1] = NULL;
	sg_def_icept[0] = sg_def_icept[1] = NULL;
	sg_defense_payload = NULL;
	sg_sidecar_log_mask = 0;
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
		 * different place. Tilt and graph-bound danger die with the level.
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

#ifdef SG_STRIKE_TRANSITION_TEST_API
/* Host-free transition probes execute the exact production helpers above.
 * Release builds contain none of these entry points. */
void SG_StrikeTestSetRune(rune_t *rune)
{
	sg_rune = rune;
}

qboolean SG_StrikeTestDeclaredDoorGuardRestore(sg_bot_t *bot)
{
	return Bot_DeclaredDoorGuardRestore(bot);
}

qboolean SG_StrikeTestApplyDutyRoute(sg_think_t *tc,
	sg_strike_duty_t duty, int team)
{
	return StrikeApplyDutyRoute(tc, duty, team);
}

void SG_StrikeTestRetireGenericRail(sg_bot_t *bot, const sg_think_t *tc)
{
	StrikeRetireGenericRail(bot, tc);
}

qboolean SG_StrikeTestApplyRallyPolicy(sg_bot_t *bot,
	const sg_think_t *tc, qboolean *rally_hold)
{
	return StrikeApplyRallyPolicy(bot, tc, rally_hold);
}

qboolean SG_StrikeTestAttackEligible(sg_role_t role, qboolean carrying,
	int ordered_role)
{
	return StrikeAttackEligible(role, carrying, ordered_role);
}
#endif
