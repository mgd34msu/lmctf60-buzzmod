/* sg_rune_loader.c -- allocation-free authenticated v3-to-native adapter. */
#include "q_shared.h"
#include "slipgate/sg_rune_loader.h"

#include "slipgate/sg_action.h"

#include <limits.h>
#include <math.h>
#include <string.h>

#define LOADER_LINK_INDEX_BITS 18U
#define LOADER_LINK_INDEX_MASK \
	((UINT64_C(1) << LOADER_LINK_INDEX_BITS) - UINT64_C(1))

_Static_assert(SG_RUNE_V3_MAX_LINKS ==
	(UINT32_C(1) << LOADER_LINK_INDEX_BITS),
	"loader duplicate-key index packing drift");
_Static_assert(SG_RUNE_V3_MAX_SEEDS <= (uint32_t)INT_MAX,
	"native seed index cannot represent v3 maximum");
_Static_assert(RSF_WATER == SG_RUNE_V3_SEED_WATER,
	"native/v3 water flag drift");
_Static_assert(RSF_TOMBSTONE == SG_RUNE_V3_SEED_TOMBSTONE,
	"native/v3 tombstone flag drift");

static sg_rune_load_result_t Loader_Result(rune_wire_diagnostic_t diagnostic,
	rune_reject_reason_t reason, sg_rune_load_stage_t stage, uint32_t index,
	sg_rune_snapshot_kind_t kind)
{
	sg_rune_load_result_t result;

	memset(&result, 0, sizeof(result));
	result.diagnostic = diagnostic;
	result.reason = reason;
	result.stage = stage;
	result.index = index;
	result.snapshot_kind = kind;
	return result;
}

static uint16_t Loader_GetU16(const unsigned char *data)
{
	return (uint16_t)((uint16_t)data[0] |
		(uint16_t)((uint16_t)data[1] << 8));
}

static uint32_t Loader_GetU32(const unsigned char *data)
{
	return (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
	       ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
}

static int16_t Loader_GetI16(const unsigned char *data)
{
	uint16_t value = Loader_GetU16(data);
	int32_t converted = value <= INT16_MAX
		? (int32_t)value : (int32_t)value - INT32_C(65536);

	return (int16_t)converted;
}

static uint32_t Loader_FloatBits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static int Loader_VectorFinite(const float vector[3])
{
	return vector && isfinite(vector[0]) && isfinite(vector[1]) &&
	       isfinite(vector[2]);
}

static int Loader_VectorInWorld(const float vector[3])
{
	static const float minimum =
		(float)SG_RUNE_PROOF_WORLD_FIXED_MIN /
		(float)SG_RUNE_PROOF_WORLD_FIXED_SCALE;
	static const float maximum =
		(float)SG_RUNE_PROOF_WORLD_FIXED_MAX /
		(float)SG_RUNE_PROOF_WORLD_FIXED_SCALE;
	int axis;

	if (!Loader_VectorFinite(vector))
		return 0;
	for (axis = 0; axis < 3; axis++)
		if (vector[axis] < minimum || vector[axis] > maximum)
			return 0;
	return 1;
}

static int Loader_VectorPositiveZero(const float vector[3])
{
	return vector && Loader_FloatBits(vector[0]) == UINT32_C(0) &&
	       Loader_FloatBits(vector[1]) == UINT32_C(0) &&
	       Loader_FloatBits(vector[2]) == UINT32_C(0);
}

static int Loader_VectorOnDoorLattice(const float vector[3])
{
	int axis;

	if (!Loader_VectorInWorld(vector))
		return 0;
	for (axis = 0; axis < 3; axis++)
	{
		float scaled = vector[axis] *
			(float)SG_RUNE_PROOF_DOOR_ANCHOR_SCALE;

		if (scaled != (float)(int)scaled)
			return 0;
	}
	return 1;
}

static int Loader_HookAngleCanonical(float angle)
{
	int units;
	float canonical;

	if (!isfinite(angle) || angle < -180.0f || angle >= 180.0f)
		return 0;
	units = ((int)(angle *
		(float)SG_RUNE_PROOF_ANGLE_SHORT_UNITS /
		(float)SG_RUNE_PROOF_FULL_TURN_DEGREES)) & 65535;
	if (units >= 32768)
		units -= 65536;
	canonical = (float)((double)units *
		((double)SG_RUNE_PROOF_FULL_TURN_DEGREES /
		 (double)SG_RUNE_PROOF_ANGLE_SHORT_UNITS));
	return angle == canonical;
}

static rune_reject_reason_t Loader_ValidateDeclared(
	const sg_rune_v3_link_t *link)
{
	if (!Loader_VectorInWorld(link->suffix_anchor) ||
	    link->provenance != RL_DECLARED || link->min_speed != 0 ||
	    link->heading != 0 ||
	    link->heading_slack != SG_RUNE_PROOF_DECLARED_CONTROL_MARKER ||
	    link->exit_speed != 0)
		return RLR_BAD_DECLARED_CONTROL;
	return RLR_OK;
}

static rune_reject_reason_t Loader_ValidateDrop(
	const sg_rune_v3_seed_t *from, const sg_rune_v3_link_t *link)
{
	float dx = link->suffix_anchor[0] - from->origin[0];
	float dy = link->suffix_anchor[1] - from->origin[1];
	float dz = link->suffix_anchor[2] - from->origin[2];
	float horizontal = sqrtf(dx * dx + dy * dy);
	float lip_yaw = atan2f(dy, dx) *
		(float)SG_RUNE_PROOF_FULL_TURN_DEGREES / (float)(2.0 * M_PI);
	float stored_yaw = (float)link->heading *
		(float)SG_RUNE_PROOF_FULL_TURN_DEGREES /
		(float)SG_RUNE_PROOF_ANGLE_BYTE_UNITS;
	float yaw_delta = lip_yaw - stored_yaw;

	while (yaw_delta > 180.0f)
		yaw_delta -= 360.0f;
	while (yaw_delta < -180.0f)
		yaw_delta += 360.0f;
	if (link->provenance != RL_PROVEN ||
	    !Loader_VectorInWorld(link->suffix_anchor) ||
	    ((uint16_t)from->flags & SG_RUNE_V3_SEED_WATER) != 0 ||
	    link->min_speed != 0 ||
	    link->heading_slack != SG_RUNE_PROOF_DROP_CONTROL_MARKER ||
	    horizontal < (float)SG_RUNE_PROOF_DROP_LIP_HORIZONTAL_MIN ||
	    horizontal > (float)SG_RUNE_PROOF_DROP_LIP_HORIZONTAL_MAX ||
	    fabsf(dz -
	        (float)SG_RUNE_PROOF_DROP_LIP_Z_FIXED /
	        (float)SG_RUNE_PROOF_WORLD_FIXED_SCALE) >
	        (float)SG_RUNE_PROOF_DROP_LIP_Z_TOLERANCE_FIXED /
	        (float)SG_RUNE_PROOF_WORLD_FIXED_SCALE ||
	    fabsf(yaw_delta) >
	        (float)SG_RUNE_PROOF_FULL_TURN_DEGREES /
	        (float)SG_RUNE_PROOF_ANGLE_BYTE_UNITS)
		return RLR_BAD_DROP_CONTROL;
	return RLR_OK;
}

static rune_reject_reason_t Loader_ValidateHook(
	const sg_rune_v3_seed_t *from, const sg_rune_v3_seed_t *to,
	const sg_rune_v3_link_t *link)
{
	int from_water = ((uint16_t)from->flags &
		SG_RUNE_V3_SEED_WATER) != 0;
	int to_water = ((uint16_t)to->flags &
		SG_RUNE_V3_SEED_WATER) != 0;
	int expected_slack = from_water
		? SG_RUNE_PROOF_WATER_HOOK_CONTROL_MARKER
		: SG_RUNE_PROOF_HOOK_CONTROL_SLACK;

	if (link->provenance != RL_PROVEN || link->min_speed != 0 ||
	    (from_water && to_water) || link->heading_slack != expected_slack ||
	    !Loader_HookAngleCanonical(link->suffix_anchor[PITCH]) ||
	    !Loader_HookAngleCanonical(link->suffix_anchor[YAW]) ||
	    link->suffix_anchor[PITCH] <
	        -(float)SG_RUNE_PROOF_HOOK_MAX_ABS_PITCH_DEGREES ||
	    link->suffix_anchor[PITCH] >
	        (float)SG_RUNE_PROOF_HOOK_MAX_ABS_PITCH_DEGREES ||
	    link->suffix_anchor[ROLL] < (float)SG_RUNE_PROOF_HOOK_MIN_RAY ||
	    link->suffix_anchor[ROLL] > (float)SG_RUNE_PROOF_HOOK_MAX_RAY)
		return RLR_BAD_HOOK_CONTROL;
	return RLR_OK;
}

static rune_reject_reason_t Loader_ValidateTeleport(
	const sg_rune_v3_seed_t *from, const sg_rune_v3_link_t *link)
{
	float dx = link->suffix_anchor[0] - from->origin[0];
	float dy = link->suffix_anchor[1] - from->origin[1];
	float dz = link->suffix_anchor[2] - from->origin[2];

	if (sqrtf(dx * dx + dy * dy) >
	        (float)SG_RUNE_PROOF_TELEPORT_SEED_REACH ||
	    fabsf(dz) > (float)SG_RUNE_PROOF_TELEPORT_SEED_REACH)
		return RLR_BAD_TELEPORT_REACH;
	return RLR_OK;
}

static rune_reject_reason_t Loader_ValidateDoor(
	const sg_rune_v3_seed_t *from, const sg_rune_v3_seed_t *to,
	const sg_rune_v3_link_t *link)
{
	float approach_x = link->suffix_anchor[0] - from->origin[0];
	float approach_y = link->suffix_anchor[1] - from->origin[1];
	float approach_z = link->suffix_anchor[2] - from->origin[2];
	float egress_x = to->origin[0] - link->suffix_anchor[0];
	float egress_y = to->origin[1] - link->suffix_anchor[1];
	float egress_z = to->origin[2] - link->suffix_anchor[2];

	if (!Loader_VectorOnDoorLattice(link->suffix_anchor))
		return RLR_BAD_ANCHOR_POLICY;
	if (approach_x * approach_x + approach_y * approach_y >
	        (float)SG_RUNE_PROOF_DOOR_APPROACH_HORIZONTAL_MAX *
	        (float)SG_RUNE_PROOF_DOOR_APPROACH_HORIZONTAL_MAX ||
	    fabsf(approach_z) >
	        (float)SG_RUNE_PROOF_DOOR_APPROACH_VERTICAL_MAX ||
	    egress_x * egress_x + egress_y * egress_y >
	        (float)SG_RUNE_PROOF_DOOR_EGRESS_HORIZONTAL_MAX *
	        (float)SG_RUNE_PROOF_DOOR_EGRESS_HORIZONTAL_MAX ||
	    fabsf(egress_z) >
	        (float)SG_RUNE_PROOF_DOOR_EGRESS_VERTICAL_MAX)
		return RLR_BAD_DOOR_REACH;
	return RLR_OK;
}

rune_reject_reason_t SG_RuneV3ValidateLiteralLink(
	const sg_rune_v3_seed_t *seeds, uint32_t num_seeds,
	const sg_rune_v3_link_t *link)
{
	const sg_rune_v3_seed_t *from;
	const sg_rune_v3_seed_t *to;
	rune_reject_reason_t reason;
	int from_water;
	int to_water;

	if (!seeds || !link || num_seeds == 0 ||
	    num_seeds > SG_RUNE_V3_MAX_SEEDS)
		return RLR_BAD_CONTROL_POLICY;
	if (!SG_RuneV3ActionWireKnown(link->action))
		return RLR_UNKNOWN_ACTION;
	switch ((int)link->action)
	{
	case RL_RUN:
	case RL_JUMP:
	case RL_DROP:
	case RL_HOOK:
	case RL_SWIM:
	case RL_LIFT:
	case RL_TELEPORT:
	case RL_DOOR:
		break;
	default:
		return RLR_ACTION_DISABLED;
	}
	if (!SG_RuneV3ActionRuntimeSupported(link->action))
		return RLR_ACTION_DISABLED;
	if (!SG_ProvenanceWireValid(SG_RUNE_WIRE_V3,
	    (int)link->provenance))
		return RLR_UNKNOWN_PROVENANCE;
	if (!SG_ActionAllowsProvenance((int)link->action,
	    (int)link->provenance))
		return RLR_PROVENANCE_FORBIDDEN;
	if (link->source >= num_seeds || link->destination >= num_seeds)
		return RLR_BAD_INDEX;
	if (link->source == link->destination)
		return RLR_SELF_LINK;
	from = &seeds[link->source];
	to = &seeds[link->destination];
	if (((uint16_t)from->flags & SG_RUNE_V3_SEED_TOMBSTONE) != 0 ||
	    ((uint16_t)to->flags & SG_RUNE_V3_SEED_TOMBSTONE) != 0)
		return RLR_TOMBSTONE_ENDPOINT;
	if (link->cost_ms < SG_RUNE_V3_MIN_COST_MS ||
	    link->cost_ms > SG_RUNE_V3_MAX_COST_MS ||
	    (link->action == RL_DROP &&
	     (link->cost_ms < SG_RUNE_PROOF_SERVER_FRAME_MS ||
	      link->cost_ms >= SG_RUNE_PROOF_DROP_TOTAL_MS ||
	      link->cost_ms % SG_RUNE_PROOF_SERVER_FRAME_MS != 0)))
		return RLR_BAD_COST;
	if (!Loader_VectorFinite(link->suffix_anchor) ||
	    !Loader_VectorFinite(link->mechanism_anchor))
		return RLR_NONFINITE_ANCHOR;
	if (link->reserved != 0)
		return RLR_NONZERO_RESERVED;
	if (link->mode != RLCM_NONE)
		return RLR_BAD_MODE;
	if (!Loader_VectorPositiveZero(link->mechanism_anchor) ||
	    link->sweep_clear_ms != 0)
		return RLR_NONZERO_TAIL;
	from_water = ((uint16_t)from->flags & SG_RUNE_V3_SEED_WATER) != 0;
	to_water = ((uint16_t)to->flags & SG_RUNE_V3_SEED_WATER) != 0;
	if (!SG_ActionEndpointAllowed((int)link->action,
	    from_water, to_water))
		return RLR_BAD_ENDPOINT_POLICY;

	switch ((int)link->action)
	{
	case RL_RUN:
		return Loader_VectorInWorld(link->suffix_anchor)
			? RLR_OK : RLR_BAD_RUN_CONTROL;
	case RL_JUMP:
		return link->min_speed == 0 &&
		       Loader_VectorPositiveZero(link->suffix_anchor)
			? RLR_OK : RLR_BAD_JUMP_CONTROL;
	case RL_DROP:
		return Loader_ValidateDrop(from, link);
	case RL_HOOK:
		return Loader_ValidateHook(from, to, link);
	case RL_SWIM:
		return link->provenance == RL_PROVEN &&
		       link->min_speed == 0 && link->heading == 0 &&
		       link->heading_slack == 0 &&
		       Loader_VectorPositiveZero(link->suffix_anchor)
			? RLR_OK : RLR_BAD_SWIM_CONTROL;
	case RL_LIFT:
		return Loader_ValidateDeclared(link);
	case RL_TELEPORT:
		reason = Loader_ValidateDeclared(link);
		return reason != RLR_OK ? reason :
			Loader_ValidateTeleport(from, link);
	case RL_DOOR:
		reason = Loader_ValidateDeclared(link);
		return reason != RLR_OK ? reason :
			Loader_ValidateDoor(from, to, link);
	default:
		return RLR_ACTION_DISABLED;
	}
}

sg_rune_snapshot_kind_t SG_RuneV3Probe(const unsigned char *snapshot,
	size_t snapshot_size)
{
	if (!snapshot || snapshot_size < 8U ||
	    Loader_GetU32(snapshot) != SG_RUNE_V3_MAGIC)
		return SG_RUNE_SNAPSHOT_UNKNOWN;
	if (Loader_GetU32(snapshot + 4) == (uint32_t)RUNE_VERSION)
		return SG_RUNE_SNAPSHOT_V2;
	if (Loader_GetU16(snapshot + 4) == SG_RUNE_V3_VERSION &&
	    Loader_GetU16(snapshot + 6) == SG_RUNE_V3_HEADER_BYTES)
		return SG_RUNE_SNAPSHOT_V3;
	return SG_RUNE_SNAPSHOT_UNKNOWN;
}

sg_rune_load_result_t SG_RuneV3InspectHeader(
	const unsigned char *encoded_header, size_t encoded_header_size,
	const sg_rune_v3_identity_t *expected,
	sg_rune_v3_header_t *header_out)
{
	sg_rune_snapshot_kind_t kind = SG_RuneV3Probe(encoded_header,
		encoded_header_size);
	sg_rune_load_result_t result = Loader_Result(RLW_OK, RLR_OK,
		SG_RUNE_LOAD_STAGE_ARGUMENT, SG_RUNE_LOAD_INDEX_NONE, kind);
	sg_rune_v3_header_t header;
	rune_wire_diagnostic_t diagnostic;

	if (!encoded_header || !expected || !header_out)
	{
		result.diagnostic = RLW_INVALID_ARGUMENT;
		return result;
	}
	if (kind == SG_RUNE_SNAPSHOT_V2)
	{
		result.diagnostic = RLW_UNSUPPORTED_VERSION;
		result.stage = SG_RUNE_LOAD_STAGE_HEADER;
		return result;
	}
	if (encoded_header_size != SG_RUNE_V3_HEADER_BYTES)
	{
		result.diagnostic = RLW_BAD_HEADER_SIZE;
		result.stage = SG_RUNE_LOAD_STAGE_HEADER;
		return result;
	}
	diagnostic = SG_RuneV3DecodeHeader(encoded_header,
		encoded_header_size, &header);
	if (diagnostic != RLW_OK)
	{
		result.diagnostic = diagnostic;
		result.stage = SG_RUNE_LOAD_STAGE_HEADER;
		return result;
	}
	diagnostic = SG_RuneV3FileSize(header.num_seeds, header.num_links,
		&result.file_size);
	if (diagnostic != RLW_OK)
	{
		result.diagnostic = diagnostic;
		result.stage = SG_RUNE_LOAD_STAGE_FILE_SIZE;
		return result;
	}
	diagnostic = SG_RuneV3MatchIdentity(&header, expected);
	if (diagnostic != RLW_OK)
	{
		result.diagnostic = diagnostic;
		result.stage = SG_RUNE_LOAD_STAGE_IDENTITY;
		return result;
	}
	*header_out = header;
	result.stage = SG_RUNE_LOAD_STAGE_DONE;
	return result;
}

sg_rune_load_result_t SG_RuneV3Inspect(const unsigned char *snapshot,
	size_t snapshot_size, const sg_rune_v3_identity_t *expected,
	sg_rune_v3_header_t *header_out)
{
	sg_rune_snapshot_kind_t kind = SG_RuneV3Probe(snapshot, snapshot_size);
	sg_rune_load_result_t result = Loader_Result(RLW_OK, RLR_OK,
		SG_RUNE_LOAD_STAGE_ARGUMENT, SG_RUNE_LOAD_INDEX_NONE, kind);
	sg_rune_v3_header_t header;
	uint32_t crc_state;
	uint32_t payload_crc;
	rune_wire_diagnostic_t diagnostic;

	if (!snapshot || !expected || !header_out)
	{
		result.diagnostic = RLW_INVALID_ARGUMENT;
		return result;
	}
	if (kind == SG_RUNE_SNAPSHOT_V2)
	{
		result.diagnostic = RLW_UNSUPPORTED_VERSION;
		result.stage = SG_RUNE_LOAD_STAGE_HEADER;
		return result;
	}
	if (snapshot_size < SG_RUNE_V3_HEADER_BYTES)
	{
		result.diagnostic = RLW_BAD_FILE_SIZE;
		result.stage = SG_RUNE_LOAD_STAGE_FILE_SIZE;
		return result;
	}
	diagnostic = SG_RuneV3DecodeHeader(snapshot,
		SG_RUNE_V3_HEADER_BYTES, &header);
	if (diagnostic != RLW_OK)
	{
		result.diagnostic = diagnostic;
		result.stage = SG_RUNE_LOAD_STAGE_HEADER;
		return result;
	}
	diagnostic = SG_RuneV3FileSize(header.num_seeds, header.num_links,
		&result.file_size);
	if (diagnostic != RLW_OK || snapshot_size != result.file_size)
	{
		result.diagnostic = diagnostic != RLW_OK
			? diagnostic : RLW_BAD_FILE_SIZE;
		result.stage = SG_RUNE_LOAD_STAGE_FILE_SIZE;
		return result;
	}
	diagnostic = SG_RuneV3PayloadCRCInit(&crc_state);
	if (diagnostic == RLW_OK)
		diagnostic = SG_RuneV3PayloadCRCUpdate(&crc_state,
			snapshot + SG_RUNE_V3_HEADER_BYTES,
			snapshot_size - SG_RUNE_V3_HEADER_BYTES);
	if (diagnostic == RLW_OK)
		diagnostic = SG_RuneV3PayloadCRCFinish(crc_state, &payload_crc);
	if (diagnostic != RLW_OK)
	{
		result.diagnostic = diagnostic;
		result.stage = SG_RUNE_LOAD_STAGE_PAYLOAD_CRC;
		return result;
	}
	if (payload_crc != header.payload_crc32)
	{
		result.diagnostic = RLW_BAD_PAYLOAD_CRC;
		result.stage = SG_RUNE_LOAD_STAGE_PAYLOAD_CRC;
		return result;
	}
	diagnostic = SG_RuneV3MatchIdentity(&header, expected);
	if (diagnostic != RLW_OK)
	{
		result.diagnostic = diagnostic;
		result.stage = SG_RUNE_LOAD_STAGE_IDENTITY;
		return result;
	}
	*header_out = header;
	result.stage = SG_RUNE_LOAD_STAGE_DONE;
	result.index = SG_RUNE_LOAD_INDEX_NONE;
	return result;
}

static void Loader_SiftKeys(uint64_t *keys, size_t root, size_t end)
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

static void Loader_SortKeys(uint64_t *keys, size_t count)
{
	size_t start;
	size_t end;

	if (count < 2U)
		return;
	for (start = count / 2U; start > 0; start--)
		Loader_SiftKeys(keys, start - 1U, count);
	for (end = count; end > 1U; end--)
	{
		uint64_t temporary = keys[0];

		keys[0] = keys[end - 1U];
		keys[end - 1U] = temporary;
		Loader_SiftKeys(keys, 0, end - 1U);
	}
}

static rune_reject_reason_t Loader_ClassifyRawLink(
	const unsigned char *record, uint32_t num_seeds)
{
	uint32_t source = Loader_GetU32(record);
	uint32_t destination = Loader_GetU32(record + 4);
	uint8_t action = record[8];
	uint8_t provenance = record[9];
	int16_t cost_ms = Loader_GetI16(record + 14);
	size_t byte;

	if (!SG_RuneV3ActionWireKnown(action))
		return RLR_UNKNOWN_ACTION;
	if (action == RL_ROCKETJUMP || action >= RL_DOOR_DROP)
		return RLR_ACTION_DISABLED;
	if (!SG_ProvenanceWireValid(SG_RUNE_WIRE_V3, (int)provenance))
		return RLR_UNKNOWN_PROVENANCE;
	if (!SG_ActionAllowsProvenance((int)action, (int)provenance))
		return RLR_PROVENANCE_FORBIDDEN;
	if (source >= num_seeds || destination >= num_seeds)
		return RLR_BAD_INDEX;
	if (source == destination)
		return RLR_SELF_LINK;
	if (cost_ms < SG_RUNE_V3_MIN_COST_MS ||
	    cost_ms > SG_RUNE_V3_MAX_COST_MS ||
	    (action == RL_DROP &&
	     (cost_ms < SG_RUNE_PROOF_SERVER_FRAME_MS ||
	      cost_ms >= SG_RUNE_PROOF_DROP_TOTAL_MS ||
	      cost_ms % SG_RUNE_PROOF_SERVER_FRAME_MS != 0)))
		return RLR_BAD_COST;
	if (record[43] != 0)
		return RLR_NONZERO_RESERVED;
	if (record[42] != RLCM_NONE)
		return RLR_BAD_MODE;
	for (byte = SG_RUNE_V3_NONCOMPOUND_TAIL_OFFSET;
	     byte < SG_RUNE_V3_NONCOMPOUND_TAIL_OFFSET +
	         SG_RUNE_V3_NONCOMPOUND_TAIL_BYTES; byte++)
		if (record[byte] != 0)
			return RLR_NONZERO_TAIL;
	return RLR_BAD_ANCHOR_POLICY;
}

static void Loader_LocalizeDecodeFailure(sg_rune_load_result_t *result,
	const unsigned char *snapshot, const sg_rune_v3_header_t *header,
	sg_rune_v3_loader_workspace_t *workspace)
{
	uint32_t index;
	size_t offset = SG_RUNE_V3_HEADER_BYTES;

	result->stage = SG_RUNE_LOAD_STAGE_DECODE;
	result->index = SG_RUNE_LOAD_INDEX_NONE;
	result->reason = RLR_OK;
	if (result->diagnostic == RLW_BAD_SEED_RECORD)
	{
		for (index = 0; index < header->num_seeds; index++)
		{
			sg_rune_v3_seed_t seed;

			if (SG_RuneV3DecodeSeed(snapshot + offset,
			    SG_RUNE_V3_SEED_BYTES, &seed) != RLW_OK)
			{
				result->stage = SG_RUNE_LOAD_STAGE_SEED;
				result->index = index;
				return;
			}
			offset += SG_RUNE_V3_SEED_BYTES;
		}
		return;
	}
	offset += (size_t)header->num_seeds * SG_RUNE_V3_SEED_BYTES;
	if (result->diagnostic == RLW_BAD_LINK_RECORD)
	{
		for (index = 0; index < header->num_links; index++)
		{
			sg_rune_v3_link_t link;

			if (SG_RuneV3DecodeLink(snapshot + offset,
			    SG_RUNE_V3_LINK_BYTES, &link) != RLW_OK)
			{
				result->index = index;
				result->reason = Loader_ClassifyRawLink(
					snapshot + offset, header->num_seeds);
				result->stage = result->reason == RLR_UNKNOWN_ACTION ||
						result->reason == RLR_ACTION_DISABLED
					? SG_RUNE_LOAD_STAGE_ACTION
					: SG_RUNE_LOAD_STAGE_LINK;
				return;
			}
			offset += SG_RUNE_V3_LINK_BYTES;
		}
		for (index = 0; index < header->num_links; index++)
		{
			const sg_rune_v3_link_t *link =
				&workspace->wire_links[index];

			if (link->source >= header->num_seeds ||
			    link->destination >= header->num_seeds)
				result->reason = RLR_BAD_INDEX;
			else if (link->source == link->destination)
				result->reason = RLR_SELF_LINK;
			else if (!SG_ActionEndpointAllowed((int)link->action,
			    ((uint16_t)workspace->wire_seeds[link->source].flags &
			        SG_RUNE_V3_SEED_WATER) != 0,
			    ((uint16_t)workspace->wire_seeds[link->destination].flags &
			        SG_RUNE_V3_SEED_WATER) != 0))
				result->reason = RLR_BAD_ENDPOINT_POLICY;
			else
				continue;
			result->stage = SG_RUNE_LOAD_STAGE_LINK;
			result->index = index;
			return;
		}
		return;
	}
	if (result->diagnostic == RLW_DUPLICATE_LINK)
	{
		for (index = 0; index < header->num_links; index++)
		{
			const sg_rune_v3_link_t *link =
				&workspace->wire_links[index];
			uint64_t identity = ((uint64_t)link->source << 23) |
				((uint64_t)link->destination << 8) |
				(uint64_t)link->action;

			workspace->graph.link_keys[index] =
				(identity << LOADER_LINK_INDEX_BITS) | index;
		}
		Loader_SortKeys(workspace->graph.link_keys,
			(size_t)header->num_links);
		for (index = 1; index < header->num_links; index++)
			if ((workspace->graph.link_keys[index - 1U] >>
			     LOADER_LINK_INDEX_BITS) ==
			    (workspace->graph.link_keys[index] >>
			     LOADER_LINK_INDEX_BITS))
			{
				result->stage = SG_RUNE_LOAD_STAGE_LINK;
				result->index = (uint32_t)
					(workspace->graph.link_keys[index] &
					 LOADER_LINK_INDEX_MASK);
				return;
			}
		return;
	}
	if (result->diagnostic == RLW_BAD_ROUTE_OWNERSHIP)
	{
		memset(workspace->graph.source_marks, 0,
			(size_t)header->num_seeds);
		for (index = 0; index < header->num_links; index++)
		{
			const sg_rune_v3_link_t *link =
				&workspace->wire_links[index];

			if (((uint16_t)workspace->wire_seeds[link->source].flags &
			     SG_RUNE_V3_SEED_TOMBSTONE) != 0 ||
			    ((uint16_t)workspace->wire_seeds[link->destination].flags &
			     SG_RUNE_V3_SEED_TOMBSTONE) != 0)
			{
				result->reason = RLR_TOMBSTONE_ENDPOINT;
				result->stage = SG_RUNE_LOAD_STAGE_LINK;
				result->index = index;
				return;
			}
			workspace->graph.source_marks[link->source] = 1;
		}
		for (index = 0; index < header->num_seeds; index++)
		{
			int tombstone =
				((uint16_t)workspace->wire_seeds[index].flags &
				 SG_RUNE_V3_SEED_TOMBSTONE) != 0;
			int outgoing = workspace->graph.source_marks[index] != 0;

			if (tombstone == outgoing)
			{
				result->stage = SG_RUNE_LOAD_STAGE_SEED;
				result->index = index;
				return;
			}
		}
	}
}

sg_rune_load_result_t SG_RuneV3Load(const unsigned char *snapshot,
	size_t snapshot_size, const sg_rune_v3_identity_t *expected,
	sg_rune_v3_header_t *header_out,
	rune_seed_t *native_seeds, size_t native_seed_capacity,
	rune_link_t *native_links, size_t native_link_capacity,
	sg_rune_v3_loader_workspace_t *workspace)
{
	sg_rune_v3_header_t header;
	sg_rune_load_result_t result;
	rune_wire_diagnostic_t diagnostic;
	uint32_t index;

	result = SG_RuneV3Inspect(snapshot, snapshot_size, expected, &header);
	if (result.diagnostic != RLW_OK)
		return result;
	if (!header_out || !native_seeds || !workspace)
	{
		result.diagnostic = RLW_INVALID_ARGUMENT;
		result.stage = SG_RUNE_LOAD_STAGE_ARGUMENT;
		return result;
	}
	if (native_seed_capacity < (size_t)header.num_seeds ||
	    (header.num_links != 0 &&
	     (!native_links || native_link_capacity < (size_t)header.num_links)) ||
	    !workspace->wire_seeds ||
	    workspace->wire_seed_capacity < (size_t)header.num_seeds ||
	    (header.num_links != 0 &&
	     (!workspace->wire_links ||
	      workspace->wire_link_capacity < (size_t)header.num_links)) ||
	    !workspace->graph.source_marks ||
	    workspace->graph.source_mark_capacity < (size_t)header.num_seeds ||
	    (header.num_links != 0 &&
	     (!workspace->graph.link_keys ||
	      workspace->graph.link_key_capacity < (size_t)header.num_links)))
	{
		result.diagnostic = RLW_ALLOCATION_FAILED;
		result.stage = SG_RUNE_LOAD_STAGE_CAPACITY;
		return result;
	}
	diagnostic = SG_RuneV3Decode(snapshot, snapshot_size, expected, &header,
		workspace->wire_seeds, workspace->wire_seed_capacity,
		workspace->wire_links, workspace->wire_link_capacity,
		&workspace->graph);
	if (diagnostic != RLW_OK)
	{
		result.diagnostic = diagnostic;
		Loader_LocalizeDecodeFailure(&result, snapshot, &header, workspace);
		return result;
	}
	for (index = 0; index < header.num_links; index++)
	{
		rune_reject_reason_t reason = SG_RuneV3ValidateLiteralLink(
			workspace->wire_seeds, header.num_seeds,
			&workspace->wire_links[index]);

		if (reason != RLR_OK)
		{
			result.diagnostic = RLW_BAD_LINK_RECORD;
			result.reason = reason;
			result.stage = reason == RLR_UNKNOWN_ACTION ||
					reason == RLR_ACTION_DISABLED
				? SG_RUNE_LOAD_STAGE_ACTION
				: SG_RUNE_LOAD_STAGE_CONTROL;
			result.index = index;
			return result;
		}
	}

	/* Validation is complete.  The remaining operations are infallible scalar
	 * copies, so no caller-visible output is changed on a rejected snapshot. */
	for (index = 0; index < header.num_seeds; index++)
	{
		const sg_rune_v3_seed_t *source = &workspace->wire_seeds[index];
		rune_seed_t *destination = &native_seeds[index];

		memcpy(destination->origin, source->origin,
			sizeof(destination->origin));
		destination->area_hint = (short)source->area_hint;
		destination->flags = (short)source->flags;
	}
	for (index = 0; index < header.num_links; index++)
	{
		const sg_rune_v3_link_t *source = &workspace->wire_links[index];
		rune_link_t *destination = &native_links[index];

		destination->from = (int)source->source;
		destination->to = (int)source->destination;
		destination->action = source->action;
		destination->provenance = source->provenance;
		destination->min_speed = source->min_speed;
		destination->heading = source->heading;
		destination->heading_slack = source->heading_slack;
		destination->exit_speed = source->exit_speed;
		destination->cost_ms = (short)source->cost_ms;
		memcpy(destination->anchor, source->suffix_anchor,
			sizeof(destination->anchor));
	}
	*header_out = header;
	result.diagnostic = RLW_OK;
	result.reason = RLR_OK;
	result.stage = SG_RUNE_LOAD_STAGE_DONE;
	result.index = SG_RUNE_LOAD_INDEX_NONE;
	return result;
}
