#ifndef SG_FIELD_KEY_H
#define SG_FIELD_KEY_H

typedef struct
{
	const int *field;
	int root_seed;
} sg_field_key_t;

static inline int SG_FieldKeyMatches(sg_field_key_t left,
	sg_field_key_t right)
{
	return left.field && right.field && left.root_seed >= 0 &&
	       right.root_seed >= 0 && left.field == right.field &&
	       left.root_seed == right.root_seed;
}

#endif
