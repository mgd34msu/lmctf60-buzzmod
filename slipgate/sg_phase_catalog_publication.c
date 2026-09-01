#include "sg_phase_catalog_internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct sg_phase_catalog_publication_payload_s
{
	sg_rune_model_identity_t identity;
	sg_phase_catalog_completion_t completion;
	sg_phase_catalog_completion_t transition_completion;
	uint64_t mover_support_verifier_identity;
	sg_rune_phase_basis_t *phases;
	uint32_t phase_count;
	sg_phase_catalog_binding_t *bindings;
	uint32_t binding_count;
	sg_rune_phase_transition_t *transitions;
	sg_phase_catalog_transition_evidence_t *transition_evidence;
	uint32_t transition_count;
} sg_phase_catalog_publication_payload_t;

typedef struct sg_phase_catalog_publication_record_s
{
	sg_phase_catalog_publication_t *token;
	sg_phase_catalog_publication_payload_t *payload;
	sg_phase_catalog_view_t view;
	sg_rune_phase_basis_t *view_phases;
	sg_phase_catalog_binding_t *view_bindings;
	sg_rune_phase_transition_t *view_transitions;
	sg_phase_catalog_transition_evidence_t *view_transition_evidence;
	struct sg_phase_catalog_publication_record_s *next;
} sg_phase_catalog_publication_record_t;

struct sg_phase_catalog_publication_owner_s
{
	sg_phase_catalog_publication_record_t *live;
	uint32_t live_count;
};

static int AllocationFits(size_t count, size_t element_size)
{
	return element_size != 0U && count <= SIZE_MAX / element_size;
}

static int RangeEnd(const void *address, size_t size, uintptr_t *end_out)
{
	uintptr_t start;

	if (!address || size == 0U || !end_out)
		return 0;
	start = (uintptr_t)address;
	if (size > UINTPTR_MAX - start)
		return 0;
	*end_out = start + (uintptr_t)size;
	return *end_out > start;
}

static int RangesOverlap(const void *left_address, size_t left_size,
	const void *right_address, size_t right_size)
{
	uintptr_t left_end;
	uintptr_t right_end;
	uintptr_t left_start;
	uintptr_t right_start;

	if (!RangeEnd(left_address, left_size, &left_end) ||
		!RangeEnd(right_address, right_size, &right_end))
		return 0;
	left_start = (uintptr_t)left_address;
	right_start = (uintptr_t)right_address;
	return left_start < right_end && right_start < left_end;
}

static sg_phase_catalog_publication_record_t *PublicationRecord(
	const sg_phase_catalog_publication_owner_t *owner,
	const sg_phase_catalog_publication_t *publication)
{
	sg_phase_catalog_publication_record_t *record;

	if (!owner || !publication)
		return NULL;
	for (record = owner->live; record; record = record->next)
		if (record->token == publication)
			return record;
	return NULL;
}

static int PayloadShapeValid(
	const sg_phase_catalog_publication_payload_t *payload)
{
	return payload && payload->phase_count <= SG_RUNE_MODEL_MAX_PHASES &&
		payload->binding_count <= SG_PHASE_CATALOG_MAX_BINDINGS &&
		payload->transition_count <= SG_RUNE_MODEL_MAX_PHASE_TRANSITIONS &&
		AllocationFits((size_t)payload->phase_count, sizeof(*payload->phases)) &&
		AllocationFits((size_t)payload->binding_count,
			sizeof(*payload->bindings)) &&
		AllocationFits((size_t)payload->transition_count,
			sizeof(*payload->transitions)) &&
		AllocationFits((size_t)payload->transition_count,
			sizeof(*payload->transition_evidence)) &&
		(payload->phase_count == 0U ? !payload->phases :
			payload->phases != NULL) &&
		(payload->binding_count == 0U ? !payload->bindings :
			payload->bindings != NULL) &&
		(payload->transition_count == 0U ?
			!payload->transitions && !payload->transition_evidence :
			payload->transitions && payload->transition_evidence);
}

static void ReleaseRecord(sg_phase_catalog_publication_record_t *record)
{
	if (!record)
		return;
	if (record->payload)
	{
		free(record->payload->phases);
		free(record->payload->bindings);
		free(record->payload->transitions);
		free(record->payload->transition_evidence);
	}
	free(record->view_phases);
	free(record->view_bindings);
	free(record->view_transitions);
	free(record->view_transition_evidence);
	free(record->payload);
	free(record);
}

static int AllocateRecordStorage(sg_phase_catalog_publication_record_t *record)
{
	const sg_phase_catalog_publication_payload_t *payload = record->payload;

	if (payload->phase_count != 0U)
	{
		record->view_phases = malloc((size_t)payload->phase_count *
			sizeof(*record->view_phases));
		if (!record->view_phases)
			return 0;
	}
	if (payload->binding_count != 0U)
	{
		record->view_bindings = malloc((size_t)payload->binding_count *
			sizeof(*record->view_bindings));
		if (!record->view_bindings)
			return 0;
	}
	if (payload->transition_count != 0U)
	{
		record->view_transitions = malloc((size_t)payload->transition_count *
			sizeof(*record->view_transitions));
		record->view_transition_evidence = malloc(
			(size_t)payload->transition_count *
			sizeof(*record->view_transition_evidence));
		if (!record->view_transitions || !record->view_transition_evidence)
			return 0;
	}
	return 1;
}

static void RefreshView(sg_phase_catalog_publication_record_t *record)
{
	const sg_phase_catalog_publication_payload_t *payload = record->payload;

	if (payload->phase_count != 0U)
		memcpy(record->view_phases, payload->phases,
			(size_t)payload->phase_count * sizeof(*payload->phases));
	if (payload->binding_count != 0U)
		memcpy(record->view_bindings, payload->bindings,
			(size_t)payload->binding_count * sizeof(*payload->bindings));
	if (payload->transition_count != 0U)
	{
		memcpy(record->view_transitions, payload->transitions,
			(size_t)payload->transition_count * sizeof(*payload->transitions));
		memcpy(record->view_transition_evidence,
			payload->transition_evidence,
			(size_t)payload->transition_count *
				sizeof(*payload->transition_evidence));
	}
	memset(&record->view, 0, sizeof(record->view));
	record->view.identity = payload->identity;
	record->view.completion = payload->completion;
	record->view.transition_completion = payload->transition_completion;
	record->view.mover_support_verifier_identity =
		payload->mover_support_verifier_identity;
	record->view.phases = record->view_phases;
	record->view.phase_count = payload->phase_count;
	record->view.bindings = record->view_bindings;
	record->view.binding_count = payload->binding_count;
	record->view.transitions = record->view_transitions;
	record->view.transition_evidence = record->view_transition_evidence;
	record->view.transition_count = payload->transition_count;
}

int SG_PhaseCatalogPublicationOwnerCreate(
	sg_phase_catalog_publication_owner_t **owner_out)
{
	if (!owner_out || *owner_out)
		return 0;
	*owner_out = calloc(1U, sizeof(**owner_out));
	return *owner_out != NULL;
}

int SG_PhaseCatalogPublicationIssue(
	sg_phase_catalog_publication_owner_t *owner,
	const sg_phase_catalog_source_t *source, const sg_phase_catalog_t *catalog,
	sg_phase_catalog_publication_t **publication_out,
	sg_phase_catalog_check_result_t *check_out)
{
	sg_phase_catalog_check_result_t check;
	sg_phase_catalog_publication_record_t *record = NULL;
	sg_phase_catalog_publication_payload_t *payload = NULL;
	uintptr_t token;

	if (check_out)
		memset(check_out, 0, sizeof(*check_out));
	if (!owner || !publication_out || *publication_out ||
		owner->live_count == UINT32_MAX)
	{
		if (check_out)
			check_out->code = SG_PHASE_CATALOG_CHECK_INVALID_ARGUMENT;
		return 0;
	}
	*publication_out = NULL;
	memset(&check, 0, sizeof(check));
	if (!SG_PhaseCatalogValidate(source, catalog, &check))
	{
		if (check_out)
			*check_out = check;
		return 0;
	}
	record = calloc(1U, sizeof(*record));
	payload = calloc(1U, sizeof(*payload));
	if (!record || !payload)
		goto allocation_failure;
	record->payload = payload;
	payload->identity = catalog->identity;
	payload->completion = catalog->completion;
	payload->transition_completion = catalog->transition_completion;
	payload->mover_support_verifier_identity =
		catalog->mover_support_verifier_identity;
	payload->phase_count = catalog->phase_count;
	payload->binding_count = catalog->binding_count;
	payload->transition_count = catalog->transition_count;
	if (catalog->phase_count != 0U)
	{
		payload->phases = malloc((size_t)catalog->phase_count *
			sizeof(*payload->phases));
		if (!payload->phases)
			goto allocation_failure;
		memcpy(payload->phases, catalog->phases,
			(size_t)catalog->phase_count * sizeof(*catalog->phases));
	}
	if (catalog->binding_count != 0U)
	{
		payload->bindings = malloc((size_t)catalog->binding_count *
			sizeof(*payload->bindings));
		if (!payload->bindings)
			goto allocation_failure;
		memcpy(payload->bindings, catalog->bindings,
			(size_t)catalog->binding_count * sizeof(*catalog->bindings));
	}
	if (catalog->transition_count != 0U)
	{
		payload->transitions = malloc((size_t)catalog->transition_count *
			sizeof(*payload->transitions));
		payload->transition_evidence = malloc((size_t)catalog->transition_count *
			sizeof(*payload->transition_evidence));
		if (!payload->transitions || !payload->transition_evidence)
			goto allocation_failure;
		memcpy(payload->transitions, catalog->transitions,
			(size_t)catalog->transition_count * sizeof(*catalog->transitions));
		memcpy(payload->transition_evidence, catalog->transition_evidence,
			(size_t)catalog->transition_count *
				sizeof(*catalog->transition_evidence));
	}
	if (!PayloadShapeValid(payload) || !AllocateRecordStorage(record) ||
		!SG_AuthorityTokenMint(&token))
		goto allocation_failure;
	record->token = (sg_phase_catalog_publication_t *)(uintptr_t)token;
	RefreshView(record);
	record->next = owner->live;
	owner->live = record;
	owner->live_count++;
	if (check_out)
		*check_out = check;
	*publication_out = record->token;
	return 1;

allocation_failure:
	if (record)
		ReleaseRecord(record);
	else
		free(payload);
	if (check_out)
	{
		check_out->code = SG_PHASE_CATALOG_CHECK_STORAGE_INVALID;
		check_out->record = 0U;
	}
	return 0;
}

int SG_PhaseCatalogPublicationRead(
	const sg_phase_catalog_publication_owner_t *owner,
	const sg_phase_catalog_publication_t *publication,
	const sg_phase_catalog_view_t **view_out)
{
	sg_phase_catalog_publication_record_t *record;

	if (view_out)
		*view_out = NULL;
	record = PublicationRecord(owner, publication);
	if (!view_out || !record || !PayloadShapeValid(record->payload))
		return 0;
	RefreshView(record);
	*view_out = &record->view;
	return 1;
}

int SG_PhaseCatalogPublicationStorageOverlaps(
	const sg_phase_catalog_publication_owner_t *owner,
	const sg_phase_catalog_publication_t *publication,
	const void *address, size_t size)
{
	sg_phase_catalog_publication_record_t *record = PublicationRecord(owner,
		publication);
	const sg_phase_catalog_publication_payload_t *payload;

	if (!record || !PayloadShapeValid(record->payload) || !address || size == 0U)
		return 0;
	payload = record->payload;
#define OVERLAPS(member, count) \
	(RangesOverlap(address, size, payload->member, \
		(size_t)payload->count * sizeof(*payload->member)) || \
	 RangesOverlap(address, size, record->view_##member, \
		(size_t)payload->count * sizeof(*record->view_##member)))
	return OVERLAPS(phases, phase_count) ||
		OVERLAPS(bindings, binding_count) ||
		OVERLAPS(transitions, transition_count) ||
		OVERLAPS(transition_evidence, transition_count);
#undef OVERLAPS
}

void SG_PhaseCatalogPublicationDestroy(
	sg_phase_catalog_publication_owner_t *owner,
	sg_phase_catalog_publication_t *publication)
{
	sg_phase_catalog_publication_record_t **link;
	sg_phase_catalog_publication_record_t *record;

	if (!owner || !publication)
		return;
	for (link = &owner->live; *link; link = &(*link)->next)
		if ((*link)->token == publication)
		{
			record = *link;
			*link = record->next;
			owner->live_count--;
			ReleaseRecord(record);
			return;
		}
}

void SG_PhaseCatalogPublicationOwnerDestroy(
	sg_phase_catalog_publication_owner_t *owner)
{
	sg_phase_catalog_publication_record_t *record;

	if (!owner)
		return;
	while (owner->live)
	{
		record = owner->live;
		owner->live = record->next;
		ReleaseRecord(record);
	}
	free(owner);
}
