#ifndef GV_PKG_INSTALL_H
#define GV_PKG_INSTALL_H

/* Private helpers for the updater: unpack a .vpk and hand it to the Vita's promoter. */

#include <stddef.h>

/* Called with 0..1 while extracting. */
typedef void (*pkg_progress_fn)(float fraction, void *user);

/* Recursively delete a file or directory tree. Missing path is not an error. */
void pkg_remove_tree(const char *path);

/* Create a directory and any missing parents. Returns 0 on success. */
int pkg_mkdir_p(const char *path);

/* Wipe dest_dir, extract the zip at vpk_path into it (stored + deflate entries only),
 * require sce_sys/param.sfo with TITLE_ID == expected_title_id.
 * Returns 0 on success, -1 with err filled. */
int pkg_extract_vpk(const char *vpk_path, const char *dest_dir, const char *expected_title_id,
                    pkg_progress_fn fn, void *user, char *err, size_t err_cap);

/* Write <dir>/sce_sys/package/head.bin (skipped if one already exists).
 * Returns 0 on success, -1 with err filled. */
int pkg_make_head_bin(const char *dir, char *err, size_t err_cap);

/* Load PAF + promoter, promote the extracted directory, poll until finished, unload.
 * Returns 0 on success, -1 with err filled (includes the promoter's hex result code). */
int pkg_promote_dir(const char *dir, char *err, size_t err_cap);

#endif
