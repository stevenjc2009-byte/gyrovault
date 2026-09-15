#ifndef GV_UPDATER_H
#define GV_UPDATER_H

typedef enum {
    UPD_IDLE = 0,
    UPD_CHECKING,
    UPD_UP_TO_DATE,
    UPD_AVAILABLE,
    UPD_DOWNLOADING,
    UPD_INSTALLING,
    UPD_DONE,      /* installed; the user must restart Gyrovault */
    UPD_ERROR
} UpdState;

/* All calls are non-blocking; work runs on a background thread. Safe to call every frame. */
void        updater_init(void);          /* net + ssl module init, once */
void        updater_start_check(void);   /* releases/latest 302 -> tag vs GV_VERSION */
void        updater_start_install(void); /* only valid in UPD_AVAILABLE */
UpdState    updater_state(void);
const char *updater_message(void);       /* human-readable status / error text */
const char *updater_latest_tag(void);    /* "" until a check succeeds */
float       updater_progress(void);      /* 0..1 during download/install */
void        updater_shutdown(void);

#endif
