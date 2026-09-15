#ifndef GV_NET_HTTP_H
#define GV_NET_HTTP_H

/* Private libcurl helpers for the updater. Not part of the game-wide contract. */

#include <stddef.h>

/* Loads SCE_SYSMODULE_NET, sceNetInit, sceNetCtlInit, curl_global_init. Idempotent.
 * Returns 0 on success, -1 with err filled. */
int  net_http_init(char *err, size_t err_cap);
void net_http_shutdown(void);

/* HEAD-style request (NOBODY) with redirects NOT followed.
 * On return 0: *status is the HTTP code and location holds CURLINFO_REDIRECT_URL ("" if none).
 * Returns -1 on transport/TLS failure with err filled. */
int net_http_get_redirect(const char *url, long *status, char *location, size_t loc_cap,
                          char *err, size_t err_cap);

/* Return nonzero from the callback to abort the transfer. total may be 0 when unknown. */
typedef int (*net_progress_fn)(double now, double total, void *user);

/* GET url (redirects followed) into dest_path. Returns 0 only for HTTP 200 with the body
 * fully written; otherwise -1 with err filled (and *status set if a response arrived). */
int net_http_download(const char *url, const char *dest_path, net_progress_fn fn, void *user,
                      long *status, char *err, size_t err_cap);

#endif
