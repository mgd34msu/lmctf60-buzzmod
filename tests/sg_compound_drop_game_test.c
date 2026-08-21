/* Exact edict-to-controller boundary fixture for production D_DROP. */
#include "g_local.h"

#include <stdio.h>
#include <string.h>

#include "slipgate/sg_local.h"
#include "slipgate/sg_bot.h"
#include "slipgate/sg_compound_drop_game.h"

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		        __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static void TestAuthoritativePoseCapture(void)
{
	edict_t entity;
	gclient_t client;
	edict_t support;
	sg_replay_pose_t pose;

	memset(&entity, 0, sizeof(entity));
	memset(&client, 0, sizeof(client));
	memset(&support, 0, sizeof(support));
	memset(&pose, 0x5a, sizeof(pose));
	entity.inuse = true;
	entity.client = &client;
	entity.groundentity = &support;
	VectorSet(entity.s.origin, 12.0f, -4.0f, 96.0f);
	VectorSet(entity.velocity, 80.0f, 16.0f, -120.0f);
	entity.watertype = CONTENTS_WATER;
	entity.waterlevel = 1;
	client.ps.pmove.pm_type = PM_NORMAL;
	client.ps.pmove.origin[0] = 96;
	client.ps.pmove.origin[1] = -32;
	client.ps.pmove.origin[2] = 768;
	client.ps.pmove.velocity[0] = 640;
	client.ps.pmove.velocity[1] = 128;
	client.ps.pmove.velocity[2] = -960;
	CHECK(SG_CompoundDropGamePose(&entity, &pose));
	CHECK(pose.pms.pm_type == PM_NORMAL);
	CHECK(pose.pms.origin[0] == 96 && pose.pms.origin[1] == -32 &&
	      pose.pms.origin[2] == 768);
	CHECK(pose.origin[0] == 12.0f && pose.origin[1] == -4.0f &&
	      pose.origin[2] == 96.0f);
	CHECK(pose.velocity[0] == 80.0f && pose.velocity[1] == 16.0f &&
	      pose.velocity[2] == -120.0f);
	CHECK(pose.grounded && pose.watertype == CONTENTS_WATER &&
	      pose.waterlevel == 1);
	entity.inuse = false;
	CHECK(!SG_CompoundDropGamePose(&entity, &pose));
}

static void TestBotStorageStartsUnowned(void)
{
	sg_bot_t bot;

	memset(&bot, 0, sizeof(bot));
	bot.compound_drop_live.drop_link = -1;
	CHECK(!bot.compound_drop_live.guard_owned);
	CHECK(!bot.compound_drop_live.command_pending);
	CHECK(bot.compound_drop_live.drop_link == -1);
	CHECK(bot.compound_drop_live.outer.phase == SG_COMPOUND_NONE);
}

int main(void)
{
	TestAuthoritativePoseCapture();
	TestBotStorageStartsUnowned();
	if (failures)
	{
		fprintf(stderr, "compound_drop_game_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("compound_drop_game_test: ok");
	return 0;
}
