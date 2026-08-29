#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "../g_local.h"
#include "sg_cvars.h"
#include "sg_human_trace.h"
#include "sg_identity.h"

#include <errno.h>
#include <inttypes.h>
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
#include <sys/resource.h>
#include <unistd.h>
#endif

#define SG_HUMAN_TRACE_FORMAT "lmctf-human-trace-v3"
#define SG_HUMAN_TRACE_LINE_BYTES 8192U
#define SG_HUMAN_TRACE_PMOVE_SUBSTEP_MS 25U
#define SG_HUMAN_TRACE_SERVER_FRAME_MS 100U
#define SG_HUMAN_TRACE_PHYSICS_FUNKY_GRAVITY UINT32_C(1)
#ifndef SG_HUMAN_TRACE_SEGMENT_BYTES
#define SG_HUMAN_TRACE_SEGMENT_BYTES (64U * 1024U * 1024U)
#endif
#ifndef SG_HUMAN_TRACE_MAX_SEGMENTS
#define SG_HUMAN_TRACE_MAX_SEGMENTS 1000000U
#endif

_Static_assert(sizeof(((level_locals_t *)0)->time) == sizeof(uint32_t),
	"human trace timing requires a 32-bit level.time");
_Static_assert(sizeof(float) == sizeof(uint32_t),
	"human trace exact values require 32-bit float storage");

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

typedef struct human_trace_physics_s
{
	uint32_t gravity_bits;
	uint32_t airaccelerate_bits;
	uint32_t maxvelocity_bits;
	uint32_t flags;
	uint16_t pmove_substep_ms;
	uint16_t server_frame_ms;
} human_trace_physics_t;

typedef struct human_trace_hook_snapshot_s
{
	uint32_t origin_bits[3];
	uint32_t velocity_bits[3];
	uint32_t mins_bits[3];
	uint32_t maxs_bits[3];
	uint32_t viewangles_bits[3];
	uint32_t viewheight_bits;
	uint32_t hook_origin_bits[3];
	uint32_t hook_velocity_bits[3];
	uint32_t hook_offset_bits[3];
	uint32_t hookend_bits[3];
	uint32_t hookangle_bits[3];
	int hook_state;
	int hook_length;
	int hand;
	int hook_target;
} human_trace_hook_snapshot_t;

typedef enum human_trace_create_result_e
{
	HUMAN_TRACE_CREATE_OK = 0,
	HUMAN_TRACE_CREATE_COLLISION,
	HUMAN_TRACE_CREATE_ERROR
} human_trace_create_result_t;

static FILE *sg_human_trace_file;
static sg_level_identity_t sg_human_trace_identity;
static human_trace_physics_t sg_human_trace_physics;
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

static int HumanTraceEntityKey(const edict_t *entity);

static uint32_t HumanTraceFloatBits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static void HumanTraceVectorBits(const vec3_t vector, uint32_t bits[3])
{
	int axis;

	for (axis = 0; axis < 3; axis++)
		bits[axis] = HumanTraceFloatBits(vector[axis]);
}

static void HumanTraceBuilderVectorBits(human_trace_builder_t *builder,
	const char *name, const uint32_t bits[3])
{
	HumanTraceBuilderAppend(builder,
		",\"%s\":[%" PRIu32 ",%" PRIu32 ",%" PRIu32 "]",
		name, bits[0], bits[1], bits[2]);
}

static qboolean HumanTracePhysicsCapture(human_trace_physics_t *physics)
{
	cvar_t *airaccelerate;

	if (!physics || !gi.cvar || !sv_gravity || !sv_maxvelocity ||
	    !want_funky_gravity)
		return false;
	if (FRAMETIME * 1000.0f !=
	    (float)SG_HUMAN_TRACE_SERVER_FRAME_MS)
		return false;
	airaccelerate = gi.cvar("sv_airaccelerate", "0", 0);
	if (!airaccelerate)
		return false;
	physics->gravity_bits = HumanTraceFloatBits(sv_gravity->value);
	physics->airaccelerate_bits =
		HumanTraceFloatBits(airaccelerate->value);
	physics->maxvelocity_bits = HumanTraceFloatBits(sv_maxvelocity->value);
	physics->flags = want_funky_gravity->value != 0.0f
		? SG_HUMAN_TRACE_PHYSICS_FUNKY_GRAVITY : 0U;
	physics->pmove_substep_ms = SG_HUMAN_TRACE_PMOVE_SUBSTEP_MS;
	physics->server_frame_ms = SG_HUMAN_TRACE_SERVER_FRAME_MS;
	return true;
}

static qboolean HumanTracePhysicsEqual(const human_trace_physics_t *left,
	const human_trace_physics_t *right)
{
	return left->gravity_bits == right->gravity_bits &&
		left->airaccelerate_bits == right->airaccelerate_bits &&
		left->maxvelocity_bits == right->maxvelocity_bits &&
		left->flags == right->flags &&
		left->pmove_substep_ms == right->pmove_substep_ms &&
		left->server_frame_ms == right->server_frame_ms;
}

static void HumanTraceHookSnapshot(const edict_t *entity,
	const edict_t *hook, human_trace_hook_snapshot_t *snapshot)
{
	memset(snapshot, 0, sizeof(*snapshot));
	HumanTraceVectorBits(entity->s.origin, snapshot->origin_bits);
	HumanTraceVectorBits(entity->velocity, snapshot->velocity_bits);
	HumanTraceVectorBits(entity->mins, snapshot->mins_bits);
	HumanTraceVectorBits(entity->maxs, snapshot->maxs_bits);
	HumanTraceVectorBits(entity->client->v_angle,
		snapshot->viewangles_bits);
	snapshot->viewheight_bits = HumanTraceFloatBits((float)entity->viewheight);
	HumanTraceVectorBits(hook->s.origin, snapshot->hook_origin_bits);
	HumanTraceVectorBits(hook->velocity, snapshot->hook_velocity_bits);
	HumanTraceVectorBits(hook->hook_offset, snapshot->hook_offset_bits);
	HumanTraceVectorBits(entity->client->hookend, snapshot->hookend_bits);
	HumanTraceVectorBits(entity->client->hookangle,
		snapshot->hookangle_bits);
	snapshot->hook_state = entity->client->hookstate;
	snapshot->hook_length = entity->client->hooklength;
	snapshot->hand = entity->client->pers.hand;
	snapshot->hook_target = HumanTraceEntityKey(hook->hook_target);
}

static void HumanTraceBuilderHookSnapshot(human_trace_builder_t *builder,
	const human_trace_hook_snapshot_t *snapshot)
{
	HumanTraceBuilderVectorBits(builder, "origin_bits",
		snapshot->origin_bits);
	HumanTraceBuilderVectorBits(builder, "velocity_bits",
		snapshot->velocity_bits);
	HumanTraceBuilderVectorBits(builder, "mins_bits", snapshot->mins_bits);
	HumanTraceBuilderVectorBits(builder, "maxs_bits", snapshot->maxs_bits);
	HumanTraceBuilderVectorBits(builder, "viewangles_bits",
		snapshot->viewangles_bits);
	HumanTraceBuilderAppend(builder, ",\"viewheight_bits\":%" PRIu32,
		snapshot->viewheight_bits);
	HumanTraceBuilderVectorBits(builder, "hook_origin_bits",
		snapshot->hook_origin_bits);
	HumanTraceBuilderVectorBits(builder, "hook_velocity_bits",
		snapshot->hook_velocity_bits);
	HumanTraceBuilderVectorBits(builder, "hook_offset_bits",
		snapshot->hook_offset_bits);
	HumanTraceBuilderVectorBits(builder, "hookend_bits",
		snapshot->hookend_bits);
	HumanTraceBuilderVectorBits(builder, "hookangle_bits",
		snapshot->hookangle_bits);
	HumanTraceBuilderAppend(builder,
		",\"hook_state\":%d,\"hook_length\":%d,\"hand\":%d,"
		"\"hook_target\":%d",
		snapshot->hook_state, snapshot->hook_length, snapshot->hand,
		snapshot->hook_target);
}

static int HumanTraceEntityKey(const edict_t *entity)
{
	uintptr_t address;
	uintptr_t base;
	uintptr_t offset;
	size_t extent;

	if (!entity)
		return -1;
	if (!g_edicts)
		return -2;
	address = (uintptr_t)entity;
	base = (uintptr_t)g_edicts;
	extent = (size_t)globals.num_edicts * sizeof(*g_edicts);
	if (address < base)
		return -2;
	offset = address - base;
	if (offset >= extent || offset % sizeof(*g_edicts))
		return -2;
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

static human_trace_create_result_t HumanTraceCreateExclusive(
	const char *path, FILE **out)
{
	int descriptor;
	FILE *file;

	*out = NULL;
#ifdef _WIN32
	descriptor = _open(path, _O_WRONLY | _O_CREAT | _O_EXCL | _O_BINARY,
		_S_IREAD | _S_IWRITE);
	if (descriptor < 0)
		return errno == EEXIST ? HUMAN_TRACE_CREATE_COLLISION :
			HUMAN_TRACE_CREATE_ERROR;
	file = _fdopen(descriptor, "wb");
	if (!file)
		_close(descriptor);
#else
	descriptor = open(path, O_WRONLY | O_CREAT | O_EXCL, 0666);
	if (descriptor < 0)
		return errno == EEXIST ? HUMAN_TRACE_CREATE_COLLISION :
			HUMAN_TRACE_CREATE_ERROR;
	file = fdopen(descriptor, "wb");
	if (!file)
		close(descriptor);
#endif
	if (!file)
		return HUMAN_TRACE_CREATE_ERROR;
	if (setvbuf(file, NULL, _IONBF, 0) != 0)
	{
		fclose(file);
		return HUMAN_TRACE_CREATE_ERROR;
	}
	*out = file;
	return HUMAN_TRACE_CREATE_OK;
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

static qboolean HumanTraceWriteFitsFileLimit(size_t current_bytes,
	size_t length)
{
#ifdef _WIN32
	(void)current_bytes;
	(void)length;
	return true;
#else
	struct rlimit limit;

	if (getrlimit(RLIMIT_FSIZE, &limit) != 0)
		return false;
	if (limit.rlim_cur == RLIM_INFINITY)
		return true;
	if ((uintmax_t)current_bytes >
	    (uintmax_t)limit.rlim_cur)
		return false;
	return (uintmax_t)length <= (uintmax_t)limit.rlim_cur -
		(uintmax_t)current_bytes;
#endif
}

static qboolean HumanTraceWriteAuthenticated(FILE *file,
	size_t *segment_bytes, const char *payload, size_t payload_length)
{
	unsigned char digest_input[65U + SG_HUMAN_TRACE_LINE_BYTES];
	char digest[65];
	char line[SG_HUMAN_TRACE_LINE_BYTES];
	int written;
	size_t line_length;

	if (!file || !segment_bytes || payload_length < 2U ||
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
	if (!HumanTraceWriteFitsFileLimit(*segment_bytes, line_length))
		return false;
	if (fwrite(line, 1U, line_length, file) != line_length ||
	    fflush(file) != 0)
		return false;
	strcpy(sg_human_trace_previous_sha256, digest);
	*segment_bytes += line_length;
	return true;
}

static qboolean HumanTraceOpenSegment(qboolean continuation)
{
	human_trace_builder_t header;
	char path[1024];
	human_trace_create_result_t created;
	FILE *candidate_file = NULL;
	uint32_t candidate = sg_human_trace_segment;
	uint32_t session;
	size_t segment_bytes = 0U;

	for (;;)
	{
		if (candidate >= SG_HUMAN_TRACE_MAX_SEGMENTS ||
		    !HumanTracePath(path, candidate))
			return false;
		created = HumanTraceCreateExclusive(path, &candidate_file);
		if (created == HUMAN_TRACE_CREATE_OK)
			break;
		if (created != HUMAN_TRACE_CREATE_COLLISION)
			return false;
		candidate++;
	}
	session = continuation ? sg_human_trace_session : candidate;
	HumanTraceBuilderBegin(&header);
	HumanTraceBuilderAppend(&header,
		"{\"format\":\"%s\",\"kind\":\"header\","
		"\"session\":%" PRIu32 ",\"segment\":%" PRIu32 ","
		"\"continuation\":%d,\"start_order\":%" PRIu64 ","
		"\"start_command\":%" PRIu64 ","
		"\"start_hook_event\":%" PRIu64 ","
		"\"map\":\"%s\",\"bsp_checksum\":%" PRIu32 ","
		"\"entity_crc32\":%" PRIu32 ",\"physics_id\":0,"
		"\"host_physics_id\":%" PRIu32 ","
		"\"gravity_bits\":%" PRIu32 ","
		"\"airaccelerate_bits\":%" PRIu32 ","
		"\"maxvelocity_bits\":%" PRIu32 ","
		"\"pmove_substep_ms\":%u,\"server_frame_ms\":%u,"
		"\"physics_flags\":%" PRIu32 ","
		"\"module_revision\":%d,\"module_version\":\"%s\"}",
		SG_HUMAN_TRACE_FORMAT, session, candidate, continuation ? 1 : 0,
		sg_human_trace_order + 1U, sg_human_trace_command + 1U,
		sg_human_trace_hook_event + 1U, sg_human_trace_identity.mapname,
		sg_human_trace_identity.bsp_checksum,
		sg_human_trace_identity.entity_crc32,
		sg_human_trace_identity.host_physics_id,
		sg_human_trace_physics.gravity_bits,
		sg_human_trace_physics.airaccelerate_bits,
		sg_human_trace_physics.maxvelocity_bits,
		(unsigned)sg_human_trace_physics.pmove_substep_ms,
		(unsigned)sg_human_trace_physics.server_frame_ms,
		sg_human_trace_physics.flags,
		LMCTF_REVISION, LMCTF_VERSION);
	if (!header.valid ||
	    !HumanTraceWriteAuthenticated(candidate_file, &segment_bytes,
	        header.bytes, header.length))
	{
		fclose(candidate_file);
		return false;
	}
	sg_human_trace_file = candidate_file;
	sg_human_trace_session = session;
	sg_human_trace_segment = candidate;
	sg_human_trace_segment_bytes = segment_bytes;
	gi.dprintf("humantrace: recording passive evidence to %s\n", path);
	return true;
}

static qboolean HumanTraceOpen(void)
{
	cvar_t *game_directory;
	const char *directory;

	if (sg_human_trace_file)
		return true;
	if (sg_human_trace_open_failed || sg_human_trace_match_ended)
		return false;
	if (SG_LevelIdentitySnapshot(level.mapname,
	        &sg_human_trace_identity) != SG_IDENTITY_OK ||
	    !HumanTraceSafeName(sg_human_trace_identity.mapname) ||
	    !HumanTracePhysicsCapture(&sg_human_trace_physics))
	{
		HumanTraceDisable("level or physics identity is unavailable");
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
	sg_human_trace_segment = 0U;
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

static qboolean HumanTracePrepareRecord(void)
{
	human_trace_physics_t current_physics;

	if (!HumanTraceOpen())
		return false;
	if (!HumanTracePhysicsCapture(&current_physics))
	{
		HumanTraceDisable("physics identity became unavailable");
		return false;
	}
	if (!HumanTracePhysicsEqual(&current_physics,
	        &sg_human_trace_physics))
	{
		sg_human_trace_physics = current_physics;
		if (!HumanTraceRotate())
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
	if (!sg_human_trace_file)
		return false;
	authenticated_size = builder->length + 166U;
	if (sg_human_trace_segment_bytes + authenticated_size >
	    SG_HUMAN_TRACE_SEGMENT_BYTES && !HumanTraceRotate())
		return false;
	if (!HumanTraceWriteAuthenticated(sg_human_trace_file,
	        &sg_human_trace_segment_bytes, builder->bytes, builder->length))
	{
		HumanTraceDisable("record write failed");
		return false;
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
	memset(&sg_human_trace_physics, 0, sizeof(sg_human_trace_physics));
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
	    sg_human_trace_order != UINT64_MAX && HumanTracePrepareRecord())
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
	uint32_t vector_bits[3];
	int client_key;
	int i;

	if (!before || !after || before->pm_type != PM_NORMAL ||
	    after->numtouch < 0 || after->numtouch > MAXTOUCH ||
	    !HumanTraceReady(entity, &client_key, &spawn_generation) ||
	    sg_human_trace_order == UINT64_MAX ||
	    sg_human_trace_command == UINT64_MAX)
		return;
	if (!HumanTracePrepareRecord())
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
	HumanTraceVectorBits(after->viewangles, vector_bits);
	HumanTraceBuilderVectorBits(&builder, "viewangles_bits", vector_bits);
	HumanTraceBuilderAppend(&builder, ",\"viewheight_bits\":%" PRIu32,
		HumanTraceFloatBits(after->viewheight));
	HumanTraceVectorBits(after->mins, vector_bits);
	HumanTraceBuilderVectorBits(&builder, "mins_bits", vector_bits);
	HumanTraceVectorBits(after->maxs, vector_bits);
	HumanTraceBuilderVectorBits(&builder, "maxs_bits", vector_bits);
	HumanTraceBuilderAppend(&builder,
		",\"ground\":%d,\"waterlevel\":%d,\"watertype\":%d,"
		"\"numtouch\":%d,"
		"\"touches\":[", HumanTraceEntityKey(after->groundentity),
		after->waterlevel, after->watertype, after->numtouch);
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
	human_trace_hook_snapshot_t snapshot;
	uint64_t spawn_generation;
	uint64_t next_order, next_event;
	int client_key, hook_key;

	if (!HumanTraceReady(entity, &client_key, &spawn_generation) ||
	    !hook || hook->owner != entity ||
	    (hook_key = HumanTraceEntityKey(hook)) <= 0 ||
	    sg_human_trace_order == UINT64_MAX ||
	    sg_human_trace_hook_event == UINT64_MAX)
		return;
	if (!HumanTracePrepareRecord())
		return;
	next_order = sg_human_trace_order + 1U;
	next_event = sg_human_trace_hook_event + 1U;
	HumanTraceHookSnapshot(entity, hook, &snapshot);
	HumanTraceBuilderBegin(&builder);
	HumanTraceBuilderAppend(&builder,
		"{\"format\":\"%s\",\"kind\":\"hook-fire\","
		"\"order\":%" PRIu64 ",\"hook_event\":%" PRIu64 ","
		"\"after_command\":%" PRIu64 ",\"client\":%d,"
		"\"spawn_generation\":%" PRIu64 ",\"frame\":%d,"
		"\"level_time_bits\":%" PRIu32 ","
		"\"hook\":%d", SG_HUMAN_TRACE_FORMAT, next_order,
		next_event, sg_human_trace_command, client_key, spawn_generation,
		level.framenum, HumanTraceLevelTimeBits(), hook_key);
	HumanTraceBuilderHookSnapshot(&builder, &snapshot);
	HumanTraceBuilderAppend(&builder, "}");
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
	human_trace_hook_snapshot_t snapshot;
	uint64_t spawn_generation;
	uint64_t next_order, next_event;
	int client_key, hook_key, target_key;

	if (!HumanTraceReady(entity, &client_key, &spawn_generation) ||
	    !hook || !target || hook->owner != entity ||
	    hook->hook_target != target ||
	    (hook_key = HumanTraceEntityKey(hook)) <= 0 ||
	    (target_key = HumanTraceEntityKey(target)) < 0 ||
	    sg_human_trace_order == UINT64_MAX ||
	    sg_human_trace_hook_event == UINT64_MAX)
		return;
	if (!HumanTracePrepareRecord())
		return;
	next_order = sg_human_trace_order + 1U;
	next_event = sg_human_trace_hook_event + 1U;
	HumanTraceHookSnapshot(entity, hook, &snapshot);
	HumanTraceBuilderBegin(&builder);
	HumanTraceBuilderAppend(&builder,
		"{\"format\":\"%s\",\"kind\":\"hook-attach\","
		"\"order\":%" PRIu64 ",\"hook_event\":%" PRIu64 ","
		"\"after_command\":%" PRIu64 ",\"client\":%d,"
		"\"spawn_generation\":%" PRIu64 ",\"frame\":%d,"
		"\"level_time_bits\":%" PRIu32 ","
		"\"hook\":%d,\"target\":%d,\"world\":%d",
		SG_HUMAN_TRACE_FORMAT,
		next_order, next_event, sg_human_trace_command, client_key,
		spawn_generation, level.framenum, HumanTraceLevelTimeBits(),
		hook_key, target_key, target == g_edicts ? 1 : 0);
	HumanTraceBuilderHookSnapshot(&builder, &snapshot);
	HumanTraceBuilderAppend(&builder, "}");
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
	human_trace_hook_snapshot_t snapshot;
	uint64_t spawn_generation;
	uint64_t next_order, next_event;
	int client_key, hook_key;

	if (!HumanTraceReady(entity, &client_key, &spawn_generation) ||
	    !hook || hook->owner != entity ||
	    (hook_key = HumanTraceEntityKey(hook)) <= 0 ||
	    sg_human_trace_order == UINT64_MAX ||
	    sg_human_trace_hook_event == UINT64_MAX)
		return;
	if (!HumanTracePrepareRecord())
		return;
	next_order = sg_human_trace_order + 1U;
	next_event = sg_human_trace_hook_event + 1U;
	HumanTraceHookSnapshot(entity, hook, &snapshot);
	HumanTraceBuilderBegin(&builder);
	HumanTraceBuilderAppend(&builder,
		"{\"format\":\"%s\",\"kind\":\"%s\","
		"\"order\":%" PRIu64 ",\"hook_event\":%" PRIu64 ","
		"\"after_command\":%" PRIu64 ",\"client\":%d,"
		"\"spawn_generation\":%" PRIu64 ",\"frame\":%d,"
		"\"level_time_bits\":%" PRIu32 ","
		"\"hook\":%d", SG_HUMAN_TRACE_FORMAT,
		kind, next_order, next_event, sg_human_trace_command, client_key,
		spawn_generation, level.framenum, HumanTraceLevelTimeBits(),
		hook_key);
	HumanTraceBuilderHookSnapshot(&builder, &snapshot);
	HumanTraceBuilderAppend(&builder, "}");
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
