/* Authenticated, append-only human command/Pmove/hook observation. */
#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "../g_local.h"
#include "sg_cvars.h"
#include "sg_human_trace.h"
#include "sg_identity.h"

#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#define SG_HUMAN_TRACE_FORMAT "lmctf-human-trace-v3"
#define SG_HUMAN_TRACE_LINE_BYTES 8192U
#ifndef SG_HUMAN_TRACE_SEGMENT_BYTES
#define SG_HUMAN_TRACE_SEGMENT_BYTES (64U * 1024U * 1024U)
#endif
#ifndef SG_HUMAN_TRACE_MAX_SEGMENTS
#define SG_HUMAN_TRACE_MAX_SEGMENTS 1000000U
#endif

_Static_assert(sizeof(((level_locals_t *)0)->time) == sizeof(uint32_t),
	"human trace timing requires a 32-bit level.time");

typedef struct human_trace_sha256_s
{
	uint32_t state[8];
	uint64_t bytes;
	unsigned char block[64];
	size_t used;
} human_trace_sha256_t;

typedef struct human_trace_builder_s
{
	char bytes[SG_HUMAN_TRACE_LINE_BYTES];
	size_t length;
	qboolean valid;
} human_trace_builder_t;

static FILE *sg_human_trace_file;
static sg_level_identity_t sg_human_trace_identity;
static char sg_human_trace_directory[512];
static uint64_t sg_human_trace_order;
static uint64_t sg_human_trace_command;
static uint64_t sg_human_trace_hook_event;
static uint32_t sg_human_trace_session;
static uint32_t sg_human_trace_segment;
static size_t sg_human_trace_segment_bytes;
static char sg_human_trace_previous_sha256[65];
static qboolean sg_human_trace_open_failed;
static qboolean sg_human_trace_match_ended;

static uint32_t HumanTraceRotateRight(uint32_t value, unsigned bits)
{
	return (value >> bits) | (value << (32U - bits));
}

static void HumanTraceSHA256Transform(human_trace_sha256_t *context,
	const unsigned char block[64])
{
	static const uint32_t constants[64] = {
		UINT32_C(0x428a2f98), UINT32_C(0x71374491), UINT32_C(0xb5c0fbcf),
		UINT32_C(0xe9b5dba5), UINT32_C(0x3956c25b), UINT32_C(0x59f111f1),
		UINT32_C(0x923f82a4), UINT32_C(0xab1c5ed5), UINT32_C(0xd807aa98),
		UINT32_C(0x12835b01), UINT32_C(0x243185be), UINT32_C(0x550c7dc3),
		UINT32_C(0x72be5d74), UINT32_C(0x80deb1fe), UINT32_C(0x9bdc06a7),
		UINT32_C(0xc19bf174), UINT32_C(0xe49b69c1), UINT32_C(0xefbe4786),
		UINT32_C(0x0fc19dc6), UINT32_C(0x240ca1cc), UINT32_C(0x2de92c6f),
		UINT32_C(0x4a7484aa), UINT32_C(0x5cb0a9dc), UINT32_C(0x76f988da),
		UINT32_C(0x983e5152), UINT32_C(0xa831c66d), UINT32_C(0xb00327c8),
		UINT32_C(0xbf597fc7), UINT32_C(0xc6e00bf3), UINT32_C(0xd5a79147),
		UINT32_C(0x06ca6351), UINT32_C(0x14292967), UINT32_C(0x27b70a85),
		UINT32_C(0x2e1b2138), UINT32_C(0x4d2c6dfc), UINT32_C(0x53380d13),
		UINT32_C(0x650a7354), UINT32_C(0x766a0abb), UINT32_C(0x81c2c92e),
		UINT32_C(0x92722c85), UINT32_C(0xa2bfe8a1), UINT32_C(0xa81a664b),
		UINT32_C(0xc24b8b70), UINT32_C(0xc76c51a3), UINT32_C(0xd192e819),
		UINT32_C(0xd6990624), UINT32_C(0xf40e3585), UINT32_C(0x106aa070),
		UINT32_C(0x19a4c116), UINT32_C(0x1e376c08), UINT32_C(0x2748774c),
		UINT32_C(0x34b0bcb5), UINT32_C(0x391c0cb3), UINT32_C(0x4ed8aa4a),
		UINT32_C(0x5b9cca4f), UINT32_C(0x682e6ff3), UINT32_C(0x748f82ee),
		UINT32_C(0x78a5636f), UINT32_C(0x84c87814), UINT32_C(0x8cc70208),
		UINT32_C(0x90befffa), UINT32_C(0xa4506ceb), UINT32_C(0xbef9a3f7),
		UINT32_C(0xc67178f2)
	};
	uint32_t words[64];
	uint32_t a, b, c, d, e, f, g, h;
	unsigned i;

	for (i = 0; i < 16U; i++)
		words[i] = ((uint32_t)block[i * 4U] << 24) |
			((uint32_t)block[i * 4U + 1U] << 16) |
			((uint32_t)block[i * 4U + 2U] << 8) |
			(uint32_t)block[i * 4U + 3U];
	for (; i < 64U; i++)
	{
		uint32_t s0 = HumanTraceRotateRight(words[i - 15U], 7U) ^
			HumanTraceRotateRight(words[i - 15U], 18U) ^
			(words[i - 15U] >> 3);
		uint32_t s1 = HumanTraceRotateRight(words[i - 2U], 17U) ^
			HumanTraceRotateRight(words[i - 2U], 19U) ^
			(words[i - 2U] >> 10);
		words[i] = words[i - 16U] + s0 + words[i - 7U] + s1;
	}
	a = context->state[0]; b = context->state[1];
	c = context->state[2]; d = context->state[3];
	e = context->state[4]; f = context->state[5];
	g = context->state[6]; h = context->state[7];
	for (i = 0; i < 64U; i++)
	{
		uint32_t upper = h +
			(HumanTraceRotateRight(e, 6U) ^
			 HumanTraceRotateRight(e, 11U) ^
			 HumanTraceRotateRight(e, 25U)) +
			((e & f) ^ ((~e) & g)) + constants[i] + words[i];
		uint32_t lower =
			(HumanTraceRotateRight(a, 2U) ^
			 HumanTraceRotateRight(a, 13U) ^
			 HumanTraceRotateRight(a, 22U)) +
			((a & b) ^ (a & c) ^ (b & c));
		h = g; g = f; f = e; e = d + upper;
		d = c; c = b; b = a; a = upper + lower;
	}
	context->state[0] += a; context->state[1] += b;
	context->state[2] += c; context->state[3] += d;
	context->state[4] += e; context->state[5] += f;
	context->state[6] += g; context->state[7] += h;
}

static void HumanTraceSHA256(const unsigned char *bytes, size_t length,
	char out[65])
{
	static const uint32_t initial[8] = {
		UINT32_C(0x6a09e667), UINT32_C(0xbb67ae85),
		UINT32_C(0x3c6ef372), UINT32_C(0xa54ff53a),
		UINT32_C(0x510e527f), UINT32_C(0x9b05688c),
		UINT32_C(0x1f83d9ab), UINT32_C(0x5be0cd19)
	};
	static const char hex[] = "0123456789abcdef";
	human_trace_sha256_t context;
	unsigned char digest[32];
	uint64_t bit_length;
	size_t offset = 0U;
	unsigned i;

	memset(&context, 0, sizeof(context));
	memcpy(context.state, initial, sizeof(initial));
	context.bytes = length;
	while (offset < length)
	{
		size_t amount = sizeof(context.block) - context.used;
		if (amount > length - offset)
			amount = length - offset;
		memcpy(context.block + context.used, bytes + offset, amount);
		context.used += amount;
		offset += amount;
		if (context.used == sizeof(context.block))
		{
			HumanTraceSHA256Transform(&context, context.block);
			context.used = 0U;
		}
	}
	bit_length = context.bytes * UINT64_C(8);
	context.block[context.used++] = 0x80U;
	if (context.used > 56U)
	{
		while (context.used < 64U)
			context.block[context.used++] = 0U;
		HumanTraceSHA256Transform(&context, context.block);
		context.used = 0U;
	}
	while (context.used < 56U)
		context.block[context.used++] = 0U;
	for (i = 0U; i < 8U; i++)
		context.block[63U - i] =
			(unsigned char)(bit_length >> (i * 8U));
	HumanTraceSHA256Transform(&context, context.block);
	for (i = 0U; i < 8U; i++)
	{
		digest[i * 4U] = (unsigned char)(context.state[i] >> 24);
		digest[i * 4U + 1U] = (unsigned char)(context.state[i] >> 16);
		digest[i * 4U + 2U] = (unsigned char)(context.state[i] >> 8);
		digest[i * 4U + 3U] = (unsigned char)context.state[i];
	}
	for (i = 0U; i < sizeof(digest); i++)
	{
		out[i * 2U] = hex[digest[i] >> 4];
		out[i * 2U + 1U] = hex[digest[i] & 15U];
	}
	out[64] = '\0';
}

static void HumanTraceBuilderBegin(human_trace_builder_t *builder)
{
	builder->bytes[0] = '\0';
	builder->length = 0U;
	builder->valid = true;
}

static void HumanTraceBuilderAppend(human_trace_builder_t *builder,
	const char *format, ...)
{
	va_list arguments;
	int written;

	if (!builder->valid)
		return;
	va_start(arguments, format);
	written = vsnprintf(builder->bytes + builder->length,
		sizeof(builder->bytes) - builder->length, format, arguments);
	va_end(arguments);
	if (written < 0 || (size_t)written >=
	    sizeof(builder->bytes) - builder->length)
	{
		builder->valid = false;
		return;
	}
	builder->length += (size_t)written;
}

static int HumanTraceEntityKey(const edict_t *entity)
{
	uintptr_t address;
	uintptr_t base;
	uintptr_t offset;
	size_t extent;

	if (!entity || !g_edicts)
		return 0;
	address = (uintptr_t)entity;
	base = (uintptr_t)g_edicts;
	extent = (size_t)globals.num_edicts * sizeof(*g_edicts);
	if (address < base)
		return -1;
	offset = address - base;
	if (offset >= extent || offset % sizeof(*g_edicts))
		return -1;
	return (int)(offset / sizeof(*g_edicts));
}

static qboolean HumanTraceSafeName(const char *name)
{
	const unsigned char *cursor = (const unsigned char *)name;

	if (!cursor || !*cursor)
		return false;
	for (; *cursor; cursor++)
		if (!((*cursor >= 'a' && *cursor <= 'z') ||
		      (*cursor >= 'A' && *cursor <= 'Z') ||
		      (*cursor >= '0' && *cursor <= '9') ||
		      *cursor == '_' || *cursor == '-' || *cursor == '.'))
			return false;
	return true;
}

static qboolean HumanTracePath(char path[1024], uint32_t segment)
{
	int written = snprintf(path, 1024, "%s/humantrace-%s-%08" PRIx32
		"-%08" PRIx32 "-%06" PRIu32 ".jsonl",
		sg_human_trace_directory, sg_human_trace_identity.mapname,
		sg_human_trace_identity.bsp_checksum,
		sg_human_trace_identity.entity_crc32, segment);

	return written >= 0 && written < 1024;
}

static qboolean HumanTraceFileExists(const char *path)
{
	FILE *file = fopen(path, "rb");

	if (!file)
		return false;
	fclose(file);
	return true;
}

static FILE *HumanTraceCreateExclusive(const char *path)
{
	int descriptor;
	FILE *file;

#ifdef _WIN32
	descriptor = _open(path, _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY,
		_S_IREAD | _S_IWRITE);
	if (descriptor < 0)
		return NULL;
	file = _fdopen(descriptor, "wb");
	if (!file)
		_close(descriptor);
#else
	descriptor = open(path, O_WRONLY | O_CREAT | O_EXCL, 0666);
	if (descriptor < 0)
		return NULL;
	file = fdopen(descriptor, "wb");
	if (!file)
		close(descriptor);
#endif
	return file;
}

static uint32_t HumanTraceLevelTimeBits(void)
{
	uint32_t bits = 0U;

	memcpy(&bits, &level.time, sizeof(bits));
	return bits;
}

static void HumanTraceDisable(const char *message)
{
	if (message)
		gi.dprintf("humantrace: %s\n", message);
	if (sg_human_trace_file)
		fclose(sg_human_trace_file);
	sg_human_trace_file = NULL;
	sg_human_trace_open_failed = true;
}

static qboolean HumanTraceWriteAuthenticated(const char *payload,
	size_t payload_length)
{
	unsigned char digest_input[65U + SG_HUMAN_TRACE_LINE_BYTES];
	char digest[65];
	char line[SG_HUMAN_TRACE_LINE_BYTES];
	int written;
	size_t line_length;

	if (!sg_human_trace_file || payload_length < 2U ||
	    payload[payload_length - 1U] != '}' ||
	    payload_length + 166U >= sizeof(line))
		return false;
	memcpy(digest_input, sg_human_trace_previous_sha256, 64U);
	memcpy(digest_input + 64U, payload, payload_length);
	HumanTraceSHA256(digest_input, 64U + payload_length, digest);
	written = snprintf(line, sizeof(line), "%.*s,\"prev_sha256\":\"%s\","
		"\"sha256\":\"%s\"}\n", (int)(payload_length - 1U), payload,
		sg_human_trace_previous_sha256, digest);
	if (written < 0 || (size_t)written >= sizeof(line))
		return false;
	line_length = (size_t)written;
	if (fwrite(line, 1U, line_length, sg_human_trace_file) != line_length ||
	    fflush(sg_human_trace_file) != 0)
		return false;
	strcpy(sg_human_trace_previous_sha256, digest);
	sg_human_trace_segment_bytes += line_length;
	return true;
}

static qboolean HumanTraceOpenSegment(qboolean continuation)
{
	human_trace_builder_t header;
	char path[1024];

	if (sg_human_trace_segment >= SG_HUMAN_TRACE_MAX_SEGMENTS ||
	    !HumanTracePath(path, sg_human_trace_segment) ||
	    HumanTraceFileExists(path))
		return false;
	sg_human_trace_file = HumanTraceCreateExclusive(path);
	if (!sg_human_trace_file)
		return false;
	sg_human_trace_segment_bytes = 0U;
	HumanTraceBuilderBegin(&header);
	HumanTraceBuilderAppend(&header,
		"{\"format\":\"%s\",\"kind\":\"header\","
		"\"session\":%" PRIu32 ",\"segment\":%" PRIu32 ","
		"\"continuation\":%d,\"start_order\":%" PRIu64 ","
		"\"start_command\":%" PRIu64 ","
		"\"start_hook_event\":%" PRIu64 ","
		"\"map\":\"%s\",\"bsp_checksum\":%" PRIu32 ","
		"\"entity_crc32\":%" PRIu32 ",\"physics_id\":%" PRIu32 ","
		"\"module_revision\":%d,\"module_version\":\"%s\"}",
		SG_HUMAN_TRACE_FORMAT, sg_human_trace_session,
		sg_human_trace_segment, continuation ? 1 : 0,
		sg_human_trace_order + 1U, sg_human_trace_command + 1U,
		sg_human_trace_hook_event + 1U, sg_human_trace_identity.mapname,
		sg_human_trace_identity.bsp_checksum,
		sg_human_trace_identity.entity_crc32,
		sg_human_trace_identity.host_physics_id,
		LMCTF_REVISION, LMCTF_VERSION);
	if (!header.valid ||
	    !HumanTraceWriteAuthenticated(header.bytes, header.length))
	{
		HumanTraceDisable("segment header write failed");
		return false;
	}
	gi.dprintf("humantrace: recording passive evidence to %s\n", path);
	return true;
}

static qboolean HumanTraceOpen(void)
{
	cvar_t *game_directory;
	const char *directory;
	char path[1024];
	uint32_t candidate;

	if (sg_human_trace_file)
		return true;
	if (sg_human_trace_open_failed || sg_human_trace_match_ended)
		return false;
	if (SG_LevelIdentitySnapshot(level.mapname,
	        &sg_human_trace_identity) != SG_IDENTITY_OK ||
	    !HumanTraceSafeName(sg_human_trace_identity.mapname))
	{
		HumanTraceDisable("level identity is unavailable or unsafe");
		return false;
	}
	game_directory = gi.cvar("gamedir", "", 0);
	directory = sg_cv.humantracedir->string;
	if (!directory[0])
		directory = game_directory && game_directory->string[0]
			? game_directory->string : ".";
	if (snprintf(sg_human_trace_directory,
	        sizeof(sg_human_trace_directory), "%s", directory) >=
	    (int)sizeof(sg_human_trace_directory))
	{
		HumanTraceDisable("output directory is too long");
		return false;
	}
	for (candidate = 0U; candidate < SG_HUMAN_TRACE_MAX_SEGMENTS;
	     candidate++)
	{
		sg_human_trace_segment = candidate;
		if (!HumanTracePath(path, candidate))
		{
			HumanTraceDisable("output path is too long");
			return false;
		}
		if (!HumanTraceFileExists(path))
			break;
	}
	if (candidate == SG_HUMAN_TRACE_MAX_SEGMENTS)
	{
		HumanTraceDisable("segment capacity exhausted");
		return false;
	}
	sg_human_trace_session = candidate;
	memset(sg_human_trace_previous_sha256, '0', 64U);
	sg_human_trace_previous_sha256[64] = '\0';
	if (!HumanTraceOpenSegment(false))
	{
		HumanTraceDisable("segment create failed");
		return false;
	}
	return true;
}

static qboolean HumanTraceRotate(void)
{
	if (sg_human_trace_file)
	{
		if (fclose(sg_human_trace_file) != 0)
		{
			sg_human_trace_file = NULL;
			HumanTraceDisable("segment close failed");
			return false;
		}
		sg_human_trace_file = NULL;
	}
	if (sg_human_trace_segment == UINT32_MAX)
	{
		HumanTraceDisable("segment counter exhausted");
		return false;
	}
	sg_human_trace_segment++;
	if (!HumanTraceOpenSegment(true))
	{
		HumanTraceDisable("rotation or capacity failed");
		return false;
	}
	return true;
}

static qboolean HumanTraceCommit(const human_trace_builder_t *builder)
{
	size_t authenticated_size;

	if (!builder->valid || builder->length < 2U)
	{
		HumanTraceDisable("record exceeded the fixed line capacity");
		return false;
	}
	if (!HumanTraceOpen())
		return false;
	authenticated_size = builder->length + 166U;
	if (sg_human_trace_segment_bytes + authenticated_size >
	    SG_HUMAN_TRACE_SEGMENT_BYTES && !HumanTraceRotate())
		return false;
	if (!HumanTraceWriteAuthenticated(builder->bytes, builder->length))
	{
		HumanTraceDisable("record write failed");
		return false;
	}
	return true;
}

static qboolean HumanTraceVectorQ8(const vec3_t vector, int32_t out[3])
{
	int i;

	for (i = 0; i < 3; i++)
	{
		double scaled = (double)vector[i] * 8.0;
		if (!isfinite(scaled) || scaled < (double)INT32_MIN ||
		    scaled > (double)INT32_MAX)
			return false;
		out[i] = (int32_t)lround(scaled);
	}
	return true;
}

static void HumanTraceState(human_trace_builder_t *builder,
	const pmove_state_t *state)
{
	HumanTraceBuilderAppend(builder,
		"{\"type\":%d,\"origin\":[%d,%d,%d],"
		"\"velocity\":[%d,%d,%d],\"flags\":%u,\"time\":%u,"
		"\"gravity\":%d,\"delta_angles\":[%d,%d,%d]}",
		(int)state->pm_type, (int)state->origin[0],
		(int)state->origin[1], (int)state->origin[2],
		(int)state->velocity[0], (int)state->velocity[1],
		(int)state->velocity[2], (unsigned int)state->pm_flags,
		(unsigned int)state->pm_time, (int)state->gravity,
		(int)state->delta_angles[0], (int)state->delta_angles[1],
		(int)state->delta_angles[2]);
}

static qboolean HumanTraceHuman(const edict_t *entity, int *client_key,
	uint64_t *spawn_generation)
{
	int key;

	if (!entity || !entity->client || !entity->inuse ||
	    (entity->flags & FL_BOT) || entity->client->ctf.ctfid == 0UL)
		return false;
	key = HumanTraceEntityKey(entity);
	if (key <= 0)
		return false;
	*client_key = key;
	*spawn_generation = (uint64_t)entity->client->ctf.ctfid;
	return true;
}

static qboolean HumanTraceReady(edict_t *entity, int *client_key,
	uint64_t *spawn_generation)
{
	SG_CvarsInit();
	return sg_cv.humantrace->value &&
		HumanTraceHuman(entity, client_key, spawn_generation);
}

void SG_HumanTraceNewLevel(void)
{
	if (sg_human_trace_file)
		fclose(sg_human_trace_file);
	sg_human_trace_file = NULL;
	memset(&sg_human_trace_identity, 0, sizeof(sg_human_trace_identity));
	sg_human_trace_directory[0] = '\0';
	sg_human_trace_order = 0U;
	sg_human_trace_command = 0U;
	sg_human_trace_hook_event = 0U;
	sg_human_trace_session = 0U;
	sg_human_trace_segment = 0U;
	sg_human_trace_segment_bytes = 0U;
	memset(sg_human_trace_previous_sha256, '0', 64U);
	sg_human_trace_previous_sha256[64] = '\0';
	sg_human_trace_open_failed = false;
	sg_human_trace_match_ended = false;
}

void SG_HumanTraceMatchEnd(void)
{
	human_trace_builder_t builder;

	if (sg_human_trace_file && !sg_human_trace_open_failed &&
	    sg_human_trace_order != UINT64_MAX)
	{
		HumanTraceBuilderBegin(&builder);
		HumanTraceBuilderAppend(&builder,
			"{\"format\":\"%s\",\"kind\":\"end\","
		"\"order\":%" PRIu64 ",\"frame\":%d,"
		"\"level_time_bits\":%" PRIu32 "}",
			SG_HUMAN_TRACE_FORMAT, sg_human_trace_order + 1U,
			level.framenum, HumanTraceLevelTimeBits());
		if (HumanTraceCommit(&builder))
			sg_human_trace_order++;
	}
	if (sg_human_trace_file)
	{
		if (fclose(sg_human_trace_file) != 0)
			gi.dprintf("humantrace: match-end close failed\n");
		sg_human_trace_file = NULL;
	}
	sg_human_trace_match_ended = true;
}

void SG_HumanTracePmove(edict_t *entity,
	const pmove_state_t *before, const pmove_t *after)
{
	human_trace_builder_t builder;
	const usercmd_t *command;
	uint64_t spawn_generation;
	uint64_t next_order, next_command;
	int client_key;
	int i;

	if (!before || !after || before->pm_type != PM_NORMAL ||
	    after->numtouch < 0 || after->numtouch > MAXTOUCH ||
	    !HumanTraceReady(entity, &client_key, &spawn_generation) ||
	    sg_human_trace_order == UINT64_MAX ||
	    sg_human_trace_command == UINT64_MAX)
		return;
	next_order = sg_human_trace_order + 1U;
	next_command = sg_human_trace_command + 1U;
	command = &after->cmd;
	HumanTraceBuilderBegin(&builder);
	HumanTraceBuilderAppend(&builder,
		"{\"format\":\"%s\",\"kind\":\"step\","
		"\"order\":%" PRIu64 ",\"command\":%" PRIu64 ","
		"\"client\":%d,\"spawn_generation\":%" PRIu64 ","
		"\"frame\":%d,\"level_time_bits\":%" PRIu32 ","
		"\"snapinitial\":%d,"
		"\"cmd\":{\"msec\":%u,\"buttons\":%u,"
		"\"angles\":[%d,%d,%d],\"forward\":%d,\"side\":%d,"
		"\"up\":%d,\"impulse\":%u,\"light\":%u},\"before\":",
		SG_HUMAN_TRACE_FORMAT, next_order, next_command, client_key,
		spawn_generation, level.framenum, HumanTraceLevelTimeBits(),
		after->snapinitial ? 1 : 0,
		(unsigned int)command->msec, (unsigned int)command->buttons,
		(int)command->angles[0], (int)command->angles[1],
		(int)command->angles[2], (int)command->forwardmove,
		(int)command->sidemove, (int)command->upmove,
		(unsigned int)command->impulse,
		(unsigned int)command->lightlevel);
	HumanTraceState(&builder, before);
	HumanTraceBuilderAppend(&builder, ",\"after\":");
	HumanTraceState(&builder, &after->s);
	HumanTraceBuilderAppend(&builder,
		",\"ground\":%d,\"waterlevel\":%d,\"watertype\":%d,"
		"\"touches\":[", HumanTraceEntityKey(after->groundentity),
		after->waterlevel, after->watertype);
	for (i = 0; i < after->numtouch; i++)
		HumanTraceBuilderAppend(&builder, "%s%d", i ? "," : "",
			HumanTraceEntityKey(after->touchents[i]));
	HumanTraceBuilderAppend(&builder, "]}");
	if (HumanTraceCommit(&builder))
	{
		sg_human_trace_order = next_order;
		sg_human_trace_command = next_command;
	}
}

void SG_HumanTraceHookFire(edict_t *entity, edict_t *hook)
{
	human_trace_builder_t builder;
	uint64_t spawn_generation;
	uint64_t next_order, next_event;
	int32_t origin[3], velocity[3];
	int client_key, hook_key;

	if (!HumanTraceReady(entity, &client_key, &spawn_generation) ||
	    !hook || hook->owner != entity ||
	    !HumanTraceVectorQ8(entity->s.origin, origin) ||
	    !HumanTraceVectorQ8(entity->velocity, velocity) ||
	    (hook_key = HumanTraceEntityKey(hook)) <= 0 ||
	    sg_human_trace_order == UINT64_MAX ||
	    sg_human_trace_hook_event == UINT64_MAX)
		return;
	next_order = sg_human_trace_order + 1U;
	next_event = sg_human_trace_hook_event + 1U;
	HumanTraceBuilderBegin(&builder);
	HumanTraceBuilderAppend(&builder,
		"{\"format\":\"%s\",\"kind\":\"hook-fire\","
		"\"order\":%" PRIu64 ",\"hook_event\":%" PRIu64 ","
		"\"after_command\":%" PRIu64 ",\"client\":%d,"
		"\"spawn_generation\":%" PRIu64 ",\"frame\":%d,"
		"\"level_time_bits\":%" PRIu32 ","
		"\"hook\":%d,\"origin_q8\":[%d,%d,%d],"
		"\"velocity_q8\":[%d,%d,%d],\"view_short\":[%d,%d],"
		"\"hand\":%d}", SG_HUMAN_TRACE_FORMAT, next_order,
		next_event, sg_human_trace_command, client_key, spawn_generation,
		level.framenum, HumanTraceLevelTimeBits(), hook_key,
		origin[0], origin[1], origin[2],
		velocity[0], velocity[1], velocity[2],
		(short)ANGLE2SHORT(entity->client->v_angle[PITCH]),
		(short)ANGLE2SHORT(entity->client->v_angle[YAW]),
		entity->client->pers.hand);
	if (HumanTraceCommit(&builder))
	{
		sg_human_trace_order = next_order;
		sg_human_trace_hook_event = next_event;
	}
}

void SG_HumanTraceHookAttach(edict_t *entity, edict_t *hook,
	edict_t *target)
{
	human_trace_builder_t builder;
	uint64_t spawn_generation;
	uint64_t next_order, next_event;
	int32_t bite[3];
	int client_key, hook_key, target_key;

	if (!HumanTraceReady(entity, &client_key, &spawn_generation) ||
	    !hook || !target || hook->owner != entity ||
	    hook->hook_target != target ||
	    !HumanTraceVectorQ8(hook->s.origin, bite) ||
	    (hook_key = HumanTraceEntityKey(hook)) <= 0 ||
	    (target_key = HumanTraceEntityKey(target)) < 0 ||
	    sg_human_trace_order == UINT64_MAX ||
	    sg_human_trace_hook_event == UINT64_MAX)
		return;
	next_order = sg_human_trace_order + 1U;
	next_event = sg_human_trace_hook_event + 1U;
	HumanTraceBuilderBegin(&builder);
	HumanTraceBuilderAppend(&builder,
		"{\"format\":\"%s\",\"kind\":\"hook-attach\","
		"\"order\":%" PRIu64 ",\"hook_event\":%" PRIu64 ","
		"\"after_command\":%" PRIu64 ",\"client\":%d,"
		"\"spawn_generation\":%" PRIu64 ",\"frame\":%d,"
		"\"level_time_bits\":%" PRIu32 ","
		"\"hook\":%d,\"bite_q8\":[%d,%d,%d],"
		"\"target\":%d,\"world\":%d}", SG_HUMAN_TRACE_FORMAT,
		next_order, next_event, sg_human_trace_command, client_key,
		spawn_generation, level.framenum, HumanTraceLevelTimeBits(),
		hook_key, bite[0], bite[1],
		bite[2], target_key, target == g_edicts ? 1 : 0);
	if (HumanTraceCommit(&builder))
	{
		sg_human_trace_order = next_order;
		sg_human_trace_hook_event = next_event;
	}
}

static void HumanTraceHookTerminal(edict_t *entity, edict_t *hook,
	const char *kind)
{
	human_trace_builder_t builder;
	uint64_t spawn_generation;
	uint64_t next_order, next_event;
	int32_t origin[3], velocity[3];
	int client_key, hook_key;

	if (!HumanTraceReady(entity, &client_key, &spawn_generation) ||
	    !hook || hook->owner != entity ||
	    !HumanTraceVectorQ8(entity->s.origin, origin) ||
	    !HumanTraceVectorQ8(entity->velocity, velocity) ||
	    (hook_key = HumanTraceEntityKey(hook)) <= 0 ||
	    sg_human_trace_order == UINT64_MAX ||
	    sg_human_trace_hook_event == UINT64_MAX)
		return;
	next_order = sg_human_trace_order + 1U;
	next_event = sg_human_trace_hook_event + 1U;
	HumanTraceBuilderBegin(&builder);
	HumanTraceBuilderAppend(&builder,
		"{\"format\":\"%s\",\"kind\":\"%s\","
		"\"order\":%" PRIu64 ",\"hook_event\":%" PRIu64 ","
		"\"after_command\":%" PRIu64 ",\"client\":%d,"
		"\"spawn_generation\":%" PRIu64 ",\"frame\":%d,"
		"\"level_time_bits\":%" PRIu32 ","
		"\"hook\":%d,\"origin_q8\":[%d,%d,%d],"
		"\"velocity_q8\":[%d,%d,%d]}", SG_HUMAN_TRACE_FORMAT,
		kind, next_order, next_event, sg_human_trace_command, client_key,
		spawn_generation, level.framenum, HumanTraceLevelTimeBits(),
		hook_key, origin[0], origin[1],
		origin[2], velocity[0], velocity[1], velocity[2]);
	if (HumanTraceCommit(&builder))
	{
		sg_human_trace_order = next_order;
		sg_human_trace_hook_event = next_event;
	}
}

void SG_HumanTraceHookRelease(edict_t *entity)
{
	if (!entity || !entity->client)
		return;
	HumanTraceHookTerminal(entity, entity->client->hook, "hook-release");
}

void SG_HumanTraceHookReset(edict_t *entity, edict_t *hook)
{
	HumanTraceHookTerminal(entity, hook, "hook-reset");
}
