/* sg_rune_codec.c -- isolated explicit-LE RUNE mechanism codec. */
#include "../q_shared.h"
#include "sg_rune_codec.h"

#include "sg_action.h"
#include "sg_crc32.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <string.h>

_Static_assert(CHAR_BIT == 8, "RUNE requires 8-bit bytes");
_Static_assert(sizeof(float) == 4, "RUNE requires 32-bit float");
_Static_assert(FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128,
	"RUNE requires IEEE-754 binary32 float");
_Static_assert(SG_RUNE_CODEC_HEADER_BYTES == 160U, "header size drift");
_Static_assert(SG_RUNE_CODEC_SEED_BYTES == 16U, "seed size drift");
_Static_assert(SG_RUNE_CODEC_LINK_BYTES == 48U, "link size drift");
_Static_assert(SG_RUNE_CODEC_ACTIVATION_NODE_BYTES == 92U,
	"activation-node size drift");
_Static_assert(SG_RUNE_CODEC_ACTIVATION_EDGE_BYTES == 16U,
	"activation-edge size drift");
_Static_assert(SG_RUNE_CODEC_ACTIVATION_PLAN_BYTES == 32U,
	"activation-plan size drift");
_Static_assert(RL_DOOR_HOOK == 11, "action graph generation drift");
_Static_assert(RL_PUSH == 13, "push action drift");

#define Codec_PAYLOAD_CRC_OFFSET 20U
#define Codec_RESERVED_ZERO_OFFSET 4U
#define Codec_MAP_OFFSET 64U
#define Codec_EXTENSION_OFFSET 128U

static sg_rune_codec_diagnostic_t Codec_Diagnostic(rune_wire_diagnostic_t value)
{
	return (sg_rune_codec_diagnostic_t)(int)value;
}

static void Codec_PutU16(unsigned char *out, uint16_t value)
{
	out[0] = (unsigned char)(value & UINT16_C(0xff));
	out[1] = (unsigned char)(value >> 8);
}

static void Codec_PutU32(unsigned char *out, uint32_t value)
{
	out[0] = (unsigned char)(value & UINT32_C(0xff));
	out[1] = (unsigned char)((value >> 8) & UINT32_C(0xff));
	out[2] = (unsigned char)((value >> 16) & UINT32_C(0xff));
	out[3] = (unsigned char)(value >> 24);
}

static uint16_t Codec_GetU16(const unsigned char *in)
{
	return (uint16_t)((uint16_t)in[0] | (uint16_t)((uint16_t)in[1] << 8));
}

static uint32_t Codec_GetU32(const unsigned char *in)
{
	return (uint32_t)in[0] |
	       ((uint32_t)in[1] << 8) |
	       ((uint32_t)in[2] << 16) |
	       ((uint32_t)in[3] << 24);
}

static void Codec_PutI16(unsigned char *out, int16_t value)
{
	Codec_PutU16(out, (uint16_t)value);
}

static int16_t Codec_GetI16(const unsigned char *in)
{
	uint16_t value = Codec_GetU16(in);
	int32_t signed_value = value <= INT16_MAX
		? (int32_t)value : (int32_t)value - INT32_C(65536);

	return (int16_t)signed_value;
}

static void Codec_PutI32(unsigned char *out, int32_t value)
{
	Codec_PutU32(out, (uint32_t)value);
}

static int32_t Codec_GetI32(const unsigned char *in)
{
	uint32_t value = Codec_GetU32(in);
	int64_t signed_value = value <= INT32_MAX
		? (int64_t)value : (int64_t)value - INT64_C(4294967296);

	return (int32_t)signed_value;
}

static uint32_t Codec_FloatBits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static void Codec_PutFloat(unsigned char *out, float value)
{
	Codec_PutU32(out, Codec_FloatBits(value));
}

static float Codec_GetFloat(const unsigned char *in)
{
	uint32_t bits = Codec_GetU32(in);
	float value;

	memcpy(&value, &bits, sizeof(value));
	return value;
}

static int Codec_MapInitial(unsigned char c)
{
	return (c >= (unsigned char)'A' && c <= (unsigned char)'Z') ||
	       (c >= (unsigned char)'a' && c <= (unsigned char)'z') ||
	       (c >= (unsigned char)'0' && c <= (unsigned char)'9') ||
	       c == (unsigned char)'_';
}

static int Codec_MapTail(unsigned char c)
{
	return Codec_MapInitial(c) || c == (unsigned char)'-';
}

static int Codec_MapNameValid(
	const char map_name[SG_RUNE_CODEC_MAP_NAME_BYTES])
{
	size_t i;
	size_t j;

	if (!map_name || !Codec_MapInitial((unsigned char)map_name[0]))
		return 0;
	for (i = 1U; i < SG_RUNE_CODEC_MAP_NAME_BYTES; i++)
	{
		unsigned char c = (unsigned char)map_name[i];

		if (c == 0U)
		{
			for (j = i + 1U; j < SG_RUNE_CODEC_MAP_NAME_BYTES; j++)
				if (map_name[j] != '\0')
					return 0;
			return 1;
		}
		if (!Codec_MapTail(c))
			return 0;
	}
	return 0;
}

static int Codec_FloatFinite(float value)
{
	return isfinite(value) != 0;
}

static int Codec_VectorFinite(const float vector[3])
{
	return vector && Codec_FloatFinite(vector[0]) &&
	       Codec_FloatFinite(vector[1]) && Codec_FloatFinite(vector[2]);
}

static int Codec_VectorExactZero(const float vector[3])
{
	return vector && Codec_FloatBits(vector[0]) == UINT32_C(0) &&
	       Codec_FloatBits(vector[1]) == UINT32_C(0) &&
	       Codec_FloatBits(vector[2]) == UINT32_C(0);
}

static int Codec_VectorNumericallyZero(const float vector[3])
{
	return vector && vector[0] == 0.0f && vector[1] == 0.0f &&
	       vector[2] == 0.0f;
}

static int Codec_VectorInFixedWorld(const float vector[3])
{
	static const float minimum =
		(float)SG_RUNE_PROOF_WORLD_FIXED_MIN /
		(float)SG_RUNE_PROOF_WORLD_FIXED_SCALE;
	static const float maximum =
		(float)SG_RUNE_PROOF_WORLD_FIXED_MAX /
		(float)SG_RUNE_PROOF_WORLD_FIXED_SCALE;
	int i;

	if (!Codec_VectorFinite(vector))
		return 0;
	for (i = 0; i < 3; i++)
		if (vector[i] < minimum || vector[i] > maximum)
			return 0;
	return 1;
}

static int Codec_VectorOnPmoveLattice(const float vector[3])
{
	int i;

	if (!Codec_VectorInFixedWorld(vector))
		return 0;
	for (i = 0; i < 3; i++)
	{
		float scaled = vector[i] *
			(float)SG_RUNE_PROOF_WORLD_FIXED_SCALE;

		if (scaled != (float)(int)scaled)
			return 0;
	}
	return 1;
}

static int Codec_VectorOnDoorLattice(const float vector[3])
{
	int i;

	if (!Codec_VectorInFixedWorld(vector))
		return 0;
	for (i = 0; i < 3; i++)
	{
		float scaled = vector[i] *
			(float)SG_RUNE_PROOF_DOOR_ANCHOR_SCALE;

		if (scaled != (float)(int)scaled ||
		    scaled < (float)SG_RUNE_PROOF_WORLD_FIXED_MIN ||
		    scaled > (float)SG_RUNE_PROOF_WORLD_FIXED_MAX)
			return 0;
	}
	return 1;
}

static int Codec_RocketControl(const float vector[3])
{
	if (!Codec_VectorFinite(vector) ||
	    vector[0] != (float)(int)vector[0] ||
	    vector[1] != (float)(int)vector[1] ||
	    vector[2] != (float)(int)vector[2])
		return 0;
	return vector[0] >= (float)INT16_MIN &&
	       vector[0] <= (float)INT16_MAX &&
	       vector[1] >= (float)INT16_MIN &&
	       vector[1] <= (float)INT16_MAX &&
	       vector[2] >= (float)SG_RUNE_PROOF_ROCKETJUMP_HEALTH_MIN &&
	       vector[2] <= (float)SG_RUNE_PROOF_ROCKETJUMP_HEALTH_MAX;
}

static int Codec_ButtonRideEndpointValid(
	const sg_rune_codec_link_t *link)
{
	int i;

	if (!link)
		return 0;
	for (i = 0; i < 3; i++)
	{
		int anchor_q8 = (int)(link->suffix_anchor[i] *
			(float)SG_RUNE_PROOF_DOOR_ANCHOR_SCALE);
		int displacement_q8 = (int)(link->mechanism_anchor[i] *
			(float)SG_RUNE_PROOF_DOOR_ANCHOR_SCALE);
		int endpoint_q8 = anchor_q8 + displacement_q8;

		if (endpoint_q8 < SG_RUNE_PROOF_WORLD_FIXED_MIN ||
		    endpoint_q8 > SG_RUNE_PROOF_WORLD_FIXED_MAX)
			return 0;
	}
	return 1;
}

static sg_rune_codec_diagnostic_t Codec_ValidatePhysics(
	uint32_t physics_flags, float gravity, float airaccelerate,
	float maxvelocity, uint16_t pmove_substep_ms,
	uint16_t server_frame_ms, uint32_t host_physics_id)
{
	if (physics_flags != SG_RUNE_PROOF_PHYSICS_FLAGS_SUPPORTED ||
	    !Codec_FloatFinite(gravity) ||
	    gravity < (float)SG_RUNE_PROOF_GRAVITY_MIN ||
	    gravity > (float)SG_RUNE_PROOF_GRAVITY_MAX ||
	    (SG_RUNE_PROOF_GRAVITY_INTEGRAL_REQUIRED &&
	     gravity != (float)(int)gravity) ||
	    !Codec_FloatFinite(airaccelerate) ||
	    (SG_RUNE_PROOF_AIRACCELERATE_ZERO_REQUIRED &&
	     airaccelerate != 0.0f) ||
	    !Codec_FloatFinite(maxvelocity) ||
	    maxvelocity < (float)SG_RUNE_PROOF_MAXVELOCITY_MIN ||
	    pmove_substep_ms != SG_RUNE_PROOF_PMOVE_SUBSTEP_MS ||
	    server_frame_ms != SG_RUNE_PROOF_SERVER_FRAME_MS)
		return Codec_Diagnostic(RLW_BAD_PHYSICS_LAW);
	if (host_physics_id < SG_RUNE_PROOF_HOST_PHYSICS_ID_MIN)
		return Codec_Diagnostic(RLW_IDENTITY_UNAVAILABLE);
	return RLCODEC_OK;
}

static sg_rune_codec_diagnostic_t Codec_ValidateSeedFields(
	const sg_rune_codec_seed_t *seed)
{
	if (!seed || !Codec_VectorOnPmoveLattice(seed->origin) ||
	    seed->area_hint < 0 || seed->area_hint > 255 || seed->flags < 0 ||
	    ((uint16_t)seed->flags & ~SG_RUNE_CODEC_SEED_FLAG_MASK) != 0U)
		return Codec_Diagnostic(RLW_BAD_SEED_RECORD);
	return RLCODEC_OK;
}

static sg_rune_codec_diagnostic_t Codec_ValidateAnchor(
	const float anchor[3], int policy)
{
	if (!Codec_VectorFinite(anchor))
		return Codec_Diagnostic(RLW_BAD_LINK_RECORD);
	switch (policy)
	{
	case RLAP_ZERO:
		return Codec_VectorExactZero(anchor) ? RLCODEC_OK :
			Codec_Diagnostic(RLW_BAD_LINK_RECORD);
	case RLAP_RUN_WAYPOINT:
	case RLAP_DROP_LIP:
	case RLAP_WORLD:
	case RLAP_TELEPORT_PAD:
		return Codec_VectorInFixedWorld(anchor) ? RLCODEC_OK :
			Codec_Diagnostic(RLW_BAD_LINK_RECORD);
	case RLAP_DOOR_WAIT:
	case RLAP_DOOR_PREOPEN_CONTACT:
	case RLAP_DOOR_RIDE_INGRESS_LIP:
	case RLAP_TRAIN_CROSS:
		return Codec_VectorOnDoorLattice(anchor) ? RLCODEC_OK :
			Codec_Diagnostic(RLW_BAD_LINK_RECORD);
	case RLAP_HOOK_CONTROL:
		return RLCODEC_OK;
	case RLAP_ROCKET_CONTROL:
		return Codec_RocketControl(anchor) ? RLCODEC_OK :
			Codec_Diagnostic(RLW_BAD_LINK_RECORD);
	default:
		return Codec_Diagnostic(RLW_BAD_LINK_RECORD);
	}
}

static sg_rune_codec_diagnostic_t Codec_ValidateLinkFields(
	const sg_rune_codec_link_t *link)
{
	const sg_action_desc_t *policy;
	sg_rune_codec_diagnostic_t diagnostic;
	int policy_action;
	int mechanism_policy;

	if (!link || !SG_ActionWireValid((int)link->action) ||
	    !SG_ActionMechanismAdmitted((int)link->action))
		return Codec_Diagnostic(RLW_BAD_LINK_RECORD);
	policy_action = SG_ActionMechanismLinkPolicyAction((int)link->action);
	policy = SG_ActionDescribe(policy_action);
	if (!policy ||
	    !SG_ProvenanceWireValid((int)link->provenance) ||
	    !SG_ModeWireValid((int)link->mode) ||
	    !SG_ActionAllowsProvenance(policy_action, (int)link->provenance) ||
	    !SG_ActionAllowsMode(policy_action, (int)link->mode) ||
	    link->cost_ms < RUNE_MIN_COST_MS ||
	    link->cost_ms > RUNE_MAX_COST_MS || link->reserved != 0U)
		return Codec_Diagnostic(RLW_BAD_LINK_RECORD);
	if (policy_action == RL_DROP &&
	    (link->cost_ms < SG_RUNE_PROOF_SERVER_FRAME_MS ||
	     link->cost_ms >= SG_RUNE_PROOF_DROP_TOTAL_MS ||
	     link->cost_ms % SG_RUNE_PROOF_SERVER_FRAME_MS != 0))
		return Codec_Diagnostic(RLW_BAD_LINK_RECORD);
	if (policy_action == RL_ROCKETJUMP &&
	    (link->provenance != RL_PROVEN || link->min_speed != 0U ||
	     link->heading_slack != SG_RUNE_PROOF_ROCKETJUMP_HEADING_SLACK ||
	     link->cost_ms > SG_RUNE_PROOF_ROCKETJUMP_TOTAL_MS ||
	     link->cost_ms % SG_RUNE_PROOF_PMOVE_SUBSTEP_MS != 0))
		return Codec_Diagnostic(RLW_BAD_LINK_RECORD);
	diagnostic = Codec_ValidateAnchor(link->suffix_anchor,
		(int)policy->suffix_anchor_policy);
	if (diagnostic != RLCODEC_OK)
		return diagnostic;
	if (!SG_ActionHasTrait(policy_action, SG_ACTF_ATOMIC))
	{
		if (!Codec_VectorExactZero(link->mechanism_anchor) ||
		    link->sweep_clear_ms != 0U)
			return Codec_Diagnostic(RLW_BAD_LINK_RECORD);
		return RLCODEC_OK;
	}
	if (link->sweep_clear_ms == 0U ||
	    link->sweep_clear_ms % SG_RUNE_PROOF_SERVER_FRAME_MS != 0U ||
	    link->sweep_clear_ms > (uint16_t)link->cost_ms ||
	    (policy->mechanism_policy != RLMP_DOOR_WORLD_FIXED_1_8 &&
	     policy->mechanism_policy != RLMP_TRAIN_WORLD_FIXED_1_8))
		return Codec_Diagnostic(RLW_BAD_LINK_RECORD);
	if (link->mode == RLCM_PREOPEN)
		mechanism_policy = (int)policy->preopen_mechanism_anchor_policy;
	else if (link->mode == RLCM_RIDE)
		mechanism_policy = (int)policy->ride_mechanism_anchor_policy;
	else
		return Codec_Diagnostic(RLW_BAD_LINK_RECORD);
	/* BUTTON_DOOR authenticates the sealed entry brush's exact start-to-end
	 * displacement here.  A zero vector cannot describe an admitted moving
	 * func_button in either STATIC or RIDER mode. */
	diagnostic = Codec_ValidateAnchor(link->mechanism_anchor,
		mechanism_policy);
	if (diagnostic != RLCODEC_OK)
		return diagnostic;
	if (policy_action == RL_BUTTON_DOOR &&
	    (Codec_VectorNumericallyZero(link->mechanism_anchor) ||
	     (link->mode == RLCM_RIDE &&
	      !Codec_ButtonRideEndpointValid(link))))
		return Codec_Diagnostic(RLW_BAD_LINK_RECORD);
	return RLCODEC_OK;
}

static void Codec_SiftKeys(uint64_t *keys, size_t root, size_t end)
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

static void Codec_SortKeys(uint64_t *keys, size_t count)
{
	size_t start;
	size_t end;

	if (count < 2U)
		return;
	for (start = count / 2U; start > 0U; start--)
		Codec_SiftKeys(keys, start - 1U, count);
	for (end = count; end > 1U; end--)
	{
		uint64_t temporary = keys[0];

		keys[0] = keys[end - 1U];
		keys[end - 1U] = temporary;
		Codec_SiftKeys(keys, 0U, end - 1U);
	}
}

static sg_rune_codec_diagnostic_t Codec_ValidateActionGraph(
	const sg_rune_codec_seed_t *seeds, uint32_t num_seeds,
	const sg_rune_codec_link_t *links, uint32_t num_links,
	sg_rune_codec_workspace_t *workspace)
{
	uint32_t i;

	if (num_seeds == 0U || num_seeds > SG_RUNE_CODEC_MAX_SEEDS ||
	    num_links > SG_RUNE_CODEC_MAX_LINKS)
		return Codec_Diagnostic(RLW_BAD_COUNTS);
	if (!seeds || (num_links != 0U && !links) || !workspace)
		return Codec_Diagnostic(RLW_INVALID_ARGUMENT);
	if (!workspace->graph_source_marks ||
	    workspace->graph_source_mark_capacity < (size_t)num_seeds ||
	    (num_links != 0U && (!workspace->graph_link_keys ||
	     workspace->graph_link_key_capacity < (size_t)num_links)))
		return Codec_Diagnostic(RLW_ALLOCATION_FAILED);
	memset(workspace->graph_source_marks, 0, (size_t)num_seeds);
	for (i = 0U; i < num_seeds; i++)
		if (Codec_ValidateSeedFields(&seeds[i]) != RLCODEC_OK)
			return Codec_Diagnostic(RLW_BAD_SEED_RECORD);
	for (i = 0U; i < num_links; i++)
	{
		const sg_rune_codec_link_t *link = &links[i];
		uint64_t plan;

		if (link->source >= num_seeds || link->destination >= num_seeds ||
		    link->source == link->destination ||
		    Codec_ValidateLinkFields(link) != RLCODEC_OK)
			return Codec_Diagnostic(RLW_BAD_LINK_RECORD);
		if (link->activation_plan == SG_RUNE_CODEC_NO_ACTIVATION_PLAN)
			plan = SG_RUNE_CODEC_MAX_ACTIVATION_PLANS;
		else if (link->activation_plan < SG_RUNE_CODEC_MAX_ACTIVATION_PLANS)
			plan = link->activation_plan;
		else
			return RLCODEC_BAD_ACTIVATION_PLAN;
		workspace->graph_link_keys[i] = ((uint64_t)link->source << 42) |
			((uint64_t)link->destination << 27) |
			((uint64_t)link->action << 19) | plan;
	}
	Codec_SortKeys(workspace->graph_link_keys, (size_t)num_links);
	for (i = 1U; i < num_links; i++)
		if (workspace->graph_link_keys[i - 1U] ==
		    workspace->graph_link_keys[i])
			return Codec_Diagnostic(RLW_DUPLICATE_LINK);
	for (i = 0U; i < num_links; i++)
	{
		const sg_rune_codec_link_t *link = &links[i];
		int from_water;
		int to_water;
		int policy_action = SG_ActionMechanismLinkPolicyAction(link->action);

		if (((uint16_t)seeds[link->source].flags &
		     SG_RUNE_CODEC_SEED_TOMBSTONE) != 0U ||
		    ((uint16_t)seeds[link->destination].flags &
		     SG_RUNE_CODEC_SEED_TOMBSTONE) != 0U)
			return Codec_Diagnostic(RLW_BAD_ROUTE_OWNERSHIP);
		workspace->graph_source_marks[link->source] = 1U;
		from_water = ((uint16_t)seeds[link->source].flags &
			SG_RUNE_CODEC_SEED_WATER) != 0U;
		to_water = ((uint16_t)seeds[link->destination].flags &
			SG_RUNE_CODEC_SEED_WATER) != 0U;
		if (!SG_ActionEndpointAllowed(policy_action, from_water, to_water))
			return Codec_Diagnostic(RLW_BAD_LINK_RECORD);
	}
	for (i = 0U; i < num_seeds; i++)
	{
		int tombstone = ((uint16_t)seeds[i].flags &
			SG_RUNE_CODEC_SEED_TOMBSTONE) != 0U;
		int has_outgoing = workspace->graph_source_marks[i] != 0U;

		if (tombstone == has_outgoing)
			return Codec_Diagnostic(RLW_BAD_ROUTE_OWNERSHIP);
	}
	return RLCODEC_OK;
}

static int Codec_AddSize(size_t *total, size_t count, size_t record_bytes)
{
	size_t bytes;

	if (!total || (record_bytes != 0U && count > SIZE_MAX / record_bytes))
		return 0;
	bytes = count * record_bytes;
	if (*total > SIZE_MAX - bytes)
		return 0;
	*total += bytes;
	return 1;
}

sg_rune_codec_diagnostic_t SG_RuneCodecFileSize(uint32_t num_seeds,
	uint32_t num_links, uint32_t num_nodes, uint32_t num_edges,
	uint32_t num_plans, uint32_t string_bytes, size_t *size_out)
{
	size_t total = SG_RUNE_CODEC_HEADER_BYTES;

	if (!size_out)
		return Codec_Diagnostic(RLW_INVALID_ARGUMENT);
	*size_out = 0;
	if (num_seeds == 0U || num_seeds > SG_RUNE_CODEC_MAX_SEEDS ||
	    num_links > SG_RUNE_CODEC_MAX_LINKS ||
	    num_nodes > SG_RUNE_CODEC_MAX_ACTIVATION_NODES ||
	    num_edges > SG_RUNE_CODEC_MAX_ACTIVATION_EDGES ||
	    num_plans > SG_RUNE_CODEC_MAX_ACTIVATION_PLANS ||
	    string_bytes == 0U || string_bytes > SG_RUNE_CODEC_MAX_STRING_BYTES ||
	    num_plans > num_links ||
	    (num_nodes == 0U && num_edges != 0U) ||
	    (num_nodes == 0U && num_plans != 0U) ||
	    (num_plans != 0U && num_nodes == 0U))
		return Codec_Diagnostic(RLW_BAD_COUNTS);
	if (!Codec_AddSize(&total, num_seeds, SG_RUNE_CODEC_SEED_BYTES) ||
	    !Codec_AddSize(&total, num_links, SG_RUNE_CODEC_LINK_BYTES) ||
	    !Codec_AddSize(&total, num_nodes, SG_RUNE_CODEC_ACTIVATION_NODE_BYTES) ||
	    !Codec_AddSize(&total, num_edges, SG_RUNE_CODEC_ACTIVATION_EDGE_BYTES) ||
	    !Codec_AddSize(&total, num_plans, SG_RUNE_CODEC_ACTIVATION_PLAN_BYTES) ||
	    !Codec_AddSize(&total, string_bytes, 1U))
		return Codec_Diagnostic(RLW_BAD_FILE_SIZE);
	*size_out = total;
	return RLCODEC_OK;
}

static sg_rune_codec_diagnostic_t Codec_ValidateHeaderFixed(
	const sg_rune_codec_header_t *header)
{
	size_t ignored;

	if (!header)
		return Codec_Diagnostic(RLW_INVALID_ARGUMENT);
	if (header->magic != SG_RUNE_CODEC_MAGIC)
		return Codec_Diagnostic(RLW_BAD_MAGIC);
	if (header->header_bytes != SG_RUNE_CODEC_HEADER_BYTES)
		return Codec_Diagnostic(RLW_BAD_HEADER_SIZE);
	if (header->seed_bytes != SG_RUNE_CODEC_SEED_BYTES)
		return Codec_Diagnostic(RLW_BAD_SEED_SIZE);
	if (header->link_bytes != SG_RUNE_CODEC_LINK_BYTES)
		return Codec_Diagnostic(RLW_BAD_LINK_SIZE);
	if (header->activation_node_bytes != SG_RUNE_CODEC_ACTIVATION_NODE_BYTES ||
	    header->activation_edge_bytes != SG_RUNE_CODEC_ACTIVATION_EDGE_BYTES ||
	    header->activation_plan_bytes != SG_RUNE_CODEC_ACTIVATION_PLAN_BYTES)
		return RLCODEC_BAD_MECHANISM_CONTRACT;
	if (header->mechanism_contract_crc32 !=
	    SG_RUNE_MECHANISM_CONTRACT_CRC32 ||
	    header->num_inventory_edges > header->num_activation_edges)
		return RLCODEC_BAD_MECHANISM_CONTRACT;
	return SG_RuneCodecFileSize(header->num_seeds, header->num_links,
		header->num_activation_nodes, header->num_activation_edges,
		header->num_activation_plans, header->string_bytes, &ignored);
}

static sg_rune_codec_diagnostic_t Codec_ValidateHeaderSemantic(
	const sg_rune_codec_header_t *header)
{
	if (!header)
		return Codec_Diagnostic(RLW_INVALID_ARGUMENT);
	if (!Codec_MapNameValid(header->map_name))
		return Codec_Diagnostic(RLW_BAD_MAPNAME);
	if (header->action_contract_crc32 != SG_RUNE_ACTION_CONTRACT_CRC32)
		return Codec_Diagnostic(RLW_BAD_ACTION_CONTRACT);
	return Codec_ValidatePhysics(header->physics_flags, header->gravity,
		header->airaccelerate, header->maxvelocity,
		header->pmove_substep_ms, header->server_frame_ms,
		header->host_physics_id);
}

sg_rune_codec_diagnostic_t SG_RuneCodecHeaderCRC32(
	const unsigned char *encoded, size_t encoded_size, uint32_t *crc_out)
{
	static const unsigned char zero_crc[4] = { 0, 0, 0, 0 };
	uint32_t state;

	if (!encoded || !crc_out)
		return Codec_Diagnostic(RLW_INVALID_ARGUMENT);
	*crc_out = 0U;
	if (encoded_size != SG_RUNE_CODEC_HEADER_BYTES)
		return Codec_Diagnostic(RLW_BAD_HEADER_SIZE);
	state = SG_CRC32Init();
	if (!SG_CRC32Update(&state, encoded, SG_RUNE_CODEC_HEADER_CRC_OFFSET) ||
	    !SG_CRC32Update(&state, zero_crc, sizeof(zero_crc)) ||
	    !SG_CRC32Update(&state,
	        encoded + SG_RUNE_CODEC_HEADER_CRC_OFFSET + sizeof(zero_crc),
	        SG_RUNE_CODEC_HEADER_BYTES - SG_RUNE_CODEC_HEADER_CRC_OFFSET -
	        sizeof(zero_crc)))
		return Codec_Diagnostic(RLW_INVALID_ARGUMENT);
	*crc_out = SG_CRC32Final(state);
	return RLCODEC_OK;
}

static void Codec_EncodeHeaderFields(const sg_rune_codec_header_t *header,
	unsigned char out[SG_RUNE_CODEC_HEADER_BYTES])
{
	memset(out, 0, SG_RUNE_CODEC_HEADER_BYTES);
	Codec_PutU32(out + 0, header->magic);
	Codec_PutU16(out + Codec_RESERVED_ZERO_OFFSET, 0U);
	Codec_PutU16(out + 6, header->header_bytes);
	Codec_PutU16(out + 8, header->seed_bytes);
	Codec_PutU16(out + 10, header->link_bytes);
	Codec_PutU32(out + 12, header->num_seeds);
	Codec_PutU32(out + 16, header->num_links);
	Codec_PutU32(out + Codec_PAYLOAD_CRC_OFFSET, header->payload_crc32);
	Codec_PutU32(out + 24, header->bsp_checksum);
	Codec_PutU32(out + 28, header->entity_crc32);
	Codec_PutU32(out + 32, header->action_contract_crc32);
	Codec_PutU32(out + 36, header->physics_flags);
	Codec_PutFloat(out + 40, header->gravity);
	Codec_PutFloat(out + 44, header->airaccelerate);
	Codec_PutFloat(out + 48, header->maxvelocity);
	Codec_PutU16(out + 52, header->pmove_substep_ms);
	Codec_PutU16(out + 54, header->server_frame_ms);
	Codec_PutU32(out + 56, header->host_physics_id);
	Codec_PutU32(out + SG_RUNE_CODEC_HEADER_CRC_OFFSET, 0U);
	memcpy(out + Codec_MAP_OFFSET, header->map_name,
		SG_RUNE_CODEC_MAP_NAME_BYTES);
	Codec_PutU16(out + Codec_EXTENSION_OFFSET + 0,
		header->activation_node_bytes);
	Codec_PutU16(out + Codec_EXTENSION_OFFSET + 2,
		header->activation_edge_bytes);
	Codec_PutU16(out + Codec_EXTENSION_OFFSET + 4,
		header->activation_plan_bytes);
	Codec_PutU16(out + Codec_EXTENSION_OFFSET + 6, 0U);
	Codec_PutU32(out + Codec_EXTENSION_OFFSET + 8,
		header->num_activation_nodes);
	Codec_PutU32(out + Codec_EXTENSION_OFFSET + 12,
		header->num_activation_edges);
	Codec_PutU32(out + Codec_EXTENSION_OFFSET + 16,
		header->num_activation_plans);
	Codec_PutU32(out + Codec_EXTENSION_OFFSET + 20, header->string_bytes);
	Codec_PutU32(out + Codec_EXTENSION_OFFSET + 24,
		header->mechanism_contract_crc32);
	Codec_PutU32(out + Codec_EXTENSION_OFFSET + 28,
		header->num_inventory_edges);
}

sg_rune_codec_diagnostic_t SG_RuneCodecEncodeHeader(
	const sg_rune_codec_header_t *header, unsigned char *encoded,
	size_t encoded_size)
{
	unsigned char raw[SG_RUNE_CODEC_HEADER_BYTES];
	uint32_t crc;
	sg_rune_codec_diagnostic_t diagnostic;

	if (!header || !encoded)
		return Codec_Diagnostic(RLW_INVALID_ARGUMENT);
	if (encoded_size != SG_RUNE_CODEC_HEADER_BYTES)
		return Codec_Diagnostic(RLW_BAD_HEADER_SIZE);
	diagnostic = Codec_ValidateHeaderFixed(header);
	if (diagnostic != RLCODEC_OK)
		return diagnostic;
	diagnostic = Codec_ValidateHeaderSemantic(header);
	if (diagnostic != RLCODEC_OK)
		return diagnostic;
	Codec_EncodeHeaderFields(header, raw);
	diagnostic = SG_RuneCodecHeaderCRC32(raw, sizeof(raw), &crc);
	if (diagnostic != RLCODEC_OK)
		return diagnostic;
	Codec_PutU32(raw + SG_RUNE_CODEC_HEADER_CRC_OFFSET, crc);
	memcpy(encoded, raw, sizeof(raw));
	return RLCODEC_OK;
}

sg_rune_codec_diagnostic_t SG_RuneCodecDecodeHeader(
	const unsigned char *encoded, size_t encoded_size,
	sg_rune_codec_header_t *header_out)
{
	uint32_t computed_crc;
	sg_rune_codec_diagnostic_t diagnostic;

	if (!encoded || !header_out)
		return Codec_Diagnostic(RLW_INVALID_ARGUMENT);
	if (encoded_size != SG_RUNE_CODEC_HEADER_BYTES)
		return Codec_Diagnostic(RLW_BAD_HEADER_SIZE);
	memset(header_out, 0, sizeof(*header_out));
	header_out->magic = Codec_GetU32(encoded + 0);
	if (Codec_GetU16(encoded + Codec_RESERVED_ZERO_OFFSET) != 0U)
		return RLCODEC_NONZERO_RESERVED;
	header_out->header_bytes = Codec_GetU16(encoded + 6);
	header_out->seed_bytes = Codec_GetU16(encoded + 8);
	header_out->link_bytes = Codec_GetU16(encoded + 10);
	header_out->num_seeds = Codec_GetU32(encoded + 12);
	header_out->num_links = Codec_GetU32(encoded + 16);
	header_out->payload_crc32 = Codec_GetU32(encoded + Codec_PAYLOAD_CRC_OFFSET);
	header_out->bsp_checksum = Codec_GetU32(encoded + 24);
	header_out->entity_crc32 = Codec_GetU32(encoded + 28);
	header_out->action_contract_crc32 = Codec_GetU32(encoded + 32);
	header_out->physics_flags = Codec_GetU32(encoded + 36);
	header_out->gravity = Codec_GetFloat(encoded + 40);
	header_out->airaccelerate = Codec_GetFloat(encoded + 44);
	header_out->maxvelocity = Codec_GetFloat(encoded + 48);
	header_out->pmove_substep_ms = Codec_GetU16(encoded + 52);
	header_out->server_frame_ms = Codec_GetU16(encoded + 54);
	header_out->host_physics_id = Codec_GetU32(encoded + 56);
	header_out->header_crc32 = Codec_GetU32(
		encoded + SG_RUNE_CODEC_HEADER_CRC_OFFSET);
	memcpy(header_out->map_name, encoded + Codec_MAP_OFFSET,
		SG_RUNE_CODEC_MAP_NAME_BYTES);
	header_out->activation_node_bytes = Codec_GetU16(
		encoded + Codec_EXTENSION_OFFSET + 0);
	header_out->activation_edge_bytes = Codec_GetU16(
		encoded + Codec_EXTENSION_OFFSET + 2);
	header_out->activation_plan_bytes = Codec_GetU16(
		encoded + Codec_EXTENSION_OFFSET + 4);
	if (Codec_GetU16(encoded + Codec_EXTENSION_OFFSET + 6) != 0U)
		return RLCODEC_NONZERO_RESERVED;
	header_out->num_activation_nodes = Codec_GetU32(
		encoded + Codec_EXTENSION_OFFSET + 8);
	header_out->num_activation_edges = Codec_GetU32(
		encoded + Codec_EXTENSION_OFFSET + 12);
	header_out->num_activation_plans = Codec_GetU32(
		encoded + Codec_EXTENSION_OFFSET + 16);
	header_out->string_bytes = Codec_GetU32(
		encoded + Codec_EXTENSION_OFFSET + 20);
	header_out->mechanism_contract_crc32 = Codec_GetU32(
		encoded + Codec_EXTENSION_OFFSET + 24);
	header_out->num_inventory_edges = Codec_GetU32(
		encoded + Codec_EXTENSION_OFFSET + 28);

	diagnostic = Codec_ValidateHeaderFixed(header_out);
	if (diagnostic != RLCODEC_OK)
		return diagnostic;
	diagnostic = SG_RuneCodecHeaderCRC32(encoded, encoded_size, &computed_crc);
	if (diagnostic != RLCODEC_OK)
		return diagnostic;
	if (computed_crc != header_out->header_crc32)
		return Codec_Diagnostic(RLW_BAD_HEADER_CRC);
	return Codec_ValidateHeaderSemantic(header_out);
}

sg_rune_codec_diagnostic_t SG_RuneCodecEncodeSeed(
	const sg_rune_codec_seed_t *seed, unsigned char *encoded,
	size_t encoded_size)
{
	sg_rune_codec_diagnostic_t diagnostic;

	if (!seed || !encoded)
		return Codec_Diagnostic(RLW_INVALID_ARGUMENT);
	if (encoded_size != SG_RUNE_CODEC_SEED_BYTES)
		return Codec_Diagnostic(RLW_BAD_SEED_SIZE);
	diagnostic = Codec_ValidateSeedFields(seed);
	if (diagnostic != RLCODEC_OK)
		return diagnostic;
	Codec_PutFloat(encoded + 0, seed->origin[0]);
	Codec_PutFloat(encoded + 4, seed->origin[1]);
	Codec_PutFloat(encoded + 8, seed->origin[2]);
	Codec_PutI16(encoded + 12, seed->area_hint);
	Codec_PutI16(encoded + 14, seed->flags);
	return RLCODEC_OK;
}

sg_rune_codec_diagnostic_t SG_RuneCodecDecodeSeed(
	const unsigned char *encoded, size_t encoded_size,
	sg_rune_codec_seed_t *seed_out)
{
	sg_rune_codec_seed_t seed;
	sg_rune_codec_diagnostic_t diagnostic;

	if (!encoded || !seed_out)
		return Codec_Diagnostic(RLW_INVALID_ARGUMENT);
	if (encoded_size != SG_RUNE_CODEC_SEED_BYTES)
		return Codec_Diagnostic(RLW_BAD_SEED_SIZE);
	seed.origin[0] = Codec_GetFloat(encoded + 0);
	seed.origin[1] = Codec_GetFloat(encoded + 4);
	seed.origin[2] = Codec_GetFloat(encoded + 8);
	seed.area_hint = Codec_GetI16(encoded + 12);
	seed.flags = Codec_GetI16(encoded + 14);
	diagnostic = Codec_ValidateSeedFields(&seed);
	if (diagnostic != RLCODEC_OK)
		return diagnostic;
	*seed_out = seed;
	return RLCODEC_OK;
}

sg_rune_codec_diagnostic_t SG_RuneCodecEncodeLink(
	const sg_rune_codec_link_t *link, unsigned char *encoded,
	size_t encoded_size)
{
	sg_rune_codec_diagnostic_t diagnostic;

	if (!link || !encoded)
		return Codec_Diagnostic(RLW_INVALID_ARGUMENT);
	if (encoded_size != SG_RUNE_CODEC_LINK_BYTES)
		return Codec_Diagnostic(RLW_BAD_LINK_SIZE);
	diagnostic = Codec_ValidateLinkFields(link);
	if (diagnostic != RLCODEC_OK)
		return diagnostic;
	Codec_PutU32(encoded + 0, link->source);
	Codec_PutU32(encoded + 4, link->destination);
	encoded[8] = link->action;
	encoded[9] = link->provenance;
	encoded[10] = link->min_speed;
	encoded[11] = link->heading;
	encoded[12] = link->heading_slack;
	encoded[13] = link->exit_speed;
	Codec_PutI16(encoded + 14, link->cost_ms);
	Codec_PutFloat(encoded + 16, link->suffix_anchor[0]);
	Codec_PutFloat(encoded + 20, link->suffix_anchor[1]);
	Codec_PutFloat(encoded + 24, link->suffix_anchor[2]);
	Codec_PutFloat(encoded + 28, link->mechanism_anchor[0]);
	Codec_PutFloat(encoded + 32, link->mechanism_anchor[1]);
	Codec_PutFloat(encoded + 36, link->mechanism_anchor[2]);
	Codec_PutU16(encoded + 40, link->sweep_clear_ms);
	encoded[42] = link->mode;
	encoded[43] = link->reserved;
	Codec_PutU32(encoded + SG_RUNE_CODEC_ACTIVATION_PLAN_OFFSET,
		link->activation_plan);
	return RLCODEC_OK;
}

sg_rune_codec_diagnostic_t SG_RuneCodecDecodeLink(
	const unsigned char *encoded, size_t encoded_size,
	sg_rune_codec_link_t *link_out)
{
	sg_rune_codec_link_t link;
	sg_rune_codec_diagnostic_t diagnostic;

	if (!encoded || !link_out)
		return Codec_Diagnostic(RLW_INVALID_ARGUMENT);
	if (encoded_size != SG_RUNE_CODEC_LINK_BYTES)
		return Codec_Diagnostic(RLW_BAD_LINK_SIZE);
	memset(&link, 0, sizeof(link));
	link.source = Codec_GetU32(encoded + 0);
	link.destination = Codec_GetU32(encoded + 4);
	link.action = encoded[8];
	link.provenance = encoded[9];
	link.min_speed = encoded[10];
	link.heading = encoded[11];
	link.heading_slack = encoded[12];
	link.exit_speed = encoded[13];
	link.cost_ms = Codec_GetI16(encoded + 14);
	link.suffix_anchor[0] = Codec_GetFloat(encoded + 16);
	link.suffix_anchor[1] = Codec_GetFloat(encoded + 20);
	link.suffix_anchor[2] = Codec_GetFloat(encoded + 24);
	link.mechanism_anchor[0] = Codec_GetFloat(encoded + 28);
	link.mechanism_anchor[1] = Codec_GetFloat(encoded + 32);
	link.mechanism_anchor[2] = Codec_GetFloat(encoded + 36);
	link.sweep_clear_ms = Codec_GetU16(encoded + 40);
	link.mode = encoded[42];
	link.reserved = encoded[43];
	link.activation_plan = Codec_GetU32(
		encoded + SG_RUNE_CODEC_ACTIVATION_PLAN_OFFSET);
	diagnostic = Codec_ValidateLinkFields(&link);
	if (diagnostic != RLCODEC_OK)
		return diagnostic;
	*link_out = link;
	return RLCODEC_OK;
}

static int Codec_KeyValueValid(uint32_t key)
{
	return key != 0U && key != SG_RUNE_CODEC_NO_KEY;
}

static int Codec_OptionalKeyValid(uint32_t key)
{
	return key == SG_RUNE_CODEC_NO_KEY || Codec_KeyValueValid(key);
}

static int Codec_TouchCallbackValid(uint16_t callback)
{
	return callback == SG_RUNE_CODEC_CALLBACK_NONE ||
	       callback == SG_RUNE_CODEC_CALLBACK_TOUCH_MULTI ||
	       callback == SG_RUNE_CODEC_CALLBACK_TOUCH_DOOR_TRIGGER ||
	       callback == SG_RUNE_CODEC_CALLBACK_BUTTON_TOUCH ||
	       callback == SG_RUNE_CODEC_CALLBACK_TOUCH_PLAT_CENTER ||
	       callback == SG_RUNE_CODEC_CALLBACK_TRIGGER_PUSH_TOUCH ||
	       callback == SG_RUNE_CODEC_CALLBACK_TELEPORTER_TOUCH ||
	       callback == SG_RUNE_CODEC_CALLBACK_PATH_CORNER_TOUCH ||
	       callback == SG_RUNE_CODEC_CALLBACK_TOUCH_ITEM ||
	       callback == SG_RUNE_CODEC_CALLBACK_UNKNOWN;
}

static int Codec_UseCallbackValid(uint16_t callback)
{
	return callback == SG_RUNE_CODEC_CALLBACK_NONE ||
	       callback == SG_RUNE_CODEC_CALLBACK_USE_MULTI ||
	       callback == SG_RUNE_CODEC_CALLBACK_BUTTON_USE ||
	       callback == SG_RUNE_CODEC_CALLBACK_USE_TRIGGER_RELAY ||
	       callback == SG_RUNE_CODEC_CALLBACK_USE_DOOR ||
	       callback == SG_RUNE_CODEC_CALLBACK_TRIGGER_ENABLE ||
	       callback == SG_RUNE_CODEC_CALLBACK_USE_PLAT ||
	       callback == SG_RUNE_CODEC_CALLBACK_TRAIN_USE ||
	       callback == SG_RUNE_CODEC_CALLBACK_TRIGGER_ELEVATOR_USE ||
	       callback == SG_RUNE_CODEC_CALLBACK_SECRET_DOOR_USE ||
	       callback == SG_RUNE_CODEC_CALLBACK_USE_TARGET_SPEAKER ||
	       callback == SG_RUNE_CODEC_CALLBACK_USE_AREAPORTAL ||
	       callback == SG_RUNE_CODEC_CALLBACK_UNKNOWN;
}

static int Codec_ThinkCallbackValid(uint16_t callback)
{
	return callback == SG_RUNE_CODEC_CALLBACK_NONE ||
	       callback == SG_RUNE_CODEC_CALLBACK_THINK_MULTI_WAIT ||
	       callback == SG_RUNE_CODEC_CALLBACK_THINK_BUTTON_WAIT ||
	       callback == SG_RUNE_CODEC_CALLBACK_THINK_CALC_MOVE_SPEED ||
	       callback == SG_RUNE_CODEC_CALLBACK_THINK_SPAWN_DOOR_TRIGGER ||
	       callback == SG_RUNE_CODEC_CALLBACK_PLAT_GO_DOWN ||
	       callback == SG_RUNE_CODEC_CALLBACK_FUNC_TRAIN_FIND ||
	       callback == SG_RUNE_CODEC_CALLBACK_TRAIN_NEXT ||
	       callback == SG_RUNE_CODEC_CALLBACK_TRAIN_WAIT ||
	       callback == SG_RUNE_CODEC_CALLBACK_TRIGGER_ELEVATOR_INIT ||
	       callback == SG_RUNE_CODEC_CALLBACK_THINK_DELAY ||
	       callback == SG_RUNE_CODEC_CALLBACK_UNKNOWN;
}

static int Codec_BlockedCallbackValid(uint16_t callback)
{
	return callback == SG_RUNE_CODEC_CALLBACK_NONE ||
	       callback == SG_RUNE_CODEC_CALLBACK_BLOCKED_DOOR ||
	       callback == SG_RUNE_CODEC_CALLBACK_BLOCKED_PLAT ||
	       callback == SG_RUNE_CODEC_CALLBACK_BLOCKED_TRAIN ||
	       callback == SG_RUNE_CODEC_CALLBACK_SECRET_DOOR_BLOCKED ||
	       callback == SG_RUNE_CODEC_CALLBACK_UNKNOWN;
}

static int Codec_NodeHasUnknownCallback(
	const sg_rune_codec_activation_node_t *node)
{
	return node && (node->touch_callback == SG_RUNE_CODEC_CALLBACK_UNKNOWN ||
		node->use_callback == SG_RUNE_CODEC_CALLBACK_UNKNOWN ||
		node->think_callback == SG_RUNE_CODEC_CALLBACK_UNKNOWN ||
		node->blocked_callback == SG_RUNE_CODEC_CALLBACK_UNKNOWN);
}

static sg_rune_codec_diagnostic_t Codec_ValidateNodeFields(
	const sg_rune_codec_activation_node_t *node)
{
	int i;
	int frame_complete;

	if (!node || !Codec_KeyValueValid(node->key) ||
	    node->kind < SG_RUNE_CODEC_NODE_TRIGGER ||
	    node->kind > SG_RUNE_CODEC_NODE_AREAPORTAL ||
	    (node->flags & ~SG_RUNE_CODEC_NODE_FLAG_MASK) != 0U ||
	    !Codec_OptionalKeyValid(node->owner_key) ||
	    !Codec_OptionalKeyValid(node->team_master_key) ||
	    !Codec_TouchCallbackValid(node->touch_callback) ||
	    !Codec_UseCallbackValid(node->use_callback) ||
	    !Codec_ThinkCallbackValid(node->think_callback) ||
	    !Codec_BlockedCallbackValid(node->blocked_callback) ||
	    node->speed_q8 > SG_RUNE_CODEC_MAX_Q8 ||
	    node->accel_q8 > SG_RUNE_CODEC_MAX_Q8 ||
	    node->decel_q8 > SG_RUNE_CODEC_MAX_Q8)
		return RLCODEC_BAD_ACTIVATION_NODE;
	for (i = 0; i < 3; i++)
		if (node->absmin_q8[i] > node->absmax_q8[i] ||
		    !isfinite(node->push_velocity[i]) ||
		    (node->kind != SG_RUNE_CODEC_NODE_PUSH &&
		     memcmp(&node->push_velocity[i], "\0\0\0\0", 4U) != 0))
			return RLCODEC_BAD_ACTIVATION_NODE;
	if (node->kind == SG_RUNE_CODEC_NODE_PUSH &&
	    node->push_velocity[0] == 0.0f &&
	    node->push_velocity[1] == 0.0f &&
	    node->push_velocity[2] == 0.0f)
		return RLCODEC_BAD_ACTIVATION_NODE;
	if ((node->flags & SG_RUNE_CODEC_NODEF_SYNTHETIC) != 0U &&
	    node->owner_key == SG_RUNE_CODEC_NO_KEY)
		return RLCODEC_BAD_ACTIVATION_NODE;
	frame_complete = (node->flags &
		SG_RUNE_CODEC_NODEF_FRAME_COMPLETE_MOVER) != 0U;
	if (frame_complete &&
	    (node->kind != SG_RUNE_CODEC_NODE_BUTTON ||
	     (node->flags & (SG_RUNE_CODEC_NODEF_MOVER |
	      SG_RUNE_CODEC_NODEF_SHOOTABLE)) !=
	         (SG_RUNE_CODEC_NODEF_MOVER | SG_RUNE_CODEC_NODEF_SHOOTABLE) ||
	     (node->flags & (SG_RUNE_CODEC_NODEF_SYNTHETIC |
	      SG_RUNE_CODEC_NODEF_INVENTORY_ONLY)) != 0U ||
	     node->speed_q8 == 0U || node->accel_q8 != node->speed_q8 ||
	     node->decel_q8 != node->speed_q8 || node->speed_q8 % 10U != 0U))
		return RLCODEC_BAD_ACTIVATION_NODE;
	if (Codec_NodeHasUnknownCallback(node) &&
	    (node->flags & SG_RUNE_CODEC_NODEF_INVENTORY_ONLY) == 0U)
		return RLCODEC_BAD_ACTIVATION_NODE;
	if (node->kind == SG_RUNE_CODEC_NODE_DOOR_MEMBER &&
	    (node->team_master_key == SG_RUNE_CODEC_NO_KEY ||
	     node->team_master_key == node->key))
		return RLCODEC_BAD_ACTIVATION_NODE;
	if (node->kind == SG_RUNE_CODEC_NODE_DOOR_MASTER &&
	    node->team_master_key != node->key)
		return RLCODEC_BAD_ACTIVATION_NODE;
	return RLCODEC_OK;
}

sg_rune_codec_diagnostic_t SG_RuneCodecEncodeActivationNode(
	const sg_rune_codec_activation_node_t *node, unsigned char *encoded,
	size_t encoded_size)
{
	unsigned char raw[SG_RUNE_CODEC_ACTIVATION_NODE_BYTES];
	sg_rune_codec_diagnostic_t diagnostic;
	int i;

	if (!node || !encoded)
		return Codec_Diagnostic(RLW_INVALID_ARGUMENT);
	if (encoded_size != SG_RUNE_CODEC_ACTIVATION_NODE_BYTES)
		return Codec_Diagnostic(RLW_BAD_FILE_SIZE);
	diagnostic = Codec_ValidateNodeFields(node);
	if (diagnostic != RLCODEC_OK)
		return diagnostic;
	Codec_PutU32(raw + 0, node->key);
	Codec_PutU16(raw + 4, node->kind);
	Codec_PutU16(raw + 6, node->flags);
	Codec_PutU32(raw + 8, node->classname_offset);
	Codec_PutU32(raw + 12, node->target_offset);
	Codec_PutU32(raw + 16, node->targetname_offset);
	Codec_PutU32(raw + 20, node->killtarget_offset);
	Codec_PutU32(raw + 24, node->owner_key);
	Codec_PutU32(raw + 28, node->team_master_key);
	Codec_PutU32(raw + 32, node->spawnflags);
	Codec_PutU16(raw + 36, node->touch_callback);
	Codec_PutU16(raw + 38, node->use_callback);
	Codec_PutU16(raw + 40, node->think_callback);
	Codec_PutU16(raw + 42, node->blocked_callback);
	Codec_PutI32(raw + 44, node->delay_ms);
	Codec_PutI32(raw + 48, node->wait_ms);
	Codec_PutU32(raw + 52, node->speed_q8);
	Codec_PutU32(raw + 56, node->accel_q8);
	Codec_PutU32(raw + 60, node->decel_q8);
	for (i = 0; i < 3; i++)
	{
		Codec_PutI16(raw + 64 + (size_t)i * 2U, node->absmin_q8[i]);
		Codec_PutI16(raw + 70 + (size_t)i * 2U, node->absmax_q8[i]);
	}
	Codec_PutU32(raw + 76, node->path_target_offset);
	for (i = 0; i < 3; i++)
		Codec_PutFloat(raw + 80 + (size_t)i * 4U,
			node->push_velocity[i]);
	memcpy(encoded, raw, sizeof(raw));
	return RLCODEC_OK;
}

sg_rune_codec_diagnostic_t SG_RuneCodecDecodeActivationNode(
	const unsigned char *encoded, size_t encoded_size,
	sg_rune_codec_activation_node_t *node_out)
{
	sg_rune_codec_activation_node_t node;
	sg_rune_codec_diagnostic_t diagnostic;
	int i;

	if (!encoded || !node_out)
		return Codec_Diagnostic(RLW_INVALID_ARGUMENT);
	if (encoded_size != SG_RUNE_CODEC_ACTIVATION_NODE_BYTES)
		return Codec_Diagnostic(RLW_BAD_FILE_SIZE);
	memset(&node, 0, sizeof(node));
	node.key = Codec_GetU32(encoded + 0);
	node.kind = Codec_GetU16(encoded + 4);
	node.flags = Codec_GetU16(encoded + 6);
	node.classname_offset = Codec_GetU32(encoded + 8);
	node.target_offset = Codec_GetU32(encoded + 12);
	node.targetname_offset = Codec_GetU32(encoded + 16);
	node.killtarget_offset = Codec_GetU32(encoded + 20);
	node.owner_key = Codec_GetU32(encoded + 24);
	node.team_master_key = Codec_GetU32(encoded + 28);
	node.spawnflags = Codec_GetU32(encoded + 32);
	node.touch_callback = Codec_GetU16(encoded + 36);
	node.use_callback = Codec_GetU16(encoded + 38);
	node.think_callback = Codec_GetU16(encoded + 40);
	node.blocked_callback = Codec_GetU16(encoded + 42);
	node.delay_ms = Codec_GetI32(encoded + 44);
	node.wait_ms = Codec_GetI32(encoded + 48);
	node.speed_q8 = Codec_GetU32(encoded + 52);
	node.accel_q8 = Codec_GetU32(encoded + 56);
	node.decel_q8 = Codec_GetU32(encoded + 60);
	for (i = 0; i < 3; i++)
	{
		node.absmin_q8[i] = Codec_GetI16(encoded + 64 + (size_t)i * 2U);
		node.absmax_q8[i] = Codec_GetI16(encoded + 70 + (size_t)i * 2U);
	}
	node.path_target_offset = Codec_GetU32(encoded + 76);
	for (i = 0; i < 3; i++)
		node.push_velocity[i] = Codec_GetFloat(encoded + 80 + (size_t)i * 4U);
	diagnostic = Codec_ValidateNodeFields(&node);
	if (diagnostic != RLCODEC_OK)
		return diagnostic;
	*node_out = node;
	return RLCODEC_OK;
}

static sg_rune_codec_diagnostic_t Codec_ValidateEdgeFields(
	const sg_rune_codec_activation_edge_t *edge)
{
	if (!edge || !Codec_KeyValueValid(edge->from_key) ||
	    !Codec_KeyValueValid(edge->to_key) ||
	    edge->kind < SG_RUNE_CODEC_EDGE_TARGET ||
	    edge->kind > SG_RUNE_CODEC_EDGE_ROUTE_TARGET)
		return RLCODEC_BAD_ACTIVATION_EDGE;
	return RLCODEC_OK;
}

sg_rune_codec_diagnostic_t SG_RuneCodecEncodeActivationEdge(
	const sg_rune_codec_activation_edge_t *edge, unsigned char *encoded,
	size_t encoded_size)
{
	sg_rune_codec_diagnostic_t diagnostic;

	if (!edge || !encoded)
		return Codec_Diagnostic(RLW_INVALID_ARGUMENT);
	if (encoded_size != SG_RUNE_CODEC_ACTIVATION_EDGE_BYTES)
		return Codec_Diagnostic(RLW_BAD_FILE_SIZE);
	diagnostic = Codec_ValidateEdgeFields(edge);
	if (diagnostic != RLCODEC_OK)
		return diagnostic;
	Codec_PutU32(encoded + 0, edge->from_key);
	Codec_PutU32(encoded + 4, edge->to_key);
	Codec_PutU16(encoded + 8, edge->kind);
	Codec_PutU16(encoded + 10, edge->ordinal);
	Codec_PutU32(encoded + 12, edge->delay_ms);
	return RLCODEC_OK;
}

sg_rune_codec_diagnostic_t SG_RuneCodecDecodeActivationEdge(
	const unsigned char *encoded, size_t encoded_size,
	sg_rune_codec_activation_edge_t *edge_out)
{
	sg_rune_codec_activation_edge_t edge;
	sg_rune_codec_diagnostic_t diagnostic;

	if (!encoded || !edge_out)
		return Codec_Diagnostic(RLW_INVALID_ARGUMENT);
	if (encoded_size != SG_RUNE_CODEC_ACTIVATION_EDGE_BYTES)
		return Codec_Diagnostic(RLW_BAD_FILE_SIZE);
	edge.from_key = Codec_GetU32(encoded + 0);
	edge.to_key = Codec_GetU32(encoded + 4);
	edge.kind = Codec_GetU16(encoded + 8);
	edge.ordinal = Codec_GetU16(encoded + 10);
	edge.delay_ms = Codec_GetU32(encoded + 12);
	diagnostic = Codec_ValidateEdgeFields(&edge);
	if (diagnostic != RLCODEC_OK)
		return diagnostic;
	*edge_out = edge;
	return RLCODEC_OK;
}

static sg_rune_codec_diagnostic_t Codec_ValidatePlanFields(
	const sg_rune_codec_activation_plan_t *plan)
{
	uint16_t expected_flags;

	if (!plan)
		return RLCODEC_BAD_ACTIVATION_PLAN;
	expected_flags = SG_MechanismControllerPlanFlags(plan->controller_kind);
	if (expected_flags == 0U)
		return RLCODEC_BAD_ACTIVATION_PLAN;
	if (!Codec_KeyValueValid(plan->entry_key) ||
	    plan->num_edges > SG_RUNE_CODEC_MAX_PLAN_EDGES ||
	    (plan->flags & ~SG_RUNE_CODEC_PLAN_FLAG_MASK) != 0U ||
	    plan->flags != expected_flags ||
	    plan->expected_members == 0U ||
	    plan->expected_members > SG_RUNE_CODEC_MAX_TEAM_MEMBERS ||
	    plan->cooldown_ms > (uint32_t)SG_RUNE_CODEC_MAX_TIME_MS ||
	    plan->closure_crc32 == 0U)
		return RLCODEC_BAD_ACTIVATION_PLAN;
	if (plan->controller_kind == SG_RUNE_CODEC_CONTROLLER_PUSH)
	{
		if (plan->mover_key != SG_RUNE_CODEC_NO_KEY ||
		    plan->num_edges != 0U || plan->expected_members != 1U ||
		    plan->cooldown_ms != 0U)
			return RLCODEC_BAD_ACTIVATION_PLAN;
	}
	else if (!Codec_KeyValueValid(plan->mover_key) ||
	         (plan->entry_key == plan->mover_key &&
	          plan->controller_kind !=
	              SG_RUNE_CODEC_CONTROLLER_TRAIN_SHOOT) ||
	         plan->num_edges == 0U)
		return RLCODEC_BAD_ACTIVATION_PLAN;
	return RLCODEC_OK;
}

sg_rune_codec_diagnostic_t SG_RuneCodecEncodeActivationPlan(
	const sg_rune_codec_activation_plan_t *plan, unsigned char *encoded,
	size_t encoded_size)
{
	sg_rune_codec_diagnostic_t diagnostic;

	if (!plan || !encoded)
		return Codec_Diagnostic(RLW_INVALID_ARGUMENT);
	if (encoded_size != SG_RUNE_CODEC_ACTIVATION_PLAN_BYTES)
		return Codec_Diagnostic(RLW_BAD_FILE_SIZE);
	diagnostic = Codec_ValidatePlanFields(plan);
	if (diagnostic != RLCODEC_OK)
		return diagnostic;
	Codec_PutU32(encoded + 0, plan->entry_key);
	Codec_PutU32(encoded + 4, plan->mover_key);
	Codec_PutU32(encoded + 8, plan->first_edge);
	Codec_PutU32(encoded + 12, plan->num_edges);
	Codec_PutU16(encoded + 16, plan->controller_kind);
	Codec_PutU16(encoded + 18, 0U);
	Codec_PutU16(encoded + 20, plan->flags);
	Codec_PutU16(encoded + 22, plan->expected_members);
	Codec_PutU32(encoded + 24, plan->cooldown_ms);
	Codec_PutU32(encoded + 28, plan->closure_crc32);
	return RLCODEC_OK;
}

sg_rune_codec_diagnostic_t SG_RuneCodecDecodeActivationPlan(
	const unsigned char *encoded, size_t encoded_size,
	sg_rune_codec_activation_plan_t *plan_out)
{
	sg_rune_codec_activation_plan_t plan;
	sg_rune_codec_diagnostic_t diagnostic;

	if (!encoded || !plan_out)
		return Codec_Diagnostic(RLW_INVALID_ARGUMENT);
	if (encoded_size != SG_RUNE_CODEC_ACTIVATION_PLAN_BYTES)
		return Codec_Diagnostic(RLW_BAD_FILE_SIZE);
	plan.entry_key = Codec_GetU32(encoded + 0);
	plan.mover_key = Codec_GetU32(encoded + 4);
	plan.first_edge = Codec_GetU32(encoded + 8);
	plan.num_edges = Codec_GetU32(encoded + 12);
	plan.controller_kind = Codec_GetU16(encoded + 16);
	if (Codec_GetU16(encoded + 18) != 0U)
		return RLCODEC_NONZERO_RESERVED;
	plan.flags = Codec_GetU16(encoded + 20);
	plan.expected_members = Codec_GetU16(encoded + 22);
	plan.cooldown_ms = Codec_GetU32(encoded + 24);
	plan.closure_crc32 = Codec_GetU32(encoded + 28);
	diagnostic = Codec_ValidatePlanFields(&plan);
	if (diagnostic != RLCODEC_OK)
		return diagnostic;
	*plan_out = plan;
	return RLCODEC_OK;
}

sg_rune_codec_diagnostic_t SG_RuneCodecPlanClosureCRC32(
	const sg_rune_codec_activation_edge_t *edges, uint32_t first_edge,
	uint32_t num_edges, uint32_t total_edges, uint32_t *crc_out)
{
	unsigned char encoded[SG_RUNE_CODEC_ACTIVATION_EDGE_BYTES];
	uint32_t state;
	uint32_t i;
	sg_rune_codec_diagnostic_t diagnostic;

	if (!crc_out || (num_edges != 0U && !edges))
		return Codec_Diagnostic(RLW_INVALID_ARGUMENT);
	*crc_out = 0U;
	if (num_edges == 0U || num_edges > SG_RUNE_CODEC_MAX_PLAN_EDGES ||
	    first_edge > total_edges || num_edges > total_edges - first_edge)
		return RLCODEC_BAD_ACTIVATION_PLAN;
	state = SG_CRC32Init();
	for (i = 0; i < num_edges; i++)
	{
		diagnostic = SG_RuneCodecEncodeActivationEdge(
			&edges[first_edge + i], encoded, sizeof(encoded));
		if (diagnostic != RLCODEC_OK)
			return diagnostic;
		if (!SG_CRC32Update(&state, encoded, sizeof(encoded)))
			return Codec_Diagnostic(RLW_INVALID_ARGUMENT);
	}
	*crc_out = SG_CRC32Final(state);
	return RLCODEC_OK;
}

sg_rune_codec_diagnostic_t SG_RuneCodecPushClosureCRC32(uint32_t entry_key,
	const float push_velocity[3], uint32_t *crc_out)
{
	static const unsigned char domain[4] = { 'P', 'U', 'S', 'H' };
	unsigned char encoded[20];
	uint32_t state;
	int axis;

	if (!crc_out || !push_velocity || !Codec_KeyValueValid(entry_key))
		return Codec_Diagnostic(RLW_INVALID_ARGUMENT);
	*crc_out = 0U;
	memcpy(encoded, domain, sizeof(domain));
	Codec_PutU32(encoded + 4, entry_key);
	for (axis = 0; axis < 3; axis++)
	{
		if (!isfinite(push_velocity[axis]))
			return RLCODEC_BAD_ACTIVATION_NODE;
		Codec_PutFloat(encoded + 8 + (size_t)axis * 4U,
			push_velocity[axis]);
	}
	state = SG_CRC32Init();
	if (!SG_CRC32Update(&state, encoded, sizeof(encoded)))
		return Codec_Diagnostic(RLW_INVALID_ARGUMENT);
	*crc_out = SG_CRC32Final(state);
	return RLCODEC_OK;
}

static uint32_t Codec_FindNode(const sg_rune_codec_activation_node_t *nodes,
	uint32_t num_nodes, uint32_t key)
{
	uint32_t low = 0U;
	uint32_t high = num_nodes;

	while (low < high)
	{
		uint32_t middle = low + (high - low) / 2U;

		if (nodes[middle].key < key)
			low = middle + 1U;
		else
			high = middle;
	}
	if (low < num_nodes && nodes[low].key == key)
		return low;
	return UINT32_MAX;
}

static int Codec_StringOffsetValid(const unsigned char *strings,
	uint32_t string_bytes, uint32_t offset)
{
	uint32_t i;

	if (offset == 0U)
		return 1;
	if (!strings || offset >= string_bytes || strings[offset] == 0U ||
	    strings[offset - 1U] != 0U)
		return 0;
	for (i = offset; i < string_bytes; i++)
		if (strings[i] == 0U)
			return 1;
	return 0;
}

static int Codec_StringLess(const unsigned char *left,
	const unsigned char *right)
{
	while (*left != 0U && *right != 0U && *left == *right)
	{
		left++;
		right++;
	}
	return *left < *right;
}

static unsigned char Codec_AsciiFold(unsigned char value)
{
	return value >= (unsigned char)'A' && value <= (unsigned char)'Z'
		? (unsigned char)(value + ((unsigned char)'a' - (unsigned char)'A'))
		: value;
}

static int Codec_StringEqualFold(const unsigned char *left,
	const unsigned char *right)
{
	if (!left || !right)
		return 0;
	while (*left != 0U && *right != 0U &&
	       Codec_AsciiFold(*left) == Codec_AsciiFold(*right))
	{
		left++;
		right++;
	}
	return *left == 0U && *right == 0U;
}

static int Codec_StringEqualLiteral(const unsigned char *value,
	const char *literal)
{
	if (!value || !literal)
		return 0;
	while (*value != 0U && *literal != '\0' &&
	       *value == (unsigned char)*literal)
	{
		value++;
		literal++;
	}
	return *value == 0U && *literal == '\0';
}

static sg_rune_codec_diagnostic_t Codec_ValidateStringPool(
	const sg_rune_codec_activation_node_t *nodes, uint32_t num_nodes,
	const unsigned char *strings, uint32_t string_bytes,
	sg_rune_codec_workspace_t *workspace)
{
	uint32_t i;
	uint32_t offset;
	uint32_t previous = 0U;

	if (!strings || string_bytes == 0U ||
	    string_bytes > SG_RUNE_CODEC_MAX_STRING_BYTES || strings[0] != 0U)
		return RLCODEC_BAD_STRING_POOL;
	if (string_bytes > 1U &&
	    (!workspace->string_marks ||
	     workspace->string_mark_capacity < (size_t)string_bytes))
		return Codec_Diagnostic(RLW_ALLOCATION_FAILED);
	if (string_bytes > 1U)
		memset(workspace->string_marks, 0, (size_t)string_bytes);
	for (i = 0U; i < num_nodes; i++)
	{
		const uint32_t offsets[5] = {
			nodes[i].classname_offset,
			nodes[i].target_offset,
			nodes[i].targetname_offset,
			nodes[i].killtarget_offset,
			nodes[i].path_target_offset
		};
		uint32_t j;

		for (j = 0U; j < 5U; j++)
		{
			if (!Codec_StringOffsetValid(strings, string_bytes, offsets[j]))
				return RLCODEC_BAD_STRING_POOL;
			if (offsets[j] != 0U)
				workspace->string_marks[offsets[j]] = 1U;
		}
	}
	offset = 1U;
	while (offset < string_bytes)
	{
		uint32_t end = offset;

		if (strings[offset] == 0U)
			return RLCODEC_BAD_STRING_POOL;
		while (end < string_bytes && strings[end] != 0U)
			end++;
		if (end == string_bytes ||
		    (previous != 0U &&
		     !Codec_StringLess(strings + previous, strings + offset)) ||
		    workspace->string_marks[offset] == 0U)
			return RLCODEC_BAD_STRING_POOL;
		previous = offset;
		offset = end + 1U;
	}
	return RLCODEC_OK;
}

static int Codec_WorkspaceMechanismReady(const sg_rune_codec_workspace_t *workspace,
	uint32_t num_nodes, uint32_t num_edges, uint32_t num_plans)
{
	return workspace &&
	       (num_plans == 0U || (workspace->plan_references &&
	        workspace->plan_reference_capacity >= (size_t)num_plans)) &&
	       workspace->node_references &&
	       workspace->node_reference_capacity >= (size_t)num_nodes &&
	       workspace->node_heads &&
	       workspace->node_head_capacity >= (size_t)num_nodes &&
	       workspace->node_indegrees &&
	       workspace->node_indegree_capacity >= (size_t)num_nodes &&
	       workspace->node_generations &&
	       workspace->node_generation_capacity >= (size_t)num_nodes &&
	       workspace->node_touched &&
	       workspace->node_touched_capacity >= (size_t)num_nodes &&
	       workspace->node_queue &&
	       workspace->node_queue_capacity >= (size_t)num_nodes &&
	       (num_edges == 0U || (workspace->edge_next &&
	        workspace->edge_next_capacity >= (size_t)num_edges));
}

static int Codec_EdgeRelationValid(
	const sg_rune_codec_activation_edge_t *edge,
	const sg_rune_codec_activation_node_t *from,
	const sg_rune_codec_activation_node_t *to,
	const unsigned char *strings)
{
	switch (edge->kind)
	{
	case SG_RUNE_CODEC_EDGE_TARGET:
	case SG_RUNE_CODEC_EDGE_ROUTE_TARGET:
		return from->target_offset != 0U &&
		       to->targetname_offset != 0U &&
		       Codec_StringEqualFold(strings + from->target_offset,
		           strings + to->targetname_offset);
	case SG_RUNE_CODEC_EDGE_KILLTARGET:
		return from->killtarget_offset != 0U &&
		       to->targetname_offset != 0U &&
		       Codec_StringEqualFold(strings + from->killtarget_offset,
		           strings + to->targetname_offset);
	case SG_RUNE_CODEC_EDGE_PATH_TARGET:
		return from->path_target_offset != 0U &&
		       to->targetname_offset != 0U &&
		       Codec_StringEqualFold(strings + from->path_target_offset,
		           strings + to->targetname_offset);
	case SG_RUNE_CODEC_EDGE_OWNER:
		return from->owner_key == to->key;
	case SG_RUNE_CODEC_EDGE_TEAM:
		return to->team_master_key == from->key &&
		       (from->flags & SG_RUNE_CODEC_NODEF_TEAM_MASTER) != 0U &&
		       (to->flags & SG_RUNE_CODEC_NODEF_TEAM_MEMBER) != 0U;
	default:
		return 0;
	}
}

static int Codec_NodeExecutable(
	const sg_rune_codec_activation_node_t *node);
static uint32_t Codec_InventoryFanoutCount(
	const sg_rune_codec_activation_edge_t *edges, uint32_t inventory_edges,
	uint32_t from_key, uint16_t kind);
static uint32_t Codec_InventoryFanoutAt(
	const sg_rune_codec_activation_edge_t *edges, uint32_t inventory_edges,
	uint32_t from_key, uint16_t kind, uint32_t ordinal);

static int Codec_ButtonNodeShapeValid(
	const sg_rune_codec_activation_node_t *node)
{
	return Codec_NodeExecutable(node) &&
	       node->kind == SG_RUNE_CODEC_NODE_BUTTON &&
	       node->flags == (SG_RUNE_CODEC_NODEF_REPEATABLE |
	           SG_RUNE_CODEC_NODEF_TOUCHABLE | SG_RUNE_CODEC_NODEF_USABLE |
	           SG_RUNE_CODEC_NODEF_MOVER) &&
	       node->touch_callback == SG_RUNE_CODEC_CALLBACK_BUTTON_TOUCH &&
	       node->use_callback == SG_RUNE_CODEC_CALLBACK_BUTTON_USE &&
	       node->think_callback == SG_RUNE_CODEC_CALLBACK_NONE &&
	       node->blocked_callback == SG_RUNE_CODEC_CALLBACK_NONE;
}

static int Codec_ButtonNodeSemanticValid(
	const sg_rune_codec_activation_node_t *node,
	const unsigned char *strings)
{
	return node && strings &&
	       Codec_StringEqualLiteral(strings + node->classname_offset,
	           "func_button") &&
	       node->target_offset != 0U && node->targetname_offset == 0U &&
	       node->killtarget_offset == 0U && node->path_target_offset == 0U &&
	       node->owner_key == SG_RUNE_CODEC_NO_KEY &&
	       node->team_master_key == SG_RUNE_CODEC_NO_KEY &&
	       node->spawnflags == 0U &&
	       node->delay_ms == 0 && node->wait_ms > 0 &&
	       node->speed_q8 != 0U && node->accel_q8 == node->speed_q8 &&
	       node->decel_q8 == node->speed_q8;
}

static int Codec_FrameCompleteButtonSemanticValid(
	const sg_rune_codec_activation_node_t *node,
	const unsigned char *strings)
{
	const uint16_t expected_flags = SG_RUNE_CODEC_NODEF_REPEATABLE |
		SG_RUNE_CODEC_NODEF_USABLE | SG_RUNE_CODEC_NODEF_MOVER |
		SG_RUNE_CODEC_NODEF_SHOOTABLE |
		SG_RUNE_CODEC_NODEF_FRAME_COMPLETE_MOVER;

	return node && strings && node->kind == SG_RUNE_CODEC_NODE_BUTTON &&
	       node->flags == expected_flags &&
	       Codec_StringEqualLiteral(strings + node->classname_offset,
	           "func_button") && node->target_offset != 0U &&
	       node->targetname_offset == 0U && node->killtarget_offset == 0U &&
	       node->path_target_offset == 0U &&
	       node->owner_key == SG_RUNE_CODEC_NO_KEY &&
	       node->team_master_key == SG_RUNE_CODEC_NO_KEY &&
	       node->spawnflags == 0U &&
	       node->touch_callback == SG_RUNE_CODEC_CALLBACK_NONE &&
	       node->use_callback == SG_RUNE_CODEC_CALLBACK_BUTTON_USE &&
	       node->think_callback == SG_RUNE_CODEC_CALLBACK_NONE &&
	       node->blocked_callback == SG_RUNE_CODEC_CALLBACK_NONE &&
	       node->delay_ms == 0 && node->wait_ms > 0 &&
	       node->speed_q8 != 0U && node->accel_q8 == node->speed_q8 &&
	       node->decel_q8 == node->speed_q8 &&
	       node->speed_q8 % 10U == 0U;
}

static int Codec_DoorNodeShapeValid(
	const sg_rune_codec_activation_node_t *node, int master)
{
	uint16_t required = SG_RUNE_CODEC_NODEF_USABLE |
		SG_RUNE_CODEC_NODEF_MOVER | (master
		    ? SG_RUNE_CODEC_NODEF_TEAM_MASTER
		    : SG_RUNE_CODEC_NODEF_TEAM_MEMBER);
	uint16_t forbidden = master ? SG_RUNE_CODEC_NODEF_TEAM_MEMBER :
		SG_RUNE_CODEC_NODEF_TEAM_MASTER;
	/* SP_func_door selects Think_CalcMoveSpeed when either health or a
	 * targetname is present.  Shootable brushes therefore retain that callback
	 * even without a targetname; ordinary anonymous brushes retain
	 * Think_SpawnDoorTrigger. */
	uint16_t expected_think = node &&
		(node->flags & SG_RUNE_CODEC_NODEF_SHOOTABLE) != 0U
		? SG_RUNE_CODEC_CALLBACK_THINK_CALC_MOVE_SPEED
		: (node && node->targetname_offset != 0U
			? SG_RUNE_CODEC_CALLBACK_THINK_CALC_MOVE_SPEED
			: SG_RUNE_CODEC_CALLBACK_THINK_SPAWN_DOOR_TRIGGER);

	return Codec_NodeExecutable(node) &&
	       node->kind == (master ? SG_RUNE_CODEC_NODE_DOOR_MASTER
	                            : SG_RUNE_CODEC_NODE_DOOR_MEMBER) &&
	       (node->flags & required) == required &&
	       (node->flags & forbidden) == 0U &&
	       node->touch_callback == SG_RUNE_CODEC_CALLBACK_NONE &&
	       node->use_callback == SG_RUNE_CODEC_CALLBACK_USE_DOOR &&
	       node->think_callback == expected_think &&
	       node->blocked_callback == SG_RUNE_CODEC_CALLBACK_BLOCKED_DOOR;
}

static int Codec_DoorNodeSemanticValid(
	const sg_rune_codec_activation_node_t *node, uint32_t master_key,
	const unsigned char *strings)
{
	return node && strings &&
	       (Codec_StringEqualLiteral(strings + node->classname_offset,
	            "func_door") ||
	        Codec_StringEqualLiteral(strings + node->classname_offset,
	            "func_door_rotating")) &&
	       node->owner_key == SG_RUNE_CODEC_NO_KEY &&
	       node->team_master_key == master_key &&
	       (node->spawnflags & (UINT32_C(1) | UINT32_C(32))) == 0U &&
	       node->killtarget_offset == 0U && node->path_target_offset == 0U &&
	       node->delay_ms >= 0 &&
	       (node->delay_ms == 0 || node->target_offset == 0U) &&
	       node->wait_ms > 0 &&
	       node->speed_q8 != 0U && node->accel_q8 == node->speed_q8 &&
	       node->decel_q8 == node->speed_q8;
}

static int Codec_PlatformDoorTriggerShape(
	const sg_rune_codec_activation_node_t *node)
{
	return Codec_NodeExecutable(node) &&
	       node->kind == SG_RUNE_CODEC_NODE_TRIGGER &&
	       node->touch_callback == SG_RUNE_CODEC_CALLBACK_TOUCH_MULTI &&
	       node->use_callback == SG_RUNE_CODEC_CALLBACK_USE_MULTI &&
	       (node->flags & (SG_RUNE_CODEC_NODEF_REPEATABLE |
	           SG_RUNE_CODEC_NODEF_TOUCHABLE | SG_RUNE_CODEC_NODEF_USABLE)) ==
	           (SG_RUNE_CODEC_NODEF_REPEATABLE |
	            SG_RUNE_CODEC_NODEF_TOUCHABLE | SG_RUNE_CODEC_NODEF_USABLE) &&
	       node->delay_ms >= 0 && node->wait_ms > 0 &&
	       node->target_offset != 0U && node->killtarget_offset == 0U &&
	       node->path_target_offset == 0U;
}

static int Codec_TriggerContainsLinkAnchor(
	const sg_rune_codec_activation_node_t *node,
	const sg_rune_codec_link_t *link)
{
	static const int hull_min_q8[3] = { -136, -136, -200 };
	static const int hull_max_q8[3] = { 136, 136, 264 };
	int axis;

	if (!node || !link || !Codec_VectorOnPmoveLattice(link->suffix_anchor))
		return 0;
	for (axis = 0; axis < 3; axis++)
	{
		int anchor_q8 = (int)(link->suffix_anchor[axis] * 8.0f);

		if (anchor_q8 + hull_max_q8[axis] <= node->absmin_q8[axis] ||
		    anchor_q8 + hull_min_q8[axis] >= node->absmax_q8[axis])
			return 0;
	}
	return 1;
}

static int Codec_SameTargetFanout(
	const sg_rune_codec_activation_edge_t *edges, uint32_t inventory_edges,
	uint32_t left_key, uint32_t right_key, uint32_t delay_ms)
{
	uint32_t left_count = Codec_InventoryFanoutCount(edges, inventory_edges,
		left_key, SG_RUNE_CODEC_EDGE_TARGET);
	uint32_t right_count = Codec_InventoryFanoutCount(edges, inventory_edges,
		right_key, SG_RUNE_CODEC_EDGE_TARGET);
	uint32_t i;

	if (left_count == 0U || left_count != right_count)
		return 0;
	for (i = 0U; i < left_count; i++)
	{
		uint32_t left = Codec_InventoryFanoutAt(edges, inventory_edges,
			left_key, SG_RUNE_CODEC_EDGE_TARGET, i);
		uint32_t right = Codec_InventoryFanoutAt(edges, inventory_edges,
			right_key, SG_RUNE_CODEC_EDGE_TARGET, i);

		if (left == UINT32_MAX || right == UINT32_MAX ||
		    edges[left].to_key != edges[right].to_key ||
		    edges[left].ordinal != edges[right].ordinal ||
		    edges[left].delay_ms != delay_ms ||
		    edges[right].delay_ms != delay_ms)
			return 0;
	}
	return 1;
}

static int Codec_SafeSpeaker(const sg_rune_codec_activation_node_t *node)
{
	return Codec_NodeExecutable(node) &&
	       node->kind == SG_RUNE_CODEC_NODE_TARGET_SPEAKER &&
	       node->use_callback == SG_RUNE_CODEC_CALLBACK_USE_TARGET_SPEAKER &&
	       node->touch_callback == SG_RUNE_CODEC_CALLBACK_NONE &&
	       node->think_callback == SG_RUNE_CODEC_CALLBACK_NONE &&
	       node->blocked_callback == SG_RUNE_CODEC_CALLBACK_NONE &&
	       (node->spawnflags & UINT32_C(3)) == 0U &&
	       node->target_offset == 0U && node->killtarget_offset == 0U &&
	       node->path_target_offset == 0U;
}

static int Codec_SafeAreaportal(const sg_rune_codec_activation_node_t *node)
{
	return Codec_NodeExecutable(node) &&
	       node->kind == SG_RUNE_CODEC_NODE_AREAPORTAL &&
	       node->use_callback == SG_RUNE_CODEC_CALLBACK_USE_AREAPORTAL &&
	       node->touch_callback == SG_RUNE_CODEC_CALLBACK_NONE &&
	       node->think_callback == SG_RUNE_CODEC_CALLBACK_NONE &&
	       node->blocked_callback == SG_RUNE_CODEC_CALLBACK_NONE &&
	       node->target_offset == 0U && node->killtarget_offset == 0U &&
	       node->path_target_offset == 0U;
}

static int Codec_RelayShape(const sg_rune_codec_activation_node_t *node)
{
	return Codec_NodeExecutable(node) &&
	       node->kind == SG_RUNE_CODEC_NODE_RELAY &&
	       node->use_callback == SG_RUNE_CODEC_CALLBACK_USE_TRIGGER_RELAY &&
	       node->touch_callback == SG_RUNE_CODEC_CALLBACK_NONE &&
	       node->think_callback == SG_RUNE_CODEC_CALLBACK_NONE &&
	       node->blocked_callback == SG_RUNE_CODEC_CALLBACK_NONE &&
	       node->delay_ms >= 0 && node->target_offset != 0U &&
	       node->killtarget_offset == 0U && node->path_target_offset == 0U;
}

static int Codec_SafeRelay(const sg_rune_codec_activation_node_t *node)
{
	return Codec_RelayShape(node) && node->delay_ms == 0;
}

static int Codec_NodeExecutable(const sg_rune_codec_activation_node_t *node)
{
	return node &&
	       (node->flags & SG_RUNE_CODEC_NODEF_INVENTORY_ONLY) == 0U &&
	       !Codec_NodeHasUnknownCallback(node);
}

static int Codec_TrainSealedThink(uint16_t callback)
{
	return callback == SG_RUNE_CODEC_CALLBACK_NONE ||
	       callback == SG_RUNE_CODEC_CALLBACK_FUNC_TRAIN_FIND;
}

static int Codec_TrainButtonShape(
	const sg_rune_codec_activation_node_t *button, int shoot)
{
	return Codec_NodeExecutable(button) &&
	       button->kind == SG_RUNE_CODEC_NODE_BUTTON &&
	       button->flags == (SG_RUNE_CODEC_NODEF_REPEATABLE |
	           SG_RUNE_CODEC_NODEF_USABLE | SG_RUNE_CODEC_NODEF_MOVER |
	           (shoot ? SG_RUNE_CODEC_NODEF_SHOOTABLE :
	               SG_RUNE_CODEC_NODEF_TOUCHABLE)) &&
	       button->touch_callback == (shoot ? SG_RUNE_CODEC_CALLBACK_NONE :
	           SG_RUNE_CODEC_CALLBACK_BUTTON_TOUCH) &&
	       button->use_callback == SG_RUNE_CODEC_CALLBACK_BUTTON_USE &&
	       button->think_callback == SG_RUNE_CODEC_CALLBACK_NONE &&
	       button->blocked_callback == SG_RUNE_CODEC_CALLBACK_NONE &&
	       button->spawnflags == 0U && button->delay_ms == 0 &&
	       button->wait_ms > 0 && button->target_offset != 0U &&
	       button->killtarget_offset == 0U && button->path_target_offset == 0U;
}

static int Codec_TrainMoverShape(
	const sg_rune_codec_activation_node_t *train)
{
	return Codec_NodeExecutable(train) &&
	       train->kind == SG_RUNE_CODEC_NODE_TRAIN &&
	       train->flags == (SG_RUNE_CODEC_NODEF_REPEATABLE |
	           SG_RUNE_CODEC_NODEF_USABLE | SG_RUNE_CODEC_NODEF_MOVER) &&
	       train->spawnflags == 2U &&
	       train->touch_callback == SG_RUNE_CODEC_CALLBACK_NONE &&
	       train->use_callback == SG_RUNE_CODEC_CALLBACK_TRAIN_USE &&
	       Codec_TrainSealedThink(train->think_callback) &&
	       train->blocked_callback == SG_RUNE_CODEC_CALLBACK_BLOCKED_TRAIN &&
	       train->delay_ms == 0 && train->speed_q8 != 0U &&
	       train->speed_q8 == train->accel_q8 &&
	       train->speed_q8 == train->decel_q8 &&
	       train->target_offset != 0U && train->targetname_offset != 0U &&
	       train->killtarget_offset == 0U && train->path_target_offset == 0U;
}

static int Codec_TrainCornerShape(
	const sg_rune_codec_activation_node_t *corner)
{
	return Codec_NodeExecutable(corner) &&
	       corner->kind == SG_RUNE_CODEC_NODE_PATH_CORNER &&
	       corner->flags == (SG_RUNE_CODEC_NODEF_TOUCHABLE |
	           SG_RUNE_CODEC_NODEF_ONE_SHOT) && corner->spawnflags == 0U &&
	       corner->touch_callback ==
	           SG_RUNE_CODEC_CALLBACK_PATH_CORNER_TOUCH &&
	       corner->use_callback == SG_RUNE_CODEC_CALLBACK_NONE &&
	       corner->think_callback == SG_RUNE_CODEC_CALLBACK_NONE &&
	       corner->blocked_callback == SG_RUNE_CODEC_CALLBACK_NONE &&
	       corner->delay_ms == 0 && corner->wait_ms == -1000 &&
	       corner->target_offset != 0U && corner->killtarget_offset == 0U;
}

static int Codec_TrainNoSideEffects(
	const sg_rune_codec_activation_edge_t *edges, uint32_t inventory_edges,
	uint32_t key)
{
	return Codec_InventoryFanoutCount(edges, inventory_edges, key,
	           SG_RUNE_CODEC_EDGE_KILLTARGET) == 0U &&
	       Codec_InventoryFanoutCount(edges, inventory_edges, key,
	           SG_RUNE_CODEC_EDGE_PATH_TARGET) == 0U;
}

static int Codec_PushNodeSemanticValid(
	const sg_rune_codec_activation_node_t *node,
	const unsigned char *strings)
{
	return Codec_NodeExecutable(node) && strings &&
	       node->kind == SG_RUNE_CODEC_NODE_PUSH &&
	       node->flags == (SG_RUNE_CODEC_NODEF_REPEATABLE |
	           SG_RUNE_CODEC_NODEF_TOUCHABLE) &&
	       Codec_StringEqualLiteral(strings + node->classname_offset,
	           "trigger_push") &&
	       node->target_offset == 0U && node->targetname_offset == 0U &&
	       node->killtarget_offset == 0U && node->path_target_offset == 0U &&
	       node->owner_key == SG_RUNE_CODEC_NO_KEY &&
	       node->team_master_key == SG_RUNE_CODEC_NO_KEY &&
	       node->spawnflags == 0U &&
	       node->touch_callback ==
	           SG_RUNE_CODEC_CALLBACK_TRIGGER_PUSH_TOUCH &&
	       node->use_callback == SG_RUNE_CODEC_CALLBACK_NONE &&
	       node->think_callback == SG_RUNE_CODEC_CALLBACK_NONE &&
	       node->blocked_callback == SG_RUNE_CODEC_CALLBACK_NONE &&
	       node->delay_ms == 0 && node->wait_ms == 0 &&
	       node->speed_q8 == 680U && node->accel_q8 == 0U &&
	       node->decel_q8 == 0U;
}

static uint32_t Codec_InventoryFanoutCount(
	const sg_rune_codec_activation_edge_t *edges, uint32_t inventory_edges,
	uint32_t from_key, uint16_t kind)
{
	uint32_t count = 0U;
	uint32_t i;

	for (i = 0U; i < inventory_edges; i++)
		if (edges[i].from_key == from_key && edges[i].kind == kind)
			count++;
	return count;
}

static uint32_t Codec_InventoryFanoutAt(
	const sg_rune_codec_activation_edge_t *edges, uint32_t inventory_edges,
	uint32_t from_key, uint16_t kind, uint32_t ordinal)
{
	uint32_t seen = 0U;
	uint32_t i;

	for (i = 0U; i < inventory_edges; i++)
		if (edges[i].from_key == from_key && edges[i].kind == kind)
		{
			if (seen == ordinal)
				return i;
			seen++;
		}
	return UINT32_MAX;
}

/* Validate the inventory suffix intentionally suppressed by the runtime when
 * a bound positive-delay relay is invoked.  The inbound edge remains in the
 * plan (preserving engine fanout ordinals); no delayed outgoing edge does. */
static int Codec_DelayedSoundOnlyRelay(
	const sg_rune_codec_activation_node_t *nodes, uint32_t num_nodes,
	const sg_rune_codec_activation_edge_t *edges, uint32_t inventory_edges,
	uint32_t node_index, int depth)
{
	const sg_rune_codec_activation_node_t *node;
	uint32_t count;
	uint32_t ordinal;

	if (!nodes || !edges || node_index >= num_nodes || depth > 4)
		return 0;
	node = &nodes[node_index];
	if (!Codec_RelayShape(node) || node->delay_ms < 0)
		return 0;
	count = Codec_InventoryFanoutCount(edges, inventory_edges, node->key,
		SG_RUNE_CODEC_EDGE_TARGET);
	if (count == 0U)
		return 0;
	for (ordinal = 0U; ordinal < count; ordinal++)
	{
		uint32_t edge_index = Codec_InventoryFanoutAt(edges,
			inventory_edges, node->key, SG_RUNE_CODEC_EDGE_TARGET, ordinal);
		uint32_t destination_index;

		if (edge_index == UINT32_MAX ||
		    edges[edge_index].delay_ms != (uint32_t)node->delay_ms)
			return 0;
		destination_index = Codec_FindNode(nodes, num_nodes,
			edges[edge_index].to_key);
		if (destination_index == UINT32_MAX)
			return 0;
		if (Codec_SafeSpeaker(&nodes[destination_index]))
			continue;
		if (!Codec_DelayedSoundOnlyRelay(nodes, num_nodes, edges,
		        inventory_edges, destination_index, depth + 1))
			return 0;
	}
	return 1;
}

static int Codec_PlanContainsExactEdge(
	const sg_rune_codec_activation_edge_t *edges,
	const sg_rune_codec_activation_plan_t *plan, uint32_t inventory_index)
{
	uint32_t i;

	for (i = 0U; i < plan->num_edges; i++)
		if (memcmp(&edges[inventory_index], &edges[plan->first_edge + i],
		    sizeof(edges[inventory_index])) == 0)
			return 1;
	return 0;
}

static sg_rune_codec_diagnostic_t Codec_ExpectInventoryEdge(
	const sg_rune_codec_activation_edge_t *edges,
	const sg_rune_codec_activation_plan_t *plan, uint32_t inventory_index,
	uint32_t generation, sg_rune_codec_workspace_t *workspace,
	uint32_t *expected_count)
{
	if (!workspace || !expected_count ||
	    workspace->edge_next[inventory_index] == generation ||
	    edges[inventory_index].delay_ms != 0U ||
	    !Codec_PlanContainsExactEdge(edges, plan, inventory_index))
		return RLCODEC_BAD_ACTIVATION_PLAN;
	workspace->edge_next[inventory_index] = generation;
	(*expected_count)++;
	return RLCODEC_OK;
}

static sg_rune_codec_diagnostic_t Codec_ExpectPlatformTriggerEdge(
	const sg_rune_codec_activation_edge_t *edges,
	const sg_rune_codec_activation_plan_t *plan, uint32_t inventory_index,
	uint32_t delay_ms, uint32_t generation,
	sg_rune_codec_workspace_t *workspace, uint32_t *expected_count)
{
	if (!workspace || !expected_count ||
	    workspace->edge_next[inventory_index] == generation ||
	    edges[inventory_index].kind != SG_RUNE_CODEC_EDGE_TARGET ||
	    edges[inventory_index].delay_ms != delay_ms ||
	    !Codec_PlanContainsExactEdge(edges, plan, inventory_index))
		return RLCODEC_BAD_ACTIVATION_PLAN;
	workspace->edge_next[inventory_index] = generation;
	(*expected_count)++;
	return RLCODEC_OK;
}

static sg_rune_codec_diagnostic_t Codec_AddSideEffect(
	const sg_rune_codec_activation_node_t *nodes, uint32_t num_nodes,
	const sg_rune_codec_activation_edge_t *edges, uint32_t inventory_edges,
	const sg_rune_codec_activation_plan_t *plan, uint32_t inventory_index,
	int allow_areaportal, uint32_t generation,
	sg_rune_codec_workspace_t *workspace, uint32_t *expected_count,
	uint32_t *relay_count)
{
	const sg_rune_codec_activation_edge_t *edge = &edges[inventory_index];
	uint32_t destination_index;
	sg_rune_codec_diagnostic_t diagnostic;

	if (edge->kind != SG_RUNE_CODEC_EDGE_TARGET || edge->delay_ms != 0U)
		return RLCODEC_BAD_ACTIVATION_PLAN;
	diagnostic = Codec_ExpectInventoryEdge(edges, plan, inventory_index,
		generation, workspace, expected_count);
	if (diagnostic != RLCODEC_OK)
		return diagnostic;
	destination_index = Codec_FindNode(nodes, num_nodes, edge->to_key);
	if (destination_index == UINT32_MAX)
		return RLCODEC_BAD_ACTIVATION_PLAN;
	if (Codec_SafeSpeaker(&nodes[destination_index]) ||
	    (allow_areaportal && Codec_SafeAreaportal(&nodes[destination_index])))
		return RLCODEC_OK;
	if (nodes[destination_index].delay_ms > 0 &&
	    Codec_DelayedSoundOnlyRelay(nodes, num_nodes, edges,
	        inventory_edges, destination_index, 1))
		return RLCODEC_OK;
	if (!Codec_SafeRelay(&nodes[destination_index]) ||
	    workspace->node_generations[destination_index] == generation ||
	    *relay_count >= num_nodes)
		return RLCODEC_BAD_ACTIVATION_PLAN;
	workspace->node_generations[destination_index] = generation;
	workspace->node_queue[*relay_count] = destination_index;
	(*relay_count)++;
	return RLCODEC_OK;
}

static int Codec_MasterSeen(
	const sg_rune_codec_activation_node_t *nodes,
	const sg_rune_codec_workspace_t *workspace, uint32_t master_count,
	uint32_t key)
{
	uint32_t i;

	for (i = 0U; i < master_count; i++)
		if (nodes[workspace->node_touched[i]].key == key)
			return 1;
	return 0;
}

static sg_rune_codec_diagnostic_t Codec_DiscoverRelayDoorTargets(
	const sg_rune_codec_activation_node_t *nodes, uint32_t num_nodes,
	const sg_rune_codec_activation_edge_t *edges, uint32_t inventory_edges,
	sg_rune_codec_workspace_t *workspace,
	uint32_t relay_count, uint32_t *master_count, uint32_t *smallest)
{
	uint32_t i;

	if (!nodes || !edges || !workspace || !master_count || !smallest)
		return RLCODEC_BAD_ACTIVATION_PLAN;
	for (i = 0U; i < relay_count; i++)
	{
		uint32_t relay_index = workspace->node_queue[i];
		uint32_t target_count = Codec_InventoryFanoutCount(edges,
			inventory_edges, nodes[relay_index].key,
			SG_RUNE_CODEC_EDGE_TARGET);
		uint32_t j;

		for (j = 0U; j < target_count; j++)
		{
			uint32_t edge_index = Codec_InventoryFanoutAt(edges,
				inventory_edges, nodes[relay_index].key,
				SG_RUNE_CODEC_EDGE_TARGET, j);
			uint32_t destination_index;
			const sg_rune_codec_activation_node_t *destination;

			if (edge_index == UINT32_MAX)
				return RLCODEC_BAD_ACTIVATION_PLAN;
			destination_index = Codec_FindNode(nodes, num_nodes,
				edges[edge_index].to_key);
			if (destination_index == UINT32_MAX)
				return RLCODEC_BAD_ACTIVATION_PLAN;
			destination = &nodes[destination_index];
			if (destination->kind == SG_RUNE_CODEC_NODE_DOOR_MASTER)
			{
				if (Codec_MasterSeen(nodes, workspace, *master_count,
				        destination->key) || *master_count >= num_nodes)
					return RLCODEC_BAD_ACTIVATION_PLAN;
				workspace->node_touched[(*master_count)++] =
					destination_index;
				if (destination->key < *smallest)
					*smallest = destination->key;
			}
			else if (destination->kind == SG_RUNE_CODEC_NODE_DOOR_MEMBER &&
			    !Codec_MasterSeen(nodes, workspace, *master_count,
			        destination->team_master_key))
				return RLCODEC_BAD_ACTIVATION_PLAN;
		}
	}
	return RLCODEC_OK;
}

static sg_rune_codec_diagnostic_t Codec_AddRelayEffect(
	const sg_rune_codec_activation_node_t *nodes, uint32_t num_nodes,
	const sg_rune_codec_activation_edge_t *edges, uint32_t inventory_edges,
	const sg_rune_codec_activation_plan_t *plan, uint32_t inventory_index,
	uint32_t generation, sg_rune_codec_workspace_t *workspace,
	uint32_t *expected_count, uint32_t *relay_count,
	uint32_t master_count)
{
	uint32_t destination_index;
	const sg_rune_codec_activation_node_t *destination;

	if (inventory_index >= inventory_edges)
		return RLCODEC_BAD_ACTIVATION_PLAN;
	destination_index = Codec_FindNode(nodes, num_nodes,
		edges[inventory_index].to_key);
	if (destination_index == UINT32_MAX)
		return RLCODEC_BAD_ACTIVATION_PLAN;
	destination = &nodes[destination_index];
	if (destination->kind == SG_RUNE_CODEC_NODE_DOOR_MASTER ||
	    destination->kind == SG_RUNE_CODEC_NODE_DOOR_MEMBER)
	{
		uint32_t master_key = destination->kind ==
		        SG_RUNE_CODEC_NODE_DOOR_MASTER
		    ? destination->key : destination->team_master_key;

		if (!Codec_MasterSeen(nodes, workspace, master_count, master_key))
			return RLCODEC_BAD_ACTIVATION_PLAN;
		return Codec_ExpectInventoryEdge(edges, plan, inventory_index,
			generation, workspace, expected_count);
	}
	return Codec_AddSideEffect(nodes, num_nodes, edges, inventory_edges,
		plan, inventory_index, 0, generation, workspace, expected_count,
		relay_count);
}

static sg_rune_codec_diagnostic_t Codec_ValidateProductionPlanExact(
	const sg_rune_codec_activation_node_t *nodes, uint32_t num_nodes,
	const sg_rune_codec_activation_edge_t *edges, uint32_t num_edges,
	uint32_t inventory_edges, const sg_rune_codec_activation_plan_t *plan,
	uint32_t plan_index, const sg_rune_codec_link_t *owner_link,
	const unsigned char *strings,
	sg_rune_codec_workspace_t *workspace)
{
	uint32_t generation = plan_index + 1U;
	uint32_t entry_index;
	uint32_t mover_index;
	uint32_t expected_count = 0U;
	uint32_t relay_count = 0U;
	uint32_t master_count = 0U;
	uint32_t physical_count = 0U;
	uint32_t i;
	uint32_t j;
	uint32_t closure_crc;
	sg_rune_codec_diagnostic_t diagnostic;

	diagnostic = Codec_ValidatePlanFields(plan);
	if (diagnostic != RLCODEC_OK || plan->first_edge > num_edges ||
	    plan->num_edges > num_edges - plan->first_edge)
		return RLCODEC_BAD_ACTIVATION_PLAN;
	entry_index = Codec_FindNode(nodes, num_nodes, plan->entry_key);
	if (entry_index == UINT32_MAX ||
	    !Codec_NodeExecutable(&nodes[entry_index]))
		return RLCODEC_BAD_ACTIVATION_PLAN;
	if (plan->controller_kind == SG_RUNE_CODEC_CONTROLLER_PUSH)
	{
		diagnostic = SG_RuneCodecPushClosureCRC32(plan->entry_key,
			nodes[entry_index].push_velocity, &closure_crc);
		if (diagnostic != RLCODEC_OK ||
		    closure_crc != plan->closure_crc32 ||
		    !Codec_PushNodeSemanticValid(&nodes[entry_index], strings))
			return RLCODEC_BAD_ACTIVATION_PLAN;
		return RLCODEC_OK;
	}
	diagnostic = SG_RuneCodecPlanClosureCRC32(edges, plan->first_edge,
		plan->num_edges, num_edges, &closure_crc);
	if (diagnostic != RLCODEC_OK || closure_crc != plan->closure_crc32)
		return RLCODEC_BAD_ACTIVATION_PLAN;
	mover_index = Codec_FindNode(nodes, num_nodes, plan->mover_key);
	if (mover_index == UINT32_MAX ||
	    !Codec_NodeExecutable(&nodes[mover_index]))
		return RLCODEC_BAD_ACTIVATION_PLAN;

	/* Executable relations are independently proved and unique before any
	 * controller-specific closure admission. */
	for (i = 0U; i < plan->num_edges; i++)
	{
		const sg_rune_codec_activation_edge_t *edge =
			&edges[plan->first_edge + i];
		uint32_t from_index = Codec_FindNode(nodes, num_nodes, edge->from_key);
		uint32_t to_index = Codec_FindNode(nodes, num_nodes, edge->to_key);

		if (Codec_ValidateEdgeFields(edge) != RLCODEC_OK ||
		    edge->from_key == edge->to_key || from_index == UINT32_MAX ||
		    to_index == UINT32_MAX ||
		    !Codec_NodeExecutable(&nodes[from_index]) ||
		    !Codec_NodeExecutable(&nodes[to_index]) ||
		    !Codec_EdgeRelationValid(edge, &nodes[from_index],
		        &nodes[to_index], strings))
			return RLCODEC_BAD_ACTIVATION_EDGE;
		for (j = 0U; j < i; j++)
			if (memcmp(edge, &edges[plan->first_edge + j],
			    sizeof(*edge)) == 0)
				return RLCODEC_BAD_ACTIVATION_PLAN;
	}

	switch (plan->controller_kind)
	{
	case SG_RUNE_CODEC_CONTROLLER_PLATFORM:
	{
		uint32_t owner_count = Codec_InventoryFanoutCount(edges,
			inventory_edges, plan->entry_key, SG_RUNE_CODEC_EDGE_OWNER);
		uint32_t owner_index = Codec_InventoryFanoutAt(edges,
			inventory_edges, plan->entry_key, SG_RUNE_CODEC_EDGE_OWNER, 0U);
		uint32_t target_count = Codec_InventoryFanoutCount(edges,
			inventory_edges, plan->entry_key, SG_RUNE_CODEC_EDGE_TARGET);
		uint32_t target_index = Codec_InventoryFanoutAt(edges,
			inventory_edges, plan->entry_key, SG_RUNE_CODEC_EDGE_TARGET, 0U);
		uint32_t cooldown = nodes[entry_index].wait_ms > RUNE_MAX_COST_MS
			? RUNE_MAX_COST_MS : (uint32_t)nodes[entry_index].wait_ms;
		int stock;
		int carrier;
		uint32_t class_indices[2] = { UINT32_MAX, UINT32_MAX };
		uint32_t class_count = 0U;
		uint32_t approach_class = UINT32_MAX;

		stock = nodes[entry_index].touch_callback ==
		        SG_RUNE_CODEC_CALLBACK_TOUCH_PLAT_CENTER &&
		    (nodes[entry_index].flags & SG_RUNE_CODEC_NODEF_SYNTHETIC) != 0U &&
		    target_count == 0U && plan->cooldown_ms == 0U;
		carrier = nodes[entry_index].touch_callback ==
		        SG_RUNE_CODEC_CALLBACK_TOUCH_MULTI &&
		    nodes[entry_index].use_callback == SG_RUNE_CODEC_CALLBACK_USE_MULTI &&
		    (nodes[entry_index].flags & (SG_RUNE_CODEC_NODEF_SYNTHETIC |
		        SG_RUNE_CODEC_NODEF_REPEATABLE | SG_RUNE_CODEC_NODEF_TOUCHABLE |
		        SG_RUNE_CODEC_NODEF_USABLE)) ==
		        (SG_RUNE_CODEC_NODEF_REPEATABLE | SG_RUNE_CODEC_NODEF_TOUCHABLE |
		         SG_RUNE_CODEC_NODEF_USABLE) &&
		    nodes[entry_index].delay_ms == 0 && nodes[entry_index].wait_ms > 0 &&
		    nodes[entry_index].killtarget_offset == 0U &&
		    nodes[entry_index].path_target_offset == 0U &&
		    nodes[mover_index].use_callback == SG_RUNE_CODEC_CALLBACK_USE_DOOR &&
		    nodes[mover_index].blocked_callback ==
		        SG_RUNE_CODEC_CALLBACK_BLOCKED_DOOR &&
		    SG_RuneCarrierDoorSpawnflags(nodes[mover_index].spawnflags) &&
		    (nodes[mover_index].flags & (SG_RUNE_CODEC_NODEF_MOVER |
		        SG_RUNE_CODEC_NODEF_TEAM_MASTER |
		        SG_RUNE_CODEC_NODEF_SHOOTABLE)) ==
		        (SG_RUNE_CODEC_NODEF_MOVER | SG_RUNE_CODEC_NODEF_TEAM_MASTER) &&
		    target_count == 1U && target_index != UINT32_MAX &&
		    edges[target_index].to_key == plan->mover_key &&
		    plan->cooldown_ms == cooldown;
		if (nodes[entry_index].kind != SG_RUNE_CODEC_NODE_PLATFORM_TRIGGER ||
		    nodes[mover_index].kind != SG_RUNE_CODEC_NODE_PLATFORM ||
		    owner_count != 1U || owner_index == UINT32_MAX ||
		    edges[owner_index].to_key != plan->mover_key ||
		    (!stock && !carrier) || !owner_link ||
		    (stock && ((owner_link->mode == RLCM_RIDE &&
		                   plan->expected_members <= 1U) ||
		              (owner_link->mode != RLCM_NONE &&
		                   owner_link->mode != RLCM_RIDE))))
			return RLCODEC_BAD_ACTIVATION_PLAN;
		if (carrier)
		{
			diagnostic = Codec_ExpectInventoryEdge(edges, plan, target_index,
				generation, workspace, &expected_count);
			if (diagnostic != RLCODEC_OK)
				return diagnostic;
		}
		diagnostic = Codec_ExpectInventoryEdge(edges, plan, owner_index,
			generation, workspace, &expected_count);
		if (diagnostic != RLCODEC_OK)
			return diagnostic;
		if (stock && plan->expected_members > 1U)
		{
			uint32_t egress_count = 0U;

			for (i = 0U; i < num_nodes; i++)
			{
				uint32_t egress_owner_count;
				uint32_t egress_owner_index;
				uint32_t door_index;

				if (nodes[i].kind !=
				        SG_RUNE_CODEC_NODE_AUTO_DOOR_TRIGGER ||
				    nodes[i].touch_callback !=
				        SG_RUNE_CODEC_CALLBACK_TOUCH_DOOR_TRIGGER ||
				    (nodes[i].flags &
				        SG_RUNE_CODEC_NODEF_SYNTHETIC) == 0U)
					continue;
				egress_owner_count = Codec_InventoryFanoutCount(edges,
					inventory_edges, nodes[i].key,
					SG_RUNE_CODEC_EDGE_OWNER);
				egress_owner_index = Codec_InventoryFanoutAt(edges,
					inventory_edges, nodes[i].key,
					SG_RUNE_CODEC_EDGE_OWNER, 0U);
				if (egress_owner_count != 1U ||
				    egress_owner_index == UINT32_MAX ||
				    !Codec_PlanContainsExactEdge(edges, plan,
				        egress_owner_index))
					continue;
				door_index = Codec_FindNode(nodes, num_nodes,
					edges[egress_owner_index].to_key);
				if (door_index == UINT32_MAX ||
				    nodes[door_index].kind !=
				        SG_RUNE_CODEC_NODE_DOOR_MASTER ||
				    nodes[i].owner_key != nodes[door_index].key ||
				    egress_count != 0U)
					return RLCODEC_BAD_ACTIVATION_PLAN;
				diagnostic = Codec_ExpectInventoryEdge(edges, plan,
					egress_owner_index, generation, workspace,
					&expected_count);
				if (diagnostic != RLCODEC_OK)
					return diagnostic;
				workspace->node_touched[master_count++] = door_index;
				egress_count++;
			}
			if (egress_count != 1U)
				return RLCODEC_BAD_ACTIVATION_PLAN;
		}
		if (carrier && plan->expected_members > 1U)
		{
			if (!owner_link || owner_link->action != RL_LIFT ||
			    !Codec_VectorOnPmoveLattice(owner_link->suffix_anchor))
				return RLCODEC_BAD_ACTIVATION_PLAN;
			for (i = 0U; i < num_nodes; i++)
			{
				uint32_t count;
				uint32_t ordinal;
				uint32_t trigger_class;
				int planned = 0;

				if (!Codec_PlatformDoorTriggerShape(&nodes[i]))
					continue;
				count = Codec_InventoryFanoutCount(edges, inventory_edges,
					nodes[i].key, SG_RUNE_CODEC_EDGE_TARGET);
				for (ordinal = 0U; ordinal < count; ordinal++)
				{
					uint32_t candidate = Codec_InventoryFanoutAt(edges,
						inventory_edges, nodes[i].key,
						SG_RUNE_CODEC_EDGE_TARGET, ordinal);

					if (candidate != UINT32_MAX &&
					    Codec_PlanContainsExactEdge(edges, plan, candidate))
					{
						planned = 1;
						break;
					}
				}
				if (!planned)
					continue;
				for (trigger_class = 0U; trigger_class < class_count;
				     trigger_class++)
				{
					uint32_t reference = class_indices[trigger_class];

					if (nodes[reference].delay_ms == nodes[i].delay_ms &&
					    nodes[reference].target_offset ==
					        nodes[i].target_offset &&
					    Codec_SameTargetFanout(edges, inventory_edges,
					        nodes[reference].key, nodes[i].key,
					        (uint32_t)nodes[i].delay_ms))
						break;
				}
				if (trigger_class == class_count)
				{
					if (class_count == 2U)
						return RLCODEC_BAD_ACTIVATION_PLAN;
					class_indices[class_count++] = i;
				}
				if (Codec_TriggerContainsLinkAnchor(&nodes[i], owner_link))
				{
					if (approach_class != UINT32_MAX &&
					    approach_class != trigger_class)
						return RLCODEC_BAD_ACTIVATION_PLAN;
					approach_class = trigger_class;
				}
			}
			if (class_count == 0U || approach_class == UINT32_MAX)
				return RLCODEC_BAD_ACTIVATION_PLAN;
			for (uint32_t stage = 0U; stage < class_count; stage++)
			{
				uint32_t trigger_class = stage == 0U ? approach_class :
					(approach_class == 0U ? 1U : 0U);
				uint32_t reference_index = class_indices[trigger_class];
				uint32_t delay_ms = (uint32_t)nodes[reference_index].delay_ms;
				uint32_t stage_master_first = master_count;

				if (stage != 0U && class_count == 1U)
					return RLCODEC_BAD_ACTIVATION_PLAN;
				for (i = 0U; i < num_nodes; i++)
				{
					uint32_t count;

					if (!Codec_PlatformDoorTriggerShape(&nodes[i]) ||
					    (uint32_t)nodes[i].delay_ms != delay_ms ||
					    nodes[i].target_offset !=
					        nodes[reference_index].target_offset)
						continue;
					if (!Codec_SameTargetFanout(edges, inventory_edges,
					        nodes[reference_index].key, nodes[i].key,
					        delay_ms))
						return RLCODEC_BAD_ACTIVATION_PLAN;
					count = Codec_InventoryFanoutCount(edges, inventory_edges,
						nodes[i].key, SG_RUNE_CODEC_EDGE_TARGET);
					for (j = 0U; j < count; j++)
					{
						const sg_rune_codec_activation_node_t *destination;
						uint32_t destination_index;
						uint32_t target_edge;

						target_edge = Codec_InventoryFanoutAt(
							edges, inventory_edges, nodes[i].key,
							SG_RUNE_CODEC_EDGE_TARGET, j);
						destination_index = target_edge == UINT32_MAX
							? UINT32_MAX
							: Codec_FindNode(nodes, num_nodes,
							    edges[target_edge].to_key);

						if (destination_index == UINT32_MAX)
							return RLCODEC_BAD_ACTIVATION_PLAN;
						destination = &nodes[destination_index];
						if (destination->kind ==
						    SG_RUNE_CODEC_NODE_DOOR_MASTER)
						{
							uint32_t prior;

							for (prior = 0U;
							     prior < master_count; prior++)
							{
								uint32_t touched_index;

								touched_index = workspace->
									node_touched[prior];
								if (touched_index ==
								    destination_index)
									break;
							}
							if (prior < stage_master_first)
								return RLCODEC_BAD_ACTIVATION_PLAN;
							if (prior == master_count)
							{
								workspace->node_touched[
									master_count] =
									destination_index;
								master_count++;
							}
						}
						else if (destination->kind ==
						         SG_RUNE_CODEC_NODE_DOOR_MEMBER)
						{
							int seen_master = 0;
							uint32_t prior;

							for (prior = stage_master_first;
							     prior < master_count; prior++)
							{
								uint32_t prior_index;

								prior_index = workspace->
									node_touched[prior];

								if (nodes[prior_index].key ==
								    destination->team_master_key)
									seen_master = 1;
							}
							if (!seen_master)
								return RLCODEC_BAD_ACTIVATION_PLAN;
						}
						else
							return RLCODEC_BAD_ACTIVATION_PLAN;
						diagnostic = Codec_ExpectPlatformTriggerEdge(
							edges, plan, target_edge, delay_ms,
							generation, workspace, &expected_count);
						if (diagnostic != RLCODEC_OK)
							return diagnostic;
					}
				}
				if (master_count == stage_master_first)
					return RLCODEC_BAD_ACTIVATION_PLAN;
			}
			if (master_count == 0U)
				return RLCODEC_BAD_ACTIVATION_PLAN;
		}
		else if (carrier && plan->expected_members != 1U)
			return RLCODEC_BAD_ACTIVATION_PLAN;
		break;
	}

	case SG_RUNE_CODEC_CONTROLLER_TELEPORT:
	{
		uint32_t owner_count = Codec_InventoryFanoutCount(edges,
			inventory_edges, plan->entry_key, SG_RUNE_CODEC_EDGE_OWNER);
		uint32_t target_count = Codec_InventoryFanoutCount(edges,
			inventory_edges, plan->entry_key, SG_RUNE_CODEC_EDGE_TARGET);
		uint32_t owner_index = Codec_InventoryFanoutAt(edges,
			inventory_edges, plan->entry_key, SG_RUNE_CODEC_EDGE_OWNER, 0U);
		uint32_t target_index = Codec_InventoryFanoutAt(edges,
			inventory_edges, plan->entry_key, SG_RUNE_CODEC_EDGE_TARGET, 0U);
		uint32_t destination_index = target_index == UINT32_MAX ? UINT32_MAX :
			Codec_FindNode(nodes, num_nodes, edges[target_index].to_key);

		if (nodes[entry_index].kind !=
		        SG_RUNE_CODEC_NODE_TELEPORT_TRIGGER ||
		    nodes[entry_index].touch_callback !=
		        SG_RUNE_CODEC_CALLBACK_TELEPORTER_TOUCH ||
		    (nodes[entry_index].flags & SG_RUNE_CODEC_NODEF_SYNTHETIC) == 0U ||
		    nodes[mover_index].kind != SG_RUNE_CODEC_NODE_TELEPORTER ||
		    owner_count != 1U || target_count != 1U ||
		    owner_index == UINT32_MAX || target_index == UINT32_MAX ||
		    edges[owner_index].to_key != plan->mover_key ||
		    destination_index == UINT32_MAX ||
		    nodes[destination_index].kind !=
		        SG_RUNE_CODEC_NODE_TELEPORT_DEST ||
		    plan->expected_members != 1U || plan->cooldown_ms != 0U)
			return RLCODEC_BAD_ACTIVATION_PLAN;
		diagnostic = Codec_ExpectInventoryEdge(edges, plan, owner_index,
			generation, workspace, &expected_count);
		if (diagnostic == RLCODEC_OK)
			diagnostic = Codec_ExpectInventoryEdge(edges, plan, target_index,
				generation, workspace, &expected_count);
		if (diagnostic != RLCODEC_OK)
			return diagnostic;
		break;
	}

	case SG_RUNE_CODEC_CONTROLLER_TRAIN:
	case SG_RUNE_CODEC_CONTROLLER_TRAIN_SHOOT:
	{
		uint32_t button_target_count = Codec_InventoryFanoutCount(edges,
			inventory_edges, plan->entry_key, SG_RUNE_CODEC_EDGE_TARGET);
		uint32_t train_route_count = Codec_InventoryFanoutCount(edges,
			inventory_edges, plan->mover_key,
			SG_RUNE_CODEC_EDGE_ROUTE_TARGET);
		uint32_t button_target = Codec_InventoryFanoutAt(edges,
			inventory_edges, plan->entry_key, SG_RUNE_CODEC_EDGE_TARGET, 0U);
		uint32_t train_route = Codec_InventoryFanoutAt(edges,
			inventory_edges, plan->mover_key,
			SG_RUNE_CODEC_EDGE_ROUTE_TARGET, 0U);
		uint32_t open_index = train_route == UINT32_MAX ? UINT32_MAX :
			Codec_FindNode(nodes, num_nodes, edges[train_route].to_key);
		uint32_t open_route_count = open_index == UINT32_MAX ? 0U :
			Codec_InventoryFanoutCount(edges, inventory_edges,
			    nodes[open_index].key, SG_RUNE_CODEC_EDGE_ROUTE_TARGET);
		uint32_t open_route = open_index == UINT32_MAX ? UINT32_MAX :
			Codec_InventoryFanoutAt(edges, inventory_edges,
			    nodes[open_index].key, SG_RUNE_CODEC_EDGE_ROUTE_TARGET, 0U);
		uint32_t closed_index = open_route == UINT32_MAX ? UINT32_MAX :
			Codec_FindNode(nodes, num_nodes, edges[open_route].to_key);
		uint32_t closed_route_count = closed_index == UINT32_MAX ? 0U :
			Codec_InventoryFanoutCount(edges, inventory_edges,
			    nodes[closed_index].key, SG_RUNE_CODEC_EDGE_ROUTE_TARGET);
		uint32_t closed_route = closed_index == UINT32_MAX ? UINT32_MAX :
			Codec_InventoryFanoutAt(edges, inventory_edges,
			    nodes[closed_index].key, SG_RUNE_CODEC_EDGE_ROUTE_TARGET, 0U);
		int shoot = plan->controller_kind ==
			SG_RUNE_CODEC_CONTROLLER_TRAIN_SHOOT;

		if (shoot && nodes[entry_index].kind ==
		        SG_RUNE_CODEC_NODE_DOOR_MASTER)
		{
			if (!owner_link || owner_link->action != RL_TRAIN ||
			    owner_link->mode != RLCM_PREOPEN ||
			    plan->entry_key != plan->mover_key ||
			    plan->expected_members == 0U || plan->cooldown_ms == 0U ||
			    plan->cooldown_ms > SG_RUNE_CODEC_MAX_TIME_MS ||
			    nodes[entry_index].team_master_key != plan->entry_key ||
			    (nodes[entry_index].flags &
			        SG_RUNE_CODEC_NODEF_SHOOTABLE) == 0U)
				return RLCODEC_BAD_ACTIVATION_PLAN;
			workspace->node_touched[master_count++] = entry_index;
			break;
		}

		if (!owner_link || owner_link->action != RL_TRAIN ||
		    plan->expected_members != 1U || plan->cooldown_ms == 0U ||
		    plan->cooldown_ms > SG_RUNE_CODEC_MAX_TIME_MS ||
		    !Codec_TrainButtonShape(&nodes[entry_index], shoot) ||
		    !Codec_TrainMoverShape(&nodes[mover_index]) ||
		    button_target_count != 1U || train_route_count != 1U ||
		    button_target == UINT32_MAX || train_route == UINT32_MAX ||
		    edges[button_target].to_key != plan->mover_key ||
		    open_index == UINT32_MAX || closed_index == UINT32_MAX ||
		    open_index == closed_index || open_route_count != 1U ||
		    closed_route_count != 1U || open_route == UINT32_MAX ||
		    closed_route == UINT32_MAX ||
		    edges[closed_route].to_key != nodes[open_index].key ||
		    !Codec_TrainCornerShape(&nodes[closed_index]) ||
		    !Codec_TrainCornerShape(&nodes[open_index]) ||
		    !Codec_TrainNoSideEffects(edges, inventory_edges,
		        plan->entry_key) ||
		    !Codec_TrainNoSideEffects(edges, inventory_edges,
		        plan->mover_key) ||
		    !Codec_TrainNoSideEffects(edges, inventory_edges,
		        nodes[closed_index].key) ||
		    !Codec_TrainNoSideEffects(edges, inventory_edges,
		        nodes[open_index].key))
			return RLCODEC_BAD_ACTIVATION_PLAN;
		diagnostic = Codec_ExpectInventoryEdge(edges, plan, button_target,
			generation, workspace, &expected_count);
		if (diagnostic == RLCODEC_OK)
			diagnostic = Codec_ExpectInventoryEdge(edges, plan, train_route,
				generation, workspace, &expected_count);
		if (diagnostic == RLCODEC_OK)
			diagnostic = Codec_ExpectInventoryEdge(edges, plan, closed_route,
				generation, workspace, &expected_count);
		if (diagnostic == RLCODEC_OK)
			diagnostic = Codec_ExpectInventoryEdge(edges, plan, open_route,
				generation, workspace, &expected_count);
		if (diagnostic != RLCODEC_OK)
			return diagnostic;
		break;
	}

	case SG_RUNE_CODEC_CONTROLLER_AUTO_DOOR:
	{
		uint32_t owner_count = Codec_InventoryFanoutCount(edges,
			inventory_edges, plan->entry_key, SG_RUNE_CODEC_EDGE_OWNER);
		uint32_t owner_index = Codec_InventoryFanoutAt(edges,
			inventory_edges, plan->entry_key, SG_RUNE_CODEC_EDGE_OWNER, 0U);

		if (nodes[entry_index].kind !=
		        SG_RUNE_CODEC_NODE_AUTO_DOOR_TRIGGER ||
		    nodes[entry_index].touch_callback !=
		        SG_RUNE_CODEC_CALLBACK_TOUCH_DOOR_TRIGGER ||
		    (nodes[entry_index].flags & SG_RUNE_CODEC_NODEF_SYNTHETIC) == 0U ||
		    owner_count != 1U || owner_index == UINT32_MAX ||
		    edges[owner_index].to_key != plan->mover_key ||
		    plan->cooldown_ms != 1000U)
			return RLCODEC_BAD_ACTIVATION_PLAN;
		diagnostic = Codec_ExpectInventoryEdge(edges, plan, owner_index,
			generation, workspace, &expected_count);
		if (diagnostic != RLCODEC_OK)
			return diagnostic;
		workspace->node_touched[master_count++] = mover_index;
		break;
	}

	case SG_RUNE_CODEC_CONTROLLER_BUTTON_DOOR:
	{
		uint32_t target_count = Codec_InventoryFanoutCount(edges,
			inventory_edges, plan->entry_key, SG_RUNE_CODEC_EDGE_TARGET);
		uint32_t matches = 0U;

		if (!Codec_ButtonNodeShapeValid(&nodes[entry_index]))
			return RLCODEC_BAD_ACTIVATION_NODE;
		if (!Codec_ButtonNodeSemanticValid(&nodes[entry_index], strings) ||
		    target_count == 0U ||
		    plan->cooldown_ms != (uint32_t)nodes[entry_index].wait_ms)
			return RLCODEC_BAD_ACTIVATION_PLAN;
		for (i = 0U; i < num_nodes; i++)
			if (nodes[i].targetname_offset != 0U &&
			    Codec_StringEqualFold(
			        strings + nodes[entry_index].target_offset,
			        strings + nodes[i].targetname_offset))
			{
				matches++;
			}
		if (matches != target_count)
			return RLCODEC_BAD_ACTIVATION_PLAN;
		/* One button may target a canonical captain followed by same-team
		 * slaves with the same targetname.  Preserve that complete engine-order
		 * fanout; the first master opens the team and later slave calls are
		 * authenticated no-ops. */
		for (i = 0U; i < target_count; i++)
		{
			uint32_t target_index = Codec_InventoryFanoutAt(edges,
				inventory_edges, plan->entry_key,
				SG_RUNE_CODEC_EDGE_TARGET, i);
			uint32_t destination_index;

			if (target_index == UINT32_MAX)
				return RLCODEC_BAD_ACTIVATION_PLAN;
			destination_index = Codec_FindNode(nodes, num_nodes,
				edges[target_index].to_key);
			if (destination_index == UINT32_MAX)
				return RLCODEC_BAD_ACTIVATION_PLAN;
			for (j = 0U; j < i; j++)
			{
				uint32_t prior = Codec_InventoryFanoutAt(edges,
					inventory_edges, plan->entry_key,
					SG_RUNE_CODEC_EDGE_TARGET, j);

				if (prior == UINT32_MAX || edges[prior].to_key ==
				    edges[target_index].to_key)
					return RLCODEC_BAD_ACTIVATION_PLAN;
			}
			if (nodes[destination_index].kind ==
			    SG_RUNE_CODEC_NODE_DOOR_MASTER)
			{
				if (master_count != 0U ||
				    nodes[destination_index].key != plan->mover_key)
					return RLCODEC_BAD_ACTIVATION_PLAN;
				workspace->node_touched[master_count++] = destination_index;
			}
			else if (nodes[destination_index].kind !=
			         SG_RUNE_CODEC_NODE_DOOR_MEMBER || master_count != 1U ||
			         nodes[destination_index].team_master_key !=
			             plan->mover_key)
				return RLCODEC_BAD_ACTIVATION_PLAN;
			diagnostic = Codec_ExpectInventoryEdge(edges, plan, target_index,
				generation, workspace, &expected_count);
			if (diagnostic != RLCODEC_OK)
				return diagnostic;
		}
		if (master_count != 1U)
			return RLCODEC_BAD_ACTIVATION_PLAN;
		break;
	}

	case SG_RUNE_CODEC_CONTROLLER_DIRECT_TRIGGER_DOOR:
	{
		uint32_t target_count = Codec_InventoryFanoutCount(edges,
			inventory_edges, plan->entry_key, SG_RUNE_CODEC_EDGE_TARGET);
		uint32_t cooldown = nodes[entry_index].wait_ms >
		        SG_RUNE_CODEC_MAX_TIME_MS
		    ? (uint32_t)SG_RUNE_CODEC_MAX_TIME_MS
		    : (uint32_t)nodes[entry_index].wait_ms;
		uint32_t filtered = 0U;
		uint32_t smallest = UINT32_MAX;

		if (nodes[entry_index].kind != SG_RUNE_CODEC_NODE_TRIGGER ||
		    nodes[entry_index].touch_callback !=
		        SG_RUNE_CODEC_CALLBACK_TOUCH_MULTI ||
		    (nodes[entry_index].flags & SG_RUNE_CODEC_NODEF_REPEATABLE) == 0U ||
		    nodes[entry_index].delay_ms != 0 ||
		    nodes[entry_index].wait_ms <= 0 ||
		    nodes[entry_index].killtarget_offset != 0U ||
		    nodes[entry_index].path_target_offset != 0U ||
		    plan->cooldown_ms != cooldown ||
		    target_count == 0U)
			return RLCODEC_BAD_ACTIVATION_PLAN;
		for (i = 0U; i < plan->num_edges; i++)
		{
			const sg_rune_codec_activation_edge_t *edge =
				&edges[plan->first_edge + i];
			uint32_t expected_index;

			if (edge->from_key != plan->entry_key ||
			    edge->kind != SG_RUNE_CODEC_EDGE_TARGET)
				continue;
			expected_index = Codec_InventoryFanoutAt(edges,
				inventory_edges, plan->entry_key,
				SG_RUNE_CODEC_EDGE_TARGET, filtered++);
			if (expected_index == UINT32_MAX ||
			    memcmp(edge, &edges[expected_index], sizeof(*edge)) != 0)
				return RLCODEC_BAD_ACTIVATION_PLAN;
		}
		if (filtered != target_count)
			return RLCODEC_BAD_ACTIVATION_PLAN;
		for (i = 0U; i < target_count; i++)
		{
			uint32_t target_index = Codec_InventoryFanoutAt(edges,
				inventory_edges, plan->entry_key,
				SG_RUNE_CODEC_EDGE_TARGET, i);
			uint32_t destination_index = Codec_FindNode(nodes, num_nodes,
				edges[target_index].to_key);
			const sg_rune_codec_activation_node_t *destination =
				&nodes[destination_index];

			if (destination->kind == SG_RUNE_CODEC_NODE_DOOR_MASTER)
			{
				for (j = 0U; j < master_count; j++)
					if (workspace->node_touched[j] == destination_index)
						return RLCODEC_BAD_ACTIVATION_PLAN;
				workspace->node_touched[master_count++] = destination_index;
				if (destination->key < smallest)
					smallest = destination->key;
				diagnostic = Codec_ExpectInventoryEdge(edges, plan,
					target_index, generation, workspace, &expected_count);
			}
			else if (destination->kind == SG_RUNE_CODEC_NODE_DOOR_MEMBER)
			{
				int seen_master = 0;

				for (j = 0U; j < master_count; j++)
					if (nodes[workspace->node_touched[j]].key ==
					    destination->team_master_key)
						seen_master = 1;
				if (!seen_master)
					return RLCODEC_BAD_ACTIVATION_PLAN;
				diagnostic = Codec_ExpectInventoryEdge(edges, plan,
					target_index, generation, workspace, &expected_count);
			}
			else
				diagnostic = Codec_AddSideEffect(nodes, num_nodes, edges,
					inventory_edges,
					plan, target_index, 0, generation, workspace,
					&expected_count, &relay_count);
			if (diagnostic != RLCODEC_OK)
				return diagnostic;
		}
		diagnostic = Codec_DiscoverRelayDoorTargets(nodes, num_nodes, edges,
			inventory_edges, workspace, relay_count,
			&master_count, &smallest);
		if (diagnostic != RLCODEC_OK)
			return diagnostic;
		if (master_count == 0U || smallest != plan->mover_key)
			return RLCODEC_BAD_ACTIVATION_PLAN;
		break;
	}

	default:
		return RLCODEC_BAD_ACTIVATION_PLAN;
	}

	if ((plan->controller_kind == SG_RUNE_CODEC_CONTROLLER_PLATFORM &&
	     master_count != 0U) ||
	    (plan->controller_kind == SG_RUNE_CODEC_CONTROLLER_TRAIN_SHOOT &&
	     master_count != 0U) ||
	    plan->controller_kind == SG_RUNE_CODEC_CONTROLLER_AUTO_DOOR ||
	    plan->controller_kind == SG_RUNE_CODEC_CONTROLLER_BUTTON_DOOR ||
	    plan->controller_kind == SG_RUNE_CODEC_CONTROLLER_DIRECT_TRIGGER_DOOR)
	{
		for (i = 0U; i < master_count; i++)
		{
			uint32_t master_node_index = workspace->node_touched[i];
			uint32_t master_key = nodes[master_node_index].key;
			uint32_t member_count = 0U;
			uint32_t team_count;

			if (!Codec_DoorNodeShapeValid(&nodes[master_node_index], 1))
				return RLCODEC_BAD_ACTIVATION_NODE;
			if (plan->controller_kind ==
			        SG_RUNE_CODEC_CONTROLLER_TRAIN_SHOOT &&
			    (nodes[master_node_index].flags &
			        SG_RUNE_CODEC_NODEF_SHOOTABLE) == 0U)
				return RLCODEC_BAD_ACTIVATION_PLAN;
			if (!Codec_DoorNodeSemanticValid(&nodes[master_node_index],
			    master_key, strings))
				return RLCODEC_BAD_ACTIVATION_PLAN;
			physical_count++;
			for (j = 0U; j < num_nodes; j++)
				if (nodes[j].kind == SG_RUNE_CODEC_NODE_DOOR_MEMBER &&
				    nodes[j].team_master_key == master_key)
				{
					if (!Codec_DoorNodeShapeValid(&nodes[j], 0))
						return RLCODEC_BAD_ACTIVATION_NODE;
					if (plan->controller_kind ==
					        SG_RUNE_CODEC_CONTROLLER_TRAIN_SHOOT &&
					    (nodes[j].flags &
					        SG_RUNE_CODEC_NODEF_SHOOTABLE) == 0U)
						return RLCODEC_BAD_ACTIVATION_PLAN;
					if (!Codec_DoorNodeSemanticValid(&nodes[j], master_key,
					    strings))
						return RLCODEC_BAD_ACTIVATION_PLAN;
					member_count++;
					physical_count++;
				}
			team_count = Codec_InventoryFanoutCount(edges, inventory_edges,
				master_key, SG_RUNE_CODEC_EDGE_TEAM);
			if (team_count != member_count)
				return RLCODEC_BAD_ACTIVATION_PLAN;
			for (j = 0U; j < team_count; j++)
			{
				uint32_t team_index = Codec_InventoryFanoutAt(edges,
					inventory_edges, master_key, SG_RUNE_CODEC_EDGE_TEAM, j);
				uint32_t member_index = Codec_FindNode(nodes, num_nodes,
					edges[team_index].to_key);

				if (member_index == UINT32_MAX ||
				    nodes[member_index].kind !=
				        SG_RUNE_CODEC_NODE_DOOR_MEMBER ||
				    nodes[member_index].team_master_key != master_key ||
				    workspace->node_generations[member_index] == generation)
					return RLCODEC_BAD_ACTIVATION_PLAN;
				/* Count equality proves closure only when every TEAM destination is
				 * unique.  Otherwise distinct ordinals could repeat one member and
				 * omit another physical brush while preserving the same count. */
				workspace->node_generations[member_index] = generation;
				diagnostic = Codec_ExpectInventoryEdge(edges, plan, team_index,
					generation, workspace, &expected_count);
				if (diagnostic != RLCODEC_OK)
					return diagnostic;
			}
		}
		if (physical_count != plan->expected_members -
		        (plan->controller_kind == SG_RUNE_CODEC_CONTROLLER_PLATFORM
		            ? 1U : 0U))
			return RLCODEC_BAD_ACTIVATION_PLAN;
		/* Every physical mover's ordered G_UseTargets fanout is part of the
		 * authenticated callback closure. */
		for (i = 0U; i < num_nodes; i++)
		{
			int physical = 0;
			uint32_t target_count;

			for (j = 0U; j < master_count; j++)
				if (nodes[i].key == nodes[workspace->node_touched[j]].key ||
				    (nodes[i].kind == SG_RUNE_CODEC_NODE_DOOR_MEMBER &&
				     nodes[i].team_master_key ==
				         nodes[workspace->node_touched[j]].key))
					physical = 1;
			if (!physical)
				continue;
			target_count = Codec_InventoryFanoutCount(edges, inventory_edges,
				nodes[i].key, SG_RUNE_CODEC_EDGE_TARGET);
			for (j = 0U; j < target_count; j++)
			{
				uint32_t target_index = Codec_InventoryFanoutAt(edges,
					inventory_edges, nodes[i].key,
					SG_RUNE_CODEC_EDGE_TARGET, j);
				diagnostic = Codec_AddSideEffect(nodes, num_nodes, edges,
					inventory_edges,
					plan, target_index, 1, generation, workspace,
					&expected_count, &relay_count);
				if (diagnostic != RLCODEC_OK)
					return diagnostic;
			}
		}
	}

	/* Sound-only relay expansion is iterative and each relay may be invoked
	 * exactly once, preventing cycles and duplicate side effects. */
	for (i = 0U; i < relay_count; i++)
	{
		uint32_t relay_index = workspace->node_queue[i];
		uint32_t target_count = Codec_InventoryFanoutCount(edges,
			inventory_edges, nodes[relay_index].key,
			SG_RUNE_CODEC_EDGE_TARGET);

		if (target_count == 0U)
			return RLCODEC_BAD_ACTIVATION_PLAN;
		for (j = 0U; j < target_count; j++)
		{
			uint32_t target_index = Codec_InventoryFanoutAt(edges,
				inventory_edges, nodes[relay_index].key,
				SG_RUNE_CODEC_EDGE_TARGET, j);

			diagnostic = Codec_AddRelayEffect(nodes, num_nodes, edges,
				inventory_edges, plan, target_index, generation, workspace,
				&expected_count, &relay_count, master_count);
			if (diagnostic != RLCODEC_OK)
				return diagnostic;
		}
	}
	if (expected_count != plan->num_edges)
		return RLCODEC_BAD_ACTIVATION_PLAN;
	return RLCODEC_OK;
}

static sg_rune_codec_diagnostic_t Codec_ValidateOnePlan(
	const sg_rune_codec_activation_node_t *nodes, uint32_t num_nodes,
	const sg_rune_codec_activation_edge_t *edges, uint32_t num_edges,
	uint32_t inventory_edges, const sg_rune_codec_activation_plan_t *plan,
	uint32_t plan_index, const sg_rune_codec_link_t *owner_link,
	const unsigned char *strings,
	sg_rune_codec_workspace_t *workspace)
{
	return Codec_ValidateProductionPlanExact(nodes, num_nodes, edges,
		num_edges, inventory_edges, plan, plan_index, owner_link, strings,
		workspace);
}

sg_rune_codec_diagnostic_t SG_RuneCodecValidate(
	const sg_rune_codec_seed_t *seeds, uint32_t num_seeds,
	const sg_rune_codec_link_t *links, uint32_t num_links,
	const sg_rune_codec_activation_node_t *nodes, uint32_t num_nodes,
	const sg_rune_codec_activation_edge_t *edges, uint32_t num_edges,
	const sg_rune_codec_activation_plan_t *plans, uint32_t num_plans,
	const unsigned char *strings, uint32_t string_bytes,
	sg_rune_codec_workspace_t *workspace)
{
	size_t ignored;
	uint32_t inventory_edges;
	uint32_t expected_first_edge;
	uint32_t i;
	sg_rune_codec_diagnostic_t diagnostic;

	diagnostic = SG_RuneCodecFileSize(num_seeds, num_links, num_nodes,
		num_edges, num_plans, string_bytes, &ignored);
	if (diagnostic != RLCODEC_OK)
		return diagnostic;
	if (!seeds || (num_links != 0U && !links) || !strings || !workspace ||
	    (num_nodes != 0U && !nodes) || (num_edges != 0U && !edges) ||
	    (num_plans != 0U && !plans))
		return Codec_Diagnostic(RLW_INVALID_ARGUMENT);
	diagnostic = Codec_ValidateActionGraph(seeds, num_seeds, links,
		num_links, workspace);
	if (diagnostic != RLCODEC_OK)
		return diagnostic;
	/* Mechanism actions require authenticated controller authority;
	 * planless actions must not smuggle an unrelated controller binding. */
	for (i = 0U; i < num_links; i++)
	{
		int has_plan = links[i].activation_plan !=
			SG_RUNE_CODEC_NO_ACTIVATION_PLAN;

		if (has_plan != SG_ActionMechanismPlanRequired(links[i].action) ||
		    (has_plan && (links[i].activation_plan >= num_plans ||
		    !SG_ActionMechanismPlanAllowed(links[i].action,
		        plans[links[i].activation_plan].controller_kind))))
			return RLCODEC_BAD_ACTIVATION_PLAN;
	}
	if (num_nodes == 0U)
	{
		if (num_edges != 0U || num_plans != 0U ||
		    string_bytes != 1U || strings[0] != 0U)
			return RLCODEC_BAD_STRING_POOL;
		return RLCODEC_OK;
	}
	if (num_plans > num_links)
		return RLCODEC_BAD_ACTIVATION_PLAN;
	inventory_edges = num_plans != 0U ? plans[0].first_edge : num_edges;
	if (inventory_edges > num_edges)
		return RLCODEC_BAD_ACTIVATION_PLAN;
	if (!Codec_WorkspaceMechanismReady(workspace, num_nodes, num_edges,
	    num_plans))
		return Codec_Diagnostic(RLW_ALLOCATION_FAILED);
	if (num_plans != 0U)
		memset(workspace->plan_references, 0,
			(size_t)num_plans * sizeof(workspace->plan_references[0]));
	memset(workspace->node_references, 0,
		(size_t)num_nodes * sizeof(workspace->node_references[0]));
	memset(workspace->node_generations, 0,
		(size_t)num_nodes * sizeof(workspace->node_generations[0]));
	if (num_edges != 0U)
		memset(workspace->edge_next, 0,
			(size_t)num_edges * sizeof(workspace->edge_next[0]));
	for (i = 0U; i < num_nodes; i++)
	{
		diagnostic = Codec_ValidateNodeFields(&nodes[i]);
		if (diagnostic != RLCODEC_OK)
			return diagnostic;
		if (i != 0U && nodes[i - 1U].key >= nodes[i].key)
			return RLCODEC_DUPLICATE_NODE_KEY;
	}
	for (i = 0U; i < num_nodes; i++)
	{
		uint32_t owner = nodes[i].owner_key;
		uint32_t team_master = nodes[i].team_master_key;

		if ((owner != SG_RUNE_CODEC_NO_KEY &&
		     Codec_FindNode(nodes, num_nodes, owner) == UINT32_MAX) ||
		    (team_master != SG_RUNE_CODEC_NO_KEY &&
		     Codec_FindNode(nodes, num_nodes, team_master) == UINT32_MAX))
			return RLCODEC_BAD_MECHANISM_GRAPH;
		if ((nodes[i].flags & SG_RUNE_CODEC_NODEF_TEAM_MEMBER) != 0U)
		{
			uint32_t master_index = Codec_FindNode(nodes, num_nodes,
				team_master);

			if (master_index == UINT32_MAX ||
			    (nodes[master_index].flags &
			     SG_RUNE_CODEC_NODEF_TEAM_MASTER) == 0U)
				return RLCODEC_BAD_MECHANISM_GRAPH;
		}
	}
	diagnostic = Codec_ValidateStringPool(nodes, num_nodes, strings,
		string_bytes, workspace);
	if (diagnostic != RLCODEC_OK)
		return diagnostic;
	for (i = 0U; i < num_nodes; i++)
		if ((nodes[i].flags &
		     SG_RUNE_CODEC_NODEF_FRAME_COMPLETE_MOVER) != 0U &&
		    !Codec_FrameCompleteButtonSemanticValid(&nodes[i], strings))
			return RLCODEC_BAD_ACTIVATION_NODE;
	/* The immutable inventory prefix is ordered by source, kind, then the
	 * engine-traversal ordinal within that source-kind fanout. */
	for (i = 0U; i < inventory_edges; i++)
	{
		const sg_rune_codec_activation_edge_t *edge = &edges[i];

		diagnostic = Codec_ValidateEdgeFields(edge);
		if (diagnostic != RLCODEC_OK ||
		    Codec_FindNode(nodes, num_nodes, edge->from_key) == UINT32_MAX ||
		    Codec_FindNode(nodes, num_nodes, edge->to_key) == UINT32_MAX)
			return RLCODEC_BAD_ACTIVATION_EDGE;
		if (i == 0U || edges[i - 1U].from_key != edge->from_key ||
		    edges[i - 1U].kind != edge->kind)
		{
			if (edge->ordinal != 0U)
				return RLCODEC_BAD_ACTIVATION_EDGE;
		}
		else if ((uint32_t)edges[i - 1U].ordinal + 1U !=
		    (uint32_t)edge->ordinal)
			return RLCODEC_BAD_ACTIVATION_EDGE;
		if (i != 0U &&
		    (edges[i - 1U].from_key > edge->from_key ||
		     (edges[i - 1U].from_key == edge->from_key &&
		      edges[i - 1U].kind > edge->kind)))
			return RLCODEC_BAD_ACTIVATION_EDGE;
	}
	for (i = 0U; i < num_links; i++)
	{
		uint32_t plan_index = links[i].activation_plan;

		if (plan_index == SG_RUNE_CODEC_NO_ACTIVATION_PLAN)
			continue;
		if (workspace->plan_references[plan_index] != 0U)
			return RLCODEC_BAD_ACTIVATION_PLAN;
		workspace->plan_references[plan_index] = i + 1U;
	}
	expected_first_edge = inventory_edges;
	for (i = 0U; i < num_plans; i++)
	{
		if (plans[i].first_edge != expected_first_edge)
			return RLCODEC_BAD_ACTIVATION_PLAN;
		/* Every executable edge is an exact copy of an inventory edge. */
		{
			uint32_t j;

			for (j = 0U; j < plans[i].num_edges; j++)
			{
				uint32_t plan_edge = plans[i].first_edge + j;
				uint32_t inventory;

				if (plan_edge >= num_edges)
					return RLCODEC_BAD_ACTIVATION_PLAN;
				for (inventory = 0U; inventory < inventory_edges;
				     inventory++)
					if (memcmp(&edges[plan_edge], &edges[inventory],
					    sizeof(edges[plan_edge])) == 0)
						break;
				if (inventory == inventory_edges)
					return RLCODEC_BAD_ACTIVATION_PLAN;
			}
		}
		diagnostic = Codec_ValidateOnePlan(nodes, num_nodes, edges,
			num_edges, inventory_edges, &plans[i], i,
			workspace->plan_references[i] == 0U ? NULL :
			    &links[workspace->plan_references[i] - 1U],
			strings, workspace);
		if (diagnostic != RLCODEC_OK)
			return diagnostic;
		expected_first_edge += plans[i].num_edges;
	}
	if (expected_first_edge != num_edges)
		return RLCODEC_BAD_ACTIVATION_PLAN;
	for (i = 0U; i < num_plans; i++)
		if (workspace->plan_references[i] == 0U)
			return RLCODEC_BAD_ACTIVATION_PLAN;
	return RLCODEC_OK;
}

static sg_rune_codec_diagnostic_t Codec_ValidateIdentity(
	const sg_rune_codec_identity_t *identity)
{
	if (!identity)
		return Codec_Diagnostic(RLW_INVALID_ARGUMENT);
	if (!Codec_MapNameValid(identity->map_name))
		return Codec_Diagnostic(RLW_BAD_MAPNAME);
	return Codec_ValidatePhysics(identity->physics_flags, identity->gravity,
		identity->airaccelerate, identity->maxvelocity,
		identity->pmove_substep_ms, identity->server_frame_ms,
		identity->host_physics_id);
}

sg_rune_codec_diagnostic_t SG_RuneCodecMatchIdentity(
	const sg_rune_codec_header_t *header,
	const sg_rune_codec_identity_t *expected_identity)
{
	sg_rune_codec_diagnostic_t diagnostic;

	diagnostic = Codec_ValidateHeaderFixed(header);
	if (diagnostic != RLCODEC_OK)
		return diagnostic;
	diagnostic = Codec_ValidateHeaderSemantic(header);
	if (diagnostic != RLCODEC_OK)
		return diagnostic;
	if (!expected_identity)
		return RLCODEC_OK;
	diagnostic = Codec_ValidateIdentity(expected_identity);
	if (diagnostic != RLCODEC_OK)
		return diagnostic;
	if (memcmp(header->map_name, expected_identity->map_name,
	    SG_RUNE_CODEC_MAP_NAME_BYTES) != 0)
		return Codec_Diagnostic(RLW_MAPNAME_MISMATCH);
	if (header->bsp_checksum != expected_identity->bsp_checksum)
		return Codec_Diagnostic(RLW_BSP_CHECKSUM_MISMATCH);
	if (header->entity_crc32 != expected_identity->entity_crc32)
		return Codec_Diagnostic(RLW_ENTITY_CRC_MISMATCH);
	if (header->host_physics_id != expected_identity->host_physics_id)
		return Codec_Diagnostic(RLW_PHYSICS_ID_MISMATCH);
	if (header->physics_flags != expected_identity->physics_flags ||
	    Codec_FloatBits(header->gravity) !=
	        Codec_FloatBits(expected_identity->gravity) ||
	    Codec_FloatBits(header->airaccelerate) !=
	        Codec_FloatBits(expected_identity->airaccelerate) ||
	    Codec_FloatBits(header->maxvelocity) !=
	        Codec_FloatBits(expected_identity->maxvelocity) ||
	    header->pmove_substep_ms != expected_identity->pmove_substep_ms ||
	    header->server_frame_ms != expected_identity->server_frame_ms)
		return Codec_Diagnostic(RLW_BAD_PHYSICS_LAW);
	return RLCODEC_OK;
}

sg_rune_codec_diagnostic_t SG_RuneCodecEncode(
	const sg_rune_codec_identity_t *identity,
	const sg_rune_codec_seed_t *seeds, uint32_t num_seeds,
	const sg_rune_codec_link_t *links, uint32_t num_links,
	const sg_rune_codec_activation_node_t *nodes, uint32_t num_nodes,
	const sg_rune_codec_activation_edge_t *edges, uint32_t num_edges,
	const sg_rune_codec_activation_plan_t *plans, uint32_t num_plans,
	const unsigned char *strings, uint32_t string_bytes,
	sg_rune_codec_workspace_t *workspace, unsigned char *encoded,
	size_t encoded_capacity, size_t *encoded_size_out)
{
	sg_rune_codec_header_t header;
	size_t encoded_size;
	size_t offset;
	uint32_t payload_crc;
	uint32_t i;
	sg_rune_codec_diagnostic_t diagnostic;

	if (!encoded_size_out)
		return Codec_Diagnostic(RLW_INVALID_ARGUMENT);
	*encoded_size_out = 0U;
	diagnostic = Codec_ValidateIdentity(identity);
	if (diagnostic != RLCODEC_OK)
		return diagnostic;
	diagnostic = SG_RuneCodecFileSize(num_seeds, num_links, num_nodes,
		num_edges, num_plans, string_bytes, &encoded_size);
	if (diagnostic != RLCODEC_OK)
		return diagnostic;
	if (!encoded)
		return Codec_Diagnostic(RLW_INVALID_ARGUMENT);
	if (encoded_capacity < encoded_size)
		return Codec_Diagnostic(RLW_BAD_FILE_SIZE);
	diagnostic = SG_RuneCodecValidate(seeds, num_seeds, links, num_links,
		nodes, num_nodes, edges, num_edges, plans, num_plans, strings,
		string_bytes, workspace);
	if (diagnostic != RLCODEC_OK)
		return diagnostic;
	offset = SG_RUNE_CODEC_HEADER_BYTES;
	for (i = 0U; i < num_seeds; i++)
	{
		diagnostic = SG_RuneCodecEncodeSeed(&seeds[i], encoded + offset,
			SG_RUNE_CODEC_SEED_BYTES);
		if (diagnostic != RLCODEC_OK)
			return diagnostic;
		offset += SG_RUNE_CODEC_SEED_BYTES;
	}
	for (i = 0U; i < num_links; i++)
	{
		diagnostic = SG_RuneCodecEncodeLink(&links[i], encoded + offset,
			SG_RUNE_CODEC_LINK_BYTES);
		if (diagnostic != RLCODEC_OK)
			return diagnostic;
		offset += SG_RUNE_CODEC_LINK_BYTES;
	}
	for (i = 0U; i < num_nodes; i++)
	{
		diagnostic = SG_RuneCodecEncodeActivationNode(&nodes[i],
			encoded + offset, SG_RUNE_CODEC_ACTIVATION_NODE_BYTES);
		if (diagnostic != RLCODEC_OK)
			return diagnostic;
		offset += SG_RUNE_CODEC_ACTIVATION_NODE_BYTES;
	}
	for (i = 0U; i < num_edges; i++)
	{
		diagnostic = SG_RuneCodecEncodeActivationEdge(&edges[i],
			encoded + offset, SG_RUNE_CODEC_ACTIVATION_EDGE_BYTES);
		if (diagnostic != RLCODEC_OK)
			return diagnostic;
		offset += SG_RUNE_CODEC_ACTIVATION_EDGE_BYTES;
	}
	for (i = 0U; i < num_plans; i++)
	{
		diagnostic = SG_RuneCodecEncodeActivationPlan(&plans[i],
			encoded + offset, SG_RUNE_CODEC_ACTIVATION_PLAN_BYTES);
		if (diagnostic != RLCODEC_OK)
			return diagnostic;
		offset += SG_RUNE_CODEC_ACTIVATION_PLAN_BYTES;
	}
	memcpy(encoded + offset, strings, string_bytes);
	offset += string_bytes;
	if (offset != encoded_size ||
	    !SG_CRC32Buffer(encoded + SG_RUNE_CODEC_HEADER_BYTES,
	        encoded_size - SG_RUNE_CODEC_HEADER_BYTES, &payload_crc))
		return Codec_Diagnostic(RLW_INVALID_ARGUMENT);
	memset(&header, 0, sizeof(header));
	header.magic = SG_RUNE_CODEC_MAGIC;
	header.header_bytes = SG_RUNE_CODEC_HEADER_BYTES;
	header.seed_bytes = SG_RUNE_CODEC_SEED_BYTES;
	header.link_bytes = SG_RUNE_CODEC_LINK_BYTES;
	header.num_seeds = num_seeds;
	header.num_links = num_links;
	header.payload_crc32 = payload_crc;
	header.bsp_checksum = identity->bsp_checksum;
	header.entity_crc32 = identity->entity_crc32;
	header.action_contract_crc32 = SG_RUNE_ACTION_CONTRACT_CRC32;
	header.physics_flags = identity->physics_flags;
	header.gravity = identity->gravity;
	header.airaccelerate = identity->airaccelerate;
	header.maxvelocity = identity->maxvelocity;
	header.pmove_substep_ms = identity->pmove_substep_ms;
	header.server_frame_ms = identity->server_frame_ms;
	header.host_physics_id = identity->host_physics_id;
	memcpy(header.map_name, identity->map_name, SG_RUNE_CODEC_MAP_NAME_BYTES);
	header.activation_node_bytes = SG_RUNE_CODEC_ACTIVATION_NODE_BYTES;
	header.activation_edge_bytes = SG_RUNE_CODEC_ACTIVATION_EDGE_BYTES;
	header.activation_plan_bytes = SG_RUNE_CODEC_ACTIVATION_PLAN_BYTES;
	header.num_activation_nodes = num_nodes;
	header.num_activation_edges = num_edges;
	header.num_activation_plans = num_plans;
	header.string_bytes = string_bytes;
	header.mechanism_contract_crc32 = SG_RUNE_MECHANISM_CONTRACT_CRC32;
	header.num_inventory_edges = num_plans != 0U
		? plans[0].first_edge : num_edges;
	diagnostic = SG_RuneCodecEncodeHeader(&header, encoded,
		SG_RUNE_CODEC_HEADER_BYTES);
	if (diagnostic != RLCODEC_OK)
		return diagnostic;
	*encoded_size_out = encoded_size;
	return RLCODEC_OK;
}

typedef struct codec_memory_range_s
{
	const void *data;
	size_t bytes;
} codec_memory_range_t;

static int Codec_RangesOverlap(const codec_memory_range_t *left,
	const codec_memory_range_t *right)
{
	uintptr_t left_start;
	uintptr_t right_start;
	uintptr_t left_end;
	uintptr_t right_end;

	if (!left || !right || !left->data || !right->data ||
	    left->bytes == 0U || right->bytes == 0U)
		return 0;
	left_start = (uintptr_t)left->data;
	right_start = (uintptr_t)right->data;
	if (left->bytes > (size_t)(UINTPTR_MAX - left_start) ||
	    right->bytes > (size_t)(UINTPTR_MAX - right_start))
		return 1;
	left_end = left_start + (uintptr_t)left->bytes;
	right_end = right_start + (uintptr_t)right->bytes;
	return left_start < right_end && right_start < left_end;
}

static int Codec_DecodeRangesDisjoint(const unsigned char *encoded,
	size_t encoded_size, const sg_rune_codec_identity_t *expected_identity,
	sg_rune_codec_header_t *header_out,
	sg_rune_codec_seed_t *seeds, uint32_t num_seeds,
	sg_rune_codec_link_t *links, uint32_t num_links,
	sg_rune_codec_activation_node_t *nodes, uint32_t num_nodes,
	sg_rune_codec_activation_edge_t *edges, uint32_t num_edges,
	sg_rune_codec_activation_plan_t *plans, uint32_t num_plans,
	unsigned char *strings, uint32_t string_bytes,
	sg_rune_codec_workspace_t *workspace)
{
	codec_memory_range_t ranges[23];
	size_t count = 0U;
	size_t i;
	size_t j;

#define Codec_ADD_RANGE(pointer_, bytes_) do { \
	ranges[count].data = (pointer_); \
	ranges[count].bytes = (bytes_); \
	count++; \
} while (0)
	Codec_ADD_RANGE(encoded, encoded_size);
	Codec_ADD_RANGE(expected_identity,
		expected_identity ? sizeof(*expected_identity) : 0U);
	Codec_ADD_RANGE(workspace, workspace ? sizeof(*workspace) : 0U);
	Codec_ADD_RANGE(header_out, sizeof(*header_out));
	Codec_ADD_RANGE(seeds, (size_t)num_seeds * sizeof(*seeds));
	Codec_ADD_RANGE(links, (size_t)num_links * sizeof(*links));
	Codec_ADD_RANGE(nodes, (size_t)num_nodes * sizeof(*nodes));
	Codec_ADD_RANGE(edges, (size_t)num_edges * sizeof(*edges));
	Codec_ADD_RANGE(plans, (size_t)num_plans * sizeof(*plans));
	Codec_ADD_RANGE(strings, (size_t)string_bytes);
	if (workspace)
	{
		Codec_ADD_RANGE(workspace->graph_link_keys,
			(size_t)num_links * sizeof(workspace->graph_link_keys[0]));
		Codec_ADD_RANGE(workspace->graph_source_marks,
			(size_t)num_seeds * sizeof(workspace->graph_source_marks[0]));
		Codec_ADD_RANGE(workspace->plan_references,
			(size_t)num_plans * sizeof(workspace->plan_references[0]));
		Codec_ADD_RANGE(workspace->node_references,
			(size_t)num_nodes * sizeof(workspace->node_references[0]));
		Codec_ADD_RANGE(workspace->node_heads,
			(size_t)num_nodes * sizeof(workspace->node_heads[0]));
		Codec_ADD_RANGE(workspace->node_indegrees,
			(size_t)num_nodes * sizeof(workspace->node_indegrees[0]));
		Codec_ADD_RANGE(workspace->node_generations,
			(size_t)num_nodes * sizeof(workspace->node_generations[0]));
		Codec_ADD_RANGE(workspace->node_touched,
			(size_t)num_nodes * sizeof(workspace->node_touched[0]));
		Codec_ADD_RANGE(workspace->node_queue,
			(size_t)num_nodes * sizeof(workspace->node_queue[0]));
		Codec_ADD_RANGE(workspace->edge_next,
			(size_t)num_edges * sizeof(workspace->edge_next[0]));
		Codec_ADD_RANGE(workspace->string_marks, (size_t)string_bytes);
	}
#undef Codec_ADD_RANGE
	for (i = 0U; i < count; i++)
		for (j = i + 1U; j < count; j++)
			if (Codec_RangesOverlap(&ranges[i], &ranges[j]))
				return 0;
	return 1;
}

sg_rune_codec_diagnostic_t SG_RuneCodecDecode(
	const unsigned char *encoded, size_t encoded_size,
	const sg_rune_codec_identity_t *expected_identity,
	sg_rune_codec_header_t *header_out,
	sg_rune_codec_seed_t *seeds, size_t seed_capacity,
	sg_rune_codec_link_t *links, size_t link_capacity,
	sg_rune_codec_activation_node_t *nodes, size_t node_capacity,
	sg_rune_codec_activation_edge_t *edges, size_t edge_capacity,
	sg_rune_codec_activation_plan_t *plans, size_t plan_capacity,
	unsigned char *strings, size_t string_capacity,
	sg_rune_codec_workspace_t *workspace)
{
	sg_rune_codec_header_t header;
	size_t expected_size;
	size_t offset;
	size_t string_offset;
	uint32_t payload_crc;
	uint32_t i;
	sg_rune_codec_diagnostic_t diagnostic;

	if (!encoded || !header_out)
		return Codec_Diagnostic(RLW_INVALID_ARGUMENT);
	if (encoded_size < SG_RUNE_CODEC_HEADER_BYTES)
		return Codec_Diagnostic(RLW_BAD_FILE_SIZE);
	diagnostic = SG_RuneCodecDecodeHeader(encoded, SG_RUNE_CODEC_HEADER_BYTES,
		&header);
	if (diagnostic != RLCODEC_OK)
		return diagnostic;
	diagnostic = SG_RuneCodecFileSize(header.num_seeds, header.num_links,
		header.num_activation_nodes, header.num_activation_edges,
		header.num_activation_plans, header.string_bytes, &expected_size);
	if (diagnostic != RLCODEC_OK)
		return diagnostic;
	if (encoded_size != expected_size)
		return Codec_Diagnostic(RLW_BAD_FILE_SIZE);
	if (!SG_CRC32Buffer(encoded + SG_RUNE_CODEC_HEADER_BYTES,
	    encoded_size - SG_RUNE_CODEC_HEADER_BYTES, &payload_crc))
		return Codec_Diagnostic(RLW_INVALID_ARGUMENT);
	if (payload_crc != header.payload_crc32)
		return Codec_Diagnostic(RLW_BAD_PAYLOAD_CRC);
	if (!seeds || seed_capacity < (size_t)header.num_seeds ||
	    (header.num_links != 0U &&
	     (!links || link_capacity < (size_t)header.num_links)) ||
	    (header.num_activation_nodes != 0U &&
	     (!nodes || node_capacity < (size_t)header.num_activation_nodes)) ||
	    (header.num_activation_edges != 0U &&
	     (!edges || edge_capacity < (size_t)header.num_activation_edges)) ||
	    (header.num_activation_plans != 0U &&
	     (!plans || plan_capacity < (size_t)header.num_activation_plans)) ||
	    !strings || string_capacity < (size_t)header.string_bytes)
		return Codec_Diagnostic(RLW_ALLOCATION_FAILED);
	if (!Codec_DecodeRangesDisjoint(encoded, encoded_size,
	    expected_identity, header_out, seeds, header.num_seeds, links,
	    header.num_links, nodes, header.num_activation_nodes, edges,
	    header.num_activation_edges, plans, header.num_activation_plans,
	    strings, header.string_bytes, workspace))
		return Codec_Diagnostic(RLW_INVALID_ARGUMENT);
	offset = SG_RUNE_CODEC_HEADER_BYTES;
	for (i = 0U; i < header.num_seeds; i++)
	{
		diagnostic = SG_RuneCodecDecodeSeed(encoded + offset,
			SG_RUNE_CODEC_SEED_BYTES, &seeds[i]);
		if (diagnostic != RLCODEC_OK)
			return diagnostic;
		offset += SG_RUNE_CODEC_SEED_BYTES;
	}
	for (i = 0U; i < header.num_links; i++)
	{
		diagnostic = SG_RuneCodecDecodeLink(encoded + offset,
			SG_RUNE_CODEC_LINK_BYTES, &links[i]);
		if (diagnostic != RLCODEC_OK)
			return diagnostic;
		offset += SG_RUNE_CODEC_LINK_BYTES;
	}
	for (i = 0U; i < header.num_activation_nodes; i++)
	{
		diagnostic = SG_RuneCodecDecodeActivationNode(encoded + offset,
			SG_RUNE_CODEC_ACTIVATION_NODE_BYTES, &nodes[i]);
		if (diagnostic != RLCODEC_OK)
			return diagnostic;
		offset += SG_RUNE_CODEC_ACTIVATION_NODE_BYTES;
	}
	for (i = 0U; i < header.num_activation_edges; i++)
	{
		diagnostic = SG_RuneCodecDecodeActivationEdge(encoded + offset,
			SG_RUNE_CODEC_ACTIVATION_EDGE_BYTES, &edges[i]);
		if (diagnostic != RLCODEC_OK)
			return diagnostic;
		offset += SG_RUNE_CODEC_ACTIVATION_EDGE_BYTES;
	}
	for (i = 0U; i < header.num_activation_plans; i++)
	{
		diagnostic = SG_RuneCodecDecodeActivationPlan(encoded + offset,
			SG_RUNE_CODEC_ACTIVATION_PLAN_BYTES, &plans[i]);
		if (diagnostic != RLCODEC_OK)
			return diagnostic;
		offset += SG_RUNE_CODEC_ACTIVATION_PLAN_BYTES;
	}
	string_offset = offset;
	offset += header.string_bytes;
	if (offset != encoded_size)
		return Codec_Diagnostic(RLW_BAD_FILE_SIZE);
	diagnostic = SG_RuneCodecValidate(seeds, header.num_seeds, links,
		header.num_links, nodes, header.num_activation_nodes, edges,
		header.num_activation_edges, plans, header.num_activation_plans,
		encoded + string_offset, header.string_bytes, workspace);
	if (diagnostic != RLCODEC_OK)
		return diagnostic;
	if (header.num_inventory_edges != (header.num_activation_plans != 0U
	    ? plans[0].first_edge : header.num_activation_edges))
		return RLCODEC_BAD_MECHANISM_CONTRACT;
	diagnostic = SG_RuneCodecMatchIdentity(&header, expected_identity);
	if (diagnostic != RLCODEC_OK)
		return diagnostic;
	memmove(strings, encoded + string_offset, header.string_bytes);
	*header_out = header;
	return RLCODEC_OK;
}
