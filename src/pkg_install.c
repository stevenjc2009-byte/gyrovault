#include "pkg_install.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include <psp2/types.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/io/dirent.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/sysmodule.h>
#include <psp2/promoterutil.h>
#include <openssl/sha.h>
#include <zlib.h>

#define PATH_CAP 512

/* ------------------------------------------------------------------ filesystem */

void pkg_remove_tree(const char *path)
{
    SceUID d = sceIoDopen(path);
    if (d < 0) {
        sceIoRemove(path); /* a plain file, or nothing there */
        return;
    }
    SceIoDirent ent;
    for (;;) {
        memset(&ent, 0, sizeof(ent));
        if (sceIoDread(d, &ent) <= 0)
            break;
        if (strcmp(ent.d_name, ".") == 0 || strcmp(ent.d_name, "..") == 0)
            continue;
        char child[PATH_CAP];
        snprintf(child, sizeof(child), "%s/%s", path, ent.d_name);
        if (SCE_S_ISDIR(ent.d_stat.st_mode))
            pkg_remove_tree(child);
        else
            sceIoRemove(child);
    }
    sceIoDclose(d);
    sceIoRmdir(path);
}

static int dir_exists(const char *path)
{
    SceIoStat st;
    return sceIoGetstat(path, &st) >= 0 && SCE_S_ISDIR(st.st_mode);
}

int pkg_mkdir_p(const char *path)
{
    char buf[PATH_CAP];
    size_t n = strlen(path);
    if (n == 0 || n >= sizeof(buf))
        return -1;
    memcpy(buf, path, n + 1);

    /* Skip the device prefix ("ux0:") so we never try to mkdir it. */
    char *p = strchr(buf, ':');
    p = p ? p + 1 : buf;
    while (*p == '/')
        p++;

    for (; *p; p++) {
        if (*p != '/')
            continue;
        *p = '\0';
        if (!dir_exists(buf))
            sceIoMkdir(buf, 0777);
        *p = '/';
    }
    if (!dir_exists(buf))
        sceIoMkdir(buf, 0777);
    return dir_exists(buf) ? 0 : -1;
}

/* ------------------------------------------------------------------ zip reader */

static uint16_t rd16(const unsigned char *b) { return (uint16_t)(b[0] | (b[1] << 8)); }
static uint32_t rd32(const unsigned char *b)
{
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}
static uint32_t rd32be(const unsigned char *b)
{
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) | ((uint32_t)b[2] << 8) | (uint32_t)b[3];
}

/* Reject absolute paths, drive prefixes, backslashes and any ".." component. */
static int entry_name_safe(const char *name)
{
    if (name[0] == '\0' || name[0] == '/' || strchr(name, ':') || strchr(name, '\\'))
        return 0;
    const char *p = name;
    while (*p) {
        const char *slash = strchr(p, '/');
        size_t len = slash ? (size_t)(slash - p) : strlen(p);
        if (len == 2 && p[0] == '.' && p[1] == '.')
            return 0;
        if (!slash)
            break;
        p = slash + 1;
    }
    return 1;
}

static int find_eocd(FILE *f, long *cd_off, uint16_t *count, char *err, size_t cap)
{
    if (fseek(f, 0, SEEK_END) != 0)
        goto bad;
    long size = ftell(f);
    if (size < 22)
        goto bad;
    long scan = size < (65535 + 22) ? size : (65535 + 22);
    unsigned char *tail = malloc((size_t)scan);
    if (!tail) {
        snprintf(err, cap, "Out of memory reading update archive");
        return -1;
    }
    if (fseek(f, size - scan, SEEK_SET) != 0 || fread(tail, 1, (size_t)scan, f) != (size_t)scan) {
        free(tail);
        goto bad;
    }
    for (long i = scan - 22; i >= 0; i--) {
        if (rd32(tail + i) == 0x06054b50UL) {
            *count  = rd16(tail + i + 10);
            *cd_off = (long)rd32(tail + i + 16);
            free(tail);
            if (*cd_off == (long)0xFFFFFFFFL || *cd_off >= size) {
                snprintf(err, cap, "Update archive uses unsupported ZIP64");
                return -1;
            }
            return 0;
        }
    }
    free(tail);
bad:
    snprintf(err, cap, "Update archive is not a valid zip");
    return -1;
}

static int extract_entry(FILE *f, long local_off, uint16_t method, uint32_t csize, uint32_t usize,
                         uint32_t crc_expect, const char *out_path, char *err, size_t cap)
{
    unsigned char lh[30];
    if (fseek(f, local_off, SEEK_SET) != 0 || fread(lh, 1, 30, f) != 30
        || rd32(lh) != 0x04034b50UL) {
        snprintf(err, cap, "Corrupt zip local header");
        return -1;
    }
    long data_off = local_off + 30 + rd16(lh + 26) + rd16(lh + 28);
    if (fseek(f, data_off, SEEK_SET) != 0) {
        snprintf(err, cap, "Corrupt zip data offset");
        return -1;
    }

    FILE *out = fopen(out_path, "wb");
    if (!out) {
        snprintf(err, cap, "Cannot create %s", out_path);
        return -1;
    }

    enum { CHUNK = 64 * 1024 };
    unsigned char *inbuf = malloc(CHUNK);
    unsigned char *outbuf = malloc(CHUNK);
    int rc = -1;
    uLong crc = crc32(0L, Z_NULL, 0);
    uint32_t written = 0;
    uint32_t remaining = csize;

    if (!inbuf || !outbuf) {
        snprintf(err, cap, "Out of memory extracting update");
        goto done;
    }

    if (method == 0) {
        while (remaining > 0) {
            size_t want = remaining < CHUNK ? remaining : CHUNK;
            if (fread(inbuf, 1, want, f) != want) {
                snprintf(err, cap, "Truncated zip entry");
                goto done;
            }
            if (fwrite(inbuf, 1, want, out) != want) {
                snprintf(err, cap, "Write failed: %s (memory card full?)", out_path);
                goto done;
            }
            crc = crc32(crc, inbuf, (uInt)want);
            written += (uint32_t)want;
            remaining -= (uint32_t)want;
        }
    } else if (method == 8) {
        z_stream zs;
        memset(&zs, 0, sizeof(zs));
        if (inflateInit2(&zs, -MAX_WBITS) != Z_OK) {
            snprintf(err, cap, "inflateInit2 failed");
            goto done;
        }
        int zr = Z_OK;
        while (zr != Z_STREAM_END) {
            if (zs.avail_in == 0) {
                if (remaining == 0) {
                    snprintf(err, cap, "Truncated deflate stream");
                    inflateEnd(&zs);
                    goto done;
                }
                size_t want = remaining < CHUNK ? remaining : CHUNK;
                if (fread(inbuf, 1, want, f) != want) {
                    snprintf(err, cap, "Truncated zip entry");
                    inflateEnd(&zs);
                    goto done;
                }
                remaining -= (uint32_t)want;
                zs.next_in = inbuf;
                zs.avail_in = (uInt)want;
            }
            zs.next_out = outbuf;
            zs.avail_out = CHUNK;
            zr = inflate(&zs, Z_NO_FLUSH);
            if (zr != Z_OK && zr != Z_STREAM_END) {
                snprintf(err, cap, "Inflate error %d in %s", zr, out_path);
                inflateEnd(&zs);
                goto done;
            }
            size_t have = CHUNK - zs.avail_out;
            if (have && fwrite(outbuf, 1, have, out) != have) {
                snprintf(err, cap, "Write failed: %s (memory card full?)", out_path);
                inflateEnd(&zs);
                goto done;
            }
            crc = crc32(crc, outbuf, (uInt)have);
            written += (uint32_t)have;
        }
        inflateEnd(&zs);
    } else {
        snprintf(err, cap, "Unsupported zip compression method %u", method);
        goto done;
    }

    if (written != usize || (uint32_t)crc != crc_expect) {
        snprintf(err, cap, "CRC/size mismatch extracting %s", out_path);
        goto done;
    }
    rc = 0;

done:
    free(inbuf);
    free(outbuf);
    if (fclose(out) != 0 && rc == 0) {
        snprintf(err, cap, "Write failed on close: %s", out_path);
        rc = -1;
    }
    return rc;
}

/* ------------------------------------------------------------------ param.sfo */

/* Reads a string value from a PSF (param.sfo) file. Returns 0 if found. */
static int sfo_read_string(const char *path, const char *key, char *out, size_t cap)
{
    out[0] = '\0';
    FILE *f = fopen(path, "rb");
    if (!f)
        return -1;
    unsigned char buf[16 * 1024];
    size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    if (n < 20 || rd32(buf) != 0x46535000UL) /* "\0PSF" */
        return -1;

    uint32_t key_tab = rd32(buf + 8);
    uint32_t val_tab = rd32(buf + 12);
    uint32_t count   = rd32(buf + 16);
    if (key_tab >= n || val_tab >= n || 20 + (uint64_t)count * 16 > n)
        return -1;

    for (uint32_t i = 0; i < count; i++) {
        const unsigned char *e = buf + 20 + i * 16;
        uint32_t k = key_tab + rd16(e);
        uint32_t len = rd32(e + 4);
        uint32_t v = val_tab + rd32(e + 12);
        if (k >= n || v >= n || (uint64_t)v + len > n)
            return -1;
        if (strncmp((const char *)buf + k, key, n - k) != 0)
            continue;
        size_t copy = len < cap - 1 ? len : cap - 1;
        memcpy(out, buf + v, copy);
        out[copy] = '\0';
        return 0;
    }
    return -1;
}

/* ------------------------------------------------------------------ extract */

int pkg_extract_vpk(const char *vpk_path, const char *dest_dir, const char *expected_title_id,
                    pkg_progress_fn fn, void *user, char *err, size_t err_cap)
{
    pkg_remove_tree(dest_dir);
    if (pkg_mkdir_p(dest_dir) != 0) {
        snprintf(err, err_cap, "Cannot create %s", dest_dir);
        return -1;
    }

    FILE *f = fopen(vpk_path, "rb");
    if (!f) {
        snprintf(err, err_cap, "Cannot open %s", vpk_path);
        return -1;
    }

    long cd_off = 0;
    uint16_t count = 0;
    if (find_eocd(f, &cd_off, &count, err, err_cap) != 0) {
        fclose(f);
        return -1;
    }

    int rc = 0;
    long cursor = cd_off;
    for (uint16_t i = 0; i < count && rc == 0; i++) {
        unsigned char ch[46];
        if (fseek(f, cursor, SEEK_SET) != 0 || fread(ch, 1, 46, f) != 46
            || rd32(ch) != 0x02014b50UL) {
            snprintf(err, err_cap, "Corrupt zip central directory");
            rc = -1;
            break;
        }
        uint16_t flags  = rd16(ch + 8);
        uint16_t method = rd16(ch + 10);
        uint32_t crc    = rd32(ch + 16);
        uint32_t csize  = rd32(ch + 20);
        uint32_t usize  = rd32(ch + 24);
        uint16_t nlen   = rd16(ch + 28);
        uint16_t xlen   = rd16(ch + 30);
        uint16_t clen   = rd16(ch + 32);
        uint32_t loff   = rd32(ch + 42);

        char name[PATH_CAP];
        if (nlen == 0 || nlen >= 256 || fread(name, 1, nlen, f) != nlen) {
            snprintf(err, err_cap, "Bad zip entry name");
            rc = -1;
            break;
        }
        name[nlen] = '\0';
        cursor += 46 + nlen + xlen + clen;

        if (!entry_name_safe(name)) {
            snprintf(err, err_cap, "Unsafe path in update archive: %s", name);
            rc = -1;
            break;
        }
        if (flags & 1) {
            snprintf(err, err_cap, "Encrypted zip entries are not supported");
            rc = -1;
            break;
        }
        if (csize == 0xFFFFFFFFUL || usize == 0xFFFFFFFFUL || loff == 0xFFFFFFFFUL) {
            snprintf(err, err_cap, "Update archive uses unsupported ZIP64");
            rc = -1;
            break;
        }

        char out_path[PATH_CAP + 260];
        snprintf(out_path, sizeof(out_path), "%s/%s", dest_dir, name);

        if (name[nlen - 1] == '/') {
            out_path[strlen(out_path) - 1] = '\0';
            if (pkg_mkdir_p(out_path) != 0) {
                snprintf(err, err_cap, "Cannot create %s", out_path);
                rc = -1;
            }
        } else {
            char parent[PATH_CAP + 260];
            snprintf(parent, sizeof(parent), "%s", out_path);
            char *slash = strrchr(parent, '/');
            if (slash) {
                *slash = '\0';
                if (pkg_mkdir_p(parent) != 0) {
                    snprintf(err, err_cap, "Cannot create %s", parent);
                    rc = -1;
                }
            }
            if (rc == 0)
                rc = extract_entry(f, (long)loff, method, csize, usize, crc, out_path, err,
                                   err_cap);
        }
        if (fn && count)
            fn((float)(i + 1) / (float)count, user);
    }
    fclose(f);
    if (rc != 0)
        return -1;

    char sfo_path[PATH_CAP];
    char title_id[16];
    snprintf(sfo_path, sizeof(sfo_path), "%s/sce_sys/param.sfo", dest_dir);
    if (sfo_read_string(sfo_path, "TITLE_ID", title_id, sizeof(title_id)) != 0) {
        snprintf(err, err_cap, "Update has no readable sce_sys/param.sfo");
        return -1;
    }
    if (strcmp(title_id, expected_title_id) != 0) {
        snprintf(err, err_cap, "Update title ID %s does not match %s", title_id,
                 expected_title_id);
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ head.bin */

/*
 * The promoter only installs a directory that looks like an unpacked PKG, which means it
 * wants sce_sys/package/head.bin. Homebrew installers build a minimal fake-PKG header:
 * a fixed 0x430-byte template, the content ID written at 0x30, then three 16-byte digests
 * patched in. Each digest is derived from SHA-1 of a byte range (see fake_pkg_digest).
 * Template bytes: the de-facto header used by VHBB / VitaShell / VitaDB Downloader
 * (sha256 cbb88299...23f45). Offsets inside it are big-endian.
 */
static const unsigned char head_template[] = {
  0x7f, 0x50, 0x4b, 0x47, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x02, 0x80, 0x00, 0x00, 0x00, 0x0b,
  0x00, 0x00, 0x01, 0x90, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x90, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0a, 0x90, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x10,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0xa6, 0x89, 0x94, 0x38, 0x19, 0xf2, 0xdd, 0x05, 0x87, 0x94, 0xb0, 0xb6, 0x7f, 0xc9, 0x30, 0x76,
  0xdc, 0x2f, 0x22, 0xf2, 0x25, 0x40, 0xc6, 0xdf, 0x94, 0xcb, 0xb7, 0x78, 0xf8, 0xa2, 0x54, 0x95,
  0x8c, 0xe6, 0xfd, 0x74, 0x81, 0x0c, 0xf7, 0x9d, 0x47, 0xb2, 0x86, 0x60, 0x3c, 0x2e, 0x00, 0xbb,
  0xa2, 0x07, 0x59, 0x51, 0xe7, 0x95, 0xa4, 0xed, 0x83, 0x50, 0x35, 0xbc, 0x65, 0x63, 0xfe, 0x70,
  0x8b, 0xab, 0x0c, 0x49, 0x73, 0x9d, 0xa3, 0xc9, 0x1f, 0x74, 0x48, 0x22, 0x70, 0x93, 0xfc, 0xe9,
  0x40, 0xca, 0x74, 0x97, 0xba, 0xf1, 0xde, 0x1c, 0xaa, 0x67, 0xb7, 0x41, 0x78, 0xd7, 0x15, 0x68,
  0x7f, 0x65, 0x78, 0x74, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x01, 0x80,
  0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x04, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0d, 0xe0,
  0x00, 0x00, 0x00, 0x00, 0xc0, 0x00, 0x00, 0x02, 0x00, 0x00, 0x04, 0x20, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0x0a, 0xa2, 0xcb, 0x21, 0x8b, 0x37, 0x06, 0x2d, 0x3e, 0x05, 0xfa, 0x11, 0x72, 0x72, 0x88, 0x85,
  0xc9, 0x7b, 0x03, 0x99, 0xa0, 0x70, 0x9c, 0xf8, 0xcf, 0x9d, 0x41, 0x01, 0xd6, 0x17, 0x9f, 0xd3,
  0x57, 0x79, 0x67, 0xf9, 0xb6, 0xf8, 0x56, 0x3d, 0xca, 0xfc, 0xa1, 0x98, 0xe2, 0xc7, 0xcf, 0xd6,
  0x2e, 0x1b, 0xd6, 0x1b, 0xbe, 0x6f, 0xc1, 0x92, 0xbe, 0xe0, 0xb3, 0xc2, 0xe5, 0x65, 0x5a, 0x45,
  0xd9, 0x88, 0xb4, 0x97, 0x5e, 0x16, 0x31, 0x3d, 0xa2, 0x3e, 0x16, 0xae, 0xd4, 0xb7, 0xd5, 0x36,
  0xe3, 0xac, 0x80, 0x8f, 0x18, 0xfe, 0xad, 0x1a, 0x85, 0x20, 0xce, 0xee, 0xda, 0x5d, 0xb7, 0x95,
  0x46, 0x34, 0xcc, 0x49, 0x52, 0x09, 0xf6, 0xeb, 0xa5, 0x0a, 0xe5, 0x7c, 0xb5, 0x7f, 0xaf, 0x6f,
  0x4c, 0x06, 0x8c, 0xe4, 0xd8, 0x5a, 0x03, 0xaf, 0x92, 0x4e, 0x95, 0x5b, 0xbc, 0xe0, 0xc2, 0xac,
  0xff, 0x12, 0x95, 0x31, 0x92, 0xad, 0x06, 0xe8, 0x17, 0x2c, 0xb1, 0xdc, 0x36, 0xa4, 0xc3, 0x9b,
  0xe2, 0x3e, 0x2b, 0xec, 0x65, 0x53, 0xeb, 0x58, 0x84, 0x49, 0x09, 0x0b, 0xf4, 0xc6, 0xb4, 0x02,
  0x70, 0xf3, 0x64, 0x58, 0x75, 0x14, 0x00, 0xf8, 0x68, 0x88, 0x46, 0x7e, 0x5c, 0xbc, 0xbe, 0x8b,
  0x5f, 0xac, 0xe0, 0xe4, 0xa6, 0xf5, 0x77, 0xdd, 0xd9, 0xe5, 0xaf, 0x05, 0xf0, 0x5d, 0xae, 0x22,
  0x7f, 0xb4, 0xd1, 0x1c, 0x7f, 0xcc, 0x3e, 0x98, 0x55, 0xb9, 0x69, 0xd2, 0xd2, 0x10, 0x55, 0x45,
  0x4b, 0x3c, 0x95, 0x70, 0xb7, 0xc3, 0xdb, 0xfe, 0x23, 0xaf, 0xcd, 0x27, 0xa2, 0xd3, 0xac, 0x8c,
  0x11, 0x09, 0xbf, 0xf6, 0xb2, 0x01, 0x62, 0x09, 0xc1, 0xda, 0xfd, 0xa7, 0x47, 0xa9, 0x48, 0xf4,
  0x46, 0x26, 0x06, 0xf2, 0x76, 0x4d, 0xfe, 0x6f, 0x3f, 0x10, 0xb0, 0x1c, 0x1a, 0xde, 0x73, 0x8b,
  0x14, 0x73, 0x3c, 0x39, 0xb6, 0xc6, 0x1b, 0xa1, 0x65, 0x99, 0xb8, 0x33, 0xac, 0xb8, 0x16, 0xb4,
  0xe6, 0xa5, 0xec, 0x02, 0x0b, 0x5b, 0x70, 0x23, 0xeb, 0x24, 0x1a, 0xf7, 0x8c, 0xda, 0x55, 0x96,
  0xdd, 0x4b, 0x1c, 0x85, 0x83, 0x49, 0x01, 0xb2, 0x39, 0xbc, 0x31, 0x3b, 0xe8, 0xf1, 0x5a, 0x49,
  0xcc, 0xcf, 0x0f, 0x85, 0x5f, 0x54, 0x79, 0xe8, 0x31, 0x8d, 0x57, 0x1b, 0xb1, 0xc2, 0x93, 0x87,
  0xe2, 0xe6, 0x56, 0xcf, 0x92, 0x51, 0xfc, 0x49, 0x94, 0xcd, 0xb5, 0x04, 0x1b, 0x04, 0x47, 0xf7,
  0xb4, 0xd2, 0x67, 0x31, 0x54, 0xf0, 0xad, 0x3a, 0xd4, 0x25, 0x8c, 0xed, 0xe9, 0x9b, 0x12, 0xfc,
  0x47, 0x1c, 0xfc, 0x6e, 0x81, 0x29, 0x8b, 0x39, 0xab, 0xbb, 0xf0, 0x35, 0x00, 0x87, 0x88, 0x87,
  0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x02,
  0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x15, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0x04,
  0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x01, 0x90, 0x00, 0x00, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x04, 0x19, 0x67, 0x01, 0x00,
  0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x24, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x0d, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xb0,
  0x1a, 0x92, 0x07, 0x04, 0x61, 0x0c, 0x9d, 0x14, 0x55, 0x8e, 0x17, 0x74, 0xb6, 0x44, 0xd2, 0x5c,
  0x93, 0xf3, 0xc1, 0x58, 0x0f, 0x91, 0x22, 0x2f, 0xfd, 0xb4, 0x42, 0xaa, 0x64, 0xfc, 0x8a, 0xd0,
  0x00, 0x00, 0x00, 0x0f, 0x00, 0x00, 0x00, 0x48, 0x00, 0x00, 0x05, 0x90, 0x00, 0x00, 0x03, 0x20,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0xbe, 0x9a, 0x07, 0x26, 0x1a, 0x91, 0xd6, 0x35, 0x93, 0xcd, 0x59, 0xf4, 0x13, 0x23, 0x34, 0x05,
  0x5b, 0xc4, 0xf5, 0xc3, 0x31, 0xf3, 0xf9, 0xf1, 0x7e, 0xdb, 0x7f, 0x53, 0x0f, 0x1a, 0x0a, 0x79,
  0x00, 0x00, 0x00, 0x10, 0x00, 0x00, 0x00, 0x38, 0x00, 0x00, 0x0a, 0x30, 0x00, 0x00, 0x00, 0x60,
  0xc2, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0xc3, 0x2a, 0xa0, 0xf7, 0x3a, 0x41, 0x84, 0x7c, 0xb8, 0x66, 0x43, 0x7b, 0xca, 0xcd, 0x68, 0x5e,
  0x44, 0xab, 0xd9, 0x85, 0xc9, 0x6b, 0xad, 0x33, 0xa9, 0xbc, 0x88, 0xc6, 0x75, 0xc5, 0x23, 0x9e,
  0x00, 0x00, 0x00, 0x11, 0x00, 0x00, 0x00, 0x28, 0x01, 0x72, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
  0x7a, 0xfe, 0xf5, 0x79, 0x30, 0xaf, 0x76, 0xe0, 0x46, 0xfc, 0x75, 0xdf, 0x08, 0x4e, 0xb8, 0x45,
  0x3d, 0x4f, 0xcb, 0xf4, 0x3d, 0x9b, 0xfa, 0x5f, 0x61, 0x99, 0x6a, 0xde, 0x9c, 0x2e, 0x1a, 0x9c,
  0x19, 0x15, 0x10, 0x1d, 0x71, 0xe6, 0xc0, 0x5a, 0x84, 0x3d, 0x20, 0xe8, 0xae, 0x1e, 0x1c, 0x71,
  0x94, 0xee, 0xbc, 0x73, 0x4d, 0x2c, 0x46, 0xbf, 0x3c, 0xf3, 0x5b, 0x30, 0x3a, 0xc3, 0x18, 0x20,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
};

#define HEAD_SIZE        sizeof(head_template)
#define HEAD_CONTENT_OFF 0x30
#define HEAD_CONTENT_LEN 48

/* 16-byte digest the promoter checks on a fake PKG header: SHA-1 the range, fold selected
 * bytes of that hash into a 64-byte block (layout below), SHA-1 the block, keep 16 bytes. */
static void fake_pkg_digest(const unsigned char *data, size_t len, unsigned char out16[16])
{
    unsigned char h[SHA_DIGEST_LENGTH];
    unsigned char block[64];

    SHA1(data, len, h);
    memset(block, 0, sizeof(block));
    memcpy(block + 0, h + 4, 8);
    memcpy(block + 8, h + 4, 8);
    memcpy(block + 16, h + 12, 4);
    block[20] = h[16];
    block[21] = h[1];
    block[22] = h[2];
    block[23] = h[3];
    memcpy(block + 24, block + 16, 8);
    SHA1(block, sizeof(block), h);
    memcpy(out16, h, 16);
}

/* Patch digest of [start, start+len) into dst; all offsets bounds-checked against HEAD_SIZE. */
static int patch_digest(unsigned char *head, uint32_t start, uint32_t len, uint32_t dst)
{
    if ((uint64_t)start + len > HEAD_SIZE || (uint64_t)dst + 16 > HEAD_SIZE)
        return -1;
    fake_pkg_digest(head + start, len, head + dst);
    return 0;
}

int pkg_make_head_bin(const char *dir, char *err, size_t err_cap)
{
    char head_path[PATH_CAP];
    char pkg_dir[PATH_CAP];
    char sfo_path[PATH_CAP];
    snprintf(head_path, sizeof(head_path), "%s/sce_sys/package/head.bin", dir);
    snprintf(pkg_dir, sizeof(pkg_dir), "%s/sce_sys/package", dir);
    snprintf(sfo_path, sizeof(sfo_path), "%s/sce_sys/param.sfo", dir);

    SceIoStat st;
    if (sceIoGetstat(head_path, &st) >= 0)
        return 0;

    char title_id[16];
    char content_id[HEAD_CONTENT_LEN + 1];
    if (sfo_read_string(sfo_path, "TITLE_ID", title_id, sizeof(title_id)) != 0
        || strlen(title_id) != 9) {
        snprintf(err, err_cap, "param.sfo has no valid TITLE_ID");
        return -1;
    }
    if (sfo_read_string(sfo_path, "CONTENT_ID", content_id, sizeof(content_id)) != 0
        || content_id[0] == '\0') {
        snprintf(content_id, sizeof(content_id), "EP9000-%s_00-0000000000000000", title_id);
    }

    unsigned char head[sizeof(head_template)];
    memcpy(head, head_template, HEAD_SIZE);
    memset(head + HEAD_CONTENT_OFF, 0, HEAD_CONTENT_LEN);
    memcpy(head + HEAD_CONTENT_OFF, content_id, strnlen(content_id, HEAD_CONTENT_LEN));

    /* 1) header digest, 2) info-block digest, 3) whole-file digest (covers 1 and 2). */
    uint32_t hdr_len  = rd32be(head + 0xD0);
    uint32_t info_off = rd32be(head + 0x08);
    uint32_t info_len = rd32be(head + 0x10);
    uint32_t info_dst = rd32be(head + 0xD4);
    uint32_t all_len  = rd32be(head + 0xE8);
    if (info_len < 64 || patch_digest(head, 0, hdr_len, hdr_len) != 0
        || patch_digest(head, info_off, info_len - 64, info_dst) != 0
        || patch_digest(head, 0, all_len, all_len) != 0) {
        snprintf(err, err_cap, "head.bin template offsets out of range");
        return -1;
    }

    if (pkg_mkdir_p(pkg_dir) != 0) {
        snprintf(err, err_cap, "Cannot create %s", pkg_dir);
        return -1;
    }
    FILE *f = fopen(head_path, "wb");
    if (!f) {
        snprintf(err, err_cap, "Cannot create %s", head_path);
        return -1;
    }
    size_t w = fwrite(head, 1, HEAD_SIZE, f);
    if (fclose(f) != 0 || w != HEAD_SIZE) {
        snprintf(err, err_cap, "Write failed: %s", head_path);
        return -1;
    }
    return 0;
}

/* ------------------------------------------------------------------ promoter */

/* The promoter module depends on ScePaf being loaded with an explicit heap argument. */
static int paf_load(void)
{
    static uint32_t paf_args[] = { 0x180000, 0xFFFFFFFF, 0xFFFFFFFF, 1, 0xFFFFFFFF, 0xFFFFFFFF };
    int result = -1;
    SceSysmoduleOpt opt;
    opt.flags = sizeof(opt);
    opt.result = &result;
    opt.unused[0] = -1;
    opt.unused[1] = -1;
    return sceSysmoduleLoadModuleInternalWithArg(SCE_SYSMODULE_INTERNAL_PAF, sizeof(paf_args),
                                                 paf_args, &opt);
}

static void paf_unload(void)
{
    SceSysmoduleOpt opt;
    memset(&opt, 0, sizeof(opt));
    sceSysmoduleUnloadModuleInternalWithArg(SCE_SYSMODULE_INTERNAL_PAF, 0, NULL, &opt);
}

#define PROMOTE_POLL_US     (100 * 1000)
#define PROMOTE_TIMEOUT_US  (10 * 60 * 1000 * 1000ULL)

int pkg_promote_dir(const char *dir, char *err, size_t err_cap)
{
    int rc = paf_load();
    if (rc < 0) {
        snprintf(err, err_cap, "ScePaf load failed (0x%08X)", (unsigned)rc);
        return -1;
    }
    rc = sceSysmoduleLoadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
    if (rc < 0) {
        snprintf(err, err_cap, "Promoter load failed (0x%08X) - is Gyrovault built UNSAFE?",
                 (unsigned)rc);
        paf_unload();
        return -1;
    }
    rc = scePromoterUtilityInit();
    if (rc < 0) {
        snprintf(err, err_cap, "scePromoterUtilityInit failed (0x%08X)", (unsigned)rc);
        sceSysmoduleUnloadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
        paf_unload();
        return -1;
    }

    int ret = -1;
    rc = scePromoterUtilityPromotePkgWithRif(dir, 0);
    if (rc < 0) {
        snprintf(err, err_cap, "Promote failed to start (0x%08X)", (unsigned)rc);
        goto out;
    }

    uint64_t waited = 0;
    for (;;) {
        int state = 0;
        rc = scePromoterUtilityGetState(&state);
        if (rc < 0) {
            snprintf(err, err_cap, "scePromoterUtilityGetState failed (0x%08X)", (unsigned)rc);
            goto out;
        }
        if (state == 0)
            break;
        if (waited >= PROMOTE_TIMEOUT_US) {
            snprintf(err, err_cap, "Install timed out (promoter state %d)", state);
            goto out;
        }
        sceKernelDelayThread(PROMOTE_POLL_US);
        waited += PROMOTE_POLL_US;
    }

    int result = 0;
    rc = scePromoterUtilityGetResult(&result);
    if (rc < 0) {
        snprintf(err, err_cap, "scePromoterUtilityGetResult failed (0x%08X)", (unsigned)rc);
        goto out;
    }
    if (result < 0) {
        snprintf(err, err_cap, "Install failed (promoter result 0x%08X)", (unsigned)result);
        goto out;
    }
    ret = 0;

out:
    scePromoterUtilityExit();
    sceSysmoduleUnloadModuleInternal(SCE_SYSMODULE_INTERNAL_PROMOTER_UTIL);
    paf_unload();
    return ret;
}
