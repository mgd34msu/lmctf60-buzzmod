#include <assert.h>

#include "g_local.h"
#include "g_ctffunc.h"
#include "slipgate/sg_util.h"

edict_t *redflag;
edict_t *blueflag;

static edict_t *held_flag;
static edict_t *holder;

edict_t *ClientHasFlag(edict_t *ent)
{
	return ent == holder && ent->inuse && ent->client ? held_flag : NULL;
}

static void ConfigurePlayer(edict_t *player, gclient_t *client, int team)
{
	memset(player, 0, sizeof(*player));
	memset(client, 0, sizeof(*client));
	player->inuse = true;
	player->health = 100;
	player->client = client;
	client->ctf.teamnum = team;
}

int main(void)
{
	edict_t flag, different_flag, former, other;
	gclient_t former_client, other_client;

	memset(&flag, 0, sizeof(flag));
	memset(&different_flag, 0, sizeof(different_flag));
	flag.inuse = true;
	redflag = &flag;
	blueflag = NULL;
	ConfigurePlayer(&former, &former_client, CTF_TEAM_BLUE);
	ConfigurePlayer(&other, &other_client, CTF_TEAM_BLUE);

	flag.owner = &former;
	holder = &former;
	held_flag = NULL;
	assert(SG_FlagCarrier(&flag) == NULL);
	assert(SG_EnemyFlag(CTF_TEAM_BLUE) == &flag);
	assert(!SG_FlagApproachAvailableTo(&flag, &former));
	assert(SG_FlagApproachAvailableTo(&flag, &other));

	held_flag = &different_flag;
	assert(SG_FlagCarrier(&flag) == NULL);
	assert(!SG_FlagApproachAvailableTo(&flag, &former));
	assert(SG_FlagApproachAvailableTo(&flag, &other));

	held_flag = &flag;
	assert(SG_FlagCarrier(&flag) == &former);
	assert(SG_EnemyFlag(CTF_TEAM_BLUE) == NULL);
	assert(!SG_FlagApproachAvailableTo(&flag, &former));
	assert(!SG_FlagApproachAvailableTo(&flag, &other));

	held_flag = NULL;
	former.inuse = false;
	assert(SG_FlagCarrier(&flag) == NULL);
	assert(SG_FlagApproachAvailableTo(&flag, &other));

	flag.owner = NULL;
	assert(SG_FlagApproachAvailableTo(&flag, &other));
	assert(!SG_FlagApproachAvailableTo(&flag, NULL));
	flag.inuse = false;
	assert(SG_EnemyFlag(CTF_TEAM_BLUE) == NULL);
	assert(!SG_FlagApproachAvailableTo(&flag, &other));
	return 0;
}
