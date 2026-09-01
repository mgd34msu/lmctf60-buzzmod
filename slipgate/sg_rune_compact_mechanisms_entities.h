#ifndef SG_RUNE_COMPACT_MECHANISMS_ENTITIES_H
#define SG_RUNE_COMPACT_MECHANISMS_ENTITIES_H

#include <stddef.h>
#include <stdint.h>

#include "sg_rune_compact_builder.h"
#include "sg_rune_compact_mechanisms.h"

typedef struct sg_rune_compact_mechanisms_entities_s
{
	struct sg_rune_compact_mechanism_entity_authority_s *mechanisms;
	uint32_t mechanism_count;
	struct sg_rune_compact_mechanism_entity_controller_s *controllers;
	uint32_t controller_count;
	sg_rune_compact_mechanism_topology_edge_t *topology_edges;
	uint32_t topology_edge_count;
} sg_rune_compact_mechanisms_entities_t;

typedef struct sg_rune_compact_mechanism_entity_authority_s
{
	sg_rune_compact_mechanism_entity_ref_t source;
	sg_rune_compact_mechanism_authority_kind_t kind;
	sg_rune_compact_mechanism_activation_mask_t activation;
	sg_rune_compact_mechanism_span_t controllers;
	sg_rune_compact_mechanism_span_t topology;
	uint32_t delay_ms;
	uint32_t dwell_ms;
	uint32_t pause_ms;
	uint32_t travel_ms;
	int32_t damage;
	int32_t health;
	uint32_t required_item;
	sg_rune_compact_mechanism_authority_state_t initial_state;
	sg_rune_compact_mechanism_authority_state_t activated_state;
	sg_rune_compact_mechanism_authority_state_t reset_state;
	uint32_t recovery_ms;
	sg_rune_compact_mechanism_authority_flags_t flags;
} sg_rune_compact_mechanism_entity_authority_t;

typedef struct sg_rune_compact_mechanism_entity_controller_s
{
	uint32_t mechanism;
	sg_rune_compact_mechanism_entity_ref_t controller;
	uint32_t topology_edge;
	sg_rune_compact_mechanism_activation_mask_t activation;
	int32_t damage;
	int32_t health;
	uint32_t required_item;
	sg_rune_compact_mechanism_controller_flags_t flags;
	sg_rune_compact_mechanism_controller_spatiality_t spatiality;
} sg_rune_compact_mechanism_entity_controller_t;

int SG_RuneCompactMechanismEntitiesEnumerate(
	const sg_rune_compact_builder_t *builder,
	sg_rune_compact_mechanisms_entities_t *entities_out,
	sg_rune_compact_mechanisms_error_t *error_out);
void SG_RuneCompactMechanismEntitiesRelease(
	sg_rune_compact_mechanisms_entities_t *entities);

#if defined(SG_RUNE_COMPACT_MECHANISMS_ENTITIES_TESTING)
void SG_RuneCompactMechanismEntitiesTestFailAfter(size_t allocation);
size_t SG_RuneCompactMechanismEntitiesTestAllocationCount(void);
#endif

#endif
