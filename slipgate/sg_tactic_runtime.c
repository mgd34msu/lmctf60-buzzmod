#include "sg_tactic_runtime.h"
#include "sg_tactic_runtime_private.h"

#include "sg_authority_entropy.h"
#include "sg_rune_compact_field_service_private.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct sg_tactic_runtime_provider_s
{
	const sg_rune_compact_model_t *model;
	sg_rune_compact_field_service_t *field_service;
	sg_compact_localization_binding_t localization;
	uint64_t rune_identity;
	uint64_t topology_revision;
	uint64_t owner_epoch;
	uint8_t active;
	uint8_t busy;
} sg_tactic_runtime_provider_t;

typedef struct sg_tactic_runtime_transaction_s
	sg_tactic_runtime_transaction_t;

typedef struct sg_tactic_runtime_probe_slot_s
{
	sg_tactic_runtime_transaction_t *transaction;
	sg_tactic_candidate_t candidate;
	sg_rune_compact_field_exact_probe_t exact;
	uint32_t descriptor_flags;
	sg_tactic_phase_t source_phase;
	uint8_t probed;
} sg_tactic_runtime_probe_slot_t;

struct sg_tactic_runtime_transaction_s
{
	const sg_tactic_runtime_step_input_t *input;
	sg_tactic_frame_capability_t frame;
	sg_tactic_request_t request;
	sg_tactic_authority_t authority;
	sg_tactic_capability_descriptor_t *descriptors;
	sg_tactic_runtime_probe_slot_t *slots;
	uint32_t slot_count;
	uint32_t slot_capacity;
	uint32_t descriptor_count;
	uint32_t legal_mask;
	sg_tactic_phase_t source_phase;
	sg_host_hook_phase_t successor_hook;
	uint64_t token;
	uint8_t have_source_phase;
	uint8_t have_successor_hook;
	uint8_t ambiguous;
	uint8_t allocation_failed;
	uint8_t active;
};

static sg_tactic_runtime_provider_t tactic_provider;

static int LocalizationBindingSameOwner(
	const sg_compact_localization_binding_t *left,
	const sg_compact_localization_binding_t *right)
{
	return left != NULL && right != NULL &&
		SG_CompactLocalizationBindingCurrent(left) &&
		SG_CompactLocalizationBindingCurrent(right) &&
		left->model == right->model &&
		left->spatial_index == right->spatial_index &&
		left->observation_owner.context == right->observation_owner.context &&
		left->observation_owner.validate == right->observation_owner.validate &&
		left->host_authority.version == right->host_authority.version &&
		left->host_authority.epoch == right->host_authority.epoch &&
		left->host_authority.epoch_complement ==
			right->host_authority.epoch_complement &&
		left->rune_identity == right->rune_identity &&
		left->topology_revision == right->topology_revision &&
		left->bound == right->bound;
}

static int ProviderShapeCurrent(void)
{
	return tactic_provider.active == 1U && tactic_provider.model != NULL &&
		tactic_provider.field_service != NULL &&
		tactic_provider.rune_identity != 0U &&
		tactic_provider.topology_revision != 0U &&
		tactic_provider.owner_epoch != 0U &&
		SG_CompactLocalizationBindingCurrent(&tactic_provider.localization) &&
		tactic_provider.localization.model == tactic_provider.model &&
		tactic_provider.localization.rune_identity ==
			tactic_provider.rune_identity &&
		tactic_provider.localization.topology_revision ==
			tactic_provider.topology_revision &&
		SG_RuneCompactFieldServiceModel(tactic_provider.field_service) ==
			tactic_provider.model &&
		SG_RuneCompactFieldServiceIdentity(tactic_provider.field_service) != 0U &&
		SG_RuneCompactFieldServiceGeneration(tactic_provider.field_service) != 0U;
}

int SG_TacticRuntimeProviderInstall(const sg_rune_compact_model_t *model,
	sg_rune_compact_field_service_t *field_service,
	const sg_compact_localization_binding_t *localization,
	uint64_t rune_identity,
	uint64_t topology_revision)
{
	uint64_t epoch = 0U;

	if (model == NULL || field_service == NULL || localization == NULL ||
		rune_identity == 0U ||
		topology_revision == 0U || tactic_provider.active != 0U ||
		SG_RuneCompactFieldServiceModel(field_service) != model ||
		!SG_CompactLocalizationBindingCurrent(localization) ||
		localization->model != model ||
		localization->rune_identity != rune_identity ||
		localization->topology_revision != topology_revision ||
		!SG_AuthorityEntropyFill(&epoch, sizeof(epoch)) || epoch == 0U)
		return 0;
	memset(&tactic_provider, 0, sizeof(tactic_provider));
	tactic_provider.model = model;
	tactic_provider.field_service = field_service;
	tactic_provider.localization = *localization;
	tactic_provider.rune_identity = rune_identity;
	tactic_provider.topology_revision = topology_revision;
	tactic_provider.owner_epoch = epoch;
	tactic_provider.active = 1U;
	return ProviderShapeCurrent();
}

void SG_TacticRuntimeProviderClear(
	sg_rune_compact_field_service_t *field_service)
{
	if (field_service != NULL && tactic_provider.field_service == field_service)
		memset(&tactic_provider, 0, sizeof(tactic_provider));
}

int SG_TacticRuntimeProviderCurrent(
	const sg_rune_compact_field_service_t *field_service)
{
	return ProviderShapeCurrent() && field_service != NULL &&
		tactic_provider.field_service == field_service;
}

int SG_TacticRuntimeProviderSnapshotCurrent(
	const sg_tactic_runtime_provider_snapshot_t *snapshot)
{
	return snapshot != NULL && ProviderShapeCurrent() &&
		snapshot->model == tactic_provider.model &&
		snapshot->field_service == tactic_provider.field_service &&
		snapshot->rune_identity == tactic_provider.rune_identity &&
		snapshot->topology_revision == tactic_provider.topology_revision &&
		snapshot->owner_epoch == tactic_provider.owner_epoch &&
		LocalizationBindingSameOwner(&snapshot->localization,
			&tactic_provider.localization);
}

static int FloatBitsEqual(float left, float right)
{
	return memcmp(&left, &right, sizeof(left)) == 0;
}

static int SubjectEqual(const sg_localization_subject_t *left,
	const sg_localization_subject_t *right)
{
	return left != NULL && right != NULL &&
		left->client_id == right->client_id &&
		left->spawn_generation == right->spawn_generation &&
		left->reserved == right->reserved;
}

static int HandleEqual(const sg_rune_compact_field_handle_t *left,
	const sg_rune_compact_field_handle_t *right)
{
	return left != NULL && right != NULL &&
		left->service_identity == right->service_identity &&
		left->service_generation == right->service_generation &&
		left->rune_identity == right->rune_identity &&
		left->topology_revision == right->topology_revision &&
		left->target_id == right->target_id &&
		left->target_generation == right->target_generation &&
		left->field_generation == right->field_generation;
}

static int ContextMatchesLocalized(
	const sg_rune_compact_field_local_context_t *context,
	const sg_compact_localized_state_t *localized)
{
	sg_rune_movement_support_kind_t support;
	sg_rune_movement_water_kind_t water;
	sg_rune_movement_state_flags_t flags = 0U;
	uint32_t axis;

	if (context == NULL || localized == NULL || localized->valid != 1U ||
		localized->presence != SG_LOCALIZATION_PRESENCE_PRESENT ||
		localized->location.cell.value == SG_RUNE_COMPACT_INDEX_NONE ||
		localized->frame_sequence == 0U || localized->localized_at_ms == 0U ||
		context->frame_sequence != localized->frame_sequence ||
		context->stance != (localized->stance == SG_RUNE_STANCE_CROUCHING ?
			SG_RUNE_COMPACT_FIELD_CROUCHING :
			SG_RUNE_COMPACT_FIELD_STANDING) ||
		(uint32_t)context->hook_phase > (uint32_t)SG_HOST_HOOK_COAST)
		return 0;
	support = localized->support == SG_RUNE_SUPPORT_MOVER ?
		SG_RUNE_MOVEMENT_SUPPORT_MOVER :
		localized->support == SG_RUNE_SUPPORT_SUPPORTED ?
			SG_RUNE_MOVEMENT_SUPPORT_STATIC : SG_RUNE_MOVEMENT_SUPPORT_NONE;
	water = localized->water_level == 0U ? SG_RUNE_MOVEMENT_WATER_DRY :
		localized->water_level >= 3U ? SG_RUNE_MOVEMENT_WATER_SUBMERGED :
			SG_RUNE_MOVEMENT_WATER_PARTIAL;
	if (localized->motion == SG_RUNE_MOTION_AIRBORNE)
		flags |= SG_RUNE_MOVEMENT_STATE_AIRBORNE;
	if (localized->reference_frame == SG_RUNE_FRAME_MOVER_RELATIVE)
		flags |= SG_RUNE_MOVEMENT_STATE_MOVER_RELATIVE;
	if (context->support != support || context->water != water ||
		context->state_flags != flags ||
		((support == SG_RUNE_MOVEMENT_SUPPORT_MOVER) !=
		 (context->mover_mechanism != SG_RUNE_COMPACT_INDEX_NONE)))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
	{
		const double scaled = (double)localized->position[axis] * 8.0;
		long long rounded;

		if (!isfinite(scaled) || scaled < (double)INT32_MIN ||
			scaled > (double)INT32_MAX)
			return 0;
		rounded = llround(scaled);
		if (context->origin.value[axis] != (int32_t)rounded ||
			!FloatBitsEqual(context->velocity[axis], localized->velocity[axis]))
			return 0;
	}
	return 1;
}

static sg_tactic_phase_t PhaseForState(
	const sg_rune_compact_movement_state_t *state)
{
	if (state->water == SG_RUNE_MOVEMENT_WATER_SUBMERGED)
		return SG_TACTIC_PHASE_SWIM;
	if (state->hook_phase != SG_HOST_HOOK_IDLE)
		return SG_TACTIC_PHASE_HOOK;
	if (state->support == SG_RUNE_MOVEMENT_SUPPORT_MOVER ||
		(state->flags & SG_RUNE_MOVEMENT_STATE_MOVER_RELATIVE) != 0U)
		return SG_TACTIC_PHASE_MOVER;
	if ((state->flags & (SG_RUNE_MOVEMENT_STATE_AIRBORNE |
		SG_RUNE_MOVEMENT_STATE_FLAG_EXTERNAL_FORCE)) != 0U ||
		state->support == SG_RUNE_MOVEMENT_SUPPORT_NONE)
		return SG_TACTIC_PHASE_AIR;
	return state->stance == SG_RUNE_STANCE_VALID_CROUCHING ?
		SG_TACTIC_PHASE_CROUCH : SG_TACTIC_PHASE_GROUND;
}

static const sg_rune_compact_field_movement_probe_t *MovementForProbe(
	const sg_rune_compact_field_exact_probe_t *probe)
{
	if (probe == NULL)
		return NULL;
	switch (probe->provenance.kind)
	{
	case SG_RUNE_COMPACT_FIELD_PROBE_PMOVE:
		return &probe->provenance.value.pmove.movement;
	case SG_RUNE_COMPACT_FIELD_PROBE_HOOK:
		return &probe->provenance.value.hook.movement;
	case SG_RUNE_COMPACT_FIELD_PROBE_MECHANISM_TRANSITION:
		return &probe->provenance.value.mechanism.movement;
	case SG_RUNE_COMPACT_FIELD_PROBE_ANGULAR_MOVER:
		return &probe->provenance.value.angular_mover.movement;
	case SG_RUNE_COMPACT_FIELD_PROBE_INTRINSIC_STANCE:
	case SG_RUNE_COMPACT_FIELD_PROBE_PROVENANCE_KIND_COUNT:
	default:
		return NULL;
	}
}

static int CapabilityForMovementKind(
	sg_rune_movement_capability_kind_t movement_kind,
	sg_tactic_capability_t *capability_out)
{
	if (capability_out == NULL)
		return 0;
	switch (movement_kind)
	{
	case SG_RUNE_MOVEMENT_CAPABILITY_WALK:
	case SG_RUNE_MOVEMENT_CAPABILITY_RAMP:
		*capability_out = SG_TACTIC_CAPABILITY_WALK;
		break;
	case SG_RUNE_MOVEMENT_CAPABILITY_CROUCH:
		*capability_out = SG_TACTIC_CAPABILITY_CROUCH;
		break;
	case SG_RUNE_MOVEMENT_CAPABILITY_JUMP:
		*capability_out = SG_TACTIC_CAPABILITY_JUMP;
		break;
	case SG_RUNE_MOVEMENT_CAPABILITY_DROP:
		*capability_out = SG_TACTIC_CAPABILITY_DROP;
		break;
	case SG_RUNE_MOVEMENT_CAPABILITY_SWIM:
		*capability_out = SG_TACTIC_CAPABILITY_SWIM;
		break;
	case SG_RUNE_MOVEMENT_CAPABILITY_AIR_CONTROL:
		*capability_out = SG_TACTIC_CAPABILITY_AIR_CONTROL;
		break;
	case SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BOLT:
	case SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BODY:
	case SG_RUNE_MOVEMENT_CAPABILITY_HOOK_PULL:
	case SG_RUNE_MOVEMENT_CAPABILITY_HOOK_RELEASE:
	case SG_RUNE_MOVEMENT_CAPABILITY_HOOK_COAST:
	case SG_RUNE_MOVEMENT_CAPABILITY_HOOK_RELAUNCH:
		*capability_out = SG_TACTIC_CAPABILITY_HOOK;
		break;
	case SG_RUNE_MOVEMENT_CAPABILITY_MOVER:
		*capability_out = SG_TACTIC_CAPABILITY_MOVER;
		break;
	case SG_RUNE_MOVEMENT_CAPABILITY_EXTERNAL_FORCE:
		*capability_out = SG_TACTIC_CAPABILITY_PUSH;
		break;
	case SG_RUNE_MOVEMENT_CAPABILITY_CONTROLLER_ACTION:
	case SG_RUNE_MOVEMENT_CAPABILITY_KIND_COUNT:
	default:
		return 0;
	}
	return 1;
}

static int TacticCapabilityForProbe(
	const sg_rune_compact_field_exact_probe_t *probe,
	sg_tactic_capability_t *capability_out)
{
	const sg_rune_compact_field_movement_probe_t *movement;

	if (probe == NULL || capability_out == NULL)
		return 0;
	switch (probe->provenance.kind)
	{
	case SG_RUNE_COMPACT_FIELD_PROBE_INTRINSIC_STANCE:
		*capability_out = probe->successor_stance ==
			SG_RUNE_COMPACT_FIELD_CROUCHING ?
			SG_TACTIC_CAPABILITY_CROUCH : SG_TACTIC_CAPABILITY_WALK;
		return 1;
	case SG_RUNE_COMPACT_FIELD_PROBE_HOOK:
		movement = &probe->provenance.value.hook.movement;
		if (movement->movement_kind < SG_RUNE_MOVEMENT_CAPABILITY_HOOK_BOLT ||
			movement->movement_kind >
				SG_RUNE_MOVEMENT_CAPABILITY_HOOK_RELAUNCH)
			return 0;
		*capability_out = SG_TACTIC_CAPABILITY_HOOK;
		return 1;
	case SG_RUNE_COMPACT_FIELD_PROBE_MECHANISM_TRANSITION:
		switch (probe->provenance.value.mechanism.mechanism_kind)
		{
		case SG_RUNE_COMPACT_MECHANISM_TRANSITION_TELEPORT:
			*capability_out = SG_TACTIC_CAPABILITY_TELEPORT;
			return 1;
		case SG_RUNE_COMPACT_MECHANISM_TRANSITION_PUSH:
			*capability_out = SG_TACTIC_CAPABILITY_PUSH;
			return 1;
		case SG_RUNE_COMPACT_MECHANISM_TRANSITION_MOVER_TRANSPORT:
			*capability_out = SG_TACTIC_CAPABILITY_MOVER;
			return 1;
		case SG_RUNE_COMPACT_MECHANISM_TRANSITION_PORTAL_STATE:
		case SG_RUNE_COMPACT_MECHANISM_TRANSITION_KIND_COUNT:
		default:
			return 0;
		}
	case SG_RUNE_COMPACT_FIELD_PROBE_ANGULAR_MOVER:
		*capability_out = SG_TACTIC_CAPABILITY_MOVER;
		return 1;
	case SG_RUNE_COMPACT_FIELD_PROBE_PMOVE:
		movement = &probe->provenance.value.pmove.movement;
		return CapabilityForMovementKind(movement->movement_kind,
			capability_out);
	case SG_RUNE_COMPACT_FIELD_PROBE_PROVENANCE_KIND_COUNT:
	default:
		return 0;
	}
}

static void StateFromContext(
	const sg_rune_compact_field_local_context_t *context,
	sg_rune_compact_field_stance_t stance,
	sg_rune_compact_movement_state_t *state_out)
{
	memset(state_out, 0, sizeof(*state_out));
	state_out->stance = stance == SG_RUNE_COMPACT_FIELD_CROUCHING ?
		SG_RUNE_STANCE_VALID_CROUCHING : SG_RUNE_STANCE_VALID_STANDING;
	state_out->support = context->support;
	state_out->water = context->water;
	state_out->hook_phase = context->hook_phase;
	state_out->flags = context->state_flags;
	state_out->mover_mechanism = context->mover_mechanism;
}

static int ProbeStates(const sg_tactic_runtime_transaction_t *transaction,
	const sg_rune_compact_field_exact_probe_t *probe,
	sg_rune_compact_movement_state_t *source_out,
	sg_rune_compact_movement_state_t *destination_out)
{
	const sg_rune_compact_field_movement_probe_t *movement =
		MovementForProbe(probe);

	if (transaction == NULL || probe == NULL || source_out == NULL ||
		destination_out == NULL)
		return 0;
	if (movement != NULL)
	{
		*source_out = movement->source_state;
		*destination_out = movement->destination_state;
		return 1;
	}
	if (probe->provenance.kind !=
		SG_RUNE_COMPACT_FIELD_PROBE_INTRINSIC_STANCE)
		return 0;
	StateFromContext(transaction->input->local_context,
		probe->provenance.value.intrinsic_stance.source_stance, source_out);
	StateFromContext(transaction->input->local_context,
		probe->provenance.value.intrinsic_stance.destination_stance,
		destination_out);
	return 1;
}

static uint32_t DescriptorFlags(
	const sg_rune_compact_field_exact_probe_t *probe,
	const sg_rune_compact_movement_state_t *source_state)
{
	uint32_t flags = 0U;

	if (source_state->support != SG_RUNE_MOVEMENT_SUPPORT_NONE)
		flags |= SG_TACTIC_CAPABILITY_REQUIRES_SUPPORT;
	if (source_state->water == SG_RUNE_MOVEMENT_WATER_SUBMERGED)
		flags |= SG_TACTIC_CAPABILITY_REQUIRES_WATER;
	if (source_state->support == SG_RUNE_MOVEMENT_SUPPORT_NONE &&
		source_state->water != SG_RUNE_MOVEMENT_WATER_SUBMERGED)
		flags |= SG_TACTIC_CAPABILITY_REQUIRES_AIR;
	if (probe->provenance.kind == SG_RUNE_COMPACT_FIELD_PROBE_HOOK ||
		probe->provenance.kind ==
			SG_RUNE_COMPACT_FIELD_PROBE_MECHANISM_TRANSITION ||
		probe->provenance.kind == SG_RUNE_COMPACT_FIELD_PROBE_ANGULAR_MOVER)
		flags |= SG_TACTIC_CAPABILITY_REQUIRES_LIVE_TRACE;
	return flags;
}

static int CandidateEqual(const sg_tactic_candidate_t *left,
	const sg_tactic_candidate_t *right)
{
	uint32_t axis;

	if (left == NULL || right == NULL ||
		left->capability != right->capability ||
		!SG_TacticSuccessorStateEqual(&left->successor, &right->successor) ||
		left->predicted_phase != right->predicted_phase ||
		left->local_cost.units != right->local_cost.units ||
		!FloatBitsEqual(left->duration_seconds, right->duration_seconds) ||
		left->exact_live_validation_required !=
			right->exact_live_validation_required)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (!FloatBitsEqual(left->displacement[axis],
				right->displacement[axis]) ||
			!FloatBitsEqual(left->velocity_delta[axis],
				right->velocity_delta[axis]))
			return 0;
	/* Keep this comparison explicit: candidate padding is not authority. */
	return 1;
}

static int MovementStateEqual(
	const sg_rune_compact_movement_state_t *left,
	const sg_rune_compact_movement_state_t *right)
{
	return left != NULL && right != NULL && left->stance == right->stance &&
		left->support == right->support && left->water == right->water &&
		left->hook_phase == right->hook_phase && left->flags == right->flags &&
		left->mover_mechanism == right->mover_mechanism;
}

static int MovementProbeEqual(
	const sg_rune_compact_field_movement_probe_t *left,
	const sg_rune_compact_field_movement_probe_t *right)
{
	return left != NULL && right != NULL &&
		left->field_arc == right->field_arc &&
		left->capability.value == right->capability.value &&
		left->fiber.value == right->fiber.value &&
		left->movement_kind == right->movement_kind &&
		MovementStateEqual(&left->source_state, &right->source_state) &&
		MovementStateEqual(&left->destination_state,
			&right->destination_state);
}

static int ExactProbeEqual(
	const sg_rune_compact_field_exact_probe_t *left,
	const sg_rune_compact_field_exact_probe_t *right)
{
	if (left == NULL || right == NULL ||
		left->transition_kind != right->transition_kind ||
		left->successor_cell.value != right->successor_cell.value ||
		left->portal.value != right->portal.value ||
		left->successor_stance != right->successor_stance ||
		left->local_cost.units != right->local_cost.units ||
		!FloatBitsEqual(left->travel_time_seconds,
			right->travel_time_seconds) ||
		left->provenance.kind != right->provenance.kind)
		return 0;
	switch (left->provenance.kind)
	{
	case SG_RUNE_COMPACT_FIELD_PROBE_INTRINSIC_STANCE:
		return left->provenance.value.intrinsic_stance.cell.value ==
				right->provenance.value.intrinsic_stance.cell.value &&
			left->provenance.value.intrinsic_stance.source_stance ==
				right->provenance.value.intrinsic_stance.source_stance &&
			left->provenance.value.intrinsic_stance.destination_stance ==
				right->provenance.value.intrinsic_stance.destination_stance &&
			left->provenance.value.intrinsic_stance.frame_ms ==
				right->provenance.value.intrinsic_stance.frame_ms;
	case SG_RUNE_COMPACT_FIELD_PROBE_PMOVE:
		return MovementProbeEqual(&left->provenance.value.pmove.movement,
			&right->provenance.value.pmove.movement);
	case SG_RUNE_COMPACT_FIELD_PROBE_HOOK:
		return MovementProbeEqual(&left->provenance.value.hook.movement,
			&right->provenance.value.hook.movement) &&
			left->provenance.value.hook.hook_target ==
				right->provenance.value.hook.hook_target;
	case SG_RUNE_COMPACT_FIELD_PROBE_MECHANISM_TRANSITION:
		return MovementProbeEqual(
				&left->provenance.value.mechanism.movement,
				&right->provenance.value.mechanism.movement) &&
			left->provenance.value.mechanism.mechanism_transition.value ==
				right->provenance.value.mechanism.mechanism_transition.value &&
			left->provenance.value.mechanism.controller.value ==
				right->provenance.value.mechanism.controller.value &&
			left->provenance.value.mechanism.controller_target.value ==
				right->provenance.value.mechanism.controller_target.value &&
			left->provenance.value.mechanism.mechanism_kind ==
				right->provenance.value.mechanism.mechanism_kind;
	case SG_RUNE_COMPACT_FIELD_PROBE_ANGULAR_MOVER:
		return MovementProbeEqual(
				&left->provenance.value.angular_mover.movement,
				&right->provenance.value.angular_mover.movement) &&
			left->provenance.value.angular_mover.angular_schedule ==
				right->provenance.value.angular_mover.angular_schedule;
	case SG_RUNE_COMPACT_FIELD_PROBE_PROVENANCE_KIND_COUNT:
	default:
		return 0;
	}
}

static int CandidateFromExact(
	const sg_tactic_runtime_transaction_t *transaction,
	const sg_rune_compact_field_exact_probe_t *probe,
	sg_tactic_candidate_t *candidate_out,
	sg_tactic_phase_t *source_phase_out)
{
	sg_tactic_candidate_t candidate;
	sg_tactic_capability_t capability;
	sg_rune_compact_movement_state_t source_state;
	sg_rune_compact_movement_state_t destination_state;

	if (transaction == NULL || probe == NULL || candidate_out == NULL ||
		source_phase_out == NULL ||
		!TacticCapabilityForProbe(probe, &capability) ||
		!ProbeStates(transaction, probe, &source_state, &destination_state))
		return 0;
	memset(&candidate, 0, sizeof(candidate));
	candidate.capability = capability;
	candidate.successor.cell = probe->successor_cell;
	candidate.successor.stance = probe->successor_stance;
	candidate.successor.hook_phase = destination_state.hook_phase;
	candidate.predicted_phase = PhaseForState(&destination_state);
	candidate.duration_seconds = probe->travel_time_seconds;
	candidate.local_cost = probe->local_cost;
	candidate.exact_live_validation_required = (uint8_t)(
		probe->provenance.kind == SG_RUNE_COMPACT_FIELD_PROBE_HOOK ||
		probe->provenance.kind ==
			SG_RUNE_COMPACT_FIELD_PROBE_MECHANISM_TRANSITION ||
		probe->provenance.kind == SG_RUNE_COMPACT_FIELD_PROBE_ANGULAR_MOVER);
	if (!SG_TacticCandidateValid(&candidate))
		return 0;
	*candidate_out = candidate;
	*source_phase_out = PhaseForState(&source_state);
	return 1;
}

static int RuntimeProbeVisit(void *context,
	const sg_rune_compact_field_exact_probe_t *probe)
{
	sg_tactic_runtime_transaction_t *transaction = context;
	sg_tactic_candidate_t candidate;
	sg_rune_compact_movement_state_t source_state;
	sg_rune_compact_movement_state_t unused_destination_state;
	sg_tactic_phase_t source_phase;
	sg_tactic_runtime_probe_slot_t *slot;
	uint32_t needed_capacity;
	sg_tactic_runtime_probe_slot_t *grown_slots;

	if (transaction == NULL || probe == NULL ||
		!CandidateFromExact(transaction, probe, &candidate, &source_phase) ||
		!ProbeStates(transaction, probe, &source_state,
			&unused_destination_state))
		return 1;
	if (transaction->have_source_phase != 0U &&
		transaction->source_phase != source_phase)
	{
		transaction->ambiguous = 1U;
		return 1;
	}
	transaction->source_phase = source_phase;
	transaction->have_source_phase = 1U;
	if (transaction->have_successor_hook != 0U &&
		transaction->successor_hook != candidate.successor.hook_phase)
	{
		transaction->ambiguous = 1U;
		return 1;
	}
	transaction->successor_hook = candidate.successor.hook_phase;
	transaction->have_successor_hook = 1U;
	if (transaction->slot_count == UINT32_MAX)
	{
		transaction->allocation_failed = 1U;
		return 0;
	}
	if (transaction->slot_count == transaction->slot_capacity)
	{
		needed_capacity = transaction->slot_capacity == 0U ? 8U :
			transaction->slot_capacity > UINT32_MAX / 2U ? UINT32_MAX :
			transaction->slot_capacity * 2U;
		if (needed_capacity == 0U ||
			needed_capacity <= transaction->slot_capacity ||
			sizeof(*transaction->slots) >
				SIZE_MAX / (size_t)needed_capacity)
		{
			transaction->allocation_failed = 1U;
			return 0;
		}
		grown_slots = realloc(transaction->slots,
			(size_t)needed_capacity * sizeof(*transaction->slots));
		if (grown_slots == NULL)
		{
			transaction->allocation_failed = 1U;
			return 0;
		}
		transaction->slots = grown_slots;
		transaction->slot_capacity = needed_capacity;
	}
	slot = &transaction->slots[transaction->slot_count++];
	memset(slot, 0, sizeof(*slot));
	slot->transaction = transaction;
	slot->candidate = candidate;
	slot->exact = *probe;
	slot->descriptor_flags = DescriptorFlags(probe, &source_state);
	slot->source_phase = source_phase;
	return 1;
}

static int RuntimeProbe(void *context, const sg_tactic_request_t *request,
	sg_tactic_candidate_t *candidate_out)
{
	sg_tactic_runtime_probe_slot_t *slot = context;

	if (slot == NULL || slot->transaction == NULL || candidate_out == NULL ||
		slot->transaction->active != 1U ||
		request != &slot->transaction->request || !ProviderShapeCurrent() ||
		request->frame != &slot->transaction->frame ||
		request->frame->token != slot->transaction->token)
		return 0;
	slot->probed = 1U;
	*candidate_out = slot->candidate;
	return 1;
}

static int RuntimeValidateFrame(const void *context,
	const sg_tactic_request_t *request,
	const sg_tactic_frame_capability_t *frame)
{
	const sg_tactic_runtime_transaction_t *transaction = context;
	const sg_tactic_runtime_step_input_t *input;

	if (transaction == NULL || transaction->active != 1U ||
		request != &transaction->request || frame != &transaction->frame ||
		frame->owner_epoch != tactic_provider.owner_epoch ||
		frame->token != transaction->token || !ProviderShapeCurrent())
		return 0;
	input = transaction->input;
	return input != NULL && SubjectEqual(&frame->subject,
		&input->localized->subject) &&
		frame->model_identity == input->localized->model_stamp.identity &&
		frame->rune_identity == input->localized->rune_identity &&
		frame->topology_revision == input->localized->topology_revision &&
		HandleEqual(&frame->field_handle,
			&input->strategy_output->field_handle) &&
		frame->frame_sequence == input->localized->frame_sequence &&
		frame->observed_at_ms == input->localized->localized_at_ms &&
		frame->localized.cell.value == input->localized->location.cell.value &&
		frame->localized.stance == input->local_context->stance &&
		frame->localized.hook_phase == input->local_context->hook_phase;
}

static int RuntimeValidateProbe(const void *context,
	const sg_tactic_request_t *request,
	const sg_tactic_capability_descriptor_t *descriptor,
	const sg_tactic_candidate_t *candidate,
	sg_rune_compact_field_cost_t nominal_cost)
{
	const sg_tactic_runtime_transaction_t *transaction = context;
	const sg_tactic_runtime_probe_slot_t *slot;
	uint64_t expected;
	uint32_t slot_index;
	int slot_owned = 0;

	if (transaction == NULL || request != &transaction->request ||
		descriptor == NULL || candidate == NULL || descriptor->context == NULL ||
		transaction->active != 1U || !ProviderShapeCurrent())
		return 0;
	slot = descriptor->context;
	for (slot_index = 0U; slot_index < transaction->slot_count; slot_index++)
		if (slot == &transaction->slots[slot_index])
		{
			slot_owned = 1;
			break;
		}
	if (!slot_owned || slot->transaction != transaction || slot->probed != 1U ||
		descriptor->capability != slot->candidate.capability ||
		!CandidateEqual(candidate, &slot->candidate) ||
		slot->candidate.local_cost.units > UINT64_MAX -
			request->gradient.transition.v12.next_cost_to_go.units)
		return 0;
	expected = request->gradient.transition.v12.next_cost_to_go.units +
		slot->candidate.local_cost.units;
	return nominal_cost.units == expected &&
		expected < request->gradient.transition.v12.cost_to_go.units;
}

static int InputBaseCurrent(const sg_tactic_runtime_step_input_t *input)
{
	const sg_strategy_caller_output_t *output;
	const sg_compact_localized_state_t *localized;

	if (input == NULL || input->model == NULL ||
		input->strategy_caller == NULL || input->strategy_proof == NULL ||
		input->query_proof == NULL ||
		input->strategy_output == NULL || input->localized == NULL ||
		input->local_context == NULL || input->field_result == NULL ||
		!ProviderShapeCurrent() || input->model != tactic_provider.model)
		return 0;
	output = input->strategy_output;
	localized = input->localized;
	return output->instruction.kind == SG_STRATEGY_INSTRUCTION_EXECUTE &&
		SG_CompactLocalizationStateCurrent(&tactic_provider.localization,
			&localized->subject, localized) &&
		input->field_result->kind == SG_RUNE_COMPACT_FIELD_STEP &&
		output->field_service == tactic_provider.field_service &&
		output->commitment_id != 0U && output->plan_id != 0U &&
		output->instruction.plan_id == output->plan_id &&
		output->frame_sequence == localized->frame_sequence &&
		output->observed_at_ms == localized->localized_at_ms &&
		output->life_identity.client_id == localized->subject.client_id &&
		output->life_identity.reserved == localized->subject.reserved &&
		output->life_identity.spawn_generation ==
			localized->subject.spawn_generation &&
		localized->model_stamp.identity == tactic_provider.rune_identity &&
		localized->model_stamp.generation == tactic_provider.topology_revision &&
		localized->model_stamp.frame_sequence == localized->frame_sequence &&
		localized->rune_identity == tactic_provider.rune_identity &&
		localized->topology_revision == tactic_provider.topology_revision &&
		output->field_handle.rune_identity == tactic_provider.rune_identity &&
		output->field_handle.topology_revision ==
			tactic_provider.topology_revision &&
		output->field_handle.target_id == output->compact_target.target_id &&
		output->field_handle.target_generation ==
			output->compact_target.target_generation &&
		input->field_result->current_cell.value ==
			localized->location.cell.value &&
		ContextMatchesLocalized(input->local_context, localized) &&
		SG_RuneCompactFieldServiceHandleCurrent(output->field_service,
			&output->field_handle, NULL, NULL, NULL);
}

static int InputCurrent(const sg_tactic_runtime_step_input_t *input,
	sg_strategy_runtime_caller_query_snapshot_t *query_snapshot_out)
{
	return query_snapshot_out != NULL && InputBaseCurrent(input) &&
		SG_StrategyCallerOutputProofCurrent(input->strategy_caller,
			input->strategy_output, input->strategy_proof) &&
		SG_StrategyRuntimeCallerQueryProofCurrent(input->strategy_caller,
			input->strategy_output, input->strategy_proof,
			input->local_context, input->field_result, input->query_proof,
			query_snapshot_out);
}

static void BuildLiveAndGradient(sg_tactic_runtime_transaction_t *transaction)
{
	const sg_tactic_runtime_step_input_t *input = transaction->input;
	const sg_compact_localized_state_t *localized = input->localized;
	const sg_rune_compact_field_local_context_t *context = input->local_context;
	const sg_rune_compact_field_step_t *step = &input->field_result->value.step;
	uint32_t axis;

	transaction->request.live.rune_identity = localized->rune_identity;
	transaction->request.live.pose_revision = localized->frame_sequence;
	transaction->request.live.now_ms = localized->localized_at_ms;
	transaction->request.live.cell = localized->location.cell;
	transaction->request.live.stance = localized->stance ==
		SG_RUNE_STANCE_CROUCHING ? SG_RUNE_STANCE_VALID_CROUCHING :
		SG_RUNE_STANCE_VALID_STANDING;
	transaction->request.live.supported = (uint8_t)(
		context->support == SG_RUNE_MOVEMENT_SUPPORT_NONE ? 0 : 1);
	transaction->request.live.waterlevel = localized->water_level;
	transaction->request.live.hook_phase = context->hook_phase;
	for (axis = 0U; axis < 3U; axis++)
	{
		transaction->request.live.origin[axis] = localized->position[axis];
		transaction->request.live.velocity[axis] = localized->velocity[axis];
	}
	transaction->request.gradient.field_generation =
		input->strategy_output->field_handle.field_generation;
	transaction->request.gradient.pose_revision = localized->frame_sequence;
	transaction->request.gradient.sampled_at_ms = localized->localized_at_ms;
	transaction->request.gradient.current_cell = localized->location.cell;
	transaction->request.gradient.transition.v12 = *step;
	transaction->request.gradient.transition.target_hook_phase =
		transaction->successor_hook;
}

static int BuildDescriptors(sg_tactic_runtime_transaction_t *transaction)
{
	uint32_t index;

	if (transaction == NULL || transaction->slot_count == 0U ||
		(sizeof(*transaction->descriptors) > SIZE_MAX /
			(size_t)transaction->slot_count))
		return 0;
	transaction->descriptors = calloc((size_t)transaction->slot_count,
		sizeof(*transaction->descriptors));
	if (transaction->descriptors == NULL)
	{
		transaction->allocation_failed = 1U;
		return 0;
	}
	for (index = 0U; index < transaction->slot_count; index++)
	{
		sg_tactic_runtime_probe_slot_t *slot = &transaction->slots[index];
		sg_tactic_capability_descriptor_t *descriptor;

		descriptor = &transaction->descriptors[transaction->descriptor_count++];
		descriptor->capability = slot->candidate.capability;
		descriptor->phase_mask = SG_TACTIC_PHASE_BIT(slot->source_phase);
		descriptor->flags = slot->descriptor_flags;
		descriptor->probe = RuntimeProbe;
		descriptor->context = slot;
		transaction->legal_mask |= SG_TACTIC_CAPABILITY_BIT(
			slot->candidate.capability);
	}
	return transaction->descriptor_count != 0U;
}

static int CandidateMatchesResult(const sg_tactic_request_t *request,
	const sg_tactic_candidate_t *candidate, const sg_tactic_result_t *result)
{
	uint64_t nominal;

	if (request == NULL || candidate == NULL || result == NULL ||
		result->status != SG_TACTIC_RESULT_PROGRESS ||
		candidate->capability != result->capability ||
		!SG_TacticSuccessorStateEqual(&candidate->successor,
			&result->successor) ||
		candidate->predicted_phase != result->target_phase ||
		candidate->exact_live_validation_required !=
			result->exact_live_validation_required ||
		candidate->local_cost.units > UINT64_MAX -
			request->gradient.transition.v12.next_cost_to_go.units)
		return 0;
	nominal = candidate->local_cost.units +
		request->gradient.transition.v12.next_cost_to_go.units;
	return nominal == result->nominal_cost.units;
}

static sg_tactic_runtime_probe_slot_t *UniqueSelectedSlot(
	sg_tactic_runtime_transaction_t *transaction,
	const sg_tactic_result_t *result)
{
	sg_tactic_runtime_probe_slot_t *winner = NULL;
	uint32_t index;

	for (index = 0U; index < transaction->slot_count; index++)
	{
		sg_tactic_runtime_probe_slot_t *slot = &transaction->slots[index];

		if (slot->probed == 0U || !CandidateMatchesResult(
			&transaction->request, &slot->candidate, result))
			continue;
		if (winner != NULL)
			return NULL;
		winner = slot;
	}
	return winner;
}

static sg_tactic_runtime_status_t PrepareStepWithToken(
	const sg_tactic_runtime_step_input_t *input,
	sg_tactic_runtime_prepared_step_t *prepared_out,
	uint64_t supplied_token, int mint_token, int require_pending_proof)
{
	sg_tactic_runtime_transaction_t transaction;
	sg_tactic_result_t selection;
	sg_tactic_runtime_probe_slot_t *winner;
	uint32_t probe_count = 0U;
	sg_rune_compact_field_service_status_t field_status;
	sg_strategy_runtime_caller_query_snapshot_t query_snapshot;
	sg_tactic_runtime_status_t status = SG_TACTIC_RUNTIME_NOT_CURRENT;

	if (prepared_out != NULL)
		memset(prepared_out, 0, sizeof(*prepared_out));
	if (prepared_out == NULL || input == NULL)
		return SG_TACTIC_RUNTIME_INVALID_ARGUMENT;
	if (tactic_provider.busy != 0U)
		return SG_TACTIC_RUNTIME_NOT_CURRENT;
	memset(&query_snapshot, 0, sizeof(query_snapshot));
	if (require_pending_proof != 0 ?
		!InputCurrent(input, &query_snapshot) : !InputBaseCurrent(input))
		return SG_TACTIC_RUNTIME_STALE_FRAME;
	memset(&transaction, 0, sizeof(transaction));
	transaction.input = input;
	tactic_provider.busy = 1U;
	field_status = SG_RuneCompactFieldServiceVisitExactStepProbes(
		input->strategy_output->field_service,
		&input->strategy_output->field_handle, input->local_context,
		input->field_result, RuntimeProbeVisit, &transaction, &probe_count);
	if (field_status != SG_RUNE_COMPACT_FIELD_SERVICE_OK)
	{
		status = transaction.allocation_failed != 0U ?
			SG_TACTIC_RUNTIME_NOT_CURRENT : SG_TACTIC_RUNTIME_FIELD_REJECTED;
		goto cleanup;
	}
	if (probe_count == 0U || !BuildDescriptors(&transaction))
	{
		status = transaction.allocation_failed != 0U ?
			SG_TACTIC_RUNTIME_NOT_CURRENT :
			SG_TACTIC_RUNTIME_NO_LEGAL_CAPABILITY;
		goto cleanup;
	}
	if (transaction.ambiguous != 0U ||
		transaction.have_source_phase == 0U ||
		transaction.have_successor_hook == 0U)
	{
		status = SG_TACTIC_RUNTIME_AMBIGUOUS_SUCCESSOR;
		goto cleanup;
	}
	if (mint_token != 0)
	{
		if (!SG_AuthorityEntropyFill(&transaction.token,
				sizeof(transaction.token)) || transaction.token == 0U)
			goto cleanup;
	}
	else
	{
		if (supplied_token == 0U)
			goto cleanup;
		transaction.token = supplied_token;
	}
	BuildLiveAndGradient(&transaction);
	transaction.request.live.phase = transaction.source_phase;
	transaction.frame.subject = input->localized->subject;
	transaction.frame.model_identity = input->localized->model_stamp.identity;
	transaction.frame.rune_identity = input->localized->rune_identity;
	transaction.frame.topology_revision = input->localized->topology_revision;
	transaction.frame.field_handle = input->strategy_output->field_handle;
	transaction.frame.frame_sequence = input->localized->frame_sequence;
	transaction.frame.observed_at_ms = input->localized->localized_at_ms;
	transaction.frame.localized.cell = input->localized->location.cell;
	transaction.frame.localized.stance = input->local_context->stance;
	transaction.frame.localized.hook_phase = input->local_context->hook_phase;
	transaction.frame.owner_epoch = tactic_provider.owner_epoch;
	transaction.frame.token = transaction.token;
	transaction.authority.context = &transaction;
	transaction.authority.validate_frame = RuntimeValidateFrame;
	transaction.authority.validate_probe = RuntimeValidateProbe;
	transaction.request.frame = &transaction.frame;
	transaction.request.legal_capability_mask = transaction.legal_mask;
	transaction.request.authority = &transaction.authority;
	transaction.active = 1U;
	if (!SG_TacticSelectCapability(&transaction.request,
		transaction.descriptors, transaction.descriptor_count, &selection))
	{
		transaction.active = 0U;
		status = SG_TACTIC_RUNTIME_PROBE_REJECTED;
		goto cleanup;
	}
	transaction.active = 0U;
	winner = UniqueSelectedSlot(&transaction, &selection);
	if (winner == NULL)
	{
		status = SG_TACTIC_RUNTIME_AMBIGUOUS_SUCCESSOR;
		goto cleanup;
	}
	prepared_out->result = selection;
	prepared_out->frame = transaction.frame;
	prepared_out->candidate = winner->candidate;
	prepared_out->exact_probe = winner->exact;
	prepared_out->provider.model = tactic_provider.model;
	prepared_out->provider.field_service = tactic_provider.field_service;
	prepared_out->provider.rune_identity = tactic_provider.rune_identity;
	prepared_out->provider.topology_revision =
		tactic_provider.topology_revision;
	prepared_out->provider.owner_epoch = tactic_provider.owner_epoch;
	prepared_out->provider.localization = tactic_provider.localization;
	prepared_out->strategy_caller = input->strategy_caller;
	prepared_out->strategy_output = *input->strategy_output;
	prepared_out->strategy_proof = *input->strategy_proof;
	prepared_out->query_proof = *input->query_proof;
	prepared_out->query_snapshot = query_snapshot;
	prepared_out->localized = *input->localized;
	prepared_out->local_context = *input->local_context;
	prepared_out->field_result = *input->field_result;
	status = SG_TACTIC_RUNTIME_OK;

cleanup:
	transaction.active = 0U;
	free(transaction.descriptors);
	free(transaction.slots);
	tactic_provider.busy = 0U;
	if (status != SG_TACTIC_RUNTIME_OK)
		memset(prepared_out, 0, sizeof(*prepared_out));
	return status;
}

sg_tactic_runtime_status_t SG_TacticRuntimePrepareStep(
	const sg_tactic_runtime_step_input_t *input,
	sg_tactic_runtime_prepared_step_t *prepared_out)
{
	return PrepareStepWithToken(input, prepared_out, 0U, 1, 1);
}

static void PreparedInput(const sg_tactic_runtime_prepared_step_t *prepared,
	sg_tactic_runtime_step_input_t *input_out)
{
	memset(input_out, 0, sizeof(*input_out));
	input_out->model = prepared->provider.model;
	input_out->strategy_caller = prepared->strategy_caller;
	input_out->strategy_output = &prepared->strategy_output;
	input_out->strategy_proof = &prepared->strategy_proof;
	input_out->query_proof = &prepared->query_proof;
	input_out->localized = &prepared->localized;
	input_out->local_context = &prepared->local_context;
	input_out->field_result = &prepared->field_result;
}

static int ProviderSnapshotEqual(
	const sg_tactic_runtime_provider_snapshot_t *left,
	const sg_tactic_runtime_provider_snapshot_t *right)
{
	return left != NULL && right != NULL && left->model == right->model &&
		left->field_service == right->field_service &&
		left->rune_identity == right->rune_identity &&
		left->topology_revision == right->topology_revision &&
		left->owner_epoch == right->owner_epoch &&
		LocalizationBindingSameOwner(&left->localization,
			&right->localization);
}

static int FrameEqual(const sg_tactic_frame_capability_t *left,
	const sg_tactic_frame_capability_t *right)
{
	return left != NULL && right != NULL &&
		SubjectEqual(&left->subject, &right->subject) &&
		left->model_identity == right->model_identity &&
		left->rune_identity == right->rune_identity &&
		left->topology_revision == right->topology_revision &&
		HandleEqual(&left->field_handle, &right->field_handle) &&
		left->frame_sequence == right->frame_sequence &&
		left->observed_at_ms == right->observed_at_ms &&
		SG_TacticSuccessorStateEqual(&left->localized, &right->localized) &&
		left->owner_epoch == right->owner_epoch && left->token == right->token;
}

static int ResultEqual(const sg_tactic_result_t *left,
	const sg_tactic_result_t *right)
{
	return left != NULL && right != NULL && left->status == right->status &&
		left->failure == right->failure &&
		left->capability == right->capability &&
		SG_TacticSuccessorStateEqual(&left->successor, &right->successor) &&
		left->target_phase == right->target_phase &&
		left->nominal_cost.units == right->nominal_cost.units &&
		FloatBitsEqual(left->progress, right->progress) &&
		left->exact_live_validation_required ==
			right->exact_live_validation_required &&
		left->mechanism_handoff_valid == right->mechanism_handoff_valid;
}

static int QuerySnapshotEqual(
	const sg_strategy_runtime_caller_query_snapshot_t *left,
	const sg_strategy_runtime_caller_query_snapshot_t *right)
{
	uint32_t axis;

	if (left == NULL || right == NULL || left->stance != right->stance ||
		left->support != right->support || left->water != right->water ||
		left->hook_phase != right->hook_phase ||
		left->state_flags != right->state_flags ||
		left->mover_mechanism != right->mover_mechanism ||
		!FloatBitsEqual(left->time_seconds, right->time_seconds) ||
		!FloatBitsEqual(left->distance, right->distance) ||
		!FloatBitsEqual(left->support_distance, right->support_distance) ||
		!FloatBitsEqual(left->fluid_fraction, right->fluid_fraction) ||
		!FloatBitsEqual(left->hook_length, right->hook_length) ||
		!FloatBitsEqual(left->target_radius, right->target_radius) ||
		left->frame_sequence != right->frame_sequence ||
		left->mechanism_digest[0] != right->mechanism_digest[0] ||
		left->mechanism_digest[1] != right->mechanism_digest[1] ||
		left->portal_root_digest[0] != right->portal_root_digest[0] ||
		left->portal_root_digest[1] != right->portal_root_digest[1])
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (left->origin.value[axis] != right->origin.value[axis] ||
			!FloatBitsEqual(left->velocity[axis], right->velocity[axis]) ||
			!FloatBitsEqual(left->direction[axis], right->direction[axis]))
			return 0;
	return 1;
}

static int PreparedTentativeEqual(
	const sg_tactic_runtime_prepared_step_t *left,
	const sg_tactic_runtime_prepared_step_t *right)
{
	return left != NULL && right != NULL &&
		left->strategy_caller == right->strategy_caller &&
		ResultEqual(&left->result, &right->result) &&
		FrameEqual(&left->frame, &right->frame) &&
		CandidateEqual(&left->candidate, &right->candidate) &&
		ExactProbeEqual(&left->exact_probe, &right->exact_probe) &&
		ProviderSnapshotEqual(&left->provider, &right->provider) &&
		memcmp(&left->strategy_proof, &right->strategy_proof,
			sizeof(left->strategy_proof)) == 0 &&
		memcmp(&left->query_proof, &right->query_proof,
			sizeof(left->query_proof)) == 0 &&
		QuerySnapshotEqual(&left->query_snapshot, &right->query_snapshot);
}

static int PreparedFrameValid(
	const sg_tactic_runtime_prepared_step_t *prepared)
{
	sg_tactic_frame_capability_t expected;

	memset(&expected, 0, sizeof(expected));
	expected.subject = prepared->localized.subject;
	expected.model_identity = prepared->localized.model_stamp.identity;
	expected.rune_identity = prepared->localized.rune_identity;
	expected.topology_revision = prepared->localized.topology_revision;
	expected.field_handle = prepared->strategy_output.field_handle;
	expected.frame_sequence = prepared->localized.frame_sequence;
	expected.observed_at_ms = prepared->localized.localized_at_ms;
	expected.localized.cell = prepared->localized.location.cell;
	expected.localized.stance = prepared->local_context.stance;
	expected.localized.hook_phase = prepared->local_context.hook_phase;
	expected.owner_epoch = prepared->provider.owner_epoch;
	expected.token = prepared->frame.token;
	return SG_TacticFrameCapabilityValid(&prepared->frame) &&
		FrameEqual(&expected, &prepared->frame);
}

static int PreparedSelectionValid(
	const sg_tactic_runtime_prepared_step_t *prepared,
	const sg_tactic_runtime_step_input_t *input)
{
	sg_tactic_runtime_transaction_t transaction;
	sg_tactic_candidate_t candidate;
	sg_tactic_phase_t source_phase;
	sg_tactic_transition_t transition;
	sg_tactic_successor_state_t successor;
	sg_tactic_result_t expected;
	sg_rune_compact_field_cost_t nominal;
	uint64_t improvement;

	if (prepared == NULL || input == NULL ||
		prepared->field_result.kind != SG_RUNE_COMPACT_FIELD_STEP)
		return 0;
	memset(&transaction, 0, sizeof(transaction));
	transaction.input = input;
	if (!CandidateFromExact(&transaction, &prepared->exact_probe,
			&candidate, &source_phase) ||
		!CandidateEqual(&candidate, &prepared->candidate))
		return 0;
	(void)source_phase;
	memset(&transition, 0, sizeof(transition));
	transition.v12 = prepared->field_result.value.step;
	transition.target_hook_phase = candidate.successor.hook_phase;
	if (!SG_TacticTransitionValid(prepared->field_result.current_cell,
			&transition) ||
		!SG_TacticTransitionSuccessor(prepared->field_result.current_cell,
			&transition, &successor) ||
		!SG_TacticSuccessorStateEqual(&successor, &candidate.successor) ||
		!SG_TacticLiveDescentValid(&transition, candidate.local_cost, &nominal))
		return 0;
	memset(&expected, 0, sizeof(expected));
	expected.status = SG_TACTIC_RESULT_PROGRESS;
	expected.capability = candidate.capability;
	expected.successor = candidate.successor;
	expected.target_phase = candidate.predicted_phase;
	expected.nominal_cost = nominal;
	improvement = transition.v12.cost_to_go.units - nominal.units;
	expected.progress = (float)((double)improvement /
		(double)transition.v12.cost_to_go.units);
	expected.exact_live_validation_required =
		candidate.exact_live_validation_required;
	return ResultEqual(&expected, &prepared->result);
}

static int PreparedBaseCurrent(
	const sg_tactic_runtime_prepared_step_t *prepared)
{
	sg_tactic_runtime_step_input_t input;

	if (prepared == NULL ||
		!SG_TacticRuntimeProviderSnapshotCurrent(&prepared->provider))
		return 0;
	PreparedInput(prepared, &input);
	return InputBaseCurrent(&input) && PreparedFrameValid(prepared) &&
		PreparedSelectionValid(prepared, &input);
}

sg_tactic_runtime_status_t SG_TacticRuntimePreparedStepConsume(
	sg_tactic_runtime_prepared_step_t *prepared)
{
	sg_tactic_runtime_step_input_t input;
	sg_tactic_runtime_prepared_step_t verified;
	sg_strategy_caller_output_receipt_t receipt;
	sg_tactic_runtime_status_t status;

	if (prepared == NULL)
		return SG_TACTIC_RUNTIME_INVALID_ARGUMENT;
	if (prepared->consumed != 0U ||
		!SG_TacticRuntimeProviderSnapshotCurrent(&prepared->provider))
		return SG_TACTIC_RUNTIME_STALE_FRAME;
	PreparedInput(prepared, &input);
	status = PrepareStepWithToken(&input, &verified, prepared->frame.token,
		0, 1);
	if (status != SG_TACTIC_RUNTIME_OK ||
		!PreparedTentativeEqual(prepared, &verified))
		return status == SG_TACTIC_RUNTIME_OK ?
			SG_TACTIC_RUNTIME_STALE_FRAME : status;
	memset(&receipt, 0, sizeof(receipt));
	if (!SG_StrategyCallerOutputProofConsume(prepared->strategy_caller,
			&prepared->strategy_output, &prepared->strategy_proof, &receipt))
		return SG_TACTIC_RUNTIME_STALE_FRAME;
	prepared->strategy_receipt = receipt;
	prepared->consumed = 1U;
	prepared->local_context.mechanisms = NULL;
	prepared->local_context.portal_roots = NULL;
	if (!PreparedBaseCurrent(prepared) ||
		!SG_StrategyCallerOutputReceiptCurrent(prepared->strategy_caller,
			&prepared->strategy_output, &prepared->strategy_receipt) ||
		!SG_StrategyRuntimeCallerQueryReceiptCurrent(
			prepared->strategy_caller, &prepared->strategy_output,
			&prepared->strategy_receipt, &prepared->query_snapshot,
			&prepared->field_result, &prepared->query_proof))
	{
		(void)SG_TacticRuntimePreparedStepRelease(prepared);
		return SG_TACTIC_RUNTIME_STALE_FRAME;
	}
	return SG_TACTIC_RUNTIME_OK;
}

int SG_TacticRuntimePreparedStepCurrent(
	const sg_tactic_runtime_prepared_step_t *prepared)
{
	return prepared != NULL && prepared->consumed == 1U &&
		prepared->local_context.mechanisms == NULL &&
		prepared->local_context.portal_roots == NULL &&
		PreparedBaseCurrent(prepared) &&
		SG_StrategyCallerOutputReceiptCurrent(prepared->strategy_caller,
			&prepared->strategy_output, &prepared->strategy_receipt) &&
		SG_StrategyRuntimeCallerQueryReceiptCurrent(
			prepared->strategy_caller, &prepared->strategy_output,
			&prepared->strategy_receipt, &prepared->query_snapshot,
			&prepared->field_result, &prepared->query_proof);
}

int SG_TacticRuntimePreparedStepRelease(
	sg_tactic_runtime_prepared_step_t *prepared)
{
	int released = 0;

	if (prepared == NULL)
		return 0;
	if (prepared->consumed == 1U)
		released = SG_StrategyRuntimeCallerQueryReceiptRelease(
			prepared->strategy_caller, &prepared->strategy_output,
			&prepared->strategy_receipt, &prepared->query_proof);
	else
		released = SG_StrategyRuntimeCallerQueryProofRelease(
			prepared->strategy_caller, &prepared->strategy_output,
			&prepared->strategy_proof, &prepared->query_proof);
	memset(prepared, 0, sizeof(*prepared));
	return released;
}

sg_tactic_runtime_status_t SG_TacticRuntimeSelectStep(
	const sg_tactic_runtime_step_input_t *input,
	sg_tactic_result_t *result_out)
{
	sg_tactic_runtime_prepared_step_t prepared;
	sg_tactic_runtime_status_t status;

	if (result_out != NULL)
		memset(result_out, 0, sizeof(*result_out));
	if (result_out == NULL || input == NULL)
		return SG_TACTIC_RUNTIME_INVALID_ARGUMENT;
	status = SG_TacticRuntimePrepareStep(input, &prepared);
	if (status == SG_TACTIC_RUNTIME_OK)
		status = SG_TacticRuntimePreparedStepConsume(&prepared);
	if (status == SG_TACTIC_RUNTIME_OK) {
		*result_out = prepared.result;
		if (!SG_TacticRuntimePreparedStepRelease(&prepared)) {
			memset(result_out, 0, sizeof(*result_out));
			status = SG_TACTIC_RUNTIME_STALE_FRAME;
		}
	}
	return status;
}

const char *SG_TacticRuntimeStatusString(sg_tactic_runtime_status_t status)
{
	static const char *const names[SG_TACTIC_RUNTIME_STATUS_COUNT] = {
		"ok", "invalid argument", "provider not current", "stale frame",
		"field rejected", "probe rejected", "ambiguous successor",
		"no legal capability"
	};

	return (uint32_t)status < (uint32_t)SG_TACTIC_RUNTIME_STATUS_COUNT ?
		names[status] : "unknown tactic runtime status";
}
