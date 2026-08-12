/*
 * sg_weights.h -- the fitted rows' public face.
 */
#ifndef SG_WEIGHTS_H
#define SG_WEIGHTS_H

void					Weights_Load(void);
const sg_weights_t		*Weights_Row(int role);

/* SG_ROLES display names, indexed by sg_role_t; SG_ROLES-sized */
extern const char *sg_role_names[];

#endif
