#include "net_http.h"
#include "version.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <psp2/sysmodule.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <curl/curl.h>

#define NET_POOL_SIZE   (1 * 1024 * 1024)
#define CA_BUNDLE_PATH  "app0:assets/cacert.pem"
#define USER_AGENT      GV_APP_NAME "/" GV_VERSION " (PS Vita)"

static int  s_ready;
static int  s_net_inited;
static int  s_netctl_inited;
static int  s_curl_inited;
static char s_net_pool[NET_POOL_SIZE];

static void set_err(char *err, size_t cap, const char *fmt_a, long code)
{
    if (err && cap)
        snprintf(err, cap, fmt_a, code);
}

int net_http_init(char *err, size_t err_cap)
{
    if (s_ready)
        return 0;

    int rc = sceSysmoduleLoadModule(SCE_SYSMODULE_NET);
    if (rc < 0) {
        set_err(err, err_cap, "Net module load failed (0x%08lX)", (long)(unsigned)rc);
        return -1;
    }

    if (!s_net_inited) {
        SceNetInitParam p;
        p.memory = s_net_pool;
        p.size   = NET_POOL_SIZE;
        p.flags  = 0;
        rc = sceNetInit(&p);
        if (rc < 0) {
            set_err(err, err_cap, "sceNetInit failed (0x%08lX)", (long)(unsigned)rc);
            return -1;
        }
        s_net_inited = 1;
    }

    if (!s_netctl_inited) {
        rc = sceNetCtlInit();
        if (rc < 0) {
            set_err(err, err_cap, "sceNetCtlInit failed (0x%08lX)", (long)(unsigned)rc);
            return -1;
        }
        s_netctl_inited = 1;
    }

    if (!s_curl_inited) {
        CURLcode cc = curl_global_init(CURL_GLOBAL_ALL);
        if (cc != CURLE_OK) {
            if (err && err_cap)
                snprintf(err, err_cap, "curl_global_init: %s", curl_easy_strerror(cc));
            return -1;
        }
        s_curl_inited = 1;
    }

    s_ready = 1;
    return 0;
}

void net_http_shutdown(void)
{
    if (s_curl_inited) {
        curl_global_cleanup();
        s_curl_inited = 0;
    }
    if (s_netctl_inited) {
        sceNetCtlTerm();
        s_netctl_inited = 0;
    }
    if (s_net_inited) {
        sceNetTerm();
        s_net_inited = 0;
    }
    s_ready = 0;
}

/* Common options for every request. errbuf must be CURL_ERROR_SIZE bytes. */
static void apply_common(CURL *c, const char *url, char *errbuf)
{
    errbuf[0] = '\0';
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, errbuf);
    curl_easy_setopt(c, CURLOPT_CAINFO, CA_BUNDLE_PATH);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, USER_AGENT);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 20L);
    /* Abort if slower than 1 byte/s for 60 s rather than hanging forever. */
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 60L);
}

static void curl_fail_text(char *err, size_t cap, CURLcode cc, const char *errbuf)
{
    if (!err || !cap)
        return;
    if (errbuf && errbuf[0])
        snprintf(err, cap, "%s: %s", curl_easy_strerror(cc), errbuf);
    else
        snprintf(err, cap, "%s (curl %d)", curl_easy_strerror(cc), (int)cc);
}

int net_http_get_redirect(const char *url, long *status, char *location, size_t loc_cap,
                          char *err, size_t err_cap)
{
    char errbuf[CURL_ERROR_SIZE];
    *status = 0;
    if (location && loc_cap)
        location[0] = '\0';

    CURL *c = curl_easy_init();
    if (!c) {
        if (err && err_cap)
            snprintf(err, err_cap, "curl_easy_init failed");
        return -1;
    }
    apply_common(c, url, errbuf);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(c, CURLOPT_NOBODY, 1L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 30L);

    CURLcode cc = curl_easy_perform(c);
    if (cc != CURLE_OK) {
        curl_fail_text(err, err_cap, cc, errbuf);
        curl_easy_cleanup(c);
        return -1;
    }

    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, status);
    char *redir = NULL;
    if (curl_easy_getinfo(c, CURLINFO_REDIRECT_URL, &redir) == CURLE_OK && redir && location
        && loc_cap) {
        snprintf(location, loc_cap, "%s", redir);
    }
    curl_easy_cleanup(c);
    return 0;
}

typedef struct {
    FILE *fp;
    int   write_failed;
} DlSink;

static size_t write_cb(char *ptr, size_t size, size_t nmemb, void *user)
{
    DlSink *s = (DlSink *)user;
    size_t n = size * nmemb;
    if (fwrite(ptr, 1, n, s->fp) != n) {
        s->write_failed = 1;
        return 0; /* makes curl fail with CURLE_WRITE_ERROR */
    }
    return n;
}

typedef struct {
    net_progress_fn fn;
    void *user;
} DlProgress;

static int xferinfo_cb(void *user, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal,
                       curl_off_t ulnow)
{
    (void)ultotal;
    (void)ulnow;
    DlProgress *p = (DlProgress *)user;
    if (p->fn)
        return p->fn((double)dlnow, (double)dltotal, p->user);
    return 0;
}

int net_http_download(const char *url, const char *dest_path, net_progress_fn fn, void *user,
                      long *status, char *err, size_t err_cap)
{
    char errbuf[CURL_ERROR_SIZE];
    *status = 0;

    DlSink sink;
    sink.write_failed = 0;
    sink.fp = fopen(dest_path, "wb");
    if (!sink.fp) {
        if (err && err_cap)
            snprintf(err, err_cap, "Cannot create %s", dest_path);
        return -1;
    }

    CURL *c = curl_easy_init();
    if (!c) {
        fclose(sink.fp);
        if (err && err_cap)
            snprintf(err, err_cap, "curl_easy_init failed");
        return -1;
    }

    DlProgress prog = { fn, user };
    apply_common(c, url, errbuf);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 8L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, xferinfo_cb);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, &prog);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);

    CURLcode cc = curl_easy_perform(c);
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, status);
    curl_easy_cleanup(c);

    int close_rc = fclose(sink.fp);

    if (cc != CURLE_OK) {
        if (sink.write_failed && err && err_cap)
            snprintf(err, err_cap, "Write to %s failed (memory card full?)", dest_path);
        else
            curl_fail_text(err, err_cap, cc, errbuf);
        return -1;
    }
    if (close_rc != 0) {
        if (err && err_cap)
            snprintf(err, err_cap, "Write to %s failed on close", dest_path);
        return -1;
    }
    if (*status != 200) {
        if (err && err_cap)
            snprintf(err, err_cap, "Download failed: HTTP %ld", *status);
        return -1;
    }
    return 0;
}
