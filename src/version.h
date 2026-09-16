#ifndef GV_VERSION_H
#define GV_VERSION_H

#include <stddef.h>

#define GV_VERSION     "3.0.1"
#define GV_TITLE_ID    "GYRV00001"
#define GV_APP_NAME    "Gyrovault"
#define GV_REPO_OWNER  "stevenjc2009-byte"
#define GV_REPO_NAME   "gyrovault"
/* Release asset the updater downloads: .../releases/download/<tag>/GV_ASSET_NAME */
#define GV_ASSET_NAME  "gyrovault.vpk"

/* Compare dotted versions, an optional leading 'v' ignored ("v1.2.10" > "1.2.9").
 * Returns <0 if a<b, 0 if equal, >0 if a>b. Unparseable input compares as 0.0.0. */
int version_compare(const char *a, const char *b);

/* Extract the tag from a GitHub releases/latest redirect Location, e.g.
 * "https://github.com/o/r/releases/tag/v1.0.1" -> "v1.0.1".
 * Returns 0 on success, -1 if the URL has no "/releases/tag/" segment or out is too small. */
int tag_from_location(const char *location, char *out, size_t cap);

#endif
