/* sg_bot_util.h -- the few helpers every bot unit shares: teams, a flat
 * distance, and level-time stamps. */
#ifndef SG_BOT_UTIL_H
#define SG_BOT_UTIL_H

int SG_TeamIdx(int team);          /* CTF_TEAM_RED -> 0, CTF_TEAM_BLUE -> 1, else -1 */
int SG_TeamFromIdx(int idx);       /* 0 -> CTF_TEAM_RED, 1 -> CTF_TEAM_BLUE */
int SG_EnemyTeam(int team);        /* the other of the two, else CTF_TEAM_UNDEFINED */
float SG_DistXY(const vec3_t a, const vec3_t b);

/* A stamp is a level.time to wait for. */
void SG_TimerArm(float *stamp, float delay);
qboolean SG_TimerPending(float stamp);

#endif /* SG_BOT_UTIL_H */
