#include "sg_rune_dynamics_model_internal.h"

#include "sg_field_attractor.h"
#define SG_FIELD_SERVICE_OWNER_PRIVATE 1
#include "sg_field_service_owner_private.h"
#undef SG_FIELD_SERVICE_OWNER_PRIVATE

#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

#define FIELD_SOURCE_MAGIC UINT64_C(0x46534f5552434531)
#define FIELD_PUBLICATION_MAGIC UINT64_C(0x465055424c494331)
#define FIELD_SERVICE_MAGIC UINT64_C(0x4653455256494331)

typedef struct sg_field_model_storage_s
{
	sg_rune_dynamics_model_t model;
} sg_field_model_storage_t;

typedef struct sg_field_terminal_copy_s
{
	sg_destination_terminal_t value;
	sg_destination_tube_segment_t *segments;
} sg_field_terminal_copy_t;

typedef struct sg_field_environment_copy_s
{
	sg_field_environment_t value;
	sg_field_guard_state_t *guards;
	sg_field_event_slab_t *slabs;
	sg_field_guard_state_t *slab_guards;
} sg_field_environment_copy_t;

typedef struct sg_field_solution_s
{
	size_t atom_count;
	size_t node_count;
	size_t region_count;
	size_t guard_vector_count;
	size_t guard_count;
	struct sg_field_time_region_s *regions;
	sg_field_guard_truth_t *guard_truths;
	uint8_t *terminal_atoms;
	uint8_t *reachable;
	uint32_t *rank;
	sg_rune_cost_bounds_t *costs;
	uint8_t *accepted_kernels;
	struct sg_field_bellman_rule_s *rules;
	size_t rule_count;
	uint32_t *rule_destinations;
	uint32_t *rule_outcomes;
	size_t rule_destination_count;
	uint64_t local_span_us;
} sg_field_solution_t;

typedef struct sg_field_time_region_s
{
	uint64_t valid_from_ms;
	uint64_t valid_until_ms;
} sg_field_time_region_t;

typedef struct sg_field_bellman_rule_s
{
	uint32_t source_node;
	uint32_t choice_index;
	uint32_t kernel_index;
	sg_field_attractor_span_t destinations;
} sg_field_bellman_rule_t;

#define FIELD_NO_KERNEL UINT32_MAX

typedef struct sg_field_cache_entry_s sg_field_cache_entry_t;
typedef struct sg_field_lease_s sg_field_lease_t;

struct sg_field_cache_entry_s
{
	sg_field_terminal_copy_t terminal;
	sg_field_environment_copy_t environment;
	uint64_t solved_at_ms;
	uint8_t incrementally_reused;
	size_t reused_node_count;
	sg_field_solution_t solution;
	sg_field_cache_entry_t *next;
};

struct sg_field_lease_s
{
	sg_field_handle_t handle;
	sg_field_cache_entry_t *entry;
	sg_field_lease_t *next;
};

struct sg_field_model_source_s
{
	uint64_t magic;
	uint64_t owner_identity;
	uint64_t source_generation;
	const sg_rune_runtime_snapshot_t *snapshot;
	const sg_rune_dynamics_model_t *model;
	uint64_t rune_identity;
	uint64_t topology_revision;
	sg_rune_dynamics_model_id_t model_id;
};

struct sg_field_model_publication_s
{
	uint64_t magic;
	uint64_t owner_identity;
	uint64_t publication_generation;
	sg_field_model_storage_t storage;
};

struct sg_field_service_s
{
	uint64_t magic;
	uint64_t identity;
	uint64_t generation;
	uint64_t next_field_generation;
	sg_field_model_storage_t storage;
	sg_field_cache_entry_t *cache;
	sg_field_lease_t *leases;
	uint64_t clean_solves;
	uint64_t incremental_reuses;
	uint64_t incremental_reused_nodes;
};

static _Atomic uint64_t next_private_identity = UINT64_C(1);

static int StableSame(const sg_rune_stable_id_t *left,
	const sg_rune_stable_id_t *right)
{
	return left->source_set_identity == right->source_set_identity &&
		left->high == right->high && left->low == right->low;
}

static int StableOrder(const sg_rune_stable_id_t *left,
	const sg_rune_stable_id_t *right)
{
	if (left->source_set_identity != right->source_set_identity)
		return left->source_set_identity < right->source_set_identity ? -1 : 1;
	if (left->high != right->high)
		return left->high < right->high ? -1 : 1;
	if (left->low != right->low)
		return left->low < right->low ? -1 : 1;
	return 0;
}

static int NextPrivateIdentity(uint64_t *identity_out)
{
	uint64_t current;

	if (!identity_out)
		return 0;
	current = atomic_load_explicit(&next_private_identity,
		memory_order_relaxed);
	for (;;)
	{
		if (current == 0U || current == UINT64_MAX)
			return 0;
		if (atomic_compare_exchange_weak_explicit(&next_private_identity,
			&current, current + 1U, memory_order_relaxed,
			memory_order_relaxed))
		{
			*identity_out = current;
			return 1;
		}
	}
}

#ifdef SG_FIELD_SERVICE_TESTING
void SG_FieldServiceTestExhaustIdentities(void)
{
	atomic_store_explicit(&next_private_identity, UINT64_MAX,
		memory_order_relaxed);
}

size_t SG_FieldServiceTestCacheCount(const sg_field_service_t *service)
{
	const sg_field_cache_entry_t *entry;
	size_t count = 0U;

	if (!service || service->magic != FIELD_SERVICE_MAGIC)
		return 0U;
	for (entry = service->cache; entry; entry = entry->next)
		count++;
	return count;
}

size_t SG_FieldServiceTestLeaseCount(const sg_field_service_t *service)
{
	const sg_field_lease_t *lease;
	size_t count = 0U;

	if (!service || service->magic != FIELD_SERVICE_MAGIC)
		return 0U;
	for (lease = service->leases; lease; lease = lease->next)
		count++;
	return count;
}

uint64_t SG_FieldServiceTestCleanSolveCount(const sg_field_service_t *service)
{
	return service && service->magic == FIELD_SERVICE_MAGIC ?
		service->clean_solves : 0U;
}

uint64_t SG_FieldServiceTestIncrementalReuseCount(
	const sg_field_service_t *service)
{
	return service && service->magic == FIELD_SERVICE_MAGIC ?
		service->incremental_reuses : 0U;
}

uint64_t SG_FieldServiceTestIncrementalReusedNodeCount(
	const sg_field_service_t *service)
{
	return service && service->magic == FIELD_SERVICE_MAGIC ?
		service->incremental_reused_nodes : 0U;
}
#endif

static int SizeProduct(size_t count, size_t element_size, size_t *bytes_out)
{
	if (!bytes_out || (element_size != 0U && count > SIZE_MAX / element_size))
		return 0;
	*bytes_out = count * element_size;
	return 1;
}

static void *CopyArray(const void *source, size_t count, size_t element_size)
{
	size_t bytes;
	void *copy;

	if (count == 0U)
		return NULL;
	if (!source || !SizeProduct(count, element_size, &bytes))
		return NULL;
	copy = malloc(bytes);
	if (copy)
		memcpy(copy, source, bytes);
	return copy;
}

static void ModelStorageDestroy(sg_field_model_storage_t *storage)
{
	sg_rune_dynamics_model_t *model;

	if (!storage)
		return;
	model = &storage->model;
	free((void *)model->state_vertices);
	free((void *)model->state_charts);
	free((void *)model->state_simplices);
	free((void *)model->state_domains);
	free((void *)model->control_fibers);
	free((void *)model->control_domains);
	free((void *)model->response_patches);
	free((void *)model->boundary_transfers);
	free((void *)model->reach_atoms);
	free((void *)model->simplex_owners);
	free((void *)model->domain_support);
	free((void *)model->domain_boundary_facets);
	free((void *)model->domain_boundary_vertices);
	free((void *)model->exact_words);
	free((void *)model->outcome_images);
	free((void *)model->outcome_cover_pieces);
	free((void *)model->guard_requirements);
	free((void *)model->guard_effects);
	free((void *)model->outcomes);
	free((void *)model->choices);
	free((void *)model->local_progress_kernels);
	free((void *)model->local_progress_sources);
	free((void *)model->local_progress_choices);
	free((void *)model->local_progress_targets);
	free((void *)model->refinement_tree.nodes);
	free((void *)model->refinement_tree.vertices);
	free((void *)model->refinement_tree.node_vertices);
	free((void *)model->refinement_tree.faces);
	free((void *)model->refinement_tree.face_vertices);
	free((void *)model->refinement_tree.face_incidences);
	free((void *)model->refinement_tree.node_faces);
	free((void *)model->refinement_tree.children);
	free((void *)model->refinement_tree.atom_roots);
	free((void *)model->hierarchy.regions);
	free((void *)model->hierarchy.children);
	free((void *)model->hierarchy.chart_leaf_regions);
	free((void *)model->hierarchy.state_domain_leaf_regions);
	free((void *)model->hierarchy.response_patch_leaf_regions);
	memset(storage, 0, sizeof(*storage));
}

#define COPY_MODEL_ARRAY(member, count_member, type) do { \
	destination->member = CopyArray(source->member, source->count_member, \
		sizeof(type)); \
	if (source->count_member != 0U && !destination->member) \
		goto fail; \
} while (0)

static int ModelStorageCopy(const sg_rune_dynamics_model_t *source,
	sg_field_model_storage_t *storage)
{
	sg_rune_dynamics_model_t *destination;

	if (!source || !storage)
		return 0;
	memset(storage, 0, sizeof(*storage));
	storage->model = *source;
	destination = &storage->model;
	COPY_MODEL_ARRAY(state_vertices, state_vertex_count,
		sg_rune_state_vertex_t);
	COPY_MODEL_ARRAY(state_charts, state_chart_count, sg_rune_state_chart_t);
	COPY_MODEL_ARRAY(state_simplices, state_simplex_count,
		sg_rune_state_simplex_t);
	COPY_MODEL_ARRAY(state_domains, state_domain_count,
		sg_rune_state_domain_t);
	COPY_MODEL_ARRAY(control_fibers, control_fiber_count,
		sg_rune_control_fiber_t);
	COPY_MODEL_ARRAY(control_domains, control_domain_count,
		sg_rune_control_domain_t);
	COPY_MODEL_ARRAY(response_patches, response_patch_count,
		sg_rune_response_patch_t);
	COPY_MODEL_ARRAY(boundary_transfers, boundary_transfer_count,
		sg_rune_boundary_transfer_t);
	COPY_MODEL_ARRAY(reach_atoms, reach_atom_count, sg_field_reach_atom_t);
	COPY_MODEL_ARRAY(simplex_owners, simplex_owner_count,
		sg_rune_state_simplex_owner_t);
	COPY_MODEL_ARRAY(domain_support, domain_support_count,
		sg_rune_domain_support_certificate_t);
	COPY_MODEL_ARRAY(domain_boundary_facets, domain_boundary_facet_count,
		sg_rune_domain_boundary_facet_t);
	COPY_MODEL_ARRAY(domain_boundary_vertices, domain_boundary_vertex_count,
		sg_field_refinement_vertex_ref_t);
	COPY_MODEL_ARRAY(exact_words, exact_word_count, uint32_t);
	COPY_MODEL_ARRAY(outcome_images, outcome_image_count,
		sg_field_outcome_image_t);
	COPY_MODEL_ARRAY(outcome_cover_pieces, outcome_cover_piece_count,
		sg_field_outcome_cover_piece_t);
	COPY_MODEL_ARRAY(guard_requirements, guard_requirement_count,
		sg_field_guard_requirement_t);
	COPY_MODEL_ARRAY(guard_effects, guard_effect_count,
		sg_field_guard_effect_t);
	COPY_MODEL_ARRAY(outcomes, outcome_count, sg_field_outcome_t);
	COPY_MODEL_ARRAY(choices, choice_count, sg_field_choice_t);
	COPY_MODEL_ARRAY(local_progress_kernels, local_progress_kernel_count,
		sg_field_local_progress_kernel_t);
	COPY_MODEL_ARRAY(local_progress_sources, local_progress_source_count,
		sg_field_refinement_node_ref_t);
	COPY_MODEL_ARRAY(local_progress_choices, local_progress_choice_count,
		sg_field_choice_ref_t);
	COPY_MODEL_ARRAY(local_progress_targets, local_progress_target_count,
		sg_field_progress_target_t);
	destination->refinement_tree.nodes = CopyArray(
		source->refinement_tree.nodes, source->refinement_tree.node_count,
		sizeof(sg_field_refinement_node_t));
	destination->refinement_tree.vertices = CopyArray(
		source->refinement_tree.vertices, source->refinement_tree.vertex_count,
		sizeof(sg_field_refinement_vertex_t));
	destination->refinement_tree.node_vertices = CopyArray(
		source->refinement_tree.node_vertices,
		source->refinement_tree.node_vertex_count,
		sizeof(sg_field_refinement_vertex_ref_t));
	destination->refinement_tree.faces = CopyArray(
		source->refinement_tree.faces, source->refinement_tree.face_count,
		sizeof(sg_field_refinement_face_t));
	destination->refinement_tree.face_vertices = CopyArray(
		source->refinement_tree.face_vertices,
		source->refinement_tree.face_vertex_count,
		sizeof(sg_field_refinement_vertex_ref_t));
	destination->refinement_tree.face_incidences = CopyArray(
		source->refinement_tree.face_incidences,
		source->refinement_tree.face_incidence_count,
		sizeof(sg_field_refinement_face_incidence_t));
	destination->refinement_tree.node_faces = CopyArray(
		source->refinement_tree.node_faces,
		source->refinement_tree.node_face_count,
		sizeof(sg_field_refinement_face_ref_t));
	destination->refinement_tree.children = CopyArray(
		source->refinement_tree.children, source->refinement_tree.child_count,
		sizeof(uint32_t));
	destination->refinement_tree.atom_roots = CopyArray(
		source->refinement_tree.atom_roots, source->refinement_tree.atom_count,
		sizeof(uint32_t));
	destination->hierarchy.regions = CopyArray(source->hierarchy.regions,
		source->hierarchy.region_count, sizeof(sg_rune_field_region_t));
	destination->hierarchy.children = CopyArray(source->hierarchy.children,
		source->hierarchy.child_count, sizeof(uint32_t));
	destination->hierarchy.chart_leaf_regions = CopyArray(
		source->hierarchy.chart_leaf_regions, source->hierarchy.chart_count,
		sizeof(uint32_t));
	destination->hierarchy.state_domain_leaf_regions = CopyArray(
		source->hierarchy.state_domain_leaf_regions,
		source->hierarchy.state_domain_count, sizeof(uint32_t));
	destination->hierarchy.response_patch_leaf_regions = CopyArray(
		source->hierarchy.response_patch_leaf_regions,
		source->hierarchy.response_patch_count, sizeof(uint32_t));
	if ((source->refinement_tree.node_count != 0U &&
	     !destination->refinement_tree.nodes) ||
	    (source->refinement_tree.vertex_count != 0U &&
	     !destination->refinement_tree.vertices) ||
	    (source->refinement_tree.node_vertex_count != 0U &&
	     !destination->refinement_tree.node_vertices) ||
	    (source->refinement_tree.face_count != 0U &&
	     !destination->refinement_tree.faces) ||
	    (source->refinement_tree.face_vertex_count != 0U &&
	     !destination->refinement_tree.face_vertices) ||
	    (source->refinement_tree.face_incidence_count != 0U &&
	     !destination->refinement_tree.face_incidences) ||
	    (source->refinement_tree.node_face_count != 0U &&
	     !destination->refinement_tree.node_faces) ||
	    (source->refinement_tree.child_count != 0U &&
	     !destination->refinement_tree.children) ||
	    (source->refinement_tree.atom_count != 0U &&
	     !destination->refinement_tree.atom_roots) ||
	    (source->hierarchy.region_count != 0U &&
	     !destination->hierarchy.regions) ||
	    (source->hierarchy.child_count != 0U &&
	     !destination->hierarchy.children) ||
	    (source->hierarchy.chart_count != 0U &&
	     !destination->hierarchy.chart_leaf_regions) ||
	    (source->hierarchy.state_domain_count != 0U &&
	     !destination->hierarchy.state_domain_leaf_regions) ||
	    (source->hierarchy.response_patch_count != 0U &&
	     !destination->hierarchy.response_patch_leaf_regions))
		goto fail;
	return 1;

fail:
	ModelStorageDestroy(storage);
	return 0;
}

#undef COPY_MODEL_ARRAY

static int ModelCoverageComplete(const sg_rune_dynamics_model_t *model)
{
	size_t index;

	if (!model || !model->state_vertices || model->state_vertex_count == 0U ||
	    !model->state_charts || model->state_chart_count == 0U ||
	    !model->state_simplices || model->state_simplex_count == 0U ||
	    !model->state_domains || model->state_domain_count == 0U ||
	    !model->reach_atoms || model->reach_atom_count == 0U ||
	    !model->outcome_images || model->outcome_image_count == 0U ||
	    !model->outcome_cover_pieces || model->outcome_cover_piece_count == 0U ||
	    !model->outcomes || model->outcome_count == 0U ||
	    !model->choices || model->choice_count == 0U ||
	    !model->local_progress_kernels ||
	    model->local_progress_kernel_count == 0U ||
	    !model->local_progress_sources || model->local_progress_source_count == 0U ||
	    !model->local_progress_choices || model->local_progress_choice_count == 0U ||
	    !model->local_progress_targets || model->local_progress_target_count == 0U ||
	    !model->refinement_tree.nodes || model->refinement_tree.node_count == 0U ||
	    !model->refinement_tree.vertices ||
	    model->refinement_tree.vertex_count == 0U ||
	    !model->refinement_tree.atom_roots ||
	    model->refinement_tree.atom_count != model->reach_atom_count)
		return 0;
	for (index = 0U; index < model->outcome_count; index++)
		if (model->outcomes[index].source_images.count == 0U ||
		    model->outcomes[index].destination_cover.count == 0U ||
		    model->outcomes[index].absolute_time_advance.minimum_ms >
			model->outcomes[index].absolute_time_advance.maximum_ms)
			return 0;
	for (index = 0U; index < model->choice_count; index++)
		if (model->choices[index].outcomes.count == 0U)
			return 0;
	for (index = 0U; index < model->local_progress_kernel_count; index++)
		if (model->local_progress_kernels[index].covered_sources.count == 0U ||
		    model->local_progress_kernels[index].admissible_choices.count == 0U ||
		    model->local_progress_kernels[index].whole_outcome_targets.count == 0U)
			return 0;
	return 1;
}

sg_field_status_t SG_FieldModelSourceAdoptOwnerPrivate(
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_rune_dynamics_model_t *dynamics_model, uint64_t owner_identity,
	sg_field_model_source_t **source_out)
{
	sg_field_model_source_t *source;
	uint64_t generation;

	if (source_out)
		*source_out = NULL;
	if (!source_out || !snapshot || !dynamics_model || owner_identity == 0U)
		return SG_FIELD_STATUS_INVALID_ARGUMENT;
	if (!ModelCoverageComplete(dynamics_model))
		return SG_FIELD_STATUS_MODEL_INCOMPLETE;
	if (!SG_RuneDynamicsModelValid(dynamics_model, snapshot))
		return SG_FIELD_STATUS_INVALID_MODEL;
	source = calloc(1U, sizeof(*source));
	if (!source)
		return SG_FIELD_STATUS_STORAGE;
	if (!NextPrivateIdentity(&generation))
	{
		free(source);
		return SG_FIELD_STATUS_CAPACITY;
	}
	source->magic = FIELD_SOURCE_MAGIC;
	source->owner_identity = owner_identity;
	source->source_generation = generation;
	source->snapshot = snapshot;
	source->model = dynamics_model;
	source->rune_identity = snapshot->identity;
	source->topology_revision = snapshot->topology_revision;
	source->model_id = dynamics_model->id;
	*source_out = source;
	return SG_FIELD_STATUS_OK;
}

void SG_FieldModelSourceDestroyOwnerPrivate(
	sg_field_model_source_t **source_io)
{
	sg_field_model_source_t *source;

	if (!source_io || !*source_io)
		return;
	source = *source_io;
	*source_io = NULL;
	if (source->magic == FIELD_SOURCE_MAGIC)
	{
		source->magic = 0U;
		source->snapshot = NULL;
		source->model = NULL;
	}
	free(source);
}

sg_field_status_t SG_FieldModelPublicationIssue(
	const sg_field_model_source_t *source,
	const sg_rune_runtime_snapshot_t *snapshot,
	const sg_rune_dynamics_model_t *dynamics_model,
	sg_field_model_publication_t **publication_out)
{
	sg_field_model_publication_t *publication;
	uint64_t generation;

	if (publication_out)
		*publication_out = NULL;
	if (!publication_out || !source || !snapshot || !dynamics_model)
		return SG_FIELD_STATUS_INVALID_ARGUMENT;
	if (source->magic != FIELD_SOURCE_MAGIC || source->owner_identity == 0U ||
	    source->source_generation == 0U || source->snapshot != snapshot ||
	    source->model != dynamics_model ||
	    source->rune_identity != snapshot->identity ||
	    source->topology_revision != snapshot->topology_revision ||
	    !StableSame(&source->model_id.value, &dynamics_model->id.value))
		return SG_FIELD_STATUS_IDENTITY_MISMATCH;
	if (!ModelCoverageComplete(dynamics_model))
		return SG_FIELD_STATUS_MODEL_INCOMPLETE;
	if (!SG_RuneDynamicsModelValid(dynamics_model, snapshot))
		return SG_FIELD_STATUS_INVALID_MODEL;
	publication = calloc(1U, sizeof(*publication));
	if (!publication)
		return SG_FIELD_STATUS_STORAGE;
	if (!ModelStorageCopy(dynamics_model, &publication->storage))
	{
		free(publication);
		return SG_FIELD_STATUS_STORAGE;
	}
	if (!NextPrivateIdentity(&generation))
	{
		ModelStorageDestroy(&publication->storage);
		free(publication);
		return SG_FIELD_STATUS_CAPACITY;
	}
	publication->magic = FIELD_PUBLICATION_MAGIC;
	publication->owner_identity = source->owner_identity;
	publication->publication_generation = generation;
	*publication_out = publication;
	return SG_FIELD_STATUS_OK;
}

void SG_FieldModelPublicationDestroy(sg_field_model_publication_t *publication)
{
	if (!publication)
		return;
	if (publication->magic == FIELD_PUBLICATION_MAGIC)
	{
		publication->magic = 0U;
		ModelStorageDestroy(&publication->storage);
	}
	free(publication);
}

static void TerminalCopyDestroy(sg_field_terminal_copy_t *copy)
{
	if (!copy)
		return;
	free(copy->segments);
	memset(copy, 0, sizeof(*copy));
}

static int TerminalCopyCreate(const sg_destination_terminal_t *terminal,
	sg_field_terminal_copy_t *copy)
{
	if (!terminal || !copy)
		return 0;
	memset(copy, 0, sizeof(*copy));
	copy->value = *terminal;
	if (terminal->kind == SG_DESTINATION_TERMINAL_MOVING_TUBE)
	{
		copy->segments = CopyArray(terminal->value.moving_tube.segments,
			terminal->value.moving_tube.segment_count,
			sizeof(*copy->segments));
		if (!copy->segments)
			return 0;
		copy->value.value.moving_tube.segments = copy->segments;
	}
	return 1;
}

static void EnvironmentCopyDestroy(sg_field_environment_copy_t *copy)
{
	if (!copy)
		return;
	free(copy->slab_guards);
	free(copy->slabs);
	free(copy->guards);
	memset(copy, 0, sizeof(*copy));
}

static int AddSize(size_t left, size_t right, size_t *sum_out)
{
	if (!sum_out || left > SIZE_MAX - right)
		return 0;
	*sum_out = left + right;
	return 1;
}

static int EnvironmentCopyCreate(const sg_field_environment_t *environment,
	sg_field_environment_copy_t *copy)
{
	size_t slab_guard_count = 0U;
	size_t index;
	size_t cursor = 0U;

	if (!environment || !copy)
		return 0;
	memset(copy, 0, sizeof(*copy));
	copy->value = *environment;
	copy->guards = CopyArray(environment->guards, environment->guard_count,
		sizeof(*copy->guards));
	copy->slabs = CopyArray(environment->event_slabs,
		environment->event_slab_count, sizeof(*copy->slabs));
	if ((environment->guard_count != 0U && !copy->guards) ||
	    (environment->event_slab_count != 0U && !copy->slabs))
		goto fail;
	for (index = 0U; index < environment->event_slab_count; index++)
		if (!AddSize(slab_guard_count,
			environment->event_slabs[index].exogenous_guard_count,
			&slab_guard_count))
			goto fail;
	copy->slab_guards = CopyArray(NULL, 0U, sizeof(*copy->slab_guards));
	if (slab_guard_count != 0U)
	{
		copy->slab_guards = calloc(slab_guard_count,
			sizeof(*copy->slab_guards));
		if (!copy->slab_guards)
			goto fail;
	}
	for (index = 0U; index < environment->event_slab_count; index++)
	{
		const sg_field_event_slab_t *source = &environment->event_slabs[index];
		sg_field_event_slab_t *destination = &copy->slabs[index];
		size_t bytes;

		if (!SizeProduct(source->exogenous_guard_count,
			sizeof(*copy->slab_guards), &bytes))
			goto fail;
		if (bytes != 0U)
			memcpy(&copy->slab_guards[cursor], source->exogenous_guards,
				bytes);
		destination->exogenous_guards = bytes == 0U ? NULL :
			&copy->slab_guards[cursor];
		cursor += source->exogenous_guard_count;
	}
	copy->value.guards = copy->guards;
	copy->value.event_slabs = copy->slabs;
	return 1;

fail:
	return 0;
}

static int DestinationRefSame(const sg_destination_ref_t *left,
	const sg_destination_ref_t *right)
{
	if (!left || !right || left->kind != right->kind)
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
		return left->value.carrier.client_id == right->value.carrier.client_id &&
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

static int IntervalSame(const sg_destination_interval_t *left,
	const sg_destination_interval_t *right)
{
	return left->min_value == right->min_value &&
		left->max_value == right->max_value;
}

static int Interval3Same(const sg_destination_interval3_t *left,
	const sg_destination_interval3_t *right)
{
	return IntervalSame(&left->x, &right->x) &&
		IntervalSame(&left->y, &right->y) &&
		IntervalSame(&left->z, &right->z);
}

static int CaptureSame(const sg_destination_terminal_capture_t *left,
	const sg_destination_terminal_capture_t *right)
{
	size_t axis;

	if (left->anchor.owner_identity != right->anchor.owner_identity ||
	    !DestinationRefSame(&left->anchor.destination,
		&right->anchor.destination) ||
	    left->anchor.destination_generation !=
		right->anchor.destination_generation ||
	    left->anchor.local_elapsed_ms != right->anchor.local_elapsed_ms ||
	    !Interval3Same(&left->position_offset, &right->position_offset) ||
	    !Interval3Same(&left->velocity, &right->velocity) ||
	    !IntervalSame(&left->local_elapsed_ms, &right->local_elapsed_ms))
		return 0;
	for (axis = 0U; axis < 3U; axis++)
		if (left->anchor.position[axis] != right->anchor.position[axis] ||
		    left->anchor.velocity[axis] != right->anchor.velocity[axis])
			return 0;
	return 1;
}

static int TerminalDomainSame(const sg_destination_terminal_domain_t *left,
	const sg_destination_terminal_domain_t *right)
{
	return StableSame(&left->chart.value, &right->chart.value) &&
		StableSame(&left->domain.value, &right->domain.value);
}

static int TerminalSame(const sg_destination_terminal_t *left,
	const sg_destination_terminal_t *right)
{
	size_t index;

	if (left->owner_identity != right->owner_identity ||
	    !DestinationRefSame(&left->destination, &right->destination) ||
	    left->generation != right->generation || left->kind != right->kind)
		return 0;
	if (left->kind == SG_DESTINATION_TERMINAL_STATIC_PATCH)
		return TerminalDomainSame(&left->value.static_patch.domain,
			&right->value.static_patch.domain) &&
			CaptureSame(&left->value.static_patch.capture,
				&right->value.static_patch.capture);
	if (left->value.moving_tube.trajectory_identity !=
		right->value.moving_tube.trajectory_identity ||
	    left->value.moving_tube.segment_count !=
		right->value.moving_tube.segment_count)
		return 0;
	for (index = 0U; index < left->value.moving_tube.segment_count; index++)
	{
		const sg_destination_tube_segment_t *a =
			&left->value.moving_tube.segments[index];
		const sg_destination_tube_segment_t *b =
			&right->value.moving_tube.segments[index];
		if (a->valid_from_ms != b->valid_from_ms ||
		    a->valid_until_ms != b->valid_until_ms ||
		    !TerminalDomainSame(&a->domain, &b->domain) ||
		    !CaptureSame(&a->capture, &b->capture))
			return 0;
	}
	return 1;
}

static int GuardSame(const sg_field_guard_state_t *left,
	const sg_field_guard_state_t *right)
{
	return StableSame(&left->condition.value, &right->condition.value) &&
		left->truth == right->truth;
}

static int EnvironmentSame(const sg_field_environment_t *left,
	const sg_field_environment_t *right)
{
	size_t index;

	if (left->rune_identity != right->rune_identity ||
	    left->topology_revision != right->topology_revision ||
	    left->environment_revision != right->environment_revision ||
	    left->sampled_at_ms != right->sampled_at_ms ||
	    left->authority_identity != right->authority_identity ||
	    left->guard_count != right->guard_count ||
	    left->event_slab_count != right->event_slab_count ||
	    left->authenticated != right->authenticated)
		return 0;
	for (index = 0U; index < left->guard_count; index++)
		if (!GuardSame(&left->guards[index], &right->guards[index]))
			return 0;
	for (index = 0U; index < left->event_slab_count; index++)
	{
		const sg_field_event_slab_t *a = &left->event_slabs[index];
		const sg_field_event_slab_t *b = &right->event_slabs[index];
		size_t guard;
		if (a->valid_from_ms != b->valid_from_ms ||
		    a->valid_until_ms != b->valid_until_ms ||
		    a->exogenous_guard_count != b->exogenous_guard_count ||
		    !StableSame(&a->schedule_proof.value, &b->schedule_proof.value))
			return 0;
		for (guard = 0U; guard < a->exogenous_guard_count; guard++)
			if (!GuardSame(&a->exogenous_guards[guard],
				&b->exogenous_guards[guard]))
				return 0;
	}
	return 1;
}

static const sg_destination_tube_segment_t *ActiveSegment(
	const sg_destination_terminal_t *terminal, uint64_t now_ms)
{
	size_t low = 0U;
	size_t high;

	if (!terminal || terminal->kind != SG_DESTINATION_TERMINAL_MOVING_TUBE)
		return NULL;
	high = terminal->value.moving_tube.segment_count;
	while (low < high)
	{
		size_t middle = low + (high - low) / 2U;
		if (terminal->value.moving_tube.segments[middle].valid_until_ms <= now_ms)
			low = middle + 1U;
		else
			high = middle;
	}
	if (low >= terminal->value.moving_tube.segment_count ||
	    now_ms < terminal->value.moving_tube.segments[low].valid_from_ms)
		return NULL;
	return &terminal->value.moving_tube.segments[low];
}

static const sg_destination_terminal_capture_t *ActiveCapture(
	const sg_destination_terminal_t *terminal, uint64_t now_ms,
	const sg_destination_terminal_domain_t **domain_out)
{
	const sg_destination_tube_segment_t *segment;

	if (!terminal || !domain_out)
		return NULL;
	if (terminal->kind == SG_DESTINATION_TERMINAL_STATIC_PATCH)
	{
		*domain_out = &terminal->value.static_patch.domain;
		return &terminal->value.static_patch.capture;
	}
	segment = ActiveSegment(terminal, now_ms);
	if (!segment)
		return NULL;
	*domain_out = &segment->domain;
	return &segment->capture;
}

static const sg_field_guard_state_t *FindGuard(
	const sg_field_guard_state_t *guards, size_t guard_count,
	const sg_rune_guard_condition_ref_t *condition)
{
	size_t low = 0U;
	size_t high = guard_count;

	while (low < high)
	{
		size_t middle = low + (high - low) / 2U;
		int order = StableOrder(&guards[middle].condition.value,
			&condition->value);
		if (order < 0)
			low = middle + 1U;
		else
			high = middle;
	}
	if (low >= guard_count || !StableSame(&guards[low].condition.value,
		&condition->value))
		return NULL;
	return &guards[low];
}

static const sg_field_event_slab_t *FindEventSlab(
	const sg_field_environment_t *environment, uint64_t time_ms)
{
	size_t low = 0U;
	size_t high = environment->event_slab_count;

	while (low < high)
	{
		size_t middle = low + (high - low) / 2U;
		if (environment->event_slabs[middle].valid_until_ms <= time_ms)
			low = middle + 1U;
		else
			high = middle;
	}
	if (low >= environment->event_slab_count ||
	    time_ms < environment->event_slabs[low].valid_from_ms)
		return NULL;
	return &environment->event_slabs[low];
}

static sg_field_guard_truth_t GuardTruthAt(
	const sg_field_environment_t *environment,
	const sg_rune_guard_condition_ref_t *condition, uint64_t time_ms)
{
	const sg_field_event_slab_t *slab = FindEventSlab(environment, time_ms);
	const sg_field_guard_state_t *guard;

	if (slab)
	{
		guard = FindGuard(slab->exogenous_guards,
			slab->exogenous_guard_count, condition);
		if (guard)
			return guard->truth;
	}
	guard = FindGuard(environment->guards, environment->guard_count, condition);
	return guard ? guard->truth : SG_FIELD_GUARD_UNKNOWN;
}

static int ChoiceEnabled(const sg_rune_dynamics_model_t *model,
	const sg_field_choice_t *choice,
	const sg_field_environment_t *environment, uint64_t time_ms)
{
	size_t index;

	for (index = choice->guard_requirements.first;
	     index < (size_t)choice->guard_requirements.first +
		choice->guard_requirements.count; index++)
	{
		const sg_field_guard_requirement_t *requirement =
			&model->guard_requirements[index];
		if (GuardTruthAt(environment, &requirement->condition, time_ms) !=
		    requirement->required)
			return 0;
	}
	for (index = choice->outcomes.first;
	     index < (size_t)choice->outcomes.first + choice->outcomes.count;
	     index++)
	{
		const sg_field_outcome_t *outcome = &model->outcomes[index];
		size_t effect;
		if (outcome->absolute_time_advance.maximum_ms > UINT64_MAX - time_ms)
			return 0;
		for (effect = outcome->guard_effects.first;
		     effect < (size_t)outcome->guard_effects.first +
			outcome->guard_effects.count; effect++)
		{
			const sg_field_guard_effect_t *guard =
				&model->guard_effects[effect];
			if (GuardTruthAt(environment, &guard->condition, time_ms) !=
			    guard->required_before)
				return 0;
		}
	}
	return 1;
}

static int FindAtomIndex(const sg_rune_dynamics_model_t *model,
	const sg_field_reach_atom_ref_t *reference, uint32_t *index_out)
{
	size_t low = 0U;
	size_t high = model->reach_atom_count;

	while (low < high)
	{
		size_t middle = low + (high - low) / 2U;
		int order = StableOrder(&model->reach_atoms[middle].id.value,
			&reference->value);
		if (order < 0)
			low = middle + 1U;
		else
			high = middle;
	}
	if (low >= model->reach_atom_count ||
	    !StableSame(&model->reach_atoms[low].id.value, &reference->value) ||
	    low > UINT32_MAX)
		return 0;
	*index_out = (uint32_t)low;
	return 1;
}

static int DomainMatches(const sg_rune_dynamics_model_t *model,
	uint32_t atom_index, const sg_destination_terminal_domain_t *domain)
{
	const sg_field_reach_atom_t *atom = &model->reach_atoms[atom_index];
	size_t index;

	if (!StableSame(&atom->domain.value, &domain->domain.value))
		return 0;
	for (index = 0U; index < model->state_domain_count; index++)
		if (StableSame(&model->state_domains[index].id.value,
			&atom->domain.value))
			return StableSame(&model->state_domains[index].chart.value,
				&domain->chart.value);
	return 0;
}

static void SolutionDestroy(sg_field_solution_t *solution)
{
	if (!solution)
		return;
	free(solution->rule_outcomes);
	free(solution->rule_destinations);
	free(solution->rules);
	free(solution->accepted_kernels);
	free(solution->costs);
	free(solution->rank);
	free(solution->reachable);
	free(solution->terminal_atoms);
	free(solution->guard_truths);
	free(solution->regions);
	memset(solution, 0, sizeof(*solution));
}

static int CompareU64(const void *left, const void *right)
{
	const uint64_t a = *(const uint64_t *)left;
	const uint64_t b = *(const uint64_t *)right;

	return a < b ? -1 : a > b ? 1 : 0;
}

static int AppendBoundary(uint64_t *boundaries, size_t capacity,
	size_t *count, uint64_t boundary, uint64_t now_ms)
{
	if (boundary <= now_ms)
		return 1;
	if (*count >= capacity)
		return 0;
	boundaries[(*count)++] = boundary;
	return 1;
}

static int BuildTimeRegions(const sg_destination_terminal_t *terminal,
	const sg_field_environment_t *environment, uint64_t now_ms,
	sg_field_time_region_t **regions_out, size_t *region_count_out)
{
	size_t boundary_capacity = 1U;
	size_t boundary_count = 1U;
	uint64_t *boundaries;
	sg_field_time_region_t *regions;
	size_t index;
	size_t unique;

	if (!AddSize(boundary_capacity, environment->event_slab_count,
		&boundary_capacity) ||
	    !AddSize(boundary_capacity,
		environment->event_slab_count, &boundary_capacity) ||
	    (terminal->kind == SG_DESTINATION_TERMINAL_MOVING_TUBE &&
	     (!AddSize(boundary_capacity,
		terminal->value.moving_tube.segment_count, &boundary_capacity) ||
	      !AddSize(boundary_capacity,
		terminal->value.moving_tube.segment_count, &boundary_capacity))))
		return 0;
	boundaries = calloc(boundary_capacity, sizeof(*boundaries));
	if (!boundaries)
		return 0;
	boundaries[0] = now_ms;
	for (index = 0U; index < environment->event_slab_count; index++)
		if (!AppendBoundary(boundaries, boundary_capacity, &boundary_count,
			environment->event_slabs[index].valid_from_ms, now_ms) ||
		    !AppendBoundary(boundaries, boundary_capacity, &boundary_count,
			environment->event_slabs[index].valid_until_ms, now_ms))
		{
			free(boundaries);
			return 0;
		}
	if (terminal->kind == SG_DESTINATION_TERMINAL_MOVING_TUBE)
		for (index = 0U;
		     index < terminal->value.moving_tube.segment_count; index++)
			if (!AppendBoundary(boundaries, boundary_capacity,
				&boundary_count,
				terminal->value.moving_tube.segments[index].valid_from_ms,
				now_ms) ||
			    !AppendBoundary(boundaries, boundary_capacity,
				&boundary_count,
				terminal->value.moving_tube.segments[index].valid_until_ms,
				now_ms))
			{
				free(boundaries);
				return 0;
			}
	qsort(boundaries, boundary_count, sizeof(*boundaries), CompareU64);
	unique = 1U;
	for (index = 1U; index < boundary_count; index++)
		if (boundaries[index] != boundaries[unique - 1U])
			boundaries[unique++] = boundaries[index];
	regions = calloc(unique, sizeof(*regions));
	if (!regions)
	{
		free(boundaries);
		return 0;
	}
	for (index = 0U; index < unique; index++)
	{
		regions[index].valid_from_ms = boundaries[index];
		regions[index].valid_until_ms = index + 1U < unique ?
			boundaries[index + 1U] : UINT64_MAX;
	}
	free(boundaries);
	*regions_out = regions;
	*region_count_out = unique;
	return 1;
}

static size_t TimeRegionIndex(const sg_field_solution_t *solution,
	uint64_t time_ms)
{
	size_t low = 0U;
	size_t high = solution->region_count;

	while (low < high)
	{
		size_t middle = low + (high - low) / 2U;
		if (solution->regions[middle].valid_from_ms <= time_ms)
			low = middle + 1U;
		else
			high = middle;
	}
	return low == 0U ? SIZE_MAX : low - 1U;
}

static size_t GuardConditionIndex(const sg_field_environment_t *environment,
	const sg_rune_guard_condition_ref_t *condition)
{
	const sg_field_guard_state_t *guard = FindGuard(environment->guards,
		environment->guard_count, condition);

	return guard ? (size_t)(guard - environment->guards) : SIZE_MAX;
}

static int GuardVectorSame(const sg_field_solution_t *solution, size_t vector,
	const sg_field_guard_truth_t *truths)
{
	return memcmp(&solution->guard_truths[vector * solution->guard_count],
		truths, solution->guard_count * sizeof(*truths)) == 0;
}

static size_t FindGuardVector(const sg_field_solution_t *solution,
	const sg_field_guard_truth_t *truths)
{
	size_t vector;

	for (vector = 0U; vector < solution->guard_vector_count; vector++)
		if (GuardVectorSame(solution, vector, truths))
			return vector;
	return SIZE_MAX;
}

static int BuildGuardVectors(const sg_rune_dynamics_model_t *model,
	const sg_field_environment_t *environment, sg_field_solution_t *solution)
{
	sg_field_guard_truth_t *candidate;
	size_t index;
	size_t vector;

	solution->guard_count = environment->guard_count;
	if (solution->guard_count == 0U)
	{
		solution->guard_vector_count = 1U;
		return model->guard_requirement_count == 0U &&
			model->guard_effect_count == 0U;
	}
	for (index = 0U; index < model->guard_requirement_count; index++)
		if (GuardConditionIndex(environment,
			&model->guard_requirements[index].condition) == SIZE_MAX)
			return 0;
	for (index = 0U; index < model->guard_effect_count; index++)
		if (GuardConditionIndex(environment,
			&model->guard_effects[index].condition) == SIZE_MAX)
			return 0;
	solution->guard_truths = calloc(solution->guard_count,
		sizeof(*solution->guard_truths));
	candidate = calloc(solution->guard_count, sizeof(*candidate));
	if (!solution->guard_truths || !candidate)
	{
		free(candidate);
		return 0;
	}
	for (index = 0U; index < solution->guard_count; index++)
		solution->guard_truths[index] = environment->guards[index].truth;
	solution->guard_vector_count = 1U;
	for (vector = 0U; vector < solution->guard_vector_count; vector++)
		for (index = 0U; index < model->outcome_count; index++)
		{
			const sg_field_outcome_t *outcome = &model->outcomes[index];
			size_t effect;
			memcpy(candidate,
				&solution->guard_truths[vector * solution->guard_count],
				solution->guard_count * sizeof(*candidate));
			for (effect = outcome->guard_effects.first;
			     effect < (size_t)outcome->guard_effects.first +
				outcome->guard_effects.count; effect++)
			{
				const sg_field_guard_effect_t *record =
					&model->guard_effects[effect];
				size_t condition = GuardConditionIndex(environment,
					&record->condition);
				if (condition == SIZE_MAX)
				{
					free(candidate);
					return 0;
				}
				candidate[condition] = record->resulting_after;
			}
			if (FindGuardVector(solution, candidate) == SIZE_MAX)
			{
				size_t new_count;
				sg_field_guard_truth_t *grown;
				if (!AddSize(solution->guard_vector_count, 1U, &new_count) ||
				    new_count > SIZE_MAX / solution->guard_count)
				{
					free(candidate);
					return 0;
				}
				grown = realloc(solution->guard_truths,
					new_count * solution->guard_count * sizeof(*grown));
				if (!grown)
				{
					free(candidate);
					return 0;
				}
				solution->guard_truths = grown;
				memcpy(&solution->guard_truths[solution->guard_vector_count *
					solution->guard_count], candidate,
					solution->guard_count * sizeof(*candidate));
				solution->guard_vector_count = new_count;
			}
		}
	free(candidate);
	return 1;
}

static sg_field_guard_truth_t GuardVectorTruthAt(
	const sg_field_solution_t *solution,
	const sg_field_environment_t *environment, size_t vector,
	const sg_rune_guard_condition_ref_t *condition, uint64_t time_ms)
{
	const sg_field_event_slab_t *slab = FindEventSlab(environment, time_ms);
	const sg_field_guard_state_t *exogenous;
	size_t condition_index;

	if (slab)
	{
		exogenous = FindGuard(slab->exogenous_guards,
			slab->exogenous_guard_count, condition);
		if (exogenous)
			return exogenous->truth;
	}
	condition_index = GuardConditionIndex(environment, condition);
	return condition_index == SIZE_MAX ? SG_FIELD_GUARD_UNKNOWN :
		solution->guard_truths[vector * solution->guard_count +
			condition_index];
}

static int ChoiceEnabledForVector(const sg_rune_dynamics_model_t *model,
	const sg_field_solution_t *solution,
	const sg_field_environment_t *environment, size_t vector,
	const sg_field_choice_t *choice, uint64_t time_ms)
{
	size_t index;

	for (index = choice->guard_requirements.first;
	     index < (size_t)choice->guard_requirements.first +
		choice->guard_requirements.count; index++)
		if (GuardVectorTruthAt(solution, environment, vector,
			&model->guard_requirements[index].condition, time_ms) !=
		    model->guard_requirements[index].required)
			return 0;
	for (index = choice->outcomes.first;
	     index < (size_t)choice->outcomes.first + choice->outcomes.count;
	     index++)
	{
		const sg_field_outcome_t *outcome = &model->outcomes[index];
		size_t effect;
		for (effect = outcome->guard_effects.first;
		     effect < (size_t)outcome->guard_effects.first +
			outcome->guard_effects.count; effect++)
			if (GuardVectorTruthAt(solution, environment, vector,
				&model->guard_effects[effect].condition, time_ms) !=
			    model->guard_effects[effect].required_before)
				return 0;
	}
	return 1;
}

static size_t OutcomeGuardVector(const sg_rune_dynamics_model_t *model,
	const sg_field_solution_t *solution,
	const sg_field_environment_t *environment, size_t vector,
	const sg_field_outcome_t *outcome)
{
	sg_field_guard_truth_t *candidate;
	size_t effect;
	size_t result;

	if (solution->guard_count == 0U)
		return 0U;
	candidate = malloc(solution->guard_count * sizeof(*candidate));
	if (!candidate)
		return SIZE_MAX;
	memcpy(candidate, &solution->guard_truths[vector * solution->guard_count],
		solution->guard_count * sizeof(*candidate));
	for (effect = outcome->guard_effects.first;
	     effect < (size_t)outcome->guard_effects.first +
		outcome->guard_effects.count; effect++)
	{
		const sg_field_guard_effect_t *record = &model->guard_effects[effect];
		size_t condition = GuardConditionIndex(environment, &record->condition);
		if (condition == SIZE_MAX)
		{
			free(candidate);
			return SIZE_MAX;
		}
		candidate[condition] = record->resulting_after;
	}
	result = FindGuardVector(solution, candidate);
	free(candidate);
	return result;
}

static int NodeIndex(const sg_field_solution_t *solution, size_t vector,
	size_t region, uint32_t atom, uint32_t *node_out)
{
	size_t node;
	size_t product_region;

	if (solution->atom_count == 0U || solution->region_count == 0U ||
	    vector >= solution->guard_vector_count ||
	    region >= solution->region_count || atom >= solution->atom_count ||
	    vector > (SIZE_MAX - region) / solution->region_count)
		return 0;
	product_region = vector * solution->region_count + region;
	if (product_region > (SIZE_MAX - atom) / solution->atom_count)
		return 0;
	node = product_region * solution->atom_count + atom;
	if (node > UINT32_MAX)
		return 0;
	*node_out = (uint32_t)node;
	return 1;
}

static int RuleWorkReserve(sg_field_solution_t *solution,
	size_t additional_rules, size_t additional_destinations)
{
	size_t rules;
	size_t destinations;
	sg_field_bellman_rule_t *new_rules;
	uint32_t *new_destinations;
	uint32_t *new_outcomes;

	if (!AddSize(solution->rule_count, additional_rules, &rules) ||
	    !AddSize(solution->rule_destination_count, additional_destinations,
		&destinations) || rules > UINT32_MAX || destinations > UINT32_MAX)
		return 0;
	new_rules = realloc(solution->rules, rules * sizeof(*new_rules));
	if (!new_rules && rules != 0U)
		return 0;
	solution->rules = new_rules;
	new_destinations = realloc(solution->rule_destinations,
		destinations * sizeof(*new_destinations));
	if (!new_destinations && destinations != 0U)
		return 0;
	solution->rule_destinations = new_destinations;
	new_outcomes = realloc(solution->rule_outcomes,
		destinations * sizeof(*new_outcomes));
	if (!new_outcomes && destinations != 0U)
		return 0;
	solution->rule_outcomes = new_outcomes;
	return 1;
}

static int ArrivalRegions(const sg_field_solution_t *solution,
	size_t source_region, const sg_field_outcome_t *outcome,
	size_t *first_out, size_t *last_out)
{
	const sg_field_time_region_t *source = &solution->regions[source_region];
	uint64_t first_time;
	uint64_t last_time;

	if (source_region + 1U == solution->region_count)
	{
		*first_out = source_region;
		*last_out = source_region;
		return 1;
	}
	if (source->valid_from_ms >
		UINT64_MAX - outcome->absolute_time_advance.minimum_ms)
		return 0;
	first_time = source->valid_from_ms +
		outcome->absolute_time_advance.minimum_ms;
	if (source->valid_until_ms - 1U >
		UINT64_MAX - outcome->absolute_time_advance.maximum_ms)
		last_time = UINT64_MAX;
	else
		last_time = source->valid_until_ms - 1U +
			outcome->absolute_time_advance.maximum_ms;
	*first_out = TimeRegionIndex(solution, first_time);
	*last_out = TimeRegionIndex(solution, last_time);
	return *first_out != SIZE_MAX && *last_out != SIZE_MAX;
}

static int AppendRuleDestination(sg_field_solution_t *solution,
	uint32_t destination, uint32_t outcome)
{
	size_t index;
	sg_field_bellman_rule_t *rule =
		&solution->rules[solution->rule_count - 1U];

	for (index = rule->destinations.first;
	     index < solution->rule_destination_count; index++)
		if (solution->rule_destinations[index] == destination &&
		    solution->rule_outcomes[index] == outcome)
			return 1;
	if (!RuleWorkReserve(solution, 0U, 1U))
		return 0;
	solution->rule_destinations[solution->rule_destination_count] = destination;
	solution->rule_outcomes[solution->rule_destination_count] = outcome;
	solution->rule_destination_count++;
	rule = &solution->rules[solution->rule_count - 1U];
	rule->destinations.count++;
	return 1;
}

static int BeginRule(sg_field_solution_t *solution, uint32_t source_node,
	uint32_t choice_index, uint32_t kernel_index)
{
	sg_field_bellman_rule_t *rule;

	if (!RuleWorkReserve(solution, 1U, 0U))
		return 0;
	rule = &solution->rules[solution->rule_count++];
	memset(rule, 0, sizeof(*rule));
	rule->source_node = source_node;
	rule->choice_index = choice_index;
	rule->kernel_index = kernel_index;
	rule->destinations.first = (uint32_t)solution->rule_destination_count;
	return 1;
}

static void DiscardEmptyRule(sg_field_solution_t *solution)
{
	if (solution->rule_count != 0U &&
	    solution->rules[solution->rule_count - 1U].destinations.count == 0U)
		solution->rule_count--;
}

static int AppendOutcomeDestinations(const sg_rune_dynamics_model_t *model,
	const sg_field_environment_t *environment, sg_field_solution_t *solution,
	size_t source_vector, size_t source_region,
	const sg_field_outcome_t *outcome, uint32_t outcome_index,
	const sg_field_progress_target_span_t *progress_targets)
{
	size_t first_region;
	size_t last_region;
	size_t region;
	size_t item;
	size_t destination_vector = OutcomeGuardVector(model, solution,
		environment, source_vector, outcome);

	if (destination_vector == SIZE_MAX ||
	    !ArrivalRegions(solution, source_region, outcome,
		&first_region, &last_region))
		return 0;
	if (progress_targets)
	{
		for (item = progress_targets->first;
		     item < (size_t)progress_targets->first + progress_targets->count;
		     item++)
		{
			uint32_t atom;
			if (!StableSame(&model->local_progress_targets[item].outcome.value,
				&outcome->id.value))
				continue;
			if (!FindAtomIndex(model,
				&model->local_progress_targets[item].atom, &atom))
				return 0;
			for (region = first_region; region <= last_region; region++)
			{
				uint32_t node;
				if (!NodeIndex(solution, destination_vector, region, atom, &node) ||
				    !AppendRuleDestination(solution, node, outcome_index))
					return 0;
			}
		}
		return 1;
	}
	for (item = outcome->destination_cover.first;
	     item < (size_t)outcome->destination_cover.first +
		outcome->destination_cover.count; item++)
	{
		uint32_t atom;
		if (!FindAtomIndex(model, &model->outcome_cover_pieces[item].atom,
			&atom))
			return 0;
		for (region = first_region; region <= last_region; region++)
		{
			uint32_t node;
			if (!NodeIndex(solution, destination_vector, region, atom, &node) ||
			    !AppendRuleDestination(solution, node, outcome_index))
				return 0;
		}
	}
	return 1;
}

static int BuildProductRules(const sg_rune_dynamics_model_t *model,
	const sg_destination_terminal_t *terminal,
	const sg_field_environment_t *environment, sg_field_solution_t *solution)
{
	size_t region;
	size_t vector;
	size_t choice_index;
	size_t kernel_index;

	for (region = 0U; region < solution->region_count; region++)
	{
		const sg_destination_terminal_domain_t *domain = NULL;
		const sg_destination_terminal_capture_t *capture = ActiveCapture(terminal,
			solution->regions[region].valid_from_ms, &domain);
		for (kernel_index = 0U;
		     capture && domain && kernel_index <
			model->local_progress_kernel_count; kernel_index++)
		{
			const sg_field_local_progress_kernel_t *kernel =
				&model->local_progress_kernels[kernel_index];
			uint32_t target;
			if (!SG_FieldLocalProgressKernelAcceptsCapture(kernel, capture) ||
			    !FindAtomIndex(model, &kernel->target_atom, &target) ||
			    !DomainMatches(model, target, domain))
				continue;
			solution->accepted_kernels[region *
				model->local_progress_kernel_count + kernel_index] = 1U;
			for (vector = 0U; vector < solution->guard_vector_count; vector++)
			{
				uint32_t node;
				if (!NodeIndex(solution, vector, region, target, &node))
					return 0;
				solution->terminal_atoms[node] = 1U;
			}
		}
		for (vector = 0U; vector < solution->guard_vector_count; vector++)
		{
			for (choice_index = 0U; choice_index < model->choice_count;
			     choice_index++)
			{
				const sg_field_choice_t *choice = &model->choices[choice_index];
				uint32_t source_atom;
				uint32_t source_node;
				size_t outcome_index;
				if (!ChoiceEnabledForVector(model, solution, environment, vector,
					choice, solution->regions[region].valid_from_ms) ||
				    !FindAtomIndex(model, &choice->source_atom, &source_atom) ||
				    !NodeIndex(solution, vector, region, source_atom,
					&source_node))
					continue;
				if (!BeginRule(solution, source_node, (uint32_t)choice_index,
					FIELD_NO_KERNEL))
					return 0;
				for (outcome_index = choice->outcomes.first;
				     outcome_index < (size_t)choice->outcomes.first +
					choice->outcomes.count; outcome_index++)
					if (!AppendOutcomeDestinations(model, environment, solution,
						vector, region, &model->outcomes[outcome_index],
						(uint32_t)outcome_index, NULL))
						return 0;
				DiscardEmptyRule(solution);
			}
			for (kernel_index = 0U;
			     kernel_index < model->local_progress_kernel_count; kernel_index++)
			{
				const sg_field_local_progress_kernel_t *kernel =
					&model->local_progress_kernels[kernel_index];
				size_t choice_ref;
				uint32_t source_atom;
				uint32_t source_node;
				if (!solution->accepted_kernels[region *
					model->local_progress_kernel_count + kernel_index] ||
				    !FindAtomIndex(model, &kernel->source_atom, &source_atom) ||
				    !NodeIndex(solution, vector, region, source_atom,
					&source_node))
					continue;
				for (choice_ref = kernel->admissible_choices.first;
				     choice_ref < (size_t)kernel->admissible_choices.first +
					kernel->admissible_choices.count; choice_ref++)
				{
					const sg_field_choice_t *choice;
					size_t outcome_index;
					for (choice_index = 0U; choice_index < model->choice_count;
					     choice_index++)
						if (StableSame(&model->choices[choice_index].id.value,
							&model->local_progress_choices[choice_ref].value))
							break;
					if (choice_index == model->choice_count)
						continue;
					choice = &model->choices[choice_index];
					if (!ChoiceEnabledForVector(model, solution, environment,
						vector, choice,
						solution->regions[region].valid_from_ms))
						continue;
					if (!BeginRule(solution, source_node,
						(uint32_t)choice_index, (uint32_t)kernel_index))
						return 0;
					for (outcome_index = choice->outcomes.first;
					     outcome_index < (size_t)choice->outcomes.first +
						choice->outcomes.count; outcome_index++)
						if (!AppendOutcomeDestinations(model, environment,
							solution, vector, region,
							&model->outcomes[outcome_index],
							(uint32_t)outcome_index,
							&kernel->whole_outcome_targets))
							return 0;
					DiscardEmptyRule(solution);
				}
			}
		}
	}
	return 1;
}

static int SafeAddU64(uint64_t left, uint64_t right, uint64_t *sum_out)
{
	if (left > UINT64_MAX - right)
		return 0;
	*sum_out = left + right;
	return 1;
}

static int SafeMultiplyU64(uint64_t left, uint64_t right,
	uint64_t *product_out)
{
	if (right != 0U && left > UINT64_MAX / right)
		return 0;
	*product_out = left * right;
	return 1;
}

static int RuleIntervalCost(const sg_rune_dynamics_model_t *model,
	const sg_field_solution_t *solution, const sg_field_bellman_rule_t *rule,
	sg_rune_cost_bounds_t *cost_out, sg_rune_cost_bounds_t *endpoint_out)
{
	const sg_field_choice_t *choice = &model->choices[rule->choice_index];
	uint64_t worst_lower = 0U;
	uint64_t worst_upper = 0U;
	uint64_t progress_cost = 0U;
	size_t outcome_index;

	for (outcome_index = choice->outcomes.first;
	     outcome_index < (size_t)choice->outcomes.first +
		choice->outcomes.count; outcome_index++)
	{
		const sg_field_outcome_t *outcome = &model->outcomes[outcome_index];
		uint64_t destination_lower = 0U;
		uint64_t destination_upper = 0U;
		uint64_t time_lower;
		uint64_t time_upper;
		uint64_t outcome_lower;
		uint64_t outcome_upper;
		size_t item;
		int found = 0;
		for (item = rule->destinations.first;
		     item < (size_t)rule->destinations.first +
			rule->destinations.count; item++)
		{
			uint32_t destination = solution->rule_destinations[item];
			if (solution->rule_outcomes[item] != outcome_index)
				continue;
			if (!solution->reachable[destination] ||
			    solution->costs[destination].lower_us ==
				SG_RUNE_FIELD_COST_INFINITE ||
			    solution->costs[destination].upper_us ==
				SG_RUNE_FIELD_COST_INFINITE)
				return 0;
			if (!found || solution->costs[destination].lower_us >
				destination_lower)
				destination_lower = solution->costs[destination].lower_us;
			if (!found || solution->costs[destination].upper_us >
				destination_upper)
				destination_upper = solution->costs[destination].upper_us;
			found = 1;
		}
		if (!found || !SafeMultiplyU64(
			outcome->absolute_time_advance.minimum_ms, UINT64_C(1000),
			&time_lower) || !SafeMultiplyU64(
			outcome->absolute_time_advance.maximum_ms, UINT64_C(1000),
			&time_upper) ||
		    !SafeAddU64(destination_lower, time_lower, &outcome_lower) ||
		    !SafeAddU64(destination_upper, time_upper, &outcome_upper))
			return 0;
		if (outcome_lower > worst_lower)
			worst_lower = outcome_lower;
		if (outcome_upper > worst_upper)
			worst_upper = outcome_upper;
	}
	endpoint_out->lower_us = worst_lower;
	endpoint_out->upper_us = worst_upper;
	if (rule->kernel_index != FIELD_NO_KERNEL)
	{
		long double decrease = ceill((long double)model->local_progress_kernels[
			rule->kernel_index].minimum_lyapunov_decrease *
			(long double)model->error_contract.cost_quantum_us);
		if (!isfinite(decrease) || decrease <= 0.0L ||
		    decrease >= (long double)UINT64_MAX)
			return 0;
		progress_cost = (uint64_t)decrease;
	}
	return SafeAddU64(choice->cost.lower_us, progress_cost,
		&cost_out->lower_us) &&
		SafeAddU64(cost_out->lower_us, worst_lower,
			&cost_out->lower_us) &&
		SafeAddU64(choice->cost.upper_us, progress_cost,
			&cost_out->upper_us) &&
		SafeAddU64(cost_out->upper_us, worst_upper,
			&cost_out->upper_us) &&
		cost_out->upper_us < SG_RUNE_FIELD_COST_INFINITE;
}

static const sg_field_refinement_node_t *FindRefinementNodeById(
	const sg_rune_dynamics_model_t *model,
	const sg_field_refinement_node_ref_t *reference)
{
	size_t index;

	for (index = 0U; index < model->refinement_tree.node_count; index++)
		if (StableSame(&model->refinement_tree.nodes[index].id.value,
			&reference->value))
			return &model->refinement_tree.nodes[index];
	return NULL;
}

static int KernelCoversLeaf(const sg_rune_dynamics_model_t *model,
	const sg_field_local_progress_kernel_t *kernel,
	const sg_field_refinement_node_t *leaf)
{
	size_t index;

	for (index = kernel->covered_sources.first;
	     index < (size_t)kernel->covered_sources.first +
		kernel->covered_sources.count; index++)
	{
		const sg_field_refinement_node_t *covered = FindRefinementNodeById(
			model, &model->local_progress_sources[index]);
		const sg_field_refinement_node_t *cursor = leaf;
		if (!covered)
			return 0;
		for (;;)
		{
			if (StableSame(&covered->id.value, &cursor->id.value))
				return 1;
			if (cursor->parent == UINT32_MAX ||
			    cursor->parent >= model->refinement_tree.node_count)
				break;
			cursor = &model->refinement_tree.nodes[cursor->parent];
		}
	}
	return 0;
}

static int KernelCoversSourceAtom(const sg_rune_dynamics_model_t *model,
	const sg_field_local_progress_kernel_t *kernel)
{
	size_t node;
	int found_leaf = 0;

	for (node = 0U; node < model->refinement_tree.node_count; node++)
	{
		const sg_field_refinement_node_t *leaf =
			&model->refinement_tree.nodes[node];
		if (leaf->children.count != 0U ||
		    !StableSame(&leaf->atom.value, &kernel->source_atom.value))
			continue;
		found_leaf = 1;
		if (!KernelCoversLeaf(model, kernel, leaf))
			return 0;
	}
	return found_leaf;
}

static int RuleHasKernelRepresentation(const sg_rune_dynamics_model_t *model,
	const sg_field_solution_t *solution, const sg_field_bellman_rule_t *rule)
{
	size_t index;

	if (rule->kernel_index != FIELD_NO_KERNEL)
		return 0;
	for (index = 0U; index < solution->rule_count; index++)
		if (solution->rules[index].source_node == rule->source_node &&
		    solution->rules[index].choice_index == rule->choice_index &&
		    solution->rules[index].kernel_index != FIELD_NO_KERNEL &&
		    KernelCoversSourceAtom(model, &model->local_progress_kernels[
			solution->rules[index].kernel_index]))
			return 1;
	return 0;
}

static sg_field_status_t BuildBellmanCosts(
	const sg_rune_dynamics_model_t *model, sg_field_solution_t *solution,
	const uint8_t *recompute)
{
	size_t node;
	int changed;

	for (node = 0U; node < solution->node_count; node++)
	{
		if (recompute && !recompute[node])
			continue;
		solution->costs[node] = (sg_rune_cost_bounds_t){
			SG_RUNE_FIELD_COST_INFINITE, SG_RUNE_FIELD_COST_INFINITE };
		if (solution->terminal_atoms[node])
			solution->costs[node] = (sg_rune_cost_bounds_t){ 0U, 0U };
	}
	do
	{
		changed = 0;
		for (node = 0U; node < solution->node_count; node++)
		{
			uint64_t lower = SG_RUNE_FIELD_COST_INFINITE;
			uint64_t upper = SG_RUNE_FIELD_COST_INFINITE;
			uint64_t fallback_lower = SG_RUNE_FIELD_COST_INFINITE;
			uint64_t fallback_upper = SG_RUNE_FIELD_COST_INFINITE;
			size_t rule_index;
			if ((recompute && !recompute[node]) ||
			    !solution->reachable[node] || solution->terminal_atoms[node])
				continue;
			for (rule_index = 0U; rule_index < solution->rule_count;
			     rule_index++)
			{
				sg_rune_cost_bounds_t candidate;
				sg_rune_cost_bounds_t endpoint;
				if (solution->rules[rule_index].source_node != node ||
				    (solution->rules[rule_index].kernel_index != FIELD_NO_KERNEL &&
				     !KernelCoversSourceAtom(model,
					&model->local_progress_kernels[solution->rules[
						rule_index].kernel_index])) ||
				    RuleHasKernelRepresentation(model, solution,
					&solution->rules[rule_index]) ||
				    !RuleIntervalCost(model, solution,
					&solution->rules[rule_index], &candidate, &endpoint))
					continue;
				if (solution->rules[rule_index].kernel_index ==
					FIELD_NO_KERNEL &&
				    candidate.lower_us <= endpoint.upper_us)
				{
					if (candidate.lower_us < fallback_lower)
						fallback_lower = candidate.lower_us;
					if (candidate.upper_us < fallback_upper)
						fallback_upper = candidate.upper_us;
					continue;
				}
				if (candidate.lower_us < lower)
					lower = candidate.lower_us;
				if (candidate.upper_us < upper)
					upper = candidate.upper_us;
			}
			if (lower == SG_RUNE_FIELD_COST_INFINITE ||
			    upper == SG_RUNE_FIELD_COST_INFINITE)
			{
				lower = fallback_lower;
				upper = fallback_upper;
			}
			if (lower == SG_RUNE_FIELD_COST_INFINITE ||
			    upper == SG_RUNE_FIELD_COST_INFINITE)
				continue;
			if (lower > solution->costs[node].lower_us ||
			    upper > solution->costs[node].upper_us)
				return SG_FIELD_STATUS_NUMERICAL_ERROR;
			if (lower != solution->costs[node].lower_us ||
			    upper != solution->costs[node].upper_us)
			{
				solution->costs[node] =
					(sg_rune_cost_bounds_t){ lower, upper };
				changed = 1;
			}
		}
	} while (changed);
	for (node = 0U; node < solution->node_count; node++)
		if (solution->reachable[node] &&
		    (solution->costs[node].lower_us == SG_RUNE_FIELD_COST_INFINITE ||
		     solution->costs[node].upper_us == SG_RUNE_FIELD_COST_INFINITE ||
		     solution->costs[node].lower_us > solution->costs[node].upper_us ||
		     solution->costs[node].upper_us - solution->costs[node].lower_us >
			model->error_contract.maximum_value_width_us))
			return SG_FIELD_STATUS_NUMERICAL_ERROR;
	return SG_FIELD_STATUS_OK;
}

static sg_field_status_t MaximumLocalSpanNew(
	const sg_rune_dynamics_model_t *model, uint64_t *span_out)
{
	long double maximum = 0.0L;
	size_t kernel_index;

	for (kernel_index = 0U; kernel_index < model->local_progress_kernel_count;
	     kernel_index++)
	{
		const sg_field_local_progress_kernel_t *kernel =
			&model->local_progress_kernels[kernel_index];
		long double value = (long double)kernel->lyapunov_constant;
		uint32_t source_atom;
		size_t dimension;
		if (!FindAtomIndex(model, &kernel->source_atom, &source_atom))
			return SG_FIELD_STATUS_PROOF_FAILED;
		for (dimension = 0U; dimension < SG_RUNE_STATE_DIMENSION_COUNT;
		     dimension++)
		{
			const sg_rune_flow_enclosure_t *state_flow =
				&model->reach_atoms[source_atom].state_bounds;
			const sg_rune_flow_enclosure_t *anchor_flow =
				&kernel->terminal_parameters.anchor_bounds;
			const sg_rune_interval_t *state = dimension < 3U ?
				(dimension == 0U ? &state_flow->position.x :
				 dimension == 1U ? &state_flow->position.y :
				 &state_flow->position.z) : dimension < 6U ?
				(dimension == 3U ? &state_flow->velocity.x :
				 dimension == 4U ? &state_flow->velocity.y :
				 &state_flow->velocity.z) : &state_flow->elapsed_ms;
			const sg_rune_interval_t *anchor = dimension < 3U ?
				(dimension == 0U ? &anchor_flow->position.x :
				 dimension == 1U ? &anchor_flow->position.y :
				 &anchor_flow->position.z) : dimension < 6U ?
				(dimension == 3U ? &anchor_flow->velocity.x :
				 dimension == 4U ? &anchor_flow->velocity.y :
				 &anchor_flow->velocity.z) : &anchor_flow->elapsed_ms;
			long double a = kernel->state_lyapunov[dimension];
			long double b = kernel->anchor_lyapunov[dimension];
			value += a >= 0.0L ? a * state->max_value : a * state->min_value;
			value += b >= 0.0L ? b * anchor->max_value : b * anchor->min_value;
		}
		if (!isfinite(value))
			return SG_FIELD_STATUS_NUMERICAL_ERROR;
		if (value > maximum)
			maximum = value;
	}
	if (maximum < 0.0L)
		maximum = 0.0L;
	maximum = ceill(maximum *
		(long double)model->error_contract.cost_quantum_us);
	if (!isfinite(maximum) || maximum >= UINT64_MAX)
		return SG_FIELD_STATUS_NUMERICAL_ERROR;
	*span_out = (uint64_t)maximum;
	return SG_FIELD_STATUS_OK;
}

static sg_field_status_t SolutionClassify(
	const sg_rune_dynamics_model_t *model,
	const sg_destination_terminal_t *terminal,
	const sg_field_environment_t *environment, uint64_t now_ms,
	sg_field_solution_t *solution)
{
	sg_field_attractor_choice_t *graph_rules = NULL;
	sg_field_attractor_graph_t graph = { 0 };
	sg_field_attractor_result_t result = { 0 };
	sg_field_status_t status = SG_FIELD_STATUS_STORAGE;
	size_t index;

	memset(solution, 0, sizeof(*solution));
	solution->atom_count = model->reach_atom_count;
	for (index = 0U; index < model->guard_requirement_count; index++)
		if (GuardConditionIndex(environment,
			&model->guard_requirements[index].condition) == SIZE_MAX)
		{
			status = SG_FIELD_STATUS_MODEL_INCOMPLETE;
			goto done;
		}
	for (index = 0U; index < model->guard_effect_count; index++)
		if (GuardConditionIndex(environment,
			&model->guard_effects[index].condition) == SIZE_MAX)
		{
			status = SG_FIELD_STATUS_MODEL_INCOMPLETE;
			goto done;
		}
	for (index = 0U; index < model->outcome_count; index++)
		if (model->outcomes[index].absolute_time_advance.maximum_ms >
			UINT64_MAX - now_ms)
		{
			status = SG_FIELD_STATUS_NUMERICAL_ERROR;
			goto done;
		}
	if (!BuildTimeRegions(terminal, environment, now_ms, &solution->regions,
		&solution->region_count) || solution->atom_count == 0U ||
	    !BuildGuardVectors(model, environment, solution) ||
	    solution->guard_vector_count == 0U ||
	    solution->region_count > SIZE_MAX / solution->guard_vector_count ||
	    solution->region_count * solution->guard_vector_count >
		SIZE_MAX / solution->atom_count)
		goto done;
	solution->node_count = solution->guard_vector_count *
		solution->region_count * solution->atom_count;
	if (solution->node_count > UINT32_MAX)
	{
		status = SG_FIELD_STATUS_CAPACITY;
		goto done;
	}
	solution->terminal_atoms = calloc(solution->node_count,
		sizeof(*solution->terminal_atoms));
	solution->reachable = calloc(solution->node_count,
		sizeof(*solution->reachable));
	solution->rank = calloc(solution->node_count, sizeof(*solution->rank));
	solution->costs = calloc(solution->node_count, sizeof(*solution->costs));
	if (solution->region_count > SIZE_MAX /
		model->local_progress_kernel_count)
	{
		status = SG_FIELD_STATUS_CAPACITY;
		goto done;
	}
	solution->accepted_kernels = calloc(solution->region_count *
		model->local_progress_kernel_count,
		sizeof(*solution->accepted_kernels));
	if (!solution->terminal_atoms || !solution->reachable || !solution->rank ||
	    !solution->costs || !solution->accepted_kernels ||
	    !BuildProductRules(model, terminal, environment, solution))
		goto done;
	graph_rules = solution->rule_count == 0U ? NULL :
		calloc(solution->rule_count, sizeof(*graph_rules));
	if (solution->rule_count != 0U && !graph_rules)
		goto done;
	for (index = 0U; index < solution->rule_count; index++)
	{
		graph_rules[index].source_state = solution->rules[index].source_node;
		graph_rules[index].destinations = solution->rules[index].destinations;
	}
	graph.state_count = solution->node_count;
	graph.terminal_states = solution->terminal_atoms;
	graph.choices = graph_rules;
	graph.choice_count = solution->rule_count;
	graph.choice_destinations = solution->rule_destinations;
	graph.choice_destination_count = solution->rule_destination_count;
	switch (SG_FieldAttractorSolve(&graph, &result))
	{
	case SG_FIELD_ATTRACTOR_OK:
		break;
	case SG_FIELD_ATTRACTOR_STORAGE_FAILURE:
		goto done;
	case SG_FIELD_ATTRACTOR_INVALID:
	default:
		status = SG_FIELD_STATUS_PROOF_FAILED;
		goto done;
	}
	memcpy(solution->reachable, result.reachable,
		solution->node_count * sizeof(*solution->reachable));
	memcpy(solution->rank, result.rank,
		solution->node_count * sizeof(*solution->rank));
	for (index = 0U; index < solution->atom_count; index++)
		if (solution->terminal_atoms[index])
			break;
	if (index == solution->atom_count)
	{
		status = SG_FIELD_STATUS_MODEL_INCOMPLETE;
		goto done;
	}
	status = MaximumLocalSpanNew(model, &solution->local_span_us);

done:
	SG_FieldAttractorResultDestroy(&result);
	free(graph_rules);
	return status;
}

static sg_field_status_t SolutionBuild(const sg_rune_dynamics_model_t *model,
	const sg_destination_terminal_t *terminal,
	const sg_field_environment_t *environment, uint64_t now_ms,
	sg_field_solution_t *solution)
{
	sg_field_status_t status = SolutionClassify(model, terminal, environment,
		now_ms, solution);

	return status == SG_FIELD_STATUS_OK ?
		BuildBellmanCosts(model, solution, NULL) : status;
}

static int NodeRulesSame(const sg_field_solution_t *left,
	const sg_field_solution_t *right, uint32_t node)
{
	size_t left_index = 0U;
	size_t right_index = 0U;

	for (;;)
	{
		const sg_field_bellman_rule_t *left_rule;
		const sg_field_bellman_rule_t *right_rule;
		size_t destination;
		while (left_index < left->rule_count &&
		       left->rules[left_index].source_node != node)
			left_index++;
		while (right_index < right->rule_count &&
		       right->rules[right_index].source_node != node)
			right_index++;
		if (left_index == left->rule_count ||
		    right_index == right->rule_count)
			return left_index == left->rule_count &&
				right_index == right->rule_count;
		left_rule = &left->rules[left_index++];
		right_rule = &right->rules[right_index++];
		if (left_rule->choice_index != right_rule->choice_index ||
		    left_rule->kernel_index != right_rule->kernel_index ||
		    left_rule->destinations.count != right_rule->destinations.count)
			return 0;
		for (destination = 0U;
		     destination < left_rule->destinations.count; destination++)
		{
			size_t left_destination =
				(size_t)left_rule->destinations.first + destination;
			size_t right_destination =
				(size_t)right_rule->destinations.first + destination;
			if (left->rule_destinations[left_destination] !=
				right->rule_destinations[right_destination] ||
			    left->rule_outcomes[left_destination] !=
				right->rule_outcomes[right_destination])
				return 0;
		}
	}
}

static void MarkRulePredecessors(const sg_field_solution_t *solution,
	uint8_t *affected, int *changed)
{
	size_t rule_index;

	for (rule_index = 0U; rule_index < solution->rule_count; rule_index++)
	{
		const sg_field_bellman_rule_t *rule = &solution->rules[rule_index];
		size_t destination;
		if (affected[rule->source_node])
			continue;
		for (destination = rule->destinations.first;
		     destination < (size_t)rule->destinations.first +
			rule->destinations.count; destination++)
			if (affected[solution->rule_destinations[destination]])
			{
				affected[rule->source_node] = 1U;
				*changed = 1;
				break;
			}
	}
}

static int SolutionProductShapeSame(const sg_rune_dynamics_model_t *model,
	const sg_field_solution_t *left, const sg_field_solution_t *right)
{
	size_t truth_count;

	if (left->atom_count != right->atom_count ||
	    left->node_count != right->node_count ||
	    left->region_count != right->region_count ||
	    left->guard_vector_count != right->guard_vector_count ||
	    left->guard_count != right->guard_count ||
	    left->local_span_us != right->local_span_us ||
	    left->region_count > SIZE_MAX / model->local_progress_kernel_count ||
	    (left->guard_count != 0U &&
	     left->guard_vector_count > SIZE_MAX / left->guard_count))
		return 0;
	truth_count = left->guard_vector_count * left->guard_count;
	return truth_count == 0U || memcmp(left->guard_truths,
		right->guard_truths, truth_count * sizeof(*left->guard_truths)) == 0;
}

static sg_field_status_t SolutionBuildIncremental(
	const sg_rune_dynamics_model_t *model,
	const sg_destination_terminal_t *terminal,
	const sg_field_environment_t *environment, uint64_t now_ms,
	const sg_field_solution_t *previous, sg_field_solution_t *solution,
	size_t *reused_out)
{
	uint8_t *affected = NULL;
	sg_field_status_t status;
	size_t node;
	size_t reused = 0U;
	int changed;

	*reused_out = 0U;
	status = SolutionClassify(model, terminal, environment, now_ms, solution);
	if (status != SG_FIELD_STATUS_OK)
		return status;
	if (!SolutionProductShapeSame(model, previous, solution))
		return BuildBellmanCosts(model, solution, NULL);
	affected = calloc(solution->node_count, sizeof(*affected));
	if (!affected)
		return SG_FIELD_STATUS_STORAGE;
	memcpy(solution->costs, previous->costs,
		solution->node_count * sizeof(*solution->costs));
	for (node = 0U; node < solution->node_count; node++)
	{
		size_t product_region = node / solution->atom_count;
		size_t region = product_region % solution->region_count;
		size_t kernel;
		if (solution->terminal_atoms[node] != previous->terminal_atoms[node] ||
		    solution->reachable[node] != previous->reachable[node] ||
		    solution->rank[node] != previous->rank[node] ||
		    !NodeRulesSame(previous, solution, (uint32_t)node))
			affected[node] = 1U;
		for (kernel = 0U; !affected[node] &&
		     kernel < model->local_progress_kernel_count; kernel++)
			if (solution->accepted_kernels[region *
				model->local_progress_kernel_count + kernel] !=
			    previous->accepted_kernels[region *
				model->local_progress_kernel_count + kernel])
				affected[node] = 1U;
	}
	do
	{
		changed = 0;
		MarkRulePredecessors(previous, affected, &changed);
		MarkRulePredecessors(solution, affected, &changed);
	} while (changed);
	for (node = 0U; node < solution->node_count; node++)
		if (!affected[node])
			reused++;
	if (reused == 0U)
		status = BuildBellmanCosts(model, solution, NULL);
	else
		status = BuildBellmanCosts(model, solution, affected);
	free(affected);
	if (status == SG_FIELD_STATUS_OK)
		*reused_out = reused;
	return status;
}

static void CacheEntryDestroy(sg_field_cache_entry_t *entry)
{
	if (!entry)
		return;
	SolutionDestroy(&entry->solution);
	EnvironmentCopyDestroy(&entry->environment);
	TerminalCopyDestroy(&entry->terminal);
	free(entry);
}

static int CacheEntryLeased(const sg_field_service_t *service,
	const sg_field_cache_entry_t *entry)
{
	const sg_field_lease_t *lease;

	for (lease = service->leases; lease; lease = lease->next)
		if (lease->entry == entry)
			return 1;
	return 0;
}

static void RemoveUnleasedCacheEntry(sg_field_service_t *service,
	sg_field_cache_entry_t *entry)
{
	sg_field_cache_entry_t **link;

	if (!service || !entry || CacheEntryLeased(service, entry))
		return;
	link = &service->cache;
	while (*link && *link != entry)
		link = &(*link)->next;
	if (*link == entry)
	{
		*link = entry->next;
		CacheEntryDestroy(entry);
	}
}

static sg_field_status_t CacheEntryCreate(const sg_rune_dynamics_model_t *model,
	const sg_destination_terminal_t *terminal,
	const sg_field_environment_t *environment, uint64_t now_ms,
	const sg_field_cache_entry_t *incremental_predecessor,
	sg_field_cache_entry_t **entry_out)
{
	sg_field_cache_entry_t *entry;
	sg_field_status_t status;

	*entry_out = NULL;
	entry = calloc(1U, sizeof(*entry));
	if (!entry)
		return SG_FIELD_STATUS_STORAGE;
	if (!TerminalCopyCreate(terminal, &entry->terminal) ||
	    !EnvironmentCopyCreate(environment, &entry->environment))
	{
		CacheEntryDestroy(entry);
		return SG_FIELD_STATUS_STORAGE;
	}
	if (incremental_predecessor)
		status = SolutionBuildIncremental(model, &entry->terminal.value,
			&entry->environment.value, now_ms,
			&incremental_predecessor->solution, &entry->solution,
			&entry->reused_node_count);
	else
		status = SolutionBuild(model, &entry->terminal.value,
			&entry->environment.value, now_ms, &entry->solution);
	if (status != SG_FIELD_STATUS_OK)
	{
		CacheEntryDestroy(entry);
		return status;
	}
	entry->solved_at_ms = now_ms;
	entry->incrementally_reused = entry->reused_node_count != 0U;
	*entry_out = entry;
	return SG_FIELD_STATUS_OK;
}

static sg_field_cache_entry_t *FindCacheEntry(const sg_field_service_t *service,
	const sg_destination_terminal_t *terminal,
	const sg_field_environment_t *environment, uint64_t now_ms)
{
	sg_field_cache_entry_t *entry;

	for (entry = service->cache; entry; entry = entry->next)
		if (entry->solved_at_ms == now_ms &&
		    TerminalSame(&entry->terminal.value, terminal) &&
		    EnvironmentSame(&entry->environment.value, environment))
			return entry;
	return NULL;
}

static sg_field_lease_t *FindLease(const sg_field_service_t *service,
	const sg_field_handle_t *handle, sg_field_lease_t ***link_out)
{
	sg_field_lease_t **link;

	if (!service || !handle)
		return NULL;
	link = (sg_field_lease_t **)(void *)&service->leases;
	while (*link)
	{
		if ((*link)->handle.field_generation == handle->field_generation)
		{
			if (link_out)
				*link_out = link;
			return *link;
		}
		link = &(*link)->next;
	}
	return NULL;
}

static sg_field_status_t ServiceIdentityStatus(
	const sg_field_service_t *service, const sg_field_handle_t *handle)
{
	if (!service || service->magic != FIELD_SERVICE_MAGIC ||
	    !SG_FieldHandleValid(handle))
		return SG_FIELD_STATUS_INVALID_ARGUMENT;
	if (handle->service_identity != service->identity ||
	    handle->rune_identity != service->storage.model.rune_identity ||
	    handle->topology_revision != service->storage.model.topology_revision)
		return SG_FIELD_STATUS_IDENTITY_MISMATCH;
	if (handle->service_generation != service->generation)
		return SG_FIELD_STATUS_STALE;
	return SG_FIELD_STATUS_OK;
}

static sg_field_status_t MintLease(sg_field_service_t *service,
	sg_field_cache_entry_t *entry, sg_field_lease_t **lease_out)
{
	sg_field_lease_t *lease;
	uint64_t field_generation = service->next_field_generation;

	*lease_out = NULL;
	if (field_generation == 0U || field_generation == UINT64_MAX)
		return SG_FIELD_STATUS_CAPACITY;
	lease = calloc(1U, sizeof(*lease));
	if (!lease)
		return SG_FIELD_STATUS_STORAGE;
	lease->handle.service_identity = service->identity;
	lease->handle.service_generation = service->generation;
	lease->handle.rune_identity = service->storage.model.rune_identity;
	lease->handle.topology_revision = service->storage.model.topology_revision;
	lease->handle.terminal_generation = entry->terminal.value.generation;
	lease->handle.field_generation = field_generation;
	lease->entry = entry;
	*lease_out = lease;
	return SG_FIELD_STATUS_OK;
}

static int RequestValidForService(const sg_field_service_t *service,
	const sg_destination_terminal_t *terminal,
	const sg_field_environment_t *environment, uint64_t now_ms,
	sg_field_status_t *status_out)
{
	const sg_rune_dynamics_model_t *model = &service->storage.model;

	if (!SG_DestinationTerminalValid(terminal) ||
	    !SG_FieldEnvironmentValid(environment) || now_ms == 0U)
	{
		*status_out = SG_FIELD_STATUS_INVALID_ARGUMENT;
		return 0;
	}
	if (environment->rune_identity != model->rune_identity ||
	    environment->topology_revision != model->topology_revision ||
	    terminal->destination.kind >= SG_DESTINATION_KIND_COUNT)
	{
		*status_out = SG_FIELD_STATUS_IDENTITY_MISMATCH;
		return 0;
	}
	if (now_ms < environment->sampled_at_ms)
	{
		*status_out = SG_FIELD_STATUS_STALE;
		return 0;
	}
	return 1;
}

sg_field_status_t SG_FieldServiceCreate(
	const sg_field_model_publication_t *publication,
	sg_field_service_t **service_out)
{
	sg_field_service_t *service;
	uint64_t identity;
	uint64_t generation;

	if (service_out)
		*service_out = NULL;
	if (!service_out || !publication ||
	    publication->magic != FIELD_PUBLICATION_MAGIC ||
	    publication->publication_generation == 0U)
		return SG_FIELD_STATUS_INVALID_ARGUMENT;
	service = calloc(1U, sizeof(*service));
	if (!service)
		return SG_FIELD_STATUS_STORAGE;
	if (!ModelStorageCopy(&publication->storage.model, &service->storage))
	{
		free(service);
		return SG_FIELD_STATUS_STORAGE;
	}
	if (!NextPrivateIdentity(&identity) || !NextPrivateIdentity(&generation))
	{
		ModelStorageDestroy(&service->storage);
		free(service);
		return SG_FIELD_STATUS_CAPACITY;
	}
	service->magic = FIELD_SERVICE_MAGIC;
	service->identity = identity;
	service->generation = generation;
	service->next_field_generation = 1U;
	*service_out = service;
	return SG_FIELD_STATUS_OK;
}

void SG_FieldServiceDestroy(sg_field_service_t *service)
{
	sg_field_cache_entry_t *entry;
	sg_field_lease_t *lease;

	if (!service)
		return;
	if (service->magic == FIELD_SERVICE_MAGIC)
	{
		service->magic = 0U;
		while ((lease = service->leases) != NULL)
		{
			service->leases = lease->next;
			free(lease);
		}
		while ((entry = service->cache) != NULL)
		{
			service->cache = entry->next;
			CacheEntryDestroy(entry);
		}
		ModelStorageDestroy(&service->storage);
	}
	free(service);
}

sg_field_status_t SG_FieldServiceResolve(sg_field_service_t *service,
	const sg_destination_terminal_t *terminal,
	const sg_field_environment_t *environment, uint64_t now_ms,
	sg_field_handle_t *handle_out)
{
	sg_field_cache_entry_t *entry;
	sg_field_cache_entry_t *new_entry = NULL;
	sg_field_lease_t *lease;
	sg_field_status_t status = SG_FIELD_STATUS_OK;

	if (!service || service->magic != FIELD_SERVICE_MAGIC || !handle_out ||
	    !terminal || !environment)
		return SG_FIELD_STATUS_INVALID_ARGUMENT;
	if (!RequestValidForService(service, terminal, environment, now_ms,
		&status))
		return status;
	entry = FindCacheEntry(service, terminal, environment, now_ms);
	if (!entry)
	{
		status = CacheEntryCreate(&service->storage.model, terminal,
			environment, now_ms, NULL, &new_entry);
		if (status != SG_FIELD_STATUS_OK)
			return status;
		entry = new_entry;
	}
	status = MintLease(service, entry, &lease);
	if (status != SG_FIELD_STATUS_OK)
	{
		CacheEntryDestroy(new_entry);
		return status;
	}
	if (new_entry)
	{
		new_entry->next = service->cache;
		service->cache = new_entry;
		if (service->clean_solves != UINT64_MAX)
			service->clean_solves++;
	}
	lease->next = service->leases;
	service->leases = lease;
	service->next_field_generation++;
	*handle_out = lease->handle;
	return SG_FIELD_STATUS_OK;
}

static int TerminalSameSemanticTarget(const sg_destination_terminal_t *left,
	const sg_destination_terminal_t *right)
{
	return left->owner_identity == right->owner_identity &&
		DestinationRefSame(&left->destination, &right->destination) &&
		left->kind == right->kind &&
		(left->kind != SG_DESTINATION_TERMINAL_MOVING_TUBE ||
		 left->value.moving_tube.trajectory_identity ==
			right->value.moving_tube.trajectory_identity);
}

sg_field_status_t SG_FieldServiceRefresh(sg_field_service_t *service,
	const sg_field_handle_t *previous,
	const sg_destination_terminal_t *terminal,
	const sg_field_environment_t *environment, uint64_t now_ms,
	sg_field_handle_t *handle_out)
{
	sg_field_lease_t **previous_link = NULL;
	sg_field_lease_t *previous_lease;
	sg_field_cache_entry_t *entry;
	sg_field_cache_entry_t *new_entry = NULL;
	sg_field_lease_t *new_lease;
	sg_field_status_t status;

	if (!service || !previous || !terminal || !environment || !handle_out)
		return SG_FIELD_STATUS_INVALID_ARGUMENT;
	status = ServiceIdentityStatus(service, previous);
	if (status != SG_FIELD_STATUS_OK)
		return status;
	previous_lease = FindLease(service, previous, &previous_link);
	if (!previous_lease || !previous_link ||
	    memcmp(&previous_lease->handle, previous, sizeof(*previous)) != 0)
		return SG_FIELD_STATUS_STALE;
	if (!TerminalSameSemanticTarget(&previous_lease->entry->terminal.value,
		terminal))
		return SG_FIELD_STATUS_IDENTITY_MISMATCH;
	if (terminal->generation <= previous->terminal_generation)
		return SG_FIELD_STATUS_STALE;
	if (!RequestValidForService(service, terminal, environment, now_ms,
		&status))
		return status;
	entry = FindCacheEntry(service, terminal, environment, now_ms);
	if (!entry)
	{
		status = CacheEntryCreate(&service->storage.model, terminal,
			environment, now_ms, previous_lease->entry, &new_entry);
		if (status != SG_FIELD_STATUS_OK)
			return status;
		entry = new_entry;
	}
	status = MintLease(service, entry, &new_lease);
	if (status != SG_FIELD_STATUS_OK)
	{
		CacheEntryDestroy(new_entry);
		return status;
	}
	if (new_entry)
	{
		new_entry->next = service->cache;
		service->cache = new_entry;
		if (new_entry->incrementally_reused)
		{
			if (service->incremental_reuses != UINT64_MAX)
				service->incremental_reuses++;
			if (service->incremental_reused_nodes > UINT64_MAX -
			    (uint64_t)new_entry->reused_node_count)
				service->incremental_reused_nodes = UINT64_MAX;
			else
				service->incremental_reused_nodes +=
					(uint64_t)new_entry->reused_node_count;
		}
		else if (service->clean_solves != UINT64_MAX)
			service->clean_solves++;
	}
	*previous_link = previous_lease->next;
	new_lease->next = service->leases;
	service->leases = new_lease;
	service->next_field_generation++;
	entry = previous_lease->entry;
	free(previous_lease);
	RemoveUnleasedCacheEntry(service, entry);
	*handle_out = new_lease->handle;
	return SG_FIELD_STATUS_OK;
}

static int ModeSame(const sg_rune_state_mode_t *left,
	const sg_rune_state_mode_t *right)
{
	if (left->kind != right->kind)
		return 0;
	switch (left->kind)
	{
	case SG_RUNE_STATE_MODE_SUPPORTED:
		return StableSame(&left->value.supported.support_surface.value,
			&right->value.supported.support_surface.value);
	case SG_RUNE_STATE_MODE_WATER:
		return left->value.water.medium == right->value.water.medium &&
			left->value.water.contents == right->value.water.contents;
	case SG_RUNE_STATE_MODE_AIRBORNE:
		return left->value.airborne.void_relation ==
			right->value.airborne.void_relation;
	case SG_RUNE_STATE_MODE_HOOK_BOLT:
		return StableSame(&left->value.hook_bolt.visibility_relation.value,
			&right->value.hook_bolt.visibility_relation.value);
	case SG_RUNE_STATE_MODE_HOOK_PULL:
		return StableSame(&left->value.hook_pull.anchor_surface.value,
			&right->value.hook_pull.anchor_surface.value);
	case SG_RUNE_STATE_MODE_HOOK_COAST:
		return left->value.hook_coast.void_relation ==
			right->value.hook_coast.void_relation;
	case SG_RUNE_STATE_MODE_MOVER_RELATIVE:
		return StableSame(&left->value.mover_relative.mover.value,
			&right->value.mover_relative.mover.value);
	case SG_RUNE_STATE_MODE_KIND_COUNT:
	default:
		return 0;
	}
}

static int FindStateAtom(const sg_rune_dynamics_model_t *model,
	const sg_localized_field_state_t *state, uint32_t *atom_out,
	const sg_field_refinement_node_t **leaf_out)
{
	size_t chart_index;
	size_t leaf_index;
	sg_rune_state_simplex_id_t simplex;
	sg_field_reach_atom_id_t atom;
	sg_field_refinement_node_id_t leaf;

	for (chart_index = 0U; chart_index < model->state_chart_count; chart_index++)
		if (StableSame(&model->state_charts[chart_index].id.value,
			&state->chart.value))
			break;
	if (chart_index == model->state_chart_count ||
	    !ModeSame(&model->state_charts[chart_index].mode, &state->mode))
		return 0;
	if (!SG_RuneDynamicsLocatePointExact(model, &state->chart,
		&state->position, &state->velocity, state->elapsed_ms,
		&simplex, &atom, &leaf) || !FindAtomIndex(model, &atom, atom_out))
		return 0;
	(void)simplex;
	for (leaf_index = 0U; leaf_index < model->refinement_tree.node_count;
	     leaf_index++)
		if (StableSame(&model->refinement_tree.nodes[leaf_index].id.value,
			&leaf.value))
			break;
	if (leaf_index == model->refinement_tree.node_count)
		return 0;
	*leaf_out = &model->refinement_tree.nodes[leaf_index];
	return 1;
}

#ifdef SG_FIELD_SERVICE_TESTING
sg_field_status_t SG_FieldServiceTestStoredCost(
	const sg_field_service_t *service, const sg_field_handle_t *handle,
	const sg_localized_field_state_t *state, sg_rune_cost_bounds_t *cost_out)
{
	const sg_field_refinement_node_t *leaf;
	sg_field_lease_t *lease;
	uint32_t atom;
	uint32_t node;
	size_t region;

	if (!service || !handle || !state || !cost_out ||
	    service->magic != FIELD_SERVICE_MAGIC)
		return SG_FIELD_STATUS_INVALID_ARGUMENT;
	lease = FindLease(service, handle, NULL);
	if (!lease)
		return SG_FIELD_STATUS_STALE;
	if (!FindStateAtom(&service->storage.model, state, &atom, &leaf))
		return SG_FIELD_STATUS_MODEL_INCOMPLETE;
	(void)leaf;
	region = TimeRegionIndex(&lease->entry->solution, state->sampled_at_ms);
	if (region == SIZE_MAX || !NodeIndex(&lease->entry->solution, 0U,
		region, atom, &node))
		return SG_FIELD_STATUS_STALE;
	*cost_out = lease->entry->solution.costs[node];
	return SG_FIELD_STATUS_OK;
}

int SG_FieldServiceTestLocateState(const sg_field_service_t *service,
	const sg_localized_field_state_t *state,
	sg_field_reach_atom_id_t *atom_out,
	sg_field_refinement_node_id_t *leaf_out)
{
	const sg_field_refinement_node_t *leaf;
	uint32_t atom;

	if (!service || !state || !atom_out || !leaf_out ||
	    service->magic != FIELD_SERVICE_MAGIC ||
	    !FindStateAtom(&service->storage.model, state, &atom, &leaf))
		return 0;
	*atom_out = service->storage.model.reach_atoms[atom].id;
	*leaf_out = leaf->id;
	return 1;
}
#endif

static int StateInsideCapture(const sg_localized_field_state_t *state,
	const sg_destination_terminal_capture_t *capture)
{
	size_t axis;
	const sg_destination_interval_t *position[3] = {
		&capture->position_offset.x, &capture->position_offset.y,
		&capture->position_offset.z
	};
	const sg_destination_interval_t *velocity[3] = {
		&capture->velocity.x, &capture->velocity.y, &capture->velocity.z
	};

	for (axis = 0U; axis < 3U; axis++)
	{
		float offset = state->position.value[axis] -
			capture->anchor.position[axis];
		if (offset < position[axis]->min_value ||
		    offset > position[axis]->max_value ||
		    state->velocity.value[axis] < velocity[axis]->min_value ||
		    state->velocity.value[axis] > velocity[axis]->max_value)
			return 0;
	}
	return state->elapsed_ms >= capture->local_elapsed_ms.min_value &&
		state->elapsed_ms <= capture->local_elapsed_ms.max_value;
}

static int ChoiceReferencedByKernel(const sg_rune_dynamics_model_t *model,
	const sg_field_local_progress_kernel_t *kernel,
	const sg_field_choice_t *choice)
{
	size_t index;

	for (index = kernel->admissible_choices.first;
	     index < (size_t)kernel->admissible_choices.first +
		kernel->admissible_choices.count; index++)
		if (StableSame(&model->local_progress_choices[index].value,
			&choice->id.value))
			return 1;
	return 0;
}

static int QueryRuleEligible(const sg_rune_dynamics_model_t *model,
	const sg_field_cache_entry_t *entry,
	const sg_field_refinement_node_t *leaf, size_t region, uint32_t node,
	uint64_t query_time_ms, size_t rule_index,
	sg_rune_cost_bounds_t *endpoint_out, uint64_t *minimum_out)
{
	const sg_field_solution_t *solution = &entry->solution;
	const sg_field_bellman_rule_t *rule = &solution->rules[rule_index];
	const sg_field_choice_t *choice;
	const sg_field_local_progress_kernel_t *kernel = NULL;
	sg_rune_cost_bounds_t candidate;
	long double decrease;

	if (rule->source_node != node)
		return 0;
	choice = &model->choices[rule->choice_index];
	if (rule->kernel_index != FIELD_NO_KERNEL)
	{
		if (rule->kernel_index >= model->local_progress_kernel_count ||
		    !solution->accepted_kernels[region *
			model->local_progress_kernel_count + rule->kernel_index])
			return 0;
		kernel = &model->local_progress_kernels[rule->kernel_index];
		if (!KernelCoversLeaf(model, kernel, leaf) ||
		    !ChoiceReferencedByKernel(model, kernel, choice))
			return 0;
	}
	if (!ChoiceEnabled(model, choice, &entry->environment.value, query_time_ms) ||
	    !RuleIntervalCost(model, solution, rule, &candidate, endpoint_out) ||
	    candidate.upper_us != solution->costs[node].upper_us)
		return 0;
	if (solution->costs[node].lower_us > endpoint_out->upper_us)
		*minimum_out = solution->costs[node].lower_us - endpoint_out->upper_us;
	else
	{
		if (!kernel)
			return 0;
		decrease = ceill((long double)kernel->minimum_lyapunov_decrease *
			(long double)model->error_contract.cost_quantum_us);
		if (!isfinite(decrease) || decrease <= 0.0L ||
		    decrease >= (long double)UINT64_MAX)
			return 0;
		*minimum_out = (uint64_t)decrease;
	}
	return 1;
}

static int EarlierEligibleRuleForChoice(const sg_rune_dynamics_model_t *model,
	const sg_field_cache_entry_t *entry,
	const sg_field_refinement_node_t *leaf, size_t region, uint32_t node,
	uint64_t query_time_ms, size_t rule_index)
{
	size_t earlier;

	for (earlier = 0U; earlier < rule_index; earlier++)
	{
		sg_rune_cost_bounds_t endpoint;
		uint64_t minimum;
		if (entry->solution.rules[earlier].choice_index ==
			entry->solution.rules[rule_index].choice_index &&
		    QueryRuleEligible(model, entry, leaf, region, node,
			query_time_ms, earlier, &endpoint, &minimum))
			return 1;
	}
	return 0;
}

static size_t CountQueryOptions(const sg_rune_dynamics_model_t *model,
	const sg_field_cache_entry_t *entry,
	const sg_field_refinement_node_t *leaf, size_t region, uint32_t node,
	uint64_t query_time_ms)
{
	size_t rule_index;
	size_t count = 0U;

	for (rule_index = 0U; rule_index < entry->solution.rule_count; rule_index++)
	{
		sg_rune_cost_bounds_t endpoint;
		uint64_t minimum;
		if (QueryRuleEligible(model, entry, leaf, region, node,
			query_time_ms, rule_index, &endpoint, &minimum) &&
		    !EarlierEligibleRuleForChoice(model, entry, leaf, region, node,
			query_time_ms, rule_index))
			count++;
	}
	return count;
}

static size_t WriteQueryOptions(const sg_rune_dynamics_model_t *model,
	const sg_field_cache_entry_t *entry,
	const sg_field_refinement_node_t *leaf, size_t region, uint32_t node,
	uint64_t query_time_ms, sg_field_option_t *storage)
{
	size_t rule_index;
	size_t count = 0U;

	for (rule_index = 0U; rule_index < entry->solution.rule_count; rule_index++)
	{
		const sg_field_bellman_rule_t *rule =
			&entry->solution.rules[rule_index];
		const sg_field_choice_t *choice = &model->choices[rule->choice_index];
		sg_rune_cost_bounds_t endpoint;
		uint64_t minimum;
		if (!QueryRuleEligible(model, entry, leaf, region, node,
			query_time_ms, rule_index, &endpoint, &minimum) ||
		    EarlierEligibleRuleForChoice(model, entry, leaf, region, node,
			query_time_ms, rule_index))
			continue;
		if (choice->kind == SG_FIELD_CHOICE_CONTROL)
		{
			storage[count].kind = SG_FIELD_OPTION_CONTROL;
			storage[count].value.control.control = choice->authority.control;
			storage[count].value.control.minimum_descent_us = minimum;
			storage[count].value.control.endpoint_cost = endpoint;
		}
		else
		{
			storage[count].kind = SG_FIELD_OPTION_TRANSFER;
			storage[count].value.transfer.transfer = choice->authority.transfer;
			storage[count].value.transfer.minimum_descent_us = minimum;
			storage[count].value.transfer.endpoint_cost = endpoint;
		}
		count++;
	}
	return count;
}

static sg_rune_interval_t PointInterval(float value)
{
	return (sg_rune_interval_t){ value, value };
}

static void GradientHull(const sg_rune_dynamics_model_t *model,
	const sg_field_cache_entry_t *entry, size_t region, uint32_t atom,
	const sg_field_refinement_node_t *leaf,
	sg_rune_interval3_t *position, sg_rune_interval3_t *velocity,
	sg_rune_interval_t *elapsed)
{
	size_t kernel_index;
	int initialized = 0;
	sg_rune_interval_t *output[7] = {
		&position->x, &position->y, &position->z,
		&velocity->x, &velocity->y, &velocity->z, elapsed
	};

	for (kernel_index = 0U; kernel_index <
	     model->local_progress_kernel_count; kernel_index++)
	{
		const sg_field_local_progress_kernel_t *kernel =
			&model->local_progress_kernels[kernel_index];
		size_t dimension;
		if (!entry->solution.accepted_kernels[region *
			model->local_progress_kernel_count + kernel_index] ||
		    !StableSame(&kernel->source_atom.value,
			&model->reach_atoms[atom].id.value) ||
		    !KernelCoversLeaf(model, kernel, leaf))
			continue;
		for (dimension = 0U; dimension < 7U; dimension++)
		{
			float value = kernel->state_lyapunov[dimension];
			if (!initialized)
				*output[dimension] = PointInterval(value);
			else
			{
				if (value < output[dimension]->min_value)
					output[dimension]->min_value = value;
				if (value > output[dimension]->max_value)
					output[dimension]->max_value = value;
			}
		}
		initialized = 1;
	}
	if (!initialized)
	{
		*position = (sg_rune_interval3_t){
			PointInterval(0.0f), PointInterval(0.0f), PointInterval(0.0f) };
		*velocity = *position;
		*elapsed = PointInterval(0.0f);
	}
}

static long double StateCoordinate(
	const sg_localized_field_state_t *state, size_t dimension)
{
	if (dimension < 3U)
		return (long double)state->position.value[dimension];
	if (dimension < 6U)
		return (long double)state->velocity.value[dimension - 3U];
	return (long double)state->elapsed_ms;
}

static long double AnchorCoordinate(
	const sg_destination_terminal_capture_t *capture, size_t dimension)
{
	if (dimension < 3U)
		return (long double)capture->anchor.position[dimension];
	if (dimension < 6U)
		return (long double)capture->anchor.velocity[dimension - 3U];
	return (long double)capture->anchor.local_elapsed_ms;
}

static int ContinuousArrivalCost(const sg_rune_dynamics_model_t *model,
	const sg_field_cache_entry_t *entry,
	const sg_localized_field_state_t *state,
	const sg_destination_terminal_capture_t *capture, size_t region,
	uint32_t node, uint32_t atom,
	const sg_field_refinement_node_t *leaf, sg_rune_cost_bounds_t *cost_out)
{
	uint64_t minimum_offset = UINT64_MAX;
	size_t kernel_index;

	for (kernel_index = 0U; kernel_index <
	     model->local_progress_kernel_count; kernel_index++)
	{
		const sg_field_local_progress_kernel_t *kernel =
			&model->local_progress_kernels[kernel_index];
		long double value = (long double)kernel->lyapunov_constant;
		long double scaled;
		uint64_t offset;
		size_t dimension;
		if (!entry->solution.accepted_kernels[region *
			model->local_progress_kernel_count + kernel_index] ||
		    !StableSame(&kernel->source_atom.value,
			&model->reach_atoms[atom].id.value) ||
		    !KernelCoversLeaf(model, kernel, leaf))
			continue;
		for (dimension = 0U; dimension < 7U; dimension++)
			value += (long double)kernel->state_lyapunov[dimension] *
				StateCoordinate(state, dimension) +
				(long double)kernel->anchor_lyapunov[dimension] *
				AnchorCoordinate(capture, dimension);
		if (!isfinite(value))
			return 0;
		if (value < 0.0L)
			value = 0.0L;
		scaled = ceill(value *
			(long double)model->error_contract.cost_quantum_us);
		if (!isfinite(scaled) || scaled < 0.0L ||
		    scaled > (long double)entry->solution.local_span_us)
			return 0;
		offset = (uint64_t)scaled;
		if (offset < minimum_offset)
			minimum_offset = offset;
	}
	if (minimum_offset == UINT64_MAX)
	{
		size_t rule_index;
		int ordinary_rule = 0;
		for (rule_index = 0U; rule_index < entry->solution.rule_count;
		     rule_index++)
		{
			sg_rune_cost_bounds_t endpoint;
			uint64_t minimum;
			if (entry->solution.rules[rule_index].kernel_index ==
				FIELD_NO_KERNEL && QueryRuleEligible(model, entry, leaf,
					region, node, state->sampled_at_ms, rule_index,
					&endpoint, &minimum))
			{
				ordinary_rule = 1;
				break;
			}
		}
		if (!ordinary_rule)
			return 0;
		minimum_offset = 0U;
	}
	if (!SafeAddU64(entry->solution.costs[node].lower_us, minimum_offset,
		&cost_out->lower_us) ||
	    !SafeAddU64(entry->solution.costs[node].upper_us, minimum_offset,
		&cost_out->upper_us) ||
	    cost_out->lower_us >= SG_RUNE_FIELD_COST_INFINITE)
		return 0;
	return cost_out->upper_us < SG_RUNE_FIELD_COST_INFINITE;
}

sg_field_status_t SG_FieldServiceQuery(const sg_field_service_t *service,
	const sg_field_handle_t *handle,
	const sg_localized_field_state_t *state,
	const sg_field_environment_t *environment,
	sg_field_option_t *option_storage, size_t option_capacity,
	sg_field_guidance_t *guidance_out)
{
	const sg_rune_dynamics_model_t *model;
	sg_field_lease_t *lease;
	const sg_destination_terminal_capture_t *capture;
	const sg_destination_terminal_domain_t *terminal_domain = NULL;
	const sg_field_refinement_node_t *leaf;
	sg_field_guidance_t guidance = { 0 };
	uint32_t atom;
	uint32_t node;
	size_t region;
	size_t required;
	sg_field_status_t status;

	if (!service || !handle || !state || !environment || !guidance_out ||
	    (option_capacity != 0U && !option_storage))
		return SG_FIELD_STATUS_INVALID_ARGUMENT;
	status = ServiceIdentityStatus(service, handle);
	if (status != SG_FIELD_STATUS_OK)
		return status;
	lease = FindLease(service, handle, NULL);
	if (!lease || memcmp(&lease->handle, handle, sizeof(*handle)) != 0)
		return SG_FIELD_STATUS_STALE;
	model = &service->storage.model;
	if (!SG_LocalizedFieldStateValid(state) ||
	    !SG_FieldEnvironmentValid(environment))
		return SG_FIELD_STATUS_INVALID_ARGUMENT;
	if (state->rune_identity != model->rune_identity ||
	    state->topology_revision != model->topology_revision ||
	    environment->rune_identity != model->rune_identity ||
	    environment->topology_revision != model->topology_revision)
		return SG_FIELD_STATUS_IDENTITY_MISMATCH;
	if (!EnvironmentSame(&lease->entry->environment.value, environment) ||
	    state->sampled_at_ms < lease->entry->solved_at_ms)
		return SG_FIELD_STATUS_STALE;
	if (!FindStateAtom(model, state, &atom, &leaf))
		return SG_FIELD_STATUS_MODEL_INCOMPLETE;
	region = TimeRegionIndex(&lease->entry->solution, state->sampled_at_ms);
	if (region == SIZE_MAX ||
	    !NodeIndex(&lease->entry->solution, 0U, region, atom, &node))
		return SG_FIELD_STATUS_STALE;
	guidance.field = *handle;
	guidance.pose_revision = state->pose_revision;
	guidance.sampled_at_ms = state->sampled_at_ms;
	capture = ActiveCapture(&lease->entry->terminal.value,
		state->sampled_at_ms, &terminal_domain);
	if (!capture || !terminal_domain)
		return SG_FIELD_STATUS_STALE;
	if (DomainMatches(model, atom, terminal_domain) &&
	    StateInsideCapture(state, capture))
	{
		guidance.kind = SG_FIELD_GUIDANCE_TERMINAL;
		guidance.value.terminal.arrival_cost =
			(sg_rune_cost_bounds_t){ 0U, 0U };
		guidance.value.terminal.residual_bound_us =
			model->error_contract.maximum_bellman_residual_us;
		*guidance_out = guidance;
		return SG_FIELD_STATUS_OK;
	}
	if (!lease->entry->solution.reachable[node])
	{
		guidance.kind = SG_FIELD_GUIDANCE_UNREACHABLE;
		guidance.value.unreachable.arrival_cost =
			(sg_rune_cost_bounds_t){ SG_RUNE_FIELD_COST_INFINITE,
				SG_RUNE_FIELD_COST_INFINITE };
		*guidance_out = guidance;
		return SG_FIELD_STATUS_UNREACHABLE;
	}
	required = CountQueryOptions(model, lease->entry, leaf, region, node,
		state->sampled_at_ms);
	if (required == 0U)
		return SG_FIELD_STATUS_MODEL_INCOMPLETE;
	if (required > option_capacity)
	{
		memset(&guidance, 0, sizeof(guidance));
		guidance.field = *handle;
		guidance.pose_revision = state->pose_revision;
		guidance.sampled_at_ms = state->sampled_at_ms;
		guidance.kind = SG_FIELD_GUIDANCE_DESCENT;
		guidance.value.descent.required_option_capacity = required;
		*guidance_out = guidance;
		return SG_FIELD_STATUS_CAPACITY;
	}
	guidance.kind = SG_FIELD_GUIDANCE_DESCENT;
	if (!ContinuousArrivalCost(model, lease->entry, state, capture, region,
		node, atom, leaf,
		&guidance.value.descent.arrival_cost))
		return SG_FIELD_STATUS_NUMERICAL_ERROR;
	guidance.value.descent.residual_bound_us =
		model->error_contract.maximum_bellman_residual_us;
	GradientHull(model, lease->entry, region, atom, leaf,
		&guidance.value.descent.spatial_subgradient,
		&guidance.value.descent.velocity_subgradient,
		&guidance.value.descent.time_subgradient);
	guidance.value.descent.position_error = model->error_contract.position_error;
	guidance.value.descent.velocity_error = model->error_contract.velocity_error;
	guidance.value.descent.time_error = model->error_contract.time_error;
	memset(option_storage, 0, required * sizeof(*option_storage));
	guidance.value.descent.option_count = WriteQueryOptions(model, lease->entry,
		leaf, region, node, state->sampled_at_ms, option_storage);
	guidance.value.descent.required_option_capacity = required;
	guidance.value.descent.options = option_storage;
	if (guidance.value.descent.option_count != required ||
	    !SG_FieldGuidanceValid(&guidance))
		return SG_FIELD_STATUS_NUMERICAL_ERROR;
	*guidance_out = guidance;
	return SG_FIELD_STATUS_OK;
}

sg_field_status_t SG_FieldServiceRelease(sg_field_service_t *service,
	const sg_field_handle_t *handle)
{
	sg_field_lease_t **link = NULL;
	sg_field_lease_t *lease;
	sg_field_status_t status = ServiceIdentityStatus(service, handle);

	if (status != SG_FIELD_STATUS_OK)
		return status;
	lease = FindLease(service, handle, &link);
	if (!lease || !link || memcmp(&lease->handle, handle, sizeof(*handle)) != 0)
		return SG_FIELD_STATUS_STALE;
	*link = lease->next;
	{
		sg_field_cache_entry_t *entry = lease->entry;
		free(lease);
		RemoveUnleasedCacheEntry(service, entry);
	}
	return SG_FIELD_STATUS_OK;
}
