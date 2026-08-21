#include <stdio.h>
#include <string.h>

#include "g_local.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_bot.h"
#include "slipgate/sg_compound_drop_game.h"
#include "slipgate/sg_compound_publication.h"
#include "slipgate/sg_compound_world.h"
#include "slipgate/sg_cvars.h"
#include "slipgate/sg_hooks.h"

game_import_t gi;
game_export_t globals;
game_locals_t game;
level_locals_t level;
edict_t *g_edicts;
sg_bot_t sg_bots[SG_MAXBOTS];
sg_cvars_t sg_cv;
sg_host_t sg_host;

static edict_t entities[6];
static gclient_t client;
static rune_t rune;
static rune_seed_t seeds[2];
static rune_link_t links[1];
static sg_compound_publication_binding_t binding;
static sg_compound_world_preopen_t mechanism;
static const edict_t *allowed_source;
static uint32_t catalog_generation[6];

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, \
		    #expression); \
		return 0; \
	} \
} while (0)

rune_t *SG_Rune(void)
{
	return &rune;
}

qboolean SG_RunePhysicsCompatible(const rune_t *candidate)
{
	return candidate == &rune;
}

const sg_compound_publication_binding_t *SG_CompoundPublicationBinding(
	const rune_t *candidate, uint32_t link_index)
{
	return candidate == &rune && link_index == 0U ? &binding : NULL;
}

const sg_compound_world_preopen_t *SG_CompoundPublicationMechanism(
	const rune_t *candidate,
	const sg_compound_publication_binding_t *candidate_binding)
{
	return candidate == &rune && candidate_binding == &binding ?
	       &mechanism : NULL;
}

int SG_CompoundWorldResolvedMember(
	const sg_compound_world_preopen_t *candidate, edict_t **member_out)
{
	if (candidate != &mechanism || !member_out)
		return 0;
	*member_out = &entities[3];
	return 1;
}

int SG_CompoundWorldTargetSourceCurrent(
	const sg_compound_world_preopen_t *candidate, const edict_t *source)
{
	return candidate == &mechanism && source == allowed_source;
}

int SG_MechCatalogEntityGeneration(const edict_t *entity,
	uint32_t *key_out, uint32_t *generation_out)
{
	ptrdiff_t key;

	if (!entity || !key_out || !generation_out || key_out == generation_out)
		return 0;
	key = entity - entities;
	if (key <= 0 || key >= 6 || !entity->inuse ||
	    catalog_generation[key] == 0U)
		return 0;
	*key_out = (uint32_t)key;
	*generation_out = catalog_generation[key];
	return 1;
}

void Think_Delay(edict_t *entity)
{
	(void)entity;
}

static void FixtureInit(void)
{
	memset(&globals, 0, sizeof(globals));
	memset(entities, 0, sizeof(entities));
	memset(&client, 0, sizeof(client));
	memset(&rune, 0, sizeof(rune));
	memset(seeds, 0, sizeof(seeds));
	memset(links, 0, sizeof(links));
	memset(&binding, 0, sizeof(binding));
	memset(&mechanism, 0, sizeof(mechanism));
	memset(sg_bots, 0, sizeof(sg_bots));
	memset(catalog_generation, 0, sizeof(catalog_generation));
	g_edicts = entities;
	globals.num_edicts = 6;
	rune.hdr.num_links = 1;
	rune.hdr.num_seeds = 2;
	rune.seeds = seeds;
	rune.links = links;
	links[0].action = RL_DOOR_DROP;
	binding.link_index = 0U;
	binding.link.action = RL_DOOR_DROP;
	mechanism.trigger_key = 2;
	mechanism.mover_key = 3;
	entities[1].inuse = true;
	entities[1].client = &client;
	entities[3].inuse = true;
	catalog_generation[3] = 103U;
	sg_bots[0].active = true;
	sg_bots[0].ent = &entities[1];
	sg_bots[0].compound_drop_live.guard_owned = true;
	sg_bots[0].compound_drop_live.snapshot.binding = binding;
	sg_bots[0].compound_drop_live.snapshot.trigger_key =
	    mechanism.trigger_key;
	sg_bots[0].compound_drop_live.snapshot.mover_key = mechanism.mover_key;
	allowed_source = NULL;
}

static int TestDelayedRelayAuthorityIsOneShot(void)
{
	edict_t *delayed;
	edict_t *relay;
	char relay_target[] = "relay-sound";

	FixtureInit();
	relay = &entities[4];
	relay->inuse = true;
	relay->classname = "trigger_relay";
	relay->target = relay_target;
	relay->delay = 0.1f;
	catalog_generation[4] = 104U;
	allowed_source = relay;
	CHECK(SG_CompoundDropGameAuthorizeTargetDispatch(&sg_bots[0], relay));

	delayed = &entities[5];
	delayed->inuse = true;
	delayed->classname = "DelayedUse";
	delayed->think = Think_Delay;
	delayed->activator = &entities[1];
	delayed->spawnflags = SG_DELAYED_USE_BOT_ACTIVATOR;
	delayed->target = relay->target;
	SG_CompoundDropGameTagDelayedTarget(relay, &entities[1], delayed);
	CHECK(delayed->sg_delayed_source_key == 4U);
	CHECK(delayed->sg_delayed_source_generation == 104U);
	CHECK(SG_CompoundDropGameAuthorizeTargetDispatch(&sg_bots[0], delayed));
	CHECK(delayed->sg_delayed_source_key == 0U);
	CHECK(delayed->sg_delayed_source_generation == 0U);
	CHECK(!SG_CompoundDropGameAuthorizeTargetDispatch(&sg_bots[0], delayed));
	return 1;
}

static int TestDelayedRelayAuthorityRejectsDrift(void)
{
	edict_t *delayed;
	edict_t *relay;
	char relay_target[] = "relay-sound";

	FixtureInit();
	relay = &entities[4];
	relay->inuse = true;
	relay->classname = "trigger_relay";
	relay->target = relay_target;
	relay->delay = 0.1f;
	catalog_generation[4] = 104U;
	allowed_source = relay;
	delayed = &entities[5];
	delayed->inuse = true;
	delayed->classname = "DelayedUse";
	delayed->think = Think_Delay;
	delayed->activator = &entities[1];
	delayed->spawnflags = SG_DELAYED_USE_BOT_ACTIVATOR;
	delayed->target = relay->target;

	SG_CompoundDropGameTagDelayedTarget(relay, &entities[1], delayed);
	delayed->sg_delayed_source_generation++;
	CHECK(!SG_CompoundDropGameAuthorizeTargetDispatch(&sg_bots[0], delayed));
	delayed->sg_delayed_source_generation = 0U;
	SG_CompoundDropGameTagDelayedTarget(relay, &entities[1], delayed);
	delayed->sg_delayed_source_key = 3U;
	delayed->sg_delayed_source_generation = 103U;
	entities[3].target = relay->target;
	CHECK(!SG_CompoundDropGameAuthorizeTargetDispatch(&sg_bots[0], delayed));
	delayed->sg_delayed_source_key = 0U;
	delayed->sg_delayed_source_generation = 0U;
	entities[3].target = NULL;
	SG_CompoundDropGameTagDelayedTarget(relay, &entities[1], delayed);
	relay->target = "rewired";
	CHECK(!SG_CompoundDropGameAuthorizeTargetDispatch(&sg_bots[0], delayed));
	relay->target = relay_target;
	delayed->sg_delayed_source_key = 0U;
	delayed->sg_delayed_source_generation = 0U;
	SG_CompoundDropGameTagDelayedTarget(relay, &entities[1], delayed);
	entities[1].inuse = false;
	CHECK(!SG_CompoundDropGameAuthorizeTargetDispatch(&sg_bots[0], delayed));
	return 1;
}

int main(void)
{
	if (!TestDelayedRelayAuthorityIsOneShot() ||
	    !TestDelayedRelayAuthorityRejectsDrift())
		return 1;
	puts("sg_compound_drop_fanout_game_test: ok");
	return 0;
}
