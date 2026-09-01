#include "../g_local.h"
#undef world

#include "../slipgate/sg_rune_compact_game.h"

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "../slipgate/sg_rune_compact_generation.h"
#include "../slipgate/sg_rune_install.h"

game_import_t gi;

static int failures;
static int host_ok = 1;
static int bsp_ok = 1;
static int construction_ok = 1;
static int generation_ok = 1;
static int generation_calls;
static int construction_destroy_calls;
static int bsp_destroy_calls;
static int progress_calls;
static sg_bsp_world_t world_storage;
static sg_host_law_construction_t *const construction_storage =
	(sg_host_law_construction_t *)(uintptr_t)1U;
static cvar_t game_directory;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

static void DebugPrint(char *format, ...)
	__attribute__((format(printf, 1, 2)));

static void DebugPrint(char *format, ...)
{
	va_list arguments;

	va_start(arguments, format);
	(void)vfprintf(stderr, format, arguments);
	va_end(arguments);
}

static cvar_t *Cvar(char *name, char *value, int flags)
{
	(void)value;
	(void)flags;
	return strcmp(name, "gamedir") == 0 ? &game_directory : NULL;
}

sg_host_law_result_t SG_HostLawProductionEnsureLevel(const char *mapname)
{
	sg_host_law_result_t result;

	memset(&result, 0, sizeof(result));
	CHECK(mapname != NULL && strcmp(mapname, "map") == 0);
	result.status = host_ok ? SG_HOST_LAW_OK : SG_HOST_LAW_HOST_UNAVAILABLE;
	return result;
}

int SG_BspWorldLoadFile(const char *path, sg_bsp_world_t **world_out,
	sg_bsp_error_t *error_out)
{
	CHECK(path != NULL && strcmp(path, "base/maps/map.bsp") == 0);
	if (!bsp_ok)
	{
		if (error_out != NULL)
			error_out->code = SG_BSP_ERROR_IO;
		return 0;
	}
	CHECK(world_out != NULL && *world_out == NULL);
	*world_out = &world_storage;
	return 1;
}

void SG_BspWorldDestroy(sg_bsp_world_t *world_value)
{
	CHECK(world_value == &world_storage);
	bsp_destroy_calls++;
}

sg_host_law_result_t SG_HostLawProductionConstructionIssue(
	const sg_host_collision_authority_t *authority,
	sg_host_law_construction_t **construction_out)
{
	sg_host_law_result_t result;

	memset(&result, 0, sizeof(result));
	CHECK(authority != NULL && authority->world == &world_storage &&
		construction_out != NULL && *construction_out == NULL);
	result.status = construction_ok ? SG_HOST_LAW_OK :
		SG_HOST_LAW_HOST_UNAVAILABLE;
	if (construction_ok)
		*construction_out = construction_storage;
	return result;
}

void SG_HostLawConstructionDestroy(sg_host_law_construction_t *construction)
{
	CHECK(construction == construction_storage);
	construction_destroy_calls++;
}

int SG_RuneInstallDestinationPath(char *output, size_t output_size,
	const char *directory, const char *map_name)
{
	int written;

	if (output == NULL || directory == NULL || map_name == NULL)
		return 0;
	written = snprintf(output, output_size, "%s/maps/%s.rune", directory,
		map_name);
	return written >= 0 && (size_t)written < output_size;
}

int SG_RuneCompactGenerationRun(
	const sg_rune_compact_generation_input_t *input,
	sg_rune_compact_generation_result_t *result_out)
{
	sg_rune_compact_generation_counts_t counts;

	generation_calls++;
	CHECK(input != NULL && input->builder_input.construction ==
		construction_storage && input->destination != NULL &&
		strcmp(input->destination, "base/maps/map.rune") == 0 &&
		input->collision_scene != NULL &&
		input->collision_scene->instance_count == 0U &&
		input->progress != NULL && result_out != NULL);
	memset(&counts, 0, sizeof(counts));
	input->progress(input->progress_context,
		SG_RUNE_COMPACT_GENERATION_STAGE_BUILDER, &counts);
	progress_calls++;
	memset(result_out, 0, sizeof(*result_out));
	result_out->stage = generation_ok ?
		SG_RUNE_COMPACT_GENERATION_STAGE_PUBLICATION :
		SG_RUNE_COMPACT_GENERATION_STAGE_BUILDER;
	result_out->error = generation_ok ?
		SG_RUNE_COMPACT_GENERATION_ERROR_NONE :
		SG_RUNE_COMPACT_GENERATION_ERROR_BUILDER_REJECTED;
	return generation_ok;
}

const char *SG_RuneCompactGenerationErrorString(
	sg_rune_compact_generation_error_code_t error)
{
	(void)error;
	return "generation error";
}

const char *SG_RuneCompactGenerationStageString(
	sg_rune_compact_generation_stage_t stage)
{
	(void)stage;
	return "stage";
}

const char *SG_RuneCompactWireErrorString(
	sg_rune_compact_wire_error_code_t code)
{
	(void)code;
	return "wire error";
}

const char *SG_HostLawStatusString(sg_host_law_status_t status)
{
	(void)status;
	return "host status";
}

const char *SG_HostLawFieldString(sg_host_law_field_t field)
{
	(void)field;
	return "host field";
}

static void Reset(void)
{
	host_ok = 1;
	bsp_ok = 1;
	construction_ok = 1;
	generation_ok = 1;
	generation_calls = 0;
	construction_destroy_calls = 0;
	bsp_destroy_calls = 0;
	progress_calls = 0;
}

static void TestDirectGeneration(void)
{
	Reset();
	CHECK(SG_RuneCompactGameGenerate("map"));
	CHECK(generation_calls == 1 && progress_calls == 1);
	CHECK(construction_destroy_calls == 1 && bsp_destroy_calls == 1);
}

static void TestFailClosed(void)
{
	Reset();
	host_ok = 0;
	CHECK(!SG_RuneCompactGameGenerate("map"));
	CHECK(generation_calls == 0 && construction_destroy_calls == 0 &&
		bsp_destroy_calls == 0);
	Reset();
	construction_ok = 0;
	CHECK(!SG_RuneCompactGameGenerate("map"));
	CHECK(generation_calls == 0 && construction_destroy_calls == 0 &&
		bsp_destroy_calls == 1);
	Reset();
	generation_ok = 0;
	CHECK(!SG_RuneCompactGameGenerate("map"));
	CHECK(generation_calls == 1 && construction_destroy_calls == 1 &&
		bsp_destroy_calls == 1);
}

int main(void)
{
	memset(&gi, 0, sizeof(gi));
	gi.dprintf = DebugPrint;
	gi.cvar = Cvar;
	memset(&game_directory, 0, sizeof(game_directory));
	game_directory.string = "base";
	memset(&world_storage, 0, sizeof(world_storage));
	world_storage.content_identity.bytes[0] = 1U;
	TestDirectGeneration();
	TestFailClosed();
	if (failures != 0)
	{
		fprintf(stderr, "%d compact game generation tests failed\n", failures);
		return 1;
	}
	puts("sg_rune_compact_game_test: PASS");
	return 0;
}
