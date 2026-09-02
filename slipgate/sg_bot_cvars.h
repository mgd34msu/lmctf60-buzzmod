/* sg_bot_cvars.h -- the bot's console variables, registered once and read
 * through sg_cv. */
#ifndef SG_BOT_CVARS_H
#define SG_BOT_CVARS_H

#define SG_CVAR_LIST(X) \
	X(debug, "sg_debug", "0")           /* SGBOT, SGFIGHT, SGPOST lines */ \
	X(persona, "sg_persona", "1")       /* named personas, else plain names */ \
	X(sessiondb, "sg_sessiondb", "1")   /* the stats database records bots */ \
	X(skill, "sg_skill", "3")           /* 0..4: aim error and reaction */ \
	X(turnrate, "sg_turnrate", "720")   /* degrees a second the view slews */

typedef struct sg_cvars_s
{
#define X(field, name, value) cvar_t *field;
	SG_CVAR_LIST(X)
#undef X
} sg_cvars_t;

extern sg_cvars_t sg_cv;

/* Registers every variable; safe to call again. */
void SG_CvarsInit(void);

#endif /* SG_BOT_CVARS_H */
