/* sg_rune_writer.c -- allocation-free native graph to RUNE v3 stream. */
#include "q_shared.h"
#include "slipgate/sg_rune_writer.h"

#include "slipgate/sg_action.h"
#include "slipgate/sg_compound.h"

#include <math.h>
#include <string.h>

#define WRITER_LINK_INDEX_BITS 18U
#define WRITER_LINK_INDEX_MASK ((UINT64_C(1) << WRITER_LINK_INDEX_BITS) - 1U)

_Static_assert(SG_RUNE_V3_MAX_LINKS ==
	(UINT32_C(1) << WRITER_LINK_INDEX_BITS),
	"writer duplicate-key index packing drift");
_Static_assert(RSF_WATER == SG_RUNE_V3_SEED_WATER,
	"native/v3 water flag drift");
_Static_assert(RSF_TOMBSTONE == SG_RUNE_V3_SEED_TOMBSTONE,
	"native/v3 tombstone flag drift");

static sg_rune_write_result_t Writer_Result(rune_wire_diagnostic_t diagnostic,
	rune_reject_reason_t reason, sg_rune_write_stage_t stage, uint32_t index)
{
	sg_rune_write_result_t result;

	memset(&result, 0, sizeof(result));
	result.diagnostic = diagnostic;
	result.reason = reason;
	result.stage = stage;
	result.index = index;
	return result;
}

static uint32_t Writer_FloatBits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static int Writer_VectorFinite(const float vector[3])
{
	return isfinite(vector[0]) && isfinite(vector[1]) &&
	       isfinite(vector[2]);
}

static int Writer_VectorInWorld(const float vector[3])
{
	static const float minimum =
		(float)SG_RUNE_PROOF_WORLD_FIXED_MIN /
		(float)SG_RUNE_PROOF_WORLD_FIXED_SCALE;
	static const float maximum =
		(float)SG_RUNE_PROOF_WORLD_FIXED_MAX /
		(float)SG_RUNE_PROOF_WORLD_FIXED_SCALE;
	int axis;

	if (!Writer_VectorFinite(vector))
		return 0;
	for (axis = 0; axis < 3; axis++)
		if (vector[axis] < minimum || vector[axis] > maximum)
			return 0;
	return 1;
}

static int Writer_VectorPositiveZero(const float vector[3])
{
	return Writer_FloatBits(vector[0]) == UINT32_C(0) &&
	       Writer_FloatBits(vector[1]) == UINT32_C(0) &&
	       Writer_FloatBits(vector[2]) == UINT32_C(0);
}

static int Writer_HookAngleCanonical(float angle)
{
	int units;
	float canonical;

	if (angle < -180.0f || angle >= 180.0f)
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

static void Writer_SetFailure(sg_rune_write_result_t *result,
	rune_wire_diagnostic_t diagnostic, rune_reject_reason_t reason,
	sg_rune_write_stage_t stage, uint32_t index)
{
	result->diagnostic = diagnostic;
	result->reason = reason;
	result->stage = stage;
	result->index = index;
}

static rune_wire_diagnostic_t Writer_EncodeSeed(const rune_seed_t *source,
	sg_rune_v3_seed_t *adapted,
	unsigned char encoded[SG_RUNE_V3_SEED_BYTES])
{
	if (!source || !adapted || !encoded)
		return RLW_INVALID_ARGUMENT;
	memset(adapted, 0, sizeof(*adapted));
	adapted->origin[0] = source->origin[0];
	adapted->origin[1] = source->origin[1];
	adapted->origin[2] = source->origin[2];
	adapted->area_hint = (int16_t)source->area_hint;
	adapted->flags = (int16_t)source->flags;
	return SG_RuneV3EncodeSeed(adapted, encoded, SG_RUNE_V3_SEED_BYTES);
}

static rune_reject_reason_t Writer_ValidateDeclared(
	const rune_link_t *source)
{
	if (!Writer_VectorInWorld(source->anchor) ||
	    source->provenance != RL_DECLARED || source->min_speed != 0 ||
	    source->heading != 0 ||
	    source->heading_slack != SG_RUNE_PROOF_DECLARED_CONTROL_MARKER ||
	    source->exit_speed != 0)
		return RLR_BAD_DECLARED_CONTROL;
	return RLR_OK;
}

static rune_reject_reason_t Writer_ValidateDrop(const rune_seed_t *from,
	const rune_link_t *source)
{
	float dx = source->anchor[0] - from->origin[0];
	float dy = source->anchor[1] - from->origin[1];
	float dz = source->anchor[2] - from->origin[2];
	float horizontal = sqrtf(dx * dx + dy * dy);
	float lip_yaw = atan2f(dy, dx) *
		(float)SG_RUNE_PROOF_FULL_TURN_DEGREES / (float)(2.0 * M_PI);
	float stored_yaw = (float)source->heading *
		(float)SG_RUNE_PROOF_FULL_TURN_DEGREES /
		(float)SG_RUNE_PROOF_ANGLE_BYTE_UNITS;
	float yaw_delta = lip_yaw - stored_yaw;

	while (yaw_delta > 180.0f)
		yaw_delta -= 360.0f;
	while (yaw_delta < -180.0f)
		yaw_delta += 360.0f;
	if (source->provenance != RL_PROVEN ||
	    !Writer_VectorInWorld(source->anchor) ||
	    ((uint16_t)from->flags & SG_RUNE_V3_SEED_WATER) != 0 ||
	    source->min_speed != 0 ||
	    source->heading_slack != SG_RUNE_PROOF_DROP_CONTROL_MARKER ||
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

static rune_reject_reason_t Writer_ValidateHook(const rune_seed_t *from,
	const rune_seed_t *to, const rune_link_t *source)
{
	int from_water = ((uint16_t)from->flags &
		SG_RUNE_V3_SEED_WATER) != 0;
	int to_water = ((uint16_t)to->flags &
		SG_RUNE_V3_SEED_WATER) != 0;
	int expected_slack = from_water
		? SG_RUNE_PROOF_WATER_HOOK_CONTROL_MARKER
		: SG_RUNE_PROOF_HOOK_CONTROL_SLACK;

	if (source->provenance != RL_PROVEN || source->min_speed != 0 ||
	    (from_water && to_water) || source->heading_slack != expected_slack ||
	    !Writer_HookAngleCanonical(source->anchor[PITCH]) ||
	    !Writer_HookAngleCanonical(source->anchor[YAW]) ||
	    source->anchor[PITCH] <
	        -(float)SG_RUNE_PROOF_HOOK_MAX_ABS_PITCH_DEGREES ||
	    source->anchor[PITCH] >
	        (float)SG_RUNE_PROOF_HOOK_MAX_ABS_PITCH_DEGREES ||
	    source->anchor[ROLL] < (float)SG_RUNE_PROOF_HOOK_MIN_RAY ||
	    source->anchor[ROLL] > (float)SG_RUNE_PROOF_HOOK_MAX_RAY)
		return RLR_BAD_HOOK_CONTROL;
	return RLR_OK;
}

static rune_reject_reason_t Writer_ValidateTeleport(
	const rune_seed_t *from, const rune_link_t *source)
{
	float dx = source->anchor[0] - from->origin[0];
	float dy = source->anchor[1] - from->origin[1];
	float dz = source->anchor[2] - from->origin[2];

	if (sqrtf(dx * dx + dy * dy) >
	        (float)SG_RUNE_PROOF_TELEPORT_SEED_REACH ||
	    fabsf(dz) > (float)SG_RUNE_PROOF_TELEPORT_SEED_REACH)
		return RLR_BAD_TELEPORT_REACH;
	return RLR_OK;
}

static rune_reject_reason_t Writer_ValidateDoor(const rune_seed_t *from,
	const rune_seed_t *to, const rune_link_t *source)
{
	float approach_x = source->anchor[0] - from->origin[0];
	float approach_y = source->anchor[1] - from->origin[1];
	float approach_z = source->anchor[2] - from->origin[2];
	float egress_x = to->origin[0] - source->anchor[0];
	float egress_y = to->origin[1] - source->anchor[1];
	float egress_z = to->origin[2] - source->anchor[2];
	int axis;

	for (axis = 0; axis < 3; axis++)
	{
		float scaled = source->anchor[axis] *
			(float)SG_RUNE_PROOF_DOOR_ANCHOR_SCALE;

		if (scaled != (float)(int)scaled)
			return RLR_BAD_ANCHOR_POLICY;
	}
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

static rune_reject_reason_t Writer_ValidateControl(
	const rune_seed_t *from, const rune_seed_t *to,
	const rune_link_t *source)
{
	rune_reject_reason_t reason;

	switch ((int)source->action)
	{
	case RL_RUN:
		return Writer_VectorInWorld(source->anchor)
			? RLR_OK : RLR_BAD_RUN_CONTROL;
	case RL_JUMP:
		return source->min_speed == 0 &&
		       Writer_VectorPositiveZero(source->anchor)
			? RLR_OK : RLR_BAD_JUMP_CONTROL;
	case RL_DROP:
		return Writer_ValidateDrop(from, source);
	case RL_HOOK:
		return Writer_ValidateHook(from, to, source);
	case RL_SWIM:
		return source->provenance == RL_PROVEN &&
		       source->min_speed == 0 && source->heading == 0 &&
		       source->heading_slack == 0 &&
		       Writer_VectorPositiveZero(source->anchor)
			? RLR_OK : RLR_BAD_SWIM_CONTROL;
	case RL_LIFT:
		return Writer_ValidateDeclared(source);
	case RL_TELEPORT:
		reason = Writer_ValidateDeclared(source);
		return reason != RLR_OK ? reason :
			Writer_ValidateTeleport(from, source);
	case RL_DOOR:
		reason = Writer_ValidateDeclared(source);
		return reason != RLR_OK ? reason :
			Writer_ValidateDoor(from, to, source);
	default:
		return RLR_ACTION_DISABLED;
	}
}

static rune_wire_diagnostic_t Writer_EncodeLink(
	const rune_seed_t *seeds, uint32_t num_seeds,
	const rune_link_t *source, sg_rune_v3_link_t *adapted,
	unsigned char encoded[SG_RUNE_V3_LINK_BYTES],
	rune_reject_reason_t *reason_out)
{
	const rune_seed_t *from;
	const rune_seed_t *to;
	rune_reject_reason_t reason = RLR_OK;
	int from_water;
	int to_water;
	rune_wire_diagnostic_t diagnostic;

	if (reason_out)
		*reason_out = RLR_OK;
	if (!seeds || !source || !adapted || !encoded || !reason_out)
		return RLW_INVALID_ARGUMENT;
	if (source->action > RL_DOOR_HOOK)
	{
		*reason_out = RLR_UNKNOWN_ACTION;
		return RLW_BAD_LINK_RECORD;
	}
	if (!SG_ActionRuntimeSupported((int)source->action) ||
	    (SG_CompoundAction(source->action) &&
	     !SG_CompoundRuntimeReady(source->action)))
	{
		*reason_out = RLR_ACTION_DISABLED;
		return RLW_BAD_LINK_RECORD;
	}
	if (!SG_ProvenanceWireValid(SG_RUNE_WIRE_V3,
	    (int)source->provenance))
	{
		*reason_out = RLR_UNKNOWN_PROVENANCE;
		return RLW_BAD_LINK_RECORD;
	}
	if (!SG_ActionAllowsProvenance((int)source->action,
	    (int)source->provenance))
	{
		*reason_out = RLR_PROVENANCE_FORBIDDEN;
		return RLW_BAD_LINK_RECORD;
	}
	if (source->from < 0 || source->to < 0 ||
	    (uint32_t)source->from >= num_seeds ||
	    (uint32_t)source->to >= num_seeds)
	{
		*reason_out = RLR_BAD_INDEX;
		return RLW_BAD_LINK_RECORD;
	}
	if (source->from == source->to)
	{
		*reason_out = RLR_SELF_LINK;
		return RLW_BAD_LINK_RECORD;
	}
	from = &seeds[source->from];
	to = &seeds[source->to];
	if (((uint16_t)from->flags & SG_RUNE_V3_SEED_TOMBSTONE) != 0 ||
	    ((uint16_t)to->flags & SG_RUNE_V3_SEED_TOMBSTONE) != 0)
	{
		*reason_out = RLR_TOMBSTONE_ENDPOINT;
		return RLW_BAD_ROUTE_OWNERSHIP;
	}
	if (source->cost_ms < SG_RUNE_V3_MIN_COST_MS ||
	    source->cost_ms > SG_RUNE_V3_MAX_COST_MS ||
	    (source->action == RL_DROP &&
	     (source->cost_ms < SG_RUNE_PROOF_SERVER_FRAME_MS ||
	      source->cost_ms >= SG_RUNE_PROOF_DROP_TOTAL_MS ||
	      source->cost_ms % SG_RUNE_PROOF_SERVER_FRAME_MS != 0)))
	{
		*reason_out = RLR_BAD_COST;
		return RLW_BAD_LINK_RECORD;
	}
	if (!Writer_VectorFinite(source->anchor) ||
	    !Writer_VectorFinite(source->mechanism_anchor))
	{
		*reason_out = RLR_NONFINITE_ANCHOR;
		return RLW_BAD_LINK_RECORD;
	}
	from_water = ((uint16_t)from->flags & SG_RUNE_V3_SEED_WATER) != 0;
	to_water = ((uint16_t)to->flags & SG_RUNE_V3_SEED_WATER) != 0;
	if (!SG_ActionEndpointAllowed((int)source->action,
	    from_water, to_water))
	{
		*reason_out = RLR_BAD_ENDPOINT_POLICY;
		return RLW_BAD_LINK_RECORD;
	}
	memset(adapted, 0, sizeof(*adapted));
	adapted->source = (uint32_t)source->from;
	adapted->destination = (uint32_t)source->to;
	adapted->action = source->action;
	adapted->provenance = source->provenance;
	adapted->min_speed = source->min_speed;
	adapted->heading = source->heading;
	adapted->heading_slack = source->heading_slack;
	adapted->exit_speed = source->exit_speed;
	adapted->cost_ms = (int16_t)source->cost_ms;
	adapted->suffix_anchor[0] = source->anchor[0];
	adapted->suffix_anchor[1] = source->anchor[1];
	adapted->suffix_anchor[2] = source->anchor[2];
	adapted->mechanism_anchor[0] = source->mechanism_anchor[0];
	adapted->mechanism_anchor[1] = source->mechanism_anchor[1];
	adapted->mechanism_anchor[2] = source->mechanism_anchor[2];
	adapted->sweep_clear_ms = source->sweep_clear_ms;
	adapted->mode = source->mode;
	if (SG_CompoundAction(source->action))
	{
		sg_rune_v3_seed_t compound_seeds[2];
		sg_rune_v3_link_t compound_link = *adapted;

		memset(compound_seeds, 0, sizeof(compound_seeds));
		memcpy(compound_seeds[0].origin, from->origin,
			sizeof(compound_seeds[0].origin));
		compound_seeds[0].flags = from->flags;
		memcpy(compound_seeds[1].origin, to->origin,
			sizeof(compound_seeds[1].origin));
		compound_seeds[1].flags = to->flags;
		compound_link.source = 0;
		compound_link.destination = 1;
		reason = SG_CompoundValidateLink(compound_seeds, 2,
			&compound_link);
	}
	else if (!Writer_VectorPositiveZero(source->mechanism_anchor) ||
	         source->sweep_clear_ms != 0)
		reason = RLR_NONZERO_TAIL;
	else if (source->mode != RLCM_NONE)
		reason = RLR_BAD_MODE;
	else
		reason = Writer_ValidateControl(from, to, source);
	if (reason != RLR_OK)
	{
		*reason_out = reason;
		return RLW_BAD_LINK_RECORD;
	}
	diagnostic = SG_RuneV3EncodeLink(adapted, encoded,
		SG_RUNE_V3_LINK_BYTES);
	if (diagnostic != RLW_OK)
		*reason_out = RLR_BAD_ANCHOR_POLICY;
	return diagnostic;
}

static void Writer_SiftKeys(uint64_t *keys, size_t root, size_t end)
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

static void Writer_SortKeys(uint64_t *keys, size_t count)
{
	size_t start;
	size_t end;

	if (count < 2)
		return;
	for (start = count / 2U; start > 0; start--)
		Writer_SiftKeys(keys, start - 1U, count);
	for (end = count; end > 1; end--)
	{
		uint64_t temporary = keys[0];

		keys[0] = keys[end - 1U];
		keys[end - 1U] = temporary;
		Writer_SiftKeys(keys, 0, end - 1U);
	}
}

static uint64_t Writer_LinkIdentity(const sg_rune_v3_link_t *link)
{
	return ((uint64_t)link->source << 23) |
	       ((uint64_t)link->destination << 8) | (uint64_t)link->action;
}

static void Writer_BuildHeader(sg_rune_v3_header_t *header,
	const sg_rune_v3_identity_t *identity, uint32_t num_seeds,
	uint32_t num_links, uint32_t payload_crc32)
{
	memset(header, 0, sizeof(*header));
	header->magic = SG_RUNE_V3_MAGIC;
	header->version = SG_RUNE_V3_VERSION;
	header->header_bytes = SG_RUNE_V3_HEADER_BYTES;
	header->seed_bytes = SG_RUNE_V3_SEED_BYTES;
	header->link_bytes = SG_RUNE_V3_LINK_BYTES;
	header->num_seeds = num_seeds;
	header->num_links = num_links;
	header->payload_crc32 = payload_crc32;
	header->bsp_checksum = identity->bsp_checksum;
	header->entity_crc32 = identity->entity_crc32;
	header->action_contract_crc32 = SG_ACTION_CONTRACT_CRC32;
	header->physics_flags = identity->physics_flags;
	header->gravity = identity->gravity;
	header->airaccelerate = identity->airaccelerate;
	header->maxvelocity = identity->maxvelocity;
	header->pmove_substep_ms = identity->pmove_substep_ms;
	header->server_frame_ms = identity->server_frame_ms;
	header->host_physics_id = identity->host_physics_id;
	memcpy(header->map_name, identity->map_name, SG_RUNE_V3_MAP_NAME_BYTES);
}

sg_rune_write_result_t SG_RuneV3Write(
	const sg_rune_v3_identity_t *identity,
	const rune_seed_t *seeds, uint32_t num_seeds,
	const rune_link_t *links, uint32_t num_links,
	sg_rune_v3_workspace_t *workspace,
	sg_rune_write_sink_fn sink, void *sink_context)
{
	sg_rune_write_result_t result = Writer_Result(RLW_OK, RLR_OK,
		SG_RUNE_WRITE_STAGE_ARGUMENT, SG_RUNE_WRITE_INDEX_NONE);
	sg_rune_v3_header_t header;
	sg_rune_v3_seed_t adapted_seed;
	sg_rune_v3_link_t adapted_link;
	unsigned char header_bytes[SG_RUNE_V3_HEADER_BYTES];
	unsigned char seed_bytes[SG_RUNE_V3_SEED_BYTES];
	unsigned char link_bytes[SG_RUNE_V3_LINK_BYTES];
	uint32_t crc_state = 0;
	uint32_t emitted_crc_state = 0;
	uint32_t emitted_crc = 0;
	uint32_t index;
	rune_wire_diagnostic_t diagnostic;

	diagnostic = SG_RuneV3FileSize(num_seeds, num_links,
		&result.file_size);
	if (diagnostic != RLW_OK)
	{
		result.diagnostic = diagnostic;
		return result;
	}
	if (!identity || !seeds || (num_links != 0 && !links) ||
	    !workspace || !sink)
	{
		result.diagnostic = RLW_INVALID_ARGUMENT;
		return result;
	}
	if (!workspace->source_marks ||
	    workspace->source_mark_capacity < (size_t)num_seeds ||
	    (num_links != 0 &&
	     (!workspace->link_keys ||
	      workspace->link_key_capacity < (size_t)num_links)))
	{
		result.diagnostic = RLW_ALLOCATION_FAILED;
		return result;
	}

	Writer_BuildHeader(&header, identity, num_seeds, num_links, 0);
	diagnostic = SG_RuneV3EncodeHeader(&header, header_bytes,
		SG_RUNE_V3_HEADER_BYTES);
	if (diagnostic != RLW_OK)
	{
		Writer_SetFailure(&result, diagnostic, RLR_OK,
			SG_RUNE_WRITE_STAGE_HEADER, SG_RUNE_WRITE_INDEX_NONE);
		return result;
	}
	memset(workspace->source_marks, 0, (size_t)num_seeds);
	diagnostic = SG_RuneV3PayloadCRCInit(&crc_state);
	if (diagnostic != RLW_OK)
	{
		Writer_SetFailure(&result, diagnostic, RLR_OK,
			SG_RUNE_WRITE_STAGE_PREFLIGHT, SG_RUNE_WRITE_INDEX_NONE);
		return result;
	}

	for (index = 0; index < num_seeds; index++)
	{
		diagnostic = Writer_EncodeSeed(&seeds[index], &adapted_seed,
			seed_bytes);
		if (diagnostic != RLW_OK)
		{
			Writer_SetFailure(&result, diagnostic, RLR_OK,
				SG_RUNE_WRITE_STAGE_ADAPT_SEED, index);
			return result;
		}
		diagnostic = SG_RuneV3PayloadCRCUpdate(&crc_state, seed_bytes,
			SG_RUNE_V3_SEED_BYTES);
		if (diagnostic != RLW_OK)
		{
			Writer_SetFailure(&result, diagnostic, RLR_OK,
				SG_RUNE_WRITE_STAGE_PREFLIGHT, index);
			return result;
		}
	}
	for (index = 0; index < num_links; index++)
	{
		rune_reject_reason_t reason;
		uint64_t identity_key;

		diagnostic = Writer_EncodeLink(seeds, num_seeds, &links[index],
			&adapted_link, link_bytes, &reason);
		if (diagnostic != RLW_OK)
		{
			Writer_SetFailure(&result, diagnostic, reason,
				SG_RUNE_WRITE_STAGE_ADAPT_LINK, index);
			return result;
		}
		diagnostic = SG_RuneV3PayloadCRCUpdate(&crc_state, link_bytes,
			SG_RUNE_V3_LINK_BYTES);
		if (diagnostic != RLW_OK)
		{
			Writer_SetFailure(&result, diagnostic, RLR_OK,
				SG_RUNE_WRITE_STAGE_PREFLIGHT, index);
			return result;
		}
		identity_key = Writer_LinkIdentity(&adapted_link);
		workspace->link_keys[index] =
			(identity_key << WRITER_LINK_INDEX_BITS) | (uint64_t)index;
		workspace->source_marks[adapted_link.source] = 1;
	}
	Writer_SortKeys(workspace->link_keys, (size_t)num_links);
	for (index = 1; index < num_links; index++)
	{
		uint64_t previous = workspace->link_keys[index - 1U];
		uint64_t current = workspace->link_keys[index];

		if ((previous >> WRITER_LINK_INDEX_BITS) ==
		    (current >> WRITER_LINK_INDEX_BITS))
		{
			Writer_SetFailure(&result, RLW_DUPLICATE_LINK, RLR_OK,
				SG_RUNE_WRITE_STAGE_PREFLIGHT,
				(uint32_t)(current & WRITER_LINK_INDEX_MASK));
			return result;
		}
	}
	for (index = 0; index < num_seeds; index++)
	{
		int tombstone = ((uint16_t)seeds[index].flags &
			SG_RUNE_V3_SEED_TOMBSTONE) != 0;
		int has_outgoing = workspace->source_marks[index] != 0;

		if (tombstone == has_outgoing)
		{
			Writer_SetFailure(&result, RLW_BAD_ROUTE_OWNERSHIP, RLR_OK,
				SG_RUNE_WRITE_STAGE_PREFLIGHT, index);
			return result;
		}
	}
	diagnostic = SG_RuneV3PayloadCRCFinish(crc_state,
		&result.payload_crc32);
	if (diagnostic != RLW_OK)
	{
		Writer_SetFailure(&result, diagnostic, RLR_OK,
			SG_RUNE_WRITE_STAGE_PREFLIGHT, SG_RUNE_WRITE_INDEX_NONE);
		return result;
	}
	Writer_BuildHeader(&header, identity, num_seeds, num_links,
		result.payload_crc32);
	diagnostic = SG_RuneV3EncodeHeader(&header, header_bytes,
		SG_RUNE_V3_HEADER_BYTES);
	if (diagnostic != RLW_OK)
	{
		Writer_SetFailure(&result, diagnostic, RLR_OK,
			SG_RUNE_WRITE_STAGE_HEADER, SG_RUNE_WRITE_INDEX_NONE);
		return result;
	}
	diagnostic = SG_RuneV3PayloadCRCInit(&emitted_crc_state);
	if (diagnostic != RLW_OK)
	{
		Writer_SetFailure(&result, diagnostic, RLR_OK,
			SG_RUNE_WRITE_STAGE_VERIFY, SG_RUNE_WRITE_INDEX_NONE);
		return result;
	}

	if (sink(sink_context, header_bytes, SG_RUNE_V3_HEADER_BYTES) != 0)
	{
		Writer_SetFailure(&result, RLW_IO_ERROR, RLR_OK,
			SG_RUNE_WRITE_STAGE_EMIT_HEADER, SG_RUNE_WRITE_INDEX_NONE);
		return result;
	}
	result.bytes_written += SG_RUNE_V3_HEADER_BYTES;
	for (index = 0; index < num_seeds; index++)
	{
		diagnostic = Writer_EncodeSeed(&seeds[index], &adapted_seed,
			seed_bytes);
		if (diagnostic != RLW_OK)
		{
			Writer_SetFailure(&result, diagnostic, RLR_OK,
				SG_RUNE_WRITE_STAGE_ADAPT_SEED, index);
			return result;
		}
		diagnostic = SG_RuneV3PayloadCRCUpdate(&emitted_crc_state,
			seed_bytes, SG_RUNE_V3_SEED_BYTES);
		if (diagnostic != RLW_OK)
		{
			Writer_SetFailure(&result, diagnostic, RLR_OK,
				SG_RUNE_WRITE_STAGE_VERIFY, index);
			return result;
		}
		if (sink(sink_context, seed_bytes, SG_RUNE_V3_SEED_BYTES) != 0)
		{
			Writer_SetFailure(&result, RLW_IO_ERROR, RLR_OK,
				SG_RUNE_WRITE_STAGE_EMIT_SEED, index);
			return result;
		}
		result.bytes_written += SG_RUNE_V3_SEED_BYTES;
	}
	for (index = 0; index < num_links; index++)
	{
		rune_reject_reason_t reason;

		diagnostic = Writer_EncodeLink(seeds, num_seeds, &links[index],
			&adapted_link, link_bytes, &reason);
		if (diagnostic != RLW_OK)
		{
			Writer_SetFailure(&result, diagnostic, reason,
				SG_RUNE_WRITE_STAGE_ADAPT_LINK, index);
			return result;
		}
		diagnostic = SG_RuneV3PayloadCRCUpdate(&emitted_crc_state,
			link_bytes, SG_RUNE_V3_LINK_BYTES);
		if (diagnostic != RLW_OK)
		{
			Writer_SetFailure(&result, diagnostic, RLR_OK,
				SG_RUNE_WRITE_STAGE_VERIFY, index);
			return result;
		}
		if (sink(sink_context, link_bytes, SG_RUNE_V3_LINK_BYTES) != 0)
		{
			Writer_SetFailure(&result, RLW_IO_ERROR, RLR_OK,
				SG_RUNE_WRITE_STAGE_EMIT_LINK, index);
			return result;
		}
		result.bytes_written += SG_RUNE_V3_LINK_BYTES;
	}
	diagnostic = SG_RuneV3PayloadCRCFinish(emitted_crc_state, &emitted_crc);
	if (diagnostic != RLW_OK || emitted_crc != result.payload_crc32)
	{
		Writer_SetFailure(&result,
			diagnostic != RLW_OK ? diagnostic : RLW_BAD_PAYLOAD_CRC,
			RLR_OK, SG_RUNE_WRITE_STAGE_VERIFY,
			SG_RUNE_WRITE_INDEX_NONE);
		return result;
	}
	result.stage = SG_RUNE_WRITE_STAGE_DONE;
	result.index = SG_RUNE_WRITE_INDEX_NONE;
	return result;
}
