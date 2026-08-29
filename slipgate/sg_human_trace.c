#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "../g_local.h"
#include "sg_cvars.h"
#include "sg_human_trace.h"
#include "sg_identity.h"
#include "sg_rune_v2_content_identity.h"

#include <errno.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#include <windows.h>
#else
#include <dirent.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

#define SG_HUMAN_TRACE_FORMAT "lmctf-human-trace-v3"
#define SG_HUMAN_TRACE_LINE_BYTES 8192U
#define SG_HUMAN_TRACE_PMOVE_SUBSTEP_MS 25U
#define SG_HUMAN_TRACE_SERVER_FRAME_MS 100U
#define SG_HUMAN_TRACE_PHYSICS_FUNKY_GRAVITY UINT32_C(1)
#define SG_HUMAN_TRACE_SPOOL_MAGIC UINT32_C(0x53475453)
#define SG_HUMAN_TRACE_SPOOL_VERSION UINT16_C(2)
#define SG_HUMAN_TRACE_SPOOL_PAYLOAD_BYTES 512U
#define SG_HUMAN_TRACE_SPOOL_NAME_BYTES 192U
#ifndef SG_HUMAN_TRACE_SEGMENT_BYTES
#define SG_HUMAN_TRACE_SEGMENT_BYTES (64U * 1024U * 1024U)
#endif

_Static_assert(sizeof(((level_locals_t *)0)->time) == sizeof(uint32_t),
	"human trace timing requires a 32-bit level.time");
_Static_assert(sizeof(float) == sizeof(uint32_t),
	"human trace exact values require 32-bit float storage");

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

typedef enum human_trace_spool_frame_kind_e
{
	HUMAN_TRACE_SPOOL_FRAME_HEADER = 1,
	HUMAN_TRACE_SPOOL_FRAME_EVENT,
	HUMAN_TRACE_SPOOL_FRAME_TERMINAL
} human_trace_spool_frame_kind_t;

typedef struct human_trace_spool_header_s
{
	sg_level_identity_t identity;
	uint32_t session;
	uint32_t root_segment;
	uint8_t root_header_sha256[SG_HUMAN_TRACE_SHA256_BYTES];
} human_trace_spool_header_t;

typedef struct human_trace_spool_event_s
{
	sg_human_trace_v3_event_t event;
	uint8_t record_sha256[SG_HUMAN_TRACE_SHA256_BYTES];
} human_trace_spool_event_t;

typedef struct human_trace_json_header_s
{
	sg_level_identity_t identity;
	uint32_t session;
	uint32_t segment;
	uint32_t continuation;
	uint32_t gravity_bits;
	uint32_t airaccelerate_bits;
	uint32_t maxvelocity_bits;
	uint16_t pmove_substep_ms;
	uint16_t server_frame_ms;
	uint32_t physics_flags;
	uint32_t module_revision;
	char module_version[SG_HUMAN_TRACE_VERSION_BYTES];
	uint64_t start_order;
	uint64_t start_command;
	uint64_t start_hook_event;
	uint8_t previous_sha256[SG_HUMAN_TRACE_SHA256_BYTES];
	uint8_t sha256[SG_HUMAN_TRACE_SHA256_BYTES];
} human_trace_json_header_t;

typedef struct human_trace_json_segment_s
{
	human_trace_json_header_t header;
	uint64_t last_order;
	uint64_t last_command;
	uint64_t last_hook_event;
	uint64_t last_hook_command;
	uint32_t last_frame;
	uint8_t have_frame;
	uint8_t ended;
	uint32_t end_level_time_bits;
	uint8_t last_sha256[SG_HUMAN_TRACE_SHA256_BYTES];
	uint8_t terminal_sha256[SG_HUMAN_TRACE_SHA256_BYTES];
} human_trace_json_segment_t;

typedef int (*human_trace_json_event_visitor_fn)(void *context,
	const sg_human_trace_v3_event_t *event,
	const uint8_t record_sha256[SG_HUMAN_TRACE_SHA256_BYTES]);

typedef struct human_trace_spool_frame_s
{
	uint32_t magic;
	uint16_t version;
	uint16_t kind;
	uint32_t payload_bytes;
	uint8_t previous_sha256[SG_HUMAN_TRACE_SHA256_BYTES];
	uint8_t sha256[SG_HUMAN_TRACE_SHA256_BYTES];
	uint8_t payload[SG_HUMAN_TRACE_SPOOL_PAYLOAD_BYTES];
} human_trace_spool_frame_t;

_Static_assert(sizeof(human_trace_spool_header_t) <=
	SG_HUMAN_TRACE_SPOOL_PAYLOAD_BYTES,
	"human trace spool header must fit its fixed record");
_Static_assert(sizeof(human_trace_spool_event_t) <=
	SG_HUMAN_TRACE_SPOOL_PAYLOAD_BYTES,
	"human trace spool event must fit its fixed record");
_Static_assert(sizeof(sg_human_trace_completion_t) <=
	SG_HUMAN_TRACE_SPOOL_PAYLOAD_BYTES,
	"human trace completion must fit its fixed spool record");

static FILE *sg_human_trace_file;
static FILE *sg_human_trace_spool_file;
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
static qboolean sg_human_trace_segment_continuation;
static qboolean sg_human_trace_open_failed;
static qboolean sg_human_trace_match_ended;
static sg_human_trace_completion_t sg_human_trace_completion;
static char sg_human_trace_spool_path[SG_HUMAN_TRACE_SPOOL_PATH_BYTES];
static uint8_t sg_human_trace_spool_previous_sha256[SG_HUMAN_TRACE_SHA256_BYTES];
static qboolean sg_human_trace_event_failed;
static char (*sg_human_trace_segment_paths)[SG_HUMAN_TRACE_SPOOL_PATH_BYTES];
static size_t sg_human_trace_segment_path_count;
static size_t sg_human_trace_segment_path_capacity;

static uint8_t sg_human_trace_collection_active;

static qboolean HumanTraceSpoolAppendEvent(
	const sg_human_trace_v3_event_t *event);
static qboolean HumanTraceSafeName(const char *name);
static qboolean HumanTraceJsonPathFor(const char *directory,
	const sg_level_identity_t *identity, uint32_t segment,
	char path[SG_HUMAN_TRACE_SPOOL_PATH_BYTES]);

static qboolean HumanTraceSHA256(const unsigned char *bytes, size_t length,
	char out[65])
{
	static const char hex[] = "0123456789abcdef";
	sg_rune_v2_content_id_t identity;
	size_t index;

	if (!SG_RuneV2ContentIdentitySHA256(bytes, length, &identity))
		return false;
	for (index = 0U; index < sizeof(identity.bytes); index++)
	{
		out[index * 2U] = hex[identity.bytes[index] >> 4];
		out[index * 2U + 1U] = hex[identity.bytes[index] & 15U];
	}
	out[64] = '\0';
	return true;
}

static int HumanTraceHexNibble(char character)
{
	if (character >= '0' && character <= '9')
		return character - '0';
	if (character >= 'a' && character <= 'f')
		return character - 'a' + 10;
	return -1;
}

static qboolean HumanTraceEventValid(
	const sg_human_trace_v3_event_t *event)
{
	if (!event || event->kind < SG_HUMAN_TRACE_V3_EVENT_STEP ||
		event->kind >= SG_HUMAN_TRACE_V3_EVENT_KIND_COUNT ||
		event->order == 0U || event->client_id == 0U ||
		event->spawn_generation == 0U || event->grounded > 1U ||
		event->reserved != 0U)
		return false;
	if (event->kind == SG_HUMAN_TRACE_V3_EVENT_STEP)
		return event->command != 0U && event->hook_event == 0U &&
			event->after_command == 0U && event->hook_entity == 0;
	return event->command == 0U && event->hook_event != 0U &&
		event->hook_entity > 0 &&
		event->command_msec == 0U;
}

static qboolean HumanTraceEventPrepare(const sg_human_trace_v3_event_t *event)
{
	/* Once the recorder cannot produce a durable witness, do not retry work on
	 * every later Pmove or hook callback. The gameplay hook remains passive and
	 * the incomplete evidence stays unavailable to consumers. */
	if (sg_human_trace_event_failed || !HumanTraceEventValid(event))
	{
		sg_human_trace_event_failed = true;
		return false;
	}
	return true;
}

static void HumanTraceEventCommit(const sg_human_trace_v3_event_t *event)
{
	if (!sg_human_trace_event_failed && !HumanTraceSpoolAppendEvent(event))
		sg_human_trace_event_failed = true;
}

static qboolean HumanTraceCompletionSet(uint64_t end_order,
	uint32_t end_frame, uint32_t end_level_time_bits)
{
	sg_human_trace_completion_t completion;
	uint32_t index;

	memset(&completion, 0, sizeof(completion));
	if (!sg_human_trace_identity.mapname[0] ||
		snprintf(completion.mapname, sizeof(completion.mapname), "%s",
			sg_human_trace_identity.mapname) >=
		(int)sizeof(completion.mapname) ||
		snprintf(completion.module_version, sizeof(completion.module_version),
			"%s", LMCTF_VERSION) >=
		(int)sizeof(completion.module_version))
		return false;
	for (index = 0U; index < SG_HUMAN_TRACE_SHA256_BYTES; index++)
	{
		int high = HumanTraceHexNibble(sg_human_trace_previous_sha256[index * 2U]);
		int low = HumanTraceHexNibble(
			sg_human_trace_previous_sha256[index * 2U + 1U]);

		if (high < 0 || low < 0)
			return false;
		completion.terminal_sha256[index] = (uint8_t)((high << 4) | low);
	}
	completion.session = sg_human_trace_session;
	completion.segment = sg_human_trace_segment;
	completion.continuation = sg_human_trace_segment_continuation ? 1U : 0U;
	completion.bsp_checksum = sg_human_trace_identity.bsp_checksum;
	completion.entity_crc32 = sg_human_trace_identity.entity_crc32;
	completion.host_physics_id = sg_human_trace_identity.host_physics_id;
	completion.gravity_bits = sg_human_trace_physics.gravity_bits;
	completion.airaccelerate_bits = sg_human_trace_physics.airaccelerate_bits;
	completion.maxvelocity_bits = sg_human_trace_physics.maxvelocity_bits;
	completion.pmove_substep_ms = sg_human_trace_physics.pmove_substep_ms;
	completion.server_frame_ms = sg_human_trace_physics.server_frame_ms;
	completion.physics_flags = sg_human_trace_physics.flags;
	completion.module_revision = LMCTF_REVISION;
	completion.end_order = end_order;
	completion.end_frame = end_frame;
	completion.end_level_time_bits = end_level_time_bits;
	sg_human_trace_completion = completion;
	return true;
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

static void HumanTraceEventHookSnapshot(sg_human_trace_v3_event_t *event,
	const human_trace_hook_snapshot_t *snapshot, int hook_key)
{
	event->hook_entity = (int32_t)hook_key;
	event->hook_target = (int32_t)snapshot->hook_target;
	memcpy(event->origin_bits, snapshot->origin_bits,
		sizeof(event->origin_bits));
	memcpy(event->hook_origin_bits, snapshot->hook_origin_bits,
		sizeof(event->hook_origin_bits));
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
	return HumanTraceJsonPathFor(sg_human_trace_directory,
		&sg_human_trace_identity, segment, path);
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

static qboolean HumanTraceHexToBytes(const char hex[65],
	uint8_t bytes[SG_HUMAN_TRACE_SHA256_BYTES])
{
	uint32_t index;

	if (!hex || !bytes)
		return false;
	for (index = 0U; index < SG_HUMAN_TRACE_SHA256_BYTES; index++)
	{
		int high = HumanTraceHexNibble(hex[index * 2U]);
		int low = HumanTraceHexNibble(hex[index * 2U + 1U]);

		if (high < 0 || low < 0)
			return false;
		bytes[index] = (uint8_t)((high << 4) | low);
	}
	return hex[64] == '\0';
}

static qboolean HumanTraceBytesNonzero(const uint8_t *bytes, size_t length)
{
	uint8_t nonzero = 0U;
	size_t index;

	if (!bytes)
		return false;
	for (index = 0U; index < length; index++)
		nonzero |= bytes[index];
	return nonzero != 0U;
}

static size_t HumanTraceSpoolPayloadBytes(uint16_t kind)
{
	switch ((human_trace_spool_frame_kind_t)kind)
	{
	case HUMAN_TRACE_SPOOL_FRAME_HEADER:
		return sizeof(human_trace_spool_header_t);
	case HUMAN_TRACE_SPOOL_FRAME_EVENT:
		return sizeof(human_trace_spool_event_t);
	case HUMAN_TRACE_SPOOL_FRAME_TERMINAL:
		return sizeof(sg_human_trace_completion_t);
	default:
		return 0U;
	}
}

static qboolean HumanTraceSpoolFrameDigest(
	const human_trace_spool_frame_t *frame,
	uint8_t digest_out[SG_HUMAN_TRACE_SHA256_BYTES])
{
	unsigned char input[SG_HUMAN_TRACE_SHA256_BYTES + sizeof(frame->kind) +
		sizeof(frame->payload_bytes) + SG_HUMAN_TRACE_SPOOL_PAYLOAD_BYTES];
	char hex[65];
	size_t length;

	if (!frame || !digest_out || frame->payload_bytes >
		SG_HUMAN_TRACE_SPOOL_PAYLOAD_BYTES)
		return false;
	memcpy(input, frame->previous_sha256, SG_HUMAN_TRACE_SHA256_BYTES);
	memcpy(input + SG_HUMAN_TRACE_SHA256_BYTES, &frame->kind,
		sizeof(frame->kind));
	memcpy(input + SG_HUMAN_TRACE_SHA256_BYTES + sizeof(frame->kind),
		&frame->payload_bytes, sizeof(frame->payload_bytes));
	length = SG_HUMAN_TRACE_SHA256_BYTES + sizeof(frame->kind) +
		sizeof(frame->payload_bytes) + frame->payload_bytes;
	memcpy(input + SG_HUMAN_TRACE_SHA256_BYTES + sizeof(frame->kind) +
		sizeof(frame->payload_bytes), frame->payload, frame->payload_bytes);
	return HumanTraceSHA256(input, length, hex) &&
		HumanTraceHexToBytes(hex, digest_out);
}

static qboolean HumanTraceSpoolFrameValid(
	const human_trace_spool_frame_t *frame,
	const uint8_t previous[SG_HUMAN_TRACE_SHA256_BYTES])
{
	uint8_t expected[SG_HUMAN_TRACE_SHA256_BYTES];
	size_t expected_payload_bytes;

	if (!frame || !previous || frame->magic != SG_HUMAN_TRACE_SPOOL_MAGIC ||
		frame->version != SG_HUMAN_TRACE_SPOOL_VERSION ||
		frame->payload_bytes > SG_HUMAN_TRACE_SPOOL_PAYLOAD_BYTES ||
		memcmp(frame->previous_sha256, previous,
			SG_HUMAN_TRACE_SHA256_BYTES) != 0)
		return false;
	expected_payload_bytes = HumanTraceSpoolPayloadBytes(frame->kind);
	if (expected_payload_bytes == 0U ||
		frame->payload_bytes != expected_payload_bytes ||
		!HumanTraceSpoolFrameDigest(frame, expected))
		return false;
	return memcmp(frame->sha256, expected, sizeof(expected)) == 0;
}

static qboolean HumanTraceSpoolWriteFrame(FILE *file,
	uint8_t previous[SG_HUMAN_TRACE_SHA256_BYTES], uint16_t kind,
	const void *payload, size_t payload_bytes)
{
	human_trace_spool_frame_t frame;
	uint8_t digest[SG_HUMAN_TRACE_SHA256_BYTES];

	if (!file || !previous || !payload || payload_bytes == 0U ||
		payload_bytes != HumanTraceSpoolPayloadBytes(kind))
		return false;
	memset(&frame, 0, sizeof(frame));
	frame.magic = SG_HUMAN_TRACE_SPOOL_MAGIC;
	frame.version = SG_HUMAN_TRACE_SPOOL_VERSION;
	frame.kind = kind;
	frame.payload_bytes = (uint32_t)payload_bytes;
	memcpy(frame.previous_sha256, previous, sizeof(frame.previous_sha256));
	memcpy(frame.payload, payload, payload_bytes);
	if (!HumanTraceSpoolFrameDigest(&frame, digest))
		return false;
	memcpy(frame.sha256, digest, sizeof(frame.sha256));
	if (fwrite(&frame, 1U, sizeof(frame), file) != sizeof(frame))
		return false;
	memcpy(previous, digest, SG_HUMAN_TRACE_SHA256_BYTES);
	return true;
}

static qboolean HumanTraceFileDurable(FILE *file)
{
	if (!file || fflush(file) != 0)
		return false;
#ifdef _WIN32
	return _commit(_fileno(file)) == 0;
#else
	return fsync(fileno(file)) == 0;
#endif
}

static qboolean HumanTraceSegmentPathAppend(const char *path)
{
	char (*grown)[SG_HUMAN_TRACE_SPOOL_PATH_BYTES];
	size_t capacity;

	if (!path)
		return false;
	if (sg_human_trace_segment_path_count ==
		sg_human_trace_segment_path_capacity)
	{
		capacity = sg_human_trace_segment_path_capacity ?
			sg_human_trace_segment_path_capacity * 2U : 8U;
		if (capacity < sg_human_trace_segment_path_capacity ||
			capacity > SIZE_MAX / sizeof(*grown))
			return false;
		grown = realloc(sg_human_trace_segment_paths,
			capacity * sizeof(*grown));
		if (!grown)
			return false;
		sg_human_trace_segment_paths = grown;
		sg_human_trace_segment_path_capacity = capacity;
	}
	if (snprintf(sg_human_trace_segment_paths[
		sg_human_trace_segment_path_count], SG_HUMAN_TRACE_SPOOL_PATH_BYTES,
		"%s", path) >= (int)SG_HUMAN_TRACE_SPOOL_PATH_BYTES)
		return false;
	sg_human_trace_segment_path_count++;
	return true;
}

static qboolean HumanTraceSegmentPathsDurable(void)
{
	size_t index;

	for (index = 0U; index < sg_human_trace_segment_path_count; index++)
	{
#ifdef _WIN32
		int descriptor = _open(sg_human_trace_segment_paths[index],
			_O_WRONLY | _O_BINARY);

		if (descriptor < 0 || _commit(descriptor) != 0 ||
			_close(descriptor) != 0)
			return false;
#else
		int descriptor = open(sg_human_trace_segment_paths[index], O_WRONLY);

		if (descriptor < 0 || fsync(descriptor) != 0 || close(descriptor) != 0)
			return false;
#endif
	}
	return true;
}

static qboolean HumanTraceDirectoryDurable(const char *directory)
{
#ifdef _WIN32
	(void)directory;
	return true;
#else
	int descriptor;
	int status;

	if (!directory)
		return false;
	descriptor = open(directory, O_RDONLY);
	if (descriptor < 0)
		return false;
	status = fsync(descriptor) == 0 && close(descriptor) == 0;
	return status != 0;
#endif
}

static qboolean HumanTraceSpoolPath(char path[SG_HUMAN_TRACE_SPOOL_PATH_BYTES],
	uint32_t root_segment)
{
	int written = snprintf(path, SG_HUMAN_TRACE_SPOOL_PATH_BYTES,
		"%s/humantrace-%s-%08" PRIx32 "-%08" PRIx32 "-%06" PRIu32
		".spool", sg_human_trace_directory, sg_human_trace_identity.mapname,
		sg_human_trace_identity.bsp_checksum,
		sg_human_trace_identity.entity_crc32, root_segment);

	return written >= 0 && (size_t)written <
		SG_HUMAN_TRACE_SPOOL_PATH_BYTES;
}

static void HumanTraceSpoolDisable(void)
{
	if (sg_human_trace_spool_file)
		fclose(sg_human_trace_spool_file);
	sg_human_trace_spool_file = NULL;
	memset(sg_human_trace_spool_previous_sha256, 0,
		sizeof(sg_human_trace_spool_previous_sha256));
}

static qboolean HumanTraceSpoolOpen(uint32_t session, uint32_t root_segment)
{
	human_trace_create_result_t created;
	human_trace_spool_header_t header;
	FILE *file = NULL;

	if (sg_human_trace_spool_file || sg_human_trace_spool_path[0] ||
		!HumanTraceSpoolPath(sg_human_trace_spool_path, root_segment))
		return false;
	created = HumanTraceCreateExclusive(sg_human_trace_spool_path, &file);
	if (created != HUMAN_TRACE_CREATE_OK)
		return false;
	memset(&header, 0, sizeof(header));
	header.identity = sg_human_trace_identity;
	header.session = session;
	header.root_segment = root_segment;
	if (!HumanTraceHexToBytes(sg_human_trace_previous_sha256,
		header.root_header_sha256) || !HumanTraceSpoolWriteFrame(file,
		sg_human_trace_spool_previous_sha256,
		HUMAN_TRACE_SPOOL_FRAME_HEADER, &header, sizeof(header)))
	{
		fclose(file);
		sg_human_trace_spool_path[0] = '\0';
		return false;
	}
	sg_human_trace_spool_file = file;
	return true;
}

static qboolean HumanTraceSpoolAppendEvent(
	const sg_human_trace_v3_event_t *event)
{
	human_trace_spool_event_t payload;

	if (!event || !HumanTraceEventValid(event) || !sg_human_trace_spool_file)
		return false;
	memset(&payload, 0, sizeof(payload));
	payload.event = *event;
	if (!HumanTraceHexToBytes(sg_human_trace_previous_sha256,
		payload.record_sha256) || !HumanTraceSpoolWriteFrame(
		sg_human_trace_spool_file, sg_human_trace_spool_previous_sha256,
		HUMAN_TRACE_SPOOL_FRAME_EVENT, &payload, sizeof(payload)))
	{
		HumanTraceSpoolDisable();
		return false;
	}
	return true;
}

static qboolean HumanTraceSpoolComplete(
	const sg_human_trace_completion_t *completion)
{
	if (!completion || !sg_human_trace_spool_file ||
		!HumanTraceSpoolWriteFrame(sg_human_trace_spool_file,
			sg_human_trace_spool_previous_sha256,
			HUMAN_TRACE_SPOOL_FRAME_TERMINAL, completion,
			sizeof(*completion)) ||
		!HumanTraceFileDurable(sg_human_trace_spool_file))
	{
		HumanTraceSpoolDisable();
		return false;
	}
	if (fclose(sg_human_trace_spool_file) != 0)
	{
		sg_human_trace_spool_file = NULL;
		return false;
	}
	sg_human_trace_spool_file = NULL;
	return true;
}

static int HumanTraceSpoolReadFrame(FILE *file,
	human_trace_spool_frame_t *frame)
{
	size_t read;

	if (!file || !frame)
		return -1;
	read = fread(frame, 1U, sizeof(*frame), file);
	if (read == 0U && feof(file))
		return 0;
	return read == sizeof(*frame) ? 1 : -1;
}

static qboolean HumanTraceSpoolCompletionValid(
	const sg_human_trace_completion_t *completion)
{
	size_t index;

	if (!completion || completion->session == UINT64_MAX ||
		completion->segment == UINT32_MAX || completion->continuation > 1U ||
		completion->end_order == 0U || !completion->mapname[0] ||
		!completion->module_version[0] || !completion->host_physics_id ||
		!completion->pmove_substep_ms || !completion->server_frame_ms)
		return false;
	for (index = 0U; index < sizeof(completion->mapname); index++)
		if (completion->mapname[index] == '\0')
			break;
	if (index == sizeof(completion->mapname))
		return false;
	for (index = 0U; index < sizeof(completion->module_version); index++)
		if (completion->module_version[index] == '\0')
			break;
	if (index == sizeof(completion->module_version))
		return false;
	return HumanTraceSafeName(completion->mapname) &&
		HumanTraceBytesNonzero(completion->terminal_sha256,
			SG_HUMAN_TRACE_SHA256_BYTES);
}

static const char *HumanTraceJsonValue(const char *line, const char *prefix)
{
	const char *found;
	size_t length;

	if (!line || !prefix)
		return NULL;
	length = strlen(prefix);
	if (length == 0U || !(found = strstr(line, prefix)) ||
		strstr(found + length, prefix))
		return NULL;
	return found + length;
}

static qboolean HumanTraceJsonUnsignedAt(const char *value,
	uint64_t *value_out)
{
	char *end;
	unsigned long long parsed;

	if (!value || !value_out || *value < '0' || *value > '9')
		return false;
	errno = 0;
	parsed = strtoull(value, &end, 10);
	if (errno != 0 || end == value || (*end != ',' && *end != '}' &&
		*end != ']'))
		return false;
	*value_out = (uint64_t)parsed;
	return true;
}

static qboolean HumanTraceJsonSignedAt(const char *value, int64_t *value_out)
{
	char *end;
	long long parsed;

	if (!value || !value_out ||
		!((*value >= '0' && *value <= '9') || *value == '-'))
		return false;
	errno = 0;
	parsed = strtoll(value, &end, 10);
	if (errno != 0 || end == value || (*end != ',' && *end != '}' &&
		*end != ']'))
		return false;
	*value_out = (int64_t)parsed;
	return true;
}

static qboolean HumanTraceJsonUnsigned(const char *line, const char *prefix,
	uint64_t *value_out)
{
	return HumanTraceJsonUnsignedAt(HumanTraceJsonValue(line, prefix),
		value_out);
}

static qboolean HumanTraceJsonSigned(const char *line, const char *prefix,
	int64_t *value_out)
{
	return HumanTraceJsonSignedAt(HumanTraceJsonValue(line, prefix), value_out);
}

static qboolean HumanTraceJsonString(const char *line, const char *prefix,
	char *value_out, size_t value_out_bytes)
{
	const char *value = HumanTraceJsonValue(line, prefix);
	const char *end;
	size_t length;

	if (!value || !value_out || value_out_bytes == 0U ||
		!(end = strchr(value, '"')) || strchr(value, '\\') || end == value)
		return false;
	length = (size_t)(end - value);
	if (length >= value_out_bytes)
		return false;
	memcpy(value_out, value, length);
	value_out[length] = '\0';
	return true;
}

static qboolean HumanTraceJsonSignedTriple(const char *value,
	int64_t values_out[3])
{
	uint32_t index;
	char *end;
	long long parsed;

	if (!value || !values_out)
		return false;
	for (index = 0U; index < 3U; index++)
	{
		if (!((*value >= '0' && *value <= '9') || *value == '-'))
			return false;
		errno = 0;
		parsed = strtoll(value, &end, 10);
		if (errno != 0 || end == value)
			return false;
		values_out[index] = (int64_t)parsed;
		if (index + 1U < 3U)
		{
			if (*end != ',')
				return false;
			value = end + 1;
		}
		else if (*end != ']')
			return false;
	}
	return true;
}

static qboolean HumanTraceJsonUnsignedTriple(const char *value,
	uint64_t values_out[3])
{
	uint32_t index;
	char *end;
	unsigned long long parsed;

	if (!value || !values_out)
		return false;
	for (index = 0U; index < 3U; index++)
	{
		if (*value < '0' || *value > '9')
			return false;
		errno = 0;
		parsed = strtoull(value, &end, 10);
		if (errno != 0 || end == value)
			return false;
		values_out[index] = (uint64_t)parsed;
		if (index + 1U < 3U)
		{
			if (*end != ',')
				return false;
			value = end + 1;
		}
		else if (*end != ']')
			return false;
	}
	return true;
}

static qboolean HumanTraceJsonRecordDigest(const char *line,
	uint8_t previous_out[SG_HUMAN_TRACE_SHA256_BYTES],
	uint8_t digest_out[SG_HUMAN_TRACE_SHA256_BYTES])
{
	const char marker[] = ",\"prev_sha256\":\"";
	const char between[] = "\",\"sha256\":\"";
	const char *at;
	const char *previous;
	const char *digest;
	char previous_hex[65];
	char digest_hex[65];
	char expected[65];
	unsigned char input[64U + SG_HUMAN_TRACE_LINE_BYTES];
	size_t payload_bytes;
	size_t line_bytes;

	if (!line || !previous_out || !digest_out ||
		!(line_bytes = strlen(line)) || line[line_bytes - 1U] != '\n' ||
		!(at = strstr(line, marker)) || strstr(at + sizeof(marker) - 1U,
			marker))
		return false;
	previous = at + sizeof(marker) - 1U;
	if (strlen(previous) != 64U + sizeof(between) - 1U + 64U + 3U ||
		previous[64] != '"' || strncmp(previous + 64U, between,
			sizeof(between) - 1U) != 0)
		return false;
	digest = previous + 64U + sizeof(between) - 1U;
	if (digest[64] != '"' || digest[65] != '}' || digest[66] != '\n' ||
		digest[67] != '\0')
		return false;
	memcpy(previous_hex, previous, 64U);
	previous_hex[64] = '\0';
	memcpy(digest_hex, digest, 64U);
	digest_hex[64] = '\0';
	if (!HumanTraceHexToBytes(previous_hex, previous_out) ||
		!HumanTraceHexToBytes(digest_hex, digest_out))
		return false;
	payload_bytes = (size_t)(at - line);
	if (payload_bytes + 1U > SG_HUMAN_TRACE_LINE_BYTES)
		return false;
	memcpy(input, previous_hex, 64U);
	memcpy(input + 64U, line, payload_bytes);
	input[64U + payload_bytes] = '}';
	if (!HumanTraceSHA256(input, 64U + payload_bytes + 1U, expected) ||
		strcmp(expected, digest_hex) != 0)
		return false;
	return true;
}

static qboolean HumanTraceJsonU32(const char *line, const char *prefix,
	uint32_t *value_out)
{
	uint64_t value;

	if (!value_out || !HumanTraceJsonUnsigned(line, prefix, &value) ||
		value > UINT32_MAX)
		return false;
	*value_out = (uint32_t)value;
	return true;
}

static qboolean HumanTraceJsonHeader(const char *line,
	const uint8_t previous_sha256[SG_HUMAN_TRACE_SHA256_BYTES],
	const uint8_t sha256[SG_HUMAN_TRACE_SHA256_BYTES],
	human_trace_json_header_t *header_out)
{
	char format[32];
	char kind[16];
	uint64_t value;

	if (!line || !previous_sha256 || !sha256 || !header_out)
		return false;
	memset(header_out, 0, sizeof(*header_out));
	if (!HumanTraceJsonString(line, "\"format\":\"", format,
		sizeof(format)) || strcmp(format, SG_HUMAN_TRACE_FORMAT) != 0 ||
		!HumanTraceJsonString(line, "\"kind\":\"", kind, sizeof(kind)) ||
		strcmp(kind, "header") != 0 || !HumanTraceJsonU32(line,
			"\"session\":", &header_out->session) ||
		!HumanTraceJsonU32(line, "\"segment\":", &header_out->segment) ||
		!HumanTraceJsonU32(line, "\"continuation\":",
			&header_out->continuation) ||
		!HumanTraceJsonUnsigned(line, "\"start_order\":",
			&header_out->start_order) ||
		!HumanTraceJsonUnsigned(line, "\"start_command\":",
			&header_out->start_command) ||
		!HumanTraceJsonUnsigned(line, "\"start_hook_event\":",
			&header_out->start_hook_event) ||
		!HumanTraceJsonString(line, "\"map\":\"",
			header_out->identity.mapname,
			sizeof(header_out->identity.mapname)) ||
		!HumanTraceJsonU32(line, "\"bsp_checksum\":",
			&header_out->identity.bsp_checksum) ||
		!HumanTraceJsonU32(line, "\"entity_crc32\":",
			&header_out->identity.entity_crc32) ||
		!HumanTraceJsonUnsigned(line, "\"physics_id\":", &value) ||
		value != 0U || !HumanTraceJsonU32(line, "\"host_physics_id\":",
			&header_out->identity.host_physics_id) ||
		!HumanTraceJsonU32(line, "\"gravity_bits\":",
			&header_out->gravity_bits) || !HumanTraceJsonU32(line,
			"\"airaccelerate_bits\":", &header_out->airaccelerate_bits) ||
		!HumanTraceJsonU32(line, "\"maxvelocity_bits\":",
			&header_out->maxvelocity_bits) || !HumanTraceJsonUnsigned(line,
			"\"pmove_substep_ms\":", &value) || value == 0U ||
		value > UINT16_MAX)
		return false;
	header_out->pmove_substep_ms = (uint16_t)value;
	if (!HumanTraceJsonUnsigned(line, "\"server_frame_ms\":", &value) ||
		value == 0U || value > UINT16_MAX)
		return false;
	header_out->server_frame_ms = (uint16_t)value;
	if (!HumanTraceJsonU32(line, "\"physics_flags\":",
		&header_out->physics_flags) || !HumanTraceJsonU32(line,
		"\"module_revision\":", &header_out->module_revision) ||
		!HumanTraceJsonString(line, "\"module_version\":\"",
			header_out->module_version, sizeof(header_out->module_version)) ||
		header_out->session == UINT32_MAX ||
		header_out->segment == UINT32_MAX ||
		header_out->continuation > 1U ||
		header_out->start_order == 0U ||
		header_out->start_command == 0U ||
		header_out->start_hook_event == 0U ||
		!header_out->identity.mapname[0] ||
		!header_out->identity.host_physics_id ||
		!header_out->module_version[0])
		return false;
	memcpy(header_out->previous_sha256, previous_sha256,
		sizeof(header_out->previous_sha256));
	memcpy(header_out->sha256, sha256, sizeof(header_out->sha256));
	return true;
}

static qboolean HumanTraceJsonEvent(const char *line,
	sg_human_trace_v3_event_t *event_out)
{
	char format[32];
	char kind[20];
	int64_t signed_value;
	int64_t signed_values[3];
	uint64_t unsigned_value;
	uint64_t unsigned_values[3];
	const char *after;
	uint32_t index;

	if (!line || !event_out || !HumanTraceJsonString(line, "\"format\":\"",
		format, sizeof(format)) || strcmp(format, SG_HUMAN_TRACE_FORMAT) != 0 ||
		!HumanTraceJsonString(line, "\"kind\":\"", kind, sizeof(kind)))
		return false;
	memset(event_out, 0, sizeof(*event_out));
	if (strcmp(kind, "step") == 0)
	{
		event_out->kind = SG_HUMAN_TRACE_V3_EVENT_STEP;
		if (!HumanTraceJsonUnsigned(line, "\"order\":", &event_out->order) ||
			!HumanTraceJsonUnsigned(line, "\"command\":",
				&event_out->command) || !HumanTraceJsonU32(line,
				"\"client\":", &event_out->client_id) ||
			!HumanTraceJsonUnsigned(line, "\"spawn_generation\":",
				&event_out->spawn_generation) || !HumanTraceJsonU32(line,
				"\"frame\":", &event_out->frame) || !HumanTraceJsonU32(line,
				"\"level_time_bits\":", &event_out->level_time_bits) ||
			!HumanTraceJsonUnsigned(line, "\"cmd\":{\"msec\":",
				&unsigned_value) || unsigned_value > UINT16_MAX ||
			!(after = HumanTraceJsonValue(line, "\"after\":")) ||
			!HumanTraceJsonSignedTriple(HumanTraceJsonValue(after,
				"\"origin\":["), signed_values) ||
			!HumanTraceJsonSigned(line, "\"ground\":", &signed_value))
			return false;
		for (index = 0U; index < 3U; index++)
		{
			if (signed_values[index] < INT16_MIN ||
				signed_values[index] > INT16_MAX)
				return false;
			event_out->after_origin[index] = (int16_t)signed_values[index];
		}
		event_out->command_msec = (uint16_t)unsigned_value;
		event_out->grounded = signed_value >= 0 ? 1U : 0U;
		return HumanTraceEventValid(event_out);
	}
	if (strcmp(kind, "hook-fire") == 0)
		event_out->kind = SG_HUMAN_TRACE_V3_EVENT_HOOK_FIRE;
	else if (strcmp(kind, "hook-attach") == 0)
		event_out->kind = SG_HUMAN_TRACE_V3_EVENT_HOOK_ATTACH;
	else if (strcmp(kind, "hook-release") == 0)
		event_out->kind = SG_HUMAN_TRACE_V3_EVENT_HOOK_RELEASE;
	else if (strcmp(kind, "hook-reset") == 0)
		event_out->kind = SG_HUMAN_TRACE_V3_EVENT_HOOK_RESET;
	else
		return false;
	if (!HumanTraceJsonUnsigned(line, "\"order\":", &event_out->order) ||
		!HumanTraceJsonUnsigned(line, "\"hook_event\":",
			&event_out->hook_event) || !HumanTraceJsonUnsigned(line,
			"\"after_command\":", &event_out->after_command) ||
		!HumanTraceJsonU32(line, "\"client\":", &event_out->client_id) ||
		!HumanTraceJsonUnsigned(line, "\"spawn_generation\":",
			&event_out->spawn_generation) || !HumanTraceJsonU32(line,
			"\"frame\":", &event_out->frame) || !HumanTraceJsonU32(line,
			"\"level_time_bits\":", &event_out->level_time_bits) ||
		!HumanTraceJsonSigned(line, "\"hook\":", &signed_value) ||
		signed_value <= 0 || signed_value > INT32_MAX)
		return false;
	event_out->hook_entity = (int32_t)signed_value;
	if (!HumanTraceJsonSigned(line, "\"hook_target\":", &signed_value) ||
		signed_value < INT32_MIN || signed_value > INT32_MAX ||
		!HumanTraceJsonUnsignedTriple(HumanTraceJsonValue(line,
			"\"origin_bits\":["), unsigned_values))
		return false;
	event_out->hook_target = (int32_t)signed_value;
	for (index = 0U; index < 3U; index++)
	{
		if (unsigned_values[index] > UINT32_MAX)
			return false;
		event_out->origin_bits[index] = (uint32_t)unsigned_values[index];
	}
	if (!HumanTraceJsonUnsignedTriple(HumanTraceJsonValue(line,
		"\"hook_origin_bits\":["), unsigned_values))
		return false;
	for (index = 0U; index < 3U; index++)
	{
		if (unsigned_values[index] > UINT32_MAX)
			return false;
		event_out->hook_origin_bits[index] = (uint32_t)unsigned_values[index];
	}
	return HumanTraceEventValid(event_out);
}

static qboolean HumanTraceJsonEnd(const char *line, uint64_t *order_out,
	uint32_t *frame_out, uint32_t *level_time_bits_out)
{
	char format[32];
	char kind[16];

	return line && order_out && frame_out && level_time_bits_out &&
		HumanTraceJsonString(line, "\"format\":\"", format,
			sizeof(format)) && strcmp(format, SG_HUMAN_TRACE_FORMAT) == 0 &&
		HumanTraceJsonString(line, "\"kind\":\"", kind, sizeof(kind)) &&
		strcmp(kind, "end") == 0 && HumanTraceJsonUnsigned(line,
			"\"order\":", order_out) && HumanTraceJsonU32(line,
			"\"frame\":", frame_out) && HumanTraceJsonU32(line,
			"\"level_time_bits\":", level_time_bits_out);
}

static qboolean HumanTraceJsonSegmentRead(const char *path,
	human_trace_json_segment_t *segment_out,
	human_trace_json_event_visitor_fn visitor, void *visitor_context)
{
	char line[SG_HUMAN_TRACE_LINE_BYTES];
	uint8_t record_previous[SG_HUMAN_TRACE_SHA256_BYTES];
	uint8_t record_sha256[SG_HUMAN_TRACE_SHA256_BYTES];
	uint8_t current_sha256[SG_HUMAN_TRACE_SHA256_BYTES];
	human_trace_json_segment_t segment;
	uint8_t header_seen = 0U;
	qboolean valid = true;
	FILE *file;

	if (segment_out)
		memset(segment_out, 0, sizeof(*segment_out));
	if (!path || !segment_out)
		return false;
	file = fopen(path, "rb");
	if (!file)
		return false;
	memset(&segment, 0, sizeof(segment));
	while (fgets(line, sizeof(line), file))
	{
		char kind[20];
		sg_human_trace_v3_event_t event;
		uint64_t end_order;
		uint32_t end_frame;
		uint32_t end_level_time_bits;

		if (!strchr(line, '\n') || line[0] == '\n' ||
			!HumanTraceJsonRecordDigest(line, record_previous, record_sha256))
		{
			valid = false;
			break;
		}
		if (!header_seen)
		{
			if (!HumanTraceJsonHeader(line, record_previous, record_sha256,
				&segment.header))
			{
				valid = false;
				break;
			}
			segment.last_order = segment.header.start_order - 1U;
			segment.last_command = segment.header.start_command - 1U;
			segment.last_hook_event = segment.header.start_hook_event - 1U;
			segment.last_hook_command = segment.header.start_command - 1U;
			memcpy(current_sha256, record_sha256, sizeof(current_sha256));
			memcpy(segment.last_sha256, record_sha256,
				sizeof(segment.last_sha256));
			header_seen = 1U;
			continue;
		}
		if (segment.ended || memcmp(record_previous, current_sha256,
			sizeof(current_sha256)) != 0 || !HumanTraceJsonString(line,
			"\"kind\":\"", kind, sizeof(kind)))
		{
			valid = false;
			break;
		}
		if (strcmp(kind, "end") == 0)
		{
			if (!HumanTraceJsonEnd(line, &end_order, &end_frame,
				&end_level_time_bits) || end_order < segment.header.start_order ||
				end_order <= segment.last_order || (segment.have_frame &&
				end_frame < segment.last_frame))
			{
				valid = false;
				break;
			}
			segment.last_order = end_order;
			segment.last_frame = end_frame;
			segment.have_frame = 1U;
			segment.ended = 1U;
			segment.end_level_time_bits = end_level_time_bits;
			memcpy(segment.terminal_sha256, record_sha256,
				sizeof(segment.terminal_sha256));
		}
		else
		{
			if (!HumanTraceJsonEvent(line, &event) ||
				event.order < segment.header.start_order ||
				event.order <= segment.last_order || (segment.have_frame &&
				event.frame < segment.last_frame))
			{
				valid = false;
				break;
			}
			if (event.kind == SG_HUMAN_TRACE_V3_EVENT_STEP)
			{
				if (event.command <= segment.last_command)
				{
					valid = false;
					break;
				}
				segment.last_command = event.command;
			}
			else
			{
				if (event.hook_event <= segment.last_hook_event ||
					event.after_command < segment.last_hook_command ||
					event.after_command > segment.last_command)
				{
					valid = false;
					break;
				}
				segment.last_hook_event = event.hook_event;
				segment.last_hook_command = event.after_command;
			}
			if (visitor && !visitor(visitor_context, &event, record_sha256))
			{
				valid = false;
				break;
			}
			segment.last_order = event.order;
			segment.last_frame = event.frame;
			segment.have_frame = 1U;
		}
		memcpy(current_sha256, record_sha256, sizeof(current_sha256));
		memcpy(segment.last_sha256, record_sha256,
			sizeof(segment.last_sha256));
	}
	if (ferror(file))
		valid = false;
	if (fclose(file) != 0)
		valid = false;
	if (header_seen)
		*segment_out = segment;
	if (!header_seen || !valid)
		return false;
	return true;
}

static qboolean HumanTraceJsonPathFor(const char *directory,
	const sg_level_identity_t *identity, uint32_t segment,
	char path[SG_HUMAN_TRACE_SPOOL_PATH_BYTES])
{
	int written;

	if (!directory || !identity || !identity->mapname[0] || !path)
		return false;
	written = snprintf(path, SG_HUMAN_TRACE_SPOOL_PATH_BYTES,
		"%s/humantrace-%s-%08" PRIx32 "-%08" PRIx32 "-%06" PRIu32
		".jsonl", directory, identity->mapname, identity->bsp_checksum,
		identity->entity_crc32, segment);
	return written >= 0 && (size_t)written < SG_HUMAN_TRACE_SPOOL_PATH_BYTES;
}

static int HumanTraceNameSegment(const char *name,
	const sg_level_identity_t *identity, const char *suffix,
	uint32_t *segment_out)
{
	char prefix[SG_HUMAN_TRACE_SPOOL_NAME_BYTES];
	char canonical[16];
	const char *first_digit;
	const char *digits;
	size_t digit_count;
	int written;
	uint32_t parsed = 0U;

	if (!name || !identity || !suffix || !segment_out ||
		!identity->mapname[0])
		return 0;
	written = snprintf(prefix, sizeof(prefix), "humantrace-%s-%08" PRIx32
		"-%08" PRIx32 "-", identity->mapname, identity->bsp_checksum,
		identity->entity_crc32);
	if (written < 0 || (size_t)written >= sizeof(prefix) ||
		strncmp(name, prefix, (size_t)written) != 0)
		return 0;
	first_digit = name + written;
	digits = first_digit;
	if (*digits < '0' || *digits > '9')
		return 0;
	for (;;)
	{
		uint32_t digit;

		if (*digits < '0' || *digits > '9')
			break;
		digit = (uint32_t)(*digits - '0');
		if (parsed > (UINT32_MAX - digit) / 10U)
			return 0;
		parsed = parsed * 10U + digit;
		digits++;
	}
	if (strcmp(digits, suffix) != 0 || parsed == UINT32_MAX)
		return 0;
	digit_count = (size_t)(digits - first_digit);
	written = snprintf(canonical, sizeof(canonical), "%06" PRIu32, parsed);
	if (written < 0 || (size_t)written >= sizeof(canonical) ||
		(size_t)written != digit_count || strncmp(first_digit, canonical,
		digit_count) != 0)
		return 0;
	*segment_out = parsed;
	return 1;
}

static int HumanTraceJsonNameSegment(const char *name,
	const sg_level_identity_t *identity, uint32_t *segment_out)
{
	return HumanTraceNameSegment(name, identity, ".jsonl", segment_out);
}

#ifdef SG_HUMAN_TRACE_TEST
int SG_HumanTraceTestFormatJsonPath(const char *directory,
	const sg_level_identity_t *identity, uint32_t segment,
	char path[SG_HUMAN_TRACE_SPOOL_PATH_BYTES])
{
	return HumanTraceJsonPathFor(directory, identity, segment, path) ? 1 : 0;
}

int SG_HumanTraceTestJsonNameSegment(const char *name,
	const sg_level_identity_t *identity, uint32_t *segment_out)
{
	return HumanTraceJsonNameSegment(name, identity, segment_out);
}
#endif

static qboolean HumanTraceJsonStableIdentityEqual(
	const human_trace_json_header_t *left,
	const human_trace_json_header_t *right)
{
	return left && right && left->identity.bsp_checksum ==
		right->identity.bsp_checksum && left->identity.entity_crc32 ==
		right->identity.entity_crc32 && left->identity.host_physics_id ==
		right->identity.host_physics_id && left->module_revision ==
		right->module_revision && strncmp(left->identity.mapname,
		right->identity.mapname, sizeof(left->identity.mapname)) == 0 &&
		strncmp(left->module_version, right->module_version,
			sizeof(left->module_version)) == 0;
}

static qboolean HumanTraceJsonFinalMatchesCompletion(
	const human_trace_json_segment_t *segment,
	const sg_human_trace_completion_t *completion)
{
	const human_trace_json_header_t *header;

	if (!segment || !completion || !segment->ended)
		return false;
	header = &segment->header;
	return memcmp(segment->terminal_sha256, completion->terminal_sha256,
		SG_HUMAN_TRACE_SHA256_BYTES) == 0 && header->session ==
		completion->session && header->segment == completion->segment &&
		header->continuation == completion->continuation &&
		strncmp(header->identity.mapname, completion->mapname,
			sizeof(header->identity.mapname)) == 0 &&
		header->identity.bsp_checksum == completion->bsp_checksum &&
		header->identity.entity_crc32 == completion->entity_crc32 &&
		header->identity.host_physics_id == completion->host_physics_id &&
		header->gravity_bits == completion->gravity_bits &&
		header->airaccelerate_bits == completion->airaccelerate_bits &&
		header->maxvelocity_bits == completion->maxvelocity_bits &&
		header->pmove_substep_ms == completion->pmove_substep_ms &&
		header->server_frame_ms == completion->server_frame_ms &&
		header->physics_flags == completion->physics_flags &&
		header->module_revision == completion->module_revision &&
		strncmp(header->module_version, completion->module_version,
			sizeof(header->module_version)) == 0 && segment->last_order ==
		completion->end_order && segment->last_frame == completion->end_frame &&
		segment->end_level_time_bits == completion->end_level_time_bits;
}

static qboolean HumanTraceJsonRangeFollows(
	const human_trace_json_segment_t *previous,
	const human_trace_json_segment_t *next)
{
	if (!previous || !next || previous->last_order == UINT64_MAX ||
		previous->last_command == UINT64_MAX ||
		previous->last_hook_event == UINT64_MAX)
		return false;
	return next->header.segment > previous->header.segment &&
		next->header.start_order == previous->last_order + 1U &&
		next->header.start_command == previous->last_command + 1U &&
		next->header.start_hook_event == previous->last_hook_event + 1U;
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
	HumanTraceSpoolDisable();
	sg_human_trace_event_failed = true;
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
	if (!HumanTraceSHA256(digest_input, 64U + payload_length, digest))
		return false;
	written = snprintf(line, sizeof(line), "%.*s,\"prev_sha256\":\"%s\","
		"\"sha256\":\"%s\"}\n", (int)(payload_length - 1U), payload,
		sg_human_trace_previous_sha256, digest);
	if (written < 0 || (size_t)written >= sizeof(line))
		return false;
	line_length = (size_t)written;
	if (!HumanTraceWriteFitsFileLimit(*segment_bytes, line_length))
		return false;
	if (fwrite(line, 1U, line_length, file) != line_length)
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
		/* UINT32_MAX is the typed exhaustion sentinel and cannot be emitted:
		 * the authenticated trace and filename parser both reserve it. */
		if (candidate == UINT32_MAX)
			return false;
		if (!HumanTracePath(path, candidate))
			return false;
		created = HumanTraceCreateExclusive(path, &candidate_file);
		if (created == HUMAN_TRACE_CREATE_OK)
			break;
		if (created != HUMAN_TRACE_CREATE_COLLISION)
			return false;
		if (candidate == UINT32_MAX)
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
	        header.bytes, header.length) || !HumanTraceSegmentPathAppend(path))
	{
		fclose(candidate_file);
		return false;
	}
	sg_human_trace_file = candidate_file;
	sg_human_trace_session = session;
	sg_human_trace_segment = candidate;
	sg_human_trace_segment_bytes = segment_bytes;
	sg_human_trace_segment_continuation = continuation;
	if (!continuation && !HumanTraceSpoolOpen(session, candidate))
		sg_human_trace_event_failed = true;
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
	return sg_cv.humantrace->value != 0.0f &&
		HumanTraceHuman(entity, client_key, spawn_generation);
}

void SG_HumanTraceNewLevel(void)
{
	if (sg_human_trace_file)
		fclose(sg_human_trace_file);
	sg_human_trace_file = NULL;
	HumanTraceSpoolDisable();
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
	sg_human_trace_segment_continuation = false;
	sg_human_trace_open_failed = false;
	sg_human_trace_match_ended = false;
	sg_human_trace_spool_path[0] = '\0';
	sg_human_trace_event_failed = false;
	memset(&sg_human_trace_completion, 0, sizeof(sg_human_trace_completion));
	free(sg_human_trace_segment_paths);
	sg_human_trace_segment_paths = NULL;
	sg_human_trace_segment_path_count = 0U;
	sg_human_trace_segment_path_capacity = 0U;
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
		{
			if (level.framenum >= 0)
			{
				if (!HumanTraceCompletionSet(sg_human_trace_order + 1U,
					(uint32_t)level.framenum, HumanTraceLevelTimeBits()) ||
					!HumanTraceSegmentPathsDurable() ||
					(!sg_human_trace_event_failed &&
					 !HumanTraceSpoolComplete(&sg_human_trace_completion)) ||
					!HumanTraceDirectoryDurable(sg_human_trace_directory))
					sg_human_trace_event_failed = true;
			}
			sg_human_trace_order++;
		}
	}
	if (sg_human_trace_file)
	{
		if (fclose(sg_human_trace_file) != 0)
			gi.dprintf("humantrace: match-end close failed\n");
		sg_human_trace_file = NULL;
	}
	if (sg_human_trace_spool_file)
		HumanTraceSpoolDisable();
	sg_human_trace_match_ended = true;
}

static qboolean HumanTraceDirectoryCurrent(char directory[512])
{
	cvar_t *game_directory;
	const char *selected;
	int written;

	if (!directory)
		return false;
	SG_CvarsInit();
	if (!sg_cv.humantracedir || !gi.cvar)
		return false;
	game_directory = gi.cvar("gamedir", "", 0);
	selected = sg_cv.humantracedir->string;
	if (!selected[0])
		selected = game_directory && game_directory->string[0]
			? game_directory->string : ".";
	written = snprintf(directory, 512U, "%s", selected);
	return written >= 0 && written < 512;
}

static int HumanTraceSpoolNameSegment(const char *name,
	const sg_level_identity_t *identity, uint32_t *segment_out)
{
	return HumanTraceNameSegment(name, identity, ".spool", segment_out);
}

typedef enum human_trace_manifest_candidate_kind_e
{
	HUMAN_TRACE_MANIFEST_SPOOL,
	HUMAN_TRACE_MANIFEST_JSON
} human_trace_manifest_candidate_kind_t;

typedef struct human_trace_manifest_candidate_s
{
	char path[SG_HUMAN_TRACE_SPOOL_PATH_BYTES];
	uint32_t segment;
	human_trace_manifest_candidate_kind_t kind;
} human_trace_manifest_candidate_t;

typedef struct human_trace_manifest_event_s
{
	human_trace_spool_event_t recorded;
	size_t scope_index;
} human_trace_manifest_event_t;

typedef struct human_trace_manifest_root_s
{
	sg_human_trace_v3_spool_ref_t spool;
	human_trace_spool_header_t header;
	human_trace_manifest_event_t *events;
	size_t event_count;
	size_t event_capacity;
	sg_human_trace_v3_scope_t *scopes;
	uint8_t *scope_started;
	size_t scope_count;
} human_trace_manifest_root_t;

typedef struct human_trace_manifest_segment_s
{
	human_trace_json_segment_t summary;
	human_trace_spool_event_t *events;
	size_t event_count;
	size_t event_capacity;
	uint8_t valid;
} human_trace_manifest_segment_t;

typedef struct human_trace_manifest_s
{
	human_trace_manifest_candidate_t *candidates;
	size_t candidate_count;
	size_t candidate_capacity;
	human_trace_manifest_segment_t *segments;
	size_t segment_count;
	size_t segment_capacity;
} human_trace_manifest_t;

typedef struct human_trace_manifest_scope_occurrence_s
{
	uint32_t client_id;
	uint64_t spawn_generation;
	size_t event_index;
	size_t group_index;
} human_trace_manifest_scope_occurrence_t;

typedef struct human_trace_manifest_scope_order_s
{
	uint32_t client_id;
	uint64_t spawn_generation;
	size_t first_event;
	size_t group_index;
} human_trace_manifest_scope_order_t;

typedef struct human_trace_manifest_json_capture_s
{
	human_trace_spool_event_t *events;
	size_t count;
	size_t capacity;
	uint8_t allocation_failed;
} human_trace_manifest_json_capture_t;

static int HumanTraceManifestGrow(void **items, size_t item_bytes,
	size_t needed, size_t *capacity)
{
	size_t next;
	void *grown;

	if (!items || item_bytes == 0U || !capacity)
		return 0;
	if (needed <= *capacity)
		return 1;
	next = *capacity ? *capacity : 8U;
	while (next < needed)
	{
		if (next > SIZE_MAX / 2U)
		{
			next = needed;
			break;
		}
		next *= 2U;
	}
	if (next > SIZE_MAX / item_bytes)
		return 0;
	grown = realloc(*items, next * item_bytes);
	if (!grown)
		return 0;
	*items = grown;
	*capacity = next;
	return 1;
}

static void HumanTraceManifestFree(human_trace_manifest_t *manifest)
{
	size_t index;

	if (!manifest)
		return;
	for (index = 0U; index < manifest->segment_count; index++)
		free(manifest->segments[index].events);
	free(manifest->candidates);
	free(manifest->segments);
	memset(manifest, 0, sizeof(*manifest));
}

static int HumanTraceManifestCandidateAppend(human_trace_manifest_t *manifest,
	const char *directory, const char *name, uint32_t segment,
	human_trace_manifest_candidate_kind_t kind)
{
	human_trace_manifest_candidate_t *candidate;

	if (!manifest || !directory || !name || !HumanTraceManifestGrow(
		(void **)&manifest->candidates, sizeof(*manifest->candidates),
		manifest->candidate_count + 1U, &manifest->candidate_capacity))
		return 0;
	candidate = &manifest->candidates[manifest->candidate_count];
	memset(candidate, 0, sizeof(*candidate));
	if (snprintf(candidate->path, sizeof(candidate->path), "%s/%s", directory,
		name) >= (int)sizeof(candidate->path))
		return 0;
	candidate->segment = segment;
	candidate->kind = kind;
	manifest->candidate_count++;
	return 1;
}

static int HumanTraceManifestScan(const char *directory,
	const sg_level_identity_t *identity, human_trace_manifest_t *manifest)
{
	if (!directory || !identity || !manifest)
		return 0;
#ifdef _WIN32
	{
		char pattern[SG_HUMAN_TRACE_SPOOL_PATH_BYTES];
		WIN32_FIND_DATAA entry;
		HANDLE handle;

		if (snprintf(pattern, sizeof(pattern), "%s/*", directory) >=
			(int)sizeof(pattern))
			return 0;
		handle = FindFirstFileA(pattern, &entry);
		if (handle == INVALID_HANDLE_VALUE)
			return GetLastError() == ERROR_FILE_NOT_FOUND;
		do
		{
			uint32_t segment;
			human_trace_manifest_candidate_kind_t kind;

			if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
				continue;
			if (HumanTraceSpoolNameSegment(entry.cFileName, identity, &segment))
				kind = HUMAN_TRACE_MANIFEST_SPOOL;
			else if (HumanTraceJsonNameSegment(entry.cFileName, identity,
				&segment))
				kind = HUMAN_TRACE_MANIFEST_JSON;
			else
				continue;
			if (!HumanTraceManifestCandidateAppend(manifest, directory,
				entry.cFileName, segment, kind))
			{
				FindClose(handle);
				return 0;
			}
		} while (FindNextFileA(handle, &entry));
		if (GetLastError() != ERROR_NO_MORE_FILES)
		{
			FindClose(handle);
			return 0;
		}
		FindClose(handle);
	}
#else
	{
		DIR *opened = opendir(directory);
		struct dirent *entry;

		if (!opened)
			return 0;
		for (;;)
		{
			uint32_t segment;
			human_trace_manifest_candidate_kind_t kind;

			errno = 0;
			entry = readdir(opened);
			if (!entry)
				break;
			if (HumanTraceSpoolNameSegment(entry->d_name, identity, &segment))
				kind = HUMAN_TRACE_MANIFEST_SPOOL;
			else if (HumanTraceJsonNameSegment(entry->d_name, identity, &segment))
				kind = HUMAN_TRACE_MANIFEST_JSON;
			else
				continue;
			if (!HumanTraceManifestCandidateAppend(manifest, directory,
				entry->d_name, segment, kind))
			{
				closedir(opened);
				return 0;
			}
		}
		if (errno != 0)
		{
			closedir(opened);
			return 0;
		}
		closedir(opened);
	}
#endif
	return 1;
}

static int HumanTraceManifestCandidatesSort(human_trace_manifest_t *manifest)
{
	human_trace_manifest_candidate_t *temporary;
	human_trace_manifest_candidate_t *source;
	human_trace_manifest_candidate_t *target;
	size_t count[256];
	size_t offset[256];
	size_t pass, index;

	if (!manifest || manifest->candidate_count < 2U)
		return manifest != NULL;
	if (manifest->candidate_count > SIZE_MAX / sizeof(*temporary))
		return 0;
	temporary = malloc(manifest->candidate_count * sizeof(*temporary));
	if (!temporary)
		return 0;
	source = manifest->candidates;
	target = temporary;
	for (pass = 0U; pass < sizeof(uint32_t); pass++)
	{
		memset(count, 0, sizeof(count));
		for (index = 0U; index < manifest->candidate_count; index++)
			count[(source[index].segment >> (pass * 8U)) & 0xffU]++;
		offset[0] = 0U;
		for (index = 1U; index < 256U; index++)
			offset[index] = offset[index - 1U] + count[index - 1U];
		for (index = 0U; index < manifest->candidate_count; index++)
		{
			uint32_t bucket = (source[index].segment >> (pass * 8U)) & 0xffU;

			target[offset[bucket]++] = source[index];
		}
		{
			human_trace_manifest_candidate_t *swap = source;

			source = target;
			target = swap;
		}
	}
	if (source != manifest->candidates)
		memcpy(manifest->candidates, source,
			manifest->candidate_count * sizeof(*source));
	free(temporary);
	return 1;
}

static int HumanTraceManifestRootRead(
	const human_trace_manifest_candidate_t *candidate,
	const sg_level_identity_t *identity, human_trace_manifest_root_t *root)
{
	human_trace_spool_frame_t frame;
	sg_human_trace_completion_t terminal;
	uint8_t previous[SG_HUMAN_TRACE_SHA256_BYTES];
	uint64_t previous_order = 0U;
	uint8_t header_seen = 0U;
	uint8_t terminal_seen = 0U;
	int status;
	int valid = 1;
	FILE *file;

	if (!candidate || !identity || !root)
		return -1;
	memset(root, 0, sizeof(*root));
	file = fopen(candidate->path, "rb");
	if (!file)
		return 0;
	memset(&terminal, 0, sizeof(terminal));
	memset(previous, 0, sizeof(previous));
	while ((status = HumanTraceSpoolReadFrame(file, &frame)) > 0)
	{
		if (!HumanTraceSpoolFrameValid(&frame, previous))
		{
			valid = 0;
			break;
		}
		memcpy(previous, frame.sha256, sizeof(previous));
		if (frame.kind == HUMAN_TRACE_SPOOL_FRAME_HEADER)
		{
			if (header_seen || terminal_seen)
			{
				valid = 0;
				break;
			}
			memcpy(&root->header, frame.payload, sizeof(root->header));
			if (!root->header.identity.mapname[0] || !memchr(
				root->header.identity.mapname, '\0',
				sizeof(root->header.identity.mapname)) || !HumanTraceSafeName(
				root->header.identity.mapname) ||
				!root->header.identity.host_physics_id ||
				root->header.session == UINT32_MAX ||
				root->header.root_segment != candidate->segment ||
				root->header.session != root->header.root_segment ||
				!HumanTraceBytesNonzero(root->header.root_header_sha256,
					SG_HUMAN_TRACE_SHA256_BYTES) || strncmp(
				root->header.identity.mapname, identity->mapname,
					sizeof(identity->mapname)) != 0 ||
				root->header.identity.bsp_checksum != identity->bsp_checksum ||
				root->header.identity.entity_crc32 != identity->entity_crc32 ||
				root->header.identity.host_physics_id != identity->host_physics_id)
			{
				valid = 0;
				break;
			}
			header_seen = 1U;
		}
		else if (frame.kind == HUMAN_TRACE_SPOOL_FRAME_EVENT)
		{
			human_trace_manifest_event_t *stored;
			human_trace_spool_event_t event;

			memcpy(&event, frame.payload, sizeof(event));
			if (!header_seen || terminal_seen || !HumanTraceEventValid(
				&event.event) || event.event.order <= previous_order ||
				!HumanTraceBytesNonzero(event.record_sha256,
					SG_HUMAN_TRACE_SHA256_BYTES))
			{
				valid = 0;
				break;
			}
			if (!HumanTraceManifestGrow((void **)&root->events,
				sizeof(*root->events), root->event_count + 1U,
				&root->event_capacity))
			{
				fclose(file);
				free(root->events);
				memset(root, 0, sizeof(*root));
				return -1;
			}
			stored = &root->events[root->event_count++];
			memset(stored, 0, sizeof(*stored));
			stored->recorded = event;
			previous_order = event.event.order;
		}
		else if (frame.kind == HUMAN_TRACE_SPOOL_FRAME_TERMINAL)
		{
			if (!header_seen || terminal_seen)
			{
				valid = 0;
				break;
			}
			memcpy(&terminal, frame.payload, sizeof(terminal));
			if (!HumanTraceSpoolCompletionValid(&terminal) ||
				terminal.session != (uint64_t)root->header.session ||
				terminal.segment < root->header.root_segment || strncmp(
				terminal.mapname, root->header.identity.mapname,
					sizeof(terminal.mapname)) != 0 ||
				terminal.bsp_checksum != root->header.identity.bsp_checksum ||
				terminal.entity_crc32 != root->header.identity.entity_crc32 ||
				terminal.host_physics_id !=
					root->header.identity.host_physics_id ||
				previous_order >= terminal.end_order)
			{
				valid = 0;
				break;
			}
			terminal_seen = 1U;
		}
		else
		{
			valid = 0;
			break;
		}
	}
	if (fclose(file) != 0)
		valid = 0;
	if (status < 0 || !header_seen || !terminal_seen || !valid)
	{
		free(root->events);
		memset(root, 0, sizeof(*root));
		return 0;
	}
	root->spool.completion = terminal;
	root->spool.root_segment = candidate->segment;
	if (snprintf(root->spool.path, sizeof(root->spool.path), "%s",
		candidate->path) >= (int)sizeof(root->spool.path))
	{
		free(root->events);
		memset(root, 0, sizeof(*root));
		return 0;
	}
	return 1;
}

static int HumanTraceManifestCaptureJsonEvent(void *opaque,
	const sg_human_trace_v3_event_t *event,
	const uint8_t record_sha256[SG_HUMAN_TRACE_SHA256_BYTES])
{
	human_trace_manifest_json_capture_t *capture = opaque;
	human_trace_spool_event_t *stored;

	if (!capture || !event || !record_sha256 || !HumanTraceManifestGrow(
		(void **)&capture->events, sizeof(*capture->events), capture->count + 1U,
		&capture->capacity))
	{
		if (capture)
			capture->allocation_failed = 1U;
		return 0;
	}
	stored = &capture->events[capture->count++];
	memset(stored, 0, sizeof(*stored));
	stored->event = *event;
	memcpy(stored->record_sha256, record_sha256,
		sizeof(stored->record_sha256));
	return 1;
}

static int HumanTraceManifestSegmentRead(
	const human_trace_manifest_candidate_t *candidate,
	human_trace_manifest_segment_t *segment)
{
	human_trace_manifest_json_capture_t capture;
	qboolean valid;

	if (!candidate || !segment)
		return -1;
	memset(segment, 0, sizeof(*segment));
	memset(&capture, 0, sizeof(capture));
	valid = HumanTraceJsonSegmentRead(candidate->path, &segment->summary,
		HumanTraceManifestCaptureJsonEvent, &capture);
	if (capture.allocation_failed)
	{
		free(capture.events);
		return -1;
	}
	segment->events = capture.events;
	segment->event_count = capture.count;
	segment->event_capacity = capture.capacity;
	if (!valid || segment->summary.header.segment != candidate->segment)
	{
		free(segment->events);
		memset(segment, 0, sizeof(*segment));
		return 0;
	}
	segment->valid = 1U;
	return 1;
}

static int HumanTraceManifestSegmentAppend(human_trace_manifest_t *manifest,
	human_trace_manifest_segment_t *segment)
{
	if (!manifest || !segment || !HumanTraceManifestGrow(
		(void **)&manifest->segments, sizeof(*manifest->segments),
		manifest->segment_count + 1U, &manifest->segment_capacity))
		return 0;
	manifest->segments[manifest->segment_count++] = *segment;
	memset(segment, 0, sizeof(*segment));
	return 1;
}

static int HumanTraceManifestAuthenticateRoot(human_trace_manifest_root_t *root,
	const human_trace_manifest_segment_t *segments, size_t segment_count)
{
	const human_trace_json_segment_t *previous = NULL;
	size_t event_index = 0U;
	size_t segment_index;
	uint8_t zero[SG_HUMAN_TRACE_SHA256_BYTES];

	if (!root || !segments || segment_count == 0U)
		return 0;
	memset(zero, 0, sizeof(zero));
	for (segment_index = 0U; segment_index < segment_count; segment_index++)
	{
		const human_trace_manifest_segment_t *stored =
			&segments[segment_index];
		const human_trace_json_segment_t *current = &stored->summary;
		size_t json_event;

		if (!stored->valid || current->header.session != root->header.session ||
			!HumanTraceJsonStableIdentityEqual(&segments[0].summary.header,
				&current->header))
			return 0;
		if (segment_index != 0U &&
			(current->header.gravity_bits != segments[0].summary.header.gravity_bits ||
			 current->header.airaccelerate_bits !=
				segments[0].summary.header.airaccelerate_bits ||
			 current->header.maxvelocity_bits !=
				segments[0].summary.header.maxvelocity_bits ||
			 current->header.pmove_substep_ms !=
				segments[0].summary.header.pmove_substep_ms ||
			 current->header.server_frame_ms !=
				segments[0].summary.header.server_frame_ms ||
			 current->header.physics_flags !=
				segments[0].summary.header.physics_flags))
			return 0;
		if (segment_index == 0U)
		{
			if (current->header.continuation != 0U || memcmp(
				current->header.previous_sha256, zero, sizeof(zero)) != 0 ||
				current->header.segment != root->header.root_segment ||
				current->header.start_order != 1U ||
				current->header.start_command != 1U ||
				current->header.start_hook_event != 1U || memcmp(
				current->header.sha256, root->header.root_header_sha256,
					SG_HUMAN_TRACE_SHA256_BYTES) != 0 || strncmp(
				current->header.identity.mapname,
				root->header.identity.mapname,
					sizeof(current->header.identity.mapname)) != 0 ||
				current->header.identity.bsp_checksum !=
					root->header.identity.bsp_checksum ||
				current->header.identity.entity_crc32 !=
					root->header.identity.entity_crc32 ||
				current->header.identity.host_physics_id !=
					root->header.identity.host_physics_id)
				return 0;
		}
		else if (!previous || previous->ended ||
			current->header.continuation != 1U || memcmp(
			current->header.previous_sha256, previous->last_sha256,
				SG_HUMAN_TRACE_SHA256_BYTES) != 0 ||
			!HumanTraceJsonRangeFollows(previous, current))
			return 0;
		for (json_event = 0U; json_event < stored->event_count; json_event++)
		{
			if (event_index >= root->event_count || memcmp(
				&root->events[event_index].recorded,
				&stored->events[json_event],
				sizeof(stored->events[json_event])) != 0)
				return 0;
			event_index++;
		}
		previous = current;
	}
	return previous && previous->ended && event_index == root->event_count &&
		HumanTraceJsonFinalMatchesCompletion(previous, &root->spool.completion);
}

static uint8_t HumanTraceManifestScopeKeyByte(
	const human_trace_manifest_scope_occurrence_t *scope, size_t pass)
{
	if (pass < sizeof(uint64_t))
		return (uint8_t)(scope->spawn_generation >> (pass * 8U));
	pass -= sizeof(uint64_t);
	return (uint8_t)(scope->client_id >> (pass * 8U));
}

static int HumanTraceManifestScopeOccurrencesSort(
	human_trace_manifest_scope_occurrence_t *items, size_t item_count)
{
	human_trace_manifest_scope_occurrence_t *temporary, *source, *target;
	size_t count[256], offset[256];
	size_t pass, index;

	if (item_count < 2U)
		return 1;
	if (!items || item_count > SIZE_MAX / sizeof(*temporary) ||
		!(temporary = malloc(item_count * sizeof(*temporary))))
		return 0;
	source = items;
	target = temporary;
	for (pass = 0U; pass < sizeof(uint64_t) + sizeof(uint32_t); pass++)
	{
		memset(count, 0, sizeof(count));
		for (index = 0U; index < item_count; index++)
			count[HumanTraceManifestScopeKeyByte(&source[index], pass)]++;
		offset[0] = 0U;
		for (index = 1U; index < 256U; index++)
			offset[index] = offset[index - 1U] + count[index - 1U];
		for (index = 0U; index < item_count; index++)
		{
			uint8_t bucket = HumanTraceManifestScopeKeyByte(&source[index], pass);

			target[offset[bucket]++] = source[index];
		}
		{
			human_trace_manifest_scope_occurrence_t *swap = source;

			source = target;
			target = swap;
		}
	}
	if (source != items)
		memcpy(items, source, item_count * sizeof(*items));
	free(temporary);
	return 1;
}

static uint8_t HumanTraceManifestScopeOrderByte(
	const human_trace_manifest_scope_order_t *scope, size_t pass)
{
	return (uint8_t)(scope->first_event >> (pass * 8U));
}

static int HumanTraceManifestScopeOrderSort(
	human_trace_manifest_scope_order_t *items, size_t item_count)
{
	human_trace_manifest_scope_order_t *temporary, *source, *target;
	size_t count[256], offset[256];
	size_t pass, index;

	if (item_count < 2U)
		return 1;
	if (!items || item_count > SIZE_MAX / sizeof(*temporary) ||
		!(temporary = malloc(item_count * sizeof(*temporary))))
		return 0;
	source = items;
	target = temporary;
	for (pass = 0U; pass < sizeof(size_t); pass++)
	{
		memset(count, 0, sizeof(count));
		for (index = 0U; index < item_count; index++)
			count[HumanTraceManifestScopeOrderByte(&source[index], pass)]++;
		offset[0] = 0U;
		for (index = 1U; index < 256U; index++)
			offset[index] = offset[index - 1U] + count[index - 1U];
		for (index = 0U; index < item_count; index++)
		{
			uint8_t bucket = HumanTraceManifestScopeOrderByte(&source[index], pass);

			target[offset[bucket]++] = source[index];
		}
		{
			human_trace_manifest_scope_order_t *swap = source;

			source = target;
			target = swap;
		}
	}
	if (source != items)
		memcpy(items, source, item_count * sizeof(*items));
	free(temporary);
	return 1;
}

static int HumanTraceManifestBuildScopes(human_trace_manifest_t *manifest,
	human_trace_manifest_root_t *root)
{
	human_trace_manifest_scope_occurrence_t *occurrences = NULL;
	human_trace_manifest_scope_order_t *order = NULL;
	size_t *group_to_scope = NULL;
	size_t group_count = 0U;
	size_t index;
	int result = 0;

	if (!manifest || !root)
		return 0;
	if (root->event_count == 0U)
		return 1;
	if (root->event_count > SIZE_MAX / sizeof(*occurrences))
		return 0;
	occurrences = malloc(root->event_count * sizeof(*occurrences));
	order = malloc(root->event_count * sizeof(*order));
	if (!occurrences || !order)
		goto finish;
	for (index = 0U; index < root->event_count; index++)
	{
		occurrences[index].client_id = root->events[index].recorded.event.client_id;
		occurrences[index].spawn_generation =
			root->events[index].recorded.event.spawn_generation;
		occurrences[index].event_index = index;
		occurrences[index].group_index = 0U;
	}
	if (!HumanTraceManifestScopeOccurrencesSort(occurrences,
		root->event_count))
		goto finish;
	for (index = 0U; index < root->event_count;)
	{
		size_t after = index + 1U;
		size_t first_event = occurrences[index].event_index;

		while (after < root->event_count &&
			occurrences[after].client_id == occurrences[index].client_id &&
			occurrences[after].spawn_generation ==
				occurrences[index].spawn_generation)
		{
			if (occurrences[after].event_index < first_event)
				first_event = occurrences[after].event_index;
			after++;
		}
		order[group_count].client_id = occurrences[index].client_id;
		order[group_count].spawn_generation =
			occurrences[index].spawn_generation;
		order[group_count].first_event = first_event;
		order[group_count].group_index = group_count;
		while (index < after)
			occurrences[index++].group_index = group_count;
		group_count++;
	}
	if (!HumanTraceManifestScopeOrderSort(order, group_count) ||
		group_count > SIZE_MAX / sizeof(*root->scopes))
		goto finish;
	root->scopes = calloc(group_count, sizeof(*root->scopes));
	root->scope_started = calloc(group_count, sizeof(*root->scope_started));
	group_to_scope = malloc(group_count * sizeof(*group_to_scope));
	if (!root->scopes || !root->scope_started || !group_to_scope)
		goto finish;
	root->scope_count = group_count;
	for (index = 0U; index < group_count; index++)
	{
		sg_human_trace_v3_scope_t *scope = &root->scopes[index];

		group_to_scope[order[index].group_index] = index;
		scope->client_id = order[index].client_id;
		scope->spawn_generation = order[index].spawn_generation;
	}
	for (index = 0U; index < root->event_count; index++)
		root->events[occurrences[index].event_index].scope_index =
			group_to_scope[occurrences[index].group_index];
	result = 1;
finish:
	free(occurrences);
	free(order);
	free(group_to_scope);
	if (!result)
	{
		free(root->scopes);
		free(root->scope_started);
		root->scopes = NULL;
		root->scope_started = NULL;
		root->scope_count = 0U;
	}
	return result;
}

static void HumanTraceManifestSegmentsClear(human_trace_manifest_t *manifest)
{
	size_t index;

	if (!manifest)
		return;
	for (index = 0U; index < manifest->segment_count; index++)
		free(manifest->segments[index].events);
	free(manifest->segments);
	manifest->segments = NULL;
	manifest->segment_count = 0U;
	manifest->segment_capacity = 0U;
}

static void HumanTraceManifestRootClear(human_trace_manifest_root_t *root)
{
	if (!root)
		return;
	free(root->events);
	free(root->scopes);
	free(root->scope_started);
	memset(root, 0, sizeof(*root));
}

static int HumanTraceManifestReadSegments(human_trace_manifest_t *manifest,
	human_trace_manifest_root_t *root, size_t first, size_t after)
{
	uint64_t expected;
	size_t index;
	int valid = 1;

	if (!manifest || !root || first > after ||
		after > manifest->candidate_count)
		return -1;
	expected = root->header.root_segment;
	for (index = first; index < after; index++)
	{
		human_trace_manifest_candidate_t *candidate =
			&manifest->candidates[index];
		human_trace_manifest_segment_t segment;
		int read;

		if (candidate->kind != HUMAN_TRACE_MANIFEST_JSON ||
			candidate->segment < root->header.root_segment ||
			candidate->segment > root->spool.completion.segment)
			continue;
		if ((uint64_t)candidate->segment != expected)
		{
			valid = 0;
			continue;
		}
		read = HumanTraceManifestSegmentRead(candidate, &segment);
		if (read < 0)
			return -1;
		if (read == 0)
			valid = 0;
		else if (!HumanTraceManifestSegmentAppend(manifest, &segment))
		{
			free(segment.events);
			return -1;
		}
		expected++;
	}
	if (expected != (uint64_t)root->spool.completion.segment + 1U)
		valid = 0;
	return valid;
}

static int HumanTraceManifestVisitRoot(human_trace_manifest_t *manifest,
	human_trace_manifest_root_t *root,
	const sg_human_trace_v3_collection_visitor_t *visitor, void *context)
{
	size_t event_index;
	int result;

	if (!manifest || !root || !visitor)
		return 0;
	if (!HumanTraceManifestAuthenticateRoot(root, manifest->segments,
		manifest->segment_count) ||
		!HumanTraceManifestBuildScopes(manifest, root))
		return 1;
	result = visitor->begin_root(context, &root->spool);
	for (event_index = 0U; result && event_index < root->event_count;
		event_index++)
	{
		human_trace_manifest_event_t *stored = &root->events[event_index];
		const sg_human_trace_v3_event_t *event = &stored->recorded.event;
		sg_human_trace_v3_scope_t *scope;

		if (stored->scope_index >= root->scope_count)
			return 0;
		scope = &root->scopes[stored->scope_index];
		if (event->client_id != scope->client_id ||
			event->spawn_generation != scope->spawn_generation)
			return 0;
		if (!root->scope_started[stored->scope_index])
		{
			root->scope_started[stored->scope_index] = 1U;
			result = visitor->scope(context, scope);
		}
		if (result)
			result = visitor->event(context, scope, event);
	}
	if (result)
		result = visitor->finish_root(context);
	return result;
}

int SG_HumanTraceVisitAcceptedV3Collection(const sg_level_identity_t *identity,
	const sg_human_trace_v3_collection_visitor_t *visitor, void *context)
{
	human_trace_manifest_t manifest;
	char directory[512];
	size_t root_index, json_cursor = 0U;
	int result = 1;

	if (!identity || !identity->mapname[0] || !visitor ||
		!visitor->begin_root || !visitor->scope || !visitor->event ||
		!visitor->finish_root || sg_human_trace_collection_active ||
		!HumanTraceDirectoryCurrent(directory))
		return 0;
	memset(&manifest, 0, sizeof(manifest));
	if (!HumanTraceManifestScan(directory, identity, &manifest) ||
		!HumanTraceManifestCandidatesSort(&manifest))
	{
		HumanTraceManifestFree(&manifest);
		return 0;
	}
	sg_human_trace_collection_active = 1U;
	root_index = 0U;
	while (result && root_index < manifest.candidate_count)
	{
		human_trace_manifest_candidate_t *candidate;
		human_trace_manifest_root_t root;
		size_t after, next_root;
		uint64_t boundary = UINT64_C(1) + UINT32_MAX;
		int read, segments;

		while (root_index < manifest.candidate_count &&
			manifest.candidates[root_index].kind != HUMAN_TRACE_MANIFEST_SPOOL)
			root_index++;
		if (root_index >= manifest.candidate_count)
			break;
		candidate = &manifest.candidates[root_index];
		next_root = root_index + 1U;
		while (next_root < manifest.candidate_count &&
			manifest.candidates[next_root].kind != HUMAN_TRACE_MANIFEST_SPOOL)
			next_root++;
		if (next_root < manifest.candidate_count)
			boundary = manifest.candidates[next_root].segment;
		after = json_cursor;
		while (after < manifest.candidate_count &&
			(uint64_t)manifest.candidates[after].segment < boundary)
			after++;
		read = HumanTraceManifestRootRead(candidate, identity, &root);
		if (read < 0)
			result = 0;
		else if (read > 0 &&
			(uint64_t)root.spool.completion.segment < boundary)
		{
			segments = HumanTraceManifestReadSegments(&manifest, &root,
				json_cursor, after);
			if (segments < 0)
				result = 0;
			else if (segments > 0)
				result = HumanTraceManifestVisitRoot(&manifest, &root,
					visitor, context);
		}
		HumanTraceManifestRootClear(&root);
		HumanTraceManifestSegmentsClear(&manifest);
		json_cursor = after;
		root_index = next_root;
	}
	sg_human_trace_collection_active = 0U;
	HumanTraceManifestFree(&manifest);
	return result;
}

void SG_HumanTracePmove(edict_t *entity,
	const pmove_state_t *before, const pmove_t *after)
{
	human_trace_builder_t builder;
	qboolean event_ready = false;
	sg_human_trace_v3_event_t event;
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
	memset(&event, 0, sizeof(event));
	if (level.framenum < 0)
		sg_human_trace_event_failed = true;
	else
	{
		event.kind = SG_HUMAN_TRACE_V3_EVENT_STEP;
		event.order = next_order;
		event.command = next_command;
		event.spawn_generation = spawn_generation;
		event.client_id = (uint32_t)client_key;
		event.frame = (uint32_t)level.framenum;
		event.level_time_bits = HumanTraceLevelTimeBits();
		event.after_origin[0] = (int16_t)after->s.origin[0];
		event.after_origin[1] = (int16_t)after->s.origin[1];
		event.after_origin[2] = (int16_t)after->s.origin[2];
		event.command_msec = command->msec;
		event.grounded = HumanTraceEntityKey(after->groundentity) >= 0 ?
			1U : 0U;
		event_ready = HumanTraceEventPrepare(&event);
	}
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
		if (event_ready)
			HumanTraceEventCommit(&event);
		sg_human_trace_order = next_order;
		sg_human_trace_command = next_command;
	}
}

void SG_HumanTraceHookFire(edict_t *entity, edict_t *hook)
{
	human_trace_builder_t builder;
	human_trace_hook_snapshot_t snapshot;
	qboolean event_ready = false;
	sg_human_trace_v3_event_t event;
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
	memset(&event, 0, sizeof(event));
	if (level.framenum < 0)
		sg_human_trace_event_failed = true;
	else
	{
		event.kind = SG_HUMAN_TRACE_V3_EVENT_HOOK_FIRE;
		event.order = next_order;
		event.hook_event = next_event;
		event.after_command = sg_human_trace_command;
		event.spawn_generation = spawn_generation;
		event.client_id = (uint32_t)client_key;
		event.frame = (uint32_t)level.framenum;
		event.level_time_bits = HumanTraceLevelTimeBits();
		HumanTraceEventHookSnapshot(&event, &snapshot, hook_key);
		event_ready = HumanTraceEventPrepare(&event);
	}
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
		if (event_ready)
			HumanTraceEventCommit(&event);
		sg_human_trace_order = next_order;
		sg_human_trace_hook_event = next_event;
	}
}

void SG_HumanTraceHookAttach(edict_t *entity, edict_t *hook,
	edict_t *target)
{
	human_trace_builder_t builder;
	human_trace_hook_snapshot_t snapshot;
	qboolean event_ready = false;
	sg_human_trace_v3_event_t event;
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
	memset(&event, 0, sizeof(event));
	if (level.framenum < 0)
		sg_human_trace_event_failed = true;
	else
	{
		event.kind = SG_HUMAN_TRACE_V3_EVENT_HOOK_ATTACH;
		event.order = next_order;
		event.hook_event = next_event;
		event.after_command = sg_human_trace_command;
		event.spawn_generation = spawn_generation;
		event.client_id = (uint32_t)client_key;
		event.frame = (uint32_t)level.framenum;
		event.level_time_bits = HumanTraceLevelTimeBits();
		HumanTraceEventHookSnapshot(&event, &snapshot, hook_key);
		event_ready = HumanTraceEventPrepare(&event);
	}
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
		if (event_ready)
			HumanTraceEventCommit(&event);
		sg_human_trace_order = next_order;
		sg_human_trace_hook_event = next_event;
	}
}

static void HumanTraceHookTerminal(edict_t *entity, edict_t *hook,
	const char *kind)
{
	human_trace_builder_t builder;
	human_trace_hook_snapshot_t snapshot;
	qboolean event_ready = false;
	sg_human_trace_v3_event_t event;
	sg_human_trace_v3_event_kind_t event_kind;
	uint64_t spawn_generation;
	uint64_t next_order, next_event;
	int client_key, hook_key;

	if (!HumanTraceReady(entity, &client_key, &spawn_generation) ||
	    !hook || hook->owner != entity ||
	    (hook_key = HumanTraceEntityKey(hook)) <= 0 ||
	    sg_human_trace_order == UINT64_MAX ||
	    sg_human_trace_hook_event == UINT64_MAX)
		return;
	if (strcmp(kind, "hook-release") == 0)
		event_kind = SG_HUMAN_TRACE_V3_EVENT_HOOK_RELEASE;
	else if (strcmp(kind, "hook-reset") == 0)
		event_kind = SG_HUMAN_TRACE_V3_EVENT_HOOK_RESET;
	else
		return;
	if (!HumanTracePrepareRecord())
		return;
	next_order = sg_human_trace_order + 1U;
	next_event = sg_human_trace_hook_event + 1U;
	HumanTraceHookSnapshot(entity, hook, &snapshot);
	memset(&event, 0, sizeof(event));
	if (level.framenum < 0)
		sg_human_trace_event_failed = true;
	else
	{
		event.kind = event_kind;
		event.order = next_order;
		event.hook_event = next_event;
		event.after_command = sg_human_trace_command;
		event.spawn_generation = spawn_generation;
		event.client_id = (uint32_t)client_key;
		event.frame = (uint32_t)level.framenum;
		event.level_time_bits = HumanTraceLevelTimeBits();
		HumanTraceEventHookSnapshot(&event, &snapshot, hook_key);
		event_ready = HumanTraceEventPrepare(&event);
	}
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
		if (event_ready)
			HumanTraceEventCommit(&event);
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
