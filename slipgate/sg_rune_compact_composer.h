/* Transactional assembly of the compact RUNE's canonical owned model. */
#ifndef SG_RUNE_COMPACT_COMPOSER_H
#define SG_RUNE_COMPACT_COMPOSER_H

#include <stdint.h>

#include "sg_rune_compact_builder.h"
#include "sg_rune_compact_geometry.h"
#include "sg_rune_compact_movement_fields.h"
#include "sg_rune_compact_static_materializer.h"
#include "sg_rune_compact_weapon_field.h"
#include "sg_rune_compact_weapon_relations.h"

typedef struct sg_rune_compact_composer_s sg_rune_compact_composer_t;

typedef enum sg_rune_compact_composer_error_code_e
{
	SG_RUNE_COMPACT_COMPOSER_ERROR_NONE = 0,
	SG_RUNE_COMPACT_COMPOSER_ERROR_INVALID_ARGUMENT,
	SG_RUNE_COMPACT_COMPOSER_ERROR_OWNER_READ,
	SG_RUNE_COMPACT_COMPOSER_ERROR_IDENTITY_MISMATCH,
	SG_RUNE_COMPACT_COMPOSER_ERROR_INVALID_FRAGMENT,
	SG_RUNE_COMPACT_COMPOSER_ERROR_LIMIT_EXCEEDED,
	SG_RUNE_COMPACT_COMPOSER_ERROR_OVERFLOW,
	SG_RUNE_COMPACT_COMPOSER_ERROR_OUT_OF_MEMORY,
	SG_RUNE_COMPACT_COMPOSER_ERROR_MODEL_REJECTED,
	SG_RUNE_COMPACT_COMPOSER_ERROR_CODE_COUNT
} sg_rune_compact_composer_error_code_t;

typedef enum sg_rune_compact_composer_record_domain_e
{
	SG_RUNE_COMPACT_COMPOSER_RECORD_MODEL = 0,
	SG_RUNE_COMPACT_COMPOSER_RECORD_IDENTITY,
	SG_RUNE_COMPACT_COMPOSER_RECORD_GEOMETRY,
	SG_RUNE_COMPACT_COMPOSER_RECORD_STATIC,
	SG_RUNE_COMPACT_COMPOSER_RECORD_MOVEMENT,
	SG_RUNE_COMPACT_COMPOSER_RECORD_WEAPON,
	SG_RUNE_COMPACT_COMPOSER_RECORD_ANALYTIC,
	SG_RUNE_COMPACT_COMPOSER_RECORD_ALLOCATION
} sg_rune_compact_composer_record_domain_t;

typedef struct sg_rune_compact_composer_error_s
{
	sg_rune_compact_composer_error_code_t code;
	sg_rune_compact_composer_record_domain_t domain;
	uint32_t record;
} sg_rune_compact_composer_error_t;

/* Every fragment is an opaque owner.  The composer obtains its identity and
 * borrowed view from owner-issued accessors, then copies the result into one
 * immutable model.  It never accepts caller-assembled array views. */
int SG_RuneCompactComposerBuild(
	const sg_rune_compact_builder_t *builder,
	const sg_rune_compact_geometry_t *geometry,
	const sg_rune_compact_mechanisms_t *mechanisms,
	const sg_rune_compact_static_materializer_t *static_materializer,
	const sg_rune_compact_movement_fields_t *movement_fields,
	const sg_rune_compact_weapon_relations_t *relations,
	const sg_rune_compact_weapon_field_t *weapon_field,
	sg_rune_compact_composer_t **composer_out,
	sg_rune_compact_composer_error_t *error_out);

const sg_rune_compact_model_t *SG_RuneCompactComposerModel(
	const sg_rune_compact_composer_t *composer);

void SG_RuneCompactComposerDestroy(sg_rune_compact_composer_t *composer);

const char *SG_RuneCompactComposerErrorString(
	sg_rune_compact_composer_error_code_t code);

#endif
