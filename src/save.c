#include "save.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define SAVE_FORMAT_VERSION    2
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
    if (!s)
        return;
    s->unlocked = 1;
    s->completed_mask = 0;
}

void save_mark_complete(SaveData *s, int index)
{
    int unlock;
    if (!s || index < 0 || index >= LEVEL_COUNT)
        return;
    /* index < LEVEL_COUNT <= 32, so the shift never reaches the UB case (1u << 32). */
    s->completed_mask |= (uint32_t)(1u << index);
    unlock = index + 2;
    if (unlock > LEVEL_COUNT)
        unlock = LEVEL_COUNT;
    if (s->unlocked < unlock)
        s->unlocked = (uint8_t)unlock;
}

int save_level_open(const SaveData *s, int index)
{
    if (!s || index < 0 || index >= LEVEL_COUNT)
        return 0;
    if (index == 0 || index < s->unlocked)
        return 1;
    return (int)((s->completed_mask >> index) & 1u);
}

/* Mask of bits that are valid for LEVEL_COUNT (avoids the UB of 1u << 32
 * when LEVEL_COUNT is the full 32). */
static uint32_t save_valid_mask(void)
{
    return (LEVEL_COUNT >= 32) ? 0xFFFFFFFFu : ((1u << LEVEL_COUNT) - 1u);
}

size_t save_serialize(const SaveData *s, uint8_t *buf, size_t cap)
{
    uint32_t crc;
    if (!s || !buf || cap < SAVE_BLOB_SIZE)
        return 0;
    memset(buf, 0, SAVE_BLOB_SIZE);
    buf[0] = 'G'; buf[1] = 'Y'; buf[2] = 'R'; buf[3] = 'V';
    buf[4] = (uint8_t)(SAVE_FORMAT_VERSION & 0xFF);
    buf[5] = (uint8_t)((SAVE_FORMAT_VERSION >> 8) & 0xFF);
    buf[6] = s->unlocked;
    buf[7] = 0; /* reserved */
    buf[8]  = (uint8_t)(s->completed_mask & 0xFF);
    buf[9]  = (uint8_t)((s->completed_mask >> 8) & 0xFF);
    buf[10] = (uint8_t)((s->completed_mask >> 16) & 0xFF);
    buf[11] = (uint8_t)((s->completed_mask >> 24) & 0xFF);
    crc = crc32_ieee(buf, 12);
    buf[12] = (uint8_t)(crc & 0xFF);
    buf[13] = (uint8_t)((crc >> 8) & 0xFF);
    buf[14] = (uint8_t)((crc >> 16) & 0xFF);
    buf[15] = (uint8_t)((crc >> 24) & 0xFF);
    return SAVE_BLOB_SIZE;
}

int save_deserialize(SaveData *s, const uint8_t *buf, size_t len)
{
    uint32_t crc, stored;
    unsigned version;

    if (!s)
        return -1;
    save_defaults(s);
    if (!buf || len < SAVE_BLOB_SIZE)
        return -1;
    if (memcmp(buf, "GYRV", 4) != 0)
        return -1;
    version = (unsigned)buf[4] | ((unsigned)buf[5] << 8);
    stored = (uint32_t)buf[12] | ((uint32_t)buf[13] << 8) |
             ((uint32_t)buf[14] << 16) | ((uint32_t)buf[15] << 24);
    crc = crc32_ieee(buf, 12);
    if (crc != stored)
        return -1;

    if (version == SAVE_FORMAT_VERSION) {
        uint8_t unlocked = buf[6];
        uint32_t mask;

        if (buf[7] != 0) /* reserved must be zero */
            return -1;
        if (unlocked < 1 || unlocked > LEVEL_COUNT)
            return -1;
        mask = (uint32_t)buf[8] | ((uint32_t)buf[9] << 8) |
               ((uint32_t)buf[10] << 16) | ((uint32_t)buf[11] << 24);
        if (mask & ~save_valid_mask())
            return -1;
        s->unlocked = unlocked;
        s->completed_mask = mask;
        return 0;
    }

    if (version == SAVE_FORMAT_VERSION_V1) {
        uint8_t unlocked = buf[6];
        uint8_t mask = buf[7];
        uint32_t new_mask;

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
            new_mask |= (uint32_t)(1u << SAVE_V1_LEVEL2_INDEX);
        }

        s->unlocked = (uint8_t)(unlocked > LEVEL_COUNT ? LEVEL_COUNT : unlocked);
        s->completed_mask = new_mask;
        return 0;
    }

    return -1; /* unknown version */
}

int save_load(SaveData *s, const char *path)
{
    uint8_t buf[SAVE_BLOB_SIZE + 1];
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
    if (n != SAVE_BLOB_SIZE) /* short or oversized file = corrupt */
        return -1;
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
    /* The Vita's rename fails if the destination exists. */
    remove(path);
    if (rename(tmp, path) != 0) {
        remove(tmp);
        return -1;
    }
    return 0;
}
