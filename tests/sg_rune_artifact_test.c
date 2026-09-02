/* Round trip of an era-4 artifact: encode, decode, validate, reject damage,
 * write and load a file. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../slipgate/sg_rune_artifact.h"

static int failures;

#define CHECK(condition) \
	do { if (!(condition)) { failures++; \
		fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition); } } while (0)

static void Vertex(sg_rune_cx_vec3_t *v, int x, int y, int z)
{
	v->value[0] = x * SG_RUNE_CX_Q8_ONE;
	v->value[1] = y * SG_RUNE_CX_Q8_ONE;
	v->value[2] = z * SG_RUNE_CX_Q8_ONE;
}

int main(int argc, char **argv)
{
	sg_rune_cx_cell_t cells[2];
	sg_rune_cx_facet_t facet;
	sg_rune_cx_incidence_t incidences[2];
	uint32_t cell_incidences[2] = { 0U, 1U };
	sg_rune_cx_vec3_t vertices[4];
	sg_rune_cx_portal_t portal;
	sg_rune_move_store_t store;
	sg_rune_move_law_t law;

	SG_RuneLawEngine(&law, 800.0f);
	sg_rune_move_crossing_t crossing;
	sg_rune_artifact_t source, decoded, loaded;
	unsigned char *image = NULL;
	size_t image_size = 0U;
	float one = 1.0f;
	const char *path = argc > 1 ? argv[1] : "sg_rune_artifact_test.rune";
	int os_error = 0;

	memset(cells, 0, sizeof(cells));
	memset(&facet, 0, sizeof(facet));
	memset(incidences, 0, sizeof(incidences));
	memset(&portal, 0, sizeof(portal));
	Vertex(&cells[0].bounds.mins, -64, -64, 0);
	Vertex(&cells[0].bounds.maxs, 0, 64, 128);
	Vertex(&cells[1].bounds.mins, 0, -64, 0);
	Vertex(&cells[1].bounds.maxs, 64, 64, 128);
	cells[0].incidences.first = 0U; cells[0].incidences.count = 1U;
	cells[1].incidences.first = 1U; cells[1].incidences.count = 1U;
	cells[0].valid_stances = cells[1].valid_stances = SG_RUNE_CX_STANCE_ALL;
	cells[0].semantics = cells[1].semantics = SG_RUNE_CX_CELL_SUPPORTED;
	Vertex(&vertices[0], 0, -64, 0); Vertex(&vertices[1], 0, 64, 0);
	Vertex(&vertices[2], 0, 64, 128); Vertex(&vertices[3], 0, -64, 128);
	memcpy(&facet.plane.normal_bits[0], &one, sizeof(one));
	facet.vertices.first = 0U; facet.vertices.count = 4U;
	facet.incidences.first = 0U; facet.incidences.count = 2U;
	facet.portal = 0U;
	facet.source.kind = SG_RUNE_CX_SOURCE_BSP_PLANE;
	incidences[0].cell = 0U; incidences[0].facet = 0U;
	incidences[0].side = SG_RUNE_CX_NEGATIVE_SIDE;
	incidences[1].cell = 1U; incidences[1].facet = 0U;
	incidences[1].side = SG_RUNE_CX_POSITIVE_SIDE;
	portal.facet = 0U; portal.source_incidence = 0U;
	portal.destination_incidence = 1U; portal.valid_stances = SG_RUNE_CX_STANCE_ALL;
	portal.clearance_q8 = 56U * SG_RUNE_CX_Q8_ONE;

	CHECK(SG_RuneMoveStoreInit(&store, &law));
	memset(&crossing, 0, sizeof(crossing));
	crossing.cell = 0U; crossing.other_cell = 1U; crossing.portal = 0U;
	crossing.cell_stances = crossing.other_stances = crossing.portal_stances =
		SG_RUNE_MOVE_STANDING | SG_RUNE_MOVE_CROUCHING;
	crossing.source_supported = crossing.target_supported = 1;
	crossing.vertical_facet = 1;
	CHECK(SG_RuneMoveEmitCrossing(&store, &crossing));
	crossing.cell = 1U; crossing.other_cell = 0U;
	CHECK(SG_RuneMoveEmitCrossing(&store, &crossing));

	memset(&source, 0, sizeof(source));
	source.identity.schema_id = SG_RUNE_ARTIFACT_SCHEMA_ID;
	source.identity.bsp_bytes = 12345U;
	source.identity.bsp_crc32 = 0xABCDEF01U;
	source.identity.entity_crc32 = 0x12345678U;
	source.law.standing_mins[0] = source.law.standing_mins[1] = -16.0f;
	source.law.standing_mins[2] = -24.0f;
	source.law.standing_maxs[0] = source.law.standing_maxs[1] = 16.0f;
	source.law.standing_maxs[2] = 32.0f;
	source.law.crouching_mins[0] = source.law.crouching_mins[1] = -16.0f;
	source.law.crouching_mins[2] = -24.0f;
	source.law.crouching_maxs[0] = source.law.crouching_maxs[1] = 16.0f;
	source.law.crouching_maxs[2] = 4.0f;
	SG_RuneLawEngine(&source.law, 800.0f);
	source.identity.law_crc32 = SG_RuneLawCrc(&source.law);
	source.complex.cells = cells; source.complex.cell_count = 2U;
	source.complex.facets = &facet; source.complex.facet_count = 1U;
	source.complex.incidences = incidences; source.complex.incidence_count = 2U;
	source.complex.cell_incidences = cell_incidences;
	source.complex.cell_incidence_count = 2U;
	source.complex.vertices = vertices; source.complex.vertex_count = 4U;
	source.complex.portals = &portal; source.complex.portal_count = 1U;
	SG_RuneMoveStoreView(&store, &source.movement);
	CHECK(source.movement.capability_count >= 2U);
	CHECK(SG_RuneArtifactValid(&source, NULL));

	CHECK(SG_RuneArtifactEncode(&source, &image, &image_size) ==
		SG_RUNE_ARTIFACT_OK);
	CHECK(image && image_size > 0U);
	CHECK(SG_RuneArtifactDecode(image, image_size, &decoded, NULL) ==
		SG_RUNE_ARTIFACT_OK);
	CHECK(decoded.complex.cell_count == 2U);
	CHECK(decoded.complex.portal_count == 1U);
	CHECK(decoded.movement.capability_count == source.movement.capability_count);
	CHECK(decoded.movement.profile_count == source.movement.profile_count);
	CHECK(decoded.movement.analytic.function_count ==
		source.movement.analytic.function_count);
	CHECK(decoded.law.gravity == 800.0f);
	CHECK(SG_RuneIdentityMatches(&decoded.identity, &source.identity));
	CHECK(SG_RuneLawMatches(&decoded.law, &source.law));
	CHECK(memcmp(decoded.complex.cells, cells, sizeof(cells)) == 0);
	{
		float inputs[SG_RUNE_FN_INPUT_COUNT];
		float cost = 0.0f;
		uint32_t profile = decoded.movement.capabilities[0].profile;

		memset(inputs, 0, sizeof(inputs));
		inputs[SG_RUNE_FN_INPUT_DISTANCE] = 100.0f;
		CHECK(SG_RuneFnEvaluate(&decoded.movement.analytic,
			decoded.movement.profiles[profile].cost, inputs, &cost));
		CHECK(cost > 0.0f);
	}

	/* Damage. */
	CHECK(SG_RuneArtifactDecode(image, image_size - 1U, &decoded, NULL) ==
		SG_RUNE_ARTIFACT_TRUNCATED);
	image[image_size - 1U] ^= 0x5AU;
	CHECK(SG_RuneArtifactDecode(image, image_size, &decoded, NULL) ==
		SG_RUNE_ARTIFACT_BAD_CHECKSUM);
	image[image_size - 1U] ^= 0x5AU;
	image[0] = 'X';
	CHECK(SG_RuneArtifactDecode(image, image_size, &decoded, NULL) ==
		SG_RUNE_ARTIFACT_BAD_MAGIC);
	image[0] = 'R';
	{
		sg_rune_move_capability_t bad = source.movement.capabilities[0];
		sg_rune_artifact_t broken = source;

		bad.cell = 7U;
		broken.movement.capabilities = &bad;
		broken.movement.capability_count = 1U;
		CHECK(!SG_RuneArtifactValid(&broken, NULL));
	}

	/* File. */
	CHECK(SG_RuneArtifactWriteFile(path, image, image_size, &os_error) ==
		SG_RUNE_ARTIFACT_OK);
	CHECK(SG_RuneArtifactLoadFile(path, &loaded, &os_error, NULL) ==
		SG_RUNE_ARTIFACT_OK);
	CHECK(loaded.owned != NULL);
	CHECK(loaded.image_size == image_size);
	CHECK(loaded.complex.portal_count == 1U);
	SG_RuneArtifactRelease(&loaded);
	remove(path);
	CHECK(SG_RuneArtifactLoadFile(path, &loaded, &os_error, NULL) ==
		SG_RUNE_ARTIFACT_FILE_ERROR);
	{
		char out[64];

		CHECK(SG_RuneArtifactPath(out, sizeof(out), "lmctf", "bctf01"));
		CHECK(strcmp(out, "lmctf/maps/bctf01.rune") == 0);
		CHECK(!SG_RuneArtifactPath(out, sizeof(out), "lmctf", "../x"));
		CHECK(!SG_RuneArtifactPath(out, 8U, "lmctf", "bctf01"));
	}

	free(image);
	SG_RuneMoveStoreFree(&store);
	if (failures)
	{
		fprintf(stderr, "sg_rune_artifact_test: %d failures\n", failures);
		return 1;
	}
	printf("sg_rune_artifact_test: ok\n");
	return 0;
}
