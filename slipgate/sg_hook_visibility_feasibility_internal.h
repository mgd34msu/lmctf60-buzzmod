#ifndef SG_HOOK_VISIBILITY_FEASIBILITY_INTERNAL_H
#define SG_HOOK_VISIBILITY_FEASIBILITY_INTERNAL_H

#include "sg_hook_visibility_feasibility.h"

#define SG_HOOK_VISIBILITY_CATALOG_MAGIC UINT64_C(0x4856464541533031)

typedef enum sg_hook_visibility_terminal_outcome_e
{
	SG_HOOK_VISIBILITY_TERMINAL_HOOKABLE = 0,
	SG_HOOK_VISIBILITY_TERMINAL_SKY,
	SG_HOOK_VISIBILITY_TERMINAL_NONHOOKABLE,
	SG_HOOK_VISIBILITY_TERMINAL_NO_HIT,
	SG_HOOK_VISIBILITY_TERMINAL_CLEARANCE_BLOCKED
} sg_hook_visibility_terminal_outcome_t;

typedef uint32_t sg_hook_visibility_terminal_flags_t;
enum
{
	SG_HOOK_VISIBILITY_TERMINAL_LOWER_DIMENSIONAL = UINT32_C(1) << 0,
	SG_HOOK_VISIBILITY_TERMINAL_EDGE = UINT32_C(1) << 1,
	SG_HOOK_VISIBILITY_TERMINAL_VERTEX = UINT32_C(1) << 2,
	SG_HOOK_VISIBILITY_TERMINAL_TIE = UINT32_C(1) << 3
};

typedef struct sg_hook_visibility_terminal_s
{
	sg_hook_visibility_domain_term_t domain;
	sg_hook_visibility_terminal_outcome_t outcome;
	uint32_t surface_rule;
	sg_hook_visibility_terminal_flags_t flags;
} sg_hook_visibility_terminal_t;

typedef struct sg_hook_visibility_relation_s
{
	uint64_t surface_id;
	uint32_t model_index;
	uint32_t texinfo;
	sg_hook_visibility_domain_term_t *terms;
	uint32_t term_count;
	uint32_t term_capacity;
} sg_hook_visibility_relation_t;

struct sg_hook_visibility_feasibility_catalog_s
{
	uint64_t magic;
	uint64_t source_digest;
	uint64_t verifier_source_digest;
	uint64_t producer_identity;
	uint64_t verifier_identity;
	sg_rune_model_identity_t collision_identity;
	uint32_t world_counts[8];
	sg_hook_visibility_q8_box_t origins;
	sg_rune_stance_t stance;
	sg_hook_visibility_fire_law_t fire_law;
	sg_hook_visibility_control_root_t *controls;
	uint32_t control_count;
	sg_hook_visibility_surface_rule_t *surface_rules;
	uint32_t surface_rule_count;
	sg_hook_visibility_terminal_t *terminals;
	uint32_t terminal_count;
	uint32_t terminal_capacity;
	sg_hook_visibility_relation_t *relations;
	uint32_t relation_count;
	sg_hook_visibility_feasibility_metrics_t metrics;
};

typedef struct sg_hook_visibility_build_context_s
{
	const sg_hook_visibility_feasibility_sources_t *sources;
	sg_hook_visibility_feasibility_catalog_t *catalog;
	sg_hook_visibility_feasibility_error_t error;
} sg_hook_visibility_build_context_t;

typedef struct sg_hook_visibility_i16_span_s
{
	int16_t minimum;
	int16_t maximum;
} sg_hook_visibility_i16_span_t;

void SG_HookVisibilityFeasibilityAngleBits(uint16_t code,
	uint32_t *sine_bits_out, uint32_t *cosine_bits_out);
void SG_HookVisibilityFeasibilityShortSinCos(int16_t code,
	float *sine_out, float *cosine_out);
void SG_HookVisibilityFeasibilityDirection(int16_t pitch, int16_t yaw,
	float forward[3], float right[3]);
void SG_HookVisibilityFeasibilitySetError(
	sg_hook_visibility_build_context_t *build,
	sg_hook_visibility_feasibility_error_code_t code, uint32_t source_index);
uint64_t SG_HookVisibilityFeasibilitySourceDigest(
	const sg_hook_visibility_feasibility_sources_t *sources);
uint64_t SG_HookVisibilityFeasibilityVerifierSourceDigest(
	const sg_hook_visibility_feasibility_sources_t *sources);
int SG_HookVisibilityFeasibilityConstruct(
	sg_hook_visibility_build_context_t *build);
int SG_HookVisibilityFeasibilityFamilyValid(
	sg_hook_visibility_build_context_t *build);
int SG_HookVisibilityFeasibilityAxisSpans(
	sg_hook_visibility_build_context_t *build, uint32_t axis, int16_t pitch,
	int16_t yaw, sg_hook_visibility_hand_t hand,
	const sg_hook_visibility_i16_span_t prior_spans[3],
	sg_hook_visibility_i16_span_t **spans_out, uint32_t *count_out);
int SG_HookVisibilityFeasibilityEventCuts(
	sg_hook_visibility_build_context_t *build, uint32_t axis, int16_t pitch,
	int16_t yaw, sg_hook_visibility_hand_t hand,
	const sg_hook_visibility_i16_span_t prior_spans[3],
	int16_t **cuts_out, uint32_t *count_out, int16_t **split_cuts_out,
	uint32_t *split_count_out);
int SG_HookVisibilityFeasibilityAuditTiling(
	const sg_hook_visibility_feasibility_sources_t *sources,
	const sg_hook_visibility_feasibility_catalog_t *catalog,
	uint64_t *cardinality_out);
int SG_HookVisibilityFeasibilityAuditDomainUniform(
	const sg_hook_visibility_feasibility_sources_t *sources,
	const sg_hook_visibility_domain_term_t *domain);
int SG_HookVisibilityFeasibilityAuditFamilyValid(
	const sg_hook_visibility_feasibility_sources_t *sources);

#endif
