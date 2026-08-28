/* Test adapter for differential comparison with the production v2 loader. */
#include "slipgate/sg_rune_v2_artifact_loader.h"

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int HexDigit(char value, uint8_t *output)
{
	if (value >= '0' && value <= '9')
		*output = (uint8_t)(value - '0');
	else if (value >= 'a' && value <= 'f')
		*output = (uint8_t)(value - 'a' + 10);
	else if (value >= 'A' && value <= 'F')
		*output = (uint8_t)(value - 'A' + 10);
	else
		return 0;
	return 1;
}

static int ParseIdentity(const char *text, sg_rune_v2_content_id_t *output)
{
	size_t index;

	if (strlen(text) != SG_RUNE_V2_CONTENT_ID_BYTES * 2U)
		return 0;
	for (index = 0U; index < SG_RUNE_V2_CONTENT_ID_BYTES; index++)
	{
		uint8_t high;
		uint8_t low;

		if (!HexDigit(text[index * 2U], &high) ||
			!HexDigit(text[index * 2U + 1U], &low))
			return 0;
		output->bytes[index] = (uint8_t)(high * 16U + low);
	}
	return 1;
}

static int ParseU64(const char *text, uint64_t *output)
{
	char *end;
	uintmax_t value;

	errno = 0;
	value = strtoumax(text, &end, 10);
	if (errno != 0 || *text == '\0' || *text == '-' || *end != '\0' ||
		value > UINT64_MAX)
		return 0;
	*output = (uint64_t)value;
	return 1;
}

static void PrintIdentity(const sg_rune_v2_content_id_t *identity)
{
	unsigned int index;

	for (index = 0U; index < SG_RUNE_V2_CONTENT_ID_BYTES; index++)
		(void)printf("%02x", identity->bytes[index]);
}

static void PrintSummary(const sg_rune_v2_artifact_snapshot_t *snapshot)
{
	const sg_rune_model_t *model = &snapshot->model;

	(void)printf("{\"affordances\":%" PRIu32 ",\"bsp\":\"",
		model->affordance_count);
	PrintIdentity(&snapshot->binding.bsp_identity);
	(void)printf("\",\"cells\":%" PRIu32 ",\"generation\":%" PRIu64
		",\"kernels\":%" PRIu32 ",\"landmarks\":%" PRIu32
		",\"mechanisms\":%" PRIu32 ",\"phases\":%" PRIu32
		",\"planes\":%" PRIu32 ",\"portal_vertices\":%" PRIu32
		",\"portals\":%" PRIu32 ",\"schema\":\"",
		model->cell_count, snapshot->binding.generation, model->kernel_count,
		model->landmark_count, model->mechanism_count, model->phase_count,
		model->plane_count, model->portal_vertex_count, model->portal_count);
	PrintIdentity(&snapshot->binding.schema_identity);
	(void)printf("\",\"surfaces\":%" PRIu32 ",\"transitions\":%" PRIu32
		"}\n", model->surface_count, model->phase_transition_count);
}

int main(int argc, char **argv)
{
	sg_rune_v2_artifact_binding_t binding;
	sg_rune_v2_content_id_t exact_identity;
	sg_rune_v2_artifact_loader_t loader =
		SG_RUNE_V2_ARTIFACT_LOADER_INITIALIZER;
	sg_rune_v2_artifact_load_result_t result;
	const sg_rune_v2_artifact_snapshot_t *snapshot;

	if (argc != 12 || strcmp(argv[1], "--generation") != 0 ||
		strcmp(argv[3], "--bsp-id") != 0 || strcmp(argv[5], "--schema-id") != 0 ||
		strcmp(argv[7], "--artifact-id") != 0 ||
		strcmp(argv[9], "--exact-artifact-id") != 0)
		return 2;
	memset(&binding, 0, sizeof(binding));
	memset(&exact_identity, 0, sizeof(exact_identity));
	if (!ParseU64(argv[2], &binding.generation) ||
		!ParseIdentity(argv[4], &binding.bsp_identity) ||
		!ParseIdentity(argv[6], &binding.schema_identity) ||
		!ParseIdentity(argv[8], &binding.artifact_identity) ||
		!ParseIdentity(argv[10], &exact_identity) ||
		!SG_RuneV2ArtifactLoaderInit(&loader, NULL))
		return 2;
	result = SG_RuneV2ArtifactLoaderLoadFile(&loader, argv[11], &binding,
		&exact_identity);
	if (result.diagnostic != SG_RUNE_V2_LOADER_OK)
	{
		SG_RuneV2ArtifactLoaderDestroy(&loader);
		return 1;
	}
	snapshot = SG_RuneV2ArtifactLoaderSnapshot(&loader);
	if (!snapshot)
	{
		SG_RuneV2ArtifactLoaderDestroy(&loader);
		return 1;
	}
	PrintSummary(snapshot);
	SG_RuneV2ArtifactLoaderDestroy(&loader);
	return 0;
}
