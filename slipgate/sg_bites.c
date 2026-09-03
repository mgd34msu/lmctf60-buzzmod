#include "../g_local.h"
#undef world
#include "sg_bites.h"
#include "sg_rune_artifact.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BITES_GRID 16
#define BITES_FLUSH_SECONDS 30.0f
#define BITES_GROWTH_MIN 50
#define BITES_GROWTH_SHARE 0.10f

typedef struct bite_entry_s
{
	float fire[3];
	float bite[3];
} bite_entry_t;

static bite_entry_t *sg_bites;
static uint32_t sg_bite_count, sg_bite_capacity;
static char sg_bites_path[MAX_OSPATH];
static int sg_bites_dirty;
static float sg_bites_flushed_at;
/* Per client: where the rope now out was fired from, and whether its bite
 * was already noted. */
static float sg_fire_at[MAX_CLIENTS][3];
static int sg_fire_valid[MAX_CLIENTS];
static int sg_bite_noted[MAX_CLIENTS];

static const char *GameDirectory(void)
{
	cvar_t *cv = gi.cvar("gamedir", "", 0);

	return cv && cv->string && cv->string[0] ? cv->string : ".";
}

static int Known(const float bite[3])
{
	uint32_t i;
	int kx = (int)bite[0] / BITES_GRID, ky = (int)bite[1] / BITES_GRID, kz = (int)bite[2] / BITES_GRID;

	for (i = 0; i < sg_bite_count; i++)
		if ((int)sg_bites[i].bite[0] / BITES_GRID == kx &&
			(int)sg_bites[i].bite[1] / BITES_GRID == ky &&
			(int)sg_bites[i].bite[2] / BITES_GRID == kz)
			return 1;
	return 0;
}

static int Add(const float fire[3], const float bite[3])
{
	if (Known(bite))
		return 0;
	if (sg_bite_count >= sg_bite_capacity)
	{
		uint32_t want = sg_bite_capacity ? sg_bite_capacity * 2U : 1024U;
		bite_entry_t *grown = realloc(sg_bites, (size_t)want * sizeof(*grown));

		if (!grown)
			return 0;
		sg_bites = grown;
		sg_bite_capacity = want;
	}
	memcpy(sg_bites[sg_bite_count].fire, fire, sizeof(float) * 3U);
	memcpy(sg_bites[sg_bite_count].bite, bite, sizeof(float) * 3U);
	sg_bite_count++;
	return 1;
}

void SG_BitesLevelBegin(const char *mapname)
{
	FILE *f;
	char line[256];

	SG_BitesFlush(1);
	sg_bite_count = 0U;
	sg_bites_dirty = 0;
	sg_bites_flushed_at = level.time;
	memset(sg_fire_valid, 0, sizeof(sg_fire_valid));
	memset(sg_bite_noted, 0, sizeof(sg_bite_noted));
	sg_bites_path[0] = 0;
	if (!mapname || !mapname[0])
		return;
	snprintf(sg_bites_path, sizeof(sg_bites_path), "%s/maps/%s.bites", GameDirectory(), mapname);
	f = fopen(sg_bites_path, "r");
	if (!f)
		return;
	while (fgets(line, sizeof(line), f))
	{
		float fire[3], bite[3];

		if (sscanf(line, "%f %f %f %f %f %f", &fire[0], &fire[1], &fire[2],
			&bite[0], &bite[1], &bite[2]) == 6)
			Add(fire, bite);
	}
	fclose(f);
}

void SG_BitesNote(edict_t *ent)
{
	int slot;

	if (!ent || !ent->client || (ent->flags & FL_BOT) || !sg_bites_path[0])
		return;
	slot = (int)(ent->client - game.clients);
	if (slot < 0 || slot >= MAX_CLIENTS)
		return;
	if (ent->client->hookstate == 0)
	{
		sg_fire_valid[slot] = 0;
		sg_bite_noted[slot] = 0;
		return;
	}
	if (!sg_fire_valid[slot])
	{
		VectorCopy(ent->s.origin, sg_fire_at[slot]);
		sg_fire_valid[slot] = 1;
	}
	if (ent->client->hookstate == 2 && ent->client->hook && !sg_bite_noted[slot])
	{
		sg_bite_noted[slot] = 1;
		if (Add(sg_fire_at[slot], ent->client->hook->s.origin))
			sg_bites_dirty = 1;
	}
}

void SG_BitesFlush(int force)
{
	FILE *f;
	uint32_t i;

	if (!sg_bites_dirty || !sg_bites_path[0])
		return;
	if (!force && level.time - sg_bites_flushed_at < BITES_FLUSH_SECONDS)
		return;
	f = fopen(sg_bites_path, "w");
	if (!f)
		return;
	for (i = 0; i < sg_bite_count; i++)
		fprintf(f, "%.0f %.0f %.0f %.0f %.0f %.0f\n",
			sg_bites[i].fire[0], sg_bites[i].fire[1], sg_bites[i].fire[2],
			sg_bites[i].bite[0], sg_bites[i].bite[1], sg_bites[i].bite[2]);
	fclose(f);
	sg_bites_dirty = 0;
	sg_bites_flushed_at = level.time;
}

static uint32_t CountLines(const char *path)
{
	FILE *f = path && path[0] ? fopen(path, "r") : NULL;
	uint32_t n = 0U;
	char line[256];

	if (!f)
		return 0U;
	while (fgets(line, sizeof(line), f))
		n++;
	fclose(f);
	return n;
}

static void CountPath(char *out, size_t size, const char *rune_path)
{
	size_t n = strlen(rune_path);

	if (n + sizeof(".bites-count") > size)
	{
		out[0] = 0;
		return;
	}
	memcpy(out, rune_path, n);
	memcpy(out + n, ".bites-count", sizeof(".bites-count"));
}

void SG_BitesWriteCountFor(const char *rune_path, const char *bites_path)
{
	char path[MAX_OSPATH];
	FILE *f;

	CountPath(path, sizeof(path), rune_path);
	f = fopen(path, "w");
	if (!f)
		return;
	fprintf(f, "%u\n", (unsigned)CountLines(bites_path));
	fclose(f);
}

int SG_BitesGrown(const char *mapname)
{
	char rune[MAX_OSPATH], count_path[MAX_OSPATH], bites[MAX_OSPATH];
	FILE *f;
	unsigned built = 0U, now;

	if (!mapname || !mapname[0] ||
		!SG_RuneArtifactPath(rune, sizeof(rune), GameDirectory(), mapname))
		return 0;
	snprintf(bites, sizeof(bites), "%s/maps/%s.bites", GameDirectory(), mapname);
	now = CountLines(bites);
	CountPath(count_path, sizeof(count_path), rune);
	f = fopen(count_path, "r");
	if (f)
	{
		if (fscanf(f, "%u", &built) != 1)
			built = 0U;
		fclose(f);
	}
	if (now <= built)
		return 0;
	return now - built >= BITES_GROWTH_MIN &&
		(float)(now - built) >= BITES_GROWTH_SHARE * (float)(built ? built : 1U);
}
