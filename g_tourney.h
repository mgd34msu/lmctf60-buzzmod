#ifndef _P_TOURNEY_H_
#define _P_TOURNEY_H_

void     SpawnTourneyClock(void);
void     StartMatch (char *levelname);
void     KillMatch(void);
qboolean Match_CanScore(void);
qboolean Match_InPlay(void);
qboolean Match_Mode(void);
qboolean Match_InCountdown(void);
qboolean GamePaused(void);
void     SetPause(qboolean state);
void     Victory(void);
qboolean Match_Over(void);
edict_t  *Query_OMVP(void);
edict_t  *Query_DMVP(void);
void     Reset_MVP(void);
void     Match_End(edict_t *ent);

extern int matchstate;

typedef enum {
    MATCH_NONE,
    MATCH_ENDLEVEL, // Match_Mode false before here
    MATCH_COUNTDOWN,
    MATCH_INPLAY,
    MATCH_OVER,
    MATCH_RAILGUN_COUNTDOWN,
    MATCH_RAILGUN_INPLAY,
    MATCH_RAILGUN_OVER,
} MATCH_STATES;

#endif
