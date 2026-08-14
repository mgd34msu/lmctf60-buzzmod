/* sg_crc32.h -- one canonical reflected IEEE CRC32 implementation. */
#ifndef SG_CRC32_H
#define SG_CRC32_H

#include <stddef.h>
#include <stdint.h>

/* Streaming form.  Feed encoded fragments in wire order between Init/Final;
 * callers never need to assemble a complete rune payload in memory. */
uint32_t SG_CRC32Init(void);
int SG_CRC32Update(uint32_t *state, const void *block, size_t size);
uint32_t SG_CRC32Final(uint32_t state);

/* Convenience form with the same zlib-compatible result.  Both checked forms
 * reject NULL output/state and NULL input with nonzero size. */
int SG_CRC32Buffer(const void *block, size_t size, uint32_t *out);

#endif
