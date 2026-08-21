#include <stdio.h>
#include <string.h>

#include "g_local.h"
#include "g_ctffunc.h"
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_local.h"

qboolean Caco_EnemyObservationValid(const rune_t *r, int team_index,
	int client, int maxclients, int seed);
void Caco_EnemyPlace(rune_t *r, int team_index, int client, int seed,
	qboolean seen, qboolean runed);

static edict_t test_edicts[4];
static gclient_t test_clients[3];

qboolean SG_OwnsBot(edict_t *entity)
{
	return entity == &test_edicts[2];
}

static qboolean NeverInPvs(const vec3_t first, const vec3_t second)
{
	(void)first;
	(void)second;
	return false;
}

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
		        __FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static void ArmHit(int victim, int slot, int attacker, float time,
	const vec3_t from)
{
	sg_damage_hit_t *hit = &sg_caco_damage[victim][slot];

	memset(hit, 0, sizeof(*hit));
	hit->landed = true;
	hit->attacker = attacker;
	hit->damage = 25;
	hit->time = time;
	hit->unseen = true;
	VectorCopy(from, hit->from);
}

static sg_damage_hit_t *HitAt(int victim, float time)
{
	int slot;

	for (slot = 0; slot < SG_DMG_RING; slot++)
		if (sg_caco_damage[victim][slot].landed &&
		    sg_caco_damage[victim][slot].time == time)
			return &sg_caco_damage[victim][slot];
	return NULL;
}

static void CheckAnonymousImpact(float time)
{
	sg_damage_hit_t *hit = HitAt(1, time);
	int slot;

	CHECK(hit && hit->attacker == -1 && !hit->unseen);
	CHECK(hit && hit->damage == 40 && hit->mod == MOD_ROCKET);
	CHECK(hit && VectorLength(hit->from) == 0.0f);
	CHECK(SG_HurtSince(&test_edicts[2], time - 0.5f));
	CHECK(!SG_RecentUnseenHit(&test_edicts[2], 0.5f, NULL));
	for (slot = 0; slot < SG_MAX_ENEMY_TRACK; slot++)
		CHECK(sg_caco_enemies[0][slot].client != 0);
}

static void CheckEnemyObservationRetirement(void)
{
	rune_t rune;
	rune_seed_t seeds[2];
	int team, slot;

	memset(&rune, 0, sizeof(rune));
	memset(seeds, 0, sizeof(seeds));
	rune.hdr.num_seeds = 2;
	rune.seeds = seeds;
	game.maxclients = 16;
	for (team = 0; team < 2; team++)
		for (slot = 0; slot < SG_MAX_ENEMY_TRACK; slot++)
		{
			sg_caco_enemies[team][slot].client = -1;
			sg_caco_enemies[team][slot].seed = -1;
		}

	level.time = 10.0f;
	Caco_EnemyPlace(&rune, 0, 7, 1, true, true);
	CHECK(sg_caco_enemies[0][0].client == 7);
	CHECK(sg_caco_enemies[0][0].seed == 1);
	CHECK(sg_caco_enemies[0][0].runed);
	CHECK(sg_caco_enemies[0][0].seen_time == 10.0f);

	level.time = 11.0f;
	Caco_EnemyPlace(&rune, 0, 7, -1, true, false);
	CHECK(sg_caco_enemies[0][0].client == -1);
	CHECK(sg_caco_enemies[0][0].seed == -1);
	CHECK(sg_caco_enemies[0][0].seen_time == 0.0f);
	CHECK(!sg_caco_enemies[0][0].runed);

	Caco_EnemyPlace(&rune, 1, 3, 0, false, false);
	CHECK(sg_caco_enemies[1][0].client == 3);
	Caco_EnemyPlace(&rune, 2, 3, -1, true, false);
	Caco_EnemyPlace(&rune, 1, 16, -1, true, false);
	CHECK(sg_caco_enemies[1][0].client == 3);
}

static void CheckEnemyObservationValidation(void)
{
	rune_t rune;
	rune_seed_t seeds[5];

	memset(&rune, 0, sizeof(rune));
	memset(seeds, 0, sizeof(seeds));
	rune.hdr.num_seeds = 5;
	rune.seeds = seeds;
	CHECK(Caco_EnemyObservationValid(&rune, 0, 0, 16, 0));
	CHECK(Caco_EnemyObservationValid(&rune, 1, 15, 16, 4));
	CHECK(!Caco_EnemyObservationValid(&rune, 0, 0, 16, -1));
	CHECK(!Caco_EnemyObservationValid(&rune, 0, 0, 16, 5));
	CHECK(!Caco_EnemyObservationValid(&rune, -1, 0, 16, 0));
	CHECK(!Caco_EnemyObservationValid(&rune, 2, 0, 16, 0));
	CHECK(!Caco_EnemyObservationValid(&rune, 0, -1, 16, 0));
	CHECK(!Caco_EnemyObservationValid(&rune, 0, 16, 16, 0));
	CHECK(!Caco_EnemyObservationValid(&rune, 0, 0, 0, 0));
	CHECK(!Caco_EnemyObservationValid(NULL, 0, 0, 16, 0));
}

static void CheckProjectileGenerationRetirement(void)
{
	edict_t projectile;
	edict_t source;
	vec3_t direction = { 1.0f, 0.0f, 0.0f };
	sg_damage_hit_t *hit;

	memset(&projectile, 0, sizeof(projectile));
	memset(&source, 0, sizeof(source));
	test_edicts[1].inuse = true;
	test_edicts[1].health = 100;
	test_edicts[1].client->ctf.teamnum = CTF_TEAM_BLUE;
	test_edicts[1].client->ctf.ctfid = 20;
	test_edicts[2].inuse = true;
	test_edicts[2].flags = FL_BOT;
	test_edicts[2].health = 100;
	test_edicts[2].client->ctf.teamnum = CTF_TEAM_RED;
	sg_host.in_pvs = NeverInPvs;
	G_ProjectileOwnerSet(&projectile, &test_edicts[1]);
	CHECK(projectile.owner == &test_edicts[1]);
	CHECK(projectile.projectile_owner_ctfid == 20);
	CHECK(G_DamageAttackerCtfid(&source, &test_edicts[1]) == 20);

	Caco_ResetClient(&test_edicts[1]);
	test_edicts[1].client->ctf.ctfid = 21;
	level.time = 12.0f;
	CHECK(G_DamageAttackerCtfid(&projectile, &test_edicts[1]) == 20);
	CHECK(G_DamageAttackerCtfid(&source, &test_edicts[1]) == 21);
	CHECK(G_DamageAttackerCtfid(&test_edicts[1], &test_edicts[1]) == 21);
	SG_NoteDamage(&test_edicts[2], &test_edicts[1],
	    G_DamageAttackerCtfid(&projectile, &test_edicts[1]),
	    40, MOD_ROCKET, direction);
	CheckAnonymousImpact(12.0f);

	G_ProjectileOwnerSet(&projectile, &test_edicts[1]);
	test_edicts[1].health = 0;
	test_edicts[1].deadflag = DEAD_DEAD;
	level.time = 13.0f;
	SG_NoteDamage(&test_edicts[2], &test_edicts[1],
	    G_DamageAttackerCtfid(&projectile, &test_edicts[1]),
	    40, MOD_ROCKET, direction);
	CheckAnonymousImpact(13.0f);

	test_edicts[1].health = 100;
	test_edicts[1].deadflag = DEAD_NO;
	Caco_ResetClient(&test_edicts[1]);
	memset(&projectile, 0, sizeof(projectile));
	projectile.owner = &test_edicts[1];
	level.time = 14.0f;
	CHECK(G_DamageAttackerCtfid(&projectile, &test_edicts[1]) == 0);
	SG_NoteDamage(&test_edicts[2], &test_edicts[1], 0,
	    40, MOD_ROCKET, direction);
	CheckAnonymousImpact(14.0f);

	Caco_ResetClient(&test_edicts[1]);
	G_ProjectileOwnerSet(&projectile, &test_edicts[3]);
	projectile.projectile_owner_ctfid = test_edicts[1].client->ctf.ctfid;
	level.time = 15.0f;
	CHECK(G_DamageAttackerCtfid(&projectile, &test_edicts[1]) == 0);
	SG_NoteDamage(&test_edicts[2], &test_edicts[1], 0,
	    40, MOD_ROCKET, direction);
	CheckAnonymousImpact(15.0f);

	Caco_ResetClient(&test_edicts[1]);
	G_ProjectileOwnerSet(&projectile, &test_edicts[1]);
	test_edicts[1].inuse = false;
	level.time = 16.0f;
	SG_NoteDamage(&test_edicts[2], &test_edicts[1],
	    G_DamageAttackerCtfid(&projectile, &test_edicts[1]),
	    40, MOD_ROCKET, direction);
	CheckAnonymousImpact(16.0f);

	test_edicts[1].inuse = true;
	level.time = 17.0f;
	SG_NoteDamage(&test_edicts[2], &test_edicts[1],
	    G_DamageAttackerCtfid(&projectile, &test_edicts[1]),
	    40, MOD_ROCKET, direction);
	CHECK(SG_RecentUnseenHit(&test_edicts[2], 1.0f, NULL));
	hit = HitAt(1, 17.0f);
	CHECK(hit && hit->attacker == 0 && hit->unseen);
}

int SG_CacoLifecycleTest(void)
{
	edict_t *saved_edicts = g_edicts;
	gclient_t *saved_clients = game.clients;
	sg_host_t saved_host = sg_host;
	float saved_time = level.time;
	int saved_maxclients = game.maxclients;
	vec3_t from = { 0.0f, 0.0f, 0.0f };
	vec3_t dead_from = { 1.0f, 0.0f, 0.0f };
	vec3_t live_from = { 0.0f, 1.0f, 0.0f };
	int victim_slot = 1;

	failures = 0;
	CheckEnemyObservationRetirement();
	CheckEnemyObservationValidation();
	memset(test_edicts, 0, sizeof(test_edicts));
	memset(test_clients, 0, sizeof(test_clients));
	memset(sg_caco_damage, 0, sizeof(sg_caco_damage));
	memset(&sg_caco_team_belief, 0, sizeof(sg_caco_team_belief));
	game.clients = test_clients;
	g_edicts = test_edicts;
	level.time = 10.0f;
	test_edicts[1].client = &test_clients[0];
	test_edicts[2].client = &test_clients[victim_slot];
	test_edicts[3].client = &test_clients[2];
	sg_caco_team_belief.carrier[0].client = -1;
	sg_caco_team_belief.carrier[1].client = -1;
	sg_caco_team_belief.enemy_carrier[0].client = -1;
	sg_caco_team_belief.enemy_carrier[1].client = -1;

	ArmHit(0, 0, 2, 9.75f, live_from);
	ArmHit(victim_slot, 0, 0, 9.5f, dead_from);
	ArmHit(victim_slot, 1, 2, 9.0f, live_from);
	ArmHit(2, 0, 0, 9.25f, dead_from);
	CHECK(SG_RecentUnseenHit(&test_edicts[2], 2.0f, from));
	CHECK(from[0] == 1.0f);

	Caco_ResetClient(&test_edicts[1]);

	CHECK(!SG_RecentUnseenHit(&test_edicts[1], 2.0f, NULL));
	CHECK(!SG_HurtSince(&test_edicts[1], 8.0f));
	CHECK(!sg_caco_damage[0][0].landed);
	CHECK(SG_HurtSince(&test_edicts[2], 8.0f));
	CHECK(SG_HurtSince(&test_edicts[3], 8.0f));
	CHECK(!SG_RecentUnseenHit(&test_edicts[3], 2.0f, NULL));
	CHECK(sg_caco_damage[victim_slot][0].attacker == -1);
	CHECK(sg_caco_damage[victim_slot][0].landed);
	CHECK(sg_caco_damage[victim_slot][0].damage == 25);
	CHECK(sg_caco_damage[victim_slot][0].time == 9.5f);
	CHECK(!sg_caco_damage[victim_slot][0].unseen);
	CHECK(VectorLength(sg_caco_damage[victim_slot][0].from) == 0.0f);
	CHECK(sg_caco_damage[victim_slot][1].attacker == 2);
	CHECK(SG_RecentUnseenHit(&test_edicts[2], 2.0f, from));
	CHECK(from[0] == 0.0f && from[1] == 1.0f);
	CheckProjectileGenerationRetirement();

	g_edicts = saved_edicts;
	game.clients = saved_clients;
	game.maxclients = saved_maxclients;
	sg_host = saved_host;
	level.time = saved_time;
	return failures;
}
