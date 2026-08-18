#include "g_local.h"
#include "slipgate/sg_net.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

game_locals_t game;
level_locals_t level;
game_import_t gi;
game_export_t globals;
spawn_temp_t st;
edict_t *g_edicts;

typedef struct
{
	int sound_calls;
	int positioned_calls;
	edict_t *ent;
	int channel;
	int soundindex;
	float volume;
	float attenuation;
	float timeofs;
	vec3_t origin;
} engine_sound_capture_t;

typedef struct
{
	int calls;
	edict_t *ent;
	qboolean has_origin;
	int channel;
	int soundindex;
	float volume;
	float attenuation;
	vec3_t origin;
} note_capture_t;

static engine_sound_capture_t engine_capture;
static note_capture_t note_capture;
static char indexed_names[64][MAX_QPATH];
static int indexed_count;
static int next_soundindex = 1;
static qboolean fail_concrete;

static char *wildcards[] =
{
	"*death1.wav", "*death2.wav", "*death3.wav", "*death4.wav",
	"*fall1.wav", "*fall2.wav", "*gurp1.wav", "*gurp2.wav",
	"*jump1.wav", "*pain25_1.wav", "*pain25_2.wav",
	"*pain50_1.wav", "*pain50_2.wav", "*pain75_1.wav",
	"*pain75_2.wav", "*pain100_1.wav", "*pain100_2.wav"
};

#define WILDCARD_COUNT ((int)(sizeof(wildcards) / sizeof(wildcards[0])))

static void EngineSound(edict_t *ent, int channel, int soundindex,
                        float volume, float attenuation, float timeofs)
{
	engine_capture.sound_calls++;
	engine_capture.ent = ent;
	engine_capture.channel = channel;
	engine_capture.soundindex = soundindex;
	engine_capture.volume = volume;
	engine_capture.attenuation = attenuation;
	engine_capture.timeofs = timeofs;
}

static void EnginePositionedSound(vec3_t origin, edict_t *ent, int channel,
                                  int soundindex, float volume,
                                  float attenuation, float timeofs)
{
	engine_capture.positioned_calls++;
	engine_capture.ent = ent;
	engine_capture.channel = channel;
	engine_capture.soundindex = soundindex;
	engine_capture.volume = volume;
	engine_capture.attenuation = attenuation;
	engine_capture.timeofs = timeofs;
	VectorCopy(origin, engine_capture.origin);
}

static int EngineSoundIndex(char *name)
{
	assert(name != NULL);
	assert(indexed_count < (int)(sizeof(indexed_names) / sizeof(indexed_names[0])));
	assert(strlen(name) < sizeof(indexed_names[0]));
	strcpy(indexed_names[indexed_count++], name);
	if (fail_concrete && !strncmp(name, "player/male/", 12))
		return 0;
	return next_soundindex++;
}

static void EngineUnicast(edict_t *ent, qboolean reliable)
{
	(void)ent;
	(void)reliable;
}

void SG_NoteSound(edict_t *ent, vec3_t origin, int channel, int soundindex,
                  float volume, float attenuation)
{
	note_capture.calls++;
	note_capture.ent = ent;
	note_capture.has_origin = origin ? true : false;
	note_capture.channel = channel;
	note_capture.soundindex = soundindex;
	note_capture.volume = volume;
	note_capture.attenuation = attenuation;
	if (origin)
		VectorCopy(origin, note_capture.origin);
}

void ClientCommand(edict_t *ent)
{
	(void)ent;
}

void G_InitEdict(edict_t *ent)
{
	(void)ent;
}

vec_t VectorNormalize(vec3_t v)
{
	(void)v;
	return 0.0f;
}

static void ClearCaptures(void)
{
	memset(&engine_capture, 0, sizeof(engine_capture));
	memset(&note_capture, 0, sizeof(note_capture));
}

static void AssertVec(vec3_t actual, float x, float y, float z)
{
	assert(actual[0] == x);
	assert(actual[1] == y);
	assert(actual[2] == z);
}

static void TestAllMappings(void)
{
	edict_t bot;
	int i;

	memset(&bot, 0, sizeof(bot));
	bot.flags = FL_BOT;
	VectorSet(bot.s.origin, 1.0f, 2.0f, 3.0f);
	for (i = 0; i < WILDCARD_COUNT; i++)
	{
		char expected[MAX_QPATH];
		int before = indexed_count;
		int index = gi.soundindex(wildcards[i]);
		int concrete = next_soundindex - 1;

		assert(index > 0);
		assert(indexed_count == before + 2);
		assert(!strcmp(indexed_names[before], wildcards[i]));
		snprintf(expected, sizeof(expected), "player/male/%s",
		         wildcards[i] + 1);
		assert(!strcmp(indexed_names[before + 1], expected));

		ClearCaptures();
		gi.sound(&bot, i, index, 1.0f, 1.0f, 0.0f);
		assert(engine_capture.sound_calls == 0);
		assert(engine_capture.positioned_calls == 1);
		assert(engine_capture.soundindex == concrete);
		assert(note_capture.calls == 1);
		assert(note_capture.soundindex == concrete);
		assert(note_capture.has_origin);
	}
}

static void TestMappedBotVoice(void)
{
	edict_t bot;
	int wildcard_index, concrete_index;

	memset(&bot, 0, sizeof(bot));
	bot.flags = FL_BOT;
	VectorSet(bot.s.origin, 111.0f, -222.0f, 33.5f);
	wildcard_index = gi.soundindex("*jump1.wav");
	concrete_index = next_soundindex - 1;
	ClearCaptures();

	gi.sound(&bot, 7, wildcard_index, 0.75f, 1.0f, 0.125f);

	assert(engine_capture.sound_calls == 0);
	assert(engine_capture.positioned_calls == 1);
	assert(engine_capture.ent == &bot);
	assert(engine_capture.channel == 7);
	assert(engine_capture.soundindex == concrete_index);
	assert(engine_capture.volume == 0.75f);
	assert(engine_capture.attenuation == 1.0f);
	assert(engine_capture.timeofs == 0.125f);
	AssertVec(engine_capture.origin, 111.0f, -222.0f, 33.5f);
	assert(note_capture.calls == 1);
	assert(note_capture.ent == &bot);
	assert(note_capture.has_origin);
	assert(note_capture.channel == 7);
	assert(note_capture.soundindex == concrete_index);
	AssertVec(note_capture.origin, 111.0f, -222.0f, 33.5f);
}

static void TestUnchangedPaths(void)
{
	edict_t human, bot, world_ent;
	vec3_t origin;
	int wildcard_index, ordinary_index;

	memset(&human, 0, sizeof(human));
	memset(&bot, 0, sizeof(bot));
	memset(&world_ent, 0, sizeof(world_ent));
	bot.flags = FL_BOT;
	VectorSet(origin, -8.0f, 16.0f, 24.0f);
	wildcard_index = gi.soundindex("*pain50_1.wav");
	ordinary_index = gi.soundindex("weapons/rocklf1a.wav");

	ClearCaptures();
	gi.sound(&human, 2, wildcard_index, 1.0f, 1.0f, 0.0f);
	assert(engine_capture.sound_calls == 1);
	assert(engine_capture.positioned_calls == 0);
	assert(engine_capture.soundindex == wildcard_index);
	assert(note_capture.calls == 1);
	assert(!note_capture.has_origin);

	ClearCaptures();
	gi.sound(&bot, 3, ordinary_index, 0.5f, 2.0f, 0.0f);
	assert(engine_capture.sound_calls == 1);
	assert(engine_capture.positioned_calls == 0);
	assert(engine_capture.soundindex == ordinary_index);
	assert(note_capture.calls == 1);
	assert(!note_capture.has_origin);

	ClearCaptures();
	gi.sound(NULL, 4, wildcard_index, 1.0f, 1.0f, 0.0f);
	assert(engine_capture.sound_calls == 1);
	assert(engine_capture.positioned_calls == 0);
	assert(engine_capture.ent == NULL);
	assert(engine_capture.soundindex == wildcard_index);
	assert(note_capture.calls == 1);
	assert(note_capture.ent == NULL);
	assert(!note_capture.has_origin);

	ClearCaptures();
	gi.sound(&world_ent, 5, wildcard_index, 1.0f, 1.0f, 0.0f);
	assert(engine_capture.sound_calls == 1);
	assert(engine_capture.positioned_calls == 0);
	assert(engine_capture.ent == &world_ent);
	assert(engine_capture.soundindex == wildcard_index);
	assert(note_capture.calls == 1);
	assert(!note_capture.has_origin);

	ClearCaptures();
	gi.positioned_sound(origin, &bot, 6, wildcard_index,
	                    0.6f, 1.5f, 0.25f);
	assert(engine_capture.sound_calls == 0);
	assert(engine_capture.positioned_calls == 1);
	assert(engine_capture.ent == &bot);
	assert(engine_capture.channel == 6);
	assert(engine_capture.soundindex == wildcard_index);
	assert(engine_capture.volume == 0.6f);
	assert(engine_capture.attenuation == 1.5f);
	assert(engine_capture.timeofs == 0.25f);
	AssertVec(engine_capture.origin, -8.0f, 16.0f, 24.0f);
	assert(note_capture.calls == 1);
	assert(note_capture.ent == &bot);
	assert(note_capture.has_origin);
	assert(note_capture.soundindex == wildcard_index);
	AssertVec(note_capture.origin, -8.0f, 16.0f, 24.0f);
}

static void TestLevelResetAndConcreteFailure(void)
{
	edict_t bot;
	int old_index, failed_index, next_before_reuse, reused_index;

	memset(&bot, 0, sizeof(bot));
	bot.flags = FL_BOT;
	old_index = gi.soundindex("*fall1.wav");
	SG_NetNewLevel();

	ClearCaptures();
	gi.sound(&bot, 1, old_index, 1.0f, 1.0f, 0.0f);
	assert(engine_capture.sound_calls == 1);
	assert(engine_capture.positioned_calls == 0);
	assert(note_capture.calls == 1);
	assert(!note_capture.has_origin);

	/* A later map may reuse the same numeric sound slot for another asset. */
	next_before_reuse = next_soundindex;
	next_soundindex = old_index;
	reused_index = gi.soundindex("misc/talk1.wav");
	next_soundindex = next_before_reuse;
	assert(reused_index == old_index);
	ClearCaptures();
	gi.sound(&bot, 2, reused_index, 1.0f, 1.0f, 0.0f);
	assert(engine_capture.sound_calls == 1);
	assert(engine_capture.positioned_calls == 0);
	assert(engine_capture.soundindex == reused_index);
	assert(note_capture.calls == 1);
	assert(!note_capture.has_origin);

	fail_concrete = true;
	failed_index = gi.soundindex("*death1.wav");
	fail_concrete = false;
	ClearCaptures();
	gi.sound(&bot, 4, failed_index, 1.0f, 1.0f, 0.0f);
	assert(engine_capture.sound_calls == 1);
	assert(engine_capture.positioned_calls == 0);
	assert(engine_capture.soundindex == failed_index);
	assert(note_capture.calls == 1);
	assert(note_capture.soundindex == failed_index);
	assert(!note_capture.has_origin);
}

int main(void)
{
	memset(&gi, 0, sizeof(gi));
	gi.sound = EngineSound;
	gi.positioned_sound = EnginePositionedSound;
	gi.soundindex = EngineSoundIndex;
	gi.unicast = EngineUnicast;

	SG_NetInstall();
	assert(gi.sound != EngineSound);
	assert(gi.positioned_sound != EnginePositionedSound);
	assert(gi.soundindex != EngineSoundIndex);

	TestAllMappings();
	TestMappedBotVoice();
	TestUnchangedPaths();
	TestLevelResetAndConcreteFailure();

	puts("sg_spectator_sound_test: ok");
	return 0;
}
