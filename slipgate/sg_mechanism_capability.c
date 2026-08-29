#include "sg_mechanism_capability_internal.h"

#include <float.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>

typedef struct sg_mechanism_edge_ref_s
{
	uint32_t edge;
	uint32_t source;
	uint32_t destination;
	uint32_t kind;
	uint32_t fanout;
} sg_mechanism_edge_ref_t;

typedef struct sg_mechanism_trace_ref_s
{
	uint32_t trace;
	uint32_t mechanism;
	uint32_t kind;
	uint32_t controller;
	uint32_t source_region;
	uint32_t destination_region;
	uint32_t source_phase;
	uint32_t destination_phase;
	uint32_t source_state;
	uint32_t destination_state;
	uint64_t identity;
} sg_mechanism_trace_ref_t;

typedef struct sg_mechanism_trace_index_s
{
	uint64_t identity;
	uint32_t fact;
} sg_mechanism_trace_index_t;

typedef struct sg_mechanism_candidate_index_s
{
	uint64_t identity;
	uint32_t candidate;
	uint32_t trace;
} sg_mechanism_candidate_index_t;

typedef struct sg_mechanism_relation_ref_s
{
	uint32_t controller;
	uint32_t mechanism;
} sg_mechanism_relation_ref_t;

typedef struct sg_mechanism_build_s
{
	const sg_mechanism_capability_source_t *source;
	sg_mechanism_capability_payload_t *output;
	uint32_t topology_edge_capacity;
	sg_mechanism_edge_ref_t *edge_refs;
	uint32_t *edge_offsets;
	uint32_t *queue;
	uint32_t *distance;
	uint32_t *ways;
	uint32_t *propagated;
	uint32_t *parent;
	uint32_t *parent_edge;
	uint32_t *path;
	sg_mechanism_capability_error_t error;
} sg_mechanism_build_t;

static void SetError(sg_mechanism_build_t *build,
	sg_mechanism_capability_error_code_t code, uint32_t source_index)
{
	if (build->error.code == SG_MECHANISM_CAPABILITY_ERROR_NONE)
	{
		build->error.code = code;
		build->error.source_index = source_index;
	}
}

static int AllocationFits(size_t count, size_t element_size)
{
	return element_size != 0U && count <= SIZE_MAX / element_size;
}

static atomic_uintptr_t next_authority_token = ATOMIC_VAR_INIT(1U);
static atomic_flag authority_key_lock = ATOMIC_FLAG_INIT;
static atomic_int authority_key_state = ATOMIC_VAR_INIT(0);
static uint64_t authority_key[2];

static int FillRandom(void *buffer, size_t size)
{
	unsigned char *bytes = buffer;
	size_t offset = 0U;

	while (offset < size)
	{
		ssize_t count = getrandom(bytes + offset, size - offset, 0U);

		if (count > 0)
		{
			offset += (size_t)count;
			continue;
		}
		if (count < 0 && errno == EINTR)
			continue;
		return 0;
	}
	return 1;
}

static int AuthorityKeyReady(void)
{
	int state = atomic_load_explicit(&authority_key_state, memory_order_acquire);

	if (state != 0)
		return state > 0;
	while (atomic_flag_test_and_set_explicit(&authority_key_lock,
		memory_order_acquire))
		;
	state = atomic_load_explicit(&authority_key_state, memory_order_relaxed);
	if (state == 0)
	{
		state = FillRandom(authority_key, sizeof(authority_key)) ? 1 : -1;
		atomic_store_explicit(&authority_key_state, state, memory_order_release);
	}
	atomic_flag_clear_explicit(&authority_key_lock, memory_order_release);
	return state > 0;
}

static uint32_t AuthorityRound(uint32_t value, uint32_t round)
{
	uint64_t mixed = (uint64_t)value ^ authority_key[round & 1U] ^
		((uint64_t)round * UINT64_C(0x9e3779b97f4a7c15));

	mixed ^= mixed >> 30U;
	mixed *= UINT64_C(0xbf58476d1ce4e5b9);
	mixed ^= mixed >> 27U;
	mixed *= UINT64_C(0x94d049bb133111eb);
	mixed ^= mixed >> 31U;
	return (uint32_t)mixed;
}

static uint64_t AuthorityPermute64(uint64_t value)
{
	uint32_t left = (uint32_t)(value >> 32U);
	uint32_t right = (uint32_t)value;
	uint32_t round;

	for (round = 0U; round < 8U; round++)
	{
		uint32_t next = left ^ AuthorityRound(right, round);

		left = right;
		right = next;
	}
	return ((uint64_t)left << 32U) | (uint64_t)right;
}

#if UINTPTR_MAX == UINT32_MAX
static uint32_t AuthorityPermute32(uint32_t value)
{
	uint16_t left = (uint16_t)(value >> 16U);
	uint16_t right = (uint16_t)value;
	uint32_t round;

	for (round = 0U; round < 8U; round++)
	{
		uint16_t next = (uint16_t)(left ^
			(uint16_t)AuthorityRound((uint32_t)right, round));

		left = right;
		right = next;
	}
	return ((uint32_t)left << 16U) | (uint32_t)right;
}
#endif

int SG_AuthorityTokenMint(uintptr_t *token_out)
{
	uintptr_t current;
	uintptr_t token;

	if (!token_out || !AuthorityKeyReady())
		return 0;
	for (;;)
	{
		current = atomic_load_explicit(&next_authority_token,
			memory_order_relaxed);
		while (current != 0U && current < UINTPTR_MAX)
			if (atomic_compare_exchange_weak_explicit(&next_authority_token,
				&current, current + 1U, memory_order_relaxed,
				memory_order_relaxed))
				break;
		if (current == 0U || current == UINTPTR_MAX)
			return 0;
#if UINTPTR_MAX == UINT64_MAX
		token = (uintptr_t)AuthorityPermute64((uint64_t)current);
#elif UINTPTR_MAX == UINT32_MAX
		token = (uintptr_t)AuthorityPermute32((uint32_t)current);
#else
#error unsupported uintptr_t width
#endif
		if (token != 0U && token != UINTPTR_MAX)
		{
			*token_out = token;
			return 1;
		}
	}
}

static void ReleaseCapabilityStorage(
	sg_mechanism_capability_payload_t *capabilities)
{
	free(capabilities->facts);
	free(capabilities->topology_edges);
	free(capabilities->topology_relations);
	free(capabilities->mechanism_offsets);
	free(capabilities->facts_by_trace);
	capabilities->facts = NULL;
	capabilities->topology_edges = NULL;
	capabilities->topology_relations = NULL;
	capabilities->mechanism_offsets = NULL;
	capabilities->facts_by_trace = NULL;
}

static void RefreshCapabilityView(sg_mechanism_capability_payload_t *capabilities)
{
	capabilities->view.identity = capabilities->identity;
	capabilities->view.candidate_verifier_identity =
		capabilities->candidate_verifier_identity;
	capabilities->view.trace_verifier_identity =
		capabilities->trace_verifier_identity;
	capabilities->view.content_identity = capabilities->content_identity;
	capabilities->view.facts = capabilities->facts;
	capabilities->view.fact_count = capabilities->fact_count;
	capabilities->view.topology_edges = capabilities->topology_edges;
	capabilities->view.topology_edge_count = capabilities->topology_edge_count;
	capabilities->view.topology_relations = capabilities->topology_relations;
	capabilities->view.topology_relation_count =
		capabilities->topology_relation_count;
	capabilities->view.mechanism_offsets = capabilities->mechanism_offsets;
	capabilities->view.mechanism_offset_count =
		capabilities->mechanism_offset_count;
	capabilities->view.facts_by_trace = capabilities->facts_by_trace;
	capabilities->view.topology_edge_visits =
		capabilities->topology_edge_visits;
}

int SG_MechanismCapabilityOwnerCreate(
	sg_mechanism_capability_owner_t **owner_out)
{
	sg_mechanism_capability_owner_t *owner;

	if (!owner_out || *owner_out)
		return 0;
	owner = calloc(1U, sizeof(*owner));
	if (!owner)
		return 0;
	*owner_out = owner;
	return 1;
}

sg_mechanism_capability_payload_t *SG_MechanismCapabilityOwnerPayload(
	const sg_mechanism_capability_owner_t *owner,
	const sg_mechanism_capability_set_t *capabilities)
{
	sg_mechanism_capability_record_t *record;

	if (!owner || !capabilities)
		return NULL;
	for (record = owner->live; record; record = record->next)
		if (record->token == capabilities)
			return record->payload;
	return NULL;
}

static int IssueAcceptedResult(sg_mechanism_capability_owner_t *owner,
	sg_mechanism_capability_payload_t *payload,
	sg_mechanism_capability_set_t **capabilities_out)
{
	sg_mechanism_capability_record_t *record;
	uintptr_t token;

	payload->content_identity = SG_MechanismCapabilityContentIdentity(payload);
	if (payload->content_identity == 0U || owner->live_count == UINT32_MAX)
		return 0;
	record = calloc(1U, sizeof(*record));
	if (!record)
		return 0;
	if (!SG_AuthorityTokenMint(&token))
	{
		free(record);
		return 0;
	}
	RefreshCapabilityView(payload);
	record->token = (sg_mechanism_capability_set_t *)(uintptr_t)token;
	record->payload = payload;
	record->next = owner->live;
	owner->live = record;
	owner->live_count++;
	*capabilities_out = record->token;
	return 1;
}

int SG_MechanismCapabilityOwnerAccepted(
	const sg_mechanism_capability_owner_t *owner,
	const sg_mechanism_capability_set_t *capabilities,
	const sg_mechanism_capability_view_t **view_out)
{
	sg_mechanism_capability_payload_t *payload;

	if (view_out)
		*view_out = NULL;
	payload = SG_MechanismCapabilityOwnerPayload(owner, capabilities);
	if (!payload || payload->content_identity == 0U ||
		payload->content_identity !=
			SG_MechanismCapabilityContentIdentity(payload))
		return 0;
	if (view_out)
		*view_out = &payload->view;
	return 1;
}

int SG_MechanismCapabilityRead(
	const sg_mechanism_capability_owner_t *owner,
	const sg_mechanism_capability_set_t *capabilities,
	const sg_mechanism_capability_view_t **view_out)
{
	return view_out &&
		SG_MechanismCapabilityOwnerAccepted(owner, capabilities, view_out);
}

static int Finite3(const sg_rune_vec3_t *value)
{
	return value && isfinite(value->value[0]) && isfinite(value->value[1]) &&
		isfinite(value->value[2]);
}

static int HullEqual(const sg_rune_hull_profile_t *left,
	const sg_rune_hull_profile_t *right)
{
	return memcmp(left, right, sizeof(*left)) == 0;
}

static int PhysicsEqual(const sg_rune_physics_parameters_t *left,
	const sg_rune_physics_parameters_t *right)
{
	return memcmp(left, right, sizeof(*left)) == 0;
}

static int IdentityEqual(const sg_rune_model_identity_t *left,
	const sg_rune_model_identity_t *right)
{
	return left && right && left->bsp_content_id == right->bsp_content_id &&
		left->entity_semantics_id == right->entity_semantics_id &&
		left->physics_abi_id == right->physics_abi_id &&
		left->source_set_identity == right->source_set_identity &&
		left->schema_id == right->schema_id &&
		left->producer_identity == right->producer_identity &&
		HullEqual(&left->standing_hull, &right->standing_hull) &&
		HullEqual(&left->crouching_hull, &right->crouching_hull) &&
		PhysicsEqual(&left->physics, &right->physics);
}

static int HullValid(const sg_rune_hull_profile_t *hull)
{
	uint32_t axis;

	if (!hull || !isfinite(hull->mins.value[0]) ||
		!isfinite(hull->mins.value[1]) || !isfinite(hull->mins.value[2]) ||
		!isfinite(hull->maxs.value[0]) || !isfinite(hull->maxs.value[1]) ||
		!isfinite(hull->maxs.value[2]))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (hull->mins.value[axis] >= hull->maxs.value[axis])
			return 0;
	return 1;
}

static int IdentityValid(const sg_rune_model_identity_t *identity)
{
	const sg_rune_physics_parameters_t *physics;

	if (!identity || identity->bsp_content_id == 0U ||
		identity->entity_semantics_id == 0U || identity->physics_abi_id == 0U ||
		identity->source_set_identity == 0U ||
		identity->source_set_identity == UINT64_MAX || identity->schema_id == 0U ||
		identity->producer_identity == 0U ||
		!HullValid(&identity->standing_hull) ||
		!HullValid(&identity->crouching_hull))
		return 0;
	physics = &identity->physics;
	return isfinite(physics->gravity) && physics->gravity >= 0.0f &&
		isfinite(physics->ground_acceleration) &&
		physics->ground_acceleration >= 0.0f &&
		isfinite(physics->air_acceleration) &&
		physics->air_acceleration >= 0.0f &&
		isfinite(physics->water_acceleration) &&
		physics->water_acceleration >= 0.0f &&
		isfinite(physics->hook_acceleration) &&
		physics->hook_acceleration >= 0.0f &&
		isfinite(physics->external_acceleration) &&
		physics->external_acceleration >= 0.0f &&
		isfinite(physics->water_drag) && physics->water_drag >= 0.0f &&
		isfinite(physics->max_velocity) && physics->max_velocity > 0.0f &&
		physics->gravity <= (float)SHRT_MAX &&
		truncf(physics->gravity) == physics->gravity &&
		physics->frame_ms != 0U && physics->substep_ms != 0U &&
		physics->substep_ms <= UCHAR_MAX &&
		physics->substep_ms <= physics->frame_ms &&
		physics->frame_ms % physics->substep_ms == 0U;
}

static int StableIdEqual(const sg_rune_stable_id_t *left,
	const sg_rune_stable_id_t *right)
{
	return left->source_set_identity == right->source_set_identity &&
		left->high == right->high && left->low == right->low;
}

static int PointInRegion(const sg_configuration_semantics_t *semantics,
	uint32_t region_index, const sg_rune_vec3_t *point)
{
	const sg_configuration_semantic_region_t *region =
		&semantics->regions[region_index];
	uint32_t face;

	if (!Finite3(point))
		return 0;
	for (face = region->first_face;
		face < region->first_face + region->face_count; face++)
	{
		const sg_configuration_semantic_face_t *plane =
			&semantics->faces[face];
		double dot = (double)point->value[0] * plane->normal[0] +
			(double)point->value[1] * plane->normal[1] +
			(double)point->value[2] * plane->normal[2];

		if (dot > (double)plane->distance)
			return 0;
	}
	return 1;
}

static int EdgeRefCompare(const void *left_value, const void *right_value)
{
	const sg_mechanism_edge_ref_t *left = left_value;
	const sg_mechanism_edge_ref_t *right = right_value;

#define COMPARE_FIELD(field) \
	do { if (left->field != right->field) \
		return left->field < right->field ? -1 : 1; } while (0)
	COMPARE_FIELD(source);
	COMPARE_FIELD(kind);
	COMPARE_FIELD(fanout);
	COMPARE_FIELD(destination);
	COMPARE_FIELD(edge);
#undef COMPARE_FIELD
	return 0;
}

static int TraceRefCompare(const void *left_value, const void *right_value)
{
	const sg_mechanism_trace_ref_t *left = left_value;
	const sg_mechanism_trace_ref_t *right = right_value;

#define COMPARE_FIELD(field) \
	do { if (left->field != right->field) \
		return left->field < right->field ? -1 : 1; } while (0)
	COMPARE_FIELD(mechanism);
	COMPARE_FIELD(kind);
	COMPARE_FIELD(controller);
	COMPARE_FIELD(source_region);
	COMPARE_FIELD(destination_region);
	COMPARE_FIELD(source_phase);
	COMPARE_FIELD(destination_phase);
	COMPARE_FIELD(source_state);
	COMPARE_FIELD(destination_state);
	COMPARE_FIELD(identity);
	COMPARE_FIELD(trace);
#undef COMPARE_FIELD
	return 0;
}

static int TraceIndexCompare(const void *left_value, const void *right_value)
{
	const sg_mechanism_trace_index_t *left = left_value;
	const sg_mechanism_trace_index_t *right = right_value;

	if (left->identity != right->identity)
		return left->identity < right->identity ? -1 : 1;
	if (left->fact != right->fact)
		return left->fact < right->fact ? -1 : 1;
	return 0;
}

static int CandidateIndexCompare(const void *left_value,
	const void *right_value)
{
	const sg_mechanism_candidate_index_t *left = left_value;
	const sg_mechanism_candidate_index_t *right = right_value;

	if (left->identity != right->identity)
		return left->identity < right->identity ? -1 : 1;
	if (left->candidate != right->candidate)
		return left->candidate < right->candidate ? -1 : 1;
	return 0;
}

static int RelationRefCompare(const void *left_value,
	const void *right_value)
{
	const sg_mechanism_relation_ref_t *left = left_value;
	const sg_mechanism_relation_ref_t *right = right_value;

	if (left->controller != right->controller)
		return left->controller < right->controller ? -1 : 1;
	if (left->mechanism != right->mechanism)
		return left->mechanism < right->mechanism ? -1 : 1;
	return 0;
}

static int EdgeTraversable(sg_mech_edge_kind_t kind)
{
	return kind == SG_MECH_EDGE_TARGET || kind == SG_MECH_EDGE_OWNER ||
		kind == SG_MECH_EDGE_TEAM || kind == SG_MECH_EDGE_PATH_TARGET ||
		kind == SG_MECH_EDGE_MOVE_TARGET ||
		kind == SG_MECH_EDGE_TARGET_ENT;
}

static int BuildTopologyIndex(sg_mechanism_build_t *build)
{
	const sg_bsp_entity_semantics_t *semantics =
		build->source->entity_semantics;
	uint32_t entity_count = semantics->entity_count;
	uint32_t edge_count = semantics->edge_count;
	uint32_t edge;
	uint32_t entity = 0U;

	if (!AllocationFits(edge_count, sizeof(*build->edge_refs)) ||
		!AllocationFits((size_t)entity_count + 1U,
			sizeof(*build->edge_offsets)))
	{
		SetError(build, SG_MECHANISM_CAPABILITY_ERROR_OVERFLOW, edge_count);
		return 0;
	}
	build->edge_refs = edge_count ? calloc(edge_count,
		sizeof(*build->edge_refs)) : NULL;
	build->edge_offsets = calloc((size_t)entity_count + 1U,
		sizeof(*build->edge_offsets));
	if ((edge_count && !build->edge_refs) || !build->edge_offsets)
	{
		SetError(build, SG_MECHANISM_CAPABILITY_ERROR_OUT_OF_MEMORY, 0U);
		return 0;
	}
	for (edge = 0U; edge < edge_count; edge++)
	{
		const sg_bsp_entity_semantic_edge_t *record = &semantics->edges[edge];

		if (record->source >= entity_count ||
			record->destination >= entity_count ||
			record->source == record->destination ||
			record->kind < SG_MECH_EDGE_TARGET ||
			record->kind > SG_MECH_EDGE_ROUTE_TARGET)
		{
			SetError(build, SG_MECHANISM_CAPABILITY_ERROR_INVALID_TOPOLOGY,
				edge);
			return 0;
		}
		build->edge_refs[edge].edge = edge;
		build->edge_refs[edge].source = record->source;
		build->edge_refs[edge].destination = record->destination;
		build->edge_refs[edge].kind = (uint32_t)record->kind;
		build->edge_refs[edge].fanout = record->fanout_ordinal;
	}
	if (edge_count)
		qsort(build->edge_refs, edge_count, sizeof(*build->edge_refs),
			EdgeRefCompare);
	for (edge = 0U; edge < edge_count; edge++)
	{
		const sg_mechanism_edge_ref_t *record = &build->edge_refs[edge];

		while (entity <= record->source)
			build->edge_offsets[entity++] = edge;
		if (edge != 0U)
		{
			const sg_mechanism_edge_ref_t *previous =
				&build->edge_refs[edge - 1U];

			if (previous->source == record->source &&
				previous->kind == record->kind &&
				previous->fanout == record->fanout)
			{
				SetError(build,
					SG_MECHANISM_CAPABILITY_ERROR_AMBIGUOUS_TOPOLOGY,
					record->edge);
				return 0;
			}
		}
	}
	while (entity <= entity_count)
		build->edge_offsets[entity++] = edge_count;
	return 1;
}

static int MechanismId(const sg_mechanism_capability_source_t *source,
	uint32_t entity_index, sg_rune_mechanism_id_t *id_out)
{
	const sg_bsp_entity_semantic_t *entity =
		&source->entity_semantics->entities[entity_index];
	sg_rune_canonical_order_input_t input;
	sg_rune_order_key_t order;

	memset(&input, 0, sizeof(input));
	input.domain = SG_RUNE_ORDER_MECHANISM;
	input.source_index = entity->source_entity_ordinal;
	input.canonical_ordinal = entity->canonical_ordinal;
	input.variant = entity->flags & SG_BSP_ENTITY_CANONICAL_MECHANISM_KIND
		? (uint32_t)entity->mechanism_kind
		: (uint32_t)entity->mechanism_role;
	input.source_set_identity = source->entity_semantics->source_set_identity;
	input.source_set_count = source->entity_semantics->entity_count;
	input.source_set_complete = 1U;
	if (SG_RuneModelOrderKeyDerive(&input, &order) !=
		SG_RUNE_ORDER_DERIVATION_OK)
		return 0;
	id_out->value = SG_RuneModelStableIdFromOrderKey(&order);
	return SG_RuneModelStableIdValid(&id_out->value);
}

static int StaticAuthorityReplays(
	const sg_mechanism_capability_source_t *source)
{
	sg_host_collision_authority_t replay;
	sg_host_collision_error_t host_error;
	sg_bsp_completeness_result_t completeness;
	sg_configuration_semantics_audit_result_t semantics_audit;

	if (!SG_HostCollisionInit(&replay, source->authority->world,
		&source->authority->identity, &host_error) ||
		!SG_BspCompletenessProve(&replay, source->configuration,
			&completeness) || completeness.code != SG_BSP_COMPLETENESS_OK ||
		completeness.omitted_cells != 0U ||
		completeness.invented_cells != 0U ||
		completeness.omitted_portals != 0U ||
		completeness.invented_portals != 0U ||
		completeness.expected_cells != source->configuration->cell_count ||
		completeness.represented_cells != source->configuration->cell_count ||
		completeness.proved_cells != source->configuration->cell_count ||
		completeness.expected_portals != source->configuration->portal_count ||
		completeness.represented_portals !=
			source->configuration->portal_count ||
		completeness.proved_portals != source->configuration->portal_count ||
		!SG_ConfigurationSemanticsAudit(&replay, source->configuration,
			source->configuration_semantics, &semantics_audit) ||
		semantics_audit.code != SG_CONFIGURATION_SEMANTICS_AUDIT_OK)
		return 0;
	return 1;
}

static int SourceShapeValid(sg_mechanism_build_t *build)
{
	const sg_mechanism_capability_source_t *source = build->source;
	const sg_rune_model_identity_t *identity;
	uint32_t entity_count;
	uint32_t edge_count;
	uint32_t trace_count;
	uint32_t index;

	if (!source || !source->authority || !source->authority->world ||
		!source->configuration || !source->configuration_semantics ||
		!source->entity_semantics ||
		!source->phases || source->phase_count == 0U ||
		!source->host_traces || !source->host_traces->candidates ||
		!source->host_traces->traces ||
		source->host_traces->candidate_count == 0U ||
		source->host_traces->trace_count == 0U ||
		source->host_traces->candidate_count !=
			source->host_traces->trace_count ||
		source->host_traces->candidate_verifier_identity == 0U ||
		source->host_traces->trace_verifier_identity == 0U ||
		source->host_traces->candidate_verifier_identity ==
			source->host_traces->trace_verifier_identity)
	{
		SetError(build, SG_MECHANISM_CAPABILITY_ERROR_INVALID_ARGUMENT, 0U);
		return 0;
	}
	identity = &source->authority->identity;
	if (!IdentityValid(identity) ||
		!IdentityValid(&source->configuration->identity) ||
		!IdentityValid(&source->configuration_semantics->identity) ||
		!IdentityValid(&source->host_traces->identity) ||
		!IdentityEqual(identity, &source->configuration->identity) ||
		!IdentityEqual(identity, &source->configuration_semantics->identity) ||
		!IdentityEqual(identity, &source->host_traces->identity) ||
		identity->source_set_identity !=
			source->entity_semantics->source_set_identity)
	{
		SetError(build, SG_MECHANISM_CAPABILITY_ERROR_IDENTITY_MISMATCH, 0U);
		return 0;
	}
	entity_count = source->entity_semantics->entity_count;
	edge_count = source->entity_semantics->edge_count;
	trace_count = source->host_traces->trace_count;
	if (entity_count == UINT32_MAX ||
		!AllocationFits(edge_count, sizeof(*build->edge_refs)) ||
		!AllocationFits((size_t)entity_count + 1U,
			sizeof(*build->edge_offsets)) ||
		!AllocationFits(entity_count, 2U * sizeof(*build->queue)) ||
		!AllocationFits(entity_count, sizeof(*build->distance)) ||
		!AllocationFits(entity_count, sizeof(*build->ways)) ||
		!AllocationFits(entity_count, sizeof(*build->propagated)) ||
		!AllocationFits(entity_count, sizeof(*build->parent)) ||
		!AllocationFits(entity_count, sizeof(*build->parent_edge)) ||
		!AllocationFits(entity_count, sizeof(*build->path)) ||
		!AllocationFits(trace_count, sizeof(sg_mechanism_trace_ref_t)) ||
		!AllocationFits(trace_count, sizeof(sg_mechanism_trace_index_t)) ||
		!AllocationFits(trace_count, sizeof(sg_mechanism_candidate_index_t)) ||
		!AllocationFits(trace_count, sizeof(sg_mechanism_relation_ref_t)) ||
		!AllocationFits(trace_count, sizeof(*build->output->facts)) ||
		!AllocationFits(trace_count,
			sizeof(*build->output->topology_relations)) ||
		!AllocationFits((size_t)entity_count + 1U,
			sizeof(*build->output->mechanism_offsets)))
	{
		SetError(build, SG_MECHANISM_CAPABILITY_ERROR_OVERFLOW, trace_count);
		return 0;
	}
	if (!StaticAuthorityReplays(source))
	{
		SetError(build,
			SG_MECHANISM_CAPABILITY_ERROR_INCOMPLETE_CONFIGURATION, 0U);
		return 0;
	}
	if (source->entity_semantics->entity_count == 0U ||
		!source->entity_semantics->entities ||
		source->configuration->cell_count == 0U ||
		!source->configuration->cells ||
		(source->entity_semantics->edge_count != 0U &&
		 !source->entity_semantics->edges) ||
		source->configuration_semantics->region_count == 0U ||
		!source->configuration_semantics->regions ||
		!source->configuration_semantics->faces)
	{
		SetError(build, SG_MECHANISM_CAPABILITY_ERROR_INVALID_SOURCE, 0U);
		return 0;
	}
	for (index = 0U;
		index < source->configuration_semantics->region_count; index++)
	{
		const sg_configuration_semantic_region_t *region =
			&source->configuration_semantics->regions[index];

		if (region->cell >= source->configuration->cell_count ||
			region->face_count < 4U ||
			region->first_face >
				source->configuration_semantics->face_count ||
			region->face_count >
				source->configuration_semantics->face_count -
				region->first_face)
		{
			SetError(build, SG_MECHANISM_CAPABILITY_ERROR_INVALID_SOURCE,
				index);
			return 0;
		}
	}
	for (index = 0U;
		index < source->configuration_semantics->face_count; index++)
	{
		const sg_configuration_semantic_face_t *face =
			&source->configuration_semantics->faces[index];

		if (!isfinite(face->normal[0]) || !isfinite(face->normal[1]) ||
			!isfinite(face->normal[2]) || !isfinite(face->distance) ||
			(face->normal[0] == 0.0f && face->normal[1] == 0.0f &&
			 face->normal[2] == 0.0f))
		{
			SetError(build, SG_MECHANISM_CAPABILITY_ERROR_INVALID_SOURCE,
				index);
			return 0;
		}
	}
	for (index = 0U; index < source->entity_semantics->entity_count; index++)
	{
		const sg_bsp_entity_semantic_t *entity =
			&source->entity_semantics->entities[index];

		if (entity->source_set_identity != identity->source_set_identity ||
			entity->canonical_ordinal != index)
		{
			SetError(build, SG_MECHANISM_CAPABILITY_ERROR_INVALID_SOURCE,
				index);
			return 0;
		}
	}
	for (index = 0U; index < source->phase_count; index++)
		if (!SG_RuneModelPhaseValid(&source->phases[index]) ||
			source->phases[index].order.source_set_identity !=
				identity->source_set_identity ||
			(index != 0U && SG_RuneModelOrderKeyCompare(
				&source->phases[index - 1U].order,
				&source->phases[index].order) >= 0))
		{
			SetError(build, SG_MECHANISM_CAPABILITY_ERROR_INVALID_PHASE, index);
			return 0;
		}
	return 1;
}

static int KindMatches(const sg_bsp_entity_semantic_t *entity,
	sg_mechanism_capability_kind_t kind)
{
	if (kind == SG_MECHANISM_CAPABILITY_AREA_PORTAL_STATE)
		return entity->mechanism_role == SG_MECH_NODE_AREAPORTAL;
	if (!(entity->flags & SG_BSP_ENTITY_CANONICAL_MECHANISM_KIND))
		return 0;
	switch (kind)
	{
	case SG_MECHANISM_CAPABILITY_DOOR_CROSSING:
		return entity->mechanism_kind == SG_RUNE_MECHANISM_DOOR;
	case SG_MECHANISM_CAPABILITY_BUTTON_ACTIVATION:
		return entity->mechanism_kind == SG_RUNE_MECHANISM_BUTTON;
	case SG_MECHANISM_CAPABILITY_TRIGGER_ACTIVATION:
		return entity->mechanism_kind == SG_RUNE_MECHANISM_TRIGGER;
	case SG_MECHANISM_CAPABILITY_DWELL:
		return entity->mechanism_kind == SG_RUNE_MECHANISM_DOOR ||
			entity->mechanism_kind == SG_RUNE_MECHANISM_BUTTON ||
			entity->mechanism_kind == SG_RUNE_MECHANISM_TRIGGER ||
			entity->mechanism_kind == SG_RUNE_MECHANISM_LIFT ||
			entity->mechanism_kind == SG_RUNE_MECHANISM_TRAIN;
	case SG_MECHANISM_CAPABILITY_LIFT_RIDE:
		return entity->mechanism_kind == SG_RUNE_MECHANISM_LIFT;
	case SG_MECHANISM_CAPABILITY_TRAIN_RIDE:
		return entity->mechanism_kind == SG_RUNE_MECHANISM_TRAIN;
	case SG_MECHANISM_CAPABILITY_ROTATOR_CROSSING:
		return entity->mechanism_kind == SG_RUNE_MECHANISM_ROTATOR;
	case SG_MECHANISM_CAPABILITY_PUSH:
		return entity->mechanism_kind == SG_RUNE_MECHANISM_PUSH;
	case SG_MECHANISM_CAPABILITY_TELEPORT:
		return entity->mechanism_kind == SG_RUNE_MECHANISM_TELEPORT;
	case SG_MECHANISM_CAPABILITY_RESET:
		return 1;
	default:
		return 0;
	}
}

static int ActivationMatches(const sg_bsp_entity_semantic_t *controller,
	sg_mechanism_activation_t activation)
{
	switch (activation)
	{
	case SG_MECHANISM_ACTIVATION_AUTOMATIC:
		return (controller->flags & SG_BSP_ENTITY_AUTO_ACTIVATED) != 0U;
	case SG_MECHANISM_ACTIVATION_TOUCH:
		return (controller->flags & SG_BSP_ENTITY_TOUCH_ACTIVATED) != 0U;
	case SG_MECHANISM_ACTIVATION_USE:
		return (controller->flags & SG_BSP_ENTITY_USE_ACTIVATED) != 0U;
	case SG_MECHANISM_ACTIVATION_DAMAGE:
		return (controller->flags & SG_BSP_ENTITY_DAMAGE_ACTIVATED) != 0U;
	case SG_MECHANISM_ACTIVATION_INVENTORY:
		return (controller->flags & SG_BSP_ENTITY_INVENTORY_GATED) != 0U;
	default:
		return 0;
	}
}

static int KindTraverses(sg_mechanism_capability_kind_t kind)
{
	return kind == SG_MECHANISM_CAPABILITY_DOOR_CROSSING ||
		kind == SG_MECHANISM_CAPABILITY_LIFT_RIDE ||
		kind == SG_MECHANISM_CAPABILITY_TRAIN_RIDE ||
		kind == SG_MECHANISM_CAPABILITY_ROTATOR_CROSSING ||
		kind == SG_MECHANISM_CAPABILITY_PUSH ||
		kind == SG_MECHANISM_CAPABILITY_TELEPORT;
}

static int KindConditional(sg_mechanism_capability_kind_t kind)
{
	return kind == SG_MECHANISM_CAPABILITY_DOOR_CROSSING ||
		kind == SG_MECHANISM_CAPABILITY_ROTATOR_CROSSING ||
		kind == SG_MECHANISM_CAPABILITY_AREA_PORTAL_STATE;
}

static int KindCollisionConditional(sg_mechanism_capability_kind_t kind)
{
	return kind == SG_MECHANISM_CAPABILITY_DOOR_CROSSING ||
		kind == SG_MECHANISM_CAPABILITY_ROTATOR_CROSSING;
}

typedef struct sg_mechanism_derived_timing_s
{
	uint32_t delay_ms;
	uint32_t dwell_ms;
	uint32_t travel_ms;
	uint32_t wait_ms;
	uint32_t reset_ms;
} sg_mechanism_derived_timing_t;

static int FillParameters(const sg_mechanism_capability_source_t *source,
	const sg_mechanism_host_trace_t *trace,
	const sg_mechanism_derived_timing_t *timing,
	sg_mechanism_kernel_parameters_t *parameters);

static int EntityMilliseconds(float value, uint32_t *milliseconds)
{
	uint32_t converted;

	if (!milliseconds || !isfinite(value) || value < 0.0f ||
		(double)value > (double)UINT32_MAX || truncf(value) != value)
		return 0;
	converted = (uint32_t)value;
	if ((float)converted != value)
		return 0;
	*milliseconds = converted;
	return 1;
}

static int DeriveTiming(const sg_mechanism_host_trace_t *trace,
	const sg_bsp_entity_semantic_t *controller,
	const sg_bsp_entity_semantic_t *mechanism,
	sg_mechanism_derived_timing_t *timing)
{
	uint64_t activation_delta;
	uint64_t active_delta;
	uint64_t reset_delta;

	if (!timing || trace->active_time_ms < trace->activation_time_ms ||
		trace->exit_time_ms < trace->active_time_ms ||
		trace->reset_time_ms < trace->exit_time_ms ||
		trace->active_time_ms - trace->activation_time_ms > UINT32_MAX ||
		trace->exit_time_ms - trace->active_time_ms > UINT32_MAX ||
		trace->reset_time_ms - trace->exit_time_ms > UINT32_MAX)
		return 0;
	activation_delta = trace->active_time_ms - trace->activation_time_ms;
	active_delta = trace->exit_time_ms - trace->active_time_ms;
	reset_delta = trace->reset_time_ms - trace->exit_time_ms;
	timing->delay_ms = (uint32_t)activation_delta;
	if ((controller->flags & SG_BSP_ENTITY_DELAY_DEFINED) != 0U)
	{
		uint32_t entity_delay;

		if (!EntityMilliseconds(controller->delay_ms, &entity_delay) ||
			entity_delay != timing->delay_ms)
			return 0;
	}
	timing->dwell_ms = 0U;
	if ((controller->flags & SG_BSP_ENTITY_DWELL_DEFINED) != 0U)
	{
		if (controller->dwell_ms != -1000.0f &&
			!EntityMilliseconds(controller->dwell_ms, &timing->dwell_ms))
			return 0;
	}
	else if (trace->kind == SG_MECHANISM_CAPABILITY_DWELL &&
		!EntityMilliseconds(mechanism->dwell_ms, &timing->dwell_ms))
		return 0;
	timing->wait_ms = 0U;
	if (trace->kind == SG_MECHANISM_CAPABILITY_RESET &&
		!EntityMilliseconds(mechanism->dwell_ms, &timing->wait_ms))
		return 0;
	if ((uint64_t)timing->dwell_ms > active_delta ||
		(uint64_t)timing->wait_ms > reset_delta)
		return 0;
	timing->travel_ms = (uint32_t)(active_delta - timing->dwell_ms);
	timing->reset_ms = (uint32_t)(reset_delta - timing->wait_ms);
	if ((trace->kind == SG_MECHANISM_CAPABILITY_DOOR_CROSSING ||
		 trace->kind == SG_MECHANISM_CAPABILITY_LIFT_RIDE ||
		 trace->kind == SG_MECHANISM_CAPABILITY_TRAIN_RIDE ||
		 trace->kind == SG_MECHANISM_CAPABILITY_ROTATOR_CROSSING) &&
		timing->travel_ms == 0U)
		return 0;
	return 1;
}

static const sg_host_collision_instance_t *FindSceneInstance(
	const sg_host_collision_scene_t *scene, uint64_t identity,
	uint32_t model_index)
{
	size_t index;

	if (!scene || !identity)
		return NULL;
	for (index = 0U; index < scene->instance_count; index++)
		if (scene->instances[index].instance_id == identity &&
			scene->instances[index].model_index == model_index)
			return &scene->instances[index];
	return NULL;
}

static int EndpointStancesValid(
	const sg_mechanism_capability_source_t *source,
	const sg_mechanism_host_trace_t *trace)
{
	const sg_configuration_semantic_region_t *source_region =
		&source->configuration_semantics->regions[trace->source_region];
	const sg_configuration_semantic_region_t *destination_region =
		&source->configuration_semantics->regions[trace->destination_region];

	return source_region->cell < source->configuration->cell_count &&
		destination_region->cell < source->configuration->cell_count &&
		source->phases[trace->source_phase].stance ==
			source->configuration->cells[source_region->cell].stance &&
		source->phases[trace->destination_phase].stance ==
			source->configuration->cells[destination_region->cell].stance;
}

static int VectorDifferenceEqual(const float destination[3],
	const float source[3], const sg_rune_vec3_t *difference)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		if (destination[axis] - source[axis] != difference->value[axis])
			return 0;
	return 1;
}

static int DeriveState(sg_mechanism_capability_kind_t kind,
	sg_mechanism_state_t *source, sg_mechanism_state_t *destination,
	sg_mechanism_recovery_t *recovery)
{
	*recovery = SG_MECHANISM_RECOVERY_NONE;
	switch (kind)
	{
	case SG_MECHANISM_CAPABILITY_BUTTON_ACTIVATION:
	case SG_MECHANISM_CAPABILITY_TRIGGER_ACTIVATION:
		*source = SG_MECHANISM_STATE_INACTIVE;
		*destination = SG_MECHANISM_STATE_ACTIVATING;
		return 1;
	case SG_MECHANISM_CAPABILITY_DWELL:
		*source = SG_MECHANISM_STATE_ACTIVE;
		*destination = SG_MECHANISM_STATE_DWELLING;
		*recovery = SG_MECHANISM_RECOVERY_WAIT_FOR_CYCLE;
		return 1;
	case SG_MECHANISM_CAPABILITY_RESET:
		*source = SG_MECHANISM_STATE_RETURNING;
		*destination = SG_MECHANISM_STATE_RESET;
		*recovery = SG_MECHANISM_RECOVERY_WAIT_FOR_RESET;
		return 1;
	case SG_MECHANISM_CAPABILITY_DOOR_CROSSING:
	case SG_MECHANISM_CAPABILITY_ROTATOR_CROSSING:
	case SG_MECHANISM_CAPABILITY_AREA_PORTAL_STATE:
		*source = SG_MECHANISM_STATE_INACTIVE;
		*destination = SG_MECHANISM_STATE_ACTIVE;
		return 1;
	case SG_MECHANISM_CAPABILITY_LIFT_RIDE:
	case SG_MECHANISM_CAPABILITY_TRAIN_RIDE:
	case SG_MECHANISM_CAPABILITY_PUSH:
	case SG_MECHANISM_CAPABILITY_TELEPORT:
		*source = SG_MECHANISM_STATE_ACTIVE;
		*destination = SG_MECHANISM_STATE_ACTIVE;
		return 1;
	case SG_MECHANISM_CAPABILITY_KIND_COUNT:
		break;
	}
	return 0;
}

static int StateLawValid(const sg_mechanism_host_trace_t *trace,
	const sg_mechanism_derived_timing_t *timing)
{
	sg_mechanism_state_t source;
	sg_mechanism_state_t destination;
	sg_mechanism_recovery_t recovery;

	if ((trace->flags & SG_MECHANISM_HOST_TRACE_ONE_SHOT) != 0U &&
		(trace->recovery != SG_MECHANISM_RECOVERY_NONE ||
		 timing->wait_ms != 0U || timing->reset_ms != 0U))
		return 0;
	if ((timing->wait_ms > 0U || timing->reset_ms > 0U) &&
		trace->recovery == SG_MECHANISM_RECOVERY_NONE)
		return 0;
	return DeriveState(trace->kind, &source, &destination, &recovery) &&
		trace->source_state == source &&
		trace->destination_state == destination &&
		trace->recovery == recovery &&
		((trace->kind != SG_MECHANISM_CAPABILITY_BUTTON_ACTIVATION &&
		  trace->kind != SG_MECHANISM_CAPABILITY_TRIGGER_ACTIVATION) ||
		 trace->controller_entity == trace->mechanism_entity) &&
		(trace->kind != SG_MECHANISM_CAPABILITY_DWELL ||
		 timing->dwell_ms > 0U) &&
		(trace->kind != SG_MECHANISM_CAPABILITY_RESET ||
		 timing->reset_ms > 0U);
}

static int ExecutionTransitionValid(
	const sg_mechanism_host_trace_t *trace)
{
	const sg_mech_execution_state_t *source = &trace->source_execution;
	const sg_mech_execution_state_t *destination =
		&trace->destination_execution;

	if (!SG_MechExecutionStateValid(source) ||
		!SG_MechExecutionStateValid(destination) ||
		source->controller_kind != destination->controller_kind ||
		source->node_kind != destination->node_kind ||
		source->touch_matches != destination->touch_matches ||
		source->touch_cleared != destination->touch_cleared)
		return 0;
	switch (trace->kind)
	{
	case SG_MECHANISM_CAPABILITY_BUTTON_ACTIVATION:
		return source->node_kind == SG_MECH_NODE_BUTTON &&
			source->controller_kind ==
				SG_MECHANISM_CONTROLLER_BUTTON_DOOR &&
			source->think_role == SG_MECH_EXEC_THINK_SEALED &&
			source->motion_state == SG_MECH_MOTION_AT_ORIGIN &&
			destination->think_role == SG_MECH_EXEC_THINK_LINEAR_DONE &&
			destination->end_role ==
				SG_MECH_EXEC_END_BUTTON_DESTINATION &&
			destination->motion_state ==
				SG_MECH_MOTION_AT_DESTINATION;
	case SG_MECHANISM_CAPABILITY_TRIGGER_ACTIVATION:
		return source->node_kind == SG_MECH_NODE_TRIGGER &&
			source->controller_kind ==
				SG_MECHANISM_CONTROLLER_DIRECT_TRIGGER_DOOR &&
			source->think_role == SG_MECH_EXEC_THINK_SEALED &&
			destination->think_role ==
				SG_MECH_EXEC_THINK_MULTI_WAIT &&
			destination->nextthink_pending;
	case SG_MECHANISM_CAPABILITY_DOOR_CROSSING:
		return source->node_kind == SG_MECH_NODE_DOOR_MASTER &&
			source->think_role == SG_MECH_EXEC_THINK_SEALED &&
			source->motion_state == SG_MECH_MOTION_AT_ORIGIN &&
			destination->think_role == SG_MECH_EXEC_THINK_LINEAR_DONE &&
			destination->end_role ==
				SG_MECH_EXEC_END_DOOR_DESTINATION &&
			destination->motion_state ==
				SG_MECH_MOTION_AT_DESTINATION;
	case SG_MECHANISM_CAPABILITY_DWELL:
		return source->node_kind == SG_MECH_NODE_DOOR_MASTER &&
			source->motion_state == SG_MECH_MOTION_AT_DESTINATION &&
			destination->think_role == SG_MECH_EXEC_THINK_DOOR_RETURN &&
			destination->nextthink_pending && destination->stopped;
	case SG_MECHANISM_CAPABILITY_RESET:
		return source->node_kind == SG_MECH_NODE_DOOR_MASTER &&
			source->motion_state == SG_MECH_MOTION_TO_ORIGIN &&
			source->end_role == SG_MECH_EXEC_END_DOOR_ORIGIN &&
			destination->motion_state == SG_MECH_MOTION_AT_ORIGIN &&
			destination->end_role == SG_MECH_EXEC_END_DOOR_ORIGIN;
	case SG_MECHANISM_CAPABILITY_LIFT_RIDE:
		return source->controller_kind == SG_MECHANISM_CONTROLLER_PLATFORM &&
			source->node_kind == SG_MECH_NODE_PLATFORM &&
			source->motion_state == SG_MECH_MOTION_AT_ORIGIN &&
			destination->motion_state ==
				SG_MECH_MOTION_AT_DESTINATION;
	case SG_MECHANISM_CAPABILITY_TRAIN_RIDE:
		return source->controller_kind == SG_MECHANISM_CONTROLLER_TRAIN &&
			source->node_kind == SG_MECH_NODE_TRAIN;
	case SG_MECHANISM_CAPABILITY_ROTATOR_CROSSING:
		return source->node_kind == SG_MECH_NODE_OTHER_MOVER;
	case SG_MECHANISM_CAPABILITY_PUSH:
		return source->controller_kind == SG_MECHANISM_CONTROLLER_PUSH &&
			source->node_kind == SG_MECH_NODE_PUSH;
	case SG_MECHANISM_CAPABILITY_TELEPORT:
		return source->controller_kind == SG_MECHANISM_CONTROLLER_TELEPORT &&
			source->node_kind == SG_MECH_NODE_TELEPORTER;
	case SG_MECHANISM_CAPABILITY_AREA_PORTAL_STATE:
		return source->node_kind == SG_MECH_NODE_AREAPORTAL;
	case SG_MECHANISM_CAPABILITY_KIND_COUNT:
		break;
	}
	return 0;
}

static int ReplayHostTrace(const sg_mechanism_capability_source_t *source,
	const sg_mechanism_host_trace_t *trace,
	sg_host_collision_transition_t *inactive_out,
	sg_host_collision_transition_t *active_out,
	sg_host_collision_transform_t *inactive_transform_out,
	sg_host_collision_transform_t *active_transform_out)
{
	const sg_bsp_entity_semantic_t *mechanism =
		&source->entity_semantics->entities[trace->mechanism_entity];
	const sg_host_collision_instance_t *inactive_instance = NULL;
	const sg_host_collision_instance_t *active_instance = NULL;
	sg_rune_stance_t stance = source->phases[trace->source_phase].stance;
	int has_model = (mechanism->flags & SG_BSP_ENTITY_HAS_BRUSH_MODEL) != 0U;

	memset(inactive_transform_out, 0, sizeof(*inactive_transform_out));
	memset(active_transform_out, 0, sizeof(*active_transform_out));
	if (source->phases[trace->destination_phase].stance != stance ||
		!SG_HostCollisionTransition(source->authority, &trace->inactive_scene,
			trace->entry_witness.value, trace->exit_witness.value, stance,
			inactive_out) ||
		!SG_HostCollisionTransition(source->authority, &trace->active_scene,
			trace->entry_witness.value, trace->exit_witness.value, stance,
			active_out) || !inactive_out->source_valid ||
		!inactive_out->destination_valid || !active_out->source_valid ||
		!active_out->destination_valid)
		return 0;
	if (has_model)
	{
		if (mechanism->bsp_model == SG_BSP_ENTITY_MODEL_NONE ||
			mechanism->bsp_model == 0U || trace->mechanism_instance_id == 0U ||
			!(inactive_instance = FindSceneInstance(&trace->inactive_scene,
				trace->mechanism_instance_id, mechanism->bsp_model)) ||
			!(active_instance = FindSceneInstance(&trace->active_scene,
				trace->mechanism_instance_id, mechanism->bsp_model)))
			return 0;
		*inactive_transform_out = inactive_instance->transform;
		*active_transform_out = active_instance->transform;
	}
	else if (trace->mechanism_instance_id != 0U)
		return 0;
	if (KindCollisionConditional(trace->kind) &&
		(inactive_out->clear || !active_out->clear || !has_model ||
		 inactive_out->sweep.model_index != mechanism->bsp_model ||
		 inactive_out->sweep.instance_id != trace->mechanism_instance_id))
		return 0;
	if (KindTraverses(trace->kind) &&
		trace->kind != SG_MECHANISM_CAPABILITY_TELEPORT && !active_out->clear)
		return 0;
	if (!KindTraverses(trace->kind) && !active_out->clear)
		return 0;
	if ((trace->kind == SG_MECHANISM_CAPABILITY_LIFT_RIDE ||
		 trace->kind == SG_MECHANISM_CAPABILITY_TRAIN_RIDE) &&
		(!has_model || !VectorDifferenceEqual(active_instance->transform.origin,
			inactive_instance->transform.origin,
			&trace->observed_displacement)))
		return 0;
	if (trace->kind == SG_MECHANISM_CAPABILITY_ROTATOR_CROSSING &&
		(!has_model || !VectorDifferenceEqual(active_instance->transform.angles,
			inactive_instance->transform.angles, &mechanism->move_angles)))
		return 0;
	return 1;
}

static int CandidateValid(const sg_mechanism_capability_source_t *source,
	const sg_mechanism_capability_candidate_t *candidate)
{
	return candidate->candidate_identity != 0U &&
		candidate->source_set_identity ==
			source->authority->identity.source_set_identity &&
		candidate->controller_entity < source->entity_semantics->entity_count &&
		candidate->mechanism_entity < source->entity_semantics->entity_count &&
		candidate->source_region <
			source->configuration_semantics->region_count &&
		candidate->destination_region <
			source->configuration_semantics->region_count &&
		candidate->source_phase < source->phase_count &&
		candidate->destination_phase < source->phase_count &&
		candidate->kind >= 0 &&
		candidate->kind < SG_MECHANISM_CAPABILITY_KIND_COUNT &&
		candidate->source_state >= 0 &&
		candidate->source_state < SG_MECHANISM_STATE_COUNT &&
		candidate->destination_state >= 0 &&
		candidate->destination_state < SG_MECHANISM_STATE_COUNT &&
		candidate->activation >= 0 &&
		candidate->activation < SG_MECHANISM_ACTIVATION_COUNT &&
		candidate->recovery >= 0 &&
		candidate->recovery < SG_MECHANISM_RECOVERY_COUNT;
}

static int CandidateMatchesTrace(
	const sg_mechanism_capability_candidate_t *candidate,
	const sg_mechanism_host_trace_t *trace)
{
	return candidate->candidate_identity == trace->candidate_identity &&
		candidate->source_set_identity == trace->source_set_identity &&
		candidate->controller_entity == trace->controller_entity &&
		candidate->mechanism_entity == trace->mechanism_entity &&
		candidate->source_region == trace->source_region &&
		candidate->destination_region == trace->destination_region &&
		candidate->source_phase == trace->source_phase &&
		candidate->destination_phase == trace->destination_phase &&
		candidate->kind == trace->kind &&
		candidate->source_state == trace->source_state &&
		candidate->destination_state == trace->destination_state &&
		candidate->activation == trace->activation &&
		candidate->recovery == trace->recovery;
}

static int TraceValid(sg_mechanism_build_t *build, uint32_t trace_index,
	const sg_mechanism_capability_candidate_t *candidate)
{
	const sg_mechanism_capability_source_t *source = build->source;
	const sg_mechanism_host_trace_t *trace =
		&source->host_traces->traces[trace_index];
	const sg_bsp_entity_semantic_t *controller;
	const sg_bsp_entity_semantic_t *mechanism;
	sg_rune_mechanism_id_t mechanism_id;
	sg_host_collision_transition_t inactive_transition;
	sg_host_collision_transition_t active_transition;
	sg_host_collision_transform_t inactive_transform;
	sg_host_collision_transform_t active_transform;
	sg_mechanism_derived_timing_t timing;
	sg_mechanism_kernel_parameters_t parameters;
	int source_relative;
	int destination_relative;

	if (!candidate || !CandidateMatchesTrace(candidate, trace) ||
		trace->candidate_identity == 0U || trace->trace_identity == 0U ||
		trace->source_set_identity != source->authority->identity.source_set_identity ||
		trace->bsp_content_id != source->authority->identity.bsp_content_id ||
		trace->physics_abi_id != source->authority->identity.physics_abi_id ||
		trace->controller_entity >= source->entity_semantics->entity_count ||
		trace->mechanism_entity >= source->entity_semantics->entity_count ||
		trace->source_region >= source->configuration_semantics->region_count ||
		trace->destination_region >=
			source->configuration_semantics->region_count ||
		trace->source_phase >= source->phase_count ||
		trace->destination_phase >= source->phase_count ||
		trace->kind < 0 || trace->kind >= SG_MECHANISM_CAPABILITY_KIND_COUNT ||
		trace->source_state < 0 || trace->source_state >= SG_MECHANISM_STATE_COUNT ||
		trace->destination_state < 0 ||
		trace->destination_state >= SG_MECHANISM_STATE_COUNT ||
		trace->activation < 0 || trace->activation >= SG_MECHANISM_ACTIVATION_COUNT ||
		trace->recovery < 0 || trace->recovery >= SG_MECHANISM_RECOVERY_COUNT ||
		(trace->flags &
			~(sg_mechanism_host_trace_flags_t)
				SG_MECHANISM_HOST_TRACE_FLAGS_KNOWN) != 0U ||
		!Finite3(&trace->entry_witness) || !Finite3(&trace->exit_witness) ||
		!Finite3(&trace->observed_displacement) ||
		!Finite3(&trace->observed_velocity))
	{
		SetError(build, SG_MECHANISM_CAPABILITY_ERROR_INVALID_SOURCE,
			trace_index);
		return 0;
	}
	controller = &source->entity_semantics->entities[trace->controller_entity];
	mechanism = &source->entity_semantics->entities[trace->mechanism_entity];
	if ((controller->flags & SG_BSP_ENTITY_HAS_MECHANISM) == 0U ||
		(mechanism->flags & SG_BSP_ENTITY_HAS_MECHANISM) == 0U ||
		!Finite3(&mechanism->move_direction) ||
		!Finite3(&mechanism->move_origin) ||
		!Finite3(&mechanism->move_angles) ||
		!KindMatches(mechanism, trace->kind) ||
		!ActivationMatches(controller, trace->activation))
	{
		SetError(build, SG_MECHANISM_CAPABILITY_ERROR_INVALID_SOURCE,
			trace_index);
		return 0;
	}
	if (!PointInRegion(source->configuration_semantics, trace->source_region,
			&trace->entry_witness) ||
		!PointInRegion(source->configuration_semantics,
			trace->destination_region, &trace->exit_witness))
	{
		SetError(build, SG_MECHANISM_CAPABILITY_ERROR_HOST_DISAGREEMENT,
			trace_index);
		return 0;
	}
	if (!EndpointStancesValid(source, trace))
	{
		SetError(build, SG_MECHANISM_CAPABILITY_ERROR_INVALID_PHASE,
			trace_index);
		return 0;
	}
	if (!DeriveTiming(trace, controller, mechanism, &timing))
	{
		SetError(build, SG_MECHANISM_CAPABILITY_ERROR_TIMING, trace_index);
		return 0;
	}
	if (!FillParameters(source, trace, &timing, &parameters))
	{
		SetError(build, SG_MECHANISM_CAPABILITY_ERROR_HOST_DISAGREEMENT,
			trace_index);
		return 0;
	}
	if ((((trace->flags & SG_MECHANISM_HOST_TRACE_ONE_SHOT) != 0U) !=
		 (((controller->flags & SG_BSP_ENTITY_DWELL_DEFINED) != 0U) &&
		  controller->dwell_ms == -1000.0f)))
	{
		SetError(build, SG_MECHANISM_CAPABILITY_ERROR_TIMING,
			trace_index);
		return 0;
	}
	if (!StateLawValid(trace, &timing) || !ExecutionTransitionValid(trace) ||
		!VectorDifferenceEqual(trace->exit_witness.value,
			trace->entry_witness.value, &trace->observed_displacement))
	{
		SetError(build, SG_MECHANISM_CAPABILITY_ERROR_HOST_DISAGREEMENT,
			trace_index);
		return 0;
	}
	if ((trace->kind == SG_MECHANISM_CAPABILITY_BUTTON_ACTIVATION &&
		 mechanism->mechanism_role != SG_MECH_NODE_BUTTON) ||
		(trace->kind == SG_MECHANISM_CAPABILITY_TRIGGER_ACTIVATION &&
		 mechanism->mechanism_role != SG_MECH_NODE_TRIGGER) ||
		(trace->kind == SG_MECHANISM_CAPABILITY_AREA_PORTAL_STATE &&
		 mechanism->mechanism_role != SG_MECH_NODE_AREAPORTAL))
	{
		SetError(build, SG_MECHANISM_CAPABILITY_ERROR_INVALID_SOURCE,
			trace_index);
		return 0;
	}
	if ((trace->kind == SG_MECHANISM_CAPABILITY_DWELL ||
		 trace->kind == SG_MECHANISM_CAPABILITY_RESET) &&
		(mechanism->flags & SG_BSP_ENTITY_DWELL_DEFINED) == 0U)
	{
		SetError(build, SG_MECHANISM_CAPABILITY_ERROR_TIMING, trace_index);
		return 0;
	}
	if (!ReplayHostTrace(source, trace, &inactive_transition,
		&active_transition, &inactive_transform, &active_transform))
	{
		SetError(build, SG_MECHANISM_CAPABILITY_ERROR_HOST_DISAGREEMENT,
			trace_index);
		return 0;
	}
	if (trace->kind == SG_MECHANISM_CAPABILITY_PUSH)
	{
		double dot = (double)trace->observed_displacement.value[0] *
			mechanism->move_direction.value[0] +
			(double)trace->observed_displacement.value[1] *
			mechanism->move_direction.value[1] +
			(double)trace->observed_displacement.value[2] *
			mechanism->move_direction.value[2];

		if (!(dot > 0.0))
		{
			SetError(build, SG_MECHANISM_CAPABILITY_ERROR_HOST_DISAGREEMENT,
				trace_index);
			return 0;
		}
	}
	if (trace->kind == SG_MECHANISM_CAPABILITY_TELEPORT &&
		trace->source_region == trace->destination_region)
	{
		SetError(build, SG_MECHANISM_CAPABILITY_ERROR_HOST_DISAGREEMENT,
			trace_index);
		return 0;
	}
	if (!MechanismId(source, trace->mechanism_entity, &mechanism_id))
	{
		SetError(build, SG_MECHANISM_CAPABILITY_ERROR_INVALID_SOURCE,
			trace_index);
		return 0;
	}
	source_relative = source->phases[trace->source_phase].reference_frame ==
		SG_RUNE_FRAME_MOVER_RELATIVE;
	destination_relative =
		source->phases[trace->destination_phase].reference_frame ==
		SG_RUNE_FRAME_MOVER_RELATIVE;
	if (source_relative && !StableIdEqual(
			&source->phases[trace->source_phase].mover.value,
			&mechanism_id.value))
		source_relative = -1;
	if (destination_relative && !StableIdEqual(
			&source->phases[trace->destination_phase].mover.value,
			&mechanism_id.value))
		destination_relative = -1;
	if (source_relative < 0 || destination_relative < 0 ||
		((trace->kind == SG_MECHANISM_CAPABILITY_LIFT_RIDE ||
		  trace->kind == SG_MECHANISM_CAPABILITY_TRAIN_RIDE) &&
		 (!source_relative || !destination_relative)))
	{
		SetError(build, SG_MECHANISM_CAPABILITY_ERROR_INVALID_PHASE,
			trace_index);
		return 0;
	}
	return 1;
}

static int AllocateTraversal(sg_mechanism_build_t *build)
{
	uint32_t count = build->source->entity_semantics->entity_count;

	if (!AllocationFits(count, 2U * sizeof(*build->queue)) ||
		!AllocationFits(count, sizeof(*build->distance)) ||
		!AllocationFits(count, sizeof(*build->ways)) ||
		!AllocationFits(count, sizeof(*build->propagated)) ||
		!AllocationFits(count, sizeof(*build->parent)) ||
		!AllocationFits(count, sizeof(*build->parent_edge)) ||
		!AllocationFits(count, sizeof(*build->path)))
	{
		SetError(build, SG_MECHANISM_CAPABILITY_ERROR_OVERFLOW, count);
		return 0;
	}
	build->queue = malloc((size_t)count * 2U * sizeof(*build->queue));
	build->distance = malloc((size_t)count * sizeof(*build->distance));
	build->ways = malloc((size_t)count * sizeof(*build->ways));
	build->propagated = malloc((size_t)count * sizeof(*build->propagated));
	build->parent = malloc((size_t)count * sizeof(*build->parent));
	build->parent_edge = malloc((size_t)count * sizeof(*build->parent_edge));
	build->path = malloc((size_t)count * sizeof(*build->path));
	if (!build->queue || !build->distance || !build->ways ||
		!build->propagated || !build->parent || !build->parent_edge ||
		!build->path)
	{
		SetError(build, SG_MECHANISM_CAPABILITY_ERROR_OUT_OF_MEMORY, 0U);
		return 0;
	}
	return 1;
}

static int PrepareTopologyPaths(sg_mechanism_build_t *build,
	uint32_t controller)
{
	uint32_t entity_count = build->source->entity_semantics->entity_count;
	uint32_t edge_count = build->source->entity_semantics->edge_count;
	size_t head = 0U;
	size_t tail = 0U;
	size_t queue_capacity = (size_t)entity_count * 2U;
	uint32_t entity;

	if (!build->edge_offsets || (edge_count != 0U && !build->edge_refs) ||
		!build->queue || !build->distance || !build->ways ||
		!build->propagated || !build->parent || !build->parent_edge ||
		controller >= entity_count)
	{
		SetError(build, SG_MECHANISM_CAPABILITY_ERROR_INVALID_TOPOLOGY,
			controller);
		return 0;
	}
	for (entity = 0U; entity < entity_count; entity++)
	{
		build->distance[entity] = UINT32_MAX;
		build->ways[entity] = 0U;
		build->propagated[entity] = 0U;
		build->parent[entity] = UINT32_MAX;
		build->parent_edge[entity] = UINT32_MAX;
	}
	build->distance[controller] = 0U;
	build->ways[controller] = 1U;
	build->queue[tail++] = controller;
	while (head < tail)
	{
		uint32_t current = build->queue[head++];
		uint32_t delta = build->ways[current] - build->propagated[current];
		uint32_t edge;

		build->propagated[current] = build->ways[current];
		if (delta == 0U)
			continue;
		for (edge = build->edge_offsets[current];
			edge < build->edge_offsets[current + 1U]; edge++)
		{
			const sg_mechanism_edge_ref_t *reference;
			uint32_t destination;
			uint32_t next_ways;

			if (edge >= edge_count)
			{
				SetError(build,
					SG_MECHANISM_CAPABILITY_ERROR_INVALID_TOPOLOGY, edge);
				return 0;
			}
			reference = &build->edge_refs[edge];
			destination = reference->destination;
			if (!EdgeTraversable((sg_mech_edge_kind_t)reference->kind))
				continue;
			build->output->topology_edge_visits++;
			next_ways = build->ways[destination] + delta;
			if (next_ways > 1U)
				next_ways = 2U;
			if (next_ways != build->ways[destination])
			{
				if (build->ways[destination] == 0U)
				{
					build->distance[destination] =
						build->distance[current] + 1U;
					build->parent[destination] = current;
					build->parent_edge[destination] = reference->edge;
				}
				build->ways[destination] = next_ways;
				if (tail >= queue_capacity)
				{
					SetError(build,
						SG_MECHANISM_CAPABILITY_ERROR_OVERFLOW, destination);
					return 0;
				}
				build->queue[tail++] = destination;
			}
		}
	}
	return 1;
}

static int AppendPreparedTopologyPath(sg_mechanism_build_t *build,
	uint32_t controller, uint32_t mechanism, uint32_t *first_out,
	uint32_t *count_out)
{
	uint32_t entity_count = build->source->entity_semantics->entity_count;
	uint32_t entity;
	uint32_t path_count = 0U;

	*first_out = build->output->topology_edge_count;
	*count_out = 0U;
	if (controller == mechanism)
		return 1;
	if (build->distance[mechanism] == UINT32_MAX)
	{
		SetError(build, SG_MECHANISM_CAPABILITY_ERROR_INVALID_TOPOLOGY,
			mechanism);
		return 0;
	}
	if (build->ways[mechanism] != 1U)
	{
		SetError(build, SG_MECHANISM_CAPABILITY_ERROR_AMBIGUOUS_TOPOLOGY,
			mechanism);
		return 0;
	}
	entity = mechanism;
	while (entity != controller)
	{
		if (path_count >= entity_count ||
			build->parent[entity] == UINT32_MAX ||
			build->parent_edge[entity] == UINT32_MAX)
		{
			SetError(build, SG_MECHANISM_CAPABILITY_ERROR_INVALID_TOPOLOGY,
				mechanism);
			return 0;
		}
		build->path[path_count++] = build->parent_edge[entity];
		entity = build->parent[entity];
	}
	if (build->output->topology_edge_count > build->topology_edge_capacity ||
		path_count > build->topology_edge_capacity -
		build->output->topology_edge_count)
	{
		SetError(build, SG_MECHANISM_CAPABILITY_ERROR_OVERFLOW,
			mechanism);
		return 0;
	}
	while (path_count != 0U)
		build->output->topology_edges[
			build->output->topology_edge_count++] = build->path[--path_count];
	*count_out = build->output->topology_edge_count - *first_out;
	return 1;
}

static int PreparedTopologyPathCount(sg_mechanism_build_t *build,
	uint32_t controller, uint32_t mechanism, uint32_t *count_out)
{
	if (controller == mechanism)
	{
		*count_out = 0U;
		return 1;
	}
	if (build->distance[mechanism] == UINT32_MAX)
	{
		SetError(build, SG_MECHANISM_CAPABILITY_ERROR_INVALID_TOPOLOGY,
			mechanism);
		return 0;
	}
	if (build->ways[mechanism] != 1U)
	{
		SetError(build, SG_MECHANISM_CAPABILITY_ERROR_AMBIGUOUS_TOPOLOGY,
			mechanism);
		return 0;
	}
	*count_out = build->distance[mechanism];
	return 1;
}

static void ExactInterval(sg_rune_interval_t *interval, float value)
{
	interval->min_value = value;
	interval->max_value = value;
}

static int IntervalFiniteOrdered(const sg_rune_interval_t *interval)
{
	return isfinite(interval->min_value) && isfinite(interval->max_value) &&
		interval->min_value <= interval->max_value;
}

static int Interval3FiniteOrdered(const sg_rune_interval3_t *interval)
{
	return IntervalFiniteOrdered(&interval->x) &&
		IntervalFiniteOrdered(&interval->y) &&
		IntervalFiniteOrdered(&interval->z);
}

static int ParametersValid(const sg_mechanism_capability_source_t *source,
	const sg_mechanism_derived_timing_t *timing,
	const sg_mechanism_kernel_parameters_t *parameters)
{
	uint64_t total = (uint64_t)timing->delay_ms +
		(uint64_t)timing->dwell_ms + (uint64_t)timing->travel_ms +
		(uint64_t)timing->wait_ms + (uint64_t)timing->reset_ms;

	return Interval3FiniteOrdered(&parameters->displacement) &&
		IntervalFiniteOrdered(&parameters->speed) &&
		parameters->speed.min_value >= 0.0f &&
		parameters->speed.max_value <=
			source->authority->identity.physics.max_velocity &&
		IntervalFiniteOrdered(&parameters->acceleration) &&
		IntervalFiniteOrdered(&parameters->vertical_acceleration) &&
		isfinite(parameters->gravity) && parameters->gravity >= 0.0f &&
		isfinite(parameters->drag) && parameters->drag >= 0.0f &&
		parameters->physics_abi_id ==
			source->authority->identity.physics_abi_id &&
		parameters->duration_ms == timing->travel_ms &&
		parameters->fixed_latency_ms == timing->delay_ms &&
		parameters->dwell_ms == timing->dwell_ms &&
		parameters->wait_ms == timing->wait_ms &&
		parameters->reset_ms == timing->reset_ms &&
		parameters->total_ms == total;
}

static int FillParameters(const sg_mechanism_capability_source_t *source,
	const sg_mechanism_host_trace_t *trace,
	const sg_mechanism_derived_timing_t *timing,
	sg_mechanism_kernel_parameters_t *parameters)
{
	double speed = hypot(hypot((double)trace->observed_velocity.value[0],
		(double)trace->observed_velocity.value[1]),
		(double)trace->observed_velocity.value[2]);
	float represented_speed;

	if (!parameters || !isfinite(speed) || speed > (double)FLT_MAX ||
		speed > (double)source->authority->identity.physics.max_velocity)
		return 0;
	represented_speed = (float)speed;
	if (!isfinite(represented_speed))
		return 0;

	memset(parameters, 0, sizeof(*parameters));
	ExactInterval(&parameters->displacement.x,
		trace->observed_displacement.value[0]);
	ExactInterval(&parameters->displacement.y,
		trace->observed_displacement.value[1]);
	ExactInterval(&parameters->displacement.z,
		trace->observed_displacement.value[2]);
	ExactInterval(&parameters->speed, represented_speed);
	parameters->gravity = source->authority->identity.physics.gravity;
	parameters->physics_abi_id = source->authority->identity.physics_abi_id;
	parameters->duration_ms = timing->travel_ms;
	parameters->fixed_latency_ms = timing->delay_ms;
	parameters->dwell_ms = timing->dwell_ms;
	parameters->wait_ms = timing->wait_ms;
	parameters->reset_ms = timing->reset_ms;
	parameters->total_ms = (uint64_t)timing->delay_ms +
		(uint64_t)timing->dwell_ms + (uint64_t)timing->travel_ms +
		(uint64_t)timing->wait_ms + (uint64_t)timing->reset_ms;
	return ParametersValid(source, timing, parameters);
}

static sg_mechanism_capability_flags_t FactFlags(
	const sg_mechanism_capability_source_t *source,
	const sg_mechanism_host_trace_t *trace,
	const sg_mechanism_derived_timing_t *timing)
{
	sg_mechanism_capability_flags_t flags =
		SG_MECHANISM_CAPABILITY_HOST_PROVEN;

	if (KindConditional(trace->kind))
		flags |= SG_MECHANISM_CAPABILITY_CONDITIONAL;
	if (source->phases[trace->source_phase].reference_frame ==
			SG_RUNE_FRAME_MOVER_RELATIVE ||
		source->phases[trace->destination_phase].reference_frame ==
			SG_RUNE_FRAME_MOVER_RELATIVE)
		flags |= SG_MECHANISM_CAPABILITY_MOVER_RELATIVE;
	if (trace->flags & SG_MECHANISM_HOST_TRACE_ONE_SHOT)
		flags |= SG_MECHANISM_CAPABILITY_ONE_SHOT;
	if (timing->reset_ms > 0U ||
		trace->recovery != SG_MECHANISM_RECOVERY_NONE)
		flags |= SG_MECHANISM_CAPABILITY_RESETS;
	return flags;
}

static const sg_mechanism_topology_relation_t *FindTopologyRelation(
	const sg_mechanism_capability_payload_t *set, uint32_t controller,
	uint32_t mechanism)
{
	uint32_t low = 0U;
	uint32_t high = set->topology_relation_count;

	while (low < high)
	{
		uint32_t middle = low + (high - low) / 2U;
		const sg_mechanism_topology_relation_t *relation =
			&set->topology_relations[middle];

		if (relation->controller_entity < controller ||
			(relation->controller_entity == controller &&
			 relation->mechanism_entity < mechanism))
			low = middle + 1U;
		else
			high = middle;
	}
	if (low >= set->topology_relation_count ||
		set->topology_relations[low].controller_entity != controller ||
		set->topology_relations[low].mechanism_entity != mechanism)
		return NULL;
	return &set->topology_relations[low];
}

static int BuildFacts(sg_mechanism_build_t *build)
{
	const sg_mechanism_host_trace_catalog_t *catalog =
		build->source->host_traces;
	sg_mechanism_trace_ref_t *refs = NULL;
	sg_mechanism_trace_index_t *trace_index = NULL;
	sg_mechanism_candidate_index_t *candidates = NULL;
	sg_mechanism_candidate_index_t *trace_candidates = NULL;
	sg_mechanism_relation_ref_t *relation_refs = NULL;
	uint32_t fact;
	uint32_t entity = 0U;

	refs = calloc(catalog->trace_count, sizeof(*refs));
	trace_index = calloc(catalog->trace_count, sizeof(*trace_index));
	candidates = calloc(catalog->candidate_count, sizeof(*candidates));
	trace_candidates = calloc(catalog->trace_count,
		sizeof(*trace_candidates));
	relation_refs = calloc(catalog->trace_count, sizeof(*relation_refs));
	build->output->facts = calloc(catalog->trace_count,
		sizeof(*build->output->facts));
	build->output->topology_relations = calloc(catalog->trace_count,
		sizeof(*build->output->topology_relations));
	build->output->mechanism_offsets = calloc(
		(size_t)build->source->entity_semantics->entity_count + 1U,
		sizeof(*build->output->mechanism_offsets));
	build->output->facts_by_trace = calloc(catalog->trace_count,
		sizeof(*build->output->facts_by_trace));
	if (!refs || !trace_index || !candidates || !trace_candidates ||
		!relation_refs || !build->output->facts ||
		!build->output->topology_relations ||
		!build->output->mechanism_offsets || !build->output->facts_by_trace)
	{
		SetError(build, SG_MECHANISM_CAPABILITY_ERROR_OUT_OF_MEMORY, 0U);
		goto failure;
	}
	for (fact = 0U; fact < catalog->trace_count; fact++)
	{
		const sg_mechanism_capability_candidate_t *candidate =
			&catalog->candidates[fact];
		const sg_mechanism_host_trace_t *trace = &catalog->traces[fact];

		if (!CandidateValid(build->source, candidate))
		{
			SetError(build, SG_MECHANISM_CAPABILITY_ERROR_INVALID_SOURCE,
				fact);
			goto failure;
		}
		candidates[fact].identity = candidate->candidate_identity;
		candidates[fact].candidate = fact;
		trace_candidates[fact].identity = trace->candidate_identity;
		trace_candidates[fact].trace = fact;
	}
	qsort(candidates, catalog->candidate_count, sizeof(*candidates),
		CandidateIndexCompare);
	qsort(trace_candidates, catalog->trace_count, sizeof(*trace_candidates),
		CandidateIndexCompare);
	for (fact = 0U; fact < catalog->trace_count; fact++)
	{
		uint32_t candidate_index = candidates[fact].candidate;
		uint32_t trace_number = trace_candidates[fact].trace;
		const sg_mechanism_host_trace_t *trace =
			&catalog->traces[trace_number];

		if ((fact != 0U && candidates[fact - 1U].identity ==
				candidates[fact].identity) ||
			(fact != 0U && trace_candidates[fact - 1U].identity ==
				trace_candidates[fact].identity) ||
			candidates[fact].identity != trace_candidates[fact].identity ||
			!TraceValid(build, trace_number,
				&catalog->candidates[candidate_index]))
		{
			if (build->error.code == SG_MECHANISM_CAPABILITY_ERROR_NONE)
				SetError(build, SG_MECHANISM_CAPABILITY_ERROR_INVALID_SOURCE,
					trace_number);
			goto failure;
		}
		refs[fact].trace = trace_number;
		refs[fact].mechanism = trace->mechanism_entity;
		refs[fact].kind = (uint32_t)trace->kind;
		refs[fact].controller = trace->controller_entity;
		refs[fact].source_region = trace->source_region;
		refs[fact].destination_region = trace->destination_region;
		refs[fact].source_phase = trace->source_phase;
		refs[fact].destination_phase = trace->destination_phase;
		refs[fact].source_state = (uint32_t)trace->source_state;
		refs[fact].destination_state = (uint32_t)trace->destination_state;
		refs[fact].identity = trace->trace_identity;
		relation_refs[fact].controller = trace->controller_entity;
		relation_refs[fact].mechanism = trace->mechanism_entity;
	}
	qsort(relation_refs, catalog->trace_count, sizeof(*relation_refs),
		RelationRefCompare);
	for (fact = 0U; fact < catalog->trace_count; fact++)
	{
		const sg_mechanism_relation_ref_t *reference = &relation_refs[fact];
		sg_mechanism_topology_relation_t *relation;

		if (fact != 0U && relation_refs[fact - 1U].controller ==
			reference->controller && relation_refs[fact - 1U].mechanism ==
			reference->mechanism)
			continue;
		if (fact == 0U || relation_refs[fact - 1U].controller !=
			reference->controller)
		{
			if (!PrepareTopologyPaths(build, reference->controller))
				goto failure;
		}
		relation = &build->output->topology_relations[
			build->output->topology_relation_count++];
		relation->controller_entity = reference->controller;
		relation->mechanism_entity = reference->mechanism;
		relation->first_edge = build->topology_edge_capacity;
		if (!PreparedTopologyPathCount(build, reference->controller,
			reference->mechanism, &relation->edge_count))
			goto failure;
		if (relation->edge_count > UINT32_MAX -
			build->topology_edge_capacity)
		{
			SetError(build, SG_MECHANISM_CAPABILITY_ERROR_OVERFLOW, fact);
			goto failure;
		}
		build->topology_edge_capacity += relation->edge_count;
	}
	if (!AllocationFits(build->topology_edge_capacity,
		sizeof(*build->output->topology_edges)))
	{
		SetError(build, SG_MECHANISM_CAPABILITY_ERROR_OVERFLOW,
			build->topology_edge_capacity);
		goto failure;
	}
	if (build->topology_edge_capacity != 0U)
	{
		build->output->topology_edges = calloc(build->topology_edge_capacity,
			sizeof(*build->output->topology_edges));
		if (!build->output->topology_edges)
		{
			SetError(build, SG_MECHANISM_CAPABILITY_ERROR_OUT_OF_MEMORY, 0U);
			goto failure;
		}
	}
	for (fact = 0U; fact < build->output->topology_relation_count; fact++)
	{
		const sg_mechanism_topology_relation_t *relation =
			&build->output->topology_relations[fact];
		uint32_t first;
		uint32_t count;

		if (fact == 0U || build->output->topology_relations[fact - 1U].
			controller_entity != relation->controller_entity)
		{
			if (!PrepareTopologyPaths(build, relation->controller_entity))
				goto failure;
		}
		if (!AppendPreparedTopologyPath(build, relation->controller_entity,
			relation->mechanism_entity, &first, &count) ||
			first != relation->first_edge || count != relation->edge_count)
		{
			if (build->error.code == SG_MECHANISM_CAPABILITY_ERROR_NONE)
				SetError(build,
					SG_MECHANISM_CAPABILITY_ERROR_INVALID_TOPOLOGY, fact);
			goto failure;
		}
	}
	qsort(refs, catalog->trace_count, sizeof(*refs), TraceRefCompare);
	for (fact = 0U; fact < catalog->trace_count; fact++)
	{
		const sg_mechanism_host_trace_t *trace =
			&catalog->traces[refs[fact].trace];
		const sg_mechanism_topology_relation_t *relation =
			FindTopologyRelation(build->output, trace->controller_entity,
				trace->mechanism_entity);
		sg_mechanism_capability_fact_t *record = &build->output->facts[fact];
		sg_host_collision_transition_t inactive_transition;
		sg_host_collision_transition_t active_transition;
		sg_host_collision_transform_t inactive_transform;
		sg_host_collision_transform_t active_transform;
		sg_mechanism_derived_timing_t timing;
		sg_mechanism_state_t source_state;
		sg_mechanism_state_t destination_state;
		sg_mechanism_recovery_t recovery;

		if (!relation)
		{
			SetError(build, SG_MECHANISM_CAPABILITY_ERROR_INVALID_TOPOLOGY,
				refs[fact].trace);
			goto failure;
		}
		if (!ReplayHostTrace(build->source, trace, &inactive_transition,
			&active_transition, &inactive_transform, &active_transform) ||
			!DeriveTiming(trace,
				&build->source->entity_semantics->
					entities[trace->controller_entity],
				&build->source->entity_semantics->
					entities[trace->mechanism_entity], &timing) ||
			!DeriveState(trace->kind, &source_state, &destination_state,
				&recovery) ||
			!FillParameters(build->source, trace, &timing,
				&record->parameters))
		{
			SetError(build, SG_MECHANISM_CAPABILITY_ERROR_HOST_DISAGREEMENT,
				refs[fact].trace);
			goto failure;
		}
		while (entity <= trace->mechanism_entity)
			build->output->mechanism_offsets[entity++] = fact;
		record->order = fact;
		record->trace_identity = trace->trace_identity;
		record->controller_entity = trace->controller_entity;
		record->mechanism_entity = trace->mechanism_entity;
		record->source_region = trace->source_region;
		record->destination_region = trace->destination_region;
		record->source_phase = trace->source_phase;
		record->destination_phase = trace->destination_phase;
		record->first_topology_edge = relation->first_edge;
		record->topology_edge_count = relation->edge_count;
		record->kind = trace->kind;
		record->source_state = source_state;
		record->destination_state = destination_state;
		record->activation = trace->activation;
		record->recovery = recovery;
		record->entry_witness = trace->entry_witness;
		record->exit_witness = trace->exit_witness;
		record->observed_displacement = trace->observed_displacement;
		record->observed_velocity = trace->observed_velocity;
		record->mechanism_direction = build->source->entity_semantics->
			entities[trace->mechanism_entity].move_direction;
		record->mechanism_origin = build->source->entity_semantics->
			entities[trace->mechanism_entity].move_origin;
		record->mechanism_angles = build->source->entity_semantics->
			entities[trace->mechanism_entity].move_angles;
		record->inactive_transition = inactive_transition;
		record->active_transition = active_transition;
		record->inactive_mechanism_transform = inactive_transform;
		record->active_mechanism_transform = active_transform;
		record->source_execution = trace->source_execution;
		record->destination_execution = trace->destination_execution;
		record->mechanism_instance_id = trace->mechanism_instance_id;
		record->delay_ms = timing.delay_ms;
		record->dwell_ms = timing.dwell_ms;
		record->travel_ms = timing.travel_ms;
		record->wait_ms = timing.wait_ms;
		record->reset_ms = timing.reset_ms;
		record->activation_time_ms = trace->activation_time_ms;
		record->active_time_ms = trace->active_time_ms;
		record->exit_time_ms = trace->exit_time_ms;
		record->reset_time_ms = trace->reset_time_ms;
		record->flags = FactFlags(build->source, trace, &timing);
		if (!MechanismId(build->source, trace->controller_entity,
				&record->controller_id) ||
			!MechanismId(build->source, trace->mechanism_entity,
				&record->mechanism_id))
		{
			SetError(build, SG_MECHANISM_CAPABILITY_ERROR_INVALID_SOURCE,
				refs[fact].trace);
			goto failure;
		}
		trace_index[fact].identity = trace->trace_identity;
		trace_index[fact].fact = fact;
	}
	while (entity <= build->source->entity_semantics->entity_count)
		build->output->mechanism_offsets[entity++] = catalog->trace_count;
	qsort(trace_index, catalog->trace_count, sizeof(*trace_index),
		TraceIndexCompare);
	for (fact = 0U; fact < catalog->trace_count; fact++)
	{
		if (fact != 0U && trace_index[fact - 1U].identity ==
			trace_index[fact].identity)
		{
			SetError(build, SG_MECHANISM_CAPABILITY_ERROR_INVALID_SOURCE,
				trace_index[fact].fact);
			goto failure;
		}
		build->output->facts_by_trace[fact] = trace_index[fact].fact;
	}
	build->output->fact_count = catalog->trace_count;
	build->output->mechanism_offset_count =
		build->source->entity_semantics->entity_count + 1U;
	free(refs);
	free(trace_index);
	free(candidates);
	free(trace_candidates);
	free(relation_refs);
	return 1;

failure:
	free(refs);
	free(trace_index);
	free(candidates);
	free(trace_candidates);
	free(relation_refs);
	return 0;
}

static void BuildScratchDestroy(sg_mechanism_build_t *build)
{
	free(build->edge_refs);
	free(build->edge_offsets);
	free(build->queue);
	free(build->distance);
	free(build->ways);
	free(build->propagated);
	free(build->parent);
	free(build->parent_edge);
	free(build->path);
}

int SG_MechanismCapabilityBuild(
	sg_mechanism_capability_owner_t *owner,
	const sg_mechanism_capability_source_t *source,
	sg_mechanism_capability_set_t **capabilities_out,
	sg_mechanism_capability_error_t *error_out)
{
	sg_mechanism_build_t build;
	int success;

	if (capabilities_out)
		*capabilities_out = NULL;
	if (error_out)
	{
		error_out->code = SG_MECHANISM_CAPABILITY_ERROR_NONE;
		error_out->source_index = SG_MECHANISM_CAPABILITY_INDEX_NONE;
	}
	memset(&build, 0, sizeof(build));
	build.source = source;
	build.error.source_index = SG_MECHANISM_CAPABILITY_INDEX_NONE;
	if (!owner || !capabilities_out)
		SetError(&build, SG_MECHANISM_CAPABILITY_ERROR_INVALID_ARGUMENT,
			SG_MECHANISM_CAPABILITY_INDEX_NONE);
	if (!owner || !capabilities_out || !SourceShapeValid(&build))
	{
		if (error_out)
			*error_out = build.error;
		return 0;
	}
	build.output = calloc(1U, sizeof(*build.output));
	if (!build.output)
	{
		SetError(&build, SG_MECHANISM_CAPABILITY_ERROR_OUT_OF_MEMORY, 0U);
		if (error_out)
			*error_out = build.error;
		return 0;
	}
	build.output->identity = source->authority->identity;
	build.output->candidate_verifier_identity =
		source->host_traces->candidate_verifier_identity;
	build.output->trace_verifier_identity =
		source->host_traces->trace_verifier_identity;
	success = BuildTopologyIndex(&build) && AllocateTraversal(&build) &&
		BuildFacts(&build);
	BuildScratchDestroy(&build);
	if (!success)
	{
		ReleaseCapabilityStorage(build.output);
		free(build.output);
		if (error_out)
			*error_out = build.error;
		return 0;
	}
	if (!IssueAcceptedResult(owner, build.output, capabilities_out))
	{
		ReleaseCapabilityStorage(build.output);
		free(build.output);
		if (error_out)
		{
			error_out->code = SG_MECHANISM_CAPABILITY_ERROR_OVERFLOW;
			error_out->source_index = 0U;
		}
		return 0;
	}
	if (!SG_MechanismCapabilityOwnerAccepted(owner, *capabilities_out, NULL))
	{
		SG_MechanismCapabilityDestroy(owner, *capabilities_out);
		*capabilities_out = NULL;
		if (error_out)
		{
			error_out->code = SG_MECHANISM_CAPABILITY_ERROR_INVALID_SOURCE;
			error_out->source_index = 0U;
		}
		return 0;
	}
	return 1;
}

static uint32_t FindFactByTrace(const sg_mechanism_capability_payload_t *set,
	uint64_t identity, uint64_t *comparisons)
{
	uint32_t low = 0U;
	uint32_t high = set->fact_count;

	while (low < high)
	{
		uint32_t middle = low + (high - low) / 2U;
		uint32_t fact = set->facts_by_trace[middle];
		uint64_t found;

		(*comparisons)++;
		if (fact >= set->fact_count)
			return SG_MECHANISM_CAPABILITY_INDEX_NONE;
		found = set->facts[fact].trace_identity;
		if (found < identity)
			low = middle + 1U;
		else
			high = middle;
	}
	if (low >= set->fact_count)
		return SG_MECHANISM_CAPABILITY_INDEX_NONE;
	(*comparisons)++;
	if (set->facts_by_trace[low] >= set->fact_count ||
		set->facts[set->facts_by_trace[low]].trace_identity != identity)
		return SG_MECHANISM_CAPABILITY_INDEX_NONE;
	return set->facts_by_trace[low];
}

static int CatalogEvidenceAuthentic(
	const sg_mechanism_capability_source_t *source)
{
	const sg_mechanism_host_trace_catalog_t *catalog;
	sg_mechanism_candidate_index_t *candidates = NULL;
	sg_mechanism_candidate_index_t *traces = NULL;
	sg_mechanism_build_t build;
	uint32_t index;
	int valid = 0;

	memset(&build, 0, sizeof(build));
	build.source = source;
	build.error.source_index = SG_MECHANISM_CAPABILITY_INDEX_NONE;
	if (!SourceShapeValid(&build))
		return 0;
	catalog = source->host_traces;
	candidates = calloc(catalog->candidate_count, sizeof(*candidates));
	traces = calloc(catalog->trace_count, sizeof(*traces));
	if (!candidates || !traces)
		goto done;
	for (index = 0U; index < catalog->trace_count; index++)
	{
		if (!CandidateValid(source, &catalog->candidates[index]))
			goto done;
		candidates[index].identity =
			catalog->candidates[index].candidate_identity;
		candidates[index].candidate = index;
		traces[index].identity = catalog->traces[index].candidate_identity;
		traces[index].trace = index;
	}
	qsort(candidates, catalog->candidate_count, sizeof(*candidates),
		CandidateIndexCompare);
	qsort(traces, catalog->trace_count, sizeof(*traces),
		CandidateIndexCompare);
	for (index = 0U; index < catalog->trace_count; index++)
	{
		if ((index != 0U && candidates[index - 1U].identity ==
				candidates[index].identity) ||
			(index != 0U && traces[index - 1U].identity ==
				traces[index].identity) ||
			candidates[index].identity != traces[index].identity ||
			!TraceValid(&build, traces[index].trace,
				&catalog->candidates[candidates[index].candidate]))
			goto done;
	}
	valid = 1;

done:
	free(candidates);
	free(traces);
	return valid;
}

static int TopologyAuthentic(
	const sg_mechanism_capability_source_t *source,
	const sg_mechanism_capability_payload_t *capabilities)
{
	sg_mechanism_capability_payload_t metrics;
	sg_mechanism_build_t build;
	uint32_t relation_index;
	int valid = 0;

	memset(&metrics, 0, sizeof(metrics));
	memset(&build, 0, sizeof(build));
	build.source = source;
	build.output = &metrics;
	if (!BuildTopologyIndex(&build) || !AllocateTraversal(&build))
		goto done;
	for (relation_index = 0U;
		relation_index < capabilities->topology_relation_count;
		relation_index++)
	{
		const sg_mechanism_topology_relation_t *relation =
			&capabilities->topology_relations[relation_index];
		uint32_t current;
		uint32_t remaining;

		if (relation_index == 0U ||
			capabilities->topology_relations[relation_index - 1U].
				controller_entity != relation->controller_entity)
		{
			if (!PrepareTopologyPaths(&build, relation->controller_entity))
				goto done;
		}
		if (relation->controller_entity == relation->mechanism_entity)
		{
			if (relation->edge_count != 0U)
				goto done;
			continue;
		}
		if (build.distance[relation->mechanism_entity] !=
			relation->edge_count || build.ways[relation->mechanism_entity] != 1U)
			goto done;
		current = relation->mechanism_entity;
		remaining = relation->edge_count;
		while (current != relation->controller_entity)
		{
			if (remaining == 0U || build.parent[current] == UINT32_MAX ||
				build.parent_edge[current] == UINT32_MAX)
				goto done;
			remaining--;
			if (capabilities->topology_edges[relation->first_edge +
				remaining] != build.parent_edge[current])
				goto done;
			current = build.parent[current];
		}
		if (remaining != 0U)
			goto done;
	}
	valid = 1;

done:
	BuildScratchDestroy(&build);
	return valid;
}

static int FactMatchesTrace(const sg_mechanism_capability_source_t *source,
	const sg_mechanism_capability_fact_t *fact,
	const sg_mechanism_host_trace_t *trace)
{
	sg_rune_mechanism_id_t controller_id;
	sg_rune_mechanism_id_t mechanism_id;
	sg_mechanism_kernel_parameters_t parameters;
	sg_host_collision_transition_t inactive_transition;
	sg_host_collision_transition_t active_transition;
	sg_host_collision_transform_t inactive_transform;
	sg_host_collision_transform_t active_transform;
	sg_mechanism_derived_timing_t timing;
	sg_mechanism_state_t source_state;
	sg_mechanism_state_t destination_state;
	sg_mechanism_recovery_t recovery;

	if (!MechanismId(source, trace->controller_entity, &controller_id) ||
		!MechanismId(source, trace->mechanism_entity, &mechanism_id) ||
		!EndpointStancesValid(source, trace) ||
		!ReplayHostTrace(source, trace, &inactive_transition,
			&active_transition, &inactive_transform, &active_transform) ||
		!DeriveTiming(trace,
			&source->entity_semantics->entities[trace->controller_entity],
			&source->entity_semantics->entities[trace->mechanism_entity],
			&timing) ||
		!DeriveState(trace->kind, &source_state, &destination_state, &recovery) ||
		!FillParameters(source, trace, &timing, &parameters))
		return 0;
	return fact->trace_identity == trace->trace_identity &&
		StableIdEqual(&fact->controller_id.value, &controller_id.value) &&
		StableIdEqual(&fact->mechanism_id.value, &mechanism_id.value) &&
		fact->controller_entity == trace->controller_entity &&
		fact->mechanism_entity == trace->mechanism_entity &&
		fact->source_region == trace->source_region &&
		fact->destination_region == trace->destination_region &&
		fact->source_phase == trace->source_phase &&
		fact->destination_phase == trace->destination_phase &&
		fact->kind == trace->kind && fact->source_state == source_state &&
		fact->destination_state == destination_state &&
		fact->activation == trace->activation &&
		fact->recovery == recovery &&
		memcmp(&fact->entry_witness, &trace->entry_witness,
			sizeof(fact->entry_witness)) == 0 &&
		memcmp(&fact->exit_witness, &trace->exit_witness,
			sizeof(fact->exit_witness)) == 0 &&
		memcmp(&fact->observed_displacement, &trace->observed_displacement,
			sizeof(fact->observed_displacement)) == 0 &&
		memcmp(&fact->observed_velocity, &trace->observed_velocity,
			sizeof(fact->observed_velocity)) == 0 &&
		memcmp(&fact->mechanism_direction,
			&source->entity_semantics->entities[trace->mechanism_entity].
				move_direction, sizeof(fact->mechanism_direction)) == 0 &&
		memcmp(&fact->mechanism_origin,
			&source->entity_semantics->entities[trace->mechanism_entity].
				move_origin, sizeof(fact->mechanism_origin)) == 0 &&
		memcmp(&fact->mechanism_angles,
			&source->entity_semantics->entities[trace->mechanism_entity].
				move_angles, sizeof(fact->mechanism_angles)) == 0 &&
		memcmp(&fact->inactive_transition, &inactive_transition,
			sizeof(inactive_transition)) == 0 &&
		memcmp(&fact->active_transition, &active_transition,
			sizeof(active_transition)) == 0 &&
		memcmp(&fact->inactive_mechanism_transform, &inactive_transform,
			sizeof(inactive_transform)) == 0 &&
		memcmp(&fact->active_mechanism_transform, &active_transform,
			sizeof(active_transform)) == 0 &&
		memcmp(&fact->source_execution, &trace->source_execution,
			sizeof(fact->source_execution)) == 0 &&
		memcmp(&fact->destination_execution, &trace->destination_execution,
			sizeof(fact->destination_execution)) == 0 &&
		fact->mechanism_instance_id == trace->mechanism_instance_id &&
		fact->delay_ms == timing.delay_ms &&
		fact->dwell_ms == timing.dwell_ms &&
		fact->travel_ms == timing.travel_ms &&
		fact->wait_ms == timing.wait_ms &&
		fact->reset_ms == timing.reset_ms &&
		fact->activation_time_ms == trace->activation_time_ms &&
		fact->active_time_ms == trace->active_time_ms &&
		fact->exit_time_ms == trace->exit_time_ms &&
		fact->reset_time_ms == trace->reset_time_ms &&
		fact->flags == FactFlags(source, trace, &timing) &&
		memcmp(&fact->parameters, &parameters, sizeof(parameters)) == 0;
}

static int FactsOrdered(const sg_mechanism_capability_fact_t *left,
	const sg_mechanism_capability_fact_t *right)
{
	sg_mechanism_trace_ref_t left_ref;
	sg_mechanism_trace_ref_t right_ref;

	memset(&left_ref, 0, sizeof(left_ref));
	memset(&right_ref, 0, sizeof(right_ref));
	left_ref.mechanism = left->mechanism_entity;
	left_ref.kind = (uint32_t)left->kind;
	left_ref.controller = left->controller_entity;
	left_ref.source_region = left->source_region;
	left_ref.destination_region = left->destination_region;
	left_ref.source_phase = left->source_phase;
	left_ref.destination_phase = left->destination_phase;
	left_ref.source_state = (uint32_t)left->source_state;
	left_ref.destination_state = (uint32_t)left->destination_state;
	left_ref.identity = left->trace_identity;
	right_ref.mechanism = right->mechanism_entity;
	right_ref.kind = (uint32_t)right->kind;
	right_ref.controller = right->controller_entity;
	right_ref.source_region = right->source_region;
	right_ref.destination_region = right->destination_region;
	right_ref.source_phase = right->source_phase;
	right_ref.destination_phase = right->destination_phase;
	right_ref.source_state = (uint32_t)right->source_state;
	right_ref.destination_state = (uint32_t)right->destination_state;
	right_ref.identity = right->trace_identity;
	return TraceRefCompare(&left_ref, &right_ref) < 0;
}

static int AuditTopologyRelation(
	const sg_mechanism_capability_source_t *source,
	const sg_mechanism_capability_payload_t *capabilities,
	const sg_mechanism_topology_relation_t *relation)
{
	uint32_t current = relation->controller_entity;
	uint32_t offset;

	if ((current == relation->mechanism_entity) !=
		(relation->edge_count == 0U))
		return 0;
	for (offset = 0U; offset < relation->edge_count; offset++)
	{
		uint32_t edge_index = capabilities->topology_edges[
			relation->first_edge + offset];
		const sg_bsp_entity_semantic_edge_t *edge;

		if (edge_index >= source->entity_semantics->edge_count)
			return 0;
		edge = &source->entity_semantics->edges[edge_index];
		if (edge->source != current || !EdgeTraversable(edge->kind))
			return 0;
		current = edge->destination;
	}
	return current == relation->mechanism_entity;
}

int SG_MechanismCapabilityAudit(
	const sg_mechanism_capability_owner_t *owner,
	const sg_mechanism_capability_source_t *source,
	const sg_mechanism_capability_set_t *capability,
	sg_mechanism_capability_audit_result_t *result_out)
{
	const sg_mechanism_capability_payload_t *capabilities =
		SG_MechanismCapabilityOwnerPayload(owner, capability);
	sg_mechanism_capability_audit_result_t result;
	uint8_t *seen = NULL;
	uint8_t *relation_seen = NULL;
	uint32_t trace;
	uint32_t cursor = 0U;
	uint32_t expected_topology_first = 0U;

	memset(&result, 0, sizeof(result));
	result.code = SG_MECHANISM_CAPABILITY_AUDIT_INVALID_ARGUMENT;
	result.record = SG_MECHANISM_CAPABILITY_INDEX_NONE;
	if (!result_out)
		return 0;
	*result_out = result;
	if (!source || !capabilities || !source->authority ||
		!source->entity_semantics || !source->host_traces ||
		!IdentityEqual(&source->authority->identity, &capabilities->identity) ||
		capabilities->candidate_verifier_identity !=
			source->host_traces->candidate_verifier_identity ||
		capabilities->trace_verifier_identity !=
			source->host_traces->trace_verifier_identity)
	{
		result.code = source && capabilities && source->authority
			? SG_MECHANISM_CAPABILITY_AUDIT_IDENTITY_MISMATCH
			: SG_MECHANISM_CAPABILITY_AUDIT_INVALID_ARGUMENT;
		*result_out = result;
		return 0;
	}
	if (!IdentityValid(&capabilities->identity))
	{
		result.code = SG_MECHANISM_CAPABILITY_AUDIT_IDENTITY_MISMATCH;
		*result_out = result;
		return 0;
	}
	if (!CatalogEvidenceAuthentic(source))
	{
		result.code = SG_MECHANISM_CAPABILITY_AUDIT_FACT_DISAGREEMENT;
		*result_out = result;
		return 0;
	}
	if (capabilities->fact_count != source->host_traces->trace_count)
	{
		result.code = capabilities->fact_count < source->host_traces->trace_count
			? SG_MECHANISM_CAPABILITY_AUDIT_OMITTED_FACT
			: SG_MECHANISM_CAPABILITY_AUDIT_INVENTED_FACT;
		result.omitted_facts = source->host_traces->trace_count >
			capabilities->fact_count ? source->host_traces->trace_count -
			capabilities->fact_count : 0U;
		result.invented_facts = capabilities->fact_count >
			source->host_traces->trace_count ? capabilities->fact_count -
			source->host_traces->trace_count : 0U;
		*result_out = result;
		return 0;
	}
	if ((capabilities->fact_count && (!capabilities->facts ||
		 !capabilities->facts_by_trace)) ||
		(capabilities->topology_relation_count &&
		 !capabilities->topology_relations) ||
		capabilities->topology_relation_count > capabilities->fact_count ||
		capabilities->mechanism_offset_count !=
			source->entity_semantics->entity_count + 1U ||
		!capabilities->mechanism_offsets ||
		(capabilities->topology_edge_count && !capabilities->topology_edges))
	{
		result.code = SG_MECHANISM_CAPABILITY_AUDIT_INVALID_INDEX;
		*result_out = result;
		return 0;
	}
	for (trace = 0U; trace < capabilities->fact_count; trace++)
	{
		const sg_mechanism_capability_fact_t *fact =
			&capabilities->facts[trace];

		if (fact->order != trace ||
			fact->mechanism_entity >= source->entity_semantics->entity_count ||
			(trace != 0U && !FactsOrdered(&capabilities->facts[trace - 1U],
				fact)))
		{
			result.code =
				SG_MECHANISM_CAPABILITY_AUDIT_NONDETERMINISTIC_ORDER;
			result.record = trace;
			*result_out = result;
			return 0;
		}
	}
	for (trace = 0U;
		trace <= source->entity_semantics->entity_count; trace++)
	{
		while (cursor < capabilities->fact_count &&
			capabilities->facts[cursor].mechanism_entity < trace)
			cursor++;
		if (capabilities->mechanism_offsets[trace] != cursor)
		{
			result.code = SG_MECHANISM_CAPABILITY_AUDIT_INVALID_INDEX;
			result.record = trace;
			*result_out = result;
			return 0;
		}
	}
	seen = calloc(capabilities->fact_count, sizeof(*seen));
	relation_seen = calloc(capabilities->topology_relation_count,
		sizeof(*relation_seen));
	if (!seen || !relation_seen)
	{
		free(seen);
		free(relation_seen);
		*result_out = result;
		return 0;
	}
	for (trace = 0U; trace < capabilities->topology_relation_count; trace++)
	{
		const sg_mechanism_topology_relation_t *relation =
			&capabilities->topology_relations[trace];

		if ((trace != 0U &&
			 (capabilities->topology_relations[trace - 1U].controller_entity >
				relation->controller_entity ||
			  (capabilities->topology_relations[trace - 1U].controller_entity ==
				relation->controller_entity &&
			   capabilities->topology_relations[trace - 1U].mechanism_entity >=
				relation->mechanism_entity))) ||
			relation->controller_entity >=
				source->entity_semantics->entity_count ||
			relation->mechanism_entity >=
				source->entity_semantics->entity_count ||
			relation->first_edge != expected_topology_first ||
			relation->first_edge > capabilities->topology_edge_count ||
			relation->edge_count > capabilities->topology_edge_count -
				relation->first_edge ||
			!AuditTopologyRelation(source, capabilities, relation))
		{
			result.code =
				SG_MECHANISM_CAPABILITY_AUDIT_TOPOLOGY_DISAGREEMENT;
			result.record = trace;
			free(seen);
			free(relation_seen);
			*result_out = result;
			return 0;
		}
		expected_topology_first += relation->edge_count;
	}
	if (expected_topology_first != capabilities->topology_edge_count)
	{
		result.code = SG_MECHANISM_CAPABILITY_AUDIT_TOPOLOGY_DISAGREEMENT;
		result.record = expected_topology_first;
		free(seen);
		free(relation_seen);
		*result_out = result;
		return 0;
	}
	if (!TopologyAuthentic(source, capabilities))
	{
		result.code = SG_MECHANISM_CAPABILITY_AUDIT_TOPOLOGY_DISAGREEMENT;
		free(seen);
		free(relation_seen);
		*result_out = result;
		return 0;
	}
	for (trace = 0U; trace < source->host_traces->trace_count; trace++)
	{
		const sg_mechanism_host_trace_t *host =
			&source->host_traces->traces[trace];
		uint32_t fact_index = FindFactByTrace(capabilities,
			host->trace_identity, &result.lookup_comparisons);
		const sg_mechanism_capability_fact_t *fact;
		const sg_mechanism_topology_relation_t *relation;

		if (fact_index == SG_MECHANISM_CAPABILITY_INDEX_NONE)
		{
			result.code = SG_MECHANISM_CAPABILITY_AUDIT_OMITTED_FACT;
			result.record = trace;
			result.omitted_facts = 1U;
			free(seen);
			free(relation_seen);
			*result_out = result;
			return 0;
		}
		if (seen[fact_index])
		{
			result.code = SG_MECHANISM_CAPABILITY_AUDIT_INVENTED_FACT;
			result.record = fact_index;
			result.invented_facts = 1U;
			free(seen);
			free(relation_seen);
			*result_out = result;
			return 0;
		}
		seen[fact_index] = 1U;
		fact = &capabilities->facts[fact_index];
		if (fact->order != fact_index || !FactMatchesTrace(source, fact, host))
		{
			result.code = fact->order != fact_index
				? SG_MECHANISM_CAPABILITY_AUDIT_NONDETERMINISTIC_ORDER
				: SG_MECHANISM_CAPABILITY_AUDIT_FACT_DISAGREEMENT;
			result.record = fact_index;
			free(seen);
			free(relation_seen);
			*result_out = result;
			return 0;
		}
		relation = FindTopologyRelation(capabilities,
			fact->controller_entity, fact->mechanism_entity);
		if (!relation || fact->first_topology_edge != relation->first_edge ||
			fact->topology_edge_count != relation->edge_count)
		{
			result.code = SG_MECHANISM_CAPABILITY_AUDIT_TOPOLOGY_DISAGREEMENT;
			result.record = fact_index;
			free(seen);
			free(relation_seen);
			*result_out = result;
			return 0;
		}
		relation_seen[(uint32_t)(relation - capabilities->topology_relations)] = 1U;
		result.proved_facts++;
	}
	for (trace = 0U; trace < capabilities->fact_count; trace++)
		if (!seen[trace])
		{
			result.code = SG_MECHANISM_CAPABILITY_AUDIT_INVENTED_FACT;
			result.record = trace;
			result.invented_facts = 1U;
			free(seen);
			free(relation_seen);
			*result_out = result;
			return 0;
		}
	for (trace = 0U; trace < capabilities->topology_relation_count; trace++)
		if (!relation_seen[trace])
		{
			result.code =
				SG_MECHANISM_CAPABILITY_AUDIT_TOPOLOGY_DISAGREEMENT;
			result.record = trace;
			free(seen);
			free(relation_seen);
			*result_out = result;
			return 0;
		}
	free(seen);
	free(relation_seen);
	result.code = SG_MECHANISM_CAPABILITY_AUDIT_OK;
	*result_out = result;
	return 1;
}

void SG_MechanismCapabilityDestroy(
	sg_mechanism_capability_owner_t *owner,
	sg_mechanism_capability_set_t *capabilities)
{
	sg_mechanism_capability_record_t **link;
	sg_mechanism_capability_record_t *record;

	if (!owner || !capabilities)
		return;
	for (link = &owner->live; *link; link = &(*link)->next)
		if ((*link)->token == capabilities)
		{
			record = *link;
			*link = record->next;
			ReleaseCapabilityStorage(record->payload);
			free(record->payload);
			free(record);
			owner->live_count--;
			break;
		}
}

void SG_MechanismCapabilityOwnerDestroy(
	sg_mechanism_capability_owner_t *owner)
{
	sg_mechanism_capability_record_t *record;

	if (!owner)
		return;
	while (owner->live)
	{
		record = owner->live;
		owner->live = record->next;
		ReleaseCapabilityStorage(record->payload);
		free(record->payload);
		free(record);
	}
	free(owner);
}

const char *SG_MechanismCapabilityErrorString(
	sg_mechanism_capability_error_code_t code)
{
	static const char *const names[] = {
		"none", "invalid argument", "identity mismatch",
		"incomplete configuration", "invalid source", "invalid topology",
		"ambiguous topology", "invalid phase", "host disagreement",
		"timing", "overflow", "out of memory"
	};

	if (code < 0 || (size_t)code >= sizeof(names) / sizeof(names[0]))
		return "unknown";
	return names[code];
}

const char *SG_MechanismCapabilityAuditCodeString(
	sg_mechanism_capability_audit_code_t code)
{
	static const char *const names[] = {
		"ok", "invalid argument", "identity mismatch", "invalid index",
		"omitted fact", "invented fact", "fact disagreement",
		"topology disagreement", "nondeterministic order"
	};

	if (code < 0 || (size_t)code >= sizeof(names) / sizeof(names[0]))
		return "unknown";
	return names[code];
}
