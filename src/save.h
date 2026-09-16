#ifndef GV_SAVE_H
#define GV_SAVE_H

#include <stddef.h>
#include <stdint.h>
#include "level.h"

#define SAVE_DIR  "ux0:data/gyrovault"
#define SAVE_PATH "ux0:data/gyrovault/save.dat"

/* Size of a v4 blob holding `n` best times: a 16-byte header (magic, version,
 * unlocked, level count, 64-bit mask) + one uint32 best time per level + a
 * trailing 4-byte CRC32. At 20 levels this is 100 bytes; at 60, 260. */
#define SAVE_BLOB_SIZE_FOR(n) (16 + (n) * 4 + 4)

/* What this build writes. Derived from LEVEL_COUNT rather than a literal, so it
 * grows with the level list -- but a blob on disk is NEVER measured against it,
 * only against the count stamped in the blob's own header (byte 7). Deriving the
 * expected length from today's LEVEL_COUNT is what would wipe every save the
 * moment a release adds levels. */
#define SAVE_BLOB_SIZE SAVE_BLOB_SIZE_FOR(LEVEL_COUNT)

/* Size of the v1 and v2 blobs (both 16 bytes); kept as a named constant since
 * save_deserialize still has to recognize and migrate them. */
#define SAVE_BLOB_SIZE_V2 16

/* v3 was written back when the game had a permanently-fixed 20 levels. Its
 * 96-byte, 20-best-time layout never changes shape no matter what LEVEL_COUNT
 * is today, so these are independent named constants -- NOT derived from
 * LEVEL_COUNT the way SAVE_BLOB_SIZE is. */
#define SAVE_V3_LEVELS    20
#define SAVE_BLOB_SIZE_V3 96

/* v1 saves had only 2 levels and a 1-byte mask (bit0 = level 1, bit1 = level 2,
 * "Pinball Gauntlet"). On migration to v2, bit0 still maps to index 0; this is
 * the new index the old level 2 now lives at, so old completion isn't lost. */
#ifndef SAVE_V1_LEVEL2_INDEX
#define SAVE_V1_LEVEL2_INDEX 15
#endif

_Static_assert(SAVE_V1_LEVEL2_INDEX < LEVEL_COUNT,
               "SAVE_V1_LEVEL2_INDEX must address a real level slot");
_Static_assert(LEVEL_COUNT <= 64,
               "completed_mask is a uint64_t and can't address more than 64 levels");

/* Sentinel meaning "no time recorded for this level". Note: this intentionally
 * has the same numeric value as TIMER_NONE in the timer module, but save.h
 * must not depend on that header, so it is defined independently here. */
#define SAVE_NO_TIME 0xFFFFFFFFu

typedef struct {
    uint8_t  unlocked;       /* levels playable, 1..LEVEL_COUNT */
    uint64_t completed_mask; /* bit i set = level i finished at least once */
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

/* Blob (format v4), always written by save_serialize:
 *   [0..3]   "GYRV" magic
 *   [4..5]   format version, little-endian = 4
 *   [6]      unlocked
 *   [7]      level count: how many best-time slots follow, 1..64
 *   [8..15]  completed_mask, uint64 little-endian
 *   [16..]   best_cs[level count], uint32 little-endian each
 *   [last 4] CRC32 (crc32_ieee) over every byte before it, little-endian
 * The blob is self-describing: byte 7 says how long it is, so a save written by
 * a build with a different LEVEL_COUNT is migrated rather than rejected. Byte 7
 * was a must-be-zero reserved field in the unreleased first draft of v4; the
 * version was not bumped because no v4 save has ever left this machine (2.0.2
 * and everything before it wrote v3), so zero there now simply reads as a
 * malformed length and is rejected.
 * serialize returns bytes written (SAVE_BLOB_SIZE) or 0 if cap too small.
 * deserialize returns 0 on success; on any corruption returns -1 and sets defaults.
 *
 * Across a level-count change: best times carry over for the levels both builds
 * have, mask bits for levels this build does not have are dropped, and the
 * unlocked counter is re-derived from the mask -- save_mark_complete clamps it to
 * the LEVEL_COUNT of the build that wrote it, so a player who had finished every
 * vault would otherwise find the new ones locked with no way in.
 *
 * Migration: deserialize also accepts three older, permanently fixed-size
 * formats and upgrades them into the current SaveData in memory (the file on
 * disk is only rewritten as v4 the next time save_write runs):
 *   - v3 (SAVE_BLOB_SIZE_V3 = 96 bytes -- fixed regardless of today's
 *     LEVEL_COUNT, because it was written back when the game had exactly
 *     SAVE_V3_LEVELS (20) levels): [4..5] version = 3, [6] unlocked,
 *     [7] reserved = 0, [8..11] completed_mask, 32-bit little-endian,
 *     [12..91] best_cs[SAVE_V3_LEVELS], uint32 little-endian each,
 *     [92..95] CRC32 over bytes 0..91. The 32-bit mask is zero-extended into
 *     the new 64-bit mask, and all SAVE_V3_LEVELS best times carry over into
 *     best_cs[0..SAVE_V3_LEVELS-1] unchanged; any level from SAVE_V3_LEVELS up
 *     to LEVEL_COUNT-1 (a level the v3 player never saw) comes back as
 *     SAVE_NO_TIME. unlocked/mask are validated exactly as for v4 -- against
 *     today's LEVEL_COUNT, not SAVE_V3_LEVELS.
 *   - v2 (old fixed layout, still 16 bytes): [4..5] version = 2, [6] unlocked,
 *     [7] reserved = 0, [8..11] completed_mask LE, [12..15] CRC32 over bytes 0..11.
 *     unlocked/mask validated exactly as for v4. Neither v2 nor v1 ever stored
 *     times, so migrating either always yields SAVE_NO_TIME for every level.
 *   - v1 (oldest layout, 2-level game): [6] unlocked (1..2), [7] uint8 mask
 *     (bits above bit1 rejected), [8..11] always zero, CRC32 over bytes 0..11.
 *     Migrated: v1 mask bit0 -> new bit0, v1 mask bit1 -> new bit SAVE_V1_LEVEL2_INDEX.
 *
 * A blob is rejected (-1, defaults set) for: bad magic, bad CRC, a length that
 * doesn't match the size required by its own stamped version (for v4, the size
 * its own byte 7 asks for; SAVE_BLOB_SIZE_V3 for v3, SAVE_BLOB_SIZE_V2 for
 * v1/v2), an unknown version, a v4 level count outside 1..64, unlocked outside
 * 1..the blob's own level count (1..LEVEL_COUNT for v2/v3, 1..2 for v1), a
 * nonzero reserved byte for v2/v3, or completed_mask bits at or above the
 * blob's own level count (LEVEL_COUNT for v2/v3, bit1 for v1). */
size_t save_serialize(const SaveData *s, uint8_t *buf, size_t cap);
int    save_deserialize(SaveData *s, const uint8_t *buf, size_t len);

/* stdio file I/O. load: missing/corrupt file -> defaults, returns -1. write: creates
 * `dir` if needed, writes a temp file then renames over `path`; returns 0 on success. */
int save_load(SaveData *s, const char *path);
int save_write(const SaveData *s, const char *dir, const char *path);

#endif
