#include <string.h>
#include <stdio.h>
#include "g_local.h"
#include "g_tourney.h"
#include "g_ctffunc.h"
#include <time.h>

stats_player_s* p_start_player = NULL;

// Appends to a fixed buffer without ever running past its end.
//
// stats_output used strcat into a 512-byte outbuf while emitting roughly 800
// bytes of literal text plus 30 numeric conversions, so every "cmd stats"
// overran the stack even with all-zero stats. Stock LMCTF fit inside 512; the
// PICKUPS, RAIL and DAMAGE sections are what pushed it over.
//
// ctf_SafePrint only queues 2000 bytes per call, so that is the real ceiling.
#define STATS_OUTBUF_SIZE	2048

static void stats_appendbuf(char* dest, size_t destsize, const char* src)
{
	size_t used;

	if (!dest || !src || destsize == 0)
		return;

	used = strlen(dest);
	if (used + 1 >= destsize)
		return;					// full; drop the rest rather than corrupt

	strncpy(dest + used, src, destsize - used - 1);
	dest[destsize - 1] = '\0';
}


void stats_log_init()
{
	p_start_player = NULL;
}

void stats_log_reset()
{
	stats_player_s* p_current_player;

	p_current_player = p_start_player;
	while (p_current_player != NULL)
	{
		p_start_player = p_start_player->pNext;
		gi.TagFree(p_current_player);
		p_current_player = p_start_player;
	}

	stats_log_init();
}

stats_player_s* stats_find_dropped_player(char* name)
{
	stats_player_s* p_current_player = p_start_player;

	while (p_current_player != NULL)
	{
		if ((strcmp(p_current_player->info.name, name) == 0) &&
			p_current_player->dropped)
			break;
		else
			p_current_player = p_current_player->pNext;
	}

	return p_current_player;
}

void stats_init_player(stats_player_s* p_player)
{
	// set up initial stats for player
	int i;
	for (i = 0; i < MAX_PLAYER_STATS; i++)
		p_player->stats[i] = 0;
	//	p_player->dropped = false;
}

stats_player_s* stats_new_player(char* name)
{
	stats_player_s* p_player;

	p_player = (stats_player_s*)gi.TagMalloc(sizeof(stats_player_s), TAG_GAME);
	if (!p_player) {
		gi.error(ERR_FATAL, "LMCTF: allocation failed in %s", __func__);
		return NULL;
	}

	stats_init_player(p_player);
	p_player->dropped = false;
	p_player->info.teamnum = CTF_TEAM_UNDEFINED;
	strcpy(p_player->info.name, name);

	//attach the player to the front of the list
	p_player->pNext = p_start_player;
	p_start_player = p_player;

	return p_player;
}

void stats_set_name(edict_t* ent, char* name)
{
	if (!ent || !ent->client || !name)
		return;

	if (ent->client->p_stats_player)
	{
		/* here should check for duplicate name
		   if duplicate name is found, disallow change,
		   and force back to original name  */
		strncpy(ent->client->p_stats_player->info.name, name,
			sizeof(ent->client->p_stats_player->info.name) - 1);
		ent->client->p_stats_player->info.name[
			sizeof(ent->client->p_stats_player->info.name) - 1] = 0;
	}
	return;
}

void stats_cleanup()
{
	stats_player_s* p_current_player, * p_prev_player;

	// clear out dropped players from start and
	// adjust start if needed
	p_current_player = p_start_player;
	while (p_current_player && (p_current_player->dropped))
	{
		p_start_player = p_current_player->pNext;
		gi.TagFree(p_current_player);
		p_current_player = p_start_player;

	}

	// case when everyone is gone
	if (p_start_player == NULL)
	{
		return;
	}

	stats_init_player(p_start_player);

	// clear out dropped players and reinitialize stats
	p_current_player = p_start_player->pNext; // When the start is valid
	p_prev_player = p_start_player;

	while (p_current_player)
	{
		if (p_current_player->dropped)
		{
			p_prev_player->pNext = p_current_player->pNext;
			gi.TagFree(p_current_player);
		}
		else
		{
			stats_init_player(p_current_player);
			p_prev_player = p_current_player;
		}
		p_current_player = p_prev_player->pNext;

	}

	if (p_current_player)
		p_current_player->pNext = NULL;
}

void stats_add(edict_t* ent, int stat, long amount)
{
	if (!ent || !ent->client)
		return;
	if (stat < 0 || stat >= MAX_PLAYER_STATS)
		return;

	if (Match_CanScore() && ent->client->p_stats_player)
		ent->client->p_stats_player->stats[stat] += amount;
}

void stats_set(edict_t* ent, int stat, long amount)
{
	if (!ent || !ent->client)
		return;
	if (stat < 0 || stat >= MAX_PLAYER_STATS)
		return;

	if (Match_CanScore() && ent->client->p_stats_player)
		ent->client->p_stats_player->stats[stat] = amount;
}

long stats_get(edict_t* ent, int stat)
{
	if (!ent || stat < 0 || stat >= MAX_PLAYER_STATS)
		return 0;

	if (ent->client && ent->client->p_stats_player)
		return (ent->client->p_stats_player->stats[stat]);
	else
		return 0;
}


/*
==================
stats_fold_session

Folds this session's counters (stats_player_s.stats[], which stats_cleanup
wipes at every level change) into the player's lifetime totals in
client->ctfstats, which is what the SQLite backends read and write.

Without this the database round-trips an all-zero struct: gameplay only ever
touched the session array, and nothing ever wrote ctfstats.

Called from CommitPlayerData just before the backend save. The session array is
zeroed afterwards so a second commit in the same session cannot double-count.

Not every lifetime field has a session counter yet -- offense_kills, num_sprees,
max_streak and suicides have no STATS_* equivalent, so they are left alone
rather than guessed at.
==================
*/
/*
==================
stats_record_frag

One frag by attacker. Bumps the running streak, remembers the best one seen
this level, and counts a spree on the single frag that reaches the threshold.
==================
*/
void stats_record_frag(edict_t* attacker)
{
	long streak;

	if (!attacker || !attacker->client)
		return;

	stats_add(attacker, STATS_FRAGS, 1);
	stats_add(attacker, STATS_CUR_STREAK, 1);

	streak = stats_get(attacker, STATS_CUR_STREAK);

	if (streak > stats_get(attacker, STATS_MAX_STREAK))
		stats_set(attacker, STATS_MAX_STREAK, streak);

	// == not >=, so a long streak counts as one spree, not one per frag
	if (streak == STATS_SPREE_MIN)
		stats_add(attacker, STATS_SPREES, 1);
}

/*
==================
stats_record_death

One death by victim, breaking any streak they had going.
==================
*/
void stats_record_death(edict_t* victim, qboolean self_inflicted)
{
	if (!victim || !victim->client)
		return;

	stats_add(victim, STATS_DEATHS, 1);
	stats_set(victim, STATS_CUR_STREAK, 0);

	if (self_inflicted)
		stats_add(victim, STATS_SUICIDES, 1);
}

/*
==================
stats_record_fragged

Killed by another player. Deliberately separate from STATS_DEATHS: in stock
LMCTF that counter means deaths you bring on yourself, which is why
Team_Change can subtract one from it after firing a synthetic player_die.
This is the counter behind playerstats_t.fragged.
==================
*/
void stats_record_fragged(edict_t* victim)
{
	if (!victim || !victim->client)
		return;

	stats_add(victim, STATS_FRAGGED, 1);
	stats_set(victim, STATS_CUR_STREAK, 0);
}

/*
==================
stats_record_capture

One flag capture by capper. Extends their capture streak and ends everyone
else's on the other side: a streak means captures in a row with no enemy
capture in between, so the moment one team scores the other team's runs stop.
==================
*/
void stats_record_capture(edict_t* capper)
{
	edict_t* other;
	long streak;
	int i;

	if (!capper || !capper->client)
		return;

	stats_add(capper, STATS_CAPTURES, 1);
	stats_add(capper, STATS_CUR_CAPSTREAK, 1);

	streak = stats_get(capper, STATS_CUR_CAPSTREAK);
	if (streak > stats_get(capper, STATS_MAX_CAPSTREAK))
		stats_set(capper, STATS_MAX_CAPSTREAK, streak);

	// this capture breaks every run held by the opposing team
	for (i = 0; i < game.maxclients; i++)
	{
		other = g_edicts + 1 + i;

		if (!other->inuse || !other->client)
			continue;
		if (other->client->ctf.teamnum == capper->client->ctf.teamnum)
			continue;

		stats_set(other, STATS_CUR_CAPSTREAK, 0);
	}
}

void stats_fold_session(edict_t* ent)
{
	playerstats_t* ps;
	time_t now;
	struct tm* lt;
	long session_seconds;

	if (!ent || !ent->client || !ent->client->p_stats_player)
		return;

	ps = &ent->client->ctfstats;

	ps->frags         += (unsigned int)stats_get(ent, STATS_FRAGS);
	ps->fragged       += (unsigned int)stats_get(ent, STATS_FRAGGED);

	ps->flag_pickups  += (int)stats_get(ent, STATS_OFFENSE_FLAG);
	ps->flag_captures += (int)stats_get(ent, STATS_CAPTURES);
	ps->flag_returns  += (int)stats_get(ent, STATS_RETURNS);
	ps->flag_kills    += (int)stats_get(ent, STATS_OFFENSE_CARRIER);
	ps->assists       += (int)stats_get(ent, STATS_ASSISTS);

	ps->defense_kills += (int)(stats_get(ent, STATS_DEFENSE_BASE) +
	                           stats_get(ent, STATS_DEFENSE_FLAG) +
	                           stats_get(ent, STATS_DEFENSE_CARRIER));

	ps->offense_kills += (int)stats_get(ent, STATS_OFFENSE_KILLS);
	ps->suicides      += (int)stats_get(ent, STATS_SUICIDES);

	ps->score         += (int)stats_get(ent, STATS_SCORE);
	ps->deaths        += (int)stats_get(ent, STATS_DEATHS);
	ps->flag_drops    += (int)stats_get(ent, STATS_OFFENSE_FLAGLOST);

	// kept separately as well as summed into defense_kills: which kind of
	// defending a player does is the interesting part, and the summed column
	// alone threw that away
	ps->defense_base    += (int)stats_get(ent, STATS_DEFENSE_BASE);
	ps->defense_flag    += (int)stats_get(ent, STATS_DEFENSE_FLAG);
	ps->defense_carrier += (int)stats_get(ent, STATS_DEFENSE_CARRIER);

	ps->item_quad     += (int)stats_get(ent, STATS_ITEM_QUAD);
	ps->item_shield   += (int)stats_get(ent, STATS_ITEM_SHIELD);
	ps->item_armor    += (int)stats_get(ent, STATS_ITEM_ARMOR);
	ps->item_mega     += (int)stats_get(ent, STATS_ITEM_MEGA);

	ps->rune_strength += (int)stats_get(ent, STATS_RUNE_STRENGTH);
	ps->rune_haste    += (int)stats_get(ent, STATS_RUNE_HASTE);
	ps->rune_regen    += (int)stats_get(ent, STATS_RUNE_REGEN);
	ps->rune_resist   += (int)stats_get(ent, STATS_RUNE_RESIST);

	ps->rail_shot     += (unsigned long)stats_get(ent, STATS_RAIL_SHOT);
	ps->rail_hit      += (unsigned long)stats_get(ent, STATS_RAIL_HIT);
	ps->rail_kill     += (int)stats_get(ent, STATS_RAIL_KILL);

	ps->damage_given    += (unsigned long)stats_get(ent, STATS_DAMAGE_GIVEN);
	ps->damage_received += (unsigned long)stats_get(ent, STATS_DAMAGE_REC);

	ps->ping_total    += (unsigned long)stats_get(ent, STATS_PING_TOTAL);
	ps->ping_samples  += (unsigned long)stats_get(ent, STATS_PING_SAMPLES);
	ps->num_sprees    += (unsigned int)stats_get(ent, STATS_SPREES);

	ps->sweeps        += (int)stats_get(ent, STATS_SWEEPS);

	// both streaks are lifetime bests, so they take the larger of the two
	if ((int)stats_get(ent, STATS_MAX_STREAK) > ps->max_streak)
		ps->max_streak = (int)stats_get(ent, STATS_MAX_STREAK);

	if ((int)stats_get(ent, STATS_MAX_CAPSTREAK) > ps->max_cap_streak)
		ps->max_cap_streak = (int)stats_get(ent, STATS_MAX_CAPSTREAK);

	ps->shots         += (unsigned long)stats_get(ent, STATS_SHOTS);
	ps->shots_hit     += (unsigned long)stats_get(ent, STATS_SHOTS_HIT);

	// time on the server this session, in seconds (one frame = 100ms)
	session_seconds = (long)((level.framenum - ent->client->ctf.original_enterframe) / 10);
	if (session_seconds > 0)
	{
		ps->playingtime    += (int)session_seconds;
		ps->total_playtime += (int)(session_seconds / 60);
	}

	now = time(NULL);
	lt = localtime(&now);
	if (lt)
	{
		strftime(ps->last_played, sizeof(ps->last_played), "%Y-%m-%d %H:%M:%S", lt);
		if (ps->member_since[0] == '\0')
			strncpy(ps->member_since, ps->last_played, sizeof(ps->member_since) - 1);
		ps->member_since[sizeof(ps->member_since) - 1] = '\0';
	}

	strncpy(ps->player_name, ent->client->pers.netname, sizeof(ps->player_name) - 1);
	ps->player_name[sizeof(ps->player_name) - 1] = '\0';

	// folded; clear the session so a second commit cannot count it twice
	stats_init_player(ent->client->p_stats_player);
}

void stats_clear(edict_t* ent)
{
	if (!ent || !ent->client || !ent->client->p_stats_player)
		return;

	stats_init_player(ent->client->p_stats_player);
	ent->client->resp.score = 0;
}


void stats_output(edict_t* ent, stats_player_s* p_player)
{
	int total_encounters;

	// callers can reach us with a NULL record -- a player who has disconnected
	// keeps their netname but has p_stats_player cleared.
	if (!p_player)
	{
		ctf_SafePrint(ent, PRINT_HIGH, "No stats recorded for that player.\n");
		return;
	}

	char teambuf[MAX_INFO_STRING];
	char* conbuf;
	char outbuf[STATS_OUTBUF_SIZE];
	char tmpbuf[MAX_INFO_STRING];

	strcpy(teambuf, "");
	ctf_teamstring(teambuf, p_player->info.teamnum, CTF_TEAM_MATCHING);

	if (p_player->dropped)
		conbuf = "quit";
	else
		conbuf = "active";



	total_encounters = p_player->stats[STATS_FRAGS] + p_player->stats[STATS_DEATHS];

	strcpy(outbuf, "");


	Com_sprintf (tmpbuf, sizeof tmpbuf, "\n(%s) [%s] %s\n", teambuf, conbuf, p_player->info.name);
	stats_appendbuf(outbuf, sizeof(outbuf), tmpbuf);

	// BUZZKILL - IMPROVED ANALYTICS - START
	snprintf(tmpbuf, sizeof(tmpbuf), "--SCORE--------------------------------------\nScore=%ld Frags=%ld Deaths=%ld Eff=%ld%%\n",
		p_player->stats[STATS_SCORE],
		p_player->stats[STATS_FRAGS],
		p_player->stats[STATS_DEATHS],
		total_encounters == 0 ? 0 : 100 * p_player->stats[STATS_FRAGS] / total_encounters);
	stats_appendbuf(outbuf, sizeof(outbuf), tmpbuf);

	snprintf(tmpbuf, sizeof(tmpbuf), "--CTF----------------------------------------\nDef Base=%ld Def Flag=%ld Def Carrier=%ld\nGot Flag=%ld Lost Flag=%ld Captures=%ld\n",
		p_player->stats[STATS_DEFENSE_BASE],
		p_player->stats[STATS_DEFENSE_FLAG],
		p_player->stats[STATS_DEFENSE_CARRIER],
		p_player->stats[STATS_OFFENSE_FLAG],
		p_player->stats[STATS_OFFENSE_FLAGLOST],
		p_player->stats[STATS_CAPTURES]);
	stats_appendbuf(outbuf, sizeof(outbuf), tmpbuf);

	snprintf(tmpbuf, sizeof(tmpbuf), "Kill Carrier=%ld Flag Returns=%ld Assists=%ld\n--PING---------------------------------------\nAverage Ping=%ld Samples=%ld\n",
		p_player->stats[STATS_OFFENSE_CARRIER],
		p_player->stats[STATS_RETURNS],
		p_player->stats[STATS_ASSISTS],
		p_player->stats[STATS_PING_TOTAL] / (p_player->stats[STATS_PING_SAMPLES] > 0 ? p_player->stats[STATS_PING_SAMPLES] : 1),
		p_player->stats[STATS_PING_SAMPLES]);
	stats_appendbuf(outbuf, sizeof(outbuf), tmpbuf);

	// BUZZKILL - IMPROVED ANALYTICS - RUNES
	snprintf(tmpbuf, sizeof(tmpbuf), "--PICKUPS------------------------------------\nStrength=%ld Haste=%ld Regen=%ld Resist=%ld\nQuad=%ld Shield=%ld Armor=%ld Mega=%ld\n---------------------------------------------\n",
		p_player->stats[STATS_RUNE_STRENGTH],
		p_player->stats[STATS_RUNE_HASTE],
		p_player->stats[STATS_RUNE_REGEN],
		p_player->stats[STATS_RUNE_RESIST],
		p_player->stats[STATS_ITEM_QUAD],
		p_player->stats[STATS_ITEM_SHIELD],
		p_player->stats[STATS_ITEM_ARMOR],
		p_player->stats[STATS_ITEM_MEGA]);
	stats_appendbuf(outbuf, sizeof(outbuf), tmpbuf);

	snprintf(tmpbuf, sizeof(tmpbuf), "--RAIL---------------------------------------\nShots=%ld Hits=%ld Kills=%ld Accuracy=%ld\n---------------------------------------------\n",
		p_player->stats[STATS_RAIL_SHOT],
		p_player->stats[STATS_RAIL_HIT],
		p_player->stats[STATS_RAIL_KILL],
		p_player->stats[STATS_RAIL_SHOT] == 0 ? 0 : 100 * p_player->stats[STATS_RAIL_HIT] / p_player->stats[STATS_RAIL_SHOT]);
	stats_appendbuf(outbuf, sizeof(outbuf), tmpbuf);

	snprintf(tmpbuf, sizeof(tmpbuf), "--DAMAGE-------------------------------------\nGiven=%ld Received=%ld Eff=%ld\n---------------------------------------------\n",
		p_player->stats[STATS_DAMAGE_GIVEN],
		p_player->stats[STATS_DAMAGE_REC],
		p_player->stats[STATS_DAMAGE_REC] == 0 ? 100 : 100 * p_player->stats[STATS_DAMAGE_GIVEN] / p_player->stats[STATS_DAMAGE_REC]);
	stats_appendbuf(outbuf, sizeof(outbuf), tmpbuf);

	snprintf(tmpbuf, sizeof(tmpbuf), "--STREAKS------------------------------------\nBest=%ld Sprees=%ld Suicides=%ld\nFragged=%ld Off Kills=%ld\nCap Streak=%ld Sweeps=%ld\n---------------------------------------------\n",
		p_player->stats[STATS_MAX_STREAK],
		p_player->stats[STATS_SPREES],
		p_player->stats[STATS_SUICIDES],
		p_player->stats[STATS_FRAGGED],
		p_player->stats[STATS_OFFENSE_KILLS],
		p_player->stats[STATS_MAX_CAPSTREAK],
		p_player->stats[STATS_SWEEPS]);
	stats_appendbuf(outbuf, sizeof(outbuf), tmpbuf);

	// BUZZKILL - IMPROVED ANALYTICS - END

	ctf_SafePrint(ent, PRINT_HIGH, outbuf);

}

void Cmd_PlayerStats_f(edict_t* ent)
{
	edict_t* temp, * target;
	stats_player_s* p_player;
	int i;
	char* p;
	char lowerstr[MAX_INFO_STRING];

	p = gi.args();

	if (p && strlen(p))
	{
		LowerCase(p);
		target = NULL;
		for (i = 0; i < game.maxclients; i++)
		{
			temp = g_edicts + 1 + i;

			// skip slots never filled or since vacated; a departed player keeps
			// their netname but not their stats record
			if (!temp->inuse || !temp->client || !temp->client->pers.connected)
				continue;

			strncpy(lowerstr, temp->client->pers.netname, sizeof(lowerstr) - 1);
			lowerstr[sizeof(lowerstr) - 1] = 0;
			LowerCase(lowerstr);
			if (strstr(lowerstr, p))
			{
				target = temp;
				break;
			}
		}
	}
	else
		target = ent;

	if (!target)
	{
		ctf_SafePrint(ent, PRINT_HIGH, "Cannot find a matching player.\n");
		return;
	}

	if (!target->client)
	{
		ctf_SafePrint(ent, PRINT_HIGH, "Cannot find a matching player.\n");
		return;
	}

	p_player = target->client->p_stats_player;
	stats_output(ent, p_player);

}

void Cmd_StatsAll_f(edict_t* ent)
{
	stats_player_s* p_player;

	p_player = p_start_player;
	while (p_player != NULL)
	{
		stats_output(ent, p_player);
		p_player = p_player->pNext;
	}
}
