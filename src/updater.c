#include "updater.h"
#include "version.h"
#include "net_http.h"
#include "pkg_install.h"

#include <stdio.h>
#include <string.h>

#include <psp2/types.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/threadmgr.h>

#define MSG_CAP        256
#define TAG_CAP        64
#define URL_CAP        512
#define UPDATE_DIR     "ux0:data/gyrovault"
#define UPDATE_VPK     UPDATE_DIR "/update.vpk"
#define UPDATE_TEMP    "ux0:temp/gyrovault_update"
#define THREAD_STACK   (512 * 1024)
#define THREAD_PRIO    0x10000100

typedef enum { JOB_CHECK, JOB_INSTALL } Job;

/* Everything below s_lock is shared between the worker and the UI thread. */
static SceKernelLwMutexWork s_lock;
static int      s_inited;
static int      s_busy;
static int      s_cancel;
static SceUID   s_thread = -1;
static UpdState s_state = UPD_IDLE;
static float    s_progress;
static char     s_message[MSG_CAP];
static char     s_tag[TAG_CAP];

/* UI-thread copies returned by the getters, so the worker can never rewrite a string
 * the caller is still reading. The getters must only be called from one (the UI) thread. */
static char s_ui_message[MSG_CAP];
static char s_ui_tag[TAG_CAP];

static void lock(void)   { sceKernelLockLwMutex(&s_lock, 1, NULL); }
static void unlock(void) { sceKernelUnlockLwMutex(&s_lock, 1); }

static void set_status(UpdState st, float progress, const char *msg)
{
    lock();
    s_state = st;
    s_progress = progress;
    snprintf(s_message, sizeof(s_message), "%s", msg ? msg : "");
    unlock();
}

static void set_progress(float p)
{
    if (p < 0.0f) p = 0.0f;
    if (p > 1.0f) p = 1.0f;
    lock();
    s_progress = p;
    unlock();
}

/* ------------------------------------------------------------------ check */

static void do_check(void)
{
    char url[URL_CAP];
    char location[URL_CAP];
    char err[MSG_CAP];
    long status = 0;

    set_status(UPD_CHECKING, 0.0f, "Checking for updates...");
    snprintf(url, sizeof(url), "https://github.com/%s/%s/releases/latest", GV_REPO_OWNER,
             GV_REPO_NAME);

    if (net_http_get_redirect(url, &status, location, sizeof(location), err, sizeof(err)) != 0) {
        set_status(UPD_ERROR, 0.0f, err);
        return;
    }

    char tag[TAG_CAP];
    if (status < 300 || status > 399 || location[0] == '\0') {
        char msg[MSG_CAP];
        snprintf(msg, sizeof(msg), "Update check failed: HTTP %ld with no redirect", status);
        set_status(UPD_ERROR, 0.0f, msg);
        return;
    }
    if (tag_from_location(location, tag, sizeof(tag)) != 0) {
        /* No releases yet: GitHub redirects /releases/latest to /releases. */
        set_status(UPD_UP_TO_DATE, 1.0f, "No releases published yet");
        return;
    }

    lock();
    snprintf(s_tag, sizeof(s_tag), "%s", tag);
    unlock();

    char msg[MSG_CAP];
    if (version_compare(tag, GV_VERSION) > 0) {
        snprintf(msg, sizeof(msg), "Update available: %s (you have %s)", tag, GV_VERSION);
        set_status(UPD_AVAILABLE, 1.0f, msg);
    } else {
        snprintf(msg, sizeof(msg), "Up to date (%s)", GV_VERSION);
        set_status(UPD_UP_TO_DATE, 1.0f, msg);
    }
}

/* ------------------------------------------------------------------ install */

static int download_progress(double now, double total, void *user)
{
    (void)user;
    if (total > 0.0)
        set_progress((float)(now / total));
    lock();
    int cancel = s_cancel;
    unlock();
    return cancel;
}

static void extract_progress(float fraction, void *user)
{
    (void)user;
    /* Extraction is 0..0.9 of the install phase; the promoter owns the last 10%. */
    set_progress(fraction * 0.9f);
}

static int vpk_has_zip_magic(const char *path)
{
    unsigned char magic[2] = { 0, 0 };
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    size_t n = fread(magic, 1, 2, f);
    fclose(f);
    return n == 2 && magic[0] == 'P' && magic[1] == 'K';
}

static void cleanup_files(void)
{
    pkg_remove_tree(UPDATE_TEMP);
    sceIoRemove(UPDATE_VPK);
}

static void do_install(void)
{
    char tag[TAG_CAP];
    char url[URL_CAP];
    char err[MSG_CAP];
    char msg[MSG_CAP];
    long status = 0;

    lock();
    snprintf(tag, sizeof(tag), "%s", s_tag);
    unlock();

    set_status(UPD_DOWNLOADING, 0.0f, "Downloading update...");

    if (pkg_mkdir_p(UPDATE_DIR) != 0) {
        set_status(UPD_ERROR, 0.0f, "Cannot create " UPDATE_DIR);
        return;
    }
    snprintf(url, sizeof(url), "https://github.com/%s/%s/releases/download/%s/%s",
             GV_REPO_OWNER, GV_REPO_NAME, tag, GV_ASSET_NAME);

    if (net_http_download(url, UPDATE_VPK, download_progress, NULL, &status, err,
                          sizeof(err)) != 0) {
        cleanup_files();
        set_status(UPD_ERROR, 0.0f, err);
        return;
    }
    if (!vpk_has_zip_magic(UPDATE_VPK)) {
        cleanup_files();
        set_status(UPD_ERROR, 0.0f, "Downloaded file is not a VPK (missing PK header)");
        return;
    }

    set_status(UPD_INSTALLING, 0.0f, "Extracting update...");
    if (pkg_extract_vpk(UPDATE_VPK, UPDATE_TEMP, GV_TITLE_ID, extract_progress, NULL, err,
                        sizeof(err)) != 0) {
        cleanup_files();
        set_status(UPD_ERROR, 0.0f, err);
        return;
    }
    sceIoRemove(UPDATE_VPK); /* free card space before the promoter copies the files */

    if (pkg_make_head_bin(UPDATE_TEMP, err, sizeof(err)) != 0) {
        cleanup_files();
        set_status(UPD_ERROR, 0.0f, err);
        return;
    }

    set_status(UPD_INSTALLING, 0.9f, "Installing update...");
    int rc = pkg_promote_dir(UPDATE_TEMP, err, sizeof(err));
    cleanup_files();
    if (rc != 0) {
        snprintf(msg, sizeof(msg), "%s", err);
        set_status(UPD_ERROR, 0.0f, msg);
        return;
    }
    set_status(UPD_DONE, 1.0f, "Update installed - close and reopen Gyrovault");
}

/* ------------------------------------------------------------------ worker */

static int worker_main(SceSize args, void *argp)
{
    Job job = JOB_CHECK;
    if (args == sizeof(Job) && argp)
        memcpy(&job, argp, sizeof(Job));

    char err[MSG_CAP];
    if (net_http_init(err, sizeof(err)) != 0) {
        set_status(UPD_ERROR, 0.0f, err);
    } else if (job == JOB_CHECK) {
        do_check();
    } else {
        do_install();
    }

    lock();
    s_busy = 0;
    unlock();
    return sceKernelExitThread(0);
}

/* Reap a finished worker so its UID is released. Caller must know it is not busy. */
static void reap_thread(void)
{
    if (s_thread >= 0) {
        sceKernelWaitThreadEnd(s_thread, NULL, NULL);
        sceKernelDeleteThread(s_thread);
        s_thread = -1;
    }
}

static void start_job(Job job)
{
    if (!s_inited)
        return;

    lock();
    if (s_busy || (job == JOB_INSTALL && s_state != UPD_AVAILABLE)) {
        unlock();
        return;
    }
    s_busy = 1;
    s_cancel = 0;
    unlock();

    reap_thread();

    SceUID th = sceKernelCreateThread("gv_updater", worker_main, THREAD_PRIO, THREAD_STACK, 0, 0,
                                      NULL);
    if (th < 0) {
        char msg[MSG_CAP];
        snprintf(msg, sizeof(msg), "Cannot create updater thread (0x%08X)", (unsigned)th);
        set_status(UPD_ERROR, 0.0f, msg);
        lock();
        s_busy = 0;
        unlock();
        return;
    }
    s_thread = th;
    int rc = sceKernelStartThread(th, sizeof(Job), &job);
    if (rc < 0) {
        char msg[MSG_CAP];
        snprintf(msg, sizeof(msg), "Cannot start updater thread (0x%08X)", (unsigned)rc);
        set_status(UPD_ERROR, 0.0f, msg);
        sceKernelDeleteThread(th);
        s_thread = -1;
        lock();
        s_busy = 0;
        unlock();
    }
}

/* ------------------------------------------------------------------ public API */

void updater_init(void)
{
    if (s_inited)
        return;
    if (sceKernelCreateLwMutex(&s_lock, "gv_updater_lock", 0, 0, NULL) < 0)
        return;
    s_state = UPD_IDLE;
    s_progress = 0.0f;
    s_message[0] = '\0';
    s_tag[0] = '\0';
    s_inited = 1;

    /* Network bring-up is done here so failures surface immediately; the worker retries
     * it (idempotent) in case it failed now, e.g. Wi-Fi was still off. */
    char err[MSG_CAP];
    if (net_http_init(err, sizeof(err)) != 0)
        set_status(UPD_ERROR, 0.0f, err);
}

void updater_start_check(void)   { start_job(JOB_CHECK); }
void updater_start_install(void) { start_job(JOB_INSTALL); }

UpdState updater_state(void)
{
    if (!s_inited)
        return UPD_IDLE;
    lock();
    UpdState st = s_state;
    unlock();
    return st;
}

const char *updater_message(void)
{
    if (!s_inited)
        return "";
    lock();
    memcpy(s_ui_message, s_message, sizeof(s_ui_message));
    unlock();
    return s_ui_message;
}

const char *updater_latest_tag(void)
{
    if (!s_inited)
        return "";
    lock();
    memcpy(s_ui_tag, s_tag, sizeof(s_ui_tag));
    unlock();
    return s_ui_tag;
}

float updater_progress(void)
{
    if (!s_inited)
        return 0.0f;
    lock();
    float p = s_progress;
    unlock();
    return p;
}

void updater_shutdown(void)
{
    if (!s_inited)
        return;
    lock();
    s_cancel = 1; /* aborts an in-flight download; the promoter step cannot be aborted */
    unlock();
    reap_thread();
    net_http_shutdown();
    sceKernelDeleteLwMutex(&s_lock);
    s_inited = 0;
    s_busy = 0;
}
