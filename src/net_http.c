#include "net_http.h"
#include "version.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <psp2/sysmodule.h>
#include <psp2/net/net.h>
#include <psp2/net/netctl.h>
#include <curl/curl.h>
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/x509.h>

#define NET_POOL_SIZE   (1 * 1024 * 1024)
#define CA_BUNDLE_PATH  "app0:assets/cacert.pem"
#define USER_AGENT      GV_APP_NAME "/" GV_VERSION " (PS Vita)"

static int  s_ready;
static int  s_net_inited;
static int  s_netctl_inited;
static int  s_curl_inited;
static char s_net_pool[NET_POOL_SIZE];

/* The CA bundle is read into memory here and handed to curl as a blob instead of letting
 * OpenSSL open app0: itself. On real hardware curl reported "error adding trust anchors
 * from locations: CAfile: app0:assets/cacert.pem CApath: none" while the same build worked
 * in Vita3K, and that message comes from OpenSSL's own file load. Reading it ourselves also
 * means a failure names the step that failed instead of one opaque TLS message. */
static void  *s_ca_blob;
static size_t s_ca_len;
static char   s_ca_note[96];

/* What OpenSSL itself makes of the bundle. curl's blob loader parses the PEM and pushes every
 * certificate into an X509_STORE; one rejected certificate makes it discard all of them
 * ("count = 0; break;" in vtls/openssl.c) and report the single CURLcode 77, which cannot tell
 * "the PEM did not parse" from "the store refused a certificate" from "out of memory".
 * Replaying those same steps here turns that one number into counts and a real OpenSSL error. */
static char   s_ca_diag[160];

static void set_err(char *err, size_t cap, const char *fmt_a, long code)
{
    if (err && cap)
        snprintf(err, cap, fmt_a, code);
}

/* Read the CA bundle once. Never fatal: apply_common() falls back to letting curl open the
 * file, which is exactly what 1.0.0 did, so this can only add a working path, not remove one.
 * s_ca_note records which path is in use and is appended to any transfer error. */
static void ca_load(void)
{
    s_ca_note[0] = '\0';

    FILE *f = fopen(CA_BUNDLE_PATH, "rb");
    if (!f) {
        snprintf(s_ca_note, sizeof(s_ca_note), "file fallback, open failed errno %d", errno);
        return;
    }

    long n = -1;
    if (fseek(f, 0, SEEK_END) == 0)
        n = ftell(f);
    if (n <= 0) {
        fclose(f);
        snprintf(s_ca_note, sizeof(s_ca_note), "file fallback, bad size %ld", n);
        return;
    }
    rewind(f);

    void *buf = malloc((size_t)n);
    if (!buf) {
        fclose(f);
        snprintf(s_ca_note, sizeof(s_ca_note), "file fallback, no memory for %ld B", n);
        return;
    }

    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    if (got != (size_t)n) {
        free(buf);
        snprintf(s_ca_note, sizeof(s_ca_note), "file fallback, read %u of %ld B", (unsigned)got,
                 n);
        return;
    }

    s_ca_blob = buf;
    s_ca_len  = got;
    snprintf(s_ca_note, sizeof(s_ca_note), "blob %ld B", n);
}

/* Reason string if this libcrypto kept them, the raw code otherwise. */
static const char *err_reason(unsigned long e)
{
    const char *r = ERR_reason_error_string(e);
    return r ? r : "?";
}

/* Replay curl's load_cacert_from_memory() against our own blob and record the outcome. Purely
 * observational: it builds a throwaway store and never touches the one curl uses. */
static void ca_diag(void)
{
    s_ca_diag[0] = '\0';
    if (!s_ca_blob)
        return;

    BIO *bio = BIO_new_mem_buf(s_ca_blob, (int)s_ca_len);
    if (!bio) {
        snprintf(s_ca_diag, sizeof(s_ca_diag), "BIO alloc failed");
        return;
    }

    ERR_clear_error();
    STACK_OF(X509_INFO) *inf = PEM_X509_INFO_read_bio(bio, NULL, NULL, NULL);
    if (!inf) {
        unsigned long e = ERR_get_error();
        snprintf(s_ca_diag, sizeof(s_ca_diag), "%uB PEM unreadable 0x%08lX %s",
                 (unsigned)s_ca_len, e, err_reason(e));
        BIO_free(bio);
        return;
    }

    X509_STORE *store = X509_STORE_new();
    if (!store) {
        unsigned long e = ERR_get_error();
        snprintf(s_ca_diag, sizeof(s_ca_diag), "%uB store alloc failed 0x%08lX %s",
                 (unsigned)s_ca_len, e, err_reason(e));
        sk_X509_INFO_pop_free(inf, X509_INFO_free);
        BIO_free(bio);
        return;
    }

    int           entries = sk_X509_INFO_num(inf);
    int           certs = 0, added = 0, first_bad = -1;
    unsigned long first_err = 0;
    for (int i = 0; i < entries; i++) {
        X509_INFO *it = sk_X509_INFO_value(inf, i);
        if (!it || !it->x509)
            continue;
        certs++;
        ERR_clear_error();
        if (X509_STORE_add_cert(store, it->x509)) {
            added++;
        } else if (first_bad < 0) {
            first_bad = i;
            first_err = ERR_get_error();
        }
    }

    if (certs > 0 && added == certs)
        snprintf(s_ca_diag, sizeof(s_ca_diag), "%uB ok %d/%d", (unsigned)s_ca_len, added, certs);
    else
        snprintf(s_ca_diag, sizeof(s_ca_diag), "%uB %d/%d of %d bad#%d 0x%08lX %s",
                 (unsigned)s_ca_len, added, certs, entries, first_bad, first_err,
                 err_reason(first_err));

    X509_STORE_free(store);
    sk_X509_INFO_pop_free(inf, X509_INFO_free);
    BIO_free(bio);
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

    if (!s_ca_blob) {
        ca_load();
        ca_diag();
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
    if (s_ca_blob) {
        free(s_ca_blob);
        s_ca_blob = NULL;
        s_ca_len = 0;
    }
    s_ready = 0;
}

/* Common options for every request. errbuf must be CURL_ERROR_SIZE bytes. */
static void apply_common(CURL *c, const char *url, char *errbuf)
{
    errbuf[0] = '\0';
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_ERRORBUFFER, errbuf);
    if (s_ca_blob) {
        struct curl_blob ca;
        ca.data  = s_ca_blob;
        ca.len   = s_ca_len;
        ca.flags = CURL_BLOB_NOCOPY; /* s_ca_blob outlives every transfer */
        curl_easy_setopt(c, CURLOPT_CAINFO_BLOB, &ca);
    } else {
        curl_easy_setopt(c, CURLOPT_CAINFO, CA_BUNDLE_PATH);
    }
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
    /* The CA note leads. Errors are drawn as three wrapped lines and anything past them is
     * dropped without an ellipsis (draw_wrapped in game.c), so a note appended after a long TLS
     * message is never seen on screen - which is exactly what happened to the 2.0.0 one. */
    const char *note = s_ca_diag[0] ? s_ca_diag : (s_ca_note[0] ? s_ca_note : "none");
    if (errbuf && errbuf[0])
        snprintf(err, cap, "[CA %s] %s: %s", note, curl_easy_strerror(cc), errbuf);
    else
        snprintf(err, cap, "[CA %s] %s (curl %d)", note, curl_easy_strerror(cc), (int)cc);
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
