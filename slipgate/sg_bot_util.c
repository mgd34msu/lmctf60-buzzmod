#include "../g_local.h"
#include "../g_ctffunc.h"

#include "sg_bot_util.h"

#include <math.h>

int SG_TeamIdx(int team)
{
	if (team == CTF_TEAM_RED)
		return 0;
	if (team == CTF_TEAM_BLUE)
		return 1;
	return -1;
}

int SG_TeamFromIdx(int idx)
{
	return idx == 0 ? CTF_TEAM_RED : idx == 1 ? CTF_TEAM_BLUE : CTF_TEAM_UNDEFINED;
}

int SG_EnemyTeam(int team)
{
	if (team == CTF_TEAM_RED)
		return CTF_TEAM_BLUE;
	if (team == CTF_TEAM_BLUE)
		return CTF_TEAM_RED;
	return CTF_TEAM_UNDEFINED;
}

float SG_DistXY(const vec3_t a, const vec3_t b)
{
	float dx = a[0] - b[0], dy = a[1] - b[1];

	return sqrtf(dx * dx + dy * dy);
}

void SG_TimerArm(float *stamp, float delay)
{
	if (stamp)
		*stamp = level.time + delay;
}

qboolean SG_TimerPending(float stamp)
{
	return level.time < stamp;
}
