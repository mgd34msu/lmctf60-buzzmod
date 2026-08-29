#ifndef SG_MECHANISM_CAPABILITY_INTERNAL_H
#define SG_MECHANISM_CAPABILITY_INTERNAL_H

#include "sg_mechanism_capability.h"

#include <stdint.h>

typedef struct sg_mechanism_capability_payload_s
{
	sg_rune_model_identity_t identity;
	uint64_t candidate_verifier_identity;
	uint64_t trace_verifier_identity;
	uint64_t content_identity;
	sg_mechanism_capability_fact_t *facts;
	uint32_t fact_count;
	uint32_t *topology_edges;
	uint32_t topology_edge_count;
	sg_mechanism_topology_relation_t *topology_relations;
	uint32_t topology_relation_count;
	uint32_t *mechanism_offsets;
	uint32_t mechanism_offset_count;
	uint32_t *facts_by_trace;
	uint64_t topology_edge_visits;
	sg_mechanism_capability_view_t view;
} sg_mechanism_capability_payload_t;

typedef struct sg_mechanism_capability_record_s
{
	sg_mechanism_capability_set_t *token;
	sg_mechanism_capability_payload_t *payload;
	struct sg_mechanism_capability_record_s *next;
} sg_mechanism_capability_record_t;

struct sg_mechanism_capability_owner_s
{
	sg_mechanism_capability_record_t *live;
	uint32_t live_count;
};

uint64_t SG_MechanismCapabilityContentIdentity(
	const sg_mechanism_capability_payload_t *capabilities);
uint64_t SG_MechanismModelIdentityValue(
	const sg_rune_model_identity_t *identity);
uint64_t SG_MechanismCapabilityFactIdentity(
	const sg_mechanism_capability_fact_t *fact);
int SG_AuthorityTokenMint(uintptr_t *token_out);
int SG_MechanismCapabilityOwnerAccepted(
	const sg_mechanism_capability_owner_t *owner,
	const sg_mechanism_capability_set_t *capabilities,
	const sg_mechanism_capability_view_t **view_out);
sg_mechanism_capability_payload_t *SG_MechanismCapabilityOwnerPayload(
	const sg_mechanism_capability_owner_t *owner,
	const sg_mechanism_capability_set_t *capabilities);

#endif /* SG_MECHANISM_CAPABILITY_INTERNAL_H */
