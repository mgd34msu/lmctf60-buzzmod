/* Focused field-flood contract for map-local snag repair input. */
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "g_local.h"
#include "slipgate/sg_cvars.h"
#include "slipgate/sg_hooks.h"
#include "slipgate/sg_local.h"
#include "slipgate/sg_rune_file.h"
#include "slipgate/sg_snag_repair.h"

sg_host_t sg_host;
sg_cvars_t sg_cv;
cvar_t *ctfflags;
level_locals_t level;
game_export_t globals;

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
static char last_diagnostic[1024];

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static void TestDprint(const char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	vsnprintf(last_diagnostic, sizeof(last_diagnostic), format, arguments);
	va_end(arguments);
}

static void FileSHA256(const char *path, char digest[65])
{
	FILE *file = fopen(path, "rb");
	unsigned char bytes[131072];
	size_t length;

	if (!file)
	{
		perror(path);
		exit(2);
	}
	length = fread(bytes, 1, sizeof(bytes), file);
	if (ferror(file) || !feof(file) || fclose(file) != 0)
	{
		fprintf(stderr, "cannot read complete test artifact\n");
		exit(2);
	}
	SG_RuneFileSHA256Buffer(bytes, length, digest);
}

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
	rune->artifact.payload_crc32 = 33;
	rune->artifact.header_crc32 = 44;
	rune->artifact.action_contract_crc32 = 55;
	rune->artifact.mechanism_contract_crc32 = 66;
	rune->artifact.num_seeds = 5;
	rune->artifact.num_links = 4;
	memset(rune->encoded_sha256, 'b', 64);
	rune->encoded_sha256[64] = '\0';
}

static void WriteHeaderFloats(FILE *file, int wrong_identity,
	int repair_count, const char *gravity, const char *airaccelerate,
	const char *maxvelocity)
{
	fprintf(file, "snag_format 2\n");
	fprintf(file, "map testmap\n");
	fprintf(file, "bsp_checksum %d\n", wrong_identity ? 12 : 11);
	fprintf(file, "entity_crc 22\nphysics_flags 0\ngravity %s\n", gravity);
	fprintf(file, "airaccelerate %s\nmaxvelocity %s\npmove_ms 25\n",
		airaccelerate, maxvelocity);
	fprintf(file, "frame_ms 100\nhost_physics_id 1\n");
	fprintf(file, "rune_payload_crc 33\nrune_header_crc 44\n");
	fprintf(file, "rune_action_contract_crc 55\n");
	fprintf(file, "rune_mechanism_contract_crc 66\n");
	fprintf(file, "rune_num_seeds 5\nrune_num_links 4\n");
	fprintf(file, "rune_sha256 bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\n");
	fprintf(file, "evidence_sha256 aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\n");
	fprintf(file, "repairs %d\n", repair_count);
}

static void WriteHeader(FILE *file, int wrong_identity, int repair_count)
{
	WriteHeaderFloats(file, wrong_identity, repair_count, "800", "0", "2000");
}

static void WriteFloatFile(const char *path, const char *gravity,
	const char *airaccelerate, const char *maxvelocity)
{
	FILE *file = fopen(path, "wb");

	if (!file)
	{
		perror(path);
		exit(2);
	}
	WriteHeaderFloats(file, 0, 0, gravity, airaccelerate, maxvelocity);
	fclose(file);
}

static void WriteFileCount(const char *path, const char *repair,
	int wrong_identity, int repair_count)
{
	FILE *file = fopen(path, "wb");

	if (!file)
	{
		perror(path);
		exit(2);
	}
	WriteHeader(file, wrong_identity, repair_count);
	if (repair)
		fputs(repair, file);
	fclose(file);
}

static void WriteFile(const char *path, const char *repair, int wrong_identity)
{
	WriteFileCount(path, repair, wrong_identity, repair ? 1 : 0);
}

int main(void)
{
	rune_t rune;
	rune_seed_t seeds[5];
	rune_link_t links[4], links_before[4];
	int field[5], source = 3, cost = 0;
	const char *path = "/tmp/sg_snag_repair_test.input";
	char digest[65], snag_digest[65], expected_diagnostic[1024];

	SG_RuneFileSHA256Buffer((const unsigned char *)"abc", 3U, digest);
	CHECK(strcmp(digest,
		"ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0);

	memset(&rune, 0, sizeof(rune));
	sg_host.dprint = TestDprint;
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
	/* Seed 4 shares the horizontal coordinate but is a different floor. */
	seeds[4].origin[0] = 100.0f;
	seeds[4].origin[2] = 200.0f;
	Link(&links[0], 0, 1, 100);
	Link(&links[1], 1, 3, 100);
	Link(&links[2], 0, 2, 50);
	Link(&links[3], 2, 3, 50);
	memcpy(links_before, links, sizeof(links));

	Fields_TestFloodFlat(&rune, field, &source, &cost, 1);
	CHECK(field[0] == 100); /* cheap route uses seed 2 before a repair */

	/* Missing is not an authenticated clean-map declaration. */
	CHECK(!SG_SnagRepairLoadFile(&rune, "/tmp/does-not-exist-snag.input"));
	CHECK(SG_SnagRepairSeedSurcharge(2) == 0);

	/* An explicit artifact-bound zero-repair file is the only clean no-op. */
	WriteFile(path, NULL, 0);
	FileSHA256(path, snag_digest);
	CHECK(SG_SnagRepairLoadFile(&rune, path));
	snprintf(expected_diagnostic, sizeof(expected_diagnostic),
		"slipgate: snag ready map=testmap repairs=0 "
		"rune_sha256=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb "
		"evidence_sha256=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa "
		"snag_sha256=%s\n", snag_digest);
	CHECK(strcmp(last_diagnostic, expected_diagnostic) == 0);
	CHECK(SG_SnagRepairSeedSurcharge(2) == 0);
	WriteFloatFile(path, "+800", "0", "2000");
	CHECK(!SG_SnagRepairLoadFile(&rune, path));
	WriteFloatFile(path, "800.0", "0", "2000");
	CHECK(!SG_SnagRepairLoadFile(&rune, path));
	WriteFloatFile(path, "800", "-0", "2000");
	CHECK(!SG_SnagRepairLoadFile(&rune, path));

	/* The producer names one exact seed and repeats its authenticated origin;
	 * a stacked floor or neighboring seed cannot be selected by radius. */
	WriteFile(path, "repair 2 100.000 0.000 200.000 3 1200 400\n", 0);
	CHECK(!SG_SnagRepairLoadFile(&rune, path));
	CHECK(SG_SnagRepairSeedSurcharge(2) == 0);
	CHECK(SG_SnagRepairSeedSurcharge(4) == 0);
	WriteFile(path, "repair 2 100 0 0 3 1200 400\n", 0);
	CHECK(!SG_SnagRepairLoadFile(&rune, path)); /* noncanonical grammar */
	WriteFile(path, "repair 2 100.000 -0.000 0.000 3 1200 400\n", 0);
	CHECK(!SG_SnagRepairLoadFile(&rune, path)); /* signed-zero identity drift */

	/* The exact repair makes the ordinary two-hop route win without mutating
	 * link costs or graph bytes. */
	WriteFile(path, "repair 2 100.000 0.000 0.000 3 1200 400\n", 0);
	CHECK(SG_SnagRepairLoadFile(&rune, path));
	CHECK(SG_SnagRepairSeedSurcharge(2) == 200);
	CHECK(SG_SnagRepairLinkSurcharge(3) == 200);
	CHECK(SG_SnagRepairLinkSurcharge(2) == 0);
	Fields_TestFloodFlat(&rune, field, &source, &cost, 1);
	CHECK(field[0] == 200);
	CHECK(memcmp(links, links_before, sizeof(links)) == 0);

	WriteFile(path, "repair 99 100.000 0.000 0.000 3 1200 400\n", 0);
	CHECK(!SG_SnagRepairLoadFile(&rune, path));
	WriteFile(path, "repair 2 nan 0 0 3 1200 400\n", 0);
	CHECK(!SG_SnagRepairLoadFile(&rune, path));
	WriteFile(path, "repair 2 1e999 0 0 3 1200 400\n", 0);
	CHECK(!SG_SnagRepairLoadFile(&rune, path));
	WriteFile(path, "repair 2 100.000 0.000 0.000 1000001 1200 400\n", 0);
	CHECK(!SG_SnagRepairLoadFile(&rune, path));
	WriteFile(path, "repair 2 100.000 0.000 0.000 3 86400001 400\n", 0);
	CHECK(!SG_SnagRepairLoadFile(&rune, path));
	WriteFileCount(path,
		"repair 2 100.000 0.000 0.000 3 1200 400\n"
		"repair 2 100.000 0.000 0.000 4 1200 500\n",
		0, 2);
	CHECK(!SG_SnagRepairLoadFile(&rune, path));
	WriteFileCount(path, "repair 2 100.000 0.000 0.000 3 1200 400\n", 0, 0);
	CHECK(!SG_SnagRepairLoadFile(&rune, path));
	WriteFile(path, "repair 2 100.000 0.000 0.000 3 1200 400\n", 1);
	CHECK(!SG_SnagRepairLoadFile(&rune, path));
	WriteFile(path, "repair 2 100.000 0.000 0.000 3 1200 400\n", 0);
	rune.artifact.payload_crc32++;
	CHECK(!SG_SnagRepairLoadFile(&rune, path));
	rune.artifact.payload_crc32--;
	rune.encoded_sha256[0] = 'c';
	CHECK(!SG_SnagRepairLoadFile(&rune, path));
	rune.encoded_sha256[0] = 'b';
	seeds[2].flags = RSF_WATER;
	CHECK(!SG_SnagRepairLoadFile(&rune, path));
	seeds[2].flags = RSF_TOMBSTONE;
	CHECK(!SG_SnagRepairLoadFile(&rune, path));
	seeds[2].flags = 0;
	links[2].from = 1;
	links[3].from = 3;
	CHECK(!SG_SnagRepairLoadFile(&rune, path)); /* selected seed has no route */
	remove(path);

	if (failures)
	{
		fprintf(stderr, "sg_snag_repair_test: %d failure(s)\n", failures);
		return 1;
	}
	puts("sg_snag_repair_test: ok");
	return 0;
}
