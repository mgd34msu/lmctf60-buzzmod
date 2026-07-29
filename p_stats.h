#ifndef _P_STATS_H_
#define _P_STATS_H_

#define STATS_PLAYER_SAMPLE_RATE 20 //how many frames between ping samples, 1 frame = 100 ms

#define STATS_PING_TOTAL		0	// long ping_total; //add to this every time ping is sampled
#define STATS_PING_SAMPLES		1	// long ping_samples; //# of samples(+1 each time you sample ping)
#define STATS_TIME				2	// long time; //time on server in seconds? minutes? 
// seconds would be cool for more precise calculation
#define STATS_SCORE				3	// int score; //the overall score
#define STATS_CAPTURES			4	// int captures; //how many direct flag captures
#define STATS_FRAGS				5	// int frags; //how many direct kills
#define STATS_DEATHS			6	// int deaths; //again, how many times you died
#define STATS_DEFENSE_FLAG		7	// int defense_flag; //defended the flag
#define STATS_DEFENSE_BASE		8	// int defense_base; //defended the base (whether flag present or not)
#define STATS_DEFENSE_CARRIER	9	// int defense_carrier; //defended flag carrier
#define STATS_ASSISTS			10	// int assists; //number of assists
#define STATS_RETURNS			11	// int returns; //returned the flag
#define STATS_OFFENSE_FLAG		12	// int offense_flag; //took the enemy flag
#define STATS_OFFENSE_CARRIER	13	// int offense_carrier; //killed enemy flag carrier
#define STATS_OFFENSE_FLAGLOST	14	// int offense_flaglost; //lost the enemy flag

// BUZZKILL - IMPROVED ANALYTICS - BEGIN
#define STATS_RUNE_STRENGTH		15	// picked up strength rune
#define STATS_RUNE_HASTE		16	// picked up haste rune
#define STATS_RUNE_REGEN		17	// picked up regen rune
#define STATS_RUNE_RESIST		18	// picked up resist rune
#define STATS_ITEM_QUAD			19	// picked up quad
#define STATS_ITEM_SHIELD		21	// picked up power shield (or screen)
#define STATS_ITEM_ARMOR		22	// picked up red armor
#define STATS_ITEM_MEGA			23  // picked up mega health
#define STATS_IS_FC				24	// is the flag carrier
#define STATS_HAS_ST			25	// has the strength rune
#define STATS_HAS_RS			26	// has the resist rune
#define STATS_HAS_HA			27	// has the haste rune
#define STATS_HAS_RG			29	// has the regen rune
#define STATS_RAIL_SHOT			30  // railgun shots fired
#define STATS_RAIL_HIT			31  // railgun hits
#define STATS_RAIL_KILL			32  // railgun kills (not equal to hits due to armor, runes, etc.)
#define STATS_RAIL_ACCURACY		33  // railgun accuracy
#define STATS_DAMAGE_GIVEN		34  // damage given
#define STATS_DAMAGE_REC		35  // damage received
// BUZZKILL - IMPROVED ANALYTICS - END

// BUZZKILL - counters behind the lifetime fields in playerstats_t
#define STATS_SUICIDES			36	// deaths the player caused themselves
#define STATS_OFFENSE_KILLS		37	// killed a defender inside the enemy base
#define STATS_CUR_STREAK		38	// frags in a row without dying
#define STATS_MAX_STREAK		39	// best streak this level
#define STATS_SPREES			40	// times a streak reached STATS_SPREE_MIN
#define MAX_PLAYER_STATS		41

#define STATS_SPREE_MIN			5	// frags in a row that count as a spree	

typedef struct {
	char name[MAX_INFO_STRING];
	int teamnum;
} stats_client_s;

typedef struct _stats_player {
	qboolean dropped; // whether this player was dropped

	stats_client_s info;

	long stats[MAX_PLAYER_STATS];

	struct _stats_player* pNext;
} stats_player_s;

typedef enum {
	STATS_SUICIDE,
	STATS_FRAG,
	STATS_FC_FRAG,
	STATS_FC_DEFENSE,
	STATS_FLAG_DEFENSE,
	STATS_FLAG_TOUCH,
	STATS_FLAG_RETURN,
	STATS_FLAG_CAPTURE,
	STATS_BASE_DEFENSE
} stats_event_t;

typedef struct {
	stats_client_s killer;
	stats_client_s killee;
	int mod;
	qboolean quad;
} stats_frag_data_s;

typedef struct {
	stats_client_s perp;
} stats_single_data_s;


void stats_add(edict_t* ent, int stat, long amount);
void stats_set(edict_t* ent, int stat, long amount);
long stats_get(edict_t* ent, int stat);
void stats_set_name(edict_t* ent, char* name);
void stats_clear(edict_t* ent);

// folds this session's counters into client->ctfstats for the SQLite backends
void stats_fold_session(edict_t* ent);

// one place each for the streak/spree and suicide bookkeeping
void stats_record_frag(edict_t* attacker);
void stats_record_death(edict_t* victim, qboolean self_inflicted);
void Cmd_PlayerStats_f(edict_t* ent);
void stats_log_init();
void stats_log_reset();
// returns pointer to lmctf_player_s struct of a dropped player given playername
stats_player_s* stats_find_dropped_player(char* name);
stats_player_s* stats_new_player(char* name);
void stats_cleanup(); // clean up stats before switching to next level
void Cmd_StatsAll_f(edict_t* ent);

#endif