#include "g_local.h"
#include "g_ctffunc.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_bot.h"
#include "slipgate/sg_cvars.h"
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_lead.h"
#include "slipgate/sg_item_policy.h"
#include "slipgate/sg_item_route.h"
#include "slipgate/sg_rune_handoff_policy.h"
#include "slipgate/sg_persona.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int failures;
static edict_t entities[4];
static gclient_t clients[2];
static rune_seed_t seeds[2];
static rune_t test_rune;
static cvar_t itemlead_cvar;
static cvar_t debug_cvar;
static qboolean itemcomm = true;
static qboolean combat;
static qboolean hurt;
static qboolean accept_powerup = true;

game_locals_t game;
level_locals_t level;
game_export_t globals;
edict_t *g_edicts = entities;
sg_bot_t sg_bots[SG_MAXBOTS];
sg_belief_item_t sg_caco_items[2][SG_MAX_BELIEF_ITEMS];
int sg_caco_num_items;
sg_team_belief_t sg_caco_team_belief;
sg_cvars_t sg_cv;
sg_host_t sg_host;

#define CHECK(condition) do { \
	if (!(condition)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, \
		    #condition); \
		failures++; \
	} \
} while (0)

static void TestDprint(const char *fmt, ...)
{
	(void)fmt;
}

qboolean SG_ItemComm(void)
{
	return itemcomm;
}

rune_t *SG_Rune(void)
{
	return &test_rune;
}

int SG_TeamIdx(int team)
{
	return team == CTF_TEAM_BLUE ? 1 : 0;
}

int Rune_NearestSeed(rune_t *r, vec3_t point)
{
	(void)r;
	(void)point;
	return 1;
}

void Field_Flood(rune_t *r, int *dist, const int *sources,
	const int *source_cost, int count)
{
	int i;

	(void)source_cost;
	for (i = 0; i < r->hdr.num_seeds; i++)
		dist[i] = SG_FIELD_INF;
	if (count == 1 && sources[0] >= 0 && sources[0] < r->hdr.num_seeds)
	{
		dist[sources[0]] = 0;
		dist[0] = 100;
	}
}

qboolean SG_CombatDuel(edict_t *self, vec3_t enemy_org, float *want_range,
	float *exposure_w)
{
	(void)self;
	(void)enemy_org;
	(void)want_range;
	(void)exposure_w;
	return combat;
}

qboolean Beat_HurtSince(edict_t *ent, float since)
{
	(void)ent;
	(void)since;
	return hurt;
}

qboolean G_PowerupPickupEligible(edict_t *item, edict_t *other)
{
	(void)item;
	(void)other;
	return accept_powerup;
}

const sg_persona_t *SG_PersonaFor(edict_t *ent)
{
	static const sg_persona_t persona = { "test", 0, 1.0f, 1.0f, 1.0f,
	    0.5f, 1.0f };

	(void)ent;
	return &persona;
}

void SG_TimerArm(float *stamp, float delay)
{
	*stamp = level.time + delay;
}

qboolean SG_TimerReady(float stamp)
{
	return level.time >= stamp;
}

qboolean SG_TimerReadyStrict(float stamp)
{
	return level.time > stamp;
}

qboolean SG_TimerPending(float stamp)
{
	return level.time < stamp;
}

float SG_TimerRemaining(float stamp)
{
	return stamp - level.time;
}

void SG_Mark(float *stamp)
{
	*stamp = level.time;
}

vec_t VectorLength(vec3_t value)
{
	return sqrtf(value[0] * value[0] + value[1] * value[1] +
	    value[2] * value[2]);
}

static sg_bot_t *ResetLead(int state, float seen_watermark)
{
	sg_bot_t *bot;
	sg_belief_item_t *belief;

	memset(sg_bots, 0, sizeof(sg_bots));
	memset(sg_caco_items, 0, sizeof(sg_caco_items));
	memset(&sg_caco_team_belief, 0, sizeof(sg_caco_team_belief));
	sg_caco_team_belief.carrier[0].client = -1;
	sg_caco_team_belief.carrier[1].client = -1;
	sg_caco_num_items = 1;
	bot = &sg_bots[0];
	bot->active = true;
	bot->ent = &entities[1];
	bot->seed = 0;
	bot->commit_link = -1;
	bot->tac_seed = 7;
	bot->lead_ent = 3;
	bot->lead_slot = 0;
	bot->lead_seed = 1;
	bot->lead_at = 10.0f;
	bot->lead_since = 1.0f;
	bot->lead_state = state;
	bot->lead_seen_up_at = seen_watermark;
	belief = &sg_caco_items[0][0];
	belief->ent = 3;
	belief->cls = SG_BI_POWERUP;
	belief->claimed_by = 0;
	belief->claimed_until = level.time + SG_LEAD_LEASE;
	belief->believed_respawn_time = 10.0f;
	return bot;
}

static void ResetWorld(void)
{
	memset(entities, 0, sizeof(entities));
	memset(clients, 0, sizeof(clients));
	memset(&test_rune, 0, sizeof(test_rune));
	memset(&itemlead_cvar, 0, sizeof(itemlead_cvar));
	memset(&debug_cvar, 0, sizeof(debug_cvar));
	memset(&sg_cv, 0, sizeof(sg_cv));
	memset(&sg_host, 0, sizeof(sg_host));
	game.maxclients = 2;
	globals.num_edicts = 4;
	entities[1].inuse = true;
	entities[1].client = &clients[0];
	entities[1].client->ctf.teamnum = CTF_TEAM_RED;
	entities[2].inuse = true;
	entities[2].client = &clients[1];
	entities[2].client->ctf.teamnum = CTF_TEAM_BLUE;
	entities[3].inuse = true;
	entities[3].classname = "item_quad";
	VectorSet(entities[3].s.origin, 64.0f, 96.0f, 24.0f);
	test_rune.hdr.num_seeds = 2;
	test_rune.seeds = seeds;
	itemlead_cvar.value = 1.0f;
	sg_cv.itemlead = &itemlead_cvar;
	sg_cv.debug = &debug_cvar;
	sg_host.dprint = TestDprint;
	itemcomm = true;
	combat = false;
	hurt = false;
	accept_powerup = true;
}

static void TestClockSpawnContinuesToPhysicalPickup(void)
{
	sg_bot_t *bot;
	sg_belief_item_t *belief;
	const int *field;

	level.time = 10.0f;
	bot = ResetLead(SG_LEAD_WAITING, 5.0f);
	belief = &sg_caco_items[0][0];
	belief->believed_up = true;
	belief->seen_up_time = 5.0f;
	belief->believed_respawn_time = 0.0f;
	field = Lead_Field(bot, SG_ROLE_ATTACK, false, -1);
	CHECK(field != NULL);
	CHECK(bot->lead_ent == 3);
	CHECK(bot->lead_state == SG_LEAD_SPAWNED);
	CHECK(fabsf(bot->lead_inferred_until - 12.0f) < 0.001f);
	CHECK(bot->tac_seed == -1);

	level.time = 12.1f;
	CHECK(Lead_Field(bot, SG_ROLE_ATTACK, false, -1) == NULL);
	CHECK(bot->lead_ent == 0);
}

static void TestSightConfirmedSpawnPersistsAndHomes(void)
{
	sg_bot_t *bot;
	sg_belief_item_t *belief;
	vec3_t target;

	level.time = 10.0f;
	bot = ResetLead(SG_LEAD_WAITING, 5.0f);
	belief = &sg_caco_items[0][0];
	belief->believed_up = true;
	belief->seen_up_time = 6.0f;
	belief->believed_respawn_time = 0.0f;
	CHECK(Lead_Field(bot, SG_ROLE_ATTACK, false, -1) != NULL);
	CHECK(bot->lead_state == SG_LEAD_SPAWNED);
	CHECK(bot->lead_inferred_until == 0.0f);
	CHECK(Lead_PickupTarget(bot, target));
	CHECK(target[0] == entities[3].s.origin[0]);
	CHECK(target[1] == entities[3].s.origin[1]);
	CHECK(target[2] == entities[3].s.origin[2]);

	level.time = 15.0f;
	CHECK(Lead_Field(bot, SG_ROLE_ATTACK, false, -1) != NULL);
	CHECK(bot->lead_ent == 3);
	belief->believed_up = false;
	CHECK(Lead_Field(bot, SG_ROLE_ATTACK, false, -1) == NULL);
	CHECK(bot->lead_ent == 0);
}

static void TestExactPickupOwnershipEndsCommitment(void)
{
	sg_bot_t *bot;
	sg_belief_item_t *belief;

	level.time = 10.0f;
	bot = ResetLead(SG_LEAD_SPAWNED, 6.0f);
	belief = &sg_caco_items[0][0];
	Lead_NoteItemTaken(&entities[1], &entities[3]);
	CHECK(bot->lead_ent == 0);
	CHECK(belief->claimed_by == -1);
	CHECK(belief->claimed_until == 0.0f);

	bot = ResetLead(SG_LEAD_SPAWNED, 6.0f);
	Lead_NoteItemTaken(&entities[2], &entities[3]);
	CHECK(bot->lead_ent == 0);
	CHECK(sg_caco_items[0][0].claimed_by == -1);

	bot = ResetLead(SG_LEAD_SPAWNED, 6.0f);
	entities[3].classname = "item_armor_body";
	CHECK(!Lead_PickupTarget(bot, entities[0].s.origin));
	CHECK(bot->lead_ent == 3);
}

static void TestRejectedTouchEndsOnlyExactOwner(void)
{
	sg_bot_t *bot;

	level.time = 10.0f;
	bot = ResetLead(SG_LEAD_SPAWNED, 6.0f);
	Lead_NoteItemRejected(&entities[1], &entities[3]);
	CHECK(bot->lead_ent == 0);
	CHECK(sg_caco_items[0][0].claimed_by == -1);

	bot = ResetLead(SG_LEAD_SPAWNED, 6.0f);
	Lead_NoteItemRejected(&entities[2], &entities[3]);
	CHECK(bot->lead_ent == 3);
	CHECK(sg_caco_items[0][0].claimed_by == 0);
}

static void TestStrongerInterruptsStillWin(void)
{
	sg_bot_t *bot;

	level.time = 10.0f;
	bot = ResetLead(SG_LEAD_SPAWNED, 6.0f);
	combat = true;
	CHECK(Lead_Field(bot, SG_ROLE_ATTACK, false, -1) == NULL);
	CHECK(bot->lead_ent == 0);
	combat = false;

	bot = ResetLead(SG_LEAD_SPAWNED, 6.0f);
	CHECK(Lead_Field(bot, SG_ROLE_ATTACK, true, -1) == NULL);
	CHECK(bot->lead_ent == 0);

	bot = ResetLead(SG_LEAD_SPAWNED, 6.0f);
	CHECK(Lead_Field(bot, SG_ROLE_ESCORT, false, -1) == NULL);
	CHECK(bot->lead_ent == 0);

	bot = ResetLead(SG_LEAD_SPAWNED, 6.0f);
	CHECK(Lead_Field(bot, SG_ROLE_ATTACK, false, SG_ROLE_ATTACK) == NULL);
	CHECK(bot->lead_ent == 0);
}

static void TestPowerupCapacityEndsTheErrand(void)
{
	sg_bot_t *bot;

	level.time = 10.0f;
	bot = ResetLead(SG_LEAD_SPAWNED, 6.0f);
	accept_powerup = false;
	CHECK(Lead_Field(bot, SG_ROLE_ATTACK, false, -1) == NULL);
	CHECK(bot->lead_ent == 0);
	CHECK(sg_caco_items[0][0].claimed_by == -1);

	bot = ResetLead(SG_LEAD_SPAWNED, 6.0f);
	CHECK(!Lead_PickupTarget(bot, entities[0].s.origin));
	CHECK(bot->lead_ent == 3);
}

int main(void)
{
	CHECK(SG_IdentityItemRouteAdmission(SG_FC_POWERUP, true));
	CHECK(!SG_IdentityItemRouteAdmission(SG_FC_POWERUP, false));
	CHECK(SG_IdentityItemRouteAdmission(SG_FC_RUNE, true));
	CHECK(!SG_IdentityItemRouteAdmission(SG_FC_RUNE, false));
	CHECK(!SG_IdentityItemRouteAdmission(SG_FC_WEAPON, true));
	CHECK(!SG_IdentityItemRouteAdmission(SG_FC_POWERUP, 2));
	CHECK(SG_IdentityItemBeliefAdmission(SG_FC_POWERUP, true, false));
	CHECK(SG_IdentityItemBeliefAdmission(SG_FC_POWERUP, false, true));
	CHECK(!SG_IdentityItemBeliefAdmission(SG_FC_POWERUP, false, false));
	CHECK(SG_IdentityItemBeliefAdmission(SG_FC_RUNE, true, false));
	CHECK(!SG_IdentityItemBeliefAdmission(SG_FC_RUNE, false, true));
	CHECK(!SG_IdentityItemBeliefAdmission(SG_FC_WEAPON, true, true));
	CHECK(!SG_IdentityItemBeliefAdmission(SG_FC_POWERUP, 2, false));
	CHECK(SG_RuneHandoffEligible(SG_ROLE_ATTACK, false, -1, false, false));
	CHECK(SG_RuneHandoffEligible(SG_ROLE_ESCORT, false, -1, false, false));
	CHECK(!SG_RuneHandoffEligible(SG_ROLE_RECOVER, false, -1, false, false));
	CHECK(!SG_RuneHandoffEligible(SG_ROLE_DEFEND, false, -1, false, false));
	CHECK(!SG_RuneHandoffEligible(SG_ROLE_CARRY, true, -1, false, false));
	CHECK(!SG_RuneHandoffEligible(SG_ROLE_ATTACK, false, SG_ROLE_ATTACK,
	    false, false));
	CHECK(!SG_RuneHandoffEligible(SG_ROLE_ESCORT, false, SG_ROLE_ESCORT,
	    false, false));
	CHECK(SG_RuneHandoffEligible(SG_ROLE_ATTACK, false, -1, true, true));
	CHECK(!SG_RuneHandoffEligible(SG_ROLE_ESCORT, false, -1, true, false));
	CHECK(!SG_RuneHandoffEligible(SG_ROLE_ATTACK, false, -1, true, false));
	CHECK(!SG_RuneHandoffEligible(SG_ROLE_ATTACK, false, -1, 2, true));
	CHECK(!SG_RuneHandoffEligible(SG_ROLE_ATTACK, false, -1, true, 2));
	CHECK(SG_RuneHandoffCarrierAllowed(CTF_TEAM_RED, 16, 3, true, true,
	    100, false, CTF_TEAM_RED, true, false));
	CHECK(!SG_RuneHandoffCarrierAllowed(CTF_TEAM_RED, 16, -1, true, true,
	    100, false, CTF_TEAM_RED, true, false));
	CHECK(!SG_RuneHandoffCarrierAllowed(CTF_TEAM_RED, 16, 16, true, true,
	    100, false, CTF_TEAM_RED, true, false));
	CHECK(!SG_RuneHandoffCarrierAllowed(CTF_TEAM_RED, 16, 3, false, true,
	    100, false, CTF_TEAM_RED, true, false));
	CHECK(!SG_RuneHandoffCarrierAllowed(CTF_TEAM_RED, 16, 3, true, false,
	    100, false, CTF_TEAM_RED, true, false));
	CHECK(!SG_RuneHandoffCarrierAllowed(CTF_TEAM_RED, 16, 3, true, true,
	    0, false, CTF_TEAM_RED, true, false));
	CHECK(!SG_RuneHandoffCarrierAllowed(CTF_TEAM_RED, 16, 3, true, true,
	    100, true, CTF_TEAM_RED, true, false));
	CHECK(!SG_RuneHandoffCarrierAllowed(CTF_TEAM_RED, 16, 3, true, true,
	    100, false, CTF_TEAM_BLUE, true, false));
	CHECK(!SG_RuneHandoffCarrierAllowed(CTF_TEAM_RED, 16, 3, true, true,
	    100, false, CTF_TEAM_RED, false, false));
	CHECK(!SG_RuneHandoffCarrierAllowed(CTF_TEAM_RED, 16, 3, 2, true,
	    100, false, CTF_TEAM_RED, true, false));
	{
		float yaw = 999.0f;

		CHECK(SG_RuneHandoffAim(1.0f, 0.0f, &yaw));
		CHECK(fabsf(yaw) < 0.001f);
		CHECK(SG_RuneHandoffAim(0.0f, 1.0f, &yaw));
		CHECK(fabsf(yaw - 90.0f) < 0.001f);
		CHECK(SG_RuneHandoffAim(-1.0f, 0.0f, &yaw));
		CHECK(fabsf(fabsf(yaw) - 180.0f) < 0.001f);
		CHECK(!SG_RuneHandoffAim(0.0f, 0.0f, &yaw));
		CHECK(!SG_RuneHandoffAim(NAN, 1.0f, &yaw));
		CHECK(!SG_RuneHandoffAim(1.0f, INFINITY, &yaw));
		CHECK(!SG_RuneHandoffAim(1.0f, 1.0f, NULL));
	}
	CHECK(SG_RuneHandoffTossPathAllowed(128.0f, true));
	CHECK(!SG_RuneHandoffTossPathAllowed(128.0f, false));
	CHECK(!SG_RuneHandoffTossPathAllowed(0.0f, true));
	CHECK(!SG_RuneHandoffTossPathAllowed(400.0f, true));
	CHECK(!SG_RuneHandoffTossPathAllowed(NAN, true));
	CHECK(!SG_RuneHandoffTossPathAllowed(INFINITY, true));
	CHECK(!SG_RuneHandoffTossPathAllowed(128.0f, 2));
	CHECK(!SG_RuneHandoffCarrierAllowed(CTF_TEAM_RED, 16, 3, true, true,
	    100, false, CTF_TEAM_RED, true, true));
	CHECK(SG_ItemPickupDisposition(1, 0, 1, 0) ==
	      SG_ITEM_PICKUP_COMMIT_ONLY);
	CHECK(SG_ItemPickupDisposition(1, 0, 1, 1) ==
	      SG_ITEM_PICKUP_COMMIT_AND_COMMUNICATE);
	CHECK(SG_ItemPickupDisposition(0, 0, 1, 1) == SG_ITEM_PICKUP_IGNORE);
	CHECK(SG_ItemPickupDisposition(1, 1, 1, 1) == SG_ITEM_PICKUP_IGNORE);
	CHECK(SG_ItemPickupDisposition(1, 0, 0, 1) == SG_ITEM_PICKUP_IGNORE);
	ResetWorld();
	TestClockSpawnContinuesToPhysicalPickup();
	TestSightConfirmedSpawnPersistsAndHomes();
	TestExactPickupOwnershipEndsCommitment();
	TestRejectedTouchEndsOnlyExactOwner();
	TestStrongerInterruptsStillWin();
	TestPowerupCapacityEndsTheErrand();
	if (failures)
	{
		fprintf(stderr, "%d sg_item_commitment tests failed\n", failures);
		return 1;
	}
	puts("sg_item_commitment_test: ok");
	return 0;
}
