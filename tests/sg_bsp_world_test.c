#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../slipgate/sg_bsp_world.h"

#define HEADER_BYTES (8U + SG_BSP_LUMP_COUNT * 8U)
#define FIXTURE_CAPACITY 4096U

static int failures;

#define CHECK(expression) do { \
	if (!(expression)) { \
		fprintf(stderr, "%s:%d: check failed: %s\n", \
			__FILE__, __LINE__, #expression); \
		failures++; \
	} \
} while (0)

typedef struct fixture_s
{
	uint8_t bytes[FIXTURE_CAPACITY];
	uint32_t size;
	uint32_t offsets[SG_BSP_LUMP_COUNT];
	uint32_t lengths[SG_BSP_LUMP_COUNT];
} fixture_t;

static uint32_t ReadU32(const uint8_t *bytes)
{
	return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
		((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static int32_t ReadI32(const uint8_t *bytes)
{
	return (int32_t)ReadU32(bytes);
}

static float ReadFloat(const uint8_t *bytes)
{
	uint32_t bits = ReadU32(bytes);
	float value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

static void WriteU16(uint8_t *bytes, uint16_t value)
{
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
}

static void WriteI16(uint8_t *bytes, int16_t value)
{
	WriteU16(bytes, (uint16_t)value);
}

static void WriteU32(uint8_t *bytes, uint32_t value)
{
	bytes[0] = (uint8_t)value;
	bytes[1] = (uint8_t)(value >> 8);
	bytes[2] = (uint8_t)(value >> 16);
	bytes[3] = (uint8_t)(value >> 24);
}

static void WriteI32(uint8_t *bytes, int32_t value)
{
	WriteU32(bytes, (uint32_t)value);
}

static void WriteFloat(uint8_t *bytes, float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	WriteU32(bytes, bits);
}

static uint8_t *AddLump(fixture_t *fixture, sg_bsp_lump_t lump,
	uint32_t length)
{
	uint8_t *result;

	CHECK(fixture->size <= FIXTURE_CAPACITY - length);
	fixture->offsets[lump] = fixture->size;
	fixture->lengths[lump] = length;
	result = fixture->bytes + fixture->size;
	fixture->size += length;
	memset(result, 0, length);
	return result;
}

static void FinishHeader(fixture_t *fixture)
{
	uint32_t lump;

	memcpy(fixture->bytes, "IBSP", 4);
	WriteU32(fixture->bytes + 4, SG_BSP_VERSION);
	for (lump = 0; lump < SG_BSP_LUMP_COUNT; lump++)
	{
		WriteU32(fixture->bytes + 8U + lump * 8U,
			fixture->offsets[lump]);
		WriteU32(fixture->bytes + 12U + lump * 8U,
			fixture->lengths[lump]);
	}
}

static fixture_t ValidFixture(void)
{
	fixture_t fixture;
	uint8_t *record;
	uint32_t lump;

	memset(&fixture, 0, sizeof(fixture));
	fixture.size = HEADER_BYTES;
	for (lump = 0; lump < SG_BSP_LUMP_COUNT; lump++)
		fixture.offsets[lump] = HEADER_BYTES;

	record = AddLump(&fixture, SG_BSP_LUMP_ENTITIES, 4);
	memcpy(record, "{}\n", 4);

	record = AddLump(&fixture, SG_BSP_LUMP_PLANES, 20);
	WriteFloat(record + 8, 1.0f);
	WriteI32(record + 16, 2);

	record = AddLump(&fixture, SG_BSP_LUMP_VERTICES, 36);
	WriteFloat(record + 0, -16.0f);
	WriteFloat(record + 4, -16.0f);
	WriteFloat(record + 12, 16.0f);
	WriteFloat(record + 16, -16.0f);
	WriteFloat(record + 24, 0.0f);
	WriteFloat(record + 28, 16.0f);

	record = AddLump(&fixture, SG_BSP_LUMP_VISIBILITY, 13);
	WriteU32(record, 1);
	WriteU32(record + 4, 12);
	WriteU32(record + 8, 12);
	record[12] = 1;

	record = AddLump(&fixture, SG_BSP_LUMP_NODES, 28);
	WriteU32(record, 0);
	WriteI32(record + 4, -1);
	WriteI32(record + 8, -1);
	WriteI16(record + 12, -16);
	WriteI16(record + 14, -16);
	WriteI16(record + 16, -16);
	WriteI16(record + 18, 16);
	WriteI16(record + 20, 16);
	WriteI16(record + 22, 16);
	WriteU16(record + 24, 0);
	WriteU16(record + 26, 1);

	record = AddLump(&fixture, SG_BSP_LUMP_TEXINFO, 76);
	WriteFloat(record + 0, 1.0f);
	WriteFloat(record + 20, 1.0f);
	WriteI32(record + 32, 4);
	WriteI32(record + 36, 7);
	memcpy(record + 40, "stone", 5);
	WriteI32(record + 72, -1);

	record = AddLump(&fixture, SG_BSP_LUMP_FACES, 20);
	WriteU16(record, 0);
	WriteI16(record + 2, 0);
	WriteI32(record + 4, 0);
	WriteI16(record + 8, 3);
	WriteI16(record + 10, 0);
	record[12] = 0;
	record[13] = record[14] = record[15] = 255;
	WriteI32(record + 16, 0);

	record = AddLump(&fixture, SG_BSP_LUMP_LIGHTING, 3);
	record[0] = 10;
	record[1] = 20;
	record[2] = 30;

	record = AddLump(&fixture, SG_BSP_LUMP_LEAVES, 28);
	WriteI32(record, 1);
	WriteI16(record + 4, 0);
	WriteI16(record + 6, 0);
	WriteI16(record + 8, -16);
	WriteI16(record + 10, -16);
	WriteI16(record + 12, -16);
	WriteI16(record + 14, 16);
	WriteI16(record + 16, 16);
	WriteI16(record + 18, 16);
	WriteU16(record + 20, 0);
	WriteU16(record + 22, 1);
	WriteU16(record + 24, 0);
	WriteU16(record + 26, 1);

	record = AddLump(&fixture, SG_BSP_LUMP_LEAF_FACES, 2);
	WriteU16(record, 0);
	record = AddLump(&fixture, SG_BSP_LUMP_LEAF_BRUSHES, 2);
	WriteU16(record, 0);

	record = AddLump(&fixture, SG_BSP_LUMP_EDGES, 12);
	WriteU16(record + 0, 0);
	WriteU16(record + 2, 1);
	WriteU16(record + 4, 1);
	WriteU16(record + 6, 2);
	WriteU16(record + 8, 2);
	WriteU16(record + 10, 0);

	record = AddLump(&fixture, SG_BSP_LUMP_SURFEDGES, 12);
	WriteI32(record + 0, 0);
	WriteI32(record + 4, 1);
	WriteI32(record + 8, 2);

	record = AddLump(&fixture, SG_BSP_LUMP_MODELS, 96);
	WriteFloat(record + 0, -16.0f);
	WriteFloat(record + 4, -16.0f);
	WriteFloat(record + 8, -16.0f);
	WriteFloat(record + 12, 16.0f);
	WriteFloat(record + 16, 16.0f);
	WriteFloat(record + 20, 16.0f);
	WriteI32(record + 36, 0);
	WriteI32(record + 40, 0);
	WriteI32(record + 44, 1);
	WriteFloat(record + 48, -8.0f);
	WriteFloat(record + 52, -8.0f);
	WriteFloat(record + 56, -8.0f);
	WriteFloat(record + 60, 8.0f);
	WriteFloat(record + 64, 8.0f);
	WriteFloat(record + 68, 8.0f);
	WriteFloat(record + 72, 32.0f);
	WriteI32(record + 84, -1);
	WriteI32(record + 88, 0);
	WriteI32(record + 92, 1);

	record = AddLump(&fixture, SG_BSP_LUMP_BRUSHES, 12);
	WriteI32(record, 0);
	WriteI32(record + 4, 1);
	WriteI32(record + 8, 1);

	record = AddLump(&fixture, SG_BSP_LUMP_BRUSH_SIDES, 4);
	WriteU16(record, 0);
	WriteI16(record + 2, 0);

	(void)AddLump(&fixture, SG_BSP_LUMP_POP, 256);
	record = AddLump(&fixture, SG_BSP_LUMP_AREAS, 16);
	WriteI32(record, 1);
	WriteI32(record + 4, 0);
	WriteI32(record + 8, 1);
	WriteI32(record + 12, 1);
	record = AddLump(&fixture, SG_BSP_LUMP_AREAPORTALS, 16);
	WriteI32(record, 0);
	WriteI32(record + 4, 1);
	WriteI32(record + 8, 0);
	WriteI32(record + 12, 0);
	FinishHeader(&fixture);
	return fixture;
}

static void ExpectFailure(fixture_t fixture, sg_bsp_error_code_t code,
	sg_bsp_lump_t lump)
{
	sg_bsp_world_t *world = NULL;
	sg_bsp_error_t error = { SG_BSP_ERROR_NONE, SG_BSP_LUMP_ENTITIES, 0 };

	CHECK(!SG_BspWorldLoadMemory(fixture.bytes, fixture.size, &world, &error));
	CHECK(world == NULL);
	CHECK(error.code == code);
	CHECK(error.lump == lump);
}

static void TestValidFixture(void)
{
	fixture_t fixture = ValidFixture();
	sg_bsp_world_t *world = NULL;
	sg_bsp_error_t error;

	CHECK(SG_BspWorldLoadMemory(fixture.bytes, fixture.size, &world, &error));
	CHECK(error.code == SG_BSP_ERROR_NONE);
	CHECK(world != NULL);
	if (!world)
		return;
	CHECK(world->entity_byte_count == 4);
	CHECK(memcmp(world->entities, "{}\n", 4) == 0);
	CHECK(world->entities[4] == 0);
	CHECK(world->plane_count == 1);
	CHECK(world->vertex_count == 3);
	CHECK(world->visibility.cluster_count == 1);
	CHECK(world->visibility.byte_count == 13);
	CHECK(world->node_count == 1);
	CHECK(world->texinfo_count == 1);
	CHECK(world->face_count == 1);
	CHECK(world->lighting_byte_count == 3);
	CHECK(world->leaf_count == 1);
	CHECK(world->leaf_face_count == 1);
	CHECK(world->leaf_brush_count == 1);
	CHECK(world->edge_count == 3);
	CHECK(world->surfedge_count == 3);
	CHECK(world->model_count == 2);
	CHECK(world->brush_count == 1);
	CHECK(world->brush_side_count == 1);
	CHECK(world->area_count == 2);
	CHECK(world->areaportal_count == 2);
	CHECK(world->planes[0].normal.value[2] == 1.0f);
	CHECK(world->texinfos[0].flags == 4);
	CHECK(world->faces[0].light_offset == 0);
	CHECK(world->leaves[0].contents == 1);
	CHECK(world->brushes[0].contents == 1);
	CHECK(world->models[0].mins.value[0] == -16.0f);
	CHECK(world->models[0].maxs.value[2] == 16.0f);
	CHECK(world->models[1].headnode == -1);
	CHECK(world->models[1].origin.value[0] == 32.0f);
	CHECK(world->areas[1].first_areaportal == 1);
	CHECK(world->areaportals[0].other_area == 1);
	memset(fixture.bytes, 0, fixture.size);
	CHECK(memcmp(world->entities, "{}\n", 4) == 0);
	CHECK(world->lighting[0] == 10);
	CHECK(world->visibility.bytes[12] == 1);
	CHECK(world->vertices[1].point.value[0] == 16.0f);
	SG_BspWorldDestroy(world);
}

static void TestHeaderFailures(void)
{
	fixture_t fixture = ValidFixture();
	sg_bsp_world_t *world = NULL;
	sg_bsp_error_t error;
	size_t size;

	for (size = 0; size < fixture.size; size++)
	{
		world = NULL;
		CHECK(!SG_BspWorldLoadMemory(fixture.bytes, size, &world, &error));
		CHECK(world == NULL);
	}
	CHECK(!SG_BspWorldLoadMemory(fixture.bytes, HEADER_BYTES - 1,
		&world, &error));
	CHECK(error.code == SG_BSP_ERROR_TRUNCATED_HEADER);
	fixture = ValidFixture();
	fixture.bytes[0] = 'X';
	ExpectFailure(fixture, SG_BSP_ERROR_BAD_MAGIC, SG_BSP_LUMP_ENTITIES);
	fixture = ValidFixture();
	WriteU32(fixture.bytes + 4, 39);
	ExpectFailure(fixture, SG_BSP_ERROR_UNSUPPORTED_VERSION,
		SG_BSP_LUMP_ENTITIES);
	fixture = ValidFixture();
	WriteU32(fixture.bytes + 8U + SG_BSP_LUMP_PLANES * 8U, UINT32_MAX);
	ExpectFailure(fixture, SG_BSP_ERROR_BAD_LUMP, SG_BSP_LUMP_PLANES);
	fixture = ValidFixture();
	WriteU32(fixture.bytes + 12U + SG_BSP_LUMP_PLANES * 8U, 19);
	ExpectFailure(fixture, SG_BSP_ERROR_BAD_LUMP, SG_BSP_LUMP_PLANES);
	fixture = ValidFixture();
	WriteU32(fixture.bytes + 8U + SG_BSP_LUMP_VERTICES * 8U,
		fixture.offsets[SG_BSP_LUMP_PLANES]);
	ExpectFailure(fixture, SG_BSP_ERROR_BAD_LUMP, SG_BSP_LUMP_VERTICES);
}

static void TestReferenceFailures(void)
{
	fixture_t fixture = ValidFixture();
	uint8_t *record;

	record = fixture.bytes + fixture.offsets[SG_BSP_LUMP_NODES];
	WriteI32(record + 4, -2);
	ExpectFailure(fixture, SG_BSP_ERROR_INVALID_REFERENCE, SG_BSP_LUMP_NODES);

	fixture = ValidFixture();
	record = fixture.bytes + fixture.offsets[SG_BSP_LUMP_FACES];
	WriteI32(record + 4, 2);
	ExpectFailure(fixture, SG_BSP_ERROR_INVALID_REFERENCE, SG_BSP_LUMP_FACES);

	fixture = ValidFixture();
	record = fixture.bytes + fixture.offsets[SG_BSP_LUMP_SURFEDGES];
	WriteI32(record, INT32_MIN);
	ExpectFailure(fixture, SG_BSP_ERROR_INVALID_REFERENCE,
		SG_BSP_LUMP_SURFEDGES);

	fixture = ValidFixture();
	record = fixture.bytes + fixture.offsets[SG_BSP_LUMP_LEAF_BRUSHES];
	WriteU16(record, 1);
	ExpectFailure(fixture, SG_BSP_ERROR_INVALID_REFERENCE,
		SG_BSP_LUMP_LEAF_BRUSHES);

	fixture = ValidFixture();
	record = fixture.bytes + fixture.offsets[SG_BSP_LUMP_FACES];
	WriteI32(record + 16, 3);
	ExpectFailure(fixture, SG_BSP_ERROR_INVALID_REFERENCE, SG_BSP_LUMP_FACES);

	fixture = ValidFixture();
	record = fixture.bytes + fixture.offsets[SG_BSP_LUMP_AREAPORTALS];
	WriteI32(record + 4, 2);
	ExpectFailure(fixture, SG_BSP_ERROR_INVALID_REFERENCE,
		SG_BSP_LUMP_AREAPORTALS);

	fixture = ValidFixture();
	record = fixture.bytes + fixture.offsets[SG_BSP_LUMP_NODES];
	WriteI32(record + 4, 0);
	ExpectFailure(fixture, SG_BSP_ERROR_INVALID_TREE, SG_BSP_LUMP_NODES);
}

static void TestGeometryAndVisibilityFailures(void)
{
	fixture_t fixture = ValidFixture();
	uint8_t *record;

	record = fixture.bytes + fixture.offsets[SG_BSP_LUMP_VERTICES];
	WriteU32(record, UINT32_C(0x7fc00000));
	ExpectFailure(fixture, SG_BSP_ERROR_NONFINITE_GEOMETRY,
		SG_BSP_LUMP_VERTICES);

	fixture = ValidFixture();
	record = fixture.bytes + fixture.offsets[SG_BSP_LUMP_VISIBILITY];
	record[12] = 0;
	ExpectFailure(fixture, SG_BSP_ERROR_INVALID_VISIBILITY,
		SG_BSP_LUMP_VISIBILITY);

	fixture = ValidFixture();
	record = fixture.bytes + fixture.offsets[SG_BSP_LUMP_VISIBILITY];
	WriteU32(record, UINT32_MAX);
	ExpectFailure(fixture, SG_BSP_ERROR_SIZE_OVERFLOW,
		SG_BSP_LUMP_VISIBILITY);

	fixture = ValidFixture();
	record = fixture.bytes + fixture.offsets[SG_BSP_LUMP_MODELS];
	WriteFloat(record, 17.0f);
	ExpectFailure(fixture, SG_BSP_ERROR_INVALID_GEOMETRY,
		SG_BSP_LUMP_MODELS);

	fixture = ValidFixture();
	record = fixture.bytes + fixture.offsets[SG_BSP_LUMP_PLANES];
	WriteFloat(record + 8, 0.0f);
	ExpectFailure(fixture, SG_BSP_ERROR_INVALID_GEOMETRY,
		SG_BSP_LUMP_PLANES);
}

static void TestMapWithoutVisibility(void)
{
	fixture_t fixture = ValidFixture();
	sg_bsp_world_t *world = NULL;
	sg_bsp_error_t error;

	WriteU32(fixture.bytes + 12U + SG_BSP_LUMP_VISIBILITY * 8U, 0);
	CHECK(SG_BspWorldLoadMemory(fixture.bytes, fixture.size, &world, &error));
	CHECK(world != NULL);
	if (world)
	{
		CHECK(world->visibility.byte_count == 0);
		CHECK(world->visibility.cluster_count == 0);
		CHECK(world->leaves[0].cluster == 0);
	}
	SG_BspWorldDestroy(world);
}

static void TestOutputOwnershipContract(void)
{
	fixture_t fixture = ValidFixture();
	sg_bsp_world_t sentinel;
	sg_bsp_world_t *world = &sentinel;
	sg_bsp_error_t error;

	CHECK(!SG_BspWorldLoadMemory(fixture.bytes, fixture.size, &world, &error));
	CHECK(world == &sentinel);
	CHECK(error.code == SG_BSP_ERROR_INVALID_ARGUMENT);
	CHECK(!SG_BspWorldLoadFile("/definitely/not/a/bsp", &world, &error));
	CHECK(error.code == SG_BSP_ERROR_INVALID_ARGUMENT);
	world = NULL;
	CHECK(!SG_BspWorldLoadFile("/definitely/not/a/bsp", &world, &error));
	CHECK(error.code == SG_BSP_ERROR_IO);
	CHECK(!SG_BspWorldLoadMemory(NULL, fixture.size, &world, &error));
	CHECK(error.code == SG_BSP_ERROR_INVALID_ARGUMENT);
	CHECK(!SG_BspWorldLoadMemory(fixture.bytes, fixture.size, NULL, &error));
	CHECK(error.code == SG_BSP_ERROR_INVALID_ARGUMENT);
	SG_BspWorldDestroy(NULL);
}

static int CompareRealBsp(const char *path)
{
	FILE *file;
	uint8_t *bytes = NULL;
	long length;
	sg_bsp_world_t *world = NULL;
	sg_bsp_world_t *file_world = NULL;
	sg_bsp_error_t error;
	uint32_t model_offset, leaf_offset, brush_offset;
	int ok = 0;

	file = fopen(path, "rb");
	if (!file || fseek(file, 0, SEEK_END) != 0 ||
		(length = ftell(file)) < 0 || fseek(file, 0, SEEK_SET) != 0)
		goto done;
	bytes = malloc((size_t)length);
	if (!bytes || fread(bytes, 1, (size_t)length, file) != (size_t)length)
		goto done;
	if (!SG_BspWorldLoadMemory(bytes, (size_t)length, &world, &error))
	{
		fprintf(stderr, "%s: real BSP rejected: %s lump=%u record=%u\n",
			path, SG_BspWorldErrorString(error.code), (unsigned)error.lump,
			(unsigned)error.record);
		goto done;
	}
	if (!SG_BspWorldLoadFile(path, &file_world, &error))
	{
		fprintf(stderr, "%s: file BSP load rejected: %s lump=%u record=%u\n",
			path, SG_BspWorldErrorString(error.code), (unsigned)error.lump,
			(unsigned)error.record);
		goto done;
	}
	CHECK(file_world->plane_count == world->plane_count);
	CHECK(file_world->node_count == world->node_count);
	CHECK(file_world->leaf_count == world->leaf_count);
	CHECK(file_world->brush_count == world->brush_count);
	CHECK(file_world->model_count == world->model_count);
	CHECK(file_world->models[0].mins.value[0] ==
		world->models[0].mins.value[0]);
	CHECK(file_world->models[0].maxs.value[2] ==
		world->models[0].maxs.value[2]);
	CHECK(file_world->leaves[0].contents == world->leaves[0].contents);
	CHECK(world->plane_count ==
		ReadU32(bytes + 12U + SG_BSP_LUMP_PLANES * 8U) / 20U);
	CHECK(world->node_count ==
		ReadU32(bytes + 12U + SG_BSP_LUMP_NODES * 8U) / 28U);
	CHECK(world->leaf_count ==
		ReadU32(bytes + 12U + SG_BSP_LUMP_LEAVES * 8U) / 28U);
	CHECK(world->brush_count ==
		ReadU32(bytes + 12U + SG_BSP_LUMP_BRUSHES * 8U) / 12U);
	CHECK(world->model_count ==
		ReadU32(bytes + 12U + SG_BSP_LUMP_MODELS * 8U) / 48U);
	model_offset = ReadU32(bytes + 8U + SG_BSP_LUMP_MODELS * 8U);
	CHECK(world->models[0].mins.value[0] == ReadFloat(bytes + model_offset));
	CHECK(world->models[0].maxs.value[2] ==
		ReadFloat(bytes + model_offset + 20U));
	leaf_offset = ReadU32(bytes + 8U + SG_BSP_LUMP_LEAVES * 8U);
	CHECK(world->leaves[0].contents == ReadI32(bytes + leaf_offset));
	brush_offset = ReadU32(bytes + 8U + SG_BSP_LUMP_BRUSHES * 8U);
	if (world->brush_count)
		CHECK(world->brushes[0].contents == ReadI32(bytes + brush_offset + 8U));
	fprintf(stdout,
		"real BSP %s: planes=%u nodes=%u leaves=%u brushes=%u models=%u "
		"bounds=[%.0f %.0f %.0f]-[%.0f %.0f %.0f] leaf0_contents=%d\n",
		path, world->plane_count, world->node_count, world->leaf_count,
		world->brush_count, world->model_count,
		world->models[0].mins.value[0], world->models[0].mins.value[1],
		world->models[0].mins.value[2], world->models[0].maxs.value[0],
		world->models[0].maxs.value[1], world->models[0].maxs.value[2],
		world->leaves[0].contents);
	ok = 1;

done:
	SG_BspWorldDestroy(file_world);
	SG_BspWorldDestroy(world);
	free(bytes);
	if (file)
		fclose(file);
	return ok;
}

int main(int argc, char **argv)
{
	TestValidFixture();
	TestHeaderFailures();
	TestReferenceFailures();
	TestGeometryAndVisibilityFailures();
	TestMapWithoutVisibility();
	TestOutputOwnershipContract();
	if (argc > 1)
		CHECK(CompareRealBsp(argv[1]));
	if (failures)
	{
		fprintf(stderr, "%d BSP world test(s) failed\n", failures);
		return 1;
	}
	puts("BSP world tests passed");
	return 0;
}
