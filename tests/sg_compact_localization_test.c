#include "../slipgate/sg_compact_localization.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

static sg_rune_compact_model_t model;
static sg_rune_compact_cell_t cells[3];
static sg_rune_compact_facet_t facets[4];
static sg_rune_compact_incidence_t incidences[7];
static sg_rune_compact_incidence_index_t cell_incidences[7];
static sg_rune_compact_portal_t portals[2];
static sg_compact_localization_binding_t binding;
static sg_rune_compact_spatial_index_t *spatial_index;
static sg_host_law_runtime_authority_t authority;
static sg_localization_subject_t subject;
static sg_host_collision_pose_t pose;
static int authority_current;
static int subject_current;
static uint32_t scratch_cells[3];
static sg_compact_localization_scratch_t scratch = {
	scratch_cells, 3U, 0U
};

struct sg_compact_localization_observation_s
{
	uint32_t slot;
	uint32_t guard;
};

typedef struct owner_observation_record_s
{
	struct sg_compact_localization_observation_s capability;
	sg_compact_localization_observation_view_t view;
} owner_observation_record_t;

static owner_observation_record_t owner_observations[128];
static uint32_t owner_observation_count;

#define SG_CompactLocalizationObserve(binding_arg, sample_arg, previous_arg, \
	state_arg) SG_CompactLocalizationObserveWithScratch((binding_arg), \
	(sample_arg), (previous_arg), &scratch, (state_arg))

#define CHECK(condition) \
	do { \
		if (!(condition)) { \
			fprintf(stderr, "check failed: %s:%d: %s\n", __FILE__, \
				__LINE__, #condition); \
			return 0; \
		} \
	} while (0)

static uint32_t FloatBits(float value)
{
	uint32_t bits;

	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

static sg_host_law_result_t HostResult(sg_host_law_status_t status)
{
	sg_host_law_result_t result;

	memset(&result, 0, sizeof(result));
	result.status = status;
	return result;
}

static sg_localization_status_t OwnerValidate(void *context,
	const sg_host_law_runtime_authority_t *candidate_authority,
	const sg_compact_localization_observation_t *observation,
	sg_compact_localization_observation_view_t *view_out)
{
	const struct sg_compact_localization_observation_s *capability = observation;
	owner_observation_record_t *records = context;
	uint32_t slot;

	if (!records || !candidate_authority || !capability || !view_out)
		return SG_LOCALIZATION_UNAUTHENTICATED;
	for (slot = 0U; slot < owner_observation_count; slot++)
		if (&records[slot].capability == capability)
			break;
	if (slot == owner_observation_count || capability->slot != slot ||
		capability->guard != UINT32_C(0xc011ab1e) ||
		candidate_authority->epoch != authority.epoch ||
		candidate_authority->epoch_complement != authority.epoch_complement)
		return SG_LOCALIZATION_UNAUTHENTICATED;
	*view_out = records[slot].view;
	return SG_LOCALIZATION_OK;
}

static sg_compact_localization_observation_owner_t ObservationOwner(void)
{
	sg_compact_localization_observation_owner_t owner;

	owner.context = owner_observations;
	owner.validate = OwnerValidate;
	return owner;
}

sg_host_law_result_t SG_HostLawProductionAuthorityCurrent(
	const sg_host_law_runtime_authority_t *candidate)
{
	return authority_current && candidate &&
		candidate->epoch == authority.epoch &&
		candidate->epoch_complement == authority.epoch_complement ?
		HostResult(SG_HOST_LAW_OK) : HostResult(SG_HOST_LAW_PRODUCTION_DRIFT);
}

sg_host_law_result_t SG_HostLawProductionSubjectCurrent(
	const sg_host_law_runtime_authority_t *candidate,
	const sg_host_law_subject_t *candidate_subject)
{
	return authority_current && candidate && candidate->epoch == authority.epoch &&
		candidate->epoch_complement == authority.epoch_complement && subject_current &&
		candidate_subject && candidate_subject->client_id == subject.client_id &&
		candidate_subject->spawn_generation == subject.spawn_generation ?
		HostResult(SG_HOST_LAW_OK) : HostResult(SG_HOST_LAW_PRODUCTION_DRIFT);
}

sg_host_law_result_t SG_HostLawProductionSubjectClassifyPose(
	const sg_host_law_runtime_authority_t *candidate,
	const sg_host_law_subject_t *candidate_subject, const float origin[3],
	sg_rune_stance_t stance, sg_host_collision_pose_t *pose_out)
{
	(void)origin;
	if (SG_HostLawProductionSubjectCurrent(candidate, candidate_subject).status !=
		SG_HOST_LAW_OK || !pose_out)
		return HostResult(SG_HOST_LAW_PRODUCTION_DRIFT);
	*pose_out = pose;
	pose_out->stance = stance;
	return HostResult(SG_HOST_LAW_OK);
}

static void SetCompactIdentity(sg_rune_compact_identity_t *identity)
{
	uint32_t index;

	memset(identity, 0, sizeof(*identity));
	for (index = 0U; index < SG_BSP_CONTENT_ID_BYTES; index++)
		identity->bsp_sha256[index] = (uint8_t)(index + 1U);
	identity->bsp_bytes = 4096U;
	identity->bsp_checksum = 17U;
	identity->entity_crc32 = 19U;
	identity->entity_semantics_id = UINT64_C(0x1001);
	identity->physics_abi_id = UINT64_C(0x1002);
	identity->collision_law_id = UINT64_C(0x1003);
	identity->pmove_law_id = UINT64_C(0x1004);
	identity->gravity_law_id = UINT64_C(0x1005);
	identity->hook_law_id = UINT64_C(0x1006);
	identity->mechanism_law_id = UINT64_C(0x1007);
	identity->weapon_law_id = UINT64_C(0x1008);
	identity->construction_id = UINT64_C(0x1009);
	identity->schema_id = UINT64_C(0x100a);
	identity->producer_identity = UINT64_C(0x100b);
	identity->source_counts.model_count = 1U;
	identity->source_counts.leaf_count = 1U;
	identity->source_counts.area_count = 1U;
	identity->source_counts.plane_count = 1U;
	identity->source_counts.brush_count = 1U;
	identity->source_counts.brush_side_count = 1U;
	identity->source_counts.entity_count = 1U;
	identity->standing_hull.mins.value[0] = -128;
	identity->standing_hull.mins.value[1] = -128;
	identity->standing_hull.mins.value[2] = -192;
	identity->standing_hull.maxs.value[0] = 128;
	identity->standing_hull.maxs.value[1] = 128;
	identity->standing_hull.maxs.value[2] = 256;
	identity->crouching_hull.mins.value[0] = -128;
	identity->crouching_hull.mins.value[1] = -128;
	identity->crouching_hull.mins.value[2] = -128;
	identity->crouching_hull.maxs.value[0] = 128;
	identity->crouching_hull.maxs.value[1] = 128;
	identity->crouching_hull.maxs.value[2] = 128;
	identity->physics.gravity_bits = FloatBits(100.0f);
	identity->physics.ground_acceleration_bits = FloatBits(10.0f);
	identity->physics.air_acceleration_bits = FloatBits(1.0f);
	identity->physics.water_acceleration_bits = FloatBits(4.0f);
	identity->physics.hook_acceleration_bits = FloatBits(800.0f);
	identity->physics.external_acceleration_bits = FloatBits(2.0f);
	identity->physics.water_drag_bits = FloatBits(0.5f);
	identity->physics.max_velocity_bits = FloatBits(2000.0f);
	identity->physics.frame_ms = 100U;
	identity->physics.substep_ms = 25U;
}

static void SetHostAuthority(const sg_rune_compact_identity_t *identity)
{
	uint32_t axis;

	memset(&authority, 0, sizeof(authority));
	authority.version = SG_HOST_LAW_RUNTIME_AUTHORITY_VERSION;
	authority.epoch = 1U;
	authority.epoch_complement = ~authority.epoch;
	authority.view.version = SG_HOST_LAW_PUBLICATION_VERSION;
	authority.view.collision_law_id = identity->collision_law_id;
	authority.view.pmove_law_id = identity->pmove_law_id;
	authority.view.gravity_law_id = identity->gravity_law_id;
	authority.view.hook_law_id = identity->hook_law_id;
	authority.view.mechanism_law_id = identity->mechanism_law_id;
	memcpy(authority.view.bsp_identity.bytes, identity->bsp_sha256,
		SG_BSP_CONTENT_ID_BYTES);
	authority.view.bsp_bytes = identity->bsp_bytes;
	authority.view.pmove_abi.identity = identity->physics_abi_id;
	authority.view.static_identity.bsp_identity = authority.view.bsp_identity;
	authority.view.static_identity.bsp_bytes = identity->bsp_bytes;
	authority.view.static_identity.engine_checksum = identity->bsp_checksum;
	authority.view.static_identity.entity_crc32 = identity->entity_crc32;
	authority.view.static_identity.host_physics_epoch = 1U;
	authority.view.static_identity.physics_abi_id = identity->physics_abi_id;
	for (axis = 0U; axis < 3U; axis++)
	{
		authority.view.static_identity.standing_hull.mins.value[axis] =
			(float)identity->standing_hull.mins.value[axis] * 0.125f;
		authority.view.static_identity.standing_hull.maxs.value[axis] =
			(float)identity->standing_hull.maxs.value[axis] * 0.125f;
		authority.view.static_identity.crouching_hull.mins.value[axis] =
			(float)identity->crouching_hull.mins.value[axis] * 0.125f;
		authority.view.static_identity.crouching_hull.maxs.value[axis] =
			(float)identity->crouching_hull.maxs.value[axis] * 0.125f;
	}
	authority.view.static_identity.physics.gravity = 100.0f;
	authority.view.static_identity.physics.ground_acceleration = 10.0f;
	authority.view.static_identity.physics.air_acceleration = 1.0f;
	authority.view.static_identity.physics.water_acceleration = 4.0f;
	authority.view.static_identity.physics.hook_acceleration = 800.0f;
	authority.view.static_identity.physics.external_acceleration = 2.0f;
	authority.view.static_identity.physics.water_drag = 0.5f;
	authority.view.static_identity.physics.max_velocity = 2000.0f;
	authority.view.static_identity.physics.frame_ms = 100U;
	authority.view.static_identity.physics.substep_ms = 25U;
}

static void SetModel(const sg_rune_compact_identity_t *identity)
{
	memset(&model, 0, sizeof(model));
	model.version = SG_RUNE_COMPACT_MODEL_VERSION;
	model.schema_tag = SG_RUNE_COMPACT_MODEL_SCHEMA_TAG;
	model.identity = *identity;
	model.cells = cells;
	model.cell_count = 1U;
	model.facets = facets;
	model.facet_count = 1U;
	model.incidences = incidences;
	model.incidence_count = 1U;
	model.cell_incidences = cell_incidences;
	model.cell_incidence_count = 1U;
	memset(cells, 0, sizeof(cells));
	cells[0].bounds.mins.value[0] = -640;
	cells[0].bounds.mins.value[1] = -640;
	cells[0].bounds.mins.value[2] = -640;
	cells[0].bounds.maxs.value[0] = 640;
	cells[0].bounds.maxs.value[1] = 640;
	cells[0].bounds.maxs.value[2] = 640;
	cells[0].incidences.count = 1U;
	cells[0].valid_stances = SG_RUNE_STANCE_VALID_ALL;
	memset(facets, 0, sizeof(facets));
	facets[0].portal.value = SG_RUNE_COMPACT_INDEX_NONE;
	facets[0].plane.normal_bits[0] = FloatBits(1.0f);
	facets[0].plane.distance_bits = FloatBits(80.0f);
	memset(incidences, 0, sizeof(incidences));
	incidences[0].cell.value = 0U;
	incidences[0].facet.value = 0U;
	incidences[0].cell_ordinal = 0U;
	incidences[0].side = SG_RUNE_FACET_NEGATIVE_SIDE;
	incidences[0].boundary = SG_RUNE_BOUNDARY_CLOSED;
	cell_incidences[0].value = 0U;
}

static void SetSlabModel(const sg_rune_compact_identity_t *identity)
{
	uint32_t index;

	SetModel(identity);
	model.cell_count = 3U;
	model.facet_count = 3U;
	model.incidence_count = 5U;
	model.cell_incidence_count = 5U;
	model.portals = portals;
	model.portal_count = 1U;
	memset(cells, 0, sizeof(cells));
	memset(facets, 0, sizeof(facets));
	memset(incidences, 0, sizeof(incidences));
	memset(cell_incidences, 0, sizeof(cell_incidences));
	memset(portals, 0, sizeof(portals));
	for (index = 0U; index < 3U; index++)
	{
		cells[index].bounds.mins.value[0] = index == 0U ? -640 :
			(int32_t)(index * 640U);
		cells[index].bounds.maxs.value[0] = (int32_t)((index + 1U) * 640U);
		cells[index].bounds.mins.value[1] = -640;
		cells[index].bounds.maxs.value[1] = 640;
		cells[index].bounds.mins.value[2] = -640;
		cells[index].bounds.maxs.value[2] = 640;
		cells[index].valid_stances = SG_RUNE_STANCE_VALID_ALL;
	}
	cells[0].incidences.first = 0U;
	cells[0].incidences.count = 1U;
	cells[1].incidences.first = 1U;
	cells[1].incidences.count = 2U;
	cells[2].incidences.first = 3U;
	cells[2].incidences.count = 2U;
	for (index = 0U; index < 3U; index++)
	{
		facets[index].plane.normal_bits[0] = FloatBits(1.0f);
		facets[index].plane.distance_bits = FloatBits((float)(index + 1U) * 80.0f);
		facets[index].portal.value = SG_RUNE_COMPACT_INDEX_NONE;
	}
	facets[0].portal.value = 0U;
	incidences[0].cell.value = 0U;
	incidences[0].facet.value = 0U;
	incidences[0].side = SG_RUNE_FACET_NEGATIVE_SIDE;
	incidences[0].boundary = SG_RUNE_BOUNDARY_CLOSED;
	incidences[1].cell.value = 1U;
	incidences[1].facet.value = 0U;
	incidences[1].side = SG_RUNE_FACET_POSITIVE_SIDE;
	incidences[1].boundary = SG_RUNE_BOUNDARY_OPEN;
	incidences[2].cell.value = 1U;
	incidences[2].facet.value = 1U;
	incidences[2].cell_ordinal = 1U;
	incidences[2].side = SG_RUNE_FACET_NEGATIVE_SIDE;
	incidences[2].boundary = SG_RUNE_BOUNDARY_CLOSED;
	incidences[3].cell.value = 2U;
	incidences[3].facet.value = 1U;
	incidences[3].side = SG_RUNE_FACET_POSITIVE_SIDE;
	incidences[3].boundary = SG_RUNE_BOUNDARY_OPEN;
	incidences[4].cell.value = 2U;
	incidences[4].facet.value = 2U;
	incidences[4].cell_ordinal = 1U;
	incidences[4].side = SG_RUNE_FACET_NEGATIVE_SIDE;
	incidences[4].boundary = SG_RUNE_BOUNDARY_CLOSED;
	for (index = 0U; index < 5U; index++)
		cell_incidences[index].value = index;
	portals[0].facet.value = 0U;
	portals[0].negative_incidence.value = 0U;
	portals[0].positive_incidence.value = 1U;
	portals[0].direction = SG_RUNE_PORTAL_CONTINUITY_BOTH;
	portals[0].valid_stances = SG_RUNE_STANCE_VALID_ALL;
}

static int BuildSpatialIndex(void)
{
	sg_rune_compact_spatial_cell_input_t spatial_cells[3];
	sg_rune_compact_spatial_face_input_t spatial_faces[18];
	sg_rune_compact_spatial_topology_input_t topology;
	sg_rune_compact_spatial_error_t error;
	uint32_t cell_index;

	if (spatial_index)
		SG_RuneCompactSpatialIndexDestroy(spatial_index);
	spatial_index = NULL;
	memset(spatial_cells, 0, sizeof(spatial_cells));
	memset(spatial_faces, 0, sizeof(spatial_faces));
	memset(&topology, 0, sizeof(topology));
	for (cell_index = 0U; cell_index < model.cell_count; cell_index++)
	{
		uint32_t axis;

		spatial_cells[cell_index].first_face = cell_index * 6U;
		spatial_cells[cell_index].face_count = 6U;
		for (axis = 0U; axis < 3U; axis++)
		{
			const float minimum =
				(float)model.cells[cell_index].bounds.mins.value[axis] * 0.125f;
			const float maximum =
				(float)model.cells[cell_index].bounds.maxs.value[axis] * 0.125f;

			spatial_cells[cell_index].bounds.mins.value[axis] = minimum;
			spatial_cells[cell_index].bounds.maxs.value[axis] = maximum;
		}
		for (axis = 0U; axis < 3U; axis++)
		{
			const uint32_t minimum_face = cell_index * 6U + axis * 2U;
			const uint32_t maximum_face = minimum_face + 1U;
			const float minimum =
				spatial_cells[cell_index].bounds.mins.value[axis];
			const float maximum =
				spatial_cells[cell_index].bounds.maxs.value[axis];

			spatial_faces[minimum_face].bounds = spatial_cells[cell_index].bounds;
			spatial_faces[minimum_face].normal[axis] = -1.0f;
			spatial_faces[minimum_face].distance = -minimum;
			spatial_faces[minimum_face].source_boundary = minimum_face;
			spatial_faces[minimum_face].ownership = SG_RUNE_BOUNDARY_CLOSED;
			spatial_faces[maximum_face].bounds = spatial_cells[cell_index].bounds;
			spatial_faces[maximum_face].normal[axis] = 1.0f;
			spatial_faces[maximum_face].distance = maximum;
			spatial_faces[maximum_face].source_boundary = maximum_face;
			spatial_faces[maximum_face].ownership = SG_RUNE_BOUNDARY_CLOSED;
		}
	}
	topology.cells = spatial_cells;
	topology.cell_count = model.cell_count;
	topology.faces = spatial_faces;
	topology.face_count = model.cell_count * 6U;
	memset(&error, 0, sizeof(error));
	return SG_RuneCompactSpatialIndexBuildTopology(&topology, NULL,
		&spatial_index, &error);
}

static void SetPose(int supported, uint8_t water_level,
	sg_host_collision_contents_t water_type)
{
	memset(&pose, 0, sizeof(pose));
	pose.valid = 1;
	pose.supported = supported;
	pose.water_level = water_level;
	pose.water_type = water_type;
	pose.gravity = 100.0f;
	pose.physics_abi_id = UINT64_C(0x1002);
}

static void SetResult(sg_host_pmove_result_t *result, int16_t origin_x,
	int grounded, int water_level, int water_type, uint8_t flags)
{
	memset(result, 0, sizeof(*result));
	result->state.pm_type = PM_NORMAL;
	result->state.origin[0] = origin_x;
	result->state.pm_flags = flags;
	result->state.gravity = 100;
	result->origin[0] = (float)result->state.origin[0] * 0.125f;
	result->grounded = grounded;
	result->support_model_index = grounded ? SG_HOST_COLLISION_MODEL_WORLD : 0U;
	result->support_instance_id = 0U;
	result->water_level = water_level;
	result->water_type = water_type;
	result->evaluated_steps = 4U;
	result->elapsed_ms = 100U;
	result->gravity = 100.0f;
	result->physics_abi_id = UINT64_C(0x1002);
}

static sg_compact_localization_sample_t OwnerIssue(
	sg_localization_observation_kind_t kind, uint64_t frame, uint64_t time,
	const sg_host_pmove_result_t *result,
	const sg_host_pmove_state_observation_t *state_observation,
	float maximum_recovery_distance, uint64_t maximum_temporary_absence_ms,
	const sg_compact_localized_state_t *retired,
	uint64_t model_identity, uint64_t model_generation)
{
	sg_compact_localization_sample_t sample;
	owner_observation_record_t *record;

	if (owner_observation_count >=
		(uint32_t)(sizeof(owner_observations) / sizeof(owner_observations[0])))
	{
		memset(&sample, 0, sizeof(sample));
		return sample;
	}
	record = &owner_observations[owner_observation_count];
	memset(record, 0, sizeof(*record));
	record->capability.slot = owner_observation_count;
	record->capability.guard = UINT32_C(0xc011ab1e);
	record->view.kind = kind;
	record->view.subject = subject;
	record->view.host_authority_epoch = authority.epoch;
	record->view.frame_sequence = frame;
	record->view.observed_at_ms = time;
	record->view.model_stamp.identity = model_identity;
	record->view.model_stamp.generation = model_generation;
	record->view.model_stamp.frame_sequence = frame;
	record->view.pmove_result = result;
	record->view.state_observation = state_observation;
	record->view.maximum_recovery_distance = maximum_recovery_distance;
	record->view.maximum_temporary_absence_ms = maximum_temporary_absence_ms;
	if (retired)
	{
		record->view.previous_subject = retired->subject;
		record->view.previous_frame_sequence = retired->frame_sequence;
		record->view.previous_observed_at_ms = retired->localized_at_ms;
	}
	memset(&sample, 0, sizeof(sample));
	sample.observation = &record->capability;
	owner_observation_count++;
	return sample;
}

static sg_compact_localization_sample_t Sample(
	sg_localization_observation_kind_t kind, uint64_t frame, uint64_t time,
	const sg_host_pmove_result_t *result)
{
	return OwnerIssue(kind, frame, time, result, NULL, 0.0f, 0U, NULL,
		UINT64_C(0x2001), UINT64_C(0x2002));
}

static int TestLocalizationLifecycle(void)
{
	sg_rune_compact_identity_t identity;
	sg_host_pmove_result_t result;
	sg_host_pmove_state_observation_t state_observation;
	sg_compact_localization_sample_t sample;
	sg_compact_localized_state_t first;
	sg_compact_localized_state_t state;
	sg_compact_localized_state_t rejected;
	sg_compact_localization_observation_owner_t observation_owner;

	SetCompactIdentity(&identity);
	SetHostAuthority(&identity);
	SetModel(&identity);
	CHECK(BuildSpatialIndex());
	observation_owner = ObservationOwner();
	owner_observation_count = 0U;
	memset(&subject, 0, sizeof(subject));
	subject.client_id = 2U;
	subject.spawn_generation = 7U;
	authority_current = 1;
	subject_current = 1;
	SetPose(1, 0U, 0U);
	authority.view.static_identity.physics.gravity = 800.0f;
	CHECK(SG_CompactLocalizationBind(&binding, &model, &identity, spatial_index,
		&observation_owner, &authority,
		UINT64_C(0x2001), UINT64_C(0x2002)) ==
		SG_LOCALIZATION_IDENTITY_MISMATCH);
	SetHostAuthority(&identity);
	CHECK(SG_CompactLocalizationBind(&binding, &model, &identity, spatial_index,
		&observation_owner, &authority,
		UINT64_C(0x2001), UINT64_C(0x2002)) == SG_LOCALIZATION_OK);
	CHECK(SG_CompactLocalizationBindingCurrent(&binding));
	SetResult(&result, 0, 1, 0, 0, PMF_ON_GROUND);
	sample = Sample(SG_LOCALIZATION_OBSERVATION_PRESENT, 1U, 100U, &result);
	CHECK(SG_CompactLocalizationObserve(&binding, &sample, NULL, &first) ==
		SG_LOCALIZATION_OK);
	CHECK(first.valid && first.location.cell.value == 0U);
	CHECK(first.model_stamp.identity == UINT64_C(0x2001));
	CHECK(first.model_stamp.generation == UINT64_C(0x2002));
	CHECK(first.model_stamp.frame_sequence == 1U);
	CHECK(first.presence == SG_LOCALIZATION_PRESENCE_PRESENT);
	CHECK(first.motion == SG_RUNE_MOTION_SUPPORTED);
	CHECK(first.support == SG_RUNE_SUPPORT_SUPPORTED);
	CHECK(first.recovery == SG_LOCALIZATION_RECOVERY_NONE);
	memset(&sample, 0, sizeof(sample));
	sample.observation =
		(const sg_compact_localization_observation_t *)(uintptr_t)1U;
	CHECK(SG_CompactLocalizationObserve(&binding, &sample, NULL, &rejected) ==
		SG_LOCALIZATION_UNAUTHENTICATED);
	CHECK(rejected.valid == 0U);

	result.state.pm_flags = PMF_DUCKED | PMF_ON_GROUND;
	sample = Sample(SG_LOCALIZATION_OBSERVATION_PRESENT, 2U, 200U, &result);
	CHECK(SG_CompactLocalizationObserve(&binding, &sample, &first, &state) ==
		SG_LOCALIZATION_OK);
	CHECK(state.stance == SG_RUNE_STANCE_CROUCHING);
	CHECK(state.recovery == SG_LOCALIZATION_RECOVERY_EXACT_CONTINUITY);

	SetPose(0, 2U, SG_HOST_CONTENTS_WATER);
	SetResult(&result, 0, 0, 2, SG_HOST_CONTENTS_WATER, 0U);
	sample = Sample(SG_LOCALIZATION_OBSERVATION_PRESENT, 3U, 300U, &result);
	CHECK(SG_CompactLocalizationObserve(&binding, &sample, &state, &first) ==
		SG_LOCALIZATION_OK);
	CHECK(first.motion == SG_RUNE_MOTION_SWIMMING);
	CHECK(first.support == SG_RUNE_SUPPORT_NONE);
	CHECK(first.medium == SG_RUNE_MEDIUM_WATER);

	SetPose(0, 0U, 0U);
	SetResult(&result, 0, 0, 0, 0, 0U);
	sample = Sample(SG_LOCALIZATION_OBSERVATION_PRESENT, 4U, 400U, &result);
	CHECK(SG_CompactLocalizationObserve(&binding, &sample, &first, &state) ==
		SG_LOCALIZATION_OK);
	CHECK(state.motion == SG_RUNE_MOTION_AIRBORNE);
	CHECK(state.support == SG_RUNE_SUPPORT_NONE);
	CHECK(state.support_model_index == SG_LOCALIZATION_SUPPORT_MODEL_NONE);

	SetPose(1, 0U, 0U);
	SetResult(&result, 642, 1, 0, 0, PMF_ON_GROUND);
	sample = OwnerIssue(SG_LOCALIZATION_OBSERVATION_PRESENT, 5U, 500U,
		&result, NULL, 0.25f, 0U, NULL, UINT64_C(0x2001),
		UINT64_C(0x2002));
	CHECK(SG_CompactLocalizationObserve(&binding, &sample, &state, &first) ==
		SG_LOCALIZATION_OK);
	CHECK(first.recovery == SG_LOCALIZATION_RECOVERY_NUMERIC_DRIFT);

	SetResult(&result, 644, 1, 0, 0, PMF_ON_GROUND);
	sample = OwnerIssue(SG_LOCALIZATION_OBSERVATION_PRESENT, 6U, 600U,
		&result, NULL, 0.5f, 0U, NULL, UINT64_C(0x2001),
		UINT64_C(0x2002));
	CHECK(SG_CompactLocalizationObserve(&binding, &sample, &first, &state) ==
		SG_LOCALIZATION_OK);
	CHECK(state.recovery == SG_LOCALIZATION_RECOVERY_NUMERIC_DRIFT);
	SetResult(&result, 645, 1, 0, 0, PMF_ON_GROUND);
	sample = OwnerIssue(SG_LOCALIZATION_OBSERVATION_PRESENT, 7U, 700U,
		&result, NULL, 0.625f, 0U, NULL, UINT64_C(0x2001),
		UINT64_C(0x2002));
	CHECK(SG_CompactLocalizationObserve(&binding, &sample, &state, &rejected) ==
		SG_LOCALIZATION_RECOVERY_PARAMETER);

	SetResult(&result, 0, 1, 0, 0, PMF_ON_GROUND);
	sample = Sample(SG_LOCALIZATION_OBSERVATION_TELEPORTED, 6U, 700U,
		&result);
	CHECK(SG_CompactLocalizationObserve(&binding, &sample, &state, &rejected) ==
		SG_LOCALIZATION_STALE);
	sample = Sample(SG_LOCALIZATION_OBSERVATION_TELEPORTED, 7U, 600U,
		&result);
	CHECK(SG_CompactLocalizationObserve(&binding, &sample, &state, &rejected) ==
		SG_LOCALIZATION_STALE);
	sample = Sample(SG_LOCALIZATION_OBSERVATION_TELEPORTED, 7U, 700U,
		&result);
	CHECK(SG_CompactLocalizationObserve(&binding, &sample, &state, &first) ==
		SG_LOCALIZATION_OK);
	CHECK(first.recovery == SG_LOCALIZATION_RECOVERY_NONE);
	sample = Sample(SG_LOCALIZATION_OBSERVATION_TELEPORTED, 8U, 800U,
		&result);
	CHECK(SG_CompactLocalizationObserve(&binding, &sample, NULL, &rejected) ==
		SG_LOCALIZATION_RECOVERY_PARAMETER);

	memset(&state_observation, 0, sizeof(state_observation));
	state_observation.state = result.state;
	memcpy(state_observation.origin, result.origin,
		sizeof(state_observation.origin));
	memcpy(state_observation.velocity, result.velocity,
		sizeof(state_observation.velocity));
	sample = OwnerIssue(SG_LOCALIZATION_OBSERVATION_TELEPORTED, 8U, 800U,
		NULL, &state_observation, 0.0f, 0U, NULL, UINT64_C(0x2001),
		UINT64_C(0x2002));
	CHECK(SG_CompactLocalizationObserve(&binding, &sample, &state, &rejected) ==
		SG_LOCALIZATION_INVALID_ARGUMENT);

	sample = OwnerIssue(SG_LOCALIZATION_OBSERVATION_TEMPORARILY_ABSENT, 8U,
		750U, NULL, NULL, 0.0f, 100U, NULL, UINT64_C(0x2001),
		UINT64_C(0x2002));
	CHECK(SG_CompactLocalizationObserve(&binding, &sample, &first, &state) ==
		SG_LOCALIZATION_OK);
	CHECK(state.recovery == SG_LOCALIZATION_RECOVERY_TEMPORARY_ABSENCE);
	CHECK(state.presence == SG_LOCALIZATION_PRESENCE_TEMPORARILY_ABSENT);
	CHECK(state.model_stamp.frame_sequence == 8U);
	CHECK(state.absence_started_at_ms == 750U);
	sample = OwnerIssue(SG_LOCALIZATION_OBSERVATION_TEMPORARILY_ABSENT, 9U,
		820U, NULL, NULL, 0.0f, 100U, NULL, UINT64_C(0x2001),
		UINT64_C(0x2002));
	CHECK(SG_CompactLocalizationObserve(&binding, &sample, &state, &rejected) ==
		SG_LOCALIZATION_OK);
	CHECK(rejected.absence_started_at_ms == 750U);
	sample = OwnerIssue(SG_LOCALIZATION_OBSERVATION_TEMPORARILY_ABSENT, 10U,
		860U, NULL, NULL, 0.0f, 100U, NULL, UINT64_C(0x2001),
		UINT64_C(0x2002));
	CHECK(SG_CompactLocalizationObserve(&binding, &sample, &rejected, &state) ==
		SG_LOCALIZATION_RECOVERY_REJECTED);

	sample = Sample(SG_LOCALIZATION_OBSERVATION_DEAD, 9U, 850U, NULL);
	CHECK(SG_CompactLocalizationObserve(&binding, &sample, &first, &rejected) ==
		SG_LOCALIZATION_OK);
	CHECK(rejected.valid == 1U);
	CHECK(rejected.presence == SG_LOCALIZATION_PRESENCE_DEAD);
	CHECK(rejected.location.cell.value == SG_RUNE_COMPACT_INDEX_NONE);

	SetResult(&result, 0, 1, 0, 0, PMF_ON_GROUND);
	sample = OwnerIssue(SG_LOCALIZATION_OBSERVATION_NEW_SPAWN, 10U, 950U,
		NULL, &state_observation, 0.0f, 0U, NULL, UINT64_C(0x2001),
		UINT64_C(0x2002));
	CHECK(SG_CompactLocalizationObserve(&binding, &sample, &first, &state) ==
		SG_LOCALIZATION_RECOVERY_REJECTED);
	subject.spawn_generation++;
	sample = OwnerIssue(SG_LOCALIZATION_OBSERVATION_NEW_SPAWN,
		first.frame_sequence, first.localized_at_ms + 100U, NULL,
		&state_observation, 0.0f, 0U, &first, UINT64_C(0x2001),
		UINT64_C(0x2002));
	CHECK(SG_CompactLocalizationObserve(&binding, &sample, &first, &state) ==
		SG_LOCALIZATION_STALE);
	sample = OwnerIssue(SG_LOCALIZATION_OBSERVATION_NEW_SPAWN,
		first.frame_sequence + 1U, first.localized_at_ms, NULL,
		&state_observation, 0.0f, 0U, &first, UINT64_C(0x2001),
		UINT64_C(0x2002));
	CHECK(SG_CompactLocalizationObserve(&binding, &sample, &first, &state) ==
		SG_LOCALIZATION_STALE);
	sample = OwnerIssue(SG_LOCALIZATION_OBSERVATION_NEW_SPAWN, 10U, 950U,
		NULL, &state_observation, 0.0f, 0U, &first, UINT64_C(0x2001),
		UINT64_C(0x2002));
	CHECK(SG_CompactLocalizationObserve(&binding, &sample, &first, &state) ==
		SG_LOCALIZATION_OK);
	CHECK(state.subject.spawn_generation == subject.spawn_generation);
	CHECK(state.recovery == SG_LOCALIZATION_RECOVERY_NONE);

	SetPose(1, 0U, 0U);
	pose.support_is_mover = 1;
	pose.support.model_index = 42U;
	pose.support.instance_id = 9U;
	SetResult(&result, 0, 1, 0, 0, PMF_ON_GROUND);
	result.support_model_index = 42U;
	result.support_instance_id = 9U;
	sample = Sample(SG_LOCALIZATION_OBSERVATION_PRESENT, 11U, 1050U, &result);
	CHECK(SG_CompactLocalizationObserve(&binding, &sample, NULL, &first) ==
		SG_LOCALIZATION_OK);
	CHECK(first.support == SG_RUNE_SUPPORT_MOVER);
	CHECK(first.reference_frame == SG_RUNE_FRAME_MOVER_RELATIVE);
	CHECK(first.support_model_index == 42U);
	CHECK(first.support_instance_id == 9U);

	sample = OwnerIssue(SG_LOCALIZATION_OBSERVATION_PRESENT, 11U, 1050U,
		&result, NULL, 0.0f, 0U, NULL, UINT64_C(0x2002),
		UINT64_C(0x2002));
	CHECK(SG_CompactLocalizationObserve(&binding, &sample, NULL, &rejected) ==
		SG_LOCALIZATION_IDENTITY_MISMATCH);
	CHECK(rejected.valid == 0U);
	sample = OwnerIssue(SG_LOCALIZATION_OBSERVATION_PRESENT, 11U, 1050U,
		&result, NULL, 0.0f, 0U, NULL, UINT64_C(0x2001),
		UINT64_C(0x2003));
	CHECK(SG_CompactLocalizationObserve(&binding, &sample, NULL, &rejected) ==
		SG_LOCALIZATION_IDENTITY_MISMATCH);
	first.model_stamp.generation++;
	CHECK(!SG_CompactLocalizationStateCurrent(&binding, &subject, &first));
	first.model_stamp.generation--;

	cells[0].semantics = SG_RUNE_COMPACT_CELL_MOVER_VOLUME;
	sample = Sample(SG_LOCALIZATION_OBSERVATION_PRESENT, 12U, 1150U, &result);
	CHECK(SG_CompactLocalizationObserve(&binding, &sample, NULL, &rejected) ==
		SG_LOCALIZATION_MOVER_UNBOUND);
	cells[0].semantics = 0U;

	authority_current = 0;
	CHECK(!SG_CompactLocalizationBindingCurrent(&binding));
	authority_current = 1;
	SG_CompactLocalizationUnbind(&binding);
	CHECK(!SG_CompactLocalizationBindingCurrent(&binding));
	return 1;
}

static int TestExactAdjacencyAndPhaseRecovery(void)
{
	sg_rune_compact_identity_t identity;
	sg_host_pmove_result_t result;
	sg_compact_localization_sample_t sample;
	sg_compact_localized_state_t cell_zero;
	sg_compact_localized_state_t cell_one;
	sg_compact_localized_state_t state;
	sg_compact_localization_observation_owner_t observation_owner;

	SetCompactIdentity(&identity);
	SetHostAuthority(&identity);
	SetSlabModel(&identity);
	CHECK(BuildSpatialIndex());
	observation_owner = ObservationOwner();
	owner_observation_count = 0U;
	memset(&subject, 0, sizeof(subject));
	subject.client_id = 3U;
	subject.spawn_generation = 2U;
	authority_current = 1;
	subject_current = 1;
	SetPose(1, 0U, 0U);
	CHECK(SG_CompactLocalizationBind(&binding, &model, &identity, spatial_index,
		&observation_owner, &authority,
		UINT64_C(0x2001), UINT64_C(0x2002)) == SG_LOCALIZATION_OK);

	SetResult(&result, 0, 1, 0, 0, PMF_ON_GROUND);
	sample = Sample(SG_LOCALIZATION_OBSERVATION_PRESENT, 1U, 100U, &result);
	CHECK(SG_CompactLocalizationObserve(&binding, &sample, NULL, &cell_zero) ==
		SG_LOCALIZATION_OK);
	SetResult(&result, 800, 1, 0, 0, PMF_ON_GROUND);
	sample = Sample(SG_LOCALIZATION_OBSERVATION_PRESENT, 2U, 200U, &result);
	CHECK(SG_CompactLocalizationObserve(&binding, &sample, &cell_zero, &cell_one) ==
		SG_LOCALIZATION_OK);
	CHECK(cell_one.location.cell.value == 1U);
	CHECK(cell_one.recovery == SG_LOCALIZATION_RECOVERY_EXACT_CONTINUITY);
	portals[0].direction = SG_RUNE_PORTAL_CONTINUITY_NEGATIVE_TO_POSITIVE;
	SetResult(&result, 0, 1, 0, 0, PMF_ON_GROUND);
	sample = Sample(SG_LOCALIZATION_OBSERVATION_PRESENT, 3U, 300U, &result);
	CHECK(SG_CompactLocalizationObserve(&binding, &sample, &cell_one, &state) ==
		SG_LOCALIZATION_OUTSIDE_CONFIGURATION);
	portals[0].direction = SG_RUNE_PORTAL_CONTINUITY_BOTH;

	SetResult(&result, 1600, 1, 0, 0, PMF_ON_GROUND);
	sample = Sample(SG_LOCALIZATION_OBSERVATION_PRESENT, 2U, 200U, &result);
	CHECK(SG_CompactLocalizationObserve(&binding, &sample, &cell_zero, &state) ==
		SG_LOCALIZATION_OUTSIDE_CONFIGURATION);
	CHECK(state.valid == 0U);

	portals[0].valid_stances = SG_RUNE_STANCE_VALID_STANDING;
	SetResult(&result, 800, 1, 0, 0, PMF_ON_GROUND | PMF_DUCKED);
	sample = Sample(SG_LOCALIZATION_OBSERVATION_PRESENT, 2U, 200U, &result);
	CHECK(SG_CompactLocalizationObserve(&binding, &sample, &cell_zero, &state) ==
		SG_LOCALIZATION_OUTSIDE_CONFIGURATION);
	portals[0].valid_stances = SG_RUNE_STANCE_VALID_ALL;
	CHECK(SG_CompactLocalizationObserve(&binding, &sample, &cell_zero, &state) ==
		SG_LOCALIZATION_OK);
	CHECK(state.stance == SG_RUNE_STANCE_CROUCHING);

	/* The first parallel portal disallows this direction; the second exact
	 * incidence permits it. The locator must examine both incident portals. */
	model.facet_count = 4U;
	model.incidence_count = 7U;
	model.cell_incidence_count = 7U;
	model.portal_count = 2U;
	cell_incidences[0].value = 0U;
	cell_incidences[1].value = 5U;
	cell_incidences[2].value = 1U;
	cell_incidences[3].value = 6U;
	cell_incidences[4].value = 2U;
	cell_incidences[5].value = 3U;
	cell_incidences[6].value = 4U;
	cells[0].incidences.first = 0U;
	cells[0].incidences.count = 2U;
	cells[1].incidences.first = 2U;
	cells[1].incidences.count = 3U;
	cells[2].incidences.first = 5U;
	cells[2].incidences.count = 2U;
	incidences[5] = incidences[0];
	incidences[5].facet.value = 3U;
	incidences[5].cell_ordinal = 1U;
	incidences[6] = incidences[1];
	incidences[6].facet.value = 3U;
	incidences[6].cell_ordinal = 1U;
	incidences[2].cell_ordinal = 2U;
	facets[3] = facets[0];
	facets[3].portal.value = 1U;
	portals[1] = portals[0];
	portals[1].facet.value = 3U;
	portals[1].negative_incidence.value = 5U;
	portals[1].positive_incidence.value = 6U;
	portals[0].direction = SG_RUNE_PORTAL_CONTINUITY_POSITIVE_TO_NEGATIVE;
	portals[1].direction = SG_RUNE_PORTAL_CONTINUITY_BOTH;
	SetResult(&result, 800, 1, 0, 0, PMF_ON_GROUND);
	sample = Sample(SG_LOCALIZATION_OBSERVATION_PRESENT, 2U, 200U, &result);
	CHECK(SG_CompactLocalizationObserve(&binding, &sample, &cell_zero, &state) ==
		SG_LOCALIZATION_OK);

	/* A Q8 gap models bounded numeric drift. Only the authenticated portal makes
	 * cell one eligible; a nearby disconnected cell remains invisible. */
	cells[0].bounds.maxs.value[0] = 636;
	cells[1].bounds.mins.value[0] = 644;
	SetResult(&result, 640, 1, 0, 0, PMF_ON_GROUND);
	sample = OwnerIssue(SG_LOCALIZATION_OBSERVATION_PRESENT, 3U, 300U,
		&result, NULL, 0.5f, 0U, NULL, UINT64_C(0x2001),
		UINT64_C(0x2002));
	CHECK(SG_CompactLocalizationObserve(&binding, &sample, &cell_zero, &state) ==
		SG_LOCALIZATION_AMBIGUOUS_INPUT);
	CHECK(state.valid == 0U);
	SetResult(&result, 642, 1, 0, 0, PMF_ON_GROUND);
	sample = OwnerIssue(SG_LOCALIZATION_OBSERVATION_PRESENT, 3U, 300U,
		&result, NULL, 0.25f, 0U, NULL, UINT64_C(0x2001),
		UINT64_C(0x2002));
	CHECK(SG_CompactLocalizationObserve(&binding, &sample, &cell_zero, &state) ==
		SG_LOCALIZATION_OK);
	CHECK(state.location.cell.value == 1U);
	CHECK(state.recovery == SG_LOCALIZATION_RECOVERY_NUMERIC_DRIFT);

	cells[1].bounds.maxs.value[0] = 1276;
	cells[2].bounds.mins.value[0] = 1284;
	SetResult(&result, 1282, 1, 0, 0, PMF_ON_GROUND);
	sample = OwnerIssue(SG_LOCALIZATION_OBSERVATION_PRESENT, 3U, 300U,
		&result, NULL, 0.25f, 0U, NULL, UINT64_C(0x2001),
		UINT64_C(0x2002));
	CHECK(SG_CompactLocalizationObserve(&binding, &sample, &cell_one, &state) ==
		SG_LOCALIZATION_OUTSIDE_CONFIGURATION);
	CHECK(state.valid == 0U);

	SG_CompactLocalizationUnbind(&binding);
	return 1;
}

int main(void)
{
	if (!TestLocalizationLifecycle() || !TestExactAdjacencyAndPhaseRecovery())
		return 1;
	puts("sg_compact_localization_test: PASS");
	return 0;
}
