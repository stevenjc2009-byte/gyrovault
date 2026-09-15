#ifndef GV_SAVE_H
#define GV_SAVE_H

#include <stddef.h>
#include <stdint.h>
#include "level.h"

#define SAVE_DIR  "ux0:data/gyrovault"
#define SAVE_PATH "ux0:data/gyrovault/save.dat"
#define SAVE_BLOB_SIZE 16

/* v1 saves had only 2 levels and a 1-byte mask (bit0 = level 1, bit1 = level 2,
 * "Pinball Gauntlet"). On migration to v2, bit0 still maps to index 0; this is
 * the new index the old level 2 now lives at, so old completion isn't lost. */
#ifndef SAVE_V1_LEVEL2_INDEX
#define SAVE_V1_LEVEL2_INDEX 15
#endif

typedef struct {
    uint8_t  unlocked;       /* levels playable, 1..LEVEL_COUNT */
    uint32_t completed_mask; /* bit i set = level i finished at least once */
} SaveData;

/* Fresh progress: level 1 unlocked, nothing completed. */
void save_defaults(SaveData *s);

/* Record finishing level `index`: sets its completed bit, unlocks the next level. */
void save_mark_complete(SaveData *s, int index);

/* 1 if level `index` may be played: unlocked in order (index < unlocked, level 1 always)
 * or already cleared. The second case keeps an old v1 player's cleared level 2 playable
 * at its new slot without opening the levels before it. */
int save_level_open(const SaveData *s, int index);

/* Fixed-size 16-byte blob (format v2), always written by save_serialize:
 *   [0..3]   "GYRV" magic
 *   [4..5]   format version, little-endian = 2
 *   [6]      unlocked
 *   [7]      reserved, must be 0
 *   [8..11]  completed_mask, little-endian
 *   [12..15] CRC32 (crc32_ieee) over bytes 0..11, little-endian
 * serialize returns bytes written (SAVE_BLOB_SIZE) or 0 if cap too small.
 * deserialize returns 0 on success; on any corruption returns -1 and sets defaults.
 * It also accepts a valid v1 blob (old layout: [6] unlocked, [7] uint8 mask,
 * [8..11] zero, CRC over 0..11) and migrates it: v1 mask bit0 -> new bit0, v1
 * mask bit1 -> new bit SAVE_V1_LEVEL2_INDEX. A v1 blob with mask bits above
 * bit1, or unlocked outside 1..2, is rejected. */
size_t save_serialize(const SaveData *s, uint8_t *buf, size_t cap);
int    save_deserialize(SaveData *s, const uint8_t *buf, size_t len);

/* stdio file I/O. load: missing/corrupt file -> defaults, returns -1. write: creates
 * `dir` if needed, writes a temp file then renames over `path`; returns 0 on success. */
int save_load(SaveData *s, const char *path);
int save_write(const SaveData *s, const char *dir, const char *path);

#endif
