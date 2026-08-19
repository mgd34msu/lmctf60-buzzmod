#ifndef SG_TEAM_COLLISION_H
#define SG_TEAM_COLLISION_H

/* Only a live body on the mover's current team receives the teammate
 * pass-through policy. Enemy bodies remain physical blockers even during a
 * reaction-delay frame in which combat has not yet claimed command authority. */
static int
SG_TeammateBodyPassable(int mover_team, int body_has_client, int body_dead,
    int body_team)
{
	if ((mover_team != 1 && mover_team != 2) ||
	    (body_has_client != 0 && body_has_client != 1) ||
	    (body_dead != 0 && body_dead != 1))
		return 0;
	return body_has_client && !body_dead && body_team == mover_team;
}

#endif
