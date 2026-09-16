#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "save.h"
#include "test.h"

static int is_defaults(const SaveData *s)
{
    int i;
    if (s->unlocked != 1 || s->completed_mask != 0)
        return 0;
    for (i = 0; i < LEVEL_COUNT; i++)
        if (s->best_cs[i] != SAVE_NO_TIME)
            return 0;
    return 1;
}

/* Standard CRC32-IEEE (same polynomial as save.c's crc32_ieee, written
 * independently here so tests can hand-forge fixtures -- including at
 * LEVEL_COUNT values other than 20, where a byte-literal fixture can't be
 * pre-computed offline -- without depending on save.c's internal, unexported
 * function). */
static uint32_t test_crc32(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    size_t idx;
    for (idx = 0; idx < n; idx++) {
        int k;
        c ^= p[idx];
        for (k = 0; k < 8; k++)
            c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}

int main(void)
{
    SaveData s, r;
    uint8_t buf[SAVE_BLOB_SIZE], bad[SAVE_BLOB_SIZE];
    size_t i;

#if LEVEL_COUNT == 20
    /* Defaults blob (v4) must match an independent CRC32 (python zlib.crc32 of the
     * 96-byte header+mask+20xSAVE_NO_TIME prefix = 0x0c524c5d). Only meaningful at
     * the LEVEL_COUNT this literal was computed for; the generic checks below cover
     * correctness at any LEVEL_COUNT. */
    {
        static const uint8_t expect[SAVE_BLOB_SIZE] = {
            'G', 'Y', 'R', 'V', 4, 0, 1, 20, 0, 0, 0, 0, 0, 0, 0, 0, /* byte 7 = level count */
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0x5d, 0x4c, 0x52, 0x0c};
        save_defaults(&s);
        CHECK(is_defaults(&s));
        CHECK(save_serialize(&s, buf, sizeof(buf)) == SAVE_BLOB_SIZE);
        CHECK(memcmp(buf, expect, SAVE_BLOB_SIZE) == 0);
    }
#endif

    /* Roundtrip (v4), including best times. */
    save_defaults(&s);
    s.unlocked = 2;
    s.completed_mask = 1;
    for (i = 0; i < (size_t)LEVEL_COUNT; i++)
        s.best_cs[i] = (i % 3 == 0) ? SAVE_NO_TIME : (uint32_t)(1000 + i);
    CHECK(save_serialize(&s, buf, sizeof(buf)) == SAVE_BLOB_SIZE);
    r.unlocked = 99;
    r.completed_mask = 99;
    for (i = 0; i < (size_t)LEVEL_COUNT; i++)
        r.best_cs[i] = 0;
    CHECK(save_deserialize(&r, buf, sizeof(buf)) == 0);
    CHECK(r.unlocked == 2 && r.completed_mask == 1);
    {
        int ok = 1;
        for (i = 0; i < (size_t)LEVEL_COUNT; i++)
            if (r.best_cs[i] != s.best_cs[i])
                ok = 0;
        CHECK(ok);
    }

    /* Serialize cap too small. */
    CHECK(save_serialize(&s, buf, SAVE_BLOB_SIZE - 1) == 0);

    /* Every single flipped byte (including the best-times region), retargeted
     * at the v4 blob size, is rejected and yields defaults. */
    {
        int all_rejected = 1;
        for (i = 0; i < SAVE_BLOB_SIZE; i++) {
            memcpy(bad, buf, sizeof(bad));
            bad[i] ^= 0x40;
            r.unlocked = 2;
            r.completed_mask = 1;
            if (save_deserialize(&r, bad, sizeof(bad)) != -1 || !is_defaults(&r))
                all_rejected = 0;
        }
        CHECK(all_rejected);
    }

    /* CRC corruption of a time byte specifically (byte 16 = low byte of best_cs[0],
     * now that the header is 16 bytes with the 64-bit mask). */
    {
        memcpy(bad, buf, sizeof(bad));
        bad[16] ^= 0x01;
        r.unlocked = 9;
        CHECK(save_deserialize(&r, bad, sizeof(bad)) == -1);
        CHECK(is_defaults(&r));
    }

    /* Bad magic (with an otherwise matching CRC is impossible to hand-build, so plain). */
    memcpy(bad, buf, sizeof(bad));
    bad[0] = 'X';
    CHECK(save_deserialize(&r, bad, sizeof(bad)) == -1);
    CHECK(is_defaults(&r));

    /* Truncated / empty / NULL. */
    r.unlocked = 2;
    CHECK(save_deserialize(&r, buf, SAVE_BLOB_SIZE - 1) == -1);
    CHECK(is_defaults(&r));
    CHECK(save_deserialize(&r, buf, 0) == -1);
    CHECK(save_deserialize(&r, NULL, SAVE_BLOB_SIZE) == -1);

    /* Wrong length for the version stamped in the blob: one byte short of the
     * v4 size, and one byte over the v2 size, are both rejected. */
    r.unlocked = 9;
    CHECK(save_deserialize(&r, buf, SAVE_BLOB_SIZE - 1) == -1);
    CHECK(is_defaults(&r));
    r.unlocked = 9;
    CHECK(save_deserialize(&r, buf, SAVE_BLOB_SIZE_V2 + 1) == -1);
    CHECK(is_defaults(&r));

    /* Valid CRC but out-of-range fields (serialize writes whatever it's given). */
    save_defaults(&s);
    s.unlocked = 0;
    save_serialize(&s, bad, sizeof(bad));
    CHECK(save_deserialize(&r, bad, sizeof(bad)) == -1);
    s.unlocked = LEVEL_COUNT + 1;
    save_serialize(&s, bad, sizeof(bad));
    CHECK(save_deserialize(&r, bad, sizeof(bad)) == -1);
#if LEVEL_COUNT < 64
    s.unlocked = 1;
    s.completed_mask = (uint64_t)1 << LEVEL_COUNT; /* bit LEVEL_COUNT: out of range */
    save_serialize(&s, bad, sizeof(bad));
    CHECK(save_deserialize(&r, bad, sizeof(bad)) == -1);
    CHECK(is_defaults(&r));
#endif
    s.unlocked = LEVEL_COUNT;
    s.completed_mask = (LEVEL_COUNT >= 64) ? ~(uint64_t)0 : (((uint64_t)1 << LEVEL_COUNT) - 1);
    save_serialize(&s, bad, sizeof(bad));
    CHECK(save_deserialize(&r, bad, sizeof(bad)) == 0);
    CHECK(r.unlocked == LEVEL_COUNT && r.completed_mask == s.completed_mask);

    /* v4 blob with the reserved byte (offset 7) nonzero but everything else --
     * including the CRC -- otherwise valid: must be rejected specifically by
     * the reserved-byte check, not merely by a length or CRC mismatch. Built
     * at runtime (not as a byte literal) so this is correct at any
     * LEVEL_COUNT, not just 20. */
    {
        uint8_t rbuf[SAVE_BLOB_SIZE];
        uint32_t crc;
        save_defaults(&s);
        CHECK(save_serialize(&s, rbuf, sizeof(rbuf)) == SAVE_BLOB_SIZE);
        rbuf[7] = 1; /* reserved, must be 0 */
        crc = test_crc32(rbuf, SAVE_BLOB_SIZE - 4);
        rbuf[SAVE_BLOB_SIZE - 4] = (uint8_t)(crc & 0xFF);
        rbuf[SAVE_BLOB_SIZE - 3] = (uint8_t)((crc >> 8) & 0xFF);
        rbuf[SAVE_BLOB_SIZE - 2] = (uint8_t)((crc >> 16) & 0xFF);
        rbuf[SAVE_BLOB_SIZE - 1] = (uint8_t)((crc >> 24) & 0xFF);
        r.unlocked = 5;
        CHECK(save_deserialize(&r, rbuf, sizeof(rbuf)) == -1);
        CHECK(is_defaults(&r));
    }

    /* v2 reserved byte (offset 7) nonzero is rejected (unlocked=1, mask=0;
     * python zlib.crc32 = 0x346de850). */
    {
        static const uint8_t reserved_nonzero[SAVE_BLOB_SIZE_V2] = {'G', 'Y', 'R', 'V', 2, 0, 1, 1,
                                                                  0, 0, 0, 0, 0x50, 0xe8, 0x6d, 0x34};
        r.unlocked = 2;
        CHECK(save_deserialize(&r, reserved_nonzero, SAVE_BLOB_SIZE_V2) == -1);
        CHECK(is_defaults(&r));
    }

    /* Hand-built v2 16-byte blob (unlocked=2, mask=3; python zlib.crc32 = 0x9d2c1ca0)
     * loads: completion kept, best times all come back as SAVE_NO_TIME. */
    {
        static const uint8_t v2_blob[SAVE_BLOB_SIZE_V2] = {'G', 'Y', 'R', 'V', 2, 0, 2, 0,
                                                             3, 0, 0, 0, 0xa0, 0x1c, 0x2c, 0x9d};
        r.unlocked = 0;
        r.completed_mask = 0;
        for (i = 0; i < (size_t)LEVEL_COUNT; i++)
            r.best_cs[i] = 0;
        CHECK(save_deserialize(&r, v2_blob, SAVE_BLOB_SIZE_V2) == 0);
        /* unlocked comes back as 3, not the stored 2: the mask says vault 2 was
         * finished, and finishing vault i earns vault i+2. */
        CHECK(r.unlocked == 3 && r.completed_mask == 3);
        {
            int ok = 1;
            for (i = 0; i < (size_t)LEVEL_COUNT; i++)
                if (r.best_cs[i] != SAVE_NO_TIME)
                    ok = 0;
            CHECK(ok);
        }
        /* Also rejected at the wrong length for a v2 blob (v4-sized). */
        r.unlocked = 9;
        CHECK(save_deserialize(&r, v2_blob, SAVE_BLOB_SIZE) == -1);
        CHECK(is_defaults(&r));
    }

    /* Real on-device v1 blob (unlocked=2, mask=3) loads and migrates:
     * bit0 -> bit0, bit1 -> bit SAVE_V1_LEVEL2_INDEX. Best times come back unset. */
    {
        static const uint8_t v1_blob[SAVE_BLOB_SIZE_V2] = {0x47, 0x59, 0x52, 0x56, 0x01, 0x00,
                                                          0x02, 0x03, 0x00, 0x00, 0x00, 0x00,
                                                          0x7d, 0xce, 0xb6, 0x46};
        r.unlocked = 0;
        r.completed_mask = 0;
        CHECK(save_deserialize(&r, v1_blob, SAVE_BLOB_SIZE_V2) == 0);
        CHECK(r.unlocked == 2);
        CHECK(r.completed_mask == ((uint64_t)1 | ((uint64_t)1 << SAVE_V1_LEVEL2_INDEX)));
        {
            int ok = 1;
            for (i = 0; i < (size_t)LEVEL_COUNT; i++)
                if (r.best_cs[i] != SAVE_NO_TIME)
                    ok = 0;
            CHECK(ok);
        }
    }

    /* v1 blob with a mask bit above bit1 is rejected (unlocked=1, mask=0x04;
     * python zlib.crc32 = 0x720260c3). */
    {
        static const uint8_t v1_bad_mask[SAVE_BLOB_SIZE_V2] = {'G', 'Y', 'R', 'V', 1, 0, 1, 0x04,
                                                              0, 0, 0, 0, 0xc3, 0x60, 0x02, 0x72};
        r.unlocked = 2;
        CHECK(save_deserialize(&r, v1_bad_mask, SAVE_BLOB_SIZE_V2) == -1);
        CHECK(is_defaults(&r));
    }

    /* v3 fixture, hand-built by hand (not via save_serialize, which now always
     * writes v4): unlocked=10, 32-bit mask=0xA5, and 20 known, distinct best
     * times. CRC32 (python zlib.crc32 over bytes 0..91) = 0xed1f7706. This is
     * the migration that matters most -- deserialize must recover every one
     * of the 20 v3 best times unchanged, zero-extend the mask to 64 bits, and
     * for a LEVEL_COUNT grown past SAVE_V3_LEVELS (20), read SAVE_NO_TIME for
     * every level the v3 player never saw. SAVE_BLOB_SIZE_V3 (96) is fixed
     * regardless of today's LEVEL_COUNT, so this fixture is valid unchanged
     * whether LEVEL_COUNT is 20 or 60. */
    {
        static const uint8_t v3_fixture[SAVE_BLOB_SIZE_V3] = {
            0x47, 0x59, 0x52, 0x56, 0x03, 0x00, 0x0a, 0x00, 0xa5, 0x00, 0x00, 0x00,
            0xe8, 0x03, 0x00, 0x00, 0xef, 0x03, 0x00, 0x00, 0xf6, 0x03, 0x00, 0x00,
            0xfd, 0x03, 0x00, 0x00, 0x04, 0x04, 0x00, 0x00, 0x0b, 0x04, 0x00, 0x00,
            0x12, 0x04, 0x00, 0x00, 0x19, 0x04, 0x00, 0x00, 0x20, 0x04, 0x00, 0x00,
            0x27, 0x04, 0x00, 0x00, 0x2e, 0x04, 0x00, 0x00, 0x35, 0x04, 0x00, 0x00,
            0x3c, 0x04, 0x00, 0x00, 0x43, 0x04, 0x00, 0x00, 0x4a, 0x04, 0x00, 0x00,
            0x51, 0x04, 0x00, 0x00, 0x58, 0x04, 0x00, 0x00, 0x5f, 0x04, 0x00, 0x00,
            0x66, 0x04, 0x00, 0x00, 0x6d, 0x04, 0x00, 0x00,
            0x06, 0x77, 0x1f, 0xed};
        static const uint32_t v3_expect_times[SAVE_V3_LEVELS] = {
            1000, 1007, 1014, 1021, 1028, 1035, 1042, 1049, 1056, 1063,
            1070, 1077, 1084, 1091, 1098, 1105, 1112, 1119, 1126, 1133};
        int ok;

        r.unlocked = 0;
        r.completed_mask = 0;
        for (i = 0; i < (size_t)LEVEL_COUNT; i++)
            r.best_cs[i] = 0;
        CHECK(save_deserialize(&r, v3_fixture, SAVE_BLOB_SIZE_V3) == 0);
        CHECK(r.unlocked == 10);
        CHECK(r.completed_mask == (uint64_t)0xA5u);

        ok = 1;
        for (i = 0; i < (size_t)SAVE_V3_LEVELS; i++)
            if (r.best_cs[i] != v3_expect_times[i])
                ok = 0;
        CHECK(ok); /* all 20 v3 best times survived the migration */

        ok = 1;
        for (i = (size_t)SAVE_V3_LEVELS; i < (size_t)LEVEL_COUNT; i++)
            if (r.best_cs[i] != SAVE_NO_TIME)
                ok = 0;
        CHECK(ok); /* any level >= 20 (new to this v3 player) reads SAVE_NO_TIME */

        /* Also rejected at the wrong length for a v3 blob (v4-sized). */
        r.unlocked = 9;
        CHECK(save_deserialize(&r, v3_fixture, SAVE_BLOB_SIZE) == -1);
        CHECK(is_defaults(&r));
    }

    /* A v4 save written by a 20-level build, read by this one. This is the case that
     * silently wiped every save while the expected length was derived from today's
     * LEVEL_COUNT: at LEVEL_COUNT 60 a 100-byte v4 blob failed the length check, so
     * save_load returned -1, the game started on defaults, and the next save_write
     * overwrote the real progress. Byte 7 now carries the writer's level count (20),
     * so the blob is measured against itself.
     *
     * The fixture has every one of the 20 vaults finished, which is also what proves
     * unlocked is re-derived: the 20-level build clamped unlocked to 20, and nothing in
     * the game can raise it, so vault 21 would stay locked forever. Built with python
     * zlib.crc32 over bytes 0..95. */
    {
        static const uint8_t v4_20[100] = {
            0x47, 0x59, 0x52, 0x56, 0x04, 0x00, 0x14, 0x14, 0xFF, 0xFF, 0x0F, 0x00,
            0x00, 0x00, 0x00, 0x00, 0xE8, 0x03, 0x00, 0x00, 0xEF, 0x03, 0x00, 0x00,
            0xF6, 0x03, 0x00, 0x00, 0xFD, 0x03, 0x00, 0x00, 0x04, 0x04, 0x00, 0x00,
            0x0B, 0x04, 0x00, 0x00, 0x12, 0x04, 0x00, 0x00, 0x19, 0x04, 0x00, 0x00,
            0x20, 0x04, 0x00, 0x00, 0x27, 0x04, 0x00, 0x00, 0x2E, 0x04, 0x00, 0x00,
            0x35, 0x04, 0x00, 0x00, 0x3C, 0x04, 0x00, 0x00, 0x43, 0x04, 0x00, 0x00,
            0x4A, 0x04, 0x00, 0x00, 0x51, 0x04, 0x00, 0x00, 0x58, 0x04, 0x00, 0x00,
            0x5F, 0x04, 0x00, 0x00, 0x66, 0x04, 0x00, 0x00, 0x6D, 0x04, 0x00, 0x00,
            0x32, 0xDC, 0x82, 0xCF};
        uint8_t bad[100];
        int ok;

        CHECK(save_deserialize(&r, v4_20, sizeof v4_20) == 0);
        CHECK(r.completed_mask == (uint64_t)0xFFFFFu);
        /* Finishing vault 20 earns vault 21, wherever LEVEL_COUNT now ends. */
        CHECK(r.unlocked == (LEVEL_COUNT >= 21 ? 21 : LEVEL_COUNT));
        ok = 1;
        for (i = 0; i < 20; i++)
            if (r.best_cs[i] != (uint32_t)(1000 + i * 7))
                ok = 0;
        CHECK(ok); /* all 20 best times survived the level-count change */
        ok = 1;
        for (i = 20; i < (size_t)LEVEL_COUNT; i++)
            if (r.best_cs[i] != SAVE_NO_TIME)
                ok = 0;
        CHECK(ok); /* vaults this player has never seen come back with no time */

        /* Byte 7 is load-bearing now, so a wrong one must be refused, not guessed at. */
        memcpy(bad, v4_20, sizeof bad);
        bad[7] = 0; /* what the unreleased first draft of v4 wrote there */
        CHECK(save_deserialize(&r, bad, sizeof bad) == -1);
        CHECK(is_defaults(&r));
        memcpy(bad, v4_20, sizeof bad);
        bad[7] = 21; /* claims one more level than the blob is long */
        CHECK(save_deserialize(&r, bad, sizeof bad) == -1);
        CHECK(is_defaults(&r));
        memcpy(bad, v4_20, sizeof bad);
        bad[6] = 21; /* unlocked past the end of the blob's own level list */
        CHECK(save_deserialize(&r, bad, sizeof bad) == -1);
        CHECK(is_defaults(&r));
        CHECK(save_deserialize(&r, v4_20, sizeof v4_20 - 1) == -1); /* truncated */
        CHECK(is_defaults(&r));
    }

    /* A v3 save with every vault of the 20-level game finished -- what a 2.0.2 player
     * who cleared the lot actually has on their memory card. Same unlock problem, same
     * fix. CRC32 over bytes 0..91. */
    {
        static const uint8_t v3_done[SAVE_BLOB_SIZE_V3] = {
            0x47, 0x59, 0x52, 0x56, 0x03, 0x00, 0x14, 0x00, 0xFF, 0xFF, 0x0F, 0x00,
            0xE8, 0x03, 0x00, 0x00, 0xEF, 0x03, 0x00, 0x00, 0xF6, 0x03, 0x00, 0x00,
            0xFD, 0x03, 0x00, 0x00, 0x04, 0x04, 0x00, 0x00, 0x0B, 0x04, 0x00, 0x00,
            0x12, 0x04, 0x00, 0x00, 0x19, 0x04, 0x00, 0x00, 0x20, 0x04, 0x00, 0x00,
            0x27, 0x04, 0x00, 0x00, 0x2E, 0x04, 0x00, 0x00, 0x35, 0x04, 0x00, 0x00,
            0x3C, 0x04, 0x00, 0x00, 0x43, 0x04, 0x00, 0x00, 0x4A, 0x04, 0x00, 0x00,
            0x51, 0x04, 0x00, 0x00, 0x58, 0x04, 0x00, 0x00, 0x5F, 0x04, 0x00, 0x00,
            0x66, 0x04, 0x00, 0x00, 0x6D, 0x04, 0x00, 0x00, 0x93, 0xD3, 0x08, 0xF9};

        CHECK(save_deserialize(&r, v3_done, SAVE_BLOB_SIZE_V3) == 0);
        CHECK(r.unlocked == (LEVEL_COUNT >= 21 ? 21 : LEVEL_COUNT));
        CHECK(save_level_open(&r, 20) == (LEVEL_COUNT >= 21)); /* vault 21 is playable */
    }

    /* save_record_time */
    save_defaults(&s);
    CHECK(is_defaults(&s)); /* defaults: all levels SAVE_NO_TIME */
    CHECK(save_record_time(&s, 0, 500) == 1); /* first time ever recorded is always a new best */
    CHECK(s.best_cs[0] == 500);
    CHECK(save_record_time(&s, 0, 600) == 0); /* slower: not a new best */
    CHECK(s.best_cs[0] == 500);
    CHECK(save_record_time(&s, 0, 500) == 0); /* equal: not a new best */
    CHECK(s.best_cs[0] == 500);
    CHECK(save_record_time(&s, 0, 400) == 1); /* faster: new best, stored */
    CHECK(s.best_cs[0] == 400);
    CHECK(save_record_time(&s, -1, 100) == 0); /* bad index */
    CHECK(save_record_time(&s, LEVEL_COUNT, 100) == 0); /* bad index */
    CHECK(save_record_time(NULL, 0, 100) == 0); /* NULL */
    CHECK(save_record_time(&s, 1, SAVE_NO_TIME) == 0); /* SAVE_NO_TIME is never a recordable time */
    CHECK(s.best_cs[1] == SAVE_NO_TIME);

    /* mark_complete */
    save_defaults(&s);
    save_mark_complete(&s, 0);
    CHECK(s.completed_mask == 1 && s.unlocked == 2);
    save_mark_complete(&s, LEVEL_COUNT - 1); /* last level: unlock clamps */
    CHECK(s.unlocked == LEVEL_COUNT);
    CHECK(s.completed_mask == (((uint64_t)1 << (LEVEL_COUNT - 1)) | (uint64_t)1));
    save_mark_complete(&s, -1);
    save_mark_complete(&s, LEVEL_COUNT); /* out of range: index must be < LEVEL_COUNT */
    save_mark_complete(&s, 7); /* only valid (and sets bit 7) when LEVEL_COUNT > 7 */
    {
        uint64_t expect_mask = ((uint64_t)1 << (LEVEL_COUNT - 1)) | (uint64_t)1;
#if LEVEL_COUNT > 7
        expect_mask |= (uint64_t)1 << 7;
#endif
        CHECK(s.unlocked == LEVEL_COUNT && s.completed_mask == expect_mask);
    }
    /* Replaying level 0 never lowers unlocked. */
    save_mark_complete(&s, 0);
    CHECK(s.unlocked == LEVEL_COUNT);
    /* Finishing the last level alone from defaults. */
    save_defaults(&s);
    save_mark_complete(&s, LEVEL_COUNT - 1);
    CHECK(s.unlocked == LEVEL_COUNT && s.completed_mask == ((uint64_t)1 << (LEVEL_COUNT - 1)));

    /* level_open: playable if unlocked in order OR already cleared (an old v1 player's
     * cleared level 2 stays replayable at its new slot without skipping the ramp). */
    save_defaults(&s);
    CHECK(save_level_open(&s, 0));
    CHECK(!save_level_open(&s, 1));
    CHECK(!save_level_open(&s, -1));
    CHECK(!save_level_open(&s, LEVEL_COUNT));
    CHECK(!save_level_open(NULL, 0));
    s.unlocked = 0; /* corrupt/zero unlocked still leaves level 1 open */
    CHECK(save_level_open(&s, 0));
    {
        /* What the real on-device v1 blob (both old levels cleared) migrates to. */
        s.unlocked = 2;
        s.completed_mask = (uint64_t)1 | ((uint64_t)1 << SAVE_V1_LEVEL2_INDEX);
        CHECK(save_level_open(&s, 0));
        CHECK(save_level_open(&s, 1));
        CHECK(save_level_open(&s, SAVE_V1_LEVEL2_INDEX));
#if SAVE_V1_LEVEL2_INDEX > 2
        CHECK(!save_level_open(&s, 2));
        CHECK(!save_level_open(&s, SAVE_V1_LEVEL2_INDEX - 1));
#endif
    }

    /* Real file I/O under a temp dir, including best times round-tripping. */
    {
        char tmpl[] = "/tmp/gyrovault_test_XXXXXX";
        char dir[256], path[300], tmpf[320];
        char *root = mkdtemp(tmpl);
        FILE *f;
        CHECK(root != NULL);
        if (root) {
            snprintf(dir, sizeof(dir), "%s/data", root);
            snprintf(path, sizeof(path), "%s/save.dat", dir);
            snprintf(tmpf, sizeof(tmpf), "%s.tmp", path);

            CHECK(save_load(&r, path) == -1); /* missing */
            CHECK(is_defaults(&r));

            save_defaults(&s);
            s.unlocked = 2;
            s.completed_mask = 1;
            CHECK(save_record_time(&s, 0, 1234) == 1);
            CHECK(save_write(&s, dir, path) == 0); /* creates dir */
            r.unlocked = 0;
            CHECK(save_load(&r, path) == 0);
            CHECK(r.unlocked == 2 && r.completed_mask == 1);
            CHECK(r.best_cs[0] == 1234);
            CHECK(r.best_cs[1] == SAVE_NO_TIME);

            s.completed_mask = 3; /* overwrite an existing file */
            CHECK(save_record_time(&s, 1, 555) == 1);
            CHECK(save_write(&s, dir, path) == 0);
            CHECK(save_load(&r, path) == 0);
            /* The mask was set by hand here without save_mark_complete, so the stored
             * unlocked (2) trails it; the load re-derives 3 from the completed bits. */
            CHECK(r.unlocked == 3 && r.completed_mask == 3);
            CHECK(r.best_cs[0] == 1234 && r.best_cs[1] == 555);
            f = fopen(tmpf, "rb");
            CHECK(f == NULL); /* temp file renamed away */
            if (f)
                fclose(f);

            /* Corrupt on disk -> -1 + defaults. */
            f = fopen(path, "r+b");
            CHECK(f != NULL);
            if (f) {
                fseek(f, 7, SEEK_SET);
                fputc(0x7F, f);
                fclose(f);
            }
            CHECK(save_load(&r, path) == -1);
            CHECK(is_defaults(&r));

            /* Truncated file. */
            f = fopen(path, "wb");
            if (f) {
                fwrite("GYRV", 1, 4, f);
                fclose(f);
            }
            CHECK(save_load(&r, path) == -1);

            /* Unwritable location. */
            CHECK(save_write(&s, "/nonexistent_gv_dir/x", "/nonexistent_gv_dir/x/save.dat") == -1);

            remove(path);
            remove(tmpf);
            remove(dir);
            remove(root);
        }
    }

    return test_summary("save");
}
