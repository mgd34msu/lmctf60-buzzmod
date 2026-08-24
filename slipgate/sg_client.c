/*
 * sg_client.c -- botfill, roster commands, the per-frame driver, and
 * level-change bookkeeping.
 */
#include "g_local.h"
#include "g_ctffunc.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_chat.h"
#include "slipgate/sg_persona.h"
#include "slipgate/sg_net.h"
#include "slipgate/sg_cvars.h"
#include "slipgate/sg_combat.h"
#include "slipgate/sg_util.h"
#include "slipgate/sg_bot.h"
#include "slipgate/sg_drop_live.h"
#include "slipgate/sg_hook_live.h"
#include "slipgate/sg_hook_game.h"
#include "slipgate/sg_compound_guard_game.h"
#include "slipgate/sg_compound_swim_game.h"
#include "slipgate/sg_swim_live.h"
#include "slipgate/sg_weights.h"    /* sg_role_names -- the roster print */
#include "slipgate/sg_goal.h"
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_move.h"
#include "slipgate/sg_lead.h"
#include "slipgate/sg_pov_identity.h"
#include "slipgate/sg_defense_shift.h"
#include "slipgate/sg_bot_ping.h"
#include "slipgate/sg_ribbon_random.h"
#include "slipgate/sg_lead_random.h"
#include "slipgate/sg_persona_assignment.h"
#include "slipgate/sg_escape_random.h"
#include "slipgate/sg_role_policy.h"

void		ClientDisconnect(edict_t *ent);
qboolean	ClientConnect(edict_t *ent, char *userinfo);
void		ClientBegin(edict_t *ent);
void		ClientUserinfoChanged(edict_t *ent, char *userinfo);

sg_bot_t sg_bots[SG_MAXBOTS];

/*
 * A slot is process storage, not level storage.  Reusing it therefore has to
 * be an initialization event in its own right: ClientConnect/ClientBegin
 * replace the engine client, but they know nothing about SLIPGATE's sidecar.
 * Keep every zero-sentinel at zero and spell out the indices for which zero
 * is a real seed/link/client and -1 is the only honest "none" value.
 */
static void BotSlot_Reset(sg_bot_t *bot)
{
	int i;
	int slot = bot ? (int)(bot - sg_bots) : -1;
	unsigned long long instance_token = bot ? bot->instance_token : 0ULL;

	/* Defensive for non-ClientDisconnect retirement paths: no delayed target
	 * chain may outlive the SG owner identity stored in this process slot. */
	if (bot)
		(void)SG_HookDiagnosticsFinish(&bot->hook_diagnostics,
		    "slot-retirement", "lifecycle");
	if (bot && bot->active && bot->ent)
		SG_CancelBotDelayedUses(bot->ent);
	if (slot >= 0 && slot < SG_MAXBOTS && instance_token != 0ULL)
		POVLock_SGInstanceRetired(slot, instance_token);
	if (slot >= 0 && slot < SG_MAXBOTS)
		SG_StrikeSlotReset(slot);
	(void)SG_CompoundGuardGameBotSlotReset(&bot->compound_guard);
	SG_CompoundSwimGameReset(bot);
	SG_TrainStationGameReset(bot);
	SG_ButtonExecutionActionReset(bot);
	/* Spell out the grenade identity retirement before raw storage reuse:
	 * a recycled bot slot must never inherit a client-life binding. */
	SG_NadeTargetClear(bot);
	/* This is the only production reset point for the immutable instance. */
	SG_BotPOVInstanceReset(bot);
	memset(bot, 0, sizeof(*bot));
	SG_CompoundHookGameReset(bot);
	bot->compound_drop_live.drop_link = -1;
	bot->seed = -1;
	bot->hook_link = -1;
	SG_ChainHookGameReset(bot);
	SG_HookLiveReset(&bot->hook_replay, &bot->hook_replay_active,
	    &bot->hook_replay_link, &bot->hook_final_guard);
	SG_HookDiagnosticsReset(&bot->hook_diagnostics);
	bot->hook_entity = NULL;
	bot->hook_legacy_settle = false;
	bot->hook_legacy_arrived = false;
	for (i = 0; i < SG_BL_MAX; i++)
		bot->bl_link[i] = -1;
	bot->last_role = -1;
	for (i = 0; i < SG_VISIT_RING; i++)
		bot->visit_seed[i] = -1;
	bot->orbit_last_seed = -1;
	bot->watch_link = -1;
	bot->jump_link = -1;
	bot->drop_link = -1;
	bot->drop_airborne = false;
	bot->drop_recover = false;
	SG_DropLiveReset(&bot->drop_replay, &bot->drop_replay_active,
	    &bot->drop_replay_link, &bot->drop_live_events);
	SG_SwimLiveReset(&bot->swim_replay, &bot->swim_replay_active,
	    &bot->swim_replay_link, &bot->swim_validated,
	    &bot->swim_proved_ms, &bot->swim_elapsed_ms);
	bot->swim_air_seed = -1;
	bot->declared_start_frame = -1;
	bot->declared_touch_frame = -1;
	bot->declared_trigger_frame = -1;
	bot->declared_egress_proof_frame = -1;
	bot->declared_door_retreat = false;
	bot->declared_door_suffix_ms = 0;
	bot->commit_link = -1;
	bot->strike_weapon_link = -1;
	bot->strike_weapon_until = 0.0f;
	bot->strike_weapon_draining = false;
	SG_StrikeWeaponTargetClear(bot);
	bot->last_goalcost = -1;
	bot->sticky_link = -1;
	bot->carry_startcost = -1;
	bot->carry_bestcost = -1;
	bot->last_role_for_legs = -1;
	bot->ribbon_link = -1;
	bot->lead_slot = -1;
	bot->lead_seed = -1;
	bot->lead_state = SG_LEAD_WAITING;
	bot->lead_seen_up_at = -1.0f;
	bot->lead_inferred_until = 0.0f;
	bot->patrol_seed = -1;
	bot->patrol_link = -1;
	bot->def_shift_seed = -1;
	bot->def_shift_link = -1;
	bot->def_shift_from = -1;
	bot->def_supply_armed = false;
	bot->def_supply_phase = SG_DEF_SUPPLY_NONE;
	bot->def_supply_instance = 0ULL;
	bot->def_supply_ent = -1;
	bot->def_supply_target_seed = -1;
	bot->def_supply_route_ms = 0;
	VectorClear(bot->def_supply_target_org);
	bot->def_supply_until = 0.0f;
	bot->def_supply_next = 0.0f;
	bot->tac_seed = -1;
	bot->tac_role = -1;
	bot->rally_cover = -1;
	bot->rail_link = -1;
	bot->railhold_enemy = -1;
	bot->door_hold_link = -1;
	bot->escprior_bucket = -1;
	bot->prev_seed = -1;
	bot->tilt_seed = -1;
	bot->tilt_killer_seed = -1;
	bot->tilt_death_time = -1000.0f;
}

/*
 * Sixteen names, because `slot & 7` on a ten-bot 5v5 fielded TWO Arachs
 * and TWO Cacos in every game since the format began -- and every
 * per-name analysis quietly merged two different bots (the it18 "role
 * flap" was two same-named clients interleaving in the telemetry, and
 * the ghost was hunted with a printf). Names are identity; identity is
 * data.
 */
static const char *sg_names[] = {
	"Arach", "Caco", "Rune", "Slip", "Gate", "Phase", "Field", "Trace",
	"Vore", "Fiend", "Scrag", "Ogre", "Knight", "Wizard", "Spawn", "Shal",
};

/* Scoped across SG_AddBotTeam's synchronous ClientConnect call only. Real
 * engine connections, including a human replacing an SG slot, must still
 * satisfy the server password; a server-owned fake client must not persist
 * that secret in userinfo merely to pass through the common lifecycle. */
static edict_t *sg_internal_connect_ent;

qboolean SG_InternalClientConnect(edict_t *ent)
{
	return ent && ent == sg_internal_connect_ent;
}

/* Botfill's cadence and hysteresis are level-time state. */
static float sg_botfill_next_check;
static int sg_botfill_over_streak[2];
static int sg_botfill_under_streak[2];

void Botfill_Reset(void)
{
	sg_botfill_next_check = 0.0f;
	memset(sg_botfill_over_streak, 0, sizeof(sg_botfill_over_streak));
	memset(sg_botfill_under_streak, 0, sizeof(sg_botfill_under_streak));
}

/*
 * BOTFILL -- sv_botfill names the players each team should field. Bots fill
 * vacancies and yield surplus slots one at a time. Zero disables balancing.
 */
static int Botfill_WorstIndex(int team, qboolean automatic)
{
	int i, worst = -1, worst_score = 0x7fffffff;

	for (i = 0; i < SG_MAXBOTS; i++)
	{
		if (!sg_bots[i].active || !sg_bots[i].ent || !sg_bots[i].ent->inuse)
			continue;
		if (team && sg_bots[i].ent->client->ctf.teamnum != team)
			continue;
		if (!SG_BotfillRemovalAllowed(automatic,
		    ClientHasFlag(sg_bots[i].ent) != NULL))
			continue;
		if (sg_bots[i].ent->client->resp.score < worst_score)
		{
			worst_score = sg_bots[i].ent->client->resp.score;
			worst = i;
		}
	}
	return worst;
}

#ifdef SG_BOTFILL_TEST_API
int SG_BotfillTestWorstIndex(int team, int automatic)
{
	return Botfill_WorstIndex(team, (qboolean)automatic);
}
#endif

/* the teardown half, shared by every path that retires a bot: disconnect,
 * free the edict, and clear the slot in that order -- the slot must not be
 * reusable until the edict is actually gone */
static void Botfill_Drop(int slot)
{
	SG_ChatResetClient(sg_bots[slot].ent);
	Caco_ResetClient(sg_bots[slot].ent);
	ClientDisconnect(sg_bots[slot].ent);
	SG_FreeClientEdict(sg_bots[slot].ent);
	BotSlot_Reset(&sg_bots[slot]);
}

static qboolean Botfill_RemoveOne(int team)
{
	int worst = Botfill_WorstIndex(team, true);

	if (worst < 0)
		return false;
	sg_host.bprint(PRINT_HIGH, "%s yields its slot.\n",
	           sg_bots[worst].ent->client->pers.netname);
	Botfill_Drop(worst);
	return true;
}

void Botfill_Frame(void)
{
	cvar_t *fill = sg_host.cvar("sv_botfill", "0", 0);
	int want[2];
	int humans[2] = {0, 0}, bots[2] = {0, 0};
	int i, t;

	/* one value fills both teams to it; "5:1" sets red and blue
	 * separately -- colon, not space, because the engine's set command
	 * reads a third token as a serverinfo flag and sets NOTHING. A 5v1
	 * control game exposed this by running with zero bots. */
	if (sscanf(fill->string, "%d:%d", &want[0], &want[1]) < 2)
		want[1] = want[0] = (int)fill->value;

	if ((want[0] <= 0 && want[1] <= 0) ||
	    SG_TimerPending(sg_botfill_next_check))
		return;
	SG_TimerArm(&sg_botfill_next_check, 1.0f);

	for (i = 0; i < game.maxclients; i++)
	{
		edict_t *e = g_edicts + 1 + i;

		if (!e->inuse || !e->client)
			continue;
		t = e->client->ctf.teamnum;
		if (t != CTF_TEAM_RED && t != CTF_TEAM_BLUE)
			continue;
		if (e->flags & FL_BOT)
			bots[SG_TeamIdx(t)]++;
		else
			humans[SG_TeamIdx(t)]++;
	}

	/* Three consecutive samples damp transient joins and leaves. A protected
	 * overfill remains ready so removal can retry after the carry ends. */
	{
		qboolean acted = false;

		for (t = 0; t < 2 && !acted; t++)
		{
			if (humans[t] + bots[t] > want[t] && bots[t] > 0)
			{
				sg_botfill_over_streak[t] = SG_BotfillOverfillStreak(
				    sg_botfill_over_streak[t], false);
				if (sg_botfill_over_streak[t] >= 3 &&
				    Botfill_RemoveOne(SG_TeamFromIdx(t)))
				{
					sg_botfill_over_streak[t] = SG_BotfillOverfillStreak(
					    sg_botfill_over_streak[t], true);
					acted = true;
				}
			}
			else
				sg_botfill_over_streak[t] = 0;
		}
		for (t = 0; t < 2; t++)
		{
			if (acted)
				break;
			if (humans[t] + bots[t] < want[t])
			{
				/* an EMPTY server fills instantly -- the hysteresis
				 * exists to damp roster churn in live games, and a
				 * fresh map after rotation has no roster to damp. In four
				 * observed cases, a human entering an empty arena waited
				 * 45 seconds. */
				if (bots[0] + bots[1] == 0 ||
				    ++sg_botfill_under_streak[t] >= 3)
				{
					SG_AddBotTeam(SG_TeamFromIdx(t));
					sg_botfill_under_streak[t] = 0;
					acted = true;
				}
			}
			else
				sg_botfill_under_streak[t] = 0;
		}
	}
}

/* The engine allocates real clients without consulting edict->inuse. If it
 * selects a fake-client slot, retire that exact SG owner before the incoming
 * ClientConnect initializes the human. ClientDisconnect is essential here:
 * it drops a carried flag/rune and frees a live hook instead of transferring
 * those objects to the next occupant. The edict itself remains the engine's
 * selected slot and is immediately reusable. */
qboolean SG_RetireBotForClient(edict_t *ent)
{
	int i;

	for (i = 0; i < SG_MAXBOTS; i++)
	{
		if (!sg_bots[i].active || sg_bots[i].ent != ent)
			continue;
		SG_ChatResetClient(ent);
		Caco_ResetClient(ent);
		Combat_ResetClient(ent);
		if (ent->client && ent->inuse)
			ClientDisconnect(ent);
		SG_FreeClientEdict(ent);
		BotSlot_Reset(&sg_bots[i]);
		return true;
	}
	return false;
}

/* Defensive half of the ownership boundary. If an external lifecycle path
 * has already replaced/cleared FL_BOT, forget only SG's slot; never disconnect
 * the now-human occupant while trying to repair stale bookkeeping. */
void SG_DisownBot(edict_t *ent)
{
	int i;

	for (i = 0; i < SG_MAXBOTS; i++)
		if (sg_bots[i].active && sg_bots[i].ent == ent)
		{
			BotSlot_Reset(&sg_bots[i]);
			return;
		}
}

qboolean SG_AddBot(void)
{
	return SG_AddBotTeam(0);
}


static uint32_t SG_OccupiedPersonaNames(void)
{
	uint32_t occupied = 0;
	int i, row;

	for (i = 1; i <= game.maxclients; i++)
	{
		edict_t *e = &g_edicts[i];

		if (!e->inuse || !e->client)
			continue;
		for (row = 0; row < 16; row++)
		{
			char candidate[32];

			Com_sprintf(candidate, sizeof(candidate), "[SG]%s", sg_names[row]);
			if (!Q_stricmp(e->client->pers.netname, candidate))
			{
				occupied |= UINT32_C(1) << row;
				break;
			}
		}
	}
	return occupied;
}

qboolean SG_AddBotTeam(int teamnum)
{
	edict_t *ent;
	char userinfo[MAX_INFO_STRING];
	char name[32];
	int i, slot = -1;
	unsigned persona_slot;

	if (!SG_LevelSetup())
		return false;

	for (i = 0; i < SG_MAXBOTS; i++)
		if (!sg_bots[i].active)
		{
			slot = i;
			break;
		}
	if (slot < 0)
		return false;
	/* Do this before the engine lifecycle.  A post-ClientBegin memset would
	 * erase any initialization a present or future begin hook writes here. */
	BotSlot_Reset(&sg_bots[slot]);

	memset(userinfo, 0, sizeof(userinfo));
	/* Tag first: "[SG]Arach", not "Arach[SG]". Names already worn by a
	 * human or an earlier bot are occupied: skipping a human must not give two
	 * later bots the same identity. If all sixteen rows are occupied, keep the
	 * preferred row rather than refusing a needed team body. */
	persona_slot = SG_PersonaAssignmentChoose(SG_OccupiedPersonaNames(),
	    (unsigned)slot);
	Com_sprintf(name, sizeof(name), "[SG]%s", sg_names[persona_slot]);
	Info_SetValueForKey(userinfo, "name", name);
	/* a CTF-conforming request from the start; the team letter is corrected
	 * in the second userinfo pass once the team is known, and servers
	 * without a skin list get a parseable rb-set name instead of grunt */
	Info_SetValueForKey(userinfo, "skin", va("male/rb-rm%d", 1 + (slot % 6)));
	Info_SetValueForKey(userinfo, "hand", "0");

	ent = SG_SpawnClientEdict();
	if (!ent)
		return false;
	/* SG_SpawnClientEdict deliberately reuses gclient storage.  CTF state is
	 * not part of a new fake client's identity: retaining an observer value
	 * here can create an active team -1/-2/-3 bot and feed that through the
	 * raw team-1 array index used throughout SG.  Persistent career stats live
	 * outside this sub-structure and remain intact. */
	memset(&ent->client->ctf, 0, sizeof(ent->client->ctf));
	/* Reset recyclable sidecars before either client lifecycle hook runs. */
	Combat_ResetClient(ent);
	Caco_ResetClient(ent);
	SG_ChatResetClient(ent);
	ent->flags &= ~FL_BOT;
	ent->inuse = false;
	sg_internal_connect_ent = ent;
	if (!ClientConnect(ent, userinfo))
	{
		sg_internal_connect_ent = NULL;
		SG_FreeClientEdict(ent);
		return false;
	}
	sg_internal_connect_ent = NULL;
	/* Same pattern as BotCTFAssignTeam: written while inuse is still
	 * false, so ClientBegin sees a client already on a team and keeps
	 * it, penalty-free -- the asymmetric fills (5v1) need bots landing
	 * where the census says, not where the balancer would put them. */
	if (teamnum == CTF_TEAM_RED || teamnum == CTF_TEAM_BLUE)
	{
		ent->client->ctf.teamnum = teamnum;
		if (ent->client->p_stats_player)
			ent->client->p_stats_player->info.teamnum = teamnum;
	}
	else
	{
		/* A bare add asks the live balancer, not the dropped-player record for
		 * this recycled bot name. Preserve career totals but clear its old team
		 * before ClientBegin decides whether TeamJoin is required. */
		ent->client->ctf.teamnum = CTF_TEAM_UNDEFINED;
		if (ent->client->p_stats_player)
			ent->client->p_stats_player->info.teamnum = CTF_TEAM_UNDEFINED;
	}
	ent->inuse = true;
	ent->flags |= FL_BOT;
	ClientUserinfoChanged(ent, userinfo);
	ClientBegin(ent);
	/* TeamJoin normally assigns a bare add and an explicit fill was assigned
	 * above.  Treat any other result as a lifecycle failure before the edict
	 * becomes SG-owned; no team-indexed bot code may see it. */
	if (ent->client->ctf.teamnum != CTF_TEAM_RED &&
	    ent->client->ctf.teamnum != CTF_TEAM_BLUE)
	{
		ClientDisconnect(ent);
		SG_FreeClientEdict(ent);
		BotSlot_Reset(&sg_bots[slot]);
		return false;
	}
	/*
	 * Again, now that a TEAM exists. The skin force inside the first
	 * userinfo pass ran while teamnum was still UNDEFINED, its red/blue
	 * ternary defaulted to blue, and every bot the balancer then sent
	 * red played the whole match in the wrong colors, visible on the
	 * scoreboard portraits. The second pass sees
	 * the real team and paints the uniform right. The REQUEST is also
	 * re-lettered here because CTF bots wear their team's CTF
	 * skin) so even the no-skin-list fallback path parses to the right
	 * color.
	 */
	if (ent->client->ctf.teamnum == CTF_TEAM_RED ||
	    ent->client->ctf.teamnum == CTF_TEAM_BLUE)
	{
		Info_SetValueForKey(ent->client->pers.userinfo, "skin",
		    va("male/rb-%cm%d",
		       ent->client->ctf.teamnum == CTF_TEAM_RED ? 'r' : 'b',
		       1 + (slot % 6)));
	}
	ClientUserinfoChanged(ent, ent->client->pers.userinfo);


	ent->client->ctf.extra_flags |=
	    (CTF_EXTRAFLAGS_RADIO_TEXT | CTF_EXTRAFLAGS_RADIO_SOUND);

	/* ClientBegin has completed PutClientInServer and assigned the bot's first
	 * ctfid. Publish one immutable SG instance before ownership becomes live. */
	sg_bots[slot].ent = ent;
	if (!SG_BotPOVInstanceAssign(&sg_bots[slot]))
	{
		ClientDisconnect(ent);
		SG_FreeClientEdict(ent);
		BotSlot_Reset(&sg_bots[slot]);
		return false;
	}
	sg_bots[slot].patrol_random = SG_DefensePatrolRandomInitial(
	    sg_bots[slot].instance_token, (unsigned)(ent - g_edicts - 1));
	sg_bots[slot].ribbon_random = SG_RibbonRandomInitial(
	    sg_bots[slot].instance_token, (unsigned)(ent - g_edicts - 1));
	sg_bots[slot].lead_random = SG_LeadRandomInitial(
	    sg_bots[slot].instance_token, (unsigned)(ent - g_edicts - 1));
	sg_bots[slot].escape_random = SG_EscapeRandomInitial(
	    sg_bots[slot].instance_token, (unsigned)(ent - g_edicts - 1));
	sg_bots[slot].active = true;
	sg_bots[slot].fake_ping = SG_BotPingBase(sg_bots[slot].instance_token,
	    (unsigned)(ent - g_edicts - 1));
	(void)SG_CompoundGuardGameBotAttach(&sg_bots[slot].compound_guard,
	    slot, ent);
	/* A fresh late join has no respawn edge from which to seed the movement
	 * watchdogs. Initialize every progress sample at the actual spawn now;
	 * otherwise level.time can make zero-initialized clocks immediately ancient
	 * and a spawn near world origin look wedged on its first live frame. */
	VectorCopy(ent->s.origin, sg_bots[slot].stuck_origin);
	VectorCopy(ent->s.origin, sg_bots[slot].last_origin);
	VectorCopy(ent->s.origin, sg_bots[slot].watch_org);
	VectorCopy(ent->s.origin, sg_bots[slot].stag_org);
	VectorCopy(ent->s.origin, sg_bots[slot].wedge_org);
	SG_Mark(&sg_bots[slot].watch_since);
	SG_Mark(&sg_bots[slot].stag_since);
	SG_Mark(&sg_bots[slot].wedge_since);
	SG_PersonaBind(ent, (int)persona_slot); /* displayed identity owns behavior */

	/*
	 * The persona in the join print, because the first question asked of a
	 * behaviour difference is always "which one is that" and the second is
	 * "was it cast that way or is it broken". A NULL persona (sg_persona 0)
	 * prints the old line unchanged.
	 */
	{
		const char *pname = SG_PersonaName(ent);

		if (pname)
			sg_host.dprint("slipgate: %s entered (persona %s)\n",
			           Info_ValueForKey(userinfo, "name"), pname);
		else
			sg_host.dprint("slipgate: %s entered\n",
			           Info_ValueForKey(userinfo, "name"));
	}
	return true;
}

int SG_RemoveBots(void)
{
	int i, n = 0;
	for (i = 0; i < SG_MAXBOTS; i++)
	{
		edict_t *ent;

		if (!sg_bots[i].active)
			continue;
		ent = sg_bots[i].ent;
		/* An engine kick can park the fake client before this bulk reset. The
		 * ownership slot is still ours, so finish clearing FL_BOT/CTF state even
		 * though ClientDisconnect must not be called twice. A live replacement
		 * that has already lost FL_BOT belongs to the engine and is left alone. */
		if (ent && ent->client && (ent->flags & FL_BOT))
		{
			if (ent->inuse)
				ClientDisconnect(ent);
			SG_FreeClientEdict(ent);
		}
		BotSlot_Reset(&sg_bots[i]);
		n++;
	}
	return n;
}

/* TAG_GAME owns both edicts and clients.  Retire live fake clients through
 * their real lifecycle, then erase every process-storage roster slot before
 * either backing array can disappear. */
void SG_RosterStorageReset(void)
{
	int i;

	(void)SG_RemoveBots();
	for (i = 0; i < SG_MAXBOTS; i++)
		BotSlot_Reset(&sg_bots[i]);
}

/*
 * The roster as the admin sees it. Every column here is something that has
 * had to be dug out of a debug print at least once: the slot (the removal
 * verbs take it), the effective skill -- bot_skill LESS this client's own
 * fixed handicap, so two bots on one server genuinely differ and a "bad"
 * bot is usually just a low-offset one -- the role being played right now,
 * and the seed the bot believes it is at, which is the first question
 * anybody asks when one looks stuck.
 */
void SG_ListBots(void)
{
	int i, n = 0;

	for (i = 0; i < SG_MAXBOTS; i++)
	{
		edict_t *e = sg_bots[i].ent;
		int role, team;

		if (!sg_bots[i].active || !e || !e->inuse || !e->client)
			continue;
		if (!n)
			sg_host.cprint(NULL, PRINT_HIGH,
			           "slot name                 team  score skill role     seed\n");
		team = e->client->ctf.teamnum;
		role = sg_bots[i].last_role;
		sg_host.cprint(NULL, PRINT_HIGH, "%3d  %-20s %-5s %5d %5.2f %-8s %4d\n",
		           i, e->client->pers.netname,
		           team == CTF_TEAM_RED ? "red" :
		           team == CTF_TEAM_BLUE ? "blue" : "-",
		           e->client->resp.score,
		           (float)SG_CombatSkill(e) / 100.0f,
		           (role >= 0 && role < SG_ROLES) ? sg_role_names[role] : "-",
		           sg_bots[i].seed);
		n++;
	}
	if (!n)
		sg_host.cprint(NULL, PRINT_HIGH, "slipgate: no bots\n");
	else
		sg_host.cprint(NULL, PRINT_HIGH, "slipgate: %d bot%s\n",
		           n, n == 1 ? "" : "s");
}

/*
 * Name matching built for the admin, not for the parser. The scoreboard
 * shows "[SG]Arach"; whoever is typing under pressure writes "arach".
 * Accept the slot number `sv sg list` just printed, the netname as shown,
 * or the netname with our own [SG] tag stripped -- that tag is decoration
 * this code puts on, so it is not something a human should have to
 * reproduce to name the thing they are looking at.
 */
qboolean SG_RemoveBotNamed(const char *who)
{
	int i, slot = -1;

	if (!who || !*who)
		return false;

	if (who[0] >= '0' && who[0] <= '9')
	{
		slot = atoi(who);
		if (slot < 0 || slot >= SG_MAXBOTS)
			return false;
	}
	else
	{
		for (i = 0; i < SG_MAXBOTS; i++)
		{
			const char *nm;

			if (!sg_bots[i].active || !sg_bots[i].ent ||
			    !sg_bots[i].ent->inuse || !sg_bots[i].ent->client)
				continue;
			nm = sg_bots[i].ent->client->pers.netname;
			if (Q_stricmp(nm, who) == 0)
			{
				slot = i;
				break;
			}
			/* the tag is emitted verbatim as "[SG]", so an exact compare
			 * finds it; only what follows it needs the loose match */
			if (!strncmp(nm, "[SG]", 4) && Q_stricmp(nm + 4, who) == 0)
			{
				slot = i;
				break;
			}
		}
	}

	if (slot < 0 || !sg_bots[slot].active || !sg_bots[slot].ent ||
	    !sg_bots[slot].ent->inuse)
		return false;

	sg_host.bprint(PRINT_HIGH, "%s was removed.\n",
	           sg_bots[slot].ent->client->pers.netname);
	Botfill_Drop(slot);
	return true;
}

qboolean SG_KickWorst(void)
{
	int worst = Botfill_WorstIndex(0, false);   /* 0: either team */

	if (worst < 0)
		return false;
	sg_host.bprint(PRINT_HIGH, "%s was cut, lowest score.\n",
	           sg_bots[worst].ent->client->pers.netname);
	Botfill_Drop(worst);
	return true;
}
