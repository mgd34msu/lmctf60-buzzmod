#include "g_local.h"
#include "g_ctffunc.h"
#include "g_tourney.h"              /* Match_Mode -- the clock read's one caveat */
#include "slipgate/sg_local.h"
#include "slipgate/sg_localization.h"
#include "slipgate/sg_action.h"
#include "slipgate/sg_combat.h"
#include "slipgate/sg_chat.h"       /* human orders replace the role quota */
#include "slipgate/sg_identity.h"
#include "slipgate/sg_hook_game.h"
#include "slipgate/sg_persona.h"    /* the roster's names, wired to behaviour */

/*
 * The client lifecycle and the glue's edict helpers are declared where the
 * legacy bot spawner declares them -- locally, the way this tree does it
 * (see bl_spawn.c:26-28). Same functions, same signatures.
 */
void		ClientThink(edict_t *ent, usercmd_t *ucmd);
void		Cmd_Kill_f(edict_t *ent);
void		Cmd_Hook_f(edict_t *ent);
void		ctf_hook_abort(edict_t *ent);
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
#include "slipgate/sg_rune_source_authority_owner.h"
#include "slipgate/sg_rune_binding.h"
#include "slipgate/sg_rune_proof.h"
#include "slipgate/sg_compound_publication.h"
#include "slipgate/sg_compound_swim_game.h"
#include "slipgate/sg_timed_vault_egress.h"

#define FIELD_INF       0x3fffffff
#include "slipgate/sg_bot.h"
#include "slipgate/sg_hook_live.h"
#include "slipgate/sg_compound_guard_game.h"
#include "slipgate/sg_compound_hook_game.h"
#include "slipgate/sg_declared_door_guard.h"
#include "slipgate/sg_drop_live.h"
#include "slipgate/sg_swim_live.h"
#include "slipgate/sg_clock.h"
#include "slipgate/sg_defense_shift.h"
#include "slipgate/sg_weights.h"
#include "slipgate/sg_tilt.h"
#include "slipgate/sg_lead.h"
#include "slipgate/sg_move.h"
#include "slipgate/sg_price.h"
#include "slipgate/sg_role_policy.h"
#include "slipgate/sg_strategy_runtime_bridge.h"
#include "slipgate/sg_strategy_runtime_bridge_private.h"
#include "slipgate/sg_traversal_transition.h"
#include "slipgate/sg_route_dither.h"
#include "slipgate/sg_escort_dose.h"
#include "slipgate/sg_role_skew_random.h"
#include "slipgate/sg_descend.h"
#include "slipgate/sg_goal.h"
#include "slipgate/sg_strike_adapter.h"
#include "slipgate/sg_field_projection.h"
#include "slipgate/sg_host_law_owner.h"
#include "slipgate/sg_bot_localization.h"
#ifdef world
#define SG_ARACH_RESTORE_WORLD_MACRO 1
#undef world
#endif
#include "slipgate/sg_rune_compact_localize.h"
#include "slipgate/sg_rune_compact_production.h"
#include "slipgate/sg_rune_compact_learning_game.h"
#include "slipgate/sg_tactic_runtime_private.h"
#ifdef SG_ARACH_RESTORE_WORLD_MACRO
#define world (&g_edicts[0])
#undef SG_ARACH_RESTORE_WORLD_MACRO
#endif
#include "slipgate/sg_tactic_execution.h"
#include "slipgate/sg_tactic_controller.h"
#include "slipgate/sg_tactic_runtime.h"

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
static sg_strike_duty_t sg_strike_duty_cache[SG_MAXBOTS];
static qboolean sg_strike_frame_ready;
static qboolean sg_strike_telemetry_valid[2];
static uint32_t sg_strike_telemetry_epoch[2];
static sg_strike_phase_t sg_strike_telemetry_phase[2];
static rune_t	*sg_rune;
static sg_rune_compact_production_t sg_compact_production =
	SG_RUNE_COMPACT_PRODUCTION_INITIALIZER;
static qboolean sg_physics_warned;
static float sg_last_frame_time;
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
	memset(sg_strike_duty_cache, 0, sizeof(sg_strike_duty_cache));
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
static qboolean	sg_autoload_attempted;

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
unsigned char *sg_human_escape; /* the ESCAPEE's cut: only the flag
                                        * carrier's own entity trajectory in
                                        * the 20s after each steal (.hme) --
                                        * the roads humans actually flee on,
                                        * as opposed to .hml's hunter-heavy
                                        * POV-agnostic window */
unsigned char *sg_human_live; /* same, cut from the 20s windows
                                      * after a steal: how humans move
                                      * when a flag is OUT (.hml) */

/* Eight exit-bearing counts per stolen flag. A color-specific map key takes
 * precedence over the plain map key. */
int	sg_escape_count[2][SG_ESC_BUCKETS];  /* [0]=red flag stolen, [1]=blue */
int	sg_escape_total[2];                  /* 0 = no prior for that flag */


typedef enum rune_load_attempt_e
{
	RUNE_LOAD_MISSING = 0,
	RUNE_LOAD_REJECTED,
	RUNE_LOAD_INFRA,
	RUNE_LOAD_READY
} rune_load_attempt_t;

static rune_load_attempt_t sg_last_rune_load;
static const char *sg_last_rune_failure_stage;
static const char *sg_setup_failure_stage;
static qboolean sg_setup_failure_artifact;

static void SG_SetupFailure(const char *stage, qboolean artifact)
{
	sg_setup_failure_stage = stage;
	sg_setup_failure_artifact = artifact;
}

typedef enum rune_validation_status_e
{
	RUNE_VALIDATION_OK = 0,
	RUNE_VALIDATION_REJECTED,
	RUNE_VALIDATION_INFRA
} rune_validation_status_t;

typedef struct rune_validation_result_s
{
	rune_validation_status_t status;
	const char *reason;
} rune_validation_result_t;

static rune_validation_result_t Rune_ValidationResult(
	rune_validation_status_t status, const char *reason)
{
	rune_validation_result_t result;

	result.status = status;
	result.reason = reason;
	return result;
}

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

static rune_validation_result_t Rune_BuildOutboundIndexes(rune_t *r)
{
	int i;

	if (!r || r->hdr.num_seeds <= 0 || r->hdr.num_links < 0)
		return Rune_ValidationResult(RUNE_VALIDATION_REJECTED,
			"invalid native graph counts");
	r->first_link = sg_host.level_alloc(sizeof(*r->first_link) *
	    (size_t)r->hdr.num_seeds);
	r->next_link = sg_host.level_alloc(sizeof(*r->next_link) *
	    (size_t)(r->hdr.num_links ? r->hdr.num_links : 1));
	r->linked_seed = sg_host.level_alloc((size_t)r->hdr.num_seeds);
	if (!r->first_link || !r->next_link || !r->linked_seed)
		return Rune_ValidationResult(RUNE_VALIDATION_INFRA,
			"outbound-index allocation failure");
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

		if ((tombstone && r->linked_seed[i]) ||
		    (!tombstone && !r->linked_seed[i]))
			return Rune_ValidationResult(RUNE_VALIDATION_REJECTED,
				"invalid route-core seed ownership");
	}
	return Rune_ValidationResult(RUNE_VALIDATION_OK, NULL);
}

static rune_validation_result_t Rune_ReplayDoorPlans(rune_t *r,
	uint32_t *index_out)
{
	int i;

	if (!SG_OracleDoorEgressReplayCacheBegin())
		return Rune_ValidationResult(RUNE_VALIDATION_INFRA,
			"door egress replay cache busy");
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
			SG_OracleDoorEgressReplayCacheEnd();
			return Rune_ValidationResult(RUNE_VALIDATION_REJECTED,
				"invalid live declared-door replay");
		}
	}
	SG_OracleDoorEgressReplayCacheEnd();
	return Rune_ValidationResult(RUNE_VALIDATION_OK, NULL);
}

rune_t *Rune_Load(const char *mapname)
{
	char path[MAX_OSPATH];
	rune_t *rune = NULL;
	sg_rune_authority_t captured;
	sg_rune_authority_t active;
	sg_rune_file_load_result_t load_result;
	sg_compound_publication_result_t compound_result;
	rune_validation_result_t validation;
	sg_mech_catalog_match_t catalog_match;
	sg_rune_mechanism_bindings_status_t binding_status;
	const char *failure = NULL;
	const char *failure_stage = "authority";
	uint32_t failure_index = UINT32_MAX;
	qboolean proof_scope_active = false;
	qboolean accepted = false;
	qboolean infrastructure = false;
	cvar_t *game_directory_cvar;
	const char *game_directory;

	memset(&captured, 0, sizeof(captured));
	memset(&active, 0, sizeof(active));
	memset(&load_result, 0, sizeof(load_result));
	path[0] = '\0';
	sg_last_rune_load = RUNE_LOAD_MISSING;
	sg_last_rune_failure_stage = "missing";
	SG_HooksInit();
	game_directory_cvar = sg_host.cvar("gamedir", "", 0);
	game_directory = game_directory_cvar && game_directory_cvar->string &&
		game_directory_cvar->string[0] ? game_directory_cvar->string : ".";

	if (!SG_RuneAuthorityCapture(mapname, &captured))
	{
		failure_stage = captured.identity_status == SG_IDENTITY_OK
			? "proof-law" : "identity";
		failure = "rune authority unavailable";
		infrastructure = true;
		goto cleanup;
	}
	if (!SG_RuneInstallDestinationPath(path, sizeof(path), game_directory,
	    captured.level.mapname))
	{
		failure_stage = "path";
		failure = "path exceeds MAX_OSPATH or has invalid map identity";
		infrastructure = true;
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
		infrastructure = load_result.status == SG_RUNE_FILE_LOAD_INFRA;
		goto cleanup;
	}
	catalog_match = SG_MechCatalogMatchStatus(rune->mechanism_nodes,
		rune->artifact.num_mechanism_nodes, rune->mechanism_edges,
		rune->artifact.num_inventory_edges, rune->mechanism_strings,
		rune->artifact.string_bytes);
	if (catalog_match != SG_MECH_CATALOG_MATCH_READY)
	{
		failure_stage = "live-catalog-rebind";
		failure = catalog_match == SG_MECH_CATALOG_MATCH_CONTENT_MISMATCH
			? "decoded mechanism inventory differs from sealed world"
			: "sealed mechanism catalog is unavailable or drifted";
		infrastructure = catalog_match != SG_MECH_CATALOG_MATCH_CONTENT_MISMATCH;
		goto cleanup;
	}
	failure_stage = "outbound-index";
	validation = Rune_BuildOutboundIndexes(rune);
	failure = validation.reason;
	infrastructure = validation.status == RUNE_VALIDATION_INFRA;
	if (failure)
		goto cleanup;
	failure_stage = "mechanism-rebind";
	binding_status = SG_RuneMechanismBindingsStatus(rune, &failure_index);
	if (binding_status != SG_RUNE_MECHANISM_BINDINGS_READY)
	{
		failure = binding_status == SG_RUNE_MECHANISM_BINDINGS_ARTIFACT
			? "live mechanism binding rejected"
			: "live mechanism binding unavailable or drifted";
		infrastructure = binding_status == SG_RUNE_MECHANISM_BINDINGS_INFRA;
		goto cleanup;
	}
	if (!SG_RuneProofScopeBegin(rune->artifact.identity.gravity))
	{
		failure_stage = "proof-scope";
		failure = "proof scope busy or invalid";
		infrastructure = true;
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
		infrastructure = compound_result.status ==
			SG_COMPOUND_PUBLICATION_ALLOCATION ||
			compound_result.status == SG_COMPOUND_PUBLICATION_WORLD_DRIFT;
		goto cleanup;
	}
	failure_stage = "door-replay";
	if (!SG_TimedVaultEgressScopeBegin(rune->seeds, rune->hdr.num_seeds,
	        rune->links, rune->hdr.num_links))
	{
		failure = "timed-vault water escape index unavailable";
		infrastructure = true;
		goto cleanup;
	}
	validation = Rune_ReplayDoorPlans(rune, &failure_index);
	failure = validation.reason;
	infrastructure = validation.status == RUNE_VALIDATION_INFRA;
	SG_TimedVaultEgressScopeEnd();
	SG_RuneProofScopeEnd();
	proof_scope_active = false;
	if (failure)
		goto cleanup;
	/* Objective reachability is a runtime destination query.  Artifact
	 * admission stops at the structural outbound-index and mechanism checks;
	 * no legacy route-contract or reverse-Dijkstra closure is consulted here. */
	if (!SG_RuneAuthorityCapture(rune->artifact.identity.map_name, &active) ||
	    !SG_RuneAuthorityMatchesArtifact(&active, &rune->artifact))
	{
		failure_stage = "authority-recheck";
		failure = "active identity or proof law drifted during load";
		infrastructure = true;
		goto cleanup;
	}
	accepted = true;

cleanup:
	if (proof_scope_active)
		SG_RuneProofScopeEnd();
	if (!accepted)
	{
		sg_last_rune_load = infrastructure ? RUNE_LOAD_INFRA : RUNE_LOAD_REJECTED;
		sg_last_rune_failure_stage = failure_stage;
		Rune_Free(rune);
		if (failure_index == UINT32_MAX)
		{
			if (infrastructure)
				sg_host.dprint("rune: infrastructure %s stage=%s reason=%s\n",
					path[0] ? path : "<unresolved>", failure_stage,
					failure ? failure : "unknown");
			else
				sg_host.dprint("rune: rejected %s stage=%s reason=%s\n",
					path[0] ? path : "<unresolved>", failure_stage,
					failure ? failure : "unknown");
		}
		else if (infrastructure)
			sg_host.dprint("rune: infrastructure %s stage=%s reason=%s index=%u\n",
				path[0] ? path : "<unresolved>", failure_stage,
				failure ? failure : "unknown", (unsigned int)failure_index);
		else
			sg_host.dprint("rune: rejected %s stage=%s reason=%s index=%u\n",
				path[0] ? path : "<unresolved>", failure_stage,
				failure ? failure : "unknown", (unsigned int)failure_index);
		return NULL;
	}
	sg_last_rune_load = RUNE_LOAD_READY;
	sg_last_rune_failure_stage = "ready";
	return rune;
}
/* --------------------------------------------------------------- fields */

/* Next graph hop toward a dry seed. NULL means no water seeds; -1 means the
 * water seed has no graph path to air. */
int	*sg_airnext;


static qboolean SG_LevelSetupAttempt(void)
{
	cvar_t *game_cvar;
	const char *game_directory;
	sg_rune_compact_production_result_t compact_result;
	char compact_path[MAX_OSPATH];

	SG_SetupFailure("setup", false);

	SG_HooksInit();     /* the host table, before any module reaches out */
	if (sg_setup_failed)
	{
		SG_SetupFailure("previous-failure", false);
		return false;
	}
	/* Confirm that the level's exact BSP, physics, and engine callbacks are
	 * published before loading the controller. */
	{
		sg_host_law_result_t host_law_result =
			SG_HostLawProductionEnsureLevel(level.mapname);

		if (host_law_result.status != SG_HOST_LAW_OK)
			sg_host.dprint("slipgate: engine movement provider unavailable for %s: %s (%s)\n",
				level.mapname, SG_HostLawStatusString(host_law_result.status),
				SG_HostLawFieldString(host_law_result.field));
	}
	if (sg_compact_production.active != 0U)
	{
		if (SG_RuneCompactProductionCurrent(&sg_compact_production) &&
			strcmp(sg_rune_map, level.mapname) == 0)
			return true;
		sg_host.dprint("slipgate: compact rune held: active runtime authority "
			"is stale or belongs to another map\n");
		SG_SetupFailure("compact-active-authority", true);
		return false;
	}
	game_cvar = sg_host.cvar("gamedir", "", 0);
	game_directory = game_cvar && game_cvar->string && game_cvar->string[0]
	    ? game_cvar->string : ".";
	if (!SG_RuneInstallDestinationPath(compact_path, sizeof(compact_path),
		game_directory, level.mapname))
	{
		sg_host.dprint("slipgate: compact rune rejected map=%s stage=path "
			"reason=invalid-or-too-long\n", level.mapname);
		SG_SetupFailure("compact-path", false);
		return false;
	}
	compact_result = SG_RuneCompactProductionInit(&sg_compact_production);
	if (compact_result.status == SG_RUNE_COMPACT_PRODUCTION_OK)
		compact_result = SG_RuneCompactProductionLoad(&sg_compact_production,
			compact_path);
	if (compact_result.status != SG_RUNE_COMPACT_PRODUCTION_OK)
	{
		sg_host.dprint("slipgate: compact rune rejected map=%s stage=%s "
			"artifact=%s artifact_stage=%u wire=%s host=%s(%s) runtime=%s "
			"os_error=%d\n", level.mapname,
			SG_RuneCompactProductionStatusString(compact_result.status),
			SG_RuneCompactArtifactLoadDiagnosticString(
				compact_result.artifact.diagnostic),
			(unsigned int)compact_result.artifact.stage,
			SG_RuneCompactWireErrorString(
				compact_result.artifact.wire_error.code),
			SG_HostLawStatusString(compact_result.host.status),
			SG_HostLawFieldString(compact_result.host.field),
			SG_CompactRuntimeLevelStatusString(compact_result.runtime),
			compact_result.artifact.os_error);
		SG_SetupFailure("compact-load", true);
		SG_RuneCompactProductionClear(&sg_compact_production);
		return false;
	}
	memcpy(sg_rune_map, level.mapname, sizeof(sg_rune_map));
	sg_rune_map[sizeof(sg_rune_map) - 1U] = '\0';
	Caco_Reset();
	{
		const sg_rune_compact_model_t *compact_model =
			SG_RuneCompactProductionModel(&sg_compact_production);

		sg_host.dprint("slipgate: compact rune ready %s, %u cells, %u portals, "
			"%u movement fibers, %u weapon kernels\n", level.mapname,
			(unsigned int)compact_model->cell_count,
			(unsigned int)compact_model->portal_count,
			(unsigned int)compact_model->movement_fiber_count,
			(unsigned int)compact_model->weapon_kernel_count);
	}
	return true;

}

static qboolean SG_LevelSetupWithSource(const char *source)
{
	qboolean ready = SG_LevelSetupAttempt();

	if (!ready)
	{
		sg_host.dprint("slipgate: rune setup terminal map=%s source=%s "
			"class=%s stage=%s\n", level.mapname,
			source, sg_setup_failure_artifact ? "artifact" : "infra",
			sg_setup_failure_stage ? sg_setup_failure_stage : "setup");
	}
	if (sg_host.flush)
		sg_host.flush();
	return ready;
}

qboolean SG_LevelSetup(void)
{
	return SG_LevelSetupWithSource("autoload");
}

void SG_LevelSetupAfterRuneWrite(void)
{
	if (SG_RuneCompactProductionCurrent(&sg_compact_production))
	{
		sg_host.dprint("slipgate: compact rune written; active rune remains in effect "
		               "until the next map setup\n");
		if (sg_host.flush)
			sg_host.flush();
		return;
	}

	sg_autoload_attempted = true;
	(void)SG_LevelSetupWithSource("write");
}

/* This is intentionally a narrow engine-owned translation of recorder facts.
 * A PMove record names an exact compact cell and its measured command time;
 * it may refine the cost of an already verified walking capability there.  A
 * hook event does not name a compact landing, weapon response, or strategic
 * outcome, so it is skipped rather than guessed.  The consumer rejects any
 * future engine mapping that names a fact outside this immutable model. */
static sg_rune_compact_learning_consumer_validation_t
SG_CompactLearningValidate(void *context, const sg_rune_compact_model_t *model,
	const sg_human_trace_v3_segment_ref_t *segment,
	const sg_human_trace_v3_event_t *event,
	sg_rune_compact_learning_consumer_claim_t *claim_out)
{
	sg_rune_q8_vec3_t point;
	sg_rune_compact_location_t location;
	uint32_t capability_index;
	uint32_t axis;

	(void)context;
	(void)segment;
	if (model == NULL || event == NULL || claim_out == NULL)
		return SG_RUNE_COMPACT_LEARNING_CONSUMER_VALIDATION_FATAL;
	if (event->kind != SG_HUMAN_TRACE_V3_EVENT_STEP ||
		event->command_msec == 0U)
		return SG_RUNE_COMPACT_LEARNING_CONSUMER_VALIDATION_SKIP;
	if (event->grounded != 1U || event->step_evidence !=
		SG_HUMAN_TRACE_V3_STEP_EVIDENCE_ORDINARY_DRY_WALK)
		return SG_RUNE_COMPACT_LEARNING_CONSUMER_VALIDATION_SKIP;
	for (axis = 0U; axis < 3U; axis++)
		point.value[axis] = (int32_t)event->after_origin[axis];
	if (SG_RuneCompactLocalize(model, &point, &location) !=
		SG_RUNE_COMPACT_LOCALIZE_OK ||
		(location.valid_stances & SG_RUNE_STANCE_VALID_STANDING) == 0U)
		return SG_RUNE_COMPACT_LEARNING_CONSUMER_VALIDATION_SKIP;
	for (capability_index = 0U;
		capability_index < model->movement_capability_count;
		capability_index++)
	{
		const sg_rune_movement_capability_t *capability =
			&model->movement_capabilities[capability_index];

		if (capability->cell.value != location.cell.value ||
			capability->kind != SG_RUNE_MOVEMENT_CAPABILITY_WALK ||
			(capability->source_stances & SG_RUNE_STANCE_VALID_STANDING) == 0U)
			continue;
		memset(claim_out, 0, sizeof(*claim_out));
		claim_out->key.kind =
			SG_RUNE_COMPACT_LEARNING_STABLE_CELL_CAPABILITY_COST;
		claim_out->key.value.cost.cell = location.cell;
		claim_out->key.value.cost.capability.value = capability_index;
		claim_out->key.value.cost.stance = SG_RUNE_STANCE_VALID_STANDING;
		claim_out->value = (float)event->command_msec / 1000.0f;
		return SG_RUNE_COMPACT_LEARNING_CONSUMER_VALIDATION_ACCEPT;
	}
	return SG_RUNE_COMPACT_LEARNING_CONSUMER_VALIDATION_SKIP;
}

void SG_CompactProductionPostMatchLearning(void)
{
	sg_rune_compact_learning_consumer_report_t report;
	sg_rune_compact_learning_consumer_status_t status;

	if (!SG_RuneCompactProductionCurrent(&sg_compact_production))
		return;
	memset(&report, 0, sizeof(report));
	status = SG_RuneCompactLearningProductionIngest(
		&sg_compact_production, level.mapname, SG_CompactLearningValidate,
		NULL, &report);
	if (status != SG_RUNE_COMPACT_LEARNING_CONSUMER_OK)
		sg_host.dprint("slipgate: compact learning rejected map=%s status=%s\n",
			level.mapname,
			SG_RuneCompactLearningConsumerStatusString(status));
}

uint32_t SG_CompactProductionLearningPriorCount(void)
{
	return SG_RuneCompactLearningProductionPriorCount(&sg_compact_production);
}

/* ----------------------------------------------------------------- body */



/* the role whose surface is being evaluated this frame -- SLIPGATE runs
 * its bots strictly serially, so a file-static carries it into the
 * detour arithmetic without widening every signature on the path */








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
			/* In 2v2, an enemy flag away from home commits both bots to attack. */
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

		/* Assign the nearest eligible teammate. A 300-unit incumbent bonus
		 * prevents equal candidates from exchanging the role every frame. */
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
				if (SG_BotLocalizationCell(&sg_bots[k]) < 0 ||
				    SG_BotLocalizationCell(&sg_bots[k]) >= SG_Rune()->hdr.num_seeds ||
				    !home)
					continue;
				route_cost = SG_EscortRouteCost(
				    sg_fields.our_carrier_valid[ti],
				    sg_fields.our_carrier[ti]
				        ? sg_fields.our_carrier[ti][SG_BotLocalizationCell(&sg_bots[k])]
				        : SG_FIELD_INF,
				    home[SG_BotLocalizationCell(&sg_bots[k])]);
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
	edict_t *carrier = SG_FlagCarrier(flag);
	int slot;

	if (!carrier || carrier->client->ctf.teamnum != team)
		return -1;
	for (slot = 0; slot < SG_MAXBOTS; slot++)
		if (sg_bots[slot].active && sg_bots[slot].ent == carrier)
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
	edict_t *carriers[2] = { NULL, NULL };
	int i, team_index;

	memset(sg_strike_role_valid, 0, sizeof(sg_strike_role_valid));
	memset(sg_strike_enemy_pressure_cache, 0,
	       sizeof(sg_strike_enemy_pressure_cache));
	memset(sg_strike_enemy_pressure_goal_cache, 0xff,
	       sizeof(sg_strike_enemy_pressure_goal_cache));
	memset(sg_strike_duty_cache, 0, sizeof(sg_strike_duty_cache));
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
		edict_t *own_carrier = SG_FlagCarrier(own_flag);
		edict_t *enemy_carrier = SG_FlagCarrier(enemy_flag);

		SG_StrikeFrameInit(&frames[team_index], level.time);
		frames[team_index].own_flag_home = own_flag && own_flag->inuse &&
		    !own_carrier && ctf_flagathome(own_flag);
		frames[team_index].enemy_flag_home = enemy_flag && enemy_flag->inuse &&
		    !enemy_carrier && ctf_flagathome(enemy_flag);
		frames[team_index].enemy_flag_dropped = enemy_flag && enemy_flag->inuse &&
		    !enemy_carrier && !frames[team_index].enemy_flag_home;
		frames[team_index].enemy_flag_carried = enemy_carrier &&
		    enemy_carrier->client->ctf.teamnum == team;
		carriers[team_index] = frames[team_index].enemy_flag_carried
		    ? enemy_carrier : NULL;
		frames[team_index].carrier_slot =
		    StrikeCarrierSlot(team, enemy_flag);
		frames[team_index].recent_enemy_room_death =
		    StrikeEnemyRoomDeath(team);
	}

	for (i = 0; i < SG_MAXBOTS; i++)
	{
		edict_t *ent = sg_bots[i].ent;
		sg_strike_slot_input_t *input;
		int team, bot_team_index, seed;
		const int *enemy_field, *own_field, *carrier_field, *home_field;
		sg_combat_weapon_state_t weapon;

		if (!sg_strike_role_valid[i] || !ent || !ent->client)
			continue;
		team = ent->client->ctf.teamnum;
		bot_team_index = SG_TeamIdx(team);
		seed = SG_BotLocalizationCell(&sg_bots[i]);
		enemy_field = StrikeEnemyField(team);
		own_field = StrikeOwnField(team);
		carrier_field = StrikeCarrierField(team);
		home_field = StrikeHomeField(team);
		input = &frames[bot_team_index].slot[i];
		input->present = 1;
		input->alive = ent->inuse && ent->deadflag == DEAD_NO && ent->health > 0;
		/* Preserve reserved defenders while admitting the real carrier. */
		input->carrying = SG_BotCarrying(ent);
		input->attack_eligible = StrikeAttackEligible(sg_strike_role_cache[i],
		    input->carrying, SG_ChatOrderedRole(ent));
		input->life_id = ent->client->ctf.ctfid;
		if (!SG_CombatWeaponState(ent, &weapon))
			memset(&weapon, 0, sizeof(weapon));
		input->weapon_tier = weapon.available_tier;
		input->enemy_flag_goal_ms = StrikeFieldCost(enemy_field, seed);
		input->recover_goal_ms = StrikeFieldCost(own_field, seed);
		input->carrier_goal_ms = StrikeFieldCost(carrier_field, seed);
		if (carriers[bot_team_index])
		{
			vec3_t delta;
			VectorSubtract(ent->s.origin, carriers[bot_team_index]->s.origin, delta);
			input->carrier_distance = VectorLength(delta);
			input->carrier_screen_clear = SG_StrikeCarrierScreenClear(
			    SG_CanSee(ent, carriers[bot_team_index]->s.origin,
				        (float)carriers[bot_team_index]->viewheight),
			        StrikeFieldCost(home_field, seed),
			        StrikeFieldCost(home_field,
			            sg_caco_team_belief.carrier[bot_team_index].seed));
		}
		input->direct_flag_touch = input->alive && input->attack_eligible &&
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
			int frame_team_index;
			qboolean participant = false;
			sg_strike_duty_t duty = SG_STRIKE_DUTY_NONE;

			if (!sg_strike_role_valid[i] || !ent || !ent->client)
				continue;
			frame_team_index = SG_TeamIdx(ent->client->ctf.teamnum);
			team = SG_StrikeAdapterTeam(&sg_strike_adapter, frame_team_index);
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
			        ? frames[frame_team_index].slot[i].enemy_flag_goal_ms : -1;
			sg_strike_duty_cache[i] = duty;
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
		sg_strike_duty_cache[slot] = SG_STRIKE_DUTY_NONE;
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

sg_strike_duty_t SG_StrikeDutySnapshot(const sg_bot_t *bot)
{
	int slot = bot ? (int)(bot - sg_bots) : -1;

	if (slot >= 0 && slot < SG_MAXBOTS && sg_strike_frame_ready &&
	    sg_strike_role_valid[slot])
		return sg_strike_duty_cache[slot];
	return SG_STRIKE_DUTY_NONE;
}

static sg_role_t StrikeRoleForBot(sg_bot_t *bot, qboolean carrying)
{
	int slot = bot ? (int)(bot - sg_bots) : -1;

	if (slot >= 0 && slot < SG_MAXBOTS && sg_strike_frame_ready &&
	    sg_strike_role_valid[slot])
		return sg_strike_role_cache[slot];
	return SG_Role(bot, carrying);
}

static sg_role_t CompactRoleForBot(sg_bot_t *bot, qboolean carrying)
{
	int forced;

	if (!bot || !bot->ent)
		return SG_ROLE_ATTACK;
	if (carrying)
		return SG_ROLE_CARRY;
	forced = SG_ChatOrderedRole(bot->ent);
	if (forced >= 0 &&
	    (forced != SG_ROLE_ESCORT || SG_ChatEscortTarget(bot->ent)))
	{
		if (forced == SG_ROLE_DEFEND)
			bot->def_stand = true;
		return (sg_role_t)forced;
	}
	return SG_ROLE_ATTACK;
}

static uint64_t StrategyNowMs(void)
{
	double milliseconds = (double)level.time * 1000.0;

	if (!isfinite(milliseconds) || milliseconds < 0.0)
		return 1U;
	if (milliseconds >= (double)UINT64_MAX - 1.0)
		return UINT64_MAX;
	return (uint64_t)milliseconds + 1U;
}

static edict_t *StrategyCarrierEntity(
	const sg_destination_carrier_ref_t *carrier)
{
	edict_t *entity;
	int client;

	if (!carrier || (carrier->team != CTF_TEAM_RED &&
	    carrier->team != CTF_TEAM_BLUE))
		return NULL;
	if (carrier->selector == SG_DESTINATION_CARRIER_EXACT)
	{
		if (carrier->client_id >= (uint16_t)game.maxclients)
			return NULL;
		entity = &g_edicts[(int)carrier->client_id + 1];
		return entity->inuse && entity->client &&
			entity->client->ctf.teamnum == carrier->team &&
			ClientHasFlag(entity) != NULL ? entity : NULL;
	}
	if (carrier->selector != SG_DESTINATION_CARRIER_ANY)
		return NULL;
	for (client = 0; client < game.maxclients; client++)
	{
		entity = &g_edicts[client + 1];
		if (entity->inuse && entity->client &&
		    entity->client->ctf.teamnum == carrier->team &&
		    ClientHasFlag(entity) != NULL)
			return entity;
	}
	return NULL;
}

static int StrategyTeamCarrierPresent(int team)
{
	sg_destination_carrier_ref_t carrier;

	memset(&carrier, 0, sizeof(carrier));
	carrier.team = (uint8_t)team;
	carrier.selector = SG_DESTINATION_CARRIER_ANY;
	carrier.client_id = UINT16_MAX;
	return StrategyCarrierEntity(&carrier) != NULL;
}

static int StrategyExecutionLivePose(const sg_destination_ref_t *destination,
	const sg_compact_localized_state_t *localized_player,
	sg_rune_compact_field_service_live_pose_t *pose_out)
{
	edict_t *entity = NULL;
	uint32_t axis;

	if (!destination || !localized_player || !pose_out)
		return 0;
	memset(pose_out, 0, sizeof(*pose_out));
	switch (destination->kind)
	{
	case SG_DESTINATION_FLAG:
		if (destination->value.flag.location == SG_DESTINATION_FLAG_HOME)
			return 1;
		if (destination->value.flag.location != SG_DESTINATION_FLAG_CURRENT)
			return 0;
		entity = ctf_flagsearch(destination->value.flag.team);
		if (entity != NULL && SG_FlagCarrier(entity) != NULL)
			entity = SG_FlagCarrier(entity);
		break;
	case SG_DESTINATION_CARRIER:
	case SG_DESTINATION_ESCORT:
	case SG_DESTINATION_INTERCEPT:
		entity = StrategyCarrierEntity(&destination->value.carrier);
		break;
	case SG_DESTINATION_LEARNED_POINT:
	case SG_DESTINATION_WAYPOINT:
		/* These semantic kinds require an owner-specific current observation.
		 * This host does not issue either kind, so fail closed if one appears. */
		return 0;
	case SG_DESTINATION_ITEM:
	case SG_DESTINATION_WEAPON:
	case SG_DESTINATION_ARMOR:
	case SG_DESTINATION_POWERUP:
	case SG_DESTINATION_DEFENSIVE_POST:
		return 1;
	case SG_DESTINATION_KIND_COUNT:
	default:
		return 0;
	}
	if (!entity || !entity->inuse || localized_player->frame_sequence == 0U ||
	    localized_player->localized_at_ms == 0U)
		return 0;
	pose_out->present = 1U;
	pose_out->generation = localized_player->frame_sequence;
	pose_out->observed_at_ms = localized_player->localized_at_ms;
	for (axis = 0U; axis < 3U; axis++)
	{
		pose_out->position[axis] = entity->s.origin[axis];
		pose_out->velocity[axis] = entity->velocity[axis];
	}
	return 1;
}

static int StrategyPopulateExecutionPoses(
	sg_strategy_runtime_plan_request_t *request)
{
	uint16_t execution_index;

	if (!request || !request->localized_player)
		return 0;
	for (execution_index = 0U; execution_index < request->execution_count;
	     execution_index++)
	{
		sg_strategy_runtime_execution_t *execution =
			&request->executions[execution_index];
		const sg_destination_ref_t *destination = NULL;
		uint16_t goal_index;

		for (goal_index = 0U; goal_index < request->spec.goal_count; goal_index++)
		{
			const sg_strategy_goal_spec_t *goal =
				&request->spec.goals[goal_index];
			uint8_t choice_index;

			if (goal->id != execution->goal_id)
				continue;
			for (choice_index = 0U; choice_index < goal->choice_count;
			     choice_index++)
				if (goal->choices[choice_index].id == execution->target_id)
				{
					if (destination != NULL)
						return 0;
					destination = &goal->choices[choice_index].destination;
				}
		}
		if (!destination || !StrategyExecutionLivePose(destination,
			request->localized_player, &execution->live_pose))
			return 0;
	}
	return 1;
}

static uint64_t StrategyCommitmentWord(uint64_t hash, uint64_t word)
{
	hash ^= word;
	return hash * UINT64_C(1099511628211);
}

static void StrategyCarrierDestination(sg_destination_ref_t *destination,
	sg_destination_kind_t kind, int team, int client)
{
	memset(destination, 0, sizeof(*destination));
	destination->kind = kind;
	destination->value.carrier.team = (uint8_t)team;
	if (client >= 0 && client < UINT16_MAX)
	{
		destination->value.carrier.selector = SG_DESTINATION_CARRIER_EXACT;
		destination->value.carrier.client_id = (uint16_t)client;
	}
	else
	{
		destination->value.carrier.selector = SG_DESTINATION_CARRIER_ANY;
		destination->value.carrier.client_id = UINT16_MAX;
	}
}

static void StrategyFlagDestination(sg_destination_ref_t *destination,
	int team, sg_destination_flag_location_t location)
{
	memset(destination, 0, sizeof(*destination));
	destination->kind = SG_DESTINATION_FLAG;
	destination->value.flag.team = (uint8_t)team;
	destination->value.flag.location = (uint8_t)location;
}

static void StrategyPointDestination(sg_destination_ref_t *destination,
	sg_destination_kind_t kind, uint64_t semantic_id)
{
	memset(destination, 0, sizeof(*destination));
	destination->kind = kind;
	if (kind == SG_DESTINATION_DEFENSIVE_POST)
		destination->value.post.region_id = (uint32_t)semantic_id;
	else
		destination->value.point.point_id = semantic_id;
}

#define SG_STRATEGY_PRIMARY_GOAL_ID UINT32_C(1)
#define SG_STRATEGY_PRIMARY_TARGET_ID UINT32_C(1)
#define SG_STRATEGY_PRIMARY_ALTERNATE_TARGET_ID UINT32_C(2)

static uint64_t StrategyCommitmentDestination(uint64_t hash,
	const sg_destination_ref_t *destination)
{
	hash = StrategyCommitmentWord(hash, (uint64_t)destination->kind);
	switch (destination->kind)
	{
	case SG_DESTINATION_FLAG:
		hash = StrategyCommitmentWord(hash, destination->value.flag.team);
		hash = StrategyCommitmentWord(hash, destination->value.flag.location);
		break;
	case SG_DESTINATION_ITEM:
	case SG_DESTINATION_WEAPON:
	case SG_DESTINATION_ARMOR:
	case SG_DESTINATION_POWERUP:
		hash = StrategyCommitmentWord(hash,
			destination->value.item.item_id);
		break;
	case SG_DESTINATION_CARRIER:
	case SG_DESTINATION_ESCORT:
	case SG_DESTINATION_INTERCEPT:
		hash = StrategyCommitmentWord(hash,
			destination->value.carrier.team);
		hash = StrategyCommitmentWord(hash,
			destination->value.carrier.selector);
		hash = StrategyCommitmentWord(hash,
			destination->value.carrier.client_id);
		break;
	case SG_DESTINATION_DEFENSIVE_POST:
		hash = StrategyCommitmentWord(hash,
			destination->value.post.region_id);
		break;
	case SG_DESTINATION_LEARNED_POINT:
	case SG_DESTINATION_WAYPOINT:
		hash = StrategyCommitmentWord(hash,
			destination->value.point.point_id);
		break;
	case SG_DESTINATION_KIND_COUNT:
	default:
		break;
	}
	return hash;
}

static uint64_t StrategyRequestCommitment(
	const sg_strategy_runtime_plan_request_t *request)
{
	uint64_t hash = UINT64_C(1469598103934665603);
	uint16_t goal_index;
	uint16_t execution_index;

	hash = StrategyCommitmentWord(hash, request->authority.rank);
	hash = StrategyCommitmentWord(hash, request->authority.principal_kind);
	hash = StrategyCommitmentWord(hash, request->authority.principal_id);
	for (goal_index = 0U; goal_index < request->spec.goal_count; goal_index++)
	{
		const sg_strategy_goal_spec_t *goal = &request->spec.goals[goal_index];
		uint8_t dependency_index;
		uint8_t choice_index;

		hash = StrategyCommitmentWord(hash, goal->id);
		hash = StrategyCommitmentWord(hash, goal->kind);
		hash = StrategyCommitmentWord(hash, (uint16_t)goal->priority);
		hash = StrategyCommitmentWord(hash, goal->dependency_count);
		for (dependency_index = 0U;
		     dependency_index < goal->dependency_count;
		     dependency_index++)
		{
			hash = StrategyCommitmentWord(hash,
				goal->dependencies[dependency_index].goal_id);
			hash = StrategyCommitmentWord(hash,
				goal->dependencies[dependency_index].accept);
		}
		for (choice_index = 0U; choice_index < goal->choice_count;
		     choice_index++)
		{
			hash = StrategyCommitmentWord(hash,
				goal->choices[choice_index].id);
			hash = StrategyCommitmentDestination(hash,
				&goal->choices[choice_index].destination);
		}
	}
	hash = StrategyCommitmentWord(hash, request->execution_count);
	for (execution_index = 0U;
	     execution_index < request->execution_count; execution_index++)
	{
		const sg_strategy_runtime_execution_t *execution =
			&request->executions[execution_index];

		hash = StrategyCommitmentWord(hash, execution->goal_id);
		hash = StrategyCommitmentWord(hash, execution->target_id);
		hash = StrategyCommitmentWord(hash, (uint64_t)(uint32_t)execution->role);
	}
	return hash ? hash : 1U;
}

static sg_strategy_goal_spec_t *StrategyRequestGoal(
	sg_strategy_runtime_plan_request_t *request, sg_strategy_goal_id_t id,
	sg_strategy_goal_kind_t kind, int16_t priority)
{
	sg_strategy_goal_spec_t *goal;

	if (!request || request->spec.goal_count >= SG_STRATEGY_MAX_GOALS)
		return NULL;
	goal = &request->spec.goals[request->spec.goal_count];
	memset(goal, 0, sizeof(*goal));
	goal->id = id;
	goal->kind = kind;
	goal->priority = priority;
	goal->unavailable = SG_STRATEGY_UNAVAILABLE_WAIT;
	goal->failure.try_alternatives = 1U;
	goal->failure.max_attempts_per_choice = UINT8_MAX;
	goal->failure.retry_wake.kind = SG_STRATEGY_RETRY_TARGET_REVISION;
	goal->failure.exhausted = SG_STRATEGY_FAILURE_SKIP_GOAL;
	request->spec.goal_count++;
	return goal;
}

static int StrategyRequestChoice(sg_strategy_runtime_plan_request_t *request,
	sg_strategy_goal_spec_t *goal, sg_strategy_target_id_t target_id,
	const sg_destination_ref_t *destination, int role)
{
	uint8_t choice_index;
	sg_strategy_runtime_execution_t *execution;

	if (!request || !goal || target_id == 0U || !destination ||
	    !SG_DestinationRefValid(destination) ||
	    goal->choice_count >= SG_STRATEGY_MAX_CHOICES ||
	    request->execution_count >= SG_STRATEGY_CALLER_MAX_BINDINGS)
		return 0;
	choice_index = goal->choice_count;
	goal->choices[choice_index].id = target_id;
	goal->choices[choice_index].destination = *destination;
	goal->choice_count++;
	execution = &request->executions[request->execution_count];
	memset(execution, 0, sizeof(*execution));
	execution->goal_id = goal->id;
	execution->target_id = target_id;
	execution->role = role;
	request->execution_count++;
	return 1;
}

static int StrategyPrimaryDestination(sg_bot_t *bot, sg_think_t *tc,
	sg_strike_duty_t strike_duty, sg_strategy_goal_kind_t *kind_out,
	sg_destination_ref_t *primary_out, sg_destination_ref_t *alternate_out,
	int *has_alternate_out)
{
	int team;
	int enemy;

	if (!bot || !tc || !kind_out || !primary_out || !alternate_out ||
	    !has_alternate_out)
		return 0;
	team = tc->team;
	enemy = SG_EnemyTeam(team);
	memset(primary_out, 0, sizeof(*primary_out));
	memset(alternate_out, 0, sizeof(*alternate_out));
	*has_alternate_out = 0;
	if (tc->carrying || strike_duty == SG_STRIKE_DUTY_CARRY)
	{
		*kind_out = SG_STRATEGY_GOAL_CARRY_FLAG;
		StrategyFlagDestination(primary_out, team, SG_DESTINATION_FLAG_HOME);
		StrategyFlagDestination(alternate_out, team,
			SG_DESTINATION_FLAG_CURRENT);
	}
	else if (strike_duty == SG_STRIKE_DUTY_RECOVER ||
	         tc->role == SG_ROLE_RECOVER)
	{
		*kind_out = SG_STRATEGY_GOAL_RECOVER_FLAG;
		StrategyFlagDestination(primary_out, team,
			SG_DESTINATION_FLAG_CURRENT);
		StrategyFlagDestination(alternate_out, team, SG_DESTINATION_FLAG_HOME);
		*has_alternate_out = 1;
	}
	else if (strike_duty == SG_STRIKE_DUTY_ESCORT ||
	         tc->role == SG_ROLE_ESCORT)
	{
		edict_t *target = SG_ChatEscortTarget(tc->e);
		int client = target ? (int)(target - g_edicts) - 1 : -1;

		*kind_out = SG_STRATEGY_GOAL_ESCORT_CARRIER;
		StrategyCarrierDestination(primary_out, SG_DESTINATION_ESCORT, team,
			client);
		if (client >= 0)
		{
			StrategyCarrierDestination(alternate_out,
				SG_DESTINATION_ESCORT, team, -1);
			*has_alternate_out = 1;
		}
	}
	else if (tc->role == SG_ROLE_DEFEND &&
	         StrategyTeamCarrierPresent(enemy))
	{
		*kind_out = SG_STRATEGY_GOAL_INTERCEPT_CARRIER;
		StrategyCarrierDestination(primary_out, SG_DESTINATION_INTERCEPT,
			enemy, -1);
	}
	else if (tc->role == SG_ROLE_DEFEND)
	{
		*kind_out = SG_STRATEGY_GOAL_DEFEND_POST;
		StrategyPointDestination(primary_out, SG_DESTINATION_DEFENSIVE_POST,
			(uint64_t)(unsigned)team);
		StrategyPointDestination(alternate_out, SG_DESTINATION_DEFENSIVE_POST,
			(uint64_t)(unsigned)team + UINT64_C(2));
		*has_alternate_out = 1;
	}
	else
	{
		*kind_out = SG_STRATEGY_GOAL_CAPTURE_FLAG;
		StrategyFlagDestination(primary_out, enemy,
			SG_DESTINATION_FLAG_CURRENT);
		StrategyFlagDestination(alternate_out, enemy, SG_DESTINATION_FLAG_HOME);
		*has_alternate_out = 1;
	}
	return SG_DestinationRefValid(primary_out) &&
		(!*has_alternate_out || SG_DestinationRefValid(alternate_out));
}

static int StrategyPolicyAuthority(const sg_bot_t *bot, const sg_think_t *tc,
	sg_strategy_caller_authority_t *authority_out)
{
	int ordered_role;
	int order_principal;
	int slot;

	if (!bot || !tc || !authority_out)
		return 0;
	ordered_role = SG_ChatOrderedRole(tc->e);
	order_principal = SG_ChatOrderPrincipal(tc->e);
	memset(authority_out, 0, sizeof(*authority_out));
	if (ordered_role >= 0 && order_principal >= 0)
	{
		authority_out->rank = SG_STRATEGY_AUTHORITY_HUMAN;
		authority_out->principal_kind = SG_STRATEGY_PRINCIPAL_HUMAN;
		authority_out->principal_id = (uint32_t)order_principal + 1U;
		return authority_out->principal_id != 0U;
	}
	slot = (int)(bot - sg_bots);
	if (slot < 0 || slot >= SG_MAXBOTS)
		return 0;
	authority_out->rank = SG_STRATEGY_AUTHORITY_AUTONOMOUS;
	authority_out->principal_kind = SG_STRATEGY_PRINCIPAL_AUTONOMOUS;
	authority_out->principal_id = (uint32_t)slot + 1U;
	return 1;
}

static int StrategyPlanRequest(sg_bot_t *bot, sg_think_t *tc,
	sg_strike_duty_t strike_duty, sg_strategy_runtime_plan_request_t *request)
{
	sg_strategy_goal_spec_t *primary;
	sg_destination_ref_t primary_destination;
	sg_destination_ref_t alternate_destination;
	sg_strategy_goal_kind_t primary_kind;
	int has_alternate;

	if (!bot || !tc || !request)
		return 0;
	memset(request, 0, sizeof(*request));
	if (!StrategyPolicyAuthority(bot, tc, &request->authority))
		return 0;
	if (!StrategyPrimaryDestination(bot, tc, strike_duty, &primary_kind,
		&primary_destination, &alternate_destination, &has_alternate))
		return 0;
	primary = StrategyRequestGoal(request, SG_STRATEGY_PRIMARY_GOAL_ID,
		primary_kind, INT16_C(50));
	if (!primary || !StrategyRequestChoice(request, primary,
		SG_STRATEGY_PRIMARY_TARGET_ID,
		&primary_destination, (int)tc->role))
		return 0;
	if (has_alternate && !StrategyRequestChoice(request, primary,
		SG_STRATEGY_PRIMARY_ALTERNATE_TARGET_ID,
		&alternate_destination, (int)tc->role))
		return 0;
	request->commitment_id = StrategyRequestCommitment(request);
	return true;
}

static int StrategyAuthorityEqual(
	const sg_strategy_caller_authority_t *left,
	const sg_strategy_caller_authority_t *right)
{
	return left && right && left->rank == right->rank &&
		left->principal_kind == right->principal_kind &&
		left->principal_id == right->principal_id;
}

/* Refresh an admitted plan without importing legacy execution data.  The
 * destination-field authority re-resolves every retained semantic target and
 * leases the resulting compact field handle; settled prerequisites remain
 * committed while the next goal activates. */
static int StrategyActivePlanRequest(const sg_strategy_caller_t *caller,
	sg_strategy_runtime_plan_request_t *request)
{
	uint16_t index;

	if (!caller || !request || !caller->has_plan || !caller->reducer.has_plan ||
	    caller->plan.commitment_id == 0U || caller->plan.binding_count == 0U ||
	    caller->plan.binding_count > SG_STRATEGY_CALLER_MAX_BINDINGS)
		return 0;
	memset(request, 0, sizeof(*request));
	request->commitment_id = caller->plan.commitment_id;
	request->authority = caller->plan.authority;
	request->spec = caller->plan.spec;
	request->execution_count = caller->plan.binding_count;
	for (index = 0U; index < caller->plan.binding_count; index++)
	{
		const sg_strategy_caller_target_binding_t *binding =
			&caller->plan.bindings[index];

		request->executions[index].goal_id = binding->goal_id;
		request->executions[index].target_id = binding->target_id;
		request->executions[index].role = binding->role;
	}
	return 1;
}

static int StrategyPlanTerminal(const sg_strategy_caller_t *caller)
{
	if (!caller)
		return 1;
	switch (caller->reducer.current_instruction.kind)
	{
	case SG_STRATEGY_INSTRUCTION_COMPLETED:
	case SG_STRATEGY_INSTRUCTION_FAILED:
	case SG_STRATEGY_INSTRUCTION_CANCELLED:
		return 1;
	case SG_STRATEGY_INSTRUCTION_EMPTY:
	case SG_STRATEGY_INSTRUCTION_WAIT_LIFE:
	case SG_STRATEGY_INSTRUCTION_WAIT_CONDITION:
	case SG_STRATEGY_INSTRUCTION_WAIT_DESTINATION:
	case SG_STRATEGY_INSTRUCTION_EXECUTE:
	case SG_STRATEGY_INSTRUCTION_SUSPENDED:
		return 0;
	case SG_STRATEGY_INSTRUCTION_KIND_COUNT:
	default:
		return 1;
	}
}

static int StrategyDestinationEqual(const sg_destination_ref_t *left,
	const sg_destination_ref_t *right)
{
	if (!left || !right || left->kind != right->kind)
		return 0;
	switch (left->kind)
	{
	case SG_DESTINATION_FLAG:
		return left->value.flag.team == right->value.flag.team &&
			left->value.flag.location == right->value.flag.location;
	case SG_DESTINATION_ITEM:
	case SG_DESTINATION_WEAPON:
	case SG_DESTINATION_ARMOR:
	case SG_DESTINATION_POWERUP:
		return left->value.item.item_id == right->value.item.item_id;
	case SG_DESTINATION_CARRIER:
	case SG_DESTINATION_ESCORT:
	case SG_DESTINATION_INTERCEPT:
		return left->value.carrier.client_id ==
				right->value.carrier.client_id &&
			left->value.carrier.team == right->value.carrier.team &&
			left->value.carrier.selector == right->value.carrier.selector;
	case SG_DESTINATION_DEFENSIVE_POST:
		return left->value.post.region_id == right->value.post.region_id;
	case SG_DESTINATION_LEARNED_POINT:
	case SG_DESTINATION_WAYPOINT:
		return left->value.point.point_id == right->value.point.point_id;
	case SG_DESTINATION_KIND_COUNT:
	default:
		return 0;
	}
}

static int StrategyFactKeyEqual(const sg_strategy_fact_key_t *left,
	const sg_strategy_fact_key_t *right)
{
	return left && right && left->kind == right->kind &&
		left->subject_id == right->subject_id && left->team == right->team;
}

static int StrategyConditionEqual(const sg_strategy_condition_t *left,
	const sg_strategy_condition_t *right)
{
	if (!left || !right || left->kind != right->kind ||
	    left->scope != right->scope)
		return 0;
	switch (left->kind)
	{
	case SG_STRATEGY_CONDITION_FACT_EQUALS:
		return StrategyFactKeyEqual(&left->value.fact.key,
			&right->value.fact.key) &&
			left->value.fact.expected_value == right->value.fact.expected_value;
	case SG_STRATEGY_CONDITION_TIME_WINDOW:
		return left->value.time.not_before_ms ==
				right->value.time.not_before_ms &&
			left->value.time.not_after_ms == right->value.time.not_after_ms;
	case SG_STRATEGY_CONDITION_KIND_COUNT:
	default:
		return 0;
	}
}

static int StrategyRetryWakeEqual(const sg_strategy_retry_wake_t *left,
	const sg_strategy_retry_wake_t *right)
{
	if (!left || !right || left->kind != right->kind)
		return 0;
	switch (left->kind)
	{
	case SG_STRATEGY_RETRY_FACT_REVISION:
		return StrategyFactKeyEqual(&left->fact, &right->fact);
	case SG_STRATEGY_RETRY_NOT_BEFORE:
		return left->delay_ms == right->delay_ms;
	case SG_STRATEGY_RETRY_NONE:
	case SG_STRATEGY_RETRY_NEXT_FRAME:
	case SG_STRATEGY_RETRY_TARGET_REVISION:
		return 1;
	case SG_STRATEGY_RETRY_WAKE_COUNT:
	default:
		return 0;
	}
}

static int StrategyFailureEqual(const sg_strategy_failure_rule_t *left,
	const sg_strategy_failure_rule_t *right)
{
	return left && right &&
		left->try_alternatives == right->try_alternatives &&
		left->max_attempts_per_choice == right->max_attempts_per_choice &&
		left->exhausted == right->exhausted &&
		StrategyRetryWakeEqual(&left->retry_wake, &right->retry_wake);
}

static int StrategyGoalSemanticsEqual(const sg_strategy_goal_spec_t *left,
	const sg_strategy_goal_spec_t *right)
{
	uint8_t index;

	if (!left || !right || left->id != right->id ||
	    left->kind != right->kind || left->priority != right->priority ||
	    left->unavailable != right->unavailable ||
	    left->condition_count != right->condition_count ||
	    left->choice_count != right->choice_count ||
	    !StrategyFailureEqual(&left->failure, &right->failure))
		return 0;
	for (index = 0U; index < left->condition_count; index++)
		if (!StrategyConditionEqual(&left->conditions[index],
			&right->conditions[index]))
			return 0;
	for (index = 0U; index < left->choice_count; index++)
		if (left->choices[index].id != right->choices[index].id ||
		    !StrategyDestinationEqual(&left->choices[index].destination,
				&right->choices[index].destination))
			return 0;
	return 1;
}

static const sg_strategy_goal_spec_t *StrategyGoalSpecFor(
	const sg_strategy_plan_spec_t *spec, sg_strategy_goal_id_t goal_id)
{
	uint16_t index;

	if (!spec)
		return NULL;
	for (index = 0U; index < spec->goal_count; index++)
		if (spec->goals[index].id == goal_id)
			return &spec->goals[index];
	return NULL;
}

static int StrategyGoalTerminalById(const sg_strategy_caller_t *caller,
	sg_strategy_goal_id_t goal_id, int *terminal_out)
{
	uint16_t index;

	if (!caller || !terminal_out)
		return 0;
	for (index = 0U; index < caller->reducer.plan.goal_count; index++)
	{
		sg_strategy_goal_phase_t phase;

		if (caller->reducer.plan.goals[index].id != goal_id)
			continue;
		phase = caller->reducer.goals[index].phase;
		*terminal_out = phase == SG_STRATEGY_GOAL_SUCCEEDED ||
			phase == SG_STRATEGY_GOAL_SKIPPED ||
			phase == SG_STRATEGY_GOAL_FAILED ||
			phase == SG_STRATEGY_GOAL_CANCELLED;
		return 1;
	}
	return 0;
}

static const sg_strategy_runtime_execution_t *StrategyExecutionFor(
	const sg_strategy_runtime_plan_request_t *request,
	sg_strategy_goal_id_t goal_id, sg_strategy_target_id_t target_id)
{
	const sg_strategy_runtime_execution_t *found = NULL;
	uint16_t index;

	if (!request)
		return NULL;
	for (index = 0U; index < request->execution_count; index++)
	{
		const sg_strategy_runtime_execution_t *execution =
			&request->executions[index];

		if (execution->goal_id != goal_id ||
		    execution->target_id != target_id)
			continue;
		if (found)
			return NULL;
		found = execution;
	}
	return found;
}

static const sg_strategy_caller_target_binding_t *StrategyBindingFor(
	const sg_strategy_caller_plan_t *plan, sg_strategy_goal_id_t goal_id,
	sg_strategy_target_id_t target_id)
{
	const sg_strategy_caller_target_binding_t *found = NULL;
	uint16_t index;

	if (!plan)
		return NULL;
	for (index = 0U; index < plan->binding_count; index++)
	{
		const sg_strategy_caller_target_binding_t *binding =
			&plan->bindings[index];

		if (binding->goal_id != goal_id || binding->target_id != target_id)
			continue;
		if (found)
			return NULL;
		found = binding;
	}
	return found;
}

static int StrategyGoalRolesEqual(
	const sg_strategy_runtime_plan_request_t *candidate,
	const sg_strategy_caller_plan_t *admitted,
	const sg_strategy_goal_spec_t *goal)
{
	uint8_t index;

	if (!candidate || !admitted || !goal)
		return 0;
	for (index = 0U; index < goal->choice_count; index++)
	{
		const sg_strategy_runtime_execution_t *execution =
			StrategyExecutionFor(candidate, goal->id, goal->choices[index].id);
		const sg_strategy_caller_target_binding_t *binding =
			StrategyBindingFor(admitted, goal->id, goal->choices[index].id);

		if (!execution || !binding || execution->role != binding->role)
			return 0;
	}
	return 1;
}

/* A fresh current request omits prerequisites that the reducer has already
 * settled.  Ignore only those terminal dependency edges; every current goal,
 * role, destination, failure rule, condition, and authority must still match
 * before an autonomous plan may be retained. */
static int StrategyGoalDependenciesMatchLivePlan(
	const sg_strategy_goal_spec_t *candidate,
	const sg_strategy_goal_spec_t *admitted,
	const sg_strategy_caller_t *caller)
{
	uint8_t candidate_index;
	uint8_t admitted_index = 0U;

	if (!candidate || !admitted || !caller)
		return 0;
	for (candidate_index = 0U;
	     candidate_index < candidate->dependency_count; candidate_index++)
	{
		const sg_strategy_dependency_spec_t *candidate_dependency =
			&candidate->dependencies[candidate_index];
		int terminal;

		if (!StrategyGoalTerminalById(caller,
			candidate_dependency->goal_id, &terminal) || terminal)
			return 0;
		while (admitted_index < admitted->dependency_count)
		{
			const sg_strategy_dependency_spec_t *admitted_dependency =
				&admitted->dependencies[admitted_index];

			if (!StrategyGoalTerminalById(caller,
				admitted_dependency->goal_id, &terminal))
				return 0;
			if (!terminal)
				break;
			admitted_index++;
		}
		if (admitted_index >= admitted->dependency_count ||
		    admitted->dependencies[admitted_index].goal_id !=
				candidate_dependency->goal_id ||
		    admitted->dependencies[admitted_index].accept !=
				candidate_dependency->accept)
			return 0;
		admitted_index++;
	}
	while (admitted_index < admitted->dependency_count)
	{
		int terminal;

		if (!StrategyGoalTerminalById(caller,
			admitted->dependencies[admitted_index].goal_id, &terminal) ||
		    !terminal)
			return 0;
		admitted_index++;
	}
	return 1;
}

static int StrategyRequestMatchesLivePlan(
	const sg_strategy_runtime_plan_request_t *candidate,
	const sg_strategy_caller_t *caller)
{
	uint16_t index;

	if (!candidate || !caller || !caller->has_plan ||
	    !StrategyAuthorityEqual(&candidate->authority,
		&caller->plan.authority))
		return 0;
	for (index = 0U; index < candidate->spec.goal_count; index++)
	{
		const sg_strategy_goal_spec_t *candidate_goal =
			&candidate->spec.goals[index];
		const sg_strategy_goal_spec_t *admitted_goal = StrategyGoalSpecFor(
			&caller->plan.spec, candidate_goal->id);
		int terminal;

		if (!admitted_goal || !StrategyGoalTerminalById(caller,
			candidate_goal->id, &terminal) || terminal ||
		    !StrategyGoalSemanticsEqual(candidate_goal, admitted_goal) ||
		    !StrategyGoalDependenciesMatchLivePlan(candidate_goal,
				admitted_goal, caller) ||
		    !StrategyGoalRolesEqual(candidate, &caller->plan, candidate_goal))
			return 0;
	}
	for (index = 0U; index < caller->plan.spec.goal_count; index++)
	{
		const sg_strategy_goal_spec_t *admitted_goal =
			&caller->plan.spec.goals[index];
		int terminal;

		if (!StrategyGoalTerminalById(caller, admitted_goal->id, &terminal))
			return 0;
		if (!terminal && !StrategyGoalSpecFor(&candidate->spec,
			admitted_goal->id))
			return 0;
	}
	return 1;
}

/* Autonomous plans are reusable only while the complete current semantic
 * request still describes the admitted live queue.  CAPTURE->RECOVER, a role
 * change, authority change, or any target/spec change deliberately returns a
 * new request so the caller replaces the plan instead of retargeting it. */
static int StrategyPlanReusable(sg_bot_t *bot, sg_think_t *tc,
	sg_strike_duty_t strike_duty)
{
	sg_strategy_caller_authority_t policy_authority;
	sg_strategy_runtime_plan_request_t candidate;

	if (!bot || !tc || !bot->strategy.has_plan ||
	    !bot->strategy.reducer.has_plan || StrategyPlanTerminal(&bot->strategy) ||
	    !StrategyPolicyAuthority(bot, tc, &policy_authority) ||
	    !StrategyAuthorityEqual(&bot->strategy.plan.authority,
		&policy_authority))
		return 0;
	return StrategyPlanRequest(bot, tc, strike_duty, &candidate) &&
		StrategyRequestMatchesLivePlan(&candidate, &bot->strategy);
}

static int StrategyFramePlanRequest(sg_bot_t *bot, sg_think_t *tc,
	sg_strike_duty_t strike_duty, sg_strategy_runtime_plan_request_t *request)
{
	if (StrategyPlanReusable(bot, tc, strike_duty))
		return StrategyActivePlanRequest(&bot->strategy, request);
	return StrategyPlanRequest(bot, tc, strike_duty, request);
}

static sg_strategy_tactical_block_reason_t StrategyBlockReason(
	const sg_bot_t *bot, const sg_think_t *tc)
{
	if (tc && tc->e && SG_CombatWouldEngage(tc->e))
		return SG_STRATEGY_BLOCK_COMBAT;
	if (bot && (bot->mate_block_last || bot->door_held_last ||
	            bot->deaddoor_ahead))
		return SG_STRATEGY_BLOCK_OBSTRUCTION;
	if (bot && (bot->hook_phase != 0 || bot->speedhook ||
	            bot->air_hook_launch_active))
		return SG_STRATEGY_BLOCK_HOOK_OPPORTUNITY;
	return SG_STRATEGY_BLOCK_NONE;
}

static void StrategyAdvanceLiveGoal(sg_bot_t *bot, const sg_think_t *tc,
	uint64_t at_ms)
{
	const sg_strategy_instruction_t *instruction;
	sg_strategy_goal_kind_t goal_kind = SG_STRATEGY_GOAL_KIND_COUNT;
	sg_strategy_goal_outcome_kind_t outcome = SG_STRATEGY_OUTCOME_NONE;
	sg_strategy_failure_reason_t failure = SG_STRATEGY_FAILURE_NONE;
	sg_strategy_caller_output_t output;
	edict_t *own_flag;
	uint16_t goal_index;

	if (!bot || !tc || at_ms == 0U || !bot->strategy.has_plan)
		return;
	instruction = &bot->strategy.reducer.current_instruction;
	if (instruction->kind != SG_STRATEGY_INSTRUCTION_EXECUTE &&
	    instruction->kind != SG_STRATEGY_INSTRUCTION_SUSPENDED)
		return;
	for (goal_index = 0U; goal_index < bot->strategy.reducer.plan.goal_count;
	     goal_index++)
		if (bot->strategy.reducer.plan.goals[goal_index].id ==
		    instruction->goal_id)
		{
			goal_kind = bot->strategy.reducer.plan.goals[goal_index].kind;
			break;
		}
	switch (goal_kind)
	{
	case SG_STRATEGY_GOAL_COLLECT_ITEM:
		break;
	case SG_STRATEGY_GOAL_CAPTURE_FLAG:
		if (tc->carrying)
			outcome = SG_STRATEGY_OUTCOME_COMPLETED;
		break;
	case SG_STRATEGY_GOAL_CARRY_FLAG:
		if (!tc->carrying)
		{
			outcome = SG_STRATEGY_OUTCOME_FAILED;
			failure = SG_STRATEGY_FAILURE_UNAVAILABLE;
		}
		break;
	case SG_STRATEGY_GOAL_RECOVER_FLAG:
		own_flag = ctf_flagsearch(tc->team);
		if (own_flag && own_flag->inuse && ctf_flagathome(own_flag))
			outcome = SG_STRATEGY_OUTCOME_COMPLETED;
		break;
	case SG_STRATEGY_GOAL_DESTINATION:
	case SG_STRATEGY_GOAL_ESCORT_CARRIER:
	case SG_STRATEGY_GOAL_INTERCEPT_CARRIER:
	case SG_STRATEGY_GOAL_DEFEND_POST:
	case SG_STRATEGY_GOAL_WAIT:
	case SG_STRATEGY_GOAL_KIND_COUNT:
	default:
		break;
	}
	if (outcome != SG_STRATEGY_OUTCOME_NONE)
	{
		if (outcome == SG_STRATEGY_OUTCOME_COMPLETED)
			(void)SG_StrategyCallerSettle(&bot->strategy, 1U, outcome, failure,
				at_ms, &output);
		else
			(void)SG_StrategyCallerAdvance(&bot->strategy, 1U, outcome, failure,
				at_ms, &output);
	}
}

static qboolean StrategyCommitFrame(sg_bot_t *bot, sg_think_t *tc,
	sg_strike_duty_t strike_duty, sg_tactic_execution_t *execution_out,
	qboolean *navigation_permitted_out)
{
	sg_strategy_runtime_plan_request_t request;
	sg_rune_compact_portal_snapshot_frame_t portal_snapshot;
	const sg_compact_localized_state_t *localized_player;
	const sg_strategy_runtime_bot_observation_t *plan_observation;
	const sg_strategy_runtime_bot_observation_t *query_observation;
	sg_strategy_caller_plan_t plan;
	sg_strategy_caller_output_t output;
	sg_strategy_caller_output_proof_t output_proof;
	sg_strategy_runtime_caller_query_proof_t query_proof;
	sg_rune_compact_field_result_t field_result;
	sg_rune_compact_field_local_context_t local_context;
	sg_tactic_runtime_step_input_t step_input;
	const sg_rune_compact_model_t *model;
	uint64_t now_ms;

	if (execution_out != NULL)
		memset(execution_out, 0, sizeof(*execution_out));
	if (navigation_permitted_out != NULL)
		*navigation_permitted_out = false;
	if (!bot || !tc || !execution_out || !navigation_permitted_out)
		return false;
	memset(&plan, 0, sizeof(plan));
	if (!SG_StrategyRuntimeCompactProviderAvailable())
	{
		tc->strategy_plan_id = 0U;
		tc->strategy_activation_id = 0U;
		return false;
	}
	memset(&portal_snapshot, 0, sizeof(portal_snapshot));
	if (!(localized_player = SG_BotLocalizationCurrent(bot)) ||
	    !(plan_observation =
		SG_BotLocalizationStrategyObservationIssue(bot, localized_player)) ||
	    !StrategyFramePlanRequest(bot, tc, strike_duty, &request) ||
	    !SG_RuneCompactProductionFrameSnapshot(&sg_compact_production,
		localized_player->frame_sequence, &portal_snapshot))
		return false;
	now_ms = localized_player->localized_at_ms;
	request.localized_player = localized_player;
	request.mechanisms = portal_snapshot.mechanisms;
	request.portal_roots = portal_snapshot.portal_roots;
	request.bot_observation = plan_observation;
	if (!StrategyPopulateExecutionPoses(&request))
		return false;
	if (!SG_StrategyRuntimePlanResolve(&request, &plan))
		return false;
	if (!SG_StrategyCallerSubmit(&bot->strategy, &plan, 1U, now_ms,
		StrategyBlockReason(bot, tc), &output))
	{
		SG_StrategyCallerPlanDiscard(&plan);
		return false;
	}
	StrategyAdvanceLiveGoal(bot, tc, now_ms);
	if (!SG_StrategyCallerPulse(&bot->strategy, 1U, now_ms,
		StrategyBlockReason(bot, tc), &output))
		return false;
	query_observation =
		SG_BotLocalizationStrategyObservationIssue(bot, localized_player);
	if (!query_observation)
		return false;
	if (!SG_StrategyRuntimeQueryCallerOutputWithContext(&bot->strategy,
		&output, localized_player,
		portal_snapshot.mechanisms, portal_snapshot.portal_roots,
		query_observation, &field_result, &local_context, &output_proof,
		&query_proof))
		return false;
	model = SG_RuneCompactFieldServiceModel(output.field_service);
	if (field_result.kind == SG_RUNE_COMPACT_FIELD_STEP &&
		output.instruction.kind == SG_STRATEGY_INSTRUCTION_EXECUTE)
	{
		sg_tactic_runtime_prepared_step_t prepared;
		sg_tactic_runtime_status_t status;
		sg_tactic_live_inventory_t inventory;
		qboolean selected = false;

		memset(&step_input, 0, sizeof(step_input));
		step_input.model = model;
		step_input.strategy_caller = &bot->strategy;
		step_input.strategy_output = &output;
		step_input.strategy_proof = &output_proof;
		step_input.query_proof = &query_proof;
		step_input.localized = localized_player;
		step_input.local_context = &local_context;
		step_input.field_result = &field_result;
		SG_CombatLiveInventory(tc->e, &inventory);
		step_input.inventory = &inventory;
		/* Select this frame's capability from the exact probes, consume the
		 * strategy proof it was selected under, and hand the choice to the
		 * executor.  A frame with no selectable capability still steers at
		 * the step's target; the strategy lease is released either way. */
		memset(&prepared, 0, sizeof(prepared));
		status = SG_TacticRuntimePrepareStep(&step_input, &prepared);
		if (status != SG_TACTIC_RUNTIME_OK)
			(void)SG_StrategyRuntimeCallerQueryProofRelease(&bot->strategy,
				&output, &output_proof, &query_proof);
		else
		{
			status = SG_TacticRuntimePreparedStepConsume(&prepared);
			selected = status == SG_TACTIC_RUNTIME_OK &&
				SG_TacticExecutionDispatchSelected(model, &field_result,
					&prepared.result, execution_out);
			(void)SG_TacticRuntimePreparedStepRelease(&prepared);
		}
		if (!selected &&
			!SG_TacticExecutionDispatch(model, &field_result, execution_out))
			return false;
	}
	else
	{
		if (!SG_TacticExecutionDispatch(model, &field_result, execution_out))
		{
			(void)SG_StrategyRuntimeCallerQueryProofRelease(&bot->strategy,
				&output, &output_proof, &query_proof);
			return false;
		}
		if (!SG_StrategyRuntimeCallerQueryProofRelease(&bot->strategy,
			&output, &output_proof, &query_proof))
			return false;
	}
	tc->role = (sg_role_t)output.role;
	tc->strategy_plan_id = output.plan_id;
	tc->strategy_activation_id = output.activation_id;
	*navigation_permitted_out = output.instruction.kind ==
		SG_STRATEGY_INSTRUCTION_EXECUTE;
	return true;
}

static void StrategyInterrupt(sg_bot_t *bot, qboolean alive,
	sg_strategy_tactical_block_reason_t reason)
{
	const sg_compact_localized_state_t *localized;
	sg_strategy_caller_output_t output;
	uint64_t at_ms;

	localized = SG_BotLocalizationCurrent(bot);
	at_ms = localized ? localized->localized_at_ms : StrategyNowMs();
	if (!alive)
		(void)SG_StrategyCallerRetireCurrentLife(&bot->strategy,
			at_ms, &output);
	else
		(void)SG_StrategyCallerPulse(&bot->strategy, 1U, at_ms,
			reason, &output);
}
















/*
 * The lateral weave. Period per bot so a squad does not oscillate in phase and
 * present one wide target; the spread is 0.4 to 0.85 s, which is fast enough
 * that a 650 u/s rocket aimed where the bot was arrives where it is not, and
 * slow enough that ground friction is not eating the whole reversal. 300 is
 * pm_maxspeed's own wishspeed clamp (the strafe work above uses 400 pre-clamp
 * for direction only; here the magnitude is the point). All three fitted.
 */



/* Body actions do not outlive a death. Preserve learned map facts, danger,
 * persona, tilt, and dead-door lessons. */
static void Bot_ResetLifeActions(sg_bot_t *bot)
{
	int i;

	(void)SG_HookDiagnosticsFinish(&bot->hook_diagnostics, "death", "lifecycle");
	bot->hook_phase = 0;
	bot->hook_link = -1;
	SG_ChainHookGameReset(bot);
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
	bot->hook_proved_fling_release = false;
	bot->hook_proved_arrival_ms = 0;
	bot->hook_proved_settle_ms = 0;
	SG_HookLiveReset(&bot->hook_replay, &bot->hook_replay_active,
	    &bot->hook_replay_link, &bot->hook_final_guard);
	bot->hook_entity = NULL;
	bot->hook_legacy_settle = false;
	bot->hook_legacy_arrived = false;
	bot->flow_release = false;
	bot->speedhook = false;
	bot->speedhook_pull_applied = false;
	VectorClear(bot->hp_cur_dep);
	VectorClear(bot->hp_prev_dep);
	bot->hp_prev_land = 0.0f;

	memset(&bot->rocketjump, 0, sizeof(bot->rocketjump));
	SG_PushLiveReset(&bot->push);
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
	bot->commit_route_goal = (sg_field_key_t){ 0 };
	bot->commit_retirement_pending = false;
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
	bot->tac_strategy_activation = 0U;
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
		bot->visit_key[i] = (sg_field_key_t){ 0 };
		bot->visit_combat[i] = false;
		bot->visit_time[i] = 0.0f;
	}
	bot->visit_head = 0;
	bot->orbit_goal = (sg_field_key_t){ 0 };
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

/* Handle one dead-bot frame, including death learning and respawn input. */
static qboolean Think_Dead(sg_bot_t *bot, edict_t *e, usercmd_t *cmd,
	qboolean allow_command)
{
	if (!e->deadflag)
		return false;
	StrategyInterrupt(bot, false, SG_STRATEGY_BLOCK_CONTROLLER);

	if (!bot->death_taught)
	{
		if (!level.intermissiontime && SG_BotLocalizationCell(bot) >= 0)
		{
			Tilt_Note(e, bot);  /* the same death, remembered personally */
		}
			Bot_ResetLifeActions(bot);
			Combat_ResetClient(e);
			Caco_ResetClient(e);
			bot->death_taught = true;
	}
	SG_BotLocalizationReset(bot);
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
	/* Respawn consumes a fresh latched press, so pulse attack at 5 Hz. */
	cmd->buttons = (((int)(level.time * 10.0f)) & 2)
	              ? BUTTON_ATTACK : 0;
	if (allow_command)
		ClientThink(e, cmd);
	return true;
}

/* Start life-local movement and tilt clocks on the first live frame. */
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
	if (bot->commit_link >= 0 && bot->sticky_link == bot->commit_link)
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
	bot->commit_route_goal = (sg_field_key_t){ 0 };
	bot->commit_retirement_pending = false;
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
		SG_BotLocalizationInvalidate(bot);
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
	/* Retirement is one-way.  This paused lease exists only because current
	 * subjects were not yet proved clear; renew its protective TOP hold, but
	 * never resume forward activation for the superseded route. */
	if (bot->strike_weapon_draining || bot->commit_retirement_pending)
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

static short CompactTacticMove(float value)
{
	if (!isfinite(value))
		return 0;
	if (value > 400.0f)
		value = 400.0f;
	else if (value < -400.0f)
		value = -400.0f;
	return (short)lrintf(value);
}


/* The hook phase the executor sees, from the same facts the localization
 * reads: the live rope and what this bot last did with it. */
static sg_host_hook_phase_t CompactHookPhase(const sg_bot_t *bot,
	const edict_t *e)
{
	if (e->client->hookstate == 1 && e->client->hook)
		return SG_HOST_HOOK_IN_FLIGHT;
	if (e->client->hookstate == 2 && e->client->hook)
		return SG_HOST_HOOK_ATTACHED;
	if (bot->hook_phase == 3 && e->client->hookstate == 0)
		return SG_HOST_HOOK_COAST;
	return SG_HOST_HOOK_IDLE;
}

/* The body command boundary.  The executor turns the selected capability
 * and the live body into one command; this converts it to a usercmd, lets
 * combat own the view unless the capability needs it, runs the host's own
 * think, and issues hook and weapon requests through the entry points a
 * human's commands reach. */
static void CompactTacticEmit(sg_bot_t *bot, edict_t *e,
	const sg_tactic_execution_t *execution, qboolean navigation_permitted)
{
	usercmd_t cmd;
	sg_tactic_body_t body;
	sg_tactic_command_t command;
	qboolean engaged = false;
	qboolean moving;
	float navigation_yaw;
	float command_yaw;
	float command_radians;

	memset(&cmd, 0, sizeof(cmd));
	cmd.msec = 100;
	memset(&command, 0, sizeof(command));
	command.status = SG_TACTIC_COMMAND_HOLD;
	if (navigation_permitted)
	{
		memset(&body, 0, sizeof(body));
		VectorCopy(e->s.origin, body.origin);
		VectorCopy(e->velocity, body.velocity);
		body.supported = e->groundentity != NULL ? 1U : 0U;
		body.waterlevel = (uint8_t)e->waterlevel;
		body.crouched = (e->client->ps.pmove.pm_flags & PMF_DUCKED) != 0 ?
			1U : 0U;
		body.hook_phase = CompactHookPhase(bot, e);
		body.launcher_ready = SG_CombatRocketLauncherReady(e) ? 1U : 0U;
		body.hook_ready = SG_HookOffhandReady(e) ? 1U : 0U;
		body.gravity = sv_gravity->value;
		body.frame_ms = SG_HOST_ENGINE_FRAME_MS;
		body.substep_ms = SG_HOST_ENGINE_PMOVE_SUBSTEP_MS;
		if (!SG_TacticControl(execution, &body, &command))
		{
			memset(&command, 0, sizeof(command));
			command.status = SG_TACTIC_COMMAND_HOLD;
		}
	}
	moving = command.status != SG_TACTIC_COMMAND_HOLD &&
		command.speed > 0.0f && isfinite(command.direction[0]) &&
		isfinite(command.direction[1]);
	if (moving)
	{
		navigation_yaw = atan2f(command.direction[1], command.direction[0]) *
			180.0f / (float)M_PI;
		cmd.angles[YAW] = (short)(ANGLE2SHORT(navigation_yaw) -
			e->client->ps.pmove.delta_angles[YAW]);
	}
	if (command.aim_owned)
	{
		cmd.angles[YAW] = (short)(ANGLE2SHORT(command.yaw) -
			e->client->ps.pmove.delta_angles[YAW]);
		cmd.angles[PITCH] = (short)(ANGLE2SHORT(command.pitch) -
			e->client->ps.pmove.delta_angles[PITCH]);
	}
	else
		SG_CombatFrame(e, &cmd, &engaged);
	bot->engaged_last = engaged;
	if (moving)
	{
		command_yaw = (float)SHORT2ANGLE(((int)cmd.angles[YAW] +
			e->client->ps.pmove.delta_angles[YAW]) & 65535);
		command_radians = command_yaw * (float)M_PI / 180.0f;
		cmd.forwardmove = CompactTacticMove(400.0f * command.speed *
			(command.direction[0] * cosf(command_radians) +
			 command.direction[1] * sinf(command_radians)));
		cmd.sidemove = CompactTacticMove(400.0f * command.speed *
			(command.direction[0] * sinf(command_radians) -
			 command.direction[1] * cosf(command_radians)));
	}
	cmd.upmove = CompactTacticMove(400.0f * command.up);
	if (command.attack)
		cmd.buttons |= BUTTON_ATTACK;
	ClientThink(e, &cmd);
	if (command.want_launcher)
		SG_CombatRequestRocketLauncher(e);
	if (command.hook_fire && SG_HookOffhandReady(e))
	{
		Cmd_Hook_f(e);
		bot->hook_phase = 2;
		bot->hook_entity = e->client->hook;
	}
	else if (command.hook_release && e->client->hookstate != 0)
	{
		ctf_hook_abort(e);
		bot->hook_phase = 3;
		bot->hook_entity = NULL;
	}
	if (bot->hook_phase == 3 && e->groundentity && e->client->hookstate == 0)
		bot->hook_phase = 0;
}

static void CompactBotThink(sg_bot_t *bot, edict_t *e)
{
	sg_tactic_execution_t execution;
	sg_think_t tc;
	qboolean navigation_permitted = false;
	qboolean carrying;

	memset(&tc, 0, sizeof(tc));
	tc.e = e;
	tc.team = e->client->ctf.teamnum;
	carrying = SG_BotCarrying(e);
	tc.carrying = carrying;
	tc.role = CompactRoleForBot(bot, carrying);
	Caco_See(NULL, e);
	Think_RespawnEdge(bot, e);
	bot->death_taught = false;
	if (e->waterlevel == 0)
		bot->swim_air_seed = -1;
	if (!StrategyCommitFrame(bot, &tc, SG_STRIKE_DUTY_NONE, &execution,
		&navigation_permitted))
	{
		memset(&execution, 0, sizeof(execution));
		execution.kind = SG_TACTIC_EXECUTION_DISCONNECTED;
	}
	bot->last_role = (int)tc.role;
	bot->was_carrying = carrying;
	CompactTacticEmit(bot, e, &execution, navigation_permitted);
}

void SG_BotThink(sg_bot_t *bot)
{
	edict_t *e = bot->ent;
	const int *goal_field;
	sg_role_t role;
	int team, bestlink = -1;
	qboolean carrying;
	qboolean rune_compatible;
	qboolean compact_current;
	qboolean declared_door_guarded;
	const sg_strike_team_t *strike_team = NULL;
	const sg_strike_frame_t *strike_frame = NULL;
	sg_strike_duty_t strike_duty = SG_STRIKE_DUTY_NONE;
	int strike_slot = -1;

	qboolean	precision = false;          /* final approach: no tricks */
	qboolean	hold_post = false;          /* defender at its stand: guard */
	qboolean	rally_hold = false;         /* attacker waiting for a partner */
	qboolean	think_over;                 /* a stage ended the frame */
	float		post_yaw = 0.0f;            /* facing the likeliest approach */
	float		post_sight = -1.0f;         /* clear distance down that facing;
	                                         * WEAPONS.md 2.4-D3 picks the
	                                         * pre-held weapon from it */

	qboolean	duel = false;               /* combat has a live or fresh target */
	vec3_t		duel_org;                   /* where it is believed to be */
	float		duel_want = 0.0f;           /* range the weapon in hand wants */
	float		duel_expo = 0.0f;           /* what being seen costs, 0 to ~1 */

	sg_think_t	tc;

	memset(&tc, 0, sizeof(tc));
	SG_BotLocalizationFrameBegin(bot);
	tc.cmd.msec = 100;
	VectorClear(duel_org);
	if (SG_CompoundSwimGameOwns(bot))
	{
		StrategyInterrupt(bot, true, SG_STRATEGY_BLOCK_CONTROLLER);
		if (!e->deadflag)
			(void)SG_CompoundSwimGameEmit(bot,
			    (int)bot->compound_swim.snapshot.binding.link_index);
		return;
	}

	compact_current = SG_RuneCompactProductionCurrent(&sg_compact_production);
	rune_compatible = SG_RunePhysicsCompatible(sg_rune);
	declared_door_guarded = Bot_DeclaredDoorGuardAction(bot);
	if (!compact_current && !rune_compatible && !declared_door_guarded)
		SG_BotLocalizationInvalidate(bot);
	{
		sg_compound_guard_run_t run_state;

		run_state = SG_DeclaredDoorGuardRunState(bot);
		if (run_state == SG_COMPOUND_GUARD_RUN_TERMINAL)
		{
			StrategyInterrupt(bot, e->deadflag == DEAD_NO,
				SG_STRATEGY_BLOCK_CONTROLLER);
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
			StrategyInterrupt(bot, true, SG_STRATEGY_BLOCK_CONTROLLER);
			/* A later bot slot must not enter a newly claimed/released set.  Its
			 * already-linked hook is independent entity physics, so retire that
			 * projectile before suppressing the body command. */
			(void)SG_HookDiagnosticsFinish(&bot->hook_diagnostics,
			    "declared-door-interrupt", "guard-hold");
			if (e->client->hookstate || e->client->hook)
				ctf_hook_abort(e);
			bot->hook_phase = 0;
			bot->hook_link = -1;
			SG_ChainHookGameReset(bot);
			bot->hook_entity = NULL;
			bot->speedhook = false;
			bot->speedhook_pull_applied = false;
			bot->nade_phase = 0;
			SG_NadeTargetClear(bot);
			return;
		}
	}
	if (Think_Dead(bot, e, &tc.cmd, true))
		return;
	if (compact_current)
	{
		CompactBotThink(bot, e);
		return;
	}
	if (!rune_compatible)
	{
		StrategyInterrupt(bot, true, SG_STRATEGY_BLOCK_CONTROLLER);
		/* A runtime cvar change invalidates every stored ballistic witness.
		 * Leave the body in real physics, but submit no navigation and retire
		 * every action that could resume under a different law. */
		(void)SG_HookDiagnosticsFinish(&bot->hook_diagnostics,
		    "physics-incompatible", "cvar-hold");
		if (e->client->hookstate || e->client->hook)
			ctf_hook_abort(e);
		bot->hook_phase = 0;
		bot->hook_link = -1;
		SG_ChainHookGameReset(bot);
		bot->hook_bite_logged = false;
		bot->hook_attached_validated = false;
		bot->hook_landbrake = 0.0f;
		SG_HookLiveReset(&bot->hook_replay, &bot->hook_replay_active,
		    &bot->hook_replay_link, &bot->hook_final_guard);
		bot->hook_entity = NULL;
		bot->hook_legacy_settle = false;
		bot->hook_legacy_arrived = false;
		bot->speedhook = false;
		bot->speedhook_pull_applied = false;
		bot->flow_release = false;
		if (declared_door_guarded)
			bot->declared_door_recovery_since = 0.0f;
		if (declared_door_guarded &&
		    Bot_DeclaredDoorGuardRetainOrRelease(bot))
			return;
		if (declared_door_guarded)
			SG_BotLocalizationInvalidate(bot);
		memset(&bot->rocketjump, 0, sizeof(bot->rocketjump));
		SG_PushLiveReset(&bot->push);
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
				bot->visit_key[visit] = (sg_field_key_t){ 0 };
				bot->visit_combat[visit] = false;
				bot->visit_time[visit] = 0.0f;
			}
			bot->visit_head = 0;
			bot->orbit_goal = (sg_field_key_t){ 0 };
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
	    !SG_CompoundHookGameOwnsHostRope(bot) &&
	    (e->client->hookstate != 0 || e->client->hook != NULL))
	{
		StrategyInterrupt(bot, true, SG_STRATEGY_BLOCK_CONTROLLER);
		(void)SG_HookDiagnosticsFinish(&bot->hook_diagnostics,
		    "stale-host-rope", "cleanup");
		ctf_hook_abort(e);
		bot->hook_link = -1;
		bot->speedhook = false;
		bot->speedhook_pull_applied = false;
		bot->flow_release = false;
		declared_door_guarded = Bot_DeclaredDoorGuardAction(bot);
		if (declared_door_guarded &&
		    Bot_DeclaredDoorGuardRetainOrRelease(bot))
			return;
		if (declared_door_guarded)
			SG_BotLocalizationInvalidate(bot);
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
		memset(&bot->rocketjump, 0, sizeof(bot->rocketjump));
		SG_PushLiveReset(&bot->push);
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

	Caco_See(sg_rune, e);
	/* A proved rope owns the complete command before role/objective/approach
	 * stages can arm a grenade, hold, or other mission-side action. It also
	 * remains executable through airborne seed-coverage gaps. */
	if (bot->hook_link >= 0 && !bot->speedhook &&
	    (bot->hook_phase == 2 || bot->hook_phase == 3) &&
	    SG_HookActiveFrame(bot, e))
	{
		StrategyInterrupt(bot, true, SG_STRATEGY_BLOCK_HOOK_OPPORTUNITY);
		return;
	}

	team = e->client->ctf.teamnum;
	carrying = SG_BotCarrying(e);

	role = StrikeRoleForBot(bot, carrying);
	if (SG_NonCarryHandoffRetireSupersededRoute(bot, bot->last_role, (int)role) &&
	    bot->commit_retirement_pending && sg_rune && sg_rune->links &&
	    bot->commit_link >= 0 && bot->commit_link < sg_rune->hdr.num_links)
	{
		int action = sg_rune->links[bot->commit_link].action;

		if (SG_DeclaredDoorRouteRequiresRelease(bot, action))
		{
			if (Bot_DeclaredDoorGuardRetainOrRelease(bot))
				return;
			SG_StagedTraversalCancel(bot, action);
		}
	}
	if (!SG_RoleOwnsDefenseState(role))
	{
		/* A patrol is a role-local leg, not a mission that may sleep through
		 * ATTACK/RECOVER/ESCORT and resume from stale topology later. */
		(void)SG_DefensePatrolRetire(bot, false);
		if (bot->commit_link < 0)
			bot->commit_until = 0.0f;
		bot->patrol_until = 0.0f;
		bot->def_stand = false;
	}

	SG_CarryStartRetireSupersededRoute(bot, carrying && !bot->was_carrying);
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
	/* Shared class fields erase client admission and pickup magnitude.  Replace
	 * them with exact live client fields before optional-item pricing: weapons
	 * must improve the held tier, ammo must fit the held gun, and health/armor
	 * source costs retain the number of points the touch would actually add. */
	for (int item_class = 0; item_class < SG_FIELD_CLASSES; item_class++)
		tc.collectible_item_field[item_class] = sg_fields.item[item_class];
	tc.collectible_item_field[SG_FC_WEAPON] =
	    (tc.live.item[SG_FC_WEAPON] > 0.0f) ?
	    SG_CollectibleWeaponField(bot) : NULL;
	tc.collectible_item_field[SG_FC_HEALTH] =
	    (tc.live.item[SG_FC_HEALTH] > 0.0f) ?
	    SG_CollectibleHealthField(bot) : NULL;
	tc.collectible_item_field[SG_FC_AMMO] =
	    (tc.live.item[SG_FC_AMMO] > 0.0f) ?
	    SG_CollectibleAmmoField(bot) : NULL;
	tc.collectible_item_field[SG_FC_ARMOR] =
	    (tc.live.item[SG_FC_ARMOR] > 0.0f) ?
	    SG_CollectibleArmorField(bot) : NULL;

	tc.support = NULL;
	tc.intercept = NULL;
	Think_InterceptField(role, team, &tc.support, &tc.intercept);

	/* Objective's tactical waypoint search calls Surface_At, so every pricing
	 * input must exist before Objective—not be filled later by PickLink. */
	tc.health = e->health;
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
			tc.carrier_screened = SG_StrikeCarrierScreened(strike_team,
			    strike_frame);
			tc.strike_blocks_optional =
			    SG_StrikeDutyRetiresOptionalErrand(strike_duty);
			SG_StrikeDutyRetireSupersededRoute(bot, tc.strike_blocks_optional);
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
	Think_TacticalRoute(bot, &tc);

	goal_field = tc.goal_field;
	/* Objective published the prior route cost before the strike overlay may
	 * replace its route.  Publish the same live directed cost for downstream
	 * approach/terminal policy so a duty switch cannot retain stale pricing. */
	bot->last_goalcost = (SG_BotLocalizationCell(bot) >= 0 &&
	                      goal_field[SG_BotLocalizationCell(bot)] < SG_FIELD_INF)
	                     ? goal_field[SG_BotLocalizationCell(bot)] : -1;

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

	if (SG_BotLocalizationCell(bot) < 0 ||
	    goal_field[SG_BotLocalizationCell(bot)] >= SG_FIELD_INF)
	{
		StrategyInterrupt(bot, true, SG_STRATEGY_BLOCK_CONTROLLER);
		memset(&tc.cmd, 0, sizeof(tc.cmd));
		tc.cmd.msec = 100;
		ClientThink(e, &tc.cmd);
		return;
	}

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
	precision = (goal_field[SG_BotLocalizationCell(bot)] < 1500);

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
	/* Recover a transition missed by the synchronous SpawnEntities/ReadLevel
	 * hooks.  TAG_LEVEL is already gone on this path, so every map pointer and
	 * owner callback is stale when the time or map-name sentinel trips. */
	if (SG_TimerPending(sg_last_frame_time) ||
	    ((sg_rune || sg_compact_production.active != 0U) &&
		strcmp(sg_rune_map, level.mapname) != 0))
	{
		/* This detector is a recovery path after the host has already retired
		 * TAG_LEVEL.  Clear callback entrypoints and dangling plan leases without
		 * calling back into the lost owner; the synchronous SpawnEntities and
		 * ReadLevel paths perform the normal exact release. */
		for (i = 0; i < SG_MAXBOTS; i++)
			SG_StrategyCallerOwnerLost(&sg_bots[i].strategy);
		SG_LevelChange();
	}
	/* Host movement is not consumed until its exact engine binding has been
	 * installed and revalidated for this frame.  The owner deliberately
	 * returns HOST_UNAVAILABLE on ordinary production builds that have no BSP
	 * bridge yet; that is the fail-closed state, not a permission to fall back
	 * to a caller callback or a hull probe.  Retry this idempotent transaction
	 * every frame: the bridge may appear after startup, and a prior consumer may
	 * have retired a drifted owner after the one-shot artifact load. */
	(void)SG_HostLawProductionEnsureLevel(level.mapname);
	if (!sg_autoload_attempted)
	{
		sg_autoload_attempted = true;
		(void)SG_LevelSetup();
	}
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
	/* Compact-only levels have no legacy seed table, but their sparse beliefs
	 * still age each frame against the accepted compact provider. */
	else if (SG_CacoCompactBeliefActive())
		Caco_Frame(NULL);
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
		SG_BotLocalizationFrameEnd(&sg_bots[i]);
	}
}

/* ---------------------------------------------------------------- spawn */


void SG_CompactProductionStorageWillFree(void)
{
	int i;

	/* Plans own field-service leases.  Retire all plans first, then revoke the
	 * compact providers and finally destroy their borrowed decoded model. */
	for (i = 0; i < SG_MAXBOTS; i++)
		SG_StrategyCallerDestroy(&sg_bots[i].strategy);
	SG_RuneCompactProductionClear(&sg_compact_production);
}

void SG_LevelChange(void)
{
	int i;

	/* Map teardown is a terminal owner in its own right. Finish before the
	 * roster removal so the original map snapshot remains attached; slot reset
	 * then sees a closed state and is intentionally idempotent. */
	for (i = 0; i < SG_MAXBOTS; i++)
		(void)SG_HookDiagnosticsFinish(&sg_bots[i].hook_diagnostics,
		    "map-transition", "level-change");
	/* The later full slot reset remains behind the compound-guard level fence
	 * and sees an already empty, idempotent strategy caller. */
	SG_CompactProductionStorageWillFree();
	SG_RuneSourceAuthorityReset();
	SG_HostLawProductionReset();
	SG_ButtonExecutionLevelReset();
	SG_TimedVaultEgressScopeEnd();
	(void)SG_CompoundGuardGameLevelReset();
	SG_LevelIdentityReset();
	/* SpawnEntities calls this before TAG_LEVEL/edict teardown. Remove fake
	 * clients through the real disconnect path while their objective state is
	 * still valid; otherwise the next map inherits invisible client slots. */
	SG_RemoveBots();
	/* Exact armor prerequisite fields live in persistent bot scratch arrays.
	 * Their topology epoch is level-local, so retire every entry after the last
	 * bot callback and before TAG_LEVEL teardown can recycle the old rune. */
	SG_CollectibleArmorTargetLevelReset();
	/* SpawnEntities resets level.time after this synchronous hook. Zero keeps
	 * the first new-map frame from interpreting that reset as an unhandled
	 * second transition and retiring a bot added by a startup/rcon command. */
	sg_last_frame_time = 0.0f;

	/* rune and fields were TAG_LEVEL -- the engine freed them */
	sg_rune = NULL;
	sg_setup_failed = false;
	sg_autoload_attempted = false;
	sg_physics_warned = false;
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
