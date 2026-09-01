#include "sg_rune_compact_field_service.h"
#include "sg_rune_compact_field_plan_private.h"
#include "sg_rune_compact_field_service_private.h"

#include <limits.h>
#include <math.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SG_COMPACT_FIELD_SERVICE_MAGIC UINT64_C(0x5347465356433031)

typedef struct sg_rune_compact_field_service_cache_entry_s
	sg_rune_compact_field_service_cache_entry_t;
typedef struct sg_rune_compact_field_service_lease_s
	sg_rune_compact_field_service_lease_t;
typedef struct sg_rune_compact_field_service_token_s
	sg_rune_compact_field_service_token_t;

struct sg_rune_compact_field_service_cache_entry_s
{
	sg_rune_compact_field_target_t target;
	sg_rune_compact_destination_plan_t *plan;
	uint32_t coarse_region;
	uint64_t coarse_region_epoch;
	uint32_t lease_count;
	uint32_t borrower_count;
	uint8_t current;
	sg_rune_compact_field_service_cache_entry_t *next;
};

struct sg_rune_compact_field_service_lease_s
{
	sg_rune_compact_field_handle_t handle;
	sg_rune_compact_field_service_cache_entry_t *entry;
	sg_rune_compact_field_service_lease_t *next;
};

struct sg_rune_compact_field_service_token_s
{
	sg_rune_compact_field_service_t *service;
	sg_rune_compact_field_service_cache_entry_t *entry;
	uint64_t request_commitment_id;
	sg_rune_compact_field_target_t request_target;
	sg_rune_compact_field_handle_t handle;
	uint8_t accepted;
	sg_rune_compact_field_service_token_t *next;
};

struct sg_rune_compact_field_service_s
{
	uint64_t magic;
	uint64_t identity;
	uint64_t generation;
	uint64_t next_field_generation;
	const sg_rune_compact_model_t *model;
	sg_rune_compact_identity_t expected_identity;
	uint64_t rune_identity;
	uint64_t topology_revision;
	sg_rune_compact_field_t *field;
	uint64_t *region_epochs;
	uint32_t region_count;
	sg_rune_compact_field_service_cache_entry_t *cache;
	sg_rune_compact_field_service_lease_t *leases;
	sg_rune_compact_field_service_token_t *tokens;
	sg_rune_compact_field_service_stats_t stats;
};

static _Atomic uint64_t next_service_identity = UINT64_C(1);

static int NextIdentity(uint64_t *identity_out)
{
	uint64_t current;

	if (identity_out == NULL)
		return 0;
	current = atomic_load_explicit(&next_service_identity,
		memory_order_relaxed);
	for (;;)
	{
		if (current == 0U || current == UINT64_MAX)
			return 0;
		if (atomic_compare_exchange_weak_explicit(&next_service_identity,
			&current, current + 1U, memory_order_relaxed,
			memory_order_relaxed))
		{
			*identity_out = current;
			return 1;
		}
	}
}

static int ServiceValid(const sg_rune_compact_field_service_t *service)
{
	return service != NULL && service->magic == SG_COMPACT_FIELD_SERVICE_MAGIC &&
		service->identity != 0U && service->generation != 0U &&
		service->next_field_generation != 0U && service->model != NULL &&
		service->field != NULL;
}

static int TargetMotionValid(sg_rune_compact_field_target_motion_t motion)
{
	return motion >= SG_RUNE_COMPACT_FIELD_TARGET_STATIC &&
		motion < SG_RUNE_COMPACT_FIELD_TARGET_MOTION_COUNT;
}

static int DestinationEqual(const sg_rune_compact_destination_t *left,
	const sg_rune_compact_destination_t *right)
{
	if (left == NULL || right == NULL || left->kind != right->kind)
		return 0;
	switch (left->kind)
	{
	case SG_RUNE_COMPACT_DESTINATION_POINT:
		return memcmp(&left->value.point, &right->value.point,
			sizeof(left->value.point)) == 0;
	case SG_RUNE_COMPACT_DESTINATION_CELL:
		return left->value.cell.value == right->value.cell.value;
	case SG_RUNE_COMPACT_DESTINATION_SURFACE:
		return left->value.surface.value == right->value.surface.value;
	case SG_RUNE_COMPACT_DESTINATION_ITEM:
		return left->value.item.value == right->value.item.value;
	case SG_RUNE_COMPACT_DESTINATION_KIND_COUNT:
	default:
		return 0;
	}
}

static int SemanticDestinationEqual(const sg_destination_ref_t *left,
	const sg_destination_ref_t *right)
{
	if (left == NULL || right == NULL || left->kind != right->kind)
		return 0;
	switch (left->kind)
	{
	case SG_DESTINATION_FLAG:
		return left->value.flag.team == right->value.flag.team &&
			left->value.flag.location == right->value.flag.location;
	case SG_DESTINATION_ITEM:
	case SG_DESTINATION_WEAPON:
	case SG_DESTINATION_ARMOR:
	case SG_DESTINATION_POWERUP:
		return left->value.item.item_id == right->value.item.item_id;
	case SG_DESTINATION_CARRIER:
	case SG_DESTINATION_ESCORT:
	case SG_DESTINATION_INTERCEPT:
		return left->value.carrier.client_id ==
				right->value.carrier.client_id &&
			left->value.carrier.team == right->value.carrier.team &&
			left->value.carrier.selector == right->value.carrier.selector;
	case SG_DESTINATION_DEFENSIVE_POST:
		return left->value.post.region_id == right->value.post.region_id;
	case SG_DESTINATION_LEARNED_POINT:
	case SG_DESTINATION_WAYPOINT:
		return left->value.point.point_id == right->value.point.point_id;
	case SG_DESTINATION_KIND_COUNT:
	default:
		return 0;
	}
}

/* Target IDs belong to one strategy plan, not to the process. Keep the
 * semantic destination in the cache key so concurrent plans cannot reuse a
 * field for a different destination that happened to use the same ID. */
static int TargetKeyEqual(const sg_rune_compact_field_target_t *left,
	const sg_rune_compact_field_target_t *right)
{
	return left != NULL && right != NULL &&
		left->target_id == right->target_id && left->motion == right->motion &&
		SemanticDestinationEqual(&left->semantic_destination,
			&right->semantic_destination);
}

static int TargetVersionEqual(const sg_rune_compact_field_target_t *left,
	const sg_rune_compact_field_target_t *right)
{
	return TargetKeyEqual(left, right) &&
		left->target_generation == right->target_generation &&
		DestinationEqual(&left->destination, &right->destination);
}

static int TargetEqual(const sg_rune_compact_field_target_t *left,
	const sg_rune_compact_field_target_t *right)
{
	return TargetVersionEqual(left, right);
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

static int TargetValid(const sg_rune_compact_field_service_t *service,
	const sg_rune_compact_field_target_t *target)
{
	if (!ServiceValid(service) || target == NULL || target->target_id == 0U ||
		target->target_generation == 0U || !TargetMotionValid(target->motion) ||
		!SG_DestinationRefValid(&target->semantic_destination))
		return 0;
	if (target->motion == SG_RUNE_COMPACT_FIELD_TARGET_STATIC &&
		target->target_generation != 1U)
		return 0;
	switch (target->destination.kind)
	{
	case SG_RUNE_COMPACT_DESTINATION_POINT:
	case SG_RUNE_COMPACT_DESTINATION_CELL:
	case SG_RUNE_COMPACT_DESTINATION_SURFACE:
	case SG_RUNE_COMPACT_DESTINATION_ITEM:
		return 1;
	case SG_RUNE_COMPACT_DESTINATION_KIND_COUNT:
	default:
		return 0;
	}
}

static void CacheEntryDestroy(
	sg_rune_compact_field_service_cache_entry_t *entry)
{
	if (entry == NULL)
		return;
	SG_RuneCompactFieldPlanDestroy(entry->plan);
	free(entry);
}

static void RemoveUnleasedCacheEntries(
	sg_rune_compact_field_service_t *service)
{
	sg_rune_compact_field_service_cache_entry_t **link;

	if (!ServiceValid(service))
		return;
	link = &service->cache;
	while (*link != NULL)
	{
		sg_rune_compact_field_service_cache_entry_t *entry = *link;

		if (!entry->current && entry->lease_count == 0U &&
			entry->borrower_count == 0U)
		{
			*link = entry->next;
			CacheEntryDestroy(entry);
		}
		else
			link = &entry->next;
	}
}

static void Increment(uint64_t *value)
{
	if (value != NULL && *value != UINT64_MAX)
		(*value)++;
}

static void Accumulate(uint64_t *value, uint64_t amount)
{
	if (value == NULL || *value == UINT64_MAX)
		return;
	if (amount > UINT64_MAX - *value)
		*value = UINT64_MAX;
	else
		*value += amount;
}

static uint32_t DestinationRegion(
	const sg_rune_compact_field_service_t *service,
	const sg_rune_compact_destination_t *destination)
{
	if (!ServiceValid(service) || destination == NULL)
		return SG_RUNE_COMPACT_INDEX_NONE;
	return SG_RuneCompactFieldDestinationRegion(service->field, destination);
}

static int Q8FromFloat(float value, int32_t *output)
{
	double scaled;
	double rounded;

	if (output == NULL || !isfinite(value))
		return 0;
	scaled = (double)value * 8.0;
	rounded = nearbyint(scaled);
	if (!isfinite(scaled) || !isfinite(rounded) ||
		rounded < (double)INT32_MIN || rounded > (double)INT32_MAX)
		return 0;
	*output = (int32_t)rounded;
	return 1;
}

static float Q8ToFloat(int32_t value)
{
	const float result = (float)value / 8.0f;

	return result == 0.0f ? 0.0f : result;
}

static int SemanticLandmarkKindMatches(sg_destination_kind_t kind,
	sg_rune_compact_landmark_kind_t landmark_kind)
{
	switch (kind)
	{
	case SG_DESTINATION_FLAG:
		return landmark_kind == SG_RUNE_COMPACT_LANDMARK_FLAG;
	case SG_DESTINATION_ITEM:
		return landmark_kind == SG_RUNE_COMPACT_LANDMARK_WEAPON ||
			landmark_kind == SG_RUNE_COMPACT_LANDMARK_AMMO ||
			landmark_kind == SG_RUNE_COMPACT_LANDMARK_ARMOR ||
			landmark_kind == SG_RUNE_COMPACT_LANDMARK_HEALTH ||
			landmark_kind == SG_RUNE_COMPACT_LANDMARK_POWERUP;
	case SG_DESTINATION_WEAPON:
		return landmark_kind == SG_RUNE_COMPACT_LANDMARK_WEAPON;
	case SG_DESTINATION_ARMOR:
		return landmark_kind == SG_RUNE_COMPACT_LANDMARK_ARMOR;
	case SG_DESTINATION_POWERUP:
		return landmark_kind == SG_RUNE_COMPACT_LANDMARK_POWERUP;
	case SG_DESTINATION_DEFENSIVE_POST:
		return landmark_kind == SG_RUNE_COMPACT_LANDMARK_DEFENSIVE_POSITION;
	case SG_DESTINATION_CARRIER:
	case SG_DESTINATION_ESCORT:
	case SG_DESTINATION_INTERCEPT:
	case SG_DESTINATION_LEARNED_POINT:
	case SG_DESTINATION_WAYPOINT:
	case SG_DESTINATION_KIND_COUNT:
	default:
		return 0;
	}
}

static int SemanticNeedsLivePose(const sg_destination_ref_t *destination)
{
	if (destination == NULL)
		return 0;
	switch (destination->kind)
	{
	case SG_DESTINATION_FLAG:
		return destination->value.flag.location ==
			SG_DESTINATION_FLAG_CURRENT;
	case SG_DESTINATION_CARRIER:
	case SG_DESTINATION_ESCORT:
	case SG_DESTINATION_INTERCEPT:
	case SG_DESTINATION_LEARNED_POINT:
	case SG_DESTINATION_WAYPOINT:
		return 1;
	case SG_DESTINATION_ITEM:
	case SG_DESTINATION_WEAPON:
	case SG_DESTINATION_ARMOR:
	case SG_DESTINATION_POWERUP:
	case SG_DESTINATION_DEFENSIVE_POST:
	case SG_DESTINATION_KIND_COUNT:
	default:
		return 0;
	}
}

static int FindSemanticLandmark(
	const sg_rune_compact_field_service_t *service,
	const sg_destination_ref_t *destination, uint32_t *index_out)
{
	const sg_rune_compact_static_t *static_data;
	uint32_t wanted_entity = UINT32_MAX;
	uint16_t wanted_variant = 0U;
	uint32_t index;
	int found = 0;

	if (!ServiceValid(service) || destination == NULL || index_out == NULL ||
		!SG_DestinationRefValid(destination))
		return 0;
	static_data = service->model->static_data;
	if (static_data == NULL)
		return 0;
	if (destination->kind == SG_DESTINATION_FLAG)
		wanted_variant = destination->value.flag.team == 2U ? 1U : 0U;
	else if (destination->kind == SG_DESTINATION_ITEM ||
		destination->kind == SG_DESTINATION_WEAPON ||
		destination->kind == SG_DESTINATION_ARMOR ||
		destination->kind == SG_DESTINATION_POWERUP)
	{
		const uint64_t item_id = destination->value.item.item_id;

		if (item_id == 0U || item_id - 1U > (uint64_t)UINT32_MAX)
			return 0;
		wanted_entity = (uint32_t)(item_id - 1U);
	}
	else if (destination->kind == SG_DESTINATION_DEFENSIVE_POST &&
		destination->value.post.region_id > (uint32_t)UINT16_MAX)
		wanted_entity = destination->value.post.region_id;
	for (index = 0U; index < static_data->landmark_count; index++)
	{
		const sg_rune_compact_landmark_t *landmark =
			&static_data->landmarks[index];
		int match = SemanticLandmarkKindMatches(destination->kind,
			landmark->kind);

		if (!match)
			continue;
		if (destination->kind == SG_DESTINATION_FLAG)
			match = landmark->variant == wanted_variant;
		else if (destination->kind == SG_DESTINATION_ITEM ||
			destination->kind == SG_DESTINATION_WEAPON ||
			destination->kind == SG_DESTINATION_ARMOR ||
			destination->kind == SG_DESTINATION_POWERUP)
			match = landmark->source.entity_ordinal == wanted_entity;
		else if (destination->kind == SG_DESTINATION_DEFENSIVE_POST)
			match = landmark->variant ==
				destination->value.post.region_id ||
				landmark->source.entity_ordinal == wanted_entity;
		if (!match)
			continue;
		if (found)
			return 0;
		found = 1;
		*index_out = index;
	}
	return found;
}

static int CompactDestinationCell(
	const sg_rune_compact_field_service_t *service,
	const sg_rune_compact_destination_t *destination,
	uint32_t *cell_out)
{
	const sg_rune_compact_static_t *static_data;
	uint32_t cell = SG_RUNE_COMPACT_INDEX_NONE;

	if (!ServiceValid(service) || destination == NULL || cell_out == NULL)
		return 0;
	static_data = service->model->static_data;
	switch (destination->kind)
	{
	case SG_RUNE_COMPACT_DESTINATION_POINT:
	{
		sg_rune_compact_location_t location;

		if (SG_RuneCompactLocalize(service->model, &destination->value.point,
			&location) != SG_RUNE_COMPACT_LOCALIZE_OK)
			return 0;
		cell = location.cell.value;
		break;
	}
	case SG_RUNE_COMPACT_DESTINATION_CELL:
		cell = destination->value.cell.value;
		break;
	case SG_RUNE_COMPACT_DESTINATION_SURFACE:
		if (destination->value.surface.value >= service->model->incidence_count)
			return 0;
		cell = service->model->incidences[
			destination->value.surface.value].cell.value;
		break;
	case SG_RUNE_COMPACT_DESTINATION_ITEM:
		if (static_data == NULL || destination->value.item.value >=
			static_data->landmark_count)
			return 0;
		if (static_data->landmarks[destination->value.item.value].cells.count == 0U)
			return 0;
		cell = static_data->landmark_cells[
			static_data->landmarks[destination->value.item.value].cells.first].value;
		break;
	case SG_RUNE_COMPACT_DESTINATION_KIND_COUNT:
	default:
		return 0;
	}
	if (cell >= service->model->cell_count)
		return 0;
	*cell_out = cell;
	return 1;
}

static uint32_t FirstDestinationRegion(
	const sg_rune_compact_field_service_t *service,
	const sg_rune_compact_destination_t *destination)
{
	uint32_t cell;
	uint32_t region;

	region = DestinationRegion(service, destination);
	if (region != SG_RUNE_COMPACT_INDEX_NONE)
		return region;
	if (!CompactDestinationCell(service, destination, &cell))
		return SG_RUNE_COMPACT_INDEX_NONE;
	return SG_RuneCompactFieldCellRegion(service->field, cell);
}

static void TouchRegion(sg_rune_compact_field_service_t *service,
	uint32_t region)
{
	if (region < service->region_count)
	{
		Increment(&service->region_epochs[region]);
		Increment(&service->stats.region_refreshes);
	}
}

static sg_rune_compact_field_service_cache_entry_t *FindCurrentEntry(
	const sg_rune_compact_field_service_t *service,
	const sg_rune_compact_field_target_t *target)
{
	sg_rune_compact_field_service_cache_entry_t *entry;

	if (service == NULL || target == NULL)
		return NULL;
	for (entry = service->cache; entry != NULL; entry = entry->next)
		if (entry->current && TargetKeyEqual(&entry->target, target))
			return entry;
	return NULL;
}

static sg_rune_compact_field_service_status_t EnsureEntry(
	sg_rune_compact_field_service_t *service,
	const sg_rune_compact_field_target_t *target,
	sg_rune_compact_field_service_cache_entry_t **entry_out)
{
	sg_rune_compact_field_service_cache_entry_t *previous;
	sg_rune_compact_field_service_cache_entry_t *entry;
	sg_rune_compact_destination_plan_t *plan = NULL;
	sg_rune_compact_field_refresh_report_t refresh_report;
	uint32_t region;
	int incremental = 0;

	if (!ServiceValid(service) || !TargetValid(service, target) ||
		entry_out == NULL)
		return SG_RUNE_COMPACT_FIELD_SERVICE_INVALID_ARGUMENT;
	*entry_out = NULL;
	previous = FindCurrentEntry(service, target);
	if (previous != NULL)
	{
		if (TargetEqual(&previous->target, target))
		{
			if (target->motion == SG_RUNE_COMPACT_FIELD_TARGET_STATIC)
				Increment(&service->stats.static_cache_hits);
			else
				Increment(&service->stats.moving_cache_hits);
			*entry_out = previous;
			return SG_RUNE_COMPACT_FIELD_SERVICE_OK;
		}
		if (target->target_generation < previous->target.target_generation)
			return SG_RUNE_COMPACT_FIELD_SERVICE_STALE;
		if (target->target_generation == previous->target.target_generation)
			return SG_RUNE_COMPACT_FIELD_SERVICE_IDENTITY_MISMATCH;
	}
	memset(&refresh_report, 0, sizeof(refresh_report));
	if (previous != NULL &&
		target->motion == SG_RUNE_COMPACT_FIELD_TARGET_MOVING)
	{
		incremental = 1;
		if (SG_RuneCompactFieldPlanDerive(previous->plan,
			&target->destination, &plan, &refresh_report) !=
			SG_RUNE_COMPACT_FIELD_OK)
			return SG_RUNE_COMPACT_FIELD_SERVICE_FIELD_FAILED;
	}
	else if (SG_RuneCompactFieldPlanCreate(service->field,
		&target->destination, &plan) != SG_RUNE_COMPACT_FIELD_OK)
		return SG_RUNE_COMPACT_FIELD_SERVICE_FIELD_FAILED;
	entry = calloc(1U, sizeof(*entry));
	if (entry == NULL)
	{
		SG_RuneCompactFieldPlanDestroy(plan);
		return SG_RUNE_COMPACT_FIELD_SERVICE_ALLOCATION_FAILED;
	}
	region = FirstDestinationRegion(service, &target->destination);
	entry->target = *target;
	entry->plan = plan;
	entry->coarse_region = region;
	entry->current = 1U;
	if (previous != NULL)
	{
		previous->current = 0U;
		TouchRegion(service, previous->coarse_region);
		if (entry->coarse_region != previous->coarse_region)
			TouchRegion(service, entry->coarse_region);
		if (target->motion == SG_RUNE_COMPACT_FIELD_TARGET_MOVING)
			Increment(&service->stats.moving_plan_rebuilds);
	}
	entry->coarse_region_epoch = region < service->region_count ?
		service->region_epochs[region] : 0U;
	entry->next = service->cache;
	service->cache = entry;
	if (incremental)
	{
		Increment(&service->stats.incremental_plan_refreshes);
		Accumulate(&service->stats.incremental_affected_states,
			refresh_report.affected_state_count);
		Accumulate(&service->stats.incremental_affected_leaf_regions,
			refresh_report.affected_leaf_region_count);
		Accumulate(&service->stats.incremental_affected_coarse_regions,
			refresh_report.affected_coarse_region_count);
		Accumulate(&service->stats.incremental_examined_transitions,
			refresh_report.examined_transition_count);
	}
	else
		Increment(&service->stats.clean_plan_builds);
	RemoveUnleasedCacheEntries(service);
	*entry_out = entry;
	return SG_RUNE_COMPACT_FIELD_SERVICE_OK;
}

static sg_rune_compact_field_service_status_t ServiceHandleStatus(
	const sg_rune_compact_field_service_t *service,
	const sg_rune_compact_field_handle_t *handle)
{
	if (!ServiceValid(service) || handle == NULL)
		return SG_RUNE_COMPACT_FIELD_SERVICE_INVALID_ARGUMENT;
	if (handle->service_identity != service->identity ||
		handle->rune_identity != service->rune_identity ||
		handle->topology_revision != service->topology_revision)
		return SG_RUNE_COMPACT_FIELD_SERVICE_IDENTITY_MISMATCH;
	if (handle->service_generation != service->generation)
		return SG_RUNE_COMPACT_FIELD_SERVICE_STALE;
	return SG_RUNE_COMPACT_FIELD_SERVICE_OK;
}

static sg_rune_compact_field_service_lease_t *FindLeaseMutable(
	sg_rune_compact_field_service_t *service,
	const sg_rune_compact_field_handle_t *handle,
	sg_rune_compact_field_service_lease_t ***link_out)
{
	sg_rune_compact_field_service_lease_t **link;

	if (link_out != NULL)
		*link_out = NULL;
	if (!ServiceValid(service) || handle == NULL)
		return NULL;
	link = &service->leases;
	while (*link != NULL)
	{
		if (HandleEqual(&(*link)->handle, handle))
		{
			if (link_out != NULL)
				*link_out = link;
			return *link;
		}
		link = &(*link)->next;
	}
	return NULL;
}

static const sg_rune_compact_field_service_lease_t *FindLeaseConst(
	const sg_rune_compact_field_service_t *service,
	const sg_rune_compact_field_handle_t *handle)
{
	const sg_rune_compact_field_service_lease_t *lease;

	if (!ServiceValid(service) || handle == NULL)
		return NULL;
	for (lease = service->leases; lease != NULL; lease = lease->next)
		if (HandleEqual(&lease->handle, handle))
			return lease;
	return NULL;
}

static sg_rune_compact_field_service_status_t PrepareLease(
	const sg_rune_compact_field_service_t *service,
	sg_rune_compact_field_service_lease_t **lease_out)
{
	sg_rune_compact_field_service_lease_t *lease;

	if (lease_out != NULL)
		*lease_out = NULL;
	if (!ServiceValid(service) || lease_out == NULL)
		return SG_RUNE_COMPACT_FIELD_SERVICE_INVALID_ARGUMENT;
	if (service->next_field_generation == 0U ||
		service->next_field_generation == UINT64_MAX)
		return SG_RUNE_COMPACT_FIELD_SERVICE_CAPACITY;
	lease = calloc(1U, sizeof(*lease));
	if (lease == NULL)
		return SG_RUNE_COMPACT_FIELD_SERVICE_ALLOCATION_FAILED;
	*lease_out = lease;
	return SG_RUNE_COMPACT_FIELD_SERVICE_OK;
}

static void PublishLease(sg_rune_compact_field_service_t *service,
	sg_rune_compact_field_service_cache_entry_t *entry,
	sg_rune_compact_field_service_lease_t *lease,
	sg_rune_compact_field_handle_t *handle_out)
{
	lease->handle.service_identity = service->identity;
	lease->handle.service_generation = service->generation;
	lease->handle.rune_identity = service->rune_identity;
	lease->handle.topology_revision = service->topology_revision;
	lease->handle.target_id = entry->target.target_id;
	lease->handle.target_generation = entry->target.target_generation;
	lease->handle.field_generation = service->next_field_generation++;
	lease->entry = entry;
	lease->next = service->leases;
	service->leases = lease;
	entry->lease_count++;
	*handle_out = lease->handle;
}

static sg_rune_compact_field_service_status_t MintLease(
	sg_rune_compact_field_service_t *service,
	sg_rune_compact_field_service_cache_entry_t *entry,
	sg_rune_compact_field_handle_t *handle_out)
{
	sg_rune_compact_field_service_lease_t *lease;
	sg_rune_compact_field_service_status_t status;

	if (!ServiceValid(service) || entry == NULL || handle_out == NULL)
		return SG_RUNE_COMPACT_FIELD_SERVICE_INVALID_ARGUMENT;
	status = PrepareLease(service, &lease);
	if (status != SG_RUNE_COMPACT_FIELD_SERVICE_OK)
		return status;
	PublishLease(service, entry, lease, handle_out);
	return SG_RUNE_COMPACT_FIELD_SERVICE_OK;
}

static sg_rune_compact_field_service_status_t ReleaseLease(
	sg_rune_compact_field_service_t *service,
	const sg_rune_compact_field_handle_t *handle)
{
	sg_rune_compact_field_service_lease_t **link;
	sg_rune_compact_field_service_lease_t *lease;

	lease = FindLeaseMutable(service, handle, &link);
	if (lease == NULL || link == NULL)
		return SG_RUNE_COMPACT_FIELD_SERVICE_STALE;
	*link = lease->next;
	if (lease->entry->lease_count != 0U)
		lease->entry->lease_count--;
	free(lease);
	RemoveUnleasedCacheEntries(service);
	return SG_RUNE_COMPACT_FIELD_SERVICE_OK;
}

sg_rune_compact_field_service_status_t
SG_RuneCompactFieldServiceCreate(
	const sg_rune_compact_model_t *model,
	const sg_rune_compact_identity_t *expected_identity,
	uint64_t rune_identity, uint64_t topology_revision,
	sg_rune_compact_field_service_t **service_out,
	sg_rune_compact_error_t *model_error_out)
{
	sg_rune_compact_field_service_t *service;
	sg_rune_compact_error_t local_error;
	sg_rune_compact_field_status_t field_status;
	uint64_t identity;
	uint32_t region;

	if (service_out != NULL)
		*service_out = NULL;
	if (model == NULL || expected_identity == NULL || service_out == NULL ||
		rune_identity == 0U || topology_revision == 0U)
		return SG_RUNE_COMPACT_FIELD_SERVICE_INVALID_ARGUMENT;
	if (!SG_RuneCompactModelValidateBound(model, expected_identity,
		model_error_out != NULL ? model_error_out : &local_error))
		return SG_RUNE_COMPACT_FIELD_SERVICE_IDENTITY_MISMATCH;
	service = calloc(1U, sizeof(*service));
	if (service == NULL)
		return SG_RUNE_COMPACT_FIELD_SERVICE_ALLOCATION_FAILED;
	if (!NextIdentity(&identity))
	{
		free(service);
		return SG_RUNE_COMPACT_FIELD_SERVICE_CAPACITY;
	}
	field_status = SG_RuneCompactFieldCreate(model, expected_identity,
		&service->field, model_error_out);
	if (field_status != SG_RUNE_COMPACT_FIELD_OK)
	{
		SG_RuneCompactFieldDestroy(service->field);
		free(service);
		return field_status == SG_RUNE_COMPACT_FIELD_ALLOCATION_FAILED ?
			SG_RUNE_COMPACT_FIELD_SERVICE_ALLOCATION_FAILED :
			SG_RUNE_COMPACT_FIELD_SERVICE_FIELD_FAILED;
	}
	service->region_count = SG_RuneCompactFieldRegionCount(service->field);
	service->region_epochs = calloc((size_t)service->region_count,
		sizeof(*service->region_epochs));
	if (service->region_count == 0U || service->region_epochs == NULL)
	{
		SG_RuneCompactFieldDestroy(service->field);
		free(service->region_epochs);
		free(service);
		return SG_RUNE_COMPACT_FIELD_SERVICE_ALLOCATION_FAILED;
	}
	for (region = 0U; region < service->region_count; region++)
		service->region_epochs[region] = 1U;
	service->magic = SG_COMPACT_FIELD_SERVICE_MAGIC;
	service->identity = identity;
	service->generation = 1U;
	service->next_field_generation = 1U;
	service->model = model;
	service->expected_identity = *expected_identity;
	service->rune_identity = rune_identity;
	service->topology_revision = topology_revision;
	service->stats.coarse_region_count = service->region_count;
	*service_out = service;
	return SG_RUNE_COMPACT_FIELD_SERVICE_OK;
}

void SG_RuneCompactFieldServiceDestroy(
	sg_rune_compact_field_service_t *service)
{
	sg_rune_compact_field_service_cache_entry_t *entry;
	sg_rune_compact_field_service_lease_t *lease;
	sg_rune_compact_field_service_token_t *token;

	if (service == NULL)
		return;
	if (service->magic != SG_COMPACT_FIELD_SERVICE_MAGIC)
	{
		free(service);
		return;
	}
	service->magic = 0U;
	while ((lease = service->leases) != NULL)
	{
		service->leases = lease->next;
		free(lease);
	}
	while ((token = service->tokens) != NULL)
	{
		service->tokens = token->next;
		free(token);
	}
	while ((entry = service->cache) != NULL)
	{
		service->cache = entry->next;
		CacheEntryDestroy(entry);
	}
	SG_RuneCompactFieldDestroy(service->field);
	free(service->region_epochs);
	free(service);
}

sg_rune_compact_field_service_status_t
SG_RuneCompactFieldServiceResolve(
	sg_rune_compact_field_service_t *service,
	const sg_rune_compact_field_target_t *target,
	sg_rune_compact_field_handle_t *handle_out)
{
	sg_rune_compact_field_service_cache_entry_t *entry;
	sg_rune_compact_field_service_lease_t *lease;
	sg_rune_compact_field_service_status_t status;

	if (handle_out != NULL)
		memset(handle_out, 0, sizeof(*handle_out));
	if (!ServiceValid(service) || target == NULL || handle_out == NULL)
		return SG_RUNE_COMPACT_FIELD_SERVICE_INVALID_ARGUMENT;
	if (!TargetValid(service, target))
		return SG_RUNE_COMPACT_FIELD_SERVICE_INVALID_ARGUMENT;
	status = PrepareLease(service, &lease);
	if (status != SG_RUNE_COMPACT_FIELD_SERVICE_OK)
		return status;
	status = EnsureEntry(service, target, &entry);
	if (status != SG_RUNE_COMPACT_FIELD_SERVICE_OK)
	{
		free(lease);
		return status;
	}
	PublishLease(service, entry, lease, handle_out);
	return SG_RUNE_COMPACT_FIELD_SERVICE_OK;
}

sg_rune_compact_field_service_status_t
SG_RuneCompactFieldServiceRefresh(
	sg_rune_compact_field_service_t *service,
	const sg_rune_compact_field_handle_t *previous,
	const sg_rune_compact_field_target_t *target,
	sg_rune_compact_field_handle_t *handle_out)
{
	sg_rune_compact_field_service_lease_t *old_lease;
	sg_rune_compact_field_service_status_t status;

	if (handle_out != NULL)
		memset(handle_out, 0, sizeof(*handle_out));
	if (!ServiceValid(service) || previous == NULL || target == NULL ||
		handle_out == NULL)
		return SG_RUNE_COMPACT_FIELD_SERVICE_INVALID_ARGUMENT;
	status = ServiceHandleStatus(service, previous);
	if (status != SG_RUNE_COMPACT_FIELD_SERVICE_OK)
		return status;
	old_lease = FindLeaseMutable(service, previous, NULL);
	if (old_lease == NULL || old_lease->entry == NULL)
		return SG_RUNE_COMPACT_FIELD_SERVICE_STALE;
	if (target->target_id != old_lease->entry->target.target_id ||
		target->motion != old_lease->entry->target.motion)
		return SG_RUNE_COMPACT_FIELD_SERVICE_IDENTITY_MISMATCH;
	if (!TargetValid(service, target))
		return SG_RUNE_COMPACT_FIELD_SERVICE_INVALID_TARGET;
	if (target->target_generation < old_lease->entry->target.target_generation)
		return SG_RUNE_COMPACT_FIELD_SERVICE_STALE;
	if (target->target_generation == old_lease->entry->target.target_generation &&
		!TargetVersionEqual(&old_lease->entry->target, target))
		return SG_RUNE_COMPACT_FIELD_SERVICE_IDENTITY_MISMATCH;
	status = SG_RuneCompactFieldServiceResolve(service, target, handle_out);
	if (status != SG_RUNE_COMPACT_FIELD_SERVICE_OK)
		return status;
	status = ReleaseLease(service, previous);
	if (status != SG_RUNE_COMPACT_FIELD_SERVICE_OK)
	{
		(void)ReleaseLease(service, handle_out);
		memset(handle_out, 0, sizeof(*handle_out));
	}
	return status;
}

sg_rune_compact_field_service_status_t
SG_RuneCompactFieldServiceQuery(
	const sg_rune_compact_field_service_t *service,
	const sg_rune_compact_field_handle_t *handle,
	const sg_rune_compact_field_local_context_t *context,
	sg_rune_compact_field_result_t *result_out)
{
	const sg_rune_compact_field_service_lease_t *lease;
	sg_rune_compact_field_result_t result;

	if (!ServiceValid(service) || handle == NULL || context == NULL ||
		result_out == NULL)
		return SG_RUNE_COMPACT_FIELD_SERVICE_INVALID_ARGUMENT;
	if (ServiceHandleStatus(service, handle) !=
		SG_RUNE_COMPACT_FIELD_SERVICE_OK)
		return ServiceHandleStatus(service, handle);
	lease = FindLeaseConst(service, handle);
	if (lease == NULL || lease->entry == NULL)
		return SG_RUNE_COMPACT_FIELD_SERVICE_STALE;
	if (SG_RuneCompactFieldQuery(lease->entry->plan, context, &result) !=
		SG_RUNE_COMPACT_FIELD_OK)
		return SG_RUNE_COMPACT_FIELD_SERVICE_FIELD_FAILED;
	*result_out = result;
	return SG_RUNE_COMPACT_FIELD_SERVICE_OK;
}

sg_rune_compact_field_service_status_t
SG_RuneCompactFieldServiceVisitExactStepProbes(
	sg_rune_compact_field_service_t *service,
	const sg_rune_compact_field_handle_t *handle,
	const sg_rune_compact_field_local_context_t *context,
	const sg_rune_compact_field_result_t *expected_result,
	sg_rune_compact_field_exact_probe_visit_fn visit, void *visit_context,
	uint32_t *probe_count_out)
{
	const sg_rune_compact_field_service_lease_t *lease;
	sg_rune_compact_field_service_cache_entry_t *entry;
	sg_rune_compact_field_status_t field_status;

	if (probe_count_out != NULL)
		*probe_count_out = 0U;
	if (!ServiceValid(service) || handle == NULL || context == NULL ||
		expected_result == NULL || visit == NULL || probe_count_out == NULL)
		return SG_RUNE_COMPACT_FIELD_SERVICE_INVALID_ARGUMENT;
	if (ServiceHandleStatus(service, handle) !=
		SG_RUNE_COMPACT_FIELD_SERVICE_OK)
		return ServiceHandleStatus(service, handle);
	lease = FindLeaseConst(service, handle);
	if (lease == NULL || lease->entry == NULL)
		return SG_RUNE_COMPACT_FIELD_SERVICE_STALE;
	entry = lease->entry;
	if (entry->borrower_count == UINT32_MAX)
		return SG_RUNE_COMPACT_FIELD_SERVICE_CAPACITY;
	/* The callback is owner code and may release or refresh this lease.  Pin
	 * the immutable plan independently until field visitation returns. */
	entry->borrower_count++;
	field_status = SG_RuneCompactFieldPlanVisitExactStepProbes(
		entry->plan, context, expected_result, visit, visit_context,
		probe_count_out);
	entry->borrower_count--;
	RemoveUnleasedCacheEntries(service);
	return field_status == SG_RUNE_COMPACT_FIELD_OK ?
		SG_RUNE_COMPACT_FIELD_SERVICE_OK :
		SG_RUNE_COMPACT_FIELD_SERVICE_FIELD_FAILED;
}

sg_rune_compact_field_service_status_t
SG_RuneCompactFieldServiceRelease(
	sg_rune_compact_field_service_t *service,
	const sg_rune_compact_field_handle_t *handle)
{
	sg_rune_compact_field_service_status_t status;

	status = ServiceHandleStatus(service, handle);
	if (status != SG_RUNE_COMPACT_FIELD_SERVICE_OK)
		return status;
	return ReleaseLease(service, handle);
}

int SG_RuneCompactFieldServiceHandleCurrent(
	const sg_rune_compact_field_service_t *service,
	const sg_rune_compact_field_handle_t *handle,
	sg_rune_compact_field_target_t *target_out,
	uint32_t *coarse_region_out,
	uint64_t *coarse_region_epoch_out)
{
	const sg_rune_compact_field_service_lease_t *lease;

	if (target_out != NULL)
		memset(target_out, 0, sizeof(*target_out));
	if (coarse_region_out != NULL)
		*coarse_region_out = SG_RUNE_COMPACT_INDEX_NONE;
	if (coarse_region_epoch_out != NULL)
		*coarse_region_epoch_out = 0U;
	if (ServiceHandleStatus(service, handle) !=
		SG_RUNE_COMPACT_FIELD_SERVICE_OK)
		return 0;
	lease = FindLeaseConst(service, handle);
	if (lease == NULL || lease->entry == NULL || !lease->entry->current)
		return 0;
	if (target_out != NULL)
		*target_out = lease->entry->target;
	if (coarse_region_out != NULL)
		*coarse_region_out = lease->entry->coarse_region;
	if (coarse_region_epoch_out != NULL)
		*coarse_region_epoch_out = lease->entry->coarse_region_epoch;
	return 1;
}

sg_rune_compact_field_service_status_t
SG_RuneCompactFieldServiceInvalidateTarget(
	sg_rune_compact_field_service_t *service, uint64_t target_id)
{
	sg_rune_compact_field_service_cache_entry_t *entry;
	int invalidated = 0;

	if (!ServiceValid(service) || target_id == 0U)
		return SG_RUNE_COMPACT_FIELD_SERVICE_INVALID_ARGUMENT;
	for (entry = service->cache; entry != NULL; entry = entry->next)
	{
		if (!entry->current || entry->target.target_id != target_id)
			continue;
		entry->current = 0U;
		TouchRegion(service, entry->coarse_region);
		invalidated = 1;
	}
	if (!invalidated)
		return SG_RUNE_COMPACT_FIELD_SERVICE_STALE;
	RemoveUnleasedCacheEntries(service);
	return SG_RUNE_COMPACT_FIELD_SERVICE_OK;
}

void SG_RuneCompactFieldServiceStats(
	const sg_rune_compact_field_service_t *service,
	sg_rune_compact_field_service_stats_t *stats_out)
{
	if (stats_out == NULL)
		return;
	memset(stats_out, 0, sizeof(*stats_out));
	if (!ServiceValid(service))
		return;
	*stats_out = service->stats;
	for (const sg_rune_compact_field_service_cache_entry_t *entry =
		service->cache; entry != NULL; entry = entry->next)
		stats_out->cached_field_count++;
	for (const sg_rune_compact_field_service_lease_t *lease = service->leases;
		lease != NULL; lease = lease->next)
		stats_out->lease_count++;
}

const sg_rune_compact_model_t *SG_RuneCompactFieldServiceModel(
	const sg_rune_compact_field_service_t *service)
{
	return ServiceValid(service) ? service->model : NULL;
}

uint64_t SG_RuneCompactFieldServiceIdentity(
	const sg_rune_compact_field_service_t *service)
{
	return ServiceValid(service) ? service->identity : 0U;
}

uint64_t SG_RuneCompactFieldServiceGeneration(
	const sg_rune_compact_field_service_t *service)
{
	return ServiceValid(service) ? service->generation : 0U;
}

uint32_t SG_RuneCompactFieldServicePortalRootCount(
	const sg_rune_compact_field_service_t *service)
{
	return ServiceValid(service) ?
		SG_RuneCompactFieldPortalRootCount(service->field) : 0U;
}

int SG_RuneCompactFieldServicePortalRootAt(
	const sg_rune_compact_field_service_t *service, uint32_t root_index,
	sg_rune_compact_portal_index_t *portal_out,
	sg_rune_compact_mechanism_index_t *mechanism_out)
{
	return ServiceValid(service) && SG_RuneCompactFieldPortalRootAt(
		service->field, root_index, portal_out, mechanism_out);
}

int SG_RuneCompactFieldServiceResolveSemanticTarget(
	const sg_rune_compact_field_service_t *service,
	uint64_t target_id,
	const sg_destination_ref_t *destination,
	const sg_rune_compact_field_service_live_pose_t *live_pose,
	sg_rune_compact_field_target_t *target_out,
	sg_destination_handle_t *handle_out)
{
	sg_rune_compact_destination_t compact_destination;
	sg_rune_compact_field_service_live_pose_t pose;
	const sg_rune_compact_landmark_t *landmark = NULL;
	sg_destination_motion_t motion;
	uint32_t landmark_index = SG_RUNE_COMPACT_INDEX_NONE;
	uint32_t cell;
	uint32_t region;
	uint64_t generation;
	uint32_t axis;

	if (target_out != NULL)
		memset(target_out, 0, sizeof(*target_out));
	if (handle_out != NULL)
		memset(handle_out, 0, sizeof(*handle_out));
	if (!ServiceValid(service) || target_id == 0U || destination == NULL ||
		target_out == NULL || handle_out == NULL ||
		!SG_DestinationRefValid(destination))
		return 0;
	memset(&pose, 0, sizeof(pose));
	if (live_pose != NULL)
		pose = *live_pose;
	for (axis = 0U; axis < 7U; axis++)
		if (pose.reserved[axis] != 0U)
			return 0;
	if (SemanticNeedsLivePose(destination))
	{
		if (live_pose == NULL || pose.present != 1U || pose.generation == 0U ||
			pose.observed_at_ms == 0U)
			return 0;
		for (axis = 0U; axis < 3U; axis++)
			if (!isfinite(pose.position[axis]) ||
				!isfinite(pose.velocity[axis]))
				return 0;
	}
	else if (pose.present != 0U)
		return 0;

	memset(&compact_destination, 0, sizeof(compact_destination));
	if (!SemanticNeedsLivePose(destination))
	{
		if (!FindSemanticLandmark(service, destination, &landmark_index))
			return 0;
		landmark = &service->model->static_data->landmarks[landmark_index];
		if (destination->kind == SG_DESTINATION_ITEM ||
			destination->kind == SG_DESTINATION_WEAPON ||
			destination->kind == SG_DESTINATION_ARMOR ||
			destination->kind == SG_DESTINATION_POWERUP)
		{
			compact_destination.kind = SG_RUNE_COMPACT_DESTINATION_ITEM;
			compact_destination.value.item.value = landmark_index;
		}
		else
		{
			compact_destination.kind = SG_RUNE_COMPACT_DESTINATION_POINT;
			compact_destination.value.point = landmark->origin;
		}
		motion = SG_DESTINATION_STATIC;
		generation = 1U;
	}
	else
	{
		compact_destination.kind = SG_RUNE_COMPACT_DESTINATION_POINT;
		for (axis = 0U; axis < 3U; axis++)
			if (!Q8FromFloat(pose.position[axis],
				&compact_destination.value.point.value[axis]))
				return 0;
		motion = SG_DESTINATION_MOVING;
		generation = pose.generation;
	}
	if (!CompactDestinationCell(service, &compact_destination, &cell))
		return 0;
	region = FirstDestinationRegion(service, &compact_destination);
	if (region == SG_RUNE_COMPACT_INDEX_NONE ||
		region >= service->region_count)
		return 0;
	target_out->target_id = target_id;
	target_out->target_generation = generation;
	target_out->motion = motion == SG_DESTINATION_MOVING ?
		SG_RUNE_COMPACT_FIELD_TARGET_MOVING : SG_RUNE_COMPACT_FIELD_TARGET_STATIC;
	target_out->semantic_destination = *destination;
	target_out->destination = compact_destination;
	handle_out->id = target_id;
	handle_out->generation = generation;
	handle_out->kind = destination->kind;
	handle_out->motion = motion;
	handle_out->valid = 1U;
	handle_out->pose.phase.phase_id = 0U;
	handle_out->pose.phase.cell_id = cell;
	handle_out->pose.region_id = region;
	if (motion == SG_DESTINATION_MOVING)
	{
		for (axis = 0U; axis < 3U; axis++)
		{
			handle_out->pose.position[axis] = pose.position[axis] == 0.0f ?
				0.0f : pose.position[axis];
			handle_out->pose.velocity[axis] = pose.velocity[axis] == 0.0f ?
				0.0f : pose.velocity[axis];
		}
		handle_out->pose.sample_time_ms = pose.observed_at_ms;
	}
	else
	{
		for (axis = 0U; axis < 3U; axis++)
		{
			const int32_t value = compact_destination.kind ==
				SG_RUNE_COMPACT_DESTINATION_POINT ?
				compact_destination.value.point.value[axis] :
				landmark->origin.value[axis];

			handle_out->pose.position[axis] = Q8ToFloat(value);
			handle_out->pose.velocity[axis] = 0.0f;
		}
		handle_out->pose.sample_time_ms = 0U;
	}
	return SG_DestinationHandleValid(handle_out);
}

static sg_rune_compact_field_service_token_t *FindToken(
	sg_rune_compact_field_service_t *service, const void *opaque,
	sg_rune_compact_field_service_token_t ***link_out)
{
	sg_rune_compact_field_service_token_t **link;

	if (link_out != NULL)
		*link_out = NULL;
	if (!ServiceValid(service) || opaque == NULL)
		return NULL;
	link = &service->tokens;
	while (*link != NULL)
	{
		if ((const void *)*link == opaque)
		{
			if (link_out != NULL)
				*link_out = link;
			return *link;
		}
		link = &(*link)->next;
	}
	return NULL;
}

static void RejectToken(sg_rune_compact_field_service_t *service,
	sg_rune_compact_field_service_token_t *token)
{
	sg_rune_compact_field_service_token_t **link = NULL;

	if (!ServiceValid(service) || token == NULL || token->accepted)
		return;
	if (FindToken(service, token, &link) != token || link == NULL)
		return;
	if (token->entry != NULL && token->entry->borrower_count != 0U)
		token->entry->borrower_count--;
	*link = token->next;
	free(token);
	RemoveUnleasedCacheEntries(service);
}

int SG_RuneCompactFieldServiceProvider(
	sg_rune_compact_field_service_t *service,
	sg_rune_compact_field_service_provider_t *provider_out)
{
	if (provider_out != NULL)
		memset(provider_out, 0, sizeof(*provider_out));
	if (!ServiceValid(service) || provider_out == NULL)
		return 0;
	provider_out->locator = SG_RuneCompactFieldServiceTargetLocate;
	provider_out->authority = SG_RuneCompactFieldServiceTargetAuthorize;
	provider_out->release_view = SG_RuneCompactFieldServiceTargetRelease;
	provider_out->context = (void *)service;
	return 1;
}

int SG_RuneCompactFieldServiceTargetLocate(void *context,
	const sg_rune_compact_field_service_target_request_t *request,
	sg_rune_compact_field_service_target_view_t *view_out)
{
	sg_rune_compact_field_service_t *service = context;
	sg_rune_compact_field_service_cache_entry_t *entry;
	sg_rune_compact_field_service_token_t *token;

	if (view_out != NULL)
		memset(view_out, 0, sizeof(*view_out));
	if (!ServiceValid(service) || request == NULL || view_out == NULL ||
		request->commitment_id == 0U || !TargetValid(service, &request->target))
		return 0;
	if (EnsureEntry(service, &request->target, &entry) !=
		SG_RUNE_COMPACT_FIELD_SERVICE_OK)
		return 0;
	token = calloc(1U, sizeof(*token));
	if (token == NULL)
		return 0;
	token->service = service;
	token->entry = entry;
	token->request_commitment_id = request->commitment_id;
	token->request_target = request->target;
	token->next = service->tokens;
	service->tokens = token;
	entry->borrower_count++;
	view_out->opaque = token;
	return 1;
}

int SG_RuneCompactFieldServiceTargetAuthorize(void *context,
	const sg_rune_compact_field_service_target_request_t *request,
	const sg_rune_compact_field_service_target_view_t *view,
	sg_rune_compact_field_service_target_binding_t *binding_out)
{
	sg_rune_compact_field_service_t *service = context;
	sg_rune_compact_field_service_token_t *token;
	sg_rune_compact_field_handle_t handle;

	if (binding_out != NULL)
		memset(binding_out, 0, sizeof(*binding_out));
	if (!ServiceValid(service) || request == NULL || view == NULL ||
		binding_out == NULL || view->opaque == NULL)
		return 0;
	token = FindToken(service, view->opaque, NULL);
	if (token == NULL || token->accepted || token->service != service)
		return 0;
	if (request->commitment_id == 0U || !TargetValid(service, &request->target) ||
		token->request_commitment_id != request->commitment_id ||
		!TargetEqual(&token->request_target, &request->target) ||
		token->entry == NULL || !token->entry->current)
	{
		RejectToken(service, token);
		return 0;
	}
	if (MintLease(service, token->entry, &handle) !=
		SG_RUNE_COMPACT_FIELD_SERVICE_OK)
	{
		RejectToken(service, token);
		return 0;
	}
	token->accepted = 1U;
	token->handle = handle;
	binding_out->commitment_id = request->commitment_id;
	binding_out->target = request->target;
	binding_out->handle = handle;
	binding_out->accepted_view = view->opaque;
	binding_out->coarse_region = token->entry->coarse_region;
	binding_out->coarse_region_epoch = token->entry->coarse_region <
		service->region_count ? service->region_epochs[
			token->entry->coarse_region] : 0U;
	return 1;
}

void SG_RuneCompactFieldServiceTargetRelease(void *context,
	const void *accepted_view)
{
	sg_rune_compact_field_service_t *service = context;
	sg_rune_compact_field_service_token_t **link;
	sg_rune_compact_field_service_token_t *token;

	if (!ServiceValid(service) || accepted_view == NULL)
		return;
	token = FindToken(service, accepted_view, &link);
	if (token == NULL || link == NULL)
		return;
	if (token->accepted)
		(void)ReleaseLease(service, &token->handle);
	if (token->entry->borrower_count != 0U)
		token->entry->borrower_count--;
	*link = token->next;
	free(token);
	RemoveUnleasedCacheEntries(service);
}

const char *SG_RuneCompactFieldServiceStatusString(
	sg_rune_compact_field_service_status_t status)
{
	static const char *const names[
		SG_RUNE_COMPACT_FIELD_SERVICE_STATUS_COUNT] = {
		"ok",
		"invalid argument",
		"invalid service",
		"invalid target",
		"identity mismatch",
		"stale",
		"field failed",
		"allocation failed",
		"capacity"
	};

	return (uint32_t)status <
		(uint32_t)SG_RUNE_COMPACT_FIELD_SERVICE_STATUS_COUNT ? names[status] :
		"unknown compact field service status";
}
