/*
 * sg_danger.c -- deaths teach the map.  Moved verbatim from
 * sg_arach.c in the 2026-08-11 standards pass.
 */
#include "g_local.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_danger.h"
#include "slipgate/sg_rune.h"

/*
 * The DANGER dimension: deaths teach the map. Team-indexed -- a corridor
 * lethal for red is safe for blue -- fed by each bot registering its own
 * death at its own seed (self-knowledge, the most honest sighting there
 * is), decayed a little every second, priced into the same descent as
 * every other dimension, and persisted beside the rune so the next match
 * on this map starts educated. The rune is a higher-order surface; this
 * is one more dimension of it.
 */
static int		sg_danger[2][SG_MAX_SEEDS];

static float	sg_danger_decay_next;

void Danger_Learn(int team, int seed)
{
	if (team < 1 || team > 2 || seed < 0 || seed >= SG_MAX_SEEDS)
		return;
	sg_danger[team - 1][seed] += 1200;      /* fitted: ~a detour's worth */
	if (sg_danger[team - 1][seed] > 8000)
		sg_danger[team - 1][seed] = 8000;
}

void Danger_Decay(void)
{
	int t, i;

	if (level.time < sg_danger_decay_next)
		return;
	sg_danger_decay_next = level.time + 1.0f;
	for (t = 0; t < 2; t++)
		for (i = 0; i < SG_MAX_SEEDS; i++)
			if (sg_danger[t][i])
				sg_danger[t][i] -= (sg_danger[t][i] >> 6) + 1;
}

static void Danger_Path(char *buf, int size)
{
	Com_sprintf(buf, size, "%s/maps/%s.rune.danger",
	            gamedir->string[0] ? gamedir->string : ".",
	            SG_RuneMapName());
}

void Danger_Save(void)
{
	char path[MAX_OSPATH];
	FILE *f;

	if (!SG_Rune())
		return;
	Danger_Path(path, sizeof(path));
	f = fopen(path, "wb");
	if (!f)
		return;
	fwrite(sg_danger, sizeof(sg_danger), 1, f);
	fclose(f);
}

void Danger_Load(void)
{
	char path[MAX_OSPATH];
	FILE *f;

	memset(sg_danger, 0, sizeof(sg_danger));
	Danger_Path(path, sizeof(path));
	f = fopen(path, "rb");
	if (!f)
		return;
	if (fread(sg_danger, sizeof(sg_danger), 1, f) != 1)
		memset(sg_danger, 0, sizeof(sg_danger));
	fclose(f);
}


/* the read side: pricing borrows the team ledger, never writes it */
const int *Danger_Field(int team)
{
	return sg_danger[team - 1];
}

