/* Era-4 CRC-32 (IEEE 802.3, reflected, as zlib's crc32): identity and
 * artifact checksums.  Table built once on first use. */
#ifndef SG_RUNE_CRC_H
#define SG_RUNE_CRC_H

#include <stddef.h>
#include <stdint.h>

uint32_t SG_RuneCrc32(const uint8_t *bytes, size_t count);
/* Continue a running CRC: start with 0, feed pieces, the result is the
 * CRC of the concatenation. */
uint32_t SG_RuneCrc32Continue(uint32_t crc, const uint8_t *bytes, size_t count);

#endif /* SG_RUNE_CRC_H */
