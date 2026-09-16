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

int main(void)
{
    SaveData s, r;
    uint8_t buf[SAVE_BLOB_SIZE], bad[SAVE_BLOB_SIZE];
    size_t i;

    /* Defaults blob (v3) must match an independent CRC32 (python zlib.crc32 of the
     * 92-byte header+mask+20xSAVE_NO_TIME prefix = 0x98665bf7). */
    {
        static const uint8_t expect[SAVE_BLOB_SIZE] = {
            'G', 'Y', 'R', 'V', 3, 0, 1, 0, 0, 0, 0, 0,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xf7, 0x5b, 0x66, 0x98};
        save_defaults(&s);
        CHECK(is_defaults(&s));
        CHECK(save_serialize(&s, buf, sizeof(buf)) == SAVE_BLOB_SIZE);
        CHECK(memcmp(buf, expect, SAVE_BLOB_SIZE) == 0);
    }

    /* Roundtrip (v3), including best times. */
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

    /* Every single flipped byte (including the best-times region) is rejected
     * and yields defaults. */
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

    /* CRC corruption of a time byte specifically (byte 12 = low byte of best_cs[0]). */
    {
        memcpy(bad, buf, sizeof(bad));
        bad[12] ^= 0x01;
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

    /* Wrong length for the version stamped in the blob: 95 and 17 bytes of an
     * otherwise-valid v3 blob are both rejected. */
    r.unlocked = 9;
    CHECK(save_deserialize(&r, buf, 95) == -1);
    CHECK(is_defaults(&r));
    r.unlocked = 9;
    CHECK(save_deserialize(&r, buf, 17) == -1);
    CHECK(is_defaults(&r));

    /* Valid CRC but out-of-range fields (serialize writes whatever it's given). */
    save_defaults(&s);
    s.unlocked = 0;
    save_serialize(&s, bad, sizeof(bad));
    CHECK(save_deserialize(&r, bad, sizeof(bad)) == -1);
    s.unlocked = LEVEL_COUNT + 1;
    save_serialize(&s, bad, sizeof(bad));
    CHECK(save_deserialize(&r, bad, sizeof(bad)) == -1);
    s.unlocked = 1;
    s.completed_mask = (uint32_t)(1u << LEVEL_COUNT); /* bit LEVEL_COUNT: out of range (LEVEL_COUNT < 32 here) */
    save_serialize(&s, bad, sizeof(bad));
    CHECK(save_deserialize(&r, bad, sizeof(bad)) == -1);
    CHECK(is_defaults(&r));
    s.unlocked = LEVEL_COUNT;
    s.completed_mask = (LEVEL_COUNT >= 32) ? 0xFFFFFFFFu : ((1u << LEVEL_COUNT) - 1u);
    save_serialize(&s, bad, sizeof(bad));
    CHECK(save_deserialize(&r, bad, sizeof(bad)) == 0);
    CHECK(r.unlocked == LEVEL_COUNT && r.completed_mask == s.completed_mask);

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
        CHECK(r.unlocked == 2 && r.completed_mask == 3);
        {
            int ok = 1;
            for (i = 0; i < (size_t)LEVEL_COUNT; i++)
                if (r.best_cs[i] != SAVE_NO_TIME)
                    ok = 0;
            CHECK(ok);
        }
        /* Also rejected at the wrong length for a v2 blob (96 bytes, v3-sized). */
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
        CHECK(r.completed_mask == ((1u << 0) | (1u << SAVE_V1_LEVEL2_INDEX)));
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
    CHECK(s.completed_mask == ((1u << (LEVEL_COUNT - 1)) | 1u));
    save_mark_complete(&s, -1);
    save_mark_complete(&s, LEVEL_COUNT); /* out of range: index must be < LEVEL_COUNT */
    save_mark_complete(&s, 7); /* only valid (and sets bit 7) when LEVEL_COUNT > 7 */
    {
        uint32_t expect_mask = (1u << (LEVEL_COUNT - 1)) | 1u;
#if LEVEL_COUNT > 7
        expect_mask |= (uint32_t)(1u << 7);
#endif
        CHECK(s.unlocked == LEVEL_COUNT && s.completed_mask == expect_mask);
    }
    /* Replaying level 0 never lowers unlocked. */
    save_mark_complete(&s, 0);
    CHECK(s.unlocked == LEVEL_COUNT);
    /* Finishing the last level alone from defaults. */
    save_defaults(&s);
    save_mark_complete(&s, LEVEL_COUNT - 1);
    CHECK(s.unlocked == LEVEL_COUNT && s.completed_mask == (1u << (LEVEL_COUNT - 1)));

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
        s.completed_mask = 1u | (uint32_t)(1u << SAVE_V1_LEVEL2_INDEX);
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
            CHECK(r.unlocked == 2 && r.completed_mask == 3);
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
