/* Quake II IBSP v38 reader and owned static-world representation. */
#include "sg_bsp_world.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BSP_HEADER_BYTES (8U + SG_BSP_LUMP_COUNT * 8U)
#define BSP_PLANE_BYTES 20U
#define BSP_VERTEX_BYTES 12U
#define BSP_NODE_BYTES 28U
#define BSP_TEXINFO_BYTES 76U
#define BSP_FACE_BYTES 20U
#define BSP_LEAF_BYTES 28U
#define BSP_LEAF_FACE_BYTES 2U
#define BSP_LEAF_BRUSH_BYTES 2U
#define BSP_EDGE_BYTES 4U
#define BSP_SURFEDGE_BYTES 4U
#define BSP_MODEL_BYTES 48U
#define BSP_BRUSH_BYTES 12U
#define BSP_BRUSH_SIDE_BYTES 4U
#define BSP_AREA_BYTES 8U
#define BSP_AREAPORTAL_BYTES 8U
#define BSP_CONTENTS_SOLID INT32_C(1)
#define BSP_MAX_FACE_EDGES UINT32_C(4096)
#define BSP_PLANE_X INT32_C(0)
#define BSP_PLANE_Y INT32_C(1)
#define BSP_PLANE_Z INT32_C(2)
#define BSP_PLANE_NON_AXIAL INT32_C(6)

typedef struct bsp_sha256_s
{
	uint32_t state[8];
	uint64_t bit_count;
	uint8_t block[64];
	size_t block_bytes;
} bsp_sha256_t;

static uint32_t BspSha256RotateRight(uint32_t value, unsigned int count)
{
	return (value >> count) | (value << (32U - count));
}

static void BspSha256Transform(bsp_sha256_t *context)
{
	static const uint32_t constants[64] = {
		UINT32_C(0x428a2f98), UINT32_C(0x71374491),
		UINT32_C(0xb5c0fbcf), UINT32_C(0xe9b5dba5),
		UINT32_C(0x3956c25b), UINT32_C(0x59f111f1),
		UINT32_C(0x923f82a4), UINT32_C(0xab1c5ed5),
		UINT32_C(0xd807aa98), UINT32_C(0x12835b01),
		UINT32_C(0x243185be), UINT32_C(0x550c7dc3),
		UINT32_C(0x72be5d74), UINT32_C(0x80deb1fe),
		UINT32_C(0x9bdc06a7), UINT32_C(0xc19bf174),
		UINT32_C(0xe49b69c1), UINT32_C(0xefbe4786),
		UINT32_C(0x0fc19dc6), UINT32_C(0x240ca1cc),
		UINT32_C(0x2de92c6f), UINT32_C(0x4a7484aa),
		UINT32_C(0x5cb0a9dc), UINT32_C(0x76f988da),
		UINT32_C(0x983e5152), UINT32_C(0xa831c66d),
		UINT32_C(0xb00327c8), UINT32_C(0xbf597fc7),
		UINT32_C(0xc6e00bf3), UINT32_C(0xd5a79147),
		UINT32_C(0x06ca6351), UINT32_C(0x14292967),
		UINT32_C(0x27b70a85), UINT32_C(0x2e1b2138),
		UINT32_C(0x4d2c6dfc), UINT32_C(0x53380d13),
		UINT32_C(0x650a7354), UINT32_C(0x766a0abb),
		UINT32_C(0x81c2c92e), UINT32_C(0x92722c85),
		UINT32_C(0xa2bfe8a1), UINT32_C(0xa81a664b),
		UINT32_C(0xc24b8b70), UINT32_C(0xc76c51a3),
		UINT32_C(0xd192e819), UINT32_C(0xd6990624),
		UINT32_C(0xf40e3585), UINT32_C(0x106aa070),
		UINT32_C(0x19a4c116), UINT32_C(0x1e376c08),
		UINT32_C(0x2748774c), UINT32_C(0x34b0bcb5),
		UINT32_C(0x391c0cb3), UINT32_C(0x4ed8aa4a),
		UINT32_C(0x5b9cca4f), UINT32_C(0x682e6ff3),
		UINT32_C(0x748f82ee), UINT32_C(0x78a5636f),
		UINT32_C(0x84c87814), UINT32_C(0x8cc70208),
		UINT32_C(0x90befffa), UINT32_C(0xa4506ceb),
		UINT32_C(0xbef9a3f7), UINT32_C(0xc67178f2)
	};
	uint32_t schedule[64];
	uint32_t words[8];
	uint32_t index;

	for (index = 0U; index < 16U; index++)
		schedule[index] = ((uint32_t)context->block[index * 4U] << 24) |
			((uint32_t)context->block[index * 4U + 1U] << 16) |
			((uint32_t)context->block[index * 4U + 2U] << 8) |
			(uint32_t)context->block[index * 4U + 3U];
	for (index = 16U; index < 64U; index++)
	{
		uint32_t first = BspSha256RotateRight(schedule[index - 15U], 7U) ^
			BspSha256RotateRight(schedule[index - 15U], 18U) ^
			(schedule[index - 15U] >> 3U);
		uint32_t second = BspSha256RotateRight(schedule[index - 2U], 17U) ^
			BspSha256RotateRight(schedule[index - 2U], 19U) ^
			(schedule[index - 2U] >> 10U);

		schedule[index] = schedule[index - 16U] + first +
			schedule[index - 7U] + second;
	}
	for (index = 0U; index < 8U; index++)
		words[index] = context->state[index];
	for (index = 0U; index < 64U; index++)
	{
		uint32_t first = BspSha256RotateRight(words[0], 2U) ^
			BspSha256RotateRight(words[0], 13U) ^
			BspSha256RotateRight(words[0], 22U);
		uint32_t majority = (words[0] & words[1]) ^
			(words[0] & words[2]) ^ (words[1] & words[2]);
		uint32_t second = BspSha256RotateRight(words[4], 6U) ^
			BspSha256RotateRight(words[4], 11U) ^
			BspSha256RotateRight(words[4], 25U);
		uint32_t choice = (words[4] & words[5]) ^
			(~words[4] & words[6]);
		uint32_t temporary1 = words[7] + second + choice +
			constants[index] + schedule[index];
		uint32_t temporary2 = first + majority;

		words[7] = words[6];
		words[6] = words[5];
		words[5] = words[4];
		words[4] = words[3] + temporary1;
		words[3] = words[2];
		words[2] = words[1];
		words[1] = words[0];
		words[0] = temporary1 + temporary2;
	}
	for (index = 0U; index < 8U; index++)
		context->state[index] += words[index];
}

static void BspSha256Init(bsp_sha256_t *context)
{
	static const uint32_t initial_state[8] = {
		UINT32_C(0x6a09e667), UINT32_C(0xbb67ae85),
		UINT32_C(0x3c6ef372), UINT32_C(0xa54ff53a),
		UINT32_C(0x510e527f), UINT32_C(0x9b05688c),
		UINT32_C(0x1f83d9ab), UINT32_C(0x5be0cd19)
	};

	memcpy(context->state, initial_state, sizeof(initial_state));
	context->bit_count = 0U;
	context->block_bytes = 0U;
}

static void BspSha256Update(bsp_sha256_t *context, const uint8_t *data,
	size_t bytes)
{
	while (bytes != 0U)
	{
		size_t available = sizeof(context->block) - context->block_bytes;
		size_t copy = bytes < available ? bytes : available;

		memcpy(context->block + context->block_bytes, data, copy);
		context->block_bytes += copy;
		data += copy;
		bytes -= copy;
		context->bit_count += (uint64_t)copy * UINT64_C(8);
		if (context->block_bytes == sizeof(context->block))
		{
			BspSha256Transform(context);
			context->block_bytes = 0U;
		}
	}
}

static void BspSha256Final(bsp_sha256_t *context,
	sg_bsp_content_identity_t *identity)
{
	uint64_t bit_count = context->bit_count;
	uint32_t index;

	context->block[context->block_bytes++] = UINT8_C(0x80);
	if (context->block_bytes > 56U)
	{
		memset(context->block + context->block_bytes, 0,
			sizeof(context->block) - context->block_bytes);
		BspSha256Transform(context);
		context->block_bytes = 0U;
	}
	memset(context->block + context->block_bytes, 0,
		56U - context->block_bytes);
	context->block_bytes = 56U;
	for (index = 0U; index < 8U; index++)
		context->block[56U + index] = (uint8_t)(bit_count >>
			(56U - index * 8U));
	BspSha256Transform(context);
	for (index = 0U; index < 8U; index++)
	{
		identity->bytes[index * 4U] = (uint8_t)(context->state[index] >> 24);
		identity->bytes[index * 4U + 1U] =
			(uint8_t)(context->state[index] >> 16);
		identity->bytes[index * 4U + 2U] =
			(uint8_t)(context->state[index] >> 8);
		identity->bytes[index * 4U + 3U] =
			(uint8_t)context->state[index];
	}
}

static void BspSha256(const uint8_t *data, size_t bytes,
	sg_bsp_content_identity_t *identity)
{
	bsp_sha256_t context;

	BspSha256Init(&context);
	BspSha256Update(&context, data, bytes);
	BspSha256Final(&context, identity);
}

_Static_assert(CHAR_BIT == 8, "IBSP decoding requires eight-bit bytes");
_Static_assert(sizeof(float) == 4 && FLT_RADIX == 2 && FLT_MANT_DIG == 24 &&
	FLT_MAX_EXP == 128, "IBSP decoding requires IEEE binary32 floats");

typedef struct bsp_lump_view_s
{
	const uint8_t *data;
	uint32_t offset;
	uint32_t length;
	uint32_t count;
} bsp_lump_view_t;

static void BspSetError(sg_bsp_error_t *error, sg_bsp_error_code_t code,
	sg_bsp_lump_t lump, uint32_t record)
{
	if (error)
	{
		error->code = code;
		error->lump = lump;
		error->record = record;
	}
}

static uint16_t BspReadU16(const uint8_t *bytes)
{
	return (uint16_t)bytes[0] | (uint16_t)((uint16_t)bytes[1] << 8);
}

static int16_t BspReadI16(const uint8_t *bytes)
{
	return (int16_t)BspReadU16(bytes);
}

static uint32_t BspReadU32(const uint8_t *bytes)
{
	return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
		((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static int32_t BspReadI32(const uint8_t *bytes)
{
	return (int32_t)BspReadU32(bytes);
}

static float BspReadFloat(const uint8_t *bytes)
{
	uint32_t bits = BspReadU32(bytes);
	float value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

static int BspSpanValid(uint32_t first, uint32_t count, uint32_t limit)
{
	return (uint64_t)first + (uint64_t)count <= (uint64_t)limit;
}

static void *BspAllocate(uint32_t count, size_t element_size,
	sg_bsp_error_t *error, sg_bsp_lump_t lump)
{
	if (!count)
		return NULL;
	if (element_size && (size_t)count > SIZE_MAX / element_size)
	{
		BspSetError(error, SG_BSP_ERROR_SIZE_OVERFLOW, lump, 0);
		return NULL;
	}
	{
		void *memory = calloc((size_t)count, element_size);

		if (!memory)
			BspSetError(error, SG_BSP_ERROR_OUT_OF_MEMORY, lump, 0);
		return memory;
	}
}

static uint8_t *BspCopyBytes(const uint8_t *data, uint32_t count,
	int terminate, sg_bsp_error_t *error, sg_bsp_lump_t lump)
{
	size_t bytes = count;
	uint8_t *copy;

	if (terminate)
	{
		if (bytes == SIZE_MAX)
		{
			BspSetError(error, SG_BSP_ERROR_SIZE_OVERFLOW, lump, 0);
			return NULL;
		}
		bytes++;
	}
	if (!bytes)
		return NULL;
	copy = malloc(bytes);
	if (!copy)
	{
		BspSetError(error, SG_BSP_ERROR_OUT_OF_MEMORY, lump, 0);
		return NULL;
	}
	if (count)
		memcpy(copy, data, count);
	if (terminate)
		copy[count] = 0;
	return copy;
}

static int BspReadLumps(const uint8_t *data, size_t size,
	bsp_lump_view_t lumps[SG_BSP_LUMP_COUNT], sg_bsp_error_t *error)
{
	static const uint32_t strides[SG_BSP_LUMP_COUNT] = {
		1U, BSP_PLANE_BYTES, BSP_VERTEX_BYTES, 1U, BSP_NODE_BYTES,
		BSP_TEXINFO_BYTES, BSP_FACE_BYTES, 1U, BSP_LEAF_BYTES,
		BSP_LEAF_FACE_BYTES, BSP_LEAF_BRUSH_BYTES, BSP_EDGE_BYTES,
		BSP_SURFEDGE_BYTES, BSP_MODEL_BYTES, BSP_BRUSH_BYTES,
		BSP_BRUSH_SIDE_BYTES, 1U, BSP_AREA_BYTES, BSP_AREAPORTAL_BYTES
	};
	uint32_t index;

	for (index = 0; index < SG_BSP_LUMP_COUNT; index++)
	{
		uint32_t offset = BspReadU32(data + 8U + index * 8U);
		uint32_t length = BspReadU32(data + 12U + index * 8U);

		if ((length && offset < BSP_HEADER_BYTES) ||
			(uint64_t)offset + (uint64_t)length > (uint64_t)size ||
			length % strides[index] != 0U)
		{
			BspSetError(error, SG_BSP_ERROR_BAD_LUMP,
				(sg_bsp_lump_t)index, 0);
			return 0;
		}
		lumps[index].data = data + offset;
		lumps[index].offset = offset;
		lumps[index].length = length;
		lumps[index].count = length / strides[index];
	}
	for (index = 0; index < SG_BSP_LUMP_COUNT; index++)
	{
		uint32_t other;

		if (!lumps[index].length)
			continue;
		for (other = index + 1U; other < SG_BSP_LUMP_COUNT; other++)
		{
			uint64_t first_end, second_end;

			if (!lumps[other].length)
				continue;
			first_end = (uint64_t)lumps[index].offset + lumps[index].length;
			second_end = (uint64_t)lumps[other].offset + lumps[other].length;
			if ((uint64_t)lumps[index].offset < second_end &&
				(uint64_t)lumps[other].offset < first_end)
			{
				BspSetError(error, SG_BSP_ERROR_BAD_LUMP,
					(sg_bsp_lump_t)other, 0);
				return 0;
			}
		}
	}
	return 1;
}

static int BspValidateHostLimits(
	const bsp_lump_view_t lumps[SG_BSP_LUMP_COUNT], sg_bsp_error_t *error)
{
	if (lumps[SG_BSP_LUMP_AREAS].count > SG_BSP_MAX_AREAS)
	{
		BspSetError(error, SG_BSP_ERROR_LIMIT_EXCEEDED,
			SG_BSP_LUMP_AREAS, SG_BSP_MAX_AREAS);
		return 0;
	}
	if (lumps[SG_BSP_LUMP_MODELS].count > SG_BSP_MAX_MODELS)
	{
		BspSetError(error, SG_BSP_ERROR_LIMIT_EXCEEDED,
			SG_BSP_LUMP_MODELS, SG_BSP_MAX_MODELS);
		return 0;
	}
	if (lumps[SG_BSP_LUMP_VISIBILITY].length >= 4U &&
		BspReadU32(lumps[SG_BSP_LUMP_VISIBILITY].data) >
			SG_BSP_MAX_CLUSTERS)
	{
		BspSetError(error, SG_BSP_ERROR_LIMIT_EXCEEDED,
			SG_BSP_LUMP_VISIBILITY, SG_BSP_MAX_CLUSTERS);
		return 0;
	}
	return 1;
}

static int BspLoadEntities(sg_bsp_world_t *world,
	const bsp_lump_view_t *lump, sg_bsp_error_t *error)
{
	world->entity_byte_count = lump->length;
	world->entities = BspCopyBytes(lump->data, lump->length, 1, error,
		SG_BSP_LUMP_ENTITIES);
	return world->entities != NULL;
}

static int BspLoadPlanes(sg_bsp_world_t *world,
	const bsp_lump_view_t *lump, sg_bsp_error_t *error)
{
	uint32_t index, axis;

	world->plane_count = lump->count;
	world->planes = BspAllocate(lump->count, sizeof(*world->planes), error,
		SG_BSP_LUMP_PLANES);
	if (lump->count && !world->planes)
		return 0;
	for (index = 0; index < lump->count; index++)
	{
		const uint8_t *record = lump->data + (size_t)index * BSP_PLANE_BYTES;

		for (axis = 0; axis < 3; axis++)
		{
			world->planes[index].normal.value[axis] =
				BspReadFloat(record + axis * 4U);
			if (!isfinite(world->planes[index].normal.value[axis]))
				goto nonfinite;
		}
		world->planes[index].distance = BspReadFloat(record + 12U);
		if (!isfinite(world->planes[index].distance))
			goto nonfinite;
		{
			float length_squared =
				world->planes[index].normal.value[0] *
					world->planes[index].normal.value[0] +
				world->planes[index].normal.value[1] *
					world->planes[index].normal.value[1] +
				world->planes[index].normal.value[2] *
					world->planes[index].normal.value[2];

			if (!isfinite(length_squared))
				goto nonfinite;
			if (fabsf(length_squared - 1.0f) > 0.001f)
			{
				BspSetError(error, SG_BSP_ERROR_INVALID_GEOMETRY,
					SG_BSP_LUMP_PLANES, index);
				return 0;
			}
		}
		if (world->planes[index].normal.value[0] == 1.0f)
			world->planes[index].type = BSP_PLANE_X;
		else if (world->planes[index].normal.value[1] == 1.0f)
			world->planes[index].type = BSP_PLANE_Y;
		else if (world->planes[index].normal.value[2] == 1.0f)
			world->planes[index].type = BSP_PLANE_Z;
		else
			world->planes[index].type = BSP_PLANE_NON_AXIAL;
	}
	return 1;

nonfinite:
	BspSetError(error, SG_BSP_ERROR_NONFINITE_GEOMETRY,
		SG_BSP_LUMP_PLANES, index);
	return 0;
}

static int BspLoadVertices(sg_bsp_world_t *world,
	const bsp_lump_view_t *lump, sg_bsp_error_t *error)
{
	uint32_t index, axis;

	world->vertex_count = lump->count;
	world->vertices = BspAllocate(lump->count, sizeof(*world->vertices), error,
		SG_BSP_LUMP_VERTICES);
	if (lump->count && !world->vertices)
		return 0;
	for (index = 0; index < lump->count; index++)
		for (axis = 0; axis < 3; axis++)
		{
			world->vertices[index].point.value[axis] = BspReadFloat(
				lump->data + (size_t)index * BSP_VERTEX_BYTES + axis * 4U);
			if (!isfinite(world->vertices[index].point.value[axis]))
			{
				BspSetError(error, SG_BSP_ERROR_NONFINITE_GEOMETRY,
					SG_BSP_LUMP_VERTICES, index);
				return 0;
			}
		}
	return 1;
}

static int BspVisibilityStreamValid(const uint8_t *bytes, uint32_t length,
	uint32_t offset, uint32_t row_bytes)
{
	uint32_t position = offset;
	uint32_t produced = 0;

	while (produced < row_bytes)
	{
		uint8_t value;

		if (position >= length)
			return 0;
		value = bytes[position++];
		if (value)
		{
			produced++;
			continue;
		}
		if (position >= length || !bytes[position] ||
			(uint32_t)bytes[position] > row_bytes - produced)
			return 0;
		produced += bytes[position++];
	}
	return 1;
}

static int BspLoadVisibility(sg_bsp_world_t *world,
	const bsp_lump_view_t *lump, sg_bsp_error_t *error)
{
	uint32_t cluster, set, table_bytes, row_bytes;

	if (!lump->length)
		return 1;
	if (lump->length < 4U)
		goto invalid;
	world->visibility.cluster_count = BspReadU32(lump->data);
	if (world->visibility.cluster_count > (UINT32_MAX - 4U) / 8U)
	{
		BspSetError(error, SG_BSP_ERROR_SIZE_OVERFLOW,
			SG_BSP_LUMP_VISIBILITY, 0);
		return 0;
	}
	table_bytes = 4U + world->visibility.cluster_count * 8U;
	if (table_bytes > lump->length)
		goto invalid;
	world->visibility.bit_offsets = BspAllocate(
		world->visibility.cluster_count,
		sizeof(*world->visibility.bit_offsets), error,
		SG_BSP_LUMP_VISIBILITY);
	if (world->visibility.cluster_count && !world->visibility.bit_offsets)
		return 0;
	world->visibility.bytes = BspCopyBytes(lump->data, lump->length, 0,
		error, SG_BSP_LUMP_VISIBILITY);
	if (!world->visibility.bytes)
		return 0;
	world->visibility.byte_count = lump->length;
	row_bytes = (world->visibility.cluster_count + 7U) >> 3;
	for (cluster = 0; cluster < world->visibility.cluster_count; cluster++)
		for (set = 0; set < SG_BSP_VISIBILITY_SET_COUNT; set++)
		{
			uint32_t offset = BspReadU32(lump->data + 4U + cluster * 8U +
				set * 4U);

			world->visibility.bit_offsets[cluster][set] = offset;
			if (offset < table_bytes || offset >= lump->length ||
				!BspVisibilityStreamValid(lump->data, lump->length, offset,
					row_bytes))
			{
				BspSetError(error, SG_BSP_ERROR_INVALID_VISIBILITY,
					SG_BSP_LUMP_VISIBILITY, cluster);
				return 0;
			}
		}
	return 1;

invalid:
	BspSetError(error, SG_BSP_ERROR_INVALID_VISIBILITY,
		SG_BSP_LUMP_VISIBILITY, 0);
	return 0;
}

static void BspReadShortBounds(const uint8_t *bytes,
	sg_bsp_short_bounds_t *bounds)
{
	uint32_t axis;

	for (axis = 0; axis < 3; axis++)
	{
		bounds->mins[axis] = BspReadI16(bytes + axis * 2U);
		bounds->maxs[axis] = BspReadI16(bytes + 6U + axis * 2U);
	}
}

static int BspLoadNodes(sg_bsp_world_t *world,
	const bsp_lump_view_t *lump, sg_bsp_error_t *error)
{
	uint32_t index;

	world->node_count = lump->count;
	world->nodes = BspAllocate(lump->count, sizeof(*world->nodes), error,
		SG_BSP_LUMP_NODES);
	if (lump->count && !world->nodes)
		return 0;
	for (index = 0; index < lump->count; index++)
	{
		const uint8_t *record = lump->data + (size_t)index * BSP_NODE_BYTES;

		world->nodes[index].plane = BspReadU32(record);
		world->nodes[index].children[0] = BspReadI32(record + 4U);
		world->nodes[index].children[1] = BspReadI32(record + 8U);
		BspReadShortBounds(record + 12U, &world->nodes[index].bounds);
		world->nodes[index].first_face = BspReadU16(record + 24U);
		world->nodes[index].face_count = BspReadU16(record + 26U);
	}
	return 1;
}

static int BspLoadTexinfos(sg_bsp_world_t *world,
	const bsp_lump_view_t *lump, sg_bsp_error_t *error)
{
	uint32_t index, row, column;

	world->texinfo_count = lump->count;
	world->texinfos = BspAllocate(lump->count, sizeof(*world->texinfos), error,
		SG_BSP_LUMP_TEXINFO);
	if (lump->count && !world->texinfos)
		return 0;
	for (index = 0; index < lump->count; index++)
	{
		const uint8_t *record = lump->data + (size_t)index * BSP_TEXINFO_BYTES;

		for (row = 0; row < 2; row++)
			for (column = 0; column < 4; column++)
			{
				world->texinfos[index].vectors[row][column] =
					BspReadFloat(record + (row * 4U + column) * 4U);
				if (!isfinite(world->texinfos[index].vectors[row][column]))
				{
					BspSetError(error, SG_BSP_ERROR_NONFINITE_GEOMETRY,
						SG_BSP_LUMP_TEXINFO, index);
					return 0;
				}
			}
		world->texinfos[index].flags = BspReadI32(record + 32U);
		world->texinfos[index].value = BspReadI32(record + 36U);
		memcpy(world->texinfos[index].texture, record + 40U,
			SG_BSP_TEXTURE_NAME_BYTES);
		world->texinfos[index].next_texinfo = BspReadI32(record + 72U);
		if (world->texinfos[index].next_texinfo <= 0)
			world->texinfos[index].next_texinfo = -1;
	}
	return 1;
}

static int BspLoadFaces(sg_bsp_world_t *world,
	const bsp_lump_view_t *lump, sg_bsp_error_t *error)
{
	uint32_t index;

	world->face_count = lump->count;
	world->faces = BspAllocate(lump->count, sizeof(*world->faces), error,
		SG_BSP_LUMP_FACES);
	if (lump->count && !world->faces)
		return 0;
	for (index = 0; index < lump->count; index++)
	{
		const uint8_t *record = lump->data + (size_t)index * BSP_FACE_BYTES;
		int32_t first_surfedge = BspReadI32(record + 4U);
		int16_t surfedge_count = BspReadI16(record + 8U);
		int16_t texinfo = BspReadI16(record + 10U);

		world->faces[index].plane = BspReadU16(record);
		world->faces[index].side = (uint32_t)(uint16_t)BspReadI16(record + 2U);
		world->faces[index].first_surfedge = (uint32_t)first_surfedge;
		world->faces[index].surfedge_count = (uint32_t)(uint16_t)surfedge_count;
		world->faces[index].texinfo = (uint32_t)(uint16_t)texinfo;
		memcpy(world->faces[index].light_styles, record + 12U,
			SG_BSP_LIGHT_STYLE_COUNT);
		world->faces[index].light_offset = world->lighting_byte_count
			? BspReadI32(record + 16U) : -1;
	}
	return 1;
}

static int BspLoadBytes(uint8_t **output, uint32_t *output_count,
	const bsp_lump_view_t *lump, sg_bsp_error_t *error, sg_bsp_lump_t kind)
{
	*output_count = lump->length;
	if (!lump->length)
		return 1;
	*output = BspCopyBytes(lump->data, lump->length, 0, error, kind);
	return *output != NULL;
}

static int BspLoadLeaves(sg_bsp_world_t *world,
	const bsp_lump_view_t *lump, sg_bsp_error_t *error)
{
	uint32_t index;

	world->leaf_count = lump->count;
	world->leaves = BspAllocate(lump->count, sizeof(*world->leaves), error,
		SG_BSP_LUMP_LEAVES);
	if (lump->count && !world->leaves)
		return 0;
	for (index = 0; index < lump->count; index++)
	{
		const uint8_t *record = lump->data + (size_t)index * BSP_LEAF_BYTES;
		uint16_t cluster = BspReadU16(record + 4U);

		world->leaves[index].contents = BspReadI32(record);
		if (cluster == UINT16_MAX)
			world->leaves[index].cluster = -1;
		else if (!world->visibility.byte_count)
			world->leaves[index].cluster = 0;
		else
			world->leaves[index].cluster = (int32_t)cluster;
		world->leaves[index].area = BspReadU16(record + 6U);
		BspReadShortBounds(record + 8U, &world->leaves[index].bounds);
		world->leaves[index].first_leaf_face = BspReadU16(record + 20U);
		world->leaves[index].leaf_face_count = BspReadU16(record + 22U);
		world->leaves[index].first_leaf_brush = BspReadU16(record + 24U);
		world->leaves[index].leaf_brush_count = BspReadU16(record + 26U);
	}
	return 1;
}

static int BspLoadU16Indices(uint32_t **output, uint32_t *output_count,
	const bsp_lump_view_t *lump, sg_bsp_error_t *error, sg_bsp_lump_t kind)
{
	uint32_t index;

	*output_count = lump->count;
	*output = BspAllocate(lump->count, sizeof(**output), error, kind);
	if (lump->count && !*output)
		return 0;
	for (index = 0; index < lump->count; index++)
		(*output)[index] = BspReadU16(lump->data + (size_t)index * 2U);
	return 1;
}

static int BspLoadEdges(sg_bsp_world_t *world,
	const bsp_lump_view_t *lump, sg_bsp_error_t *error)
{
	uint32_t index;

	world->edge_count = lump->count;
	world->edges = BspAllocate(lump->count, sizeof(*world->edges), error,
		SG_BSP_LUMP_EDGES);
	if (lump->count && !world->edges)
		return 0;
	for (index = 0; index < lump->count; index++)
	{
		world->edges[index].vertices[0] = BspReadU16(
			lump->data + (size_t)index * BSP_EDGE_BYTES);
		world->edges[index].vertices[1] = BspReadU16(
			lump->data + (size_t)index * BSP_EDGE_BYTES + 2U);
	}
	return 1;
}

static int BspLoadSurfedges(sg_bsp_world_t *world,
	const bsp_lump_view_t *lump, sg_bsp_error_t *error)
{
	uint32_t index;

	world->surfedge_count = lump->count;
	world->surfedges = BspAllocate(lump->count, sizeof(*world->surfedges),
		error, SG_BSP_LUMP_SURFEDGES);
	if (lump->count && !world->surfedges)
		return 0;
	for (index = 0; index < lump->count; index++)
		world->surfedges[index] = BspReadI32(
			lump->data + (size_t)index * BSP_SURFEDGE_BYTES);
	return 1;
}

static int BspLoadModels(sg_bsp_world_t *world,
	const bsp_lump_view_t *lump, sg_bsp_error_t *error)
{
	uint32_t index, axis;

	world->model_count = lump->count;
	world->models = BspAllocate(lump->count, sizeof(*world->models), error,
		SG_BSP_LUMP_MODELS);
	if (lump->count && !world->models)
		return 0;
	for (index = 0; index < lump->count; index++)
	{
		const uint8_t *record = lump->data + (size_t)index * BSP_MODEL_BYTES;

		for (axis = 0; axis < 3; axis++)
		{
			float raw_min = BspReadFloat(record + axis * 4U);
			float raw_max = BspReadFloat(record + 12U + axis * 4U);
			float origin = BspReadFloat(record + 24U + axis * 4U);

			if (!isfinite(raw_min) || !isfinite(raw_max) ||
				!isfinite(origin))
			{
				BspSetError(error, SG_BSP_ERROR_NONFINITE_GEOMETRY,
					SG_BSP_LUMP_MODELS, index);
				return 0;
			}
			if (raw_min > raw_max)
			{
				BspSetError(error, SG_BSP_ERROR_INVALID_GEOMETRY,
					SG_BSP_LUMP_MODELS, index);
				return 0;
			}
			world->models[index].mins.value[axis] = raw_min - 1.0f;
			world->models[index].maxs.value[axis] = raw_max + 1.0f;
			world->models[index].origin.value[axis] = origin;
		}
		world->models[index].headnode = BspReadI32(record + 36U);
		/* The host owns world faces through model 0's node tree. */
		if (index != 0U)
		{
			world->models[index].first_face = BspReadU32(record + 40U);
			world->models[index].face_count = BspReadU32(record + 44U);
		}
	}
	return 1;
}

static int BspLoadBrushes(sg_bsp_world_t *world,
	const bsp_lump_view_t *lump, sg_bsp_error_t *error)
{
	uint32_t index;

	world->brush_count = lump->count;
	world->brushes = BspAllocate(lump->count, sizeof(*world->brushes), error,
		SG_BSP_LUMP_BRUSHES);
	if (lump->count && !world->brushes)
		return 0;
	for (index = 0; index < lump->count; index++)
	{
		const uint8_t *record = lump->data + (size_t)index * BSP_BRUSH_BYTES;

		world->brushes[index].first_side = (uint32_t)BspReadI32(record);
		world->brushes[index].side_count = (uint32_t)BspReadI32(record + 4U);
		world->brushes[index].contents = BspReadI32(record + 8U);
	}
	return 1;
}

static int BspLoadBrushSides(sg_bsp_world_t *world,
	const bsp_lump_view_t *lump, sg_bsp_error_t *error)
{
	uint32_t index;

	world->brush_side_count = lump->count;
	world->brush_sides = BspAllocate(lump->count,
		sizeof(*world->brush_sides), error, SG_BSP_LUMP_BRUSH_SIDES);
	if (lump->count && !world->brush_sides)
		return 0;
	for (index = 0; index < lump->count; index++)
	{
		uint16_t texinfo;

		world->brush_sides[index].plane = BspReadU16(
			lump->data + (size_t)index * BSP_BRUSH_SIDE_BYTES);
		texinfo = BspReadU16(lump->data +
			(size_t)index * BSP_BRUSH_SIDE_BYTES + 2U);
		world->brush_sides[index].texinfo = texinfo == UINT16_MAX
			? -1 : (int32_t)texinfo;
	}
	return 1;
}

static int BspLoadAreas(sg_bsp_world_t *world,
	const bsp_lump_view_t *lump, sg_bsp_error_t *error)
{
	uint32_t index;

	world->area_count = lump->count;
	world->areas = BspAllocate(lump->count, sizeof(*world->areas), error,
		SG_BSP_LUMP_AREAS);
	if (lump->count && !world->areas)
		return 0;
	for (index = 0; index < lump->count; index++)
	{
		const uint8_t *record = lump->data + (size_t)index * BSP_AREA_BYTES;

		world->areas[index].areaportal_count = (uint32_t)BspReadI32(record);
		world->areas[index].first_areaportal =
			(uint32_t)BspReadI32(record + 4U);
	}
	return 1;
}

static int BspLoadAreaportals(sg_bsp_world_t *world,
	const bsp_lump_view_t *lump, sg_bsp_error_t *error)
{
	uint32_t index;

	world->areaportal_count = lump->count;
	world->areaportals = BspAllocate(lump->count,
		sizeof(*world->areaportals), error, SG_BSP_LUMP_AREAPORTALS);
	if (lump->count && !world->areaportals)
		return 0;
	for (index = 0; index < lump->count; index++)
	{
		const uint8_t *record = lump->data +
			(size_t)index * BSP_AREAPORTAL_BYTES;

		world->areaportals[index].portal_number =
			(uint32_t)BspReadI32(record);
		world->areaportals[index].other_area =
			(uint32_t)BspReadI32(record + 4U);
	}
	return 1;
}

static int BspBoundsValid(const sg_bsp_short_bounds_t *bounds)
{
	uint32_t axis;

	for (axis = 0; axis < 3; axis++)
		if (bounds->mins[axis] > bounds->maxs[axis])
			return 0;
	return 1;
}

static int BspChildValid(int32_t child, uint32_t node_count,
	uint32_t leaf_count)
{
	if (child >= 0)
		return (uint32_t)child < node_count;
	if (child == INT32_MIN)
		return 0;
	return (uint32_t)(-1 - child) < leaf_count;
}

static int BspValidateReferences(const sg_bsp_world_t *world,
	sg_bsp_error_t *error)
{
	uint32_t index, axis;

	if (!world->plane_count)
	{
		BspSetError(error, SG_BSP_ERROR_INVALID_REFERENCE,
			SG_BSP_LUMP_PLANES, 0);
		return 0;
	}
	if (!world->node_count)
	{
		BspSetError(error, SG_BSP_ERROR_INVALID_REFERENCE,
			SG_BSP_LUMP_NODES, 0);
		return 0;
	}
	if (!world->leaf_count || world->leaves[0].contents != BSP_CONTENTS_SOLID)
	{
		BspSetError(error, SG_BSP_ERROR_INVALID_REFERENCE,
			SG_BSP_LUMP_LEAVES, 0);
		return 0;
	}
	if (!world->model_count)
	{
		BspSetError(error, SG_BSP_ERROR_INVALID_REFERENCE,
			SG_BSP_LUMP_MODELS, 0);
		return 0;
	}
	if (!world->area_count)
	{
		BspSetError(error, SG_BSP_ERROR_INVALID_REFERENCE,
			SG_BSP_LUMP_AREAS, 0);
		return 0;
	}
	for (index = 0; index < world->node_count; index++)
	{
		const sg_bsp_node_t *node = &world->nodes[index];

		if (node->plane >= world->plane_count ||
			!BspChildValid(node->children[0], world->node_count,
				world->leaf_count) ||
			!BspChildValid(node->children[1], world->node_count,
				world->leaf_count) ||
			!BspBoundsValid(&node->bounds) ||
			!BspSpanValid(node->first_face, node->face_count,
				world->face_count))
			goto bad_node;
	}
	for (index = 0; index < world->texinfo_count; index++)
		if (world->texinfos[index].next_texinfo > 0 &&
			(uint32_t)world->texinfos[index].next_texinfo >=
				world->texinfo_count)
			goto bad_texinfo;
	for (index = 0; index < world->face_count; index++)
	{
		const sg_bsp_face_t *face = &world->faces[index];

		if (face->plane >= world->plane_count || face->side > 1U ||
			face->surfedge_count < 3U ||
			face->surfedge_count > BSP_MAX_FACE_EDGES ||
			!BspSpanValid(face->first_surfedge, face->surfedge_count,
				world->surfedge_count) || face->texinfo >= world->texinfo_count ||
			(face->light_offset < -1 ||
			 (face->light_offset >= 0 &&
			  (uint32_t)face->light_offset >= world->lighting_byte_count)))
			goto bad_face;
	}
	for (index = 0; index < world->leaf_count; index++)
	{
		const sg_bsp_leaf_t *leaf = &world->leaves[index];

		if (leaf->cluster < -1 ||
			(world->visibility.byte_count && leaf->cluster >= 0 &&
			 (uint32_t)leaf->cluster >= world->visibility.cluster_count) ||
			leaf->area >= world->area_count || !BspBoundsValid(&leaf->bounds) ||
			!BspSpanValid(leaf->first_leaf_face, leaf->leaf_face_count,
				world->leaf_face_count) ||
			!BspSpanValid(leaf->first_leaf_brush, leaf->leaf_brush_count,
				world->leaf_brush_count))
			goto bad_leaf;
	}
	for (index = 0; index < world->leaf_face_count; index++)
		if (world->leaf_faces[index] >= world->face_count)
			goto bad_leaf_face;
	for (index = 0; index < world->leaf_brush_count; index++)
		if (world->leaf_brushes[index] >= world->brush_count)
			goto bad_leaf_brush;
	for (index = 0; index < world->edge_count; index++)
		if (world->edges[index].vertices[0] >= world->vertex_count ||
			world->edges[index].vertices[1] >= world->vertex_count)
			goto bad_edge;
	for (index = 0; index < world->surfedge_count; index++)
	{
		int32_t surfedge = world->surfedges[index];
		uint32_t edge;

		if (surfedge == INT32_MIN)
			goto bad_surfedge;
		edge = surfedge < 0 ? (uint32_t)-surfedge : (uint32_t)surfedge;
		if (edge >= world->edge_count)
			goto bad_surfedge;
	}
	for (index = 0; index < world->model_count; index++)
	{
		const sg_bsp_model_t *model = &world->models[index];

		for (axis = 0; axis < 3; axis++)
		{
			if (!isfinite(model->mins.value[axis]) ||
				!isfinite(model->maxs.value[axis]) ||
				!isfinite(model->origin.value[axis]))
				goto bad_model_geometry;
			if (model->mins.value[axis] > model->maxs.value[axis])
				goto bad_model_bounds;
		}
		if (!BspChildValid(model->headnode, world->node_count,
				world->leaf_count) ||
			!BspSpanValid(model->first_face, model->face_count,
				world->face_count))
			goto bad_model;
	}
	if (world->models[0].headnode != 0)
		goto bad_model;
	for (index = 0; index < world->brush_count; index++)
		if (!BspSpanValid(world->brushes[index].first_side,
				world->brushes[index].side_count, world->brush_side_count))
			goto bad_brush;
	for (index = 0; index < world->brush_side_count; index++)
		if (world->brush_sides[index].plane >= world->plane_count ||
			world->brush_sides[index].texinfo < -1 ||
			(world->brush_sides[index].texinfo >= 0 &&
			 (uint32_t)world->brush_sides[index].texinfo >=
				world->texinfo_count))
			goto bad_brush_side;
	for (index = 0; index < world->area_count; index++)
		if (!BspSpanValid(world->areas[index].first_areaportal,
				world->areas[index].areaportal_count,
				world->areaportal_count))
			goto bad_area;
	for (index = 0; index < world->areaportal_count; index++)
		if (world->areaportals[index].portal_number >=
				world->areaportal_count ||
			world->areaportals[index].other_area >= world->area_count)
			goto bad_areaportal;
	return 1;

bad_node:
	BspSetError(error, SG_BSP_ERROR_INVALID_REFERENCE, SG_BSP_LUMP_NODES,
		index); return 0;
bad_texinfo:
	BspSetError(error, SG_BSP_ERROR_INVALID_REFERENCE, SG_BSP_LUMP_TEXINFO,
		index); return 0;
bad_face:
	BspSetError(error, SG_BSP_ERROR_INVALID_REFERENCE, SG_BSP_LUMP_FACES,
		index); return 0;
bad_leaf:
	BspSetError(error, SG_BSP_ERROR_INVALID_REFERENCE, SG_BSP_LUMP_LEAVES,
		index); return 0;
bad_leaf_face:
	BspSetError(error, SG_BSP_ERROR_INVALID_REFERENCE,
		SG_BSP_LUMP_LEAF_FACES, index); return 0;
bad_leaf_brush:
	BspSetError(error, SG_BSP_ERROR_INVALID_REFERENCE,
		SG_BSP_LUMP_LEAF_BRUSHES, index); return 0;
bad_edge:
	BspSetError(error, SG_BSP_ERROR_INVALID_REFERENCE, SG_BSP_LUMP_EDGES,
		index); return 0;
bad_surfedge:
	BspSetError(error, SG_BSP_ERROR_INVALID_REFERENCE,
		SG_BSP_LUMP_SURFEDGES, index); return 0;
bad_model_geometry:
	BspSetError(error, SG_BSP_ERROR_NONFINITE_GEOMETRY, SG_BSP_LUMP_MODELS,
		index); return 0;
bad_model_bounds:
	BspSetError(error, SG_BSP_ERROR_INVALID_GEOMETRY, SG_BSP_LUMP_MODELS,
		index); return 0;
bad_model:
	BspSetError(error, SG_BSP_ERROR_INVALID_REFERENCE, SG_BSP_LUMP_MODELS,
		index); return 0;
bad_brush:
	BspSetError(error, SG_BSP_ERROR_INVALID_REFERENCE, SG_BSP_LUMP_BRUSHES,
		index); return 0;
bad_brush_side:
	BspSetError(error, SG_BSP_ERROR_INVALID_REFERENCE,
		SG_BSP_LUMP_BRUSH_SIDES, index); return 0;
bad_area:
	BspSetError(error, SG_BSP_ERROR_INVALID_REFERENCE, SG_BSP_LUMP_AREAS,
		index); return 0;
bad_areaportal:
	BspSetError(error, SG_BSP_ERROR_INVALID_REFERENCE,
		SG_BSP_LUMP_AREAPORTALS, index); return 0;
}

static int BspValidateTexinfoAnimations(const sg_bsp_world_t *world,
	sg_bsp_error_t *error)
{
	uint32_t *indegree, *queue;
	uint32_t index, first = 0, count = 0;

	if (!world->texinfo_count)
		return 1;
	indegree = BspAllocate(world->texinfo_count, sizeof(*indegree), error,
		SG_BSP_LUMP_TEXINFO);
	queue = BspAllocate(world->texinfo_count, sizeof(*queue), error,
		SG_BSP_LUMP_TEXINFO);
	if (!indegree || !queue)
	{
		free(queue);
		free(indegree);
		return 0;
	}
	for (index = 0; index < world->texinfo_count; index++)
		if (world->texinfos[index].next_texinfo > 0)
			indegree[(uint32_t)world->texinfos[index].next_texinfo]++;
	for (index = 0; index < world->texinfo_count; index++)
		if (!indegree[index])
			queue[count++] = index;
	while (first < count)
	{
		int32_t next = world->texinfos[queue[first++]].next_texinfo;

		if (next > 0 && --indegree[(uint32_t)next] == 0U)
			queue[count++] = (uint32_t)next;
	}
	for (index = 0; index < world->texinfo_count; index++)
	{
		int32_t next = world->texinfos[index].next_texinfo;

		if (!indegree[index] && next > 0 && indegree[(uint32_t)next])
		{
			BspSetError(error, SG_BSP_ERROR_INVALID_ANIMATION,
				SG_BSP_LUMP_TEXINFO, index);
			free(queue);
			free(indegree);
			return 0;
		}
	}
	free(queue);
	free(indegree);
	return 1;
}

static int BspClaimNodeFaces(const sg_bsp_world_t *world, uint32_t node,
	uint32_t owner, uint32_t *face_owner, sg_bsp_error_t *error)
{
	const sg_bsp_node_t *record = &world->nodes[node];
	uint32_t offset;

	for (offset = 0; offset < record->face_count; offset++)
	{
		uint32_t face = record->first_face + offset;

		if (face_owner[face])
		{
			BspSetError(error, SG_BSP_ERROR_INVALID_TREE,
				SG_BSP_LUMP_FACES, face);
			return 0;
		}
		face_owner[face] = owner;
	}
	return 1;
}

static int BspWalkTree(const sg_bsp_world_t *world, int32_t headnode,
	uint32_t owner, uint8_t *node_parent, uint8_t *leaf_parent,
	uint32_t *face_owner, uint32_t *stack, uint8_t *next_side,
	sg_bsp_error_t *error)
{
	uint32_t depth;

	if (headnode < 0)
		return 1;
	if (!BspClaimNodeFaces(world, (uint32_t)headnode, owner, face_owner,
			error))
		return 0;
	depth = 1;
	stack[0] = (uint32_t)headnode;
	next_side[0] = 0;
	while (depth)
	{
		uint32_t node = stack[depth - 1U];
		uint8_t side = next_side[depth - 1U];
		int32_t child;

		if (side == 2U)
		{
			depth--;
			continue;
		}
		next_side[depth - 1U]++;
		child = world->nodes[node].children[side];
		if (child < 0)
		{
			uint32_t leaf = (uint32_t)(-1 - child);

			if (leaf_parent[leaf])
			{
				BspSetError(error, SG_BSP_ERROR_INVALID_TREE,
					SG_BSP_LUMP_LEAVES, leaf);
				return 0;
			}
			leaf_parent[leaf] = 1;
			continue;
		}
		if (node_parent[(uint32_t)child])
		{
			BspSetError(error, SG_BSP_ERROR_INVALID_TREE,
				SG_BSP_LUMP_NODES, (uint32_t)child);
			return 0;
		}
		node_parent[(uint32_t)child] = 1;
		if (depth == world->node_count)
		{
			BspSetError(error, SG_BSP_ERROR_INVALID_TREE,
				SG_BSP_LUMP_NODES, (uint32_t)child);
			return 0;
		}
		if (!BspClaimNodeFaces(world, (uint32_t)child, owner,
				face_owner, error))
			return 0;
		stack[depth] = (uint32_t)child;
		next_side[depth] = 0;
		depth++;
	}
	return 1;
}

static int BspValidateTrees(const sg_bsp_world_t *world,
	sg_bsp_error_t *error)
{
	uint8_t *node_parent = NULL, *leaf_parent = NULL;
	uint8_t *next_side = NULL;
	uint32_t empty_face_owner = 0;
	uint32_t *face_owner = &empty_face_owner, *stack = NULL;
	uint32_t model;
	int valid = 0;

	node_parent = BspAllocate(world->node_count, sizeof(*node_parent), error,
		SG_BSP_LUMP_NODES);
	if (!node_parent)
		goto done;
	leaf_parent = BspAllocate(world->leaf_count, sizeof(*leaf_parent), error,
		SG_BSP_LUMP_LEAVES);
	if (!leaf_parent)
		goto done;
	stack = BspAllocate(world->node_count, sizeof(*stack), error,
		SG_BSP_LUMP_NODES);
	if (!stack)
		goto done;
	next_side = BspAllocate(world->node_count, sizeof(*next_side), error,
		SG_BSP_LUMP_NODES);
	if (!next_side)
		goto done;
	if (world->face_count)
	{
		face_owner = BspAllocate(world->face_count, sizeof(*face_owner), error,
			SG_BSP_LUMP_FACES);
		if (!face_owner)
			goto done;
	}
	for (model = 0; model < world->model_count; model++)
	{
		const sg_bsp_model_t *record = &world->models[model];
		uint32_t owner = model + 1U;
		uint32_t offset;

		if (!BspWalkTree(world, record->headnode, owner, node_parent,
				leaf_parent, face_owner, stack, next_side, error))
			goto done;
		for (offset = 0; offset < record->face_count; offset++)
		{
			uint32_t face = record->first_face + offset;

			if (face_owner[face] && face_owner[face] != owner)
			{
				BspSetError(error, SG_BSP_ERROR_INVALID_TREE,
					SG_BSP_LUMP_FACES, face);
				goto done;
			}
			face_owner[face] = owner;
		}
	}
	valid = 1;

done:
	if (face_owner != &empty_face_owner)
		free(face_owner);
	free(next_side);
	free(stack);
	free(leaf_parent);
	free(node_parent);
	return valid;
}

int SG_BspWorldLoadMemory(const void *data_value, size_t size,
	sg_bsp_world_t **world_out, sg_bsp_error_t *error_out)
{
	const uint8_t *data = data_value;
	bsp_lump_view_t lumps[SG_BSP_LUMP_COUNT];
	sg_bsp_world_t *world;

	BspSetError(error_out, SG_BSP_ERROR_NONE, SG_BSP_LUMP_ENTITIES, 0);
	if (!data || !world_out || *world_out)
	{
		BspSetError(error_out, SG_BSP_ERROR_INVALID_ARGUMENT,
			SG_BSP_LUMP_ENTITIES, 0);
		return 0;
	}
	if (size < BSP_HEADER_BYTES)
	{
		BspSetError(error_out, SG_BSP_ERROR_TRUNCATED_HEADER,
			SG_BSP_LUMP_ENTITIES, 0);
		return 0;
	}
	if (memcmp(data, "IBSP", 4) != 0)
	{
		BspSetError(error_out, SG_BSP_ERROR_BAD_MAGIC,
			SG_BSP_LUMP_ENTITIES, 0);
		return 0;
	}
	if (BspReadU32(data + 4U) != SG_BSP_VERSION)
	{
		BspSetError(error_out, SG_BSP_ERROR_UNSUPPORTED_VERSION,
			SG_BSP_LUMP_ENTITIES, 0);
		return 0;
	}
	if (!BspReadLumps(data, size, lumps, error_out) ||
		!BspValidateHostLimits(lumps, error_out))
		return 0;
	world = calloc(1, sizeof(*world));
	if (!world)
	{
		BspSetError(error_out, SG_BSP_ERROR_OUT_OF_MEMORY,
			SG_BSP_LUMP_ENTITIES, 0);
		return 0;
	}
	BspSha256(data, size, &world->content_identity);
	if (!BspLoadEntities(world, &lumps[SG_BSP_LUMP_ENTITIES], error_out) ||
		!BspLoadPlanes(world, &lumps[SG_BSP_LUMP_PLANES], error_out) ||
		!BspLoadVertices(world, &lumps[SG_BSP_LUMP_VERTICES], error_out) ||
		!BspLoadVisibility(world, &lumps[SG_BSP_LUMP_VISIBILITY], error_out) ||
		!BspLoadNodes(world, &lumps[SG_BSP_LUMP_NODES], error_out) ||
		!BspLoadTexinfos(world, &lumps[SG_BSP_LUMP_TEXINFO], error_out) ||
		!BspLoadBytes(&world->lighting, &world->lighting_byte_count,
			&lumps[SG_BSP_LUMP_LIGHTING], error_out, SG_BSP_LUMP_LIGHTING) ||
		!BspLoadFaces(world, &lumps[SG_BSP_LUMP_FACES], error_out) ||
		!BspLoadLeaves(world, &lumps[SG_BSP_LUMP_LEAVES], error_out) ||
		!BspLoadU16Indices(&world->leaf_faces, &world->leaf_face_count,
			&lumps[SG_BSP_LUMP_LEAF_FACES], error_out,
			SG_BSP_LUMP_LEAF_FACES) ||
		!BspLoadU16Indices(&world->leaf_brushes, &world->leaf_brush_count,
			&lumps[SG_BSP_LUMP_LEAF_BRUSHES], error_out,
			SG_BSP_LUMP_LEAF_BRUSHES) ||
		!BspLoadEdges(world, &lumps[SG_BSP_LUMP_EDGES], error_out) ||
		!BspLoadSurfedges(world, &lumps[SG_BSP_LUMP_SURFEDGES], error_out) ||
		!BspLoadModels(world, &lumps[SG_BSP_LUMP_MODELS], error_out) ||
		!BspLoadBrushes(world, &lumps[SG_BSP_LUMP_BRUSHES], error_out) ||
		!BspLoadBrushSides(world, &lumps[SG_BSP_LUMP_BRUSH_SIDES], error_out) ||
		!BspLoadAreas(world, &lumps[SG_BSP_LUMP_AREAS], error_out) ||
		!BspLoadAreaportals(world, &lumps[SG_BSP_LUMP_AREAPORTALS],
		error_out) || !BspValidateReferences(world, error_out) ||
		!BspValidateTexinfoAnimations(world, error_out) ||
		!BspValidateTrees(world, error_out))
	{
		SG_BspWorldDestroy(world);
		return 0;
	}
	*world_out = world;
	return 1;
}

int SG_BspWorldLoadFile(const char *path, sg_bsp_world_t **world_out,
	sg_bsp_error_t *error_out)
{
	FILE *file = NULL;
	uint8_t *bytes = NULL;
	long length;
	int result = 0;

	BspSetError(error_out, SG_BSP_ERROR_NONE, SG_BSP_LUMP_ENTITIES, 0);
	if (!path || !path[0] || !world_out || *world_out)
	{
		BspSetError(error_out, SG_BSP_ERROR_INVALID_ARGUMENT,
			SG_BSP_LUMP_ENTITIES, 0);
		return 0;
	}
	file = fopen(path, "rb");
	if (!file || fseek(file, 0, SEEK_END) != 0 ||
		(length = ftell(file)) < 0 || (uint64_t)length > SIZE_MAX ||
		fseek(file, 0, SEEK_SET) != 0)
		goto io_failure;
	if (!length)
	{
		BspSetError(error_out, SG_BSP_ERROR_TRUNCATED_HEADER,
			SG_BSP_LUMP_ENTITIES, 0);
		goto done;
	}
	bytes = malloc((size_t)length);
	if (!bytes)
	{
		BspSetError(error_out, SG_BSP_ERROR_OUT_OF_MEMORY,
			SG_BSP_LUMP_ENTITIES, 0);
		goto done;
	}
	if (fread(bytes, 1, (size_t)length, file) != (size_t)length)
		goto io_failure;
	result = SG_BspWorldLoadMemory(bytes, (size_t)length, world_out,
		error_out);
	goto done;

io_failure:
	BspSetError(error_out, SG_BSP_ERROR_IO, SG_BSP_LUMP_ENTITIES, 0);
done:
	free(bytes);
	if (file)
		fclose(file);
	return result;
}

void SG_BspWorldDestroy(sg_bsp_world_t *world)
{
	if (!world)
		return;
	free(world->entities);
	free(world->planes);
	free(world->vertices);
	free(world->visibility.bit_offsets);
	free(world->visibility.bytes);
	free(world->nodes);
	free(world->texinfos);
	free(world->faces);
	free(world->lighting);
	free(world->leaves);
	free(world->leaf_faces);
	free(world->leaf_brushes);
	free(world->edges);
	free(world->surfedges);
	free(world->models);
	free(world->brushes);
	free(world->brush_sides);
	free(world->areas);
	free(world->areaportals);
	free(world);
}

const char *SG_BspWorldErrorString(sg_bsp_error_code_t code)
{
	switch (code)
	{
	case SG_BSP_ERROR_NONE: return "no error";
	case SG_BSP_ERROR_INVALID_ARGUMENT: return "invalid argument";
	case SG_BSP_ERROR_IO: return "I/O failure";
	case SG_BSP_ERROR_TRUNCATED_HEADER: return "truncated BSP header";
	case SG_BSP_ERROR_BAD_MAGIC: return "not an IBSP file";
	case SG_BSP_ERROR_UNSUPPORTED_VERSION: return "unsupported BSP version";
	case SG_BSP_ERROR_BAD_LUMP: return "invalid BSP lump";
	case SG_BSP_ERROR_SIZE_OVERFLOW: return "BSP size overflow";
	case SG_BSP_ERROR_LIMIT_EXCEEDED: return "BSP host limit exceeded";
	case SG_BSP_ERROR_OUT_OF_MEMORY: return "out of memory";
	case SG_BSP_ERROR_NONFINITE_GEOMETRY: return "non-finite BSP geometry";
	case SG_BSP_ERROR_INVALID_GEOMETRY: return "invalid BSP geometry";
	case SG_BSP_ERROR_INVALID_REFERENCE: return "invalid BSP reference";
	case SG_BSP_ERROR_INVALID_VISIBILITY: return "invalid BSP visibility";
	case SG_BSP_ERROR_INVALID_TREE: return "invalid BSP tree";
	case SG_BSP_ERROR_INVALID_ANIMATION: return "invalid BSP animation chain";
	default: return "unknown BSP error";
	}
}
