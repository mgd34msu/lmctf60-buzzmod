/* Focused field-flood contract for map-local snag repair input. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "g_local.h"
#include "slipgate/sg_cvars.h"
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_snag_repair.h"

sg_host_t sg_host;
sg_cvars_t sg_cv;
cvar_t *ctfflags;
level_locals_t level;
game_export_t globals;

int SG_ActionFieldBiasMs(int action, int rope_bias_ms)
{
	(void)action;
	(void)rope_bias_ms;
	return 0;
}

sg_identity_status_t SG_LevelIdentitySnapshot(const char *map,
	sg_level_identity_t *out)
{
	(void)map;
	memset(out, 0, sizeof(*out));
	return SG_IDENTITY_UNAVAILABLE;
}

void Fields_TestFloodFlat(rune_t *r, int *dist,
	const int *sources, const int *source_cost, int num_sources);

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static void Link(rune_link_t *link, int from, int to, int cost)
{
	memset(link, 0, sizeof(*link));
	link->from = from;
	link->to = to;
	link->action = RL_RUN;
	link->cost_ms = (short)cost;
	link->heading_slack = 255;
}

static void Identity(rune_t *rune)
{
	rune_identity_t *identity = &rune->artifact.identity;

	memset(identity, 0, sizeof(*identity));
	strcpy(identity->map_name, "testmap");
	identity->bsp_checksum = 11;
	identity->entity_crc32 = 22;
	identity->physics_flags = 0;
	identity->gravity = 800.0f;
	identity->airaccelerate = 0.0f;
	identity->maxvelocity = 2000.0f;
	identity->pmove_substep_ms = 25;
	identity->server_frame_ms = 100;
	identity->host_physics_id = 1;
}

static void WriteHeader(FILE *file, int wrong_identity)
{
	fprintf(file, "map testmap\n");
	fprintf(file, "bsp_checksum %d\n", wrong_identity ? 12 : 11);
	fprintf(file, "entity_crc 22\nphysics_flags 0\ngravity 800\n");
	fprintf(file, "airaccelerate 0\nmaxvelocity 2000\npmove_ms 25\n");
	fprintf(file, "frame_ms 100\nhost_physics_id 1\n");
}

static void WriteFile(const char *path, const char *repair, int wrong_identity)
{
	FILE *file = fopen(path, "wb");

	if (!file)
	{
		perror(path);
		exit(2);
	}
	WriteHeader(file, wrong_identity);
	if (repair)
		fputs(repair, file);
	fclose(file);
}

int main(void)
{
	rune_t rune;
	rune_seed_t seeds[5];
	rune_link_t links[4], links_before[4];
	int field[5], source = 3, cost = 0;
	const char *path = "/tmp/sg_snag_repair_test.input";

	memset(&rune, 0, sizeof(rune));
	memset(seeds, 0, sizeof(seeds));
	memset(links, 0, sizeof(links));
	rune.hdr.num_seeds = 5;
	rune.hdr.num_links = 4;
	rune.seeds = seeds;
	rune.links = links;
	Identity(&rune);
	seeds[1].origin[0] = 50.0f;
	seeds[2].origin[0] = 100.0f;
	seeds[3].origin[0] = 200.0f;
	/* Seed 4 shares the horizontal coordinate only; it proves that a repair
	 * cannot snap a lower/upper floor into a different vertical route. */
	seeds[4].origin[0] = 100.0f;
	seeds[4].origin[2] = 200.0f;
	Link(&links[0], 0, 1, 100);
	Link(&links[1], 1, 3, 100);
	Link(&links[2], 0, 2, 50);
	Link(&links[3], 2, 3, 50);
	memcpy(links_before, links, sizeof(links));

	Fields_TestFloodFlat(&rune, field, &source, &cost, 1);
	CHECK(field[0] == 100); /* cheap route uses seed 2 before a repair */

	/* Missing is an explicit no-op. */
	CHECK(SG_SnagRepairLoadFile(&rune, "/tmp/does-not-exist-snag.input"));
	CHECK(SG_SnagRepairSeedSurcharge(2) == 0);

	/* The stacked seed makes this coordinate vertically ambiguous. */
	WriteFile(path, "repair 100 0 0 16 3 1200 400\n", 0);
	CHECK(!SG_SnagRepairLoadFile(&rune, path));
	CHECK(SG_SnagRepairSeedSurcharge(2) == 0);

	/* Two eligible grounded seeds are equally unsafe: reject before mutating
	 * either surcharge, with no nearest-seed tie break. */
	seeds[4].origin[0] = 105.0f;
	seeds[4].origin[2] = 0.0f;
	CHECK(!SG_SnagRepairLoadFile(&rune, path));
	CHECK(SG_SnagRepairSeedSurcharge(2) == 0);
	CHECK(SG_SnagRepairSeedSurcharge(4) == 0);

	/* Remove the conflicting floor.  The exact repair makes the ordinary
	 * two-hop route win without mutating link costs or graph bytes. */
	seeds[4].flags = RSF_TOMBSTONE;
	CHECK(SG_SnagRepairLoadFile(&rune, path));
	CHECK(SG_SnagRepairSeedSurcharge(2) == 200);
	CHECK(SG_SnagRepairLinkSurcharge(3) == 200);
	CHECK(SG_SnagRepairLinkSurcharge(2) == 0);
	Fields_TestFloodFlat(&rune, field, &source, &cost, 1);
	CHECK(field[0] == 200);
	CHECK(memcmp(links, links_before, sizeof(links)) == 0);

	WriteFile(path, "repair 900 0 0 16 3 1200 400\n", 0);
	CHECK(!SG_SnagRepairLoadFile(&rune, path)); /* no grounded seed */
	WriteFile(path, "repair nan 0 0 16 3 1200 400\n", 0);
	CHECK(!SG_SnagRepairLoadFile(&rune, path));
	WriteFile(path, "repair 1e999 0 0 16 3 1200 400\n", 0);
	CHECK(!SG_SnagRepairLoadFile(&rune, path));
	WriteFile(path, "repair 100 0 0 16 1000001 1200 400\n", 0);
	CHECK(!SG_SnagRepairLoadFile(&rune, path));
	WriteFile(path, "repair 100 0 0 16 3 86400001 400\n", 0);
	CHECK(!SG_SnagRepairLoadFile(&rune, path));
	WriteFile(path, "repair 100 0 0 16 3 1200 400\nrepair 100 0 0 16 4 1200 500\n", 0);
	CHECK(!SG_SnagRepairLoadFile(&rune, path));
	WriteFile(path, NULL, 0);
	CHECK(!SG_SnagRepairLoadFile(&rune, path));
	WriteFile(path, "repair 100 0 0 16 3 1200 400\n", 1);
	CHECK(!SG_SnagRepairLoadFile(&rune, path));
	remove(path);

	if (failures)
	{
		fprintf(stderr, "sg_snag_repair_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("sg_snag_repair_test: ok");
	return 0;
}
