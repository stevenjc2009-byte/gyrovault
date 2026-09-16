#ifndef GV_SAVE_H
#define GV_SAVE_H

#include <stddef.h>
#include <stdint.h>
#include "level.h"

#define SAVE_DIR  "ux0:data/gyrovault"
#define SAVE_PATH "ux0:data/gyrovault/save.dat"
#define SAVE_BLOB_SIZE 96

/* Size of the v1 and v2 blobs (both 16 bytes); kept as a named constant since
 * save_deserialize still has to recognize and migrate them. */
#define SAVE_BLOB_SIZE_V2 16

/* v1 saves had only 2 levels and a 1-byte mask (bit0 = level 1, bit1 = level 2,
 * "Pinball Gauntlet"). On migration to v2, bit0 still maps to index 0; this is
 * the new index the old level 2 now lives at, so old completion isn't lost. */
#ifndef SAVE_V1_LEVEL2_INDEX
#define SAVE_V1_LEVEL2_INDEX 15
#endif

_Static_assert(SAVE_V1_LEVEL2_INDEX < LEVEL_COUNT,
               "SAVE_V1_LEVEL2_INDEX must address a real level slot");
_Static_assert(LEVEL_COUNT <= 32,
               "completed_mask is 32 bits wide and can't address more levels");

/* Sentinel meaning "no time recorded for this level". Note: this intentionally
 * has the same numeric value as TIMER_NONE in the timer module, but save.h
 * must not depend on that header, so it is defined independently here. */
#define SAVE_NO_TIME 0xFFFFFFFFu

typedef struct {
    uint8_t  unlocked;       /* levels playable, 1..LEVEL_COUNT */
    uint32_t completed_mask; /* bit i set = level i finished at least once */
    uint32_t best_cs[LEVEL_COUNT]; /* best clear time per level, centiseconds;
                                     * SAVE_NO_TIME = never cleared */
} SaveData;

/* Fresh progress: level 1 unlocked, nothing completed, no times recorded. */
void save_defaults(SaveData *s);

/* Record finishing level `index`: sets its completed bit, unlocks the next level. */
void save_mark_complete(SaveData *s, int index);

/* Record a clear time for level `index`. Returns 1 if it is a new best, which includes the
 * first time ever recorded for that level; 0 otherwise or for a bad index/NULL/SAVE_NO_TIME. */
int save_record_time(SaveData *s, int index, uint32_t cs);

/* 1 if level `index` may be played: unlocked in order (index < unlocked, level 1 always)
 * or already cleared. The second case keeps an old v1 player's cleared level 2 playable
 * at its new slot without opening the levels before it. */
int save_level_open(const SaveData *s, int index);

/* Fixed-size 96-byte blob (format v3), always written by save_serialize:
 *   [0..3]   "GYRV" magic
 *   [4..5]   format version, little-endian = 3
 *   [6]      unlocked
 *   [7]      reserved, must be 0
 *   [8..11]  completed_mask, little-endian
 *   [12..91] best_cs[LEVEL_COUNT], uint32 little-endian each (LEVEL_COUNT == 20 -> 80 bytes)
 *   [92..95] CRC32 (crc32_ieee) over bytes 0..91, little-endian
 * serialize returns bytes written (SAVE_BLOB_SIZE) or 0 if cap too small.
 * deserialize returns 0 on success; on any corruption returns -1 and sets defaults.
 *
 * Migration: deserialize also accepts the older 16-byte (SAVE_BLOB_SIZE_V2) blobs,
 * and always yields SAVE_NO_TIME for every level in that case since neither old
 * format stored times:
 *   - v2 (old fixed layout, still 16 bytes): [4..5] version = 2, [6] unlocked,
 *     [7] reserved = 0, [8..11] completed_mask LE, [12..15] CRC32 over bytes 0..11.
 *     unlocked/mask validated exactly as for v3.
 *   - v1 (oldest layout, 2-level game): [6] unlocked (1..2), [7] uint8 mask
 *     (bits above bit1 rejected), [8..11] always zero, CRC32 over bytes 0..11.
 *     Migrated: v1 mask bit0 -> new bit0, v1 mask bit1 -> new bit SAVE_V1_LEVEL2_INDEX.
 *
 * A blob is rejected (-1, defaults set) for: bad magic, bad CRC, a length that
 * doesn't match the size required by its own stamped version (96 for v3, 16 for
 * v1/v2), an unknown version, unlocked outside 1..LEVEL_COUNT (1..2 for v1),
 * reserved byte != 0, or completed_mask bits at or above LEVEL_COUNT set (v1
 * mask bits above bit1 set). */
size_t save_serialize(const SaveData *s, uint8_t *buf, size_t cap);
int    save_deserialize(SaveData *s, const uint8_t *buf, size_t len);

/* stdio file I/O. load: missing/corrupt file -> defaults, returns -1. write: creates
 * `dir` if needed, writes a temp file then renames over `path`; returns 0 on success. */
int save_load(SaveData *s, const char *path);
int save_write(const SaveData *s, const char *dir, const char *path);

#endif
