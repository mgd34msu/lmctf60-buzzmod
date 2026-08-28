/* sg_rune_v2_acceptance.h -- minimal acceptance and publication contract. */
#ifndef SG_RUNE_V2_ACCEPTANCE_H
#define SG_RUNE_V2_ACCEPTANCE_H

#include "sg_rune_v2_wire.h"

#define SG_RUNE_V2_PROOF_BSP_OVERLAY UINT32_C(1)
#define SG_RUNE_V2_PROOF_HOST_BOUNDARY UINT32_C(2)
#define SG_RUNE_V2_PROOF_NO_INVENTED_PORTAL UINT32_C(4)
#define SG_RUNE_V2_PROOF_DETERMINISTIC UINT32_C(8)
#define SG_RUNE_V2_REQUIRED_PROOF_MASK UINT32_C(15)

#define SG_RUNE_V2_READER_GNU_C UINT32_C(1)
#define SG_RUNE_V2_READER_MAKE_C UINT32_C(2)
#define SG_RUNE_V2_READER_PYTHON UINT32_C(4)
#define SG_RUNE_V2_READER_LINTER UINT32_C(8)
#define SG_RUNE_V2_READER_SEMANTIC UINT32_C(16)
#define SG_RUNE_V2_READER_COLD_PROCESS UINT32_C(32)
#define SG_RUNE_V2_REQUIRED_READER_MASK UINT32_C(63)
#define SG_RUNE_V2_REQUIRED_READER_COUNT UINT32_C(6)

#define SG_RUNE_V2_SIDECAR_COST_ONLY UINT32_C(1)
#define SG_RUNE_V2_MAX_SIDECARS UINT32_C(31)
#define SG_RUNE_V2_MAX_SIDECAR_COST_MS UINT32_C(30000)

typedef struct sg_rune_v2_completeness_proof_s
{
	uint64_t generation;
	uint64_t expected_cells;
	uint64_t represented_cells;
	uint64_t expected_portals;
	uint64_t represented_portals;
	uint64_t omitted_cells;
	uint64_t omitted_portals;
	uint64_t invented_portals;
	uint64_t invalid_portals;
	uint32_t flags;
	sg_rune_v2_content_id_t bsp_identity;
	sg_rune_v2_content_id_t schema_identity;
	sg_rune_v2_content_id_t artifact_identity;
	sg_rune_v2_content_id_t proof_identity;
} sg_rune_v2_completeness_proof_t;

typedef struct sg_rune_v2_reader_result_s
{
	uint32_t reader;
	uint64_t generation;
	sg_rune_v2_content_id_t artifact_identity;
	sg_rune_v2_content_id_t proof_identity;
} sg_rune_v2_reader_result_t;

typedef struct sg_rune_v2_reader_agreement_s
{
	uint32_t count;
	sg_rune_v2_reader_result_t result[SG_RUNE_V2_REQUIRED_READER_COUNT];
} sg_rune_v2_reader_agreement_t;

/* Sidecar identities are exact sidecar-file identities supplied by the same
 * content-identity boundary as artifact identities. */
typedef struct sg_rune_v2_sidecar_binding_s
{
	uint32_t kind;
	uint32_t flags;
	uint64_t generation;
	uint32_t geometry_changes;
	uint32_t connectivity_changes;
	uint32_t maximum_cost_ms;
	sg_rune_v2_content_id_t artifact_identity;
	sg_rune_v2_content_id_t sidecar_identity;
} sg_rune_v2_sidecar_binding_t;

typedef struct sg_rune_v2_acceptance_evidence_s
{
	const sg_rune_v2_artifact_binding_t *artifact;
	const sg_rune_v2_content_id_t *exact_artifact_identity;
	const sg_rune_v2_completeness_proof_t *proof;
	const sg_rune_v2_content_id_t *exact_proof_identity;
	const sg_rune_v2_reader_agreement_t *readers;
	const sg_rune_v2_sidecar_binding_t *sidecars;
	const sg_rune_v2_content_id_t *exact_sidecar_identities;
	uint32_t sidecar_count;
	sg_rune_v2_content_id_t sidecar_set_identity;
} sg_rune_v2_acceptance_evidence_t;

typedef struct sg_rune_v2_accepted_sidecar_s
{
	uint32_t kind;
	sg_rune_v2_content_id_t exact_identity;
} sg_rune_v2_accepted_sidecar_t;

typedef struct sg_rune_v2_accepted_artifact_s
{
	uint64_t generation;
	uint32_t reader_mask;
	uint32_t sidecar_mask;
	uint32_t sidecar_count;
	sg_rune_v2_content_id_t bsp_identity;
	sg_rune_v2_content_id_t schema_identity;
	sg_rune_v2_content_id_t artifact_identity;
	sg_rune_v2_content_id_t proof_identity;
	sg_rune_v2_content_id_t sidecar_set_identity;
	sg_rune_v2_accepted_sidecar_t sidecars[SG_RUNE_V2_MAX_SIDECARS];
} sg_rune_v2_accepted_artifact_t;

typedef enum sg_rune_v2_acceptance_diagnostic_e
{
	SG_RUNE_V2_ACCEPT_OK = 0,
	SG_RUNE_V2_ACCEPT_INVALID_ARGUMENT,
	SG_RUNE_V2_ACCEPT_WIRE_REJECTED,
	SG_RUNE_V2_ACCEPT_ARTIFACT_IDENTITY,
	SG_RUNE_V2_ACCEPT_PROOF,
	SG_RUNE_V2_ACCEPT_READER_AGREEMENT,
	SG_RUNE_V2_ACCEPT_SIDECAR
} sg_rune_v2_acceptance_diagnostic_t;

static inline int SG_RuneV2CompletenessProofAccepts(
	const sg_rune_v2_completeness_proof_t *proof,
	const sg_rune_v2_artifact_binding_t *artifact,
	const sg_rune_v2_content_id_t *exact_proof_identity,
	uint32_t cell_count, uint32_t portal_count)
{
	return proof && artifact && SG_RuneV2ContentIdValid(exact_proof_identity) &&
		proof->generation == artifact->generation &&
		proof->expected_cells == proof->represented_cells &&
		proof->expected_portals == proof->represented_portals &&
		proof->represented_cells == cell_count &&
		proof->represented_portals == portal_count &&
		proof->omitted_cells == 0U && proof->omitted_portals == 0U &&
		proof->invented_portals == 0U && proof->invalid_portals == 0U &&
		proof->flags == SG_RUNE_V2_REQUIRED_PROOF_MASK &&
		SG_RuneV2ContentIdEqual(&proof->bsp_identity,
			&artifact->bsp_identity) &&
		SG_RuneV2ContentIdEqual(&proof->schema_identity,
			&artifact->schema_identity) &&
		SG_RuneV2ContentIdEqual(&proof->artifact_identity,
			&artifact->artifact_identity) &&
		SG_RuneV2ContentIdEqual(&proof->proof_identity,
			exact_proof_identity);
}

static inline int SG_RuneV2ReaderAgreementAccepts(
	const sg_rune_v2_reader_agreement_t *agreement,
	const sg_rune_v2_artifact_binding_t *artifact,
	const sg_rune_v2_content_id_t *proof_identity)
{
	uint32_t mask = 0U;
	uint32_t index;

	if (!agreement || !artifact || !proof_identity ||
		agreement->count != SG_RUNE_V2_REQUIRED_READER_COUNT)
		return 0;
	for (index = 0; index < agreement->count; index++)
	{
		const sg_rune_v2_reader_result_t *result = &agreement->result[index];

		if (result->reader == 0U ||
			(result->reader & ~SG_RUNE_V2_REQUIRED_READER_MASK) != 0U ||
			(result->reader & (result->reader - 1U)) != 0U ||
			(mask & result->reader) != 0U ||
			result->generation != artifact->generation ||
			!SG_RuneV2ContentIdEqual(&result->artifact_identity,
				&artifact->artifact_identity) ||
			!SG_RuneV2ContentIdEqual(&result->proof_identity, proof_identity))
			return 0;
		mask |= result->reader;
	}
	return mask == SG_RUNE_V2_REQUIRED_READER_MASK;
}

static inline int SG_RuneV2SidecarBindingAccepts(
	const sg_rune_v2_sidecar_binding_t *sidecar,
	const sg_rune_v2_artifact_binding_t *artifact,
	const sg_rune_v2_content_id_t *exact_sidecar_identity)
{
	return sidecar && artifact && sidecar->kind > 0U &&
		sidecar->kind <= SG_RUNE_V2_MAX_SIDECARS &&
		(sidecar->flags & ~SG_RUNE_V2_SIDECAR_COST_ONLY) == 0U &&
		sidecar->generation == artifact->generation &&
		sidecar->geometry_changes == 0U && sidecar->connectivity_changes == 0U &&
		SG_RuneV2ContentIdValid(&sidecar->sidecar_identity) &&
		SG_RuneV2ContentIdValid(exact_sidecar_identity) &&
		((sidecar->flags & SG_RUNE_V2_SIDECAR_COST_ONLY) == 0U ||
		 (sidecar->maximum_cost_ms > 0U &&
		  sidecar->maximum_cost_ms <= SG_RUNE_V2_MAX_SIDECAR_COST_MS)) &&
		SG_RuneV2ContentIdEqual(&sidecar->artifact_identity,
			&artifact->artifact_identity) &&
		SG_RuneV2ContentIdEqual(&sidecar->sidecar_identity,
			exact_sidecar_identity);
}

static inline sg_rune_v2_acceptance_diagnostic_t SG_RuneV2Accept(
	const unsigned char *encoded, size_t encoded_size,
	const sg_rune_v2_acceptance_evidence_t *evidence,
	sg_rune_v2_accepted_artifact_t *accepted_out)
{
	sg_rune_v2_wire_view_t wire;
	uint32_t sidecar_mask = 0U;
	uint32_t index;
	uint32_t accepted_index = 0U;

	if (!encoded || !evidence || !accepted_out || !evidence->artifact ||
		!evidence->exact_artifact_identity || !evidence->proof ||
		!evidence->exact_proof_identity || !evidence->readers ||
		evidence->sidecar_count > SG_RUNE_V2_MAX_SIDECARS ||
		(evidence->sidecar_count != 0U && (!evidence->sidecars ||
		 !evidence->exact_sidecar_identities)) ||
		(evidence->sidecar_count == 0U && (evidence->sidecars ||
		 evidence->exact_sidecar_identities ||
		 SG_RuneV2ContentIdValid(&evidence->sidecar_set_identity))) ||
		(evidence->sidecar_count != 0U &&
		 !SG_RuneV2ContentIdValid(&evidence->sidecar_set_identity)))
		return SG_RUNE_V2_ACCEPT_INVALID_ARGUMENT;
	if (SG_RuneV2WireInspect(encoded, encoded_size, &wire) != SG_RUNE_V2_WIRE_OK)
		return SG_RUNE_V2_ACCEPT_WIRE_REJECTED;
	if (!SG_RuneV2ArtifactBindingAccepts(&wire, evidence->artifact,
		evidence->exact_artifact_identity))
		return SG_RUNE_V2_ACCEPT_ARTIFACT_IDENTITY;
	if (!SG_RuneV2CompletenessProofAccepts(evidence->proof,
		evidence->artifact, evidence->exact_proof_identity,
		wire.section[SG_RUNE_V2_SECTION_CELLS - 1U].count,
		wire.section[SG_RUNE_V2_SECTION_PORTALS - 1U].count))
		return SG_RUNE_V2_ACCEPT_PROOF;
	if (!SG_RuneV2ReaderAgreementAccepts(evidence->readers,
		evidence->artifact, evidence->exact_proof_identity))
		return SG_RUNE_V2_ACCEPT_READER_AGREEMENT;
	for (index = 0; index < evidence->sidecar_count; index++)
	{
		uint32_t bit;

		if (!SG_RuneV2SidecarBindingAccepts(&evidence->sidecars[index],
			evidence->artifact, &evidence->exact_sidecar_identities[index]))
			return SG_RUNE_V2_ACCEPT_SIDECAR;
		bit = UINT32_C(1) << (evidence->sidecars[index].kind - 1U);
		if ((sidecar_mask & bit) != 0U)
			return SG_RUNE_V2_ACCEPT_SIDECAR;
		sidecar_mask |= bit;
	}
	accepted_out->generation = evidence->artifact->generation;
	accepted_out->reader_mask = SG_RUNE_V2_REQUIRED_READER_MASK;
	accepted_out->sidecar_mask = sidecar_mask;
	accepted_out->sidecar_count = evidence->sidecar_count;
	accepted_out->bsp_identity = evidence->artifact->bsp_identity;
	accepted_out->schema_identity = evidence->artifact->schema_identity;
	accepted_out->artifact_identity = evidence->artifact->artifact_identity;
	accepted_out->proof_identity = evidence->proof->proof_identity;
	accepted_out->sidecar_set_identity = evidence->sidecar_set_identity;
	for (index = 0U; index < SG_RUNE_V2_MAX_SIDECARS; index++)
		accepted_out->sidecars[index] = (sg_rune_v2_accepted_sidecar_t){ 0 };
	for (index = 1U; index <= SG_RUNE_V2_MAX_SIDECARS; index++)
	{
		uint32_t source;

		if ((sidecar_mask & (UINT32_C(1) << (index - 1U))) == 0U)
			continue;
		for (source = 0U; source < evidence->sidecar_count; source++)
			if (evidence->sidecars[source].kind == index)
			{
				accepted_out->sidecars[accepted_index].kind = index;
				accepted_out->sidecars[accepted_index].exact_identity =
					evidence->exact_sidecar_identities[source];
				accepted_index++;
				break;
			}
	}
	return SG_RUNE_V2_ACCEPT_OK;
}

typedef struct sg_rune_v2_generation_s
{
	uint64_t generation;
	uint32_t sidecar_mask;
	sg_rune_v2_content_id_t artifact_identity;
	sg_rune_v2_content_id_t sidecar_set_identity;
} sg_rune_v2_generation_t;

typedef enum sg_rune_v2_publication_state_e
{
	SG_RUNE_V2_PUBLICATION_EMPTY = 0,
	SG_RUNE_V2_PUBLICATION_STAGED,
	SG_RUNE_V2_PUBLICATION_DURABLE,
	SG_RUNE_V2_PUBLICATION_COMMITTING,
	SG_RUNE_V2_PUBLICATION_PUBLISHED,
	SG_RUNE_V2_PUBLICATION_ROLLING_BACK,
	SG_RUNE_V2_PUBLICATION_ROLLED_BACK
} sg_rune_v2_publication_state_t;

typedef enum sg_rune_v2_publication_event_kind_e
{
	SG_RUNE_V2_EVENT_PUBLICATION = 1,
	SG_RUNE_V2_EVENT_ROLLBACK = 2
} sg_rune_v2_publication_event_kind_t;

typedef struct sg_rune_v2_publication_event_s
{
	uint32_t kind;
	uint64_t sequence;
	uint64_t affected_generation;
	uint64_t prior_generation;
	uint64_t active_generation;
	sg_rune_v2_content_id_t affected_artifact_identity;
	sg_rune_v2_content_id_t active_artifact_identity;
} sg_rune_v2_publication_event_t;

typedef struct sg_rune_v2_publication_s
{
	uint32_t state;
	uint32_t state_inverse;
	uint64_t event_sequence;
	sg_rune_v2_generation_t active;
	sg_rune_v2_generation_t pending;
} sg_rune_v2_publication_t;

static inline int SG_RuneV2GenerationValid(
	const sg_rune_v2_generation_t *generation)
{
	return generation && generation->generation != 0U &&
		SG_RuneV2ContentIdValid(&generation->artifact_identity) &&
		(generation->sidecar_mask & UINT32_C(0x80000000)) == 0U &&
		((generation->sidecar_mask == 0U &&
		  !SG_RuneV2ContentIdValid(&generation->sidecar_set_identity)) ||
		 (generation->sidecar_mask != 0U &&
		  SG_RuneV2ContentIdValid(&generation->sidecar_set_identity)));
}

static inline int SG_RuneV2PublicationValid(
	const sg_rune_v2_publication_t *publication)
{
	return publication && publication->state_inverse == ~publication->state;
}

static inline void SG_RuneV2PublicationInit(
	sg_rune_v2_publication_t *publication)
{
	if (!publication)
		return;
	*publication = (sg_rune_v2_publication_t){ 0 };
	publication->state_inverse = ~publication->state;
}

static inline void SG_RuneV2PublicationSetState(
	sg_rune_v2_publication_t *publication, sg_rune_v2_publication_state_t state)
{
	publication->state = (uint32_t)state;
	publication->state_inverse = ~publication->state;
}

static inline int SG_RuneV2PublicationStage(
	sg_rune_v2_publication_t *publication,
	const sg_rune_v2_generation_t *generation)
{
	if (!SG_RuneV2PublicationValid(publication) ||
		!SG_RuneV2GenerationValid(generation) ||
		(publication->state != SG_RUNE_V2_PUBLICATION_EMPTY &&
		 publication->state != SG_RUNE_V2_PUBLICATION_PUBLISHED &&
		 publication->state != SG_RUNE_V2_PUBLICATION_ROLLED_BACK) ||
		(SG_RuneV2GenerationValid(&publication->active) &&
		 generation->generation <= publication->active.generation))
		return 0;
	publication->pending = *generation;
	SG_RuneV2PublicationSetState(publication, SG_RUNE_V2_PUBLICATION_STAGED);
	return 1;
}

static inline int SG_RuneV2PublicationMarkDurable(
	sg_rune_v2_publication_t *publication)
{
	if (!SG_RuneV2PublicationValid(publication) ||
		publication->state != SG_RUNE_V2_PUBLICATION_STAGED)
		return 0;
	SG_RuneV2PublicationSetState(publication, SG_RUNE_V2_PUBLICATION_DURABLE);
	return 1;
}

static inline int SG_RuneV2PublicationBeginCommit(
	sg_rune_v2_publication_t *publication)
{
	if (!SG_RuneV2PublicationValid(publication) ||
		publication->state != SG_RUNE_V2_PUBLICATION_DURABLE)
		return 0;
	SG_RuneV2PublicationSetState(publication, SG_RUNE_V2_PUBLICATION_COMMITTING);
	return 1;
}

static inline int SG_RuneV2PublicationCommit(
	sg_rune_v2_publication_t *publication,
	sg_rune_v2_publication_event_t *event_out)
{
	sg_rune_v2_generation_t prior;

	if (!SG_RuneV2PublicationValid(publication) || !event_out ||
		publication->state != SG_RUNE_V2_PUBLICATION_COMMITTING ||
		!SG_RuneV2GenerationValid(&publication->pending))
		return 0;
	prior = publication->active;
	publication->active = publication->pending;
	publication->pending = (sg_rune_v2_generation_t){ 0 };
	SG_RuneV2PublicationSetState(publication, SG_RUNE_V2_PUBLICATION_PUBLISHED);
	publication->event_sequence++;
	*event_out = (sg_rune_v2_publication_event_t){ 0 };
	event_out->kind = SG_RUNE_V2_EVENT_PUBLICATION;
	event_out->sequence = publication->event_sequence;
	event_out->affected_generation = publication->active.generation;
	event_out->prior_generation = prior.generation;
	event_out->active_generation = publication->active.generation;
	event_out->affected_artifact_identity = publication->active.artifact_identity;
	event_out->active_artifact_identity = publication->active.artifact_identity;
	return 1;
}

static inline int SG_RuneV2PublicationBeginRollback(
	sg_rune_v2_publication_t *publication)
{
	if (!SG_RuneV2PublicationValid(publication) ||
		(publication->state != SG_RUNE_V2_PUBLICATION_STAGED &&
		 publication->state != SG_RUNE_V2_PUBLICATION_DURABLE &&
		 publication->state != SG_RUNE_V2_PUBLICATION_COMMITTING) ||
		!SG_RuneV2GenerationValid(&publication->pending))
		return 0;
	SG_RuneV2PublicationSetState(publication,
		SG_RUNE_V2_PUBLICATION_ROLLING_BACK);
	return 1;
}

static inline int SG_RuneV2PublicationRollback(
	sg_rune_v2_publication_t *publication,
	sg_rune_v2_publication_event_t *event_out)
{
	sg_rune_v2_generation_t affected;

	if (!SG_RuneV2PublicationValid(publication) || !event_out ||
		publication->state != SG_RUNE_V2_PUBLICATION_ROLLING_BACK ||
		!SG_RuneV2GenerationValid(&publication->pending))
		return 0;
	affected = publication->pending;
	publication->pending = (sg_rune_v2_generation_t){ 0 };
	SG_RuneV2PublicationSetState(publication,
		SG_RUNE_V2_PUBLICATION_ROLLED_BACK);
	publication->event_sequence++;
	*event_out = (sg_rune_v2_publication_event_t){ 0 };
	event_out->kind = SG_RUNE_V2_EVENT_ROLLBACK;
	event_out->sequence = publication->event_sequence;
	event_out->affected_generation = affected.generation;
	event_out->prior_generation = publication->active.generation;
	event_out->active_generation = publication->active.generation;
	event_out->affected_artifact_identity = affected.artifact_identity;
	event_out->active_artifact_identity = publication->active.artifact_identity;
	return 1;
}

static inline int SG_RuneV2PublicationEventMatches(
	const sg_rune_v2_publication_t *publication,
	const sg_rune_v2_publication_event_t *event)
{
	uint32_t expected_state;

	if (!SG_RuneV2PublicationValid(publication) || !event ||
		event->sequence == 0U || event->sequence != publication->event_sequence ||
		event->active_generation != publication->active.generation ||
		!SG_RuneV2ContentIdEqual(&event->active_artifact_identity,
			&publication->active.artifact_identity))
		return 0;
	expected_state = event->kind == SG_RUNE_V2_EVENT_PUBLICATION
		? SG_RUNE_V2_PUBLICATION_PUBLISHED
		: event->kind == SG_RUNE_V2_EVENT_ROLLBACK
			? SG_RUNE_V2_PUBLICATION_ROLLED_BACK : UINT32_MAX;
	return publication->state == expected_state;
}

typedef enum sg_rune_v2_receipt_kind_e
{
	SG_RUNE_V2_RECEIPT_ACCEPTANCE = 1,
	SG_RUNE_V2_RECEIPT_PUBLICATION = 2,
	SG_RUNE_V2_RECEIPT_ROLLBACK = 3
} sg_rune_v2_receipt_kind_t;

typedef struct sg_rune_v2_receipt_s
{
	uint32_t kind;
	uint64_t generation;
	uint64_t event_sequence;
	uint32_t reader_mask;
	uint32_t sidecar_mask;
	sg_rune_v2_content_id_t bsp_identity;
	sg_rune_v2_content_id_t schema_identity;
	sg_rune_v2_content_id_t artifact_identity;
	sg_rune_v2_content_id_t proof_identity;
	sg_rune_v2_content_id_t sidecar_set_identity;
} sg_rune_v2_receipt_t;

static inline int SG_RuneV2ReceiptEventMatches(
	const sg_rune_v2_accepted_artifact_t *accepted,
	const sg_rune_v2_publication_event_t *event, uint32_t receipt_kind)
{
	uint32_t event_kind = receipt_kind == SG_RUNE_V2_RECEIPT_PUBLICATION
		? SG_RUNE_V2_EVENT_PUBLICATION
		: receipt_kind == SG_RUNE_V2_RECEIPT_ROLLBACK
			? SG_RUNE_V2_EVENT_ROLLBACK : 0U;

	return accepted && event && event_kind != 0U && event->kind == event_kind &&
		event->sequence != 0U &&
		event->affected_generation == accepted->generation &&
		SG_RuneV2ContentIdEqual(&event->affected_artifact_identity,
			&accepted->artifact_identity);
}

static inline int SG_RuneV2ReceiptIssue(
	const sg_rune_v2_accepted_artifact_t *accepted,
	const sg_rune_v2_publication_event_t *event, uint32_t kind,
	sg_rune_v2_receipt_t *receipt_out)
{
	if (!accepted || !receipt_out || accepted->generation == 0U ||
		!SG_RuneV2ContentIdValid(&accepted->artifact_identity) ||
		(kind == SG_RUNE_V2_RECEIPT_ACCEPTANCE && event) ||
		(kind != SG_RUNE_V2_RECEIPT_ACCEPTANCE &&
		 !SG_RuneV2ReceiptEventMatches(accepted, event, kind)))
		return 0;
	*receipt_out = (sg_rune_v2_receipt_t){ 0 };
	receipt_out->kind = kind;
	receipt_out->generation = accepted->generation;
	receipt_out->event_sequence = event ? event->sequence : 0U;
	receipt_out->reader_mask = accepted->reader_mask;
	receipt_out->sidecar_mask = accepted->sidecar_mask;
	receipt_out->bsp_identity = accepted->bsp_identity;
	receipt_out->schema_identity = accepted->schema_identity;
	receipt_out->artifact_identity = accepted->artifact_identity;
	receipt_out->proof_identity = accepted->proof_identity;
	receipt_out->sidecar_set_identity = accepted->sidecar_set_identity;
	return 1;
}

static inline int SG_RuneV2ReceiptAccepts(
	const sg_rune_v2_receipt_t *receipt,
	const sg_rune_v2_accepted_artifact_t *accepted,
	const sg_rune_v2_publication_event_t *event)
{
	if (!receipt || !accepted || receipt->generation != accepted->generation ||
		receipt->reader_mask != accepted->reader_mask ||
		receipt->sidecar_mask != accepted->sidecar_mask ||
		!SG_RuneV2ContentIdEqual(&receipt->bsp_identity,
			&accepted->bsp_identity) ||
		!SG_RuneV2ContentIdEqual(&receipt->schema_identity,
			&accepted->schema_identity) ||
		!SG_RuneV2ContentIdEqual(&receipt->artifact_identity,
			&accepted->artifact_identity) ||
		!SG_RuneV2ContentIdEqual(&receipt->proof_identity,
			&accepted->proof_identity) ||
		!SG_RuneV2ContentIdEqual(&receipt->sidecar_set_identity,
			&accepted->sidecar_set_identity))
		return 0;
	if (receipt->kind == SG_RUNE_V2_RECEIPT_ACCEPTANCE)
		return !event && receipt->event_sequence == 0U;
	return SG_RuneV2ReceiptEventMatches(accepted, event, receipt->kind) &&
		receipt->event_sequence == event->sequence;
}

#define SG_RUNE_V2_INVALIDATE_SNAPSHOT UINT32_C(1)
#define SG_RUNE_V2_INVALIDATE_ARTIFACT UINT32_C(2)
#define SG_RUNE_V2_INVALIDATE_PROOF UINT32_C(4)
#define SG_RUNE_V2_INVALIDATE_SIDECARS UINT32_C(8)
#define SG_RUNE_V2_INVALIDATE_BUNDLE UINT32_C(16)
#define SG_RUNE_V2_INVALIDATE_INSTALLATION UINT32_C(32)
#define SG_RUNE_V2_INVALIDATE_COLD_LOAD UINT32_C(64)
#define SG_RUNE_V2_INVALIDATE_MATCH_EVIDENCE UINT32_C(128)
#define SG_RUNE_V2_INVALIDATE_ROLLBACK UINT32_C(256)
#define SG_RUNE_V2_INVALIDATE_RECEIPTS UINT32_C(512)

typedef enum sg_rune_v2_invalidation_change_e
{
	SG_RUNE_V2_CHANGE_UNKNOWN = 0,
	SG_RUNE_V2_CHANGE_SOURCE,
	SG_RUNE_V2_CHANGE_BSP,
	SG_RUNE_V2_CHANGE_SCHEMA,
	SG_RUNE_V2_CHANGE_ARTIFACT,
	SG_RUNE_V2_CHANGE_PROOF,
	SG_RUNE_V2_CHANGE_SIDECAR,
	SG_RUNE_V2_CHANGE_BUNDLE,
	SG_RUNE_V2_CHANGE_INSTALLATION,
	SG_RUNE_V2_CHANGE_COLD_LOAD,
	SG_RUNE_V2_CHANGE_MATCH_EVIDENCE,
	SG_RUNE_V2_CHANGE_ROLLBACK,
	SG_RUNE_V2_CHANGE_RECEIPT,
	SG_RUNE_V2_CHANGE_OS_FILE_IDENTITY
} sg_rune_v2_invalidation_change_t;

static inline uint32_t SG_RuneV2InvalidationClosure(uint32_t mask)
{
	uint32_t previous;

	do
	{
		previous = mask;
		if ((mask & SG_RUNE_V2_INVALIDATE_SNAPSHOT) != 0U)
			mask |= SG_RUNE_V2_INVALIDATE_ARTIFACT;
		if ((mask & SG_RUNE_V2_INVALIDATE_ARTIFACT) != 0U)
			mask |= SG_RUNE_V2_INVALIDATE_PROOF |
				SG_RUNE_V2_INVALIDATE_SIDECARS |
				SG_RUNE_V2_INVALIDATE_BUNDLE;
		if ((mask & SG_RUNE_V2_INVALIDATE_SIDECARS) != 0U)
			mask |= SG_RUNE_V2_INVALIDATE_BUNDLE;
		if ((mask & SG_RUNE_V2_INVALIDATE_BUNDLE) != 0U)
			mask |= SG_RUNE_V2_INVALIDATE_INSTALLATION;
		if ((mask & SG_RUNE_V2_INVALIDATE_INSTALLATION) != 0U)
			mask |= SG_RUNE_V2_INVALIDATE_COLD_LOAD |
				SG_RUNE_V2_INVALIDATE_ROLLBACK;
		if ((mask & SG_RUNE_V2_INVALIDATE_COLD_LOAD) != 0U)
			mask |= SG_RUNE_V2_INVALIDATE_MATCH_EVIDENCE;
		if ((mask & (SG_RUNE_V2_INVALIDATE_PROOF |
			SG_RUNE_V2_INVALIDATE_MATCH_EVIDENCE |
			SG_RUNE_V2_INVALIDATE_ROLLBACK)) != 0U)
			mask |= SG_RUNE_V2_INVALIDATE_RECEIPTS;
	} while (mask != previous);
	return mask;
}

static inline uint32_t SG_RuneV2InvalidationMask(
	sg_rune_v2_invalidation_change_t change)
{
	uint32_t initial;

	switch (change)
	{
	case SG_RUNE_V2_CHANGE_SOURCE:
	case SG_RUNE_V2_CHANGE_BSP:
	case SG_RUNE_V2_CHANGE_SCHEMA:
	case SG_RUNE_V2_CHANGE_UNKNOWN:
		initial = SG_RUNE_V2_INVALIDATE_SNAPSHOT;
		break;
	case SG_RUNE_V2_CHANGE_ARTIFACT:
		initial = SG_RUNE_V2_INVALIDATE_ARTIFACT;
		break;
	case SG_RUNE_V2_CHANGE_PROOF:
		initial = SG_RUNE_V2_INVALIDATE_PROOF;
		break;
	case SG_RUNE_V2_CHANGE_SIDECAR:
		initial = SG_RUNE_V2_INVALIDATE_SIDECARS;
		break;
	case SG_RUNE_V2_CHANGE_BUNDLE:
		initial = SG_RUNE_V2_INVALIDATE_BUNDLE;
		break;
	case SG_RUNE_V2_CHANGE_INSTALLATION:
		initial = SG_RUNE_V2_INVALIDATE_INSTALLATION;
		break;
	case SG_RUNE_V2_CHANGE_COLD_LOAD:
		initial = SG_RUNE_V2_INVALIDATE_COLD_LOAD;
		break;
	case SG_RUNE_V2_CHANGE_MATCH_EVIDENCE:
		initial = SG_RUNE_V2_INVALIDATE_MATCH_EVIDENCE;
		break;
	case SG_RUNE_V2_CHANGE_ROLLBACK:
		initial = SG_RUNE_V2_INVALIDATE_ROLLBACK;
		break;
	case SG_RUNE_V2_CHANGE_RECEIPT:
		initial = SG_RUNE_V2_INVALIDATE_RECEIPTS;
		break;
	case SG_RUNE_V2_CHANGE_OS_FILE_IDENTITY:
		return 0U;
	default:
		initial = SG_RUNE_V2_INVALIDATE_SNAPSHOT;
		break;
	}
	return SG_RuneV2InvalidationClosure(initial);
}

#endif /* SG_RUNE_V2_ACCEPTANCE_H */
