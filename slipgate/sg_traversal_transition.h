#ifndef SG_TRAVERSAL_TRANSITION_H
#define SG_TRAVERSAL_TRANSITION_H

qboolean SG_TraversalControllerPhysical(const sg_bot_t *bot, int action);
void SG_StagedTraversalCancel(sg_bot_t *bot, int action);
void SG_CarryStartRetireSupersededRoute(sg_bot_t *bot, qboolean carry_started);

#endif
