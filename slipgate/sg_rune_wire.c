/* sg_rune_wire.c -- allocation-free, explicit little-endian RUNE v3 codec. */
#include "slipgate/sg_rune_wire.h"

#include "slipgate/sg_action.h"
#include "slipgate/sg_crc32.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <string.h>

_Static_assert(CHAR_BIT == 8, "RUNE v3 requires 8-bit bytes");
_Static_assert(sizeof(float) == 4, "RUNE v3 requires 32-bit float");
_Static_assert(FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128,
	"RUNE v3 requires IEEE-754 binary32 float");
_Static_assert(SG_RUNE_V3_HEADER_BYTES == 128,
	"RUNE v3 header size drift");
_Static_assert(SG_RUNE_V3_SEED_BYTES == 16,
	"RUNE v3 seed size drift");
_Static_assert(SG_RUNE_V3_LINK_BYTES == 44,
	"RUNE v3 link size drift");

#define SG_RUNE_V3_PAYLOAD_CRC_OFFSET 20U
#define SG_RUNE_V3_MAP_OFFSET 64U

static void Wire_PutU16(unsigned char *out, uint16_t value)
{
	out[0] = (unsigned char)(value & UINT16_C(0xff));
	out[1] = (unsigned char)(value >> 8);
}

static void Wire_PutU32(unsigned char *out, uint32_t value)
{
	out[0] = (unsigned char)(value & UINT32_C(0xff));
	out[1] = (unsigned char)((value >> 8) & UINT32_C(0xff));
	out[2] = (unsigned char)((value >> 16) & UINT32_C(0xff));
	out[3] = (unsigned char)(value >> 24);
}

static uint16_t Wire_GetU16(const unsigned char *in)
{
	return (uint16_t)((uint16_t)in[0] | (uint16_t)((uint16_t)in[1] << 8));
}

static uint32_t Wire_GetU32(const unsigned char *in)
{
	return (uint32_t)in[0] |
	       ((uint32_t)in[1] << 8) |
	       ((uint32_t)in[2] << 16) |
	       ((uint32_t)in[3] << 24);
}

static void Wire_PutI16(unsigned char *out, int16_t value)
{
	Wire_PutU16(out, (uint16_t)value);
}

static int16_t Wire_GetI16(const unsigned char *in)
{
	uint16_t value = Wire_GetU16(in);
	int32_t signed_value = value <= INT16_MAX
		? (int32_t)value : (int32_t)value - INT32_C(65536);

	return (int16_t)signed_value;
}

static uint32_t Wire_FloatBits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static void Wire_PutFloat(unsigned char *out, float value)
{
	Wire_PutU32(out, Wire_FloatBits(value));
}

static float Wire_GetFloat(const unsigned char *in)
{
	uint32_t bits = Wire_GetU32(in);
	float value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

static int Wire_MapInitial(unsigned char c)
{
	return (c >= (unsigned char)'A' && c <= (unsigned char)'Z') ||
	       (c >= (unsigned char)'a' && c <= (unsigned char)'z') ||
	       (c >= (unsigned char)'0' && c <= (unsigned char)'9') ||
	       c == (unsigned char)'_';
}

static int Wire_MapTail(unsigned char c)
{
	return Wire_MapInitial(c) || c == (unsigned char)'-';
}

static int Wire_MapNameValid(const char map_name[SG_RUNE_V3_MAP_NAME_BYTES])
{
	size_t i;
	size_t j;

	if (!map_name || !Wire_MapInitial((unsigned char)map_name[0]))
		return 0;
	for (i = 1; i < SG_RUNE_V3_MAP_NAME_BYTES; i++)
	{
		unsigned char c = (unsigned char)map_name[i];

		if (c == 0)
		{
			for (j = i + 1; j < SG_RUNE_V3_MAP_NAME_BYTES; j++)
				if (map_name[j] != '\0')
					return 0;
			return 1;
		}
		if (!Wire_MapTail(c))
			return 0;
	}
	return 0;
}

static int Wire_FloatFinite(float value)
{
	return isfinite(value) != 0;
}

static int Wire_VectorFinite(const float vector[3])
{
	return vector && Wire_FloatFinite(vector[0]) &&
	       Wire_FloatFinite(vector[1]) && Wire_FloatFinite(vector[2]);
}

static int Wire_VectorExactZero(const float vector[3])
{
	return vector && Wire_FloatBits(vector[0]) == UINT32_C(0) &&
	       Wire_FloatBits(vector[1]) == UINT32_C(0) &&
	       Wire_FloatBits(vector[2]) == UINT32_C(0);
}

static int Wire_VectorInFixedWorld(const float vector[3])
{
	static const float minimum =
		(float)SG_RUNE_PROOF_WORLD_FIXED_MIN /
		(float)SG_RUNE_PROOF_WORLD_FIXED_SCALE;
	static const float maximum =
		(float)SG_RUNE_PROOF_WORLD_FIXED_MAX /
		(float)SG_RUNE_PROOF_WORLD_FIXED_SCALE;
	int i;

	if (!Wire_VectorFinite(vector))
		return 0;
	for (i = 0; i < 3; i++)
		if (vector[i] < minimum || vector[i] > maximum)
			return 0;
	return 1;
}

static int Wire_VectorOnDoorLattice(const float vector[3])
{
	int i;

	if (!Wire_VectorInFixedWorld(vector))
		return 0;
	for (i = 0; i < 3; i++)
	{
		float scaled = vector[i] *
			(float)SG_RUNE_PROOF_DOOR_ANCHOR_SCALE;

		if (scaled != (float)(int)scaled)
			return 0;
		if (scaled < (float)SG_RUNE_PROOF_WORLD_FIXED_MIN ||
		    scaled > (float)SG_RUNE_PROOF_WORLD_FIXED_MAX)
			return 0;
	}
	return 1;
}

static rune_wire_diagnostic_t Wire_ValidatePhysics(
	uint32_t physics_flags, float gravity, float airaccelerate,
	float maxvelocity, uint16_t pmove_substep_ms,
	uint16_t server_frame_ms, uint32_t host_physics_id)
{
	if (physics_flags != SG_RUNE_PROOF_PHYSICS_FLAGS_SUPPORTED)
		return RLW_BAD_PHYSICS_LAW;
	if (!Wire_FloatFinite(gravity) ||
	    gravity < (float)SG_RUNE_PROOF_GRAVITY_MIN ||
	    gravity > (float)SG_RUNE_PROOF_GRAVITY_MAX ||
	    (SG_RUNE_PROOF_GRAVITY_INTEGRAL_REQUIRED &&
	     gravity != (float)(int)gravity))
		return RLW_BAD_PHYSICS_LAW;
	if (!Wire_FloatFinite(airaccelerate) ||
	    (SG_RUNE_PROOF_AIRACCELERATE_ZERO_REQUIRED &&
	     airaccelerate != 0.0f))
		return RLW_BAD_PHYSICS_LAW;
	if (!Wire_FloatFinite(maxvelocity) ||
	    maxvelocity < (float)SG_RUNE_PROOF_MAXVELOCITY_MIN)
		return RLW_BAD_PHYSICS_LAW;
	if (pmove_substep_ms != SG_RUNE_PROOF_PMOVE_SUBSTEP_MS ||
	    server_frame_ms != SG_RUNE_PROOF_SERVER_FRAME_MS)
		return RLW_BAD_PHYSICS_LAW;
	if (host_physics_id < SG_RUNE_PROOF_HOST_PHYSICS_ID_MIN)
		return RLW_IDENTITY_UNAVAILABLE;
	return RLW_OK;
}

static rune_wire_diagnostic_t Wire_ValidateIdentity(
	const sg_rune_v3_identity_t *identity)
{
	if (!identity)
		return RLW_INVALID_ARGUMENT;
	if (!Wire_MapNameValid(identity->map_name))
		return RLW_BAD_MAPNAME;
	return Wire_ValidatePhysics(identity->physics_flags, identity->gravity,
		identity->airaccelerate, identity->maxvelocity,
		identity->pmove_substep_ms, identity->server_frame_ms,
		identity->host_physics_id);
}

static rune_wire_diagnostic_t Wire_ValidateHeaderSemantic(
	const sg_rune_v3_header_t *header)
{
	if (!header)
		return RLW_INVALID_ARGUMENT;
	if (!Wire_MapNameValid(header->map_name))
		return RLW_BAD_MAPNAME;
	if (header->action_contract_crc32 != SG_ACTION_CONTRACT_CRC32)
		return RLW_BAD_ACTION_CONTRACT;
	return Wire_ValidatePhysics(header->physics_flags, header->gravity,
		header->airaccelerate, header->maxvelocity,
		header->pmove_substep_ms, header->server_frame_ms,
		header->host_physics_id);
}

static rune_wire_diagnostic_t Wire_ValidateHeaderFixed(
	const sg_rune_v3_header_t *header)
{
	if (!header)
		return RLW_INVALID_ARGUMENT;
	if (header->magic != SG_RUNE_V3_MAGIC)
		return RLW_BAD_MAGIC;
	if (header->version != SG_RUNE_V3_VERSION)
		return RLW_UNSUPPORTED_VERSION;
	if (header->header_bytes != SG_RUNE_V3_HEADER_BYTES)
		return RLW_BAD_HEADER_SIZE;
	if (header->seed_bytes != SG_RUNE_V3_SEED_BYTES)
		return RLW_BAD_SEED_SIZE;
	if (header->link_bytes != SG_RUNE_V3_LINK_BYTES)
		return RLW_BAD_LINK_SIZE;
	if (header->num_seeds == 0 ||
	    header->num_seeds > SG_RUNE_V3_MAX_SEEDS ||
	    header->num_links > SG_RUNE_V3_MAX_LINKS)
		return RLW_BAD_COUNTS;
	return RLW_OK;
}

rune_wire_diagnostic_t SG_RuneV3FileSize(uint32_t num_seeds,
	uint32_t num_links, size_t *size_out)
{
	size_t seed_bytes;
	size_t link_bytes;

	if (!size_out)
		return RLW_INVALID_ARGUMENT;
	*size_out = 0;
	if (num_seeds == 0 || num_seeds > SG_RUNE_V3_MAX_SEEDS ||
	    num_links > SG_RUNE_V3_MAX_LINKS)
		return RLW_BAD_COUNTS;
	seed_bytes = (size_t)num_seeds * (size_t)SG_RUNE_V3_SEED_BYTES;
	link_bytes = (size_t)num_links * (size_t)SG_RUNE_V3_LINK_BYTES;
	if (seed_bytes > SIZE_MAX - SG_RUNE_V3_HEADER_BYTES ||
	    link_bytes > SIZE_MAX - SG_RUNE_V3_HEADER_BYTES - seed_bytes)
		return RLW_BAD_FILE_SIZE;
	*size_out = (size_t)SG_RUNE_V3_HEADER_BYTES + seed_bytes + link_bytes;
	return RLW_OK;
}

rune_wire_diagnostic_t SG_RuneV3PayloadCRCInit(uint32_t *state_out)
{
	if (!state_out)
		return RLW_INVALID_ARGUMENT;
	*state_out = SG_CRC32Init();
	return RLW_OK;
}

rune_wire_diagnostic_t SG_RuneV3PayloadCRCUpdate(uint32_t *state,
	const void *fragment, size_t fragment_size)
{
	return SG_CRC32Update(state, fragment, fragment_size)
		? RLW_OK : RLW_INVALID_ARGUMENT;
}

rune_wire_diagnostic_t SG_RuneV3PayloadCRCFinish(uint32_t state,
	uint32_t *crc_out)
{
	if (!crc_out)
		return RLW_INVALID_ARGUMENT;
	*crc_out = SG_CRC32Final(state);
	return RLW_OK;
}

rune_wire_diagnostic_t SG_RuneV3HeaderCRC32(const unsigned char *encoded,
	size_t encoded_size, uint32_t *crc_out)
{
	static const unsigned char zero_crc[4] = { 0, 0, 0, 0 };
	uint32_t state;

	if (!encoded || !crc_out)
		return RLW_INVALID_ARGUMENT;
	*crc_out = 0;
	if (encoded_size != SG_RUNE_V3_HEADER_BYTES)
		return RLW_BAD_HEADER_SIZE;
	state = SG_CRC32Init();
	if (!SG_CRC32Update(&state, encoded, SG_RUNE_V3_HEADER_CRC_OFFSET) ||
	    !SG_CRC32Update(&state, zero_crc, sizeof(zero_crc)) ||
	    !SG_CRC32Update(&state,
	        encoded + SG_RUNE_V3_HEADER_CRC_OFFSET + sizeof(zero_crc),
	        SG_RUNE_V3_HEADER_BYTES - SG_RUNE_V3_HEADER_CRC_OFFSET -
	        sizeof(zero_crc)))
		return RLW_INVALID_ARGUMENT;
	*crc_out = SG_CRC32Final(state);
	return RLW_OK;
}

static void Wire_EncodeHeaderFields(const sg_rune_v3_header_t *header,
	unsigned char out[SG_RUNE_V3_HEADER_BYTES])
{
	memset(out, 0, SG_RUNE_V3_HEADER_BYTES);
	Wire_PutU32(out + 0, header->magic);
	Wire_PutU16(out + 4, header->version);
	Wire_PutU16(out + 6, header->header_bytes);
	Wire_PutU16(out + 8, header->seed_bytes);
	Wire_PutU16(out + 10, header->link_bytes);
	Wire_PutU32(out + 12, header->num_seeds);
	Wire_PutU32(out + 16, header->num_links);
	Wire_PutU32(out + SG_RUNE_V3_PAYLOAD_CRC_OFFSET,
		header->payload_crc32);
	Wire_PutU32(out + 24, header->bsp_checksum);
	Wire_PutU32(out + 28, header->entity_crc32);
	Wire_PutU32(out + 32, header->action_contract_crc32);
	Wire_PutU32(out + 36, header->physics_flags);
	Wire_PutFloat(out + 40, header->gravity);
	Wire_PutFloat(out + 44, header->airaccelerate);
	Wire_PutFloat(out + 48, header->maxvelocity);
	Wire_PutU16(out + 52, header->pmove_substep_ms);
	Wire_PutU16(out + 54, header->server_frame_ms);
	Wire_PutU32(out + 56, header->host_physics_id);
	Wire_PutU32(out + SG_RUNE_V3_HEADER_CRC_OFFSET, 0);
	memcpy(out + SG_RUNE_V3_MAP_OFFSET, header->map_name,
		SG_RUNE_V3_MAP_NAME_BYTES);
}

rune_wire_diagnostic_t SG_RuneV3EncodeHeader(
	const sg_rune_v3_header_t *header, unsigned char *encoded,
	size_t encoded_size)
{
	unsigned char raw[SG_RUNE_V3_HEADER_BYTES];
	uint32_t crc;
	rune_wire_diagnostic_t diagnostic;

	if (!header || !encoded)
		return RLW_INVALID_ARGUMENT;
	if (encoded_size != SG_RUNE_V3_HEADER_BYTES)
		return RLW_BAD_HEADER_SIZE;
	diagnostic = Wire_ValidateHeaderFixed(header);
	if (diagnostic != RLW_OK)
		return diagnostic;
	diagnostic = Wire_ValidateHeaderSemantic(header);
	if (diagnostic != RLW_OK)
		return diagnostic;
	Wire_EncodeHeaderFields(header, raw);
	diagnostic = SG_RuneV3HeaderCRC32(raw, sizeof(raw), &crc);
	if (diagnostic != RLW_OK)
		return diagnostic;
	Wire_PutU32(raw + SG_RUNE_V3_HEADER_CRC_OFFSET, crc);
	memcpy(encoded, raw, sizeof(raw));
	return RLW_OK;
}

static rune_wire_diagnostic_t Wire_DecodeHeaderPrefix(
	const unsigned char *encoded, size_t encoded_size,
	sg_rune_v3_header_t *header_out)
{
	uint32_t computed_crc;
	rune_wire_diagnostic_t diagnostic;

	if (!encoded || !header_out)
		return RLW_INVALID_ARGUMENT;
	if (encoded_size != SG_RUNE_V3_HEADER_BYTES)
		return RLW_BAD_HEADER_SIZE;
	memset(header_out, 0, sizeof(*header_out));
	header_out->magic = Wire_GetU32(encoded + 0);
	header_out->version = Wire_GetU16(encoded + 4);
	header_out->header_bytes = Wire_GetU16(encoded + 6);
	header_out->seed_bytes = Wire_GetU16(encoded + 8);
	header_out->link_bytes = Wire_GetU16(encoded + 10);
	header_out->num_seeds = Wire_GetU32(encoded + 12);
	header_out->num_links = Wire_GetU32(encoded + 16);
	header_out->payload_crc32 = Wire_GetU32(
		encoded + SG_RUNE_V3_PAYLOAD_CRC_OFFSET);
	header_out->bsp_checksum = Wire_GetU32(encoded + 24);
	header_out->entity_crc32 = Wire_GetU32(encoded + 28);
	header_out->action_contract_crc32 = Wire_GetU32(encoded + 32);
	header_out->physics_flags = Wire_GetU32(encoded + 36);
	header_out->gravity = Wire_GetFloat(encoded + 40);
	header_out->airaccelerate = Wire_GetFloat(encoded + 44);
	header_out->maxvelocity = Wire_GetFloat(encoded + 48);
	header_out->pmove_substep_ms = Wire_GetU16(encoded + 52);
	header_out->server_frame_ms = Wire_GetU16(encoded + 54);
	header_out->host_physics_id = Wire_GetU32(encoded + 56);
	header_out->header_crc32 = Wire_GetU32(
		encoded + SG_RUNE_V3_HEADER_CRC_OFFSET);
	memcpy(header_out->map_name, encoded + SG_RUNE_V3_MAP_OFFSET,
		SG_RUNE_V3_MAP_NAME_BYTES);

	if (header_out->magic != SG_RUNE_V3_MAGIC)
		return RLW_BAD_MAGIC;
	if (header_out->version != SG_RUNE_V3_VERSION)
		return RLW_UNSUPPORTED_VERSION;
	if (header_out->header_bytes != SG_RUNE_V3_HEADER_BYTES)
		return RLW_BAD_HEADER_SIZE;
	if (header_out->seed_bytes != SG_RUNE_V3_SEED_BYTES)
		return RLW_BAD_SEED_SIZE;
	if (header_out->link_bytes != SG_RUNE_V3_LINK_BYTES)
		return RLW_BAD_LINK_SIZE;
	diagnostic = SG_RuneV3HeaderCRC32(encoded, encoded_size, &computed_crc);
	if (diagnostic != RLW_OK)
		return diagnostic;
	if (computed_crc != header_out->header_crc32)
		return RLW_BAD_HEADER_CRC;
	if (header_out->num_seeds == 0 ||
	    header_out->num_seeds > SG_RUNE_V3_MAX_SEEDS ||
	    header_out->num_links > SG_RUNE_V3_MAX_LINKS)
		return RLW_BAD_COUNTS;
	return RLW_OK;
}

rune_wire_diagnostic_t SG_RuneV3DecodeHeader(const unsigned char *encoded,
	size_t encoded_size, sg_rune_v3_header_t *header_out)
{
	rune_wire_diagnostic_t diagnostic;

	diagnostic = Wire_DecodeHeaderPrefix(encoded, encoded_size, header_out);
	if (diagnostic != RLW_OK)
		return diagnostic;
	return Wire_ValidateHeaderSemantic(header_out);
}

static rune_wire_diagnostic_t Wire_ValidateSeed(
	const sg_rune_v3_seed_t *seed)
{
	if (!seed || !Wire_VectorInFixedWorld(seed->origin) ||
	    seed->area_hint < 0 || seed->area_hint > 255 ||
	    seed->flags < 0 ||
	    ((uint16_t)seed->flags & ~SG_RUNE_V3_SEED_FLAG_MASK) != 0)
		return RLW_BAD_SEED_RECORD;
	return RLW_OK;
}

rune_wire_diagnostic_t SG_RuneV3EncodeSeed(const sg_rune_v3_seed_t *seed,
	unsigned char *encoded, size_t encoded_size)
{
	unsigned char raw[SG_RUNE_V3_SEED_BYTES];
	rune_wire_diagnostic_t diagnostic;

	if (!seed || !encoded)
		return RLW_INVALID_ARGUMENT;
	if (encoded_size != SG_RUNE_V3_SEED_BYTES)
		return RLW_BAD_SEED_SIZE;
	diagnostic = Wire_ValidateSeed(seed);
	if (diagnostic != RLW_OK)
		return diagnostic;
	Wire_PutFloat(raw + 0, seed->origin[0]);
	Wire_PutFloat(raw + 4, seed->origin[1]);
	Wire_PutFloat(raw + 8, seed->origin[2]);
	Wire_PutI16(raw + 12, seed->area_hint);
	Wire_PutI16(raw + 14, seed->flags);
	memcpy(encoded, raw, sizeof(raw));
	return RLW_OK;
}

rune_wire_diagnostic_t SG_RuneV3DecodeSeed(const unsigned char *encoded,
	size_t encoded_size, sg_rune_v3_seed_t *seed_out)
{
	sg_rune_v3_seed_t seed;
	rune_wire_diagnostic_t diagnostic;

	if (!encoded || !seed_out)
		return RLW_INVALID_ARGUMENT;
	if (encoded_size != SG_RUNE_V3_SEED_BYTES)
		return RLW_BAD_SEED_SIZE;
	seed.origin[0] = Wire_GetFloat(encoded + 0);
	seed.origin[1] = Wire_GetFloat(encoded + 4);
	seed.origin[2] = Wire_GetFloat(encoded + 8);
	seed.area_hint = Wire_GetI16(encoded + 12);
	seed.flags = Wire_GetI16(encoded + 14);
	diagnostic = Wire_ValidateSeed(&seed);
	if (diagnostic != RLW_OK)
		return diagnostic;
	*seed_out = seed;
	return RLW_OK;
}

static rune_wire_diagnostic_t Wire_ValidateAnchor(const float anchor[3],
	int policy)
{
	if (!Wire_VectorFinite(anchor))
		return RLW_BAD_LINK_RECORD;
	switch (policy)
	{
	case RLAP_ZERO:
		return Wire_VectorExactZero(anchor) ? RLW_OK : RLW_BAD_LINK_RECORD;
	case RLAP_RUN_WAYPOINT:
	case RLAP_DROP_LIP:
	case RLAP_WORLD:
	case RLAP_TELEPORT_PAD:
		return Wire_VectorInFixedWorld(anchor)
			? RLW_OK : RLW_BAD_LINK_RECORD;
	case RLAP_DOOR_WAIT:
	case RLAP_DOOR_PREOPEN_CONTACT:
	case RLAP_DOOR_RIDE_INGRESS_LIP:
		return Wire_VectorOnDoorLattice(anchor)
			? RLW_OK : RLW_BAD_LINK_RECORD;
	case RLAP_HOOK_CONTROL:
	case RLAP_UNSUPPORTED:
		return RLW_OK;
	default:
		return RLW_BAD_LINK_RECORD;
	}
}

int SG_RuneV3ActionWireKnown(uint8_t action)
{
	return SG_ActionWireValid(SG_RUNE_WIRE_V3, (int)action);
}

int SG_RuneV3ActionRuntimeSupported(uint8_t action)
{
	return SG_ActionRuntimeSupported((int)action);
}

static rune_wire_diagnostic_t Wire_ValidateLinkFields(
	const sg_rune_v3_link_t *link)
{
	const sg_action_desc_t *action;
	rune_wire_diagnostic_t diagnostic;
	int mechanism_policy;

	if (!link)
		return RLW_BAD_LINK_RECORD;
	if (!SG_RuneV3ActionWireKnown(link->action))
		return RLW_BAD_LINK_RECORD;
	action = SG_ActionDescribe((int)link->action);
	if (!action ||
	    !SG_ProvenanceWireValid(SG_RUNE_WIRE_V3,
	        (int)link->provenance) ||
	    !SG_ModeWireValid(SG_RUNE_WIRE_V3, (int)link->mode) ||
	    !SG_ActionAllowsProvenance((int)link->action,
	        (int)link->provenance) ||
	    !SG_ActionAllowsMode((int)link->action, (int)link->mode) ||
	    link->cost_ms < SG_RUNE_V3_MIN_COST_MS ||
	    link->cost_ms > SG_RUNE_V3_MAX_COST_MS || link->reserved != 0)
		return RLW_BAD_LINK_RECORD;
	diagnostic = Wire_ValidateAnchor(link->suffix_anchor,
		(int)action->suffix_anchor_policy);
	if (diagnostic != RLW_OK)
		return diagnostic;
	if (!SG_ActionHasTrait((int)link->action, SG_ACTF_ATOMIC))
	{
		if (!Wire_VectorExactZero(link->mechanism_anchor) ||
		    link->sweep_clear_ms != 0 || link->mode != RLCM_NONE ||
		    link->reserved != 0)
			return RLW_BAD_LINK_RECORD;
		return RLW_OK;
	}
	if (link->sweep_clear_ms == 0 ||
	    link->sweep_clear_ms % SG_RUNE_PROOF_SERVER_FRAME_MS != 0 ||
	    link->sweep_clear_ms > (uint16_t)link->cost_ms ||
	    action->mechanism_policy != RLMP_DOOR_WORLD_FIXED_1_8)
		return RLW_BAD_LINK_RECORD;
	if (link->mode == RLCM_PREOPEN)
		mechanism_policy = (int)action->preopen_mechanism_anchor_policy;
	else if (link->mode == RLCM_RIDE)
		mechanism_policy = (int)action->ride_mechanism_anchor_policy;
	else
		return RLW_BAD_LINK_RECORD;
	return Wire_ValidateAnchor(link->mechanism_anchor, mechanism_policy);
}

rune_wire_diagnostic_t SG_RuneV3EncodeLink(const sg_rune_v3_link_t *link,
	unsigned char *encoded, size_t encoded_size)
{
	unsigned char raw[SG_RUNE_V3_LINK_BYTES];
	rune_wire_diagnostic_t diagnostic;

	if (!link || !encoded)
		return RLW_INVALID_ARGUMENT;
	if (encoded_size != SG_RUNE_V3_LINK_BYTES)
		return RLW_BAD_LINK_SIZE;
	diagnostic = Wire_ValidateLinkFields(link);
	if (diagnostic != RLW_OK)
		return diagnostic;
	Wire_PutU32(raw + 0, link->source);
	Wire_PutU32(raw + 4, link->destination);
	raw[8] = link->action;
	raw[9] = link->provenance;
	raw[10] = link->min_speed;
	raw[11] = link->heading;
	raw[12] = link->heading_slack;
	raw[13] = link->exit_speed;
	Wire_PutI16(raw + 14, link->cost_ms);
	Wire_PutFloat(raw + 16, link->suffix_anchor[0]);
	Wire_PutFloat(raw + 20, link->suffix_anchor[1]);
	Wire_PutFloat(raw + 24, link->suffix_anchor[2]);
	Wire_PutFloat(raw + 28, link->mechanism_anchor[0]);
	Wire_PutFloat(raw + 32, link->mechanism_anchor[1]);
	Wire_PutFloat(raw + 36, link->mechanism_anchor[2]);
	Wire_PutU16(raw + 40, link->sweep_clear_ms);
	raw[42] = link->mode;
	raw[43] = link->reserved;
	memcpy(encoded, raw, sizeof(raw));
	return RLW_OK;
}

rune_wire_diagnostic_t SG_RuneV3DecodeLink(const unsigned char *encoded,
	size_t encoded_size, sg_rune_v3_link_t *link_out)
{
	sg_rune_v3_link_t link;
	rune_wire_diagnostic_t diagnostic;

	if (!encoded || !link_out)
		return RLW_INVALID_ARGUMENT;
	if (encoded_size != SG_RUNE_V3_LINK_BYTES)
		return RLW_BAD_LINK_SIZE;
	memset(&link, 0, sizeof(link));
	link.source = Wire_GetU32(encoded + 0);
	link.destination = Wire_GetU32(encoded + 4);
	link.action = encoded[8];
	link.provenance = encoded[9];
	link.min_speed = encoded[10];
	link.heading = encoded[11];
	link.heading_slack = encoded[12];
	link.exit_speed = encoded[13];
	link.cost_ms = Wire_GetI16(encoded + 14);
	link.suffix_anchor[0] = Wire_GetFloat(encoded + 16);
	link.suffix_anchor[1] = Wire_GetFloat(encoded + 20);
	link.suffix_anchor[2] = Wire_GetFloat(encoded + 24);
	link.mechanism_anchor[0] = Wire_GetFloat(encoded + 28);
	link.mechanism_anchor[1] = Wire_GetFloat(encoded + 32);
	link.mechanism_anchor[2] = Wire_GetFloat(encoded + 36);
	link.sweep_clear_ms = Wire_GetU16(encoded + 40);
	link.mode = encoded[42];
	link.reserved = encoded[43];
	diagnostic = Wire_ValidateLinkFields(&link);
	if (diagnostic != RLW_OK)
		return diagnostic;
	*link_out = link;
	return RLW_OK;
}

static void Wire_DecodeLinkFields(const unsigned char *encoded,
	sg_rune_v3_link_t *link)
{
	memset(link, 0, sizeof(*link));
	link->source = Wire_GetU32(encoded + 0);
	link->destination = Wire_GetU32(encoded + 4);
	link->action = encoded[8];
	link->provenance = encoded[9];
	link->min_speed = encoded[10];
	link->heading = encoded[11];
	link->heading_slack = encoded[12];
	link->exit_speed = encoded[13];
	link->cost_ms = Wire_GetI16(encoded + 14);
	link->suffix_anchor[0] = Wire_GetFloat(encoded + 16);
	link->suffix_anchor[1] = Wire_GetFloat(encoded + 20);
	link->suffix_anchor[2] = Wire_GetFloat(encoded + 24);
	link->mechanism_anchor[0] = Wire_GetFloat(encoded + 28);
	link->mechanism_anchor[1] = Wire_GetFloat(encoded + 32);
	link->mechanism_anchor[2] = Wire_GetFloat(encoded + 36);
	link->sweep_clear_ms = Wire_GetU16(encoded + 40);
	link->mode = encoded[42];
	link->reserved = encoded[43];
}

static void Wire_SiftKeys(uint64_t *keys, size_t root, size_t end)
{
	for (;;)
	{
		size_t child;
		size_t swap_index = root;
		uint64_t temporary;

		if (root > (SIZE_MAX - 1U) / 2U)
			return;
		child = root * 2U + 1U;
		if (child >= end)
			return;
		if (keys[swap_index] < keys[child])
			swap_index = child;
		if (child + 1U < end && keys[swap_index] < keys[child + 1U])
			swap_index = child + 1U;
		if (swap_index == root)
			return;
		temporary = keys[root];
		keys[root] = keys[swap_index];
		keys[swap_index] = temporary;
		root = swap_index;
	}
}

static void Wire_SortKeys(uint64_t *keys, size_t count)
{
	size_t start;
	size_t end;

	if (count < 2)
		return;
	for (start = count / 2U; start > 0; start--)
		Wire_SiftKeys(keys, start - 1U, count);
	for (end = count; end > 1; end--)
	{
		uint64_t temporary = keys[0];

		keys[0] = keys[end - 1U];
		keys[end - 1U] = temporary;
		Wire_SiftKeys(keys, 0, end - 1U);
	}
}

rune_wire_diagnostic_t SG_RuneV3ValidateGraph(
	const sg_rune_v3_seed_t *seeds, uint32_t num_seeds,
	const sg_rune_v3_link_t *links, uint32_t num_links,
	sg_rune_v3_workspace_t *workspace)
{
	uint32_t i;

	if (num_seeds == 0 || num_seeds > SG_RUNE_V3_MAX_SEEDS ||
	    num_links > SG_RUNE_V3_MAX_LINKS)
		return RLW_BAD_COUNTS;
	if (!seeds || (num_links != 0 && !links) || !workspace)
		return RLW_INVALID_ARGUMENT;
	if (!workspace->source_marks ||
	    workspace->source_mark_capacity < (size_t)num_seeds ||
	    (num_links != 0 &&
	     (!workspace->link_keys ||
	      workspace->link_key_capacity < (size_t)num_links)))
		return RLW_ALLOCATION_FAILED;
	memset(workspace->source_marks, 0, (size_t)num_seeds);
	for (i = 0; i < num_seeds; i++)
		if (Wire_ValidateSeed(&seeds[i]) != RLW_OK)
			return RLW_BAD_SEED_RECORD;

	for (i = 0; i < num_links; i++)
	{
		const sg_rune_v3_link_t *link = &links[i];

		if (link->source >= num_seeds || link->destination >= num_seeds ||
		    link->source == link->destination)
			return RLW_BAD_LINK_RECORD;
		if (!SG_RuneV3ActionWireKnown(link->action))
			return RLW_BAD_LINK_RECORD;
		workspace->link_keys[i] = ((uint64_t)link->source << 23) |
			((uint64_t)link->destination << 8) | (uint64_t)link->action;
	}
	Wire_SortKeys(workspace->link_keys, (size_t)num_links);
	for (i = 1; i < num_links; i++)
		if (workspace->link_keys[i - 1U] == workspace->link_keys[i])
			return RLW_DUPLICATE_LINK;

	for (i = 0; i < num_links; i++)
	{
		const sg_rune_v3_link_t *link = &links[i];
		int from_water;
		int to_water;
		rune_wire_diagnostic_t diagnostic = Wire_ValidateLinkFields(link);

		if (diagnostic != RLW_OK)
			return diagnostic;
		if (((uint16_t)seeds[link->source].flags &
		     SG_RUNE_V3_SEED_TOMBSTONE) != 0 ||
		    ((uint16_t)seeds[link->destination].flags &
		     SG_RUNE_V3_SEED_TOMBSTONE) != 0)
			return RLW_BAD_ROUTE_OWNERSHIP;
		workspace->source_marks[link->source] = 1;
		from_water = ((uint16_t)seeds[link->source].flags &
			SG_RUNE_V3_SEED_WATER) != 0;
		to_water = ((uint16_t)seeds[link->destination].flags &
			SG_RUNE_V3_SEED_WATER) != 0;
		if (!SG_ActionEndpointAllowed((int)link->action,
		    from_water, to_water))
			return RLW_BAD_LINK_RECORD;
	}
	for (i = 0; i < num_seeds; i++)
	{
		int tombstone = ((uint16_t)seeds[i].flags &
			SG_RUNE_V3_SEED_TOMBSTONE) != 0;
		int has_outgoing = workspace->source_marks[i] != 0;

		if (tombstone == has_outgoing)
			return RLW_BAD_ROUTE_OWNERSHIP;
	}
	return RLW_OK;
}

rune_wire_diagnostic_t SG_RuneV3MatchIdentity(
	const sg_rune_v3_header_t *header,
	const sg_rune_v3_identity_t *expected_identity)
{
	rune_wire_diagnostic_t diagnostic;

	if (!header)
		return RLW_INVALID_ARGUMENT;
	diagnostic = Wire_ValidateHeaderFixed(header);
	if (diagnostic != RLW_OK)
		return diagnostic;
	diagnostic = Wire_ValidateHeaderSemantic(header);
	if (diagnostic != RLW_OK)
		return diagnostic;
	if (!expected_identity)
		return RLW_OK;
	diagnostic = Wire_ValidateIdentity(expected_identity);
	if (diagnostic != RLW_OK)
		return diagnostic;
	if (memcmp(header->map_name, expected_identity->map_name,
	    SG_RUNE_V3_MAP_NAME_BYTES) != 0)
		return RLW_MAPNAME_MISMATCH;
	if (header->bsp_checksum != expected_identity->bsp_checksum)
		return RLW_BSP_CHECKSUM_MISMATCH;
	if (header->entity_crc32 != expected_identity->entity_crc32)
		return RLW_ENTITY_CRC_MISMATCH;
	if (header->host_physics_id != expected_identity->host_physics_id)
		return RLW_PHYSICS_ID_MISMATCH;
	if (header->physics_flags != expected_identity->physics_flags ||
	    Wire_FloatBits(header->gravity) !=
	        Wire_FloatBits(expected_identity->gravity) ||
	    Wire_FloatBits(header->airaccelerate) !=
	        Wire_FloatBits(expected_identity->airaccelerate) ||
	    Wire_FloatBits(header->maxvelocity) !=
	        Wire_FloatBits(expected_identity->maxvelocity) ||
	    header->pmove_substep_ms != expected_identity->pmove_substep_ms ||
	    header->server_frame_ms != expected_identity->server_frame_ms)
		return RLW_BAD_PHYSICS_LAW;
	return RLW_OK;
}

rune_wire_diagnostic_t SG_RuneV3Encode(
	const sg_rune_v3_identity_t *identity,
	const sg_rune_v3_seed_t *seeds, uint32_t num_seeds,
	const sg_rune_v3_link_t *links, uint32_t num_links,
	sg_rune_v3_workspace_t *workspace, unsigned char *encoded,
	size_t encoded_capacity, size_t *encoded_size_out)
{
	sg_rune_v3_header_t header;
	size_t encoded_size;
	size_t offset;
	uint32_t crc_state;
	uint32_t payload_crc;
	uint32_t i;
	rune_wire_diagnostic_t diagnostic;

	if (!encoded_size_out)
		return RLW_INVALID_ARGUMENT;
	*encoded_size_out = 0;
	diagnostic = Wire_ValidateIdentity(identity);
	if (diagnostic != RLW_OK)
		return diagnostic;
	diagnostic = SG_RuneV3FileSize(num_seeds, num_links, &encoded_size);
	if (diagnostic != RLW_OK)
		return diagnostic;
	if (!encoded)
		return RLW_INVALID_ARGUMENT;
	if (encoded_capacity < encoded_size)
		return RLW_BAD_FILE_SIZE;
	diagnostic = SG_RuneV3ValidateGraph(seeds, num_seeds, links,
		num_links, workspace);
	if (diagnostic != RLW_OK)
		return diagnostic;
	diagnostic = SG_RuneV3PayloadCRCInit(&crc_state);
	if (diagnostic != RLW_OK)
		return diagnostic;
	offset = SG_RUNE_V3_HEADER_BYTES;
	for (i = 0; i < num_seeds; i++)
	{
		diagnostic = SG_RuneV3EncodeSeed(&seeds[i], encoded + offset,
			SG_RUNE_V3_SEED_BYTES);
		if (diagnostic != RLW_OK)
			return diagnostic;
		diagnostic = SG_RuneV3PayloadCRCUpdate(&crc_state,
			encoded + offset, SG_RUNE_V3_SEED_BYTES);
		if (diagnostic != RLW_OK)
			return diagnostic;
		offset += SG_RUNE_V3_SEED_BYTES;
	}
	for (i = 0; i < num_links; i++)
	{
		diagnostic = SG_RuneV3EncodeLink(&links[i], encoded + offset,
			SG_RUNE_V3_LINK_BYTES);
		if (diagnostic != RLW_OK)
			return diagnostic;
		diagnostic = SG_RuneV3PayloadCRCUpdate(&crc_state,
			encoded + offset, SG_RUNE_V3_LINK_BYTES);
		if (diagnostic != RLW_OK)
			return diagnostic;
		offset += SG_RUNE_V3_LINK_BYTES;
	}
	diagnostic = SG_RuneV3PayloadCRCFinish(crc_state, &payload_crc);
	if (diagnostic != RLW_OK)
		return diagnostic;
	memset(&header, 0, sizeof(header));
	header.magic = SG_RUNE_V3_MAGIC;
	header.version = SG_RUNE_V3_VERSION;
	header.header_bytes = SG_RUNE_V3_HEADER_BYTES;
	header.seed_bytes = SG_RUNE_V3_SEED_BYTES;
	header.link_bytes = SG_RUNE_V3_LINK_BYTES;
	header.num_seeds = num_seeds;
	header.num_links = num_links;
	header.payload_crc32 = payload_crc;
	header.bsp_checksum = identity->bsp_checksum;
	header.entity_crc32 = identity->entity_crc32;
	header.action_contract_crc32 = SG_ACTION_CONTRACT_CRC32;
	header.physics_flags = identity->physics_flags;
	header.gravity = identity->gravity;
	header.airaccelerate = identity->airaccelerate;
	header.maxvelocity = identity->maxvelocity;
	header.pmove_substep_ms = identity->pmove_substep_ms;
	header.server_frame_ms = identity->server_frame_ms;
	header.host_physics_id = identity->host_physics_id;
	memcpy(header.map_name, identity->map_name, SG_RUNE_V3_MAP_NAME_BYTES);
	diagnostic = SG_RuneV3EncodeHeader(&header, encoded,
		SG_RUNE_V3_HEADER_BYTES);
	if (diagnostic != RLW_OK)
		return diagnostic;
	*encoded_size_out = encoded_size;
	return RLW_OK;
}

rune_wire_diagnostic_t SG_RuneV3Decode(
	const unsigned char *encoded, size_t encoded_size,
	const sg_rune_v3_identity_t *expected_identity,
	sg_rune_v3_header_t *header_out,
	sg_rune_v3_seed_t *seeds, size_t seed_capacity,
	sg_rune_v3_link_t *links, size_t link_capacity,
	sg_rune_v3_workspace_t *workspace)
{
	sg_rune_v3_header_t header;
	size_t expected_size;
	size_t offset;
	uint32_t computed_payload_crc;
	uint32_t i;
	rune_wire_diagnostic_t diagnostic;

	if (!encoded || !header_out)
		return RLW_INVALID_ARGUMENT;
	if (encoded_size < SG_RUNE_V3_HEADER_BYTES)
		return RLW_BAD_FILE_SIZE;
	diagnostic = Wire_DecodeHeaderPrefix(encoded,
		SG_RUNE_V3_HEADER_BYTES, &header);
	if (diagnostic != RLW_OK)
		return diagnostic;
	diagnostic = SG_RuneV3FileSize(header.num_seeds, header.num_links,
		&expected_size);
	if (diagnostic != RLW_OK)
		return diagnostic;
	if (encoded_size != expected_size)
		return RLW_BAD_FILE_SIZE;
	diagnostic = Wire_ValidateHeaderSemantic(&header);
	if (diagnostic != RLW_OK)
		return diagnostic;
	if (!SG_CRC32Buffer(encoded + SG_RUNE_V3_HEADER_BYTES,
	    encoded_size - SG_RUNE_V3_HEADER_BYTES, &computed_payload_crc))
		return RLW_INVALID_ARGUMENT;
	if (computed_payload_crc != header.payload_crc32)
		return RLW_BAD_PAYLOAD_CRC;
	if (!seeds || seed_capacity < (size_t)header.num_seeds ||
	    (header.num_links != 0 &&
	     (!links || link_capacity < (size_t)header.num_links)))
		return RLW_ALLOCATION_FAILED;
	offset = SG_RUNE_V3_HEADER_BYTES;
	for (i = 0; i < header.num_seeds; i++)
	{
		diagnostic = SG_RuneV3DecodeSeed(encoded + offset,
			SG_RUNE_V3_SEED_BYTES, &seeds[i]);
		if (diagnostic != RLW_OK)
			return diagnostic;
		offset += SG_RUNE_V3_SEED_BYTES;
	}
	for (i = 0; i < header.num_links; i++)
	{
		Wire_DecodeLinkFields(encoded + offset, &links[i]);
		offset += SG_RUNE_V3_LINK_BYTES;
	}
	diagnostic = SG_RuneV3ValidateGraph(seeds, header.num_seeds, links,
		header.num_links, workspace);
	if (diagnostic != RLW_OK)
		return diagnostic;
	diagnostic = SG_RuneV3MatchIdentity(&header, expected_identity);
	if (diagnostic != RLW_OK)
		return diagnostic;
	*header_out = header;
	return RLW_OK;
}
