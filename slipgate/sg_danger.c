/*
 * sg_danger.c -- deaths teach the map.  Moved verbatim from
 * sg_arach.c in the 2026-08-11 standards pass.
 */
#include "g_local.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_danger.h"
#include "slipgate/sg_rune.h"
#include "slipgate/sg_util.h"
#include <errno.h>

#define DANGER_MAGIC		0x31474E44U /* "DNG1" */
#define DANGER_VERSION		1U

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

static unsigned int Danger_CRC32Update(unsigned int crc,
	const void *block, size_t size)
{
	static unsigned int table[256];
	static qboolean ready;
	const unsigned char *p = (const unsigned char *)block;
	unsigned int c;
	int i, j;

	if (!ready)
	{
		for (i = 0; i < 256; i++)
		{
			c = (unsigned int)i;
			for (j = 0; j < 8; j++)
				c = (c & 1U) ? (c >> 1) ^ 0xEDB88320U : c >> 1;
			table[i] = c;
		}
		ready = true;
	}
	while (size--)
		crc = table[(crc ^ (unsigned int)*p++) & 0xffU] ^ (crc >> 8);
	return crc;
}

static unsigned int Danger_SeedCRC(const rune_t *r)
{
	unsigned int crc = 0xffffffffU;

	if (!r || r->hdr.num_seeds <= 0)
		return 0;
	crc = Danger_CRC32Update(crc, r->seeds,
	    sizeof(rune_seed_t) * (size_t)r->hdr.num_seeds);
	return crc ^ 0xffffffffU;
}

void Danger_Learn(int team, int seed)
{
	if (team < 1 || team > 2 || seed < 0 || seed >= SG_MAX_SEEDS)
		return;
	sg_danger[SG_TeamIdx(team)][seed] += 1200;      /* fitted: ~a detour's worth */
	if (sg_danger[SG_TeamIdx(team)][seed] > 8000)
		sg_danger[SG_TeamIdx(team)][seed] = 8000;
}

void Danger_Decay(void)
{
	int t, i;

	if (SG_TimerPending(sg_danger_decay_next))
		return;
	SG_TimerArm(&sg_danger_decay_next, 1.0f);
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
	char path[MAX_OSPATH], tmp_path[MAX_OSPATH];
	FILE *f = NULL;
	rune_t *r = SG_Rune();
	unsigned int header[5], attempt;
	qboolean ok;

	if (!r || r->hdr.num_seeds <= 0 || r->hdr.num_seeds > SG_MAX_SEEDS)
		return;
	Danger_Path(path, sizeof(path));
	tmp_path[0] = '\0';
	for (attempt = 0; attempt < 64 && !f; attempt++)
	{
		unsigned long pid;

#ifdef _WIN32
		pid = (unsigned long)GetCurrentProcessId();
#else
		pid = (unsigned long)getpid();
#endif
		Com_sprintf(tmp_path, sizeof(tmp_path), "%s.%lu.%u.tmp",
		            path, pid, attempt);
		errno = 0;
		f = fopen(tmp_path, "wbx");
		if (!f && errno != EEXIST)
			break;
	}
	if (!f)
		return;
	header[0] = DANGER_MAGIC;
	header[1] = DANGER_VERSION;
	header[2] = (unsigned int)r->hdr.version;
	header[3] = (unsigned int)r->hdr.num_seeds;
	header[4] = Danger_SeedCRC(r);
	ok = fwrite(header, sizeof(header), 1, f) == 1 &&
	     fwrite(sg_danger[0], sizeof(int), r->hdr.num_seeds, f) ==
	         (size_t)r->hdr.num_seeds &&
	     fwrite(sg_danger[1], sizeof(int), r->hdr.num_seeds, f) ==
	         (size_t)r->hdr.num_seeds &&
	     fflush(f) == 0;
	if (fclose(f) != 0)
		ok = false;
	if (!ok)
	{
		remove(tmp_path);
		return;
	}
#ifdef _WIN32
	if (!MoveFileExA(tmp_path, path,
	                 MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
#else
	if (rename(tmp_path, path) != 0)
#endif
		remove(tmp_path);
}

void Danger_Load(void)
{
	char path[MAX_OSPATH];
	FILE *f;
	rune_t *r = SG_Rune();
	unsigned int header[5];
	long file_size;
	size_t expected;
	qboolean ok = false;

	memset(sg_danger, 0, sizeof(sg_danger));
	/* This deadline is level-time state, not learned map state. A time rewind
	 * must never freeze the freshly loaded field for the old map's uptime. */
	sg_danger_decay_next = 0.0f;
	if (!r || r->hdr.num_seeds <= 0 || r->hdr.num_seeds > SG_MAX_SEEDS)
		return;
	Danger_Path(path, sizeof(path));
	f = fopen(path, "rb");
	if (!f)
		return;
	expected = sizeof(header) + 2U * (size_t)r->hdr.num_seeds * sizeof(int);
	if (fread(header, sizeof(header), 1, f) == 1 &&
	    header[0] == DANGER_MAGIC && header[1] == DANGER_VERSION &&
	    header[2] == (unsigned int)r->hdr.version &&
	    header[3] == (unsigned int)r->hdr.num_seeds &&
	    header[4] == Danger_SeedCRC(r) &&
	    fseek(f, 0, SEEK_END) == 0 &&
	    (file_size = ftell(f)) >= 0 && (size_t)file_size == expected &&
	    fseek(f, (long)sizeof(header), SEEK_SET) == 0 &&
	    fread(sg_danger[0], sizeof(int), r->hdr.num_seeds, f) ==
	        (size_t)r->hdr.num_seeds &&
	    fread(sg_danger[1], sizeof(int), r->hdr.num_seeds, f) ==
	        (size_t)r->hdr.num_seeds)
		ok = true;
	fclose(f);
	if (!ok)
		memset(sg_danger, 0, sizeof(sg_danger));
}


/* the read side: pricing borrows the team ledger, never writes it */
const int *Danger_Field(int team)
{
	return sg_danger[SG_TeamIdx(team)];
}
