#include "sg_phase_catalog_internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SG_PHASE_CATALOG_PUBLICATION_MAGIC UINT64_C(0x5043415055423031)

static int AllocationFits(size_t count, size_t element_size)
{
	return element_size != 0U && count <= SIZE_MAX / element_size;
}

struct sg_phase_catalog_publication_s
{
	uint64_t magic;
	uint64_t magic_inverse;
	const struct sg_phase_catalog_publication_s *self;
	sg_phase_catalog_view_t view;
	/* Keep writable ownership separate from the const-facing view.  The
	 * publication is an immutable snapshot after Issue returns. */
	sg_rune_phase_basis_t *phase_storage;
	sg_phase_catalog_binding_t *binding_storage;
	sg_rune_phase_transition_t *transition_storage;
	sg_phase_catalog_transition_evidence_t *transition_evidence_storage;
	uint32_t phase_capacity;
	uint32_t binding_capacity;
	uint32_t transition_capacity;
};

static int PublicationHeaderValid(
	const sg_phase_catalog_publication_t *publication)
{
	return publication && publication->magic ==
		SG_PHASE_CATALOG_PUBLICATION_MAGIC &&
		publication->magic_inverse == ~SG_PHASE_CATALOG_PUBLICATION_MAGIC &&
		publication->self == publication;
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

static int PublicationStorageShapeValid(
	const sg_phase_catalog_publication_t *publication)
{
	const sg_phase_catalog_view_t *view = &publication->view;

	return publication->phase_capacity <= SG_RUNE_MODEL_MAX_PHASES &&
		publication->binding_capacity <= SG_PHASE_CATALOG_MAX_BINDINGS &&
		publication->transition_capacity <= SG_RUNE_MODEL_MAX_PHASE_TRANSITIONS &&
		AllocationFits((size_t)publication->phase_capacity,
			sizeof(*publication->phase_storage)) &&
		AllocationFits((size_t)publication->binding_capacity,
			sizeof(*publication->binding_storage)) &&
		AllocationFits((size_t)publication->transition_capacity,
			sizeof(*publication->transition_storage)) &&
		AllocationFits((size_t)publication->transition_capacity,
			sizeof(*publication->transition_evidence_storage)) &&
		view->phase_count <= publication->phase_capacity &&
		view->binding_count <= publication->binding_capacity &&
		view->transition_count <= publication->transition_capacity &&
		(view->phase_count == 0U ? !view->phases && !publication->phase_storage :
			view->phases && publication->phase_storage &&
			view->phases == publication->phase_storage) &&
		(view->binding_count == 0U ?
			!view->bindings && !publication->binding_storage :
			view->bindings && publication->binding_storage &&
			view->bindings == publication->binding_storage) &&
		(view->transition_count == 0U ?
			!view->transitions && !view->transition_evidence &&
			!publication->transition_storage &&
			!publication->transition_evidence_storage :
			view->transitions && publication->transition_storage &&
			view->transition_evidence &&
			publication->transition_evidence_storage &&
			view->transitions == publication->transition_storage &&
			view->transition_evidence == publication->transition_evidence_storage);
}

int SG_PhaseCatalogPublicationIssue(const sg_phase_catalog_source_t *source,
	const sg_phase_catalog_t *catalog,
	sg_phase_catalog_publication_t **publication_out,
	sg_phase_catalog_audit_result_t *audit_out)
{
	sg_phase_catalog_audit_result_t audit;
	sg_phase_catalog_publication_t *publication;

	if (audit_out)
		memset(audit_out, 0, sizeof(*audit_out));
	if (!publication_out || *publication_out)
	{
		if (audit_out)
			audit_out->code = SG_PHASE_CATALOG_AUDIT_INVALID_ARGUMENT;
		return 0;
	}
	*publication_out = NULL;
	memset(&audit, 0, sizeof(audit));
	if (!SG_PhaseCatalogAudit(source, catalog, &audit))
	{
		if (audit_out)
			*audit_out = audit;
		return 0;
	}
	publication = calloc(1U, sizeof(*publication));
	if (!publication)
	{
		if (audit_out)
		{
			audit_out->code = SG_PHASE_CATALOG_AUDIT_STORAGE_DISAGREEMENT;
			audit_out->record = 0U;
		}
		return 0;
	}
	publication->phase_capacity = catalog->phase_count;
	publication->binding_capacity = catalog->binding_count;
	publication->transition_capacity = catalog->transition_count;
	if (catalog->phase_count != 0U &&
		!AllocationFits((size_t)catalog->phase_count,
			sizeof(*publication->phase_storage)))
		goto allocation_failure;
	if (catalog->phase_count != 0U)
	{
		publication->phase_storage = malloc((size_t)catalog->phase_count *
			sizeof(*publication->phase_storage));
		if (!publication->phase_storage)
			goto allocation_failure;
		memcpy(publication->phase_storage, catalog->phases,
			(size_t)catalog->phase_count * sizeof(*catalog->phases));
		publication->view.phases = publication->phase_storage;
	}
	if (catalog->binding_count != 0U &&
		!AllocationFits((size_t)catalog->binding_count,
			sizeof(*publication->binding_storage)))
		goto allocation_failure;
	if (catalog->binding_count != 0U)
	{
		publication->binding_storage = malloc((size_t)catalog->binding_count *
			sizeof(*publication->binding_storage));
		if (!publication->binding_storage)
			goto allocation_failure;
		memcpy(publication->binding_storage, catalog->bindings,
			(size_t)catalog->binding_count * sizeof(*catalog->bindings));
		publication->view.bindings = publication->binding_storage;
	}
	if (catalog->transition_count != 0U &&
		(!AllocationFits((size_t)catalog->transition_count,
			sizeof(*publication->transition_storage)) ||
		 !AllocationFits((size_t)catalog->transition_count,
			sizeof(*publication->transition_evidence_storage))))
		goto allocation_failure;
	if (catalog->transition_count != 0U)
	{
		publication->transition_storage = malloc((size_t)catalog->transition_count *
			sizeof(*publication->transition_storage));
		publication->transition_evidence_storage = malloc(
			(size_t)catalog->transition_count *
			sizeof(*publication->transition_evidence_storage));
		if (!publication->transition_storage ||
			!publication->transition_evidence_storage)
			goto allocation_failure;
		memcpy(publication->transition_storage, catalog->transitions,
			(size_t)catalog->transition_count * sizeof(*catalog->transitions));
		memcpy(publication->transition_evidence_storage,
			catalog->transition_evidence,
			(size_t)catalog->transition_count *
				sizeof(*catalog->transition_evidence));
		publication->view.transitions = publication->transition_storage;
		publication->view.transition_evidence =
			publication->transition_evidence_storage;
	}
	publication->magic = SG_PHASE_CATALOG_PUBLICATION_MAGIC;
	publication->magic_inverse = ~SG_PHASE_CATALOG_PUBLICATION_MAGIC;
	publication->self = publication;
	publication->view.identity = catalog->identity;
	publication->view.completion = catalog->completion;
	publication->view.transition_completion = catalog->transition_completion;
	publication->view.mover_support_verifier_identity =
		catalog->mover_support_verifier_identity;
	publication->view.phase_count = catalog->phase_count;
	publication->view.binding_count = catalog->binding_count;
	publication->view.transition_count = catalog->transition_count;
	if (!PublicationStorageShapeValid(publication))
		goto allocation_failure;
	if (audit_out)
		*audit_out = audit;
	*publication_out = publication;
	return 1;

allocation_failure:
	free(publication->phase_storage);
	free(publication->binding_storage);
	free(publication->transition_storage);
	free(publication->transition_evidence_storage);
	memset(publication, 0, sizeof(*publication));
	free(publication);
	if (audit_out)
	{
		audit_out->code = SG_PHASE_CATALOG_AUDIT_STORAGE_DISAGREEMENT;
		audit_out->record = 0U;
	}
	return 0;
}

int SG_PhaseCatalogPublicationRead(
	const sg_phase_catalog_publication_t *publication,
	const sg_phase_catalog_view_t **view_out)
{
	if (view_out)
		*view_out = NULL;
	if (!view_out || !PublicationHeaderValid(publication) ||
		!PublicationStorageShapeValid(publication))
		return 0;
	*view_out = &publication->view;
	return 1;
}

int SG_PhaseCatalogPublicationStorageOverlaps(
	const sg_phase_catalog_publication_t *publication,
	const void *address, size_t size)
{
	if (!PublicationHeaderValid(publication) ||
		!PublicationStorageShapeValid(publication) || !address || size == 0U)
		return 0;
	return RangesOverlap(address, size, publication->view.phases,
		(size_t)publication->view.phase_count * sizeof(*publication->view.phases)) ||
		RangesOverlap(address, size, publication->view.bindings,
		(size_t)publication->view.binding_count * sizeof(*publication->view.bindings)) ||
		RangesOverlap(address, size, publication->view.transitions,
		(size_t)publication->view.transition_count *
			sizeof(*publication->view.transitions)) ||
		RangesOverlap(address, size, publication->view.transition_evidence,
		(size_t)publication->view.transition_count *
			sizeof(*publication->view.transition_evidence));
}

void SG_PhaseCatalogPublicationDestroy(
	sg_phase_catalog_publication_t *publication)
{
	if (!publication)
		return;
	free(publication->phase_storage);
	free(publication->binding_storage);
	free(publication->transition_storage);
	free(publication->transition_evidence_storage);
	memset(publication, 0, sizeof(*publication));
	free(publication);
}
