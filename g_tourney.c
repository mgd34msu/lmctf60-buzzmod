#include "g_local.h"
#include "ctf_file_io.h"
#include "ctf_sqlite_unidb.h"
#include "g_ctffunc.h"
#include "stdlog.h"
#include "g_tourney.h"
#include "bat.h"

int matchstate = MATCH_NONE;
edict_t *tourneyclock = NULL;
qboolean match_pause = false;
edict_t *omvp = NULL, *dmvp = NULL;

edict_t *Reg_Clock = NULL;

extern stats_player_s *p_start_player;

extern edict_t *Railgun_Victor;
int One_Man_Left(void);
edict_t *Declare_Railgun_Victor(void);

void Use_Quad (edict_t *ent, gitem_t *item);


void Reset_MVP(void)
{
    omvp = dmvp = NULL;
}

edict_t *Query_OMVP(void)
{
    return omvp;
}

edict_t *Query_DMVP(void)
{
    return dmvp;
}

void Match_Start(edict_t *ent)
{
    int i;
    edict_t *player;
    char message[MAX_INFO_STRING];
    int player_count = 0;
    gitem_t *Item;
    //edict_t    *Item_ent;

    // Blank all players' stats, and respawn them
    for (i=0 ; i < game.maxclients ; i++) {
        player = g_edicts + 1 + i;

        // Don't bother killing them if they are an observer
        if (player->inuse && !player->client->resp.spectator) {
            player_count++;

            if(matchstate != MATCH_RAILGUN_COUNTDOWN) {
                player->health = 0;
                player_die (player, player, player, 100000, vec3_origin);
                // don't even bother waiting for death frames

                // Start our time over so we respawn on team spawn point
                player->client->resp.enterframe = level.framenum;

                //bat force them to hit the button to respawn.
                //took out respawn() and added a second to the respawn time
                //Too Many overflows!!!!!
                //respawn (player);
                //This is in seconds!!!

                //Let's try this in the respawn instead;
                //player->client->respawn_time = level.time + (0.2 * player_count);

                stats_clear(player); // Blank all our stats, whether we are here or not
            } else {
                player->health = 100;
                player->client->pers.weapon = FindItem ("railgun");;
                player->client->newweapon = player->client->pers.weapon;
                Item = FindItem("slugs");
                Add_Ammo(player, Item, 1000);
                ChangeWeapon(player);
            }
        }
    }

    if(matchstate == MATCH_RAILGUN_COUNTDOWN) {
        matchstate = MATCH_RAILGUN_INPLAY;
        ent->count = railtime->value;
        sprintf(message, "%d seconds. %d men enter 1 man leaves \n", ent->count, player_count);
        ctf_BSafePrint(PRINT_HIGH, message);
    } else {
        matchstate = MATCH_INPLAY;
        ent->count = ((unsigned short)timelimit->value) * 60;
        sprintf(message, "%d minutes until match ends.\n", ent->count/60);
        ctf_BSafePrint(PRINT_HIGH, message);
    }
}

void Victory(void)
{
    long i;
    edict_t *ent=NULL;
    long redscore, bluescore;
    long redcaps, bluecaps;
    long temp, oscore, dscore;
    char victory_buf[MAX_INFO_STRING];
    char temp_buf[MAX_INFO_STRING];
    char teambuf[MAX_INFO_STRING];

    bluescore = redscore = 0;
    bluecaps = redcaps = 0;
    oscore = dscore = 0;
    dmvp = omvp = NULL;

    strcpy(victory_buf,"");

    for (i=0 ; i<game.maxclients ; i++) {
        ent = g_edicts + 1 + i;

        if (!ent->inuse) {
            continue;
        }

        if (ent->client->ctf.teamnum == CTF_TEAM_RED) { // RED TEAM
            redscore += stats_get(ent, STATS_SCORE);
            redcaps  += stats_get(ent, STATS_CAPTURES);
        } else if (ent->client->ctf.teamnum == CTF_TEAM_BLUE) { // BLUE TEAM
            bluescore += stats_get(ent, STATS_SCORE);
            bluecaps  += stats_get(ent, STATS_CAPTURES);
        }
    }

    // Find the dmvp
    for (i=0 ; i<game.maxclients ; i++) {
        ent = g_edicts + 1 + i;

        temp = 4*stats_get(ent, STATS_OFFENSE_CARRIER) +
            3*stats_get(ent, STATS_DEFENSE_FLAG) +
            2*stats_get(ent, STATS_RETURNS) +
            stats_get(ent, STATS_DEFENSE_CARRIER) +
            stats_get(ent, STATS_DEFENSE_BASE);

        if (temp > dscore) {
            dscore = temp;
            dmvp = ent;
        }
    }

    // Find the omvp
    for (i=0 ; i<game.maxclients ; i++) {
        ent = g_edicts + 1 + i;

        // Exclude dmvp
        if (ent == dmvp) {
            continue;
        }

        temp = 8*stats_get(ent, STATS_CAPTURES) + 4*stats_get(ent, STATS_DEFENSE_CARRIER)
            + stats_get(ent, STATS_ASSISTS);

        if (temp > oscore) {
            oscore = temp;
            omvp = ent;
        }
    }

    if (dmvp) {
        strcpy(teambuf,"");
        ctf_teamstring(teambuf,dmvp->client->ctf.teamnum,CTF_TEAM_MATCHING);

        Com_sprintf(temp_buf, sizeof temp_buf, "Defense MVP: %s (%s)!\n",
            dmvp->client->pers.netname,
            teambuf);
        strcat(victory_buf,temp_buf);
    }

    if (omvp) {
        strcpy(teambuf,"");
        ctf_teamstring(teambuf,omvp->client->ctf.teamnum,CTF_TEAM_MATCHING);

        Com_sprintf(temp_buf, sizeof temp_buf, "Offense MVP: %s (%s)!\n",
            omvp->client->pers.netname,
            teambuf);
        strcat(victory_buf, temp_buf);
    }

    if (bluescore > redscore) {
        gi.sound (ent, CHAN_CTF, gi.soundindex ("ctf/end_blue.wav"), 1, ATTN_NONE, 0);
        sprintf(temp_buf, "Blue: %ld beats red: %ld!\n", bluescore,redscore);
        strcat(victory_buf, temp_buf);
    } else if (redscore > bluescore) {
        gi.sound (ent, CHAN_CTF, gi.soundindex ("ctf/end_red.wav"), 1, ATTN_NONE, 0);
        sprintf(temp_buf, "Red: %ld beats blue: %ld!\n", redscore,bluescore);
        strcat(victory_buf, temp_buf);
    } else {
        gi.sound (ent, CHAN_CTF, gi.soundindex ("ctf/end_tie.wav"), 1, ATTN_NONE, 0);
        sprintf(temp_buf, "Tie game at %ld!\n", redscore);
        strcat(victory_buf, temp_buf);
    }

    // A sweep: you were on the winning team and the other side never took your
    // flag once. Awarded before matchstate flips to MATCH_OVER, because
    // stats_add goes through Match_CanScore, which refuses to score after that.
    //
    // Victory() runs from both Match_End and BeginIntermission, so this is
    // gated on a level flag to keep it to one award per level.
    if (!level.sweeps_awarded) {
        int  winner = CTF_TEAM_UNDEFINED;
        long loser_caps = 0;

        if (bluescore > redscore) {
            winner = CTF_TEAM_BLUE;
            loser_caps = redcaps;
        } else if (redscore > bluescore) {
            winner = CTF_TEAM_RED;
            loser_caps = bluecaps;
        }
        // a tie is not a sweep, so winner stays undefined

        if (winner != CTF_TEAM_UNDEFINED && loser_caps == 0) {
            strcpy(teambuf, "");
            ctf_teamstring(teambuf, winner, CTF_TEAM_MATCHING);
            Com_sprintf(temp_buf, sizeof temp_buf,
                "Sweep! %s held the enemy to zero captures.\n", teambuf);
            strcat(victory_buf, temp_buf);

            for (i = 0; i < game.maxclients; i++) {
                ent = g_edicts + 1 + i;

                if (!ent->inuse || !ent->client) {
                    continue;
                }
                if (ent->client->ctf.teamnum == winner) {
                    stats_add(ent, STATS_SWEEPS, 1);
                }
            }
        }

        level.sweeps_awarded = true;
    }

    // Record once after sweep awards and before MATCH_OVER hides live stats.
    if (!level.match_recorded && CTF_StatsDBMode() == CTF_STATSDB_UNIFIED) {
        int winner_team = CTF_TEAM_UNDEFINED;
        int db_match_id;

        if (bluescore > redscore) {
            winner_team = CTF_TEAM_BLUE;
        } else if (redscore > bluescore) {
            winner_team = CTF_TEAM_RED;
        }

        // A match that never reaches Victory() -- server killed mid-game --
        // simply leaves no row, which beats a half-written one.
        db_match_id = DB_MatchBegin(level.mapname);

        if (db_match_id >= 0) {
            for (i = 0; i < game.maxclients; i++) {
                ent = g_edicts + 1 + i;

                if (!ent->inuse || !ent->client) {
                    continue;
                }
                if (ent->client->ctf.teamnum != CTF_TEAM_RED &&
                    ent->client->ctf.teamnum != CTF_TEAM_BLUE) {
                    continue;   // observers did not play the match
                }

                DB_MatchRecord(ent, db_match_id, ent->client->ctf.teamnum);
            }

            // level.time is seconds since the level started, which is the
            // match length for every practical purpose here
            DB_MatchFinish(db_match_id, (int)redscore, (int)bluescore,
                (int)redcaps, (int)bluecaps, winner_team, (int)level.time);

            level.match_recorded = true;
        }
    }

    ctf_BSafePrint(PRINT_HIGH, victory_buf);
    ctf_SetLogName(); //automated log rename check each level
    //sl_Logging( gi, NULL );
    //this will cause the name of the log to change when the day number changes
    //which hypothetically would be midnight
}

void Match_End(edict_t *ent)
{
    Victory();

    matchstate = MATCH_OVER;
    ent->count = 300; // five minutes
    game.teamslocked = false;
}

qboolean Match_InCountdown(void)
{
    return matchstate == MATCH_COUNTDOWN;
}

qboolean Match_InPlay(void)
{
    return matchstate == MATCH_INPLAY;
}

qboolean Match_Mode(void)
{
    return matchstate > MATCH_ENDLEVEL;
}

qboolean Match_CanScore(void)
{
    if ((matchstate == MATCH_OVER) || (matchstate == MATCH_COUNTDOWN) || (matchstate == MATCH_RAILGUN_COUNTDOWN)) {
        return false;
    } else {
        return true;
    }
}

qboolean Match_Over(void)
{
    return matchstate == MATCH_OVER;
}

qboolean GamePaused(void)
{
    return match_pause;
}

void SetPause(qboolean state)
{
    edict_t *ent;
    char *message;
    int i;

    match_pause = state;

    if (state) {
    	if ((int)autolock->value) {
    		game.teamslocked = false;
    	}
        message = "Game Paused\n";
    } else {
    	if ((int)autolock->value) {
    		game.teamslocked = true;
    	}
        message = "Game Unpaused\n";
    }

    for (i=0 ; i<game.maxclients ; i++) {  // Go through everyone
        ent = g_edicts + 1 + i;            // Select the client entity from the list of ents.
        if (!ent->inuse) {                 // Not in game yet is what I think this means.
            continue;
        }
        gi.centerprintf(ent, message);
    }
    gi.dprintf(message);
}


char *CTF_Countdown_Table[11] = {
    "ctf/go.wav",
    "ctf/1.wav",
    "ctf/2.wav",
    "ctf/3.wav",
    "ctf/4.wav",
    "ctf/5.wav",
    "ctf/6.wav",
    "ctf/7.wav",
    "ctf/8.wav",
    "ctf/9.wav",
    "ctf/10.wav",
};

short Last_Guy       = 0;
short Position_Count = 0;
int   Time_Left      = 0;

/**
 * Called from the game clock's think function pointer.
 * the edict_t* arg is the game clock
 */
void Tourney_Think(edict_t *ent)
{
    int     minutes;
    edict_t *player;
    int     i;
    char    message[MAX_INFO_STRING];

    ent->nextthink = level.time + 1;

    // If game paused, don't keep counting down
    if (GamePaused()) {
        return;
    }

    minutes = ent->count/60;

    if (matchstate == MATCH_OVER) {
        Time_Left = 0;
    } else if (ent->count < 60) {
        Time_Left = ent->count;
    } else {
        Time_Left = minutes + 1;
    }


    if (matchstate == MATCH_COUNTDOWN || matchstate == MATCH_RAILGUN_COUNTDOWN) {
        if(ent->count <= 10) {
            gi.sound (ent, CHAN_CTF, gi.soundindex (CTF_Countdown_Table[ent->count]), 1, ATTN_NONE, 0);
        }

        switch (ent->count) {
        case 60:
            ctf_BSafePrint(PRINT_HIGH, "60 seconds until match begins.\n");
            break;
        case 30:
            ctf_BSafePrint(PRINT_HIGH, "30 seconds until match begins.\n");
            break;
        case 15:
            if (matchstate == MATCH_RAILGUN_COUNTDOWN) {
                ctf_BSafePrint(PRINT_HIGH, "Prepare to annihilate your enemy...\n");

                gi.sound(ent, CHAN_CTF, gi.soundindex ("weapons/bfg__l1a.wav"), 1, ATTN_NONE, 0);
            } else {
                ctf_BSafePrint(PRINT_HIGH, "15 seconds until match begins.\n");
            }
            break;
        case 10:
            ctf_BSafePrint(PRINT_HIGH, "10 seconds until match begins.\n");
            break;
        case 0:
        //    gi.sound (ent, CHAN_CTF, gi.soundindex ("ctf/go.wav"), 1, ATTN_NONE, 0);
            Last_Guy = 0;
            Position_Count = 0;
            Match_Start(ent);
            break;
        }
    } else if (matchstate == MATCH_RAILGUN_INPLAY) {
        edict_t *cl_ent;

        if (++Position_Count == 6) {
            cl_ent = g_edicts + 1 + Last_Guy;

            while (cl_ent->health <= 0) {
                Last_Guy++;

                if (Last_Guy == game.maxclients) {
                    Last_Guy = 0;
                    break;
                }
            }

            cl_ent = g_edicts + 1 + Last_Guy;

            if(cl_ent->health > 0) {
                ForceCommand(cl_ent, "say I am %p");
            }

            Last_Guy++;
            Position_Count = 0;
        }


        if (ent->count == 0 || One_Man_Left()) {
            Railgun_Victor = Declare_Railgun_Victor();
            matchstate = MATCH_RAILGUN_OVER;
        } else if(ent->count <= 15) {
            sprintf(message, "%d\n", ent->count);
            ctf_BSafePrint(PRINT_HIGH, message);

            if (ent->count <= 10) {
                gi.sound (ent, CHAN_CTF, gi.soundindex (CTF_Countdown_Table[ent->count]), 1, ATTN_NONE, 0);
            }
        }

    } else if (matchstate == MATCH_INPLAY) {
        // Start the countdown if we hit the fraglimit
        if (ent->count > 10) {
            for (i=0 ; i<maxclients->value ; i++) {
                player = g_edicts + 1 + i;
                if (!player->inuse) {
                    continue;
                }

                if (fraglimit->value && stats_get(player, STATS_SCORE) >= fraglimit->value) {
                    // Fraglimit was hit
                    ent->count = 10;
                }
            }
        }

        if (ent->count <= 0) { // End match
            Match_End(ent);
            return;
        } else if (!(ent->count % 60)) {
            if (minutes > 1) {
                sprintf(message, "%d minutes until match ends.\n", minutes);
            } else if (minutes == 1) {
                sprintf(message, "%d minute until match ends.\n", minutes);
            } else {
                sprintf(message, "%d frags until match ends.\n", (int)fraglimit->value);
            }
            ctf_BSafePrint(PRINT_HIGH, message);
        } else if(ent->count <= 10) {
            gi.sound (ent, CHAN_CTF, gi.soundindex (CTF_Countdown_Table[ent->count]), 1, ATTN_NONE, 0);
        }
    } else if (matchstate == MATCH_OVER) {
        if (ent->count <= 0) {
            matchstate = MATCH_NONE;
            ent->think = G_FreeEdict;
            tourneyclock = NULL;
        }
    }

    ent->count--;
}

void KillMatch(void)
{
    matchstate = MATCH_NONE;

    if (tourneyclock) {
        tourneyclock->think = G_FreeEdict;
        tourneyclock->nextthink = level.time + 1;
        tourneyclock = NULL;
    }

    if ((int)autolock->value) {
    	game.teamslocked = false;
    }
}

void StartMatch (char *levelname)
{
	if ((int) autolock->value) {
		game.teamslocked = true;
	}

    ctf_ChangeMap(levelname, true);
}

void SpawnTourneyClock(void)
{
    edict_t    *ent;

    if (!tourneyclock) {
        ent = G_Spawn();
        tourneyclock = ent;
    } else {
        ent = tourneyclock;
    }

    if (matchstate == MATCH_RAILGUN_COUNTDOWN) {
        ent->count = 16;
    } else {
        ent->count = (int)countdown_time->value;
        matchstate = MATCH_COUNTDOWN;
    }

    // game is about to start, lock teams if necessary
    if ((int)autolock->value) {
    	game.teamslocked = true;
    }

    ent->think = Tourney_Think;
    ent->nextthink = level.time + 1;
}
