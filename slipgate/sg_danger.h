/* sg_danger.h -- graph-bound danger model and explicit-LE payload. */
#ifndef SG_DANGER_H
#define SG_DANGER_H

#include <stddef.h>
#include <stdint.h>

/* sg_local.h supplies rune_t and qboolean before this internal header. */
size_t		Danger_PayloadBytes(const rune_t *r);
qboolean	Danger_DecodeCandidate(const rune_t *r,
			const unsigned char *payload, size_t payload_size,
			int *red_out, int *blue_out, size_t plane_capacity);
qboolean	Danger_Publish(const rune_t *r, const int *red,
			const int *blue, size_t plane_count,
			qboolean persistence_enabled);

void		Danger_ResetLevel(void);
void		Danger_Learn(int team, int seed);
void		Danger_Decay(void);
const int	*Danger_Field(int team);

qboolean	Danger_IsActive(void);
qboolean	Danger_PersistenceEnabled(void);
qboolean	Danger_IsDirty(void);
uint64_t	Danger_Revision(void);
qboolean	Danger_CheckpointPending(void);
qboolean	Danger_CapturePayload(unsigned char *payload,
			size_t payload_capacity, size_t *payload_size_out,
			uint64_t *revision_out);
qboolean	Danger_MarkCommitted(uint64_t revision);

#endif /* SG_DANGER_H */
