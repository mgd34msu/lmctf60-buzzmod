/* sg_util.h -- team, flag, sight, and timer helpers. */
#ifndef SG_UTIL_H
#define SG_UTIL_H

int SG_TeamIdx(int team);          /* CTF_TEAM_RED -> 0, CTF_TEAM_BLUE -> 1 */
int SG_TeamFromIdx(int idx);
int SG_EnemyTeam(int team);

edict_t *SG_FlagCarrier(edict_t *flag);   /* the client carrying this flag, or NULL */
edict_t *SG_OwnFlag(int team);
edict_t *SG_EnemyFlag(int team);
edict_t *SG_FlagStand(int team, qboolean own);

float SG_DistXY(const vec3_t a, const vec3_t b);
qboolean SG_CanSee(edict_t *e, const vec3_t pt, float lift_z);

/* level.time stamps */
void SG_TimerArm(float *stamp, float delay);
qboolean SG_TimerReady(float stamp);
qboolean SG_TimerReadyStrict(float stamp);
qboolean SG_TimerPending(float stamp);
float SG_TimerRemaining(float stamp);
void SG_Mark(float *stamp);
float SG_Age(float since);
qboolean SG_AgeOver(float since, float span);
qboolean SG_AgeAtLeast(float since, float span);
qboolean SG_AgeUnder(float since, float span);

#endif /* SG_UTIL_H */
