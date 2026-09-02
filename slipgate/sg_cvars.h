/* sg_cvars.h -- the bot's cvars: registered once, read through sg_cv. */
#ifndef SG_CVARS_H
#define SG_CVARS_H

#define SG_CVAR_LIST(X) \
	X(debug, "sg_debug", "0") \
	X(persona, "sg_persona", "1") \
	X(sessiondb, "sg_sessiondb", "1") \
	X(skill, "sg_skill", "3") \
	X(turnrate, "sg_turnrate", "720")

typedef struct sg_cvars_s {
#define X(field, name, value) cvar_t *field;
	SG_CVAR_LIST(X)
#undef X
} sg_cvars_t;

extern sg_cvars_t sg_cv;

void SG_CvarsInit(void);   /* idempotent; call before any sg_cv use */

#endif /* SG_CVARS_H */
