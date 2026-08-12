/*
 * sg_client.c -- the roster keeps itself: botfill, add/remove/list,
 * the per-frame driver, and level-change bookkeeping.  Moved verbatim
 * from the tail of sg_arach.c in the 2026-08-11 standards pass.
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
#include "slipgate/sg_weights.h"    /* sg_role_names -- the roster print */

void		ClientDisconnect(edict_t *ent);
qboolean	ClientConnect(edict_t *ent, char *userinfo);
void		ClientBegin(edict_t *ent);
void		ClientUserinfoChanged(edict_t *ent, char *userinfo);

sg_bot_t sg_bots[SG_MAXBOTS];

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

/*
 * BOTFILL -- the roster keeps itself. sv_botfill names the players each
 * team should field; bots make up whatever humans do not, one change per
 * second so joins stagger like the launch scripts always staggered them.
 * A human connecting displaces a bot on the team the balancer gives them;
 * a human leaving is backfilled the next second. Zero (the default) turns
 * the whole thing off and the manual `sv sg add` world works as before.
 * Only SLIPGATE's own bots are ever removed -- the legacy library's bots
 * belong to the legacy library (SG_OwnsBot is the property line).
 */
/*
 * Which bot goes, by the only rule the roster has ever used: lowest score.
 * team 0 means "either team", which is what an admin typing `kick worst`
 * is asking. Split out of Botfill_RemoveOne so the balancer and the console
 * retire the same bot for the same reason -- two different notions of
 * "worst" is how an admin and an automatic balancer end up fighting over
 * the roster in the middle of a wave.
 */
static int Botfill_WorstIndex(int team)
{
	int i, worst = -1, worst_score = 0x7fffffff;

	for (i = 0; i < SG_MAXBOTS; i++)
	{
		if (!sg_bots[i].active || !sg_bots[i].ent || !sg_bots[i].ent->inuse)
			continue;
		if (team && sg_bots[i].ent->client->ctf.teamnum != team)
			continue;
		if (sg_bots[i].ent->client->resp.score < worst_score)
		{
			worst_score = sg_bots[i].ent->client->resp.score;
			worst = i;
		}
	}
	return worst;
}

/* the teardown half, shared by every path that retires a bot: disconnect,
 * free the edict, and clear the slot in that order -- the slot must not be
 * reusable until the edict is actually gone */
static void Botfill_Drop(int slot)
{
	ClientDisconnect(sg_bots[slot].ent);
	SG_FreeClientEdict(sg_bots[slot].ent);
	sg_bots[slot].active = false;
	sg_bots[slot].ent = NULL;
}

static qboolean Botfill_RemoveOne(int team)
{
	int worst = Botfill_WorstIndex(team);

	if (worst < 0)
		return false;
	gi.bprintf(PRINT_HIGH, "%s yields its slot.\n",
	           sg_bots[worst].ent->client->pers.netname);
	Botfill_Drop(worst);
	return true;
}

void Botfill_Frame(void)
{
	static float next_check;
	cvar_t *fill = gi.cvar("sv_botfill", "0", 0);
	int want[2];
	int humans[2] = {0, 0}, bots[2] = {0, 0};
	int i, t;

	/* one value fills both teams to it; "5:1" sets red and blue
	 * separately -- colon, not space, because the engine's set command
	 * reads a third token as a serverinfo flag and sets NOTHING (wave
	 * 139: the 5v1 control ran its whole game with zero bots) */
	if (sscanf(fill->string, "%d:%d", &want[0], &want[1]) < 2)
		want[1] = want[0] = (int)fill->value;

	if ((want[0] <= 0 && want[1] <= 0) || SG_TimerPending(next_check))
		return;
	SG_TimerArm(&next_check, 1.0f);

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

	/* hysteresis: act only when the imbalance has stood three checks --
	 * a roster decision is not an emergency, and patience ends every
	 * oscillation a second controller could start */
	{
		static int over_streak[2], under_streak[2];
		qboolean acted = false;

		for (t = 0; t < 2 && !acted; t++)
		{
			if (humans[t] + bots[t] > want[t] && bots[t] > 0)
			{
				if (++over_streak[t] >= 3)
				{
					Botfill_RemoveOne(SG_TeamFromIdx(t));
					over_streak[t] = 0;
					acted = true;
				}
			}
			else
				over_streak[t] = 0;
		}
		for (t = 0; t < 2; t++)
		{
			if (acted)
				break;
			if (humans[t] + bots[t] < want[t])
			{
				/* an EMPTY server fills instantly -- the hysteresis
				 * exists to damp roster churn in live games, and a
				 * fresh map after rotation has no roster to damp; a
				 * human walking in stared at an empty arena for 45
				 * seconds (the owner, wave 264, four times over) */
				if (bots[0] + bots[1] == 0 ||
				    ++under_streak[t] >= 3)
				{
					SG_AddBotTeam(SG_TeamFromIdx(t));
					under_streak[t] = 0;
					acted = true;
				}
			}
			else
				under_streak[t] = 0;
		}
	}
}

/*
 * Ownership, for the legacy glue to ask. Two bot systems share the match --
 * that is the A/B harness working -- and the old code's FL_BOT loops must
 * not assume every bot is theirs.
 */
qboolean SG_OwnsBot(edict_t *ent)
{
	int i;

	for (i = 0; i < SG_MAXBOTS; i++)
		if (sg_bots[i].active && sg_bots[i].ent == ent)
			return true;
	return false;
}

qboolean SG_AddBot(void)
{
	return SG_AddBotTeam(0);
}

/*
 * DUPLICATE-NAME GUARD (capability census gap 12b).
 *
 * The sixteen names above exist because two clients wearing one name merge
 * in every per-name analysis. A HUMAN wearing the name a bot is about to
 * take is the same fault arriving from the other side -- nothing stops a
 * player calling himself "[SG]Arach" -- and it lands on the scoreboard as
 * well as in the telemetry.
 *
 * The seat keeps its identity: the slot number, the skin and everything
 * else derived from the slot are untouched. Only the DISPLAY name walks
 * forward to the next free entry in the table.
 */
static qboolean SG_NameOnHuman(const char *name)
{
	int i;

	for (i = 1; i <= game.maxclients; i++)
	{
		edict_t *e = &g_edicts[i];

		if (!e->inuse || !e->client)
			continue;
		if (e->flags & FL_BOT)      /* a bot's name is ours to move, not to
		                             * dodge; the slots already differ */
			continue;
		if (!Q_stricmp(e->client->pers.netname, name))
			return true;
	}
	return false;
}

qboolean SG_AddBotTeam(int teamnum)
{
	edict_t *ent;
	char userinfo[MAX_INFO_STRING];
	char name[32];
	int i, slot = -1, tries;

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

	memset(userinfo, 0, sizeof(userinfo));
	/* tag FIRST -- owner's ruling 2026-08-05: "[SG]Arach", not "Arach[SG]".
	 * The name walks past any connected human already wearing it; sixteen
	 * tries is the whole table, and past that the collision is accepted
	 * rather than the bot refused -- a duplicate name is a nuisance, a
	 * short-handed team is a broken match. */
	for (tries = 0; tries < 16; tries++)
	{
		Com_sprintf(name, sizeof(name), "[SG]%s",
		            sg_names[(slot + tries) & 15]);
		if (!SG_NameOnHuman(name))
			break;
	}
	Info_SetValueForKey(userinfo, "name", name);
	/* a CTF-conforming request from the start; the team letter is corrected
	 * in the second userinfo pass once the team is known, and servers
	 * without a skin list get a parseable rb-set name instead of grunt */
	Info_SetValueForKey(userinfo, "skin", va("male/rb-rm%d", 1 + (slot % 6)));
	Info_SetValueForKey(userinfo, "hand", "0");

	ent = SG_SpawnClientEdict();
	if (!ent)
		return false;
	ent->flags &= ~FL_BOT;
	ent->inuse = false;
	if (!ClientConnect(ent, userinfo))
	{
		SG_FreeClientEdict(ent);
		return false;
	}
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
	ent->inuse = true;
	ent->flags |= FL_BOT;
	ClientUserinfoChanged(ent, userinfo);
	ClientBegin(ent);
	/*
	 * Again, now that a TEAM exists. The skin force inside the first
	 * userinfo pass ran while teamnum was still UNDEFINED, its red/blue
	 * ternary defaulted to blue, and every bot the balancer then sent
	 * red played the whole match in the wrong colors (reported live
	 * off the scoreboard portraits, wave 99 era). The second pass sees
	 * the real team and paints the uniform right. The REQUEST is also
	 * re-lettered here (owner's ruling: CTF bots wear their team's CTF
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

	/*
	 * THE RADIO IS ON (sg_radio, owner's ruling 2026-08-05: "callout in
	 * conjunction, that's just good teamplay"). PlayTeamSound (g_cmds.c:121)
	 * tests these bits TWICE -- once on the sender, where a clear pair is
	 * "Your radio is off!" and no call at all, and once per recipient, where a
	 * clear pair skips that client. A human never sees the menu that sets them
	 * for a bot, so a bot with them clear is a bot that can neither speak on
	 * the radio nor be spoken to, and every call it made would be refused at
	 * the first test with a print aimed at itself.
	 *
	 * Both bits, not just the sound one: the text half is what a receiver
	 * running radiotext reads, and a bot is a receiver too when a teammate
	 * calls. The bot-bound half costs nothing -- ForceCommand's unicast at a
	 * bot is retracted by the net shim (sg_net.c SG_unicast: bots have no
	 * engine-side client to address) and the radiotext print goes down the
	 * suppressed print path in the same file.
	 *
	 * Set unconditionally rather than under sg_radio: the cvar decides whether
	 * bots CALL, and a bot that joined while it was 0 must still be reachable
	 * by a human's radio the moment it goes to 1.
	 */
	ent->client->ctf.extra_flags |=
	    (CTF_EXTRAFLAGS_RADIO_TEXT | CTF_EXTRAFLAGS_RADIO_SOUND);

	sg_bots[slot].ent = ent;
	sg_bots[slot].active = true;
	sg_bots[slot].seed = -1;
	sg_bots[slot].tac_seed = -1;
	sg_bots[slot].tac_role = -1;
	sg_bots[slot].stuck_time = 0.0f;
	sg_bots[slot].inlinks_n = 0;
	sg_bots[slot].exitasym_n = 0;
	sg_bots[slot].exitasym_armed = false;
	sg_bots[slot].beat_ready = false;   /* a join is not a respawn */
	sg_bots[slot].beat_until = 0.0f;
	/* no errand, and no row: slot 0 is a real belief row, -1 is nobody's */
	sg_bots[slot].lead_ent = 0;
	sg_bots[slot].lead_slot = -1;
	sg_bots[slot].lead_seed = -1;
	sg_bots[slot].lead_at = 0.0f;
	sg_bots[slot].lead_next = 0.0f;
	sg_bots[slot].mega_on = false;
	sg_bots[slot].mega_hp = 0;
	sg_bots[slot].mega_since = 0.0f;
	sg_bots[slot].mega_next = 0.0f;
	sg_bots[slot].escprior_bucket = -1;
	sg_bots[slot].escprior_until = 0.0f;
	sg_bots[slot].fake_ping = 5 + rand() % 11;
	/* a fresh joiner holds no grudge, and seed 0 is a real place: -1 is
	 * the only value that means "nobody has died here yet" */
	sg_bots[slot].tilt_seed = -1;
	sg_bots[slot].tilt_killer_seed = -1;
	sg_bots[slot].tilt_lane_n = 0;
	sg_bots[slot].tilt_until = 0.0f;
	sg_bots[slot].tilt_caution_until = 0.0f;
	sg_bots[slot].tilt_death_time = -1000.0f;
	sg_bots[slot].tilt_window = 0.0f;
	SG_PersonaBind(ent, slot);      /* the name now indexes a character */

	/*
	 * The persona in the join print, because the first question asked of a
	 * behaviour difference is always "which one is that" and the second is
	 * "was it cast that way or is it broken". A NULL persona (sg_persona 0)
	 * prints the old line unchanged.
	 */
	{
		const char *pname = SG_PersonaName(ent);

		if (pname)
			gi.dprintf("slipgate: %s entered (persona %s)\n",
			           Info_ValueForKey(userinfo, "name"), pname);
		else
			gi.dprintf("slipgate: %s entered\n",
			           Info_ValueForKey(userinfo, "name"));
	}
	return true;
}

int SG_RemoveBots(void)
{
	int i, n = 0;
	for (i = 0; i < SG_MAXBOTS; i++)
	{
		if (!sg_bots[i].active)
			continue;
		if (sg_bots[i].ent && sg_bots[i].ent->inuse)
		{
			ClientDisconnect(sg_bots[i].ent);
			SG_FreeClientEdict(sg_bots[i].ent);
		}
		sg_bots[i].active = false;
		sg_bots[i].ent = NULL;
		n++;
	}
	return n;
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
			gi.cprintf(NULL, PRINT_HIGH,
			           "slot name                 team  score skill role     seed\n");
		team = e->client->ctf.teamnum;
		role = sg_bots[i].last_role;
		gi.cprintf(NULL, PRINT_HIGH, "%3d  %-20s %-5s %5d %5.2f %-8s %4d\n",
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
		gi.cprintf(NULL, PRINT_HIGH, "slipgate: no bots\n");
	else
		gi.cprintf(NULL, PRINT_HIGH, "slipgate: %d bot%s\n",
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

	gi.bprintf(PRINT_HIGH, "%s was removed.\n",
	           sg_bots[slot].ent->client->pers.netname);
	Botfill_Drop(slot);
	return true;
}

qboolean SG_KickWorst(void)
{
	int worst = Botfill_WorstIndex(0);   /* 0: either team */

	if (worst < 0)
		return false;
	gi.bprintf(PRINT_HIGH, "%s was cut, lowest score.\n",
	           sg_bots[worst].ent->client->pers.netname);
	Botfill_Drop(worst);
	return true;
}

