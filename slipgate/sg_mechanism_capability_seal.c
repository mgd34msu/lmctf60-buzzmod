#include "sg_mechanism_capability_internal.h"

#include <string.h>

typedef struct sg_canonical_digest_s { uint64_t value; } sg_canonical_digest_t;

static void Byte(sg_canonical_digest_t *d, uint8_t v)
{
	d->value = (d->value ^ v) * UINT64_C(1099511628211);
}

static void U16(sg_canonical_digest_t *d, uint16_t v)
{
	Byte(d, (uint8_t)v); Byte(d, (uint8_t)(v >> 8));
}

static void U32(sg_canonical_digest_t *d, uint32_t v)
{
	uint32_t s;
	for (s = 0U; s != 32U; s += 8U) Byte(d, (uint8_t)(v >> s));
}

static void U64(sg_canonical_digest_t *d, uint64_t v)
{
	uint32_t s;
	for (s = 0U; s != 64U; s += 8U) Byte(d, (uint8_t)(v >> s));
}

static void F32(sg_canonical_digest_t *d, float v)
{
	uint32_t bits;
	if (v == 0.0f) v = 0.0f;
	memcpy(&bits, &v, sizeof(bits)); U32(d, bits);
}

static void Vec3(sg_canonical_digest_t *d, const sg_rune_vec3_t *v)
{
	uint32_t i;
	for (i = 0U; i < 3U; i++) F32(d, v->value[i]);
}

static void StableId(sg_canonical_digest_t *d, const sg_rune_stable_id_t *v)
{
	U64(d, v->source_set_identity); U64(d, v->high); U64(d, v->low);
}

static void Hull(sg_canonical_digest_t *d, const sg_rune_hull_profile_t *v)
{
	Vec3(d, &v->mins); Vec3(d, &v->maxs);
}

static void ModelIdentity(sg_canonical_digest_t *d,
	const sg_rune_model_identity_t *v)
{
	U64(d, v->bsp_content_id); U64(d, v->entity_semantics_id);
	U64(d, v->physics_abi_id); U64(d, v->source_set_identity);
	U64(d, v->schema_id); U64(d, v->producer_identity);
	Hull(d, &v->standing_hull); Hull(d, &v->crouching_hull);
	F32(d, v->physics.gravity); F32(d, v->physics.ground_acceleration);
	F32(d, v->physics.air_acceleration); F32(d, v->physics.water_acceleration);
	F32(d, v->physics.hook_acceleration);
	F32(d, v->physics.external_acceleration); F32(d, v->physics.water_drag);
	F32(d, v->physics.max_velocity); U32(d, v->physics.frame_ms);
	U32(d, v->physics.substep_ms);
}

static void HostTransform(sg_canonical_digest_t *d,
	const sg_host_collision_transform_t *v)
{
	uint32_t i;
	for (i = 0U; i < 3U; i++) F32(d, v->origin[i]);
	for (i = 0U; i < 3U; i++) F32(d, v->angles[i]);
}

static void HostTrace(sg_canonical_digest_t *d,
	const sg_host_collision_trace_t *v)
{
	uint32_t i;
	U32(d, (uint32_t)v->allsolid); U32(d, (uint32_t)v->startsolid);
	F32(d, v->fraction);
	for (i = 0U; i < 3U; i++) F32(d, v->end[i]);
	for (i = 0U; i < 3U; i++) F32(d, v->plane.normal[i]);
	F32(d, v->plane.distance); U32(d, (uint32_t)v->plane.type);
	U32(d, v->contents); U32(d, v->texinfo);
	U32(d, (uint32_t)v->surface_flags); U32(d, v->model_index);
	U64(d, v->instance_id);
}

static void HostTransition(sg_canonical_digest_t *d,
	const sg_host_collision_transition_t *v)
{
	U32(d, (uint32_t)v->source_valid);
	U32(d, (uint32_t)v->destination_valid); U32(d, (uint32_t)v->clear);
	HostTrace(d, &v->sweep);
}

static void Execution(sg_canonical_digest_t *d,
	const sg_mech_execution_state_t *v)
{
	U16(d, v->controller_kind); U16(d, v->node_kind); U16(d, v->think_role);
	U16(d, v->end_role); U16(d, v->platform_profile);
	U32(d, (uint32_t)v->motion_state);
	U32(d, (uint32_t)v->fixed_callbacks_match);
	U32(d, (uint32_t)v->touch_matches); U32(d, (uint32_t)v->touch_cleared);
	U32(d, (uint32_t)v->nextthink_pending); U32(d, (uint32_t)v->stopped);
}

static void Interval(sg_canonical_digest_t *d, const sg_rune_interval_t *v)
{
	F32(d, v->min_value); F32(d, v->max_value);
}

static void Parameters(sg_canonical_digest_t *d,
	const sg_mechanism_kernel_parameters_t *v)
{
	Interval(d, &v->displacement.x); Interval(d, &v->displacement.y);
	Interval(d, &v->displacement.z); Interval(d, &v->speed);
	Interval(d, &v->acceleration); Interval(d, &v->vertical_acceleration);
	F32(d, v->gravity); F32(d, v->drag); U64(d, v->physics_abi_id);
	U32(d, v->duration_ms); U32(d, v->fixed_latency_ms);
	U32(d, v->dwell_ms); U32(d, v->wait_ms); U32(d, v->reset_ms);
	U64(d, v->total_ms);
}

static void Fact(sg_canonical_digest_t *d,
	const sg_mechanism_capability_fact_t *v)
{
	U32(d, v->order); U64(d, v->trace_identity);
	StableId(d, &v->controller_id.value); StableId(d, &v->mechanism_id.value);
	U32(d, v->controller_entity); U32(d, v->mechanism_entity);
	U32(d, v->source_region); U32(d, v->destination_region);
	U32(d, v->source_phase); U32(d, v->destination_phase);
	U32(d, v->first_topology_edge); U32(d, v->topology_edge_count);
	U32(d, (uint32_t)v->kind); U32(d, (uint32_t)v->source_state);
	U32(d, (uint32_t)v->destination_state); U32(d, (uint32_t)v->activation);
	U32(d, (uint32_t)v->recovery); Vec3(d, &v->entry_witness);
	Vec3(d, &v->exit_witness); Vec3(d, &v->observed_displacement);
	Vec3(d, &v->observed_velocity); Vec3(d, &v->mechanism_direction);
	Vec3(d, &v->mechanism_origin); Vec3(d, &v->mechanism_angles);
	HostTransition(d, &v->inactive_transition);
	HostTransition(d, &v->active_transition);
	HostTransform(d, &v->inactive_mechanism_transform);
	HostTransform(d, &v->active_mechanism_transform);
	Execution(d, &v->source_execution); Execution(d, &v->destination_execution);
	U64(d, v->mechanism_instance_id); U32(d, v->delay_ms);
	U32(d, v->dwell_ms); U32(d, v->travel_ms); U32(d, v->wait_ms);
	U32(d, v->reset_ms); U64(d, v->activation_time_ms);
	U64(d, v->active_time_ms); U64(d, v->exit_time_ms);
	U64(d, v->reset_time_ms); Parameters(d, &v->parameters);
	U32(d, v->flags);
}

uint64_t SG_MechanismModelIdentityValue(
	const sg_rune_model_identity_t *identity)
{
	sg_canonical_digest_t d = { UINT64_C(1469598103934665603) };

	if (!identity) return 0U;
	U64(&d, UINT64_C(0x53474d4f444c3031)); ModelIdentity(&d, identity);
	return d.value == 0U ? UINT64_C(1) : d.value;
}

uint64_t SG_MechanismCapabilityFactIdentity(
	const sg_mechanism_capability_fact_t *fact)
{
	sg_canonical_digest_t d = { UINT64_C(1469598103934665603) };

	if (!fact) return 0U;
	U64(&d, UINT64_C(0x53474d4641433031)); Fact(&d, fact);
	return d.value == 0U ? UINT64_C(1) : d.value;
}

uint64_t SG_MechanismCapabilityContentIdentity(
	const sg_mechanism_capability_set_t *c)
{
	sg_canonical_digest_t d = { UINT64_C(1469598103934665603) };
	uint32_t i;

	if (!c || (c->fact_count != 0U && (!c->facts || !c->facts_by_trace)) ||
		(c->topology_edge_count != 0U && !c->topology_edges) ||
		(c->topology_relation_count != 0U && !c->topology_relations) ||
		(c->mechanism_offset_count != 0U && !c->mechanism_offsets)) return 0U;
	U64(&d, UINT64_C(0x53474d4341503031)); ModelIdentity(&d, &c->identity);
	U64(&d, c->candidate_verifier_identity); U64(&d, c->trace_verifier_identity);
	U32(&d, c->fact_count);
	for (i = 0U; i < c->fact_count; i++) Fact(&d, &c->facts[i]);
	U32(&d, c->topology_edge_count);
	for (i = 0U; i < c->topology_edge_count; i++) U32(&d, c->topology_edges[i]);
	U32(&d, c->topology_relation_count);
	for (i = 0U; i < c->topology_relation_count; i++) {
		U32(&d, c->topology_relations[i].controller_entity);
		U32(&d, c->topology_relations[i].mechanism_entity);
		U32(&d, c->topology_relations[i].first_edge);
		U32(&d, c->topology_relations[i].edge_count);
	}
	U32(&d, c->mechanism_offset_count);
	for (i = 0U; i < c->mechanism_offset_count; i++) U32(&d, c->mechanism_offsets[i]);
	for (i = 0U; i < c->fact_count; i++) U32(&d, c->facts_by_trace[i]);
	U64(&d, c->topology_edge_visits);
	return d.value == 0U ? UINT64_C(1) : d.value;
}
