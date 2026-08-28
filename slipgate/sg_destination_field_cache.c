#include "sg_destination_field_cache.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

typedef struct sg_field_cache_entry_s
{
	uint64_t serial;
	uint32_t newer;
	uint32_t older;
	uint8_t active;
	sg_destination_field_t field;
	sg_field_sample_t *samples;
} sg_field_cache_entry_t;

struct sg_destination_field_cache_s
{
	uint64_t rune_identity;
	uint64_t topology_revision;
	const sg_rune_model_t *model;
	const sg_phase_coordinate_t *phases;
	sg_rune_model_identity_t model_identity;
	uint32_t cell_count;
	uint32_t phase_count;
	uint32_t leaf_region_count;
	uint32_t level_count;
	uint32_t capacity;
	uint32_t count;
	uint32_t newest;
	uint32_t oldest;
	uint64_t next_serial;
	sg_field_cache_entry_t *entries;
	sg_field_region_level_t *levels;
	uint32_t *hierarchy_storage;
	uint32_t *phase_to_leaf_region;
	uint8_t *affected_phases;
	uint8_t *affected_leaf_regions;
	sg_field_region_ref_t *affected_regions;
	uint32_t affected_region_capacity;
	sg_destination_field_cache_stats_t stats;
};

static void SaturatingIncrement(uint64_t *value)
{
	if (*value != UINT64_MAX)
		(*value)++;
}

static int SizeMultiply(size_t left, size_t right, size_t *out)
{
	if (left != 0U && right > SIZE_MAX / left)
		return 0;
	*out = left * right;
	return 1;
}

static int VecEqual(const sg_rune_vec3_t *left, const sg_rune_vec3_t *right)
{
	uint32_t axis;

	for (axis = 0U; axis < 3U; axis++)
		if (left->value[axis] != right->value[axis])
			return 0;
	return 1;
}

static int HullEqual(const sg_rune_hull_profile_t *left,
	const sg_rune_hull_profile_t *right)
{
	return VecEqual(&left->mins, &right->mins) &&
		VecEqual(&left->maxs, &right->maxs);
}

static int PhysicsEqual(const sg_rune_physics_parameters_t *left,
	const sg_rune_physics_parameters_t *right)
{
	return left->gravity == right->gravity &&
		left->ground_acceleration == right->ground_acceleration &&
		left->air_acceleration == right->air_acceleration &&
		left->water_acceleration == right->water_acceleration &&
		left->hook_acceleration == right->hook_acceleration &&
		left->external_acceleration == right->external_acceleration &&
		left->water_drag == right->water_drag &&
		left->max_velocity == right->max_velocity &&
		left->frame_ms == right->frame_ms &&
		left->substep_ms == right->substep_ms;
}

static int ModelIdentityEqual(const sg_rune_model_identity_t *left,
	const sg_rune_model_identity_t *right)
{
	return left->bsp_content_id == right->bsp_content_id &&
		left->entity_semantics_id == right->entity_semantics_id &&
		left->physics_abi_id == right->physics_abi_id &&
		left->source_set_identity == right->source_set_identity &&
		left->schema_id == right->schema_id &&
		left->producer_identity == right->producer_identity &&
		HullEqual(&left->standing_hull, &right->standing_hull) &&
		HullEqual(&left->crouching_hull, &right->crouching_hull) &&
		PhysicsEqual(&left->physics, &right->physics);
}

/* Creation performs the O(n) snapshot validation. Runtime operations only
 * compare the already-validated binding, so a reference query is O(1). */
static int SnapshotBound(const sg_destination_field_cache_t *cache,
	const sg_rune_runtime_snapshot_t *snapshot)
{
	return cache && snapshot && snapshot->model &&
		snapshot->identity == cache->rune_identity &&
		snapshot->topology_revision == cache->topology_revision &&
		snapshot->model == cache->model &&
		snapshot->phases == cache->phases &&
		snapshot->cell_count == cache->cell_count &&
		snapshot->phase_count == cache->phase_count &&
		snapshot->region_count == cache->leaf_region_count &&
		ModelIdentityEqual(&snapshot->model->identity, &cache->model_identity);
}

static int CoordinateEqual(const sg_phase_coordinate_t *left,
	const sg_phase_coordinate_t *right)
{
	return left->phase_id == right->phase_id && left->cell_id == right->cell_id;
}

static int PoseEqual(const sg_destination_pose_t *left,
	const sg_destination_pose_t *right)
{
	uint32_t axis;

	if (!CoordinateEqual(&left->phase, &right->phase) ||
		left->sample_time_ms != right->sample_time_ms ||
		left->region_id != right->region_id)
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (left->position[axis] != right->position[axis] ||
			left->velocity[axis] != right->velocity[axis])
			return 0;
	return 1;
}

static int DestinationEqual(const sg_destination_handle_t *left,
	const sg_destination_handle_t *right)
{
	return left->id == right->id && left->generation == right->generation &&
		left->kind == right->kind && left->motion == right->motion &&
		left->valid == right->valid && PoseEqual(&left->pose, &right->pose);
}

static int EntryKeyEqual(const sg_field_cache_entry_t *entry,
	const sg_destination_handle_t *destination, uint64_t now_ms)
{
	return entry->active &&
		DestinationEqual(&entry->field.destination, destination) &&
		(destination->motion == SG_DESTINATION_STATIC ||
		 entry->field.computed_at_ms == now_ms);
}

static void Detach(sg_destination_field_cache_t *cache, uint32_t slot)
{
	sg_field_cache_entry_t *entry = &cache->entries[slot];

	if (entry->newer != SG_FIELD_CACHE_NO_SLOT)
		cache->entries[entry->newer].older = entry->older;
	else
		cache->newest = entry->older;
	if (entry->older != SG_FIELD_CACHE_NO_SLOT)
		cache->entries[entry->older].newer = entry->newer;
	else
		cache->oldest = entry->newer;
	entry->newer = SG_FIELD_CACHE_NO_SLOT;
	entry->older = SG_FIELD_CACHE_NO_SLOT;
}

static void AttachNewest(sg_destination_field_cache_t *cache, uint32_t slot)
{
	sg_field_cache_entry_t *entry = &cache->entries[slot];

	entry->newer = SG_FIELD_CACHE_NO_SLOT;
	entry->older = cache->newest;
	if (cache->newest != SG_FIELD_CACHE_NO_SLOT)
		cache->entries[cache->newest].newer = slot;
	else
		cache->oldest = slot;
	cache->newest = slot;
}

static void Touch(sg_destination_field_cache_t *cache, uint32_t slot)
{
	if (cache->newest == slot)
		return;
	Detach(cache, slot);
	AttachNewest(cache, slot);
}

static void ReleaseEntry(sg_destination_field_cache_t *cache, uint32_t slot)
{
	sg_field_cache_entry_t *entry = &cache->entries[slot];

	if (!entry->active)
		return;
	Detach(cache, slot);
	free(entry->samples);
	memset(entry, 0, sizeof(*entry));
	entry->newer = SG_FIELD_CACHE_NO_SLOT;
	entry->older = SG_FIELD_CACHE_NO_SLOT;
	cache->count--;
}

static int PeekSerial(const sg_destination_field_cache_t *cache,
	uint64_t *serial_out)
{
	if (cache->next_serial == 0U || cache->next_serial == UINT64_MAX)
		return 0;
	*serial_out = cache->next_serial;
	return 1;
}

static void CommitSerial(sg_destination_field_cache_t *cache)
{
	cache->next_serial++;
}

static uint32_t FindKey(const sg_destination_field_cache_t *cache,
	const sg_destination_handle_t *destination, uint64_t now_ms)
{
	uint32_t slot;

	for (slot = 0U; slot < cache->capacity; slot++)
		if (EntryKeyEqual(&cache->entries[slot], destination, now_ms))
			return slot;
	return SG_FIELD_CACHE_NO_SLOT;
}

static uint32_t FindDestination(const sg_destination_field_cache_t *cache,
	const sg_destination_handle_t *destination)
{
	uint32_t slot = cache->newest;

	while (slot != SG_FIELD_CACHE_NO_SLOT) {
		if (cache->entries[slot].active && DestinationEqual(
			&cache->entries[slot].field.destination, destination))
			return slot;
		slot = cache->entries[slot].older;
	}
	return SG_FIELD_CACHE_NO_SLOT;
}

static uint32_t SelectSlot(const sg_destination_field_cache_t *cache)
{
	uint32_t slot;

	for (slot = 0U; slot < cache->capacity; slot++)
		if (!cache->entries[slot].active)
			return slot;
	return cache->oldest;
}

static int DestinationUsable(const sg_destination_field_cache_t *cache,
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_destination_handle_t *destination, uint64_t now_ms)
{
	return SnapshotBound(cache, snapshot) && now_ms != 0U &&
		SG_DestinationHandleValid(destination) &&
		SG_PhaseCoordinateValid(snapshot, &destination->pose.phase) &&
		destination->pose.region_id < cache->leaf_region_count &&
		destination->pose.region_id == cache->phase_to_leaf_region[
			destination->pose.phase.phase_id] &&
		(destination->motion != SG_DESTINATION_MOVING ||
		 now_ms >= destination->pose.sample_time_ms);
}

static int SolveNewEntry(sg_destination_field_cache_t *cache,
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_destination_handle_t *destination, uint64_t now_ms,
	uint32_t preferred_slot, uint32_t *slot_out)
{
	sg_destination_field_t field;
	sg_field_sample_t *samples;
	size_t sample_bytes;
	uint64_t serial;
	uint32_t slot = preferred_slot;

	if (!SizeMultiply((size_t)cache->phase_count, sizeof(*samples),
		&sample_bytes) || !PeekSerial(cache, &serial))
		return 0;
	samples = malloc(sample_bytes);
	if (!samples)
		return 0;
	if (!SG_DestinationFieldSolve(snapshot, destination, now_ms, samples,
		cache->phase_count, &field)) {
		free(samples);
		return 0;
	}
	if (slot == SG_FIELD_CACHE_NO_SLOT)
		slot = SelectSlot(cache);
	if (cache->entries[slot].active) {
		if (preferred_slot == SG_FIELD_CACHE_NO_SLOT)
			SaturatingIncrement(&cache->stats.evictions);
		ReleaseEntry(cache, slot);
	}
	cache->entries[slot].active = 1U;
	CommitSerial(cache);
	cache->entries[slot].serial = serial;
	cache->entries[slot].field = field;
	cache->entries[slot].samples = samples;
	cache->entries[slot].field.samples = samples;
	cache->entries[slot].newer = SG_FIELD_CACHE_NO_SLOT;
	cache->entries[slot].older = SG_FIELD_CACHE_NO_SLOT;
	AttachNewest(cache, slot);
	cache->count++;
	SaturatingIncrement(&cache->stats.clean_solves);
	*slot_out = slot;
	return 1;
}

static void SetResult(sg_field_cache_result_t *out, uint32_t slot,
	uint64_t serial, sg_field_cache_disposition_t disposition,
	sg_field_cache_scope_t scope)
{
	memset(out, 0, sizeof(*out));
	out->ref.slot = slot;
	out->ref.serial = serial;
	out->disposition = disposition;
	out->scope = scope;
}

static int HierarchyValid(const sg_rune_runtime_snapshot_t *snapshot,
	const sg_field_region_hierarchy_t *hierarchy)
{
	uint32_t leaf;
	uint32_t level;

	if (!hierarchy || hierarchy->leaf_region_count == 0U ||
		hierarchy->leaf_region_count != snapshot->region_count ||
		hierarchy->level_count == 0U ||
		hierarchy->level_count > SG_DESTINATION_FIELD_MAX_REGION_LEVEL + 1U ||
		!hierarchy->levels || !hierarchy->phase_to_leaf_region)
		return 0;
	for (leaf = 0U; leaf < snapshot->phase_count; leaf++)
		if (hierarchy->phase_to_leaf_region[leaf] >=
			hierarchy->leaf_region_count)
			return 0;
	for (level = 0U; level < hierarchy->level_count; level++) {
		if (hierarchy->levels[level].region_count == 0U ||
			!hierarchy->levels[level].leaf_to_region)
			return 0;
		for (leaf = 0U; leaf < hierarchy->leaf_region_count; leaf++)
			if (hierarchy->levels[level].leaf_to_region[leaf] >=
				hierarchy->levels[level].region_count ||
				(level == 0U &&
				 hierarchy->levels[level].leaf_to_region[leaf] != leaf))
				return 0;
	}
	return hierarchy->levels[0].region_count == hierarchy->leaf_region_count;
}

int SG_DestinationFieldCacheCreate(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_field_region_hierarchy_t *hierarchy, uint32_t entry_capacity,
	sg_destination_field_cache_t **out)
{
	sg_destination_field_cache_t *cache;
	size_t entry_bytes;
	size_t level_bytes;
	size_t mapping_bytes;
	size_t phase_region_bytes;
	size_t affected_phase_bytes;
	size_t affected_leaf_bytes;
	size_t affected_region_bytes;
	size_t mapping_count;
	size_t affected_region_count = 0U;
	uint32_t level;

	if (out)
		*out = NULL;
	if (!out || entry_capacity == 0U ||
		!SG_RuneRuntimeSnapshotValid(snapshot) ||
		!HierarchyValid(snapshot, hierarchy) ||
		!SizeMultiply((size_t)entry_capacity, sizeof(*cache->entries),
			&entry_bytes) ||
		!SizeMultiply((size_t)hierarchy->level_count,
			sizeof(*cache->levels), &level_bytes) ||
		!SizeMultiply((size_t)hierarchy->leaf_region_count,
			(size_t)hierarchy->level_count, &mapping_count) ||
		!SizeMultiply(mapping_count, sizeof(*cache->hierarchy_storage),
			&mapping_bytes) ||
		!SizeMultiply((size_t)snapshot->phase_count,
			sizeof(*cache->phase_to_leaf_region), &phase_region_bytes) ||
		!SizeMultiply((size_t)snapshot->phase_count,
			sizeof(*cache->affected_phases), &affected_phase_bytes) ||
		!SizeMultiply((size_t)hierarchy->leaf_region_count,
			sizeof(*cache->affected_leaf_regions), &affected_leaf_bytes))
		return 0;
	for (level = 0U; level < hierarchy->level_count; level++) {
		if ((size_t)hierarchy->levels[level].region_count >
			SIZE_MAX - affected_region_count)
			return 0;
		affected_region_count += hierarchy->levels[level].region_count;
	}
	if (affected_region_count > UINT32_MAX ||
		!SizeMultiply(affected_region_count, sizeof(*cache->affected_regions),
			&affected_region_bytes))
		return 0;
	cache = calloc(1U, sizeof(*cache));
	if (!cache)
		return 0;
	cache->entries = calloc(1U, entry_bytes);
	cache->levels = calloc(1U, level_bytes);
	cache->hierarchy_storage = malloc(mapping_bytes);
	cache->phase_to_leaf_region = malloc(phase_region_bytes);
	cache->affected_phases = malloc(affected_phase_bytes);
	cache->affected_leaf_regions = malloc(affected_leaf_bytes);
	cache->affected_regions = malloc(affected_region_bytes);
	if (!cache->entries || !cache->levels || !cache->hierarchy_storage ||
		!cache->phase_to_leaf_region || !cache->affected_phases ||
		!cache->affected_leaf_regions || !cache->affected_regions) {
		SG_DestinationFieldCacheDestroy(cache);
		return 0;
	}
	cache->rune_identity = snapshot->identity;
	cache->topology_revision = snapshot->topology_revision;
	cache->model = snapshot->model;
	cache->phases = snapshot->phases;
	cache->model_identity = snapshot->model->identity;
	cache->cell_count = snapshot->cell_count;
	cache->phase_count = snapshot->phase_count;
	cache->leaf_region_count = hierarchy->leaf_region_count;
	cache->level_count = hierarchy->level_count;
	cache->capacity = entry_capacity;
	cache->affected_region_capacity = (uint32_t)affected_region_count;
	cache->newest = SG_FIELD_CACHE_NO_SLOT;
	cache->oldest = SG_FIELD_CACHE_NO_SLOT;
	cache->next_serial = 1U;
	for (level = 0U; level < entry_capacity; level++) {
		cache->entries[level].newer = SG_FIELD_CACHE_NO_SLOT;
		cache->entries[level].older = SG_FIELD_CACHE_NO_SLOT;
	}
	for (level = 0U; level < hierarchy->level_count; level++) {
		uint32_t *mapping = cache->hierarchy_storage +
			(size_t)level * hierarchy->leaf_region_count;

		memcpy(mapping, hierarchy->levels[level].leaf_to_region,
			(size_t)hierarchy->leaf_region_count * sizeof(*mapping));
		cache->levels[level].region_count =
			hierarchy->levels[level].region_count;
		cache->levels[level].leaf_to_region = mapping;
	}
	memcpy(cache->phase_to_leaf_region, hierarchy->phase_to_leaf_region,
		phase_region_bytes);
	*out = cache;
	return 1;
}

void SG_DestinationFieldCacheInvalidate(sg_destination_field_cache_t *cache)
{
	uint32_t slot;

	if (!cache)
		return;
	for (slot = 0U; slot < cache->capacity; slot++)
		if (cache->entries[slot].active)
			ReleaseEntry(cache, slot);
	cache->newest = SG_FIELD_CACHE_NO_SLOT;
	cache->oldest = SG_FIELD_CACHE_NO_SLOT;
}

void SG_DestinationFieldCacheDestroy(sg_destination_field_cache_t *cache)
{
	if (!cache)
		return;
	SG_DestinationFieldCacheInvalidate(cache);
	free(cache->affected_regions);
	free(cache->affected_leaf_regions);
	free(cache->affected_phases);
	free(cache->phase_to_leaf_region);
	free(cache->hierarchy_storage);
	free(cache->levels);
	free(cache->entries);
	free(cache);
}

int SG_DestinationFieldCacheResolve(sg_destination_field_cache_t *cache,
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_destination_handle_t *destination, uint64_t now_ms,
	sg_field_cache_result_t *out)
{
	uint32_t slot;

	if (out)
		memset(out, 0, sizeof(*out));
	if (!out || !DestinationUsable(cache, snapshot, destination, now_ms))
		return 0;
	slot = FindKey(cache, destination, now_ms);
	if (slot != SG_FIELD_CACHE_NO_SLOT) {
		Touch(cache, slot);
		if (destination->motion == SG_DESTINATION_STATIC)
			SaturatingIncrement(&cache->stats.static_hits);
		SetResult(out, slot, cache->entries[slot].serial,
			SG_FIELD_CACHE_HIT, SG_FIELD_CACHE_SCOPE_NONE);
		return 1;
	}
	if (!SolveNewEntry(cache, snapshot, destination, now_ms,
		SG_FIELD_CACHE_NO_SLOT, &slot))
		return 0;
	SetResult(out, slot, cache->entries[slot].serial,
		SG_FIELD_CACHE_MISS_SOLVED, SG_FIELD_CACHE_SCOPE_ALL);
	return 1;
}

static int TerminalSemanticsEqual(const sg_destination_handle_t *left,
	const sg_destination_handle_t *right)
{
	return left->motion == right->motion && PoseEqual(&left->pose, &right->pose);
}

static void AddAffected(sg_destination_field_cache_t *cache,
	sg_field_cache_result_t *out, uint32_t level, uint32_t region)
{
	uint32_t index;

	for (index = 0U; index < out->affected_region_count; index++)
		if (cache->affected_regions[index].level == level &&
			cache->affected_regions[index].region == region)
			return;
	if (out->affected_region_count < cache->affected_region_capacity) {
		cache->affected_regions[out->affected_region_count].level = level;
		cache->affected_regions[out->affected_region_count].region = region;
		out->affected_region_count++;
		out->affected_regions = cache->affected_regions;
	}
}

static void ReportLocalRegions(sg_destination_field_cache_t *cache,
	const sg_destination_handle_t *before,
	const sg_destination_handle_t *after, sg_field_cache_result_t *out)
{
	uint32_t level;

	if (TerminalSemanticsEqual(before, after)) {
		out->scope = SG_FIELD_CACHE_SCOPE_NONE;
		return;
	}
	out->scope = SG_FIELD_CACHE_SCOPE_LOCAL;
	for (level = 0U; level < cache->level_count; level++) {
		AddAffected(cache, out, level, cache->levels[level].leaf_to_region[
			before->pose.region_id]);
		AddAffected(cache, out, level, cache->levels[level].leaf_to_region[
			after->pose.region_id]);
	}
}

static int ReportDependencyClosure(sg_destination_field_cache_t *cache,
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_destination_handle_t *before,
	const sg_destination_handle_t *after, sg_field_cache_result_t *out)
{
	uint32_t affected_leaf_count = 0U;
	uint32_t phase;
	uint32_t level;

	if (!SG_DestinationFieldDependencyClosure(snapshot, &before->pose.phase,
		&after->pose.phase, cache->affected_phases, cache->phase_count))
		return 0;
	memset(cache->affected_leaf_regions, 0,
		(size_t)cache->leaf_region_count *
		sizeof(*cache->affected_leaf_regions));
	for (phase = 0U; phase < cache->phase_count; phase++)
		if (cache->affected_phases[phase] != 0U) {
			uint32_t leaf = cache->phase_to_leaf_region[phase];

			if (cache->affected_leaf_regions[leaf] == 0U) {
				cache->affected_leaf_regions[leaf] = 1U;
				affected_leaf_count++;
			}
		}
	if (affected_leaf_count == cache->leaf_region_count) {
		out->scope = SG_FIELD_CACHE_SCOPE_ALL;
		return 1;
	}
	out->scope = SG_FIELD_CACHE_SCOPE_LOCAL;
	for (level = 0U; level < cache->level_count; level++) {
		uint32_t leaf;

		for (leaf = 0U; leaf < cache->leaf_region_count; leaf++)
			if (cache->affected_leaf_regions[leaf] != 0U)
				AddAffected(cache, out, level,
					cache->levels[level].leaf_to_region[leaf]);
	}
	return 1;
}

static int SolveRegionalEntry(sg_destination_field_cache_t *cache,
	const sg_rune_runtime_snapshot_t *snapshot, uint32_t slot,
	const sg_destination_handle_t *destination, uint64_t now_ms,
	sg_field_cache_result_t *out)
{
	sg_field_cache_entry_t *entry = &cache->entries[slot];
	sg_destination_field_t field;
	sg_field_sample_t *samples;
	size_t sample_bytes;
	uint64_t serial;

	if (!SizeMultiply((size_t)cache->phase_count, sizeof(*samples),
		&sample_bytes) || !PeekSerial(cache, &serial))
		return 0;
	samples = malloc(sample_bytes);
	if (!samples)
		return 0;
	if (!SG_DestinationFieldSolveAffected(snapshot, &entry->field,
		destination, now_ms,
		cache->affected_phases, cache->phase_count, samples,
		cache->phase_count, &field)) {
		free(samples);
		return 0;
	}
	free(entry->samples);
	CommitSerial(cache);
	entry->samples = samples;
	entry->field = field;
	entry->field.samples = samples;
	entry->serial = serial;
	Touch(cache, slot);
	if (out->scope == SG_FIELD_CACHE_SCOPE_ALL)
		SaturatingIncrement(&cache->stats.clean_solves);
	else
		SaturatingIncrement(&cache->stats.regional_updates);
	out->ref.slot = slot;
	out->ref.serial = serial;
	out->disposition = out->scope == SG_FIELD_CACHE_SCOPE_ALL ?
		SG_FIELD_CACHE_CLEAN_REBUILD : SG_FIELD_CACHE_INCREMENTAL_REUSE;
	return 1;
}

int SG_DestinationFieldCacheUpdate(sg_destination_field_cache_t *cache,
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_destination_handle_t *before,
	const sg_destination_handle_t *after, uint64_t now_ms,
	sg_field_cache_result_t *out)
{
	uint64_t serial;
	uint32_t before_slot;
	uint32_t after_slot;
	uint32_t solved_slot;

	if (out)
		memset(out, 0, sizeof(*out));
	if (!out || !DestinationUsable(cache, snapshot, after, now_ms) ||
		!SG_DestinationHandleValid(before) ||
		!SG_PhaseCoordinateValid(snapshot, &before->pose.phase) ||
		before->pose.region_id >= cache->leaf_region_count ||
		before->pose.region_id != cache->phase_to_leaf_region[
			before->pose.phase.phase_id] ||
		!SG_DestinationSameTarget(before, after) ||
		after->generation <= before->generation ||
		(before->motion == SG_DESTINATION_MOVING &&
		 after->motion == SG_DESTINATION_MOVING &&
		 after->pose.sample_time_ms <= before->pose.sample_time_ms))
		return 0;
	after_slot = FindKey(cache, after, now_ms);
	before_slot = FindDestination(cache, before);
	if (after_slot != SG_FIELD_CACHE_NO_SLOT) {
		SetResult(out, after_slot, cache->entries[after_slot].serial,
			SG_FIELD_CACHE_HIT, SG_FIELD_CACHE_SCOPE_NONE);
		if (CoordinateEqual(&before->pose.phase, &after->pose.phase))
			ReportLocalRegions(cache, before, after, out);
		else if (!ReportDependencyClosure(cache, snapshot, before, after, out)) {
			memset(out, 0, sizeof(*out));
			return 0;
		}
		if (before_slot != SG_FIELD_CACHE_NO_SLOT && before_slot != after_slot)
			ReleaseEntry(cache, before_slot);
		Touch(cache, after_slot);
		return 1;
	}
	if (before_slot != SG_FIELD_CACHE_NO_SLOT &&
		CoordinateEqual(&before->pose.phase, &after->pose.phase)) {
		sg_field_cache_entry_t *entry = &cache->entries[before_slot];

		if (!PeekSerial(cache, &serial))
			return 0;
		CommitSerial(cache);
		entry->serial = serial;
		entry->field.generation = after->generation;
		entry->field.computed_at_ms = now_ms;
		entry->field.destination = *after;
		Touch(cache, before_slot);
		SaturatingIncrement(&cache->stats.incremental_reuses);
		SetResult(out, before_slot, serial,
			SG_FIELD_CACHE_INCREMENTAL_REUSE, SG_FIELD_CACHE_SCOPE_LOCAL);
		ReportLocalRegions(cache, before, after, out);
		return 1;
	}
	if (before_slot != SG_FIELD_CACHE_NO_SLOT) {
		if (!ReportDependencyClosure(cache, snapshot, before, after, out) ||
			!SolveRegionalEntry(cache, snapshot, before_slot, after, now_ms,
				out)) {
			memset(out, 0, sizeof(*out));
			return 0;
		}
		return 1;
	}
	if (!SolveNewEntry(cache, snapshot, after, now_ms, before_slot,
		&solved_slot))
		return 0;
	SetResult(out, solved_slot, cache->entries[solved_slot].serial,
		SG_FIELD_CACHE_CLEAN_REBUILD, SG_FIELD_CACHE_SCOPE_ALL);
	return 1;
}

int SG_DestinationFieldCacheField(const sg_destination_field_cache_t *cache,
	const sg_rune_runtime_snapshot_t *snapshot, sg_field_cache_ref_t ref,
	const sg_destination_field_t **out)
{
	if (out)
		*out = NULL;
	if (!out || !SnapshotBound(cache, snapshot) ||
		ref.slot >= cache->capacity || ref.serial == 0U ||
		!cache->entries[ref.slot].active ||
		cache->entries[ref.slot].serial != ref.serial)
		return 0;
	*out = &cache->entries[ref.slot].field;
	return 1;
}

int SG_DestinationFieldCacheQuery(const sg_destination_field_cache_t *cache,
	const sg_rune_runtime_snapshot_t *snapshot, sg_field_cache_ref_t ref,
	const sg_destination_pose_t *source, sg_field_query_result_t *out)
{
	const sg_destination_field_t *field;

	if (out)
		memset(out, 0, sizeof(*out));
	if (!out || !SG_DestinationFieldCacheField(cache, snapshot, ref, &field))
		return 0;
	return SG_FieldQuery(snapshot, field, source, out);
}

void SG_DestinationFieldCacheStats(const sg_destination_field_cache_t *cache,
	sg_destination_field_cache_stats_t *out)
{
	if (!out)
		return;
	if (!cache) {
		memset(out, 0, sizeof(*out));
		return;
	}
	*out = cache->stats;
}
