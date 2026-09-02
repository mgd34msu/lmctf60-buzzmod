/* sg_bot_persona.h -- who each bot is.
 *
 * A persona is a name and a few leanings that shade behavior without
 * changing what is possible: how steady the aim is against the skill
 * setting, how readily it starts a fight, how much it likes the rope, how
 * long it holds a post, how much it says.  Names are unique on a server:
 * a name a human or another bot wears is not given out.  Personas are
 * bound to client slots and looked up by entity. */
#ifndef SG_BOT_PERSONA_H
#define SG_BOT_PERSONA_H

typedef struct sg_bot_persona_s
{
	const char *name;
	int aim;                  /* -2..+2 grades against sg_skill */
	float aggression;         /* 0.5..1.5: readiness to engage */
	float rope;               /* 0.5..1.5: appetite for hook routes */
	float patience;           /* 0..1: holds a post this readily */
	float talk;               /* 0.5..1.5: how much it says */
} sg_bot_persona_t;

int SG_BotPersonaCount(void);
const sg_bot_persona_t *SG_BotPersonaAt(int index);

/* The first persona from `from` onward whose name is not in occupied
 * (a bit per persona), wrapping; -1 when all are taken. */
int SG_BotPersonaPick(unsigned occupied, int from);

/* Bind a persona to a client and forget it; the persona of an entity. */
void SG_BotPersonaBind(edict_t *ent, int index);
void SG_BotPersonaUnbind(edict_t *ent);
const sg_bot_persona_t *SG_BotPersona(const edict_t *ent);

#endif /* SG_BOT_PERSONA_H */
