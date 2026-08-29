#ifndef SG_BSP_ENTITY_SEMANTICS_PUBLICATION_H
#define SG_BSP_ENTITY_SEMANTICS_PUBLICATION_H

#include <stdint.h>

#include "sg_bsp_entity_semantics.h"
#include "sg_host_collision.h"
#include "sg_rune_v2_wire.h"

extern const sg_rune_v2_content_id_t SG_BSP_ENTITY_SEMANTICS_SCHEMA_ID;

/* The source identity is the SHA-256 identity calculated by the BSP loader.
 * Production callers must use an authority initialized on that same loaded
 * world; the audit compares both copied authority and world identities before
 * replaying any parsed entity facts. */
typedef struct sg_bsp_entity_semantics_binding_s
{
	sg_rune_v2_content_id_t source_identity;
	uint64_t source_set_identity;
	sg_rune_v2_content_id_t schema_identity;
} sg_bsp_entity_semantics_binding_t;

typedef enum sg_bsp_entity_semantics_completeness_e
{
	SG_BSP_ENTITY_SEMANTICS_COMPLETENESS_NONE = 0,
	SG_BSP_ENTITY_SEMANTICS_PROVEN_EMPTY,
	SG_BSP_ENTITY_SEMANTICS_COMPLETE
} sg_bsp_entity_semantics_completeness_t;

typedef enum sg_bsp_entity_semantics_audit_code_e
{
	SG_BSP_ENTITY_SEMANTICS_AUDIT_OK = 0,
	SG_BSP_ENTITY_SEMANTICS_AUDIT_INVALID_ARGUMENT,
	SG_BSP_ENTITY_SEMANTICS_AUDIT_INVALID_SOURCE,
	SG_BSP_ENTITY_SEMANTICS_AUDIT_IDENTITY_MISMATCH,
	SG_BSP_ENTITY_SEMANTICS_AUDIT_INVALID_FACT,
	SG_BSP_ENTITY_SEMANTICS_AUDIT_DUPLICATE_FACT,
	SG_BSP_ENTITY_SEMANTICS_AUDIT_UNRESOLVED_FACT,
	SG_BSP_ENTITY_SEMANTICS_AUDIT_OMITTED_FACT,
	SG_BSP_ENTITY_SEMANTICS_AUDIT_INVENTED_FACT,
	SG_BSP_ENTITY_SEMANTICS_AUDIT_FACT_DISAGREEMENT,
	SG_BSP_ENTITY_SEMANTICS_AUDIT_OUT_OF_MEMORY
} sg_bsp_entity_semantics_audit_code_t;

typedef enum sg_bsp_entity_semantics_fact_domain_e
{
	SG_BSP_ENTITY_SEMANTICS_FACT_NONE = 0,
	SG_BSP_ENTITY_SEMANTICS_FACT_IDENTITY,
	SG_BSP_ENTITY_SEMANTICS_FACT_WORLD,
	SG_BSP_ENTITY_SEMANTICS_FACT_ENTITY,
	SG_BSP_ENTITY_SEMANTICS_FACT_LANDMARK,
	SG_BSP_ENTITY_SEMANTICS_FACT_MECHANISM,
	SG_BSP_ENTITY_SEMANTICS_FACT_TOPOLOGY
} sg_bsp_entity_semantics_fact_domain_t;

typedef struct sg_bsp_entity_semantics_audit_result_s
{
	sg_bsp_entity_semantics_audit_code_t code;
	sg_bsp_entity_semantics_completeness_t completeness;
	sg_bsp_entity_semantics_fact_domain_t domain;
	uint32_t expected_entities;
	uint32_t expected_landmarks;
	uint32_t expected_mechanisms;
	uint32_t expected_edges;
	uint32_t omitted_facts;
	uint32_t invented_facts;
	uint32_t invalid_facts;
	uint32_t duplicate_facts;
	uint32_t unresolved_facts;
	uint32_t record;
} sg_bsp_entity_semantics_audit_result_t;

typedef struct sg_bsp_entity_semantics_publication_s
	sg_bsp_entity_semantics_publication_t;

typedef struct sg_bsp_entity_semantics_view_s
{
	sg_bsp_entity_semantics_binding_t binding;
	sg_bsp_entity_semantics_completeness_t completeness;
	sg_bsp_world_entity_semantics_t world;
	const sg_bsp_entity_semantic_t *entities;
	uint32_t entity_count;
	const sg_bsp_entity_semantic_edge_t *edges;
	uint32_t edge_count;
	const char *strings;
	uint32_t string_bytes;
} sg_bsp_entity_semantics_view_t;

int SG_BspEntitySemanticsAudit(const sg_host_collision_authority_t *authority,
	const sg_bsp_entity_semantics_binding_t *binding,
	const sg_bsp_entity_semantics_t *candidate,
	sg_bsp_entity_semantics_audit_result_t *result_out);

/* Issue publishes a source-reconstructed complete snapshot.  The caller may
 * destroy both authority/world and candidate after success; output remains
 * valid until destroy. */
int SG_BspEntitySemanticsPublicationIssue(
	const sg_host_collision_authority_t *authority,
	const sg_bsp_entity_semantics_binding_t *binding,
	const sg_bsp_entity_semantics_t *candidate,
	sg_bsp_entity_semantics_publication_t **publication_out,
	sg_bsp_entity_semantics_audit_result_t *result_out);

int SG_BspEntitySemanticsPublicationRead(
	const sg_bsp_entity_semantics_publication_t *publication,
	sg_bsp_entity_semantics_view_t *view_out);

const char *SG_BspEntitySemanticsViewString(
	const sg_bsp_entity_semantics_view_t *view, uint32_t offset);

void SG_BspEntitySemanticsPublicationDestroy(
	sg_bsp_entity_semantics_publication_t *publication);

const char *SG_BspEntitySemanticsAuditCodeString(
	sg_bsp_entity_semantics_audit_code_t code);

#endif
