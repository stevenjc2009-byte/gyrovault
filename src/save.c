#include "save.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define SAVE_FORMAT_VERSION    4
#define SAVE_FORMAT_VERSION_V3 3
#define SAVE_FORMAT_VERSION_V2 2
#define SAVE_FORMAT_VERSION_V1 1

static uint32_t crc32_ieee(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        int k;
        c ^= *p++;
        for (k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

void save_defaults(SaveData *s)
{
    int i;
    if (!s)
        return;
    s->unlocked = 1;
    s->completed_mask = 0;
    for (i = 0; i < LEVEL_COUNT; i++)
        s->best_cs[i] = SAVE_NO_TIME;
}

void save_mark_complete(SaveData *s, int index)
{
    int unlock;
    if (!s || index < 0 || index >= LEVEL_COUNT)
        return;
    /* index < LEVEL_COUNT <= 64, so the shift never reaches the UB case (1 << 64). */
    s->completed_mask |= (uint64_t)1 << index;
    unlock = index + 2;
    if (unlock > LEVEL_COUNT)
        unlock = LEVEL_COUNT;
    if (s->unlocked < unlock)
        s->unlocked = (uint8_t)unlock;
}

int save_record_time(SaveData *s, int index, uint32_t cs)
{
    if (!s || index < 0 || index >= LEVEL_COUNT || cs == SAVE_NO_TIME)
        return 0;
    if (s->best_cs[index] == SAVE_NO_TIME || cs < s->best_cs[index]) {
        s->best_cs[index] = cs;
        return 1;
    }
    return 0;
}

int save_level_open(const SaveData *s, int index)
{
    if (!s || index < 0 || index >= LEVEL_COUNT)
        return 0;
    if (index == 0 || index < s->unlocked)
        return 1;
    return (int)((s->completed_mask >> index) & (uint64_t)1);
}

/* Mask of bits that are valid for LEVEL_COUNT (avoids the UB of 1 << 64
 * when LEVEL_COUNT is the full 64). */
static uint64_t save_valid_mask(void)
{
    return (LEVEL_COUNT >= 64) ? ~(uint64_t)0 : (((uint64_t)1 << LEVEL_COUNT) - 1);
}

/* Mask of the bits a blob that stamped `levels` levels is allowed to have set. */
static uint64_t save_mask_for(int levels)
{
    return (levels >= 64) ? ~(uint64_t)0 : (((uint64_t)1 << levels) - 1);
}

/* Re-derive the unlocked counter from the completed mask. save_mark_complete clamps
 * unlocked to the LEVEL_COUNT of the build that wrote the save, so a player who had
 * finished the last vault of an older, shorter game comes back with unlocked sitting
 * on that old count -- and every vault added since would be locked with nothing in the
 * game able to open it. Finishing vault i always earns i+2, so take the highest bit
 * that is set and apply the same rule against today's LEVEL_COUNT. Never lowers
 * unlocked, so a save from this same build is unchanged.
 *
 * Deliberately not applied to v1: its migration moves the old level 2 to a slot far up
 * the list on purpose and documents that it must not open everything below it. */
static void save_unlock_from_mask(SaveData *s)
{
    int i;
    for (i = LEVEL_COUNT - 1; i >= 0; i--) {
        if ((s->completed_mask >> i) & (uint64_t)1) {
            int unlock = (i + 2 > LEVEL_COUNT) ? LEVEL_COUNT : i + 2;
            if (s->unlocked < unlock)
                s->unlocked = (uint8_t)unlock;
            return;
        }
    }
}

size_t save_serialize(const SaveData *s, uint8_t *buf, size_t cap)
{
    uint32_t crc;
    int i;
    if (!s || !buf || cap < SAVE_BLOB_SIZE)
        return 0;
    memset(buf, 0, SAVE_BLOB_SIZE);
    buf[0] = 'G'; buf[1] = 'Y'; buf[2] = 'R'; buf[3] = 'V';
    buf[4] = (uint8_t)(SAVE_FORMAT_VERSION & 0xFF);
    buf[5] = (uint8_t)((SAVE_FORMAT_VERSION >> 8) & 0xFF);
    buf[6] = s->unlocked;
    buf[7] = (uint8_t)LEVEL_COUNT; /* how many best-time slots follow */
    buf[8]  = (uint8_t)(s->completed_mask & 0xFF);
    buf[9]  = (uint8_t)((s->completed_mask >> 8) & 0xFF);
    buf[10] = (uint8_t)((s->completed_mask >> 16) & 0xFF);
    buf[11] = (uint8_t)((s->completed_mask >> 24) & 0xFF);
    buf[12] = (uint8_t)((s->completed_mask >> 32) & 0xFF);
    buf[13] = (uint8_t)((s->completed_mask >> 40) & 0xFF);
    buf[14] = (uint8_t)((s->completed_mask >> 48) & 0xFF);
    buf[15] = (uint8_t)((s->completed_mask >> 56) & 0xFF);
    for (i = 0; i < LEVEL_COUNT; i++) {
        uint32_t v = s->best_cs[i];
        size_t off = 16 + (size_t)i * 4;
        buf[off]     = (uint8_t)(v & 0xFF);
        buf[off + 1] = (uint8_t)((v >> 8) & 0xFF);
        buf[off + 2] = (uint8_t)((v >> 16) & 0xFF);
        buf[off + 3] = (uint8_t)((v >> 24) & 0xFF);
    }
    crc = crc32_ieee(buf, SAVE_BLOB_SIZE - 4);
    buf[SAVE_BLOB_SIZE - 4] = (uint8_t)(crc & 0xFF);
    buf[SAVE_BLOB_SIZE - 3] = (uint8_t)((crc >> 8) & 0xFF);
    buf[SAVE_BLOB_SIZE - 2] = (uint8_t)((crc >> 16) & 0xFF);
    buf[SAVE_BLOB_SIZE - 1] = (uint8_t)((crc >> 24) & 0xFF);
    return SAVE_BLOB_SIZE;
}

int save_deserialize(SaveData *s, const uint8_t *buf, size_t len)
{
    uint32_t crc, stored;
    unsigned version;

    if (!s)
        return -1;
    save_defaults(s);
    if (!buf || len < SAVE_BLOB_SIZE_V2)
        return -1;
    if (memcmp(buf, "GYRV", 4) != 0)
        return -1;
    version = (unsigned)buf[4] | ((unsigned)buf[5] << 8);

    if (version == SAVE_FORMAT_VERSION) {
        uint8_t unlocked;
        uint64_t mask;
        int i, levels, shared;
        size_t blob;

        /* The blob's own byte 7 says how long it is, not today's LEVEL_COUNT: a save
         * written by a build with fewer levels has to migrate, not be thrown away. */
        levels = buf[7];
        if (levels < 1 || levels > 64)
            return -1;
        blob = (size_t)SAVE_BLOB_SIZE_FOR(levels);
        if (len != blob)
            return -1;
        stored = (uint32_t)buf[blob - 4] | ((uint32_t)buf[blob - 3] << 8) |
                 ((uint32_t)buf[blob - 2] << 16) | ((uint32_t)buf[blob - 1] << 24);
        crc = crc32_ieee(buf, blob - 4);
        if (crc != stored)
            return -1;
        unlocked = buf[6];
        if (unlocked < 1 || unlocked > levels)
            return -1;
        mask = (uint64_t)buf[8] | ((uint64_t)buf[9] << 8) |
               ((uint64_t)buf[10] << 16) | ((uint64_t)buf[11] << 24) |
               ((uint64_t)buf[12] << 32) | ((uint64_t)buf[13] << 40) |
               ((uint64_t)buf[14] << 48) | ((uint64_t)buf[15] << 56);
        if (mask & ~save_mask_for(levels))
            return -1;
        /* Fold onto this build: a longer save loses the bits and times for levels this
         * build does not have; a shorter one leaves the rest at SAVE_NO_TIME. */
        s->unlocked = (uint8_t)(unlocked > LEVEL_COUNT ? LEVEL_COUNT : unlocked);
        s->completed_mask = mask & save_valid_mask();
        shared = (levels < LEVEL_COUNT) ? levels : LEVEL_COUNT;
        for (i = 0; i < shared; i++) {
            size_t off = 16 + (size_t)i * 4;
            s->best_cs[i] = (uint32_t)buf[off] | ((uint32_t)buf[off + 1] << 8) |
                             ((uint32_t)buf[off + 2] << 16) | ((uint32_t)buf[off + 3] << 24);
        }
        save_unlock_from_mask(s);
        return 0;
    }

    if (version == SAVE_FORMAT_VERSION_V3) {
        uint8_t unlocked;
        uint32_t mask32;
        int i, v3_levels;

        if (len != SAVE_BLOB_SIZE_V3)
            return -1;
        stored = (uint32_t)buf[92] | ((uint32_t)buf[93] << 8) |
                 ((uint32_t)buf[94] << 16) | ((uint32_t)buf[95] << 24);
        crc = crc32_ieee(buf, 92);
        if (crc != stored)
            return -1;
        if (buf[7] != 0) /* reserved must be zero */
            return -1;
        unlocked = buf[6];
        if (unlocked < 1 || unlocked > LEVEL_COUNT)
            return -1;
        mask32 = (uint32_t)buf[8] | ((uint32_t)buf[9] << 8) |
                 ((uint32_t)buf[10] << 16) | ((uint32_t)buf[11] << 24);
        if ((uint64_t)mask32 & ~save_valid_mask())
            return -1;
        s->unlocked = unlocked;
        s->completed_mask = (uint64_t)mask32;
        /* Carry all SAVE_V3_LEVELS best times over unchanged. If LEVEL_COUNT is
         * ever smaller than SAVE_V3_LEVELS (not expected, but not our call to
         * assume), don't write past best_cs. Anything from SAVE_V3_LEVELS up
         * to LEVEL_COUNT-1 is already SAVE_NO_TIME from save_defaults above. */
        v3_levels = (SAVE_V3_LEVELS < LEVEL_COUNT) ? SAVE_V3_LEVELS : LEVEL_COUNT;
        for (i = 0; i < v3_levels; i++) {
            size_t off = 12 + (size_t)i * 4;
            s->best_cs[i] = (uint32_t)buf[off] | ((uint32_t)buf[off + 1] << 8) |
                             ((uint32_t)buf[off + 2] << 16) | ((uint32_t)buf[off + 3] << 24);
        }
        save_unlock_from_mask(s);
        return 0;
    }

    if (version == SAVE_FORMAT_VERSION_V2) {
        uint8_t unlocked;
        uint32_t mask;

        if (len != SAVE_BLOB_SIZE_V2)
            return -1;
        stored = (uint32_t)buf[12] | ((uint32_t)buf[13] << 8) |
                 ((uint32_t)buf[14] << 16) | ((uint32_t)buf[15] << 24);
        crc = crc32_ieee(buf, 12);
        if (crc != stored)
            return -1;
        if (buf[7] != 0) /* reserved must be zero */
            return -1;
        unlocked = buf[6];
        if (unlocked < 1 || unlocked > LEVEL_COUNT)
            return -1;
        mask = (uint32_t)buf[8] | ((uint32_t)buf[9] << 8) |
               ((uint32_t)buf[10] << 16) | ((uint32_t)buf[11] << 24);
        if ((uint64_t)mask & ~save_valid_mask())
            return -1;
        s->unlocked = unlocked;
        s->completed_mask = (uint64_t)mask;
        /* best_cs already SAVE_NO_TIME from save_defaults above */
        save_unlock_from_mask(s);
        return 0;
    }

    if (version == SAVE_FORMAT_VERSION_V1) {
        uint8_t unlocked;
        uint8_t mask;
        uint64_t new_mask;

        if (len != SAVE_BLOB_SIZE_V2)
            return -1;
        stored = (uint32_t)buf[12] | ((uint32_t)buf[13] << 8) |
                 ((uint32_t)buf[14] << 16) | ((uint32_t)buf[15] << 24);
        crc = crc32_ieee(buf, 12);
        if (crc != stored)
            return -1;
        unlocked = buf[6];
        mask = buf[7];

        /* v1's trailing word [8..11] was always zero; nonzero here is corruption. */
        if (buf[8] != 0 || buf[9] != 0 || buf[10] != 0 || buf[11] != 0)
            return -1;
        if (unlocked < 1 || unlocked > 2)
            return -1;
        if (mask & (uint8_t)~0x03u)
            return -1;

        new_mask = 0;
        if (mask & 0x01u)
            new_mask |= 1u;
        if (mask & 0x02u) {
            if (SAVE_V1_LEVEL2_INDEX >= LEVEL_COUNT)
                return -1; /* can't represent the migrated bit at this LEVEL_COUNT */
            new_mask |= (uint64_t)1 << SAVE_V1_LEVEL2_INDEX;
        }

        s->unlocked = (uint8_t)(unlocked > LEVEL_COUNT ? LEVEL_COUNT : unlocked);
        s->completed_mask = new_mask;
        return 0;
    }

    return -1; /* unknown version */
}

/* Read cap: comfortably bigger than any real blob (SAVE_BLOB_SIZE, which is
 * itself derived from LEVEL_COUNT -- 100 bytes today, 260 at LEVEL_COUNT 60)
 * so a valid v1/v2/v3/v4 file always reads in full, while still bounding an
 * oversized/corrupt file. */
#define SAVE_LOAD_READ_CAP (SAVE_BLOB_SIZE + 64)

int save_load(SaveData *s, const char *path)
{
    uint8_t buf[SAVE_LOAD_READ_CAP];
    size_t n;
    FILE *f;

    if (!s)
        return -1;
    save_defaults(s);
    if (!path)
        return -1;
    f = fopen(path, "rb");
    if (!f)
        return -1;
    n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    return save_deserialize(s, buf, n);
}

int save_write(const SaveData *s, const char *dir, const char *path)
{
    uint8_t buf[SAVE_BLOB_SIZE];
    char tmp[512];
    size_t plen;
    FILE *f;
    int ok;

    if (!s || !dir || !path)
        return -1;
    if (save_serialize(s, buf, sizeof(buf)) != SAVE_BLOB_SIZE)
        return -1;
    plen = strlen(path);
    if (plen + 5 > sizeof(tmp))
        return -1;
    memcpy(tmp, path, plen);
    memcpy(tmp + plen, ".tmp", 5);

    /* Result ignored: an already-existing dir is fine, and a real failure makes fopen fail. */
    (void)mkdir(dir, 0777);

    f = fopen(tmp, "wb");
    if (!f)
        return -1;
    ok = fwrite(buf, 1, sizeof(buf), f) == sizeof(buf);
    if (fflush(f) != 0)
        ok = 0;
    if (fclose(f) != 0)
        ok = 0;
    if (!ok) {
        remove(tmp);
        return -1;
    }
    /* Try the atomic replace first. Only if that fails -- the Vita's rename refuses an
     * existing destination -- fall back to clearing the way and retrying. Removing first
     * unconditionally, as this used to, opened a window where a crash or a pulled battery
     * left no save at all: the old file already deleted, the new one still called .tmp. */
    if (rename(tmp, path) != 0) {
        remove(path);
        if (rename(tmp, path) != 0) {
            remove(tmp);
            return -1;
        }
    }
    return 0;
}
